#include "slack-api.h"
#include "slack-im.h"

void slack_im_free(SlackIM *im) {
	g_free(im->user);
	g_free(im);
}

static void im_update(SlackAccount *sa, json_value *json) {
	json_value *id = json_get_prop_type(json, "id", string) || json_get_prop_type(json, "channel", string);
	if (!id)
		return;
	json_value *is_open = json_get_prop_type(json, "is_open", boolean);
	if (is_open && !is_open->u.boolean) {
		g_hash_table_remove(sa->ims, id);
		return;
	}
	json_value *user = json_get_prop_type(json, "user", string);

	SlackIM *im = g_new0(SlackIM, 1);
	if (user) im->user = g_strdup(user->u.string.ptr);

	g_hash_table_replace(sa->ims, g_strdup(id->u.string.ptr), im);
}

void slack_im_closed(SlackAccount *sa, json_value *json) {
	json_value *id = json_get_prop_type(json, "channel", string);
	if (!id)
		return;
	g_hash_table_remove(sa->ims, id->u.string.ptr);
}

void slack_im_opened(SlackAccount *sa, json_value *json) {
	im_update(sa, json);
}

static void im_list_cb(SlackAPICall *api, gpointer data, json_value *json, const char *error) {
	SlackAccount *sa = data;

	json_value *ims = json_get_prop_type(json, "ims", array);
	if (!ims) {
		purple_connection_error_reason(sa->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR, error ?: "Missing IM channel list");
		return;
	}

	g_hash_table_remove_all(sa->ims);
	for (unsigned i = 0; i < ims->u.array.length; i ++)
		im_update(sa, ims->u.array.values[i]);

	purple_connection_set_state(sa->gc, PURPLE_CONNECTED);
}

void slack_ims_load(SlackAccount *sa) {
	purple_connection_update_progress(sa->gc, "Loading IM channels", 5, SLACK_CONNECT_STEPS);
	slack_api_call(sa, "im.list", NULL, im_list_cb, sa);
}
