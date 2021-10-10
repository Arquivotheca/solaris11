/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2003 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *
 *
 * History:
 *    10/10/01 Hav Khauv        Inception.
 ******************************************************************************/

#ifndef _LM5710_H
#define _LM5710_H

//migrated from 5706_reg.h
#ifndef __BIG_ENDIAN
#ifndef LITTLE_ENDIAN
    #define LITTLE_ENDIAN
#endif
#else
#undef LITTLE_ENDIAN
#ifndef BIG_ENDIAN
    #define BIG_ENDIAN
#endif
#ifndef BIG_ENDIAN_HOST
    #define BIG_ENDIAN_HOST
#endif
#endif

#ifndef INLINE
#if DBG
#define INLINE
#else
#define INLINE __inline
#endif
#endif

#if !defined(LITTLE_ENDIAN) && !defined(BIG_ENDIAN)
    #error "Missing either LITTLE_ENDIAN or BIG_ENDIAN definition."
#endif

#ifdef __LINUX
#include <linux/types.h>
#endif
#include "bcmtype.h"
#include "debug.h"
#include "igu_def.h"
#include "microcode_constants.h"
#include "fcoe_constants.h"
#include "toe_constants.h"
#include "tcp_constants.h"
#include "eth_constants.h"
//this is the included HSI
#include "5710_hsi.h"
#include "lm5710_hsi.h"
#include "pcics_reg_driver.h"
#include "bigmac_addresses.h"
#include "misc_bits.h"
#include "emac_reg_driver.h"
#include "dmae_clients.h"
#include "prs_flags.h"
#include "57712_reg.h"
#include "grc_addr.h"
#include "bd_chain_st.h"
#include "lm_sp_req_mgr.h"
#include "ecore_sp_verbs.h"
#include "license.h"
#include "mcp_shmem.h"


#ifdef VF_INVOLVED
#include "lm_vf.h"
#endif

#ifndef elink_dev
#define elink_dev _lm_device_t
#endif
#include "clc.h"
//#include "status_code.h"
// TODO - we will add ou rown shmem
//#include "shmem.h"
//
#define DEVICE_TYPE_PF      0
#define DEVICE_TYPE_VF      1

/* Virtualization types (vt) */
#define VT_NONE             0
#define VT_BASIC_VF         1
#define VT_CHANNEL_VF       2

#define IS_BASIC_VFDEV(pdev)    ((pdev)->pf_dev != NULL)
#define IS_CHANNEL_VFDEV(pdev)  (((pdev)->params.device_type == DEVICE_TYPE_VF) && ((pdev)->params.virtualization_type == VT_CHANNEL_VF))

#define IS_BASIC_VIRT_MODE_MASTER_PFDEV(pdev)      (((pdev)->params.device_type == DEVICE_TYPE_PF) && ((pdev)->params.virtualization_type == VT_BASIC_VF))
#define IS_CHANNEL_VIRT_MODE_MASTER_PFDEV(pdev)    (((pdev)->params.device_type == DEVICE_TYPE_PF) && ((pdev)->params.virtualization_type == VT_CHANNEL_VF))

#define IS_PFDEV(pdev)          (((pdev)->pf_dev == NULL) && ((pdev)->params.device_type == DEVICE_TYPE_PF))
#define IS_VFDEV(pdev)          (((pdev)->pf_dev != NULL) || ((pdev)->params.device_type == DEVICE_TYPE_VF))
#define PFDEV(pdev)         (((pdev)->pf_dev == NULL) ? (pdev) : (pdev)->pf_dev)



#define LM_VF_MAX_RVFID_SIZE    6

#define LM_VF_CID_WND_SIZE      0

#define LM_VF_NUM_CIDS_MASK     ((1 << LM_VF_CID_WND_SIZE) - 1)

#define LM_VF_CID_BASE          (1 << (LM_VF_MAX_RVFID_SIZE + LM_VF_CID_WND_SIZE))


#define VF_TO_PF_CID(pdev,cid) (IS_BASIC_VFDEV(pdev) ? (((1 << LM_VF_MAX_RVFID_SIZE) | ABS_VFID(pdev)) <<  LM_VF_CID_WND_SIZE) | cid : cid)
#define PF_TO_VF_CID(pdev,cid) (IS_BASIC_VFDEV(pdev) ? cid & LM_VF_NUM_CIDS_MASK : cid)

#define SET_CON_CID(_pdev,_cid) PFDEV(_pdev)->vars.connections[VF_TO_PF_CID(_pdev,_cid)].con_cid = VF_TO_PF_CID(_pdev,_cid)
#define SET_CON_STATE(_pdev,_cid,_state) PFDEV(_pdev)->vars.connections[VF_TO_PF_CID(_pdev,_cid)].con_state = _state
#define GET_CON_STATE(_pdev,_cid) PFDEV(_pdev)->vars.connections[VF_TO_PF_CID(_pdev,_cid)].con_state

#define VF_BAR0_IGU_OFFSET          0x0000   /*0x0000-0x3000: (12KB)*/
#define VF_BAR0_USDM_QUEUES_OFFSET  0x3000  /*-0x4100: (ZoneA) (4352B)*/
#define VF_BAR0_CSDM_QUEUES_OFFSET  0x4100  /*-0x5200: (ZoneA) (4352B)*/
#define VF_BAR0_XSDM_QUEUES_OFFSET  0x5200  /*-0x6300: (ZoneA) (4352B)*/
#define VF_BAR0_TSDM_QUEUES_OFFSET  0x6300  /*-0x7400: (ZoneA) (4352B)*/
#define VF_BAR0_USDM_GLOBAL_OFFSET  0x7400  /*-0x7600: (ZoneB) (512B)*/
#define VF_BAR0_CSDM_GLOBAL_OFFSET  0x7600  /*-0x7800: (ZoneB) (512B)*/
#define VF_BAR0_XSDM_GLOBAL_OFFSET  0x7800  /*-0x7A00: (ZoneB) (512B)*/
#define VF_BAR0_TSDM_GLOBAL_OFFSET  0x7A00  /*-0x7C00: (ZoneB) (512B)*/
#define VF_BAR0_DB_OFFSET           0x7C00  /*-0x7E00: (512B)*/
#define VF_BAR0_GRC_OFFSET          0x7E00   /*-0x8000:(512B) */

/* multi function mode is supported on (5711+5711E FPGA+EMUL) and on (5711E ASIC) and on 5712E and 5713E */
#define IS_MF_MODE_CAPABLE(pdev) ((CHIP_NUM(pdev) == CHIP_NUM_5711E) || \
                                  (CHIP_NUM(pdev) == CHIP_NUM_5712E) || \
                                  (CHIP_IS_E3(pdev)))

/* Macro for triggering PCIE analyzer: write to 0x2000 */
#define LM_TRIGGER_PCIE(_pdev)               \
        {                                    \
              u32_t kuku = 0xcafecafe;       \
              REG_WR((_pdev), 0x2000, kuku); \
        }

// Send an attention on this Function.
#define LM_GENERAL_ATTN_INTERRUPT_SET(_pdev,_func)                REG_WR((_pdev),MISC_REG_AEU_GENERAL_ATTN_12 + 4*(_func),0x1)
/*******************************************************************************
 * Constants.
 ******************************************************************************/
#define MAX_PATH_NUM               2
#define E2_MAX_NUM_OF_VFS          64
#define E1H_FUNC_MAX               8
#define E2_FUNC_MAX                4   /* per path */
#define MAX_FUNC_NUM               8   /* Common to all chips */
#define MAX_VNIC_NUM               4
#define MAX_NDSB                   128
#define MAX_RSS_CHAINS             (16)   /* a constatnt for _HW_ limit */

typedef enum
{
    LM_CLI_IDX_NDIS          =  0,
    LM_CLI_IDX_RDMA          =  1,
    LM_CLI_IDX_ISCSI         =  2, /* iSCSI idx must be after ndis+rdma */
    LM_CLI_IDX_FCOE          =  3, /* FCOE idx must be after ndis+rdma */
    LM_CLI_IDX_FWD           =  4,
    LM_CLI_IDX_OOO           =  5,
    LM_CLI_IDX_MAX           =  6,
} lm_cli_idx_t;

typedef enum
{
    LM_RESOURCE_NDIS          =  LM_CLI_IDX_NDIS,
    LM_RESOURCE_RDMA          =  LM_CLI_IDX_RDMA,
    LM_RESOURCE_ISCSI         =  LM_CLI_IDX_ISCSI, /* iSCSI idx must be after ndis+rdma */
    LM_RESOURCE_FCOE          =  LM_CLI_IDX_FCOE, /* FCOE idx must be after ndis+rdma */
    LM_RESOURCE_FWD           =  LM_CLI_IDX_FWD,
    LM_RESOURCE_OOO           =  LM_CLI_IDX_OOO,
    LM_RESOURCE_COMMON        =  LM_CLI_IDX_MAX,
} lm_resource_idx_t;

struct sq_pending_command
{
    d_list_entry_t           list;
    u32_t                    cid;
    u16_t                    type;
    u8_t                     cmd;
    u8_t                     flags;
#define SQ_PEND_RELEASE_MEM 0x1
#define SQ_PEND_COMP_CALLED 0x2

    struct slow_path_element command;
};

#include "lm_desc.h"
#include "listq.h"
#include "lm.h"
#include "lm_stats.h"
#include "hw_dmae.h"
#if !defined(_B10KD_EXT)
#include "bcm_utils.h"
#endif

#define EVEREST 1

/* non rss chains - RDMA, ISCSI, FCOE, FWD, ISCSI OOO */
#define MAX_NON_RSS_CHAINS         (5)

/* which of the non-rss chains need fw clients - ISCSI, FCOE*/
#define MAX_NON_RSS_FW_CLIENTS     (3)

#define MAX_ETH_CONS               (MAX_RSS_CHAINS + MAX_NON_RSS_CHAINS)

#ifndef VF_INVOLVED
#define MAX_VF_ETH_CONS             0
#endif

#define MAX_TX_CHAIN               (MAX_ETH_CONS)
#define MAX_RX_CHAIN               (MAX_ETH_CONS)


#define ILT_NUM_PAGE_ENTRIES 3072
#define ILT_NUM_PAGE_ENTRIES_PER_FUNC 384

/* According to the PCI-E Init document */
#define SEARCHER_TOTAL_MEM_REQUIRED_PER_CON 64
#define TIMERS_TOTAL_MEM_REQUIRED_PER_CON   8
#define QM_TOTAL_MEM_REQUIRED_PER_CON       (32*4)


/* Number of bits must be 10 to 25. */
#ifndef LM_PAGE_BITS
#define LM_PAGE_BITS                            12  /* 4K page. */
#endif

#define LM_PAGE_SIZE                            (1 << LM_PAGE_BITS)
#define LM_PAGE_MASK                            (LM_PAGE_SIZE - 1)


/* Number of bits must be 10 to 25. */
#define LM_DQ_CID_BITS                            7  /* 128 Byte page. */

#define LM_DQ_CID_SIZE                            (1 << LM_DQ_CID_BITS)
#define LM_DQ_CID_MASK                            (LM_DQ_CID_SIZE - 1)

#define LM_ILT_ALIGNMENT                        0x1000 /* ILT assumes pages aligned to 4K NOTE: E1 has a bug,
                                                        * in which page needs to be aligned to page-size
                                                        */

#define LM_ILT_ALIGNMENT_MASK                   (LM_ILT_ALIGNMENT - 1)

#define LM_TIMERS_SCAN_POLL                     20000 /* 20 sec */
#define LM_TIMERS_SCAN_TIME                     1000 /*1m*/
#define LM_UNLOAD_TIME                          100000 /*100m in micros */
#if !defined(_VBD_CMD_)
#define LM_CID_RETURN_TIME                      2000  /*2 sec on emulation*/
#define LM_CID_RETURN_TIME_EMUL                 10000 /*10 sec on emulation*/

#else
#define LM_CID_RETURN_TIME                      0
#define LM_CID_RETURN_TIME_EMUL                 0
#endif

// TODO add for ASIC
#define LM_FREE_CID_DELAY_TIME(pdev)  ((pdev)->params.l4_free_cid_delay_time)
/*
#define LM_FREE_CID_DELAY_TIME(pdev) (CHIP_REV(pdev) == CHIP_REV_FPGA || CHIP_REV(pdev) == CHIP_REV_EMUL) ? LM_CID_RETURN_TIME_EMUL : LM_CID_RETURN_TIME;
*/

#define LM_EMUL_FACTOR 2000
#define LM_FPGA_FACTOR 200

#ifndef CACHE_LINE_SIZE_MASK
#define CACHE_LINE_SIZE_MASK        0x3f
#endif
/*need to know from where can I take these values */
#define NVRAM_1MB_SIZE              0x20000  // 1M bit in bytes
#define NVRAM_PAGE_SIZE             256

/* Number of packets per indication in calls to mm_indicate_rx/tx. */
#ifndef MAX_PACKETS_PER_INDICATION
#define MAX_PACKETS_PER_INDICATION  50
#endif

// TODO - adjust to our needs - the limitation of the PBF
#ifndef MAX_FRAG_CNT
#define MAX_FRAG_CNT                33
#endif
#ifndef MAX_FRAG_CNT_PER_TB
/* MichalS TODO - do we want to leave it like this or calculate it according to connection params. */
#define MAX_FRAG_CNT_PER_TB         33  /* arbitrary(?) */
#endif

/* The maximum is actually 0xffff which can be described by a BD. */
// TODO - adjust to our needs
#define MAX_FRAGMENT_SIZE           0xf000

/* Maximum Packet Size: max jumbo frame: 9600 + ethernet-header+llc-snap+vlan+crc32 */
#define MAXIMUM_PACKET_SIZE 9632

// TODO - adjust to our needs
/* Buffer size of the statistics block. */
#define CHIP_STATS_BUFFER_SIZE      ((sizeof(statistics_block_t) + \
                                        CACHE_LINE_SIZE_MASK) & \
                                        ~CACHE_LINE_SIZE_MASK)

// Status blocks type per storm - used for initialization
#define STATUS_BLOCK_INVALID_TYPE       0
#define STATUS_BLOCK_SP_SL_TYPE         1
#define STATUS_BLOCK_NORMAL_TYPE        2
#define STATUS_BLOCK_NORMAL_SL_TYPE     3

#define LM_DEF_NO_EVENT_ACTIVE                     0x00000000
#define LM_DEF_ATTN_ACTIVE                         (1L<<0)
#define LM_SP_ACTIVE                               (LM_DEF_USTORM_ACTIVE | LM_DEF_CSTORM_ACTIVE | LM_DEF_XSTORM_ACTIVE | LM_DEF_TSTORM_ACTIVE)

#define LM_DEF_USTORM_ACTIVE                       (1L<<1)
#define LM_DEF_CSTORM_ACTIVE                       (1L<<2)
#define LM_DEF_XSTORM_ACTIVE                       (1L<<3)
#define LM_DEF_TSTORM_ACTIVE                       (1L<<4)

#define LM_DEF_EVENT_MASK                          0xffff

#define LM_NON_DEF_USTORM_ACTIVE                   (1L<<16)
#define LM_NON_DEF_CSTORM_ACTIVE                   (1L<<17)
#define LM_NON_DEF_EVENT_MASK                      0xffff0000

#define ATTN_NIG_FOR_FUNC     1L << 8
#define ATTN_SW_TIMER_4_FUNC  1L << 9
#define GPIO_2_FUNC           1L << 10
#define GPIO_3_FUNC           1L << 11
#define GPIO_4_FUNC           1L << 12
#define ATTN_GENERAL_ATTN_1   1L << 13
#define ATTN_GENERAL_ATTN_2   1L << 14
#define ATTN_GENERAL_ATTN_3   1L << 15

#define ATTN_NIG_FOR_FUNC1     1L << 8
#define ATTN_SW_TIMER_4_FUNC1  1L << 9
#define GPIO_2_FUNC1           1L << 10
#define GPIO_3_FUNC1           1L << 11
#define GPIO_4_FUNC1           1L << 12
#define ATTN_GENERAL_ATTN_4    1L << 13
#define ATTN_GENERAL_ATTN_5    1L << 14
#define ATTN_GENERAL_ATTN_6    1L << 15

#define ATTN_HARD_WIRED_MASK  0xff00

#define HC_SEG_ACCESS_DEF           0   /*Driver decision 0-3*/
#define HC_SEG_ACCESS_ATTN          4

#define HC_SEG_ACCESS_NORM          0   /*Driver decision 0-1*/

//Buffer size of the status block. This is the same for host_def_status_block, they are the same size.
//TODO: check the cache line issue! do we need it as in Teton?
#define E2_STATUS_BLOCK_BUFFER_SIZE     ((sizeof(struct host_hc_status_block_e2) + \
                                        CACHE_LINE_SIZE_MASK) & \
                                        ~CACHE_LINE_SIZE_MASK)

#define E1X_STATUS_BLOCK_BUFFER_SIZE     ((sizeof(struct host_hc_status_block_e1x) + \
                                        CACHE_LINE_SIZE_MASK) & \
                                        ~CACHE_LINE_SIZE_MASK)

#define DEF_STATUS_BLOCK_BUFFER_SIZE ((sizeof(struct host_sp_status_block) + \
                                        CACHE_LINE_SIZE_MASK) & \
                                        ~CACHE_LINE_SIZE_MASK)

/* This is the def and non-def status block ID format according to spec --> used for debugging purpose only */
#define DBG_SB_ID(port,stormID,cpuID) (((port) << 7) | ((stormID) << 5) | (cpuID))
#define DBG_DEF_SB_ID(port,stormID,vnicID) (((port) << 7) | ((stormID) << 5) | (0x10+vnicID)) /* the ID is for debugging purposes, it's not looked at by hw/fw*/

#define SB_RX_INDEX(pdev, index)     ((pdev)->vars.u_hc_ack[index])
#define SB_TX_INDEX(pdev, index)     ((pdev)->vars.c_hc_ack[index])

#define SB_INDEX_OF_USTORM(pdev, index)     ((pdev)->vars.u_hc_ack[index])
//#define SB_INDEX_OF_CSTORM(pdev, index)     ((pdev)->vars.c_hc_ack[index])

#define DEF_SB_INDEX(pdev)                  ((pdev)->vars.hc_def_ack)
#define DEF_SB_INDEX_OF_ATTN(pdev)          ((pdev)->vars.attn_def_ack)

//_________________________________________________________________________________________________--

#define NUM_OF_ELT_PAGES 16 // this is the size of the elt in the hw
#define DEF_STATUS_BLOCK_IGU_INDEX 16 //MAX_NDSB //this is where the default status block lies (that is VBD's static index of default status block)
#define DEF_STATUS_BLOCK_INDEX HC_SP_SB_ID //this is where the default status block lies (that is VBD's static index of default status block)
#define MAX_DYNAMIC_ATTN_GRPS 8 //this is the 8 non hard-wired groups configured by the driver (exc. PXP,NIG)
#define MAX_NUM_BAR 3 // number of bars suported by the hw 1 bar in first phase emulation
#define BAR_0 0 //index for BAR0
#define BAR_1 1 //index for BAR1
#define BAR_2 2 //index for BAR2

/* HW RSS configuration */
#define RSS_INDIRECTION_TABLE_SIZE  0x80    /* Maximum indirection table. */
#define RSS_HASH_KEY_SIZE           0x28    /* Maximum key size. */

/* RX BD to RX CQE size ratio */
#define LM_RX_BD_CQ_SIZE_RATIO      4



/*******************************************************************************
 * Macros.
 ******************************************************************************/
#ifndef OFFSETOF
#define OFFSETOF(_s, _m)    ((u32_t) PTR_SUB(&((_s *) 0)->_m, (u8_t *) 0))
#endif
#define WORD_ALIGNED_OFFSETOF(_s, _m)       (OFFSETOF(_s, _m) & ~0x03)

/* warning NOT side effect safe dont use this with CEIL_DIV( a++,b) */
#define CEIL_DIV( a, b )    ((a / b) + ( (a % b) ? 1 : 0))


/*
   The attention lines works with the state machine below for parallel computation:

   cols:     0 1 2 3 4 5 6 7
   _________________________
   Attn_bits 0 0 1 1 0 0 1 1
   Attn_ack  0 1 0 1 0 1 0 1
   State     0 0 0 0 1 1 1 1

   cols: 0,1,6,7 - NOP
   cols: 3,4     - ASSERT
   cols: 2       - Assertion procedure
   cols: 5       - Deassertion procedure
*/
#define GET_ATTN_CHNG_GROUPS(_pdev, _attn_bits, _attn_ack, _asserted_grps_ptr, _deasserted_grps_ptr) \
    {                                                                         \
        u16_t _state = (_pdev)->vars.attn_state;                              \
                                                                              \
        DbgBreakIf(~(_attn_bits ^ _attn_ack) & (_attn_bits ^ _state));        \
                                                                              \
        *(_asserted_grps_ptr)    =  _attn_bits & ~_attn_ack & ~_state;        \
        *(_deasserted_grps_ptr)  = ~_attn_bits &  _attn_ack &  _state;        \
    }

