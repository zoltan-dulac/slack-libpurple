#ifndef _PURPLE_SLACK_USER_H
#define _PURPLE_SLACK_USER_H

#include "slack-json.h"
#include "slack.h"

typedef struct _SlackUser {
	char *name;
} SlackUser;

void slack_user_free(SlackUser *user);
void slack_user_changed(SlackAccount *sa, json_value *json);
void slack_users_load(SlackAccount *sa);
SlackUser *slack_user_find(SlackAccount *sa, const char *name);

#endif // _PURPLE_SLACK_USER_H
