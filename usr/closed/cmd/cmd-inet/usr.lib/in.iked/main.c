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
 * Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stddef.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <net/route.h>
#include <net/pfkeyv2.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <libintl.h>
#include <locale.h>
#include <netdb.h>
#include <syslog.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <deflt.h>
#include <libscf.h>
#include <priv_utils.h>

#include <ipsec_util.h>

#include "defs.h"
#include "lock.h"

#include <ike/ssheloop.h>
#include <ike/sshtime.h>
#include <ike/pkcs11-glue.h>
#include <ike/sshdebug.h>

/*
 * SSH's IKE defines these to halt compiles and force use of ssh_snprintf...
 * We bypass that madness here.
 */
#undef vsnprintf
extern char	*optarg;

uint32_t	debug = 0;
boolean_t	do_natt = B_TRUE;
boolean_t	cflag = B_FALSE;
static boolean_t	nofork = B_FALSE;
static uchar_t	nat_t_portstr[8];	/* Enough to hold a UDP port. */
FILE		*debugfile = stderr;

int		privilege = 0;
char		*my_fmri;
boolean_t	ignore_errors;

#define	PRIV_NEEDED_NET		PRIV_SYS_IP_CONFIG, PRIV_NET_PRIVADDR

#define	PRIV_NEEDED_LABEL 	PRIV_NET_MAC_AWARE, PRIV_NET_MAC_IMPLICIT, \
				PRIV_NET_BINDMLP

#define	PRIV_NEEDED_ALL		PRIV_FILE_DAC_WRITE, PRIV_NEEDED_NET

/*
 * For now, make these globals.
 */
SshIkeContext ike_context;
static SshAuditContext audit_context;
static struct SshX509ConfigRec x509config_storage;
SshX509Config x509config;

static const char	*progname;

/*
 * daemon's global stat counters
 */
ike_stats_t	 ikestats;
ike_defaults_t	 ike_defs;

/*
 * AVL trees to track active local addresses and logical interfaces.  The only
 * reason to track logical interfaces separately is due to a misfeature of
 * PF_ROUTE which generates spurious RTM_DELADDR messages for addresses which
 * aren't actually present on the system.
 */
avl_tree_t ike_servers;
avl_tree_t ike_lifs;
int num_server_contexts;

struct SshIkeParamsRec ike_params;

static struct SshIkePMContextRec policy_state;

char *cfile = NULL;

/* yyerror() routine for the rule parser. */
void
yyerror(const char *msg)
{
	extern int yylineno;
	extern char yytext[];

	PRTDBG(D_OP, ("line %d near token '%s': %s.",
	    yylineno, yytext, msg));

	/* parsing errors are assigned errno EINVAL */
	errno = EINVAL;
}

/*
 * Load the config file, but just to check if it's syntactically okay.
 * This function never returns.
 */
void
config_check(void)
{
	int rc;

	yyin = fopen(cfile, "r");
	if (yyin == NULL) {
		EXIT_BADCONFIG3("Can't open file %s. (%s)", cfile,
		    strerror(errno));
	}

	rc = yyparse();
	(void) fclose(yyin);

	if (rc == 0)
		EXIT_OK2("Configuration file %s syntactically checks out.",
		    cfile);
	else
		EXIT_BADCONFIG2("Configuration file %s has syntax errors.\n",
		    cfile);
}

/*
 * Initialize the IKE parameters and rulebase.
 */
static void
init_ike_params(SshIkeParams params)
{
	/*
	 * NOTE:  Params are pointing to the global variable, which is
	 *	  already zeroed.
	 */

	/* Use libike's local secret length. */
	params->length_of_local_secret = 0;
	/* Use libike's default hash value. */
	params->token_hash_type = NULL;
	/* Process certificate request payloads. */
	params->ignore_cr_payloads = FALSE;
	/* For now, allow key hash payloads. */
	params->no_key_hash_payload = FALSE;
	/* Now send CR payloads. */
	params->no_cr_payloads = FALSE;
	/* Now send CRLs. */
	params->do_not_send_crls = FALSE;
	/* For now, don't send full chains. */
	params->send_full_chains = FALSE;
	/* Don't trust ICMP port-unreachable messages. */
	params->trust_icmp_messages = FALSE;
	/* Listen on all IP addresses. */
	params->default_ip = NULL;
	/* Use port 500 (default port) */
	params->default_port = NULL;
	/* Use configured NAT-T port */
	(void) ssh_snprintf((char *)nat_t_portstr, sizeof (nat_t_portstr),
	    "%d", nat_t_port);
	params->default_natt_port = (char *)nat_t_portstr;
	/*
	 * Base retry (rexmit packets) limit of 5.  We lower this from
	 * the SSH default, which was higher.
	 */
	if (params->base_retry_limit == 0)
		params->base_retry_limit = 5;
	/* NOTE:  base_retry_time* and base_expire_time* are in gram.y now. */

	/* Extended retry limit, use default values. */
	params->extended_retry_limit = 0;
	/* Extended retry timer, use default values. */
	params->extended_retry_timer = 0;
	params->extended_retry_timer_usec = 0;
	/* Extended retry timer max, use default values. */
	params->extended_retry_timer_max = 0;
	params->extended_retry_timer_max_usec = 0;
	/* Extended expire timer, use default values. */
	params->extended_expire_timer = 0;
	params->extended_expire_timer_usec = 0;
	/* Secret recreation timer, use default values. */
	params->secret_recreate_timer = 0;
	/* SPI size of 0, because zero_spi will be FALSE (for now). */
	params->spi_size = 0;
	params->zero_spi = FALSE;
	/* Maximum key length (encryption key), use default values. */
	params->max_key_length = 0;
	/*
	 * Number of simultaneous ISAKMP SAs, use default values.
	 * TODO: Make this configurable?
	 */
	params->max_isakmp_sa_count = 0;
	/*
	 * For now, keep all randomizer parameters at their defaults.
	 */
	params->randomizers_default_cnt = 0;
	params->randomizers_default_max_cnt = 0;
	params->randomizers_private_cnt = 0;
	params->randomizers_private_max_cnt = 0;
	params->randomizers_default_retry = 0;
	params->randomizers_private_retry = 0;
}

/* ARGSUSED */
static void
logmsg_handler(SshLogFacility fac, SshLogSeverity sev, const char *msg,
    void *context)
{
	syslog(fac | sev, "%s", msg);
}

/* ARGSUSED */
static void
audit_callback(SshAuditEvent event, SshUInt32 argc, SshAuditArgument argv,
    void *context)
{
}

static void
init_audit_context(SshAuditContext *contextp)
{
	/*
	 * Send the audit output from the ssh lib to syslog.  Accomplish
	 * this by (a) setting up an audit context, with no output file
	 * specified, and (b) registering a callback that will pass us a
	 * fully formatted message string, which we can dump to syslog.
	 */

	*contextp = ssh_audit_create(audit_callback, NULL);
	if (*contextp == NULL) {
		PRTDBG(D_OP, ("Could not enable libike auditing!"));
		return;
	}

	/*
	 * Second param is context pointer that will be sent to callback;
	 * currently, we don't use it, but might want to?
	 */
	ssh_log_register_callback(logmsg_handler, NULL);
}