/* Finds out whether a specific unicore interrupt has caused the NIG attn to get asserted.
 * If this is the case, need to adjust the portion of bits of the NIG config status interrupt register
 * to the value read from the unicore interrupt register.
 * We use here a "bit overwrite" instead of just a "bit flip" since the value read from the
 * unicore interrupt register might be spread over more than a single bit!
 */
#define HANDLE_UNICORE_INT_ASSERTED(_pdev, _nig_reg_name, _unicore_intr_val_ptr, _unicore_intr_name, _nig_status_port_ptr, _is_unicore_assrtd_ptr, _unicore_intr_size)  \
    {                                                                                                                                         \
        *(_unicore_intr_val_ptr) = REG_RD(_pdev, _nig_reg_name);                                                                     \
        *(_is_unicore_assrtd_ptr) = ( ( *(_unicore_intr_val_ptr) << _unicore_intr_size) ^ (*(_nig_status_port_ptr) & _unicore_intr_name));    \
                                                                                                                                              \
        if (*(_is_unicore_assrtd_ptr))                                                                                                        \
        {                                                                                                                                     \
            DbgMessage(_pdev, WARN, "lm_handle_assertion_processing(): " #_unicore_intr_name " asserted!\n");                                \
            *(_nig_status_port_ptr)  = (*(_nig_status_port_ptr) & ~(_unicore_intr_name)) | (*(_unicore_intr_val_ptr) << _unicore_intr_size);  \
        }                                                                                                                                     \
    }
    // *(_nig_status_port_ptr) ^= ( 0x1 << _unicore_intr_size);

/***********************  ECORE WRAPPER MACROS *****************************************/
#define ECORE_ZALLOC(_pdev, _size) mm_rt_zalloc_mem(_pdev, _size)
#define ECORE_FREE(_pdev, _buf, _size) mm_rt_free_mem(_pdev, _buf, _size, 0)

/* Ecore implementation of set/get flag (differs from our set_flags, get_flags) */
#define ECORE_SET_FLAG(value, mask, flag) \
    do {\
        (value) &= ~(mask);\
        (value) |= ((flag) << (mask##_SHIFT));\
    }while(0)

#define ECORE_GET_FLAG(value, mask) \
    (((value) &= (mask)) >> (mask##_SHIFT))

/* Ecore debug prints */
#define ECORE_ERR(str) DbgMessage(pdev, FATAL, str)
#define ECORE_ERR1(str,param1) DbgMessage1(pdev, FATAL, str,param1)
#define ECORE_ERR2(str,param1, param2) DbgMessage1(pdev, FATAL, str,param1, param2)

#define ecore_sp_post(_pdev,_cmd , _cid, _data, _con_type) \
    lm_sq_post(_pdev, _cid, _cmd, CMD_PRIORITY_NORMAL, _con_type, _data.as_u64)

#ifndef __LINUX
#define BUG() DbgBreakMsg("Bug")
#define smp_mb()                   mm_barrier()
#define smp_mb__before_clear_bit() mm_barrier()
#define smp_mb__after_clear_bit()  mm_barrier()
#define mb()                       mm_barrier()

#define ECORE_MIGHT_SLEEP() //IRQL_PASSIVE_CODE()

/* Aomic Bit Manipulation */
#define ECORE_SET_BIT(bit, var)   mm_atomic_or(  var, (1<<bit))
#define ECORE_CLEAR_BIT(bit, var) mm_atomic_and( var, ~(1<<bit))
#define ECORE_TEST_BIT(bit, var) ((mm_atomic_read( var) & (1<<bit)) == (1<<bit))

#else

#define ECORE_MIGHT_SLEEP() ediag_might_sleep()

#define ECORE_SET_BIT(bit, var)   set_bit(bit, var)
#define ECORE_CLEAR_BIT(bit, var) clear_bit(bit, var)
#define ECORE_TEST_BIT(bit, var)  test_bit(bit, var)

#endif

#define ecore_list_for_each_entry(pos, _head, _link, cast) \
    for (pos = (cast *)d_list_peek_head(_head); \
         pos; \
         pos = (cast *)d_list_next_entry(&pos->_link))

/**
  * list_for_each_entry_safe - iterate over list of given type safe against removal of list entry
  * @pos:        the type * to use as a loop cursor.
  * @n:          another type * to use as temporary storage
  * @head:       the head for your list.
  * @member:     the name of the list_struct within the struct.
  */
#define ecore_list_for_each_entry_safe(pos, n, head, member, cast)   \
         for (pos = (cast *)d_list_peek_head(head), n = (pos)? (cast *)d_list_next_entry(&pos->member) : NULL; \
              pos != NULL;  \
              pos = (cast *)n, n = (pos)? (cast *)d_list_next_entry(&pos->member) : NULL)


#define ecore_list_is_last(_link, _list) (_link == (_list)->tail)

/* Types used in ecore */

typedef enum ecore_status_t
{
    ECORE_SUCCESS=LM_STATUS_SUCCESS,
    ECORE_TIMEOUT=LM_STATUS_TIMEOUT,
    ECORE_INVAL=LM_STATUS_INVALID_PARAMETER,
    ECORE_BUSY=LM_STATUS_BUSY,
    ECORE_NOMEM=LM_STATUS_RESOURCE,
    ECORE_PENDING=LM_STATUS_PENDING,
    ECORE_EXISTS=LM_STATUS_EXISTING_OBJECT,
    ECORE_IO=LM_STATUS_FAILURE
} ecore_status ;

 /*******************************************************************************
 * Statistics.
 ******************************************************************************/
typedef struct _lm_rx_statistics_t
{
    u32_t aborted;
} lm_rx_stats_t;

/*******************************************************************************
 * Packet descriptor.
 ******************************************************************************/

typedef struct _lm_coalesce_buffer_t
{
    s_list_entry_t link;

    u8_t *mem_virt;
    u32_t buf_size;
    lm_frag_list_t frags; /* coalesce buf is a frag list with 1 frag */
} lm_coalesce_buffer_t;

typedef struct _lm_client_con_params_t
{
    u32_t       mtu;
    u32_t       lah_size;
    u32_t       num_rx_desc;
    u32_t       num_tx_desc;
    u8_t        attributes;
    #define     LM_CLIENT_ATTRIBUTES_RX     (1<<0)
    #define     LM_CLIENT_ATTRIBUTES_TX     (1<<1)
} lm_client_con_params_t;

typedef struct _lm_packet_t
{
    /* Must be the first entry in this structure. */
    s_list_entry_t link;

    lm_status_t status;
    u32_t size;

    union _lm_pkt_info_t
    {
        struct _lm_tx_pkt_info_t
        {
            lm_coalesce_buffer_t *coalesce_buf;
            u16_t next_bd_idx;

            u16_t bd_used;
            u8_t span_pages;
            u8_t _pad1;
            u8_t  hdr_nbds;

            u16_t reserve;

            // TODO - Do we want this stuff ????
            #if DBG
            struct eth_tx_bd *dbg_start_bd;
            u16_t dbg_start_bd_idx;
            u16_t dbg_frag_cnt;
            #endif
        } tx;

        struct _lm_rx_pkt_info_t
        {
            u16_t next_bd_idx;

#define LM_MAX_SGES_FOR_PACKET 1 // TODO_QG rename to LM_MAX_FW_SGES_FOR_PACKET
            lm_address_t mem_phys[1+LM_MAX_SGES_FOR_PACKET]; // arrays content:
                                                             // bd ring address[0] + sge addresses[1] (optional)
                                                             // (currently one)
            u32_t*       hash_val_ptr;

#if DBG
            struct eth_rx_sge *dbg_sge;
            struct eth_rx_bd  *dbg_bd;
#endif
            union eth_sgl_or_raw_data sgl_or_raw_data; // currently used by OOO_CID. upper layer should handle endianity!
        } rx;
    } u1; // _lm_pkt_info_t

    lm_pkt_tx_info_t*         l2pkt_tx_info;
    lm_pkt_rx_info_t*         l2pkt_rx_info;

} lm_packet_t;

DECLARE_FRAG_LIST_BUFFER_TYPE(lm_packet_frag_list_t, MAX_FRAG_CNT);

/*******************************************************************************
 * Configurable parameters for the hardware dependent module.
 ******************************************************************************/

// I only want this enum for LLFC_TRAFFIC_TYPE_MAX value (should be HSI and fixed by FW)
typedef enum _driver_traafic_type_t
{
    LLFC_DRIVER_TRAFFIC_TYPE_NW         = 0,
    LLFC_DRIVER_TRAFFIC_TYPE_FCOE,
    LLFC_DRIVER_TRAFFIC_TYPE_ISCSI,
    LLFC_DRIVER_TRAFFIC_TYPE_MAX
}driver_traafic_type_t;
typedef struct _app_params_t
{
    u32_t enabled;
    u32_t traffic_type_priority[LLFC_DRIVER_TRAFFIC_TYPE_MAX];
}app_params_t;
//Cos DCBX params
#define E2_NUM_OF_COS                           (2)
typedef enum _dcbx_cos_params_strict
{
    DCBX_COS_NOT_STRICT         = 0,
    DCBX_COS_LOW_STRICT         = 1,
    DCBX_COS_HIGH_STRICT        = 2,
}dcbx_cos_params_strict;

typedef struct _dcbx_cos_params_t
{
    u32_t                   bw_tbl;
    u32_t                   pri_bitmask;
    dcbx_cos_params_strict  strict;
    u8_t                    pauseable;
}dcbx_cos_params_t;

typedef struct _pg_params_t
{
    u32_t                   enabled;
    #define LM_DCBX_ETS_IS_ENABLED(_pdev)       ((TRUE == IS_DCB_ENABLED(pdev)) && \
                                                 (TRUE == ((_pdev)->params.dcbx_port_params.ets.enabled)))
    u8_t                    num_of_cos; //valid COS entries
    dcbx_cos_params_t       cos_params[E2_NUM_OF_COS];
}pg_params_t;

typedef struct _pfc_params_t
{
    u32_t enabled;
    u32_t priority_non_pauseable_mask;
    #define DCBX_PFC_PRI_NON_PAUSE_MASK(_pdev)              (_pdev->params.dcbx_port_params.pfc.priority_non_pauseable_mask)
    #define DCBX_PFC_PRI_PAUSE_MASK(_pdev)                  ((u8_t)(~DCBX_PFC_PRI_NON_PAUSE_MASK(_pdev)))
    #define DCBX_PFC_PRI_MASK                               (0xFF)
    #define DCBX_PFC_PRI_GET_PAUSE(_pdev,_pg_pri)           (_pg_pri & DCBX_PFC_PRI_PAUSE_MASK(_pdev))
    #define DCBX_PFC_PRI_GET_NON_PAUSE(_pdev,_pg_pri)       (DCBX_PFC_PRI_NON_PAUSE_MASK(_pdev) & _pg_pri)
    #define IS_DCBX_PFC_PRI_ONLY_PAUSE(_pdev,_pg_pri)       (_pg_pri == DCBX_PFC_PRI_GET_PAUSE(_pdev,_pg_pri))
    #define IS_DCBX_PFC_PRI_ONLY_NON_PAUSE(_pdev,_pg_pri)   (_pg_pri == DCBX_PFC_PRI_GET_NON_PAUSE(_pdev,_pg_pri))
    #define IS_DCBX_PFC_PRI_MIX_PAUSE(_pdev,_pg_pri)        (!(IS_DCBX_PFC_PRI_ONLY_NON_PAUSE(_pdev,_pg_pri) || \
                                                               IS_DCBX_PFC_PRI_ONLY_PAUSE(_pdev,_pg_pri)))
}pfc_params_t;

typedef struct _dcbx_port_params_t
{
    u32_t dcbx_enabled;
    pfc_params_t pfc;
    pg_params_t  ets;
    app_params_t app;
}dcbx_port_params_t;


typedef enum
{
    DCBX_READ_LOCAL_MIB,
    DCBX_READ_REMOTE_MIB
}dcbx_read_mib_type;

typedef enum
{
    DCBX_UPDATE_TASK_STATE_FREE,
    DCBX_UPDATE_TASK_STATE_SCHEDULE,
    DCBX_UPDATE_TASK_STATE_HANDLED
} dcbx_update_task_state;

typedef enum
{
    LM_SINGLE_SM             = 0, /* default */
    LM_DOUBLE_SM_SINGLE_IGU  = 1,
    LM_DOUBLE_SM_DOUBLE_IGU  = 2
} fw_ndsb_type;

typedef struct _lm_params_t
{
    /* This value is used by the upper module to inform the protocol
     * of the maximum transmit/receive packet size.  Packet size
     * ranges from 1500-9600 bytes.  This value does not include ETH_PACKET_LEN, LLC-SNAP, VLAN tag, CRC32
     */
    u32_t mtu[LM_CLI_IDX_MAX];
    #define LM_MTU_INVALID_VALUE            (0xFFFFFFFF)
    u32_t mtu_max;

    #define MAX_CLI_PACKET_SIZE(pdev, chain_idx) ((u16_t)(pdev)->params.l2_cli_con_params[(chain_idx)].mtu + (pdev)->params.rcv_buffer_offset + ETHERNET_PACKET_HEADER_SIZE+ ETHERNET_VLAN_TAG_SIZE + ETHERNET_LLC_SNAP_SIZE + ETHERNET_CRC32_SIZE)
    #define CLI_MTU_WITH_ETH_HDR_SIZE(pdev, chain_idx) ((u16_t)(pdev)->params.l2_cli_con_params[(chain_idx)].mtu + ETHERNET_PACKET_HEADER_SIZE)
    #define MAX_L2_CLI_BUFFER_SIZE(pdev, chain_idx) ((MAX_CLI_PACKET_SIZE(pdev, chain_idx) + CACHE_LINE_SIZE_MASK) & \
                                       ~CACHE_LINE_SIZE_MASK)

    #define LM_MTU_NDIS_DEFAULT             (1500)
    #define LM_MTU_ISCSI_DEFAULT            (1500)
    #define LM_MTU_FCOE_DEFAULT             (2500)
    #define LM_MTU_FWD_DEFAULT              (LM_MTU_NDIS_DEFAULT)

    #define LM_MTU_FLOW_CTRL_TX_THR         (5000)
    #define LM_MTU_MAX_DEFAULT              (1500)
    #define LM_MTU_MAX                      (9600)
    /* Current node address.  The MAC address is initially set to the
     * hardware address.  This entry can be modified to allow the driver
     * to override the default MAC address.  The new MAC address takes
     * effect after a driver reset. */
    u8_t mac_addr[8];

    /* parameters for tx/rx chians.
       1 for all rss chains, and 1 more for each non-rss chain */
    u32_t l2_rx_desc_cnt[1+MAX_NON_RSS_CHAINS];
    u32_t l2_tx_bd_page_cnt[1+MAX_NON_RSS_CHAINS];
    u32_t l2_tx_coal_buf_cnt[1+MAX_NON_RSS_CHAINS];
    lm_client_con_params_t l2_cli_con_params[MAX_ETH_CONS];

    /* All the L2 receive buffers start at a cache line size aligned
     * address.  This value determines the location of the L2 frame header
     * from the beginning of the receive buffer. */
    u32_t rcv_buffer_offset;

    /* network type for defintion of max cwnd */
    u32_t network_type;
    #define LM_NETOWRK_TYPE_LAN                  0
    #define LM_NETOWRK_TYPE_WAN                  1
    #define LM_NETOWRK_TYPE_AUTO                 2 /* Linux only */
    u32_t max_cwnd_wan;
    u32_t max_cwnd_lan;

    u32_t cid_allocation_mode;
    #define LM_CID_ALLOC_REGULAR                 1
    #define LM_CID_ALLOC_DELAY                   2 /* delay cid allocation when there are no free cids but there are
                                                    * cids pending allocation */
    #define LM_CID_ALLOC_NUM_MODES               2


    u32_t ndsb_type;

    u32_t int_coalesing_mode;
    #define LM_INT_COAL_NONE                     0
    #define LM_INT_COAL_PERIODIC_SYNC            1 /* default */
    #define LM_INT_COAL_NUM_MODES                2
    u32_t int_per_sec_rx_override;
    u32_t int_per_sec_rx[HC_USTORM_SB_NUM_INDICES];
    u32_t int_per_sec_tx_override;
    u32_t int_per_sec_tx[HC_CSTORM_SB_NUM_INDICES];

    /* all protocols dynamic coalescing params */
    u32_t enable_dynamic_hc[HC_DHC_SB_NUM_INDICES];
    u32_t hc_timeout0[2][HC_DHC_SB_NUM_INDICES];
    u32_t hc_timeout1[2][HC_DHC_SB_NUM_INDICES];
    u32_t hc_timeout2[2][HC_DHC_SB_NUM_INDICES];
    u32_t hc_timeout3[2][HC_DHC_SB_NUM_INDICES];
    u32_t hc_threshold0[2];
    u32_t hc_threshold1[2];
    u32_t hc_threshold2[2];
    u32_t l2_dynamic_hc_min_bytes_per_packet;
    u32_t l4_hc_scaling_factor;

    u32_t l4_hc_ustorm_thresh;
    u32_t l4_scq_page_cnt;
    u32_t l4_rcq_page_cnt;
    u32_t l4_grq_page_cnt;

#if defined(_VBD_) || defined(_VBD_CMD_)
    #define NUM_BUFS_FOR_GRQS(pdev) \
                                    (pdev)->params.l4_grq_page_cnt*512*(LM_TOE_RSS_CHAIN_CNT(pdev))
#else
    #define NUM_BUFS_FOR_GRQS(pdev) \
                                  (pdev)->params.l4_grq_page_cnt*512*1
#endif
//    #define NUM_BUFS_FOR_GRQS(pdev)
//   (pdev)->params.l4_grq_page_cnt*512*(LM_TOE_RSS_CHAIN_CNT(pdev))

    u32_t l4_tx_chain_page_cnt;
    u32_t l4_rx_chain_page_cnt;
    u32_t l4_gen_buf_size;              /* minimum size of generic buffer */
    u32_t l4_history_cqe_cnt;           /* how much history to save       */

    /* DCA Related params */
    u32_t l4_ignore_grq_push_enabled; /* Configuration passed to fw whether to ignore push on grq or not */

    u32_t l4cli_flags;             /* such as LLC_SNAP*/
    u32_t l4cli_ticks_per_second;
    u32_t l4cli_ack_frequency;
    u32_t l4cli_delayed_ack_ticks;
    u32_t l4cli_max_retx;
    u32_t l4cli_doubt_reachability_retx;
    u32_t l4cli_sws_prevention_ticks;
    u32_t l4cli_dup_ack_threshold;
    u32_t l4cli_push_ticks;
    u32_t l4cli_nce_stale_ticks;
    u32_t l4cli_starting_ip_id;

    /* Various test/debug modes.  Any validation failure will cause the
     * driver to write to misc.swap_diag0 with the corresponding flag.
     * The intention is to trigger the bus analyzer. */
    // TODO - adjust to our needs
    u32_t test_mode;
    #define TEST_MODE_DISABLED                  0x00
    #define TEST_MODE_OBSOLETE_0                0x01    /* was TEST_MODE_IKOS */
    #define TEST_MODE_OBSOLETE_1                0x02    /* was TEST_MODE_FPGA */
    #define TEST_MODE_VERIFY_RX_CRC             0x10
    #define TEST_MODE_RX_BD_TAGGING             0x20
    #define TEST_MODE_TX_BD_TAGGING             0x40
    #define TEST_MODE_LOG_REG_ACCESS            0x80
    #define TEST_MODE_SAVE_DUMMY_DMA_DATA       0x0100
    #define TEST_MODE_INIT_GEN_BUF_DATA         0x0200
    #define TEST_MODE_DRIVER_PULSE_ALWAYS_ALIVE 0x0400
    #define TEST_MODE_IGNORE_SHMEM_SIGNATURE    0x0800
    #define TEST_MODE_NO_MCP                    0x1000

    lm_offload_t ofld_cap;
    lm_offload_t ofld_cap_to_ndis;

    lm_wake_up_mode_t wol_cap;

    lm_flow_control_t flow_ctrl_cap;
    lm_medium_t req_medium;

    u32_t selective_autoneg;
    #define SELECTIVE_AUTONEG_OFF                   0
    #define SELECTIVE_AUTONEG_SINGLE_SPEED          1
    #define SELECTIVE_AUTONEG_ENABLE_SLOWER_SPEEDS  2

    u32_t wire_speed;                           /* Not valid on SERDES. */

    /* Ways for the MAC to determine a link change. */
    u32_t phy_int_mode;
    #define PHY_INT_MODE_AUTO                   0
    #define PHY_INT_MODE_MI_INTERRUPT           1
    #define PHY_INT_MODE_LINK_READY             2
    #define PHY_INT_MODE_AUTO_POLLING           3

    /* Ways for the driver to get the link change event. */
    u32_t link_chng_mode;
    #define LINK_CHNG_MODE_AUTO                 0
    #define LINK_CHNG_MODE_USE_STATUS_REG       1
    #define LINK_CHNG_MODE_USE_STATUS_BLOCK     2

    /* Ways for the driver to determine which phy to prefer in case of dual media. */
    u32_t phy_priority_mode;
    #define PHY_PRIORITY_MODE_HW_DEF            0
    #define PHY_PRIORITY_MODE_10GBASET          1
    #define PHY_PRIORITY_MODE_SERDES            2
    #define PHY_PRIORITY_MODE_HW_PIN            3

    u32_t interrupt_mode; /* initialized by um to state whether we are using MSI-X or not, determined after we receive resources from OS */
    #define LM_INT_MODE_INTA 0
    #define LM_INT_MODE_SIMD 1 /* Single ISR / Multiple DPC */
    #define LM_INT_MODE_MIMD 2 /* Multiple ISR / Multple DPC */

    /* Relevant only for E2, and defines how the igu will be worked with (via GRC / BAR). Default will be set to BAR,
     * the defines for this are INTR_BLK_ACCESS_GRC, INTR_BLK_ACCESS_IGUMEM */
    u32_t igu_access_mode;

    u32_t sw_config;
    #define LM_SWCFG_1G                         0
    #define LM_SWCFG_10G                        1
    #define LM_SWCFG_AD                         2
    #define LM_SWCFG_OT_AD                      3
    #define LM_SWCFG_HW_DEF                     4

    u32_t mf_mode; //use enum mf_mode
    #define IS_MF_NIV_MODE(_pdev) (IS_MULTI_VNIC(_pdev) && ((_pdev)->params.mf_mode == MULTI_FUNCTION_NIV))
    #define IS_MF_SI_MODE(_pdev)  (IS_MULTI_VNIC(_pdev) && ((_pdev)->params.mf_mode == MULTI_FUNCTION_SI))
    #define IS_MF_SD_MODE(_pdev)  (IS_MULTI_VNIC(_pdev) && ((_pdev)->params.mf_mode == MULTI_FUNCTION_SD))

    u32_t tmr_reload_value1;

    u32_t max_func_connections;  // Number of connection supported by this function.
    /* TODO: init max_toe/max_rdma from somewhere else should come from licence info */
    u32_t max_supported_toe_cons;
    u32_t max_func_toe_cons;        // Number of TOE connections supported
    u32_t max_func_rdma_cons;       // Number of RDMA connections supported
    u32_t max_func_iscsi_cons;      // Number of iSCSI connections supported
    u32_t max_func_fcoe_cons;       // Number of FCoE connections supported
    u32_t max_fcoe_task;            // Number of FCoE max_fcoe_exchanges
    #define FCOE_MAX_TASK_SF           (4096)
    #define FCOE_MAX_TASK_MF           (2048)
    u32_t context_line_size; //Size of the context as configured in the CDU.
    u32_t context_waste_size; // Waste size as configured in the CDU.
    u32_t num_context_in_page;
    u32_t client_page_size; // Client memory page size.
    u32_t elt_page_size; // ELT page size.
    u32_t ilt_client_page_size; // ILT clients page size. We will give all client same page size. All ports as well.
    u32_t cfc_last_lcid; // number of supported connections in CFC - 1
    u32_t bandwidth_min; //The last value of min CMNG bandwidth configured by BACS
    u32_t bandwidth_max; //The last value of max CMNG bandwidth configured by BACS

    /* vnic parameters */
    /* Relative Function Number */
    u8_t pfunc_rel;
    #define PORT_ID_PARAM_FUNC_REL(_pfunc_rel)              ((_pfunc_rel) & 1)   //0 or 1
    #define PORT_ID_PARAM_FUNC_ABS(_chip_num, _port_mode, _pfunc_abs)   (lm_get_port_id_from_func_abs(_chip_num, _port_mode, _pfunc_abs))   //0 or 1
    #define PORT_ID(pdev)                                   (PORT_ID_PARAM_FUNC_REL(PFDEV(pdev)->params.pfunc_rel))   //0 or 1
    #define FUNC_ID(pdev)                                   (PFDEV(pdev)->params.pfunc_rel)       //0-7
    #define VNIC_ID_PARAM_FUNC_REL(_pfunc_rel)              ((_pfunc_rel) >> 1)  //0, 1, 2 or 3
    #define VNIC_ID(pdev)                                   (VNIC_ID_PARAM_FUNC_REL(PFDEV(pdev)->params.pfunc_rel))  //0, 1, 2 or 3
    #define LM_FOREACH_FUNC_IN_PORT(pdev, func) \
        for ((func) = PORT_ID(pdev); (func) < E1H_FUNC_MAX; (func)+=2)

    #define LM_FOREACH_ABS_FUNC_IN_PORT(pdev, func) \
        for ((func) = (CHIP_IS_E1x(pdev) ? PORT_ID(pdev) : PATH_ID(pdev)) ; (func) < E1H_FUNC_MAX; (func)+=2)


    #define FUNC_MAILBOX_ID_PARAM(_port,_vnic,_chip_num)    ((_port) + (_vnic) * (CHIP_IS_E1x_PARAM(_chip_num)? 2 : 1))
    #define FUNC_MAILBOX_ID(pdev)                           (FUNC_MAILBOX_ID_PARAM(PORT_ID(pdev) ,VNIC_ID(pdev),CHIP_NUM(pdev)))
    /* Absolute Function Number */
    u8_t pfunc_abs;
    #define ABS_FUNC_ID(pdev)    (PFDEV(pdev)->params.pfunc_abs)
    #define LM_FOREACH_FUNC_MAILBOX_IN_PORT(pdev, func) \
        for ((func) = PORT_ID(pdev); (func) < (CHIP_IS_E1x(pdev) ? E1H_FUNC_MAX : E2_FUNC_MAX); (func)+= (CHIP_IS_E1x(pdev) ? 2 : 1))
    u8_t path_id;
    #define PATH_ID(pdev)        (PFDEV(pdev)->params.path_id)

    u8_t vnics_per_port;  //1, 2 or 4
    u8_t multi_vnics_mode; //flag for multi function mode (can be set even if vnics_per_port==1)
    u8_t path_has_ovlan; // The multi function mode in the path (can be different than the mutli-function-mode of the function (4-port MF / SF mode E3 only)
    u8_t _pad[2];

    #define IS_MULTI_VNIC(pdev)       (PFDEV(pdev)->params.multi_vnics_mode)
    u16_t ovlan;  //vnic outer vlan

    /** 32-bit aligned **/
    // min max bw
    u8_t min_bw[MAX_VNIC_NUM];
    u8_t max_bw[MAX_VNIC_NUM];

    /* 32 bit aligned. */

    /* Status-Block-Related. Status blocks */
    u8_t sb_cnt;  //number of vnic's non-default status blocks (16, 8 or 4)
    #define LM_SB_CNT(pdev)  ((pdev)->params.sb_cnt)
    #define LM_NON_RSS_SB(pdev) (LM_MAX_RSS_CHAINS(pdev) - 1)
    #define LM_OOO_SB(pdev)     (LM_NON_RSS_SB(pdev))

    #define LM_SB_ID_VALID(pdev, sb_id) ((sb_id) < LM_SB_CNT(pdev))
    #define LM_FOREACH_SB_ID(pdev, sb_id)  \
        for ((sb_id) = 0; (sb_id) < LM_SB_CNT(pdev); (sb_id)++)

    #define LM_REST_OF_SB_ID(pdev, sb_id)  \
        for ((sb_id) = LM_SB_CNT(pdev); (sb_id) < MAX_RSS_CHAINS / pdev->params.vnics_per_port; (sb_id)++)

    u8_t base_fw_client_id;
    u8_t base_fw_ndsb;

    u8_t base_fw_stats_id; /* Where to collect statistics to */
    u8_t base_fw_qzone_id; /* Which qzone-id in the qzone-table is used for updating producers + dhc counters
                            * relevant for E2 only */

    u8_t base_cam_offset; /* Relevant only for VFs (FIXME: revisit... ) */

    u8_t vf_num_in_pf;
    u8_t vf_num_in_path;
    #define REL_VFID(_pdev) ((_pdev)->params.vf_num_in_pf)
    #define ABS_VFID(_pdev) ((_pdev)->params.vf_num_in_path)
    #define FW_VFID(_pdev) (8 + ABS_VFID((_pdev)))
    /* 32 bit aligned. */
    u32_t debug_me_register;

    /* cam/mac parameters (see lm_init_cam_params) */
    u16_t base_offset_in_cam_table;
    #define BASE_OFFSET_IN_CAM_TABLE(_pdev) (_pdev)->params.base_offset_in_cam_table

    u16_t cam_size;
    #define LM_CAM_SIZE(pdev)                           ((pdev)->params.cam_size)

    u16_t mc_table_size[LM_CLI_IDX_MAX];
    #define LM_MC_TABLE_SIZE(pdev,lm_client_idx)        ((pdev)->params.mc_table_size[lm_client_idx])

    u16_t uc_table_size[LM_CLI_IDX_MAX];
    #define LM_UC_TABLE_SIZE(pdev,lm_client_idx)        ((pdev)->params.uc_table_size[lm_client_idx])

    #define LM_MC_NDIS_TABLE_SIZE    (64)
    #define LM_MC_FCOE_TABLE_SIZE    (2)

    #define LM_MAX_MC_TABLE_SIZE     (LM_MC_NDIS_TABLE_SIZE + LM_MC_FCOE_TABLE_SIZE)
    #define LM_KEEP_CURRENT_CAM_VALUE (0xFFFF)
    #define LM_INVALID_CAM_BASE_IDX                 (0xFF)

    u8_t rss_caps;              /* rss hash calculation types supported */
    #define LM_RSS_CAP_IPV4     1
    #define LM_RSS_CAP_IPV6     2

    u8_t rss_chain_cnt;         /* number of rss chains. lm wise, if rss_chain_cnt==1 then rss is disabled */
    u8_t tss_chain_cnt;         /* number of tss chains. should be identical to rss_chain_cnt. */

    /* TODO FIX MAX RSS Chains with new HC SB management*/
    u8_t max_rss_chains;
    #define LM_MAX_RSS_CHAINS(pdev) (pdev)->params.max_rss_chains

    /** 32-bit aligned *   */
    /* for registry */
    u32_t override_rss_chain_cnt; /* value for overriding configured rss_chain_cnt */

    #define RSS_ID_TO_SB_ID(_rss_id)                (_rss_id) /* Mapping between rss-id to sb-id */
    #define RSS_ID_TO_CID(_rss_id)                  (_rss_id) /* Mapping between rss-id to cid */
    #define TSS_ID_TO_CID(_tss_id)                  (_tss_id) /* Mapping between rss-id to cid */
    #define CID_TO_RSS_ID(_cid)                     (_cid)    /* Mapping between rss-id to cid */

    #define LM_CLI_RX_FILTER_MASK(pdev, cid)       (1 << LM_FW_CLI_ID(pdev, cid))

    #define LM_RX_FILTER_ALL_MASK(pdev, ret_val) \
    { \
        ret_val |= LM_CLI_RX_FILTER_MASK((pdev), NDIS_CID(pdev)); \
        ret_val |= LM_CLI_RX_FILTER_MASK((pdev), ISCSI_CID(pdev));\
        ret_val |= LM_CLI_RX_FILTER_MASK((pdev), RDMA_CID(pdev)); \
        ret_val |= LM_CLI_RX_FILTER_MASK((pdev), FCOE_CID(pdev)); \
    }

    #define LM_SW_LEADING_SB_ID                     0
    #define LM_SW_LEADING_RSS_CID(pdev)             0

    #define LM_INVALID_ETH_CID              (0xFF)

    u8_t map_client_to_cid[LM_CLI_IDX_MAX];
    #define NDIS_CID(_pdev)                         (_pdev)->params.map_client_to_cid[LM_CLI_IDX_NDIS]
    #define ISCSI_CID(_pdev)                        (_pdev)->params.map_client_to_cid[LM_CLI_IDX_ISCSI]
    #define FCOE_CID(_pdev)                         (_pdev)->params.map_client_to_cid[LM_CLI_IDX_FCOE]
    #define RDMA_CID(_pdev)                         (_pdev)->params.map_client_to_cid[LM_CLI_IDX_RDMA]
    #define FWD_CID(_pdev)                          (_pdev)->params.map_client_to_cid[LM_CLI_IDX_FWD]
    #define OOO_CID(_pdev)                          (_pdev)->params.map_client_to_cid[LM_CLI_IDX_OOO]

    #define LM_CLI_CID(_pdev, lm_cli_idx)           ((_pdev)->params.map_client_to_cid[lm_cli_idx])

    #define LM_CHAIN_IDX_CLI(pdev, cid) (cid < LM_MAX_RSS_CHAINS(pdev) ? LM_CLI_IDX_NDIS   :   \
                                        ((cid == RDMA_CID(pdev)        ? LM_CLI_IDX_RDMA   :   \
                                        ((cid == ISCSI_CID(pdev)       ? LM_CLI_IDX_ISCSI  :   \
                                        ((cid == FCOE_CID(pdev)        ? LM_CLI_IDX_FCOE   :   \
                                        ((cid == FWD_CID(pdev)         ? LM_CLI_IDX_FWD    :   \
                                        ((cid == OOO_CID(pdev)         ? LM_CLI_IDX_OOO    :   \
                                                                         LM_CLI_IDX_MAX)))))))))))


    #define LM_CHAIN_IDX_TRAFFIC_TYPE(pdev, cid)    (cid < LM_MAX_RSS_CHAINS(pdev)  ? LLFC_TRAFFIC_TYPE_NW   :   \
                                                    ((cid == RDMA_CID(pdev)  ? LLFC_DRIVER_TRAFFIC_TYPE_MAX   :   \
                                                    ((cid == ISCSI_CID(pdev) ? LLFC_TRAFFIC_TYPE_ISCSI  :   \
                                                    ((cid == FCOE_CID(pdev)  ? LLFC_TRAFFIC_TYPE_FCOE   :   \
                                                    ((cid == FWD_CID(pdev)   ? LLFC_TRAFFIC_TYPE_NW    :   \
                                                    LLFC_DRIVER_TRAFFIC_TYPE_MAX)))))))))

    #define LM_FW_CLI_ID(pdev, cid)  (pdev->params.base_fw_client_id + cid)

    /* A bit about E2 Qzone-IDs: qzone is a new area in internal memory where the FW stores producers + dynamic-host-coalesing (dhc) values.
     * It is a separate area than areas the have arrays for clients / status-blocks. Technically, the driver can decide to have separate entries
     * for producers + dhc entries (it has to do with permissions in PXP for VFs..., for now there is no reason to do this. And we'll use the same
     * id, but note that QZONE_ID is intended for fp ring producers. DHC_QZONE_ID is intended for status-block, and thus the parameter they receive.
     */
    #define LM_FW_QZONE_ID(pdev, cid)        (pdev->params.base_fw_qzone_id + cid)
    #define LM_FW_DHC_QZONE_ID(pdev, sb_id)  (pdev->params.base_fw_qzone_id + sb_id)
    #define LM_FW_SB_ID(pdev, sb_id) ((sb_id == DEF_STATUS_BLOCK_INDEX)? DEF_STATUS_BLOCK_INDEX: pdev->params.base_fw_ndsb + sb_id)

    #define LM_CLIENT_BIT_VECTOR(pdev, lm_cli_idx)  (1 << (LM_FW_CLI_ID(pdev, LM_CLI_CID(pdev, lm_cli_idx))))
    #define LM_CID_BIT_VECTOR(pdev, cid)            (1 << (LM_FW_CLI_ID(pdev, cid)))


    /* 'for loop' macros on rss/tss chains  */
    #define LM_FOREACH_RSS_IDX(pdev, rss_idx)  \
        for ((rss_idx) = 0; (rss_idx) < (pdev)->params.rss_chain_cnt; (rss_idx)++)
    #define LM_FOREACH_TSS_IDX(pdev, tss_idx)  \
        for ((tss_idx) = 0; (tss_idx) < (pdev)->params.tss_chain_cnt; (tss_idx)++)
    #define LM_FOREACH_RSS_IDX_SKIP_LEADING(pdev, rss_idx)  \
        for ((rss_idx) = 1; (u8_t)(rss_idx) < (pdev)->params.rss_chain_cnt; (rss_idx)++)
    #define LM_FOREACH_TSS_IDX_SKIP_LEADING(pdev, tss_idx)  \
        for ((tss_idx) = 1; (u8_t)(tss_idx) < (pdev)->params.tss_chain_cnt; (tss_idx)++)


    /* L4 RSS */
    u8_t l4_rss_chain_cnt;         /* number of L4 rss chains. lm wise, if rss_chain_cnt==1 then rss is disabled */
    u8_t l4_tss_chain_cnt;         /* number of L4 tss chains. */
    u8_t l4_rss_base_chain_idx;    /* L4 rss base chain Where do the L4 status block start */
    u8_t l4_base_fw_rss_id;        /* L4 rss base chain Where do the L4 status block start */

    #define LM_TOE_BASE_RSS_ID(pdev)            ((pdev)->params.l4_rss_base_chain_idx)   /* that is first L4 SB */
    #define LM_TOE_FW_RSS_ID(pdev, rss_id)      ((pdev)->params.l4_base_fw_rss_id + rss_id)    /* that is first L4 SB */
    #define LM_TOE_RSS_CHAIN_CNT(pdev)                  ((pdev)->params.l4_rss_chain_cnt)
    #define LM_TOE_TSS_CHAIN_CNT(pdev)                  ((pdev)->params.l4_tss_chain_cnt)


    /* 'for loop' macros on L4 rss/tss chains  */
    #define LM_TOE_FOREACH_RSS_IDX(pdev, rss_idx)  \
        for ((rss_idx) = (pdev)->params.l4_rss_base_chain_idx; (rss_idx) < (pdev)->params.l4_rss_base_chain_idx + (pdev)->params.l4_rss_chain_cnt; (rss_idx)++)
    #define LM_TOE_FOREACH_TSS_IDX(pdev, tss_idx)  \
        for ((tss_idx) = (pdev)->params.l4_rss_base_chain_idx; (tss_idx) < (pdev)->params.l4_rss_base_chain_idx + (pdev)->params.l4_tss_chain_cnt; (tss_idx)++)

    /* for multi function mode, when 'rss_base_chain_idx' != 0 */
    // In new VBD dsign chain doesn't equal client and
    // we must add client offset
    //((pdev)->params.base_fw_client_id + (val))
    #define LM_CHAIN_TO_FW_CLIENT(_pdev, _chain)   ((_pdev)->params.base_fw_client_id + (_chain))

    // eth configuration.
    u32_t keep_vlan_tag;

    u16_t eth_align_enable;

    // TODO: encapsulate in a connection object
    u32_t update_comp_cnt;
    u32_t update_suspend_cnt;
    u32_t update_toe_comp_cnt;

    lm_address_t dmae_copy_scratchpad_phys;

    // congestion managment parameters
    u32_t cmng_enable;
    u32_t cmng_rate_shaping_enable;
    u32_t cmng_fairness_enable;
    // safc
    u32_t cmng_safc_rate_thresh;
    u32_t cmng_activate_safc;
    // fairness
    u32_t cmng_fair_port0_rate;
    u32_t cmng_eth_weight;
    u32_t cmng_toe_weight;
    u32_t cmng_rdma_weight;
    u32_t cmng_iscsi_weight;
    // rate shaping
    u32_t cmng_eth_rate;
    u32_t cmng_toe_rate;
    u32_t cmng_rdma_rate;
    u32_t cmng_iscsi_rate;
    // Demo will be removed later
    u32_t cmng_toe_con_number;
    u32_t cmng_rdma_con_number;
    u32_t cmng_iscsi_con_number;
    // iscsi
    u32_t l5sc_max_pending_tasks;

    // cls_params
    struct elink_params link;

    // fw flow control
    u32_t l2_fw_flow_ctrl;
    u32_t l4_fw_flow_ctrl;

    // preemphasis rx/tx configuration
    u32_t preemphasis_enable;

    u32_t preemphasis_rx_0;
    u32_t preemphasis_rx_1;
    u32_t preemphasis_rx_2;
    u32_t preemphasis_rx_3;

    u32_t preemphasis_tx_0;
    u32_t preemphasis_tx_1;
    u32_t preemphasis_tx_2;
    u32_t preemphasis_tx_3;
    u32_t l4_rss_enabled_by_os;
    u32_t disable_patent_using;
    u32_t l4_grq_filling_threshold_divider;
    u32_t l4_free_cid_delay_time;
    u32_t l4_enable_rss;
    u32_t l4_rss_is_possible;
        #define L4_RSS_DISABLED 0       /* shmulikr: l4_enable_rss is more then a flag. The various values represent the possible flavors           */
        #define L4_RSS_DYNAMIC  1       /* Full support including support for indirection table update */
    u32_t l4_max_rcv_wnd_size;
    /* disable PCIe non-FATAL error reporting */
    u32_t disable_pcie_nfr;

    u32_t mf_proto_support_flags; /* For multi-function: which protocols are supported */
    #define LM_PROTO_SUPPORT_ETHERNET  0x1
    #define LM_PROTO_SUPPORT_ISCSI     0x2
    #define LM_PROTO_SUPPORT_FCOE      0x4

    /* In release this flag will prevent us from crashing in customer site */
    u32_t debug_cap_flags;
