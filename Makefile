# BShitOS top-level build. Components live in their own *.mk files and append
# what they produce to ESP_FILES or ROOTFS_FILES; sources are discovered automatically.

BUILD  := build
OUT    := out
ESP    := $(OUT)/esp
ROOTFS := $(OUT)/rootfs
IMAGE  := $(OUT)/BShitOS.img
LIVE_ISO := $(OUT)/BShitOS-live.iso
BSFS_IMAGE := $(BUILD)/bsfs.img
BSFS_SIZE_MIB := 64

COMMON_CFLAGS := -std=gnu11 -O2 -g0 -Wall -Wextra -ffreestanding -fno-stack-protector \
                 -fno-tree-loop-distribute-patterns -mno-sse -mno-mmx -mno-80387 \
                 -MMD -MP -Isrc/include

COMMON_SRCS := $(wildcard src/common/*.c)
ESP_FILES :=
ROOTFS_FILES :=

.PHONY: all clean run iso
all: $(IMAGE)

include src/boot/boot.mk
include src/kernel/kernel.mk
include src/user/user.mk
include src/web/web.mk

# Everything in res/ ships on the boot partition under \RES.
RES_FILES := $(wildcard res/*)
ESP_FILES += $(patsubst res/%,$(ESP)/RES/%,$(RES_FILES))

$(ESP)/RES/%: res/%
	@mkdir -p $(dir $@)
	cp $< $@

# rootfs/ is copied onto BSFS as-is (configuration and other static files).
OVERLAY_FILES := $(shell find rootfs -type f)
ROOTFS_FILES += $(patsubst rootfs/%,$(ROOTFS)/%,$(OVERLAY_FILES))

$(ROOTFS)/%: rootfs/%
	@mkdir -p $(dir $@)
	cp $< $@

# Trusted HTTPS roots, taken from the build host's CA list.
ROOTS := $(ROOTFS)/conf/roots.der
ROOTFS_FILES += $(ROOTS)

$(ROOTS): tools/mkroots.py
	@mkdir -p $(dir $@)
	python3 tools/mkroots.py $@

# /userspace is carried over from the previous disk image, so rebuilding the OS
# keeps your files. Everything else on the disk is rebuilt from the source tree.
CARRY := $(BUILD)/carry

$(BSFS_IMAGE): $(ROOTFS_FILES) tools/mkbsfs.py tools/carryover.py
	@mkdir -p $(dir $@)
	python3 tools/carryover.py $(IMAGE) $(CARRY)
	python3 tools/mkbsfs.py $(ROOTFS) $@ --size $(BSFS_SIZE_MIB) --overlay $(CARRY) \
		--dir userspace/user --dir bin --dir conf --dir lib --dir mnt \
		--symlink boot/efi=esp:/ --symlink mnt/cd=cd:/ --symlink mnt/usb=usb:/

$(IMAGE): $(ESP_FILES) $(BSFS_IMAGE) tools/mkimage.py
	python3 tools/mkimage.py $(ESP) $(BSFS_IMAGE) $@

iso: $(LIVE_ISO)

$(LIVE_ISO): $(ESP_FILES) $(BSFS_IMAGE) tools/mkiso.py tools/mkimage.py
	python3 tools/mkiso.py $(ESP) $(BSFS_IMAGE) $@

run: all
	./STARTVM.sh

clean:
	rm -rf $(BUILD) $(OUT)
