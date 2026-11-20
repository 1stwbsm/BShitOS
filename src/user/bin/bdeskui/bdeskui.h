/*
 * BDeskUI: the operating environment that runs on top of BShitOS. Shared
 * definitions.
 *
 * Modern DOS-like: one program runs at a time. A program started from BDeskUI
 * takes over the inside of a window (the kernel's viewport) while the rest of
 * the desktop waits, and BDeskUI comes back when it exits or detaches.
 *
 * Everything is laid out in "layout pixels"; the screen shows each as scale x
 * scale real pixels, scale picked so the layout fills the screen
 * (1280x800 -> 640x400 at 2x, 1920x1080 -> 960x540 at 2x, 1024x768 -> 1:1).
 */
#ifndef BDESKUI_H
#define BDESKUI_H

#include "bs.h"

extern int screen_w, screen_h; /* the layout, set once at start */
#define SCREEN_W screen_w
#define SCREEN_H screen_h
#define MIN_W 640
#define MIN_H 400
#define MAX_W 1400 /* the biggest layout drawn 1:1; larger screens scale up */
#define MAX_H 900
#define CHAR_W 6
#define CHAR_H 8
#define LINE_H 10
#define KEY_ESCAPE 0x1B
#define CTRL(c) ((c) & 0x1F)

/* Exit codes, read by login (the greeter) and by /conf/autoexec.bsh. */
#define EXIT_LOGOUT 0
#define EXIT_NO_GRAPHICS 1
#define EXIT_TO_TTY 3
#define EXIT_TTY_LOGIN 4 /* the greeter's "TTY" button: login asks on the text console */

/* ---- Colour --------------------------------------------------------------- */
unsigned int mix(unsigned int a, unsigned int b, int amount); /* amount 0..256 toward b */
#define LIGHTER(c, n) mix((c), 0xFFFFFF, (n))
#define DARKER(c, n) mix((c), 0x000000, (n))
#define INK 0x0B1014u /* black, for drop shadows under text */

/* ---- Icons ------------------------------------------------------------------ */
enum icon {
    ICON_TERMINAL, ICON_FILES, ICON_NOTES, ICON_WEB, ICON_SYSTEM, ICON_SETTINGS,
    ICON_CUBE, ICON_FOLDER, ICON_FILE, ICON_PROGRAM, ICON_POWER, ICON_USER, ICON_COUNT
};

/* ---- Skins: everything about how BDeskUI looks -------------------------------- */
enum icon_style {
    ICONS_GLOSSY,  /* full colour with a sheen */
    ICONS_LINE,    /* light line art on dark, colour only as a hint */
    ICONS_EGA,     /* the 16 VGA colours, no shading */
    ICONS_PICTO,   /* black pictograms with one accent colour */
};
enum pointer_style { POINTER_CLASSIC, POINTER_BLOCK, POINTER_HEAVY };
enum pointer_kind { POINTER_ARROW, POINTER_RESIZE, POINTER_MOVE };

/* Where the window manager put a window's parts; skins only draw them. */
struct frame_info {
    int x, y, w, h;                   /* the whole window */
    int title_x, title_y, title_w;    /* the title bar (title_h tall) */
    int content_x, content_y, content_w, content_h;
    int button_x[3];                  /* minimise, maximise, close: title_button_w wide */
    const char *title;
    enum icon icon;
    int focused;
};

#define TASK_MAX 8
struct bar_info {
    int y, width;                     /* the taskbar's top and width; taskbar_h tall */
    int menu_open;
    int count;
    struct {
        int x, w, name_w;             /* the entry, and its name part (buttons follow it) */
        const char *name;
        enum icon icon;
        int active, minimized;
    } entries[TASK_MAX];
    const char *clock, *address;
};

struct skin {
    const char *name, *blurb;
    /* colours; every background is one solid colour */
    unsigned int background;          /* the desktop and the login screen */
    unsigned int chrome, chrome_text; /* window frames, title bars, the taskbar */
    unsigned int line;                /* outlines and rules */
    unsigned int accent;              /* the launcher and anything that should stand out */
    unsigned int idle;                /* text of things without the focus */
    unsigned int tile;                /* desktop icon tiles */
    unsigned int window, text, dim, rule, select, field, button; /* inside windows */
    int gloss;                        /* sheen on chrome, 0..256; 0 is flat */
    /* metrics, in layout pixels */
    int frame;                        /* the border around the content (and above the title bar) */
    int title_h, gap;                 /* the title bar, and the rule under it */
    int title_button_w;
    int taskbar_h, taskbar_top;       /* taskbar_top: the bar is at the top of the screen */
    int launcher_w;
    int task_buttons;                 /* 2: each taskbar entry has ^ and X; 0: name only */
    int tile_size;                    /* desktop tiles (0: icons stand on the background) */
    enum icon_style icons;
    enum pointer_style pointer;
    /* drawing */
    void (*draw_window)(const struct frame_info *f);
    void (*draw_taskbar)(const struct bar_info *b);
    void (*draw_tile)(int x, int y, int size, enum icon icon, const char *label, int selected);
    void (*draw_panel)(int x, int y, int w, int h, const char *title); /* menus, dialogs, login */
    void (*draw_button)(int x, int y, int w, int h, const char *label, int hot);
    void (*draw_field)(int x, int y, int w, int h, int focused);
};

extern const struct skin *skin;
extern const struct skin *const skins[];
extern const int skin_count;
const struct skin *skin_named(const char *name); /* 0 if there is none */
int panel_title_h(void); /* the title band skins draw at the top of a panel */

