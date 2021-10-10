/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */


/*
 * IPMI: front end to IPMI access
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
#include <sys/smbios.h>
#include <sys/conf.h>
#include <sys/sysmacros.h>
#include <sys/atomic.h>

#include "ipmi_sol_int.h"

/*
 * The KCS (Keyboard Controller Style) interface is used to
 * send command request messages to, and receive command response
 * messages from, the Baseboard Management Controller (BMC).
 *
 * Messages are transferred over the interface using a per-byte
 * handshake. Two I/O mapped registers are used, the Control and
 * Status Register and the Data Out/Data In Register
 *
 * The interface is implemented by two state machines: one for
 * writing a message to the BMC, kcs_write(); and one for reading
 * a message from the BMC, kcs_read().
 *
 * The state machines are defined in the IPMI version 1.5
 * Specification, Chapter 9.
 */

/*
 * interface state bits
 */
#define	KCS_ERR		0xc0		/* error state */
#define	KCS_WR		0x80		/* write state */
#define	KCS_RD		0x40		/* read state */
#define	KCS_IDL		0x00		/* IDLE=>state bits zero */
#define	KCS_ATN		0x04		/* Attention required */
#define	KCS_IBF		0x02		/* KCS input buffer full */
#define	KCS_OBF		0x01		/* KCS output buffer full */

#define	KCS_STATEMASK	0xc0		/* highest 2 bits are state */
#define	KCS_STATUSMASK	0x07		/* OBF, IBF, ATN */

#define	KCS_STATEBITS(csr)	((csr) & KCS_STATEMASK)
#define	KCS_STATUSBITS(csr)	((csr) & KCS_STATUSMASK)

#define	KCS_IDLE(csr)		(KCS_STATEBITS(csr) == KCS_IDL)
#define	KCS_READ(csr)		(KCS_STATEBITS(csr) == KCS_RD)
#define	KCS_WRITE(csr)		(KCS_STATEBITS(csr) == KCS_WR)
#define	KCS_ERROR(csr)		(KCS_STATEBITS(csr) == KCS_ERR)
#define	KCS_DAVAIL(csr)		(((csr) & KCS_OBF) == KCS_OBF)
#define	KCS_DPEND(csr)		(((csr) & KCS_IBF) == KCS_IBF)
#define	KCS_ATTN(csr)		(((csr) & KCS_ATN) == KCS_ATN)

#define	TIMEDOUT(dev) ((dev)->timedout)
#define	BMC_RESET_TIMEOUT (1 * 1000 * 1000)	/* 1 second */

/*
 * Default Data limits for the KCS interface
 */
#define	BMC_KCS_MAX_REQUEST_SIZE	257 /* 257 to allow the payload */
					    /* to be the full 255 bytes */
#define	KCS_SEND_MAX_PAYLOAD_SIZE	(BMC_KCS_MAX_REQUEST_SIZE - 2)
#define	BMC_KCS_MAX_RESPONSE_SIZE	258 /* Max-out receive payload */
#define	KCS_RECV_MAX_PAYLOAD_SIZE	(BMC_KCS_MAX_RESPONSE_SIZE - 3)

/*
 * IPMI Request and response data overhead
 */
#define	IPMI_SOVERHD 2		/* IPIM request overhad bytes */
#define	IPMI_ROVERHD 3		/* IPMI response overhead bytes */

/*
 * Offsets to IPMI overhead bytes in request and respnse data stream
 */
#define	IPMI_ADDROFFSET 0
#define	IPMI_CMNDOFFSET 1
#define	IPMI_FDATOFFSET 2

/*
 * Define KCS Interface timeouts
 */
#define	BMC_MAX_RETRY_CNT	10

/*
 * System controller Control codes
 */
#define	KCS_ABORT	0x60
#define	WRITE_START	0x61
#define	WRITE_END	0x62
#define	READ_START	0x68

/*
 * KCS registers
 */
#define	DEFAULT_KCS_BASE	0xca2


#define	BMC_SUCCESS		0x0
#define	BMC_FAILURE		0x1
#define	BMC_ABORT		0x2
#define	BMC_TIMEOUT		0x3
#define	BMC_SIG			0x4
#define	BMC_LOGERR		0x5

static int warn_toomuch_data = 1;

/*
 * Tunables
 */

/*
 * Some KCS interfaces can handle a massive amount of request data, e.g.
 * when they're used to transfer flash images to the system service processor.
 * To enable faster bulk-data transfer on such BMCs, allow the max send payload
 * to be dynamically tunable.
 */
int kcs_max_send_payload = KCS_SEND_MAX_PAYLOAD_SIZE;

int ipmi_kcs_debug = 0;

/*
 * Private Data Structures
 */

/*
 * data structure to send a message to BMC.
 * Ref. IPMI Spec 9.2
 */
