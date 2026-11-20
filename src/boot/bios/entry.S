/*
 * BSBOOT for BIOS, stage 2 entry. The boot sector jumps here (0x8000) with the
 * boot drive in DL. The C code is gcc -m16: real mode, 32-bit operations. With
 * "unreal mode" (data segments given 4 GiB limits, then back to real mode) its
 * 32-bit pointers reach memory above 1 MiB, where the kernel goes.
 */
#include "layout.h"

    .code16
    .section .text.entry, "ax"
    .globl _start
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $STACK_TOP, %esp
    movzbl %dl, %edx
    cld
    movl $__bss_start, %edi    /* the BSS is not in the file: clear it */
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorb %al, %al
    rep stosb
    calll unreal
    pushl %edx                  /* interrupts stay off: a BIOS IRQ handler could undo unreal mode */
    calll stage2_main
1:
    cli
    hlt
    jmp 1b

/* Gives DS, ES, FS and GS 4 GiB limits, which they keep back in real mode. */
    .globl unreal
unreal:
    pushfl
    cli
    pushl %eax
    lgdtl gdt_descriptor
    movl %cr0, %eax
    orb $1, %al
    movl %eax, %cr0
    jmp 1f
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movl %cr0, %eax
    andb $0xFE, %al
    movl %eax, %cr0
    jmp 2f
2:
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    popl %eax
    popfl
    retl

/*
 * void bios_call(int number, struct bios_regs *r): INT number with eax..edi
 * from r, and the results (plus eflags) stored back. DS and ES are 0.
 */
    .globl bios_call
bios_call:
    pushal
    movl 36(%esp), %eax
    movb %al, int_number
    movl 40(%esp), %eax
    pushl %eax
    movl 4(%eax), %ebx
    movl 8(%eax), %ecx
    movl 12(%eax), %edx
    movl 16(%eax), %esi
    movl 20(%eax), %edi
    movl 0(%eax), %eax
    sti                         /* the BIOS may need its interrupts (disk, timer) */
    jmp 1f                      /* the patched instruction is fetched fresh */
1:
    .byte 0xCD                  /* int */
int_number:
    .byte 0
    cli
    pushfl
    pushl %eax
    xorw %ax, %ax               /* some BIOS calls hand back other segments */
    movw %ax, %ds
    movw %ax, %es
    movl 8(%esp), %eax
    movl %ebx, 4(%eax)
    movl %ecx, 8(%eax)
    movl %edx, 12(%eax)
    movl %esi, 16(%eax)
    movl %edi, 20(%eax)
    popl %ebx
    movl %ebx, 0(%eax)
    popl %ebx
    movl %ebx, 24(%eax)
    addl $4, %esp
    popal
    calll unreal                /* and some BIOS calls drop the 4 GiB limits */
    retl

/* void enter_kernel(uint32_t entry, uint32_t bootinfo): 32-bit protected mode, cdecl call. */
    .globl enter_kernel
enter_kernel:
    cli
    movl 4(%esp), %ebx
    movl 8(%esp), %edx
    lgdtl gdt_descriptor
    movl %cr0, %eax
    orb $1, %al
    movl %eax, %cr0
    ljmpl $0x08, $protected
    .code32
protected:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $STACK_TOP, %esp
    pushl %edx                  /* _start(struct bootinfo *) */
    pushl $0
    jmp *%ebx

    .section .rodata
    .balign 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF    /* 0x08: flat 32-bit code */
    .quad 0x00CF92000000FFFF    /* 0x10: flat data, 4 GiB */
gdt_end:
gdt_descriptor:
    .word gdt_end - gdt - 1
    .long gdt
