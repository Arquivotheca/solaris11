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
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2005 SilverStorm Technologies, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
/*
 * Sun elects to include this software in Sun product
 * under the OpenIB BSD license.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ib/clients/rds/rdsib_cm.h>
#include <sys/ib/clients/rds/rdsib_ib.h>
#include <sys/ib/clients/rds/rdsib_buf.h>
#include <sys/ib/clients/rds/rdsib_ep.h>
#include <sys/ib/clients/rds/rds_kstat.h>

static void rds_async_handler(void *clntp, ibt_hca_hdl_t hdl,
    ibt_async_code_t code, ibt_async_event_t *event);

static struct ibt_clnt_modinfo_s rds_ib_modinfo = {
	IBTI_V_CURR,
	IBT_NETWORK,
	rds_async_handler,
	NULL,
	"RDS"
};

/* performance tunables */
uint_t		rds_no_interrupts = 0;
uint_t		rds_poll_percent_full = 25;
uint_t		rds_wc_signal = IBT_NEXT_SOLICITED;
uint_t		rds_waittime_ms = 100; /* ms */

extern dev_info_t *rdsib_dev_info;
extern void rds_close_sessions();

static void
rdsib_validate_chan_sizes(ibt_hca_attr_t *hattrp)
{
	/* The SQ size should not be more than that supported by the HCA */
	if (((MaxDataSendBuffers + RDS_NUM_ACKS) > hattrp->hca_max_chan_sz) ||
	    ((MaxDataSendBuffers + RDS_NUM_ACKS) > hattrp->hca_max_cq_sz)) {
		RDS_DPRINTF2("RDSIB", "MaxDataSendBuffers + %d is greater "
		    "than that supported by the HCA driver "
		    "(%d + %d > %d or %d), lowering it to a supported value.",
		    RDS_NUM_ACKS, MaxDataSendBuffers, RDS_NUM_ACKS,
		    hattrp->hca_max_chan_sz, hattrp->hca_max_cq_sz);

		MaxDataSendBuffers = (hattrp->hca_max_chan_sz >
		    hattrp->hca_max_cq_sz) ?
		    hattrp->hca_max_cq_sz - RDS_NUM_ACKS :
		    hattrp->hca_max_chan_sz - RDS_NUM_ACKS;
	}

	/* The RQ size should not be more than that supported by the HCA */
	if ((MaxDataRecvBuffers > hattrp->hca_max_chan_sz) ||
	    (MaxDataRecvBuffers > hattrp->hca_max_cq_sz)) {
		RDS_DPRINTF2("RDSIB", "MaxDataRecvBuffers is greater than that "
		    "supported by the HCA driver (%d > %d or %d), lowering it "
		    "to a supported value.", MaxDataRecvBuffers,
		    hattrp->hca_max_chan_sz, hattrp->hca_max_cq_sz);

		MaxDataRecvBuffers = (hattrp->hca_max_chan_sz >
		    hattrp->hca_max_cq_sz) ? hattrp->hca_max_cq_sz :
		    hattrp->hca_max_chan_sz;
	}

	/* The SQ size should not be more than that supported by the HCA */
	if ((MaxCtrlSendBuffers > hattrp->hca_max_chan_sz) ||
	    (MaxCtrlSendBuffers > hattrp->hca_max_cq_sz)) {
		RDS_DPRINTF2("RDSIB", "MaxCtrlSendBuffers is greater than that "
		    "supported by the HCA driver (%d > %d or %d), lowering it "
		    "to a supported value.", MaxCtrlSendBuffers,
		    hattrp->hca_max_chan_sz, hattrp->hca_max_cq_sz);

		MaxCtrlSendBuffers = (hattrp->hca_max_chan_sz >
		    hattrp->hca_max_cq_sz) ? hattrp->hca_max_cq_sz :
		    hattrp->hca_max_chan_sz;
	}

	/* The RQ size should not be more than that supported by the HCA */
	if ((MaxCtrlRecvBuffers > hattrp->hca_max_chan_sz) ||
	    (MaxCtrlRecvBuffers > hattrp->hca_max_cq_sz)) {
		RDS_DPRINTF2("RDSIB", "MaxCtrlRecvBuffers is greater than that "
		    "supported by the HCA driver (%d > %d or %d), lowering it "
		    "to a supported value.", MaxCtrlRecvBuffers,
		    hattrp->hca_max_chan_sz, hattrp->hca_max_cq_sz);

		MaxCtrlRecvBuffers = (hattrp->hca_max_chan_sz >
		    hattrp->hca_max_cq_sz) ? hattrp->hca_max_cq_sz :
		    hattrp->hca_max_chan_sz;
	}

	/* The MaxRecvMemory should be less than that supported by the HCA */
	if ((NDataRX * RdsPktSize) > hattrp->hca_max_memr_len) {
		RDS_DPRINTF2("RDSIB", "MaxRecvMemory is greater than that "
		    "supported by the HCA driver (%d > %d), lowering it to %d",
		    NDataRX * RdsPktSize, hattrp->hca_max_memr_len,
		    hattrp->hca_max_memr_len);

		NDataRX = hattrp->hca_max_memr_len/RdsPktSize;
	}
}

/* Return hcap, given the hca guid */
rds_hca_t *
rds_lkup_hca(ib_guid_t hca_guid)
{
	rds_hca_t	*hcap;

	RDS_DPRINTF4("rds_lkup_hca", "Enter: statep: 0x%p "
	    "guid: %llx", rdsib_statep, hca_guid);

	rw_enter(&rdsib_statep->rds_hca_lock, RW_READER);

	hcap = rdsib_statep->rds_hcalistp;
	while ((hcap != NULL) && (hcap->hca_guid != hca_guid)) {
		hcap = hcap->hca_nextp;
	}

	rw_exit(&rdsib_statep->rds_hca_lock);

	RDS_DPRINTF4("rds_lkup_hca", "return");

	return (hcap);
}

void rds_randomize_qps(rds_hca_t *hcap);

static rds_hca_t *
rdsib_init_hca(ib_guid_t hca_guid)
{
	rds_hca_t	*hcap;
	boolean_t	alloc = B_FALSE;
	int		ret;

	RDS_DPRINTF2("rdsib_init_hca", "enter: HCA 0x%llx", hca_guid);

	/* Do a HCA lookup */
	hcap = rds_lkup_hca(hca_guid);

	if (hcap != NULL && hcap->hca_hdl != NULL) {
		/*
		 * This can happen if we get IBT_HCA_ATTACH_EVENT on an HCA
		 * that we have already opened. Just return NULL so that
		 * we'll not end up reinitializing the HCA again.
		 */
		RDS_DPRINTF2("rdsib_init_hca", "HCA already initialized");
		return (NULL);
	}

	if (hcap == NULL) {
		RDS_DPRINTF2("rdsib_init_hca", "New HCA is added");
		hcap = (rds_hca_t *)kmem_zalloc(sizeof (rds_hca_t), KM_SLEEP);
		alloc = B_TRUE;
	}

	hcap->hca_guid = hca_guid;
	ret = ibt_open_hca(rdsib_statep->rds_ibhdl, hca_guid,
	    &hcap->hca_hdl);
	if (ret != IBT_SUCCESS) {
		if (ret == IBT_HCA_IN_USE) {
			RDS_DPRINTF2("rdsib_init_hca",
			    "ibt_open_hca: 0x%llx returned IBT_HCA_IN_USE",
			    hca_guid);
		} else {
			RDS_DPRINTF2("rdsib_init_hca",
			    "ibt_open_hca: 0x%llx failed: %d", hca_guid, ret);
		}
		if (alloc == B_TRUE) {
			kmem_free(hcap, sizeof (rds_hca_t));
		}
		return (NULL);
	}

	ret = ibt_query_hca(hcap->hca_hdl, &hcap->hca_attr);
	if (ret != IBT_SUCCESS) {
		RDS_DPRINTF2("rdsib_init_hca",
		    "Query HCA: 0x%llx failed:  %d", hca_guid, ret);
		ret = ibt_close_hca(hcap->hca_hdl);
		ASSERT(ret == IBT_SUCCESS);
		if (alloc == B_TRUE) {
			kmem_free(hcap, sizeof (rds_hca_t));
		} else {
			hcap->hca_hdl = NULL;
		}
		return (NULL);
	}

	ret = ibt_query_hca_ports(hcap->hca_hdl, 0,
	    &hcap->hca_pinfop, &hcap->hca_nports, &hcap->hca_pinfo_sz);
	if (ret != IBT_SUCCESS) {
		RDS_DPRINTF2("rdsib_init_hca",
		    "Query HCA 0x%llx ports failed: %d", hca_guid,
		    ret);
		ret = ibt_close_hca(hcap->hca_hdl);
		hcap->hca_hdl = NULL;
		ASSERT(ret == IBT_SUCCESS);
		if (alloc == B_TRUE) {
			kmem_free(hcap, sizeof (rds_hca_t));
		} else {
			hcap->hca_hdl = NULL;
		}
		return (NULL);
	}

	/* Only one PD per HCA is allocated, so do it here */
	ret = ibt_alloc_pd(hcap->hca_hdl, IBT_PD_NO_FLAGS,
	    &hcap->hca_pdhdl);
	if (ret != IBT_SUCCESS) {
		RDS_DPRINTF2("rdsib_init_hca",
		    "ibt_alloc_pd 0x%llx failed: %d", hca_guid, ret);
		(void) ibt_free_portinfo(hcap->hca_pinfop,
		    hcap->hca_pinfo_sz);
		ret = ibt_close_hca(hcap->hca_hdl);
		ASSERT(ret == IBT_SUCCESS);
		hcap->hca_hdl = NULL;
		if (alloc == B_TRUE) {
			kmem_free(hcap, sizeof (rds_hca_t));
		} else {
			hcap->hca_hdl = NULL;
		}
		return (NULL);
	}

	rdsib_validate_chan_sizes(&hcap->hca_attr);

	/* To minimize stale connections after ungraceful reboots */
	rds_randomize_qps(hcap);

	rw_enter(&rdsib_statep->rds_hca_lock, RW_WRITER);
	hcap->hca_state = RDS_HCA_STATE_OPEN;
	if (alloc == B_TRUE) {
		/* this is a new HCA, add it to the list */
		rdsib_statep->rds_nhcas++;
		hcap->hca_nextp = rdsib_statep->rds_hcalistp;
		rdsib_statep->rds_hcalistp = hcap;
	}
	rw_exit(&rdsib_statep->rds_hca_lock);

	RDS_DPRINTF2("rdsib_init_hca", "return: HCA 0x%llx", hca_guid);

	return (hcap);
}

