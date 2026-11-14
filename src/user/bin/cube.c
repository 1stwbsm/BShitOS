/* cube: a spinning multicoloured cube. Any key quits. */
#include "bs.h"

#define WIDTH 320
#define HEIGHT 200
#define FRAME_MS 16

static unsigned int frame[WIDTH * HEIGHT];

int main(void)
{
    int angle_x = 20, angle_y = 30, angle_z = 0;

    while (!poll_key()) {
        cube_render(frame, WIDTH, HEIGHT, angle_x, angle_y, angle_z);
        fb_blit(frame, WIDTH, HEIGHT);
        angle_x += 2;
        angle_y += 3;
        angle_z += 1;
        sleep_ms(FRAME_MS);
    }
    return 0;
}
