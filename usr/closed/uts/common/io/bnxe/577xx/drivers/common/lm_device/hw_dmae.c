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

#include "lm5710.h"


// converts index to DMAE command register name define
u32_t lm_dmae_idx_to_go_cmd( u8_t idx )
{
    u32_t ret = 0 ;
    switch( idx )
    {
    case 0:  ret = DMAE_REG_GO_C0;  break;
    case 1:  ret = DMAE_REG_GO_C1;  break;
    case 2:  ret = DMAE_REG_GO_C2;  break;
    case 3:  ret = DMAE_REG_GO_C3;  break;
    case 4:  ret = DMAE_REG_GO_C4;  break;
    case 5:  ret = DMAE_REG_GO_C5;  break;
    case 6:  ret = DMAE_REG_GO_C6;  break;
    case 7:  ret = DMAE_REG_GO_C7;  break;
    case 8:  ret = DMAE_REG_GO_C8;  break;
    case 9:  ret = DMAE_REG_GO_C9;  break;
    case 10: ret = DMAE_REG_GO_C10; break;
    case 11: ret = DMAE_REG_GO_C11; break;
    case 12: ret = DMAE_REG_GO_C12; break;
    case 13: ret = DMAE_REG_GO_C13; break;
    case 14: ret = DMAE_REG_GO_C14; break;
    case 15: ret = DMAE_REG_GO_C15; break;
    default:
        break;
    }
    return ret ;
}

// returns dmae sgl context
// allocate memory for commands and init general dmae fields (commands indexes and address)
// idx_cmd_loader   - dmae register command index
// idx_cmd_executer - dmae register command index
// cnt_executer_cmd - number of executer commands needed
// this fucntion should not write/read to/from bars (it's alloc)
void* lm_dmae_sgl_alloc( IN lm_device_t*  pdev,
                         IN const u8_t    idx_cmd_loader,
                         IN const u8_t    idx_cmd_executer,
                         IN const u8_t    cnt_executer_cmd )
{
    lm_address_t             phys_addr    = {{0}} ;
    u32_t                    alloc_size   = 0 ;
    struct dmae_sgl_context* dmae_sgl_ctx = NULL ;
    u8_t                     mm_cli_idx   = 0;

    DbgBreakIf(IS_VFDEV(pdev));
    // check valid params
    if( cnt_executer_cmd > DMAE_SGL_MAX_COMMANDS )
    {
        return NULL;
    }

    mm_cli_idx = LM_RESOURCE_COMMON;//!!DP mm_cli_idx_to_um_idx(LM_CLI_IDX_MAX);

    // we allocate contiguous memory (loader command + executer so it is number of commands - 1) -1 for the [1]
    alloc_size = sizeof( struct dmae_sgl_context ) + ( sizeof( struct dmae_cmd ) * (cnt_executer_cmd-1) ) ;

    dmae_sgl_ctx = mm_alloc_phys_mem( pdev, alloc_size, &phys_addr, PHYS_MEM_TYPE_NONCACHED, mm_cli_idx ) ;

    if CHK_NULL( dmae_sgl_ctx )
    {
        return NULL ;
    }

    // reset stucture to zero
    mm_mem_zero( dmae_sgl_ctx, alloc_size ) ;

    // set number of commands and other in params
    dmae_sgl_ctx->cnt_executer_cmd  = cnt_executer_cmd ;
    dmae_sgl_ctx->phys_addr         = phys_addr ;
    dmae_sgl_ctx->idx_cmd_loader    = idx_cmd_loader ;
    dmae_sgl_ctx->idx_cmd_executer  = idx_cmd_executer ;

    return dmae_sgl_ctx ;
}

