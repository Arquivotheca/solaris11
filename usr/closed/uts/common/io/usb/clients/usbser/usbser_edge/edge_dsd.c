/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright(c) 2009 Digi International, Inc., Inside Out
 * Networks, Inc.  All rights reserved.
 */

/*
 * This file may contain confidential information of Digi
 * International, Inc. and should not be distributed in source
 * form without approval from Sun Legal.
 */

/*
 *
 * DSD code common between original (SP) and new (TI) Edgeports
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
#include <sys/kobj.h>
#include <sys/kobj_lex.h>

#define	USBDRV_MAJOR_VER	2
#define	USBDRV_MINOR_VER	0

#include <sys/usb/usba.h>

#include <sys/usb/clients/usbser/usbser_dsdi.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_var.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_pipe.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_subr.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_fw.h>
#include <sys/usb/clients/usbser/usbser_edge/usbvend.h>
#include <sys/usb/clients/usbser/usbser_edge/ionsp.h>

/*
 * DSD operations
 */
static int	edge_attach(ds_attach_info_t *);
static void	edge_detach(ds_hdl_t);
static int	edge_register_cb(ds_hdl_t, uint_t, ds_cb_t *);
static void	edge_unregister_cb(ds_hdl_t, uint_t);
static int	edge_open_port(ds_hdl_t, uint_t);
static int	edge_close_port(ds_hdl_t, uint_t);
/* override property handling */
static void	edge_override(edge_state_t *);
static int	edge_parse_input_str(char *, edge_ov_t *, edge_state_t *);
static void	edge_override_error(char *, edge_state_t *);
static char	*edge_strtok_r(char *, char *, char **);
/* power management */
static int	edge_usb_power(ds_hdl_t, int, int, int *);
static int	edge_suspend(ds_hdl_t);
static int	edge_resume(ds_hdl_t);
static int	edge_disconnect(ds_hdl_t);
static int	edge_reconnect(ds_hdl_t);
/* standard UART operations */
static int	edge_set_port_params(ds_hdl_t, uint_t, ds_port_params_t *);
static int	edge_set_modem_ctl(ds_hdl_t, uint_t, int, int);
static int	edge_get_modem_ctl(ds_hdl_t, uint_t, int, int *);
static int	edge_break_ctl(ds_hdl_t, uint_t, int);
static int	edge_loopback(ds_hdl_t, uint_t, int);
/* data xfer */
static int	edge_tx(ds_hdl_t, uint_t, mblk_t *);
static mblk_t	*edge_rx(ds_hdl_t, uint_t);
static void	edge_stop(ds_hdl_t, uint_t, int);
static void	edge_start(ds_hdl_t, uint_t, int);
static int	edge_fifo_flush(ds_hdl_t, uint_t, int);
static int	edge_fifo_drain(ds_hdl_t, uint_t, int);

/* configuration routines */
static void	edge_free_soft_state(edge_state_t *);
static void	edge_init_sync_objs(edge_state_t *);
static void	edge_fini_sync_objs(edge_state_t *);
static int	edge_usb_register(edge_state_t *);
static void	edge_usb_unregister(edge_state_t *);
static int	edge_attach_dev(edge_state_t *);
static void	edge_detach_dev(edge_state_t *);
static void	edge_attach_ports(edge_state_t *);
static void	edge_detach_ports(edge_state_t *);
static void	edge_init_port_params(edge_state_t *);
static void	edge_free_descr_tree(edge_state_t *);
static int	edge_register_events(edge_state_t *);
static void	edge_unregister_events(edge_state_t *);
static void	edge_set_dev_state_online(edge_state_t *);

/* hotplug */
static int	edge_restore_device_state(edge_state_t *);
static int	edge_restore_ports_state(edge_state_t *);

/* power management */
static int	edge_create_pm_components(edge_state_t *);
static void	edge_destroy_pm_components(edge_state_t *);
static int	edge_pm_set_busy(edge_state_t *);
static void	edge_pm_set_idle(edge_state_t *);
static int	edge_pwrlvl0(edge_state_t *);
static int	edge_pwrlvl1(edge_state_t *);
static int	edge_pwrlvl2(edge_state_t *);
static int	edge_pwrlvl3(edge_state_t *);

/* pipe operations */
static int	edge_attach_pipes(edge_state_t *);
static void	edge_detach_pipes(edge_state_t *);
static void	edge_disconnect_pipes(edge_state_t *);
static int	edge_reconnect_pipes(edge_state_t *);

/* data transfer routines */
static int	edge_wait_tx_drain(edge_port_t *, int);

/* misc */
static int	edge_reg2mctl(uint8_t, uint8_t);
static void	edge_mctl2reg(int, int, uint8_t *);

typedef struct edge_transfer_mode_override {
	const char	*name;
	const uint8_t	value;
}edge_transfer_mode_override_t;

static const edge_transfer_mode_override_t edge_transfer_mode[] = {
	{"RS232", 0x00},
	{"RS422_NOTERM", 0x01},
	{"RS422_TERM", 0x09},
	{"RS485_HALF_ECHO_ENDUNIT", 0x0D},
	{"RS485_HALF_NOECHO_ENDUNIT", 0x0F},
	{"RS485_HALF_ECHO_MIDDLE", 0x05},
	{"RS485_HALF_NOECHO_MIDDLE", 0x07},
	{"RS485_FULL_MASTER_ENDUNIT", 0x09},
	{"RS485_FULL_SLAVE_ENDUNIT", 0x0D},
	{"RS485_FULL_MASTER_MIDDLE", 0x01},
	{"RS485_FULL_SLAVE_MIDDLE", 0x05}
};

#define	N_EDGE_XFER_MODE (sizeof (edge_transfer_mode))/ \
			sizeof (struct edge_transfer_mode_override)

/*
 * DSD ops structure
 */
ds_ops_t edge_ds_ops = {
	DS_OPS_VERSION,
	edge_attach,
	edge_detach,
	edge_register_cb,
	edge_unregister_cb,
	edge_open_port,
	edge_close_port,
	edge_usb_power,
	edge_suspend,
	edge_resume,
	edge_disconnect,
	edge_reconnect,
	edge_set_port_params,
	edge_set_modem_ctl,
	edge_get_modem_ctl,
	edge_break_ctl,
	edge_loopback,
	edge_tx,
	edge_rx,
	edge_stop,
	edge_start,
	edge_fifo_flush,
	edge_fifo_drain
};

/* debug support */
uint_t   edge_errlevel = USB_LOG_L4;
uint_t   edge_errmask = DPRINT_MASK_ALL;
uint_t   edge_instance_debug = (uint_t)-1;

