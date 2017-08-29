#include <dbus/dbus-glib-bindings.h>

#include <stdlib.h>

#include "nsv-service-marshal.h"

static void nsv_decoder_service_decode();
#include "dbus-glib-marshal-nsv-decoder-service.h"

#include "nsv-decoder-task.h"

#define NSV_DECODER_SERVICE_TYPE (nsv_decoder_service_get_type ())
#define NSV_DECODER_SERVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
            NSV_DECODER_SERVICE_TYPE, NsvDecoderService))
#define NSV_DECODER_SERVICE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
            NSV_DECODER_SERVICE_TYPE, NsvDecoderServicePrivate))

typedef struct _NsvDecoderService NsvDecoderService;
typedef struct _NsvDecoderServiceClass NsvDecoderServiceClass;
typedef struct _NsvDecoderServicePrivate NsvDecoderServicePrivate;

struct _NsvDecoderService
{
  GObject parent_instance;
  NsvDecoderServicePrivate *priv;
};

struct _NsvDecoderServiceClass {
  GObjectClass parent_class;
};

struct _NsvDecoderServicePrivate
{
  DBusGConnection *conn;
  DBusGProxy *proxy;
  GError *error;
  GQueue queue;
  NsvDecoderTask *current_task;
  guint exit_timeout_id;
};

G_DEFINE_TYPE(NsvDecoderService, nsv_decoder_service, G_TYPE_OBJECT);

static void nsv_decoder_service_start_next_task(NsvDecoderService *self);

static GObjectClass *parent_class = NULL;
static guint decoded_id;
static guint error_decoding_id;

#define EXIT_TIMEOUT 5

