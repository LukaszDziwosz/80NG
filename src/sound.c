#include "sound.h"
#include "hw.h"

/* SID registers. The C128 has a 6581 SID at $d400-$d41f. */
#define SID_FREQ_LO HW(0xd400)
#define SID_FREQ_HI HW(0xd401)
#define SID_CTRL HW(0xd404)
#define SID_AD HW(0xd405)
#define SID_SR HW(0xd406)
#define SID_VOLUME HW(0xd418)

/* Frames the current tone's gate is held; 0 means the voice is idle. */
static char sound_timer;

void sound_init(void)
{
    SID_VOLUME = 15;

    /* Fast attack, short release. */
    SID_AD = 0x00;
    SID_SR = 0x82;

    SID_CTRL = 0;
    sound_timer = 0;
}

/* Start a tone and let sound_tick() stop it later. */
static void sound_start(unsigned freq, char frames)
{
    SID_CTRL = 0;

    SID_FREQ_LO = (char)freq;
    SID_FREQ_HI = (char)(freq >> 8);

    /* Sawtooth + gate. No pulse-width register needed. */
    SID_CTRL = 0x21;

    sound_timer = frames;
}

void sound_tick(void)
{
    if (sound_timer)
    {
        --sound_timer;

        if (!sound_timer)
        {
            /* Sawtooth, gate off. SID release handles the tail. */
            SID_CTRL = 0x20;
        }
    }
}

void sound_bar(void)
{
    sound_start(0x3000, 4); /* ~67 ms */
}

void sound_wall(void)
{
    sound_start(0x1800, 3); /* ~50 ms */
}

void sound_point(void)
{
    sound_start(0x0800, 12); /* ~200 ms */
}
