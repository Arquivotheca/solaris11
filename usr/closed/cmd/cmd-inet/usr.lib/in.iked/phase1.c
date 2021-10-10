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
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <netinet/in.h>
#include <net/pfkeyv2.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <time.h>
#include <locale.h>
#include <stdio.h>
#include <strings.h>

#include <ipsec_util.h>

#include "defs.h"
#include "getssh.h"
#include <ike/isakmp_internal.h>

phase1_t *phase1_head;

/* HACK - More SSH madness... */
#undef snprintf

/*
 * Quick-n-dirty sockaddr printer.  Uses static storage...
 */
const char *
sap(const struct sockaddr_storage *sap)
{
	static char buf[INET6_ADDRSTRLEN + 32];
	uchar_t tbuf[INET6_ADDRSTRLEN + IFNAMSIZ + 2];
	struct sockaddr_in *sap_in = (struct sockaddr_in *)sap;

	if (sockaddr_to_string(sap, tbuf)) {
		(void) snprintf(buf, INET6_ADDRSTRLEN + 32, "%s[%d]", tbuf,
		    ntohs(sap_in->sin_port));
		return (buf);
	} else {
		return (gettext("inet_ntop() failure printing sockaddr"));
	}
}


/*
 * addrspec printer with 2 internal buffers for laziness/conciseness of use
 * (no free or local variables required).  For debug use only.
 */
static const char *
asp(const struct ike_addrspec *asp)
{
	static char buf[3 * INET6_ADDRSTRLEN];
	char *this = buf;
	int i;

	if (asp->num_includes < 1)
		return ("NONE");

	this[0] = 0;
	for (i = 0; i < asp->num_includes; ++i) {
		if (asp->includes[i]->beginaddr.ss.ss_family !=
		    asp->includes[i]->endaddr.ss.ss_family ||
		    memcmp(&asp->includes[i]->beginaddr,
		    &asp->includes[i]->endaddr,
		    sizeof (asp->includes[i]->endaddr)) == 0) {
			(void) strcat(this,
			    sap(&asp->includes[i]->beginaddr.ss));
		} else {
			(void) strcat(this,
			    sap(&asp->includes[i]->beginaddr.ss));
			(void) strcat(this, "-");
			(void) strcat(this, sap(&asp->includes[i]->endaddr.ss));
		}
	}
	this[strlen(this)] = 0;

	return (this);
}

/*
 * Check to see if addr falls in the range defined by beg and end.  Assume
 * that end >= beg.  Return true if it does, false if not.
 *
 * This define is from /usr/include/netinet/in.h; it's only defined there
 * for the kernel, but is awfully convenient in user-space as well.
 */

#define	s6_addr32	_S6_un._S6_u32
static boolean_t
in6_addr_is_in_range(in6_addr_t *addr, in6_addr_t *beg, in6_addr_t *end)
{
	/* first check for a match at the boundaries */
	if (IN6_ARE_ADDR_EQUAL(addr, beg) || IN6_ARE_ADDR_EQUAL(addr, end))
		return (B_TRUE);

	/* otherwise, have to start doing some compares... */
	return (in6_addr_cmp(beg, addr) && in6_addr_cmp(addr, end));
}

static boolean_t
address_match(struct sockaddr_storage *sap, struct ike_addrspec *asp)
{
	int i;
	uint32_t v4addr, b, e, scope_id, bsid, esid;
	in6_addr_t v6addr, b6, e6;
	struct ike_addrrange *arp;
	boolean_t isv4 = B_FALSE;

	if (sap->ss_family == AF_INET) {
		v4addr = ntohl(((struct sockaddr_in *)sap)->sin_addr.s_addr);
		isv4 = B_TRUE;
	} else if (sap->ss_family == AF_INET6) {
		v6addr = ((struct sockaddr_in6 *)sap)->sin6_addr;
		scope_id = ((struct sockaddr_in6 *)sap)->sin6_scope_id;
	} else {
		return (B_FALSE);
	}

	for (i = 0; i < asp->num_includes; ++i) {
		arp = asp->includes[i];
		if (isv4) {
			if (arp->beginaddr.ss.ss_family != AF_INET)
				return (B_FALSE);

			b = ntohl(arp->beginaddr.v4.sin_addr.s_addr);
			e = ntohl(arp->endaddr.v4.sin_addr.s_addr);
			/*
			 * Note: this test assumes that in the case of a
			 * single address, that address is stored as both
			 * the beginning and end of the range.
			 */
			if (v4addr >= b && v4addr <= e)
				return (B_TRUE);
		} else {
			if (arp->beginaddr.ss.ss_family != AF_INET6)
				return (B_FALSE);
			b6 = arp->beginaddr.v6.sin6_addr;
			e6 = arp->endaddr.v6.sin6_addr;

			bsid = arp->beginaddr.v6.sin6_scope_id;
			esid = arp->endaddr.v6.sin6_scope_id;
			if (IN6_IS_ADDR_LINKLOCAL(&b6) &&
			    IN6_IS_ADDR_LINKLOCAL(&e6) &&
			    IN6_IS_ADDR_LINKLOCAL(&v6addr) &&
			    scope_id != 0 &&
			    ((bsid != 0 && scope_id != bsid) ||
			    (esid != 0 && scope_id != esid)))
				return (B_FALSE);

			if (in6_addr_is_in_range(&v6addr, &b6, &e6))
				return (B_TRUE);
		}
	}

	return (B_FALSE);
}


static void
free_phase1(phase1_t *deadman)
{
	parsedmsg_t *pmsg;
	int ri;				/* rule index */
	/* TODO: use a finer-grained errno */
	int err = (deadman->p1_dpd_status == DPD_FAILURE) ? ETIMEDOUT : EPERM;

	/* Free the samsg(s) now.   Send down negative ACQUIREs! */
	while (deadman->p1_pmsg != NULL) {
		pmsg = deadman->p1_pmsg;
		deadman->p1_pmsg = pmsg->pmsg_next;
		send_negative_acquire(pmsg, err);
		free_pmsg(pmsg);
	}

	for (ri = 0; ri < deadman->p1_rulebase.num_rules; ++ri)
		if (deadman->p1_rulebase.rules[ri] != NULL)
			rule_free(deadman->p1_rulebase.rules[ri]);
	ssh_free(deadman->p1_rulebase.rules);

	ssh_free(deadman->p1_localid);
	ssh_free(deadman->p1_remoteid);
	ssh_free(deadman);
}

/*
 * Called from ssh_policy_isakmp_sa_freed() and handle_flush().  Nuke a local
 * phase1 structure when the library says the negotiation is going away, or we
 * get a FLUSH message from PF_KEY.  The aforementioned policy.c file is just
 * a wrapper to this, but the policy.c function may be used someday for other
 * tasks beyond this.
 */