/*
 * Utility function to convert one of those annoying SSH "IP address" forms
 * into a sockaddr.
 */
boolean_t
string_to_sockaddr(uchar_t *string, struct sockaddr_storage *sa)
{
	struct addrinfo hints;
	struct addrinfo *ai;
	int ai_rc;
	boolean_t rc;

	(void) memset(&hints, 0, sizeof (hints));
	hints.ai_flags = AI_NUMERICHOST;
	hints.ai_family = PF_UNSPEC;

	ai_rc = getaddrinfo((char *)string, NULL, &hints, &ai);
	switch (ai_rc) {
	case 0:
		(void) memcpy(sa, ai->ai_addr, MIN(ai->ai_addrlen,
		    sizeof (struct sockaddr_storage)));
		rc = B_TRUE;
		break;
	default:
		PRTDBG(D_OP,
		    ("getaddrinfo(\"%s\") failed with %d.", string, ai_rc));
		rc = B_FALSE;
		/* FALLTHRU */
	}

	freeaddrinfo(ai);
	return (rc);
}

/*
 * The evil twin that takes a sockaddr and goes to the SSH "IP address"
 * string.  Assume string has been allocated to be the appropriate
 * length, etc.
 */
boolean_t
sockaddr_to_string(const struct sockaddr_storage *ss, uchar_t *string)
{
	int rc, salen;

	switch (ss->ss_family) {
	case AF_INET:
		salen = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		salen = sizeof (struct sockaddr_in6);
		break;
	default:
		return (B_FALSE);
	}

	/*
	 * getnameinfo() should do all of the magic of scope_id mappings,
	 * too.
	 */
	rc = getnameinfo((struct sockaddr *)ss, salen, (char *)string,
	    NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

	return (rc == 0);
}

/*
 * Address cache and address entry routines.  In an MT in.iked, they should
 * have various locks around them.
 */

/* Look up an address entry. */
static addrentry_t *
addrentry_lookup(addrentry_t *first, struct sockaddr_storage *sa)
{
	addrentry_t *rc;
	boolean_t isv4 = (sa->ss_family == AF_INET);
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

	for (rc = first; rc != NULL; rc = rc->addrentry_next) {
		if (isv4 != rc->addrentry_isv4)
			continue;

		if (isv4) {
			if (sin->sin_addr.s_addr == rc->addrentry_addr4.s_addr)
				return (rc);
		} else {
			if (IN6_ARE_ADDR_EQUAL(&sin6->sin6_addr,
			    &rc->addrentry_addr6) &&
			    (!IN6_IS_ADDR_LINKLOCAL(&rc->addrentry_addr6) ||
			    sin6->sin6_scope_id == 0 ||
			    rc->addrentry_scopeid == 0 ||
			    sin6->sin6_scope_id == rc->addrentry_scopeid))
				return (rc);
		}
	}

	/* rc should be == NULL. */
	return (rc);
}

static void
addrentry_delete(addrentry_t *entry)
{
	*entry->addrentry_ptpn = entry->addrentry_next;
	if (entry->addrentry_next != NULL)
		entry->addrentry_next->addrentry_ptpn = entry->addrentry_ptpn;

	ssh_free(entry);
}

static void
addrcache_init(addrcache_t *addrcache)
{
	int i;

	for (i = 0; i < ADDRCACHE_BUCKETS; i++)
		addrcache->addrcache_bucket[i] = NULL;
}

void
addrcache_destroy(addrcache_t *addrcache)
{
	int i;

	for (i = 0; i < ADDRCACHE_BUCKETS; i++)
		while (addrcache->addrcache_bucket[i] != NULL)
			addrentry_delete(addrcache->addrcache_bucket[i]);
}

/*
 * Add an address to the address cache.	 The expiration is an absolute
 * (wall-clock) time.
 */
void
addrcache_add(addrcache_t *addrcache, struct sockaddr_storage *sa,
    time_t expiration)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
	boolean_t isv4;
	addrentry_t *entry, *first;
	int bucket_number;

	isv4 = (sa->ss_family == AF_INET);
	assert(isv4 || sa->ss_family == AF_INET6);

	bucket_number = (isv4) ? ADDRCACHE_HASH_V4(sin->sin_addr.s_addr) :
	    ADDRCACHE_HASH_V6(sin6->sin6_addr);
	first = addrcache->addrcache_bucket[bucket_number];

	entry = addrentry_lookup(first, sa);
	if (entry == NULL) {
		entry = ssh_malloc(sizeof (*entry));
		if (entry == NULL)
			return;

		entry->addrentry_isv4 = isv4;
		if (isv4) {
			entry->addrentry_addr4 = sin->sin_addr;
			entry->addrentry_scopeid = 0;
		} else {
			entry->addrentry_addr6 = sin6->sin6_addr;
			if (IN6_IS_ADDR_LINKLOCAL(&entry->addrentry_addr6))
				entry->addrentry_scopeid = sin6->sin6_scope_id;
			else
				entry->addrentry_scopeid = 0;
		}
		entry->addrentry_ptpn =
		    addrcache->addrcache_bucket + bucket_number;
		addrcache->addrcache_bucket[bucket_number] = entry;
		entry->addrentry_next = first;
		entry->addrentry_timeout = 0;
		entry->addrentry_num_p1_reqsent = 0;
		if (first != NULL)
			first->addrentry_ptpn = &entry->addrentry_next;
	}

	entry->addrentry_num_p1_reqsent++;
	entry->addrentry_addtime = ssh_time();
	entry->addrentry_timeout = MAX(expiration, entry->addrentry_timeout);
}

void
addrcache_delete(addrcache_t *addrcache, struct sockaddr_storage *sa)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
	boolean_t isv4;
	addrentry_t *entry, *first;
	int bucket_number;

	isv4 = (sa->ss_family == AF_INET);
	assert(isv4 || sa->ss_family == AF_INET6);

	bucket_number = (isv4) ? ADDRCACHE_HASH_V4(sin->sin_addr.s_addr) :
	    ADDRCACHE_HASH_V6(sin6->sin6_addr);
	first = addrcache->addrcache_bucket[bucket_number];

	entry = addrentry_lookup(first, sa);

	if (entry != NULL)
		addrentry_delete(entry);
}

addrentry_t *
addrcache_check(addrcache_t *addrcache, struct sockaddr_storage *sa)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
	boolean_t isv4;
	addrentry_t *entry, *first;
	int bucket_number;
	time_t	time_now;

	isv4 = (sa->ss_family == AF_INET);
	assert(isv4 || sa->ss_family == AF_INET6);

	bucket_number = (isv4) ? ADDRCACHE_HASH_V4(sin->sin_addr.s_addr) :
	    ADDRCACHE_HASH_V6(sin6->sin6_addr);
	first = addrcache->addrcache_bucket[bucket_number];

	entry = addrentry_lookup(first, sa);

	time_now  = ssh_time();
	if (entry != NULL && entry->addrentry_timeout <= time_now) {
		return (NULL);
	}
	return (entry);
}

/*
 * Flush all address caches after a full SADB flush
 */
void
flush_addrcache(void)
{
	ike_server_t *s;

	for (s = avl_first(&ike_servers); s != NULL;
	    s = AVL_NEXT(&ike_servers, s)) {
		addrcache_destroy(&s->ikesrv_addrcache);
	}
}

