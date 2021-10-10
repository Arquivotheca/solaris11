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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Sun NDI hotplug interfaces
 */

#include <sys/note.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/avintr.h>
#include <sys/autoconf.h>
#include <sys/sunndi.h>
#include <sys/ndi_impldefs.h>
#include <sys/ddi.h>
#include <sys/disp.h>
#include <sys/stat.h>
#include <sys/callb.h>
#include <sys/sysevent.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/sysevent/dr.h>
#include <sys/taskq.h>

/* Local functions prototype */
static void ddihp_cn_run_event(void *arg);
static int ddihp_cn_req_handler(ddi_hp_cn_handle_t *hdlp,
    ddi_hp_cn_change_state_arg_t *kargp);

/*
 * Global functions (called by hotplug controller or nexus drivers)
 */

/*
 * Register the Hotplug Connection (CN)
 */
int
ndi_hp_register(dev_info_t *dip, ddi_hp_cn_info_t *info_p)
{
	ddi_hp_cn_handle_t	*hdlp;
	int			count;

	DDI_HP_NEXDBG((CE_CONT, "ndi_hp_register: dip %p, info_p %p\n",
	    (void *)dip, (void *)info_p));

	ASSERT(!servicing_interrupt());
	if (servicing_interrupt())
		return (NDI_FAILURE);

	/* Validate the arguments */
	if ((dip == NULL) || (info_p == NULL))
		return (NDI_EINVAL);

	if (!NEXUS_HAS_HP_OP(dip)) {
		return (NDI_ENOTSUP);
	}
	/* Lock before access */
	ndi_devi_enter(dip, &count);

	hdlp = ddihp_cn_name_to_handle(dip, info_p->cn_name);
	if (hdlp) {
		/* This cn_name is already registered. */
		ndi_devi_exit(dip, count);

		return (NDI_SUCCESS);
	}
	/*
	 * Create and initialize hotplug Connection handle
	 */
	hdlp = (ddi_hp_cn_handle_t *)kmem_zalloc(
	    (sizeof (ddi_hp_cn_handle_t)), KM_SLEEP);

	/* Initialize cn_last_change */
	info_p->cn_last_change = (time32_t)ddi_get_time();

	/* Copy the Connection information */
	hdlp->cn_dip = dip;
	bcopy(info_p, &(hdlp->cn_info), sizeof (*info_p));

	/* Copy cn_name */
	hdlp->cn_info.cn_name = ddi_strdup(info_p->cn_name, KM_SLEEP);

	if (ddihp_cn_getstate(hdlp) != DDI_SUCCESS) {
		DDI_HP_NEXDBG((CE_CONT, "ndi_hp_register: dip %p, hdlp %p"
		    "ddi_cn_getstate failed\n", (void *)dip, (void *)hdlp));

		goto fail;
	}

	/*
	 * Append the handle to the list
	 */
	DDIHP_LIST_APPEND(ddi_hp_cn_handle_t, (DEVI(dip)->devi_hp_hdlp),
	    hdlp);

	ndi_devi_exit(dip, count);

	return (NDI_SUCCESS);

fail:
	kmem_free(hdlp->cn_info.cn_name, strlen(hdlp->cn_info.cn_name) + 1);
	kmem_free(hdlp, sizeof (ddi_hp_cn_handle_t));
	ndi_devi_exit(dip, count);

	return (NDI_FAILURE);
}

/*
 * Unregister a Hotplug Connection (CN)
 */
int
ndi_hp_unregister(dev_info_t *dip, char *cn_name)
{
	ddi_hp_cn_handle_t	*hdlp;
	int			count;
	int			ret;

	DDI_HP_NEXDBG((CE_CONT, "ndi_hp_unregister: dip %p, cn name %s\n",
	    (void *)dip, cn_name));

	ASSERT(!servicing_interrupt());
	if (servicing_interrupt())
		return (NDI_FAILURE);

	/* Validate the arguments */
	if ((dip == NULL) || (cn_name == NULL))
		return (NDI_EINVAL);

	ndi_devi_enter(dip, &count);

	hdlp = ddihp_cn_name_to_handle(dip, cn_name);
	if (hdlp == NULL) {
		ndi_devi_exit(dip, count);
		return (NDI_EINVAL);
	}

	switch (ddihp_cn_unregister(hdlp)) {
	case DDI_SUCCESS:
		ret = NDI_SUCCESS;
		break;
	case DDI_EINVAL:
		ret = NDI_EINVAL;
		break;
	case DDI_EBUSY:
		ret = NDI_BUSY;
		break;
	default:
		ret = NDI_FAILURE;
		break;
	}

	ndi_devi_exit(dip, count);

	return (ret);
}