// functions sets up loader and common executers dmae_sgl fields
lm_status_t lm_dmae_sgl_setup( IN lm_device_t* pdev,
                               IN OUT void*    ctx )
{
    lm_address_t             phys_addr    = {{0}} ;
    u32_t                    i            = 0 ;
    struct dmae_sgl_context* dmae_sgl_ctx = (struct dmae_sgl_context*)ctx ;
    lm_status_t              lm_status    = LM_STATUS_SUCCESS ;

    DbgBreakIf(IS_VFDEV(pdev));

    if CHK_NULL( dmae_sgl_ctx )
    {
        DbgBreakIf(!dmae_sgl_ctx) ;
        return LM_STATUS_INVALID_PARAMETER;
    }

    if CHK_NULL( pdev )
    {
        DbgBreakIf(!pdev) ;
        return LM_STATUS_INVALID_PARAMETER;
    }

    // check valid params
    if ERR_IF( dmae_sgl_ctx->cnt_executer_cmd > DMAE_SGL_MAX_COMMANDS )
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }

    // set loader command
    // ==================

    // opcode
    // ------

    // reset to 0!
    dmae_sgl_ctx->loader.opcode = 0 ;

    // src is PCI (0)
    dmae_sgl_ctx->loader.opcode       |= ( 0 << DMAE_CMD_SRC_SHIFT ) ;
    // dst is GRC (2)
    dmae_sgl_ctx->loader.opcode       |= ( 2 << DMAE_CMD_DST_SHIFT ) ;
    // comp dst is GRC (1)
    dmae_sgl_ctx->loader.opcode       |= ( 1 << DMAE_CMD_C_DST_SHIFT ) ;
    // comp do write to dst (1)
    dmae_sgl_ctx->loader.opcode       |= ( 1 << DMAE_CMD_C_TYPE_ENABLE_SHIFT ) ;
    // comp disable crc write to dst (0)
    dmae_sgl_ctx->loader.opcode       |= ( 0 << DMAE_CMD_C_TYPE_CRC_ENABLE_SHIFT ) ;
#ifdef __BIG_ENDIAN
    // Endianity mode 3
    dmae_sgl_ctx->loader.opcode       |= ( 3 << DMAE_CMD_ENDIANITY_SHIFT ) ;
#else
    // Endianity mode 2
    dmae_sgl_ctx->loader.opcode       |= ( 2 << DMAE_CMD_ENDIANITY_SHIFT ) ;
