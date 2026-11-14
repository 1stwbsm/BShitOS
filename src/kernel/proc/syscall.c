#include "drivers/acpi.h"
#include "drivers/ahci.h"
#include "drivers/console.h"
#include "drivers/fb.h"
#include "fs/partitions.h"
#include "drivers/pit.h"
#include "drivers/ps2.h"
#include "drivers/rtc.h"
#include "lib/string.h"
#include "net/net.h"
#include "proc/proc.h"
#include "proc/users.h"

#define FB_BLIT_MAX 2048u

extern int system_live;
#define SPAWN_ARGS_MAX 16
#define SPAWN_BYTES_MAX 1024u

static int user_range_ok(uint32_t address, uint32_t length)
{
    return address >= BS_USER_BASE && address <= BS_USER_TOP && length <= BS_USER_TOP - address;
}

static int copy_string(uint32_t address, char *out, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        if (!user_range_ok(address + i, 1))
            return -BS_EFAULT;
        out[i] = ((const char *)address)[i];
        if (!out[i])
            return (int)i;
    }
    return -BS_ENAMETOOLONG;
}

static struct file *get_file(int fd)
{
    struct file *f;

    if (fd < FD_FIRST_FILE || fd >= FD_FIRST_FILE + PROC_MAX_FILES)
        return 0;
    f = &proc_current()->files[fd - FD_FIRST_FILE];
    return f->used ? f : 0;
}

static int sys_read(int fd, uint32_t buf, uint32_t length)
{
    struct file *f;
    int got;

    if (!user_range_ok(buf, length))
        return -BS_EFAULT;
    if (fd == 0)
        return length ? (int)console_read_line((char *)buf, length) : 0;

    f = get_file(fd);
    if (!f)
        return -BS_EBADF;
    if (f->node.type == BS_TYPE_DIR)
        return -BS_EISDIR;
    got = f->node.ops->read(&f->node, f->offset, (void *)buf, length);
    if (got > 0)
        f->offset += (uint32_t)got;
    return got;
}

static int sys_write(int fd, uint32_t buf, uint32_t length)
{
    struct file *f;
    int written;

    if (!user_range_ok(buf, length))
        return -BS_EFAULT;
    if (fd == 1 || fd == 2) {
        console_write((const char *)buf, length);
        return (int)length;
    }

    f = get_file(fd);
    if (!f)
        return -BS_EBADF;
    if (!f->writable || !f->node.ops->write)
        return -BS_EROFS;
    written = f->node.ops->write(&f->node, f->offset, (const void *)buf, length);
    if (written > 0)
        f->offset += (uint32_t)written;
    return written;
}

static int sys_open(uint32_t path_ptr, uint32_t flags)
{
    struct proc *p = proc_current();
    char path[BS_PATH_MAX];
    struct vnode node;
    int rc = copy_string(path_ptr, path, sizeof(path));

    if (rc < 0)
        return rc;
    if (flags & BS_O_WRITE) {
        rc = vfs_open_write(p->cwd, path, &node);
    } else {
        rc = vfs_open(p->cwd, path, &node);
        if (rc == 0)
            rc = vfs_permit(&node, VFS_READ);
    }
    if (rc < 0)
        return rc;

    for (int i = 0; i < PROC_MAX_FILES; i++) {
        if (!p->files[i].used) {
            p->files[i].used = 1;
            p->files[i].writable = (flags & BS_O_WRITE) != 0;
            p->files[i].node = node;
            p->files[i].offset = 0;
            return FD_FIRST_FILE + i;
        }
    }
    return -BS_EMFILE;
}

static int sys_close(int fd)
{
    struct file *f = get_file(fd);

    if (!f)
        return -BS_EBADF;
    f->used = 0;
    return 0;
}

static int sys_readdir(int fd, uint32_t out)
{
    struct file *f = get_file(fd);

    if (!f)
        return -BS_EBADF;
    if (f->node.type != BS_TYPE_DIR)
        return -BS_ENOTDIR;
    if (!user_range_ok(out, sizeof(struct bs_dirent)))
        return -BS_EFAULT;
    return f->node.ops->readdir(&f->node, &f->offset, (struct bs_dirent *)out);
}

