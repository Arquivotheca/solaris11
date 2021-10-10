/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright(c) 2009 Digi International, Inc., Inside Out
 * Networks, Inc.  All rights reserved.
 */

/*
 *
 * DSD code for the TI-based Edgeports
 *
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/termio.h>
#include <sys/termiox.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/byteorder.h>

#include <sys/usb/usba.h>

#include <sys/usb/clients/usbser/usbser_dsdi.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_var.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_pipe.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_subr.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_fw.h>
#include <sys/usb/clients/usbser/usbser_edge/usbvend.h>
#include <sys/usb/clients/usbser/usbser_edge/ionti.h>
#include <sys/usb/clients/usbser/usbser_rseq.h>

static void	edgeti_get_port_cnt(edge_state_t *);

static int	edgeti_receive_data(edge_port_t *);
static int	edgeti_tx_bytes_left(edge_port_t *);

/* commands */
static int	edgeti_send_cmd_sync(edge_port_t *, uint8_t, uint16_t, void *,
		int);
static int	edgeti_cmd_open(edge_port_t *, uint16_t);
static int	edgeti_cmd_close(edge_port_t *);
static int	edgeti_cmd_start(edge_port_t *);
static int	edgeti_cmd_set_dtr(edge_port_t *, uint16_t);
static int	edgeti_cmd_set_rts(edge_port_t *, uint16_t);
static int	edgeti_cmd_set_config(edge_port_t *,
		edgeti_ump_uart_config_t *);

static void	edgeti_parse_intr_data(edge_state_t *, mblk_t *);
static uint8_t	edgeti_map_line_status(uint8_t);

static void	edgeti_new_msr(edge_port_t *, uint8_t);
static void	edgeti_new_lsr(edge_port_t *, uint8_t);
static void	edgeti_new_lsr_data(edge_port_t *, uint8_t, uint8_t);

static int	edgeti_set_defaults(edge_port_t *);
static int	edgeti_restore_cfg(edge_state_t *, usb_pipe_handle_t, int);

extern usb_pipe_handle_t	usba_get_dflt_pipe_handle(dev_info_t *);

/*
 * baud divisors, precalculated using the following algorithm:
 *
 *	divisor = (uint16_t)(461550L / baud);
 *	round = 4615500L / baud;
 *	if ((round - (divisor * 10)) >= 5)
 *		 divisor++;
 */
static uint16_t	edgeti_speedtab[] = {
	0x0,	/* B0 */
	0x240f,	/* B50 */
	0x180a,	/* B75 */
	0x1064,	/* B110 */
	0xd74,	/* B134 */
	0xc05,	/* B150 */
	0x904,	/* B200 */
	0x603,	/* B300 */
	0x301,	/* B600 */
	0x181,	/* B1200 */
	0x100,	/* B1800 */
	0xc0,	/* B2400 */
	0x60,	/* B4800 */
	0x30,	/* B9600 */
	0x18,	/* B19200 */
	0xc,	/* B38400 */
	0x8,	/* B57600 */
	0x6,	/* B76800 */
	0x4,	/* B115200 */
	0x3,	/* B153600 */
	0x2,	/* B230400 */
	0,	/* B307200 */
	0x1,	/* B460800 */
};

/* convert baud code into baud rate */
static int edgeti_speed2baud[] = {
	0,	/* B0 */
	50,	/* B50 */
	75,	/* B75 */
	110,	/* B110 */
	134,	/* B134 */
	150,	/* B150 */
	200,	/* B200 */
	300,	/* B300 */
	600,	/* B600 */
	1200,	/* B1200 */
	1800,	/* B1800 */
	2400,	/* B2400 */
	4800,	/* B4800 */
	9600,	/* B9600 */
	19200,	/* B19200 */
	38400,	/* B38400 */
	57600,	/* B57600 */
	76800,	/* B76800 */
	115200,	/* B115200 */
	153600,	/* B153600 */
	230400,	/* B230400 */
	307200,	/* B307200 */
	460800	/* B460800 */
};


/*
 * This is the second half of the ti edgeport attach. The initial
 * boot mode -> download mode transition is handled by edgeti_boot_attach().
 */
int
edgeti_attach_dev(edge_state_t *esp)
{

	mutex_enter(&esp->es_mutex);
	esp->es_i2c_type = DTK_ADDR_SPACE_I2C_TYPE_II;
	mutex_exit(&esp->es_mutex);

	if (edgeti_validate_i2c_image(&esp->es_def_pipe) != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgeti_attach_dev(): edgeti_validate_i2c_image() failed");

		return (USB_FAILURE);
	}

	if (edgeti_get_descriptors(esp) != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgeti_attach_dev(): edgeti_get_descriptors() failed");

		return (USB_FAILURE);
	}
	edgeti_get_port_cnt(esp);

	return (USB_SUCCESS);
}


/*
 * get port count from descriptor and save into soft state
 */
static void
edgeti_get_port_cnt(edge_state_t *esp)
{
	mutex_enter(&esp->es_mutex);
	esp->es_port_cnt = esp->es_ti_mfg_descr.NumPorts;
	mutex_exit(&esp->es_mutex);
}


void
edgeti_init_port_params(edge_port_t *ep)
{
	edge_state_t	*esp = ep->ep_esp;
	size_t		sz;

	/* device register addresses */
	ep->ep_uart_base = UMPMEM_BASE_UART1 +
	    ep->ep_port_num * UMPMEM_UART_LEN;
	ep->ep_dma_addr = UMPD_OEDB1_ADDRESS + ep->ep_port_num * UMPD_OEDB_LEN;

	/* lengths for bulk requests */
	if (usb_pipe_get_max_bulk_transfer_size(esp->es_dip, &sz) ==
	    USB_SUCCESS) {
		ep->ep_read_len = (int)min(sz, EDGETI_BULKIN_MAX_LEN);
	} else {
		ep->ep_read_len = EDGETI_BULKIN_MAX_LEN;
	}

	sz = ep->ep_bulkin_pipe.pipe_ep_descr.wMaxPacketSize;
	ep->ep_write_len = (int)min(sz, EDGETI_BULKOUT_MAX_LEN);
}


/*
 * set port defaults on each open and make sure modem status register
 * interrupts are enabled immediately.
 */
