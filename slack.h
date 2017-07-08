#ifndef _PURPLE_SLACK_H
#define _PURPLE_SLACK_H

#include <account.h>

#include "purple-websocket.h"

#define SLACK_PLUGIN_ID "prpl-slack"

#define SLACK_CONNECT_STEPS 5

typedef struct _SlackAccount {
	PurpleAccount *account;
	PurpleConnection *gc;
	char *api_url; /* e.g., "https://slack.com/api" */
	char *token; /* url encoded */

	PurpleWebsocket *rtm;

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
} SlackAccount;

#endif // _PURPLE_SLACK_H
