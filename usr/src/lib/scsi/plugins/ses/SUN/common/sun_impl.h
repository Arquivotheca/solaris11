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

#ifndef	_PLUGIN_SUN_IMPL_H
#define	_PLUGIN_SUN_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/scsi/impl/uscsi.h>
#include <sys/scsi/generic/commands.h>
#include <sys/scsi/impl/spc3_types.h>
#include <sys/ccompile.h>
#include <stdarg.h>
#include <libnvpair.h>

#include <scsi/libscsi.h>
#include <scsi/libses_plugin.h>
#include <scsi/plugins/ses/framework/ses2_impl.h>

#pragma	pack(1)

/*
 * SPMS-1 r111, section 4.2.4 - Configuration
 * SPMS-1 r111, section 4.1.3.1, Table 8 - Sun Feature Definition
 */
typedef struct sun_feature_block_impl {
	uint8_t		sfbi_spms_header[4];
	uint8_t		sfbi_spms_major_ver;
	uint8_t		_reserved1;
	uint16_t	sfbi_spms_revision;
	uint8_t		sfbi_chassis_id_off;
	uint8_t		sfbi_chassis_id_len;
	DECL_BITFIELD2(
	    sfbi_fw_upload_max_chunk_sz	:7,
	    sfbi_int			:1);
	uint8_t		sfbi_subchassis_index;
	uint8_t		_reserved2[48];
	uint8_t		sfbi_ps[1];	/* Flexible platform specific content */
} sun_feature_block_impl_t;

/*
 * SPMS-1 SUNW,FRUID FRU descriptor (Table 23, 4.2.18)
 */
typedef struct sun_fru_descr_impl {
	DECL_BITFIELD2(
	    sfdi_fru	:1,
	    _reserved1	:7);
	uint8_t sfdi_parent_element_index;
	uint16_t sfdi_fru_data_length;
	uint8_t sfdi_fru_data[1];	/* FRUPROM contents */
} sun_fru_descr_impl_t;

/*
 * SPMS-1 SUNW,FRUID diagnostic page (Table 22, 4.2.18)
 */
typedef struct sun_fruid_page_impl {
	uint8_t sfpi_page_code;
	uint8_t _reserved1;
	uint16_t sfpi_page_length;
	uint32_t sfpi_generation_code;
	uint16_t sfpi_descr_addrs[1];
} sun_fruid_page_impl_t;

/*
 * SPMS Fan Module element for control-type diagnostic pages (Table 53).
 */
typedef struct sun_fanmodule_ctl_impl {
	ses2_cmn_elem_ctl_impl_t sfci_common;
	DECL_BITFIELD5(
	    _reserved1			:4,
	    sfci_rqst_ready_to_rmv	:1,
	    sfci_rqst_on		:1,
	    sfci_rqst_fail		:1,
	    sfci_rqst_ident		:1);
	DECL_BITFIELD2(
	    sfci_requested_speed_code	:3,
	    _reserved2			:5);
	uint8_t _reserved3;
} sun_fanmodule_ctl_impl_t;

/*
 * SPMS Fan Module element for status-type diagnostic pages (Table 54).
 */
typedef struct sun_fanmodule_status_impl {
	ses2_cmn_elem_status_impl_t sfsi_common;
	DECL_BITFIELD8(
	    sfsi_ready_to_rmv_sup	:1,
	    sfsi_on_sup			:1,
	    sfsi_fail_sup		:1,
	    sfsi_ident_sup		:1,
	    sfsi_rqst_ready_to_rmv	:1,
	    sfsi_rqst_on		:1,
	    sfsi_rqst_fail		:1,
	    sfsi_rqst_ident		:1);
	DECL_BITFIELD6(
	    sfsi_requested_speed_code	:3,
	    _reserved1			:1,
	    sfsi_ready_to_rmv		:1,
	    sfsi_on			:1,
	    sfsi_fail			:1,
	    sfsi_ident			:1);
	DECL_BITFIELD2(
	    sfsi_actual_speed_code	:3,
	    _reserved2			:5);
} sun_fanmodule_status_impl_t;

#pragma pack()

extern ses_pagedesc_t sun_pages[];

extern int sun_fill_element_node(ses_plugin_t *, ses_node_t *);
extern int sun_fill_enclosure_node(ses_plugin_t *, ses_node_t *);
extern int sun_fruid_parse_common(sun_fru_descr_impl_t *, nvlist_t *);

extern int sun_setprop(ses_plugin_t *, ses_node_t *, const ses2_ctl_prop_t *,
    nvlist_t *);

extern int sun_element_setdef(ses_node_t *, ses2_diag_page_t, void *);
extern int sun_enclosure_setdef(ses_node_t *, ses2_diag_page_t, void *);

extern int sun_element_ctl(ses_plugin_t *, ses_node_t *, const char *,
    nvlist_t *);
extern int sun_enclosure_ctl(ses_plugin_t *, ses_node_t *, const char *,
    nvlist_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _PLUGIN_SUN_IMPL_H */
