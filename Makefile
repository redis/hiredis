# Hiredis Makefile
# Copyright (C) 2010 Salvatore Sanfilippo <antirez at gmail dot com>
# This file is released under the BSD license, see the COPYING file

OBJ=net.o hiredis.o sds.o async.o
BINS=hiredis-example hiredis-test
LIBNAME=libhiredis

HIREDIS_MAJOR=0
HIREDIS_MINOR=10

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
OPTIMIZATION?=-O3
ifeq ($(uname_S),SunOS)
  CFLAGS?=$(OPTIMIZATION) -fPIC -Wall -W -D__EXTENSIONS__ -D_XPG6 $(ARCH) $(PROF)
  CCLINK?=-ldl -lnsl -lsocket -lm -lpthread
  LDFLAGS?=-L.
  DYLIBSUFFIX=so
  STLIBSUFFIX=a
  DYLIB_MINOR_NAME?=$(LIBNAME).$(DYLIBSUFFIX).$(HIREDIS_MAJOR).$(HIREDIS_MINOR)
  DYLIB_MAJOR_NAME?=$(LIBNAME).$(DYLIBSUFFIX).$(HIREDIS_MAJOR)
  DYLIBNAME?=$(LIBNAME).$(DYLIBSUFFIX)
  DYLIB_MAKE_CMD?=$(CC) -G -o $(DYLIBNAME) -h $(DYLIB_MINOR_NAME)
  STLIBNAME?=$(LIBNAME).$(STLIBSUFFIX)
  STLIB_MAKE_CMD?=ar rcs $(STLIBNAME)
  INSTALL= cp -r
else
ifeq ($(uname_S),Darwin)
  CFLAGS?=$(OPTIMIZATION) -fPIC -Wall -W -Wstrict-prototypes -Wwrite-strings $(ARCH) $(PROF)
  CCLINK?=-lm -pthread
  LDFLAGS?=-L.
  OBJARCH?=-arch i386 -arch x86_64
  DYLIBSUFFIX=dylib
  STLIBSUFFIX=a
  DYLIB_MINOR_NAME?=$(LIBNAME).$(HIREDIS_MAJOR).$(HIREDIS_MINOR).$(DYLIBSUFFIX)
  DYLIB_MAJOR_NAME?=$(LIBNAME).$(HIREDIS_MAJOR).$(DYLIBSUFFIX)
  DYLIBNAME?=$(LIBNAME).$(DYLIBSUFFIX)
  DYLIB_MAKE_CMD?=libtool -dynamic -o $(DYLIBNAME) -install_name $(DYLIB_MINOR_NAME) -lm $(DEBUG) -
  STLIBNAME?=$(LIBNAME).$(STLIBSUFFIX)
  STLIB_MAKE_CMD?=libtool -static -o $(STLIBNAME) -
  INSTALL= cp -a
else
  CFLAGS?=$(OPTIMIZATION) -fPIC -Wall -W -Wstrict-prototypes -Wwrite-strings $(ARCH) $(PROF)
  CCLINK?=-lm -pthread
  LDFLAGS?=-L.
  DYLIBSUFFIX=so
  STLIBSUFFIX=a
  DYLIB_MINOR_NAME?=$(LIBNAME).$(DYLIBSUFFIX).$(HIREDIS_MAJOR).$(HIREDIS_MINOR)
  DYLIB_MAJOR_NAME?=$(LIBNAME).$(DYLIBSUFFIX).$(HIREDIS_MAJOR)
  DYLIBNAME?=$(LIBNAME).$(DYLIBSUFFIX)
  DYLIB_MAKE_CMD?=gcc -shared -Wl,-soname,$(DYLIB_MINOR_NAME) -o $(DYLIBNAME)
  STLIBNAME?=$(LIBNAME).$(STLIBSUFFIX)
  STLIB_MAKE_CMD?=ar rcs $(STLIBNAME)
  INSTALL= cp -a
endif
endif

CCOPT= $(CFLAGS) $(CCLINK)
DEBUG?= -g -ggdb