int
edgeti_set_defaults(edge_port_t *ep)
{
	edgeti_ump_uart_config_t *config = &ep->ep_uart_config;

	/* default config for open */
	mutex_enter(&ep->ep_mutex);
	config->wBaudRate = edgeti_speedtab[B9600];
	config->wFlags = (UMP_MASK_UART_FLAGS_RECEIVE_MS_INT |
	    UMP_MASK_UART_FLAGS_AUTO_START_ON_ERR);
	config->bDataBits = UMP_UART_CHAR8BITS;
	config->bParity = UMP_UART_NOPARITY;
	config->bStopBits = UMP_UART_STOPBIT1;
	config->cXon = 0;
	config->cXoff = 0;

	/* Convert to big endian values, need to do this for x86 */
	config->wFlags = BE_16(config->wFlags);
	config->wBaudRate = BE_16(config->wBaudRate);
	mutex_exit(&ep->ep_mutex);

	return (edgeti_cmd_set_config(ep, config));
}


/*
 * pipe operations
 * ---------------
 *
 * open device pipes, serialized version
 *
 * shared pipes user counter will be increased by one
 * every time except when USB_FAILURE is returned
 */
int
edgeti_open_pipes_serialized(edge_port_t *ep)
{
	int		rval;

	if ((rval = edgeti_open_port_pipes(ep)) != USB_SUCCESS) {

		return (rval);
	}

	return (rval);
}


/*
 * close device pipes, serialized version
 */
int
edgeti_close_pipes_serialized(edge_port_t *ep)
{
	edgeti_close_port_pipes(ep);

	return (USB_SUCCESS);
}


/*
 * initialize hardware serial port
 *
 * 'open_pipes' specifies whether to open USB pipes or not
 */
int
edgeti_open_hw_port(edge_port_t *ep, boolean_t open_pipes)
{
	uint16_t	settings;
	int		rval;

	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh,
	    "edgeti_open_hw_port: [%d] starting", ep->ep_port_num);

	if (open_pipes) {
		if ((rval = edgeti_open_pipes_serialized(ep)) != USB_SUCCESS) {

			return (rval);
		}
	}

	/* clear loopback */
	if ((rval = edgeti_cmd_set_loopback(ep, EDGETI_CLEAR)) != USB_SUCCESS) {

		goto fail;
	}

	/* set default port parameters */
	if ((rval = edgeti_set_defaults(ep)) != USB_SUCCESS) {

		goto fail;
	}

	/* milliseconds to timeout for DMA transfer */
	settings = (UMP_DMA_MODE_CONTINOUS | UMP_PIPE_TRANS_TIMEOUT_ENA |
	    (EDGETI_TRANSACTION_TIMEOUT << 2));

	/* Open the port */
	if ((rval = edgeti_cmd_open(ep, settings)) != USB_SUCCESS) {

		goto fail;
	}

	/* Start the DMA */
	if ((rval = edgeti_cmd_start(ep)) != USB_SUCCESS) {

		goto fail;
	}

	/* Clear TX and RX buffers */
	if ((rval = edgeti_cmd_purge(ep, UMP_PORT_DIR_OUT | UMP_PORT_DIR_IN)) !=
	    USB_SUCCESS) {

		goto fail;
	}

	if ((rval = edgeti_receive_data(ep)) != USB_SUCCESS) {

		goto fail;
	}

	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh,
	    "edgeti_open_hw_port: [%d] finished", ep->ep_port_num);

	return (rval);

fail:
	if (open_pipes) {
		(void) edgeti_close_pipes_serialized(ep);
	}

	return (rval);
}


/*
 * close hardware serial port
 */
void
edgeti_close_hw_port(edge_port_t *ep)
{
	edge_state_t	*esp = ep->ep_esp;

	ASSERT(!mutex_owned(&ep->ep_mutex));

	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh,
	    "edgeti_close_hw_port: [%d]", ep->ep_port_num);

	/*
	 * The bulk IN/OUT pipes might have got closed due to
	 * a device disconnect event. So its required to check the
	 * pipe handle and proceed if it is not NULL
	 */

	mutex_enter(&ep->ep_mutex);
	if ((ep->ep_bulkin_pipe.pipe_handle == NULL) &&
	    (ep->ep_bulkout_pipe.pipe_handle == NULL)) {
		mutex_exit(&ep->ep_mutex);

		return;
	}
	mutex_exit(&ep->ep_mutex);

	(void) edgeti_cmd_close(ep);

	/* blow away bulkin requests or pipe close will wait until timeout */
	usb_pipe_reset(esp->es_dip, ep->ep_bulkin_pipe.pipe_handle,
	    USB_FLAGS_SLEEP, NULL, NULL);

	(void) edgeti_close_pipes_serialized(ep);
}


/*
 * Wrapper call to edge_receive_data(). The TI edgeports always request
 * a constant length of data set through edgeti_init_port_params()
 */
static int
edgeti_receive_data(edge_port_t *ep)
{
	return (edge_receive_data(&ep->ep_bulkin_pipe, ep->ep_read_len,
	    ep, EDGE_NOBLOCK));
}


/*
 * Device specific port parameters
 */
