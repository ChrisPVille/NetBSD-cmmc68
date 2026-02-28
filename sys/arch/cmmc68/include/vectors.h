/*	$NetBSD$	*/

#include <m68k/vectors.h>

#ifdef _KERNEL

/* CMMC68 uses common m68k bus error and address error handlers */
#define MACHINE_BUSERR_HANDLER  buserr
#define MACHINE_ADDRERR_HANDLER addrerr

/* Autovector handlers for interrupt levels 0-7 */
#define MACHINE_AV0_HANDLER     intrhand_autovec
#define MACHINE_AV1_HANDLER     intrhand_autovec
#define MACHINE_AV2_HANDLER     intrhand_autovec
#define MACHINE_AV3_HANDLER     intrhand_autovec
#define MACHINE_AV4_HANDLER     intrhand_autovec
#define MACHINE_AV5_HANDLER     intrhand_autovec
#define MACHINE_AV6_HANDLER     intrhand_autovec
#define MACHINE_AV7_HANDLER     intrhand_autovec

#endif /* _KERNEL */
