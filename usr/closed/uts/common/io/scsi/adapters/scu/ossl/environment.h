/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _OSSL_ENVIRONMENT_H
#define	_OSSL_ENVIRONMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/systm.h>

#ifndef U64
typedef uint64_t U64;
#endif

#ifndef U32
typedef	uint32_t U32;
#endif

#ifndef U16
typedef uint16_t U16;
#endif

#ifndef U8
typedef uint8_t  U8;
#endif

#ifndef S64
typedef int64_t S64;
#endif

#ifndef S32
typedef	int32_t	S32;
#endif

#ifndef S16
typedef	short	S16;
#endif

#ifndef S8
typedef	char	S8;
#endif

#ifndef FUNCPTR
typedef	void (*FUNCPTR)(void *);
#endif

#ifndef SATI_LBA
typedef uint64_t	SATI_LBA;
#endif

/* the following defined as required by sci_types.h */
#define	sci_cb_physical_address_upper(x) (x >> 32)
#define	sci_cb_physical_address_lower(x) (x & 0xffffffffull)
#define	sci_cb_make_physical_address(x, upper, lower)	\
		(x = (((uint64_t)upper << 32) | lower))

typedef uint64_t SCI_PHYSICAL_ADDRESS;

#define	MAX_COMPILER_WORD_SIZE	64

#ifdef	_LP64
#define	OS_ARCHITECTURE	64
#else
#define	OS_ARCHITECTURE	32
#endif

#ifdef __cplusplus
#ifndef INLINE
#define	INLINE inline
#endif
#else
#ifndef INLINE
#define	INLINE
#endif
#endif

#define	PLACEMENT_HINTS(args ...)

#ifdef __cplusplus
}
#endif

#endif /* _OSSL_ENVIRONMENT_H */