static int
edge_attach(ds_attach_info_t *aip)
{
	edge_state_t	*esp;
	usb_dev_descr_t *usb_dev_descr;

	esp = (edge_state_t *)kmem_zalloc(sizeof (edge_state_t), KM_SLEEP);
	esp->es_dip = aip->ai_dip;
	esp->es_usb_events = aip->ai_usb_events;
	*aip->ai_hdl = (ds_hdl_t)esp;

	if (edge_usb_register(esp) != USB_SUCCESS) {

		goto fail_register;
	}

	edge_init_sync_objs(esp);

	if (edge_attach_dev(esp) != USB_SUCCESS) {

		goto fail_attach_dev;
	}

	edge_attach_ports(esp);

	if (edge_init_pipes(esp) != USB_SUCCESS) {

		goto fail_init_pipes;
	}

	edge_init_port_params(esp);
	edge_free_descr_tree(esp);
	edge_set_dev_state_online(esp);

	if (edge_create_pm_components(esp) != USB_SUCCESS) {

		goto fail_pm;
	}

	if (edge_register_events(esp) != USB_SUCCESS) {

		goto fail_events;
	}

	if (edge_attach_pipes(esp) != USB_SUCCESS) {

		goto fail_attach_pipes;
	}

	*aip->ai_port_cnt = esp->es_port_cnt;
	usb_dev_descr = esp->es_dev_data->dev_descr;

	/*
	 * For new Edgeport/416, we need to stop autodetach,
	 * otherwise it would be reattaching whenever
	 * it is autodetached.
	 */
	if (usb_dev_descr->idProduct == 0x247) {
		if (ddi_prop_update_int(DDI_DEV_T_NONE, esp->es_dip,
		    DDI_NO_AUTODETACH, 1) != DDI_PROP_SUCCESS) {

			goto fail_attach_pipes;
		}
	}

	if (esp->es_is_ti) {
		edgeti_start_reset_timer(esp);
	}

	return (USB_SUCCESS);

fail_attach_pipes:
	edge_unregister_events(esp);
fail_events:
	edge_destroy_pm_components(esp);
fail_pm:
	edge_fini_pipes(esp);
fail_init_pipes:
	edge_detach_ports(esp);
	edge_detach_dev(esp);
fail_attach_dev:
	edge_fini_sync_objs(esp);
	edge_usb_unregister(esp);
fail_register:
	edge_free_soft_state(esp);

	return (USB_FAILURE);
}


/*
 * ds_detach
 */
static void
edge_detach(ds_hdl_t hdl)
{
	edge_state_t	*esp = (edge_state_t *)hdl;

	if (esp->es_is_ti) {
		edgeti_stop_reset_timer(esp);
	}
	edge_detach_pipes(esp);
	edge_unregister_events(esp);
	edge_destroy_pm_components(esp);
	edge_fini_pipes(esp);
	edge_detach_ports(esp);
	edge_detach_dev(esp);
	edge_fini_sync_objs(esp);
	edge_usb_unregister(esp);
	edge_free_soft_state(esp);
}


/*
 * ds_register_cb
 */
static int
edge_register_cb(ds_hdl_t hdl, uint_t port_num, ds_cb_t *cb)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep;

	if (port_num >= esp->es_port_cnt) {

		return (USB_FAILURE);
	}
	ep = &esp->es_ports[port_num];
	ep->ep_cb = *cb;

	return (USB_SUCCESS);
}


/*
 * ds_unregister_cb
 */
static void
edge_unregister_cb(ds_hdl_t hdl, uint_t port_num)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep;

	if (port_num < esp->es_port_cnt) {
		ep = &esp->es_ports[port_num];
		bzero(&ep->ep_cb, sizeof (ep->ep_cb));
	}
}


/*
 * ds_open_port
 */
static int
edge_open_port(ds_hdl_t hdl, uint_t port_num)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];
	int		rval;

	if (port_num >= esp->es_port_cnt) {

		return (USB_FAILURE);
	}
	USB_DPRINTF_L4(DPRINT_OPEN, ep->ep_lh, "edge_open_port");

	mutex_enter(&esp->es_mutex);
	if (esp->es_dev_state == USB_DEV_DISCONNECTED) {
		mutex_exit(&esp->es_mutex);

		return (USB_FAILURE);
	}
	mutex_exit(&esp->es_mutex);

	if (edge_pm_set_busy(esp) != USB_SUCCESS) {

		return (USB_FAILURE);
	}

	/*
	 * initialize state
	 */
	mutex_enter(&ep->ep_mutex);
	ASSERT(ep->ep_state == EDGE_PORT_CLOSED);
	ASSERT((ep->ep_rx_mp == NULL) && (ep->ep_tx_mp == NULL));

	ep->ep_state = EDGE_PORT_OPENING;
	ep->ep_flags = 0;
	mutex_exit(&ep->ep_mutex);

	/*
	 * initialize hardware serial port, B_TRUE means open pipes
	 */
	sema_p(&esp->es_pipes_sema);
	if (esp->es_is_ti) {
		rval = edgeti_open_hw_port(ep, B_TRUE);
	} else {
		rval = edgesp_open_hw_port(ep, B_TRUE);
	}
	sema_v(&esp->es_pipes_sema);
	mutex_enter(&ep->ep_mutex);
	ep->ep_state = (rval == USB_SUCCESS) ?
	    EDGE_PORT_OPEN : EDGE_PORT_CLOSED;
	mutex_exit(&ep->ep_mutex);

	if (rval != USB_SUCCESS) {
		edge_pm_set_idle(esp);
	}

	return (rval);
}


/*
 * ds_close_port
 */
static int
edge_close_port(ds_hdl_t hdl, uint_t port_num)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];

	if (port_num >= esp->es_port_cnt) {

		return (USB_FAILURE);
	}
	USB_DPRINTF_L4(DPRINT_CLOSE, ep->ep_lh, "edge_close_port");

	sema_p(&esp->es_pipes_sema);
	mutex_enter(&ep->ep_mutex);
	ep->ep_no_more_reads = B_TRUE;

	/* close hardware serial port */
	mutex_exit(&ep->ep_mutex);
	if (esp->es_is_ti) {
		edgeti_close_hw_port(ep);
	} else {
		edgesp_close_hw_port(ep);
	}
	mutex_enter(&ep->ep_mutex);

	/*
	 * free resources and finalize state
	 */
	if (ep->ep_rx_mp) {
		freemsg(ep->ep_rx_mp);
		ep->ep_rx_mp = NULL;
	}
	if (ep->ep_tx_mp) {
		freemsg(ep->ep_tx_mp);
		ep->ep_tx_mp = NULL;
	}

	ep->ep_no_more_reads = B_FALSE;
	ep->ep_state = EDGE_PORT_CLOSED;
	mutex_exit(&ep->ep_mutex);

	edge_pm_set_idle(esp);

	sema_v(&esp->es_pipes_sema);

	return (USB_SUCCESS);
}


/*
 * edge_override:
 *  Determine the transfer mode setting from the conf file
 *  for each corresponding port.
 */
