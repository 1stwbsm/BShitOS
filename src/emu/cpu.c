/*
 * An i686 interpreter: 32-bit protected mode with flat segments, paging
 * (4 KiB and 4 MiB pages), rings 0 and 3, and the integer instruction set
 * that GCC generates. No FPU, MMX or SSE; BShitOS uses none of them.
 */
#include "emu.h"

enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
enum { ES, CS, SS, DS, FS, GS };

#define F_CF 0x0001u
#define F_PF 0x0004u
#define F_AF 0x0010u
#define F_ZF 0x0040u
#define F_SF 0x0080u
#define F_TF 0x0100u
#define F_IF 0x0200u
#define F_DF 0x0400u
#define F_OF 0x0800u
#define F_FIXED 0x0002u
#define F_WRITABLE 0x00254DD5u /* the flags POPF and IRET may change */

#define CR0_PE 0x00000001u
#define CR0_WP 0x00010000u
#define CR0_PG 0x80000000u
#define CR4_PSE 0x00000010u

#define VECTOR_DE 0
#define VECTOR_UD 6
#define VECTOR_GP 13
#define VECTOR_PF 14

#define TLB_SIZE 1024u

static u32 regs[8], eip, eflags;
static u16 seg[6];
static u32 cr0, cr2, cr3, cr4;
static u32 gdt_base, gdt_limit, idt_base, idt_limit;
static u16 task_register;
static int cpl;
static u64 tsc;
static int halted, crashed;
static char crash_text[160];

static int fault, fault_has_error;
static u32 fault_vector, fault_error;

static u32 tlb_tag[TLB_SIZE], tlb_phys[TLB_SIZE];
static u8 tlb_user[TLB_SIZE], tlb_write[TLB_SIZE];

static u8 parity_even[256];

/* Per-instruction decode state */
static int opsize, rep_prefix;
static int modrm_is_register, modrm_reg, modrm_rm;
static u32 effective_address;

struct snapshot {
    u32 regs[8], eip, eflags;
    u16 seg[6];
    int cpl;
};

/* ---- Errors --------------------------------------------------------------- */

static void append_text(char *out, unsigned int *n, const char *text)
{
    while (*text && *n + 1 < sizeof(crash_text))
        out[(*n)++] = *text++;
    out[*n] = 0;
}

static void append_hex(char *out, unsigned int *n, u32 value)
{
    char digits[9];

    for (int i = 7; i >= 0; i--, value >>= 4)
        digits[i] = "0123456789abcdef"[value & 15];
    digits[8] = 0;
    append_text(out, n, digits);
}

static void crash(const char *why)
{
    unsigned int n = 0;

    if (crashed)
        return;
    crashed = 1;
    append_text(crash_text, &n, why);
    append_text(crash_text, &n, " (eip ");
    append_hex(crash_text, &n, eip);
    append_text(crash_text, &n, ")");
}

const char *cpu_crash_message(void)
{
    return crash_text;
}

static void raise(u32 vector, int has_error, u32 error)
{
    if (fault)
        return;
    fault = 1;
    fault_vector = vector;
    fault_has_error = has_error;
    fault_error = error;
}

/* ---- Paging ----------------------------------------------------------------- */

static void tlb_flush(void)
{
    for (u32 i = 0; i < TLB_SIZE; i++)
        tlb_tag[i] = 0xFFFFFFFFu;
}

static int page_fault(u32 linear, u32 error)
{
    if (!fault)
        cr2 = linear;
    raise(VECTOR_PF, 1, error);
    return 0;
}

static int translate(u32 linear, int write, int user, u32 *physical)
{
    u32 page = linear >> 12, slot = page & (TLB_SIZE - 1);
    u32 error = (write ? 2u : 0u) | (user ? 4u : 0u);
    u32 pde, base, flags;
    int allowed_user, allowed_write;

    if (!(cr0 & CR0_PG)) {
        *physical = linear;
        return 1;
    }

    if (tlb_tag[slot] == page) {
        allowed_user = tlb_user[slot];
        allowed_write = tlb_write[slot];
        base = tlb_phys[slot];
    } else {
        pde = phys_read((cr3 & 0xFFFFF000u) + (linear >> 22) * 4, 4);
        if (!(pde & 1))
            return page_fault(linear, error);
        if ((pde & 0x80) && (cr4 & CR4_PSE)) {
            base = (pde & 0xFFC00000u) | (linear & 0x003FF000u);
            flags = pde;
        } else {
            u32 pte = phys_read((pde & 0xFFFFF000u) + ((linear >> 12) & 0x3FF) * 4, 4);

            if (!(pte & 1))
                return page_fault(linear, error);
            base = pte & 0xFFFFF000u;
            flags = pde & pte;
        }
        allowed_user = (flags >> 2) & 1;
        allowed_write = (flags >> 1) & 1;
        tlb_tag[slot] = page;
        tlb_phys[slot] = base;
        tlb_user[slot] = (u8)allowed_user;
        tlb_write[slot] = (u8)allowed_write;
    }

    if (user && !allowed_user)
        return page_fault(linear, error | 1);
    if (write && !allowed_write && (user || (cr0 & CR0_WP)))
        return page_fault(linear, error | 1);
    *physical = base | (linear & 0xFFF);
    return 1;
}

static u32 linear_read(u32 linear, int size, int user)
{
    u32 physical;

    if (fault)
        return 0;
    if ((linear & 0xFFF) + (u32)size > 0x1000) {
        u32 value = 0;

        for (int i = 0; i < size; i++)
            value |= linear_read(linear + (u32)i, 1, user) << (8 * i);
        return value;
    }
    if (!translate(linear, 0, user, &physical))
        return 0;
    return phys_read(physical, size);
}

static void linear_write(u32 linear, u32 value, int size, int user)
{
    u32 physical;

    if (fault)
        return;
    if ((linear & 0xFFF) + (u32)size > 0x1000) {
        for (int i = 0; i < size; i++)
            linear_write(linear + (u32)i, value >> (8 * i), 1, user);
        return;
    }
    if (!translate(linear, 1, user, &physical))
        return;
    phys_write(physical, value, size);
}

#define READ(address, size) linear_read((address), (size), cpl == 3)
#define WRITE(address, value, size) linear_write((address), (value), (size), cpl == 3)

static u32 fetch(int size)
{
    u32 value = linear_read(eip, size, cpl == 3);

    eip += (u32)size;
    return value;
}

