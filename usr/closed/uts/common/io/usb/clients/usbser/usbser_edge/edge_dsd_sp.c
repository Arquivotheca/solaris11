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
 * DSD code for the original (SP, Serial Protocol) Edgeports
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

#include <sys/usb/usba.h>

#include <sys/usb/clients/usbser/usbser_dsdi.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_var.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_pipe.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_subr.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_fw.h>
#include <sys/usb/clients/usbser/usbser_edge/usbvend.h>
#include <sys/usb/clients/usbser/usbser_edge/ionsp.h>

static void	edgesp_get_port_cnt(edge_state_t *);

/* device operations */
static int	edgesp_send_open(edge_port_t *);
static int	edgesp_wait_open_resp(edge_port_t *, int);
static int	edgesp_send_close(edge_port_t *);
static int	edgesp_wait_chase_resp(edge_port_t *, int);
static int	edgesp_init_regs(edge_port_t *);

/* pipe callbacks */
static void	edgesp_intr_update_tx_credits(edge_state_t *,
			intr_status_pkt_t *, uint_t);

/* device data parsing */
static void	edgesp_parse_bulkin_data(edge_state_t *, mblk_t *);
static void	edgesp_parse_hdr1(edge_state_t *, uint8_t, uint8_t);
static void	edgesp_parse_cmd_stat_pkt(edge_state_t *, uint8_t, uint8_t);
static void	edgesp_copy_rx_data(edge_state_t *, mblk_t *);

/* misc */
static void	edgesp_new_msr(edge_port_t *, uint8_t);
static void	edgesp_new_lsr(edge_port_t *, uint8_t);
static void	edgesp_new_lsr_data(edge_port_t *, uint8_t, uint8_t);
static uint_t	edgesp_next_port(edge_state_t *, uint_t);

/*
 * table to convert baud rate values defined in sys/termios.h
 * to the UART baud rate divisor; 0 means not supported
 */
static uint16_t edgesp_speedtab[] = {
	0,	/* 0 baud */
	0x1200,	/* 50 baud */
	0x0c00,	/* 75 baud */
	0x082f,	/* 110 baud (2094.545455 -> 230450, %0.0217 over) */
	0x06b1,	/* 134 baud (1713.011152 -> 230398.5, %0.00065 under) */
	0x0600,	/* 150 baud */
	0x0480,	/* 200 baud */
	0x0300,	/* 300 baud */
	0x0180,	/* 600 baud */
	0x00c0,	/* 1200 baud */
	0x0080,	/* 1800 baud */
	0x0060,	/* 2400 baud */
	0x0030,	/* 4800 baud */
	0x0018,	/* 9600 baud */
	0x000c,	/* 19200 baud */
	0x0006,	/* 38400 baud */
	0x0004,	/* 57600 baud */
	0x0003,	/* 76800 baud */
	0x0002,	/* 115200 baud */
	0,	/* 153600 baud - not supported */
	0x0001,	/* 230400 baud */
	0,	/* 307200 baud - not supported */
	0	/* 460800 baud - not supported */
};

int usbser_edge_reset_on_detach = 0;

/* various statistics. TODO: replace with kstats */
static int edge_st_rx_bad_port_num = 0;
static int edge_st_rx_data_loss = 0;
static int edge_st_bulkout_data_loss = 0;
_NOTE(SCHEME_PROTECTS_DATA("monotonic", edge_st_rx_bad_port_num
	edge_st_rx_data_loss edge_st_bulkout_data_loss))


/*
 * attach entire device
 */
int
edgesp_attach_dev(edge_state_t *esp)
{
	if (edgesp_get_descriptors(esp) != USB_SUCCESS) {
		return (USB_FAILURE);
	}

	edgesp_get_port_cnt(esp);

	if (edgesp_setup_firmware(esp) != USB_SUCCESS) {
		return (USB_FAILURE);
	}

	if (edgesp_activate_device(esp) != USB_SUCCESS) {
		return (USB_FAILURE);
	}

	return (USB_SUCCESS);
}


void
edgesp_detach_dev(edge_state_t *esp)
{
	if (usbser_edge_reset_on_detach) {
		(void) edgesp_reset_device(esp);
	}
}


/*
 * get port count from descriptor and save into soft state
 */
static void
edgesp_get_port_cnt(edge_state_t *esp)
{
	mutex_enter(&esp->es_mutex);
	esp->es_port_cnt = esp->es_mfg_descr.NumPorts;
	mutex_exit(&esp->es_mutex);
}


/*
 * pipe operations
 * ---------------
 *
 * open device pipes, serialized version
 *
 * pipes user counter will be increased by one
 * every time except when USB_FAILURE is returned
 */
int
edgesp_open_pipes_serialized(edge_state_t *esp)
{
	int	rval = USB_SUCCESS;

	/* if user counter is non-zero, pipes are already opened */
	if ((esp->es_pipes_users > 0) ||
	    ((rval = edgesp_open_pipes(esp)) == USB_SUCCESS)) {
		esp->es_pipes_users++;
	}

	return (rval);
}


/*
 * close device pipes, serialized version
 */
void
edgesp_close_pipes_serialized(edge_state_t *esp)
{
	/* number of closes cannot exceed number of opens */
	ASSERT(esp->es_pipes_users >= 0);
	/*
	 * close pipes only if we are the last user
	 */
	if ((esp->es_pipes_users > 0) && (--esp->es_pipes_users == 0)) {
		edgesp_close_pipes(esp);
	}
}


/*
 * standard UART operations
 * ------------------------
 *
 *
 * ds_set_port_params
 */
