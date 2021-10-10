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

#include "common.h"

#include <inet/ilb.h>
#include <inet/ilb/ilb_impl.h>
#include <inet/ilb/ilb_stack.h>
#include <inet/ilb/ilb_nat.h>
#include <inet/ilb/ilb_conn.h>

int
ilb_stacks_walk_step(mdb_walk_state_t *wsp)
{
	return (ns_walk_step(wsp, NS_ILB));
}

int
ilb_rules_walk_init(mdb_walk_state_t *wsp)
{
	ilb_stack_t ilbs;

	if (wsp->walk_addr == NULL)
		return (WALK_ERR);

	if (mdb_vread(&ilbs, sizeof (ilbs), wsp->walk_addr) == -1) {
		mdb_warn("failed to read ilb_stack_t at %p", wsp->walk_addr);
		return (WALK_ERR);
	}
	if ((wsp->walk_addr = (uintptr_t)ilbs.ilbs_rule_head) != NULL)
		return (WALK_NEXT);
	else
		return (WALK_DONE);
}

int
ilb_rules_walk_step(mdb_walk_state_t *wsp)
{
	ilb_rule_t rule;
	int status;

	if (mdb_vread(&rule, sizeof (rule), wsp->walk_addr) == -1) {
		mdb_warn("failed to read ilb_rule_t at %p", wsp->walk_addr);
		return (WALK_ERR);
	}
	status = wsp->walk_callback(wsp->walk_addr, &rule, wsp->walk_cbdata);
	if (status != WALK_NEXT)
		return (status);
	if ((wsp->walk_addr = (uintptr_t)rule.ir_next) == NULL)
		return (WALK_DONE);
	else
		return (WALK_NEXT);
}

int
ilb_servers_walk_init(mdb_walk_state_t *wsp)
{
	ilb_rule_t rule;

	if (wsp->walk_addr == NULL)
		return (WALK_ERR);

	if (mdb_vread(&rule, sizeof (rule), wsp->walk_addr) == -1) {
		mdb_warn("failed to read ilb_rule_t at %p", wsp->walk_addr);
		return (WALK_ERR);
	}
	if ((wsp->walk_addr = (uintptr_t)rule.ir_servers) != NULL)
		return (WALK_NEXT);
	else
		return (WALK_DONE);
}

int
ilb_servers_walk_step(mdb_walk_state_t *wsp)
{
	ilb_server_t server;
	int status;

	if (mdb_vread(&server, sizeof (server), wsp->walk_addr) == -1) {
		mdb_warn("failed to read ilb_server_t at %p", wsp->walk_addr);
		return (WALK_ERR);
	}
	status = wsp->walk_callback(wsp->walk_addr, &server, wsp->walk_cbdata);
	if (status != WALK_NEXT)
		return (status);
	if ((wsp->walk_addr = (uintptr_t)server.iser_next) == NULL)
		return (WALK_DONE);
	else
		return (WALK_NEXT);
}

/*
 * Helper structure for ilb_nat_src walker.  It stores the current index of the
 * nat src table.
 */
typedef struct {
	ilb_stack_t ilbs;
	int idx;
} ilb_walk_t;

/* Copy from list.c */
#define	list_object(a, node)	((void *)(((char *)node) - (a)->list_offset))

