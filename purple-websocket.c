#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <cipher.h>
#include <sslconn.h>

#include "purple-websocket.h"

static const char WS_SALT[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

struct _PurpleWebsocket {
	PurpleWebsocketConnectCallback connect_cb;
	void *user_data;

	/* non-ssl: */
	PurpleProxyConnectData *connection;
	int fd;
	/* ssl: */
	PurpleSslConnection *ssl_connection;
	guint inpa;

	char *key;

	char *buffer;
	gsize buffer_off, buffer_len;
};

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
	g_free(ws->buffer);

	g_free(ws);
}

static void ws_connect_error(PurpleWebsocket *ws, const char *error) {
	ws->connect_cb(ws, ws->user_data, error);
	purple_websocket_cancel(ws);
}

static void wss_error_cb(G_GNUC_UNUSED PurpleSslConnection *ssl_connection, PurpleSslErrorType error, gpointer data) {
	PurpleWebsocket *ws = data;

	ws->ssl_connection = NULL;

	ws_connect_error(ws, purple_ssl_strerror(error));
}

static const char *skip_lws(const char *s) {
	while (s) {
		while (*s == ' ' || *s == '\t')
			s ++;
		if (s[0] == '\r' && s[1] == '\n') {
			s += 2;
			if (*s == ' ' || *s == '\t')
				s ++;
			else
				s = NULL;
		} else
			break;
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

static void ws_connect_recv_cb(gpointer data, G_GNUC_UNUSED gint source, G_GNUC_UNUSED PurpleInputCondition cond) {
	PurpleWebsocket *ws = data;
	char *eoh = NULL;

	do {
		if (ws->buffer_off >= ws->buffer_len) {
			if (ws->buffer_len >= 4096) {
				ws_connect_error(ws, "Response headers too long");
				return;
			}
			ws->buffer = g_realloc(ws->buffer, ws->buffer_len *= 2);
		}

		int len;
		if (ws->ssl_connection)
			len = purple_ssl_read(ws->ssl_connection, ws->buffer + ws->buffer_off, ws->buffer_len - ws->buffer_off);
		else
			len = read(ws->fd, ws->buffer + ws->buffer_off, ws->buffer_len - ws->buffer_off);

		if (len < 0) {
			if (errno != EAGAIN)
				ws_connect_error(ws, g_strerror(errno));
			return;
		}

		if (len == 0) {
			ws_connect_error(ws, "Connection closed reading response");
			return;
		}

		/* search for the end of headers in the new block, backing up 4-1 */
		eoh = g_strstr_len(ws->buffer + ws->buffer_off - 3, len + 3, "\r\n\r\n");
		ws->buffer_off += len;
	} while (!eoh);

	/* got all the headers now */
	*eoh = '\0';
	eoh += 4;

	const char *upgrade = skip_lws(find_header_content(ws->buffer, "Upgrade"));
	if (upgrade && (!g_ascii_strncasecmp(upgrade, "websocket", 9) || skip_lws(upgrade+9)))
		upgrade = NULL;

	const char *connection = find_header_content(ws->buffer, "Connection");
	while (connection && g_ascii_strncasecmp(connection, "Upgrade", 7))
		while ((connection = skip_lws(connection)) && *connection++ != ',');
	if (connection) {
		const char *e = skip_lws(connection+7);
		if (e && *e != ',')
			connection = NULL;
	}

	const char *accept = skip_lws(find_header_content(ws->buffer, "Sec-WebSocket-Accept"));
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

	if (strncmp(ws->buffer, "HTTP/1.1 101 ", 13) || !upgrade || !connection || !accept) {
		ws_connect_error(ws, ws->buffer);
		return;
	}

	memmove(ws->buffer, eoh, ws->buffer_off -= eoh - ws->buffer);

	ws->connect_cb(ws, ws->user_data, NULL);
	/* go! */
}

static void wss_connect_recv_cb(gpointer data, G_GNUC_UNUSED PurpleSslConnection *ssl_connection, PurpleInputCondition cond)
{
	ws_connect_recv_cb(data, -1, cond);
}


static void ws_connect_send_cb(gpointer data, G_GNUC_UNUSED gint source, G_GNUC_UNUSED PurpleInputCondition cond) {
	PurpleWebsocket *ws = data;

	int len;
	if (ws->ssl_connection)
		len = purple_ssl_write(ws->ssl_connection, ws->buffer + ws->buffer_off,
				ws->buffer_len - ws->buffer_off);
	else
		len = write(ws->fd, ws->buffer + ws->buffer_off,
				ws->buffer_len - ws->buffer_off);

	if (len < 0) {
		if (errno != EAGAIN)
			ws_connect_error(ws, g_strerror(errno));
		return;
	}

	ws->buffer_off += len;
	if (ws->buffer_off < ws->buffer_len)
		return;

	/* We're done writing our request, now start reading the response */
	purple_input_remove(ws->inpa);
	ws->buffer_off = 0;
	if (ws->ssl_connection) {
		ws->inpa = 0;
		purple_ssl_input_add(ws->ssl_connection, wss_connect_recv_cb, ws);
	} else {
		ws->inpa = purple_input_add(ws->fd, PURPLE_INPUT_READ, ws_connect_recv_cb, ws);
	}
}

static void wss_connect_cb(gpointer data, PurpleSslConnection *ssl_connection, G_GNUC_UNUSED PurpleInputCondition cond) {
	PurpleWebsocket *ws = data;

	ws->inpa = purple_input_add(ssl_connection->fd, PURPLE_INPUT_WRITE,
			ws_connect_send_cb, ws);
	ws_connect_send_cb(ws, ssl_connection->fd, PURPLE_INPUT_WRITE);
}

static void ws_connect_cb(gpointer data, gint source, const gchar *error_message) {
	PurpleWebsocket *ws = data;
	ws->connection = NULL;

	if (source == -1) {
		ws_connect_error(ws, error_message ?: "Unable to connect to websocket");
		return;
	}

	ws->fd = source;

	ws->inpa = purple_input_add(source, PURPLE_INPUT_WRITE,
			ws_connect_send_cb, ws);
	ws_connect_send_cb(ws, source, PURPLE_INPUT_WRITE);
}

PurpleWebsocket *
purple_websocket_connect(PurpleAccount *account,
		const char *url, const char *protocol,
		PurpleWebsocketConnectCallback connect_cb, void *user_data) {
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
	ws->connect_cb = connect_cb;
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

		ws->buffer_len = request->len;
		ws->buffer = g_string_free(request, FALSE);

		if (ssl)
			ws->ssl_connection = purple_ssl_connect(account, host, port,
					wss_connect_cb, wss_error_cb, ws);
		else
			ws->connection = purple_proxy_connect(NULL, account, host, port,
					ws_connect_cb, ws);
	}

	if (!(ws->ssl_connection || ws->connection)) {
		ws_connect_error(ws, "Unable to connect to websocket");
		return NULL;
	}

	return ws;
}