int
edgeti_set_port_params(edge_port_t *ep, ds_port_params_t *tp)
{
	uint_t		ui;
	int		i;
	int		cnt = tp->tp_cnt;
	ds_port_param_entry_t *pe = tp->tp_entries;
	edgeti_ump_uart_config_t *config = &ep->ep_uart_config;

	mutex_enter(&ep->ep_mutex);
	ASSERT(ep->ep_state == EDGE_PORT_OPEN);

	/* These flags must be set */
	config->wFlags = (UMP_MASK_UART_FLAGS_RECEIVE_MS_INT |
	    UMP_MASK_UART_FLAGS_AUTO_START_ON_ERR);

	/* translate parameters into UART config bits */
	for (i = 0; i < cnt; i++, pe++) {
		switch (pe->param) {
		case DS_PARAM_BAUD:
			ui = pe->val.ui;

			/* if we don't support this speed, return error */
			if ((ui >= NELEM(edgeti_speedtab)) ||
			    ((ui > 0) && (edgeti_speedtab[ui] == 0))) {
				mutex_exit(&ep->ep_mutex);
				USB_DPRINTF_L3(DPRINT_CTLOP, ep->ep_lh,
				    "edgeti_set_port_params: bad baud %d", ui);

				return (USB_FAILURE);
			}

			config->wBaudRate = edgeti_speedtab[ui];
			ep->ep_speed = ui;

			USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh,
			    "edgeti_set_port_params: [%d] baud=%x",
			    ep->ep_port_num, edgeti_speedtab[ui]);

			break;
		case DS_PARAM_PARITY:
			if (pe->val.ui & PARENB) {
				if (pe->val.ui & PARODD) {
					config->wFlags |=
					    UMP_MASK_UART_FLAGS_PARITY;
					config->bParity = UMP_UART_ODDPARITY;
				} else {
					config->wFlags |=
					    UMP_MASK_UART_FLAGS_PARITY;
					config->bParity = UMP_UART_EVENPARITY;
				}
			} else {
				config->bParity = UMP_UART_NOPARITY;
			}

			USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh,
			    "edgeti_set_port_params: [%d] parity=%x",
			    ep->ep_port_num, pe->val.ui);

			break;
		case DS_PARAM_STOPB:
			if (pe->val.ui & CSTOPB) {
				config->bStopBits = UMP_UART_STOPBIT2;
			} else {
				config->bStopBits = UMP_UART_STOPBIT1;
			}

			USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh,
			    "edgeti_set_port_params: [%d] stopb=%x",
			    ep->ep_port_num, pe->val.ui);

			break;
		case DS_PARAM_CHARSZ:
			switch (pe->val.ui) {
			case CS5:
				config->bDataBits = UMP_UART_CHAR5BITS;

				break;
			case CS6:
				config->bDataBits = UMP_UART_CHAR6BITS;

				break;
			case CS7:
				config->bDataBits = UMP_UART_CHAR7BITS;

				break;
			case CS8:
			default:
				config->bDataBits = UMP_UART_CHAR8BITS;

				break;
			}

			USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh,
			    "edgeti_set_port_params: [%d] cs=%x",
			    ep->ep_port_num, config->bDataBits);

			break;
		case DS_PARAM_XON_XOFF:
			config->cXon = pe->val.uc[0];
			config->cXoff = pe->val.uc[1];

			break;
		case DS_PARAM_FLOW_CTL:
			if (pe->val.ui & CTSXON) {
				config->wFlags |=
				    UMP_MASK_UART_FLAGS_OUT_X_CTS_FLOW;
			}
			if (pe->val.ui & RTSXOFF) {
				config->wFlags |= UMP_MASK_UART_FLAGS_RTS_FLOW;
			}

			break;
		default:
			USB_DPRINTF_L2(DPRINT_CTLOP, ep->ep_lh,
			    "edgeti_set_port_params: bad param %d", pe->param);

			break;
		}
	}

	/* Convert to big endian values, need to do this for x86 */
	config->wFlags = BE_16(config->wFlags);
	config->wBaudRate = BE_16(config->wBaudRate);

	mutex_exit(&ep->ep_mutex);

	if (edgeti_cmd_set_config(ep, config) != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_CTLOP, ep->ep_lh,
		    "edgeti_set_port_params() FAILED");

		return (USB_FAILURE);
	}

	return (USB_SUCCESS);
}


/*
 * send CHASE_PORT command and wait for response
 */
/*ARGSUSED*/
int
edgeti_chase_port(edge_port_t *ep, int timeout)
{
	int	baud_rate;
	int	loops;
	int	last_count;
	int	write_size;

	ASSERT(mutex_owned(&ep->ep_mutex));

restart_tx_loop:
	/* Base the LoopTime on the baud rate */
	baud_rate = edgeti_speed2baud[ep->ep_speed];
	if (baud_rate == 0) {
		baud_rate = 1200;
	}

	write_size = msgdsize(ep->ep_tx_mp);
	loops = max(100, (100 * write_size) / (baud_rate / 10));

	for (;;) {
		/* Save Last count */
		last_count = msgdsize(ep->ep_tx_mp);

		/* Is the Edgeport Buffer empty? */
		if (msgdsize(ep->ep_tx_mp) == 0) {

			break;
		}

		mutex_exit(&ep->ep_mutex);
		delay(drv_usectohz(10 * 1000));
		mutex_enter(&ep->ep_mutex);

		if (last_count == msgdsize(ep->ep_tx_mp)) {
			/* No activity.. count down. */
			if (--loops == 0) {

				return (USB_FAILURE);
			}
		} else {
			/* Reset timeout value back to a minimum of 1 second */
			goto restart_tx_loop;
		}
	}

	mutex_exit(&ep->ep_mutex);
	write_size = edgeti_tx_bytes_left(ep);
	loops = max(50, (100 * write_size) / (baud_rate / 10));

	for (;;) {
		if (edgeti_tx_bytes_left(ep) == 0) {
			/* Delay a few char times */
			delay(drv_usectohz(50 * 1000));
			mutex_enter(&ep->ep_mutex);

			return (USB_SUCCESS);
		}

		if (--loops == 0) {
			mutex_enter(&ep->ep_mutex);

			return (USB_FAILURE);
		}
	}
}


/*
 * Check the MSBit of the X and Y DMA byte count registers.
 * A zero in this bit indicates that the TX DMA buffers are empty
 * then check the TX Empty bit in the UART.
 */
static int
edgeti_tx_bytes_left(edge_port_t *ep)
{
	edge_state_t	*esp = ep->ep_esp;
	edgeti_out_ep_desc_block_t oedb;
	mblk_t		*bp;
	uint8_t		lsr;
	int		bytes_left = 0;

	ASSERT(!mutex_owned(&ep->ep_mutex));

	/* Read the DMA Count Registers */
	bp = edgeti_read_ram(&esp->es_def_pipe, ep->ep_dma_addr, UMPD_OEDB_LEN);
	if (bp == NULL) {

		return (bytes_left);
	}
	(void) usb_parse_data(UMPD_OEDB_FORMAT, bp->b_rptr, UMPD_OEDB_LEN,
	    &oedb, sizeof (oedb));
	freemsg(bp);

	if ((oedb.XByteCount & 0x80) != 0) {
		bytes_left += 64;
	}

	/* and the LSR */
	bp = edgeti_read_ram(&esp->es_def_pipe,
	    ep->ep_dma_addr + UMPMEM_OFFS_UART_LSR, 1);
	if (bp == NULL) {

		return (bytes_left);
	}
	lsr = bp->b_rptr[0];
	freemsg(bp);

	if ((lsr & UMP_UART_LSR_TX_MASK) == 0) {
		bytes_left += 1;
	}

	return (bytes_left);
}


