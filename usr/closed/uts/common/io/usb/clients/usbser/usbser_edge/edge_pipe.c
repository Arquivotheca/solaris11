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
 * Edgeport pipe routines (mostly device-neutral)
 *
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/termio.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#include <sys/usb/usba.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_var.h>
#include <sys/usb/clients/usbser/usbser_edge/edge_pipe.h>

static int	edgesp_init_pipes(edge_state_t *esp);
static void	edgesp_fini_pipes(edge_state_t *esp);
static int	edgeti_init_pipes(edge_state_t *esp);
static void	edgeti_fini_pipes(edge_state_t *esp);


/*
 * initialize pipe structure with the given parameters
 */
static void
edge_init_one_pipe(edge_state_t *esp, edge_pipe_t *pipe)
{
	usb_pipe_policy_t	*policy;

	/* init sync primitives */
	mutex_init(&pipe->pipe_mutex, NULL, MUTEX_DRIVER, (void *)NULL);
	cv_init(&pipe->pipe_cv, NULL, CV_DRIVER, NULL);
	cv_init(&pipe->pipe_req_cv, NULL, CV_DRIVER, NULL);

	/* init pipe policy */
	policy = &pipe->pipe_policy;
	policy->pp_max_async_reqs = 2;

	pipe->pipe_esp = esp;
	pipe->pipe_lh = esp->es_lh;
	pipe->pipe_state = EDGE_PIPE_CLOSED;
}


static void
edge_fini_one_pipe(edge_pipe_t *pipe)
{
	if (pipe->pipe_state != EDGE_PIPE_NOT_INIT) {
		ASSERT(pipe->pipe_state != EDGE_PIPE_ACTIVE);
		mutex_destroy(&pipe->pipe_mutex);
		cv_destroy(&pipe->pipe_cv);
		cv_destroy(&pipe->pipe_req_cv);
		pipe->pipe_state = EDGE_PIPE_NOT_INIT;
	}
}


/*
 * Allocate resources, initialize pipe structures.
 * SP device should have three EPs: 0 - bulk in, 1 - bulk out, 2 - interrupt.
 */
static int
edgesp_init_pipes(edge_state_t *esp)
{
	usb_client_dev_data_t *dev_data = esp->es_dev_data;
	int		ifc, alt;
	usb_ep_data_t	*out_data, *in_data, *intr_data;

	ifc = dev_data->dev_curr_if;
	alt = 0;

	/*
	 * get EP descriptors
	 */
	in_data = usb_lookup_ep_data(esp->es_dip, dev_data, ifc, alt, 0,
	    USB_EP_ATTR_BULK, USB_EP_DIR_IN);
	out_data = usb_lookup_ep_data(esp->es_dip, dev_data, ifc, alt, 0,
	    USB_EP_ATTR_BULK, USB_EP_DIR_OUT);
	intr_data = usb_lookup_ep_data(esp->es_dip, dev_data, ifc, alt, 0,
	    USB_EP_ATTR_INTR, USB_EP_DIR_IN);

	if ((in_data == NULL) || (out_data == NULL) || (intr_data == NULL)) {
		USB_DPRINTF_L2(DPRINT_OPEN, esp->es_lh, "edgesp_init_pipes: "
		    "null ep_data %p %p %p",
		    (void *)in_data, (void *)out_data, (void *)intr_data);

		return (USB_FAILURE);
	}

	mutex_enter(&esp->es_mutex);
	esp->es_bulkin_pipe.pipe_ep_descr = in_data->ep_descr;
	edge_init_one_pipe(esp, &esp->es_bulkin_pipe);

	esp->es_bulkout_pipe.pipe_ep_descr = out_data->ep_descr;
	edge_init_one_pipe(esp, &esp->es_bulkout_pipe);

	esp->es_intr_pipe.pipe_ep_descr = intr_data->ep_descr;
	edge_init_one_pipe(esp, &esp->es_intr_pipe);
	mutex_exit(&esp->es_mutex);

	return (USB_SUCCESS);
}


/*
 * free resources taken by the pipes
 */
static void
edgesp_fini_pipes(edge_state_t *esp)
{
	edge_fini_one_pipe(&esp->es_bulkin_pipe);
	edge_fini_one_pipe(&esp->es_bulkout_pipe);
	edge_fini_one_pipe(&esp->es_intr_pipe);
}