static void
edge_override(edge_state_t *esp)
{
	edge_ov_t ov;
	char	**override_str = NULL;
	char	*name;
	edge_port_t	*ep;
	uint_t	override_str_len;
	uint_t	i;

	if (ddi_prop_lookup_string_array(DDI_DEV_T_ANY, esp->es_dip,
	    DDI_PROP_DONTPASS, "portflag",
	    &override_str, &override_str_len) != DDI_PROP_SUCCESS) {

		return;
	}

	name = esp->es_dev_data->dev_serial;
	/* parse each string in the override property */
	for (i = 0; i < override_str_len; i++) {

		USB_DPRINTF_L4(DPRINT_OV, esp->es_lh,
		    "override_str[%d] = %s", i, override_str[i]);

		bzero(&ov, sizeof (edge_ov_t));

		if (edge_parse_input_str(override_str[i], &ov, esp) ==
		    USB_FAILURE) {
			continue;
		}

		if ((strcmp(name, ov.serialnumber) == 0) &&
		    (ov.portnumber < esp->es_port_cnt)) {
			ep = &esp->es_ports[ov.portnumber];
			ep->ep_uart_config.bUartMode = ov.mode;
		}
	}

	ddi_prop_free(override_str);
}

/*
 * edge_parse_input_str:
 *	parse one conf file override string
 *	return serialnumber, portnumber, mode
 *	function return is success or failure
 */
static int
edge_parse_input_str(char *str, edge_ov_t *ovp,
    edge_state_t *esp)
{
	char		*input_field, *input_value;
	char		*lasts;
	uint_t		i;
	u_longlong_t	value;

	/* parse all the input pairs in the string */
	for (input_field = edge_strtok_r(str, "=", &lasts);
	    input_field != NULL;
	    input_field = edge_strtok_r(lasts, "=", &lasts)) {

		if ((input_value = edge_strtok_r(lasts, " ", &lasts)) ==
		    NULL) {
			edge_override_error("format", esp);

			return (USB_FAILURE);
		}

		if (strcasecmp(input_field, "serialnumber") == 0) {
			(void) snprintf(ovp->serialnumber, 80, "%s",
			    input_value);
		} else if (strcasecmp(input_field, "portnumber") == 0) {
			if (kobj_getvalue(input_value, &value) == -1) {
				edge_override_error("portnumber", esp);

				return (USB_FAILURE);
			}
			ovp->portnumber = (uint_t)value;
		} else if (strcasecmp(input_field, "mode") == 0) {
			for (i = 0; i < N_EDGE_XFER_MODE; i++) {
				if (strcmp(edge_transfer_mode[i].name,
				    input_value) == 0)
					break;
			}
			if (i == N_EDGE_XFER_MODE) {
				edge_override_error("mode", esp);

				return (USB_FAILURE);
			}
			ovp->mode = edge_transfer_mode[i].value;
		} else {
			edge_override_error(input_field, esp);

			return (USB_FAILURE);
		}
	}

	return (USB_SUCCESS);
}

/*
 * edge_override_error:
 *	print an error message if conf file string is bad format
 */
static void
edge_override_error(char *input_field, edge_state_t *esp)
{
	USB_DPRINTF_L1(DPRINT_OV, esp->es_lh,
	    "invalid %s in usbser_edge.conf file entry", input_field);
}

/*
 * edge_strtok_r:
 *	parse a list of tokens
 */
static char *
edge_strtok_r(char *p, char *sep, char **lasts)
{
	char	*p_end;
	char	*tok = NULL;

	if (p == 0 || *p == 0) {

		return (NULL);
	}

	p_end = p+strlen(p);

	do {
		if (strchr(sep, *p) != NULL) {
			if (tok != NULL) {
				*p = 0;
				*lasts = p+1;

				return (tok);
			}
		} else if (tok == NULL) {
			tok = p;
		}
	} while (++p < p_end);

	*lasts = NULL;

	return (tok);
}



/*
 * power management
 *
 * ds_usb_power
 */
/*ARGSUSED*/
static int
edge_usb_power(ds_hdl_t hdl, int comp, int level, int *new_state)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_pm_t	*pm = esp->es_pm;
	int		rval;

	USB_DPRINTF_L4(DPRINT_PM, esp->es_lh, "edge_usb_power");

	mutex_enter(&esp->es_mutex);

	/*
	 * check if we are transitioning to a legal power level
	 */
	if (USB_DEV_PWRSTATE_OK(pm->pm_pwr_states, level)) {
		USB_DPRINTF_L2(DPRINT_PM, esp->es_lh, "edge_usb_power: illegal"
		    " power level %d, pwr_states=%x", level, pm->pm_pwr_states);
		mutex_exit(&esp->es_mutex);

		return (USB_FAILURE);
	}

	/*
	 * if we are about to raise power and asked to lower power, fail
	 */
	if (pm->pm_raise_power && (level < (int)pm->pm_cur_power)) {
		mutex_exit(&esp->es_mutex);

		return (USB_FAILURE);
	}

	switch (level) {
	case USB_DEV_OS_PWR_OFF:
		rval = edge_pwrlvl0(esp);

		break;
	case USB_DEV_OS_PWR_1:
		rval = edge_pwrlvl1(esp);

		break;
	case USB_DEV_OS_PWR_2:
		rval = edge_pwrlvl2(esp);

		break;
	case USB_DEV_OS_FULL_PWR:
		rval = edge_pwrlvl3(esp);
		/*
		 * If usbser dev_state is DISCONNECTED or SUSPENDED, it shows
		 * that the usb serial device is disconnected/suspended while it
		 * is under power down state, now the device is powered up
		 * before it is reconnected/resumed. xxx_pwrlvl3() will set dev
		 * state to ONLINE, we need to set the dev state back to
		 * DISCONNECTED/SUSPENDED.
		 */
		if ((rval == USB_SUCCESS) &&
		    ((*new_state == USB_DEV_DISCONNECTED) ||
		    (*new_state == USB_DEV_SUSPENDED))) {
			esp->es_dev_state = *new_state;
		}

		break;
	default:
		ASSERT(0);	/* cannot happen */
	}

	*new_state = esp->es_dev_state;
	mutex_exit(&esp->es_mutex);

	return (rval);
}


/*
 * ds_suspend
 */
static int
edge_suspend(ds_hdl_t hdl)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	int		state = USB_DEV_SUSPENDED;

	USB_DPRINTF_L4(DPRINT_PM, esp->es_lh, "edge_suspend");

	/*
	 * If the device is suspended while it is under PWRED_DOWN state, we
	 * need to keep the PWRED_DOWN state so that it could be powered up
	 * later. In the mean while, usbser dev state will be changed to
	 * SUSPENDED state.
	 */
	mutex_enter(&esp->es_mutex);
	if (esp->es_dev_state != USB_DEV_PWRED_DOWN) {
		esp->es_dev_state = USB_DEV_SUSPENDED;
	}
	mutex_exit(&esp->es_mutex);

	edge_disconnect_pipes(esp);

	if (esp->es_is_ti) {
		edgeti_stop_reset_timer(esp);
	}

	return (state);
}


/*
 * ds_resume
 */
