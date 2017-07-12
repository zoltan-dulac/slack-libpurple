#ifndef _PURPLE_SLACK_JSON_H
#define _PURPLE_SLACK_JSON_H

#include <glib.h>
#include <json.h>

#define json_get_type(JSON, TYPE) ({ \
		json_value *_val = (JSON); \
		_val && _val->type == json_##TYPE ? _val : NULL; \
	})
#define json_get_strptr(JSON) ({ \
		json_value *_val = (JSON); \
		_val && _val->type == json_string ? _val->u.string.ptr : NULL; \
	})

json_value *json_get_prop(json_value *val, const char *prop) __attribute__((pure));

#define json_get_prop_type(JSON, PROP, TYPE) json_get_type(json_get_prop(JSON, PROP), TYPE)
#define json_get_prop_strptr(JSON, PROP) json_get_strptr(json_get_prop(JSON, PROP))

/* Add an escaped, quoted json string to a GString */
GString *append_json_string(GString *str, const char *s);

#endif
