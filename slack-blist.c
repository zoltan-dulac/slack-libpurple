#include <string.h>

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