static int
edge_resume(ds_hdl_t hdl)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	int		current_state;
	int		rval;

	USB_DPRINTF_L4(DPRINT_PM, esp->es_lh, "edge_resume");

	mutex_enter(&esp->es_mutex);
	current_state = esp->es_dev_state;
	mutex_exit(&esp->es_mutex);

	if (current_state != USB_DEV_ONLINE) {
		rval = edge_restore_device_state(esp);
	} else {
		rval = USB_SUCCESS;
	}

	if (esp->es_is_ti) {
		edgeti_start_reset_timer(esp);
	}

	return (rval);
}


/*
 * ds_disconnect
 */
static int
edge_disconnect(ds_hdl_t hdl)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	int		state = USB_DEV_DISCONNECTED;

	USB_DPRINTF_L4(DPRINT_HOTPLUG, esp->es_lh, "edge_disconnect");

	/*
	 * If the device is disconnected while it is under PWRED_DOWN state, we
	 * need to keep the PWRED_DOWN state so that it could be powered up
	 * later. In the mean while, usbser dev state will be changed to
	 * DISCONNECTED state.
	 */
	mutex_enter(&esp->es_mutex);
	if (esp->es_dev_state != USB_DEV_PWRED_DOWN) {
		esp->es_dev_state = USB_DEV_DISCONNECTED;
	}
	mutex_exit(&esp->es_mutex);

	edge_disconnect_pipes(esp);

	return (state);
}


/*
 * ds_reconnect
 */
static int
edge_reconnect(ds_hdl_t hdl)
{
	edge_state_t	*esp = (edge_state_t *)hdl;

	USB_DPRINTF_L4(DPRINT_HOTPLUG, esp->es_lh, "edge_reconnect");

	return (edge_restore_device_state(esp));
}


/*
 * standard UART operations
 * ------------------------
 *
 *
 * ds_set_port_params
 */
static int
edge_set_port_params(ds_hdl_t hdl, uint_t port_num, ds_port_params_t *tp)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];
	int		cnt = tp->tp_cnt;

	ASSERT(port_num < esp->es_port_cnt);
	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh,
	    "edge_set_port_params: %d params", cnt);

	if (cnt <= 0) {

		return (USB_SUCCESS);
	}

	if (esp->es_is_ti) {

		return (edgeti_set_port_params(ep, tp));
	} else {

		return (edgesp_set_port_params(ep, tp));
	}
}


/*
 * ds_set_modem_ctl
 */
static int
edge_set_modem_ctl(ds_hdl_t hdl, uint_t port_num, int mask, int val)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];
	uint8_t		r_mcr, old_mcr, msr;
	uint8_t		regs[2];
	int		nreg = 0;

	ASSERT(port_num < esp->es_port_cnt);

	mutex_enter(&ep->ep_mutex);

	r_mcr = old_mcr = ep->ep_regs[MCR];
	msr = ep->ep_regs[MSR];

	mutex_exit(&ep->ep_mutex);

	edge_mctl2reg(mask, val, &r_mcr);

	if (r_mcr != old_mcr) {
		regs[nreg++] = MCR;
		regs[nreg++] = r_mcr;
	}

	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh, "edge_set_modem_ctl: "
	    "old_mcr=%x mcr=%x msr=%x mask=0%o val=0%o", old_mcr, r_mcr,
	    msr, mask, val);

	if (nreg == 0) {

		return (USB_SUCCESS);
	} else if (esp->es_is_ti) {

		return (edgeti_set_mcr(ep, r_mcr));
	} else {

		return (edgesp_write_regs(ep, regs, nreg));
	}
}


/*
 * ds_get_modem_ctl
 */
static int
edge_get_modem_ctl(ds_hdl_t hdl, uint_t port_num, int mask, int *valp)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];
	uint8_t		mcr, msr;

	ASSERT(port_num < esp->es_port_cnt);

	mutex_enter(&ep->ep_mutex);

	/* grab reg values */
	mcr = ep->ep_regs[MCR];
	msr = ep->ep_regs[MSR];
	mutex_exit(&ep->ep_mutex);

	*valp = edge_reg2mctl(mcr, msr) & mask;

	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh, "edge_get_modem_ctl: "
	    "mcr=%x msr=%x mask=0%o, val=0%o", mcr, msr, mask, *valp);

	return (USB_SUCCESS);
}


/*
 * ds_break_ctl
 */
static int
edge_break_ctl(ds_hdl_t hdl, uint_t port_num, int ctl)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];
	int		is_break;
	int		rval = USB_SUCCESS;

	ASSERT(port_num < esp->es_port_cnt);
	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh,
	    "edge_break_ctl: %s", (ctl == DS_ON) ? "on" : "off");

	mutex_enter(&ep->ep_mutex);
	ASSERT(ep->ep_state == EDGE_PORT_OPEN);
	ASSERT(ctl == DS_ON || ctl == DS_OFF);

	is_break = ep->ep_flags & EDGE_PORT_BREAK;
	mutex_exit(&ep->ep_mutex);

	if ((ctl == DS_ON) && !is_break) {
		if (esp->es_is_ti) {
			rval = edgeti_cmd_set_break(ep, EDGETI_SET);
		} else {
			rval = edgesp_send_cmd_sync(ep, IOSP_CMD_SET_BREAK, 0);
		}

		if (rval == USB_SUCCESS) {
			mutex_enter(&ep->ep_mutex);
			ep->ep_flags |= EDGE_PORT_BREAK;
			mutex_exit(&ep->ep_mutex);
		}
	} if ((ctl == DS_OFF) && is_break) {
		if (esp->es_is_ti) {
			rval = edgeti_cmd_set_break(ep, EDGETI_CLEAR);
		} else {
			rval = edgesp_send_cmd_sync(ep, IOSP_CMD_CLEAR_BREAK,
			    0);
		}

		if (rval == USB_SUCCESS) {
			mutex_enter(&ep->ep_mutex);
			ep->ep_flags &= ~EDGE_PORT_BREAK;

			/* resume transmit */
			edge_tx_start(ep, NULL);
			mutex_exit(&ep->ep_mutex);
		}
	}

	return (rval);
}


/*
 * ds_loopback
 */
static int
edge_loopback(ds_hdl_t hdl, uint_t port_num, int ctl)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];
	uint8_t		regs[2];
	int		is_loop;
	int		rval = USB_SUCCESS;

	ASSERT(port_num < esp->es_port_cnt);
	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh,
	    "edge_loopback: %s", (ctl == DS_ON) ? "on" : "off");

	mutex_enter(&ep->ep_mutex);
	ASSERT(ep->ep_state == EDGE_PORT_OPEN);
	ASSERT(ctl == DS_ON || ctl == DS_OFF);

	/* check bit indicating internal loopback state */
	is_loop = ep->ep_regs[MCR] & MCR_LOOPBACK;

	regs[0] = MCR;
	regs[1] = ep->ep_regs[MCR];
	mutex_exit(&ep->ep_mutex);

	if ((ctl == DS_ON) && !is_loop) {
		if (esp->es_is_ti) {
			rval = edgeti_cmd_set_loopback(ep, EDGETI_SET);

		} else {
			regs[1] |= MCR_LOOPBACK;
			rval = edgesp_write_regs(ep, regs, 2);
		}
	} else if ((ctl == DS_OFF) && is_loop) {
		if (esp->es_is_ti) {
			rval = edgeti_cmd_set_loopback(ep, EDGETI_CLEAR);

		} else {
			regs[1] &= MCR_LOOPBACK;
			rval = edgesp_write_regs(ep, regs, 2);
		}
	}

	return (rval);
}