static int
edgeti_init_pipes(edge_state_t *esp)
{
	usb_client_dev_data_t *dev_data = esp->es_dev_data;
	int		ifc, alt;
	usb_ep_data_t	*in_data, *out_data, *intr_data;
	edge_port_t	*ep;
	int		i;

	mutex_enter(&esp->es_mutex);

	ifc = dev_data->dev_curr_if;
	alt = 0;

	/*
	 * a bulk IN/OUT pair for each port
	 */
	for (i = 0; i < esp->es_port_cnt; i++) {
		ep = &esp->es_ports[i];

		in_data = usb_lookup_ep_data(esp->es_dip, dev_data, ifc, alt,
		    i, USB_EP_ATTR_BULK, USB_EP_DIR_IN);
		out_data = usb_lookup_ep_data(esp->es_dip, dev_data, ifc, alt,
		    i, USB_EP_ATTR_BULK, USB_EP_DIR_OUT);

		if ((in_data == NULL) || (out_data == NULL)) {
			mutex_exit(&esp->es_mutex);
			edgeti_fini_pipes(esp);
			USB_DPRINTF_L2(DPRINT_OPEN, esp->es_lh,
			    "edgeti_init_pipes[%d]: null ep_data %p %p",
			    i, (void *)in_data, (void *)out_data);

			return (USB_FAILURE);
		}

		ep->ep_bulkin_pipe.pipe_ep_descr = in_data->ep_descr;
		edge_init_one_pipe(esp, &ep->ep_bulkin_pipe);

		ep->ep_bulkout_pipe.pipe_ep_descr = out_data->ep_descr;
		edge_init_one_pipe(esp, &ep->ep_bulkout_pipe);
	}

	/* Interrupt endpoint shared between all ports */
	intr_data = usb_lookup_ep_data(esp->es_dip, dev_data, ifc, alt, 0,
	    USB_EP_ATTR_INTR, USB_EP_DIR_IN);

	if (intr_data == NULL) {
		mutex_exit(&esp->es_mutex);
		USB_DPRINTF_L2(DPRINT_OPEN, esp->es_lh,
		    "edgeti_init_pipes: null intr_data");

		return (USB_FAILURE);
	}

	esp->es_intr_pipe.pipe_ep_descr = intr_data->ep_descr;
	edge_init_one_pipe(esp, &esp->es_intr_pipe);

	mutex_exit(&esp->es_mutex);

	return (USB_SUCCESS);
}


static void
edgeti_fini_pipes(edge_state_t *esp)
{
	edge_port_t	*ep;
	int		i;

	for (i = 0; i < esp->es_port_cnt; i++) {
		ep = &esp->es_ports[i];
		edge_fini_one_pipe(&ep->ep_bulkin_pipe);
		edge_fini_one_pipe(&ep->ep_bulkout_pipe);
	}
	edge_fini_one_pipe(&esp->es_intr_pipe);
}


/*
 * generic init/fini pipes
 */
int
edge_init_pipes(edge_state_t *esp)
{
	if (esp->es_is_ti) {

		return (edgeti_init_pipes(esp));
	} else {

		return (edgesp_init_pipes(esp));
	}
}


void
edge_fini_pipes(edge_state_t *esp)
{
	if (esp->es_is_ti) {
		edgeti_fini_pipes(esp);
	} else {
		edgesp_fini_pipes(esp);
	}
}


static int
edge_open_one_pipe(edge_state_t *esp, edge_pipe_t *pipe)
{
	int	rval;
	int	usb_flags = USB_FLAGS_SLEEP;

	/* don't open for the second time */
	mutex_enter(&pipe->pipe_mutex);
	if (pipe->pipe_state != EDGE_PIPE_CLOSED) {
		mutex_exit(&pipe->pipe_mutex);

		return (USB_SUCCESS);
	}
	mutex_exit(&pipe->pipe_mutex);

	if (esp->es_is_ti) {
		if (pipe->pipe_ep_descr.bEndpointAddress & USB_EP_DIR_IN) {
			usb_flags |= USB_FLAGS_SERIALIZED_CB;
		}

		rval = usb_pipe_open(esp->es_dip, &pipe->pipe_ep_descr,
		    &pipe->pipe_policy, usb_flags,
		    &pipe->pipe_handle);
	} else {
		rval = usb_pipe_open(esp->es_dip, &pipe->pipe_ep_descr,
		    &pipe->pipe_policy, USB_FLAGS_SLEEP, &pipe->pipe_handle);
	}

	if (rval == USB_SUCCESS) {
		mutex_enter(&pipe->pipe_mutex);
		pipe->pipe_state = EDGE_PIPE_IDLE;
		mutex_exit(&pipe->pipe_mutex);
	}

	return (rval);
}


