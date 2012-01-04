include ../Makefile.common

TESTS=test-format test-parser test-object test-handle

all: $(TESTS)

test-%: test-%.o
	$(CC) -o $@ $(REAL_LDFLAGS) $< ../$(STLIBNAME)

.c.o:
	$(CC) -std=c99 -pedantic -c -O0 $(REAL_CFLAGS) $<

clean:
	rm -f *.o $(TESTS)

include ./Makefile.dep

dep:
	$(CC) -MM *.c > Makefile.dep

.PHONY: all clean dep
