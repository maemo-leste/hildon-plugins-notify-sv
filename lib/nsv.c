#include <glib-object.h>
#include <glib/gstdio.h>
#include <pulse/pulseaudio.h>
#include <X11/Xlib.h>

#include "nsv.h"
#include "nsv-private.h"
#include "nsv-decoder.h"
#include "nsv-notification.h"
#include "nsv-profile.h"
#include "nsv-pulse-context.h"
#include "nsv-system-proxy.h"
#include "nsv-util.h"

#include "ringtone.h"
#include "alarm-clock.h"
#include "alarm-calendar.h"
#include "message-events.h"
#include "system-events.h"

struct nsv
{
  NsvDecoder *decoder;
  Atom mb_capp_atom;
  Window mb_capp_window;
  GIOChannel *dpy_io_chan;
  guint dpy_io_chan_id;
  Display *dpy;
  NsvProfile *profile;
  NsvPulseContext *pulse_context;
  NsvSystemProxy *system_proxy;
};

static struct nsv *nsv = NULL;

gint
nsv_play(const char *category, const char *sound_file, int volume,
         const char *vibra_pattern, gchar *sender, gboolean override)
{
  struct nsv_notification *n;
  int profile_volume;
  const char *tone;
  const char *vibra;
  gchar *decoded;
  const char *fallback_sound;
  gchar *fallback_sound_file;

  if (!nsv)
    return -1;

  n = nsv_notification_new(category);

  if (!n)
    return -1;

  n->sound_file = g_strdup(sound_file);
  n->vibra_pattern = g_strdup(vibra_pattern);
  n->sender = g_strdup(sender);
  n->volume = volume;
  n->sound_enabled = !nsv_profile_is_silent_mode(nsv->profile);
  n->vibra_enabled = nsv_profile_is_vibra_enabled(nsv->profile);

  if (!override)
  {
    fallback_sound = nsv_profile_get_fallback(nsv->profile, category);
    fallback_sound_file =
        nsv_decoder_get_decoded_filename(nsv->decoder, fallback_sound);
    n->fallback_sound_file = g_strdup(fallback_sound_file);
    tone = nsv_profile_get_tone(nsv->profile, category);
    decoded = nsv_decoder_get_decoded_filename(nsv->decoder, tone);

    if (!tone || nsv_util_valid_rootfs_sound_file(tone))
    {
      if (n->sound_file)
        g_free(n->sound_file);

      n->sound_file = g_strdup(tone);
    }
    else
    {
      if (!decoded || !g_file_test(decoded, G_FILE_TEST_EXISTS))
      {
        if (fallback_sound && fallback_sound_file &&
            g_file_test(fallback_sound_file, G_FILE_TEST_EXISTS))
        {
          if (n->sound_file)
            g_free(n->sound_file);

          n->sound_file = g_strdup(fallback_sound_file);
        }

        g_free(fallback_sound_file);
        goto check_decoded;
      }

      if (n->sound_file)
        g_free(n->sound_file);

      n->sound_file = g_strdup(decoded);
    }

    if (!fallback_sound_file)
    {
check_decoded:
      if(decoded)
        g_free(decoded);

      vibra = nsv_profile_get_vibra_pattern(nsv->profile, category);

      if (vibra)
      {
        if (n->vibra_pattern)
          g_free(n->vibra_pattern);

        n->vibra_pattern = g_strdup(vibra);
      }

      goto out;
    }

    g_free(fallback_sound_file);
    goto check_decoded;
  }

out:
  n->vibra_enabled = n->vibra_pattern ? n->vibra_enabled : FALSE;

  profile_volume = nsv_profile_get_volume(nsv->profile, category);
  n->volume = profile_volume < 0 ? volume : profile_volume;

  if (g_str_equal(category, NSV_CATEGORY_SYSTEM) ||
      g_str_equal(category, NSV_CATEGORY_CRITICAL))
  {
    n->volume = nsv_profile_get_system_volume(nsv->profile);
  }

  if (g_str_equal(category, NSV_CATEGORY_CALENDAR))
    n->volume = nsv_profile_get_volume(nsv->profile, NSV_CATEGORY_RINGTONE);

  return nsv_notification_start(n);
}

void
nsv_stop(gint id)
{
  if (id >= 0)
    nsv_notification_stop(id);
}

void
nsv_sv_stop_event(void *plugin, gint id)
{
  nsv_stop(id);
}

static void
nsv_unlink_decoded(struct nsv *self, const char *tone)
{
  gchar *decoded;

  if (!self->decoder || !tone)
    return;

  decoded = nsv_decoder_get_decoded_filename(self->decoder, tone);

  if (decoded)
  {
    g_unlink(decoded);
    g_free(decoded);
  }
}