static int sys_chdir(uint32_t path_ptr)
{
    struct proc *p = proc_current();
    char path[BS_PATH_MAX], clean[BS_PATH_MAX];
    int rc = copy_string(path_ptr, path, sizeof(path));

    if (rc < 0)
        return rc;
    rc = vfs_chdir(p->cwd, path, clean);
    if (rc < 0)
        return rc;
    strlcpy_checked(p->cwd, clean, BS_PATH_MAX);
    return 0;
}

static int sys_getcwd(uint32_t out, uint32_t size)
{
    struct proc *p = proc_current();

    if (!user_range_ok(out, size) || size == 0)
        return -BS_EFAULT;
    return strlcpy_checked((char *)out, p->cwd, size) < 0 ? -BS_ENAMETOOLONG : 0;
}

static int path_call(int (*fn)(const char *, const char *), uint32_t path_ptr)
{
    char path[BS_PATH_MAX];
    int rc = copy_string(path_ptr, path, sizeof(path));

    return rc < 0 ? rc : fn(proc_current()->cwd, path);
}

static int sys_fb_blit(uint32_t pixels, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || width > FB_BLIT_MAX || height > FB_BLIT_MAX ||
        !user_range_ok(pixels, width * height * 4))
        return -BS_EINVAL;
    console_blit((const uint32_t *)pixels, width, height);
    return 0;
}

static int sys_mouse(uint32_t out)
{
    if (!user_range_ok(out, sizeof(struct bs_mouse)))
        return -BS_EFAULT;
    ps2_mouse_read((struct bs_mouse *)out);
    return 0;
}

static int sys_time(uint32_t out)
{
    if (!user_range_ok(out, sizeof(struct bs_time)))
        return -BS_EFAULT;
    rtc_read((struct bs_time *)out);
    return 0;
}

static int sys_net_info(uint32_t out)
{
    if (!user_range_ok(out, sizeof(struct bs_netinfo)))
        return -BS_EFAULT;
    net_info((struct bs_netinfo *)out);
    return 0;
}

static int sys_dns(uint32_t name_ptr, uint32_t out)
{
    char name[BS_PATH_MAX];
    int rc = copy_string(name_ptr, name, sizeof(name));

    if (rc < 0)
        return rc;
    if (!user_range_ok(out, 4))
        return -BS_EFAULT;
    if (!net_configured())
        return -BS_ENETDOWN;
    return net_dns_lookup(name, (uint32_t *)out);
}

uint32_t boot_seconds;
int system_live;

static int sys_sysinfo(uint32_t out)
{
    struct bs_sysinfo *info = (struct bs_sysinfo *)out;

    if (!user_range_ok(out, sizeof(*info)))
        return -BS_EFAULT;
    proc_memory(&info->memory_total_kib, &info->memory_free_kib);
    bsfs_usage(&info->disk_total_kib, &info->disk_free_kib);
    info->uptime_seconds = rtc_seconds() - boot_seconds;
    fb_resolution(&info->screen_width, &info->screen_height);
    info->flags = system_live ? BS_SYSINFO_LIVE : 0;
    return 0;
}

static int sys_login(uint32_t name_ptr, uint32_t password_ptr)
{
    char name[BS_USER_NAME_MAX], password[128];
    int rc = copy_string(name_ptr, name, sizeof(name));

    if (rc >= 0 && password_ptr)
        rc = copy_string(password_ptr, password, sizeof(password));
    if (rc < 0)
        return rc;
    rc = users_login(name, password_ptr ? password : 0);
    memset(password, 0, sizeof(password));
    return rc;
}

static int sys_setpass(uint32_t name_ptr, uint32_t old_ptr, uint32_t new_ptr)
{
    char name[BS_USER_NAME_MAX], old_password[128], new_password[128];
    int rc = copy_string(name_ptr, name, sizeof(name));

    if (rc >= 0)
        rc = copy_string(old_ptr, old_password, sizeof(old_password));
    if (rc >= 0)
        rc = copy_string(new_ptr, new_password, sizeof(new_password));
    if (rc >= 0)
        rc = users_set_password(name, old_password, new_password);
    memset(old_password, 0, sizeof(old_password));
    memset(new_password, 0, sizeof(new_password));
    return rc;
}

