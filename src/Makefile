# Hiredis Makefile
# Copyright (C) 2010-2011 Salvatore Sanfilippo <antirez at gmail dot com>
# Copyright (C) 2010-2011 Pieter Noordhuis <pcnoordhuis at gmail dot com>
# This file is released under the BSD license, see the COPYING file

include ./Makefile.common

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