int
ilb_nat_src_walk_init(mdb_walk_state_t *wsp)
{
	int i;
	ilb_walk_t *ns_walk;
	ilb_nat_src_entry_t *entry = NULL;

	if (wsp->walk_addr == NULL)
		return (WALK_ERR);

	ns_walk = mdb_alloc(sizeof (ilb_walk_t), UM_SLEEP);
	if (mdb_vread(&ns_walk->ilbs, sizeof (ns_walk->ilbs),
	    wsp->walk_addr) == -1) {
		mdb_warn("failed to read ilb_stack_t at %p", wsp->walk_addr);
		mdb_free(ns_walk, sizeof (ilb_walk_t));
		return (WALK_ERR);
	}

	if (ns_walk->ilbs.ilbs_nat_src == NULL) {
		mdb_free(ns_walk, sizeof (ilb_walk_t));
		return (WALK_DONE);
	}

	wsp->walk_data = ns_walk;
	for (i = 0; i < ns_walk->ilbs.ilbs_nat_src_hash_size; i++) {
		list_t head;
		char  *khead;

		/* Read in the nsh_head in the i-th element of the array. */
		khead = (char *)ns_walk->ilbs.ilbs_nat_src + i *
		    sizeof (ilb_nat_src_hash_t);
		if (mdb_vread(&head, sizeof (list_t), (uintptr_t)khead) == -1) {
			mdb_warn("failed to read ilbs_nat_src at %p\n", khead);
			return (WALK_ERR);
		}

		/*
		 * Note that list_next points to a kernel address and we need
		 * to compare list_next with the kernel address of the list
		 * head.  So we need to calculate the address manually.
		 */
		if ((char *)head.list_head.list_next != khead +
		    offsetof(list_t, list_head)) {
			entry = list_object(&head, head.list_head.list_next);
			break;
		}
	}

	if (entry == NULL)
		return (WALK_DONE);

	wsp->walk_addr = (uintptr_t)entry;
	ns_walk->idx = i;
	return (WALK_NEXT);
}

int
ilb_nat_src_walk_step(mdb_walk_state_t *wsp)
{
	int status;
	ilb_nat_src_entry_t entry, *next_entry;
	ilb_walk_t *ns_walk;
	ilb_stack_t *ilbs;
	list_t head;
	char *khead;
	int i;

	if (mdb_vread(&entry, sizeof (ilb_nat_src_entry_t),
	    wsp->walk_addr) == -1) {
		mdb_warn("failed to read ilb_nat_src_entry_t at %p",
		    wsp->walk_addr);
		return (WALK_ERR);
	}
	status = wsp->walk_callback(wsp->walk_addr, &entry, wsp->walk_cbdata);
	if (status != WALK_NEXT)
		return (status);

	ns_walk = (ilb_walk_t *)wsp->walk_data;
	ilbs = &ns_walk->ilbs;
	i = ns_walk->idx;

	/* Read in the nsh_head in the i-th element of the array. */
	khead = (char *)ilbs->ilbs_nat_src + i * sizeof (ilb_nat_src_hash_t);
	if (mdb_vread(&head, sizeof (list_t), (uintptr_t)khead) == -1) {
		mdb_warn("failed to read ilbs_nat_src at %p\n", khead);
		return (WALK_ERR);
	}

	/*
	 * Check if there is still entry in the current list.
	 *
	 * Note that list_next points to a kernel address and we need to
	 * compare list_next with the kernel address of the list head.
	 * So we need to calculate the address manually.
	 */
	if ((char *)entry.nse_link.list_next != khead + offsetof(list_t,
	    list_head)) {
		wsp->walk_addr = (uintptr_t)list_object(&head,
		    entry.nse_link.list_next);
		return (WALK_NEXT);
	}

	/* Start with the next bucket in the array. */
	next_entry = NULL;
	for (i++; i < ilbs->ilbs_nat_src_hash_size; i++) {
		khead = (char *)ilbs->ilbs_nat_src + i *
		    sizeof (ilb_nat_src_hash_t);
		if (mdb_vread(&head, sizeof (list_t), (uintptr_t)khead) == -1) {
			mdb_warn("failed to read ilbs_nat_src at %p\n", khead);
			return (WALK_ERR);
		}

		if ((char *)head.list_head.list_next != khead +
		    offsetof(list_t, list_head)) {
			next_entry = list_object(&head,
			    head.list_head.list_next);
			break;
		}
	}

	if (next_entry == NULL)
		return (WALK_DONE);

	wsp->walk_addr = (uintptr_t)next_entry;
	ns_walk->idx = i;
	return (WALK_NEXT);
}

void
ilb_common_walk_fini(mdb_walk_state_t *wsp)
{
	ilb_walk_t *walk;

	walk = (ilb_walk_t *)wsp->walk_data;
	if (walk == NULL)
		return;
	mdb_free(walk, sizeof (ilb_walk_t *));
}

