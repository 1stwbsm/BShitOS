/*
 * Runs BShitEmu natively, for testing without a browser.
 *
 * usage: emutest <bskern.elf> <root.img> <script>
 * Script lines: "wait MS", "type TEXT" (~ is Enter), "key CODE" (hex, set 1),
 * "ctrl LETTER", "mouse DX DY BUTTONS", "shot FILE.ppm". Serial output goes to stdout.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/emu/emu.h"

u32 emu_staging(void);
u32 emu_ram(void);
u32 emu_ramdisk_base(void);
int emu_boot(u32 kernel_length, u32 ramdisk_length);
int emu_run(double budget_ms);
void emu_key(u32 code, int down);
void emu_mouse(s32 dx, s32 dy, u32 buttons);

double host_now(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

void host_date(u32 *out)
{
    time_t now = time(0);
    struct tm *t = gmtime(&now);

    out[0] = (u32)t->tm_year + 1900;
    out[1] = (u32)t->tm_mon + 1;
    out[2] = (u32)t->tm_mday;
    out[3] = (u32)t->tm_hour;
    out[4] = (u32)t->tm_min;
    out[5] = (u32)t->tm_sec;
}

void host_serial(u32 byte)
{
    putchar((int)byte);
    fflush(stdout);
}

void host_tone(u32 hz)
{
    if (hz)
        fprintf(stderr, "[tone %u Hz]\n", hz);
}

static long load(const char *path, u8 *where, long limit)
{
    FILE *f = fopen(path, "rb");
    long n;

    if (!f) {
        perror(path);
        exit(1);
    }
    n = (long)fread(where, 1, (size_t)limit, f);
    fclose(f);
    return n;
}

static void shot(const char *path)
{
    FILE *f = fopen(path, "wb");

    fprintf(f, "P6\n%d %d\n255\n", FB_WIDTH, FB_HEIGHT);
    for (int i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
        u32 p = framebuffer[i];
        u8 rgb[3] = {(u8)p, (u8)(p >> 8), (u8)(p >> 16)};

        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

/* US layout, scancode set 1 */
static int scancode(char c, int *shift)
{
    static const char *plain = "\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./";
    static const char *shifted = "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?";

    *shift = 0;
    if (c == ' ')
        return 0x39;
    for (int i = 1; i < 0x36; i++) {
        if (plain[i] == c)
            return i;
        if (shifted[i] == c) {
            *shift = 1;
            return i;
        }
    }
    return -1;
}

static void run_for(double ms, int *status)
{
    double end = host_now() + ms;

    while (*status == 0 && host_now() < end)
        *status = emu_run(10);
}

int main(int argc, char **argv)
{
    char line[512];
    FILE *script;
    long kernel, disk;
    int status;

    if (argc != 4) {
        fprintf(stderr, "usage: emutest <bskern.elf> <root.img> <script>\n");
        return 2;
    }
    kernel = load(argv[1], (u8 *)(unsigned long)emu_staging(), 1 << 20);
    disk = load(argv[2], ram + emu_ramdisk_base(), 64 << 20);
    status = emu_boot((u32)kernel, (u32)disk);
    if (status < 0) {
        fprintf(stderr, "boot failed: %d (kernel %ld bytes, disk %ld bytes)\n", status, kernel, disk);
        return 1;
    }
    status = 0;
    script = fopen(argv[3], "r");
    while (script && fgets(line, sizeof(line), script)) {
        line[strcspn(line, "\n")] = 0;
        if (strncmp(line, "wait ", 5) == 0) {
            run_for(atof(line + 5), &status);
        } else if (strncmp(line, "type ", 5) == 0) {
            for (char *p = line + 5; *p; p++) {
                int shift, code = scancode(*p == '~' ? '\n' : *p, &shift);

                if (code < 0)
                    continue;
                if (shift)
                    emu_key(0x2A, 1);
                emu_key((u32)code, 1);
                emu_key((u32)code, 0);
                if (shift)
                    emu_key(0x2A, 0);
                run_for(25, &status);
            }
        } else if (strncmp(line, "key ", 4) == 0) {
            u32 code = (u32)strtoul(line + 4, 0, 16);

            emu_key(code, 1);
            emu_key(code, 0);
            run_for(25, &status);
        } else if (strncmp(line, "ctrl ", 5) == 0) {
            int shift, code = scancode(line[5], &shift);

            if (code >= 0) {
                emu_key(0x1D, 1);
                emu_key((u32)code, 1);
                emu_key((u32)code, 0);
                emu_key(0x1D, 0);
                run_for(25, &status);
            }
        } else if (strncmp(line, "mouse ", 6) == 0) {
            int dx, dy, b;

            sscanf(line + 6, "%d %d %d", &dx, &dy, &b);
            emu_mouse(dx, dy, (u32)b);
            run_for(40, &status);
        } else if (strncmp(line, "shot ", 5) == 0) {
            shot(line + 5);
        }
        if (status) {
            fprintf(stderr, "\n[cpu stopped: status %d: %s]\n", status, cpu_crash_message());
            break;
        }
    }
    shot("final.ppm");
    return 0;
}
