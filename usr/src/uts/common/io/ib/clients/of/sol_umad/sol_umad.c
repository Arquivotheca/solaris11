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
 * sol_umad.c
 *
 * ofuv user MAD kernel agent module
 *
 * Enables functionality of the OFED 1.3 Linux based MAD application code.
 */

#include <sys/open.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/sysmacros.h>
#include <sys/ib/ibtl/ibti.h>
#include <sys/ib/mgt/ibmf/ibmf.h>
#include <sys/ib/mgt/ibmf/ibmf_rmpp.h>

#include <sys/types.h>
#include <sys/ib/clients/of/ofed_kernel.h>
#include <sys/ib/clients/of/sol_ofs/sol_ofs_common.h>
#include <sys/ib/clients/of/rdma/ib_user_mad.h>
#include <sys/ib/clients/of/sol_umad/sol_umad.h>
#include <sys/ib/clients/of/sol_umad/sol_umad_ioctl.h>
#include <sys/policy.h>
#include <sys/priv_const.h>

#define	MAX_NAME_LEN	32

static char *sol_umad_dbg_str = "sol_umad";

/* Local definitions */
static void *umad_statep;
static int umad_get_mad_clhdr_offset(uint8_t);
static void *umad_copy_ibmf_msg(ibmf_msg_t *msgp, size_t *size);
static void umad_async_event_handler(struct ib_event_handler *,
    struct ib_event *);
static void umad_set_ioctl_portinfo(sol_umad_ioctl_port_info_t *,
    umad_hca_info_t *, umad_port_info_t *);

static struct cb_ops umad_cb_ops = {
	.cb_open			= umad_open,
	.cb_close			= umad_close,
	.cb_strategy			= nodev,
	.cb_print			= nodev,
	.cb_dump			= nodev,
	.cb_read			= umad_read,
	.cb_write			= umad_write,
	.cb_ioctl			= umad_ioctl,
	.cb_devmap			= nodev,
	.cb_mmap			= nodev,
	.cb_segmap			= nodev,
	.cb_chpoll			= umad_poll,
	.cb_prop_op			= umad_prop_op,
	.cb_str				= NULL,
	.cb_flag			= D_NEW | D_MP,
	.cb_rev				= CB_REV,
	.cb_aread			= nodev,
	.cb_awrite			= nodev
};

static struct dev_ops umad_dev_ops = {
	.devo_rev			= DEVO_REV,
	.devo_refcnt			= 0,
	.devo_getinfo			= umad_getinfo,
	.devo_identify			= nulldev,
	.devo_probe			= nulldev,
	.devo_attach			= umad_attach,
	.devo_detach			= umad_detach,
	.devo_reset			= nodev,
	.devo_cb_ops			= &umad_cb_ops,
	.devo_bus_ops			= NULL,
	.devo_power			= nodev,
	.devo_quiesce			= ddi_quiesce_not_needed
};

static struct modldrv umad_modldrv = {
	.drv_modops			= &mod_driverops,
	.drv_linkinfo			= "Solaris IB user MAD kernel driver",
	.drv_dev_ops			= &umad_dev_ops
};

static struct modlinkage modlinkage = {
	.ml_rev				= MODREV_1,
	.ml_linkage = {
		[0]			= &umad_modldrv,
		[1]			= NULL,
	}
};

static struct ib_client sol_umad_ib_client = {
	.name		= "sol_umad",
	.add		= umad_add_hca,
	.remove		= umad_remove_hca,
	.clnt_hdl 	= NULL,
	.state		= IB_CLNT_UNINITIALIZED
};

#define	MAX_MAD_TO_IBMF_MAPPINGS	4 /* Max of 4 MADs to 1 IBMF */
const struct ibmf_class_to_mad_type {
	ibmf_client_type_t	ibmf_class;
	uint8_t				mad_types[MAX_MAD_TO_IBMF_MAPPINGS];
} ibmf_class_to_mad_types[] = {
	{SUBN_MANAGER,
	    {MAD_MGMT_CLASS_SUBN_LID_ROUTED,
	    MAD_MGMT_CLASS_SUBN_DIRECT_ROUTE,
	    0}},
	{0,
	{0}}
};

const ibmf_client_type_t umad_type_to_ibmf_class[256] = {
	0,				/* 0x00 Reserved */
	SUBN_MANAGER,			/* 0x01 CLASS_SUBN_LID_ROUTED */
	0,				/* 0x02 Reserved */
	SUBN_ADM_AGENT,			/* 0x03 CLASS_SUBN_ADM */
	PERF_MANAGER,			/* 0x04 CLASS_PERF_MGMT */
	BM_AGENT, 			/* 0x05 CLASS_BM */
	DEV_MGT_AGENT,			/* 0x06 CLASS_DEVICE_MGMT */
	COMM_MGT_MANAGER_AGENT,		/* 0x07 CLASS_CM */
	SNMP_MANAGER_AGENT,		/* 0x08 CLASS_SNMP */

	VENDOR_09_MANAGER_AGENT,	/* 0x09 */
	VENDOR_0A_MANAGER_AGENT,	/* 0x0A */
	VENDOR_0B_MANAGER_AGENT,	/* 0x0B */
	VENDOR_0C_MANAGER_AGENT,	/* 0x0C */
	VENDOR_0D_MANAGER_AGENT,	/* 0x0D */
	VENDOR_0E_MANAGER_AGENT,	/* 0x0E */
	VENDOR_0F_MANAGER_AGENT,	/* 0x0F */

	APPLICATION_10_MANAGER_AGENT,	/* 0x10 */
	APPLICATION_11_MANAGER_AGENT,	/* 0x11 */
	APPLICATION_12_MANAGER_AGENT,	/* 0x12 */
	APPLICATION_13_MANAGER_AGENT,	/* 0x13 */
	APPLICATION_14_MANAGER_AGENT,	/* 0x14 */
	APPLICATION_15_MANAGER_AGENT,	/* 0x15 */
	APPLICATION_16_MANAGER_AGENT,	/* 0x16 */
	APPLICATION_17_MANAGER_AGENT,	/* 0x17 */
	APPLICATION_18_MANAGER_AGENT,	/* 0x18 */
	APPLICATION_19_MANAGER_AGENT,	/* 0x19 */
	APPLICATION_1A_MANAGER_AGENT,	/* 0x1A */
	APPLICATION_1B_MANAGER_AGENT,	/* 0x1B */
	APPLICATION_1C_MANAGER_AGENT,	/* 0x1C */
	APPLICATION_1D_MANAGER_AGENT,	/* 0x1D */
	APPLICATION_1E_MANAGER_AGENT,	/* 0x1E */
	APPLICATION_1F_MANAGER_AGENT,	/* 0x1F */
	APPLICATION_20_MANAGER_AGENT,	/* 0x20 */
	APPLICATION_21_MANAGER_AGENT,	/* 0x21 */
	APPLICATION_22_MANAGER_AGENT,	/* 0x22 */
	APPLICATION_23_MANAGER_AGENT,	/* 0x23 */
	APPLICATION_24_MANAGER_AGENT,	/* 0x24 */
	APPLICATION_25_MANAGER_AGENT,	/* 0x25 */
	APPLICATION_26_MANAGER_AGENT,	/* 0x26 */
	APPLICATION_27_MANAGER_AGENT,	/* 0x27 */
	APPLICATION_28_MANAGER_AGENT,	/* 0x28 */
	APPLICATION_29_MANAGER_AGENT,	/* 0x29 */
	APPLICATION_2A_MANAGER_AGENT,	/* 0x2A */
	APPLICATION_2B_MANAGER_AGENT,	/* 0x2B */
	APPLICATION_2C_MANAGER_AGENT,	/* 0x2C */
	APPLICATION_2D_MANAGER_AGENT,	/* 0x2D */
	APPLICATION_2E_MANAGER_AGENT,	/* 0x2E */
	APPLICATION_2F_MANAGER_AGENT,	/* 0x2F */

	VENDOR_30_MANAGER_AGENT,	/* 0x30 */
	VENDOR_31_MANAGER_AGENT,	/* 0x31 */
	VENDOR_32_MANAGER_AGENT,	/* 0x32 */
	VENDOR_33_MANAGER_AGENT,	/* 0x33 */
	VENDOR_34_MANAGER_AGENT,	/* 0x34 */
	VENDOR_35_MANAGER_AGENT,	/* 0x35 */
	VENDOR_36_MANAGER_AGENT,	/* 0x36 */
	VENDOR_37_MANAGER_AGENT,	/* 0x37 */
	VENDOR_38_MANAGER_AGENT,	/* 0x38 */
	VENDOR_39_MANAGER_AGENT,	/* 0x39 */
	VENDOR_3A_MANAGER_AGENT,	/* 0x3A */
	VENDOR_3B_MANAGER_AGENT,	/* 0x3B */
	VENDOR_3C_MANAGER_AGENT,	/* 0x3C */
	VENDOR_3D_MANAGER_AGENT,	/* 0x3D */
	VENDOR_3E_MANAGER_AGENT,	/* 0x3E */
	VENDOR_3F_MANAGER_AGENT,	/* 0x3F */
	VENDOR_40_MANAGER_AGENT,
	VENDOR_41_MANAGER_AGENT,
	VENDOR_42_MANAGER_AGENT,
	VENDOR_43_MANAGER_AGENT,
	VENDOR_44_MANAGER_AGENT,
	VENDOR_45_MANAGER_AGENT,
	VENDOR_46_MANAGER_AGENT,
	VENDOR_47_MANAGER_AGENT,
	VENDOR_48_MANAGER_AGENT,
	VENDOR_49_MANAGER_AGENT,
	VENDOR_4A_MANAGER_AGENT,
	VENDOR_4B_MANAGER_AGENT,
	VENDOR_4C_MANAGER_AGENT,
	VENDOR_4D_MANAGER_AGENT,
	VENDOR_4E_MANAGER_AGENT,
	VENDOR_4F_MANAGER_AGENT,

	0,			/* 0x50 Reserved */
	0,			/* 0x51 Reserved */
	0,			/* 0x52 Reserved */
	0,			/* 0x53 Reserved */
	0,			/* 0x54 Reserved */
	0,			/* 0x55 Reserved */
	0,			/* 0x56 Reserved */
	0,			/* 0x57 Reserved */
	0,			/* 0x58 Reserved */
	0,			/* 0x59 Reserved */
	0,			/* 0x5A Reserved */
	0,			/* 0x5B Reserved */
	0,			/* 0x5C Reserved */
	0,			/* 0x5D Reserved */
	0,			/* 0x5E Reserved */
	0,			/* 0x5F Reserved */
	0,			/* 0x60 Reserved */
	0,			/* 0x61 Reserved */
	0,			/* 0x62 Reserved */
	0,			/* 0x63 Reserved */
	0,			/* 0x64 Reserved */
	0,			/* 0x65 Reserved */
	0,			/* 0x66 Reserved */
	0,			/* 0x67 Reserved */
	0,			/* 0x68 Reserved */
	0,			/* 0x69 Reserved */
	0,			/* 0x6A Reserved */
	0,			/* 0x6B Reserved */
	0,			/* 0x6C Reserved */
	0,			/* 0x6D Reserved */
	0,			/* 0x6E Reserved */
	0,			/* 0x6F Reserved */
	0,			/* 0x70 Reserved */
	0,			/* 0x71 Reserved */
	0,			/* 0x72 Reserved */
	0,			/* 0x73 Reserved */
	0,			/* 0x74 Reserved */
	0,			/* 0x75 Reserved */
	0,			/* 0x76 Reserved */
	0,			/* 0x77 Reserved */
	0,			/* 0x78 Reserved */
	0,			/* 0x79 Reserved */
	0,			/* 0x7A Reserved */
	0,			/* 0x7B Reserved */
	0,			/* 0x7C Reserved */
	0,			/* 0x7D Reserved */
	0,			/* 0x7E Reserved */
	0,			/* 0x7F Reserved */
	0,			/* 0x80 Reserved */

	SUBN_MANAGER,		/* 0x81 CLASS_SUBN_DIRECT_ROUTE */

	0,			/* 0x82 Reserved */
	0,			/* 0x82 Reserved */
	0,			/* 0x84 Reserved */
	0,			/* 0x85 Reserved */
	0,			/* 0x86 Reserved */
	0,			/* 0x87 Reserved */
	0,			/* 0x88 Reserved */
	0,			/* 0x89 Reserved */
	0,			/* 0x8A Reserved */
	0,			/* 0x8B Reserved */
	0,			/* 0x8C Reserved */
	0,			/* 0x8D Reserved */
	0,			/* 0x8E Reserved */
	0,			/* 0x8f Reserved */
	0,			/* 0x90 Reserved */
	0,			/* 0x91 Reserved */
	0,			/* 0x92 Reserved */
	0,			/* 0x93 Reserved */
	0,			/* 0x94 Reserved */
	0,			/* 0x95 Reserved */
	0,			/* 0x96 Reserved */
	0,			/* 0x97 Reserved */
	0,			/* 0x98 Reserved */
	0,			/* 0x99 Reserved */
	0,			/* 0x9A Reserved */
	0,			/* 0x9B Reserved */
	0,			/* 0x9C Reserved */
	0,			/* 0x9D Reserved */
	0,			/* 0x9E Reserved */
	0,			/* 0x9F Reserved */
	0,			/* 0xA0 Reserved */
	0,			/* 0xA1 Reserved */
	0,			/* 0xA2 Reserved */
	0,			/* 0xA3 Reserved */
	0,			/* 0xA4 Reserved */
	0,			/* 0xA5 Reserved */
	0,			/* 0xA6 Reserved */
	0,			/* 0xA7 Reserved */
	0,			/* 0xA8 Reserved */
	0,			/* 0xA9 Reserved */
	0,			/* 0xAA Reserved */
	0,			/* 0xAB Reserved */
	0,			/* 0xAC Reserved */
	0,			/* 0xAD Reserved */
	0,			/* 0xAE Reserved */
	0,			/* 0xAF Reserved */
	0,			/* 0xB0 Reserved */
	0,			/* 0xB1 Reserved */
	0,			/* 0xB2 Reserved */
	0,			/* 0xB3 Reserved */
	0,			/* 0xB4 Reserved */
	0,			/* 0xB5 Reserved */
	0,			/* 0xB6 Reserved */
	0,			/* 0xB7 Reserved */
	0,			/* 0xB8 Reserved */
	0,			/* 0xB9 Reserved */
	0,			/* 0xBA Reserved */
	0,			/* 0xBB Reserved */
	0,			/* 0xBC Reserved */
	0,			/* 0xBD Reserved */
	0,			/* 0xBE Reserved */
	0,			/* 0xBF Reserved */
	0,			/* 0xC0 Reserved */
	0,			/* 0xC1 Reserved */
	0,			/* 0xC2 Reserved */
	0,			/* 0xC3 Reserved */
	0,			/* 0xC4 Reserved */
	0,			/* 0xC5 Reserved */
	0,			/* 0xC6 Reserved */
	0,			/* 0xC7 Reserved */
	0,			/* 0xC8 Reserved */
	0,			/* 0xC9 Reserved */
	0,			/* 0xCA Reserved */
	0,			/* 0xCB Reserved */
	0,			/* 0xCC Reserved */
	0,			/* 0xCD Reserved */
	0,			/* 0xCE Reserved */
	0,			/* 0xCF Reserved */
	0,			/* 0xD0 Reserved */
	0,			/* 0xD1 Reserved */
	0,			/* 0xD2 Reserved */
	0,			/* 0xD3 Reserved */
	0,			/* 0xD4 Reserved */
	0,			/* 0xD5 Reserved */
	0,			/* 0xD6 Reserved */
	0,			/* 0xD7 Reserved */
	0,			/* 0xD8 Reserved */
	0,			/* 0xD9 Reserved */
	0,			/* 0xDA Reserved */
	0,			/* 0xDB Reserved */
	0,			/* 0xDC Reserved */
	0,			/* 0xDD Reserved */
	0,			/* 0xDE Reserved */
	0,			/* 0xDF Reserved */
	0,			/* 0xE0 Reserved */
	0,			/* 0xE1 Reserved */
	0,			/* 0xE2 Reserved */
	0,			/* 0xE3 Reserved */
	0,			/* 0xE4 Reserved */
	0,			/* 0xE5 Reserved */
	0,			/* 0xE6 Reserved */
	0,			/* 0xE7 Reserved */
	0,			/* 0xE8 Reserved */
	0,			/* 0xE9 Reserved */
	0,			/* 0xEA Reserved */
	0,			/* 0xEB Reserved */
	0,			/* 0xEC Reserved */
	0,			/* 0xED Reserved */
	0,			/* 0xEE Reserved */
	0,			/* 0xEF Reserved */
	0,			/* 0xF0 Reserved */
	0,			/* 0xF1 Reserved */
	0,			/* 0xF2 Reserved */
	0,			/* 0xF3 Reserved */
	0,			/* 0xF4 Reserved */
	0,			/* 0xF5 Reserved */
	0,			/* 0xF6 Reserved */
	0,			/* 0xF7 Reserved */
	0,			/* 0xF8 Reserved */
	0,			/* 0xF9 Reserved */
	0,			/* 0xFA Reserved */
	0,			/* 0xFB Reserved */
	0,			/* 0xFC Reserved */
	0,			/* 0xFD Reserved */
	0,			/* 0xFE Reserved */
	0,			/* 0xFF Reserved */
};