#if DBG
#define DEFAULT_DEBUG_CAP_FLAGS_VAL     0xffffffff
#else
#define DEFAULT_DEBUG_CAP_FLAGS_VAL     0x0
#endif

#define DEBUG_CAP_FLAGS_STATS_FW        0x1
//#define DEBUG_CAP_FLAGS_XXX           0x2

    /* Enable Everest2 FW integration testing mode + with default settings - taken from registry */
    u32_t e2_integ_testing_enabled;

    u32_t e2_integ_def_cos_setting_0;
    u32_t e2_integ_def_pbfq_setting_0;
    u32_t e2_integ_def_voq_setting_0;
    u32_t e2_integ_def_priority_setting_0;
    u32_t e2_integ_def_flags_0;

    u32_t e2_integ_def_cos_setting_1;
    u32_t e2_integ_def_pbfq_setting_1;
    u32_t e2_integ_def_voq_setting_1;
    u32_t e2_integ_def_priority_setting_1;
    u32_t e2_integ_def_flags_1;

    /* Enable VFC testing mode. Each packet is compared agains both VFC and CAM */
    u32_t e2_integ_vfc_test_en;
    u32_t l4_limit_isles;
#define L4_LI_NOTIFY                        0x0001
#define L4_LI_MAX_GEN_BUFS_IN_ISLE          0x0002
#define L4_LI_MAX_GEN_BUFS_IN_ARCHIPELAGO   0x0004

    u32_t l4_max_gen_bufs_in_isle;
    u32_t l4_max_gen_bufs_in_archipelago;
    u32_t l4_valid_gen_bufs_in_archipelago;
    u32_t l4_max_gen_buf_cnt;      /* maximum number of generic buffers the system can allocate, duplicated from UM*/

    u32_t l4_isles_pool_size;

    u8_t  l4_num_of_blocks_per_connection;
    // PF_FLR
    u8_t  is_flr;
    u8_t  phy_hw_lock_required;
    u8_t  __nmb_pad[1];
    //LLFC should be moved to vars
    dcbx_port_params_t    dcbx_port_params;

    config_lldp_params_t  lldp_config_params;
    config_dcbx_params_t  dcbx_config_params;
    u32_t try_not_align_page_multiplied_memory;

    /* Virtualization related */
    u8_t    device_type;
    u8_t    virtualization_type;

    u8_t fw_stats_init_value;
    u8_t int_coalesing_mode_disabled_by_ndis;
    u32_t l4_dominance_threshold;   /*for firmware debug.*/
    u32_t l4_max_dominance_value;   /* set to 0 to disable dominant connection, set to 20 (default) to enable */

    u32_t l4_data_integrity;
    u32_t l4_start_port;
    u32_t l4_num_of_ports;
    u32_t l4_skip_start_bytes;
    u32_t l4_support_pending_sp_req_complete;
    u32_t l4_support_upload_req_on_abortive_disconnect;

    u32_t grc_timeout_max_ignore ;

    /* Error Recovery supported only on E2. Can be controlled via registry */
    u32_t enable_error_recovery;
    u32_t validate_sq_complete;

    /* Enables switching between non-enlighted vms under npar configuration. 
     * vm's that don't have their mac in the tx cam can't be 'switched' between pfs
     * this mode actually means that all traffic will be passed on loopback channel if
     * there is a pf in promiscuous/accept unmatched (which is set when there are vms)
     * this feature hurts performance and therefore can be disabled */
    u32_t npar_vm_switching_enable;

} lm_params_t;



