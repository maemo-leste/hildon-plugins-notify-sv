AC_INIT([hildon-plugins-notify-sv], [0.50])
AC_CANONICAL_SYSTEM

AM_CONFIG_HEADER(config.h)

AM_MAINTAINER_MODE

AM_INIT_AUTOMAKE

AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_LIBTOOL

AC_HEADER_STDC

AC_PATH_X
AC_PATH_XTRA
AC_SUBST(X_CFLAGS)

AM_GCONF_SOURCE_2
AC_PATH_PROG(GCONFTOOL, gconftool-2)

PKG_PROG_PKG_CONFIG

#+++++++++++++++
# Misc programs 
#+++++++++++++++

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
AC_PATH_PROG(DBUS_BINDING_TOOL, dbus-binding-tool)

AC_SUBST(CFLAGS)

PKG_CHECK_MODULES(NSV_DECODER_SERVICE,
			[glib-2.0 dnl
			dbus-glib-1 dnl
			gstreamer-1.0 dnl
			gstreamer-controller-1.0])

AC_SUBST(NSV_DECODER_SERVICE_LIBS)
AC_SUBST(NSV_DECODER_SERVICE_CFLAGS)

PKG_CHECK_MODULES(HILDON_PLUGINS_NOTIFY_SV,
			[glib-2.0 dnl
			dbus-glib-1 dnl
			gconf-2.0 dnl
			profile dnl
			mce dnl
			sndfile dnl
			gio-2.0 dnl
			libplayback-1 dnl
			libpulse dnl
			libpulse-mainloop-glib dnl
			x11])

AC_SUBST(HILDON_PLUGINS_NOTIFY_SV_LIBS)
AC_SUBST(HILDON_PLUGINS_NOTIFY_SV_CFLAGS)

#+++++++++++++++++++
# Directories setup
#+++++++++++++++++++

hildondesktoplibdir=`pkg-config libhildondesktop-1 --variable=hildondesktoplibdir`
AC_SUBST(hildondesktoplibdir)

AC_OUTPUT([
Makefile
src/Makefile
lib/Makefile
com.nokia.NsvDecoder.service
hildon-plugins-notify-sv.pc
])

