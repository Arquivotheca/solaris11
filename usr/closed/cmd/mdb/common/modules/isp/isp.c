/*
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/mdb_modapi.h>

#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/ispmail.h>
#include <sys/scsi/adapters/ispvar.h>
#include <sys/scsi/adapters/ispcmd.h>



/*
 * these routines are for walking the isp instance list
 */

/*
 * Initialize the isp walker by either using the given starting address,
 * or reading the value of the kernel's isp_head pointer.  We also allocate
 * a struct isp for storage, and save this using the walk_data pointer.
 */
static int
isp_walk_init(mdb_walk_state_t *wsp)
{
	/*
	 * if no addr specified then get "isp_head"
	 */
	if ((wsp->walk_addr == NULL) &&
	    (mdb_readvar(&wsp->walk_addr, "isp_head") == -1)) {
		mdb_warn("failed to read 'isp_head'");
		return (WALK_ERR);
	}

	wsp->walk_data = mdb_alloc(sizeof (struct isp), UM_SLEEP);
	return (WALK_NEXT);
}


/*
 * At each step, read a struct isp into our private storage, and then invoke
 * the callback function.  We terminate when we reach a NULL isp_next pointer.
 */
static int
isp_walk_step(mdb_walk_state_t *wsp)
{
	int	status;


	if (wsp->walk_addr == NULL) {
		/* we've finished walking (we're at the end) */
		return (WALK_DONE);
	}

	/* read the next chunk */
	if (mdb_vread(wsp->walk_data, sizeof (struct isp),
	    wsp->walk_addr) == -1) {
		mdb_warn("failed to read stuct isp at %p", wsp->walk_addr);
		return (WALK_DONE);
	}

	status = wsp->walk_callback(wsp->walk_addr, wsp->walk_data,
	    wsp->walk_cbdata);

	/* get pointer to next instance */
	wsp->walk_addr = (uintptr_t)(((struct isp *)wsp->walk_data)->isp_next);

	/* all done */
	return (status);
}


/*
 * the walker's fini function is invoked at the end of each walk
 *
 * since we dynamically allocated a isp in isp_walk_init, we must free it now
 */
static void
isp_walk_fini(mdb_walk_state_t *wsp)
{
	mdb_free(wsp->walk_data, sizeof (struct isp));
}


/*
 * dcmd: isp_slot_info
 *
 * usage: [ADDR] ::isp_slot_info [-h]
 *
 * this dcmd prints active slots given an isp instance
 *
 * if an address is supplied then that will be the isp state pointer
 *
 * if no address is supplied all isp instances will get printed
 */
