/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 2006 IronPort Systems Inc. <ambrisko@ironport.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define	IN_BSD_CODE	1
#include "ipmi_sol_int.h"

#ifdef IPMB
static int ipmi_ipmb_checksum(uchar_t, int);
static int ipmi_ipmb_send_message(device_t, uchar_t, uchar_t, uchar_t,
    uchar_t, uchar_t, int)
#endif

static d_ioctl_t ipmi_ioctl;
static d_poll_t ipmi_poll;
static d_open_t ipmi_open;
static void ipmi_dtor(void *arg);

int ipmi_attached = 0;

extern	int ipmi_watchd_timo;		/* Watchdog time-out time in secs. */
extern	int ipmi_watchd_update;		/* Watchdog system update in secs. */

static int on = 1;
SYSCTL_NODE(_hw, OID_AUTO, ipmi, CTLFLAG_RD, 0, "IPMI driver parameters");
SYSCTL_INT(_hw_ipmi, OID_AUTO, on, CTLFLAG_RW,
	&on, 0, "");

static struct cdevsw ipmi_cdevsw = {
	.d_version =    D_VERSION,
	.d_open =	ipmi_open,
	.d_ioctl =	ipmi_ioctl,
	.d_poll =	ipmi_poll,
	.d_name =	"ipmi",
};

MALLOC_DEFINE(M_IPMI, "ipmi", "ipmi");

/* ARGSUSED */

static int
ipmi_open(ipmi_clone_t *clonep, int flags, int fmt, struct thread *td)
{
	struct ipmi_device *dev;
	struct ipmi_softc *sc;
	int error;

	if (!on)
		return (ENOENT);

	/* Initialize the per file descriptor data. */
	dev = kmem_zalloc(sizeof (struct ipmi_device), KM_SLEEP);
	error = devfs_set_cdevpriv(clonep, dev, ipmi_dtor);
	if (error) {
		kmem_free(dev, sizeof (struct ipmi_device));
		return (error);
	}
	sc = &clonep->ic_statep->is_bsd_softc;
	list_create(&dev->ipmi_completed_requests,
	    sizeof (struct ipmi_request),
	    offsetof(struct ipmi_request, ir_link));
	cv_init(&dev->ipmi_close_cv, "ipmiclose");
	dev->ipmi_address = IPMI_BMC_SLAVE_ADDR;
	dev->ipmi_lun = IPMI_BMC_SMS_LUN;
	dev->ipmi_softc = sc;
	return (0);
}

static int
ipmi_poll(ipmi_clone_t *clonep, int poll_events, void **td)
{
	struct ipmi_device *dev;
	struct ipmi_softc *sc;
	int revents = 0;

	if (devfs_get_cdevpriv(clonep, (void **)&dev))
		return (0);

	sc = &clonep->ic_statep->is_bsd_softc;
	IPMI_LOCK(sc);
	if (poll_events & (POLLIN | POLLRDNORM)) {
		if (list_head(&dev->ipmi_completed_requests))
			revents |= poll_events & (POLLIN | POLLRDNORM);
		if (dev->ipmi_requests == 0)
			revents |= POLLERR;
	}

	if (revents == 0) {
		if (poll_events & (POLLIN | POLLRDNORM))
			selrecord(td, &dev->ipmi_select);
	}
	IPMI_UNLOCK(sc);

	return (revents);
}

static void
ipmi_purge_completed_requests(struct ipmi_device *dev)
{
	struct ipmi_request *req;

	while ((req = list_head(&dev->ipmi_completed_requests)) != NULL) {
		list_remove(&dev->ipmi_completed_requests, req);
		if (dev->ipmi_requests > 0)
			dev->ipmi_requests--;
		ipmi_free_request(req);
	}
}

static void
ipmi_dtor(void *arg)
{
	struct ipmi_request *req;
	struct ipmi_device *dev;
	struct ipmi_softc *sc;

	dev = arg;
	sc = dev->ipmi_softc;

	IPMI_LOCK(sc);
	dev->ipmi_closing = 1;
	if (dev->ipmi_requests) {
		/* Throw away any pending requests for this device. */
		req =  list_head(&sc->ipmi_pending_requests);
		while (req) {
			if (req->ir_owner == dev && req->ir_sflag & IR_PENDQ) {
				list_remove(&sc->ipmi_pending_requests, req);
				dev->ipmi_requests--;
				ipmi_free_request(req);
				req =  list_head(&sc->ipmi_pending_requests);
			} else {
				req = list_next(&sc->ipmi_pending_requests,
				    req);
			}
		}

		/* Throw away any pending completed requests for this device. */
		ipmi_purge_completed_requests(dev);

		/*
		 * If we still have outstanding requests, they must be stuck
		 * in an interface driver, so wait for those to drain.
		 */
		while (dev->ipmi_requests > 0) {
			dev->ipmi_slflag = 1;
			(void) ipmi_msleep(&dev->ipmi_close_cv, &sc->ipmi_lock,
			    &dev->ipmi_slflag, 0);
			ipmi_purge_completed_requests(dev);
		}
	}
	/*
	 * Incase there are unknow messages on queue.
	 * Not that this should not ever occur. If it does we have a problem
	 * but it is better to free the entries instead of causing a memory
	 * leak!
	 */
	ipmi_purge_completed_requests(dev);


	list_destroy(&dev->ipmi_completed_requests);
	cv_destroy(&dev->ipmi_close_cv);
	/*	sc->ipmi_opened--; */
	IPMI_UNLOCK(sc);

	/* Cleanup. */
	kmem_free(dev, sizeof (struct ipmi_device));
}

