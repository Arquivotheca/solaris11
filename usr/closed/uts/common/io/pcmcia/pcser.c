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
 * Driver for PCMCIA asynchronous modem
 *
 * This modem is based on a 16550 UART and it's variants.
 *
 * The XXX_DEBUG defines are now in pcser_var.h
 */

#if defined(DEBUG)
#define	PCSER_DEBUG
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/kmem.h>
#include <sys/termios.h>
#include <sys/termio.h>
#include <sys/termiox.h>
#include <sys/stermio.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/tty.h>
#include <sys/ptyvar.h>
#include <sys/cred.h>
#include <sys/stat.h>
#include <sys/ioccom.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/policy.h>

#include <sys/strtty.h>
#include <sys/ksynch.h>

/*
 * PCMCIA and DDI related header files
 */
#include <sys/pccard.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/pcmcia.h>
#include <sys/pcic_reg.h>
#include <sys/pcic_var.h>
/*
 * XXX - get rid of all this
 */
#define	PCSER_DRIVERID		"PCMCIA Serial Card Driver V2.0"

/*
 * pcser-related header files
 */
#include <sys/pcmcia/pcser_io.h>
#include <sys/pcmcia/pcser_var.h>
#include <sys/pcmcia/pcser_reg.h>
#include <sys/pcmcia/pcser_conf.h>

#ifdef	PCSER_DEBUG
int pcser_debug = 0;
int pcser_debug_events = 1;
#endif

/*
 * The pcser_unid_irq_max variable is used to set the maximum
 *	number of unidentified IRQ's that we accept in pcser_poll.
 */
int pcser_unid_irq_max = PCSER_UNID_IRQ_MAX;

/*
 * Card Services related functions and variables
 */
static int pcser_event(event_t, int, event_callback_args_t *);
static int pcser_card_insertion(pcser_unit_t *, int);
static int pcser_card_removal(pcser_unit_t *, int);
static int pcser_card_ready(pcser_unit_t *, int);
static void pcser_card_ready_timeout(void *);
static void pcser_readywait_timeout(void *);

/*
 * external data of interest to us
 */
extern kcondvar_t lbolt_cv;
char *pcser_name = PCSER_NAME;

/*
 * local driver data
 */
void *pcser_soft_state_p = NULL;

/*
 * uart functions
 * XXX - prototype all of these!!
 */
static int pcser_open(queue_t *, dev_t *, int, int, cred_t *);
static int pcser_close(queue_t *q, int flag, cred_t *);
static int pcser_wput(queue_t *q, mblk_t *mp);
static int pcser_ioctl(queue_t *q, mblk_t *mp);
static void pcser_reioctl(void *);
static void pcser_restart(void *);
static void pcser_start(pcser_line_t *line);
static void pcser_param(pcser_line_t *line);
static int pcser_xmit(pcser_unit_t *, uchar_t, int);
static int pcser_rcv(pcser_unit_t *);
static int pcser_rcvex(pcser_unit_t *, uchar_t, uchar_t);
static int pcser_modem(pcser_unit_t *, uchar_t, int);
static void pcser_drainsilo(/*pcser_line_t *line */);
static void pcser_draintimeout(void *arg);
static void silocopy(pcser_line_t *line, uchar_t *buf, int count);
static void pcser_xwait(pcser_line_t *line);
static void pcser_timeout(void *arg);
static uint32_t pcser_poll(pcser_unit_t *);
static uint32_t pcser_softint(pcser_unit_t *);
static void pcser_txflush(pcser_line_t *line);
static int pcser_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int pcser_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int pcser_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);
static void pcser_ignore_cd_timeout(void *);
static void pcser_auctl(pcser_line_t *, int);
static int pcser_set_baud(pcser_line_t *, tcflag_t);
static tcflag_t pcser_convert_speed(pcser_unit_t *, tcflag_t);
static void OUTB_DELAY();
static void pcser_dtr_off(pcser_line_t *);
static void pcser_dtr_on(pcser_line_t *);

/*
 * CIS related functions
 */
int pcser_parse_cis(pcser_unit_t *, pcser_cftable_t **);
void pcser_destroy_cftable_list(pcser_cftable_t **);
int pcser_sort_cftable_list(pcser_cftable_t **);

#ifdef	PCSER_DEBUG
void pcser_display_cftable_list(pcser_cftable_t *, int);
void pcser_show_baud(tcflag_t, char *);
static void pcser_debug_report_event(pcser_unit_t *pcser,
	event_t event, int priority);
#endif

#ifndef	USE_MACRO_RTSCTS
static void CHECK_RTS_OFF(pcser_line_t *);
static void CHECK_RTS_ON(pcser_line_t *);
#endif	/* USE_MACRO_RTSCTS */

/*
 * misc STREAMS data structs
 */
static struct module_info pcserm_info = {
	0,		/* module id number */
	"pcser",	/* module name */
	0,		/* min packet size */
	INFPSZ,		/* max packet size, was INFPSZ */
	2048,		/* hi-water mark, was 2048 */
	128,		/* lo-water mark, was 128 */
};

static struct qinit pcser_rinit = {
	putq,		/* put proc */
	NULL,		/* service proc */
	pcser_open,
	pcser_close,
	NULL,		/* admin - "for 3bnet only" (see stream.h) */
	&pcserm_info,
	NULL,		/* statistics */
};

static struct qinit pcser_winit = {
	pcser_wput,
	NULL,
	NULL,
	NULL,
	NULL,
	&pcserm_info,
	NULL,
};

/*
 * streamtab
 */
struct streamtab pcser_stab = {	/* used in cdevsw */
	&pcser_rinit,
	&pcser_winit,
	NULL,
	NULL,
/*	pcser_modlist, */
};

/*
 * pcser driver cb_ops and dev_ops
 */

static struct cb_ops pcser_cb_ops = {
	nodev,		/* open */
	nodev,		/* close */
	nodev,		/* strategy */
	nodev,		/* print */
	nodev,		/* dump */
	nodev,		/* read */
	nodev,		/* write */
	nodev,		/* ioctl */
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* poll */
	ddi_prop_op,	/* cb_prop op */
	&pcser_stab,	/* stream tab */
	D_MP		/* Driver Compatability Flags */
};

static struct dev_ops pcser_dev_ops = {
	DEVO_REV,		/* devo_rev */
	0,			/* refcnt */
	pcser_getinfo,		/* get_dev_info */
	nulldev,		/* identify */
	nulldev,		/* probe */
	pcser_attach,		/* attach */
	pcser_detach,		/* detach */
	nodev,			/* reset */
	&pcser_cb_ops,		/* Driver Ops */
	(struct bus_ops *)0,	/* Bus Operations */
	NULL,			/* power */
	ddi_quiesce_not_supported,	/* devo_quiesce */
};

/*
 * Module linkage information for the kernel
 */

static struct modldrv modldrv = {
	&mod_driverops,		/* Type of Module = Driver */
	PCSER_DRIVERID,		/* Driver Identifier string. */
	&pcser_dev_ops,		/* Driver Ops. */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

/*
 * Module Initialization functions.
 */

int
_init(void)
{
	int stat;

	/* Allocate soft state */
	if ((stat = ddi_soft_state_init(&pcser_soft_state_p,
	    sizeof (pcser_unit_t), N_PCSER)) != DDI_SUCCESS)
		return (stat);

	if ((stat = mod_install(&modlinkage)) != 0)
		ddi_soft_state_fini(&pcser_soft_state_p);

	return (stat);
}

int
_info(struct modinfo *infop)
{

	return (mod_info(&modlinkage, infop));
}

int
_fini(void)
{
	int stat = 0;

	if ((stat = mod_remove(&modlinkage)) != 0)
		return (stat);

	ddi_soft_state_fini(&pcser_soft_state_p);

	return (stat);
}


/*
 * Wait for minor nodes to be created before returning from attach,
 * with a 5 sec. timeout to avoid hangs should an error occur.
 */
static void
pcser_minor_wait(pcser_unit_t *pcser)
{
	clock_t	timeout;

	timeout = ddi_get_lbolt() + drv_usectohz(5000000);
	mutex_enter(&pcser->event_hilock);
	while (((pcser->flags & PCSER_MAKEDEVICENODE) == 0) &&
	    (((pcser->card_state & PCSER_READY_ERR)) == 0)) {
		if (cv_timedwait(&pcser->readywait_cv, &pcser->event_hilock,
		    timeout) == (clock_t)-1)
			break;
	}
	mutex_exit(&pcser->event_hilock);
}

/*
 * pcser_attach() - performs board initialization
 *
 * This routine initializes the pcser driver and the board.
 *
 *	Returns:	DDI_SUCCESS, if able to attach.
 *			DDI_FAILURE, if unable to attach.
 */
static int
pcser_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	pcser_unit_t *pcser;
	pcser_line_t *line;
	int ret;
	client_reg_t client_reg;
	sockmask_t sockmask;
	map_log_socket_t map_log_socket;
	get_status_t get_status;

	/*
	 * resume from a checkpoint
	 */
	if (cmd == DDI_RESUME) {
		return (DDI_SUCCESS);
	}

	/*
	 * make sure we're only being asked to do an attach
	 */
	if (cmd != DDI_ATTACH) {
		cmn_err(CE_CONT, "pcser_attach: cmd != DDI_ATTACH\n");
		return (DDI_FAILURE);
		/* NOTREACHED */
	}

	/* Allocate soft state associated with this instance. */
	if (ddi_soft_state_zalloc(pcser_soft_state_p, ddi_get_instance(dip)) !=
	    DDI_SUCCESS) {
		cmn_err(CE_CONT, "pcser_attach: Unable to alloc state\n");
		return (DDI_FAILURE);
	}

	pcser = ddi_get_soft_state(pcser_soft_state_p, ddi_get_instance(dip));
	pcser->dip = dip;
	ddi_set_driver_private(dip, pcser);

	/*
	 * clear the per-unit flags field
	 */
	pcser->flags = 0;

	pcser->instance = ddi_get_instance(dip);

	/*
	 * Register with Card Services
	 * Note that we set CS_EVENT_CARD_REMOVAL_LOWP so that we get
	 *	low priority CS_EVENT_CARD_REMOVAL events as well.
	 */
	client_reg.Attributes = (INFO_IO_CLIENT |
	    INFO_CARD_SHARE |
	    INFO_CARD_EXCL);
	client_reg.EventMask = (CS_EVENT_CARD_INSERTION |
	    CS_EVENT_CARD_REMOVAL |
	    CS_EVENT_CARD_REMOVAL_LOWP |
	    CS_EVENT_PM_RESUME |
	    CS_EVENT_PM_SUSPEND |
	    CS_EVENT_CLIENT_INFO |
	    CS_EVENT_REGISTRATION_COMPLETE);
	client_reg.event_handler = (csfunction_t *)pcser_event;
	client_reg.event_callback_args.client_data = pcser;
	client_reg.Version = _VERSION(2, 1);
	client_reg.dip = dip;
	(void) strcpy(client_reg.driver_name, pcser_name);
	if ((ret = csx_RegisterClient(&pcser->client_handle,
	    &client_reg)) != CS_SUCCESS) {
		error2text_t cft;

		cft.item = ret;
		(void) csx_Error2Text(&cft);
		cmn_err(CE_CONT, "pcser_attach: instance %d "
		    "RegisterClient failed %s\n",
		    pcser->instance, cft.text);
		(void) pcser_detach(dip, DDI_DETACH);
		return (DDI_FAILURE);
	}

#ifdef	PCSER_DEBUG
	if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1)
		cmn_err(CE_CONT, "pcser_attach[%d]: RegisterClient "
		    "client_handle 0x%x\n",
		    (int)pcser->instance,
		    (int)pcser->client_handle);
#endif

	/*
	 * Get logical socket number and store in pcser struct
	 */

	if ((ret = csx_MapLogSocket(pcser->client_handle,
	    &map_log_socket)) != CS_SUCCESS) {
		error2text_t cft;

		cft.item = ret;
		(void) csx_Error2Text(&cft);
		cmn_err(CE_CONT, "pcser_attach: instance %d "
		    "MapLogSocket failed %s\n",
		    pcser->instance, cft.text);
	}

	pcser->sn = map_log_socket.PhySocket;

#ifdef	PCSER_DEBUG
	if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1)
		cmn_err(CE_CONT,
		    "pcser_attach[%d]: MapLogSocket for socket %d\n",
		    (int)pcser->instance, (int)pcser->sn);
#endif

	pcser->flags |= PCSER_REGCLIENT;

	ret = ddi_add_softintr(dip, PCSER_SOFT_PREF, &pcser->softint_id,
	    &pcser->soft_blk_cookie, (ddi_idevice_cookie_t *)0,
	    (uint32_t (*)(char *))pcser_softint, (caddr_t)pcser);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_CONT, "pcser_attach: unable to install "
		    "soft interrupt\n");
		(void) pcser_detach(dip, DDI_DETACH);
		return (ret);
	}

	pcser->flags |= PCSER_SOFTINTROK;

	/*
	 * Setup the mutexii and condition variables.
	 */
	mutex_init(&pcser->event_hilock, NULL, MUTEX_DRIVER,
	    *(client_reg.iblk_cookie));

	mutex_init(&pcser->noirq_mutex, NULL, MUTEX_DRIVER,
	    (void *)pcser->soft_blk_cookie);

	pcser->pcser_mutex = &pcser->noirq_mutex;

	cv_init(&pcser->readywait_cv, NULL, CV_DRIVER, NULL);

	line = &pcser->line;
	mutex_init(&line->line_mutex, NULL, MUTEX_DRIVER,
	    (void *)pcser->soft_blk_cookie);
	cv_init(&line->cvp, NULL, CV_DRIVER, NULL);

	line = &pcser->control_line;
	mutex_init(&line->line_mutex, NULL, MUTEX_DRIVER,
	    (void *)pcser->soft_blk_cookie);
	cv_init(&line->cvp, NULL, CV_DRIVER, NULL);

	pcser->flags |= PCSER_DIDLOCKS;

	line = &pcser->line;

	line->state = 0;
	line->flags = 0;
	line->pcser = pcser;		/* ugh... kludge */
	line->pcser_timeout_id = 0;
	line->pcser_draintimeout_id = 0;
	line->restart_timeout_id = 0;
	line->pcser_silosize = PCSER_SILOSIZE;	/* size of soft rx silo */
	line->pcser_txcount = 0;
	line->ignore_cd_timeout_id = 0;
	line->pcser_ignore_cd_time = PCSER_IGNORE_CD_TIMEOUT;

	line->saved_state = PCSER_AUDIO_ON; /* XXX default to audio on */

	/*
	 * allocate and set up the Tx buffer
	 */
	line->pcser_max_txbufsize = MAX_TX_BUF_SIZE;
	line->pcser_txbufsize = MAX_TX_BUF_SIZE;
	line->pcser_txbuf = kmem_zalloc(line->pcser_max_txbufsize, KM_SLEEP);

	/*
	 * the following code is used to initialize the driver statistics
	 *	structure for each board
	 */
	bzero(&line->pcser_stats, sizeof (struct pcser_stats_t));
	line->pcser_stats.nqfretry = NQFRETRY;


	pcser->control_line.state = 0;

	/* pcser->flags |= PCSER_ATTACHOK; XXX - was here */

	/*
	 * We may not get the CARD_READY event until after we leave
	 *	this function.
	 */
	pcser->card_state |= PCSER_READY_WAIT;
	pcser->card_state &= ~PCSER_READY_ERR;

	/*
	 * After the RequestSocketMask call,
	 * we can start receiving events
	 */
	sockmask.EventMask = (CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL);

	if ((ret = csx_RequestSocketMask(pcser->client_handle,
	    &sockmask)) != CS_SUCCESS) {
		error2text_t cft;

		cft.item = ret;
		(void) csx_Error2Text(&cft);

		cmn_err(CE_CONT, "pcser_attach: socket %d RequestSocketMask "
		    "failed %s\n",
		    (int)pcser->sn, cft.text);
		(void) pcser_detach(dip, DDI_DETACH);
		return (DDI_FAILURE);
	}

	pcser->flags |= PCSER_REQSOCKMASK;

#ifdef	PCSER_DEBUG
	if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1)
		cmn_err(CE_CONT, "pcser_attach[%d]: RequestSocketMask OK\n",
		    pcser->instance);
#endif

	/*
	 * See if there's a card that we can control in the socket. If
	 *	there is, see if we've already flagged it as READY.
	 *	If not, we have to wait for it to become READY or if
	 *	this attach is triggered by an open, that open will
	 *	fail.
	 */
	/* XXX function return value ignored */
	(void) csx_GetStatus(pcser->client_handle, &get_status);

	mutex_enter(&pcser->event_hilock);

	if (get_status.CardState & CS_EVENT_CARD_INSERTION) {
		if (CARD_INSERT_CHECK(pcser) == 0) {
		pcser->readywait_timeout_id = timeout(pcser_readywait_timeout,
		    (caddr_t)pcser,
		    PCSER_READYWAIT_TIMEOUT);
		while (CARD_INSERT_CHECK(pcser) == 0) {
			cv_wait(&pcser->readywait_cv, &pcser->event_hilock);
		}

		if (!CARD_PRESENT(pcser)) {
#ifdef  PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_READY)
				cmn_err(CE_CONT,
				    "pcser_attach: CARD not found\n");
#endif
			pcser->card_state &=
			    ~(PCSER_READY_WAIT | PCSER_READY_ERR);
			mutex_exit(&pcser->event_hilock);
			cmn_err(CE_WARN, "!PC card not ready\n");
			(void) pcser_detach(dip, DDI_DETACH);
			return (DDI_FAILURE);
		} else {
#ifdef  PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_READY)
			cmn_err(CE_CONT, "pcser_attach: CARD found\n");
#endif
		} /* if !CARD_PRESENT */
		} /* if !CARD_PRESENT */
	} /* if (CS_EVENT_CARD_INSERTION) */

	mutex_exit(&pcser->event_hilock);

	/*
	 * Ensure that we allow some time for minor nodes to
	 * be created before returning from attach.
	 */
	pcser_minor_wait(pcser);

#ifdef	CBAUDEXT
	/*
	 * Check for the hardware flow control override property.
	 *	Normally, on a pre-2.5 system, CRTSCTS is treated
	 *	as both incoming (RTS) and outgoing (CTS) hardware
	 *	flow control, and on a 2.5 and greater system,
	 *	CRTSCTS is used to enable outgoing (CTS) flow control
	 *	and CRTSXOFF is used to enable incoming (RTS) flow
	 *	control, however some applications may not properly
	 *	set up both CRTSCTS and CRTSXOFF when they want to
	 *	do bidirectional hardware flow control. TO get
	 *	around this, if the PCSER_DUALHW_FLOW property is
	 *	present, we alias CRTSCTS and CRTSXOFF togther
	 *	so that setting one will automatically set the
	 *	other one. A hack? Yes. Will it provide happier
	 *	customers? Absolutely!
	 * We only check this on systems where CBAUDEXT is defined.
	 */
	if (ddi_getprop(DDI_DEV_T_ANY, dip, (DDI_PROP_CANSLEEP |
	    DDI_PROP_NOTPROM), PCSER_DUALHW_FLOW, NULL)) {

		pcser->flags |= PCSER_USE_DUALFLOW;
		cmn_err(CE_CONT, "pcser: socket %d using dual flow mode\n",
		    (int)pcser->sn);

	} /* if (ddi_getprop) */
#endif

#ifndef	CBAUDEXT
	/*
	 * If CBAUDEXT is not defined, then that means that we're being
	 *	built on a system without high speed extensions. If the
	 *	PCSER_HIGHSPEED_PROP property is present in the .conf
	 *	file then that means that the user wants to use the
	 *	higher speeds. See comments in pcser_convert_speed.
	 */
	if (ddi_getprop(DDI_DEV_T_ANY, dip, (DDI_PROP_CANSLEEP |
	    DDI_PROP_NOTPROM), PCSER_HIGHSPEED_PROP, NULL)) {

		pcser->flags |= PCSER_USE_HIGHSPEED;
		cmn_err(CE_CONT, "pcser: socket %d using high speed mode\n",
		    (int)pcser->sn);

	} /* if (ddi_getprop) */
#endif

	ddi_report_dev(dip);

	/*
	 * XXX - moved from above
	 */
	pcser->flags |= PCSER_ATTACHOK;

	return (DDI_SUCCESS);
}

/*
 * static int
 * pcser_detach() - Deallocate kernel resources associate with this instance
 *	of the driver.
 *
 *	Returns:	DDI_SUCCESS, if able to detach.
 *			DDI_FAILURE, if unable to detach.
 */
static int
pcser_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	pcser_unit_t *pcser;
	pcser_line_t *line;
	int ret;


	/*
	 * suspend
	 */
	if (cmd == DDI_SUSPEND)
		return (DDI_SUCCESS);

	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);

	if ((pcser = ddi_get_soft_state(pcser_soft_state_p,
	    ddi_get_instance(dip))) == NULL) {
		return (DDI_FAILURE);
	}

	pcser->flags &= ~PCSER_ATTACHOK;

	/*
	 * Call pcser_card_removal to do any final card cleanup
	 */
	if (pcser->flags & PCSER_DIDLOCKS)
		(void) pcser_card_removal(pcser, CS_EVENT_PRI_LOW);

	/*
	 * Remove the various timers that may still be running around
	 *	waiting to go off. We UNTIMEOUT the ready_timeout_id
	 *	in pcser_card_removal().
	 */
	line = &pcser->line;
	UNTIMEOUT(pcser->readywait_timeout_id);
	UNTIMEOUT(line->pcser_timeout_id);
	UNTIMEOUT(line->pcser_draintimeout_id);
	UNTIMEOUT(line->ignore_cd_timeout_id);
	UNTIMEOUT(line->restart_timeout_id);

	/*
	 * Release our socket mask - note that we can't do much
	 *	if we fail these calls other than to note that
	 *	the system will probably panic shortly.  Perhaps
	 *	we should fail the detach in the case where these
	 *	CS calls fail?
	 */
	if (pcser->flags & PCSER_REQSOCKMASK) {
		release_socket_mask_t rsm;

		if ((ret = csx_ReleaseSocketMask(pcser->client_handle,
		    &rsm)) != CS_SUCCESS) {
		error2text_t cft;

		cft.item = ret;
		(void) csx_Error2Text(&cft);

		cmn_err(CE_CONT, "pcser_detach: socket %d "
		    "ReleaseSocketMask failed %s\n",
		    (int)pcser->sn, cft.text);
		} /* ReleaseSocketMask */
	} /* PCSER_REQSOCKMASK */

	/*
	 * Deregister with Card Services - we will stop getting
	 *	events at this point.
	 */
	if (pcser->flags & PCSER_REGCLIENT) {
		if ((ret = csx_DeregisterClient(pcser->client_handle)) !=
		    CS_SUCCESS) {
		error2text_t cft;

		cft.item = ret;
		(void) csx_Error2Text(&cft);

		cmn_err(CE_CONT, "pcser_detach: socket %d "
		    "DeregisterClient failed %s\n",
		    (int)pcser->sn, cft.text);
		return (DDI_FAILURE);
		} /* DeregisterClient */
	} /* PCSER_REGCLIENT */

	/*
	 * unregister the softinterrupt handler
	 */
	if (pcser->flags & PCSER_SOFTINTROK)
		ddi_remove_softintr(pcser->softint_id);

	/*
	 * free the various mutexii
	 */
	if (pcser->flags & PCSER_DIDLOCKS) {
		mutex_destroy(&pcser->event_hilock);
		mutex_destroy(&pcser->noirq_mutex);
		cv_destroy(&pcser->readywait_cv);

		line = &pcser->line;
		mutex_destroy(&line->line_mutex);
		cv_destroy(&line->cvp);

		if (line->pcser_txbuf)
		kmem_free(line->pcser_txbuf, line->pcser_max_txbufsize);

		line = &pcser->control_line;
		mutex_destroy(&line->line_mutex);
		cv_destroy(&line->cvp);

	} /* PCSER_DIDLOCKS */

	ddi_soft_state_free(pcser_soft_state_p, pcser->instance);

	return (DDI_SUCCESS);
}

