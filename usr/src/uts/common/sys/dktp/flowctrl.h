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
 * Copyright (c) 1992, 2005, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_DKTP_FLOWCTRL_H
#define	_SYS_DKTP_FLOWCTRL_H

#ifdef	__cplusplus
extern "C" {
#endif

struct	flc_obj {
	opaque_t		flc_data;
	struct flc_objops	*flc_ops;
};

struct	flc_objops {
	int	(*flc_init)(opaque_t, opaque_t, opaque_t, void *);
	int	(*flc_free)(struct flc_obj *);
	int	(*flc_enque)(opaque_t, struct buf *);
	int	(*flc_deque)(opaque_t, struct buf *);
	int	(*flc_start_kstat)(opaque_t, char *, int);
	int	(*flc_stop_kstat)(opaque_t);
	void	*flc_resv[2];
};

struct flc_obj *dsngl_create();
struct flc_obj *dmult_create();
struct flc_obj *duplx_create();
struct flc_obj *adapt_create();

#define	FLC_INIT(X, tgcomobjp, queobjp, lkarg) \
	(*((struct flc_obj *)(X))->flc_ops->flc_init) \
	(((struct flc_obj *)(X))->flc_data, (tgcomobjp), (queobjp), (lkarg))
#define	FLC_FREE(X) (*((struct flc_obj *)(X))->flc_ops->flc_free) ((X))
#define	FLC_ENQUE(X, bp) (*((struct flc_obj *)(X))->flc_ops->flc_enque) \
	(((struct flc_obj *)(X))->flc_data, (bp))
#define	FLC_DEQUE(X, bp) (*((struct flc_obj *)(X))->flc_ops->flc_deque) \
	(((struct flc_obj *)(X))->flc_data, (bp))
#define	FLC_START_KSTAT(X, devtype, instance) \
	(*((struct flc_obj *)(X))->flc_ops->flc_start_kstat)\
	(((struct flc_obj *)(X))->flc_data, (devtype), (instance))
#define	FLC_STOP_KSTAT(X) (*((struct flc_obj *)(X))->flc_ops->flc_stop_kstat) \
	(((struct flc_obj *)(X))->flc_data)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DKTP_FLOWCTRL_H */
