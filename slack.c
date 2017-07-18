#ifndef PURPLE_PLUGINS
#define PURPLE_PLUGINS
#endif

#include <string.h>

#include <accountopt.h>
#include <debug.h>
#include <plugin.h>
#include <version.h>

#include "slack.h"
#include "slack-rtm.h"
#include "slack-user.h"
#include "slack-im.h"
#include "slack-channel.h"
#include "slack-blist.h"
#include "slack-message.h"

static const char *slack_list_icon(G_GNUC_UNUSED PurpleAccount * account, G_GNUC_UNUSED PurpleBuddy * buddy) {
	return "slack";
}

static GList *slack_status_types(G_GNUC_UNUSED PurpleAccount *acct) {
	GList *types = NULL;

	types = g_list_append(types,
		purple_status_type_new(PURPLE_STATUS_AVAILABLE, "active", "active", TRUE));

	types = g_list_append(types,
		purple_status_type_new(PURPLE_STATUS_AWAY, "away", "away", TRUE));

	/* Even though slack never says anyone is offline, we need this status.
	 * (Maybe could treat deleted users as this?)
	 */
	types = g_list_append(types,
		purple_status_type_new(PURPLE_STATUS_OFFLINE, NULL, NULL, TRUE));

	return types;
}

static GList *slack_chat_info(PurpleConnection *gc) {
	GList *l = NULL;

	struct proto_chat_entry *e;
	e = g_new0(struct proto_chat_entry, 1);
	e->label = "_Channel:";
	e->identifier = "name";
	e->required = TRUE;
	l = g_list_append(l, e);

	return l;
}

GHashTable *slack_chat_info_defaults(PurpleConnection *gc, const char *name) {
	/* While the docs say to use NULL key_destructor, libpurple actually uses g_free when loading buddies, so matching that here instead */
	GHashTable *info = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	if (name)
		g_hash_table_insert(info, g_strdup("name"), g_strdup(name));
	/* we could look up the channel here and add more... */

	return info;
}

static char *slack_get_chat_name(GHashTable *info) {
	return g_hash_table_lookup(info, "name");
}

static void slack_login(PurpleAccount *account) {
	PurpleConnection *gc = purple_account_get_connection(account);

	const gchar *token = purple_account_get_string(account, "api_token", NULL);
	if (!token)
	{
		purple_connection_error_reason(gc,
			PURPLE_CONNECTION_ERROR_INVALID_SETTINGS, "API token required");
		return;
	}

	SlackAccount *sa = g_new0(SlackAccount, 1);
	gc->proto_data = sa;
	sa->account = account;
	sa->gc = gc;

	const char *host = strrchr(account->username, '@');
	sa->api_url = g_strdup_printf("https://%s/api", host ? host+1 : "slack.com");

	sa->token = g_strdup(purple_url_encode(token));

	sa->rtm_call = g_hash_table_new_full(g_direct_hash,        g_direct_equal,        NULL, (GDestroyNotify)slack_rtm_cancel);

	sa->users    = g_hash_table_new_full(slack_object_id_hash, slack_object_id_equal, NULL, g_object_unref);
	sa->user_names = g_hash_table_new_full(g_str_hash,         g_str_equal,           NULL, NULL);
	sa->ims      = g_hash_table_new_full(slack_object_id_hash, slack_object_id_equal, NULL, NULL);

	sa->channels = g_hash_table_new_full(slack_object_id_hash, slack_object_id_equal, NULL, g_object_unref);
	sa->channel_names = g_hash_table_new_full(g_str_hash,      g_str_equal,           NULL, NULL);
	sa->channel_cids = g_hash_table_new_full(g_direct_hash,    g_direct_equal,        NULL, NULL);

	sa->buddies = g_hash_table_new_full(/* slack_object_id_hash, slack_object_id_equal, */ g_str_hash, g_str_equal, NULL, NULL);

	purple_connection_set_display_name(gc, account->alias ?: account->username);
	purple_connection_set_state(gc, PURPLE_CONNECTING);

	/* connect order (SLACK_CONNECT_STEPS):
		1. slack_rtm_connect
		2. slack_connect_cb
		   purple_websocket_connect
		3. rtm_cb
		   rtm_msg("hello")
		4. slack_users_load
		5. slack_ims_load
		6. slack_channels_load
		7. slack_groups_load
	*/
	slack_rtm_connect(sa);
}

static void slack_close(PurpleConnection *gc) {
	SlackAccount *sa = gc->proto_data;

	if (!sa)
		return;

	if (sa->rtm)
		purple_websocket_abort(sa->rtm);
	g_hash_table_destroy(sa->rtm_call);

	g_hash_table_destroy(sa->buddies);

	g_hash_table_destroy(sa->channel_cids);
	g_hash_table_destroy(sa->channel_names);
	g_hash_table_destroy(sa->channels);

	g_hash_table_destroy(sa->ims);
	g_hash_table_destroy(sa->user_names);
	g_hash_table_destroy(sa->users);
	g_free(sa->team.id);
	g_free(sa->team.name);
	g_free(sa->team.domain);
	g_free(sa->self);

	g_free(sa->api_url);
	g_free(sa->token);
	g_free(sa);
	gc->proto_data = NULL;
}

