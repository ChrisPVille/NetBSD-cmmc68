/*
 * Assembly macros for CMMC68
 * Include m68k macros and add platform-specific definitions
 */
#include <m68k/asm.h>

#ifndef _LOCORE
/* Include function code definitions for C code */
#include <m68k/fcode.h>
#else
/* For assembly, define only the constants needed by copy.s */
#define FC_USERD	1
#define FC_USERP	2
#define FC_SUPERD	5
#define FC_SUPERP	6
#define FC_CPU		7
#endif
