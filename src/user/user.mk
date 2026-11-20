# Userspace: libbs.blib plus one .bs program per file in bin/.

USER_CC  ?= gcc
USER_LD  ?= ld
USER_DIR := src/user
USER_OBJ_DIR := $(BUILD)/user
USER_LDSCRIPT := $(USER_DIR)/user.ld

USER_CFLAGS := $(COMMON_CFLAGS) -m32 -march=i386 -mtune=generic -I$(USER_DIR)/include -fno-pic -fno-pie \
               -fno-asynchronous-unwind-tables -fno-unwind-tables
USER_LDFLAGS := -m elf_i386 -nostdlib -z noexecstack --no-warn-rwx-segments --build-id=none -T $(USER_LDSCRIPT)

PROGRAM_BASE := 0x40000000
LIBBS_BASE   := 0x40400000

LIBBS_SRCS := $(wildcard $(USER_DIR)/lib/*.c)
LIBBS_OBJS := $(patsubst $(USER_DIR)/%.c,$(USER_OBJ_DIR)/%.o,$(LIBBS_SRCS))
LIBBS_ELF  := $(USER_OBJ_DIR)/libbs.elf
LIBBS      := $(ROOTFS)/lib/libbs.blib

# A program is either bin/name.c or a directory bin/name/ of sources.
PROGRAM_SRCS := $(wildcard $(USER_DIR)/bin/*.c)
PROGRAM_DIRS := $(notdir $(patsubst %/,%,$(wildcard $(USER_DIR)/bin/*/)))
PROGRAMS     := $(patsubst $(USER_DIR)/bin/%.c,$(ROOTFS)/bin/%.bs,$(PROGRAM_SRCS)) \
                $(patsubst %,$(ROOTFS)/bin/%.bs,$(PROGRAM_DIRS))
CRT0         := $(USER_OBJ_DIR)/crt0.o

ROOTFS_FILES += $(LIBBS) $(PROGRAMS)

$(LIBBS_ELF): $(LIBBS_OBJS) $(USER_LDSCRIPT)
	$(USER_LD) $(USER_LDFLAGS) --defsym=BASE=$(LIBBS_BASE) -e $(LIBBS_BASE) -o $@ $(LIBBS_OBJS)

$(LIBBS): $(LIBBS_ELF) tools/mkbs.py
	@mkdir -p $(dir $@)
	python3 tools/mkbs.py $< $@ --lib

# Programs resolve library calls against libbs's fixed addresses.
$(USER_OBJ_DIR)/bin/%.elf: $(USER_OBJ_DIR)/bin/%.o $(CRT0) $(LIBBS_ELF) $(USER_LDSCRIPT)
	$(USER_LD) $(USER_LDFLAGS) --defsym=BASE=$(PROGRAM_BASE) -e _start -R $(LIBBS_ELF) -o $@ $(CRT0) $<

define MULTI_FILE_PROGRAM
$(1)_OBJS := $$(patsubst $$(USER_DIR)/%.c,$$(USER_OBJ_DIR)/%.o,$$(wildcard $$(USER_DIR)/bin/$(1)/*.c))
$$(USER_OBJ_DIR)/bin/$(1).elf: $$($(1)_OBJS) $$(CRT0) $$(LIBBS_ELF) $$(USER_LDSCRIPT)
	$$(USER_LD) $$(USER_LDFLAGS) --defsym=BASE=$$(PROGRAM_BASE) -e _start -R $$(LIBBS_ELF) -o $$@ $$(CRT0) $$($(1)_OBJS)
-include $$($(1)_OBJS:.o=.d)
endef
$(foreach name,$(PROGRAM_DIRS),$(eval $(call MULTI_FILE_PROGRAM,$(name))))

$(ROOTFS)/bin/%.bs: $(USER_OBJ_DIR)/bin/%.elf tools/mkbs.py
	@mkdir -p $(dir $@)
	python3 tools/mkbs.py $< $@ --need libbs.blib

$(USER_OBJ_DIR)/%.o: $(USER_DIR)/%.c
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -c -o $@ $<

$(USER_OBJ_DIR)/%.o: $(USER_DIR)/%.S
	@mkdir -p $(dir $@)
	$(USER_CC) $(USER_CFLAGS) -c -o $@ $<

.PRECIOUS: $(USER_OBJ_DIR)/bin/%.elf $(USER_OBJ_DIR)/bin/%.o

-include $(LIBBS_OBJS:.o=.d) $(patsubst $(USER_DIR)/bin/%.c,$(USER_OBJ_DIR)/bin/%.d,$(PROGRAM_SRCS))
