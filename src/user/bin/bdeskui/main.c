/*
 * bdeskui: the BDeskUI operating environment, on top of the BShitOS TTY.
 *
 *   bdeskui --login [--auto user]   the graphical login, then that user's session
 *                                   (started by login, as root)
 *   bdeskui --autostart             from /conf/autoexec.bsh after a text login
 *   bdeskui [app...]                a session for whoever runs it
 *
 * Exits with EXIT_LOGOUT when the session ends, EXIT_TTY_LOGIN when the greeter's
 * TTY button was pressed, EXIT_NO_GRAPHICS without a graphics mode.
 */
#include "bdeskui.h"

#define FRAME_MS 15
#define SHUTDOWN_POLL_FRAMES 30 /* about half a second */

/* Case-insensitive, so "bsnote" finds BSnote. */
static void open_by_name(const char *wanted)
{
    static const struct app *const apps[] = {&app_files, &app_notes, &app_system, &app_settings};

    if ((wanted[0] | 0x20) == 'b' && (wanted[1] | 0x20) == 's' && strcmp(wanted + 2, "term") == 0) {
        terminal_new(0);
        return;
    }
    for (unsigned int a = 0; a < sizeof(apps) / sizeof(apps[0]); a++) {
        const char *p = apps[a]->name, *q = wanted;

        while (*p && (*p | 0x20) == (*q | 0x20))
            p++, q++;
        if (!*p && !*q)
            wm_open(apps[a], 0, 0);
    }
}

/*
 * The layout: the screen divided by the biggest whole scale that leaves at
 * least 640x400, so it fills the screen exactly. Returns 0 if there is no
 * graphics mode or the screen is too small.
 */
static int pick_layout(int quiet)
{
    struct bs_sysinfo info;
    int scale;

    viewport(0, 0, 0, 0); /* the whole screen, whatever was left before */
    sysinfo(&info);
    if (info.screen_width < MIN_W || info.screen_height < MIN_H) {
        if (!quiet && info.screen_width == 0)
            printf("\033[93mbdeskui: no graphics mode here (VGA text); staying in the TTY\033[0m\n");
        else if (!quiet)
            printf("\033[93mbdeskui: needs %ux%u, the screen is %ux%u; staying in the TTY\033[0m\n", MIN_W, MIN_H,
                   info.screen_width, info.screen_height);
        return 0;
    }
    scale = (int)(info.screen_width / MIN_W < info.screen_height / MIN_H ? info.screen_width / MIN_W
                                                                          : info.screen_height / MIN_H);
    while ((int)info.screen_width / scale > MAX_W || (int)info.screen_height / scale > MAX_H)
        scale++;
    gfx_init((int)info.screen_width / scale, (int)info.screen_height / scale);
    return 1;
}

/* With the graphical login on, a text login means someone chose the TTY. */
static int graphical_login_enabled(void)
{
    char value[8];

    return conf_get("/conf/login.conf", "graphical", value, sizeof(value)) != 0 || strcmp(value, "no") != 0;
}

static int session(void)
{
    int x = SCREEN_W / 2, y = SCREEN_H / 2, dirty = 1, frames = 0, told_shutdown = 0;
    unsigned int buttons = 0;
    struct bs_mouse mouse;

    mouse_read(&mouse); /* discard motion from before */
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
            told_shutdown = 1; /* started elsewhere: a terminal's poweroff, the power button */
            wm_shutdown_started();
            dirty = 1;
        }
        if (wm_tick())
            dirty = 1;
        if (dirty && wm_exit_code() < 0) {
            wm_render(x, y, 1);
            dirty = 0;
        }
        sleep_ms(FRAME_MS);
    }
    return wm_exit_code();
}

int main(int argc, char **argv)
{
    const char *auto_user = 0;
    int greeter = 0, code;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--login") == 0) {
            greeter = 1;
        } else if (strcmp(argv[i], "--auto") == 0 && i + 1 < argc) {
            auto_user = argv[++i];
        } else if (strcmp(argv[i], "--autostart") == 0) {
            char value[8];
            struct bs_sysinfo info;

            sysinfo(&info);
            if (info.screen_width && graphical_login_enabled())
                return EXIT_TO_TTY; /* they came through the TTY login on purpose */
            if (bdeskui_conf_get("autostart", value, sizeof(value)) == 0 && strcmp(value, "no") == 0)
                return EXIT_TO_TTY;
        }
    }
    if (!pick_layout(greeter))
        return greeter ? EXIT_TTY_LOGIN : EXIT_NO_GRAPHICS;

    settings_load(); /* the system's skin, for the login screen */
    if (greeter) {
        code = greeter_run(auto_user);
        if (code != 0) { /* the TTY button, or shutting down: back to text */
            graphics_end();
            console_clear();
            return code;
        }
        bdeskui_forget_user(); /* this process is someone now */
        chdir(bdeskui_home());
        settings_load(); /* their skin */
    }
    terminal_load_conf();
    wm_reset();
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-' && (i == 1 || strcmp(argv[i - 1], "--auto") != 0))
            open_by_name(argv[i]);
    code = session();
    graphics_end();
    viewport(0, 0, 0, 0);
    console_clear();
    return code;
}
