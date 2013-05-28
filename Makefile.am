ACLOCAL_AMFLAGS = -I m4
AM_CFLAGS=-Wall -W -Wstrict-prototypes -Wwrite-strings -std=c99 -pedantic

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = hiredis.pc

HIREDIS_VERSION=0:11:0

lib_LTLIBRARIES = libhiredis-1.0.la
libhiredis_1_0_la_SOURCES = \
	net.h net.c \
	hiredis.h hiredis.c \
	sds.h sds.c \
	async.h async.c

libhiredis_1_0_la_LDFLAGS = -version-info $(HIREDIS_VERSION)
#libhiredis_1_0_la_CPPFLAGS = $(DEPS_CFLAGS)
#libhiredis_1_0_la_LIBADD = $(DEPS_LIBS)

include_HEADERS = hiredis.h

noinst_PROGRAMS = hiredis-test hiredis-example

if USE_LIBEV
noinst_PROGRAMS += hiredis-example-libev
hiredis_example_libev_SOURCES = example-libev.c adapters/libev.h
hiredis_example_libev_CPPFLAGS = $(LIBEV_CFLAGS)
hiredis_example_libev_LDADD = libhiredis-1.0.la $(LIBEV_LIBS)
endif

if USE_LIBEVENT
noinst_PROGRAMS += hiredis-example-libevent
hiredis_example_libevent_SOURCES = example-libevent.c adapters/libevent.h
hiredis_example_libevent_CPPFLAGS = $(LIBEVENT_CFLAGS)
hiredis_example_libevent_LDADD = libhiredis-1.0.la $(LIBEVENT_LIBS)
endif

hiredis_example_SOURCES = example.c
hiredis_example_LDADD = libhiredis-1.0.la

hiredis_test_SOURCES = test.c
hiredis_test_LDADD = libhiredis-1.0.la

