#	$NetBSD$

.include <bsd.own.mk>
.include "${NETBSDSRCDIR}/distrib/common/Makefile.distrib"

IMAGE=		root.fs
IMAGESIZE=	768k
MAKEFS_FLAGS+=	-f 10
WARNS=		1
DBG=		-Os

CRUNCHBIN=	ramdiskbin

# cmmc68 user VA starts at 0x400000 (kernel occupies 0x0-0x3FFFFF)
CRUNCHGEN_FLAGS+=	-V LDFLAGS=-Wl,-Ttext-segment=0x400000
LISTS=		${.CURDIR}/list
MTREECONF=	${.CURDIR}/mtree.cmmc68
IMAGEENDIAN=	be
MAKEDEVTARGETS=	ramdisk

IMAGEDEPENDS+=	${CRUNCHBIN} \
		dot.profile \
		${NETBSDSRCDIR}/etc/group ${NETBSDSRCDIR}/etc/master.passwd

# Use stubs to eliminate some large stuff from libc
HACKSRC=	${DISTRIBDIR}/utils/libhack
.include	"${HACKSRC}/Makefile.inc"
${CRUNCHBIN}:	libhack.o

# Build sh with full libedit support (arrow keys, history) despite SMALLPROG=1
CRUNCHGEN_FLAGS+=	-V SH_FULL=1
.include "${DISTRIBDIR}/common/Makefile.crunch"
.include "${DISTRIBDIR}/common/Makefile.makedev"
.include "${DISTRIBDIR}/common/Makefile.image"

release::

.include <bsd.prog.mk>
