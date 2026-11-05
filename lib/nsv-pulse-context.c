#include <glib-object.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include <string.h>

#include "nsv-pulse-context.h"

#define NSV_TYPE_PULSE_CONTEXT (nsv_pulse_context_get_type ())
#define NSV_PULSE_CONTEXT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                                         NSV_TYPE_PULSE_CONTEXT, NsvPulseContext))

typedef struct _NsvPulseContextClass NsvPulseContextClass;
typedef struct _NsvPulseContextPrivate NsvPulseContextPrivate;

enum
{
  PROP_0,
  PROP_CONTEXT
};

struct _NsvPulseContextClass {
  GObjectClass parent_class;
};

struct _NsvPulseContext
{
  GObject parent_instance;
  NsvPulseContextPrivate *priv;
};

struct _NsvPulseContextPrivate
{
  pa_glib_mainloop *pa_mainloop;
  pa_context *pa_context;
  gchar *media_role;
  gint media_role_volume;
  gboolean media_role_pending;
  pa_stream *media_role_stream;
};

G_DEFINE_TYPE(NsvPulseContext, nsv_pulse_context, G_TYPE_OBJECT);

static GObjectClass *parent_class = NULL;

static guint connecting_id;
static guint ready_id;
static guint terminated_id;
static guint failed_id;
static guint error_id;

static gboolean nsv_pulse_context_reinitialize(gpointer user_data);
static void nsv_pulse_context_terminate(NsvPulseContext *self);

