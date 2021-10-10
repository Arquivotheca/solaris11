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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SXGE_IMPL_H
#define	_SXGE_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _ASM
#include <sys/types.h>
#include <sys/byteorder.h>
#include <sys/debug.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strlog.h>
#include <sys/strsubr.h>
#include <sys/cmn_err.h>
#include <sys/vtrace.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/strsun.h>
#include <sys/stat.h>
#include <sys/cpu.h>
#include <sys/kstat.h>
#include <inet/common.h>
#include <inet/ip.h>
#include <inet/ip6.h>
#include <sys/dlpi.h>
#include <inet/nd.h>
#include <netinet/in.h>
#include <sys/ethernet.h>
#include <sys/vlan.h>
#include <sys/pci.h>
#include <sys/taskq.h>
#include <sys/atomic.h>

#include <sys/netlb.h>
#include <sys/ddi_intr.h>

#include <sys/mac_provider.h>
#include <sys/mac_ether.h>

typedef kmutex_t			sxge_os_mutex_t;
typedef	krwlock_t			sxge_os_rwlock_t;

typedef	dev_info_t			sxge_dev_info_t;
typedef	ddi_iblock_cookie_t 		sxge_intr_cookie_t;

typedef ddi_acc_handle_t		sxge_os_acc_handle_t;
#if defined(__i386)
typedef	uint32_t			sxge_reg_ptr_t;
#else
typedef	uint64_t			sxge_reg_ptr_t;
#endif

typedef ddi_dma_handle_t		sxge_os_dma_handle_t;
typedef frtn_t				sxge_os_frtn_t;

#define	SXGE_SUCCESS			(DDI_SUCCESS)
#define	SXGE_FAILURE			(DDI_FAILURE)
#define	SXGE_INTR_CLAIMED		(DDI_INTR_CLAIMED)

#define	MUTEX_INIT(lock, name, type, arg)	\
					mutex_init(lock, name, type, arg)
#define	MUTEX_ENTER(lock)		mutex_enter(lock)
#define	MUTEX_TRY_ENTER(lock)		mutex_tryenter(lock)
#define	MUTEX_EXIT(lock)		mutex_exit(lock)
#define	MUTEX_DESTROY(lock)		mutex_destroy(lock)

#define	RW_INIT(lock, name, type, arg)	rw_init(lock, name, type, arg)
#define	RW_ENTER_WRITER(lock)		rw_enter(lock, RW_WRITER)
#define	RW_ENTER_READER(lock)		rw_enter(lock, RW_READER)
#define	RW_TRY_ENTER(lock, type)	rw_tryenter(lock, type)
#define	RW_EXIT(lock)			rw_exit(lock)
#define	RW_DESTROY(lock)		rw_destroy(lock)

#define	SXGE_ALLOC(size) 		kmem_alloc(size, KM_SLEEP)
#define	SXGE_ZALLOC(size)		kmem_zalloc(size, KM_SLEEP)
#define	SXGE_FREE(buf, size)		kmem_free(buf, size)

#define	SXGE_ESBALLOC(buf, sz, pri, freep)	desballoc(buf, sz, pri, freep)
#if 0
#define	SXGE_ESBFREE
#endif

#define	SXGE_ALLOCB(sz)		allocb(sz, BPRI_LO)
#define	SXGE_FREEB(mp)		freeb(mp)

#define	SXGE_UDELAY(sxge, usec, busy)	\
	{	\
		if (busy) {	\
		    (void) drv_usecwait(usec);	\
		} else {	\
		    (void) cv_timedwait(&sxge->nbw_cv, &sxge->nbw_lock,	\
			    drv_usectohz(usec));	\
		}	\
	}

#define	SXGE_MDELAY(sxge, msec, busy)	 (SXGE_UDELAY(sxge, msec * 1000, busy))

#define	SXGE_DBG(params)	sxge_dbg_msg params
#ifdef SXGE_DEBUG
#define	SXGE_DBG0(params)	sxge_dbg_msg params
#else
#define	SXGE_DBG0(params)
#endif


/*
 * Debug message level and block encoding:
 * Debug code is 32 bits long with the lower 16 bits used for the debug
 * level/priority and the upper 16 bits used for the block.
 */
#define	SXGE_ERR_PRI	0x00000001
#define	SXGE_WARN_PRI1	0x00000002
#define	SXGE_WARN_PRI2	0x00000003
#define	SXGE_WARN_PRI3	0x00000004
#define	SXGE_INFO_PRI	0x00000005
#define	SXGE_DBG_NONE	0x0000ffff

