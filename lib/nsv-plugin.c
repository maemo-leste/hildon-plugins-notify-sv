#include <glib-object.h>

#include <stdio.h>

#include "nsv.h"
#include "nsv-plugin.h"
#include "nsv-util.h"

GHashTable *categories = NULL;

void
nsv_plugin_load()
{
  char *category;

  nsv_initialize_with_x11();

  categories = g_hash_table_new(g_str_hash, g_str_equal);

  category = "System";
  g_hash_table_insert(categories, "system-sound", category);

  category = "Ringtone";
  g_hash_table_insert(categories, "incoming-call", category);

  category = "SMS";
  g_hash_table_insert(categories, "sms-message", category);
  g_hash_table_insert(categories, "sms-message-class-0", category);
  g_hash_table_insert(categories, "voice-mail", category);

  category = "Chat";
  g_hash_table_insert(categories, "chat-message", category);
  g_hash_table_insert(categories, "auth-request", category);
  g_hash_table_insert(categories, "chat-invitation", category);
  g_hash_table_insert(categories, "im", category);
  g_hash_table_insert(categories, "im.received", category);
  g_hash_table_insert(categories, "im.error", category);

  category = "Email";
  g_hash_table_insert(categories, "email-message", category);
  g_hash_table_insert(categories, "email", category);
  g_hash_table_insert(categories, "email.arrived", category);
  g_hash_table_insert(categories, "email.bounced", category);

  category = "Critical";
  g_hash_table_insert(categories, "system-critical", category);

  category = "Sound";
  g_hash_table_insert(categories, "play-sound", category);
}

void
nsv_plugin_unload()
{
  g_hash_table_destroy(categories);
  nsv_shutdown();
}

static const char *
nsv_plugin_get_category(GHashTable *hints)
{
  const char *category;
  const gchar *nc;
  const GValue *val = (const GValue *)g_hash_table_lookup(hints, "category");

  if (!G_VALUE_HOLDS_STRING(val) || !(nc = g_value_get_string(val)))
    return NULL;

  if (g_str_has_prefix(nc, "system.note."))
    category = "Sound";
  else if (g_str_equal(nc, "alarm-event"))
  {
    val = g_hash_table_lookup(hints, "alarm-type");

    if (G_VALUE_HOLDS_STRING(val) && (nc = g_value_get_string(val)) &&
        g_str_equal(nc, "clock"))
    {
      category = "Clock";
    }
    else
      category = "Calendar";
  }
  else
  {
    category = g_hash_table_lookup(categories, nc);
  }

  return category;
}

gint
nsv_plugin_play_event(GHashTable *hints, gchar *sender)
{
  const char *category;
  gchar *sound_file;
  const GValue *val;
  gboolean override;
  gint id;
  gchar *vibra_pattern;
  gint volume;

  _sp_timestamp("Notification received.");

  category = nsv_plugin_get_category(hints);

  if (category == NULL)
    goto err_cat;

  val = (const GValue *)g_hash_table_lookup(hints, "sound-file");

  if (val)
    sound_file = g_value_dup_string(val);
  else
    sound_file = NULL;

  val = (const GValue *)g_hash_table_lookup(hints, "volume");

  if (val)
    volume = g_value_get_int(val);
  else
    volume = 100;

  val = (const GValue *)g_hash_table_lookup(hints, "vibra");

  if (val)
    vibra_pattern = g_value_dup_string(val);
  else
    vibra_pattern = NULL;

  val = (const GValue *)g_hash_table_lookup(hints, "override");

  if (val)
    override = g_value_get_boolean(val);
  else
    override = FALSE;

  id = nsv_play(category, sound_file, volume, vibra_pattern, sender, override);

  if (sound_file)
    g_free(sound_file);

  if (vibra_pattern)
    g_free(vibra_pattern);

  return id;

err_cat:
  g_debug("No category for notification, no need to do anything.");

  return -1;
}

void
nsv_plugin_stop_event(gint id)
{
  nsv_stop(id);
}
