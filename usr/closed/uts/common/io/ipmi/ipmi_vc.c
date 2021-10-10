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

#include "ipmi_sol_int.h"


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
 *	==============                  .     ------------------
 *	# /dev/ipmi0 #                  .     | IPMI ILOM task |
 *	==============                  .     ------------------
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

#define	BMC_SUCCESS			0x0
#define	BMC_FAILURE			0x1
#define	BMC_ABORT			0x2
#define	BMC_TIMEOUT			0x3
#define	BMC_SIG				0x4
#define	BMC_LOGERR			0x5

/*
 * Tunables
 */

/*
 * Some interfaces can handle a massive amount of request data, e.g.
 * when they're used to transfer flash images to the system service processor.
 * To enable faster bulk-data transfer on such BMCs, allow the max send payload
 * to be dynamically tunable.
 */
static int vc_max_send_payload = VC_SEND_MAX_PAYLOAD_SIZE;

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

int ipmi_vc_debug = 0;

static int ipmi_vc_probe(dev_info_t *, int, int);
static int ipmi_vc_attach(void *);
static int ipmi_vc_detach(void *);
static int ipmi_vc_resume(void *);
static int ipmi_vc_suspend(void *);
static int ipmi_vc_poll(void *);

/*ARGSUSED*/
/*PRINTFLIKE2*/
static void
dprintf(int d, const char *format, ...)
{
#ifdef DEBUG
	if (d <= ipmi_vc_debug) {
		va_list ap;
		va_start(ap, format);
		vcmn_err(d < BMC_DEBUG_LEVEL_2 ? CE_WARN : CE_CONT, format, ap);
		va_end(ap);
	}
#endif
}

static int
vc_init(dev_info_t *dip, ldi_ident_t *vc_li)
{
	int ret = BMC_SUCCESS;

	if (ldi_ident_from_dip(dip, vc_li) != 0)
		ret = BMC_FAILURE;

	return (ret);
}

static void
vc_uninit(ldi_ident_t *vc_li)
{
	ldi_ident_release(*vc_li);
	*vc_li = NULL;
}

