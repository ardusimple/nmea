CC      = cc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -Isrc
LDFLAGS = -lm

SRC  = src/nmea.c
TEST = tests/test_nmea.c

.PHONY: all test clean lint

all: test

test: $(SRC) $(TEST) src/nmea.h
	$(CC) $(CFLAGS) -o $@ $(TEST) $(SRC) $(LDFLAGS)
	./$@
	rm -f $@

lint:
	clang --analyze $(CFLAGS) $(SRC) src/nmea.h

clean:
	rm -f test
