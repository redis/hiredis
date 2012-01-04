# Hiredis Makefile
# Copyright (C) 2010-2011 Salvatore Sanfilippo <antirez at gmail dot com>
# Copyright (C) 2010-2011 Pieter Noordhuis <pcnoordhuis at gmail dot com>
# This file is released under the BSD license, see the COPYING file

include ./Makefile.common

OBJ=net.o hiredis.o sds.o async.o parser.o object.o handle.o format.o
BINS=hiredis-example hiredis-test

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
	$(CC) -o $@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -levent example-libevent.c $(STLIBNAME)

hiredis-example-libev: example-libev.c adapters/libev.h $(STLIBNAME)
	$(CC) -o $@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -lev example-libev.c $(STLIBNAME)

ifndef AE_DIR
hiredis-example-ae:
	@echo "Please specify AE_DIR (e.g. <redis repository>/src)"
	@false
else
hiredis-example-ae: example-ae.c adapters/ae.h $(STLIBNAME)
	$(CC) -o $@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I$(AE_DIR) $(AE_DIR)/ae.o $(AE_DIR)/zmalloc.o example-ae.c $(STLIBNAME)
endif

hiredis-%: %.o $(STLIBNAME)
	$(CC) -o $@ $(REAL_LDFLAGS) $< $(STLIBNAME)

test: hiredis-test
	./hiredis-test

check: hiredis-test
	echo \
		"daemonize yes\n" \
		"pidfile /tmp/hiredis-test-redis.pid\n" \
		"port 56379\n" \
		"bind 127.0.0.1\n" \
		"unixsocket /tmp/hiredis-test-redis.sock" \
			| redis-server -
	./hiredis-test -h 127.0.0.1 -p 56379 -s /tmp/hiredis-test-redis.sock || \
			( kill `cat /tmp/hiredis-test-redis.pid` && false )
	kill `cat /tmp/hiredis-test-redis.pid`

.c.o:
	$(CC) -std=c99 -pedantic -c $(OPTIMIZATION) $(REAL_CFLAGS) $<

clean:
	rm -rf $(DYLIBNAME) $(STLIBNAME) $(BINS) hiredis-example* *.o *.gcda *.gcno *.gcov test/*.o

dep:
	$(CC) -MM *.c

# Installation related variables and target
PREFIX?=/usr/local
INCLUDE_PATH?=include/hiredis
LIBRARY_PATH?=lib
INSTALL_INCLUDE_PATH= $(PREFIX)/$(INCLUDE_PATH)
INSTALL_LIBRARY_PATH= $(PREFIX)/$(LIBRARY_PATH)

ifeq ($(uname_S),SunOS)
  INSTALL?= cp -r
endif

INSTALL?= cp -a

install: $(DYLIBNAME) $(STLIBNAME)
	mkdir -p $(INSTALL_INCLUDE_PATH) $(INSTALL_LIBRARY_PATH)
	$(INSTALL) hiredis.h async.h adapters $(INSTALL_INCLUDE_PATH)
	$(INSTALL) $(DYLIBNAME) $(INSTALL_LIBRARY_PATH)/$(DYLIB_MINOR_NAME)
	cd $(INSTALL_LIBRARY_PATH) && ln -sf $(DYLIB_MINOR_NAME) $(DYLIB_MAJOR_NAME)
	cd $(INSTALL_LIBRARY_PATH) && ln -sf $(DYLIB_MAJOR_NAME) $(DYLIBNAME)
	$(INSTALL) $(STLIBNAME) $(INSTALL_LIBRARY_PATH)

32bit:
	@echo ""
	@echo "WARNING: if this fails under Linux you probably need to install libc6-dev-i386"
	@echo ""
	$(MAKE) CFLAGS="-m32" LDFLAGS="-m32"

gprof:
	$(MAKE) CFLAGS="-pg" LDFLAGS="-pg"

gcov:
	$(MAKE) CFLAGS="-fprofile-arcs -ftest-coverage" LDFLAGS="-fprofile-arcs"

coverage: gcov
	make check
	mkdir -p tmp/lcov
	lcov -d . -c -o tmp/lcov/hiredis.info
	genhtml --legend -o tmp/lcov/report tmp/lcov/hiredis.info

noopt:
	$(MAKE) OPTIMIZATION=""

.PHONY: all test check clean dep install 32bit gprof gcov noopt