/*
 * PF_ROUTE may toss us a curveball due to bug 4869579 -- RTM_DELADDR for
 * interfaces which aren't actually there.  Defend against this.
 */
static void
delete_ike_server(struct sockaddr_storage *sa, const char *ifname, size_t iflen)
{
	uchar_t servname[NI_MAXHOST];
	char intf[LIFNAMSIZ];
	ike_server_t *ikesrv, template;
	ike_lif_t *ikelif, lif_template;

	(void) strlcpy(intf, ifname, MIN(iflen + 1, LIFNAMSIZ));
	if (!sockaddr_to_string(sa, servname)) {
		EXIT_RESTART("sockaddr_to_string failed!");
	}

	PRTDBG(D_OP, ("Removing %s address %s from in.iked service list...",
	    intf, servname));

	lif_template.ikelif_af = sa->ss_family;
	(void) strlcpy(lif_template.ikelif_name, intf, LIFNAMSIZ);
	ikelif = avl_find(&ike_lifs, &lif_template, NULL);
	if (ikelif == NULL) {
		PRTDBG(D_OP, ("  Spurious RTM_DELADDR"));
		return;
	}
	avl_remove(&ike_lifs, ikelif);
	ssh_free(ikelif);

	template.ikesrv_addr = *sa;
	ikesrv = avl_find(&ike_servers, &template, NULL);
	if (ikesrv == NULL) {
		PRTDBG(D_OP, ("  Address not being serviced."));
		return;
	}
	if (ikesrv->ikesrv_addrref == 0) {
		PRTDBG(D_OP, ("  Zero ref count on ike server!"));
		return;
	}
	ikesrv->ikesrv_addrref--;
	if (ikesrv->ikesrv_addrref > 0) {
		PRTDBG(D_OP, ("  %d more references", ikesrv->ikesrv_addrref));
		return;
	}
	PRTDBG(D_OP, ("  Last reference"));
	avl_remove(&ike_servers, ikesrv);
	num_server_contexts--;

	addrcache_destroy(&ikesrv->ikesrv_addrcache);
	ssh_ike_stop_server(ikesrv->ikesrv_ctx);
	ssh_free(ikesrv);
	PRTDBG(D_OP, ("  Now %d addresses being serviced.",
	    num_server_contexts));
}

/*
 * Used to filter out uninteresting addresses we shouldn't listen on.
 */
static boolean_t
skip_address(struct sockaddr_storage *sa)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
	uint32_t addr;

	/*
	 * Filter out addresses we know we don't want to listen to.
	 */

	switch (sa->ss_family) {
	case AF_INET:
		addr = ntohl(sin->sin_addr.s_addr);
		if (addr == INADDR_ANY)
			return (B_TRUE);
		if ((addr >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET)
			return (B_TRUE);
		if (IN_MULTICAST(addr))
			return (B_TRUE);
		break;
	case AF_INET6:
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr))
			return (B_TRUE);
		if (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr))
			return (B_TRUE);
		if (IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr))
			return (B_TRUE);
		break;
	}
	return (B_FALSE);
}

/*
 * If address is "interesting", add a new entry to the array of ike servers.
 */
void
add_new_ike_server(struct sockaddr_storage *sa, const char *ifname,
    size_t iflen)
{
	uchar_t servname[NI_MAXHOST];
	char intf[LIFNAMSIZ];
	ike_server_t template, *existing, *new;
	ike_lif_t lif_template, *ikelif;
	avl_index_t where;

	(void) strlcpy(intf, ifname, MIN(iflen + 1, LIFNAMSIZ));

	if (!sockaddr_to_string(sa, servname)) {
		EXIT_RESTART("sockaddr_to_string failed!");
	}

	if (skip_address(sa)) {
		PRTDBG(D_OP, ("Skipping %s address %s", intf, servname));
		return;
	}

	PRTDBG(D_OP, ("Adding %s address %s to in.iked service list...",
	    intf, servname));

	lif_template.ikelif_af = sa->ss_family;
	(void) strlcpy(lif_template.ikelif_name, intf, LIFNAMSIZ);
	ikelif = avl_find(&ike_lifs, &lif_template, &where);
	if (ikelif != NULL) {
		PRTDBG(D_OP, ("  Logical IF already exists"));
		return;
	}

	ikelif = ssh_malloc(sizeof (ike_lif_t));
	if (ikelif == NULL) {
		PRTDBG(D_OP, ("  Out of memory!"));
		return;
	}

	(void) strlcpy(ikelif->ikelif_name, intf, LIFNAMSIZ);
	ikelif->ikelif_af = sa->ss_family;
	avl_insert(&ike_lifs, ikelif, where);

	template.ikesrv_addr = *sa;
	existing = avl_find(&ike_servers, &template, &where);

	/* If we've already got it covered, then so be it. */
	if (existing != NULL) {
		existing->ikesrv_addrref++;
		PRTDBG(D_OP, ("  Address already exists: now %d users",
		    existing->ikesrv_addrref));
		return;
	}

	new = ssh_malloc(sizeof (ike_server_t));
	if (new == NULL) {
		EXIT_RESTART("  Out of memory!");
	}

	num_server_contexts++;

	PRTDBG(D_OP, ("  Adding entry #%d; IP address = %s, interface = %s.",
	    num_server_contexts, servname, intf));

	new->ikesrv_addrref = 1;
	new->ikesrv_addr = *sa;
	addrcache_init(&(new->ikesrv_addrcache));

	avl_insert(&ike_servers, new, where);

	/*
	 * Hardwire port 500, and use nat_t_port.
	 */
	new->ikesrv_ctx = ssh_ike_start_server(ike_context, servname,
	    MKSTR(IPPORT_IKE), nat_t_portstr, &policy_state, our_sa_handler,
	    NULL);
	if (new->ikesrv_ctx == NULL) {
		/*
		 * We may have been sent an address that then disappeared out
		 * from underneath us.  Nuke this entry, log it, and carry on.
		 */
		PRTDBG(D_OP, ("  Address failed to start an IKE server."));
		avl_remove(&ike_lifs, ikelif);
		ssh_free(ikelif);
		avl_remove(&ike_servers, new);
		addrcache_destroy(&new->ikesrv_addrcache);
		ssh_free(new);
		num_server_contexts--;
		return;
	}
	if (hide_outer_label) {
		if (!ssh_ike_enable_mac_bypass(new->ikesrv_ctx)) {
			EXIT_RESTART("  Could not set MAC bypass!");
		}
	}
	PRTDBG(D_OP, ("  Now %d addresses being serviced.",
	    num_server_contexts));
}

static int ike_server_cmp(const void *, const void *);

/*
 * Return an IKE server, based on the passed-in local address.
 */