PREFIX?=/usr/local
INCLUDE_PATH?=include/hiredis
LIBRARY_PATH?=lib
INSTALL_INCLUDE_PATH= $(PREFIX)/$(INCLUDE_PATH)
INSTALL_LIBRARY_PATH= $(PREFIX)/$(LIBRARY_PATH)

all: $(DYLIBNAME) $(BINS)

# Deps (use make dep to generate this)
net.o: net.c fmacros.h net.h hiredis.h
async.o: async.c async.h hiredis.h sds.h dict.c dict.h
example.o: example.c hiredis.h
hiredis.o: hiredis.c fmacros.h hiredis.h net.h sds.h
sds.o: sds.c sds.h
test.o: test.c hiredis.h

$(DYLIBNAME): $(OBJ)
	$(DYLIB_MAKE_CMD) $(OBJ)

$(STLIBNAME): $(OBJ)
	$(STLIB_MAKE_CMD) $(OBJ)

dynamic: $(DYLIBNAME)
static: $(STLIBNAME)

# Binaries:
hiredis-example-libevent: example-libevent.c adapters/libevent.h $(STLIBNAME)
	$(CC) -o $@ $(CCOPT) $(DEBUG) $(LDFLAGS) -levent example-libevent.c $(STLIBNAME)

hiredis-example-libev: example-libev.c adapters/libev.h $(STLIBNAME)
	$(CC) -o $@ $(CCOPT) $(DEBUG) $(LDFLAGS) -lev example-libev.c $(STLIBNAME)

ifndef AE_DIR
hiredis-example-ae:
	@echo "Please specify AE_DIR (e.g. <redis repository>/src)"
	@false
else
hiredis-example-ae: example-ae.c adapters/ae.h $(STLIBNAME)
	$(CC) -o $@ $(CCOPT) $(DEBUG) -I$(AE_DIR) $(LDFLAGS) $(AE_DIR)/ae.o $(AE_DIR)/zmalloc.o example-ae.c $(STLIBNAME)
endif

hiredis-%: %.o $(STLIBNAME)
	$(CC) -o $@ $(CCOPT) $(DEBUG) $(LDFLAGS) $< $(STLIBNAME)

test: hiredis-test
	./hiredis-test

check: hiredis-test
	echo "daemonize yes\n pidfile /tmp/redis-check.pid\n port 56379" \
		| redis-server -
	./hiredis-test -p 56379 || (kill `cat /tmp/redis-check.pid` && false)
	kill `cat /tmp/redis-check.pid`

.c.o:
	$(CC) -std=c99 -pedantic -c $(CFLAGS) $(OBJARCH) $(DEBUG) $(COMPILE_TIME) $<

clean:
	rm -rf $(DYLIBNAME) $(STLIBNAME) $(BINS) hiredis-example* *.o *.gcda *.gcno *.gcov

dep:
	$(CC) -MM *.c

install: $(DYLIBNAME) $(STLIBNAME)
	mkdir -p $(INSTALL_INCLUDE_PATH) $(INSTALL_LIBRARY_PATH)
	$(INSTALL) hiredis.h async.h adapters $(INSTALL_INCLUDE_PATH)
	$(INSTALL) $(DYLIBNAME) $(INSTALL_LIBRARY_PATH)/$(DYLIB_MINOR_NAME)
	cd $(INSTALL_LIBRARY_PATH) && ln -sf $(DYLIB_MINOR_NAME) $(DYLIB_MAJOR_NAME)
	cd $(INSTALL_LIBRARY_PATH) && ln -sf $(DYLIB_MAJOR_NAME) $(DYLIBNAME)
	$(INSTALL) $(STLIBNAME) $(INSTALL_LIBRARY_PATH)


32bit:
	@echo ""
	@echo "WARNING: if it fails under Linux you probably need to install libc6-dev-i386"
	@echo ""
	$(MAKE) ARCH="-m32"

gprof:
	$(MAKE) PROF="-pg"

gcov:
	$(MAKE) PROF="-fprofile-arcs -ftest-coverage"

noopt:
	$(MAKE) OPTIMIZATION=""