#endif
    // Port number
    dmae_sgl_ctx->loader.opcode       |= ( PORT_ID(pdev) << DMAE_CMD_PORT_SHIFT ) ;
    // crc reset - not relevant
    dmae_sgl_ctx->loader.opcode       |= ( 0 << DMAE_CMD_CRC_RESET_SHIFT ) ;
    // src address reset must be 0 for loader!
    dmae_sgl_ctx->loader.opcode       |= ( 0 << DMAE_CMD_SRC_RESET_SHIFT ) ;
    // dst address reset 1
    dmae_sgl_ctx->loader.opcode       |= ( 1 << DMAE_CMD_DST_RESET_SHIFT ) ;
    // VNIC id
    dmae_sgl_ctx->loader.opcode       |= ( VNIC_ID(pdev) << DMAE_CMD_E1HVN_SHIFT ) ;

    //calc first executer addr - advance to dmae_sgl_ctx->executer[0]
    phys_addr = dmae_sgl_ctx->phys_addr ;
    LM_INC64( &phys_addr, OFFSETOF( struct dmae_sgl_context, executer[0] ) );

    // src address (first executer on PCI)
    dmae_sgl_ctx->loader.src_addr_lo = phys_addr.as_u32.low ;
    dmae_sgl_ctx->loader.src_addr_hi = phys_addr.as_u32.high ;

    // dst address (cmd_idx_loader on GRC)  //exec command grc addr
    // The address to command X; row Y is to calculated as 14*X+Y
    // DMAE_URI - check address resultion (is it DWORD)
    dmae_sgl_ctx->loader.dst_addr_lo = DMAE_REG_CMD_MEM + (dmae_sgl_ctx->idx_cmd_executer*DMAE_CMD_SIZE*sizeof(u32_t)) ;

    dmae_sgl_ctx->loader.dst_addr_lo = dmae_sgl_ctx->loader.dst_addr_lo  / 4 ;
    dmae_sgl_ctx->loader.len         = sizeof(dmae_sgl_ctx->executer[0]) / 4;

    // Special handling for E1 HW DMAE operation: we give here the size we are writing MINUS 1,
    // since when 'no reset' is on (src address is 0 ), the DMAE advance pointer by
    // length + 1, so in order to comply, we send length-1
    // when relevant data struct we send is not bigger than lnegth-1,
    // in this specific case, we send struct size 14 when relevant data is 9
    // so even when we send 13 as length, it's ok, since we copy 13, 9 is intersting
    // and next time DMAE will read from +14 which is good for us
    if( CHIP_NUM(pdev) <= CHIP_NUM_5710 )
    {
        --dmae_sgl_ctx->loader.len ;
    }

    // complition is the executer go grc addr
    dmae_sgl_ctx->loader.comp_addr_lo = ( lm_dmae_idx_to_go_cmd(dmae_sgl_ctx->idx_cmd_executer) ) / 4 ; // DW res

    dmae_sgl_ctx->loader.comp_val     = DMAE_GO_VALUE ;

    // set executers commands common values
    for( i = 0; i<dmae_sgl_ctx->cnt_executer_cmd; i++ )
    {
        u8_t b_is_last = ( dmae_sgl_ctx->cnt_executer_cmd == i+1 ) ;

        // opcode
        // ------
        dmae_sgl_ctx->executer[i].opcode = 0 ;
        // src is GRC (1)
        dmae_sgl_ctx->executer[i].opcode       |= ( 1 << DMAE_CMD_SRC_SHIFT ) ;
        // dst is PCI (1)
        dmae_sgl_ctx->executer[i].opcode       |= ( 1 << DMAE_CMD_DST_SHIFT ) ;

        if( !b_is_last )
        {
            // comp dst is GRC (1)
            dmae_sgl_ctx->executer[i].opcode   |= ( 1 << DMAE_CMD_C_DST_SHIFT ) ;
        }
        else
        {
            // comp dst is PCI (0)
            dmae_sgl_ctx->executer[i].opcode   |= ( 0 << DMAE_CMD_C_DST_SHIFT ) ;
        }

        // comp do write to dst (1)
        dmae_sgl_ctx->executer[i].opcode       |= ( 1 << DMAE_CMD_C_TYPE_ENABLE_SHIFT ) ;
        // comp disable crc write to dst (0)
        dmae_sgl_ctx->executer[i].opcode       |= ( 0 << DMAE_CMD_C_TYPE_CRC_ENABLE_SHIFT ) ;
#ifdef __BIG_ENDIAN
    // Endianity mode 3
        dmae_sgl_ctx->executer[i].opcode       |= ( 3 << DMAE_CMD_ENDIANITY_SHIFT ) ;
#else
        // Endianity mode 2
        dmae_sgl_ctx->executer[i].opcode       |= ( 2 << DMAE_CMD_ENDIANITY_SHIFT ) ;
#endif
        // Port number
        dmae_sgl_ctx->executer[i].opcode       |= ( PORT_ID(pdev) << DMAE_CMD_PORT_SHIFT ) ;
        // crc reset - not relevant
        dmae_sgl_ctx->executer[i].opcode       |= ( 0 << DMAE_CMD_CRC_RESET_SHIFT ) ;
        // src address reset 1
        dmae_sgl_ctx->executer[i].opcode       |= ( 1 << DMAE_CMD_SRC_RESET_SHIFT ) ;
        // dst address reset 1
        dmae_sgl_ctx->executer[i].opcode       |= ( 1 << DMAE_CMD_DST_RESET_SHIFT ) ;
        // VNIC id
        dmae_sgl_ctx->executer[i].opcode       |= ( VNIC_ID(pdev) << DMAE_CMD_E1HVN_SHIFT ) ;

        if( !b_is_last )
        {
            // set loader go grc addr if not the last one
            dmae_sgl_ctx->executer[i].comp_addr_lo = ( lm_dmae_idx_to_go_cmd(dmae_sgl_ctx->idx_cmd_loader) ) / 4 ;

            // go value - go execute
            dmae_sgl_ctx->executer[i].comp_val     = DMAE_GO_VALUE ;
        }
        else
        {
            // set completion_word address to our context completion word
            // loader go grc addr - for last set to 0
            phys_addr = dmae_sgl_ctx->phys_addr ;
            LM_INC64( &phys_addr, OFFSETOF( struct dmae_sgl_context, completion_word ) ) ;

            dmae_sgl_ctx->executer[i].comp_addr_lo = dmae_sgl_ctx->phys_addr.as_u32.low ;
            dmae_sgl_ctx->executer[i].comp_addr_hi = dmae_sgl_ctx->phys_addr.as_u32.high ;

            // comp value - special indication for us
            dmae_sgl_ctx->executer[i].comp_val     = DMAE_SGL_COMPLETION_VAL ;
        }
    } // for loop

    return lm_status ;
}