/*
 * start data transmit
 */
void
edgeti_tx_start(edge_port_t *ep, int *xferd)
{
	edge_state_t	*esp = ep->ep_esp;
	int		len;		/* # of bytes we can transmit */
	mblk_t		*data;		/* data to be transmitted */
	int		data_len;	/* # of bytes in 'data' */
	int		rval;

	ASSERT(!mutex_owned(&esp->es_mutex));
	ASSERT(mutex_owned(&ep->ep_mutex));

	len = min(msgdsize(ep->ep_tx_mp), ep->ep_write_len);
	if (len == 0) {

		return;
	}

	mutex_exit(&ep->ep_mutex);
	if ((data = allocb(len, BPRI_LO)) == NULL) {
		mutex_enter(&ep->ep_mutex);

		return;
	}
	mutex_enter(&ep->ep_mutex);

	/* copy at most 'len' bytes from mblk chain for transmission */
	data_len = edge_tx_copy_data(ep, data, len);

	mutex_exit(&ep->ep_mutex);
	if (data_len <= 0) {
		USB_DPRINTF_L3(DPRINT_OUT_PIPE, ep->ep_lh, "edgeti_tx_start: "
		    "edge_tx_copy_data copied zero bytes");
		freeb(data);
		mutex_enter(&ep->ep_mutex);
		return;
	}
	rval = edge_send_data(&ep->ep_bulkout_pipe, &data, NULL, ep,
	    EDGE_NOBLOCK);
	mutex_enter(&ep->ep_mutex);

	/*
	 * if send failed, put data back
	 */
	if (rval != USB_SUCCESS) {
		ASSERT(data);
		edge_put_head(&ep->ep_tx_mp, data);
	} else if (xferd) {
		*xferd = data_len;
	}

	USB_DPRINTF_L4(DPRINT_OUT_PIPE, ep->ep_lh, "edgeti_tx_start: send_bulk"
	    "(%d) rval=%d", data_len, rval);
}


/*
 * commands
 * --------
 *
 * send command and wait for its completion
 */
static int
edgeti_send_cmd_sync(edge_port_t *ep, uint8_t cmd, uint16_t value, void *data,
    int size)
{
	edge_state_t	*esp = ep->ep_esp;
	mblk_t		*bp = NULL;
	usb_ctrl_setup_t setup = { EDGE_RQ_WRITE_DEV, 0, 0, 0, 0, 0 };
	usb_cb_flags_t	cb_flags;
	usb_cr_t	cr;
	int		rval;

	ASSERT(!mutex_owned(&ep->ep_mutex));
	USB_DPRINTF_L4(DPRINT_DEF_PIPE, ep->ep_lh, "edgeti_send_cmd_sync: "
	    "cmd=%x value=%x data=%p size=%d", cmd, value, data, size);

	/* copy data into an mblk */
	if (size > 0) {
		ASSERT(data != NULL);
		if ((bp = allocb(size, BPRI_LO)) == NULL) {

			return (USB_FAILURE);
		}
		bcopy(data, bp->b_rptr, size);
		bp->b_wptr = bp->b_rptr + size;
	}

	setup.bRequest = cmd;
	setup.wValue = value;
	setup.wIndex = UMPM_UART1_PORT + ep->ep_port_num;
	setup.wLength = (uint16_t)size;

	/*
	 * USBA1.0 or later can queue control requests. If this code
	 * is ported to the old framework, serialization may be needed.
	 */
	rval = usb_pipe_ctrl_xfer_wait(esp->es_def_pipe.pipe_handle,
	    &setup, &bp, &cr, &cb_flags, 0);

	if (rval != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, ep->ep_lh,
		    "edgeti_send_cmd_sync: %x failed %d %d %x",
		    cmd, rval, cr, cb_flags);
	}
	if (bp) {
		freeb(bp);
	}

	return (rval);
}


/*
 * TI Command sync functions to send commands to the ti device.
 * When a value for the command needs to be sent, 1 sets the command,
 * 0 clears it.
 */
static int
edgeti_cmd_open(edge_port_t *ep, uint16_t settings)
{
	return (edgeti_send_cmd_sync(ep, UMPC_OPEN_PORT, settings, NULL, 0));
}


static int
edgeti_cmd_close(edge_port_t *ep)
{
	return (edgeti_send_cmd_sync(ep, UMPC_CLOSE_PORT, 0, NULL, 0));
}


static int
edgeti_cmd_start(edge_port_t *ep)
{
	return (edgeti_send_cmd_sync(ep, UMPC_START_PORT, 0, NULL, 0));
}


int
edgeti_cmd_purge(edge_port_t *ep, uint16_t mask)
{
	return (edgeti_send_cmd_sync(ep, UMPC_PURGE_PORT, mask, NULL, 0));
}


static int
edgeti_cmd_set_dtr(edge_port_t *ep, uint16_t val)
{
	return (edgeti_send_cmd_sync(ep, UMPC_SET_CLR_DTR, val, NULL, 0));
}


static int
edgeti_cmd_set_rts(edge_port_t *ep, uint16_t val)
{
	return (edgeti_send_cmd_sync(ep, UMPC_SET_CLR_RTS, val, NULL, 0));
}


int
edgeti_cmd_set_loopback(edge_port_t *ep, uint16_t val)
{
	int		rval = USB_SUCCESS;

	rval = edgeti_send_cmd_sync(ep, UMPC_SET_CLR_LOOPBACK, val, NULL, 0);

	if (rval != USB_SUCCESS) {

		return (rval);
	}

	/* update loopback flag in ep_regs[MCR] */
	mutex_enter(&ep->ep_mutex);
	if (val == EDGETI_SET) {
		ep->ep_regs[MCR] |= MCR_LOOPBACK;
	} else {
		ep->ep_regs[MCR] &= ~MCR_LOOPBACK;
	}
	mutex_exit(&ep->ep_mutex);

	return (USB_SUCCESS);
}


