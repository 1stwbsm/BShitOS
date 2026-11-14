# The web version: BShitEmu compiled to WebAssembly, the page, the kernel and a
# fresh root filesystem that boots as a live ramdisk. Serve out/web over HTTP.
WEB       := $(OUT)/web
WEB_CC    ?= zig cc
EMU_SRCS  := $(wildcard src/emu/*.c)
WEB_ROOT_MIB := 16

.PHONY: web
web: $(WEB)/index.html $(WEB)/bshitos.js $(WEB)/bshitos.wasm $(WEB)/bskern.elf $(WEB)/root.img

$(WEB)/index.html $(WEB)/bshitos.js: $(WEB)/%: src/web/%
	@mkdir -p $(dir $@)
	cp $< $@

$(WEB)/bshitos.wasm: $(EMU_SRCS) $(wildcard src/emu/*.h) src/include/bshit/bootinfo.h
	@mkdir -p $(dir $@)
	$(WEB_CC) -target wasm32-freestanding -O2 -nostdlib -Isrc/include -Wl,--no-entry -o $@ $(EMU_SRCS)

$(WEB)/bskern.elf: $(KERNEL_ELF)
	@mkdir -p $(dir $@)
	cp $< $@

# No carry-over here: the web version starts every visitor with an empty /userspace.
$(WEB)/root.img: $(ROOTFS_FILES) tools/mkbsfs.py
	@mkdir -p $(dir $@)
	python3 tools/mkbsfs.py $(ROOTFS) $@ --size $(WEB_ROOT_MIB) \
		--dir userspace/user --dir root --dir bin --dir conf --dir lib --dir mnt \
		--symlink boot/efi=esp:/ --symlink mnt/cd=cd:/ --symlink mnt/usb=usb:/

# The full disk image, zipped, for STARTVM.bat to download. Separate from `web`
# because it also needs the bootloader toolchain (i686-w64-mingw32-gcc).
.PHONY: web-disk
web-disk: $(WEB)/BShitOS-disk.zip

$(WEB)/BShitOS-disk.zip: $(IMAGE)
	@mkdir -p $(dir $@)
	python3 -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED, compresslevel=9).write(sys.argv[2], 'BShitOS.img')" $@ $<
