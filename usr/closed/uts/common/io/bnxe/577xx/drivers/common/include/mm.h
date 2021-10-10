  /*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2005 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *
 *
 * History:
 *    05/02/05 Hav Khauv        Inception.
 ******************************************************************************/

#ifndef _MM_H
#define _MM_H

#include <stdarg.h>

#if defined(UEFI)

#include "sync.h"
#include "lm5710.h"
#ifndef UEFI64
typedef u32_t mm_int_ptr_t;
#else
typedef u64_t mm_int_ptr_t;
#endif
#define mm_read_barrier()
#define mm_write_barrier()
#define mm_barrier()           

#define MM_ACQUIRE_SPQ_LOCK(_pdev)      LOCK()
#define MM_RELEASE_SPQ_LOCK(_pdev)      UNLOCK()
#define MM_ACQUIRE_SPQ_LOCK_DPC(_pdev)  LOCK()
#define MM_RELEASE_SPQ_LOCK_DPC(_pdev)  UNLOCK()
#define MM_ACQUIRE_CID_LOCK(_pdev)      LOCK()
#define MM_RELEASE_CID_LOCK(_pdev)      UNLOCK()
#define MM_ACQUIRE_REQUEST_LOCK(_pdev)  LOCK()
#define MM_RELEASE_REQUEST_LOCK(_pdev)  UNLOCK()
#define MM_ACQUIRE_PHY_LOCK(_pdev)      LOCK()
#define MM_RELEASE_PHY_LOCK(_pdev)      UNLOCK()
#define MM_ACQUIRE_PHY_LOCK_DPC(_pdev)  LOCK()
#define MM_RELEASE_PHY_LOCK_DPC(_pdev)  UNLOCK()
#define MM_ACQUIRE_IND_REG_LOCK(_pdev)  LOCK()
#define MM_RELEASE_IND_REG_LOCK(_pdev)  UNLOCK()
#define MM_ACQUIRE_LOADER_LOCK()        LOCK()
#define MM_RELEASE_LOADER_LOCK()        UNLOCK()
#define MM_ACQUIRE_SP_REQ_MGR_LOCK(_pdev)  LOCK()
#define MM_RELEASE_SP_REQ_MGR_LOCK(_pdev)  UNLOCK()
#define MM_ACQUIRE_DMAE_STATS_LOCK(_pdev)  LOCK()
#define MM_RELEASE_DMAE_STATS_LOCK(_pdev)  UNLOCK()
#define MM_ACQUIRE_DMAE_MISC_LOCK(_pdev)  LOCK()
#define MM_RELEASE_DMAE_MISC_LOCK(_pdev)  UNLOCK()
#define MM_ACQUIRE_ISLES_CONTROL_LOCK(_pdev)     LOCK()
#define MM_RELEASE_ISLES_CONTROL_LOCK(_pdev)     UNLOCK()
#define MM_ACQUIRE_ISLES_CONTROL_LOCK_DPC(_pdev)     LOCK()
#define MM_RELEASE_ISLES_CONTROL_LOCK_DPC(_pdev)     UNLOCK()
#define MM_ACQUIRE_RAMROD_COMP_LOCK(_pdev) LOCK()
#define MM_RELEASE_RAMROD_COMP_LOCK(_pdev) UNLOCK()
#define MM_ACQUIRE_MCP_LOCK(_pdev) LOCK()
#define MM_RELEASE_MCP_LOCK(_pdev) UNLOCK()
#define MM_ACQUIRE_SB_LOCK(_pdev, _sb_idx) LOCK()
#define MM_RELEASE_SB_LOCK(_pdev, _sb_idx) UNLOCK()


static __inline void mm_atomic_set(u32_t *p, u32_t v)
{
    LOCK();
    *p = v;
    UNLOCK();
}
static __inline s32_t mm_atomic_dec(u32_t *p)
{
    s32_t ret;
    LOCK();
    ret = --(*p);
    UNLOCK();
    return ret;
}
static __inline s32_t mm_atomic_inc(u32_t *p)
{
    s32_t ret;
    LOCK();
    ret = ++(*p);
    UNLOCK();
    return ret;
}

static __inline s32_t mm_atomic_and(u32_t *p, u32_t v)
{
    s32_t ret;
    LOCK();
    ret = *p;
    *p &= v;
    UNLOCK();
    return ret;
}

void mm_bar_read_byte(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u8_t                * ret
	);
void mm_bar_read_word(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u16_t               * ret
	);
void mm_bar_read_dword(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u32_t               * ret
	);
void mm_bar_read_ddword(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u64_t               * ret
	);
void mm_bar_write_byte(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u8_t                  val
	);
void mm_bar_write_word(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u16_t                 val
	);
void mm_bar_write_dword(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u32_t                 val
	);
void mm_bar_write_ddword(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u64_t                 val
	);

#define MM_WRITE_DOORBELL(PDEV,BAR,CID,VAL) \
    LM_BAR_WR32_OFFSET(PDEV,BAR_1,(u32_t)((int_ptr_t)((u8_t *) (PDEV)->context_info->array[CID].cid_resc.mapped_cid_bar_addr - \
                                              (PDEV)->hw_info.mem_base[BAR_1].as_u64 +(DPM_TRIGER_TYPE))), VAL)

#define MM_REGISTER_LPME(_pdev,_func,_b_fw_access, _b_queue_for_fw)     (LM_STATUS_SUCCESS)

#define MM_EMPTY_RAMROD_RECEIVED(pdev,empty_data)      

#define mm_dbus_start_if_enable(_pdev)

#define mm_dbus_stop_if_started(_pdev)

static __inline s32_t mm_atomic_or(u32_t *p, u32_t v)
{
    s32_t ret;
    LOCK();
    ret = *p;
    *p |= v;
    UNLOCK();
    return ret;
}

#define mm_atomic_read(_p) (*_p)

static __inline s32_t mm_atomic_cmpxchg_imp(u32_t *p, u32_t old_v, u32_t new_v)
{
    s32_t ret;
    LOCK();
    ret = *p;
    if (*p == old_v)
    {
        *p = new_v;
    }
    UNLOCK();
    return ret;
}
#define mm_atomic_cmpxchg(p, old_v, new_v) mm_atomic_cmpxchg_imp((u32_t *)p, old_v, new_v)
/* Not expected to be called under this OSs */
#define mm_er_initiate_recovery(_pdev) (LM_STATUS_FAILURE)
#define MM_REGISTER_DPC(_pdev,_func)  (LM_STATUS_FAILURE)

#elif defined(DOS)
#include "sync.h"

// portable integer type of the pointer size for current platform (64/32)
typedef u32_t mm_int_ptr_t;

#define mm_read_barrier()
#define mm_write_barrier()
#define mm_barrier()           

#define MM_ACQUIRE_SPQ_LOCK(_pdev)      LOCK()
#define MM_RELEASE_SPQ_LOCK(_pdev)      UNLOCK()
#define MM_ACQUIRE_SPQ_LOCK_DPC(_pdev)  LOCK()
#define MM_RELEASE_SPQ_LOCK_DPC(_pdev)  UNLOCK()

#define MM_ACQUIRE_CID_LOCK(_pdev)      LOCK()
#define MM_RELEASE_CID_LOCK(_pdev)      UNLOCK()

// RAMROD
#define MM_ACQUIRE_REQUEST_LOCK(_pdev)  LOCK()
#define MM_RELEASE_REQUEST_LOCK(_pdev)  UNLOCK()

//phy lock
#define MM_ACQUIRE_PHY_LOCK(_pdev)      LOCK()
#define MM_RELEASE_PHY_LOCK(_pdev)      UNLOCK()
#define MM_ACQUIRE_PHY_LOCK_DPC(_pdev)  LOCK()
#define MM_RELEASE_PHY_LOCK_DPC(_pdev)  UNLOCK()

#define MM_ACQUIRE_IND_REG_LOCK(_pdev)  LOCK()
#define MM_RELEASE_IND_REG_LOCK(_pdev)  UNLOCK()

// lock used by lm_loader
#define MM_ACQUIRE_LOADER_LOCK()        LOCK()
#define MM_RELEASE_LOADER_LOCK()        UNLOCK()