/*
 * pcser_getinfo() - this routine translates the dip info dev_t and
 *	vice versa.
 *
 *	Returns:	DDI_SUCCESS, if successful.
 *			DDI_FAILURE, if unsuccessful.
 */
/* ARGSUSED */
static int
pcser_getinfo(dev_info_t *dip, ddi_info_cmd_t cmd, void *arg, void **result)
{
	pcser_unit_t *pcser;
	int error = DDI_SUCCESS;
	cs_ddi_info_t cs_ddi_info;

	switch (cmd) {
		case DDI_INFO_DEVT2DEVINFO:
		case DDI_INFO_DEVT2INSTANCE:
		cs_ddi_info.Socket = PCSER_SOCKET((dev_t)arg);
		cs_ddi_info.driver_name = pcser_name;
		if (csx_CS_DDI_Info(&cs_ddi_info) != CS_SUCCESS)
			return (DDI_FAILURE);

		switch (cmd) {
			case DDI_INFO_DEVT2DEVINFO:
			if (!(pcser = ddi_get_soft_state(pcser_soft_state_p,
			    cs_ddi_info.instance))) {
				*result = NULL;
			} else {
				*result = pcser->dip;
			}
			break;
			case DDI_INFO_DEVT2INSTANCE:
			*result = (void *)(uintptr_t)cs_ddi_info.instance;
			break;
		} /* switch */
		break;
		default:
		error = DDI_FAILURE;
		break;
	} /* switch */

	return (error);
}

/*
 * pcser_auctl(line,action) - initialize the modem and do other
 *	hardware-related things to the card and socket
 */
static void
pcser_auctl(pcser_line_t *line, int action)
{
	pcser_unit_t *pcser = line->pcser;
	access_config_reg_t access_config_reg;

	/*
	 * If ther's no CCSR, then there's nothing that
	 *	we can do.
	 */
	if (!USE_CCSR(line))
		return;

	access_config_reg.Action = CONFIG_REG_READ;
	access_config_reg.Offset = CONFIG_STATUS_REG_OFFSET;

	/* XXX function return value ignored */
	(void) csx_AccessConfigurationRegister(pcser->client_handle,
	    &access_config_reg);

	switch (action) {
		case MODEM_SET_AUDIO_ON:
		access_config_reg.Value |= CCSR_AUDIO;
		break;

		case MODEM_SET_AUDIO_OFF:
		access_config_reg.Value &= ~CCSR_AUDIO;
		break;
	} /* switch(action) */

	access_config_reg.Action = CONFIG_REG_WRITE;
	access_config_reg.Offset = CONFIG_STATUS_REG_OFFSET;

	/* XXX function return value ignored */
	(void) csx_AccessConfigurationRegister(pcser->client_handle,
	    &access_config_reg);

	/* return; */
}

/*
 * pcser_softint(pcser) - handle the STREAMS stuff that the routines called
 *			by pcser_poll() setup for us
 */
static uint32_t
pcser_softint(pcser_unit_t *pcser)
{
	pcser_line_t *line = &pcser->line;
	queue_t *q;
	int ret = DDI_INTR_UNCLAIMED;
	unsigned state;

	/*
	 * Make sure that we made it through attach.  It's OK to run
	 *	some of this code if there's work to do even if there
	 *	is no card in the socket, since the work we're being
	 *	asked to do might be cleanup from a card removal.
	 */
	if (pcser->flags & PCSER_ATTACHOK) {
		unsigned work_flags = (PCSER_UNTIMEOUT | PCSER_RXWORK |
		    PCSER_MBREAK | PCSER_MHANGUP |
		    PCSER_MUNHANGUP | PCSER_TXWORK |
		    PCSER_CVBROADCAST);

		do {

		mutex_enter(&line->line_mutex);

		PCSER_HIMUTEX_ENTER(pcser);
		/*
		 * The softint is triggered whenever a valid HARD interrupt
		 * occurs. pcser_softint is run at lower priority than HARD
		 * interrupts. There could be multiple softints piled up before
		 * one is processed. All the processing gets done in the first
		 * call to pcser_softint and subsequent calls to pcser_softint
		 * find nothing to do.
		 * We should always return DDI_INTR_CLAIMED from pcser_softintr.
		 */
		ret = DDI_INTR_CLAIMED;
		state = (line->state & work_flags);
		line->state &= ~work_flags;
		PCSER_HIMUTEX_EXIT(pcser);

#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_SOFTINT)
			cmn_err(CE_CONT, "pcser_softint: state = 0x%x\n",
			    state);
#endif

		/*
		 * see if we have to do a untimeout() on behalf of
		 *	the higher-level interrupt routines
		 */
		if (state & PCSER_UNTIMEOUT) {
			UNTIMEOUT(line->pcser_timeout_id);
		} /* PCSER_UNTIMEOUT */

		/*
		 * handle any Rx work to be done
		 */
		if (state & PCSER_RXWORK) {
			if (line->state & (PCSER_WOPEN | PCSER_ISOPEN)) {

			q = line->pcser_ttycommon.t_readq;
			if (q != 0 && line->pcser_sscnt != 0)
				pcser_drainsilo(line);

			/*
			 * now, deal with the out-of-band conditions:
			 *	BREAK
			 *	HANGUP (loss of carrier)
			 *	UNHANGUP (resumption of carrier)
			 * these should probably be handled in band...
			 */
			if (state & PCSER_MBREAK) {
				if ((q = line->pcser_ttycommon.t_readq)
				    != NULL) {
					mutex_exit(&line->line_mutex);
					(void) putctl(q->q_next, M_BREAK);
					mutex_enter(&line->line_mutex);
				} /* if q */
			} /* PCSER_MBREAK */

			if (state & PCSER_MHANGUP) {
				if (!(line->state & PCSER_IGNORE_CD) &&
				    (q = line->pcser_ttycommon.t_readq)) {
				mutex_exit(&line->line_mutex);
				(void) putctl(q->q_next, M_HANGUP);
				mutex_enter(&line->line_mutex);
				} /* if !PCSER_IGNORE_CD */
			} /* PCSER_MHANGUP */

			if (state & PCSER_MUNHANGUP) {
				if (!(line->state & PCSER_IGNORE_CD) &&
				    (q = line->pcser_ttycommon.t_readq)) {
				mutex_exit(&line->line_mutex);
				(void) putctl(q->q_next, M_UNHANGUP);
				mutex_enter(&line->line_mutex);
				} /* if !PCSER_IGNORE_CD */
			} /* PCSER_MUNHANGUP */

#ifdef	XXX
			/*
			 * if there's no more Rx data left in
			 *	the softsilo, then clear the
			 *	PCSER_RXWORK flag, it will be
			 *	set again by the hardware interrupt
			 *	handler when we get more work to do
			 */
			PCSER_HIMUTEX_ENTER(pcser);
			if (!(line->pcser_sscnt))
				line->state &= ~PCSER_RXWORK;
			PCSER_HIMUTEX_EXIT(pcser);
#endif
			} /* if (PCSER_WOPEN | PCSER_ISOPEN) */
		} /* if (PCSER_RXWORK) */

		/*
		 * handle any Tx work to be done
		 */
		if (state & PCSER_TXWORK) {
			if (line->state & (PCSER_WOPEN | PCSER_ISOPEN)) {
			pcser_start(line);
			} /* if (PCSER_WOPEN | PCSER_ISOPEN) */
		} /* if (PCSER_TXWORK) */

		/*
		 * see if we have to do a cv_broadcast() on behalf of
		 *	the higher-level interrupt routines
		 */
		if (state & PCSER_CVBROADCAST) {
			cv_broadcast(&line->cvp);
		} /* PCSER_CVBROADCAST */
		mutex_exit(&line->line_mutex);
		} while (line->state & work_flags);

	} /* PCSER_ATTACHOK */

	return (ret);
}

int pcser_poll_loop = 0;

/*
 * pcser_poll(pcser) - modem interrupt routine
 *
 * return:	1 if one or more interrupts has been serviced
 *		0 if not
 */
static uint32_t
pcser_poll(pcser_unit_t *pcser)
{
	pcser_line_t *line = &pcser->line;
	int serviced = DDI_INTR_UNCLAIMED;
	volatile uchar_t iir, msr;
	int pcser_poll_loop_cnt = 0;

#ifdef	PCSER_DEBUG
	if (pcser_debug & PCSER_DEBUG_POLL) {
		cmn_err(CE_CONT, "pcser_poll: socket %d\n", (int)pcser->sn);
	}
#endif

	if (CARD_PRESENT(pcser) && (pcser->line.state & PCSER_OPEN_READY)) {

		PCSER_HIMUTEX_ENTER(pcser);

#define	PCSER_POLL_LOOP
#ifdef	PCSER_POLL_LOOP

		while (!pcser_poll_loop_cnt &&
		    !((iir = csx_Get8(line->handle,
		    PCSER_REGS_IIR)) & IIR_PENDING)) {
#else
		if (!((iir = csx_Get8(line->handle, PCSER_REGS_IIR)) &
		    IIR_PENDING)) {
#endif

#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_POLL) {
		cmn_err(CE_CONT, "pcser_poll: socket %d iir = 0x%x\n",
		    (int)pcser->sn, (int)iir);
		}
#endif

		if (pcser_poll_loop)
			pcser_poll_loop_cnt = 1;

		if ((iir&IIR_MASK) == RCV_EXP) {
			serviced = pcser_rcvex(pcser, 0, 0);
		} else if ((iir&IIR_MASK) == RCV_DATA) {
			serviced = pcser_rcv(pcser);
		} else if ((iir & IIR_MASK) == XMIT_DATA) {
			msr = csx_Get8(line->handle, PCSER_REGS_MSR);
			serviced = pcser_xmit(pcser, msr, PCSER_CALL);
		} else if ((iir & IIR_MASK) == MODEM_CHANGE) {
			msr = csx_Get8(line->handle, PCSER_REGS_MSR);
			serviced = pcser_modem(pcser, msr, PCSER_CALL);
		} else {
			/*
			 * If we got here, it means that the UART told us
			 *	that it is generating an interrupt, but for
			 *	some reason, it can't identify which interrupt
			 *	it is.
			 * After some number of these unidentified interrupts
			 *	we just assume that the card is bad and deep six
			 *	the whole thing.
			 */
			if (pcser->unid_irq++ == pcser_unid_irq_max) {
			cmn_err(CE_CONT, "pcser: socket %d too many "
			    "unidentified IRQs\n", (int)pcser->sn);

			/*
			 * disable UART interrupts and arrange to have
			 *	an M_HANGUP message sent downstream
			 * XXX - if the card is really broken, then
			 *	disabling the UART interrupts here may
			 *	not stop the card from interrupting.
			 */
			csx_Put8(line->handle, PCSER_REGS_IER, 0);
			line->state |= (PCSER_MHANGUP | PCSER_RXWORK |
			    PCSER_DRAIN | PCSER_TXWORK);
			serviced = DDI_INTR_CLAIMED;
			} /* if (PCSER_UNID_IRQ_MAX) */
		} /* if (iir & IIR_MASK) */
		} /* IIR_PENDING */

		PCSER_HIMUTEX_EXIT(pcser);

		/*
		 * if we've got any work to do, schedule a softinterrupt
		 */
		if (serviced == DDI_INTR_CLAIMED) {
		ddi_trigger_softintr(pcser->softint_id);
		}

	} /* CARD_PRESENT */

#ifdef  PCSER_DEBUG
	if (pcser_debug & PCSER_DEBUG_POLL) {
		cmn_err(CE_CONT, "pcser_poll: exit serviced=%d\n", serviced);
	}
#endif

	return (serviced);
}

/*
 * pcser_xmit(pcser) - handle xmit interrupts
 */
static int
pcser_xmit(pcser_unit_t *pcser, uchar_t msr, int call_state)
{
	pcser_line_t *line = &pcser->line;
	int i;
	/*
	 * disable xmit interrupts for this channel and clear the busy
	 * flag - the reason we disable both xmit interrupts is because
	 * we might get here from pcser_xwait() which enables the TX_MPTY
	 * interrupt and it's easier to disable both of them rather than
	 * put another test in (save a few cycles and all that...)
	 */

#ifdef PCSER_DEBUG
	if (pcser_debug & PCSER_DEBUG_XMIT) {
		cmn_err(CE_CONT, "pcser_xmit: socket %d txcount 0x%x\n",
		    (int)pcser->sn, (int)line->pcser_txcount);
	}
#endif

	csx_Put8(line->handle, PCSER_REGS_IER,
	    (csx_Get8(line->handle, PCSER_REGS_IER) & ~TX_EMPTY_E));
	line->state &= ~(PCSER_BUSY | PCSER_CTSWAIT);

	/*
	 * if we're coming here to signal when the transmitter is empty,
	 *	then just clear the wait flag and schedule a cv_broadcast()
	 *	otherwise load any chars ready into the xmit buffer and
	 *	schedule a call to pcser_start() to keep the pump going
	 * if there's more output to send; we might also be coming here
	 *	due to a break condition - enable the cd180 to do the
	 *	break thing then go away until it's done
	 * we set pcser_txcount equal to LINE_TXBUFSIZE as an indication to
	 *	us that we've been here before on a call from pcser_xwait()
	 *	as well as to allow pcser_xwait() to enble one more TxEMPTY
	 *	interrupt after the final Tx bytes have been written to
	 *	the UART
	 * XXX - we don;t handle the CTS flow control properly in the
	 *	XWAIT case yet
	 */
	if (line->state & PCSER_XWAIT) {
		line->state &= ~PCSER_XWAIT;
#ifdef PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_XMIT) {
			cmn_err(CE_CONT, "pcser_xmit: socket %d PCSER_XWAIT\n",
			    (int)pcser->sn);
		}
#endif
		if (line->pcser_txcount == line->pcser_max_txbufsize)
			line->pcser_txcount = 0;
		if (line->pcser_txcount) {
			line->pcser_stats.xmit_cc += line->pcser_txcount;
			for (i = 0; i < line->pcser_txcount; i++)
			csx_Put8(line->handle, PCSER_REGS_RBRTHR,
			    line->pcser_txbuf[i]);
			line->pcser_txcount = line->pcser_max_txbufsize;
		}
		line->state |= (PCSER_CVBROADCAST | PCSER_UNTIMEOUT);
	/*
	 * We got here from either a TCSBRK(BREAK) or TIOCSBRK call.
	 *	We clear the PCSER_BREAK bit to tell the caller that
	 *	we have started the BREAK.  The caler is responsible
	 *	for actually turning on the BREAK bit in the lcr.
	 */
	} else if (line->state & PCSER_BREAK) {
		line->state &= ~PCSER_BREAK;
		line->state |= PCSER_TXWORK;
#ifdef PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_XMIT) {
			cmn_err(CE_CONT, "pcser_xmit: socket %d PCSER_BREAK\n",
			    (int)pcser->sn);
		}
#endif

	} else { /* this is a real send-data interrupt */
		/*
		 * check to see if CTS is deasserted and if so,
		 *	set a flag so that we will be scheduled
		 *	when CTS becomes asserted again
		 * we only set PCSER_TXWORK if CTS is asserted
		 *	otherwise it will get set via the call
		 *	into this code from the modem signal
		 *	interrupt handler
		 * XXX - is this true??
		 */
		if ((line->pcser_ttycommon.t_cflag & CRTSCTS) &&
		    !(msr & CTS_ON_MSR)) { /* CTS deasserted */
			line->state |= PCSER_CTSWAIT;
#ifdef	PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_XMIT) {
			cmn_err(CE_CONT, "pcser_xmit: socket %d CTS_ON_MSR "
			    "was false, waiting with %d bytes\n",
			    (int)pcser->sn,
			    (int)line->pcser_txcount);
			}
#endif
		} else {	/* CTS asserted or not using RTS/CTS */
			line->state &= ~PCSER_CTSWAIT;
			line->state |= PCSER_TXWORK;

#ifdef	PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_XMIT) {
			cmn_err(CE_CONT, "pcser_xmit: socket %d CTS_ON_MSR "
			    "was true, sending %d bytes\n",
			    (int)pcser->sn,
			    (int)line->pcser_txcount);
			}
#endif
			line->pcser_stats.xmit_cc += line->pcser_txcount;
			for (i = 0; i < line->pcser_txcount; i++) {

#ifdef	PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_XMIT) {
				cmn_err(CE_CONT, "pcser_xmit: socket %d "
				    "sending: (0x%x) [%c]\n",
				    (int)pcser->sn,
				    (int)line->pcser_txbuf[i],
				    (int)line->pcser_txbuf[i]);
			}
#endif

			csx_Put8(line->handle, PCSER_REGS_RBRTHR,
			    line->pcser_txbuf[i]);
			}
			line->pcser_txcount = 0;
		} /* not using CTS flow control and/or CTS asserted */
	} /* real send-data interrupt */

	if (!(call_state & PCSER_DONTCALL) &&
	    (msr & (DSR_CHANGE | RI_CHANGE | CD_CHANGE))) {
#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_XMIT) {
		cmn_err(CE_CONT, "pcser_xmit: calling pcser_modem"
		    "(msr = 0x%x)\n", msr);
		}
#endif
		(void) pcser_modem(pcser, msr, PCSER_DONTCALL);
	}

	line->pcser_stats.xmit_int++;

	return (DDI_INTR_CLAIMED);
}

/*
 * pcser_rcv(pcser) - handle receive interrupts
 */
static int
pcser_rcv(pcser_unit_t *pcser)
{
	pcser_line_t *line = &pcser->line;
	int i;
	uchar_t c;
	uchar_t rcsr;
	tcflag_t iflag;

	iflag = line->pcser_ttycommon.t_iflag;

	if ((rcsr = csx_Get8(line->handle, PCSER_REGS_LSR) & RX_DATA_AVAIL)
	    == 0) {
		/*
		 * The 3COM 3CXM756 modem card is occasionally generating
		 * a receive interrupt without any data which results
		 * in a loop in the interrupt routine.
		 * Reading a byte from the Rx/Tx buffer appears to clear
		 * the interrupt and solve the problem.
		 */
		c = csx_Get8(line->handle, PCSER_REGS_RBRTHR);
		line->pcser_stats.rcv_int++;
#ifdef PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_RCV) {
			cmn_err(CE_CONT,
			    "pcser_rcv rcsr=%x c=%x socket=%d"
			    " card_id=%x manufacturer_id=%x\n",
			    rcsr, (int)pcser->sn, (int)c,
			    line->cis_vars.card_id,
			    line->cis_vars.manufacturer_id);
		}
#endif
		return (DDI_INTR_CLAIMED);
	}

	/*
	 * move the characters from the cd180's receive fifo into our
	 *	local soft silo
	 * the streams stuff takes care of ISTRIP
	 *	IGNPAR and PARMRK is taken care of in pcser_rcvex() except
	 *	for checking for a valid `\377` received character
	 * Also read the status of the current FIFO entry and call the
	 *	Rx exception handler if there's an exception.
	 */
	i = 0;	/* XXX hack */
	while ((rcsr = csx_Get8(line->handle, PCSER_REGS_LSR)) &
	    RX_DATA_AVAIL) {
		c = csx_Get8(line->handle, PCSER_REGS_RBRTHR);
		if (rcsr & (RX_OVERRUN | RX_PARITY | RX_FRAMING | RX_BREAK))
		(void) pcser_rcvex(pcser, rcsr, c);

		line->pcser_stats.rcv_cc++;

#ifdef PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_RCV) {
		cmn_err(CE_CONT, "pcser_rcv: socket %d char (0x%x) [%c]\n",
		    (int)pcser->sn, (int)(c&255), (int)c);
		}
#endif

		if ((iflag & IGNPAR) || !((iflag & PARMRK) &&
		    !(iflag & ISTRIP) && (c == (uchar_t)'\377'))) {
		PUTSILO(line, c);
		} else { /* !IGNPAR */
		PUTSILO(line, (uchar_t)'\377');
		PUTSILO(line, c);
		}

	    /* XXX hack */
		if (i++ > line->pcser_rxfifo_size) {
#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_RCV) {
			cmn_err(CE_CONT, "pcser_rcv: socket %d terminating "
			    "FIFO pullout\n", (int)pcser->sn);
		}
#endif

#ifdef	XXX
		break;
#else
		i = 0;
#endif
		}
	} /* while (RX_DATA_AVAIL) */

	/*
	 * flag the line as having data available in the soft silo
	 */
	line->state |= PCSER_RXWORK;

	line->pcser_stats.rcv_int++;

	return (DDI_INTR_CLAIMED);
}

/*
 * pcser_rcvex(pcser) - handle receive exception interrupts
 */
static int
pcser_rcvex(pcser_unit_t *pcser, uchar_t rcsr, uchar_t c)
{
	pcser_line_t *line = &pcser->line;
	int i, rc;
	tcflag_t iflag;

	iflag = line->pcser_ttycommon.t_iflag;

	rc = 1;

	for (i = 0; i < rc; i++) {
		/*
		 * read the status of the current fifo entry as
		 *	well as the character associated with it
		 */
		if (!rcsr) {
		c = csx_Get8(line->handle, PCSER_REGS_RBRTHR);
		rcsr = csx_Get8(line->handle, PCSER_REGS_LSR);
#ifdef PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_RCVEX) {
			cmn_err(CE_CONT, "pcser_rcvex: socket %d !rcsr, "
			    "c=0x%x, lsr=0x%x\n",
			    (int)pcser->sn,
			    (int)(c&255),
			    (int)(rcsr&255));
		}
#endif

		}

#ifdef PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_RCVEX) {
			cmn_err(CE_CONT, "pcser_rcvex: socket %d rcsr 0x%x\n",
			    (int)pcser->sn,
			    (int)(rcsr&255));
		}
#endif

		/*
		 * if we got a break, send an M_BREAK message upstream
		 */
		if (rcsr & RX_BREAK) {
		line->pcser_stats.break_cnt++;
		line->state |= PCSER_MBREAK;
		/*
		 * If IGNBRK is clear and BRKINT is set, then we need
		 *	to flush our local input and output queues
		 */
		if (!(iflag & IGNBRK) && (iflag & BRKINT)) {
#ifdef	PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_RCVEX) {
			cmn_err(CE_CONT, "pcser_rcvex: socket %d RX_BREAK "
			    "&& !IGNBRK && BRKINT\n",
			    (int)pcser->sn);
			}
#endif
			FLUSHSILO(line);
			CHECK_RTS_ON(line);
			pcser_txflush(line);
		}
		}

		/*
		 * If IGNPAR is set, characters with framing or  parity  errors
		 * (other  than  break)  are  ignored.  Otherwise, if PARMRK is
		 * set, a character with a framing or parity error that is  not
		 * ignored  is  read  as  the three-character sequence: '\377',
		 * '\0', X, where X is the data of the  character  received  in
		 * error.  To  avoid  ambiguity  in this case, if ISTRIP is not
		 * set, a valid character of '\377' is read as '\377',  '\377'.
		 * If  neither  IGNPAR  nor  PARMRK is set, a framing or parity
		 * error (other than break) is read as a single ASCII NUL char-
		 * acter ('\0').
		 *
		 * Note that ldterm now does most of this for us, so we should
		 *	probably take this stuff out in the next release.
		 */
		if (rcsr & (RX_PARITY | RX_FRAMING)) {
		if (!(iflag & IGNPAR)) {
			if (iflag & PARMRK) {
			PUTSILO(line, (uchar_t)'\377');
			if ((c == (uchar_t)'\377') && !(iflag & ISTRIP)) {
				PUTSILO(line, (uchar_t)'\377');
			} else { /* ISTRIP */
				PUTSILO(line, '\0');
				PUTSILO(line, c);
			}
			} else { /* !PARMRK */
			PUTSILO(line, '\0');
			}
		} /* !IGNPAR */
		}

		/*
		 * We don't bother with this message if we're
		 *	ignoring CD transitions, since some
		 *	cards generate one of these
		 *	when the interrupts are enabled.
		 */
		if ((rcsr & RX_OVERRUN) && !(line->state & PCSER_IGNORE_CD)) {
		cmn_err(CE_CONT, "pcser_rcvex: socket %d receiver overrun "
		    "char: 0x%x\n",
		    (int)pcser->sn, (int)(0x0ff&c));
		}
	} /* for (i<rc) */

	line->state |= PCSER_RXWORK;

	line->pcser_stats.rcvex_int++;

	return (DDI_INTR_CLAIMED);
}