/*
 * Called from attach
 */
int
rdsib_initialize_ib()
{
	ib_guid_t	*guidp;
	rds_hca_t	*hcap;
	uint_t		ix, hcaix, nhcas;
	int		ret;

	RDS_DPRINTF2("rdsib_initialize_ib", "enter: statep %p", rdsib_statep);

	ASSERT(rdsib_statep != NULL);
	if (rdsib_statep == NULL) {
		RDS_DPRINTF1("rdsib_initialize_ib",
		    "RDS Statep not initialized");
		return (-1);
	}

	/* How many hcas are there? */
	nhcas = ibt_get_hca_list(&guidp);
	if (nhcas == 0) {
		RDS_DPRINTF2("rdsib_initialize_ib", "No IB HCAs Available");
		return (-1);
	}

	RDS_DPRINTF3("rdsib_initialize_ib", "Number of HCAs: %d", nhcas);

	/* Register with IBTF */
	ret = ibt_attach(&rds_ib_modinfo, rdsib_dev_info, rdsib_statep,
	    &rdsib_statep->rds_ibhdl);
	if (ret != IBT_SUCCESS) {
		RDS_DPRINTF2("rdsib_initialize_ib", "ibt_attach failed: %d",
		    ret);
		(void) ibt_free_hca_list(guidp, nhcas);
		return (-1);
	}

	/*
	 * Open each HCA and gather its information. Don't care about HCAs
	 * that cannot be opened. It is OK as long as atleast one HCA can be
	 * opened.
	 * Initialize a HCA only if all the information is available.
	 */
	for (ix = 0, hcaix = 0; ix < nhcas; ix++) {
		RDS_DPRINTF3(LABEL, "Open HCA: 0x%llx", guidp[ix]);

		hcap = rdsib_init_hca(guidp[ix]);
		if (hcap != NULL) hcaix++;
	}

	/* free the HCA list, we are done with it */
	(void) ibt_free_hca_list(guidp, nhcas);

	if (hcaix == 0) {
		/* Failed to Initialize even one HCA */
		RDS_DPRINTF2("rdsib_initialize_ib", "No HCAs are initialized");
		(void) ibt_detach(rdsib_statep->rds_ibhdl);
		rdsib_statep->rds_ibhdl = NULL;
		return (-1);
	}

	if (hcaix < nhcas) {
		RDS_DPRINTF2("rdsib_open_ib", "HCAs %d/%d failed to initialize",
		    (nhcas - hcaix), nhcas);
	}

	RDS_DPRINTF2("rdsib_initialize_ib", "return: statep %p", rdsib_statep);

	return (0);
}

/*
 * Called from detach
 */
void
rdsib_deinitialize_ib()
{
	rds_hca_t	*hcap, *nextp;
	int		ret;

	RDS_DPRINTF2("rdsib_deinitialize_ib", "enter: statep %p", rdsib_statep);

	/* close and destroy all the sessions */
	rds_close_sessions(NULL);

	/* Release all HCA resources */
	rw_enter(&rdsib_statep->rds_hca_lock, RW_WRITER);
	RDS_DPRINTF2("rdsib_deinitialize_ib", "HCA List: %p, NHCA: %d",
	    rdsib_statep->rds_hcalistp, rdsib_statep->rds_nhcas);
	hcap = rdsib_statep->rds_hcalistp;
	rdsib_statep->rds_hcalistp = NULL;
	rdsib_statep->rds_nhcas = 0;
	rw_exit(&rdsib_statep->rds_hca_lock);

	while (hcap != NULL) {
		nextp = hcap->hca_nextp;

		if (hcap->hca_hdl != NULL) {
			ret = ibt_free_pd(hcap->hca_hdl, hcap->hca_pdhdl);
			ASSERT(ret == IBT_SUCCESS);

			(void) ibt_free_portinfo(hcap->hca_pinfop,
			    hcap->hca_pinfo_sz);

			ret = ibt_close_hca(hcap->hca_hdl);
			ASSERT(ret == IBT_SUCCESS);
		}

		kmem_free(hcap, sizeof (rds_hca_t));
		hcap = nextp;
	}

	/* Deregister with IBTF */
	if (rdsib_statep->rds_ibhdl != NULL) {
		(void) ibt_detach(rdsib_statep->rds_ibhdl);
		rdsib_statep->rds_ibhdl = NULL;
	}

	RDS_DPRINTF2("rdsib_deinitialize_ib", "return: statep %p",
	    rdsib_statep);
}

/*
 * Called on open of first RDS socket
 */
int
rdsib_open_ib()
{
	int	ret;

	RDS_DPRINTF2("rdsib_open_ib", "enter: statep %p", rdsib_statep);

	/* Enable incoming connection requests */
	if (rdsib_statep->rds_srvhdl == NULL) {
		rdsib_statep->rds_srvhdl =
		    rds_register_service(rdsib_statep->rds_ibhdl);
		if (rdsib_statep->rds_srvhdl == NULL) {
			RDS_DPRINTF2("rdsib_open_ib",
			    "Service registration failed");
			return (-1);
		} else {
			/* bind the service on all available ports */
			ret = rds_bind_service(rdsib_statep);
			if (ret != 0) {
				RDS_DPRINTF2("rdsib_open_ib",
				    "Bind service failed: %d", ret);
			}
		}
	}

	RDS_DPRINTF2("rdsib_open_ib", "return: statep %p", rdsib_statep);

	return (0);
}

/*
 * Called when all ports are closed.
 */
void
rdsib_close_ib()
{
	int	ret;

	RDS_DPRINTF2("rdsib_close_ib", "enter: statep %p", rdsib_statep);

	/* Disable incoming connection requests */
	if (rdsib_statep->rds_srvhdl != NULL) {
		ret = ibt_unbind_all_services(rdsib_statep->rds_srvhdl);
		if (ret != 0) {
			RDS_DPRINTF2("rdsib_close_ib",
			    "ibt_unbind_all_services failed: %d\n", ret);
		}
		ret = ibt_deregister_service(rdsib_statep->rds_ibhdl,
		    rdsib_statep->rds_srvhdl);
		if (ret != 0) {
			RDS_DPRINTF2("rdsib_close_ib",
			    "ibt_deregister_service failed: %d\n", ret);
		} else {
			rdsib_statep->rds_srvhdl = NULL;
		}
	}

	RDS_DPRINTF2("rdsib_close_ib", "return: statep %p", rdsib_statep);
}