int
edgeti_cmd_set_break(edge_port_t *ep, uint16_t val)
{
	return (edgeti_send_cmd_sync(ep, UMPC_SET_CLR_BREAK, val, NULL, 0));
}


static int
edgeti_cmd_set_config(edge_port_t *ep, edgeti_ump_uart_config_t *config)
{
	return (edgeti_send_cmd_sync(ep, UMPC_SET_CONFIG, 0, config,
	    sizeof (edgeti_ump_uart_config_t)));
}


/*
 * Set modem control register
 */
int
edgeti_set_mcr(edge_port_t *ep, uint8_t mcr)
{
	int		rval = USB_SUCCESS;

	ASSERT(!mutex_owned(&ep->ep_mutex));

	if (mcr & MCR_DTR) {
		rval = edgeti_cmd_set_dtr(ep, EDGETI_SET);
		if (rval != USB_SUCCESS) {

			return (rval);
		}
		mutex_enter(&ep->ep_mutex);
		ep->ep_regs[MCR] |= MCR_DTR;
		mutex_exit(&ep->ep_mutex);
	} else {
		rval = edgeti_cmd_set_dtr(ep, EDGETI_CLEAR);
		if (rval != USB_SUCCESS) {

			return (rval);
		}
		mutex_enter(&ep->ep_mutex);
		ep->ep_regs[MCR] &= ~MCR_DTR;
		mutex_exit(&ep->ep_mutex);
	}

	if (mcr & MCR_RTS) {
		rval = edgeti_cmd_set_rts(ep, EDGETI_SET);
		if (rval != USB_SUCCESS) {

			return (rval);
		}
		mutex_enter(&ep->ep_mutex);
		ep->ep_regs[MCR] |= MCR_RTS;
		mutex_exit(&ep->ep_mutex);
	} else {
		rval = edgeti_cmd_set_rts(ep, EDGETI_CLEAR);
		if (rval != USB_SUCCESS) {

			return (rval);
		}
		mutex_enter(&ep->ep_mutex);
		ep->ep_regs[MCR] &= ~MCR_RTS;
		mutex_exit(&ep->ep_mutex);
	}

	return (rval);
}


/*
 * pipe callbacks
 * --------------
 *
 *
 * bulk in common callback
 */
/*ARGSUSED*/
void
edgeti_bulkin_cb(usb_pipe_handle_t pipe, usb_bulk_req_t *req)
{
	edge_port_t	*ep = (edge_port_t *)req->bulk_client_private;
	edge_pipe_t	*bulkin = &ep->ep_bulkin_pipe;
	mblk_t		*data = req->bulk_data;
	uint_t		cr = req->bulk_completion_reason;
	int		data_len;
	boolean_t	no_more_reads;
	boolean_t	lsr_data = B_FALSE;

	data_len = (data) ? MBLKL(data) : 0;

	USB_DPRINTF_L4(DPRINT_IN_PIPE, ep->ep_lh, "edgeti_bulkin_cb[%d]: len=%d"
	    " cr=%d flags=%x data=%p", ep->ep_port_num, data_len, cr,
	    req->bulk_cb_flags, (void *)data);

	/* put data on the read queue */
	mutex_enter(&ep->ep_mutex);
	if ((data_len > 0) && (ep->ep_state != EDGE_PORT_CLOSED) &&
	    (cr == USB_CR_OK)) {
		if (ep->ep_lsr_event) {
			ep->ep_lsr_event = B_FALSE;
			edgeti_new_lsr_data(ep, ep->ep_lsr_mask,
			    data->b_rptr[0]);
			data->b_rptr++;
			data_len--;
			lsr_data = B_TRUE;
		}
		if (data_len > 0) {
			edge_put_tail(&ep->ep_rx_mp, data);
			req->bulk_data = NULL;
		}
	} else {
		data_len = 0;
		ep->ep_no_more_reads = B_TRUE;
	}

	no_more_reads = ep->ep_no_more_reads;

	mutex_exit(&ep->ep_mutex);

	usb_free_bulk_req(req);

	mutex_enter(&bulkin->pipe_mutex);
	bulkin->pipe_cr = cr;
	mutex_exit(&bulkin->pipe_mutex);

	edge_pipe_release(bulkin);

	/* kick off another read unless indicated otherwise */
	if (!no_more_reads) {
		(void) edgeti_receive_data(ep);
	}

	/* setup rx callback for this port */
	if ((data_len > 0) || (lsr_data)) {
		ep->ep_cb.cb_rx(ep->ep_cb.cb_arg);
	}
}


/*
 * bulk out common callback
 */
/*ARGSUSED*/
void
edgeti_bulkout_cb(usb_pipe_handle_t pipe, usb_bulk_req_t *req)
{
	edge_port_t	*ep = (edge_port_t *)req->bulk_client_private;
	edge_pipe_t	*bulkout = &ep->ep_bulkout_pipe;
	mblk_t		*data = req->bulk_data;
	int		data_len;
	boolean_t	need_cb = B_FALSE;

	data_len = (data) ? MBLKL(data) : 0;

	USB_DPRINTF_L4(DPRINT_OUT_PIPE, ep->ep_lh,
	    "edgeti_bulkout_cb[%d]: len=%d cr=%d cb_flags=%x data=%p",
	    ep->ep_port_num, data_len, req->bulk_completion_reason,
	    req->bulk_cb_flags, (void *)req->bulk_data);

	if (req->bulk_completion_reason && (data_len > 0)) {
		/* put untransferred data back on the queue */
		edge_put_head(&ep->ep_tx_mp, data);
		req->bulk_data = NULL;
	}

	/* request complete - notify waiters, free resources */
	edge_pipe_req_complete(bulkout, req->bulk_completion_reason);

	usb_free_bulk_req(req);

	/* if more data available, kick off another transmit */
	mutex_enter(&ep->ep_mutex);
	if (ep->ep_tx_mp == NULL) {
		/* no more data, notify waiters */
		cv_broadcast(&ep->ep_tx_cv);

		/* see if tx callback is needed */
		need_cb = ((ep->ep_flags & EDGE_PORT_TX_CB) != 0);
		ep->ep_flags &= ~EDGE_PORT_TX_CB;
	} else {
		edge_tx_start(ep, NULL);
	}
	mutex_exit(&ep->ep_mutex);

	/* setup tx callback for this port */
	if (need_cb) {
		ep->ep_cb.cb_tx(ep->ep_cb.cb_arg);
	}
}