/*
 * Notify the Hotplug Connection (CN) to change state.
 * Flag:
 *	DDI_HP_REQ_SYNC	    Return after the change is finished.
 *	DDI_HP_REQ_ASYNC    Return after the request is dispatched.
 */
int
ndi_hp_state_change_req(dev_info_t *dip, char *cn_name,
    ddi_hp_cn_state_t *state, uint_t flag)
{
	ddi_hp_cn_async_event_entry_t	*eventp;

	DDI_HP_NEXDBG((CE_CONT, "ndi_hp_state_change_req: dip %p "
	    "cn_name: %s, state %x, flag %x\n",
	    (void *)dip, cn_name, state->state_code, flag));

	/* Validate the arguments */
	if (dip == NULL || cn_name == NULL || state == NULL)
		return (NDI_EINVAL);

	if (!NEXUS_HAS_HP_OP(dip)) {
		return (NDI_ENOTSUP);
	}
	/*
	 * If the request is to handle the event synchronously, then call
	 * the event handler without queuing the event.
	 */
	if (flag & DDI_HP_REQ_SYNC) {
		ddi_hp_cn_handle_t	*hdlp;
		ddi_hp_cn_change_state_arg_t	karg;
		int			count;
		int			ret;

		ASSERT(!servicing_interrupt());
		if (servicing_interrupt())
			return (NDI_FAILURE);

		ndi_devi_enter(dip, &count);

		hdlp = ddihp_cn_name_to_handle(dip, cn_name);
		if (hdlp == NULL) {
			ndi_devi_exit(dip, count);

			return (NDI_EINVAL);
		}

		DDI_HP_NEXDBG((CE_CONT, "ndi_hp_state_change_req: hdlp %p "
		    "calling ddihp_cn_req_handler() directly to handle "
		    "target_state %x\n", (void *)hdlp, state->state_code));

		/*
		 * We are not bus nexus drivers, we don't understand the state
		 * priv, so we can't copy it in, which means the bus nexus
		 * driver will access the state_priv memory provided by the
		 * caller.
		 *
		 * Since here we are synchronous call, the memory is valid
		 * when the bus nexus driver access it.
		 */
		karg.target_state = *state;
		karg.type = ARG_PRIV_KERNEL_SYNC;

		ret = ddihp_cn_req_handler(hdlp, &karg);

		ndi_devi_exit(dip, count);

		return (ret);
	}

	eventp = kmem_zalloc(sizeof (ddi_hp_cn_async_event_entry_t),
	    KM_NOSLEEP);
	if (eventp == NULL)
		return (NDI_NOMEM);

	eventp->cn_name = ddi_strdup(cn_name, KM_NOSLEEP);
	if (eventp->cn_name == NULL) {
		kmem_free(eventp, sizeof (ddi_hp_cn_async_event_entry_t));
		return (NDI_NOMEM);
	}
	eventp->dip = dip;
	/*
	 * We are not bus nexus drivers, we don't understand the state priv,
	 * so we can't copy it in, which means the bus nexus driver will
	 * access the state_priv memory provided by the caller.
	 *
	 * Here we are asynchronous call, which means when the bus nexus driver
	 * accesses this piece of memory, the caller may not be alive(interrupt
	 * handler may finished). So it requires that the memory used for
	 * state_priv must be kmem_alloc()-ed memory, and must be kmem_free()-ed
	 * by the bus nexus driver.
	 */
	eventp->karg.target_state = *state;
	eventp->karg.type = ARG_PRIV_KERNEL_ASYNC;

	/*
	 * Hold the parent's ref so that it won't disappear when the taskq is
	 * scheduled to run.
	 */
	ndi_hold_devi(dip);

	if (!taskq_dispatch(system_taskq, ddihp_cn_run_event, eventp,
	    TQ_NOSLEEP)) {
		ndi_rele_devi(dip);
		DDI_HP_NEXDBG((CE_CONT, "ndi_hp_state_change_req: "
		    "taskq_dispatch failed! dip %p "
		    "target_state %x\n", (void *)dip, state->state_code));
		return (NDI_NOMEM);
	}

	return (NDI_CLAIMED);
}

/*
 * Walk the link of Hotplug Connection handles of a dip:
 *	DEVI(dip)->devi_hp_hdlp->[link of connections]
 */
