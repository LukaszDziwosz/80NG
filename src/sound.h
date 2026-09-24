#ifndef SOUND_H
#define SOUND_H

/* Pull in the SID driver implementation when this header is included. */
#pragma compile("sound.c")

/* Minimal SID driver: one voice, no interrupts. sound_tick() is called once
   per game frame and releases the gate when the effect's timer expires. */
void sound_init(void);
void sound_tick(void);

/* Effects. */
void sound_bar(void);
void sound_wall(void);
void sound_point(void);

#endif /* SOUND_H */
