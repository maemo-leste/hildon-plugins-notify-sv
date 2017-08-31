#include <glib-object.h>
#include <libplayback/playback.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <stdlib.h>

#include "nsv-policy.h"

#define NSV_TYPE_POLICY (nsv_policy_get_type ())
#define NSV_POLICY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
            NSV_TYPE_POLICY, NsvPolicy))

typedef struct _NsvPolicyClass NsvPolicyClass;
typedef struct _NsvPolicyPrivate NsvPolicyPrivate;

enum
{
  PROP_0,
  PROP_POLICY_CLASS
};

struct _NsvPolicy
{
  GObject parent_instance;
  NsvPolicyPrivate *priv;
};

struct _NsvPolicyClass
{
  GObjectClass parent_class;
};

struct _NsvPolicyPrivate
{
  gchar *policy;
  enum pb_class_e pb_class;
  pb_req_t *pb_req;
};

G_DEFINE_TYPE(NsvPolicy, nsv_policy, G_TYPE_OBJECT);

struct pb_class
{
  enum pb_class_e pb_class;
  uint32_t pb_flags;
  enum pb_state_e pb_state;
  int state;
};

static GObjectClass *parent_class = NULL;

static guint play_reply_id;
static guint stop_reply_id;
static guint command_id;

static NsvPolicy *class_policy[PB_CLASS_LAST] = {0, };
static pb_playback_t *pb_playback[PB_CLASS_LAST] = {0, };
static int pb_states[PB_CLASS_LAST] = {0, };
static enum pb_state_e playback_allowed[PB_CLASS_LAST] = {PB_STATE_NONE, };
static struct pb_class pb_classes[] =
{
  {PB_CLASS_RINGTONE, PB_FLAG_AUDIO, PB_STATE_STOP, PB_STATE_STOP},
  {PB_CLASS_ALARM, PB_FLAG_AUDIO, PB_STATE_STOP, PB_STATE_STOP},
  {PB_CLASS_EVENT, PB_FLAG_AUDIO, PB_STATE_STOP, PB_STATE_STOP},
  {PB_CLASS_SYSTEM, PB_FLAG_AUDIO, PB_STATE_STOP, PB_STATE_NONE}
};

static DBusConnection *dbus;

