#include "audio/audio.h"

static const struct audio_driver *const drivers[] = {
    &hda_driver,
};

EFI_STATUS audio_open(const struct audio_format *format, UINT32 ring_bytes, UINT8 **ring,
                      const struct audio_driver **driver)
{
    EFI_STATUS status = EFI_NOT_FOUND;

    for (UINTN i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++) {
        status = drivers[i]->open(format, ring_bytes, ring);
        if (!EFI_ERROR(status)) {
            *driver = drivers[i];
            return status;
        }
    }
    return status;
}
