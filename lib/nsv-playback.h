#ifndef NSV_PLAYBACK_H
#define NSV_PLAYBACK_H

typedef struct _NsvPlayback NsvPlayback;

NsvPlayback *nsv_playback_new();

gboolean nsv_playback_play(NsvPlayback *self);
gboolean nsv_playback_stop(NsvPlayback *self);

#endif // NSV_PLAYBACK_H
