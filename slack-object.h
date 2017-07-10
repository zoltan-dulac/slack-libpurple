#ifndef _PURPLE_SLACK_OBJECT_H
#define _PURPLE_SLACK_OBJECT_H

#include <string.h>

#include <glib-object.h>

/* object IDs seem to always be of the form "TXXXXXXXX" where T is a type identifier and X are [0-9A-Z] (base32?) */
#define SLACK_OBJECT_ID_LEN	11
/* These may be safely treated as strings (always NULL terminated and padded),
 * but strings are not valid slack_object_ids */
typedef char slack_object_id[SLACK_OBJECT_ID_LEN+1];

static inline void slack_object_id_set(slack_object_id id, const char *s) {
	if (s) {
		strncpy(id, s, SLACK_OBJECT_ID_LEN);
		id[SLACK_OBJECT_ID_LEN] = 0;
	} else
		memset(id, 0, SLACK_OBJECT_ID_LEN+1);
}

static inline gboolean slack_object_id_is(const slack_object_id id, const char *s) {
	return s ? !strncmp(id, s, SLACK_OBJECT_ID_LEN) : !*id;
}

#define slack_object_id_copy(dst, src) \
	memcpy(dst, src, SLACK_OBJECT_ID_LEN+1)
#define slack_object_id_cmp(a, b) \
	memcmp(a, b, SLACK_OBJECT_ID_LEN)

guint slack_object_id_hash(gconstpointer id);
gboolean slack_object_id_equal(gconstpointer a, gconstpointer b);

struct _SlackObject {
	GObject parent;

	slack_object_id id;
};

#define SLACK_TYPE_OBJECT slack_object_get_type()
G_DECLARE_FINAL_TYPE(SlackObject, slack_object, SLACK, OBJECT, GObject)

#define slack_object_hash_table_new() \
	g_hash_table_new_full(slack_object_id_hash, slack_object_id_equal, NULL, g_object_unref)

static inline gboolean slack_object_hash_table_replace(GHashTable *hash_table, SlackObject *obj) {
	return g_hash_table_replace(hash_table, obj->id, obj);
}

static inline SlackObject *slack_object_hash_table_lookup(GHashTable *hash_table, const char *sid) {
	slack_object_id id;
	slack_object_id_set(id, sid);
	return g_hash_table_lookup(hash_table, id);
}

static inline gboolean slack_object_hash_table_remove(GHashTable *hash_table, const char *sid) {
	slack_object_id id;
	slack_object_id_set(id, sid);
	return g_hash_table_remove(hash_table, id);
}

static inline SlackObject *slack_object_hash_table_take(GHashTable *hash_table, const char *sid) {
	slack_object_id id;
	slack_object_id_set(id, sid);
	SlackObject *obj = g_hash_table_lookup(hash_table, id);
	if (obj) g_hash_table_steal(hash_table, id);
	return obj;
}

static inline SlackObject *slack_object_hash_table_get(GHashTable *hash_table, GType type, const char *sid) __attribute__((returns_nonnull));
static inline SlackObject *slack_object_hash_table_get(GHashTable *hash_table, GType type, const char *sid) {
	slack_object_id id;
	slack_object_id_set(id, sid);
	SlackObject *obj = g_hash_table_lookup(hash_table, id);
	if (!obj) {
		obj = g_object_new(type, NULL);
		slack_object_id_copy(obj->id, id);
		slack_object_hash_table_replace(hash_table, obj);
	}
	return obj;
}

static inline gboolean slack_object_has_id(const SlackObject *obj, const char *s) {
	return slack_object_id_is(obj->id, s);
}

#endif
