#include <errno.h>

#include <debug.h>

#include "slack-json.h"
#include "slack-api.h"
#include "slack-blist.h"
#include "slack-rtm.h"
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

static SlackChannel *channel_update(SlackAccount *sa, json_value *json, SlackChannelType type);

static void channels_info_cb(SlackAccount *sa, gpointer data, json_value *json, const char *error) {
	json_value *chan = json_get_prop_type(json, "channel", object);

	if (!chan || error) {
		purple_debug_error("slack", "Error loading channel info: %s\n", error ?: "missing");
		return;
	}

	channel_update(sa, chan, SLACK_CHANNEL_PUBLIC);
}

static void channel_depart(SlackAccount *sa, SlackChannel *chan) {
	if (chan->cid) {
		serv_got_chat_left(sa->gc, chan->cid);
		g_hash_table_remove(sa->channel_cids, GUINT_TO_POINTER(chan->cid));
		chan->cid = 0;
	}
	if (chan->buddy) {
		slack_blist_uncache(sa, &chan->buddy->node);
		purple_blist_remove_chat(chan->buddy);
		chan->buddy = NULL;
	}
}

static SlackChannel *channel_update(SlackAccount *sa, json_value *json, SlackChannelType type) {
	const char *sid = json_get_strptr(json);
	if (sid)
		json = NULL;
	else
		sid = json_get_prop_strptr(json, "id");
	if (!sid)
		return NULL;
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
			return NULL;
		channel_depart(sa, chan);
		if (chan->name)
			g_hash_table_remove(sa->channel_names, chan->name);
		g_hash_table_remove(sa->channels, id);
		return NULL;
	}

	if (!chan && !json) {
		purple_debug_info("slack", "Requesting info for unknown channel %s\n", sid);
		slack_api_call(sa, channels_info_cb, NULL, "channels.info", "channel", sid, NULL);
		return NULL;
	}

	if (!chan) {
		chan = g_object_new(SLACK_TYPE_CHANNEL, NULL);
		slack_object_id_copy(chan->object.id, id);
		g_hash_table_replace(sa->channels, chan->object.id, chan);
	}

	if (type > SLACK_CHANNEL_UNKNOWN)
		chan->type = type;

	const char *name = json_get_prop_strptr(json, "name");

	if (name && g_strcmp0(chan->name, name)) {
		purple_debug_misc("slack", "channel %s: %s %d\n", sid, name, type);
		
		if (chan->name)
			g_hash_table_remove(sa->channel_names, chan->name);
		g_free(chan->name);
		chan->name = g_strdup(name);
		g_hash_table_insert(sa->channel_names, chan->name, chan);
		if (chan->buddy)
			g_hash_table_insert(chan->buddy->components, g_strdup("name"), g_strdup(chan->name));
	}

	if (!chan->buddy && chan->type >= SLACK_CHANNEL_MEMBER) {
		chan->buddy = g_hash_table_lookup(sa->buddies, sid);
		if (chan->buddy && PURPLE_BLIST_NODE_IS_CHAT(PURPLE_BLIST_NODE(chan->buddy))) {
			if (chan->name)
				g_hash_table_insert(chan->buddy->components, g_strdup("name"), g_strdup(chan->name));
		} else {
			GHashTable *info = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
			g_hash_table_insert(info, "name", g_strdup(chan->name));
			chan->buddy = purple_chat_new(sa->account, chan->name, info);
			slack_blist_cache(sa, &chan->buddy->node, sid);
			purple_blist_add_chat(chan->buddy, sa->blist, NULL);
		}
	}
	else if (chan->type < SLACK_CHANNEL_MEMBER) {
		channel_depart(sa, chan);
	}

	return chan;
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

struct join_channel {
	SlackChannel *chan;
	char *name;
};

static void join_channel_free(struct join_channel *join) {
	if (join->chan)
		g_object_unref(join->chan);
	g_free(join->name);
	g_free(join);
}

