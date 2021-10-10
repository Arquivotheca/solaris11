/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * NCP Crypto DR Module Domain Service message handling
 */

#include <sys/sunddi.h>
#include <sys/ds.h>
#include <sys/ncp.h>
#include <sys/n2_crypto_dr.h>


/*
 * Supported DS Capability Versions
 */
static ds_ver_t		dr_mau_vers[] = { { 1, 0 } };
#define	DR_MAU_NVERS	(sizeof (dr_mau_vers) / sizeof (dr_mau_vers[0]))

/*
 * DS Capability Description
 */
static ds_capability_t ncp_dr_cap = {
	DR_CRYPTO_MAU_ID,	/* svc_id */
	dr_mau_vers,		/* vers */
	DR_MAU_NVERS		/* nvers */
};

/*
 * DS Callbacks
 */
static void ncp_dr_reg_handler(ds_cb_arg_t, ds_ver_t *, ds_svc_hdl_t);
static void ncp_dr_unreg_handler(ds_cb_arg_t arg);
static void ncp_dr_data_handler(ds_cb_arg_t arg, void *buf, size_t buflen);

/*
 * DS Client Ops Vector
 */
static ds_clnt_ops_t ncp_dr_ops = {
	ncp_dr_reg_handler,	/* ds_reg_cb */
	ncp_dr_unreg_handler,	/* ds_unreg_cb */
	ncp_dr_data_handler,	/* ds_data_cb */
	NULL			/* cb_arg */
};


/*
 * Internal Functions
 */

static void ncp_dr_crypto_config(ncp_t *, dr_crypto_hdr_t *,
	dr_crypto_hdr_t **, int *);
static void ncp_dr_crypto_unconfig(ncp_t *, dr_crypto_hdr_t *,
	dr_crypto_hdr_t **, int *);
static void ncp_dr_crypto_status(ncp_t *, dr_crypto_hdr_t *,
	dr_crypto_hdr_t **, int *);
static size_t ncp_dr_pack_response(dr_crypto_hdr_t *, dr_crypto_res_t *,
	dr_crypto_hdr_t **);

static int (*ncp_ds_init)(ds_capability_t *, ds_clnt_ops_t *);
static int (*ncp_ds_fini)(ds_capability_t *);
static int (*ncp_ds_send)(ds_svc_hdl_t, void *, size_t);

/* Stubs for DS */

/* ARGSUSED */
static int
ncp_ds_init_stub(ds_capability_t *cap, ds_clnt_ops_t *ops)
{
	return (ENOSYS);
}

/* ARGSUSED */
static int
ncp_ds_fini_stub(ds_capability_t *cap)
{
	return (ENOSYS);
}

/* ARGSUSED */
static int
ncp_ds_send_stub(ds_svc_hdl_t hdl, void *buf, size_t len)
{
	return (ENOSYS);
}


/*
 * manually resolve domain services (DS) entry points.
 *
 * on systems running pre-LDOMs fw the DS
 * module does not load so detection has
 * to be done manually.
 */
static void
ncp_ds_resolve(void)
{
	/* Try to load the DS module */
	if (modload("misc", "ds") == -1) {
		goto no_ds;
	}

	/* find needed DS entry points */

	ncp_ds_init = (int (*)(ds_capability_t *, ds_clnt_ops_t *))modlookup(
	    "misc/ds", "ds_cap_init");

	if (ncp_ds_init == 0) {
		goto no_ds;
	}

	ncp_ds_fini = (int (*)(ds_capability_t *))modlookup(
	    "misc/ds", "ds_cap_fini");

	if (ncp_ds_fini == 0) {
		goto no_ds;
	}

	ncp_ds_send = (int (*)(ds_svc_hdl_t, void *, size_t))modlookup(
	    "misc/ds", "ds_cap_send");

	if (ncp_ds_send == 0) {
		goto no_ds;
	}

	return;

no_ds:
	/* DS not present log a message and stub out ds calls */
	cmn_err(CE_NOTE,
	    "ncp: crypto DR support not enabled, "
	    "domain services interfaces not present.");
	ncp_ds_send = ncp_ds_send_stub;
	ncp_ds_fini = ncp_ds_fini_stub;
	ncp_ds_init = ncp_ds_init_stub;
}

/*
 * register the ncp crypto domain services capabilities.
 */