static gboolean
nsv_is_valid_tone(NsvProfile *self, const char *tone)
{
  GList *l;

  for (l = g_list_first(nsv_profile_get_tone_keys(self)); l; l = l->next)
  {
    const char *profile = (const char *)l->data;

    if (g_str_equal(nsv_profile_get_tone(self, profile), tone))
      return TRUE;

    if (g_str_equal(nsv_profile_get_fallback(self, profile), tone))
      return TRUE;
  }

  return FALSE;
}

static void
_nsv_profile_tone_changed_cb(NsvProfile *self, gchar *category, gchar *tone,
                             char *file)
{
  gchar *decoded;

  if (!nsv->decoder)
    return;

  decoded = nsv_decoder_get_decoded_filename(nsv->decoder, file);

  if (decoded)
  {
    if (!nsv_util_valid_sound_file(decoded))
      nsv_decoder_decode(nsv->decoder, category, file);

    if (!nsv_is_valid_tone(self, tone) )
      nsv_unlink_decoded(nsv, tone);

    g_free(decoded);
  }
  else if (file)
  {
    if (!nsv_is_valid_tone(self, tone))
      nsv_unlink_decoded(nsv, tone);

    if (!nsv_util_valid_rootfs_sound_file(file))
      nsv_decoder_decode(nsv->decoder, category, file);
  }
}

static void
nsv_set_profile_pulse_context_rule_volume(NsvPulseContext *self, int volume)
{
  if (self)
  {
    nsv_pulse_context_set_rule_volume(nsv->pulse_context,
                                      "x-maemo-system-sound", volume);
  }
}

static void
_nsv_profile_system_volume_changed_cb(NsvProfile *self, int volume)
{
  nsv_set_profile_pulse_context_rule_volume(nsv->pulse_context, volume);
}

static void
_nsv_pulse_context_ready_cb(NsvPulseContext *self)
{
  if (nsv->profile)
  {
    nsv_set_profile_pulse_context_rule_volume(
          self, nsv_profile_get_system_volume(nsv->profile));
  }
}

static gboolean
_nsv_unref_system_proxy_cb(gpointer user_data)
{
  if (nsv && nsv->system_proxy)
  {
    g_object_unref(nsv->system_proxy);
    nsv->system_proxy = NULL;
  }

  return FALSE;
}

static void
_nsv_system_proxy_startup_cb()
{
  GList *l;
  const gchar *fallback;

  if (!nsv)
    return;

  for (l = g_list_first(nsv_profile_get_tone_keys(nsv->profile)); l;
       l = l->next)
  {
    const gchar *category = (const gchar *)l->data;
    const char *tone = nsv_profile_get_tone(nsv->profile, category);

    if (tone && !nsv_util_valid_rootfs_sound_file(tone))
    {
      gchar *decoded = nsv_decoder_get_decoded_filename(nsv->decoder, tone);

      if (decoded)
      {
        if (!nsv_util_valid_sound_file(decoded))
        {
          g_unlink(decoded);
          nsv_decoder_decode(nsv->decoder, category, tone);
        }
      }
      else
        nsv_decoder_decode(nsv->decoder, category, tone);

      g_free(decoded);
    }

    fallback = nsv_profile_get_fallback(nsv->profile, category);

    if (fallback && !nsv_util_valid_rootfs_sound_file(fallback))
    {
      gchar *decoded = nsv_decoder_get_decoded_filename(nsv->decoder, fallback);

      if (!decoded || !nsv_util_valid_sound_file(decoded))
        nsv_decoder_decode(nsv->decoder, category, fallback);

      g_free(decoded);
    }
  }

  g_idle_add(_nsv_unref_system_proxy_cb, NULL);
}

gboolean
nsv_initialize()
{
  const char *target_path = "/home/user/.local/share/sounds";

  if (nsv)
    return TRUE;

  nsv = g_new0(struct nsv, 1);

  if (!nsv)
    return FALSE;

  nsv->profile = nsv_profile_get_instance();
  g_signal_connect(G_OBJECT(nsv->profile), "tone-changed",
                   G_CALLBACK(_nsv_profile_tone_changed_cb), NULL);
  g_signal_connect(G_OBJECT(nsv->profile), "system-volume-changed",
                   G_CALLBACK(_nsv_profile_system_volume_changed_cb), NULL);

  nsv->pulse_context = nsv_pulse_context_get_instance();
  g_signal_connect(G_OBJECT(nsv->pulse_context), "ready",
                   G_CALLBACK(_nsv_pulse_context_ready_cb), NULL);

  nsv_notification_init();
  register_ringtone();
  register_alarm_clock();
  register_alarm_calendar();
  register_message_events();
  register_system_events();

  g_mkdir_with_parents(target_path, 0700);

  nsv->decoder = nsv_decoder_new();
  g_object_set(G_OBJECT(nsv->decoder), "target-path", target_path, NULL);

  nsv->system_proxy = nsv_system_proxy_get_instance();

  if (nsv->system_proxy)
  {
    g_signal_connect(G_OBJECT(nsv->system_proxy), "startup-done",
                     G_CALLBACK(_nsv_system_proxy_startup_cb), NULL);
    g_signal_connect(G_OBJECT(nsv->system_proxy), "startup-timeout",
                     G_CALLBACK(_nsv_system_proxy_startup_cb), NULL);
  }

  return TRUE;
}

