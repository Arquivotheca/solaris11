/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2007 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *
 *
 * History:
 *    02/05/07 Alon Elhanani    Inception.
 ******************************************************************************/

#ifndef _LM_DMAE_H
#define _LM_DMAE_H

// defines
#define DMAE_SGL_MAX_COMMANDS       100         // max number of commands in a DMAE SGL (just as a limit - can be defined other number)
#define DMAE_GO_VALUE               0x1         // DMAE spec
#define DMAE_SGL_COMPLETION_VAL     0xD0AE      // local completion word value (for SGL)
#define DMAE_COMPLETION_VAL         0xD1AE      // local completion word value with edianity mode 2 (for regular command)
#define DMAE_COMPLETION_VAL_SWAPPED 0xAED10000  // local completion word value with edianity mode 3
#define DMAE_CMD_SIZE               14          // size of DMAE command structure

#define DMAE_MAX_RW_SIZE_E1        0x0400 // maximun size (in DW) of read/write commands (HW limit) - for  (chip id<=5710)

// up to 0xffff actually limit is 64KB-1 so 0x2000 dwords is 32KB
#define DMAE_MAX_RW_SIZE_NEW       0x2000 // maximun size of read/write commands (HW limit) - for E1.5 and above (chip id>5710)
#define DMAE_MAX_READ_SIZE         0x80   // due to a HW issue in E1, E1.5 A0, pci to pci and grc to pci operations are limited to 128 DWORDS

// max value for static allocations
#define DMAE_MAX_RW_SIZE_STATIC    max(DMAE_MAX_RW_SIZE_E1,DMAE_MAX_RW_SIZE_NEW)

#define DMAE_MAX_RW_SIZE(pdev)    (CHIP_IS_E1(pdev) ?  DMAE_MAX_RW_SIZE_E1 : DMAE_MAX_RW_SIZE_NEW)


#define DMAE_STATS_GET_PORT_CMD_IDX(port_num,cmd_idx) DMAE_STATS_PORT_##port_num##_CMD_IDX_##cmd_idx

#define DMAE_STATS_PORT_0_CMD_IDX_0       DMAE_CMD_DRV_0
#define DMAE_STATS_PORT_0_CMD_IDX_1       DMAE_CMD_DRV_1
#define DMAE_STATS_PORT_1_CMD_IDX_0       DMAE_CMD_DRV_2
#define DMAE_STATS_PORT_1_CMD_IDX_1       DMAE_CMD_DRV_3
#define DMAE_WB_ACCESS_FUNCTION_CMD(_idx) DMAE_CMD_DRV_4+(_idx)
#define DMAE_COPY_PCI_PCI_PORT_0_CMD      DMAE_CMD_DRV_12
#define DMAE_COPY_PCI_PCI_PORT_1_CMD      DMAE_CMD_DRV_13

typedef enum {
    lm_dmae_mem_type_grc,
    lm_dmae_mem_type_pci
} lm_dmae_mem_type;

// internal lm strucutre used for dmae
// TODO - split to phys (like command context)
// TODO - common header with sgl_context
struct dmae_sgl_context
{
    u32_t                     completion_word ;  // this will be set to special value to indicate end of sgl
    u8_t                      cnt_executer_cmd ; // number of executer allocated commands
    u8_t                      idx_cmd_loader ;
    u8_t                      idx_cmd_executer ;
    u64_t                     dmae_sgl_cnt ; // number of times dmae sgl called
    lm_address_t              phys_addr ;
    struct dmae_cmd           loader ;
    volatile struct dmae_cmd  executer[1] ;      // multiple commands (at least 1 of course) - must be last field!
};

struct dmae_command_context_phys
{
    u32_t                     completion_word ;  // this will be set to special value to indicate end of dmae
    u32_t                     data[1] ;
};


struct dmae_command_context
{
    u8_t                              idx_cmd ;        // DMAE command index to use for this context
    lm_address_t                      phys_addr ;      // phys field address (dmae_command_context_phys)
    u32_t                             max_pci_length ; // in DW resultion
    u32_t                             cmd_post_cnt ;   // for debugging purpose only - count how many times this DMAE cmd was used 
    struct dmae_cmd                   command ;
    struct dmae_command_context_phys* phys ;
};

// Functions prototypes
u32_t lm_dmae_idx_to_go_cmd( u8_t idx );

lm_status_t lm_dmae_sgl_set_executer_cmd_read_grc_to_pci( IN struct _lm_device_t* pdev,
                                                          IN OUT void*            ctx,
                                                          IN const u8_t           cmd_seq,
                                                          IN const u32_t          reg_offset,
                                                          IN const lm_address_t   phys_addr,
                                                          IN const u16_t          length ) ;

void* lm_dmae_sgl_alloc( IN struct _lm_device_t* pdev,
                         IN const u8_t           idx_cmd_loader,
                         IN const u8_t           idx_cmd_executer,
                         IN const u8_t           cnt_executer_cmd ) ;

lm_status_t lm_dmae_sgl_setup( IN struct _lm_device_t* pdev,
                               IN OUT void* ctx ) ;


lm_status_t lm_dmae_sgl_go( IN struct _lm_device_t* pdev,
                            IN OUT void*    ctx ) ;

lm_status_t lm_dmae_sgl_is_done( IN OUT void* ctx ) ;

void lm_dmae_post_command( IN struct _lm_device_t*   pdev,
                           IN const u8_t             idx_cmd,
                           IN const struct dmae_cmd* command  ) ;

lm_status_t lm_dmae_command_alloc( IN struct _lm_device_t* pdev,
                                   IN OUT void*            ctx,
                                   IN const  u8_t          idx_cmd,
                                   IN const  u16_t         max_pci_length ) ;


lm_status_t lm_dmae_command( IN struct _lm_device_t*    pdev,
                             IN void*                   ctx,
                             IN const  lm_dmae_mem_type src_mem_type,
                             IN const  lm_dmae_mem_type dst_mem_type,
                             IN const  lm_address_t     src_addr,
                             IN OUT    lm_address_t     dst_addr,
                             IN const  u8_t             b_pci_addr_is_virt,
                             IN const  u8_t             change_endianity,
                             IN const  u8_t             le32_swap,
                             IN const  u16_t            length );

lm_status_t lm_dmae_command_split( IN struct _lm_device_t*    pdev,
                                   IN void*                   ctx,
                                   IN const  lm_dmae_mem_type src_mem_type,
                                   IN const  lm_dmae_mem_type dst_mem_type,
                                   IN const  void*            src_addr,
                                   IN OUT    void*            dst_addr,
                                   IN const  u8_t             b_src_is_zeroed,
                                   IN const  u8_t             b_pci_addr_is_virt,
                                   IN const  u8_t             change_endianity,
                                   IN const  u8_t             le32_swap,
                                   IN const  u32_t            length );

lm_status_t lm_dmae_command_split_imp( IN struct _lm_device_t*    pdev,
                                       IN void*                   ctx,
                                       IN const  lm_dmae_mem_type src_mem_type,
                                       IN const  lm_dmae_mem_type dst_mem_type,
                                       IN const  lm_address_t     src_addr,
                                       IN OUT    lm_address_t     dst_addr,
                                       IN const  u8_t             b_src_is_zeroed,
                                       IN const  u8_t             b_pci_addr_is_virt,
                                       IN const  u8_t             change_endianity,
                                       IN const  u8_t             le32_swap,
                                       IN const  u32_t            length );

#endif// _LM_DMAE_H