/* ---- Registers and operands ------------------------------------------------ */

static u32 mask_of(int size)
{
    return size == 4 ? 0xFFFFFFFFu : size == 2 ? 0xFFFFu : 0xFFu;
}

static u32 sign_bit(int size)
{
    return size == 4 ? 0x80000000u : size == 2 ? 0x8000u : 0x80u;
}

static u32 sign_extend(u32 value, int size)
{
    return size == 1 ? (u32)(s32)(s8)value : size == 2 ? (u32)(s32)(s16)value : value;
}

static u32 get_reg(int r, int size)
{
    if (size == 4)
        return regs[r];
    if (size == 2)
        return regs[r] & 0xFFFF;
    return r < 4 ? regs[r] & 0xFF : (regs[r - 4] >> 8) & 0xFF;
}

static void set_reg(int r, u32 value, int size)
{
    if (size == 4)
        regs[r] = value;
    else if (size == 2)
        regs[r] = (regs[r] & 0xFFFF0000u) | (value & 0xFFFF);
    else if (r < 4)
        regs[r] = (regs[r] & ~0xFFu) | (value & 0xFF);
    else
        regs[r - 4] = (regs[r - 4] & ~0xFF00u) | ((value & 0xFF) << 8);
}

static void decode_modrm(void)
{
    u8 m = (u8)fetch(1);
    int mod = m >> 6, rm = m & 7;
    u32 address;

    modrm_reg = (m >> 3) & 7;
    if (mod == 3) {
        modrm_is_register = 1;
        modrm_rm = rm;
        return;
    }
    modrm_is_register = 0;

    if (rm == 4) {
        u8 sib = (u8)fetch(1);
        int scale = sib >> 6, index = (sib >> 3) & 7, base = sib & 7;

        address = (base == 5 && mod == 0) ? fetch(4) : regs[base];
        if (index != 4)
            address += regs[index] << scale;
    } else if (rm == 5 && mod == 0) {
        address = fetch(4);
    } else {
        address = regs[rm];
    }
    if (mod == 1)
        address += (u32)(s32)(s8)fetch(1);
    else if (mod == 2)
        address += fetch(4);
    effective_address = address;
}

static u32 read_rm(int size)
{
    return modrm_is_register ? get_reg(modrm_rm, size) : READ(effective_address, size);
}

static void write_rm(u32 value, int size)
{
    if (modrm_is_register)
        set_reg(modrm_rm, value, size);
    else
        WRITE(effective_address, value, size);
}

static void push(u32 value, int size)
{
    u32 sp = regs[ESP] - (u32)size;

    WRITE(sp, value, size);
    if (!fault)
        regs[ESP] = sp;
}

static u32 pop(int size)
{
    u32 value = READ(regs[ESP], size);

    if (!fault)
        regs[ESP] += (u32)size;
    return value;
}

/* ---- Flags --------------------------------------------------------------------- */

static void set_szp(u32 result, int size)
{
    result &= mask_of(size);
    eflags &= ~(F_ZF | F_SF | F_PF);
    if (!result)
        eflags |= F_ZF;
    if (result & sign_bit(size))
        eflags |= F_SF;
    if (parity_even[result & 0xFF])
        eflags |= F_PF;
}

enum { ALU_ADD, ALU_OR, ALU_ADC, ALU_SBB, ALU_AND, ALU_SUB, ALU_XOR, ALU_CMP };

static u32 alu(int op, u32 a, u32 b, int size)
{
    u32 m = mask_of(size), result, carry;
    u32 carry_in = (eflags & F_CF) ? 1 : 0; /* read before the flags are cleared */

    a &= m;
    b &= m;
    eflags &= ~(F_CF | F_OF | F_AF);
    switch (op) {
    case ALU_ADD:
    case ALU_ADC:
        carry = op == ALU_ADC ? carry_in : 0;
        result = (a + b + carry) & m;
        if ((u64)a + b + carry > m)
            eflags |= F_CF;
        if ((a ^ result) & (b ^ result) & sign_bit(size))
            eflags |= F_OF;
        if ((a ^ b ^ result) & 0x10)
            eflags |= F_AF;
        break;
    case ALU_SUB:
    case ALU_SBB:
    case ALU_CMP:
        carry = op == ALU_SBB ? carry_in : 0;
        result = (a - b - carry) & m;
        if ((u64)a < (u64)b + carry)
            eflags |= F_CF;
        if ((a ^ b) & (a ^ result) & sign_bit(size))
            eflags |= F_OF;
        if ((a ^ b ^ result) & 0x10)
            eflags |= F_AF;
        break;
    case ALU_OR:
        result = a | b;
        break;
    case ALU_AND:
        result = a & b;
        break;
    default:
        result = a ^ b;
        break;
    }
    set_szp(result, size);
    return result;
}

static int condition(int code)
{
    int r;

    switch (code >> 1) {
    case 0: r = (eflags & F_OF) != 0; break;
    case 1: r = (eflags & F_CF) != 0; break;
    case 2: r = (eflags & F_ZF) != 0; break;
    case 3: r = (eflags & (F_CF | F_ZF)) != 0; break;
    case 4: r = (eflags & F_SF) != 0; break;
    case 5: r = (eflags & F_PF) != 0; break;
    case 6: r = ((eflags & F_SF) != 0) != ((eflags & F_OF) != 0); break;
    default: r = (eflags & F_ZF) || (((eflags & F_SF) != 0) != ((eflags & F_OF) != 0)); break;
    }
    return code & 1 ? !r : r;
}

static u32 increment(u32 value, int delta, int size)
{
    u32 m = mask_of(size), result = (value + (u32)delta) & m;

    eflags &= ~(F_OF | F_AF);
    if (delta > 0 && result == sign_bit(size))
        eflags |= F_OF;
    if (delta < 0 && (value & m) == sign_bit(size))
        eflags |= F_OF;
    if (delta > 0 ? (value & 0xF) == 0xF : (value & 0xF) == 0)
        eflags |= F_AF;
    set_szp(result, size);
    return result;
}