/*
 * ds_tx
 */
static int
edge_tx(ds_hdl_t hdl, uint_t port_num, mblk_t *mp)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];
	int		xferd;

	ASSERT(port_num < esp->es_port_cnt);
	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh, "edge_tx");

	/*
	 * sanity checks
	 */
	if (mp == NULL) {
		USB_DPRINTF_L3(DPRINT_CTLOP, ep->ep_lh, "edge_tx: mp=NULL");

		return (USB_SUCCESS);
	}
	if (MBLKL(mp) < 1) {
		USB_DPRINTF_L3(DPRINT_CTLOP, ep->ep_lh, "edge_tx: len<=0");
		freemsg(mp);

		return (USB_SUCCESS);
	}

	ep = &esp->es_ports[port_num];

	mutex_enter(&ep->ep_mutex);

	edge_put_tail(&ep->ep_tx_mp, mp);	/* add to the chain */
	ep->ep_flags |= EDGE_PORT_TX_CB;	/* callback after completion */

	edge_tx_start(ep, &xferd);		/* go! */

	mutex_exit(&ep->ep_mutex);

	return (USB_SUCCESS);
}


/*
 * ds_rx
 */
static mblk_t *
edge_rx(ds_hdl_t hdl, uint_t port_num)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];
	mblk_t		*mp;

	ASSERT(port_num < esp->es_port_cnt);
	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh, "edge_rx");

	mutex_enter(&ep->ep_mutex);
	mp = ep->ep_rx_mp;
	ep->ep_rx_mp = NULL;
	mutex_exit(&ep->ep_mutex);

	return (mp);
}


/*
 * ds_stop
 */
static void
edge_stop(ds_hdl_t hdl, uint_t port_num, int dir)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];

	ASSERT(port_num < esp->es_port_cnt);
	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh, "edge_stop");

	if (dir & DS_TX) {
		mutex_enter(&ep->ep_mutex);
		ep->ep_flags |= EDGE_PORT_TX_STOPPED;
		mutex_exit(&ep->ep_mutex);
	}
}


/*
 * ds_start
 */
static void
edge_start(ds_hdl_t hdl, uint_t port_num, int dir)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];

	ASSERT(port_num < esp->es_port_cnt);
	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh, "edge_start");

	if (dir & DS_TX) {
		mutex_enter(&ep->ep_mutex);
		if (ep->ep_flags & EDGE_PORT_TX_STOPPED) {
			ep->ep_flags &= ~EDGE_PORT_TX_STOPPED;
			edge_tx_start(ep, NULL);
		}
		mutex_exit(&ep->ep_mutex);
	}
}


/*
 * ds_fifo_flush
 */
static int
edge_fifo_flush(ds_hdl_t hdl, uint_t port_num, int dir)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];

	ASSERT(port_num < esp->es_port_cnt);
	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh, "edge_fifo_flush: dir=%x", dir);

	mutex_enter(&ep->ep_mutex);
	ASSERT(ep->ep_state == EDGE_PORT_OPEN);

	/*
	 * current Edgeport firmware does not support flushing internal
	 * buffers, so we just discard the data in DSD buffers
	 */
	if ((dir & DS_TX) && ep->ep_tx_mp) {
		freemsg(ep->ep_tx_mp);
		ep->ep_tx_mp = NULL;
	}
	if ((dir & DS_RX) && ep->ep_rx_mp) {
		freemsg(ep->ep_rx_mp);
		ep->ep_rx_mp = NULL;
	}
	mutex_exit(&ep->ep_mutex);

	return (USB_SUCCESS);
}


/*
 * ds_fifo_drain
 *
 * real wait time can exceed what is requested by the caller
 * because Edgeport firmware has its own hard-coded timeout (5 sec).
 * it is the caller's responsibility to cease submitting new tx data
 * while this function executes
 */
static int
edge_fifo_drain(ds_hdl_t hdl, uint_t port_num, int timeout)
{
	edge_state_t	*esp = (edge_state_t *)hdl;
	edge_port_t	*ep = &esp->es_ports[port_num];
	int		rval = USB_SUCCESS;

	ASSERT(port_num < esp->es_port_cnt);
	USB_DPRINTF_L4(DPRINT_CTLOP, ep->ep_lh, "edge_fifo_drain");

	mutex_enter(&ep->ep_mutex);
	ASSERT(ep->ep_state == EDGE_PORT_OPEN);

	/* wait until local data drains */
	if (edge_wait_tx_drain(ep, 0) != USB_SUCCESS) {
		mutex_exit(&ep->ep_mutex);

		return (USB_FAILURE);
	}

	/* wait until hw fifo drains */
	if (esp->es_is_ti) {
		rval = edgeti_chase_port(ep, timeout);
	} else {
		rval = edgesp_chase_port(ep, timeout);
	}
	mutex_exit(&ep->ep_mutex);

	return (rval);
}


/*
 * configuration routines
 * ----------------------
 *
 */

/*
 * free state structure
 */
static void
edge_free_soft_state(edge_state_t *esp)
{
	kmem_free(esp, sizeof (edge_state_t));
}


/*
 * register/unregister USBA client
 */
static int
edge_usb_register(edge_state_t *esp)
{
	int	rval;

	rval = usb_client_attach(esp->es_dip, USBDRV_VERSION, 0);
	if (rval == USB_SUCCESS) {
		rval = usb_get_dev_data(esp->es_dip, &esp->es_dev_data,
		    USB_PARSE_LVL_IF, 0);
		if (rval == USB_SUCCESS) {
			esp->es_lh =
			    usb_alloc_log_hdl(esp->es_dip, "edge[*].",
			    &edge_errlevel, &edge_errmask,
			    &edge_instance_debug, 0);
			esp->es_def_pipe.pipe_handle =
			    esp->es_dev_data->dev_default_ph;
			esp->es_def_pipe.pipe_esp = esp;
			esp->es_def_pipe.pipe_lh = esp->es_lh;
		}
	}

	return (rval);
}


static void
edge_usb_unregister(edge_state_t *esp)
{
	usb_free_log_hdl(esp->es_lh);
	esp->es_lh = NULL;
	usb_client_detach(esp->es_dip, esp->es_dev_data);
	esp->es_def_pipe.pipe_handle = NULL;
	esp->es_dev_data = NULL;
}


/*
 * init/fini soft state during attach
 */
