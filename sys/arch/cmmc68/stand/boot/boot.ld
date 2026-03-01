/* NetBSD/cmmc68 Bootloader linker script
 *
 * Produces a PIC 4KB binary:
 *   0x000-0x1FF  Header (metadata)
 *   0x200-0xFFF  Executable code
 *
 * For ROM mode: load at PA 0xFE0000 (sim -base 0xFE0000)
 * For RAM mode: load at PA 0x400000 (packed with kernel)
 */

ENTRY(_start)

SECTIONS
{
    . = 0x0;

    .header : {
        *(.header)
    }

    . = 0x200;
    .text : {
        *(.text)
    }

    . = 0x1000;
    _end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }
}
