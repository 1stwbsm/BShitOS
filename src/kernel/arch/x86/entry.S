/*
 * Kernel entry. BSBOOT calls _start(struct bootinfo *) with cdecl, in 32-bit
 * protected mode, interrupts off. The firmware's GDT and stack are about to
 * become untrustworthy, so install our own straight away.
 */
    .section .text.boot, "ax"
    .globl _start
_start:
    cli
    movl 4(%esp), %eax
    lgdt gdt_descriptor
    ljmp $0x08, $1f
1:
    movw $0x10, %cx
    movw %cx, %ds
    movw %cx, %es
    movw %cx, %fs
    movw %cx, %gs
    movw %cx, %ss
    movl $stack_top, %esp
    xorl %ebp, %ebp
    subl $12, %esp
    pushl %eax
    call kmain
2:
    cli
    hlt
    jmp 2b

    .section .rodata
    .balign 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF /* 0x08: flat ring 0 code */
    .quad 0x00CF92000000FFFF /* 0x10: flat ring 0 data */
gdt_end:
gdt_descriptor:
    .word gdt_end - gdt - 1
    .long gdt

    .section .bss
    .balign 16
    .space 65536
stack_top:

    .section .note.GNU-stack, "", @progbits
