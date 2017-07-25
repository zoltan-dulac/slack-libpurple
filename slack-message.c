#include <debug.h>

#include "slack-json.h"
#include "slack-rtm.h"
#include "slack-user.h"
#include "slack-channel.h"
#include "slack-message.h"

static gchar *slack_message_to_html(SlackAccount *sa, gchar *s, PurpleMessageFlags *flags) {
	g_return_val_if_fail(s, NULL);

	*flags |= PURPLE_MESSAGE_NO_LINKIFY;

	size_t l = strlen(s);
	char *end = &s[l];
	GString *html = g_string_sized_new(l);

	while (s < end) {
		char c = *s++;
		if (c == '\n') {
			g_string_append(html, "<BR>");
			continue;
		}
		if (c != '<') {
			g_string_append_c(html, c);
			continue;
		}

		/* found a <tag> */
		char *r = strchr(s, '>');
		if (!r)
			/* should really be error */
			r = end;
		else
			*r = 0;
		char *b = memchr(s, '|', r-s);
		if (b) {
			*b = 0;
			b++;
		}
		switch (*s) {
			case '#':
				s++;
				g_string_append_c(html, '#');
				if (!b) {
					SlackChannel *chan = (SlackChannel*)slack_object_hash_table_lookup(sa->channels, s);
					if (chan)
						b = chan->name;
				}
				g_string_append(html, b ?: s);
				break;
			case '@':
				s++;
				g_string_append_c(html, '@');
				if (!strcmp(s, sa->self))
					*flags |= PURPLE_MESSAGE_NICK;
				if (!b) {
					SlackUser *user = (SlackUser*)slack_object_hash_table_lookup(sa->users, s);
					if (user)
						b = user->name;
				}
				g_string_append(html, b ?: s);
				break;
			case '!':
				s++;
				if (!strcmp(s, "channel") || !strcmp(s, "group") || !strcmp(s, "here") || !strcmp(s, "everyone")) {
					*flags |= PURPLE_MESSAGE_NOTIFY;
					g_string_append_c(html, '@');
					g_string_append(html, b ?: s);
				} else {
					g_string_append(html, "&lt;");
					g_string_append(html, b ?: s);
					g_string_append(html, "&gt;");
				}
				break;
			default:
				/* URL */
				g_string_append(html, "<A HREF=\"");
				g_string_append(html, s); /* XXX embedded quotes? */
				g_string_append(html, "\">");
				g_string_append(html, b ?: s);
				g_string_append(html, "</A>");
		}
		s = r+1;
	}

	return g_string_free(html, FALSE);
}

void slack_message(SlackAccount *sa, json_value *json) {
	const char *user_id    = json_get_prop_strptr(json, "user");
	const char *channel_id = json_get_prop_strptr(json, "channel");
	const char *subtype    = json_get_prop_strptr(json, "subtype");

	time_t mt = slack_parse_time(json_get_prop(json, "ts"));

	PurpleMessageFlags flags = PURPLE_MESSAGE_RECV;
	/* TODO: "me_message" */
	if (subtype)
		flags |= PURPLE_MESSAGE_SYSTEM; /* PURPLE_MESSAGE_NOTIFY? */
	if (json_get_prop_boolean(json, "hidden", FALSE))
		flags |= PURPLE_MESSAGE_INVISIBLE;

	char *html = slack_message_to_html(sa, json_get_prop_strptr(json, "text"), &flags);

	SlackUser *user = (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id);
	SlackChannel *chan;
	if (user && slack_object_id_is(user->im, channel_id)) {
		/* IM */
		serv_got_im(sa->gc, user->name, html, flags, mt);
	} else if ((chan = (SlackChannel*)slack_object_hash_table_lookup(sa->channels, channel_id))) {
		/* Channel */
		if (!chan->cid)
			return;

		PurpleConvChat *conv;
		if (subtype && (conv = slack_channel_get_conversation(sa, chan))) {
			if (!strcmp(subtype, "channel_topic") ||
					!strcmp(subtype, "group_topic"))
				purple_conv_chat_set_topic(conv, user ? user->name : user_id, json_get_prop_strptr(json, "topic"));
		}

		serv_got_chat_in(sa->gc, chan->cid, user ? user->name : user_id ?: "", flags, html, mt);
	} else {
		purple_debug_warning("slack", "Unhandled message: %s@%s: %s\n", user_id, channel_id, html);
	}
}

void slack_user_typing(SlackAccount *sa, json_value *json) {
	const char *user_id    = json_get_prop_strptr(json, "user");
	const char *channel_id = json_get_prop_strptr(json, "channel");

	SlackUser *user = (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id);
	SlackChannel *chan;
	if (user && slack_object_id_is(user->im, channel_id)) {
		/* IM */
		serv_got_typing(sa->gc, user->name, 3, PURPLE_TYPING);
	} else if ((chan = (SlackChannel*)slack_object_hash_table_lookup(sa->channels, channel_id))) {
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

void slack_member_joined_channel(SlackAccount *sa, json_value *json, gboolean joined) {
	SlackChannel *chan = (SlackChannel*)slack_object_hash_table_lookup(sa->channels, json_get_prop_strptr(json, "channel"));
	if (!chan)
		return;

	PurpleConvChat *conv = slack_channel_get_conversation(sa, chan);
	if (!conv)
		return;

	const char *user_id = json_get_prop_strptr(json, "user");
	SlackUser *user = (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id);
	if (joined)
		purple_conv_chat_add_user(conv, user ? user->name : user_id, NULL, PURPLE_CBFLAGS_VOICE, TRUE);
	else
		purple_conv_chat_remove_user(conv, user ? user->name : user_id, NULL);
}
