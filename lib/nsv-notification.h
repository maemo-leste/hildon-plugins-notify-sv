#ifndef NSV_NOTIFICATION_H
#define NSV_NOTIFICATION_H

#include <glib.h>

#define NSV_CATEGORY_RINGTONE "Ringtone"
#define NSV_CATEGORY_CALENDAR "Calendar"
#define NSV_CATEGORY_CLOCK "Clock"
#define NSV_CATEGORY_SMS "SMS"
#define NSV_CATEGORY_EMAIL "Email"
#define NSV_CATEGORY_CHAT "Chat"
#define NSV_CATEGORY_SYSTEM "System"
#define NSV_CATEGORY_SOUND "Sound"
#define NSV_CATEGORY_CRITICAL "Critical"

typedef struct nsv_notification nsv_notification;

struct notification_impl
{
  gboolean (*initialize)(struct nsv_notification *);
  gboolean (*shutdown)(struct nsv_notification *);
  gboolean (*play)(struct nsv_notification *);
  gboolean (*stop)(struct nsv_notification *);
  const char *subtype;
  const char *type;
  int priority;
  int flags;
  /* Called when the level a running sound plays at has changed. The
   * implementation owns the playback object, so it is the only one that
   * can put the new level on it. */
  void (*volume_changed)(struct nsv_notification *, int volume);
};

gboolean nsv_notification_init();
void nsv_notification_shutdown();

struct nsv_notification *nsv_notification_new(const char *category);
void nsv_notification_finish(struct nsv_notification *n);

/* Re-applies the profile level to the sound that is playing, if its
 * category follows that level. */
void nsv_notification_volume_changed(int volume);
void nsv_notification_finish_by_sender(const char *sender);
void nsv_notification_finish_by_category(const char *category);
void nsv_notification_error(struct nsv_notification *n);

gint nsv_notification_start(struct nsv_notification *n);
void nsv_notification_stop(gint id);

gboolean nsv_notification_has_events();

void nsv_notification_register(const char *type,
                               struct notification_impl *event);

#endif // NSV_NOTIFICATION_H
