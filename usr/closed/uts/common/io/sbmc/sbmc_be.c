/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */

/*
 * The sbmc interface is used to send command request messages to,
 * and receive command response messages from, the Service Processor (SP).
 *
 * Messages are transferred to the /dev/ipmi0 driver via the
 * Layered Driver Interface.
 *
 */

/*
 * IPMI: back-end to BMC access
 */

#include <sys/types.h>
#include <sys/stropts.h>
#include <sys/note.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/devops.h>
#include <sys/dditypes.h>
#include <sys/stream.h>
#include <sys/modctl.h>
#include <sys/varargs.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/conf.h>
#include <sys/sysmacros.h>
#include <sys/bmc_intf.h>
#include <sys/uio.h>
#include <sys/sunldi.h>
#include <sys/file.h>
#include <sys/atomic.h>
#include <io/ipmi/ipmi.h>

#include "sbmc_fe.h"

/*
 * IPMI module path
 */
#define	IPMI_MODULE \
	"/devices/pseudo/ipmi@0:ipmi0"
#define	VLDC_MTU	0x400

/*
 * IPMI MAX SIZE parameter
 */
#define	MAX_IPMI_RECV_SIZE	300

/*
 * IPMI LDI parameters
 */
static ldi_handle_t	sbmc_lh;
static ldi_ident_t	sbmc_li;

#define	VC_MAGIC_NUM	0x4B59554E

#define	TIMEDOUT(dev) ((dev)->timedout)
#define	BMC_DISCARD_TIMEOUT (1 * 1000 * 1000)	/* 1 second */

/*
 * Default Data limits for the virtual channel interface
 */
#define	BMC_VC_MAX_REQUEST_SIZE		263 /* 263 to allow the payload */
					    /* to be the full 255 bytes */
#define	VC_SEND_NONDATA_SIZE		8
#define	VC_SEND_MAX_PAYLOAD_SIZE	(BMC_VC_MAX_REQUEST_SIZE \
					    - VC_SEND_NONDATA_SIZE)
#define	BMC_VC_MAX_RESPONSE_SIZE	266 /* Max-out receive payload */
#define	VC_RECV_NONDATA_SIZE		11
#define	VC_RECV_MAX_PAYLOAD_SIZE	(BMC_VC_MAX_RESPONSE_SIZE \
					    - VC_RECV_NONDATA_SIZE)

/*
 * Some interfaces can handle a massive amount of request data, e.g.
 * when they're used to transfer flash images to the system service
 * processor.
 *
 * To enable faster bulk-data transfer on such BMCs, allow the max send
 * payload to be dynamically tunable.
 */
int sbmc_max_send_payload = VC_SEND_MAX_PAYLOAD_SIZE;

#ifdef DEBUG1
#define	DUMPLEN 300
static char sbmc_bufit[DUMPLEN];

void
sbmc_dump(uint8_t *data, uint_t dlen)
{
	char *bt;
	int  indx, len;

	if (dlen) {
		bzero(sbmc_bufit, DUMPLEN);
		bt = sbmc_bufit;
		len = 0;
		for (indx = 0; indx < dlen; indx++) {
			len += snprintf(bt + len, (DUMPLEN - 1) - len,
			    " %02x", data[indx]);
		}
		cmn_err(CE_NOTE, "%s", sbmc_bufit);
	}
}
#endif

int
sbmc_init(dev_info_t *dip)
{
	int ret = BMC_SUCCESS;

	if (ldi_ident_from_dip(dip, &sbmc_li) != 0)
		ret = BMC_FAILURE;

	return (ret);
}

void
sbmc_uninit()
{
	ldi_ident_release(sbmc_li);
	sbmc_li = NULL;
}

static void
sbmc_timer_handler(void *devp)
{
	ipmi_dev_t *dev = (ipmi_dev_t *)devp;

	dev->timedout = B_TRUE;
}

static int
sbmc_write(ipmi_state_t *ipmip, struct ipmi_req *send_buf)
{
	int		error;
	int		ret = BMC_FAILURE;
	bmc_kstat_t	*ksp = &ipmip->bmc_kstats;

	error = ldi_ioctl(sbmc_lh, IPMICTL_SEND_COMMAND, (intptr_t)send_buf,
			FKIOCTL, kcred, NULL);

	if (!error) {
		atomic_add_64(&ksp->bmc_bytes_out.value.ui64,
		    send_buf->msg.data_len);
		ret = BMC_SUCCESS;
	} else {
		sbmc_printf(BMC_DEBUG_LEVEL_3, "ldi_ioctl ret %d", error);
	}

	return (ret);
}