static u32 shift(int op, u32 value, u32 count, int size)
{
    int bits = size * 8;
    u32 m = mask_of(size), result = value & m;

    count &= 31;
    if (count == 0)
        return result;

    switch (op) {
    case 0: /* ROL */
        count %= (u32)bits;
        result = ((value << count) | (value >> ((u32)bits - count) % (u32)bits)) & m;
        if (count == 0)
            result = value & m;
        eflags = (eflags & ~(F_CF | F_OF)) | (result & 1 ? F_CF : 0);
        if (((result & sign_bit(size)) != 0) != ((result & 1) != 0))
            eflags |= F_OF;
        return result;
    case 1: /* ROR */
        count %= (u32)bits;
        if (count)
            result = ((value >> count) | (value << ((u32)bits - count))) & m;
        eflags = (eflags & ~(F_CF | F_OF)) | (result & sign_bit(size) ? F_CF : 0);
        if (((result & sign_bit(size)) != 0) != ((result & (sign_bit(size) >> 1)) != 0))
            eflags |= F_OF;
        return result;
    case 2: /* RCL */
    case 3: /* RCR */
        count %= (u32)(bits + 1);
        for (u32 i = 0; i < count; i++) {
            u32 carry = (eflags & F_CF) ? 1 : 0;

            if (op == 2) {
                eflags = (eflags & ~F_CF) | (result & sign_bit(size) ? F_CF : 0);
                result = ((result << 1) | carry) & m;
            } else {
                eflags = (eflags & ~F_CF) | (result & 1 ? F_CF : 0);
                result = (result >> 1) | (carry ? sign_bit(size) : 0);
            }
        }
        eflags &= ~F_OF;
        if (op == 2 && (((result & sign_bit(size)) != 0) != ((eflags & F_CF) != 0)))
            eflags |= F_OF;
        if (op == 3 && (((result & sign_bit(size)) != 0) != ((result & (sign_bit(size) >> 1)) != 0)))
            eflags |= F_OF;
        return result;
    case 4: /* SHL */
    case 6:
        eflags &= ~(F_CF | F_OF);
        if (count <= (u32)bits && ((value >> ((u32)bits - count)) & 1))
            eflags |= F_CF;
        result = count < 32 ? (value << count) & m : 0;
        if (((result & sign_bit(size)) != 0) != ((eflags & F_CF) != 0))
            eflags |= F_OF;
        set_szp(result, size);
        return result;
    case 5: /* SHR */
        eflags &= ~(F_CF | F_OF);
        if ((value & m) >> (count - 1) & 1)
            eflags |= F_CF;
        if (count == 1 && (value & sign_bit(size)))
            eflags |= F_OF;
        result = (value & m) >> count;
        set_szp(result, size);
        return result;
    default: { /* SAR */
        s32 signed_value = (s32)sign_extend(value & m, size);

        eflags &= ~(F_CF | F_OF);
        if (((u32)(signed_value >> (count > 31 ? 31 : count - 1))) & 1)
            eflags |= F_CF;
        result = (u32)(signed_value >> (count > 31 ? 31 : count)) & m;
        set_szp(result, size);
        return result;
    }
    }
}

/* ---- Descriptor tables and interrupts ------------------------------------------- */

static u32 descriptor_base(u16 selector)
{
    u32 offset = (u32)(selector & ~7u);
    u32 low = linear_read(gdt_base + offset, 4, 0), high = linear_read(gdt_base + offset + 4, 4, 0);

    return (low >> 16) | ((high & 0xFF) << 16) | (high & 0xFF000000u);
}

static void deliver(u32 vector, int has_error, u32 error, u32 return_eip, int software)
{
    u32 low, high, target, old_eflags = eflags, old_esp = regs[ESP];
    u16 selector, old_cs = seg[CS], old_ss = seg[SS];
    int new_cpl;

    if (vector * 8 + 7 > idt_limit) {
        crash("interrupt beyond the IDT");
        return;
    }
    low = linear_read(idt_base + vector * 8, 4, 0);
    high = linear_read(idt_base + vector * 8 + 4, 4, 0);
    if (fault || !(high & 0x8000)) {
        crash("missing interrupt handler");
        return;
    }
    if (software && (int)((high >> 13) & 3) < cpl) {
        raise(VECTOR_GP, 1, vector * 8 + 2);
        return;
    }

    target = (low & 0xFFFF) | (high & 0xFFFF0000u);
    selector = (u16)(low >> 16);
    new_cpl = selector & 3;

    if (new_cpl < cpl) {
        u32 tss = descriptor_base(task_register);

        regs[ESP] = linear_read(tss + 4, 4, 0);
        seg[SS] = (u16)linear_read(tss + 8, 4, 0);
        cpl = new_cpl;
        push(old_ss, 4);
        push(old_esp, 4);
    } else {
        cpl = new_cpl;
    }
    push(old_eflags, 4);
    push(old_cs, 4);
    push(return_eip, 4);
    if (has_error)
        push(error, 4);
    if (fault) {
        crash("fault while entering an interrupt handler");
        return;
    }
    seg[CS] = selector;
    eip = target;
    if (((high >> 8) & 0xF) == 0xE)
        eflags &= ~F_IF;
    eflags &= ~F_TF;
}

/* ---- Instruction groups --------------------------------------------------------------- */

static void string_instruction(int op, int size)
{
    int step = (eflags & F_DF) ? -size : size;
    int compares = op == 0xA6 || op == 0xAE;

    for (;;) {
        if (rep_prefix && regs[ECX] == 0)
            return;
        switch (op) {
        case 0xA4: /* MOVS */
            WRITE(regs[EDI], READ(regs[ESI], size), size);
            regs[ESI] += (u32)step;
            regs[EDI] += (u32)step;
            break;
        case 0xA6: /* CMPS */
            alu(ALU_CMP, READ(regs[ESI], size), READ(regs[EDI], size), size);
            regs[ESI] += (u32)step;
            regs[EDI] += (u32)step;
            break;
        case 0xAA: /* STOS */
            WRITE(regs[EDI], get_reg(EAX, size), size);
            regs[EDI] += (u32)step;
            break;
        case 0xAC: /* LODS */
            set_reg(EAX, READ(regs[ESI], size), size);
            regs[ESI] += (u32)step;
            break;
        default: /* SCAS */
            alu(ALU_CMP, get_reg(EAX, size), READ(regs[EDI], size), size);
            regs[EDI] += (u32)step;
            break;
        }
        if (fault || !rep_prefix)
            return;
        regs[ECX]--;
        if (compares && rep_prefix == 0xF3 && !(eflags & F_ZF))
            return;
        if (compares && rep_prefix == 0xF2 && (eflags & F_ZF))
            return;
    }
}

