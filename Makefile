# Hiredis Makefile
# Copyright (C) 2010 Salvatore Sanfilippo <antirez at gmail dot com>
# This file is released under the BSD license, see the COPYING file

OBJ = anet.o hiredis.o sds.o
BINS = hiredis-example hiredis-test

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
OPTIMIZATION?=-O2
ifeq ($(uname_S),SunOS)
  CFLAGS?= -std=c99 -pedantic $(OPTIMIZATION) -fPIC -Wall -W -D__EXTENSIONS__ -D_XPG6
  CCLINK?= -ldl -lnsl -lsocket -lm -lpthread
  DYLIBNAME?=libhiredis.so
  DYLIB_MAKE_CMD?=gcc -shared -Wl,-soname,${DYLIBNAME} -o ${DYLIBNAME} ${OBJ}
  STLIBNAME?=libhiredis.a
  STLIB_MAKE_CMD?=ar rcs ${STLIBNAME} ${OBJ}
else ifeq ($(uname_S),Darwin)
  CFLAGS?= -std=c99 -pedantic $(OPTIMIZATION) -fPIC -Wall -W -Wwrite-strings $(ARCH) $(PROF)
  CCLINK?= -lm -pthread
  DYLIBNAME?=libhiredis.dylib
  DYLIB_MAKE_CMD?=libtool -dynamic -o ${DYLIBNAME} -lm ${DEBUG} - ${OBJ}
  STLIBNAME?=libhiredis.a
  STLIB_MAKE_CMD?=libtool -static -o ${STLIBNAME} - ${OBJ}
else
  CFLAGS?= -std=c99 -pedantic $(OPTIMIZATION) -fPIC -Wall -W -Wwrite-strings $(ARCH) $(PROF)
  CCLINK?= -lm -pthread
  DYLIBNAME?=libhiredis.so
  DYLIB_MAKE_CMD?=gcc -shared -Wl,-soname,${DYLIBNAME} -o ${DYLIBNAME} ${OBJ}
  STLIBNAME?=libhiredis.a
  STLIB_MAKE_CMD?=ar rcs ${STLIBNAME} ${OBJ}
endif
CCOPT= $(CFLAGS) $(CCLINK) $(ARCH) $(PROF)
DEBUG?= -g -ggdb 

all: ${DYLIBNAME} ${BINS}

# Deps (use make dep to generate this)
anet.o: anet.c fmacros.h anet.h
example.o: example.c hiredis.h sds.h
test.o: test.c hiredis.h sds.h
hiredis.o: hiredis.c hiredis.h sds.h anet.h
sds.o: sds.c sds.h
hiredis.o: hiredis.c hiredis.h sds.h anet.h

${DYLIBNAME}: ${OBJ}
	${DYLIB_MAKE_CMD}

${STLIBNAME}: ${OBJ}
	${STLIB_MAKE_CMD}

dynamic: ${DYLIBNAME}
static: ${STLIBNAME}

# Binaries:
hiredis-%: %.o ${DYLIBNAME}
	$(CC) -o $@ $(CCOPT) $(DEBUG) -L. -l hiredis -Wl,-rpath,. $<

test: hiredis-test
	./hiredis-test

libevent-example: extra/hiredis/libevent.h libevent-example.c ${DYLIBNAME}
	$(CC) -o $@ $(CCOPT) $(DEBUG) -I. -Iextra -L. -lhiredis -levent libevent-example.c

libev-example: extra/hiredis/libev.h libev-example.c ${DYLIBNAME}
	$(CC) -o $@ $(CCOPT) $(DEBUG) -I. -Iextra -L. -lhiredis -lev libev-example.c

.c.o:
	$(CC) -c $(CFLAGS) $(DEBUG) $(COMPILE_TIME) $<

clean:
	rm -rf ${DYLIBNAME} ${STLIBNAME} $(BINS) *-example *.o *.gcda *.gcno *.gcov

dep:
	$(CC) -MM *.c

32bit:
	@echo ""
	@echo "WARNING: if it fails under Linux you probably need to install libc6-dev-i386"
	@echo ""
	make ARCH="-m32"

gprof:
	make PROF="-pg"

gcov:
	make PROF="-fprofile-arcs -ftest-coverage"

noopt:
	make OPTIMIZATION=""
