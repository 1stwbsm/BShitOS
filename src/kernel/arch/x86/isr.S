/* Trap stubs and the ring 0 <-> ring 3 transitions. */
    .text

    .macro STUB n
isr\n:
    .if !(\n == 8 || \n == 10 || \n == 11 || \n == 12 || \n == 13 || \n == 14 || \n == 17 || \n == 21 || \n == 29 || \n == 30)
    pushl $0
    .endif
    pushl $\n
    jmp isr_common
    .endm

    .irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,128
    STUB \n
    .endr

isr_common:
    pushal
    pushl %ds
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    pushl %esp
    call trap_handler
    addl $4, %esp
    popl %ds
    movw %ds, %ax
    movw %ax, %es
    popal
    addl $8, %esp
    iret

    .globl enter_user
enter_user:
    movl 4(%esp), %eax
    movl 8(%esp), %ecx
    movl 12(%esp), %edx
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl %esp, (%edx)
    movl %esp, kernel_tss + 4
    movw $0x23, %bx
    movw %bx, %ds
    movw %bx, %es
    movw %bx, %fs
    movw %bx, %gs
    pushl $0x23
    pushl %ecx
    pushl $0x2
    pushl $0x1B
    pushl %eax
    iret

    .globl leave_user
leave_user:
    movl 8(%esp), %eax
    movl 4(%esp), %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret

    .section .rodata
    .globl isr_exceptions
isr_exceptions:
    .irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .long isr\n
    .endr
    .globl isr_syscall
isr_syscall:
    .long isr128

    .section .note.GNU-stack, "", @progbits
