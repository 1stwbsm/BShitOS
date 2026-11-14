#include "lib/lib.h"
#include "ui/menu.h"

#define BOX_WIDTH 50 /* inner width */

#define BOX_H  0x2500
#define BOX_V  0x2502
#define BOX_TL 0x250C
#define BOX_TR 0x2510
#define BOX_BL 0x2514
#define BOX_BR 0x2518

struct layout {
    UINTN left, top;
};

static void draw_border_row(const struct layout *l, UINTN row, CHAR16 left, CHAR16 right)
{
    CHAR16 c[2] = {left, 0};

    con_goto(l->left, row);
    con_color(CON_TITLE);
    con_print(c);
    con_repeat(BOX_H, BOX_WIDTH);
    c[0] = right;
    con_print(c);
}

static void draw_entry(const struct layout *l, const struct menu_entry *entry, UINTN index, BOOLEAN selected)
{
    UINTN attr = selected ? CON_SELECTED : CON_NORMAL;
    UINTN pad = BOX_WIDTH - 4 - con_strlen(entry->label);

    con_goto(l->left + 1, l->top + 1 + index);
    con_color(attr);
    con_print(selected ? L" > " : L"   ");
    con_print(entry->label);
    con_repeat(L' ', pad + 1);
}

static void draw_all(struct layout *l, const struct menu_entry *entries, UINTN count, UINTN selected)
{
    CHAR16 v[2] = {BOX_V, 0};
    UINTN cols, rows;

    con_clear();
    con_size(&cols, &rows);
    l->left = (cols - BOX_WIDTH - 2) / 2;
    l->top = rows > count + 8 ? (rows - count - 2) / 2 : 3;

    con_center(l->top - 3, L"BSBOOT", CON_ACCENT);
    con_center(l->top - 2, L"the BShitOS bootloader", CON_DIM);

    draw_border_row(l, l->top, BOX_TL, BOX_TR);
    for (UINTN i = 0; i < count; i++) {
        con_color(CON_TITLE);
        con_goto(l->left, l->top + 1 + i);
        con_print(v);
        con_goto(l->left + BOX_WIDTH + 1, l->top + 1 + i);
        con_print(v);
        draw_entry(l, &entries[i], i, i == selected);
    }
    draw_border_row(l, l->top + count + 1, BOX_BL, BOX_BR);

    con_center(l->top + count + 3, L"Up/Down: choose    Enter: run", CON_DIM);
}

void menu_run(const struct menu_entry *entries, UINTN count)
{
    struct layout layout;
    UINTN selected = 0;

    draw_all(&layout, entries, count, selected);
    for (;;) {
        EFI_INPUT_KEY key = con_wait_key();
        UINTN previous = selected;

        if (key.ScanCode == SCAN_UP)
            selected = (selected + count - 1) % count;
        else if (key.ScanCode == SCAN_DOWN)
            selected = (selected + 1) % count;
        else if (key.UnicodeChar == L'\r') {
            entries[selected].action();
            draw_all(&layout, entries, count, selected);
        }

        if (selected != previous) {
            draw_entry(&layout, &entries[previous], previous, 0);
            draw_entry(&layout, &entries[selected], selected, 1);
        }
    }
}
