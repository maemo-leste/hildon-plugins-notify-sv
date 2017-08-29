#ifndef NSV_H
#define NSV_H

enum {
  UNKNOWN_EVENT,
  RINGTONE_EVENT,
  CALENDAR_EVENT,
  CLOCK_EVENT,
  CHAT_EVENT,
  MESSAGE_EVENT,
  SMS_EVENT,
  EMAIL_EVENT,
  SOUND_EVENT
};

gboolean nsv_initialize_with_x11(void);
void nsv_shutdown();

gint nsv_play(const char *category, const char *sound_file, int volume,
              const char *vibra_pattern, gchar *sender, gboolean override);
void nsv_stop(gint id);

gint nsv_sv_play_event(void *plugin, unsigned int event, const char *sound_file,
                       gboolean sound_enabled, const char *vibra_pattern,
                       gboolean vibra_enabled, int volume);
void nsv_sv_stop_event(void *plugin, gint id);

void nsv_sv_init(void **sv);
void nsv_sv_shutdown(void *sv);

#endif // NSV_H
