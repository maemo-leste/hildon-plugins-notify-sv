#include <glib-object.h>

#include <stdio.h>

#include "sp_timestamp.h"

#include "nsv-plugin.h"
#include "nsv.h"

void
nsv_plugin_load()
{
  nsv_initialize_with_x11();
}

void
nsv_plugin_unload()
{
  nsv_shutdown();
}

gint
nsv_plugin_play_event(GHashTable *hints, gchar *sender)
{
  const gchar *s;
  const char *category;
  gchar *sound_file;
  const GValue *val;
  gboolean override;
  gint id;
  gchar *vibra_pattern;
  gint volume;

  _sp_timestamp("Notification received.");

  val = (const GValue *)g_hash_table_lookup(hints, "category");

  if (!G_VALUE_HOLDS_STRING(val) || !(s = g_value_get_string(val)))
    goto err_cat;


  if (g_str_equal(s, "system-sound"))
    category = "System";
  else if (g_str_equal(s, "incoming-call"))
    category = "Ringtone";
  else if (g_str_equal(s, "sms-message") ||
           g_str_equal(s, "sms-message-class-0") ||
           g_str_equal(s, "voice-mail"))
  {
    category = "SMS";
  }
  else if (g_str_equal(s, "chat-message") || g_str_equal(s, "auth-request") ||
           g_str_equal(s, "chat-invitation"))
  {
    category = "Chat";
  }
  else if (g_str_equal(s, "email-message"))
    category = "Email";
  else if (g_str_equal(s, "alarm-event"))
  {
    val = g_hash_table_lookup(hints, "alarm-type");

    if (G_VALUE_HOLDS_STRING(val) && (s = g_value_get_string(val)) &&
        g_str_equal(s, "clock"))
    {
      category = "Clock";
    }
    else
      category = "Calendar";
  }
  else if (g_str_equal(s, "system-critical"))
    category = "Critical";
  else if (g_str_equal(s, "play-sound") || g_str_has_prefix(s, "system.note."))
    category = "Sound";
  else
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