typedef struct bmc_kcs_send {
	uint8_t fnlun;		/* Network Function and LUN */
	uint8_t cmd;		/* command */
	uint8_t data[1];	/* Variable-length, see kcs_max_send_payload */
} bmc_kcs_send_t;

/*
 * data structure to receive a message from BMC.
 * Ref. IPMI Spec 9.3
 */
typedef struct bmc_kcs_recv {
	uint8_t fnlun;		/* Network Function and LUN */
	uint8_t cmd;		/* command */
	uint8_t ccode;		/* completion code */
	uint8_t data[KCS_RECV_MAX_PAYLOAD_SIZE];
} bmc_kcs_recv_t;

static int ipmi_kcs_probe(dev_info_t *, int, int);
static int ipmi_kcs_attach(void *);
static int ipmi_kcs_detach(void *);
static int ipmi_kcs_resume(void *);
static int ipmi_kcs_suspend(void *);
static int ipmi_kcs_poll(void *);

/*ARGSUSED*/
/*PRINTFLIKE2*/
static void
dprintf(int d, const char *format, ...)
{
#ifdef DEBUG
	if (d <= ipmi_kcs_debug) {
		va_list ap;
		va_start(ap, format);
		vcmn_err(d < BMC_DEBUG_LEVEL_2 ? CE_WARN : CE_CONT, format, ap);
		va_end(ap);
	}
#endif
}


static void
kcs_initregs(ipmi_dev_t *devp, struct ipmi_get_info *info)
{
	devp->kcs_base = info->address;
	devp->kcs_cmd_reg = devp->kcs_base + info->offset;
	devp->kcs_status_reg = devp->kcs_base + info->offset;
	devp->kcs_in_reg = devp->kcs_base;
	devp->kcs_out_reg = devp->kcs_base;

	dprintf(BMC_DEBUG_LEVEL_3,
	    "cmd 0x%x status 0x%x datain 0x%x dataout 0x%x",
	    devp->kcs_cmd_reg,
	    devp->kcs_status_reg,
	    devp->kcs_in_reg,
	    devp->kcs_out_reg);
}


/*
 * Write a single data byte to the BMC Data Out/Data In register
 */
static inline void
kcs_put(ipmi_dev_t *devp, uint8_t byte)
{
	ipmi_kstat_t *ksp = devp->kstatsp;

	outb(devp->kcs_in_reg, byte);
	atomic_inc_64(&ksp->ipmi_bytes_out.value.ui64);
}

/*
 * Read a single data byte from the BMC Data Out/Data In register
 */
static uint8_t
kcs_get(ipmi_dev_t *devp)
{
	ipmi_kstat_t *ksp = devp->kstatsp;
	uint8_t b = inb(devp->kcs_out_reg);

	atomic_inc_64(&ksp->ipmi_bytes_in.value.ui64);
	return (b);
}

/*
 * Read the BMC's current status.
 */
static inline uint8_t
kcs_status(ipmi_dev_t *devp)
{
	return (inb(devp->kcs_status_reg));
}

/*
 * Wait for IBF to clear, and return whether it did.
 *
 * The KCS_IBF status bit in the Control Status Register
 * is set to 1 when a byte has been written
 * to the Command or Data In registers, and cleared by
 * the BMC when it has accepted the byte.
 */