/* Slow path request manager lock */
#define MM_ACQUIRE_SP_REQ_MGR_LOCK(_pdev)  LOCK()
#define MM_RELEASE_SP_REQ_MGR_LOCK(_pdev)  UNLOCK()

/*DMAE locks*/
#define MM_ACQUIRE_DMAE_STATS_LOCK(_pdev)  LOCK()
#define MM_RELEASE_DMAE_STATS_LOCK(_pdev)  UNLOCK()
#define MM_ACQUIRE_DMAE_MISC_LOCK(_pdev)  LOCK()
#define MM_RELEASE_DMAE_MISC_LOCK(_pdev)  UNLOCK()

#define MM_ACQUIRE_ISLES_CONTROL_LOCK(_pdev)     LOCK()
#define MM_RELEASE_ISLES_CONTROL_LOCK(_pdev)     UNLOCK()
#define MM_ACQUIRE_ISLES_CONTROL_LOCK_DPC(_pdev)     LOCK()
#define MM_RELEASE_ISLES_CONTROL_LOCK_DPC(_pdev)     UNLOCK()

/* ramrod completions */
#define MM_ACQUIRE_RAMROD_COMP_LOCK(_pdev)  LOCK()
#define MM_RELEASE_RAMROD_COMP_LOCK(_pdev)  UNLOCK()

/* MCP mailbox access */
#define MM_ACQUIRE_MCP_LOCK(_pdev) LOCK()
#define MM_RELEASE_MCP_LOCK(_pdev) UNLOCK()

#define MM_ACQUIRE_SB_LOCK(_pdev, _sb_idx)  LOCK()
#define MM_RELEASE_SB_LOCK(_pdev, _sb_idx)  UNLOCK()

#ifdef VF_INVOLVED
#define MM_ACQUIRE_PF_LOCK(pdev)            LOCK()
#define MM_RELEASE_PF_LOCK(pdev)            UNLOCK()
#endif

#define MM_WRITE_DOORBELL(PDEV,BAR,CID,VAL) \
    LM_BAR_WR32_ADDRESS(PDEV,((u8_t *) (PFDEV(PDEV))->context_info->array[VF_TO_PF_CID(PDEV,CID)].cid_resc.mapped_cid_bar_addr+(DPM_TRIGER_TYPE)), VAL)

#define MM_REGISTER_LPME(_pdev,_func,_b_fw_access, _b_queue_for_fw)       (LM_STATUS_SUCCESS)

/* Not expected to be called under this OSs */
#define mm_er_initiate_recovery(_pdev) (LM_STATUS_FAILURE)
#define MM_REGISTER_DPC(_pdev,_func)  (LM_STATUS_FAILURE)


#define MM_EMPTY_RAMROD_RECEIVED(pdev,empty_data)        

#define mm_dbus_start_if_enable(_pdev)

#define mm_dbus_stop_if_started(_pdev)

static __inline void mm_atomic_set_imp(u32_t *p, u32_t v)
{
    LOCK();
    *p = v;
    UNLOCK();
}
#define mm_atomic_set(_p, _v) mm_atomic_set_imp((u32_t *)_p, _v)

/* returns the decremented value */
static __inline u32_t mm_atomic_dec(u32_t *p)
{
    u32_t ret;
    LOCK();
    ret = --(*p);
    UNLOCK();
    return ret;
}

/* returns the decremented value */
static __inline s32_t mm_atomic_inc(u32_t *p)
{
    s32_t ret;
    LOCK();
    ret = ++(*p);
    UNLOCK();
    return ret;
}

static __inline s32_t mm_atomic_and(u32_t *p, u32_t v)
{
    s32_t ret;
    LOCK();
    ret = *p;
    *p &= v;
    UNLOCK();
    return ret;
}

static __inline s32_t mm_atomic_or(u32_t *p, u32_t v)
{
    s32_t ret;
    LOCK();
    ret = *p;
    *p |= v;
    UNLOCK();
    return ret;
}

#define mm_atomic_read(_p) (*_p)

static __inline s32_t mm_atomic_cmpxchg_imp(u32_t *p, u32_t old_v, u32_t new_v)
{
    s32_t ret;
    LOCK();
    ret = *p;
    if (*p == old_v)
    {
        *p = new_v;
    }
    UNLOCK();
    return ret;
}
#define mm_atomic_cmpxchg(p, old_v, new_v) mm_atomic_cmpxchg_imp((u32_t *)p, old_v, new_v)


#elif defined(__LINUX)

#include "ediag_compat.h"

// portable integer type of the pointer size for current platform (64/32)
typedef unsigned long mm_int_ptr_t;


#define mm_read_barrier()  do {barrier(); ediag_rmb();} while(0)
#define mm_write_barrier() do {barrier(); ediag_wmb();} while(0)
#define mm_barrier()       do {barrier(); ediag_rmb(); ediag_wmb();} while(0)    


void MM_ACQUIRE_SPQ_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_SPQ_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_SPQ_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_SPQ_LOCK_DPC(struct _lm_device_t *_pdev);

void MM_ACQUIRE_CID_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_CID_LOCK(struct _lm_device_t *_pdev);

// RAMROD
void MM_ACQUIRE_REQUEST_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_REQUEST_LOCK(struct _lm_device_t *_pdev);

void MM_ACQUIRE_REQUEST_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_REQUEST_LOCK_DPC(struct _lm_device_t *_pdev);

// phy lock
void MM_ACQUIRE_PHY_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_PHY_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_PHY_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_PHY_LOCK_DPC(struct _lm_device_t *_pdev);

void MM_ACQUIRE_DMAE_STATS_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_DMAE_STATS_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_DMAE_MISC_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_DMAE_MISC_LOCK(struct _lm_device_t *_pdev);

void MM_ACQUIRE_ISLES_CONTROL_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_ISLES_CONTROL_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_ISLES_CONTROL_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_ISLES_CONTROL_LOCK_DPC(struct _lm_device_t *_pdev);

/* ramrod completions */
void MM_ACQUIRE_RAMROD_COMP_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_RAMROD_COMP_LOCK(struct _lm_device_t *_pdev);

/* Empty for Linux */
#define MM_ACQUIRE_IND_REG_LOCK(_pdev)
#define MM_RELEASE_IND_REG_LOCK(_pdev)

// lock used by lm_loader
void MM_ACQUIRE_LOADER_LOCK(void);
void MM_RELEASE_LOADER_LOCK(void);

/* Slow path request manager lock */
void MM_ACQUIRE_SP_REQ_MGR_LOCK(struct _lm_device_t *pdev);
void MM_RELEASE_SP_REQ_MGR_LOCK(struct _lm_device_t *pdev);

/* MCP mailbox access lock */
void MM_ACQUIRE_MCP_LOCK(struct _lm_device_t *pdev);
void MM_RELEASE_MCP_LOCK(struct _lm_device_t *pdev);

void MM_ACQUIRE_SB_LOCK(struct _lm_device_t *_pdev, u8_t _sb_idx);
void MM_RELEASE_SB_LOCK(struct _lm_device_t *_pdev, u8_t _sb_idx);

#ifdef VF_INVOLVED
void MM_ACQUIRE_PF_LOCK(struct _lm_device_t *pdev);
void MM_RELEASE_PF_LOCK(struct _lm_device_t *pdev);
#endif

#define MM_WRITE_DOORBELL(PDEV,BAR,CID,VAL) \
    LM_BAR_WR32_ADDRESS(PDEV,((u8_t *) PFDEV(PDEV)->context_info->array[VF_TO_PF_CID(PDEV,CID)].cid_resc.mapped_cid_bar_addr+(DPM_TRIGER_TYPE)), VAL)

#define MM_REGISTER_LPME(_pdev,_func,_b_fw_access, _b_queue_for_fw)       (LM_STATUS_SUCCESS)      

/* Not expected to be called under this OSs */
#define mm_er_initiate_recovery(_pdev) (LM_STATUS_FAILURE)
#define MM_REGISTER_DPC(_pdev,_func)  (LM_STATUS_FAILURE)

