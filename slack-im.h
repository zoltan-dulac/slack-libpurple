#ifndef _PURPLE_SLACK_IM_H
#define _PURPLE_SLACK_IM_H

#include <json.h>

#include "slack.h"

void slack_im_closed(SlackAccount *sa, json_value *json);
void slack_im_opened(SlackAccount *sa, json_value *json);
void slack_ims_load(SlackAccount *sa);

int slack_send_im(PurpleConnection *gc, const char *who, const char *message, PurpleMessageFlags flags);

#endif // _PURPLE_SLACK_IM_H
