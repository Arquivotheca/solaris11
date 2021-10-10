
/*
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2008-2011 Broadcom Corporation, ALL RIGHTS RESERVED.
 */

#ifndef __BRCM_FCOEIO_IOCTL_H__
#define __BRCM_FCOEIO_IOCTL_H__

#define BNXE_FCOEIO_VERSION             1

/* The Main Ioctl commands definition (only one defined) */
#define BNXE_FCOEIO_CMD                 ('B' << 8) | 1

/* Sub command defined for use */
#define BNXE_FCOE_CREATE_PORT           1
#define BNXE_FCOE_DELETE_PORT           2
#define BNXE_FCOE_LIST_PORT             3

#define BNXE_FCOEIO_XFER_WRITE          1
#define BNXE_FCOEIO_XFER_READ           2

#ifndef BNXE_FCOE_WWN_SIZE
#define BNXE_FCOE_WWN_SIZE              8
#endif


typedef struct bnxe_fcoeio_create_port_param
{
    uint8_t  fcp_pwwn[BNXE_FCOE_WWN_SIZE];
    uint8_t  fcp_nwwn[BNXE_FCOE_WWN_SIZE];
    uint32_t fcp_nwwn_provided;
    uint32_t fcp_pwwn_provided;
    uint32_t fcp_force_promisc; /* Not used maintained for compatibility */
    uint32_t fcp_port_type;     /* (unused) 0 for initiator 1 for target */
    uint32_t fcp_mac_linkid;    /* Link to MAC ID */
} bnxe_fcoeio_create_port_param_t;


#if 0 /* XXX unused... */
typedef struct bnxe_fcoeio_delete_port_param
{
    uint32_t fdp_mac_linkid; /* Link to MAC ID */
    uint32_t fdp_reserved;   /* reserved */
} bnxe_fcoeio_delete_port_param_t;
#endif


typedef struct bnxe_fcoe_port_instance
{
    uint8_t  fpi_pwwn[BNXE_FCOE_WWN_SIZE];
    uint32_t fpi_mac_linkid;
    uint8_t  fpi_mac_factory_addr[ETHERADDRL];
    uint16_t fpi_reserved0;   /* reserved (alignment) */
    uint32_t fpi_mac_promisc; /* Not used maintained for compatibility */
    uint8_t  fpi_mac_current_addr[ETHERADDRL];
    uint16_t fpi_reserved1;   /* reserved (alignment) */
    uint32_t fpi_port_type;   /* (unused) 0 for initiator 1 for target */
    uint32_t mtu_size;        /* MTU size configured */
} bnxe_fcoe_port_instance_t;


typedef struct bnxe_fcoe_port_list
{
    uint32_t num_ports; /* always one(1) FCoE port per MAC interface */
    bnxe_fcoe_port_instance_t ports[1]; /* array of "num_ports" */
} bnxe_fcoe_port_list_t;


typedef union bnxe_fcoeio_input
{
    bnxe_fcoeio_create_port_param_t create_port;
} bnxe_fcoeio_input_t;


typedef union bnxe_fcoeio_output
{
    bnxe_fcoe_port_list_t port_list;
} bnxe_fcoeio_output_t;


/* Data for BNXE_FCOEIO_CMD */
typedef struct bnxe_fcoeio
{
    uint32_t             fcoeio_version; /* version */
    uint32_t             fcoeio_xfer;    /* direction */
    uint32_t             fcoeio_cmd;     /* sub-command */
    uint32_t             fcoeio_flags;   /* flags (reserved) */
    uint32_t             fcoeio_stat;    /* return status */
    bnxe_fcoeio_input_t  fcoeio_input;   /* cmd input */
    bnxe_fcoeio_output_t fcoeio_output;  /* cmd output */
} bnxe_fcoeio_t;

#endif /* __BRCM_FCOEIO_IOCTL_H__ */