#define MM_EMPTY_RAMROD_RECEIVED(pdev,empty_data)        

#define mm_dbus_start_if_enable(_pdev)

#define mm_dbus_stop_if_started(_pdev)

// read bar offset
lm_status_t mm_get_bar_offset(
	struct _lm_device_t *pdev,
        u8_t           barn,
	lm_address_t * bar_addr
	);

lm_status_t mm_get_bar_size(
	struct _lm_device_t  * pdev,
    u8_t                   bar_num,
	u32_t                * bar_sz
	);

static inline void mm_check_bar_offset(
	struct _lm_device_t * pdev,
	u8_t                  bar,
	u32_t                 offset,
	const char *          func_name
	)
{
#if 0
	/* This will not work with the L4 dynamic bar1 mapping */
	if (bar == BAR_1) {
		if (offset >= LM_DQ_CID_SIZE * MAX_ETH_CONS) {
			DbgMessage(pdev, FATAL, "%s: bar[%d]: offset(0x%x) is outside the range (0x%x)\n",
				   func_name, bar, offset, LM_DQ_CID_SIZE * MAX_ETH_CONS);
			DbgBreak();
		}
	}
#endif
	if (bar == 0) {
		if (offset >= pdev->hw_info.bar_size[0]) {
			DbgMessage(pdev, FATAL, "%s: bar[%d]: offset(0x%x) is outside the range (0x%x)\n",
				   func_name, bar, offset, pdev->hw_info.bar_size[0]);
			DbgBreak();
		}
	}

	barrier();
}


static inline void mm_bar_read_byte(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u8_t                * ret
	)
{
	mm_check_bar_offset(_pdev, bar, offset, __FUNCTION__);
	mm_read_barrier();
	*ret = ediag_readb(((u8_t*)(_pdev->vars.mapped_bar_addr[bar])) + offset);
}

static inline void mm_bar_read_word(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u16_t               * ret
	)
{
	mm_check_bar_offset(_pdev, bar, offset, __FUNCTION__);
	mm_read_barrier();
	*ret = ediag_readw(((u8_t*)(_pdev->vars.mapped_bar_addr[bar])) + offset);
}

static void mm_bar_read_dword(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u32_t               * ret
	)
{
	mm_check_bar_offset(_pdev, bar, offset, __FUNCTION__);
	mm_read_barrier();
	*ret = ediag_readl(((u8_t*)(_pdev->vars.mapped_bar_addr[bar])) + offset);
}

static inline void mm_bar_read_ddword(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u64_t               * ret
	)
{
	mm_check_bar_offset(_pdev, bar, offset, __FUNCTION__);
	mm_read_barrier();
	*ret = ediag_readq(((u8_t*)(_pdev->vars.mapped_bar_addr[bar])) + offset);
}

static inline void mm_bar_write_byte(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u8_t                  val
	)
{
	mm_check_bar_offset(_pdev, bar, offset, __FUNCTION__);
	ediag_writeb(val, ((u8_t*)(_pdev->vars.mapped_bar_addr[bar])) + offset);
	ediag_mmiowb();
}

static inline void mm_bar_write_word(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u16_t                 val
	)
{
	mm_check_bar_offset(_pdev, bar, offset, __FUNCTION__);
	ediag_writew(val, ((u8_t*)(_pdev->vars.mapped_bar_addr[bar])) + offset);
	ediag_mmiowb();
}

static inline void mm_bar_write_dword(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u32_t                 val
	)
{
	mm_check_bar_offset(_pdev, bar, offset, __FUNCTION__);
	ediag_writel(val, ((u8_t*)(_pdev->vars.mapped_bar_addr[bar])) + offset);
	ediag_mmiowb();
}

static inline void mm_io_write_dword(
	struct _lm_device_t * _pdev,
	void                * addr,
	u32_t                 val
	)
{
	mm_check_bar_offset(_pdev, 1, (int_ptr_t)((u8_t*)addr - (u8_t*)(_pdev->vars.mapped_bar_addr[1])), __FUNCTION__);
	ediag_writel(val, addr);
	ediag_mmiowb();
}

static inline void mm_bar_write_ddword(
	struct _lm_device_t * _pdev,
	u8_t                  bar,
	u32_t                 offset,
	u64_t                 val
	)
{
	mm_check_bar_offset(_pdev, bar, offset, __FUNCTION__);
	ediag_writeq(val, ((u8_t*)(_pdev->vars.mapped_bar_addr[bar])) + offset);
	ediag_mmiowb();
}


static inline u16_t mm_le16_to_cpu(u16_t val)
{
	return ediag_le16_to_cpu(val);
}

static inline u32_t mm_le32_to_cpu(u32_t val)
{
	return ediag_le32_to_cpu(val);
}

static inline u32_t mm_be32_to_cpu(u32_t val)
{
	return ediag_be32_to_cpu(val);
}

static inline u32_t mm_be16_to_cpu(u32_t val)
{
    return ediag_be16_to_cpu(val);
}

static inline u32_t mm_cpu_to_be32(u32_t val)
{
    return ediag_cpu_to_be32(val);
}

static inline u32_t mm_cpu_to_be16(u32_t val)
{
    return ediag_cpu_to_be16(val);
}

static inline u16_t mm_cpu_to_le16(u16_t val)
{
	return ediag_cpu_to_le16(val);
}

static inline u32_t mm_cpu_to_le32(u32_t val)
{
	return ediag_cpu_to_le32(val);
}

static inline void mm_atomic_set(u32_t *p, u32_t v)
{
    ediag_atomic_set((s32_t *)p, (s32_t)v);
}

/* returns the decremented value */
static inline s32_t mm_atomic_dec(u32_t *p)
{
    return ediag_atomic_dec_and_test((s32_t *)p);
}

/* returns the decremented value */
static inline s32_t mm_atomic_inc(u32_t *p)
{
    return ediag_atomic_inc_and_test((s32_t *)p);
}

static inline s32_t mm_atomic_read_imp(u32_t *p)
{
    return ediag_atomic_read((s32_t *)p);
}
#define mm_atomic_read(p) mm_atomic_read_imp((u32_t *)p)


static inline s32_t mm_atomic_cmpxchg_imp(u32_t *p, u32_t cmp, u32_t new_v)
{
    return ediag_atomic_cmpxchg((s32_t *)p, (int)cmp, (int)new_v);
}
#define mm_atomic_cmpxchg(p, cmp, new_v) mm_atomic_cmpxchg_imp((u32_t *)p, cmp, new_v)

#elif defined(__SunOS)

#include <sys/atomic.h>

// portable integer type of the pointer size for current platform (64/32)
typedef unsigned long mm_int_ptr_t;

#define mm_read_barrier()  membar_consumer()
#define mm_write_barrier() membar_producer()
#define mm_barrier() do {membar_consumer(); membar_producer();} while(0)

void MM_ACQUIRE_SPQ_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_SPQ_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_SPQ_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_SPQ_LOCK_DPC(struct _lm_device_t *_pdev);

void MM_ACQUIRE_CID_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_CID_LOCK(struct _lm_device_t *_pdev);

// RAMROD
void MM_ACQUIRE_REQUEST_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_REQUEST_LOCK(struct _lm_device_t *_pdev);

// phy lock
void MM_ACQUIRE_PHY_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_PHY_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_PHY_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_PHY_LOCK_DPC(struct _lm_device_t *_pdev);

void MM_ACQUIRE_DMAE_STATS_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_DMAE_STATS_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_DMAE_MISC_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_DMAE_MISC_LOCK(struct _lm_device_t *_pdev);

void MM_ACQUIRE_ISLES_CONTROL_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_ISLES_CONTROL_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_ISLES_CONTROL_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_ISLES_CONTROL_LOCK_DPC(struct _lm_device_t *_pdev);

/* ramrod completions */
void MM_ACQUIRE_RAMROD_COMP_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_RAMROD_COMP_LOCK(struct _lm_device_t *_pdev);

void MM_ACQUIRE_IND_REG_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_IND_REG_LOCK(struct _lm_device_t *_pdev);