/* warlock directives */
_NOTE(DATA_READABLE_WITHOUT_LOCK(umad_uctx_s::uctx_port))
_NOTE(DATA_READABLE_WITHOUT_LOCK(umad_send::send_len))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ibmf_reg_info::ibmf_class))
_NOTE(DATA_READABLE_WITHOUT_LOCK(ib_user_mad_reg_req::id))

_NOTE(SCHEME_PROTECTS_DATA("safe sharing", genlist_s))
_NOTE(SCHEME_PROTECTS_DATA("safe sharing", llist_head))

static int
ibmf_to_errno_status(int ibmf_status)
{
	int errno_status;

	switch (ibmf_status) {
	case IBMF_SUCCESS:
		errno_status = 0;
		break;
	case IBMF_TRANSPORT_FAILURE:
	case IBMF_FAILURE:
	case IBMF_UNEXP_TRANS_RECVD:
	case IBMF_TRANS_FAILURE:
		errno_status = EIO;
		break;
	case IBMF_PORT_IN_USE:
	case IBMF_TID_IN_USE:
		errno_status = EADDRINUSE;
		break;
	case IBMF_BUSY:
		errno_status = EBUSY;
		break;
	case IBMF_NO_RESOURCES:
	case IBMF_NO_MEMORY:
	case IBMF_INSUFF_COMPS:
		errno_status = ENOMEM;
		break;
	case IBMF_NOT_SUPPORTED:
		errno_status = ENOTSUP;
		break;
	case IBMF_PARTIAL_TRANSFER:
		errno_status = EFBIG;
		break;
	case IBMF_TRANS_TIMEOUT:
		errno_status = ETIMEDOUT;
		break;
	case IBMF_NO_RECORDS:
		errno_status = ENXIO;
		break;
	case IBMF_TOO_MANY_RECORDS:
		errno_status = EFBIG;
		break;
	default:
		errno_status = EINVAL;
	}
	return (errno_status);
}



static void
umad_flush_rcv_queue(umad_uctx_t *uctx, umad_agent_t *agent,
    struct ibmf_reg_info  *ibmf_info)
{

	genlist_entry_t		*entry;
	genlist_entry_t		*next_entry;
	ib_umad_msg_t		*msg;
	ibmf_msg_t		*ibmf_msg;
	umad_agent_t		*madmsg_agent;
	struct umad_send	*umad_ctx;
	int			rc;

	mutex_enter(&uctx->uctx_recv_lock);
	entry = uctx->uctx_recv_list.head;
	while (entry) {
		msg = (ib_umad_msg_t *)entry->data;
		ibmf_msg = msg->umad_msg_ibmf_msg;
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*ibmf_msg))
		madmsg_agent = entry->data_context;
		next_entry = entry->next;

		if (((agent != NULL) && (madmsg_agent == agent)) ||
		    ((ibmf_info != NULL) &&
		    (madmsg_agent->agent_reg == ibmf_info))) {
			umad_ctx = msg->umad_ctx;

			if (umad_ctx) {
				ibmf_msg->im_msgbufs_send.im_bufs_cl_hdr = NULL;
				ibmf_msg->im_msgbufs_send.im_bufs_mad_hdr =
				    NULL;
				ibmf_msg->im_msgbufs_send.im_bufs_cl_hdr_len =
				    0;
				ibmf_msg->im_msgbufs_send.im_bufs_cl_data =
				    NULL;
				ibmf_msg->im_msgbufs_send.im_bufs_cl_data_len =
				    0;
				kmem_free(umad_ctx, umad_ctx->send_len);
				umad_ctx = NULL;
			}

			if (msg->umad_ibmf_msg_cpy_sz) {
				kmem_free(ibmf_msg, msg->umad_ibmf_msg_cpy_sz);
			} else {
				rc = ibmf_free_msg(
				    madmsg_agent->agent_reg->ibmf_reg_handle,
				    &ibmf_msg);
				ASSERT(rc == IBMF_SUCCESS);
			}

			kmem_free(msg, sizeof (*msg));
			delete_genlist(&uctx->uctx_recv_list, entry);
		}
		entry = next_entry;
	}
	mutex_exit(&uctx->uctx_recv_lock);
}

static void *
umad_copy_ibmf_msg(ibmf_msg_t *msgp, size_t *sz)
{
	void		*umad_ibmf_msg_cpy;
	uint8_t		*p;
	ib_mad_hdr_t	*ib_mad_hdr, **ib_mad_hdr_cpy_ptr;
	size_t		size, hdr_size;

	ib_mad_hdr = (ib_mad_hdr_t *)(msgp->im_msgbufs_recv.im_bufs_mad_hdr);

	/*
	 * Calculate how big a buffer we need.
	 */
	hdr_size = umad_get_mad_clhdr_offset(ib_mad_hdr->MgmtClass);
	size = (sizeof (ibmf_msg_t)) + hdr_size +
	    msgp->im_msgbufs_recv.im_bufs_cl_hdr_len +
	    msgp->im_msgbufs_recv.im_bufs_cl_data_len;

	*sz = size;

	umad_ibmf_msg_cpy =  kmem_zalloc(size, KM_SLEEP);
	(void) memcpy(umad_ibmf_msg_cpy, msgp, sizeof (ibmf_msg_t));

	/*
	 * Setup the pointers in our fabricated ibmf message.
	 */
	p = (uint8_t *)umad_ibmf_msg_cpy + sizeof (ibmf_msg_t);
	ib_mad_hdr_cpy_ptr = &(((ibmf_msg_t *)umad_ibmf_msg_cpy)->
	    im_msgbufs_recv.im_bufs_mad_hdr);

	*(uint8_t **)(ib_mad_hdr_cpy_ptr) =  p;
	(void) memcpy(p, ib_mad_hdr, hdr_size);

	p += hdr_size;
	((ibmf_msg_t *)umad_ibmf_msg_cpy)->im_msgbufs_recv.im_bufs_cl_hdr = p;
	(void) memcpy(p, msgp->im_msgbufs_recv.im_bufs_cl_hdr,
	    msgp->im_msgbufs_recv.im_bufs_cl_hdr_len);

	p += msgp->im_msgbufs_recv.im_bufs_cl_hdr_len;
	((ibmf_msg_t *)umad_ibmf_msg_cpy)->im_msgbufs_recv.im_bufs_cl_data = p;
	(void) memcpy(p, msgp->im_msgbufs_recv.im_bufs_cl_data,
	    msgp->im_msgbufs_recv.im_bufs_cl_data_len);

	return (umad_ibmf_msg_cpy);
}

/*
 * Release all resources allocated during umad_init_hca()
 */
static void
umad_release_hca(umad_info_t *info, umad_hca_info_t *hcap)
{
	dev_info_t	*dip = info->info_dip;
	uint_t		j;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*hcap))
	for (j = 0; j < hcap->drv_hca_nports; j++) {
		umad_port_info_t	*port = &hcap->hca_ports[j];
		char			name[MAX_NAME_LEN];

		if (!port->port_num)
			continue;

		(void) snprintf(name, sizeof (name),
		    "umad%d", port->port_minor_idx);
		ddi_remove_minor_node(dip, name);

		(void) snprintf(name, sizeof (name),
		    "issm%d", port->port_minor_idx);
		ddi_remove_minor_node(dip, name);

		mutex_destroy(&port->port_lock);
	}

	kmem_free(hcap->hca_ports, hcap->drv_hca_nports *
	    sizeof (umad_port_info_t));
	hcap->hca_ports = NULL;
	kmem_free(hcap, sizeof (umad_hca_info_t));
}

/*
 * Initialize HCA resources.
 */
static umad_hca_info_t *
umad_init_hca(umad_info_t *info, ib_device_t *device)
{
	ibt_status_t	rc;
	umad_hca_info_t	*hcap;
	uint_t		hca_id = device->ofusr_hca_idx;
	uint_t		minor_idx;
	char		name[MAX_NAME_LEN];
	int		j;
	dev_info_t	*dip = info->info_dip;

	hcap = kmem_zalloc(sizeof (umad_hca_info_t), KM_SLEEP);
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*hcap))
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*device))
	SOL_OFS_DPRINTF_L4(sol_umad_dbg_str,
	    "umad_init_hca(%p, %p) : %x, hcap %p", info, device,
	    device->phys_port_cnt, hcap);
	hcap->hca_ib_devp = device;
	hcap->hca_ports = kmem_zalloc(
	    sizeof (umad_port_info_t) * hcap->drv_hca_nports,
	    KM_SLEEP);
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*hcap->hca_ports))

	/*
	 * ofusr_port_idx is incremented 2 per port, as there are
	 * 2 minor nodes per port (1 for umad and one for issm).
	 * The umad and issm prefix in the minor node string,
	 * however should increment by 1. For the prefix to umad
	 * and issm minor node, sol_umad should use
	 * ofusr_port_index / 2, to get the right prefix.
	 *
	 *  TBD  - Use ofusr_port_idx as the minor number also.
	 *  This is currently used for minor string alone.
	 */
	minor_idx = device->ofusr_port_idx / 2;

	/*
	 * Initialize the ports structures.
	 * Port number != 0 => initialized port
	 */
	for (j = 0; j < hcap->drv_hca_nports; j++) {
		umad_port_info_t	*port;

		port = &hcap->hca_ports[j];

		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*port))

		port->port_hca = hcap;
		llist_head_init(&port->port_ibmf_regs, NULL);
		mutex_init(&port->port_lock, NULL, MUTEX_DRIVER, NULL);
		port->port_num = j + 1;

		port->port_minor_idx = minor_idx;
		(void) snprintf(name, sizeof (name), "umad%d",
		    minor_idx);
		rc = ddi_create_minor_node(dip, name, S_IFCHR,
		    GET_UMAD_MINOR(hca_id, j), DDI_PSEUDO, 0);
		if (rc != DDI_SUCCESS) {
			goto err;
		}

		(void) snprintf(name, sizeof (name), "issm%d",
		    minor_idx);
		rc = ddi_create_minor_node(dip, name, S_IFCHR,
		    GET_ISSM_MINOR(hca_id, j), DDI_PSEUDO, 0);
		if (rc != DDI_SUCCESS) {
			goto err;
		}

		minor_idx++;
	}

	return (hcap);

