#!/bin/sh
# Builds if needed, then boots out/BShitOS.img in QEMU. Extra arguments are
# passed through to QEMU.
#
# BSHIT_AUDIODEV overrides the audio backend, e.g.
#   BSHIT_AUDIODEV=wav,id=snd,path=capture.wav ./STARTVM.sh
set -eu

cd "$(dirname "$0")"

IMAGE=out/BShitOS.img
FIRMWARE=${BSHIT_FIRMWARE:-/usr/share/qemu/edk2-i386-code.fd}
VARS_TEMPLATE=${BSHIT_VARS:-/usr/share/qemu/edk2-i386-vars.fd}
VARS=build/efivars.fd

# Only rebuilds when sources changed. Files in /userspace are carried over into
# the new disk; everything else is rebuilt.
# ./STARTVM.sh --live boots the live ISO as a CD instead of the hard disk.
LIVE_ISO=out/BShitOS-live.iso
if [ "${1:-}" = "--live" ]; then
    shift
    make -j"$(nproc)" --no-print-directory iso
    STORAGE="-cdrom $LIVE_ISO"
else
    make -j"$(nproc)" --no-print-directory
    STORAGE="-drive if=none,id=disk,format=raw,file=$IMAGE -device ide-hd,drive=disk,bootindex=1"
fi

for file in "$FIRMWARE" "$VARS_TEMPLATE"; do
    if [ ! -f "$file" ]; then
        echo "STARTVM: firmware file $file not found (set BSHIT_FIRMWARE / BSHIT_VARS)" >&2
        exit 1
    fi
done

[ -f "$VARS" ] || cp "$VARS_TEMPLATE" "$VARS"

if [ -z "${BSHIT_AUDIODEV:-}" ]; then
    if command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1; then
        BSHIT_AUDIODEV=pa,id=snd
    else
        BSHIT_AUDIODEV=alsa,id=snd
    fi
fi

ACCEL=tcg
if [ -w /dev/kvm ]; then
    ACCEL=kvm
fi

exec qemu-system-i386 \
    -name BShitOS \
    -machine q35,pcspk-audiodev=snd \
    -accel "$ACCEL" \
    -m 512M \
    -drive if=pflash,format=raw,unit=0,readonly=on,file="$FIRMWARE" \
    -drive if=pflash,format=raw,unit=1,file="$VARS" \
    $STORAGE \
    -netdev user,id=net0 \
    -device e1000,netdev=net0 \
    -device qemu-xhci,id=xhci \
    -audiodev "$BSHIT_AUDIODEV" \
    -device ich9-intel-hda,addr=0x1b \
    -device hda-output,audiodev=snd \
    -serial stdio \
    "$@"
