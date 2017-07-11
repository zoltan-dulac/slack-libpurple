#ifndef _PURPLE_SLACK_RTM_H
#define _PURPLE_SLACK_RTM_H

#include "slack.h"

void slack_rtm_connect(SlackAccount *sa);
GString *slack_rtm_json_init(SlackAccount *sa, const char *type);
void slack_rtm_send(SlackAccount *sa, const GString *json);

#endif
