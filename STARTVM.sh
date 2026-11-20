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
#
#   ./STARTVM.sh            boot out/BShitOS.img
#   ./STARTVM.sh --live     boot the live ISO, with out/target.img attached as a
#                           blank disk to try the installer on (su -c install)
#   ./STARTVM.sh --target   boot out/target.img, once something is installed on it
#   ./STARTVM.sh --486      boot out/BShitOS.img the old way: a 486 with 8 MiB, BIOS,
#                           an IDE disk and VGA text mode (no BDeskUI: no graphics)
LIVE_ISO=out/BShitOS-live.iso
TARGET=out/target.img
TARGET_SIZE=2G
disk() { echo "-drive if=none,id=disk,format=raw,file=$1 -device ide-hd,drive=disk,bus=ide.0,bootindex=1"; }
case "${1:-}" in
--486)
    shift
    make -j"$(nproc)" --no-print-directory
    exec qemu-system-i386 -name "BShitOS (486)" -M pc -cpu 486 -m 8M -accel tcg \
        -drive file="$IMAGE",format=raw,if=ide -netdev user,id=net0 -device e1000,netdev=net0 \
        -serial stdio "$@"
    ;;
--live)
    shift
    make -j"$(nproc)" --no-print-directory iso
    [ -f "$TARGET" ] || truncate -s "$TARGET_SIZE" "$TARGET"
    STORAGE="$(disk "$TARGET") -drive if=none,id=cd,media=cdrom,file=$LIVE_ISO -device ide-cd,drive=cd,bus=ide.1,bootindex=0"
    ;;
--target)
    shift
    if [ ! -f "$TARGET" ]; then
        echo "STARTVM: $TARGET does not exist yet; install onto it with --live first" >&2
        exit 1
    fi
    STORAGE="$(disk "$TARGET")"
    ;;
*)
    make -j"$(nproc)" --no-print-directory
    STORAGE="$(disk "$IMAGE")"
    ;;
esac

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