/*
 * close one pipe if open
 */
static void
edge_close_one_pipe(edge_pipe_t *pipe)
{
	/*
	 * pipe may already be closed, e.g. if device has been physically
	 * disconnected and the driver immediately detached
	 */
	if (pipe->pipe_handle != NULL) {
		usb_pipe_close(pipe->pipe_esp->es_dip, pipe->pipe_handle,
		    USB_FLAGS_SLEEP, NULL, NULL);
		mutex_enter(&pipe->pipe_mutex);
		pipe->pipe_handle = NULL;
		pipe->pipe_req = NULL;
		pipe->pipe_state = EDGE_PIPE_CLOSED;
		mutex_exit(&pipe->pipe_mutex);
	}
}



/*
 * open all device pipes that we use
 */
int
edgesp_open_pipes(edge_state_t *esp)
{
	int	rval;

	USB_DPRINTF_L4(DPRINT_OPEN, esp->es_lh, "edgesp_open_pipes");

	rval = edge_open_one_pipe(esp, &esp->es_bulkin_pipe);
	if (rval != USB_SUCCESS) {
		goto fail;
	}

	rval = edge_open_one_pipe(esp, &esp->es_bulkout_pipe);
	if (rval != USB_SUCCESS) {
		goto fail;
	}

	rval = edge_open_one_pipe(esp, &esp->es_intr_pipe);
	if (rval != USB_SUCCESS) {
		goto fail;
	}

	/* start polling on the interrupt pipe */
	edge_pipe_start_polling(&esp->es_intr_pipe);

	return (rval);

fail:
	USB_DPRINTF_L2(DPRINT_OPEN, esp->es_lh,
	    "edgesp_open_pipes: failed %d", rval);
	edgesp_close_pipes(esp);

	return (rval);
}


/*
 * close all device pipes that are open
 */
void
edgesp_close_pipes(edge_state_t *esp)
{
	USB_DPRINTF_L4(DPRINT_CLOSE, esp->es_lh, "edgesp_close_pipes");

	edge_close_one_pipe(&esp->es_bulkin_pipe);
	edge_close_one_pipe(&esp->es_bulkout_pipe);
	edge_close_one_pipe(&esp->es_intr_pipe);
}


/*
 * open global TI interrupt pipe
 */
int
edgeti_open_dev_pipes(edge_state_t *esp)
{
	int		rval;

	USB_DPRINTF_L4(DPRINT_OPEN, esp->es_lh, "edgeti_open_dev_pipes");

	rval = edge_open_one_pipe(esp, &esp->es_intr_pipe);
	if (rval != USB_SUCCESS) {
		USB_DPRINTF_L2(DPRINT_OPEN, esp->es_lh,
		    "edgeti_open_dev_pipes: failed %d", rval);

		return (rval);
	}

	/* start polling */
	edge_pipe_start_polling(&esp->es_intr_pipe);

	return (rval);
}


/*
 * Reopen IN and OUT bulk pipes if the port had them open
 */
int
edgeti_reopen_pipes(edge_state_t *esp)
{
	edge_port_t	*ep;
	int		i;

	USB_DPRINTF_L4(DPRINT_OPEN, esp->es_lh, "edgeti_reopen_pipes");

	for (i = 0; i < esp->es_port_cnt; i++) {
		ep = &esp->es_ports[i];
		mutex_enter(&ep->ep_mutex);
		if (ep->ep_state == EDGE_PORT_OPEN) {
			USB_DPRINTF_L4(DPRINT_OPEN, esp->es_lh,
			    "edgeti_reopen_pipes() reopen pipe #%d", i);
			mutex_exit(&ep->ep_mutex);
			if (edgeti_open_port_pipes(ep) != USB_SUCCESS) {

				return (USB_FAILURE);
			}
			mutex_enter(&ep->ep_mutex);
			ep->ep_no_more_reads = B_FALSE;
		}
		mutex_exit(&ep->ep_mutex);
	}

	return (USB_SUCCESS);
}


/*
 * Close IN and OUT bulk pipes if the port had them open
 */
