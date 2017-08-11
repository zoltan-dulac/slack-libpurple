#include <string.h>

#include <debug.h>

#include "slack-json.h"
#include "slack-channel.h"
#include "slack-user.h"
#include "slack-api.h"
#include "slack-blist.h"

void slack_blist_uncache(SlackAccount *sa, PurpleBlistNode *b) {
	const char *bid = purple_blist_node_get_string(b, SLACK_BLIST_KEY);
	if (bid)
		g_hash_table_remove(sa->buddies, bid);
	purple_blist_node_remove_setting(b, SLACK_BLIST_KEY);
}

void slack_blist_cache(SlackAccount *sa, PurpleBlistNode *b, const char *id) {
	if (id)
		purple_blist_node_set_string(b, SLACK_BLIST_KEY, id);
	const char *bid = purple_blist_node_get_string(b, SLACK_BLIST_KEY);
	if (bid)
		g_hash_table_insert(sa->buddies, (gpointer)bid, b);
}

void slack_buddy_free(PurpleBuddy *b) {
	/* This should be unnecessary, as there's no analogue for PurpleChat so we have to deal with cleanup elsewhere anyway */
	if (b->account->gc && b->account->gc->proto_data)
	slack_blist_uncache(b->account->gc->proto_data, &b->node);
}

#define PURPLE_BLIST_ACCOUNT(n) \
	( PURPLE_BLIST_NODE_IS_BUDDY(n) \
		? PURPLE_BUDDY(n)->account \
	: PURPLE_BLIST_NODE_IS_CHAT(n) \
		? PURPLE_CHAT(n)->account \
		: NULL)

void slack_blist_init(SlackAccount *sa) {
	char *id = sa->team.id ?: "";
	if (!sa->blist) {
		PurpleBlistNode *g;
		for (g = purple_blist_get_root(); g; g = purple_blist_node_next(g, TRUE)) {
			const char *bid;
			if (PURPLE_BLIST_NODE_IS_GROUP(g) &&
					(bid = purple_blist_node_get_string(g, SLACK_BLIST_KEY)) &&
					!strcmp(bid, id)) {
				sa->blist = PURPLE_GROUP(g);
				break;
			}
		}
		if (!sa->blist) {
			sa->blist = purple_group_new(sa->team.name ?: "Slack");
			purple_blist_node_set_string(&sa->blist->node, SLACK_BLIST_KEY, id);
			purple_blist_add_group(sa->blist, NULL);
		}
	}

	/* Find all leaf nodes on this account (buddies and chats) with slack ids and cache them */
	PurpleBlistNode *node;
	for (node = purple_blist_get_root(); node; node = node->next) {
		while (node->child)
			node = node->child;

		if (PURPLE_BLIST_ACCOUNT(node) == sa->account)
			slack_blist_cache(sa, node, NULL);

		while (node->parent && !node->next)
			node = node->parent;
	}
}

PurpleChat *slack_find_blist_chat(PurpleAccount *account, const char *name) {
	if (account->gc && account->gc->proto_data) {
		SlackAccount *sa = account->gc->proto_data;
		if (sa->channel_names) {
			SlackChannel *chan = g_hash_table_lookup(sa->channel_names, name);
			if (chan && chan->buddy)
				return chan->buddy;
		}
	}
	return purple_blist_find_chat(account, name);
}

struct roomlist_expand {
	PurpleRoomlist *list;
	PurpleRoomlistRoom *parent;
	SlackChannelType type;
	gboolean archived;
};

void free_roomlist_expand(struct roomlist_expand *expand) {
	purple_roomlist_unref(expand->list);
	g_free(expand);
}

