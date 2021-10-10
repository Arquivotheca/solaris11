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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *  Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 *  All right Reserved.
 *
 *  FileName :    vxge_osdep.h
 *
 *  Description:  OSPAL - Solaris
 *
 */

#ifndef _SYS_VXGE_OSDEP_H
#define	_SYS_VXGE_OSDEP_H

#ifndef __GNUC__
#endif

#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/varargs.h>
#include <sys/atomic.h>
#include <sys/policy.h>
#include <sys/int_fmtio.h>
#include <sys/thread.h>
#include <sys/cpuvar.h>

#include <inet/common.h>
#include <inet/ip.h>
#include <inet/mi.h>
#include <inet/nd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------- includes and defines ------------------------- */

#define	VXGE_OS_PCI_CONFIG_SIZE		256

#if defined(__sparc)
#define	VXGE_OS_DMA_REQUIRES_SYNC	1
#endif

#ifdef _BIG_ENDIAN
#define	VXGE_OS_HOST_BIG_ENDIAN		1
#else
#define	VXGE_OS_HOST_LITTLE_ENDIAN	1
#endif

#if defined(__sparc)
#define	VXGE_OS_HOST_PAGE_SIZE		4096 /* should be 8K */
#else
#define	VXGE_OS_HOST_PAGE_SIZE		4096
#endif

#if defined(_LP64)
#define	VXGE_OS_PLATFORM_64BIT		1
#else
#define	VXGE_OS_PLATFORM_32BIT		1
#endif

#define	VXGE_OS_HAS_SNPRINTF		1

#ifdef ptr_t
#undef ptr_t
#endif
#define	ptr_t				size_t

/* ---------------------- fixed size primitive types ----------------------- */

#define	u8			uint8_t
#define	u16			uint16_t
#define	u32			uint32_t
#define	u64			uint64_t
typedef	u64			dma_addr_t;
#define	ulong_t			ulong_t
#define	ptrdiff_t		ptrdiff_t
typedef	kmutex_t		spinlock_t;
typedef dev_info_t		*pci_dev_h;
typedef ddi_acc_handle_t	pci_reg_h;
typedef ddi_acc_handle_t	pci_cfg_h;
typedef ddi_iblock_cookie_t	pci_irq_h;
typedef ddi_dma_handle_t	pci_dma_h;
typedef ddi_acc_handle_t	pci_dma_acc_h;
#define	FALSE			(0)
#define	TRUE			(1)

#include "vxge-defs.h"

/* LRO types */
#define	OS_NETSTACK_BUF		mblk_t *
#define	OS_LL_HEADER		uint8_t *
#define	OS_IP_HEADER		uint8_t *
#define	OS_TL_HEADER		uint8_t *

/* print formats specifiers */
#undef VXGE_OS_LLXFMT
#define	VXGE_OS_LLXFMT "%"PRIx64

#undef VXGE_OS_LLDFMT
#define	VXGE_OS_LLDFMT "%"PRId64

#undef VXGE_OS_STXFMT
#define	VXGE_OS_STXFMT "%"PRIx64

#undef VXGE_OS_STDFMT
#define	VXGE_OS_STDFMT "%"PRId64

#undef VXGE_OS_LLUFMT
#define	VXGE_OS_LLUFMT "%"PRIu64

/* -------------------------- "libc" functionality ------------------------- */

#define	vxge_os_strcpy			(void) strcpy
#define	vxge_os_strlcpy			(void)strlcpy
#define	vxge_os_strlen			strlen
#define	vxge_os_snprintf		snprintf
#define	vxge_os_memzero(addr, size)	bzero(addr, size)
#define	vxge_os_memcpy(dst, src, size)	bcopy(src, dst, size)
#define	vxge_os_memcmp(src1, src2, size)	bcmp(src1, src2, size)
#define	vxge_os_ntohl			ntohl
#define	vxge_os_htonl			htonl
#define	vxge_os_htons			htons
#define	vxge_os_ntohs			ntohs

u64 vxge_os_htonll(u64 host_longlong);

u64 vxge_os_ntohll(u64 host_longlong);

#ifdef __GNUC__
#define	vxge_os_printf(fmt...)		cmn_err(CE_CONT, fmt)
#define	vxge_os_sprintf(buf, fmt...)	strlen(sprintf(buf, fmt))
#define	vxge_os_vaprintf(hldev, vpid, fmt...) { \
	char buff[255]; \
	sprintf(buff, fmt); \
	vxge_os_printf(buff); \
}
#else
#define	vxge_os_vaprintf(hldev, vpid, fmt, args...) { \
	cmn_err(CE_CONT, fmt, args); \
}

