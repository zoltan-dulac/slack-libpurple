#include <debug.h>

#include "slack-api.h"
#include "slack-user.h"
#include "slack-im.h"

G_DEFINE_TYPE(SlackUser, slack_user, SLACK_TYPE_OBJECT);

static void slack_user_finalize(GObject *gobj) {
	SlackUser *user = SLACK_USER(gobj);

	// purple_debug_misc("slack", "freeing user %s\n", user->object.id);
	g_free(user->name);

	G_OBJECT_CLASS(slack_user_parent_class)->finalize(gobj);
}

static void slack_user_class_init(SlackUserClass *klass) {
	GObjectClass *gobj = G_OBJECT_CLASS(klass);
	gobj->finalize = slack_user_finalize;
}

static void slack_user_init(SlackUser *self) {
}

static void user_update(SlackAccount *sa, json_value *json) {
	json_value *id = json_get_prop_type(json, "id", string);
	if (!id)
		return;

	json_value *deleted = json_get_prop_type(json, "deleted", boolean);
	if (deleted && deleted->u.boolean) {
		slack_object_hash_table_remove(sa->users, id->u.string.ptr);
		return;
	}

	SlackUser *user = (SlackUser*)slack_object_hash_table_get(sa->users, SLACK_TYPE_USER, id->u.string.ptr);

	json_value *name = json_get_prop_type(json, "name", string);

	if (name) {
		g_free(user->name);
		user->name = g_strdup(name->u.string.ptr);
		purple_debug_misc("slack", "user %s: %s\n", id->u.string.ptr, name->u.string.ptr);
	} else
		purple_debug_warning("slack", "user %s missing name\n", id->u.string.ptr);

}

void slack_user_changed(SlackAccount *sa, json_value *json) {
	user_update(sa, json_get_prop(json, "user"));
}

static void users_list_cb(SlackAPICall *api, gpointer data, json_value *json, const char *error) {
	SlackAccount *sa = data;

	json_value *members = json_get_prop_type(json, "members", array);
	if (!members) {
		purple_connection_error_reason(sa->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR, error ?: "Missing user list");
		return;
	}

	g_hash_table_remove_all(sa->users);
	for (unsigned i = 0; i < members->u.array.length; i ++)
		user_update(sa, members->u.array.values[i]);

	slack_ims_load(sa);
}

void slack_users_load(SlackAccount *sa) {
	purple_connection_update_progress(sa->gc, "Loading Users", 4, SLACK_CONNECT_STEPS);
	slack_api_call(sa, "users.list", "presence=false", users_list_cb, sa);
}

static gboolean user_name_equal(char *id, SlackUser *user, const char *name) {
	return !g_strcmp0(user->name, name);
}

SlackUser *slack_user_find(SlackAccount *sa, const char *name) {
	return g_hash_table_find(sa->users, (GHRFunc)user_name_equal, (gpointer)name);
}

static void presence_set(SlackAccount *sa, json_value *json, const char *presence) {
	if (json->type != json_string)
		return;
	const char *id = json->u.string.ptr;
	SlackUser *user = SLACK_USER(slack_object_hash_table_lookup(sa->users, id));
	if (!user || !user->name)
		return;
	purple_debug_misc("slack", "setting user %s presence to %s\n", user->name, presence);
	purple_prpl_got_user_status(sa->account, user->name, presence, NULL);
}

void slack_presence_change(SlackAccount *sa, json_value *json) {
	json_value *users = json_get_prop(json, "users");
	if (!users)
		users = json_get_prop(json, "user");
	json_value *presence = json_get_prop_type(json, "presence", string);
	if (!users || !presence)
		return;

	if (users->type == json_array)
		for (unsigned i = 0; i < users->u.array.length; i ++)
			presence_set(sa, users->u.array.values[i], presence->u.string.ptr);
	else
		presence_set(sa, users, presence->u.string.ptr);
}

