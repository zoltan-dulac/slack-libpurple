#include <debug.h>

#include "slack-json.h"
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

static gboolean user_update(SlackAccount *sa, json_value *json) {
	const char *sid = json_get_prop_strptr(json, "id");
	if (!sid)
		return FALSE;
	slack_object_id id;
	slack_object_id_set(id, sid);

	SlackUser *user = g_hash_table_lookup(sa->users, id);

	if (json_get_prop_boolean(json, "deleted", FALSE)) {
		if (!user)
			return FALSE;
		if (user->name)
			g_hash_table_remove(sa->user_names, user->name);
		if (*user->im)
			g_hash_table_remove(sa->ims, user->im);
		g_hash_table_remove(sa->users, id);
		return TRUE;
	}

	gboolean changed = FALSE;

	if (!user) {
		user = g_object_new(SLACK_TYPE_USER, NULL);
		slack_object_id_copy(user->object.id, id);
		g_hash_table_replace(sa->users, user->object.id, user);
		changed = TRUE;
	}

	const char *name = json_get_prop_strptr(json, "name");
	g_warn_if_fail(name);

	if (g_strcmp0(user->name, name)) {
		purple_debug_misc("slack", "user %s: %s\n", sid, name);

		if (user->name)
			g_hash_table_remove(sa->user_names, user->name);
		g_free(user->name);
		user->name = g_strdup(name);
		g_hash_table_insert(sa->user_names, user->name, user);
		if (user->buddy)
			purple_blist_rename_buddy(user->buddy, user->name);
		changed = TRUE;
	}

	return changed;
}

void slack_user_changed(SlackAccount *sa, json_value *json) {
	user_update(sa, json_get_prop(json, "user"));
}

static void users_list_cb(SlackAccount *sa, gpointer data, json_value *json, const char *error) {

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
	slack_api_call(sa, users_list_cb, NULL, "users.list", "presence", "false", NULL);
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
	const char *presence = json_get_prop_strptr(json, "presence");
	if (!users || !presence)
		return;

	if (users->type == json_array)
		for (unsigned i = 0; i < users->u.array.length; i ++)
			presence_set(sa, users->u.array.values[i], presence);
	else
		presence_set(sa, users, presence);
}

static void users_info_cb(SlackAccount *sa, gpointer data, json_value *json, const char *error) {
	char *who = data;

	json = json_get_prop_type(json, "user", object);

	if (error || !json) {
		/* need to close userinfo dialog somehow? */
		purple_notify_error(sa->gc, NULL, "No such user", error ?: who);
		g_free(who);
		return;
	}

	PurpleNotifyUserInfo *info = purple_notify_user_info_new();

	const char *s;
	time_t t;
	if ((s = json_get_prop_strptr(json, "id")))
		purple_notify_user_info_add_pair_plaintext(info, "id", s);
	if ((s = json_get_prop_strptr(json, "name")))
		purple_notify_user_info_add_pair_plaintext(info, "name", s);
	if (json_get_prop_boolean(json, "deleted", FALSE))
		purple_notify_user_info_add_pair_plaintext(info, "status", "deleted");
	if (json_get_prop_boolean(json, "is_primary_owner", FALSE))
		purple_notify_user_info_add_pair_plaintext(info, "role", "primary owner");
	else if (json_get_prop_boolean(json, "is_owner", FALSE))
		purple_notify_user_info_add_pair_plaintext(info, "role", "owner");
	else if (json_get_prop_boolean(json, "is_admin", FALSE))
		purple_notify_user_info_add_pair_plaintext(info, "role", "admin");
	else if (json_get_prop_boolean(json, "is_ultra_restricted", FALSE))
		purple_notify_user_info_add_pair_plaintext(info, "role", "ultra restricted");
	else if (json_get_prop_boolean(json, "is_restricted", FALSE))
		purple_notify_user_info_add_pair_plaintext(info, "role", "restricted");
	if (json_get_prop_boolean(json, "has_2fa", FALSE)) {
		s = json_get_prop_strptr(json, "two_factor_type");
		purple_notify_user_info_add_pair_plaintext(info, "2fa", s ?: "true");
	}
	if ((t = slack_parse_time(json_get_prop(json, "updated"))))
		purple_notify_user_info_add_pair_plaintext(info, "updated", purple_date_format_long(localtime(&t)));

	json_value *prof = json_get_prop_type(json, "profile", object);
	if (prof) {
		purple_notify_user_info_add_section_header(info, "profile");
		if ((s = json_get_prop_strptr(prof, "status_text")))
			purple_notify_user_info_add_pair_plaintext(info, "status", s);
		if ((s = json_get_prop_strptr(prof, "first_name")))
			purple_notify_user_info_add_pair_plaintext(info, "first name", s);
		if ((s = json_get_prop_strptr(prof, "last_name")))
			purple_notify_user_info_add_pair_plaintext(info, "last name", s);
		if ((s = json_get_prop_strptr(prof, "real_name")))
			purple_notify_user_info_add_pair_plaintext(info, "real name", s);
		if ((s = json_get_prop_strptr(prof, "email")))
			purple_notify_user_info_add_pair_plaintext(info, "email", s);
		if ((s = json_get_prop_strptr(prof, "skype")))
			purple_notify_user_info_add_pair_plaintext(info, "skype", s);
		if ((s = json_get_prop_strptr(prof, "phone")))
			purple_notify_user_info_add_pair_plaintext(info, "phone", s);
	}

	purple_notify_userinfo(sa->gc, who, info, NULL, NULL);
	purple_notify_user_info_destroy(info);
	g_free(who);
}

void slack_get_info(PurpleConnection *gc, const char *who) {
	SlackAccount *sa = gc->proto_data;
	SlackUser *user = g_hash_table_lookup(sa->user_names, who);
	if (!user)
		users_info_cb(sa, g_strdup(who), NULL, NULL);
	else
		slack_api_call(sa, users_info_cb, g_strdup(who), "users.info", "user", user->object.id, NULL);
}

