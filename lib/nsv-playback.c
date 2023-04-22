#include <glib-object.h>
#include <libplayback/playback.h>
#include <pulse/glib-mainloop.h>
#include <pulse/pulseaudio.h>
#include <pulse/ext-stream-restore.h>
#include <sndfile.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "config.h"

#include "nsv-playback.h"

#define NSV_TYPE_PLAYBACK (nsv_playback_get_type ())
#define NSV_PLAYBACK(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
            NSV_TYPE_PLAYBACK, NsvPlayback))

typedef struct _NsvPlaybackClass NsvPlaybackClass;
typedef struct _NsvPlaybackPrivate NsvPlaybackPrivate;

enum
{
  PROP_0,
  PROP_PLAYBACK_FILENAME,
  PROP_PLAYBACK_VOLUME,
  PROP_PLAYBACK_REPEAT,
  PROP_PLAYBACK_MIN_TIMEOUT,
  PROP_PLAYBACK_MAX_TIMEOUT,
  PROP_PLAYBACK_EVENT_ID,
  PROP_PLAYBACK_MEDIA_ROLE
};

struct _NsvPlayback
{
  GObject parent_instance;
  NsvPlaybackPrivate *priv;
};

struct _NsvPlaybackClass
{
  GObjectClass parent_class;
};

struct _NsvPlaybackPrivate
{
  gchar *filename;
  gboolean volume_set;
  gint volume;
  gboolean repeat;
  gint min_timeout;
  gint max_timeout;
  gchar *event_id;
  gchar *media_role;
  GTimer *timer;
  guint check_repeat_id;
  guint max_timeout_id;
  guint repeat_id;
  int field_30;
  int handle;
  int last_errno;
  SNDFILE *sndfile;
  pa_glib_mainloop *pa_glib_mainloop;
  pa_context *pa_context;
  pa_stream *pa_stream;
  uint32_t stream_index;
  FILE *fp;
  gboolean decoded;
  gboolean started;
  gboolean stopped;
  gboolean play_pending;
  char buffer[65536];
};

G_DEFINE_TYPE(NsvPlayback, nsv_playback, G_TYPE_OBJECT);

static GObjectClass *parent_class = NULL;

static guint started_id;
static guint succeeded_id;
static guint stopped_id;
static guint error_id;

static void _nsv_playback_play_real(NsvPlayback *self);

static void
_nsv_playback_cleanup(NsvPlayback *self)
{
  NsvPlaybackPrivate *priv = self->priv;

  if (priv->repeat_id)
  {
    g_source_remove(priv->repeat_id);
    priv->repeat_id = 0;
  }

  if (priv->check_repeat_id)
  {
    g_source_remove(priv->check_repeat_id);
    priv->check_repeat_id = 0;
  }

  if (priv->max_timeout_id)
  {
    g_source_remove(priv->max_timeout_id);
    priv->max_timeout_id = 0;
  }

  if (priv->pa_stream)
  {
    pa_stream_set_state_callback(priv->pa_stream, NULL, NULL);
    pa_stream_set_write_callback(priv->pa_stream, NULL, NULL);
    pa_stream_disconnect(priv->pa_stream);
    pa_stream_unref(priv->pa_stream);
    priv->pa_stream = NULL;
  }

  if (priv->fp)
  {
    fclose(priv->fp);
    priv->fp = NULL;
  }

  if (priv->sndfile)
  {
    sf_close(priv->sndfile);
    priv->sndfile = NULL;
  }

  if (priv->handle != -1)
  {
    close(priv->handle);
    priv->handle = -1;
  }
}

