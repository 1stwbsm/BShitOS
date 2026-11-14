/* Integer-only spinning cube renderer, shared by cube and graphic. */
#include "bs.h"

#define ONE 16384 /* fixed-point 1.0 */
#define CAMERA_DISTANCE (ONE * 7 / 2)

struct point {
    int x, y;
    int r, g, b;
};

/* Corners: bit 0 is +x, bit 1 is +y, bit 2 is +z. */
static const unsigned char corner_colors[8][3] = {
    {255, 40, 40}, {255, 160, 0}, {255, 255, 0}, {40, 255, 40},
    {0, 255, 255}, {60, 90, 255}, {255, 0, 255}, {255, 255, 255},
};

/* Wound so each face's normal points out of the cube. */
static const unsigned char faces[6][4] = {
    {0, 2, 3, 1}, {4, 5, 7, 6}, {0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3},
};

static unsigned int *target;
static int target_width, target_height;

/* Bhaskara's approximation; close enough to be invisible here. */
static int sin_deg(int angle)
{
    int sign = 1, p;

    angle %= 360;
    if (angle < 0)
        angle += 360;
    if (angle >= 180) {
        angle -= 180;
        sign = -1;
    }
    p = angle * (180 - angle);
    return sign * (4 * p * ONE / (40500 - p));
}

static void rotate(int *a, int *b, int angle)
{
    int s = sin_deg(angle), c = sin_deg(angle + 90);
    int next_a = (*a * c - *b * s) / ONE;
    int next_b = (*a * s + *b * c) / ONE;

    *a = next_a;
    *b = next_b;
}

static int edge(const struct point *a, const struct point *b, int x, int y)
{
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static int min3(int a, int b, int c)
{
    int m = a < b ? a : b;
    return m < c ? m : c;
}

static int max3(int a, int b, int c)
{
    int m = a > b ? a : b;
    return m > c ? m : c;
}

/* Fills a triangle facing the camera, blending its corner colours. */
static void triangle(const struct point *p0, const struct point *p1, const struct point *p2)
{
    int area = edge(p0, p1, p2->x, p2->y);
    int left, right, top, bottom;

    if (area >= 0)
        return; /* facing away */

    /* Swapping two corners makes every inside test positive. */
    {
        const struct point *swap = p1;

        p1 = p2;
        p2 = swap;
        area = -area;
    }

    left = min3(p0->x, p1->x, p2->x);
    right = max3(p0->x, p1->x, p2->x);
    top = min3(p0->y, p1->y, p2->y);
    bottom = max3(p0->y, p1->y, p2->y);
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right >= target_width)
        right = target_width - 1;
    if (bottom >= target_height)
        bottom = target_height - 1;

    for (int y = top; y <= bottom; y++) {
        for (int x = left; x <= right; x++) {
            int w0 = edge(p1, p2, x, y);
            int w1 = edge(p2, p0, x, y);
            int w2 = edge(p0, p1, x, y);

            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            target[y * target_width + x] =
                (unsigned int)((w0 * p0->r + w1 * p1->r + w2 * p2->r) / area) << 16 |
                (unsigned int)((w0 * p0->g + w1 * p1->g + w2 * p2->g) / area) << 8 |
                (unsigned int)((w0 * p0->b + w1 * p1->b + w2 * p2->b) / area);
        }
    }
}

void cube_render(unsigned int *pixels, int width, int height, int angle_x, int angle_y, int angle_z)
{
    struct point screen[8];
    int focal = (width < height * 8 / 5 ? width * 5 / 8 : height) * 165 / 200;

    target = pixels;
    target_width = width;
    target_height = height;
    memset(pixels, 0, (unsigned int)(width * height) * 4);

    for (int i = 0; i < 8; i++) {
        int x = i & 1 ? ONE : -ONE;
        int y = i & 2 ? ONE : -ONE;
        int z = i & 4 ? ONE : -ONE;
        int depth;

        rotate(&y, &z, angle_x);
        rotate(&z, &x, angle_y);
        rotate(&x, &y, angle_z);
        depth = z + CAMERA_DISTANCE;

        screen[i].x = width / 2 + x * focal / depth;
        screen[i].y = height / 2 + y * focal / depth;
        screen[i].r = corner_colors[i][0];
        screen[i].g = corner_colors[i][1];
        screen[i].b = corner_colors[i][2];
    }

    for (int f = 0; f < 6; f++) {
        const unsigned char *q = faces[f];

        triangle(&screen[q[0]], &screen[q[1]], &screen[q[2]]);
        triangle(&screen[q[0]], &screen[q[2]], &screen[q[3]]);
    }
}
