#ifndef _PURPLE_SLACK_BLIST_H
#define _PURPLE_SLACK_BLIST_H

#include "slack.h"

/* the key we store the channel ID in */
#define SLACK_BLIST_KEY "slack"

void slack_blist_uncache(SlackAccount *sa, PurpleBlistNode *b);
void slack_blist_cache(SlackAccount *sa, PurpleBlistNode *b, const char *id);
void slack_blist_init(SlackAccount *sa);

void slack_buddy_free(PurpleBuddy *b);

#endif // _PURPLE_SLACK_BLIST_H
