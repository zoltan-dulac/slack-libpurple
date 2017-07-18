#include <string.h>

#include "slack-channel.h"
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

PurpleRoomlist *slack_roomlist_get_list(PurpleConnection *gc) {
	SlackAccount *sa = gc->proto_data;

	PurpleRoomlist *list = purple_roomlist_new(sa->account);

	GList *fields = NULL;
	fields = g_list_append(fields, purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_STRING, "Topic", "topic", FALSE));
	fields = g_list_append(fields, purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_STRING, "Purpose", "purpose", FALSE));
	// fields = g_list_append(fields, purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_INT, "Members", "members", FALSE));
	// fields = g_list_append(fields, purple_roomlist_field_new(PURPLE_ROOMLIST_FIELD_STRING, "Created", "created", FALSE));
	purple_roomlist_set_fields(list, fields);

	PurpleRoomlistRoom *public = purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_CATEGORY, "Public Channels", NULL);
	purple_roomlist_room_add(list, public);
	PurpleRoomlistRoom *private = purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_CATEGORY, "Private Channels", NULL);
	purple_roomlist_room_add(list, private);
	/* TODO: archived? */

	GHashTableIter iter;
	char *key;
	SlackChannel *chan;

	g_hash_table_iter_init(&iter, sa->channels);
	while (g_hash_table_iter_next(&iter, (gpointer*)&key, (gpointer*)&chan)) {
		PurpleRoomlistRoom *parent;
		switch (chan->type) {
			case SLACK_CHANNEL_PUBLIC:
			case SLACK_CHANNEL_MEMBER:
				parent = public;
				break;
			case SLACK_CHANNEL_GROUP:
			case SLACK_CHANNEL_MPIM:
				parent = private;
				break;
			default:
				continue;
		}
		PurpleRoomlistRoom *room = purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_ROOM, chan->name, parent);
		purple_roomlist_room_add_field(list, room, chan->topic);
		purple_roomlist_room_add_field(list, room, chan->purpose);
		// purple_roomlist_room_add_field(list, room, GUINT_TO_POINTER(chan->member_count));
		purple_roomlist_room_add(list, room);
	}

	purple_roomlist_unref(list);
	return list;
}
