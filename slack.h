#ifndef _PURPLE_SLACK_H
#define _PURPLE_SLACK_H

#include <account.h>

#include "purple-websocket.h"

#define SLACK_PLUGIN_ID "prpl-slack"

#define SLACK_CONNECT_STEPS 6

typedef struct _SlackAccount {
	PurpleAccount *account;
	PurpleConnection *gc;
	char *api_url; /* e.g., "https://slack.com/api" */
	char *token; /* url encoded */

	PurpleWebsocket *rtm;
	unsigned rtm_id;

	char *self; /* self id */
	struct _SlackTeam {
		char *id;
		char *name;
		char *domain;
	} team;

	/* map IDs to objects */
	GHashTable *users;
	GHashTable *ims;
	GHashTable *channels;
	GHashTable *groups;

	PurpleGroup *blist; /* default group for ims/channels */
	GHashTable *buddies; /* slack ID -> PurpleBListNode */
} SlackAccount;

#endif // _PURPLE_SLACK_H
