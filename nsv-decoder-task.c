#include <glib-object.h>
#include <gst/gstelement.h>
#include <gst/controller/gstcontroller.h>

#include "nsv-decoder-task.h"

#define NSV_DECODER_TASK_TYPE (nsv_decoder_task_get_type ())
#define NSV_DECODER_TASK(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
            NSV_DECODER_TASK_TYPE, NsvDecoderTask))

typedef struct _NsvDecoderTaskClass NsvDecoderTaskClass;

enum
{
  PROP_0,
  PROP_SOURCE_FILE,
  PROP_TARGET_FILE,
  PROP_CUT_OFF,
  PROP_FADE_LENGTH
};

struct _NsvDecoderTaskClass {
  GObjectClass parent_class;
};

struct _NsvDecoderTaskPrivate
{
  gchar *source_file;
  gchar *target_file;
  gint cut_off_time;
  gint fade_length_time;
  gboolean decoding_completed;
  guint data_so_far;
  guint cut_off_time_mul_96;
  gint emit_suceeded_timeout;
  gboolean decoding_started;
  GstElement *pipeline;
  GstBus *gst_bus;
  GstController *gst_volume_controller;
};

G_DEFINE_TYPE(NsvDecoderTask, nsv_decoder_task, G_TYPE_OBJECT);

static GObjectClass *parent_class = NULL;
static guint succeeded_id;
static guint error_id;

NsvDecoderTask *
nsv_decoder_task_new(const gchar *source_file, const gchar *target_file)
{
  return NSV_DECODER_TASK(g_object_new(NSV_DECODER_TASK_TYPE,
                                       "source-file", source_file,
                                       "target-file", target_file, NULL));
}

