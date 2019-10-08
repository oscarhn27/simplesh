#!/usr/bin/make -f

TARGET=simplesh

CFLAGS=-ggdb3 -Wall -Werror -Wno-unused -std=c11
LDLIBS=-lreadline

OBJECTS=$(patsubst %.c,%.o,$(wildcard *.c))

$(TARGET): $(OBJECTS)

clean:
	rm -rf *~ $(OBJECTS) $(TARGET) core

.PHONY: clean
