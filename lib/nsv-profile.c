#include <glib-object.h>
#include <glib/gmem.h>
#include <gconf/gconf-client.h>
#include <libprofile.h>
#include <keys_nokia.h>

#include <malloc.h>

#include "nsv-profile.h"
#include "nsv-profile-marshal.h"

#define NSV_PROFILE_TYPE (nsv_profile_get_type ())
#define NSV_PROFILE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
            NSV_PROFILE_TYPE, NsvProfile))

typedef struct _NsvProfileClass NsvProfileClass;
typedef struct _NsvProfilePrivate NsvProfilePrivate;

struct _NsvProfile
{
  GObject parent_instance;
  NsvProfilePrivate *priv;
};

struct _NsvProfileClass {
  GObjectClass parent_class;
};

struct _NsvProfilePrivate
{
  GHashTable *tones;
  GHashTable *fallbacks;
  GHashTable *volumes;
  GHashTable *vibras;
  gboolean vibrating_alert_enabled;
  guint sound_level;
  gboolean is_silent;
  GConfClient *gc;
};

G_DEFINE_TYPE(NsvProfile, nsv_profile, G_TYPE_OBJECT);

static GObjectClass *parent_class = NULL;
static guint tone_changed_id;
static guint volume_changed_id;
static guint system_volume_changed_id;

struct nsv_profile_tone
{
  const char *key;
  const char *profile_key;
  int type;
  const char *gc_dir;
};

const struct nsv_profile_tone nsv_profile_tone_keys[] =
{
  {NSV_PROFILE_RINGTONE, PROFILEKEY_RINGING_ALERT_TONE, 1, NULL},
  {NSV_PROFILE_CALENDAR, "/apps/calendar/calendar-alarm-tone", 2, "/apps/calendar"},
  {NSV_PROFILE_CLOCK, "/apps/clock/alarm-tone", 2, "/apps/clock"},
  {NSV_PROFILE_SMS, PROFILEKEY_SMS_ALERT_TONE, 1, NULL},
  {NSV_PROFILE_EMAIL, PROFILEKEY_EMAIL_ALERT_TONE, 1, NULL},
  {NSV_PROFILE_CHAT, PROFILEKEY_IM_ALERT_TONE, 1, NULL}
};

struct nsv_profile_volume
{
  const char *name;
  const char *profile_key;
  int type;
  const char *dir;
};

const struct nsv_profile_volume nsv_profile_volume_keys[] =
{
  {NSV_PROFILE_RINGTONE, PROFILEKEY_RINGING_ALERT_VOLUME, 1, NULL},
  {NSV_PROFILE_CALENDAR, NULL, 0, NULL},
  {NSV_PROFILE_CLOCK, NULL, 0, NULL},
  {NSV_PROFILE_SMS, PROFILEKEY_SMS_ALERT_VOLUME, 1, NULL},
  {NSV_PROFILE_EMAIL, PROFILEKEY_EMAIL_ALERT_VOLUME, 1, NULL},
  {NSV_PROFILE_CHAT, PROFILEKEY_IM_ALERT_VOLUME, 1, NULL}
};

struct nsv_profile_vibra
{
  const char *name;
  const char *pattern;
};

const struct nsv_profile_vibra nsv_profile_vibra_values[] =
{
  {NSV_PROFILE_RINGTONE, "PatternIncomingCall"},
  {NSV_PROFILE_CALENDAR, "PatternIncomingCall"},
  {NSV_PROFILE_CLOCK, "PatternIncomingCall"},
  {NSV_PROFILE_SMS, "PatternIncomingMessage"},
  {NSV_PROFILE_EMAIL, "PatternChatAndEmail"},
  {NSV_PROFILE_CHAT, "PatternChatAndEmail"}
};

struct nsv_profile_tone_fallback
{
  const char *name;
  const char *profile_key;
  int type;
  const char *sound_file;
};

const struct nsv_profile_tone_fallback nsv_profile_tone_fallbacks[] =
{
  {NSV_PROFILE_RINGTONE, PROFILEKEY_RINGING_ALERT_TONE, 1, "fallback"},
  {NSV_PROFILE_CALENDAR, NULL, 2, "/usr/share/sounds/ui-calendar_alarm_default.aac"},
  {NSV_PROFILE_CLOCK, NULL, 2, "/usr/share/sounds/ui-clock_alarm_default.aac"},
  {NSV_PROFILE_SMS, PROFILEKEY_SMS_ALERT_TONE, 1, "fallback"},
  {NSV_PROFILE_EMAIL, PROFILEKEY_EMAIL_ALERT_TONE, 1, "fallback"},
  {NSV_PROFILE_CHAT , PROFILEKEY_IM_ALERT_TONE, 1, "fallback"}
};