void
delete_phase1(phase1_t *dp, boolean_t notify_library)
{
	SshIkePMPhaseI	pm_info;
	ike_server_t	*ikesrv;
	int rc;

	if (dp == NULL)
		return;

	/* remove this entry from the global linked list... */
	if (dp->p1_ptpn != NULL) {
		UNLINK_PHASE1(dp);
	}

	pm_info = dp->p1_pminfo;
	if (pm_info != NULL && notify_library) {
		/*
		 * this is really the best place to decrement the counter for
		 * current SAs; however, it can be called both for SAs that
		 * were never successfully established as well as for SAs that
		 * were.  We only want to decrement the counter for the latter;
		 * hence this klugy test.
		 */
		if (get_ssh_p1state(pm_info->negotiation) >=
		    IKE_SA_STATE_DONE) {
			if (pm_info->this_end_is_initiator)
				ikestats.st_init_p1_current--;
			else
				ikestats.st_resp_p1_current--;
		}

		PRTDBG(D_P1, ("Deleting Phase 1 SA and notifying peer..."));

		/*
		 * This function is specified so that it could return failure,
		 * but the implementation does not currently.  Still, print a
		 * log message in case things change.
		 */
		rc = ssh_ike_remove_isakmp_sa(pm_info->negotiation,
		    SSH_IKE_REMOVE_FLAGS_SEND_DELETE |
		    SSH_IKE_REMOVE_FLAGS_FORCE_DELETE_NOW);
		if (rc != SSH_IKE_ERROR_OK) {
			PRTDBG(D_P1, ("  Removing P1 SA returned %d (%s).",
			    rc, ike_connect_error_to_string(rc)));
		}

		/*
		 * The rest of this gets called by the callback initiated
		 * by our ssh_ike_remove_isakmp_sa().
		 */
		return;
	}

	ikesrv = get_server_context(&dp->p1_local);
	if ((ikesrv != NULL) && dp->p1_create_phase2) {
		addrcache_delete(&ikesrv->ikesrv_addrcache, &dp->p1_remote);
	}

	/* Don't let the library free us, let us do it! */
	free_phase1(dp);

	/* unlink from the policy manager structure */
	if (pm_info != NULL)
		pm_info->policy_manager_data = NULL;
}

/* Abbreviation macros. Also used in check_rule(). */
#define	HARD_seconds	selected_rule->p2_lifetime_secs
#define	HARD_kilobytes	selected_rule->p2_lifetime_kb
#define	SOFT_seconds	selected_rule->p2_softlife_secs
#define	SOFT_kilobytes	selected_rule->p2_softlife_kb
#define	IDLE_seconds	selected_rule->p2_idletime_secs

/* Print lifetimes in ike_rule structure. */
static void
print_rule_lifetimes(struct ike_rule *selected_rule, const char *str) {
	char byte_str[BYTE_STR_SIZE]; /* byte lifetime string representation */
	char secs_str[SECS_STR_SIZE]; /* lifetime string representation */

	/*
	 * Perform explicit conversion of kilobyte values before passing to
	 * bytecnt2out() to avoid integer overflow.
	 */
	PRTDBG(D_POL, ("%s: HARD: %7u s %s, %10u KB %s",
	    str, HARD_seconds,
	    secs2out(HARD_seconds, secs_str, sizeof (secs_str), B_FALSE),
	    HARD_kilobytes,
	    bytecnt2out((uint64_t)(HARD_kilobytes) << 10, byte_str,
	    sizeof (byte_str), B_FALSE)));
	PRTDBG(D_POL, ("%*s  SOFT: %7u s %s, %10u KB %s",
	    (int)strlen(str), "", SOFT_seconds,
	    secs2out(SOFT_seconds, secs_str, sizeof (secs_str), B_FALSE),
	    SOFT_kilobytes,
	    bytecnt2out((uint64_t)(SOFT_kilobytes) << 10, byte_str,
	    sizeof (byte_str), B_FALSE)));
	PRTDBG(D_POL, ("%*s  IDLE: %7u s %s", (int)strlen(str), "",
	    IDLE_seconds,
	    secs2out(IDLE_seconds, secs_str, sizeof (secs_str), B_FALSE)));
}

/*
 * Sanity check values in a rule and adjust them as needed. This is used for
 * local configuration checking and also to unify local configuration with
 * values proposed by the peer.
 *
 * The members of the resulting structure have to comply to the following rules:
 *   Enforced:
 *     p2_lifetime_secs >= MIN_P2_LIFETIME_HARD_SECS
 *     p2_softlife_secs >= MIN_P2_LIFETIME_SOFT_SECS
 *     p2_lifetime_secs <= MAX_P2_LIFETIME_SECS
 *     p2_softlife_secs <= MAX_P2_LIFETIME_SECS
 *     p2_idletime_secs >= MIN_P2_LIFETIME_IDLE_SECS
 *     p2_idletime_secs <  p2_softlife_secs
 *     p2_softlife_kb   <= p2_lifetime_kb
 *     p2_softlife_secs <= p2_lifetime_secs
 *     p2_lifetime_kb   >= MIN_P2_LIFETIME_HARD_KB
 *     p2_softlife_kb   >= MIN_P2_LIFETIME_SOFT_KB
 *     p2_lifetime_kb   <= MAX_P2_LIFETIME_KB
 *     p2_softlife_kb   <= MAX_P2_LIFETIME_KB
 *     if (p2_lifetime_secs != p2_softlife_secs)
 *         diff(p2_lifetime_secs, p2_softlife_secs) >= MINDIFF_SECS
 *     diff(p2_softlife_secs, p2_idletime_secs) >= MINDIFF_SECS
 *     if (p2_lifetime_secs != p2_softlife_secs)
 *         diff(p2_lifetime_kb, p2_softlife_kb) >= MINDIFF_KB
 *   Derived (not actively enforced):
 *     p2_softlife_kb   ~= SECS2KBYTES(p2_softlife_secs)
 *     p2_lifetime_kb   ~= SECS2KBYTES(p2_lifetime_secs)
 *   Derived + warning:
 *     p2_softlife_secs ~= 90% of p2_lifetime_secs [not enforced]
 *     p2_softlife_kb   ~= 90% of p2_lifetime_kb [not enforced]
 *
 * If we find that some values do not match some of the rules it might
 * result in recomputation of different values.
 *
 * No matter how much the input structure is incoherent, this function must
 * always return correctly formed structure (in terms of the above
 * expressions).
 *
 * Some basic ideas:
 *   - HARD lifetimes are more important than SOFT lifetimes
 *   - the proportions between lifetimes expressed in the same units
 *     are more important than proportions between HARD and SOFT in different
 *     units
 *
 * The strategy for single pass is such that lifetimes expressed in seconds
 * are checked first (first stage) and kilobyte lifetimes after that (second
 * stage). In second stage, only change kilobyte lifetimes according to the
 * lifetimes expressed in seconds.
 *
 * If cfgtest is set, some of the warning messages will make it to the log.
 */
