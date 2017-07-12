#ifndef _PURPLE_SLACK_API_H
#define _PURPLE_SLACK_API_H

#include <glib.h>

#include <json.h>
#include "slack.h"

typedef struct _SlackAPICall SlackAPICall;

typedef void (*SlackAPICallback)(SlackAccount *sa, gpointer user_data, json_value *json, const char *error);

void slack_api_call(SlackAccount *sa, SlackAPICallback callback, gpointer data, const char *method, /* const char *query_param1, const char *query_value1, */ ...) G_GNUC_NULL_TERMINATED;

#endif
