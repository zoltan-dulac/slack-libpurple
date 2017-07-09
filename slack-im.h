#ifndef _PURPLE_SLACK_IM_H
#define _PURPLE_SLACK_IM_H

#include "slack-json.h"
#include "slack.h"

typedef struct _SlackIM {
	char *user; /* id */
} SlackIM;

void slack_im_free(SlackIM *user);
void slack_im_closed(SlackAccount *sa, json_value *json);
void slack_im_opened(SlackAccount *sa, json_value *json);
void slack_ims_load(SlackAccount *sa);

#endif // _PURPLE_SLACK_IM_H