void
check_rule(struct ike_rule *selected_rule, boolean_t cfgtest)
{
	char byte_str[BYTE_STR_SIZE]; /* byte lifetime string representation */
	uint32_t log_level = D_POL; /* default log level for warnings */
	char *warn_prefix = "WARNING: ";
	char *rulename = NULL;

/*
 * Adjust low value which is too close to high value so that it is at least
 * mindiff away.
 */
#define	TOO_CLOSE(high, high_str, low, low_str, mindiff, units)	\
	if (high - low < mindiff) {				\
		PRTDBG(D_POL, ("%s lifetime too close to "	\
		    "%s lifetime", low_str, high_str));		\
		PRTDBG(D_POL, ("  %s lifetime: %u %s",		\
		    high_str, (high), units));			\
		PRTDBG(D_POL, ("  %s lifetime: %u %s",		\
		    low_str, (low), units));			\
		PRTDBG(D_POL, ("  minimum difference = "	\
		    "%u %s", mindiff, units));			\
		(low) = (high) - mindiff;			\
		PRTDBG(D_POL, ("  -> Scaling down %s lifetime "	\
		    "to: %u %s", low_str, (low), units));	\
	}

	if (selected_rule->label == NULL) {
		rulename = "global rule";
	} else {
		rulename = selected_rule->label;
	}

	PRTDBG(D_POL, ("Checking p2 lifetimes in rule \"%s\"", rulename));

	/* Print the input structure. */
	print_rule_lifetimes(selected_rule, "In ");

	/*
	 * If we are checking the configuration set the log level so some
	 * of the messages appear in the log.
	 */
	if (cfgtest)
		log_level = D_OP;

	/* FIRST STAGE: check lifetimes expressed in seconds only. */

	/* Check HARD lifetime (seconds) */
	if (HARD_seconds == 0) {
		HARD_seconds = DEF_P2_LIFETIME_HARD;
		PRTDBG(D_POL, ("Using default value for HARD lifetime: "
		    "%u seconds", HARD_seconds));
	}
	if (HARD_seconds < MIN_P2_LIFETIME_HARD_SECS) {
		PRTDBG(log_level, ("%sProblem in rule \"%s\":",
		    warn_prefix, rulename));
		PRTDBG(log_level, (" HARD lifetime too small (%u < %u)",
		    HARD_seconds, MIN_P2_LIFETIME_HARD_SECS));
		PRTDBG(log_level, ("  -> Using %u seconds (minimum)",
		    MIN_P2_LIFETIME_HARD_SECS));
		HARD_seconds = MIN_P2_LIFETIME_HARD_SECS;
	}
	if (HARD_seconds > MAX_P2_LIFETIME_SECS) {
		PRTDBG(log_level, ("%sProblem in rule \"%s\":",
		    warn_prefix, rulename));
		PRTDBG(log_level, (" HARD lifetime too big (%u > %u)",
		    HARD_seconds, MAX_P2_LIFETIME_SECS));
		PRTDBG(log_level, ("  -> Using %u seconds (maximum)",
		    MAX_P2_LIFETIME_SECS));
		HARD_seconds = MAX_P2_LIFETIME_SECS;
	}

	/* Check SOFT lifetime (seconds). */
	if (SOFT_seconds != 0 &&
	    SOFT_seconds < MIN_P2_LIFETIME_SOFT_SECS) {
		PRTDBG(D_POL, ("SOFT lifetime too small (%u < %u)",
		    SOFT_seconds, MIN_P2_LIFETIME_SOFT_SECS));
		PRTDBG(D_POL, ("  -> Using %u seconds (minimum)",
		    MIN_P2_LIFETIME_SOFT_SECS));
		SOFT_seconds = MIN_P2_LIFETIME_SOFT_SECS;
	}
	if (SOFT_seconds > MAX_P2_LIFETIME_SECS) {
		PRTDBG(D_POL, ("SOFT lifetime too big (%u > %u)",
		    SOFT_seconds, MAX_P2_LIFETIME_SECS));
		PRTDBG(D_POL, ("  -> Using %u seconds (maximum)",
		    MAX_P2_LIFETIME_SECS));
		/*
		 * If HARD was also bigger than MAX_P2_LIFETIME_SECS then SOFT
		 * is scaled down according to HARD (do not set it to
		 * MAX_P2_LIFETIME_SECS because this would disable SOFT
		 * expires).
		 * If only SOFT was bigger than MAX_P2_LIFETIME_SECS then this
		 * is most probably invalid configuration so it will be scaled
		 * according to the supplied HARD value or default HARD value.
		 */
		SOFT_seconds = MAX_P2_LIFETIME_SECS;
	}

	/*
	 * Do the same sanity checking on the SOFT expire lifetime. If the
	 * user specifies a value then use this, but if its too close to
	 * the HARD lifetime value, or zero, use a default value. If the
	 * user specified the SOFT lifetime to be the same as the HARD
	 * lifetime then treat this as a special case, this effectively
	 * disables SOFT expires.
	 */
	if (SOFT_seconds == HARD_seconds) {
		PRTDBG(D_POL, ("HARD lifetime == SOFT lifetime == %u secs"
		    " (no SOFT expires)", SOFT_seconds));
	} else {
		if (SOFT_seconds == 0) {
			PRTDBG(D_POL, ("SOFT lifetime not set"));
			SOFT_seconds = DEF_P2_LIFETIME_SOFT;
			PRTDBG(D_POL, ("  -> Using default value for SOFT "
			    "lifetime: %u seconds", SOFT_seconds));
		}
		if (SOFT_seconds > HARD_seconds) {
			PRTDBG(D_POL, ("SOFT lifetime bigger than "
			    "HARD lifetime (%u > %u)", SOFT_seconds,
			    HARD_seconds));
			DEFSOFT(SOFT_seconds, HARD_seconds);
			PRTDBG(D_POL, ("  -> SOFT derived from HARD "
			    "lifetime: %u seconds", SOFT_seconds));
		}

		/* SOFT should not be too close to HARD. */
		TOO_CLOSE(HARD_seconds, "HARD", SOFT_seconds, "SOFT",
		    MINDIFF_SECS, "seconds");
	}

	/* Start IDLE lifetime checking by comparing to minimum. */
	if ((IDLE_seconds != 0) && (IDLE_seconds < MIN_P2_LIFETIME_IDLE_SECS)) {
		PRTDBG(log_level, ("%sProblem in rule \"%s\":",
		    warn_prefix, rulename));
		PRTDBG(log_level, (" IDLE lifetime too small (%u < %u)",
		    IDLE_seconds, MIN_P2_LIFETIME_IDLE_SECS));
		PRTDBG(log_level, ("  -> Using %u seconds",
		    MIN_P2_LIFETIME_IDLE_SECS));
		IDLE_seconds = MIN_P2_LIFETIME_IDLE_SECS;
	}

	/*
	 * IDLE time must never be greater than SOFT lifetime because we
	 * do not want to run DPD on DYING SAs (that is, after SOFT expiration).
	 * This is because normally we actively renegotiate the SAs.
	 */
	if (IDLE_seconds == 0 || (IDLE_seconds > SOFT_seconds)) {
		PRTDBG(D_POL, ("IDLE lifetime %s",
		    (IDLE_seconds == 0) ? "not set" :
		    "bigger than SOFT lifetime"));
		PRTDBG(D_POL, ("  IDLE lifetime: %u seconds",
		    IDLE_seconds));
		PRTDBG(D_POL, ("  SOFT lifetime: %u seconds",
		    SOFT_seconds));
		if (IDLE_seconds == 0) {
			PRTDBG(D_POL, ("  -> Using default value for IDLE "
			    "(derived from SOFT lifetime)"));
		} else {
			PRTDBG(D_POL, ("  -> Scaling down IDLE lifetime "
			    "according to SOFT lifetime."));
		}
		DEFIDLE(IDLE_seconds, SOFT_seconds);
		PRTDBG(D_POL, ("  -> IDLE lifetime: %u seconds",
		    IDLE_seconds));
	}

	/* IDLE should not be too close to SOFT. */
	TOO_CLOSE(SOFT_seconds, "SOFT", IDLE_seconds, "IDLE",
	    MINDIFF_SECS, "seconds");

	/* SECOND STAGE: from now on check kilobyte lifetime values only. */

	/* Check HARD kilobyte lifetime for default value. */
	if (HARD_kilobytes == 0) {
		DEFKBYTES(HARD_kilobytes, HARD_seconds);
		SOFT_kilobytes = 0;
		PRTDBG(D_POL, ("Using default value for HARD kilobyte "
		    "lifetime: %u KB", HARD_kilobytes));
		PRTDBG(D_POL, ("  Resetting SOFT kilobyte lifetime"));
	}

	/*
	 * NOTE: the checks for too small/big values should be done only
	 *	 after the above check since big values of HARD_seconds
	 *	 can lead to HARD_kilobytes bigger than the maximum.
	 */

	/* Check HARD kilobyte lifetime for too small values. */
	if (HARD_kilobytes < MIN_P2_LIFETIME_HARD_KB) {
		PRTDBG(log_level, ("%sProblem in rule \"%s\":",
		    warn_prefix, rulename));
		PRTDBG(log_level, (" HARD kilobyte lifetime too small "
		    "(%u < %u)", HARD_kilobytes, MIN_P2_LIFETIME_HARD_KB));
		PRTDBG(log_level, ("  -> Using %u kilobytes (minimum)",
		    MIN_P2_LIFETIME_HARD_KB));
		HARD_kilobytes = MIN_P2_LIFETIME_HARD_KB;
	}
	/*
	 * HARD kilobyte lifetime should not be too big (cca 2^(blksize/2))
	 * for CBC ciphers) because of possible data leaking due to collisions.
	 */
	if (HARD_kilobytes > MAX_P2_LIFETIME_KB) {
		PRTDBG(log_level, ("%sProblem in rule \"%s\":",
		    warn_prefix, rulename));
		PRTDBG(log_level, (" HARD KB lifetime too big (%u > %u)",
		    HARD_kilobytes, MAX_P2_LIFETIME_KB));
		PRTDBG(log_level, ("  -> Using %u kilobytes (maximum)",
		    MAX_P2_LIFETIME_KB));
		HARD_kilobytes = MAX_P2_LIFETIME_KB;
	}

	/* Check SOFT kilobyte lifetime for too small values. */
	if ((SOFT_kilobytes != 0) && (SOFT_kilobytes
	    < MIN_P2_LIFETIME_SOFT_KB)) {
		PRTDBG(log_level, ("%sProblem in rule \"%s\":",
		    warn_prefix, rulename));
		PRTDBG(log_level, (" SOFT kilobyte lifetime too small "
		    "(%u < %u)", SOFT_kilobytes, MIN_P2_LIFETIME_SOFT_KB));
		PRTDBG(log_level, ("  -> Resetting"));
		SOFT_kilobytes = 0;
	}

	/* Disable SOFT expires */
	if (HARD_seconds == SOFT_seconds) {
		SOFT_kilobytes = HARD_kilobytes;
	} else {
		/* Check SOFT kilobyte lifetime for default value. */
		if (SOFT_kilobytes == 0) {
			/*
			 * Use the conversion from HARD kilobyte lifetime if
			 * it was set by the user in order to keep the
			 * proportions between kilobyte lifetimes.
			 * It is more important to keep the proportions between
			 * SOFT and HARD lifetimes (both seconds and kilobytes)
			 * than between seconds and kilobytes lifetimes. If HARD
			 * kilobyte lifetime was 0 (default), SOFT will be
			 * derived from HARD lifetime set according to the HARD
			 * lifetime in seconds; if HARD kilobyte lifetime was
			 * specified, SOFT kilobyte lifetime will be derived
			 * from it - in both cases the proportions will be fine.
			 */
			if (HARD_kilobytes !=
			    SECS2KBYTES(DEF_P2_LIFETIME_HARD)) {
				DEFSOFT(SOFT_kilobytes, HARD_kilobytes);
				PRTDBG(D_POL, ("Using SOFT KB lifetime "
				    "derived from HARD KB: %u KB",
				    SOFT_kilobytes));
			} else {
				DEFKBYTES(SOFT_kilobytes, SOFT_seconds);
				PRTDBG(D_POL, ("Using default value for SOFT "
				    "KB lifetime: %u KB",
				    SOFT_kilobytes));
			}
		}

		/*
		 * Check that SOFT kilobyte lifetime <= HARD kilobyte lifetime.
		 * Only the equality of HARD/SOFT lifetimes expressed in
		 * seconds is the driver for disabling SOFT expires.
		 */
		if (SOFT_kilobytes > HARD_kilobytes) {
			PRTDBG(log_level, ("%sProblem in rule \"%s\":",
			    warn_prefix, rulename));
			PRTDBG(log_level, (" SOFT KB lifetime > HARD"
			    " KB lifetime"));
			PRTDBG(log_level, ("  -> Deriving SOFT KB "
			    "from HARD KB lifetime"));
			/*
			 * HARD is more important in such case (e.g.
			 * when peer proposed smaller HARD kilobyte
			 * lifetime than local config).
			 */
			DEFSOFT(SOFT_kilobytes, HARD_kilobytes);
		}

		/*
		 * Check that HARD kilobyte lifetime is not too close
		 * to SOFT kilobyte lifetime.
		 */
		TOO_CLOSE(HARD_kilobytes, "HARD", SOFT_kilobytes, "SOFT",
		    MINDIFF_KB, "kilobytes");
	}

	/* THIRD STAGE: print warnings (if any) and report the results. */

	/*
	 * If SOFT is far from HARD issue a warning this is probably not
	 * what is desired but do not enforce it.
	 */
	if ((SOFT_seconds < HARD_seconds / 2) ||
	    (SOFT_kilobytes < HARD_kilobytes / 2)) {
		PRTDBG(D_POL, ("SOFT lifetime less than 50%% or HARD"
		    " lifetime."));
		PRTDBG(D_POL, ("  Leaving as is."));
	}

	/* Check the secure threshold. This is mostly for CBC ciphers. */
	if (HARD_kilobytes > P2_LIFETIME_KB_THRESHOLD) {
		PRTDBG(log_level, ("%sPossible problem in rule \"%s\":",
		    warn_prefix, rulename));
		PRTDBG(log_level, (" HARD KB lifetime bigger than secure "
		    "threshold for CBC ciphers"));
		PRTDBG(log_level, (" This might only be an issue if CBC"
		    "cipher is selected."));
		PRTDBG(log_level, ("  HARD     : %u %s", HARD_kilobytes,
		    bytecnt2out((uint64_t)HARD_kilobytes << 10, byte_str,
		    sizeof (byte_str), B_FALSE)));
		PRTDBG(log_level, ("  Threshold: %u %s",
		    P2_LIFETIME_KB_THRESHOLD,
		    bytecnt2out((uint64_t)P2_LIFETIME_KB_THRESHOLD << 10,
		    byte_str, sizeof (byte_str), B_FALSE)));
	}

	/* Print the resulting structure. */
	print_rule_lifetimes(selected_rule, "Out");

	/*
	 * Catch the bugs in the act (verify the enforced set of the
	 * expressions above).
	 */
	assert(HARD_seconds >= MIN_P2_LIFETIME_HARD_SECS);
	assert(SOFT_seconds >= MIN_P2_LIFETIME_SOFT_SECS);
	assert(HARD_seconds <= MAX_P2_LIFETIME_SECS);
	assert(SOFT_seconds <= MAX_P2_LIFETIME_SECS);
	assert(HARD_kilobytes >= MIN_P2_LIFETIME_HARD_KB);
	assert(SOFT_kilobytes >= MIN_P2_LIFETIME_SOFT_KB);
	assert(HARD_kilobytes <= MAX_P2_LIFETIME_KB);
	assert(SOFT_kilobytes <= MAX_P2_LIFETIME_KB);
	assert(IDLE_seconds >= MIN_P2_LIFETIME_IDLE_SECS);
	assert(IDLE_seconds < SOFT_seconds);
	assert(SOFT_kilobytes   <= HARD_kilobytes);
	assert(SOFT_seconds <= HARD_seconds);
	if (HARD_seconds != SOFT_seconds)
		assert(HARD_seconds - SOFT_seconds >= MINDIFF_SECS);
	assert(SOFT_seconds - IDLE_seconds >= MINDIFF_SECS);
	if (HARD_seconds != SOFT_seconds)
		assert((HARD_kilobytes - SOFT_kilobytes) >= MINDIFF_KB);
}

