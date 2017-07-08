#include <string.h>

#include "slack-json.h"

json_value *json_get_prop(json_value *val, const char *index) {
   if (!val || val->type != json_object)
      return NULL;

   for (unsigned int i = 0; i < val->u.object.length; ++ i)
      if (!strcmp (val->u.object.values [i].name, index))
         return val->u.object.values[i].value;

   return NULL;
}