// ctx - dmae sgl context
// cmd_seq - index of the executer command
// block_name - block base e.g. GRC_BASE
// reg_name
// phys_addr - on host - target buffer
// length - length of wtire op in DWORD resulation
// The fucntion sets address to be read from GRC
lm_status_t lm_dmae_sgl_set_executer_cmd_read_grc_to_pci( IN lm_device_t*        pdev,
                                                          IN OUT void*           ctx,
                                                          IN const u8_t          cmd_seq,
                                                          IN const u32_t         reg_offset,
                                                          IN const lm_address_t  phys_addr,
                                                          IN const u16_t         length )
{
    struct dmae_sgl_context* dmae_sgl_ctx = (struct dmae_sgl_context*)ctx ;

    DbgBreakIf(IS_VFDEV(pdev));

    if CHK_NULL( dmae_sgl_ctx )
    {
        DbgBreakIf(!dmae_sgl_ctx) ;
        return LM_STATUS_INVALID_PARAMETER;
    }

    if CHK_NULL( pdev )
    {
        DbgBreakIf(!pdev) ;
        return LM_STATUS_INVALID_PARAMETER;
    }

    if ERR_IF( cmd_seq >= dmae_sgl_ctx->cnt_executer_cmd )
    {
        DbgBreakMsg("Error!\n") ;
        return LM_STATUS_INVALID_PARAMETER;
    }
    dmae_sgl_ctx->executer[cmd_seq].src_addr_lo = reg_offset >> 2 ;// resultion should be DW

    // dst address
    dmae_sgl_ctx->executer[cmd_seq].dst_addr_lo = phys_addr.as_u32.low ;
    dmae_sgl_ctx->executer[cmd_seq].dst_addr_hi = phys_addr.as_u32.high ;
    dmae_sgl_ctx->executer[cmd_seq].len         = length ; // DW

    return LM_STATUS_SUCCESS ;
}

// Copy the loader command to DMAE - need to do it before every call - for source/dest address no reset...
// Due to parity checks error, we write zero for last 5 registers of command (9-13, zero based)
void lm_dmae_post_command( IN struct _lm_device_t*   pdev,
                           IN const u8_t             idx_cmd,
                           IN const struct dmae_cmd* command  )
{
    u8_t i = 0 ;

    DbgBreakIf(IS_VFDEV(pdev));

    if ( CHK_NULL(pdev) || CHK_NULL(command))
    {
        return;
    }

    // verify address is not NULL
    if ERR_IF( ( ( 0 == command->dst_addr_lo ) && ( command->dst_addr_hi == command->dst_addr_lo ) ) ||
               ( ( 0 == command->src_addr_lo ) && ( command->src_addr_hi == command->src_addr_lo ) ) )

    {
            DbgMessage6(pdev,FATAL,"lm_dmae_command: idx_cmd=%d len=0x%x src=0x%x:%x dst=0x%x:%x\n",
                       idx_cmd,
                       command->len,
                       command->src_addr_hi,
                       command->src_addr_lo,
                       command->dst_addr_hi,
                       command->dst_addr_lo );
            DbgBreakMsg("lm_dmae_command: Trying to write/read to NULL address\n");
    }

    // Copy the command to DMAE - need to do it before every call - for source/dest address no reset...
    // Due to parity checks error, we write zero for last 5 registers of command (9-13, zero based)
    for( i = 0 ; i < 14 ; i++ )
    {
        REG_WR( pdev,
                DMAE_REG_CMD_MEM+(idx_cmd*DMAE_CMD_SIZE*sizeof(u32_t))+i*sizeof(u32_t),
                i < 9 ? *(((u32_t*)command)+i) : 0 ) ;
    }

    REG_WR(pdev, lm_dmae_idx_to_go_cmd(idx_cmd), DMAE_GO_VALUE) ;
}