int
ilb_conn_walk_init(mdb_walk_state_t *wsp)
{
	int i;
	ilb_walk_t *conn_walk;
	ilb_conn_hash_t head;

	if (wsp->walk_addr == NULL)
		return (WALK_ERR);

	conn_walk = mdb_alloc(sizeof (ilb_walk_t), UM_SLEEP);
	if (mdb_vread(&conn_walk->ilbs, sizeof (conn_walk->ilbs),
	    wsp->walk_addr) == -1) {
		mdb_warn("failed to read ilb_stack_t at %p", wsp->walk_addr);
		mdb_free(conn_walk, sizeof (ilb_walk_t));
		return (WALK_ERR);
	}

	if (conn_walk->ilbs.ilbs_c2s_conn_hash == NULL) {
		mdb_free(conn_walk, sizeof (ilb_walk_t));
		return (WALK_DONE);
	}

	wsp->walk_data = conn_walk;
	for (i = 0; i < conn_walk->ilbs.ilbs_conn_hash_size; i++) {
		char *khead;

		/* Read in the nsh_head in the i-th element of the array. */
		khead = (char *)conn_walk->ilbs.ilbs_c2s_conn_hash + i *
		    sizeof (ilb_conn_hash_t);
		if (mdb_vread(&head, sizeof (ilb_conn_hash_t),
		    (uintptr_t)khead) == -1) {
			mdb_warn("failed to read ilbs_c2s_conn_hash at %p\n",
			    khead);
			return (WALK_ERR);
		}

		if (head.ilb_connp != NULL)
			break;
	}

	if (head.ilb_connp == NULL)
		return (WALK_DONE);

	wsp->walk_addr = (uintptr_t)head.ilb_connp;
	conn_walk->idx = i;
	return (WALK_NEXT);
}

int
ilb_conn_walk_step(mdb_walk_state_t *wsp)
{
	int status;
	ilb_conn_t conn;
	ilb_walk_t *conn_walk;
	ilb_stack_t *ilbs;
	ilb_conn_hash_t head;
	char *khead;
	int i;

	if (mdb_vread(&conn, sizeof (ilb_conn_t), wsp->walk_addr) == -1) {
		mdb_warn("failed to read ilb_conn_t at %p", wsp->walk_addr);
		return (WALK_ERR);
	}

	status = wsp->walk_callback(wsp->walk_addr, &conn, wsp->walk_cbdata);
	if (status != WALK_NEXT)
		return (status);

	conn_walk = (ilb_walk_t *)wsp->walk_data;
	ilbs = &conn_walk->ilbs;
	i = conn_walk->idx;

	/* Check if there is still entry in the current list. */
	if (conn.conn_c2s_next != NULL) {
		wsp->walk_addr = (uintptr_t)conn.conn_c2s_next;
		return (WALK_NEXT);
	}

	/* Start with the next bucket in the array. */
	for (i++; i < ilbs->ilbs_conn_hash_size; i++) {
		khead = (char *)ilbs->ilbs_c2s_conn_hash + i *
		    sizeof (ilb_conn_hash_t);
		if (mdb_vread(&head, sizeof (ilb_conn_hash_t),
		    (uintptr_t)khead) == -1) {
			mdb_warn("failed to read ilbs_c2s_conn_hash at %p\n",
			    khead);
			return (WALK_ERR);
		}

		if (head.ilb_connp != NULL)
			break;
	}

	if (head.ilb_connp == NULL)
		return (WALK_DONE);

	wsp->walk_addr = (uintptr_t)head.ilb_connp;
	conn_walk->idx = i;
	return (WALK_NEXT);
}