static int
isp_slot_info(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct isp		p;
	struct isp_cmd		c;
	uint16_t		i;		/* slot count */
	uint16_t		j;		/* slot index */
	int			cnt = 0;	/* cnt of used slots */


	/* ensure no options */
	if (argc != 0) {
		return (DCMD_USAGE);
	}

	/*
	 * if no isp address was specified on the command line, we can
	 * print out all instances by invoking the walker, using this
	 * dcmd itself as the callback
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("isp_walk", "isp_slot_info",
		    argc, argv) == -1) {
			mdb_warn("failed to walk 'isp_walk'");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	/* print info about this isp instance */
	if (mdb_vread(&p, sizeof (p), addr) != sizeof (p)) {
		mdb_warn("failed to read struct isp at %p", addr);
		return (DCMD_ERR);
	}

	/* scan through all slots to find the number of entries */
	for (i = 0; i < ISP_MAX_SLOTS; i++) {
		/* is this slot used ?? */
		if (p.isp_slots[i].isp_cmd != NULL) {
			cnt++;
		}
	}

	/* if not slots why continue */
	if (cnt == 0) {
		mdb_printf("No active slots\n", cnt);
		return (DCMD_OK);
	}

	mdb_printf("\nActive I/O Slots for ISP Instance at %p:\n\n", addr);

	/*
	 * if this is the first invocation of the command, print a nice
	 * header line for the output that will follow
	 */
	if (DCMD_HDRSPEC(flags)) {
		/* print the header */
		mdb_printf(
		    "Slot                SCSI         Cmd             Cmd  "
		    "\n---------Request--------- "
		    "----------Response-------------     CDB\n");
		mdb_printf(
		    " No       Addr      pkt          fwd             seq   "
		    "    timeout\n    Hdr     Token Tgt.Lun "
		    "   Hdr     Token   State  Flags Len  CDB[0-2] ...\n");
	}

	for (cnt = 0, j = p.busy_slots.tail;
	    j != ISP_MAX_SLOTS && cnt < ISP_MAX_SLOTS;
	    j = p.isp_slots[j].prev) {
		uintptr_t		saddr;
		struct isp_request	*req;
		struct isp_response	*resp;
		struct cq_header	*reqh;
		struct cq_header	*resph;

		/* is this slot used ?? */
		if (p.isp_slots[j].isp_cmd == NULL) {
			mdb_warn("Slot %d on busy list but is free\n", j);
			break;
		}
		cnt++;

		/* get addr to read from */
		saddr = (uintptr_t)(p.isp_slots[j].isp_cmd);

		/* read info on this slot */
		if (mdb_vread(&c, sizeof (c), saddr) != sizeof (c)) {
			mdb_warn("failed to read struct isp at %p", saddr);
			return (DCMD_ERR);
		}

		req = &(c.cmd_isp_request);
		reqh = &(req->req_header);
		resp = &(c.cmd_isp_response);
		resph = &(resp->resp_header);

		mdb_printf("%4d 0x%-11p 0x%-11p 0x%-11p 0x%04x "
		    "%16llx\n"
		    "%d/%d/%d/%03d %8x %2d.%-2d  "
		    "%d/%d/%d/%03d %8x 0x%04x 0x%04x "
		    "%2d 0x%02x%02x%02x\n",
		    j, saddr, c.cmd_pkt, c.cmd_forw, p.isp_slots[j].seq,
		    p.isp_slots[j].timeout,
		    reqh->cq_entry_count, reqh->cq_entry_type,
		    reqh->cq_flags, reqh->cq_seqno,
		    req->req_token,
		    req->req_scsi_id.req_target, req->req_scsi_id.req_lun_trn,
		    resph->cq_entry_count, resph->cq_entry_type,
		    resph->cq_flags, resph->cq_seqno,
		    resp->resp_token, resp->resp_state,
		    resp->resp_status_flags, c.cmd_cdblen,
		    c.cmd_cdb[0], c.cmd_cdb[1], c.cmd_cdb[2]);
	}

	mdb_printf("\nActive slots: %d\n", cnt);

	return (DCMD_OK);
}


/*
 * isp_slot_info_help - return help on isp_slot_info
 */
static void
isp_slot_info_help(void)
{
	mdb_printf("Print information about the active slots in one or "
	    "all isp instances\n");
	mdb_printf("\nUsage: [isp_instance_addr] ::isp_slot_info\n");
	mdb_printf("\nIf no isp instance address is supplied then all\n");
	mdb_printf("isp instances are walked (using isp_walk)\n");
}


static void isp_check(struct isp *);

/*
 * the dcmd: isp_info
 *
 * usage: [ADDR] ::isp_info [-lc]
 *
 * this is the dcmd to print a (short by default else long) one-line listing
 * of the isp state structure
 *
 * if an address is passed in then we display just that instance
 *
 * if no address is passed in then we display the state for all isp instances
 */
