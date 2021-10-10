/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/stropts.h>
#include <sys/ib/clients/sdpib/sdp_main.h>
#include <inet/kstatcom.h>

static sdp_named_kstat_t template = {
	{ "sdpActiveOpens",	KSTAT_DATA_UINT64, 0 },
	{ "sdpPassiveOpens",	KSTAT_DATA_UINT64, 0 },
	{ "sdpAttemptFails",	KSTAT_DATA_UINT64, 0 },
	{ "sdpCurrEstab",	KSTAT_DATA_UINT64, 0 },
	{ "sdpConnTableSize",	KSTAT_DATA_UINT64, 0 },
	{ "sdpRejects",		KSTAT_DATA_UINT64, 0 },
	{ "sdpCmFails",		KSTAT_DATA_UINT64, 0 },
	{ "sdpPrFails",		KSTAT_DATA_UINT64, 0 },

	{ "sdpOutDataBytes",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutControl",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutSegs",		KSTAT_DATA_UINT64, 0 },
	{ "sdpOutUrg",		KSTAT_DATA_UINT64, 0 },
	{ "sdpOutSendSm",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutSrcCancel",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutSinkCancel",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutSinkCancelAck", KSTAT_DATA_UINT64, 0 },
	{ "sdpOutAborts",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutResizeAck",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutResize",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutRdmaRdCompl",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutRdmaWrCompl",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutSinkAvail",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutSrcAvail",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutModeChange",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutSuspend",	KSTAT_DATA_UINT64, 0 },
	{ "sdpOutSuspendAck",	KSTAT_DATA_UINT64, 0 },

	{ "sdpInDataBytes",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInControl",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInSegs",		KSTAT_DATA_UINT64, 0 },
	{ "sdpInSendSm",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInSrcCancel",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInSinkCancel",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInSinkCancelAck",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInAborts",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInResizeAck",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInResize",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInRdmaRdCompl",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInRdmaWrCompl",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInSinkAvail",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInSrcAvail",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInModeChange",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInSuspend",	KSTAT_DATA_UINT64, 0 },
	{ "sdpInSuspendAck",	KSTAT_DATA_UINT64, 0 }
};

kstat_t *
sdp_kstat_init(netstackid_t stackid, sdp_named_kstat_t *sdp_statp)
{
	kstat_t *ksp;

	if ((ksp = kstat_create_netstack(SDPIB_STR_NAME, 0, "sdpstat", "net",
	    KSTAT_TYPE_NAMED, sizeof (template) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL, stackid)) != NULL) {
		bcopy(&template, sdp_statp, sizeof (template));
		ksp->ks_data = (void *)sdp_statp;
		ksp->ks_private = (void *)(uintptr_t)stackid;
		kstat_install(ksp);
	}
	return (ksp);
}

void
sdp_kstat_fini(netstackid_t stackid, kstat_t *ksp)
{
	if (ksp != NULL) {
		ASSERT(stackid == (netstackid_t)(uintptr_t)ksp->ks_private);
		kstat_delete_netstack(ksp, stackid);
	}
}