/*******************************************************************************
 * Device NVM info -- The native strapping does not support the new parts, the
 *                    software needs to reconfigure for them.
 ******************************************************************************/
//TODO we need check
typedef struct _flash_spec_t
{
    u32_t page_size;
    u32_t total_size;
} flash_spec_t;

//TODO resolve big endian issues
typedef struct _lm_cam_entry_t
{
    u8_t cam_addr[ETHERNET_ADDRESS_SIZE];
    u16_t ref_cnt;
} lm_cam_entry_t;


#define MAX_MAC_OFFSET_IN_NIG 16
#define ISCSI_OFFSET_IN_NIG   15 /* Iscsi mac must have a place in the nig, otherwise classification
                                    will fail and packet won't get to the offloaded connection.
                                    We save the last one for iscsi... */

typedef struct _lm_nig_mirror_entry_t
{
    s32_t refcnt; //signed to detect underflow.

    //atomic access is not needed because this struct is modified under TOE_LOCK.
#define NIG_ENTRY_INC_REFCNT(_entry) ++(_entry)->refcnt
#define NIG_ENTRY_DEC_REFCNT(_entry) {--(_entry)->refcnt; DbgBreakIf((_entry)->refcnt < 0);}

    u8_t addr[ETHERNET_ADDRESS_SIZE]; //MAC address of this entry.
}lm_nig_mirror_entry_t;

typedef struct _lm_nig_mirror_t
{
    lm_nig_mirror_entry_t entries[MAX_MAC_OFFSET_IN_NIG];
}lm_nig_mirror_t;


/*******************************************************************************
 * Device info.
 ******************************************************************************/

/* multi function specific */
typedef struct _lm_hardware_mf_info_t
{
    u32_t func_mf_cfg;
    #define NIV_FUNCTION_ENABLED(_pdev) (GET_FLAGS((_pdev)->hw_info.mf_info.func_mf_cfg, FUNC_MF_CFG_FUNC_DISABLED|FUNC_MF_CFG_FUNC_DELETED)==0)

    u8_t vnics_per_port;  //1, 2 or 4
    u8_t multi_vnics_mode;
    u8_t path_has_ovlan; /* the multi function mode of the path... */

    u8_t min_bw[MAX_VNIC_NUM];
    u8_t max_bw[MAX_VNIC_NUM];

    u16_t ext_id;  //vnic outer vlan or VIF ID
    #define VALID_OVLAN(ovlan) ((ovlan) <= 4096)
    #define INVALID_VIF_ID 0xFFFF
    #define OVLAN(_pdev) ((_pdev)->hw_info.mf_info.ext_id)
    #define VIF_ID(_pdev) ((_pdev)->hw_info.mf_info.ext_id)

    u16_t default_vlan;
    #define NIV_DEFAULT_VLAN(_pdev) ((_pdev)->hw_info.mf_info.default_vlan)

    u8_t niv_priority;
    #define NIV_PRIORITY(_pdev) ((_pdev)->hw_info.mf_info.niv_priority)

    u16_t flags;
    #define MF_INFO_VALID_MAC       0x0001

    u32_t mf_mode; /* Switch-dependent / Switch-Independent */

} lm_hardware_mf_info_t;


/* IGU related params for status-blocks */
typedef struct _lm_igu_info_t {
    u8_t igu_base_sb; /* base for all ndsb u + c */
    #define IGU_BASE_NDSB(pdev) ((pdev)->hw_info.intr_blk_info.igu_info.igu_base_sb)
    u8_t igu_sb_cnt;
    #define LM_IGU_SB_CNT(pdev)  ((pdev)->hw_info.intr_blk_info.igu_info.igu_sb_cnt)
    u8_t igu_dsb_id;
    #define IGU_DSB_ID(pdev) ((pdev)->hw_info.intr_blk_info.igu_info.igu_dsb_id)
    u8_t igu_u_sb_offset;
    #define IGU_U_NDSB_OFFSET(pdev) ((pdev)->hw_info.intr_blk_info.igu_info.igu_u_sb_offset)
    u8_t igu_func_id;
    #define IGU_FUNC_ID(pdev) ((pdev)->hw_info.intr_blk_info.igu_info.igu_func_id)
    u8_t igu_test_sb_cnt;
    u8_t _pad[2];
} lm_igu_info_t;

typedef struct _lm_intr_blk_info_t
{
    u8_t blk_type;
    #define INTR_BLK_HC  0
    #define INTR_BLK_IGU 1
    #define INTR_BLK_TYPE(_pdev) ((_pdev)->hw_info.intr_blk_info.blk_type)

    u8_t blk_mode;
    #define INTR_BLK_MODE_BC   0
    #define INTR_BLK_MODE_NORM 1
    #define INTR_BLK_MODE(_pdev) ((_pdev)->hw_info.intr_blk_info.blk_mode)

    u8_t access_type;
    #define INTR_BLK_ACCESS_GRC 0
    #define INTR_BLK_ACCESS_IGUMEM 1
    #define INTR_BLK_ACCESS(_pdev) ((_pdev)->hw_info.intr_blk_info.access_type)

    u32_t simd_addr_wmask;
    #define INTR_BLK_SIMD_ADDR_WMASK(_pdev) ((_pdev)->hw_info.intr_blk_info.simd_addr_wmask)

    u32_t simd_addr_womask;
    #define INTR_BLK_SIMD_ADDR_WOMASK(_pdev) ((_pdev)->hw_info.intr_blk_info.simd_addr_womask)

    u32_t cmd_ctrl_rd_wmask;
    u32_t cmd_ctrl_rd_womask;
    #define INTR_BLK_CMD_CTRL_INVALID 0
    #define INTR_BLK_REQUIRE_CMD_CTRL(_pdev) ((_pdev)->hw_info.intr_blk_info.cmd_ctrl_rd_wmask != INTR_BLK_CMD_CTRL_INVALID)
    #define INTR_BLK_CMD_CTRL_RD_WMASK(_pdev) ((_pdev)->hw_info.intr_blk_info.cmd_ctrl_rd_wmask)
    #define INTR_BLK_CMD_CTRL_RD_WOMASK(_pdev) ((_pdev)->hw_info.intr_blk_info.cmd_ctrl_rd_womask)

    /* IGU specific data */
    lm_igu_info_t igu_info;

} lm_intr_blk_info_t;

typedef struct _lm_sriov_info_t {
//    #define MAX_VF_BAR 3 Fix it when emulation supports 3 bars
    #define MAX_VF_BAR 2
    u16_t total_vfs; /* maximum allowed vfs      */
    u16_t vf_device_id;
    u32_t first_vf_in_pf;
    u32_t vf_bar_size[MAX_VF_BAR];
    lm_address_t vf_bars[MAX_VF_BAR];
} lm_sriov_info_t;


typedef enum
{
    LM_CHIP_PORT_MODE_NONE = 0x0,
    LM_CHIP_PORT_MODE_2    = 0x1,
    LM_CHIP_PORT_MODE_4    = 0x2
} lm_chip_port_mode_t ;

typedef struct _lm_hardware_info_t
{
    /* PCI info. */
    u16_t vid;
    u16_t did;
    u16_t ssid;
    u16_t svid;

    u8_t irq;
    u8_t int_pin;
    u8_t latency_timer;
    u8_t cache_line_size;
    u8_t rev_id;
    u8_t _pad[3];

    lm_address_t mem_base[MAX_NUM_BAR];
    u32_t bar_size[MAX_NUM_BAR];

    lm_address_t mem_base1;
    u32_t bar_size1;

    /* Device info. */
    u32_t phy_id;                       /* (phy_reg2 << 16) | phy_reg3 */
    u8_t mac_addr[8];                   /* Hardware MAC address. */
    u8_t iscsi_mac_addr[8];             /* Hardware MAC address for iSCSI. */
    u8_t fcoe_mac_addr[8];              /* Hardware MAC address for FCoE. */
    u8_t fcoe_wwn_port_name[8];         /* Hardware MAC address for FCoE WWPN. */
    u8_t fcoe_wwn_node_name[8];         /* Hardware MAC address for FCoE WWNN. */
    
    u32_t shmem_base;                   /* Firmware share memory   base addr. */
    u32_t mf_cfg_base;                  /* MF cfg offset in shmem_base        */
    u32_t shmem_base2;                  /* Firmware share memory 2 base addr. */

    u32_t chip_id;                      /* chip num:16-31, rev:12-15, metal:4-11, bond_id:0-3 */
    #define CHIP_NUM_SET(_chip_id,_p)   ((_chip_id) = (((_p) & 0xffff) << 16))
    #define CHIP_NUM(_p)                (((_p)->hw_info.chip_id) & 0xffff0000)
    #define CHIP_NUM_5710               0x164e0000
    #define CHIP_NUM_5711               0x164f0000
    #define CHIP_NUM_5711E              0x16500000
    #define CHIP_NUM_5712               0x16620000
    #define CHIP_NUM_5712E              0x16630000
    #define CHIP_NUM_5713               0x16510000
    #define CHIP_NUM_5713E              0x16520000
    #define CHIP_NUM_57800              0x168a0000
    #define CHIP_NUM_57840              0x168d0000
    #define CHIP_NUM_57810              0x168e0000


    #define CHIP_IS_E1_PARAM(_chip_num)     ((_chip_num) == CHIP_NUM_5710)
    #define CHIP_IS_E1(_p)                  (CHIP_IS_E1_PARAM(CHIP_NUM(_p)))

    #define CHIP_IS_E1H_PARAM(_chip_num)    (((_chip_num) == CHIP_NUM_5711) || ((_chip_num) == CHIP_NUM_5711E))
    #define CHIP_IS_E1H(_p)                 (CHIP_IS_E1H_PARAM(CHIP_NUM(_p)))

    #define CHIP_IS_E1x_PARAM(_chip_num)    (CHIP_IS_E1_PARAM(((_chip_num))) || CHIP_IS_E1H_PARAM(((_chip_num))))
    #define CHIP_IS_E1x(_p)                 (CHIP_IS_E1x_PARAM(CHIP_NUM(_p)))

    #define CHIP_IS_E2_PARAM(_chip_num)     (((_chip_num) == CHIP_NUM_5712) || ((_chip_num) == CHIP_NUM_5713) || \
                                             ((_chip_num) == CHIP_NUM_5712E) || ((_chip_num) == CHIP_NUM_5713E))

    #define CHIP_IS_E2(_p)                  (CHIP_IS_E2_PARAM(CHIP_NUM(_p)))

    #define CHIP_IS_E3_PARAM(_chip_num)     ((_chip_num == CHIP_NUM_57800) || (_chip_num == CHIP_NUM_57810) || \
                                             (_chip_num == CHIP_NUM_57840))

    #define CHIP_IS_E3(_p)                  (CHIP_IS_E3_PARAM(CHIP_NUM(_p)))

    #define CHIP_IS_E2E3(_p)                (CHIP_IS_E2(_p) || (CHIP_IS_E3(_p)))

    #define CHIP_REV(_p)                (((_p)->hw_info.chip_id) & 0x0000f000)
    #define CHIP_REV_Ax                 0x00000000
    #define CHIP_REV_Bx                 0x00001000
    #define CHIP_REV_Cx                 0x00002000
    #define CHIP_REV_FPGA               0x0000f000
    #define CHIP_REV_IKOS               0x0000d000
    #define CHIP_REV_EMUL               0x0000e000

    #define CHIP_REV_IS_ASIC(_p)        ((CHIP_REV(_p) != CHIP_REV_EMUL) && (CHIP_REV(_p) != CHIP_REV_FPGA))
    #define CHIP_REV_IS_FPGA(_p)        (CHIP_REV(_p) == CHIP_REV_FPGA)
    #define CHIP_REV_IS_EMUL(_p)        (CHIP_REV(_p) == CHIP_REV_EMUL)
    #define CHIP_REV_IS_SLOW(_p)        (CHIP_REV(_p) > 0x00005000)

    #define CHIP_METAL(_p)              (((_p)->hw_info.chip_id) & 0x00000ff0)
    #define CHIP_BONDING(_p)            (((_p)->hw_info.chip_id) & 0x0000000f)

    #define CHIP_ID(_p)                 (((_p)->hw_info.chip_id) & 0xfffffff0)
    #define CHIP_ID_5706_A0             0x57060000
    #define CHIP_ID_5706_A1             0x57060010
    #define CHIP_ID_5706_FPGA           0x5706f000
    #define CHIP_ID_5706_IKOS           0x5706e000
    #define CHIP_ID_5708_A0             0x57080000
    #define CHIP_ID_5708_B0             0x57081000
    #define CHIP_ID_5708_FPGA           0x5708f000
    #define CHIP_ID_5708_IKOS           0x5708e000
    #define CHIP_ID_5710_EMUL           0X164ed000
    #define CHIP_ID_5710_A0             0x164e0000
    #define CHIP_ID_5710_A1             0x164e0010

    #define IS_CHIP_REV_A0(_p)          (CHIP_ID(_p) == CHIP_ID_5710_A0)
    #define IS_CHIP_REV_A1(_p)          (CHIP_ID(_p) == CHIP_ID_5710_A1)

    #define CHIP_BOND_ID(_p)            (((_p)->hw_info.chip_id) & 0xf)

    /* A serdes chip will have the first bit of the bond id set. */
    #define CHIP_BOND_ID_SERDES_BIT     0x01

    u8_t silent_chip_rev;                           /* silent chip rev:
                                                                              For 57711 0-A0, 1-A1 2-A2
                                                                              For 57710 0-A1  1-A2 */
    #define SILENT_CHIP_REV(_p)             ((_p)->hw_info.silent_chip_rev)
    #define SILENT_REV_E1_A0                        0xFF
    #define SILENT_REV_E1_A1                0x00
    #define SILENT_REV_E1_A2                        0x01

    #define SILENT_REV_E1H_A0               0x00
    #define SILENT_REV_E1H_A1               0x01
    #define SILENT_REV_E1H_A2               0x02

    /* In E2, the chip can be configured in 2-port mode  (i.e. 1 port per path) or 4-port mode (i.e. 2 port per path)
     * the driver needs this information since it needs to configure several blocks accordingly */
    lm_chip_port_mode_t chip_port_mode;
    #define CHIP_PORT_MODE(_p)              ((_p)->hw_info.chip_port_mode)

    /* HW config from nvram. */
    u32_t nvm_hw_config;
    u32_t nvm_hw_config2;

    /* board sn*/
    u8_t  board_num[16];

    /* Flash info. */
    flash_spec_t flash_spec;

    /* Needed for pxp config should be done by the MCP*/
    u8_t max_payload_size;
    u8_t max_read_req_size;

    u8_t mcp_detected;

    // external phy fw version
    u8_t sz_ext_phy_fw_ver[16];// NULL terminated string populated only after a call to get ext phy fw version

    // link config
    u32_t link_config[ELINK_LINK_CONFIG_SIZE];

    // initial dual phy priority config
    u32_t multi_phy_config;

    u32_t phy_force_kr_enabler; // read from shmem

    u8_t  phy_no_10g_support; // TRUE if 10g is not supported by hardware congiuration.

    // pcie info
    u8_t pcie_lane_width;
    #define PCIE_WIDTH_1                1
    #define PCIE_WIDTH_2                2
    #define PCIE_WIDTH_4                4
    #define PCIE_WIDTH_8                8
    #define PCIE_WIDTH_16               16
    #define PCIE_WIDTH_32               32

    u8_t pcie_lane_speed;
    #define PCIE_LANE_SPEED_2_5G        1
    #define PCIE_LANE_SPEED_5G          2

    // In E2 chip rev A0 the PCI LANE speed are different (ERR 8)
    #define PCIE_LANE_SPEED_2_5G_E2_A0  0
    #define PCIE_LANE_SPEED_5G_E2_A0    1

    // mba features
    u8_t mba_features;

    // port_feature_config bits
    u32_t port_feature_config;

    // mba vlan enable bits
    u32_t mba_vlan_cfg ;

    // TRUE if dcc is active
    u8_t is_dcc_active;

    // bc rev
    u32_t bc_rev;
    // ther driver should not load with bc less then the following
    #define BC_REV_SUPPORTED            0x040200 //4.2.0

    /* HW Licensing of Max #connections for each protocol, takes into account bar-size, licensing is 'per-port' and not 'per functions' */
    u32_t max_port_toe_conn;
    u32_t max_port_rdma_conn;
    u32_t max_port_iscsi_conn;
    u32_t max_port_fcoe_conn;
    u32_t max_port_conns; /* the maximum number of connections support for this port, used to configure PORT registers */
    u32_t max_common_conns; /* the maximum number of connections support for ALL ports, used to configure COMMON registers, only used by PORT-MASTER */

    lm_hardware_mf_info_t mf_info;

    /* Information on interrupt block are we working with - HC or IGU (E1/E1H or E2 and above) */
    lm_intr_blk_info_t intr_blk_info;

    lm_sriov_info_t sriov_info;
} lm_hardware_info_t;



//this struct encapsulates both the default status block as well as the RSS status blocks.
typedef struct _gen_sp_status_block_t
{
    /*physical address of the status block.*/
    lm_address_t blk_phy_address;
    struct hc_sp_status_block_data sb_data;
    volatile struct host_sp_status_block * hc_sp_status_blk;
} gen_sp_status_block_t;

//this struct encapsulates both the default status block as well as the RSS status blocks.
typedef struct _gen_status_block_t
{
    union {
        struct hc_status_block_data_e1x    e1x_sb_data;
        struct hc_status_block_data_e2     e2_sb_data;
        lm_address_t vf_sb_phy_address;
    } hc_status_block_data;

    union {
        /*pointer to default status block */
        volatile struct host_hc_status_block_e1x * e1x_sb;
        /*pointer to RSS status block   */
        volatile struct host_hc_status_block_e2  * e2_sb;
        volatile u16_t * vf_sb;
    } host_hc_status_block;

    /*physical address of the status block.*/
} gen_status_block_t;

