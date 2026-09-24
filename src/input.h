#ifndef INPUT_H
#define INPUT_H

/* Pull in the keyboard scanner when this header is included. */
#pragma compile("input.c")

/* Poll the keyboard matrix and return a KEY_* bitmask (see hw.h). Bits 0..4
   map to A, Z, K, M and Q respectively; untouched bits read 0. */
char keys(void);

#endif /* INPUT_H */
