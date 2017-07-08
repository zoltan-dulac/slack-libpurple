#ifndef _SLACK_PLUGIN_H
#define _SLACK_PLUGIN_H

/*system includes*/
#include <glib/gi18n.h>

/*purple includes*/
#include <plugin.h>
#include <prpl.h>

#include "purple-websocket.h"

typedef struct _SlackAccount {
	PurpleAccount *account;
	PurpleConnection *pc;
	PurpleSslConnection *gsc;
	gchar *token;
	gboolean needs_join;
	guint poll_timeout;
	guint connection_timeout;
	gchar *last_message_update_time;

	GSList *dns_queries;
	GHashTable *cookie_table;
	GHashTable *hostname_ip_cache;
	PurpleWebsocket *rtm;
	GSList *conns; /**< A list of all active Connections */
	GQueue *waiting_conns; /**< A list of all Connections waiting to process */

	GSList *channels;
	GHashTable *channel; /* id -> slack_channel */
} SlackAccount;


typedef enum
{
	BUDDY_USER    = 0x0001,
	BUDDY_CHANNEL = 0x0002,
} SlackPerson;


typedef struct _SlackBuddy {
	SlackAccount *sa;
	PurpleBuddy *buddy;
	
	gchar *slack_id;
	gchar *nickname;
	gchar *avatar;
	guint personflags;
	guint personastateflags;
} SlackBuddy;


#endif // _SLACK_PLUGIN_H
