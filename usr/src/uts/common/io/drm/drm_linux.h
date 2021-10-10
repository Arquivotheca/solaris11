/* BEGIN CSTYLED */

/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef __DRM_LINUX_H__
#define __DRM_LINUX_H__

#include <sys/types.h>
#include <sys/byteorder.h>
#include "drm_atomic.h"

#define DRM_MEM_CACHED	0
#define DRM_MEM_UNCACHED 	1
#define DRM_MEM_WC 		2

#define ioremap_wc(base,size) drm_sun_ioremap((base), (size), DRM_MEM_WC)
#define ioremap(base, size)   drm_sun_ioremap((base), (size), DRM_MEM_UNCACHED)
#define iounmap(addr)         drm_sun_iounmap((addr))

#define spinlock_t                       kmutex_t
#define	spin_lock_init(l)                mutex_init((l), NULL, MUTEX_DRIVER, NULL);
#define	spin_lock(l)	                 mutex_enter(l)
#define	spin_unlock(u)                   mutex_exit(u)
#define	spin_lock_irqsave(l, flag)       mutex_enter(l)
#define	spin_unlock_irqrestore(u, flag)  mutex_exit(u)

#define mutex_lock(l)                    mutex_enter(l)
#define mutex_unlock(u)                  mutex_exit(u)

#define kmalloc           kmem_alloc
#define kzalloc           kmem_zalloc
#define kcalloc(x, y, z)  kzalloc((x)*(y), z)
#define kfree             kmem_free

#define do_gettimeofday   (void) uniqtime
#define msleep_interruptible(s)  DRM_UDELAY(s)

#define GFP_KERNEL KM_SLEEP
#define GFP_ATOMIC KM_SLEEP

#define udelay			drv_usecwait
#define mdelay(x)		udelay((x) * 1000)
#define msleep(x)		mdelay((x))
#define msecs_to_jiffies(x)	drv_usectohz((x) * 1000)
#define time_after(a,b)	((long)(b) - (long)(a) < 0)

#define	jiffies	ddi_get_lbolt()

#ifdef _BIG_ENDIAN
#define cpu_to_le16(x) LE_16(x) 
#define le16_to_cpu(x) LE_16(x)
#else
#define cpu_to_le16(x) (x) 
#define le16_to_cpu(x) (x)
#endif


#define abs(x) ((x < 0) ? -x : x)

#define div_u64(x, y) ((unsigned long long)(x))/((unsigned long long)(y))  /* XXX FIXME */
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))

#define put_user(val,ptr) DRM_COPY_TO_USER(ptr,(&val),sizeof(val))
#define get_user(x,ptr) DRM_COPY_FROM_USER((&x),ptr,sizeof(x))
#define copy_to_user DRM_COPY_TO_USER
#define copy_from_user DRM_COPY_FROM_USER
#define unlikely(a)  (a)

#define BUG_ON(a)	ASSERT(!(a))

#define ALIGN(x, a)	(((x) + ((a) - 1)) & ~((a) - 1))

typedef unsigned long dma_addr_t;
typedef uint64_t	u64;
typedef uint32_t	u32;
typedef uint16_t	u16;
typedef uint8_t		u8;
typedef uint_t		irqreturn_t;

typedef int		bool;

#define true		(1)
#define false		(0)

#define __init
#define __exit
#define __iomem

#ifdef _ILP32
typedef u32 resource_size_t;
#else /* _LP64 */
typedef u64 resource_size_t;
#endif

typedef struct kref {
	atomic_t refcount;
} kref_t;

extern void kref_init(struct kref *kref);
extern void kref_get(struct kref *kref);
extern void kref_put(struct kref *kref, void (*release)(struct kref *kref));

extern unsigned int hweight16(unsigned int w);

extern long IS_ERR(const void *ptr);

#endif /* __DRM_LINUX_H__ */
