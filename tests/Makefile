# Host build of the platform-independent modules plus their unit tests.
# Usage: make -C tests        (builds and runs)

CC      ?= cc
SAN     ?= -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  ?= -std=gnu99 -O1 -g -Wall -Wextra -Werror $(SAN)
LDFLAGS ?= $(SAN)

SRC     := ../source
SOURCES := $(SRC)/canvas.c $(SRC)/coverflow.c $(SRC)/covers.c $(SRC)/iso9660.c $(SRC)/lang.c \
           $(SRC)/launcher.c $(SRC)/library.c $(SRC)/serial.c $(SRC)/text.c tests.c

.PHONY: all run clean

all: run

run_tests: $(SOURCES) $(wildcard ../include/*.h)
	$(CC) $(CFLAGS) -I../include $(SOURCES) -o $@ $(LDFLAGS) -lm

run: run_tests
	./run_tests

clean:
	rm -rf run_tests run_tests.dSYM