#ifndef _VBD_CMD_ //VBDCommander has a different implementation of this function. @see dmae_vbd_cmd.c
// executes dmae sgl
lm_status_t lm_dmae_sgl_go( IN lm_device_t* pdev,
                            IN OUT void*    ctx )
{
    struct dmae_sgl_context* dmae_sgl_ctx = (struct dmae_sgl_context*)ctx ;
    u32_t                               i = 0 ;

    DbgBreakIf(IS_VFDEV(pdev));

    if CHK_NULL( dmae_sgl_ctx )
    {
        DbgBreakIf(!dmae_sgl_ctx) ;
        return LM_STATUS_INVALID_PARAMETER;
    }

    if CHK_NULL( pdev )
    {
        DbgBreakIf(!pdev) ;
        return LM_STATUS_INVALID_PARAMETER;
    }

    // validate commands - consider state machine - commnad ready flag etc...
    for( i = 0; i<dmae_sgl_ctx->cnt_executer_cmd; i++ )
    {
        if ERR_IF( 0 == dmae_sgl_ctx->executer[i].dst_addr_lo ||
                   0 == dmae_sgl_ctx->executer[i].src_addr_lo ||
                   0 == dmae_sgl_ctx->executer[i].len )
        {
            DbgBreakMsg("Error!\n") ;
            return LM_STATUS_INVALID_PARAMETER ;
        }
    }

    // reset the completion word
    dmae_sgl_ctx->completion_word = 0 ;
    // for debugging
    ++dmae_sgl_ctx->dmae_sgl_cnt ;

    lm_dmae_post_command( pdev, dmae_sgl_ctx->idx_cmd_loader, &dmae_sgl_ctx->loader ) ;

    return LM_STATUS_SUCCESS ;
}
#endif //!_VBD_CMD_

// check weather an sgl command is done
lm_status_t lm_dmae_sgl_is_done( IN OUT void* ctx )
{
    lm_status_t              lm_status    = LM_STATUS_SUCCESS ;
    struct dmae_sgl_context* dmae_sgl_ctx = (struct dmae_sgl_context*)ctx ;

    if CHK_NULL( dmae_sgl_ctx )
    {
        DbgBreakIf(!dmae_sgl_ctx) ;
        return LM_STATUS_INVALID_PARAMETER;
    }

    ( DMAE_SGL_COMPLETION_VAL == dmae_sgl_ctx->completion_word ) ? (lm_status = LM_STATUS_SUCCESS) : (lm_status = LM_STATUS_TIMEOUT) ;

    return lm_status ;
}

// returns lm_status
// allocate physical memory for commands and init general dmae fields (commands indexes and address)
// max_length - max_length of data (in DW)
lm_status_t lm_dmae_command_alloc( IN struct _lm_device_t* pdev,
                                   IN OUT void*            ctx,
                                   IN const  u8_t          idx_cmd,
                                   IN const  u16_t         max_pci_length )
{
    lm_address_t                 phys_addr        = {{0}} ;
    u32_t                        alloc_size       = 0 ;
    struct dmae_command_context* dmae_command_ctx = ctx ;
    u8_t                         mm_cli_idx       = 0;

    DbgBreakIf(IS_VFDEV(pdev));

    if CHK_NULL(dmae_command_ctx)
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }

    mm_cli_idx = LM_RESOURCE_COMMON; //!!DP mm_cli_idx_to_um_idx(LM_CLI_IDX_MAX);

    // we allocate contiguous memory ( command + number of commands DW - 1) -1 for the [1]
    alloc_size = sizeof( struct dmae_command_context_phys ) + ( sizeof( u32_t ) * (max_pci_length-1) ) ;

    dmae_command_ctx->phys = mm_alloc_phys_mem( pdev, alloc_size, &dmae_command_ctx->phys_addr, PHYS_MEM_TYPE_NONCACHED, mm_cli_idx ) ;

    if CHK_NULL(dmae_command_ctx->phys)
    {
        return LM_STATUS_RESOURCE ;
    }

    // reset stucture to zero
    mm_mem_zero( dmae_command_ctx->phys, alloc_size ) ;

    dmae_command_ctx->idx_cmd   = idx_cmd ;

    phys_addr = dmae_command_ctx->phys_addr ;

    LM_INC64( &phys_addr, OFFSETOF( struct dmae_command_context_phys, completion_word ) ) ;

    dmae_command_ctx->command.comp_addr_lo = dmae_command_ctx->phys_addr.as_u32.low ;
    dmae_command_ctx->command.comp_addr_hi = dmae_command_ctx->phys_addr.as_u32.high ;

    // comp value - special indication for us
    dmae_command_ctx->command.comp_val     = DMAE_COMPLETION_VAL ;
    dmae_command_ctx->max_pci_length       = max_pci_length ;

    return LM_STATUS_SUCCESS ;
}

