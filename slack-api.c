#include "slack-api.h"
#include "slack-json.h"

struct _SlackAPICall {
	SlackAccount *sa;
	PurpleUtilFetchUrlData *fetch;
	SlackAPICallback *callback;
	gpointer data;
};

static void api_error(SlackAPICall *call, const char *error) {
	call->callback(call->sa, call->data, NULL, error);
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
		const char *err = json_get_prop_strptr(json, "error");
		api_error(call, err ?: "Unknown error");
	} else {
		call->callback(call->sa, call->data, json, NULL);
	}

	json_value_free(json);
	g_free(call);
}

void slack_api_call(SlackAccount *sa, SlackAPICallback callback, gpointer user_data, const char *method, ...)
{

	SlackAPICall *call = g_new0(SlackAPICall, 1);
	call->sa = sa;
	call->callback = callback;
	call->data = user_data;

	GString *url = g_string_new(NULL);
	g_string_printf(url, "%s/%s?token=%s", sa->api_url, method, sa->token);

	va_list qargs;
	va_start(qargs, method);
	const char *param;
	while ((param = va_arg(qargs, const char*))) {
		const char *val = va_arg(qargs, const char*);
		g_string_append_printf(url, "&%s=%s", param, purple_url_encode(val));
	}
	va_end(qargs);

	call->fetch = purple_util_fetch_url_request_data_len_with_account(sa->account,
			url->str, TRUE, NULL, TRUE, NULL, 0, FALSE, 4096*1024,
			api_cb, call);
	g_string_free(url, TRUE);
}
