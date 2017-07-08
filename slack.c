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

static const char *slack_list_icon(G_GNUC_UNUSED PurpleAccount * account, G_GNUC_UNUSED PurpleBuddy * buddy) {
	return "slack";
}

static GList *slack_status_types(G_GNUC_UNUSED PurpleAccount *acct) {
	GList *types = NULL;

	types = g_list_append(types,
		purple_status_type_new_full(PURPLE_STATUS_AVAILABLE, NULL, NULL, TRUE, TRUE, FALSE));

	types = g_list_append(types,
		purple_status_type_new_full(PURPLE_STATUS_OFFLINE, NULL, NULL, TRUE, TRUE, FALSE));

	return types;
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

	const char *username = purple_account_get_username(account);
	const char *host = strrchr(username, '@');
	sa->api_url = g_strdup_printf("https://%s/api", host ? host+1 : "slack.com");

	sa->token = g_strdup(purple_url_encode(token));

	sa->users    = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)slack_user_free);
	/*
	sa->ims      = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)slack_im_free);
	sa->channels = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)slack_channel_free);
	sa->groups   = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)slack_group_free);
	*/

	purple_connection_set_display_name(gc, username);
	purple_connection_set_state(gc, PURPLE_CONNECTING);

	slack_rtm_connect(sa);
}

static void slack_close(PurpleConnection *gc) {
	SlackAccount *sa = gc->proto_data;

	if (!sa)
		return;

	if (sa->rtm)
		purple_websocket_abort(sa->rtm);

	g_hash_table_destroy(sa->groups);
	g_hash_table_destroy(sa->channels);
	g_hash_table_destroy(sa->ims);
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
	NULL,			/* chat_info */
	NULL,			/* chat_info_defaults */
	slack_login,		/* login */
	slack_close,		/* close */
	NULL,			/* send_im */
	NULL,			/* set_info */
	NULL,			/* send_typing */
	NULL,			/* get_info */
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
	NULL,			/* join_chat */	
	NULL,			/* reject chat invite */
	NULL,			/* get_chat_name */
	NULL,			/* chat_invite */
	NULL,			/* chat_leave */
	NULL,			/* chat_whisper */
	NULL,			/* chat_send */
	NULL,			/* keepalive */
	NULL,			/* register_user */
	NULL,			/* get_cb_info */
	NULL,			/* get_cb_away */
	NULL,			/* alias_buddy */
	NULL,			/* group_buddy */
	NULL,			/* rename_group */
	NULL,			/* buddy_free */
	NULL,			/* convo_closed */
	NULL,			/* normalize */
	NULL,			/* set_buddy_icon */
	NULL,			/* remove_group */
	NULL,			/* get_cb_real_name */
	NULL,			/* set_chat_topic */
	NULL,			/* find_blist_chat */
	NULL,			/* roomlist_get_list */
	NULL,			/* roomlist_cancel */
	NULL,			/* roomlist_expand_category */
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
    "0.1" ,
    "Slack protocol plugin",          
    "Add slack protocol support to libpurple.",          
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
