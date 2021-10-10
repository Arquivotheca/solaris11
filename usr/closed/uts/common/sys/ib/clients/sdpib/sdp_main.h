/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_IB_CLIENTS_SDP_MAIN_H
#define	_SYS_IB_CLIENTS_SDP_MAIN_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	_SUN_TPI_VERSION 2

#include <sys/systm.h>
#include <sys/sdt.h>
#include <sys/types.h>
#include <sys/conf.h>
#include <sys/atomic.h>
#include <sys/mutex.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/stream.h>
#include <sys/errno.h>
#include <sys/strlog.h>
#include <sys/ib/ibtl/ibti.h>

#include <inet/common.h>
#include <netinet/ip6.h>
#include <inet/ip.h>
#include <inet/ip6.h>
#include <inet/mi.h>
#include <inet/ip_if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <inet/sdp_itf.h>

#include <sys/ib/clients/sdpib/sdp_types.h>
#include <sys/ib/clients/sdpib/sdp_msgs.h>
#include <sys/ib/clients/sdpib/sdp_inet.h>
#include <sys/ib/clients/sdpib/sdp_link.h>
#include <sys/ib/clients/sdpib/sdp_buff.h>
#include <sys/ib/clients/sdpib/sdp_buff_p.h>
#include <sys/ib/clients/sdpib/sdp_advt.h>
#include <sys/ib/clients/sdpib/sdp_dev.h>
#include <sys/ib/clients/sdpib/sdp_msgs.h>
#include <sys/ib/clients/sdpib/sdp_advt.h>
#include <sys/ib/clients/sdpib/sdp_buff_p.h>
#include <sys/ib/clients/sdpib/sdp_conn.h>
#include <sys/ib/clients/sdpib/sdp_proto.h>
#include <sys/ib/clients/sdpib/sdp_ddi.h>
#include <sys/ib/clients/sdpib/sdp_kstat.h>

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_MAIN_H */