static void
nsv_playback_finalize(GObject *object)
{
  NsvPlayback *self = NSV_PLAYBACK(object);
  NsvPlaybackPrivate *priv = self->priv;

  _nsv_playback_cleanup(self);

  if (priv->pa_context)
  {
    pa_context_set_state_callback(priv->pa_context, NULL, NULL);
    pa_context_disconnect(priv->pa_context);
    pa_context_unref(priv->pa_context);
    priv->pa_context = NULL;
  }

  if (priv->pa_glib_mainloop)
  {
    pa_glib_mainloop_free(priv->pa_glib_mainloop);
    priv->pa_glib_mainloop = NULL;
  }

  if (priv->filename)
  {
    g_free(priv->filename);
    priv->filename = NULL;
  }

  if (priv->event_id)
  {
    g_free(priv->event_id);
    priv->event_id = NULL;
  }

  if (priv->media_role)
  {
    g_free(priv->media_role);
    priv->media_role = NULL;
  }

  g_timer_destroy(priv->timer);
  priv->timer = NULL;
  g_free(self->priv);
  self->priv = NULL;

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static gboolean
_nsv_playback_pa_set_volume(NsvPlayback *self, gint volume)
{
  pa_context *pa_context;
  NsvPlaybackPrivate *priv = self->priv;
  pa_operation *op;
  pa_ext_stream_restore_info info;
  const pa_ext_stream_restore_info *pinfo = &info;
  pa_cvolume cvol;

  pa_context = self->priv->pa_context;

  if (pa_context && pa_context_get_state(pa_context) == PA_CONTEXT_READY)
  {
    pa_volume_t v;

    if (volume < 0)
      volume = 0;

    v = ((double)volume / 100.0 * 65536.0);
    pa_cvolume_set(&cvol, 1, v);

    info.name = "x-maemo-hildon-notify";
    info.channel_map.channels = 1;
    info.channel_map.map[0] = 0;
    info.volume = cvol;
    info.device = 0;
    info.mute = FALSE;

    op = pa_ext_stream_restore_write(priv->pa_context,PA_UPDATE_REPLACE,
                                     pinfo, 1, TRUE, NULL, NULL);
    if (op)
      pa_operation_unref(op);

    return TRUE;
  }

  return FALSE;
}

static void
nsv_playback_set_property(GObject *object, guint prop_id, const GValue *value,
                          GParamSpec *pspec)
{
  NsvPlayback *self = NSV_PLAYBACK(object);
  NsvPlaybackPrivate *priv = self->priv;

  switch (prop_id)
  {
    case PROP_PLAYBACK_FILENAME:
    {
      if (priv->filename)
        g_free(priv->filename);

      priv->filename = g_value_dup_string(value);
      break;
    }
    case PROP_PLAYBACK_VOLUME:
    {
      priv->volume_set = TRUE;
      priv->volume = g_value_get_int(value);
      _nsv_playback_pa_set_volume(self, priv->volume);
      break;
    }
    case PROP_PLAYBACK_REPEAT:
    {
      priv->repeat = g_value_get_boolean(value);
      break;
    }
    case PROP_PLAYBACK_MIN_TIMEOUT:
    {
      priv->min_timeout = g_value_get_int(value);
      break;
    }
    case PROP_PLAYBACK_MAX_TIMEOUT:
    {
      priv->max_timeout = g_value_get_int(value);
      break;
    }
    case PROP_PLAYBACK_EVENT_ID:
    {
      if (priv->event_id)
        g_free(priv->event_id);
      priv->event_id = g_value_dup_string(value);
      break;
    }
    case PROP_PLAYBACK_MEDIA_ROLE:
    {
      if (priv->media_role)
        g_free(priv->media_role);

      priv->media_role = g_value_dup_string(value);
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
nsv_playback_get_property(GObject *object, guint prop_id, GValue *value,
                          GParamSpec *pspec)
{
  NsvPlaybackPrivate *priv = NSV_PLAYBACK(object)->priv;

  switch (prop_id)
  {
    case PROP_PLAYBACK_FILENAME:
    {
      g_value_set_string(value, priv->filename);
      break;
    }
    case PROP_PLAYBACK_VOLUME:
    {
      g_value_set_int(value, priv->volume);
      break;
    }
    case PROP_PLAYBACK_REPEAT:
      g_value_set_boolean(value, priv->repeat);
      break;
    case PROP_PLAYBACK_MIN_TIMEOUT:
    {
      g_value_set_int(value, priv->min_timeout);
      break;
    }
    case PROP_PLAYBACK_MAX_TIMEOUT:
    {
      g_value_set_int(value, priv->max_timeout);
      break;
    }
    case PROP_PLAYBACK_EVENT_ID:
    {
      g_value_set_string(value, priv->event_id);
      break;
    }
    case PROP_PLAYBACK_MEDIA_ROLE:
    {
      g_value_set_string(value, priv->media_role);
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
nsv_playback_class_init(NsvPlaybackClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  parent_class = g_type_class_peek_parent(klass);

  object_class->finalize = nsv_playback_finalize;
  object_class->set_property = nsv_playback_set_property;
  object_class->get_property = nsv_playback_get_property;

  g_object_class_install_property(
        object_class, PROP_PLAYBACK_FILENAME,
        g_param_spec_string("filename",
                            NULL, NULL, NULL,
                            G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  g_object_class_install_property(
        object_class, PROP_PLAYBACK_VOLUME,
        g_param_spec_int("volume",
                         NULL, NULL, G_MININT, G_MAXINT, 0,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  g_object_class_install_property(
        object_class, PROP_PLAYBACK_REPEAT,
        g_param_spec_boolean("repeat",
                             NULL, NULL, FALSE,
                             G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  g_object_class_install_property(
        object_class, PROP_PLAYBACK_MIN_TIMEOUT,
        g_param_spec_int("min-timeout",
          NULL, NULL, G_MININT, G_MAXINT, -1,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  g_object_class_install_property(
        object_class, PROP_PLAYBACK_MAX_TIMEOUT,
        g_param_spec_int("max-timeout",
                         NULL, NULL, G_MININT, G_MAXINT, -1,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  g_object_class_install_property(
        object_class, PROP_PLAYBACK_EVENT_ID,
        g_param_spec_string("event-id",
                            NULL, NULL, NULL,
                            G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  g_object_class_install_property(
        object_class, PROP_PLAYBACK_MEDIA_ROLE,
        g_param_spec_string("media-role",
                            NULL, NULL, NULL,
                            G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  started_id =  g_signal_new("started",
                             G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                             0, NULL, NULL,
                             g_cclosure_marshal_VOID__VOID,
                             G_TYPE_NONE, 0, G_TYPE_NONE);

  succeeded_id = g_signal_new("succeeded",
                              G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                              0, NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0, G_TYPE_NONE);

  stopped_id = g_signal_new("stopped",
                            G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                            0, NULL, NULL,
                            g_cclosure_marshal_VOID__VOID,
                            G_TYPE_NONE, 0, G_TYPE_NONE);

  error_id = g_signal_new("error",
                          G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                          0, NULL, NULL,
                          g_cclosure_marshal_VOID__VOID,
                          G_TYPE_NONE, 0, G_TYPE_NONE);
}

static gboolean
_nsv_playback_emit_error_cb(gpointer user_data)
{
  g_signal_emit(NSV_PLAYBACK(user_data), error_id, 0);

  return FALSE;
}

static gboolean
_nsv_playback_repeat_cb(gpointer user_data)
{
  NsvPlayback *self = NSV_PLAYBACK(user_data);

  self->priv->repeat_id = 0;
  _nsv_playback_cleanup(self);
  _nsv_playback_play_real(self);

  return FALSE;
}

static gboolean
_nsv_playback_repeat(NsvPlayback *self)
{
  NsvPlaybackPrivate *priv = self->priv;

  if (priv->repeat)
  {
    priv->repeat_id = g_timeout_add(1000, _nsv_playback_repeat_cb, self);
    return TRUE;
  }

  return FALSE;
}

static gboolean
_nsv_playback_emit_succeeded_cb(gpointer user_data)
{
  NsvPlayback *self = NSV_PLAYBACK(user_data);

  if (!_nsv_playback_repeat(self))
  {
    _nsv_playback_cleanup(self);
    g_signal_emit(self, succeeded_id, 0);
  }

  return FALSE;
}

static void
_nsv_playback_stream_state_cb(pa_stream *p, void *userdata)
{
  NsvPlayback *self = NSV_PLAYBACK(userdata);
  NsvPlaybackPrivate *priv = self->priv;
  pa_stream_state_t stream_state = pa_stream_get_state(p);

  if (stream_state == PA_STREAM_FAILED || stream_state == PA_STREAM_TERMINATED)
  {
    _nsv_playback_cleanup(self);
    g_idle_add(_nsv_playback_emit_error_cb, self);
  }
  else if (stream_state == PA_STREAM_READY)
  {
    priv->stream_index = pa_stream_get_index(priv->pa_stream);
  }
}

static gboolean
_nsv_playback_check_repeat_cb(gpointer user_data)
{
  NsvPlayback *self = NSV_PLAYBACK(user_data);
  NsvPlaybackPrivate *priv = self->priv;

  if (!_nsv_playback_repeat(self))
    g_signal_emit(self, succeeded_id, 0);

  priv->check_repeat_id = 0;

  return FALSE;
}

static void
_nsv_playback_pa_stream_drain_cb(pa_stream *s, int success, void *userdata)
{
  NsvPlayback *self = NSV_PLAYBACK(userdata);
  NsvPlaybackPrivate *priv = self->priv;
  gint min_timeout;
  gint chk_rpt_tm;
  double elapsed;

  elapsed = g_timer_elapsed(priv->timer, 0);

  if (priv->max_timeout_id)
  {
    g_source_remove(priv->max_timeout_id);
    priv->max_timeout_id = 0;
  }

  if (!priv->stopped)
  {
    min_timeout = priv->min_timeout;

    if (min_timeout > 0 &&
        (chk_rpt_tm = min_timeout - (gint)(elapsed * 1000.0), chk_rpt_tm > 50))
    {
      priv->check_repeat_id =
          g_timeout_add(chk_rpt_tm, _nsv_playback_check_repeat_cb, self);
    }
    else if (!_nsv_playback_repeat(self))
      g_signal_emit(self, succeeded_id, 0);
  }
}

static void
_nsv_playback_stream_write_cb(pa_stream *p, size_t nbytes, void *userdata)
{
  NsvPlayback *self = NSV_PLAYBACK(userdata);
  NsvPlaybackPrivate *priv = self->priv;
  size_t bytes;
  pa_operation *op;
  void *buf;
  const size_t bufsize = sizeof(priv->buffer);

  if (!nbytes)
    return;

  buf = priv->buffer;

  do
  {
    if (priv->decoded)
    {
      bytes = fread(buf, 1, nbytes > bufsize ? bufsize : nbytes, priv->fp);

      if (!bytes)
        goto finished;
    }
    else
    {
      bytes =
          sf_read_raw(priv->sndfile, buf, nbytes > bufsize ? bufsize : nbytes);

      if (!bytes)
        goto finished;
    }

    if (pa_stream_write(p, buf, bytes, NULL, 0, PA_SEEK_RELATIVE) < 0)
      goto finished;

    nbytes -= bytes;
  }
  while (nbytes);

  return;

finished:
  pa_stream_set_write_callback(p, NULL, NULL);
  op = pa_stream_drain(p, _nsv_playback_pa_stream_drain_cb, self);

  if (op)
    pa_operation_unref(op);
}

static void
_nsv_playback_play_real(NsvPlayback *self)
{
  NsvPlaybackPrivate *priv = self->priv;
  pa_proplist *proplist;
  SF_INFO sfinfo;
  pa_buffer_attr attr;
  pa_sample_spec spec;

  if (!priv->filename)
    return;

  _nsv_playback_pa_set_volume(self, priv->volume);

  if (g_str_has_suffix(priv->filename, ".decoded"))
    priv->decoded = TRUE;

  if (priv->decoded)
  {
    priv->fp = fopen(priv->filename, "rb");

    if (!priv->fp)
      goto emit_error;

    spec.format = PA_SAMPLE_ALAW;
    spec.channels = 1;
    spec.rate = 48000;
  }
  else
  {
    priv->handle = open(priv->filename, 0);

    if (priv->handle == -1)
    {
      g_warning("Unable to open file descriptor '%s': %s (%d)", priv->filename,
                strerror(errno), errno);
      goto emit_error;
    }

    priv->sndfile = sf_open_fd(priv->handle, SFM_READ, &sfinfo, 0);

    if (!priv->sndfile)
      goto emit_error;

    switch (sfinfo.format & 0xFFFF)
    {
      case SF_FORMAT_ALAW:
        spec.format = PA_SAMPLE_ALAW;
        break;
      case SF_FORMAT_ULAW:
        spec.format = PA_SAMPLE_ULAW;
        break;
      case SF_FORMAT_FLOAT:
        spec.format = PA_SAMPLE_FLOAT32LE;
        break;
      case SF_FORMAT_PCM_U8:
        spec.format = PA_SAMPLE_U8;
        break;
      case SF_FORMAT_PCM_32:
        spec.format = PA_SAMPLE_S32LE;
        break;
      case SF_FORMAT_PCM_16:
        spec.format = PA_SAMPLE_S16LE;
        break;
      default:
        goto emit_error;
    }

    spec.channels = sfinfo.channels;
    spec.rate = sfinfo.samplerate;
  }

  attr.maxlength = -1;
  attr.minreq = pa_usec_to_bytes(4000000, &spec);
  attr.tlength = 2 * attr.minreq;
  attr.prebuf = pa_usec_to_bytes(2000000, &spec);
  attr.fragsize = -1;

  proplist = pa_proplist_new();

  if (priv->volume_set)
  {
    pa_proplist_sets(proplist, "module-stream-restore.id",
                     "x-maemo-hildon-notify");
  }

  if (priv->event_id)
    pa_proplist_sets(proplist, "event.id",  priv->event_id);

  if (priv->media_role)
    pa_proplist_sets(proplist, "media.role", priv->media_role);

  priv->pa_stream = pa_stream_new_with_proplist(priv->pa_context, PACKAGE,
                                                &spec, 0, proplist);
  pa_proplist_free(proplist);

  if (!priv->pa_stream)
    goto emit_error;

  pa_stream_set_state_callback(priv->pa_stream,
                               _nsv_playback_stream_state_cb, self);
  pa_stream_set_write_callback(priv->pa_stream,
                               _nsv_playback_stream_write_cb, self);

  if (pa_stream_connect_playback(priv->pa_stream, 0, &attr, 0, 0, 0) < 0)
    goto emit_error;

  g_timer_start(priv->timer);

  if (priv->max_timeout > 0)
  {
    priv->max_timeout_id = g_timeout_add(priv->max_timeout,
                                         _nsv_playback_emit_succeeded_cb, self);
  }

  if (priv->started)
  {
    g_signal_emit(self, started_id, 0);
    priv->started = FALSE;
  }

  return;

emit_error:
  g_idle_add(_nsv_playback_emit_error_cb, self);
}

static void
_nsv_playback_pa_context_state_cb(pa_context *c, void *userdata)
{
  NsvPlayback *self = NSV_PLAYBACK(userdata);
  NsvPlaybackPrivate *priv = self->priv;

  if (pa_context_get_state(c) == PA_CONTEXT_READY &&
      priv->play_pending)
  {
      _nsv_playback_play_real(self);
  }
}

static void
nsv_playback_init(NsvPlayback *self)
{
  NsvPlaybackPrivate *priv;
  pa_proplist *proplist;
  pa_mainloop_api *api;

  priv = g_new0(NsvPlaybackPrivate, 1);
  self->priv = priv;
  priv->handle = -1;
  priv->timer = g_timer_new();

  priv->pa_glib_mainloop = pa_glib_mainloop_new(g_main_context_default());

  if (!priv->pa_glib_mainloop)
    goto emit_error;

  api = pa_glib_mainloop_get_api(priv->pa_glib_mainloop);

  if (!api)
    goto emit_error;

  proplist = pa_proplist_new();
  pa_proplist_sets(proplist, "application.name", PACKAGE);
  pa_proplist_sets(proplist, "application.id", PACKAGE);
  pa_proplist_sets(proplist, "application.version", PACKAGE_VERSION);

  priv->pa_context =
      pa_context_new_with_proplist(api, PACKAGE, proplist);

  pa_proplist_free(proplist);

  if (!priv->pa_context)
    goto emit_error;

  pa_context_set_state_callback(priv->pa_context,
                                _nsv_playback_pa_context_state_cb, self);

  if (pa_context_connect(priv->pa_context, NULL,
                         PA_CONTEXT_NOFAIL|PA_CONTEXT_NOAUTOSPAWN, 0) < 0)
  {
    goto emit_error;
  }

  return;

emit_error:
    g_signal_emit(self, error_id, 0);
}

NsvPlayback *
nsv_playback_new()
{
  return NSV_PLAYBACK(g_object_new(NSV_TYPE_PLAYBACK, NULL));
}

gboolean
nsv_playback_play(NsvPlayback *self)
{
  NsvPlaybackPrivate *priv = self->priv;

  priv->started = TRUE;
  priv->stopped = FALSE;

  if (priv->pa_context &&
      pa_context_get_state(priv->pa_context) == PA_CONTEXT_READY)
  {
    _nsv_playback_play_real(self);
  }
  else
    priv->play_pending = TRUE;

  return TRUE;
}

gboolean
nsv_playback_stop(NsvPlayback *self)
{
  NsvPlaybackPrivate *priv = self->priv;

  if (!priv->pa_context)
    return FALSE;

  priv->stopped = TRUE;
  _nsv_playback_cleanup(self);
  g_signal_emit(self, stopped_id, 0);

  return TRUE;
}
