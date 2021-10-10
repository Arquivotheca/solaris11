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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Routines to communicate with the userland helper responsible of issuing
 * BIOS calls.
 */
#include <sys/door.h>
#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <sys/vbios.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/queue.h>
#include <sys/ddi_impldefs.h>

typedef struct vbios_exec_cmd {
	int				ret;
	door_arg_t			d_args;
} vbios_exec_cmd_t;

typedef struct vbios_vreq {
	SIMPLEQ_ENTRY(vbios_vreq)	cmdq_entry;
	kmutex_t			cmdreq_mu;
	kcondvar_t			cmdreq_cv;
	boolean_t			cmd_done;
	boolean_t			cmd_discard;
	boolean_t			cmd_processing;
	vbios_exec_cmd_t		cmd;
	vbios_cmd_req_t			cmdreq_dc;
} vbios_vreq_t;

SIMPLEQ_HEAD(vbios_cmd_head, vbios_vreq);
typedef struct vbios_cmd_head vbios_cmd_head_t;

struct vbios_handle {
	/* Door handle for the upcalls towards the userland deamon. */
	kmutex_t		hndl_mu;
	door_handle_t		dh;
	/* Synchronization primitives protecting the command queue. */
	vbios_cmd_head_t	cmdq_head;
	kmutex_t		cmdq_mu;
	kcondvar_t		cmdq_cv;
	uint32_t		cmdq_size;
};

static struct vbios_handle	vbh;

static int			vbios_inited;

static void			vbios_consumer_thread();

static vbios_vreq_t *
vbios_alloc_request()
{
	return (kmem_zalloc(sizeof (vbios_vreq_t), KM_SLEEP));
}

static void
vbios_free_request(vbios_vreq_t *req)
{
	kmem_free(req, sizeof (*req));
}

/* Executed only at the first call. */
static void
vbios_init()
{
	extern pri_t 	minclsyspri;
	/* Mutex to protect the handle registration. */
	mutex_init(&vbh.hndl_mu, NULL, MUTEX_DEFAULT, NULL);


	SIMPLEQ_INIT(&vbh.cmdq_head);
	mutex_init(&vbh.cmdq_mu, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&vbh.cmdq_cv, NULL, CV_DEFAULT, NULL);
	vbh.cmdq_size = 0;

	/* Start the helper thread. */
	(void) thread_create(NULL, 0, vbios_consumer_thread, NULL,
	    0, &p0, TS_RUN, minclsyspri);
	vbios_inited++;
}

int
vbios_register_handle(fbio_load_handle_t *fbh)
{
	if (vbios_inited == 0)
		vbios_init();

	mutex_enter(&vbh.hndl_mu);
	if (vbh.dh != NULL)
		door_ki_rele(vbh.dh);

	vbh.dh = door_ki_lookup(fbh->id);
	mutex_exit(&vbh.hndl_mu);

	if (vbh.dh == NULL) {
		cmn_err(CE_CONT, "!vbios: unable to lookup userland door id");
		return (VBIOS_FAIL);
	}
	return (VBIOS_SUCCESS);
}

/*
 * vbios_do_cmd - exec the requested VBIOS command by issuing a door call to
 * the userland daemon. The door call will block until the daemon returns or
 * gets interrupted by out timeout.
 */
static void
vbios_do_cmd(vbios_exec_cmd_t *exec_cmd)
{
	door_handle_t	dh;

	/* The door subsystem will alloc the exact size for us. */
	exec_cmd->d_args.rbuf = NULL;
	exec_cmd->d_args.rsize = SIZE_MAX;

	mutex_enter(&vbh.hndl_mu);
	dh = vbh.dh;
	mutex_exit(&vbh.hndl_mu);
	exec_cmd->ret = door_ki_upcall_limited(dh, &exec_cmd->d_args,
	    kcred, SIZE_MAX, 0);

	if (exec_cmd->ret != 0) {
		cmn_err(CE_CONT, "!vbios: error executing door call (-%d)",
		    exec_cmd->ret);
	}
}

/*
 * vbios_consumer_thread.
 *
 * This thread fetches the commands placed inside the queue and sends them
 * down to the user land helper one after the other. If a command has waited
 * too long in the queue (or was stuck in user land for any reason), it is
 * not dispatched in the first case and its results discarded in the second.
 * Whenever we discard the results of a command, freeing the request is up
 * on us.
 */
