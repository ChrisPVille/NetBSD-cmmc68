/* NetBSD/cmmc68 Bootloader — 4KB PIC format with metadata header
 *
 * Binary layout (exactly 4096 bytes):
 *   0x000-0x003: Initial SSP (MC68010 reset vector 0)
 *   0x004-0x007: Initial PC  (MC68010 reset vector 1 → code_start)
 *   0x008-0x027: Image name (32 bytes, null-terminated ASCII)
 *   0x028-0x02B: Total memory required (bytes)
 *   0x02C-0x02F: Physical load address (0xFFFFFFFF = position-independent)
 *   0x030-0x033: Entry offset (byte offset from image start to payload)
 *   0x034-0x133: Physical range table (32 entries × 8 bytes)
 *   0x134-0x1FF: Reserved (zero-padded)
 *   0x200-0xFFF: Executable bootloader code (~3.5KB)
 *
 * Operating modes (auto-detected from PC):
 *   ROM mode:  Bootloader in ROM at PA 0xFE0000, kernel at PA 0x400000
 *              VPN 0x000-0x3FF → PPN 0x400-0x7FF (offset 0x400)
 *   RAM mode:  Bootloader packed with kernel at PA 0x400000
 *              Bootloader=0x400000-0x400FFF, kernel=0x401000+
 *              VPN 0x000-0x3FE → PPN 0x401-0x7FF (offset 0x401)
 *              VPN 0x400 → PPN 0x400 (temporary self-map for execution)
 *
 * Hardware:
 *   MC68010, 24-bit address bus, custom MMU
 *   MMU control: PA 0xFD0001 (context[6:1], enable[0])
 *   MMU page table window: PA 0xFD2000 (4096 × 16-bit PTEs)
 *   PTE format: EX(15) | RW(14) | U(13) | M(12) | PPN(11:0)
 *   MFP: PA 0xFFFF00, UDR at PA 0xFFFFEF, TSR at PA 0xFFFFED
 */

   .global _start

/* ======================================================================
 * HEADER — 0x000 to 0x1FF (512 bytes)
 * ====================================================================== */
.section .header, "a"
_start:
    /* 0x000: MC68010 reset vectors */
    .long   0x007FFFFE                  /* Initial SSP: top of 4MB physical RAM */
    .long   0x00FE0200                  /* Initial PC: code_start in ROM */

    /* 0x008: Image name (32 bytes, null-padded) */
img_name:
    .ascii  "NetBSD/cmmc68\0"
    .space  18                          /* pad to 32 bytes total */

    /* 0x028: Total memory required (4MB) */
    .long   0x00400000

    /* 0x02C: Physical load address (0xFFFFFFFF = PIC) */
    .long   0xFFFFFFFF

    /* 0x030: Entry offset (0x1000 for packed kernel, 0x0 for standalone) */
    .long   0x00001000

    /* 0x034: Physical range table (32 entries × 8 bytes = 256 bytes) */
    /* Entry 0: RAM 4MB */
    .long   0x00400000
    .long   0x007FFFFF
    /* Entry 1: MMU registers */
    .long   0x00FD0000
    .long   0x00FD3FFF
    /* Entry 2: ROM/IO/MFP */
    .long   0x00FE0000
    .long   0x00FFFFFF
    /* Entries 3-31: unused (zero) */
    .space  232                         /* 29 entries × 8 bytes */

    /* 0x134-0x1FF: Reserved, zero-padded */
    .space  204

/* ======================================================================
 * CODE — 0x200 to 0xFFF (~3.5KB available)
 * ====================================================================== */
.section .text, "ax"
code_start:
    /* Set supervisor mode, interrupts disabled */
    move.w  #0x2700, %sr

    /* Disable MMU (in case it was enabled) */
    move.b  #0x00, 0xFD0001

    /* Print banner */
    lea     str_banner(%pc), %a0
    bsr     puts

    /* Print image name from header */
    lea     str_image(%pc), %a0
    bsr     puts
    lea     img_name(%pc), %a0
    bsr     puts
    lea     str_crlf(%pc), %a0
    bsr     puts

    /* ---- Detect execution mode from PC ---- */
    lea     code_start(%pc), %a0
    move.l  %a0, %d0
    and.l   #0x00FF0000, %d0
    cmp.l   #0x00FE0000, %d0
    bge     rom_mode

    /* ======== RAM MODE (packed) ========
     * Bootloader at PA 0x400000, kernel at PA 0x401000
     * Map VPN 0x000-0x3FE → PPN 0x401-0x7FF (1023 pages)
     */
    lea     str_ram(%pc), %a0
    bsr     puts

    /* Map ROM region identity (VPN 0xFE0-0xFFF, 32 pages) */
    move.l  #0xFD2000 + (0xFE0 * 2), %a0
    move.l  #0xFE0, %d1
    move.l  #31, %d2
1:  move.l  %d1, %d3
    or.l    #0xF000, %d3
    move.w  %d3, (%a0)
    addq.l  #2, %a0
    addq.l  #1, %d1
    dbra    %d2, 1b

    /* Map kernel VA: VPN 0x000-0x3FE → PPN 0x401-0x7FF (1023 pages) */
    move.l  #0xFD2000, %a0
    move.l  #0x401, %d1                /* PPN starts at 0x401 (PA 0x401000) */
    move.l  #1022, %d2                 /* 1023 pages - 1 */