/* Return hcap, given the hca guid */
rds_hca_t *
rds_get_hcap(rds_state_t *statep, ib_guid_t hca_guid)
{
	rds_hca_t	*hcap;

	RDS_DPRINTF4("rds_get_hcap", "rds_get_hcap: Enter: statep: 0x%p "
	    "guid: %llx", statep, hca_guid);

	rw_enter(&statep->rds_hca_lock, RW_READER);

	hcap = statep->rds_hcalistp;
	while ((hcap != NULL) && (hcap->hca_guid != hca_guid)) {
		hcap = hcap->hca_nextp;
	}

	/*
	 * don't let anyone use this HCA until the RECV memory
	 * is registered with this HCA
	 */
	if ((hcap != NULL) &&
	    (hcap->hca_state == RDS_HCA_STATE_MEM_REGISTERED)) {
		ASSERT(hcap->hca_mrhdl != NULL);
		rw_exit(&statep->rds_hca_lock);
		return (hcap);
	}

	RDS_DPRINTF2("rds_get_hcap",
	    "HCA (0x%p, 0x%llx) is not initialized", hcap, hca_guid);
	rw_exit(&statep->rds_hca_lock);

	RDS_DPRINTF4("rds_get_hcap", "rds_get_hcap: return");

	return (NULL);
}

/* Return hcap, given a gid */
rds_hca_t *
rds_gid_to_hcap(rds_state_t *statep, ib_gid_t gid)
{
	rds_hca_t	*hcap;
	uint_t		ix;

	RDS_DPRINTF4("rds_gid_to_hcap", "Enter: statep: 0x%p gid: %llx:%llx",
	    statep, gid.gid_prefix, gid.gid_guid);

	rw_enter(&statep->rds_hca_lock, RW_READER);

	hcap = statep->rds_hcalistp;
	while (hcap != NULL) {

		/*
		 * don't let anyone use this HCA until the RECV memory
		 * is registered with this HCA
		 */
		if (hcap->hca_state != RDS_HCA_STATE_MEM_REGISTERED) {
			RDS_DPRINTF3("rds_gid_to_hcap",
			    "HCA (0x%p, 0x%llx) is not initialized",
			    hcap, gid.gid_guid);
			hcap = hcap->hca_nextp;
			continue;
		}

		for (ix = 0; ix < hcap->hca_nports; ix++) {
			if ((hcap->hca_pinfop[ix].p_sgid_tbl[0].gid_prefix ==
			    gid.gid_prefix) &&
			    (hcap->hca_pinfop[ix].p_sgid_tbl[0].gid_guid ==
			    gid.gid_guid)) {
				RDS_DPRINTF4("rds_gid_to_hcap",
				    "gid found in hcap: 0x%p", hcap);
				rw_exit(&statep->rds_hca_lock);
				return (hcap);
			}
		}
		hcap = hcap->hca_nextp;
	}

	rw_exit(&statep->rds_hca_lock);

	return (NULL);
}

/* This is called from the send CQ handler */
void
rds_send_acknowledgement(rds_ep_t *ep)
{
	int	ret;
	uint_t	ix;

	RDS_DPRINTF4("rds_send_acknowledgement", "Enter EP(%p)", ep);

	mutex_enter(&ep->ep_lock);

	ASSERT(ep->ep_rdmacnt != 0);

	/*
	 * The previous ACK completed successfully, send the next one
	 * if more messages were received after sending the last ACK
	 */
	if (ep->ep_rbufid != *(uintptr_t *)(uintptr_t)ep->ep_ackds.ds_va) {
		*(uintptr_t *)(uintptr_t)ep->ep_ackds.ds_va = ep->ep_rbufid;
		mutex_exit(&ep->ep_lock);

		/* send acknowledgement */
		RDS_INCR_TXACKS();
		ret = ibt_post_send(ep->ep_chanhdl, &ep->ep_ackwr, 1, &ix);
		if (ret != IBT_SUCCESS) {
			RDS_DPRINTF2("rds_send_acknowledgement",
			    "EP(%p): ibt_post_send for acknowledgement "
			    "failed: %d, SQ depth: %d",
			    ep, ret, ep->ep_sndpool.pool_nbusy);
			mutex_enter(&ep->ep_lock);
			ep->ep_rdmacnt--;
			mutex_exit(&ep->ep_lock);
		}
	} else {
		/* ACKed all messages, no more to ACK */
		ep->ep_rdmacnt--;
		mutex_exit(&ep->ep_lock);
		return;
	}

	RDS_DPRINTF4("rds_send_acknowledgement", "Return EP(%p)", ep);
}

static int
rds_poll_ctrl_completions(ibt_cq_hdl_t cq, rds_ep_t *ep)
{
	ibt_wc_t	wc;
	uint_t		npolled;
	rds_buf_t	*bp;
	rds_ctrl_pkt_t	*cpkt;
	rds_qp_t	*recvqp;
	int		ret = IBT_SUCCESS;

	RDS_DPRINTF4("rds_poll_ctrl_completions", "Enter: EP(%p)", ep);

	bzero(&wc, sizeof (ibt_wc_t));
	ret = ibt_poll_cq(cq, &wc, 1, &npolled);
	if (ret != IBT_SUCCESS) {
		if (ret != IBT_CQ_EMPTY) {
			RDS_DPRINTF2(LABEL, "EP(%p) CQ(%p): ibt_poll_cq "
			    "returned: %d", ep, cq, ret);
		} else {
			RDS_DPRINTF5(LABEL, "EP(%p) CQ(%p): ibt_poll_cq "
			    "returned: IBT_CQ_EMPTY", ep, cq);
		}
		return (ret);
	}

	bp = (rds_buf_t *)(uintptr_t)wc.wc_id;

	if (wc.wc_status != IBT_WC_SUCCESS) {
		mutex_enter(&ep->ep_recvqp.qp_lock);
		ep->ep_recvqp.qp_level--;
		mutex_exit(&ep->ep_recvqp.qp_lock);

		/* Free the buffer */
		bp->buf_state = RDS_RCVBUF_FREE;
		rds_free_recv_buf(bp, 1);

		/* Receive completion failure */
		if (wc.wc_status != IBT_WC_WR_FLUSHED_ERR) {
			RDS_DPRINTF2("rds_poll_ctrl_completions",
			    "EP(%p) CQ(%p) BP(%p): WC Error Status: %d",
			    ep, cq, wc.wc_id, wc.wc_status);
		}
		return (ret);
	}

	/* there is one less in the RQ */
	recvqp = &ep->ep_recvqp;
	mutex_enter(&recvqp->qp_lock);
	recvqp->qp_level--;
	if ((recvqp->qp_taskqpending == B_FALSE) &&
	    (recvqp->qp_level <= recvqp->qp_lwm)) {
		/* Time to post more buffers into the RQ */
		recvqp->qp_taskqpending = B_TRUE;
		mutex_exit(&recvqp->qp_lock);

		ret = ddi_taskq_dispatch(rds_taskq,
		    rds_post_recv_buf, (void *)ep->ep_chanhdl, DDI_NOSLEEP);
		if (ret != DDI_SUCCESS) {
			RDS_DPRINTF2(LABEL, "ddi_taskq_dispatch failed: %d",
			    ret);
			mutex_enter(&recvqp->qp_lock);
			recvqp->qp_taskqpending = B_FALSE;
			mutex_exit(&recvqp->qp_lock);
		}
	} else {
		mutex_exit(&recvqp->qp_lock);
	}

	cpkt = (rds_ctrl_pkt_t *)(uintptr_t)bp->buf_ds.ds_va;
	rds_handle_control_message(ep->ep_sp, cpkt);

	bp->buf_state = RDS_RCVBUF_FREE;
	rds_free_recv_buf(bp, 1);

	RDS_DPRINTF4("rds_poll_ctrl_completions", "Return: EP(%p)", ep);

	return (ret);
}