/* ---- Drawing (gfx.c). Coordinates are relative to the current clip. ------- */
void gfx_init(int width, int height); /* the layout size, at start */
void gfx_clip(int x, int y, int w, int h);
void gfx_unclip(void);
void gfx_origin(int *x, int *y); /* where (0,0) currently is, in layout pixels */
void gfx_fill(int x, int y, int w, int h, unsigned int color);
void gfx_gradient(int x, int y, int w, int h, unsigned int top, unsigned int bottom);
void gfx_gel(int x, int y, int w, int h, unsigned int base, int pressed); /* glossy, for buttons */
void gfx_chrome(int x, int y, int w, int h, unsigned int base, int pressed); /* sheen as strong as the skin's gloss */
void gfx_inset(int x, int y, int w, int h, unsigned int fill); /* a recessed well */
void gfx_outline(int x, int y, int w, int h, unsigned int color);
void gfx_frame(int x, int y, int w, int h); /* a 2 px frame in the skin's line colour */
void gfx_line(int x0, int y0, int x1, int y1, int thickness, unsigned int color);
void gfx_disc(int cx, int cy, int r, unsigned int color);
void gfx_shadow(int x, int y, int w, int h, int offset, int amount); /* hard, down-right */
void gfx_darken(int x, int y, int w, int h, int amount);
void gfx_slab(int x, int y, int w, int h, unsigned int base, int pressed); /* framed gel with cut corners */
void gfx_text(int x, int y, const char *s, unsigned int color);
void gfx_text_n(int x, int y, const char *s, int count, unsigned int color);
void gfx_text_bold(int x, int y, const char *s, unsigned int color);
void gfx_text_raised(int x, int y, const char *s, unsigned int color); /* bold, with a dark drop */
void gfx_text_scaled(int x, int y, const char *s, int scale, unsigned int color);
void gfx_button(int x, int y, int w, int h, const char *label, int highlighted); /* the skin's */
void gfx_bar(int x, int y, int w, int h, unsigned int used, unsigned int total, unsigned int color);
int gfx_inside(int px, int py, int x, int y, int w, int h);
void gfx_icon(int x, int y, enum icon which, int scale); /* 16x16 times scale, in the skin's style */
void gfx_background(void); /* the skin's solid background, everywhere */
void gfx_pointer(int x, int y, enum pointer_kind kind);
void gfx_present(void);
/* Where layout rectangle x, y, w, h lands on the real screen, in real pixels. */
void gfx_screen_rect(int x, int y, int w, int h, int *sx, int *sy, int *sw, int *sh);
int gfx_text_scale(void); /* the kernel console's cell over a layout cell: 1 or 2 */

/* ---- Apps ----------------------------------------------------------------- */
struct app {
    const char *name, *blurb;
    enum icon icon;
    short default_w, default_h, min_w, min_h;
    int instances;                             /* how many windows at once (0 or 1: one) */
    void (*open)(int instance, const char *arg); /* may be 0; runs on every open request */
    void (*draw)(int instance, int w, int h, int focused); /* the content area, at (0,0) */
    void (*key)(int instance, int key);        /* may be 0 */
    void (*press)(int instance, int x, int y, int w, int h); /* may be 0 */
    void (*drag)(int instance, int x, int y, int w, int h);  /* may be 0 */
    int (*tick)(void);                         /* may be 0; returns nonzero to redraw */
    int (*unsaved)(void);                      /* may be 0; nonzero if leaving loses work */
    int (*save)(void);                         /* may be 0; 0 on success */
    const char *(*title)(int instance);        /* may be 0: the app's name */
    void (*closed)(int instance);              /* may be 0 */
};

extern const struct app app_terminal, app_program, app_files, app_notes, app_system, app_settings;

/* ---- Window manager (wm.c) ------------------------------------------------ */
int wm_open(const struct app *app, int instance, const char *arg); /* returns the window, or -1 */
void wm_close_instance(const struct app *app, int instance);
int wm_frame(void); /* advances once per frame; used for blinking and double clicks */
void wm_mouse(int x, int y, unsigned int buttons);
void wm_key(int key);
int wm_tick(void);
void wm_render(int mouse_x, int mouse_y, int show_pointer);
int wm_exit_code(void); /* -1 while running */
void wm_shutdown_started(void); /* someone else started a shutdown */
/* The real-screen rectangle of an instance's content, for handing it to a program. */
int wm_content_rect(const struct app *app, int instance, int *sx, int *sy, int *sw, int *sh);
void wm_reset(void); /* forget all windows (a new session) */

/* ---- Running programs in windows (term.c) --------------------------------- */
void terminal_new(const char *command); /* a terminal window; command 0 for a shell */
void program_window(const char *program, const char *arg); /* a program with a graphical mode */
void run_full_screen_shell(void); /* "to TTY": bsh on the whole screen until it exits */
int is_windowed_program(const char *program); /* has a graphical mode (bdeskui.conf windowed=) */
void launch_command(const char *line); /* the run field: windowed program or terminal */
void terminal_load_conf(void);

/* ---- Elsewhere ------------------------------------------------------------ */
int greeter_run(const char *auto_user); /* 0 once someone has logged in, else an EXIT_* code */
void notes_open(const char *file);
void settings_load(void);
int settings_mouse_speed(void);
int bdeskui_conf_get(const char *key, char *out, unsigned int size);
int bdeskui_conf_set(const char *key, const char *value);
const char *bdeskui_home(void);
const char *bdeskui_user(void);
void bdeskui_forget_user(void); /* after a login: who we are has changed */
void copy_string(char *out, unsigned int size, const char *text);
int ends_with(const char *text, const char *suffix);

#endif
