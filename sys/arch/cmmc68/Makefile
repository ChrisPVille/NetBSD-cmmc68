#	$NetBSD$

# Makefile for cmmc68 tags file

# Find where m68k source files are for inclusion in tags
.include <../m68k/Makefile.inc>

TCMMC68=	../cmmc68/tags
SCMMC68=	../cmmc68/cmmc68/*.[ch] ../cmmc68/include/*.h
ACMMC68=	../cmmc68/cmmc68/*.s

# Directories in which to place tags links
DCMMC68=	cmmc68 include

.include "../../kern/Make.tags.inc"

tags:
	-ctags -wdtf ${TCMMC68} ${SCMMC68} ${SM68K} ${COMM}
	egrep "^ENTRY(.*)|^ALTENTRY(.*)" ${ACMMC68} ${AM68K} | \
	    ${TOOL_SED} -e \
		"s;\([^:]*\):\([^(]*\)(\([^, )]*\)\(.*\);\3 \1 /^\2(\3\4$$/;" \
	    >> ${TCMMC68}
	sort -o ${TCMMC68} ${TCMMC68}

links:
	-for i in ${DCMMC68}; do \
	    cd $$i && rm -f tags; ln -s ../tags tags; done


SUBDIR=	compile include

.include <bsd.subdir.mk>
