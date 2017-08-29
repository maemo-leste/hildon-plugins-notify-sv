#include <glib-object.h>
#include <dbus/dbus.h>
#include <libplayback/playback.h>

#include <stdio.h>

#include "sp_timestamp.h"

#include "nsv-private.h"
#include "nsv-notification.h"

struct nsv_notification_manager
{
  struct nsv_notification *current_notification;
  GQueue *queue;
  GHashTable *events;
  gint id;
  DBusConnection *conn;
};

static struct nsv_notification_manager *mgr = NULL;

static void nsv_notification_mgr_queue_try_next(struct nsv_notification *n,
                                                gboolean play_granted);
static void nsv_notification_mgr_queue_start_next();

gboolean
nsv_notification_init()
{
  if (mgr)
    return TRUE;

  mgr = g_new0(struct nsv_notification_manager, 1);

  if (!mgr)
    return FALSE;

  mgr->events = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  if (!mgr->events)
    goto err_events;

  mgr->queue = g_queue_new();

  if (!mgr->queue)
    goto err_queue;

  mgr->conn = dbus_bus_get(DBUS_BUS_SESSION, NULL);

  if (!mgr->conn)
    goto err_dbus;

  if (nsv_policy_mgr_init())
    return TRUE;

  dbus_connection_unref(mgr->conn);
  mgr->conn = NULL;

err_dbus:
  g_queue_free(mgr->queue);
  mgr->queue = NULL;

err_queue:
  g_hash_table_destroy(mgr->events);
  mgr->events = NULL;

err_events:
  g_free(mgr);
  mgr = NULL;

  return FALSE;
}

struct nsv_notification *
nsv_notification_new(const char *category)
{
  if (mgr && g_hash_table_lookup(mgr->events, category))
  {
    struct nsv_notification *n = g_new0(struct nsv_notification, 1);

    n->type = g_strdup(category);
    n->event_status = g_new0(struct notification_event_status, 1);
    n->event_status->playing = FALSE;

    return n;
  }

  return NULL;
}

static struct notification_impl *
get_implementation(nsv_notification *n)
{
  g_assert(n != NULL);
  g_assert(n->type != NULL);

  if (mgr)
  {
    return
        (struct notification_impl *)g_hash_table_lookup(mgr->events, n->type );
  }

  return NULL;
}