static int
sbmc_read(ipmi_state_t *ipmip, struct ipmi_recv **recv_ipmi,
		boolean_t *aborted)
{
	int		error;
	int		ret = BMC_FAILURE;
	int		anyyet = 0;
	short		reventsp;
	struct pollhead	*php;
	ipmi_dev_t	*devp = &ipmip->ipmi_dev_ext;
	bmc_kstat_t	*ksp = &ipmip->bmc_kstats;

	/*
	 * these need to do the waterfall of frees in specific order
	 * like in construct
	 */
	*recv_ipmi = kmem_zalloc(sizeof (struct ipmi_recv), KM_NOSLEEP);
	if (*recv_ipmi == NULL)
		return (BMC_FAILURE);

	(*recv_ipmi)->addr_len = sizeof (struct ipmi_addr);
	(*recv_ipmi)->addr = kmem_zalloc(sizeof (struct ipmi_addr), KM_NOSLEEP);
	if ((*recv_ipmi)->addr == NULL) {
		kmem_free(*recv_ipmi, sizeof (struct ipmi_recv));
		return (BMC_FAILURE);
	}

	(*recv_ipmi)->msg.data_len = MAX_IPMI_RECV_SIZE;
	(*recv_ipmi)->msg.data = kmem_zalloc(MAX_IPMI_RECV_SIZE, KM_NOSLEEP);
	if ((*recv_ipmi)->msg.data == NULL) {
		kmem_free((*recv_ipmi)->addr, sizeof (struct ipmi_recv));
		kmem_free(*recv_ipmi, sizeof (struct ipmi_recv));
		return (BMC_FAILURE);
	}

	while (!TIMEDOUT(devp)) {
		if (*aborted == B_TRUE) {
			sbmc_printf(BMC_DEBUG_LEVEL_3, "VLDC read task abort");
			break;
		}
		if ((error = ldi_poll(sbmc_lh, (POLLIN | POLLPRI), anyyet,
		    &reventsp, &php))
		    != 0) {
			sbmc_printf(BMC_DEBUG_LEVEL_3, "ldi_poll ret %d",
			    error);
			continue;
		}
		if (!(reventsp & (POLLIN | POLLPRI))) {
			delay(drv_usectohz(100));
			continue;
		}
		error = ldi_ioctl(sbmc_lh, IPMICTL_RECEIVE_MSG,
		    (intptr_t) *recv_ipmi, FKIOCTL, kcred, NULL);
		if (!error) {
			atomic_add_64(&ksp->bmc_bytes_in.value.ui64,
			    (*recv_ipmi)->msg.data_len);
			ret = BMC_SUCCESS;
			break;
		} else {
			sbmc_printf(BMC_DEBUG_LEVEL_3, "ldi_read ret %d",
			    error);
			break;
		}
	}

	return (ret);
}

/*
 * Allocates an ipmi_req with enough space in the data member to
 * fit `datalen' bytes and initializes it with the supplied values.
 */
static struct ipmi_req *
sbmc_construct_send_struct(uint8_t netfn, uint8_t lun, uint8_t cmd, int datalen,
    uint8_t *datap)
{
	struct ipmi_req *ipmi_reqp;
	struct ipmi_system_interface_addr *sbmc_addrp;

	/* here's the happy waterfall of free's */
	ipmi_reqp = kmem_zalloc(sizeof (struct ipmi_req), KM_NOSLEEP);
	if (ipmi_reqp == NULL)
		return (NULL);

	ipmi_reqp->addr_len = sizeof (struct ipmi_addr);
	ipmi_reqp->addr = kmem_zalloc(ipmi_reqp->addr_len, KM_NOSLEEP);
	if (ipmi_reqp->addr == NULL) {
		kmem_free(ipmi_reqp, sizeof (struct ipmi_req));
		return (NULL);
	}

	if (datalen != 0) {
		ipmi_reqp->msg.data_len = (uint16_t)datalen;
		ipmi_reqp->msg.data = kmem_zalloc(datalen, KM_NOSLEEP);
		if (ipmi_reqp->msg.data == NULL) {
			kmem_free(ipmi_reqp->addr, sizeof (struct ipmi_addr));
			kmem_free(ipmi_reqp, sizeof (struct ipmi_req));
			return (NULL);
		}
	}

	ipmi_reqp->msg.netfn = netfn;
	ipmi_reqp->msg.cmd = cmd;

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	sbmc_addrp = (struct ipmi_system_interface_addr *) ipmi_reqp->addr;
	sbmc_addrp->lun = lun;
	sbmc_addrp->addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
	sbmc_addrp->channel = IPMI_BMC_CHANNEL;

	if (datalen)
		bcopy(datap, ipmi_reqp->msg.data, datalen);

	return (ipmi_reqp);
}

