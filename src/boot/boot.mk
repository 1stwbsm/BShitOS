# BSBOOT: a 32-bit UEFI application (PE32, subsystem 10).

BOOT_CC  ?= i686-w64-mingw32-gcc
BOOT_DIR := src/boot
BOOT_OBJ_DIR := $(BUILD)/boot
BOOT_EFI := $(ESP)/EFI/BOOT/BOOTIA32.EFI

BOOT_SRCS := $(shell find $(BOOT_DIR) -name '*.c')
BOOT_OBJS := $(patsubst $(BOOT_DIR)/%.c,$(BOOT_OBJ_DIR)/%.o,$(BOOT_SRCS)) \
             $(patsubst src/common/%.c,$(BOOT_OBJ_DIR)/common/%.o,$(COMMON_SRCS))

BOOT_CFLAGS := $(COMMON_CFLAGS) -I$(BOOT_DIR) -mno-stack-arg-probe -fshort-wchar
BOOT_LDFLAGS := -nostdlib -Wl,--subsystem,10 -Wl,--entry,_efi_main -Wl,--dynamicbase -s

ESP_FILES += $(BOOT_EFI)

$(BOOT_EFI): $(BOOT_OBJS)
	@mkdir -p $(dir $@)
	$(BOOT_CC) $(BOOT_LDFLAGS) -o $@ $^ -lgcc

$(BOOT_OBJ_DIR)/common/%.o: src/common/%.c
	@mkdir -p $(dir $@)
	$(BOOT_CC) $(BOOT_CFLAGS) -c -o $@ $<

$(BOOT_OBJ_DIR)/%.o: $(BOOT_DIR)/%.c
	@mkdir -p $(dir $@)
	$(BOOT_CC) $(BOOT_CFLAGS) -c -o $@ $<

-include $(BOOT_OBJS:.o=.d)