static void group3(int size)
{
    u32 value = read_rm(size);

    switch (modrm_reg) {
    case 0:
    case 1:
        alu(ALU_AND, value, fetch(size), size);
        break;
    case 2:
        write_rm(~value, size);
        break;
    case 3:
        write_rm(alu(ALU_SUB, 0, value, size), size);
        if (value & mask_of(size))
            eflags |= F_CF;
        else
            eflags &= ~F_CF;
        break;
    case 4: { /* MUL */
        int high;

        if (size == 1) {
            u32 r = (regs[EAX] & 0xFF) * (value & 0xFF);

            set_reg(EAX, r, 2);
            high = (r >> 8) != 0;
        } else if (size == 2) {
            u32 r = (regs[EAX] & 0xFFFF) * (value & 0xFFFF);

            set_reg(EAX, r, 2);
            set_reg(EDX, r >> 16, 2);
            high = (r >> 16) != 0;
        } else {
            u64 r = (u64)regs[EAX] * value;

            regs[EAX] = (u32)r;
            regs[EDX] = (u32)(r >> 32);
            high = regs[EDX] != 0;
        }
        eflags = (eflags & ~(F_CF | F_OF)) | (high ? F_CF | F_OF : 0);
        break;
    }
    case 5: { /* IMUL */
        int overflow;

        if (size == 1) {
            s32 r = (s32)(s8)regs[EAX] * (s32)(s8)value;

            set_reg(EAX, (u32)r, 2);
            overflow = r != (s32)(s8)r;
        } else if (size == 2) {
            s32 r = (s32)(s16)regs[EAX] * (s32)(s16)value;

            set_reg(EAX, (u32)r, 2);
            set_reg(EDX, (u32)r >> 16, 2);
            overflow = r != (s32)(s16)r;
        } else {
            s64 r = (s64)(s32)regs[EAX] * (s32)value;

            regs[EAX] = (u32)r;
            regs[EDX] = (u32)((u64)r >> 32);
            overflow = r != (s64)(s32)r;
        }
        eflags = (eflags & ~(F_CF | F_OF)) | (overflow ? F_CF | F_OF : 0);
        break;
    }
    case 6: { /* DIV */
        value &= mask_of(size);
        if (value == 0) {
            raise(VECTOR_DE, 0, 0);
            return;
        }
        if (size == 1) {
            u32 n = regs[EAX] & 0xFFFF, q = n / value;

            if (q > 0xFF) {
                raise(VECTOR_DE, 0, 0);
                return;
            }
            set_reg(EAX, q | ((n % value) << 8), 2);
        } else if (size == 2) {
            u32 n = ((regs[EDX] & 0xFFFF) << 16) | (regs[EAX] & 0xFFFF), q = n / value;

            if (q > 0xFFFF) {
                raise(VECTOR_DE, 0, 0);
                return;
            }
            set_reg(EAX, q, 2);
            set_reg(EDX, n % value, 2);
        } else {
            u64 n = ((u64)regs[EDX] << 32) | regs[EAX], q = n / value;

            if (q > 0xFFFFFFFFull) {
                raise(VECTOR_DE, 0, 0);
                return;
            }
            regs[EAX] = (u32)q;
            regs[EDX] = (u32)(n % value);
        }
        break;
    }
    default: { /* IDIV */
        s64 divisor = (s32)sign_extend(value & mask_of(size), size), n, q;

        if (divisor == 0) {
            raise(VECTOR_DE, 0, 0);
            return;
        }
        if (size == 1)
            n = (s16)(regs[EAX] & 0xFFFF);
        else if (size == 2)
            n = (s32)(((regs[EDX] & 0xFFFF) << 16) | (regs[EAX] & 0xFFFF));
        else
            n = (s64)(((u64)regs[EDX] << 32) | regs[EAX]);
        if (n == (s64)0x8000000000000000ull && divisor == -1) {
            raise(VECTOR_DE, 0, 0);
            return;
        }
        q = n / divisor;
        if ((size == 1 && (q < -128 || q > 127)) || (size == 2 && (q < -32768 || q > 32767)) ||
            (size == 4 && (q < -2147483648LL || q > 2147483647LL))) {
            raise(VECTOR_DE, 0, 0);
            return;
        }
        if (size == 1) {
            set_reg(EAX, ((u32)q & 0xFF) | (((u32)(n % divisor) & 0xFF) << 8), 2);
        } else if (size == 2) {
            set_reg(EAX, (u32)q, 2);
            set_reg(EDX, (u32)(n % divisor), 2);
        } else {
            regs[EAX] = (u32)q;
            regs[EDX] = (u32)(n % divisor);
        }
        break;
    }
    }
}

static u32 imul_two(u32 a, u32 b, int size)
{
    s64 r = (s64)(s32)sign_extend(a, size) * (s32)sign_extend(b, size);
    int overflow = size == 2 ? r != (s64)(s16)r : r != (s64)(s32)r;

    eflags = (eflags & ~(F_CF | F_OF)) | (overflow ? F_CF | F_OF : 0);
    return (u32)r & mask_of(size);
}

static void undefined(u32 opcode)
{
    /* Logged to serial: an unknown instruction is an emulator gap worth seeing. */
    const char *text = "emu: undefined opcode ";

    while (*text)
        host_serial((u32)*text++);
    for (int i = 3; i >= 0; i--)
        host_serial((u32)"0123456789abcdef"[(opcode >> (i * 4)) & 15]);
    host_serial('\n');
    raise(VECTOR_UD, 0, 0);
}

static int privileged(void)
{
    if (cpl != 0) {
        raise(VECTOR_GP, 1, 0);
        return 0;
    }
    return 1;
}

static void bit_test(int op, int size, u32 offset, int offset_from_register)
{
    u32 bits = (u32)size * 8, bit, value;
    u32 address = effective_address;

    if (!modrm_is_register && offset_from_register) {
        address += (u32)(((s32)offset >> (size == 4 ? 5 : 4)) * size);
        bit = offset & (bits - 1);
    } else {
        bit = offset & (bits - 1);
    }
    value = modrm_is_register ? get_reg(modrm_rm, size) : READ(address, size);
    eflags = (eflags & ~F_CF) | ((value >> bit) & 1 ? F_CF : 0);
    if (op == 0)
        return;
    if (op == 1)
        value |= 1u << bit;
    else if (op == 2)
        value &= ~(1u << bit);
    else
        value ^= 1u << bit;
    if (modrm_is_register)
        set_reg(modrm_rm, value, size);
    else
        WRITE(address, value, size);
}

