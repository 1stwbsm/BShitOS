/*
 * BDeskUI: the operating environment that runs on top of BShitOS. Windows, a
 * taskbar and desktop icons, drawn as glossy gel and glass held in heavy black
 * frames with hard shadows. Shared definitions.
 */
#ifndef BDESKUI_H
#define BDESKUI_H

#include "bs.h"

#define SCREEN_W 640
#define SCREEN_H 400
#define CHAR_W 6
#define CHAR_H 8
#define LINE_H 10
#define TASKBAR_H 26
#define KEY_ESCAPE 0x1B
#define CTRL(c) ((c) & 0x1F)

/* Exit codes, read by the login shell's /conf/autoexec.bsh. */
#define EXIT_LOGOUT 0
#define EXIT_NO_GRAPHICS 1
#define EXIT_TO_TTY 3

struct theme {
    const char *name;
    unsigned int sky_top, sky_bottom; /* the wallpaper gradient */
    unsigned int glass_top, glass_bottom; /* taskbar and menu glass */
    unsigned int accent; /* the launch button's gel */
    unsigned int idle; /* title bars of windows without the focus */
    /* inside windows */
    unsigned int window, text, dim, line, select, field;
};

extern const struct theme *theme;

/* ---- Colour --------------------------------------------------------------- */
unsigned int mix(unsigned int a, unsigned int b, int amount); /* amount 0..256 toward b */
#define LIGHTER(c, n) mix((c), 0xFFFFFF, (n))
#define DARKER(c, n) mix((c), 0x000000, (n))
#define INK 0x0B1014u /* the black of frames and outlines */

/* ---- Drawing (gfx.c). Coordinates are relative to the current clip. ------- */
void gfx_clip(int x, int y, int w, int h);
void gfx_unclip(void);
void gfx_fill(int x, int y, int w, int h, unsigned int color);
void gfx_gradient(int x, int y, int w, int h, unsigned int top, unsigned int bottom);
/* Glossy gel: a lit upper half with a highlight band over a deeper lower half. */
void gfx_gel(int x, int y, int w, int h, unsigned int base, int pressed);
/* Smoked glass: a gradient with a bright top edge. */
void gfx_glass(int x, int y, int w, int h, unsigned int top, unsigned int bottom);
void gfx_bevel(int x, int y, int w, int h, int raised);
void gfx_inset(int x, int y, int w, int h, unsigned int fill); /* a recessed well */
void gfx_outline(int x, int y, int w, int h, unsigned int color);
void gfx_frame(int x, int y, int w, int h); /* a 2 px black frame */
void gfx_shadow(int x, int y, int w, int h); /* hard shadow, offset down-right */
void gfx_darken(int x, int y, int w, int h, int amount);
/* A framed gel slab with cut corners: buttons, tabs, the launch button. */
void gfx_slab(int x, int y, int w, int h, unsigned int base, int pressed);
void gfx_text(int x, int y, const char *s, unsigned int color);
void gfx_text_n(int x, int y, const char *s, int count, unsigned int color);
void gfx_text_bold(int x, int y, const char *s, unsigned int color);
void gfx_text_raised(int x, int y, const char *s, unsigned int color); /* bold, with a dark drop */
void gfx_text_scaled(int x, int y, const char *s, int scale, unsigned int color);
void gfx_button(int x, int y, int w, int h, const char *label, int highlighted);
void gfx_bar(int x, int y, int w, int h, unsigned int used, unsigned int total, unsigned int color);
int gfx_inside(int px, int py, int x, int y, int w, int h);

enum icon {
    ICON_TERMINAL, ICON_FILES, ICON_NOTES, ICON_WEB, ICON_SYSTEM, ICON_SETTINGS,
    ICON_CUBE, ICON_FOLDER, ICON_FILE, ICON_PROGRAM, ICON_POWER, ICON_COUNT
};
void gfx_icon(int x, int y, enum icon which, int scale); /* 16x16 times scale */

void gfx_wallpaper(void); /* redraws the cached wallpaper for the current theme */
void gfx_draw_wallpaper(int top, int height);
enum pointer_kind { POINTER_ARROW, POINTER_RESIZE, POINTER_MOVE };
void gfx_pointer(int x, int y, enum pointer_kind kind);
void gfx_present(void);

/* ---- Apps ----------------------------------------------------------------- */
struct app {
    const char *name, *blurb;
    unsigned int accent;
    enum icon icon;
    short default_w, default_h, min_w, min_h;
    void (*open)(const char *arg);            /* may be 0; runs on every open request */
    void (*draw)(int w, int h, int focused);   /* the content area, at (0,0) */
    void (*key)(int key);                      /* may be 0 */
    void (*press)(int x, int y, int w, int h); /* may be 0 */
    void (*drag)(int x, int y, int w, int h);  /* may be 0 */
    int (*tick)(void);                         /* may be 0; returns nonzero to redraw */
    int (*unsaved)(void);                      /* may be 0; nonzero if leaving loses work */
    int (*save)(void);                         /* may be 0; 0 on success */
};

extern const struct app app_terminal, app_files, app_notes, app_system, app_settings;

/* ---- Window manager (wm.c) ------------------------------------------------ */
void wm_open(const struct app *app, const char *arg);
int wm_frame(void); /* advances once per frame; used for blinking and double clicks */
void wm_mouse(int x, int y, unsigned int buttons);
void wm_key(int key);
int wm_tick(void);
void wm_render(int mouse_x, int mouse_y);
int wm_exit_code(void); /* -1 while running */
void wm_leave(int code); /* asks about unsaved work, then exits with code */
void wm_shutdown_started(void); /* someone else started a shutdown */

/* ---- Elsewhere ------------------------------------------------------------ */
void bdeskui_run_program(int argc, char **argv); /* full screen or TTY, by /conf/bdeskui.conf */
int bdeskui_needs_screen(const char *program);
int bdeskui_needs_tty(const char *program);
void terminal_run(const char *command);
void notes_open(const char *file);
void settings_load(void);
int settings_mouse_speed(void);
int bdeskui_conf_get(const char *key, char *out, unsigned int size);
int bdeskui_conf_set(const char *key, const char *value);
const char *bdeskui_home(void);
const char *bdeskui_user(void);
void copy_string(char *out, unsigned int size, const char *text);
int ends_with(const char *text, const char *suffix);

#endif