static void
vbios_consumer_thread()
{
	for (;;) {
		vbios_vreq_t		*req;
		boolean_t		discard_req;

		mutex_enter(&vbh.cmdq_mu);
		while (vbh.cmdq_size == 0)
			cv_wait(&vbh.cmdq_cv, &vbh.cmdq_mu);

		req = SIMPLEQ_FIRST(&vbh.cmdq_head);
		SIMPLEQ_REMOVE_HEAD(&vbh.cmdq_head, cmdq_entry);
		vbh.cmdq_size--;
		mutex_exit(&vbh.cmdq_mu);

		mutex_enter(&req->cmdreq_mu);
		discard_req = req->cmd_discard;
		mutex_exit(&req->cmdreq_mu);

		/*
		 * The request waited in the queue too long and the
		 * caller is not interested in its result: ignore it.
		 */
		if (discard_req == B_TRUE) {
			vbios_free_request(req);
			continue;
		}

		/* Record that the request is being processed. */
		mutex_enter(&req->cmdreq_mu);
		req->cmd_processing = B_TRUE;
		mutex_exit(&req->cmdreq_mu);

		/* Execute the request. */
		vbios_do_cmd(&req->cmd);

		/* Processing done. */
		mutex_enter(&req->cmdreq_mu);
		req->cmd_processing = B_FALSE;
		/* Is the caller still interested in this request ? */
		if (req->cmd_discard == B_TRUE) {
			mutex_exit(&req->cmdreq_mu);
			if (req->cmd.d_args.rbuf != 0) {
				/*
				 * This really happens only if the door server
				 * is misbehaving and ignores our SIGUSR2.
				 */
				kmem_free(req->cmd.d_args.rbuf,
				    req->cmd.d_args.rsize);
			}
			vbios_free_request(req);
			continue;
		}

		/* Signal that we are done processing the request. */
		req->cmd_done = B_TRUE;
		cv_signal(&req->cmdreq_cv);
		mutex_exit(&req->cmdreq_mu);
	}
}

void
vbios_free_reply(vbios_cmd_reply_t *reply)
{
	if (reply != NULL) {
		if (reply->call_results != NULL) {
			kmem_free(reply->call_results, reply->results_size);
		}
		kmem_free(reply, sizeof (*reply));
	}
}

/*
 * vbios_exec_cmd.  Returns NULL if the requested command is invalid and a
 * pointer to an allocated reply structure containing the results of the call
 * otherwise. Freeing the reply struct is up to the caller, which should call
 * vbios_free_reply() passing it as an argument.
 *
 * Callers of our interface use this function to pass us a VBIOS command to
 * execute. The command is inserted in the worker thread queue and the function
 * blocks waiting for its completion. To avoid waiting indefinitely, a
 * timeout is set on the conditional variable (cv_timedwait).
 * This functions allocates and fills a vbios_cmd_reply_t according to the
 * result of the door call (successful, failed, timeout expired).
 */
