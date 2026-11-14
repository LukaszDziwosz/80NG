# 80NG

A text-mode arcade experiment for the Commodore 128 VDC, built with
[Oscar64](https://github.com/drmortalwombat/oscar64). It is a Pong-like game
used as a test bed for pixel-level motion on the VDC, at approximately 60 Hz.
Works with 16 KB VDC RAM.

- [Quick start](#quick-start)
- [How it works](#how-it-works)
- [Controls](#controls)
- [Building](#building)
- [Testing](#testing)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [License](#license)

## Quick start

Grab `build/80ng.prg` and run it in VICE (or on real hardware with a VDC and at
least 16 KB of VDC RAM):

```sh
x128 -80col -autostart build/80ng.prg
```

The binary is committed to the repository, so no toolchain is required just to
play. To build it yourself, see [Building](#building).

## How it works

The main trick is that the game never redraws the whole screen. Moving objects are implemented using custom VDC character shapes, effectively acting as small software sprites. Movement is pixel-level, but only the character cells touched by the bars and ball are updated.
Each frame, the ball renderer regenerates only 32 bytes of character data and commits a small dirty list of changed screen cells. All game logic is calculated before the vertical-blank wait, so the blanking period is used almost entirely for the actual VDC writes.
Combined with 2 MHz CPU execution and VDC-synchronised rendering, this keeps the update workload tiny and predictable. In VICE, the game runs at about 59.6 FPS on both PAL and NTSC, with zero blanking overruns.

## Controls

- Left bar: hold A / Z
- Right bar: hold K / M
- Quit: Q

Both players can move at once. The VDC uses non-interlaced 60 Hz timing even on
a PAL C128; quitting restores the original display timing and font.

## Building

```sh
make        # build/80ng.prg
make run    # build, then autostart it in x128
make clean  # remove build/80ng.prg
```

If Oscar64 is not on PATH, the Makefile falls back to a local install at
`$HOME/Developer/oscar64/bin/oscar64` (or `$OSCAR64_HOME/bin/oscar64`); override it with
`make OSCAR64=/path/to/oscar64` or by setting `OSCAR64` in the environment.

## Testing

```sh
make test
```

Requires `x128` (VICE) and Python 3. Tests run the actual PRG on PAL/16 KB and
NTSC/64 KB configurations and save screenshots under `build/`.

## Repository layout

| Path | Purpose |
|---|---|
| `src/` | All source. `80ng.c` is the only file handed to Oscar64. |
| `tests/` | `vice_80ng.py` drives the real PRG through the VICE binary monitor. |
| `build/` | `80ng.prg` (the runnable program) plus ignored intermediates. |
| `Makefile` | Build, run and test targets. |

## License

This project is released under the MIT License. See [LICENSE](LICENSE).
