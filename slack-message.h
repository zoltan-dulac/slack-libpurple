#ifndef _PURPLE_SLACK_MESSAGE_H
#define _PURPLE_SLACK_MESSAGE_H

#include <json.h>

#include "slack.h"

void slack_message(SlackAccount *sa, json_value *json);

#endif // _PURPLE_SLACK_MESSAGE_H
