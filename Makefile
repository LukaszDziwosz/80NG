OSCAR64 ?= $(shell command -v oscar64 2>/dev/null)
ifeq ($(strip $(OSCAR64)),)
OSCAR64 := $(HOME)/Desktop/80Terminal/.tools/oscar64/bin/oscar64
endif
VICE ?= x128

.PHONY: all run test clean

all: build/80ng.prg

build/80ng.prg: src/80ng.c
	mkdir -p build
	"$(OSCAR64)" -tm=c128e -O2 -n -dNOFLOAT -o=$@ src/80ng.c

run: build/80ng.prg
	$(VICE) -80col -autostartprgmode 1 -autostart $<

test: build/80ng.prg
	python3 -B tests/vice_80ng.py
	python3 -B tests/vice_80ng.py --ntsc --ram 64

clean:
	rm -f build/80ng.prg build/80ng.asm build/80ng.int build/80ng.lbl build/80ng.map