// execute DMAE command
// ctx - allocated exits prior to this call
// lm_dmae_mem_type - src/dst (checked only for src=pci dst=grc)
// src/dst_addr
// length - in DW (e.g. writing 64 bit value - length=2 !!!)
lm_status_t lm_dmae_command( IN struct _lm_device_t*    pdev,
                             IN void*                   ctx,
                             IN const  lm_dmae_mem_type src_mem_type,
                             IN const  lm_dmae_mem_type dst_mem_type,
                             IN const  lm_address_t     src_addr,
                             IN OUT    lm_address_t     dst_addr,
                             IN const  u8_t             b_pci_addr_is_virt,
                             IN const  u8_t             change_endianity,
                             IN const  u8_t             le32_swap,
                             IN const  u16_t            length )
{
    struct dmae_command_context* dmae_command_ctx       = ctx ;
    u32_t                                     val       = 0 ;
    lm_address_t                              phys_addr = {{0}} ;

    u32_t        wait_cnt       = 0 ;
    u32_t       wait_cnt_limit   = 10000; // 200ms for ASIC, * vars.clk_factor FPAGA/EMUL
#ifndef __BIG_ENDIAN
    // if we changed the endianity, the completion word should be swapped
    u32_t const desired_comp_val = change_endianity ? DMAE_COMPLETION_VAL_SWAPPED : DMAE_COMPLETION_VAL ;
#else
    u32_t const desired_comp_val = DMAE_COMPLETION_VAL;
#endif // !__BIG_ENDIAN

    DbgBreakIf(IS_VFDEV(pdev));

    if ( CHK_NULL(pdev) || CHK_NULL(ctx) )
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }

    wait_cnt_limit*= pdev->vars.clk_factor ;

    DbgBreakIf( 0 == wait_cnt_limit );

    if ERR_IF( dmae_command_ctx->idx_cmd >= 15 )
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }

    if ERR_IF( length > dmae_command_ctx->max_pci_length)
    {
        return LM_STATUS_INVALID_PARAMETER ;
    }

    phys_addr = dmae_command_ctx->phys_addr ;
    LM_INC64( &phys_addr, OFFSETOF( struct dmae_command_context_phys, data[0] ) );

    // set opcode
    // -----------
    dmae_command_ctx->command.opcode = 0 ;

    // src
    switch( src_mem_type )
    {
    case lm_dmae_mem_type_grc:
        dmae_command_ctx->command.src_addr_lo = src_addr.as_u32.low;
        dmae_command_ctx->command.src_addr_hi = src_addr.as_u32.high;
        val = 1 ;
        break;
    case lm_dmae_mem_type_pci:
        if (b_pci_addr_is_virt)
        {
        int i;

            // we're sure that src_addr is virtual addresses
            mm_memcpy( &dmae_command_ctx->phys->data[0], src_addr.as_ptr, length*sizeof(u32_t) ) ;
            dmae_command_ctx->command.src_addr_lo = phys_addr.as_u32.low ;
            dmae_command_ctx->command.src_addr_hi = phys_addr.as_u32.high ;

            if (le32_swap)
            {
                for (i = 0; i < length; i++)
                {
                dmae_command_ctx->phys->data[i] = mm_cpu_to_le32(dmae_command_ctx->phys->data[i]);
            }
        }
        } // b_pci_addr_is_virt
        else
        {
            dmae_command_ctx->command.src_addr_lo = src_addr.as_u32.low;
            dmae_command_ctx->command.src_addr_hi = src_addr.as_u32.high;
        }
        val = 0 ;
        break;
    default:
        return LM_STATUS_INVALID_PARAMETER ;
    }
    dmae_command_ctx->command.opcode       |= ( val << DMAE_CMD_SRC_SHIFT ) ;

    // dst
    switch( dst_mem_type )
    {
    case lm_dmae_mem_type_grc:
        dmae_command_ctx->command.dst_addr_lo = dst_addr.as_u32.low ;
        dmae_command_ctx->command.dst_addr_hi = dst_addr.as_u32.high ;
        val = 2 ;
        break;
    case lm_dmae_mem_type_pci:
        if (b_pci_addr_is_virt)
        {
            dmae_command_ctx->command.dst_addr_lo = phys_addr.as_u32.low ;
            dmae_command_ctx->command.dst_addr_hi = phys_addr.as_u32.high ;
        }
        else
        {
            dmae_command_ctx->command.dst_addr_lo = dst_addr.as_u32.low;
            dmae_command_ctx->command.dst_addr_hi = dst_addr.as_u32.high;
        }

        val = 1 ;
        break;
    default:
        return LM_STATUS_INVALID_PARAMETER ;
    }
    dmae_command_ctx->command.opcode       |= ( val << DMAE_CMD_DST_SHIFT ) ;

    // comp dst is PCI (0)
    dmae_command_ctx->command.opcode       |= ( 0 << DMAE_CMD_C_DST_SHIFT ) ;
    // comp do write to dst (1)
    dmae_command_ctx->command.opcode       |= ( 1 << DMAE_CMD_C_TYPE_ENABLE_SHIFT ) ;
    // comp disable crc write to dst (0)
    dmae_command_ctx->command.opcode       |= ( 0 << DMAE_CMD_C_TYPE_CRC_ENABLE_SHIFT ) ;
    // Endianity mode - possibly set different byte swapping - to avoid HW constraint when copying from pci to pci
    if (FALSE == change_endianity)
    {
        // Endianity mode 2
        dmae_command_ctx->command.opcode       |= ( 2 << DMAE_CMD_ENDIANITY_SHIFT ) ;
    }
    else
    {
        // Endianity mode 3
        dmae_command_ctx->command.opcode       |= ( 3 << DMAE_CMD_ENDIANITY_SHIFT ) ;
    }
    // Port number
    dmae_command_ctx->command.opcode       |= ( PORT_ID(pdev) << DMAE_CMD_PORT_SHIFT ) ;
    // crc reset - not relevant
    dmae_command_ctx->command.opcode       |= ( 0 << DMAE_CMD_CRC_RESET_SHIFT ) ;
    // src address reset 1
    dmae_command_ctx->command.opcode       |= ( 1 << DMAE_CMD_SRC_RESET_SHIFT ) ;
    // TBD
    // dst address reset 0
    dmae_command_ctx->command.opcode       |= ( 0 << DMAE_CMD_DST_RESET_SHIFT ) ;
    // VNIC id
    dmae_command_ctx->command.opcode       |= ( VNIC_ID(pdev) << DMAE_CMD_E1HVN_SHIFT ) ;
    // addresses
    dmae_command_ctx->command.len         = length ; // DW

    // reset the completion word
    dmae_command_ctx->phys->completion_word = 0 ;

    lm_dmae_post_command( pdev, dmae_command_ctx->idx_cmd, &dmae_command_ctx->command ) ;
    ++dmae_command_ctx->cmd_post_cnt;

    while( desired_comp_val != dmae_command_ctx->phys->completion_word  )
    {
        // should not happen...
        mm_wait( pdev, 20 );
        if( wait_cnt == wait_cnt_limit )
        {
            DbgMessage7(pdev,FATAL,"lm_dmae_command: len=0x%x comp_addr=0x%x:%x src=0x%x:%x dst=0x%x:%x\n",
                       length,
                       dmae_command_ctx->command.comp_addr_hi,
                       dmae_command_ctx->command.comp_addr_lo,
                       dmae_command_ctx->command.src_addr_hi,
                       dmae_command_ctx->command.src_addr_lo,
                       dmae_command_ctx->command.dst_addr_hi,
                       dmae_command_ctx->command.dst_addr_lo );
        }
        if ((++wait_cnt > wait_cnt_limit) || lm_reset_is_inprogress(pdev))
        {

            /* shutdown bug - BSOD only if shutdown is not in progress */
            if (!lm_reset_is_inprogress(pdev))
            {
                DbgBreakIfAll( 1 ) ;
                return LM_STATUS_FAILURE;
            }
            else
            {
                break;
            }
        }
    }

    if( lm_dmae_mem_type_pci == dst_mem_type )
    {
        if (b_pci_addr_is_virt)
        {
            // we're sure that src_addr is virtual addresses
            mm_memcpy( dst_addr.as_ptr, (void*)(&dmae_command_ctx->phys->data[0]), length*sizeof(u32_t) ) ;
        }
    }

    return LM_STATUS_SUCCESS ;
}

