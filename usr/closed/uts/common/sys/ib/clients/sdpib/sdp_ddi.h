/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_IB_CLIENTS_SDP_DDI_H
#define	_SYS_IB_CLIENTS_SDP_DDI_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	SDP_STR_DRVDESC		"SDP IB STREAMS driver"
#define	SDP_STR_MODDESC		"SDP IB STREAMS module"
#define	SDP_STR_MODMTFLAGS	(D_MP | D_MTQPAIR | _D_QNEXTLESS | D_SYNCSTR)

#define	SDP_MINOR_NUMBER	0
#define	SDP_MODULE_ID		5110

#define	SDP_STR_HIWAT		49152	/* set it this way */
#define	SDP_STR_LOWAT		4096

#define	SDP_MODREV_1		1

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_DDI_H */