static void
nsv_decoder_task_set_property(GObject *object, guint prop_id,
                              const GValue *value, GParamSpec *pspec)
{
  NsvDecoderTaskPrivate *priv = NSV_DECODER_TASK(object)->priv;

  switch (prop_id)
  {
    case PROP_SOURCE_FILE:
      if (priv->source_file)
        g_free(priv->source_file);

      priv->source_file = g_value_dup_string(value);
      break;
    case PROP_TARGET_FILE:
      if (priv->target_file)
        g_free(priv->target_file);

      priv->target_file = g_value_dup_string(value);
      break;
    case PROP_CUT_OFF:
      priv->cut_off_time = g_value_get_int(value);
      break;
    case PROP_FADE_LENGTH:
      priv->fade_length_time = g_value_get_int(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
nsv_decoder_task_get_property(GObject *object, guint prop_id, GValue *value,
                              GParamSpec *pspec)
{
  NsvDecoderTaskPrivate *priv = NSV_DECODER_TASK(object)->priv;

  switch (prop_id)
  {
    case PROP_SOURCE_FILE:
      g_value_set_string(value, priv->source_file);
      break;
    case PROP_TARGET_FILE:
      g_value_set_string(value, priv->target_file);
      break;
    case PROP_CUT_OFF:
      g_value_set_int(value, priv->cut_off_time);
      break;
    case PROP_FADE_LENGTH:
      g_value_set_int(value, priv->fade_length_time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_nsv_decoder_task_gst_cleanup(NsvDecoderTask *self)
{
  NsvDecoderTaskPrivate *priv = self->priv;

  if (priv->gst_volume_controller)
  {
    gst_object_unref(priv->gst_volume_controller);
    priv->gst_volume_controller = NULL;
  }

  if (priv->pipeline)
  {
    gst_element_set_state(priv->pipeline, GST_STATE_NULL);
    gst_object_unref(priv->pipeline);
    priv->pipeline = NULL;
  }
}

static void
nsv_decoder_task_finalize(GObject *object)
{
  NsvDecoderTask *self = NSV_DECODER_TASK(object);
  NsvDecoderTaskPrivate *priv = self->priv;

  if (priv->emit_suceeded_timeout)
  {
    g_source_remove(priv->emit_suceeded_timeout);
    priv->emit_suceeded_timeout = 0;
  }

  if (self->category)
    g_free(self->category);

  _nsv_decoder_task_gst_cleanup(self);

  if (priv->target_file)
  {
    g_free(priv->target_file);
    priv->target_file = NULL;
  }

  if (priv->source_file)
  {
    g_free(priv->source_file);
    priv->source_file = NULL;
  }

  g_free(priv);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
nsv_decoder_task_class_init(NsvDecoderTaskClass *klass)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent(klass);
  object_class = G_OBJECT_CLASS(klass);
  object_class->set_property = nsv_decoder_task_set_property;
  object_class->get_property = nsv_decoder_task_get_property;
  object_class->finalize = nsv_decoder_task_finalize;

  g_object_class_install_property(
        object_class, PROP_SOURCE_FILE,
        g_param_spec_string("source-file",
                            NULL, NULL, NULL,
                            G_PARAM_READWRITE));
  g_object_class_install_property(
        object_class, PROP_TARGET_FILE,
        g_param_spec_string("target-file",
                            NULL, NULL, NULL,
                            G_PARAM_READWRITE));
  g_object_class_install_property(
        object_class, PROP_CUT_OFF,
        g_param_spec_int("cut-off",
                         NULL, "Cut off time in milliseconds",
                         G_MININT, G_MAXINT, 60000,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  g_object_class_install_property(
        object_class, PROP_FADE_LENGTH,
        g_param_spec_int("fade-length",
                         NULL, "How long to fade out in milliseconds",
                         G_MININT, G_MAXINT, 5000,
                         G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  succeeded_id = g_signal_new("succeeded",
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

static void
nsv_decoder_task_init(NsvDecoderTask *self)
{
  self->priv = g_new0(NsvDecoderTaskPrivate, 1);
}

static void
nsv_decoder_task_gst_cleanup(NsvDecoderTask *self)
{
  NsvDecoderTaskPrivate *priv = self->priv;

  if (priv->gst_volume_controller)
  {
    gst_object_unref(priv->gst_volume_controller);
    priv->gst_volume_controller = NULL;
  }

  if (priv->pipeline)
  {
    gst_element_set_state(priv->pipeline, GST_STATE_NULL);
    gst_object_unref(priv->pipeline);
    priv->pipeline = NULL;
  }
}

static gboolean
_nsv_decoder_task_emit_suceeded_cb(gpointer user_data)
{
  NsvDecoderTask *self = NSV_DECODER_TASK(user_data);
  NsvDecoderTaskPrivate *priv = self->priv;

  if (priv->decoding_started)
  {
    priv->decoding_started = FALSE;
    gst_element_set_state(priv->pipeline, GST_STATE_NULL);
    g_signal_emit(self, succeeded_id, 0);
  }

  return FALSE;
}

static gboolean
_nsv_decoder_task_gst_buffer_probe_cb(GstPad *pad, GstBuffer *buffer,
                                      NsvDecoderTask *self)
{
  NsvDecoderTaskPrivate *priv = self->priv;

  if (priv->decoding_completed)
    return TRUE;

  priv->data_so_far += buffer->size;

  if (priv->data_so_far < priv->cut_off_time_mul_96)
    return TRUE;

  priv->decoding_completed = TRUE;
  priv->emit_suceeded_timeout =
      g_idle_add(_nsv_decoder_task_emit_suceeded_cb, self);

  return TRUE;
}

static void
_nsv_decoder_task_gst_new_decoded_pab_cb(GstElement *decodebin, GstPad *pad,
                                         gboolean last, gpointer data)
{
  GstCaps *caps = gst_pad_get_caps(pad);

  if (!gst_caps_is_empty(caps) && !gst_caps_is_any(caps))
  {
    GstStructure *caps_s = gst_caps_get_structure(caps, 0);

    if (g_str_has_prefix(gst_structure_get_name(caps_s), "audio/x-raw"))
    {
      GstPad *sink_pad = gst_element_get_pad(GST_ELEMENT(data), "sink");

      gst_pad_link(pad, sink_pad);
      gst_object_unref(sink_pad);
    }
  }

  if (caps)
    gst_caps_unref(caps);
}

static gboolean
_nsv_decoder_task_gst_bus_watch_cb(GstBus *bus, GstMessage *message,
                                   gpointer user_data)
{
  NsvDecoderTask *self = (NsvDecoderTask *)user_data;
  NsvDecoderTaskPrivate *priv = self->priv;

  switch (GST_MESSAGE(message)->type)
  {
    case GST_MESSAGE_ERROR:
    {
      GError *error = NULL;

      priv->decoding_started = FALSE;
      gst_message_parse_error(message, &error, 0);
      g_error_free(error);

      if (priv->target_file &&
          g_file_test(priv->target_file, G_FILE_TEST_EXISTS))
      {
        g_unlink(priv->target_file);
      }

      g_signal_emit(self, error_id, 0);
      break;
    }
    case GST_MESSAGE_SEGMENT_DONE:
    {
      if (GST_ELEMENT(GST_MESSAGE(message)->src) != priv->pipeline)
        break;

      gst_element_send_event(GST_ELEMENT(GST_MESSAGE(message)->src),
                             gst_event_new_eos());
    }
    case GST_MESSAGE_EOS:
    {
      if (GST_ELEMENT(GST_MESSAGE(message)->src) == priv->pipeline &&
          priv->decoding_started)
      {
        if (priv->emit_suceeded_timeout)
        {
          g_source_remove(priv->emit_suceeded_timeout);
          priv->emit_suceeded_timeout = 0;
        }

        priv->decoding_started = FALSE;
        priv->decoding_completed = TRUE;
        g_signal_emit(self, succeeded_id, 0);
      }

      break;
    }
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState pending;
      GstState newstate;
      GstState oldstate;

      if (GST_ELEMENT(GST_MESSAGE(message)->src) != priv->pipeline)
        break;

      gst_message_parse_state_changed(message, &oldstate, &newstate, &pending);

      if (oldstate == GST_STATE_READY && newstate == GST_STATE_PAUSED)
      {
        gst_element_send_event(
              priv->pipeline,
              gst_event_new_seek(1.0,
                                 GST_FORMAT_TIME,
                                 GST_SEEK_FLAG_SEGMENT | GST_SEEK_FLAG_FLUSH,
                                 GST_SEEK_TYPE_SET,
                                 0LL,
                                 GST_SEEK_TYPE_SET,
                                 1000000LL * priv->cut_off_time));
      }

      break;
    }
    default:
      break;
  }

  return 1;
}

gboolean
nsv_decoder_task_start(NsvDecoderTask *self)
{
  NsvDecoderTaskPrivate *priv;
  GstElement *filesrc;
  GstElement *encoder_bin;
  GstElement *audioconvert;
  GstElement *audioresample;
  GstElement *capsfilter;
  GstPad *sink_pad;
  GstCaps *caps;
  GstInterpolationControlSource *interp_ctl_src;
  GstBus *bus;
  GstElement *filesink;
  GstElement *volume;
  GstElement *wavenc;
  GstElement *decodebin;
  GValue interp_val =  {0};

  priv = self->priv;
  gst_controller_init(NULL, NULL);

  if (!(priv->pipeline = gst_pipeline_new("decoder-pipeline")) ||
      !(filesrc = gst_element_factory_make("filesrc", NULL)) ||
      !(decodebin = gst_element_factory_make("decodebin2", NULL)))
  {
    if (priv->pipeline)
    {
      gst_object_unref(priv->pipeline);
      priv->pipeline = NULL;

      if (filesrc)
        gst_object_unref(filesrc);
    }

    return FALSE;
  }

  gst_bin_add_many(GST_BIN(priv->pipeline), filesrc, decodebin, NULL);

  if (!gst_element_link(filesrc, decodebin))
  {
    g_object_unref(priv->pipeline);
    priv->pipeline = NULL;

    return FALSE;
  }

  if (!(encoder_bin = gst_bin_new("encoder-bin")) ||
      !(audioconvert = gst_element_factory_make("audioconvert", NULL)) ||
      !(audioresample = gst_element_factory_make("audioresample", NULL)) ||
      !(capsfilter = gst_element_factory_make("capsfilter", NULL)) ||
      !(volume = gst_element_factory_make("volume", NULL)) ||
      !(wavenc = gst_element_factory_make("wavenc", NULL)) ||
      !(filesink = gst_element_factory_make("filesink", NULL)))
  {

    if (encoder_bin)
    {
      gst_object_unref(encoder_bin);

      if (audioconvert)
      {
        gst_object_unref(audioconvert);

        if (audioresample)
        {
          gst_object_unref(audioresample);

          if (capsfilter)
          {
            gst_object_unref(capsfilter);

            if (volume)
            {
              gst_object_unref(volume);

              if (wavenc)
                gst_object_unref(wavenc);
            }
          }
        }
      }
    }

    g_object_unref(priv->pipeline);
    priv->pipeline = NULL;

    return FALSE;
  }

  gst_bin_add_many(GST_BIN(encoder_bin), audioconvert, audioresample,
                   capsfilter, volume, wavenc, filesink, NULL);

  if (!gst_element_link_many(audioconvert, audioresample, capsfilter, volume,
                             wavenc, filesink, NULL))
  {
    gst_object_unref(encoder_bin);
    gst_object_unref(priv->pipeline);
    priv->pipeline = NULL;

    return FALSE;
  }

  sink_pad = gst_element_get_pad(audioconvert, "sink");
  gst_element_add_pad(encoder_bin, gst_ghost_pad_new("sink", sink_pad));
  gst_object_unref(sink_pad);

  gst_bin_add(GST_BIN(priv->pipeline), encoder_bin);

  caps = gst_caps_new_simple("audio/x-raw-int",
                             "rate", 24, 48000,
                             "width", 24, 16,
                             "depth", 24, 16,
                             "channels", 24, 1,
                             NULL);

  g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
  g_object_set(G_OBJECT(filesink), "location", priv->target_file, NULL);
  g_object_set(G_OBJECT(filesrc), "location", priv->source_file, NULL);

  g_signal_connect_data(G_OBJECT(decodebin), "new-decoded-pad",
                        (GCallback)_nsv_decoder_task_gst_new_decoded_pab_cb,
                        encoder_bin, NULL, 0);

  priv->gst_bus = gst_pipeline_get_bus(GST_PIPELINE(priv->pipeline));
  priv->gst_volume_controller =
      gst_controller_new(G_OBJECT(volume), "volume", 0);

  interp_ctl_src = gst_interpolation_control_source_new();
  gst_interpolation_control_source_set_interpolation_mode(
        interp_ctl_src, GST_INTERPOLATE_LINEAR);
  gst_controller_set_control_source(
        priv->gst_volume_controller,
        "volume", GST_CONTROL_SOURCE(interp_ctl_src));
  gst_object_unref(interp_ctl_src);

  g_value_init(&interp_val, G_TYPE_DOUBLE);

  g_value_set_double(&interp_val, 1.0);
  gst_interpolation_control_source_set(
        interp_ctl_src,
        1000000LL * (priv->cut_off_time - priv->fade_length_time), &interp_val);

  g_value_set_double(&interp_val, 0.0);
  gst_interpolation_control_source_set(
        interp_ctl_src, 1000000LL * priv->cut_off_time, &interp_val);
  g_value_unset(&interp_val);

  priv->cut_off_time_mul_96 = 96 * priv->cut_off_time;

  if (priv->cut_off_time_mul_96 > 0)
  {
    sink_pad = gst_element_get_pad(filesink, "sink");

    gst_pad_add_buffer_probe(
          sink_pad, (GCallback)_nsv_decoder_task_gst_buffer_probe_cb, self);
    gst_object_unref(sink_pad);
  }

  if (gst_element_set_state(priv->pipeline, GST_STATE_PLAYING))
  {
    priv->decoding_started = TRUE;
    bus = gst_element_get_bus(priv->pipeline);
    gst_bus_add_watch(bus, _nsv_decoder_task_gst_bus_watch_cb, self);
    gst_object_unref(bus);
    return TRUE;
  }

  _nsv_decoder_task_gst_cleanup(self);

  return FALSE;
}

void
nsv_decoder_task_stop(NsvDecoderTask *self)
{
  NsvDecoderTaskPrivate *priv = self->priv;
  _nsv_decoder_task_gst_cleanup(self);

  if (priv->target_file)
  {
    if (g_file_test(priv->target_file, G_FILE_TEST_EXISTS))
      g_unlink(priv->target_file);
  }
}