/*
 * pcser_modem(pcser) - handles modem control line changes
 */
static int
pcser_modem(pcser_unit_t *pcser, uchar_t msr, int call_state)
{
	pcser_line_t *line = &pcser->line;

	/*
	 * The modem status register (msr) is now read in the main
	 *	interrupt handler body, since both the xmit and modem
	 *	change handlers need to see it, and reading that
	 *	register causes all of the set changed bits to be
	 *	cleared.
	 */
#ifdef	PCSER_DEBUG
	if (pcser_debug & PCSER_DEBUG_MODEM) {
		cmn_err(CE_CONT, "pcser_modem: socket %d msr 0x%x\n",
		    (int)pcser->sn, (int)(msr&255));
#ifdef	XXX
	cmn_err(CE_CONT, "pcser_modem: changed: [");
	if (msr & CTS_CHANGE)
		cmn_err(CE_CONT, "CTS_CHANGE ");
	if (msr & DSR_CHANGE)
		cmn_err(CE_CONT, "DSR_CHANGE ");
	if (msr & RI_CHANGE)
		cmn_err(CE_CONT, "RI_CHANGE ");
	if (msr & CD_CHANGE)
		cmn_err(CE_CONT, "CD_CHANGE ");
	cmn_err(CE_CONT, "]\n");

	cmn_err(CE_CONT, "pcser_modem: status: [");
	if (msr & CTS_ON_MSR)
		cmn_err(CE_CONT, "CTS_ON_MSR ");
	if (msr & DSR_ON_MSR)
		cmn_err(CE_CONT, "DSR_ON_MSR ");
	if (msr & RI_ON_MSR)
		cmn_err(CE_CONT, "RI_ON_MSR ");
	if (msr & CD_ON_MSR)
		cmn_err(CE_CONT, "CD_ON_MSR ");
	cmn_err(CE_CONT, "]\n");
#endif

	cmn_err(CE_CONT, "    changed: [%s%s%s%s]\n     status: [%s%s%s%s]\n",
	    (msr & CTS_CHANGE)?"CTS_CHANGE ":"",
	    (msr & DSR_CHANGE)?"DSR_CHANGE ":"",
	    (msr & RI_CHANGE)?"RI_CHANGE ":"",
	    (msr & CD_CHANGE)?"CD_CHANGE ":"",
	    (msr & CTS_ON_MSR)?"CTS_ON_MSR ":"",
	    (msr & DSR_ON_MSR)?"DSR_ON_MSR ":"",
	    (msr & RI_ON_MSR)?"RI_ON_MSR ":"",
	    (msr & CD_ON_MSR)?"CD_ON_MSR ":"");

}
#endif


	/*
	 * check to see if we're supposed to restart the transmitter
	 *	here because CTS was asserted
	 */
	if (!(call_state & PCSER_DONTCALL) && (msr & CTS_CHANGE) &&
			(msr & CTS_ON_MSR) && (line->state & PCSER_CTSWAIT)) {
		/*
		 * clear this bit here since pcser_xmit might call back into us
		 */
	    msr &= ~(CTS_CHANGE);

#ifdef	PCSER_DEBUG
	    if (pcser_debug & PCSER_DEBUG_MODEM) {
		cmn_err(CE_CONT, "pcser_modem: socket %d CTS_CHANGE && "
				"CTS_ON_MSR && PCSER_CTSWAIT reached\n",
							(int)pcser->sn);
	    }
#endif

		/*
		 * calling pcser_xmit here will flush out the data that we've
		 *	been holding in pcser_txbuf and that never got a
		 *	chance to be sent to the UART since at the last Tx
		 *	interrupt we got we were held off by CTS flow control
		 */
	    (void) pcser_xmit(pcser, msr, PCSER_DONTCALL);
	}

	if (msr & CD_CHANGE) {
		/*
		 * clear this bit here since pcser_xmit might call back into us
		 */
	    msr &= ~(CD_CHANGE);

	    if (!(line->state & PCSER_IGNORE_CD)) {
		/* carrier dropped */
		if (!(msr&CD_ON_MSR) &&
			!(line->pcser_ttycommon.t_flags & TS_SOFTCAR)) {
		    if ((line->state&PCSER_CARR_ON) &&
				!(line->pcser_ttycommon.t_cflag&CLOCAL)) {
			/*
			 * Carrier went away.
			 * Drop DTR, abort any output in progress,
			 * indicate that output is not stopped, and
			 * send a hangup notification upstream.
			 */
			pcser_dtr_off(line);		/* drop DTR */
			FLUSHSILO(line);
			CHECK_RTS_ON(line);
			pcser_txflush(line);
			/*
			 * Bug ID 1171519
			 * Don't set PCSER_DRAIN anymore.
			 */
			line->state |= (PCSER_MHANGUP | PCSER_RXWORK);
		    }
		    line->state &= ~(PCSER_CARR_ON | PCSER_BUSY);
		} else {	/* carrier raised */
		    if (!(line->state&PCSER_CARR_ON)) {
			line->state |= (PCSER_CARR_ON | PCSER_MUNHANGUP |
					PCSER_RXWORK | PCSER_CVBROADCAST);
			/*
			 * XXX - Do we really need to clear PCSER_DRAIN here?
			 */
			line->state &= ~(PCSER_DRAIN | PCSER_BUSY);
		    }
		}
	    } /* if (!PCSER_IGNORE_CD) */
	} /* if (CD_CHANGE) */

	line->pcser_stats.modem_int++;

	return (DDI_INTR_CLAIMED);
}

/*
 * pcser_txflush(line) - resets channel and flushes the rest of it's data
 *			even if being held off by flow control
 *
 *    Note: this is protected by a mutex by the caller; we are also called
 *		from pcser_rcvex.
 */
static void
pcser_txflush(pcser_line_t *line)
{
	line->pcser_txcount = 0;

#ifdef	PX_IFLUSH_DEBUG
	cmn_err(CE_CONT, "pcser_txflush: socket %d flushing\n",
	    line->pcser->sn);
#endif	/* PX_IFLUSH_DEBUG */
}

/*
 * pcser_draintimeout(line)
 */
static void
pcser_draintimeout(void *arg)
{
	pcser_line_t *line = (pcser_line_t *)arg;
	pcser_unit_t *pcser = line->pcser;

	PCSER_HIMUTEX_ENTER(pcser);
	line->state &= ~PCSER_CANWAIT;
	PCSER_HIMUTEX_EXIT(pcser);

	mutex_enter(&line->line_mutex);
	pcser_drainsilo(line);
	mutex_exit(&line->line_mutex);
}

/*
 * pcser_drainsilo(line) - This snarfs chars out of the input silo and
 *				does queue fu up the stream
 */
static void
pcser_drainsilo(pcser_line_t *line)
{
	queue_t *q = line->pcser_ttycommon.t_readq;
	pcser_unit_t *pcser = line->pcser;
	mblk_t *bp;
	int cc;

	/*
	 * if we got here because of a receive or receive exception interrupt,
	 * remove the timer from the timeout table or we'll get a panic
	 */
	if (line->state & PCSER_CANWAIT) {
		UNTIMEOUT(line->pcser_draintimeout_id);
		PCSER_HIMUTEX_ENTER(pcser);
		line->state &= ~PCSER_CANWAIT;
		PCSER_HIMUTEX_EXIT(pcser);
		line->pcser_qfcnt = 0;
		line->pcser_stats.canwait++;
	}

	/*
	 * see if we're already in this code for this line - if we are,
	 *	then just return without doing anything; this is kind of
	 *	ugly, but since I can't hold locks over the putq calls
	 *	it seems that another thread on another processor gets
	 *	in here while we're doing the putq without holding any
	 *	mutex locks
	 * *** LOOK ***
	 */
	if (line->state & PCSER_INDRAIN) {
#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_DRAINSILO) {
		cmn_err(CE_CONT, "pcser_drainsilo: socket %d "
		    "PCSER_INDRAIN already\n",
		    (int)pcser->sn);
		}
#endif
		return;
	}

	PCSER_HIMUTEX_ENTER(pcser);
	line->state |= PCSER_INDRAIN;
	PCSER_HIMUTEX_EXIT(pcser);

	/*
	 * we should never see this condition unless a timer pop goes off
	 *	after the line is closed (but that should have been taken
	 *	care of in pcser_close())
	 * if we do see it, it could mean that the timer popped before the
	 *	line was closed, usually due to a process sleeping in
	 *	pcser_open() waiting for carrier to come up - this is not
	 *	an error, just silently go away
	 */
	if (q == NULL) {
#ifdef	PCSER_DEBUG
	if (pcser_debug & PCSER_DEBUG_DRAINSILO) {
		cmn_err(CE_CONT, "pcser_drainsilo: socket %d q == NULL\n",
		    (int)pcser->sn);
	}
#endif

		goto drain_o;
	}

	/*
	 * allocate a message and send the data up the stream
	 * silocopy() handles all the pointers and counts
	 * if bufcall() fails, the softsilo will get cleared by
	 * PUTSILO someday.
	 */
	while (line->pcser_sscnt) {
		if (q && canputnext(q)) {
		line->pcser_qfcnt = 0;
		cc = MIN(line->drain_size, line->pcser_sscnt);
		if ((bp = allocb(cc, BPRI_MED)) != NULL) {
			PCSER_HIMUTEX_ENTER(pcser);
			silocopy(line, bp->b_wptr, cc);
			bp->b_wptr += cc;
			PCSER_HIMUTEX_EXIT(pcser);
			mutex_exit(&line->line_mutex);
			if (q)
				putnext(q, bp);
			mutex_enter(&line->line_mutex);
		} else {
			/*
			 * pcser_draintimeout will acquire the
			 *	line->line_mutex before we get
			 *	called back
			 */
			if (!(line->pcser_wbufcid = bufcall(cc, BPRI_HI,
			    pcser_draintimeout, line))) {
				PCSER_HIMUTEX_ENTER(pcser);
				FLUSHSILO(line);	/* flush the rcv silo */
				CHECK_RTS_ON(line);
				PCSER_HIMUTEX_EXIT(pcser);
				cmn_err(CE_CONT, "pcser_drainsilo: socket %d "
				    "can't allocate %d byte "
				    "streams buffer\n",
				    (int)pcser->sn, (int)cc);
			} else {
				line->pcser_stats.bufcall++;

#ifdef	PCSER_DEBUG
				if (pcser_debug & PCSER_DEBUG_DRAINSILO) {
					cmn_err(CE_CONT, "pcser_drainsilo: "
					    "socket %d doing bufcall "
					    "(%d) already\n",
					    (int)pcser->sn,
					    line->pcser_stats.bufcall);
				}
#endif


			}
			goto drain_q;
		} /* allocb() */
		} else { /* canput() */
		/*
		 * no room in read queues, so try a few times then give up if we
		 * still can't do it
		 */
		line->pcser_stats.no_canput++;

		/*
		 * if we've exceeded the number of retrys that we want
		 *	to make, punt by printing a message and flush
		 *	the data
		 */
		if (++line->pcser_qfcnt > line->pcser_stats.nqfretry) {
			cmn_err(CE_CONT, "pcser_drainsilo: socket %d punting "
			    "after %d put retries\n",
			    (int)pcser->sn,
			    line->pcser_stats.nqfretry);
			line->pcser_stats.qpunt++;
			mutex_exit(&line->line_mutex);
			ttycommon_qfull(&line->pcser_ttycommon, q);
			mutex_enter(&line->line_mutex);
			line->pcser_qfcnt = 0;
			PCSER_HIMUTEX_ENTER(pcser);
			FLUSHSILO(line);	/* flush the rcv silo */
			CHECK_RTS_ON(line);
			PCSER_HIMUTEX_EXIT(pcser);
		} else {
			/*
			 * post a timer so that we can try again later
			 */
			PCSER_HIMUTEX_ENTER(pcser);
			line->state |= PCSER_CANWAIT;
			PCSER_HIMUTEX_EXIT(pcser);

			line->pcser_draintimeout_id = timeout(
			    pcser_draintimeout,
			    line, QFRETRYTIME);
			line->pcser_stats.drain_timer++;
		}
		goto drain_q;
		} /* if (canputnext(q)) */
	} /* while */

drain_q:
drain_o:

	PCSER_HIMUTEX_ENTER(pcser);
	line->state &= ~PCSER_INDRAIN; /* XXX */
	PCSER_HIMUTEX_EXIT(pcser);

	return;

}

/*
 *	do a copy from ring queue to buffer using bcopy(),
 *	and update counts and pointers when done
 */
static void
silocopy(pcser_line_t *line, uchar_t *buf, int count)
{
	uchar_t *sink = line->pcser_sink;
	int cc = count;

	/*
	 * if the sink pointer is less than the source pointer,
	 * we haven't wrapped yet, so just bcopy and update
	 */
	if (sink < line->pcser_source) {
		bcopy(sink, buf, cc);
	} else {
		cc = MIN(count,
		    &line->pcser_ssilo[line->pcser_silosize] - sink);
		bcopy(sink, buf, cc);
		if (cc != count) {
			buf += cc;
			cc = count - cc;
			sink = line->pcser_ssilo;
			bcopy(sink, buf, cc);
		}
	}
	line->pcser_sink = (sink + cc);
	line->pcser_sscnt -= count;
	/*
	 * check if we should diddle with RTS now that we've taken some
	 * data out of the silo
	 */
	CHECK_RTS_ON(line);
}

/*
 * pcser_open(q, dev, flag, sflag) - the open system call
 *
 */
/* ARGSUSED */
static int
pcser_open(queue_t *q, dev_t *dev, int oflag, int sflag, cred_t *credp)
{
	pcser_line_t *line;
	pcser_unit_t *pcser;
	void *instance;

#ifdef	PCSER_DEBUG
	if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1)
		cmn_err(CE_CONT, "pcser_open: dev = 0x%p *dev = 0x%x\n",
		    (void *)dev, (int)*dev);
#endif

	/*
	 * Get the instance number for this device so that we can get
	 *	a pointer to the per-card structure.  If we fail this
	 *	call, it just means that there has never been a card
	 *	inserted into this socket since we did a cold boot.
	 * If this call succeeds, it still doesn't mean that there
	 *	is a card that we can control in the socket, only that
	 *	at least once since cold boot time, and since this driver
	 *	has been loaded, the system saw a card for us.
	 */
	if (pcser_getinfo(NULL, DDI_INFO_DEVT2INSTANCE, (void *)*dev,
	    &instance) != DDI_SUCCESS)
		return (ENODEV);

	if ((pcser = ddi_get_soft_state(pcser_soft_state_p,
	    (int)(uintptr_t)instance)) == NULL) {
		cmn_err(CE_CONT, "pcser_open: socket %d NULL soft state\n",
		    PCSER_SOCKET(*dev));
		return (ENODEV);
	}

	if (!CARD_PRESENT(pcser)) {
#ifdef	PCSER_DEBUG
		if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1)
		cmn_err(CE_CONT, "pcser_open: socket %d no card inserted\n",
		    (int)pcser->sn);
#endif
		return (ENODEV);
	}

#ifdef	PCSER_DEBUG
	if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1)
		cmn_err(CE_CONT, "pcser_open: socket %d card inserted\n",
		    (int)pcser->sn);
#endif

	if (PCSER_CONTROL_LINE(*dev)) {
		if (!drv_priv(credp)) {
			line = &pcser->control_line;
			mutex_enter(&line->line_mutex);
			line->state |= (PCSER_CONTROL | PCSER_DRAIN);
			mutex_exit(&line->line_mutex);
		} else {
		return (EPERM);
		}
	} else {
		line = &pcser->line;
	}

	/*
	 * protect the line struct
	 */
	mutex_enter(&line->line_mutex);

	/*
	 * if we're inhibited because the user wants to unload the
	 *	driver, we won't allow any more opens except for the
	 *	control port also come here if we fall out of the
	 *	sleep() due to a wakeup() in close() or pcser_modem()
	 */

again:
	if (!CARD_PRESENT(pcser)) {
		mutex_exit(&line->line_mutex);
		return (ENODEV);
	}

	if (!(line->state & PCSER_CONTROL)) {
		/*
		 * See if we have to change the state of the audio signal
		 *	here.  If we've been requested to turn on audio
		 *	previously, then do it here, since close() turns
		 *	off audio unconditionally.
		 */
		if (AUDIO_GET(line))
		pcser_auctl(line, MODEM_SET_AUDIO_ON);
	} /* !PCSER_CONTROL */

	/*
	 * check to see if we're already open.  If so, check for exclusivity,
	 * if not, set up our data and enable the line.
	 * most of this stuff makes no sense if we're the control line, but
	 * it's easier to go through the generic case than to have a bunch
	 * of conditionals all around here
	 */
	if (!(line->state & PCSER_ISOPEN)) {
		line->pcser_ttycommon.t_cflag = (CS8 | B9600 | CREAD | HUPCL);
		line->pcser_ttycommon.t_iflag = 0;
		line->pcser_ttycommon.t_iocpending = NULL;
		line->pcser_ttycommon.t_size.ws_row = 0;
		line->pcser_ttycommon.t_size.ws_col = 0;
		line->pcser_ttycommon.t_size.ws_xpixel = 0;
		line->pcser_ttycommon.t_size.ws_ypixel = 0;
		line->pcser_flowc = 0;	/* set in pcser_wput() */
		/* these next two are duplicates of what pcser_init() does */
		line->pcser_source = line->pcser_sink = line->pcser_ssilo;
		line->pcser_sscnt = line->pcser_qfcnt = 0;
		line->pcser_wbufcid = 0;
		line->flags = 0;
		line->flags |= DTR_ASSERT;	/* assert DTR on open */

		/*
		 * the control line
		 */
		if (line->state & PCSER_CONTROL) {
			line->flags |= SOFT_CARR;
			line->pcser_ttycommon.t_flags |= TS_SOFTCAR;
		/*
		 * a serial line
		 */
		} else {
			/*
			 * disable draining on a fresh open
			 */
			line->state &= ~PCSER_DRAIN;
			/*
			 * set up some parameters for the Rx interrupt handler
			 */
			line->drain_size = PCSER_DRAIN_BSIZE;
			line->pcser_hiwater = PCSER_HIWATER;
			line->pcser_lowwater = PCSER_LOWWATER;
			/*
			 * make sure this line's default flags track TS_SOFTCAR
			 * in the tty struct also
			 */
			if (line->pcser_ttycommon.t_flags & TS_SOFTCAR) {
				line->flags |= SOFT_CARR;
			}

			/*
			 * If this is the first open since the card was
			 *	configured, then we need to ignore CD
			 *	transitions for a period of time. This
			 *	is a "feature" of many of the PCMCIA
			 *	cards that use an 8250-type UART.
			 * If the PCSER_IGNORE_CD_ON_OPEN flag is set,
			 *	use the ignore CD timer on each open.
			 *	This is to handle cards that always
			 *	provide bogus transitions on the CD line
			 *	after the UART is initialized.
			 */
			if ((line->state & PCSER_FIRST_OPEN) ||
			    (line->flags & PCSER_IGNORE_CD_ON_OPEN)) {
				line->state &= ~PCSER_FIRST_OPEN;
				line->state |= PCSER_IGNORE_CD;
				line->ignore_cd_timeout_id = timeout(
				    pcser_ignore_cd_timeout, line,
				    MS2HZ(line->pcser_ignore_cd_time));
			}

			/*
			 * Set up the hardware.
			 */
			/*
			 * Disable all interrupts and clear out any
			 *	pending state.
			 */
			PCSER_HIMUTEX_ENTER(pcser);

			/*
			 * This line is partially open and ready
			 *	to take interrupts.
			 */
			line->state |= PCSER_OPEN_READY;

			csx_Put8(line->handle, PCSER_REGS_IER, 0);
			OUTB_DELAY();
			pcser->unid_irq = 0;

			PCSER_HIMUTEX_EXIT(pcser);

			/*
			 * Set the UART to default parameters.
			 */
			pcser_param(line);

			PCSER_HIMUTEX_ENTER(pcser);

			/*
			 * Turn on the handshaking and modem control lines.
			 */
			csx_Put8(line->handle, PCSER_REGS_MCR,
			    (OUT2_ON_MCR | RTS_ON_MCR));
			OUTB_DELAY();

			line->state &= ~(PCSER_RTSOFF_MESSAGE |
			    PCSER_RTSON_MESSAGE);
			/*
			 * If this card supports FIFOs, clear both FIFOs
			 *	and enable them.
			 */
			if (line->cis_vars.flags & PCSER_FIFO_DISABLE) {
				csx_Put8(line->handle, PCSER_REGS_IIR,
				    line->cis_vars.fifo_disable);
				OUTB_DELAY();
			}
			if (line->cis_vars.flags & PCSER_FIFO_ENABLE) {
				csx_Put8(line->handle, PCSER_REGS_IIR,
				    line->cis_vars.fifo_enable);
				OUTB_DELAY();
			}

			/*
			 * Enable the interrupts that we want to see.
			 */
			csx_Put8(line->handle, PCSER_REGS_IER,
			    (RX_DATA_E | MODEM_CHANGE_E | RX_EXCEPTION_E));
			OUTB_DELAY();
			PCSER_HIMUTEX_EXIT(pcser);
		}
	/* only one open at a time, unless we're root */
	} else if ((line->pcser_ttycommon.t_flags & TS_XCLUDE) &&
	    secpolicy_excl_open(credp) != 0) {
		mutex_exit(&line->line_mutex);
		return (EBUSY);		/* already opened exclusively */
	} else if ((PCSER_OUTLINE(*dev)) && !(line->state & PCSER_OUT)) {
		mutex_exit(&line->line_mutex);
		return (EBUSY);	/* already opened but not for dial-out */
	}

	/*
	 * if we're the control port, there's no carrier
	 * so wait for, so just blast on through
	 */
	if (!(line->state & PCSER_CONTROL)) {
		/*
		 * play carrier games
		 * note that since the only way we can get here is if this
		 *	is a serial port open, we're still covered by the
		 *	SPL_PCSER() issued up above so I commented out the
		 *	folowing line that was in the V1.0 release
		 *		s = SPL_PCSER(unit_no);
		 */
		PCSER_HIMUTEX_ENTER(pcser);
		if (line->flags & (DTR_ASSERT|PCSER_DTRFORCE)) {
			pcser_dtr_on(line);
		}
		/*
		 * assert RTS - most of the time, this will be asserted
		 *	by pcser_param(), above, unless the user has set the
		 *	default for this line to be CRTSCTS, in which case
		 *	RTS could never be set, since pcser_param() depends
		 *	on PUTSILO() to diddle with RTS, and PUTSILO() only
		 *	gets called at receive interrupt time, and if a
		 *	device is waiting for RTS to be asserted before they
		 *	start sending us data, we'll get into a deadlock
		 *	situation, so raise RTS here (this has no nasty side
		 *	effects), assuming an empty receive silo (a valid
		 *	assumption), and let PUTSILO() deal with it if the
		 *	user wants CTS/RTS flow control
		 */
		/*
		 * XXX - we do this above as well now; should we do it in
		 *	BOTH places??
		 */
		csx_Put8(line->handle, PCSER_REGS_MCR,
		    (csx_Get8(line->handle, PCSER_REGS_MCR) |
		    RTS_ON_MCR));

		if (PCSER_OUTLINE(*dev)) {
			line->state |= (PCSER_CARR_ON | PCSER_OUT);
		} else if ((line->pcser_ttycommon.t_flags & TS_SOFTCAR) ||
		    (csx_Get8(line->handle, PCSER_REGS_MSR) &
		    CD_ON_MSR)) {
			line->state |= PCSER_CARR_ON;
		}
		PCSER_HIMUTEX_EXIT(pcser);

		/*
		 * Sleep here if:
		 *  - opened with blocking and we're paying attention to modem
		 *	lines (CLOCAL not set) and there's no carrier
		 *  - we're dial IN and the device has been opened to dial OUT,
		 *	stay asleep even though carrier is on.
		 */
		if (!(oflag & (FNDELAY | FNONBLOCK)) &&
		    !(line->pcser_ttycommon.t_cflag & CLOCAL)) {
			if (!(line->state & PCSER_CARR_ON) ||
			    (line->state&PCSER_OUT &&
			    !(PCSER_OUTLINE(*dev)))) {
			line->state |= PCSER_WOPEN;
			/*
			 * there are 3 reasons that we can return from the sleep
			 *  - if we get an interrupted sys call, and the line
			 *	has not been opened by someone else, turn off
			 *	interrupts and return OPENFAIL; also, drop DTR
			 *	and RTS; we sleep on lbolt before retunring so
			 *	that an external device has time to recognize
			 *	to modem signal transitions
			 *  - we were woken up in close() by the outgoing line's
			 *	close, so go back up and restore all the line
			 *	and h/w parameters that close set to defaults
			 *  - CD occured, so just proceed normally
			 */
			if (!cv_wait_sig(&line->cvp, &line->line_mutex)) {
				line->state &= ~PCSER_WOPEN;
				if (!(line->state & PCSER_ISOPEN)) {
					PCSER_HIMUTEX_ENTER(pcser);
					csx_Put8(line->handle,
					    PCSER_REGS_IER, 0);
					pcser_dtr_off(line);
#ifdef	DROP_RTS_OLD
					csx_Put8(line->handle, PCSER_REGS_MCR,
					    (csx_Get8(line->handle,
					    PCSER_REGS_MCR) &
					    ~RTS_ON_MCR));
#else
					csx_Put8(line->handle, PCSER_REGS_MCR,
					    (csx_Get8(line->handle,
					    PCSER_REGS_MCR) |
					    RTS_ON_MCR));
#endif	/* DROP_RTS_OLD */
					PCSER_HIMUTEX_EXIT(pcser);
					/* Timeplex hack below */
					cv_wait(&lbolt_cv, &line->line_mutex);
				}
				mutex_exit(&line->line_mutex);
				return (EINTR);
			/*
			 * we got here because pcser_close() did a wakeup() on
			 * 	us or the CD line changed state; either way,
			 * 	this handles the case where we were sleeping
			 * 	waiting for CD on an incoming line, and
			 * 	either the incoming line asserted CD after
			 * 	the (modem) answered the phone and
			 * 	established a connection or we were using
			 * 	this line as an outgoing line (i.e. via tip)
			 * 	in either case, clear the line state (but
			 * 	leave the open inhibit flag alone in case
			 * 	the user wanted to unload the driver), and
			 * 	reinitialize the line, because we don't know
			 * 	what nasty things the outgoing line process
			 * 	could have done to our parameters
			 */
			} else {	/* woken up by close() or CD */
				goto again;
			}
			}
		} else {
			if (!(PCSER_OUTLINE(*dev)) &&
			    (line->state & PCSER_OUT)) {
			    /* already opened but not for dial-in */
				mutex_exit(&line->line_mutex);
				return (EBUSY);
			}
		}
	} /* !PCSER_CONTROL */

	/*
	 * now set up the streams queues
	 */
	line->pcser_ttycommon.t_readq = q;
	line->pcser_ttycommon.t_writeq = WR(q);
	q->q_ptr = (caddr_t)line;
	WR(q)->q_ptr = (caddr_t)line;

	/*
	 * start queue processing and mark the line as finished with the open
	 */
	qprocson(q);
	line->state &= ~PCSER_WOPEN;
	line->state |= PCSER_ISOPEN;
	mutex_exit(&line->line_mutex);

