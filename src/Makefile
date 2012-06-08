# Hiredis Makefile
# Copyright (C) 2010-2011 Salvatore Sanfilippo <antirez at gmail dot com>
# Copyright (C) 2010-2011 Pieter Noordhuis <pcnoordhuis at gmail dot com>
# This file is released under the BSD license, see the COPYING file

LIBNAME= libhiredis
HIREDIS_MAJOR= 0
HIREDIS_MINOR= 11

DYLIBSUFFIX= so
STLIBSUFFIX= a
DYLIB_MINOR_NAME= $(LIBNAME).$(DYLIBSUFFIX).$(HIREDIS_MAJOR).$(HIREDIS_MINOR)
DYLIB_MAJOR_NAME= $(LIBNAME).$(DYLIBSUFFIX).$(HIREDIS_MAJOR)
DYLIBNAME= $(LIBNAME).$(DYLIBSUFFIX)
DYLIB_MAKE_CMD= $(CC) -shared -Wl,-soname,$(DYLIB_MINOR_NAME) -o $(DYLIBNAME) $(FINAL_LDFLAGS)
STLIBNAME= $(LIBNAME).$(STLIBSUFFIX)
STLIB_MAKE_CMD= ar rcs $(STLIBNAME)

# Default settings
STD= -std=c99 -pedantic
WARN= -Wall -W -Wstrict-prototypes -Wwrite-strings -Wno-unused-label
OPT= -O3
FINAL_CFLAGS= $(STD) $(WARN) $(OPT) $(DEBUG) -fPIC $(CFLAGS) $(HIREDIS_CFLAGS)
FINAL_LDFLAGS= $(DEBUG) $(LDFLAGS) $(HIREDIS_LDFLAGS)
DEBUG= -g -ggdb

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

# SunOS overrides
ifeq ($(uname_S),SunOS)
  HIREDIS_CFLAGS= -D__EXTENSIONS__ -D_XPG6
  HIREDIS_LDFLAGS= -ldl -lnsl -lsocket
  DYLIB_MAKE_CMD=$(CC) -G -o $(DYLIBNAME) -h $(DYLIB_MINOR_NAME) $(LDFLAGS)
  INSTALL= cp -r
endif

# Darwin overrides
ifeq ($(uname_S),Darwin)
  DYLIBSUFFIX= dylib
  DYLIB_MINOR_NAME= $(LIBNAME).$(HIREDIS_MAJOR).$(HIREDIS_MINOR).$(DYLIBSUFFIX)
  DYLIB_MAJOR_NAME= $(LIBNAME).$(HIREDIS_MAJOR).$(DYLIBSUFFIX)
  DYLIB_MAKE_CMD= $(CC) -shared -Wl,-install_name,$(DYLIB_MINOR_NAME) -o $(DYLIBNAME) $(FINAL_LDFLAGS)
endif

OBJ= \
		address.o \
		context.o \
		fd.o \
		format.o \
		handle.o \
		object.o \
		parser.o \
		request.o \
		sds.o \

all: $(DYLIBNAME)

$(DYLIBNAME): $(OBJ)
	$(DYLIB_MAKE_CMD) $(OBJ)

$(STLIBNAME): $(OBJ)
	$(STLIB_MAKE_CMD) $(OBJ)

dynamic: $(DYLIBNAME)
static: $(STLIBNAME)

.c.o:
	$(CC) $(FINAL_CFLAGS) -c $<

clean:
	rm -rf $(DYLIBNAME) $(STLIBNAME) *.o

-include ./Makefile.dep

dep:
	$(CC) -MM *.c > Makefile.dep
