/*
 * Audio output abstraction. A driver plays a DMA ring forever and reports where
 * it is; callers keep the ring topped up. To support new hardware, implement
 * struct audio_driver and list it in audio.c.
 */
#ifndef BSBOOT_AUDIO_H
#define BSBOOT_AUDIO_H

#include "efi/efi.h"

struct audio_format {
    UINT32 rate;
    UINT8 channels;
    UINT8 bits;
};

struct audio_driver {
    const CHAR16 *name;
    /* Finds hardware and allocates a zeroed ring of ring_bytes. */
    EFI_STATUS (*open)(const struct audio_format *format, UINT32 ring_bytes, UINT8 **ring);
    void (*start)(void);
    /* Byte offset within the ring currently being played. */
    UINT32 (*position)(void);
    /* Stops playback and releases the ring. */
    void (*close)(void);
};

/* Returns the first driver able to play format, with it already open. */
EFI_STATUS audio_open(const struct audio_format *format, UINT32 ring_bytes, UINT8 **ring,
                      const struct audio_driver **driver);

extern const struct audio_driver hda_driver;

#endif
