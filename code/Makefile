CC = gcc
CFLAGS = -Wall -Wextra -std=gnu17 -g
# the gnu17 standard spec. is to prevent OSx or Windows(more likely) eventually blowing up or something strange IDK
# (default for Ubuntu 24.04)

SCENARIO ?= basic

all: build

build: controller bulb window fridge manual_interaction

controller: src/controller.c src/common.h
	$(CC) $(CFLAGS) -o controller src/controller.c

bulb: src/bulb.c src/common.h
	$(CC) $(CFLAGS) -o bulb src/bulb.c

window: src/window.c src/common.h
	$(CC) $(CFLAGS) -o window src/window.c

fridge: src/fridge.c src/common.h
	$(CC) $(CFLAGS) -o fridge src/fridge.c

manual_interaction: src/manual_interaction.c src/common.h
	$(CC) $(CFLAGS) -o manual_interaction src/manual_interaction.c

# TODO: Add compilation rules for window, fridge, hub, timer here as you create them

clean:
	rm -f controller bulb window fridge manual_interaction
	rm -f /tmp/domotics_*.fifo
	rm -f /tmp/test_stdin.fifo

run: build
	chmod +x run_demo.sh
	./run_demo.sh $(SCENARIO)