ike_server_t *
get_server_context(struct sockaddr_storage *sa)
{
	ike_server_t *rc = NULL, template;
	avl_index_t where;
	struct sockaddr_in6 *sin6, *rsin6, *tsin6;

	PRTDBG(D_OP, ("Looking for %s in IKE daemon context...", sap(sa)));

	template.ikesrv_addr = *sa;

	rc = avl_find(&ike_servers, &template, &where);

	if ((rc != NULL) || (sa->ss_family != AF_INET6))
		return (rc);

	sin6 = (struct sockaddr_in6 *)sa;

	if (sin6->sin6_scope_id != 0)
		return (NULL);

	rc = avl_nearest(&ike_servers, where, AVL_AFTER);
	if (rc == NULL)
		return (NULL);

	tsin6 = (struct sockaddr_in6 *)&template.ikesrv_addr;
	rsin6 = (struct sockaddr_in6 *)&rc->ikesrv_addr;
	tsin6->sin6_scope_id = rsin6->sin6_scope_id;

	if (ike_server_cmp(&template, rc) != 0)
		return (NULL);

	return (rc);
}

/*
 * AVL expects a strict (-1, 0, 1) return rather than merely looking for the
 * negative, zero, positive values which many useful comparison functions
 * return.  Use this function to canonicalize.
 */
static int
avlify(int x)
{
	if (x < 0)
		return (-1);
	if (x > 0)
		return (1);
	return (0);
}

/*
 * Comparator for ike_lifs AVL tree
 */
static int
ike_lif_cmp(const void *a, const void *b)
{
	const ike_lif_t *la = a;
	const ike_lif_t *lb = b;

	if (la->ikelif_af < lb->ikelif_af)
		return (-1);
	if (la->ikelif_af > lb->ikelif_af)
		return (1);
	return (avlify(strcmp(la->ikelif_name, lb->ikelif_name)));
}

/*
 * Comparator for ike_servers AVL tree
 */
static int
ike_server_cmp(const void *a, const void *b)
{
	int rv;
	const ike_server_t *sa = a;
	const ike_server_t *sb = b;
	const struct sockaddr_storage *ssa = &sa->ikesrv_addr;
	const struct sockaddr_storage *ssb = &sb->ikesrv_addr;
	const struct sockaddr_in *sina = (const struct sockaddr_in *)ssa;
	const struct sockaddr_in *sinb = (const struct sockaddr_in *)ssb;
	const struct sockaddr_in6 *sin6a = (const struct sockaddr_in6 *)ssa;
	const struct sockaddr_in6 *sin6b = (const struct sockaddr_in6 *)ssb;

	if (ssa->ss_family < ssb->ss_family)
		return (-1);
	if (ssa->ss_family > ssb->ss_family)
		return (1);

	switch (sa->ikesrv_addr.ss_family) {
	case AF_INET:
		return (avlify(memcmp(&sina->sin_addr, &sinb->sin_addr,
		    sizeof (sina->sin_addr))));

	case AF_INET6:
		rv = memcmp(&sin6a->sin6_addr, &sin6b->sin6_addr,
		    sizeof (sin6a->sin6_addr));
		if (rv == 0)
			rv = sin6a->sin6_scope_id - sin6b->sin6_scope_id;
		return (avlify(rv));
	default:
		PRTDBG(D_OP, ("Odd family %d", sa->ikesrv_addr.ss_family));
		return (avlify(sa - sb));
	}

}

/*
 * Initialize array of ike servers.  (Lots stolen from in.rdisc.c.)
 */
static void
start_ike_servers_af(int af)
{
	struct lifnum lifn;
	struct lifconf lifc;
	struct lifreq *lifr;
	struct lifreq lifreq;
	int n, sock;
	char *buf;
	int numbufs, numifs;
	unsigned bufsize;

	sock = socket(af, SOCK_DGRAM, 0);
	if (sock < 0) {
		EXIT_FATAL2("Couldn't initialize socket for in.iked. "
		    "socket() error: %s", strerror(errno));
	}

	numbufs = 0;
	do {
		lifn.lifn_family = af;
		lifn.lifn_flags = LIFC_ALLZONES;
		lifn.lifn_count = 0;
		if (ioctl(sock, SIOCGLIFNUM, (char *)&lifn) < 0) {
			EXIT_MAINTAIN2(
			    "Couldn't determine number of interfaces for "
			    "in.iked. SIOCGLIFNUM: %s", strerror(errno));
		}

		/*
		 * Make the buffer larger than necessary so that a race
		 * condition between SIOCGLIFNUM and SIOCGLIFCONF gets much
		 * less likely.
		 */
		numifs = lifn.lifn_count + 8;
		if (numifs > numbufs)
			numbufs = numifs;
		bufsize = numbufs * sizeof (struct lifreq);
		buf = ssh_calloc(1, bufsize);
		if (buf == NULL) {
			EXIT_FATAL("Out of memory initializing interface "
			    "parameters for in.iked.");
		}
		lifc.lifc_family = af;
		lifc.lifc_len = bufsize;
		lifc.lifc_buf = buf;
		lifc.lifc_flags = LIFC_ALLZONES;

		if (ioctl(sock, SIOCGLIFCONF, (char *)&lifc) < 0) {
			if (errno != EINVAL)
				EXIT_FATAL2("Error initializing interfaces for "
				    "iked: ioctl(get interface conf): %s",
				    strerror(errno));
			/*
			 * The buffer is too small because an interface has
			 * been added between the SIOCGLIFNUM and SIOCGLIFCONF
			 * ioctl(2). Free the buffer and try again.
			 */
			ssh_free(buf);
			buf = NULL;
		} else {
			/*
			 * Be prepared in case the behaviour of SIOCGLIFCONF
			 * changes to match BSD and only the interfaces that
			 * fit are reported.
			 */
			if (lifc.lifc_len >=
			    (bufsize - sizeof (struct lifreq))) {
				ssh_free(buf);
				buf = NULL;
			}
		}

		/*
		 * Double the number of buffers so that the loop runs only
		 * logarithmically.
		 */
		numbufs <<= 1;
	} while (buf == NULL);

	lifr = lifc.lifc_req;
	for (n = lifc.lifc_len/sizeof (struct lifreq); n > 0; n--, lifr++) {
		if (lifr->lifr_addr.ss_family != af)
			continue;

		(void) strncpy(lifreq.lifr_name, lifr->lifr_name,
		    sizeof (lifr->lifr_name));
		if (ioctl(sock, SIOCGLIFFLAGS, (char *)&lifreq) < 0) {
			PRTDBG(D_OP,
			    ("Error getting interface flags for in.iked."
			    "ioctl(): %s", strerror(errno)));
			continue;
		}
		if ((lifreq.lifr_flags & IFF_UP) == 0)
			continue;

		/*
		 * Work around 6425953 -- find ifindex for link-local ifaddrs
		 * so we can listen.
		 */
		if (af == AF_INET6) {
			struct sockaddr_in6 *sin6;

			sin6 = (struct sockaddr_in6 *)&lifr->lifr_addr;
			if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
				/* Assume name is still there. */
				if (ioctl(sock, SIOCGLIFINDEX,
				    (char *)&lifreq) < 0) {
					PRTDBG(D_OP,
					    ("Error getting interface index "
					    "for in.iked: ioctl() %s",
					    strerror(errno)));
					continue;
				}
				sin6->sin6_scope_id = lifreq.lifr_index;
			}
		}
		add_new_ike_server(&(lifr->lifr_addr), lifr->lifr_name,
		    strlen(lifr->lifr_name));
	}

	assert(buf == (char *)lifc.lifc_req);
	ssh_free(buf);
	(void) close(sock);
}