static boolean_t
kcs_data_pending(ipmi_dev_t *devp, int *bmc_gonep, boolean_t *aborted)
{
	uint8_t	csr;

	csr = kcs_status(devp);

	/*
	 * If the BMC has "gone away" (e.g. it has reset or is
	 * in the process of booting), I/O to the BMC's port will
	 * return 0xFF.  This value can be used as a sentinel for
	 * detecting a BMC that has disappeared because some bits
	 * are reserved for "OEM use" (which, on all known BMCs,
	 * are 0).  Note that if the top 2 MSBs are set, that's an
	 * error indication anyway, and we'll end up attempting a
	 * reset on the next operation if that operation fails.
	 */
	if (csr == 0xFF) {
		*bmc_gonep = 1;
		return (B_FALSE);
	}

	while (KCS_DPEND(csr)) {
		csr = kcs_status(devp);

		if (csr == 0xFF || *aborted == B_TRUE) {
			*bmc_gonep = 1;
			return (B_FALSE);
		}

		if (TIMEDOUT(devp)) {
			dprintf(BMC_DEBUG_LEVEL_3,
			    "kcs_data_pending KCS_DPEND failed: CSR=0x%x",
			    (uint_t)csr);
			return (B_TRUE);
		}
	}

	if (KCS_DPEND(csr)) {
		dprintf(BMC_DEBUG_LEVEL_3,
		    "kcs_data_pending KCS_DPEND failed: CSR=0x%x", (uint_t)csr);
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * KCS command op: WRITE_START | WRITE_END | READ_START | KCS_ABORT
 */
static boolean_t
kcs_command(ipmi_dev_t *devp, uint8_t cmd, int *bmc_gonep, boolean_t *aborted)
{
	outb(devp->kcs_cmd_reg, cmd);

	/* Return whether command was accepted (eventually) by the BMC */
	return (!kcs_data_pending(devp, bmc_gonep, aborted));
}

/*
 * Wait until KCS reaches WRITE state.
 *
 * Once a command transfer to the BMC has been started,
 * the KCS Write State Machine should go to the write
 * state (KCS_WR bit set in the BMC Command/Status register).
 */
static boolean_t
kcs_writeable(ipmi_dev_t *devp, int *bmc_gonep, boolean_t *aborted)
{
	uint8_t	csr;

	csr = kcs_status(devp);

	if (csr == 0xFF) {	/* see comment in kcs_data_pending() */
		*bmc_gonep = 1;
		return (B_FALSE);
	}

	while (!KCS_WRITE(csr)) {
		csr = kcs_status(devp);

		if (csr == 0xFF || *aborted == B_TRUE) {
			*bmc_gonep = 1;
			return (B_FALSE);
		}

		if (TIMEDOUT(devp)) {
			dprintf(BMC_DEBUG_LEVEL_3,
			    "kcs_writeable: timed out [csr=0x%x]",
			    (uint_t)csr);
			return (B_FALSE);
		}
	}

	return (KCS_WRITE(csr));
}

/*
 * Wait while transfer pending. When a data byte
 * is written to the BMC, the KCS_OBF bit will be
 * set. Before the next write, this bit should be
 * cleared by reading a dummy byte from the BMC.
 */
static boolean_t
kcs_transfer_pending(ipmi_dev_t *devp, int *bmc_gonep, boolean_t *aborted)
{
	uint8_t	csr;

	/*
	 * Dummy read of data byte to clear OBF
	 */
	csr = kcs_status(devp);

	if (csr == 0xFF) { /* see comment in kcs_data_pending() */
		*bmc_gonep = 1;
		return (B_FALSE);
	}

	while (KCS_DAVAIL(csr)) {
		(void) kcs_get(devp);
		csr = kcs_status(devp);

		if (csr == 0xFF || *aborted == B_TRUE) {
			*bmc_gonep = 1;
			return (B_FALSE);
		}

		if (TIMEDOUT(devp)) {
			dprintf(BMC_DEBUG_LEVEL_3,
			    "kcs_transfer_pending: timed out [csr=0x%x]",
			    (uint_t)csr);
			return (B_TRUE);
		}
	}

	return (KCS_DAVAIL(csr));
}

/*
 * Wait for OBF bit in BMC Command/Status register to set
 * During a read transfer, this will indicate the next
 * data byte is available
 */
static boolean_t
kcs_data_available(ipmi_dev_t *devp, int *bmc_gonep, boolean_t *aborted)
{
	uint8_t csr;

	csr = kcs_status(devp);

	if (csr == 0xFF) { /* see comment in kcs_data_pending() */
		*bmc_gonep = 1;
		return (B_FALSE);
	}

	while (!KCS_DAVAIL(csr)) {
		csr = kcs_status(devp);

		if (csr == 0xFF || *aborted == B_TRUE) {
			*bmc_gonep = 1;
			return (B_FALSE);
		}

		if (TIMEDOUT(devp)) {
			dprintf(BMC_DEBUG_LEVEL_3,
			    "kcs_data_available: timed out [csr=0x%x]",
			    (uint_t)csr);
			return (B_FALSE);
		}
	}

	return (KCS_DAVAIL(csr));
}

static void
kcs_timer_handler(void *devp)
{
	ipmi_dev_t *dev = (ipmi_dev_t *)devp;

	dev->timedout = B_TRUE;
}


/*
 * Implementation of the 'KCS write message' state machine
 *
 * Write Processing Summary
 * ------------------------
 * - Issue a WRITE_START Command to the BMC to start the message
 *   transfer
 * - Write all data bytes except the final byte
 * - Issue a WRITE_END Command
 * - Write the last data byte to complete the message transfer
 *
 * - Bytes are written to the BMC using the handshake:
 *   - Wait for the previous byte written to the BMC to be
 *     accepted (!kcs_data_pending) - i.e. wait for IBF
 *     in the Command/Status Register (KCS_DPEND) to be cleared
 *   - Clear the  OBF bit in the Command/Status Register by
 *     reading a dummy byte from the BMC (!kcs_transfer_pending)
 *   - Once the command transfer is in progress, check that
 *     the BMC is in the write state (kcs_writeable)
 *   - Write the data byte to the Data Out/Data In register
 *     (kcs_put)
 *
 */
static int
kcs_write(ipmi_dev_t *devp, struct ipmi_request *req, uint32_t pktlen,
    int *bmc_gonep, boolean_t *aborted)
{
	int		ret = BMC_FAILURE;
	uint8_t		*send_buf;
	uint_t	i = 0;
	enum {
		BMC_TRANSFER_INIT,
		BMC_TRANSFER_START,
		BMC_TRANSFER_NEXT,
		BMC_TRANSFER_LAST,
		BMC_TRANSFER_STOP
	} state = BMC_TRANSFER_INIT;

	dprintf(BMC_DEBUG_LEVEL_3, "KCS Len 0x%x [[START]]", pktlen);

	while (state != BMC_TRANSFER_STOP && !TIMEDOUT(devp)) {

		if (*aborted == B_TRUE) {
			*bmc_gonep = 1;
			return (BMC_ABORT);
		}

		switch (state) {

		case  BMC_TRANSFER_INIT:
			if (kcs_data_pending(devp, bmc_gonep, aborted) ||
			    kcs_transfer_pending(devp, bmc_gonep, aborted)) {
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (BMC_ABORT);

			state = BMC_TRANSFER_START;
			break;

		case  BMC_TRANSFER_START:
			state = BMC_TRANSFER_NEXT;

			if (!kcs_command(devp, WRITE_START, bmc_gonep,
			    aborted)) {

				if (*bmc_gonep != 0)
					return (BMC_ABORT);

				dprintf(BMC_DEBUG_LEVEL_3,
				    "xfer_start write_start failed");
				state = BMC_TRANSFER_STOP;
				break;
			}
			if (!kcs_writeable(devp, bmc_gonep, aborted)) {

				if (*bmc_gonep != 0)
					return (BMC_ABORT);

				dprintf(BMC_DEBUG_LEVEL_3,
				    "not in WRITE mode in xfer_start");
				state = BMC_TRANSFER_STOP;
				break;
			}
			if (kcs_transfer_pending(devp, bmc_gonep, aborted)) {
				if (*bmc_gonep != 0)
					return (BMC_ABORT);

				dprintf(BMC_DEBUG_LEVEL_3,
				    "xfer_start clear OBF failed");
				state = BMC_TRANSFER_STOP;
				break;
			}
			break;

		case  BMC_TRANSFER_NEXT:
			switch (i) {
			case IPMI_ADDROFFSET:
				kcs_put(devp, req->ir_addr);
				dprintf(BMC_DEBUG_LEVEL_3, "write: 0x%x addr",
				    req->ir_addr);
				break;
			case IPMI_CMNDOFFSET:
				kcs_put(devp, req->ir_command);
				dprintf(BMC_DEBUG_LEVEL_3, "write: 0x%x cmd",
				    req->ir_command);
				send_buf = req->ir_request;
				break;
			default:
				kcs_put(devp, send_buf[i-IPMI_SOVERHD]);
				dprintf(BMC_DEBUG_LEVEL_3, "write: 0x%x i = %d",
				    send_buf[i-IPMI_SOVERHD], i-IPMI_SOVERHD);
				break;
			}
			i++;

			if (kcs_data_pending(devp, bmc_gonep, aborted)) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "write not pending failed");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (BMC_ABORT);

			if (!kcs_writeable(devp, bmc_gonep, aborted)) {

				if (*bmc_gonep != 0)
					return (BMC_ABORT);

				dprintf(BMC_DEBUG_LEVEL_3,
				    "not in WRITE mode in xfer_next");
				state = BMC_TRANSFER_STOP;
				break;
			}
			if (kcs_transfer_pending(devp, bmc_gonep, aborted)) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "write clear OBF failed");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (BMC_ABORT);

			if (i == (pktlen - 1)) {
				state = BMC_TRANSFER_LAST;
			}
			break;

		case  BMC_TRANSFER_LAST:
			(void) kcs_command(devp, WRITE_END, bmc_gonep, aborted);

			if (*bmc_gonep != 0)
				return (BMC_ABORT);

			if (kcs_data_pending(devp, bmc_gonep, aborted)) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "xfer_last not pending failed");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (BMC_ABORT);

			if (!kcs_writeable(devp, bmc_gonep, aborted)) {

				if (*bmc_gonep != 0)
					return (BMC_ABORT);

				dprintf(BMC_DEBUG_LEVEL_3,
				    "not in WRITE mode in xfer_start");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (kcs_transfer_pending(devp, bmc_gonep, aborted)) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "xfer_last clear_OBF failed");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (BMC_ABORT);

			switch (i) {
			case IPMI_ADDROFFSET:
				/*
				 * This should never happen! Since we always
				 * need to send an address and a commend.
				 * If we get here its a bad request from
				 * user land.
				 */
				kcs_put(devp, req->ir_addr);
				cmn_err(CE_WARN, "%s write last byte short pkg",
				    devp->if_name);
				break;
			case IPMI_CMNDOFFSET:
				/*
				 * If we are here it means we have a request
				 * that only contains an address and command
				 * with no additional payload bytes. This is
				 * ok and is the shortest request exceptable.
				 */
				kcs_put(devp, req->ir_command);
				dprintf(BMC_DEBUG_LEVEL_3, "write: 0x%x cmd",
				    req->ir_command);
				send_buf = req->ir_request;
				break;
			default:
				kcs_put(devp,
				    send_buf[i-IPMI_SOVERHD]);
				dprintf(BMC_DEBUG_LEVEL_3, "write: 0x%x i = %d",
				    send_buf[i-IPMI_SOVERHD], i-IPMI_SOVERHD);
				break;
			}
			i++;

			if (kcs_data_pending(devp, bmc_gonep, aborted)) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "xfer_last not pending");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (BMC_ABORT);

			/* If we got here we succeeded! */
			ret = BMC_SUCCESS;

			state = BMC_TRANSFER_STOP;
			break;
		}
	}

	dprintf(BMC_DEBUG_LEVEL_3, "KCS [[END]]");

	return (ret);
}