static DBusHandlerResult
_nsv_notification_dbus_filter_cb(DBusConnection *connection,
                                 DBusMessage *message, void *user_data)
{
  gchar *new_owner = NULL;
  gchar *old_owner = NULL;
  gchar *name = NULL;

  if (nsv_notification_has_events() &&
      dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_SIGNAL &&
      dbus_message_has_member(message, "NameOwnerChanged") &&
      dbus_message_has_interface(message, DBUS_INTERFACE_DBUS) &&
      !dbus_message_get_destination(message) &&
      dbus_message_get_args(message, NULL,
                            DBUS_TYPE_STRING, &name,
                            DBUS_TYPE_STRING, &old_owner,
                            DBUS_TYPE_STRING, &new_owner,
                            DBUS_TYPE_INVALID) &&
      g_str_equal(new_owner, ""))
  {
    nsv_notification_finish_by_sender(name);
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void
nsv_notification_destroy(struct nsv_notification *n)
{
  struct notification_impl *event;
  struct notification_event_status *event_status;

  if (!n)
    return;

  event_status = n->event_status;
  event = get_implementation(n);

  if (n->sender && event->flags & 2 && mgr && mgr->conn)
  {
    gchar *match =
        g_strdup_printf("type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged',arg0='%s'",
                        n->sender);

    dbus_bus_remove_match(mgr->conn, match, NULL);
    g_free(match);
    dbus_connection_remove_filter(
          mgr->conn, _nsv_notification_dbus_filter_cb, NULL);
  }

  if (n->sound_file)
  {
    g_free(n->sound_file);
    n->sound_file = NULL;
  }

  if (n->fallback_sound_file)
  {
    g_free(n->fallback_sound_file);
    n->fallback_sound_file = NULL;
  }

  if (n->vibra_pattern)
  {
    g_free(n->vibra_pattern);
    n->vibra_pattern = NULL;
  }

  if (n->sender)
  {
    g_free(n->sender);
    n->sender = NULL;
  }

  if (event_status->policy)
  {
    g_object_unref(event_status->policy);
    event_status->policy = NULL;
  }

  g_free(event_status);
  g_free(n->type);
  g_free(n);
}

void
nsv_notification_finish(struct nsv_notification *n)
{
  struct notification_event_status *event_status = n->event_status;

  if (event_status->status == STOPPED)
    return;

  if (event_status->status == INITIALIZED)
    n->event_status->stopped = TRUE;
  else
  {
    struct notification_impl *event = get_implementation(n);

    if (event)
    {
      event_status->status = STOPPED;

      if (event_status->playing)
      {
        event->stop(n);
        event_status->playing = FALSE;
      }

      if (event_status->policy)
      {
        sp_timestamp("hildon-plugins-notify-sv: policy: Request stop");
        nsv_policy_stop_permission(event_status->policy);
      }
      else
      {
        if ( mgr->current_notification == n )
          mgr->current_notification = NULL;

        event->shutdown(n);
        nsv_notification_destroy(n);
      }
    }
  }
}

void
nsv_notification_finish_by_sender(const char *sender)
{
  GList *l;

  if (!mgr)
    return;

  if (mgr->current_notification)
  {
    if (get_implementation(mgr->current_notification)->flags & 2 &&
        g_str_equal(mgr->current_notification->sender, sender))
    {
      nsv_notification_finish(mgr->current_notification);
      return;
    }
  }

  for (l = g_queue_peek_head_link(mgr->queue); l; l = l->next)
  {
    struct nsv_notification *n = (struct nsv_notification *)l->data;

    if (get_implementation(n)->flags & 2 &&
        g_str_equal(n->sender, sender))
    {

      g_queue_remove(mgr->queue, n);
      nsv_notification_finish(n);
      return;
    }
  }
}

void
nsv_notification_finish_by_category(const char *category)
{
  GList *l;

  if (!mgr)
    return;

  if (mgr->current_notification)
  {
    if (g_str_equal(mgr->current_notification->type, category))
    {
      nsv_notification_finish(mgr->current_notification);
      return;
    }
  }

  for (l = g_queue_peek_head_link(mgr->queue); l; l = l->next)
  {
    struct nsv_notification *n = (struct nsv_notification *)l->data;

    if (g_str_equal(n->type, category))
    {
      g_queue_remove(mgr->queue, n);
      nsv_notification_finish(n);
    }
  }
}

gboolean
nsv_notification_has_events()
{
  if (mgr)
    return !!mgr->current_notification;

  return FALSE;
}

static void
_nsv_notification_shutdown_finish_cb(gpointer data, gpointer user_data)
{
  nsv_notification_finish((struct nsv_notification *)data);
}

void
nsv_notification_shutdown()
{
  if (!mgr)
    return;

  if (mgr->current_notification &&
      mgr->current_notification->event_status->status != STOPPED)
  {
    nsv_notification_finish(mgr->current_notification);
  }

  g_queue_foreach(mgr->queue, _nsv_notification_shutdown_finish_cb, NULL);
  g_queue_free(mgr->queue);
  g_hash_table_destroy(mgr->events);
  mgr->events = NULL;

  if (mgr->conn)
  {
    dbus_connection_unref(mgr->conn);
    mgr->conn = NULL;
  }

  nsv_policy_mgr_shutdown();
  g_free(mgr);
  mgr = NULL;
}

static gboolean
_nsv_notification_finish_cb(gpointer userdata)
{
  nsv_notification_finish((struct nsv_notification *)userdata);

  return FALSE;
}

void
nsv_notification_error(struct nsv_notification *n)
{
  struct notification_event_status *event_status = n->event_status;

  if (event_status->fallback)
    nsv_notification_finish(n);
  else
  {
    struct notification_impl *event;

    if (!n->fallback_sound_file)
      nsv_notification_finish(n);

    if (n->sound_file)
      g_free(n->sound_file);

    n->sound_file = g_strdup(n->fallback_sound_file);
    event_status->fallback = TRUE;
    event = get_implementation(n);

    if (event_status->playing)
    {
      event->stop(n);
      event_status->playing = FALSE;
    }

    if (event->play(n))
      event_status->playing = TRUE;
    else
      g_idle_add(_nsv_notification_finish_cb, n);
  }
}

void
nsv_notification_register(const char *type, struct notification_impl *event)
{
  if (mgr)
  {
    if (!g_hash_table_lookup(mgr->events, type))
    {
      g_hash_table_insert(mgr->events, g_strdup(type), event);
    }
  }
}

void
nsv_notification_stop(gint id)
{
  GList *l;

  if (!mgr)
    return;

  if (mgr->current_notification && mgr->current_notification->id == id)
  {
    nsv_notification_finish(mgr->current_notification);
    return;
  }

  for (l = g_queue_peek_head_link(mgr->queue); l; l = l->next)
  {
    struct nsv_notification *n = (struct nsv_notification *)l->data;

    if (n->id == id)
    {
      g_queue_remove(mgr->queue, n);
      nsv_notification_finish(n);
      return;
    }
  }
}

/* what is this function for? dumping the queue in debug builds? */
static void
nsv_notification_mgr_queue_for_each()
{
  if (mgr)
  {
    GQueue *queue = mgr->queue;

    if (queue)
    {
      GList *l;

      for (l = g_queue_peek_head_link(queue); l; l = l->next)
        ;
    }
  }
}

static void
_nsv_notification_policy_play_reply_cb(NsvPolicy *policy,
                                       enum pb_state_e granted_state,
                                       nsv_notification *n)
{
  struct notification_event_status *event_status;

  sp_timestamp("hildon-plugins-notify-sv: policy: Play received.");
  event_status = n->event_status;

  if (event_status->stopped)
  {
    event_status->status = UNKNOWN;
    g_idle_add(_nsv_notification_finish_cb, n);
  }
  else
    nsv_notification_mgr_queue_try_next(n, granted_state == PB_STATE_PLAY);
}

static void
_nsv_notification_policy_stop_reply_cb(NsvPolicy *policy,
                                       enum pb_state_e granted_state,
                                       nsv_notification *n)
{
  struct notification_event_status *event_status;
  struct notification_impl *impl;

  sp_timestamp("hildon-plugins-notify-sv: policy: Stop received.");

  event_status = n->event_status;
  g_signal_handlers_disconnect_matched(
        policy, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
        _nsv_notification_policy_stop_reply_cb, n);

  if (mgr->current_notification == n)
    mgr->current_notification = NULL;

  impl = get_implementation(n);
  impl->shutdown(n);
  nsv_notification_destroy(n);
  event_status->status = UNKNOWN;
  nsv_notification_mgr_queue_start_next();
}

static void
_nsv_notification_policy_command_cb(NsvPolicy *policy,
                                    enum pb_state_e req_state,
                                    nsv_notification *n)
{
  struct notification_event_status *event_status = n->event_status;
  struct notification_impl *impl;

  if (req_state == PB_STATE_STOP)
  {
    if (mgr->current_notification)
    {
      impl = get_implementation(n);

      if (event_status->playing)
      {
        impl->stop(n);
        event_status->playing = FALSE;
      }
    }
  }
  else if (req_state == PB_STATE_PLAY && mgr->current_notification &&
           !n->play_granted && g_str_equal(n->type, "Ringtone") )
  {
    impl = get_implementation(n);

    if (event_status->playing)
    {
      impl->stop(n);
      event_status->playing = FALSE;
    }

    n->play_granted = TRUE;

    if (impl->play(n))
      event_status->playing = TRUE;
    else
      g_idle_add(_nsv_notification_finish_cb, n);
  }
}

static gboolean
start_notification(nsv_notification *n)
{
  struct notification_impl *impl;
  struct notification_event_status *event_status;

  g_assert(mgr != NULL);
  g_assert(n != NULL);
  g_assert(n->type != NULL);

  impl = get_implementation(n);

  if (!impl)
    return FALSE;

  mgr->current_notification = n;
  event_status = n->event_status;
  event_status->status = INITIALIZED;

  if (n->sound_enabled || !(impl->flags & 4))
  {
    NsvPolicy *policy = nsv_policy_new(impl->type);

    event_status->policy = policy;
    g_signal_connect(G_OBJECT(policy), "play-reply",
                     G_CALLBACK(_nsv_notification_policy_play_reply_cb), n);
    g_signal_connect(G_OBJECT(policy), "command",
                     G_CALLBACK(_nsv_notification_policy_command_cb), n);
    g_signal_connect(G_OBJECT(policy), "stop-reply",
                     G_CALLBACK(_nsv_notification_policy_stop_reply_cb), n);
    sp_timestamp("hildon-plugins-notify-sv: Requesting play permission.");
    nsv_policy_play_permission(event_status->policy);
  }
  else
    nsv_notification_mgr_queue_try_next(n, TRUE);

  return FALSE;
}

static void
nsv_notification_mgr_queue_start_next()
{
  if (!g_queue_is_empty(mgr->queue))
  {
    gpointer p = g_queue_pop_head(mgr->queue);

    nsv_notification_mgr_queue_for_each();
    g_idle_add((GSourceFunc)start_notification, p);
  }
}

static void
nsv_notification_mgr_queue_try_next(struct nsv_notification *n,
                                    gboolean play_granted)
{
  struct notification_impl *event = get_implementation(n);
  struct notification_event_status *event_status = n->event_status;

  event_status->status = PLAYING;
  n->play_granted = play_granted;

  if (event->play(n))
  {
    event_status->playing = TRUE;
    nsv_notification_mgr_queue_start_next();
  }
  else
  {
    g_idle_add(_nsv_notification_finish_cb, n);
    nsv_notification_mgr_queue_start_next();
  }
}

gint
nsv_notification_start(struct nsv_notification *n)
{
  struct notification_impl *impl;

  g_assert(mgr != NULL);
  g_assert(n != NULL);
  g_assert(n->type != NULL);

  if (mgr->current_notification)
  {
    struct notification_impl *current_impl =
        get_implementation(mgr->current_notification);
    struct notification_impl *new_impl = get_implementation(n);
    int current_prio = current_impl ? current_impl->priority : -1;
    int new_prio = new_impl ? new_impl->priority : -1;

    if (current_prio >= 0)
    {
      if (new_prio < 0)
        goto destroy;

      if (new_impl->flags & 1)
      {
        if (new_prio < current_prio)
          goto destroy;
      }
      else if (new_prio <= current_prio)
        goto destroy;
    }
  }

  impl = get_implementation(n);

  if (!impl || !impl->initialize(n))
    goto destroy;

  mgr->id++;
  n->id = mgr->id;

  if (n->sender && impl->flags & 2 && mgr->conn)
  {
    gchar *match = g_strdup_printf(
          "type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged',arg0='%s'",
          n->sender);

    dbus_bus_add_match(mgr->conn, match, NULL);
    g_free(match);

    dbus_connection_add_filter(mgr->conn,
                               _nsv_notification_dbus_filter_cb, NULL, NULL);
  }

  if (mgr->current_notification)
  {
    g_queue_push_tail(mgr->queue, n);
    nsv_notification_mgr_queue_for_each();
    nsv_notification_finish(mgr->current_notification);
  }
  else
    start_notification(n);

  return n->id;

destroy:
  nsv_notification_destroy(n);
  return -1;
}