err:
	umad_release_hca(info, hcap);
	return (NULL);
}

/*
 * Process HCA attach
 */
static ibt_status_t
umad_hca_attach(umad_info_t *info, ib_device_t *device)
{
	umad_hca_info_t	*hca;

	if ((hca = umad_init_hca(info, device)) == NULL) {
		return (IBT_FAILURE);
	}

	mutex_enter(&info->info_mutex);
	info->info_hcas[device->ofusr_hca_idx] = hca;
	mutex_exit(&info->info_mutex);

	return (IBT_SUCCESS);
}

/*
 * Process HCA detach
 */
static ibt_status_t
umad_hca_detach(umad_info_t *info, struct ib_device *device)
{
	umad_hca_info_t	*hcap;

	mutex_enter(&info->info_mutex);
	hcap = info->info_hcas[device->ofusr_hca_idx];
	if (!hcap) {
		mutex_exit(&info->info_mutex);
		return (IBT_NO_SUCH_OBJECT);
	} else if (hcap->hca_ref_cnt) {
		mutex_exit(&info->info_mutex);
		return (IBT_HCA_IN_USE);
	} else {
		info->info_hcas[device->ofusr_hca_idx] = NULL;
		mutex_exit(&info->info_mutex);
		umad_release_hca(info, hcap);
		return (IBT_SUCCESS);
	}

}

/*
 * Process detach of all hcas. Called during driver detach.
 */
static void
umad_detach_hcas(umad_info_t *info)
{
	int		i;
	umad_hca_info_t	*hcap;

	mutex_enter(&info->info_mutex);
	for (i = 0; i < UMAD_MAX_HCAS; i++) {
		hcap = info->info_hcas[i];
		if (hcap) {
			info->info_hcas[i] = NULL;
			mutex_exit(&info->info_mutex);
			umad_release_hca(info, hcap);
			mutex_enter(&info->info_mutex);
		}
	}
	mutex_exit(&info->info_mutex);
}

static umad_hca_info_t *
umad_get_hca(umad_info_t *info, uint_t hca_id, int do_ref_cnt)
{
	int		i;
	umad_hca_info_t	*hca;

	mutex_enter(&info->info_mutex);

	for (i = 0; i < UMAD_MAX_HCAS; i++) {
		hca = info->info_hcas[i];
		if (hca == NULL)
			continue;
		if (hca->drv_hca_idx == hca_id) {
			if (do_ref_cnt)
				hca->hca_ref_cnt++;
			mutex_exit(&info->info_mutex);
			return (hca);
		}
	}

	mutex_exit(&info->info_mutex);
	return (NULL);
}

/*
 * Function:
 *	umad_init_driver_info
 * Output:
 *	info		- driver info
 * Returns:
 * 	IBT_SUCCESS
 *	IBT_INVALID_PARAM
 *	IBT_HCA_IN_USE
 *	IBT_HCA_INVALID
 *	IBT_INVALID_PARAM
 * Called by:
 *	umad_attach
 * Description:
 *	- Calls ib_register_client() to register with sol_ofs
 */
static ibt_status_t
umad_init_driver_info(umad_info_t *info)
{
	ibt_status_t		rc;

	info->info_hca_count 	= 0;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(sol_umad_ib_client))
	sol_umad_ib_client.clnt_hdl = NULL;
	sol_umad_ib_client.dip = info->info_dip;
	rc = ib_register_client(&sol_umad_ib_client);
	if (rc) {
		SOL_OFS_DPRINTF_L3(sol_umad_dbg_str,
		    "ib_register_failed with %x", rc);
	}

	return (rc);
}

/*
 * Function:
 *	umad_context_destroy
 * Input:
 *	info		- driver info
 * Output:
 *	None
 * Returns:
 *	None
 * Called by:
 *	umad_attach
 *	umad_detach
 * Description:
 *	frees driver info resources
 */
static void
umad_context_destroy(umad_info_t *info)
{
	umad_detach_hcas(info);

	ib_unregister_client(&sol_umad_ib_client);
	mutex_enter(&info->info_mutex);
	info->info_hca_count = 0;
	mutex_exit(&info->info_mutex);

	mutex_destroy(&info->info_mutex);
}

/*
 * Function:
 *	_init
 * Input:
 *	None
 * Output:
 *	None
 * Returns:
 *	status
 * Called by:
 *	Framework
 * Description:
 *	driver initialization function
 *	inits debug tracing, river info and calls mod_install
 */
int
_init(void)
{
	int rc;

	rc = ddi_soft_state_init(&umad_statep, sizeof (umad_info_t), 0);

	if (rc != 0)
		goto err;

	rc = mod_install(&modlinkage);

	if (rc != 0) {
		ddi_soft_state_fini(&umad_statep);
	}

err:
	return (rc);
}

/*
 * Function:
 *	_info
 * Input:
 *	None
 * Output:
 *	modinfop	Module information
 * Returns:
 *	status
 * Called by:
 *	Framework
 * Description:
 *	Provides module information
 */
int
_info(struct modinfo *modinfop)
{
	int rc;

	rc = mod_info(&modlinkage, modinfop);

	return (rc);
}

/*
 * Function:
 *	_fini
 * Input:
 *	None
 * Output:
 *	None
 * Returns:
 *	status
 * Called by:
 *	Framework
 * Description:
 *	Cleans up upon module unloading
 */
int
_fini(void)
{
	int rc;

	if ((rc = mod_remove(&modlinkage)) == 0) {
		ddi_soft_state_fini(&umad_statep);
	}

	return (rc);
}

/*
 * Function:
 *	umad_attach
 * Input:
 *	dip		device info
 *	cmd		DDI_ATTACH all others are invalid
 * Output:
 *	None
 * Returns:
 *	DDI_SUCCESS or DDI_FAILURE
 * Called by:
 *	Framwork
 * Description:
 *	Device attach routine
 */
static int
umad_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	int			rc;
	umad_info_t		*info;

	switch (cmd) {
	case DDI_ATTACH:
		if (ddi_soft_state_zalloc(umad_statep, UMAD_INSTANCE)
		    != DDI_SUCCESS)
			goto err1;

		info = ddi_get_soft_state(umad_statep, UMAD_INSTANCE);
		if (info == NULL)
			goto err2;

		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*info))
		info->info_dip = dip;
		mutex_init(&info->info_mutex, NULL, MUTEX_DRIVER, NULL);

		/* initialize our data and per HCA info */
		rc = umad_init_driver_info(info);

		if (rc != 0)
			goto err3;

		ddi_report_dev(dip);
		break;

	default:
		goto err1;
	}

	rc = DDI_SUCCESS;

	return (rc);

err3:
	umad_context_destroy(info);
err2:
	ddi_soft_state_free(umad_statep, UMAD_INSTANCE);
err1:
	rc = DDI_FAILURE;

	return (rc);
}

/*
 * Function:
 *	umad_detach
 * Input:
 *	dip		Device pointer
 *	cmd		DDI_DETACH all others are an error
 * Output:
 *	None
 * Returns:
 *	DDI_SUCCESS or DDI_FAILURE
 * Called by:
 *	Framework
 * Description:
 *	Used when a device is removed
 */
static int
/* LINTED E_FUNC_ARG_UNUSED */
umad_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int		rc = DDI_SUCCESS;
	umad_info_t	*info;

	switch (cmd) {
	case DDI_DETACH:
		info = ddi_get_soft_state(umad_statep, UMAD_INSTANCE);
		umad_context_destroy(info);
		ddi_soft_state_free(umad_statep, UMAD_INSTANCE);
		break;

	default:
		rc = DDI_FAILURE;
		break;
	}

	return (rc);
}

/*
 * Function:
 *	umad_getinfo
 * Input:
 *	dip	device pointer
 *	cmd	DDI_INFO_DEVT2DEVINFO or DDI_INFO_DEV2INSTANCE
 *	arg	Unused
 * Output:
 *	resultp	device pointer or device instance as per cmd
 * Returns:
 *	status
 * Called by:
 *	Framework
 * Description:
 *	Gets information about specific device
 */
static int
umad_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **resultp)
{
	int rc;

#if defined(__lint)
	extern void dummy2(void *);

	dummy2(arg);
#endif

	switch (cmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*resultp = (void *)dip;
		break;

	case DDI_INFO_DEVT2INSTANCE:
		*resultp = (void *)UMAD_INSTANCE;
		rc = DDI_SUCCESS;
		break;

	default:
		rc = DDI_FAILURE;
		break;
	}

	return (rc);
}

/*
 * Function:
 *	umad_prop_op
 * Input:
 *	dev		device
 *	dip		device pointer
 *	prop_op		which property operation
 *	flags		property flags
 *	name		proper name
 * Output:
 *	valuep		- property value
 *	lengthp		- propery length
 * Returns:
 *	status
 * Called by:
 *	Framework
 * Description:
 *	Passes straight through to default ddi_prop_op()
 */
static int
umad_prop_op(
	dev_t dev,
	dev_info_t *dip,
	ddi_prop_op_t prop_op,
	int flags,
	char *name,
	caddr_t valuep,
	int *lengthp)
{
	int rc;

	rc = ddi_prop_op(dev, dip, prop_op, flags, name, valuep, lengthp);

	return (rc);
}


/* Returns an array of mad classes associated with IBMF class */
static const uint8_t *
umad_get_mad_classes_by_ibmf_class(ibmf_client_type_t ibmf_class)
{
	const struct ibmf_class_to_mad_type *entry;

	for (entry = &ibmf_class_to_mad_types[0];
	    entry->ibmf_class != 0;
	    ++entry) {
		if (ibmf_class == entry->ibmf_class)
			return (entry->mad_types);
	}
	return (NULL);
}

/* Returns an agent from its ID. */
static umad_agent_t *
umad_get_agent_by_id(umad_uctx_t *uctx, uint32_t agent_id)
{
	umad_agent_t *agent;
	llist_head_t *entry;

	ASSERT(MUTEX_HELD(&uctx->uctx_lock));

	/* Look for the agent */
	list_for_each(entry, &uctx->uctx_agent_list) {
		agent = entry->ptr;

		if (agent_id == agent->agent_req.id)
			return (agent);
	}

	return (NULL);
}

/* Returns an agent from its MAD class. */
static umad_agent_t *
umad_get_agent_by_class(umad_uctx_t *uctx, uint8_t agent_class)
{
	umad_agent_t *agent;
	llist_head_t *entry;

	ASSERT(MUTEX_HELD(&uctx->uctx_lock));

	/* Look for the agent */
	list_for_each(entry, &uctx->uctx_agent_list) {
		agent = entry->ptr;
		if (agent_class == agent->agent_req.mgmt_class)
			return (agent);
	}

	return (NULL);
}

/* Returns agents from its MAD class. */
static void
umad_get_agents_by_class(umad_uctx_t *uctx, uint8_t agent_class,
	llist_head_t *mad_class_agent_list)
{
	umad_agent_t *agent;
	llist_head_t *entry, *ret_entry;

	ASSERT(MUTEX_HELD(&uctx->uctx_lock));

	/* Look for agents in the specified class */
	list_for_each(entry, &uctx->uctx_agent_list) {
		agent = entry->ptr;
		if (agent_class == agent->agent_req.mgmt_class) {
			ret_entry = kmem_zalloc(sizeof (llist_head_t),
			    KM_SLEEP);
			ret_entry->ptr = agent;
			llist_add(ret_entry, mad_class_agent_list);
		}
	}
}

/*
 * Register the agent with a class.
 * mgmt_class is given from userspace.
 */