/*
 * interrupt pipe normal callback
 */
/*ARGSUSED*/
void
edgeti_intr_cb(usb_pipe_handle_t ph, usb_intr_req_t *req)
{
	edge_state_t	*esp = (edge_state_t *)req->intr_client_private;
	mblk_t		*data = req->intr_data;
	int		data_len;

	data_len = (data) ? MBLKL(data) : 0;

	USB_DPRINTF_L4(DPRINT_INTR_PIPE, esp->es_lh, "edgeti_intr_cb: "
	    "cr=%d cb_flags=%x data=%p len=%d", req->intr_completion_reason,
	    req->intr_cb_flags, (void *)data, data_len);

	if (data_len < 2) {
		USB_DPRINTF_L2(DPRINT_INTR_PIPE, esp->es_lh,
		    "edgeti_intr_cb: %d packet too short", data_len);
		usb_free_intr_req(req);

		return;
	}
	req->intr_data = NULL;
	usb_free_intr_req(req);

	edge_dump_buf(esp->es_lh, DPRINT_INTR_PIPE, data->b_rptr, data_len);

	mutex_enter(&esp->es_mutex);
	edgeti_parse_intr_data(esp, data);
	mutex_exit(&esp->es_mutex);
}


/*
 * Parse data received from interrupt callback
 */
static void
edgeti_parse_intr_data(edge_state_t *esp, mblk_t *data)
{
	int		port_num;
	int		function;
	edge_port_t	*ep;
	uint8_t		lsr;
	uint8_t		msr;

	port_num = TIUMP_GET_PORT_FROM_CODE(data->b_rptr[0]);
	function = TIUMP_GET_FUNC_FROM_CODE(data->b_rptr[0]);

	USB_DPRINTF_L4(DPRINT_INTR_PIPE, esp->es_lh, "edgeti_parse_intr_data: "
	    "num=%x func=%x", port_num, function);

	/* not trust the device */
	if (port_num >= esp->es_port_cnt) {
		USB_DPRINTF_L2(DPRINT_INTR_PIPE, esp->es_lh,
		    "edgeti_parse_intr_data: invalid port_num %d", port_num);
		freemsg(data);

		return;
	}

	ep = &esp->es_ports[port_num];

	mutex_enter(&ep->ep_mutex);
	if (ep->ep_state == EDGE_PORT_CLOSED) {
		USB_DPRINTF_L2(DPRINT_INTR_PIPE, esp->es_lh,
		    "edgeti_parse_intr_data: port_num %d closed",
		    port_num);
		mutex_exit(&ep->ep_mutex);
		freemsg(data);

		return;
	}

	switch (function) {
	case TIUMP_INTERRUPT_CODE_LSR:
		lsr = edgeti_map_line_status(data->b_rptr[1]);
		if (lsr & UMP_UART_LSR_DATA_MASK) {
			ep->ep_lsr_event = B_TRUE;
			ep->ep_lsr_mask = lsr;
		} else {
			edgeti_new_lsr(ep, lsr);
		}

		break;

	case TIUMP_INTERRUPT_CODE_MSR:
		msr = data->b_rptr[1];
		edgeti_new_msr(ep, msr);

		break;

	default:
		USB_DPRINTF_L2(DPRINT_INTR_PIPE, esp->es_lh,
		    "edgeti_intr_cb: port_num=%d function=%x unknown",
		    port_num, function);

		break;
	}

	mutex_exit(&ep->ep_mutex);
	freemsg(data);
}


/*
 * Convert TI LSR to standard UART flags
 */
static uint8_t
edgeti_map_line_status(uint8_t ti_lsr)
{
	uint8_t	lsr = 0;

#define	MAP_FLAG(flagUmp, flagUart) \
	if (ti_lsr & flagUmp) lsr |= flagUart;

	MAP_FLAG(UMP_UART_LSR_OV_MASK, LSR_OVER_ERR)	/* overrun */
	MAP_FLAG(UMP_UART_LSR_PE_MASK, LSR_PAR_ERR)	/* parity error */
	MAP_FLAG(UMP_UART_LSR_FE_MASK, LSR_FRM_ERR)	/* framing error */
	MAP_FLAG(UMP_UART_LSR_BR_MASK, LSR_BREAK)	/* break detected */
	MAP_FLAG(UMP_UART_LSR_RX_MASK, LSR_RX_AVAIL)	/* rx data available */
	MAP_FLAG(UMP_UART_LSR_TX_MASK, LSR_TX_EMPTY)	/* THR empty */

#undef MAP_FLAG

	return (lsr);
}


/*
 * interrupt pipe exception callback
 */
/*ARGSUSED*/
void
edgeti_intr_ex_cb(usb_pipe_handle_t ph, usb_intr_req_t *req)
{
	edge_state_t	*esp = (edge_state_t *)req->intr_client_private;
	usb_cr_t	cr = req->intr_completion_reason;

	USB_DPRINTF_L4(DPRINT_INTR_PIPE, esp->es_lh,
	    "edgeti_intr_ex_cb: cr=%d", cr);

	usb_free_intr_req(req);

	if ((cr != USB_CR_PIPE_CLOSING) && (cr != USB_CR_STOPPED_POLLING) &&
	    (cr != USB_CR_UNSPECIFIED_ERR) && edge_dev_is_online(esp)) {
		edge_pipe_start_polling(&esp->es_intr_pipe);
	}
}


/*
 * Save new modem status register value
 */
static void
edgeti_new_msr(edge_port_t *ep, uint8_t r_msr)
{
	USB_DPRINTF_L4(DPRINT_IN_PIPE, ep->ep_lh,
	    "edgeti_new_msr: port# %d, old=%x new=%x", ep->ep_port_num,
	    ep->ep_regs[MSR], r_msr);

	ep->ep_regs[MSR] = r_msr;	/* update shadow copy */

	/* invoke status callback */
	mutex_exit(&ep->ep_mutex);
	ep->ep_cb.cb_status(ep->ep_cb.cb_arg);
	mutex_enter(&ep->ep_mutex);
}


