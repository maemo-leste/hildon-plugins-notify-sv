#ifndef NSV_PLUGIN_H
#define NSV_PLUGIN_H

void nsv_plugin_load();
void nsv_plugin_unload();

gint nsv_plugin_play_event(GHashTable *hints, gchar *sender);
void nsv_plugin_stop_event(gint id);

#endif // NSVPLUGIN_H
