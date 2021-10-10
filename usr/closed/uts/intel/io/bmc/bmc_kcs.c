/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * IPMI: front end to BMC access
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
#include <sys/atomic.h>

#include "bmc_fe.h"


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

static uint32_t kcs_base = DEFAULT_KCS_BASE;
static uint32_t kcs_cmd_reg;
static uint32_t kcs_status_reg;
static uint32_t kcs_in_reg;
static uint32_t kcs_out_reg;
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

void
kcs_initregs(uint32_t base, int regspacing)
{
	kcs_base = base;
	kcs_cmd_reg = kcs_base + regspacing;
	kcs_status_reg = kcs_base + regspacing;
	kcs_in_reg = kcs_base;
	kcs_out_reg = kcs_base;

	dprintf(BMC_DEBUG_LEVEL_3,
	    "cmd 0x%x status 0x%x datain 0x%x dataout 0x%x",
	    kcs_cmd_reg, kcs_status_reg, kcs_in_reg, kcs_out_reg);
}

/*
 * Write a single data byte to the BMC Data Out/Data In register
 */
static void
kcs_put(bmc_kstat_t *ksp, uint8_t byte)
{
	outb(kcs_in_reg, byte);
	atomic_inc_64(&ksp->bmc_bytes_out.value.ui64);
}

/*
 * Read a single data byte from the BMC Data Out/Data In register
 */
static uint8_t
kcs_get(bmc_kstat_t *ksp)
{
	uint8_t b = inb(kcs_out_reg);
	atomic_inc_64(&ksp->bmc_bytes_in.value.ui64);
	return (b);
}

/*
 * Read the BMC's current status.
 */