int
edgesp_set_port_params(edge_port_t *ep, ds_port_params_t *tp)
{
	int		rval;
	mblk_t		*mp;
	int		i;
	uint8_t		r_lcr;
	uint_t		ui;
	int		cnt = tp->tp_cnt;
	ds_port_param_entry_t *pe = tp->tp_entries;

	/*
	 * the biggest command is DS_PARAM_BAUD:
	 * 4 register writes (2 byte each)
	 */
	if ((mp = allocb(4 * 2 * cnt, BPRI_LO)) == NULL) {
		USB_DPRINTF_L3(DPRINT_CTLOP, ep->ep_lh,
		    "edgesp_set_port_params: allocb failed");

		return (USB_NO_RESOURCES);
	}

	mutex_enter(&ep->ep_mutex);
	ASSERT(ep->ep_state == EDGE_PORT_OPEN);

	r_lcr = ep->ep_regs[LCR];
	/*
	 * generate a sequence of IOSPs
	 *
	 * additional optimization is based on the fact that
	 * most commands start and end with the write to LCR,
	 * so we can save on LCR writes by re-using previous command
	 */
	for (i = 0; i < cnt; i++, pe++) {
		switch (pe->param) {
		case DS_PARAM_BAUD:
			ui = pe->val.ui;

			/*
			 * if we don't support this speed, return error
			 */
			if ((ui >= NELEM(edgesp_speedtab)) ||
			    ((ui > 0) && (edgesp_speedtab[ui] == 0))) {
				mutex_exit(&ep->ep_mutex);
				freemsg(mp);
				USB_DPRINTF_L3(DPRINT_CTLOP, ep->ep_lh,
				    "edgesp_set_port_params: bad baud %d", ui);

				return (USB_FAILURE);
			}

			r_lcr |= LCR_DL_ENABLE;
			if ((i > 0) && (*(mp->b_wptr - 2) == LCR)) {
				*(mp->b_wptr - 1) = r_lcr;
			} else {
				*mp->b_wptr++ = LCR;
				*mp->b_wptr++ = r_lcr;
			}

			*mp->b_wptr++ = DLL;
			*mp->b_wptr++ = edgesp_speedtab[ui] & 0xff;

			*mp->b_wptr++ = DLM;
			*mp->b_wptr++ = (edgesp_speedtab[ui] >> 8) & 0xff;

			r_lcr &= ~LCR_DL_ENABLE;
			*mp->b_wptr++ = LCR;
			*mp->b_wptr++ = r_lcr;

			continue;
		case DS_PARAM_PARITY:
			ui = pe->val.ui;

			if (ui & PARENB) {
				r_lcr |= LCR_PAR_EN;

				if (ui & PARODD) {
					r_lcr &= ~LCR_PAR_EVEN;
				} else {
					r_lcr |= LCR_PAR_EVEN;
				}
			} else {
				r_lcr &= ~LCR_PAR_EN;
			}

			break;
		case DS_PARAM_STOPB:
			if (pe->val.ui & CSTOPB) {
				r_lcr |= LCR_STOP_MASK;
			} else {
				r_lcr &= ~LCR_STOP_MASK;
			}

			break;
		case DS_PARAM_CHARSZ:
			r_lcr &= ~LCR_BITS_MASK;

			switch (pe->val.ui) {
			case CS5:
				r_lcr |= LCR_BITS_5;

				break;
			case CS6:
				r_lcr |= LCR_BITS_6;

				break;
			case CS7:
				r_lcr |= LCR_BITS_7;

				break;
			case CS8:
				r_lcr |= LCR_BITS_8;

				break;
			}

			break;
		case DS_PARAM_XON_XOFF:
			/*
			 * no need to choose Bank 2, because special commands
			 * are used to set xon/xoff instead of register writes
			 */
			*mp->b_wptr++ = XON1;
			*mp->b_wptr++ = pe->val.uc[0];

			*mp->b_wptr++ = XOFF1;
			*mp->b_wptr++ = pe->val.uc[1];

			continue;
		case DS_PARAM_FLOW_CTL:
			/*
			 * outbound flow control: stop tx if CTS goes low
			 */
			*mp->b_wptr++ = EDGE_REG_TX_FLOW;
			*mp->b_wptr++ = (pe->val.ui & CTSXON) ?
			    IOSP_TX_FLOW_CTS : 0;

			continue;
		default:
			continue;
		}

		if ((i > 0) && (*(mp->b_wptr - 2) == LCR)) {
			*(mp->b_wptr - 1) = r_lcr;  /* reuse previous write */
		} else {
			*mp->b_wptr++ = LCR;
			*mp->b_wptr++ = r_lcr;
		}
	}
	mutex_exit(&ep->ep_mutex);

	rval = edgesp_write_regs(ep, mp->b_rptr, MBLKL(mp));
	freemsg(mp);

	return (rval);
}



/*
 * device operations
 * -----------------
 *
 *
 * write device registers and update shadow copies
 *
 * 'data' is an sequence of register-value pairs,
 * 'num' specifies the number of array entries
 *
 * although this command is synchronous, we use edge_send_data()
 * instead of edge_send_data_sync() to send IOSPs,
 * because we need to make sure the data has reached the device
 * and update the register shadow copies *before* releasing the pipe
 * (to avoid possible race conditions with other register writes)
 *
 * write to XON1/XOFF1 registers is replaced with special commands
 * SET_XON_CHAR/SET_XOFF_CHAR;
 *
 * write to EDGE_REG_TX_FLOW/EDGE_REG_RX_FLOW is replaced with commands
 * IOSP_CMD_SET_TX_FLOW/IOSP_CMD_SET_RX_FLOW
 */
