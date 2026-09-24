#include "input.h"
#include "hw.h"

/* Read the C64/C128 keyboard matrix directly through CIA #1 instead of the
   slow KERNAL routine. A column is selected by driving one bit of PRA low,
   then PRB is read: a cleared row bit means that key is held. */
char keys(void)
{
    char k = 0, b;

    CIA1_PRA = 0xfd; /* select column 1 */
    b = CIA1_PRB;    /* snapshot all rows for this column */
    if (!(b & 0x04))
        k |= KEY_P1_UP; /* A */
    if (!(b & 0x10))
        k |= KEY_P1_DOWN; /* Z */

    CIA1_PRA = 0xef; /* select column 4 */
    b = CIA1_PRB;
    if (!(b & 0x20))
        k |= KEY_P2_UP; /* K */
    if (!(b & 0x10))
        k |= KEY_P2_DOWN; /* M */

    CIA1_PRA = 0x7f; /* select column 7 */
    if (!(CIA1_PRB & 0x40))
        k |= KEY_QUIT; /* Q */

    CIA1_PRA = 0xff; /* release all columns for the KERNAL's own scanning */
    return k;
}
