# build/

Only one file here is part of the repository: **`80ng.prg`**, the runnable
Commodore 128 program. Everything else is produced locally and ignored by Git.

`80ng.prg` is committed so the game can be run without installing Oscar64. Load
it in an emulator or on real hardware:

```sh
x64sc -80col ...            # not this one - it is a C128 VDC program
x128 -80col -autostart build/80ng.prg
```

Requires a VDC with at least 16 KB RAM. Hold A/Z for the left bar, K/M for the
right bar, Q to quit.

Everything below is generated and not versioned:

| File | Produced by |
|---|---|
| `80ng.asm`, `80ng.int`, `80ng.lbl`, `80ng.map` | `make` (Oscar64 intermediate output; `80ng.lbl` is read by the tests) |
| `80ng-pal-16k.png`, `80ng-ntsc-64k.png` (+ `-exit`) | `make test` (`m.capture(...)` screenshots) |
| `vice-*.log` | `make test` (VICE console output) |

`make clean` removes `80ng.prg`; a fresh `make` recreates it.