static void roomlist_cb(SlackAccount *sa, gpointer data, json_value *json, const char *error) {
	struct roomlist_expand *expand = data;

	json = json_get_prop_type(json, expand->type >= SLACK_CHANNEL_GROUP ? "groups" : "channels", array);
	if (!json || error) {
		purple_notify_error(sa->gc, "Channel list error", "Could not read channel list", error);
		free_roomlist_expand(expand);
		return;
	}

	for (unsigned i = 0; i < json->u.array.length; i++) {
		json_value *chan = json->u.array.values[i];

		gboolean archived = json_get_prop_boolean(chan, "is_archived", FALSE) || json_get_prop_boolean(chan, "is_deleted", FALSE);
		if (archived != expand->archived)
			continue;

		PurpleRoomlistRoom *room = purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_ROOM, json_get_prop_strptr(chan, "name"), expand->parent);
		purple_roomlist_room_add_field(expand->list, room, json_get_prop_strptr(chan, "id"));
		purple_roomlist_room_add_field(expand->list, room, json_get_prop_strptr(json_get_prop(chan, "topic"), "value"));
		purple_roomlist_room_add_field(expand->list, room, json_get_prop_strptr(json_get_prop(chan, "purpose"), "value"));
		purple_roomlist_room_add_field(expand->list, room, GUINT_TO_POINTER(json_get_val(json_get_prop(chan, "num_members"), integer, 0)));
		time_t t = slack_parse_time(json_get_prop(chan, "created"));
		purple_roomlist_room_add_field(expand->list, room, purple_date_format_long(localtime(&t)));
		SlackUser *creator = (SlackUser*)slack_object_hash_table_lookup(sa->users, json_get_prop_strptr(chan, "creator"));
		purple_roomlist_room_add_field(expand->list, room, creator ? creator->name : NULL);
		purple_roomlist_room_add(expand->list, room);
	}

	free_roomlist_expand(expand);
}

void slack_roomlist_expand_category(PurpleRoomlist *list, PurpleRoomlistRoom *parent) {
	if (!list->account || !list->account->gc || !list->account->gc->proto_data || strcmp(list->account->protocol_id, SLACK_PLUGIN_ID))
		return;
	SlackAccount *sa = list->account->gc->proto_data;

	g_warn_if_fail(list == sa->roomlist);

	struct roomlist_expand *expand = g_new0(struct roomlist_expand, 1);
	expand->list = list;
	expand->parent = parent;
	const char *cat = purple_roomlist_room_get_name(parent);
	if (!g_strcmp0(cat, "Archived") && parent->parent) {
		expand->archived = TRUE;
		cat = purple_roomlist_room_get_name(parent->parent);
	}
	const char *op;
	if (!g_strcmp0(cat, "Public Channels")) {
		expand->type = SLACK_CHANNEL_PUBLIC;
		op = "channels.list";
	}
	else if (!g_strcmp0(cat, "Private Channels")) {
		expand->type = SLACK_CHANNEL_GROUP;
		op = "groups.list";
	}
	else if (!g_strcmp0(cat, "Multiparty Direct Messages")) {
		expand->type = SLACK_CHANNEL_MPIM;
		op = "mpim.list";
	}
	else
		return;
	purple_roomlist_ref(list);
	slack_api_call(sa, roomlist_cb, expand, op, "exclude_archived", expand->archived ? "false" : "true", "exclude_members", "true", NULL);
}

PurpleRoomlist *slack_roomlist_get_list(PurpleConnection *gc) {
	SlackAccount *sa = gc->proto_data;

	if (sa->roomlist)
		purple_roomlist_unref(sa->roomlist);

	PurpleRoomlist *list = sa->roomlist = purple_roomlist_new(sa->account);

	GList *fields = NULL;
	fields = g_list_append(fields, purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_STRING, "ID", "id", TRUE));
	fields = g_list_append(fields, purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_STRING, "Topic", "topic", FALSE));
	fields = g_list_append(fields, purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_STRING, "Purpose", "purpose", FALSE));
	fields = g_list_append(fields, purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_INT, "Members", "members", FALSE));
	fields = g_list_append(fields, purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_STRING, "Created", "created", FALSE));
	fields = g_list_append(fields, purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_STRING, "Creator", "creator", FALSE));
	purple_roomlist_set_fields(list, fields);

	PurpleRoomlistRoom *public = purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_CATEGORY, "Public Channels", NULL);
	purple_roomlist_room_add(list, public);
	PurpleRoomlistRoom *private = purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_CATEGORY, "Private Channels", NULL);
	purple_roomlist_room_add(list, private);
	purple_roomlist_room_add(list, purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_CATEGORY, "Multiparty Direct Messages", NULL));
	purple_roomlist_room_add(list, purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_CATEGORY, "Archived", public));
	purple_roomlist_room_add(list, purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_CATEGORY, "Archived", private));

	return list;
}

void slack_roomlist_cancel(PurpleRoomlist *list) {
	if (!list->account || !list->account->gc || !list->account->gc->proto_data || strcmp(list->account->protocol_id, SLACK_PLUGIN_ID))
		return;
	SlackAccount *sa = list->account->gc->proto_data;

	if (sa->roomlist == list) {
		purple_roomlist_unref(list);
		sa->roomlist = NULL;
	}
}
