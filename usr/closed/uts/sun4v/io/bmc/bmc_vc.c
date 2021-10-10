/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
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
#include <sys/vldc.h>
#include <sys/ldc.h>
#include <sys/sunldi.h>
#include <sys/file.h>
#include <sys/atomic.h>

#include "bmc_fe.h"


/*
 * The VLDC (Virtual Logical Domain Channel) interface is used to
 * send command request messages to, and receive command response
 * messages from, the Service Processor (SP).
 *
 * Messages are transferred over the interface using Layered Driver
 * Interface over the vldc driver.
 *
 *  -------------------                 .
 *  | ipmitool/others |                 .
 *  -------------------                 .
 *	    |                           .
 *	    |                           .
 *	============                    .     ------------------
 *	# /dev/bmc #                    .     | IPMI ILOM task |
 *	============                    .     ------------------
 *	    |                           .             |
 *	    |                           .             |
 *	--------    virtual channel     .         --------
 *	| vldc |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~| vBSC |
 *	--------                        .         --------
 *					.
 *	   H  O  S  T                   .            S  P
 *
 */

/*
 * IPMI virtual channel name
 */
#define	IPMI_VLDC \
	"/devices/virtual-devices@100/channel-devices@200" \
	"/virtual-channel@3:ipmi"
#define	VLDC_MTU	0x400

/*
 * Virtual channel LDI parameters
 */
static ldi_handle_t	vc_lh;
static boolean_t	vc_opened = B_FALSE;
static ldi_ident_t	vc_li;

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
 * Tunables
 */

/*
 * Some interfaces can handle a massive amount of request data, e.g.
 * when they're used to transfer flash images to the system service processor.
 * To enable faster bulk-data transfer on such BMCs, allow the max send payload
 * to be dynamically tunable.
 */
int vc_max_send_payload = VC_SEND_MAX_PAYLOAD_SIZE;

/*
 * Private Data Structures
 */

/*
 * data structure to send a message to BMC.
 */
typedef struct bmc_vc_send {
	uint32_t magic_num;	/* magic number */
	uint16_t datalen;	/* data length */
	uint8_t  fnlun;		/* Network Function and LUN */
	uint8_t  cmd;		/* command */
	uint8_t  data[1];	/* Variable-length, see vc_max_send_payload */
} bmc_vc_send_t;

/*
 * data structure to receive a message from BMC.
 */
typedef struct bmc_vc_recv {
	uint32_t magic_num;	/* magic number */
	uint16_t datalen;	/* data length */
	uint16_t reserved;	/* reserved */
	uint8_t  fnlun;		/* Network Function and LUN */
	uint8_t  cmd;		/* command */
	uint8_t  ccode;		/* completion code */
	uint8_t  data[VC_RECV_MAX_PAYLOAD_SIZE];
} bmc_vc_recv_t;


int
vc_init(dev_info_t *dip)
{
	int ret = BMC_SUCCESS;

	if (ldi_ident_from_dip(dip, &vc_li) != 0)
		ret = BMC_FAILURE;

	return (ret);
}

void
vc_uninit()
{
	ldi_ident_release(vc_li);
	vc_li = NULL;
}

void
dump_raw_pkt(uint8_t *buf, uint_t sz)
{
	uint_t i;

	for (i = 0; i < sz; i++) {
		dprintf(BMC_DEBUG_LEVEL_3,
		    "%d => 0x%x", i, buf[i]);
	}
}

static void
vc_timer_handler(void *devp)
{
	ipmi_dev_t *dev = (ipmi_dev_t *)devp;

	dev->timedout = B_TRUE;
}

/*
 * If the service processor takes a reset, the connection to the vldc channel
 * could be lost and vldc_chpoll() might always return ENOTACTIVE or ECONNRESET.
 * This may be fixed in vldc eventually, but it needs, for now, to provide
 * workaround by closing and reopening the channel.  Refer to CR 6629230.
 *
 * In vc_read() and vc_write(), if ldi_read() or ldi_write() returns ENOTACTIVE
 * or ECONNRESET, it will be eventually caught by the call to ldi_poll(); and
 * this function will be called at that time.
 *
 * Called with if_busy = B_TRUE.
 */
