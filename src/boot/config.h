/* Build-time settings for BSBOOT. */
#ifndef BSBOOT_CONFIG_H
#define BSBOOT_CONFIG_H

/* Read from the BSFS root partition, not from the boot volume. */
#define BSBOOT_KERNEL_PATH  "/boot/bskern.elf"
#define BSBOOT_KERNEL_LABEL L"/boot/bskern.elf"

/* On a live CD the root filesystem is this file on the boot volume. */
#define BSBOOT_LIVE_ROOT L"\\LIVE\\ROOTFS.IMG"

/* User-supplied shell; the shell built into the firmware is the fallback. */
#define BSBOOT_SHELL_PATH  L"\\EFI\\TOOLS\\SHELL.EFI"

/* res/dnb.pcm is headerless, so its format has to be stated here. */
#define BSBOOT_DNB_PATH     L"\\RES\\DNB.PCM"
#define BSBOOT_DNB_RATE     48000
#define BSBOOT_DNB_CHANNELS 2
#define BSBOOT_DNB_BITS     16

#endif