static int
isp_info(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct isp	p;
	int		i;			/* for getting opts */
	uint_t		long_mode = FALSE;
	uint_t		check_mode = FALSE;
	struct dev_info	d;


	/* check for options */
	if ((i = mdb_getopts(argc, argv,
	    'l', MDB_OPT_SETBITS, TRUE, &long_mode,
	    'c', MDB_OPT_SETBITS, TRUE, &check_mode,
	    NULL)) != argc) {
		/* left over arguments ?? */
		return (DCMD_USAGE);
	}

	/* if any args left we have a problem */
	if ((argc - i) != 0) {
		/* don't need no stinkin' args */
		return (DCMD_USAGE);
	}

	/*
	 * if no isp address was specified on the command line, we can
	 * print out all processes by invoking the walker, using this
	 * dcmd itself as the callback
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("isp_walk", "isp_info",
		    argc, argv) == -1) {
			mdb_warn("failed to walk using `isp_walk'");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	/*
	 * if this is the first invocation of the command, print a nice
	 * header line for the output that will follow
	 */
	if (DCMD_HDRSPEC(flags)) {
		/* print first line of heading */
		mdb_printf(
		    "                              SCSI  Tag Init "
		    "             ReqQ   RespQ  ReqQ");
		if (long_mode) {
			mdb_printf(
			    "  Free      Marker  Marker   In   In   No");
		}
		mdb_printf("\n");
		/* print second line of heading */
		mdb_printf(
		    "ISP      Addr        Next     Opts  Age  ID  "
		    "Susp In/Out  In/Out Space");
		if (long_mode) {
			mdb_printf(
			    " Slot Alive In/Out  Free   Intr Reset OBP");
		}
		mdb_printf("\n");
	}

	/*
	 * XXX: spacing will be different on 64-bit systems
	 */

	/*
	 * print info about this isp instance
	 */

	/* read in this isp instance struct */
	if (mdb_vread(&p, sizeof (p), addr) != sizeof (p)) {
		/* couldn't even read this isp instance! */
		mdb_warn("failed to read struct isp at %p", addr);
		return (DCMD_ERR);
	}

	/* read in the dip (for the instance number) */
	if (mdb_vread(&d, sizeof (d), (uintptr_t)(p.isp_dip)) !=
	    sizeof (d)) {
		/* couldn't read the dip for this instance ??? */
		mdb_warn("failed to read struct dev_info for isp at %p",
		    (uintptr_t)(p.isp_dip));
		return (DCMD_ERR);
	}

	/* print the standard line */
	mdb_printf("%2d  %11p %11p  0x%04x %2d   %2d  "
	    " %1d    %3d/%-3d %3d/%-3d  %3d",
	    d.devi_instance,
	    addr, p.isp_next, p.isp_scsi_options,
	    p.isp_scsi_tag_age_limit, p.isp_initiator_id,
	    p.isp_suspended, p.isp_request_in, p.isp_request_out,
	    p.isp_response_in, p.isp_response_out,
	    p.isp_queue_space);

	/* if long mode requested print more */
	if (long_mode) {
		mdb_printf(" %5d  %4d%3d/%-3d   %3d     %1d    %1d    %1d",
		    p.free_slots.head, p.isp_alive, p.isp_marker_in,
		    p.isp_marker_out, p.isp_marker_free,
		    p.isp_in_intr, p.isp_in_reset, p.isp_no_obp);
	}

	/* done with this line */
	mdb_printf("\n");

	/*
	 * if in check mode then do common-sense checks for this instance,
	 * printing out the results in the form of notes and warnings
	 */
	if (check_mode) {
		/* do checking */
		isp_check(&p);
	}

	return (DCMD_OK);
}


/*
 * isp_info_help: help function for isp_info
 */
static void
isp_info_help(void)
{
	mdb_printf("Print information about one or all isp instances\n");
	mdb_printf("\nUsage: [isp_instance_addr] ::isp_info [-lc]\n");
	mdb_printf(
	    "\nwhere: -l    => long mode (display wider than 80 columns)\n");
	mdb_printf(
	    "and:   -c    => perform heuristic checks on driver sanity\n");
	mdb_printf("\nIf no isp instance address is supplied then all\n");
	mdb_printf("isp instances are walked (using isp_walk)\n");
}



/*
 * check an isp instance for sanity, printing results found
 */