#define	vxge_os_vaprintfs(hldev, vpid, fmt) { \
	va_list va; \
	va_start(va, fmt); \
	vcmn_err(CE_CONT, fmt, va); \
	va_end(va); \
}

void vxge_os_printf(char *fmt, ...);

#define	vxge_os_vasprintf(hldev, vpid, buf, fmt) { \
	va_list va; \
	va_start(va, fmt); \
	(void) vsprintf(buf, fmt, va); \
	va_end(va); \
}

int vxge_os_sprintf(char *buf, char *fmt, ...);

#endif

#define	vxge_os_timestamp(buf) { \
	todinfo_t todinfo = utc_to_tod(ddi_get_time()); \
	(void) vxge_os_sprintf(buf, "%02d/%02d/%02d.%02d:%02d:%02d: ", \
		todinfo.tod_day, todinfo.tod_month, \
		(1970 + todinfo.tod_year - 70), \
		todinfo.tod_hour, todinfo.tod_min, todinfo.tod_sec); \
}

#define	vxge_os_println			vxge_os_printf

#define	vxge_os_curr_time		ddi_get_time

/* -------------------- synchronization primitives ------------------------- */

#define	vxge_os_spin_lock_init(lockp, ctxh) \
	mutex_init(lockp, NULL, MUTEX_DRIVER, NULL)
#define	vxge_os_spin_lock_init_irq(lockp, irqh) \
	mutex_init(lockp, NULL, MUTEX_DRIVER, irqh)
#define	vxge_os_spin_lock_destroy(lockp, cthx) \
	(cthx = cthx, mutex_destroy(lockp))
#define	vxge_os_spin_lock_destroy_irq(lockp, cthx) \
	(cthx = cthx, mutex_destroy(lockp))
#define	vxge_os_spin_lock(lockp)			mutex_enter(lockp)
#define	vxge_os_spin_unlock(lockp)		mutex_exit(lockp)
#define	vxge_os_spin_lock_irq(lockp, flags) (flags = flags, mutex_enter(lockp))
#define	vxge_os_spin_unlock_irq(lockp, flags)	mutex_exit(lockp)

/* x86 arch will never re-order writes, Sparc can */
#define	vxge_os_wmb()				membar_producer()

#ifdef VXGE_HAL_TITAN_EMULATION
#define	vxge_os_udelay(us)			drv_usecwait(us * 100)
#define	vxge_os_mdelay(ms)			drv_usecwait(ms * 1000 * 100)
#else
#define	vxge_os_udelay(us)			drv_usecwait(us)
#define	vxge_os_mdelay(ms)			drv_usecwait(ms * 1000)
#endif
#define	vxge_os_cmpxchg(targetp, cmp, newval)		\
	sizeof (*(targetp)) == 4 ?			\
	cas32((uint32_t *)targetp, cmp, newval) :	\
	cas64((uint64_t *)targetp, cmp, newval)

#define	vxge_os_xchg(targetp, newval)			\
	sizeof (*(targetp)) == 4 ?			\
	atomic_swap_32((uint32_t *)targetp, newval) :	\
	atomic_swap_64((uint64_t *)targetp, newval)

/* ------------------------- misc primitives ------------------------------- */

#define	vxge_os_be32			u32
#define	vxge_os_unlikely(x)		(x)
#define	vxge_os_prefetch(a)		(a = a)
#define	vxge_os_prefetchw
#ifdef __GNUC__
#define	vxge_os_bug(fmt...)		cmn_err(CE_PANIC, fmt)
#else
static inline void
/*LINTED */
vxge_os_bug(char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vcmn_err(CE_PANIC, fmt, ap);
	va_end(ap);
}
#endif

/* -------------------------- compiler stuffs ------------------------------ */

#if defined(__i386)
#define	__vxge_os_cacheline_size		128
#else
#define	__vxge_os_cacheline_size		128
#endif

#ifdef __GNUC__
#define	__vxge_os_attr_cacheline_aligned	\
	__attribute__((__aligned__(__vxge_os_cacheline_size)))
#else
#define	__vxge_os_attr_cacheline_aligned
#endif

/* ---------------------- memory primitives -------------------------------- */
#ifdef	VXGE_OS_MEMORY_CHECK

typedef struct _vxge_os_malloc_t {
	void *ptr;
	u32 size;
	const char *file;
	u32 line;
} vxge_os_malloc_t;

