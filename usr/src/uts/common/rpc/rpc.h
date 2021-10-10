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
 * Copyright (c) 1989, 2005, Oracle and/or its affiliates. All rights reserved.
 */
/* Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */
/*
 * Portions of this source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 */

/*
 * rpc.h, Just includes the billions of rpc header files necessary to
 * do remote procedure calling.
 *
 */

#ifndef _RPC_RPC_H
#define	_RPC_RPC_H

#include <rpc/types.h>		/* some typedefs */

#ifndef _KERNEL
#include <tiuser.h>
#include <fcntl.h>
#include <memory.h>
#else
#include <sys/tiuser.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <sys/t_kuser.h>
#endif

#include <rpc/xdr.h>		/* generic (de)serializer */
#include <rpc/auth.h>		/* generic authenticator (client side) */
#include <rpc/clnt.h>		/* generic client side rpc */

#include <rpc/rpc_msg.h>	/* protocol for rpc messages */
#include <rpc/auth_sys.h>	/* protocol for unix style cred */
#include <rpc/auth_des.h>	/* protocol for des style cred */
#include <sys/socket.h>		/* generic socket info */
#include <rpc/rpcsec_gss.h>	/* GSS style security */

#include <rpc/svc.h>		/* service manager and multiplexer */
#include <rpc/svc_auth.h>	/* service side authenticator */

#ifndef _KERNEL
#ifndef _RPCB_PROT_H_RPCGEN	/* Don't include before rpcb_prot defined */
#include <rpc/rpcb_clnt.h>	/* rpcbind interface functions */
#endif
#include <rpc/svc_mt.h>		/* private server definitions */
#endif

#endif	/* !_RPC_RPC_H */