#define	SXGE_GLD_BLK	0x00010000
#define	SXGE_INIT_BLK	0x00020000
#define	SXGE_UNINIT_BLK	0x00040000
#define	SXGE_STATS_BLK	0x00080000
#define	SXGE_CFG_BLK	0x00100000
#define	SXGE_MBOX_BLK	0x00200000
#define	SXGE_RXDMA_BLK	0x00400000
#define	SXGE_TXDMA_BLK	0x00800000
#define	SXGE_VMAC_BLK	0x01000000
#define	SXGE_INTR_BLK	0x02000000
#define	SXGE_ERR_BLK	0x40000000
#define	SXGE_COSIM_BLK	0x80000000

#define	SXGE_DBG_LVL_MSK	0xffff
#define	SXGE_DBG_BLK_MSK	0xffff0000
#define	SXGE_DBG_BLK_SH		16

typedef struct _pci_cfg_t {
	uint16_t vendorid;
	uint16_t devid;
	uint16_t command;
	uint16_t status;
	uint8_t  revid;
	uint8_t  res0;
	uint16_t junk1;
	uint8_t  cache_line;
	uint8_t  latency;
	uint8_t  header;
	uint8_t  bist;
	uint32_t base;
	uint32_t base14;
	uint32_t base18;
	uint32_t base1c;
	uint32_t base20;
	uint32_t base24;
	uint32_t base28;
	uint32_t base2c;
	uint32_t base30;
	uint32_t res1[2];
	uint8_t int_line;
	uint8_t int_pin;
	uint8_t	min_gnt;
	uint8_t max_lat;
} pci_cfg_t;

typedef struct _sxge_pcicfg_handle_t {
	sxge_os_acc_handle_t	regh;	/* PCI config DDI IO handle */
	pci_cfg_t		*regp;	/* mapped PCI registers */
	void			*sxge;
} sxge_pcicfg_handle_t;

typedef struct _sxge_pio_handle_t {
	sxge_os_acc_handle_t	regh;	/* device DDI IO (BAR 0) */
	sxge_reg_ptr_t		regp;	/* mapped device registers */
	void			*sxge;
} sxge_pio_handle_t;

typedef struct _sxge_msix_handle_t {
	sxge_os_acc_handle_t	regh;	/* MSI/X DDI handle (BAR 2) */
	sxge_reg_ptr_t		regp; /* MSI/X register */
	void			*sxge;
} sxge_msix_handle_t;

typedef struct _sxge_rom_handle_t {
	sxge_os_acc_handle_t	regh;	/* fcode rom handle */
	unsigned char		*regp;	/* fcode pointer */
	void			*sxge;
} sxge_rom_handle_t;

#define	SXGE_GET32(os_handle, offset)		\
	(ddi_get32(os_handle.regh,			\
	(uint32_t *)(os_handle.regp + offset)))
#define	SXGE_PUT32(os_handle, offset, data)		\
	(ddi_put32(os_handle.regh,				\
	(uint32_t *)(os_handle.regp + offset), data))

#if defined(__i386)
#define	SXGE_GET64(os_handle, offset)		\
	(ddi_get64(os_handle.regh,			\
	(uint64_t *)(os_handle.regp + (uint32_t)offset)))
#define	SXGE_PUT64(os_handle, offset, data)		\
	(ddi_put64(os_handle.regh,				\
	(uint64_t *)(os_handle.regp + (uint32_t)offset), data))
#else
#define	SXGE_GET64(os_handle, offset)		\
	(ddi_get64(os_handle.regh,			\
	(uint64_t *)(os_handle.regp + offset)))
#define	SXGE_PUT64(os_handle, offset, data)		\
	(ddi_put64(os_handle.regh,				\
	(uint64_t *)(os_handle.regp + offset), data))
#endif

#define	SXGE_MGET64(os_handle, addr)	\
	ddi_get64(os_handle.regh, (uint64_t *)addr)
#define	SXGE_MPUT64(os_handle, addr, data)	\
	ddi_put64(os_handle.regh, (uint64_t *)addr, data)
#define	SXGE_MGETN(os_handle, addr, datap, len)		\
{							\
	uint8_t *dp = (uint8_t *)datap;			\
	uint8_t *ad = (uint8_t *)addr;			\
	while (len) {					\
		*dp = ddi_get8(os_handle.regh, ad);	\
		len--;					\
		dp++;					\
		ad++;					\
	}						\
}

#define	SXGE_MPUTN(os_handle, addr, datap, len)		\
{							\
	uint8_t *dp = (uint8_t *)datap;			\
	uint8_t *ad = (uint8_t *)addr;			\
	while (len) {					\
		ddi_put8(os_handle.regh, ad, *dp);	\
		len--;					\
		dp++;					\
		ad++;					\
	}						\
}

#define	SXGE_CGET16(pci_handle, addr)			\
	(pci_config_get16(pci_handle.regh, addr))
#define	SXGE_CGET32(pci_handle, addr)			\
	(pci_config_get32(pci_handle.regh, addr))
#define	SXGE_CGET64(pci_handle, addr)			\
	(pci_config_get64(pci_handle.regh, addr))