/*
 * read a response to a request posted from the kcs_write operation.
 */
static int
kcs_read(ipmi_dev_t *devp, struct ipmi_request *req, int *bmc_gonep,
    boolean_t *aborted)
{
	int		ret = BMC_FAILURE;
	uint8_t		csr, data;
	uint_t		i = 0;
	uint_t		replylen = 0;
	enum {
		BMC_RECEIVE_INIT,
		BMC_RECEIVE_START,
		BMC_RECEIVE_NEXT,
		BMC_RECEIVE_NEXT_AGAIN,
		BMC_RECEIVE_END,
		BMC_RECEIVE_STOP
	} state = BMC_RECEIVE_START;

	dprintf(BMC_DEBUG_LEVEL_3, "KCS Len 0x%x [[START]]",
	    (int)(req->ir_replybuflen + IPMI_ROVERHD));

	while (state != BMC_RECEIVE_STOP && !TIMEDOUT(devp)) {

		if (*aborted == B_TRUE) {
			*bmc_gonep = 1;
			return (BMC_ABORT);
		}

		switch (state) {

		case BMC_RECEIVE_START:
			csr = kcs_status(devp);

			/* see comment in kcs_data_pending() */
			if (csr == 0xFF) {
				*bmc_gonep = 1;
				return (BMC_ABORT);
			}

			switch (KCS_STATEBITS(csr)) {

			case KCS_WR:
			case KCS_IDL:
				break;

			case KCS_RD:
				state = BMC_RECEIVE_INIT;
				break;

			case KCS_ERR:
				state = BMC_RECEIVE_STOP;
				break;
			}
			break;

		case BMC_RECEIVE_INIT:
			csr = kcs_status(devp);

			/* see comment in kcs_data_pending() */
			if (csr == 0xFF) {
				*bmc_gonep = 1;
				return (BMC_ABORT);
			}

			switch (KCS_STATEBITS(csr)) {

			case KCS_ERR:
			case KCS_WR:
				state = BMC_RECEIVE_STOP;
				break;

			case KCS_IDL:
				state = BMC_RECEIVE_END;
				break;

			case KCS_RD:
				csr = kcs_status(devp);

				/* see comment in kcs_data_pending() */
				if (csr == 0xFF) {
					*bmc_gonep = 1;
					return (BMC_ABORT);
				}

				if (csr & KCS_OBF) {
					state = BMC_RECEIVE_NEXT;
				}
				break;
			}
			break;

		case BMC_RECEIVE_NEXT:
			/*
			 * If we have read more than the maximum response size,
			 * continue to consume the response, but print a warning
			 * so the user knows that they'll be missing data.
			 */
			if (i >= BMC_KCS_MAX_RESPONSE_SIZE) {
				/* Read the next byte and throw it away */
				unsigned nextbyte = kcs_get(devp);

				if (warn_toomuch_data) {
					cmn_err(CE_NOTE, "KCS response buffers "
					    "are too small to accommodate BMC "
					    "responses.  Some information "
					    "from the BMC will be lost.");
					warn_toomuch_data = 0;
				}

				dprintf(BMC_DEBUG_LEVEL_3,
				    "too much data from KCS: byte #%d, "
				    "(limit was %d bytes), "
				    "data byte=0x%x",
				    i, (int)req->ir_replybuflen, nextbyte);
			} else {
				switch (i) {
				case IPMI_ADDROFFSET:
					data = kcs_get(devp);
					if (data !=
					    IPMI_REPLY_ADDR(req->ir_addr)) {
						cmn_err(CE_NOTE, "KCS Reply"
						    "address mismatch\n");
						state = BMC_RECEIVE_STOP;
						return (ret);
					}
					break;
				case IPMI_CMNDOFFSET:
					data = kcs_get(devp);
					if (data != req->ir_command) {
						cmn_err(CE_NOTE, "KCS Reply"
						    "Command mismatch\n");
						state = BMC_RECEIVE_STOP;
						return (ret);
					}
					break;
				case IPMI_FDATOFFSET:
					req->ir_compcode = kcs_get(devp);
					dprintf(BMC_DEBUG_LEVEL_3,
					    "[%d] rec: 0x%x completion",
					    i, req->ir_compcode);
					break;
				default:
					data = kcs_get(devp);
					if (i < req->ir_replybuflen) {
						req->ir_reply[replylen] = data;
						dprintf(BMC_DEBUG_LEVEL_3,
						    "[%d] rec: 0x%x",
						    replylen,
						    req->ir_reply[replylen]);
					} else {
						dprintf(BMC_DEBUG_LEVEL_3,
						    "KCS: Read short %02x"
						    "byte %d\n",
						    data, replylen);
					}
					replylen++;
					break;
				}
				i++;
			}

			kcs_put(devp, READ_START);

			if (kcs_data_pending(devp, bmc_gonep, aborted)) {
				state = BMC_RECEIVE_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (BMC_ABORT);

			state = BMC_RECEIVE_NEXT_AGAIN;
			break;

		case  BMC_RECEIVE_NEXT_AGAIN:
			/*
			 * READ_STATE?
			 */
			csr = kcs_status(devp);

			/* see comment in kcs_data_pending() */
			if (csr == 0xFF) {
				*bmc_gonep = 1;
				return (BMC_ABORT);
			}

			switch (KCS_STATEBITS(csr)) {

			case KCS_RD:
				if (kcs_data_available(devp, bmc_gonep,
				    aborted)) {
					state = BMC_RECEIVE_NEXT;
				} else {
					if (*bmc_gonep != 0)
						return (BMC_ABORT);

					state = BMC_RECEIVE_STOP;
				}
				break;

			case KCS_ERR:
			case KCS_WR:
				state = BMC_RECEIVE_STOP;
				break;

			case KCS_IDL:
				if (kcs_data_available(devp, bmc_gonep,
				    aborted)) {
					/* read dummy data */
					(void) kcs_get(devp);
					state = BMC_RECEIVE_END;
				} else {
					if (*bmc_gonep != 0)
						return (BMC_ABORT);
					state = BMC_RECEIVE_STOP;
				}
				break;
			}

			break;

		case  BMC_RECEIVE_END:
			/* If we got here we succeeded! */
			ret = BMC_SUCCESS;
			req->ir_replylen = replylen;

			state = BMC_RECEIVE_STOP;
			break;
		} /* switch */
	}

	dprintf(BMC_DEBUG_LEVEL_3, "KCS Len 0x%x [[END]]", replylen);

	return (ret);
}

static void
reset_timeout_handler(void *arg)
{
	*(boolean_t *)arg = B_TRUE;
}

/*ARGSUSED*/
static void
kcs_reset_bmc(ipmi_dev_t *devp, int *bmc_gonep, boolean_t  *aborted)
{
	uint8_t	csr = kcs_status(devp);
	volatile boolean_t reset_timed_out = B_FALSE;
	timeout_id_t reset_to;

	/* see comment in kcs_data_pending() */
	if (csr == 0xFF) {
		*bmc_gonep = 1;
		return;
	}

	reset_to = realtime_timeout(reset_timeout_handler,
	    (void *)&reset_timed_out, drv_usectohz(BMC_RESET_TIMEOUT));

	/*
	 * Try to wait for IBF to clear, but don't worry if it doesn't
	 * Note that we cannot use kcs_data_pending here because our caller
	 * may have timed out, and kcs_data_pending checks the global timeout
	 * flag and will not wait at all if we've already timed out.
	 */
	while (KCS_DPEND(csr) && !reset_timed_out) {
		csr = kcs_status(devp);
		if (csr == 0xFF || *aborted == B_TRUE) {
			(void) untimeout(reset_to);
			*bmc_gonep = 1;
			return;
		}
	}
	(void) untimeout(reset_to);

	/*
	 * Write an ABORT to the command register regardless of IBF.
	 * don't use kcs_command() because it will also wait for the
	 * KCS_DPEND flag to clear, and if we timed out above, we're in
	 * a last-ditch attempt to perform the abort.
	 */
	outb(devp->kcs_cmd_reg, KCS_ABORT);
	dprintf(BMC_DEBUG_LEVEL_3, "KCS RESET");
}


static int
do_kcs2bmc(ipmi_state_t *statep, struct ipmi_request *req,
    boolean_t interruptable)
{
	clock_t		clticks;
	uint_t		retrycount = 2;
	uint32_t	spktsz;
	int		ret = BMC_FAILURE;
	int		bmc_gone = 0;
	ipmi_dev_t	*dev = statep->is_dev_ext;
	boolean_t	*aborted = &statep->is_task_abort;

	if (req->ir_requestlen > kcs_max_send_payload) {
		req->ir_compcode = BMC_IPMI_DATA_LENGTH_EXCEED;
		req->ir_error = EINVAL;
		dprintf(BMC_DEBUG_LEVEL_3, "failed send sz: %d",
		    (int)req->ir_requestlen);
		return (BMC_LOGERR);
	}

	mutex_enter(&dev->if_mutex);
	while (dev->if_busy) {
		if (*aborted == B_TRUE) {
			mutex_exit(&dev->if_mutex);
			req->ir_error = EINTR;
			dprintf(BMC_DEBUG_LEVEL_3, "do_kcs2bmc task aborted");
			return (BMC_ABORT);
		}

		if (cv_wait_sig(&dev->if_cv, &dev->if_mutex) == 0 &&
		    interruptable) {
			mutex_exit(&dev->if_mutex);
			req->ir_error = EINTR;
			return (BMC_SIG);
		}
	}

	dev->if_busy = B_TRUE;
	mutex_exit(&dev->if_mutex);

	clticks = drv_usectohz(MSEC2USEC(req->ir_retrys.retry_time_ms));

	dev->timedout = B_FALSE;
	dev->timer_handle = realtime_timeout(kcs_timer_handler, dev, clticks);

	dprintf(BMC_DEBUG_LEVEL_4,
	    "addr 0x%02x  cmd 0x%02x reqlen 0x%x repylen 0x%x",
	    req->ir_addr, req->ir_command, (int)req->ir_requestlen,
	    req->ir_replylen);

	spktsz = req->ir_requestlen + IPMI_SOVERHD;

	while (retrycount-- != 0) {

		if (*aborted == B_TRUE) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_kcs2bmc aborted");
			req->ir_error = EINTR;
			/* req->ir_compcode = BMC_IPMI_COMMAND_TIMEOUT; */
			ret = BMC_ABORT;
			break;
		}

		if (TIMEDOUT(dev)) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_kcs2bmc timed out");
			ret = BMC_TIMEOUT;
			break;
		}

		if (kcs_write(dev, req, spktsz, &bmc_gone, aborted)
		    == BMC_FAILURE) {
			/* If we had a write failure, try to reset the BMC */
			kcs_reset_bmc(dev, &bmc_gone, aborted);
			dprintf(BMC_DEBUG_LEVEL_3, "send BMC failed");
			continue;
		} else if (bmc_gone) {
			break;
		}

		if (TIMEDOUT(dev)) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_kcs2bmc timed out");
			ret = BMC_TIMEOUT;
			break;
		}

		if (kcs_read(dev, req, &bmc_gone, aborted)
		    == BMC_FAILURE) {
			/* If we had a read failure, try to reset the BMC */
			kcs_reset_bmc(dev, &bmc_gone, aborted);
			dprintf(BMC_DEBUG_LEVEL_3, "recv BMC failed");
			/* req->ir_compcode = BMC_IPMI_COMMAND_TIMEOUT; */
			continue;

		} else if (bmc_gone) {
			break;
		}

		if (TIMEDOUT(dev)) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_kcs2bmc timed out");
			ret = BMC_TIMEOUT;
			break;
		}

		req->ir_error = 0;
		ret = BMC_SUCCESS;
		break;
	}

	(void) untimeout(dev->timer_handle);
	dev->timer_handle = 0;

	if (TIMEDOUT(dev) || bmc_gone) {
		if (TIMEDOUT(dev))
			req->ir_compcode = BMC_IPMI_COMMAND_TIMEOUT;
		bmc_gone = 0;
		kcs_reset_bmc(dev, &bmc_gone, aborted);
		if (TIMEDOUT(dev))
			ret = BMC_TIMEOUT;
		else
			ret = BMC_FAILURE;
	}

	mutex_enter(&dev->if_mutex);
	ASSERT(dev->if_busy == B_TRUE);
	dev->if_busy = B_FALSE;
	cv_signal(&dev->if_cv);
	mutex_exit(&dev->if_mutex);

	return (ret);
}


