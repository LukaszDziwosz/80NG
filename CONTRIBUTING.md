# Contributing

This is a small single-board demo hack, but fixes and improvements are welcome.

## Building

- [Oscar64](https://github.com/drmortalwombat/oscar64) on `PATH`, or edit the
  `OSCAR64` fallback at the top of the `Makefile`.
- [VICE](https://vice-emu.sourceforge.io/) (`x128`) and Python 3 for the tests.
  The test script only uses the standard library.

```sh
make        # build/80ng.prg
make run    # build + start x128
make test   # PAL/16 KB and NTSC/64 KB regression test
make clean  # remove build/80ng.prg
```

## Repo layout

| Path | Purpose |
|---|---|
| `src/80ng.c` | Game state, drawing, physics, main loop. This is the only file passed to Oscar64. |
| `src/hw.h` | Volatile register macro and CIA1/`KEY_*` definitions. |
| `src/input.{h,c}` | Keyboard matrix scan. |
| `src/sound.{h,c}` | Minimal one-voice SID driver. |
| `tests/vice_80ng.py` | Drives the real PRG through VICE's binary monitor. |
| `docs/` | Background notes and how the renderer works. |
| `build/80ng.prg` | The committed, runnable binary. Everything else in `build/` is ignored. |

## Source rules

Oscar64 accepts exactly one source file on the command line. Multi-file builds are
done by putting `#pragma compile("x.c")` in the **header**; `80ng.c` just
`#include`s the header and the Makefile stays a single rule. Follow that pattern
when adding a module.

## Test rule

The tests read the real VDC screen and font RAM and compare them against pixel
rectangles built independently in Python. If you change what is drawn, update
both sides — the expectations in `tests/vice_80ng.py` mirror the renderer
exactly. A change is not done until `make test` passes on both PAL and NTSC with
zero blank overruns.

## Committing the binary

`build/80ng.prg` is versioned on purpose so people can try the game without the
toolchain. If you change behaviour, rebuild and commit the refreshed `80ng.prg`
together with the source change.
