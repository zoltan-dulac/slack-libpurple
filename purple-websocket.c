#include <unistd.h>
#include <errno.h>

#include <sslconn.h>

#include "purple-websocket.h"

struct _PurpleWebsocket {
	PurpleWebsocketConnectCallback connect_cb;
	void *user_data;

	/* non-ssl: */
	PurpleProxyConnectData *connection;
	int fd;
	/* ssl: */
	PurpleSslConnection *ssl_connection;

	char *request;
	gsize request_len;
	gsize request_written;

	guint inpa;
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

	g_free(ws->request);

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

static void ws_connect_recv_cb(gpointer data, gint source, PurpleInputCondition cond) {
	PurpleWebsocket *ws = data;
	/* TODO */
}

static void wss_connect_recv_cb(gpointer data, G_GNUC_UNUSED PurpleSslConnection *ssl_connection, PurpleInputCondition cond)
{
	ws_connect_recv_cb(data, -1, cond);
}


static void ws_connect_send_cb(gpointer data, G_GNUC_UNUSED gint source, G_GNUC_UNUSED PurpleInputCondition cond) {
	PurpleWebsocket *ws = data;

	int len;
	if (ws->ssl_connection)
		len = purple_ssl_write(ws->ssl_connection, ws->request + ws->request_written,
				ws->request_len - ws->request_written);
	else
		len = write(ws->fd, ws->request + ws->request_written,
				ws->request_len - ws->request_written);

	if (len < 0) {
		if (errno != EAGAIN)
			ws_connect_error(ws, g_strerror(errno));
		return;
	}

	ws->request_written += len;
	if (ws->request_written < ws->request_len)
		return;

	/* We're done writing our request, now start reading the response */
	purple_input_remove(ws->inpa);
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
		gchar *ekey = g_base64_encode((guchar*)key, 16);

		GString *request = g_string_new(NULL);
		g_string_printf(request, "\
GET %s HTTP/1.1\r\n\
Host: %s\r\n\
Connection: Upgrade\r\n\
Upgrade: websocket\r\n\
Sec-WebSocket-Key: %s\r\n\
Sec-WebSocket-Version: 13\r\n", path, host, ekey);
		if (protocol)
			g_string_append_printf(request, "Sec-WebSocket-Protocol: %s\r\n", protocol);
		g_string_append(request, "\r\n");
		g_free(ekey);

		ws->request_len = request->len;
		ws->request = g_string_free(request, FALSE);

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
