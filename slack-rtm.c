#include <string.h>

#include <debug.h>

#include "slack-json.h"
#include "slack-api.h"
#include "slack-rtm.h"
#include "slack-user.h"

static void rtm_msg(SlackAccount *sa, const char *type, json_value *json) {
	if (!strcmp(type, "hello")) {
		slack_get_users(sa);
	}
	else if (!strcmp(type, "user_changed") ||
		 !strcmp(type, "team_join")) {
		slack_user_changed(sa, json);
	}
	else {
		purple_debug_info("slack", "Unhandled RTM type %s\n", type);
	}
}

static void rtm_cb(PurpleWebsocket *ws, gpointer data, PurpleWebsocketOp op, const guchar *msg, size_t len) {
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
			purple_connection_update_progress(sa->gc, "RTM Connected", 3, SLACK_CONNECT_STEPS);
		default:
			return;
	}

	json_value *json = json_parse((const char *)msg, len);
	json_value *type = json_get_prop_type(json, "type", string);
	if (!type)
	{
		purple_debug_error("slack", "RTM: %.*s\n", (int)len, msg);
		purple_connection_error_reason(sa->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
				"Could not parse RTM JSON");
	} else
		rtm_msg(sa, type->u.string.ptr, json);

	json_value_free(json);
}

static void rtm_connect_cb(SlackAPICall *api, gpointer data, json_value *json, const char *error) {
	SlackAccount *sa = data;

	if (sa->rtm) {
		purple_websocket_abort(sa->rtm);
		sa->rtm = NULL;
	}

	json_value *url = json_get_prop_type(json, "url", string);
	json_value *self = json_get_prop_type(json, "self", object);
	json_value *self_id = json_get_prop_type(self, "id", string);

	if (!url || !self_id) {
		purple_connection_error_reason(sa->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR, error ?: "Missing RTM parameters");
		return;
	}

#define SET_STR(FIELD, JSON, PROP) ({ \
		json_value *_j = json_get_prop_type(JSON, PROP, string); \
		g_free(sa->FIELD); \
		sa->FIELD = g_strdup(_j ? _j->u.string.ptr : NULL); \
	})

	SET_STR(self, self, "id");

	json_value *self_name = json_get_prop_type(self, "name", string);
	if (self_name)
		purple_connection_set_display_name(sa->gc, self_name->u.string.ptr);

	json_value *team = json_get_prop_type(json, "team", object);
	SET_STR(team.id, team, "id");
	SET_STR(team.name, team, "name");
	SET_STR(team.domain, team, "domain");

#undef SET_STR

	purple_connection_update_progress(sa->gc, "Connecting to RTM", 2, SLACK_CONNECT_STEPS);
	purple_debug_info("slack", "RTM URL: %s\n", url->u.string.ptr);
	sa->rtm = purple_websocket_connect(sa->account, url->u.string.ptr, NULL, rtm_cb, sa);
}

void slack_rtm_connect(SlackAccount *sa) {
	purple_connection_update_progress(sa->gc, "Requesting RTM", 1, SLACK_CONNECT_STEPS);
	slack_api_call(sa, "rtm.connect", "batch_presence_aware=1", rtm_connect_cb, sa);
}