#undef TOO_CLOSE
#undef HARD_seconds
#undef HARD_kilobytes
#undef SOFT_seconds
#undef SOFT_kilobytes
#undef IDLE_seconds

/*
 * Update the rule and global ike_defs structure with lifetime values
 * from the last *samsg sent up as an ACQUIRE message by the kernel.
 */
void
update_defs(sadb_msg_t *samsg, struct ike_rule *selected_rule)
{
	uint64_t *current, *end;
	struct sadb_x_ecomb *ecomb = NULL;
	boolean_t updated_secs = B_FALSE;
	boolean_t updated_kb = B_FALSE;
	struct ike_rule default_rule;
	char *deflabel = "Default rule";

	end = ((uint64_t *)(samsg)) + samsg->sadb_msg_len;
	current = (uint64_t *)(samsg + 1);

	while (current < end) {
		sadb_ext_t *ext = (struct sadb_ext *)current;
		if (ext->sadb_ext_type == SADB_X_EXT_EPROP) {
			struct sadb_prop *eprop = (struct sadb_prop *)current;
			ecomb = (struct sadb_x_ecomb *)(eprop + 1);
			break;

		}
		current += ext->sadb_ext_len;
	}

	if (ecomb == NULL)
		return;

	/*
	 * Update the rule with sensible values taken from the ACQUIRE
	 * message, unless they were specified in the IKE configuration
	 * in which case leave well alone (modulo SOFT lifetimes which also
	 * get updated if HARD is updated). Values set to 0 will get
	 * recalculated by check_rule(selected_rule).
	 */

	/* Save default values in a ike_rule structure for easy comparison. */
	bzero(&default_rule, sizeof (default_rule));
	default_rule.label = deflabel;
	default_rule.p2_lifetime_secs = DEF_P2_LIFETIME_HARD;
	default_rule.p2_softlife_secs = DEF_P2_LIFETIME_SOFT;
	DEFIDLE(default_rule.p2_idletime_secs, default_rule.p2_softlife_secs);
	DEFKBYTES(default_rule.p2_lifetime_kb, default_rule.p2_lifetime_secs);
	DEFKBYTES(default_rule.p2_softlife_kb, default_rule.p2_softlife_secs);
	PRTDBG(D_POL, ("Constructing rule with default values"));
	print_rule_lifetimes(&default_rule, "Default");

	/*
	 * Update the rule even if ACQUIRE lifetimes are out of
	 * boundaries, check_rule() will do the right thing.
	 */
#define	INSIDE_BOUNDARIES(x, min, max)	(((x) >= (min)) && ((x) <= (max)))
#define	UPDATE_HARD(rule, dflt, msg, min, max, flag, units)		\
	{								\
		if (!INSIDE_BOUNDARIES(msg, min, max)) 			\
			PRTDBG(D_POL, ("ACQUIRE p2 lifetime outside "	\
			    "boundaries (%" PRIu64 " %s ! <%u, %u>)",	\
			    msg, units, min, max));			\
		rule = (uint32_t)(msg);					\
		PRTDBG(D_POL, ("Updating p2 lifetime to %u %s"		\
		    " from ACQUIRE", rule, units));			\
		flag = B_TRUE;						\
	}
#define	UPDATE_SOFT(rule, hard, dflt, msg, min, max, flag, units)	\
	{								\
		if ((msg != 0) && !INSIDE_BOUNDARIES(msg, min, max))	\
			PRTDBG(D_POL, ("ACQUIRE p2 softlife outside "	\
			    "boundaries (%" PRIu64 " %s ! <%u, %u>)",	\
			    msg, units, min, max));			\
		if ((msg) != 0) {					\
			rule = (uint32_t)(msg);				\
			PRTDBG(D_POL, ("Updating p2 softlife to %u %s"	\
			    " from ACQUIRE", rule, units));		\
		} else if (flag) {					\
			/*						\
			 * If HARD was updated from ACQUIRE derive SOFT	\
			 * from HARD.					\
			 */						\
			DEFSOFT(rule, hard);				\
			PRTDBG(D_POL, ("Updating p2 softlife to %u %s"	\
			    " (derived from HARD)", rule, units));	\
		}							\
	}
#define	SENSIBLE_HARD(rule, dflt, msg, min, max, flag, units)		\
	if ((rule == dflt) && (msg != 0ULL))				\
		UPDATE_HARD(rule, dflt, msg, min, max, flag, units)
#define	SENSIBLE_SOFT(rule, hard, dflt, msg, min, max, flag, units)	\
	if (flag || (rule == dflt))					\
		UPDATE_SOFT(rule, hard, dflt, msg, min, max, flag, units)

	PRTDBG(D_POL, ("Unifying ACQUIRE lifetimes with rule '%s'",
	    selected_rule->label));

	SENSIBLE_HARD(selected_rule->p2_lifetime_secs,
	    default_rule.p2_lifetime_secs,
	    ecomb->sadb_x_ecomb_hard_addtime,
	    MIN_P2_LIFETIME_HARD_SECS, MAX_P2_LIFETIME_SECS,
	    updated_secs, "seconds");

	SENSIBLE_SOFT(selected_rule->p2_softlife_secs,
	    selected_rule->p2_lifetime_secs,
	    default_rule.p2_softlife_secs,
	    ecomb->sadb_x_ecomb_soft_addtime,
	    MIN_P2_LIFETIME_SOFT_SECS, MAX_P2_LIFETIME_SECS,
	    updated_secs, "seconds");

	SENSIBLE_HARD(selected_rule->p2_lifetime_kb,
	    default_rule.p2_lifetime_kb,
	    ecomb->sadb_x_ecomb_hard_bytes >> 10,
	    MIN_P2_LIFETIME_HARD_KB, MAX_P2_LIFETIME_KB,
	    updated_kb, "kilobytes");

	SENSIBLE_SOFT(selected_rule->p2_softlife_kb,
	    selected_rule->p2_lifetime_kb,
	    default_rule.p2_softlife_kb,
	    ecomb->sadb_x_ecomb_soft_bytes >> 10,
	    MIN_P2_LIFETIME_SOFT_KB, MAX_P2_LIFETIME_KB,
	    updated_kb, "kilobytes");

#undef UPDATE_HARD
#undef UPDATE_SOFT
#undef SENSIBLE_HARD
#undef SENSIBLE_SOFT
#undef INSIDE_BOUNDARIES

	check_rule(selected_rule, B_FALSE);

	/*
	 * The ACQUIRE message will contain the system default values for
	 * hard_addtime etc. These are set my ndd(1M) on /dev/ipsecesp.
	 * These may have been changed since the last ACQUIRE, so update
	 * in.iked's defaults based on what is in this ACQUIRE message.
	 */
	ike_defs.sys_p2_lifetime_secs =
	    (uint32_t)ecomb->sadb_x_ecomb_hard_addtime;
	ike_defs.sys_p2_softlife_secs =
	    (uint32_t)ecomb->sadb_x_ecomb_soft_addtime;
	ike_defs.sys_p2_lifetime_bytes =
	    (uint32_t)ecomb->sadb_x_ecomb_hard_bytes;
	ike_defs.sys_p2_softlife_bytes =
	    (uint32_t)ecomb->sadb_x_ecomb_soft_bytes;
}