static int
umad_register_agent(struct umad_agent_s *agent)
{
	ibmf_client_type_t	ibmf_class;
	uint8_t			mgmt_class_num = agent->agent_req.mgmt_class;
	umad_port_info_t	*port = agent->agent_uctx->uctx_port;
	const umad_hca_info_t	*hca = port->port_hca;
	int			rc, rc2;
	ibmf_register_info_t    reg_info	= {0, };
	ibmf_impl_caps_t	impl_caps	= {0, };
	uint_t			flags = 0, found = 0;
	const uint8_t		*umad_types;
	struct ibmf_reg_info	*ibmf_info;
	llist_head_t		*entry;
	umad_uctx_t		*uctx = agent->agent_uctx;
	struct umad_agent_s	*other_agent = NULL;

	SOL_OFS_DPRINTF_L5(sol_umad_dbg_str,
	    "umad_register_agent: Enter agent: %p, port %d", agent,
	    port->port_num);

	/*
	 * Map MAD class to IBMF class
	 */
	ibmf_class = umad_type_to_ibmf_class[mgmt_class_num];

	/*
	 * It is is reserved, bail
	 */
	if (ibmf_class == 0) {
		rc = EINVAL;
		goto done;
	}

	/*
	 * Check to see if any other mad classes also map to this IBMF class
	 * And if they do, check to see if we have already registerd that
	 * class, if we have increment the reference count. Then we
	 * need to check if this request wants unsolicited MADs, if it
	 * doesn't, or if it does and an async callback is already registered
	 * on this port (by the other agent or an other user process), we are
	 * done, otherwise we  need to register an async callback
	 */
	umad_types = umad_get_mad_classes_by_ibmf_class(ibmf_class);

	mutex_enter(&port->port_lock);

	if (umad_types != NULL) {

		for (; *umad_types != 0; ++umad_types) {
			mutex_enter(&uctx->uctx_lock);
			other_agent = umad_get_agent_by_class(uctx,
			    *umad_types);
			mutex_exit(&uctx->uctx_lock);

			if (other_agent != NULL) {
				ibmf_info = other_agent->agent_reg;
				agent->agent_reg = ibmf_info;
				break;
			}

		}
	}

	if (other_agent != NULL) {
		rc = 0;
		if (request_unsolicited_mad(agent->agent_req.method_mask)) {
			if (other_agent->agent_flags & UMAD_HANDLING_ASYNC) {
				agent->agent_flags |= UMAD_HANDLING_ASYNC;
				mutex_enter(&uctx->uctx_lock);
				uctx->uctx_async_ref++;
				mutex_exit(&uctx->uctx_lock);
			} else {
				mutex_enter(&ibmf_info->ibmf_reg_lock);
				if (!(ibmf_info->ibmf_flags &
				    UMAD_IBMF_ASYNC_REGISTERED)) {
					rc = ibmf_setup_async_cb(ibmf_info->
					    ibmf_reg_handle,
					    IBMF_QP_HANDLE_DEFAULT,
					    umad_unsolicited_cb,
					    (void *)ibmf_info, 0);

					if (rc != IBMF_SUCCESS) {
						SOL_OFS_DPRINTF_L2(
						    sol_umad_dbg_str,
						    "umad_register_agent: "
						    "ibmf_setup_async_cb fail: "
						    "%d, ibmf_info %p",
						    rc, ibmf_info);
					} else {
						ibmf_info->ibmf_flags |=
						    UMAD_IBMF_ASYNC_REGISTERED;
					}
				}
				if (rc == IBMF_SUCCESS || rc == 0) {
					entry = kmem_zalloc(
					    sizeof (llist_head_t), KM_SLEEP);
					mutex_enter(&uctx->uctx_lock);
					entry->ptr = uctx;
					llist_add(entry,
					    &ibmf_info->ibmf_uctx_list);
					agent->agent_flags |=
					    UMAD_HANDLING_ASYNC;
					uctx->uctx_async_ref++;
					mutex_exit(&uctx->uctx_lock);
				} else {
					rc = EINVAL;
				}
				mutex_exit(&ibmf_info->ibmf_reg_lock);
			}
		}
		if (rc == IBMF_SUCCESS || rc == 0) {
			mutex_enter(&ibmf_info->ibmf_reg_lock);
			ibmf_info->ibmf_reg_refcnt++;
			mutex_exit(&ibmf_info->ibmf_reg_lock);
		}
		mutex_exit(&port->port_lock);
		return (rc);
	}

	/*
	 * At this point we need to check if there is already an
	 * ibmf_info already associated with this HCA, port and ibmf
	 * class.  If so, simply increment the reference count
	 * and set the agent's agent_reg field to point to the
	 * ibmf_info structure that was found. (under locking)
	 * Also check if the agent wants to recieve unsolicited MADs, we need
	 * to check if this agent's uctx is on the ibmf_info
	 * uctx list, if not we need to add it.
	 */
	if (!llist_empty(&port->port_ibmf_regs)) {
		list_for_each(entry, &port->port_ibmf_regs) {
			ibmf_info = (struct ibmf_reg_info *)entry->ptr;
			if (ibmf_info->ibmf_class == ibmf_class) {
				found = 1;
				break;
			}
		}
	}

	if (found) {
		rc = 0;
		mutex_enter(&ibmf_info->ibmf_reg_lock);
		if (request_unsolicited_mad(agent->agent_req.method_mask)) {
			found = 0;
			list_for_each(entry, &ibmf_info->ibmf_uctx_list) {
				if ((umad_uctx_t *)entry->ptr == uctx) {
					found = 1;
					agent->agent_flags |=
					    UMAD_HANDLING_ASYNC;
					mutex_enter(&uctx->uctx_lock);
					uctx->uctx_async_ref++;
					mutex_exit(&uctx->uctx_lock);
					break;
				}
			}

			if (!found) {
				/*
				 * First check to see is an async callback
				 * has been registered with IBMF, if not
				 * register one.  Then Add the uctx to the
				 * ibmf_reg_info ucts list.
				 */
				if (!(ibmf_info->ibmf_flags &
				    UMAD_IBMF_ASYNC_REGISTERED)) {
					rc = ibmf_setup_async_cb(ibmf_info->
					    ibmf_reg_handle,
					    IBMF_QP_HANDLE_DEFAULT,
					    umad_unsolicited_cb,
					    (void *)ibmf_info, 0);

					if (rc != IBMF_SUCCESS) {
						SOL_OFS_DPRINTF_L2(
						    sol_umad_dbg_str,
						    "umad_register_agent: "
						    "ibmf_setup_async_cb fail: "
						    "%d, ibmf_info %p",
						    rc, ibmf_info);
					} else {
						ibmf_info->ibmf_flags |=
						    UMAD_IBMF_ASYNC_REGISTERED;
					}
				}
				if (rc == IBMF_SUCCESS || rc == 0) {
					entry = kmem_zalloc(
					    sizeof (llist_head_t), KM_SLEEP);
					mutex_enter(&uctx->uctx_lock);
					entry->ptr = uctx;
					llist_add(entry,
					    &ibmf_info->ibmf_uctx_list);
					agent->agent_flags |=
					    UMAD_HANDLING_ASYNC;
					uctx->uctx_async_ref++;
					mutex_exit(&uctx->uctx_lock);
				} else {
					rc = EINVAL;
				}
			} /* !found this uctx on ibmf_info async list */
		} /* request_unsolicited_mad ? */

		if (rc == IBMF_SUCCESS || rc == 0) {
			ibmf_info->ibmf_reg_refcnt++;
			agent->agent_reg = ibmf_info;
		}
		mutex_exit(&ibmf_info->ibmf_reg_lock);
		mutex_exit(&port->port_lock);
		return (rc);
	} /* found class already registered with IBMF */

	/*
	 * This is a new registration.
	 */
	ibmf_info = kmem_zalloc(sizeof (struct ibmf_reg_info), KM_SLEEP);
	mutex_init(&ibmf_info->ibmf_reg_lock, NULL, MUTEX_DRIVER, NULL);
	llist_head_init(&ibmf_info->ibmf_uctx_list, NULL);

	if (agent->agent_req.rmpp_version)
		flags = IBMF_REG_FLAG_RMPP;

	reg_info.ir_ci_guid = htonll(hca->drv_hca_guid);
	reg_info.ir_port_num = port->port_num;
	reg_info.ir_client_class = ibmf_class;

	mutex_enter(&ibmf_info->ibmf_reg_lock);


	rc = ibmf_register(&reg_info, IBMF_VERSION, flags, NULL, NULL,
	    &ibmf_info->ibmf_reg_handle, &impl_caps);

	if (rc != IBMF_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
		    "umad_register_agent: ibmf_register fail: %d, ibmf_info "
		    "%p", rc, ibmf_info);
		mutex_exit(&ibmf_info->ibmf_reg_lock);
		kmem_free(ibmf_info, sizeof (*ibmf_info));
	} else {
		/*
		 * If the client wants to receive some unsolicited MADs.
		 * Register an async callback and add the clients uctx
		 * to the ibmf_reg_info uctx list.
		 */
		if (request_unsolicited_mad(agent->agent_req.method_mask)) {
			rc = ibmf_setup_async_cb(ibmf_info->ibmf_reg_handle,
			    IBMF_QP_HANDLE_DEFAULT, umad_unsolicited_cb,
			    (void *)ibmf_info, 0);

			if (rc != IBMF_SUCCESS) {
				SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
				    "umad_register_agent:ibmf_setup_async_cb "
				    "fail: %d, ibmf_info %p", rc, ibmf_info);
				rc2 = ibmf_unregister(&ibmf_info->
				    ibmf_reg_handle, 0);

				if (rc2 != IBMF_SUCCESS)
					SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
					    "umad_register_agent: "
					    "ibmf_unregister fail: %d, "
					    "ibmf_info %p", rc2, ibmf_info);
				mutex_exit(&ibmf_info->ibmf_reg_lock);
				kmem_free(ibmf_info, sizeof (*ibmf_info));
				mutex_exit(&port->port_lock);
				return (rc);
			} else {
				agent->agent_flags |= UMAD_HANDLING_ASYNC;
				ibmf_info->ibmf_flags |=
				    UMAD_IBMF_ASYNC_REGISTERED;
				/*
				 * Add the agent's uctx to the uctx list.
				 */
				entry = kmem_zalloc(sizeof (llist_head_t),
				    KM_SLEEP);
				mutex_enter(&uctx->uctx_lock);
				entry->ptr = uctx;
				llist_add(entry, &ibmf_info->ibmf_uctx_list);
				uctx->uctx_async_ref++;
				mutex_exit(&uctx->uctx_lock);
			}
		}
		ibmf_info->ibmf_reg_refcnt++;
		ibmf_info->ibmf_class = ibmf_class;
		agent->agent_reg = ibmf_info;
		mutex_exit(&ibmf_info->ibmf_reg_lock);

		entry = kmem_zalloc(sizeof (llist_head_t), KM_SLEEP);
		entry->ptr = ibmf_info;
		llist_add(entry, &port->port_ibmf_regs);
	}
	mutex_exit(&port->port_lock);
done:
	return (rc);
}

/*
 * Function:
 *      umad_queue_mad_msg
 * Input:
 *	port            - handle to ibmf
 *      ibmf_msg        - The incoming SM MAD
 * Output:
 *	None
 * Returns:
 *     0 on success, otherwise error number
 * Called by:
 *      umad_solicitied_cb and umad_unsolicited_cb
 * Description:
 *      creates a umad_msg and adds it to the appropriate user's context
 */

static int
umad_queue_mad_msg(struct umad_agent_s *agent, ibmf_msg_t *ibmf_msg,
    struct umad_send *umad_ctx, size_t size)
{
	int 		rc;
	ib_umad_msg_t	*umad_msg;
	umad_uctx_t	*uctx = agent->agent_uctx;
	ib_mad_hdr_t	*ib_mad_hdr;

	if (agent->agent_uctx == NULL) {
		rc = ENOENT;
		goto err1;
	}

	umad_msg = kmem_zalloc(sizeof (*umad_msg), KM_NOSLEEP);
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*umad_msg))
	if (umad_msg == NULL) {
		rc = ENOMEM;
		goto err1;
	}

	umad_msg->umad_msg_hdr.id = agent->agent_req.id;
	umad_msg->umad_msg_hdr.status =
	    ibmf_to_errno_status(ibmf_msg->im_msg_status);
	umad_msg->umad_ctx = umad_ctx;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*ibmf_msg))

	if ((ibmf_msg->im_msg_status == IBMF_SUCCESS) ||
	    ((ibmf_msg->im_msg_status == IBMF_TRANS_TIMEOUT) &&
	    (ibmf_msg->im_msgbufs_recv.im_bufs_mad_hdr != NULL))) {
		ib_mad_hdr =
		    (ib_mad_hdr_t *)ibmf_msg->im_msgbufs_recv.im_bufs_mad_hdr;
		umad_msg->umad_msg_hdr.length =
		    sizeof (struct ib_user_mad) +
		    umad_get_mad_clhdr_offset(ib_mad_hdr->MgmtClass) +
		    ibmf_msg->im_msgbufs_recv.im_bufs_cl_hdr_len +
		    ibmf_msg->im_msgbufs_recv.im_bufs_cl_data_len;
	} else {
		ib_mad_hdr =
		    (ib_mad_hdr_t *)ibmf_msg->im_msgbufs_send.im_bufs_mad_hdr;
		umad_msg->umad_msg_hdr.length =
		    sizeof (struct ib_user_mad) +
		    umad_get_mad_clhdr_offset(ib_mad_hdr->MgmtClass) +
		    ibmf_msg->im_msgbufs_send.im_bufs_cl_hdr_len;
	}

	umad_msg->umad_msg_hdr.qpn =
	    htonl(ibmf_msg->im_local_addr.ia_remote_qno);
	umad_msg->umad_msg_hdr.lid =
	    htons(ibmf_msg->im_local_addr.ia_remote_lid);
	umad_msg->umad_msg_hdr.sl =
	    htonl(ibmf_msg->im_local_addr.ia_service_level);

	umad_msg->umad_msg_ibmf_msg = ibmf_msg;

	/*
	 * We were called from the unsolicited cb handler, the ibmf msg
	 * is a copy, let umad_read know it just has to kmem_free() it
	 * and not call IBMF to free it.
	 */
	if (size) {
		umad_msg->umad_ibmf_msg_cpy_sz = size;
	}

	mutex_enter(&uctx->uctx_recv_lock);
	if (! add_genlist(&uctx->uctx_recv_list, (uintptr_t)umad_msg, agent)) {
		kmem_free(umad_msg, sizeof (*umad_msg));
		mutex_exit(&uctx->uctx_recv_lock);
		rc = ENOMEM;
		goto err1;
	}
	cv_broadcast(&uctx->uctx_recv_cv);
	mutex_exit(&uctx->uctx_recv_lock);

	pollwakeup(&uctx->uctx_pollhead, POLLIN | POLLRDNORM);

	rc = 0;

err1:
	return (rc);
}

/* Frees up user context state */
static void
umad_release_uctx(umad_uctx_t *uctx)
{
	ASSERT(genlist_empty(&uctx->uctx_recv_list));
	ASSERT(llist_empty(&uctx->uctx_agent_list));

	cv_destroy(&uctx->uctx_recv_cv);
	mutex_destroy(&uctx->uctx_lock);
	mutex_destroy(&uctx->uctx_recv_lock);
}

