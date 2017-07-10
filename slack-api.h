#ifndef _PURPLE_SLACK_API_H
#define _PURPLE_SLACK_API_H

#include <glib.h>

#include <json.h>
#include "slack.h"

typedef struct _SlackAPICall SlackAPICall;

typedef void (*SlackAPICallback)(SlackAPICall *api, gpointer user_data, json_value *json, const char *error);

SlackAPICall *slack_api_call(SlackAccount *sa, const char *method, const char *query, SlackAPICallback calback, gpointer data);

#endif