int
edgesp_write_regs(edge_port_t *ep, uint8_t *data, int num)
{
	edge_state_t	*esp = ep->ep_esp;
	edge_pipe_t	*bulkout = &esp->es_bulkout_pipe;
	int		rval;
	mblk_t		*mp;
	uint8_t		regnum;
	uint8_t		ext_cmd;
	uint8_t		*p;
	edge_pipe_req_t *req;

	ASSERT(!mutex_owned(&ep->ep_mutex));

	edge_dump_buf(ep->ep_lh, DPRINT_OUT_PIPE, data, num);

	/* allocate mblk for USB data */
	if ((mp = allocb(num * 2, BPRI_LO)) == NULL) {

		return (USB_FAILURE);
	}

	ext_cmd = IOSP_BUILD_CMD_HDR1(ep->ep_port_num, IOSP_EXT_CMD);
	/*
	 * generate IOSP sequence
	 */
	for (p = data; p < &data[num]; ) {
		regnum = *p++;
		switch (regnum) {
		case XON1:
			*mp->b_wptr++ = ext_cmd;
			*mp->b_wptr++ = IOSP_CMD_SET_XON_CHAR;

			break;
		case XOFF1:
			*mp->b_wptr++ = ext_cmd;
			*mp->b_wptr++ = IOSP_CMD_SET_XOFF_CHAR;

			break;
		case EDGE_REG_TX_FLOW:
			*mp->b_wptr++ = ext_cmd;
			*mp->b_wptr++ = IOSP_CMD_SET_TX_FLOW;

			break;
		case EDGE_REG_RX_FLOW:
			*mp->b_wptr++ = ext_cmd;
			*mp->b_wptr++ = IOSP_CMD_SET_RX_FLOW;

			break;
		default:
			ASSERT(regnum < NUM_16654_REGS);
			*mp->b_wptr++ = MAKE_CMD_WRITE_REG(ep->ep_port_num,
			    regnum);
		}
		*mp->b_wptr++ = *p++;
	}

	rval = edge_send_data(&esp->es_bulkout_pipe, &mp, &req, esp,
	    EDGE_BLOCK);
	if (rval != USB_SUCCESS) {
		if (mp) {
			freemsg(mp);
		}

		return (rval);
	}
	ASSERT(req != NULL);

	edge_pipe_req_wait_completion(bulkout, req);

	rval = (req->req_cr == USB_CR_OK) ? USB_SUCCESS : USB_FAILURE;
	/*
	 * if operation was successful, update register shadow copies
	 */
	if (rval == USB_SUCCESS) {
		mutex_enter(&ep->ep_mutex);
		for (p = data; p < &data[num]; p += 2) {
			if (*p < NUM_16654_REGS) {
				ep->ep_regs[*p] = *(p + 1);
			}
		}
		mutex_exit(&ep->ep_mutex);
	}

	/* delete request and release the pipe */
	edge_pipe_req_delete(bulkout, req);

	if (mp) {
		freemsg(mp);
	}

	return (rval);
}


/*
 * send IOSP extended (3-byte) command and wait for its completion
 */
int
edgesp_send_cmd_sync(edge_port_t *ep, uint8_t cmd, uint8_t param)
{
	edge_state_t	*esp = ep->ep_esp;
	mblk_t		*mp;
	int		rval;

	ASSERT(!mutex_owned(&ep->ep_mutex));
	USB_DPRINTF_L4(DPRINT_OUT_PIPE, ep->ep_lh, "edgesp_send_cmd_sync: "
	    "cmd = %x, param = %x", cmd, param);

	if ((mp = allocb(3, BPRI_LO)) == NULL) {

		return (USB_FAILURE);
	}

	*mp->b_wptr++ = IOSP_BUILD_CMD_HDR1(ep->ep_port_num, IOSP_EXT_CMD);
	*mp->b_wptr++ = cmd;
	*mp->b_wptr++ = param;

	rval = edge_send_data_sync(&esp->es_bulkout_pipe, &mp, esp);

	if (mp) {
		freemsg(mp);
	}

	return (rval);
}


/*
 * initialize hardware serial port
 *
 * 'open_pipes' specifies whether to open USB pipes or not
 */
int
edgesp_open_hw_port(edge_port_t *ep, boolean_t open_pipes)
{
	edge_state_t	*esp = ep->ep_esp;
	int		rval;

	if (open_pipes) {
		if ((rval = edgesp_open_pipes_serialized(esp)) != USB_SUCCESS) {

			return (rval);
		}
	}
	/*
	 * send open command to the device
	 * (XXX sometimes need extra attempts, maybe a data toggle problem)
	 */
	if (((rval = edgesp_send_open(ep)) != USB_SUCCESS) &&
	    ((rval = edgesp_send_open(ep)) != USB_SUCCESS) &&
	    ((rval = edgesp_send_open(ep)) != USB_SUCCESS)) {
		if (open_pipes) {
			edgesp_close_pipes_serialized(esp);
		}

		return (rval);
	}
	/*
	 * initialize registers and their shadow copies
	 */
	if ((rval = edgesp_init_regs(ep)) != USB_SUCCESS) {
		if (open_pipes) {
			edgesp_close_pipes_serialized(esp);
		}

		return (rval);
	}

	return (rval);
}


/*
 * send OPEN_PORT command and wait for response
 */
static int
edgesp_send_open(edge_port_t *ep)
{
	int	rval;

	if (edgesp_send_cmd_sync(ep, IOSP_CMD_OPEN_PORT, 0) != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_OPEN, ep->ep_lh,
		    "edgesp_send_open: OPEN_PORT failed");

		return (USB_FAILURE);
	}

	mutex_enter(&ep->ep_mutex);

	rval = edgesp_wait_open_resp(ep, EDGE_OPEN_RSP_TIMEOUT);

	if (rval == USB_SUCCESS) {
		/* initialize credits with output buffer size */
		ep->ep_tx_credit = ep->ep_tx_bufsz;
	}
	mutex_exit(&ep->ep_mutex);

	return (rval);
}