/*
 * Function:
 *	umad_open
 * Input:
 *	devp		device pointer
 *	flag		Unused
 *	otyp		Open type (just validated)
 *	cred		Unused
 * Output:
 *	None
 * Returns:
 *	status
 * Called by:
 *	Device open framework
 * Description:
 *	If this is the issm device, modify the port to indicate that this is
 *	a subnet manager.  If regular umad device, allocate and initialize
 *	a new user context and connect it to the hca info.  Return the new
 *	dev_t for the new minor.
 */
static int
umad_open(dev_t *dev, int flag, int otyp, cred_t *cred)
{
	umad_info_t		*info;
	minor_t			minor;
	minor_t			ctx_minor;
	int			node_id, port_num;
	int			rc;
	umad_hca_info_t		*hca;
	umad_port_info_t	*port;
	umad_uctx_t		*uctx;

#if defined(__lint)
	extern void dummy(int);

	dummy(flag);
#endif

	rc = priv_policy(cred, PRIV_SYS_NET_CONFIG, B_FALSE, EACCES, NULL);
	if (rc != 0)
		return (rc);

	info = ddi_get_soft_state(umad_statep, UMAD_INSTANCE);
	if (info == NULL)
		return (ENXIO);

	if (otyp != OTYP_CHR)
		return (EINVAL);

	/* lookup the node and port #s */
	minor = getminor(*dev);

	node_id	= GET_NODE(minor);
	if ((hca = umad_get_hca(info, node_id, 1)) == NULL)
		return (ENXIO);

	port_num = GET_PORT(minor);
	port = &hca->hca_ports[port_num];

	if (ISSM_MINOR(minor)) {
		mutex_enter(&port->port_lock);
		if (port->port_issm_open_cnt) {
			mutex_exit(&port->port_lock);
			rc = EBUSY;
			goto err1;
		}
		port->port_issm_open_cnt++;
		mutex_exit(&port->port_lock);

		rc = ib_modify_port(hca->hca_ib_devp,
		    port->port_num, B_TRUE);
		if (rc) {
			mutex_enter(&port->port_lock);
			port->port_issm_open_cnt--;
			mutex_exit(&port->port_lock);
			goto err1;
		}
	} else {
		unsigned int uctx_num;

		uctx = kmem_zalloc(sizeof (umad_uctx_t), KM_SLEEP);
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*uctx))

		mutex_init(&uctx->uctx_lock, NULL, MUTEX_DRIVER, NULL);
		cv_init(&uctx->uctx_recv_cv, NULL, CV_DRIVER, NULL);
		init_genlist(&uctx->uctx_recv_list);
		mutex_init(&uctx->uctx_recv_lock, NULL, MUTEX_DRIVER, NULL);
		llist_head_init(&uctx->uctx_agent_list, NULL);
		uctx->uctx_port = port;

		mutex_enter(&info->info_mutex);
		mutex_enter(&port->port_lock);

		/* Find a free entry in uctx list */
		for (uctx_num = 0; uctx_num < MAX_UCTX; uctx_num++) {
			if (info->info_uctx[uctx_num] == NULL)
				break;
		}

		if (uctx_num == MAX_UCTX) {
			/* No room found */
			mutex_exit(&port->port_lock);
			mutex_exit(&info->info_mutex);

			umad_release_uctx(uctx);

			rc = EBUSY;
			goto err1;
		}

		ctx_minor = GET_NEW_UCTX_MINOR(minor, uctx_num);
		info->info_uctx[uctx_num] = uctx;
		*dev = makedevice(getmajor(*dev), ctx_minor);

		mutex_exit(&port->port_lock);
		mutex_exit(&info->info_mutex);
	}
	return (rc);

err1:
	/* ibt_get_hca() incremented the ref_cnt. Now decrement the count */
	mutex_enter(&info->info_mutex);
	hca->hca_ref_cnt--;
	mutex_exit(&info->info_mutex);
	return (rc);
}

/*
 * Function:
 *	umad_close
 * Input:
 *	dev		device
 *	flag		Unused
 *	otyp		Unused
 *	cred		Unused
 * Output:
 *	None
 * Returns:
 *	status
 * Called by:
 *	Device close framework
 * Description:
 *	Unwinds open while waiting for any pending I/O to complete.
 */
/* ARGSUSED1 */
static int
umad_close(dev_t dev, int flag, int otyp, cred_t *cred)
{
	umad_info_t		*info;
	minor_t			minor;
	umad_port_info_t	*port;
	umad_uctx_t		*uctx;
	llist_head_t		*lentry;
	llist_head_t		*lentry_temp;
	umad_agent_t		*agent;
	int			port_num;
	umad_hca_info_t		*hca;
	int			node_id;

	SOL_OFS_DPRINTF_L5(sol_umad_dbg_str, "umad_close: Enter");

	info = ddi_get_soft_state(umad_statep, UMAD_INSTANCE);
	if (info == NULL)
		return (ENXIO);

	minor = getminor(dev);

	node_id	= GET_NODE(minor);
	if ((hca = umad_get_hca(info, node_id, 0)) == NULL)
		return (ENXIO);

	port_num = GET_PORT(minor);
	port = &hca->hca_ports[port_num];

	ASSERT(port != NULL);

	if (ISSM_MINOR(minor)) {
		if (ib_modify_port(hca->hca_ib_devp,
		    port->port_num, B_FALSE) != 0) {
			SOL_OFS_DPRINTF_L5(sol_umad_dbg_str,
			    "umad_close: ib_modify_port failed");
			return (EINVAL);
		}

		mutex_enter(&port->port_lock);
		port->port_issm_open_cnt--;
		ASSERT(port->port_issm_open_cnt == 0);
		mutex_exit(&port->port_lock);

	} else {

		mutex_enter(&info->info_mutex);
		uctx = info->info_uctx[GET_UCTX(minor)];
		ASSERT(uctx != NULL);

		mutex_enter(&uctx->uctx_lock);

		/* Unregister the agents. Cancel the pending operations. */
		lentry = uctx->uctx_agent_list.nxt;
		lentry_temp = lentry->nxt;
		while (lentry != &uctx->uctx_agent_list) {
			ASSERT(lentry);
			agent = lentry->ptr;

			mutex_exit(&uctx->uctx_lock);
			mutex_exit(&info->info_mutex);
			(void) umad_unregister(&agent->agent_req, uctx);
			lentry = lentry_temp;
			lentry_temp = lentry->nxt;
			mutex_enter(&info->info_mutex);
			mutex_enter(&uctx->uctx_lock);
		}

		mutex_exit(&uctx->uctx_lock);

		umad_release_uctx(uctx);
		kmem_free(uctx, sizeof (umad_uctx_t));

		info->info_uctx[GET_UCTX(minor)] = NULL;
		mutex_exit(&info->info_mutex);
	}

	mutex_enter(&info->info_mutex);
	hca->hca_ref_cnt--;
	ASSERT(hca->hca_ref_cnt >= 0);
	mutex_exit(&info->info_mutex);

	return (0);
}

/*
 * return where optional header starts relative to the start
 * of the transmitted mad
 */
static int
umad_get_mad_clhdr_offset(uint8_t mgmt_class)
{
	switch (mgmt_class) {
	case MAD_MGMT_CLASS_SUBN_LID_ROUTED:
	case MAD_MGMT_CLASS_SUBN_DIRECT_ROUTE:
	case MAD_MGMT_CLASS_PERF:
	case MAD_MGMT_CLASS_BM:
	case MAD_MGMT_CLASS_DEV_MGT:
	case MAD_MGMT_CLASS_COMM_MGT:
		return (IB_MGMT_MAD_HDR);
	case MAD_MGMT_CLASS_SUBN_ADM:
		return (IB_MGMT_RMPP_HDR);
	case MAD_MGMT_CLASS_SNMP:
		return (IB_MGMT_SNMP_HDR);
	default:
		if (((mgmt_class >= MAD_MGMT_CLASS_VENDOR_START) &&
		    (mgmt_class <= MAD_MGMT_CLASS_VENDOR_END)) ||
		    ((mgmt_class >= MAD_MGMT_CLASS_APPLICATION_START) &&
		    (mgmt_class <= MAD_MGMT_CLASS_APPLICATION_END)))
			return (IB_MGMT_MAD_HDR);
		else if ((mgmt_class >= MAD_MGMT_CLASS_VENDOR2_START) &&
		    (mgmt_class <= MAD_MGMT_CLASS_VENDOR2_END))
			return (IB_MGMT_RMPP_HDR);
		else {
			SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
			    "umad_get_mad_clhdr_offset:"
			    " got illegal management class %d", mgmt_class);
			return (0);  /* invalid mad */
		}
	}
}

/*
 * return the offset of the mad data in the transmitted mad
 * following all headers
 */
static int
umad_get_mad_data_offset(uint8_t mgmt_class)
{
	switch (mgmt_class) {
	case MAD_MGMT_CLASS_SUBN_LID_ROUTED:
	case MAD_MGMT_CLASS_SUBN_DIRECT_ROUTE:
	case MAD_MGMT_CLASS_PERF:
	case MAD_MGMT_CLASS_BM:
	case MAD_MGMT_CLASS_DEV_MGT:
	case MAD_MGMT_CLASS_COMM_MGT:
		return (IB_MGMT_MAD_HDR);
	case MAD_MGMT_CLASS_SUBN_ADM:
		return (IB_MGMT_SA_HDR);
	case MAD_MGMT_CLASS_SNMP:
		return (IB_MGMT_SNMP_DATA);
	default:
		if (((mgmt_class >= MAD_MGMT_CLASS_VENDOR_START) &&
		    (mgmt_class <= MAD_MGMT_CLASS_VENDOR_END)) ||
		    ((mgmt_class >= MAD_MGMT_CLASS_APPLICATION_START) &&
		    (mgmt_class <= MAD_MGMT_CLASS_APPLICATION_END)))
			return (IB_MGMT_MAD_HDR);
		else if ((mgmt_class >= MAD_MGMT_CLASS_VENDOR2_START) &&
		    (mgmt_class <= MAD_MGMT_CLASS_VENDOR2_END))
			return (IB_MGMT_VENDOR_HDR);
		else {
			SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
			    "umad_get_mad_clhdr_offset:"
			    " got illegal management class %d", mgmt_class);
			return (0);  /* invalid mad */
		}
	}

}

/*
 * Function:
 *	umad_read
 * Input:
 *	dev		device
 *	uiop		User I/O pointer
 *	credp		Unused
 * Output:
 *	None
 * Returns:
 *	status
 * Called by:
 *	Device read framework
 * Description:
 *	Cannot read from ISSM device.  Read from UMAD device
 *	does usual checks for blocking and when data is present,
 *	removes message from user context receive list, fills in user
 *	space with message and frees kernel copy of the message.
 */
/* ARGSUSED2 */
static int
umad_read(dev_t dev, struct uio *uiop, cred_t *credp)
{
	int			minor, rc = 0;
	size_t			data_len;
	umad_info_t		*info;
	umad_uctx_t		*uctx;
	genlist_entry_t		*entry;
	ib_umad_msg_t		*umad_msg;
	ibmf_msg_t		*ibmf_msg;
	struct umad_agent_s	*agent;
	ib_mad_hdr_t		*ib_mad_hdr;
	struct umad_send 	*umad_ctx;

	rc = priv_policy(credp, PRIV_SYS_NET_CONFIG, B_FALSE, EACCES, NULL);
	if (rc != 0)
		return (rc);

	info = ddi_get_soft_state(umad_statep, UMAD_INSTANCE);
	if (info == NULL) {
		rc = ENXIO;
		goto err1;
	}

	minor = getminor(dev);

	if (ISSM_MINOR(minor)) {
		rc = ENXIO;
		goto err1;
	}

	mutex_enter(&info->info_mutex);
	uctx = info->info_uctx[GET_UCTX(minor)];
	mutex_exit(&info->info_mutex);
	ASSERT(uctx != NULL);
	if (uctx->uctx_port == NULL) {
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str, "port NULL");
		rc = ENXIO;
		goto err1;
	}

	mutex_enter(&uctx->uctx_recv_lock);

	/* Check to see if we are in blocking mode or not */
	if (! (uiop->uio_fmode & (FNDELAY | FNONBLOCK))) {
		while (genlist_empty(&uctx->uctx_recv_list)) {
			if (cv_wait_sig(&uctx->uctx_recv_cv,
			    &uctx->uctx_recv_lock) == 0) {
				mutex_exit(&uctx->uctx_recv_lock);
				return (EINTR);
			}
		}
	} else if (genlist_empty(&uctx->uctx_recv_list)) {
		mutex_exit(&uctx->uctx_recv_lock);
			return (EAGAIN);

	}

	entry = remove_genlist_head(&uctx->uctx_recv_list);
	mutex_exit(&uctx->uctx_recv_lock);

	ASSERT(entry != NULL);
	agent = entry->data_context;

	umad_msg = (ib_umad_msg_t *)entry->data;
	ibmf_msg =  (ibmf_msg_t *)umad_msg->umad_msg_ibmf_msg;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*ibmf_msg))

	/*
	 * Copy the OFED MAD cmd struct first.
	 */
	data_len = min(uiop->uio_resid, sizeof (struct ib_user_mad));
	rc = uiomove(umad_msg, data_len, UIO_READ, uiop);
	if (rc)
		goto err2;
	/*
	 * Check to see if we have room for the returned MAD
	 * (See umad_queue_mad_msg()). Usually not the case when
	 * we are doing RMPP. If we don't then need to requeue the
	 * MAD and return ENOSPC to the caller.
	 */
	if ((umad_msg->umad_msg_hdr.length - data_len) > uiop->uio_resid) {
		mutex_enter(&uctx->uctx_recv_lock);
		insert_genlist_head(&uctx->uctx_recv_list, entry);
		mutex_exit(&uctx->uctx_recv_lock);
		rc = ENOSPC;
		goto err1;
	}

	if ((ibmf_msg->im_msg_status == IBMF_SUCCESS) ||
	    ((ibmf_msg->im_msg_status == IBMF_TRANS_TIMEOUT) &&
	    (ibmf_msg->im_msgbufs_recv.im_bufs_mad_hdr != NULL))) {
		/*
		 * Success or Receive timeout
		 */
		ib_mad_hdr =
		    (ib_mad_hdr_t *)ibmf_msg->im_msgbufs_recv.im_bufs_mad_hdr;
		data_len = umad_get_mad_clhdr_offset(ib_mad_hdr->MgmtClass);
		data_len = min(uiop->uio_resid, data_len);

		rc = uiomove(ibmf_msg->im_msgbufs_recv.im_bufs_mad_hdr,
		    data_len, UIO_READ, uiop);
		if (rc)
			goto err2;

		data_len = min(uiop->uio_resid,
		    ibmf_msg->im_msgbufs_recv.im_bufs_cl_hdr_len);

		rc = uiomove(ibmf_msg->im_msgbufs_recv.im_bufs_cl_hdr,
		    data_len, UIO_READ, uiop);
		if (rc)
			goto err2;

		data_len = min(uiop->uio_resid,
		    ibmf_msg->im_msgbufs_recv.im_bufs_cl_data_len);

		rc = uiomove(ibmf_msg->im_msgbufs_recv.im_bufs_cl_data,
		    data_len, UIO_READ, uiop);
	} else  {
		/*
		 * Send error, return the MAD that was sent back to
		 * userland.
		 */
		ib_mad_hdr = (ib_mad_hdr_t *)
		    ibmf_msg->im_msgbufs_send.im_bufs_mad_hdr;
		data_len = umad_get_mad_clhdr_offset(ib_mad_hdr->MgmtClass);
		data_len = min(uiop->uio_resid, data_len);

		rc = uiomove(ibmf_msg->im_msgbufs_send.im_bufs_mad_hdr,
		    data_len, UIO_READ, uiop);
		if (rc)
			goto err2;

		data_len = min(uiop->uio_resid,
		    ibmf_msg->im_msgbufs_send.im_bufs_cl_hdr_len);

		rc = uiomove(ibmf_msg->im_msgbufs_send.im_bufs_cl_hdr,
		    data_len, UIO_READ, uiop);
	}
