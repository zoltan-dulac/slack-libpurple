#include "slack-object.h"

guint slack_object_id_hash(gconstpointer p) {
	const guint *x = p+1;
	return x[0] ^ (x[1] << 1);
}

gboolean slack_object_id_equal(gconstpointer a, gconstpointer b) {
	return !slack_object_id_cmp(a, b);
}

G_DEFINE_ABSTRACT_TYPE(SlackObject, slack_object, G_TYPE_OBJECT);

static void slack_object_class_init(SlackObjectClass *klass) {
}

static void slack_object_init(SlackObject *self) {
}
