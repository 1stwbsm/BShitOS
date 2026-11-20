# BSBOOT for BIOS: a boot sector for the GPT's protective MBR and a real-mode
# stage 2 (gcc -m16) placed right after the GPT entries. mkimage.py and the
# installer put them on disks, so the same disk boots under UEFI and BIOS.

BIOS_DIR := src/boot/bios
BIOS_OBJ_DIR := $(BUILD)/bios
BIOS_CC ?= gcc
BIOS_LD ?= ld
BIOS_CFLAGS := -std=gnu11 -Os -m16 -march=i386 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
               -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-sse -mno-mmx -mno-80387 \
               -fno-tree-loop-distribute-patterns -mstringop-strategy=libcall --param=min-pagesize=0 -fno-delete-null-pointer-checks -Wall -Wextra -MMD -MP -Isrc/include -I$(BIOS_DIR)
MBR_BIN := $(BIOS_OBJ_DIR)/mbr.bin
STAGE2_BIN := $(BIOS_OBJ_DIR)/stage2.bin
STAGE2_OBJS := $(BIOS_OBJ_DIR)/entry.o $(BIOS_OBJ_DIR)/stage2.o $(BIOS_OBJ_DIR)/bsfs.o

# The installer copies these from the live system onto the disk it installs to.
ROOTFS_FILES += $(ROOTFS)/boot/mbr.bin $(ROOTFS)/boot/stage2.bin

$(BIOS_OBJ_DIR)/%.o: $(BIOS_DIR)/%.S $(BIOS_DIR)/layout.h
	@mkdir -p $(dir $@)
	$(BIOS_CC) -m32 -I$(BIOS_DIR) -c -o $@ $<

$(BIOS_OBJ_DIR)/stage2.o: $(BIOS_DIR)/stage2.c
	@mkdir -p $(dir $@)
	$(BIOS_CC) $(BIOS_CFLAGS) -c -o $@ $<

$(BIOS_OBJ_DIR)/bsfs.o: src/common/bsfs.c
	@mkdir -p $(dir $@)
	$(BIOS_CC) $(BIOS_CFLAGS) -c -o $@ $<

$(MBR_BIN): $(BIOS_OBJ_DIR)/mbr.o
	$(BIOS_LD) -m elf_i386 -Ttext 0x7C00 -e _start -o $(BIOS_OBJ_DIR)/mbr.elf $<
	objcopy -O binary -j .text $(BIOS_OBJ_DIR)/mbr.elf $@
	@size=$$(stat -c %s $@); if [ $$size -gt 440 ]; then echo "mbr.bin is $$size bytes; 440 fit" >&2; rm -f $@; exit 1; fi

$(BIOS_OBJ_DIR)/stage2.elf: $(STAGE2_OBJS) $(BIOS_DIR)/stage2.ld
	$(BIOS_LD) -m elf_i386 -T $(BIOS_DIR)/stage2.ld -o $@ $(STAGE2_OBJS)
	@end=$$(nm $@ | awk '/ __bss_end$$/ {print $$1}'); if [ $$((0x$$end)) -gt $$((0x10000)) ]; then \
		echo "stage 2 ends at 0x$$end; it must end below 0x10000" >&2; rm -f $@; exit 1; fi

$(STAGE2_BIN): $(BIOS_OBJ_DIR)/stage2.elf
	objcopy -O binary $< $@

$(ROOTFS)/boot/mbr.bin: $(MBR_BIN)
	@mkdir -p $(dir $@)
	cp $< $@

$(ROOTFS)/boot/stage2.bin: $(STAGE2_BIN)
	@mkdir -p $(dir $@)
	cp $< $@

-include $(STAGE2_OBJS:.o=.d)