err2:
	umad_ctx = umad_msg->umad_ctx;

	if (umad_ctx) {
		ibmf_msg->im_msgbufs_send.im_bufs_mad_hdr = NULL;
		ibmf_msg->im_msgbufs_send.im_bufs_cl_hdr = NULL;
		ibmf_msg->im_msgbufs_send.im_bufs_cl_hdr_len = 0;
		ibmf_msg->im_msgbufs_send.im_bufs_cl_data = NULL;
		ibmf_msg->im_msgbufs_send.im_bufs_cl_data_len = 0;
		kmem_free(umad_ctx, umad_ctx->send_len);
		umad_ctx = NULL;
	}

	if (rc)
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str, "umad_read:"
		    "uiomove() failed %d", rc);

	if (umad_msg->umad_ibmf_msg_cpy_sz) {
		kmem_free(ibmf_msg, umad_msg->umad_ibmf_msg_cpy_sz);
	} else {
		rc = ibmf_free_msg(agent->agent_reg->ibmf_reg_handle,
		    &ibmf_msg);
		if (rc != IBMF_SUCCESS) {
			SOL_OFS_DPRINTF_L2(sol_umad_dbg_str, "umad_read:"
			    " ibmf_free_msg failed %d", rc);
		}
	}
	kmem_free(umad_msg, sizeof (*umad_msg));
	kmem_free((void *)entry, sizeof (genlist_entry_t));
err1:
	return (rc);
}

/*
 * Function:
 *     umad_solicited_cb
 * Input:
 *	ibmf_handle     -  handle to ibmf
 *      msgp            -  The incoming SM MAD
 *      args            -  umad_port_info_t object that the MAD cam in on
 * Output:
 *	None
 * Returns:
 *      none
 * Called by:
 * Description:
 *      Callback function (ibmf_msg_cb_t) that is invoked when the
 *      ibmf receives a SM MAD for the given Port.
 *      This function copies the MAD into the port recv queue.
 */
static void
/* LINTED E_FUNC_ARG_UNUSED */
umad_solicited_cb(ibmf_handle_t ibmf_handle, ibmf_msg_t *msgp, void *args)
{
	struct umad_send	*umad_ctx = (struct umad_send *)args;
	umad_agent_t		*agent = umad_ctx->send_agent;
	int			rc;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*msgp))

	mutex_enter(&agent->agent_lock);
	agent->agent_outstanding_msgs--;
	ASSERT(agent->agent_outstanding_msgs >= 0);

	if (!(agent->agent_flags & UMAD_AGENT_UNREGISTERING)) {
		rc = umad_queue_mad_msg(agent, msgp, umad_ctx, 0);
		if (rc) {
			SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
			    "umad_solicited_cb: queue msg failed %d", rc);
			goto drop_exit;
		} else {
			goto exit;
		}
	}

drop_exit:
	msgp->im_msgbufs_send.im_bufs_mad_hdr = NULL;
	msgp->im_msgbufs_send.im_bufs_cl_hdr = NULL;
	msgp->im_msgbufs_send.im_bufs_cl_hdr_len = 0;
	msgp->im_msgbufs_send.im_bufs_cl_data = NULL;
	msgp->im_msgbufs_send.im_bufs_cl_data_len = 0;
	kmem_free(umad_ctx, umad_ctx->send_len);
	umad_ctx = NULL;

	rc = ibmf_free_msg(agent->agent_reg->ibmf_reg_handle, &msgp);

	if (rc != IBMF_SUCCESS)
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
		    "umad_solicited_cb: ibmf_free_msg failed %d", rc);

	if (agent->agent_flags & UMAD_AGENT_UNREGISTERING) {
		if (agent->agent_outstanding_msgs == 0)
			cv_signal(&agent->agent_cv);
	}
exit:
	mutex_exit(&agent->agent_lock);
}

/*
 * Function:
 *	umad_write
 * Input:
 *	dev		device
 *	uiop		User I/O pointer
 *	credp		Unused
 * Output:
 *	None
 * Returns:
 *	status
 * Called by:
 *	Device write framework
 * Description:
 *	Cannot write to ISSM device.  Allocate new umad_send structure
 *	and ibmf message and copy from user space into allocated message.
 *	Fill in required fields.  If this is a request make sure
 *	umad_solicited_cb() is passed.
 */
/* ARGSUSED1 */
static int
umad_write(dev_t dev, struct uio *uiop, cred_t *credp)
{
	int			rc, rc2;
	int			mad_offset, flags = 0;
	int			hdr_len;
	size_t			len = uiop->uio_resid;
	minor_t			minor;
	ibmf_retrans_t		mad_retrans;
	umad_info_t		*info;
	umad_port_info_t	*port;
	umad_uctx_t		*uctx;
	umad_agent_t		*agent;
	struct ib_user_mad	*user_mad;	/* incoming uMAD hdr */
	ibmf_msg_t		*ibmf_msg;	/* outbound MAD mesg */
	ib_mad_hdr_t		*ib_mad_hdr;	/* outbound MAD hdrs */
	struct umad_send 	*umad_ctx;
	boolean_t		need_callback;
	ib_pkey_t		pkey;
	ibmf_handle_t		ibmf_hdl;

	rc = priv_policy(credp, PRIV_SYS_NET_CONFIG, B_FALSE, EACCES, NULL);
	if (rc != 0)
		return (rc);

	info = ddi_get_soft_state(umad_statep, UMAD_INSTANCE);
	if (info == NULL) {
		rc = ENXIO;
		goto err1;
	}

	/* lookup the node and port #s */
	minor = getminor(dev);

	if (ISSM_MINOR(minor)) {
		rc = ENXIO;
		goto err1;
	}

	mutex_enter(&info->info_mutex);
	uctx = info->info_uctx[GET_UCTX(minor)];
	mutex_exit(&info->info_mutex);
	ASSERT(uctx != NULL);
	port = uctx->uctx_port;
	ASSERT(port != NULL);

	umad_ctx = kmem_zalloc(sizeof (struct umad_send) + len, KM_SLEEP);
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*umad_ctx))
	umad_ctx->send_len = sizeof (struct umad_send) + len;

	/* copy the MAD data in from user space */
	/* data = user_mad + mad_hdrs + class_hdrs + class data */
	/* LINTED */
	user_mad = (struct ib_user_mad *)umad_ctx->send_umad;
	rc = uiomove(user_mad, len, UIO_WRITE, uiop);
	if (rc != 0)
		goto err3;


	/* Look for the agent */
	mutex_enter(&uctx->uctx_lock);
	agent = umad_get_agent_by_id(uctx, user_mad->hdr.id);
	mutex_exit(&uctx->uctx_lock);
	if (! agent) {
		rc = EINVAL;
		goto err3;
	}

	mutex_enter(&agent->agent_lock);
	if (agent->agent_flags & UMAD_AGENT_UNREGISTERING) {
		mutex_exit(&agent->agent_lock);
		rc = EINVAL;
		goto err3;
	}

	/* Allocate the msg buf for IBMF */
	rc = ibmf_alloc_msg(agent->agent_reg->ibmf_reg_handle,
	    IBMF_ALLOC_NOSLEEP, &ibmf_msg);
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*ibmf_msg))
	if (rc != IBMF_SUCCESS) {
		mutex_exit(&agent->agent_lock);
		goto err3;
	}
	mutex_exit(&agent->agent_lock);

	ib_mad_hdr = (ib_mad_hdr_t *)user_mad->data;

	hdr_len = umad_get_mad_data_offset(ib_mad_hdr->MgmtClass);

	/*
	 * build the IBMF msg from the mad data passed in
	 * construct the addr info
	 */
#if defined(__FUTURE_FEATURE__)
	/* TODO Proper GRH handling (non-smp traffic only) */
	if (mad.addr.grh_present) {
		memcpy(&ibmf_msg->im_global_addr.ig_recver_gid, mad.addr.gid,
		    16);
		//  where can we get the GID??
		im_global_addr.ig_sender_gid = get_gid(umad->addr.gid_index);
		ibmf_msg->im_global_addr.ig_tclass = mad.addr.traffic_class;
		ibmf_msg->im_global_addr.ig_hop_limit = mad.addr.hop_limit;
		ibmf_msg->im_global_addr.ig_flow_label = mad.addr.flow_label;
	}
#endif

	/*
	 * Note: umad lid, qpn and qkey are in network order, so we need
	 * to revert them to give them to ibmf. See userspace
	 * umad_set_addr() and umad_set_addr_net().
	 */
	ibmf_msg->im_local_addr.ia_local_lid = port->port_lid;
	ibmf_msg->im_local_addr.ia_remote_lid = ntohs(user_mad->hdr.lid);
	ibmf_msg->im_local_addr.ia_remote_qno = ntohl(user_mad->hdr.qpn);
	ibmf_msg->im_local_addr.ia_q_key = ntohl(user_mad->hdr.qkey);
	ibmf_msg->im_local_addr.ia_service_level = user_mad->hdr.sl;

	rc = ib_index2pkey(port->port_hca->hca_ib_devp,
	    port->port_num, user_mad->hdr.pkey_index, &pkey);
	if (rc != IBT_SUCCESS) {
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
		    "umad_write: ibt_index2pkey failed %d",
		    rc);
		return (rc);
	}
	else
		ibmf_msg->im_local_addr.ia_p_key = ntohs(pkey);

	if ((ib_mad_hdr->R_Method & 0x80) == 0) {
		flags = IBMF_MSG_TRANS_FLAG_SEQ;
	}

	/*
	 * This code is only correct for the cases of
	 * no headers beyond the MAD header or the case of
	 * MAD_MGMT_CLASS_SUBN_ADM (SA type) which has both
	 * an RMPP header and an SA header.  Other header combinations
	 * are simply not dealt with correctly, but no applications
	 * utilize them either, so we should be ok.
	 */

	/* set use RMPP if UserAgent registered for it */
	mutex_enter(&agent->agent_lock);
	if (agent->agent_req.rmpp_version > 0) {
		ibmf_rmpp_hdr_t *rmpp_hdr;

		rmpp_hdr = (ibmf_rmpp_hdr_t *)(ib_mad_hdr + 1);

		if (rmpp_hdr->rmpp_flags != 0)
			flags |= IBMF_MSG_TRANS_FLAG_RMPP;
	}
	mutex_exit(&agent->agent_lock);

	/* construct the msg bufs */
	ibmf_msg->im_msgbufs_send.im_bufs_mad_hdr = ib_mad_hdr;

	hdr_len = umad_get_mad_clhdr_offset(ib_mad_hdr->MgmtClass);
	mad_offset = umad_get_mad_data_offset(ib_mad_hdr->MgmtClass);

	/* Class headers and len, rmpp? */
	ibmf_msg->im_msgbufs_send.im_bufs_cl_hdr =
	    (unsigned char *)user_mad +
	    offsetof(struct ib_user_mad, data) + hdr_len;
	ibmf_msg->im_msgbufs_send.im_bufs_cl_hdr_len =
	    mad_offset - hdr_len;

	ibmf_msg->im_msgbufs_send.im_bufs_cl_data =
	    (unsigned char *) user_mad + (sizeof (struct ib_user_mad) +
	    mad_offset);
	ibmf_msg->im_msgbufs_send.im_bufs_cl_data_len =
	    len - sizeof (struct ib_user_mad) - mad_offset;

	mad_retrans.retrans_retries = user_mad->hdr.retries;
	mad_retrans.retrans_rtv = user_mad->hdr.timeout_ms * 1000;
	mad_retrans.retrans_rttv = 0;
	mutex_enter(&agent->agent_lock);
	if (agent->agent_req.rmpp_version > 0) {
		mad_retrans.retrans_trans_to = 10;
	} else {
		mad_retrans.retrans_trans_to = 0;
	}
	umad_ctx->send_agent = agent;

	need_callback = (flags & IBMF_MSG_TRANS_FLAG_SEQ) != 0;

	if (need_callback)
		agent->agent_outstanding_msgs++;

	ibmf_hdl = agent->agent_reg->ibmf_reg_handle;
	mutex_exit(&agent->agent_lock);

	/* pass the MAD down to the IBMF layer */
	rc = ibmf_msg_transport(ibmf_hdl, IBMF_QP_HANDLE_DEFAULT,
	    ibmf_msg, &mad_retrans,
	    need_callback ? umad_solicited_cb : NULL,
	    umad_ctx, flags);

	if (!need_callback) {
		/*
		 * Free the IBMF msg. Log an err message if
		 * ibmf_free_msg() fails.
		 *
		 * if ibmf_msg_transport() failed, log err
		 * message and return EIO.
		 */
		rc2 = ibmf_free_msg(ibmf_hdl, &ibmf_msg);
		if (rc2 != IBMF_SUCCESS) {
			SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
			    "umad_write: ibmf_free_msg failed %d",
			    rc2);
		} else if (rc != IBMF_SUCCESS) {
			SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
			    "umad_write: ibmf_msg_transport failed %d",
			    rc);
			rc = EIO;
			goto err3;
		} else {
			kmem_free(umad_ctx, umad_ctx->send_len);
		}
	} else if (rc != IBMF_SUCCESS) {
		mutex_enter(&agent->agent_lock);
		agent->agent_outstanding_msgs--;
		ASSERT(agent->agent_outstanding_msgs >= 0);
		if (agent->agent_flags & UMAD_AGENT_UNREGISTERING) {
			if (agent->agent_outstanding_msgs == 0)
				cv_signal(&agent->agent_cv);
		}
		mutex_exit(&agent->agent_lock);

		rc2 = ibmf_free_msg(ibmf_hdl, &ibmf_msg);
		ASSERT(rc2 == IBMF_SUCCESS);

		rc = EIO;
		goto err3;
	}

	return (0);

