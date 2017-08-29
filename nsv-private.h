#ifndef NSV_PRIVATE_H
#define NSV_PRIVATE_H

#include "nsv-policy.h"

enum notification_event_status_e
{
  UNKNOWN = 0,
  INITIALIZED = 1,
  PLAYING = 2,
  STOPPED = 3,
  SHUTDOWN = 4
};

struct notification_event_status
{
  NsvPolicy *policy;
  gboolean playing;
  gboolean fallback;
  enum notification_event_status_e status;
  int stopped;
};

struct nsv_notification
{
  gchar *type;
  gint id;
  gchar *sound_file;
  gchar *fallback_sound_file;
  int volume;
  gboolean sound_enabled;
  gchar *vibra_pattern;
  gboolean vibra_enabled;
  gboolean play_granted;
  gchar *sender;
  struct notification_event_status *event_status;
  void *private;
};

#endif // NSV_PRIVATE_H
