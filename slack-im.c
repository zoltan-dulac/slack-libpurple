#include <errno.h>

#include <debug.h>

#include "slack-api.h"
#include "slack-rtm.h"
#include "slack-blist.h"
#include "slack-im.h"

static void slack_presence_sub(SlackAccount *sa) {
	GString *json = slack_rtm_json_init(sa, "presence_sub");
	GHashTableIter iter;
	gpointer id;
	SlackUser *user;
	g_hash_table_iter_init(&iter, sa->ims);
	g_string_append(json, ",\"ids\":[");
	gboolean first = TRUE;
	while (g_hash_table_iter_next(&iter, &id, (gpointer*)&user)) {
		if (first)
			first = FALSE;
		else
			g_string_append_c(json, ',');
		append_json_string(json, user->object.id);
	}
	g_string_append(json, "]}");

	slack_rtm_send(sa, json);
	g_string_free(json, TRUE);
}

static gboolean im_update(SlackAccount *sa, json_value *json, gboolean open) {
	json_value *jid = json_get_prop_type(json, "id", string);
	if (!jid)
		jid = json_get_prop_type(json, "channel", string);
	if (!jid)
		return FALSE;
	slack_object_id id;
	slack_object_id_set(id, jid->u.string.ptr);

	SlackUser *user = g_hash_table_lookup(sa->ims, id);

	json_value *is_open = json_get_prop_type(json, "is_open", boolean);
	if (!(is_open ? is_open->u.boolean : open)) {
		if (!user)
			return FALSE;
		g_return_val_if_fail(*user->im, FALSE);
		g_hash_table_remove(sa->ims, user->im);
		slack_object_id_clear(user->im);
		if (user->buddy) {
			purple_blist_remove_buddy(user->buddy);
			user->buddy = NULL;
		}
		return TRUE;
	}

	gboolean changed = FALSE;

	json_value *user_id = json_get_prop_type(json, "user", string);
	g_return_val_if_fail(user_id, FALSE);

	if (!user) {
		user = (SlackUser *)slack_object_hash_table_lookup(sa->users, user_id->u.string.ptr);
		if (!user)
			return FALSE;
		if (slack_object_id_cmp(user->im, id)) {
			if (*user->im)
				g_hash_table_remove(sa->ims, user->im);
			slack_object_id_copy(user->im, id);
			g_hash_table_insert(sa->ims, user->im, user);
			changed = TRUE;
		}
	} else
		g_warn_if_fail(slack_object_id_is(user->object.id, user_id->u.string.ptr));

	if (!user->buddy) {
		user->buddy = g_hash_table_lookup(sa->buddies, jid->u.string.ptr);
		if (user->buddy && PURPLE_BLIST_NODE_IS_BUDDY(PURPLE_BLIST_NODE(user->buddy))) {
			if (user->name && strcmp(user->name, purple_buddy_get_name(user->buddy))) {
				purple_blist_rename_buddy(user->buddy, user->name);
				changed = TRUE;
			}
		} else {
			user->buddy = purple_buddy_new(sa->account, user->name, NULL);
			slack_blist_cache(sa, &user->buddy->node, jid->u.string.ptr);
			purple_blist_add_buddy(user->buddy, NULL, sa->blist, NULL);
			changed = TRUE;
		}
	}

	purple_debug_misc("slack", "im %s: %s\n", user->im, user->object.id);
	return changed;
}

void slack_im_closed(SlackAccount *sa, json_value *json) {
	if (im_update(sa, json, FALSE))
		slack_presence_sub(sa);
}

void slack_im_opened(SlackAccount *sa, json_value *json) {
	if (im_update(sa, json, TRUE))
		slack_presence_sub(sa);
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
		im_update(sa, ims->u.array.values[i], TRUE);

	slack_presence_sub(sa);

	purple_connection_set_state(sa->gc, PURPLE_CONNECTED);
}

void slack_ims_load(SlackAccount *sa) {
	purple_connection_update_progress(sa->gc, "Loading IM channels", 5, SLACK_CONNECT_STEPS);
	slack_api_call(sa, "im.list", NULL, im_list_cb, sa);
}

int slack_send_im(PurpleConnection *gc, const char *who, const char *message, PurpleMessageFlags flags) {
	return -ENOTSUP;
}
