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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/pfkeyv2.h>
#include <time.h>
#include <locale.h>
#include <stdio.h>
#include <strings.h>

#include <ipsec_util.h>

#include "defs.h"
#include "getssh.h"
#include <netdb.h>

#include <ike/dn.h>
#include <ike/isakmp_internal.h>
#include <ike/sshtimeouts.h>

#undef index				/* ssh namespace pollution */
#undef sprintf

/*
 * Simple NAT-T state number to name lookup
 */

static const char *natt_codes[] = {
	/* Codes consecutive integers from -1 to 4 */
	"NEVER", "INIT", "VID", "NAT-D", "PORT", "GOT PORT"
};

static const char *
natt_code_string(int code)
{
	if (code < -1 || code > 4)
		return ("ILLEGAL");
	else
		return (natt_codes[code + 1]);
}


/*
 * Make the destination rulebase contain the same rules as the source rulebase.
 * If 'allocmem' is true, allocates new memory for each rule and copies the
 * data; if false, simply references the existing rule and increments the ref
 * count on each rule.  Currently includes the "dead" rules too.
 *
 * Return B_FALSE if there's an memory-allocation error.
 */
boolean_t
rulebase_dup(struct ike_rulebase *d, const struct ike_rulebase *s,
    boolean_t allocmem)
{
	int i;
	if (d == NULL)
		return (B_FALSE);

	if ((s == NULL) || (s->rules == NULL)) {
		d->rules = NULL;
		d->num_rules = 0;
		d->allocnum_rules = 0;
		return (B_TRUE);
	}

	d->allocnum_rules = s->num_rules;
	d->rules = ssh_calloc(d->allocnum_rules, sizeof (*d->rules));
	if (d->rules == NULL)
		return (B_FALSE);

	d->num_rules = 0;
	for (i = 0; i < s->num_rules; ++i) {
		if (s->rules[i] == NULL)
			continue;
		/* Include dead rules for now. */
		if (allocmem) {
			d->rules[d->num_rules] = rule_dup(s->rules[i]);
			if (d->rules[d->num_rules] == NULL) {
				ssh_free(d->rules);
				return (B_FALSE);
			}
			d->rules[d->num_rules]->refcount = 1;
			d->num_rules++;
		} else {
			d->rules[d->num_rules++] = s->rules[i];
			++s->rules[i]->refcount;
		}
	}
	return (B_TRUE);
}

/*
 * Delete the specified rule from the rulebase.  A side-effect is that
 * rb->num_rules is decremented, and this often messes with loop-terminating
 * conditions in callers.
 *
 * The current implementation just NULLs out this entry.  However, the interface
 * allows for an implementation that moves all the lower entries up to fill in
 * the gap.  We return -1 if we want the caller to "back up" in its iteration
 * through the table, or 0 if we have simply NULLed out the specified entry.
 * I'm not yet sure which is the better way.
 *
 * Given the iterators of rulebases, the -1 and "back up" way is better.
 * There's eventual locking hell here, but since we're mostly single-threaded,
 * that's not so bad.
 */
int
rulebase_delete_rule(struct ike_rulebase *rb, int index)
{
	int i;

	rb->num_rules--;
	rule_free(rb->rules[index]);
#if not_any_more
	/* This is the old broken way... */
	rb->rules[index] = NULL;
	return (0);
#endif

	for (i = index; i < rb->num_rules; i++)
		rb->rules[i] = rb->rules[i + 1];
	rb->rules[i] = NULL;	/* Just for safety's sake. */

	return (-1);
}

/*
 * Walk a rulebase, from 0 to allocnum - 1, and print the index and
 * either the label, "dead", or "null" for each entry.
 */
void
rulebase_dbg_walk(struct ike_rulebase *rb)
{
	int	i;

	PRTDBG(D_POL, ("Walking rulebase: num rules=%u, num rules alloc'd=%u",
	    rb->num_rules, rb->allocnum_rules));
	for (i = 0; i < rb->allocnum_rules; i++) {
		PRTDBG(D_POL, ("index %u: %s", i,
		    ((rb->rules[i] == NULL) ? "null" :
		    ((rb->rules[i]->label == NULL) ? "dead" :
		    rb->rules[i]->label))));
	}
}

/*
 * Look up a rule in the given rulebase that matches the given label
 *
 * Increment the rule's refcount and return the index on success;
 * return -1 on failure.
 */
int
rulebase_lookup(const struct ike_rulebase *s, char *label, int labellen)
{
	int	i;

	PRTDBG(D_POL, ("Rulebase lookup: looking for rule label '%s'", label));
	for (i = 0; i < s->allocnum_rules; i++) {
		if ((s->rules[i] == NULL) || (s->rules[i]->label == NULL))
			continue;
		if (strncmp(s->rules[i]->label, label, labellen) == 0) {
			s->rules[i]->refcount++;
			return (i);
		}
	}
	return (-1);
}

/*
 * Look up the nth rule in the given rulebase.  We need an explicit lookup
 * function (rather than grabbing the nth array element) because some slots
 * in the array might be empty.
 *
 * Increment the rule's refcount and return the index on success; return
 * -1 on failure.
 */
int
rulebase_lookup_nth(const struct ike_rulebase *s, int n)
{
	int	i, rcnt = 0;

	if (n > s->num_rules)
		return (-1);

	for (i = 0; i < s->allocnum_rules; i++) {
		if ((s->rules[i] == NULL) || (s->rules[i]->label == NULL))
			continue;
		if (rcnt == n) {
			s->rules[i]->refcount++;
			return (i);
		} else {
			rcnt++;
		}
	}
	return (-1);
}

/*
 * A Phase 1 SA has been freed.  Deal with it.  See phase1.c's delete_phase1()
 * for why this function still lives in this tiny form.
 */
void
ssh_policy_isakmp_sa_freed(SshIkePMPhaseI pm_info)
{
	PRTDBG((D_POL | D_P1),
	    ("Deleting local phase 1 instance."));

	/*
	 * boolean says don't notify sshlib
	 */
	delete_phase1(pm_info->policy_manager_data, B_FALSE);
}


/*
 * This function constructs our local identity payload based on our local
 * certificate or the local address.
 * Returns a newly allocated SshIkePayloadID or NULL for various failures.
 */
SshIkePayloadID
construct_local_id(const struct certlib_cert *cert,
    const struct sockaddr_storage *addr,
    const struct ike_rule *rule)
{
	SshIkePayloadID local_id;
#define	MAX_ID_LEN 100
	char buffer[MAX_ID_LEN];
	SshX509Name names = NULL;
	Boolean critical;
	ipaddr_t *ipv4_addr;
	in6_addr_t *ipv6_addr;
	int orig_len;

	PRTDBG(D_P1, ("Constructing local identity payload..."));

	local_id = ssh_calloc(1, sizeof (*local_id));
	if (local_id == NULL) {
		return (NULL);
	}

	/*
	 * RFC 2407, section 4.6.2:  During Phase I negotiations, the ID port
	 * and protocols fields MUST be set to zero or UDP port 500.
	 *
	 * We opt for zero, which has already been filled in by ssh_calloc().
	 */

	/*
	 * As a tentative default, fill in our local IP address.  This may get
	 * replaced below with something from our certificate.
	 */
	switch (addr->ss_family) {
	case AF_INET:
		local_id->id_type = IPSEC_ID_IPV4_ADDR;
		local_id->identification_len = sizeof (struct in_addr);
		(void) memcpy(&local_id->identification,
		    &((struct sockaddr_in *)addr)->sin_addr,
		    sizeof (struct in_addr));
		break;
	case AF_INET6:
		/*
		 * NOTE: scope_id is not specified in the specs here.  The OS
		 * takes care of scope_id, because this part is bits on the
		 * wire, and scope_id is which wire your bits came off of or
		 * are going on to.
		 */
		local_id->id_type = IPSEC_ID_IPV6_ADDR;
		local_id->identification_len = sizeof (struct in6_addr);
		(void) memcpy(&local_id->identification,
		    &((struct sockaddr_in6 *)addr)->sin6_addr,
		    sizeof (struct in6_addr));
		break;
	default:
		EXIT_FATAL("Socket address family not IPv4 or IPv6!");
	}

	if (cert != NULL) {
		if (rule->local_idtype == IPSEC_ID_DER_ASN1_DN) {
			/*
			 * Extract stuff from the local certificate.  Should
			 * use the subject name if present, otherwise a
			 * subjectAltName of type DN.  For now, assumes that
			 * there is always a subject name.
			 */
			PRTDBG(D_P1, ("  DN ID type:"));
			if (ssh_x509_cert_get_subject_name_der(cert->cert,
			    &local_id->identification.asn1_data,
			    &local_id->identification_len) == TRUE)
				local_id->id_type = IPSEC_ID_DER_ASN1_DN;
			PRTDBG(D_P1, ("    %s",
			    ssh_ike_id_to_string(buffer, sizeof (buffer),
			    local_id)));

			return (local_id);
		}

		/*
		 * Get the subjectAltNames, if applicable.
		 */
		if (!ssh_x509_cert_get_subject_alternative_names(cert->cert,
		    &names, &critical))
			goto leaveIP;
		orig_len = local_id->identification_len;

		switch (rule->local_idtype) {
		case IPSEC_ID_IPV4_ADDR:
			/*
			 * These cases should extract a subjectAltName of type
			 * IP from the certificate.
			 */
			if (!ssh_x509_name_pop_ip(names, (uchar_t **)&ipv4_addr,
			    &local_id->identification_len))
				goto leaveIP;
			if (local_id->identification_len != sizeof (ipaddr_t)) {
				ssh_free(ipv4_addr);
				local_id->identification_len = orig_len;
				goto leaveIP;
			}
			(void) memcpy(local_id->identification.ipv4_addr,
			    ipv4_addr, sizeof (ipaddr_t));
			ssh_free(ipv4_addr);
			local_id->id_type = IPSEC_ID_IPV4_ADDR;
			break;
		case IPSEC_ID_IPV6_ADDR:
			if (!ssh_x509_name_pop_ip(names, (uchar_t **)&ipv6_addr,
			    &local_id->identification_len))
				goto leaveIP;
			if (local_id->identification_len !=
			    sizeof (in6_addr_t)) {
				ssh_free(ipv6_addr);
				local_id->identification_len = orig_len;
				goto leaveIP;
			}
			(void) memcpy(local_id->identification.ipv6_addr,
			    ipv6_addr, sizeof (in6_addr_t));
			ssh_free(ipv6_addr);
			local_id->id_type = IPSEC_ID_IPV6_ADDR;
			break;
		case IPSEC_ID_USER_FQDN:
			if (!ssh_x509_name_pop_email(names,
			    (char **)&local_id->identification.user_fqdn))
				goto leaveIP;
			local_id->identification_len = strlen(
			    (char *)local_id->identification.user_fqdn);
			local_id->id_type = IPSEC_ID_USER_FQDN;
			break;
		case IPSEC_ID_FQDN:
			/*
			 * These cases should extract a subjectAltName of the
			 * appropriate type from the local certificate.
			 */
			if (!ssh_x509_name_pop_dns(names,
			    (char **)&local_id->identification.fqdn))
				goto leaveIP;
			local_id->identification_len = strlen(
			    (char *)local_id->identification.fqdn);
			local_id->id_type = IPSEC_ID_FQDN;
			break;
		case IPSEC_ID_IPV4_ADDR_SUBNET:
		case IPSEC_ID_IPV6_ADDR_SUBNET:
		case IPSEC_ID_IPV4_ADDR_RANGE:
		case IPSEC_ID_IPV6_ADDR_RANGE:
		case IPSEC_ID_DER_ASN1_GN:
		case IPSEC_ID_KEY_ID:
		default:
			PRTDBG(D_P1,
			    ("  Unsupported ID type %d", rule->local_idtype));
			/*
			 * Don't know what to do with these, just leaveIP our
			 * IP address in there.
			 */
			/* FALLTHRU */
		leaveIP:
			PRTDBG(D_P1,
			    ("  Using local IP address for local ID"));
			break;
		}
		if (names != NULL)
			ssh_x509_name_reset(names);
	}

	PRTDBG(D_P1, ("  Local ID type: %s",
	    ssh_ike_id_to_string(buffer, sizeof (buffer), local_id)));

	return (local_id);
}

/*
 * Given a pm_info, try and guess what the remote_id should be.  This
 * is used in RSA encryption situations (see isakmp_policy.c).  It's hard
 * to guess what the remote_id should be, but we give it our best here.
 */
SshIkePayloadID
construct_remote_id(SshIkePMPhaseI pm_info)
{
	SshIkePayloadID remote_id;
	struct ike_rule *rule;
	const char *current, *paydirt = NULL;
	char buffer[MAX_ID_LEN];
	int i;
	uint8_t ipv4[4], ipv6[16];
	SshDNStruct dn;
	boolean_t dn_convert;
	phase1_t *p1;

	PRTDBG(D_P1, ("Constructing remote identity..."));

	p1 = (phase1_t *)pm_info->policy_manager_data;
	if (p1 == NULL)
		return (NULL);

	PRTDBG(D_P1, ("  NAT-T state: %d (%s)",
	    pm_info->p1_natt_state, natt_code_string(pm_info->p1_natt_state)));

	rule = p1->p1_rule;
	if (rule == NULL)
		return (NULL);

	remote_id = ssh_calloc(1, sizeof (*remote_id));
	if (remote_id == NULL) {
		return (NULL);
	}

	/*
	 * NOTE:	We won't be able to use excludes very effectively
	 *		here w/o rewhacking all of ssh_policy_find_public_key().
	 *		Perhaps that'll come later.  For now, use just the
	 *		includes.
	 *
	 * Step through includes.  If I find one that I can determine
	 * a real ISAKMP ID out of, pluck it out and return.  (NOTE2:  In
	 * a perfect world, I could exploit more of pm_info to see what I
	 * can and should use.)
	 */
	for (i = 0; (current = rule->remote_id.includes[i]) != NULL; i++) {
		/*
		 * First, look for any FOO= prefixes.  If we find one, we can
		 * determine for sure what to use.
		 */
		if (strncmp(current, "EMAIL=", 6) == 0) {
			remote_id->id_type = IPSEC_ID_USER_FQDN;
			paydirt = current + 6;
		} else if (strncmp(current, "DNS=", 4) == 0) {
			remote_id->id_type = IPSEC_ID_FQDN;
			paydirt = current + 4;
		} else if (strncmp(current, "DN=", 3) == 0) {
			remote_id->id_type = IPSEC_ID_DER_ASN1_DN;
			paydirt = current + 3;
		} else if (strncmp(current, "IP=", 3) == 0) {
			remote_id->id_type = IPSEC_ID_IPV4_ADDR;
			paydirt = current + 3;
			/*
			 * NOTE:IPV4_ADDR is just an indicator.
			 *	We'll have to use a conversion routine
			 *	to figure out IPv4/IPv6.
			 */

			/* If these fail, try some heuristics... */
		} else if (strchr(current, '=') != NULL) {
			/* Probably a DN. */
			paydirt = current;
			remote_id->id_type = IPSEC_ID_DER_ASN1_DN;
		} else if (strchr(current, '@') != NULL) {
			/* Probably an email. */
			paydirt = current;
			remote_id->id_type = IPSEC_ID_USER_FQDN;
		} else if (inet_pton(AF_INET, current, &ipv4) == 1) {
			/*
			 * Paydirt == NULL and id_type set
			 * means we did the conversion!
			 */
			remote_id->id_type = IPSEC_ID_IPV4_ADDR;
		} else if (inet_pton(AF_INET6, current, &ipv6) == 1) {
			remote_id->id_type = IPSEC_ID_IPV6_ADDR;
		} else if (strchr(current, '.') != NULL) {
			/* DNS is worth a shot... */
			paydirt = current;
			remote_id->id_type = IPSEC_ID_FQDN;
		}

		switch (remote_id->id_type) {
		case IPSEC_ID_USER_FQDN:
		case IPSEC_ID_FQDN:
			/*
			 * Exploiting that unions overlay pointers,
			 * and that paydirt points to a known-to-be-bounded
			 * string.
			 */
			remote_id->identification.fqdn = ssh_strdup(paydirt);
			if (remote_id->identification.fqdn == NULL)
				continue;
			remote_id->identification_len = strlen(paydirt);
			break;
		case IPSEC_ID_IPV4_ADDR:
			if (paydirt != NULL &&
			    inet_pton(AF_INET, paydirt, &ipv4) != 1)
				continue;
			(void) memcpy(&(remote_id->identification.ipv4_addr),
			    ipv4, sizeof (ipv4));
			remote_id->identification_len = sizeof (ipv4);
			break;
		case IPSEC_ID_IPV6_ADDR:
			if (paydirt != NULL &&
			    inet_pton(AF_INET6, paydirt, &ipv6) != 1)
				continue;
			(void) memcpy(&(remote_id->identification.ipv6_addr),
			    ipv6, sizeof (ipv6));
			remote_id->identification_len = sizeof (ipv6);
			break;
		case IPSEC_ID_DER_ASN1_DN:
			/*
			 * Convert "paydirt" into ASN.1.
			 * Do this by using libike's ldap routines.
			 */
			ssh_dn_init(&dn);
			/* Assume C's short-circuiting works. */
			dn_convert = (ssh_dn_decode_ldap((uchar_t *)paydirt,
			    &dn) && ssh_dn_encode_der(&dn,
			    &(remote_id->identification.asn1_data),
			    &(remote_id->identification_len), x509config));
			ssh_dn_clear(&dn);
			if (!dn_convert)
				continue;
			break;
		default:
			remote_id->id_type = 0;
		}

		if (remote_id->id_type != 0)
			break;
	}

	if (remote_id->id_type == 0) {
		ssh_ike_id_free(remote_id);
		remote_id = NULL;
		PRTDBG(D_P1, ("  Remote ID is NULL!"));
	} else {
		PRTDBG(D_P1, ("  Remote ID type: %s",
		    ssh_ike_id_to_string(buffer, sizeof (buffer), remote_id)));
	}

	return (remote_id);
}