#ifdef	PX_DEBUG
	cmn_err(CE_CONT, "pcser_open: done\n");
#endif	/* PX_DEBUG */

	return (0);
}

/*
 * your basic close() routine
 */
/* ARGSUSED */
static int
pcser_close(queue_t *q, int flag, cred_t *credp)
{
	pcser_line_t *line;
	pcser_unit_t *pcser;

	/*
	 * get the pointer this this queue's line struct; if it's NULL, then
	 * we're already closed (but we should never see this)
	 */
	if ((line = (pcser_line_t *)q->q_ptr) == NULL)
		return (0);

	/*
	 * get a pointer to the unit structure and our registers
	 */
	pcser = line->pcser;

#ifdef	PCSER_DEBUG
	if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1)
		cmn_err(CE_CONT, "pcser_close: socket %d\n", (int)pcser->sn);
#endif

	/*
	 * if we're the control line, there's no real device attached
	 *	to us, so there's nothing really to close
	 * ignore the open inhibit flag, since pcser_open() ignores
	 *	it for a control line open
	 */
	if (line->state & PCSER_CONTROL) {
		/*
		 * cancel any outstanding bufcall() request; note that this
		 *	might cause the pending bufcall callback to run, so
		 *	we need to be sure that we don't hold any locks
		 *	that the callback routine uses or we may get a
		 *	deadlock condition.
		 */
		if (line->pcser_wbufcid) {
			unbufcall(line->pcser_wbufcid);
			line->pcser_wbufcid = 0;
		}

		/*
		 * disable the line and free the queues
		 */
		mutex_enter(&line->line_mutex);
		ttycommon_close(&line->pcser_ttycommon);

		line->state = 0;
		qprocsoff(q);
		q->q_ptr = WR(q)->q_ptr = NULL;
		mutex_exit(&line->line_mutex);
		return (0);
	}

	/*
	 * serial port close code here
	 */

	/*
	 * If we've got pending timeouts, remove them. Don't hold any
	 *	lock that the timeout routines use since they might
	 *	get run by the UNTIMEOUT, causing a deadlock.
	 */
	UNTIMEOUT(line->pcser_draintimeout_id);
	UNTIMEOUT(line->ignore_cd_timeout_id);
	UNTIMEOUT(line->restart_timeout_id);

	/*
	 * cancel any outstanding bufcall() request; note that this
	 *	might cause the pending bufcall callback to run, so
	 *	we need to be sure that we don't hold any locks
	 *	that the callback routine uses or we may get a
	 *	deadlock condition.
	 */
	if (line->pcser_wbufcid) {
		unbufcall(line->pcser_wbufcid);
		line->pcser_wbufcid = 0;
	}

	mutex_enter(&line->line_mutex);

	/*
	 * We don't need to ignore CD transitions anymore since the
	 *	next open will clear them.
	 */
	line->state &= ~PCSER_IGNORE_CD;

	/*
	 * Don't diddle the hardware unless there is a card in
	 *	the socket.
	 */
	if (CARD_PRESENT(pcser)) {

#ifdef	PCSER_DEBUG
	if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1)
		cmn_err(CE_CONT, "pcser_close: socket %d calling "
		    "pcser_xwait\n", (int)pcser->sn);
#endif

		/*
		 * wait for the transmitter to drain
		 */
		pcser_xwait(line);

		/*
		 * clear any break condition, and drop DTR if HUPCL or nobody
		 * listening if we're holding DTR high via soft carrier, raise
		 * it
		 * an interesting point here about DTR and the PCSER_DTRCLOSE
		 * flag - even though the semantics for DTR specify that DTR be
		 * held high after the close if TS_SOFTCAR is set, the
		 * zs/alm/mcp drivers drop it no matter what the state of
		 * TS_SOFTCAR (see sundev/zs_async.c in zsclose() and
		 * zsmctl()), so we provide a flag that when CLEAR (the default
		 * case), we implement the same semantics for DTR on close() as
		 * the zs/alm/mcp drivers do - if the PCSER_DTRCLOSE is SET,
		 * then TS_SOFTCAR will govern the state of DTR after the
		 * close() here is done
		 */
		PCSER_HIMUTEX_ENTER(pcser);
		if ((line->pcser_ttycommon.t_cflag & HUPCL) ||
		    (line->state & PCSER_WOPEN) ||
		    !(line->state & PCSER_ISOPEN)) {
			if ((line->pcser_ttycommon.t_flags & TS_SOFTCAR) &&
			    (line->flags & PCSER_DTRCLOSE)) {
				pcser_dtr_on(line);
			} else {
				pcser_dtr_off(line);
			}
		} /* if HUPCL */

		/*
		 * disable uart interrupts, receiver and transmitter
		 */
		csx_Put8(line->handle, PCSER_REGS_IER, 0);
		csx_Put8(line->handle, PCSER_REGS_MCR,
		    (csx_Get8(line->handle, PCSER_REGS_MCR) | RTS_ON_MCR));

		PCSER_HIMUTEX_EXIT(pcser);

		/*
		 * turn off audio as well
		 */
		pcser_auctl(line,  MODEM_SET_AUDIO_OFF);
	} /* CARD_PRESENT */

	/*
	 * We do this here because we might not get a chance to
	 *	to do it if the card was pulled out before the
	 *	close completed.  open will reset this to the
	 *	correct value for us.
	 */
	DTR_SET(line, DTR_OFF_SHADOW);

	/*
	 * Flush our buffers.
	 */
	FLUSHSILO(line);
	CHECK_RTS_ON(line);
	pcser_txflush(line);

	/*
	 * disable the line and free the queues
	 */
	ttycommon_close(&line->pcser_ttycommon);

	/*
	 * set PCSER_WCLOSE if we're sleeping in open
	 * save the state of the open inhibit flag
	 */
	if (line->state & PCSER_WOPEN) {
		line->state |= PCSER_WCLOSE;
		line->state &= ~PCSER_WOPEN;
		/*
		 * give the modem control lines some time to settle down
		 * so that the external device will have enough time to
		 * notice the DTR (and maybe RTS) transition
		 * only do this if we're waiting in pcser_open()
		 */
		cv_wait(&lbolt_cv, &line->line_mutex);
	} else {
		line->state = 0;
	} /* if PCSER_WOPEN */

	/*
	 * clean up the queue pointers and signal any potential
	 *	sleepers in pcser_open()
	 */
	cv_broadcast(&line->cvp);
	qprocsoff(q);

	line->pcser_ttycommon.t_readq = (queue_t *)NULL;
	line->pcser_ttycommon.t_writeq = (queue_t *)NULL;

	mutex_exit(&line->line_mutex);

	return (0);
}

/*
 * pcser_wput(q, mp) - Write side put procedure
 * All outgoing traffic goes through this routine - ioctls and data.
 * we silently discard any messages for the control line that aren't
 * M_IOCTL messages
 */
static int
pcser_wput(queue_t *q, mblk_t *mp)
{
	pcser_line_t *line = (pcser_line_t *)q->q_ptr;
	pcser_unit_t *pcser = line->pcser;
	struct pcser_state_t *stp;
	struct copyresp *csp;
	int size;

#ifdef	PCSER_DEBUG
	if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 250) {
		cmn_err(CE_CONT, "pcser_wput: socket %d here\n",
		    (int)pcser->sn);
	}
#endif

	switch (mp->b_datap->db_type) {
		case M_DATA:
		case M_DELAY:
		if (line->state & PCSER_CONTROL) {
			freemsg(mp);
			break;
		}
		/*
		 * put the message on the queue and start it off. pcser_start()
		 * does the idle check
		 */
		(void) putq(q, mp);
		mutex_enter(&line->line_mutex);
		pcser_start(line);
		mutex_exit(&line->line_mutex);
		break;
		/*
		 * XXX - Do we really want to generate a BREAK here?
		 */
		case M_BREAK:
		freemsg(mp);
		break;
		/*
		 * note that we really should have a general way of handling
		 *	transparent and non-transparent ioctls here and
		 *	shouldn't have to special-case all of them here
		 */
		case M_IOCTL:

#ifdef	DEBUG_PCSERIOCTL
#ifdef	PCSER_DEBUG
		if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 90)
			ioctl2text("pcser_wput",
			    (((struct iocblk *)mp->b_rptr)->ioc_cmd));
#endif
#endif

		switch (((struct iocblk *)mp->b_rptr)->ioc_cmd) {
			case TCSBRK:
			case TIOCSSOFTCAR:
			/*
			 * These changes happen in band - when all output
			 *	that preceded them has drained - we call
			 *	pcser_start() just in case we're idle
			 */
			/*
			 * XXX LOOK - see if there are any more we need to
			 *	deal with here
			 */
			case TCSETSW:
			case TCSETSF:
			case TCSETAW:
			case TCSETAF:
			case TIOCSBRK:
			case TIOCCBRK:
				(void) putq(q, mp);
				mutex_enter(&line->line_mutex);
				pcser_start(line);
				mutex_exit(&line->line_mutex);
				break;
			/*
			 * These ioctls all happen immediately
			 */
			case TCSETS:
				(void) pcser_ioctl(q, mp);
				break;
			/*
			 * handle some of the termiox stuff here
			 */
			case TCSETX:
			case TCSETXW:
			case TCSETXF:
			/*
			 * handle the driver-specific ioctl()'s here
			 */
			case PCSER_GSTATS:
			case TIOCMSET:
			case TIOCMBIS:
			case TIOCMBIC:
			case TIOCMGET:
				if (((struct iocblk *)mp->b_rptr)->ioc_count !=
				    TRANSPARENT) {
				if (mp->b_cont) {
					freemsg(mp->b_cont);
					mp->b_cont = NULL;
				}
				mp->b_datap->db_type = M_IOCNAK;
				qreply(q, mp);
				break;
			}
			switch (((struct iocblk *)mp->b_rptr)->ioc_cmd) {
				case TCSETX:
				case TCSETXW:
				case TCSETXF:
					size = sizeof (struct termiox);
					break;
				case PCSER_GSTATS:
					size = sizeof (struct pcser_stats_t);
					break;
				case TIOCMSET:
				case TIOCMBIS:
				case TIOCMBIC:
				case TIOCMGET:
					size = sizeof (int);
					break;
				}
				stp = kmem_zalloc(sizeof (struct pcser_state_t),
				    KM_NOSLEEP);
				if (stp == NULL)
				miocnak(q, mp, 0, ENOMEM);
				else {
				stp->state = PCSER_COPYIN;
				stp->addr = (caddr_t)(*(uintptr_t *)
				    (mp->b_cont->b_rptr));
				mcopyin(mp, stp, size, NULL);
				qreply(q, mp);
				}
				break;

			/*
			 * We don't understand this ioctl(), for now, just
			 *	pass it along to pcser_ioctl() where it will
			 *	either live long and prosper, or die a firey
			 *	death
			 */
			default:
#ifdef	DEBUG_PCSERIOCTL
				if (((struct iocblk *)mp->b_rptr)->ioc_count !=
				    TRANSPARENT) {
				ioctl2text("pcser_wput (!TRANSPARENT)",
				    (((struct iocblk *)
				    mp->b_rptr)->ioc_cmd));
				} else {
				ioctl2text("pcser_wput (TRANSPARENT)",
				    (((struct iocblk *)
				    mp->b_rptr)->ioc_cmd));
				}
#endif	/* DEBUG_PCSERIOCTL */
				(void) pcser_ioctl(q, mp);
				break;
		} /* switch (struct iocblk *) */
		break;
		/*
		 * do all M_IOCDATA processing in pcser_ioctl()
		 */
		case M_IOCDATA:
		csp = (struct copyresp *)mp->b_rptr;
		stp = (struct pcser_state_t *)csp->cp_private;
		switch (stp->state) {
			case PCSER_COPYOUT:
			/*
			 * free the state struct - we don't need it anymore
			 */
			if (stp)
				kmem_free(stp, sizeof (struct pcser_state_t));
			if (csp->cp_rval) {
				freemsg(mp);
				return (0);
			}
			mioc2ack(mp, NULL, 0, 0);
			qreply(q, mp);
			break;
			case PCSER_COPYIN:
			/* fall-through case */
			default:
			(void) pcser_ioctl(q, mp);
		}
		break;
		case M_FLUSH:
		/*
		 * we can't stop any transmission in progress, but we can
		 *	inhibit further xmits while flushing.
		 * FLUSHALL was FLUSHDATA
		 */
		if (*mp->b_rptr & FLUSHW) {

#ifdef	PCSER_DEBUG
			if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 250)
				cmn_err(CE_CONT, "pcser_wput: socket %d "
				    "M_FLUSH(FLUSHW)\n",
				    (int)pcser->sn);
#endif

			mutex_enter(&line->line_mutex);
			/*
			 * we grab a hardware mutex here since some of these
			 *	flags are modified in our interrupt handlers
			 */
			PCSER_HIMUTEX_ENTER(pcser);
			line->state &= ~PCSER_BUSY;
			line->state |= PCSER_FLUSH;
			PCSER_HIMUTEX_EXIT(pcser);
			flushq(q, FLUSHDATA);	/* XXX doesn't flush M_DELAY */
			*mp->b_rptr &= ~FLUSHW;
			PCSER_HIMUTEX_ENTER(pcser);
			line->state &= ~PCSER_FLUSH;
			pcser_txflush(line);
			PCSER_HIMUTEX_EXIT(pcser);
			mutex_exit(&line->line_mutex);
		}
		if (*mp->b_rptr & FLUSHR) {
#ifdef	PCSER_DEBUG
			if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 250)
				cmn_err(CE_CONT, "pcser_wput: socket %d "
				    "M_FLUSH(FLUSHR)\n",
				    (int)pcser->sn);
#endif

			flushq(RD(q), FLUSHDATA);
			PCSER_HIMUTEX_ENTER(pcser);
			FLUSHSILO(line);
			CHECK_RTS_ON(line);
			PCSER_HIMUTEX_EXIT(pcser);
			qreply(q, mp);	/* give the read queues a shot */
		} else {
#ifdef	PCSER_DEBUG
			if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 250)
				cmn_err(CE_CONT, "pcser_wput: socket %d M_FLUSH"
				    "(!FLUSHW && !FLUSHR)\n",
				    (int)pcser->sn);
#endif

			freemsg(mp);
		}
		/*
		 * We must make sure we process messages that survive the
		 * write-side flush.  Without this call, the close protocol
		 * with ldterm can hang forever.  (ldterm will have sent us a
		 * TCSBRK ioctl that it expects a response to.)
		 */
#ifdef	PCSER_DEBUG
		if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 250)
			cmn_err(CE_CONT, "pcser_wput: socket %d case M_FLUSH, "
			    "line->state 0x%x\n",
			    (int)pcser->sn, line->state);
#endif

		mutex_enter(&line->line_mutex);
		pcser_start(line);
		mutex_exit(&line->line_mutex);
		break;
		/*
		 * If we are supposed to stop, the best we can do is not xmit
		 *	anymore (the current xmit will go).  When we are told
		 *	to start, pcser_start() sees if it has work to do
		 *	(or is in progress).
		 */
		case M_STOP:

#ifdef	PCSER_DEBUG
		if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 250)
			cmn_err(CE_CONT, "pcser_wput: socket %d M_STOP\n",
			    (int)pcser->sn);
#endif

		PCSER_HIMUTEX_ENTER(pcser);
		line->state |= PCSER_STOPPED;
		line->state &= ~PCSER_BUSY;	/* XXX - per zs for POSIX fix */
		PCSER_HIMUTEX_EXIT(pcser);
		freemsg(mp);
		break;
		case M_START:

#ifdef	PCSER_DEBUG
		if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 250)
			cmn_err(CE_CONT, "pcser_wput: socket %d M_START\n",
			    (int)pcser->sn);
#endif

		if (line->state & PCSER_STOPPED) {
			PCSER_HIMUTEX_ENTER(pcser);
			line->state &= ~PCSER_STOPPED;
			PCSER_HIMUTEX_EXIT(pcser);
			mutex_enter(&line->line_mutex);
			pcser_start(line);
			mutex_exit(&line->line_mutex);
		}
		freemsg(mp);
		break;
		/*
		 * stop and start input - send flow control
		 * this only works for the serial lines
		 */
		case M_STOPI:
#ifdef	PCSER_DEBUG
		if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 250)
			cmn_err(CE_CONT, "pcser_wput: "
			    "socket %d M_STOPI (0x%x)\n",
			    (int)pcser->sn,
			    (int)line->pcser_ttycommon.t_stopc);
#endif
		if (!(line->state & PCSER_CONTROL)) {
			mutex_enter(&line->line_mutex);
			PCSER_HIMUTEX_ENTER(pcser);

			/*
			 * XXX - Should this be in-band after all other output
			 *	has drained?
			 */
			csx_Put8(line->handle, PCSER_REGS_RBRTHR,
			    line->pcser_ttycommon.t_stopc);

			PCSER_HIMUTEX_EXIT(pcser);
			mutex_exit(&line->line_mutex);
		}
		freemsg(mp);
		break;
		case M_STARTI:
#ifdef	PCSER_DEBUG
		if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 250)
			cmn_err(CE_CONT, "pcser_wput: "
			    "socket %d M_STARTI (0x%x)\n",
			    (int)pcser->sn,
			    (int)line->pcser_ttycommon.t_startc);
#endif
		if (!(line->state & PCSER_CONTROL)) {
			mutex_enter(&line->line_mutex);
			PCSER_HIMUTEX_ENTER(pcser);

			/*
			 * XXX - Should this be in-band after all other output
			 *	has drained?
			 */
			csx_Put8(line->handle, PCSER_REGS_RBRTHR,
			    line->pcser_ttycommon.t_startc);

			PCSER_HIMUTEX_EXIT(pcser);
			mutex_exit(&line->line_mutex);
		}
		freemsg(mp);
		break;
		case M_CTL:
		switch (*mp->b_rptr) {
			case MC_SERVICEIMM:
			break;
			case MC_SERVICEDEF:
			break;
			default:
			break;
		}
		freemsg(mp);
		break;
		default:
		freemsg(mp);
		break;
	}

	return (0);
}

/*
 * pcser_reioctl(line) - retry an ioctl.
 * Called when ttycommon_ioctl fails due to an allocb() failure
 */
static void
pcser_reioctl(void *arg)
{
	queue_t *q;
	mblk_t *mp;
	pcser_line_t *line = (pcser_line_t *)arg;

	/*
	 * The bufcall is no longer pending.
	 */
	mutex_enter(&line->line_mutex);
	line->pcser_wbufcid = 0;
	if ((q = line->pcser_ttycommon.t_writeq) == NULL) {
		mutex_exit(&line->line_mutex);
		return;
	}
	if ((mp = line->pcser_ttycommon.t_iocpending) != NULL) {
	    /* not pending any more */
		line->pcser_ttycommon.t_iocpending = NULL;
		mutex_exit(&line->line_mutex);
		(void) pcser_ioctl(q, mp);
	} else {
		mutex_exit(&line->line_mutex);
	}

	/* return; Bill says this is a no-no */
}

/*
 * pcser_ioctl(q, mp) - Process an "ioctl" message sent down to us.
 */