/*
 * Deallocates the resources associated with the send structure `ipmi_reqp'
 * of size `datalen'.
 */
static void
sbmc_destruct_send_struct(struct ipmi_req *ipmi_reqp, int datalen)
{
	if (ipmi_reqp == NULL)
		return;
	if (ipmi_reqp->msg.data != NULL) {
		kmem_free(ipmi_reqp->msg.data, datalen);
	}
	if (ipmi_reqp->addr != NULL) {
		kmem_free(ipmi_reqp->addr, sizeof (struct ipmi_addr));
	}
	kmem_free(ipmi_reqp, sizeof (struct ipmi_req));
}

/*
 * Deallocates the resources associated with the recv structure `ipmi_recv'
 */
static void
sbmc_destruct_recv_struct(struct ipmi_recv *recv_bmc)
{
	if (recv_bmc == NULL)
		return;
	if (recv_bmc->addr != NULL) {
		kmem_free(recv_bmc->addr, sizeof (struct ipmi_addr));
	}
	if (recv_bmc->msg.data != NULL) {
		kmem_free(recv_bmc->msg.data, MAX_IPMI_RECV_SIZE);
	}
	kmem_free(recv_bmc, sizeof (struct ipmi_recv));
}

/*
 * The only time this is not interruptable is during attach
 */
