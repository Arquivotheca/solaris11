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

/*
 * Sun elects to include this software in this distribution under the
 * OpenIB.org BSD license
 *
 *
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
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
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#ifndef _SYS_IB_CLIENTS_SDP_TRACE_UTIL_H
#define	_SYS_IB_CLIENTS_SDP_TRACE_UTIL_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Strlog levels.  the higher the value, the less important the message.
 */
typedef enum {
	T_VERY_TERSE = 0x1,
	T_TERSE = 0x2,
	T_VERBOSE = 0x3,
	T_VERY_VERBOSE = 0x4,
	T_SCREAM = 0x5,
	T_MAX = 0x6
} t_ts_trace_level;

#define	TRACE_FLOW_INOUT	SL_TRACE
#define	TRACE_FLOW_INIT		SL_TRACE
#define	TRACE_FLOW_CLEANUP	SL_TRACE
#define	TRACE_FLOW_FATAL	SL_FATAL
#define	TRACE_FLOW_CONFIG	SL_NOTIFY | SL_CONSOLE
#define	TRACE_FLOW_WARN		SL_WARN

#define	SDP_DBG		7
#define	SDP_DATA	6
#define	SDP_CTRL	5
#define	SDP_INIT	4
#define	SDP_CONT	3
#define	SDP_WARN	2
#define	SDP_ERR		1


/* debugging */
#undef  dprint
#ifdef DEBUG
extern int sdpdebug;
#define	dprint(level, args)	{ if (sdpdebug > (level)) printf args; }

#define	SDP_PR(level, type, conn, format, arg...) \
	do { \
		sdp_conn_t *ptr = (conn); \
		_NOTE(CONSTCOND) \
		if (ptr) { \
			if (level < sdpdebug) { \
				printf("%s: [%p][%04x:%d]" format "\n", \
					type, (void *)ptr, ptr->state, \
					ptr->sdp_ib_refcnt, arg);  \
			} \
		} \
		else {  \
			if (level < sdpdebug) { \
				printf("%s: " format "\n", \
					type, arg);  \
			} \
		} \
_NOTE(CONSTCOND) } while (0)

#define	SDP_PRINT(level, type, format, arg...) \
	do { \
		if (level < sdpdebug) { \
			printf("%s: " format "\n", \
				type, arg);  \
		} \
_NOTE(CONSTCOND) } while (0)

#define	sdp_print_dbg(conn, format, arg...) \
	SDP_PR(SDP_DBG, "DBG", conn, format, arg)
#define	sdp_print_data(conn, format, arg...) \
	SDP_PR(SDP_DATA, "DATA", conn, format, arg)
#define	sdp_print_ctrl(conn, format, arg...) \
	SDP_PR(SDP_CTRL, "CTRL", conn, format, arg)
#define	sdp_print_init(conn, format, arg...) \
	SDP_PR(SDP_INIT, "INIT", conn, format, arg)
#define	sdp_print_note(conn, format, arg...) \
	SDP_PR(SDP_CONT, "NOTE", conn, format, arg)
#define	sdp_print_warn(conn, format, arg...) \
	SDP_PR(SDP_WARN, "WARN", conn, format, arg)
#define	sdp_print_err(conn, format, arg...) \
	SDP_PR(SDP_ERR, "ERR", conn, format, arg)
#define	sdp_note(format, arg...) \
	SDP_PRINT(SDP_CONT, "NOTE", format, arg)
#define	sdp_warn(format, arg...) \
	SDP_PRINT(SDP_WARN, "WARN", format, arg)

#else
#define	dprint(level, args) {}

#define	sdp_print_dbg(conn, format, arg...) do { } while (0)
#define	sdp_print_data(conn, format, arg...) do { } while (0)
#define	sdp_print_ctrl(conn, format, arg...) do { } while (0)
#define	sdp_print_init(conn, format, arg...) do { } while (0)
#define	sdp_print_note(conn, format, arg...) do { } while (0)
#define	sdp_print_warn(conn, format, arg...) do { } while (0)
#define	sdp_print_err(conn, format, arg...) do { } while (0)
#define	sdp_warn(format, arg...) do { } while (0)
#define	sdp_note(format, arg...) do { } while (0)

#endif


#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_TRACE_UTIL_H */