void
ndi_hp_walk_cn(dev_info_t *dip, int (*f)(ddi_hp_cn_info_t *,
    void *), void *arg)
{
	int			count;
	ddi_hp_cn_handle_t	*head, *curr, *prev;

	DDI_HP_NEXDBG((CE_CONT, "ndi_hp_walk_cn: dip %p arg %p\n",
	    (void *)dip, arg));

	ASSERT(!servicing_interrupt());
	if (servicing_interrupt())
		return;

	/* Validate the arguments */
	if (dip == NULL)
		return;

	ndi_devi_enter(dip, &count);

	head = DEVI(dip)->devi_hp_hdlp;
	curr = head;
	prev = NULL;
	while (curr != NULL) {
		DDI_HP_NEXDBG((CE_CONT, "ndi_hp_walk_cn: dip %p "
		    "current cn_name: %s\n",
		    (void *)dip, curr->cn_info.cn_name));
		switch ((*f)(&(curr->cn_info), arg)) {
		case DDI_WALK_TERMINATE:
			ndi_devi_exit(dip, count);

			return;
		case DDI_WALK_CONTINUE:
		default:
			if (DEVI(dip)->devi_hp_hdlp != head) {
				/*
				 * The current node is head and it is removed
				 * by last call to (*f)()
				 */
				head = DEVI(dip)->devi_hp_hdlp;
				curr = head;
				prev = NULL;
			} else if (prev && prev->next != curr) {
				/*
				 * The current node is a middle node or tail
				 * node and it is removed by last call to
				 * (*f)()
				 */
				curr = prev->next;
			} else {
				/* no removal accurred on curr node */
				prev = curr;
				curr = curr->next;
			}
		}
	}
	ndi_devi_exit(dip, count);
}

/*
 * Local functions (called within this file)
 */

/*
 * Wrapper function for ddihp_cn_req_handler() called from taskq
 */
static void
ddihp_cn_run_event(void *arg)
{
	ddi_hp_cn_async_event_entry_t	*eventp =
	    (ddi_hp_cn_async_event_entry_t *)arg;
	dev_info_t			*dip = eventp->dip;
	ddi_hp_cn_handle_t		*hdlp;
	int				count;

	/* Lock before access */
	ndi_devi_enter(dip, &count);

	hdlp = ddihp_cn_name_to_handle(dip, eventp->cn_name);
	if (hdlp) {
		(void) ddihp_cn_req_handler(hdlp, &eventp->karg);
	} else {
		DDI_HP_NEXDBG((CE_CONT, "ddihp_cn_run_event: no handle for "
		    "cn_name: %s dip %p. Request for target_state %x is"
		    " dropped. \n",
		    eventp->cn_name, (void *)dip,
		    eventp->karg.target_state.state_code));
	}

	ndi_devi_exit(dip, count);

	/* Release the devi's ref that is held from interrupt context. */
	ndi_rele_devi((dev_info_t *)DEVI(dip));
	kmem_free(eventp->cn_name, strlen(eventp->cn_name) + 1);
	kmem_free(eventp, sizeof (ddi_hp_cn_async_event_entry_t));
}

/*
 * Handle state change request of a Hotplug Connection (CN)
 */
static int
ddihp_cn_req_handler(ddi_hp_cn_handle_t *hdlp,
    ddi_hp_cn_change_state_arg_t *kargp)
{
	dev_info_t	*dip = hdlp->cn_dip;
	ddi_hp_cn_state_code_t target_state;
	int		ret = DDI_SUCCESS;

	target_state = kargp->target_state.state_code;
	DDI_HP_NEXDBG((CE_CONT, "ddihp_cn_req_handler:"
	    " hdlp %p, target_state %x\n",
	    (void *)hdlp, target_state));

	ASSERT(DEVI_BUSY_OWNED(dip));

	if (ddihp_cn_getstate(hdlp) != DDI_SUCCESS) {
		DDI_HP_NEXDBG((CE_CONT, "ddihp_cn_req_handler: dip %p, "
		    "hdlp %p ddi_cn_getstate failed\n", (void *)dip,
		    (void *)hdlp));

		return (NDI_UNCLAIMED);
	}
	if (hdlp->cn_info.cn_state != target_state) {
		ddi_hp_cn_state_code_t result_state = 0;

		DDIHP_CN_OPS(hdlp, DDI_HPOP_CN_CHANGE_STATE,
		    (void *)kargp, (void *)&result_state, ret);

		DDI_HP_NEXDBG((CE_CONT, "ddihp_cn_req_handler: dip %p, "
		    "hdlp %p changed state to %x, ret=%x\n",
		    (void *)dip, (void *)hdlp, result_state, ret));
	}

	if (ret == DDI_SUCCESS)
		return (NDI_CLAIMED);
	else
		return (NDI_UNCLAIMED);
}