#ifdef IPMB
static int
ipmi_ipmb_checksum(uchar_t *data, int len)
{
	uchar_t sum = 0;

	for (; len; len--) {
		sum += *data++;
	}
	return (-sum);
}

/* XXX: Needs work */
static int
ipmi_ipmb_send_message(device_t dev, uchar_t channel, uchar_t netfn,
    uchar_t command, uchar_t seq, uchar_t *data, int data_len)
{
	struct ipmi_softc *sc = device_get_softc(dev);
	struct ipmi_request *req;
	uchar_t slave_addr = 0x52;
	int error;

	req = ipmi_alloc_driver_request(IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_SEND_MSG, data_len + 8, 0);
	req->ir_request[0] = channel;
	req->ir_request[1] = slave_addr;
	req->ir_request[2] = IPMI_ADDR(netfn, 0);
	req->ir_request[3] = ipmi_ipmb_checksum(&req->ir_request[1], 2);
	req->ir_request[4] = sc->ipmi_address;
	req->ir_request[5] = IPMI_ADDR(seq, sc->ipmi_lun);
	req->ir_request[6] = command;

	bcopy(data, &req->ir_request[7], data_len);
	temp[data_len + 7] = ipmi_ipmb_checksum(&req->ir_request[4],
	    data_len + 3);

	(void) ipmi_submit_driver_request(sc, req);
	error = req->ir_error;
	ipmi_free_request(req);

	return (error);
}

static int
ipmi_handle_attn(struct ipmi_softc *sc)
{
	struct ipmi_request *req;
	int error;

	device_printf(sc->ipmi_dev, "BMC has a message\n");
	req = ipmi_alloc_driver_request(IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_GET_MSG_FLAGS, 0, 1);

	(void) ipmi_submit_driver_request(sc, req);

	if (req->ir_error == 0 && req->ir_compcode == 0) {
		if (req->ir_reply[0] & IPMI_MSG_BUFFER_FULL) {
			device_printf(sc->ipmi_dev, "message buffer full");
		}
		if (req->ir_reply[0] & IPMI_WDT_PRE_TIMEOUT) {
			device_printf(sc->ipmi_dev,
			    "watchdog about to go off");
		}
		if (req->ir_reply[0] & IPMI_MSG_AVAILABLE) {
			ipmi_free_request(req);

			req = ipmi_alloc_driver_request(
			    IPMI_ADDR(IPMI_APP_REQUEST, 0), IPMI_GET_MSG, 0,
			    16);

			device_printf(sc->ipmi_dev, "throw out message ");
			dump_buf(temp, 16);
		}
	}
	error = req->ir_error;
	ipmi_free_request(req);

	return (error);
}
#endif

#ifdef IPMICTL_SEND_COMMAND_32
#define	PTRIN(p)	((void *)(uintptr_t)(p))
#define	PTROUT(p)	((uintptr_t)(p))
#endif