2:  move.l  %d1, %d3
    or.l    #0xD000, %d3               /* EX+RW+M, supervisor only */
    move.w  %d3, (%a0)
    addq.l  #2, %a0
    addq.l  #1, %d1
    dbra    %d2, 2b

    /* Map MMU registers identity (VPN 0xFD0-0xFDF, 16 pages) */
    move.l  #0xFD2000 + (0xFD0 * 2), %a0
    move.l  #0xFD0, %d1
    move.l  #15, %d2
3:  move.l  %d1, %d3
    or.l    #0xF000, %d3
    move.w  %d3, (%a0)
    addq.l  #2, %a0
    addq.l  #1, %d1
    dbra    %d2, 3b

    /* Map VPN 0x3FF → PPN 0x400 (reuse bootloader page for full 4MB VA) */
    move.l  #0xFD2000 + (0x3FF * 2), %a0
    move.w  #0xD400, (%a0)              /* PPN 0x400 + EX+RW+M */

    /* Temporary self-map: VPN 0x400 → PPN 0x400 */
    move.l  #0xFD2000 + (0x400 * 2), %a0
    move.w  #0xD400, (%a0)              /* PPN 0x400 + EX+RW+M */

    lea     str_page(%pc), %a0
    bsr     puts

    /* Enable MMU, then immediately set stack to mapped VA */
    move.b  #0x80, 0xFD0001
    move.l  #0x003FE000, %sp

    lea     str_mmu(%pc), %a0
    bsr     puts

    /* Disable ROM overlay */
    move.b  #0x01, 0xFFFFC5            /* DDR bit 0 = output */
    move.b  #0x01, 0xFFFFC1            /* GPDR bit 0 = disable overlay */

    /* Jump to kernel at VA 0x0 */
    lea     str_boot(%pc), %a0
    bsr     puts
    jmp     0x0

    /* NOT REACHED */

/* ======== ROM MODE (separate load) ======== */
rom_mode:
    lea     str_rom(%pc), %a0
    bsr     puts

    /* Map ROM region identity (VPN 0xFE0-0xFFF, 32 pages) */
    move.l  #0xFD2000 + (0xFE0 * 2), %a0
    move.l  #0xFE0, %d1
    move.l  #31, %d2
4:  move.l  %d1, %d3
    or.l    #0xF000, %d3
    move.w  %d3, (%a0)
    addq.l  #2, %a0
    addq.l  #1, %d1
    dbra    %d2, 4b

    /* Map kernel VA: VPN 0x000-0x3FF → PPN 0x400-0x7FF (1024 pages) */
    move.l  #0xFD2000, %a0
    move.l  #0x400, %d1                /* PPN starts at 0x400 (PA 0x400000) */
    move.l  #1023, %d2                 /* 1024 pages - 1 */
5:  move.l  %d1, %d3
    or.l    #0xD000, %d3               /* EX+RW+M, supervisor only */
    move.w  %d3, (%a0)
    addq.l  #2, %a0
    addq.l  #1, %d1
    dbra    %d2, 5b

    /* Map MMU registers identity (VPN 0xFD0-0xFDF, 16 pages) */
    move.l  #0xFD2000 + (0xFD0 * 2), %a0
    move.l  #0xFD0, %d1
    move.l  #15, %d2
6:  move.l  %d1, %d3
    or.l    #0xF000, %d3
    move.w  %d3, (%a0)
    addq.l  #2, %a0
    addq.l  #1, %d1
    dbra    %d2, 6b

    lea     str_page(%pc), %a0
    bsr     puts

    /* Enable MMU, then immediately set stack to mapped VA */
    move.b  #0x80, 0xFD0001
    move.l  #0x003FF000, %sp

    lea     str_mmu(%pc), %a0
    bsr     puts

    /* Disable ROM overlay */
    move.b  #0x01, 0xFFFFC5            /* DDR bit 0 = output */
    move.b  #0x01, 0xFFFFC1            /* GPDR bit 0 = disable overlay */

    /* Jump to kernel at VA 0x0 */
    lea     str_boot(%pc), %a0
    bsr     puts
    jmp     0x0

    /* NOT REACHED */
spin:
    bra.s   spin

/* ======================================================================
 * puts — print null-terminated string at (%a0) to MFP serial
 * Clobbers: %a0 (advanced past string), %d0 (scratch)
 * ====================================================================== */
puts:
    move.l  %d0, -(%sp)
.Lp_next:
    move.b  (%a0)+, %d0
    beq.s   .Lp_done
.Lp_wait:
    btst    #7, 0xFFFFED                /* MFP TSR bit 7: TX buffer empty? */
    beq.s   .Lp_wait
    move.b  %d0, 0xFFFFEF              /* write char to MFP UDR */
    bra.s   .Lp_next
.Lp_done:
    move.l  (%sp)+, %d0
    rts

/* ======================================================================
 * String table — kept compact, shares the trailing CRLF where possible
 * ====================================================================== */
str_banner:
    .ascii  "\r\nCMMC-68 Bootloader v0.2\r\n\0"
str_image:
    .ascii  "Image: \0"
str_rom:
    .ascii  "ROM \0"
str_ram:
    .ascii  "RAM \0"
str_page:
    .ascii  "PAGE \0"
str_mmu:
    .ascii  "MMU \0"
str_boot:
    .ascii  "BOOT\r\n\0"
str_crlf:
    .ascii  "\r\n\0"
