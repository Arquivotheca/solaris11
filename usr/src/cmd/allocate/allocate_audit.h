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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ALLOCATE_AUDIT_H
#define	_ALLOCATE_AUDIT_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <bsm/adt.h>
#include <bsm/adt_event.h>

/* Default authorizations */

#define	AUDIT_DEV_ALLOC_AUTH	    0x1
#define	AUDIT_DEVICE_REVOKE_AUTH    0x2

/* Audit event data */

typedef struct ae_ticket {
	au_event_t	event_type;
	uint32_t	auths;
	char		*auth_list_s;
	char		*auth_list_f;
	char		*dev_path;
	struct stat64	dev_attr;
	char		*dev_list;
	char		*zonename;
	uid_t		uid;
} ae_ticket_t;

/* Audit event data setters/getter */

#define	AUDIT_EVENT_GET_AUTHS(ae)	    ((ae).auths)
#define	AUDIT_EVENT_SET_DEV(ae, dpath)	    ((ae).dev_path = (dpath))
#define	AUDIT_EVENT_SET_DEVLIST(ae, dlist)  ((ae).dev_list = (dlist))

/* Audit setup functions */

void audit_init(char *, char *, uid_t);
void audit_finish(void);
void audit_set_auth(uint32_t);

/* Audit event functions */

void audit_event_init(au_event_t, ae_ticket_t *);
void audit_event_submit(ae_ticket_t *, int);
void audit_event_set_dev_attr(ae_ticket_t *);
void audit_event_set_auth_list(ae_ticket_t *, const char *, boolean_t);
void audit_event_reset_auth_lists(ae_ticket_t *);
void audit_event_set_def_auth(ae_ticket_t *, uint32_t);

#ifdef	__cplusplus
}
#endif

#endif /* _ALLOCATE_AUDIT_H */
