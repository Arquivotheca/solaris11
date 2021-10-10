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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_AUTH_TYPES_H
#define	_AUTH_TYPES_H

#include <sys/queue.h>

#include <netinet/ip_compat.h>
#include <netinet/ip_fil.h>
#include <netinet/ip_auth.h>

#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	AUTH_PASS,
	AUTH_BLOCK
} auth_pkt_action_t;

typedef enum {
	PKT_IN = 0,
	PKT_OUT
} auth_pkt_dir_t;

typedef enum {
	/*
	 * AUTH_T_FAIL must the smallest
	 */
	AUTH_T_FAIL = -1,
	AUTH_T_NOMATCH,
	AUTH_T_MATCH,
	AUTH_T_LAST
} auth_tresult_t;

/*
 * context for comparing IP addresses
 */
typedef struct ip_test_cx {
	unsigned int	ipcx_v;		/* IP version */
	i6addr_t	ipcx_addr;
	i6addr_t	ipcx_mask;
} ip_test_cx_t;

/*
 * The structures representing rules are private to module, we don't need them
 * in any header file
 */

typedef struct auth_rule	auth_rule_t;
typedef struct auth_test	auth_test_t;

/*
 * auth_test_t represents particular test to perform. Each rule contains a list
 * of tests to match packet.
 */
struct auth_test {
	LIST_ENTRY(auth_test)	at_link; /* linking elm. for list of tests */
	/*
	 * at_func()
	 * pointer to test (match) function takes two arguments:
	 *	auth_test	- pointer to its own context with data needed
	 *			for match
	 *	frauth_t	- pointer to auth request fetched from IPF
	 * Returns: auth_tresult_t
	 */
	auth_tresult_t	(*at_func)(struct auth_test *, frauth_t *);
	/*
	 * at_destroy
	 * pointer to function, which releases (destroys) at_cx member
	 */
	void		(*at_destroy)(struct auth_test *);
	/*
	 * context - pointer for data needed by match function to test packet
	 */
	void		*at_cx;
};

/*
 * auth_action_t represtns particular action to perform on rule match. Each
 * rule can define several actions to take. Rule stores actions in to the list.
 */
typedef struct auth_action {
	LIST_ENTRY(auth_action)	aa_link; /* linking elm. for list of actions */
	/*
	 * aa_perform
	 * performs desired action defined by rule (or rule option), takes two
	 * arguments:
	 *	auth_action	- pointer to action context with data * for
	 *			action
	 *	frauth_t	- pointer to auth request fetched from IPF
	 * Returns: 0 on success, -1 on failure
	 */
	int	(*aa_perform)(struct auth_action *, frauth_t *);
	/*
	 * aa_destroy
	 * pointer to function, which releases (destroys) aa_cx member.
	 */
	void	(*aa_destroy)(struct auth_action *);
	/*
	 * pointer to data needed by action
	 */
	void	*aa_cx;
} auth_action_t;

/*
 * auth_rule_t is rule itself. Auth rules are stored in global list. Each rule
 * contains list of match parameters and list of actions to perform on packet
 * match.
 */
struct auth_rule {
	LIST_ENTRY(auth_rule)			ar_link;
	LIST_HEAD(test_list, auth_test)		ar_tests;
	LIST_HEAD(action_list, auth_action)	ar_actions;
};

#ifdef __cplusplus
}
#endif

#endif	/* _AUTH_TYPES_H */