static void
isp_check(struct isp *isp)
{
	int		requests;
	int		responses;
	int		waiting;
	struct isp_cmd	*ic;
	int		i;			/* index */
	int		c;			/* count */
	int		free_count;


	/* are we alive ?? */
	if (!isp->isp_alive) {
		mdb_printf("Note: isp instance is not alive "
		    "=> no I/O since last sanity check\n");
	}

	/* check number of requests */
	if ((requests = isp->isp_request_in - isp->isp_request_out) < 0) {
		/* wrap around */
		requests += ISP_MAX_REQUESTS;
	}
	if (requests > 0) {
		mdb_printf("Note: %d requests pending to ISP chip\n",
		    requests);
	}

	/* check request queue free space */
	if (requests != (ISP_MAX_REQUESTS - 1 - isp->isp_queue_space)) {
		mdb_printf("Note: queue space does not agree "
		    "with request queue count\n");
	}

	/* check for mbox request_out matching */
	if (isp->isp_mbox.mbox_cmd.mbox_in[0] == ISP_MBOX_EVENT_CMD) {
		/* last mbox event was a command completion result */
		if (isp->isp_mbox.mbox_cmd.mbox_in[4] !=
		    isp->isp_request_out) {
			mdb_printf("Note: request_out (%d) does not agree "
			    "with last read mbox (%d)\n",
			    isp->isp_request_out,
			    isp->isp_mbox.mbox_cmd.mbox_in[4]);
		}
	}

	/* check wait queue */
	if (isp->isp_waitf != NULL) {
		waiting = 0;
		ic = isp->isp_waitf;
		while (ic != NULL) {
			ic = ic->cmd_forw;
			waiting++;
		}
		mdb_printf("Note: %d requests on wait queue\n", waiting);
	}

	/* check number of responses */
	if ((responses = isp->isp_response_in - isp->isp_response_out) < 0) {
		responses += ISP_MAX_RESPONSES;
	}
	if (responses > 0) {
		mdb_printf("Note: %d responses pending from ISP chip\n",
		    responses);
	}

	/* check for mbox response_out matching */
	if (isp->isp_mbox.mbox_cmd.mbox_in[0] == ISP_MBOX_EVENT_CMD) {
		/* last mbox event was a command completion result */
		if (isp->isp_mbox.mbox_cmd.mbox_in[5] !=
		    isp->isp_response_in) {
			mdb_printf("Note: response_in (%d) does not agree "
			    "with last read mbox (%d)\n",
			    isp->isp_response_in,
			    isp->isp_mbox.mbox_cmd.mbox_in[5]);
		}
	}

	/* check each response entry for sanity */
	i = isp->isp_response_in;
	for (c = 0; c < responses; c++) {
		if (isp->isp_response_base[i].resp_header.cq_entry_type !=
		    CQ_TYPE_RESPONSE) {
			mdb_warn("Warning: non-responses in the "
			    "response queue @ 0x%p",
			    isp->isp_response_base + i);
		}
		if (--i < 0) {
			/* wrap around */
			i = ISP_MAX_RESPONSES - 1;
		}
	}

	/* check the slots lists contain all the slots */
	for (c = 0, i = isp->busy_slots.tail;
	    i != ISP_MAX_SLOTS && c < ISP_MAX_SLOTS;
	    i = isp->isp_slots[i].prev) {
		if (isp->isp_slots[i].isp_cmd == NULL) {
			mdb_warn("Slot %d on busy list but is free\n", i);
		}
		if (isp->isp_slots[i].prev != ISP_MAX_SLOTS &&
		    isp->isp_slots[isp->isp_slots[i].prev].next != i) {
			mdb_warn("bad prev/next in slots %d and %d\n",
			    isp->isp_slots[i].prev, i);
		}
		c++;
	}
	for (free_count = 0, i = isp->free_slots.tail;
	    i != ISP_MAX_SLOTS && free_count < ISP_MAX_SLOTS;
	    i = isp->isp_slots[i].prev) {
		if (isp->isp_slots[i].isp_cmd != NULL) {
			mdb_warn("Slot %d on free list but is busy\n", i);
		}
		if (isp->isp_slots[i].prev != ISP_MAX_SLOTS &&
		    isp->isp_slots[isp->isp_slots[i].prev].next != i) {
			mdb_warn("bad prev/next in slots %d and %d\n",
			    isp->isp_slots[i].prev, i);
		}
		free_count++;
	}
	if (c + free_count != ISP_MAX_SLOTS) {
		mdb_printf("Slot list count mismatch. %d slots found on busy"
		    " and %d on free lists, %d slots in array\n", c,
		    free_count, ISP_MAX_SLOTS);
	} else {
		mdb_printf("%d busy slots\n", c);
		mdb_printf("%d free slots\n", free_count);
	}
}


/*
 * dcmd: isp_request_info
 *
 * usage: [ADDR] ::isp_request_info
 *
 * this dcmd prints active requests given an isp instance
 *
 * if an address is supplied then that will be the isp state pointer
 *
 * if no address is supplied all isp instances will get printed
 */
