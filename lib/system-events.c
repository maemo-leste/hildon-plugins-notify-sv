#include <glib-object.h>

#include <stdio.h>

#include "nsv-private.h"
#include "nsv-notification.h"
#include "nsv-playback.h"
#include "nsv-util.h"

#include "system-events.h"

struct system_event_private
{
  NsvPlayback *playback;
  guint finish_timeout_id;
};

static gboolean
system_initialize(nsv_notification *n)
{
  g_assert(n != NULL);
  g_assert(n->private == NULL);

  if ((n->private = g_new0(struct system_event_private, 1)))
    return TRUE;

  return FALSE;
}

static gboolean
system_shutdown(nsv_notification *n)
{
  g_assert(n != NULL);
  g_assert(n->private != NULL);

  g_free(n->private);
  n->private = NULL;

  return TRUE;
}

static void
_system_playback_error_cb(NsvPlayback *self, nsv_notification *n)
{
  nsv_notification_error(n);
}

static void
_system_playback_succeeded_cb(NsvPlayback *self, nsv_notification *n)
{
  nsv_notification_finish(n);
}

static gboolean
system_play(nsv_notification *n)
{
  struct system_event_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct system_event_private *)n->private;

  if (n->play_granted && n->sound_enabled)
  {
    priv->playback = nsv_playback_new();
    g_object_set(G_OBJECT(priv->playback),
                 "filename", n->sound_file,
                 "volume", n->volume,
                 "repeat", FALSE,
                 "min-timeout", 1000,
                 "event-id", "dialog-information",
                 NULL);

    g_signal_connect(G_OBJECT(priv->playback), "error",
                     G_CALLBACK(_system_playback_error_cb), n);
    g_signal_connect(G_OBJECT(priv->playback), "succeeded",
                     G_CALLBACK(_system_playback_succeeded_cb), n);
    nsv_playback_play(priv->playback);
    return TRUE;
  }

  return FALSE;
}

static gboolean
system_stop(nsv_notification *n)
{
  struct system_event_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct system_event_private *)n->private;

  if (priv->playback)
  {
    nsv_playback_stop(priv->playback);
    g_object_unref(priv->playback);
    priv->playback = NULL;
  }

  return TRUE;
}

static struct notification_impl system_events =
{
  &system_initialize,
  &system_shutdown,
  &system_play,
  &system_stop,
  NSV_CATEGORY_SYSTEM,
  NSV_CATEGORY_SYSTEM,
  1,
  5
};

static gboolean
_critical_event_finish_cb(gpointer userdata)
{
  nsv_notification *n = (nsv_notification *)userdata;
  struct system_event_private *priv = (struct system_event_private *)n->private;

  priv->finish_timeout_id = 0;

  nsv_notification_finish(n);

  return FALSE;
}

static gboolean
system_play_critical(nsv_notification *n)
{
  struct system_event_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct system_event_private *)n->private;

  if (!n->sound_enabled)
    return FALSE;

  if (n->play_granted)
  {
    priv->playback = nsv_playback_new();
    g_object_set(G_OBJECT(priv->playback),
                 "filename", n->sound_file,
                 "volume", n->volume,
                 "repeat", FALSE,
                 "min-timeout", 1000,
                 "event-id", "dialog-information",
                 NULL);

    g_signal_connect(G_OBJECT(priv->playback), "error",
                     G_CALLBACK(_system_playback_error_cb), n);
    g_signal_connect(G_OBJECT(priv->playback), "succeeded",
                     G_CALLBACK(_system_playback_succeeded_cb), n);
    nsv_playback_play(priv->playback);
  }
  else
  {
    nsv_tone_start(256);
    priv->finish_timeout_id =
        g_timeout_add(3000, _critical_event_finish_cb, n);
  }

  return TRUE;
}

static gboolean
system_stop_critical(nsv_notification *n)
{
  struct system_event_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct system_event_private *)n->private;

  if (n->play_granted)
  {
    if (priv->playback)
    {
      nsv_playback_stop(priv->playback);
      g_object_unref(priv->playback);
      priv->playback = NULL;
    }
  }
  else
    nsv_tone_stop(256u);

  return TRUE;
}

static struct notification_impl critical_events =
{
  &system_initialize,
  &system_shutdown,
  &system_play_critical,
  &system_stop_critical,
  NSV_CATEGORY_CRITICAL,
  NSV_CATEGORY_SYSTEM,
  1,
  5
};

void
register_system_events()
{
  nsv_notification_register(NSV_CATEGORY_SYSTEM, &system_events);
  nsv_notification_register(NSV_CATEGORY_CRITICAL, &critical_events);
}