#define	RDS_POST_FEW_ATATIME	100
/* Post recv WRs into the RQ. Assumes the ep->refcnt is already incremented */
void
rds_post_recv_buf(void *arg)
{
	ibt_channel_hdl_t	chanhdl;
	rds_ep_t		*ep;
	rds_session_t		*sp;
	rds_qp_t		*recvqp;
	rds_bufpool_t		*gp;
	rds_buf_t		*bp, *bp1;
	ibt_recv_wr_t		*wrp, wr[RDS_POST_FEW_ATATIME];
	rds_hca_t		*hcap;
	uint_t			npost, nspace, rcv_len;
	uint_t			ix, jx, kx;
	int			ret;

	chanhdl = (ibt_channel_hdl_t)arg;
	RDS_DPRINTF4("rds_post_recv_buf", "Enter: CHAN(%p)", chanhdl);
	RDS_INCR_POST_RCV_BUF_CALLS();

	ep = (rds_ep_t *)ibt_get_chan_private(chanhdl);
	ASSERT(ep != NULL);
	sp = ep->ep_sp;
	recvqp = &ep->ep_recvqp;

	RDS_DPRINTF5("rds_post_recv_buf", "EP(%p)", ep);

	/* get the hcap for the HCA hosting this channel */
	hcap = rds_lkup_hca(ep->ep_hca_guid);
	if (hcap == NULL) {
		RDS_DPRINTF2("rds_post_recv_buf", "HCA (0x%llx) not found",
		    ep->ep_hca_guid);
		return;
	}

	/* Make sure the session is still connected */
	rw_enter(&sp->session_lock, RW_READER);
	if ((sp->session_state != RDS_SESSION_STATE_INIT) &&
	    (sp->session_state != RDS_SESSION_STATE_CONNECTED) &&
	    (sp->session_state != RDS_SESSION_STATE_HCA_CLOSING)) {
		RDS_DPRINTF2("rds_post_recv_buf", "EP(%p): Session is not "
		    "in active state (%d)", ep, sp->session_state);
		rw_exit(&sp->session_lock);
		return;
	}
	rw_exit(&sp->session_lock);

	/* how many can be posted */
	mutex_enter(&recvqp->qp_lock);
	nspace = recvqp->qp_depth - recvqp->qp_level;
	if (nspace == 0) {
		RDS_DPRINTF2("rds_post_recv_buf", "RQ is FULL");
		recvqp->qp_taskqpending = B_FALSE;
		mutex_exit(&recvqp->qp_lock);
		return;
	}
	mutex_exit(&recvqp->qp_lock);

	if (ep->ep_type == RDS_EP_TYPE_DATA) {
		gp = &rds_dpool;
		rcv_len = RdsPktSize;
	} else {
		gp = &rds_cpool;
		rcv_len = RDS_CTRLPKT_SIZE;
	}

	bp = rds_get_buf(gp, nspace, &jx);
	if (bp == NULL) {
		RDS_DPRINTF2(LABEL, "EP(%p): No Recv buffers available", ep);
		/* try again later */
		ret = ddi_taskq_dispatch(rds_taskq, rds_post_recv_buf,
		    (void *)chanhdl, DDI_NOSLEEP);
		if (ret != DDI_SUCCESS) {
			RDS_DPRINTF2(LABEL, "ddi_taskq_dispatch failed: %d",
			    ret);
			mutex_enter(&recvqp->qp_lock);
			recvqp->qp_taskqpending = B_FALSE;
			mutex_exit(&recvqp->qp_lock);
		}
		return;
	}

	if (jx != nspace) {
		RDS_DPRINTF2(LABEL, "EP(%p): Recv buffers "
		    "needed: %d available: %d", ep, nspace, jx);
		nspace = jx;
	}

	bp1 = bp;
	for (ix = 0; ix < nspace; ix++) {
		bp1->buf_ep = ep;
		ASSERT(bp1->buf_state == RDS_RCVBUF_FREE);
		bp1->buf_state = RDS_RCVBUF_POSTED;
		bp1->buf_ds.ds_key = hcap->hca_lkey;
		bp1->buf_ds.ds_len = rcv_len;
		bp1 = bp1->buf_nextp;
	}

#if 0
	wrp = kmem_zalloc(RDS_POST_FEW_ATATIME * sizeof (ibt_recv_wr_t),
	    KM_SLEEP);
#else
	wrp = &wr[0];
#endif

	npost = nspace;
	while (npost) {
		jx = (npost > RDS_POST_FEW_ATATIME) ?
		    RDS_POST_FEW_ATATIME : npost;
		for (ix = 0; ix < jx; ix++) {
			wrp[ix].wr_id = (uintptr_t)bp;
			wrp[ix].wr_nds = 1;
			wrp[ix].wr_sgl = &bp->buf_ds;
			bp = bp->buf_nextp;
		}

		ret = ibt_post_recv(chanhdl, wrp, jx, &kx);
		if ((ret != IBT_SUCCESS) || (kx != jx)) {
			RDS_DPRINTF2(LABEL, "ibt_post_recv for %d WRs failed: "
			    "%d", npost, ret);
			npost -= kx;
			break;
		}

		npost -= jx;
	}

	mutex_enter(&recvqp->qp_lock);
	if (npost != 0) {
		RDS_DPRINTF2("rds_post_recv_buf",
		    "EP(%p) Failed to post %d WRs", ep, npost);
		recvqp->qp_level += (nspace - npost);
	} else {
		recvqp->qp_level += nspace;
	}

	/*
	 * sometimes, the recv WRs can get consumed as soon as they are
	 * posted. In that case, taskq thread to post more WRs to the RQ will
	 * not be scheduled as the taskqpending flag is still set.
	 */
	if (recvqp->qp_level == 0) {
		mutex_exit(&recvqp->qp_lock);
		ret = ddi_taskq_dispatch(rds_taskq,
		    rds_post_recv_buf, (void *)chanhdl, DDI_NOSLEEP);
		if (ret != DDI_SUCCESS) {
			RDS_DPRINTF2("rds_post_recv_buf",
			    "ddi_taskq_dispatch failed: %d", ret);
			mutex_enter(&recvqp->qp_lock);
			recvqp->qp_taskqpending = B_FALSE;
			mutex_exit(&recvqp->qp_lock);
		}
	} else {
		recvqp->qp_taskqpending = B_FALSE;
		mutex_exit(&recvqp->qp_lock);
	}

#if 0
	kmem_free(wrp, RDS_POST_FEW_ATATIME * sizeof (ibt_recv_wr_t));
#endif

	RDS_DPRINTF4("rds_post_recv_buf", "Return: EP(%p)", ep);
}

static int
rds_poll_data_completions(ibt_cq_hdl_t cq, rds_ep_t *ep)
{
	ibt_wc_t	wc;
	rds_buf_t	*bp;
	rds_data_hdr_t	*pktp;
	rds_qp_t	*recvqp;
	uint_t		npolled;
	int		ret = IBT_SUCCESS;


	RDS_DPRINTF4("rds_poll_data_completions", "Enter: EP(%p)", ep);

	bzero(&wc, sizeof (ibt_wc_t));
	ret = ibt_poll_cq(cq, &wc, 1, &npolled);
	if (ret != IBT_SUCCESS) {
		if (ret != IBT_CQ_EMPTY) {
			RDS_DPRINTF2(LABEL, "EP(%p) CQ(%p): ibt_poll_cq "
			    "returned: %d", ep, cq, ret);
		} else {
			RDS_DPRINTF5(LABEL, "EP(%p) CQ(%p): ibt_poll_cq "
			    "returned: IBT_CQ_EMPTY", ep, cq);
		}
		return (ret);
	}

	bp = (rds_buf_t *)(uintptr_t)wc.wc_id;
	ASSERT(bp->buf_state == RDS_RCVBUF_POSTED);
	bp->buf_state = RDS_RCVBUF_ONSOCKQ;
	bp->buf_nextp = NULL;

	if (wc.wc_status != IBT_WC_SUCCESS) {
		mutex_enter(&ep->ep_recvqp.qp_lock);
		ep->ep_recvqp.qp_level--;
		mutex_exit(&ep->ep_recvqp.qp_lock);

		/* free the buffer */
		bp->buf_state = RDS_RCVBUF_FREE;
		rds_free_recv_buf(bp, 1);

		/* Receive completion failure */
		if (wc.wc_status != IBT_WC_WR_FLUSHED_ERR) {
			RDS_DPRINTF2("rds_poll_data_completions",
			    "EP(%p) CQ(%p) BP(%p): WC Error Status: %d",
			    ep, cq, wc.wc_id, wc.wc_status);
			RDS_INCR_RXERRS();
		}
		return (ret);
	}

	/* there is one less in the RQ */
	recvqp = &ep->ep_recvqp;
	mutex_enter(&recvqp->qp_lock);
	recvqp->qp_level--;
	if ((recvqp->qp_taskqpending == B_FALSE) &&
	    (recvqp->qp_level <= recvqp->qp_lwm)) {
		/* Time to post more buffers into the RQ */
		recvqp->qp_taskqpending = B_TRUE;
		mutex_exit(&recvqp->qp_lock);

		ret = ddi_taskq_dispatch(rds_taskq,
		    rds_post_recv_buf, (void *)ep->ep_chanhdl, DDI_NOSLEEP);
		if (ret != DDI_SUCCESS) {
			RDS_DPRINTF2(LABEL, "ddi_taskq_dispatch failed: %d",
			    ret);
			mutex_enter(&recvqp->qp_lock);
			recvqp->qp_taskqpending = B_FALSE;
			mutex_exit(&recvqp->qp_lock);
		}
	} else {
		mutex_exit(&recvqp->qp_lock);
	}

	pktp = (rds_data_hdr_t *)(uintptr_t)bp->buf_ds.ds_va;
	ASSERT(pktp->dh_datalen != 0);

	RDS_DPRINTF5(LABEL, "Message Received: sendIP: 0x%x recvIP: 0x%x "
	    "sendport: %d recvport: %d npkts: %d pktno: %d", ep->ep_remip,
	    ep->ep_myip, pktp->dh_sendport, pktp->dh_recvport,
	    pktp->dh_npkts, pktp->dh_psn);

	RDS_DPRINTF3(LABEL, "BP(%p): npkts: %d psn: %d", bp,
	    pktp->dh_npkts, pktp->dh_psn);

	if (pktp->dh_npkts == 1) {
		/* single pkt or last packet */
		if (pktp->dh_psn != 0) {
			/* last packet of a segmented message */
			ASSERT(ep->ep_seglbp != NULL);
			ep->ep_seglbp->buf_nextp = bp;
			ep->ep_seglbp = bp;
			rds_received_msg(ep, ep->ep_segfbp);
			ep->ep_segfbp = NULL;
			ep->ep_seglbp = NULL;
		} else {
			/* single packet */
			rds_received_msg(ep, bp);
		}
	} else {
		/* multi-pkt msg */
		if (pktp->dh_psn == 0) {
			/* first packet */
			ASSERT(ep->ep_segfbp == NULL);
			ep->ep_segfbp = bp;
			ep->ep_seglbp = bp;
		} else {
			/* intermediate packet */
			ASSERT(ep->ep_segfbp != NULL);
			ep->ep_seglbp->buf_nextp = bp;
			ep->ep_seglbp = bp;
		}
	}

	RDS_DPRINTF4("rds_poll_data_completions", "Return: EP(%p)", ep);

	return (ret);
}

