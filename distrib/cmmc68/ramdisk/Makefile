#	$NetBSD$

.include <bsd.own.mk>
.include "${NETBSDSRCDIR}/distrib/common/Makefile.distrib"

IMAGE=		root.fs
IMAGESIZE=	1536k
MAKEFS_FLAGS+=	-f 10
WARNS=		1
DBG=		-Os

CRUNCHBIN=	ramdiskbin

# Link at VA 0x1000 (page 1) — page 0 is null guard.
# max-page-size=0x1000 matches our 4KB MMU pages so ELF segments
# stay page-congruent after the crunchgen strip step.
# The default strip removes .eh_frame, which collapses the inter-segment
# padding and breaks page congruency.  Override via CRUNCHENV (command-line
# variables beat Makefile assignments) to keep .eh_frame.
CRUNCHGEN_FLAGS=	-V LDSTATIC=-static \
	-V DBG="${DBG}" -V NOMAN= -V NOLIBCSANITIZER= \
	-V NOSANITIZER= -V NOPIE= -V NOSSP= -V NOFORT= \
	-V LDFLAGS=-Wl,-Ttext-segment=0x1000,-z,max-page-size=0x1000
CRUNCHENV+=	OBJCOPY_REMOVE_FLAGS="-R .eh_frame_hdr -R .note -R .ident -R .comment -R .copyright -R .note.netbsd.mcmodel"
LISTS=		${.CURDIR}/list
MTREECONF=	${.CURDIR}/mtree.cmmc68
IMAGEENDIAN=	be
MAKEDEVTARGETS=	ramdisk

IMAGEDEPENDS+=	${CRUNCHBIN} \
		dot.profile \
		${NETBSDSRCDIR}/etc/group \
		${.CURDIR}/master.passwd \
		${.CURDIR}/etc.rc \
		${.CURDIR}/gettytab \
		${NETBSDSRCDIR}/etc/etc.cmmc68/ttys \
		${.CURDIR}/motd

# Use stubs to eliminate some large stuff from libc
HACKSRC=	${DISTRIBDIR}/utils/libhack
.include	"${HACKSRC}/Makefile.inc"
${CRUNCHBIN}:	libhack.o

# Disable SMALLPROG so programs get full features (full sh builtins,
# full init with multi-user/runcom/getty, full ls long format, etc.)
SMALLPROG=	0
CRUNCHGEN_FLAGS+=	-V USE_PAM=no -V USE_KERBEROS=no -V USE_SKEY=no
.include "${DISTRIBDIR}/common/Makefile.crunch"
.include "${DISTRIBDIR}/common/Makefile.makedev"
.include "${DISTRIBDIR}/common/Makefile.image"

release::

.include <bsd.prog.mk>