int
ncp_dr_init(ncp_t *ncp)
{
	int	rv;

	ncp_dr_ops.cb_arg = ncp;

	/* locate ds entry points */
	ncp_ds_resolve();

	rv = ncp_ds_init(&ncp_dr_cap, &ncp_dr_ops);

	/*
	 * If we get a return value of ENOSYS, then the ds module
	 * is not present. Therefore we won't have any DR operations.
	 * We have already notified the user so don't fail here. However,
	 * if we have received some other error condition fail the
	 * initialization.
	 */
	if (rv != 0 && rv != ENOSYS) {
		cmn_err(CE_NOTE, "ds_cap_init failed: %d", rv);
		return (-1);
	}

	return (0);
}

/*
 * unregister the ncp crypto domain services capabilities.
 */
int
ncp_dr_fini(void)
{
	int	rv;

	if ((rv = ncp_ds_fini(&ncp_dr_cap)) != 0) {
		cmn_err(CE_NOTE, "ds_cap_fini failed: %d", rv);
		return (-1);
	}

	return (0);
}

static void
ncp_dr_reg_handler(ds_cb_arg_t arg, ds_ver_t *ver, ds_svc_hdl_t hdl)
{
	ncp_t	*ncp = arg;

	DBG4(arg, DDR, "reg_handler: arg=0x%p, ver=%d.%d, hdl=0x%lx\n",
	    arg, ver->major, ver->minor, hdl);

	ncp->n_ds_handle = hdl;
}

static void
ncp_dr_unreg_handler(ds_cb_arg_t arg)
{
	ncp_t	*ncp = arg;

	DBG1(ncp, DDR, "unreg_handler: arg=0x%p\n", arg);

	ncp->n_ds_handle = DS_INVALID_HDL;
}

/*
 * Data handler.  This is the entry point for crypto domain service
 * requests.
 */
static void
ncp_dr_data_handler(ds_cb_arg_t arg, void *buf, size_t buflen)
{
	ncp_t		*ncp = arg;
	dr_crypto_hdr_t	*req = buf;
	dr_crypto_hdr_t	err_resp;
	dr_crypto_hdr_t	*resp = &err_resp;
	int		resp_len = 0;

	/*
	 * Sanity check the message
	 */
	if (buflen < sizeof (dr_crypto_hdr_t)) {
		DBG2(NULL, DWARN,
		    "incoming message short: expected at least %ld "
		    "bytes, received %ld\n", sizeof (dr_crypto_hdr_t), buflen);
		goto done;
	}

	if (req == NULL) {
		DBG1(NULL, DWARN,
		    "empty message: expected at least %ld bytes\n",
		    sizeof (dr_crypto_hdr_t));
		goto done;
	}

	DBG1(ncp, DDR, "incoming request:%d\n", req->msg_type);

	if (req->num_records > N2_DR_MAX_CRYPTO_UNITS) {
		DBG2(ncp, DWARN,
		    "CPU list too long: %d when %d is the maximum\n",
		    req->num_records, N2_DR_MAX_CRYPTO_UNITS);
		goto done;
	}

	if (req->num_records == 0) {
		DBG0(ncp, DWARN, "No CPU specified for operation\n");
		goto done;
	}

	/*
	 * Process the command
	 */
	switch (req->msg_type) {
	case DR_CRYPTO_CONFIG:
		ncp_dr_crypto_config(ncp, req, &resp, &resp_len);
		break;
	case DR_CRYPTO_UNCONFIG:
	case DR_CRYPTO_FORCE_UNCONFIG:
		ncp_dr_crypto_unconfig(ncp, req, &resp, &resp_len);
		break;
	case DR_CRYPTO_STATUS:
		ncp_dr_crypto_status(ncp, req, &resp, &resp_len);
		break;
	default:
		cmn_err(CE_NOTE, "unsupported DR operation (%d)",
		    req->msg_type);
		break;
	}

done:
	/* check if an error occurred */
	if (resp == &err_resp) {
		resp->req_num = (req) ? req->req_num : 0;
		resp->msg_type = DR_CRYPTO_ERROR;
		resp->num_records = 0;
		resp_len = sizeof (dr_crypto_hdr_t);
	}

	DBG2(ncp, DDR, "outgoing response:%d, len %d\n",
	    resp->msg_type, resp_len);

	/* send back the response */
	if (ncp_ds_send(ncp->n_ds_handle, resp, resp_len) != 0) {
		DBG0(ncp, DWARN, "ds_send failed\n");
	}

	/* free any allocated memory */
	if (resp != &err_resp) {
		kmem_free(resp, resp_len);
	}
}