void
edgeti_close_open_pipes(edge_state_t *esp)
{
	edge_port_t	*ep;
	int		i;

	USB_DPRINTF_L4(DPRINT_CLOSE, esp->es_lh, "edgeti_close_open_pipes");

	for (i = 0; i < esp->es_port_cnt; i++) {
		ep = &esp->es_ports[i];
		mutex_enter(&ep->ep_mutex);
		if (ep->ep_state == EDGE_PORT_OPEN) {
			ep->ep_no_more_reads = B_TRUE;
			mutex_exit(&ep->ep_mutex);
			usb_pipe_reset(esp->es_dip,
			    ep->ep_bulkin_pipe.pipe_handle, USB_FLAGS_SLEEP,
			    NULL, NULL);
			edgeti_close_port_pipes(ep);
		} else {
			mutex_exit(&ep->ep_mutex);
		}
	}
}


/*
 * Close TI interrupt pipe
 */
void
edgeti_close_dev_pipes(edge_state_t *esp)
{
	USB_DPRINTF_L4(DPRINT_CLOSE, esp->es_lh, "edgeti_close_dev_pipes");

	edge_close_one_pipe(&esp->es_intr_pipe);
}


/*
 * Open TI Edgeport bulk IN and OUT pipes
 */
int
edgeti_open_port_pipes(edge_port_t *ep)
{
	edge_state_t	*esp = ep->ep_esp;
	int		rval;

	USB_DPRINTF_L4(DPRINT_OPEN, ep->ep_lh, "edgeti_open_port_pipes");

	rval = edge_open_one_pipe(esp, &ep->ep_bulkin_pipe);
	if (rval != USB_SUCCESS) {

		goto fail;
	}

	rval = edge_open_one_pipe(esp, &ep->ep_bulkout_pipe);
	if (rval != USB_SUCCESS) {

		goto fail;
	}

	return (rval);

fail:
	USB_DPRINTF_L2(DPRINT_OPEN, ep->ep_lh,
	    "edgeti_open_port_pipes: failed %d", rval);
	edgeti_close_port_pipes(ep);

	return (rval);
}


void
edgeti_close_port_pipes(edge_port_t *ep)
{
	USB_DPRINTF_L4(DPRINT_CLOSE, ep->ep_lh, "edgeti_close_port_pipes");

	edge_close_one_pipe(&ep->ep_bulkout_pipe);
	edge_close_one_pipe(&ep->ep_bulkin_pipe);
}


/*
 * generic open/close pipes
 */
int
edge_open_pipes(edge_state_t *esp)
{
	USB_DPRINTF_L4(DPRINT_OPEN, esp->es_lh, "edge_open_pipes");

	if (esp->es_is_ti) {
		if (edgeti_reopen_pipes(esp) != USB_SUCCESS) {

			return (USB_FAILURE);
		}
	} else {

		return (edgesp_open_pipes(esp));
	}

	return (USB_SUCCESS);
}


void
edge_close_pipes(edge_state_t *esp)
{
	USB_DPRINTF_L4(DPRINT_OPEN, esp->es_lh, "edge_close_pipes");

	if (esp->es_is_ti) {
		edgeti_close_open_pipes(esp);
	} else {
		edgesp_close_pipes(esp);
	}
}


/*
 * Boot mode lightweight open/close
 */
int
edgeti_boot_open_pipes(edgeti_boot_t *etp)
{
	usb_client_dev_data_t *dev_data = etp->et_dev_data;
	usb_ep_data_t	*out_data;
	edge_pipe_t	*pipe;
	int		rval;

	/* Bulk OUT */
	out_data = usb_lookup_ep_data(etp->et_dip, dev_data,
	    dev_data->dev_curr_if, 0, 0, USB_EP_ATTR_BULK, USB_EP_DIR_OUT);

	if (out_data == NULL) {
		USB_DPRINTF_L2(DPRINT_OPEN, etp->et_lh,
		    "edgeti_boot_open_pipes: null ep_data");

		return (USB_FAILURE);
	}

	pipe = &etp->et_bulk_pipe;
	pipe->pipe_policy.pp_max_async_reqs = 2;
	pipe->pipe_etp = etp;
	pipe->pipe_lh = etp->et_lh;

	rval = usb_pipe_open(etp->et_dip, &out_data->ep_descr,
	    &pipe->pipe_policy, USB_FLAGS_SLEEP, &pipe->pipe_handle);

	/* Default */
	etp->et_def_pipe.pipe_handle = etp->et_dev_data->dev_default_ph;
	etp->et_def_pipe.pipe_etp = etp;
	etp->et_def_pipe.pipe_lh = etp->et_lh;

	return (rval);
}


