# The kernel: a freestanding 32-bit ELF that BSBOOT loads from BSFS.

KERNEL_CC  ?= gcc
KERNEL_LD  ?= ld
KERNEL_DIR := src/kernel
KERNEL_OBJ_DIR := $(BUILD)/kernel
KERNEL_ELF := $(ROOTFS)/boot/bskern.elf
KERNEL_LDSCRIPT := $(KERNEL_DIR)/arch/x86/linker.ld

KERNEL_SRCS := $(shell find $(KERNEL_DIR) -name '*.c' -o -name '*.S')
KERNEL_OBJS := $(patsubst $(KERNEL_DIR)/%,$(KERNEL_OBJ_DIR)/%.o,$(KERNEL_SRCS))
KERNEL_COMMON_OBJS := $(patsubst src/common/%,$(KERNEL_OBJ_DIR)/common/%.o,$(COMMON_SRCS))

KERNEL_CFLAGS := $(COMMON_CFLAGS) -m32 -march=i386 -mtune=generic -I$(KERNEL_DIR) -fno-pic -fno-pie \
                 -fno-asynchronous-unwind-tables -fno-unwind-tables

ROOTFS_FILES += $(KERNEL_ELF)

$(KERNEL_ELF): $(KERNEL_OBJS) $(KERNEL_COMMON_OBJS) $(KERNEL_LDSCRIPT)
	@mkdir -p $(dir $@)
	$(KERNEL_LD) -m elf_i386 -nostdlib -z noexecstack -z max-page-size=4096 --build-id=none \
		-T $(KERNEL_LDSCRIPT) -o $@ $(KERNEL_OBJS) $(KERNEL_COMMON_OBJS)

$(KERNEL_OBJ_DIR)/common/%.o: src/common/%
	@mkdir -p $(dir $@)
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c -o $@ $<

$(KERNEL_OBJ_DIR)/%.o: $(KERNEL_DIR)/%
	@mkdir -p $(dir $@)
	$(KERNEL_CC) $(KERNEL_CFLAGS) -c -o $@ $<

-include $(KERNEL_OBJS:.o=.d) $(KERNEL_COMMON_OBJS:.o=.d)
