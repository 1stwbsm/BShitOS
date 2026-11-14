/* Program entry. The kernel leaves [argc][argv] at the stack pointer. */
    .section .text.start, "ax"
    .globl _start
_start:
    popl %eax
    popl %ecx
    pushl %ecx
    pushl %eax
    call main
    pushl %eax
    call exit

    .section .note.GNU-stack, "", @progbits
