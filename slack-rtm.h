#ifndef _PURPLE_SLACK_RTM_H
#define _PURPLE_SLACK_RTM_H

#include "slack.h"

void slack_rtm_connect(SlackAccount *sa);
/* Send an RTM message of the given type (unquoted, escaped json string) with the given key (unquoted, escaped json string), value (const char *json) pairs */
void slack_rtm_send(SlackAccount *sa, const char *type, /* const char *key1, const char *json1, */ ...) G_GNUC_NULL_TERMINATED;

#endif
