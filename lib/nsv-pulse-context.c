#include <glib-object.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <pulse/ext-stream-restore.h>

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
  pa_ext_stream_restore2_info stream_restore;
  gboolean stream_restore_write_pending;
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
nsv_pulse_context_pa_ext_stream_restore2_write(
    NsvPulseContext *self, const pa_ext_stream_restore2_info *info)
{
  NsvPulseContextPrivate *priv = self->priv;
  pa_operation *pa_op =
    pa_ext_stream_restore2_write(priv->pa_context, PA_UPDATE_REPLACE,
                                 &info, 1, TRUE, NULL, NULL);

  if (pa_op)
    pa_operation_unref(pa_op);
}

static void
_nsv_pulse_context_pa_context_state_cb(pa_context *c, void *userdata)
{
  NsvPulseContext *self = NSV_PULSE_CONTEXT(userdata);
  NsvPulseContextPrivate *priv = self->priv;
  pa_context_state_t state;

  state = pa_context_get_state(c);

  switch (pa_context_get_state(c))
  {
    case PA_CONTEXT_READY:
    {
      if (priv->stream_restore_write_pending)
      {
        priv->stream_restore_write_pending = FALSE;
        nsv_pulse_context_pa_ext_stream_restore2_write(self,
                                                       &priv->stream_restore);
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

void
nsv_pulse_context_set_rule_volume(NsvPulseContext *self, const char *rule,
                                  int volume)
{
  NsvPulseContextPrivate *priv = self->priv;
  pa_cvolume cvol;
  pa_volume_t v;

  if (volume < 0)
    volume = 0;

  v = ((double)volume / 100.0 * 65536.0);

  pa_cvolume_set(&cvol, 1, v);
  priv->stream_restore.name = rule;
  priv->stream_restore.channel_map.channels = 1;
  priv->stream_restore.channel_map.map[0] = 0;
  priv->stream_restore.volume = cvol;
  priv->stream_restore.device = NULL;
  priv->stream_restore.mute = FALSE;
  priv->stream_restore.volume_is_absolute = TRUE;

  if (priv->pa_context &&
      pa_context_get_state(priv->pa_context) == PA_CONTEXT_READY)
  {
    nsv_pulse_context_pa_ext_stream_restore2_write(self, &priv->stream_restore);
  }
  else
    priv->stream_restore_write_pending = TRUE;
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
