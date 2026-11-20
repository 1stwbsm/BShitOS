/* System: what the machine has and how much of it is in use. */
#include "bs.h"
#include "bdeskui.h"

#define LEFT 10
#define VALUE_X 92
#define ROW 14
#define REFRESH_FRAMES 60

static struct bs_sysinfo info;
static struct bs_netinfo net;
static char cpu[49];
static int last_refresh = -1000;

static void number(char *out, unsigned int value)
{
    char reversed[12];
    int n = 0, i = 0;

    do {
        reversed[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        out[i++] = reversed[--n];
    out[i] = 0;
}

static void append(char *out, unsigned int size, const char *text)
{
    copy_string(out + strlen(out), size - strlen(out), text);
}

static void append_number(char *out, unsigned int size, unsigned int value)
{
    char digits[12];

    number(digits, value);
    append(out, size, digits);
}

static void read_cpu_name(void)
{
    unsigned int regs[12], eax = 0x80000000u, ebx, ecx, edx;

    if (!cpu_has_cpuid()) {
        copy_string(cpu, sizeof(cpu), "386/486 (no CPUID)");
        return;
    }
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 0x80000004u) {
        copy_string(cpu, sizeof(cpu), "unknown x86 processor");
        return;
    }
    for (unsigned int leaf = 0; leaf < 3; leaf++) {
        unsigned int a = 0x80000002u + leaf;

        __asm__ volatile("cpuid" : "+a"(a), "=b"(ebx), "=c"(ecx), "=d"(edx));
        regs[leaf * 4] = a;
        regs[leaf * 4 + 1] = ebx;
        regs[leaf * 4 + 2] = ecx;
        regs[leaf * 4 + 3] = edx;
    }
    memcpy(cpu, regs, 48);
    cpu[48] = 0;
    {
        char *p = cpu;

        while (*p == ' ')
            p++;
        copy_string(cpu, sizeof(cpu), p);
    }
}

static void refresh(void)
{
    sysinfo(&info);
    net_info(&net);
    if (!cpu[0])
        read_cpu_name();
}

static int system_tick(void)
{
    if (wm_frame() - last_refresh < REFRESH_FRAMES)
        return 0;
    last_refresh = wm_frame();
    refresh();
    return 1;
}

static void row(int y, const char *label, const char *value)
{
    gfx_text(LEFT, y, label, theme->dim);
    gfx_text(VALUE_X, y, value, theme->text);
}

static void usage_row(int y, int w, const char *label, unsigned int used_kib, unsigned int total_kib)
{
    char text[48] = "";

    append_number(text, sizeof(text), used_kib / 1024);
    append(text, sizeof(text), " of ");
    append_number(text, sizeof(text), total_kib / 1024);
    append(text, sizeof(text), " MiB used");
    row(y, label, text);
    gfx_bar(VALUE_X, y + 10, w - VALUE_X - LEFT, 9, used_kib, total_kib, used_kib * 5 > total_kib * 4 ? 0xE2412F : 0x4DC24A);
}

static void ip_text(char *out, unsigned int size, unsigned int ip)
{
    out[0] = 0;
    for (int i = 3; i >= 0; i--) {
        append_number(out, size, (ip >> (i * 8)) & 255);
        if (i)
            append(out, size, ".");
    }
}

static void system_draw(int w, int h, int focused)
{
    char text[64];
    int y = 8;

    (void)h;
    (void)focused;
    if (last_refresh < 0)
        refresh();

    gfx_text_scaled(LEFT + 1, y + 1, "BShitOS", 2, INK);
    gfx_text_scaled(LEFT, y, "BShitOS", 2, app_system.accent);
    y += 22;

    text[0] = 0;
    append_number(text, sizeof(text), info.uptime_seconds / 3600);
    append(text, sizeof(text), "h ");
    append_number(text, sizeof(text), info.uptime_seconds / 60 % 60);
    append(text, sizeof(text), "m ");
    append_number(text, sizeof(text), info.uptime_seconds % 60);
    append(text, sizeof(text), "s");
    row(y, "running for", text);
    y += ROW;
    row(y, "processor", cpu);
    y += ROW;
    text[0] = 0;
    append_number(text, sizeof(text), info.screen_width);
    append(text, sizeof(text), " x ");
    append_number(text, sizeof(text), info.screen_height);
    row(y, "screen", text);
    y += ROW + 4;

    usage_row(y, w, "memory", info.memory_total_kib - info.memory_free_kib, info.memory_total_kib);
    y += ROW + 12;
    usage_row(y, w, "disk", info.disk_total_kib - info.disk_free_kib, info.disk_total_kib);
    y += ROW + 16;

    if (!net.present) {
        row(y, "network", "no supported network card");
        return;
    }
    if (!net.ip) {
        row(y, "network", "card found, but no address");
        return;
    }
    ip_text(text, sizeof(text), net.ip);
    row(y, "address", text);
    y += ROW;
    ip_text(text, sizeof(text), net.gateway);
    row(y, "gateway", text);
    y += ROW;
    ip_text(text, sizeof(text), net.dns);
    row(y, "dns", text);
    y += ROW;
    text[0] = 0;
    for (int i = 0; i < 6; i++) {
        char pair[4] = {"0123456789abcdef"[net.mac[i] >> 4], "0123456789abcdef"[net.mac[i] & 15], i < 5 ? ':' : 0, 0};

        append(text, sizeof(text), pair);
    }
    row(y, "mac", text);
}

const struct app app_system = {
    .name = "System", .blurb = "machine status", .accent = 0x9A5ACB, .icon = ICON_SYSTEM,
    .default_w = 330, .default_h = 230, .min_w = 260, .min_h = 150,
    .draw = system_draw, .tick = system_tick,
};