/*
 * wait until OPEN_PORT command gets response from the device,
 * 'timeout' is in seconds
 */
static int
edgesp_wait_open_resp(edge_port_t *ep, int timeout)
{
	clock_t	until;
	int	rval;

	until = ddi_get_lbolt() + drv_usectohz(timeout * 1000000);

	while (!(ep->ep_flags & EDGE_PORT_OPEN_RSP)) {
		if ((rval = cv_timedwait_sig(&ep->ep_resp_cv, &ep->ep_mutex,
		    until)) <= 0) {

			break;
		}
	}

	if (!(ep->ep_flags & EDGE_PORT_OPEN_RSP)) {
		USB_DPRINTF_L2(DPRINT_OPEN, ep->ep_lh,
		    "edgesp_wait_open_resp: failed (%d)", rval);

		return (USB_FAILURE);
	} else {

		return (USB_SUCCESS);
	}
}


/*
 * close hardware serial port
 */
void
edgesp_close_hw_port(edge_port_t *ep)
{
	edge_state_t	*esp = ep->ep_esp;

	if (edge_dev_is_online(esp)) {
		(void) edgesp_send_close(ep);
	}
	edgesp_close_pipes_serialized(esp);
}


/*
 * drain and close port
 */
static int
edgesp_send_close(edge_port_t *ep)
{
	/*
	 * mandatory output buffer drain
	 */
	mutex_enter(&ep->ep_mutex);
	ep->ep_flags &= ~EDGE_PORT_CHASE_RSP;
	(void) edgesp_chase_port(ep, EDGE_CLOSE_CHASE_TIMEOUT);
	mutex_exit(&ep->ep_mutex);

	if (edgesp_send_cmd_sync(ep, IOSP_CMD_CLOSE_PORT, 0) != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_CLOSE, ep->ep_lh,
		    "edgesp_send_close: CLOSE_PORT failed");

		return (USB_FAILURE);
	} else {

		return (USB_SUCCESS);
	}
}


/*
 * send CHASE_PORT command and wait for response
 */
int
edgesp_chase_port(edge_port_t *ep, int timeout)
{
	int	rval;

	/*
	 * if no transmits happened after last chase, do not waste time
	 */
	if (ep->ep_flags & EDGE_PORT_CHASE_RSP) {

		return (USB_SUCCESS);
	}

	mutex_exit(&ep->ep_mutex);
	rval = edgesp_send_cmd_sync(ep, IOSP_CMD_CHASE_PORT, 0);
	mutex_enter(&ep->ep_mutex);

	if (rval != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_CLOSE, ep->ep_lh,
		    "edgesp_chase_port: CHASE failed");

		return (rval);
	} else {

		return (edgesp_wait_chase_resp(ep, timeout));
	}
}


/*
 * wait until CHASE_PORT command gets response from the device
 * 'timeout' is in seconds
 */
static int
edgesp_wait_chase_resp(edge_port_t *ep, int timeout)
{
	clock_t	until;

	until = ddi_get_lbolt() + drv_usectohz(timeout * 1000000);

	while (!(ep->ep_flags & EDGE_PORT_CHASE_RSP)) {
		if (cv_timedwait_sig(&ep->ep_resp_cv, &ep->ep_mutex,
		    until) <= 0) {

			break;
		}
	}

	if ((ep->ep_flags & EDGE_PORT_CHASE_RSP) &&
	    (ep->ep_chase_status == 0)) {

		return (USB_SUCCESS);
	} else {
		USB_DPRINTF_L2(DPRINT_CLOSE, ep->ep_lh,
		    "edgesp_wait_chase_resp: failed");

		return (USB_FAILURE);
	}
}


/*
 * initialize registers and shadow copies
 */