/* ARGSUSED */
static int
vc_reopen_vldc(ipmi_dev_t *devp)
{
	vldc_opt_op_t channel_op;

	ASSERT(devp->if_busy == B_TRUE);

	if (vc_opened == B_TRUE) {
		(void) ldi_close(vc_lh, NULL, kcred);
		vc_opened = B_FALSE;
	}

	delay(drv_usectohz(50000));

	if (ldi_open_by_name(IPMI_VLDC, (FREAD | FWRITE | FEXCL),
	    kcred, &vc_lh, vc_li) != 0) {
		dprintf(BMC_DEBUG_LEVEL_4,
		    "reconnect failed at ldi_open_by_name");
		return (BMC_FAILURE);
	} else {
		vc_opened = B_TRUE;
	}

	channel_op.op_sel = VLDC_OP_SET;
	channel_op.opt_sel = VLDC_OPT_MODE;
	channel_op.opt_val = LDC_MODE_RELIABLE;

	if (ldi_ioctl(vc_lh, VLDC_IOCTL_OPT_OP,
	    (intptr_t)&channel_op, FKIOCTL, kcred, NULL) != 0) {
		dprintf(BMC_DEBUG_LEVEL_4, "reconnect failed at ldi_ioctl");
		(void) ldi_close(vc_lh, NULL, kcred);
		vc_opened = B_FALSE;
		return (BMC_FAILURE);
	}

	dprintf(BMC_DEBUG_LEVEL_4,
	    "reestablished connection with %s", IPMI_VLDC);

	return (BMC_SUCCESS);
}

static int
vc_write(ipmi_state_t *ipmip, uint8_t *send_buf, uint32_t pktsz,
    boolean_t *aborted)
{
	uint8_t		*p;
	struct uio	uio;
	struct iovec	iov;
	int		chunksize;
	int		error;
	int		ret = BMC_FAILURE;
	int		anyyet = 0;
	short		reventsp;
	struct pollhead	*php;
	int		notready_count = 0;
	ipmi_dev_t	*devp = &ipmip->ipmi_dev_ext;
	bmc_kstat_t	*ksp = &ipmip->bmc_kstats;

	dprintf(BMC_DEBUG_LEVEL_3, "VLDC write Len 0x%x [[START]]", pktsz);

	p = (uint8_t *)send_buf;
	while (vc_opened == B_TRUE && p < (uint8_t *)&send_buf[pktsz] &&
	    !TIMEDOUT(devp)) {

		if (*aborted == B_TRUE) {
			dprintf(BMC_DEBUG_LEVEL_3, "VLDC write task abort");
			break;
		}

		if ((error = ldi_poll(vc_lh, POLLOUT,
		    anyyet, &reventsp, &php)) != 0) {
			dprintf(BMC_DEBUG_LEVEL_3, "ldi_poll ret %d", error);
			if (error == ENOTACTIVE || error == ECONNRESET) {
				if (vc_reopen_vldc(devp) != BMC_SUCCESS)
					break;
			}
			continue;
		}

		if (!(reventsp & POLLOUT)) {

			/*
			 * We don't always expect the virtual channel to be
			 * reliable and responsive.  For example, what if the
			 * channel unexpectedly resets and we end up polling
			 * forever for some reason?  This kind of problem has
			 * been observed particulary with poll for write.  We
			 * should be suspicious of excessive number of polls,
			 * and should take a recovery measure.
			 */
			if (++notready_count % 50 == 0) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "excessive poll retries.");
				if (vc_reopen_vldc(devp) != BMC_SUCCESS)
					break;
			}

			delay(drv_usectohz(100));
			continue;
		}

		chunksize = (uintptr_t)&send_buf[pktsz] - (uintptr_t)p;

		if (chunksize > VLDC_MTU) {
			chunksize = VLDC_MTU;
		}

		/*
		 * Write to VLDC in MTU chunks at a time.
		 */
		bzero(&uio, sizeof (uio));
		bzero(&iov, sizeof (iov));
		iov.iov_base = (char *)p;
		iov.iov_len = chunksize;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_loffset = 0;
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_resid = chunksize;

		error = ldi_write(vc_lh, &uio, kcred);

		if (error == EAGAIN) continue;

		if (!error) {
			p = (uint8_t *)iov.iov_base;
			atomic_add_64(&ksp->bmc_bytes_out.value.ui64,
			    chunksize);
		} else {
			dprintf(BMC_DEBUG_LEVEL_3, "ldi_write ret %d", error);
			break;
		}
	}

	/* vc_opened might have been set to B_FALSE in failed vc_reopen_vldc */
	if (!error && vc_opened == B_TRUE)
		ret = BMC_SUCCESS;

	dprintf(BMC_DEBUG_LEVEL_3, "VLDC write [[END]]");

	return (ret);
}