static void double_shift(int left, int size, u32 count)
{
    u32 dest = read_rm(size), source = get_reg(modrm_reg, size), result;
    u32 bits = (u32)size * 8;

    count &= 31;
    if (count == 0 || count > bits)
        return;
    if (size == 2) {
        u32 combined = left ? (dest << 16) | source : (source << 16) | dest;

        if (left) {
            eflags = (eflags & ~F_CF) | ((combined >> (32 - count)) & 1 ? F_CF : 0);
            result = (combined << count) >> 16;
        } else {
            eflags = (eflags & ~F_CF) | ((combined >> (count - 1)) & 1 ? F_CF : 0);
            result = combined >> count;
        }
    } else if (left) {
        eflags = (eflags & ~F_CF) | ((dest >> (32 - count)) & 1 ? F_CF : 0);
        result = (dest << count) | (count == 32 ? 0 : source >> (32 - count));
    } else {
        eflags = (eflags & ~F_CF) | ((dest >> (count - 1)) & 1 ? F_CF : 0);
        result = (dest >> count) | (count == 32 ? 0 : source << (32 - count));
    }
    result &= mask_of(size);
    eflags &= ~F_OF;
    if (((result ^ dest) & sign_bit(size)) && count == 1)
        eflags |= F_OF;
    set_szp(result, size);
    write_rm(result, size);
}

static void cpuid(void)
{
    static const char vendor[] = "BShitEmuCPU!";
    static const char brand[48] = "BShitEmu WebAssembly i686";
    u32 leaf = regs[EAX];

    regs[EAX] = regs[EBX] = regs[ECX] = regs[EDX] = 0;
    if (leaf == 0) {
        regs[EAX] = 1;
        regs[EBX] = (u32)vendor[0] | (u32)vendor[1] << 8 | (u32)vendor[2] << 16 | (u32)vendor[3] << 24;
        regs[EDX] = (u32)vendor[4] | (u32)vendor[5] << 8 | (u32)vendor[6] << 16 | (u32)vendor[7] << 24;
        regs[ECX] = (u32)vendor[8] | (u32)vendor[9] << 8 | (u32)vendor[10] << 16 | (u32)vendor[11] << 24;
    } else if (leaf == 1) {
        regs[EAX] = 0x00000611; /* family 6 */
        regs[EDX] = (1u << 3) | (1u << 4) | (1u << 15); /* PSE, TSC, CMOV */
    } else if (leaf == 0x80000000u) {
        regs[EAX] = 0x80000004u;
    } else if (leaf >= 0x80000002u && leaf <= 0x80000004u) {
        const char *p = brand + (leaf - 0x80000002u) * 16;
        u32 *out[4] = {&regs[EAX], &regs[EBX], &regs[ECX], &regs[EDX]};

        for (int i = 0; i < 4; i++)
            *out[i] = (u32)(u8)p[i * 4] | (u32)(u8)p[i * 4 + 1] << 8 | (u32)(u8)p[i * 4 + 2] << 16 |
                      (u32)(u8)p[i * 4 + 3] << 24;
    }
}

/* ---- Two-byte opcodes ------------------------------------------------------------------ */

