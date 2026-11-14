/*
 * Processes run synchronously: a parent sleeps inside SYS_SPAWN until its child
 * exits, so all of them can share one kernel stack. Each owns two 4 MiB frames,
 * mapped at the user windows while it runs.
 */
#include "arch/x86/cpu.h"
#include "drivers/console.h"
#include "lib/string.h"
#include "net/net.h"
#include "proc/proc.h"

#define MAX_PROCS 6
#define MAX_FRAMES 256
#define FRAME_SIZE 0x400000u
#define STACK_SIZE 0x10000u
#define MAX_NEEDS 4
#define NEED_NAME 32
#define LIB_DIR "/lib/"
#define HEADER_VERSION 1u

struct bs_header {
    char magic[4];
    uint32_t version, load, file_size, mem_size, entry, need_count, reserved;
};

static struct proc procs[MAX_PROCS];
static struct proc *current;
static uint32_t free_frames[MAX_FRAMES];
static uint32_t free_frame_count, total_frames;

void proc_init(const struct bootinfo *info)
{
    const struct bootinfo_mem_range *ranges =
        (const struct bootinfo_mem_range *)(uintptr_t)info->mem_ranges;

    for (uint32_t i = 0; i < info->mem_range_count; i++) {
        uint64_t base = ranges[i].base, end = base + ranges[i].length;

        if (ranges[i].type != BOOTINFO_MEM_USABLE)
            continue;
        base = (base + FRAME_SIZE - 1) & ~(uint64_t)(FRAME_SIZE - 1);
        for (; base + FRAME_SIZE <= end && base + FRAME_SIZE <= 0x100000000ull; base += FRAME_SIZE)
            if (free_frame_count < MAX_FRAMES)
                free_frames[free_frame_count++] = (uint32_t)base;
    }
    total_frames = free_frame_count;
}

void proc_memory(uint32_t *total_kib, uint32_t *free_kib)
{
    *total_kib = total_frames * (FRAME_SIZE / 1024);
    *free_kib = free_frame_count * (FRAME_SIZE / 1024);
}

struct proc *proc_current(void)
{
    return current;
}

static void map_process(const struct proc *p)
{
    paging_map_user(0, p->frames[0]);
    paging_map_user(1, p->frames[1]);
}

static int load_image(const struct vnode *file, const char *magic, uint32_t low, uint32_t high,
                      struct bs_header *header, char needs[][NEED_NAME])
{
    uint32_t offset = sizeof(*header);
    int got;

    if (file->type != BS_TYPE_FILE)
        return -BS_ENOEXEC;
    got = file->ops->read(file, 0, header, sizeof(*header));
    if (got < 0)
        return got;
    if (got != (int)sizeof(*header) || memcmp(header->magic, magic, 4) != 0 ||
        header->version != HEADER_VERSION || header->file_size > header->mem_size ||
        header->load < low || (uint64_t)header->load + header->mem_size > high)
        return -BS_ENOEXEC;

    if (needs) {
        if (header->need_count > MAX_NEEDS)
            return -BS_ENOEXEC;
        got = file->ops->read(file, offset, needs, header->need_count * NEED_NAME);
        if (got != (int)(header->need_count * NEED_NAME))
            return got < 0 ? got : -BS_ENOEXEC;
    } else if (header->need_count != 0) {
        return -BS_ENOEXEC; /* libraries cannot depend on libraries yet */
    }
    offset += header->need_count * NEED_NAME;

    got = file->ops->read(file, offset, (void *)header->load, header->file_size);
    if (got != (int)header->file_size)
        return got < 0 ? got : -BS_ENOEXEC;
    return 0;
}

static int load_libraries(uint32_t count, char needs[][NEED_NAME])
{
    for (uint32_t i = 0; i < count; i++) {
        char path[sizeof(LIB_DIR) + NEED_NAME];
        struct vnode node;
        struct bs_header header;
        int rc;

        needs[i][NEED_NAME - 1] = 0;
        memcpy(path, LIB_DIR, sizeof(LIB_DIR) - 1);
        strlcpy_checked(path + sizeof(LIB_DIR) - 1, needs[i], NEED_NAME);
        rc = vfs_open("/", path, &node);
        if (rc < 0)
            return rc;
        rc = load_image(&node, "BLIB", BS_LIB_BASE, BS_USER_TOP, &header, 0);
        if (rc < 0)
            return rc;
    }
    return 0;
}

/* Lays out [argc][argv][pointers...][strings...]; returns the stack pointer. */
static uint32_t build_stack(int argc, char *const *argv)
{
    uint32_t sp = BS_LIB_BASE;
    uint32_t pointers[32];

    for (int i = argc - 1; i >= 0; i--) {
        uint32_t size = strlen(argv[i]) + 1;

        sp -= size;
        memcpy((void *)sp, argv[i], size);
        pointers[i] = sp;
    }
    sp &= ~3u;

    sp -= 4;
    *(uint32_t *)sp = 0;
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 4;
        *(uint32_t *)sp = pointers[i];
    }
    uint32_t argv_address = sp;
    sp -= 4;
    *(uint32_t *)sp = argv_address;
    sp -= 4;
    *(uint32_t *)sp = (uint32_t)argc;
    return sp;
}

int proc_run(const char *path, int argc, char *const *argv)
{
    struct proc *parent = current, *child = 0;
    const char *cwd = parent ? parent->cwd : "/";
    struct vnode node;
    struct bs_header header;
    char needs[MAX_NEEDS][NEED_NAME];
    uint32_t sp;
    int rc;

    if (argc > 32)
        return -BS_EINVAL;
    rc = vfs_open(cwd, path, &node);
    if (rc < 0)
        return rc;

    for (int i = 0; i < MAX_PROCS && !child; i++)
        if (!procs[i].used)
            child = &procs[i];
    if (!child || free_frame_count < 2)
        return -BS_ENOMEM;

    memset(child, 0, sizeof(*child));
    child->used = 1;
    child->parent = parent;
    strlcpy_checked(child->cwd, cwd, BS_PATH_MAX);
    child->frames[0] = free_frames[--free_frame_count];
    child->frames[1] = free_frames[--free_frame_count];

    /* argv lives in kernel memory, so it survives the remap. */
    map_process(child);
    memset((void *)BS_USER_BASE, 0, BS_USER_TOP - BS_USER_BASE);

    rc = load_image(&node, "BSX1", BS_USER_BASE, BS_LIB_BASE - STACK_SIZE, &header, needs);
    if (rc == 0)
        rc = load_libraries(header.need_count, needs);

    if (rc == 0) {
        sp = build_stack(argc, argv);
        current = child;
        rc = enter_user(header.entry, sp, &child->ksp);
        current = parent;
        tcp_close_owned(child);
        console_graphics_end();
    }

    free_frames[free_frame_count++] = child->frames[1];
    free_frames[free_frame_count++] = child->frames[0];
    child->used = 0;
    if (parent) {
        map_process(parent);
        kernel_tss.esp0 = parent->ksp;
    }
    return rc;
}

void proc_exit(int code)
{
    leave_user(current->ksp, code);
}
