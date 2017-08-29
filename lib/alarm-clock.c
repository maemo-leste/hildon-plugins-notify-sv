#include <glib-object.h>

#include <stdio.h>

#include "sp_timestamp.h"

#include "nsv-private.h"
#include "nsv-notification.h"
#include "nsv-playback.h"
#include "nsv-util.h"

#include "alarm-clock.h"

struct alarm_clock_private
{
  NsvPlayback *playback;
  int volume_step;
  guint volume_step_timeout_id;
  guint tone_timeout_id;
};

static gboolean
clock_initialize(nsv_notification *n)
{
  g_assert(n != NULL);
  g_assert(n->private == NULL);

  if ((n->private = g_new0(struct alarm_clock_private, 1)))
    return TRUE;

  return FALSE;
}

static gboolean
clock_shutdown(nsv_notification *n)
{
  struct alarm_clock_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct alarm_clock_private *)n->private;

  if (priv->playback)
  {
    g_object_unref(priv->playback);
    priv->playback = NULL;
  }

  if (priv->volume_step_timeout_id > 0)
    g_source_remove(priv->volume_step_timeout_id);

  g_free(priv);
  n->private = NULL;

  return TRUE;
}

static gboolean
clock_stop(nsv_notification *n)
{
  struct alarm_clock_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct alarm_clock_private *)n->private;

  if (priv->volume_step_timeout_id)
  {
    g_source_remove(priv->volume_step_timeout_id);
    priv->volume_step_timeout_id = 0;
  }

  if (n->play_granted)
  {
    if (n->vibra_pattern && n->vibra_enabled)
    {
      g_debug("stop vibra");
      nsv_vibra_stop(n->vibra_pattern);
    }

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

static void
_alarm_clock_playback_error_cb(NsvPlayback *self, nsv_notification *n)
{
  nsv_notification_error(n);
}

static gboolean
_alarm_clock_playback_finish_cb(gpointer userdata)
{
  nsv_notification *n = (nsv_notification *)userdata;
  struct alarm_clock_private *priv = (struct alarm_clock_private *)n->private;

  priv->volume_step_timeout_id = 0;
  nsv_notification_finish(n);

  return FALSE;
}

static gboolean
_alarm_clock_volume_step_cb(gpointer userdata)
{
  struct alarm_clock_private *priv =
      (struct alarm_clock_private *)((nsv_notification *)userdata)->private;
  int volume;
  guint timeout;

  switch (priv->volume_step)
  {
    case 0:
    {
      volume = 50;
      timeout = 6000;
      break;
    }
    case 1:
    {
      volume = 75;
      timeout = 6000;
      break;
    }
    case 2:
    {
      volume = 100;
      timeout = 46000;
      break;
    }
    case 3:
    {
      priv->volume_step_timeout_id = 0;

      if (priv->playback)
      {
        nsv_playback_stop(priv->playback);
        g_object_unref(priv->playback);
        priv->playback = NULL;
      }

      nsv_notification_finish((nsv_notification *)userdata);
    } /* fallthrough */
    default:
      return FALSE;
  }

  g_object_set(G_OBJECT(priv->playback), "volume", volume, NULL);
  priv->volume_step++;
  priv->volume_step_timeout_id =
      g_timeout_add(timeout, _alarm_clock_volume_step_cb, userdata);

  return FALSE;
}

static void
_alarm_clock_playback_started_cb(NsvPlayback *self, nsv_notification *n)
{
  struct alarm_clock_private *priv = (struct alarm_clock_private *)n->private;

  _sp_timestamp("playing: Clock");

  if (n->vibra_pattern && n->vibra_enabled)
    nsv_vibra_start(n->vibra_pattern);

  priv->volume_step_timeout_id =
      g_timeout_add(2000, _alarm_clock_volume_step_cb, n);
}

static gboolean
clock_play(nsv_notification *n)
{
  struct alarm_clock_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct alarm_clock_private *)n->private;
  priv->volume_step = 0;

  if (n->play_granted)
  {
    priv->playback = nsv_playback_new();

    g_object_set(G_OBJECT(priv->playback),
                 "filename", n->sound_file,
                 "volume", 50,
                 "repeat", TRUE,
                 "min-timeout", 3000,
                 "event-id", "alarm-clock-elapsed",
                 NULL);

    g_signal_connect(G_OBJECT(priv->playback), "error",
                     (GCallback)_alarm_clock_playback_error_cb, n);
    g_signal_connect(G_OBJECT(priv->playback), "started",
                     (GCallback)_alarm_clock_playback_started_cb, n);
    nsv_playback_play(priv->playback);
  }
  else
  {
    nsv_tone_start(256); /* TONE_RADIO_ACK, see rfc4733.c#L140*/

    priv->tone_timeout_id =
        g_timeout_add(3000, _alarm_clock_playback_finish_cb, n);
  }

  return TRUE;
}

static struct notification_impl alarm_clock =
{
  clock_initialize,
  clock_shutdown,
  clock_play,
  clock_stop,
  "Alarm clock",
  "Alarm",
  10,
  1
}; // weak

void register_alarm_clock()
{
  nsv_notification_register("Clock", &alarm_clock);
}
