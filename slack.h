#ifndef _PURPLE_SLACK_H
#define _PURPLE_SLACK_H

#include <account.h>

#include "purple-websocket.h"

#define SLACK_PLUGIN_ID "prpl-slack"

typedef struct _SlackAccount {
	PurpleAccount *account;
	PurpleConnection *gc;
	char *api_url; /* e.g., "https://slack.com/api" */
	char *token; /* url encoded */

	PurpleWebsocket *rtm;
} SlackAccount;

#endif // _SLACK_PLUGIN_H
