#include "lib/lib.h"

#define DP_END_TYPE 0x7F
#define DP_MESSAGING_TYPE 0x03
#define DP_MESSAGING_UART 0x0E

static BOOLEAN path_has_uart(const UINT8 *node)
{
    for (; node[0] != DP_END_TYPE; node += node[2] | (UINTN)node[3] << 8)
        if (node[0] == DP_MESSAGING_TYPE && node[1] == DP_MESSAGING_UART)
            return 1;
    return 0;
}

void con_detach_serial(void)
{
    static const EFI_GUID text_out_guid = EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
    static const EFI_GUID device_path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
    EFI_HANDLE *handles;
    UINTN count;

    if (EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &text_out_guid, 0, &count, &handles)))
        return;
    for (UINTN i = 0; i < count; i++) {
        void *path;

        /* The firmware's combined console has no device path and is skipped. */
        if (!EFI_ERROR(gBS->HandleProtocol(handles[i], &device_path_guid, &path)) &&
            path_has_uart(path))
            gBS->DisconnectController(handles[i], 0, 0);
    }
    gBS->FreePool(handles);
}

void con_clear(void)
{
    gST->ConOut->SetAttribute(gST->ConOut, CON_NORMAL);
    gST->ConOut->ClearScreen(gST->ConOut);
    gST->ConOut->EnableCursor(gST->ConOut, 0);
}

void con_size(UINTN *cols, UINTN *rows)
{
    if (EFI_ERROR(gST->ConOut->QueryMode(gST->ConOut, gST->ConOut->Mode->Mode, cols, rows))) {
        *cols = 80;
        *rows = 25;
    }
}

void con_goto(UINTN col, UINTN row)
{
    gST->ConOut->SetCursorPosition(gST->ConOut, col, row);
}

void con_color(UINTN attr)
{
    gST->ConOut->SetAttribute(gST->ConOut, attr);
}

void con_print(const CHAR16 *s)
{
    gST->ConOut->OutputString(gST->ConOut, (CHAR16 *)s);
}

void con_print_u32(UINT32 value, UINTN min_digits)
{
    CHAR16 buf[11];
    UINTN i = 10;

    buf[i] = 0;
    do {
        buf[--i] = L'0' + value % 10;
        value /= 10;
    } while (value || 10 - i < min_digits);
    con_print(&buf[i]);
}

static void con_print_hex(UINT32 value)
{
    CHAR16 buf[9];

    for (int i = 7; i >= 0; i--, value >>= 4)
        buf[i] = L"0123456789ABCDEF"[value & 0xF];
    buf[8] = 0;
    con_print(buf);
}

void con_repeat(CHAR16 c, UINTN count)
{
    CHAR16 buf[2] = {c, 0};

    while (count--)
        con_print(buf);
}

UINTN con_strlen(const CHAR16 *s)
{
    UINTN n = 0;

    while (s[n])
        n++;
    return n;
}

void con_center(UINTN row, const CHAR16 *s, UINTN attr)
{
    UINTN cols, rows, len = con_strlen(s);

    con_size(&cols, &rows);
    con_goto(len < cols ? (cols - len) / 2 : 0, row);
    con_color(attr);
    con_print(s);
}

EFI_INPUT_KEY con_wait_key(void)
{
    EFI_INPUT_KEY key;
    UINTN index;

    while (EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &key)))
        gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &index);
    return key;
}

BOOLEAN con_poll_key(EFI_INPUT_KEY *key)
{
    return !EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, key));
}

void con_error(const CHAR16 *what, const CHAR16 *detail, EFI_STATUS status)
{
    UINTN cols, rows;

    con_clear();
    con_size(&cols, &rows);
    con_center(rows / 2 - 2, what, CON_ERROR);
    if (detail)
        con_center(rows / 2, detail, CON_NORMAL);
    if (EFI_ERROR(status)) {
        con_goto(cols / 2 - 12, rows / 2 + 1);
        con_color(CON_DIM);
        con_print(L"EFI status 0x");
        con_print_hex(status);
    }
    con_center(rows / 2 + 3, L"Press any key to return", CON_DIM);
    con_wait_key();
}
