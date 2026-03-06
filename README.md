# NetBSD/cmmc68 Source Tree

NetBSD source tree with the cmmc68 port — a custom MC68010 board with
24-bit addressing and a custom page-table MMU.

Based on NetBSD 11.99.5 (current as of early 2026).

## Building

The cross-toolchain must be built first (one-time):

```bash
./build.sh -U -m cmmc68 tools
```

Then use the wrapper script in the cmmc-68 top project directory (wip/not included):

```bash
cd .. && ./netbsd/build.sh      # full build
./netbsd/build.sh kernel        # kernel only (incremental)
./netbsd/build.sh ramdisk       # ramdisk only
./netbsd/build.sh clean         # clean all
```

Output: `cmmc68.bin` in the parent `netbsd/` directory.

## Virtual Address Space

24-bit address bus, 16 MB total. Split 12:3:1 between user, kernel, and I/O.

```
0x000000 ┌─────────────────────────────────┐
         │ User space                      │  12 MB (per-process)
0xC00000 ├─────────────────────────────────┤
         │ Kernel text/data/bss + free KVA │  3 MB
0xF00000 ├─────────────────────────────────┤
         │ I/O devices (identity mapped)   │  1 MB (supervisor-only)
0xFFFFFF └─────────────────────────────────┘
```

### Kernel VA Detail

Kernel linked at VA 0xC00000, loaded at PA 0x400000 (offset set by bootloader
MMU mappings, recorded in `kernel_pa_offset`).

| VA Range            | Contents                                   |
|---------------------|--------------------------------------------|
| 0xC00000-0xD4EFFF   | Kernel text + data + BSS (~1.3 MB)         |
| 0xD4F000-0xEFFFFF   | Free KVA (~1.7 MB) for kmem, UBC, pmap     |
| 0xF00000-0xFCFFFF   | Unmapped / available                       |
| 0xFD0000-0xFD3FFF   | MMU registers (identity mapped)            |
| 0xFD9000            | DUART registers                            |
| 0xFDC000            | PIT registers                              |
| 0xFE0000-0xFEFFFF   | ROM (64 KB)                                |
| 0xFF0000-0xFFFFBF   | SRAM (64 KB, reboot trampoline)            |
| 0xFFFFC0-0xFFFFFF   | MFP registers                              |

### User VA Detail

Programs link at VA 0x1000 (page 1). Page 0 is an unmapped null-pointer
guard. Low placement enables m68k 16-bit absolute short addressing for
the first 32 KB of the address space.

```
0x000000 ┌─────────────────────────────────┐
         │  Null guard (unmapped)          │  4 KB (1 page)
0x001000 ├─────────────────────────────────┤
         │  Text (R-X)                     │  ~1.2 MB
         │  Entry point: 0x10C8            │
~0x137000├ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┤
         │  Alignment padding              │
0x1385C0 ├─────────────────────────────────┤
         │  Data + BSS (RW-)               │  ~207 KB
~0x16C000├─────────────────────────────────┤
         │                                 │
         │  brk() / mmap() arena           │  ~7.5 MB free
         │  brk() grows upward             │
         │  mmap() hints from data end     │
         │                                 │
0x900000 ├─────────────────────────────────┤
         │  Stack guard (PROT_NONE)        │  1 MB (hard fault on access)
0xA00000 ├─────────────────────────────────┤
         │  Stack noaccess (PROT_NONE)     │  1.5 MB (demand-fault growth)
0xB80000 ├─────────────────────────────────┤
         │  Stack (RW-, grows downward)    │  512 KB initial (DFLSSIZ)
         │  SP starts at USRSTACK          │
0xC00000 └─────────────────────────────────┘  ← USRSTACK
```

Sizes shown are for the current crunchbin (~41 programs). The layout adapts
to whatever ELF is exec'd — only USRSTACK and the stack reservation are fixed.

#### Resource Limits

| Limit   | Default    | Maximum    | Notes                              |
|---------|------------|------------|------------------------------------|
| Data    | 12 MB      | 12 MB      | Uncapped (VM_MAXUSER_ADDRESS)      |
| Stack   | 512 KB     | 2 MB       | exec reserves MAXSSIZ upfront      |

Data limits are effectively uncapped — brk() grows freely until uvm_map
detects a collision with the stack reservation or an mmap'd region.

Stack limits must be explicit because `exec_subr.c` reserves the full MAXSSIZ
as PROT_NONE address space during exec. Setting MAXSSIZ too large would
overlap the text segment.

The mmap hint address (`VM_DEFAULT_ADDRESS_BOTTOMUP`) is overridden to hint
right after the data segment rather than using the default `data_addr + maxdmap`
formula, which would overflow the 24-bit user VA.

## MMU

Custom hardware MMU with:
- 128 contexts (one per process, selected by context register)
- 4096 PTEs per context (covers full 24-bit / 16 MB VA)
- 4 KB pages
- 16-bit PTEs: `[valid:1][user:1][write:1][exec:1][ppn:12]`

The kernel accesses PTEs through an 8 KB page table window at VA 0xFD2000.
Context and control registers are at VA 0xFD0000.

See `sys/arch/cmmc68/include/mmu.h` and `sys/arch/cmmc68/cmmc68/pmap.c`.

## Boot Sequence