static int
pcser_ioctl(queue_t *q, mblk_t *mp)
{
	pcser_line_t *line = (pcser_line_t *)q->q_ptr;
	pcser_unit_t *pcser = line->pcser;
	struct iocblk *iocp;
	size_t datasize;
	int ioc_done = 0;
	struct pcser_state_t *stp;
	struct copyresp *csp;
	int error = 0;
	unsigned int dblkhold = mp->b_datap->db_type;
	mblk_t *cp_private;

	/*
	 * cancel any outstanding bufcall() request; note that this
	 *	might cause the pending bufcall callback to run, so
	 *	we need to be sure that we don't hold any locks
	 *	that the callback routine uses or we may get a
	 *	deadlock condition.
	 */
	if (line->pcser_wbufcid) {
		unbufcall(line->pcser_wbufcid);
		line->pcser_wbufcid = 0;
	}

	mutex_enter(&line->line_mutex);

	if (line->pcser_ttycommon.t_iocpending != NULL) {
		/*
		 * We were holding an "ioctl" response pending the
		 * availability of an "mblk" to hold data to be passed up;
		 * another "ioctl" came through, which means that "ioctl"
		 * must have timed out or been aborted.
		 */
		freemsg(line->pcser_ttycommon.t_iocpending);
		line->pcser_ttycommon.t_iocpending = NULL;
	}


	/*
	 * XXXX for the LP64 kernel the call to ttycommon_ioctl() below
	 * with message type M_IOCDATA will clobber the field cp_private
	 * so the field cp_private needs to be saved and restored later.
	 * For ILP32 kernel the field cp_rval will be clobbered by
	 * ttycommon_iocl() but then this field is already checked for
	 * stc_wput for M_IOCDATA message type before it gets clobbered.
	 * The driver should be fixed in later release to not call
	 * ttycommon_ioctl for the ioctl cmd that ttycommon_ioctl does
	 * not recognise.
	 */
	if (dblkhold == M_IOCDATA) {
		csp = (struct copyresp *)mp->b_rptr;
		cp_private = csp->cp_private;
	}

	iocp = (struct iocblk *)mp->b_rptr;

	/*
	 * ttycommon_ioctl sets up the data in pcser_ttycommon for us.
	 * The only way in which "ttycommon_ioctl" can fail is if the "ioctl"
	 * requires a response containing data to be returned to the user,
	 * and no mblk could be allocated for the data.
	 * No such "ioctl" alters our state.  Thus, we always go ahead and
	 * do any state-changes the "ioctl" calls for.  If we couldn't allocate
	 * the data, "ttycommon_ioctl" has stashed the "ioctl" away safely, so
	 * we just call "bufcall" to request that we be called back when we
	 * stand a better chance of allocating the data.
	 */
	if (datasize = ttycommon_ioctl(&line->pcser_ttycommon, q, mp, &error)) {
		if (!(line->pcser_wbufcid = bufcall(datasize, BPRI_HI,
		    pcser_reioctl, line))) {
		cmn_err(CE_CONT, "pcser_ioctl: socket %d can't allocate "
		    "streams buffer for ioctl\n",
		    (int)pcser->sn);
		miocnak(q, mp, 0, error);
		}
		mutex_exit(&line->line_mutex);
		return (0);
	}

	if (error == 0) {
	/*
	 * "ttycommon_ioctl" did most of the work; we just use the
	 *	data it set up.
	 */
		switch (iocp->ioc_cmd) {
		case TCSETS:		/* set termios immediate */
		case TCSETSW:		/* set termios 'in band' */
		case TCSETSF:		/* set termios 'in band', flush input */
		case TCSETA:		/* set termio immediate */
		case TCSETAW:		/* set termio 'in band' */
		case TCSETAF:		/* set termio 'in band', flush input */
			if (!(line->state & PCSER_CONTROL))
				pcser_param(line);
			break;
		case TIOCSSOFTCAR:
			error = miocpullup(mp, sizeof (int));
			if (error != 0)
				break;

			if ((*(int *)mp->b_cont->b_rptr) & 1)
				line->pcser_ttycommon.t_flags |= TS_SOFTCAR;
			else
				line->pcser_ttycommon.t_flags &= ~TS_SOFTCAR;
			break;
		}
	} else if (error < 0) {
	/*
	 * "ttycommon_ioctl" didn't do anything; we process it here.
	 */
		error = 0;
		if (dblkhold == M_IOCDATA) {
		csp = (struct copyresp *)mp->b_rptr;
		csp->cp_private = cp_private;
		}

		switch (iocp->ioc_cmd) {
		/*
		 * we only understand the concept of RTS/CTS flow control;
		 *	all the other termiox stuff doesn't apply to us so
		 *	we ignore it
		 * XXX - what if CRTSCTS and/or CRTSXOFF is not set, but
		 *	RTSXOFF or CTSXON is? The next call to pcser_param
		 *	will clobber the CRTSXOFF/CRTSCTS setting that we
		 *	establish here.
		 */
		case TCSETX:
		case TCSETXW:
		case TCSETXF: {
			struct termiox *tiox =
			    (struct termiox *)mp->b_cont->b_rptr;

				if (tiox->x_hflag & RTSXOFF)
				line->pcser_ttycommon.t_cflag |= CRTSXOFF;
				else
				line->pcser_ttycommon.t_cflag &= ~CRTSXOFF;

				if (tiox->x_hflag & CTSXON)
				line->pcser_ttycommon.t_cflag |= CRTSCTS;
				else
				line->pcser_ttycommon.t_cflag &= ~CRTSCTS;

				csp = (struct copyresp *)mp->b_rptr;
				stp = (struct pcser_state_t *)csp->cp_private;
				if (stp)
				kmem_free(stp, sizeof (struct pcser_state_t));
				mioc2ack(mp, NULL, 0, 0);
				/*
				 * now set the line parameters
				 */
				if (!(line->state & PCSER_CONTROL))
				pcser_param(line);
			}
			ioc_done++;
			break;
		/*
		 * there are XXX ioctl()'s that are specific to this driver:
		 *	XXX - and they are?
		 */
		case PCSER_GSTATS: {	/* get driver stats */
			struct pcser_stats_t *pcser_stats =
			    (struct pcser_stats_t *)mp->b_cont->b_rptr;
			mblk_t *datap;
			int lcmd = pcser_stats->cmd;
			int nqfretry = pcser_stats->nqfretry;
			pcser_line_t *Line;

			if ((line->state & PCSER_CONTROL) &&
			    drv_priv(iocp->ioc_cr) == 0) {
				if ((datap = allocb(
				    (sizeof (struct pcser_stats_t)),
				    BPRI_HI)) == NULL) {
					error = ENOSR;
					cmn_err(CE_CONT, "pcser_ioctl: "
					    "socket %d "
					    "can't allocate PCSER_GSTATS "
					    "block\n", (int)pcser->sn);
				} else {
				pcser_stats =
				    (struct pcser_stats_t *)datap->b_wptr;
				Line = &pcser->line;
				if (!(lcmd & STAT_SET)) {
					nqfretry = Line->pcser_stats.nqfretry;
				}
				if (lcmd & STAT_CLEAR)
					bzero(&pcser->line.pcser_stats,
					    sizeof (struct pcser_stats_t));
				bcopy(&pcser->line.pcser_stats,
				    pcser_stats, sizeof (struct pcser_stats_t));
				pcser_stats->nqfretry = nqfretry;
				Line->pcser_stats.nqfretry = nqfretry;

				if (CARD_PRESENT(Line->pcser))
					pcser_stats->flags |= CARD_IN_SOCKET;
				else
					pcser_stats->flags &= ~CARD_IN_SOCKET;

				csp = (struct copyresp *)mp->b_rptr;
				stp = (struct pcser_state_t *)csp->cp_private;
				stp->state = PCSER_COPYOUT;
				mcopyout(mp, stp, sizeof (struct pcser_stats_t),
				    stp->addr, datap);
				}
			} else { /* not root */
				error = EPERM;
			}
			}
			ioc_done++;
			break;
		case TCSBRK:
			error = miocpullup(mp, sizeof (int));
			if (error != 0)
				break;

			if (*(int *)mp->b_cont->b_rptr) { /* if !0, flush */
				if (!(line->state & PCSER_CONTROL)) {
				/* serial */
				/*EMPTY*/
#ifdef	PCSER_DEBUG
				if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 127) {
					cmn_err(CE_CONT, "pcser_ioctl: TCSBRK "
					    "(drain-start)\n");
				}
#endif
#ifdef	XXX
				/*
				 * if we're a serial line and we need to flush,
				 * the only thing I can think of doing here is
				 * to clear any pending BREAK in progress and
				 * enable the transmitter (which should also
				 * cause a received XOFF to be tossed and
				 * restart output if we're using XON/XOFF
				 * flow control)
				 */

				/* XXX - what to do here?? */
				PCSER_HIMUTEX_ENTER(pcser);
				line->state &= ~(PCSER_BUSY | PCSER_STOPPED);
				line->state &= ~PCSER_STOPPED;
				PCSER_HIMUTEX_EXIT(pcser);

				/*
				 * XXX - should we wait for both of these here?
				 */
				/*
				 * while ((line->state & PCSER_BUSY) ||
				 *			(line->pcser_txcount))
				 */
				while (line->pcser_txcount &&
				    CARD_PRESENT(pcser)) {
					cv_wait(&line->cvp, &line->line_mutex);
				}

#endif	/* XXX */

#ifdef	PCSER_DEBUG
				if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 127) {
					cmn_err(CE_CONT, "pcser_ioctl: TCSBRK "
					    "(drain-end)\n");
				}
#endif
				} /* if (!PCSER_CONTROL) */
				ioc_done++;
				/*
				 * now ACK this ioctl(), even if we're the
				 *	control line
				 * if we're supposed to do a real BREAK,
				 *	we'll ACK this ioctl() farther down
				 */
				mioc2ack(mp, NULL, 0, 0);
			} else { /* trying to do a BREAK on the control line */
				if (line->state & PCSER_CONTROL)
				error = EINVAL;
			}
			break;
		default:
			/* the rest are just for the serial ports */
			if (line->state & PCSER_CONTROL)
				error = EINVAL;
			break;
		} /* switch */

		/*
		 * if it's an ioctl specific to a serial line, handle it
		 *	here, otherwise return.
		 */
		if (!error && !(line->state & PCSER_CONTROL) && !ioc_done) {
		switch (iocp->ioc_cmd) {
		/*
		 * set a break condition on the line for 0.25sec. We do it
		 * immediately here, so the message had better have been
		 * queued in the right place!
		 */
		case TCSBRK:	/* timed BREAK */
			error = miocpullup(mp, sizeof (int));
			if (error != 0)
				break;

			if (*(int *)mp->b_cont->b_rptr == 0) {
#ifdef	PCSER_DEBUG
				if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1) {
				cmn_err(CE_CONT, "pcser_ioctl: TCSBRK "
				    "(BREAK-start)\n");
				}
#endif
				/*
				 * Wait for all output to drain before we send
				 *	a BREAK
				 */
				while (CARD_PRESENT(pcser) &&
				    ((line->state &
				    (PCSER_STOPPED | PCSER_CTSWAIT)) ||
				    (line->pcser_txcount))) {

#ifdef	PCSER_DEBUG
					if ((pcser_debug &
					    PCSER_DEBUG_LOWMASK) > 127) {
					cmn_err(CE_CONT, "TCSBRK(1): "
					    "line->state = 0x%x\n",
					    line->state);
					}
#endif

					line->pcser_timeout_id = timeout(
					    pcser_timeout, line, DRAIN_TIMEOUT);
					cv_wait(&line->cvp, &line->line_mutex);
					UNTIMEOUT(line->pcser_timeout_id);
				}

				/*
				 * Set the PCSER_BREAK and PCSER_BUSY flags;
				 *	the Tx interrupt handler will notice
				 *	PCSER_BREAK set and then it will start
				 *	the BREAK.
				 */
				if (CARD_PRESENT(pcser)) {
					PCSER_HIMUTEX_ENTER(pcser);
					line->state |= (PCSER_BREAK |
					    PCSER_BUSY);
					PCSER_HIMUTEX_EXIT(pcser);
				}

				/*
				 * Wait until the Tx handler has cleared
				 *	PCSER_BREAK so that we know we're
				 *	ready to start a BREAK.
				 */
				while (CARD_PRESENT(pcser) &&
				    line->state & PCSER_BREAK) {

#ifdef	PCSER_DEBUG
					if ((pcser_debug &
					    PCSER_DEBUG_LOWMASK) > 127) {
					cmn_err(CE_CONT, "TCSBRK(2): "
					    "line->state = 0x%x\n",
					    line->state);
					}
#endif

					/*
					 * Enable Tx interrupts so that the
					 *	next one will notice that the
					 *	PCSER_BREAK bit is set and so
					 *	it will clear the PCSER_BREAK
					 *	flag.
					 */
					PCSER_HIMUTEX_ENTER(pcser);
					csx_Put8(line->handle, PCSER_REGS_IER,
					    (csx_Get8(line->handle,
					    PCSER_REGS_IER) |
					    TX_READY_E));
					PCSER_HIMUTEX_EXIT(pcser);
					line->pcser_timeout_id =
					    timeout(pcser_timeout, line,
					    DRAIN_TIMEOUT);
					cv_wait(&line->cvp, &line->line_mutex);
					UNTIMEOUT(line->pcser_timeout_id);
				}

				/*
				 * We clear the BREAK and BUSY bits here since
				 *	the card may have been removed and the
				 *	previous cv_wait may have been woken
				 *	up via the card removal code.
				 */
				PCSER_HIMUTEX_ENTER(pcser);
				line->state &= ~(PCSER_BREAK | PCSER_BUSY);
				PCSER_HIMUTEX_EXIT(pcser);

				/*
				 * The Tx interrupt handler has finally gotten
				 *	around to noticing the PCSER_BREAK bit,
				 *	so now we start the BREAK timer.
				 */
				if (CARD_PRESENT(pcser)) {


#ifdef	PCSER_DEBUG
					if ((pcser_debug & PCSER_DEBUG_LOWMASK)
					    > 127) {
						cmn_err(CE_CONT,
						    "TCSBRK: setting "
						    "BREAK1_TIMEOUT = "
						    "0x%x\n",
						    (int)BREAK1_TIMEOUT);
					}
#endif


					line->pcser_timeout_id =
					    timeout(pcser_timeout, line,
					    BREAK1_TIMEOUT);
				    /* XXX - should be a timed wait instead */
					cv_wait(&line->cvp, &line->line_mutex);
					UNTIMEOUT(line->pcser_timeout_id);
				}

				if (CARD_PRESENT(pcser)) {
					PCSER_HIMUTEX_ENTER(pcser);
					csx_Put8(line->handle, PCSER_REGS_LCR,
					    (csx_Get8(line->handle,
					    PCSER_REGS_LCR) |
					    SET_BREAK));
					PCSER_HIMUTEX_EXIT(pcser);

#ifdef	PCSER_DEBUG
					if ((pcser_debug & PCSER_DEBUG_LOWMASK)
					    > 127) {
					cmn_err(CE_CONT, "TCSBRK: setting "
					    "BREAK2_TIMEOUT = "
					    "0x%x\n",
					    (int)BREAK2_TIMEOUT);
					}
#endif

					line->pcser_timeout_id =
					    timeout(pcser_timeout, line,
					    BREAK2_TIMEOUT);
				    /* XXX - should be a timed wait instead */
					cv_wait(&line->cvp, &line->line_mutex);
					UNTIMEOUT(line->pcser_timeout_id);
				}


				if (CARD_PRESENT(pcser)) {
					PCSER_HIMUTEX_ENTER(pcser);
					csx_Put8(line->handle,
					    PCSER_REGS_LCR,
					    (csx_Get8(line->handle,
					    PCSER_REGS_LCR) &
					    ~SET_BREAK));
					line->pcser_stats.pcser_break++;
					/*
					 * enabling the Tx interrupt should
					 *	cause the Tx interrupt handler
					 *	to start things going again (see
					 *	comment for TIOCCBRK).
					 */
					csx_Put8(line->handle, PCSER_REGS_IER,
					    (csx_Get8(line->handle,
					    PCSER_REGS_IER) |
					    TX_READY_E));
					line->state &= ~(PCSER_BREAK |
					    PCSER_BUSY);
					PCSER_HIMUTEX_EXIT(pcser);
				}

#ifdef	PCSER_DEBUG
				if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1) {
					cmn_err(CE_CONT, "pcser_ioctl: "
					    "TCSBRK (BREAK-end)\n");
				}
#endif
			} /* else (flush) case taken care of up above */
			mioc2ack(mp, NULL, 0, 0);
			break;

			case TIOCSBRK:	/* start BREAK */
			/*
			 * Start a BREAK - we don't wait for any output
			 *	to drain before we do this.
			 */
			PCSER_HIMUTEX_ENTER(pcser);
			csx_Put8(line->handle, PCSER_REGS_LCR,
			    (csx_Get8(line->handle,
			    PCSER_REGS_LCR) | SET_BREAK));
			PCSER_HIMUTEX_EXIT(pcser);

			mioc2ack(mp, NULL, 0, 0);
			break;

			case TIOCCBRK:	/* end BREAK */
			/*
			 * Finish the BREAK and enable the Tx interrupts
			 *	to start any pent-up data flowing again.
			 */
			PCSER_HIMUTEX_ENTER(pcser);
			csx_Put8(line->handle, PCSER_REGS_LCR,
			    (csx_Get8(line->handle,
			    PCSER_REGS_LCR) & ~SET_BREAK));
			csx_Put8(line->handle, PCSER_REGS_IER,
			    (csx_Get8(line->handle,
			    PCSER_REGS_IER) | TX_READY_E));
			PCSER_HIMUTEX_EXIT(pcser);

			mioc2ack(mp, NULL, 0, 0);
			break;

		    /* Set modem control lines */
			case TIOCMSET:
				PCSER_HIMUTEX_ENTER(pcser);

				if (*(int *)mp->b_cont->b_rptr & TIOCM_DTR) {
					pcser_dtr_on(line);
				} else {
					pcser_dtr_off(line);
				}

				if (*(int *)mp->b_cont->b_rptr & TIOCM_RTS)
					csx_Put8(line->handle,
					    PCSER_REGS_MCR,
					    (csx_Get8(line->handle,
					    PCSER_REGS_MCR) |
					    RTS_ON_MCR));
				else
					csx_Put8(line->handle,
					    PCSER_REGS_MCR,
					    (csx_Get8(line->handle,
					    PCSER_REGS_MCR) &
					    ~RTS_ON_MCR));
				PCSER_HIMUTEX_EXIT(pcser);
				csp = (struct copyresp *)mp->b_rptr;
				stp = (struct pcser_state_t *)csp->cp_private;
				if (stp)
					kmem_free(stp,
					    sizeof (struct pcser_state_t));

				mioc2ack(mp, NULL, 0, 0);
				line->pcser_stats.set_modem++;
				break;
		    /* Turn on modem control lines */
			case TIOCMBIS:
				PCSER_HIMUTEX_ENTER(pcser);
				if (*(int *)mp->b_cont->b_rptr & TIOCM_DTR) {
					pcser_dtr_on(line);
				}
				if (*(int *)mp->b_cont->b_rptr & TIOCM_RTS)
					csx_Put8(line->handle,
					    PCSER_REGS_MCR,
					    (csx_Get8(line->handle,
					    PCSER_REGS_MCR) |
					    RTS_ON_MCR));
				PCSER_HIMUTEX_EXIT(pcser);
				if (*(int *)mp->b_cont->b_rptr & TIOCM_AUDIO) {
					pcser_auctl(line, MODEM_SET_AUDIO_ON);
					line->saved_state |= PCSER_AUDIO_ON;
				}
				csp = (struct copyresp *)mp->b_rptr;
				stp = (struct pcser_state_t *)csp->cp_private;
				if (stp)
					kmem_free(stp,
					    sizeof (struct pcser_state_t));

				mioc2ack(mp, NULL, 0, 0);
				line->pcser_stats.set_modem++;
				break;
		    /* Turn off modem control lines */
			case TIOCMBIC:
				PCSER_HIMUTEX_ENTER(pcser);
				if (*(int *)mp->b_cont->b_rptr & TIOCM_DTR) {
					pcser_dtr_off(line);
				}
				if (*(int *)mp->b_cont->b_rptr & TIOCM_RTS)
					csx_Put8(line->handle,
					    PCSER_REGS_MCR,
					    (csx_Get8(line->handle,
					    PCSER_REGS_MCR) &
					    ~RTS_ON_MCR));
				PCSER_HIMUTEX_EXIT(pcser);
				if (*(int *)mp->b_cont->b_rptr & TIOCM_AUDIO) {
					pcser_auctl(line, MODEM_SET_AUDIO_OFF);
					line->saved_state &= ~PCSER_AUDIO_ON;
				}
				csp = (struct copyresp *)mp->b_rptr;
				stp = (struct pcser_state_t *)csp->cp_private;
				if (stp)
					kmem_free(stp,
					    sizeof (struct pcser_state_t));

				mioc2ack(mp, NULL, 0, 0);
				line->pcser_stats.set_modem++;
				break;

		    /* Return values of modem control lines */
			case TIOCMGET: {
			int *bits;
			mblk_t *datap;

			if ((datap = allocb(sizeof (int), BPRI_HI)) == NULL) {
				error = ENOSR;
				cmn_err(CE_CONT, "pcser_ioctl: socket %d "
				    "can't allocate TIOCMGET "
				    "block\n", (int)pcser->sn);
			} else {
				volatile uchar_t msr;

				bits = (int *)datap->b_wptr;
				*bits = 0;
				PCSER_HIMUTEX_ENTER(pcser);

				msr = csx_Get8(line->handle, PCSER_REGS_MSR);

				if (msr & CD_ON_MSR)
					*bits |= TIOCM_CAR;
				if (msr & DSR_ON_MSR)
					*bits |= TIOCM_DSR;
				if (msr & CTS_ON_MSR)
					*bits |= TIOCM_CTS;
				if (msr & RI_ON_MSR)
					*bits |= TIOCM_RI;
				if (DTR_GET(line))
					*bits |= TIOCM_DTR;
				if (AUDIO_GET(line))
					*bits |= TIOCM_SR;

				PCSER_HIMUTEX_EXIT(pcser);
				csp = (struct copyresp *)mp->b_rptr;
				stp = (struct pcser_state_t *)csp->cp_private;
				stp->state = PCSER_COPYOUT;
				mcopyout(mp, stp, sizeof (int), stp->addr,
				    datap);
			}
			line->pcser_stats.get_modem++;
			}
			break;
		/* We don't understand it either. */
		default:
#ifdef	PCSER_DEBUG
			if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1)
			cmn_err(CE_CONT, "pcser_ioctl: socket %d unrecognized "
			    "ioctl = 0x%x\n",
			    (int)pcser->sn,
			    (int)iocp->ioc_cmd);
#endif

			error = EINVAL;
			break;
		} /* switch */
		} /* !error && !PCSER_CONTROL */
	} /* (error < 0) */

	/*
	 * if there was an error, send a NAK, otherwise ACK
	 * this ioctl
	 */
	if (error) {
		miocnak(q, mp, 0, error);
	} else {
		qreply(q, mp);
	}

	mutex_exit(&line->line_mutex);
	return (0);
}

/*
 * pcser_rstart(line) - restart output on a line after a delay
 */
static void
pcser_restart(void *arg)
{
	pcser_line_t *line = (pcser_line_t *)arg;
	pcser_unit_t *pcser = line->pcser;

	/*
	 * If PCSER_DELAY isn't set then that means that we're running
	 *	due to an UNTIMEOUT call, so don't do anything.
	 */
	if (!(line->state & PCSER_DELAY))
		return;

	PCSER_HIMUTEX_ENTER(pcser);
	line->state &= ~PCSER_DELAY;
	PCSER_HIMUTEX_EXIT(pcser);

	/*
	 * Get a pointer to our write side queue
	 */
	if (!(line->pcser_ttycommon.t_writeq)) {
		cmn_err(CE_CONT, "pcser_restart: socket %d q is NULL\n",
		    (int)pcser->sn);
	} else {
		mutex_enter(&line->line_mutex);
		pcser_start(line);
		mutex_exit(&line->line_mutex);
	}
}

