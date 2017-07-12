#include <debug.h>

#include "slack-json.h"
#include "slack-user.h"
#include "slack-message.h"

void slack_message(SlackAccount *sa, json_value *json) {
	const char *user_id    = json_get_prop_strptr(json, "user");
	const char *channel_id = json_get_prop_strptr(json, "channel");
	const char *text       = json_get_prop_strptr(json, "text");
	const char *ts         = json_get_prop_strptr(json, "ts");
	const char *subtype    = json_get_prop_strptr(json, "subtype");
	json_value *hidden     = json_get_prop_type(json, "hidden", boolean);

	/* ts is EPOCH.0000ID, atol is sufficient */
	time_t mt = ts ? atol(ts) : 0;

	PurpleMessageFlags flags = PURPLE_MESSAGE_RECV;
	if (subtype)
		flags |= PURPLE_MESSAGE_SYSTEM; /* PURPLE_MESSAGE_NOTIFY? */
	if (hidden && hidden->u.boolean)
		flags |= PURPLE_MESSAGE_INVISIBLE;

	SlackUser *user = user_id ? (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id) : NULL;
	if (user && slack_object_id_is(user->im, channel_id)) {
		/* IM */
		serv_got_im(sa->gc, user->name, text, flags, mt);
	} else {
		purple_debug_warning("slack", "Unhandled message: %s@%s: %s\n", user_id, channel_id, text);
	}
}
