#include <dbus/dbus.h>
#include <dbus/dbus-glib-bindings.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "nsv-system-proxy.h"

#define NSV_TYPE_SYSTEM_PROXY (nsv_system_proxy_get_type ())
#define NSV_SYSTEM_PROXY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                                         NSV_TYPE_SYSTEM_PROXY, NsvSystemProxy))

typedef struct _NsvSystemProxyClass NsvSystemProxyClass;
typedef struct _NsvSystemProxyPrivate NsvSystemProxyPrivate;

struct _NsvSystemProxy
{
  GObject parent_instance;
  NsvSystemProxyPrivate *priv;
};

struct _NsvSystemProxyClass {
  GObjectClass parent_class;
};

struct _NsvSystemProxyPrivate
{
  DBusGConnection *conn;
  DBusGProxy *proxy;
  guint startup_timeout_id;
};

G_DEFINE_TYPE(NsvSystemProxy, nsv_system_proxy, G_TYPE_OBJECT);

static GObjectClass *parent_class = NULL;
static guint startup_done_id;
static guint startup_timeout_id;

static DBusHandlerResult
_nsv_system_proxy_dbus_filter(DBusConnection *connection, DBusMessage *message,
                              void *user_data)
{
  NsvSystemProxy *self = NSV_SYSTEM_PROXY(user_data);
  NsvSystemProxyPrivate *priv = self->priv;

  if (dbus_message_is_signal(message, "com.nokia.startup.signal", "init_done"))
  {
    if (priv->startup_timeout_id)
    {
      g_source_remove(priv->startup_timeout_id);
      priv->startup_timeout_id = 0;
    }

    g_signal_emit(self, startup_done_id, 0);
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
nsv_system_proxy_finalize(GObject *object)
{
  NsvSystemProxy *self = NSV_SYSTEM_PROXY(object);
  NsvSystemProxyPrivate *priv = self->priv;
  if (priv)
  {
    if (priv->startup_timeout_id)
    {
      g_source_remove(priv->startup_timeout_id);
      priv->startup_timeout_id = 0;
    }

    if (priv->proxy)
    {
      g_object_unref(priv->proxy);
      priv->proxy = NULL;
    }

    if (priv->conn)
    {
      DBusConnection *dbus = dbus_g_connection_get_connection(priv->conn);

      dbus_connection_remove_filter(dbus, _nsv_system_proxy_dbus_filter, self);
      dbus_g_connection_unref(priv->conn);
      priv->conn = NULL;
    }

    g_free(priv);
  }

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
nsv_system_proxy_class_init(NsvSystemProxyClass *klass)
{
  parent_class = g_type_class_peek_parent(klass);

  G_OBJECT_CLASS(klass)->finalize = nsv_system_proxy_finalize;

  startup_done_id =
      g_signal_new("startup-done",
                   G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                   0, NULL, NULL,
                   &g_cclosure_marshal_VOID__VOID,
                   G_TYPE_NONE, 0, G_TYPE_NONE);
  startup_timeout_id =
      g_signal_new("startup-timeout",
                   G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                   0, NULL, NULL,
                   &g_cclosure_marshal_VOID__VOID,
                   G_TYPE_NONE, 0, G_TYPE_NONE);
}

static gboolean
_nsv_system_proxy_emit_startup_timeout_cb(gpointer user_data)
{
  NsvSystemProxy *self = NSV_SYSTEM_PROXY(user_data);

  self->priv->startup_timeout_id = 0;
  g_signal_emit(self, startup_timeout_id, 0);

  return FALSE;
}

static void
nsv_system_proxy_init(NsvSystemProxy *self)
{
  DBusConnection *dbus;
  GError *error = NULL;
  NsvSystemProxyPrivate *priv = g_new0(NsvSystemProxyPrivate, 1);

  if (priv)
  {
    self->priv = priv;
    priv->conn = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);

    if (priv->conn)
    {
      priv->proxy = dbus_g_proxy_new_for_name(priv->conn,
                                              DBUS_SERVICE_DBUS,
                                              DBUS_PATH_DBUS,
                                              DBUS_INTERFACE_DBUS);
      if (priv->proxy)
      {
        gchar *match =
            g_strdup_printf("type='signal', interface='%s', member='%s'",
                            "com.nokia.startup.signal",
                            "init_done");

        org_freedesktop_DBus_add_match(priv->proxy, match, NULL);
        g_free(match);

        dbus = dbus_g_connection_get_connection(priv->conn);
        dbus_connection_add_filter(dbus,
                                   _nsv_system_proxy_dbus_filter,
                                   self,
                                   NULL);
        priv->startup_timeout_id =
            g_timeout_add_seconds(90,
                                  _nsv_system_proxy_emit_startup_timeout_cb,
                                  self);
      }
      else
        g_log(0, G_LOG_LEVEL_WARNING,
              "Failed to create DBus system bus proxy!");
    }
    else
    {
      g_log(0, G_LOG_LEVEL_WARNING,
            "Failed to connect to system bus: %s", error->message);
      g_error_free(error);
    }
  }
}

/* FIXME? - not thread safe */
NsvSystemProxy *
nsv_system_proxy_get_instance()
{
  static NsvSystemProxy *nsv_system_proxy = NULL;

  if (!nsv_system_proxy)
  {
    nsv_system_proxy =
        (NsvSystemProxy *)g_object_new(NSV_TYPE_SYSTEM_PROXY, NULL);
  }

  return nsv_system_proxy;
}