// this funciton is used to support reads/writes greater than 64K-1 length, and actually greater than 0x2000
// the function splits length size to 0x1000
lm_status_t lm_dmae_command_split_imp( IN struct _lm_device_t*    pdev,
                                       IN void*                   ctx,
                                       IN const  lm_dmae_mem_type src_mem_type,
                                       IN const  lm_dmae_mem_type dst_mem_type,
                                       IN const  lm_address_t     src_addr,
                                       IN OUT    lm_address_t     dst_addr,
                                       IN const  u8_t             b_src_is_zeroed,    // indicate that the source buffer is all zeroed
                                       IN const  u8_t             b_pci_addr_is_virt, // indicate that the pci type addresses are virtual
                                       IN const  u8_t             change_endianity,
                                       IN const  u8_t             le32_swap,
                                       IN const  u32_t            length )
{
    lm_status_t        lm_status       = LM_STATUS_SUCCESS ;
    u16_t              length_current  = 0 ;
    u16_t              i               = 0 ;
    u32_t              offset          = 0 ;
    lm_address_t       src_addr_split  = src_addr ;
    lm_address_t       dst_addr_split  = dst_addr ;

    DbgBreakIf(IS_VFDEV(pdev));

    if ( CHK_NULL(pdev) || ERR_IF( 0 == length ) )
    {
        lm_status = LM_STATUS_INVALID_PARAMETER ;
    }
    else
    {
        const u16_t        length_limit    = ( lm_dmae_mem_type_pci == dst_mem_type) ? min( DMAE_MAX_READ_SIZE, DMAE_MAX_RW_SIZE(pdev) ) : DMAE_MAX_RW_SIZE(pdev) ;
        const u16_t        cnt_split       = length / length_limit  ; // number of chunks of splits
        const u16_t        length_mod      = length % length_limit  ;

        MM_ACQUIRE_DMAE_MISC_LOCK(pdev);

        for( i = 0; i <= cnt_split; i++ )
        {
            offset = length_limit*i ;

            // in case source address is all zeroed - no need (and not allowed) to advance the pointer!
            if( !b_src_is_zeroed )
            {
                if (src_mem_type == lm_dmae_mem_type_grc)
                {
                    src_addr_split.as_u64 = src_addr.as_u64 + offset;
                }
                else
                {
                    src_addr_split.as_u64 = src_addr.as_u64 + (offset*4);
                }
            }

            if (dst_mem_type == lm_dmae_mem_type_grc)
            {
                dst_addr_split.as_u64 = dst_addr.as_u64 + offset;
            }
            else
            {
                dst_addr_split.as_u64 = dst_addr.as_u64 + (offset*4);
            }

            length_current = (cnt_split==i)? length_mod : length_limit ;

            // might be zero on last iteration
            if( 0 != length_current )
            {
                lm_status = lm_dmae_command(pdev, ctx, src_mem_type, dst_mem_type, src_addr_split, dst_addr_split, b_pci_addr_is_virt, change_endianity, le32_swap, length_current ) ;
                if( LM_STATUS_SUCCESS != lm_status )
                {
                    MM_RELEASE_DMAE_MISC_LOCK(pdev);
                    return lm_status ;
                }
            }
        }
    }

    MM_RELEASE_DMAE_MISC_LOCK(pdev);
    return lm_status ;
}