static void
vc_timer_handler(void *devp)
{
	ipmi_vc_dev_t *dev = (ipmi_vc_dev_t *)devp;

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
static int
vc_reopen_vldc(ipmi_vc_dev_t *devp)
{
	vldc_opt_op_t channel_op;

	ASSERT(devp->if_busy == B_TRUE);

	if (devp->if_vc_opened == B_TRUE) {
		(void) ldi_close(devp->if_vc_lh, NULL, kcred);
		devp->if_vc_opened = B_FALSE;
	}

	delay(drv_usectohz(50000));

	if (ldi_open_by_name(IPMI_VLDC, (FREAD | FWRITE | FEXCL),
	    kcred, &devp->if_vc_lh, devp->if_vc_li) != 0) {
		dprintf(BMC_DEBUG_LEVEL_4,
		    "reconnect failed at ldi_open_by_name");
		return (BMC_FAILURE);
	} else {
		devp->if_vc_opened = B_TRUE;
	}

	channel_op.op_sel = VLDC_OP_SET;
	channel_op.opt_sel = VLDC_OPT_MODE;
	channel_op.opt_val = LDC_MODE_RELIABLE;

	if (ldi_ioctl(devp->if_vc_lh, VLDC_IOCTL_OPT_OP,
	    (intptr_t)&channel_op, FKIOCTL, kcred, NULL) != 0) {
		dprintf(BMC_DEBUG_LEVEL_4, "reconnect failed at ldi_ioctl");
		(void) ldi_close(devp->if_vc_lh, NULL, kcred);
		devp->if_vc_opened = B_FALSE;
		return (BMC_FAILURE);
	}

	dprintf(BMC_DEBUG_LEVEL_4,
	    "reestablished connection with %s", IPMI_VLDC);

	return (BMC_SUCCESS);
}

static int
vc_write(ipmi_vc_dev_t *devp, uint8_t *send_buf, uint32_t pktsz,
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
	ipmi_kstat_t	*ksp = devp->kstatsp;

	dprintf(BMC_DEBUG_LEVEL_3, "VLDC write Len 0x%x [[START]]",
	    (int)pktsz);

	p = (uint8_t *)send_buf;
	while (devp->if_vc_opened == B_TRUE &&
	    p < (uint8_t *)&send_buf[pktsz] && !TIMEDOUT(devp)) {

		if (*aborted == B_TRUE) {
			dprintf(BMC_DEBUG_LEVEL_3, "VLDC write task abort");
			break;
		}

		if ((error = ldi_poll(devp->if_vc_lh, POLLOUT,
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

		error = ldi_write(devp->if_vc_lh, &uio, kcred);

		if (error == EAGAIN) continue;

		if (!error) {
			p = (uint8_t *)iov.iov_base;
			atomic_add_64(&ksp->ipmi_bytes_out.value.ui64,
			    chunksize);
		} else {
			dprintf(BMC_DEBUG_LEVEL_3, "ldi_write ret %d", error);
			break;
		}
	}

	if (devp->if_vc_opened == B_TRUE && !error)
		ret = BMC_SUCCESS;

	dprintf(BMC_DEBUG_LEVEL_3, "VLDC write [[END]]");

	return (ret);
}

static int
vc_read(ipmi_vc_dev_t *devp, uint8_t *recv_buf, uint32_t *pktsz,
    boolean_t *aborted)
{
	struct uio	uio;
	struct iovec	iov;
	int		error;
	int		ret = BMC_FAILURE;
	int		anyyet = 0;
	short		reventsp;
	struct pollhead	*php;
	ipmi_kstat_t	*ksp = devp->kstatsp;

	dprintf(BMC_DEBUG_LEVEL_3, "VLDC read [[START]]");

	*pktsz = 0;

	while (devp->if_vc_opened == B_TRUE && !TIMEDOUT(devp)) {

		if (*aborted == B_TRUE) {
			dprintf(BMC_DEBUG_LEVEL_3, "VLDC read task abort");
			break;
		}

		if ((error = ldi_poll(devp->if_vc_lh, (POLLIN | POLLPRI),
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

		error = ldi_read(devp->if_vc_lh, &uio, kcred);

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

			atomic_add_64(&ksp->ipmi_bytes_in.value.ui64, *pktsz);

			ret = BMC_SUCCESS;
			break;
		} else {
			dprintf(BMC_DEBUG_LEVEL_3, "ldi_read ret %d", error);
			break;
		}
	}

	return (ret);
}

/*
 * Allocates a bmc_vc_send_t with enough space in the data member to
 * fit `datalen' bytes and initializes it with the supplied values.
 */
static bmc_vc_send_t *
vc_construct_send_struct(uint8_t addr, uint8_t cmd, int datalen,
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
	sendp->fnlun = addr;
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
static int
do_vc2bmc(ipmi_state_t *statep, struct ipmi_request *req,
    boolean_t interruptable)
{
	bmc_vc_send_t	*send_bmc;
	bmc_vc_recv_t	recv_bmc;
	clock_t		clticks;
	uint_t		retrycount = 2;
	uint32_t	spktsz, rpktsz;
	int		rval, error, ret = BMC_FAILURE;
	int		send_bmc_len = 0;
	vldc_opt_op_t	channel_op;
	ipmi_vc_dev_t	*dev = statep->is_dev_ext;
	boolean_t	*aborted = &statep->is_task_abort;

	if (req->ir_requestlen > vc_max_send_payload) {
		req->ir_compcode = BMC_IPMI_DATA_LENGTH_EXCEED;
		req->ir_error = EINVAL;
		dprintf(BMC_DEBUG_LEVEL_3, "failed send sz: %d",
		    (int)req->ir_requestlen);
		return (BMC_LOGERR);
	}

	send_bmc = vc_construct_send_struct(req->ir_addr, req->ir_command,
	    req->ir_requestlen, req->ir_request, &send_bmc_len);

	if (send_bmc == NULL)
		return (BMC_LOGERR);

	mutex_enter(&dev->if_mutex);
	while (dev->if_busy) {

		if (*aborted == B_TRUE) {
			mutex_exit(&dev->if_mutex);
			req->ir_error = EINTR;
			dprintf(BMC_DEBUG_LEVEL_3, "do_vc2bmc task aborted");
			vc_destruct_send_struct(send_bmc, send_bmc_len);
			return (BMC_ABORT);
		}

		if (cv_wait_sig(&dev->if_cv, &dev->if_mutex) == 0 &&
		    interruptable) {
			mutex_exit(&dev->if_mutex);
			vc_destruct_send_struct(send_bmc, send_bmc_len);
			req->ir_error = EINTR;
			return (BMC_SIG);
		}
	}

	dev->if_busy = B_TRUE;
	mutex_exit(&dev->if_mutex);

	clticks = drv_usectohz(MSEC2USEC(req->ir_retrys.retry_time_ms));

	dev->timedout = B_FALSE;
	dev->timer_handle = realtime_timeout(vc_timer_handler, dev, clticks);

	dprintf(BMC_DEBUG_LEVEL_4, "addr 0x%x cmd 0x%x data len 0x%x",
	    req->ir_addr, req->ir_command, (int)req->ir_requestlen);

	spktsz = req->ir_requestlen + VC_SEND_NONDATA_SIZE;

	if (*aborted == B_TRUE) {
		dprintf(BMC_DEBUG_LEVEL_3, "do_vc2bmc open aborted");
		req->ir_error = EINTR;
		/* req->ir_compcode = BMC_IPMI_COMMAND_TIMEOUT; */
		ret = BMC_ABORT;
		goto error_open;
	}

	/* Try to open the vldc channel. */
	if (ldi_open_by_name(IPMI_VLDC, (FREAD | FWRITE | FEXCL),
	    kcred, &dev->if_vc_lh, dev->if_vc_li) != 0) {
		dprintf(BMC_DEBUG_LEVEL_3, "ldi_open failed: %s", IPMI_VLDC);
		req->ir_error = ENOENT;
		ret = BMC_LOGERR;
		goto error_open;
	} else {
		dev->if_vc_opened = B_TRUE;
	}

	channel_op.op_sel = VLDC_OP_SET;
	channel_op.opt_sel = VLDC_OPT_MODE;
	channel_op.opt_val = LDC_MODE_RELIABLE;

	if ((error = ldi_ioctl(dev->if_vc_lh, VLDC_IOCTL_OPT_OP,
	    (intptr_t)&channel_op, FKIOCTL, kcred, &rval)) != 0) {
		dprintf(BMC_DEBUG_LEVEL_3, "ldi_ioctl ret %d", error);
		req->ir_error = error;
		ret = BMC_LOGERR;
		goto error_ioctl;
	}

	while (dev->if_vc_opened == B_TRUE && retrycount-- != 0) {

		if (*aborted == B_TRUE) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_vc2bmc aborted");
			req->ir_error = EINTR;
			ret = BMC_ABORT;
			break;
		}

		if (TIMEDOUT(dev)) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_vc2bmc timed out");
			ret = BMC_TIMEOUT;
			break;
		}

		if (vc_write(dev, (uint8_t *)send_bmc, spktsz, aborted) ==
		    BMC_FAILURE) {
			dprintf(BMC_DEBUG_LEVEL_3, "send BMC failed");
			req->ir_error = ENXIO;
			continue;
		}

		if (vc_read(dev, (uint8_t *)&recv_bmc, &rpktsz, aborted) ==
		    BMC_FAILURE) {
			dprintf(BMC_DEBUG_LEVEL_3, "recv BMC failed");
			req->ir_error = ENXIO;
			continue;
		}

		/*
		 * Subtract the size of non-data fields from the receive packet
		 * size to get the amount of data in the data field.
		 */
		rpktsz -= VC_RECV_NONDATA_SIZE;

		if (IPMI_REPLY_ADDR(req->ir_addr) != recv_bmc.fnlun) {
			cmn_err(CE_NOTE, "KCS Reply address mismatch\n");
			ret = BMC_FAILURE;
		}
		if (req->ir_command != recv_bmc.cmd) {
			cmn_err(CE_NOTE, "KCS Reply Command mismatch\n");
			ret = BMC_FAILURE;
		}

		req->ir_compcode = recv_bmc.ccode;

		/*
		 * If the caller didn't provide enough data space to hold
		 * the response, return failure.
		 */
		if (rpktsz > req->ir_replybuflen) {
			dprintf(BMC_DEBUG_LEVEL_3, "failed recv sz: %d",
			    (int)req->ir_replybuflen);
			ret = BMC_LOGERR;
		} else {
			req->ir_error = 0;
			req->ir_replylen = rpktsz;
			bcopy(&recv_bmc.data, req->ir_reply, rpktsz);
			ret = BMC_SUCCESS;
		}
		break;
	}

error_ioctl:
	if (dev->if_vc_opened == B_TRUE) {
		(void) ldi_close(dev->if_vc_lh, NULL, kcred);
		dev->if_vc_opened = B_FALSE;
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
 * Template for driver plugin conversions
 */


/*
 * Interface for new IPMI driver
 */

/*
 * Normally there are one of these per-interface instance.
 * We place this in the array of pi instances for connection
 * to the main driver ipmi_pi[]. When the plugins are turned
 * into a separate module this will be passed to a function
 * ipmi_sol_pi_register(struct ipmi_plugin *).
 */
struct ipmi_plugin ipmi_vc = {
	.ipmi_pi_probe = ipmi_vc_probe,
	.ipmi_pi_attach = ipmi_vc_attach,
	.ipmi_pi_detach = ipmi_vc_detach,
	.ipmi_pi_suspend = ipmi_vc_suspend,
	.ipmi_pi_resume = ipmi_vc_resume,
	.ipmi_pi_pollstatus = ipmi_vc_poll,
	.ipmi_pi_name = "ipmivc",
	.ipmi_pi_intfinst = 1,
	.ipmi_pi_flags = (IPMIPI_POLLED|IPMIPI_PEVENT|IPMIPI_SUSRES|
	    IPMIPI_DELYATT|IPMIPI_NOASYNC),
};



static void
vc_loop(void *arg)
{
	ipmi_state_t		*statep = arg;
	ipmi_vc_dev_t		*devp = statep->is_dev_ext;
	struct ipmi_softc	*sc = &statep->is_bsd_softc;
	struct ipmi_request	*req;
	int			try;
	int			retry;
	int			ret;
	int			done;

	mutex_enter(&statep->is_bsd_softc.ipmi_lock);
	while ((req = statep->is_pi->ipmi_pi_getreq(sc)) != NULL) {
		done = 0;
		mutex_exit(&statep->is_bsd_softc.ipmi_lock);
		retry = req->ir_retrys.retries; /* Is zero based */
		retry++;			/* Were one based */
		for (try = 0; !done && try < retry; try++) {
			ret = do_vc2bmc(statep, req, B_TRUE);
			switch (ret) {
			case BMC_SUCCESS:
			case BMC_LOGERR:
			case BMC_SIG:
			case BMC_ABORT:
				done = 1;
				continue;
			case BMC_FAILURE:
			case BMC_TIMEOUT:
				break;
			}
		}
		mutex_enter(&statep->is_bsd_softc.ipmi_lock);
		statep->is_pi->ipmi_pi_putresp(sc, req);
		if (devp->if_up == B_FALSE)
			break;
	}
	mutex_exit(&statep->is_bsd_softc.ipmi_lock);
}

/* ARGSUSED */
static int
ipmi_vc_suspend(void *ipmi_state)
{
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
ipmi_vc_resume(void *ipmi_state)
{
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
ipmi_vc_probe(dev_info_t *dip, int ipmi_instance, int kcs_instance)
{
	ldi_handle_t	vc_lh;
	ldi_ident_t	vc_li;

	if (vc_init(dip, &vc_li) != BMC_SUCCESS)
		return (0);

	/* Try to open the vldc channel. */
	if (ldi_open_by_name(IPMI_VLDC, (FREAD | FWRITE | FEXCL),
	    kcred, &vc_lh, vc_li) != 0) {
		vc_uninit(&vc_li);
		return (0);
	}

	(void) ldi_close(vc_lh, NULL, kcred);
	vc_uninit(&vc_li);

	return (1);
}

static int
ipmi_vc_poll(void *arg)
{
	ipmi_state_t	*statep = arg;
	ipmi_vc_dev_t	*dev = statep->is_dev_ext;
	int		anyyet = 0;
	short		reventsp;
	struct pollhead	*php;
	int		error;
	int		retval = 0;

	error = ldi_poll(dev->if_vc_lh, (POLLIN | POLLPRI), anyyet,
	    &reventsp, &php);
	if (error == ENOTACTIVE || error == ECONNRESET) {
		if (vc_reopen_vldc(dev) != BMC_SUCCESS)
			return (0);
		anyyet = 0;
		error = ldi_poll(dev->if_vc_lh, (POLLIN | POLLPRI),
		    anyyet, &reventsp, &php);
	}

	if (!error)
		if ((reventsp & (POLLIN | POLLPRI)))
			retval |= IPMIFL_ATTN;

	return (retval);
}

static int
ipmi_vc_attach(void *ipmi_state)
{
	ipmi_state_t		*statep = (ipmi_state_t *)ipmi_state;
	ipmi_vc_dev_t		*devp;


	statep->is_dev_ext = kmem_zalloc(sizeof (ipmi_vc_dev_t), KM_SLEEP);
	if (statep->is_dev_ext == NULL) {
		return (DDI_FAILURE);
	}
	devp = statep->is_dev_ext;
	devp->kstatsp = &statep->is_kstats;

	if (vc_init(statep->is_dip, &devp->if_vc_li) != BMC_SUCCESS) {
		kmem_free(statep->is_dev_ext, sizeof (ipmi_vc_dev_t));
		return (DDI_FAILURE);
	}

	mutex_init(&devp->if_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&devp->if_cv, NULL, CV_DEFAULT, NULL);
	devp->if_busy = B_FALSE;

	devp->if_vc_opened = B_FALSE;

	devp->if_up = B_TRUE;

	/*
	 * Now that we have hardware and software setup
	 * start the dispatch task
	 */
	(void) sprintf(devp->if_name, "%s%d", ipmi_vc.ipmi_pi_name,
	    statep->is_instance);
	if (statep->is_pi->ipmi_pi_taskinit(statep, vc_loop, devp->if_name)
	    != DDI_SUCCESS) {
		vc_uninit(&devp->if_vc_li);
		mutex_destroy(&devp->if_mutex);
		cv_destroy(&devp->if_cv);
		kmem_free(statep->is_dev_ext, sizeof (ipmi_vc_dev_t));
		statep->is_dev_ext = NULL;
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
ipmi_vc_detach(void *ipmi_state)
{
	ipmi_state_t		*statep = (ipmi_state_t	*)ipmi_state;
	ipmi_vc_dev_t		*devp;

	devp = statep->is_dev_ext;
	if (devp) {
		devp->if_up = B_FALSE;	/* End tasks while loop */
		statep->is_pi->ipmi_pi_taskexit(statep);

		vc_uninit(&devp->if_vc_li);

		mutex_destroy(&devp->if_mutex);
		cv_destroy(&devp->if_cv);

		kmem_free(statep->is_dev_ext, sizeof (ipmi_vc_dev_t));
	}
	statep->is_dev_ext = NULL;
	return (DDI_SUCCESS);
}
