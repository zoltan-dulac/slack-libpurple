#ifndef _PURPLE_WEBSOCKET_H_
#define _PURPLE_WEBSOCKET_H_

#include <glib.h>

typedef struct _PurpleWebsocket PurpleWebsocket;

typedef void (*PurpleWebsocketCallback)(PurpleWebsocket *ws, gpointer user_data, const gchar *error_message);

#endif
