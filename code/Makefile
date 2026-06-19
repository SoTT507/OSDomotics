CC = gcc
CFLAGS = -Wall -Wextra -std=gnu17 -g
# the gnu17 standard spec. is to prevent OSx or Windows(more likely) eventually blowing up or something strange IDK
# (default for Ubuntu 24.04)

all: build

build: controller bulb

controller: src/controller.c src/common.h
	$(CC) $(CFLAGS) -o controller src/controller.c

bulb: src/bulb.c src/common.h
	$(CC) $(CFLAGS) -o bulb src/bulb.c

# TODO: Add compilation rules for window, fridge, hub, timer here as you create them

clean:
	rm -f controller bulb
	rm -f /tmp/domotics_*.fifo

run: build
	./controller