/*
 * pcser_param(line) - set up paramters for a line
 *
 *	line - pointer to pcser_line_t structure
 */
static void
pcser_param(pcser_line_t *line)
{
	tcflag_t ospeed;
	tcflag_t cflag = line->pcser_ttycommon.t_cflag;
	tcflag_t iflag = line->pcser_ttycommon.t_iflag;
	volatile uchar_t lcr = 0;
	pcser_unit_t *pcser = line->pcser;

	line->pcser_stats.set_params++;

#ifdef	CBAUDEXT
	/*
	 * Alias CRTSCTS and CRTSXOFF if necessary.
	 */
	if (pcser->flags & PCSER_USE_DUALFLOW) {

		if (cflag & CRTSXOFF)
		line->pcser_ttycommon.t_cflag |= CRTSCTS;
		else
		line->pcser_ttycommon.t_cflag &= ~CRTSCTS;

		if (cflag & CRTSCTS)
		line->pcser_ttycommon.t_cflag |= CRTSXOFF;
		else
		line->pcser_ttycommon.t_cflag &= ~CRTSXOFF;

	} /* if PCSER_USE_DUALFLOW */
#endif

	/*
	 * Convert termios-type speed to canonical speed. This
	 *	function may return an invalid or unsupported
	 *	speed.
	 */
	ospeed = pcser_convert_speed(line->pcser, cflag);

#ifdef	PCSER_DEBUG
	if (pcser_debug & PCSER_DEBUG_PARAM) {
		char dbuff[1024];

		pcser_show_baud(ospeed, dbuff);

		switch (cflag & CSIZE) {
			case CS8:
			(void) strcat(dbuff, "CHAR_8 ");
			break;
			case CS7:
			(void) strcat(dbuff, "CHAR_7 ");
			break;
			case CS6:
			(void) strcat(dbuff, "CHAR_6 ");
			break;
			default:
			(void) strcat(dbuff, "CHAR_5 ");
			break;
		}

		if (cflag & CSTOPB)
			(void) strcat(dbuff, "STOP_2 ");
		else
			(void) strcat(dbuff, "STOP_1 ");

		if (cflag & PARENB) {
			(void) strcat(dbuff, "USE_P ");
			if (!(iflag & INPCK))
			(void) strcat(dbuff, "IGNORE_P ");
			if (cflag & PARODD)
			(void) strcat(dbuff, "ODD_P ");
			else
			(void) strcat(dbuff, "EVEN_P ");
		} else {
			(void) strcat(dbuff, "NO_PARITY ");
		}

		if (cflag & CRTSCTS)
			(void) strcat(dbuff, "CRTSCTS ");
		else
			(void) strcat(dbuff, "NO_CRTSCTS ");

		if (cflag & CRTSXOFF)
			(void) strcat(dbuff, "CRTSXOFF ");
		else
			(void) strcat(dbuff, "NO_CRTSXOFF ");

		if (iflag & IXOFF)
			(void) strcat(dbuff, "IXOFF ");
		else
			(void) strcat(dbuff, "NO_IXOFF ");

		if (iflag & IXON)
			(void) strcat(dbuff, "IXON ");
		else
			(void) strcat(dbuff, "NO_IXON ");

		if (iflag & IXANY)
			(void) strcat(dbuff, "IXANY ");
		else
			(void) strcat(dbuff, "NO_IXANY ");

		cmn_err(CE_CONT, "pcser_param: socket %d %s\n",
		    (int)line->pcser->sn, dbuff);

	} /* PCSER_DEBUG_PARAM */
#endif

	/*
	 * Hang up if zero speed, but continue processing
	 *	the remainder of the parameters.
	 * Note that we need to delay a while here so that
	 *	some modems will pick up the DTR transition.
	 */
	if (ospeed == 0) {
		delay(PCSER_DTR_DROP_DELAY);
		PCSER_HIMUTEX_ENTER(pcser);
		pcser_dtr_off(line);
		PCSER_HIMUTEX_EXIT(pcser);
		delay(PCSER_DTR_DROP_DELAY);
	}

	/*
	 * character length, parity and stop bits
	 * CS5 is 00, so we don't need a case for it
	 */
	switch (cflag & CSIZE) {
		case CS8:
		lcr |= CHAR_8;
		break;
		case CS7:
		lcr |= CHAR_7;
		break;
		case CS6:
		lcr |= CHAR_6;
		break;
	}

	if (cflag & CSTOPB)
		lcr |= STOP_2;

	/*
	 * do the parity stuff here - if we get a parity error,
	 * it will show up in the receive exception interrupt
	 * handler (pcser_rcvex())
	 */
	if (cflag & PARENB) {
		if (!(iflag & INPCK))	/* checking input parity? */
		lcr |= IGNORE_P;	/* nope, ignore input parity */
		lcr |= USE_P;		/* do normal parity processing */
		if (!(cflag & PARODD))
		lcr |= EVEN_P;
	} /* lcr case is no parity at all */

	PCSER_HIMUTEX_ENTER(pcser);
	csx_Put8(line->handle, PCSER_REGS_LCR, lcr);

	/*
	 * Rx/Tx speeds - we don't do split since the 8250-type
	 *	UARTs don't support it.
	 * Note: Must be called after LCR is initialized.
	 */
	(void) pcser_set_baud(line, ospeed);

	/*
	 * Check for CTS/RTS flow control
	 *
	 * If we're using CTS/RTS flow control, enable automatic RTS
	 *	if the modem supports it; in any case, PUTSILO will
	 *	also see (cflag & CRTSXOFF) set and will manage RTS
	 *	there as well
	 * Since the CRTS_IFLOW and CCTS_OFLOW flags have the same
	 *	values as the CRTSXOFF and CRTSCTS flags, we handle
	 *	both flags here by default. The TERMIOX flags are
	 *	handled in pcser_ioctl.
	 * XXX - need to keep a shadow of iir to handle different
	 *	FIFO sizes
	 */
	if (cflag & CRTSXOFF) {
		volatile uchar_t _tempvar = 0;

		if (line->cis_vars.flags & PCSER_FIFO_ENABLE)
		_tempvar |= line->cis_vars.fifo_enable;

		if (line->cis_vars.flags & PCSER_AUTO_RTS)
		_tempvar |= line->cis_vars.auto_rts;

		csx_Put8(line->handle, PCSER_REGS_IIR, _tempvar);
	} else {
		/*
		 * This will also turn off AUTO_RTS (if the card
		 *	supports it). We need to keep the FIFO enabled
		 *	all the time (if the card has a FIFO).
		 */
		volatile uchar_t _tempvar = 0;

		if (line->cis_vars.flags & PCSER_FIFO_ENABLE) {
		_tempvar |= line->cis_vars.fifo_enable;
		}

		csx_Put8(line->handle, PCSER_REGS_IIR, _tempvar);

		_tempvar = csx_Get8(line->handle, PCSER_REGS_MCR) | RTS_ON_MCR;
		csx_Put8(line->handle, PCSER_REGS_MCR, _tempvar);
	}

	/*
	 * check for XON/XOFF flow control
	 * this is only done for the transmitter, i.e. when we
	 * receive an XOFF, the UART will stop transmitting
	 * until we get an XON (or any char if IXANY is set)
	 * we let streams handle IXOFF (the receiver side of
	 * things) until the code gets into the receiver
	 * interrupt handler
	 * XXX - should this be added to PUTSILO(), just as we
	 * handle the RTS line there, or should we let the
	 * STREAMS code deal with it?
	 */

	/*
	 * set the receive flow control flag for the receive
	 *	interrupt handler so that it can pop an XOFF to
	 *	the remote when the receive silo becomes close to full
	 * XXX - see comment above about PUTSILO() control of this
	 */
	if (iflag & IXOFF)
		line->state |= PCSER_IXOFF;
	else
		line->state &= ~PCSER_IXOFF;

	PCSER_HIMUTEX_EXIT(pcser);
}

/*
 * pcser_xwait(line) - waits for transmitter to drain
 *
 *    this routine will wait for the bytes in the cd-180's transmitter to
 *	drain by requesting a transmitter empty interrupt; it is assumed
 *	that pcser_txcount is 0, meaning that all of the data from the
 *	driver's Tx buffer has been transferred to the cd-180's Tx fifo
 *
 */
static void
pcser_xwait(pcser_line_t *line)
{
	pcser_unit_t *pcser = line->pcser;
	uchar_t ier;

	/*
	 * save the old interrupt mask in case we're being called from
	 * ioctl, enable only the xmit interrupt and let the transmitter
	 * service routine wake us up when the xmit buffer is empty
	 * if we get interrupted from sleep, then disable all interrupts
	 * because there's probably a good reason for the interrupt
	 * if we're draining on the line because of an PCSER_DCONTROL ioctl(),
	 * then don't bother to wait for the rest of the data to be shifted
	 * out of the UART, because the line is probably hosed anyway
	 */
	if (!(line->state & PCSER_DRAIN) && CARD_PRESENT(pcser)) {
		do {
		PCSER_HIMUTEX_ENTER(pcser);
		line->state |= PCSER_XWAIT;
		ier = csx_Get8(line->handle, PCSER_REGS_IER);
		csx_Put8(line->handle, PCSER_REGS_IER, TX_EMPTY_E);

		/*
		 * if we just want to blast through this close without
		 *	waiting for software flow control semantics,
		 *	disable s/w flow control (special character
		 *	detection) and	hit the Tx enable bit in the
		 *	CCR; this will start data moving again if it's
		 *	been stopped due to a received XOFF character
		 */
		if (line->flags & PCSER_CFLOWFLUSH) {
			FLUSHSILO(line);	/* XXX */
			CHECK_RTS_ON(line);
			pcser_txflush(line);
		}
		PCSER_HIMUTEX_EXIT(pcser);

		/*
		 * post a timer so that we don't hang here forever
		 * the timer will be removed from the queue by pcser_xmit()
		 * when the transmitter becomes empty, or if we get
		 * interrupted while in the sleep()
		 */
		line->pcser_timeout_id = timeout(pcser_timeout,
		    line, PCSER_TIMEOUT);
		if (!cv_wait_sig(&line->cvp, &line->line_mutex)) {
			/* interrupted here, so disable interrupts */
			UNTIMEOUT(line->pcser_timeout_id);
			ier = 0;
			line->pcser_txcount = 0;
		} else {
			/* XXX *** LOOK vvvv this shouldn't be here ***XXX */
			UNTIMEOUT(line->pcser_timeout_id);
			/* XXX*** LOOK ^^^^ this shouldn't be here ***XXX */
		}

		/*
		 * If there's no card anymore, just return.
		 */
		if (!CARD_PRESENT(pcser)) {
			line->pcser_txcount = 0;
			return;
		}

		/*
		 * restore the old transmitter and receiver enable states
		 * and the interrupt enable register
		 */
		PCSER_HIMUTEX_ENTER(pcser);
		/* restore old interrupt mask */
		csx_Put8(line->handle, PCSER_REGS_IER, ier);
		PCSER_HIMUTEX_EXIT(pcser);
		} while (line->pcser_txcount && (!(line->state & PCSER_DRAIN)));
	} /* if (!PCSER_DRAIN) */
}

static void
pcser_timeout(void *arg)
{
	pcser_line_t *line = (pcser_line_t *)arg;

	mutex_enter(&line->line_mutex);
	cv_broadcast(&line->cvp);
	mutex_exit(&line->line_mutex);
}

/*
 * pcser_start(line) - start output on a line
 *
 * this handles both the serial lines and the ppc
 */
static void
pcser_start(pcser_line_t *line)
{

	queue_t *q;
	mblk_t *bp;
	int cc, bytesleft;
	pcser_unit_t *pcser = line->pcser;
	uchar_t *current;

	/*
	 * Get a pointer to our write side queue
	 */
	if ((q = line->pcser_ttycommon.t_writeq) == NULL) {
		cmn_err(CE_CONT, "pcser_start: socket %d q is NULL\n",
		    (int)pcser->sn);
		goto out;		/* not attached to a stream */
	}

	/*
	 * we grab a hardware mutex here since some of these flags are modified
	 *	in our interrupt handlers
	 */
	PCSER_HIMUTEX_ENTER(pcser);
	if ((line->state &
	    (PCSER_BREAK | PCSER_BUSY | PCSER_DELAY | PCSER_FLUSH)) &&
	    !(line->state & PCSER_DRAIN)) {

#ifdef	PCSER_DEBUG
		if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 254) {
		cmn_err(CE_CONT, "pcser_start: doing zip because: ");
		if (line->state & PCSER_BREAK)
			cmn_err(CE_CONT, "PCSER_BREAK ");
		if (line->state & PCSER_BUSY)
			cmn_err(CE_CONT, "PCSER_BUSY ");
		if (line->state & PCSER_FLUSH)
			cmn_err(CE_CONT, "PCSER_FLUSH ");
		if (line->state & PCSER_DELAY)
			cmn_err(CE_CONT, "PCSER_DELAY ");
		if (line->state & PCSER_DRAIN)
			cmn_err(CE_CONT, "PCSER_DRAIN ");
		cmn_err(CE_CONT, "\n");
		}
#endif

#ifdef	XXX
		/*
		 * set the PCSER_TXWORK flag since we weren't able to do
		 *	anything this time around
		 * XXX - we should really only set this if we are being called
		 *	from pcser_softint
		 */
		line->state |= PCSER_TXWORK;
#endif	/* XXX */
		PCSER_HIMUTEX_EXIT(pcser);
		goto out;	/* we're in the middle of something already */
	}

	/*
	 * setup the local transmit buffer stuff
	 */
	bytesleft = line->pcser_txbufsize;
	current = line->pcser_txbuf;
	line->pcser_txcount = 0;
	line->state &= ~PCSER_TXWORK;
	PCSER_HIMUTEX_EXIT(pcser);

	/*
	 * handle next message block (if any)
	 */
	while ((bp = getq(q)) != NULL) {
		switch (bp->b_datap->db_type) {
		case M_IOCDATA:
			cmn_err(CE_CONT, "pcser_start: M_IOCDATA received\n");
			freemsg(bp);
			break;
		/*
		 * For either delay or an ioctl, we need to process them when
		 * any characters seen so far have drained - if there aren't any
		 * we are in the right place at the right time.
		 */
		case M_IOCTL:
			if (bytesleft != line->pcser_txbufsize) {
				(void) putbq(q, bp);
				goto transmit;
			}
			mutex_exit(&line->line_mutex);
			(void) pcser_ioctl(q, bp);
			mutex_enter(&line->line_mutex);
			break;
		case M_DELAY:
			if (bytesleft != line->pcser_txbufsize) {
				(void) putbq(q, bp);
				goto transmit;
			}
			PCSER_HIMUTEX_ENTER(pcser);
			line->state &= ~PCSER_DELAY;
			PCSER_HIMUTEX_EXIT(pcser);
			UNTIMEOUT(line->restart_timeout_id);
			PCSER_HIMUTEX_ENTER(pcser);
			line->state |= PCSER_DELAY;
			PCSER_HIMUTEX_EXIT(pcser);
			line->restart_timeout_id = timeout(pcser_restart,
			    line,
			    (int)(*(unsigned char *) bp->b_rptr));
			freemsg(bp);
			/*
			 * exit this right now; we'll get called back
			 *	by pcser_restart() to continue
			 *	processing any other messages that
			 *	are still on our queue
			 */
			goto out;
			/* NOTREACHED */
		/*
		 * suck up all the data we can from these mesages until we
		 * run out of data messages (above) or we fill the txbuf
		 * if we're draining the queue (PCSER_DRAIN set), just suck
		 * every last byte of data out and pretend that we have
		 * transmitted it to the device
		 */
		case M_DATA: {
			mblk_t *nbp;
			/*
			 * If output is stopped, then just put this block back
			 *	and return.  This test is done here instead of
			 *	at the top of pcser_start so that if output
			 *	is stopped, we can still process non-M_DATA
			 *	messages.
			 */
			if (!(line->state & PCSER_DRAIN) &&
			    (line->state &
			    (PCSER_STOPPED | PCSER_CTSWAIT))) {
				(void) putbq(q, bp);
				return;
			}
			do {
				if (!(line->state & PCSER_DRAIN)) {
				while ((cc = (bp->b_wptr - bp->b_rptr)) > 0) {
					if (!bytesleft) {
					(void) putbq(q, bp);
					goto transmit;
					}
					PCSER_HIMUTEX_ENTER(pcser);
					cc = MIN(cc, bytesleft);
					bcopy(bp->b_rptr, current, cc);
					line->pcser_txcount += cc;
					current += cc;
					bytesleft -= cc;
					bp->b_rptr += cc;
					PCSER_HIMUTEX_EXIT(pcser);
				}
				} /* PCSER_DRAIN */
				nbp = bp;
				bp = bp->b_cont;
				freeb(nbp);
			} while (bp != NULL);
			} /* case M_DATA */
			break;
		default:
			freemsg(bp);
			goto out;
			/* NOTREACHED */
		}
	}

transmit:
#ifdef PCSER_DEBUG
	if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 250) {
		cmn_err(CE_CONT, "pcser_start: socket %d txcount=%d, "
		    "txbufsize=%d, bytesleft=%d\n",
		    (int)pcser->sn, line->pcser_txcount,
		    line->pcser_txbufsize, bytesleft);
	}
#endif
	if (CARD_PRESENT(pcser) &&
	    (!(line->state & (PCSER_DRAIN | PCSER_CONTROL)))) {
		if ((cc = (line->pcser_txbufsize - bytesleft)) > 0) {
		volatile uchar_t ier;

#ifdef PCSER_DEBUG
		if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 250) {
			cmn_err(CE_CONT, "pcser_start: socket %d xmit %d\n",
			    (int)pcser->sn, line->pcser_txcount);
		}
#endif
		/*
		 * enable the transmitter empty interrupts and away we go
		 */
		PCSER_HIMUTEX_ENTER(pcser);
		line->state |= PCSER_BUSY;
		ier = csx_Get8(line->handle, PCSER_REGS_IER);
		ier |= TX_READY_E;
		csx_Put8(line->handle, PCSER_REGS_IER, ier);
		PCSER_HIMUTEX_EXIT(pcser);
		}
	} /* CARD_PRESENT */

out:
	;
}

#ifdef	DEBUG_PCSER
print_bits(int bits)
{

	cmn_err(CE_CONT, "print_bits: 0x%x\n", bits);
	if (bits & TIOCM_LE)
		cmn_err(CE_CONT, "TIOCM_LE ");
	if (bits & TIOCM_DTR)
		cmn_err(CE_CONT, "TIOCM_DTR ");
	if (bits & TIOCM_RTS)
		cmn_err(CE_CONT, "TIOCM_RTS ");
	if (bits & TIOCM_ST)
		cmn_err(CE_CONT, "TIOCM_ST ");
	if (bits & TIOCM_SR)
		cmn_err(CE_CONT, "TIOCM_SR ");
	if (bits & TIOCM_CTS)
		cmn_err(CE_CONT, "TIOCM_CTS ");
	if (bits & TIOCM_CAR)
		cmn_err(CE_CONT, "TIOCM_CAR ");
	if (bits & TIOCM_RI)
		cmn_err(CE_CONT, "TIOCM_RI ");
	if (bits & TIOCM_DSR)
		cmn_err(CE_CONT, "TIOCM_DSR ");
	cmn_err(CE_CONT, "\n");
}
#endif	/* DEBUG_PCSER */

#ifdef	DEBUG_PCSERIOCTL
ioctl2text(char *tag, int ioc_cmd)
{
	int i = 0, found = 0;

	while (ioc_txt[i].name) {
		if (ioc_txt[i].ioc_cmd == ioc_cmd) {
		if (!found) {
			cmn_err(CE_CONT, "%s: M_IOCTL(0x%x) [%s]\n",
			    tag, ioc_cmd, ioc_txt[i].name);
		} else {
			cmn_err(CE_CONT, "... and M_IOCTL(0x%x) [%s]\n",
			    ioc_cmd, ioc_txt[i].name);
		}
		found = 1;
		}
		i++;
	}

	if (!found)
		cmn_err(CE_CONT, "%s: M_IOCTL(0x%x) [(unknown)]\n",
		    tag, ioc_cmd);
}
#endif

/*
 * pcser_set_baud - load baud rate generator divisor latches
 *
 * The 8250-type UARTs don't support separate receive and
 *	transmit speeds.
 *
 *	calling:	ospeed - baud rate in canonical format
 *
 *	returns:	0 - if speed is in range
 *			1 - if speed is out of range
 *
 * Note: This function must be protected by a hw_mutex by the caller
 */
static int
pcser_set_baud(pcser_line_t *line, tcflag_t ospeed)
{
	uchar_t lo_baud, hi_baud;
	volatile uchar_t lcr;

	/*
	 * If this speed is out of range, then display a message
	 *	since this should be an invalid condition, then
	 *	return an error.
	 */
	if (ospeed >= PCSER_MAX_SPEEDS) {
		cmn_err(CE_CONT, "pcser_set_baud: socket %d speed out of "
		    "range 0x%x\n", (int)line->pcser->sn,
		    (int)ospeed);
		return (1);
	}

	/*
	 * If this is an unsupported speed, then just fail silently,
	 *	do not return an error.
	 * From termios(7I):
	 *	For any particular hardware, impossible
	 *	speed changes are ignored.
	 * Note: This will take care of the case of an unsupported
	 *	speed as well as the B0 "speed".
	 */
	if (pcser_baud_table[ospeed] == 0)
		return (0);

	lo_baud = pcser_baud_table[ospeed] & 0x0ff;
	hi_baud = (pcser_baud_table[ospeed] >> 8) & 0x0ff;

	lcr = csx_Get8(line->handle, PCSER_REGS_LCR) & ~DLAB;

	csx_Put8(line->handle, PCSER_REGS_LCR, DLAB);
	OUTB_DELAY();
	csx_Put8(line->handle, PCSER_REGS_RBRTHR, lo_baud);
	OUTB_DELAY();
	csx_Put8(line->handle, PCSER_REGS_IER, hi_baud);
	OUTB_DELAY();
	csx_Put8(line->handle, PCSER_REGS_LCR, lcr);
	OUTB_DELAY();

	return (0);
}

#ifdef	PCSER_DEBUG
static char *pcser_text_speeds[PCSER_MAX_SPEEDS] = { "B0", "B50", "B75",
	"B110", "B134", "B150", "B200", "B300", "B600", "B1200", "B1800",
	"B2400", "B4800", "B9600", "B19200", "B38400", "B57600", "B76800",
	"B115200", "B153600", "B230400", "B307200", "B460800" };

void
pcser_show_baud(tcflag_t ospeed, char *buf)
{

	if (ospeed >= PCSER_MAX_SPEEDS)
		(void) sprintf(buf, "[invalid speed 0x%x] ", (int)ospeed);
	else
		(void) sprintf(buf, "%s ", pcser_text_speeds[ospeed]);
}
#endif


