#ifndef _PURPLE_SLACK_IM_H
#define _PURPLE_SLACK_IM_H

#include "slack-json.h"
#include "slack-object.h"
#include "slack-user.h"
#include "slack.h"

struct _SlackIM {
	SlackObject object;

	SlackUser *user;
};

#define SLACK_TYPE_IM slack_im_get_type()
G_DECLARE_FINAL_TYPE(SlackIM, slack_im, SLACK, IM, SlackObject)
	
void slack_im_closed(SlackAccount *sa, json_value *json);
void slack_im_opened(SlackAccount *sa, json_value *json);
void slack_ims_load(SlackAccount *sa);

#endif // _PURPLE_SLACK_IM_H
