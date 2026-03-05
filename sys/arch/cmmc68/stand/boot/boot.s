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
 *   0x134-0x137: Kernel binary size (bytes, patched by build.sh)
 *   0x138-0x13B: Ramdisk image size (bytes, patched by build.sh)
 *   0x13C-0x1FF: Reserved (zero-padded)
 *   0x200-0xFFF: Executable bootloader code (~3.5KB)
 *
 * VA layout (12:3:1 split):
 *   0x000000-0xBFFFFF  User space (12MB)
 *   0xC00000-0xEFFFFF  Kernel (3MB)
 *   0xF00000-0xFFFFFF  I/O hardware (1MB, identity-mapped)
 *
 * Operating modes (auto-detected from PC):
 *   ROM mode:  Bootloader in ROM at PA 0xFE0000, kernel at PA 0x400000
 *              VPN 0xC00-0xEFF → PPN 0x400-0x6FF (768 pages)
 *   RAM mode:  Bootloader packed with kernel at PA 0x400000
 *              Bootloader=0x400000-0x400FFF, kernel=0x401000+
 *              VPN 0xC00-0xEFF → PPN 0x401+ (768 pages)
 *              Ramdisk mapped at VPN 0x800+ (supervisor, no PTE_U)
 *
 * Hardware:
 *   MC68010, 24-bit address bus, custom MMU
 *   MMU control: PA 0xFD0001 (enable[7], context[6:0])
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

    /* 0x028: Total memory required (8MB) */
    .long   0x00800000

    /* 0x02C: Physical load address (0xFFFFFFFF = PIC) */
    .long   0xFFFFFFFF

    /* 0x030: Entry offset (0x1000 for packed kernel, 0x0 for standalone) */
    .long   0x00001000

    /* 0x034: Physical range table (32 entries × 8 bytes = 256 bytes) */
    /* Entry 0: RAM 8MB */
    .long   0x00400000
    .long   0x00BFFFFF
    /* Entry 1: MMU registers */
    .long   0x00FD0000
    .long   0x00FD3FFF
    /* Entry 2: ROM/IO/MFP */
    .long   0x00FE0000
    .long   0x00FFFFFF
    /* Entries 3-31: unused (zero) */
    .space  232                         /* 29 entries × 8 bytes */

    /* 0x134: Kernel binary size (patched by build.sh) */
kernel_size:
    .long   0x00000000
    /* 0x138: Ramdisk image size (patched by build.sh) */
ramdisk_size:
    .long   0x00000000

    /* 0x13C-0x1FF: Reserved, zero-padded */
    .space  196

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
     * New VA layout: kernel at 0xC00000, ramdisk at 0x800000
     */
    lea     str_ram(%pc), %a0
    bsr     puts

    /* ---- Step 1: Clear all 4096 PTEs to INVALID ---- */
    move.l  #0xFD2000, %a0
    move.l  #2047, %d2                  /* 4096/2 - 1 (clear 2 entries per loop) */
clr_loop:
    clrl    (%a0)+                      /* clear 2 PTEs (4 bytes = 2 × 16-bit) */
    dbra    %d2, clr_loop

    /* ---- Step 2: Read kernel_size and ramdisk_size from header ---- */
    lea     kernel_size(%pc), %a6
    move.l  (%a6), %d4                  /* d4 = kernel_size (bytes) */
    lea     ramdisk_size(%pc), %a6
    move.l  (%a6), %d5                  /* d5 = ramdisk_size (bytes) */

    /* ---- Step 3: Compute ramdisk PPN range ---- */
    /* Kernel starts at PA 0x401000 (PPN 0x401).
     * kernel_size is in bytes. Round up to page boundary.
     * ramdisk_pa_start = 0x401000 + round_page(kernel_size)
     * ramdisk_ppn_start = ramdisk_pa_start >> 12
     */
    move.l  %d4, %d0                    /* d0 = kernel_size */
    add.l   #0xFFF, %d0                 /* round up */
    and.l   #0xFFFFF000, %d0            /* mask to page boundary */
    move.l  #0x401000, %d1
    add.l   %d0, %d1                    /* d1 = ramdisk PA start */
    lsr.l   #8, %d1
    lsr.l   #4, %d1                     /* d1 = ramdisk PPN start */

    /* Compute ramdisk page count */
    move.l  %d5, %d0                    /* d0 = ramdisk_size */
    add.l   #0xFFF, %d0                 /* round up */
    lsr.l   #8, %d0
    lsr.l   #4, %d0                     /* d0 = ramdisk page count */

    /* ---- Step 4: Map ramdisk at VPN 0x800+ ---- */
    /* VPN 0x800 = VA 0x800000. Supervisor-only (no PTE_U).
     * PTE = EX+RW+M | PPN = 0xD000 | PPN
     */
    move.l  #0xFD2000 + (0x800 * 2), %a0  /* PTE window + VPN 0x800 offset */
    move.l  %d1, %d3                    /* d3 = current PPN (ramdisk start) */
    subq.l  #1, %d0                     /* adjust for dbra */
    bmi.s   skip_ramdisk                /* skip if no ramdisk pages */