// lock used by lm_loader
void MM_ACQUIRE_LOADER_LOCK();
void MM_RELEASE_LOADER_LOCK();

/* Slow path request manager lock */
void MM_ACQUIRE_SP_REQ_MGR_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_SP_REQ_MGR_LOCK(struct _lm_device_t *_pdev);

void MM_ACQUIRE_SB_LOCK(struct _lm_device_t *_pdev, u8_t _sb_idx);
void MM_RELEASE_SB_LOCK(struct _lm_device_t *_pdev, u8_t _sb_idx);

void MM_ACQUIRE_MCP_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_MCP_LOCK(struct _lm_device_t *_pdev);

#if defined(__SunOS_MDB)

/* Solaris debugger (MDB) doesn't have access to ddi_get/put routines */

#define MM_WRITE_DOORBELL(PDEV,BAR,CID,VAL); \
    LM_BAR_WR32_ADDRESS(PDEV,((u8_t *) PFDEV(PDEV)->context_info->array[VF_TO_PF_CID(PDEV,CID)].cid_resc.mapped_cid_bar_addr+(DPM_TRIGER_TYPE)), VAL);

#else /* __SunOS && !__SunOS_MDB */

#define MM_WRITE_DOORBELL(PDEV,BAR,CID,VAL) \
    ddi_put32(PFDEV(PDEV)->context_info->array[VF_TO_PF_CID(PDEV,CID)].cid_resc.reg_handle, \
              (uint32_t *)((caddr_t)PFDEV(PDEV)->context_info->array[VF_TO_PF_CID(PDEV,CID)].cid_resc.mapped_cid_bar_addr+(DPM_TRIGER_TYPE)), \
              VAL)

#endif /* __SunOS_MDB */

typedef void lm_generic_workitem_function(struct _lm_device_t *pdev);
lm_status_t 
mm_register_lpme(IN struct _lm_device_t *_pdev, 
                 IN lm_generic_workitem_function    *func,
                 IN const u8_t                      b_fw_access,
                 IN const u8_t                      b_queue_for_fw);
				 
#define MM_REGISTER_LPME(_pdev,_func,_b_fw_access, _b_queue_for_fw) mm_register_lpme(_pdev,_func,_b_fw_access ,_b_queue_for_fw)

#define MM_EMPTY_RAMROD_RECEIVED(pdev,empty_data)        

/* Not expected to be called under this OSs */
#define mm_er_initiate_recovery(_pdev) (LM_STATUS_FAILURE)
#define MM_REGISTER_DPC(_pdev,_func)  (LM_STATUS_FAILURE)

#define mm_dbus_start_if_enable(_pdev)

#define mm_dbus_stop_if_started(_pdev)

#define mm_atomic_set(_p, _v) atomic_swap_32((volatile uint32_t *)(_p), (uint32_t)(_v))

/* returns the decremented value */
#define mm_atomic_dec(_p) atomic_dec_32_nv((volatile uint32_t *)(_p))

/* returns the incremented value */
#define mm_atomic_inc(_p) atomic_inc_32_nv((volatile uint32_t *)(_p))

#define mm_atomic_and(_p, _v) atomic_and_32((volatile uint32_t *)(_p), (uint32_t)(_v))
#define mm_atomic_or(_p, _v) atomic_or_32((volatile uint32_t *)(_p), (uint32_t)(_v))

#define mm_atomic_cmpxchg(_p, _old_val, _new_val) atomic_cas_32((volatile uint32_t *)(_p), (uint32_t)_new_val, (uint32_t)_old_val)

#define mm_atomic_read(_p) atomic_add_32_nv((volatile uint32_t *)(_p), (int32_t)0)

#elif defined(__USER_MODE_DEBUG) 

#include <minmax.h>

// portable integer type of the pointer size for current platform (64/32)
typedef u64_t mm_int_ptr_t;

#define mm_read_barrier()
#define mm_write_barrier()
#define mm_barrier()           

#define MM_ACQUIRE_SPQ_LOCK(_pdev)      DbgMessage(pdev, VERBOSEi, "Acquiring global SPQ lock\n");
#define MM_RELEASE_SPQ_LOCK(_pdev)      DbgMessage(pdev, VERBOSEi, "Releasing global SPQ lock\n");
#define MM_ACQUIRE_SPQ_LOCK_DPC(_pdev)  DbgMessage(pdev, VERBOSEi, "Acquiring global SPQ lock\n");
#define MM_RELEASE_SPQ_LOCK_DPC(_pdev)  DbgMessage(pdev, VERBOSEi, "Releasing global SPQ lock\n");

#define MM_ACQUIRE_CID_LOCK(_pdev)      DbgMessage(pdev, VERBOSEi, "Acquiring global CID lock\n");
#define MM_RELEASE_CID_LOCK(_pdev)      DbgMessage(pdev, VERBOSEi, "Releasing global CID lock\n");
// RAMROD
#define MM_ACQUIRE_REQUEST_LOCK(_pdev)  DbgMessage(pdev, VERBOSEi, "Acquiring ramrod lock\n");
#define MM_RELEASE_REQUEST_LOCK(_pdev)  DbgMessage(pdev, VERBOSEi, "Releasing ramrod lock\n");

// phy lock
#define MM_ACQUIRE_PHY_LOCK(_pdev)      DbgMessage(pdev, VERBOSEi, "Acquiring phy lock\n");
#define MM_RELEASE_PHY_LOCK(_pdev)      DbgMessage(pdev, VERBOSEi, "Releasing phy lock\n");
#define MM_ACQUIRE_PHY_LOCK_DPC(_pdev)  DbgMessage(pdev, VERBOSEi, "Acquiring phy lock\n");
#define MM_RELEASE_PHY_LOCK_DPC(_pdev)  DbgMessage(pdev, VERBOSEi, "Releasing phy lock\n");

/* DMAE */
#define MM_ACQUIRE_DMAE_STATS_LOCK(_pdev)    DbgMessage(pdev, VERBOSEi, "Acquiring DMAE Port lock\n");
#define MM_RELEASE_DMAE_STATS_LOCK(_pdev)    DbgMessage(pdev, VERBOSEi, "Releasing DMAE Port lock\n");
#define MM_ACQUIRE_DMAE_MISC_LOCK(_pdev)      DbgMessage(pdev, VERBOSEi, "Acquiring DMAE WB lock\n");
#define MM_RELEASE_DMAE_MISC_LOCK(_pdev)      DbgMessage(pdev, VERBOSEi, "Releasing DMAE WB lock\n");

#define MM_ACQUIRE_ISLES_CONTROL_LOCK(_pdev)        DbgMessage(pdev, VERBOSEi, "Acquiring isles control lock\n");
#define MM_RELEASE_ISLES_CONTROL_LOCK(_pdev)        DbgMessage(pdev, VERBOSEi, "Releasing isles control lock\n");
#define MM_ACQUIRE_ISLES_CONTROL_LOCK_DPC(_pdev)    DbgMessage(pdev, VERBOSEi, "Acquiring isles control lock\n");
#define MM_RELEASE_ISLES_CONTROL_LOCK_DPC(_pdev)    DbgMessage(pdev, VERBOSEi, "Releasing isles control lock\n");

/* ramrod completions */
#define MM_ACQUIRE_RAMROD_COMP_LOCK(_pdev)  DbgMessage(pdev, VERBOSEi, "Acquiring ramrod completions lock\n");
#define MM_RELEASE_RAMROD_COMP_LOCK(_pdev)  DbgMessage(pdev, VERBOSEi, "Releasing ramrod completions lock\n");

#define MM_ACQUIRE_IND_REG_LOCK(_pdev)  DbgMessage(pdev, VERBOSEi, "Acquiring ind_reg lock\n");
#define MM_RELEASE_IND_REG_LOCK(_pdev)  DbgMessage(pdev, VERBOSEi, "Releasing ind_reg lock\n");