void
edgeti_boot_close_pipes(edgeti_boot_t *etp)
{
	usb_pipe_close(etp->et_dip, etp->et_bulk_pipe.pipe_handle,
	    USB_FLAGS_SLEEP, NULL, NULL);
}


/*
 * start polling on the interrupt pipe
 */
void
edge_pipe_start_polling(edge_pipe_t *pipe)
{
	usb_intr_req_t	*br;
	edge_state_t	*esp = pipe->pipe_esp;
	int		rval;

	USB_DPRINTF_L4(DPRINT_INTR_PIPE, esp->es_lh, "edge_pipe_start_polling");

	mutex_enter(&pipe->pipe_mutex);
	if (pipe->pipe_state != EDGE_PIPE_IDLE) {
		mutex_exit(&pipe->pipe_mutex);

		return;
	}
	mutex_exit(&pipe->pipe_mutex);

	br = usb_alloc_intr_req(pipe->pipe_esp->es_dip, 0, USB_FLAGS_SLEEP);

	/*
	 * If it is in interrupt context, usb_alloc_intr_req will return NULL if
	 * called with SLEEP flag.
	 */
	if (!br) {
		USB_DPRINTF_L2(DPRINT_INTR_PIPE, esp->es_lh,
		    "edge_pipe_start_polling: alloc req failed.");

		return;
	}
	br->intr_attributes = USB_ATTRS_SHORT_XFER_OK | USB_ATTRS_AUTOCLEARING;
	br->intr_len = pipe->pipe_ep_descr.wMaxPacketSize;
	br->intr_client_private = (usb_opaque_t)esp;

	if (esp->es_is_ti) {
		br->intr_cb = edgeti_intr_cb;
		br->intr_exc_cb = edgeti_intr_ex_cb;
	} else {
		br->intr_cb = edgesp_intr_cb;
		br->intr_exc_cb = edgesp_intr_ex_cb;
	}

	rval = usb_pipe_intr_xfer(pipe->pipe_handle, br, USB_FLAGS_SLEEP);

	mutex_enter(&pipe->pipe_mutex);
	if (rval == USB_SUCCESS) {
		pipe->pipe_state = EDGE_PIPE_ACTIVE;
	} else {
		usb_free_intr_req(br);
		pipe->pipe_state = EDGE_PIPE_IDLE;
		USB_DPRINTF_L3(DPRINT_INTR_PIPE, esp->es_lh,
		    "edge_pipe_start_polling: failed (%d)", rval);
	}
	mutex_exit(&pipe->pipe_mutex);
}

/*
 * stop polling on the interrupt pipe
 */
void
edge_pipe_stop_polling(edge_pipe_t *intr_pipe)
{
	usb_pipe_stop_intr_polling(intr_pipe->pipe_handle,
	    USB_FLAGS_SLEEP);

	mutex_enter(&intr_pipe->pipe_mutex);
	intr_pipe->pipe_state = EDGE_PIPE_IDLE;
	mutex_exit(&intr_pipe->pipe_mutex);
}

/*
 * acquire pipe for exclusive use
 * 'flag' defines behaviour when pipe is acquired by someone else:
 *	EDGE_BLOCK	- wait until it is released;
 *	EDGE_NOBLOCK	- do not wait and return USB_FAILURE
 */
int
edge_pipe_acquire(edge_pipe_t *pipe, edge_fblock_t flag)
{
	mutex_enter(&pipe->pipe_mutex);
	/*
	 * pipe may be closed, e.g. if device was disconnected
	 */
	if (pipe->pipe_state == EDGE_PIPE_CLOSED) {
		mutex_exit(&pipe->pipe_mutex);

		return (USB_FAILURE);
	}
	ASSERT(pipe->pipe_state != EDGE_PIPE_NOT_INIT);

	if (flag == EDGE_NOBLOCK) {
		if (pipe->pipe_state != EDGE_PIPE_IDLE) {
			mutex_exit(&pipe->pipe_mutex);

			return (USB_FAILURE);
		}
	} else {
		while (pipe->pipe_state != EDGE_PIPE_IDLE) {
			cv_wait(&pipe->pipe_cv, &pipe->pipe_mutex);
		}
	}

	pipe->pipe_state = EDGE_PIPE_ACTIVE;
	mutex_exit(&pipe->pipe_mutex);

	return (USB_SUCCESS);
}