static void channels_join_cb(SlackAccount *sa, gpointer data, json_value *json, const char *error) {
	struct join_channel *join = data;

	SlackChannel *chan = json
		? channel_update(sa, json_get_prop(json, "channel"), SLACK_CHANNEL_MEMBER)
		: join->chan;

	if (!chan || error) {
		purple_notify_error(sa->gc, "Invalid Channel", "Could not join channel", error ?: join->name);
		/* hacky: reconstruct info */
		GHashTable *info = slack_chat_info_defaults(sa->gc, join->name);
		purple_serv_got_join_chat_failed(sa->gc, info);
		join_channel_free(join);
		return;
	}

	g_warn_if_fail(chan->type >= SLACK_CHANNEL_MEMBER);

	if (!chan->cid) {
		chan->cid = ++sa->cid;
		g_hash_table_insert(sa->channel_cids, GUINT_TO_POINTER(chan->cid), chan);
	}

	serv_got_joined_chat(sa->gc, chan->cid, chan->name);
	join_channel_free(join);
}

void slack_join_chat(PurpleConnection *gc, GHashTable *info) {
	SlackAccount *sa = gc->proto_data;

	const char *name = g_hash_table_lookup(info, "name");
	g_return_if_fail(name);

	SlackChannel *chan = g_hash_table_lookup(sa->channel_names, name);
	if (chan)
		g_object_ref(chan);
	struct join_channel *join = g_new0(struct join_channel, 1);
	if (chan)
		join->chan = g_object_ref(chan);
	join->name = g_strdup(name);

	if (chan && chan->type >= SLACK_CHANNEL_MEMBER)
		channels_join_cb(sa, join, NULL, NULL);
	else
		slack_api_call(sa, channels_join_cb, join, "channels.join", "name", name, NULL);
}

void slack_chat_leave(PurpleConnection *gc, int cid) {
	SlackAccount *sa = gc->proto_data;
	SlackChannel *chan = g_hash_table_lookup(sa->channel_cids, GUINT_TO_POINTER(cid));
	if (!chan)
		return;
	g_hash_table_remove(sa->channel_cids, GUINT_TO_POINTER(cid));
	chan->cid = 0;
}

struct send_chat {
	SlackChannel *chan;
	int cid;
	PurpleMessageFlags flags;
};

static void send_chat_free(struct send_chat *send) {
	g_object_unref(send->chan);
	g_free(send);
}

static void send_chat_cb(SlackAccount *sa, gpointer data, json_value *json, const char *error) {
	struct send_chat *send = data;

	/* XXX better way to present chat errors? */
	if (error) {
		purple_conv_present_error(send->chan->name, sa->account, error);
		send_chat_free(send);
		return;
	}

	const char *text       = json_get_prop_strptr(json, "text");
	const char *ts         = json_get_prop_strptr(json, "ts");
	time_t mt = ts ? atol(ts) : 0;
	serv_got_chat_in(sa->gc, send->cid, purple_connection_get_display_name(sa->gc), send->flags, text, mt);
	send_chat_free(send);
}

int slack_chat_send(PurpleConnection *gc, int cid, const char *msg, PurpleMessageFlags flags) {
	SlackAccount *sa = gc->proto_data;

	glong mlen = g_utf8_strlen(msg, 16384);
	if (mlen > 4000)
		return -E2BIG;

	SlackChannel *chan = g_hash_table_lookup(sa->channel_cids, GUINT_TO_POINTER(cid));
	if (!chan)
		return -ENOENT;

	struct send_chat *send = g_new(struct send_chat, 1);
	send->chan = g_object_ref(chan);
	send->cid = cid;
	send->flags = flags;

	GString *channel = append_json_string(g_string_new(NULL), chan->object.id);
	GString *text = append_json_string(g_string_new(NULL), msg);
	slack_rtm_send(sa, send_chat_cb, send, "message", "channel", channel->str, "text", text->str, NULL);
	g_string_free(channel, TRUE);
	g_string_free(text, TRUE);

	return 1;
}
