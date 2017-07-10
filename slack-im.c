#include <debug.h>

#include "slack-api.h"
#include "slack-blist.h"
#include "slack-im.h"

G_DEFINE_TYPE(SlackIM, slack_im, SLACK_TYPE_OBJECT);

static void slack_im_dispose(GObject *gobj) {
	SlackIM *im = SLACK_IM(gobj);

	purple_debug_misc("slack", "freeing im %s\n", im->object.id);
	g_clear_object(&im->user);

	G_OBJECT_CLASS(slack_im_parent_class)->dispose(gobj);
}

static void slack_im_class_init(SlackIMClass *klass) {
	GObjectClass *gobj = G_OBJECT_CLASS(klass);
	gobj->dispose = slack_im_dispose;
}

static void slack_im_init(SlackIM *self) {
}

static void im_update(SlackAccount *sa, json_value *json, gboolean open) {
	json_value *id = json_get_prop_type(json, "id", string);
	if (!id)
		id = json_get_prop_type(json, "channel", string);
	if (!id)
		return;

	json_value *is_open = json_get_prop_type(json, "is_open", boolean);
	if (!(is_open ? is_open->u.boolean : open)) {
		SlackIM *im = (SlackIM*)slack_object_hash_table_take(sa->ims, id->u.string.ptr);
		if (im) {
			if (im->buddy)
				purple_blist_remove_buddy(im->buddy);
			g_object_unref(im);
		}
		return;
	}

	SlackIM *im = (SlackIM*)slack_object_hash_table_get(sa->ims, SLACK_TYPE_IM, id->u.string.ptr);

	json_value *user_id = json_get_prop_type(json, "user", string);
	if (user_id && !(im->user && slack_object_has_id(&im->user->object, user_id->u.string.ptr))) {
		if (im->buddy) {
			purple_blist_remove_buddy(im->buddy);
			slack_blist_uncache(sa, &im->buddy->node);
			im->buddy = NULL;
		}
		g_clear_object(&im->user);
		SlackUser *user = (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id->u.string.ptr);
		if (user)
			im->user = g_object_ref(user);
		else
			purple_debug_info("slack", "im: no user: %s\n", user_id->u.string.ptr);
	}

	purple_debug_info("slack", "im: %p, %s\n", im->user, im->user ? im->user->name : NULL);
	if (im->user && im->user->name && !im->buddy) {
		im->buddy = g_hash_table_lookup(sa->buddies, im->object.id);
		if (im->buddy && PURPLE_BLIST_NODE_IS_BUDDY(PURPLE_BLIST_NODE(im->buddy))) {
			if (strcmp(im->user->name, purple_buddy_get_name(im->buddy)))
				purple_blist_rename_buddy(im->buddy, im->user->name);
		} else {
			im->buddy = purple_buddy_new(sa->account, im->user->name, NULL);
			slack_blist_cache(sa, &im->buddy->node, im->object.id);
			purple_blist_add_buddy(im->buddy, NULL, sa->blist, NULL);
		}
	}
}

void slack_im_closed(SlackAccount *sa, json_value *json) {
	im_update(sa, json, FALSE);
}

void slack_im_opened(SlackAccount *sa, json_value *json) {
	im_update(sa, json, TRUE);
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

	purple_connection_set_state(sa->gc, PURPLE_CONNECTED);
}

void slack_ims_load(SlackAccount *sa) {
	purple_connection_update_progress(sa->gc, "Loading IM channels", 5, SLACK_CONNECT_STEPS);
	slack_api_call(sa, "im.list", NULL, im_list_cb, sa);
}
