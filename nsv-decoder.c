#include <dbus/dbus-glib-bindings.h>
#include <glib/gstdio.h>

#include <stdio.h>

#include "nsv-decoder.h"

#define NSV_DECODER_TYPE (nsv_decoder_get_type ())
#define NSV_DECODER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
            NSV_DECODER_TYPE, NsvDecoder))

typedef struct _NsvDecoderClass NsvDecoderClass;
typedef struct _NsvDecoderPrivate NsvDecoderPrivate;

enum
{
  PROP_0,
  PROP_TARGET_PATH
};

struct _NsvDecoder
{
  GObject parent_instance;
  NsvDecoderPrivate *priv;
};

struct _NsvDecoderClass {
  GObjectClass parent_class;
};

struct _NsvDecoderPrivate
{
  gchar *target_path;
  DBusGConnection *conn;
  DBusGProxy *proxy;
};

G_DEFINE_TYPE(NsvDecoder, nsv_decoder, G_TYPE_OBJECT);

struct nsv_decoder_decode_data
{
  void (*cb)(DBusGProxy *proxy, GError *error, NsvDecoder *self);
  NsvDecoder *decoder;
};

static GObjectClass *parent_class = NULL;

static gchar *
_nsv_decoder_create_target_filename(NsvDecoder *self, const gchar *target_file)
{
  NsvDecoderPrivate *priv = self->priv;

  if (target_file)
  {
    gchar *target_path = priv->target_path;
    gchar *basename = g_path_get_basename(target_file);
    gchar *rv;

    if (!priv->target_path)
      target_path = ".";

    rv = g_strdup_printf("%s/%s.%s", target_path, basename, "wav");
    g_free(basename);

    return rv;
  }

  return NULL;
}

static void
_nsv_decoder_decoded_cb(DBusGProxy *proxy, gchar *category, gchar *source_file,
                        char *target_file, gpointer user_data)
{
  gchar *target_filename =
      _nsv_decoder_create_target_filename((NsvDecoder *)user_data, source_file);

  if (target_filename)
  {
    rename(target_file, target_filename);
    g_free(target_filename);
  }
  else
  {
    g_log(0, G_LOG_LEVEL_WARNING, "Can't make a decoded filename");
    g_unlink(target_file);
  }
}

static void
_nsv_decoder_error_decoding_cb(DBusGProxy *proxy, gchar *category,
                               gchar *source_file, char *target_file,
                               gpointer user_data)
{
}

static void
nsv_decoder_init(NsvDecoder *self)
{
  NsvDecoderPrivate *priv;

  priv = g_new0(NsvDecoderPrivate, 1);
  self->priv = priv;
  priv->conn = dbus_g_bus_get(DBUS_BUS_SESSION, NULL);

  priv->proxy = dbus_g_proxy_new_for_name(priv->conn,
                                          "com.nokia.NsvDecoder",
                                          "/com/nokia/NsvDecoder",
                                          "com.nokia.NsvDecoder");

  dbus_g_proxy_add_signal(self->priv->proxy,
                          "Decoded",
                          G_TYPE_STRING,
                          G_TYPE_STRING,
                          G_TYPE_STRING,
                          G_TYPE_INVALID);
  dbus_g_proxy_add_signal(self->priv->proxy,
                          "ErrorDecoding",
                          G_TYPE_STRING,
                          G_TYPE_STRING,
                          G_TYPE_STRING,
                          G_TYPE_INVALID);
  dbus_g_proxy_connect_signal(
        self->priv->proxy, "Decoded",
        G_CALLBACK(_nsv_decoder_decoded_cb), self, NULL);
  dbus_g_proxy_connect_signal(
        self->priv->proxy, "ErrorDecoding",
        G_CALLBACK(_nsv_decoder_error_decoding_cb), self, NULL);
}