static int
edgesp_init_regs(edge_port_t *ep)
{
	uint8_t	regs[4];
	uint8_t	*p = &regs[0];

	USB_DPRINTF_L4(DPRINT_OPEN, ep->ep_lh, "edgesp_init_regs");

	*p++ = LCR;
	*p++ = LCR_BITS_8 | LCR_STOP_1;

	*p++ = MCR;
	*p++ = MCR_MASTER_IE;

	return (edgesp_write_regs(ep, regs, _PTRDIFF(p, &regs[0])));
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
edgesp_bulkin_cb(usb_pipe_handle_t pipe, usb_bulk_req_t *req)
{
	edge_state_t	*esp = (edge_state_t *)req->bulk_client_private;
	edge_pipe_t	*bulkin = &esp->es_bulkin_pipe;
	mblk_t		*data = req->bulk_data;
	int		data_len;
	edge_port_t	*ep;
	int		rx_avail;
	int		pcb_rx;
	int		len, i;
	int		rval;

	data_len = (data) ? MBLKL(data) : 0;

	/* update rx counter */
	mutex_enter(&esp->es_mutex);
	esp->es_rx_avail -= data_len;
	rx_avail = esp->es_rx_avail;
	mutex_exit(&esp->es_mutex);

	USB_DPRINTF_L4(DPRINT_IN_PIPE, esp->es_lh, "edgesp_bulkin_cb: cr=%d "
	    "flags=%x data=%p len=%d rx_avail=%d", req->bulk_completion_reason,
	    req->bulk_cb_flags, (void *)data, data_len, rx_avail);

	/* parse incoming data */
	if (data) {
		edgesp_parse_bulkin_data(esp, data);
	}

	mutex_enter(&bulkin->pipe_mutex);
	bulkin->pipe_cr = req->bulk_completion_reason;
	mutex_exit(&bulkin->pipe_mutex);

	usb_free_bulk_req(req);
	req = NULL;
	data = NULL;
	edge_pipe_release(bulkin);

	/*
	 * if more data available, request another receive
	 */
	if ((rx_avail > 0) && edge_dev_is_online(esp)) {
		len = max(EDGE_BULKIN_LEN, rx_avail);
		rval = edge_receive_data(&esp->es_bulkin_pipe, len, esp,
		    EDGE_NOBLOCK);

		USB_DPRINTF_L4(DPRINT_IN_PIPE, esp->es_lh,
		    "edgesp_bulkin_cb: receive_data(%d) rval=%d", len, rval);
	}

	/*
	 * invoke rx callbacks if needed
	 */
	for (i = 0; i < esp->es_port_cnt; i++) {
		ep = &esp->es_ports[i];

		mutex_enter(&ep->ep_mutex);
		pcb_rx = (ep->ep_rx_mp != NULL);
		mutex_exit(&ep->ep_mutex);

		if (pcb_rx) {
			ep->ep_cb.cb_rx(ep->ep_cb.cb_arg);
		}
	}
}


/*
 * bulk out common callback
 */
/*ARGSUSED*/
void
edgesp_bulkout_cb(usb_pipe_handle_t pipe, usb_bulk_req_t *req)
{
	edge_state_t	*esp = (edge_state_t *)req->bulk_client_private;
	edge_pipe_t	*bulkout = &esp->es_bulkout_pipe;
	edge_port_t	*ep;
	int		data_len;
	int		i, tx_last;
	int		xferd = 0;
	int		pcb_tx;		/* tx callback needed? */

	data_len = (req->bulk_data) ? MBLKL(req->bulk_data) : 0;
	if (req->bulk_completion_reason && (data_len > 0)) {
		edge_st_bulkout_data_loss++;
	}
	USB_DPRINTF_L4(DPRINT_OUT_PIPE, esp->es_lh,
	    "edgesp_bulkout_cb: cr=%d cb_flags=%x data=%p len=%d",
	    req->bulk_completion_reason, req->bulk_cb_flags,
	    (void *)req->bulk_data, data_len);

	/* request complete - notify waiters, free resources */
	edge_pipe_req_complete(bulkout, req->bulk_completion_reason);

	usb_free_bulk_req(req);
	req = NULL;

	mutex_enter(&esp->es_mutex);
	tx_last = esp->es_tx_last;	/* port last written to */
	mutex_exit(&esp->es_mutex);

	/*
	 * find port with available tx data and kick off new transmit.
	 * to avoid one port starving others, we scan ports in a round-robin
	 * fashion, always starting with the least recently used one
	 */
	for (i = esp->es_port_cnt; i > 0; i--) {
		tx_last = edgesp_next_port(esp, tx_last);
		ep = &esp->es_ports[tx_last];

		mutex_enter(&ep->ep_mutex);
		if (ep->ep_tx_mp == NULL) {
			/* no more data, notify waiters */
			cv_broadcast(&ep->ep_tx_cv);

			/* see if tx callback is needed */
			pcb_tx = (ep->ep_flags & EDGE_PORT_TX_CB);
			ep->ep_flags &= ~EDGE_PORT_TX_CB;

		} else if ((ep->ep_tx_credit > 0) && (xferd == 0)) {
			/* send more data */
			edge_tx_start(ep, &xferd);
		}
		mutex_exit(&ep->ep_mutex);

		/* setup tx callback for this port */
		if (pcb_tx) {
			ep->ep_cb.cb_tx(ep->ep_cb.cb_arg);
		}
	}
}


/*
 * interrupt pipe normal callback
 */
/*ARGSUSED*/
void
edgesp_intr_cb(usb_pipe_handle_t ph, usb_intr_req_t *req)
{
	edge_state_t	*esp = (edge_state_t *)req->intr_client_private;
	mblk_t		*data;
	int		rval;
	int		status_len, len;
	char		format_str[8];
	intr_status_pkt_t intr_pkt;
	int		rx_avail;
	uint_t		tx_last;

	data = req->intr_data;
	status_len = INTR_STATUS_PKT_SIZE(esp->es_port_cnt);

	USB_DPRINTF_L4(DPRINT_INTR_PIPE, esp->es_lh, "edgesp_intr_cb: "
	    "cr=%d cb_flags=%x data=%p len=%ld", req->intr_completion_reason,
	    req->intr_cb_flags, (void *)data, (long)((data) ? MBLKL(data) : 0));

	if (MBLKL(data) < status_len) {
		USB_DPRINTF_L2(DPRINT_INTR_PIPE, esp->es_lh,
		    "edgesp_intr_cb: %ld packet too short", (long)MBLKL(data));
		usb_free_intr_req(req);

		return;
	}
	edge_dump_buf(esp->es_lh, DPRINT_INTR_PIPE, data->b_rptr, status_len);

	/* parse interrupt status packet (see ionsp.h for details) */
	(void) sprintf(format_str, "%ds", esp->es_port_cnt + 1);
	(void) usb_parse_data(format_str,
	    data->b_rptr, status_len, &intr_pkt, status_len);

	/* no longer needed */
	usb_free_intr_req(req);
	req = NULL;

	mutex_enter(&esp->es_mutex);
	rx_avail = esp->es_rx_avail += intr_pkt.RxBytesAvail;
	tx_last = esp->es_tx_last;
	mutex_exit(&esp->es_mutex);

	/* update per port tx credits, kick off transmit if credits permit */
	edgesp_intr_update_tx_credits(esp, &intr_pkt, tx_last);

	/*
	 * if more data available, request another receive
	 */
	if ((rx_avail > 0) && edge_dev_is_online(esp)) {
		len = max(EDGE_BULKIN_LEN, rx_avail);
		rval = edge_receive_data(&esp->es_bulkin_pipe, len, esp,
		    EDGE_NOBLOCK);

		USB_DPRINTF_L4(DPRINT_IN_PIPE, esp->es_lh,
		    "edgesp_intr_cb: receive_bulk(%d) rval=%d", len, rval);
	}
}


/*
 * update per port tx credits, kick off transmit if credits permit
 */
static void
edgesp_intr_update_tx_credits(edge_state_t *esp, intr_status_pkt_t *intr_pkt,
		uint_t tx_last)
{
	edge_port_t	*ep;
	int		i;
	int		credit;
	int		xferd = 0;	/* amount of data sent */

	/*
	 * find port with available tx data and kick off transmit.
	 * to avoid one port starving others, we scan ports in a round-robin
	 * fashion, always starting with the least recently used one
	 */
	for (i = esp->es_port_cnt; i > 0; i--) {
		tx_last = edgesp_next_port(esp, tx_last);
		credit = intr_pkt->TxCredits[tx_last];

		if (credit > 0) {
			ep = &esp->es_ports[tx_last];

			mutex_enter(&ep->ep_mutex);
			/* ignore if port is closed */
			if (ep->ep_state == EDGE_PORT_CLOSED) {
				mutex_exit(&ep->ep_mutex);
				continue;
			}

			ep->ep_tx_credit += credit;

			USB_DPRINTF_L4(DPRINT_INTR_PIPE, esp->es_lh,
			    "edgesp_intr_update_tx_credits: new tx_credit[%d]"
			    "=%d", tx_last, ep->ep_tx_credit);

			if (ep->ep_tx_mp && (ep->ep_tx_credit > 0) && !xferd) {
				edge_tx_start(ep, &xferd);
			}
			mutex_exit(&ep->ep_mutex);
		}
	}
}


/*
 * interrupt pipe exception callback
 */
/*ARGSUSED*/
void
edgesp_intr_ex_cb(usb_pipe_handle_t ph, usb_intr_req_t *req)
{
	edge_state_t	*esp = (edge_state_t *)req->intr_client_private;
	usb_cr_t	cr = req->intr_completion_reason;

	USB_DPRINTF_L4(DPRINT_INTR_PIPE, esp->es_lh,
	    "edgesp_intr_ex_cb: cr=%d", cr);

	usb_free_intr_req(req);

	if ((cr != USB_CR_PIPE_CLOSING) && (cr != USB_CR_STOPPED_POLLING) &&
	    edge_dev_is_online(esp)) {
		edge_pipe_start_polling(&esp->es_intr_pipe);
	}
}


/*
 * device data parsing
 * -------------------
 *
 *
 * ALL incoming data is parsed, so we have a few optimizations here
 */
static void
edgesp_parse_bulkin_data(edge_state_t *esp, mblk_t *data)
{
	uint8_t	p0, p1, p2;	/* header bytes */

	mutex_enter(&esp->es_mutex);
	while (data->b_rptr < data->b_wptr) {
		switch (esp->es_rxp.rx_state) {
		case EDGE_RX_NEXT_HDR0:
			esp->es_rxp.rx_hdr[0] = *data->b_rptr++;
			/*
			 * all data is prepended with at least 2 header bytes
			 * so we save this state only when header in split
			 * between current and next packets
			 */
			if (data->b_rptr >= data->b_wptr) {
				esp->es_rxp.rx_state = EDGE_RX_NEXT_HDR1;

				break;
			}

			/* FALLTHRU */
		case EDGE_RX_NEXT_HDR1:
			p1 = esp->es_rxp.rx_hdr[1] = *data->b_rptr++;
			p0 = esp->es_rxp.rx_hdr[0];

			if (IS_CMD_STAT_HDR(p0)) {  /* command/status packet */
				edgesp_parse_hdr1(esp, p0, p1);

				break;
			}

			/* data packet */
			esp->es_rxp.rx_data_len =
			    IOSP_GET_HDR_DATA_LEN(p0, p1);
			esp->es_rxp.rx_port_num = IOSP_GET_HDR_PORT(p0);

			if (data->b_rptr >= data->b_wptr) {
				esp->es_rxp.rx_state = EDGE_RX_NEXT_DATA;

				break;
			}

			/* FALLTHRU */
		case EDGE_RX_NEXT_DATA:
			edgesp_copy_rx_data(esp, data);

			break;
		case EDGE_RX_NEXT_HDR2:
			p2 = esp->es_rxp.rx_hdr[2] = *data->b_rptr++;
			p1 = esp->es_rxp.rx_hdr[1];

			/* 3-byte command/status packet */
			edgesp_parse_cmd_stat_pkt(esp, p1, p2);

			esp->es_rxp.rx_state = EDGE_RX_NEXT_HDR0;

			break;
		default:
			ASSERT(0);	/* cannot happen (parser breakage) */
		}
	}
	mutex_exit(&esp->es_mutex);
}


/*
 * parse bytes 0 and 1 of header
 */
static void
edgesp_parse_hdr1(edge_state_t *esp, uint8_t p0, uint8_t p1)
{
	esp->es_rxp.rx_port_num = IOSP_GET_HDR_PORT(p0);
	esp->es_rxp.rx_stat_code = IOSP_GET_STATUS_CODE(p0);

	if (!IOSP_STATUS_IS_2BYTE(esp->es_rxp.rx_stat_code)) {
		/* additional bytes needed */
		esp->es_rxp.rx_state = EDGE_RX_NEXT_HDR2;
	} else {
		edgesp_parse_cmd_stat_pkt(esp, p1, 0);
	}
}


/*
 * parse command/status packet
 */
static void
edgesp_parse_cmd_stat_pkt(edge_state_t *esp, uint8_t p1, uint8_t p2)
{
	edge_port_t	*ep;

	/* don't trust the device - check port number */
	if (esp->es_rxp.rx_port_num >= esp->es_port_cnt) {
		edge_st_rx_bad_port_num++;
		USB_DPRINTF_L2(DPRINT_IN_PIPE, esp->es_lh,
		    "edgesp_parse_cmd_stat_pkt: invalid port_num, dismissed");

		return;
	}
	ep = &esp->es_ports[esp->es_rxp.rx_port_num];

	mutex_enter(&ep->ep_mutex);
	if (ep->ep_state == EDGE_PORT_CLOSED) {
		/* closed ports don't care about status */
		USB_DPRINTF_L2(DPRINT_IN_PIPE, ep->ep_lh,
		    "edgesp_parse_cmd_stat_pkt: port closed, dismissed");
		mutex_exit(&ep->ep_mutex);

		return;
	}

	switch (esp->es_rxp.rx_stat_code) {
	case IOSP_STATUS_LSR:		/* LSR update */
		edgesp_new_lsr(ep, p1);

		break;
	case IOSP_STATUS_MSR:		/* MSR update */
		edgesp_new_msr(ep, p1);

		break;
	case IOSP_STATUS_LSR_DATA:	/* LSR update + bad data */
		edgesp_new_lsr_data(ep, p1, p2);

		break;
	case IOSP_EXT_STATUS:
		if (p1 == IOSP_EXT_STATUS_CHASE_RSP) { /* CHASE_PORT response */
			USB_DPRINTF_L4(DPRINT_IN_PIPE, esp->es_lh,
			    "edgesp_parse_cmd_stat_pkt[%d]: CHASE_RSP status="
			    "%d", ep->ep_port_num, p2);

			ep->ep_chase_status = p2;
			ep->ep_flags |= EDGE_PORT_CHASE_RSP;
			cv_signal(&ep->ep_resp_cv);
		}

		break;
	case IOSP_STATUS_OPEN_RSP:	/* OPEN_PORT response */
		/* we get current MSR value and transmit buffer size */
		ep->ep_regs[MSR] = p1;
		ep->ep_tx_bufsz = GET_TX_BUFFER_SIZE(p2);
		/*
		 * to avoid too many small writes, we only start transmit
		 * when at least 25% of the output buffer is free
		 */
		ep->ep_tx_credit_thre = max(ep->ep_tx_bufsz / 4,
		    EDGE_FW_BULK_MAX_PACKET_SIZE);

		USB_DPRINTF_L4(DPRINT_IN_PIPE, esp->es_lh,
		    "edgesp_parse_cmd_stat_pkt[%d]: OPEN_RSP msr=%x tx_bufsz="
		    "%d", ep->ep_port_num, p1, ep->ep_tx_bufsz);

		ep->ep_flags |= EDGE_PORT_OPEN_RSP;
		cv_signal(&ep->ep_resp_cv);

		break;
	}
	mutex_exit(&ep->ep_mutex);
}


/*
 * copy data block from packet to separate mblk
 * GSD will later grab it with ds_rx()
 */
static void
edgesp_copy_rx_data(edge_state_t *esp, mblk_t *data)
{
	edge_port_t	*ep;
	int		len, mblk_len;
	mblk_t		*mp;
	uint8_t		*startp;

	/*
	 * calculate amount of data to be copied and update state
	 */
	startp = data->b_rptr;
	mblk_len = MBLKL(data);
	if (mblk_len < esp->es_rxp.rx_data_len) {
		/* rest of the data should arrive later */
		len = mblk_len;
		esp->es_rxp.rx_state = EDGE_RX_NEXT_DATA;
	} else {
		/* all expected data arrived in this packet */
		len = esp->es_rxp.rx_data_len;
		esp->es_rxp.rx_state = EDGE_RX_NEXT_HDR0;
	}
	esp->es_rxp.rx_data_len -= len;
	data->b_rptr += len;

	USB_DPRINTF_L4(DPRINT_IN_PIPE, esp->es_lh, "edgesp_copy_rx_data[%d]: "
	    "%d data bytes", esp->es_rxp.rx_port_num, len);

	/* don't trust device - check port number */
	if (esp->es_rxp.rx_port_num >= esp->es_port_cnt) {
		edge_st_rx_bad_port_num++;
		USB_DPRINTF_L2(DPRINT_IN_PIPE, esp->es_lh,
		    "edgesp_copy_rx_data: invalid port_num, data dismissed");

		return;
	}
	ep = &esp->es_ports[esp->es_rxp.rx_port_num];

	/* copy data into a mblk */
	if ((mp = allocb(len, BPRI_LO)) == NULL) {
		edge_st_rx_data_loss++;
		USB_DPRINTF_L2(DPRINT_IN_PIPE, ep->ep_lh,
		    "edgesp_copy_rx_data: allocb failed, data lost");

		return;
	}
	bcopy(startp, mp->b_wptr, len);
	mp->b_wptr += len;

	mutex_enter(&ep->ep_mutex);
	if (ep->ep_state != EDGE_PORT_OPEN) {
		mutex_exit(&ep->ep_mutex);
		freemsg(mp);

		return;
	} else {
		/* add to other received data */
		edge_put_tail(&ep->ep_rx_mp, mp);
		mutex_exit(&ep->ep_mutex);
	}
}


/*
 * start data transmit
 */
void
edgesp_tx_start(edge_port_t *ep, int *xferd)
{
	edge_state_t	*esp = ep->ep_esp;
	int		len;		/* # of bytes we can transmit */
	mblk_t		*data;		/* data to be transmitted */
	int		data_len;	/* # of bytes in 'data' */
	int		rval;

	ASSERT(!mutex_owned(&esp->es_mutex));
	ASSERT(mutex_owned(&ep->ep_mutex));

	/* see if there is enough credit to make it worth doing a write */
	if (ep->ep_tx_credit < ep->ep_tx_credit_thre) {

		return;
	}

	/* send as much data as port can receive */
	len = min(ep->ep_tx_credit, IOSP_MAX_DATA_LENGTH);
	ASSERT(len > 0);

	/* allocate space for both header and data */
	mutex_exit(&ep->ep_mutex);
	if ((data = allocb(IOSP_DATA_HDR_SIZE + len, BPRI_LO)) == NULL) {
		mutex_enter(&ep->ep_mutex);

		return;
	}

	mutex_enter(&ep->ep_mutex);

	/* Make sure there is still data to transmit */
	if (msgdsize(ep->ep_tx_mp) == 0) {
		freemsg(data);
		USB_DPRINTF_L4(DPRINT_OUT_PIPE, ep->ep_lh,
		    "edgesp_tx_start: no data to transmit");

		return;
	}

	/* leave space for the header */
	data->b_wptr += IOSP_DATA_HDR_SIZE;

	/* copy at most 'len' bytes from mblk chain for transmission */
	data_len = edge_tx_copy_data(ep, data, len);

	/* build header */
	data->b_rptr[0] = IOSP_BUILD_DATA_HDR1(ep->ep_port_num, data_len);
	data->b_rptr[1] = IOSP_BUILD_DATA_HDR2(ep->ep_port_num, data_len);

	mutex_exit(&ep->ep_mutex);
	rval = edge_send_data(&esp->es_bulkout_pipe, &data, NULL, esp,
	    EDGE_NOBLOCK);
	mutex_enter(&ep->ep_mutex);

	/*
	 * if send failed, put data back
	 */
	if (rval != USB_SUCCESS) {
		ASSERT(data);
		data->b_rptr += IOSP_DATA_HDR_SIZE;	/* dismiss header */
		if (ep->ep_tx_mp == NULL) {
			ep->ep_tx_mp = data;
		} else {
			edge_put_head(&ep->ep_tx_mp, data);
		}
	} else {
		ep->ep_tx_credit -= data_len;	/* spent these credits */
		if (xferd) {
			*xferd = data_len;
		}
		/* indicate the need for data drain */
		ep->ep_flags &= ~EDGE_PORT_CHASE_RSP;
	}

	/*
	 * remember that we transmitted to this port
	 */
	mutex_exit(&ep->ep_mutex);
	mutex_enter(&esp->es_mutex);
	esp->es_tx_last = ep->ep_port_num;
	mutex_exit(&esp->es_mutex);
	mutex_enter(&ep->ep_mutex);

	USB_DPRINTF_L4(DPRINT_OUT_PIPE, ep->ep_lh, "edgesp_tx_start: send_bulk"
	    "(%d) rval=%d tx_credit=%d", data_len, rval, ep->ep_tx_credit);
}



/*
 * misc routines
 * -------------
 *
 */
/*
 * Modem Status Register receives new value
 */
static void
edgesp_new_msr(edge_port_t *ep, uint8_t r_msr)
{
	USB_DPRINTF_L4(DPRINT_IN_PIPE, ep->ep_lh,
	    "edgesp_new_msr: old=%x new=%x", ep->ep_regs[MSR], r_msr);

	ep->ep_regs[MSR] = r_msr;	/* update shadow copy */

	mutex_exit(&ep->ep_mutex);
	ep->ep_cb.cb_status(ep->ep_cb.cb_arg);
	mutex_enter(&ep->ep_mutex);

}


/*
 * Line Status Register received new value
 */
static void
edgesp_new_lsr(edge_port_t *ep, uint8_t r_lsr)
{
	USB_DPRINTF_L4(DPRINT_IN_PIPE, ep->ep_lh,
	    "edgesp_new_lsr: old=%x new=%x", ep->ep_regs[LSR], r_lsr);

	ep->ep_regs[LSR] = r_lsr;	/* update shadow copy */
}


/*
 * Line Status Register received new value along with bad data byte.
 * Report this condition to GSD, which will decide what to do.
 */
static void
edgesp_new_lsr_data(edge_port_t *ep, uint8_t r_lsr, uint8_t byte)
{
	mblk_t	*mp;
	uchar_t	err = 0;

	USB_DPRINTF_L4(DPRINT_IN_PIPE, ep->ep_lh, "edgesp_new_lsr_data: "
	    "old=%x new=%x byte=%x", ep->ep_regs[LSR], r_lsr, byte);

	ep->ep_regs[LSR] = r_lsr;	/* update shadow copy */

	/* unless port is fully open */
	if (ep->ep_state != EDGE_PORT_OPEN) {

		return;
	}

	if ((mp = allocb(2, BPRI_HI)) == NULL) {
		USB_DPRINTF_L2(DPRINT_IN_PIPE, ep->ep_lh,
		    "edgesp_new_lsr_data: allocb failed");

		return;
	}
	DB_TYPE(mp) = M_BREAK;

	err |= (r_lsr & LSR_OVER_ERR) ? DS_OVERRUN_ERR : 0;
	err |= (r_lsr & LSR_PAR_ERR) ? DS_PARITY_ERR : 0;
	err |= (r_lsr & LSR_FRM_ERR) ? DS_FRAMING_ERR : 0;
	err |= (r_lsr & LSR_BREAK) ? DS_BREAK_ERR : 0;
	*mp->b_wptr++ = err;
	*mp->b_wptr++ = byte;

	edge_put_tail(&ep->ep_rx_mp, mp);	/* add to the received list */
}


/*
 * get next port number, wrap if necessary
 */
static uint_t
edgesp_next_port(edge_state_t *esp, uint_t num)
{
	if (++num >= esp->es_port_cnt) {
		num = 0;
	}

	return (num);
}