static void
_profile_track_profile_fn_data_cb(const char *profile, void *user_data)
{
  NSV_PROFILE(user_data)->priv->is_silent = g_str_equal(profile, "silent");
}

static void
_nsv_profile_emit_volume_changed(NsvProfile *self, const char *key, int volume)
{
  int i = 0;

  NsvProfilePrivate *priv = self->priv;

  for (i = 0; i < G_N_ELEMENTS(nsv_profile_volume_keys); i++)
  {
    const struct nsv_profile_volume *v = &nsv_profile_volume_keys[i];

    if (!v->profile_key || !g_str_equal(v->profile_key, key))
        continue;

    if (GPOINTER_TO_INT(g_hash_table_lookup(priv->volumes, v->name)) == volume)
      break;

    g_hash_table_replace(priv->volumes, g_strdup(v->name),
                         GINT_TO_POINTER(volume));
    g_signal_emit(self, volume_changed_id, 0, v->name, volume);
  }
}

static void
_nsv_profile_emit_tone_changed(NsvProfile *self, const char *key,
                               const char *val)
{
  NsvProfilePrivate *priv = self->priv;
  int i;

  for (i = 0; i < G_N_ELEMENTS(nsv_profile_tone_keys); i++)
  {
    const struct nsv_profile_tone *t = &nsv_profile_tone_keys[i];
    const gchar *tone;

    if (!t->profile_key || !g_str_equal(t->profile_key, key))
      continue;

    tone = (const gchar *)g_hash_table_lookup(priv->tones, t->key);

    if (g_str_equal(tone, val))
      break;

    g_hash_table_replace(priv->tones, g_strdup(t->key), g_strdup(val));
    g_signal_emit(self, tone_changed_id, 0, t->key, tone, val);
  }
}

static void
_profile_track_value_fn_data_cb(const char *profile, const char *key,
                                const char *val, const char *type,
                                void *user_data)
{
  NsvProfile *self = NSV_PROFILE(user_data);
  NsvProfilePrivate *priv = self->priv;

  if (g_str_equal(key, PROFILEKEY_VIBRATING_ALERT_ENABLED))
    priv->vibrating_alert_enabled = profile_parse_bool(val);
  else if (g_str_equal(key, PROFILEKEY_SYSTEM_SOUND_LEVEL))
  {
    int sys_snd_lvl = profile_parse_int(val);
    int old_snd_lvl = priv->sound_level;

    if (sys_snd_lvl <= 1)
    {
      if (sys_snd_lvl == 1)
        priv->sound_level = 40;
      else
        priv->sound_level = 0;
    }
    else
      priv->sound_level = 70;

    if (priv->sound_level != old_snd_lvl)
      g_signal_emit(self, system_volume_changed_id, 0, priv->sound_level);
  }
  else if (!g_str_equal(profile, "silent") )
  {
    int i;
    int v = profile_parse_int(val);

    for (i = 0; i < G_N_ELEMENTS(nsv_profile_tone_keys); i++)
      _nsv_profile_emit_tone_changed(self, key, val);

    for (i = 0; i < G_N_ELEMENTS(nsv_profile_volume_keys); i++)
      _nsv_profile_emit_volume_changed(self, key, v);
  }
}