void
rds_recvcq_handler(ibt_cq_hdl_t cq, void *arg)
{
	rds_ep_t	*ep;
	int		ret = IBT_SUCCESS;
	int		(*func)(ibt_cq_hdl_t, rds_ep_t *);

	ep = (rds_ep_t *)arg;

	RDS_DPRINTF4("rds_recvcq_handler", "enter: EP(%p)", ep);

	if (ep->ep_type == RDS_EP_TYPE_DATA) {
		func = rds_poll_data_completions;
	} else {
		func = rds_poll_ctrl_completions;
	}

	do {
		ret = func(cq, ep);
	} while (ret != IBT_CQ_EMPTY);

	/* enable the CQ */
	ret = ibt_enable_cq_notify(cq, rds_wc_signal);
	if (ret != IBT_SUCCESS) {
		RDS_DPRINTF2(LABEL, "EP(%p) CQ(%p): ibt_enable_cq_notify "
		    "failed: %d", ep, cq, ret);
		return;
	}

	do {
		ret = func(cq, ep);
	} while (ret != IBT_CQ_EMPTY);

	RDS_DPRINTF4("rds_recvcq_handler", "Return: EP(%p)", ep);
}

void
rds_poll_send_completions(ibt_cq_hdl_t cq, rds_ep_t *ep, boolean_t lock)
{
	ibt_wc_t	wc[RDS_NUM_DATA_SEND_WCS];
	uint_t		npolled, nret, send_error = 0;
	rds_buf_t	*headp, *tailp, *bp;
	int		ret, ix;

	RDS_DPRINTF4("rds_poll_send_completions", "Enter EP(%p)", ep);

	headp = NULL;
	tailp = NULL;
	npolled = 0;
	do {
		ret = ibt_poll_cq(cq, wc, RDS_NUM_DATA_SEND_WCS, &nret);
		if (ret != IBT_SUCCESS) {
			if (ret != IBT_CQ_EMPTY) {
				RDS_DPRINTF2(LABEL, "EP(%p) CQ(%p): "
				    "ibt_poll_cq returned: %d", ep, cq, ret);
			} else {
				RDS_DPRINTF5(LABEL, "EP(%p) CQ(%p): "
				    "ibt_poll_cq returned: IBT_CQ_EMPTY",
				    ep, cq);
			}

			break;
		}

		for (ix = 0; ix < nret; ix++) {
			if (wc[ix].wc_status == IBT_WC_SUCCESS) {
				if (wc[ix].wc_type == IBT_WRC_RDMAW) {
					rds_send_acknowledgement(ep);
					continue;
				}

				bp = (rds_buf_t *)(uintptr_t)wc[ix].wc_id;
				ASSERT(bp->buf_state == RDS_SNDBUF_PENDING);
				bp->buf_state = RDS_SNDBUF_FREE;
			} else if (wc[ix].wc_status == IBT_WC_WR_FLUSHED_ERR) {
				RDS_INCR_TXERRS();
				RDS_DPRINTF5("rds_poll_send_completions",
				    "EP(%p): WC ID: %p ERROR: %d", ep,
				    wc[ix].wc_id, wc[ix].wc_status);

				send_error = 1;

				if (wc[ix].wc_id == RDS_RDMAW_WRID) {
					mutex_enter(&ep->ep_lock);
					ep->ep_rdmacnt--;
					mutex_exit(&ep->ep_lock);
					continue;
				}

				bp = (rds_buf_t *)(uintptr_t)wc[ix].wc_id;
				ASSERT(bp->buf_state == RDS_SNDBUF_PENDING);
				bp->buf_state = RDS_SNDBUF_FREE;
			} else {
				RDS_INCR_TXERRS();
				RDS_DPRINTF2("rds_poll_send_completions",
				    "EP(%p): WC ID: %p ERROR: %d", ep,
				    wc[ix].wc_id, wc[ix].wc_status);
				if (send_error == 0) {
					rds_session_t	*sp = ep->ep_sp;

					/* don't let anyone send anymore */
					rw_enter(&sp->session_lock, RW_WRITER);
					if (sp->session_state !=
					    RDS_SESSION_STATE_ERROR) {
						sp->session_state =
						    RDS_SESSION_STATE_ERROR;
						/* Make this the active end */
						sp->session_type =
						    RDS_SESSION_ACTIVE;
					}
					rw_exit(&sp->session_lock);
				}

				send_error = 1;

				if (wc[ix].wc_id == RDS_RDMAW_WRID) {
					mutex_enter(&ep->ep_lock);
					ep->ep_rdmacnt--;
					mutex_exit(&ep->ep_lock);
					continue;
				}

				bp = (rds_buf_t *)(uintptr_t)wc[ix].wc_id;
				ASSERT(bp->buf_state == RDS_SNDBUF_PENDING);
				bp->buf_state = RDS_SNDBUF_FREE;
			}

			bp->buf_nextp = NULL;
			if (headp) {
				tailp->buf_nextp = bp;
				tailp = bp;
			} else {
				headp = bp;
				tailp = bp;
			}

			npolled++;
		}

		if (rds_no_interrupts && (npolled > 100)) {
			break;
		}

		if (rds_no_interrupts == 1) {
			break;
		}
	} while (ret != IBT_CQ_EMPTY);

	RDS_DPRINTF5("rds_poll_send_completions", "Npolled: %d send_error: %d",
	    npolled, send_error);

	/* put the buffers to the pool */
	if (npolled != 0) {
		rds_free_send_buf(ep, headp, tailp, npolled, lock);
	}

	if (send_error != 0) {
		rds_handle_send_error(ep);
	}

	RDS_DPRINTF4("rds_poll_send_completions", "Return EP(%p)", ep);
}

void
rds_sendcq_handler(ibt_cq_hdl_t cq, void *arg)
{
	rds_ep_t	*ep;
	int		ret;

	ep = (rds_ep_t *)arg;

	RDS_DPRINTF4("rds_sendcq_handler", "Enter: EP(%p)", ep);

	/* enable the CQ */
	ret = ibt_enable_cq_notify(cq, IBT_NEXT_COMPLETION);
	if (ret != IBT_SUCCESS) {
		RDS_DPRINTF2(LABEL, "EP(%p) CQ(%p): ibt_enable_cq_notify "
		    "failed: %d", ep, cq, ret);
		return;
	}

	rds_poll_send_completions(cq, ep, B_FALSE);

	RDS_DPRINTF4("rds_sendcq_handler", "Return: EP(%p)", ep);
}