/*
 * Check if a (remote) certificate matches the policy's certspec.
 * This is like certlib_match_cert() except it works on an SshX509Certificate
 * instead of a struct certlib_cert.
 * Returns 1 for match, 0 for no match.
 * Heavily copied from certlib.c.
 */
static boolean_t
policy_match_cert(const SshX509Certificate certp, phase1_t *p1)
{
	char **cert_pattern = NULL;
	int i, iv, cert_count;
	struct certlib_certspec *certspec = &p1->p1_rule->remote_id;

	/* If we specify an empty string, let any remote cert through. */
	if (certspec->num_includes == 1 && strlen(certspec->includes[0]) == 0)
		return (B_TRUE);

	/* Derive a cert_pattern from the passed-in X.509 certificate. */
	cert_count = certlib_get_x509_pattern(certp, &cert_pattern);

	/* Check certificate's names with provided exclusion certspecs. */
	for (i = 0; i < certspec->num_excludes; ++i) {
		for (iv = 0; iv < cert_count; ++iv) {
			if (strcmp(certspec->excludes[i], cert_pattern[iv]) ==
			    0) {
				PRTDBG(D_POL,
				    ("Cert Match: %s found, but excluded.\n",
				    certspec->excludes[i]));
				certlib_clear_cert_pattern(cert_pattern,
				    cert_count);
				return (B_FALSE);
			}
		}
	}

	/* Check certificate's names with provided inclusion certspecs. */
	for (i = 0; i < certspec->num_includes; ++i) {
		for (iv = 0; iv < cert_count; ++iv) {
			if (strcmp(certspec->includes[i],
			    cert_pattern[iv]) == 0) {
				PRTDBG(D_POL,
				    ("Cert Match: %s found.\n",
				    certspec->includes[i]));
				/* Clear certificate's certspec */
				certlib_clear_cert_pattern(cert_pattern,
				    cert_count);
				return (B_TRUE);
			}
		}
		PRTDBG(D_POL, ("Cert match: Pattern %s not found\n",
		    certspec->includes[i]));
	}

	/* Clear certificate's names. */
	certlib_clear_cert_pattern(cert_pattern, cert_count);

	return (B_FALSE);
}

/*
 * This is a hook from ssh_policy_find_public_key_found() to tell us that the
 * remote's certificate has been found during Phase I.	This is our chance to
 * apply local policy about whether we are willing to talk to this peer based
 * on its certificate.	To reject the negotiation, this function returns
 * B_FALSE, which the caller will use to abort things.
 */