static boolean_t have_label_priv = B_FALSE;

static void
priv_net_enable()
{
	if (have_label_priv)
		priv_set(PRIV_ON, PRIV_EFFECTIVE, PRIV_NEEDED_NET,
		    PRIV_NEEDED_LABEL, NULL);
	else
		priv_set(PRIV_ON, PRIV_EFFECTIVE, PRIV_NEEDED_NET,
		    NULL);
}

static void
priv_net_disable()
{
	if (have_label_priv)
		priv_set(PRIV_OFF, PRIV_EFFECTIVE, PRIV_NEEDED_NET,
		    PRIV_NEEDED_LABEL, NULL);
	else
		priv_set(PRIV_OFF, PRIV_EFFECTIVE, PRIV_NEEDED_NET,
		    NULL);
}

static void
start_ike_servers(void)
{
	avl_create(&ike_servers, ike_server_cmp, sizeof (ike_server_t),
	    offsetof(ike_server_t, ikesrv_link));
	avl_create(&ike_lifs, ike_lif_cmp, sizeof (ike_lif_t),
	    offsetof(ike_lif_t, ikelif_link));

	start_ike_servers_af(AF_INET);
	start_ike_servers_af(AF_INET6);
}

/*
 * Return a pointer to a sockaddr that is a particular RTA_* address in a
 * RTM_NEWADDR or RTM_DELADDR routing message.
 *
 * We need to be able to pull RTA_IFA and RTA_IFP out; as with any
 * other robust PF_ROUTE code we must avoid making assumptions about which
 * other addresses are present or absent.
 */
static struct sockaddr_storage *
get_addr(int rta, const ifa_msghdr_t *ifam, int count)
{
	struct sockaddr_storage *next, *rc = NULL;
	int placeholder = rta, addrs = ifam->ifam_addrs;
	uint8_t *ptr = (uint8_t *)(ifam + 1);
	uint8_t *endmsg = (((uint8_t *)ifam) + count);

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	next = (struct sockaddr_storage *)ptr;

	do {
		rc = next;

		if (addrs & 1) {
			switch (next->ss_family) {
			case AF_INET:
				ptr += sizeof (struct sockaddr_in);
				break;
			case AF_INET6:
				ptr += sizeof (struct sockaddr_in6);
				break;
			case AF_LINK:
				ptr += sizeof (struct sockaddr_dl);
				break;
			default:
				return (NULL);
			}
			if (ptr >= endmsg) {
				PRTDBG(D_OP,
				    ("Truncated address list? "
				    "start %p count 0x%x end %p cur %p",
				    (void *)ifam, count, (void *)endmsg,
				    (void *)ptr));
				rc = NULL;
				break;
			}
			/* LINTED E_BAD_PTR_CAST_ALIGN */
			next = (struct sockaddr_storage *)ptr;
		}

		addrs >>= 1;
		placeholder >>= 1;
	} while (placeholder != 0);

	return (rc);
}

/*
 * The event loop calls this when there's data waiting on the routing socket.
 * Read the routing socket and deal with it.
 *
 * Note:  If this function calls any library stuff, this function may be
 *	  re-entered from the event loop.  So be re-entrant.
 */
/* ARGSUSED */
static void
rts_handler(uint_t not_used, void *cookie)
{
	int s = (int)(uintptr_t)cookie;
	int rc;
	uint64_t buffer[PF_KEY_ALLOC_LEN];
	ifa_msghdr_t *ifam = (ifa_msghdr_t *)buffer;
	struct sockaddr_storage *ifa, *ifp;
	struct sockaddr_dl *dl;
	boolean_t up;

	rc = read(s, ifam, sizeof (buffer));
	if (rc <= 0) {
		if (rc == -1) {
			PRTDBG(D_OP,
			    ("routing socket read: %s", strerror(errno)));
			/* Should I exit()? */
		}
		return;
	}

	if (ifam->ifam_version != RTM_VERSION) {
		PRTDBG(D_OP, ("PF_ROUTE version (%d) mismatch, msg %d, len %d.",
		    ifam->ifam_version, ifam->ifam_type, rc));
		syslog(LOG_ERR, "PF_ROUTE version (%d) mismatch, msg %d.",
		    ifam->ifam_version, ifam->ifam_type);
		return;
	}

	switch (ifam->ifam_type) {
	case RTM_NEWADDR:
	case RTM_CHGADDR:
		up = B_TRUE;
		break;
	case RTM_DELADDR:
	case RTM_FREEADDR:
		up = B_FALSE;
		break;
	default:
		/* Uninteresting message. */
		PRTDBG(D_OP, ("Received uninteresting routing message %d.",
		    ifam->ifam_type));
		return;
	}

	/*
	 * Extract interface address from message.
	 */
	ifa = get_addr(RTA_IFA, ifam, rc);
	if (ifa == NULL)
		return;

	/*
	 * Extract interface name from message.
	 */
	ifp = get_addr(RTA_IFP, ifam, rc);
	if (ifp == NULL)
		return;

	dl = (struct sockaddr_dl *)ifp;

	/*
	 * Work around 6425939 -- if link-local, fill in interface index as
	 * sin6_scope_id
	 */
	if (ifa->ss_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ifa;
		if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
			sin6->sin6_scope_id = dl->sdl_index;
		}
	}

	/*
	 * All set; add/delete IKE server.
	 */
	if (up) {
		priv_net_enable();
		add_new_ike_server(ifa, dl->sdl_data, dl->sdl_nlen);
		priv_net_disable();
	} else {
		delete_ike_server(ifa, dl->sdl_data, dl->sdl_nlen);
	}
}

/*
 * Set up a routing socket for RTM_{NEW,DEL}ADDR.
 */
static void
rts_init()
{
	int s;

	s = socket(PF_ROUTE, SOCK_RAW, 0);

	if (s == -1) {
		EXIT_BADPERM2("routing socket: %s", strerror(errno));
	}

	(void) ssh_io_register_fd(s, rts_handler, (void *)(uintptr_t)s);
	ssh_io_set_fd_request(s, SSH_IO_READ);
}

/*
 * Initialize the random state.	 Start with (hrtime ^ clock), then add
 * goodness from /dev/random.
 */
static void
init_random(void)
{
	hrtime_t hrtime = gethrtime();
	time_t wallclock = ssh_time();
	uint8_t buf[RANDOM_FACTOR_LEN];
	int fd, rc;

	ssh_random_add_noise((uchar_t *)&hrtime, sizeof (hrtime_t));
	ssh_random_add_noise((uchar_t *)&wallclock, sizeof (wallclock));

	/*
	 * Read in bytes from /dev/random, if available.
	 */

	fd = open("/dev/random", O_RDONLY, 0);
	if (fd == -1) {
		PRTDBG(D_OP, ("Failure initializing random state:"
		    "open(/dev/random): %s", strerror(errno)));
	} else {
		rc = read(fd, buf, RANDOM_FACTOR_LEN);
		if (rc == -1) {
			PRTDBG(D_OP, ("Failure reading from /dev/random: %s",
			    strerror(errno)));
		} else {
			ssh_random_add_noise(buf, RANDOM_FACTOR_LEN);
		}
		(void) close(fd);
	}
}


