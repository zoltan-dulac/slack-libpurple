#include <string.h>

#include <debug.h>

#include "slack-json.h"
#include "slack-api.h"
#include "slack-rtm.h"

#define CONNECT_STEPS 4

static void slack_rtm_cb(PurpleWebsocket *ws, gpointer data, PurpleWebsocketOp op, const guchar *msg, size_t len) {
	SlackAccount *sa = data;

	purple_debug_misc("slack", "RTM %x: %.*s\n", op, (int)len, msg);
	switch (op) {
		case PURPLE_WEBSOCKET_TEXT:
			break;
		case PURPLE_WEBSOCKET_ERROR:
		case PURPLE_WEBSOCKET_CLOSE:
			purple_connection_error_reason(sa->gc,
					PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
					(const char *)msg ?: "RTM connection closed");
			sa->rtm = NULL;
			break;
		case PURPLE_WEBSOCKET_OPEN:
			purple_connection_update_progress(sa->gc, "RTM Connected", 3, CONNECT_STEPS);
		default:
			return;
	}

	json_value *json = json_parse((const char *)msg, len);
	json_value *type = json_get_prop(json, "type");
	if (!type || type->type != json_string)
	{
		purple_debug_error("slack", "RTM: %.*s\n", (int)len, msg);
		purple_connection_error_reason(sa->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
				"Could not parse RTM JSON");
		return;
	}

	if (!strcmp("hello", type->u.string.ptr)) {
		purple_connection_set_state(sa->gc, PURPLE_CONNECTED);
	}
}

static void rtm_connect_cb(SlackAPICall *api, gpointer data, json_value *json, const char *error) {
	SlackAccount *sa = data;

	json_value *url = json_get_prop(json, "url");
	if (!url || url->type != json_string) {
		purple_connection_error_reason(sa->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR, error ?: "Missing RTM URL");
		return;
	}

	purple_connection_update_progress(sa->gc, "Connecting to RTM", 2, CONNECT_STEPS);
	purple_debug_info("slack", "RTM URL: %s\n", url->u.string.ptr);
	sa->rtm = purple_websocket_connect(sa->account, url->u.string.ptr, NULL, slack_rtm_cb, sa);
}

void slack_rtm_connect(SlackAccount *sa) {
	purple_connection_update_progress(sa->gc, "Requesting RTM", 1, CONNECT_STEPS);
	slack_api_call(sa, "rtm.connect", NULL, rtm_connect_cb, sa);
}