static void
nsv_pulse_context_finalize(GObject *object)
{
  NsvPulseContext *self = NSV_PULSE_CONTEXT(object);

  nsv_pulse_context_terminate(self);
  g_free(self->priv);
  self->priv = NULL;

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
nsv_pulse_context_get_property(GObject *object, guint prop_id, GValue *value,
                               GParamSpec *pspec)
{
  switch (prop_id)
  {
    case PROP_CONTEXT:
    {
      g_value_set_pointer(value, NSV_PULSE_CONTEXT(object)->priv->pa_context);
      break;
    }
  default:
    {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}

static void
nsv_pulse_context_class_init(NsvPulseContextClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  parent_class = g_type_class_peek_parent(klass);

  object_class->finalize = nsv_pulse_context_finalize;
  object_class->get_property = nsv_pulse_context_get_property;


  g_object_class_install_property(
        object_class, PROP_CONTEXT,
        g_param_spec_pointer("context",
                             NULL, NULL,
                             G_PARAM_READABLE));

  connecting_id = g_signal_new("connecting",
                               G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                               0, NULL, NULL,
                               &g_cclosure_marshal_VOID__VOID,
                               G_TYPE_NONE, 0, G_TYPE_NONE);

  ready_id = g_signal_new("ready",
                          G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                          0, NULL, NULL,
                          &g_cclosure_marshal_VOID__VOID,
                          G_TYPE_NONE, 0, G_TYPE_NONE);

  terminated_id = g_signal_new("terminated",
                               G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                               0, NULL, NULL,
                               &g_cclosure_marshal_VOID__VOID,
                               G_TYPE_NONE, 0, G_TYPE_NONE);

  failed_id = g_signal_new("failed",
                           G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                           0, NULL, NULL,
                           &g_cclosure_marshal_VOID__VOID,
                           G_TYPE_NONE, 0, G_TYPE_NONE);

  error_id = g_signal_new("error",
                          G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                          0, NULL, NULL,
                          &g_cclosure_marshal_VOID__VOID,
                          G_TYPE_NONE, 0, G_TYPE_NONE);
}

static void
nsv_pulse_context_media_role_stream_dispose(NsvPulseContext *self)
{
  NsvPulseContextPrivate *priv = self->priv;

  if (priv->media_role_stream)
  {
    pa_stream_disconnect(priv->media_role_stream);
    pa_stream_unref(priv->media_role_stream);
    priv->media_role_stream = NULL;
  }
}

static gboolean
nsv_pulse_context_media_role_stream_dispose_later(gpointer user_data)
{
  nsv_pulse_context_media_role_stream_dispose(NSV_PULSE_CONTEXT(user_data));

  return G_SOURCE_REMOVE;
}

/* The level a class of sounds plays at has to live somewhere the sounds that
 * carry no level of their own can find it, and on this stack that place is the
 * volume WirePlumber keeps for a role. It is put there by a stream that has the
 * role and the volume and nothing else, connected corked so it never becomes
 * audible, and dropped again once the server has taken the number. */
static void
nsv_pulse_context_publish_media_role_volume(NsvPulseContext *self)
{
  NsvPulseContextPrivate *priv = self->priv;
  pa_proplist *proplist;
  pa_sample_spec spec;
  pa_cvolume cvol;

  if (priv->media_role == NULL ||
      priv->pa_context == NULL ||
      pa_context_get_state(priv->pa_context) != PA_CONTEXT_READY)
    return;

  nsv_pulse_context_media_role_stream_dispose(self);

  pa_cvolume_set(&cvol, 1, ((double)priv->media_role_volume / 100.0 *
                            PA_VOLUME_NORM));

  spec.format = PA_SAMPLE_S16LE;
  spec.rate = 44100;
  spec.channels = 2;

  proplist = pa_proplist_new();
  pa_proplist_sets(proplist, PA_PROP_MEDIA_ROLE, priv->media_role);
  pa_proplist_sets(proplist, PA_PROP_MEDIA_NAME, priv->media_role);

  priv->media_role_stream = pa_stream_new_with_proplist(priv->pa_context,
                                                 priv->media_role,
                                                 &spec, NULL, proplist);
  pa_proplist_free(proplist);

  if (priv->media_role_stream == NULL)
    return;

  if (pa_stream_connect_playback(priv->media_role_stream, NULL, NULL,
                                 PA_STREAM_START_CORKED, &cvol, NULL) < 0)
  {
    nsv_pulse_context_media_role_stream_dispose(self);
    return;
  }

  g_timeout_add(500, nsv_pulse_context_media_role_stream_dispose_later, self);
}

static void
_nsv_pulse_context_pa_context_state_cb(pa_context *c, void *userdata)
{
  NsvPulseContext *self = NSV_PULSE_CONTEXT(userdata);
  NsvPulseContextPrivate *priv = self->priv;

  switch (pa_context_get_state(c))
  {
    case PA_CONTEXT_READY:
    {
      if (priv->media_role_pending)
      {
        priv->media_role_pending = FALSE;
        nsv_pulse_context_publish_media_role_volume(self);
      }

      g_signal_emit(self, ready_id, 0);
      break;
    }
    case PA_CONTEXT_FAILED:
    {
      g_signal_emit(self, failed_id, 0);
      g_timeout_add_seconds(1, nsv_pulse_context_reinitialize, self);
      break;
    }
    case PA_CONTEXT_TERMINATED:
    {
      g_signal_emit(self, terminated_id, 0);
      break;
    }
    case PA_CONTEXT_CONNECTING:
    {
      g_signal_emit(self, connecting_id, 0);
      break;
    }
    default: /*make the compiler happy */
      break;
  }
}

static void
nsv_pulse_context_real_init(NsvPulseContext *self)
{
  NsvPulseContextPrivate *priv = self->priv;
  pa_proplist *proplist;
  pa_mainloop_api *api;

  priv->pa_mainloop = pa_glib_mainloop_new(g_main_context_default());

  if (!priv->pa_mainloop)
    goto emit_error;

  api = pa_glib_mainloop_get_api(priv->pa_mainloop);

  if (!api)
    goto emit_error;

  proplist = pa_proplist_new();
  pa_proplist_sets(proplist, "application.name", "notification-daemon");
  pa_proplist_sets(proplist, "application.id", "notification-daemon");
  pa_proplist_sets(proplist, "application.version", "0.50");

  priv->pa_context = pa_context_new_with_proplist(api, "notification-daemon",
                                                  proplist);

  pa_proplist_free(proplist);

  if (!priv->pa_context)
    goto emit_error;

  pa_context_set_state_callback(
        priv->pa_context, _nsv_pulse_context_pa_context_state_cb, self);

  if (pa_context_connect(priv->pa_context, NULL,
                         PA_CONTEXT_NOFAIL|PA_CONTEXT_NOAUTOSPAWN, 0) < 0)
  {
    goto emit_error;
  }

  return;

emit_error:
    g_signal_emit(self, error_id, 0);
}

static void
nsv_pulse_context_terminate(NsvPulseContext *self)
{
  NsvPulseContextPrivate *priv = self->priv;

  if (priv->pa_context)
  {
    pa_context_set_state_callback(priv->pa_context, NULL, NULL);
    pa_context_disconnect(priv->pa_context);
    pa_context_unref(priv->pa_context);
    priv->pa_context = NULL;
  }

  if (priv->pa_mainloop)
  {
    pa_glib_mainloop_free(priv->pa_mainloop);
    priv->pa_mainloop = NULL;
  }
}

static gboolean
nsv_pulse_context_reinitialize(gpointer user_data)
{
  NsvPulseContext *self = NSV_PULSE_CONTEXT(user_data);

  nsv_pulse_context_terminate(self);
  nsv_pulse_context_real_init(self);

  return FALSE;
}

static void
nsv_pulse_context_init(NsvPulseContext *self)
{
  self->priv = g_new0(NsvPulseContextPrivate, 1);
  nsv_pulse_context_real_init(self);
}

gboolean
nsv_pulse_context_is_ready(NsvPulseContext *self)
{
  if (self->priv->pa_context)
    return pa_context_get_state(self->priv->pa_context) == PA_CONTEXT_READY;

  return FALSE;
}

/* The level belongs to the class of sounds, not to the sound that happens to
 * be playing, so it is stored under the class name and reaches the sounds that
 * carry no level of their own. Like the write it replaces, it waits for the
 * server rather than being lost when the profile answers first. */
void
nsv_pulse_context_set_media_role_volume(NsvPulseContext *self,
                                        const char *media_role, int volume)
{
  NsvPulseContextPrivate *priv;

  if (self == NULL || media_role == NULL)
    return;

  priv = self->priv;

  if (volume < 0)
    volume = 0;
  else if (volume > 100)
    volume = 100;

  g_free(priv->media_role);
  priv->media_role = g_strdup(media_role);
  priv->media_role_volume = volume;

  if (priv->pa_context &&
      pa_context_get_state(priv->pa_context) == PA_CONTEXT_READY)
    nsv_pulse_context_publish_media_role_volume(self);
  else
    priv->media_role_pending = TRUE;
}

pa_context *
nsv_pulse_context_get_context(NsvPulseContext *self)
{
  return self->priv->pa_context;
}

NsvPulseContext *
nsv_pulse_context_get_instance()
{
  static NsvPulseContext *instance = NULL;

  if (!instance)
    instance = NSV_PULSE_CONTEXT(g_object_new(NSV_TYPE_PULSE_CONTEXT, NULL));

  return instance;
}
