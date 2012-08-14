include ../src/Makefile.common

STD= -std=c99 -pedantic -D_POSIX_C_SOURCE=200112L
OPT= -O0
WARN= -Wall -W
HIREDIS_CFLAGS+= -I../src

TESTS=test-format test-parser test-object test-handle test-context test-request
OBJ=spawn.o net-helper.o

all: $(TESTS)

../src/$(STLIBNAME):
	cd ../src && $(MAKE) $(STLIBNAME)

test-%: test-%.o $(OBJ) ../src/$(STLIBNAME)
	$(HIREDIS_LD) -o $@ $^ $(FINAL_LIBS)

.c.o:
	$(HIREDIS_CC) -c $<

clean:
	rm -f *.o $(TESTS)

-include ./Makefile.dep

dep:
	$(HIREDIS_CC) -MM *.c > Makefile.dep

.PHONY: all clean dep