/*
 * Look in existing phase 1 list for an appropriate phase1 SA.  Currently,
 * match on addresses only.  Eventually, use IDs and KM cookie lookup also!
 *
 * Boolean param indicates whether or not it's okay to return a larval
 * or expiring SA, these are only acceptable if SA's are being looked
 * up for deletion.
 *
 * TODO - If identities on ACQUIRE messages come up, use them.
 */

/*
 * For scope_id-aware sockaddr_in6 comparison.
 *
 * NOTE:  Using common-case of matching first in list if scope_id is not
 *	  specified.  This will be a problem on multi-homed systems that use
 *	  IKE to secure link-local traffic.
 */
#define	sin6_equal(s1, s2) \
	(IN6_ARE_ADDR_EQUAL(&((s1)->sin6_addr), &((s2)->sin6_addr)) && \
	(!IN6_IS_ADDR_LINKLOCAL(&((s1)->sin6_addr)) || \
		(s1)->sin6_scope_id == 0 || (s2)->sin6_scope_id == 0 || \
		(s1)->sin6_scope_id == (s2)->sin6_scope_id))

/* ARGSUSED */
phase1_t *
match_phase1(struct sockaddr_storage *local, struct sockaddr_storage *remote,
    sadb_ident_t *localid, sadb_ident_t *remoteid, sadb_x_kmc_t *cookie,
    boolean_t any_phase1_ok)
{
	phase1_t *walker;

	PRTDBG(D_P1, ("Looking for an existing Phase 1 SA..."));
	assert(local->ss_family == remote->ss_family);

	for (walker = phase1_head; walker != NULL; walker = walker->p1_next) {
		if (local->ss_family != walker->p1_local.ss_family)
			continue;

		/*
		 * Don't use a phase I SA if its about to expire. libike
		 * will set lock_flags if the SA is in a special state,
		 * typically waiting for deletion. See isakmp_internal.h
		 * for the "negotiation" structure details.
		 *
		 * It's OK to return an expiring SA if (any_phase1_ok)
		 * as this flag is set when ikeadm wants to delete
		 * any matching SA's
		 */

		if (walker->p1_pminfo->negotiation->sa->lock_flags) {
			if (any_phase1_ok) {
				PRTDBG(D_P1, ("Found expiring Phase I SA."));
			} else {
				PRTDBG(D_P1, ("Skipping expiring phase I SA"));
				continue;
			}
		}

		switch (local->ss_family) {
		case AF_INET:
			if (((struct sockaddr_in *)local)->sin_addr.s_addr !=
			    ((struct sockaddr_in *)&walker->p1_local)->
			    sin_addr.s_addr ||
			    ((struct sockaddr_in *)remote)->sin_addr.s_addr !=
			    ((struct sockaddr_in *)&walker->p1_remote)->
			    sin_addr.s_addr)
				continue;
			break;
		case AF_INET6:
			if (!sin6_equal((struct sockaddr_in6 *)local,
			    (struct sockaddr_in6 *)(&walker->p1_local)) ||
			    !sin6_equal((struct sockaddr_in6 *)remote,
			    (struct sockaddr_in6 *)(&walker->p1_remote)))
				continue;
			break;
		}
		/*
		 * Don't take any larval/expring phase 1s, unless user
		 * explicitly asked for them.
		 */
		if (any_phase1_ok || walker->p1_complete) {
			/*
			 * If we've reached here, compare identities if needed.
			 */
			break;	/* Out of for loop. */
		}
	}

	return (walker);
}

