#include "slack-api.h"
#include "slack-im.h"

G_DEFINE_TYPE(SlackIM, slack_im, SLACK_TYPE_OBJECT);

static void slack_im_dispose(GObject *gobj) {
	SlackIM *im = SLACK_IM(gobj);

	g_clear_object(&im->user);

	G_OBJECT_CLASS(slack_im_parent_class)->dispose(gobj);
}

static void slack_im_class_init(SlackIMClass *klass) {
	GObjectClass *gobj = G_OBJECT_CLASS(klass);
	gobj->dispose = slack_im_dispose;
}

static void slack_im_init(SlackIM *self) {
}

static void im_update(SlackAccount *sa, json_value *json) {
	json_value *id = json_get_prop_type(json, "id", string);
	if (!id)
		id = json_get_prop_type(json, "channel", string);
	if (!id)
		return;

	json_value *is_open = json_get_prop_type(json, "is_open", boolean);
	if (is_open && !is_open->u.boolean) {
		slack_object_hash_table_remove(sa->ims, id->u.string.ptr);
		return;
	}

	SlackIM *im = (SlackIM*)slack_object_hash_table_get(sa->ims, SLACK_TYPE_IM, id->u.string.ptr);

	json_value *user_id = json_get_prop_type(json, "user", string);
	if (user_id && !(im->user && slack_object_has_id(&im->user->object, user_id->u.string.ptr))) {
		g_clear_object(&im->user);
		SlackUser *user = (SlackUser*)slack_object_hash_table_lookup(sa->ims, user_id->u.string.ptr);
		if (user)
			im->user = g_object_ref(user);
	}
}

void slack_im_closed(SlackAccount *sa, json_value *json) {
	json_value *id = json_get_prop_type(json, "channel", string);
	if (!id)
		return;
	slack_object_hash_table_remove(sa->ims, id->u.string.ptr);
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