int
do_bmc2ipmi(ipmi_state_t *ipmip, bmc_req_t *send_pkt, bmc_rsp_t *recv_pkt,
    boolean_t interruptable, uint8_t *errp)
{
	struct ipmi_req		*send_bmc;
	struct ipmi_recv	*recv_bmc = NULL;
	clock_t			ticks;
	uint_t			retrycount = 5;
	uint32_t		rpktsz;
	int			ret = BMC_FAILURE;
	ipmi_dev_t		*dev = &ipmip->ipmi_dev_ext;
	boolean_t		*aborted = &ipmip->task_abort;

	if (send_pkt->datalength > sbmc_max_send_payload) {
		sbmc_printf(BMC_DEBUG_LEVEL_3, "failed send sz: %d",
		    send_pkt->datalength);
		recv_pkt->ccode = BMC_IPMI_DATA_LENGTH_EXCEED;
		recv_pkt->datalength = 0;
		return (ret);
	}

	send_bmc = sbmc_construct_send_struct(send_pkt->fn, send_pkt->lun,
	    send_pkt->cmd, send_pkt->datalength, send_pkt->data);

	if (send_bmc == NULL) {
		*errp = ENOMEM;
		return (BMC_FAILURE);
	}

	mutex_enter(&dev->if_mutex);
	while (dev->if_busy) {

		if (*aborted == B_TRUE) {
			mutex_exit(&dev->if_mutex);
			sbmc_printf(BMC_DEBUG_LEVEL_3,
			    "do_bmc2ipmi task aborted");
			recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
			recv_pkt->datalength = 0;
			sbmc_destruct_send_struct(send_bmc,
			    send_pkt->datalength);
			send_bmc = NULL;
			return (BMC_FAILURE);
		}

		if (cv_wait_sig(&dev->if_cv, &dev->if_mutex) == 0 &&
		    interruptable) {
			mutex_exit(&dev->if_mutex);
			sbmc_destruct_send_struct(send_bmc,
			    send_pkt->datalength);
			send_bmc = NULL;
			return (BMC_FAILURE);
		}
	}

	dev->if_busy = B_TRUE;
	mutex_exit(&dev->if_mutex);

	ticks = drv_usectohz(DEFAULT_MSG_TIMEOUT);

	dev->timedout = B_FALSE;
	dev->timer_handle = realtime_timeout(sbmc_timer_handler, dev, ticks);

	sbmc_printf(BMC_DEBUG_LEVEL_4,
	    "fn 0x%x lun 0x%x cmd 0x%x fnlun 0x%x len 0x%x",
	    send_pkt->fn, send_pkt->lun,
	    send_pkt->cmd, send_bmc->msg.cmd,
	    send_pkt->datalength);

	if (*aborted == B_TRUE) {
		sbmc_printf(BMC_DEBUG_LEVEL_3, "do_bmc2ipmi open aborted");
		recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
		recv_pkt->datalength = 0;
		goto error_open;
	}

	/* Try to open the ipmi module. */
	if (ldi_open_by_name(IPMI_MODULE, (FREAD | FWRITE),
	    kcred, &sbmc_lh, sbmc_li) != 0) {
		sbmc_printf(BMC_DEBUG_LEVEL_3, "ldi_open failed: %s",
		    IPMI_MODULE);
		recv_pkt->ccode = BMC_IPMI_OEM_FAILURE_SENDBMC;
		goto error_open;
	}

#ifdef DEBUG1
	cmn_err(CE_NOTE, "REQ send_pkt %p", send_pkt);
	cmn_err(CE_NOTE, " cmd %x fn %x lun %x datlen %x",
	    send_pkt->cmd, send_pkt->fn, send_pkt->lun, send_pkt->datalength);
	sbmc_dump(&send_pkt->data[0], send_pkt->datalength);
#endif

	while (retrycount-- != 0) {

		if (*aborted == B_TRUE) {
			sbmc_printf(BMC_DEBUG_LEVEL_3, "do_bmc2ipmi aborted");
			recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
			recv_pkt->datalength = 0;
			break;
		}

		if (TIMEDOUT(dev)) {
			sbmc_printf(BMC_DEBUG_LEVEL_3, "do_bmc2ipmi timed out");
			break;
		}

		if (sbmc_write(ipmip, send_bmc) == BMC_FAILURE) {
			sbmc_printf(BMC_DEBUG_LEVEL_3, "send BMC failed");
			recv_pkt->ccode = BMC_IPMI_OEM_FAILURE_SENDBMC;
			continue;
		}

		if (sbmc_read(ipmip, &recv_bmc, aborted) == BMC_FAILURE) {
			sbmc_printf(BMC_DEBUG_LEVEL_3, "recv BMC failed");
			recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
			continue;
		}

		break;
	}

	if (recv_bmc) {
		/*
		 * Subtract the size of non-data fields from the receive packet
		 * size to get the amount of data in the data field.
		 */
		rpktsz = recv_bmc->msg.data_len - 1;

		recv_pkt->fn = GET_NETFN(recv_bmc->msg.netfn);
		recv_pkt->lun = GET_LUN(recv_bmc->msg.netfn);
		recv_pkt->cmd = recv_bmc->msg.cmd;

		/*
		 * If the caller didn't provide enough data space to hold
		 * the response, return failure.
		 */
		if (rpktsz > recv_pkt->datalength) {
			sbmc_printf(BMC_DEBUG_LEVEL_3, "failed recv sz: %d",
				    recv_pkt->datalength);
			recv_pkt->ccode = BMC_IPMI_DATA_LENGTH_EXCEED;
			recv_pkt->datalength = 0;
		} else {
			recv_pkt->ccode = *recv_bmc->msg.data;
			recv_pkt->datalength = (uint8_t)rpktsz;
			bcopy(&recv_bmc->msg.data[1], &recv_pkt->data[0],
			    rpktsz);
			ret = BMC_SUCCESS;
		}
		sbmc_destruct_recv_struct(recv_bmc);
		recv_bmc = NULL;
#ifdef DEBUG1
		cmn_err(CE_NOTE, "RSP recv_pkt %p", recv_pkt);
		cmn_err(CE_NOTE, " cmd %x fn %x lun %x datlen %x ccod %x",
			recv_pkt->cmd, recv_pkt->fn, recv_pkt->lun,
			recv_pkt->datalength, recv_pkt->ccode);
		sbmc_dump(&recv_pkt->data[0], recv_pkt->datalength);
#endif
	}

	(void) ldi_close(sbmc_lh, NULL, kcred);

error_open:
	(void) untimeout(dev->timer_handle);
	dev->timer_handle = 0;

	mutex_enter(&dev->if_mutex);
	ASSERT(dev->if_busy == B_TRUE);
	dev->if_busy = B_FALSE;
	cv_signal(&dev->if_cv);
	mutex_exit(&dev->if_mutex);

	sbmc_destruct_send_struct(send_bmc, send_pkt->datalength);
	send_bmc = NULL;

	return (ret);
}

/*
 * Returns the size of the largest possible response payload.
 */
int
sbmc_vc_max_response_payload_size(void)
{
	return (VC_RECV_MAX_PAYLOAD_SIZE);
}

/*
 * Returns the size of the largest possible request payload.
 */
int
sbmc_vc_max_request_payload_size(void)
{
	return (VC_SEND_MAX_PAYLOAD_SIZE);
}
