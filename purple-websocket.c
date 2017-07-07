#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <cipher.h>
#include <sslconn.h>

#include "purple-websocket.h"

static const char WS_SALT[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

struct buffer {
	char *buf;
	gsize off; /* next byte to read/write to */
	gsize len; /* (expected) size of data in buffer */
	gsize siz; /* allocated size of buffer */
};

struct _PurpleWebsocket {
	PurpleWebsocketCallback callback;
	void *user_data;

	char *key;

	PurpleProxyConnectData *connection;
	PurpleSslConnection *ssl_connection;

	int fd;
	guint inpa;

	struct buffer input, output;

	gboolean connected;
};

static void buffer_alloc(struct buffer *b, size_t n) {
	if (n > b->siz) {
		b->buf = g_realloc(b->buf, n);
		b->siz = n;
	}
}

static void
purple_websocket_cancel(PurpleWebsocket *ws) {
	if (ws->ssl_connection != NULL)
		purple_ssl_close(ws->ssl_connection);

	if (ws->connection != NULL)
		purple_proxy_connect_cancel(ws->connection);

	if (ws->inpa > 0)
		purple_input_remove(ws->inpa);

	if (ws->fd >= 0)
		close(ws->fd);

	g_free(ws->key);
	g_free(ws->output.buf);
	g_free(ws->input.buf);

	g_free(ws);
}

static void ws_error(PurpleWebsocket *ws, const char *error) {
	ws->callback(ws, ws->user_data, error);
	purple_websocket_cancel(ws);
}

static const char *skip_lws(const char *s) {
	while (s) {
		switch (*s) {
			case ' ':
			case '\t':
				s++;
				break;
			case '\r':
				if (s[1] == '\n' && (s[2] == ' ' || s[2] == '\t')) {
					s += 3;
					break;
				}
			case '\n':
			case '\0':
				return NULL;
			default:
				return s;
		}
	}
	return s;
}

static const char *find_header_content(const char *data, const char *name) {
	int nlen = strlen(name);
	const char *p = data;

	while ((p = strstr(p, "\r\n"))) {
		p += 2;
		if (!g_ascii_strncasecmp(p, name, nlen) && p[nlen] == ':')
			return &p[nlen+1];
	}
	return NULL;
}

static void ws_headers(PurpleWebsocket *ws, const char *headers) {
	const char *upgrade = skip_lws(find_header_content(headers, "Upgrade"));
	if (upgrade && (!g_ascii_strncasecmp(upgrade, "websocket", 9) || skip_lws(upgrade+9)))
		upgrade = NULL;

	const char *connection = find_header_content(headers, "Connection");
	while (connection && g_ascii_strncasecmp(connection, "Upgrade", 7))
		while ((connection = skip_lws(connection)) && *connection++ != ',');
	if (connection) {
		const char *e = skip_lws(connection+7);
		if (e && *e != ',')
			connection = NULL;
	}

	const char *accept = skip_lws(find_header_content(headers, "Sec-WebSocket-Accept"));
	if (accept) {
		char *k = g_strjoin(NULL, ws->key, WS_SALT, NULL);
		size_t l = 20;
		guchar s[l];
		g_warn_if_fail(purple_cipher_digest_region("sha1", (guchar *)k, strlen(k), l, s, &l));
		g_free(k);
		gchar *b = g_base64_encode(s, l);
		if (strcmp(accept, b))
			accept = NULL;
		g_free(b);
	}

	/* TODO: Sec-WebSocket-Extensions, Sec-WebSocket-Protocol */

	if (strncmp(headers, "HTTP/1.1 101 ", 13) || !upgrade || !connection || !accept) {
		ws_error(ws, headers);
		return;
	}

	ws->connected = TRUE;
	ws->callback(ws, ws->user_data, NULL);
}

static void ws_input_cb(gpointer data, gint source, PurpleInputCondition cond);

static void ws_next(PurpleWebsocket *ws) {
	if (ws->inpa)
		purple_input_remove(ws->inpa);
	ws->inpa = 0;

	if (ws->output.off) {
		/* always none left in practice: */
		memmove(ws->output.buf, ws->output.buf + ws->output.off, ws->output.len -= ws->output.off);
		ws->output.off = 0;
	}

	PurpleInputCondition cond
		= (ws->ssl_connection ? 0 : PURPLE_INPUT_READ) /* permanent purple_ssl_input_add for ssl */
		| (ws->output.len ? PURPLE_INPUT_WRITE : 0);
	if (cond)
		ws->inpa = purple_input_add(ws->fd, cond, ws_input_cb, ws);
}

static void ws_input_cb(gpointer data, G_GNUC_UNUSED gint source, PurpleInputCondition cond) {
	PurpleWebsocket *ws = data;

	if (cond & PURPLE_INPUT_WRITE) {
		int len;
		if (ws->ssl_connection)
			len = purple_ssl_write(ws->ssl_connection, ws->output.buf + ws->output.off, ws->output.len - ws->output.off);
		else
			len = write(ws->fd, ws->output.buf + ws->output.off, ws->output.len - ws->output.off);

		if (len < 0) {
			if (errno != EAGAIN) {
				ws_error(ws, g_strerror(errno));
				return;
			}
		} else if ((ws->output.off += len) >= ws->output.len)
			ws_next(ws);
	}

	if (cond & PURPLE_INPUT_READ) {
		int len;
		if (ws->ssl_connection)
			len = purple_ssl_read(ws->ssl_connection, ws->input.buf + ws->input.off, ws->input.len - ws->input.off);
		else
			len = read(ws->fd, ws->input.buf + ws->input.off, ws->input.len - ws->input.off);

		if (len < 0) {
			if (errno != EAGAIN) {
				ws_error(ws, g_strerror(errno));
				return;
			}
		}
		else if (len == 0) {
			ws_error(ws, "Connection closed");
			return;
		} else {
			ws->input.off += len;

			if (!ws->connected) {
				/* search for the end of headers in the new block (backing up 4-1) */
				char *eoh = g_strstr_len(ws->input.buf + ws->input.off - len - 3, len + 3, "\r\n\r\n");

				if (eoh) {
					/* got all the headers now */
					*eoh = '\0';
					eoh += 4;
					ws_headers(ws, ws->input.buf);

					memmove(ws->input.buf, eoh, ws->input.off -= eoh - ws->input.buf);
					/* TODO next */
				}
				else if (ws->input.off >= ws->input.len) {
					ws_error(ws, "Response headers too long");
					return;
				}
			} else if (ws->input.off >= ws->input.len) {
				/* TODO */
			}
		}
	}
}

static void wss_input_cb(gpointer data, G_GNUC_UNUSED PurpleSslConnection *ssl_connection, PurpleInputCondition cond)
{
	PurpleWebsocket *ws = data;
	ws_input_cb(data, ws->fd, cond);
}

static void wss_connect_cb(gpointer data, PurpleSslConnection *ssl_connection, G_GNUC_UNUSED PurpleInputCondition cond) {
	PurpleWebsocket *ws = data;

	ws->fd = ssl_connection->fd;
	purple_ssl_input_add(ws->ssl_connection, wss_input_cb, ws);

	ws_next(ws);
	ws_input_cb(ws, ws->fd, PURPLE_INPUT_WRITE);
}

static void wss_error_cb(G_GNUC_UNUSED PurpleSslConnection *ssl_connection, PurpleSslErrorType error, gpointer data) {
	PurpleWebsocket *ws = data;
	ws->ssl_connection = NULL;
	ws_error(ws, purple_ssl_strerror(error));
}

static void ws_connect_cb(gpointer data, gint source, const gchar *error_message) {
	PurpleWebsocket *ws = data;
	ws->connection = NULL;

	if (source == -1) {
		ws_error(ws, error_message ?: "Unable to connect to websocket");
		return;
	}

	ws->fd = source;

	ws_next(ws);
	ws_input_cb(ws, ws->fd, PURPLE_INPUT_WRITE);
}

PurpleWebsocket *
purple_websocket_connect(PurpleAccount *account,
		const char *url, const char *protocol,
		PurpleWebsocketCallback callback, void *user_data) {
	gboolean ssl = FALSE;

	if (!g_ascii_strcasecmp(url, "ws://")) {
		ssl = FALSE;
		url += 5;
	}
	else if (!g_ascii_strcasecmp(url, "wss://")) {
		ssl = TRUE;
		url += 6;
	}
	if (!g_ascii_strcasecmp(url, "http://")) {
		ssl = FALSE;
		url += 7;
	}
	if (!g_ascii_strcasecmp(url, "https://")) {
		ssl = TRUE;
		url += 8;
	}

	PurpleWebsocket *ws = g_new0(PurpleWebsocket, 1);
	ws->callback = callback;
	ws->user_data = user_data;
	ws->fd = -1;

	char *host, *path;
	int port;
	if (!purple_url_parse(url, &host, &port, &path, NULL, NULL));
	else {
		guint32 key[4] = {
			g_random_int(),
			g_random_int(),
			g_random_int(),
			g_random_int()
		};
		ws->key = g_base64_encode((guchar*)key, 16);

		GString *request = g_string_new(NULL);
		g_string_printf(request, "\
GET %s HTTP/1.1\r\n\
Host: %s\r\n\
Connection: Upgrade\r\n\
Upgrade: websocket\r\n\
Sec-WebSocket-Key: %s\r\n\
Sec-WebSocket-Version: 13\r\n", path, host, ws->key);
		if (protocol)
			g_string_append_printf(request, "Sec-WebSocket-Protocol: %s\r\n", protocol);
		g_string_append(request, "\r\n");

		ws->output.len = request->len;
		ws->output.siz = request->allocated_len;
		ws->output.buf = g_string_free(request, FALSE);

		/* allocate space for responses (headers) */
		buffer_alloc(&ws->input, 4096);
		ws->input.len = ws->input.siz;

		if (ssl)
			ws->ssl_connection = purple_ssl_connect(account, host, port,
					wss_connect_cb, wss_error_cb, ws);
		else
			ws->connection = purple_proxy_connect(NULL, account, host, port,
					ws_connect_cb, ws);
	}

	if (!(ws->ssl_connection || ws->connection)) {
		ws_error(ws, "Unable to connect to websocket");
		return NULL;
	}

	return ws;
}