static void
edge_init_sync_objs(edge_state_t *esp)
{
	mutex_init(&esp->es_mutex, NULL, MUTEX_DRIVER,
	    esp->es_dev_data->dev_iblock_cookie);
	sema_init(&esp->es_pipes_sema, 1, NULL, SEMA_DRIVER, NULL);
}


static void
edge_fini_sync_objs(edge_state_t *esp)
{
	mutex_destroy(&esp->es_mutex);
	sema_destroy(&esp->es_pipes_sema);
}


/*
 * attach entire device
 */
static int
edge_attach_dev(edge_state_t *esp)
{

	mutex_enter(&esp->es_mutex);
	esp->es_is_ti = edgeti_is_ti(esp->es_dev_data);
	mutex_exit(&esp->es_mutex);

	if (esp->es_is_ti) {

		return (edgeti_attach_dev(esp));
	} else {

		return (edgesp_attach_dev(esp));
	}
}


static void
edge_detach_dev(edge_state_t *esp)
{
	if (!esp->es_is_ti) {
		edgesp_detach_dev(esp);
	}
}


/*
 * allocate and initialize per port resources
 */
static void
edge_attach_ports(edge_state_t *esp)
{
	int		i;
	edge_port_t	*ep;

	esp->es_ports = kmem_zalloc(esp->es_port_cnt *
	    sizeof (edge_port_t), KM_SLEEP);

	for (i = 0; i < esp->es_port_cnt; i++) {
		ep = &esp->es_ports[i];
		ep->ep_port_num = i;
		ep->ep_esp = esp;
		ep->ep_uart_config.bUartMode = 0x0;

		(void) sprintf(ep->ep_lh_name, "edge[%d].", i);
		ep->ep_lh = usb_alloc_log_hdl(esp->es_dip, ep->ep_lh_name,
		    &edge_errlevel, &edge_errmask, &edge_instance_debug,
		    0);

		ep->ep_state = EDGE_PORT_CLOSED;
		mutex_init(&ep->ep_mutex, NULL, MUTEX_DRIVER,
		    esp->es_dev_data->dev_iblock_cookie);
		cv_init(&ep->ep_resp_cv, NULL, CV_DRIVER, NULL);
		cv_init(&ep->ep_tx_cv, NULL, CV_DRIVER, NULL);
	}
	edge_override(esp);
}


/*
 * free per port resources
 */
static void
edge_detach_ports(edge_state_t *esp)
{
	int		i;
	edge_port_t	*ep;

	for (i = 0; i < esp->es_port_cnt; i++) {
		ep = &esp->es_ports[i];
		if (ep->ep_state != EDGE_PORT_NOT_INIT) {
			ASSERT(ep->ep_state == EDGE_PORT_CLOSED);

			mutex_destroy(&ep->ep_mutex);
			cv_destroy(&ep->ep_resp_cv);
			cv_destroy(&ep->ep_tx_cv);
			usb_free_log_hdl(ep->ep_lh);
		}
	}
	kmem_free(esp->es_ports, esp->es_port_cnt * sizeof (edge_port_t));
}


static void
edge_init_port_params(edge_state_t *esp)
{
	int		i;

	if (esp->es_is_ti) {
		for (i = 0; i < esp->es_port_cnt; i++) {
			edgeti_init_port_params(&esp->es_ports[i]);
		}
	}
}


/*
 * free descriptor tree
 */
static void
edge_free_descr_tree(edge_state_t *esp)
{
	usb_free_descr_tree(esp->es_dip, esp->es_dev_data);

}


/*
 * register/unregister USB event callbacks
 */
static int
edge_register_events(edge_state_t *esp)
{
	return (usb_register_event_cbs(esp->es_dip, esp->es_usb_events, 0));
}


static void
edge_unregister_events(edge_state_t *esp)
{
	usb_unregister_event_cbs(esp->es_dip, esp->es_usb_events);
}


static void
edge_set_dev_state_online(edge_state_t *esp)
{
	esp->es_dev_state = USB_DEV_ONLINE;
}


/*
 * hotplug
 * -------
 *
 *
 * restore device state after CPR resume or reconnect
 */
static int
edge_restore_device_state(edge_state_t *esp)
{
	int	state;
	uint_t	old_port_cnt = esp->es_port_cnt;

	mutex_enter(&esp->es_mutex);
	state = esp->es_dev_state;
	mutex_exit(&esp->es_mutex);

	if ((state != USB_DEV_DISCONNECTED) && (state != USB_DEV_SUSPENDED)) {

		return (state);
	}

	if (esp->es_is_ti) {
		if (edgeti_restore_device(esp) != USB_SUCCESS) {
			/*
			 * Return same state for the first TI reconnect event.
			 * Once the FW download is complete we will get
			 * another reconnect event, then we check the device.
			 */

			return (state);
		}
	}

	if (usb_check_same_device(esp->es_dip, esp->es_lh, USB_LOG_L0,
	    DPRINT_MASK_ALL, USB_CHK_ALL, NULL) != USB_SUCCESS) {
		mutex_enter(&esp->es_mutex);
		state = esp->es_dev_state = USB_DEV_DISCONNECTED;
		mutex_exit(&esp->es_mutex);

		return (state);
	}

	if (state == USB_DEV_DISCONNECTED) {
		USB_DPRINTF_L0(DPRINT_HOTPLUG, esp->es_lh,
		    "device has been reconnected but data may have been lost");
	}

	/* init entire device */
	if (edge_attach_dev(esp) != USB_SUCCESS) {

		return (state);
	}

	/* port count should be the same (paranoid) */
	if (esp->es_port_cnt != old_port_cnt) {

		return (state);
	}

	if (edge_reconnect_pipes(esp) != USB_SUCCESS) {

		return (state);
	}

	/*
	 * init device state
	 */
	mutex_enter(&esp->es_mutex);
	state = esp->es_dev_state = USB_DEV_ONLINE;
	esp->es_rxp.rx_state = EDGE_RX_NEXT_HDR0;
	mutex_exit(&esp->es_mutex);

	/*
	 * now restore each open port
	 */
	(void) edge_restore_ports_state(esp);

	return (state);
}


/*
 * restore ports state after CPR resume or reconnect
 */
static int
edge_restore_ports_state(edge_state_t *esp)
{
	edge_port_t	*ep;
	int		rval = USB_SUCCESS;
	int		err;
	int		i;

	for (i = 0; i < esp->es_port_cnt; i++) {
		ep = &esp->es_ports[i];
		/*
		 * only care about open ports
		 */
		mutex_enter(&ep->ep_mutex);
		if (ep->ep_state != EDGE_PORT_OPEN) {
			mutex_exit(&ep->ep_mutex);
			continue;
		}
		mutex_exit(&ep->ep_mutex);

		sema_p(&esp->es_pipes_sema);
		/* open hardware serial port */
		if (esp->es_is_ti) {
			err = edgeti_open_hw_port(ep, B_FALSE);
		} else {
			err = edgesp_open_hw_port(ep, B_FALSE);
		}
		sema_v(&esp->es_pipes_sema);
		if (err != USB_SUCCESS) {
			USB_DPRINTF_L2(DPRINT_HOTPLUG, ep->ep_lh,
			    "edge_restore_ports_state: failed");
			rval = err;
		}
	}

	return (rval);
}