vbios_cmd_reply_t *
vbios_exec_cmd(vbios_cmd_req_t *cmd)
{
	clock_t				cur_ticks, to_wait;
	vbios_vreq_t			*vreq;
	vbios_cmd_reply_t		*reply;
	boolean_t			discard_vreq = B_FALSE;

	if (vbios_inited == 0) {
		cmn_err(CE_CONT, "!vbios: no daemon registered yet!");
		return (NULL);
	}

	if (cmd->type > VBIOS_TYPE_MAX) {
		cmn_err(CE_CONT, "!vbios: unknown call type %d", cmd->type);
		return (NULL);
	}

	if (cmd->cmd > VBIOS_CMD_MAX) {
		cmn_err(CE_CONT, "!vbios: unsupported command %d", cmd->cmd);
		return (NULL);
	}

	/* Allocate space for the reply. */
	reply = kmem_zalloc(sizeof (*reply), KM_SLEEP);
	/* Allocate space for the consumer thread request. */
	vreq = vbios_alloc_request();
	/* Make a defensive copy of the request. */
	vreq->cmdreq_dc = *cmd;
	/* Prepare door upcall parameters. */
	vreq->cmd.d_args.data_ptr = (void *)&vreq->cmdreq_dc;
	vreq->cmd.d_args.data_size = sizeof (vbios_cmd_req_t);
	vreq->cmd.d_args.desc_ptr = NULL;
	vreq->cmd.d_args.desc_num = 0;

	/* Prevent a spurious success value. */
	vreq->cmd.ret = -1;

	mutex_init(&vreq->cmdreq_mu, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&vreq->cmdreq_cv, NULL, CV_DEFAULT, NULL);
	vreq->cmd_done = B_FALSE;
	vreq->cmd_discard = B_FALSE;
	vreq->cmd_processing = B_FALSE;

	/* Place the command in the consumer queue. */
	mutex_enter(&vbh.cmdq_mu);
	SIMPLEQ_INSERT_TAIL(&vbh.cmdq_head, vreq, cmdq_entry);
	vbh.cmdq_size++;
	cv_broadcast(&vbh.cmdq_cv);
	mutex_exit(&vbh.cmdq_mu);

	/*
	 * Wait for results.
	 * We set a timeout to not wait indefinitely on a stopped userland
	 * process. If the timeout expires, we mark the command as discarded
	 * so that our consumer thread knows how to behave with it. In such a
	 * case, freeing the vreq is up to the consumer thread.
	 * If, instead, the results (either successful or unsuccessful) are
	 * available before the timer expires, freeing the vreq is up on us.
	 */
	mutex_enter(&vreq->cmdreq_mu);
	/* Set the timeout value. */
	cur_ticks = ddi_get_lbolt();
	to_wait = cur_ticks + drv_usectohz(VBIOS_TIMEOUT * 1000000);
	while (vreq->cmd_done == B_FALSE) {
		to_wait = cv_timedwait(&vreq->cmdreq_cv, &vreq->cmdreq_mu,
		    to_wait);

		/* Kernel timeout expired, discard the request. */
		if (to_wait == -1) {
			discard_vreq = B_TRUE;
			vreq->cmd_discard = B_TRUE;
			break;
		}
	}
	mutex_exit(&vreq->cmdreq_mu);

	/*
	 * At this point we are in one of two possible situations:
	 *
	 * discard_vreq == B_TRUE: the kernel timeout expired before the call
	 *	completed (f.e. the daemon is stopped). Note that the timer
	 *	takes into account the time spent inside the queue, too.
	 *
	 * discard_vreq == B_FALSE: the door call completed. That does not
	 * 	imply that the bios call was successful and we set
	 *	call_ret accordingly.
	 */
	if (discard_vreq == B_TRUE) {
		/*
		 * Our request never completed. We do a best effort attempt to
		 * terminate it with a SIGUSR2, but only if it had actually been
		 * dispatched to user land.
		 */
		door_info_t	dinfo;
		door_handle_t	dh;

		mutex_enter(&vbh.hndl_mu);
		dh = vbh.dh;
		mutex_exit(&vbh.hndl_mu);

		if (door_ki_info(dh, &dinfo) == 0) {
			boolean_t	is_running;
			/*
			 * Send SIGUSR2 to the userland process to inform it
			 * to exit from execution. Send the signal only if the
			 * servicing door thread is 'running'.
			 */
			mutex_enter(&vreq->cmdreq_mu);
			is_running = vreq->cmd_processing;
			mutex_exit(&vreq->cmdreq_mu);

			/*
			 * The aim of this check is to avoid sending signals to
			 * the process if the request has been discarded before
			 * being dispatched to the user land daemon (see the
			 * first check for cmd_discard in the worker thread).
			 * For this reason we accept the small race condition
			 * between evaluating cmd_processing and doing the
			 * kill().
			 */
			if (is_running)
				(void) kill(dinfo.di_target, SIGUSR2);
		}
		reply->call_ret = VBIOS_STUCK;
		reply->call_results = NULL;
		reply->results_size = 0;
	} else {
		/* Our request completed, was it successful? */
		if (vreq->cmd.ret == VBIOS_SUCCESS) {
			/*
			 * Send back the door results. 'rbuf' has been
			 * kmem allocated for us by the door subsystem.
			 */
			reply->call_ret = VBIOS_SUCCESS;
			reply->call_results = (void *)vreq->cmd.d_args.rbuf;
			reply->results_size = vreq->cmd.d_args.rsize;
		} else {
			/*
			 * Door call failed.
			 * We return the reason of the failure as returned by
			 * the door subsystem (door_ki_upcall_limited). The
			 * call_errtype value will be the positive value of the
			 * associated errno reason (ex. 4 -> EINTR).
			 */
			reply->call_ret = VBIOS_FAIL;
			reply->call_errtype = vreq->cmd.ret;
			reply->call_results = NULL;
			reply->results_size = 0;
		}
		/* Cleaning up the vreq is up on us. */
		vbios_free_request(vreq);
	}

	return (reply);
}