/*
 * Handle SIGHUP safely with respect to the SSH event loop.
 * Re-load config, preshared keys, and certlib stuff.
 * Temporary until door interface is done.
 */
/* ARGSUSED */
static void
sighup(int signal, void *not_used)
{
	/*
	 * in.iked can be forced to re-read its configuration file
	 * using 'ikeadm read rule ...' or by sending it a SIGHUP.
	 * This should be done via 'svcadm refresh ike'
	 */
	PRTDBG(D_OP, ("Received SIGHUP signal...refreshing in.iked..."));
	(void) mutex_lock(&door_lock);
	(void) config_load(cfile, -1, B_TRUE);
	preshared_reload();
	cmi_reload();
	(void) mutex_unlock(&door_lock);
}


/*
 * Handle signals: for now, we'll catch
 *	SIGINT	  just exit for now; should do clean-up
 *	SIGTERM	  same as sigint
 *	SIGQUIT	  same as sigint
 *
 * According to libike/common/.../sshunixeloop.c, the only time this function
 * is called directly from the UNIX signal handler is if the program was
 * calling select(3).  We will be safe, therefore, from middle-of-cipher
 * problems, and other event loop issues.
 *
 * If we ever chuck libike in favor of an improved model, we will have to
 * revisit this issue.
 */
/* ARGSUSED */
static void
sig_handler(int signo, void *unused)
{
	char	buf[SIG2STR_MAX];

	switch (signo) {
	case SIGINT:
	case SIGTERM:
	case SIGQUIT:
		/*
		 * do clean-up and exit.
		 * clean-up is currently pretty minimal!
		 */
		(void) priv_set(PRIV_ON, PRIV_EFFECTIVE, PRIV_FILE_DAC_WRITE,
		    NULL);
		ike_door_destroy();
		(void) priv_set(PRIV_OFF, PRIV_EFFECTIVE, PRIV_FILE_DAC_WRITE,
		    NULL);
		closelog();
		/*
		 * Process has been killed, if running under smf(5)
		 * then exit quietly and let svc.startd(1M) do its job.
		 */
		(void) sig2str(signo, buf);
		EXIT_OK2("Received %s signal...exiting", buf);
	default:
		(void) sig2str(signo, buf);
		PRTDBG(D_OP, ("Received %s signal...ignoring", buf));
		break;
	}
}

/*PRINTFLIKE1*/
void
dbgprintf(char *fmt, ...)
{
	va_list	ap;
	char	msgbuf[BUFSIZ];
	struct timeval now;
	struct tm tmnow;
	char datestr[40];

	va_start(ap, fmt);

	(void) gettimeofday(&now, 0);
	(void) localtime_r(&now.tv_sec, &tmnow);

	if (*fmt == NULL) {
		/*
		 * Use a longer version of the date string to
		 * timestamp the log file when starting the daemon.
		 */
		(void) strftime(datestr, sizeof (datestr) - 1,
		    "%b %d %H:%M:%S: %Y (%z)", &tmnow);
		(void) fprintf(debugfile, "%s *** %s started ***\n",
		    datestr, progname);
		return;
	}

	(void) strftime(datestr, sizeof (datestr) - 1,
	    "%b %d %H:%M:%S:", &tmnow);
	(void) vsnprintf(msgbuf, BUFSIZ, fmt, ap);
	(void) fprintf(debugfile, "%s %s\n", datestr, msgbuf);
	(void) fflush(debugfile);

	va_end(ap);
}

static void
usage(const char *cmd)
{
	(void) fprintf(stderr,
	    gettext("usage: %s [ -f file ] [ -d ] [ -p privilege_level ]\n"),
	    cmd);
	(void) fprintf(stderr, gettext("       %s [-f file] -c\n"), cmd);
}

/* ARGSUSED */
static void
ike_fatal(const char *message, void *context)
{
	syslog(LOG_ALERT|LOG_DAEMON, message);
	(void) fprintf(stderr, "%s\n", message);
}