static PurplePluginProtocolInfo prpl_info = {
	/* options */
	OPT_PROTO_CHAT_TOPIC | OPT_PROTO_NO_PASSWORD,	/*| OPT_PROTO_SLASH_COMMANDS_NATIVE, */
	NULL,			/* user_splits */
	NULL,			/* protocol_options */
	NO_BUDDY_ICONS,
	slack_list_icon,	/* list_icon */
	NULL,			/* list_emblems */
	NULL,			/* status_text */
	NULL,			/* tooltip_text */
	slack_status_types,	/* status_types */
	NULL,			/* blist_node_menu */
	slack_chat_info,	/* chat_info */
	slack_chat_info_defaults, /* chat_info_defaults */
	slack_login,		/* login */
	slack_close,		/* close */
	slack_send_im,		/* send_im */
	NULL,			/* set_info */
	slack_send_typing,	/* send_typing */
	slack_get_info,		/* get_info */
	NULL,			/* set_status */
	NULL,			/* set_idle */
	NULL,			/* change_passwd */
	NULL,			/* add_buddy */
	NULL,			/* add_buddies */
	NULL,			/* remove_buddy */
	NULL,			/* remove_buddies */
	NULL,			/* add_permit */
	NULL,			/* add_deny */
	NULL,			/* rem_permit */
	NULL,			/* rem_deny */
	NULL,			/* set_permit_deny */
	slack_join_chat,	/* join_chat */	
	NULL,			/* reject chat invite */
	slack_get_chat_name,	/* get_chat_name */
	NULL,			/* chat_invite */
	slack_chat_leave,	/* chat_leave */
	NULL,			/* chat_whisper */
	slack_chat_send,	/* chat_send */
	NULL,			/* keepalive */
	NULL,			/* register_user */
	NULL,			/* get_cb_info */
	NULL,			/* get_cb_away */
	NULL,			/* alias_buddy */
	NULL,			/* group_buddy */
	NULL,			/* rename_group */
	slack_buddy_free,	/* buddy_free */
	NULL,			/* convo_closed */
	NULL,			/* normalize */
	NULL,			/* set_buddy_icon */
	NULL,			/* remove_group */
	NULL,			/* get_cb_real_name */
	NULL,			/* set_chat_topic */
	slack_find_blist_chat,	/* find_blist_chat */
	slack_roomlist_get_list,/* roomlist_get_list */
	NULL,			/* roomlist_cancel */
	slack_roomlist_expand_category,	/* roomlist_expand_category */
	NULL,			/* can_receive_file */
	NULL,			/* send_file */
	NULL,			/* new_xfer */
	NULL,			/* offline_message */
	NULL,			/* whiteboard_prpl_ops */
	NULL,			/* send_raw */
	NULL,			/* roomlist_room_serialize */
	NULL,			/* unregister_user */
	NULL,			/* send_attention */
	NULL,			/* attention_types */
	sizeof(PurplePluginProtocolInfo),	/* struct_size */
	NULL,			/*campfire_get_account_text_table *//* get_account_text_table */
	NULL,			/* initiate_media */
	NULL,			/* get_media_caps */
	NULL,			/* get_moods */
	NULL,			/* set_public_alias */
	NULL,			/* get_public_alias */
	NULL,			/* add_buddy_with_invite */
	NULL,			/* add_buddies_with_invite */
};

static PurplePluginInfo info = {
    PURPLE_PLUGIN_MAGIC,
    PURPLE_MAJOR_VERSION,
    PURPLE_MINOR_VERSION,
    PURPLE_PLUGIN_PROTOCOL,
    NULL,
    0,
    NULL,
    PURPLE_PRIORITY_DEFAULT,
    SLACK_PLUGIN_ID,
    "Slack",
    "0.1",
    "Slack protocol plugin",
    "Slack protocol support for libpurple.",
    "Dylan Simon <dylan@dylex.net>, Valeriy Golenkov <valery.golenkov@gmail.com>",                          
    "http://github.com/dylex/slack-libpurple",     
    NULL,                   
    NULL,                          
    NULL,                          
    NULL,                          
    &prpl_info,	/* extra info */ 
    NULL,                        
    NULL,                   
    NULL,                          
    NULL,                          
    NULL,                          
    NULL                           
};                               
    
static void init_plugin(G_GNUC_UNUSED PurplePlugin *plugin)
{
	prpl_info.user_splits = g_list_append(prpl_info.user_splits,
		purple_account_user_split_new("Host", "slack.com", '@'));

	prpl_info.protocol_options = g_list_append(prpl_info.protocol_options,
		purple_account_option_string_new("API token", "api_token", ""));
}

PURPLE_INIT_PLUGIN(slack, init_plugin, info);