#undef sin6_equal

/*
 * A Phase 1 receiver calls this to create a phase1_t to go with this
 * Phase 1 SA/negotiation (from the pm_info).
 */
void
create_receiver_phase1(SshIkePMPhaseI pm_info)
{
	phase1_t *p1;
	int ri;
	struct ike_rule *rp;
	struct sockaddr_storage sa;

	/* Match on pm_info pointer. */
	for (p1 = phase1_head; p1 != NULL; p1 = p1->p1_next)
		if (p1->p1_pminfo == pm_info)
			return;

	PRTDBG((D_POL | D_P1), ("Creating receiver phase1 structure for P1 "
	    "SA negotiation."));

	assert(rules.num_rules > 0);

	p1 = ssh_calloc(1, sizeof (phase1_t));
	if (p1 == NULL) {
		PRTDBG((D_POL | D_P1),
		    ("  Out of memory"));
		return;
	}

	if (!string_to_sockaddr(pm_info->local_ip, &sa)) {
		PRTDBG((D_POL | D_P1),
		    ("  Could not convert string to sockaddr(remote)"));
		ssh_free(p1);
		return;
	} else {
		p1->p1_local = sa;
	}

	/*
	 * Let's hope string_to_sockaddr()/getaddrinfo()/libike internals
	 * handle scope_ids properly.
	 */
	if (!string_to_sockaddr(pm_info->remote_ip, &sa)) {
		PRTDBG((D_POL | D_P1),
		    ("  Could not convert string to sockaddr(remote)"));
		ssh_free(p1);
		return;
	} else {
		p1->p1_remote = sa;
	}

	if (!rulebase_dup(&p1->p1_rulebase, &rules, B_FALSE)) {
		ssh_free(p1);
		PRTDBG((D_POL | D_P1),
		    ("  Out of memory"));
		return;
	}

	/*
	 * Build a candidate rulebase of rules that could work for this phase I.
	 * Just base this on the exchange type (main vs. aggressive) and src/dst
	 * addresses, since that's all we have to work with.
	 */
	PRTDBG((D_POL), ("  Examining rule list."));
	for (ri = 0; ri < p1->p1_rulebase.num_rules; ++ri) {
		rp = p1->p1_rulebase.rules[ri];
		PRTDBG((D_POL), ("  rule '%s' %d;",
		    rp->label ? rp->label : "[dead]", rp->mode));
		PRTDBG((D_POL), ("                         local addr %s;",
		    asp(&rp->local_addr)));
		PRTDBG((D_POL), ("                         remote addr %s",
		    asp(&rp->remote_addr)));

		if (rp->mode != SSH_IKE_XCHG_TYPE_ANY &&
		    pm_info->exchange_type != rp->mode ||
		    !address_match(&p1->p1_local, &rp->local_addr) ||
		    !address_match(&p1->p1_remote, &rp->remote_addr)) {
			PRTDBG((D_POL), (" [doesn't match]"));
			ri += rulebase_delete_rule(&p1->p1_rulebase, ri);
		} else {
			PRTDBG((D_POL), ("   [match]"));
		}
	}


	/* Join this phase1_t and the SSH policy mgr. pm_info at the hip. */
	assert(pm_info->policy_manager_data == NULL);
	p1->p1_pminfo = pm_info;
	pm_info->policy_manager_data = p1;

	p1->p1_pmsg = NULL;
	p1->p1_pmsg_tail = NULL;

	p1->p1_ptpn = &phase1_head;
	p1->p1_next = phase1_head;
	if (phase1_head != NULL)
		phase1_head->p1_ptpn = &(p1->p1_next);
	phase1_head = p1;

	/* cache a quick are we v4 or v6 */
	p1->p1_isv4 = ((p1->p1_local.ss_family == AF_INET) ||
	    IN6_IS_ADDR_V4MAPPED((&((struct sockaddr_in6 *)(&p1->p1_local))
	    ->sin6_addr)));
	/* Enable Quick mode SA negotiation */
	p1->p1_create_phase2 = B_TRUE;
}