/*
 * power management
 * ----------------
 *
 *
 * create PM components
 */
static int
edge_create_pm_components(edge_state_t *esp)
{
	dev_info_t	*dip = esp->es_dip;
	edge_pm_t	*pm;
	uint_t		pwr_states;

	pm = esp->es_pm = kmem_zalloc(sizeof (edge_pm_t), KM_SLEEP);
	pm->pm_cur_power = USB_DEV_OS_FULL_PWR;

	if (usb_create_pm_components(dip, &pwr_states) != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_PM, esp->es_lh,
		    "edge_create_pm_components: failed");

		return (USB_SUCCESS);
	}

	pm->pm_wakeup_enabled = (usb_handle_remote_wakeup(dip,
	    USB_REMOTE_WAKEUP_ENABLE) == USB_SUCCESS);
	pm->pm_pwr_states = (uint8_t)pwr_states;

	(void) pm_raise_power(dip, 0, USB_DEV_OS_FULL_PWR);

	return (USB_SUCCESS);
}


/*
 * destroy PM components
 */
static void
edge_destroy_pm_components(edge_state_t *esp)
{
	edge_pm_t	*pm = esp->es_pm;
	dev_info_t	*dip = esp->es_dip;
	int		rval;

	if (esp->es_dev_state != USB_DEV_DISCONNECTED) {
		if (pm->pm_wakeup_enabled) {
			(void) pm_raise_power(dip, 0, USB_DEV_OS_FULL_PWR);

			rval = usb_handle_remote_wakeup(dip,
			    USB_REMOTE_WAKEUP_DISABLE);
			if (rval != USB_SUCCESS) {
				USB_DPRINTF_L2(DPRINT_PM, esp->es_lh,
				    "edge_destroy_pm_components: disable "
				    "remote wakeup failed, rval=%d", rval);
			}
		}

		(void) pm_lower_power(dip, 0, USB_DEV_OS_PWR_OFF);
	}
	kmem_free(pm, sizeof (edge_pm_t));
	esp->es_pm = NULL;
}


/*
 * mark device busy and raise power
 */
static int
edge_pm_set_busy(edge_state_t *esp)
{
	edge_pm_t	*pm = esp->es_pm;
	dev_info_t	*dip = esp->es_dip;

	USB_DPRINTF_L4(DPRINT_PM, esp->es_lh, "edge_pm_set_busy");

	mutex_enter(&esp->es_mutex);
	/* if already marked busy, just increment the counter */
	if (pm->pm_busy_cnt++ > 0) {
		mutex_exit(&esp->es_mutex);

		return (USB_SUCCESS);
	}

	(void) pm_busy_component(dip, 0);

	if (pm->pm_cur_power == USB_DEV_OS_FULL_PWR) {
		mutex_exit(&esp->es_mutex);

		return (USB_SUCCESS);
	}

	/* need to raise power  */
	pm->pm_raise_power = B_TRUE;
	mutex_exit(&esp->es_mutex);

	(void) pm_raise_power(dip, 0, USB_DEV_OS_FULL_PWR);

	mutex_enter(&esp->es_mutex);
	pm->pm_raise_power = B_FALSE;
	mutex_exit(&esp->es_mutex);

	return (USB_SUCCESS);
}


/*
 * mark device idle
 */
static void
edge_pm_set_idle(edge_state_t *esp)
{
	edge_pm_t	*pm = esp->es_pm;
	dev_info_t	*dip = esp->es_dip;

	USB_DPRINTF_L4(DPRINT_PM, esp->es_lh, "edge_pm_set_idle");

	/*
	 * if more ports use the device, do not mark as yet
	 */
	mutex_enter(&esp->es_mutex);
	if (--pm->pm_busy_cnt > 0) {
		mutex_exit(&esp->es_mutex);

		return;
	}

	(void) pm_idle_component(dip, 0);

	mutex_exit(&esp->es_mutex);
}


/*
 * Functions to handle power transition for OS levels 0 -> 3
 */
static int
edge_pwrlvl0(edge_state_t *esp)
{
	int	rval;
	edge_pipe_t	*intr_pipe = &esp->es_intr_pipe;

	USB_DPRINTF_L4(DPRINT_PM, esp->es_lh, "edge_pwrlvl0");

	switch (esp->es_dev_state) {
	case USB_DEV_ONLINE:
		/* issue USB D3 command to the device */
		rval = usb_set_device_pwrlvl3(esp->es_dip);
		ASSERT(rval == USB_SUCCESS);

		mutex_exit(&esp->es_mutex);
		edge_pipe_stop_polling(intr_pipe);
		mutex_enter(&esp->es_mutex);
		esp->es_dev_state = USB_DEV_PWRED_DOWN;
		esp->es_pm->pm_cur_power = USB_DEV_OS_PWR_OFF;

		/* FALLTHRU */
	case USB_DEV_DISCONNECTED:
	case USB_DEV_SUSPENDED:
		/* allow a disconnect/cpr'ed device to go to lower power */

		return (USB_SUCCESS);
	case USB_DEV_PWRED_DOWN:
	default:
		USB_DPRINTF_L2(DPRINT_PM, esp->es_lh,
		    "edge_pwrlvl0: illegal device state");

		return (USB_FAILURE);
	}
}


static int
edge_pwrlvl1(edge_state_t *esp)
{
	USB_DPRINTF_L4(DPRINT_PM, esp->es_lh, "edge_pwrlvl1");

	/* issue USB D2 command to the device */
	(void) usb_set_device_pwrlvl2(esp->es_dip);

	return (USB_FAILURE);
}


static int
edge_pwrlvl2(edge_state_t *esp)
{
	USB_DPRINTF_L4(DPRINT_PM, esp->es_lh, "edge_pwrlvl2");

	/* issue USB D1 command to the device */
	(void) usb_set_device_pwrlvl1(esp->es_dip);

	return (USB_FAILURE);
}


static int
edge_pwrlvl3(edge_state_t *esp)
{
	int	rval;

	USB_DPRINTF_L4(DPRINT_PM, esp->es_lh, "edge_pwrlvl3");

	switch (esp->es_dev_state) {
	case USB_DEV_PWRED_DOWN:
		/* Issue USB D0 command to the device here */
		rval = usb_set_device_pwrlvl0(esp->es_dip);
		ASSERT(rval == USB_SUCCESS);

		mutex_exit(&esp->es_mutex);
		edge_pipe_start_polling(&esp->es_intr_pipe);
		mutex_enter(&esp->es_mutex);

		esp->es_dev_state = USB_DEV_ONLINE;
		esp->es_pm->pm_cur_power = USB_DEV_OS_FULL_PWR;

		/* FALLTHRU */
	case USB_DEV_ONLINE:
		/* we are already in full power */

		/* FALLTHRU */
	case USB_DEV_DISCONNECTED:
	case USB_DEV_SUSPENDED:

		return (USB_SUCCESS);
	default:
		USB_DPRINTF_L2(DPRINT_PM, esp->es_lh,
		    "edge_pwrlvl3: illegal device state");

		return (USB_FAILURE);
	}
}