/* ======================================================================= */
/*		Template for driver plugin conversions			   */
/* ======================================================================= */

/*
 * Interface stuff for new IPMI driver
 */

/*
 * Normally there are one of these per-interface instance.
 * We place this in the array of pi instances for connection
 * to the main driver ipmi_pi[]. When the plugins are turned
 * into a separate module this will be passed to a function
 * ipmi_sol_pi_register(struct ipmi_plugin *).
 */
struct ipmi_plugin ipmi_kcs = {
	.ipmi_pi_probe = ipmi_kcs_probe,
	.ipmi_pi_attach = ipmi_kcs_attach,
	.ipmi_pi_detach = ipmi_kcs_detach,
	.ipmi_pi_suspend = ipmi_kcs_suspend,
	.ipmi_pi_resume = ipmi_kcs_resume,
	.ipmi_pi_pollstatus = ipmi_kcs_poll,
	.ipmi_pi_name = "ipmikcs",
	.ipmi_pi_intfinst = 1,
	.ipmi_pi_flags = (IPMIPI_POLLED|IPMIPI_PEVENT|IPMIPI_SUSRES),
};



static void
kcs_loop(void *arg)
{
	ipmi_state_t		*statep = arg;
	ipmi_dev_t		*devp = statep->is_dev_ext;
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
			ret = do_kcs2bmc(statep, req, B_TRUE);
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

/*
 * Probe helper for KCS. On Solaris we use smbios to indicate
 * a KCS interface.
 */
/*
 * Return the SMBIOS IPMI table entry info to the caller.  If we haven't
 * searched the IPMI table yet, search it.  Otherwise, return a cached
 * copy of the data.
 */
int
ipmi_smbios_identify(struct ipmi_get_info *info)
{
	smbios_ipmi_t ip;

	if (ksmbios == NULL || smbios_info_ipmi(ksmbios, &ip) == SMB_ERR) {
		return (0);
	}

	switch (ip.smbip_type) {
	case SMB_IPMI_T_KCS:
		info->iface_type = KCS_MODE;
		/* 0xca2 */
		info->address = (uint32_t)ip.smbip_addr;
		info->io_mode = 1;
		/* 1 */
		info->offset = ip.smbip_regspacing;
		break;
	case SMB_IPMI_T_SMIC:
		info->iface_type = SMIC_MODE;
		info->address = 0xca9;
		info->io_mode = 1;
		info->offset = 1;
		break;
	case SMB_IPMI_T_BT:
		info->iface_type = BT_MODE;
		info->address = 0xe4;
		info->io_mode = 1;
		info->offset = 1;
		break;
	default:
		return (0);
	}
	return (1);
}

/* ARGSUSED */
static int
ipmi_kcs_suspend(void *ipmi_state)
{
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
ipmi_kcs_resume(void *ipmi_state)
{
	return (DDI_SUCCESS);
}

/* ARGSUSED */
static int
ipmi_kcs_probe(dev_info_t *dip, int ipmi_instance, int kcs_instance)
{
	struct ipmi_get_info info;

	if (!ipmi_smbios_identify(&info)) {
		return (0);
	}
	if (info.iface_type != KCS_MODE) {
		return (0);
	}
	return (1);
}

static int
ipmi_kcs_poll(void *arg)
{
	ipmi_state_t	*statep = arg;
	ipmi_dev_t	*dev = statep->is_dev_ext;
	uint8_t		csr;
	int		retval = 0;

	csr = kcs_status(dev);

	if (KCS_ATTN(csr))
		retval |= IPMIFL_ATTN;
	if (KCS_IDLE(csr))
		retval |= IPMIFL_BUSY;

	return (retval);
}

static int
ipmi_kcs_attach(void *ipmi_state)
{
	ipmi_state_t		*statep = (ipmi_state_t *)ipmi_state;
	struct ipmi_get_info	info;
	ipmi_dev_t		*devp;

	if (!ipmi_smbios_identify(&info)) {
		return (DDI_FAILURE);
	}

	statep->is_dev_ext = kmem_zalloc(sizeof (ipmi_dev_t), KM_SLEEP);
	if (statep->is_dev_ext == NULL) {
		return (DDI_FAILURE);
	}
	devp = statep->is_dev_ext;
	devp->kstatsp = &statep->is_kstats;

	mutex_init(&devp->if_mutex, NULL, MUTEX_DRIVER, NULL);
	cv_init(&devp->if_cv, NULL, CV_DEFAULT, NULL);
	devp->if_busy = B_FALSE;

	kcs_initregs(devp, &info);

	devp->if_up = B_TRUE;

	/*
	 * Now that we have hardware and software setup
	 * start the dispatch task
	 */
	(void) sprintf(devp->if_name, "%s%d", ipmi_kcs.ipmi_pi_name,
	    statep->is_instance);
	if (statep->is_pi->ipmi_pi_taskinit(statep, kcs_loop, devp->if_name)
	    != DDI_SUCCESS) {
		kmem_free(statep->is_dev_ext, sizeof (ipmi_dev_t));
		statep->is_dev_ext = NULL;
		return (DDI_FAILURE);
	}

	return (DDI_SUCCESS);
}

static int
ipmi_kcs_detach(void *ipmi_state)
{
	ipmi_state_t		*statep = (ipmi_state_t	*)ipmi_state;
	ipmi_dev_t		*devp;

	devp = statep->is_dev_ext;
	if (devp) {
		devp->if_up = B_FALSE;	/* End tasks while loop */
		statep->is_pi->ipmi_pi_taskexit(statep);

		mutex_destroy(&devp->if_mutex);
		cv_destroy(&devp->if_cv);

		kmem_free(statep->is_dev_ext, sizeof (ipmi_dev_t));
	}
	statep->is_dev_ext = NULL;
	return (DDI_SUCCESS);
}
