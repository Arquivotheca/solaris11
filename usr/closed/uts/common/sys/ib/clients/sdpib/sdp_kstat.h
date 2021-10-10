/*
 * Copyright (c) 2007, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_IB_CLIENTS_SDP_KSTAT_H
#define	_SYS_IB_CLIENTS_SDP_KSTAT_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SDP statistics.
 */
typedef struct sdp_named_kstat {
	kstat_named_t	ActiveOpens;
	kstat_named_t	PassiveOpens;
	kstat_named_t	AttemptFails;
	kstat_named_t	CurrEstab;
	kstat_named_t	ConnTableSize;
	kstat_named_t	Rejects;
	kstat_named_t	CmFails;
	kstat_named_t	PrFails;

	kstat_named_t	OutDataBytes;
	kstat_named_t	OutControl;
	kstat_named_t	OutSegs;
	kstat_named_t	OutUrg;
	kstat_named_t	OutSendSm;
	kstat_named_t	OutSrcCancel;
	kstat_named_t	OutSinkCancel;
	kstat_named_t	OutSinkCancelAck;
	kstat_named_t	OutAborts;
	kstat_named_t	OutResizeAck;
	kstat_named_t	OutResize;
	kstat_named_t	OutRdmaRdCompl;
	kstat_named_t	OutRdmaWrCompl;
	kstat_named_t	OutSinkAvail;
	kstat_named_t	OutSrcAvail;
	kstat_named_t	OutModeChange;
	kstat_named_t	OutSuspend;
	kstat_named_t	OutSuspendAck;

	kstat_named_t	InDataBytes;
	kstat_named_t	InControl;
	kstat_named_t	InSegs;
	kstat_named_t	InSendSm;
	kstat_named_t	InSrcCancel;
	kstat_named_t	InSinkCancel;
	kstat_named_t	InSinkCancelAck;
	kstat_named_t	InAborts;
	kstat_named_t	InResizeAck;
	kstat_named_t	InResize;
	kstat_named_t	InRdmaRdCompl;
	kstat_named_t	InRdmaWrCompl;
	kstat_named_t	InSinkAvail;
	kstat_named_t	InSrcAvail;
	kstat_named_t	InModeChange;
	kstat_named_t	InSuspend;
	kstat_named_t	InSuspendAck;

} sdp_named_kstat_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_KSTAT_H */