/*
 * pipe operations
 * ---------------
 *
 * XXX Edgeport seem to malfunction after the pipes are closed
 * and reopened again (does not respond to OPEN_PORT command).
 * so we open them once in attach
 */
static int
edge_attach_pipes(edge_state_t *esp)
{
	if (!esp->es_is_ti) {

		return (edgesp_open_pipes_serialized(esp));
	} else {

		return (edgeti_open_dev_pipes(esp));
	}
}


void
edge_detach_pipes(edge_state_t *esp)
{
	edge_pipe_stop_polling(&esp->es_intr_pipe);

	if (!esp->es_is_ti) {

		edgesp_close_pipes_serialized(esp);
	} else {
		edgeti_close_dev_pipes(esp);
	}
}


/*
 * during device disconnect/suspend, close pipes if they are open.
 * unlike edge_close_pipes_serialized(), pipes user counter is preserved.
 */
static void
edge_disconnect_pipes(edge_state_t *esp)
{
	sema_p(&esp->es_pipes_sema);
	edge_pipe_stop_polling(&esp->es_intr_pipe);
	edge_close_pipes(esp);
	sema_v(&esp->es_pipes_sema);
}


/*
 * during device reconnect/resume, close pipes if they are open.
 * unlike edgesp_open_pipes_serialized(), pipes user counter is preserved.
 */
static int
edge_reconnect_pipes(edge_state_t *esp)
{
	int	rval = USB_SUCCESS;

	sema_p(&esp->es_pipes_sema);
	rval = edge_open_pipes(esp);

	/* Start polling on the INTR pipe */
	edge_pipe_start_polling(&esp->es_intr_pipe);

	sema_v(&esp->es_pipes_sema);

	return (rval);
}


/*
 * data transfer routines
 * ----------------------
 *
 *
 * start data transmit
 */
void
edge_tx_start(edge_port_t *ep, int *xferd)
{
	edge_state_t	*esp = ep->ep_esp;

	ASSERT(!mutex_owned(&esp->es_mutex));
	ASSERT(mutex_owned(&ep->ep_mutex));
	ASSERT(ep->ep_state != EDGE_PORT_CLOSED);

	USB_DPRINTF_L4(DPRINT_OUT_PIPE, ep->ep_lh, "edge_tx_start");

	if (xferd) {
		*xferd = 0;
	}
	if ((ep->ep_flags & EDGE_PORT_TX_STOPPED) || (ep->ep_tx_mp == NULL)) {

		return;
	}
	ASSERT(MBLKL(ep->ep_tx_mp) > 0);

	if (esp->es_is_ti) {
		edgeti_tx_start(ep, xferd);
	} else {
		edgesp_tx_start(ep, xferd);
	}
}


/*
 * copy no more than 'len' bytes from mblk chain to transmit mblk 'data'.
 * return number of bytes copied
 */
int
edge_tx_copy_data(edge_port_t *ep, mblk_t *data, int len)
{
	mblk_t		*mp;	/* current msgblk */
	int		copylen; /* # of bytes to copy from 'mp' to 'data' */
	int		data_len = 0;

	ASSERT(mutex_owned(&ep->ep_mutex));

	while ((data_len < len) && ep->ep_tx_mp) {
		mp = ep->ep_tx_mp;
		copylen = min(MBLKL(mp), len - data_len);
		bcopy(mp->b_rptr, data->b_wptr, copylen);

		mp->b_rptr += copylen;
		data->b_wptr += copylen;
		data_len += copylen;

		if (MBLKL(mp) < 1) {
			ep->ep_tx_mp = unlinkb(mp);
			freeb(mp);
		} else {
			ASSERT(data_len == len);
		}
	}

	return (data_len);
}


/*
 * wait until local tx buffer drains.
 * 'timeout' is in seconds, zero means wait forever
 */
static int
edge_wait_tx_drain(edge_port_t *ep, int timeout)
{
	clock_t	until;
	int	over = 0;

	until = ddi_get_lbolt() + drv_usectohz(1000000 * timeout);

	while (ep->ep_tx_mp && !over) {
		if (timeout > 0) {
			over = (cv_timedwait_sig(&ep->ep_tx_cv,
			    &ep->ep_mutex, until) <= 0);
		} else {
			over = (cv_wait_sig(&ep->ep_tx_cv, &ep->ep_mutex) == 0);
		}
	}

	return ((ep->ep_tx_mp == NULL) ? USB_SUCCESS : USB_FAILURE);
}


/*
 * misc routines
 * -------------
 *
 *
 * returns 0 if device is not online, != 0 otherwise
 */
int
edge_dev_is_online(edge_state_t *esp)
{
	int	rval;

	mutex_enter(&esp->es_mutex);
	rval = (esp->es_dev_state == USB_DEV_ONLINE);
	mutex_exit(&esp->es_mutex);

	return (rval);
}


/*
 * convert register values to termio modem controls
 */
static int
edge_reg2mctl(uint8_t r_mcr, uint8_t r_msr)
{
	int	val = 0;

	if (r_mcr & MCR_RTS) {
		val |= TIOCM_RTS;
	}
	if (r_mcr & MCR_DTR) {
		val |= TIOCM_DTR;
	}
	if (r_msr & MSR_CD) {
		val |= TIOCM_CD;
	}
	if (r_msr & MSR_CTS) {
		val |= TIOCM_CTS;
	}
	if (r_msr & MSR_DSR) {
		val |= TIOCM_DSR;
	}

	return (val);
}


/*
 * convert termio model controls to register values
 */
static void
edge_mctl2reg(int mask, int val, uint8_t *r_mcr)
{
	if (mask & TIOCM_RTS) {
		if (val & TIOCM_RTS) {
			*r_mcr |= MCR_RTS;
		} else {
			*r_mcr &= ~MCR_RTS;
		}
	}
	if (mask & TIOCM_DTR) {
		if (val & TIOCM_DTR) {
			*r_mcr |= MCR_DTR;
		} else {
			*r_mcr &= ~MCR_DTR;
		}
	}
}


/*
 * link a message block to tail of message
 * account for the case when message is null
 */
void
edge_put_tail(mblk_t **mpp, mblk_t *bp)
{
	if (*mpp) {
		linkb(*mpp, bp);
	} else {
		*mpp = bp;
	}
}


/*
 * put a message block at the head of the message
 * account for the case when message is null
 */
void
edge_put_head(mblk_t **mpp, mblk_t *bp)
{
	if (*mpp) {
		linkb(bp, *mpp);
	}
	*mpp = bp;
}
