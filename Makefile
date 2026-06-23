SHELL := /bin/sh

.PHONY: build clean run

build:
	$(MAKE) -C code build

clean:
	$(MAKE) -C code clean

run:
	$(MAKE) -C code run $(if $(ARGS),SCENARIO="$(ARGS)")