static uint8_t
kcs_status(void)
{
	return (inb(kcs_status_reg));
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

	csr = kcs_status();

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
		csr = kcs_status();

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
	outb(kcs_cmd_reg, cmd);

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

	csr = kcs_status();

	if (csr == 0xFF) {	/* see comment in kcs_data_pending() */
		*bmc_gonep = 1;
		return (B_FALSE);
	}

	while (!KCS_WRITE(csr)) {
		csr = kcs_status();

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
kcs_transfer_pending(ipmi_state_t *ipmip, int *bmc_gonep, boolean_t *aborted)
{
	uint8_t	csr;
	ipmi_dev_t	*devp = &ipmip->ipmi_dev_ext;

	/*
	 * Dummy read of data byte to clear OBF
	 */
	csr = kcs_status();

	if (csr == 0xFF) { /* see comment in kcs_data_pending() */
		*bmc_gonep = 1;
		return (B_FALSE);
	}

	while (KCS_DAVAIL(csr)) {
		(void) kcs_get(&ipmip->bmc_kstats);
		csr = kcs_status();

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

	csr = kcs_status();

	if (csr == 0xFF) { /* see comment in kcs_data_pending() */
		*bmc_gonep = 1;
		return (B_FALSE);
	}

	while (!KCS_DAVAIL(csr)) {
		csr = kcs_status();

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

void
dump_raw_pkt(uint8_t *buf, uint_t sz)
{
	uint_t i;

	for (i = 0; i < sz; i++) {
		dprintf(BMC_DEBUG_LEVEL_3,
		    "%d => 0x%x", i, buf[i]);
	}
}

void
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
int
kcs_write(ipmi_state_t *ipmip, uint8_t *send_buf, uint32_t pktlen,
    int *bmc_gonep, boolean_t *aborted)
{
	int		ret = BMC_FAILURE;
	ipmi_dev_t	*devp = &ipmip->ipmi_dev_ext;
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
			return (B_FALSE);
		}

		switch (state) {

		case  BMC_TRANSFER_INIT:
			if (kcs_data_pending(devp, bmc_gonep, aborted) ||
			    kcs_transfer_pending(ipmip, bmc_gonep, aborted)) {
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (B_FALSE);

			state = BMC_TRANSFER_START;
			break;

		case  BMC_TRANSFER_START:
			state = BMC_TRANSFER_NEXT;

			if (!kcs_command(devp, WRITE_START, bmc_gonep,
			    aborted)) {

				if (*bmc_gonep != 0)
					return (B_FALSE);

				dprintf(BMC_DEBUG_LEVEL_3,
				    "xfer_start write_start failed");
				state = BMC_TRANSFER_STOP;
				break;
			}
			if (!kcs_writeable(devp, bmc_gonep, aborted)) {

				if (*bmc_gonep != 0)
					return (B_FALSE);

				dprintf(BMC_DEBUG_LEVEL_3,
				    "not in WRITE mode in xfer_start");
				state = BMC_TRANSFER_STOP;
				break;
			}
			if (kcs_transfer_pending(ipmip, bmc_gonep, aborted)) {
				if (*bmc_gonep != 0)
					return (B_FALSE);

				dprintf(BMC_DEBUG_LEVEL_3,
				    "xfer_start clear OBF failed");
				state = BMC_TRANSFER_STOP;
				break;
			}
			break;

		case  BMC_TRANSFER_NEXT:
			kcs_put(&ipmip->bmc_kstats, send_buf[i++]);
			dprintf(BMC_DEBUG_LEVEL_3, "write: 0x%x i = %d",
			    send_buf[i - 1], i);

			if (kcs_data_pending(devp, bmc_gonep, aborted)) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "write not pending failed");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (B_FALSE);

			if (!kcs_writeable(devp, bmc_gonep, aborted)) {

				if (*bmc_gonep != 0)
					return (B_FALSE);

				dprintf(BMC_DEBUG_LEVEL_3,
				    "not in WRITE mode in xfer_next");
				state = BMC_TRANSFER_STOP;
				break;
			}
			if (kcs_transfer_pending(ipmip, bmc_gonep, aborted)) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "write clear OBF failed");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (B_FALSE);

			if (i == (pktlen - 1)) {
				state = BMC_TRANSFER_LAST;
			}
			break;

		case  BMC_TRANSFER_LAST:
			(void) kcs_command(devp, WRITE_END, bmc_gonep, aborted);

			if (*bmc_gonep != 0)
				return (B_FALSE);

			if (kcs_data_pending(devp, bmc_gonep, aborted)) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "xfer_last not pending failed");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (B_FALSE);

			if (!kcs_writeable(devp, bmc_gonep, aborted)) {

				if (*bmc_gonep != 0)
					return (B_FALSE);

				dprintf(BMC_DEBUG_LEVEL_3,
				    "not in WRITE mode in xfer_start");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (kcs_transfer_pending(ipmip, bmc_gonep, aborted)) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "xfer_last clear_OBF failed");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (B_FALSE);

			kcs_put(&ipmip->bmc_kstats, send_buf[i++]);

			dprintf(BMC_DEBUG_LEVEL_3, "xfer_last write: 0x%x",
			    send_buf[i - 1]);

			if (kcs_data_pending(devp, bmc_gonep, aborted)) {
				dprintf(BMC_DEBUG_LEVEL_3,
				    "xfer_last not pending");
				state = BMC_TRANSFER_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (B_FALSE);

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
 * XXX: comment describing read FSM (as for kcs_write).
 */
int
kcs_read(ipmi_state_t *ipmip, uint8_t *recv_buf, uint32_t *pktsz,
    int *bmc_gonep, boolean_t *aborted)
{
	int		ret = BMC_FAILURE;
	ipmi_dev_t	*devp = &ipmip->ipmi_dev_ext;
	uint8_t		csr;
	uint_t		i = 0;
	enum {
		BMC_RECEIVE_INIT,
		BMC_RECEIVE_START,
		BMC_RECEIVE_NEXT,
		BMC_RECEIVE_NEXT_AGAIN,
		BMC_RECEIVE_END,
		BMC_RECEIVE_STOP
	} state = BMC_RECEIVE_START;

	recv_buf[3] = BMC_IPMI_UNSPECIFIC_ERROR; /* invalidate */

	dprintf(BMC_DEBUG_LEVEL_3, "KCS Len 0x%x [[START]]", *pktsz);

	while (state != BMC_RECEIVE_STOP && !TIMEDOUT(devp)) {

		if (*aborted == B_TRUE) {
			*bmc_gonep = 1;
			return (B_FALSE);
		}

		switch (state) {

		case BMC_RECEIVE_START:
			csr = kcs_status();

			/* see comment in kcs_data_pending() */
			if (csr == 0xFF) {
				*bmc_gonep = 1;
				return (B_FALSE);
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
			csr = kcs_status();

			/* see comment in kcs_data_pending() */
			if (csr == 0xFF) {
				*bmc_gonep = 1;
				return (B_FALSE);
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
				csr = kcs_status();

				/* see comment in kcs_data_pending() */
				if (csr == 0xFF) {
					*bmc_gonep = 1;
					return (B_FALSE);
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
				unsigned nextbyte = kcs_get(&ipmip->bmc_kstats);

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
				    "data byte=0x%x", i, *pktsz, nextbyte);
			} else {
				recv_buf[i++] = kcs_get(&ipmip->bmc_kstats);
				dprintf(BMC_DEBUG_LEVEL_3, "[%d] rec: 0x%x",
				    i - 1, recv_buf[i - 1]);
			}

			kcs_put(&ipmip->bmc_kstats, READ_START);

			if (kcs_data_pending(devp, bmc_gonep, aborted)) {
				state = BMC_RECEIVE_STOP;
				break;
			}

			if (*bmc_gonep != 0)
				return (B_FALSE);

			state = BMC_RECEIVE_NEXT_AGAIN;
			break;

		case  BMC_RECEIVE_NEXT_AGAIN:
			/*
			 * READ_STATE?
			 */
			csr = kcs_status();

			/* see comment in kcs_data_pending() */
			if (csr == 0xFF) {
				*bmc_gonep = 1;
				return (B_FALSE);
			}

			switch (KCS_STATEBITS(csr)) {

			case KCS_RD:
				if (kcs_data_available(devp, bmc_gonep,
				    aborted)) {
					state = BMC_RECEIVE_NEXT;
				} else {
					if (*bmc_gonep != 0)
						return (B_FALSE);

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
					(void) kcs_get(&ipmip->bmc_kstats);
					state = BMC_RECEIVE_END;
				} else {
					if (*bmc_gonep != 0)
						return (B_FALSE);
					state = BMC_RECEIVE_STOP;
				}
				break;
			}

			break;

		case  BMC_RECEIVE_END:
			*pktsz = i;

			/* Iff we got here we succeeded! */
			ret = BMC_SUCCESS;

			state = BMC_RECEIVE_STOP;
			break;
		} /* switch */
	}

	dprintf(BMC_DEBUG_LEVEL_3, "KCS Len 0x%x [[END]]", *pktsz);

	return (ret);
}

void
reset_timeout_handler(void *arg)
{
	*(boolean_t *)arg = B_TRUE;
}

/*ARGSUSED*/
void
kcs_reset_bmc(ipmi_dev_t *devp, int *bmc_gonep, boolean_t  *aborted)
{
	uint8_t	csr = kcs_status();
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
		csr = kcs_status();
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
	outb(kcs_cmd_reg, KCS_ABORT);
	dprintf(BMC_DEBUG_LEVEL_3, "KCS RESET");
}

/*
 * Allocates a bmc_kcs_send_t with enough space in the data member to
 * fit `datalen' bytes and initializes it with the supplied values.
 */
static bmc_kcs_send_t *
kcs_construct_send_struct(uint8_t netfn, uint8_t lun, uint8_t cmd, int datalen,
    uint8_t *datap, int *send_struct_length)
{
	bmc_kcs_send_t *sendp;

	ASSERT(datalen >= 0);
	ASSERT(send_struct_length != NULL);

	*send_struct_length = offsetof(bmc_kcs_send_t, data) + datalen;
	if ((sendp = kmem_alloc(*send_struct_length, KM_NOSLEEP)) == NULL)
		return (NULL);

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
kcs_destruct_send_struct(bmc_kcs_send_t *sendp, int sendp_len)
{
	kmem_free(sendp, sendp_len);
}

int
do_kcs2bmc(ipmi_state_t *ipmip, bmc_req_t *send_pkt, bmc_rsp_t *recv_pkt,
    boolean_t interruptable, uint8_t *errp)
{
	bmc_kcs_send_t	*send_bmc;
	bmc_kcs_recv_t	recv_bmc;
	clock_t		ticks;
	uint_t		retrycount = 2;
	uint32_t	spktsz, rpktsz;
	int		ret = BMC_FAILURE;
	int		send_bmc_len = 0;
	int		bmc_gone = 0;
	ipmi_dev_t	*dev = &ipmip->ipmi_dev_ext;
	boolean_t	*aborted = &ipmip->task_abort;

	if (send_pkt->datalength > kcs_max_send_payload) {
		dprintf(BMC_DEBUG_LEVEL_3, "failed send sz: %d",
		    send_pkt->datalength);
		recv_pkt->ccode = BMC_IPMI_DATA_LENGTH_EXCEED;
		recv_pkt->datalength = 0;
		return (ret);
	}

	send_bmc = kcs_construct_send_struct(send_pkt->fn, send_pkt->lun,
	    send_pkt->cmd, send_pkt->datalength, send_pkt->data, &send_bmc_len);

	if (send_bmc == NULL) {
		*errp = ENOMEM;
		return (BMC_FAILURE);
	}

	mutex_enter(&dev->if_mutex);
	while (dev->if_busy) {
		if (*aborted == B_TRUE) {
			mutex_exit(&dev->if_mutex);
			dprintf(BMC_DEBUG_LEVEL_3, "do_kcs2bmc task aborted");
			recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
			recv_pkt->datalength = 0;
			kcs_destruct_send_struct(send_bmc, send_bmc_len);
			return (BMC_FAILURE);
		}

		if (cv_wait_sig(&dev->if_cv, &dev->if_mutex) == 0 &&
		    interruptable) {
			mutex_exit(&dev->if_mutex);
			kcs_destruct_send_struct(send_bmc, send_bmc_len);
			return (BMC_FAILURE);
		}
	}

	dev->if_busy = B_TRUE;
	mutex_exit(&dev->if_mutex);

	ticks = drv_usectohz(DEFAULT_MSG_TIMEOUT);

	dev->timedout = B_FALSE;
	dev->timer_handle = realtime_timeout(kcs_timer_handler, dev, ticks);

	dprintf(BMC_DEBUG_LEVEL_4,
	    "fn 0x%x lun 0x%x cmd 0x%x fnlun 0x%x len 0x%x",
	    send_pkt->fn, send_pkt->lun,
	    send_pkt->cmd, send_bmc->cmd,
	    send_pkt->datalength);

	spktsz = send_pkt->datalength + 2;

	rpktsz = recv_pkt->datalength + 3;

	while (retrycount-- != 0) {

		if (*aborted == B_TRUE) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_kcs2bmc aborted");
			recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
			recv_pkt->datalength = 0;
			break;
		}

		if (TIMEDOUT(dev)) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_kcs2bmc timed out");
			break;
		}

		if (kcs_write(ipmip, (uint8_t *)send_bmc, spktsz, &bmc_gone,
		    aborted) == BMC_FAILURE) {
			/* If we had a write failure, try to reset the BMC */
			kcs_reset_bmc(dev, &bmc_gone, aborted);
			dprintf(BMC_DEBUG_LEVEL_3, "send BMC failed");
			recv_pkt->ccode = BMC_IPMI_OEM_FAILURE_SENDBMC;
			recv_pkt->datalength = 0;
			continue;

		} else if (bmc_gone) {
			*errp = ENXIO;
			ret = BMC_FAILURE;
			break;
		}

		if (TIMEDOUT(dev)) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_kcs2bmc timed out");
			break;
		}

		if (kcs_read(ipmip, (uint8_t *)&recv_bmc, &rpktsz,
		    &bmc_gone, aborted) == BMC_FAILURE) {
			/* If we had a read failure, try to reset the BMC */
			kcs_reset_bmc(dev, &bmc_gone, aborted);
			dprintf(BMC_DEBUG_LEVEL_3, "recv BMC failed");
			recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
			recv_pkt->datalength = 0;
			continue;

		} else if (bmc_gone) {
			*errp = ENXIO;
			ret = BMC_FAILURE;
			break;
		}

		if (TIMEDOUT(dev)) {
			dprintf(BMC_DEBUG_LEVEL_3, "do_kcs2bmc timed out");
			break;
		}
#if DEBUG
		dprintf(BMC_DEBUG_LEVEL_3,
		    "SUMMARY 0x%x 0x%x resp: 0x%x req: 0x%x cmd 0x%x \
		    CMD 0x%x len %d",
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
		}
#endif
		/*
		 * Subtract 3 from the receive packet size to get the amount
		 * of data in the data field
		 */
		rpktsz -= 3;

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
			recv_pkt->datalength = rpktsz;
			bcopy(&recv_bmc.data, recv_pkt->data, rpktsz);
			ret = BMC_SUCCESS;
		}
		break;
	}

	(void) untimeout(dev->timer_handle);
	dev->timer_handle = 0;

	if (TIMEDOUT(dev)) {
		recv_pkt->ccode = BMC_IPMI_COMMAND_TIMEOUT;
		recv_pkt->datalength = 0;
		kcs_reset_bmc(dev, &bmc_gone, aborted);
		if (bmc_gone) {
			*errp = ENXIO;
			ret = BMC_FAILURE;
		}
	}

	mutex_enter(&dev->if_mutex);
	ASSERT(dev->if_busy == B_TRUE);
	dev->if_busy = B_FALSE;
	cv_signal(&dev->if_cv);
	mutex_exit(&dev->if_mutex);

	kcs_destruct_send_struct(send_bmc, send_bmc_len);

	return (ret);
}

/*
 * Returns the size of the largest possible response payload.
 */
int
bmc_kcs_max_response_payload_size(void)
{
	return (KCS_RECV_MAX_PAYLOAD_SIZE);
}

/*
 * Returns the size of the largest possible request payload.
 */
int
bmc_kcs_max_request_payload_size(void)
{
	return (kcs_max_send_payload);
}
