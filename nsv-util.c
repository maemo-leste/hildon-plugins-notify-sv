#include <glib.h>
#include <dbus/dbus.h>
#include <mce/dbus-names.h>
#include <gio/gio.h>

#include <sndfile.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "nsv-util.h"

static void
nsv_vibra(const char *pattern, gboolean enable)
{
  DBusConnection *dbus;
  const gchar *method;
  DBusMessage *msg;

  if (!(dbus = dbus_bus_get(DBUS_BUS_SYSTEM, NULL)))
      return;

  if (enable)
    method = MCE_ACTIVATE_VIBRATOR_PATTERN;
  else
    method = MCE_DEACTIVATE_VIBRATOR_PATTERN;

  msg = dbus_message_new_method_call(MCE_SERVICE,
                                     MCE_REQUEST_PATH,
                                     MCE_REQUEST_IF,
                                     method);

  if (msg)
  {
    dbus_message_append_args(msg,
                             DBUS_TYPE_STRING, &pattern,
                             DBUS_TYPE_INVALID);
    dbus_connection_send(dbus, msg, NULL);
    dbus_connection_flush(dbus);
    dbus_message_unref(msg);
  }

  dbus_connection_unref(dbus);
}

void
nsv_vibra_start(const char *pattern)
{
  nsv_vibra(pattern, TRUE);
}

void
nsv_vibra_stop(const char *pattern)
{
  nsv_vibra(pattern, FALSE);
}

static void
nsv_tone(gboolean start, guint event)
{
  const gchar *method;
  DBusMessage *msg;
  dbus_int32_t volume = -5;
  dbus_uint32_t dbus_event = event;
  dbus_uint32_t duration = 0;
  DBusConnection *conn = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);

  if (!conn)
    return;

  if (start)
    method = "StartNotificationTone";
  else
    method = "StopTone";

  msg = dbus_message_new_method_call(
        "com.Nokia.Telephony.Tones",
        "/com/Nokia/Telephony/Tones",
        "com.Nokia.Telephony.Tones",
        method);

  if (!msg)
    goto err_msg;

  if (start)
  {
    if (!dbus_message_append_args(msg,
                                  DBUS_TYPE_UINT32, &dbus_event,
                                  DBUS_TYPE_INT32, &volume,
                                  DBUS_TYPE_UINT32, &duration,
                                  DBUS_TYPE_INVALID))
    {
      goto err;
    }
  }

  dbus_connection_send(conn, msg, NULL);

err:
  dbus_message_unref(msg);
err_msg:
  dbus_connection_unref(conn);
}

void
nsv_tone_start(guint event)
{
  nsv_tone(TRUE, event);
}

void
nsv_tone_stop(guint event)
{
  nsv_tone(FALSE, event);
}

static void
nsv_knock(gboolean start, guint event)
{
  const char *method;
  DBusMessage *msg;
  dbus_uint32_t duration = 0;
  dbus_int32_t volume = -5;
  dbus_uint32_t dbus_event = event;
  DBusConnection *conn = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);

  if (!conn)
    return;

  if (start)
    method = "StartEventTone";
  else
    method = "StopTone";

  msg = dbus_message_new_method_call("com.Nokia.Telephony.Tones",
                                     "/com/Nokia/Telephony/Tones",
                                     "com.Nokia.Telephony.Tones",
                                     method);

  if (!msg)
    goto err_msg;

  if (start)
  {
    if (!dbus_message_append_args(msg,
                                  DBUS_TYPE_UINT32, &dbus_event,
                                  DBUS_TYPE_INT32, &volume,
                                  DBUS_TYPE_UINT32, &duration,
                                  DBUS_TYPE_INVALID))
    {
      goto err;
    }
  }

  dbus_connection_send(conn, msg, NULL);

err:
  dbus_message_unref(msg);

err_msg:
  dbus_connection_unref(conn);
}

void
nsv_knock_start(guint event)
{
  nsv_knock(TRUE, event);
}

void
nsv_knock_stop(guint event)
{
  nsv_knock(FALSE, event);
}

gboolean
nsv_util_valid_sound_file(const char *file)
{
  int handle;
  gboolean rv = FALSE;
  SNDFILE *sndfile;
  SF_INFO sfinfo;

  handle = open(file, 0);

  if (handle == -1)
  {
    g_log(0, G_LOG_LEVEL_DEBUG,
          "valid_sound_file: Unable to open file '%s': %s",
          file, strerror(errno));
    return FALSE;
  }

  sndfile = sf_open_fd(handle, 16, &sfinfo, 0);

  if (sndfile)
  {
    int subtype = sfinfo.format & 0xFFFF;

    switch (subtype)
    {
      case SF_FORMAT_PCM_16:
      case SF_FORMAT_PCM_32:
      case SF_FORMAT_PCM_U8:
      case SF_FORMAT_FLOAT:
      case SF_FORMAT_ULAW:
      case SF_FORMAT_ALAW:
        g_log(0, G_LOG_LEVEL_DEBUG,
              "valid_sound_file: File '%s' is valid (format = 0x%X, channels = %d, samplerate = %d",
              file, sfinfo.format, sfinfo.channels, sfinfo.samplerate);
        rv = TRUE;
        break;
      default:
        break;
    }

    sf_close(sndfile);
  }
  else
  {
    g_log(0, G_LOG_LEVEL_DEBUG,
          "valid_sound_file: sndfile failed to open file '%s': %s", file,
          sf_strerror(0));
  }

  close(handle);

  return rv;
}

gboolean
nsv_util_valid_rootfs_sound_file(const char *file)
{
  GFile *f;
  gboolean rv;
  gchar *path;

  if (!file)
    return FALSE;

  f = g_file_new_for_path(file);

  if (!f)
    return FALSE;

  path = g_file_get_path(f);
  rv = g_str_has_prefix(path, "/usr/share/sounds") &&
      g_str_has_suffix(path, "wav") &&
      nsv_util_valid_sound_file(file);
  g_free(path);
  g_object_unref(f);

  return rv;
}