typedef struct _connection_info
{
    volatile u32_t con_state;
        #define LM_CON_STATE_CLOSE      0
        #define LM_CON_STATE_OPEN       1
        #define LM_CON_STATE_HALT       2
        #define LM_CON_STATE_TERMINATE  3
    u32_t con_cid;
    //more will follow...
}connection_info;

//attn group wiring
typedef struct _route_cfg_sig_output
{
    #define NUM_ATTN_REGS_E1X 4
    #define NUM_ATTN_REGS_E2  5
    #define MAX_ATTN_REGS 5

    u32_t attn_sig_dword[MAX_ATTN_REGS];

} route_cfg_sig_output;

/* interrupt/host coalesing configuration info */
#define HC_TIMEOUT_RESOLUTION_IN_US     4
typedef struct _lm_int_coalesing_info {
    struct dynamic_hc_config    eth_dynamic_hc_cfg;

    u32_t  hc_usec_c_sb[HC_CSTORM_SB_NUM_INDICES];          /* static host coalescing period for cstorm sb indexes */
    u32_t  hc_usec_u_sb[HC_USTORM_SB_NUM_INDICES];          /* static host coalescing period for ustorm sb indexes */
} lm_int_coalesing_info;

/*******************************************************************************
 * Device state variables.
 ******************************************************************************/