#define MM_ACQUIRE_LOADER_LOCK()        DbgMessage(pdev, VERBOSEi, "Acquiring loader lock\n");
#define MM_RELEASE_LOADER_LOCK()        DbgMessage(pdev, VERBOSEi, "Releasing loader lock\n");
/* Slow path request manager lock */
#define MM_ACQUIRE_SP_REQ_MGR_LOCK(_pdev)      DbgMessage(pdev, VERBOSEi, "Acquiring sp_req_mgr lock\n");
#define MM_RELEASE_SP_REQ_MGR_LOCK(_pdev)      DbgMessage(pdev, VERBOSEi, "Releasing sp_req_mgr lock\n");

#define MM_ACQUIRE_SB_LOCK(_pdev, _sb_idx)      DbgMessage(pdev, VERBOSEi, "Acquiring sb lock\n");
#define MM_RELEASE_SB_LOCK(_pdev, _sb_idx)      DbgMessage(pdev, VERBOSEi, "Releasing sb lock\n");

#define MM_WRITE_DOORBELL(PDEV,BAR,CID,VAL);\
    LM_BAR_WR32_ADDRESS(PDEV,((u8_t *) PFDEV(PDEV)->context_info->array[VF_TO_PF_CID(PDEV,CID)].cid_resc.mapped_cid_bar_addr+(DPM_TRIGER_TYPE)), VAL);\


#define MM_REGISTER_LPME(_pdev,_func,_b_fw_access, _b_queue_for_fw)       (LM_STATUS_SUCCESS) 

#define MM_EMPTY_RAMROD_RECEIVED(pdev,lm_cli_idx)

/* Not expected to be called under this OSs */
#define mm_er_initiate_recovery(_pdev) (LM_STATUS_FAILURE)
#define MM_REGISTER_DPC(_pdev,_func)  (LM_STATUS_FAILURE)

#define mm_dbus_start_if_enable(_pdev)

#define mm_dbus_stop_if_started(_pdev)

static __inline void mm_atomic_set(u32_t *p, u32_t v)
{
    *p = v;
}

/* returns the decremented value */
static __inline s32_t mm_atomic_dec(u32_t *p)
{
    return --(*p);
}

/* returns the decremented value */
static __inline s32_t mm_atomic_inc(u32_t *p)
{
    return ++(*p);
}

#elif defined(_VBD_) || defined(_VBD_CMD_)

#if defined(_VBD_)
#include <ntddk.h>
#elif defined(_VBD_CMD_)
#include "vc_os_emul.h"
#endif

#if defined(_IA64_) || defined(_VBD_CMD_)
#define mm_read_barrier()      KeMemoryBarrier()
#else
#define mm_read_barrier()      KeMemoryBarrierWithoutFence()
#endif

#define mm_write_barrier()     KeMemoryBarrier()
#define mm_barrier()           KeMemoryBarrier()

// portable integer type of the pointer size for current platform (64/32)
typedef ULONG_PTR mm_int_ptr_t;

void MM_ACQUIRE_SPQ_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_SPQ_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_SPQ_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_SPQ_LOCK_DPC(struct _lm_device_t *_pdev);

void MM_ACQUIRE_CID_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_CID_LOCK(struct _lm_device_t *_pdev);

// RAMROD
void MM_ACQUIRE_REQUEST_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_REQUEST_LOCK(struct _lm_device_t *_pdev);

// phy lock
void MM_ACQUIRE_PHY_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_PHY_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_PHY_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_PHY_LOCK_DPC(struct _lm_device_t *_pdev);

// DMAE
void MM_ACQUIRE_DMAE_STATS_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_DMAE_STATS_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_DMAE_MISC_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_DMAE_MISC_LOCK(struct _lm_device_t *_pdev);

//MCP lock
void MM_ACQUIRE_MCP_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_MCP_LOCK(struct _lm_device_t *_pdev);

void MM_ACQUIRE_ISLES_CONTROL_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_ISLES_CONTROL_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_ISLES_CONTROL_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_ISLES_CONTROL_LOCK_DPC(struct _lm_device_t *_pdev);

/* ramrod completions */
#define MM_ACQUIRE_RAMROD_COMP_LOCK(_pdev)
#define MM_RELEASE_RAMROD_COMP_LOCK(_pdev)

void MM_ACQUIRE_IND_REG_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_IND_REG_LOCK(struct _lm_device_t *_pdev);

void MM_ACQUIRE_LOADER_LOCK();
void MM_RELEASE_LOADER_LOCK();
/* Slow path request manager lock */
void MM_ACQUIRE_SP_REQ_MGR_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_SP_REQ_MGR_LOCK(struct _lm_device_t *_pdev);

void MM_ACQUIRE_SB_LOCK(struct _lm_device_t *_pdev, u8_t _sb_idx);
void MM_RELEASE_SB_LOCK(struct _lm_device_t *_pdev, u8_t _sb_idx);

#define MM_WRITE_DOORBELL(PDEV,BAR,CID,VAL);\
    LM_BAR_WR32_ADDRESS(PDEV,((u8_t *) PFDEV(PDEV)->context_info->array[VF_TO_PF_CID(PDEV,CID)].cid_resc.mapped_cid_bar_addr+(DPM_TRIGER_TYPE)), VAL);\

typedef void lm_generic_workitem_function(struct _lm_device_t *pdev);
lm_status_t 
mm_register_lpme(IN struct _lm_device_t *_pdev, 
                 IN lm_generic_workitem_function    *func,
                 IN const u8_t                      b_fw_access,
                 IN const u8_t                      b_queue_for_fw);

#define MM_REGISTER_LPME(_pdev,_func,_b_fw_access, _b_queue_for_fw)       mm_register_lpme(_pdev,_func,_b_fw_access, _b_queue_for_fw)

lm_status_t mm_er_initiate_recovery(struct _lm_device_t * pdev);

typedef void lm_generic_dpc_func(struct _lm_device_t *pdev);
lm_status_t mm_register_dpc(struct _lm_device_t *_pdev, lm_generic_dpc_func *func);
#define MM_REGISTER_DPC(_pdev,_func)  mm_register_dpc(_pdev, _func)


void 
mm_empty_ramrod_received_imp(struct _lm_device_t    *_pdev, 
                             const u32_t            empty_data);
#define MM_EMPTY_RAMROD_RECEIVED(pdev,lm_cli_idx)   mm_empty_ramrod_received_imp(pdev,empty_data)      

void 
mm_dbus_start_if_enable(struct _lm_device_t    *_pdev);

void 
mm_dbus_stop_if_started(struct _lm_device_t    *_pdev);


/* Atomic Operations */

#define mm_atomic_set(_p, _v)   InterlockedExchange((long*)(_p), (long)(_v))

/* returns the decremented value */
#define mm_atomic_dec(_p)       InterlockedDecrement((long*)(_p))
#define mm_atomic_inc(_p)       InterlockedIncrement((long*)(_p))

#define mm_atomic_add(_p, _v)   InterlockedExchangeAdd((long*)(_p), (long)(_v))
#define mm_atomic_sub(_p, _v)   InterlockedExchangeAdd((long*)(_p), -1*(long)(_v))

#define mm_atomic_and(_p, _v)   InterlockedAnd((long*)(_p), (long)(_v))
#define mm_atomic_or(_p, _v)    InterlockedOr((long*)(_p), (long)(_v) )

#define mm_atomic_read(_p)      InterlockedExchangeAdd((long*)(_p), (long)(0))
#define mm_atomic_cmpxchg(_p, _old_val, _new_val) InterlockedCompareExchange(_p, (long)_new_val, (long)_old_val )


/**** End Atomic Operations ******/


void * mm_alloc_mem_imp( struct _lm_device_t *    pdev,
                         u32_t                    mem_size,
                         IN const char*           sz_file,
                         IN const unsigned long   line,
                         IN u8_t                  cli_idx ) ;

#define mm_alloc_mem(_pdev,_mem_size, cli_idx) mm_alloc_mem_imp( _pdev, _mem_size, __FILE_STRIPPED__, __LINE__, cli_idx ) ;

void * mm_alloc_phys_mem_imp( struct _lm_device_t*     pdev,
                              u32_t                    mem_size,
                              lm_address_t *           phys_mem,
                              u8_t                     mem_type,
                              IN const char*           sz_file,
                              IN const unsigned long   line,
                              IN u8_t                  cli_idx ) ;