#define	VXGE_OS_MALLOC_CNT_MAX  64*1024
extern u32 g_malloc_cnt;
extern vxge_os_malloc_t g_malloc_arr[VXGE_OS_MALLOC_CNT_MAX];

#define	VXGE_OS_MEMORY_CHECK_MALLOC(_vaddr, _size, _file, _line) {          \
	if (_vaddr) {                                                       \
	u32 i;                                                              \
	for (i = 0; i < g_malloc_cnt; i++) {                                \
		if (g_malloc_arr[i].ptr == NULL)                            \
		break;                                                      \
	}                                                                   \
	if (i == g_malloc_cnt) {                                            \
	g_malloc_cnt++;                                                     \
		if (g_malloc_cnt >= VXGE_OS_MALLOC_CNT_MAX) {               \
		vxge_os_bug("g_malloc_cnt exceed %d\n",                     \
		VXGE_OS_MALLOC_CNT_MAX);                                    \
		}                                                           \
	}                                                                   \
	g_malloc_arr[i].ptr = _vaddr;                                       \
	g_malloc_arr[i].size = _size;                                       \
	g_malloc_arr[i].file = _file;                                       \
	g_malloc_arr[i].line = _line;                                       \
	for (i = 0; i < _size; i++)                                         \
		*((u8 *)_vaddr + i) = 0x5a;                                 \
	}                                                                   \
}

#define	VXGE_OS_MEMORY_CHECK_FREE(_vaddr, _size, _file, _line) {            \
	u32 i;                                                              \
	for (i = 0; i < VXGE_OS_MALLOC_CNT_MAX; i++) {                      \
	if (g_malloc_arr[i].ptr == _vaddr) {                                \
		g_malloc_arr[i].ptr = NULL;                                 \
		if (_size && g_malloc_arr[i].size !=  _size) {              \
			vxge_os_printf("freeing wrong size %lx allocated "  \
			"at %s:%d:"VXGE_OS_LLXFMT":%ld\n",                  \
			(uintptr_t)_size,                                   \
			g_malloc_arr[i].file,                               \
			g_malloc_arr[i].line,                               \
			(u64)(ulong_t)g_malloc_arr[i].ptr,                  \
			(uintptr_t)g_malloc_arr[i].size);                   \
		}                                                           \
		break;                                                      \
	}                                                                   \
	}                                                                   \
	if (i == VXGE_OS_MALLOC_CNT_MAX) {                                  \
		vxge_os_printf("ptr "VXGE_OS_LLXFMT" not found! %s:%d\n",   \
		(u64)(ulong_t)_vaddr, _file, _line);                        \
	}                                                                   \
}

#else
#define	VXGE_OS_MEMORY_CHECK_MALLOC(prt, size, file, line)
#define	VXGE_OS_MEMORY_CHECK_FREE(vaddr, size, file, line)
#endif

void *
vxge_mem_alloc_ex(u32 size, const char *file, int line);
#pragma inline(vxge_mem_alloc_ex)

void
vxge_mem_free_ex(const void *vaddr, u32 size,
	const char *file, int line);
#pragma inline(vxge_mem_free_ex)

#define	vxge_os_malloc(pdev, size)                              \
	vxge_mem_alloc_ex(size, __FILE__, __LINE__)

#define	vxge_os_free(pdev, vaddr, size)                         \
	vxge_mem_free_ex(vaddr, size, __FILE__, __LINE__)

#define	vxge_mem_alloc(size)                                    \
	vxge_mem_alloc_ex(size, __FILE__, __LINE__)

#define	vxge_mem_free(vaddr, size)                              \
	vxge_mem_free_ex(vaddr, size, __FILE__, __LINE__)

#define	vxge_os_dma_malloc(pdev, size, dma_flags, p_dmah,	\
	p_dma_acch)                                    		\
	vxge_os_dma_malloc_ex(pdev, size, dma_flags, p_dmah,	\
	p_dma_acch, __FILE__, __LINE__)

#define	vxge_os_dma_free(pdev, vaddr, size, flags, p_dmah,	\
	p_dma_acch)						\
	vxge_os_dma_free_ex(pdev, vaddr, size, flags, p_dmah,	\
	p_dma_acch, __FILE__, __LINE__)


void *vxge_os_dma_malloc_ex(pci_dev_h pdev, unsigned long size,
	int dma_flags, pci_dma_h *p_dmah, pci_dma_acc_h *p_dma_acch,
	const char *file, int line);
