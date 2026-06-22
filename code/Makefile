CC = gcc
CFLAGS = -Wall -Wextra -std=gnu17 -g
# the gnu17 standard spec. is to prevent OSx or Windows(more likely) eventually blowing up or something strange IDK
# (default for Ubuntu 24.04)

all: build

build: controller bulb window fridge

controller: src/controller.c src/common.h
	$(CC) $(CFLAGS) -o controller src/controller.c

bulb: src/bulb.c src/common.h
	$(CC) $(CFLAGS) -o bulb src/bulb.c

window: src/window.c src/common.h
	$(CC) $(CFLAGS) -o window src/window.c

fridge: src/fridge.c src/common.h
	$(CC) $(CFLAGS) -o fridge src/fridge.c

# TODO: Add compilation rules for hub and timer here as they are created

clean:
	rm -f controller bulb window fridge
	rm -f /tmp/domotics_*.fifo

run: build
	./controller