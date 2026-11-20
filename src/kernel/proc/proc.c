/*
 * Processes run synchronously: a parent sleeps inside SYS_SPAWN until its child
 * exits, so all of them can share one kernel stack. Each has its own page tables
 * for the user window, and only the pages it touches get memory.
 */
#include "arch/x86/cpu.h"
#include "drivers/console.h"
#include "lib/string.h"
#include "net/net.h"
#include "proc/memory.h"
#include "proc/proc.h"
#include "proc/shutdown.h"

#define MAX_PROCS 6
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

void proc_memory(uint32_t *total_kib, uint32_t *free_kib)
{
    mem_stats(total_kib, free_kib);
}

struct proc *proc_current(void)
{
    return current;
}

void proc_credentials(uint32_t *uid, uint32_t *gid)
{
    *uid = current ? current->user.uid : BS_UID_ROOT;
    *gid = current ? current->user.gid : 0;
}

static void map_process(const struct proc *p)
{
    paging_use_tables(p ? (uint32_t *)p->tables : 0);
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
    if (rc == 0)
        rc = vfs_permit(&node, VFS_EXEC);
    if (rc < 0)
        return rc;

    for (int i = 0; i < MAX_PROCS && !child; i++)
        if (!procs[i].used)
            child = &procs[i];
    if (!child)
        return -BS_ENOMEM;

    memset(child, 0, sizeof(*child));
    child->used = 1;
    child->parent = parent;
    strlcpy_checked(child->cwd, cwd, BS_PATH_MAX);
    {
        const char *base = path;

        for (const char *p = path; *p; p++)
            if (*p == '/')
                base = p + 1;
        strlcpy_checked(child->name, base, sizeof(child->name)); /* truncated is fine */
        for (char *dot = child->name; *dot; dot++)
            if (dot[0] == '.' && dot[1] == 'b' && dot[2] == 's' && !dot[3])
                *dot = 0; /* "bsh", not "bsh.bs" */
    }
    if (parent) {
        child->user = parent->user;
    } else { /* started by the kernel: root */
        strlcpy_checked(child->user.name, "root", sizeof(child->user.name));
        strlcpy_checked(child->user.home, "/root", sizeof(child->user.home));
        strlcpy_checked(child->user.shell, "/bin/bsh.bs", sizeof(child->user.shell));
    }

    /* argv lives in kernel memory, so it survives the remap. */
    map_process(child);

    rc = load_image(&node, "BSX1", BS_USER_BASE, BS_LIB_BASE - STACK_SIZE, &header, needs);
    if (rc == 0)
        rc = load_libraries(header.need_count, needs);

    if (rc == 0) {
        sp = build_stack(argc, argv);
        current = child;
        rc = enter_user(header.entry, sp, &child->ksp);
        current = parent;
        if (shutdown_pending()) {
            console_print("[\033[92m ok \033[0m] ");
            console_print(child->name);
            console_print(rc == BS_EXIT_STOPPED ? " stopped\n" : " exited\n");
        }
        tcp_close_owned(child);
        console_graphics_end();
    }

    paging_free_user(child->tables);
    child->used = 0;
    map_process(parent);
    if (parent)
        kernel_tss.esp0 = parent->ksp;
    return rc;
}

void proc_exit(int code)
{
    leave_user(current->ksp, code);
}
