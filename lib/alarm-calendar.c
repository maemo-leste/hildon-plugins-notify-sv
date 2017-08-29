#include <glib-object.h>

#include <stdio.h>

#include "sp_timestamp.h"

#include "nsv-private.h"
#include "nsv-notification.h"
#include "nsv-playback.h"
#include "nsv-util.h"

#include "alarm-calendar.h"

struct alarm_calendar_private
{
  NsvPlayback *playback;
  guint tone_timeout_id;
};

static gboolean
calendar_initialize(nsv_notification *n)
{
  g_assert(n != NULL);
  g_assert(n->private == NULL);

  if ((n->private = g_new0(struct alarm_calendar_private, 1)))
    return TRUE;

  return FALSE;
}

static gboolean
calendar_shutdown(nsv_notification *n)
{
  struct alarm_calendar_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct alarm_calendar_private *)n->private;

  if (priv->playback)
    g_object_unref(priv->playback);

  g_free(priv);
  n->private = NULL;

  return TRUE;
}

static void
_alarm_calendar_playback_error_cb(NsvPlayback *self, nsv_notification *n)
{
  nsv_notification_error(n);
}

static void
_alarm_calendar_playback_started_cb(NsvPlayback *self, nsv_notification *n)
{
  sp_timestamp("hildon-plugins-notify-sv: playing: Calendar");

  if (n->vibra_pattern && n->vibra_enabled)
    nsv_vibra_start(n->vibra_pattern);
}

static void
_alarm_calendar_playback_succeeded_cb(NsvPlayback *self, nsv_notification *n)
{
  nsv_notification_finish(n);
}

static gboolean
_alarm_calendar_tone_finish_cb(gpointer userdata)
{
  nsv_notification *n = (nsv_notification *)userdata;
  struct alarm_calendar_private *priv =
      (struct alarm_calendar_private *)n->private;

  priv->tone_timeout_id = 0;
  nsv_tone_stop(256);
  _alarm_calendar_playback_succeeded_cb(priv->playback, n);

  return FALSE;
}

static gboolean
calendar_play(nsv_notification *n)
{
  struct alarm_calendar_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct alarm_calendar_private *)n->private;

  if (n->play_granted)
  {
    if (!n->sound_enabled)
      n->volume = 0;

    priv->playback = nsv_playback_new();
    g_object_set(G_OBJECT(priv->playback),
                 "filename", n->sound_file,
                 "volume", n->volume,
                 "repeat", FALSE,
                 "min-timeout", 3000,
                 "max-timeout", 10000,
                 "event-id", "alarm-clock-elapsed",
                 NULL);
    g_signal_connect(G_OBJECT(priv->playback), "error",
                     G_CALLBACK(_alarm_calendar_playback_error_cb), n);
    g_signal_connect(G_OBJECT(priv->playback), "started",
                     G_CALLBACK(_alarm_calendar_playback_started_cb), n);
    g_signal_connect(G_OBJECT(priv->playback), "succeeded",
                     G_CALLBACK(_alarm_calendar_playback_succeeded_cb), n);
    nsv_playback_play(priv->playback);
  }
  else if (n->sound_enabled)
  {
    nsv_tone_start(256); /* TONE_RADIO_ACK, see rfc4733.c#L140*/
    priv->tone_timeout_id =
        g_timeout_add(3000u, _alarm_calendar_tone_finish_cb, n);
  }

  return TRUE;
}

static gboolean
calendar_stop(nsv_notification *n)
{
  struct alarm_calendar_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct alarm_calendar_private *)n->private;

  if ( n->play_granted )
  {
    if (n->vibra_pattern && n->vibra_enabled)
      nsv_vibra_stop(n->vibra_pattern);

    if (priv->playback)
    {
      nsv_playback_stop(priv->playback);
      g_object_unref(priv->playback);
      priv->playback = NULL;
    }
  }
  else
  {
    nsv_tone_stop(256);

    if (priv->tone_timeout_id)
    {
      g_source_remove(priv->tone_timeout_id);
      priv->tone_timeout_id = 0;
    }
  }

  return TRUE;
}

static struct notification_impl calendar =
{
  calendar_initialize,
  calendar_shutdown,
  calendar_play,
  calendar_stop,
  "Alarm calendar",
  "Alarm",
  10,
  1
};

void
register_alarm_calendar()
{
  nsv_notification_register("Calendar", &calendar);
}
