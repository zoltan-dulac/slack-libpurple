#include <string.h>

#include <debug.h>

#include "slack-json.h"
#include "slack-api.h"
#include "slack-user.h"
#include "slack-im.h"
#include "slack-blist.h"
#include "slack-message.h"
#include "slack-rtm.h"

struct _SlackRTMCall {
	SlackAccount *sa;
	SlackRTMCallback *callback;
	gpointer data;
};

static void rtm_msg(SlackAccount *sa, const char *type, json_value *json) {
	if (!strcmp(type, "message")) {
		slack_message(sa, json);
	}
	else if (!strcmp(type, "presence_change") ||
	         !strcmp(type, "presence_change_batch")) {
		slack_presence_change(sa, json);
	}
	else if (!strcmp(type, "im_closed")) {
		slack_im_closed(sa, json);
	}
	else if (!strcmp(type, "im_open")) {
		slack_im_opened(sa, json);
	}
	else if (!strcmp(type, "user_changed") ||
		 !strcmp(type, "team_join")) {
		slack_user_changed(sa, json);
	}
	else if (!strcmp(type, "hello")) {
		slack_users_load(sa);
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
	json_value *reply_to = json_get_prop_type(json, "reply_to", integer);
	const char *type = json_get_prop_strptr(json, "type");

	if (reply_to) {
		SlackRTMCall *call = g_hash_table_lookup(sa->rtm_call, GUINT_TO_POINTER(reply_to->u.integer));
		if (call) {
			g_hash_table_steal(sa->rtm_call, GUINT_TO_POINTER(reply_to->u.integer));
			json_value *ok = json_get_prop_type(json, "ok", boolean);
			if (!ok || !ok->u.boolean) {
				json_value *err = json_get_prop(json, "error");
				if (err->type == json_object)
					err = json_get_prop(err, "msg");
				err = json_get_type(err, string);
				call->callback(call->sa, call->data, json, err ? err->u.string.ptr : "Unknown error");
			} else
				call->callback(call->sa, call->data, json, NULL);
			g_free(call);
		}
	}
	else if (type)
		rtm_msg(sa, type, json);
	else {
		purple_debug_error("slack", "RTM: %.*s\n", (int)len, msg);
		purple_connection_error_reason(sa->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR,
				"Could not parse RTM JSON");
	}

	json_value_free(json);
}

static void rtm_connect_cb(SlackAccount *sa, gpointer data, json_value *json, const char *error) {

	if (sa->rtm) {
		purple_websocket_abort(sa->rtm);
		sa->rtm = NULL;
	}

	const char *url     = json_get_prop_strptr(json, "url");
	json_value *self    = json_get_prop_type(json, "self", object);
	const char *self_id = json_get_prop_strptr(self, "id");

	if (!url || !self_id) {
		purple_connection_error_reason(sa->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR, error ?: "Missing RTM parameters");
		return;
	}

#define SET_STR(FIELD, JSON, PROP) ({ \
		const char *_j = json_get_prop_strptr(JSON, PROP); \
		g_free(sa->FIELD); \
		sa->FIELD = g_strdup(_j); \
	})

	SET_STR(self, self, "id");

	const char *self_name = json_get_prop_strptr(self, "name");
	if (self_name)
		purple_connection_set_display_name(sa->gc, self_name);

	json_value *team = json_get_prop_type(json, "team", object);
	SET_STR(team.id, team, "id");
	SET_STR(team.name, team, "name");
	SET_STR(team.domain, team, "domain");

#undef SET_STR

	/* now that we have team info... */
	slack_blist_init(sa);

	purple_connection_update_progress(sa->gc, "Connecting to RTM", 2, SLACK_CONNECT_STEPS);
	purple_debug_info("slack", "RTM URL: %s\n", url);
	sa->rtm = purple_websocket_connect(sa->account, url, NULL, rtm_cb, sa);
}

void slack_rtm_cancel(SlackRTMCall *call) {
	/* Called from sa->rtm_call value destructor: perhaps should be more explicit */
	call->callback(call->sa, call->data, NULL, NULL);
	g_free(call);
}

void slack_rtm_send(SlackAccount *sa, SlackRTMCallback *callback, gpointer user_data, const char *type, ...) {
	gulong id = ++sa->rtm_id;

	GString *json = g_string_new(NULL);
	g_string_printf(json, "{\"id\":%lu,\"type\":\"%s\"", id, type);
	va_list qargs;
	va_start(qargs, type);
	const char *key;
	while ((key = va_arg(qargs, const char*))) {
		const char *val = va_arg(qargs, const char*);
		g_string_append_printf(json, ",\"%s\":%s", key, val);
	}
	va_end(qargs);
	g_string_append_c(json, '}');
	g_return_if_fail(json->len > 0 && json->len <= 16384);

	purple_debug_misc("slack", "RTM: %.*s\n", (int)json->len, json->str);

	if (callback) {
		SlackRTMCall *call = g_new(SlackRTMCall, 1);
		call->sa = sa;
		call->callback = callback;
		call->data = user_data;
		g_hash_table_insert(sa->rtm_call, GUINT_TO_POINTER(id), call);
	}

	purple_websocket_send(sa->rtm, PURPLE_WEBSOCKET_TEXT, (guchar*)json->str, json->len);
	g_string_free(json, TRUE);
}

void slack_rtm_connect(SlackAccount *sa) {
	purple_connection_update_progress(sa->gc, "Requesting RTM", 1, SLACK_CONNECT_STEPS);
	slack_api_call(sa, rtm_connect_cb, NULL, "rtm.connect", "batch_presence_aware", "1", "presence_sub", "true", NULL);
}
