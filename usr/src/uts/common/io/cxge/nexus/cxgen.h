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

/*
 * Copyright (c) 2010 by Chelsio Communication, Inc.
 * All rights reserved.
 */

#ifndef _CXGE_CXGEN_H
#define	_CXGE_CXGEN_H

#include <sys/dditypes.h>
#include <sys/ddifm.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/fm/io/ddi.h>
#include <sys/mac_provider.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL
extern int t3_fw_image_sz;
extern uchar_t t3_fw_image[];
int cxgen_alloc_intr(p_adapter_t, int);
void cxgen_free_intr(p_adapter_t);
int cxgen_setup_kstats(p_adapter_t);
void cxgen_finalize_config_kstats(p_adapter_t); /* ugly hack */
void cxgen_destroy_kstats(p_adapter_t);
void cxgen_setup_qset_kstats(p_adapter_t, int);
void cxgen_destroy_qset_kstats(p_adapter_t, int);
struct port_info *cxgen_get_portinfo(dev_info_t *, dev_info_t *);
int first_port_up(p_adapter_t);
int last_port_down(p_adapter_t);
int t3_sge_alloc_qset(p_adapter_t, uint_t, int, const struct qset_params *,
	struct port_info *);
void t3_sge_free_qset(p_adapter_t, uint_t);
void t3_sge_start(p_adapter_t);
void t3_sge_stop(p_adapter_t);
mblk_t *sge_rx_data(struct sge_qset *);
int sge_tx_data(struct sge_qset *, mblk_t *, int);
void bind_qsets(p_adapter_t);
int sge_qs_intr_enable(mac_ring_driver_t);
int sge_qs_intr_disable(mac_ring_driver_t);

/* FMA Routines */
void cxgen_fm_init(p_adapter_t);
void cxgen_fm_fini(p_adapter_t);
int  cxgen_fm_check_acc_handle(ddi_acc_handle_t);
int  cxgen_fm_check_dma_handle(ddi_dma_handle_t);
void cxgen_fm_ereport_post(p_adapter_t, char *);
int  cxgen_fm_errcb(dev_info_t *, ddi_fm_error_t *, const void *);
void cxgen_fm_err_report(p_adapter_t, char *, int);

/* TODO: Tune. */
#define	QS_DEFAULT_DESC_BUDGET(q) ((q)->depth / 4)
#define	QS_DEFAULT_FRAME_BUDGET(q) ((q)->depth / 8)
#endif

#define	CXGEN_IOCTL		((('c' << 16) | 'h') << 8)
#define	CXGEN_IOCTL_PCIGET32    CXGEN_IOCTL + 1
#define	CXGEN_IOCTL_PCIPUT32    CXGEN_IOCTL + 2
#define	CXGEN_IOCTL_GET32	CXGEN_IOCTL + 3
#define	CXGEN_IOCTL_PUT32	CXGEN_IOCTL + 4
#define	CXGEN_IOCTL_SFGET	CXGEN_IOCTL + 5
#define	CXGEN_IOCTL_SFPUT	CXGEN_IOCTL + 6
#define	CXGEN_IOCTL_MIIGET	CXGEN_IOCTL + 7
#define	CXGEN_IOCTL_MIIPUT	CXGEN_IOCTL + 8
#define	CXGEN_IOCTL_GET_MEM 	CXGEN_IOCTL + 9
#define	CXGEN_IOCTL_FPGA_READY	CXGEN_IOCTL + 10
#define	CXGEN_IOCTL_MAKE_UNLOAD	CXGEN_IOCTL + 11
#define	CXGEN_IOCTL_GET_CNTXT	CXGEN_IOCTL + 12
#define	CXGEN_IOCTL_GET_QE	CXGEN_IOCTL + 13
#define	CXGEN_IOCTL_GET_QS	CXGEN_IOCTL + 14
#define	CXGEN_IOCTL_SEND	CXGEN_IOCTL + 15
#define	CXGEN_IOCTL_CTRLSEND	CXGEN_IOCTL + 16
#define	CXGEN_IOCTL_IRQ		CXGEN_IOCTL + 17
#define	CXGEN_IOCTL_GET_SGEDESC	CXGEN_IOCTL + 18
#define	CXGEN_IOCTL_GET_QSPARAM	CXGEN_IOCTL + 19
#define	CXGEN_IOCTL_REGDUMP	CXGEN_IOCTL + 20

typedef struct cxgen_reg32_cmd_s {
	uint32_t reg;
	uint32_t value;
} cxgen_reg32_cmd_t, *p_cxgen_reg32_cmd_t;

typedef struct cxgen_mii_reg_data_s {
	uint32_t phy_id;
	uint32_t reg_num;
	uint32_t val_in;
	uint32_t val_out;
} cxgen_mii_reg_data_t, *p_cxgen_mii_reg_data_t;

typedef enum {
	MEM_CM,
	MEM_PMRX,
	MEM_PMTX
} mem_id_t;

typedef struct cxgen_mem_range_s {
	uint32_t cmd;
	uint32_t mem_id;
	uint32_t addr;
	uint32_t len;
	uint32_t version;
	uint8_t *buf;
} cxgen_mem_range_t, *p_cxgen_mem_range_t;

typedef enum {
	tunnel,
	command,
	control,
	free_list,
	resp_queue,
	cq
} cntxt_type_t;

typedef struct cxgen_get_cntxt_cmd_s {
	cntxt_type_t type;
	uint32_t id;
	uint32_t data[4];
} cxgen_get_cntxt_cmd_t, *p_cxgen_get_cntxt_cmd_t;

typedef struct cxgen_sgedesc_s {
	uint32_t queue_num;
	uint32_t idx;
	uint32_t size;
	uint64_t  data[16];
} cxgen_sgedesc_t, *p_cxgen_sgedesc_t;

typedef struct cxgen_qsparam_s {
	uint32_t qset_idx;
	int32_t  txq_size[3];
	int32_t  rspq_size;
	int32_t  fl_size[2];
	int32_t  cong_thres;
	int32_t  intr_lat;
	int32_t instance;
} cxgen_qsparam_t, *p_cxgen_qsparam_t;

#define	REGDUMP_SIZE (3 * 1024)
typedef struct cxgen_regdump_s {
	uint32_t  version;
	uint32_t  len; /* bytes */
	uint8_t   *data;
} cxgen_regdump_t, *p_cxgen_regdump_t;

#ifdef __cplusplus
}
#endif

#endif /* _CXGE_CXGEN_H */