static int
vc_read(ipmi_state_t *ipmip, uint8_t *recv_buf, uint32_t *pktsz,
    boolean_t *aborted)
{
	struct uio	uio;
	struct iovec	iov;
	int		error;
	int		ret = BMC_FAILURE;
	int		anyyet = 0;
	short		reventsp;
	struct pollhead	*php;
	ipmi_dev_t	*devp = &ipmip->ipmi_dev_ext;
	bmc_kstat_t	*ksp = &ipmip->bmc_kstats;

	dprintf(BMC_DEBUG_LEVEL_3, "VLDC read [[START]]");

	*pktsz = 0;

	while (vc_opened == B_TRUE && !TIMEDOUT(devp)) {

		if (*aborted == B_TRUE) {
			dprintf(BMC_DEBUG_LEVEL_3, "VLDC read task abort");
			break;
		}

		if ((error = ldi_poll(vc_lh, (POLLIN | POLLPRI),
		    anyyet, &reventsp, &php)) != 0) {
			dprintf(BMC_DEBUG_LEVEL_3, "ldi_poll ret %d", error);
			if (error == ENOTACTIVE || error == ECONNRESET) {
				if (vc_reopen_vldc(devp) != BMC_SUCCESS)
					break;
			}
			continue;
		}

		if (!(reventsp & (POLLIN | POLLPRI))) {
			delay(drv_usectohz(100));
			continue;
		}

		/*
		 * Read bytes from VLDC
		 */
		bzero(&uio, sizeof (uio));
		bzero(&iov, sizeof (iov));
		iov.iov_base = (char *)recv_buf;
		iov.iov_len = BMC_VC_MAX_RESPONSE_SIZE;
		uio.uio_iov = &iov;
		uio.uio_iovcnt = 1;
		uio.uio_loffset = 0;
		uio.uio_segflg = UIO_SYSSPACE;
		uio.uio_resid = BMC_VC_MAX_RESPONSE_SIZE;

		error = ldi_read(vc_lh, &uio, kcred);

		if (error == EAGAIN) continue;

		if (!error) {
			*pktsz = BMC_VC_MAX_RESPONSE_SIZE - uio.uio_resid;

			/* Check datalen and magic_num. */
			if (((bmc_vc_recv_t *)(void *)recv_buf)->datalen +
			    VC_RECV_NONDATA_SIZE != *pktsz ||
			    ((bmc_vc_recv_t *)(void *)recv_buf)->magic_num
			    != VC_MAGIC_NUM) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "garbage was received");
				break;
			}

			atomic_add_64(&ksp->bmc_bytes_in.value.ui64, *pktsz);

			ret = BMC_SUCCESS;
			break;
		} else {
			dprintf(BMC_DEBUG_LEVEL_3, "ldi_read ret %d", error);
			break;
		}
	}

#ifdef	DEBUG
	dump_raw_pkt(recv_buf, *pktsz);
#endif
	dprintf(BMC_DEBUG_LEVEL_3, "VLDC read Len 0x%x [[END]]", *pktsz);

	return (ret);
}

/*
 * Allocates a bmc_vc_send_t with enough space in the data member to
 * fit `datalen' bytes and initializes it with the supplied values.
 */
static bmc_vc_send_t *
vc_construct_send_struct(uint8_t netfn, uint8_t lun, uint8_t cmd, int datalen,
    uint8_t *datap, int *send_struct_length)
{
	bmc_vc_send_t *sendp;

	ASSERT(datalen >= 0);
	ASSERT(send_struct_length != NULL);

	*send_struct_length = offsetof(bmc_vc_send_t, data) + datalen;
	if ((sendp = kmem_alloc(*send_struct_length, KM_NOSLEEP)) == NULL)
		return (NULL);

	sendp->magic_num = VC_MAGIC_NUM;
	sendp->datalen = (uint8_t)datalen;
	sendp->fnlun = FORM_NETFNLUN(netfn, lun);
	sendp->cmd = cmd;
	if (datalen > 0)
		bcopy(datap, sendp->data, datalen);

	return (sendp);
}