static int
isp_request_info(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	struct isp		p;
	int			requests;
	int			i;		/* index */
	int			c;		/* count */


	/* ensure no options */
	if (argc != 0) {
		return (DCMD_USAGE);
	}

	/*
	 * if no isp address was specified on the command line, we can
	 * print out all processes by invoking the walker, using this
	 * dcmd itself as the callback
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		if (mdb_walk_dcmd("isp_walk", "isp_request_info",
		    argc, argv) == -1) {
			mdb_warn("failed to walk 'isp_walk'");
			return (DCMD_ERR);
		}
		return (DCMD_OK);
	}

	/* print info about this isp instance */
	if (mdb_vread(&p, sizeof (p), addr) != sizeof (p)) {
		mdb_warn("failed to read struct isp at %p\n", addr);
		return (DCMD_ERR);
	}

	/* get number of requests */
	if ((requests = p.isp_request_in - p.isp_request_out) < 0) {
		requests += ISP_MAX_REQUESTS;
	}

	/* if not slots why continue */
	if (requests == 0) {
		mdb_printf("No requests pending\n");
		return (DCMD_OK);
	}

	mdb_printf("\nRequests for ISP Instance at %p:\n\n", addr);

	/*
	 * if this is the first invocation of the command, print a nice
	 * header line for the output that will follow
	 */
	if (DCMD_HDRSPEC(flags)) {
		/* print the header */
		mdb_printf("                   -------cq_header----"
		    "                         CDB\n");
		mdb_printf("Index     Addr     type cnt seqno flags"
		    "  token  Tgt.Lun Flags   Len        CDB[0-15]\n");
	}

	/* scan through all requests */
	i = p.isp_request_in;
	for (c = 0; c < requests; c++) {
		uintptr_t		saddr;
		struct isp_request	r;
		struct isp_request	*req;
		struct cq_header	*reqh;


		saddr = (uintptr_t)(p.isp_request_base + i);

		if (mdb_vread(&r, sizeof (r), saddr) != sizeof (r)) {
			mdb_warn("failed to read struct isp_request at %p\n",
			    saddr);
			return (DCMD_ERR);
		}

		req = &r;
		reqh = &(req->req_header);

		mdb_printf(" %3d 0x%-11p  %d    %d    %d     %d   %8x "
		    "%2d.%-2d  0x%04x  %2d   0x%08x%08x\n",
		    i, saddr, reqh->cq_entry_type, reqh->cq_entry_count,
		    reqh->cq_seqno, reqh->cq_flags, req->req_token,
		    req->req_scsi_id.req_target, req->req_scsi_id.req_lun_trn,
		    req->req_flags,
		    req->req_cdblen,
		    req->req_cdb[0], req->req_cdb[1]);

		if (--i < 0) {
			i = ISP_MAX_REQUESTS - 1;
		}
	}

	mdb_printf("\nrequests pending: %d\n", requests);

	return (DCMD_OK);
}


/*
 * isp_request_info_help - print help info for isp_request_info
 */
static void
isp_request_info_help(void)
{
	mdb_printf("Print information about outstanding requests for"
	    "one or all isp instances\n");
	mdb_printf("\nUsage: [isp_instance_addr] ::isp_request_info\n");
	mdb_printf("\nIf no isp instance address is supplied then all\n");
	mdb_printf("isp instances are walked (using isp_walk)\n");
}


/*
 * MDB module linkage information:
 *
 * We declare a list of structures describing our dcmds, a list of structures
 * describing our walkers, and a function named _mdb_init to return a pointer
 * to our module information.
 */

static const mdb_dcmd_t isp_dcmds[] = {
	{ "isp_info", "[-lc]", "info about an isp instance", isp_info,
	    isp_info_help },
	{ "isp_slot_info", NULL, "info about all active isp cmds",
	    isp_slot_info, isp_slot_info_help },
	{ "isp_request_info", NULL, "info about all active isp requests",
	    isp_request_info, isp_request_info_help },
	{ NULL }
};

static const mdb_walker_t isp_walkers[] = {
	{ "isp_walk", "walk list of isp structures",
	    isp_walk_init, isp_walk_step, isp_walk_fini },
	{ NULL }
};

static const mdb_modinfo_t modinfo = {
	MDB_API_VERSION, isp_dcmds, isp_walkers
};

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
