/*
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _IPMI_H
#define	_IPMI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/ioccom.h>
#include <sys/mutex.h>
#include <sys/list.h>
#include <sys/note.h>
#include <sys/stat.h>
#include <sys/kstat.h>
#include <sys/devops.h>
#include <sys/dditypes.h>
#include <sys/modctl.h>
#include <sys/varargs.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/policy.h>
#include <sys/sysmacros.h>
#include <sys/smbios.h>
#include <sys/atomic.h>
#include <sys/debug.h>
#include <sys/isa_defs.h>
#include <sys/mkdev.h>

#include "ipmi_bsd_int.h"
#include "ipmi.h"
#include "ipmivars.h"

#define	IPMI_NODENAME		"ipmi"
#define	IPMI_MINOR		0
#define	MAX_STR_LEN		50


#define	BMC_DEBUG_LEVEL_0	0
#define	BMC_DEBUG_LEVEL_1	1
#define	BMC_DEBUG_LEVEL_2	2
#define	BMC_DEBUG_LEVEL_3	3
#define	BMC_DEBUG_LEVEL_4	4

#define	DEFAULT_MSG_TIMEOUT	(5 * 1000) /* 5 seconds */
#define	DEFAULT_MSG_RETRY	(7)
#define	MSEC2USEC(mstime)	((mstime) * 1000)

/* BMC Completion Code and OEM Completion Code */
#define	BMC_IPMI_UNSPECIFIC_ERROR	0xFF /* Unspecific Error */
#define	BMC_IPMI_INVALID_COMMAND	0xC1 /* Invalid Command */
#define	BMC_IPMI_COMMAND_TIMEOUT	0xC3 /* Command Timeout */
#define	BMC_IPMI_DATA_LENGTH_EXCEED	0xC8 /* DataLength exceeded limit */
#define	BMC_IPMI_OEM_FAILURE_SENDBMC	0x7E /* Cannot send BMC req */

int ipmi_ddi_get8(void *, int);
void ipmi_ddi_put8(void *, int, int);


typedef struct ipmi_kstat {
	kstat_named_t	ipmi_alloc_failures;
	kstat_named_t	ipmi_bytes_in;
	kstat_named_t	ipmi_bytes_out;
} ipmi_kstat_t;

struct ipmi_state {
	/*
	 * Overall instance state.
	 */
	dev_info_t		*is_dip;
	int			is_instance; /* Overall driver instance */
	int			is_pi_instance;	 /* Plugin suppled instance */
	boolean_t		is_suspended;
	boolean_t		is_task_abort;
	boolean_t		is_detaching;
	char			is_name[MAX_STR_LEN]; /* dev node name */
	kstat_t			*is_ksp;
	ipmi_kstat_t		is_kstats;

	/*
	 * List of open attached clones, each with its state.
	 */
	list_t			is_open_clones; /* Open instances */
	uint_t			is_nextclone;	 /* Next free minor # */
	krwlock_t		is_clone_lock;

	/*
	 * Event Polling task
	 */
	ddi_taskq_t		*is_poll_taskq;
	boolean_t		is_task_end;
	kcondvar_t		is_poll_cv;

	/*
	 * Interface instance state.
	 */
	struct ipmi_plugin	*is_pi;
	void			*is_dev_ext;

	/* interface instance name */
	char			is_pi_name[MAX_STR_LEN];
	ddi_taskq_t		*is_pi_taskq;

	/*
	 * BSD state - to help make BSD driver part of code happy.
	 */
	struct ipmi_softc	is_bsd_softc; /* BSD state */
	struct cdev		is_bsd_cdev; /* Proto for open clones */
};

typedef struct ipmi_state ipmi_state_t;