/*
 * release pipe for use by others
 */
void
edge_pipe_release(edge_pipe_t *pipe)
{
	mutex_enter(&pipe->pipe_mutex);
	/*
	 * pipe may be closed if device was disconnected
	 */
	if (pipe->pipe_state == EDGE_PIPE_CLOSED) {
		mutex_exit(&pipe->pipe_mutex);

		return;
	}
	ASSERT(pipe->pipe_state != EDGE_PIPE_NOT_INIT);

	pipe->pipe_state = EDGE_PIPE_IDLE;
	cv_signal(&pipe->pipe_cv);	/* wake next waiter */
	mutex_exit(&pipe->pipe_mutex);
}


/*
 * create request and make it a current one
 * pipe should be acquired before calling this function
 *
 * non-zero 'waiter' indicates that someone will be waiting for completion
 * of this request and edge_pipe_req_complete() will not remove it;
 * waiter must call edge_pipe_req_delete() when done with request
 *
 * 'flag' can be KM_SLEEP or KM_NOSLEEP
 */
edge_pipe_req_t	*
edge_pipe_req_new(edge_pipe_t *pipe, int waiter, int flag)
{
	edge_pipe_req_t	*req;

	if ((req = kmem_zalloc(sizeof (edge_pipe_req_t), flag)) != NULL) {
		mutex_enter(&pipe->pipe_mutex);
		ASSERT(pipe->pipe_state == EDGE_PIPE_ACTIVE);
		ASSERT(pipe->pipe_req == NULL);

		req->req_state = EDGE_REQ_RUNNING;
		req->req_waiter = waiter;

		pipe->pipe_req = req;
		mutex_exit(&pipe->pipe_mutex);
	}

	return (req);
}


/*
 * free memory and release the pipe
 */
void
edge_pipe_req_delete(edge_pipe_t *pipe, edge_pipe_req_t	*req)
{
	mutex_enter(&pipe->pipe_mutex);
	/*
	 * is this a current request on the pipe?
	 */
	if (req == pipe->pipe_req) {
		/* pipe is no longer serving this request */
		pipe->pipe_req = NULL;

		/*
		 * typically pipe state should be busy here,
		 * except when it is reset or disconnect,
		 * in which case it will be handled somewhere else
		 */
		if (pipe->pipe_state == EDGE_PIPE_ACTIVE) {
			pipe->pipe_state = EDGE_PIPE_IDLE;
			cv_signal(&pipe->pipe_cv);
		}
	}
	kmem_free(req, sizeof (edge_pipe_req_t));
	mutex_exit(&pipe->pipe_mutex);
}


/*
 * mark request complete. if there are waiters for this request,
 * save completion reason and wake them, otherwise delete request
 */
void
edge_pipe_req_complete(edge_pipe_t *pipe, uint_t cr)
{
	edge_pipe_req_t	*req;

	mutex_enter(&pipe->pipe_mutex);
	pipe->pipe_cr = cr;

	req = pipe->pipe_req;

	if (req->req_waiter) {
		req->req_cr = pipe->pipe_cr;
		req->req_state = EDGE_REQ_COMPLETE;
		cv_broadcast(&pipe->pipe_req_cv);	/* wake up waiters */
		mutex_exit(&pipe->pipe_mutex);
	} else {
		mutex_exit(&pipe->pipe_mutex);
		edge_pipe_req_delete(pipe, req);
	}
}


/*
 * wait until request is complete and return USB_SUCCESS
 */
void
edge_pipe_req_wait_completion(edge_pipe_t *pipe, edge_pipe_req_t *req)
{
	mutex_enter(&pipe->pipe_mutex);
	while (req->req_state != EDGE_REQ_COMPLETE) {
		cv_wait(&pipe->pipe_req_cv, &pipe->pipe_mutex);
	}
	mutex_exit(&pipe->pipe_mutex);
}


/*
 * submit data read request. if this function returns USB_SUCCESS,
 * pipe is acquired and request is sent, otherwise pipe is free.
 */
