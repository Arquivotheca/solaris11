/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright(c) 2009 Digi International, Inc., Inside Out
 * Networks, Inc.  All rights reserved.
 */


#ifndef _SYS_USB_USBSER_EDGE_PIPE_H
#define	_SYS_USB_USBSER_EDGE_PIPE_H

/*
 * Edgeport USB pipe management (mostly device-neutral)
 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * pipe request
 *
 * Typically there are a some actions to be performed after the data
 * has been transferred over USB, based on the completion status, but
 * we don't want these actions to prevent pending data from being sent.
 * So we save completion reason of each request in struct edge_pipe_req,
 * kick off next transfer and let the interested parties examine
 * the structure for as long as they need.
 */
typedef struct edge_pipe_req {
	int			req_state;	/* request state */
	uint_t			req_cr;		/* completion reason */
	int			req_waiter;	/* someone waits for compl. */
} edge_pipe_req_t;

_NOTE(SCHEME_PROTECTS_DATA("unique per call", edge_pipe_req))

/*
 * pipe request states:
 *
 * NON-EXISTENT ---edge_pipe_req_new---> EDGE_REQ_RUNNING
 *      ^                                       |
 *      |                                       |
 * edge_pipe_req_delete               edge_pipe_req_complete
 *      |                                       |
 *      +-----<----- EDGE_REQ_COMPLETE <--------+
 */
enum {
	EDGE_REQ_RUNNING,
	EDGE_REQ_COMPLETE
};

/*
 * pipe structure
 */
typedef struct edge_pipe {
	kmutex_t		pipe_mutex;	/* structure lock */
	edge_state_t		*pipe_esp;	/* backpointer to state */
	edgeti_boot_t		*pipe_etp;	/* set if in TI boot mode */
	usb_pipe_handle_t	pipe_handle;	/* pipe handle */
	usb_ep_descr_t		pipe_ep_descr;	/* endpoint descriptor */
	usb_pipe_policy_t	pipe_policy;	/* pipe policy */
	int			pipe_state;	/* pipe state */
	kcondvar_t		pipe_cv;	/* cv for pipe acquisition */
	uint_t			pipe_cr;	/* last completion reason */
	kcondvar_t		pipe_req_cv;	/* cv for request completion */
	edge_pipe_req_t		*pipe_req;	/* current request */
	usb_log_handle_t	pipe_lh;	/* log handle */
} edge_pipe_t;

_NOTE(MUTEX_PROTECTS_DATA(edge_pipe::pipe_mutex, edge_pipe))
_NOTE(DATA_READABLE_WITHOUT_LOCK(edge_pipe::{
	pipe_esp
	pipe_etp
	pipe_handle
	pipe_lh
	pipe_ep_descr
	pipe_policy
}))

/*
 * pipe state:
 *
 *        EDGE_PIPE_NOT_INIT
 *              |    ^
 *              |    |
 * edge_init_pipes  edge_fini_pipes
 *              |    |
 *              v    |
 *         EDGE_PIPE_CLOSED <---edge_close_pipes---------+
 *                ^         ----edge_open_pipes-----+    |
 *                |                                 |    |
 *            disconnect                            |    |
 *                |                                 v    |
 *                +---------<------------------ EDGE_PIPE_IDLE
 *                |                                 |    ^
 *                |                                 |    |
 *                ^                  edge_pipe_acquire  edge_pipe_release
 *                |            edge_pipe_start_polling  edge_pipe_req_delete
 *                |                                 |    |
 *                |                                 v    |
 *                +---------<----------------- EDGE_PIPE_ACTIVE
 *
 */
enum {
	EDGE_PIPE_NOT_INIT = 0,
	EDGE_PIPE_CLOSED,
	EDGE_PIPE_IDLE,
	EDGE_PIPE_ACTIVE
};


int	edge_init_pipes(edge_state_t *);
void	edge_fini_pipes(edge_state_t *);
int	edgesp_open_pipes(edge_state_t *);
void	edgesp_close_pipes(edge_state_t *);
int	edgeti_open_dev_pipes(edge_state_t *);
void	edgeti_close_dev_pipes(edge_state_t *);
int	edgeti_open_port_pipes(edge_port_t *);
void	edgeti_close_port_pipes(edge_port_t *);
int	edge_open_pipes(edge_state_t *);
void	edge_close_pipes(edge_state_t *);
void	edgeti_close_open_pipes(edge_state_t *esp);
int	edgeti_reopen_pipes(edge_state_t *esp);

int	edgeti_boot_open_pipes(edgeti_boot_t *);
void	edgeti_boot_close_pipes(edgeti_boot_t *);

int	edge_pipe_acquire(edge_pipe_t *, edge_fblock_t);
void	edge_pipe_release(edge_pipe_t *);

edge_pipe_req_t	*edge_pipe_req_new(edge_pipe_t *, int, int);
void	edge_pipe_req_delete(edge_pipe_t *, edge_pipe_req_t *);
void	edge_pipe_req_complete(edge_pipe_t *, uint_t);
void	edge_pipe_req_wait_completion(edge_pipe_t *, edge_pipe_req_t *);

void	edge_pipe_start_polling(edge_pipe_t *);
void	edge_pipe_stop_polling(edge_pipe_t *);
int	edge_receive_data(edge_pipe_t *, int, void *, edge_fblock_t);
int	edge_send_data(edge_pipe_t *, mblk_t **, edge_pipe_req_t **, void *,
		edge_fblock_t);
int	edge_send_data_sync(edge_pipe_t *, mblk_t **, void *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_USB_USBSER_EDGE_PIPE_H */