static int
pcser_card_insertion(pcser_unit_t *pcser, int priority)
{
	pcser_line_t *line = &pcser->line;
	pcser_cis_vars_t *cis_vars = &line->cis_vars;
	pcser_cftable_t *cftable = NULL;
	int ret = CS_OUT_OF_RESOURCE;

	if (priority & CS_EVENT_PRI_LOW) {
		make_device_node_t make_device_node;
		devnode_desc_t *dnd;
		get_status_t get_status;

		/*
		 * Parse the CIS and setup the variables.
		 */
		if ((ret = pcser_parse_cis(pcser, &cftable)) != CS_SUCCESS) {
		cmn_err(CE_CONT, "pcser: socket %d error parsing "
		    "CIS information\n", (int)pcser->sn);
		pcser_destroy_cftable_list(&cftable);
		mutex_enter(&pcser->event_hilock);
		pcser->card_state |= PCSER_READY_ERR;
		cv_broadcast(&pcser->readywait_cv);
		mutex_exit(&pcser->event_hilock);
		return (ret);
		} else {
		io_req_t io_req;
		irq_req_t irq_req;
		sockevent_t sockevent;
		config_req_t config_req;
		pcser_cftable_t *cft;
		int pcser_insert_ready_cnt;

		line->pcser_txbufsize = cis_vars->txbufsize;
		line->pcser_rxfifo_size = cis_vars->rxbufsize;

		/*
		 * Check to see if the card is ready - if not, loop a
		 *	few times until it either is ready, or we
		 *	exhaust the loop counter.
		 */
		pcser_insert_ready_cnt = PCSER_INSERT_READY_CNT;
		do {
		    /* XXX function return value ignored */
			(void) csx_GetStatus(pcser->client_handle, &get_status);
			if (!(get_status.CardState & CS_EVENT_CARD_READY)) {
#ifdef PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_READY)
				cmn_err(CE_CONT, "pcser_card_insertion:"
				" doing %d mS delay (1) \n", 20);
#endif
			delay(PCSER_INSERT_READY_TMO1);
			} else {
			pcser_insert_ready_cnt = 0;
			} /* if (!CS_EVENT_CARD_READY) */
		} while (pcser_insert_ready_cnt--);

		if (cis_vars->ready_delay_1 > 0) {
#ifdef	PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_READY_DELAY)
			cmn_err(CE_CONT, "pcser_card_insertion: socket %d (1) "
			    "doing %d mS READY delay\n",
			    (int)pcser->sn,
			    (int)cis_vars->ready_delay_1);
#endif
			delay(MS2HZ(cis_vars->ready_delay_1));
#ifdef	PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_READY_DELAY)
			cmn_err(CE_CONT, "........READY delay done.\n");
#endif
		}

		(void) csx_GetStatus(pcser->client_handle, &get_status);

#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_READY) {
			cmn_err(CE_CONT, "pcser_card_insertion: socket %d (1)"
			    "get_status.CardState = 0x%x [%s]\n",
			    (int)pcser->sn,
			    get_status.CardState,
			    (get_status.CardState &
			    CS_EVENT_CARD_READY)?
			    "CS_EVENT_CARD_READY":
			    "card NOT ready");
		}
#endif
		if (!(get_status.CardState & CS_EVENT_CARD_READY)) {
			pcser_destroy_cftable_list(&cftable);
			mutex_enter(&pcser->event_hilock);
			pcser->card_state |= PCSER_READY_ERR;
			cv_broadcast(&pcser->readywait_cv);
			mutex_exit(&pcser->event_hilock);
			return (CS_CARD_NOT_READY);
		}

		cft = cftable;
		/*
		 * Try to allocate IO resources; if we fail to get
		 *	an IO range from the system, then we exit
		 *	since there's not much we can do.
		 */
		while (cft) {
			io_req.BasePort1.base = cft->p.modem_base;

			/*
			 * Some cards only specify an odd-number of
			 *	registers, leaving out the scratchpad
			 *	register, so if we see an odd number
			 *	of registers, add one to the request.
			 */
			if (cft->p.length & 1)
			cft->p.length++;
			io_req.NumPorts1 = cft->p.length;
			io_req.Attributes1 = IO_DATA_PATH_WIDTH_8;
			io_req.Attributes2 = 0;
			io_req.NumPorts2 = 0;
			io_req.IOAddrLines = cft->p.addr_lines;

#ifdef	PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_CIS) {
			cmn_err(CE_CONT, "=== pcser_card_insertion: "
			    "socket %d trying ===\n",
			    (int)pcser->sn);
			pcser_display_cftable_list(cft, 1);
			}

#endif

			if ((ret = csx_RequestIO(pcser->client_handle,
			    &io_req)) == CS_SUCCESS) {

			/*
			 * We found a good IO range, so save the
			 *	information and break out of this
			 *	loop.
			 */

#ifdef	PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_CIS) {
				cmn_err(CE_CONT, "pcser_card_insertion: "
				    "socket %d "
				    "selected configuration "
				    "index 0x%x\n",
				    (int)pcser->sn,
				    cft->p.config_index);
			}
#endif

			cis_vars->modem_base = cft->p.modem_base;
			cis_vars->length = cft->p.length;
			cis_vars->addr_lines = cft->p.addr_lines;
			cis_vars->modem_vcc = cft->p.modem_vcc;
			cis_vars->modem_vpp1 = cft->p.modem_vpp1;
			cis_vars->modem_vpp2 = cft->p.modem_vpp2;
			cis_vars->pin = cft->p.pin;
			cis_vars->config_index = cft->p.config_index;
			break;
#ifdef	PCSER_DEBUG
			} else {
			error2text_t cft;

			if (pcser_debug & PCSER_DEBUG_CIS) {
				cft.item = ret;
				(void) csx_Error2Text(&cft);
				cmn_err(CE_CONT, "pcser: socket %d RequestIO "
				    "returns %s\n",
				    (int)pcser->sn, cft.text);
			}
#endif

			} /* RequestIO */
			cft = cft->next;
		} /* while (cft) */

		/*
		 * Now destroy the config table entries list since
		 *	we don't need it anymore.
		 */
		pcser_destroy_cftable_list(&cftable);

		/*
		 * If we weren't able to get an IO range, then report that
		 *	to the user and return.
		 */
		if (!cft) {
			cmn_err(CE_CONT,
			    "pcser_card_insertion: socket %d unable "
			    "to get IO range\n", (int)pcser->sn);
			mutex_enter(&pcser->event_hilock);
			pcser->card_state |= PCSER_READY_ERR;
			cv_broadcast(&pcser->readywait_cv);
			mutex_exit(&pcser->event_hilock);
			return (ret);
		}

		line->handle = io_req.BasePort1.handle;

		mutex_enter(&pcser->event_hilock);
		pcser->flags |= PCSER_REQUESTIO;
		mutex_exit(&pcser->event_hilock);

#ifdef	PCSER_DEBUG
if (pcser_debug & PCSER_DEBUG_CISVARS) {
	int i;
	cmn_err(CE_CONT,
	    "	  flags: 0x%x\n", (int)cis_vars->flags);
	cmn_err(CE_CONT,
	    "   present mask: 0x%x\n", (int)cis_vars->present);
	cmn_err(CE_CONT,
	    "   PRR pin mask: 0x%x\n", (int)cis_vars->pin);
	cmn_err(CE_CONT,
	    " major_revision: 0x%x\n", (int)cis_vars->major_revision);
	cmn_err(CE_CONT,
	    " minor_revision: 0x%x\n", (int)cis_vars->minor_revision);
	cmn_err(CE_CONT,
	    "manufacturer_id: 0x%x\n", (int)cis_vars->manufacturer_id);
	cmn_err(CE_CONT,
	    "	card_id: 0x%x\n", (int)cis_vars->card_id);
	cmn_err(CE_CONT,
	    "    config_base: 0x%x\n", (int)cis_vars->config_base);
	cmn_err(CE_CONT,
	    "     modem_base: 0x%x\n", (int)cis_vars->modem_base);
	cmn_err(CE_CONT,
	    "   config_index: 0x%x\n", (int)cis_vars->config_index);
	cmn_err(CE_CONT,
	    "      txbufsize: 0x%x bytes\n", (int)cis_vars->txbufsize);
	cmn_err(CE_CONT,
	    "      rxbufsize: 0x%x bytes\n", (int)cis_vars->rxbufsize);
	cmn_err(CE_CONT,
	    "    fifo_enable: 0x%x\n", (int)cis_vars->fifo_enable);
	cmn_err(CE_CONT,
	    "   fifo_disable: 0x%x\n", (int)cis_vars->fifo_disable);
	cmn_err(CE_CONT,
	    "       auto_rts: 0x%x\n", (int)cis_vars->auto_rts);
	cmn_err(CE_CONT,
	    "       auto_cts: 0x%x\n", (int)cis_vars->auto_cts);
	cmn_err(CE_CONT,
	    "      modem_vcc: %d volts\n", ((int)cis_vars->modem_vcc)/10);
	cmn_err(CE_CONT,
	    "     modem_vpp1: %d volts\n", ((int)cis_vars->modem_vpp1)/10);
	cmn_err(CE_CONT,
	    "     modem_vpp2: %d volts\n", ((int)cis_vars->modem_vpp2)/10);
	cmn_err(CE_CONT,
	    "     addr_lines: %d\n", (int)cis_vars->addr_lines);
	cmn_err(CE_CONT,
	    "	 length: %d bytes\n", (int)cis_vars->length);
	cmn_err(CE_CONT,
	    "  ready_delay_1: %d mS\n", (int)cis_vars->ready_delay_1);
	cmn_err(CE_CONT,
	    "  ready_delay_2: %d mS\n", (int)cis_vars->ready_delay_2);
	cmn_err(CE_CONT,
	    " CD_ignore_time: %d mS\n",
	    (int)pcser->line.pcser_ignore_cd_time);

	for (i = 0; i < CISTPL_VERS_1_MAX_PROD_STRINGS; i++)
		if (cis_vars->prod_strings[i] && cis_vars->prod_strings[i][0])
		cmn_err(CE_CONT, "           [%d]: [%s]\n", i,
		    cis_vars->prod_strings[i]);
}
#endif

#ifdef	PCSER_DEBUG
	if (pcser_debug & PCSER_DEBUG_CIS)
	    cmn_err(CE_CONT, "pcser_card_insertion: socket %d "
				"line->handle 0x%p\n",
				(int)pcser->sn, (void *)line->handle);
#endif

		/*
		 * Allocate an IRQ.
		 */
		irq_req.Attributes = IRQ_TYPE_EXCLUSIVE;
		irq_req.irq_handler = (csfunction_t *)pcser_poll;
		irq_req.irq_handler_arg = (caddr_t)pcser;

		if ((ret = csx_RequestIRQ(pcser->client_handle,
		    &irq_req)) != CS_SUCCESS) {
			error2text_t cft;

			cft.item = ret;
			(void) csx_Error2Text(&cft);
			cmn_err(CE_CONT,
			    "pcser: socket %d RequestIRQ failed %s\n",
			    (int)pcser->sn, cft.text);
			mutex_enter(&pcser->event_hilock);
			pcser->card_state |= PCSER_READY_ERR;
			cv_broadcast(&pcser->readywait_cv);
			mutex_exit(&pcser->event_hilock);
			return (ret);
		} /* RequestIRQ */

		/*
		 * Initialize the mutex that protects the UART registers.
		 */
		mutex_enter(&pcser->event_hilock);
		pcser->pcser_mutex = &pcser->irq_mutex;
		mutex_init(pcser->pcser_mutex, NULL, MUTEX_DRIVER,
		    *(irq_req.iblk_cookie));
		pcser->flags |= PCSER_REQUESTIRQ;
		pcser->unid_irq = 0;
		mutex_exit(&pcser->event_hilock);

		/*
		 * Set up the client event mask to give us card ready
		 *	events as well as what other events we have already
		 *	registered for.
		 * Note that since we set the global event mask in the call
		 *	to RegisterClient in pcser_attach, we don't have to
		 *	duplicate those events in this event mask.
		 */
		sockevent.Attributes = CONF_EVENT_MASK_CLIENT;
		if ((ret = csx_GetEventMask(pcser->client_handle,
		    &sockevent)) != CS_SUCCESS) {
			error2text_t cft;

			cft.item = ret;
			(void) csx_Error2Text(&cft);

			cmn_err(CE_CONT, "pcser_card_insertion: socket %d "
			    "GetEventMask failed %s \n",
			    (int)pcser->sn, cft.text);
			mutex_enter(&pcser->event_hilock);
			pcser->card_state |= PCSER_READY_ERR;
			cv_broadcast(&pcser->readywait_cv);
			mutex_exit(&pcser->event_hilock);
			return (ret);
		} /* GetEventMask */

		sockevent.EventMask |= CS_EVENT_CARD_READY;

		if ((ret = csx_SetEventMask(pcser->client_handle,
		    &sockevent)) != CS_SUCCESS) {
			error2text_t cft;

			cft.item = ret;
			(void) csx_Error2Text(&cft);

			cmn_err(CE_CONT, "pcser_card_insertion: socket %d "
			    "SetEventMask failed %s\n",
			    (int)pcser->sn, cft.text);
			mutex_enter(&pcser->event_hilock);
			pcser->card_state |= PCSER_READY_ERR;
			cv_broadcast(&pcser->readywait_cv);
			mutex_exit(&pcser->event_hilock);
			return (ret);
		} /* SetEventMask */

		/*
		 * Configure the card.
		 */
		config_req.Attributes = 0;
		config_req.Vcc = cis_vars->modem_vcc;
		config_req.Vpp1 = cis_vars->modem_vpp1;
		config_req.Vpp2 = cis_vars->modem_vpp2;
		config_req.IntType = SOCKET_INTERFACE_MEMORY_AND_IO;
		config_req.ConfigBase = cis_vars->config_base;
		config_req.Status = CCSR_SIG_CHG; /* XXX CS is broken here */
		config_req.Status = 0;
		config_req.Pin = cis_vars->pin;
		config_req.Copy = 0;
		config_req.ExtendedStatus = 0;
		config_req.ConfigIndex = cis_vars->config_index;
		/* only write to registers which we want to change */
		config_req.Present = cis_vars->present &
		    (CONFIG_OPTION_REG_PRESENT | CONFIG_PINREPL_REG_PRESENT);

#ifdef  PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_CIS) {
			cmn_err(CE_CONT, "pcser_card_insertion: "
			    "card_id=0x%x ConfigIndex 0x%x \n",
			    cis_vars->card_id, config_req.ConfigIndex);
		}
#endif

		if ((ret = csx_RequestConfiguration(
		    pcser->client_handle, &config_req)) != CS_SUCCESS) {
			error2text_t cft;

			cft.item = ret;
			(void) csx_Error2Text(&cft);
			cmn_err(CE_CONT,
			    "pcser: socket %d RequestConfiguration "
			    "failed %s\n",
			    (int)pcser->sn, cft.text);
			mutex_enter(&pcser->event_hilock);
			pcser->card_state |= PCSER_READY_ERR;
			cv_broadcast(&pcser->readywait_cv);
			mutex_exit(&pcser->event_hilock);
			return (ret);
		} /* RequestConfiguration */

		mutex_enter(&pcser->event_hilock);
		pcser->flags |= PCSER_REQUESTCONFIG;
		mutex_exit(&pcser->event_hilock);

		/*
		 * Check to see if the card is ready - if not, loop a
		 *	few times until it either is ready, or we
		 *	exhaust the loop counter.
		 */
		pcser_insert_ready_cnt = PCSER_INSERT_READY_CNT;
		do {
		    /* XXX function return value ignored */
		    (void) csx_GetStatus(pcser->client_handle, &get_status);
		    if (!(get_status.CardState & CS_EVENT_CARD_READY)) {
#ifdef PCSER_DEBUG
			if (pcser_debug & PCSER_DEBUG_READY)
			    cmn_err(CE_CONT, "pcser_card_insertion:"
				" doing %d mS delay (2) \n", 200);
#endif
			delay(PCSER_INSERT_READY_TMO2);
		    } else {
			pcser_insert_ready_cnt = 0;
		    } /* if (!CS_EVENT_CARD_READY) */
		} while (pcser_insert_ready_cnt--);

		if (cis_vars->ready_delay_2 > 0) {
#ifdef	PCSER_DEBUG
		    if (pcser_debug & PCSER_DEBUG_READY_DELAY)
			cmn_err(CE_CONT, "pcser_card_insertion: socket %d (2) "
					"doing %d mS READY delay\n",
					(int)pcser->sn,
					(int)cis_vars->ready_delay_2);
#endif
		    delay(MS2HZ(cis_vars->ready_delay_2));
#ifdef	PCSER_DEBUG
		    if (pcser_debug & PCSER_DEBUG_READY_DELAY)
			cmn_err(CE_CONT, "........READY delay done.\n");
#endif
		}
		(void) csx_GetStatus(pcser->client_handle, &get_status);
#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_READY) {
		    cmn_err(CE_CONT, "pcser_card_insertion: socket %d (2) "
					"get_status.CardState = 0x%x [%s]\n",
					(int)pcser->sn,
					get_status.CardState,
					(get_status.CardState &
						CS_EVENT_CARD_READY)?
						"CS_EVENT_CARD_READY":
						"card NOT ready");
		}
#endif
		if (!(get_status.CardState & CS_EVENT_CARD_READY)) {
			mutex_enter(&pcser->event_hilock);
			pcser->card_state |= PCSER_READY_ERR;
			cv_broadcast(&pcser->readywait_cv);
			mutex_exit(&pcser->event_hilock);
			return (CS_CARD_NOT_READY);
		}

	    } /* pcser_parse_cis */

/* XXX */
		/*
		 * Turn off card interrupts
		 */
	    csx_Put8(line->handle, PCSER_REGS_IER, 0);
	    OUTB_DELAY();
/* XXX */

		/*
		 * Create the minor devices for this instance; we create
		 *	three devices per card:
		 *
		 *		a dial-in device (/dev/term)
		 *		a dial-out device (/dev/cua)
		 *		a control device
		 * The MakeDeviceNode function will prepend the socket
		 *	number to the device name, so we don't have to
		 *	generate unique device names here.
		 */
	    make_device_node.Action = CREATE_DEVICE_NODE;
	    make_device_node.NumDevNodes = 2;

	    make_device_node.devnode_desc =
		kmem_zalloc(sizeof (struct devnode_desc) *
		make_device_node.NumDevNodes, KM_SLEEP);

	    dnd = &make_device_node.devnode_desc[0];
	    dnd->name = "pcser";
	    dnd->spec_type = S_IFCHR;
	    dnd->minor_num = PCSER_DINODE(pcser->sn);
	    dnd->node_type = DDI_NT_SERIAL;

	    dnd = &make_device_node.devnode_desc[1];
	    dnd->name = "pcser,cu";
	    dnd->spec_type = S_IFCHR;
	    dnd->minor_num = PCSER_DONODE(pcser->sn);
	    dnd->node_type = DDI_NT_SERIAL_DO;

#ifdef	XXX_PCSER_CTL_DEVICE
	    dnd = &make_device_node.devnode_desc[2];
	    dnd->name = "pcser,ctl";
	    dnd->spec_type = S_IFCHR;
	    dnd->minor_num = PCSER_CTLNODE(pcser->sn);
	    dnd->node_type = DDI_PSEUDO;
#endif	/* XXX_PCSER_CTL_DEVICE */

	    if ((ret = csx_MakeDeviceNode(pcser->client_handle,
					&make_device_node)) != CS_SUCCESS) {
		error2text_t cft;

		cft.item = ret;
		(void) csx_Error2Text(&cft);

		cmn_err(CE_CONT, "pcser_card_insertion: socket %d "
					"MakeDeviceNode failed %s\n",
						(int)pcser->sn, cft.text);
		mutex_enter(&pcser->event_hilock);
		pcser->card_state |= PCSER_READY_ERR;
		cv_broadcast(&pcser->readywait_cv);
		mutex_exit(&pcser->event_hilock);
		return (ret);

	    } /* MakeDeviceNode */

	    mutex_enter(&pcser->event_hilock);
	    pcser->flags |= PCSER_MAKEDEVICENODE;
	    cv_broadcast(&pcser->readywait_cv);
	    mutex_exit(&pcser->event_hilock);

		/*
		 * We don't need this structure anymore since we've
		 *	created the devices.  If we need to keep track
		 *	of the devices that we've created for some reason,
		 *	then you'll want to keep this structure and the
		 *	make_device_node_t structure around in a global
		 *	data area.
		 */
	    kmem_free(make_device_node.devnode_desc,
			sizeof (struct devnode_desc) *
				make_device_node.NumDevNodes);

	    make_device_node.devnode_desc = NULL;

		/*
		 * Check the card status - if the card is READY, then just
		 *	note it, otherwise, setup a timer to wait for the card
		 *	to become ready.
		 */
	    /* XXX function return value ignored */
	    (void) csx_GetStatus(pcser->client_handle, &get_status);

#ifdef	PCSER_DEBUG
	    if (pcser_debug & PCSER_DEBUG_CIS)
		cmn_err(CE_CONT, "pcser_card_insertion: socket %d "
				"get_status.CardState 0x%x\n",
				(int)pcser->sn, (int)get_status.CardState);
#endif

	    if (get_status.CardState & CS_EVENT_CARD_READY) {
		mutex_enter(&pcser->event_hilock);
		pcser->card_state |= PCSER_CARD_IS_READY;
		mutex_exit(&pcser->event_hilock);
		(void) pcser_card_ready(pcser, CS_EVENT_PRI_LOW);

#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_CIS)
		    cmn_err(CE_CONT, "pcser_card_insertion: socket %d "
					"card is READY\n", (int)pcser->sn);
#endif
	    } else {
		/*
		 * Set up a card ready timeout so that if the card
		 *	doesn't assert the READY bit in the PRR within
		 *	the PCSER_READY_TIMEOUT period, we won't hang.
		 */
		pcser->ready_timeout_id = timeout(pcser_card_ready_timeout,
						(caddr_t)pcser,
						PCSER_READY_TIMEOUT);
		mutex_enter(&pcser->event_hilock);
		pcser->card_state |= PCSER_WAIT_FOR_READY;
		mutex_exit(&pcser->event_hilock);

#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_CIS)
		    cmn_err(CE_CONT, "pcser_card_insertion: socket %d "
					"setting PCSER_WAIT_FOR_READY\n",
							(int)pcser->sn);
#endif

	    } /* CS_EVENT_CARD_READY */

	    line->state |= PCSER_FIRST_OPEN;

	} /* CS_EVENT_PRI_LOW */

	return (CS_SUCCESS);
}