err3:
	kmem_free(umad_ctx, umad_ctx->send_len);

err1:
	return (rc);
}

static void
umad_add_hca(struct ib_device *device)
{
	umad_info_t	*info;
	ibt_status_t	status;
	int		rc;
	struct ib_event_handler umad_async_evt_hdlr;
	struct ib_event_handler	*umad_async_evt_hdlrp;

	umad_async_evt_hdlrp = &umad_async_evt_hdlr;

	SOL_OFS_DPRINTF_L5(sol_umad_dbg_str,
	    "umad_add_hca(%p)", device);

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(
	    *umad_async_evt_hdlrp))
	INIT_IB_EVENT_HANDLER(umad_async_evt_hdlrp, device,
	    umad_async_event_handler);
	rc = ib_register_event_handler(umad_async_evt_hdlrp);
	if (rc != 0) {
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
		    "hca_open: ib_register_event_handler "
		    "failed %d", rc);
		return;
	}

	/*
	 * Get umad_info_t * from the soft state.
	 */
	info = ddi_get_soft_state(umad_statep, UMAD_INSTANCE);
	if (!info) {
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
		    "hca_open: get_soft_state failed");
		(void) ib_unregister_event_handler(umad_async_evt_hdlrp);
		return;
	}

	/* Only handle IB (no iWARP) devices */
	if (device->node_type != RDMA_NODE_IB_CA) {
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
		    "hca_open: Non IB device!!");
		(void) ib_unregister_event_handler(umad_async_evt_hdlrp);
		return;
	}

	status = umad_hca_attach(info, device);
	if (status == IBT_SUCCESS) {
		mutex_enter(&info->info_mutex);
		info->info_hca_count++;
		mutex_exit(&info->info_mutex);
	} else {
		SOL_OFS_DPRINTF_L3(sol_umad_dbg_str,
		    "umad_add_hca(): hca_attach failed");
		(void) ib_unregister_event_handler(umad_async_evt_hdlrp);
	}
}

static void
umad_remove_hca(struct ib_device *device)
{
	ibt_status_t	status;
	umad_info_t	*info;
	int		rc;
	struct ib_event_handler umad_async_evt_hdlr;
	struct ib_event_handler	*umad_async_evt_hdlrp;

	umad_async_evt_hdlrp = &umad_async_evt_hdlr;
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(
	    *umad_async_evt_hdlrp))

	/* Get umad_info_t * from soft_state */
	info = ddi_get_soft_state(umad_statep, UMAD_INSTANCE);
	if (!info)
		return;

	INIT_IB_EVENT_HANDLER(umad_async_evt_hdlrp, device,
	    umad_async_event_handler);
	rc = ib_unregister_event_handler(umad_async_evt_hdlrp);
	if (rc) {
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
		    "umad_remove_hca: ib_unregister failed");
		return;
	}

	status = umad_hca_detach(info, device);
	if (status != IBT_HCA_IN_USE) {
		mutex_enter(&info->info_mutex);
		info->info_hca_count--;
		mutex_exit(&info->info_mutex);
	} else {
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
		    "umad_remove_hca: failed "
		    "umad_hca_detach() error %d", status);
		(void) ib_register_event_handler(umad_async_evt_hdlrp);
	}
}

/*
 * Need this ioctl to enable the newer interface (pkey_index and some
 * reserved key).  Since OFED changed the abi without changing the abi
 * version.  This resulted in wo abi interfaces (with and without the
 * pkey_index and some reserved bytes, but one abi version number.  The
 * application then tries to do an ioctl() to enable the "newwer" interface
 * and it that ioctl succeeds, the application code assumes the newer abi
 * interface otherwise it assumes the older abi intrface (Uggggggg).
 */
static int
umad_pkey_enable()
{
	/* When we move to later releases of OFED, this will go away */
	return (DDI_SUCCESS);

}

/*
 * Function:
 *	umad_ioctl
 * Input:
 *	dev		device
 *	cmd		IB_USER_MAD_ENABLE_PKEY, IB_USER_MAD_REGISTER_AGENT or
 *			IB_USER_MAD_UNREGISTER_AGENT
 *	arg		which agent to register or unregister
 *	mode		passed on to ddi_copyin()
 *	credp		Unused
 *	rvalp		Unused
 * Output:
 *	None
 * Returns:
 *	Error status
 * Called by:
 *	Device ioctl framework
 * Description:
 *	IB_USER_MAD_ENABLE_PKEY just allows the ioctl to succed to
 *	indicate that we are at ABI version 5+, not really 5.
 *	IB_USER_MAD_REGISTER_AGENT requests that a specific MAD class
 *	for this device be handled by this process.
 *	IB_USER_MAD_UNREGISTER_AGENT undoes the request above.
 */
/* ARGSUSED3 */
static int
umad_ioctl(
	dev_t dev,
	int cmd,
	intptr_t arg,
	int mode,
	cred_t *credp,
	int *rvalp)
{
	int				rc = 0;
	int				minor;
	umad_info_t			*info;
	umad_port_info_t		*port;
	umad_uctx_t			*uctx;
	struct ib_user_mad_reg_req	req = {0};
	sol_umad_ioctl_info_t		umad_info, *infop;
	sol_umad_ioctl_port_info_t	*pip;
	umad_hca_info_t			*hca;
	int				port_cnt, cnt, bufsize, i, j;

	info = ddi_get_soft_state(umad_statep, UMAD_INSTANCE);
	if (info == NULL) {
		rc = ENXIO;
		goto err1;
	}

	/* lookup the node and port #s */
	minor = getminor(dev);

	/*
	 * If ISSM_MINOR needs to handle IB_USER_MAD_GET_ALL_PORT_INFO ioctl
	 * call, then the following check can be moved after the
	 * IB_USER_MAD_GET_ALL_PORT_INFO
	 */
	if (ISSM_MINOR(minor)) {
		rc = ENXIO;
		goto err1;
	}

	switch (cmd) {
	case IB_USER_MAD_ENABLE_PKEY:
		rc = umad_pkey_enable();
		break;

	case IB_USER_MAD_GET_PORT_INFO:
		rc = copyin((void*)arg, (void*)&umad_info,
		    sizeof (sol_umad_ioctl_info_t));
		if (rc != 0) {
			SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
			    "umad_ioctl: copyin (rc=%d)", rc);
			rc = EFAULT;
			goto err1;
		}

		port_cnt = umad_info.umad_port_cnt;
		bufsize = sizeof (sol_umad_ioctl_info_t) +
		    sizeof (sol_umad_ioctl_port_info_t) * port_cnt;
		infop = (sol_umad_ioctl_info_t *)kmem_zalloc(bufsize,
		    KM_NOSLEEP);
		if (infop == NULL) {
			SOL_OFS_DPRINTF_L2(sol_umad_dbg_str, "umad_ioctl: "
			    "kmem_zalloc failed");
			rc = ENOMEM;
			goto err1;
		}

		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*infop))
		infop->umad_abi_version = IB_USER_MAD_ABI_VERSION;
		infop->umad_solaris_abi_version =
		    IB_USER_MAD_SOLARIS_ABI_VERSION;
		pip = infop->umad_port_info;
		cnt = 0;

		mutex_enter(&info->info_mutex);
		for (i = 0; i < UMAD_MAX_HCAS; i++) {
			hca = info->info_hcas[i];
			if (hca == NULL || hca->hca_ports == NULL)
				continue;
			if (port_cnt == 1) {
				if ((minor >=
				    hca->drv_hca_port_idx) &&
				    (minor <= (hca->drv_hca_nports +
				    hca->drv_hca_port_idx))) {
					port = &(hca->hca_ports[minor -
					    hca->drv_hca_port_idx]);
					if (port->port_num) {
						umad_set_ioctl_portinfo(
						    pip, hca, port);
						cnt = 1;
						break;
					}
				} else
					continue;
			}
			for (j = 0; j < hca->drv_hca_nports; j++) {
				port = &(hca->hca_ports[j]);
				if (!port->port_num)
					continue;
				umad_set_ioctl_portinfo(pip, hca, port);
				pip++;
				cnt++;
				if (cnt == port_cnt)
					break;
			}
		}
		mutex_exit(&info->info_mutex);

		infop->umad_port_cnt = (int16_t)cnt;
		if (ddi_copyout((void *) infop, (void *) arg,
		    sizeof (sol_umad_ioctl_info_t) + (cnt *
		    sizeof (sol_umad_ioctl_port_info_t)), mode) != 0) {
			SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
			    "umad_ioctl: EFAULT - copyout failure");
			rc = EFAULT;
		}
		kmem_free(infop, bufsize);
		break;

	case IB_USER_MAD_REGISTER_AGENT:
	case IB_USER_MAD_UNREGISTER_AGENT:
		mutex_enter(&info->info_mutex);
		uctx = info->info_uctx[GET_UCTX(minor)];
		mutex_exit(&info->info_mutex);
		ASSERT(uctx != NULL);
		port = uctx->uctx_port;
		if (port == NULL) {
			SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
			    "umad_ioctl: port NULL");
			goto err1;
		}

		if (ddi_copyin((void *) arg, &req, sizeof (req), mode) != 0) {
			rc = EFAULT;
			goto err1;
		}

		if (cmd == IB_USER_MAD_REGISTER_AGENT) {
			rc = umad_register(&req, uctx);
			if (rc)
				goto err1;

			/* return agent ID to user */
			rc = ddi_copyout(&req, (void *) arg, sizeof (req),
			    mode);

			if (rc) {
				rc = umad_unregister(&req, uctx);
				if (rc != IBMF_SUCCESS)
					SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
					    "umad_ioctl: umad_unregister - "
					    "fail %d", rc);
				rc = EFAULT;
				goto err1;
			}
		} else /* IB_USER_MAD_UNREGISTER_AGENT */
			rc = umad_unregister(&req, uctx);
		break;

	default:
		rc = DDI_FAILURE;
	}


err1:
	return (rc);
}

static void
umad_set_ioctl_portinfo(sol_umad_ioctl_port_info_t *pip,
    umad_hca_info_t *hca, umad_port_info_t *port)
{
	pip->umad_port_num = port->port_num;
	pip->umad_port_idx = port->port_minor_idx;
	(void) strcpy(pip->umad_port_ibdev_name,
	    hca->drv_hca_ofusr_name);
}

/*
 * Get a new unique agent ID. The agent list is already locked. The
 * complexity is not ideal, but the number of agents should be small
 * (ie 2 or 3) so it shouldn't matter.
 */
static int
umad_get_new_agent_id(umad_uctx_t *uctx)
{
	boolean_t found;
	unsigned int agent_id;
	llist_head_t *entry;

	agent_id = 0;

	ASSERT(MUTEX_HELD(&uctx->uctx_lock));

	for (;;) {
		found = B_FALSE;
		list_for_each(entry, &uctx->uctx_agent_list) {
			umad_agent_t *agent = entry->ptr;

			if (agent_id == agent->agent_req.id) {
				found = B_TRUE;
				break;
			}
		}

		if (! found)
			break;

		agent_id++;
	}

	return (agent_id);
}

/*
 * Function:
 *	umad_register
 * Input:
 *	req 	User registration request
 *	uctx	User context
 * Output:
 *	None
 * Returns:
 *	status
 * Called by:
 *	umad_ioctl
 * Description:
 *      Handles the registration of user agents from userspace.
 *      Each call will result in the creation of a new agent object for
 *      the given HCA/port.  If UMAD_CA_MAX_AGENTS has been reached then an
 *      error is raised.
 */
