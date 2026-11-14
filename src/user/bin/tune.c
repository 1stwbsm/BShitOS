/* tune: plays an original little fanfare on the PC speaker. */
#include "bs.h"

static const unsigned short notes[][2] = {
    {523, 120}, {659, 120}, {784, 120}, {1047, 240}, {0, 60},
    {784, 120}, {1047, 360}, {0, 120},
    {587, 120}, {698, 120}, {880, 120}, {1175, 240}, {0, 60},
    {880, 120}, {1175, 360}, {0, 120},
    {1319, 120}, {1175, 120}, {1047, 120}, {784, 120}, {1047, 480},
};

int main(void)
{
    for (unsigned int i = 0; i < sizeof(notes) / sizeof(notes[0]); i++)
        beep(notes[i][0], notes[i][1]);
    return 0;
}
