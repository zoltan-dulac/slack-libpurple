#include "slack-api.h"
#include "slack-json.h"

struct _SlackAPICall {
	SlackAccount *sa;
	PurpleUtilFetchUrlData *fetch;
	SlackAPICallback callback;
	gpointer user_data;
};

static void api_error(SlackAPICall *call, const char *error) {
	call->callback(call, call->user_data, NULL, error);
	g_free(call);
};

static void api_cb(G_GNUC_UNUSED PurpleUtilFetchUrlData *fetch, gpointer data, const gchar *buf, gsize len, const gchar *error) {
	SlackAPICall *call = data;
	if (error) {
		api_error(call, error);
		return;
	}

	json_value *json = json_parse(buf, len);
	if (!json) {
		api_error(call, "Invalid JSON response");
		return;
	}

	json_value *ok = json_get_prop_type(json, "ok", boolean);
	if (!ok || !ok->u.boolean) {
		json_value *err = json_get_prop_type(json, "error", string);
		api_error(call, err ? err->u.string.ptr : "Unknown error");
	} else {
		call->callback(call, call->user_data, json, NULL);
	}

	json_value_free(json);
	g_free(call);
}

SlackAPICall *slack_api_call(SlackAccount *sa, const char *method, const char *query, SlackAPICallback callback, gpointer data)
{
	SlackAPICall *call = g_new0(SlackAPICall, 1);
	call->callback = callback;
	call->user_data = data;

	GString *url = g_string_new(NULL);
	g_string_printf(url, "%s/%s?token=%s", sa->api_url, method, sa->token);
	if (query) {
		g_string_append_c(url, '&');
		g_string_append(url, query);
	}

	call->fetch = purple_util_fetch_url_request_data_len_with_account(sa->account,
			url->str, TRUE, NULL, TRUE, NULL, 0, FALSE, -1,
			api_cb, call);
	g_string_free(url, TRUE);

	return call;
}
