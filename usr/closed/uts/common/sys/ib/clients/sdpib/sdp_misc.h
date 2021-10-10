/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * LEGAL NOTICE
 *
 * This file contains source code that implements the Sockets Direct
 * Protocol (SDP) as defined by the InfiniBand Architecture Specification,
 * Volume 1, Annex A4, Version 1.1.  Due to restrictions in the SDP license,
 * source code contained in this file may not be distributed outside of
 * Sun Microsystems without further legal review to ensure compliance with
 * the license terms.
 *
 * Sun employees and contactors are cautioned not to extract source code
 * from this file and use it for other purposes.  The SDP implementation
 * code in this and other files must be kept separate from all other source
 * code.
 *
 * As required by the license, the following notice is added to the source
 * code:
 *
 * This source code may incorporate intellectual property owned by
 * Microsoft Corporation.  Our provision of this source code does not
 * include any licenses or any other rights to you under any Microsoft
 * intellectual property.  If you would like a license from Microsoft
 * (e.g., to rebrand, redistribute), you need to contact Microsoft
 * directly.
 */

#ifndef _SYS_IB_CLIENTS_SDP_MISC_H
#define	_SYS_IB_CLIENTS_SDP_MISC_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SDP connection inline functions
 */

/*
 * Writable space on send side.
 */
static int32_t
sdp_inet_write_space(sdp_conn_t *conn, int32_t urg)
{
	int32_t size;

	SDP_CHECK_NULL(conn, EINVAL);

	/*
	 * Currently queued bytes
	 */
	size = (conn->sdpc_tx_max_queue_size - conn->sdpc_tx_bytes_queued);

	/*
	 * More buffers can be queued only if the bytes queued are
	 * less than SDP_DEV_INET_THRSH or if there is urgent data.
	 */
	return ((SDP_DEV_INET_THRSH < size || urg) ? size : 0);
}	/* sdp_inet_write_space */
#pragma inline(sdp_inet_write_space)

/*
 * sdp_inet_writable - return non-zero if socket is writable
 */
static int
sdp_inet_writable(sdp_conn_t *conn)
{
	if (conn->sdpc_tx_max_queue_size > 0)
		return (sdp_inet_write_space(conn, 0) <
		    (conn->sdpc_tx_bytes_queued / 2)) ? 0 : 1;
	else
		return (0);
}
#pragma inline(sdp_inet_writable)

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_MISC_H */
