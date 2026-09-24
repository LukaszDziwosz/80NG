# What makes BustedBoulders smooth

Investigation of `References/BustedBoulders/bustedboulders_disassembled.txt`.
Addresses below are 8502 addresses printed in that file. Its automatic comments
incorrectly call `$d600/$d601` SID registers; they are the VDC ports.

## Evidence in the supplied disassembly

| Addresses | Behavior | Why it matters |
|---|---|---|
| `$1e4c–$1e59` | Blanks the VIC display, writes 1 to `$d030`, executes `SEI`. | Runs at 2 MHz and keeps KERNAL IRQ code out of VDC register transactions. |
| `$1f4b–$1f59` | Waits for bit 5 of `$d600` to clear, then become set. | One game tick per new VDC vertical blank, independent of the VIC/CIA clock. |
| `$2a63–$2a6a` | Gameplay state 2 calls `$50ea` and returns to the frame loop. | The gameplay renderer is paced by that VDC wait. |
| `$37bf–$3904` | Programs display timing and mode; R0=`$7e`, R8=3, character/attribute bases and font base. | High resolution is a display configuration. It is not the source of the rendering throughput. |
| `$3c40–$420b` | Builds custom character definitions in VDC RAM starting at `$2000`, with 16-byte glyph slots. | Artwork and motion variants are represented by characters. |
| `$50ea–$51ca` | Calls old-object erasers, object drawers, simulation and keyboard routines. | Updates individual objects rather than transferring a complete screen. |
| `$51cb–$52ca` | Erases old ball cells, using stored cell coordinates and horizontal/vertical spill flags. | The old ball occupies only one to four cells. |
| `$55aa–$56b4` | Selects a ball character variant from `$8c14`, writes its character code and optional adjoining codes; the next row is `+80` bytes. | Sub-character motion is implemented through glyph variants and tiny screen updates. |
| `$43b1–$4460` | Scans the keyboard via CIA1 directly. | Input does not depend on KERNAL key-repeat events. |


Its high-resolution mode is interlaced. A vertical-blank tick is therefore a
field tick; that alone does not prove 60 complete progressive high-resolution
frames per second. The important, reusable technique is small character-based
object updates synchronized with each VDC refresh.


## Adaptation in this project

`src/80ng.c` keeps 80x25 character mode and uses a small custom font:

- Bars use precomputed top/bottom glyphs for pixel offsets inside a cell.
- The ball uses four private glyph slots. Their 32 visible scanline bytes are
  composed in CPU RAM, including the background net or any paddle underneath.
- A bounded, deduplicated dirty-cell list restores old positions and draws new
  ones. There is one screen page and no frame-by-frame display blanking.
- Input and simulation run outside the blanking transfer. A new VDC blank then
  triggers the short font/cell transfer, with the VDC ready flag checked for
  every access.
- The CPU runs at 2 MHz with IRQs masked during play. A/Z and K/M are sampled as
  held keys, including simultaneous input from both players.
- Standard non-interlaced approximately 60 Hz VDC timing is selected on both PAL
  and NTSC machines: R0=126, R4=32, R5=0, R7=29, R8=0, R9=7. This does not change
  the VIC/CPU PAL/NTSC setting. The nominal VDC period is
  `127 * 8 / 16 MHz * 264 = 16.764 ms`, or approximately 59.65 Hz.
- All video RAM addresses fit below `$4000`. The overwritten font, display and
  timing registers, CIA port configuration, VIC display and CPU speed are
  restored on quit.

The ball travels three horizontal pixels and one vertical pixel per tick;
bars travel two vertical pixels per tick. Collision tests use pixel bounds
and bar-face crossings, including reflected overshoot. Points display 00–99
and wrap at 100.

## Verification

Built with Oscar64 1.32.273. Tested the compiled program in VICE 3.9 with PAL/
16 KB and NTSC/64 KB configurations. Both sustained approximately 60 updates/s,
with zero `blank_overruns`; the exercised cases queued at most 22 screen cells.
That is at most 54 video RAM data bytes in the observed cases (32 glyph bytes
plus 22 character bytes), versus 2,000 before. Register/address writes are
additional, so this is a payload comparison rather than a CPU speedup claim.

`tests/vice_80ng.py` checks actual VDC screen/font memory against independently
constructed pixel rectangles, including all 64 ball offsets, all bar phases,
net overlap, continuous movement without trails, simultaneous input, limits,
collisions, misses, serving, score carry/wrap, and quit restoration. Input tests
inject the held-key result at `keys()`; they do not emulate physical key presses.
The frame-rate sample is measured against host time at normal emulator speed.
These are emulator results, not measurements from a physical C128.

The monitor harness follows the [VICE binary monitor protocol](https://vice-emu.sourceforge.io/vice_13.html).

Run:

```sh
make test
```

A test-owned VICE instance starts and exits for each configuration. Screenshots
and logs are written under `build/`.