static void execute_0f(void)
{
    u32 op = fetch(1), value;
    int size = opsize;

    if (op >= 0x80 && op <= 0x8F) { /* Jcc rel32 */
        u32 offset = fetch(4);

        if (condition((int)(op & 15)))
            eip += offset;
        return;
    }
    if (op >= 0x40 && op <= 0x4F) { /* CMOVcc */
        decode_modrm();
        value = read_rm(size);
        if (!fault && condition((int)(op & 15)))
            set_reg(modrm_reg, value, size);
        return;
    }
    if (op >= 0x90 && op <= 0x9F) { /* SETcc */
        decode_modrm();
        write_rm(condition((int)(op & 15)) ? 1 : 0, 1);
        return;
    }
    if (op >= 0xC8 && op <= 0xCF) { /* BSWAP */
        u32 v = regs[op - 0xC8];

        regs[op - 0xC8] = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
        return;
    }

    switch (op) {
    case 0x00: /* LLDT, LTR, ... */
        decode_modrm();
        value = read_rm(2);
        if (modrm_reg == 3 && privileged())
            task_register = (u16)value;
        return;
    case 0x01:
        decode_modrm();
        if (modrm_reg == 2 || modrm_reg == 3) { /* LGDT, LIDT */
            u32 limit = READ(effective_address, 2), base = READ(effective_address + 2, 4);

            if (!privileged())
                return;
            if (modrm_reg == 2) {
                gdt_limit = limit;
                gdt_base = base;
            } else {
                idt_limit = limit;
                idt_base = base;
            }
        } else if (modrm_reg == 0 || modrm_reg == 1) { /* SGDT, SIDT */
            WRITE(effective_address, modrm_reg ? idt_limit : gdt_limit, 2);
            WRITE(effective_address + 2, modrm_reg ? idt_base : gdt_base, 4);
        } else if (modrm_reg == 7) { /* INVLPG */
            if (privileged())
                tlb_flush();
        } else {
            undefined(0x0F01);
        }
        return;
    case 0x06: /* CLTS */
    case 0x08: /* INVD */
    case 0x09: /* WBINVD */
        privileged();
        return;
    case 0x0B:
        undefined(0x0F0B);
        return;
    case 0x1F: /* NOP Ev */
        decode_modrm();
        return;
    case 0x20: /* MOV r32, CRn */
        decode_modrm();
        if (!privileged())
            return;
        value = modrm_reg == 0 ? cr0 : modrm_reg == 2 ? cr2 : modrm_reg == 3 ? cr3 : modrm_reg == 4 ? cr4 : 0;
        regs[modrm_rm] = value;
        return;
    case 0x22: /* MOV CRn, r32 */
        decode_modrm();
        if (!privileged())
            return;
        value = regs[modrm_rm];
        if (modrm_reg == 0)
            cr0 = value | 0x10;
        else if (modrm_reg == 2)
            cr2 = value;
        else if (modrm_reg == 3)
            cr3 = value;
        else if (modrm_reg == 4)
            cr4 = value;
        tlb_flush();
        return;
    case 0x31: /* RDTSC */
        regs[EAX] = (u32)tsc;
        regs[EDX] = (u32)(tsc >> 32);
        return;
    case 0xA0: push(seg[FS], 4); return;
    case 0xA1: seg[FS] = (u16)pop(4); return;
    case 0xA8: push(seg[GS], 4); return;
    case 0xA9: seg[GS] = (u16)pop(4); return;
    case 0xA2:
        cpuid();
        return;
    case 0xA3: /* BT */
    case 0xAB: /* BTS */
    case 0xB3: /* BTR */
    case 0xBB: /* BTC */
        decode_modrm();
        bit_test(op == 0xA3 ? 0 : op == 0xAB ? 1 : op == 0xB3 ? 2 : 3, size, get_reg(modrm_reg, size), 1);
        return;
    case 0xBA: /* BT group with an immediate */
        decode_modrm();
        value = fetch(1);
        if (modrm_reg >= 4)
            bit_test(modrm_reg - 4, size, value, 0);
        else
            undefined(0x0FBA);
        return;
    case 0xA4: /* SHLD imm8 */
    case 0xAC: /* SHRD imm8 */
        decode_modrm();
        value = fetch(1);
        double_shift(op == 0xA4, size, value);
        return;
    case 0xA5: /* SHLD CL */
    case 0xAD: /* SHRD CL */
        decode_modrm();
        double_shift(op == 0xA5, size, regs[ECX] & 0xFF);
        return;
    case 0xAF: /* IMUL Gv, Ev */
        decode_modrm();
        value = read_rm(size);
        set_reg(modrm_reg, imul_two(get_reg(modrm_reg, size), value, size), size);
        return;
    case 0xB0: /* CMPXCHG */
    case 0xB1: {
        int s = op == 0xB0 ? 1 : size;

        decode_modrm();
        value = read_rm(s);
        alu(ALU_CMP, get_reg(EAX, s), value, s);
        if (eflags & F_ZF)
            write_rm(get_reg(modrm_reg, s), s);
        else
            set_reg(EAX, value, s);
        return;
    }
    case 0xB6: /* MOVZX */
    case 0xB7:
        decode_modrm();
        value = read_rm(op == 0xB6 ? 1 : 2);
        set_reg(modrm_reg, value, size);
        return;
    case 0xBE: /* MOVSX */
    case 0xBF:
        decode_modrm();
        value = sign_extend(read_rm(op == 0xBE ? 1 : 2), op == 0xBE ? 1 : 2);
        set_reg(modrm_reg, value, size);
        return;
    case 0xBC: /* BSF */
    case 0xBD: /* BSR */
        decode_modrm();
        value = read_rm(size) & mask_of(size);
        if (!value) {
            eflags |= F_ZF;
            return;
        }
        eflags &= ~F_ZF;
        {
            u32 index = 0;

            if (op == 0xBC)
                while (!((value >> index) & 1))
                    index++;
            else
                for (index = (u32)size * 8 - 1; !((value >> index) & 1); index--)
                    ;
            set_reg(modrm_reg, index, size);
        }
        return;
    case 0xC0: /* XADD */
    case 0xC1: {
        int s = op == 0xC0 ? 1 : size;
        u32 dest, source;

        decode_modrm();
        dest = read_rm(s);
        source = get_reg(modrm_reg, s);
        value = alu(ALU_ADD, dest, source, s);
        if (fault)
            return;
        set_reg(modrm_reg, dest, s);
        write_rm(value, s);
        return;
    }
    default:
        undefined(0x0F00 | op);
        return;
    }
}

/* ---- One-byte opcodes ------------------------------------------------------------------ */