int
main(int argc, char *argv[])
{
	int c, i, err;
	char bel = 0x7;
	pid_t pid;
	int pipefds[2];

	/*
	 * This will be NULL if the daemon is started from the command line.
	 */
	my_fmri = getenv("SMF_FMRI");

	/*
	 * Behaviour of in.iked before it knew about smf(5) was to
	 * keep running even if the configuration was broken. When
	 * run under smf(5) the decision to ignore errors will depend
	 * on what the the ignore_errors property, this is just a default.
	 */
	if (my_fmri == NULL)
		ignore_errors = B_TRUE;
	else
		ignore_errors = B_FALSE;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	/*
	 * No access to printing keying material
	 * unless running in a privilege mode.
	 */
	pflag = B_TRUE;

	progname = strrchr(argv[0], '/');
	if (progname == NULL)
		progname = argv[0];
	else
		progname++;

	/* Parse the command-line. */
	while ((c = getopt(argc, argv, "dD:p:cf:ZN")) != EOF) {
		switch (c) {
		case 'd':
			debug = D_ALL;
			ignore_errors = B_TRUE;
			nofork = B_TRUE;
			break;
		case 'D':
			i = parsedbgopts(optarg);
			(void) printf("in.iked: Setting debug level to 0x%x\n",
			    i);
			debug |= i;
			break;
		case 'p':
			i = privstr2num(optarg);
			if ((i < 0) || (i > IKE_PRIV_MAXIMUM)) {
				EXIT_BADCONFIG2("Bad privilege flag: %s",
				    optarg);
			}
			(void) printf(
			    "in.iked: Setting privilege level to %d\n", i);
			privilege = i;
			if (privilege >= IKE_PRIV_KEYMAT)
				pflag = B_FALSE;
			break;
		case 'f':
			cfile = optarg;
			break;
		case 'c':
			/*
			 * Just check config and exit, made sure the
			 * debug level is sufficient to see the errors.
			 */
			cflag = B_TRUE;
			break;
		case 'N':
			do_natt = B_FALSE;
			break;
		case 'Z':
			/*
			 * if using a debug libike, turn
			 * on the libike debugging
			 */
			ssh_debug_set_level_string((uchar_t *)"global=999");
			break;
		case '?':
			usage(progname);
			/*
			 * This should cause the service to enter maintenance.
			 */
			EXIT_BADCONFIG(
			    "Incorrect or missing command parameters.");
		}
	}

	/*
	 * Read in some defaults from the smf(5) framework.
	 * These can be overridden using ikeadm(1M). Don't
	 * expect any of these parameters to be set if in.iked
	 * is started outside of the smf(5) framework.
	 */
	if (my_fmri != NULL)
		get_scf_properties();

	/* Timestamp the log file with long date string. */
	PRTDBG(D_OP, (""));

	if (cfile == NULL) {
		cfile = CONFIG_FILE;
		PRTDBG(D_OP, ("Configuration file not defined using %s.",
		    cfile));
	}

	/*
	 * Check the config file syntax. This option will typically be used
	 * from the command line. Turn on enough debug to capture any errors.
	 * This program will exit after checking the configuration.
	 */

	if (cflag) {
		debug |= D_OP;
		config_check();
	}

	/*
	 * Privileges as follows:
	 *
	 * Keep running as root because it needs to read, write, create,
	 * and delete files as root.  Giving this process all of those
	 * privileges actually almost defeats the point.
	 *
	 * sys_ip_config:   for opening PF_KEY socket, setsockopt
	 * proc_fork:	    to fork as daemon - privilege revoked upon
	 * 			use and not inheritable to forked child.
	 * 			This is the one basic privilege we need.
	 * file_dac_write:  to mount/unmount ike_door
	 * net_privaddr:    for binding to port 500
	 *
	 */
	if (_create_daemon_lock(IKED, 0, 0) < 0) {
		EXIT_BADPERM2("Insufficient privilege to create daemon lock "
		    "file: %s\n", strerror(errno));
	}

	/*
	 * Initialize daemon privileges - basic implied.
	 *
	 * On a labeled system we may be running in the global zone
	 * and in a position to do label management, so hang onto those
	 * privileges if we can.
	 *
	 * We may also be running in an exclusive non-global zone
	 * without the label-management-related privileges;
	 * __init_daemon_priv fails if requested privs are missing.
	 */
	if (priv_ineffect(PRIV_NET_MAC_IMPLICIT) &&
	    priv_ineffect(PRIV_NET_MAC_AWARE) &&
	    priv_ineffect(PRIV_NET_BINDMLP)) {
		have_label_priv = B_TRUE;
		err = __init_daemon_priv(PU_LIMITPRIVS, 0, 0,
		    PRIV_NEEDED_ALL, PRIV_NEEDED_LABEL, NULL);
	} else {
		err = __init_daemon_priv(PU_LIMITPRIVS, 0, 0,
		    PRIV_NEEDED_ALL, NULL);
	}

	if (err == -1) {
		/*
		 * This condition could occur if a non-privileged
		 * user tried to start the daemon, or if an attempt was
		 * made to run the daemon in a zone without sufficient
		 * privileges.
		 */
		EXIT_BADPERM("Insufficient privileges in the "
		    "current environment for the daemon to run.");
	}

	/*
	 * Trim down limit and permitted sets to just
	 * what we could possibly need.
	 */

	/* Get rid of basic privileges we'll never need */
	(void) priv_set(PRIV_OFF, PRIV_ALLSETS,
	    PRIV_FILE_LINK_ANY,
	    PRIV_PROC_INFO,
	    PRIV_PROC_EXEC,
	    NULL);

	/* We can drop effective set until we need it */
	if (have_label_priv)
		(void) priv_set(PRIV_OFF, PRIV_EFFECTIVE,
		    PRIV_PROC_FORK, PRIV_NEEDED_ALL, PRIV_NEEDED_LABEL, NULL);
	else
		(void) priv_set(PRIV_OFF, PRIV_EFFECTIVE,
		    PRIV_PROC_FORK, PRIV_NEEDED_ALL, NULL);

	/*
	 * Register our own printer for ssh_fatal() messages.
	 */
	ssh_debug_register_callbacks(ike_fatal, NULL, NULL, NULL);

	/*
	 * Open PF_KEY socket for our SA handler, which is passed into
	 * the IKE server.  We do this early, to catch attempts to run
	 * the daemon without root permission (and besides, nothing else
	 * matters if this fails; we have to exit!).
	 */

	(void) priv_set(PRIV_ON, PRIV_EFFECTIVE, PRIV_SYS_IP_CONFIG, NULL);
	if (!open_pf_key()) {
		EXIT_BADPERM2("Error opening PF_KEY socket: %s",
		    strerror(errno));
	}
	(void) priv_set(PRIV_OFF, PRIV_EFFECTIVE, PRIV_SYS_IP_CONFIG, NULL);

	/*
	 * Load the initial config file.
	 */
	(void) config_load(cfile, -1, B_TRUE);
	if (pkcs11_path == NULL) {
		pkcs11_path = ikestats.st_pkcs11_libname;
		/*
		 * Don't pick an absolute pathname in this case, so we can
		 * resolve 64-bit vs. 32-bit easier.
		 */
		(void) strlcpy(pkcs11_path, "libpkcs11.so",
		    sizeof (ikestats.st_pkcs11_libname));
	}

	/* Get configuration parameters and initialize the IKE parameters. */
	init_ike_params(&ike_params);

	/* We cannot register events until we call this, so initialize! */
	ssh_event_loop_initialize();

	/* Init the crypto routines. */
	x509config = &x509config_storage;
	ssh_x509_library_set_default_config(x509config);
	if (!ssh_x509_library_initialize(x509config))
		EXIT_FATAL("Failed to initialize X.509 library.");

	/*
	 * Initialize audit_context.  Unfortunately, the docs inadequately
	 * describe the function right now, but let's try something anyway.
	 */
	init_audit_context(&audit_context);

	/* Initalize and update random state. */
	init_random();

	/*
	 * Read any configuration state from files and what-not.  This
	 * includes:
	 */

	/* Pre-shared IKE keys. */
	preshared_init();

	/*
	 * Now that we've done all our reading from files and basic config,
	 * let's go into the background to actually do our work.
	 */
	if (!nofork) {
		int read_rc;
		char inbuf;

		/*
		 * Create a pipe so the parent can keep track of the child
		 * during the cmi_init() portion.  The cmi_init() portion
		 * has to be done post-fork() because of PKCS#11 brain-
		 * damage (sessions do not have to survive fork() invocations).
		 */
		if (pipe(pipefds) != 0)
			EXIT_FATAL2("Couldn't create pipe, error: %s",
			    strerror(errno));

		/* We need to fork now */
		(void) priv_set(PRIV_ON, PRIV_EFFECTIVE, PRIV_PROC_FORK, NULL);

		switch (fork()) {
		case 0:
			/* Child */
			(void) close(pipefds[0]);
			break;
		case -1:
			EXIT_FATAL2("Couldn't fork, error: %s",
			    strerror(errno));
		default:
			(void) close(pipefds[1]);
			/* Parent */
			while ((read_rc =
			    read(pipefds[0], &inbuf, sizeof (inbuf))) != -1) {
				if (read_rc == 0) {
					EXIT_RESTART(
					    "EOF from pipe after fork().");
				}
				/*
				 * If the byte read is a BEL (0x7) byte,
				 * I can exit.
				 */
				if (inbuf == 0x7) {
					EXIT_OK(NULL);
				}
			}
			EXIT_RESTART("Error reading from pipe after fork().");
		}
		(void) close(0);
		(void) close(1);

		(void) open("/", 0);
		(void) dup2(0, 1);
		(void) setsid();
		/* fork() again to avoid controlling terminal problems. */
		switch (fork()) {
		case 0:
			/* Child - Keep running. */
			(void) priv_set(PRIV_OFF, PRIV_ALLSETS,
			    PRIV_PROC_FORK, NULL);
			break;
		case -1:
			EXIT_MAINTAIN2("Couldn't fork post-setsid(), error: %s",
			    strerror(errno));
		default:
			EXIT_OK(NULL);
		}
	}

	/*
	 * Before we try to start the in.iked process, check
	 * for the existence of the PID file and, if it exists,
	 * make sure that the associated process is not running.
	 */

	pid = _enter_daemon_lock(IKED);

	if (!nofork) {
		/* At this point, signal the parent to stop. */
		if (write(pipefds[1], &bel, sizeof (bel)) != sizeof (bel))
			EXIT_FATAL2("Couldn't write on pipe to parent: %s",
			    strerror(errno));
		(void) close(pipefds[1]);
	}

	switch (pid) {
		case 0:
			PRTDBG(D_OP, ("Unique instance of in.iked started."));
			break;
		case -1:
			EXIT_MAINTAIN2("error while locking daemon: %s",
			    strerror(errno));
		default:
			EXIT_MAINTAIN2("Can't start in.iked, process already "
			    "running with PID %d", pid);
	}

	/*
	 * Clean up stderr. From this point only use PRTDBG().
	 */
	(void) close(2);
	(void) dup2(0, 2);

	/* Public keys/CA root certs and CMI initialization */
	if (!cmi_init())
		EXIT_FATAL("Could not initialize certificate database.");

	/*
	 * Fire up the SSH ISAKMP library.
	 * (Do so after cmi_init() so we can accelerate the Oakley groups.)
	 */
	ike_context = ssh_ike_init(&ike_params, audit_context);

	/* Assuming we returned from ssh_ike_init(), see what's going on. */
	if (ike_context == NULL) {
		EXIT_FATAL("Could not initialize libike.");
	}

	/* catch a few signals... */
	ssh_register_signal(SIGHUP, sighup, NULL);
	ssh_register_signal(SIGTERM, sig_handler, NULL);
	ssh_register_signal(SIGINT, sig_handler, NULL);
	ssh_register_signal(SIGQUIT, sig_handler, NULL);

	/* should we ignore SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU? */

	priv_net_enable();
	/*
	 * Start the IKE server, which listens on port 500.
	 * Start individual ones for each IP address.
	 */
	start_ike_servers();
	priv_net_disable();

	/*
	 * If I reach this point, I have to do something to:
	 *
	 * 1.) Poll PF_KEY
	 * 2.) Let the IKE library handle inbound stuff.
	 */
	pf_key_init();

	/*
	 * Init a routing socket for ike server adds/deletes.
	 */
	rts_init();

	/*
	 * Initialize the door server
	 */
	(void) priv_set(PRIV_ON, PRIV_EFFECTIVE, PRIV_FILE_DAC_WRITE, NULL);
	ike_door_init();
	(void) priv_set(PRIV_OFF, PRIV_EFFECTIVE, PRIV_FILE_DAC_WRITE, NULL);

	/*
	 * Schedule any grim-reaper threads here if needed.
	 */

	/*
	 * Now that everything's set up, run the event loop.
	 * The configuration file has been parsed at this point and
	 * any errors detected, disable exit on error from this point.
	 */
	ignore_errors = B_TRUE;
	ssh_event_loop_run();

	/* NOTREACHED */
	return (0);
}