#define	SXGE_CPUT32(pci_handle, addr, data)		\
	(pci_config_put32(pci_handle.regh, addr, data))
#define	SXGE_CPUT64(pci_handle, addr, data)		\
	(pci_config_put64(pci_handle.regh, addr, data))

#define	FM_SERVICE_RESTORED(sxge)				\
	if (DDI_FM_EREPORT_CAP(sxge->fm_capabilities))		\
		ddi_fm_service_impact(sxge->dip, DDI_SERVICE_RESTORED)
#define	FM_REPORT_ERROR(sxge, chan, ereport_id)		\
	if (DDI_FM_EREPORT_CAP(sxge->fm_capabilities))		\
		sxge_fm_report_error(sxge, chan, ereport_id)

#define	SXGE_SERVICE_LOST	DDI_SERVICE_LOST
#define	SXGE_SERVICE_DEGRADED	DDI_SERVICE_DEGRADED
#define	SXGE_SERVICE_UNAFFECTED	DDI_SERVICE_UNAFFECTED
#define	SXGE_SERVICE_RESTORED	DDI_SERVICE_RESTORED

#define	SXGE_DATAPATH_FAULT	DDI_DATAPATH_FAULT
#define	SXGE_DEVICE_FAULT	DDI_DEVICE_FAULT
#define	SXGE_EXTERNAL_FAULT	DDI_EXTERNAL_FAULT

#define	NOTE_LINK_UP		DL_NOTE_LINK_UP
#define	NOTE_LINK_DOWN		DL_NOTE_LINK_DOWN
#define	NOTE_SPEED		DL_NOTE_SPEED
#define	NOTE_PHYS_ADDR		DL_NOTE_PHYS_ADDR
#define	NOTE_AGGR_AVAIL		DL_NOTE_AGGR_AVAIL
#define	NOTE_AGGR_UNAVAIL	DL_NOTE_AGGR_UNAVAIL

#define	SXGE_FM_REPORT_FAULT(sxgep, impact, location, msg)\
		ddi_dev_report_fault(sxgep->dip, impact, location, msg)
#define	SXGE_FM_CHECK_DEV_HANDLE(sxgep)\
		ddi_check_acc_handle(sxgep->pio_hdl.regh)
#define	SXGE_FM_GET_DEVSTATE(sxgep)\
		ddi_get_devstate(sxgep->dip)
#define	SXGE_FM_SERVICE_RESTORED(sxgep)\
		ddi_fm_service_impact(sxgep->dip, DDI_SERVICE_RESTORED)
#define	SXGE_FM_REPORT_ERROR(sxgep, portn, chan, ereport_id)\
		sxge_fm_report_error(sxgep, portn, chan, ereport_id)
#define	SXGE_FM_CHECK_ACC_HANDLE(sxgep, handle)\
		sxge_fm_check_acc_handle(handle)
#define	SXGE_FM_CHECK_DMA_HANDLE(sxgep, handle)\
		sxge_fm_check_dma_handle(handle)


/*
 * Handy macros (taken from bge driver)
 */
#define	DMA_COMMON_VPTR(area)		((area.kaddrp))
#define	DMA_COMMON_HANDLE(area)		((area.dma_handle))
#define	DMA_COMMON_ACC_HANDLE(area)	((area.acc_handle))
#define	DMA_COMMON_IOADDR(area)		((area.dma_cookie.dmac_laddress))
#define	DMA_COMMON_SYNC(area, flag)	((void) ddi_dma_sync((area).dma_handle,\
						(area).offset, (area).alength, \
						(flag)))
#define	DMA_COMMON_SYNC_OFFSET(area, bufoffset, len, flag)	\
					((void) ddi_dma_sync((area).dma_handle,\
					(area.offset + bufoffset), len, \
					(flag)))

#define	NEXT_ENTRY(index, wrap)		((index + 1) & wrap)
#define	NEXT_ENTRY_PTR(ptr, first, last)	\
					((ptr == last) ? first : (ptr + 1))

#if 0
/*
 * Reconfiguring the network devices requires the net_config privilege
 * in Solaris 10+.  Prior to this, root privilege is required.  In order
 * that the driver binary can run on both S10+ and earlier versions, we
 * make the decisiion as to which to use at runtime.  These declarations
 * allow for either (or both) to exist ...
 */
extern int secpolicy_net_config(const cred_t *, boolean_t);
extern void sxge_fm_report_error(struct sxge_t *sxge,
	uint8_t err_chan, sxge_fm_ereport_id_t fm_ereport_id);
#pragma weak    secpolicy_net_config
#endif

extern int fm_check_acc_handle(ddi_acc_handle_t);
extern int fm_check_dma_handle(ddi_dma_handle_t);

int sxge_m_stat(void *, uint_t, uint64_t *);

#endif	/* !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SXGE_IMPL_H */
