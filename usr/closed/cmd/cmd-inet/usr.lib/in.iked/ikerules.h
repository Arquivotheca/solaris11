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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_IKED_CONFIG_IKERULES_H
#define	_IKED_CONFIG_IKERULES_H

#include <net/pfkeyv2.h>
#include <ucred.h>
#include <sys/tsol/tndb.h>
#include <sys/tsol/label_macro.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct ike_xform {
	uint8_t encr_alg;
	uint8_t auth_alg;
	uint8_t oakley_group;
	uint16_t encr_low_bits;
	uint16_t encr_high_bits;
	SshIkeAttributeAuthMethValues auth_method;
	unsigned p1_lifetime_secs;
};

struct ike_addrrange {
	union {
		struct sockaddr_storage ss;
		struct sockaddr_in v4;
		struct sockaddr_in6 v6;
	} beginaddr, endaddr;
};

struct ike_addrspec {
	struct ike_addrrange **includes;
	int num_includes;
};

/*
 * One IKE rule, which can be referenced in one or more rulebases.  It has a
 * reference count so that it can hang off of multiple rulebases corresponding
 * to multiple phase-I's, some of which may have been cloned from different
 * current rulebases.  A reference from a rulebase to a rule is represented in
 * refcount, but the reference from phase1_t.p1_rule is not represented in
 * refcount because it is always the same as one of the rules in
 * phase1_t.p1_rulebase.
 */
struct ike_rule {
	int refcount;
	char *label;
	uint32_t cookie;
	struct ike_addrspec local_addr;
	struct ike_addrspec remote_addr;
	struct certlib_certspec local_id;
	struct certlib_certspec remote_id;
	SshIkeIpsecIdentificationType local_idtype;
	SshIkeExchangeType mode;
	struct ike_xform **xforms;
	int num_xforms;
	int p2_pfs;
	unsigned p1_nonce_len;
	boolean_t p1_multi_label;
	boolean_t p1_single_label;
	boolean_t p1_override_label;
	boolean_t p1_implicit_label;
	/*
	 * NOTE: when changing the types of lifetimes, be sure to make
	 *	 appropriate changes in defs.h as well (e.g. MAX_P2_LIFETIME).
	 */
	unsigned p2_lifetime_secs;
	unsigned p2_softlife_secs;
	unsigned p2_idletime_secs;
	unsigned p2_lifetime_kb;
	unsigned p2_softlife_kb;
	unsigned p2_nonce_len;
	boolean_t label_set;
	bslabel_t *outer_bslabel;
	sadb_sens_t *outer_label;
	ucred_t *outer_ucred;
};

/*
 * A rulebase (ordered list) of IKE rules.  There is a "current" rulebase, the
 * global variable "rules", maintained by the config module, and a rulebase
 * affiliated with each phase-I, whether it is in-progress or established.
 * The current rulebase represents the rules defined in the ike/config file
 * (plus any dead rules), and each phase-I gets a clone of the current rulebase
 * when it first comes into existence.  Rules get discarded from the phase-I's
 * rulebase as they are determined not to match the proposed/negotiated
 * phase-I parameters.
 */
struct ike_rulebase {
	struct ike_rule **rules;
	int num_rules;
	int allocnum_rules;
};

extern struct certlib_certspec requested_certs;
extern struct certlib_certspec root_certs;
extern struct certlib_certspec trusted_certs;
extern struct ike_rulebase rules;	/* current rulebase */

extern void rule_free(struct ike_rule *);
extern struct ike_rule *rule_dup(struct ike_rule *);

extern int rulebase_add(struct ike_rulebase *, struct ike_rule *);

extern int config_load(const char *, int, boolean_t);
extern int config_update(const char *, int);

extern boolean_t label_already(struct ike_rule *);

#ifdef	__cplusplus
}
#endif

#endif	/* _IKED_CONFIG_IKERULES_H */