#define mm_alloc_phys_mem( _pdev, _mem_size, _phys_mem, _mem_type, cli_idx ) \
    mm_alloc_phys_mem_imp(_pdev, _mem_size, _phys_mem, _mem_type, __FILE_STRIPPED__, __LINE__, cli_idx ) ;

void * mm_alloc_phys_mem_align_imp( struct _lm_device_t*     pdev,
                                    u32_t                    mem_size,
                                    lm_address_t*            phys_mem,
                                    u32_t                    alignment,
                                    u8_t                     mem_type,
                                    IN const char*           sz_file,
                                    IN const unsigned long   line,
                                    IN u8_t                  cli_idx ) ;
#pragma alloc_text(PAGE, mm_alloc_phys_mem_align_imp)

#define mm_alloc_phys_mem_align( _pdev, _mem_size, _phys_mem, _alignment, _mem_type, cli_idx ) \
    mm_alloc_phys_mem_align_imp( _pdev, _mem_size, _phys_mem, _alignment, _mem_type, __FILE_STRIPPED__, __LINE__, cli_idx ) ;

void * mm_rt_alloc_mem_imp( struct _lm_device_t*    pdev,
                            u32_t                  mem_size,
                            IN const char*         sz_file,
                            IN const unsigned long line,
                            IN u8_t                cli_idx ) ;

#define mm_rt_alloc_mem( _pdev, _mem_size, cli_idx ) \
        mm_rt_alloc_mem_imp( _pdev, _mem_size, __FILE_STRIPPED__, __LINE__, cli_idx ) ;

void * mm_rt_alloc_phys_mem_imp( struct _lm_device_t*    pdev,
                                 u32_t                  mem_size,
                                 lm_address_t*          phys_mem,
                                 u8_t                   mem_type,
                                 IN const char*         sz_file,
                                 IN const unsigned long line,
                                 IN u8_t                cli_idx ) ;
#pragma alloc_text(PAGE, mm_rt_alloc_phys_mem_imp)

#define mm_rt_alloc_phys_mem( _pdev, _mem_size, _phys_mem, _flush_type, cli_idx ) \
        mm_rt_alloc_phys_mem_imp( _pdev, _mem_size, _phys_mem, _flush_type, __FILE_STRIPPED__, __LINE__, cli_idx ) ;


// read bar offset
lm_status_t mm_get_bar_offset( struct _lm_device_t *pdev,
                               u8_t           barn,
                               lm_address_t * bar_addr);

// read bar size
lm_status_t mm_get_bar_size( struct _lm_device_t  * pdev,
                             u8_t           bar_num,
                             u32_t        * bar_sz);

// return number of processors
u8_t mm_get_cpu_count();


#elif defined (NDISMONO) // VBD
#include <ntddk.h>
#include <ndis.h>

// portable integer type of the pointer size for current platform (64/32)
typedef ULONG_PTR mm_int_ptr_t;

void MM_ACQUIRE_SPQ_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_SPQ_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_SPQ_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_SPQ_LOCK_DPC(struct _lm_device_t *_pdev);

void MM_ACQUIRE_CID_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_CID_LOCK(struct _lm_device_t *_pdev);

// RAMROD
void MM_ACQUIRE_REQUEST_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_REQUEST_LOCK(struct _lm_device_t *_pdev);

// phy lock
void MM_ACQUIRE_PHY_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_PHY_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_PHY_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_PHY_LOCK_DPC(struct _lm_device_t *_pdev);

//DMAE
void MM_ACQUIRE_DMAE_MISC_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_DMAE_MISC_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_DMAE_STATS_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_DMAE_STATS_LOCK(struct _lm_device_t *_pdev);

void MM_ACQUIRE_ISLES_CONTROL_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_ISLES_CONTROL_LOCK(struct _lm_device_t *_pdev);
void MM_ACQUIRE_ISLES_CONTROL_LOCK_DPC(struct _lm_device_t *_pdev);
void MM_RELEASE_ISLES_CONTROL_LOCK_DPC(struct _lm_device_t *_pdev);

/* ramrod completions */
#define MM_ACQUIRE_RAMROD_COMP_LOCK(_pdev)
#define MM_RELEASE_RAMROD_COMP_LOCK(_pdev)

void MM_ACQUIRE_IND_REG_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_IND_REG_LOCK(struct _lm_device_t *_pdev);

void MM_ACQUIRE_LOADER_LOCK();
void MM_RELEASE_LOADER_LOCK();
/* Slow path request manager lock */
void MM_ACQUIRE_SP_REQ_MGR_LOCK(struct _lm_device_t *_pdev);
void MM_RELEASE_SP_REQ_MGR_LOCK(struct _lm_device_t *_pdev);

void MM_ACQUIRE_SB_LOCK(struct _lm_device_t *_pdev, u8_t _sb_idx);
void MM_RELEASE_SB_LOCK(struct _lm_device_t *_pdev, u8_t _sb_idx);

#define MM_WRITE_DOORBELL(PDEV,BAR,CID,VAL);\
    LM_BAR_WR32_ADDRESS(PDEV,((u8_t *) PFDEV(PDEV)->context_info->array[VF_TO_PF_CID(PDEV,CID)].cid_resc.mapped_cid_bar_addr+(DPM_TRIGER_TYPE)), VAL);\

typedef void lm_generic_workitem_function(struct _lm_device_t *pdev);
lm_status_t 
mm_register_lpme(IN struct _lm_device_t *_pdev, 
                 IN lm_generic_workitem_function    *func,
                 IN const u8_t                      b_fw_access,
                 IN cons tu8_t                      b_queue_for_fw);

#define MM_REGISTER_LPME(_pdev,_func,_b_fw_access, _b_queue_for_fw)       mm_register_lpme(_pdev,_func,_b_fw_access, _b_queue_for_fw)

/* Not expected to be called under this OSs */
#define mm_er_initiate_recovery(_pdev) (LM_STATUS_FAILURE)
#define MM_REGISTER_DPC(_pdev,_func)  (LM_STATUS_FAILURE)

void 
mm_empty_ramrod_received_imp(struct _lm_device_t    *_pdev, 
                             const u32_t            empty_data);
#define MM_EMPTY_RAMROD_RECEIVED(pdev,lm_cli_idx)   mm_empty_ramrod_received_imp(pdev,empty_data)      

#define mm_dbus_start_if_enable(_pdev)

#define mm_dbus_stop_if_started(_pdev)

#define mm_atomic_set(_p, _v)   InterlockedExchange((long*)(_p), (long)(_v))

/* returns the decremented value */
#define mm_atomic_dec(_p)       InterlockedDecrement((long*)(_p))
#define mm_atomic_inc(_p)       InterlockedIncrement((long*)(_p))
#if (0)
void *
mm_alloc_mem(
    struct _lm_device_t* pdev,
    u32_t mem_size);

void *
mm_alloc_phys_mem(
    struct _lm_device_t* pdev,
    u32_t mem_size,
    lm_address_t *phys_mem,
    u8_t mem_type);
#endif
#if defined(NDIS31_MINIPORT) || defined(NDIS40_MINIPORT) || defined(NDIS50_MINIPORT)

#define mm_read_barrier()
#define mm_write_barrier()
#define mm_barrier()           

#else

#if defined(_IA64_)
#define mm_read_barrier()      KeMemoryBarrier()
#else
#define mm_read_barrier()      KeMemoryBarrierWithoutFence()
#endif

#define mm_write_barrier()     KeMemoryBarrier()
#define mm_barrier()           KeMemoryBarrier()

#endif

#define mm_atomic_set(_p, _v)   InterlockedExchange((long*)(_p), (long)(_v))

/* returns the decremented value */
#define mm_atomic_dec(_p)       InterlockedDecrement((long*)(_p))
#define mm_atomic_inc(_p)       InterlockedIncrement((long*)(_p))


#endif //NDISMONO


#define RESOURCE_TRACE_FLAG_COUNTERS 0x01
#define RESOURCE_TRACE_FLAG_DESC     0x02
#define RESOURCE_TRACE_FLAG_MDL      0x04 // Currently - not working well!!!