void
rds_ep_free_rc_channel(rds_ep_t *ep)
{
	int ret;

	RDS_DPRINTF2("rds_ep_free_rc_channel", "EP(%p) - Enter", ep);

	ASSERT(mutex_owned(&ep->ep_lock));

	/* free the QP */
	if (ep->ep_chanhdl != NULL) {
		/* wait until the RQ is empty */
		(void) ibt_flush_channel(ep->ep_chanhdl);
		(void) rds_is_recvq_empty(ep, B_TRUE);
		ret = ibt_free_channel(ep->ep_chanhdl);
		if (ret != IBT_SUCCESS) {
			RDS_DPRINTF2("rds_ep_free_rc_channel", "EP(%p) "
			    "ibt_free_channel returned: %d", ep, ret);
		}
		ep->ep_chanhdl = NULL;
	} else {
		RDS_DPRINTF2("rds_ep_free_rc_channel",
		    "EP(%p) Channel is ALREADY FREE", ep);
	}

	/* free the Send CQ */
	if (ep->ep_sendcq != NULL) {
		ret = ibt_free_cq(ep->ep_sendcq);
		if (ret != IBT_SUCCESS) {
			RDS_DPRINTF2("rds_ep_free_rc_channel",
			    "EP(%p) - for sendcq, ibt_free_cq returned %d",
			    ep, ret);
		}
		ep->ep_sendcq = NULL;
	} else {
		RDS_DPRINTF2("rds_ep_free_rc_channel",
		    "EP(%p) SendCQ is ALREADY FREE", ep);
	}

	/* free the Recv CQ */
	if (ep->ep_recvcq != NULL) {
		ret = ibt_free_cq(ep->ep_recvcq);
		if (ret != IBT_SUCCESS) {
			RDS_DPRINTF2("rds_ep_free_rc_channel",
			    "EP(%p) - for recvcq, ibt_free_cq returned %d",
			    ep, ret);
		}
		ep->ep_recvcq = NULL;
	} else {
		RDS_DPRINTF2("rds_ep_free_rc_channel",
		    "EP(%p) RecvCQ is ALREADY FREE", ep);
	}

	RDS_DPRINTF2("rds_ep_free_rc_channel", "EP(%p) - Return", ep);
}

/* Allocate resources for RC channel */
ibt_channel_hdl_t
rds_ep_alloc_rc_channel(rds_ep_t *ep, uint8_t hca_port)
{
	int				ret = IBT_SUCCESS;
	ibt_cq_attr_t			scqattr, rcqattr;
	ibt_rc_chan_alloc_args_t	chanargs;
	ibt_channel_hdl_t		chanhdl;
	rds_session_t			*sp;
	rds_hca_t			*hcap;

	RDS_DPRINTF4("rds_ep_alloc_rc_channel", "Enter: 0x%p port: %d",
	    ep, hca_port);

	/* Update the EP with the right IP address and HCA guid */
	sp = ep->ep_sp;
	ASSERT(sp != NULL);
	rw_enter(&sp->session_lock, RW_READER);
	mutex_enter(&ep->ep_lock);
	ep->ep_myip = sp->session_myip;
	ep->ep_remip = sp->session_remip;
	hcap = rds_gid_to_hcap(rdsib_statep, sp->session_lgid);
	ep->ep_hca_guid = hcap->hca_guid;
	mutex_exit(&ep->ep_lock);
	rw_exit(&sp->session_lock);

	/* reset taskqpending flag here */
	ep->ep_recvqp.qp_taskqpending = B_FALSE;

	if (ep->ep_type == RDS_EP_TYPE_CTRL) {
		scqattr.cq_size = MaxCtrlSendBuffers;
		scqattr.cq_sched = NULL;
		scqattr.cq_flags = IBT_CQ_NO_FLAGS;

		rcqattr.cq_size = MaxCtrlRecvBuffers;
		rcqattr.cq_sched = NULL;
		rcqattr.cq_flags = IBT_CQ_NO_FLAGS;

		chanargs.rc_sizes.cs_sq = MaxCtrlSendBuffers;
		chanargs.rc_sizes.cs_rq = MaxCtrlRecvBuffers;
		chanargs.rc_sizes.cs_sq_sgl = 1;
		chanargs.rc_sizes.cs_rq_sgl = 1;
	} else {
		scqattr.cq_size = MaxDataSendBuffers + RDS_NUM_ACKS;
		scqattr.cq_sched = NULL;
		scqattr.cq_flags = IBT_CQ_NO_FLAGS;

		rcqattr.cq_size = MaxDataRecvBuffers;
		rcqattr.cq_sched = NULL;
		rcqattr.cq_flags = IBT_CQ_NO_FLAGS;

		chanargs.rc_sizes.cs_sq = MaxDataSendBuffers + RDS_NUM_ACKS;
		chanargs.rc_sizes.cs_rq = MaxDataRecvBuffers;
		chanargs.rc_sizes.cs_sq_sgl = 1;
		chanargs.rc_sizes.cs_rq_sgl = 1;
	}

	mutex_enter(&ep->ep_lock);
	if (ep->ep_sendcq == NULL) {
		/* returned size is always greater than the requested size */
		ret = ibt_alloc_cq(hcap->hca_hdl, &scqattr,
		    &ep->ep_sendcq, NULL);
		if (ret != IBT_SUCCESS) {
			RDS_DPRINTF2(LABEL, "ibt_alloc_cq for sendCQ "
			    "failed, size = %d: %d", scqattr.cq_size, ret);
			mutex_exit(&ep->ep_lock);
			return (NULL);
		}

		(void) ibt_set_cq_handler(ep->ep_sendcq, rds_sendcq_handler,
		    ep);

		if (rds_no_interrupts == 0) {
			ret = ibt_enable_cq_notify(ep->ep_sendcq,
			    IBT_NEXT_COMPLETION);
			if (ret != IBT_SUCCESS) {
				RDS_DPRINTF2(LABEL,
				    "ibt_enable_cq_notify failed: %d", ret);
				(void) ibt_free_cq(ep->ep_sendcq);
				ep->ep_sendcq = NULL;
				mutex_exit(&ep->ep_lock);
				return (NULL);
			}
		}
	}

	if (ep->ep_recvcq == NULL) {
		/* returned size is always greater than the requested size */
		ret = ibt_alloc_cq(hcap->hca_hdl, &rcqattr,
		    &ep->ep_recvcq, NULL);
		if (ret != IBT_SUCCESS) {
			RDS_DPRINTF2(LABEL, "ibt_alloc_cq for recvCQ "
			    "failed, size = %d: %d", rcqattr.cq_size, ret);
			(void) ibt_free_cq(ep->ep_sendcq);
			ep->ep_sendcq = NULL;
			mutex_exit(&ep->ep_lock);
			return (NULL);
		}

		(void) ibt_set_cq_handler(ep->ep_recvcq, rds_recvcq_handler,
		    ep);

		ret = ibt_enable_cq_notify(ep->ep_recvcq, rds_wc_signal);
		if (ret != IBT_SUCCESS) {
			RDS_DPRINTF2(LABEL,
			    "ibt_enable_cq_notify failed: %d", ret);
			(void) ibt_free_cq(ep->ep_recvcq);
			ep->ep_recvcq = NULL;
			(void) ibt_free_cq(ep->ep_sendcq);
			ep->ep_sendcq = NULL;
			mutex_exit(&ep->ep_lock);
			return (NULL);
		}
	}

	chanargs.rc_flags = IBT_ALL_SIGNALED;
	chanargs.rc_control = IBT_CEP_RDMA_RD | IBT_CEP_RDMA_WR |
	    IBT_CEP_ATOMIC;
	chanargs.rc_hca_port_num = hca_port;
	chanargs.rc_scq = ep->ep_sendcq;
	chanargs.rc_rcq = ep->ep_recvcq;
	chanargs.rc_pd = hcap->hca_pdhdl;
	chanargs.rc_srq = NULL;

	ret = ibt_alloc_rc_channel(hcap->hca_hdl,
	    IBT_ACHAN_NO_FLAGS, &chanargs, &chanhdl, NULL);
	if (ret != IBT_SUCCESS) {
		RDS_DPRINTF2(LABEL, "ibt_alloc_rc_channel fail: %d",
		    ret);
		(void) ibt_free_cq(ep->ep_recvcq);
		ep->ep_recvcq = NULL;
		(void) ibt_free_cq(ep->ep_sendcq);
		ep->ep_sendcq = NULL;
		mutex_exit(&ep->ep_lock);
		return (NULL);
	}
	mutex_exit(&ep->ep_lock);

	/* Chan private should contain the ep */
	(void) ibt_set_chan_private(chanhdl, ep);

	RDS_DPRINTF4("rds_ep_alloc_rc_channel", "Return: 0x%p", chanhdl);

	return (chanhdl);
}


