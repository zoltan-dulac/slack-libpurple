#ifndef _PURPLE_SLACK_RTM_H
#define _PURPLE_SLACK_RTM_H

#include <json.h>

#include "slack.h"

typedef struct _SlackRTMCall SlackRTMCall;

typedef void SlackRTMCallback(SlackAccount *sa, gpointer user_data, json_value *json, const char *error);

void slack_rtm_connect(SlackAccount *sa);
/* Send an RTM message of the given type (unquoted, escaped json string) with the given key (unquoted, escaped json string), value (const char *json) pairs */
void slack_rtm_send(SlackAccount *sa, SlackRTMCallback *callback, gpointer user_data, const char *type, /* const char *key1, const char *json1, */ ...) G_GNUC_NULL_TERMINATED;
void slack_rtm_cancel(SlackRTMCall *call);

#endif