static void
nsv_decoder_set_property(GObject *object, guint prop_id, const GValue *value,
                         GParamSpec *pspec)
{
  NsvDecoderPrivate *priv = NSV_DECODER(object)->priv;

  switch (prop_id)
  {
    case PROP_TARGET_PATH:

      if (priv->target_path)
        g_free(priv->target_path);

      priv->target_path = g_value_dup_string(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
nsv_decoder_get_property(GObject *object, guint prop_id, GValue *value,
                         GParamSpec *pspec)
{
  NsvDecoderPrivate *priv = NSV_DECODER(object)->priv;

  switch (prop_id)
  {
    case PROP_TARGET_PATH:
      g_value_set_string(value, priv->target_path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
nsv_decoder_finalize(GObject *object)
{
  NsvDecoderPrivate *priv = NSV_DECODER(object)->priv;

  g_object_unref(priv->proxy);
  dbus_g_connection_unref(priv->conn);

  if (priv->target_path)
    g_free(priv->target_path);

  g_free(priv);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
nsv_decoder_class_init(NsvDecoderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  parent_class = g_type_class_peek_parent(klass);

  object_class->set_property = nsv_decoder_set_property;
  object_class->get_property = nsv_decoder_get_property;
  object_class->finalize = nsv_decoder_finalize;

  g_object_class_install_property(
        object_class, PROP_TARGET_PATH,
        g_param_spec_string("target-path",
                            NULL, NULL, NULL,
                            G_PARAM_READWRITE));
}

NsvDecoder *
nsv_decoder_new()
{
  return NSV_DECODER(g_object_new(NSV_DECODER_TYPE, NULL));
}

gchar *
nsv_decoder_get_decoded_filename(NsvDecoder *self, const gchar *target_file)
{
  gchar *target_filename =
      _nsv_decoder_create_target_filename(self, target_file);

  if (target_filename && !g_file_test(target_filename, G_FILE_TEST_EXISTS))
  {
    NsvDecoderPrivate *priv = self->priv;

    g_free(target_filename);

    if (target_file)
    {
      gchar *basename = g_path_get_basename(target_file);
      gchar *target_path = priv->target_path;

      if (!priv->target_path)
        target_path = ".";

      target_filename =
          g_strdup_printf("%s/%s.%s", target_path, basename, "decoded");
      g_free(basename);

      if (target_filename && !g_file_test(target_filename, G_FILE_TEST_EXISTS))
      {
        g_free(target_filename);
        target_filename = NULL;
      }
    }
    else
      target_filename = NULL;
  }

  return target_filename;
}

static void
_nsv_decoder_decode_finished_cb(DBusGProxy *proxy, GError *error,
                                NsvDecoder *self)
{
}

static void
_nsv_decoder_decode_cb(DBusGProxy *proxy, DBusGProxyCall *call_id,
                       gpointer user_data)
{
  GError *error = NULL;
  struct nsv_decoder_decode_data *data =
      (struct nsv_decoder_decode_data *)user_data;

  dbus_g_proxy_end_call(proxy, call_id, &error, G_TYPE_INVALID);
  data->cb(proxy, error, data->decoder);
}

static void
_nsv_decoder_decode_destroy_notify_cb(gpointer mem_block)
{
  g_slice_free(struct nsv_decoder_decode_data, mem_block);
}

void
nsv_decoder_decode(NsvDecoder *self, const gchar *category,
                   const gchar *source_file)
{
  NsvDecoderPrivate *priv = self->priv;
  gchar *basename;
  gchar *target_path;
  gchar *target_file;
  DBusGProxy *proxy;
  struct nsv_decoder_decode_data *data;

  basename = g_path_get_basename(source_file);
  target_path = priv->target_path;

  if (!priv->target_path)
    target_path = ".";

  target_file = g_strdup_printf("%s/%s.%s", target_path, basename, "wav");
  g_free(basename);

  proxy = self->priv->proxy;
  data = g_slice_new(struct nsv_decoder_decode_data);
  data->cb = _nsv_decoder_decode_finished_cb;
  data->decoder = self;

  dbus_g_proxy_begin_call(proxy, "Decode", _nsv_decoder_decode_cb, data,
                          _nsv_decoder_decode_destroy_notify_cb,
                          G_TYPE_STRING, category,
                          G_TYPE_STRING, source_file,
                          G_TYPE_STRING, target_file,
                          G_TYPE_INVALID);
  g_free(target_file);
}
