PACKAGES:=glib-2.0 gconf-2.0 profile gstreamer-0.10 gstreamer-controller-0.10 mce sndfile gio-2.0 libplayback-1 pulsecore libpulse libpulse-mainloop-glib x11 sndfile

MARSHALLERS=nsv-profile nsv-service
MARSHALLERS_C=$(addsuffix -marshal.c, $(MARSHALLERS))
MARSHALLERS_H=$(addsuffix -marshal.h, $(MARSHALLERS))

all: $(MARSHALLERS_C) $(MARSHALLERS_H) nsv-decoder-service libhildon-plugins-notify-sv.so
#-Wl,--no-undefined
libhildon-plugins-notify-sv.so: $(MARSHALLERS_C) \
				nsv-profile.c \
				nsv-decoder.c \
				nsv-system-proxy.c \
				nsv-util.c \
				nsv-policy.c \
				nsv-pulse-context.c \
				nsv-notification.c \
				nsv-playback.c \
				nsv-plugin.c \
				nsv.c \
				ringtone.c \
				alarm-clock.c \
				alarm-calendar.c \
				message-events.c \
				system-events.c
	$(CC) $(CFLAGS) -fPIC -Wall -shared -Wl,--no-undefined $(shell pkg-config --cflags $(PACKAGES)) $^ -o $@ \
		$(shell pkg-config --libs $(PACKAGES)) \
		-DSYSTEMUI_VERSION="0.3.0.0" \
		-DSYSTEMUI_PACKAGE="osso-systemui" \
		-DSP_TIMESTAMP_CREATE=1

nsv-decoder-service: dbus-glib-marshal-nsv-decoder-service.h
	$(CC) $(CFLAGS) $(shell pkg-config --cflags $(PACKAGES)) nsv-service-marshal.c nsv-decoder-service.c nsv-decoder-task.c -o $@ \
		$(shell pkg-config --libs $(PACKAGES)) \

%-marshal.h: %-marshal.list
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.h

%-marshal.c: %-marshal.list
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-marshal.c

dbus-glib-marshal-nsv-decoder-service.h:
	dbus-binding-tool --mode=glib-server --prefix=nsv_decoder_service nsv-decoder-service.xml > dbus-glib-marshal-nsv-decoder-service.h

clean:
	$(RM) *.o nsv-decoder-service libhildon-plugins-notify-sv.so *-marshal.c *-marshal.h dbus-glib-marshal-*.h

install:
	install -m 755 libhildon-plugins-notify-sv.so "$(DESTDIR)/usr/lib/