/*
 * DR in a new crypto unit.
 */
static void
ncp_dr_crypto_config(ncp_t *ncp, dr_crypto_hdr_t *req,
	dr_crypto_hdr_t **resp, int *resp_len)
{
	int		idx;
	int		count;
	uint32_t	*req_cpus;
	dr_crypto_res_t	res[N2_DR_MAX_CRYPTO_UNITS];

	ASSERT((req != NULL) && (req->num_records != 0));

	count = req->num_records;

	/* the incoming array of cpuids to operate on */
	req_cpus = DR_CRYPTO_CMD_CPUIDS(req);

	/* try to config/onine the crypto unit */
	for (idx = 0; idx < count; idx++) {
		res[idx].cpuid = req_cpus[idx];
		ncp_mau_config(ncp, &res[idx]);
	}

	*resp_len =  ncp_dr_pack_response(req, res, resp);
}

/*
 * DR out a crypto unit.
 */
static void
ncp_dr_crypto_unconfig(ncp_t *ncp, dr_crypto_hdr_t *req,
	dr_crypto_hdr_t **resp, int *resp_len)
{
	int		idx;
	int		count;
	uint32_t	*req_cpus;
	dr_crypto_res_t	res[N2_DR_MAX_CRYPTO_UNITS];

	ASSERT((req != NULL) && (req->num_records != 0));

	count = req->num_records;

	/* the incoming array of cpuids to operate on */
	req_cpus = DR_CRYPTO_CMD_CPUIDS(req);

	/* try to config/onine the crypto unit */
	for (idx = 0; idx < count; idx++) {
		res[idx].cpuid = req_cpus[idx];
		ncp_mau_unconfig(ncp, &res[idx]);
	}

	*resp_len =  ncp_dr_pack_response(req, res, resp);
}


/*
 * build a crypto domain services response message
 */
static size_t
ncp_dr_pack_response(dr_crypto_hdr_t *req, dr_crypto_res_t *res,
	dr_crypto_hdr_t **respp)
{
	int			idx;
	dr_crypto_hdr_t		*resp;
	dr_crypto_stat_t	*resp_stat;
	size_t			resp_len;
	size_t			stat_len;
	int			nstat = req->num_records;

	/*
	 * Calculate the size of the response message
	 * and allocate an appropriately sized buffer.
	 */
	resp_len = 0;

	/* add the header size */
	resp_len += sizeof (dr_crypto_hdr_t);

	/* add the stat array size */
	stat_len = sizeof (dr_crypto_stat_t) * nstat;
	resp_len += stat_len;

	/* allocate the message buffer */
	resp = kmem_zalloc(resp_len, KM_SLEEP);

	/*
	 * Fill in the header information.
	 */
	resp->req_num = req->req_num;
	resp->msg_type = DR_CRYPTO_OK;
	resp->num_records = nstat;

	/*
	 * Fill in the stat information.
	 */
	resp_stat = DR_CRYPTO_RESP_STATS(resp);

	for (idx = 0; idx < nstat; idx++) {
		resp_stat[idx].cpuid = res[idx].cpuid;
		resp_stat[idx].result = res[idx].result;
		resp_stat[idx].status = res[idx].status;
	}

	*respp = resp;
	return (resp_len);
}

static void
ncp_dr_crypto_status(ncp_t *ncp, dr_crypto_hdr_t *req, dr_crypto_hdr_t **resp,
	int *resp_len)
{
	int			idx;
	int			rlen;
	uint32_t		*cpuids;
	dr_crypto_hdr_t		*rp;
	dr_crypto_stat_t	*stat;

	/* the incoming array of cpuids to configure */
	cpuids = DR_CRYPTO_CMD_CPUIDS(req);

	/* allocate a response message */
	rlen = sizeof (dr_crypto_hdr_t);
	rlen += req->num_records * sizeof (dr_crypto_stat_t);
	rp = kmem_zalloc(rlen, KM_SLEEP);

	/* fill in the known data */
	rp->req_num = req->req_num;
	rp->msg_type = DR_CRYPTO_STATUS;
	rp->num_records = req->num_records;

	/* stat array for the response */
	stat = DR_CRYPTO_RESP_STATS(rp);

	/* get the status for each of the CPUs */
	for (idx = 0; idx < req->num_records; idx++) {
		stat[idx].cpuid = cpuids[idx];
		ncp_mau_status(ncp, &stat[idx]);
	}

	*resp = rp;
	*resp_len = rlen;
}
