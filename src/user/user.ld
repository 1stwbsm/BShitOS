/* Flat layout for .BS programs and .blib libraries. BASE comes from --defsym. */
OUTPUT_FORMAT(elf32-i386)

SECTIONS
{
    . = BASE;
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(COMMON) *(.bss .bss.*) }

    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
