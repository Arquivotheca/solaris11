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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_NCALL_MODULE_H
#define	_NCALL_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL

#define	NCALL_MODULE_VER	4

typedef struct ncall_module_s {
	int ncall_version;
	char *ncall_name;

	int (*ncall_stop)(void);

	void (*ncall_register_svc)(int, void (*)(ncall_t *, int *));
	void (*ncall_unregister_svc)(int);

	int  (*ncall_nodeid)(char *);
	char *(*ncall_nodename)(int);
	int  (*ncall_mirror)(int);
	int  (*ncall_self)(void);

	int  (*ncall_alloc)(int, int, int, ncall_t **);
	int  (*ncall_timedsend)(ncall_t *, int, int, struct timeval *,
	    va_list);
	int  (*ncall_timedsendnotify)(ncall_t *, int, int, struct timeval *,
	    void (*)(ncall_t *, void *), void *, va_list);
	int  (*ncall_broadcast)(ncall_t *, int, int, struct timeval *,
	    va_list);
	int  (*ncall_read_reply)(ncall_t *, int, va_list);
	void (*ncall_reset)(ncall_t *);
	void (*ncall_free)(ncall_t *);

	int  (*ncall_put_data)(ncall_t *, void *, int);
	int  (*ncall_get_data)(ncall_t *, void *, int);

	int  (*ncall_sender)(ncall_t *);
	void (*ncall_reply)(ncall_t *, va_list);
	void (*ncall_pend)(ncall_t *);
	void (*ncall_done)(ncall_t *);

	int  (*ncall_ping)(char *, int *);
	int  (*ncall_maxnodes)(void);
	int  (*ncall_nextnode)(void **);
	int  (*ncall_errcode)(ncall_t *, int *);
} ncall_module_t;

extern int ncall_register_module(ncall_module_t *, ncall_node_t *);
extern int ncall_unregister_module(ncall_module_t *);

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _NCALL_MODULE_H */
