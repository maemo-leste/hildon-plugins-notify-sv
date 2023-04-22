#ifndef NSVPROFILE_H
#define NSVPROFILE_H

#include <glib.h>

typedef struct _NsvProfile NsvProfile;

NsvProfile *nsv_profile_get_instance();

GList *nsv_profile_tones_get_all(NsvProfile *self);
GList *nsv_profile_get_volume_keys(NsvProfile *self);
GList *nsv_profile_get_tone_keys(NsvProfile *self);
const char *nsv_profile_get_vibra_pattern(NsvProfile *self, const char *profile);
const char *nsv_profile_get_fallback(NsvProfile *self, const char *profile);
const char *nsv_profile_get_tone(NsvProfile *self, const char *profile);
int nsv_profile_get_volume(NsvProfile *self, const char *profile);
int nsv_profile_get_system_volume(NsvProfile *self);
gboolean nsv_profile_is_silent_mode(NsvProfile *self);
gboolean nsv_profile_is_vibra_enabled(NsvProfile *self);

void nsv_profile_set_tone(NsvProfile *self, const char *profile, char *val);

#endif // NSVPROFILE_H