1. **ROM overlay** — CPU fetches reset vectors from ROM at PA 0x000000
2. **Bootloader** (`stand/boot/boot.s`) — Copies itself to SRAM, sets up
   MMU page tables mapping kernel VA 0xC00000 → PA 0x400000, maps I/O
   1:1, enables MMU, jumps to kernel entry
3. **locore.s `_start`** — Sets up supervisor stack, initializes BSS,
   records ramdisk location, calls `main()`
4. **Kernel init** — `cpu_startup()` → autoconfiguration → mountroot → init
5. **Userspace** — `/sbin/init` runs `/etc/rc` then spawns `getty` on console

## Ramdisk

The crunchbin is a single statically-linked ELF containing 41 programs:
cat, chmod, chown, cp, date, dd, df, dmesg, echo, expr, getty, head,
hostname, id, init, kill, less, ln, login, ls, mkdir, mknod, mount,
mount_ffs, mv, ps, pwd, reboot, rm, sh, sleep, stty, sync, sysctl,
tail, tee, test, umount, uname, vmstat, wc, who.

The crunchbin is linked at VA 0x1000 with `-z max-page-size=0x1000` to
match the 4 KB MMU page size. The crunchgen strip step is configured to
retain `.eh_frame` (via CRUNCHENV override of OBJCOPY_REMOVE_FLAGS) because
removing it would collapse the inter-segment alignment padding and break
ELF page congruency.

The ramdisk is a big-endian FFS image (~1.4 MB) embedded into the kernel
binary by `mdsetimage`. Init runs in multi-user mode (SMALLPROG=0 in the
ramdisk Makefile prevents LETS_GET_SMALL).

Configuration: `distrib/cmmc68/ramdisk/`

## Key Port Files

```
sys/arch/cmmc68/
  cmmc68/
    locore.s          Reset entry, trap vectors, context switch, copyin/out
    machdep.c         cpu_startup, consinit, cpu_reboot, bus_space
    pmap.c            Page table management (128 contexts, TLB-less)
    trap.c            Trap dispatch, syscall return, exframesize[8]=50
    intr.c            Vectored interrupt dispatch
    mfp.c             MC68901 MFP low-level (polled TX, interrupt RX)
    mfpcon.c          MFP console tty (softint RX, line discipline, cnputc)
    pit.c             MC68230 PIT (100 Hz system clock)
    duart.c           MC68681 DUART (hardware init only)
    clock.c           hz=100 clock interface
    mainbus.c         Bus attachment (manual device init)
    autoconf.c        Autoconfiguration glue
    mem.c             /dev/mem, /dev/kmem
    stubs.c           Unimplemented kernel stubs
  conf/
    CMMC68            Kernel config (options, devices, pseudo-devices)
    files.cmmc68      Source file list for config(1)
    kern.ldscript     Kernel linker script (text at 0xC00000)
    std.cmmc68        Standard machine options
    majors.cmmc68     Device major numbers
  include/
    vmparam.h         Address space layout, resource limits, mmap hint
    mmu.h             MMU register definitions and PTE format
    param.h           PAGE_SIZE=4096, NKMEMPAGES defaults
    pte.h             PTE bits (PTE_V, PTE_U, PTE_W, PTE_EX, PTE_PPN)
    intr.h            IPL definitions
    cpu.h             cpu_info, AST, need_resched
    frame.h           Trap frame layout
    vectors.h         Interrupt vector assignments
    trap.h            Trap type definitions
    ...               (~50 headers total)
  stand/boot/
    boot.s            Bootloader (MMU setup, kernel load, handoff)
    boot.ld           Bootloader linker script

distrib/cmmc68/ramdisk/
    Makefile          Crunchbin + FFS image build
    list              Ramdisk file list (symlinks, devices, dirs)
    etc.rc            /etc/rc for multi-user boot
    dot.profile       Shell profile (PATH, TERM=vt100, emacs mode)
    gettytab          Getty config
    master.passwd     Root account (no password)
    mtree.cmmc68      Directory hierarchy spec
    motd              Message of the day

etc/etc.cmmc68/
    ttys              Console getty configuration
    MAKEDEV.conf      Device node creation rules
```

## Out-of-Tree Modifications

These shared NetBSD source files have cmmc68-specific changes:

| File | Change |
|------|--------|
| `sys/uvm/uvm_km.c` | Disable vmem qcache for arenas < 2 MB; reduce to 4-page quantum cache for arenas < 64 MB (prevents 64 KB pool allocations that exhaust small KVA) |
| `sys/uvm/uvm_bio.c` | pmap_kremove in ubc_init for stale PTE cleanup |
| `sys/crypto/aes/aes_impl.c` | NO_CRYPTO_SELFTEST guard |
| `sys/crypto/aes/aes_ccm.c` | NO_CRYPTO_SELFTEST guard |
| `sys/crypto/blake2/blake2s.c` | NO_CRYPTO_SELFTEST guard |
| `sys/crypto/chacha/chacha_impl.c` | NO_CRYPTO_SELFTEST guard |
| `bin/sh/Makefile` | SH_FULL guard around SMALLPROG (libedit in ramdisk sh) |

## Known Issues

1. **config_search crash** — T_FMTERR on format error during device
   autoconfiguration; workaround: manual device init in mainbus_attach.
2. **DUART tty not connected** — MC68681 hardware initialized with interrupt
   vector, but softint/tty layer not yet attached.