static int
pcser_card_removal(pcser_unit_t *pcser, int priority)
{
	pcser_line_t *line = &pcser->line;
	pcser_cis_vars_t *cis_vars = &line->cis_vars;
	sockevent_t sockevent;
	int ret;

	/*
	 * Remove the card ready timer, since the card is gone and any
	 *	ready events or ready timeout that we get will be bogus.
	 * Note that we can't hold the pcser->event_hilock mutex when
	 *	we do this since the timeout routine calls the card
	 *	ready routine, which grabs the same mutex.  Holding the
	 *	pcser->event_hilock mutex here could lead to a deadlock
	 *	condition if the timeout thread runs while we're in the
	 *	untimeout function.
	 */
	if (priority & CS_EVENT_PRI_LOW) {
		UNTIMEOUT(pcser->ready_timeout_id);

		/*
		 * We don't need to ignore CD transitions anymore since the
		 *	card is gone.
		 */
		line->state &= ~PCSER_IGNORE_CD;

		/*
		 * If we're coming in here at a low priority, we need to grab
		 *	the pcser->event_hilock mutex so that we can clear some
		 *	of the card state flags.  If we're coming in at high
		 *	priority, the caller has already grabbed this mutex.
		 */
		mutex_enter(&pcser->event_hilock);
	}

	pcser->card_state &= ~(PCSER_CARD_INSERTED | PCSER_WAIT_FOR_READY |
	    PCSER_READY_ERR);

	/*
	 * If we're being called at high priority, we can't do much more
	 *	than note that the card went away.
	 */
	if (priority & CS_EVENT_PRI_HIGH)
		return (CS_SUCCESS);

	/*
	 * FLush the soft silos since there isn't any hardware.
	 */
	FLUSHSILO(line);
	pcser_txflush(line);

	/*
	 * Set up the client event mask to not give us card ready
	 *	events; we will still receive other events we have
	 *	registered for.
	 * Note that since we set the global event mask in the call
	 *	to RegisterClient in pcser_attach, we don't have to
	 *	duplicate those events in this event mask.
	 */
	sockevent.Attributes = CONF_EVENT_MASK_CLIENT;
	if ((ret = csx_GetEventMask(pcser->client_handle,
	    &sockevent)) != CS_SUCCESS) {
		error2text_t cft;

		cft.item = ret;
		(void) csx_Error2Text(&cft);

		cmn_err(CE_CONT, "pcser_card_removal: socket %d "
		"GetEventMask failed %s \n",
		    (int)pcser->sn, cft.text);
		mutex_exit(&pcser->event_hilock);
		return (ret);
	} /* GetEventMask */

	sockevent.EventMask &= ~CS_EVENT_CARD_READY;

	if ((ret = csx_SetEventMask(pcser->client_handle,
	    &sockevent)) != CS_SUCCESS) {
		error2text_t cft;

		cft.item = ret;
		(void) csx_Error2Text(&cft);

		cmn_err(CE_CONT, "pcser_card_removal: socket %d "
		"SetEventMask failed %s\n",
		    (int)pcser->sn, cft.text);
		mutex_exit(&pcser->event_hilock);
		return (ret);
	} /* SetEventMask */

	if (pcser->flags & PCSER_REQUESTCONFIG) {
		modify_config_t modify_config;
		release_config_t release_config;

#ifdef  PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_CISVARS) {
			cmn_err(CE_CONT, "pcser_card_removal: "
			    "flags=0x%x card_id=0x%x \n",
			    pcser->flags, cis_vars->card_id);
		}
#endif

		/*
		 * First, turn off VPP1 and VPP2 since we don't need
		 *	them anymore. This will take care of the case
		 *	of the driver being unloaded but the card
		 *	still being present.
		 */
		modify_config.Attributes =
		    (CONF_VPP1_CHANGE_VALID | CONF_VPP2_CHANGE_VALID);
		modify_config.Vpp1 = 0;
		modify_config.Vpp2 = 0;

		ret = csx_ModifyConfiguration(pcser->client_handle,
		    &modify_config);

		if ((ret != CS_NO_CARD) && (ret != CS_SUCCESS)) {

			error2text_t cft;

			cft.item = ret;
			(void) csx_Error2Text(&cft);
			cmn_err(CE_CONT, "pcser: socket %d "
			    "ModifyConfiguration (Vpp1/Vpp2)"
			    " failed %s\n",
			    (int)pcser->sn, cft.text);
		} /* ModifyConfig. != (CS_NO_CARD || CS_SUCCESS) */

		/*
		 * Release card configuration.
		 */
		if ((ret = csx_ReleaseConfiguration(pcser->client_handle,
		    &release_config)) != CS_SUCCESS) {
			error2text_t cft;

			cft.item = ret;
			(void) csx_Error2Text(&cft);
			cmn_err(CE_CONT, "pcser: socket %d "
			    "ReleaseConfiguration failed %s\n",
			    (int)pcser->sn, cft.text);
		} /* ReleaseConfiguration */
		pcser->flags &= ~PCSER_REQUESTCONFIG;
	} /* PCSER_REQUESTCONFIG */

	if (pcser->flags & PCSER_REQUESTIRQ) {
		irq_req_t irq_req;

		/*
		 * Release allocated IRQ resources.
		 */
		if ((ret = csx_ReleaseIRQ(pcser->client_handle,
		    &irq_req)) != CS_SUCCESS) {
		error2text_t cft;

		cft.item = ret;
		(void) csx_Error2Text(&cft);
		cmn_err(CE_CONT, "pcser: socket %d ReleaseIRQ failed %s\n",
		    (int)pcser->sn, cft.text);
		} /* ReleaseIRQ */
		/*
		 * XXX - We have to think about this whole IRQ mutex thing here
		 */
		mutex_destroy(pcser->pcser_mutex);
		pcser->pcser_mutex = &pcser->noirq_mutex;
		pcser->flags &= ~PCSER_REQUESTIRQ;
	} /* PCSER_REQUESTIRQ */

	if (pcser->flags & PCSER_REQUESTIO) {
		/*
		 * Release allocated IO resources.
		 */
		{
			io_req_t io_req; /* XXX */
			if ((ret = csx_ReleaseIO(pcser->client_handle,
			    &io_req)) != CS_SUCCESS) {
				error2text_t cft;

				cft.item = ret;
				(void) csx_Error2Text(&cft);
				cmn_err(CE_CONT,
				    "pcser: socket %d ReleaseIO failed %s\n",
				    (int)pcser->sn, cft.text);
			} /* ReleaseIO */
		} /* XXX */
		pcser->flags &= ~PCSER_REQUESTIO;
	} /* PCSER_REQUESTIO */

	/*
	 * Remove all the device nodes.  We don't have to explictly
	 *	specify the names if we want Card Services to remove
	 *	all of the devices.
	 * Note that when you call MakeDeviceNode with the Action
	 *	argument set to REMOVAL_ALL_DEVICE_NODES, the
	 *	NumDevNodes must be zero.
	 */
	if (pcser->flags & PCSER_MAKEDEVICENODE) {
		make_device_node_t make_device_node;

		make_device_node.Action = REMOVAL_ALL_DEVICE_NODES;
		make_device_node.NumDevNodes = 0;

		if ((ret = csx_MakeDeviceNode(pcser->client_handle,
		    &make_device_node)) != CS_SUCCESS) {
		error2text_t cft;

		cft.item = ret;
		(void) csx_Error2Text(&cft);

		cmn_err(CE_CONT, "pcser_card_removal: socket %d "
		    "MakeDeviceNode failed %s\n",
		    (int)pcser->sn, cft.text);
		} /* MakeDeviceNode */
		pcser->flags &= ~PCSER_MAKEDEVICENODE;
	} /* PCSER_MAKEDEVICENODE */

	if (pcser->flags & PCSER_ATTACHOK) {
		/*
		 * Since the card went away, send an M_HANGUP message
		 *	downstream
		 */
		line->state &= ~(PCSER_STOPPED | PCSER_CTSWAIT);

		/*
		 * PCSER_RXWORK is set for the M_HANGUP message to get
		 *	downstream
		 * PCSER_TXWORK is set so that the PCSER_DRAIN will proogate
		 *	through to pcser_start() and flush the downstream
		 *	queues
		 * PCSER_CVBROADCAST is set to wake up any sleepers in open
		 */
		line->state |= (PCSER_MHANGUP | PCSER_RXWORK |
		    PCSER_TXWORK | PCSER_DRAIN |
		    PCSER_CVBROADCAST);

		ddi_trigger_softintr(pcser->softint_id);
	} /* PCSER_ATTACHOK */

	mutex_exit(&pcser->event_hilock);

	/*
	 * Wakeup all sleepers so that they can get the current card state.
	 */
	cv_broadcast(&line->cvp);

	return (CS_SUCCESS);
}

/*
 * pcser_event - this is the event handler
 */
static int
pcser_event(event_t event, int priority, event_callback_args_t *eca)
{
	pcser_unit_t *pcser = eca->client_data;
	pcser_line_t *line = &pcser->line;
	client_info_t *ci = &eca->client_info;
	int retcode = CS_UNSUPPORTED_EVENT;

#ifdef	DEBUG
	if (pcser_debug_events) {
		pcser_debug_report_event(pcser, event, priority);
	}
#endif

	if (priority & CS_EVENT_PRI_HIGH) {
		mutex_enter(&pcser->event_hilock);
	} else {
		mutex_enter(&line->line_mutex);
	}

#ifdef	PCSER_DEBUG
	if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1) {
		event2text_t event2text;
		event2text.event = event;

		(void) csx_Event2Text(&event2text);

		cmn_err(CE_CONT, "pcser_event[%d]: socket %d \n"
		    "\tevent %s (0x%x) priority 0x%x\n",
		    pcser->instance, (int)pcser->sn,
		    event2text.text, (int)event, priority);
	}
#endif

	/*
	 * Find out which event we got and do the appropriate thing
	 */
	switch (event) {
		case CS_EVENT_REGISTRATION_COMPLETE:
		break;
		case CS_EVENT_CARD_READY:
		retcode = pcser_card_ready(pcser, priority);
		break;
		case CS_EVENT_CARD_INSERTION:
		retcode = pcser_card_insertion(pcser, priority);
		break;
		/*
		 * Note that we get two CS_EVENT_CARD_REMOVAL events -
		 *  one at high priority and the other at low priority.
		 *  This is determined by the setting of the
		 *  CS_EVENT_CARD_REMOVAL_LOWP bit in either of the
		 *  event masks.
		 *  (See the call to RegisterClient).
		 */
		case CS_EVENT_CARD_REMOVAL:
		retcode = pcser_card_removal(pcser, priority);
		break;
		case CS_EVENT_CLIENT_INFO:
		if (GET_CLIENT_INFO_SUBSVC(ci->Attributes) ==
		    CS_CLIENT_INFO_SUBSVC_CS) {
			ci->Revision = 0x7329;
			ci->CSLevel = CS_VERSION;
			ci->RevDate = PCSER_REV_DATE;
			(void) strcpy(ci->ClientName, PCSER_CLIENT_DESCRIPTION);
			(void) strcpy(ci->VendorName, PCSER_VENDOR_DESCRIPTION);
			ci->Attributes |= CS_CLIENT_INFO_VALID;
			retcode = CS_SUCCESS;
		} /* CS_CLIENT_INFO_SUBSVC_CS */
		break;
#ifdef	XXX
		/*
		 * XXX - How do we handle the PM events?
		 * We handle the CS_EVENT_PM_SUSPEND event the same
		 *	way that we handle a CS_EVENT_CARD_REMOVAL event
		 *	since if we're being asked to suspend, then we
		 *	can't tell if the same card is inserted after
		 *	a resume.
		 */
		case CS_EVENT_PM_SUSPEND:
		break;
#endif
	}

	if (priority & CS_EVENT_PRI_HIGH) {
		mutex_exit(&pcser->event_hilock);
	} else {
		mutex_exit(&line->line_mutex);
	}

	return (retcode);
}

/*
 * pcser_card_ready - handle card ready events or card ready timeouts
 *
 * If we're being called at CS_EVENT_PRI_HIGH priority, then we can't
 *	do much.
 */
static int
pcser_card_ready(pcser_unit_t *pcser, int priority)
{
	int ret = CS_SUCCESS;

#ifdef	PCSER_DEBUG
	if (pcser_debug & PCSER_DEBUG_READY)
		cmn_err(CE_CONT, "pcser_card_ready:"
		    " socket %d priority=%x card_state=%x\n",
		    (int)pcser->sn, priority, pcser->card_state);
#endif

	if (priority & CS_EVENT_PRI_HIGH)
		return (CS_SUCCESS);

	/*
	 * Remove any pending CARD_READY timer.
	 */
	UNTIMEOUT(pcser->ready_timeout_id);

	if (pcser->card_state & (PCSER_WAIT_FOR_READY | PCSER_CARD_IS_READY)) {
		modify_config_t modify_config;

		mutex_enter(&pcser->event_hilock);
		pcser->card_state &= ~(PCSER_WAIT_FOR_READY |
		    PCSER_CARD_IS_READY |
		    PCSER_READY_ERR) | PCSER_READY_WAIT;
		mutex_exit(&pcser->event_hilock);

		modify_config.Socket = pcser->sn;
		modify_config.Attributes =
		    (CONF_IRQ_CHANGE_VALID | CONF_ENABLE_IRQ_STEERING);

		if ((ret = csx_ModifyConfiguration(
		    pcser->client_handle,
		    &modify_config)) != CS_SUCCESS) {
		error2text_t cft;

		cft.item = ret;
		(void) csx_Error2Text(&cft);
		cmn_err(CE_CONT, "pcser: socket %d ModifyConfiguration "
		    "(IRQ) failed %s\n",
		    (int)pcser->sn, cft.text);
		} else {
		mutex_enter(&pcser->event_hilock);
		pcser->card_state |= PCSER_CARD_INSERTED;
		mutex_exit(&pcser->event_hilock);
		} /* ModifyConfiguration */
	} /* if (PCSER_WAIT_FOR_READY) */

	if (pcser->card_state & PCSER_READY_WAIT) {
		UNTIMEOUT(pcser->readywait_timeout_id);
		mutex_enter(&pcser->event_hilock);
		pcser->card_state &= ~PCSER_READY_WAIT;
		pcser->card_state |= PCSER_CARD_INSERTED;
		cv_broadcast(&pcser->readywait_cv);
		mutex_exit(&pcser->event_hilock);
	}

#ifdef  PCSER_DEBUG
	if (pcser_debug & PCSER_DEBUG_READY)
		cmn_err(CE_CONT, "pcser_card_ready: exit"
		    " card_state=%x PCSER_CARD_INSERTED=%x\n",
		    pcser->card_state, PCSER_CARD_INSERTED);
#endif

	return (CS_SUCCESS);
}

static void
pcser_card_ready_timeout(void *arg)
{
	pcser_unit_t	*pcser = (pcser_unit_t *)arg;

#ifdef	PCSER_DEBUG
	if (pcser_debug & PCSER_DEBUG_READY)
		cmn_err(CE_CONT, "pcser_card_ready_timeout: socket %d\n",
		    (int)pcser->sn);
#endif
	(void) pcser_card_ready(pcser, CS_EVENT_PRI_LOW);
}

static void
pcser_readywait_timeout(void *arg)
{
	pcser_unit_t	*pcser = (pcser_unit_t *)arg;

#ifdef	PCSER_DEBUG
	if (pcser_debug & PCSER_DEBUG_READY)
		cmn_err(CE_CONT, "pcser_readywait_timeout: socket %d\n",
		    (int)pcser->sn);
#endif

	mutex_enter(&pcser->event_hilock);
	pcser->card_state |= PCSER_READY_ERR;
	cv_broadcast(&pcser->readywait_cv);
	mutex_exit(&pcser->event_hilock);
}

static void
pcser_ignore_cd_timeout(void *arg)
{
	pcser_line_t *line = (pcser_line_t *)arg;

#ifdef	PCSER_DEBUG
	pcser_unit_t *pcser = line->pcser;
#endif

	mutex_enter(&line->line_mutex);
	line->state &= ~PCSER_IGNORE_CD;

#ifdef	XXX
	PCSER_HIMUTEX_ENTER(pcser);
	msr = csx_Get8(line->handle, PCSER_REGS_MSR);
	if (!(msr & CD_ON_MSR))
		msr |= CD_CHANGE;
	serviced = pcser_modem(pcser, msr, PCSER_DONTCALL);
	PCSER_HIMUTEX_EXIT(pcser);
#endif

	mutex_exit(&line->line_mutex);

#ifdef	XXX
	/*
	 * if we've got any work to do, schedule a softinterrupt
	 */
	if (serviced == DDI_INTR_CLAIMED)
		ddi_trigger_softintr(pcser->softint_id);
#endif

#ifdef	PCSER_DEBUG
	if ((pcser_debug & PCSER_DEBUG_LOWMASK) > 1)
		cmn_err(CE_CONT,
		    "pcser_ignore_cd_timeout: socket %d looking for "
		    "CD transitions\n", (int)pcser->sn);
#endif
}

int pcser_outbdelay = 0;

static void
OUTB_DELAY()
{
	if (pcser_outbdelay)
		drv_usecwait(pcser_outbdelay);

}

/*
 * pcser_convert_speed - convert termios-type speed to canonical
 *				speed. This function may return
 *				an invalid or unsupported speed.
 *
 *	calling:	pcser - pointer to unit structure
 *			cflag - termios cflag
 *	returns:	tcflag_t - canonical speed
 *
 * Note: The CBAUDEXT flag was added to Solaris 2.5 as a way
 *		to support speeds greater than the traditional
 *		sixteen that UNIX has supported for years. Since
 *		this driver may be built on 2.4 or greater, we
 *		use an #ifdef so that if CBAUDEXT is not defined,
 *		only one of the sixteen traditional speeds will
 *		be returned.
 */
/* ARGSUSED */
static tcflag_t
pcser_convert_speed(pcser_unit_t *pcser, tcflag_t c_cflag)
{
	tcflag_t ospeed;

	ospeed = c_cflag & CBAUD;

#ifdef	CBAUDEXT
	if (c_cflag & CBAUDEXT)
		ospeed = (c_cflag & CBAUD) + CBAUD + 1;
#else
	/*
	 * If CBAUDEXT is not defined, then that means that
	 *	we're being built on a system without high
	 *	speed extensions, so we allow the user to
	 *	replace the two lowest speeds with useful
	 *	higher speeds.
	 */
	if (pcser->flags & PCSER_USE_HIGHSPEED) {
		SWITCH(ospeed) {
		case B50:
			ospeed = 16;	/* B57600 */
			break;
		case B75:
			ospeed = 18;	/* B115200 */
			break;
		} /* switch */
	} /* if (PCSER_USE_HIGHSPEED) */
#endif

	return (ospeed);
}

/*
 * Functions to set/clear DTR
 */
static void
pcser_dtr_off(pcser_line_t *line)
{

	if (line->flags & PCSER_DTRFORCE) {
		pcser_dtr_on(line);
	} else {

		if (line->pcser->flags & PCSER_REQUESTCONFIG) {
		volatile uchar_t mcr;

		mcr = csx_Get8(line->handle, PCSER_REGS_MCR);
		mcr &= ~DTR_ON_MCR;
		csx_Put8(line->handle, PCSER_REGS_MCR, mcr);
		}
		line->dtr_shadow = (uchar_t)DTR_OFF_SHADOW;
	}
}

static void
pcser_dtr_on(pcser_line_t *line)
{

	if (line->pcser->flags & PCSER_REQUESTCONFIG) {
		volatile uchar_t mcr;

		mcr = csx_Get8(line->handle, PCSER_REGS_MCR);
		mcr |= DTR_ON_MCR;
		csx_Put8(line->handle, PCSER_REGS_MCR, mcr);
	}

	line->dtr_shadow = (uchar_t)DTR_ON_SHADOW;
}

#ifndef	USE_MACRO_RTSCTS
static void
CHECK_RTS_OFF(pcser_line_t *line)
{
	volatile uchar_t mcr;

	/*
	 * Don't do anything if the card hasn't been configured yet.
	 */
	if (!(line->pcser->flags & PCSER_REQUESTCONFIG))
		return;

	if (line->pcser_ttycommon.t_cflag & CRTSXOFF) {
		if (line->pcser_sscnt > line->pcser_hiwater) {
#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_RTSCTS) {
			if (!(line->state & PCSER_RTSOFF_MESSAGE)) {
			line->state |= PCSER_RTSOFF_MESSAGE;
			line->state &= ~PCSER_RTSON_MESSAGE;
			cmn_err(CE_CONT, "CHECK_RTS: socket %d turned "
			    "off RTS @ %d\n",
			    (int)line->pcser->sn,
			    line->pcser_sscnt);
			} /* if PCSER_RTSOFF_MESSAGE */
		} /* if PCSER_DEBUG_RTSCTS */
#endif
		mcr = csx_Get8(line->handle, PCSER_REGS_MCR);
		mcr &= ~RTS_ON_MCR;
		csx_Put8(line->handle, PCSER_REGS_MCR, mcr);
		} /* if pcser_hiwater */
	} /* if CRTSXOFF */
}

static void
CHECK_RTS_ON(pcser_line_t *line)
{
	volatile uchar_t mcr;

	/*
	 * Don't do anything if the card hasn't been configured yet.
	 */
	if (!(line->pcser->flags & PCSER_REQUESTCONFIG))
		return;

	if (line->pcser_ttycommon.t_cflag & CRTSXOFF) {
		if (line->pcser_sscnt < line->pcser_lowwater) {
#ifdef	PCSER_DEBUG
		if (pcser_debug & PCSER_DEBUG_RTSCTS) {
			if (!(line->state & PCSER_RTSON_MESSAGE)) {
			line->state |= PCSER_RTSON_MESSAGE;
			line->state &= ~PCSER_RTSOFF_MESSAGE;
			cmn_err(CE_CONT, "CHECK_RTS: socket %d turned "
			    "on RTS @ %d\n",
			    (int)line->pcser->sn,
			    line->pcser_sscnt);
			} /* if PCSER_RTSON_MESSAGE */
		} /* if PCSER_DEBUG_RTSCTS */
#endif
		mcr = csx_Get8(line->handle, PCSER_REGS_MCR);
		mcr |= RTS_ON_MCR;
		csx_Put8(line->handle, PCSER_REGS_MCR, mcr);
		} /* if pcser_lowwater */
	} /* if CRTSXOFF */
}
#endif	/* USE_MACRO_RTSCTS */




#ifdef	DEBUG
static void
pcser_debug_report_event(pcser_unit_t *pcser, event_t event, int priority)
{
	char		*event_priority;
	char		*event_text;
	char		buf[64];

	event_priority = (priority & CS_EVENT_PRI_HIGH) ? "high" : "low";

	switch (event) {
	case CS_EVENT_REGISTRATION_COMPLETE:
		event_text = "Registration Complete";
		break;
	case CS_EVENT_PM_RESUME:
		event_text = "Power Management Resume";
		break;
	case CS_EVENT_CARD_INSERTION:
		event_text = "Card Insertion";
		break;
	case CS_EVENT_CARD_READY:
		event_text = "Card Ready";
		break;
	case CS_EVENT_BATTERY_LOW:
		event_text = "Battery Low";
		break;
	case CS_EVENT_BATTERY_DEAD:
		event_text = "Battery Dead";
		break;
	case CS_EVENT_CARD_LOCK:
		event_text = "Card Lock";
		break;
	case CS_EVENT_PM_SUSPEND:
		event_text = "Power Management Suspend";
		break;
	case CS_EVENT_CARD_RESET:
		event_text = "Card Reset";
		break;
	case CS_EVENT_CARD_UNLOCK:
		event_text = "Card Unlock";
		break;
	case CS_EVENT_EJECTION_COMPLETE:
		event_text = "Ejection Complete";
		break;
	case CS_EVENT_EJECTION_REQUEST:
		event_text = "Ejection Request";
		break;
	case CS_EVENT_ERASE_COMPLETE:
		event_text = "Erase Complete";
		break;
	case CS_EVENT_EXCLUSIVE_COMPLETE:
		event_text = "Exclusive Complete";
		break;
	case CS_EVENT_EXCLUSIVE_REQUEST:
		event_text = "Exclusive Request";
		break;
	case CS_EVENT_INSERTION_COMPLETE:
		event_text = "Insertion Complete";
		break;
	case CS_EVENT_INSERTION_REQUEST:
		event_text = "Insertion Request";
		break;
	case CS_EVENT_RESET_COMPLETE:
		event_text = "Reset Complete";
		break;
	case CS_EVENT_RESET_PHYSICAL:
		event_text = "Reset Physical";
		break;
	case CS_EVENT_RESET_REQUEST:
		event_text = "Reset Request";
		break;
	case CS_EVENT_MTD_REQUEST:
		event_text = "MTD Request";
		break;
	case CS_EVENT_CLIENT_INFO:
		event_text = "Client Info";
		break;
	case CS_EVENT_TIMER_EXPIRED:
		event_text = "Timer Expired";
		break;
	case CS_EVENT_WRITE_PROTECT:
		event_text = "Write Protect";
		break;
	case CS_EVENT_SS_UPDATED:
		event_text = "SS Updated";
		break;
	case CS_EVENT_STATUS_CHANGE:
		event_text = "Status Change";
		break;
	case CS_EVENT_CARD_REMOVAL:
		event_text = "Card Removal";
		break;
	case CS_EVENT_CARD_REMOVAL_LOWP:
		event_text = "Card Removal Low Power";
		break;
	default:
		event_text = buf;
		(void) sprintf(buf, "Unknown Event (0x%x)", event);
		break;
	}

	cmn_err(CE_CONT,
	    "pcser%d [socket %d]: %s (%s priority)\n",
	    ddi_get_instance(pcser->dip), (int)pcser->sn,
	    event_text, event_priority);
}
#endif
