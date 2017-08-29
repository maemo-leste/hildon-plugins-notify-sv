#ifndef NSV_POLICY_H
#define NSV_POLICY_H

typedef struct _NsvPolicy NsvPolicy;

NsvPolicy *nsv_policy_new(const char *policy_class);

gboolean nsv_policy_play_permission(NsvPolicy *self);
gboolean nsv_policy_stop_permission(NsvPolicy *self);

gboolean nsv_policy_mgr_init();
gboolean nsv_policy_mgr_shutdown();

#endif // NSV_POLICY_H
