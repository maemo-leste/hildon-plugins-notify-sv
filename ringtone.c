#include <glib-object.h>

#include <stdio.h>

#include "sp_timestamp.h"

#include "nsv-private.h"
#include "nsv-notification.h"
#include "nsv-playback.h"
#include "nsv-util.h"

#include "ringtone.h"

struct ringtone_private
{
  NsvPlayback *playback;
};

static gboolean
ringtone_initialize(nsv_notification *n)
{
  g_assert(n != NULL);
  g_assert(n->private == NULL);

  if ((n->private = g_new0(struct ringtone_private, 1)))
    return TRUE;

  return FALSE;
}

static gboolean
ringtone_shutdown(nsv_notification *n)
{
  g_assert(n != NULL);
  g_assert(n->private != NULL);

  g_free(n->private);
  n->private = NULL;
  return TRUE;
}

static void
_ringtone_playback_error_cb(NsvPlayback *self, nsv_notification *n)
{
  nsv_notification_error(n);
}

static void
_ringtone_playback_started_cb(NsvPlayback *self, nsv_notification *n)
{
  sp_timestamp("hildon-plugins-notify-sv: playing: Ringtone");

  if (n->vibra_pattern && n->vibra_enabled)
    nsv_vibra_start(n->vibra_pattern);
}

static void
_ringtone_playback_succeeded_cb(NsvPlayback *self, nsv_notification *n)
{
  nsv_notification_finish(n);
}

static gboolean
ringtone_play(nsv_notification *n)
{
  struct ringtone_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct ringtone_private *)n->private;

  if (n->play_granted)
  {
    if (n->sound_enabled)
    {
      priv->playback = nsv_playback_new();
      g_object_set(G_OBJECT(priv->playback),
                   "filename", n->sound_file,
                   "volume", n->volume,
                   "repeat", TRUE,
                   "min-timeout", 3000,
                   "event-id", "phone-incoming-call",
                   NULL);
      g_signal_connect(G_OBJECT(priv->playback), "error",
                       G_CALLBACK(_ringtone_playback_error_cb), n);
      g_signal_connect(G_OBJECT(priv->playback), "started",
                       G_CALLBACK(_ringtone_playback_started_cb), n);
      g_signal_connect(G_OBJECT(priv->playback), "succeeded",
                       G_CALLBACK(_ringtone_playback_succeeded_cb), n);
      nsv_playback_play(priv->playback);
    }
    else if (n->vibra_enabled && n->vibra_pattern)
      nsv_vibra_start(n->vibra_pattern);
  }
  else
    nsv_knock_start(79); /* TONE_WAIT, see rfc4733.c#L143 */

  return TRUE;
}

static gboolean
ringtone_stop(nsv_notification *n)
{
  struct ringtone_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct ringtone_private *)n->private;

  if (n->play_granted)
  {
    if (n->vibra_pattern && n->vibra_enabled )
        nsv_vibra_stop(n->vibra_pattern);

    if (priv->playback)
    {
      nsv_playback_stop(priv->playback);
      g_object_unref(priv->playback);
      priv->playback = NULL;
    }
  }
  else
    nsv_knock_stop(79);

  return TRUE;
}

static struct notification_impl ringtone =
{
  ringtone_initialize,
  ringtone_shutdown,
  ringtone_play,
  ringtone_stop,
  "Ringtone",
  "Ringtone",
  100,
  2
}; // weak

void
register_ringtone()
{
  nsv_notification_register("Ringtone", &ringtone);
}
