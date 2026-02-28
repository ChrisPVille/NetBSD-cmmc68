/* Linker script for minimal CMMC68 init binary */
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(7);     /* RWX */
    note PT_NOTE FLAGS(4);     /* R */
}

SECTIONS
{
    . = 0x400000;

    .text : { *(.text) *(.rodata*) } :text
    .data : { *(.data) }
    .bss : { *(.bss) }

    .note.netbsd.ident : { *(.note.netbsd.ident) } :text :note

    /DISCARD/ : { *(.comment) *(.eh_frame) }
}