boolean_t
policy_notify_remote_cert(SshIkePMPhaseI pm_info,
    SshX509Certificate x509_cert)
{
	phase1_t *p1 = pm_info->policy_manager_data;

	PRTDBG(D_POL, ("Applying local policy to peer cert, "
	    "NAT-T state %d (%s)", pm_info->p1_natt_state,
	    natt_code_string(pm_info->p1_natt_state)));

	if (debug & D_POL) {
		char *subject;
		if (ssh_x509_cert_get_subject_name(x509_cert, &subject)) {
			PRTDBG(D_POL,
			    ("  Subject = %s.", subject));
			ssh_free(subject);
		}
		ssh_x509_name_reset(x509_cert->subject_name);
	}

	/*
	 * Check the policy to make sure we are willing to communicate with
	 * the remote identity.
	 */
	if (!policy_match_cert(x509_cert, p1)) {
		PRTDBG((D_POL | D_P1), ("  Remote cert does not match policy, "
		    "rejected!"));
		/* TODO: audit message needed */
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Get our own local ID for an inbound ISAKMP SA (Phase 1) negotiation (we are
 * the responder).  At this point we know the initiator's ID so we can use that
 * to select a local certificate, and from that, a rule.  We also know the
 * auth method so we can skip the certificate stuff if it's using pre-shared
 * keys.
 */
void
ssh_policy_isakmp_id(SshIkePMPhaseI pm_info, SshPolicyIsakmpIDCB callback_in,
    void *callback_context_in)
{
	SshIkePayloadID local_id;
	struct sockaddr_storage sa;
	phase1_t *p1 = pm_info->policy_manager_data;
	struct ike_rule *rp;

	PRTDBG((D_POL | D_P1), ("Getting local id for inbound P1: "
	    "NAT-T state %d (%s)", pm_info->p1_natt_state,
	    natt_code_string(pm_info->p1_natt_state)));

	rp = p1->p1_rule;

	if (rp == NULL) {
		/* No rules left, reject this phase I. */
		local_id = NULL;
		goto out;
	}


	p1->p1_p2_group = rp->p2_pfs;

	/*
	 * At this point, there could be rules remaining in the rulebase that
	 * are inconsistent with the local identity we have chosen.  We assume
	 * that if any further constraining of the rulebase is necessary then it
	 * will be done in ssh_policy_negotiation_done_isakmp (see the comment
	 * there).
	 */
	if (pm_info->auth_method != SSH_IKE_VALUES_AUTH_METH_PRE_SHARED_KEY &&
	    p1->p1_localcert == NULL) {
		p1->p1_localcert = certlib_find_local_ident(&rp->local_id);
		/*
		 * If I misconfigured the rules so I don't have a matching
		 * cert for the rule, bail!
		 */
		if (p1->p1_localcert == NULL) {
			local_id = NULL;
			goto out;
		}
	}

	/*
	 * The local address for this phase1 is in the pm_info.  This is
	 * why we launch separate IKE servers for each IP address.
	 */
	if (!string_to_sockaddr(pm_info->local_ip,
	    (struct sockaddr_storage *)&sa)) {
		local_id = NULL;
		goto out;
	}

	local_id = construct_local_id(p1->p1_localcert, &sa, rp);

out:
	/* If I can't find a local_id, stop the Phase 1 negotiation now! */
	if (local_id == NULL) {
		delete_phase1(p1, B_TRUE);
		PRTDBG((D_POL | D_P1),
		    ("  No local ID to match incoming P1."));
		/*
		 * Unfortunately, calling the callback with local_id == NULL
		 * will invoke a fatal error in the SSH IKE library.  We will
		 * therefore return, which will cancel any phase 1 timers
		 * for this negotiation and save us from having to callback.
		 */
		return;
	}

	callback_in(local_id, callback_context_in);
}

/*
 * Deal with the initial contact notification
 */
static void
handle_initial_contact(SshIkePMPhaseI pm_info)
{
	struct sockaddr_storage ss_remote, ss_local;
	struct sockaddr_storage *remote = &ss_remote;
	struct sockaddr_storage *local = &ss_local;
	SshIkePayloadID rid = NULL;
	phase1_t *p1 = (phase1_t *)pm_info->policy_manager_data;

	PRTDBG(D_POL, ("Handling initial contact notification from peer: "
	    "NAT-T state %d (%s) phase2 %d", pm_info->p1_natt_state,
	    natt_code_string(pm_info->p1_natt_state), p1->p1_create_phase2));

	if (!p1->p1_create_phase2) {
		return;
	}

	/*
	 * I got an initial contact from this peer.  If encrypted, clobber all
	 * other SA stuff for this peer so I can resynch.
	 */
	if (!string_to_sockaddr(pm_info->local_ip, local))
		return;

	if (!string_to_sockaddr(pm_info->remote_ip, remote))
		return;


	/*
	 * Expand for every AH, ESP, <ipcomp?> for now.
	 */

	if (pm_info->p1_natt_state > 1) {
		/*
		 * Include ID if using NATT, and disregard remote address.
		 * Initiator does not move the natt_state beyond VID if
		 * there is no NATT, so the state check is more liberal.
		 */
		rid = pm_info->remote_id;
		remote = NULL;
	} else {
		/* No NATT Phase 1 SA can create AH SAs. */
		delete_assoc(0, local, remote, NULL, NULL, SADB_SATYPE_AH);
		delete_assoc(0, remote, local, NULL, NULL, SADB_SATYPE_AH);
	}

	delete_assoc(0, local, remote, NULL, rid, SADB_SATYPE_ESP);
	delete_assoc(0, remote, local, rid, NULL, SADB_SATYPE_ESP);

	/*
	 * Don't worry about the addrcache entry.  We add to it
	 * based on completion of a phase1 SA.  We delete from it
	 * on the basis of SADB_FLUSH, and potentially other
	 * TBD stimuli.
	 */
}

/*
 * Deals with the R-U-THERE notification.
 */
static void
handle_dpd_notification(unsigned char *local_ip,
    unsigned char *spi, size_t spi_size,
    SshIkeNotifyMessageType notify_message_type,
    unsigned char *notification_data,
    size_t notification_data_size, phase1_t *p1)
{

	SshIkeNegotiation neg = p1->p1_negotiation;
	ike_server_t *ikesrv;
	struct sockaddr_storage src_ip;
	uint32_t recv_seqnum;
	SshIkeErrorCode ssh_rc;
	char	msgtype[32];

	(notify_message_type == 36136) ?
	    (void) strcpy(msgtype, "R-U-THERE") :
	    (void) strcpy(msgtype, "R-U-THERE-ACK");
	PRTDBG(D_POL, ("DPD: Received notify message %s (type %d), from %s.",
	    msgtype, notify_message_type, sap(&p1->p1_remote)));
	if (spi_size != (2 * SSH_IKE_COOKIE_LENGTH) || !p1->p1_use_dpd) {
		PRTDBG(D_POL, ("Dropping DPD notify message - "
		    "Bad SPI Size."));
		return;
	}

	if ((memcmp(spi, neg->sa->cookies.initiator_cookie,
	    SSH_IKE_COOKIE_LENGTH) != 0) ||
	    (memcmp(spi + SSH_IKE_COOKIE_LENGTH,
	    neg->sa->cookies.responder_cookie,
	    SSH_IKE_COOKIE_LENGTH) != 0)) {
		PRTDBG(D_POL, ("Cookies do not match in DPD request"));
		return;
	}

	/* Sent on the wire, might be unaligned. */
	(void) memcpy(&recv_seqnum, notification_data, sizeof (recv_seqnum));
	recv_seqnum = ntohl(recv_seqnum);

	if (notify_message_type == SSH_IKE_NOTIFY_MESSAGE_R_U_THERE_ACK) {
		if (recv_seqnum != p1->p1_dpd_sent_seqnum) {
			PRTDBG(D_OP, ("Sequence numbers do not match recv "
			    "0x%x, sent 0x%x", recv_seqnum,
			    p1->p1_dpd_sent_seqnum));
			return;
		}
		/*
		 * It might be possible that we are getting a very late
		 * response from the peer and the msg might have been
		 * discarded.
		 */
		if (p1->p1_dpd_pmsg != NULL) {
			ssh_cancel_timeouts(pfkey_idle_timer, p1->p1_dpd_pmsg);
			handle_dpd_action(p1->p1_dpd_pmsg, SADB_X_UPDATEPAIR);
			free_pmsg(p1->p1_dpd_pmsg);
		}
		p1->p1_dpd_pmsg = NULL;
		p1->p1_dpd_status = DPD_SUCCESSFUL;
		p1->p1_dpd_time = ssh_time();
		p1->p1_num_dpd_reqsent = 0;
		PRTDBG(D_POL, ("DPD: DPD handshake successful with peer %s",
		    sap(&p1->p1_remote)));
		return;
	}

	/*
	 * Check for the sequence number. We implement a window of
	 * MAX_DPD_RETRIES for acceptable sequence numbers.
	 */

	if (p1->p1_dpd_recv_seqnum != 0 &&
	    (recv_seqnum - p1->p1_dpd_recv_seqnum) > MAX_DPD_RETRIES) {
		PRTDBG(D_POL, ("Recv Sequence number do not match"));
		return;
	}
	p1->p1_dpd_recv_seqnum = recv_seqnum;
	if (!string_to_sockaddr(local_ip, &src_ip)) {
		PRTDBG(D_POL, ("Failed to convert to sockaddr format"));
		return;
	}
	ikesrv = get_server_context(&src_ip);
	if (ikesrv == NULL) {
		PRTDBG(D_POL, ("in.iked context lookup failed %s ",
		    sap(&src_ip)));
		return;
	}

	if ((ssh_rc = ssh_ike_connect_notify(ikesrv->ikesrv_ctx, neg, NULL,
	    NULL, SSH_IKE_DELETE_FLAGS_WANT_ISAKMP_SA, SSH_IKE_DOI_IPSEC,
	    SSH_IKE_PROTOCOL_ISAKMP, spi, spi_size,
	    SSH_IKE_NOTIFY_MESSAGE_R_U_THERE_ACK,
	    notification_data, notification_data_size)) != SSH_IKE_ERROR_OK) {
		PRTDBG(D_POL, ("DPD: R_U_THERE_ACK notify failed code %d (%s).",
		    ssh_rc, ike_connect_error_to_string(ssh_rc)));
	} else {
		PRTDBG(D_POL, ("DPD: R_U_THERE_ACK successfully sent "
		    "to peer %s", sap(&p1->p1_remote)));
	}
}

/*
 * I've a phase 1 status notification.  This notification is not authenticated,
 * but it may have been encrypted.
 */
/* ARGSUSED */
void
ssh_policy_phase_i_notification(SshIkePMPhaseI pm_info, Boolean encrypted,
    SshIkeProtocolIdentifiers protocol_id,
    unsigned char *spi, size_t spi_size,
    SshIkeNotifyMessageType notify_message_type,
    unsigned char *notification_data, size_t notification_data_size)
{
	PRTDBG((D_POL | D_P1), ("Handling P1 status notification from peer."));
	PRTDBG((D_POL | D_P1),
	    ("  NAT-T state %d (%s)",
	    pm_info == NULL ? -2 : pm_info->p1_natt_state,
	    pm_info == NULL ? "No State" :
	    natt_code_string(pm_info->p1_natt_state)));

	switch (notify_message_type) {
	case SSH_IKE_NOTIFY_MESSAGE_CONNECTED:
		/* Handle outside the switch. */
		break;
	case SSH_IKE_NOTIFY_MESSAGE_INITIAL_CONTACT:
		/*
		 * SECURITY NOTE:  Even though I check for encrypted,
		 * the initial-contact may be forged if the adversary can
		 * paste a legitimate value into the CBC ciphertext.  Trust
		 * this one less then the one in ssh_policy_notification.
		 * We may need to delay this call until after Phase 1 is
		 * truly established.
		 */
		if (encrypted)
			handle_initial_contact(pm_info);
		return;

	default:
		/* For now, just return. */
		PRTDBG((D_POL | D_P1), ("  Phase 1 error: code %d (%s).",
		    notify_message_type,
		    ssh_ike_error_code_to_string(notify_message_type)));
		return;
	}

	/* If I reach here we have a working Phase 1 SA. */
	PRTDBG((D_POL | D_P1), ("  Phase 1 SA successfully negotiated."));
}

/*
 * hash of "RFC XXXX" (md5sum, no newline), used in S10 FCS from the last
 * pre-RFC version of the NAT-T draft.
 */
static uint8_t old_rfc_vid[16] = {0x81, 0x0f, 0xa5, 0x65, 0xf8, 0xab,
    0x14, 0x36, 0x91, 0x05, 0xd7, 0x06, 0xfb, 0xd5, 0x72, 0x79};

/*
 * Hash of "RFC 3947" (md5sum, no newline), for official NAT-T RFC support.
 */
static uint8_t rfc_vid[16] = {0x4a, 0x13, 0x1c, 0x81, 0x07, 0x03, 0x58, 0x45,
    0x5c, 0x57, 0x28, 0xf2, 0x0e, 0x95, 0x45, 0x2f};

/*
 * RFC 3706
 */

static uint8_t dpd_vid[14] = {0xAF, 0xCA, 0xD7, 0x13, 0x68, 0xA1, 0xF1, 0xC9,
    0x6B, 0x86, 0x96, 0xFC, 0x77, 0x57};
static uint8_t dpd_mjr = 0x1;
static uint8_t dpd_mnr = 0x0;

static const vid_table_t vid_table[] = {
	/*
	 * Many of these values came from here:
	 *   http://www.cipherica.com/libike/files/vendorid.txt
	 */
	{ "NAT-Traversal (RFC 3947)", "4a131c81070358455c5728f20e95452f" },
	{ "NAT-Traversal (draft-ietf-ipsec-nat-t-ike-09)",
	    "810fa565f8ab14369105d706fbd57279" },
	{ "NAT-Traversal (draft-ietf-ipsec-nat-t-ike-00)",
	    "448552d18b6bbcd0be8a8469579ddcc" },
	{ "NAT-Traversal (draft-ietf-ipsec-nat-t-ike-02 (Draft RFC md5sum))",
	    "90cb80913ebb696e086381b5ec427b1f" },
	{ "NAT-Traversal (draft-ietf-ipsec-nat-t-ike-02 (proper md5))",
	    "cd60464335df21f87cfdb2fc68b6a448" },
	{ "NAT-Traversal (draft-ietf-ipsec-nat-t-ike-03)",
	    "7d9419a65310ca6f2c179d9215529d56" },
	{ "MS NT5 ISAKMPOAKLEY", "1e2b516905991c7d7c96fcbfb587e461" },
	{ "Microsoft IPsec client",
	    "1e2b516905991c7d7c96fcbfb587e46100000002" },
	{ "FRAGMENTATION (NAT-T capable (draft 02) Microsoft IPsec agent)",
	    "4048b7d56ebce88525e7de7f00d6c2d3" },
	{ "Cisco-Unity", "12f5f28c457168a9702d9fe274cc0100" },
	{ "KAME/racoon", "7003cbc1097dbe9c2600ba6983bc8b35" },
	{ "GSSAPI", "621b04bb09882ac1e15935fefa24aeee" },
	{ "XAUTH", "09002689dfd6b712" },
	{ "OpenPGP", "4f70656e5047503130313731" },
	{ "TIMESTEP", "54494d4553544550" },
	{ "CheckPoint", "f4ed19e0c114eb516faaac0ee37daf2897b4381f" },
	{ "CheckPoint VPN-1 4.1",
	    "f4ed19e0c114eb516faaac0ee37daf2897b4381f0000000100000002" },
	{ "CheckPoint VPN-1 4.1 SP-1",
	    "f4ed19e0c114eb516faaac0ee37daf2897b4381f0000000100000003" },
	{ "CheckPoint VPN-1 4.1 SP-2 or above",
	    "f4ed19e0c114eb516faaac0ee37daf2897b4381f0000000100004002" },
	{ "CheckPoint VPN-1 NG",
	    "f4ed19e0c114eb516faaac0ee37daf2897b4381f0000000100005000" },
	{ "CheckPoint VPN-1 NG Feature Pack 1",
	    "f4ed19e0c114eb516faaac0ee37daf2897b4381f0000000100005001" },
	{ "CheckPoint VPN-1 NG Feature Pack 2",
	    "f4ed19e0c114eb516faaac0ee37daf2897b4381f0000000100005002" },
	{ "CheckPoint VPN-1 NG Feature Pack 3",
	    "f4ed19e0c114eb516faaac0ee37daf2897b4381f0000000100005003" },
	{ "CheckPoint VPN-1 NG with Application Intelligence",
	    "f4ed19e0c114eb516faaac0ee37daf2897b4381f0000000100005004" },
	{ "Detecting Dead IKE Peers (RFC 3706)",
	    "afcad71368a1f1c96b8696fc77570100"},
	{ NULL, NULL }
};

static const char *find_vid_description(char *vid)
{
	int i;
	int len;

	len = strlen(vid);

	for (i = 0; vid_table[i].desc; i++)
		if (strlen(vid_table[i].hex_vid) == len)
			if (strncmp(vid, vid_table[i].hex_vid, len) == 0)
				return (vid_table[i].desc);
	return ("Could not find VID description");
}

/*
 * Prints hex value of Vendor ID hash and value, if known
 */
void
print_vid(unsigned char *vendor_id, size_t vendor_id_len)
{
	char *vid_string;
	int i;

	vid_string = ssh_malloc(2 * vendor_id_len + 1);

	for (i = 0; i < vendor_id_len; i++)
		(void) sprintf(&vid_string[2 * i], "%.2x", vendor_id[i]);
	PRTDBG(D_POL, ("Vendor ID from peer:"));
	PRTDBG(D_POL, ("  0x%s", vid_string));
	PRTDBG(D_POL, ("  %s", find_vid_description(vid_string)));
	ssh_free(vid_string);
}

/*
 * Checks vendor IDs.
 * Currently, this is only used to check if an implementation does NAT-T
 * or DPD.
 */
void
ssh_policy_isakmp_vendor_id(SshIkePMPhaseI pm_info, unsigned char *vendor_id,
    size_t vendor_id_len)
{
	phase1_t *p1 = (phase1_t *)pm_info->policy_manager_data;
	if (debug & D_POL)
		print_vid(vendor_id, vendor_id_len);

	if (vendor_id_len != sizeof (rfc_vid)) {
		/*
		 * Both DPD and NAT-T vendor IDs are the same size.
		 */
		return;
	}

	if (memcmp(vendor_id, old_rfc_vid, vendor_id_len) == 0 &&
	    pm_info->p1_use_natt == 0) {
		if (p1->p1_isv4 && do_natt) {
			PRTDBG(D_POL, ("  Using NAT-D (draft VID)"));
			pm_info->p1_use_natt = 1;
		}
		return;
	}

	/*
	 * No need for p1_use_natt check in this case.  We always prefer
	 * using the RFC versions of things if at all possible.
	 */
	if (memcmp(vendor_id, rfc_vid, vendor_id_len) == 0) {
		if (p1->p1_isv4 && do_natt) {
			PRTDBG(D_POL, ("  Using NAT-D (RFC 3947 VID)"));
			pm_info->p1_use_natt = 2;
		}
		return;
	}

	if (memcmp(vendor_id, dpd_vid, sizeof (dpd_vid)) == 0) {
		unsigned char *tmp_vid_ptr = vendor_id + sizeof (dpd_vid);
		if (tmp_vid_ptr[0] == dpd_mjr && tmp_vid_ptr[1] == dpd_mnr) {
			phase1_t *p1 = pm_info->policy_manager_data;
			PRTDBG(D_POL,
			    ("  Using Dead Peer Detection (RFC 3706)"));
			p1->p1_use_dpd = B_TRUE;
			p1->p1_dpd_sent_seqnum = (uint32_t)rand();
		}
	}
}

/*
 * Send out vendor IDs.
 * Currently, this is only used for NAT-Traversal indication.
 */
void
ssh_policy_isakmp_request_vendor_ids(SshIkePMPhaseI pm_info,
    SshPolicyRequestVendorIDsCB callback_in, void *callback_context_in)
{
	size_t *out_len;
	unsigned char **out_vid_l;
	phase1_t *p1 = (phase1_t *)pm_info->policy_manager_data;
	unsigned char *tmp_vid_ptr;

	PRTDBG(D_POL, ("Sending out Vendor IDs, if needed: "
	    "NAT-T state %d (%s)", pm_info->p1_natt_state,
	    natt_code_string(pm_info->p1_natt_state)));

	if (!do_natt || !p1->p1_isv4) {
bail:
		callback_in(0, ssh_malloc(0), ssh_malloc(0),
		    callback_context_in);
		return;
	}

	out_len = ssh_malloc(3 * sizeof (size_t));
	if (out_len == NULL)
		goto bail;
	out_len[0] = sizeof (rfc_vid);
	out_vid_l = ssh_malloc(3 * sizeof (unsigned char *));
	if (out_vid_l == NULL) {
		ssh_free(out_len);
		goto bail;
	}
	out_vid_l[0] = ssh_malloc(sizeof (rfc_vid));
	if (out_vid_l[0] == NULL) {
		ssh_free(out_len);
		ssh_free(out_vid_l);
		goto bail;
	}
	(void) memcpy(out_vid_l[0], rfc_vid, sizeof (rfc_vid));

	out_len[1] = sizeof (old_rfc_vid);
	out_vid_l[1] = ssh_malloc(sizeof (old_rfc_vid));
	if (out_vid_l[1] == NULL) {
		ssh_free(out_vid_l[0]);
		ssh_free(out_len);
		ssh_free(out_vid_l);
		goto bail;
	}
	(void) memcpy(out_vid_l[1], old_rfc_vid, sizeof (old_rfc_vid));

	out_len[2] = sizeof (dpd_vid) + sizeof (dpd_mjr) + sizeof (dpd_mnr);
	out_vid_l[2] = ssh_malloc(sizeof (dpd_vid) + sizeof (dpd_mjr) +
	    sizeof (dpd_mnr));
	if (out_vid_l[2] == NULL) {
		ssh_free(out_vid_l[1]);
		ssh_free(out_vid_l[0]);
		ssh_free(out_vid_l);
		ssh_free(out_len);
		goto bail;
	}
	tmp_vid_ptr = out_vid_l[2];
	(void) memcpy(out_vid_l[2], dpd_vid, sizeof (dpd_vid));
	tmp_vid_ptr += sizeof (dpd_vid);
	(void) memcpy(tmp_vid_ptr, &dpd_mjr, sizeof (dpd_mjr));
	tmp_vid_ptr += sizeof (dpd_mjr);
	(void) memcpy(tmp_vid_ptr, &dpd_mnr, sizeof (dpd_mnr));
	callback_in(3, out_vid_l, out_len, callback_context_in);
}

/*
 * How many bytes of nonce data should we create?
 */
void
ssh_policy_isakmp_nonce_data_len(SshIkePMPhaseI pm_info,
    SshPolicyNonceDataLenCB callback_in, void *callback_context_in)
{
	phase1_t *p1 = pm_info->policy_manager_data;
	struct ike_rule *rp = p1->p1_rule;

	PRTDBG(D_POL, ("Determining P1 nonce data length."));
	PRTDBG(D_POL, ("  NAT-T state %d (%s)", pm_info->p1_natt_state,
	    natt_code_string(pm_info->p1_natt_state)));

	callback_in(rp->p1_nonce_len, callback_context_in);
}

/*
 * Finish pinning up the phase1_t here.
 *
 * Called when we are initiator or responder, when Phase I is complete (or
 * failed).
 */
void
ssh_policy_negotiation_done_isakmp(SshIkePMPhaseI pm_info,
    SshIkeNotifyMessageType code)
{
	struct sockaddr_storage remote, local;
	ike_server_t *server;
	int rc;
	phase1_t *p1 = pm_info->policy_manager_data;

	PRTDBG(D_POL, ("Finishing P1 negotiation: NAT-T state %d (%s)",
	    pm_info->p1_natt_state, natt_code_string(pm_info->p1_natt_state)));

	if (code != SSH_IKE_NOTIFY_MESSAGE_CONNECTED) {
		PRTDBG((D_POL | D_P1),
		    ("Phase 1 negotiation error: code %d (%s).", code,
		    ssh_ike_error_code_to_string(code)));
		/*
		 * The library is sometimes slow cleaning up the phase1_t
		 * hanging off things.
		 *
		 * Hopefully that'll not cause too many lingering phase1_t's.
		 *
		 * Chalk this up as a failed attempt
		 */
		if (pm_info->this_end_is_initiator) {
			if (code == SSH_IKE_NOTIFY_MESSAGE_TIMEOUT)
				ikestats.st_init_p1_noresp++;
			else
				ikestats.st_init_p1_respfail++;
		} else {
			ikestats.st_resp_p1_fail++;
		}
		return;
	}
	/*
	 * We have a successfully created SA.  Update counters!
	 */
	if (pm_info->this_end_is_initiator) {
		ikestats.st_init_p1_current++;
		ikestats.st_init_p1_total++;
	} else {
		ikestats.st_resp_p1_current++;
		ikestats.st_resp_p1_total++;
	}

	/*
	 * Construct sockaddrs, find IKE server, and update its addrcache.
	 */

	if (!string_to_sockaddr(pm_info->remote_ip, &remote)) {
		PRTDBG((D_POL | D_P1), ("  Failed to convert remote address "
		    "to sockaddr."));
		return;
	}

	if (!string_to_sockaddr(pm_info->local_ip, &local)) {
		PRTDBG((D_POL | D_P1), ("  Failed to convert local address "
		    "to sockaddr."));
		return;
	}

	server = get_server_context(&local);
	if (server == NULL) {
		PRTDBG((D_POL | D_P1), ("  Couldn't find in.iked context for "
		    "local address."));
		return;
	}

#if 0
	PRTDBG((D_POL | D_P1),
	    ("pm_info says phase 1 sa lifetime is %llu (%llu - %llu).",
	    pm_info->sa_expire_time - pm_info->sa_start_time,
	    pm_info->sa_expire_time, pm_info->sa_start_time));
#endif

	if (pm_info->this_end_is_initiator) {
		/*
		 * Wire pm_info and the phase1 together.  For responders
		 * this was already done in create_receiver_phase1.
		 */
		p1->p1_pminfo = pm_info;

		/*
		 * If we were expecting a particular remote identity (because
		 * the kernel policy wants it or we are re-keying existing SAs)
		 * make sure that the remote gave us the identity we were
		 * expecting.  Otherwise, just use whatever was sent.
		 */
#ifdef NOTYET
		if (p1->p1_remoteid != NULL)
			check_remote_id(p1->p1_remoteid, pm_info->remote_id);
		else
#endif
			p1->p1_remoteid =
			    payloadid_to_pfkey(pm_info->remote_id, B_FALSE);
	} else {
		/* ID stuff we will need later for PF_KEY processing */
		p1->p1_localid =
		    payloadid_to_pfkey(pm_info->local_id, B_TRUE);
		p1->p1_remoteid =
		    payloadid_to_pfkey(pm_info->remote_id, B_FALSE);

		/*
		 * Also check if this is an initial contact for us...
		 * NOTE: We may wish to move this outside the initiator/
		 * receiver check because this way, the INITIAL-CONTACT would
		 * be always authenticated by the now-complete Phase 1 SA.
		 */
		if ((addrcache_check(&server->ikesrv_addrcache, &remote)) ==
		    NULL) {
			rc = ssh_ike_connect_notify(server->ikesrv_ctx,
			    pm_info->negotiation, NULL, NULL,
			    SSH_IKE_NOTIFY_FLAGS_WANT_ISAKMP_SA,
			    SSH_IKE_DOI_IPSEC, SSH_IKE_PROTOCOL_ISAKMP,
			    (uchar_t *)pm_info->cookies,
			    sizeof (pm_info->cookies),
			    SSH_IKE_NOTIFY_MESSAGE_INITIAL_CONTACT,
			    (uchar_t *)"", 0);
			if (rc != SSH_IKE_ERROR_OK) {
				EXIT_FATAL2("Receiver initial-contact: "
				    "error (%s)",
				    ike_connect_error_to_string(rc));
			}
		}
	}

	/* Cache P1 oakley group */
	p1->p1_group = (pm_info->negotiation != NULL) ?
	    (int)get_ssh_dhgroup(pm_info->negotiation) : 0;

	/* No longer a larval phase1_t! */
	p1->p1_complete = B_TRUE;

	/*
	 * At this point, there could be rules remaining in the rulebase
	 * that are incompatible with the selected rule (different modes, for
	 * example) or with the actual remote and local identities.  Currently,
	 * the rulebase is never refered to after this point, but someday we
	 * might start using it for something, such as attaching a list of
	 * compatible kmcookies (instead of just one) to SADB_ADD messages.  If
	 * so, this would be a good place to further constrain the rulebase to
	 * conform to the selected rule and to the details of the phase I
	 * connection, such as remote and local identities, so that the rulebase
	 * contains only rules that are compatible with the connection.
	 */

	/* pm_info->sa_expire_time should have what we're looking for. */

	addrcache_add(&server->ikesrv_addrcache, &remote,
	    pm_info->sa_expire_time);
}

/*
 * Checks off-the-wire attributes vs. a rule-specified transform. Returns
 * TRUE if there is a match, FALSE otherwise.
 * The pti (proposal transform index) and verbose arguments are only used
 * for debug prints.
 *
 * SIDE-EFFECT: Writes a non-zero default value in to attrs for key_length IF
 * the appropriate cipher (for now, AES) is selected.
 */
static boolean_t
attrs_vs_rules(SshIkeAttributes attrs, struct ike_xform *rulesuite,
    int pti, boolean_t verbose)
{
	char bitstr[32];

	if (verbose) {
		if (debug & (D_POL | D_P1))
			(void) ssh_snprintf(bitstr, sizeof (bitstr),
			    "\n\tkey_length = %d bits",
			    attrs->key_length);
		PRTDBG((D_POL | D_P1), ("Peer Proposal: transform %d\n"
		    "\tauth_method = %d (%s)\n"
		    "\thash_alg = %d (%s)\n"
		    "\tencr_alg = %d (%s)%s\n "
		    "\toakley_group = %d",
		    pti, attrs->auth_method,
		    ike_auth_method_to_string(attrs->auth_method),
		    attrs->hash_algorithm,
		    ike_hash_alg_to_string(attrs->hash_algorithm),
		    attrs->encryption_algorithm,
		    ike_encryption_alg_to_string(
		    attrs->encryption_algorithm),
		    attrs->key_length != 0 ? bitstr : "",
		    attrs->group_desc->descriptor));
	}
	if (attrs->auth_method != rulesuite->auth_method)
		return (B_FALSE);
	if (attrs->hash_algorithm != rulesuite->auth_alg)
		return (B_FALSE);
	if (attrs->encryption_algorithm != rulesuite->encr_alg)
		return (B_FALSE);
	if (attrs->group_desc->descriptor != rulesuite->oakley_group)
		return (B_FALSE);

	/*
	 * Check algorithm size ranges for AES only for now.
	 * Broaden this case if things come to it.
	 * Remember, be liberal in what you receive.  No indicated
	 * key size should default to 128-bits (for now).
	 */
	if (rulesuite->encr_alg == SSH_IKE_VALUES_ENCR_ALG_AES_CBC) {
		if (attrs->key_length == 0)
			attrs->key_length = 128;

		if (attrs->key_length < rulesuite->encr_low_bits ||
		    attrs->key_length > rulesuite->encr_high_bits ||
		    (attrs->key_length != 256 && attrs->key_length != 128 &&
		    attrs->key_length != 192))
			return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Given a vector of ISAKMP (phase 1) transform attribute sets received from
 * a remote initiator, see if we support at least one of them.
 * Use the current rulebase to check if a rule matches one of the phase 1
 * transforms.  Select the first transform that matches at least one rule.
 * Returns the index of the selected transform, or -1 if no rule matched any
 * proposed transform.
 */
static int
phase1_reality(SshIkePMPhaseI pm_info, struct SshIkeAttributesRec *attrsv,
    int num_transforms)
{
	int ri;				/* rule index */
	int rti;			/* rule transform index */
	int pti;			/* proposal transform index */
	int selected_pti;		/* selected proposal transform index */
	phase1_t *p1 = (phase1_t *)pm_info->policy_manager_data;
	SshIkeAttributes attrs;
	uint16_t descriptor;

	PRTDBG(D_P1, ("Checking P1 transform from remote initiator!"));

	/*
	 * Make sure we have a phase1_t...
	 */
	if (p1 == NULL) {
		PRTDBG(D_P1, ("  No P1 transform data from remote initiator!"));
		return (-1);
	}

	PRTDBG(D_P1, ("  NAT-T state %d (%s)",
	    pm_info == NULL ? -2 : pm_info->p1_natt_state,
	    pm_info == NULL ? "No State" :
	    natt_code_string(pm_info->p1_natt_state)));

	/*
	 * Check for unsupported transforms.  We use (encryption_algorithm == 0)
	 * to indicate an unsupported transform (or one that was rejected
	 * earlier by ssh_ike_read_isakmp_attrs).
	 */
	for (pti = 0; pti < num_transforms; ++pti) {
		attrs = &attrsv[pti];

		if (attrs->encryption_algorithm == 0)
			continue;

		/*
		 * Check if the group is in the list of supported Oakley groups.
		 */

		if (attrs->group_desc == NULL) {
			PRTDBG((D_POL | D_P1),
			    ("  No oakley group given."));
			continue;
		}

		descriptor = attrs->group_desc->descriptor;

		if (!ike_group_supported(descriptor)) {
			PRTDBG((D_POL | D_P1), ("  Unsupported "
			    "oakley group %d", descriptor));
			attrs->encryption_algorithm = 0;
			continue;
		}

		/*
		 * Check if the hash algorithm is in the list of supported
		 * hashes.
		 */
		if (!ike_hash_supported(attrs->hash_algorithm)) {
			PRTDBG((D_POL | D_P1),
			    ("  Unsupported hash algorithm %d (%s)",
			    attrs->hash_algorithm,
			    ike_hash_alg_to_string(attrs->hash_algorithm)));
			attrs->encryption_algorithm = 0;
			continue;
		}

		/*
		 * Check if the encryption algorithm is in the list of supported
		 * encryption algorithms.
		 */
		if (!ike_cipher_supported(attrs->encryption_algorithm)) {
			PRTDBG((D_POL | D_P1), ("  Unsupported encryption "
			    "algorithm %d (%s)", attrs->encryption_algorithm,
			    ike_encryption_alg_to_string(
			    attrs->encryption_algorithm)));
			attrs->encryption_algorithm = 0;
			continue;
		}
	}

	/*
	 * At this point, use the information in the rulebase to see if this
	 * is an acceptable proposal.
	 *
	 * Attributes include:	hash, cipher, auth method and Oakley group,
	 *
	 * Go through the p1->p1_rulebase and eliminate all incompatible rules.
	 * If there's at least one rule left afterward, the proposal is
	 * acceptable (so far).
	 */

	selected_pti = -1;		/* >= 0 when we select a transform */

	/*
	 * Discard rules that don't match any of the proposed transforms.  When
	 * we're through with this loop, we've discarded all the rules, or we've
	 * selected a transform that matches (at least) one rule.
	 */
	for (ri = 0; ri < p1->p1_rulebase.num_rules; ++ri) {
		struct ike_rule *rp = p1->p1_rulebase.rules[ri];
		if (rp == NULL)
			continue;

		for (rti = 0; rti < rp->num_xforms; ++rti) {
			struct ike_xform *rulesuite = rp->xforms[rti];
			char bitstr[32];

			if (debug & (D_POL | D_P1))
				(void) ssh_snprintf(bitstr, sizeof (bitstr),
				    "\n\tkeysizes = %d..%d bits",
				    rulesuite->encr_low_bits,
				    rulesuite->encr_high_bits);
			PRTDBG((D_POL | D_P1),
			    ("P1 Transform check\n"
			    "\tRule \"%s\", transform %d: \n"
			    "\tauth_method = %d (%s)\n\thash_alg = %d (%s)\n"
			    "\tencr_alg = %d (%s)%s\n\toakley_group = %d",
			    rp->label, rti,
			    rulesuite->auth_method,
			    ike_auth_method_to_string(rulesuite->auth_method),
			    rulesuite->auth_alg,
			    ike_hash_alg_to_string(rulesuite->auth_alg),
			    rulesuite->encr_alg,
			    ike_encryption_alg_to_string(rulesuite->encr_alg),
			    rulesuite->encr_low_bits == 0 ? "" : bitstr,
			    rulesuite->oakley_group));
			for (pti = 0; pti < num_transforms; ++pti) {
				attrs = &attrsv[pti];

				if (attrs->encryption_algorithm == 0)
					continue;

				if (attrs->group_desc == NULL) {
					PRTDBG((D_POL | D_P1),
					    ("phase1_reality: no group\n"));
					continue;
				}

				if (attrs_vs_rules(attrs, rulesuite, pti,
				    B_TRUE)) {
					selected_pti = pti;
					PRTDBG((D_POL | D_P1),
					    ("  Rule \"%s\" matches proposal.",
					    rp->label));
					break;
				}
				PRTDBG((D_POL | D_P1),
				    ("  Rule \"%s\" does not match proposal.",
				    rp->label));
			}

			if (selected_pti >= 0)
				break;
		}

		if (selected_pti >= 0)
			break;

		PRTDBG((D_POL | D_P1),
		    ("  Rule \"%s\", does not match any transforms "
		    "sent from peer.", rp->label));
		ri += rulebase_delete_rule(&p1->p1_rulebase, ri);
	}

	if (selected_pti < 0) {
		PRTDBG((D_POL | D_P1),
		    ("  No rules match peer proposal."));
		return (-1);		/* No rules match the proposal. */
	}

	/*
	 * Now we have selected one of the proposed transforms, and found the
	 * first matching rule.  The selected transform's index is in
	 * selected_pti and the first rule's index is in ri.
	 * Go through the remaining rules [ri+1..num_rules) and delete any that
	 * are inconsistent with the selected transform.
	 */
	for (++ri; ri < p1->p1_rulebase.num_rules; ++ri) {
		struct ike_rule *rp = p1->p1_rulebase.rules[ri];
		if (rp == NULL)
			continue;

		for (rti = 0; rti < rp->num_xforms; ++rti) {
			struct ike_xform *rulesuite = rp->xforms[rti];

			/*
			 * Don't print out debug info here, it is
			 * not relevant and confusing.  A simple
			 * rule deletion notice at the end is much
			 * more clear.
			 */

			if (attrs_vs_rules(attrs, rulesuite, 0, B_FALSE))
				break;
		}
		if (rti == rp->num_xforms) { /* didn't match */
			PRTDBG((D_POL | D_P1),
			    ("\tRule \"%s\" has no matching "
			    "transforms, deleting.",
			    rp->label));
			ri += rulebase_delete_rule(&p1->p1_rulebase, ri);
		}
	}

	PRTDBG((D_POL | D_P1),
	    ("  Selected Proposal Transform %d.", selected_pti));

	return (selected_pti);
}

/*
 * Select a transform from the inbound ISAKMP (Phase 1) proposal.
 *
 * Use what's in the pm_info and the rulebase to select the most appropriate
 * transform.
 */
void
ssh_policy_isakmp_select_sa(SshIkePMPhaseI pm_info,
    SshIkeNegotiation negotiation, SshIkePayload sa_in,
    SshPolicySACB callback_in, void *callback_context_in)
{
	phase1_t *p1 = (phase1_t *)pm_info->policy_manager_data;
	struct SshIkeAttributesRec *attrsv;
	int *transforms_index = NULL;
	int proposal_index = -1, number_of_protocols = 0;
	int i;
	SshIkePayloadSA sa = &(sa_in->pl.sa);
	SshIkePayloadP proposals;
	SshIkePayloadPProtocol protocols;
	SshIkePayloadT transforms;
	struct ike_rule *rp;

	PRTDBG((D_POL | D_P1),
	    ("Selecting transform from inbound SA..."));

	PRTDBG((D_POL | D_P1),
	    ("  NAT-T state %d (%s)", pm_info->p1_natt_state,
	    natt_code_string(pm_info->p1_natt_state)));

	/*
	 * NOTE:  We only should ever see one proposal and one protocol for
	 * Phase 1, so we should only cycle through the transforms.
	 */

	if (sa->doi != SSH_IKE_DOI_IPSEC) {
		PRTDBG((D_POL | D_P1),
		    ("  Non IPsec DOI (%d) packet.", sa->doi));
		goto bail;
	}

	if ((sa->situation.situation_flags & ~SSH_IKE_SIT_IDENTITY_ONLY) != 0) {
		PRTDBG((D_POL | D_P1),
		    ("  integrity/secrecy situation flags."));
		goto bail;
	}

	if (sa->number_of_proposals != 1) {
		PRTDBG((D_POL | D_P1),
		    ("  Too many/few proposals for phase 1 (%d instead of 1)!",
		    sa->number_of_proposals));
		goto bail;
	}
	proposals = sa->proposals;

	/*
	 * For now, walk the sa_in and select one transform from
	 * the protocol in the proposal.
	 */
	protocols = proposals[0].protocols;
	number_of_protocols = proposals[0].number_of_protocols;
	if (number_of_protocols != 1) {
		PRTDBG((D_POL | D_P1),
		    ("  Too many/few protocols for phase 1 (%d instead of 1)!",
		    number_of_protocols));
		goto bail;
	}
	transforms_index = ssh_calloc(1, sizeof (int));
	if (transforms_index == NULL) {
		PRTDBG((D_POL | D_P1),
		    ("  Out of memory allocating transform index."));
		goto bail;
	}

	/* And we know spi_size and spi here, too. */
	transforms = protocols[0].transforms;
	transforms_index[0] = -1;

	/* Build an array of attribute records. */
	attrsv = ssh_calloc(protocols[0].number_of_transforms,
	    sizeof (*attrsv));
	if (attrsv == NULL) {
		ssh_free(transforms_index);
		transforms_index = NULL;
		PRTDBG((D_POL | D_P1),
		    ("  Out of memory allocating attribute record array."));
		goto bail;
	}

	for (i = 0; i < protocols[0].number_of_transforms; i++) {
		ssh_ike_clear_isakmp_attrs(&attrsv[i]);
		/* For each transform... */
		/* And we know transform_id here... */
		if (transforms[i].transform_id.isakmp !=
		    SSH_IKE_ISAKMP_TRANSFORM_KEY_IKE) {
			PRTDBG((D_POL | D_P1),
			    ("  Non IKE Phase 1 transform."));
			continue;
		}
		if (!ssh_ike_read_isakmp_attrs(negotiation, (transforms + i),
		    &attrsv[i])) {
			PRTDBG((D_POL | D_P1),
			    ("  Library-defined unsupported attrs."));
			continue;
		}
	}

	transforms_index[0] =
	    phase1_reality(pm_info, attrsv, protocols[0].number_of_transforms);

	PRTDBG((D_POL | D_P1),
	    ("  Sending selected SA with transforms_index %d to library.",
	    transforms_index[0]));

	pm_info->sa_start_time = ssh_time();
	if (transforms_index[0] >= 0) {
		SshIkeAttributes attrs = &attrsv[transforms_index[0]];
		proposal_index = 0;

		/*
		 * Set expiration time here. Default lifetimes set
		 * in gram.y
		 */
		pm_info->sa_expire_time = pm_info->sa_start_time +
		    attrs->life_duration_secs;
		p1->p1_p2_group = attrs->group_desc->descriptor;
	}


	/*
	 * Here we latch in the first rule that still remains after the above
	 * constraints, and use it to select our local certificate.
	 */

	rp = NULL;
	for (i = 0; i < p1->p1_rulebase.num_rules; ++i) {
		rp = p1->p1_rulebase.rules[i];

		if (rp != NULL && rp->label != NULL)
			break;
	}

	p1->p1_rule = rp;
	if (rp == NULL) {
		proposal_index = -1;
	} else if (is_system_labeled()) {
		fix_p1_label_range(p1);
	}
	ssh_free(attrsv);

bail:
	callback_in(proposal_index, number_of_protocols, transforms_index,
	    callback_context_in);
}

/*
 * This callback is telling us that Phase 2 negotiation was freed. This can be
 * called in the process of sending notify message or as a result of deleting
 * negotiation.
 */
void
ssh_policy_negotiation_done_phase_ii(SshIkePMPhaseII pm_info,
    SshIkeNotifyMessageType code)
{

	if (code == SSH_IKE_NOTIFY_MESSAGE_CONNECTED) {
		PRTDBG((D_POL | D_P2), ("Phase 2 negotiation completed."));
		return;
	}

	if (code == SSH_IKE_NOTIFY_MESSAGE_ABORTED) {
		PRTDBG((D_POL | D_P2), ("Phase 2 negotiation deleted"));
		return;
	}

	PRTDBG((D_POL | D_P2), ("Phase 2 negotiation failed: code %d (%s).",
	    code, ssh_ike_error_code_to_string(code)));
	/* All of these fields are valid according to SSH documentation. */
	assert(pm_info->local_ip != NULL && pm_info->remote_ip != NULL);
	PRTDBG((D_POL | D_P2), ("  Local IP: %s[%s], Remote IP: %s[%s]",
	    pm_info->local_ip, pm_info->local_port,
	    pm_info->remote_ip, pm_info->remote_port));
}

void
ssh_policy_negotiation_done_qm(SshIkePMPhaseQm pm_info,
    SshIkeNotifyMessageType code)
{

	if (code == SSH_IKE_NOTIFY_MESSAGE_CONNECTED) {
		PRTDBG((D_POL | D_P2), ("Quick Mode negotiation completed."));
		return;
	}

	PRTDBG((D_POL | D_P2), ("Quick Mode negotiation failed: code %d (%s).",
	    code, ssh_ike_error_code_to_string(code)));
	/* All of these fields are valid according to SSH documentation. */
	assert(pm_info->local_ip != NULL && pm_info->remote_ip != NULL &&
	    pm_info->local_i_id_txt != NULL &&
	    pm_info->local_r_id_txt != NULL &&
	    pm_info->remote_i_id_txt != NULL &&
	    pm_info->remote_r_id_txt != NULL);
	PRTDBG((D_POL | D_P2), ("  Local IP: %s[%s], Remote IP: %s[%s]",
	    pm_info->local_ip, pm_info->local_port,
	    pm_info->remote_ip, pm_info->remote_port));
	PRTDBG((D_POL | D_P2), ("  %sInitiator Local ID = %s",
	    pm_info->this_end_is_initiator ? "** " : "",
	    pm_info->local_i_id_txt));
	PRTDBG((D_POL | D_P2), ("  %sInitiator Remote ID = %s",
	    pm_info->this_end_is_initiator ? "** " : "",
	    pm_info->remote_i_id_txt));
	PRTDBG((D_POL | D_P2), ("  %sResponder Local ID = %s",
	    pm_info->this_end_is_initiator ? "" : "** ",
	    pm_info->local_r_id_txt));
	PRTDBG((D_POL | D_P2), ("  %sResponder Remote ID = %s",
	    pm_info->this_end_is_initiator ? "" : "** ",
	    pm_info->remote_r_id_txt));
}

/*
 * Make a policy decision about an incoming phase 1 connection (we are the
 * responder) based on the pm_info structure fields.
 * Available at this point are:
 *
 *	cookies
 *	local_ip
 *	local_port
 *	remote_ip
 *	remote_port
 *	major_version
 *	minor_version
 *	exchange_type
 *
 * Make a decision based on those.  Call callback_in() with results.
 * Probably only drop at THIS POINT because of memory leakage, or
 * some such resource starvation issue.  Dropping here means no
 * notification to the initiator.
 */
void
ssh_policy_new_connection(SshIkePMPhaseI pm_info,
    SshPolicyNewConnectionCB callback_in, void *callback_context_in)
{
	Boolean allow_connection = TRUE;
	/* -1 means use default settings.  Set those in */
	/* main.c:init_ike_params(). */
	SshUInt32 compat_flags = SSH_IKE_FLAGS_USE_DEFAULTS;
	SshInt32 retry_limit = -1;
	SshInt32 retry_timer = -1;
	SshInt32 retry_timer_usec = -1;
	SshInt32 retry_timer_max = -1;
	SshInt32 retry_timer_max_usec = -1;
	SshInt32 expire_timer = -1;
	SshInt32 expire_timer_usec = -1;

	PRTDBG(D_POL,
	    ("New incoming phase 1 from %s[%s].", pm_info->remote_ip,
	    pm_info->remote_port));

	PRTDBG(D_POL, ("  NAT-T state %d (%s)",
	    pm_info == NULL ? -2 : pm_info->p1_natt_state,
	    pm_info == NULL ? "No State" :
	    natt_code_string(pm_info->p1_natt_state)));

	/* update global stats */
	ikestats.st_resp_p1_attempts++;

	/* Check to see if we have any rules. */
	if (rules.num_rules < 1) {
		PRTDBG((D_POL | D_P1),
		    ("  Rejecting inbound phase 1: no rules."));
		allow_connection = FALSE;
	}

	if (allow_connection) {
		/*
		 * Create a phase1_t for a responder.
		 * Initiator phase1_ts are created in phase1_notify().
		 */
		create_receiver_phase1(pm_info);
	}

out:
	callback_in(allow_connection, compat_flags, retry_limit, retry_timer,
	    retry_timer_usec, retry_timer_max, retry_timer_max_usec,
	    expire_timer, expire_timer_usec, callback_context_in);
}

void
ssh_policy_new_connection_phase_ii(SshIkePMPhaseII pm_info,
    SshPolicyNewConnectionCB callback_in, void *callback_context_in)
{
	PRTDBG((D_POL | D_P1),
	    ("New Phase 2 negotiation received, type = %d (%s).",
	    pm_info->exchange_type, ike_xchg_type_to_string(
	    pm_info->exchange_type)));

	/*
	 * For now, tell the callback not to accept this.
	 */
	callback_in(FALSE, 0, 0, 0, 0, 0, 0, 0, 0, callback_context_in);
}

/*
 * For now, copy ssh_policy_new_connection().  We may need to apply more
 * fine-grained policy checks later.  Especially since we can probably look
 * at ipsecconf(1m) related kernel data!
 */
void
ssh_policy_new_connection_phase_qm(SshIkePMPhaseQm pm_info,
    SshPolicyNewConnectionCB callback_in, void *callback_context_in)
{
	Boolean allow_connection = TRUE;
	/* -1 means use default settings.  Set those in */
	/* main.c:init_ike_params(). */
	SshUInt32 compat_flags = SSH_IKE_FLAGS_USE_DEFAULTS;
	SshInt32 retry_limit = -1;
	SshInt32 retry_timer = -1;
	SshInt32 retry_timer_usec = -1;
	SshInt32 retry_timer_max = -1;
	SshInt32 retry_timer_max_usec = -1;
	SshInt32 expire_timer = -1;
	SshInt32 expire_timer_usec = -1;

	PRTDBG((D_POL | D_P2),
	    ("New Quick Mode (QM) connection received from %s[%s]",
	    pm_info->remote_ip, pm_info->remote_port));

	callback_in(allow_connection, compat_flags, retry_limit, retry_timer,
	    retry_timer_usec, retry_timer_max, retry_timer_max_usec,
	    expire_timer, expire_timer_usec, callback_context_in);
}

/* ARGSUSED */	/* We don't support New Group Mode for now. */
void
ssh_policy_ngm_select_sa(SshIkePMPhaseII pm_info,
    SshIkeNegotiation negotiation, SshIkePayload sa_in,
    SshPolicySACB callback_in, void *callback_context_in)
{
	PRTDBG((D_POL | D_P2), ("New Group Mode not supported."));

	/*
	 * Tell the callback it failed for now.
	 */
	callback_in(-1, 0, NULL, callback_context_in);
}

/*
 * Handle an ISAKMP delete request.
 * Do not do so unless authenticated == TRUE.
 */
void
ssh_policy_delete(SshIkePMPhaseII pm_info,
    Boolean authenticated, SshIkeProtocolIdentifiers protocol_id,
    int number_of_spis, unsigned char **spis, size_t spi_size)
{
	int i;
	uint32_t spi;
	struct sockaddr_storage sa;

	PRTDBG((D_POL | D_P2), ("Got ISAKMP delete request: "
	    "%d SPIs of protocol %d", number_of_spis, protocol_id));

	if (!authenticated) {
		PRTDBG((D_POL | D_P2),
		    ("  Ignoring unauthenticated ISAKMP DELETE message."));
		/* TODO AUDIT print more stuff. */
		return;
	}

	/* Assume that protocol_id is from the DOI, and that PF_KEY is too. */
	switch (protocol_id) {
	case SADB_SATYPE_ESP:
	case SADB_SATYPE_AH:
		if (spi_size == sizeof (uint32_t))
			break;
		/* FALLTHRU */
	default:
		PRTDBG((D_POL | D_P2), ("  Unexpected spi size %u",
		    (uint_t)spi_size));
		return;
	}

	/*
	 * You know the drill.  Take the pm_info, derive a sockaddr from
	 * the source (in this case, since it's someone giving ME a DELETE
	 * notification), and call a PF_KEY wrapper function.
	 *
	 * Why aren't we grabbing the other sockaddr?  IPsec SAs (which is
	 * what this notification is about) are only defined by <AH/ESP,
	 * dst_addr, SPI>.  Obtaining the src_addr for the SA is superfluous.
	 * Furthermore, the DELETE notification only applies to the DELETE
	 * sender's local address space.  Combined with the fact that we
	 * only accept authenticated DELETE notifications, we can trust that
	 * nothing bad is happening.
	 *
	 * If we ever deal with DELETE notifications that deal with SAs that
	 * _require_ a source address as part of their definition, then we'll
	 * have to revisit this assumption.
	 */

	if (!string_to_sockaddr(pm_info->remote_ip, &sa))
		return;

	for (i = 0; i < number_of_spis; i++) {
		(void) memcpy(&spi, spis[i], spi_size);
		delete_assoc(spi, NULL, &sa, NULL, NULL, protocol_id);
	}
}

/*
 * Handle RESPONDER-LIFETIME notify message from the peer and update local
 * SAs with the lifetimes from the message which are smaller than the
 * lifetimes specified by local policy.
 */
static void
handle_responder_lifetime(uchar_t *ip_addr,
    SshIkeProtocolIdentifiers protocol_id,
    unsigned char *spi, size_t spi_size, unsigned char *notification_data,
    size_t notification_data_size, phase1_t *p1)
{
	int i;
	uint32_t *life = NULL;
	struct ike_rule newlife;
	char rule_label[] = {"Responder Lifetime"};
	boolean_t updated_secs = B_TRUE;
	boolean_t updated_kb = B_TRUE;
	uint32_t spi_val;
	struct sockaddr_storage sa, src_ip;
	char byte_str[BYTE_STR_SIZE]; /* byte lifetime string representation */
	char secs_str[SECS_STR_SIZE]; /* buffer for seconds representation */
	char kb_str[40];
	ike_server_t *ikesrv;
	SshIkeErrorCode ssh_rc;
	SshIkeNotifyMessageType msg_type =
	    SSH_IKE_NOTIFY_MESSAGE_ATTRIBUTES_NOT_SUPPORTED;

	PRTDBG(D_POL, ("Handling responder lifetime notification from %s.",
	    ip_addr));

	if (protocol_id != SSH_IKE_PROTOCOL_IPSEC_AH &&
	    protocol_id != SSH_IKE_PROTOCOL_IPSEC_ESP) {
		PRTDBG(D_POL, ("Responder lifetime notification - Invalid "
		    "protocol (%d).", protocol_id));
		return;
	}

	(void) memset(&newlife, 0, sizeof (struct ike_rule));
	newlife.label = rule_label;

	for (i = 0; i + 4 <= notification_data_size; i +=
	    ssh_ike_decode_data_attribute_size(notification_data + i, 0L)) {
		uint16_t type;
		uint32_t value;

		if (!ssh_ike_decode_data_attribute_int(notification_data + i,
		    notification_data_size - i, &type, &value, 0L)) {
			/* override default message type */
			msg_type =
			    SSH_IKE_NOTIFY_MESSAGE_UNEQUAL_PAYLOAD_LENGTHS;
			goto unsupported;
		}

		switch (type) {
		case IPSEC_CLASSES_SA_LIFE_TYPE: /* Life type selector */
			if (life != NULL) {
				goto unsupported;
			}
			if (value == SSH_IKE_VALUES_LIFE_TYPE_SECONDS) {
				life = &newlife.p2_lifetime_secs;
			} else if (value ==
			    SSH_IKE_VALUES_LIFE_TYPE_KILOBYTES) {
				life = &newlife.p2_lifetime_kb;
			} else {
				goto unsupported;
			}
			break;
		case IPSEC_CLASSES_SA_LIFE_DURATION: /* Life type value */
			if (life == NULL) {
				goto unsupported;
			}
			if (*life != 0) {
				goto unsupported;
			}
			*life = value;
			life = NULL;
		}
	}

	/* No lifetime values were found in the message. */
	if (newlife.p2_lifetime_secs == 0 && newlife.p2_lifetime_kb == 0)
		goto unsupported;

	bzero(secs_str, sizeof (secs_str));
	bzero(kb_str, sizeof (kb_str));
	if (newlife.p2_lifetime_secs != 0)
		(void) ssh_snprintf(secs_str, sizeof (secs_str), "%u seconds",
		    newlife.p2_lifetime_secs);
	if (newlife.p2_lifetime_kb != 0)
		(void) ssh_snprintf(kb_str, sizeof (kb_str),
		    "%u kilobytes%s", newlife.p2_lifetime_kb,
		    bytecnt2out((uint64_t)newlife.p2_lifetime_kb << 10,
		    byte_str, sizeof (byte_str), SPC_BEGIN));
	PRTDBG(D_POL, ("Peer (%s) wants lifetime of %s%s%s "
	    "for SPI: = 0x%02x%02x%02x%02x", ip_addr, secs_str,
	    strlen(secs_str) > 0 && strlen(kb_str) > 0 ? ", " : "", kb_str,
	    spi[0], spi[1], spi[2], spi[3]));

	/* We need p1 to get lifetimes information from policy. */
	if (p1 == NULL) {
		PRTDBG(D_POL, ("No phase 1 associated with responder "
		    "lifetime - ignoring."));
		return;
	}

	/* For now, only support 4-byte SPIs. */
	if (spi_size != sizeof (uint32_t))
		return;

	(void) memcpy(&spi_val, spi, sizeof (uint32_t));
	if (!string_to_sockaddr(ip_addr, &sa))
		return;

	/*
	 * Don't allow the responder to increase SA lifetimes
	 * to values greater than the policy.
	 */
	if (newlife.p2_lifetime_secs > p1->p1_rule->p2_lifetime_secs) {
		PRTDBG(D_POL, ("Ignoring responder lifetime "
		    "notification from %s, lifetime exceeds our policy "
		    "value of %u seconds %s", ip_addr,
		    p1->p1_rule->p2_lifetime_secs,
		    secs2out(p1->p1_rule->p2_lifetime_secs, secs_str,
		    sizeof (secs_str), B_FALSE)));
		newlife.p2_lifetime_secs = p1->p1_rule->p2_lifetime_secs;
		updated_secs = B_FALSE;
	}
	if (newlife.p2_lifetime_kb > p1->p1_rule->p2_lifetime_kb) {
		PRTDBG(D_POL, ("Ignoring responder lifetime "
		    "notification from %s, lifetime exceeds our policy "
		    "value of %u kilobytes%s", ip_addr,
		    p1->p1_rule->p2_lifetime_kb,
		    bytecnt2out(
		    (uint64_t)p1->p1_rule->p2_lifetime_kb << 10,
		    byte_str, sizeof (byte_str), SPC_BEGIN)));
		newlife.p2_lifetime_kb = p1->p1_rule->p2_lifetime_kb;
		updated_kb = B_FALSE;
	}

	/*
	 * If a value is not supplied update it with cached value.
	 * (check_rule() would have updated it with default value.)
	 */
	if (newlife.p2_lifetime_secs == 0) {
		newlife.p2_lifetime_secs = p1->p1_rule->p2_lifetime_secs;
		updated_secs = B_FALSE;
	}
	if (newlife.p2_lifetime_kb == 0) {
		newlife.p2_lifetime_kb = p1->p1_rule->p2_lifetime_kb;
		updated_kb = B_FALSE;
	}

	/* Nothing to update, return. */
	if (!updated_secs && !updated_kb)
		return;

	/* Set SOFT lifetimes according to local policy. */
	newlife.p2_softlife_secs = p1->p1_rule->p2_softlife_secs;
	newlife.p2_softlife_kb = p1->p1_rule->p2_softlife_kb;

	PRTDBG(D_POL, ("Current HARD lifetime is %u secs %u KB%s",
	    p1->p1_rule->p2_lifetime_secs,
	    p1->p1_rule->p2_lifetime_kb,
	    bytecnt2out((uint64_t)p1->p1_rule->p2_lifetime_kb << 10,
	    byte_str, sizeof (byte_str), SPC_BEGIN)));
	PRTDBG(D_POL, ("Current SOFT lifetime is %u secs %u KB%s",
	    p1->p1_rule->p2_softlife_secs,
	    p1->p1_rule->p2_softlife_kb,
	    bytecnt2out((uint64_t)p1->p1_rule->p2_softlife_kb << 10,
	    byte_str, sizeof (byte_str), SPC_BEGIN)));

	/* Sanitize the lifetime values. */
	check_rule(&newlife, B_FALSE);

	PRTDBG(D_POL, ("Updated HARD lifetime to %u secs %u KB%s",
	    newlife.p2_lifetime_secs,
	    newlife.p2_lifetime_kb,
	    bytecnt2out((uint64_t)newlife.p2_lifetime_kb << 10,
	    byte_str, sizeof (byte_str), SPC_BEGIN)));
	PRTDBG(D_POL, ("Updated SOFT lifetime to %u secs %u KB%s",
	    newlife.p2_softlife_secs,
	    newlife.p2_softlife_kb,
	    bytecnt2out((uint64_t)newlife.p2_softlife_kb << 10,
	    byte_str, sizeof (byte_str), SPC_BEGIN)));

	/* Update values cached in the phase1 */
	p1->p2_lifetime_secs = newlife.p2_lifetime_secs;
	p1->p2_lifetime_kb = newlife.p2_lifetime_kb;

	/* convert kilobytes to bytes for update_assoc_lifetime() */
	update_assoc_lifetime(spi_val,
	    (uint64_t)newlife.p2_lifetime_kb << 10,
	    (uint64_t)newlife.p2_softlife_kb << 10,
	    newlife.p2_lifetime_secs,
	    newlife.p2_softlife_secs,
	    &sa, protocol_id);
	return;

unsupported:
	PRTDBG(D_POL, ("Responder lifetime notification contained %s.",
	    (msg_type == SSH_IKE_NOTIFY_MESSAGE_ATTRIBUTES_NOT_SUPPORTED) ?
	    "unsupported parameters" : "invalid encoding"));

	if (p1->p1_pminfo == NULL) {
		PRTDBG(D_POL, ("No policy manager info for Phase 1"));
		return;
	}

	if (!string_to_sockaddr(p1->p1_pminfo->local_ip, &src_ip)) {
		PRTDBG(D_POL, ("Failed to convert to sockaddr format"));
		return;
	}

	ikesrv = get_server_context(&src_ip);
	if (ikesrv == NULL) {
		PRTDBG(D_POL, ("in.iked context lookup failed %s ", sap(&sa)));
		return;
	}

	PRTDBG(D_POL, ("Sending %s notify message to the peer.",
	    (msg_type == SSH_IKE_NOTIFY_MESSAGE_ATTRIBUTES_NOT_SUPPORTED) ?
	    "ATTRIBUTES-NOT-SUPPORTED" : "UNEQUAL-PAYLOAD-LENGTHS"));
	if ((ssh_rc = ssh_ike_connect_notify(ikesrv->ikesrv_ctx,
	    p1->p1_negotiation, NULL, NULL,
	    SSH_IKE_NOTIFY_FLAGS_WANT_ISAKMP_SA, SSH_IKE_DOI_IPSEC,
	    SSH_IKE_PROTOCOL_ISAKMP, spi, spi_size, msg_type,
	    notification_data, notification_data_size)) != SSH_IKE_ERROR_OK) {
		PRTDBG(D_POL, ("Failed to send notify message: code %d (%s).",
		    ssh_rc, ike_connect_error_to_string(ssh_rc)));
	}
}

/* ARGSUSED */
void
ssh_policy_notification(SshIkePMPhaseII pm_info,
    Boolean authenticated, SshIkeProtocolIdentifiers protocol_id,
    unsigned char *spi, size_t spi_size,
    SshIkeNotifyMessageType notify_message_type,
    unsigned char *notification_data, size_t notification_data_size)
{
	phase1_t *p1 = NULL;

	PRTDBG(D_POL, ("Processing IKE notification message."));

	if (pm_info != NULL)
		p1 = (phase1_t *)pm_info->phase_i->policy_manager_data;

	/*
	 * For now, only accept authenticated notifications.  If we change our
	 * minds, put checks before this comment.
	 */
	if (!authenticated) {
		PRTDBG(D_POL,
		    ("Received unauthenticated notification %d, ignoring",
		    notify_message_type));
		return;
	}

	switch (notify_message_type) {
	case SSH_IKE_NOTIFY_MESSAGE_INITIAL_CONTACT:
		/*
		 * Special-case INITIAL-CONTACT to hit the phase 1 path.
		 * (authenticated here is better than or same as encrypted in
		 * phase 1).
		 */
		handle_initial_contact(pm_info->phase_i);
		break;
	case SSH_IKE_NOTIFY_MESSAGE_RESPONDER_LIFETIME:
		handle_responder_lifetime(pm_info->remote_ip, protocol_id,
		    spi, spi_size, notification_data, notification_data_size,
		    p1);
		break;
	case SSH_IKE_NOTIFY_MESSAGE_R_U_THERE:
	case SSH_IKE_NOTIFY_MESSAGE_R_U_THERE_ACK:
		handle_dpd_notification(pm_info->local_ip, spi, spi_size,
		    notify_message_type, notification_data,
		    notification_data_size, p1);
		break;
	}
}

/* ARGSUSED */
void
ssh_policy_phase_qm_notification(SshIkePMPhaseQm pm_info,
    SshIkeProtocolIdentifiers protocol_id,
    unsigned char *spi, size_t spi_size,
    SshIkeNotifyMessageType notify_message_type,
    unsigned char *notification_data, size_t notification_data_size)
{
	phase1_t *p1 = NULL;

	PRTDBG((D_POL | D_P2), ("Processing quick mode notification."));

	if (pm_info != NULL)
		p1 = (phase1_t *)pm_info->phase_i->policy_manager_data;

	/*
	 * Deal with RESPONDER-LIFETIME notification.  Right now, it's all we
	 * care about.  Parts are blatantly stolen from SSH's test_policy.c.
	 */

	if (notify_message_type == SSH_IKE_NOTIFY_MESSAGE_RESPONDER_LIFETIME)
		handle_responder_lifetime(pm_info->remote_ip, protocol_id, spi,
		    spi_size, notification_data, notification_data_size, p1);
}


void
ssh_policy_phase_ii_sa_freed(SshIkePMPhaseII pm_info)
{
	PRTDBG((D_POL | D_P2), ("Notifying library that P2 SA is freed."));
	/* All of these fields are valid according to SSH documentation. */
	assert(pm_info->local_ip != NULL && pm_info->remote_ip != NULL);
	PRTDBG((D_POL | D_P2), ("  Local IP = %s, Remote IP = %s,",
	    pm_info->local_ip, pm_info->remote_ip));
}

/*
 * Free pm_info->policy_manager_data, which is a parsedmsg_t.
 */
void
ssh_policy_qm_sa_freed(SshIkePMPhaseQm pm_info)
{
	parsedmsg_t *pmsg = (parsedmsg_t *)pm_info->policy_manager_data;

	PRTDBG((D_POL | D_P2), ("Notifying library that quick mode "
	    "negotiation now freed."));
	if (pmsg != NULL) {
		/*
		 * Perhaps we should treat the pmsg as a new ACQUIRE,
		 * and renegotiate!
		 */

		pm_info->policy_manager_data = NULL;
		free_pmsg(pmsg);
	}
}

/*
 * Construct an ID payload and ship it back to the callback.
 *
 * Only called when we're the quick-mode responder.
 */
void
ssh_policy_qm_local_id(SshIkePMPhaseQm pm_info,
    SshPolicyIsakmpIDCB callback_in, void *callback_context_in)
{
	SshIkePayloadID id;
	parsedmsg_t *pmsg = (parsedmsg_t *)pm_info->policy_manager_data;
	sadb_ext_t **extv;
	sadb_address_t *sadb_addr;
	phase1_t *p1 = (phase1_t *)pm_info->phase_i->policy_manager_data;

	assert(pmsg != NULL);
	extv = pmsg->pmsg_exts;
	assert(extv != NULL);
	sadb_addr = (sadb_address_t *)extv[SADB_X_EXT_ADDRESS_INNER_SRC];

	PRTDBG((D_POL | D_P2), ("Constructing local identity payload "
	    "(We are QM responder)"));
	PRTDBG((D_POL | D_P2), ("  Local IP : %s[%s], Remote IP : %s[%s]",
	    pm_info->local_ip, pm_info->local_port,
	    pm_info->remote_ip, pm_info->remote_port));
	PRTDBG((D_POL | D_P2), ("  %sInitiator Local ID = %s",
	    pm_info->this_end_is_initiator ? "** " : "",
	    pm_info->local_i_id_txt));
	PRTDBG((D_POL | D_P2), ("  %sInitiator Remote ID = %s",
	    pm_info->this_end_is_initiator ? "** " : "",
	    pm_info->remote_i_id_txt));
	PRTDBG((D_POL | D_P2), ("  %sResponder Local ID = %s",
	    pm_info->this_end_is_initiator ? "" : "** ",
	    pm_info->local_r_id_txt));
	PRTDBG((D_POL | D_P2), ("  %sResponder Remote ID = %s",
	    pm_info->this_end_is_initiator ? "" : "** ",
	    pm_info->remote_r_id_txt));

	id = ssh_calloc(1, sizeof (*id));
	if (id == NULL)
		goto bail;

	/*
	 * For transport mode, take IP address right from pm_info, otherwise,
	 * take it from inner information in the pmsg if possible.
	 */

	if (sadb_addr == NULL) {
		if (p1->p1_isv4) {
			id->id_type = IPSEC_ID_IPV4_ADDR;
			id->identification_len = sizeof (struct in_addr);
			if (inet_pton(AF_INET, (char *)pm_info->local_ip,
			    &id->identification.ipv4_addr) != 1) {
				PRTDBG((D_POL | D_P2),
				    ("  inet_pton(v4) failed,"
				    " setting identity to NULL."));
				ssh_free(id);
				id = NULL;
				goto bail;
			}
		} else {
			id->id_type = IPSEC_ID_IPV6_ADDR;
			id->identification_len = sizeof (struct in6_addr);
			if (inet_pton(AF_INET6,
			    strtok((char *)pm_info->local_ip, "%"),
			    &id->identification.ipv6_addr) != 1) {
				PRTDBG((D_POL | D_P2),
				    ("  inet_pton(v6) failed,"
				    " setting identity to NULL."));
				ssh_free(id);
				id = NULL;
				goto bail;
			}
		}

		if (pm_info->local_i_id != NULL) {
			id->protocol_id = pm_info->local_i_id->protocol_id;
			id->port_number = pm_info->local_i_id->port_number;

			PRTDBG((D_POL | D_P2),
			    ("  chose local proto %d port %d",
			    id->protocol_id, id->port_number));
		}
	} else {
		PRTDBG((D_POL | D_P2), ("  Have inner identities..."));
		assert(extv[SADB_X_EXT_ADDRESS_INNER_DST] != NULL);
		id->protocol_id = sadb_addr->sadb_address_proto;
		if (pmsg->pmsg_psss->ss_family == AF_INET) {
			pfkey_inner_to_id4(id,
			    sadb_addr->sadb_address_prefixlen,
			    pmsg->pmsg_pssin);
			id->port_number = htons(pmsg->pmsg_pssin->sin_port);
		} else {
			pfkey_inner_to_id6(id,
			    sadb_addr->sadb_address_prefixlen,
			    pmsg->pmsg_pssin6);
			id->port_number = htons(pmsg->pmsg_pssin6->sin6_port);
		}
	}

bail:
	callback_in(id, callback_context_in);
}

/*
 * Construct an ID payload and ship it back to the callback.
 *
 * For now, echo back the initiator's port number & protocol along with
 * our ip address; this isn't quite right..
 *
 * Only called when we're the quick-mode responder.
 */
void
ssh_policy_qm_remote_id(SshIkePMPhaseQm pm_info,
    SshPolicyIsakmpIDCB callback_in, void *callback_context_in)
{
	SshIkePayloadID id;
	parsedmsg_t *pmsg = (parsedmsg_t *)pm_info->policy_manager_data;
	sadb_ext_t **extv;
	sadb_address_t *sadb_addr;
	phase1_t *p1 = (phase1_t *)pm_info->phase_i->policy_manager_data;

	assert(pmsg != NULL);
	extv = pmsg->pmsg_exts;
	assert(extv != NULL);
	sadb_addr = (sadb_address_t *)extv[SADB_X_EXT_ADDRESS_INNER_DST];

	PRTDBG((D_POL | D_P2), ("Constructing remote identity payload "
	    "(We are QM responder)"));
	PRTDBG((D_POL | D_P2), ("  Local IP : %s[%s], Remote IP : %s[%s]",
	    pm_info->local_ip, pm_info->local_port,
	    pm_info->remote_ip, pm_info->remote_port));
	PRTDBG((D_POL | D_P2), ("  %sInitiator Local ID = %s",
	    pm_info->this_end_is_initiator ? "** " : "",
	    pm_info->local_i_id_txt));
	PRTDBG((D_POL | D_P2), ("  %sInitiator Remote ID = %s",
	    pm_info->this_end_is_initiator ? "** " : "",
	    pm_info->remote_i_id_txt));
	PRTDBG((D_POL | D_P2), ("  %sResponder Local ID = %s",
	    pm_info->this_end_is_initiator ? "" : "** ",
	    pm_info->local_r_id_txt));
	PRTDBG((D_POL | D_P2), ("  %sResponder Remote ID = %s",
	    pm_info->this_end_is_initiator ? "" : "** ",
	    pm_info->remote_r_id_txt));

	id = ssh_calloc(1, sizeof (*id));
	if (id == NULL)
		goto bail;

	/*
	 * For transport mode, take IP address right from pm_info, otherwise,
	 * take it from inner information in the pmsg if possible.
	 */

	if (extv[SADB_X_EXT_ADDRESS_INNER_DST] == NULL) {
		if (p1->p1_isv4) {
			id->id_type = IPSEC_ID_IPV4_ADDR;
			id->identification_len = sizeof (struct in_addr);
			if (inet_pton(AF_INET, (char *)pm_info->remote_ip,
			    &id->identification.ipv4_addr) != 1) {
				PRTDBG((D_POL | D_P2),
				    ("  inet_pton(v4) failed,"
				    " setting identity to NULL."));
				ssh_free(id);
				id = NULL;
				goto bail;
			}
		} else {
			id->id_type = IPSEC_ID_IPV6_ADDR;
			id->identification_len = sizeof (struct in6_addr);
			if (inet_pton(AF_INET6,
			    strtok((char *)pm_info->remote_ip, "%"),
			    &id->identification.ipv6_addr) != 1) {
				PRTDBG((D_POL | D_P2),
				    ("  inet_pton(v6) failed,"
				    " setting identity to NULL."));
				ssh_free(id);
				id = NULL;
				goto bail;
			}
		}

		if (pm_info->remote_i_id != NULL) {
			id->protocol_id = pm_info->remote_i_id->protocol_id;
			id->port_number = pm_info->remote_i_id->port_number;

			PRTDBG((D_POL | D_P2),
			    ("  chose remote proto %d port %d",
			    id->protocol_id, id->port_number));
		}
	} else {
		PRTDBG((D_POL | D_P2), ("  Have inner identities..."));
		assert(extv[SADB_X_EXT_ADDRESS_INNER_SRC] != NULL);
		id->protocol_id = sadb_addr->sadb_address_proto;
		if (pmsg->pmsg_pdss->ss_family == AF_INET) {
			pfkey_inner_to_id4(id,
			    sadb_addr->sadb_address_prefixlen,
			    pmsg->pmsg_pdsin);
			id->port_number = htons(pmsg->pmsg_pdsin->sin_port);
		} else {
			pfkey_inner_to_id6(id,
			    sadb_addr->sadb_address_prefixlen,
			    pmsg->pmsg_pdsin6);
			id->port_number = htons(pmsg->pmsg_pdsin6->sin6_port);
		}
	}

bail:
	callback_in(id, callback_context_in);
}

/*
 * How much nonce data do we wish to send?
 */
/* ARGSUSED */
void
ssh_policy_qm_nonce_data_len(SshIkePMPhaseQm pm_info,
    SshPolicyNonceDataLenCB callback_in, void *callback_context_in)
{
	PRTDBG((D_POL | D_P2), ("Setting QM nonce data length to 32 bytes."));

	/* TODO config - How many bytes of nonce data?  How 'bout 32 for now! */
	callback_in(32, callback_context_in);
}

/*
 * Check for an AH presence in a proposal's protocol list.
 */
static boolean_t
ah_alg_there(SshIkePayloadPProtocol ah, sadb_x_algdesc_t *auth_alg,
    SshIkePayloadT *ah_xform)
{
	int i;
	char algname[80];

	*ah_xform = NULL;

	if (ah == NULL)
		return (auth_alg == NULL);

	/*
	 * Now that the easy 3-out-of-4 cases are done, let's perform
	 * an actual evaluation.
	 */

	for (i = 0; i < ah->number_of_transforms; i++) {
		/*
		 * Write the transform offset into the transform number so
		 * when I callback to the IKE library, I have this easily
		 * available.
		 *
		 * NOTE: It _may_ be hazardous to interoperability,
		 * but interop testing so far hasn't shown that to be the case.
		 */
		ah->transforms[i].transform_number = i + 1;

		PRTDBG((D_POL | D_P2),
		    ("    AH transform #%d:",
		    ah->transforms[i].transform_number));
		PRTDBG((D_POL | D_P2),
		    ("      AH auth alg %d (%s)",
		    (int)(ah->transforms[i].transform_id.ipsec_ah),
		    kef_alg_to_string((int)(ah->
		    transforms[i].transform_id.ipsec_ah), IPSEC_PROTO_AH,
		    algname)));
		if (auth_alg != NULL) {
			if (ah->transforms[i].transform_id.ipsec_ah ==
			    auth_alg->sadb_x_algdesc_alg) {
				*ah_xform = ah->transforms + i;
				return (B_TRUE);
			}
			PRTDBG((D_POL | D_P2),
			    ("    [No match - different AH auth "
			    "algorithms]"));
		} else {
			PRTDBG((D_POL | D_P2),
			    ("    [No match - local policy has no AH auth "
			    "alg]"));
		}
	}

	return (B_FALSE);
}

/*
 * Verify that the supplied key length is appropriate for this algorithm
 * using the configured minimum/increment information.
 *
 * Called in the case where the peer supplies a nonzero length attribute
 * or in the case of missing length attribute for variable-sized cipher
 */
static boolean_t
check_key_incr(const p2alg_t *alg, int length)
{
	int incrguess, newbits;

	assert(length != 0);

	if (alg == NULL)
		return (B_FALSE);

	if (alg->p2alg_key_len_incr == 0)
		return (B_TRUE);

	/*
	 * Compute the closest key increment, then figure out if it's
	 * exact when we work back to the underlying key length in bits.
	 */

	incrguess = (length - alg->p2alg_min_bits) / alg->p2alg_key_len_incr;
	newbits = alg->p2alg_min_bits + (incrguess * alg->p2alg_key_len_incr);

	return (newbits == length);
}

/*
 * Values for the authentication algorithm _attribute_ in
 * IKE don't match the transform ID values in the DOI.
 * See auth_id_to_attr() for details.
 */
int
attr_to_auth_id(int authid)
{
	switch (authid) {
	case IPSEC_VALUES_AUTH_ALGORITHM_HMAC_MD5:
		return (SADB_AALG_MD5HMAC);
	case IPSEC_VALUES_AUTH_ALGORITHM_HMAC_SHA_1:
		return (SADB_AALG_SHA1HMAC);
	}
	/* Lucky for us, SHA-2 series maps directly. */
	return (authid);
}

/*
 * Check for an ESP presence in a proposal's protocol list.
 */
static boolean_t
esp_alg_there(SshIkeNegotiation negotiation, SshIkePayloadPProtocol esp,
    sadb_x_algdesc_t *encr_alg, sadb_x_algdesc_t *auth_alg,
    SshIkePayloadT *esp_xform)
{
	int i, len;
	char algname[80];
	struct SshIkeIpsecAttributesRec esp_attrs;
	boolean_t status;

	*esp_xform = NULL;

	if (esp == NULL) {
		boolean_t null_status = (encr_alg == NULL && auth_alg == NULL);

		if (!null_status) {
			PRTDBG((D_POL | D_P2),
			    ("    [No match - local policy has no "
			    "ESP but peer offered ESP proposal]"));
		}
		return (null_status);
	}

	if (encr_alg == NULL && auth_alg == NULL) {
		PRTDBG((D_POL | D_P2), ("    [No match - local policy has NULL "
		    "ESP policy but peer offered ESP proposal]"));
		return (B_FALSE);
	}

	/*
	 * Now that the easy 5-out-of-8 cases are done, let's perform
	 * an actual evaluation.
	 */
	for (i = 0; i < esp->number_of_transforms; i++) {
		/*
		 * Reset the status on each iteration to B_TRUE.
		 */
		status = B_TRUE;

		/*
		 * Write the transform offset into the transform number so
		 * when I callback to the IKE library, I have this easily
		 * available.
		 *
		 * NOTE: It _may_ be hazardous to interoperability,
		 * but interop testing so far hasn't shown that to be the case.
		 */
		esp->transforms[i].transform_number = i + 1;

		ssh_ike_clear_ipsec_attrs(&esp_attrs);
		if (!ssh_ike_read_ipsec_attrs(negotiation, esp->transforms + i,
		    &esp_attrs))
			continue;
		PRTDBG((D_POL | D_P2),
		    ("    ESP transform #%d:",
		    esp->transforms[i].transform_number));
		PRTDBG((D_POL | D_P2),
		    ("      ESP auth alg %d (%s)",
		    attr_to_auth_id(esp_attrs.auth_algorithm),
		    kef_alg_to_string(attr_to_auth_id(
		    esp_attrs.auth_algorithm), IPSEC_PROTO_AH, algname)));
		if (auth_alg != NULL) {
			if (esp_attrs.auth_algorithm !=
			    auth_id_to_attr(auth_alg->sadb_x_algdesc_alg)) {
				PRTDBG((D_POL | D_P2),
				    ("    [No match - different ESP auth "
				    "algs]"));
				status = B_FALSE;
				/* Continue on to show other proposals */
			}
		} else {
			if (esp_attrs.auth_algorithm != 0) {
				PRTDBG((D_POL | D_P2),
				    ("    [No match - local policy has no"
				    " ESP auth alg]"));
				status = B_FALSE;
				/* Continue on to show other proposals */
			}
		}
		/*
		 * Print out peer proposals even if we don't have
		 * encryption, then short-circuit immediately after.
		 */
		len = esp_attrs.key_length;

		PRTDBG((D_POL | D_P2), ("      ESP encr alg %d (%s)",
		    (int)(esp->transforms[i].transform_id.ipsec_esp),
		    kef_alg_to_string((int)(esp->
		    transforms[i].transform_id.ipsec_esp),
		    IPSEC_PROTO_ESP, algname)));
		if (len != 0)
			PRTDBG((D_POL | D_P2),
			    ("          Key Length = %d bits", len));
		/*
		 * Don't check subtle points when we've already failed
		 * to find a match.  Diagnostics above are enough to
		 * give the administrator a good clue as to the policy
		 * mismatch.
		 */
		if (!status)
			continue;

		if (encr_alg != NULL) {
			int id = encr_alg->sadb_x_algdesc_alg;
			const p2alg_t *alg = find_esp_encr_alg(id);

			if (alg == NULL) {
				PRTDBG((D_POL | D_P2),
				    ("    [No match - local policy has "
				    " ESP encr alg, peer does not]"));
				continue;
			}

			if (esp->transforms[i].transform_id.ipsec_esp != id) {
				PRTDBG((D_POL | D_P2),
				    ("    [No match - different ESP encr "
				    "algs]"));
				continue;
			}
			/*
			 * If missing IPSEC_CLASSES_KEY_LENGTH attribute
			 * for a variable-sized cipher then set len
			 * to default key length for that cipher so we
			 * can check it against SADB
			 */
			if ((len == 0) &&
			    (alg->p2alg_min_bits != alg->p2alg_max_bits)) {
				/* need the length in bits */
				len = alg->p2alg_default_incr;
			}
			if (len != 0) {
				if (len < encr_alg->sadb_x_algdesc_minbits) {
					PRTDBG((D_POL | D_P2),
					    ("    [No match - key size (%d) "
					    "less than minimum (%d)]", len,
					    encr_alg->sadb_x_algdesc_minbits));
					continue;
				}
				if (len > encr_alg->sadb_x_algdesc_maxbits) {
					PRTDBG((D_POL | D_P2),
					    ("    [No match - key size more "
					    "(%d) than maximum (%d)]", len,
					    encr_alg->sadb_x_algdesc_maxbits));
					continue;
				}
				if (!check_key_incr(alg, len)) {
					PRTDBG((D_POL | D_P2),
					    ("    [No match - key size "
					    "increment check failed]"));
					continue;
				}
			}
			/*
			 * TODO: look at esp_attrs.key_rounds if we
			 * ever support variable-round ciphers..
			 */
		} else {
			if (esp->transforms[i].transform_id.ipsec_esp != 0 &&
			    esp->transforms[i].transform_id.ipsec_esp !=
			    SADB_EALG_NULL) {
				PRTDBG((D_POL | D_P2),
				    ("    [No match - local policy has NULL "
				    "ESP, peer proposed non-NULL ESP]"));
				continue;
			}
		}

		*esp_xform = esp->transforms + i;
		return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * See if the algorithm descriptor array matches what is in the AH/ESP
 * (and eventually IPCOMP) transform list.
 */
static boolean_t
matching_algdescs(SshIkeNegotiation negotiation, sadb_x_algdesc_t *algdescs,
    SshIkePayloadT *ah_xform, SshIkePayloadT *esp_xform, int numalgs,
    SshIkePayloadPProtocol ah, SshIkePayloadPProtocol esp)
{
	sadb_x_algdesc_t *current_alg, *ah_alg, *esp_auth_alg, *esp_encr_alg;
	boolean_t ah_status = B_FALSE;
	boolean_t esp_status = B_FALSE;
	int i;

	ah_alg = esp_encr_alg = esp_auth_alg = NULL;

	/*
	 * Extract AH and ESP algorithms in this extended combination.
	 * We assume the kernel won't give us multiple algdescs of the same
	 * type (see which_ecomb()).  Otherwise, we'd have to pass arrays of
	 * acceptable algorithms descriptors into *_alg_there(), making those
	 * functions more complex.
	 */
	for (i = 0; i < numalgs; i++) {
		current_alg = algdescs + i;

		switch (current_alg->sadb_x_algdesc_satype) {
		case SADB_SATYPE_ESP:
			/* Initiator didn't ask for ESP, but that's ok. */
			if (esp == NULL)
				continue;
			if (current_alg->sadb_x_algdesc_algtype ==
			    SADB_X_ALGTYPE_AUTH) {
				esp_auth_alg = current_alg;
			} else {
				assert(current_alg->sadb_x_algdesc_algtype ==
				    SADB_X_ALGTYPE_CRYPT);
				esp_encr_alg = current_alg;
			}
			break;
		case SADB_SATYPE_AH:
			/* Initiator didn't ask for AH, but that's ok. */
			if (ah == NULL)
				continue;
			ah_alg = current_alg;
			break;
		default:
			EXIT_FATAL("Kernel bug found matching algorithm "
			    "descriptors!");
			break;
		}
	}

	PRTDBG((D_POL | D_P2), ("  Peer P2 proposals:"));
	ah_status = ah_alg_there(ah, ah_alg, ah_xform);
	esp_status = esp_alg_there(negotiation, esp, esp_encr_alg, esp_auth_alg,
	    esp_xform);
	return (ah_status & esp_status);
}

/*
 * Given a transform, its component protocols, and an inverse ACQUIRE, see
 * what ecomb offset I hit.  If I return -1, then it hits none in the
 * extended ACQUIRE.  If I return -2, then the inverse ACQUIRE is NULL.
 */
#define	NO_MATCHING_ECOMB -1
#define	UNKNOWN_POLICY -2
/* ARGSUSED */  /* TODO IPCOMP */
static int
which_ecomb(SshIkeNegotiation negotiation, parsedmsg_t *iacq_pmsg,
    SshIkePayloadT *ah_xform, SshIkePayloadT *esp_xform,
    SshIkePayloadT *ipcomp_xform, SshIkePayloadPProtocol ah,
    SshIkePayloadPProtocol esp, SshIkePayloadPProtocol ipcomp)
{
	sadb_prop_t *eprop;
	sadb_x_ecomb_t *ecomb;
	int ecomb_offset;
	sadb_x_algdesc_t *algdesc;
	SshIkeNegotiation p1_nego;

	if (negotiation->u.q.pm_info != NULL)
		p1_nego = negotiation->u.q.pm_info->phase_i->negotiation;
	else
		p1_nego = NULL;

	if (iacq_pmsg == NULL)
		return (UNKNOWN_POLICY);

	/* TODO Fix when we support IPCOMP. */
	if (ipcomp != NULL)
		return (NO_MATCHING_ECOMB);

	assert(ah != NULL || esp != NULL);

	/*
	 * NOTE: I'm assuming that the kernel will be uniform in giving
	 *	 me only one algdesc per protocol-algorithm per ecomb.
	 *	 This means if I want ESP with either 3DES or DES and MD5,
	 *	 I will get:
	 *	 EPROP: algdesc(ESP, CRYPT, 3DES), algdesc(ESP, AUTH, MD5)
	 *	 EPROP:	algdesc(ESP, CRYPT, DES), algdesc(ESP, AUTH, MD5)
	 *
	 *	 and not:
	 *	 EPROP: algdesc(ESP, CRYPT, 3DES), algdesc(ESP, CRYPT, DES),
	 *		algdesc(ESP, AUTH, MD5).
	 *
	 *	PF_KEY is vague here to match IKE's vagueness.  Our
	 *	implementation, however, will not be this vague in our kernel
	 *	policy for extended ACQUIRES (outbound) or inverse ACQUIRES
	 *	(inbound).  It is difficult for the kernel to be vague,
	 *	because of IKE treating ESP authentication as a transform
	 *	attribute.  See	matching_algdescs() for why this is annoying.
	 */

	PRTDBG((D_POL | D_P2), ("Choosing an extended combination..."));

	eprop = (sadb_prop_t *)iacq_pmsg->pmsg_exts[SADB_X_EXT_EPROP];
	ecomb = (sadb_x_ecomb_t *)(eprop + 1);
	for (ecomb_offset = 0; ecomb_offset < eprop->sadb_x_prop_numecombs;
	    ecomb_offset++) {
		PRTDBG((D_POL | D_P2), ("  Evaluating extended combination %d.",
		    ecomb_offset + 1));
		algdesc = (sadb_x_algdesc_t *)(ecomb + 1);

		if (matching_algdescs(negotiation, algdesc, ah_xform, esp_xform,
		    ecomb->sadb_x_ecomb_numalgs, ah, esp)) {
			if ((algdesc->sadb_x_algdesc_satype ==
			    SADB_SATYPE_AH) && p1_nego != NULL &&
			    NEED_NATT(p1_nego->sa->sa_natt_state)) {
				PRTDBG((D_POL | D_P2), ("  AH not supported "
				    "with NAT-T."));
			} else {
				PRTDBG((D_POL | D_P2), ("  Extended "
				    "combination %d was chosen.",
				    ecomb_offset + 1));
				return (ecomb_offset);
			}
		}

		PRTDBG((D_POL | D_P2), ("  Extended combination %d was not "
		    "chosen.", ecomb_offset + 1));
		ecomb = (sadb_x_ecomb_t *)
		    (algdesc + ecomb->sadb_x_ecomb_numalgs);
	}

	PRTDBG((D_POL | D_P2), ("  No extended combinations chosen!"));
	return (NO_MATCHING_ECOMB);
}

/*
 * The newer method for evaluating IKE Quick Mode proposals.
 */
static SshIkePayloadP
evaluate_qm_proposal(SshIkeNegotiation negotiation, parsedmsg_t *iacq_pmsg,
    SshIkePayloadP challenger,
    SshIkePayloadT *ah_xform, SshIkePayloadT *esp_xform,
    SshIkePayloadT *ipcomp_xform, int *winning_offset)
{
	SshIkePayloadT new_ah_xf, new_esp_xf, new_ipcomp_xf;
	SshIkePayloadPProtocol ah = NULL, esp = NULL, ipcomp = NULL;
	int challenger_offset, i;

	/*
	 * Set all of the protocols.
	 */
	for (i = 0; i < challenger->number_of_protocols; i++) {
		switch (challenger->protocols[i].protocol_id) {
		case SSH_IKE_PROTOCOL_IPSEC_AH:
			ah = challenger->protocols + i;
			break;
		case SSH_IKE_PROTOCOL_IPSEC_ESP:
			esp = challenger->protocols + i;
			break;
		case SSH_IKE_PROTOCOL_IPCOMP:
			ipcomp = challenger->protocols + i;
		default:
			return (NULL);
		}
	}

	/*
	 * Get the challenger's highest index in the iacq_pmsg.
	 */
	challenger_offset = which_ecomb(negotiation, iacq_pmsg,
	    &new_ah_xf, &new_esp_xf, &new_ipcomp_xf, ah, esp, ipcomp);

	if (challenger_offset == NO_MATCHING_ECOMB) {
		PRTDBG((D_POL | D_P2), ("No matching extended combination "
		    "found when evaluating QM proposal."));
		return (NULL);
	}

	/*
	 * If the winner is NULL, return the challenger as is.
	 *
	 * Else get the winner's highest index in the iacq_pmsg and
	 * compare.
	 */
	*winning_offset = challenger_offset;
	*ah_xform = new_ah_xf;
	*esp_xform = new_esp_xf;
	*ipcomp_xform = new_ipcomp_xf;
	return (challenger);
}

/*
 * Decide if we have any chance of accepting a situation including a
 * sensitivity or integrity label.  This is not the last word; we also
 * give the kernel a crack at it via inverse acquire.
 */
static boolean_t
evaluate_sensitive_sit(SshIkePMPhaseQm pm_info, SshIkePayloadSA sa_payload,
    SshIkePayloadSA prev)
{
	SshIkeIpsecSituationPacket sit = &sa_payload->situation;
	uint32_t sit_flags = sit->situation_flags;
	phase1_t *p1;
	bslabel_t sens, psens;

	if (!is_ike_labeled())
		return (B_FALSE);

	p1 = pm_info->phase_i->policy_manager_data;

	if (sit_flags & SSH_IKE_SIT_SECRECY) {
		if (sit->secrecy_level_length != 1)
			return (B_FALSE);
	}

	if (sit_flags & SSH_IKE_SIT_INTEGRITY) {
		if (sit->integrity_level_length != 1)
			return (B_FALSE);
	}

	if (sit->labeled_domain_identifier != p1->label_doi)
		return (B_FALSE);

	if (!sit_to_bslabel(sit, &sens))
		return (B_FALSE);

	if (!bldominates(&sens, &p1->min_sl))
		return (B_FALSE);

	if (!bldominates(&p1->max_sl, &sens))
		return (B_FALSE);

	if (prev != NULL) {
		SshIkeIpsecSituationPacket psit = &prev->situation;

		if (psit->labeled_domain_identifier !=
		    sit->labeled_domain_identifier)
			return (B_FALSE);

		if (!sit_to_bslabel(psit, &psens))
			return (B_FALSE);

		if (!blequal(&sens, &psens))
			return (B_FALSE);
	}
	return (B_TRUE);
}

/*
 * Decide if we should consider accepting a situation which doesn't
 * include a label.  This is not the last word; we also give the kernel
 * a crack at it via inverse acquire.
 */
/* ARGSUSED */
static boolean_t
evaluate_unlabeled_sit(SshIkePMPhaseQm pm_info, SshIkePayloadSA sa_payload)
{
	phase1_t *p1;

	if (!is_ike_labeled())
		return (B_TRUE);

	/*
	 * TBD?  fail if we expect peer to be multi-label.
	 * Maybe this is the wrong thing to do here -- instead we should just
	 * hallucinate the right label instead.
	 */

	p1 = pm_info->phase_i->policy_manager_data;
	if (p1->label_aware) {
		PRTDBG((D_POL | D_P2),
		    ("  multi-label peer without labeled situation"));
		/* XXX spew something here */
	}

	return (B_TRUE);
}

/*
 * Free the SA indices array.
 */
static void
free_selection(SshIkeIpsecSelectedSAIndexes selection, int num)
{
	int i, j;

	for (i = 0; i < num; i++) {
		ssh_free(selection[i].transform_indexes);
		ssh_free(selection[i].spi_sizes);
		for (j = 0; j < selection[i].number_of_protocols; j++)
			ssh_free(selection[i].spis[j]);
		ssh_free(selection[i].spis);
	}

	ssh_free(selection);
}

static void continue_qm_select_sa(saselect_t *, parsedmsg_t *);
static void finish_qm_select_sa(spiwait_t *);

/*
 * Select one proposal for each inbound SA request, and one transform for each
 * protocol in a proposal.  We ask the kernel about policy here with an
 * inverse ACQUIRE.
 */
void
ssh_policy_qm_select_sa(SshIkePMPhaseQm pm_info, SshIkeNegotiation negotiation,
    int number_of_sas_in, SshIkePayload *sa_table_in,
    SshPolicyQmSACB callback_in, void *callback_context_in)
{
	saselect_t *ssap;
	int i;
	SshIkePayloadSA prevsa = NULL;

	PRTDBG((D_POL | D_P2),
	    ("Selecting proposal for %d inbound QM SA(s).", number_of_sas_in));

	ssap = (saselect_t *)ssh_malloc(sizeof (*ssap));
	if (ssap == NULL) {
		PRTDBG((D_POL | D_P2),
		    ("Out of memory: ssap allocation failed."));
		callback_in(NULL, callback_context_in);
		return;
	}

	ssap->ssa_pm_info = pm_info;
	ssap->ssa_negotiation = negotiation;
	ssap->ssa_nsas = number_of_sas_in;
	ssap->ssa_sas = sa_table_in;
	ssap->ssa_callback = callback_in;
	ssap->ssa_context = callback_context_in;
	ssap->ssa_sit = -1;

	ssap->ssa_selection = ssh_calloc(number_of_sas_in,
	    sizeof (struct SshIkeIpsecSelectedSAIndexesRec));
	if (ssap->ssa_selection == NULL) {
		PRTDBG((D_POL | D_P2),
		    ("Out of memory: first ssh_calloc() failed."));
		ssap->ssa_callback(NULL, ssap->ssa_context);
		ssh_free(ssap);
		return;
	}

	/*
	 * Pre-screen for labels.
	 */
	for (i = 0; i < number_of_sas_in; i++) {
		unsigned long sitflags;
		SshIkeIpsecSelectedSAIndexes current;

		SshIkePayloadSA sa_payload = &(sa_table_in[i]->pl.sa);

		current = ssap->ssa_selection + i;

		sitflags = sa_payload->situation.situation_flags;

		if ((sitflags & ~SSH_IKE_SIT_IDENTITY_ONLY) != 0) {
			if (!evaluate_sensitive_sit(pm_info,
			    sa_payload, prevsa)) {
				PRTDBG((D_POL | D_P2),
				    ("  bad labeled situation"));
				current->proposal_index = -1;
				continue;
			}
			if (prevsa == NULL) {
				ssap->ssa_sit = i;
				prevsa = sa_payload;
			}
		}
	}
	/*
	 * Perform an inverse ACQUIRE here.  That way, you
	 * can pass the results to evaluate_qm_proposal().
	 */
	ssap->ssa_complete = continue_qm_select_sa;
	inverse_acquire(ssap);
}

/*
 * Continue SA selection once we have the inverse_acquire response.
 */
static void
continue_qm_select_sa(saselect_t *ssap,  parsedmsg_t *iacq_pmsg)
{

	SshIkeIpsecSelectedSAIndexes current;
	SshIkePayloadP pwinner = NULL;
	SshIkePayloadT twinner_ah, twinner_esp, twinner_ipcomp;
	int i, x, num_sa_indices = 0, winning_index = -1, winning_offset;
	SshIkePMPhaseQm pm_info;
	SshIkeNegotiation negotiation;
	int number_of_sas_in;
	SshIkePayload *sa_table_in;

	pm_info = ssap->ssa_pm_info;
	negotiation = ssap->ssa_negotiation;
	number_of_sas_in = ssap->ssa_nsas;
	sa_table_in = ssap->ssa_sas;

	PRTDBG((D_POL | D_P2), ("Continuing QM SA selection..."));

	if (iacq_pmsg == NULL) {
		PRTDBG((D_POL | D_P2),
		    ("  inverse_acquire() failed."));
		ssh_free(ssap->ssa_selection);
		ssap->ssa_callback(NULL, ssap->ssa_context);
		ssh_free(ssap);
		return;
	}

	if (iacq_pmsg == (parsedmsg_t *)-1) {
		/* This means there's no policy, but otherwise things worked. */
		struct sockaddr_storage *local, *remote;

		iacq_pmsg = NULL;
		local = &ssap->ssa_local;
		remote = &ssap->ssa_remote;
		if (!string_to_sockaddr(pm_info->local_ip, local) ||
		    !string_to_sockaddr(pm_info->remote_ip, remote)) {
			PRTDBG((D_POL | D_P2),
			    ("  Failed to convert string to socket address "));
			ssh_free(ssap->ssa_selection);
			ssap->ssa_callback(NULL, ssap->ssa_context);
			ssh_free(ssap);
			return;
		}
	} else {
		ssap->ssa_local = *iacq_pmsg->pmsg_sss;
		ssap->ssa_remote = *iacq_pmsg->pmsg_dss;
	}

	for (i = 0; i < number_of_sas_in; i++) {
		unsigned long sitflags;

		SshIkePayloadSA sa_payload = &(sa_table_in[i]->pl.sa);

		current = ssap->ssa_selection + i;

		/* already toast? */

		if (current->proposal_index == -1)
			continue;

		sitflags = sa_payload->situation.situation_flags;

		PRTDBG((D_POL | D_P2), ("  SA #%d.  sitflags %lx",
		    i, sitflags));

		if (sa_payload->doi != SSH_IKE_DOI_IPSEC) {
			PRTDBG((D_POL | D_P2), ("  bad DOI %x",
			    sa_payload->doi));
			current->proposal_index = -1;
			continue;
		}

		if ((sitflags & ~SSH_IKE_SIT_IDENTITY_ONLY) != 0) {
			if (!evaluate_sensitive_sit(pm_info,
			    sa_payload, NULL)) {
				PRTDBG((D_POL | D_P2),
				    ("  bad labeled situation"));
				current->proposal_index = -1;
				continue;
			}
		} else {
			if (!evaluate_unlabeled_sit(pm_info, sa_payload)) {
				PRTDBG((D_POL | D_P2),
				    ("  bad unlabeled situation"));
				current->proposal_index = -1;
				continue;
			}
		}

		pwinner = NULL;
		twinner_ah = NULL;
		twinner_esp = NULL;
		twinner_ipcomp = NULL;
		PRTDBG((D_POL | D_P2), ("  Number of proposals = %d.",
		    sa_payload->number_of_proposals));
		for (x = 0; x < sa_payload->number_of_proposals; x++) {
			PRTDBG((D_POL | D_P2), ("  Proposal %d.", x + 1));
			pwinner = evaluate_qm_proposal(negotiation, iacq_pmsg,
			    sa_payload->proposals + x, &twinner_ah,
			    &twinner_esp, &twinner_ipcomp, &winning_offset);

			if (pwinner != NULL) {
				winning_index = x;
				break;
			}
		}

		/*
		 * No winner this time, mark as -1 index and continue.
		 */
		if (pwinner == NULL) {
			PRTDBG((D_POL | D_P2), ("  No selection this time."));
			current->proposal_index = -1;
			continue;
		}
		PRTDBG((D_POL | D_P2), ("  Selection is %d.",
		    winning_index + 1));

		if (winning_index == -1) {
			EXIT_FATAL("Assertion failed, winning_index == -1");
		}

		current->proposal_index = winning_index;
		current->number_of_protocols = pwinner->number_of_protocols;
		current->transform_indexes = ssh_calloc(1, sizeof (int) *
		    current->number_of_protocols);
		current->spis = ssh_calloc(1, sizeof (char *) *
		    current->number_of_protocols);
		current->spi_sizes = ssh_calloc(1, sizeof (size_t) *
		    current->number_of_protocols);
		if (current->transform_indexes == NULL ||
		    current->spis == NULL || current->spi_sizes == NULL) {
			PRTDBG((D_POL | D_P2),
			    ("  Out of memory."));
			free_selection(ssap->ssa_selection, num_sa_indices);
			ssap->ssa_selection = NULL;
			goto bail;
		}

		current->expire_secs = 0;
		current->expire_kb = 0;

		for (x = 0; x < pwinner->number_of_protocols; x++) {
			SshIkePayloadT twinner;

			switch (pwinner->protocols[x].protocol_id) {
			case SSH_IKE_PROTOCOL_IPSEC_AH:
			case SSH_IKE_PROTOCOL_IPSEC_ESP:
				current->spi_sizes[x] = sizeof (uint32_t);
				current->spis[x] = NULL;
				twinner =
				    (pwinner->protocols[x].protocol_id ==
				    SSH_IKE_PROTOCOL_IPSEC_AH ?
				    twinner_ah : twinner_esp);
				break;
			case SSH_IKE_PROTOCOL_IPCOMP:
				/*
				 * evaluate_qm_proposal() will keep
				 * this path from ever executing until we
				 * support IPCOMP.  When we support IPCOMP,
				 * change this section of code too.
				 */
				EXIT_FATAL("IPCOMP not supported.");
			}

			if (twinner == NULL) {
				/* Free a bunch of stuff in selection. */
				free_selection(ssap->ssa_selection,
				    num_sa_indices);
				ssap->ssa_selection = NULL;
				goto bail;
			}

			PRTDBG((D_POL | D_P2), ("  Transform number %d.",
			    twinner->transform_number));
			/*  Maybe revert to 0?  (no "- 1") */
			current->transform_indexes[x] =
			    (twinner->transform_number - 1);
		}
	}
bail:
#if 0
	if (ssap->ssa_selection != NULL) {
		/*
		 * Inspect lifetimes and the pmsg, and see if I need to send a
		 * responder- lifetime notification.
		 */
	}
#endif

	/* Pass the parsed message on for further processing */
	if (iacq_pmsg != NULL)
		pm_info->policy_manager_data = iacq_pmsg;

	ssap->ssa_spiwait.sw_context = ssap;
	finish_qm_select_sa(&ssap->ssa_spiwait);
}

/*
 * Now, get SPI's for the winner(s).
 */
static void
finish_qm_select_sa(spiwait_t *swp)
{
	saselect_t *ssap = swp->sw_context;
	int i, j;
	SshIkeIpsecSelectedSAIndexes current;
	SshIkePayloadP pwinner = NULL;
	SshIkePayloadSA sa_payload;

	PRTDBG(D_P2, ("Finishing SA selection."));

	for (i = 0; i < ssap->ssa_nsas; i++) {
		current = ssap->ssa_selection + i;
		/* Don't bother with rejected proposals. */
		if (current->proposal_index == -1)
			continue;

		sa_payload = &(ssap->ssa_sas[i]->pl.sa);
		pwinner = sa_payload->proposals + current->proposal_index;

		for (j = 0; j < pwinner->number_of_protocols; j++) {
			if (current->spis[j] == NULL) {
				current->spis[j] = ssh_malloc(4);
				if (current->spis[j] == NULL) {
					PRTDBG((D_POL | D_P2),
					    ("  Out of memory."));
					free_selection(ssap->ssa_selection,
					    ssap->ssa_nsas);
					ssap->ssa_selection = NULL;
					goto bail;
				}
				getspi(swp,
				    &ssap->ssa_local, &ssap->ssa_remote,
				    pwinner->protocols[j].protocol_id,
				    0,
				    current->spis[j],
				    finish_qm_select_sa, ssap);
				return;
			}
		}
	}

bail:
	ssap->ssa_callback(ssap->ssa_selection, ssap->ssa_context);
	ssh_free(ssap);
}

/*
 * Config-mode functions.  We don't support config mode, so make these
 * as minimal as possible.
 */
/* ARGSUSED */
void
ssh_policy_cfg_fill_attrs(SshIkePMPhaseII pm_info, int number_of_attrs,
    SshIkePayloadAttr *return_attributes, SshPolicyCfgFillAttrsCB callback_in,
    void *callback_context_in)
{
	PRTDBG((D_POL | D_P2),
	    ("Config mode [unsupported]: filling attributes."));
	PRTDBG((D_POL | D_P2),
	    ("  Number of attributes %d.", number_of_attrs));
	callback_in(0, return_attributes, callback_context_in);
}

/* ARGSUSED */
void
ssh_policy_cfg_notify_attrs(SshIkePMPhaseII pm_info, int number_of_attrs,
    SshIkePayloadAttr *return_attributes)
{
	PRTDBG((D_POL | D_P2),
	    ("Config mode [unsupported]: notify attributes."));
	PRTDBG((D_POL | D_P2),
	    ("  Number of attributes %d.", number_of_attrs));
}

#if 0
void
ssh_policy_sun_info(const char *msg, int val)
{
	PRTDBG((D_POL),
	    ("IKE library: %s %d", msg, val));
}
#else
void
ssh_policy_sun_info(char *fmt, ...)
{
	va_list ap;
	char msgbuf[BUFSIZ];

	va_start(ap, fmt);
	(void) ssh_vsnprintf(msgbuf, BUFSIZ, fmt, ap);

	PRTDBG((D_POL),
	    ("IKE library: %s", msgbuf));

	va_end(ap);
}
#endif



/* ARGSUSED */
void
ssh_policy_phase_i_server_changed(SshIkePMPhaseI pm_info,
    SshIkeServerContext server, const unsigned char *new_remote_ip,
    const unsigned char *new_remote_port)
{
	PRTDBG((D_POL | D_P1),
	    ("Notifying library P1 server address or ports have changed."));
}

/* ARGSUSED */
void
ssh_policy_phase_ii_server_changed(SshIkePMPhaseII pm_info,
    SshIkeServerContext server, const unsigned char *new_remote_ip,
    const unsigned char *new_remote_port)
{
	PRTDBG((D_POL | D_P2),
	    ("Notifying library P2 server address or ports have changed."));
}

/* ARGSUSED */
void
ssh_policy_phase_qm_server_changed(SshIkePMPhaseQm pm_info,
    SshIkeServerContext server, const unsigned char *new_remote_ip,
    const unsigned char *new_remote_port)
{
	PRTDBG((D_POL | D_P2),
	    ("Notifying library QM server address or ports have changed."));
}