#pragma inline(vxge_os_dma_malloc_ex)

extern void vxge_hal_blockpool_block_add(
			void *devh,
			void *block_addr,
			u32 length,
			pci_dma_h dma_h,
			pci_dma_acc_h acc_handle);

void vxge_os_dma_malloc_async(pci_dev_h pdev, void *devh,
	unsigned long size, int dma_flags);
#pragma inline(vxge_os_dma_malloc_async)

void vxge_os_dma_free_ex(pci_dev_h pdev, const void *vaddr, int size,
	u32 flags, pci_dma_h *p_dmah, pci_dma_acc_h *p_dma_acch,
	const char *file, int line);
#pragma inline(vxge_os_dma_free_ex)


/* --------------------------- pci primitives ------------------------------ */

#define	vxge_os_pci_read8(pdev, cfgh, where, val)	\
	(*(val) = pci_config_get8(cfgh, where))

#define	vxge_os_pci_write8(pdev, cfgh, where, val)	\
	pci_config_put8(cfgh, where, val)

#define	vxge_os_pci_read16(pdev, cfgh, where, val)	\
	(*(val) = pci_config_get16(cfgh, where))

#define	vxge_os_pci_write16(pdev, cfgh, where, val)	\
	pci_config_put16(cfgh, where, val)

#define	vxge_os_pci_read32(pdev, cfgh, where, val)	\
	(*(val) = pci_config_get32(cfgh, where))

#define	vxge_os_pci_write32(pdev, cfgh, where, val)	\
	pci_config_put32(cfgh, where, val)

/* --------------------------- io primitives ------------------------------- */

#define	vxge_os_pio_mem_read8(pdev, regh, addr)		\
	(ddi_get8(regh, (uint8_t *)(addr)))

#define	vxge_os_pio_mem_write8(pdev, regh, val, addr)	\
	(ddi_put8(regh, (uint8_t *)(addr), val))

#define	vxge_os_pio_mem_read16(pdev, regh, addr)		\
	(ddi_get16(regh, (uint16_t *)(addr)))

#define	vxge_os_pio_mem_write16(pdev, regh, val, addr)	\
	(ddi_put16(regh, (uint16_t *)(addr), val))

#define	vxge_os_pio_mem_read32(pdev, regh, addr)		\
	(ddi_get32(regh, (uint32_t *)(addr)))

#define	vxge_os_pio_mem_write32(pdev, regh, val, addr)	\
	(ddi_put32(regh, (uint32_t *)(addr), val))

#define	vxge_os_pio_mem_read64(pdev, regh, addr)		\
	(ddi_get64(regh, (uint64_t *)(addr)))

#define	vxge_os_pio_mem_write64(pdev, regh, val, addr)	\
	(ddi_put64(regh, (uint64_t *)(addr), val))

#define	vxge_os_flush_bridge vxge_os_pio_mem_read64

/* --------------------------- dma primitives ----------------------------- */

#define	VXGE_OS_DMA_DIR_TODEVICE		DDI_DMA_SYNC_FORDEV
#define	VXGE_OS_DMA_DIR_FROMDEVICE	DDI_DMA_SYNC_FORKERNEL
#define	VXGE_OS_DMA_DIR_BIDIRECTIONAL	-1
#if defined(__x86)
#define	VXGE_OS_DMA_USES_IOMMU		0
#else
#define	VXGE_OS_DMA_USES_IOMMU		1
#endif

#define	VXGE_OS_INVALID_DMA_ADDR		((dma_addr_t)0)

#define	VXGE_HAL_ALIGN_XMIT		1

dma_addr_t vxge_os_dma_map(pci_dev_h pdev, pci_dma_h dmah,
	void *vaddr, size_t size, int dir, int dma_flags);
#pragma inline(vxge_os_dma_map)

void vxge_os_dma_unmap(pci_dev_h pdev, pci_dma_h dmah,
	dma_addr_t dma_addr, size_t size, int dir);
#pragma inline(vxge_os_dma_unmap)

void vxge_os_dma_sync(pci_dev_h pdev, pci_dma_h dmah,
	dma_addr_t dma_addr, u64 dma_offset, size_t length, int dir);
#pragma inline(vxge_os_dma_sync)

int vxge_check_dma_handle(pci_dev_h pdev, ddi_dma_handle_t handle);
#pragma inline(vxge_check_dma_handle)

#ifdef __cplusplus
}
#endif

#endif /* _SYS_VXGE_OSDEP_H */