static void execute(void)
{
    u32 op, value, a;
    int size;

    opsize = 4;
    rep_prefix = 0;
    for (;;) {
        op = fetch(1);
        if (op == 0x66)
            opsize = 2;
        else if (op == 0xF2 || op == 0xF3)
            rep_prefix = (int)op;
        else if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E || op == 0x64 || op == 0x65 || op == 0xF0 ||
                 op == 0x67)
            continue; /* flat segments; LOCK means nothing with one CPU */
        else
            break;
        if (fault)
            return;
    }
    if (fault)
        return;
    size = opsize;

    if (op < 0x40 && (op & 7) < 6) { /* the ALU block: ADD, OR, ADC, SBB, AND, SUB, XOR, CMP */
        int alu_op = (int)(op >> 3), form = (int)(op & 7);
        int s = (form & 1) ? size : 1;
        u32 result;

        if (form < 4) {
            decode_modrm();
            if (form < 2) { /* Ev, Gv */
                value = read_rm(s);
                result = alu(alu_op, value, get_reg(modrm_reg, s), s);
                if (alu_op != ALU_CMP)
                    write_rm(result, s);
            } else { /* Gv, Ev */
                value = read_rm(s);
                result = alu(alu_op, get_reg(modrm_reg, s), value, s);
                if (alu_op != ALU_CMP && !fault)
                    set_reg(modrm_reg, result, s);
            }
        } else { /* AL/eAX, imm */
            value = fetch(s);
            result = alu(alu_op, get_reg(EAX, s), value, s);
            if (alu_op != ALU_CMP)
                set_reg(EAX, result, s);
        }
        return;
    }

    switch (op) {
    case 0x06: push(seg[ES], 4); return;
    case 0x07: seg[ES] = (u16)pop(4); return;
    case 0x0E: push(seg[CS], 4); return;
    case 0x16: push(seg[SS], 4); return;
    case 0x17: seg[SS] = (u16)pop(4); return;
    case 0x1E: push(seg[DS], 4); return;
    case 0x1F: seg[DS] = (u16)pop(4); return;
    case 0x0F: execute_0f(); return;

    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        set_reg((int)(op - 0x40), increment(get_reg((int)(op - 0x40), size), 1, size), size);
        return;
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        set_reg((int)(op - 0x48), increment(get_reg((int)(op - 0x48), size), -1, size), size);
        return;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        push(get_reg((int)(op - 0x50), size), size);
        return;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        value = pop(size);
        if (!fault)
            set_reg((int)(op - 0x58), value, size);
        return;
    case 0x60: { /* PUSHA */
        u32 sp = regs[ESP];

        for (int r = 0; r < 8; r++)
            push(r == ESP ? sp : get_reg(r, size), size);
        return;
    }
    case 0x61: /* POPA */
        for (int r = 7; r >= 0; r--) {
            value = pop(size);
            if (r != ESP)
                set_reg(r, value, size);
        }
        return;
    case 0x68: push(fetch(size), size); return;
    case 0x6A: push(sign_extend(fetch(1), 1), size); return;
    case 0x69: /* IMUL Gv, Ev, Iv */
    case 0x6B: /* IMUL Gv, Ev, Ib */
        decode_modrm();
        value = read_rm(size);
        a = op == 0x69 ? fetch(size) : sign_extend(fetch(1), 1);
        set_reg(modrm_reg, imul_two(value, a, size), size);
        return;

    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
        value = sign_extend(fetch(1), 1);
        if (condition((int)(op & 15)))
            eip += value;
        return;

    case 0x80: case 0x81: case 0x83: { /* ALU group with an immediate */
        int s = op == 0x80 ? 1 : size;
        u32 result;

        decode_modrm();
        value = read_rm(s);
        a = op == 0x83 ? sign_extend(fetch(1), 1) : fetch(s);
        result = alu(modrm_reg, value, a, s);
        if (modrm_reg != ALU_CMP)
            write_rm(result, s);
        return;
    }
    case 0x84: case 0x85: {
        int s = op == 0x84 ? 1 : size;

        decode_modrm();
        alu(ALU_AND, read_rm(s), get_reg(modrm_reg, s), s);
        return;
    }
    case 0x86: case 0x87: { /* XCHG */
        int s = op == 0x86 ? 1 : size;

        decode_modrm();
        value = read_rm(s);
        write_rm(get_reg(modrm_reg, s), s);
        if (!fault)
            set_reg(modrm_reg, value, s);
        return;
    }
    case 0x88: decode_modrm(); write_rm(get_reg(modrm_reg, 1), 1); return;
    case 0x89: decode_modrm(); write_rm(get_reg(modrm_reg, size), size); return;
    case 0x8A: decode_modrm(); value = read_rm(1); if (!fault) set_reg(modrm_reg, value, 1); return;
    case 0x8B: decode_modrm(); value = read_rm(size); if (!fault) set_reg(modrm_reg, value, size); return;
    case 0x8C: decode_modrm(); write_rm(seg[modrm_reg % 6], modrm_is_register ? size : 2); return;
    case 0x8D: decode_modrm(); set_reg(modrm_reg, effective_address, size); return;
    case 0x8E: decode_modrm(); value = read_rm(2); if (!fault && modrm_reg < 6 && modrm_reg != CS) seg[modrm_reg] = (u16)value; return;
    case 0x8F: /* POP Ev */
        value = pop(size);
        decode_modrm();
        write_rm(value, size);
        return;

    case 0x90: return; /* NOP, and PAUSE with F3 */
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        value = get_reg(EAX, size);
        set_reg(EAX, get_reg((int)(op - 0x90), size), size);
        set_reg((int)(op - 0x90), value, size);
        return;
    case 0x98: /* CWDE / CBW */
        if (size == 4)
            regs[EAX] = (u32)(s32)(s16)regs[EAX];
        else
            set_reg(EAX, (u32)(s16)(s8)regs[EAX], 2);
        return;
    case 0x99: /* CDQ / CWD */
        if (size == 4)
            regs[EDX] = (regs[EAX] & 0x80000000u) ? 0xFFFFFFFFu : 0;
        else
            set_reg(EDX, (regs[EAX] & 0x8000) ? 0xFFFF : 0, 2);
        return;
    case 0x9B: return; /* WAIT */
    case 0x9C: push(eflags & 0x00FCFFFFu, size); return;
    case 0x9D: { /* POPF */
        u32 writable = cpl == 0 ? F_WRITABLE : F_WRITABLE & ~(F_IF | 0x3000u);

        value = pop(size);
        if (size == 2)
            writable &= 0xFFFF;
        eflags = (eflags & ~writable) | (value & writable) | F_FIXED;
        return;
    }
    case 0x9E: eflags = (eflags & ~0xD5u) | ((regs[EAX] >> 8) & 0xD5u) | F_FIXED; return;
    case 0x9F: set_reg(4, eflags & 0xFF, 1); return; /* AH */

    case 0xA0: value = READ(fetch(4), 1); if (!fault) set_reg(EAX, value, 1); return;
    case 0xA1: value = READ(fetch(4), size); if (!fault) set_reg(EAX, value, size); return;
    case 0xA2: WRITE(fetch(4), get_reg(EAX, 1), 1); return;
    case 0xA3: WRITE(fetch(4), get_reg(EAX, size), size); return;
    case 0xA4: case 0xA6: case 0xAA: case 0xAC: case 0xAE:
        string_instruction((int)op, 1);
        return;
    case 0xA5: case 0xA7: case 0xAB: case 0xAD: case 0xAF:
        string_instruction((int)op - 1, size);
        return;
    case 0xA8: alu(ALU_AND, regs[EAX], fetch(1), 1); return;
    case 0xA9: alu(ALU_AND, regs[EAX], fetch(size), size); return;

    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        set_reg((int)(op - 0xB0), fetch(1), 1);
        return;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        set_reg((int)(op - 0xB8), fetch(size), size);
        return;

    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: { /* shift group */
        int s = (op & 1) ? size : 1;
        u32 count;

        decode_modrm();
        value = read_rm(s);
        count = op <= 0xC1 ? fetch(1) : op <= 0xD1 ? 1 : regs[ECX] & 0xFF;
        if (!fault)
            write_rm(shift(modrm_reg, value, count, s), s);
        return;
    }
    case 0xC2: value = fetch(2); eip = pop(4); regs[ESP] += value; return;
    case 0xC3: eip = pop(4); return;
    case 0xC6: decode_modrm(); write_rm(fetch(1), 1); return;
    case 0xC7: decode_modrm(); write_rm(fetch(size), size); return;
    case 0xC8: { /* ENTER (nesting level 0) */
        u32 frame_size = fetch(2);

        fetch(1);
        push(regs[EBP], 4);
        regs[EBP] = regs[ESP];
        regs[ESP] -= frame_size;
        return;
    }
    case 0xC9: /* LEAVE */
        regs[ESP] = regs[EBP];
        regs[EBP] = pop(4);
        return;
    case 0xCC: deliver(3, 0, 0, eip, 1); return;
    case 0xCD: value = fetch(1); deliver(value, 0, 0, eip, 1); return;
    case 0xCF: { /* IRET */
        u32 new_eip = pop(4), new_cs = pop(4) & 0xFFFF, new_flags = pop(4);

        if ((int)(new_cs & 3) > cpl) {
            u32 new_esp = pop(4), new_ss = pop(4) & 0xFFFF;

            if (fault)
                return;
            regs[ESP] = new_esp;
            seg[SS] = (u16)new_ss;
        }
        if (fault)
            return;
        eip = new_eip;
        seg[CS] = (u16)new_cs;
        cpl = (int)(new_cs & 3);
        eflags = (new_flags & F_WRITABLE) | F_FIXED;
        return;
    }

    case 0xE0: case 0xE1: case 0xE2: { /* LOOPNE, LOOPE, LOOP */
        value = sign_extend(fetch(1), 1);
        regs[ECX]--;
        if (regs[ECX] && (op == 0xE2 || (op == 0xE1) == ((eflags & F_ZF) != 0)))
            eip += value;
        return;
    }
    case 0xE3: value = sign_extend(fetch(1), 1); if (regs[ECX] == 0) eip += value; return;
    case 0xE4: value = fetch(1); if (privileged()) set_reg(EAX, io_in(value, 1), 1); return;
    case 0xE5: value = fetch(1); if (privileged()) set_reg(EAX, io_in(value, size), size); return;
    case 0xE6: value = fetch(1); if (privileged()) io_out(value, regs[EAX] & 0xFF, 1); return;
    case 0xE7: value = fetch(1); if (privileged()) io_out(value, get_reg(EAX, size), size); return;
    case 0xE8: value = fetch(4); push(eip, 4); if (!fault) eip += value; return;
    case 0xE9: value = fetch(4); eip += value; return;
    case 0xEA: { /* JMP ptr16:32 */
        u32 target = fetch(4), selector = fetch(2);

        if (!privileged())
            return;
        eip = target;
        seg[CS] = (u16)selector;
        return;
    }
    case 0xEB: value = sign_extend(fetch(1), 1); eip += value; return;
    case 0xEC: if (privileged()) set_reg(EAX, io_in(regs[EDX] & 0xFFFF, 1), 1); return;
    case 0xED: if (privileged()) set_reg(EAX, io_in(regs[EDX] & 0xFFFF, size), size); return;
    case 0xEE: if (privileged()) io_out(regs[EDX] & 0xFFFF, regs[EAX] & 0xFF, 1); return;
    case 0xEF: if (privileged()) io_out(regs[EDX] & 0xFFFF, get_reg(EAX, size), size); return;

    case 0xF4: /* HLT: with interrupts off, nothing will ever wake the CPU */
        if (privileged())
            halted = 1;
        return;
    case 0xF5: eflags ^= F_CF; return;
    case 0xF6: decode_modrm(); group3(1); return;
    case 0xF7: decode_modrm(); group3(size); return;
    case 0xF8: eflags &= ~F_CF; return;
    case 0xF9: eflags |= F_CF; return;
    case 0xFA: if (privileged()) eflags &= ~F_IF; return;
    case 0xFB: if (privileged()) eflags |= F_IF; return;
    case 0xFC: eflags &= ~F_DF; return;
    case 0xFD: eflags |= F_DF; return;
    case 0xFE: /* INC/DEC Eb */
        decode_modrm();
        value = read_rm(1);
        if (modrm_reg > 1) {
            undefined(0xFE);
            return;
        }
        write_rm(increment(value, modrm_reg == 0 ? 1 : -1, 1), 1);
        return;
    case 0xFF:
        decode_modrm();
        switch (modrm_reg) {
        case 0: case 1:
            value = read_rm(size);
            write_rm(increment(value, modrm_reg == 0 ? 1 : -1, size), size);
            return;
        case 2: /* CALL Ev */
            value = read_rm(4);
            push(eip, 4);
            if (!fault)
                eip = value;
            return;
        case 4: /* JMP Ev */
            value = read_rm(4);
            if (!fault)
                eip = value;
            return;
        case 6: /* PUSH Ev */
            value = read_rm(size);
            push(value, size);
            return;
        default:
            undefined(0xFF);
            return;
        }
    default:
        undefined(op);
        return;
    }
}

