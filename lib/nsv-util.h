#ifndef NSV_UTIL_H
#define NSV_UTIL_H

#include "config.h"

#include "sp_timestamp.h"

void nsv_vibra_start(const char *pattern);
void nsv_vibra_stop(const char *pattern);
void nsv_tone_start(guint event);
void nsv_tone_stop(guint event);
void nsv_knock_start(guint event);
void nsv_knock_stop(guint event);

gboolean nsv_util_valid_sound_file(const char *file);
gboolean nsv_util_valid_rootfs_sound_file(const char *file);

#define _sp_timestamp(s) sp_timestamp(PACKAGE ": " s)

#endif // NSV_UTIL_H
