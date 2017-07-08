#include "slack-api.h"
#include "slack-user.h"

void slack_user_free(SlackUser *user) {
	g_free(user->name);
	g_free(user);
}

static void user_update(SlackAccount *sa, json_value *json) {
	json_value *id = json_get_prop_type(json, "id", string);
	if (!id)
		return;
	json_value *deleted = json_get_prop_type(json, "deleted", boolean);
	if (deleted && deleted->u.boolean) {
		g_hash_table_remove(sa->users, id);
		return;
	}
	json_value *name = json_get_prop_type(json, "name", string);

	SlackUser *user = g_new0(SlackUser, 1);
	if (name) user->name = g_strdup(name->u.string.ptr);
	g_hash_table_replace(sa->users, g_strdup(id->u.string.ptr), user);
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

	purple_connection_set_state(sa->gc, PURPLE_CONNECTED);
}

void slack_users_get(SlackAccount *sa) {
	purple_connection_update_progress(sa->gc, "Loading Users", 4, SLACK_CONNECT_STEPS);
	slack_api_call(sa, "users.list", "presence=false", users_list_cb, sa);
}

static gboolean user_name_equal(char *id, SlackUser *user, const char *name) {
	return !g_strcmp0(user->name, name);
}

SlackUser *slack_user_find(SlackAccount *sa, const char *name) {
	return g_hash_table_find(sa->users, (GHRFunc)user_name_equal, (gpointer)name);
}
