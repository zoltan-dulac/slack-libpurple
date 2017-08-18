#include <debug.h>
#include <version.h>

#include "slack-json.h"
#include "slack-rtm.h"
#include "slack-api.h"
#include "slack-user.h"
#include "slack-channel.h"
#include "slack-message.h"

gchar *slack_html_to_message(SlackAccount *sa, const char *s, PurpleMessageFlags flags) {
	if (flags & PURPLE_MESSAGE_RAW)
		return g_strdup(s);

	GString *msg = g_string_sized_new(strlen(s));
	while (*s) {
		const char *ent;
		int len;
		if ((ent = purple_markup_unescape_entity(s, &len))) {
			if (!strcmp(ent, "&"))
				g_string_append(msg, "&amp;");
			else if (!strcmp(ent, "<"))
				g_string_append(msg, "&lt;");
			else if (!strcmp(ent, ">"))
				g_string_append(msg, "&gt;");
			else
				g_string_append(msg, ent);
			s += len;
		}
		else if (!g_ascii_strncasecmp(s, "<br>", 4)) {
			g_string_append_c(msg, '\n');
			s += 4;
		} else {
			/* what about other tags? user/channel refs? urls? dates? */
			g_string_append_c(msg, *s++);
		}
	}

	return g_string_free(msg, FALSE);
}

static gchar *slack_message_to_html(SlackAccount *sa, gchar *s, const char *subtype, PurpleMessageFlags *flags) {
	g_return_val_if_fail(s, NULL);

	size_t l = strlen(s);
	char *end = &s[l];
	GString *html = g_string_sized_new(l);

	if (!g_strcmp0(subtype, "me_message"))
		g_string_append(html, "/me ");
	else if (subtype)
		*flags |= PURPLE_MESSAGE_SYSTEM;
	*flags |= PURPLE_MESSAGE_NO_LINKIFY;

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
				SlackUser *user = NULL;
				if (slack_object_id_is(sa->self->object.id, s)) {
					user = sa->self;
					*flags |= PURPLE_MESSAGE_NICK;
				}
				if (!b) {
					if (!user)
						user = (SlackUser*)slack_object_hash_table_lookup(sa->users, s);
					if (user)
						b = user->name;
				}
				g_string_append(html, b ?: s);
				break;
			case '!':
				s++;
				if (!strcmp(s, "channel") || !strcmp(s, "group") || !strcmp(s, "here") || !strcmp(s, "everyone")) {
					*flags |= PURPLE_MESSAGE_NICK;
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

static void handle_message(SlackAccount *sa, SlackObject *obj, json_value *json, PurpleMessageFlags flags) {
	const char *user_id    = json_get_prop_strptr(json, "user");
	const char *subtype    = json_get_prop_strptr(json, "subtype");
	time_t mt = slack_parse_time(json_get_prop(json, "ts"));

	if (json_get_prop_boolean(json, "hidden", FALSE))
		flags |= PURPLE_MESSAGE_INVISIBLE;

	SlackUser *user = NULL;
	if (slack_object_id_is(sa->self->object.id, user_id)) {
		user = sa->self;
#if PURPLE_VERSION_CHECK(2,12,0)
		flags |= PURPLE_MESSAGE_REMOTE_SEND;
#endif
	}

	char *html = slack_message_to_html(sa, json_get_prop_strptr(json, "text"), subtype, &flags);

	if (SLACK_IS_CHANNEL(obj)) {
		SlackChannel *chan = (SlackChannel*)obj;
		/* Channel */
		if (!chan->cid) {
			if (!purple_account_get_bool(sa->account, "open_chat", FALSE))
				return;
			slack_chat_open(sa, chan);
		}

		if (!user)
			user = (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id);

		PurpleConvChat *conv;
		if (subtype && (conv = slack_channel_get_conversation(sa, chan))) {
			if (!strcmp(subtype, "channel_topic") ||
					!strcmp(subtype, "group_topic"))
				purple_conv_chat_set_topic(conv, user ? user->name : user_id, json_get_prop_strptr(json, "topic"));
		}

		serv_got_chat_in(sa->gc, chan->cid, user ? user->name : user_id ?: "", flags, html, mt);
	} else if (SLACK_IS_USER(obj)) {
		SlackUser *im = (SlackUser*)obj;
		/* IM */
		if (slack_object_id_is(im->object.id, user_id))
			serv_got_im(sa->gc, im->name, html, flags, mt);
		else {
			PurpleConversation *conv = purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, im->name, sa->account);
			if (!conv)
				conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, sa->account, im->name);
			if (!user)
				/* is this necessary? shouldn't be anyone else in here */
				user = (SlackUser*)slack_object_hash_table_lookup(sa->users, user_id);
			purple_conversation_write(conv, user ? user->name : user_id, html, flags, mt);
		}
	}
}

void slack_message(SlackAccount *sa, json_value *json) {
	const char *channel_id = json_get_prop_strptr(json, "channel");

	handle_message(sa, slack_object_hash_table_lookup(sa->channels, channel_id)
			?: slack_object_hash_table_lookup(sa->ims,      channel_id),
			json, PURPLE_MESSAGE_RECV);
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
		/* TODO: purple_conv_chat_user_set_flags (though nothing seems to use this) */
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

static void get_history_cb(SlackAccount *sa, gpointer data, json_value *json, const char *error) {
	SlackObject *obj = data;
	json_value *list = json_get_prop_type(json, "messages", array);

	if (!list || error) {
		purple_debug_error("slack", "Error loading channel history: %s\n", error ?: "missing");
		g_object_unref(obj);
		return;
	}

	/* what order are these in? */
	for (unsigned i = list->u.array.length; i; i --) {
		json_value *msg = list->u.array.values[i-1];
		if (g_strcmp0(json_get_prop_strptr(msg, "type"), "message"))
			continue;

		handle_message(sa, obj, msg, PURPLE_MESSAGE_RECV | PURPLE_MESSAGE_DELAYED | PURPLE_MESSAGE_NO_LOG);
	}

	g_object_unref(obj);
}

void slack_get_history(SlackAccount *sa, SlackObject *obj, const char *since, unsigned count) {
	const char *call = NULL, *id = NULL;
	if (SLACK_IS_CHANNEL(obj)) {
		SlackChannel *chan = (SlackChannel*)obj;
		if (!chan->cid)
			slack_chat_open(sa, chan);
		switch (chan->type) {
			case SLACK_CHANNEL_MEMBER:
				call = "channels.history";
				break;
			case SLACK_CHANNEL_GROUP:
				call = "groups.history";
				break;
			case SLACK_CHANNEL_MPIM:
				call = "mpim.history";
				break;
			default:
				break;
		}
		id = chan->object.id;
	} else if (SLACK_IS_USER(obj)) {
		SlackUser *user = (SlackUser*)obj;
		if (*user->im) {
			call = "im.history";
			id = user->im;
		}
	}

	if (!call)
		return;

	char count_buf[6] = "";
	snprintf(count_buf, 5, "%u", count);
	slack_api_call(sa, get_history_cb, g_object_ref(obj), call, "channel", id, "oldest", since ?: "0", "count", count_buf, NULL);
}