static int
ipmi_ioctl(ipmi_clone_t *clonep, int cmd, intptr_t data,
    int flags, cred_t *cred)
{
	struct ipmi_softc *sc;
	struct ipmi_device *dev;
	struct ipmi_request *kreq;
	struct ipmi_req i_req;
	struct ipmi_req_settime i_req_time;
	struct ipmi_recv i_recv;
	struct ipmi_addr addr;
#ifdef IPMICTL_SEND_COMMAND_32
	struct ipmi_req32 i_req32;
	struct ipmi_recv32 i_recv32;
	struct ipmi_req_settime32 i_req_time32;
#endif
	int error = 0;
	int len, alen;

	extern boolean_t ipmi_command_requires_privilege(uint8_t, uint8_t);


	error = devfs_get_cdevpriv(clonep, (void **)&dev);
	if (error)
		return (error);

	sc = &clonep->ic_statep->is_bsd_softc;

#ifdef IPMICTL_SEND_COMMAND_32
	/* Convert 32-bit structures to native. */
	switch (cmd) {
	case IPMICTL_SEND_COMMAND_SETTIME_32:
		/*
		 * First copyin the reqest structure
		 */
		error = ddi_copyin((void *)data, &i_req_time32,
		    sizeof (i_req_time32), flags);
		if (error == -1)
			return (EFAULT);
		i_req_time.req.addr = PTRIN(i_req_time32.req.addr);
		i_req_time.req.addr_len = i_req_time32.req.addr_len;
		i_req_time.req.msgid = i_req_time32.req.msgid;
		i_req_time.req.msg.netfn = i_req_time32.req.msg.netfn;
		i_req_time.req.msg.cmd = i_req_time32.req.msg.cmd;
		i_req_time.req.msg.data_len = i_req_time32.req.msg.data_len;
		i_req_time.req.msg.data = PTRIN(i_req_time32.req.msg.data);
		i_req_time.retries = i_req_time32.retries;
		i_req_time.retry_time_ms = i_req_time32.retry_time_ms;
		break;
	case IPMICTL_SEND_COMMAND_32:
		/*
		 * First copyin the reqest structure
		 */
		error = ddi_copyin((void *)data, &i_req32,
		    sizeof (i_req32), flags);
		if (error == -1)
			return (EFAULT);
		i_req.addr = PTRIN(i_req32.addr);
		i_req.addr_len = i_req32.addr_len;
		i_req.msgid = i_req32.msgid;
		i_req.msg.netfn = i_req32.msg.netfn;
		i_req.msg.cmd = i_req32.msg.cmd;
		i_req.msg.data_len = i_req32.msg.data_len;
		i_req.msg.data = PTRIN(i_req32.msg.data);
		break;
	case IPMICTL_RECEIVE_MSG_TRUNC_32:
	case IPMICTL_RECEIVE_MSG_32:
		/*
		 * First copyin the reqest structure
		 */
		error = ddi_copyin((void *)data, &i_recv32,
		    sizeof (i_recv32), flags);
		if (error == -1)
			return (EFAULT);
		i_recv.addr = PTRIN(i_recv32.addr);
		i_recv.addr_len = i_recv32.addr_len;
		i_recv.msg.data_len = i_recv32.msg.data_len;
		i_recv.msg.data = PTRIN(i_recv32.msg.data);
		break;
	}
#endif


	switch (cmd) {
	case IPMICTL_SEND_COMMAND_SETTIME:
		/*
		 * First copyin the reqest structure
		 */
		error = ddi_copyin((void *)data, &i_req_time,
		    sizeof (i_req_time), flags);
		if (error == -1)
			return (EFAULT);
		/* FALLTHRU */
#ifdef IPMICTL_SEND_COMMAND_32
	case IPMICTL_SEND_COMMAND_SETTIME_32:
#endif
		/* Is the user authorized to use this command? */
		if (ipmi_command_requires_privilege(i_req_time.req.msg.cmd,
		    i_req_time.req.msg.netfn) &&
		    secpolicy_sys_config(cred, B_FALSE) != 0)
			return (EACCES);


		/*
		 * Now copyin the addr structure
		 */
		if (i_req_time.req.addr_len <= sizeof (addr))
			len = i_req_time.req.addr_len;
		else
			len = sizeof (addr);
		error = ddi_copyin((void *)i_req_time.req.addr, &addr,
		    len, flags);
		if (error == -1)
			return (EFAULT);

		IPMI_LOCK(sc);
		if (dev->ipmi_closing) {
			IPMI_UNLOCK(sc);
			return (ENXIO);
		}
		IPMI_UNLOCK(sc);

		kreq = ipmi_alloc_request(dev, i_req_time.req.msgid,

		    IPMI_ADDR(i_req_time.req.msg.netfn, 0),
		    i_req_time.req.msg.cmd,
		    i_req_time.req.msg.data_len, IPMI_MAX_RX);
		if (i_req_time.req.msg.data_len) {
			error = ddi_copyin((void *)i_req_time.req.msg.data,
			    kreq->ir_request,
			    i_req_time.req.msg.data_len, flags);
			if (error == -1) {
				ipmi_free_request(kreq);
				return (EFAULT);
			}
		}
		kreq->ir_retrys.retries = i_req_time.retries;
		kreq->ir_retrys.retry_time_ms = i_req_time.retry_time_ms;
		IPMI_LOCK(sc);
		dev->ipmi_requests++;
		error = sc->ipmi_enqueue_request(sc, kreq);
		IPMI_UNLOCK(sc);
		if (error)
			return (error);

		break;
	case IPMICTL_SEND_COMMAND:
		/*
		 * First copyin the reqest structure
		 */
		error = ddi_copyin((void *)data, &i_req, sizeof (i_req), flags);
		if (error == -1)
			return (EFAULT);
		/* FALLTHRU */
#ifdef IPMICTL_SEND_COMMAND_32
	case IPMICTL_SEND_COMMAND_32:
#endif

		/* Is the user authorized to use this command? */
		if (ipmi_command_requires_privilege(i_req.msg.cmd,
		    i_req.msg.netfn) &&
		    secpolicy_sys_config(cred, B_FALSE) != 0)
			return (EACCES);


		/*
		 * Now copyin the addr structure
		 */
		if (i_req.addr_len <= sizeof (addr))
			alen = i_req.addr_len;
		else
			alen = sizeof (addr);
		error = ddi_copyin((void *)i_req.addr, &addr, alen, flags);
		if (error == -1)
			return (EFAULT);

		IPMI_LOCK(sc);
		if (dev->ipmi_closing) {
			IPMI_UNLOCK(sc);
			return (ENXIO);
		}
		IPMI_UNLOCK(sc);

		kreq = ipmi_alloc_request(dev, i_req.msgid,
		    IPMI_ADDR(i_req.msg.netfn, 0), i_req.msg.cmd,
		    i_req.msg.data_len, IPMI_MAX_RX);
		if (i_req.msg.data_len) {
			error = ddi_copyin((void *)i_req.msg.data,
			    kreq->ir_request, i_req.msg.data_len, flags);
			if (error == -1) {
				ipmi_free_request(kreq);
				return (EFAULT);
			}
		}
		kreq->ir_retrys.retries = clonep->ic_retrys.retries;
		kreq->ir_retrys.retry_time_ms = clonep->ic_retrys.retry_time_ms;
		IPMI_LOCK(sc);
		dev->ipmi_requests++;
		error = sc->ipmi_enqueue_request(sc, kreq);
		IPMI_UNLOCK(sc);
		if (error)
			return (error);
		break;

	case IPMICTL_RECEIVE_MSG_TRUNC:
	case IPMICTL_RECEIVE_MSG:
		/*
		 * First copyin the reqest structure
		 */
		error = ddi_copyin((void *)data, &i_recv, sizeof (i_recv),
		    flags);
		if (error == -1)
			return (EFAULT);
		/* FALLTHRU */
#ifdef IPMICTL_SEND_COMMAND_32
	case IPMICTL_RECEIVE_MSG_TRUNC_32:
	case IPMICTL_RECEIVE_MSG_32:
#endif
		/*
		 * Now copyin the addr structure
		 */
		if (i_recv.addr_len <= sizeof (addr))
			alen = i_recv.addr_len;
		else
			alen = sizeof (addr);
		error = ddi_copyin((void *)i_recv.addr, &addr, alen, flags);
		if (error == -1)
			return (EFAULT);

		IPMI_LOCK(sc);
		kreq = list_head(&dev->ipmi_completed_requests);
		if (kreq == NULL) {
			IPMI_UNLOCK(sc);
			return (EAGAIN);
		}
		addr.channel = IPMI_BMC_CHANNEL;
		i_recv.recv_type = IPMI_RESPONSE_RECV_TYPE;
		i_recv.msgid = kreq->ir_msgid;
		i_recv.msg.netfn = IPMI_REPLY_ADDR(kreq->ir_addr) >> 2;
		i_recv.msg.cmd = kreq->ir_command;
		error = kreq->ir_error;
		if (error) {
			list_remove(&dev->ipmi_completed_requests, kreq);
			dev->ipmi_requests--;
			IPMI_UNLOCK(sc);
			ipmi_free_request(kreq);
			return (error);
		}
		len = kreq->ir_replylen + 1;
#ifdef IPMICTL_RECEIVE_MSG_32
		if (i_recv.msg.data_len < len && (cmd == IPMICTL_RECEIVE_MSG ||
		    cmd == IPMICTL_RECEIVE_MSG_32)) {
#else
		if (i_recv.msg.data_len < len && (cmd == IPMICTL_RECEIVE_MSG)) {
#endif
			IPMI_UNLOCK(sc);
			return (EMSGSIZE);
		}
		list_remove(&dev->ipmi_completed_requests, kreq);
		dev->ipmi_requests--;
		IPMI_UNLOCK(sc);
		len = min(i_recv.msg.data_len, len);
		i_recv.msg.data_len = (unsigned short)len;

		error = ddi_copyout(&addr, i_recv.addr, alen, flags);
		if (error == 0)
			error = ddi_copyout(&kreq->ir_compcode, i_recv.msg.data,
			    1, flags);
		if (error == 0)
			error = ddi_copyout(kreq->ir_reply, i_recv.msg.data + 1,
			    len - 1, flags);

		/* Update changed fields in 32-bit structures. */
		switch (cmd) {
#ifdef IPMICTL_SEND_COMMAND_32
		case IPMICTL_RECEIVE_MSG_TRUNC_32:
		case IPMICTL_RECEIVE_MSG_32:
			i_recv32.recv_type = i_recv.recv_type;
			i_recv32.msgid = (int32_t)i_recv.msgid;
			i_recv32.msg.netfn = i_recv.msg.netfn;
			i_recv32.msg.cmd = i_recv.msg.cmd;
			i_recv32.msg.data_len = i_recv.msg.data_len;
			if (error == 0)
				error = ddi_copyout(&i_recv32, (void *)data,
				    sizeof (i_recv32), flags);
			break;
#endif
		default:
			if (error == 0)
				error = ddi_copyout(&i_recv, (void *)data,
				    sizeof (i_recv), flags);
			break;
		}
		ipmi_free_request(kreq);
		break;
	case IPMICTL_SET_MY_ADDRESS_CMD:
		IPMI_LOCK(sc);
		if (ddi_copyin((void *)data, &dev->ipmi_address, sizeof (int),
		    flags) < 0)
			error = EFAULT;
		IPMI_UNLOCK(sc);
		break;
	case IPMICTL_GET_MY_ADDRESS_CMD:
		IPMI_LOCK(sc);
		if (ddi_copyout(&dev->ipmi_address, (void *)data, sizeof (int),
		    flags) < 0)
			error = EFAULT;
		IPMI_UNLOCK(sc);
		break;
	case IPMICTL_SET_MY_LUN_CMD:
		IPMI_LOCK(sc);
		if (ddi_copyin((void *)data, &dev->ipmi_lun, sizeof (int),
		    flags) < 0)
			error = EFAULT;
		dev->ipmi_lun &= 0x3;
		IPMI_UNLOCK(sc);
		break;
	case IPMICTL_GET_MY_LUN_CMD:
		IPMI_LOCK(sc);
		if (ddi_copyout(&dev->ipmi_lun, (void *)data, sizeof (int),
		    flags) < 0)
			error = EFAULT;
		IPMI_UNLOCK(sc);
		break;
	case IPMICTL_SET_GETS_EVENTS_CMD:
		IPMI_LOCK(sc);
		if (ddi_copyin((void *)data, &clonep->ic_getevents,
		    sizeof (int), flags) < 0)
			error = EFAULT;
		IPMI_UNLOCK(sc);
		break;
	case IPMICTL_SET_TIMING_PARMS_CMD:
		IPMI_LOCK(sc);
		if (ddi_copyin((void *)data, &clonep->ic_retrys,
		    sizeof (struct ipmi_timing_parms), flags) < 0)
			error = EFAULT;
		IPMI_UNLOCK(sc);
		break;
	case IPMICTL_GET_TIMING_PARMS_CMD:
		IPMI_LOCK(sc);
		if (ddi_copyout(&clonep->ic_retrys, (void *)data,
		    sizeof (struct ipmi_timing_parms), flags) < 0)
			error = EFAULT;
		IPMI_UNLOCK(sc);
		break;
	case IPMICTL_REGISTER_FOR_CMD:
	case IPMICTL_UNREGISTER_FOR_CMD:
		return (EOPNOTSUPP);
	default:
		device_printf(sc->ipmi_dev, "Unknown IOCTL %lX\n", cmd);
		return (ENOIOCTL);
	}

	return (error);
}

/*
 * Request management.
 */

/* Allocate a new request with request and reply buffers. */
struct ipmi_request *
ipmi_alloc_request(struct ipmi_device *dev, long msgid, uint8_t addr,
    uint8_t command, size_t requestlen, size_t replylen)
{
	struct ipmi_request *req;
	int reqreplen;

	reqreplen = sizeof (struct ipmi_request) + requestlen  + replylen;
	req = kmem_zalloc(reqreplen, KM_SLEEP);
	req->ir_memlen = reqreplen;
	req->ir_owner = dev;
	req->ir_msgid = msgid;
	req->ir_addr = addr;
	req->ir_command = command;
	cv_init(&req->ir_request_cv, "ipmiareq");
	req->ir_retrys.retries = DEFAULT_MSG_RETRY;
	req->ir_retrys.retry_time_ms = DEFAULT_MSG_TIMEOUT;
	if (requestlen) {
		req->ir_request = (uchar_t *)&req[1];
		req->ir_requestlen = requestlen;
	}
	if (replylen) {
		req->ir_reply = (uchar_t *)&req[1] + requestlen;
		req->ir_replybuflen = replylen;
	}
	return (req);
}

/* Free a request no longer in use. */
void
ipmi_free_request(struct ipmi_request *req)
{
	/*
	 * If this request is stuck in the pending queue or is busy
	 * being worked on we can not free it yet. This happens on driver
	 * requests that timed-out. The request will cycle through the
	 * process and get free'd in ipmi_complete_request().
	 */
	if (req->ir_sflag & IR_INPROC)
		return;
	cv_destroy(&req->ir_request_cv);
	kmem_free(req, req->ir_memlen);
}

/* Store a processed request on the appropriate completion queue. */
void
ipmi_complete_request(struct ipmi_softc *sc, struct ipmi_request *req)
{
	struct ipmi_device *dev;

	IPMI_LOCK_ASSERT(sc);
	/*
	 * Anonymous requests (from inside the driver) always have a
	 * waiter that we awaken.
	 */
	if (req->ir_owner == NULL) {
		req->ir_sflag &= ~IR_BUSY;
		if (req->ir_sflag & IR_TIMEO) {
			ipmi_free_request(req);
		} else {
			ipmi_wakeup(&req->ir_request_cv, &sc->ipmi_lock,
			    &req->ir_slflag);
		}
	} else {
		dev = req->ir_owner;
		req->ir_sflag |= IR_COMPQ;
		req->ir_sflag &= ~IR_BUSY;
		list_insert_tail(&dev->ipmi_completed_requests, req);
		if (dev->ipmi_closing)
			ipmi_wakeup(&dev->ipmi_close_cv, &sc->ipmi_lock,
			    &dev->ipmi_slflag);
		else
			selwakeup(&dev->ipmi_select);
	}
}

/* Enqueue an internal driver request and wait until it is completed. */
int
ipmi_submit_driver_request(struct ipmi_softc *sc, struct ipmi_request *req,
    int timo)
{
	int error;

	IPMI_LOCK(sc);
	error = sc->ipmi_enqueue_request(sc, req);
	if (error == 0) {
		req->ir_sflag |= IR_SLEEPQ;
		req->ir_slflag = 1;
		error = ipmi_msleep(&req->ir_request_cv, &sc->ipmi_lock,
		    &req->ir_slflag, timo);
		req->ir_sflag &= ~IR_SLEEPQ;
		if (error == -1) {
			req->ir_sflag |= IR_TIMEO;
			req->ir_compcode = BMC_IPMI_COMMAND_TIMEOUT;
		}
	}
	IPMI_UNLOCK(sc);
	if (error == 0)
		error = req->ir_error;
	return (error);
}

/*
 * Helper routine for polled system interfaces that use
 * ipmi_polled_enqueue_request() to queue requests.  This request
 * waits until there is a pending request and then returns the first
 * request.  If the driver is shutting down, it returns NULL.
 */
struct ipmi_request *
ipmi_dequeue_request(struct ipmi_softc *sc)
{
	struct ipmi_request *req;

	IPMI_LOCK_ASSERT(sc);

	while (!sc->ipmi_detaching) {
		req = list_head(&sc->ipmi_pending_requests);
		if (req != NULL) {
			req->ir_sflag |= IR_BUSY;
			req->ir_sflag &= ~IR_PENDQ;
			list_remove(&sc->ipmi_pending_requests, req);
			return (req);
		}
		cv_wait(&sc->ipmi_request_added, &sc->ipmi_lock);
	}
	return (NULL);
}

/* Default implementation of ipmi_enqueue_request() for polled interfaces. */
int
ipmi_polled_enqueue_request(struct ipmi_softc *sc, struct ipmi_request *req)
{

	IPMI_LOCK_ASSERT(sc);

	list_insert_tail(&sc->ipmi_pending_requests, req);
	req->ir_sflag |= IR_PENDQ;
	cv_signal(&sc->ipmi_request_added);
	return (0);
}

/*
 * Watchdog event handler.
 */

int
ipmi_set_watchdog(struct ipmi_softc *sc, uint32_t sec)
{
	struct ipmi_request *req;
	int error;

	req = ipmi_alloc_driver_request(IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_SET_WDOG, 6, 0);

	if (sec) {
		req->ir_request[0] = IPMI_SET_WD_TIMER_DONT_STOP
		    | IPMI_SET_WD_TIMER_SMS_OS;
		req->ir_request[1] = IPMI_SET_WD_ACTION_RESET;
		req->ir_request[2] = 0;
		req->ir_request[3] = 0;	/* Timer use */
		req->ir_request[4] = (sec * 10) & 0xff; /* LSB 1 = .1sec */
		req->ir_request[5] = (sec * 10) >> 8;	/* MSB */
	} else {
		req->ir_request[0] = IPMI_SET_WD_TIMER_SMS_OS;
		req->ir_request[1] = 0;
		req->ir_request[2] = 0;
		req->ir_request[3] = 0;	/* Timer use */
		req->ir_request[4] = 0;
		req->ir_request[5] = 0;
	}

	error = ipmi_submit_driver_request(sc, req, 0);
	if (error)
		device_printf(sc->ipmi_dev, "Failed to set watchdog\n");
	else if (sec) {
		ipmi_free_request(req);

		req = ipmi_alloc_driver_request(IPMI_ADDR(IPMI_APP_REQUEST, 0),
		    IPMI_RESET_WDOG, 0, 0);

		error = ipmi_submit_driver_request(sc, req, 0);
		if (error)
			device_printf(sc->ipmi_dev,
			    "Failed to reset watchdog\n");
	}

	ipmi_free_request(req);
	return (error);
}

static void
ipmi_wd_event(void *arg, uint32_t timeo, int *error)
{
	struct ipmi_softc *sc = arg;
	int e;

	if (timeo > 0 && timeo <= 600) { /* 10 min max for now */
		e = ipmi_set_watchdog(sc, timeo);
		if (e == 0)
			*error = 0;
		else
			(void) ipmi_set_watchdog(sc, 0);
	} else {
		e = ipmi_set_watchdog(sc, 0);
		if (e != 0 && timeo == 0)
			*error = EOPNOTSUPP;
	}
}

int
ipmi_startup_io(struct ipmi_softc *sc)
{
	struct ipmi_request *req;
	device_t dev;
	int error, i;

	dev = sc->ipmi_dev;

	if (sc->ipmi_initio)
		return (1);

	/* Send a GET_DEVICE_ID request. */
	req = ipmi_alloc_driver_request(IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_GET_DEVICE_ID, 0, 15);

	error = ipmi_submit_driver_request(sc, req, MAX_TIMEOUT);
	if (error == EWOULDBLOCK) {
		device_printf(dev, "Timed out waiting for GET_DEVICE_ID\n");
		ipmi_free_request(req);
		return (0);
	} else if (error) {
		device_printf(dev, "Failed GET_DEVICE_ID: %d\n", error);
		ipmi_free_request(req);
		return (0);
	} else if (req->ir_compcode != 0) {
		device_printf(dev,
		    "Bad completion code for GET_DEVICE_ID: %d\n",
		    req->ir_compcode);
		ipmi_free_request(req);
		return (0);
	} else if (req->ir_replylen < 5) {
		device_printf(dev, "Short reply for GET_DEVICE_ID: %d\n",
		    req->ir_replylen);
		ipmi_free_request(req);
		return (0);
	}

	device_printf(dev, "IPMI device rev. %d, firmware rev. %d.%d, "
	    "version %d.%d\n",
	    req->ir_reply[1] & 0x0f,
	    req->ir_reply[2] & 0x0f, req->ir_reply[4],
	    req->ir_reply[4] & 0x0f, req->ir_reply[4] >> 4);

	ipmi_free_request(req);

	if (!(sc->ipmi_pi_flags & IPMIPI_NOASYNC)) {
		req = ipmi_alloc_driver_request(IPMI_ADDR(IPMI_APP_REQUEST, 0),
		    IPMI_CLEAR_FLAGS, 1, 0);

		(void) ipmi_submit_driver_request(sc, req, 0);

		/* XXX: Magic numbers */
		if (req->ir_compcode == 0xc0) {
			device_printf(dev, "Clear flags is busy\n");
		}
		if (req->ir_compcode == 0xc1) {
			device_printf(dev, "Clear flags illegal\n");
		}
		ipmi_free_request(req);
	}

	for (i = 0; i < 8; i++) {
		req = ipmi_alloc_driver_request(IPMI_ADDR(IPMI_APP_REQUEST, 0),
		    IPMI_GET_CHANNEL_INFO, 1, 0);
		req->ir_request[0] = (uchar_t)i;

		(void) ipmi_submit_driver_request(sc, req, 0);

		if (req->ir_compcode != 0) {
			ipmi_free_request(req);
			break;
		}
		ipmi_free_request(req);
	}
	device_printf(dev, "Number of channels %d\n", i);

	/*
	 * If the watchdog is enabled by driver properties, try to enable it
	 * in hardware. Note that by default this is enabled and the time for
	 * update (ipmi_watchd_update) is a third of the time-out
	 * (ipmi_watchd_timo). This should allow enough headroom for a very
	 * busy system to get to the update before the timer goes off. But if
	 * te timeout value gets to be too small this may not be the case. So
	 * we may want to limit small timeout values to something reasonable.
	 *
	 * TBD BSH what is a reasonable small limit??
	 */
	if (!(sc->ipmi_pi_flags & IPMIPI_NOASYNC))
		if (ipmi_watchd_timo && ipmi_watchd_update) {
			/* probe for watchdog */
			req = ipmi_alloc_driver_request(
			    IPMI_ADDR(IPMI_APP_REQUEST, 0),
			    IPMI_GET_WDOG, 0, 0);

			(void) ipmi_submit_driver_request(sc, req, 0);

			if (req->ir_compcode == 0x00) {
				device_printf(dev, "Attached watchdog\n");
				/* register the watchdog event handler */
				sc->ipmi_watchdog_tag = EVENTHANDLER_REGISTER(
				    watchdog_list, ipmi_wd_event, sc, 0);
			}
			ipmi_free_request(req);
		}

	sc->ipmi_initio = 1;
	return (1);
}

static void
ipmi_startup(void *arg)
{
	struct ipmi_softc *sc = arg;
	device_t dev;
	int error;

	config_intrhook_disestablish(&sc->ipmi_ich);
	dev = sc->ipmi_dev;

	/* Initialize interface-dependent state. */
	error = sc->ipmi_startup(sc);
	if (error) {
		device_printf(dev, "Failed to initialize interface: %d\n",
		    error);
		return;
	}

	if (!(sc->ipmi_pi_flags & IPMIPI_DELYATT)) {
		if (!ipmi_startup_io(sc))
			return;
	}
	sc->ipmi_cdev = make_dev(&ipmi_cdevsw, device_get_unit(dev),
	    UID_ROOT, GID_OPERATOR, 0660, "ipmi%d", device_get_unit(dev));
	if (sc->ipmi_cdev == NULL) {
		device_printf(dev, "Failed to create cdev\n");
		return;
	}
	sc->ipmi_cdev->si_drv1 = sc;
}

int
ipmi_attach(device_t dev)
{
	struct ipmi_softc *sc = device_get_softc(dev);
#ifndef sun
	int error;
	if (sc->ipmi_irq_res != NULL && sc->ipmi_intr != NULL) {
		error = bus_setup_intr(dev, sc->ipmi_irq_res, INTR_TYPE_MISC,
		    NULL, sc->ipmi_intr, sc, &sc->ipmi_irq);
		if (error) {
			device_printf(dev, "can't set up interrupt\n");
			return (error);
		}
	}
#endif
	bzero(&sc->ipmi_ich, sizeof (struct intr_config_hook));
	sc->ipmi_ich.ich_func = &ipmi_startup;
	sc->ipmi_ich.ich_arg = sc;
	if (config_intrhook_establish(&sc->ipmi_ich) != 0) {
		device_printf(dev, "can't establish configuration hook\n");
		return (ENOMEM);
	}

	ipmi_attached = 1;
	return (0);
}

int
ipmi_detach(device_t dev)
{
	struct ipmi_softc *sc;

	sc = device_get_softc(dev);

	/* Fail if there are any open handles. */
	IPMI_LOCK(sc);
	if (sc->ipmi_opened) {
		IPMI_UNLOCK(sc);
		return (EBUSY);
	}
	IPMI_UNLOCK(sc);
	if (sc->ipmi_cdev)
		destroy_dev(sc->ipmi_cdev);

	/* Detach from watchdog handling and turn off watchdog. */
	if (sc->ipmi_watchdog_tag) {
		EVENTHANDLER_DEREGISTER(watchdog_list, sc->ipmi_watchdog_tag);
		(void) ipmi_set_watchdog(sc, 0);
	}

	/* XXX: should use shutdown callout I think. */
	/* If the backend uses a kthread, shut it down. */
	IPMI_LOCK(sc);
	sc->ipmi_detaching = 1;
	IPMI_UNLOCK(sc);
#ifndef sun
	if (sc->ipmi_irq)
		bus_teardown_intr(dev, sc->ipmi_irq_res, sc->ipmi_irq);
	ipmi_release_resources(dev);
#endif
	return (0);
}

#ifndef sun
void
ipmi_release_resources(device_t dev)
{
	struct ipmi_softc *sc;
	int i;

	sc = device_get_softc(dev);

	if (sc->ipmi_irq)
		bus_teardown_intr(dev, sc->ipmi_irq_res, sc->ipmi_irq);
	if (sc->ipmi_irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, sc->ipmi_irq_rid,
		    sc->ipmi_irq_res);

	for (i = 0; i < MAX_RES; i++)
		if (sc->ipmi_io_res[i])
			bus_release_resource(dev, sc->ipmi_io_type,
			    sc->ipmi_io_rid + i, sc->ipmi_io_res[i]);
}

devclass_t ipmi_devclass;

/* XXX: Why? */
static void
ipmi_unload(void *arg)
{
	device_t *devs;
	int count;
	int i;

	if (devclass_get_devices(ipmi_devclass, &devs, &count) != 0)
		return;
	for (i = 0; i < count; i++)
		device_delete_child(device_get_parent(devs[i]), devs[i]);
	free(devs, M_TEMP);
}
SYSUNINIT(ipmi_unload, SI_SUB_DRIVERS, SI_ORDER_FIRST, ipmi_unload, NULL);
#endif

#ifdef IMPI_DEBUG
static void
dump_buf(uchar_t *data, int len)
{
	char buf[20];
	char line[1024];
	char temp[30];
	int count = 0;
	int i = 0;

	printf("Address %p len %d\n", data, len);
	if (len > 256)
		len = 256;
	line[0] = '\000';
	for (; len > 0; len--, data++) {
		sprintf(temp, "%02x ", *data);
		strcat(line, temp);
		if (*data >= ' ' && *data <= '~')
			buf[count] = *data;
		else if (*data >= 'A' && *data <= 'Z')
			buf[count] = *data;
		else
			buf[count] = '.';
		if (++count == 16) {
			buf[count] = '\000';
			count = 0;
			printf("  %3x  %s %s\n", i, line, buf);
			i += 16;
			line[0] = '\000';
		}
	}
	buf[count] = '\000';

	for (; count != 16; count++) {
		strcat(line, "   ");
	}
	printf("  %3x  %s %s\n", i, line, buf);
}
#endif