static gboolean
_nsv_handle_x11(GIOChannel *source, GIOCondition condition, gpointer data)
{
  if (condition & G_IO_IN)
  {
    while (XPending(nsv->dpy))
    {
      XEvent ev;
      Atom type;
      int format;
      unsigned long nitems;
      unsigned long bytes_after;
      unsigned char *prop;

      XNextEvent(nsv->dpy, &ev);

      if (ev.type != PropertyNotify ||
          XGetWindowProperty(nsv->dpy, RootWindow(nsv->dpy,0),
                             nsv->mb_capp_atom, 0, -1, FALSE,AnyPropertyType,
                             &type, &format, &nitems, &bytes_after, &prop)
          != Success)
      {
        continue;
      }

      if (nitems == 1)
      {
        Window window= *(Window *)prop;

        if (nsv->mb_capp_window != window)
        {
          nsv->mb_capp_window = window;

          if (window == -1)
          {
            if (nsv_notification_has_events())
            {
              nsv_notification_finish_by_category(NSV_CATEGORY_SMS);
              nsv_notification_finish_by_category(NSV_CATEGORY_CHAT);
              nsv_notification_finish_by_category(NSV_CATEGORY_EMAIL);
            }
          }
        }
      }

      XFree(prop);
    }
  }

  return TRUE;
}

gboolean nsv_initialize_with_x11(void)
{
  if (!nsv_initialize())
    return FALSE;

  if (!nsv)
    return FALSE;

  if ((nsv->dpy = XOpenDisplay(0)))
  {
    Window window;

    nsv->mb_capp_atom = XInternAtom(nsv->dpy, "_MB_CURRENT_APP_WINDOW", FALSE);

    if ((window = RootWindow(nsv->dpy, 0)) != BadWindow)
    {
      XSelectInput(nsv->dpy, window, StructureNotifyMask | PropertyChangeMask);

      if ((nsv->dpy_io_chan =
           g_io_channel_unix_new(ConnectionNumber(nsv->dpy))))
      {
        nsv->dpy_io_chan_id =
            g_io_add_watch(nsv->dpy_io_chan, G_IO_IN, _nsv_handle_x11, NULL);

        if (nsv->dpy_io_chan_id)
          XFlush(nsv->dpy);
      }
    }
  }

  return TRUE;
}

void
nsv_shutdown()
{
  if (!nsv)
    return;

  g_object_unref(nsv->decoder);
  nsv->decoder = NULL;

  if (nsv->system_proxy)
  {
    g_object_unref(nsv->system_proxy);
    nsv->system_proxy = NULL;
  }

  if (nsv->profile)
  {
    g_object_unref(nsv->profile);
    nsv->profile = NULL;
  }

  if (nsv->pulse_context)
  {
    g_object_unref(nsv->pulse_context);
    nsv->pulse_context = NULL;
  }

  g_free(nsv);
  nsv = NULL;
  nsv_notification_shutdown();
}

gint
nsv_sv_play_event(void *plugin, unsigned int event, const char *sound_file,
                  gboolean sound_enabled, const char *vibra_pattern,
                  gboolean vibra_enabled, int volume)
{
  const char *category;

  switch (event)
  {
    case RINGTONE_EVENT:
      category = NSV_CATEGORY_RINGTONE;
      break;
    case CALENDAR_EVENT:
      category = NSV_CATEGORY_CALENDAR;
      break;
    case CLOCK_EVENT:
      category = NSV_CATEGORY_CLOCK;
      break;
    case CHAT_EVENT:
    case MESSAGE_EVENT:
      category = NSV_CATEGORY_CHAT;
      break;
    case SMS_EVENT:
      category = NSV_CATEGORY_SMS;
      break;
    case EMAIL_EVENT:
      category = NSV_CATEGORY_EMAIL;
      break;
    case SOUND_EVENT:
      category = NSV_CATEGORY_SOUND;
      break;
    default:
      return -1;
  }

  return nsv_play(category, sound_file, volume, vibra_pattern, NULL, FALSE);
}

void
nsv_sv_init(void **sv)
{
  nsv_initialize();
  *sv = g_malloc0(4);
}

void
nsv_sv_shutdown(void *sv)
{
  nsv_shutdown();
  g_free(sv);
}