static void
nsv_policy_set_property(GObject *object, guint prop_id, const GValue *value,
                        GParamSpec *pspec)
{
  switch (prop_id)
  {
    case PROP_POLICY_CLASS:
    {
      NsvPolicyPrivate *priv = NSV_POLICY(object)->priv;
      gchar *s;

      if (priv->policy)
        g_free(priv->policy);

      s = g_value_dup_string(value);
      priv->policy = s;
      priv->pb_class = pb_string_to_class(s);
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
nsv_policy_get_property(GObject *object, guint prop_id, GValue *value,
                        GParamSpec *pspec)
{
  switch (prop_id)
  {
    case PROP_POLICY_CLASS:
    {
      NsvPolicyPrivate *priv = NSV_POLICY(object)->priv;

      g_value_set_string(value, priv->policy);
      break;
    }
    default:
    {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }
}

static void nsv_policy_finalize(GObject *object)
{
  NsvPolicyPrivate *priv = NSV_POLICY(object)->priv;

  if (class_policy[priv->pb_class])
    class_policy[priv->pb_class] = NULL;

  if (priv->policy)
    g_free(priv->policy);

  g_free(priv);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}


static void
nsv_policy_class_init(NsvPolicyClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  parent_class = g_type_class_peek_parent(klass);

  object_class->finalize = nsv_policy_finalize;
  object_class->set_property = nsv_policy_set_property;
  object_class->get_property = nsv_policy_get_property;

  g_object_class_install_property(
        object_class, PROP_POLICY_CLASS,
        g_param_spec_string("policy-class",
                            NULL, NULL, NULL,
                            G_PARAM_READWRITE));

  play_reply_id =
      g_signal_new(
        "play-reply",
        G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
        0, NULL, NULL,
        g_cclosure_marshal_VOID__INT,
        G_TYPE_NONE,
        1, G_TYPE_INT);

  stop_reply_id =
      g_signal_new(
        "stop-reply",
        G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
        0, NULL, NULL,
        g_cclosure_marshal_VOID__INT,
        G_TYPE_NONE,
        1, G_TYPE_INT);

  command_id =
      g_signal_new(
        "command",
        G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
        0, NULL, NULL,
        g_cclosure_marshal_VOID__INT,
        G_TYPE_NONE,
        1, G_TYPE_INT);
}

static void
nsv_policy_init(NsvPolicy *self)
{
  self->priv = g_new0(NsvPolicyPrivate, 1);
}

NsvPolicy *
nsv_policy_new(const char *policy_class)
{
  return NSV_POLICY(g_object_new(NSV_TYPE_POLICY, "policy-class",
                                 policy_class, NULL));
}

static void
_nsv_policy_pb_stop_state_reply_cb(pb_playback_t *pb,
                                   enum pb_state_e granted_state,
                                   const char *reason, pb_req_t *req,
                                   void *data)
{
  NsvPolicy *self = NSV_POLICY(data);
  NsvPolicyPrivate *priv = self->priv;

  pb_playback_req_completed(pb, req);
  priv->pb_req = NULL;
  class_policy[priv->pb_class] = NULL;
  g_signal_emit(self, stop_reply_id, 0, granted_state);
}

gboolean
nsv_policy_stop_permission(NsvPolicy *self)
{
  NsvPolicyPrivate *priv= self->priv;
  enum pb_class_e policy_class= priv->pb_class;
  pb_playback_t *pb = pb_playback[policy_class];
  NsvPolicy *policy= class_policy[policy_class];

  if (!pb)
    return FALSE;

  if (policy && policy != self)
    return FALSE;

    if (pb_states[policy_class])
    {
      class_policy[policy_class] = self;
      priv->pb_req = pb_playback_req_state(pb,
                                           PB_STATE_STOP,
                                           _nsv_policy_pb_stop_state_reply_cb,
                                           self);
    }
    else
      g_signal_emit(self, stop_reply_id, 0, PB_STATE_STOP);

    return TRUE;
}

static void
_nsv_policy_pb_play_state_reply_cb(pb_playback_t *pb,
                                   enum pb_state_e granted_state,
                                   const char *reason, pb_req_t *req,
                                   void *data)
{
  NsvPolicy *self = NSV_POLICY(data);
  NsvPolicyPrivate *priv = self->priv;

  pb_playback_req_completed(pb, req);
  priv->pb_req = NULL;
  g_signal_emit(self, play_reply_id, 0, granted_state);
}

gboolean
nsv_policy_play_permission(NsvPolicy *self)
{
  NsvPolicyPrivate *priv= self->priv;
  enum pb_class_e policy_class = priv->pb_class;
  pb_playback_t *pb= pb_playback[policy_class];

  if (!pb || class_policy[policy_class])
    return FALSE;

  if (pb_states[policy_class])
  {
    class_policy[policy_class] = self;
    priv->pb_req = pb_playback_req_state(pb,
                                         PB_STATE_PLAY,
                                         _nsv_policy_pb_play_state_reply_cb,
                                         self);
  }
  else
  {
    enum pb_state_e state = playback_allowed[policy_class];

    if (state == PB_STATE_STOP || state == PB_STATE_PLAY)
      state = PB_STATE_PLAY;
    else
      state = PB_STATE_NONE;

    g_signal_emit(self, play_reply_id, 0, state);
  }

  return TRUE;
}

static void
_nsv_policy_mgr_pb_state_request_cb(pb_playback_t *pb,
                                    enum pb_state_e req_state,
                                    pb_req_t *ext_req,
                                    void *data)
{
  pb_playback_req_completed(pb, ext_req);

  if (class_policy[(uintptr_t)data])
    g_signal_emit(class_policy[(uintptr_t)data], command_id, 0, req_state);
}

static void
_nsv_policy_mgr_pb_state_hint_cb(pb_playback_t *pb, const int allowed_state[],
                                 void *data)
{
  playback_allowed[(uintptr_t)data] = allowed_state[PB_STATE_PLAY] != PB_STATE_NONE;
}

gboolean
nsv_policy_mgr_init()
{
  int i;

  dbus = dbus_bus_get(DBUS_BUS_SESSION, NULL);

  if (!dbus)
    return FALSE;

  dbus_connection_setup_with_g_main((DBusConnection *)dbus, 0);

  for (i = 0; i < G_N_ELEMENTS(pb_classes); i++)
  {
    struct pb_class *pb_class = &pb_classes[i];

    pb_playback[pb_class->pb_class] =
        pb_playback_new_2(dbus,
                          pb_class->pb_class,
                          pb_class->pb_flags,
                          pb_class->pb_state,
                          _nsv_policy_mgr_pb_state_request_cb,
                          (void *)pb_class->pb_class);

    pb_states[pb_class->pb_class] = pb_class->state;

    if (pb_class->state == PB_STATE_NONE)
    {
      pb_playback_set_state_hint(pb_playback[pb_class->pb_class],
                                 _nsv_policy_mgr_pb_state_hint_cb,
                                 (void *)pb_class->pb_class);
    }
  }

  return TRUE;
}

gboolean
nsv_policy_mgr_shutdown()
{
  int i;

  for (i = 0; i < G_N_ELEMENTS(pb_playback); i++)
  {
    pb_playback_t *pb = pb_playback[i];

    if (pb);
    {
      pb_playback_destroy(pb);
      free(pb);
    }
  }

  if (dbus)
  {
    dbus_connection_unref(dbus);
    dbus = NULL;
  }

  return TRUE;
}