/*
 * Save new line status register value received from an interrupt.
 */
static void
edgeti_new_lsr(edge_port_t *ep, uint8_t r_lsr)
{
	USB_DPRINTF_L4(DPRINT_IN_PIPE, ep->ep_lh, "edgeti_new_lsr: "
	    "old=%x new=%x", ep->ep_regs[LSR], r_lsr);

	ep->ep_regs[LSR] = r_lsr;	/* update shadow copy */
}


/*
 * Create a message with the new LSR data byte and
 * put it on the receive buffer
 */
static void
edgeti_new_lsr_data(edge_port_t *ep, uint8_t r_lsr, uint8_t byte)
{
	mblk_t	*mp;
	uchar_t	err = 0;

	USB_DPRINTF_L4(DPRINT_IN_PIPE, ep->ep_lh, "edgeti_new_lsr_data: "
	    "old=%x new=%x byte=%x", ep->ep_regs[LSR], r_lsr, byte);

	ep->ep_regs[LSR] = r_lsr;	/* update shadow copy */

	/* unless port is fully open */
	if (ep->ep_state != EDGE_PORT_OPEN) {

		return;
	}

	if (r_lsr & LSR_BREAK) {
		/*
		 * Parity and Framing errors only count if they
		 * occur exclusive of a break being received.
		 */
		r_lsr &= (uint8_t)(LSR_OVER_ERR | LSR_BREAK);
	}

	mutex_exit(&ep->ep_mutex);
	if ((mp = allocb(2, BPRI_HI)) == NULL) {
		USB_DPRINTF_L2(DPRINT_IN_PIPE, ep->ep_lh,
		    "edgeti_handle_new_lsr: allocb failed");
		mutex_enter(&ep->ep_mutex);

		return;
	}
	DB_TYPE(mp) = M_BREAK;

	err |= (r_lsr & LSR_OVER_ERR) ? DS_OVERRUN_ERR : 0;
	err |= (r_lsr & LSR_PAR_ERR) ? DS_PARITY_ERR : 0;
	err |= (r_lsr & LSR_FRM_ERR) ? DS_FRAMING_ERR : 0;
	err |= (r_lsr & LSR_BREAK) ? DS_BREAK_ERR : 0;
	*mp->b_wptr++ = err;
	*mp->b_wptr++ = byte;

	mutex_enter(&ep->ep_mutex);
	edge_put_tail(&ep->ep_rx_mp, mp);	/* add to the received list */
}


/*
 * TI Edgeport device hotplug restore functions
 * --------------------------------------------
 *
 *
 * TI devices have a 2 part boot process. After a hotplug or a CPR suspend,
 * the TI devices revert to boot mode. At this point usb_check_same_device()
 * will fail, as this is essentially a different device than what was
 * originally connected.
 *
 * This function verifies the reinserted device's vendor and product
 * information before starting the firmware download. (The devices do not have
 * a serial number in boot mode.)
 *
 * When the FW dl is complete, USB_FAILURE is returned to keep the device
 * in its existing state as the device disconnects itself, boots, and then
 * reconnects.
 *
 * On this next reconnect event, the TI device, now in download mode, has a
 * serial number and this function can just return success.
 * usb_check_same_device() can now verify if this is the same edgeport device
 * that had previously been connected. If so, device restore can proceed.
 */
int
edgeti_restore_device(edge_state_t *esp)
{
	usb_dev_descr_t		usb_dev_descr;
	usb_pipe_handle_t	def_ph;
	mblk_t			*pdata = NULL;
	uint16_t		length;
	usb_cr_t		completion_reason;
	usb_cb_flags_t		cb_flags;
	uint8_t			cfg = 0;
	int			rval;

	if (esp->es_dip == NULL) {

		return (USB_INVALID_ARGS);
	}

	length = esp->es_dev_data->dev_descr->bLength;
	def_ph = esp->es_def_pipe.pipe_handle;
	ASSERT(def_ph);

	/* get the boot mode device descriptor */
	rval = usb_pipe_sync_ctrl_xfer(esp->es_dip, def_ph,
	    USB_DEV_REQ_DEV_TO_HOST |
	    USB_DEV_REQ_TYPE_STANDARD,
	    USB_REQ_GET_DESCR,		/* bRequest */
	    USB_DESCR_TYPE_SETUP_DEV,	/* wValue */
	    0,				/* wIndex */
	    length,				/* wLength */
	    &pdata, 0,
	    &completion_reason,
	    &cb_flags, USB_FLAGS_SLEEP);

	if (rval != USB_SUCCESS) {
		if (!((completion_reason == USB_CR_DATA_OVERRUN) && (pdata))) {
			USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh,
			    "edgeti_restore_device() get device descriptor "
			    "failed (%d)", rval);
			freemsg(pdata);

			return (USB_FAILURE);
		}
	}

	ASSERT(pdata != NULL);

	/* parse device descriptor */
	(void) usb_parse_data("2cs4c3s4c", pdata->b_rptr,
	    MBLKL(pdata), &usb_dev_descr,
	    sizeof (usb_dev_descr_t));

	freemsg(pdata);
	pdata = NULL;

	/* bDeviceProtocol is 00 in boot mode */
	if (usb_dev_descr.bDeviceProtocol != NULL) {
		USB_DPRINTF_L4(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgeti_restore_device() TI device in download mode");

		return (USB_SUCCESS);
	}

	if (edgeti_restore_cfg(esp, def_ph, cfg) != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgeti_restore_device: could not restore "
		    "config %d", cfg);
	}

	return (USB_FAILURE);
}


/*
 * Restore the TI's configuration from boot mode back to download mode.
 *
 * This is a multistep process:
 * 1. Get the device's config cloud.
 * 2. Parse the device descriptors from the config cloud looking for the
 *    OUT endpoint.
 * 3. Open this endpoint and download the device's firmware through that.
 * 4. Device will disconnect and boot itself afterwards.
 *    edge_restore_device_state() can proceed with its remaining steps.
 */