/*
 * Check if a sockaddr is a broadcast or multicast SA.  Returns FALSE if I
 * don't know.
 */
static boolean_t
sa_ismbcast(struct sockaddr_storage *sa)
{
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;

	switch (sa->ss_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)sa;
		if (IN_MULTICAST(ntohl(sin->sin_addr.s_addr)))
			return (B_TRUE);
		/*
		 * TODO: How do I easily determine bcast?  For now, punt.
		 */
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)sa;
		return (IN6_IS_ADDR_MULTICAST(&(sin6->sin6_addr)));
	}

	return (B_FALSE);
}

/*
 * Locate an existing phase 1 session, or initiate a new one, to handle a
 * locally initiated acquire request.
 *
 * Other parameters will be passed here.  Hell, it may be easier to
 * just inline this in handle_acquire().
 *
 * Look up matching rules based on src/dst addresses and identities.
 * If the acquire message included a key manager cookie, use it to choose a
 * rule, otherwise use the first one that matches the addresses and identities.
 * Currently it doesn't really pay attention to the acquire identities.
 *
 * Also, assume big-time single-threading, so don't worry about locks, etc.
 *
 * The ssh_ike_connect() stuff itself is in initiator.c.
 *
 * This function MUST consume pmsg.
 */
phase1_t *
get_phase1(parsedmsg_t *pmsg, boolean_t create_phase2)
{
	phase1_t *p1;
	struct ike_rule *selected_rule;
	SshIkeNegotiation p1_neg;
	int ri;				/* rule index */
	int rc;
	int salen;
	struct sockaddr_storage *src = pmsg->pmsg_sss;
	struct sockaddr_storage *dst = pmsg->pmsg_dss;
	sadb_ident_t *localid = (sadb_ident_t *)
	    pmsg->pmsg_exts[SADB_EXT_IDENTITY_SRC];
	sadb_ident_t *remoteid = (sadb_ident_t *)
	    pmsg->pmsg_exts[SADB_EXT_IDENTITY_DST];
	sadb_x_kmc_t *cookie = (sadb_x_kmc_t *)
	    pmsg->pmsg_exts[SADB_X_EXT_KM_COOKIE];

	PRTDBG((D_POL | D_P1), ("Trying to get Phase 1 (%s)...",
	    create_phase2 ? "by itself" : "and then QM"));

	if (cookie != NULL && cookie->sadb_x_kmc_cookie == 0)
		cookie = NULL;

	/*
	 * Do address reality checking here.  May wish to inline here.
	 */
	if (sa_ismbcast(dst)) {
		send_negative_acquire(pmsg, ENETUNREACH);
		free_pmsg(pmsg);
		return (NULL);
	}

	assert(src->ss_family == dst->ss_family);
	switch (src->ss_family) {
	case AF_INET:
		salen = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		salen = sizeof (struct sockaddr_in6);
		break;
	default:
		send_negative_acquire(pmsg, EAFNOSUPPORT);
		free_pmsg(pmsg);
		return (NULL);
	}

	/* only match completed p1 SAs... */
	p1 = match_phase1(src, dst, localid, remoteid, cookie, B_FALSE);
	if (p1 != NULL) {
		PRTDBG(D_P1, ("  Found existing Phase 1."));

		/*
		 * CORNER CASE:  If P1 is in-progress from DPD creation,
		 * enable the create_phase2 if we're now trying to add a
		 * phase 2 on to this.
		 *
		 * The cleverest way to express this is with a logical OR.
		 *
		 * Existing-P1-creating-P2?	Need-Create-P2?	   Set-To
		 * ------------------------	---------------	   ------
		 * no				no		   no
		 * no				yes		   yes
		 * yes				no		   yes
		 * yes				yes		   yes
		 *
		 * DPD-initiator pmsgs are of type SADB_EXPIRE, not
		 * SADB_ACQUIRE.
		 */
		p1->p1_create_phase2 |= create_phase2;

		/*
		 * Caller takes care of new samsg in case of existing phase1!
		 */
		return (p1);
	}

	if (rules.num_rules < 1) {
		PRTDBG((D_POL | D_P1), ("  No rules to create Phase 1!"));
		free_pmsg(pmsg);
		return (NULL);
	}

	/*
	 * Create a new phase1 structure, and call initiate_phase1().
	 */

	p1 = ssh_calloc(1, sizeof (phase1_t));
	if (p1 == NULL ||
	    !rulebase_dup(&p1->p1_rulebase, &rules, B_FALSE)) {
		ssh_free(p1);
		send_negative_acquire(pmsg, ENOMEM);
		free_pmsg(pmsg);
		return (NULL);
	}

	p1->p1_isv4 = (src->ss_family == AF_INET);

	/*
	 * We need to pick a rule based on the key manager cookie.
	 * If there is no cookie, use the first rule that matches the addresses.
	 * We don't deal with identities in the acquire yet.
	 */
	if (cookie != NULL) {
		PRTDBG((D_POL | D_P1),
		    ("  Searching rulebase for cookie 0x%X",
		    cookie->sadb_x_kmc_cookie));
	} else {
		PRTDBG((D_POL | D_P1),
		    ("  Searching rulebase for src = %s", sap(src)));
		PRTDBG((D_POL | D_P1),
		    ("                         dst = %s", sap(dst)));
	}

	selected_rule = NULL;

	PRTDBG((D_POL), ("  Examining rule list."));
	for (ri = 0; ri < p1->p1_rulebase.allocnum_rules; ++ri) {
		struct ike_rule *rp = p1->p1_rulebase.rules[ri];
		if (rp == NULL)
			continue;

		PRTDBG((D_POL), ("  rule '%s' cookie 0x%X;",
		    rp->label ? rp->label : "[dead]", rp->cookie));
		PRTDBG((D_POL), ("                         local addr %s;",
		    asp(&rp->local_addr)));
		PRTDBG((D_POL), ("                         remote addr %s",
		    asp(&rp->remote_addr)));

		if (!address_match(src, &rp->local_addr) ||
		    !address_match(dst, &rp->remote_addr))
			goto nomatch;

		if (localid != NULL && remoteid != NULL) {
			if (rp->xforms[0]->auth_method ==
			    SSH_IKE_VALUES_AUTH_METH_PRE_SHARED_KEY) {
				/*
				 * If the identities are not of address type,
				 * we can't use pre-shared keys (until/unless
				 * we support aggressive mode).
				 */
				if (localid->sadb_ident_type !=
				    SADB_IDENTTYPE_PREFIX ||
				    remoteid->sadb_ident_type !=
				    SADB_IDENTTYPE_PREFIX)
					goto nomatch;
#ifdef NOTYET
				/*
				 * If the identities are not the same as the
				 * addresses, we can't use pre-shared keys
				 * (until/unless we support aggressive mode).
				 */
				if (!match_ident_addr(localid, src) ||
				    !match_ident_addr(remoteid, dst))
					goto nomatch;
			} else {
				if (!match_ident_certspec(localid,
				    &rp->local_id) ||
				    !match_ident_certspec(remoteid,
				    &rp->remote_id))
					goto nomatch;
#endif
			}
		}

		/*
		 * We have a basic match, so we won't delete this rule, and we
		 * may select it as the binding rule.
		 * If we have a kmcookie from the kernel, then we select the
		 * rule that has the same cookie value, otherwise we select the
		 * first live rule that matches.
		 */

		if (cookie != NULL) {
			if (cookie->sadb_x_kmc_cookie == rp->cookie)
				selected_rule = rp;
		} else {
			if (rp->label != NULL && selected_rule == NULL)
				selected_rule = rp;
		}

		PRTDBG((D_POL), ("   [basic match]"));
		continue;

	nomatch:
		PRTDBG((D_POL), ("   [doesn't match]"));
		ri += rulebase_delete_rule(&p1->p1_rulebase, ri);
	}

	if (selected_rule == NULL) {
		PRTDBG((D_POL | D_P1), ("  No matching rule for Phase 1!"));
		rulebase_datafree(&p1->p1_rulebase);
		ssh_free(p1);
		send_negative_acquire(pmsg, ENOENT);
		free_pmsg(pmsg);
		return (NULL);
	}

	/* The selected rule now gets permanently latched in for this phase1. */
	PRTDBG((D_POL | D_P1), ("  Selected rule: '%s'",
	    selected_rule->label ? selected_rule->label : "[dead]"));
	p1->p1_rule = selected_rule;

	update_defs(pmsg->pmsg_samsg, selected_rule);

	/* Inherit phase 2 PFS group from rule. */
	p1->p1_p2_group = p1->p1_rule->p2_pfs;

	/*
	 * At this point, there could be rules remaining in the rulebase that
	 * are incompatible with the selected rule (different modes, for
	 * example).  We assume that if any further constraining of the rulebase
	 * is necessary then it will be done in
	 * ssh_policy_negotiation_done_isakmp (see the comment there).
	 */

	(void) memcpy(&p1->p1_local, src, salen);
	(void) memcpy(&p1->p1_remote, dst, salen);
	if (localid != NULL && remoteid != NULL) {
		p1->p1_localid = ssh_memdup(localid,
		    SADB_64TO8(localid->sadb_ident_len));
		if (p1->p1_localid == NULL) {
			free_pmsg(pmsg);
			free_phase1(p1);
			return (NULL);
		}

		p1->p1_remoteid = ssh_memdup(remoteid,
		    SADB_64TO8(remoteid->sadb_ident_len));
		if (p1->p1_remoteid == NULL) {
			free_pmsg(pmsg);
			free_phase1(p1);
			return (NULL);
		}
	}

	if (is_system_labeled())
		fix_p1_label_range(p1);

	p1->p1_pminfo = NULL;
	p1->p1_pmsg = pmsg;
	p1->p1_pmsg_tail = pmsg;
	p1->p1_create_phase2 = create_phase2;
	p1->p1_dpd_status = DPD_NOT_INITIATED;

	PRTDBG(D_P1, ("Starting Phase 1 negotiation..."));

	rc = initiate_phase1(p1, &p1_neg);

	switch (rc) {
	case SSH_IKE_ERROR_OK:
		/* Get out of the switch, and do real work! */
		break;
	default:
		/*
		 * Eventually do something useful based on the returned
		 * error code.  For now, just return NULL.
		 */
		free_phase1(p1);	/* Frees pmsg too! */
		PRTDBG(D_P1, ("  Phase 1 initiation error %d (%s). "
		    "Returning NULL.", rc, ike_connect_error_to_string(rc)));
		return (NULL);
	}

	/* Let's do some real work! */
	if (p1_neg == NULL) {
		/*
		 * Let the callback handle things (including freeing of
		 * the phase1_t).  It's a soon-to-be-error.
		 */
		PRTDBG(D_P1, ("  Phase 1 negotiation is NULL, but still in "
		    " first step of negotiation, okay for now."));
		return (NULL);
	}

	PRTDBG(D_P1, ("  New Phase 1 negotiation!"));

	/*
	 * At this point, the ssh code has already created an SshIkePMPhaseI
	 * structure which points to our phase1_t; but it won't bother to
	 * tell us about it until later.  We'd really like to know about it
	 * now, though...so try to get a pointer to it by devious methods.
	 * (i.e. through knowledge of an ssh-internal struct).
	 */
	p1->p1_pminfo = get_ssh_pminfo(p1_neg);

	/*
	 * Insert it now!
	 */
	p1->p1_ptpn = &phase1_head;
	p1->p1_next = phase1_head;
	if (phase1_head != NULL)
		phase1_head->p1_ptpn = &(p1->p1_next);
	phase1_head = p1;

	return ((phase1_t *)1);
}


/*
 * Clear local cert pointers so we don't have dangling key pointers.
 * This is safe to do because we do NULL checks at every p1_localcert
 * reference and reload the local cert if it exists.
 */
void
p1_localcert_reset(void)
{
	phase1_t *walker;

	for (walker = phase1_head; walker != NULL; walker = walker->p1_next)
		walker->p1_localcert = NULL;
}