/*
 * Deallocates the resources associated with the send structure `sendp'
 * of size `sendp_len'.
 */
static void
vc_destruct_send_struct(bmc_vc_send_t *sendp, int sendp_len)
{
	kmem_free(sendp, sendp_len);
}

/*
 * The only time this is not interruptable is during attach
 */
int
do_vc2bmc(ipmi_state_t *ipmip, bmc_req_t *send_pkt, bmc_rsp_t *recv_pkt,
    boolean_t interruptable, uint8_t *errp)
{
	bmc_vc_send_t	*send_bmc;
	bmc_vc_recv_t	recv_bmc;
	clock_t		ticks;
	uint_t		retrycount = 5;
	uint32_t	spktsz, rpktsz;
	int		error, rval, ret = BMC_FAILURE;
	int		send_bmc_len = 0;
	vldc_opt_op_t	channel_op;
	ipmi_dev_t	*dev = &ipmip->ipmi_dev_ext;
	boolean_t	*aborted = &ipmip->task_abort;

	if (send_pkt->datalength > vc_max_send_payload) {
		dprintf(BMC_DEBUG_LEVEL_3, "failed send sz: %d",
		    send_pkt->datalength);
		recv_pkt->ccode = BMC_IPMI_DATA_LENGTH_EXCEED;
		recv_pkt->datalength = 0;
		return (ret);
	}

	send_bmc = vc_construct_send_struct(send_pkt->fn, send_pkt->lun,
	    send_pkt->cmd, send_pkt->datalength, send_pkt->data, &send_bmc_len);

	if (send_bmc == NULL) {
		*errp = ENOMEM;
		return (BMC_FAILURE);
	}


	mutex_enter(&dev->if_mutex);
	while (dev->if_busy) {

		if (*aborted == B_TRUE) {
			mutex_exit(&dev->if_mutex);
			dprintf(BMC_DEBUG_LEVEL_3, "do_vc2bmc task aborted");
			recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
			recv_pkt->datalength = 0;
			vc_destruct_send_struct(send_bmc, send_bmc_len);
			return (BMC_FAILURE);
		}

		if (cv_wait_sig(&dev->if_cv, &dev->if_mutex) == 0 &&
		    interruptable) {
			mutex_exit(&dev->if_mutex);
			vc_destruct_send_struct(send_bmc, send_bmc_len);
			return (BMC_FAILURE);
		}
	}

	dev->if_busy = B_TRUE;
	mutex_exit(&dev->if_mutex);

	ticks = drv_usectohz(DEFAULT_MSG_TIMEOUT);

	dev->timedout = B_FALSE;
	dev->timer_handle = realtime_timeout(vc_timer_handler, dev, ticks);

	dprintf(BMC_DEBUG_LEVEL_4,
	    "fn 0x%x lun 0x%x cmd 0x%x fnlun 0x%x len 0x%x",
	    send_pkt->fn, send_pkt->lun,
	    send_pkt->cmd, send_bmc->cmd,
	    send_pkt->datalength);

	spktsz = send_pkt->datalength + VC_SEND_NONDATA_SIZE;

	if (*aborted == B_TRUE) {
		dprintf(BMC_DEBUG_LEVEL_3, "do_vc2bmc open aborted");
		recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
		recv_pkt->datalength = 0;
		goto error_open;
	}

	/* Try to open the vldc channel. */
	if (ldi_open_by_name(IPMI_VLDC, (FREAD | FWRITE | FEXCL),
	    kcred, &vc_lh, vc_li) != 0) {
		dprintf(BMC_DEBUG_LEVEL_3, "ldi_open failed: %s", IPMI_VLDC);
		recv_pkt->ccode = BMC_IPMI_OEM_FAILURE_SENDBMC;
		goto error_open;
	} else {
		vc_opened = B_TRUE;
	}

	channel_op.op_sel = VLDC_OP_SET;
	channel_op.opt_sel = VLDC_OPT_MODE;
	channel_op.opt_val = LDC_MODE_RELIABLE;

	if ((error = ldi_ioctl(vc_lh, VLDC_IOCTL_OPT_OP,
	    (intptr_t)&channel_op, FKIOCTL, kcred, &rval)) != 0) {
		dprintf(BMC_DEBUG_LEVEL_3, "ldi_ioctl ret %d", error);
		goto error_ioctl;
	}

	while (vc_opened == B_TRUE && retrycount-- != 0) {

		if (*aborted == B_TRUE) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_vc2bmc aborted");
			recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
			recv_pkt->datalength = 0;
			break;
		}

		if (TIMEDOUT(dev)) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_vc2bmc timed out");
			break;
		}

		if (vc_write(ipmip, (uint8_t *)send_bmc, spktsz, aborted) ==
		    BMC_FAILURE) {
			dprintf(BMC_DEBUG_LEVEL_3, "send BMC failed");
			recv_pkt->ccode = BMC_IPMI_OEM_FAILURE_SENDBMC;
			continue;
		}

		if (vc_read(ipmip, (uint8_t *)&recv_bmc, &rpktsz, aborted) ==
		    BMC_FAILURE) {
			dprintf(BMC_DEBUG_LEVEL_3, "recv BMC failed");
			recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
			continue;
		}
