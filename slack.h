#ifndef _PURPLE_SLACK_H
#define _PURPLE_SLACK_H

#include <account.h>

#include "purple-websocket.h"

#define SLACK_PLUGIN_ID "prpl-slack"

#define SLACK_CONNECT_STEPS 8

typedef struct _SlackAccount {
	PurpleAccount *account;
	PurpleConnection *gc;
	char *api_url; /* e.g., "https://slack.com/api" */
	char *token; /* url encoded */

	PurpleWebsocket *rtm;
	gulong rtm_id;
	GHashTable *rtm_call; /* unsigned rtm_id -> SlackRTMCall */

	char *self; /* self id */
	struct _SlackTeam {
		char *id;
		char *name;
		char *domain;
	} team;

	GHashTable *users; /* slack_object_id user_id -> SlackUser (ref) */
	GHashTable *user_names; /* char *user_name -> SlackUser (no ref) */
	GHashTable *ims; /* slack_object_id im_id -> SlackUser (no ref) */

	GHashTable *channels; /* slack_object_id channel_id -> SlackChannel (ref) */
	GHashTable *channel_names; /* char *chan_name -> SlackChannel (no ref) */
	int cid;
	GHashTable *channel_cids; /* int purple_chat_id -> SlackChannel (no ref) */

	PurpleGroup *blist; /* default group for ims/channels */
	GHashTable *buddies; /* char *slack_id -> PurpleBListNode */
} SlackAccount;

GHashTable *slack_chat_info_defaults(PurpleConnection *gc, const char *name);

#endif // _PURPLE_SLACK_H
