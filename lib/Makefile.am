lib_LTLIBRARIES = libhildon-plugins-notify-sv.la

INCLUDES = $(HILDON_PLUGINS_NOTIFY_SV_CFLAGS) -I$(srcdir)/../include

BUILT_SOURCES =					\
		nsv-profile-marshal.c		\
		nsv-profile-marshal.h

libhildon_plugins_notify_sv_la_LDFLAGS = -version-info 0:5:0

libhildon_plugins_notify_sv_la_LIBADD = $(HILDON_PLUGINS_NOTIFY_SV_LIBS)
			
libhildon_plugins_notify_sv_la_SOURCES =	\
			alarm-calendar.c	\
			alarm-clock.c		\
			message-events.c	\
			nsv-decoder.c		\
			nsv-notification.c	\
			nsv-playback.c		\
			nsv-plugin.c		\
			nsv-policy.c		\
			nsv-profile.c		\
			nsv-pulse-context.c	\
			nsv-system-proxy.c	\
			nsv-util.c		\
			nsv.c			\
			ringtone.c		\
			system-events.c		\
			nsv-profile-marshal.c

nsv-profile-marshal.c: nsv-profile-marshal.list
	$(GLIB_GENMARSHAL) --prefix=nsv_profile_marshal $< --header --body --internal > xgen-$(@F)	\
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ )							\
	&& rm -f xgen-$(@F)

nsv-profile-marshal.h: nsv-profile-marshal.list
	$(GLIB_GENMARSHAL) --prefix=nsv_profile_marshal $< --header --internal > xgen-$(@F)		\
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ )							\
	&& rm -f xgen-$(@F)

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES = Makefile.in