#if 0

/* Return node guid given a port gid */
ib_guid_t
rds_gid_to_node_guid(ib_gid_t gid)
{
	ibt_node_info_t	nodeinfo;
	int		ret;

	RDS_DPRINTF4("rds_gid_to_node_guid", "Enter: gid: %llx:%llx",
	    gid.gid_prefix, gid.gid_guid);

	ret = ibt_gid_to_node_info(gid, &nodeinfo);
	if (ret != IBT_SUCCESS) {
		RDS_DPRINTF2(LABEL, "ibt_gid_node_info for gid: %llx:%llx "
		    "failed", gid.gid_prefix, gid.gid_guid);
		return (0LL);
	}

	RDS_DPRINTF4("rds_gid_to_node_guid", "Return: Node guid: %llx",
	    nodeinfo.n_node_guid);

	return (nodeinfo.n_node_guid);
}

#endif

static void
rds_handle_portup_event(rds_state_t *statep, ibt_hca_hdl_t hdl,
    ibt_async_event_t *event)
{
	rds_hca_t		*hcap;
	ibt_hca_portinfo_t	*newpinfop, *oldpinfop;
	uint_t			newsize, oldsize, nport;
	ib_gid_t		gid;
	int			ret;

	RDS_DPRINTF2("rds_handle_portup_event",
	    "Enter: GUID: 0x%llx Statep: %p", event->ev_hca_guid, statep);

	rw_enter(&statep->rds_hca_lock, RW_WRITER);

	hcap = statep->rds_hcalistp;
	while ((hcap != NULL) && (hcap->hca_guid != event->ev_hca_guid)) {
		hcap = hcap->hca_nextp;
	}

	if (hcap == NULL) {
		RDS_DPRINTF2("rds_handle_portup_event", "HCA: 0x%llx is "
		    "not in our list", event->ev_hca_guid);
		rw_exit(&statep->rds_hca_lock);
		return;
	}

	ret = ibt_query_hca_ports(hdl, 0, &newpinfop, &nport, &newsize);
	if (ret != IBT_SUCCESS) {
		RDS_DPRINTF2(LABEL, "ibt_query_hca_ports failed: %d", ret);
		rw_exit(&statep->rds_hca_lock);
		return;
	}

	oldpinfop = hcap->hca_pinfop;
	oldsize = hcap->hca_pinfo_sz;
	hcap->hca_pinfop = newpinfop;
	hcap->hca_pinfo_sz = newsize;

	(void) ibt_free_portinfo(oldpinfop, oldsize);

	/* If RDS service is not registered then no bind is needed */
	if (statep->rds_srvhdl == NULL) {
		RDS_DPRINTF2("rds_handle_portup_event",
		    "RDS Service is not registered, so no action needed");
		rw_exit(&statep->rds_hca_lock);
		return;
	}

	/*
	 * If the service was previously bound on this port and
	 * if this port has changed state down and now up, we do not
	 * need to bind the service again. The bind is expected to
	 * persist across state changes. If the service was never bound
	 * before then we bind it this time.
	 */
	if (hcap->hca_bindhdl[event->ev_port - 1] == NULL) {

		/* structure copy */
		gid = newpinfop[event->ev_port - 1].p_sgid_tbl[0];

		/* bind RDS service on the port, pass statep as cm_private */
		ret = ibt_bind_service(statep->rds_srvhdl, gid, NULL, statep,
		    &hcap->hca_bindhdl[event->ev_port - 1]);
		if (ret != IBT_SUCCESS) {
			RDS_DPRINTF2("rds_handle_portup_event",
			    "Bind service for HCA: 0x%llx Port: %d "
			    "gid %llx:%llx returned: %d", event->ev_hca_guid,
			    event->ev_port, gid.gid_prefix, gid.gid_guid, ret);
		}
	}

	rw_exit(&statep->rds_hca_lock);

	RDS_DPRINTF2("rds_handle_portup_event", "Return: GUID: 0x%llx",
	    event->ev_hca_guid);
}

static void
rdsib_add_hca(ib_guid_t hca_guid)
{
	rds_hca_t	*hcap;
	ibt_mr_attr_t	mem_attr;
	ibt_mr_desc_t	mem_desc;
	int		ret;

	RDS_DPRINTF2("rdsib_add_hca", "Enter: GUID: 0x%llx", hca_guid);

	hcap = rdsib_init_hca(hca_guid);
	if (hcap == NULL)
		return;

	/* register the recv memory with this hca */
	mutex_enter(&rds_dpool.pool_lock);
	if (rds_dpool.pool_memp == NULL) {
		/* no memory to register */
		RDS_DPRINTF2("rdsib_add_hca", "No memory to register");
		mutex_exit(&rds_dpool.pool_lock);
		return;
	}

	mem_attr.mr_vaddr = (ib_vaddr_t)(uintptr_t)rds_dpool.pool_memp;
	mem_attr.mr_len = rds_dpool.pool_memsize;
	mem_attr.mr_as = NULL;
	mem_attr.mr_flags = IBT_MR_ENABLE_LOCAL_WRITE;

	ret = ibt_register_mr(hcap->hca_hdl, hcap->hca_pdhdl, &mem_attr,
	    &hcap->hca_mrhdl, &mem_desc);

	mutex_exit(&rds_dpool.pool_lock);

	if (ret != IBT_SUCCESS) {
		RDS_DPRINTF2("rdsib_add_hca", "ibt_register_mr failed: %d",
		    ret);
	} else {
		rw_enter(&rdsib_statep->rds_hca_lock, RW_WRITER);
		hcap->hca_state = RDS_HCA_STATE_MEM_REGISTERED;
		hcap->hca_lkey = mem_desc.md_lkey;
		hcap->hca_rkey = mem_desc.md_rkey;
		rw_exit(&rdsib_statep->rds_hca_lock);
	}

	RDS_DPRINTF2("rdsib_add_hca", "Retrun: GUID: 0x%llx", hca_guid);
}

void rds_close_this_session(rds_session_t *sp, uint8_t wait);
int rds_post_control_message(rds_session_t *sp, uint8_t code, in_port_t port);