rd_loop:
    move.l  %d3, %d6
    or.l    #0xD000, %d6               /* EX+RW+M, supervisor only */
    move.w  %d6, (%a0)
    addq.l  #2, %a0
    addq.l  #1, %d3
    dbra    %d0, rd_loop
skip_ramdisk:

    /* ---- Step 5: Map kernel+free-RAM at VPN 0xC00-0xEFF ---- */
    /* 768 pages from PPN 0x401 (PA 0x401000)
     * PTE = EX+RW+M | PPN
     */
    move.l  #0xFD2000 + (0xC00 * 2), %a0
    move.l  #0x401, %d1                /* PPN starts at 0x401 */
    move.l  #767, %d2                  /* 768 pages - 1 */
kern_loop:
    move.l  %d1, %d3
    or.l    #0xD000, %d3               /* EX+RW+M, supervisor only */
    move.w  %d3, (%a0)
    addq.l  #2, %a0
    addq.l  #1, %d1
    dbra    %d2, kern_loop

    /* ---- Step 6: Map I/O at VPN 0xF00-0xFFF (identity) ---- */
    move.l  #0xFD2000 + (0xF00 * 2), %a0
    move.l  #0xF00, %d1
    move.l  #255, %d2                  /* 256 pages */
io_loop:
    move.l  %d1, %d3
    or.l    #0xF000, %d3               /* EX+RW+M+U (identity, all access) */
    move.w  %d3, (%a0)
    addq.l  #2, %a0
    addq.l  #1, %d1
    dbra    %d2, io_loop

    /* ---- Step 7: Map MMU regs at VPN 0xFD0-0xFDF (identity) ---- */
    /* Already mapped in step 6 above (F00-FFF includes FD0-FDF) */

    /* ---- Map temporary self-map for bootloader execution ---- */
    /* VPN 0x400 → PPN 0x400 so we can keep running at current PC */
    move.l  #0xFD2000 + (0x400 * 2), %a0
    move.w  #0xD400, (%a0)              /* PPN 0x400 + EX+RW+M */

    lea     str_page(%pc), %a0
    bsr     puts

    /* ---- Step 8: Enable MMU ---- */
    move.b  #0x80, 0xFD0001
    move.l  #0x00EFE000, %sp            /* new SP in kernel VA space */

    lea     str_mmu(%pc), %a0
    bsr     puts

    /* ---- Step 9: Pass ramdisk info to kernel ---- */
    /* %d3 = ramdisk_size, %d4 = ramdisk_va (0x800000) */
    lea     ramdisk_size(%pc), %a6
    move.l  (%a6), %d3                  /* d3 = ramdisk_size */
    move.l  #0x00800000, %d4            /* d4 = ramdisk VA */

    /* ---- Step 10: Jump to kernel at VA 0xC00000 ---- */
    lea     str_boot(%pc), %a0
    bsr     puts
    jmp     0xC00000

    /* NOT REACHED */

/* ======== ROM MODE (separate load) ======== */
rom_mode:
    lea     str_rom(%pc), %a0
    bsr     puts

    /* ---- Clear all 4096 PTEs to INVALID ---- */
    move.l  #0xFD2000, %a0
    move.l  #2047, %d2
rom_clr:
    clrl    (%a0)+
    dbra    %d2, rom_clr

    /* ---- Map kernel VA: VPN 0xC00-0xEFF → PPN 0x400-0x6FF (768 pages) ---- */
    move.l  #0xFD2000 + (0xC00 * 2), %a0
    move.l  #0x400, %d1                /* PPN starts at 0x400 (PA 0x400000) */
    move.l  #767, %d2                  /* 768 pages - 1 */
rom_kern:
    move.l  %d1, %d3
    or.l    #0xD000, %d3               /* EX+RW+M, supervisor only */
    move.w  %d3, (%a0)
    addq.l  #2, %a0
    addq.l  #1, %d1
    dbra    %d2, rom_kern

    /* ---- Map I/O at VPN 0xF00-0xFFF (identity, 256 pages) ---- */
    move.l  #0xFD2000 + (0xF00 * 2), %a0
    move.l  #0xF00, %d1
    move.l  #255, %d2
rom_io:
    move.l  %d1, %d3
    or.l    #0xF000, %d3
    move.w  %d3, (%a0)
    addq.l  #2, %a0
    addq.l  #1, %d1
    dbra    %d2, rom_io

    lea     str_page(%pc), %a0
    bsr     puts

    /* Enable MMU, set stack */
    move.b  #0x80, 0xFD0001
    move.l  #0x00EFE000, %sp

    lea     str_mmu(%pc), %a0
    bsr     puts

    /* No ramdisk in ROM mode */
    moveq   #0, %d3                    /* ramdisk_size = 0 */
    moveq   #0, %d4                    /* ramdisk_va = 0 */

    /* Jump to kernel at VA 0xC00000 */
    lea     str_boot(%pc), %a0
    bsr     puts
    jmp     0xC00000

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
    .ascii  "\r\nCMMC-68 Bootloader v0.3\r\n\0"
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
