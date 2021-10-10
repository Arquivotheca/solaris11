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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

#ifndef _YP_B_H
#define	_YP_B_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This is a private header file included by the yp commands and yp client
 * code in libnsl.  The commands and the libraries differ in fields in some
 * of the structures.
 */

#include <rpc/types.h>
#include <paths.h>
#include <netconfig.h>
#include <stdio.h>
#include <synch.h>
#include <netdb.h>

extern bool_t xdr_netconfig(XDR *, struct netconfig *);

#define	TEMP_BINDING	_PATH_SYSVOL "/daemon/ypbind"
#define	PERM_BINDING	"/var/yp/binding"
#define	BINDING		TEMP_BINDING

#define	YPSETNONE 0
#define	YPSETLOCAL 3
#define	YPSETALL 5

struct dom_binding {
	struct dom_binding *dom_pnext;
	char *dom_domain;
	struct ypbind_binding *dom_binding;
	CLIENT *dom_client;
#ifndef YPCMDS
	int cache_bad;
	int fd;		/* fd in dom_client */
	dev_t rdev;	/* device id of fd */
	int ref_count;	/* number of threads using this structure */
	int need_free;	/* if true, this structure needs to be freed */
	mutex_t server_name_lock;    /* protects server name in dom_binding */
#endif /* YPCMDS */
};

/* Following structure is used only by ypbind */

struct domain {
	struct domain *dom_pnext;
	char	*dom_name;
	bool_t dom_boundp;
	unsigned short dom_vers;	/* only YPVERS */
	unsigned int	dom_error;
	CLIENT * ping_clnt;
	struct ypbind_binding *dom_binding;
	int	dom_report_success;	/* Controls msg to /dev/console */
	int	dom_broadcaster_pid;
	int	bindfile;		/* File with binding info in it */
	int 	broadcaster_fd;
	FILE    *broadcaster_pipe;	/* to get answer from locater */
	XDR	broadcaster_xdr;	/* xdr for pipe */
	struct timeval lastping;	/* info to avoid a ping storm */
#ifdef YPCMDS
	FILE	*cache_fp;		/* file pointer opened on cache_file */
	char	*cache_file;		/* cached version of server info */
#endif
};

enum ypbind_resptype {
	YPBIND_SUCC_VAL = 1,
	YPBIND_FAIL_VAL = 2
};
typedef enum ypbind_resptype ypbind_resptype;
extern bool_t xdr_ypbind_resptype(XDR *, ypbind_resptype *);
#define	YPBIND_ERR_ERR 1		/* Internal error */
#define	YPBIND_ERR_NOSERV 2		/* No bound server for passed domain */
#define	YPBIND_ERR_RESC 3		/* System resource allocation failure */
#define	YPBIND_ERR_NODOMAIN 4		/* Domain doesn't exist */

/* Following struct is used only by ypwhich and yppoll */

struct ypbind_domain {
	char *ypbind_domainname;
	rpcvers_t ypbind_vers;
};
typedef struct ypbind_domain ypbind_domain;
extern bool_t xdr_ypbind_domain(XDR *, ypbind_domain *);

/*
 * This structure is used to store information about the server
 * Returned by ypbind to the libnsl/yp clients to contact ypserv.
 * Also used by ypxfr.
 */

struct ypbind_binding {
	struct netconfig *ypbind_nconf;
	struct netbuf *ypbind_svcaddr;
	char *ypbind_servername;
	rpcvers_t ypbind_hi_vers;
	rpcvers_t ypbind_lo_vers;
};
typedef struct ypbind_binding ypbind_binding;
extern bool_t xdr_ypbind_binding(XDR *, ypbind_binding *);

struct ypbind_resp {
	ypbind_resptype ypbind_status;
	union {
		uint_t ypbind_error;
		struct ypbind_binding *ypbind_bindinfo;
	} ypbind_resp_u;
};
typedef struct ypbind_resp ypbind_resp;
extern bool_t xdr_ypbind_resp(XDR *, ypbind_resp *);

struct ypbind_setdom {
	char *ypsetdom_domain;
	struct ypbind_binding *ypsetdom_bindinfo;
};
typedef struct ypbind_setdom ypbind_setdom;
extern bool_t xdr_ypbind_setdom(XDR *, ypbind_setdom *);

#define	YPBINDPROG ((rpcprog_t)100007)
#define	YPBINDVERS ((rpcvers_t)3)
#define	YPBINDPROC_NULL ((rpcproc_t)0)
extern void *ypbindproc_null_3();
#define	YPBINDPROC_DOMAIN ((rpcproc_t)1)
extern ypbind_resp *ypbindproc_domain_3();
#define	YPBINDPROC_SETDOM ((rpcproc_t)2)
extern void *ypbindproc_setdom_3();

extern struct timeval _ypserv_timeout;
extern unsigned int _ypsleeptime;

extern int __yp_dobind(char *, struct dom_binding **);
extern int __yp_dobind_rsvdport(char *, struct dom_binding **);
extern void free_dom_binding(struct dom_binding *);
extern CLIENT *__yp_clnt_create_rsvdport(const char *, rpcprog_t,
    rpcvers_t, const char *, const uint_t, const uint_t);
extern void __yp_rel_binding(struct dom_binding *);
extern CLIENT *__clnt_create_loopback(rpcprog_t, rpcvers_t, int *);

#ifdef YPCMDS
/*
 * Definitions compiled and edited from yp.x
 * These structures are added here to
 * support binary compatibility with static
 * apps that use the old ypbind protocol.
 * These structures are lifted from
 * 4.x source lib/libc/yp/yp_prot.h
 * and renamed with a suffix _2 to avoid
 * conflicts with similar structs for
 * native ypbind protocol, as above.
 * Only needed for the YP commands.
 */

typedef char *domainname_2;

struct ypbind_binding_2 {
	struct in_addr ypbind_binding_addr; /* In network order */
	unsigned short int ypbind_binding_port; /* In network order */
};
typedef struct ypbind_binding_2 ypbind_binding_2;

struct ypbind_resp_2 {
	ypbind_resptype ypbind_status;
	union {
		unsigned long ypbind_error;
		ypbind_binding_2 ypbind_bindinfo;
	} ypbind_respbody_2;
};
typedef struct ypbind_resp_2 ypbind_resp_2;

struct ypbind_setdom_2 {
	char ypsetdom_domain[YPMAXDOMAIN + 1];
	ypbind_binding_2 ypsetdom_binding;
	unsigned short ypsetdom_vers;
};
typedef struct ypbind_setdom_2 ypbind_setdom_2;

/*
 * ypbind V2 and ypbind V1 differ only in the "set domain"
 * procedure, which we don't support.
 */
#define	YPBINDVERS_2 ((unsigned long)(2))
#define	YPBINDVERS_1 ((unsigned long)(1))
extern  ypbind_resp_2 * ypbindproc_domain_2();
extern int ypbindprog_2_freeresult();

/* the xdr functions */
extern bool_t xdr_ypbind_binding_2();
extern bool_t xdr_ypbind_resp_2();

#endif /* YPCMDS */

#ifdef __cplusplus
}
#endif

#endif /* _YP_B_H */