static int
umad_register(struct ib_user_mad_reg_req *req, umad_uctx_t *uctx)
{
	int			rc = IBMF_SUCCESS;
	umad_agent_t		*agent = NULL;

	/* check for valid QP */
	if ((req->qpn != 0) && (req->qpn != 1)) {
		rc = EINVAL;
		goto err1;
	}

	agent = kmem_zalloc(sizeof (umad_agent_t), KM_SLEEP);
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*agent))
	mutex_init(&agent->agent_lock, NULL, MUTEX_DRIVER, NULL);
	cv_init(&agent->agent_cv, NULL, CV_DRIVER, NULL);

	agent->agent_req = *req;
	agent->agent_uctx = uctx;

	llist_head_init(&agent->agent_list, agent);

	mutex_enter(&uctx->uctx_lock);
	agent->agent_req.id = req->id = umad_get_new_agent_id(uctx);
	mutex_exit(&uctx->uctx_lock);

	rc = umad_register_agent(agent);
	if (rc)
		goto err1;

	mutex_enter(&uctx->uctx_lock);
	llist_add(&agent->agent_list, &uctx->uctx_agent_list);
	mutex_exit(&uctx->uctx_lock);

	return (0);

err1:
	if (rc) {
		if (agent) {
			cv_destroy(&agent->agent_cv);
			mutex_destroy(&agent->agent_lock);
			kmem_free(agent, sizeof (umad_agent_t));
		}
	}

	return (rc);
}

/*
 * Function:
 *	umad_unregister
 * Input:
 *	req		- user unregister request
 *	info		- user context
 * Output:
 *	None
 * Returns:
 *	Status
 * Called by:
 *	umad_ioct
 * Description:
 *	Undoes registration.  Waits for pending operations before completing.
 */
static int
umad_unregister(struct ib_user_mad_reg_req *req, umad_uctx_t *uctx)
{
	int			rc;
	int			agent_id = req->id;
	uint_t			last_agent = 0;
	boolean_t		did_ibmf_unregister;
	umad_agent_t		*agent;
	llist_head_t 		*lentry;
	struct ibmf_reg_info	*ibmf_info;
	umad_port_info_t	*port;

	mutex_enter(&uctx->uctx_lock);
	agent = umad_get_agent_by_id(uctx, agent_id);

	SOL_OFS_DPRINTF_L5(sol_umad_dbg_str,
	    "umad_unregister: Enter agent: %p", agent);

	if (agent == NULL) {
		mutex_exit(&uctx->uctx_lock);
		rc = EINVAL;
		goto done;
	}

	mutex_exit(&uctx->uctx_lock);
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*agent))

	mutex_enter(&agent->agent_lock);
	agent->agent_flags |= UMAD_AGENT_UNREGISTERING;
	while (agent->agent_outstanding_msgs != 0) {
		cv_wait(&agent->agent_cv, &agent->agent_lock);
	}
	mutex_exit(&agent->agent_lock);

	/*
	 * Remove agent from the uctx list.
	 * If this agent is handling asyncs, decrement the number
	 * of agents handling asyncs for this uctx, if this is the last
	 * then remove this uctx from the ibmf_reg_info uctx async list.
	 */
	mutex_enter(&uctx->uctx_lock);
	llist_del(&agent->agent_list);

	if (agent->agent_flags & UMAD_HANDLING_ASYNC) {
		uctx->uctx_async_ref--;
		if (uctx->uctx_async_ref == 0) {
			last_agent = 1;
		}
	}

	mutex_exit(&uctx->uctx_lock);

	/* Get the IBMF registration information */
	port = uctx->uctx_port;
	mutex_enter(&port->port_lock);

	ibmf_info = agent->agent_reg;
	mutex_enter(&ibmf_info->ibmf_reg_lock);

	if (last_agent) {
		list_for_each(lentry, &ibmf_info->ibmf_uctx_list) {
			if ((umad_uctx_t *)lentry->ptr == uctx) {
				llist_del(lentry);
				kmem_free(lentry, sizeof (llist_head_t));
				break;
			}
		}
	}

	/*
	 * Remove the pending received MADs associated with this agent.
	 */
	umad_flush_rcv_queue(uctx, agent, NULL);

	/*
	 * If no more references, tear down the ibmf registration
	 */
	if (--ibmf_info->ibmf_reg_refcnt == 0) {

		if (ibmf_info->ibmf_flags & UMAD_IBMF_ASYNC_REGISTERED) {

			mutex_exit(&ibmf_info->ibmf_reg_lock);

			/* Remove the callback */
			rc = ibmf_tear_down_async_cb(ibmf_info->ibmf_reg_handle,
			    IBMF_QP_HANDLE_DEFAULT, 0);

			if (rc) {
				SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
				    "umad_unregister: failed "
				    "ibmf_tear_down_async_cb() error %d", rc);
			}
			mutex_enter(&ibmf_info->ibmf_reg_lock);
		}

		/*
		 * Remove the pending received MADs associated with this
		 * IBMF registration.
		 */
		umad_flush_rcv_queue(uctx, NULL, ibmf_info);

		/*
		 * unregister from IBMF
		 */
		rc = ibmf_unregister(&ibmf_info->ibmf_reg_handle, 0);

		if (rc) {
			SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
			    "umad_unregister: failed "
			    "ibmf_unregister() error %d", rc);
		}
		did_ibmf_unregister = B_TRUE;
		mutex_exit(&ibmf_info->ibmf_reg_lock);
	} else {
		mutex_exit(&ibmf_info->ibmf_reg_lock);
		did_ibmf_unregister = B_FALSE;
	}

	if (did_ibmf_unregister) {
		struct ibmf_reg_info *ibmf_entry = NULL;

		list_for_each(lentry, &port->port_ibmf_regs) {
			ibmf_entry = lentry->ptr;

			if (ibmf_info == ibmf_entry) {
				break;
			}
		}
		llist_del(lentry);
		kmem_free(lentry, sizeof (*lentry));

		/* Release the registration memory */
		kmem_free(ibmf_info, sizeof (*ibmf_info));
	}
	mutex_exit(&port->port_lock);
	agent->agent_uctx = NULL;
	cv_destroy(&agent->agent_cv);
	mutex_destroy(&agent->agent_lock);
	kmem_free(agent, sizeof (*agent));

	rc = 0;
done:
	return (rc);
}

/*
 * Function:
 *      umad_poll
 * Input:
 *	dev             device
 *	events          which events
 *	anyyet          any events yet?
 * Output:
 *	reventsp        return of which events
 *	phpp            poll head pointer
 * Returns:
 *      return 0 for success, or the appropriate error number
 * Called by:
 *	Device poll framework
 * Description:
 *	Fails for ISSM device. POLLOUT is always true. POLLIN or POLLRDNORM
 *	is true if a message has been queued for the user context receive list.
 */
static int
umad_poll(
	dev_t dev,
	short events,
	int anyyet,
	short *reventsp,
	struct pollhead **phpp)
{
	int			rc = 0;
	int			minor;
	umad_uctx_t		*uctx;
	umad_info_t		*info;
	short			revent = 0;

	info = ddi_get_soft_state(umad_statep, UMAD_INSTANCE);
	if (info == NULL) {
		rc = ENXIO;
		goto err1;
	}

	/* lookup the node and port #s */
	minor = getminor(dev);

	if (ISSM_MINOR(minor)) {
		rc = ENXIO;
		goto err1;
	}

	mutex_enter(&info->info_mutex);
	uctx = info->info_uctx[GET_UCTX(minor)];
	mutex_exit(&info->info_mutex);
	ASSERT(uctx != NULL);

	if (uctx->uctx_port == NULL) {
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str, "umad_poll: port NULL");
		goto err1;
	}

	/*
	 * Always signal ready for POLLOUT / POLLWRNORM.
	 * Signal for POLLIN / POLLRDNORM whenever there is something in
	 * the receive list.
	 */
	if (events & POLLOUT) {
		revent = POLLOUT;
	} else if (events & (POLLIN | POLLRDNORM)) {
		mutex_enter(&uctx->uctx_recv_lock);
		if (! genlist_empty(&uctx->uctx_recv_list)) {
			revent |=  POLLIN | POLLRDNORM;
		}
		mutex_exit(&uctx->uctx_recv_lock);
	}

	if (revent == 0) {
		if (! anyyet)
			*phpp = &uctx->uctx_pollhead;
	}

	*reventsp = revent;
err1:

	return (rc);
}

/*
 * Function:
 *     umad_unsolicited_cb
 * Input:
 *	ibmf_handle     - handle to ibmf
 *      msgp            -  The incoming SM MAD
 *      args            -  umad_port_info_t object that the MAD came in on
 * Output:
 *	None
 * Returns:
 *      none
 * Called by:
 *	IBMF from below
 * Description:
 *      Callback function (ibmf_msg_cb_t) that is invoked when the
 *      ibmf receives a response MAD and passes it up if requested.
 *      The message is tossed if no one wants it or queued if requested.
 */
static void
umad_unsolicited_cb(ibmf_handle_t ibmf_handle, ibmf_msg_t *msgp, void *args)
{
	struct ibmf_reg_info	*ibmf_info = (struct ibmf_reg_info *)args;
	struct ibmf_reg_info	*agent_ibmf_info;
	struct umad_agent_s	*agent;
	ib_mad_hdr_t		*mad_hdr;
	int			rc;
	llist_head_t		*agent_entry, *uctx_entry;
	llist_head_t		*tmp, mad_class_agent_list;
	void			*umad_ibmf_msg_cpy;
	umad_uctx_t		*uctxt;
	size_t			size;

#if defined(__lint)
	ibmf_handle = 0;
#endif

	ASSERT(msgp->im_msgbufs_send.im_bufs_mad_hdr == NULL);
	ASSERT(msgp->im_msgbufs_send.im_bufs_cl_data == NULL);
	ASSERT(msgp->im_msgbufs_send.im_bufs_cl_data_len == 0);

	/* Apply the filters to this MAD. */
	mad_hdr = msgp->im_msgbufs_recv.im_bufs_mad_hdr;

	/*
	 * Make sure that there is at least one user context that was
	 * receiving the unsolicited  messages is still present.
	 */
	mutex_enter(&ibmf_info->ibmf_reg_lock);
	if (llist_empty(&ibmf_info->ibmf_uctx_list)) {
		goto exit;
	}

	list_for_each(uctx_entry, &ibmf_info->ibmf_uctx_list) {
		uctxt = (umad_uctx_t *)uctx_entry->ptr;

		/*
		 * Get the MAD agents registered under the class
		 * and queue the msg.
		 */
		llist_head_init(&mad_class_agent_list, NULL);
		mutex_enter(&uctxt->uctx_lock);

		umad_get_agents_by_class(uctxt,
		    mad_hdr->MgmtClass, &mad_class_agent_list);

		if (llist_empty(&mad_class_agent_list)) {
			mutex_exit(&uctxt->uctx_lock);
			continue;
		}

		list_for_each(agent_entry, &mad_class_agent_list) {
			agent = (struct umad_agent_s *)agent_entry->ptr;

			if (!(agent->agent_flags & UMAD_HANDLING_ASYNC) ||
			    (agent->agent_flags & UMAD_AGENT_UNREGISTERING))
				continue;

			agent_ibmf_info = agent->agent_reg;
			if (agent_ibmf_info != ibmf_info)
				continue;

			if (mad_hdr->ClassVersion !=
			    agent->agent_req.mgmt_class_version)
				continue;

			if (!is_supported_mad_method(
			    mad_hdr->R_Method & MAD_METHOD_MASK,
			    agent->agent_req.method_mask))
				continue;


			/*
			 * Make a copy of the ibmf msg, and queue it.
			 */
			umad_ibmf_msg_cpy = umad_copy_ibmf_msg(msgp, &size);
			if (umad_queue_mad_msg(agent, umad_ibmf_msg_cpy, NULL,
			    size)) {
				kmem_free(umad_ibmf_msg_cpy, size);
			}
			continue;
		}
		mutex_exit(&uctxt->uctx_lock);

		agent_entry = mad_class_agent_list.nxt;
		while (agent_entry != &mad_class_agent_list) {
			tmp = agent_entry->nxt;
			llist_del(agent_entry);
			kmem_free(agent_entry, sizeof (*agent_entry));
			agent_entry = tmp;
		}
	}

exit:
	rc = ibmf_free_msg(ibmf_info->ibmf_reg_handle, &msgp);
	mutex_exit(&ibmf_info->ibmf_reg_lock);
	ASSERT(rc == IBMF_SUCCESS);
	if (rc != IBMF_SUCCESS)
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
		    "umad_unsolicited_cb: ibmf_free_msg fail %d", rc);
}

static void
umad_async_event_handler(struct ib_event_handler *hdlr,
    struct ib_event *ib_eventp)
{
	SOL_OFS_DPRINTF_L5(sol_umad_dbg_str, "umad_async_event(%p, %p)",
	    hdlr, ib_eventp);

#ifdef DEBUG

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*hdlr))
	if (hdlr->handler != umad_async_event_handler) {
		SOL_OFS_DPRINTF_L2(sol_umad_dbg_str,
		    "umad_async_event: invalid handler!!");
		return;
	}
#endif
}

#if defined(__lint)
/*
 * This is needed because rdma/ib_verbs.h and sol_ofs/sol_ofs_common.h
 * both implement static functions.  Not all of those functions are
 * used by sol_umad, but lint doesn't like seeing static function that
 * are defined but not used.
 */
void
lint_function(llist_head_t *a, llist_head_t *b)
{
	(void) llist_is_last(a, b);
	llist_add_tail(a, b);
	(void) ib_width_enum_to_int(IB_WIDTH_1X);
}
#endif
