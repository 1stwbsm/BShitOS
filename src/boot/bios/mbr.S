/*
 * The BIOS boot sector. It lives in the first 440 bytes of the GPT's protective
 * MBR (the partition table after it is left alone), loads stage 2 from the gap
 * between the GPT entries and the first partition (LBA 34 on), and jumps to it
 * with the boot drive in DL. LBA reads where the BIOS has them, else
 * cylinder/head/sector reads, so even a 386's BIOS can boot it.
 */
#include "layout.h"

    .code16
    .section .text
    .globl _start
_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    sti
    ljmp $0, $start

start:
    movb %dl, drive
    movb $0x41, %ah            /* INT 13h extensions present? */
    movw $0x55AA, %bx
    int $0x13
    jc chs
    cmpw $0xAA55, %bx
    jne chs
    movw $dap, %si
    movb $0x42, %ah
    movb drive, %dl
    int $0x13
    jc fail
    jmp go

chs:
    movb $0x08, %ah            /* drive geometry */
    movb drive, %dl
    xorw %di, %di
    int $0x13
    jc fail
    andb $0x3F, %cl
    movb %cl, sectors_per_track
    incb %dh
    movb %dh, heads
    movw $STAGE2_SEGMENT, %ax
    movw %ax, %es
    movw $STAGE2_LBA, lba
next:
    movw lba, %ax
    xorw %dx, %dx
    movzbw sectors_per_track, %bx
    divw %bx                   /* ax = track, dx = sector - 1 */
    incw %dx
    movw %dx, %cx              /* cl = sector */
    xorw %dx, %dx
    movzbw heads, %bx
    divw %bx                   /* ax = cylinder, dx = head */
    movb %dl, %dh
    movb %al, %ch
    shlb $6, %ah
    orb %ah, %cl
    movb drive, %dl
    xorw %bx, %bx
    movw $0x0201, %ax          /* read one sector to es:0 */
    int $0x13
    jc fail
    movw %es, %ax
    addw $0x20, %ax            /* the next 512 bytes */
    movw %ax, %es
    incw lba
    cmpw $STAGE2_LBA + STAGE2_SECTORS, lba
    jb next

go:
    xorw %ax, %ax
    movw %ax, %es
    movb drive, %dl
    ljmp $0, $STAGE2_ADDRESS

fail:
    movw $message, %si
print:
    lodsb
    testb %al, %al
    jz hang
    movb $0x0E, %ah
    movw $7, %bx
    int $0x10
    jmp print
hang:
    hlt
    jmp hang

message:
    .asciz "BSBOOT: cannot read stage 2 from this disk"
drive:
    .byte 0
sectors_per_track:
    .byte 0
heads:
    .byte 0
lba:
    .word 0
    .balign 4
dap:                           /* INT 13h AH=42h: size, 0, count, offset, segment, LBA */
    .byte 0x10, 0
    .word STAGE2_SECTORS
    .word 0, STAGE2_SEGMENT
    .long STAGE2_LBA, 0