#if DEBUG
		dprintf(BMC_DEBUG_LEVEL_3,
		    "SUMMARY 0x%x 0x%x resp: 0x%x req: 0x%x cmd 0x%x  " \
		    "CMD 0x%x len %d",
		    recv_bmc.fnlun, send_bmc->fnlun,
		    GET_NETFN(recv_bmc.fnlun),
		    RESP_NETFN(GET_NETFN(send_bmc->fnlun)),
		    recv_bmc.cmd, send_bmc->cmd, rpktsz);

		/*
		 * only for driver debugging:
		 * check return data and repackage
		 */
		if ((GET_NETFN(recv_bmc.fnlun) !=
		    RESP_NETFN(GET_NETFN(send_bmc->fnlun))) ||
		    (recv_bmc.cmd != send_bmc->cmd)) {
			dprintf(BMC_DEBUG_LEVEL_3,
			    "return parameters are not expected");
			dprintf(BMC_DEBUG_LEVEL_4,
			    "GET_NETFN(recv_bmc.fnlun) 0x%x " \
			    "RESP_NETFN(GET_NETFN(send_bmc->fnlun)) 0x%x " \
			    "recv cmd 0x%x send cmd 0x%x", \
			    GET_NETFN(recv_bmc.fnlun), \
			    RESP_NETFN(GET_NETFN(send_bmc->fnlun)), \
			    recv_bmc.cmd, send_bmc->cmd);
		}
#endif
		/*
		 * Subtract the size of non-data fields from the receive packet
		 * size to get the amount of data in the data field.
		 */
		rpktsz -= VC_RECV_NONDATA_SIZE;

		recv_pkt->fn = GET_NETFN(recv_bmc.fnlun);
		recv_pkt->lun = GET_LUN(recv_bmc.fnlun);
		recv_pkt->cmd = recv_bmc.cmd;

		/*
		 * If the caller didn't provide enough data space to hold
		 * the response, return failure.
		 */
		if (rpktsz > recv_pkt->datalength) {
			dprintf(BMC_DEBUG_LEVEL_3, "failed recv sz: %d",
			    recv_pkt->datalength);
			recv_pkt->ccode = BMC_IPMI_DATA_LENGTH_EXCEED;
			recv_pkt->datalength = 0;
		} else {
			recv_pkt->ccode = recv_bmc.ccode;
			recv_pkt->datalength = (uint8_t)rpktsz;
			bcopy(&recv_bmc.data, recv_pkt->data, rpktsz);
			ret = BMC_SUCCESS;
		}
		break;
	}

error_ioctl:
	if (vc_opened == B_TRUE) {
		(void) ldi_close(vc_lh, NULL, kcred);
		vc_opened = B_FALSE;
	}

error_open:
	(void) untimeout(dev->timer_handle);
	dev->timer_handle = 0;

	mutex_enter(&dev->if_mutex);
	ASSERT(dev->if_busy == B_TRUE);
	dev->if_busy = B_FALSE;
	cv_signal(&dev->if_cv);
	mutex_exit(&dev->if_mutex);

	vc_destruct_send_struct(send_bmc, send_bmc_len);

	return (ret);
}

/*
 * Returns the size of the largest possible response payload.
 */
int
bmc_vc_max_response_payload_size(void)
{
	return (VC_RECV_MAX_PAYLOAD_SIZE);
}

/*
 * Returns the size of the largest possible request payload.
 */
int
bmc_vc_max_request_payload_size(void)
{
	return (vc_max_send_payload);
}
