#include <errno.h>

#include <debug.h>

#include "slack-json.h"
#include "slack-api.h"
#include "slack-blist.h"
#include "slack-channel.h"

G_DEFINE_TYPE(SlackChannel, slack_channel, SLACK_TYPE_OBJECT);

static void slack_channel_finalize(GObject *gobj) {
	SlackChannel *chan = SLACK_CHANNEL(gobj);

	g_free(chan->name);

	G_OBJECT_CLASS(slack_channel_parent_class)->finalize(gobj);
}

static void slack_channel_class_init(SlackChannelClass *klass) {
	GObjectClass *gobj = G_OBJECT_CLASS(klass);
	gobj->finalize = slack_channel_finalize;
}

static void slack_channel_init(SlackChannel *self) {
}

static gboolean channel_update(SlackAccount *sa, json_value *json, SlackChannelType type);

static void channels_info_cb(SlackAccount *sa, gpointer data, json_value *json, const char *error) {
	json_value *chan = json_get_prop_type(json, "channel", object);

	if (!chan || error) {
		purple_debug_error("slack", "Error loading channel info: %s\n", error ?: "missing");
		return;
	}

	channel_update(sa, chan, SLACK_CHANNEL_PUBLIC);
}

static gboolean channel_update(SlackAccount *sa, json_value *json, SlackChannelType type) {
	const char *sid = json_get_strptr(json);
	if (sid)
		json = NULL;
	else
		sid = json_get_prop_strptr(json, "id");
	if (!sid)
		return FALSE;
	slack_object_id id;
	slack_object_id_set(id, sid);

	SlackChannel *chan = g_hash_table_lookup(sa->channels, id);

	     if (json_get_prop_boolean(json, "is_archived", FALSE))
		type = SLACK_CHANNEL_DELETED;
	else if (json_get_prop_boolean(json, "is_mpim", FALSE))
		type = SLACK_CHANNEL_MPIM;
	else if (json_get_prop_boolean(json, "is_group", FALSE))
		type = SLACK_CHANNEL_GROUP;
	else if (json_get_prop_boolean(json, "is_member", FALSE))
		type = SLACK_CHANNEL_MEMBER;
	else if (json_get_prop_boolean(json, "is_channel", FALSE))
		type = SLACK_CHANNEL_PUBLIC;

	if (type == SLACK_CHANNEL_DELETED) {
		if (!chan)
			return FALSE;
		if (chan->buddy) {
			slack_blist_uncache(sa, &chan->buddy->node);
			purple_blist_remove_chat(chan->buddy);
			chan->buddy = NULL;
		}
		g_hash_table_remove(sa->channels, id);
		return TRUE;
	}

	if (!chan && !json) {
		purple_debug_info("slack", "Requesting info for unknown channel %s\n", sid);
		slack_api_call(sa, channels_info_cb, NULL, "channels.info", "channel", sid, NULL);
		return FALSE;
	}

	gboolean changed = FALSE;

	if (!chan) {
		chan = g_object_new(SLACK_TYPE_CHANNEL, NULL);
		slack_object_id_copy(chan->object.id, id);
		g_hash_table_replace(sa->channels, chan->object.id, chan);
		changed = TRUE;
	}

	if (type > SLACK_CHANNEL_UNKNOWN)
		chan->type = type;

	const char *name = json_get_prop_strptr(json, "name");

	if (name && g_strcmp0(chan->name, name)) {
		purple_debug_misc("slack", "channel %s: %s\n", sid, name);
		
		g_free(chan->name);
		chan->name = g_strdup(name);
		if (chan->buddy)
			purple_blist_alias_chat(chan->buddy, chan->name);
		changed = TRUE;
	}

	if (!chan->buddy && chan->type >= SLACK_CHANNEL_MEMBER) {
		chan->buddy = g_hash_table_lookup(sa->buddies, sid);
		if (chan->buddy && PURPLE_BLIST_NODE_IS_CHAT(PURPLE_BLIST_NODE(chan->buddy))) {
			if (chan->name && strcmp(chan->name, purple_chat_get_name(chan->buddy))) {
				purple_blist_alias_chat(chan->buddy, chan->name);
				changed = TRUE;
			}
		} else {
			/*
			chan->buddy = purple_chat_new(sa->account, chan->name, NULL);
			slack_blist_cache(sa, &chan->buddy->node, sid);
			purple_blist_add_chat(chan->buddy, sa->blist, NULL);
			changed = TRUE;
			*/
		}
	}
	else if (chan->buddy && chan->type < SLACK_CHANNEL_MEMBER) {
		slack_blist_uncache(sa, &chan->buddy->node);
		purple_blist_remove_chat(chan->buddy);
		chan->buddy = NULL;
		changed = TRUE;
	}

	return changed;
}

void slack_channel_update(SlackAccount *sa, json_value *json, SlackChannelType event) {
	channel_update(sa, json_get_prop(json, "channel"), event);
}

static void channels_list_cb(SlackAccount *sa, gpointer data, json_value *json, const char *error) {
	json_value *chans = json_get_prop_type(json, "channels", array);
	if (!chans) {
		purple_connection_error_reason(sa->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR, error ?: "Missing channel list");
		return;
	}

	g_hash_table_remove_all(sa->channels);
	for (unsigned i = 0; i < chans->u.array.length; i++)
		channel_update(sa, chans->u.array.values[i], SLACK_CHANNEL_PUBLIC);

	slack_groups_load(sa);
}

static void groups_list_cb(SlackAccount *sa, gpointer data, json_value *json, const char *error) {
	json_value *chans = json_get_prop_type(json, "groups", array);
	if (!chans) {
		purple_connection_error_reason(sa->gc,
				PURPLE_CONNECTION_ERROR_NETWORK_ERROR, error ?: "Missing group list");
		return;
	}

	for (unsigned i = 0; i < chans->u.array.length; i++)
		channel_update(sa, chans->u.array.values[i], SLACK_CHANNEL_GROUP);

	purple_connection_set_state(sa->gc, PURPLE_CONNECTED);
}

void slack_channels_load(SlackAccount *sa) {
	purple_connection_update_progress(sa->gc, "Loading Channels", 6, SLACK_CONNECT_STEPS);
	slack_api_call(sa, channels_list_cb, NULL, "channels.list", "exclude_archived", "true", "exclude_members", "true", NULL);
}

void slack_groups_load(SlackAccount *sa) {
	purple_connection_update_progress(sa->gc, "Loading Groups", 7, SLACK_CONNECT_STEPS);
	slack_api_call(sa, groups_list_cb, NULL, "groups.list", "exclude_archived", "true", NULL);
}

int slack_chat_send(PurpleConnection *gc, int cid, const char *msg, PurpleMessageFlags flags) {
	// SlackAccount *sa = gc->proto_data;
	return -ENOTSUP;
}