int
ilb_sticky_walk_init(mdb_walk_state_t *wsp)
{
	int i;
	ilb_walk_t *sticky_walk;
	ilb_sticky_t *st = NULL;

	if (wsp->walk_addr == NULL)
		return (WALK_ERR);

	sticky_walk = mdb_alloc(sizeof (ilb_walk_t), UM_SLEEP);
	if (mdb_vread(&sticky_walk->ilbs, sizeof (sticky_walk->ilbs),
	    wsp->walk_addr) == -1) {
		mdb_warn("failed to read ilb_stack_t at %p", wsp->walk_addr);
		mdb_free(sticky_walk, sizeof (ilb_walk_t));
		return (WALK_ERR);
	}

	if (sticky_walk->ilbs.ilbs_sticky_hash == NULL) {
		mdb_free(sticky_walk, sizeof (ilb_walk_t));
		return (WALK_DONE);
	}

	wsp->walk_data = sticky_walk;
	for (i = 0; i < sticky_walk->ilbs.ilbs_sticky_hash_size; i++) {
		list_t head;
		char *khead;

		/* Read in the nsh_head in the i-th element of the array. */
		khead = (char *)sticky_walk->ilbs.ilbs_sticky_hash + i *
		    sizeof (ilb_sticky_hash_t);
		if (mdb_vread(&head, sizeof (list_t), (uintptr_t)khead) == -1) {
			mdb_warn("failed to read ilbs_sticky_hash at %p\n",
			    khead);
			return (WALK_ERR);
		}

		/*
		 * Note that list_next points to a kernel address and we need
		 * to compare list_next with the kernel address of the list
		 * head.  So we need to calculate the address manually.
		 */
		if ((char *)head.list_head.list_next != khead +
		    offsetof(list_t, list_head)) {
			st = list_object(&head, head.list_head.list_next);
			break;
		}
	}

	if (st == NULL)
		return (WALK_DONE);

	wsp->walk_addr = (uintptr_t)st;
	sticky_walk->idx = i;
	return (WALK_NEXT);
}

int
ilb_sticky_walk_step(mdb_walk_state_t *wsp)
{
	int status;
	ilb_sticky_t st, *st_next;
	ilb_walk_t *sticky_walk;
	ilb_stack_t *ilbs;
	list_t head;
	char *khead;
	int i;

	if (mdb_vread(&st, sizeof (ilb_sticky_t), wsp->walk_addr) == -1) {
		mdb_warn("failed to read ilb_sticky_t at %p", wsp->walk_addr);
		return (WALK_ERR);
	}

	status = wsp->walk_callback(wsp->walk_addr, &st, wsp->walk_cbdata);
	if (status != WALK_NEXT)
		return (status);

	sticky_walk = (ilb_walk_t *)wsp->walk_data;
	ilbs = &sticky_walk->ilbs;
	i = sticky_walk->idx;

	/* Read in the nsh_head in the i-th element of the array. */
	khead = (char *)ilbs->ilbs_sticky_hash + i * sizeof (ilb_sticky_hash_t);
	if (mdb_vread(&head, sizeof (list_t), (uintptr_t)khead) == -1) {
		mdb_warn("failed to read ilbs_sticky_hash at %p\n", khead);
		return (WALK_ERR);
	}

	/*
	 * Check if there is still entry in the current list.
	 *
	 * Note that list_next points to a kernel address and we need to
	 * compare list_next with the kernel address of the list head.
	 * So we need to calculate the address manually.
	 */
	if ((char *)st.list.list_next != khead + offsetof(list_t,
	    list_head)) {
		wsp->walk_addr = (uintptr_t)list_object(&head,
		    st.list.list_next);
		return (WALK_NEXT);
	}

	/* Start with the next bucket in the array. */
	st_next = NULL;
	for (i++; i < ilbs->ilbs_nat_src_hash_size; i++) {
		khead = (char *)ilbs->ilbs_sticky_hash + i *
		    sizeof (ilb_sticky_hash_t);
		if (mdb_vread(&head, sizeof (list_t), (uintptr_t)khead) == -1) {
			mdb_warn("failed to read ilbs_sticky_hash at %p\n",
			    khead);
			return (WALK_ERR);
		}

		if ((char *)head.list_head.list_next != khead +
		    offsetof(list_t, list_head)) {
			st_next = list_object(&head,
			    head.list_head.list_next);
			break;
		}
	}

	if (st_next == NULL)
		return (WALK_DONE);

	wsp->walk_addr = (uintptr_t)st_next;
	sticky_walk->idx = i;
	return (WALK_NEXT);
}
