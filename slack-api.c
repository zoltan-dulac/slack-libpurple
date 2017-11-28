#include <debug.h>

#include "slack-api.h"
#include "slack-json.h"
#include "slack-channel.h"
#include "slack-user.h"

PurpleConnectionError slack_api_connection_error(const gchar *error) {
	if (!g_strcmp0(error, "not_authed"))
		return PURPLE_CONNECTION_ERROR_INVALID_USERNAME;
	if (!g_strcmp0(error, "invalid_auth") ||
			!g_strcmp0(error, "account_inactive"))
		return PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED;
	return PURPLE_CONNECTION_ERROR_NETWORK_ERROR;
}

struct _SlackAPICall {
	SlackAccount *sa;
	PurpleUtilFetchUrlData *fetch;
	SlackAPICallback *callback;
	gpointer data;
};

static void api_error(SlackAPICall *call, const char *error) {
	if (call->callback)
		call->callback(call->sa, call->data, NULL, error);
	g_free(call);
};

static void api_cb(G_GNUC_UNUSED PurpleUtilFetchUrlData *fetch, gpointer data, const gchar *buf, gsize len, const gchar *error) {
	SlackAPICall *call = data;

	purple_debug_misc("slack", "api response: %s\n", error ?: buf);
	if (error) {
		api_error(call, error);
		return;
	}

	json_value *json = json_parse(buf, len);
	if (!json) {
		api_error(call, "Invalid JSON response");
		return;
	}

	if (!json_get_prop_boolean(json, "ok", FALSE)) {
		const char *err = json_get_prop_strptr(json, "error");
		api_error(call, err ?: "Unknown error");
		call = NULL;
	} else if (call->callback) {
		call->callback(call->sa, call->data, json, NULL);
	}

	json_value_free(json);
	g_free(call);
}

static GString *slack_api_encode_url(SlackAccount *sa, const char *pfx, const char *method, va_list qargs) {
	GString *url = g_string_new(NULL);
	g_string_printf(url, "%s/%s%s?token=%s", sa->api_url, pfx, method, sa->token);

	const char *param;
	while ((param = va_arg(qargs, const char*))) {
		const char *val = va_arg(qargs, const char*);
		g_string_append_printf(url, "&%s=%s", param, purple_url_encode(val));
	}

	return url;
}

static void slack_api_call_url(SlackAccount *sa, SlackAPICallback callback, gpointer user_data, const char *url) {
	SlackAPICall *call = g_new0(SlackAPICall, 1);
	call->sa = sa;
	call->callback = callback;
	call->data = user_data;

	purple_debug_misc("slack", "api call: %s\n", url);
	call->fetch = purple_util_fetch_url_request_len_with_account(sa->account,
			url, TRUE, NULL, TRUE, NULL, FALSE, 4096*1024,
			api_cb, call);
}

void slack_api_call(SlackAccount *sa, SlackAPICallback callback, gpointer user_data, const char *method, ...)
{
	va_list qargs;
	va_start(qargs, method);
	GString *url = slack_api_encode_url(sa, "", method, qargs);
	va_end(qargs);

	slack_api_call_url(sa, callback, user_data, url->str);
	g_string_free(url, TRUE);
}

gboolean slack_api_channel_call(SlackAccount *sa, SlackAPICallback callback, gpointer user_data, SlackObject *obj, const char *method, ...) {
	g_return_val_if_fail(obj, FALSE);
	const char *type = NULL, *id = NULL;
	if (SLACK_IS_CHANNEL(obj)) {
		SlackChannel *chan = (SlackChannel*)obj;
		switch (chan->type) {
			case SLACK_CHANNEL_MEMBER:
				type = "channels.";
				break;
			case SLACK_CHANNEL_GROUP:
				type = "groups.";
				break;
			case SLACK_CHANNEL_MPIM:
				type = "mpim.";
				break;
			default:
				break;
		}
		id = chan->object.id;
	} else if (SLACK_IS_USER(obj)) {
		SlackUser *user = (SlackUser*)obj;
		if (*user->im) {
			type = "im.";
			id = user->im;
		}
	}

	if (!type || !id)
		return FALSE;

	va_list qargs;
	va_start(qargs, method);
	GString *url = slack_api_encode_url(sa, type, method, qargs);
	va_end(qargs);
	g_string_append_printf(url, "&channel=%s", purple_url_encode(id));

	slack_api_call_url(sa, callback, user_data, url->str);
	g_string_free(url, TRUE);
	return TRUE;
}
