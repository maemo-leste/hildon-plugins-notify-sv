#ifndef NSV_PULSE_CONTEXT_H
#define NSV_PULSE_CONTEXT_H

typedef struct _NsvPulseContext NsvPulseContext;

gboolean nsv_pulse_context_is_ready(NsvPulseContext *self);
void nsv_pulse_context_set_rule_volume(NsvPulseContext *self, const char *rule, int volume);
pa_context *nsv_pulse_context_get_context(NsvPulseContext *self);
NsvPulseContext *nsv_pulse_context_get_instance();

#endif // NSV_PULSE_CONTEXT_H
