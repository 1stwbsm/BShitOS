/* Desk: the BShitOS windowed environment. Shared definitions. */
#ifndef DESK_H
#define DESK_H

#define SCREEN_W 640
#define SCREEN_H 400
#define CHAR_W 6
#define CHAR_H 8
#define LINE_H 10
#define RAIL_H 18
#define KEY_ESCAPE 0x1B
#define CTRL(c) ((c) & 0x1F)

struct theme {
    const char *name;
    unsigned int desk, window, text, dim, line, rail, rail_text, select, field;
};

extern const struct theme *theme;

/* ---- Drawing (gfx.c). Coordinates are relative to the current clip. ---- */
void gfx_clip(int x, int y, int w, int h);
void gfx_unclip(void);
void gfx_fill(int x, int y, int w, int h, unsigned int color);
void gfx_text(int x, int y, const char *s, unsigned int color);
void gfx_text_n(int x, int y, const char *s, int count, unsigned int color);
void gfx_text_scaled(int x, int y, const char *s, int scale, unsigned int color);
void gfx_outline(int x, int y, int w, int h, unsigned int color);
void gfx_button(int x, int y, int w, int h, const char *label, int highlighted);
void gfx_bar(int x, int y, int w, int h, unsigned int used, unsigned int total, unsigned int color);
int gfx_inside(int px, int py, int x, int y, int w, int h);
enum pointer_kind { POINTER_ARROW, POINTER_RESIZE, POINTER_MOVE };
void gfx_pointer(int x, int y, enum pointer_kind kind);
void gfx_present(void);

/* ---- Apps ---------------------------------------------------------------- */
struct app {
    const char *name, *blurb;
    unsigned int accent;
    short default_w, default_h, min_w, min_h;
    void (*open)(const char *arg);            /* may be 0; runs on every open request */
    void (*draw)(int w, int h, int focused);   /* the content area, at (0,0) */
    void (*key)(int key);                      /* may be 0 */
    void (*press)(int x, int y, int w, int h); /* may be 0 */
    void (*drag)(int x, int y, int w, int h);  /* may be 0 */
    int (*tick)(void);                         /* may be 0; returns nonzero to redraw */
};

extern const struct app app_terminal, app_files, app_notes, app_system, app_settings;

/* ---- Window manager (wm.c) ----------------------------------------------- */
void wm_open(const struct app *app, const char *arg);
int wm_frame(void); /* advances once per frame; used for blinking and double clicks */
void wm_mouse(int x, int y, unsigned int buttons);
void wm_key(int key);
int wm_tick(void);
void wm_render(int mouse_x, int mouse_y);
int wm_wants_exit(void);
void wm_request_exit(void);

/* ---- Shared actions -------------------------------------------------------- */
/* Hands the whole screen to a program (web, cube, edit) until it exits. */
void desk_run_fullscreen(int argc, char **argv);
/* Opens the terminal and runs a command in it. */
void terminal_run(const char *command);
/* Opens a text file in Notes. */
void notes_open(const char *path);
void settings_load(void);
/* Settings come from ~/.desk.conf, then /conf/desk.conf; changes go to ~/.desk.conf. */
int desk_conf_get(const char *key, char *out, unsigned int size);
int desk_conf_set(const char *key, const char *value);
const char *desk_home(void);
int settings_mouse_speed(void);
/* Programs that need the whole screen, from /conf/desk.conf. */
int desk_is_fullscreen(const char *program);

/* Small string helpers */
void copy_string(char *out, unsigned int size, const char *text);
int ends_with(const char *text, const char *suffix);

#endif