#define MEM_TRACE_FLAG_HIGH          ( RESOURCE_TRACE_FLAG_COUNTERS | RESOURCE_TRACE_FLAG_DESC )
#define MEM_TRACE_FLAG_DEFAULT       RESOURCE_TRACE_FLAG_COUNTERS

#define RESOURCE_TRACE_INC(_pdev, _cli_idx, _type, _field)                                                              \
{                                                                                                                       \
    DbgBreakIf((_cli_idx) >= MAX_DO_TYPE_CNT);                                                                          \
    DbgBreakIf((_type) >= RESOURCE_TYPE_MAX);                                                                           \
    InterlockedIncrement((long*)&_pdev->resource_list.type_counters_arr[_cli_idx][_type]._field);                       \
}

#define RESOURCE_TRACE_DEC(_pdev, _cli_idx, _type, _field)                                                              \
{                                                                                                                       \
    DbgBreakIf((_cli_idx) >= MAX_DO_TYPE_CNT);                                                                          \
    DbgBreakIf((_type) >= RESOURCE_TYPE_MAX);                                                                           \
    InterlockedDecrement((long*)&_pdev->resource_list.type_counters_arr[_cli_idx][_type]._field);                       \
}

#define RESOURCE_TRACE_ADD(_pdev, _cli_idx, _type, _field, _size)                                                       \
{                                                                                                                       \
    DbgBreakIf((_cli_idx) >= MAX_DO_TYPE_CNT);                                                                          \
    DbgBreakIf((_type) >= RESOURCE_TYPE_MAX);                                                                           \
    InterlockedExchangeAdd((long*)&(_pdev->resource_list.type_counters_arr[_cli_idx][_type]._field), (long)(_size));    \
}

#define RESOURCE_TRACE_SUB(_pdev, _cli_idx, _type, _field, _size) RESOURCE_TRACE_ADD( _pdev, _cli_idx, _type, _field, 0L-(long)_size)

#define RESOURCE_TRACE_UPDATE_PEAK(_pdev, _cli_idx, _type)                                                              \
{                                                                                                                       \
    DbgBreakIf((_cli_idx) >= MAX_DO_TYPE_CNT);                                                                          \
    DbgBreakIf((_type) >= RESOURCE_TYPE_MAX);                                                                           \
    if( _pdev->resource_list.type_counters_arr[_cli_idx][_type].size > _pdev->resource_list.type_counters_arr[_cli_idx][_type].size_peak )  \
    {\
        _pdev->resource_list.type_counters_arr[_cli_idx][_type].size_peak = _pdev->resource_list.type_counters_arr[_cli_idx][_type].size;   \
    }\
    if( _pdev->resource_list.type_counters_arr[_cli_idx][_type].cnt > _pdev->resource_list.type_counters_arr[_cli_idx][_type].cnt_peak)     \
    {\
        _pdev->resource_list.type_counters_arr[_cli_idx][_type].cnt_peak = _pdev->resource_list.type_counters_arr[_cli_idx][_type].cnt;     \
    }\
}


/*******************************************************************************
* OS dependent functions called by the 'lm' routines.
******************************************************************************/

/* Busy delay for the specified microseconds. */
void
mm_wait(
struct _lm_device_t *pdev,
    u32_t delay_us);

/* This routine is called to read a PCI configuration register.  The register
* must be 32-bit aligned. */
lm_status_t
mm_read_pci(
struct _lm_device_t *pdev,
    u32_t pci_reg,
    u32_t *reg_value);

/* This routine is called to write a PCI configuration register.  The
* register must be 32-bit aligned. */
lm_status_t
mm_write_pci(
struct _lm_device_t *pdev,
    u32_t pci_reg,
    u32_t reg_value);

/* This routine is called to map the base address of the device registers
* to system address space so that registers are accessible.  The base
* address will be unmapped when the driver unloads. */
void *
mm_map_io_base(
struct _lm_device_t *pdev,
    lm_address_t base_addr,
    u32_t size,
    u8_t  bar /*used in linux*/);

/* This routine is called to read driver configuration.  It is called from
* lm_get_dev_info. */
lm_status_t
mm_get_user_config(
struct _lm_device_t *pdev);

/* This routine returns the size of a packet descriptor. */
u32_t
mm_desc_size(
struct _lm_device_t *pdev,
    u32_t desc_type);
#define DESC_TYPE_L2TX_PACKET           0
#define DESC_TYPE_L2RX_PACKET           1

#ifdef __SunOS
void *
mm_map_io_space_solaris(lm_device_t *      pLM,
                        lm_address_t       physAddr,
                        u8_t               bar,
                        u32_t              offset,
                        u32_t              size,
                        ddi_acc_handle_t * pRegAccHandle);
#else
void *
mm_map_io_space(
    struct _lm_device_t *pdev,
    lm_address_t phys_addr,
    u32_t size);
#endif

void
mm_unmap_io_space(
    struct _lm_device_t *pdev,
    void *virt_addr,
    u32_t size);

/* correlates between UM-LM client indexes values */
u8_t
mm_cli_idx_to_um_idx(
    u8_t cli_idx);

#ifdef VF_INVOLVED
void mm_vf_pf_arm_trigger(struct _lm_device_t *pdev, struct _lm_vf_pf_message_t *mess);
#endif

#if !defined(_VBD_)
#if (defined _VBD_CMD_)
void * mm_alloc_mem_imp( struct _lm_device_t *    pdev,
                         u32_t                    mem_size,
                         IN const char*           sz_file,
                         IN const unsigned long   line,
                         IN u8_t                  cli_idx ) ;

#define mm_alloc_mem(_pdev,_mem_size, cli_idx) mm_alloc_mem_imp( _pdev, _mem_size, __FILE_STRIPPED__, __LINE__, cli_idx ) ;

void * mm_rt_alloc_phys_mem_imp( struct _lm_device_t*    pdev,
                                 u32_t                  mem_size,
                                 lm_address_t*          phys_mem,
                                 u8_t                   mem_type,
                                 IN const char*         sz_file,
                                 IN const unsigned long line,
                                 IN u8_t                cli_idx ) ;
#define mm_rt_alloc_phys_mem( _pdev, _mem_size, _phys_mem, _flush_type, cli_idx ) \
        mm_rt_alloc_phys_mem_imp( _pdev, _mem_size, _phys_mem, _flush_type, __FILE_STRIPPED__, __LINE__, cli_idx ) ;

void * mm_rt_alloc_mem_imp( struct _lm_device_t*    pdev,
                            u32_t                  mem_size,
                            IN const char*         sz_file,
                            IN const unsigned long line,
                            IN u8_t                cli_idx ) ;

#define mm_rt_alloc_mem( _pdev, _mem_size, cli_idx ) \
        mm_rt_alloc_mem_imp( _pdev, _mem_size, __FILE_STRIPPED__, __LINE__, cli_idx ) ;

void * mm_alloc_phys_mem_imp( struct _lm_device_t*     pdev,
                              u32_t                    mem_size,
                              lm_address_t *           phys_mem,
                              u8_t                     mem_type,
                              IN const char*           sz_file,
                              IN const unsigned long   line,
                              IN u8_t                  cli_idx ) ;

#define mm_alloc_phys_mem( _pdev, _mem_size, _phys_mem, _mem_type, cli_idx ) \
    mm_alloc_phys_mem_imp(_pdev, _mem_size, _phys_mem, _mem_type, __FILE_STRIPPED__, __LINE__, cli_idx ) ;

void * mm_alloc_phys_mem_align_imp( struct _lm_device_t*     pdev,
                                    u32_t                    mem_size,
                                    lm_address_t*            phys_mem,
                                    u32_t                    alignment,
                                    u8_t                     mem_type,
                                    IN const char*           sz_file,
                                    IN const unsigned long   line,
                                    IN u8_t                  cli_idx ) ;

#define mm_alloc_phys_mem_align( _pdev, _mem_size, _phys_mem, _alignment, _mem_type, cli_idx ) \
    mm_alloc_phys_mem_align_imp( _pdev, _mem_size, _phys_mem, _alignment, _mem_type, __FILE_STRIPPED__, __LINE__, cli_idx ) ;