static int sys_whoami(uint32_t out)
{
    if (!user_range_ok(out, sizeof(struct bs_user)))
        return -BS_EFAULT;
    memcpy((void *)out, &proc_current()->user, sizeof(struct bs_user));
    return 0;
}

static int sys_stat(uint32_t path_ptr, uint32_t out)
{
    char path[BS_PATH_MAX];
    int rc = copy_string(path_ptr, path, sizeof(path));

    if (rc < 0)
        return rc;
    if (!user_range_ok(out, sizeof(struct bs_stat)))
        return -BS_EFAULT;
    return vfs_stat(proc_current()->cwd, path, (struct bs_stat *)out);
}

static int sys_chmod(uint32_t path_ptr, uint32_t perm)
{
    char path[BS_PATH_MAX];
    int rc = copy_string(path_ptr, path, sizeof(path));

    return rc < 0 ? rc : vfs_chmod(proc_current()->cwd, path, perm);
}

static int sys_chown(uint32_t path_ptr, uint32_t uid, uint32_t gid)
{
    char path[BS_PATH_MAX];
    int rc = copy_string(path_ptr, path, sizeof(path));

    return rc < 0 ? rc : vfs_chown(proc_current()->cwd, path, uid, gid);
}

static int sys_readlink(uint32_t path_ptr, uint32_t out, uint32_t size)
{
    char path[BS_PATH_MAX];
    int rc = copy_string(path_ptr, path, sizeof(path));

    if (rc < 0)
        return rc;
    if (!user_range_ok(out, size) || size == 0)
        return -BS_EFAULT;
    return vfs_readlink(proc_current()->cwd, path, (char *)out, size);
}

static int is_root(void)
{
    return proc_current()->user.uid == BS_UID_ROOT;
}

static int sys_disk_info(uint32_t out)
{
    struct bs_disk_info *info = (struct bs_disk_info *)out;
    uint64_t sectors = 0;

    if (!user_range_ok(out, sizeof(*info)))
        return -BS_EFAULT;
    memset(info, 0, sizeof(*info));
    if (!ahci_has_disk() || ahci_identify(&sectors, info->model, sizeof(info->model)) < 0)
        return 0;
    info->present = 1;
    info->in_use = !system_live;
    info->sectors_low = (uint32_t)sectors;
    info->sectors_high = (uint32_t)(sectors >> 32);
    return 0;
}

/* User memory is not identity-mapped, so DMA goes through this. */
static uint8_t bounce[BS_DISK_IO_MAX * BS_DISK_SECTOR] __attribute__((aligned(16)));

static int sys_disk_io(uint32_t request, int write)
{
    struct bs_disk_io io;
    uint64_t lba;
    uint32_t bytes;

    if (!is_root())
        return -BS_EPERM;
    if (!user_range_ok(request, sizeof(io)))
        return -BS_EFAULT;
    memcpy(&io, (const void *)request, sizeof(io));
    bytes = io.sectors * BS_DISK_SECTOR;
    if (io.sectors == 0 || io.sectors > BS_DISK_IO_MAX || !user_range_ok(io.buffer, bytes))
        return -BS_EINVAL;
    if (!ahci_has_disk())
        return -BS_ENOENT;
    if (write && !system_live)
        return -BS_EPERM; /* the running system lives on this disk */
    lba = (uint64_t)io.lba_high << 32 | io.lba_low;
    if (write) {
        memcpy(bounce, (const void *)io.buffer, bytes);
        return ahci_write(lba, io.sectors, bounce) < 0 ? -BS_EIO : 0;
    }
    if (ahci_read(lba, io.sectors, bounce) < 0)
        return -BS_EIO;
    memcpy((void *)io.buffer, bounce, bytes);
    return 0;
}

