#include <glib-object.h>

#include <stdio.h>

#include "sp_timestamp.h"

#include "nsv-private.h"
#include "nsv-notification.h"
#include "nsv-playback.h"
#include "nsv-util.h"

#include "message-events.h"

struct message_event_private
{
  NsvPlayback *playback;
  guint finish_timeout_id;
};

static gboolean
event_initialize(nsv_notification *n)
{
  g_assert(n != NULL);
  g_assert(n->private == NULL);

  if ((n->private = g_new0(struct message_event_private, 1)))
    return TRUE;

  return FALSE;
}

static gboolean
event_shutdown(nsv_notification *n)
{
  g_assert(n != NULL);
  g_assert(n->private != NULL);

  g_free(n->private);
  n->private = NULL;

  return TRUE;
}

static gboolean
_event_finish_cb(gpointer userdata)
{
  nsv_notification *n = (nsv_notification *)userdata;
  struct message_event_private *priv =
      (struct message_event_private *)n->private;

  priv->finish_timeout_id = 0;
  nsv_notification_finish(n);

  return FALSE;
}

static gboolean
_event_tone_finish_cb(gpointer userdata)
{
  nsv_tone_stop(256);
  _event_finish_cb(userdata);

  return FALSE;
}

static void
_event_playback_error_cb(NsvPlayback *self, nsv_notification *n)
{
  nsv_notification_error(n);
}

static void
_event_playback_started_cb(NsvPlayback *self, nsv_notification *n)
{
  sp_timestamp("hildon-plugins-notify-sv: playing: Event");

  if (n->vibra_pattern && n->vibra_enabled)
    nsv_vibra_start(n->vibra_pattern);
}

static void
_event_playback_succeeded_cb(NsvPlayback *self, nsv_notification *n)
{
  nsv_notification_finish(n);
}

static gboolean
event_play(nsv_notification *n)
{
  struct message_event_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct message_event_private *)n->private;

  if (!n->sound_enabled)
  {
    if (n->vibra_pattern &&n->vibra_enabled )
      nsv_vibra_start(n->vibra_pattern);

    priv->finish_timeout_id = g_timeout_add(3000, _event_finish_cb, n);

    return TRUE;
  }

  if (!n->play_granted)
  {
    if (n->vibra_pattern && n->vibra_enabled)
      nsv_vibra_start(n->vibra_pattern);

    nsv_tone_start(256);
    priv->finish_timeout_id = g_timeout_add(3000, _event_tone_finish_cb, n);
    return TRUE;
  }

  priv->playback = nsv_playback_new();
  g_object_set(G_OBJECT(priv->playback),
               "filename", n->sound_file,
               "volume", n->volume,
               "repeat", FALSE,
               "min-timeout", 3000,
               "event-id", "message-new-email", NULL);

  g_signal_connect(G_OBJECT(priv->playback), "error",
                   G_CALLBACK(_event_playback_error_cb), n);
  g_signal_connect(G_OBJECT(priv->playback), "started",
                   G_CALLBACK(_event_playback_started_cb), n);
  g_signal_connect(G_OBJECT(priv->playback), "succeeded",
                   G_CALLBACK(_event_playback_succeeded_cb), n);
  nsv_playback_play(priv->playback);

  return TRUE;
}

static gboolean
event_stop(nsv_notification *n)
{
  struct message_event_private *priv;

  g_assert(n != NULL);
  g_assert(n->private != NULL);

  priv = (struct message_event_private *)n->private;

  if (priv->finish_timeout_id)
  {
    g_source_remove(priv->finish_timeout_id);
    priv->finish_timeout_id = 0;
  }

  if (n->play_granted)
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
    nsv_tone_stop(256);

  return TRUE;
}

static struct notification_impl message_events =
{
  event_initialize,
  event_shutdown,
  event_play,
  event_stop,
  "Message event",
  "Event",
  5,
  4
};

void
register_message_events()
{
  nsv_notification_register("SMS", &message_events);
  nsv_notification_register("Email", &message_events);
  nsv_notification_register("Chat", &message_events);
  nsv_notification_register("Sound", &message_events);
}
