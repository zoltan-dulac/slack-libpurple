#ifndef _PURPLE_SLACK_USER_H
#define _PURPLE_SLACK_USER_H

#include <json.h>

#include "slack-object.h"
#include "slack.h"

/* SlackUser represents both a user object, and an optional im object */
struct _SlackUser {
	SlackObject object;

	char *name;

	/* when there is an open IM channel: */
	slack_object_id im;
	PurpleBuddy *buddy;
};

#define SLACK_TYPE_USER slack_user_get_type()
G_DECLARE_FINAL_TYPE(SlackUser, slack_user, SLACK, USER, SlackObject)

/* Initialization */
void slack_users_load(SlackAccount *sa);

/* RTM event handlers */
void slack_user_changed(SlackAccount *sa, json_value *json);
void slack_presence_change(SlackAccount *sa, json_value *json);

/* Purple protocol handlers */
void slack_get_info(PurpleConnection *gc, const char *who);

#endif // _PURPLE_SLACK_USER_H