static void
nsv_profile_finalize(GObject *object)
{
  NsvProfile *self = NSV_PROFILE(object);
  NsvProfilePrivate *priv = self->priv;

  profile_track_remove_active_cb(_profile_track_value_fn_data_cb, self);
  profile_track_remove_profile_cb(_profile_track_profile_fn_data_cb, self);
  profile_tracker_quit();

  if (priv)
  {
    if (priv->gc)
    {
      g_object_unref(priv->gc);
      priv->gc = NULL;
    }

    if (priv->tones)
    {
      g_hash_table_destroy(priv->tones);
      priv->tones = NULL;
    }

    if (priv->fallbacks)
    {
      g_hash_table_destroy(priv->fallbacks);
      priv->fallbacks = 0;
    }

    if (priv->volumes)
    {
      g_hash_table_destroy(priv->volumes);
      priv->volumes = NULL;
    }

    if (priv->vibras)
    {
      g_hash_table_destroy(priv->vibras);
      priv->tones = 0;
    }

    g_free(priv);
    self->priv = NULL;
  }

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
nsv_profile_class_init(NsvProfileClass *klass)
{
  parent_class = g_type_class_peek_parent(klass);
  G_OBJECT_CLASS(klass)->finalize = nsv_profile_finalize;

  tone_changed_id =
      g_signal_new("tone-changed",
                   G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                   0, NULL, NULL,
                   nsv_profile_marshal_VOID__STRING_STRING_STRING,
                   G_TYPE_NONE, 3,
                   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
  volume_changed_id =
      g_signal_new("volume-changed",
                   G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                   0, NULL, NULL,
                   nsv_profile_marshal_VOID__STRING_INT,
                   G_TYPE_NONE,
                   2,
                   G_TYPE_STRING, G_TYPE_INT);
  system_volume_changed_id =
      g_signal_new("system-volume-changed",
                   G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                   0, NULL, NULL,
                   g_cclosure_marshal_VOID__INT,
                   G_TYPE_NONE,
                   1,
                   G_TYPE_INT);
}

static void
_nsv_volume_key_changed_cb(GConfClient *client, guint cnxn_id,
                           GConfEntry *entry, gpointer user_data)
{
  GConfValue *value;
  int i;
  NsvProfile *self = NSV_PROFILE(user_data);

  value = gconf_entry_get_value(entry);

  if (value)
  {
    const char *key = gconf_entry_get_key(entry);
    int volume = gconf_value_get_int(value);

    for (i = 0; i < G_N_ELEMENTS(nsv_profile_volume_keys); i++)
      _nsv_profile_emit_volume_changed(self, key, volume);
  }
}

static void
_nsv_tone_key_changed_cb(GConfClient *client, guint cnxn_id, GConfEntry *entry,
                         gpointer user_data)
{
  GConfValue *value;
  int i;
  NsvProfile *self = NSV_PROFILE(user_data);

  value = gconf_entry_get_value(entry);

  if (value)
  {
    const char *key = gconf_entry_get_key(entry);
    const char *tone = gconf_value_get_string(value);

    for (i = 0; i < G_N_ELEMENTS(nsv_profile_tone_keys); i++)
      _nsv_profile_emit_tone_changed(self, key, tone);
  }
}

static void
nsv_profile_init(NsvProfile *self)
{
  NsvProfilePrivate *priv;
  char *profile;
  int system_sound_level;
  int i;

  priv = g_new0(NsvProfilePrivate, 1);

  if (priv)
  {
    self->priv = priv;

    priv->tones =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    priv->fallbacks =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    priv->volumes =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    priv->vibras =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    priv->gc = gconf_client_get_default();

    profile = profile_get_profile();
    priv->is_silent = g_str_equal(profile, "silent");
    free(profile);

    priv->vibrating_alert_enabled =
        profile_get_value_as_bool(NULL, PROFILEKEY_VIBRATING_ALERT_ENABLED);
    system_sound_level =
        profile_get_value_as_int(NULL, PROFILEKEY_SYSTEM_SOUND_LEVEL);

    if (system_sound_level <= 1)
    {
      if (system_sound_level == 1)
        priv->sound_level = 50;
      else
        priv->sound_level = 0;
    }
    else
      priv->sound_level = 80;

    for (i = 0; i < G_N_ELEMENTS(nsv_profile_tone_keys); i++)
    {
      const struct nsv_profile_tone *tone = &nsv_profile_tone_keys[i];

      if (tone->type == 1)
      {
        char *s = profile_get_value(0, tone->profile_key);

        g_hash_table_replace(priv->tones, g_strdup(tone->key), g_strdup(s));
        free(s);
      }
      else if (tone->type == 2)
      {
        char *s;

        gconf_client_add_dir(priv->gc, tone->gc_dir, GCONF_CLIENT_PRELOAD_NONE,
                             NULL);
        gconf_client_notify_add(priv->gc, tone->profile_key,
                                _nsv_tone_key_changed_cb, self, NULL, NULL);
        s = gconf_client_get_string(priv->gc, tone->profile_key, NULL);

        if (s)
        {
          /* FIXME - no need to g_strdup(s) and then g_free(s) */
          g_hash_table_replace(priv->tones, g_strdup(tone->key), g_strdup(s));
          g_free(s);
        }
      }
    }

    for (i = 0; i < G_N_ELEMENTS(nsv_profile_tone_fallbacks); i++)
    {
      const struct nsv_profile_tone_fallback *fallback =
          &nsv_profile_tone_fallbacks[i];

      if (fallback->type == 1)
      {
        char *s = profile_get_value("fallback", fallback->profile_key);

        g_hash_table_replace(priv->fallbacks, g_strdup(fallback->name),
                             g_strdup(s));
        free(s);
      }
      else if (fallback->type == 2)
      {
        g_hash_table_replace(priv->fallbacks, g_strdup(fallback->name),
                             g_strdup(fallback->sound_file));
      }
    }

    for (i = 0; i < G_N_ELEMENTS(nsv_profile_volume_keys); i++)
    {
      const struct nsv_profile_volume *volume = &nsv_profile_volume_keys[i];

      if (volume->type == 1)
      {
        int v = profile_get_value_as_int(NULL, volume->profile_key);

        g_hash_table_replace(priv->volumes, g_strdup(volume->name),
                             GINT_TO_POINTER(v));
      }
      else if (volume->type == 2)
      {
        gconf_client_add_dir(priv->gc, volume->dir, GCONF_CLIENT_PRELOAD_NONE,
                             NULL);
        gconf_client_notify_add(priv->gc, volume->profile_key,
                                _nsv_volume_key_changed_cb, self, NULL, NULL);
        g_hash_table_replace(
              priv->tones, g_strdup(volume->name),
              GINT_TO_POINTER(gconf_client_get_int(priv->gc,
                                                   volume->profile_key, NULL)));
      }
    }

    for (i = 0; i < G_N_ELEMENTS(nsv_profile_vibra_values); i++)
    {
      const struct nsv_profile_vibra *vibra = &nsv_profile_vibra_values[i];

      g_hash_table_replace(priv->vibras, g_strdup(vibra->name),
                           g_strdup(vibra->pattern));
    }

    profile_track_add_active_cb(_profile_track_value_fn_data_cb, self, 0);
    profile_track_add_profile_cb(_profile_track_profile_fn_data_cb, self, 0);
    profile_tracker_init();
  }
}

GList *
nsv_profile_tones_get_all(NsvProfile *self)
{
  GList *l = NULL;
  GHashTableIter iter;
  gpointer data;
  gpointer key;

  g_hash_table_iter_init(&iter, self->priv->tones);

  while (g_hash_table_iter_next(&iter, &key, &data))
    l = g_list_append(l, data);

  return l;
}

const char *
nsv_profile_get_vibra_pattern(NsvProfile *self, const char *profile)
{
  return (const char *)g_hash_table_lookup(self->priv->vibras, profile);
}

const char *
nsv_profile_get_fallback(NsvProfile *self, const char *profile)
{
  return (const char *)g_hash_table_lookup(self->priv->fallbacks, profile);
}

const char *
nsv_profile_get_tone(NsvProfile *self, const char *profile)
{
  return (const char *)g_hash_table_lookup(self->priv->tones, profile);
}

int
nsv_profile_get_volume(NsvProfile *self, const char *profile)
{
  gpointer v;

  if (g_hash_table_lookup_extended(self->priv->volumes, profile, NULL, &v))
  {
    return GPOINTER_TO_INT(v);
  }

  return -1;
}

int
nsv_profile_get_system_volume(NsvProfile *self)
{
  return self->priv->sound_level;
}

gboolean
nsv_profile_is_silent_mode(NsvProfile *self)
{
  return self->priv->is_silent;
}

gboolean
nsv_profile_is_vibra_enabled(NsvProfile *self)
{
  return self->priv->vibrating_alert_enabled;
}

GList *nsv_profile_get_volume_keys(NsvProfile *self)
{
  GList *l = NULL;
  int i;

  for (i = 0; i < G_N_ELEMENTS(nsv_profile_volume_keys); i++)
    l = g_list_append(l, (gpointer)nsv_profile_volume_keys[i].name);

  return l;
}

GList *nsv_profile_get_tone_keys(NsvProfile *self)
{
  GList *l = NULL;
  int i;

  for (i = 0; i < G_N_ELEMENTS(nsv_profile_tone_keys); i++)
    l = g_list_append(l, (gpointer)nsv_profile_tone_keys[i].key);

  return l;
}

void
nsv_profile_set_tone(NsvProfile *self, const char *profile, char *val)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS(nsv_profile_tone_keys); i++)
  {
    const struct nsv_profile_tone *tone = &nsv_profile_tone_keys[i];

    if (g_str_equal(tone->key, profile))
    {
      profile_set_value(NULL, tone->profile_key, val);
      break;
    }
  }
}

NsvProfile *
nsv_profile_get_instance()
{
  /* FIXME - not thread safe */
  static NsvProfile *instance = NULL;

  if (!instance)
    instance = (NsvProfile *)g_object_new(NSV_PROFILE_TYPE, NULL);

  return instance;
}