static int
edgeti_restore_cfg(edge_state_t *esp, usb_pipe_handle_t default_ph, int cfg)
{
	usb_cr_t	completion_reason;
	usb_cb_flags_t	cb_flags;
	usb_cfg_descr_t cfg_descr;
	usb_ep_descr_t	end_pt;
	edge_pipe_t	pipe;
	mblk_t		*pdata = NULL;
	uchar_t		*curr_raw_descr;
	uchar_t		raw_descr_type;	/* Type of curr descr */
	uchar_t		raw_descr_len;	/* Length of curr descr */
	uint8_t		*last_byte;
	boolean_t	found = B_FALSE;
	uint_t		cfg_descr_size = 9;

	if (usb_pipe_sync_ctrl_xfer(esp->es_dip, default_ph,
	    USB_DEV_REQ_DEV_TO_HOST | USB_DEV_REQ_TYPE_STANDARD,
	    USB_REQ_GET_DESCR,
	    USB_DESCR_TYPE_SETUP_CFG | cfg,
	    0,
	    cfg_descr_size,
	    &pdata,
	    0,
	    &completion_reason,
	    &cb_flags,
	    0) != USB_SUCCESS) {

		freemsg(pdata);

		return (USB_FAILURE);
	}

	(void) usb_parse_data("2cs5c", pdata->b_rptr,
	    MBLKL(pdata), &cfg_descr, cfg_descr_size);
	freemsg(pdata);
	pdata = NULL;

	if (usb_pipe_sync_ctrl_xfer(esp->es_dip, default_ph,
	    USB_DEV_REQ_DEV_TO_HOST | USB_DEV_REQ_TYPE_STANDARD,
	    USB_REQ_GET_DESCR,
	    USB_DESCR_TYPE_SETUP_CFG | cfg,
	    0,
	    cfg_descr.wTotalLength,
	    &pdata,
	    0,
	    &completion_reason,
	    &cb_flags,
	    0) != USB_SUCCESS) {

		freemsg(pdata);

		return (USB_FAILURE);
	}

	curr_raw_descr = (uchar_t *)pdata->b_rptr;
	do {
		raw_descr_len = curr_raw_descr[0];
		raw_descr_type = curr_raw_descr[1];

		switch (raw_descr_type) {
		case USB_DESCR_TYPE_CFG:
			last_byte = curr_raw_descr +
			    (cfg_descr.wTotalLength * sizeof (uchar_t));

			break;

		case USB_DESCR_TYPE_EP:
			found = B_TRUE;

			break;

		case USB_DESCR_TYPE_IF:
		case USB_DESCR_TYPE_STRING:
		default:

			break;
		}

		if (!found) {
			curr_raw_descr += raw_descr_len;
		}

	} while ((curr_raw_descr < last_byte) && (found == B_FALSE));

	/* Found descriptor */
	ASSERT(found);
	bcopy(curr_raw_descr, &end_pt, raw_descr_len);

	end_pt.wMaxPacketSize = LE_16(end_pt.wMaxPacketSize);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, esp->es_lh,
	    "edgeti_restore_cfg() endpoint %x (%x-%s) found, "
	    "MaxPktSize: %x, Interval:%x",
	    (end_pt.bEndpointAddress & USB_EP_NUM_MASK),
	    (end_pt.bmAttributes & USB_EP_ATTR_MASK),
	    ((end_pt.bEndpointAddress & USB_EP_DIR_IN) ? "IN" : "OUT"),
	    end_pt.wMaxPacketSize, end_pt.bInterval);

	/* no longer needed */
	freemsg(pdata);
	pdata = NULL;

	pipe.pipe_policy.pp_max_async_reqs = 2;

	if (usb_pipe_open(esp->es_dip, &end_pt,
	    &pipe.pipe_policy, USB_FLAGS_SLEEP, &pipe.pipe_handle) !=
	    USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgeti_restore_cfg() could not open pipe for "
		    "FW download");

		return (USB_FAILURE);
	}

	if (edgeti_download_code(esp->es_dip, esp->es_lh, pipe.pipe_handle) !=
	    USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_DEF_PIPE, esp->es_lh,
		    "edgeti_restore_cfg() firmware download failed");

		return (USB_FAILURE);
	}

	usb_pipe_close(esp->es_dip, pipe.pipe_handle, USB_FLAGS_SLEEP,
	    NULL, NULL);

	USB_DPRINTF_L4(DPRINT_DEF_PIPE, esp->es_lh,
	    "edgeti_restore_cfg() firmware downloaded ok");

	return (USB_SUCCESS);
}

/*
 * Reset timer to eliminate detach/reattach behavior
 */
void
edgeti_reset_timer(void *arg)
{
	edge_state_t *esp = (edge_state_t *)arg;
	mblk_t		*data = NULL;
	usb_cr_t	completion_reason;
	usb_cb_flags_t	cb_flags;
	usb_ctrl_setup_t setup = { USB_DEV_REQ_DEV_TO_HOST,
	    USB_REQ_GET_DESCR, USB_DESCR_TYPE_STRING << 8, 0,
	    4, USB_ATTRS_SHORT_XFER_OK };

	/*
	 * From Edgeport hardware spec, driver need to send
	 * "getstringdescriptor 0" to the device once per 15 secs.
	 */
	(void) usb_pipe_ctrl_xfer_wait(esp->es_def_pipe.pipe_handle,
	    &setup, &data, &completion_reason, &cb_flags, 0);

	if (data) {
		freemsg(data);
	}

	mutex_enter(&esp->es_mutex);
	if (esp->es_timeout_enable) {
		esp->es_timeout_id = timeout(edgeti_reset_timer,
		    (void *)esp, drv_usectohz(EDGE_RESET_TIMEOUT));
	} else {
		esp->es_timeout_id = 0;
	}
	mutex_exit(&esp->es_mutex);
}

void
edgeti_start_reset_timer(edge_state_t *esp)
{
	mutex_enter(&esp->es_mutex);
	esp->es_timeout_enable = B_TRUE;
	mutex_exit(&esp->es_mutex);
	edgeti_reset_timer(esp);
}

void
edgeti_stop_reset_timer(edge_state_t *esp)
{
	timeout_id_t tid;

	mutex_enter(&esp->es_mutex);
	esp->es_timeout_enable = B_FALSE;
	if (esp->es_timeout_id) {
		tid = esp->es_timeout_id;
		esp->es_timeout_id = 0;
		mutex_exit(&esp->es_mutex);
		(void) untimeout(tid);
	} else {
		mutex_exit(&esp->es_mutex);
	}
}
