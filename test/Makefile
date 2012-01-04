include ../Makefile.common

TESTS=test-format test-parser test-object test-handle

all: $(TESTS)

../$(STLIBNAME):
	cd .. && $(MAKE) $(STLIBNAME)

test-%: test-%.o ../$(STLIBNAME)
	$(CC) -o $@ $(REAL_LDFLAGS) $^

.c.o:
	$(CC) -std=c99 -pedantic -c -O0 $(REAL_CFLAGS) $<

clean:
	rm -f *.o $(TESTS)

include ./Makefile.dep

dep:
	$(CC) -MM *.c > Makefile.dep

.PHONY: all clean dep