struct ipmi_plugin {
	/* These are supplied by the plugin and used by the main driver */
	int (*ipmi_pi_probe)(dev_info_t *, int, int);
	int (*ipmi_pi_attach)(void *);
	int (*ipmi_pi_detach)(void *);
	int (*ipmi_pi_suspend)(void *);
	int (*ipmi_pi_resume)(void *);
	int (*ipmi_pi_pollstatus)(void *);
	/* Plugin instance properties */
	char *ipmi_pi_name;
	int ipmi_pi_intfinst;	/* one based instance number */
	int ipmi_pi_flags;

	/* These are supplied by the main driver and used by the plugin */
	struct ipmi_request *(*ipmi_pi_getreq)(struct ipmi_softc *);
	void (*ipmi_pi_putresp)(struct ipmi_softc *, struct ipmi_request *req);
	int (*ipmi_pi_taskinit)(ipmi_state_t *, void (*func)(void *), char *);
	void (*ipmi_pi_taskexit)(ipmi_state_t *);
};

/* Plugin flags */
#define	IPMIPI_POLLED	0x00001	/* Supports polled mode */
#define	IPMIPI_INTER	0x00002	/* Supports interrupt mode */
#define	IPMIPI_REVENT	0x00004	/* Supports hw async events */
#define	IPMIPI_PEVENT	0x00008	/* Simulate async events */
#define	IPMIPI_SUSRES	0x01000	/* Supports suspend/resume */
#define	IPMIPI_DELYATT	0x02000	/* Delay IO until after attach */
#define	IPMIPI_NOASYNC	0x04000	/* Don't use any async ops */

/* Poll Status flags */
#define	IPMIFL_ATTN	0x00001	/* The interface needs attention */
#define	IPMIFL_BUSY	0x00002	/* The interface is busy */
#define	IPMIFL_ERR	0x00004	/* Fatal interface error */
/*
 * Watchdog timer setup
 */
#define	IPMI_WDTIME	90	/* Watchdog time in seconds */
#define	IPMI_WDUPDATE	(IPMI_WDTIME/3)
struct ipmi_wd_periodic {
	struct ipmi_wd_periodic *wd_next;
	ddi_periodic_t		wd_periodic;
	void			(*wd_callfunc)(void *, uint32_t, int *);
	void			*wd_arg;
	uint32_t		wd_timeo;
};

/*
 * Async event poll time (in milliseconds)
 */
#define	IPMI_POLLTIME	1000

/*
 * KCS driver plugin extended state
 */

typedef struct  ipmi_dev {
	char		if_name[MAX_STR_LEN];
	int		if_instance;
	uint32_t	kcs_base;
	uint32_t	kcs_cmd_reg;
	uint32_t	kcs_status_reg;
	uint32_t	kcs_in_reg;
	uint32_t	kcs_out_reg;
	kmutex_t	if_mutex; /* Interface lock */
	kcondvar_t	if_cv; /* Interface condition variable */
	boolean_t	if_busy; /* Busy Bit */
	boolean_t	if_up;
	timeout_id_t	timer_handle; /* timer handle */
	boolean_t	timedout; /* timeout flag */
	ipmi_kstat_t	*kstatsp;
} ipmi_dev_t;

typedef struct ipmi_vc_dev {
	char		if_name[MAX_STR_LEN];
	int		if_instance;
	ldi_handle_t	if_vc_lh;
	boolean_t	if_vc_opened;
	ldi_ident_t	if_vc_li;
	kmutex_t	if_mutex; /* Interface lock */
	kcondvar_t	if_cv; /* Interface condition variable */
	boolean_t	if_busy; /* Busy Bit */
	boolean_t	if_up;
	timeout_id_t	timer_handle; /* timer handle */
	boolean_t	timedout; /* timeout flag */
	ipmi_kstat_t	*kstatsp;
} ipmi_vc_dev_t;

int ipmi_pitask_init(ipmi_state_t *, void (*taskfunc)(void *), char *);
void ipmi_pitask_exit(ipmi_state_t *statep);

#ifdef __cplusplus
}
#endif

#endif /* _IPMI_H */