int
edge_receive_data(edge_pipe_t *bulkin, int len, void *cb_arg,
    edge_fblock_t fblock)
{
	edge_state_t	*esp = bulkin->pipe_esp;
	usb_bulk_req_t	*br;
	int		rval;

	ASSERT(!mutex_owned(&bulkin->pipe_mutex));

	if ((rval = edge_pipe_acquire(bulkin, fblock)) != USB_SUCCESS) {

		return (rval);
	}

	br = usb_alloc_bulk_req(esp->es_dip, len, USB_FLAGS_SLEEP);
	br->bulk_len = len;

	/* No timeout, just wait for data */
	br->bulk_timeout = 0;
	br->bulk_client_private = cb_arg;
	br->bulk_attributes = USB_ATTRS_SHORT_XFER_OK | USB_ATTRS_AUTOCLEARING;
	if (esp->es_is_ti) {
		br->bulk_cb = edgeti_bulkin_cb;
		br->bulk_exc_cb = edgeti_bulkin_cb;
	} else {
		br->bulk_cb = edgesp_bulkin_cb;
		br->bulk_exc_cb = edgesp_bulkin_cb;
	}

	rval = usb_pipe_bulk_xfer(bulkin->pipe_handle, br, 0);
	if (rval != USB_SUCCESS) {
		usb_free_bulk_req(br);
		edge_pipe_release(bulkin);
	}

	return (rval);
}


/*
 * submit data for transfer (asynchronous)
 *
 * if data was sent successfully, 'mpp' will be nulled to indicate
 * that mblk is consumed by USBA and no longer belongs to the caller.
 *
 * if reqp is NULL, then the pipe callback will free request resources,
 * otherwise the caller should take care of this.
 *
 * if this function returns USB_SUCCESS, pipe is acquired and request
 * is sent, otherwise pipe is free.
 */
int
edge_send_data(edge_pipe_t *bulkout, mblk_t **mpp, edge_pipe_req_t **reqp,
    void *cb_arg, edge_fblock_t fblock)
{
	edge_state_t	*esp = bulkout->pipe_esp;
	edge_pipe_req_t	*req;
	int		waiter = (reqp != NULL);
	usb_bulk_req_t	*br;
	int		rval;

	ASSERT(!mutex_owned(&bulkout->pipe_mutex));

	if ((rval = edge_pipe_acquire(bulkout, fblock)) != USB_SUCCESS) {

		return (rval);
	}

	if ((req = edge_pipe_req_new(bulkout, waiter, KM_SLEEP)) == NULL) {
		edge_pipe_release(bulkout);

		return (USB_FAILURE);
	}

	br = usb_alloc_bulk_req(esp->es_dip, 0, USB_FLAGS_SLEEP);
	br->bulk_len = MBLKL(*mpp);
	br->bulk_data = *mpp;
	br->bulk_timeout = EDGE_BULK_TIMEOUT;
	br->bulk_client_private = cb_arg;
	br->bulk_attributes = USB_ATTRS_AUTOCLEARING;
	if (esp->es_is_ti) {
		br->bulk_cb = edgeti_bulkout_cb;
		br->bulk_exc_cb = edgeti_bulkout_cb;
	} else {
		br->bulk_cb = edgesp_bulkout_cb;
		br->bulk_exc_cb = edgesp_bulkout_cb;
	}

	rval = usb_pipe_bulk_xfer(bulkout->pipe_handle, br, 0);
	if (rval == USB_SUCCESS) {
		*mpp = NULL;	/* data consumed */
		if (reqp) {
			*reqp = req;
		}
	} else {
		br->bulk_data = NULL;	/* we'll free it ourselves */
		usb_free_bulk_req(br);
		edge_pipe_req_delete(bulkout, req);
	}

	return (rval);
}


/*
 * synchronous version of edge_send_data()
 */
int
edge_send_data_sync(edge_pipe_t *bulkout, mblk_t **mpp, void *cb_arg)
{
	int		rval;
	edge_pipe_req_t *req;

	rval = edge_send_data(bulkout, mpp, &req, cb_arg, EDGE_BLOCK);

	if (rval != USB_SUCCESS) {

		return (rval);
	}
	ASSERT(req != NULL);

	/*
	 * wait until the transfer is over
	 */
	edge_pipe_req_wait_completion(bulkout, req);

	rval = (req->req_cr == USB_CR_OK) ? USB_SUCCESS : USB_FAILURE;

	edge_pipe_req_delete(bulkout, req);

	return (rval);
}