static int sys_spawn(uint32_t path_ptr, int argc, uint32_t argv_ptr)
{
    static char storage[SPAWN_BYTES_MAX];
    char path[BS_PATH_MAX];
    char *argv[SPAWN_ARGS_MAX];
    uint32_t used = 0;
    int rc = copy_string(path_ptr, path, sizeof(path));

    if (rc < 0)
        return rc;
    if (argc < 0 || argc > SPAWN_ARGS_MAX || !user_range_ok(argv_ptr, (uint32_t)argc * 4))
        return -BS_EINVAL;

    for (int i = 0; i < argc; i++) {
        rc = copy_string(((const uint32_t *)argv_ptr)[i], storage + used, SPAWN_BYTES_MAX - used);
        if (rc < 0)
            return rc;
        argv[i] = storage + used;
        used += (uint32_t)rc + 1;
    }
    return proc_run(path, argc, argv);
}

int syscall_dispatch(uint32_t number, uint32_t a, uint32_t b, uint32_t c)
{
    switch (number) {
    case SYS_EXIT:    proc_exit((int)a);
    case SYS_READ:    return sys_read((int)a, b, c);
    case SYS_WRITE:   return sys_write((int)a, b, c);
    case SYS_OPEN:    return sys_open(a, b);
    case SYS_CLOSE:   return sys_close((int)a);
    case SYS_READDIR: return sys_readdir((int)a, b);
    case SYS_CHDIR:   return sys_chdir(a);
    case SYS_GETCWD:  return sys_getcwd(a, b);
    case SYS_MKDIR:   return path_call(vfs_mkdir, a);
    case SYS_UNLINK:  return path_call(vfs_unlink, a);
    case SYS_SPAWN:   return sys_spawn(a, (int)b, c);
    case SYS_FB_BLIT: return sys_fb_blit(a, b, c);
    case SYS_POLL_KEY: return (uint8_t)console_poll_key();
    case SYS_SLEEP:   pit_sleep_ms(a); return 0;
    case SYS_SERIAL:  console_set_serial(a != 0); return 0;
    case SYS_MOUSE:   return sys_mouse(a);
    case SYS_TIME:    return sys_time(a);
    case SYS_BEEP:    pit_beep(a, b); return 0;
    case SYS_CLEAR:   console_clear(); return 0;
    case SYS_REBOOT:  reboot_machine();
    case SYS_CONSOLE_SIZE: return (int)console_size();
    case SYS_POWEROFF: acpi_poweroff(); return -BS_EINVAL;
    case SYS_NET_INFO: return sys_net_info(a);
    case SYS_DNS:     return sys_dns(a, b);
    case SYS_PING:    return net_configured() ? net_ping(a, b) : -BS_ENETDOWN;
    case SYS_TCP_OPEN: return tcp_open(a, (uint16_t)b, proc_current());
    case SYS_TCP_SEND:
        return user_range_ok(b, c) ? tcp_send((int)a, (const void *)b, c) : -BS_EFAULT;
    case SYS_TCP_RECEIVE:
        return user_range_ok(b, c) ? tcp_receive((int)a, (void *)b, c) : -BS_EFAULT;
    case SYS_TCP_CLOSE: return tcp_close((int)a);
    case SYS_GRAPHICS_END: console_graphics_end(); return 0;
    case SYS_SYSINFO: return sys_sysinfo(a);
    case SYS_LOGIN:   return sys_login(a, b);
    case SYS_WHOAMI:  return sys_whoami(a);
    case SYS_STAT:    return sys_stat(a, b);
    case SYS_CHMOD:   return sys_chmod(a, b);
    case SYS_CHOWN:   return sys_chown(a, b, c);
    case SYS_SETPASS: return sys_setpass(a, b, c);
    case SYS_READLINK: return sys_readlink(a, b, c);
    case SYS_DISK_INFO: return sys_disk_info(a);
    case SYS_DISK_READ: return sys_disk_io(a, 0);
    case SYS_DISK_WRITE: return sys_disk_io(a, 1);
    case SYS_CONSOLE_LINE:
        return user_range_ok(b, c) && c ? console_line(a, (char *)b, c) : -BS_EFAULT;
    default:          return -BS_EINVAL;
    }
}