/* ---- Running ------------------------------------------------------------------------- */

void cpu_reset(u32 entry, u32 stack)
{
    for (int i = 0; i < 256; i++) {
        int bits = 0;

        for (int b = 0; b < 8; b++)
            bits += (i >> b) & 1;
        parity_even[i] = (u8)!(bits & 1);
    }
    for (int i = 0; i < 8; i++)
        regs[i] = 0;
    regs[ESP] = stack;
    eip = entry;
    eflags = F_FIXED;
    for (int i = 0; i < 6; i++)
        seg[i] = 0x10;
    seg[CS] = 0x08;
    cr0 = CR0_PE | 0x10;
    cr2 = cr3 = cr4 = 0;
    gdt_base = gdt_limit = idt_base = idt_limit = 0;
    task_register = 0;
    cpl = 0;
    tsc = 0;
    halted = crashed = fault = 0;
    crash_text[0] = 0;
    tlb_flush();
}

static void step(void)
{
    struct snapshot saved;
    u32 start = eip;

    for (int i = 0; i < 8; i++)
        saved.regs[i] = regs[i];
    saved.eflags = eflags;
    for (int i = 0; i < 6; i++)
        saved.seg[i] = seg[i];
    saved.cpl = cpl;

    fault = 0;
    execute();
    tsc++;
    if (!fault)
        return;

    /* Undo the half-done instruction, then raise the exception at it. */
    for (int i = 0; i < 8; i++)
        regs[i] = saved.regs[i];
    eflags = saved.eflags;
    for (int i = 0; i < 6; i++)
        seg[i] = saved.seg[i];
    cpl = saved.cpl;
    eip = start;
    fault = 0;
    deliver(fault_vector, fault_has_error, fault_error, start, 0);
    if (fault)
        crash("double fault");
}

enum cpu_status cpu_run(u32 instructions)
{
    while (instructions--) {
        if (crashed)
            return CPU_CRASHED;
        if (halted)
            return CPU_HALTED;
        if (reboot_requested)
            return CPU_REBOOT;
        step();
    }
    return crashed ? CPU_CRASHED : halted ? CPU_HALTED : reboot_requested ? CPU_REBOOT : CPU_RUNNING;
}
