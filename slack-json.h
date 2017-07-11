#ifndef _PURPLE_SLACK_JSON_H
#define _PURPLE_SLACK_JSON_H

#include <glib.h>
#include <json.h>

json_value *json_get_prop(json_value *val, const char *prop) __attribute__((pure));

#define json_get_type(JSON, TYPE) ({ \
		json_value *_val = (JSON); \
		_val && _val->type == json_##TYPE ? _val : NULL; \
	})
#define json_get_prop_type(JSON, PROP, TYPE) json_get_type(json_get_prop(JSON, PROP), TYPE)

/* Add an escaped, quoted json string to a GString */
GString *append_json_string(GString *str, const char *s);

#endif