#else

/* This routine is responsible for allocating system memory and keeping track
* of it.  The memory will be freed later when the driver unloads.  This
* routine is called during driver initialization. */
void *
mm_alloc_mem(
struct _lm_device_t *pdev,
    u32_t mem_size,
	u8_t cli_idx);

/* This routine is responsible for physical memory and keeping track
* of it.  The memory will be freed later when the driver unloads. */
void *
mm_alloc_phys_mem(
struct _lm_device_t *pdev,
    u32_t mem_size,
    lm_address_t *phys_mem,
    u8_t mem_type,
	u8_t cli_idx);

void *
mm_alloc_phys_mem_align(
struct _lm_device_t *pdev,
    u32_t mem_size,
    lm_address_t *phys_mem,
    u32_t alignment,
    u8_t mem_type,
	u8_t cli_idx);

void *
mm_rt_alloc_mem(
    struct _lm_device_t *pdev,
    u32_t mem_size,
	u8_t cli_idx);

void *
mm_rt_alloc_phys_mem(
    struct _lm_device_t *pdev,
    u32_t mem_size,
    lm_address_t *phys_mem,
    u8_t flush_type,
	u8_t cli_idx);
#endif

#ifdef LM_RXPKT_NON_CONTIGUOUS
void *
mm_alloc_rxpkt(
    struct _lm_device_t *pdev,
    u32_t desc_size,
    u8_t mm_cli_idx);

/*
 * UM is responsible for calling mm_free_rxpkt() for all packets
 * allocated via mm_alloc_rxpkt().
 */
void
mm_free_rxpkt(
    struct _lm_device_t *pdev,
    lm_packet_t *pkt);
#endif /* not LM_RXPKT_CONTIGUOUS */

#endif // !_VBD_

lm_status_t mm_vf_en(
	IN   struct _lm_device_t* pdev,
	IN   u16_t vfs_num
	);

void mm_vf_dis(
	IN   struct _lm_device_t* pdev
	);

/**
 * Returns current high-definition time.
 */
u64_t mm_get_current_time(struct _lm_device_t *pdev);

#define PHYS_MEM_TYPE_UNSPECIFIED       0
#define PHYS_MEM_TYPE_NONCACHED         1

void
mm_rt_free_mem(
    struct _lm_device_t *pdev,
    void *mem_virt,
    u32_t mem_size,   /* this parameter is used for debugging only */
    u8_t  cli_idx);

void
mm_rt_free_phys_mem(
    struct _lm_device_t *pdev,
    u32_t mem_size,
    void *virt_mem,
    lm_address_t phys_mem,
    u8_t  cli_idx);

// use to reset memory areas
void
mm_memset(void * buf,
          u8_t val,
          u32_t mem_size);

// use to copy memory
void
mm_memcpy(void * destenation,
          const void * source,
          u32_t  mem_size);

// use to zero memory
#define mm_mem_zero( buf, mem_size ) mm_memset( buf, 0, mem_size )

// Windows only function
u32_t mm_get_wol_flags( IN struct _lm_device_t* pdev );

/**
 *  Description
 *    function compares between two buffers. compares 'count' bytes
 *  Returns
 *    TRUE if buffers match
 *    FALSE o/w
 */
u8_t
mm_memcmp(void * buf1, void * buf2, u32_t count);

/* This routine is called to indicate completion of a transmit request.
* If 'packet' is not NULL, all the packets in the completion queue will be
* indicated.  Otherwise, only 'packet' will be indicated. */
void
mm_indicate_tx(
struct _lm_device_t *pdev,
    u32_t chain_idx,
    s_list_t *packet_list);

/* This routine is called to indicate received packets.  If 'packet' is not
* NULL, all the packets in the received queue will be indicated.  Otherwise,
* only 'packet' will be indicated. */
void
mm_indicate_rx(
struct _lm_device_t *pdev,
    u32_t chain_idx,
    s_list_t *packet_list);

/* lm_service_phy_int calls this routine to indicate the current link. */
void
mm_indicate_link(
struct _lm_device_t *pdev,
    lm_status_t link,
    lm_medium_t medium);

typedef void(*lm_task_cb_t)(struct _lm_device_t *pdev, void *param);

/* call the task cb after the specified delay */
lm_status_t
mm_schedule_task(
    struct _lm_device_t *pdev,
    u32_t delay_ms,
    lm_task_cb_t task,
    void * param
);

void
mm_set_done(
    struct _lm_device_t *pdev,
    u32_t cid,
    void * cookie);

struct sq_pending_command;

void mm_return_sq_pending_command(
    struct _lm_device_t * pdev,
    struct sq_pending_command * pending);

struct sq_pending_command * mm_get_sq_pending_command(
    struct _lm_device_t * pdev);

u32_t mm_copy_packet_buf(
    IN struct _lm_device_t  * pdev,
    IN struct _lm_packet_t  * lmpkt,         /* packet to copy from      */
    IN u8_t                 * mem_buf,       /* Memory buffer to copy to */
    IN u32_t                size           /* number of bytes to be copied */
    );

// logging functions (e.g. Windows event viewer)
lm_status_t mm_event_log_generic_arg_fwd( IN struct _lm_device_t* pdev, const lm_log_id_t lm_log_id, va_list ap );
lm_status_t mm_event_log_generic( IN struct _lm_device_t* pdev, const lm_log_id_t lm_log_id, ... );

void mm_print_bdf(int, void*);

u32_t mm_get_cap_offset(
	IN  struct _lm_device_t *pdev, 
	IN  u32_t cap_id
	);

#ifdef __LINUX
void mm_eth_ramrod_comp_cb(struct _lm_device_t *pdev, struct common_ramrod_eth_rx_cqe *cqe);
void mm_common_ramrod_comp_cb(struct _lm_device_t *pdev, struct event_ring_msg *msg);
#endif // __LINUX

#ifdef VF_INVOLVED
lm_status_t mm_get_sriov_info(
	IN   struct _lm_device_t     *pdev,
	OUT  struct _lm_sriov_info_t *info
	);
#endif


void mm_set_l2pkt_info_offset( struct _lm_packet_t* lmpkt );

#ifndef __LINUX
#ifndef _VBD_
/* In Linux  & VBD these functions are define in mm.c --> they should be for other OSs as well */
#define mm_get_bar_offset lm_get_bar_offset_direct
#define mm_get_bar_size   lm_get_bar_size_direct
#endif



#ifdef BIG_ENDIAN
// LE
#define mm_le16_to_cpu(val) SWAP_BYTES16(val)
#define mm_cpu_to_le16(val) SWAP_BYTES16(val)
#define mm_le32_to_cpu(val) SWAP_BYTES32(val)
#define mm_cpu_to_le32(val) SWAP_BYTES32(val)
// BE
#define mm_be32_to_cpu(val) (val)
#define mm_cpu_to_be32(val) (val)
#define mm_be16_to_cpu(val) (val)
#define mm_cpu_to_be16(val) (val)
#else /* LITTLE_ENDIAN */
// LE
#define mm_le16_to_cpu(val) (val)
#define mm_cpu_to_le16(val) (val)
#define mm_le32_to_cpu(val) (val)
#define mm_cpu_to_le32(val) (val)
// BE
#define mm_be32_to_cpu(val) SWAP_BYTES32(val)
#define mm_cpu_to_be32(val) SWAP_BYTES32(val)
#define mm_be16_to_cpu(val) SWAP_BYTES16(val)
#define mm_cpu_to_be16(val) SWAP_BYTES16(val)

#endif

#endif

/* COMMON IMPLEMENATION FOR ALL PLATFORMS */
/* MACROS used in Ecore layer that need a LM implementation...                         */
static __inline void * mm_rt_zalloc_mem(struct _lm_device_t * pdev, u32_t size)
{
    void *ptr;
    ptr = mm_rt_alloc_mem(pdev, size,  0);
    if (ptr)
    {
        mm_mem_zero(ptr, size);
    }
    return ptr;
}


#endif /* _MM_H */

