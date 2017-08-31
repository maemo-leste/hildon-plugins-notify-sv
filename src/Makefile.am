nsv_decoder_service_PROGRAMS = nsv-decoder-service

nsv_decoder_servicedir = $(prefix)/libexec

nsv_decoder_service_CFLAGS = $(NSV_DECODER_SERVICE_CFLAGS)

nsv_decoder_service_LDFLAGS = $(NSV_DECODER_SERVICE_LIBS)

BUILT_SOURCES =						\
		dbus-glib-marshal-nsv-decoder-service.h	\
		nsv-service-marshal.c			\
		nsv-service-marshal.h

dbus-glib-marshal-nsv-decoder-service.h: nsv-decoder-service.xml
	$(DBUS_BINDING_TOOL) --prefix=nsv_decoder_service --mode=glib-server $< > xgen-$(@F)		\
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ )							\
	&& rm -f xgen-$(@F)

nsv-service-marshal.c: nsv-service-marshal.list
	$(GLIB_GENMARSHAL) --prefix=nsv_service_marshal $< --header --body --internal > xgen-$(@F)	\
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ )							\
	&& rm -f xgen-$(@F)

nsv-service-marshal.h: nsv-service-marshal.list
	$(GLIB_GENMARSHAL) --prefix=nsv_service_marshal $< --header --internal > xgen-$(@F)		\
	&& ( cmp -s xgen-$(@F) $@ || cp xgen-$(@F) $@ )							\
&& rm -f xgen-$(@F)

nsv_decoder_service_SOURCES =		\
		nsv-decoder-service.c	\
		nsv-decoder-task.c	\
		nsv-service-marshal.c

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES = Makefile.in
