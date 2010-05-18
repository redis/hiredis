# Hiredis Makefile
# Copyright (C) 2010 Salvatore Sanfilippo <antirez at gmail dot com>
# This file is released under the BSD license, see the COPYING file

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
OPTIMIZATION?=-O2
ifeq ($(uname_S),SunOS)
  CFLAGS?= -std=c99 -pedantic $(OPTIMIZATION) -Wall -W -D__EXTENSIONS__ -D_XPG6
  CCLINK?= -ldl -lnsl -lsocket -lm -lpthread
else
  CFLAGS?= -std=c99 -pedantic $(OPTIMIZATION) -Wall -W $(ARCH) $(PROF)
  CCLINK?= -lm -pthread
endif
CCOPT= $(CFLAGS) $(CCLINK) $(ARCH) $(PROF)
DEBUG?= -g -rdynamic -ggdb 

OBJ = anet.o hiredis.o sds.o example.o
TESTOBJ = anet.o hiredis.o sds.o test.o

PRGNAME = hiredis-example
TESTNAME = hiredis-test

all: hiredis-example hiredis-test

# Deps (use make dep to generate this)
anet.o: anet.c fmacros.h anet.h
example.o: example.c hiredis.h sds.h
test.o: test.c hiredis.h sds.h
hiredis.o: hiredis.c hiredis.h sds.h anet.h
sds.o: sds.c sds.h
hiredis.o: hiredis.c hiredis.h sds.h anet.h

hiredis-example: $(OBJ)
	$(CC) -o $(PRGNAME) $(CCOPT) $(DEBUG) $(OBJ)

hiredis-test: $(TESTOBJ)
	$(CC) -o $(TESTNAME) $(CCOPT) $(DEBUG) $(TESTOBJ)
	./hiredis-test

.c.o:
	$(CC) -c $(CFLAGS) $(DEBUG) $(COMPILE_TIME) $<

clean:
	rm -rf $(PRGNAME) $(TESTNAME) *.o *.gcda *.gcno *.gcov

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
