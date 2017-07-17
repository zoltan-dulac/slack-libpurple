#include <debug.h>

#include "slack-json.h"
#include "slack-rtm.h"
#include "slack-user.h"
#include "slack-channel.h"
#include "slack-message.h"

void slack_message(SlackAccount *sa, json_value *json) {
	const char *user_id    = json_get_prop_strptr(json, "user");
	const char *channel_id = json_get_prop_strptr(json, "channel");
	const char *text       = json_get_prop_strptr(json, "text");
	const char *ts         = json_get_prop_strptr(json, "ts");
	const char *subtype    = json_get_prop_strptr(json, "subtype");

	/* ts is EPOCH.0000ID, atol is sufficient */
	time_t mt = ts ? atol(ts) : 0;

	PurpleMessageFlags flags = PURPLE_MESSAGE_RECV;
	if (subtype)
		flags |= PURPLE_MESSAGE_SYSTEM; /* PURPLE_MESSAGE_NOTIFY? */
	if (json_get_prop_boolean(json, "hidden", FALSE))
		flags |= PURPLE_MESSAGE_INVISIBLE;

	SlackUser *user = user_id ? (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id) : NULL;
	SlackChannel *chan;
	if (user && slack_object_id_is(user->im, channel_id)) {
		/* IM */
		serv_got_im(sa->gc, user->name, text, flags, mt);
	} else if ((chan = channel_id ? (SlackChannel*)slack_object_hash_table_lookup(sa->channels, channel_id) : NULL)) {
		/* Channel */
		if (!chan->cid)
			return;
		serv_got_chat_in(sa->gc, chan->cid, user ? user->name : user_id ?: "", flags, text, mt);
	} else {
		purple_debug_warning("slack", "Unhandled message: %s@%s: %s\n", user_id, channel_id, text);
	}
}

void slack_user_typing(SlackAccount *sa, json_value *json) {
	const char *user_id    = json_get_prop_strptr(json, "user");
	const char *channel_id = json_get_prop_strptr(json, "channel");

	SlackUser *user = user_id ? (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id) : NULL;
	SlackChannel *chan;
	if (user && slack_object_id_is(user->im, channel_id)) {
		/* IM */
		serv_got_typing(sa->gc, user->name, 3, PURPLE_TYPING);
	} else if ((chan = channel_id ? (SlackChannel*)slack_object_hash_table_lookup(sa->channels, channel_id) : NULL)) {
		/* Channel */
		/* libpurple does not support chat typing indicators */
	} else {
		purple_debug_warning("slack", "Unhandled typing: %s@%s\n", user_id, channel_id);
	}
}

unsigned int slack_send_typing(PurpleConnection *gc, const char *who, PurpleTypingState state) {
	SlackAccount *sa = gc->proto_data;

	if (state != PURPLE_TYPING)
		return 0;

	SlackUser *user = g_hash_table_lookup(sa->user_names, who);
	if (!user || !*user->im)
		return 0;

	GString *channel = append_json_string(g_string_new(NULL), user->im);
	slack_rtm_send(sa, NULL, NULL, "typing", "channel", channel->str, NULL);
	g_string_free(channel, TRUE);

	return 3;
}