static void
nsv_decoder_service_dispose(GObject *object)
{
  NsvDecoderService *self = NSV_DECODER_SERVICE(object);
  NsvDecoderServicePrivate *priv = self->priv;

  if (priv->proxy)
  {
    g_object_unref(priv->proxy);
    priv->proxy = NULL;
  }

  if (priv->conn)
  {
    dbus_g_connection_unref(priv->conn);
    priv->conn = NULL;
  }

  if (priv->current_task)
  {
    g_object_unref(priv->current_task);
    priv->current_task = NULL;
  }

  g_queue_clear(&priv->queue);
  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
nsv_decoder_service_class_init(NsvDecoderServiceClass *klass)
{
  parent_class = g_type_class_peek_parent(klass);
  g_type_class_add_private(klass, sizeof(NsvDecoderServicePrivate));
  G_OBJECT_CLASS(klass)->dispose = nsv_decoder_service_dispose;

  decoded_id =
      g_signal_new("decoded",
                   G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                   0, NULL, NULL,
                   nsv_service_marshal_VOID__STRING_STRING_STRING,
                   G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  error_decoding_id =
      g_signal_new("error-decoding",
                   G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                   0, NULL, NULL,
                   nsv_service_marshal_VOID__STRING_STRING_STRING,
                   G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

  dbus_g_object_type_install_info(NSV_DECODER_SERVICE_TYPE,
                                  &dbus_glib_nsv_decoder_service_object_info);
}

static gboolean
exit_timeout_cb(gpointer user_data)
{
  g_log(0, G_LOG_LEVEL_DEBUG, "Timed out, exiting");
  exit(0);
}

static void
nsv_decoder_service_init(NsvDecoderService *self)
{
  NsvDecoderServicePrivate *priv = NSV_DECODER_SERVICE_GET_PRIVATE(self);

  self->priv = priv;
  g_queue_init(&priv->queue);
  priv->conn = dbus_g_bus_get(DBUS_BUS_SESSION, &priv->error);

  if (priv->conn)
  {
    guint nameret;

    priv->proxy = dbus_g_proxy_new_for_name(priv->conn,
                                            DBUS_SERVICE_DBUS,
                                            DBUS_PATH_DBUS,
                                            DBUS_INTERFACE_DBUS);

    dbus_g_connection_register_g_object(priv->conn,
                                        "/com/nokia/NsvDecoder",
                                        G_OBJECT(self));

    if (org_freedesktop_DBus_request_name(self->priv->proxy,
                                          "com.nokia.NsvDecoder",
                                          DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                          &nameret,
                                          &self->priv->error))
    {
      if (nameret == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
      {
        priv->exit_timeout_id =
            g_timeout_add_seconds(EXIT_TIMEOUT, exit_timeout_cb, NULL);
      }
      else
        priv->error = g_error_new(0, 0, "Service already exists");
    }
  }
}

NsvDecoderService *nsv_decoder_service_new()
{
  NsvDecoderService *self =
      (NsvDecoderService *)g_object_new(NSV_DECODER_SERVICE_TYPE, NULL);

  if (self->priv->error)
  {
    g_log(0, G_LOG_LEVEL_ERROR, "ERROR: %s", self->priv->error->message);

    /* FIXME - WTF?!? */
    while (1)
      ;
  }

  return self;
}

static void
_nsv_decoder_service_task_succeeded_cb(NsvDecoderTask *task,
                                       NsvDecoderService *service)
{
  gchar *target_file = NULL;
  gchar *source_file = NULL;

  g_object_get(task, "source-file", &source_file, "target-file", &target_file,
               NULL);
  g_signal_emit(service, decoded_id, 0, task->category, source_file,
                target_file);
  g_free(source_file);
  g_free(target_file);
  g_object_unref(service->priv->current_task);
  service->priv->current_task = NULL;
  nsv_decoder_service_start_next_task(service);
}

static void
_nsv_decoder_service_task_error_cb(NsvDecoderTask *task,
                                   NsvDecoderService *service)
{
  gchar *target_file = NULL;
  gchar *source_file = NULL;

  nsv_decoder_task_stop(service->priv->current_task);
  g_object_get(task, "source-file", &source_file, "target-file", &target_file,
               NULL);
  g_signal_emit(service, error_decoding_id, 0, task->category, source_file,
                target_file);
  g_free(source_file);
  g_free(target_file);
  g_object_unref(service->priv->current_task);
  service->priv->current_task = NULL;
  nsv_decoder_service_start_next_task(service);
}

static void
nsv_decoder_service_start_next_task(NsvDecoderService *self)
{
  NsvDecoderServicePrivate *priv = self->priv;
  NsvDecoderTask *task;

  if (priv->current_task)
    return;

  while((task = (NsvDecoderTask *)g_queue_pop_head(&priv->queue)))
  {
    g_signal_connect_data(task, "succeeded",
                          (GCallback)_nsv_decoder_service_task_succeeded_cb,
                          self, NULL, 0);
    g_signal_connect_data(task, "error",
                          (GCallback)_nsv_decoder_service_task_error_cb,
                          self, NULL, 0);

    if (!nsv_decoder_task_start(task))
    {
      gchar *source_file = NULL;
      gchar *target_file = NULL;

      g_object_get(task,
                   "source-file", &source_file,
                   "target-file", &target_file,
                   NULL);

      g_signal_emit(self, error_decoding_id, 0, task->category,
                    source_file, target_file);

      g_free(source_file);
      g_free(target_file);
      g_object_unref(task);
    }
    else
    {
      priv->current_task = task;
      return;
    }
  }

  if (!priv->exit_timeout_id)
  {
    priv->exit_timeout_id =
        g_timeout_add_seconds(EXIT_TIMEOUT, exit_timeout_cb, NULL);
  }
}

static void
nsv_decoder_service_decode(NsvDecoderService *self, const gchar *category,
                           const char *source_filename,
                           const char *target_filename,
                           DBusGMethodInvocation *context)
{
  NsvDecoderTask *  task;
  NsvDecoderServicePrivate *priv = self->priv;

  g_log(0, G_LOG_LEVEL_DEBUG, "Decoding (%s): %s -> %s",
        category, source_filename, target_filename);

  if (priv->exit_timeout_id)
  {
    g_source_remove(priv->exit_timeout_id);
    priv->exit_timeout_id = 0;
  }

  task =
      (NsvDecoderTask *)nsv_decoder_task_new(source_filename, target_filename);
  task->category = g_strdup(category);
  g_queue_push_tail(&priv->queue, task);
  nsv_decoder_service_start_next_task(self);
  dbus_g_method_return(context, 0);
}

int
main(int argc, char **argv)
{
  NsvDecoderService *decoder;
  GMainLoop *loop;

  g_thread_init(NULL);
  g_type_init();
  gst_init(&argc, &argv);
  decoder = nsv_decoder_service_new();
  loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
  g_object_unref(decoder);

  return 0;
}