void
get_scf_properties(void)
{
	char *p;
	int fd;
	FILE *ofile;
	scf_simple_prop_t *prop;
	uint8_t *u8p;
	char *config_name;
	char config[] = {"config"};
	char alt_config[] = {"alt_config"};
	char propmsg[] = {"Property \"%s\" set to: \"%s\""};

	config_name = alt_config;

	PRTDBG(D_OP, ("Reading service properties from smf(5) repository."));

	if ((prop = scf_simple_prop_get(NULL, NULL, config_name,
	    "config_file")) == NULL) {
		/*
		 * Try again using the default configuration as the
		 * alternate configuration does not exist.
		 */
		config_name = config;
		prop = scf_simple_prop_get(NULL, NULL, config_name,
		    "config_file");
	}

	if (prop != NULL) {
		if ((p = scf_simple_prop_next_astring(prop)) != NULL) {
			/*
			 * cfile could be NULL, thats OK for now as this
			 * can be overridden later.
			 */
			cfile = ssh_strdup(p);
		}
		scf_simple_prop_free(prop);
	}

	/* Turn on enough debug to be useful. */
	debug |= D_OP;

	if ((prop = scf_simple_prop_get(NULL, NULL, config_name,
	    "debug_logfile")) != NULL) {
		if ((p = scf_simple_prop_next_astring(prop)) != NULL) {
			/*
			 * Because in.iked calls fork(), the log file is the
			 * only place that errors can be recorded. Treat log
			 * file problems as fatal.
			 */
			fd = open(p, O_RDWR | O_CREAT | O_APPEND,
			    S_IRUSR | S_IWUSR);

			if (fd < 0) {
				EXIT_MAINTAIN2("Can't open debug_logfile: %s",
				    strerror(errno));
			}
			ofile = fdopen(fd, "a+");
			if (ofile == NULL) {
				EXIT_MAINTAIN2("fopen of debug_logfile: %s",
				    strerror(errno));
			}
			/*
			 * This message will go to stdout or the smf(5) log.
			 */
			PRTDBG(D_OP, ("Errors and debug messages will be "
			    "written to: %s", p));
			debugfile = ofile;
		}
		scf_simple_prop_free(prop);
	} else {
		/* somebody deleted this property, messages go to stdout */
		PRTDBG(D_OP,
		    ("Warning: debug_logfile property does not exist."));
	}

	PRTDBG(D_OP, ("Using \"%s\" property group.", config_name));
	if (cfile != NULL)
		PRTDBG(D_OP, (propmsg, "config_file", cfile));
	else
		PRTDBG(D_OP, (propmsg, "config_file", "NULL"));

	/*
	 * This switch allows in.iked to start even if the configuration
	 * file is broken, this gives backward compatibility with
	 * the pre-SMF in.iked. The running in.iked will need to be configured
	 * via ikeadm(1M) before it does anything useful.
	 *
	 * The following functions will fail silently if the daemon is started
	 * from the command line.
	 */
	if ((prop = scf_simple_prop_get(NULL, NULL, config_name,
	    "ignore_errors")) != NULL) {
		if ((u8p = scf_simple_prop_next_boolean(prop)) != NULL) {
			ignore_errors = (boolean_t)*u8p;
			if (ignore_errors) {
				PRTDBG(D_OP, (propmsg,
				    "ignore_errors", "true"));
				PRTDBG(D_OP, ("Configuration file errors will "
				    "not cause in.iked to exit."));
			} else {
				PRTDBG(D_OP, (propmsg,
				    "ignore_errors", "false"));
			}
		}
		scf_simple_prop_free(prop);
	}
	if ((prop = scf_simple_prop_get(NULL, NULL, config_name,
	    "debug_level")) != NULL) {
		if ((p = scf_simple_prop_next_astring(prop)) != NULL) {
			PRTDBG(D_OP, (propmsg, "debug_level", p));
			debug = dbgstr2num(p);
		}
		scf_simple_prop_free(prop);
	}
	if ((prop = scf_simple_prop_get(NULL, NULL, config_name,
	    "admin_privilege")) != NULL) {
		if ((p = scf_simple_prop_next_astring(prop)) != NULL) {
			PRTDBG(D_OP, (propmsg, "admin_privilege", p));
			privilege = privstr2num(p);
			if ((privilege < 0) ||
			    (privilege > IKE_PRIV_MAXIMUM)) {
				EXIT_MAINTAIN2("Bad property value for "
				    "\"admin_privilege\": \"%s\"", p);
			}
		}
		scf_simple_prop_free(prop);
	}
}
