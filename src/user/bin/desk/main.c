/* desk: the BShitOS windowed environment. */
#include "bs.h"
#include "desk.h"

#define FRAME_MS 15
#define CONF_PATH "/conf/desk.conf"
#define LIST_MAX 128
#define DEFAULT_FULLSCREEN "edit web cube"

static char fullscreen_list[LIST_MAX] = DEFAULT_FULLSCREEN;

int desk_is_fullscreen(const char *program)
{
    unsigned int length = strlen(program);
    const char *p = fullscreen_list;

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

void desk_run_fullscreen(int argc, char **argv)
{
    char path[BS_PATH_MAX];
    unsigned int length = strlen(argv[0]);
    int code;

    if (length + 10 > sizeof(path))
        return;
    memcpy(path, "/bin/", 5);
    memcpy(path + 5, argv[0], length);
    memcpy(path + 5 + length, ".bs", 4);

    graphics_end();
    console_clear();
    code = spawn(path, argc, argv);
    console_clear();
    if (code < 0) {
        /* Shown in the terminal, where the user can see it. */
        char message[96];

        copy_string(message, sizeof(message), "echo could not start ");
        copy_string(message + strlen(message), sizeof(message) - strlen(message), argv[0]);
        terminal_run(message);
    }
}

int main(void)
{
    int x = SCREEN_W / 2, y = SCREEN_H / 2, dirty = 1;
    unsigned int buttons = 0;
    struct bs_mouse mouse;

    conf_get(CONF_PATH, "fullscreen", fullscreen_list, sizeof(fullscreen_list));
    settings_load();
    mouse_read(&mouse); /* discard motion from before we started */

    while (!wm_wants_exit()) {
        int key, speed = settings_mouse_speed();

        mouse_read(&mouse);
        if (mouse.dx || mouse.dy || mouse.buttons != buttons) {
            x += mouse.dx * speed;
            y += mouse.dy * speed;
            if (x < 0)
                x = 0;
            if (y < 0)
                y = 0;
            if (x >= SCREEN_W)
                x = SCREEN_W - 1;
            if (y >= SCREEN_H)
                y = SCREEN_H - 1;
            buttons = mouse.buttons;
            wm_mouse(x, y, buttons);
            dirty = 1;
        }
        while ((key = poll_key()) != 0) {
            wm_key(key);
            dirty = 1;
        }
        if (wm_tick())
            dirty = 1;
        if (dirty) {
            wm_render(x, y);
            dirty = 0;
        }
        sleep_ms(FRAME_MS);
    }
    graphics_end();
    return 0;
}