// wrapper function that calls lm_dmae_command_split_imp using lm_address_t
// for the addresses rather then void *
lm_status_t lm_dmae_command_split( IN struct _lm_device_t*    pdev,
                                   IN void*                   ctx,
                                   IN const  lm_dmae_mem_type src_mem_type,
                                   IN const  lm_dmae_mem_type dst_mem_type,
                                   IN const  void*            src_addr,
                                   IN OUT    void*            dst_addr,
                                   IN const  u8_t             b_src_is_zeroed,    // indicate that the source buffer is all zeroed
                                   IN const  u8_t             b_pci_addr_is_virt, // indicate that the pci type addresses are virtual
                                   IN const  u8_t             change_endianity,
                                   IN const  u8_t             le32_swap,
                                   IN const  u32_t            length )
{
    lm_address_t src_lm_addr;
    lm_address_t dst_lm_addr;

    DbgBreakIf(IS_VFDEV(pdev));

    // risky cast, shouldn't be used with physical addresses
    src_lm_addr.as_ptr = (void *)src_addr;
    dst_lm_addr.as_ptr = (void *)dst_addr;


    return lm_dmae_command_split_imp(pdev,
                                            ctx,
                                            src_mem_type,
                                            dst_mem_type,
                                            src_lm_addr,
                                            dst_lm_addr,
                                            b_src_is_zeroed,
                                            b_pci_addr_is_virt,
                                            change_endianity,
                                            le32_swap,
                                            length);

}

