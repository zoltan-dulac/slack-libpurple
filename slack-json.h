#ifndef _PURPLE_SLACK_JSON_H
#define _PURPLE_SLACK_JSON_H

#include "json/json.h"

json_value *json_get_prop(json_value *val, const char *prop) __attribute__((pure));

#define json_get_type(JSON, TYPE) ({ \
		json_value *_val = (JSON); \
		_val && _val->type == json_##TYPE ? _val : NULL; \
	})
#define json_get_prop_type(JSON, PROP, TYPE) json_get_type(json_get_prop(JSON, PROP), TYPE)

#endif
