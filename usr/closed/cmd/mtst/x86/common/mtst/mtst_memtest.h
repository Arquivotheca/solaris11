/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MTST_MEMTEST_H
#define	_MTST_MEMTEST_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif

extern void mtst_memtest_close(void);
extern int mtst_memtest_ioctl(int, void *);

#ifdef __cplusplus
}
#endif

#endif /* _MTST_MEMTEST_H */
