#include "actions/actions.h"
#include "audio/audio.h"
#include "config.h"
#include "lib/lib.h"

#define SEGMENT_BYTES (64u * 1024u)
#define SEGMENTS 4u
#define BAR_WIDTH 48u
#define POLL_US 2000u

#define BLOCK_FULL  0x2588
#define BLOCK_LIGHT 0x2591

static EFI_FILE_PROTOCOL *track;
static UINT8 *ring;

/* Pads with silence past the end of the track, or after a read error. */
static void fill_segment(UINT32 segment)
{
    UINT8 *dst = ring + segment * SEGMENT_BYTES;
    UINTN got = SEGMENT_BYTES;

    if (EFI_ERROR(track->Read(track, &got, dst)))
        got = 0;
    if (got < SEGMENT_BYTES)
        memset(dst + got, 0, SEGMENT_BYTES - got);
}

static void print_time(UINT32 seconds)
{
    con_print_u32(seconds / 60, 2);
    con_print(L":");
    con_print_u32(seconds % 60, 2);
}

static void draw_progress(UINT32 row, UINT32 left, UINT32 done, UINT32 total, UINT32 bytes_per_second)
{
    UINT32 filled = (UINT64)done * BAR_WIDTH / total;

    con_goto(left, row);
    con_color(CON_ACCENT);
    con_repeat(BLOCK_FULL, filled);
    con_color(CON_DIM);
    con_repeat(BLOCK_LIGHT, BAR_WIDTH - filled);

    con_goto(left, row + 1);
    con_color(CON_NORMAL);
    print_time(done / bytes_per_second);
    con_print(L" / ");
    print_time(total / bytes_per_second);
}

void action_play_dnb(void)
{
    const struct audio_format format = {BSBOOT_DNB_RATE, BSBOOT_DNB_CHANNELS, BSBOOT_DNB_BITS};
    const UINT32 frame_bytes = format.channels * format.bits / 8;
    const UINT32 bytes_per_second = format.rate * frame_bytes;
    const struct audio_driver *driver;
    EFI_INPUT_KEY key;
    UINT64 size;
    UINT32 total, retired = 0, next = 0, last_drawn = ~0u;
    UINTN cols, rows;
    UINT32 row, left;
    EFI_STATUS status;

    status = file_open(BSBOOT_DNB_PATH, &track);
    if (EFI_ERROR(status)) {
        con_error(L"Could not open the track", BSBOOT_DNB_PATH, status);
        return;
    }
    status = file_size(track, &size);
    if (EFI_ERROR(status) || size < frame_bytes || size > 0xFFFFFFFFu) {
        con_error(L"The track is empty or unreadable", BSBOOT_DNB_PATH, status);
        goto close_track;
    }
    total = (UINT32)size - (UINT32)size % frame_bytes;

    status = audio_open(&format, SEGMENT_BYTES * SEGMENTS, &ring, &driver);
    if (EFI_ERROR(status)) {
        con_error(L"No usable sound hardware found", L"BSBOOT needs an Intel HD Audio controller", status);
        goto close_track;
    }

    con_clear();
    con_size(&cols, &rows);
    row = rows / 2;
    left = (cols - BAR_WIDTH) / 2;
    con_center(row - 3, L"now playing some sick D&B", CON_TITLE);
    con_center(row + 3, L"Esc: stop", CON_DIM);

    for (UINT32 i = 0; i < SEGMENTS; i++)
        fill_segment(i);
    driver->start();

    for (;;) {
        UINT32 position = driver->position();
        UINT32 current = position / SEGMENT_BYTES;
        UINT32 heard, tick;

        if (con_poll_key(&key) && key.ScanCode == SCAN_ESC)
            break;

        /* Refill each segment the hardware has moved past. */
        while (current < SEGMENTS && next != current) {
            fill_segment(next);
            next = (next + 1) % SEGMENTS;
            retired += SEGMENT_BYTES;
        }

        heard = retired + (position % SEGMENT_BYTES);
        if (heard >= total)
            break;

        tick = heard / (bytes_per_second / 4);
        if (tick != last_drawn) {
            last_drawn = tick;
            draw_progress(row, left, heard, total, bytes_per_second);
        }
        gBS->Stall(POLL_US);
    }

    driver->close();
close_track:
    track->Close(track);
}