static void
rdsib_del_hca(rds_state_t *statep, ib_guid_t hca_guid)
{
	rds_session_t	*sp;
	rds_hca_t	*hcap;
	rds_hca_state_t	saved_state;
	int		ret, ix;

	RDS_DPRINTF2("rdsib_del_hca", "Enter: GUID: 0x%llx", hca_guid);

	/*
	 * This should be a write lock as we don't want anyone to get access
	 * to the hcap while we are modifing its contents
	 */
	rw_enter(&statep->rds_hca_lock, RW_WRITER);

	hcap = statep->rds_hcalistp;
	while ((hcap != NULL) && (hcap->hca_guid != hca_guid)) {
		hcap = hcap->hca_nextp;
	}

	/* Prevent initiating any new activity on this HCA */
	ASSERT(hcap != NULL);
	saved_state = hcap->hca_state;
	hcap->hca_state = RDS_HCA_STATE_STOPPING;

	rw_exit(&statep->rds_hca_lock);

	/*
	 * stop the outgoing traffic and close any active sessions on this hca.
	 * Any pending messages in the SQ will be allowed to complete.
	 */
	rw_enter(&statep->rds_sessionlock, RW_READER);
	sp = statep->rds_sessionlistp;
	while (sp) {
		if (sp->session_hca_guid != hca_guid) {
			sp = sp->session_nextp;
			continue;
		}

		rw_enter(&sp->session_lock, RW_WRITER);
		RDS_DPRINTF2("rdsib_del_hca", "SP(%p) State: %d", sp,
		    sp->session_state);
		/*
		 * We are changing the session state in advance. This prevents
		 * further messages to be posted to the SQ. We then
		 * send a control message to the remote and tell it close
		 * the session.
		 */
		sp->session_state = RDS_SESSION_STATE_HCA_CLOSING;
		RDS_DPRINTF3("rds_handle_cm_conn_closed", "SP(%p) State "
		    "RDS_SESSION_STATE_PASSIVE_CLOSING", sp);
		rw_exit(&sp->session_lock);

		/*
		 * wait until the sendq is empty then tell the remote to
		 * close this session. This enables for graceful shutdown of
		 * the session
		 */
		(void) rds_is_sendq_empty(&sp->session_dataep, 2);
		(void) rds_post_control_message(sp,
		    RDS_CTRL_CODE_CLOSE_SESSION, 0);

		sp = sp->session_nextp;
	}

	/* wait until all the sessions are off this HCA */
	sp = statep->rds_sessionlistp;
	while (sp) {
		if (sp->session_hca_guid != hca_guid) {
			sp = sp->session_nextp;
			continue;
		}

		rw_enter(&sp->session_lock, RW_READER);
		RDS_DPRINTF2("rdsib_del_hca", "SP(%p) State: %d", sp,
		    sp->session_state);

		while ((sp->session_state == RDS_SESSION_STATE_HCA_CLOSING) ||
		    (sp->session_state == RDS_SESSION_STATE_ERROR) ||
		    (sp->session_state == RDS_SESSION_STATE_PASSIVE_CLOSING) ||
		    (sp->session_state == RDS_SESSION_STATE_CLOSED)) {
			rw_exit(&sp->session_lock);
			delay(drv_usectohz(1000000));
			rw_enter(&sp->session_lock, RW_READER);
			RDS_DPRINTF2("rdsib_del_hca", "SP(%p) State: %d", sp,
			    sp->session_state);
		}

		rw_exit(&sp->session_lock);

		sp = sp->session_nextp;
	}
	rw_exit(&statep->rds_sessionlock);

	/*
	 * if rdsib_close_ib was called before this, then that would have
	 * unbound the service on all ports. In that case, the HCA structs
	 * will contain stale bindhdls. Hence, we do not call unbind unless
	 * the service is still registered.
	 */
	if (statep->rds_srvhdl != NULL) {
		/* unbind RDS service on all ports on this HCA */
		for (ix = 0; ix < hcap->hca_nports; ix++) {
			if (hcap->hca_bindhdl[ix] == NULL) {
				continue;
			}

			RDS_DPRINTF2("rdsib_del_hca",
			    "Unbinding Service: port: %d, bindhdl: %p",
			    ix + 1, hcap->hca_bindhdl[ix]);
			(void) ibt_unbind_service(rdsib_statep->rds_srvhdl,
			    hcap->hca_bindhdl[ix]);
			hcap->hca_bindhdl[ix] = NULL;
		}
	}

	RDS_DPRINTF2("rdsib_del_hca", "HCA(%p) State: %d", hcap,
	    hcap->hca_state);

	switch (saved_state) {
	case RDS_HCA_STATE_MEM_REGISTERED:
		ASSERT(hcap->hca_mrhdl != NULL);
		ret = ibt_deregister_mr(hcap->hca_hdl, hcap->hca_mrhdl);
		if (ret != IBT_SUCCESS) {
			RDS_DPRINTF2("rdsib_del_hca",
			    "ibt_deregister_mr failed: %d", ret);
			return;
		}
		hcap->hca_mrhdl = NULL;
		/* FALLTHRU */
	case RDS_HCA_STATE_OPEN:
		ASSERT(hcap->hca_hdl != NULL);
		ASSERT(hcap->hca_pdhdl != NULL);


		ret = ibt_free_pd(hcap->hca_hdl, hcap->hca_pdhdl);
		if (ret != IBT_SUCCESS) {
			RDS_DPRINTF2("rdsib_del_hca",
			    "ibt_free_pd failed: %d", ret);
		}

		(void) ibt_free_portinfo(hcap->hca_pinfop, hcap->hca_pinfo_sz);

		ret = ibt_close_hca(hcap->hca_hdl);
		if (ret != IBT_SUCCESS) {
			RDS_DPRINTF2("rdsib_del_hca",
			    "ibt_close_hca failed: %d", ret);
		}

		hcap->hca_hdl = NULL;
		hcap->hca_pdhdl = NULL;
		hcap->hca_lkey = 0;
		hcap->hca_rkey = 0;
	}

	/*
	 * This should be a write lock as we don't want anyone to get access
	 * to the hcap while we are modifing its contents
	 */
	rw_enter(&statep->rds_hca_lock, RW_WRITER);
	hcap->hca_state = RDS_HCA_STATE_REMOVED;
	rw_exit(&statep->rds_hca_lock);

	RDS_DPRINTF2("rdsib_del_hca", "Return: GUID: 0x%llx", hca_guid);
}

static void
rds_async_handler(void *clntp, ibt_hca_hdl_t hdl, ibt_async_code_t code,
    ibt_async_event_t *event)
{
	rds_state_t		*statep = (rds_state_t *)clntp;

	RDS_DPRINTF2("rds_async_handler", "Async code: %d", code);

	switch (code) {
	case IBT_EVENT_PORT_UP:
		rds_handle_portup_event(statep, hdl, event);
		break;
	case IBT_HCA_ATTACH_EVENT:
		/*
		 * NOTE: In some error recovery paths, it is possible to
		 * receive IBT_HCA_ATTACH_EVENTs on already known HCAs.
		 */
		(void) rdsib_add_hca(event->ev_hca_guid);
		break;
	case IBT_HCA_DETACH_EVENT:
		(void) rdsib_del_hca(statep, event->ev_hca_guid);
		break;

	default:
		RDS_DPRINTF2(LABEL, "Async event: %d not handled", code);
	}

	RDS_DPRINTF2("rds_async_handler", "Return: code: %d", code);
}

/*
 * This routine exists to minimize stale connections across ungraceful
 * reboots of nodes in a cluster.
 */
void
rds_randomize_qps(rds_hca_t *hcap)
{
	ibt_cq_attr_t			cqattr;
	ibt_rc_chan_alloc_args_t	chanargs;
	ibt_channel_hdl_t		qp1, qp2;
	ibt_cq_hdl_t			cq_hdl;
	hrtime_t			nsec;
	uint8_t				i, j, rand1, rand2;
	int				ret;

	bzero(&cqattr, sizeof (ibt_cq_attr_t));
	cqattr.cq_size = 1;
	cqattr.cq_sched = NULL;
	cqattr.cq_flags = IBT_CQ_NO_FLAGS;
	ret = ibt_alloc_cq(hcap->hca_hdl, &cqattr, &cq_hdl, NULL);
	if (ret != IBT_SUCCESS) {
		RDS_DPRINTF2("rds_randomize_qps",
		    "ibt_alloc_cq failed: %d", ret);
		return;
	}

	bzero(&chanargs, sizeof (ibt_rc_chan_alloc_args_t));
	chanargs.rc_flags = IBT_ALL_SIGNALED;
	chanargs.rc_control = IBT_CEP_RDMA_RD | IBT_CEP_RDMA_WR |
	    IBT_CEP_ATOMIC;
	chanargs.rc_hca_port_num = 1;
	chanargs.rc_scq = cq_hdl;
	chanargs.rc_rcq = cq_hdl;
	chanargs.rc_pd = hcap->hca_pdhdl;
	chanargs.rc_srq = NULL;

	nsec = gethrtime();
	rand1 = (nsec & 0xF);
	rand2 = (nsec >> 4) & 0xF;
	RDS_DPRINTF2("rds_randomize_qps", "rand1: %d rand2: %d",
	    rand1, rand2);

	for (i = 0; i < rand1 + 3; i++) {
		if (ibt_alloc_rc_channel(hcap->hca_hdl,
		    IBT_ACHAN_NO_FLAGS, &chanargs, &qp1, NULL) !=
		    IBT_SUCCESS) {
			RDS_DPRINTF2("rds_randomize_qps",
			    "Bailing at i: %d", i);
			(void) ibt_free_cq(cq_hdl);
			return;
		}
		for (j = 0; j < rand2 + 3; j++) {
			if (ibt_alloc_rc_channel(hcap->hca_hdl,
			    IBT_ACHAN_NO_FLAGS, &chanargs, &qp2,
			    NULL) != IBT_SUCCESS) {
				RDS_DPRINTF2("rds_randomize_qps",
				    "Bailing at i: %d j: %d", i, j);
				(void) ibt_free_channel(qp1);
				(void) ibt_free_cq(cq_hdl);
				return;
			}
			(void) ibt_free_channel(qp2);
		}
		(void) ibt_free_channel(qp1);
	}

	(void) ibt_free_cq(cq_hdl);
}