// Driver increase/decrease/set macros for L2/L4
#define LM_COMMON_DRV_STATS_ATOMIC_INC(_pdev, layer_type, field_name) \
            mm_atomic_inc(&((_pdev->vars.stats.stats_mirror.stats_drv.drv_##layer_type.field_name)));
#define LM_COMMON_DRV_STATS_ATOMIC_DEC(_pdev, layer_type, field_name) \
            mm_atomic_dec(&((_pdev->vars.stats.stats_mirror.stats_drv.drv_##layer_type.field_name)));
#define LM_COMMON_DRV_STATS_INC(_pdev, layer_type, field_name) \
            ((_pdev->vars.stats.stats_mirror.stats_drv.drv_##layer_type.field_name)++);
#define LM_COMMON_DRV_STATS_DEC(_pdev, layer_type, field_name) \
            ((_pdev->vars.stats.stats_mirror.stats_drv.drv_##layer_type.field_name)--);

#define LM_COMMON_DRV_STATS_ATOMIC_INC_TOE(_pdev, field_name)  LM_COMMON_DRV_STATS_ATOMIC_INC(_pdev, toe, field_name)
#define LM_COMMON_DRV_STATS_ATOMIC_DEC_TOE(_pdev, field_name)  LM_COMMON_DRV_STATS_ATOMIC_DEC(_pdev, toe, field_name)

#define LM_COMMON_DRV_STATS_INC_ETH(_pdev, field_name)  LM_COMMON_DRV_STATS_INC(_pdev, eth, field_name)
#define LM_COMMON_DRV_STATS_DEC_ETH(_pdev, field_name)  LM_COMMON_DRV_STATS_DEC(_pdev, eth, field_name)

/* currently driver ETH stats that use ATOMIC_INC are not required for NDIS or BACS, therefore they are disabled in release version */
#if DBG

#define LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(_pdev, field_name)  LM_COMMON_DRV_STATS_ATOMIC_INC(_pdev, eth, field_name)
#define LM_COMMON_DRV_STATS_ATOMIC_DEC_ETH(_pdev, field_name)  LM_COMMON_DRV_STATS_ATOMIC_DEC(_pdev, eth, field_name)
#else
#define LM_COMMON_DRV_STATS_ATOMIC_INC_ETH(_pdev, field_name)
#define LM_COMMON_DRV_STATS_ATOMIC_DEC_ETH(_pdev, field_name)
#endif /* DBG */

/* this is a wrapper structure for a vf to pf message, it contains the message itself,
 * we use a void pointer to the actual message to enable compiling the vbd with out the vf/pf interface
 */
typedef struct _lm_vf_pf_message_t
{
    u32_t           state;
    u32_t           message_size;
    void *          message_virt_addr;
    lm_address_t    message_phys_addr;
    volatile u16 *  done;
    void         *  cookie;
    u16_t           do_not_arm_trigger;
}
lm_vf_pf_message_t;
////////////////////// Start DCBX define /////////////////////////////////////////////////////
#define IS_DCB_SUPPORTED(_pdev)  (((_pdev)->params.dcbx_config_params.dcb_enable) && \
                                  (!CHIP_IS_E1x(_pdev) && (LM_CHIP_PORT_MODE_2 == CHIP_PORT_MODE(_pdev))))

#define IS_DCB_ENABLED(_pdev)  ((_pdev)->dcbx_info.is_enabled)

typedef enum {
    FUNCTION_DCBX_START_POSTED      = 0,
    FUNCTION_DCBX_START_COMPLETED   = 1,
    FUNCTION_DCBX_STOP_POSTED       = 2,
    FUNCTION_DCBX_STOP_COMPLETED    = 3,
} lm_dcbx_function_state_t;

typedef struct _lm_dcbx_info_t
{
    dcbx_update_task_state dcbx_update_lpme_task_state;
    // The dcbx ramrod state
    volatile u32_t dcbx_ramrod_state;
    // Flow control configuration
    void            *pfc_fw_cfg_virt;
    lm_address_t    pfc_fw_cfg_phys;

    u32_t dcbx_error;
    #define DCBX_ERROR_NO_ERROR             (0)
    #define DCBX_ERROR_MCP_CMD_FAILED       (1 << 0)
    #define DCBX_ERROR_SET_TIMER            (1 << 1)
    #define DCBX_ERROR_REGISTER_LPME        (1 << 2)
    #define DCBX_ERROR_WRONG_PORT           (1 << 3)
    #define DCBX_ERROR_RESOURCE             (1 << 4)

    // This parameter can only be changed in is_dcbx_neg_received and is a one-shut parameter
    u8_t is_dcbx_neg_received;
    u8_t is_enabled;
    u8_t _pad[2];

    // For debugging
    u32_t lpme_failed_cnt;
}lm_dcbx_info_t;


////////////////////// End DCBX define /////////////////////////////////////////////////////

typedef enum
{
    NOT_PMF         = 0,
    PMF_ORIGINAL    = 1,
    PMF_MIGRATION   = 2,
}pmf_type_t;

typedef struct _lm_variables_t
{
#if defined(__SunOS)
    ddi_acc_handle_t reg_handle[MAX_NUM_BAR]; /* Holds the DMA registration handle */
#endif
    volatile void * mapped_bar_addr[MAX_NUM_BAR]; /* Holds the mapped BAR address.*/

    gen_sp_status_block_t gen_sp_status_block;
    gen_status_block_t status_blocks_arr[MAX_NDSB]; /* at index 16 the the default status block lies */
    // Host Coalescing acknowledge numbers - this is the local copy to compare against the status index of each of the status blocks.
    u16_t u_hc_ack[MAX_NDSB]; //local copy of non-default USTORM consumer
    u16_t c_hc_ack[MAX_NDSB]; //local copy of non-default CSTORM consumer
    u16_t hc_def_ack;            //local copy of SP consumer
    u16_t _hc_pad;
    u16_t attn_def_ack;          //local copy of attention bits consumer
    u16_t attn_state;            //states for all 16 attn lines (per func) 0=ready for assertion 1=ready for deassertion
    route_cfg_sig_output attn_groups_output[MAX_DYNAMIC_ATTN_GRPS]; //dynamic attn groups wiring definitions
    u32_t attn_sig_af_inv_reg_addr[MAX_ATTN_REGS]; // addresses of the AEU_AFTER_INVERT registers
    u8_t  num_attn_sig_regs;
    u32_t aeu_mask_attn_func;    //mask the relevant AEU line from config register
    lm_status_t link_status;

    lm_int_coalesing_info       int_coal;

    connection_info connections[MAX_ETH_CONS + MAX_VF_ETH_CONS];
    u8_t eth_init_state;        /* deprecated. used only to mark if eth is already init or not. */
    #define PORT_STATE_CLOSE   0
    #define PORT_STATE_OPEN    1
    #define PORT_STATE_CLOSING 2

    lm_medium_t medium;
    lm_flow_control_t flow_control;

    // lm statistics
    lm_stats_all_t stats ;

    #define LM_DMAE_CMD_WB_ACCESS    0    // wide bus access
    #define LM_DMAE_CMD_PCI2PCI_COPY 1    // dmae pci to pci copy cmd
    #define LM_DMAE_CMD_MAX          2
    struct dmae_command_context lm_dmae_ctx_arr[LM_DMAE_CMD_MAX] ; // this is used for dmae commands

    // TRUE if read/write DMAE operations can be done (DMAE block + PXP initialized)
    #define DMAE_READY(pdev) (pdev->vars.b_is_dmae_ready)
    u8_t b_is_dmae_ready ;

    // mirrored NIG MAC table - used in MF/SI mode to support VMChimney.
    lm_nig_mirror_t nig_mirror;

    //TODO MCP interface ready
    u16_t fw_wr_seq;
    u8_t  fw_timed_out;
    u32_t fw_port_stats_ptr; // pointer to mcp scratch pad for statistics saving (host_func_stats_t)
    u32_t fw_func_stats_ptr; // pointer to Managment statistics (host_port_stats_t)


    /* Serdes autonegotiation fallback.  For a serdes medium,
     * if we cannot get link via autonegotiation, we'll force
     * the speed to get link. */
    //TODO after specs of serdes
    u8_t mac_type;
    #define MAC_TYPE_NONE                       0
    #define MAC_TYPE_EMAC                       1
    #define MAC_TYPE_BMAC                       2
    #define MAC_TYPE_UMAC                       3
    #define MAC_TYPE_XMAC                       4
    #define MAC_TYPE_MAX                        4

    /*Target phy address used with mread and mwrite*/
    u8_t phy_addr;

    /* This flag is set if the cable is attached when there
     * is no link.  The upper module could check this flag to
     * determine if there is a need to wait for link. */
    u8_t cable_is_attached;

    /* Write sequence for driver pulse. */
    u16_t drv_pulse_wr_seq;

    // the page tables
    u32_t searcher_t1_num_pages;
    void **searcher_t1_virt_addr_table;
    lm_address_t *searcher_t1_phys_addr_table;

    u32_t searcher_t2_num_pages;
    void **searcher_t2_virt_addr_table;
    lm_address_t *searcher_t2_phys_addr_table;

    u32_t timers_linear_num_pages;
    void **timers_linear_virt_addr_table;
    lm_address_t *timers_linear_phys_addr_table;

    u32_t qm_queues_num_pages;
    void** qm_queues_virt_addr_table;
    lm_address_t *qm_queues_phys_addr_table;

    u32_t context_cdu_num_pages;
    void **context_cdu_virt_addr_table;
    lm_address_t *context_cdu_phys_addr_table;

    u32_t elt_num_pages; // must be less then 16
    void * elt_virt_addr_table[NUM_OF_ELT_PAGES];
    lm_address_t elt_phys_addr_table[NUM_OF_ELT_PAGES];

    // Zeroed buffer to use in WB zero memory
    u32_t zero_buffer[DMAE_MAX_RW_SIZE_STATIC] ;

    u32_t clk_factor ; // clock factor to multiple timeouts in non ASIC (EMUL/FPGA) cases (value is 1 for ASIC)

    u32_t inst_id; // represents Bus & Device numbers
                   //  0x0000ff00 - Bus
                   //  0x000000ff - Device
    #ifndef INST_ID_TO_BUS_NUM
    #define INST_ID_TO_BUS_NUM(_inst_id) (((_inst_id) >> 8)& 0xFF)
    #define MAX_PCI_BUS_NUM                  (256)
    #endif // INST_ID_TO_BUS_NUM
    /* Emulation/FPAG doorbell full workaround is enabled.
     * The only impact on ASIC is an extra "if" command to check chip rev */
    #ifndef USER_LINUX
    #define EMULATION_DOORBELL_FULL_WORKAROUND
    #endif
    #if defined(EMULATION_DOORBELL_FULL_WORKAROUND)
    u32_t doorbells_cnt;
    #define DOORBELL_CHECK_FREQUENCY 500

    #define ALLOWED_DOORBELLS_HIGH_WM 1000
    #define ALLOWED_DOORBELLS_LOW_WM 700
    u8_t  doorbells_blocked;
    u32_t doorbells_high_wm_reached; /* for statistics */
    #endif
    u8_t enable_intr; /* When this flag is set process interrupt */
    u8_t dbg_intr_in_wrong_state;
    u8_t dbg_intr_in_disabled;
    u8_t dbg_intr_zero_status;
    // is this device in charge on link support.
    pmf_type_t is_pmf;
    #define IS_PMF(_pdev)               (( PMF_ORIGINAL == (_pdev)->vars.is_pmf) || ( PMF_MIGRATION == (_pdev)->vars.is_pmf))
    #define IS_PMF_ORIGINAL(_pdev)      ( PMF_ORIGINAL == (_pdev)->vars.is_pmf)
    #define IS_PMF_MIGRATION(_pdev)     ( PMF_MIGRATION == (_pdev)->vars.is_pmf)
    // The load-response we received from MCP when loading... need for elink calls and convenient
    // for debugging.
    lm_loader_response load_code;

    BOOL b_in_init_reset_flow;

    // cls_vars
    struct elink_vars link;
    u32_t link_chng_cnt;
    #define LM_LINK_CHNG_CNT(pdev) ((pdev)->vars.link_chng_cnt)
    u32_t shared_l5_mac_client_id;
    u64_t last_recycling_timestamp;
    /* sriov-related */
    u8_t num_vfs_enabled; /* number of vfs that were enabled, need this for disabling them */
    u8_t is_igu_test_mode;
    u8_t _pad[2];
    u32_t vf_pf_channel_lock;
    lm_vf_pf_message_t  vf_pf_mess;
} lm_variables_t;

typedef struct _eth_tx_prod_t
{
    u32_t packets_prod;
    u16_t bds_prod;
    u16_t reserved;
}eth_tx_prod_t;

/*******************************************************************************
 * bd chain
 ******************************************************************************/


/*******************************************************************************
 * Transmit info.
 ******************************************************************************/

typedef struct _lm_tx_chain_t
{
    u32_t idx;

    lm_bd_chain_t bd_chain;


    eth_tx_prod_t eth_tx_prods;


    u32_t prod_bseq;
    u16_t pkt_idx;
    u16_t volatile *hw_con_idx_ptr;

    u16_t coalesce_buf_cnt;
    u16_t _reserved;

    /* debug stats */
    u32_t coalesce_buf_used;
    u32_t lso_split_used;

    lm_hc_sb_info_t hc_sb_info;

    s_list_t active_descq;
    s_list_t coalesce_buf_list;
} lm_tx_chain_t;


typedef struct _lm_tx_info_t
{
    lm_tx_chain_t chain[MAX_TX_CHAIN];
    #define LM_TXQ(_pdev, _idx)             (_pdev)->tx_info.chain[_idx]

    u32_t max_chain_idx;
    u32_t catchup_chain_idx;

} lm_tx_info_t;

/*******************************************************************************
 * Receive info.
******************************************************************************/

typedef enum
{
    LM_RXQ_CHAIN_IDX_BD  = 0,
    LM_RXQ_CHAIN_IDX_SGE = 1,
    LM_RXQ_CHAIN_IDX_MAX = 2,
} lm_rxq_chain_idx_t ;

typedef struct _lm_rx_chain_t
{
    u32_t           idx;

    lm_bd_chain_t   chain_arr[LM_RXQ_CHAIN_IDX_MAX];

    u32_t           lah_size; // if 0 - only LM_RXQ_CHAIN_IDX_BD chain is valid
    u16_t           bd_prod_without_next; // bd prod without next BD taken into account
    u32_t           prod_bseq;
    u32_t           desc_cnt;
    u32_t           ret_bytes;
    u32_t           ret_bytes_last_fw_update;
    u16_t volatile *hw_con_idx_ptr; // TODO - remove - check non NDIS clients

    lm_hc_sb_info_t hc_sb_info;

    s_list_t        free_descq;
    s_list_t        active_descq;
} lm_rx_chain_t;

/*******************************************************************************
* send queue  info.
******************************************************************************/

typedef struct _lm_sq_chain_t
{
    /* This is a contiguous memory block of params.l2_sq_bd_page_cnt pages
     * used for rx completion.  The BD chain is arranged as a circular
     * chain where the last BD entry of a page points to the next page,
     * and the last BD entry of the last page points to the first. */
    struct slow_path_element *sq_chain_virt;
    lm_address_t bd_chain_phy;

    u16_t prod_idx;
    u16_t con_idx;

    struct slow_path_element *prod_bd;
    struct slow_path_element *last_bd;
    u16_t bd_left;

} lm_sq_chain_t;


/**
 * Event Queue Structure. Used for the main event-queue, and
 * also event queues used by iscsi + fcoe
 */
typedef struct _lm_eq_chain_t
{
    lm_bd_chain_t bd_chain;
    u16_t volatile *hw_con_idx_ptr;
    u16_t iro_prod_offset; /* The producer offset inside internal RAM */
    lm_hc_sb_info_t hc_sb_info;

} lm_eq_chain_t;


/* the rcq chain now holds the real HSI eth_rx_cqe */
typedef struct _lm_rcq_chain_t
{
    u32_t idx; //this is the symmetric index of the corresponding Rx

    lm_bd_chain_t bd_chain;

    u32_t prod_bseq;
    u16_t volatile *hw_con_idx_ptr;
    u16_t iro_prod_offset; /* The producer offset inside internal RAM */

    lm_hc_sb_info_t hc_sb_info;

} lm_rcq_chain_t;

typedef struct _lm_rx_info_t
{
    lm_rx_chain_t  rxq_chain[MAX_RX_CHAIN];
    lm_rcq_chain_t rcq_chain[MAX_RX_CHAIN];

    #define LM_RXQ(_pdev, _idx)                       (_pdev)->rx_info.rxq_chain[_idx]
    #define LM_RXQ_CHAIN(_pdev, _idx, _rxq_chain_idx) (_pdev)->rx_info.rxq_chain[_idx].chain_arr[_rxq_chain_idx]
    #define LM_RXQ_CHAIN_BD(_pdev, _idx)              LM_RXQ_CHAIN(_pdev, _idx, LM_RXQ_CHAIN_IDX_BD )
    #define LM_RXQ_CHAIN_SGE(_pdev, _idx)             LM_RXQ_CHAIN(_pdev, _idx, LM_RXQ_CHAIN_IDX_SGE )
    #define LM_RXQ_IS_CHAIN_SGE_VALID(_pdev, _idx)    (0 != (_pdev)->rx_info.rxq_chain[_idx].lah_size)
    #define LM_RXQ_SGE_PTR_IF_VALID(_pdev, _idx)      LM_RXQ_IS_CHAIN_SGE_VALID(_pdev, _idx) ? &LM_RXQ_CHAIN_SGE(_pdev, _idx ) : NULL

    #define LM_RCQ(_pdev, _idx)                       (_pdev)->rx_info.rcq_chain[_idx]

    struct tstorm_eth_approximate_match_multicast_filtering appr_mc;

} lm_rx_info_t;

#define MAX_RAMRODS_OUTSTANDING 2

typedef struct _lm_request_sp
{
    u8_t req_type;
    #define REQ_SET_INFORMATION   0x1
    #define REQ_QUERY_INFORMATION 0x2

    u32_t ioc;  //IOCTL number of the request
    u8_t ok_to_indicate; //should the request be indicated up to NDIS or not
    void *clnt_blk; //L2/L4 client block
    u8_t ramrod_priority; //ramrod priority (this priority is for the 'common sq' and not for the 'per CID one outstanding' mechnism)
    struct sq_pending_command sp_list_command;
} lm_request_sp;

typedef struct _lm_ramrod_info_t
{
    lm_request_sp ramrod_reqs[MAX_RAMRODS_OUTSTANDING];
    volatile u8_t insert_idx;
    volatile u8_t extract_idx;
    volatile u8_t num_req_outstanding;
    volatile u8_t is_req_pending;
} lm_ramrod_info_t;

typedef struct _lm_client_info_t
{
    struct client_init_ramrod_data * client_init_data_virt;
    lm_address_t client_init_data_phys;
    lm_ramrod_info_t                 ramrod_info;

    /* Classification objects used in ecore-sp-verbs */
    struct ecore_vlan_mac_obj mac_obj;
    struct ecore_vlan_mac_obj mac_vlan_obj;

    volatile void * set_mac_cookie;
    volatile u32_t  sp_mac_state;

    /* RX_MODE related */
    volatile void * set_rx_mode_cookie;
    volatile unsigned long sp_rxmode_state;

    u32_t  last_set_rx_mask;
} lm_client_info_t ;

/*************** SlowPath Queue Information: should be modified under SQ_LOCK ************/
typedef void(*lm_sq_comp_cb_t)(struct _lm_device_t *pdev, struct sq_pending_command *pending);

typedef enum {
    SQ_STATE_NORMAL  = 0,
    SQ_STATE_PENDING = 1, /* In this state slowpath will be posted but not to HW.
                           * completed by vbd work-item (Error Recovery) */
    SQ_STATE_BLOCKED = 2
} lm_sq_state_t;

typedef struct _lm_sq_info_t
{
    lm_sq_chain_t sq_chain;
    u8_t num_pending_normal;
    u8_t num_pending_high;

    d_list_t pending_normal;
    d_list_t pending_high;

    /* This list contains the elements that have been posted to the SQ
     * but not completed by FW yet. Maximum list size is MAX_NUM_SPE anyway */
    d_list_t pending_complete;

    lm_sq_state_t sq_state;

    lm_sq_comp_cb_t sq_comp_cb[MAX_CONNECTION_TYPE];
    u8_t sq_comp_scheduled;

} lm_sq_info_t;

typedef enum {
    FUNCTION_START_POSTED = 0,
    FUNCTION_START_COMPLETED = 1,
    FUNCTION_STOP_POSTED = 2,
    FUNCTION_STOP_COMPLETED = 3
} lm_function_state_t;

typedef struct _lm_eq_info_t
{
    lm_eq_chain_t eq_chain;

    volatile u32_t function_state;

} lm_eq_info_t;

/* for now */
//TODO : need to change according to hsi enum
#define MAX_PROTO (FCOE_CONNECTION_TYPE + 1)
#if 0
#define LM_PROTO_NIC    0
#define LM_PROTO_TOE    1
#endif //0

/*******************************************************************************
 * cid resources
 ******************************************************************************/

typedef struct _lm_cid_resc_t
{
    lm_sp_req_manager_t sp_req_mgr;
    void                *cookies[MAX_PROTO];
    u8_t                cid_pending;
#if defined(__SunOS)
    ddi_acc_handle_t    reg_handle; /* Holds the DMA registration handle */
#endif
    volatile void       *mapped_cid_bar_addr;/* Holds the mapped BAR address.*/
} lm_cid_resc_t;

struct lm_context_cookie{
    lm_cid_resc_t cid_resc;
    u32_t next;
    u32_t prev; /* for enabling extraction */
    u8_t  invalid;
    u8_t  ip_type; /* for searcher mirror hash management */
    u8_t  cfc_delete_cnt;
    u8_t _pad;
    u32_t h_val;   /* for searcher mirror hash management */
};
#define LM_MAX_VALID_CFC_DELETIONS  3

#define LM_CONTEXT_VALID 0
#define LM_CONTEXT_INVALID_WAIT 1
#define LM_CONTEXT_INVALID_DELETE 2

/* The size of the context is currently 1K... this can change in the future*/
#define LM_CONTEXT_SIZE 1024

/* structures to support searcher hash table entries */
typedef struct _lm_searcher_hash_entry {
    u8_t num_ipv4;
    u8_t num_ipv6;
    u8_t depth_ipv4;
} lm_searcher_hash_entry_t;

typedef struct _lm_searcher_hash_info {
    #define SEARCHER_KEY_LEN 40
    u8_t searcher_key[SEARCHER_KEY_LEN];
    u8_t searcher_key_bits[SEARCHER_KEY_LEN*8];

    /* length in bytes of IPV6 "4 tuple" */
    #define MAX_SEARCHER_IN_STR 36
    u8_t searcher_in_str_bits[MAX_SEARCHER_IN_STR*8];

    lm_searcher_hash_entry_t *searcher_table;
    u32_t num_tuples;           /* for debug */
    u8_t hash_depth_reached;    /* for debug */
    u8_t num_hash_bits;
} lm_searcher_hash_info_t;

/* per-function context data */
typedef struct _lm_context_info {
    struct lm_context_cookie * array;
    /* spinlock_t lock; lock was moved to the UM */
    u32_t proto_start[MAX_PROTO];
    u32_t proto_end[MAX_PROTO];
    u32_t proto_ffree[MAX_PROTO];
    u32_t proto_pending[MAX_PROTO]; /* list of cids that are pending for cfc-delete */

    /* field added for searcher mirror hash management.
     * it is part of the context info because this hash management
     * is done as part of cid allocation/de-allocating */
    lm_searcher_hash_info_t searcher_hash;
} lm_context_info_t;

//#endif /* 0 */

/*******************************************************************************
 * Include the l4 header file.
 ******************************************************************************/
#include "lm_l4st.h"
#include "lm_l4if.h"

#include "lm_l5st.h"
#include "lm_l5if.h"

/* lm device offload info that is common to all offloaded protocols */
typedef struct _lm_offload_info_t
{
    struct _lm_device_t *pdev;

    /* Per stack offload state info.  Each index correspond to a stack. */
    #define STATE_BLOCK_IDX0                0
    #define STATE_BLOCK_TOE                 STATE_BLOCK_IDX0
    #define STATE_BLOCK_IDX1                1
    #define STATE_BLOCK_IDX2                2
    #define STATE_BLOCK_ISCSI               STATE_BLOCK_IDX2
    #define STATE_BLOCK_IDX3                3
    #define STATE_BLOCK_RDMA                STATE_BLOCK_IDX3
    #define STATE_BLOCK_IDX4                4
    #define STATE_BLOCK_FCOE                STATE_BLOCK_IDX4
    #define STATE_BLOCK_CNT                 5
    lm_state_block_t *state_blks[STATE_BLOCK_CNT];
} lm_offload_info_t;

typedef void(*lm_cid_recycled_cb_t)(struct _lm_device_t *pdev, void *cookie, s32_t cid);

struct iro {
    u32_t base;
    u16_t m1;
    u16_t m2;
    u16_t m3;
    u16_t size;
} ;

/* ecore info. Variables that are accessed from the common init code need using the defines below */
typedef struct _ecore_info_t
{
    void         * gunzip_buf;     /* used for unzipping data */
    u32_t          gunzip_outlen;
    lm_address_t   gunzip_phys;     /* physical address of buffer */
    #define FW_BUF_SIZE 0x8000
    #define GUNZIP_BUF(_pdev) (_pdev)->ecore_info.gunzip_buf
    #define GUNZIP_OUTLEN(_pdev) (_pdev)->ecore_info.gunzip_outlen
    #define GUNZIP_PHYS(_pdev) (_pdev)->ecore_info.gunzip_phys
    const struct raw_op          *init_ops;
    /* Init blocks offsets inside init_ops */
    const u16_t                    *init_ops_offsets;
    /* Data blob - has 32 bit granularity */
    const u32_t                    *init_data;
    /* Zipped PRAM blobs - raw data */
    const u8_t               *tsem_int_table_data;
    const u8_t               *tsem_pram_data;
    const u8_t               *usem_int_table_data;
    const u8_t               *usem_pram_data;
    const u8_t               *xsem_int_table_data;
    const u8_t               *xsem_pram_data;
    const u8_t               *csem_int_table_data;
    const u8_t               *csem_pram_data;
    #define INIT_OPS(_pdev)                 (_pdev)->ecore_info.init_ops
    #define INIT_DATA(_pdev)                (_pdev)->ecore_info.init_data
    #define INIT_OPS_OFFSETS(_pdev)         (_pdev)->ecore_info.init_ops_offsets
    #define INIT_TSEM_PRAM_DATA(_pdev)      (_pdev)->ecore_info.tsem_pram_data
    #define INIT_XSEM_PRAM_DATA(_pdev)      (_pdev)->ecore_info.xsem_pram_data
    #define INIT_USEM_PRAM_DATA(_pdev)      (_pdev)->ecore_info.usem_pram_data
    #define INIT_CSEM_PRAM_DATA(_pdev)      (_pdev)->ecore_info.csem_pram_data
    #define INIT_TSEM_INT_TABLE_DATA(_pdev) (_pdev)->ecore_info.tsem_int_table_data
    #define INIT_XSEM_INT_TABLE_DATA(_pdev) (_pdev)->ecore_info.xsem_int_table_data
    #define INIT_USEM_INT_TABLE_DATA(_pdev) (_pdev)->ecore_info.usem_int_table_data
    #define INIT_CSEM_INT_TABLE_DATA(_pdev) (_pdev)->ecore_info.csem_int_table_data
    const struct iro              *iro_arr;
    #define INIT_IRO_ARRAY(_pdev) (_pdev)->ecore_info.iro_arr
    #define IRO (PFDEV(pdev))->ecore_info.iro_arr

} ecore_info_t;

typedef struct _flr_stats_t {
    u32_t   is_pf;
    u32_t   default_wait_interval_ms;
    u32_t   cfc_usage_counter;
    u32_t   qm_usage_counter;
    u32_t   tm_vnic_usage_counter;
    u32_t   tm_num_scans_usage_counter;
    u32_t   dq_usage_counter;
    u32_t   final_cleanup_complete;
    u32_t   dmae_cx;
    u32_t   pbf_queue[3];
    u32_t   pbf_transmit_buffer[3];
} flr_stats_t;


typedef struct _lm_slowpath_data_t {
    /* Function Start Data  */
    struct function_start_data * func_start_data;
    lm_address_t func_start_data_phys;

    /* Classification */
    union {
        struct mac_configuration_cmd        e1x;
        struct eth_classify_rules_ramrod_data   e2;
    } * mac_rdata[LM_CLI_IDX_MAX];
    lm_address_t mac_rdata_phys[LM_CLI_IDX_MAX];

    /* TODO: MAC-VLAN PAIR!!! */

    union {
        struct tstorm_eth_mac_filter_config e1x;
        struct eth_filter_rules_ramrod_data e2;
    } * rx_mode_rdata[LM_CLI_IDX_MAX];
    lm_address_t rx_mode_rdata_phys[LM_CLI_IDX_MAX]; // FIXME: multi-client...

    union {
        struct mac_configuration_cmd            e1;
        struct eth_multicast_rules_ramrod_data  e2;
    } * mcast_rdata[LM_CLI_IDX_MAX];
    lm_address_t mcast_rdata_phys[LM_CLI_IDX_MAX];

    union {
        //struct eth_rss_update_ramrod_data_e1x e1x;
        struct eth_rss_update_ramrod_data   e2;
    } * rss_rdata;
    lm_address_t rss_rdata_phys;

    struct function_niv_update_data* niv_function_update_data;
    lm_address_t niv_function_update_data_phys;

} lm_slowpath_data_t ;



typedef struct _lm_slowpath_info_t {
    lm_slowpath_data_t slowpath_data;

    #define LM_SLOWPATH(pdev, var)      (pdev->slowpath_info.slowpath_data.var)
    #define LM_SLOWPATH_PHYS(pdev, var) (pdev->slowpath_info.slowpath_data.var##_phys)


    /* CAM credit pools */
    struct ecore_credit_pool_obj    vlans_pool;
    struct ecore_credit_pool_obj    macs_pool;

    /* Rx-Mode Object */
    struct ecore_rx_mode_obj rx_mode_obj;

    /* Multi-Cast */
    struct ecore_mcast_obj mcast_obj[LM_CLI_IDX_MAX];
    volatile void * set_mcast_cookie[LM_CLI_IDX_MAX];
    volatile u32_t  sp_mcast_state[LM_CLI_IDX_MAX];

    /* RSS - Only support for NDIS client ! */
    struct ecore_rss_config_obj rss_conf_obj;
    volatile void * set_rss_cookie;
    volatile u32_t  sp_rss_state;

    u32_t  rss_hash_key[RSS_HASH_KEY_SIZE/4];
    u32_t  last_set_rss_flags;
    u32_t  last_set_rss_result_mask;
    u8     last_set_indirection_table[T_ETH_INDIRECTION_TABLE_SIZE];

    volatile u32_t niv_ramrod_state;
    #define NIV_RAMROD_NOT_POSTED 0
    #define NIV_RAMROD_POSTED 1
    #define NIV_RAMROD_COMPLETED 2
    volatile u8_t last_vif_list_bitmap;

} lm_slowpath_info_t;

#define MAX_ER_DEBUG_ENTRIES 10

typedef struct _lm_er_debug_info_t
{
    u32_t attn_sig[MAX_ATTN_REGS];
} lm_er_debug_info_t;

typedef struct _lm_debug_info_t
{
    u32_t ack_dis[MAX_RSS_CHAINS];
    u32_t ack_en[MAX_RSS_CHAINS];
    u32_t ack_def_dis;
    u32_t ack_def_en;

    /* Debug information for error recovery. */
    /* Data for last MAX_ER_DEBUG_ENTRIES recoveries */
    lm_er_debug_info_t er_debug_info[MAX_ER_DEBUG_ENTRIES];
    u8_t               curr_er_debug_idx; /* Index into array above */

} lm_debug_info_t;

/*******************************************************************************
 * Main device block.
 ******************************************************************************/
typedef struct _lm_device_t
{
    d_list_entry_t link;        /* Link for the device list. */

    u32_t ver_num;              /* major:8 minor:8 fix:16 */
    u8_t  ver_str[16];          /* null terminated version string. */
    u32_t ver_num_fw;           /* major:8 minor:8 fix:16 */

    lm_variables_t     vars;
    lm_tx_info_t       tx_info;
    lm_rx_info_t       rx_info;
    lm_sq_info_t       sq_info;
    lm_eq_info_t       eq_info;
    lm_client_info_t   client_info[MAX_ETH_CONS + MAX_VF_ETH_CONS];
    lm_offload_info_t  ofld_info;
    lm_toe_info_t      toe_info;
    lm_dcbx_info_t     dcbx_info;
    lm_hardware_info_t hw_info;
    lm_slowpath_info_t slowpath_info;
    lm_params_t        params;
    lm_context_info_t* context_info;
    //lm_mc_table_t mc_table;
    lm_nwuf_list_t     nwuf_list;

    /* Statistics. */
    u32_t chip_reset_cnt;
    u32_t fw_timed_out_cnt;

    lm_cid_recycled_cb_t    cid_recycled_callbacks[MAX_PROTO];

    lm_iscsi_info_t iscsi_info;

    lm_fcoe_info_t  fcoe_info;

    ecore_info_t    ecore_info;
    struct _lm_device_t*    pf_dev;
#ifdef VF_INVOLVED
    pf_resources_set_t      pf_resources;
    u8_t                    vf_idx;
    u8_t                    _vf_pad[3];
//VF PF Channel params
    void *                  pf_vf_acquiring_resp;
#endif
    flr_stats_t     flr_stats;

    lm_debug_info_t debug_info;

    /* Turned on if a panic occured in the device... (viewed by functions that wait and get a timeout... - do not assert... )
     * not turned on yet, prep for the future...
     */
    u8_t panic;
} lm_device_t;


// driver pulse interval calculation
#define DRV_PULSE_PERIOD_MS_FACTOR(_p)  CHIP_REV_IS_ASIC(_p) ? DRV_PULSE_PERIOD_MS : (DRV_PULSE_PERIOD_MS*10)

/*******************************************************************************
 * Functions exported between file modules.
 ******************************************************************************/
/* Prints the entire information of all status blocks
 * Parameters:
 * pdev   - LM device which holds the status blocks within
 */
void print_sb_info(lm_device_t *pdev);

//__________________________________________________________________________________

lm_status_t lm_pretend_func( struct _lm_device_t *pdev, u16_t pretend_func_num );

/* returns a non-default status block according to rss ID
 * Parameters:
 * pdev   - LM device which holds the status blocks within
 * rss_id - RSS ID for which we return the specific status block
 */
volatile struct host_status_block * lm_get_status_block(lm_device_t *pdev, u8_t rss_id);

/* returns the default status block. It is unique per function.
 * Parameters:
 * pdev   - LM device which holds the status blocks within
 */
volatile struct hc_sp_status_block * lm_get_default_status_block(lm_device_t *pdev);

/* returns the attention status block. It is unique per function.
 * Parameters:
 * pdev   - LM device which holds the status blocks within
 */
volatile struct atten_sp_status_block * lm_get_attention_status_block(lm_device_t *pdev);

/**
 * @Description
 *      Prepares for MCP reset: takes care of CLP
 *      configurations.
 *
 * @param pdev
 * @param magic_val Old value of 'magic' bit.
 */
lm_status_t lm_reset_mcp_prep(lm_device_t *pde, u32_t * magic_val);
lm_status_t lm_reset_mcp_comp(lm_device_t *pdev, u32_t magic_val);



/* Driver calls this function in order to ACK the default/non-default status block index(consumer) toward the chip.
 * This is needed by the hw in order to decide whether an interrupt should be generated by the IGU.
 * This is achieved via write into the INT ACK register.
 * This function is also controls whether to enable/disable the interrupt line
 *
 * Parameters:
 * rss_id        - the RSS/CPU number we are running on
 * pdev          - this is the LM device
 */
void lm_int_ack_sb_enable(lm_device_t *pdev, u8_t rss_id);
void lm_int_ack_sb_disable(lm_device_t *pdev, u8_t rss_id);
void lm_int_ack_def_sb_enable(lm_device_t *pdev);
void lm_int_ack_def_sb_disable(lm_device_t *pdev);

#define USTORM_INTR_FLAG    1
#define CSTORM_INTR_FLAG    2
#define SERV_RX_INTR_FLAG   4
#define SERV_TX_INTR_FLAG   8

#ifndef USER_LINUX
static __inline u16_t lm_get_sb_number_indexes(lm_device_t *pdev)
{
    if (CHIP_IS_E1x(pdev))
    {
        return HC_SB_MAX_INDICES_E1X;
    }
    else
    {
        return HC_SB_MAX_INDICES_E2;
    }
}

#include "mm.h"
static __inline u16_t lm_get_sb_running_index(lm_device_t *pdev, u8_t sb_id, u8_t sm_idx)
{
#ifdef VF_INVOLVED
    if (IS_CHANNEL_VFDEV(pdev)) {
        return lm_vf_pf_get_sb_running_index(pdev, sb_id, sm_idx);
    }
#endif
    if (CHIP_IS_E1x(pdev))
    {
        return mm_le16_to_cpu(pdev->vars.status_blocks_arr[sb_id].host_hc_status_block.e1x_sb->sb.running_index[sm_idx]);
    }
    else
    {
        return mm_le16_to_cpu(pdev->vars.status_blocks_arr[sb_id].host_hc_status_block.e2_sb->sb.running_index[sm_idx]);
    }
}
static __inline u16_t lm_get_sb_index(lm_device_t *pdev, u8_t sb_id, u8_t idx)
{
#ifdef VF_INVOLVED
    if (IS_CHANNEL_VFDEV(pdev)) {
        return lm_vf_pf_get_sb_index(pdev, sb_id, idx);
    }
#endif
    if (CHIP_IS_E1x(pdev))
    {
        return mm_le16_to_cpu(pdev->vars.status_blocks_arr[sb_id].host_hc_status_block.e1x_sb->sb.index_values[idx]);
    }
    else
    {
        return mm_le16_to_cpu(pdev->vars.status_blocks_arr[sb_id].host_hc_status_block.e2_sb->sb.index_values[idx]);
    }
}


static __inline  u16_t volatile * lm_get_sb_running_indexes(lm_device_t *pdev, u8_t sb_idx)
{
    u16_t volatile * running_indexes_ptr;
    if (CHIP_IS_E1x(pdev))
    {
        running_indexes_ptr = &pdev->vars.status_blocks_arr[sb_idx].host_hc_status_block.e1x_sb->sb.running_index[0];
    }
    else
    {
        running_indexes_ptr = &pdev->vars.status_blocks_arr[sb_idx].host_hc_status_block.e2_sb->sb.running_index[0];
    }
    return running_indexes_ptr;
}
static __inline  u16_t volatile * lm_get_sb_indexes(lm_device_t *pdev, u8_t sb_idx)
{
    u16_t volatile * indexes_ptr;

#ifdef VF_INVOLVED
    if (IS_CHANNEL_VFDEV(pdev)) {
        return pdev->vars.status_blocks_arr[sb_idx].host_hc_status_block.vf_sb;
    }
#endif

    if (CHIP_IS_E1x(pdev))
    {
        indexes_ptr = &pdev->vars.status_blocks_arr[sb_idx].host_hc_status_block.e1x_sb->sb.index_values[0];
    }
    else
    {
        indexes_ptr = &pdev->vars.status_blocks_arr[sb_idx].host_hc_status_block.e2_sb->sb.index_values[0];
    }
    return indexes_ptr;
}


static __inline u8_t lm_map_igu_sb_id_to_drv_rss(lm_device_t *pdev, u8_t igu_sb_id)
{
    u8_t drv_sb_id = igu_sb_id;
    if (INTR_BLK_TYPE(pdev) == INTR_BLK_IGU)
    {
        if (drv_sb_id >= IGU_U_NDSB_OFFSET(pdev))
        {
            drv_sb_id -= IGU_U_NDSB_OFFSET(pdev);
        }
    }
    /* FIXME: this doesn't have to be right - drv rss id can differ from sb-id */
    return drv_sb_id;
}
static __inline u8_t lm_query_storm_intr(lm_device_t *pdev, u8_t igu_sb_id, u8_t * drv_sb_id)
{
    u8_t flags = 0;

    *drv_sb_id = igu_sb_id;

    switch(pdev->params.ndsb_type)
    {
        case LM_SINGLE_SM:
        /* One Segment Per u/c */
        SET_FLAGS(flags, USTORM_INTR_FLAG);
            break;

        case LM_DOUBLE_SM_SINGLE_IGU:
        /* One Segment Per u/c */
        SET_FLAGS(flags, USTORM_INTR_FLAG);
            break;

        default:
        {
            if (igu_sb_id >= IGU_U_NDSB_OFFSET(pdev))
            {
            *drv_sb_id -= IGU_U_NDSB_OFFSET(pdev);
            SET_FLAGS(flags, USTORM_INTR_FLAG);
            }
            else
            {
                SET_FLAGS(flags, CSTORM_INTR_FLAG);
            }
        }
        break;
    }
    return flags;
}

/* Check whether a non-default status block has changed, that is,
 * the hw has written a new prod_idx for on or more of its storm parts.
 *
 * Parameters:
 * pdev   - this is the LM device
 * sb_idx - this is the index where the status block lies in the array under the lm_device
 *
 * Return Value:
 * result - TRUE in case the specific status block is considered as changed.
 *          FALSE otherwise.
 *
 * Nots:
 * For performance optimization, this function is static inline.
 */
static __inline u8_t lm_is_sb_updated(lm_device_t *pdev, u8_t igu_sb_id)
{
    u8_t  result     = FALSE;
    u16_t hw_sb_idx  = 0;
    u8_t  flags      = 0;
    u8_t  drv_sb_id  = 0;

    DbgBreakIfFastPath(!pdev);
    if (!pdev)
    {
        return FALSE;
    }

    flags = lm_query_storm_intr(pdev, igu_sb_id, &drv_sb_id);

    if (GET_FLAGS(flags, USTORM_INTR_FLAG))
    {
        hw_sb_idx = lm_get_sb_running_index(pdev, drv_sb_id, SM_RX_ID);
        if (hw_sb_idx != pdev->vars.u_hc_ack[drv_sb_id])
        {
            DbgMessage2(pdev, INFORMi, "lm_is_sb_updated():u_sb.status_block_index:%d u_hc_ack:%d\n",
                        hw_sb_idx, pdev->vars.u_hc_ack[drv_sb_id]);

            result = TRUE;
        }
    }

    if (GET_FLAGS(flags, CSTORM_INTR_FLAG))
    {
        hw_sb_idx = lm_get_sb_running_index(pdev, drv_sb_id, SM_TX_ID);
        if (hw_sb_idx != pdev->vars.c_hc_ack[drv_sb_id])
        {
            DbgMessage2(pdev, INFORMi, "lm_is_sb_updated():c_sb.status_block_index:%d c_hc_ack:%d\n",
                        hw_sb_idx, pdev->vars.u_hc_ack[drv_sb_id]);

            result = TRUE;
        }
    }

    DbgMessage1(pdev, INFORMi, "lm_is_sb_updated(): result:%s\n", result? "TRUE" : "FALSE");

    return result;
}
#endif // !USER_LINUX

/* Check if the default statu blocks has changed, that is,
 * the hw has written a new prod_idx for on or more of its storm parts.
 *
 * Parameters:
 * pdev   - this is the LM device
 *
 * Return Value:
 * result - TRUE in case the status block is considered as changed.
 *          FALSE otherwise.
 */
u8_t lm_is_def_sb_updated(lm_device_t *pdev);


/* Check if the status block has outstanding completed Rx requests
 *
 * Parameters:
 * pdev   - this is the LM device
 * sb_idx - this is the index where the status block lies in the array under the lm_device
 *
 * Return Value:
 * result - TRUE in case the status block has new update regarding Rx completion
 *          FALSE otherwise.
 */
u8_t lm_is_rx_completion(lm_device_t *pdev, u8_t chain_idx);

/* Check if the status block has outstanding completed Tx requests
 *
 * Parameters:
 * pdev   - this is the LM device
 * sb_idx - this is the index where the status block lies in the array under the lm_device
 *
 * Return Value:
 * result - TRUE in case the status block has new update regarding Tx completion
 *          FALSE otherwise.
 */
u8_t lm_is_tx_completion(lm_device_t *pdev, u8_t chain_idx);

/*
 * Handle an IGU status-block update.
 * Parameters:
 * pdev - the LM device
 * igu_sb_id - the igu sb id that got the interrupt / MSI-X message
 * rx_rss_id / tx_rss_id - matching driver chains
 * flags: service_rx / service_tx to know which activity occured
 */
u8_t lm_handle_igu_sb_id(lm_device_t *pdev, u8_t igu_sb_id, OUT u8_t *rx_rss_id, OUT u8_t *tx_rss_id);

lm_status_t lm_establish_eth_con(struct _lm_device_t *pdev, u8_t const cid, u8_t sb_id);
lm_status_t lm_establish_forward_con(struct _lm_device_t *pdev);
lm_status_t lm_close_forward_con(struct _lm_device_t *pdev);
lm_status_t lm_close_eth_con(struct _lm_device_t *pdev, u8_t const cid);
lm_status_t lm_chip_stop(struct _lm_device_t *pdev);

lm_status_t
lm_empty_ramrod_eth(IN struct _lm_device_t *pdev,
                    IN const u32_t          cid,
                    IN u32_t                data_cid,
                    IN volatile u32_t       *curr_state,
                    IN u32_t                new_state);
/*
 * save client connection parameters for a given L2 client
 */
lm_status_t
lm_setup_client_con_params( IN struct _lm_device_t *pdev,
                            IN u8_t const          chain_idx,
                            IN struct              _lm_client_con_params_t *cli_params );

lm_status_t
lm_eq_ramrod_post_sync( IN struct _lm_device_t  *pdev,
                        IN u8_t                 cmd_id,
                        IN u64_t                data,
                        IN u8_t                 ramrod_priority,
                        IN volatile u32_t       *p_curr_state,
                        IN u32_t                curr_state,
                        IN u32_t                new_state);

//L2 Client conn, used for iscsi/rdma
/*
 * allocate and setup txq, rxq, rcq and set tstrom ram values for L2 client connection of a given client index
 */
lm_status_t
lm_init_client_con( IN struct _lm_device_t *pdev,
                    IN u8_t const          chain_idx,
                    IN u8_t const          b_alloc );

/*
 * reset txq, rxq, rcq counters for L2 client connection
 */
lm_status_t
lm_clear_eth_con_resc(
    IN struct _lm_device_t *pdev,
    IN u8_t const           cid
    );

/*
 * clear the status block consumer index in the internal ram for a given status block index
 */
lm_status_t
lm_clear_chain_sb_cons_idx(
    IN struct _lm_device_t *pdev,
    IN u8_t sb_idx,
    IN struct _lm_hc_sb_info_t *hc_sb_info,
    IN volatile u16_t ** hw_con_idx_ptr
    );


u8_t lm_is_eq_completion(lm_device_t *pdev);

/* Does relevant processing in case of attn signals assertion.
 * 1)Write '1' into attn_ack to chip(IGU) (do this in parallel for _all_ bits including the fixed 8 hard-wired via the
 *   set_ack_bit_register
 * 2)MASK AEU lines via the mask_attn_func_x register (also in parallel) via GRC - for AEU lower lines 0-7 only!
 * 3)Only for the 8 upper fixed hard-wired AEU lines: do their relevant processing, if any.
     Finally, drv needs to "clean the attn in the hw block"(e.g. INT_STS_CLR) for them.
 *
 * Parameters:
 * pdev      - this is the LM device
 * assertion_proc_flgs - attn lines which got asserted
 */
void lm_handle_assertion_processing(lm_device_t *pdev, u16_t assertion_proc_flgs);

/* Does relevant processing in case of attn signals deassertion.
 * 1) Grab split access lock register of MCP (instead of SW arbiter)
 * 2) Read 128bit after inverter via the 4*32regs via GRC.
 * 3) For each dynamic group (8 lower bits only!), read the masks which were set aside to find for each group which attn bit is a member and
 *    needs to be handled. pass all over atten bits belonged to this group and treat them accordingly.
 *    After an attn signal was handled, drv needs to "clean the attn in the hw block"(e.g. INT_STS_CLR) for that attn bit.
 * 4) Release split access lock register of MCP
 * 5) Write '0' into attn_ack to chip(IGU) (do this in parallel for _all_ bits, including the fixed 8 hard-wired, via the set_ack_bit_register)
 * 6) UNMASK AEU lines via the mask_attn_func_x register (also in parallel) via GRC - for AEU lower lines 0-7 only!
 *
 * Parameters:
 * pdev      - this is the LM device
 * deassertion_proc_flgs - attn lines which got deasserted
 */
void lm_handle_deassertion_processing(lm_device_t *pdev, u16_t deassertion_proc_flgs);

/* Returns the attn_bits and attn_ack fields from the default status block
 *
 * Parameters:
 * pdev      - this is the LM device
 * attn_bits - OUT param which receives the attn_bits from the atten part of the def sb
 * attn_ack  - OUT param which receives the attn_ack from the atten part of the def sb
 */
void lm_get_attn_info(lm_device_t *pdev, u16_t *attn_bits, u16_t *attn_ack);

/**Genrate a general attention on all functions but this one,
 * which causes them to update their link status and CMNG state
 * from SHMEM.
 *
 * @param pdev the LM device
 */
void sync_link_status(lm_device_t *pdev);
/**
 * @description
 * Calculates BW according to current linespeed and MF
 * configuration  of the function in Mbps.
 * @param pdev
 * @param link_speed - Port rate in Mbps.
 * @param vnic
 *
 * @return u16
 * Return the max BW of the function in Mbps.
 */
u16_t
lm_get_max_bw(IN const lm_device_t  *pdev,
              IN const u32_t        link_speed,
              IN const u8_t         vnic);

/**Update CMNG and link info from SHMEM and configure the
 * firmware to the right CMNG values if this device is the PMF.
 *
 * @note This function must be called under PHY_LOCK
 *
 * @param pdev the LM device
 */
void lm_reload_link_and_cmng(lm_device_t *pdev);

/* Returns the number of toggled bits in a 32 bit integer
 * n - integer to count its '1' bits
 */
u32_t count_bits(u32_t n);

/* copy the new values of the status block prod_index for each strom into the local copy we hold in the lm_device
 *
 * Parameters:
 * pdev   - this is the LM device
 * sb_idx - this is the index where the status block lies in the array under the lm_device
 */
void lm_update_fp_hc_indices(lm_device_t *pdev, u8_t igu_sb_id, u32_t *activity_flg, u8_t *drv_rss_id);
void lm_update_def_hc_indices(lm_device_t *pdev, u8_t sb_id, u32_t *activity_flg);

void lm_57710A0_dbg_intr( struct _lm_device_t * pdev );

/* mdio access functions*/
lm_status_t
lm_mwrite(
    lm_device_t *pdev,
    u32_t reg,
    u32_t val);

lm_status_t
lm_mread(
    lm_device_t *pdev,
    u32_t reg,
    u32_t *ret_val);

lm_status_t
lm_m45write(
    lm_device_t *pdev,
    u32_t reg,
    u32_t addr,
    u32_t val);

lm_status_t
lm_m45read(
    lm_device_t *pdev,
    u32_t reg,
    u32_t addr,
    u32_t *ret_val);

lm_status_t
lm_phy45_read(
    lm_device_t *pdev,
    u8_t  phy_addr,
    u8_t  dev_addr,
    u16_t reg, // offset
    u16_t *ret_val);

lm_status_t
lm_phy45_write(
    lm_device_t *pdev,
    u8_t  phy_addr,
    u8_t dev_addr,
    u16_t reg, // offset
    u16_t val);

lm_status_t
lm_set_phy_addr(
         lm_device_t *pdev,
         u8_t addr);

void
lm_reset_link(lm_device_t *pdev);

u32_t
lm_nvram_query(
    lm_device_t *pdev,
    u8_t reset_flash_block,
    u8_t no_hw_mod);

void
lm_nvram_init(
    lm_device_t *pdev,
    u8_t reset_flash_block);

lm_status_t
lm_nvram_read(
    lm_device_t *pdev,
    u32_t offset,
    u32_t *ret_buf,
    u32_t buf_size);        /* Must be a multiple of 4. */

lm_status_t
lm_nvram_write(
    lm_device_t *pdev,
    u32_t offset,
    u32_t *data_buf,
    u32_t buf_size);        /* Must be a multiple of 4. */

void
lm_reg_rd_ind(
    lm_device_t *pdev,
    u32_t offset,
    u32_t *ret);

void
lm_reg_wr_ind(
    lm_device_t *pdev,
    u32_t offset,
    u32_t val);

void
lm_reg_rd_ind_imp(
    lm_device_t *pdev,
    u32_t offset,
    u32_t *ret);

void
lm_reg_wr_ind_imp(
    lm_device_t *pdev,
    u32_t offset,
    u32_t val);

lm_status_t
lm_init_mac_link(
    lm_device_t *pdev);

//TODO check if we need that when MCP ready
u8_t
fw_reset_sync(
    lm_device_t *pdev,
    lm_reason_t reason,
    u32_t msg_data,
    u32_t fw_ack_timeout_us);   /* timeout in microseconds. */

// mcp interface
lm_status_t
lm_mcp_submit_cmd(
    lm_device_t *pdev,
    u32_t drv_msg);

lm_status_t
lm_mcp_get_resp(
    lm_device_t *pdev);


lm_coalesce_buffer_t *
lm_get_coalesce_buffer(
    IN lm_device_t      *pdev,
    IN lm_tx_chain_t    *txq,
    IN u32_t            buf_size);


void
lm_put_coalesce_buffer(
    IN lm_device_t          *pdev,
    IN lm_tx_chain_t        *txq,
    IN lm_coalesce_buffer_t *coalesce_buf);

void lm_reset_device_if_undi_active(
    IN struct _lm_device_t *pdev);

void
lm_cmng_init(
    struct _lm_device_t *pdev,
    u32_t port_rate);

lm_status_t lm_get_pcicfg_mps_mrrs(lm_device_t * pdev);

void lm_set_pcie_nfe_report( lm_device_t *pdev);


void lm_clear_non_def_status_block(struct _lm_device_t *pdev,
                              u8_t  sb_id,
                              u8_t  port);

void lm_init_non_def_status_block(struct _lm_device_t *pdev,
                              u8_t  sb_id,
                              u8_t  port);

void lm_eth_init_command_comp(struct _lm_device_t *pdev, struct common_ramrod_eth_rx_cqe *cqe);


/*******************************************************************************
 * Description:
 *
 * Return:
 ******************************************************************************/
void
lm_pfc_handle_pfc(
    lm_device_t *pdev);
/*******************************************************************************
 * Register access macros.
 ******************************************************************************/

#ifdef __BIG_ENDIAN
#define CHANGE_ENDIANITY  TRUE
#else
#define CHANGE_ENDIANITY  FALSE
#endif

// do not call this macro directly from the code!
#define REG_WR_DMAE_LEN_IMP(_pdev,_reg_offset, _addr_src, _b_src_is_zeroed, _len, le32_swap) lm_dmae_command_split( _pdev,\
                                                                                             &_pdev->vars.lm_dmae_ctx_arr[LM_DMAE_CMD_WB_ACCESS],\
                                                                                             lm_dmae_mem_type_pci,\
                                                                                             lm_dmae_mem_type_grc,\
                                                                                             (void*)(_addr_src),\
                                                                                             (void*)((mm_int_ptr_t)((_reg_offset)>>2)),\
                                                                                             _b_src_is_zeroed,\
                                                                                             TRUE,/* virtual memory */\
                                                                                             CHANGE_ENDIANITY,\
                                                 le32_swap, \
                                                                                             _len )

// do not call this macro directly from the code!
#define REG_RD_DMAE_LEN_IMP(_pdev,_reg_offset, _addr_dst, _b_src_is_zeroed, _len) lm_dmae_command_split( _pdev,\
                                                                                             &_pdev->vars.lm_dmae_ctx_arr[LM_DMAE_CMD_WB_ACCESS],\
                                                                                             lm_dmae_mem_type_grc,\
                                                                                             lm_dmae_mem_type_pci,\
                                                                                             (void*)((mm_int_ptr_t)((_reg_offset)>>2)),\
                                                                                             (void*)(_addr_dst),\
                                                                                             _b_src_is_zeroed,\
                                                                                             TRUE,/* virtual memory */\
                                                                                             CHANGE_ENDIANITY,\
                                                 0, \
                                                                                             _len )

// Macro for writing a buffer to destination address using DMAE when data given is in VIRTUAL ADDRESS,
#define VIRT_WR_DMAE_LEN(_pdev, _src_addr, _dst_addr, _len, le32_swap) REG_WR_DMAE_LEN_IMP(_pdev, _dst_addr, _src_addr, FALSE, _len, le32_swap)

// Macro for writing a buffer to destination address using DMAE when data given is in PHYSICAL ADDRESS,
#define PHYS_WR_DMAE_LEN(_pdev, _src_addr, _dst_addr, _len) \
do { \
    lm_address_t _dst_lm_addr; \
    _dst_lm_addr.as_u64 = (_dst_addr >> 2);    /* destination is grc, so we have to translate address to dwords */\
    lm_dmae_command_split_imp( _pdev,\
                               &_pdev->vars.lm_dmae_ctx_arr[LM_DMAE_CMD_WB_ACCESS],\
                               lm_dmae_mem_type_pci,\
                               lm_dmae_mem_type_grc,\
                               _src_addr,\
                               _dst_lm_addr,\
                               FALSE,\
                               FALSE,/* virtual memory */\
                               CHANGE_ENDIANITY,\
                   0, \
                               _len );\
} while (0)

// Macro for copying physical buffer using DMAE,
#define PHYS_COPY_DMAE_LEN(_pdev, _src_addr, _dst_addr, _len) \
            lm_dmae_command_split_imp( _pdev,\
                                       &_pdev->vars.lm_dmae_ctx_arr[LM_DMAE_CMD_PCI2PCI_COPY],\
                                       lm_dmae_mem_type_pci,\
                                       lm_dmae_mem_type_pci,\
                                       _src_addr,\
                                       _dst_addr,\
                                       FALSE,\
                                       FALSE,/* virtual memory */\
                                       TRUE,/* change endianity */\
                                       0, \
                                       _len )
// write a buffer to destination address using DMAE
#define REG_WR_DMAE_LEN(_pdev,_reg_offset, _addr_src, _len) REG_WR_DMAE_LEN_IMP(_pdev, _reg_offset, _addr_src, FALSE, _len, 0)

// read from a buffer to destination address using DMAE
#define REG_RD_DMAE_LEN(_pdev,_reg_offset, _addr_dst, _len) REG_RD_DMAE_LEN_IMP(_pdev,_reg_offset, _addr_dst, FALSE, _len)

// write a zeroed buffer to destination address using DMAE
#define REG_WR_DMAE_LEN_ZERO(_pdev,_reg_offset, _len) REG_WR_DMAE_LEN_IMP(_pdev,_reg_offset, pdev->vars.zero_buffer, TRUE, _len, 0)

// Write to regiters, value of length 64 bit
#define REG_WR_DMAE(_pdev,_reg_offset, _addr_src ) REG_WR_DMAE_LEN(_pdev,_reg_offset, _addr_src, 2)

// Read from regiters, value of length 64 bit
#define REG_RD_DMAE(_pdev,_reg_offset, _addr_dst ) REG_RD_DMAE_LEN(_pdev,_reg_offset, _addr_dst, 2)




/* Indirect register access. */
#define REG_RD_IND(_pdev, _reg_offset, _ret)    lm_reg_rd_ind(_pdev, (_reg_offset), _ret)
#define REG_WR_IND(_pdev, _reg_offset, _val)    lm_reg_wr_ind(_pdev, (_reg_offset), _val)

#ifndef __LINUX
/* BAR write32 via register address */
#define LM_BAR_WR32_ADDRESS(_pdev, _address, _val) \
    *((u32_t volatile *) (_address))=(_val); \
    mm_write_barrier()
#else
/* BAR write32 via register address */
#define LM_BAR_WR32_ADDRESS(_pdev, _address, _val) \
    mm_io_write_dword(_pdev, _address, _val)
#endif


#if !(defined(UEFI) || defined(__LINUX))
/* BAR read8 via register offset and specific bar */
#define LM_BAR_RD8_OFFSET(_pdev, _bar, _offset, _ret) \
    mm_read_barrier(); \
    *(_ret) = *((u8_t volatile *) ((u8_t *) (_pdev)->vars.mapped_bar_addr[(_bar)] + (_offset)))
/* BAR read16 via register offset and specific bar */
#define LM_BAR_RD16_OFFSET(_pdev, _bar, _offset, _ret) \
    mm_read_barrier(); \
    *(_ret) = *((u16_t volatile *) ((u8_t *) (_pdev)->vars.mapped_bar_addr[(_bar)]+(_offset)))

/* BAR read32 via register offset and specific bar */
#define LM_BAR_RD32_OFFSET(_pdev, _bar, _offset, _ret) \
    mm_read_barrier(); \
    *(_ret) = *((u32_t volatile *) ((u8_t *) (_pdev)->vars.mapped_bar_addr[(_bar)]+(_offset)))

/* BAR read64 via register offset and specific bar */
#define LM_BAR_RD64_OFFSET(_pdev, _bar, _offset, _ret) \
    mm_read_barrier(); \
    *(_ret) = *((u64_t volatile *) ((u8_t *) (_pdev)->vars.mapped_bar_addr[(_bar)]+(_offset)))

/* BAR write8 via register offset and specific bar */
#define LM_BAR_WR8_OFFSET(_pdev, _bar, _offset, _val) \
    *((u8_t volatile *) ((u8_t *) (_pdev)->vars.mapped_bar_addr[(_bar)]+(_offset)))=(_val); \
    mm_write_barrier()

/* BAR write16 via register offset and specific bar */
#define LM_BAR_WR16_OFFSET(_pdev, _bar, _offset, _val) \
    *((u16_t volatile *) ((u8_t *) (_pdev)->vars.mapped_bar_addr[(_bar)]+(_offset)))=(_val); \
    mm_write_barrier()

/* BAR write32 via register offset and specific bar */
#define LM_BAR_WR32_OFFSET(_pdev, _bar, _offset, _val) \
    *((u32_t volatile *) ((u8_t *) (_pdev)->vars.mapped_bar_addr[(_bar)]+(_offset)))=(_val); \
    mm_write_barrier()

/* BAR write64 via register offset and specific bar */
#define LM_BAR_WR64_OFFSET(_pdev, _bar, _offset, _val) \
    *((u64_t volatile *) ((u8_t *) (_pdev)->vars.mapped_bar_addr[(_bar)]+(_offset)))=(_val); \
    mm_write_barrier()
#else
#define LM_BAR_RD8_OFFSET(_pdev, _bar, _offset, _ret) \
    mm_bar_read_byte(_pdev, _bar, _offset, _ret)
#define LM_BAR_RD16_OFFSET(_pdev, _bar, _offset, _ret) \
    mm_bar_read_word(_pdev, _bar, _offset, _ret)
#define LM_BAR_RD32_OFFSET(_pdev, _bar, _offset, _ret) \
    mm_bar_read_dword(_pdev, _bar, _offset, _ret)
#define LM_BAR_RD64_OFFSET(_pdev, _bar, _offset, _ret) \
    mm_bar_read_ddword(_pdev, _bar, _offset, _ret)
#define LM_BAR_WR8_OFFSET(_pdev, _bar, _offset, _val) \
    mm_bar_write_byte(_pdev, _bar, _offset, _val)
#define LM_BAR_WR16_OFFSET(_pdev, _bar, _offset, _val) \
    mm_bar_write_word(_pdev, _bar, _offset, _val)
#define LM_BAR_WR32_OFFSET(_pdev, _bar, _offset, _val) \
    mm_bar_write_dword(_pdev, _bar, _offset, _val)
#define LM_BAR_WR64_OFFSET(_pdev, _bar, _offset, _val) \
    mm_bar_write_ddword(_pdev, _bar, _offset, _val)
#endif

#ifndef USER_LINUX

#if DBG && LOG_REG_ACCESS

#define LOG_REG_RD(_pdev, _offset, _val)                                   \
    if((_pdev)->params.test_mode & TEST_MODE_LOG_REG_ACCESS)               \
    {                                                                      \
        DbgMessage2(_pdev, INFORM, "rd 0x%04x = 0x%08x\n", _offset, _val); \
    }

#define LOG_REG_WR(_pdev, _offset, _val)                                 \
    if((_pdev)->params.test_mode & TEST_MODE_LOG_REG_ACCESS)             \
    {                                                                    \
        DbgMessage2(_pdev, INFORM, "wr 0x%04x 0x%08x\n", _offset, _val); \
    }

#else

#define LOG_REG_RD(_pdev, _offset, _val)
#define LOG_REG_WR(_pdev, _offset, _val)

#endif /* DBG */

#endif /* USER_LINUX */

#if defined(__SunOS)

#ifdef __SunOS_MDB

/* Solaris debugger (MDB) doesn't have access to ddi_get/put routines */

static __inline u32_t _reg_rd(struct _lm_device_t * pdev, u32_t reg_offset)
{
    u32_t val;
    LM_BAR_RD32_OFFSET(pdev, BAR_0, reg_offset, &val);
    return val;
}

#define REG_RD(_pdev, _reg_offset) _reg_rd(_pdev, _reg_offset)
#define VF_REG_RD(_pdev, _reg_offset) _reg_rd(_pdev, _reg_offset)

#define REG_WR(_pdev, _reg_offset, _val)                     \
    do {                                                     \
        LOG_REG_WR(_pdev, (u32_t)(_reg_offset), _val);       \
        LM_BAR_WR32_OFFSET(_pdev, BAR_0, _reg_offset, _val); \
    } while (0)

#define VF_REG_WR(_pdev, _reg_offset, _val) REG_WR(_pdev, _reg_offset, _val)

#else /* __SunOS && !__SunOS_MDB */

#define REG_RD(_pdev, _reg_offset)                                         \
    ddi_get32((_pdev)->vars.reg_handle[BAR_0],                             \
              (uint32_t *)((caddr_t)(_pdev)->vars.mapped_bar_addr[BAR_0] + \
                           (_reg_offset)))

#define REG_WR(_pdev, _reg_offset, _val)                                    \
    ddi_put32((_pdev)->vars.reg_handle[BAR_0],                              \
               (uint32_t *)((caddr_t)(_pdev)->vars.mapped_bar_addr[BAR_0] + \
                            (_reg_offset)),                                 \
              (_val))                                                       \

#define VF_REG_RD(_pdev, _reg_offset)                                      \
    ddi_get32((_pdev)->vars.reg_handle[BAR_0],                             \
              (uint32_t *)((caddr_t)(_pdev)->vars.mapped_bar_addr[BAR_0] + \
                           (_reg_offset)))

#define VF_REG_WR(_pdev, _reg_offset, _val)                                 \
    ddi_put32((_pdev)->vars.reg_handle[BAR_0],                              \
               (uint32_t *)((caddr_t)(_pdev)->vars.mapped_bar_addr[BAR_0] + \
                            (_reg_offset)),                                 \
              (_val))

#endif /* __SunOS_MDB */

#elif defined (_VBD_CMD_)

//we repeat this function's signature here because including everest_sim.h leads to a circular dependency.
void vbd_cmd_on_reg_write(lm_device_t* pdev, u32_t offset);

static __inline u32_t _reg_rd(struct _lm_device_t * pdev, u32_t reg_offset)
{
    u32_t val;
    DbgBreakIf(IS_VFDEV(pdev));
    LM_BAR_RD32_OFFSET(pdev, BAR_0, reg_offset, &val);
    LOG_REG_RD(pdev, (reg_offset), val);
    return val;
}

/* Register access via register name. Macro returns a value */
#define REG_RD(_pdev, _reg_offset) _reg_rd(_pdev, _reg_offset)

static __inline u32_t _vf_reg_rd(struct _lm_device_t * pdev, u32_t reg_offset)
{
    u32_t val;
    LM_BAR_RD32_OFFSET(pdev, BAR_0, reg_offset, &val);
    LOG_REG_RD(pdev, (reg_offset), val);
    return val;
}

#define VF_REG_RD(_pdev, _reg_offset) _reg_rd(_pdev, _reg_offset)

// Offset passed to LOG_REG_WR is now without the bar address!
#define REG_WR(_pdev, _reg_offset, _val) \
    do {  \
        DbgBreakIf(IS_VFDEV(_pdev)); \
        LOG_REG_WR(_pdev, (u32_t)(_reg_offset), _val); \
        LM_BAR_WR32_OFFSET(_pdev, BAR_0, _reg_offset, _val); \
        vbd_cmd_on_reg_write(_pdev, _reg_offset);\
    } while (0)

#define VF_REG_WR(_pdev, _reg_offset, _val) \
    do {  \
        LOG_REG_WR(_pdev, (u32_t)(_reg_offset), _val); \
        LM_BAR_WR32_OFFSET(_pdev, BAR_0, _reg_offset, _val); \
        vbd_cmd_on_reg_write(_pdev, _reg_offset);\
    } while (0)


#elif !defined(USER_LINUX)

static __inline u32_t _reg_rd(struct _lm_device_t * pdev, u32_t reg_offset)
{
    u32_t val;
    DbgBreakIf(IS_VFDEV(pdev));
    LM_BAR_RD32_OFFSET(pdev, BAR_0, reg_offset, &val);
    LOG_REG_RD(pdev, (reg_offset), val);
    return val;
}

/* Register access via register name. Macro returns a value */
#define REG_RD(_pdev, _reg_offset) _reg_rd(_pdev, _reg_offset)

static __inline u32_t _vf_reg_rd(struct _lm_device_t * pdev, u32_t reg_offset)
{
    u32_t val;
    LM_BAR_RD32_OFFSET(pdev, BAR_0, reg_offset, &val);
    LOG_REG_RD(pdev, (reg_offset), val);
    return val;
}

#define VF_REG_RD(_pdev, _reg_offset) _reg_rd(_pdev, _reg_offset)

// Offset passed to LOG_REG_WR is now without the bar address!
#define REG_WR(_pdev, _reg_offset, _val) \
    do {  \
        DbgBreakIf(IS_VFDEV(_pdev)); \
        LOG_REG_WR(_pdev, (u32_t)(_reg_offset), _val); \
        LM_BAR_WR32_OFFSET(_pdev, BAR_0, _reg_offset, _val); \
    } while (0)

#define VF_REG_WR(_pdev, _reg_offset, _val) \
    do {  \
        LOG_REG_WR(_pdev, (u32_t)(_reg_offset), _val); \
        LM_BAR_WR32_OFFSET(_pdev, BAR_0, _reg_offset, _val); \
    } while (0)

#endif /* USER_LINUX */

/* TBA: optionally add LOG_REG_WR as in Teton to write 8/16/32*/

// special macros for reading from shared memory

/* TBD - E1H: all shmen read/write operations currenly use FUNC_ID for offset calculatio. This may not be right! MCP TBD*/
#define LM_SHMEM_READ_IMP(_pdev,_offset,_ret,_shmem_base_name) \
    LM_BAR_RD32_OFFSET((_pdev),BAR_0,(_pdev)->hw_info._shmem_base_name + _offset,(_ret));

#define LM_SHMEM_READ(_pdev,_offset,_ret)  LM_SHMEM_READ_IMP(_pdev,_offset,_ret, shmem_base );
#define LM_SHMEM2_READ(_pdev,_offset,_ret) LM_SHMEM_READ_IMP(_pdev,_offset,_ret, shmem_base2 );
#define LM_MFCFG_READ(_pdev,_offset,_ret) LM_SHMEM_READ_IMP(_pdev,_offset,_ret, mf_cfg_base );

#define LM_SHMEM_WRITE_IMP(_pdev,_offset,_val,_shmem_base_name) \
    LM_BAR_WR32_OFFSET((_pdev),BAR_0,(_pdev)->hw_info._shmem_base_name + _offset,(_val));

#define LM_SHMEM_WRITE(_pdev,_offset,_val)  LM_SHMEM_WRITE_IMP(_pdev,_offset,_val,shmem_base);
#define LM_SHMEM2_WRITE(_pdev,_offset,_val) LM_SHMEM_WRITE_IMP(_pdev,_offset,_val,shmem_base2);
#define LM_MFCFG_WRITE(_pdev,_offset,_val) LM_SHMEM_WRITE_IMP(_pdev,_offset,_val,mf_cfg_base);

/* Macros for read/write to internal memory of storms */
#define LM_INTMEM_READ8(_pdev,_offset,_ret,_type) \
    DbgMessage2(pdev, INFORMi, "LM_INTMEM_READ8() inside! storm:%s address:0x%x\n",#_type,_type); \
    LM_BAR_RD8_OFFSET((_pdev),BAR_0,((_type)+(_offset)),(_ret));

#define LM_INTMEM_WRITE8(_pdev,_offset,_val,_type) \
    DbgMessage2(pdev, INFORMi, "LM_INTMEM_WRITE8() inside! storm:%s address:0x%x\n",#_type,_type); \
    LM_BAR_WR8_OFFSET((_pdev),BAR_0,((_type)+(_offset)),(_val));

#define LM_INTMEM_READ16(_pdev,_offset,_ret,_type) \
    DbgMessage2(pdev, INFORMi, "LM_INTMEM_READ16() inside! storm:%s address:0x%x\n",#_type,_type); \
    LM_BAR_RD16_OFFSET((_pdev),BAR_0,((_type)+(_offset)),(_ret));

#define LM_INTMEM_WRITE16(_pdev,_offset,_val,_type) \
    DbgMessage4(pdev, INFORMi, "LM_INTMEM_WRITE16() inside! storm:%s address:0x%x offset=%x val=%x\n",#_type,_type, _offset, _val); \
    LM_BAR_WR16_OFFSET((_pdev),BAR_0,((_type)+(_offset)),(_val));

#define LM_INTMEM_READ32(_pdev,_offset,_ret,_type) \
    DbgMessage2(pdev, INFORMi, "LM_INTMEM_READ32() inside! storm:%s address:0x%x\n",#_type,_type); \
    LM_BAR_RD32_OFFSET((_pdev),BAR_0,((_type)+(_offset)),(_ret));

#define LM_INTMEM_WRITE32(_pdev,_offset,_val,_type) \
    DbgMessage2(pdev, INFORMi, "LM_INTMEM_WRITE32() inside! storm:%s address:0x%x\n",#_type,_type); \
    LM_BAR_WR32_OFFSET((_pdev),BAR_0,((_type)+(_offset)),(_val));

#define LM_INTMEM_READ64(_pdev,_offset,_ret,_type) \
    DbgMessage2(pdev, INFORMi, "LM_INTMEM_READ64() inside! storm:%s address:0x%x\n",#_type,_type); \
    LM_BAR_RD64_OFFSET((_pdev),BAR_0,((_type)+(_offset)),(_ret));

#define LM_INTMEM_WRITE64(_pdev,_offset,_val,_type) \
    DbgMessage2(pdev, INFORMi, "LM_INTMEM_WRITE64() inside! storm:%s address:0x%x\n",#_type,_type); \
    LM_BAR_WR64_OFFSET((_pdev),BAR_0,((_type)+(_offset)),(_val));
//________________________________________________________________________________


#define DEFAULT_WAIT_INTERVAL_MICSEC 30 // wait interval microseconds

u32_t reg_wait_verify_val(struct _lm_device_t * pdev, u32_t reg_offset, u32_t excpected_val, u32_t total_wait_time_ms );
#if !defined(_VBD_CMD_)
#define REG_WAIT_VERIFY_VAL(_pdev, _reg_offset, _excpected_val, _total_wait_time_ms ) \
    reg_wait_verify_val(_pdev, _reg_offset, _excpected_val, _total_wait_time_ms );
#else
/* For VBD_CMD: we don't verify values written... */
#define REG_WAIT_VERIFY_VAL(_pdev, _reg_offset, _excpected_val, _total_wait_time_ms ) 0
#endif

#define DPM_TRIGER_TYPE 0x40

#if defined(EMULATION_DOORBELL_FULL_WORKAROUND)
#define _DOORBELL(PDEV,CID,VAL)  do{\
    MM_WRITE_DOORBELL(PDEV,BAR_1,CID,VAL);\
    } while(0)

static __inline void DOORBELL(lm_device_t *pdev, u32_t cid, u32_t val)
{
    u32_t db_fill;
    u32_t wait_cnt = 0;

    if ((CHIP_REV(pdev) == CHIP_REV_EMUL) || (CHIP_REV(pdev) == CHIP_REV_FPGA)) {
        /* wait while doorbells are blocked */
        while(pdev->vars.doorbells_blocked) {
            wait_cnt++; /* counter required to avoid Watcom warning */
        }

        if(mm_atomic_dec(&pdev->vars.doorbells_cnt) == 0) {

            mm_atomic_set(&pdev->vars.doorbells_cnt, DOORBELL_CHECK_FREQUENCY);

            db_fill=REG_RD(PFDEV(pdev),DORQ_REG_DQ_FILL_LVLF);

            if (db_fill > ALLOWED_DOORBELLS_HIGH_WM) {

                DbgMessage1(pdev, WARN,
                            "EMULATION_DOORBELL_FULL_WORKAROUND: db_fill=%d, doorbell in busy wait!\n",
                            db_fill);

                /* block additional doorbells */
                pdev->vars.doorbells_blocked = 1;

                /* busy wait for doorbell capacity */

                do {
                    db_fill=REG_RD(PFDEV(pdev),DORQ_REG_DQ_FILL_LVLF);
                    if (db_fill == 0xffffffff) {
                        DbgMessage(pdev, FATAL, "DOORBELL: fill level 0xffffffff\n");
                        break;
                    }
                } while (db_fill  > ALLOWED_DOORBELLS_LOW_WM);

                /* incr statistics */
                pdev->vars.doorbells_high_wm_reached++;

                /* unblock additional doorbells */
                pdev->vars.doorbells_blocked = 0;
            }
        }
    }

    _DOORBELL(pdev,cid,val);
}

#else

// need to change LM_PAGE_SIZE to OS page size + when we will have 2 bars BAR_DOORBELL_OFFSET is not needed.
#define DOORBELL(PDEV,CID,VAL)  do{\
    MM_WRITE_DOORBELL(PDEV,BAR_1,CID,VAL);\
    } while(0)

#endif /* defined(EMULATION_DOORBELL_FULL_WORKAROUND) */


#define HW_CID(pdev,x) (x |(PORT_ID(pdev) << 23 | VNIC_ID(pdev) << 17))
// used on a CID received from the HW - ignore bits 17, 18 and 23 (though 19-22 can be ignored as well)
#define SW_CID(x)    (x & COMMON_RAMROD_ETH_RX_CQE_CID & ~0x860000)


u64_t lm_get_timestamp_of_recent_cid_recycling(struct _lm_device_t *pdev);

static u8_t __inline lm_sb_id_from_chain(struct _lm_device_t *pdev, u8_t chain_idx)
{
    u8_t sb_id = 0 ;

    if (CID_TO_RSS_ID(chain_idx) >= LM_MAX_RSS_CHAINS(pdev))
    {
        if(CID_TO_RSS_ID(chain_idx) == OOO_CID(pdev))
        {
             //TODO_OOO Ask Michal regarding E2
            sb_id = LM_OOO_SB(pdev);
        }
        else
        {
            /* mapping iscsi / fcoe cids to the default status block */
            sb_id = DEF_STATUS_BLOCK_INDEX;
        }
    }
    else
    {
        sb_id = RSS_ID_TO_SB_ID(CID_TO_RSS_ID(chain_idx));
    }
    return sb_id;
}
static void __inline lm_set_virt_mode(struct _lm_device_t *pdev, u8_t device_type, u8_t virtualization_type)
{
    DbgBreakIf(!pdev);
    if ((pdev->params.device_type == DEVICE_TYPE_PF) && (pdev->params.virtualization_type == VT_NONE)) {
        switch (device_type) {
        case DEVICE_TYPE_PF:
            pdev->params.device_type = device_type;
            switch (virtualization_type) {
            case VT_NONE:
                break;
            case VT_BASIC_VF:
                pdev->params.virtualization_type = virtualization_type;
                break;
            case VT_CHANNEL_VF:
            default:
                DbgMessage1(pdev,FATAL,"Master PF mode %d is not supported in virt.mode\n",virtualization_type);
                DbgBreak();
                break;
            }
            break;
        case DEVICE_TYPE_VF:
            pdev->params.device_type = device_type;
            switch (virtualization_type) {
            case VT_BASIC_VF:
            case VT_CHANNEL_VF:
                pdev->params.virtualization_type = virtualization_type;
                break;
            case VT_NONE:
                DbgMessage(pdev,FATAL,"VF mode is mandatory parameter\n");
                DbgBreak();
                break;
            default:
                DbgMessage1(pdev,FATAL,"VF mode %d is not supported\n",virtualization_type);
                DbgBreak();
                break;
            }
            break;
        default:
            DbgMessage1(pdev,FATAL,"Device type %d is not supported in virt.mode\n",device_type);
            DbgBreak();
        }
    } else {
        DbgMessage2(pdev, FATAL, "Virt.mode is set already (%d,%d)\n",device_type,virtualization_type);
    }
    DbgMessage2(pdev, WARN, "Virt.mode is set as (%d,%d)\n", pdev->params.device_type, pdev->params.virtualization_type);
}

u32_t lm_get_num_of_cashed_grq_bds(struct _lm_device_t *pdev);
void lm_set_waitp(lm_device_t *pdev);
u8_t lm_get_port_id_from_func_abs( const u32_t chip_num,  const lm_chip_port_mode_t lm_chip_port_mode, const u8_t abs_func );
u8_t lm_get_abs_func_vector( const u32_t chip_num,  const lm_chip_port_mode_t chip_port_mode, const u8_t b_multi_vnics_mode, const u8_t path_id );

#endif /* _LM5710_H */
