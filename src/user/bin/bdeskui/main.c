/*
 * bdeskui: the BDeskUI operating environment, on top of the BShitOS TTY.
 *
 * Started by the login shell's /conf/autoexec.bsh. Exits with EXIT_LOGOUT
 * (the shell logs out too), EXIT_TO_TTY (the shell stays), or EXIT_NO_GRAPHICS
 * when the machine has no graphics mode, which leaves you at the TTY.
 */
#include "bdeskui.h"

#define FRAME_MS 15
#define LIST_MAX 160
#define SHUTDOWN_POLL_FRAMES 30 /* about half a second */
#define DEFAULT_SCREEN "web cube edit"
#define DEFAULT_TTY "su passwd install useradd userdel bsh login"

static char screen_list[LIST_MAX] = DEFAULT_SCREEN, tty_list[LIST_MAX] = DEFAULT_TTY;

static int listed(const char *list, const char *program)
{
    unsigned int length = strlen(program);
    const char *p = list;

    while (*p) {
        unsigned int n = 0;

        while (*p == ' ')
            p++;
        while (p[n] && p[n] != ' ')
            n++;
        if (n == length && n && strncmp(p, program, n) == 0)
            return 1;
        p += n;
    }
    return 0;
}

int bdeskui_needs_screen(const char *program)
{
    return listed(screen_list, program);
}

int bdeskui_needs_tty(const char *program)
{
    return listed(tty_list, program);
}

/*
 * Graphics programs (web, cube) take the screen and give it back. TTY programs
 * (su, passwd, install) get the real text console, like BDeskUI stepping aside,
 * and the screen waits for a key afterwards so their last words can be read.
 */
void bdeskui_run_program(int argc, char **argv)
{
    char path[BS_PATH_MAX];
    unsigned int length = strlen(argv[0]);
    int tty = bdeskui_needs_tty(argv[0]), code;

    if (length + 10 > sizeof(path))
        return;
    memcpy(path, "/bin/", 5);
    memcpy(path + 5, argv[0], length);
    memcpy(path + 5 + length, ".bs", 4);

    graphics_end();
    console_clear();
    if (tty)
        printf("\033[90m-- %s, on the TTY. BDeskUI comes back when it exits --\033[0m\n\n", argv[0]);
    code = spawn(path, argc, argv);
    if (code < 0)
        printf("\033[91m%s: %s\033[0m\n", argv[0], strerror(code));
    if ((tty || code < 0) && !shutting_down()) {
        printf("\n\033[90m-- any key: back to BDeskUI --\033[0m");
        for (unsigned int idle = 1; !poll_key(); idle++)
            if ((idle & 63) == 0 && shutting_down())
                break;
    }
    console_clear();
}

/* Case-insensitive, so "notes" finds Notes. */
static void open_by_name(const char *name)
{
    static const struct app *const apps[] = {&app_terminal, &app_files, &app_notes, &app_system, &app_settings};

    for (unsigned int a = 0; a < sizeof(apps) / sizeof(apps[0]); a++) {
        const char *p = apps[a]->name, *q = name;

        while (*p && (*p | 0x20) == (*q | 0x20))
            p++, q++;
        if (!*p && !*q)
            wm_open(apps[a], 0);
    }
}

static int graphics_available(void)
{
    struct bs_sysinfo info;

    sysinfo(&info);
    if (info.screen_width >= SCREEN_W && info.screen_height >= SCREEN_H)
        return 1;
    if (info.screen_width == 0)
        printf("\033[93mbdeskui: no graphics mode here (VGA text); staying in the TTY\033[0m\n");
    else
        printf("\033[93mbdeskui: needs %ux%u, the screen is %ux%u; staying in the TTY\033[0m\n", SCREEN_W, SCREEN_H,
               info.screen_width, info.screen_height);
    return 0;
}

int main(int argc, char **argv)
{
    int x = SCREEN_W / 2, y = SCREEN_H / 2, dirty = 1, frames = 0, told_shutdown = 0;
    unsigned int buttons = 0;
    struct bs_mouse mouse;

    if (argc > 1 && strcmp(argv[1], "--autostart") == 0) { /* from autoexec, at login */
        char value[8];

        if (bdeskui_conf_get("autostart", value, sizeof(value)) == 0 && strcmp(value, "no") == 0)
            return EXIT_TO_TTY;
        argc = 1;
    }
    if (!graphics_available())
        return EXIT_NO_GRAPHICS;
    bdeskui_conf_get("fullscreen", screen_list, sizeof(screen_list));
    bdeskui_conf_get("tty", tty_list, sizeof(tty_list));
    settings_load();
    gfx_wallpaper();
    mouse_read(&mouse); /* discard motion from before we started */
    for (int i = 1; i < argc; i++) /* "bdeskui terminal notes" opens those apps */
        open_by_name(argv[i]);

    while (wm_exit_code() < 0) {
        int key, speed = settings_mouse_speed();

        mouse_read(&mouse);
        if (mouse.dx || mouse.dy || mouse.buttons != buttons) {
            x += mouse.dx * speed;
            y += mouse.dy * speed;
            x = x < 0 ? 0 : x >= SCREEN_W ? SCREEN_W - 1 : x;
            y = y < 0 ? 0 : y >= SCREEN_H ? SCREEN_H - 1 : y;
            buttons = mouse.buttons;
            wm_mouse(x, y, buttons);
            dirty = 1;
        }
        while ((key = poll_key()) != 0) {
            wm_key(key);
            dirty = 1;
        }
        if (++frames % SHUTDOWN_POLL_FRAMES == 0 && !told_shutdown && shutting_down()) {
            told_shutdown = 1; /* started elsewhere: a Terminal 'poweroff', the power button */
            wm_shutdown_started();
            dirty = 1;
        }
        if (wm_tick())
            dirty = 1;
        if (dirty && wm_exit_code() < 0) {
            wm_render(x, y);
            dirty = 0;
        }
        sleep_ms(FRAME_MS);
    }
    graphics_end();
    console_clear();
    return wm_exit_code();
}
