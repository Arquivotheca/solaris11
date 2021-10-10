/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */

/*
 * sbmc_fe.h
 *
 */

#ifndef _BMC_FE_H
#define	_BMC_FE_H

#ifdef __cplusplus
extern "C" {
#endif

#define	BMC_NODENAME		"bmc"
#define	BMC_MINOR		0

#define	BMC_DEBUG_LEVEL_0	0
#define	BMC_DEBUG_LEVEL_1	1
#define	BMC_DEBUG_LEVEL_2	2
#define	BMC_DEBUG_LEVEL_3	3
#define	BMC_DEBUG_LEVEL_4	4

void sbmc_printf(int, const char *, ...);

typedef struct  ipmi_dev {
	uint8_t		bmcversion;	/* IPMI Version */
	uint32_t	bmcaddress;	/* See IPMI Spec */
	uint8_t		bmcregspacing;	/* See IPMI Spec */
	kmutex_t	if_mutex;	/* Interface lock */
	kcondvar_t	if_cv;		/* Interface condition variable */
	boolean_t	if_busy;	/* Busy Bit */
	kmutex_t	timer_mutex;	/* timer lock */
	kcondvar_t	timer_cv;	/* timer cv */
	timeout_id_t	timer_handle;	/* timer handle */
	boolean_t	timedout;	/* timeout flag */
} ipmi_dev_t;

typedef struct bmc_kstat {
	kstat_named_t	bmc_alloc_failures;
	kstat_named_t	bmc_bytes_in;
	kstat_named_t	bmc_bytes_out;
} bmc_kstat_t;


typedef struct ipmi_state {
	dev_info_t	*ipmi_dip;	/* driver's device pointer */
	ipmi_dev_t	ipmi_dev_ext;	/* controlling the BMC interface */
	ddi_taskq_t	*task_q;
	kmutex_t	task_mutex;
	kcondvar_t	task_cv;
	list_t		task_list;
	boolean_t	task_abort;
	kstat_t		*ksp;		/* kstats for this instance */
	bmc_kstat_t	bmc_kstats;
} ipmi_state_t;

typedef struct bmc_clone {
	ipmi_state_t	*ipmip;		/* IPMI state */
	dev_t		dev;		/* maj/min for this clone */
	kmutex_t	clone_mutex;	/* Per clone close state protection */
	uint32_t	clone_req_cnt;	/* Count of pending request per clone */
	boolean_t	clone_closed;	/* Closed waiting to drain pend reqst */
} bmc_clone_t;

#define	BMC_CLONE(x)	((bmc_clone_t *)(x))

typedef struct ipmi_task {
	list_node_t	task_linkage;
	bmc_clone_t	*clone;
	int		cmd;
	queue_t		*q;
	mblk_t		*reqmp;
	mblk_t		*respmp;
	bmc_req_t	*request;
	bmc_rsp_t	*response;
} ipmi_task_t;

/* bmc task operations */
#define	BMC_TASK_EXIT	1
#define	BMC_TASK_IPMI	2

/* transfer time limit */
#define	DEFAULT_MSG_TIMEOUT (5 * 1000000) /* 5 seconds */

#define	BMC_MAX_RESPONSE_PAYLOAD_SIZE   sbmc_vc_max_response_payload_size

int sbmc_vc_max_response_payload_size(void);
int sbmc_vc_max_request_payload_size(void);

int do_bmc2ipmi(ipmi_state_t *, bmc_req_t *, bmc_rsp_t *, boolean_t, uint8_t *);
int sbmc_init(dev_info_t *);
void sbmc_uninit(void);

#ifdef __cplusplus
}
#endif

#endif /* _BMC_FE_H */
