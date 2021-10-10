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
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/pfkeyv2.h>
#include <net/if.h>
#include <locale.h>
#include <stropts.h>
#include <ipsec_util.h>
#include <netdb.h>
#include <sys/cladm.h>
#include <strings.h>

#include "defs.h"

#include <ike/ssheloop.h>
#include <ike/sshtimeouts.h>
#include <ike/sshrandom.h>
#include <ike/isakmp_internal.h>

#define	SAMSG_BUFF_LEN 2048

static int handler_socket;

/*
 * Added to support Solaris Cluster. The socket is used by the daemon
 * to let Solaris Cluster know when change to SADB is initiated.
 */
static int cluster_socket;
static struct sockaddr_in cli_addr;

static boolean_t extract_exts(sadb_msg_t *, parsedmsg_t *, int, ...);
static void pfkey_request(pfkeyreq_t *);
static void handle_reply(sadb_msg_t *);
static void update_complete(pfkeyreq_t *, sadb_msg_t *);
static void add_complete(pfkeyreq_t *, sadb_msg_t *);
static void gotspi(pfkeyreq_t *, sadb_msg_t *);
static void delete_finish(pfkeyreq_t *, sadb_msg_t *);
static void handle_delete(sadb_msg_t *);
static void handle_flush(sadb_msg_t *);
static void handle_expire(sadb_msg_t *);
static void handle_acquire(sadb_msg_t *, boolean_t);
static void handle_register(sadb_msg_t *);
static void pfkey_analyze_error(pfkeyreq_t *, sadb_msg_t *);
static void delete_p2sa(struct sockaddr_storage *, struct sockaddr_storage *,
    sadb_ident_t *, sadb_ident_t *, sadb_x_kmc_t *, uint32_t, uint8_t);
static void handle_idle_timeout(sadb_msg_t *samsg);

static const p2alg_t *find_esp_auth_alg(uint_t);
static const p2alg_t *find_auth_alg(uint_t);
static void send_early_negative_acquire(sadb_msg_t *, int);

static boolean_t in_cluster_mode = B_FALSE;

p2alg_t *p2_ah_algs = NULL;
p2alg_t *p2_esp_auth_algs = NULL;
p2alg_t *p2_esp_encr_algs = NULL;

/*
 * State needed to send a RESPONDER-LIFETIME (or DELETE) message back.
 */
typedef struct rl_act_s {
	uint32_t my_spi;
	uint64_t my_lifetime_secs;
	uint64_t my_lifetime_kb;
	struct sockaddr_storage my_local;
	SshIkeNegotiation my_neg;
} rl_act_t;

static void send_delete(rl_act_t *, int, const char *);

/*
 * Deal with PF_KEY sockets and data structures in this file.
 */

static const char *pfkey_opcodes[] = {
	"RESERVED", "GETSPI", "UPDATE", "ADD", "DELETE", "GET",
	"ACQUIRE", "REGISTER", "EXPIRE", "FLUSH", "DUMP", "X_PROMISC",
	"X_INVERSE_ACQUIRE", "X_UPDATEPAIR", "X_DELPAIR", "X_DELPAIR_STATE"
};

static const char *
pfkey_type(unsigned int type)
{
	if (type > SADB_MAX)
		return ("ILLEGAL");
	else
		return (pfkey_opcodes[type]);
}

static const char *pfkey_satypes[] = {
	"UNSPEC", "<undef>", "AH", "ESP", "<undef>", "RSVP", "OSPFV2",
	"RIPV2", "MIP"
};

static const char *
pfkey_satype(unsigned int type)
{
	if (type > SADB_SATYPE_MAX)
		return ("ILLEGAL");
	else
		return (pfkey_satypes[type]);
}

/* Deal with algorithm name lookups */

static const algindex_t algtype_table[] = {
	{ "None", SADB_X_ALGTYPE_NONE },
	{ "Auth", SADB_X_ALGTYPE_AUTH },
	{ "Crypt", SADB_X_ALGTYPE_CRYPT },
	{ "Compress", SADB_X_ALGTYPE_COMPRESS },
	{ NULL, 0 }
};

static const char *
alg_to_string(int doi_number, const algindex_t *algindex)
{
	int i;

	for (i = 0; algindex[i].desc; i++)
		if (doi_number == algindex[i].doi_num)
			return (algindex[i].desc);
	return ("unknown");
}

char *
kef_alg_to_string(int algnum, int protonum, char *algname)
{
	struct ipsecalgent *testentry;
	int error;

	testentry = getipsecalgbynum(algnum, protonum, &error);
	if (testentry == NULL || testentry->a_names[0] == NULL)
		(void) ssh_snprintf(algname, 80, "unknown");
	else
		(void) ssh_snprintf(algname, 80, "%s", testentry->a_names[0]);

	/* safe to use on a NULL pointer */
	(void) freeipsecalgent(testentry);
	return (algname);
}

/*
 * For variable length algorithms, we need to get the default key length.
 * This used to be calculated using the alg_increment and the alg_defincr
 * values from the SADB_REGISTER message from the kernel.
 * Now we will obtain it using getipsecprotobynum(3NSL), the fact that
 * we are looking this up here means the kernel has already verified the
 * algorithm, so its safe to use what was entered via ipsecalgs(1m).
 * The first element in the a_key_sizes array is the default key length.
 */
void
get_default_keylen(int protonum, p2alg_t *p2_alg)
{
	struct ipsecalgent *testentry;
	int error;

	if (p2_alg == NULL)
		return;

	testentry = getipsecalgbynum(p2_alg->p2alg_doi_num, protonum, &error);
	if (testentry == NULL)
		p2_alg->p2alg_default_incr = 0;
	else
		p2_alg->p2alg_default_incr = testentry->a_key_sizes[0];

	/* safe to use on a NULL pointer */
	(void) freeipsecalgent(testentry);
}

/*
 * In order to nicely "interoperate" with the ssh string constants,
 * accept 'name-cbc' in addition to 'name' for each encr alg.
 */
uint8_t
encr_alg_lookup(const char *name, int *minlow, int *maxhigh)
{
	*minlow = *maxhigh = 0;
	if (strcasecmp(name, "DES") == 0 ||
	    strcasecmp(name, "DES-CBC") == 0) {
		return (SSH_IKE_VALUES_ENCR_ALG_DES_CBC);
	}
	if (strcasecmp(name, "Blowfish") == 0 ||
	    strcasecmp(name, "Blowfish-CBC") == 0) {
		/*
		 * No explicit default for Blowfish due to RFC 2451,
		 * but most implementations agree it should be 128.
		 * We treat blowfish as a single-key-size 128-bit cipher.
		 */
		return (SSH_IKE_VALUES_ENCR_ALG_BLOWFISH_CBC);
	}
	if (strcasecmp(name, "3DES") == 0 ||
	    strcasecmp(name, "3DES-CBC") == 0) {
		return (SSH_IKE_VALUES_ENCR_ALG_3DES_CBC);
	}
	if (strcasecmp(name, "AES") == 0 ||
	    strcasecmp(name, "AES-CBC") == 0) {
		*minlow = 128;
		*maxhigh = 256;
		return (SSH_IKE_VALUES_ENCR_ALG_AES_CBC);
	}
	return (0);
}

/*
 * The event loop calls this when there's data waiting on a PF_KEY socket.
 * Read the PF_KEY message and deal with it.  (This may be the start of
 * another file.)
 *
 * Note:  If this function calls any library stuff, this function may
 *	  be re-entered from the event loop.  So be re-entrant.
 */
/* ARGSUSED */
static void
pf_key_handler(uint_t events, void *cookie)
{
	int s = (int)(uintptr_t)cookie;
	int rc;
	sadb_msg_t *samsg;
	int pid = getpid();
	int length = 0;

	/*
	 * Since we're hitting this relatively frequently,
	 * fflush stdout/stderr here so log files don't get behind..
	 */
	(void) fflush(stdout);
	(void) fflush(stderr);

	if (ioctl(s, I_NREAD, &length) < 0) {
		PRTDBG(D_OP, ("PF_KEY ioctl(I_NREAD): %s", strerror(errno)));
		return;
	}

	/* handle zero length message case */

	if (length == 0) {
		PRTDBG(D_OP, ("PF_KEY ioctl: zero length message"));
		return;
	}

	samsg = ssh_malloc(length);
	if (samsg == NULL) {
		PRTDBG(D_OP, ("PF_KEY: malloc failure"));
		return;
	}

	rc = read(s, samsg, length);
	if (rc <= 0) {
		if (rc == -1) {
			PRTDBG(D_OP, ("PF_KEY read: %s", strerror(errno)));
			/* Should I exit()? */
		}
		ssh_free(samsg);
		return;
	}

	PRTDBG(D_PFKEY, ("Handling data on PF_KEY socket:\n"
	    "\t\t\t\t\t SADB msg: message type %d (%s), SA type %d (%s),\n"
	    "\t\t\t\t\t pid %d, sequence number %u,\n"
	    "\t\t\t\t\t error code %d (%s), diag code %d (%s), length %d",
	    samsg->sadb_msg_type, pfkey_type(samsg->sadb_msg_type),
	    samsg->sadb_msg_satype, pfkey_satype(samsg->sadb_msg_satype),
	    samsg->sadb_msg_pid,
	    samsg->sadb_msg_seq,
	    samsg->sadb_msg_errno, strerror(samsg->sadb_msg_errno),
	    samsg->sadb_x_msg_diagnostic,
	    keysock_diag(samsg->sadb_x_msg_diagnostic),
	    samsg->sadb_msg_len));

	/*
	 * If it might be a reply to us, handle it.
	 */
	if (samsg->sadb_msg_pid == pid) {
		handle_reply(samsg);
		return;
	}
	/*
	 * Silently pitch the message if it's an error reply to someone else.
	 */
	if (samsg->sadb_msg_errno != 0) {
		PRTDBG(D_PFKEY, ("  Reply not for us, dropped"));
		ssh_free(samsg);
		return;
	}

	/*
	 * We only care about ACQUIRE and EXPIRE messages on this socket.
	 */
	switch (samsg->sadb_msg_type) {
	case SADB_ACQUIRE:
		handle_acquire(samsg, B_TRUE);
		return;
	case SADB_EXPIRE:
		handle_expire(samsg);
		return;
	case SADB_DELETE:
	case SADB_X_DELPAIR:
		handle_delete(samsg);
		return;
	case SADB_FLUSH:
		handle_flush(samsg);
		return;
	case SADB_REGISTER:
		handle_register(samsg);
		ssh_free(samsg);
		return;
	}

	ssh_free(samsg);
	PRTDBG(D_PFKEY, ("  SADB message type unknown, ignored."));
}

/*
 * Create a PF_KEY socket and put it in the event loop list of active FDs.
 */
void
pf_key_init(void)
{
	uint64_t buffer[128];
	sadb_msg_t *samsg = (sadb_msg_t *)buffer;
	sadb_x_ereg_t *ereg = (sadb_x_ereg_t *)(samsg + 1);
	boolean_t ah_ack = B_FALSE, esp_ack = B_FALSE;
	pid_t pid = getpid();
	int rc;

	/*
	 * Extended REGISTER for AH/ESP combination(s).
	 */

	PRTDBG(D_PFKEY, ("Initializing PF_KEY socket..."));

	samsg->sadb_msg_version = PF_KEY_V2;
	samsg->sadb_msg_type = SADB_REGISTER;
	samsg->sadb_msg_errno = 0;
	samsg->sadb_msg_satype = SADB_SATYPE_UNSPEC;
	samsg->sadb_msg_reserved = 0;
	samsg->sadb_msg_seq = 1;
	samsg->sadb_msg_pid = pid;
	samsg->sadb_msg_len = SADB_8TO64(sizeof (*samsg) + sizeof (*ereg));

	ereg->sadb_x_ereg_len = SADB_8TO64(sizeof (*ereg));
	ereg->sadb_x_ereg_exttype = SADB_X_EXT_EREG;
	ereg->sadb_x_ereg_satypes[0] = SADB_SATYPE_ESP;
	ereg->sadb_x_ereg_satypes[1] = SADB_SATYPE_AH;
	ereg->sadb_x_ereg_satypes[2] = SADB_SATYPE_UNSPEC;

	rc = write(handler_socket, buffer, sizeof (*samsg) + sizeof (*ereg));
	if (rc == -1) {
		EXIT_FATAL2("Extended register write error: %s",
		    strerror(errno));
	}

	do {

		do {
			rc = read(handler_socket, buffer, sizeof (buffer));
			if (rc == -1) {
				EXIT_FATAL2("Extended register read error: %s",
				    strerror(errno));
			}

		} while (samsg->sadb_msg_seq != 1 ||
		    samsg->sadb_msg_pid != pid ||
		    samsg->sadb_msg_type != SADB_REGISTER);

		if (samsg->sadb_msg_errno != 0) {
			if (samsg->sadb_msg_errno == EPROTONOSUPPORT) {
				PRTDBG(D_PFKEY,
				    ("Protocol %d not supported.",
				    samsg->sadb_msg_satype));
			} else {
				EXIT_FATAL2("Extended REGISTER returned: %s",
				    strerror(samsg->sadb_msg_errno));
			}
		}

		switch (samsg->sadb_msg_satype) {
		case SADB_SATYPE_ESP:
			esp_ack = B_TRUE;
			PRTDBG(D_PFKEY, ("ESP initial REGISTER with SADB..."));
			break;
		case SADB_SATYPE_AH:
			ah_ack = B_TRUE;
			PRTDBG(D_PFKEY, ("AH initial REGISTER with SADB..."));
			break;
		default:
			EXIT_FATAL2("Bad satype in extended register ACK %d.",
			    samsg->sadb_msg_satype);
		}

		handle_register(samsg);
	} while (!esp_ack || !ah_ack);

	label_update();

	(void) ssh_io_register_fd(handler_socket, pf_key_handler,
	    (void *)(uintptr_t)handler_socket);
	ssh_io_set_fd_request(handler_socket, SSH_IO_READ);
}

/*
 * Handle the PF_KEY SADB_DELETE message.  This'll involve transmitting an
 * ISAKMP DELETE notification... IF we can find a phase 1 SA.
 *
 * Lots of stuff here that needs to have refrele semantics when we go MT-hot.
 */
static void
handle_delete(sadb_msg_t *samsg)
{
	parsedmsg_t pmsg;
	sadb_sa_t *assoc;
	sadb_ident_t *srcid, *dstid;
	struct sockaddr_storage *src, *dst;
	sadb_x_kmc_t *cookie;
	sadb_x_pair_t *pair;

	PRTDBG(D_PFKEY, ("Handling SADB delete..."));

	/*
	 * Use results of delete to call ssh_ike_connect_delete() to notify
	 * the other end of this SAs deletion.
	 */

	if (!extract_exts(samsg, &pmsg, 3, SADB_EXT_ADDRESS_DST,
	    SADB_EXT_ADDRESS_SRC, SADB_EXT_SA)) {
		PRTDBG(D_PFKEY, ("  Extracting SADB extensions failed."));
		ssh_free(samsg);
		return;
	}

	assoc = (sadb_sa_t *)pmsg.pmsg_exts[SADB_EXT_SA];
	pair = (struct sadb_x_pair *)pmsg.pmsg_exts[SADB_X_EXT_PAIR];
	srcid = (sadb_ident_t *)pmsg.pmsg_exts[SADB_EXT_IDENTITY_SRC];
	dstid = (sadb_ident_t *)pmsg.pmsg_exts[SADB_EXT_IDENTITY_DST];
	src = pmsg.pmsg_sss;
	dst = pmsg.pmsg_dss;
	cookie = (sadb_x_kmc_t *)pmsg.pmsg_exts[SADB_X_EXT_KM_COOKIE];
	if (cookie != NULL && cookie->sadb_x_kmc_cookie == 0)
		cookie = NULL;

	delete_p2sa(dst, src, srcid, dstid, cookie, assoc->sadb_sa_spi,
	    samsg->sadb_msg_satype);

	/*
	 * If the DELETE message contains a pair extension delete the pair SA.
	 */
	if (pair != NULL) {
		delete_p2sa(src, dst, dstid, srcid, cookie,
		    pair->sadb_x_pair_spi, samsg->sadb_msg_satype);
	}

	ssh_free(samsg);
}



/*
 * Send DELETE notification if this is an SA with ME as a destination.
 * get_server_context() will return NULL if the destination address
 * is not the IP address of this system.
 */
static void
delete_p2sa(struct sockaddr_storage *dst, struct sockaddr_storage *src,
    sadb_ident_t *srcid, sadb_ident_t *dstid, sadb_x_kmc_t *cookie,
    uint32_t spi, uint8_t sadb_msg_satype)
{
	phase1_t *p1;
	ike_server_t *ikesrv;
	int rc;
	SshIkeNegotiation neg;
	uchar_t remote_name_buf[INET6_ADDRSTRLEN + IFNAMSIZ + 2];
	uchar_t remote_port_buf[INET6_ADDRSTRLEN + 1];
	uchar_t *remote_name, *remote_port;
	uint8_t *spiptr;

	spiptr = (uint8_t *)&spi;

	/*
	 * I don't want to try this if get_server_context() fails.
	 * I do want to try it if phase1 fails.  So we need a context, but
	 * not a phase1_t.
	 */
	ikesrv = get_server_context(dst);
	if (ikesrv == NULL) {
		PRTDBG(D_PFKEY,
		    ("  Delete notification not sent - Outgoing SA."));
		PRTDBG(D_PFKEY,
		    ("  Delete notification not sent: %s, SPI: 0x%x", sap(dst),
		    ntohl(spi)));
		return;
	}

	/*
	 * Okay, find a phase 1 SA that we can use.  Reverse src and dest,
	 * because we want to notify the remote side about one of our INBOUND
	 * SAs losing it.
	 *
	 * TODO Should we match larval p1 SAs?  Boolean param to match_phase1
	 * determines this...behavior before adding the param was that larvals
	 * were NOT returned.
	 *
	 * Use the phase1_t's negotiation to ssh_ike_connect_delete().
	 */

	p1 = match_phase1(dst, src, dstid, srcid, cookie, B_FALSE);
	if (p1 == NULL) {
		PRTDBG(D_PFKEY, ("  No Phase 1 for: %s, SPI: 0x%x", sap(dst),
		    ntohl(spi)));
		neg = NULL;
		remote_name = remote_name_buf;
		remote_port = remote_port_buf;

		/* TODO: If we allow other than 500, fix this! */
		(void) ssh_snprintf((char *)remote_port,
		    sizeof (remote_port_buf), "%d", IPPORT_IKE);
		if (!sockaddr_to_string(src, remote_name)) {
			PRTDBG(D_PFKEY, ("  sockaddr_to_string() failed."));
			return;
		}
	} else {
		neg = p1->p1_negotiation;
		PRTDBG(D_PFKEY, ("  Found Phase_1 for destination %s, "
		    "SPI: 0x%x", sap(dst), ntohl(spi)));
		remote_name = NULL;
		remote_port = NULL;
	}

	/*
	 * This function attempts to send a delete notification to peer.
	 */

	rc = ssh_ike_connect_delete(ikesrv->ikesrv_ctx, neg, remote_name,
	    remote_port, SSH_IKE_DELETE_FLAGS_WANT_ISAKMP_SA,
	    SSH_IKE_DOI_IPSEC, sadb_msg_satype, 1, &spiptr, sizeof (uint32_t));

	if (rc != SSH_IKE_ERROR_OK) {
		/*
		 * Must not have found a phase 1 SA, in spite of p1 being
		 * there.
		 *
		 * ??? Nuke the phase1_t?
		 */

		/* Silently handle the case where I don't have a phase1_t. */
		if (neg == NULL) {
			PRTDBG(D_PFKEY,
			    ("  Normally silent error %d (%s).",
			    rc, ike_connect_error_to_string(rc)));
			PRTDBG(D_PFKEY,
			    ("  Delete notification not sent (no P1)"));
			PRTDBG(D_PFKEY,
			    ("  SPI: 0x%x (no P1): %s", ntohl(spi), sap(dst)));
			return;
		}

		PRTDBG(D_PFKEY,
		    ("  Delete notification not sent: library delete function "
		    "failed with error %d (%s).", rc,
		    ike_connect_error_to_string(rc)));
		PRTDBG(D_PFKEY, ("  SPI: 0x%x: %s", ntohl(spi), sap(dst)));
	} else {
		/*
		 * deletion was successful!  increment the counter in
		 * the phase1_t, if we found one.
		 */
		PRTDBG(D_PFKEY,
		    ("  Delete notification sent: %s, SPI: 0x%x", sap(dst),
		    ntohl(spi)));
		if (p1 != NULL)
			p1->p1_stats.p1stat_del_qm_sas++;
	}
}

/*
 * Handle PF_KEY EXPIRE message. This can be SOFT/HARD/IDLE expire event.
 * For SOFT/HARD this is handled as ACQUIRE or DELETE, respectively;
 * for IDLE this will kick off DPD handshake.
 */
static void
handle_expire(sadb_msg_t *samsg)
{
	parsedmsg_t pmsg;
	uint64_t *buf, *curp;
	uint_t protocol, iprotocol, ispfx, idpfx;
	sadb_msg_t *nsamsg;
	sadb_address_t *address;
	struct sockaddr_storage *socaddr, src, dst;
	sadb_lifetime_t *c_lifetime, *s_lifetime;
	sadb_sa_t *sadb;
	sadb_x_ecomb_t *ecomb;
	sadb_x_propbase_t *prop;
	sadb_x_algdesc_t *algdesc;
	sadb_address_t *local, *remote, *ilocal, *iremote;
	boolean_t tunnel_mode;
	time_t wallclock;
	static const p2alg_t *alg_strength;
	uint32_t spi;
	uint32_t sadb_msg_satype;
	boolean_t inbound_sa = B_FALSE;
	struct sockaddr_storage *tmp_addr;
	char byte_str[BYTE_STR_SIZE]; /* byte lifetime string representation */
	char secs_str[SECS_STR_SIZE]; /* lifetime string representation */

	nsamsg = NULL;

	/*
	 * If SOFT expire, see if the SADB_X_SAFLAGS_KM1 (initiator) is set,
	 * if so, consider treating this expire as an ACQUIRE message.
	 *
	 * If HARD expire, treat this message like a DELETE.
	 */

	if (extract_exts(samsg, &pmsg, 1, SADB_EXT_LIFETIME_HARD)) {
		PRTDBG(D_PFKEY, ("Handling SADB expire message..."));
		handle_delete(samsg);
		return;
	}

	if (pmsg.pmsg_exts[SADB_X_EXT_LIFETIME_IDLE] != NULL) {
		handle_idle_timeout(samsg);
		return;
	}

	/* Handle SOFT expires from now on. */

	/*
	 * extract_exts() has already filled in pmsg with data from
	 * samsg. pmsg.pmsg_exts[foo] will be NULL if this was
	 * not set in samsg. Bail out if the message appears to be
	 * poorly formed. If everything looks good, create a new
	 * "ACQUIRE like" message and pass off to handle_acquire().
	 */

	if (pmsg.pmsg_exts[SADB_EXT_LIFETIME_SOFT] == NULL) {
		PRTDBG(D_PFKEY, ("SADB EXPIRE message is missing both"
		    " hard and soft lifetimes."));
		goto rekey_failed;
	}

	PRTDBG(D_PFKEY, ("Handling SADB soft expire message..."));
	if (pmsg.pmsg_exts[SADB_EXT_ADDRESS_SRC] == NULL ||
	    pmsg.pmsg_exts[SADB_EXT_ADDRESS_DST] == NULL) {
		PRTDBG(D_PFKEY, ("Unable to find addresses in samsg."));
		goto rekey_failed;
	}
	if (pmsg.pmsg_exts[SADB_X_EXT_ADDRESS_INNER_SRC] != NULL) {
		tunnel_mode = B_TRUE;
		if (pmsg.pmsg_exts[SADB_X_EXT_ADDRESS_INNER_DST] == NULL) {
			PRTDBG(D_PFKEY,
			    ("Unable to find tunnel-destination in samsg."));
			goto rekey_failed;
		}
		address = (sadb_address_t *)
		    pmsg.pmsg_exts[SADB_X_EXT_ADDRESS_INNER_SRC];
		iprotocol = address->sadb_address_proto;
		ispfx = address->sadb_address_prefixlen;
		address = (sadb_address_t *)
		    pmsg.pmsg_exts[SADB_X_EXT_ADDRESS_INNER_DST];
		idpfx = address->sadb_address_prefixlen;
	} else {
		tunnel_mode = B_FALSE;
	}

	sadb = (struct sadb_sa *)pmsg.pmsg_exts[SADB_EXT_SA];
	if (sadb == NULL) {
		PRTDBG(D_PFKEY, ("  No message extensions"));
		goto rekey_failed;
	}

	/*
	 * Check to see if the expiring SA has ever been used. There
	 * is no point in renewing an unused SA ..
	 */
	if (!(sadb->sadb_sa_flags & SADB_X_SAFLAGS_USED)) {
		PRTDBG(D_PFKEY, ("  Expiring SA (SPI: 0x%x) was never used.",
		    ntohl(sadb->sadb_sa_spi)));
		goto rekey_failed;
	}

	if (pmsg.pmsg_exts[SADB_EXT_LIFETIME_CURRENT] == NULL) {
		PRTDBG(D_PFKEY, ("  No lifetime extension"));
		goto rekey_failed;
	}

	/* copy these for later. */
	spi = sadb->sadb_sa_spi;
	sadb_msg_satype = samsg->sadb_msg_satype;
	(void) memcpy(&dst, pmsg.pmsg_dss, sizeof (struct sockaddr_storage));
	address = (struct sadb_address *)pmsg.pmsg_exts[SADB_EXT_ADDRESS_SRC];
	protocol = address->sadb_address_proto;
	(void) memcpy(&src, pmsg.pmsg_sss, sizeof (struct sockaddr_storage));

	/* Check if the SA is inbound and act appropriately. */
	if (!get_server_context(&src)) {
		PRTDBG(D_PFKEY, ("  Inbound SA - swapping src and dst"));
		inbound_sa = B_TRUE;
		/* Swap src and dst so correct ACQUIRE is constructed below. */
		tmp_addr = pmsg.pmsg_sss;
		pmsg.pmsg_sss = pmsg.pmsg_dss;
		pmsg.pmsg_dss = tmp_addr;
		if (tunnel_mode) {
			uint_t tmp_pfx;
			/* swap inner src and dst addresses */
			tmp_addr = pmsg.pmsg_psss;
			pmsg.pmsg_psss = pmsg.pmsg_pdss;
			pmsg.pmsg_pdss = tmp_addr;
			/* swap prefix lengths */
			tmp_pfx = ispfx;
			ispfx = idpfx;
			idpfx = tmp_pfx;
		}
	}

	/*
	 * The kernel sadb_ager() function runs approximately every eight
	 * seconds by default, the exact interval is self tuning. If the
	 * current lifetime is within eight seconds of the soft lifetime
	 * then assume this was a time based SOFT expire.
	 */

	wallclock = ssh_time();

	c_lifetime = (struct sadb_lifetime *)
	    pmsg.pmsg_exts[SADB_EXT_LIFETIME_CURRENT];
	s_lifetime = (struct sadb_lifetime *)
	    pmsg.pmsg_exts[SADB_EXT_LIFETIME_SOFT];

	/*
	 * There is no need to rekey for time expiring inbound SAs since its
	 * paired (outbound) SA will SOFT-expire at roughly the same time.
	 *
	 * It is important to rekey for both inbound and outbound kilobyte
	 * SOFT expires otherwise there might be a traffic black hole.
	 */
	if ((wallclock - (c_lifetime->sadb_lifetime_addtime +
	    s_lifetime->sadb_lifetime_addtime)) < 8) {
		PRTDBG(D_PFKEY, ("  SPI: 0x%x expired", ntohl(spi)));
		PRTDBG(D_PFKEY, ("    SOFT lifetime exceeded: %" PRIu64
		    " seconds%s", s_lifetime->sadb_lifetime_addtime,
		    secs2out(s_lifetime->sadb_lifetime_addtime, secs_str,
		    sizeof (secs_str), SPC_BEGIN)));

		if (inbound_sa) {
			PRTDBG(D_PFKEY, ("  Time expiring SA (SPI: 0x%x) was "
			    "inbound - ignored.", ntohl(spi)));
			goto rekey_failed;
		}
	}

	if (s_lifetime->sadb_lifetime_bytes != 0 &&
	    (c_lifetime->sadb_lifetime_bytes >
	    s_lifetime->sadb_lifetime_bytes)) {
		PRTDBG(D_PFKEY, ("  SPI: 0x%" PRIx32 " expired", ntohl(spi)));
		PRTDBG(D_PFKEY, ("    SOFT lifetime exceeded: %" PRIu64
		    " bytes%s", s_lifetime->sadb_lifetime_bytes,
		    bytecnt2out(s_lifetime->sadb_lifetime_bytes, byte_str,
		    sizeof (byte_str), SPC_BEGIN)));
	}

	/*
	 * Allocate a new buffer for the ACQUIRE message that is
	 * going to be created.
	 *
	 * New ACQUIRE message will look like this:
	 *
	 * SA Message header (sadb_msg_t)
	 * Address header (sadb_msg_t)
	 * Source address (sockaddr_in6 )
	 * Address header (sadb_msg_t)
	 * Destination address (sockaddr_in6)
	 * (tunnel-mode) Address header (sadb_msg_t)
	 * (tunnel-mode) Inner-Source address (sockaddr_in6)
	 * (tunnel-mode) Address header (sadb_msg_t)
	 * (tunnel-mode) Inner-Destination address (sockaddr_in6)
	 * Proposal (sadb_x_propbase_t)
	 * Extended Proposal (sadb_x_ecomb_t)
	 *
	 * Either or both of these:
	 *
	 * Authentication algorithm description (sadb_x_algdesc_t)
	 * Encryption algorithm description (sadb_x_algdesc_t)
	 */

	buf = ssh_malloc(SAMSG_BUFF_LEN);
	if (buf == NULL) {
		PRTDBG(D_PFKEY, ("  No memory"));
		goto rekey_failed;
	}

	curp = buf;

#define	ALLOCP2(x, t1, t2) x = (t1 *)curp; curp += SADB_8TO64(sizeof (t2));
#define	ALLOCP(x, t) ALLOCP2(x, t, t)

	ALLOCP(nsamsg, sadb_msg_t);
	nsamsg->sadb_msg_version = PF_KEY_V2;
	nsamsg->sadb_msg_type = SADB_ACQUIRE;
	nsamsg->sadb_msg_errno = 0;
	nsamsg->sadb_msg_satype = samsg->sadb_msg_satype;
	nsamsg->sadb_msg_reserved = 0;
	nsamsg->sadb_msg_len = SADB_8TO64(sizeof (*nsamsg));
	nsamsg->sadb_msg_seq = 0;
	nsamsg->sadb_msg_pid = 0;

	/*
	 * Construct addresses...
	 */

	ALLOCP(local, sadb_address_t);
	local->sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
	local->sadb_address_len = SADB_8TO64(sizeof (*local) +
	    sizeof (struct sockaddr_in6));
	local->sadb_address_proto = protocol;
	local->sadb_address_prefixlen = 0;
	local->sadb_address_reserved = 0;

	nsamsg->sadb_msg_len += local->sadb_address_len;

	ALLOCP2(socaddr, struct sockaddr_storage, struct sockaddr_in6);
	(void) memcpy(socaddr, pmsg.pmsg_sss, sizeof (struct sockaddr_in6));

	ALLOCP(remote, sadb_address_t);
	remote->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	remote->sadb_address_len = SADB_8TO64(sizeof (*remote) +
	    sizeof (struct sockaddr_in6));
	remote->sadb_address_proto = protocol;
	remote->sadb_address_prefixlen = 0;
	remote->sadb_address_reserved = 0;

	nsamsg->sadb_msg_len += remote->sadb_address_len;

	ALLOCP2(socaddr, struct sockaddr_storage, struct sockaddr_in6);
	(void) memcpy(socaddr, pmsg.pmsg_dss, sizeof (struct sockaddr_in6));

	if (tunnel_mode) {
		ALLOCP(ilocal, sadb_address_t);
		ilocal->sadb_address_exttype = SADB_X_EXT_ADDRESS_INNER_SRC;
		ilocal->sadb_address_len = SADB_8TO64(sizeof (*ilocal) +
		    sizeof (struct sockaddr_in6));
		ilocal->sadb_address_proto = iprotocol;
		ilocal->sadb_address_prefixlen = ispfx;
		ilocal->sadb_address_reserved = 0;
		nsamsg->sadb_msg_len += ilocal->sadb_address_len;
		ALLOCP2(socaddr, struct sockaddr_storage, struct sockaddr_in6);
		(void) memcpy(socaddr, pmsg.pmsg_psss,
		    sizeof (struct sockaddr_in6));

		ALLOCP(iremote, sadb_address_t);
		iremote->sadb_address_exttype = SADB_X_EXT_ADDRESS_INNER_DST;
		iremote->sadb_address_len = SADB_8TO64(sizeof (*iremote) +
		    sizeof (struct sockaddr_in6));
		iremote->sadb_address_proto = iprotocol;
		iremote->sadb_address_prefixlen = idpfx;
		iremote->sadb_address_reserved = 0;
		nsamsg->sadb_msg_len += iremote->sadb_address_len;
		ALLOCP2(socaddr, struct sockaddr_storage, struct sockaddr_in6);
		(void) memcpy(socaddr, pmsg.pmsg_pdss,
		    sizeof (struct sockaddr_in6));
	}

	/*
	 * Create Extended proposals
	 */

	ALLOCP(prop, sadb_x_propbase_t);
	prop->sadb_x_propb_exttype = SADB_X_EXT_EPROP;
	prop->sadb_x_propb_len = SADB_8TO64(sizeof (*prop));
	prop->sadb_x_propb_len += SADB_8TO64(sizeof (*ecomb));
	prop->sadb_x_propb_numecombs = 1;
	prop->sadb_x_propb_replay = sadb->sadb_sa_replay;

	ALLOCP(ecomb, sadb_x_ecomb_t);
	ecomb->sadb_x_ecomb_numalgs = 0;
	ecomb->sadb_x_ecomb_reserved = 0;
	ecomb->sadb_x_ecomb_flags = 0;
	ecomb->sadb_x_ecomb_flags = sadb->sadb_sa_flags;

	ecomb->sadb_x_ecomb_reserved2 = 0;
	ecomb->sadb_x_ecomb_soft_allocations = 0;
	ecomb->sadb_x_ecomb_hard_allocations = 0;
	ecomb->sadb_x_ecomb_soft_bytes = 0;
	ecomb->sadb_x_ecomb_soft_addtime = 0;
	/*
	 * Policy manager will reconfigure the SA lifetime in the extended
	 * proposals sent to the peer. The life value for time needs to be
	 * initialized with a non zero value in order for handle_algdesc() to
	 * register any lifetime types with libike.  This will ensure the
	 * extended proposal contains at least a lifetime in seconds.
	 */
	ecomb->sadb_x_ecomb_hard_addtime =
	    (ike_defs.rule_p2_lifetime_secs != 0) ?
	    ike_defs.rule_p2_lifetime_secs : DEF_P2_LIFETIME_HARD;

	ecomb->sadb_x_ecomb_hard_bytes = 0;
	ecomb->sadb_x_ecomb_soft_usetime = 0;
	ecomb->sadb_x_ecomb_hard_usetime = 0;

	if (sadb->sadb_sa_auth) {
		PRTDBG(D_PFKEY, ("  Have Authentication Alg"));
		ecomb->sadb_x_ecomb_numalgs++;
		prop->sadb_x_propb_len +=
		    SADB_8TO64(sizeof (struct sadb_x_algdesc));

		ALLOCP(algdesc, sadb_x_algdesc_t);
		algdesc->sadb_x_algdesc_satype = samsg->sadb_msg_satype;
		algdesc->sadb_x_algdesc_algtype = SADB_X_ALGTYPE_AUTH;
		algdesc->sadb_x_algdesc_alg =  sadb->sadb_sa_auth;
		if ((alg_strength =
		    find_esp_auth_alg(algdesc->sadb_x_algdesc_alg)) != NULL) {
			algdesc->sadb_x_algdesc_minbits =
			    alg_strength->p2alg_min_bits;
			algdesc->sadb_x_algdesc_maxbits =
			    alg_strength->p2alg_max_bits;
		} else {
			algdesc->sadb_x_algdesc_minbits = 0;
			algdesc->sadb_x_algdesc_maxbits = 0;
		}
	}
	if (sadb->sadb_sa_encrypt) {
		PRTDBG(D_PFKEY, ("  Have Encryption Alg"));
		ecomb->sadb_x_ecomb_numalgs++;
		prop->sadb_x_propb_len +=
		    SADB_8TO64(sizeof (struct sadb_x_algdesc));

		ALLOCP(algdesc, sadb_x_algdesc_t);
		algdesc->sadb_x_algdesc_satype = samsg->sadb_msg_satype;
		algdesc->sadb_x_algdesc_algtype = SADB_X_ALGTYPE_CRYPT;
		algdesc->sadb_x_algdesc_alg = sadb->sadb_sa_encrypt;
		/*
		 * in.iked already knows the kernels algorithm capabilities
		 * this was passed up when in.iked sent SADB_REGISTER.
		 */
		if ((alg_strength =
		    find_esp_encr_alg(algdesc->sadb_x_algdesc_alg)) != NULL) {
			algdesc->sadb_x_algdesc_minbits =
			    (uint16_t)alg_strength->p2alg_min_bits;
			algdesc->sadb_x_algdesc_maxbits =
			    (uint16_t)alg_strength->p2alg_max_bits;
			algdesc->sadb_x_algdesc_reserved =
			    (uint8_t)alg_strength->p2alg_salt_bits;
		} else {
			algdesc->sadb_x_algdesc_minbits = 0;
			algdesc->sadb_x_algdesc_maxbits = 0;
			algdesc->sadb_x_algdesc_reserved = 0;
		}
	}
	if (ecomb->sadb_x_ecomb_numalgs < 1) {
		PRTDBG(D_PFKEY, ("  No algorithms defined"));
		goto rekey_failed;
	}
	nsamsg->sadb_msg_len += prop->sadb_x_propb_len;

#undef ALLOCP
#undef ALLOCP2

	PRTDBG(D_PFKEY, ("  Attempting to negotiate new phase 2"));
	/*
	 * ssh_free() before calling handle_acquire(), this could
	 * trigger a sequence of events that renders *samsg invalid.
	 * Update the HARD lifetime of the expiring SA after handle_acquire()
	 * caused its replacement to be created.
	 */
	ssh_free(samsg);
	handle_acquire(nsamsg, B_TRUE);
	update_assoc_lifetime(spi, 0, 0, 8, 0, &dst, sadb_msg_satype);
	return;

rekey_failed:
	ssh_free(samsg);
	if (nsamsg != NULL)
		ssh_free(nsamsg);
}

/*
 * The guts of handle_flush()...extracted from the pfkey context so it
 * can be used elsewhere.
 */
void
flush_cache_and_p1s()
{
	extern phase1_t *phase1_head;

	/*
	 * Nuke all addrcache entries!
	 *
	 * In an even more perfect world, you'd send DELETE notifications
	 * for every SA, too.
	 */

	flush_addrcache();

	/*
	 * Find all of the phase 1 SAs and nuke them.
	 *
	 * Warning - There may be a race condition here w.r.t. callbacks
	 * being invoked during the loop.
	 */

	while (phase1_head != NULL) {
		delete_phase1(phase1_head, B_TRUE);
		/* Implicit unlink in delete_phase1() means loop will halt. */
	}
}

static void
handle_flush(sadb_msg_t *samsg)
{
	extern SshIkeContext ike_context;

	PRTDBG(D_PFKEY, ("Handling SADB flush message..."));

	/* Return if just AH or ESP SAs are being freed. */
	if (samsg->sadb_msg_satype != SADB_SATYPE_UNSPEC)
		return;

	flush_cache_and_p1s();

	/*
	 * Drop the big one on the SSH library.  It'll nuke all of the phase
	 * 1 SAs that haven't been nuked already.  Given we have unlinked all
	 * of the phase1_ts, we won't have to worry about new ACQUIREs picking
	 * one of the in-progress Phase 1 SAs.
	 */
	(void) ssh_ike_remove_isakmp_sa_by_address(ike_context, NULL, NULL,
	    NULL, NULL, SSH_IKE_REMOVE_FLAGS_SEND_DELETE |
	    SSH_IKE_REMOVE_FLAGS_FORCE_DELETE_NOW);

	ssh_free(samsg);
}


/*
 * Check ecomb from ACQUIRE for supported algorithms.  Unless
 * everything in this alternative is supported, return FALSE.
 */
static boolean_t
ecomb_check(int nalg, sadb_x_algdesc_t *ada)
{
	boolean_t nontrivial = B_FALSE;
	int j;

	for (j = 0; j < nalg; j++) {
		sadb_x_algdesc_t *ad = &ada[j];
		int satype = ad->sadb_x_algdesc_satype;
		int algtype = ad->sadb_x_algdesc_algtype;
		int alg = ad->sadb_x_algdesc_alg;
		char algname[80];

		switch (satype) {
		case SADB_SATYPE_ESP:
			switch (algtype) {
			case SADB_X_ALGTYPE_AUTH:
				if (!find_esp_auth_alg(alg)) {
					PRTDBG(D_PFKEY,
					    ("Unsupported ESP auth alg %d (%s)",
					    alg, kef_alg_to_string(alg,
					    IPSEC_PROTO_AH, algname)));
					return (B_FALSE);
				}
				break;
			case SADB_X_ALGTYPE_CRYPT:
				if (!find_esp_encr_alg(alg)) {
					PRTDBG(D_PFKEY,
					    ("Unsupported ESP encr alg %d (%s)",
					    alg, kef_alg_to_string(alg,
					    IPSEC_PROTO_ESP, algname)));
					return (B_FALSE);
				}
				break;
			default:
				PRTDBG(D_PFKEY, ("Unsupported ESP algtype %d "
				    "(%s)", algtype, alg_to_string(algtype,
				    algtype_table)));
				return (B_TRUE);
			}
			nontrivial = B_TRUE;
			break;
		case SADB_SATYPE_AH:
			if (ad->sadb_x_algdesc_algtype !=
			    SADB_X_ALGTYPE_AUTH) {
				PRTDBG(D_PFKEY, ("Unsupported AH algtype %d "
				    "(%s)", algtype, alg_to_string(algtype,
				    algtype_table)));
				return (B_FALSE);
			}
			if (!find_auth_alg(ad->sadb_x_algdesc_alg)) {
				PRTDBG(D_PFKEY,
				    ("Unsupported AH auth alg %d (%s)", alg,
				    kef_alg_to_string(alg, IPSEC_PROTO_AH,
				    algname)));
				return (B_FALSE);
			}

			nontrivial = B_TRUE;
			break;
		default:
			PRTDBG(D_PFKEY, ("Unsupported SA type %d (%s), "
			    "not AH or ESP.", satype, pfkey_satype(satype)));
			return (B_FALSE);
		}
	}
	if (!nontrivial)
		PRTDBG(D_PFKEY, ("Empty extended proposal rejected!"));

	return (nontrivial);
}

/*
 * Sanity check an extended proposal by scanning all the extended combinations
 * Fail if no good ecombs found.
 */
static boolean_t
eprop_check(sadb_prop_t *prop)
{
	sadb_x_ecomb_t *ecomb = (sadb_x_ecomb_t *)(prop + 1);
	sadb_x_ecomb_t *next_ecomb;
	sadb_x_algdesc_t *algdesc;
	int good_ecombs = 0;
	int i, nc;

	nc = prop->sadb_x_prop_numecombs;
	if (nc <= 0)
		return (B_FALSE);

	for (i = 0; i < nc; i++, ecomb = next_ecomb) {
		int nalg = ecomb->sadb_x_ecomb_numalgs;
		algdesc = (sadb_x_algdesc_t *)(ecomb + 1);
		next_ecomb = (sadb_x_ecomb_t *)(algdesc + nalg);

		if (nalg <= 0)
			continue;

		if (ecomb_check(nalg, algdesc))
			good_ecombs++;
	}

	if (good_ecombs > 0)
		return (B_TRUE);

	PRTDBG(D_PFKEY, ("Extended Proposal Check: no valid proposals found "
	    "in ACQUIRE"));
	return (B_FALSE);
}

/*
 * Sanity-check proposal against known-supported algorithms.
 */
static boolean_t
alg_check(parsedmsg_t *pmsg)
{
	sadb_prop_t *prop;

	prop = (sadb_prop_t *)pmsg->pmsg_exts[SADB_X_EXT_EPROP];
	if (prop != NULL)
		return (eprop_check(prop));

	PRTDBG(D_PFKEY, ("Algorithm check: no extended proposals found "
	    "in ACQUIRE"));
	/*
	 * Note: we do not support traditional SADB_EXT_PROPOSAL-style
	 * acquires here as they are also not supported further downstream
	 * in initiator.c.  Might as well just head it off at the pass..
	 */
	return (B_FALSE);
}



/*
 * Handle a PF_KEY ACQUIRE message.  This function, or something that it
 * calls (either directly or via callbacks) must free samsg.
 */
static void
handle_acquire(sadb_msg_t *samsg, boolean_t create_phase2)
{
	phase1_t *p1;
	parsedmsg_t *pmsg;

	if (samsg->sadb_msg_errno != 0) {
		EXIT_FATAL2("Errno in ACQUIRE: %s",
		    strerror(samsg->sadb_msg_errno));
	}

	if (samsg->sadb_msg_satype != SADB_SATYPE_ESP &&
	    samsg->sadb_msg_satype != SADB_SATYPE_AH &&
	    samsg->sadb_msg_satype != SADB_SATYPE_UNSPEC) {
		/*
		 * Drop on floor; clearly not intended for us.
		 */
		if (samsg->sadb_msg_errno != 0)
			PRTDBG(D_PFKEY, ("Non AH/ESP/extended ACQUIRE."));
		ssh_free(samsg);
		return;
	}

	pmsg = ssh_malloc(sizeof (parsedmsg_t));
	if (pmsg == NULL) {
		send_early_negative_acquire(samsg, ENOMEM);
		ssh_free(samsg);
		return;
	}

	if (!extract_exts(samsg, pmsg, 2, SADB_EXT_ADDRESS_SRC,
	    SADB_EXT_ADDRESS_DST)) {
		/* extract_exts() prints a message. */
		send_negative_acquire(pmsg, EINVAL);
		free_pmsg(pmsg);
		return;
	}

	/* Check if inner addrs exist (already extracted above) */
	if (pmsg->pmsg_exts[SADB_X_EXT_ADDRESS_INNER_SRC] == NULL &&
	    pmsg->pmsg_exts[SADB_X_EXT_ADDRESS_INNER_DST] == NULL) {
		PRTDBG(D_PFKEY, ("No inner addresses present"));
	} else {
		PRTDBG(D_PFKEY, ("Inner addresses present"));
	}

	/*
	 * Sanity-check proposal against known-supported algorithms.
	 */
	if (!alg_check(pmsg)) {
		send_negative_acquire(pmsg, EINVAL);
		free_pmsg(pmsg);
		return;
	}

	if (pmsg->pmsg_exts[SADB_EXT_SENSITIVITY] != NULL) {
		PRTDBG(D_PFKEY, ("Sensitive ACQUIRE"));
	}

	/*
	 * Now that I've fully parsed the ACQUIRE, set IKE in motion.
	 *
	 * 1.)	Lookup policy for this ACQUIRE, see if you want aggressive or
	 *	main.
	 * 2.)	See if you already have a matching ISAKMP Phase 1 SA to go
	 *	with it.  (E.g. If you're replacing expired SAs, find the
	 *	existing Phase 1 SA and do another Quick Mode.)  If one
	 *	doesn't exist, create one using ssh_ike_connect().  If
	 *	ssh_ike_connect() gets called, bail, and prep for callback.
	 * 3.)	With a Phase 1 SA, call ssh_ike_connect_ipsec(), bail, and
	 *	prep for callback.
	 */

	PRTDBG(D_PFKEY, ("Doing ACQUIRE...."));

	p1 = get_phase1(pmsg, create_phase2);
	if (p1 == NULL) {
		/*
		 * Failure.  Negative ACQUIRE was sent already.
		 * (See free_phase1(), which get_phase1() calls upon
		 * failure.)
		 * Also, get_phase1 eats the pmsg.
		 */
		PRTDBG(D_PFKEY, ("  No Phase 1."));
		return;
	}

	if (p1 == (phase1_t *)1) {
		PRTDBG(D_PFKEY, ("  Waiting for IKE results."));
		return;
	}

	if (p1->p1_pmsg == NULL) {
		/*
		 * Phase 1 that's already had its initial ACQUIRE handled,
		 * or a receiver-created phase1_t.  Just plug in our new
		 * samsg here and run with it!
		 */
		assert(p1->p1_pmsg_tail == NULL);
		p1->p1_pmsg = pmsg;
		p1->p1_pmsg_tail = pmsg;
	} else if (p1->p1_pmsg != pmsg) {
		/*
		 * Phase 1 with an in-progress ACQUIRE-driven negotiation.
		 */
		PRTDBG(D_PFKEY, ("  ACQUIRE is already in-progress."));

		assert(p1->p1_pmsg_tail->pmsg_next == NULL);
		p1->p1_pmsg_tail->pmsg_next = pmsg;
		p1->p1_pmsg_tail = pmsg;

		/*
		 * Return, since phase1_notify will be called eventually
		 * anyway.  BTW, if this gets MT-hot, we'll have to
		 * hold down a phase1_t lock.
		 */
		return;
	}

	/*
	 * Else we have one that's good to go!
	 * Call some other function with a fully-functional SshIkeNegotiation.
	 */
	PRTDBG(D_PFKEY, ("  ACQUIRE succeeded!"));
	DUMP_PFKEY(samsg);

	phase1_notify(SSH_IKE_NOTIFY_MESSAGE_CONNECTED, p1->p1_negotiation,
	    p1);
}



static size_t
payloadid_size(SshIkePayloadID p)
{
	size_t len;

	if (p == NULL)
		return (0);

	len = roundup(p->identification_len + 1, 8);

	/*
	 * Keep switch body synchronized with payloadid_to_pfreq
	 */
	switch (p->id_type) {
	case IPSEC_ID_FQDN:
	case IPSEC_ID_USER_FQDN:
	case IPSEC_ID_DER_ASN1_DN:
	case IPSEC_ID_DER_ASN1_GN:
	case IPSEC_ID_KEY_ID:
	case IPSEC_ID_IPV4_ADDR_SUBNET:
	case IPSEC_ID_IPV4_ADDR_RANGE:
	case IPSEC_ID_IPV6_ADDR_RANGE:
	case IPSEC_ID_IPV6_ADDR_SUBNET:
		return (len);
	default:
		return (0);
	}
}

static void
payloadid_to_pfreq(SshIkePayloadID p, boolean_t srcid, sadb_ident_t *storage)
{
	sadb_ident_t *ident = storage;
	size_t len = sizeof (*ident) + roundup(p->identification_len + 1, 8);
	void *idptr;
	uint16_t type;
	uint8_t *dst;

	/*
	 * Keep switch body synchronized with payloadid_size
	 */
	switch (p->id_type) {
	case IPSEC_ID_FQDN:
		type = SADB_IDENTTYPE_FQDN;
		idptr = (void *)p->identification.fqdn;
		break;
	case IPSEC_ID_USER_FQDN:
		type = SADB_IDENTTYPE_USER_FQDN;
		idptr = (void *)p->identification.user_fqdn;
		break;
	case IPSEC_ID_DER_ASN1_DN:
		type = SADB_X_IDENTTYPE_DN;
		idptr = (void *)p->identification.asn1_data;
		break;
	case IPSEC_ID_DER_ASN1_GN:
		type = SADB_X_IDENTTYPE_GN;
		idptr = (void *)p->identification.asn1_data;
		break;
	case IPSEC_ID_KEY_ID:
		type = SADB_X_IDENTTYPE_KEY_ID;
		idptr = (void *)p->identification.key_id;
		break;
	case IPSEC_ID_IPV4_ADDR_SUBNET:
		type = SADB_IDENTTYPE_PREFIX;
		idptr = &p->identification.ipv4_addr_subnet;
		break;
	case IPSEC_ID_IPV4_ADDR_RANGE:
		type = SADB_X_IDENTTYPE_ADDR_RANGE;
		idptr = &p->identification.ipv4_addr_range1;
		break;
	case IPSEC_ID_IPV6_ADDR_RANGE:
		type = SADB_X_IDENTTYPE_ADDR_RANGE;
		idptr = &p->identification.ipv6_addr_range1;
		break;
	case IPSEC_ID_IPV6_ADDR_SUBNET:
		type = SADB_IDENTTYPE_PREFIX;
		idptr = &p->identification.ipv6_addr_subnet;
		break;
	default:
		/* Default includes IPv4 and IPv6 addresses... */
		return;
	}

	ident->sadb_ident_exttype = (srcid ?
	    SADB_EXT_IDENTITY_SRC : SADB_EXT_IDENTITY_DST);
	ident->sadb_ident_len = SADB_8TO64(len);
	ident->sadb_ident_type = type;
	dst = (uint8_t *)(ident + 1);
	(void) memcpy(dst, idptr, p->identification_len);
	/* Just in case, null-terminate. */
	dst[p->identification_len] = '\0';
}

/*
 * Convert an SSH IKE identity payload into a PF_KEY identity, if applicable.
 * Return NULL if you don't need to, or run out of memory.
 */
sadb_ident_t *
payloadid_to_pfkey(SshIkePayloadID payload, boolean_t srcid)
{
	sadb_ident_t *ident;
	size_t len = payloadid_size(payload);

	if (len == 0)
		return (NULL);

	len += sizeof (*ident);

	ident = ssh_calloc(1, len);
	if (ident == NULL)
		return (ident);

	payloadid_to_pfreq(payload, srcid, ident);

	return (ident);
}



/*
 * Convert a PF_KEY identity extension into an SSH IKE identity payload, if
 * applicable.  Return NULL if you don't need to, or run out of memory.
 */
SshIkePayloadID
pfkeyid_to_payload(sadb_ident_t *ident, int af)
{
	uint8_t *id_data = (uint8_t *)(ident + 1);
	SshIkePayloadID rc;
	int datalen = SADB_64TO8(ident->sadb_ident_len) - sizeof (*ident);

	assert(af == AF_INET || af == AF_INET6);

	rc = ssh_calloc(1, sizeof (*rc));
	if (rc == NULL)
		return (rc);

	switch (ident->sadb_ident_type) {
	case SADB_IDENTTYPE_PREFIX:
		if (af == AF_INET) {
			rc->id_type = IPSEC_ID_IPV4_ADDR_SUBNET;
			rc->identification_len = 2 * sizeof (struct in_addr);
		} else {
			rc->id_type = IPSEC_ID_IPV6_ADDR_SUBNET;
			rc->identification_len = 2 * sizeof (struct in6_addr);
		}
		(void) memcpy(&rc->identification, id_data,
		    rc->identification_len);
		break;
	case SADB_IDENTTYPE_FQDN:
	case SADB_IDENTTYPE_USER_FQDN:
		rc->id_type = (ident->sadb_ident_type == SADB_IDENTTYPE_FQDN ?
		    IPSEC_ID_FQDN : IPSEC_ID_USER_FQDN);
		rc->identification_len = strlen((char *)id_data);
		rc->identification.fqdn = ssh_strdup((char *)id_data);
		if (rc->identification.fqdn == NULL) {
			ssh_free(rc);
			rc = NULL;
		}
		break;
	case SADB_X_IDENTTYPE_DN:
	case SADB_X_IDENTTYPE_GN:
	case SADB_X_IDENTTYPE_KEY_ID:
		rc->id_type = (ident->sadb_ident_type == SADB_X_IDENTTYPE_DN ?
		    IPSEC_ID_DER_ASN1_DN :
		    (ident->sadb_ident_type == SADB_X_IDENTTYPE_GN ?
		    IPSEC_ID_DER_ASN1_GN : IPSEC_ID_KEY_ID));
		/*
		 * NOTE: datalen may be overly padded.  Hopefully, the ASN.1
		 * has enough redundant info such that I don't have to modify
		 * PF_KEY to contain an explicit datalen field.
		 */
		rc->identification_len = datalen;
		rc->identification.asn1_data = ssh_calloc(1, datalen);
		if (rc->identification.asn1_data == NULL) {
			ssh_free(rc);
			rc = NULL;
		} else {
			(void) memcpy(rc->identification.asn1_data, id_data,
			    datalen);
		}
		break;
	case SADB_X_IDENTTYPE_ADDR_RANGE:
		if (af == AF_INET) {
			rc->id_type = IPSEC_ID_IPV4_ADDR_RANGE;
			rc->identification_len = 2 * sizeof (struct in_addr);
		} else {
			rc->id_type = IPSEC_ID_IPV6_ADDR_RANGE;
			rc->identification_len = 2 * sizeof (struct in6_addr);
		}
		(void) memcpy(&rc->identification, id_data,
		    rc->identification_len);
		break;
	default:
		PRTDBG(D_PFKEY, ("PF_KEY id to libike payload id: Unknown "
		    "type %d.", ident->sadb_ident_type));
		ssh_free(rc);
		rc = NULL;
		/* FALLTHRU */
	}

	return (rc);
}

/*
 * Convert authentication algorithm extension values to PF_KEY/DOI transform
 * values.
 */
int
auth_ext_to_doi(int ext)
{
	switch (ext) {
	case IPSEC_VALUES_AUTH_ALGORITHM_HMAC_MD5:
		return (SADB_AALG_MD5HMAC);
	case IPSEC_VALUES_AUTH_ALGORITHM_HMAC_SHA_1:
		return (SADB_AALG_SHA1HMAC);
	}
	/* Lucky for us, SHA-2 series maps directly. */
	return (ext);
}

const p2alg_t *
find_esp_encr_alg(uint_t doi_number)
{
	int i;

	if (p2_esp_encr_algs == NULL)
		return (NULL);

	for (i = 0; p2_esp_encr_algs[i].p2alg_doi_num != 0; i++)
		if (p2_esp_encr_algs[i].p2alg_doi_num == doi_number)
			return (&p2_esp_encr_algs[i]);
	return (NULL);
}

static const p2alg_t *
find_esp_auth_alg(uint_t doi_number)
{
	int i;

	if (p2_esp_auth_algs == NULL)
		return (NULL);

	for (i = 0; p2_esp_auth_algs[i].p2alg_doi_num != 0; i++)
		if (p2_esp_auth_algs[i].p2alg_doi_num == doi_number)
			return (&p2_esp_auth_algs[i]);
	return (NULL);
}

static const p2alg_t *
find_auth_alg(uint_t doi_number)
{
	int i;

	if (p2_ah_algs == NULL)
		return (NULL);

	for (i = 0; p2_ah_algs[i].p2alg_doi_num != 0; i++)
		if (p2_ah_algs[i].p2alg_doi_num == doi_number)
			return (&p2_ah_algs[i]);
	return (NULL);
}

/*
 * Given an algorithm, derive a length (in bytes).  Encryption algorithms are
 * from the PF_KEY/DOI transform space.  Authentication algorithms are from
 * the DOI's auth-alg attribute space.
 *
 * NOTE: The length hint is in bits!
 *
 * We do not validate that the user-supplied length is sane here; by
 * the time we get here, it's in some sense too late..
 */
static int
alg2keylen(boolean_t encrypt, int alg, int lenhint)
{
	const p2alg_t *algp;
	if (encrypt)
		algp = find_esp_encr_alg(alg);
	else
		algp = find_auth_alg(alg);

	if (algp == NULL)
		return (0);

	if (lenhint == 0)
		lenhint = algp->p2alg_default_incr;

	/* Season key (add salt) */
	lenhint += algp->p2alg_salt_bits;
	/* Return length in bytes */
	lenhint = SADB_1TO8(lenhint);
	return (lenhint);
}

/*
 * Extract `len' bytes of keying material from *key_so_far into a
 * sadb_key_t extension.  Caller is responsible for ensuring there's
 * enough room for us starting at *key, and that there's "len" bytes
 * of keying material available at **key_so_far.
 *
 * For now, we don't do any weak key checking.  This could be
 * dangerous if we can't reflect PF_KEY errors to the IKE negotiation.
 */
static void
extract_key(phase1_t *p1, int type, int len, uint8_t **key_so_far,
    sadb_key_t *key)
{
	uint8_t *so_far = *key_so_far;
	uint8_t *dst = (uint8_t *)(key + 1);

	if (p1 != NULL)
		p1->p1_stats.p1stat_keyuses++;

	key->sadb_key_exttype = type;
	key->sadb_key_bits = SADB_8TO1(len);
	key->sadb_key_len = SADB_8TO64(roundup(sizeof (*key) + len, 8));
	key->sadb_key_reserved = 0;

	(void) memcpy(dst, so_far, len);
	so_far += len;

	*key_so_far = so_far;
}

/*
 * Take the "lesser of" two values, but treat zeroes as highest value.
 */
static uint64_t
lesser_of(uint64_t challenger, uint64_t champion)
{
	/*
	 * Subtract 1 so zero becomes highest-possible value.
	 * Unsigned integer arithmetic is our friend here!
	 */
	uint64_t tchal = challenger - (uint64_t)1;
	uint64_t tchamp = champion - (uint64_t)1;

	return ((tchal > tchamp) ? champion : challenger);
}

static int
idtoprefixlen(SshIkePayloadID idp)
{
	/*
	 * This code currently does not deal with range as we have not
	 * seen it as a common or necessary piece of functionality.
	 * If we deal with range in the future, we should see if it's a
	 * broken peer that expresses a subnet in terms of a range.
	 */
	if (idp->id_type == IPSEC_ID_IPV4_ADDR_SUBNET) {
		struct in6_addr mask6;

		/* LINTED */
		IN6_INADDR_TO_V4MAPPED((struct in_addr *)
		    &idp->identification.ipv4_addr_netmask, &mask6);
		return (in_masktoprefix((&mask6)->s6_addr, B_TRUE));
	} else if (idp->id_type == IPSEC_ID_IPV6_ADDR_SUBNET) {
		return (in_masktoprefix(idp->
		    identification.ipv6_addr_netmask, B_FALSE));
	} else {
		return ((idp->id_type == IPSEC_ID_IPV4_ADDR) ?
		    SADB_8TO1(sizeof (struct in_addr)) :
		    SADB_8TO1(sizeof (struct in6_addr)));
	}
}


/*
 * Marshall an SADB_ADD or SADB_UPDATE request in all its full complexity.
 * This function will SADB_ADD an outgoing SA, the PF_KEY message will
 * include a sadb_x_pair_t extension which has the pair_spi value for
 * the matching inbound LARVAL SA, this value was returned by SADB_GETSPI.
 * The LARVAL SA is SADB_UPDATE'd once the SADB_ADD is complete.
 * If buf is NULL, just compute the length without actually assembling
 * the request.
 */
static size_t
marshall_samsg(uint64_t *buf, SshIkeNegotiation neg,
    SshIkePMPhaseQm pm_info, SshIkeIpsecKeymat keymat,
    SshIkeIpsecSelectedProtocol info, boolean_t inbound,
    uint64_t *my_lifetime_secs, uint64_t *my_lifetime_kb)
{
	size_t len;
	sadb_msg_t *samsg;
	sadb_x_kmc_t *kmc;
	sadb_address_t *src, *dst, *remote, *local, *natt, *innerdst, *innersrc;
	sadb_key_t *encrypt, *auth;
	sadb_lifetime_t *hard, *soft, *idle;
	sadb_sa_t *assoc;
	sadb_ident_t *remid, *locid;
	sadb_x_pair_t *pair;
	sadb_ext_t *sx;
	pid_t mypid = getpid();
	uint8_t keybuf[256], *key_so_far;
	struct sockaddr_storage *remote_sa, *local_sa, *natt_sa;
	struct sockaddr_storage *src_sa, *dst_sa;
	struct sockaddr_storage *innerdst_sa, *innersrc_sa;
	SshIkeIpsecAttributes attrs = &info->attributes;
	SshCryptoStatus rcc;
	parsedmsg_t *pmsg = (parsedmsg_t *)pm_info->policy_manager_data;
	phase1_t *p1 = (phase1_t *)pm_info->phase_i->policy_manager_data;
	SshIkePayloadID local_id, remote_id;
	int auth_len = 0, encr_len = 0;
	boolean_t is_initiator = pm_info->this_end_is_initiator;
	boolean_t tunnel_mode;
	struct ike_rule lifetimes;
	int auth_id, encr_id;
	uint8_t *spip, *ospip;
	char inlabel[] = {"Inbound SA."};
	char outlabel[] = {"Outbound SA."};

	uint64_t *curp = buf;

	/* local_id/remote_id are for Phase I (certificate) identities. */
	local_id = pm_info->phase_i->local_id;
	remote_id = pm_info->phase_i->remote_id;

	assert(info->spi_size_in == info->spi_size_out);
	spip = inbound ? info->spi_in : info->spi_out;
	ospip = inbound ? info->spi_out : info->spi_in;

	/* Inner destination is NULL in Transport mode */
	tunnel_mode = (pmsg->pmsg_pdss != NULL);

	/* Don't print the message if we're just getting the size. */
	if (buf != NULL)
		PRTDBG(D_PFKEY, ("Marshalling: %s",
		    tunnel_mode ? "Tunnel Mode " : "Transport Mode "));

/*
 * These macros are used for sizing up an allocation of a PF_KEY message.
 *
 * ALLOCP2() is the general case, where it assigns a pointer of type t1, but
 * moves the allocation by the size of type t2.
 *
 * ALLOCP() is where both types are the same.
 */
#define	ALLOCP2(x, t1, t2) x = (t1 *)curp; curp += SADB_8TO64(sizeof (t2))
#define	ALLOCP(x, t) ALLOCP2(x, t, t)
#define	ALLOCXTRA(n) curp += SADB_8TO64(roundup(n, 8))
#define	ALLOCEXT(ext) curp += ((ext)->sadb_ext_len)

	ALLOCP(samsg, sadb_msg_t);
	if (buf != NULL) {
		samsg->sadb_msg_version = PF_KEY_V2;
		samsg->sadb_msg_type = inbound ? SADB_UPDATE : SADB_ADD;
		samsg->sadb_msg_errno = 0;
		/* For starters... */
		samsg->sadb_msg_len = 0;
		samsg->sadb_msg_reserved = 0;
		if (pmsg == NULL)
			samsg->sadb_msg_seq = 0;
		else
			samsg->sadb_msg_seq = pmsg->pmsg_samsg->sadb_msg_seq;
		samsg->sadb_msg_pid = mypid;
	}

	ALLOCP(kmc, sadb_x_kmc_t);
	if (buf != NULL) {
		kmc->sadb_x_kmc_exttype = SADB_X_EXT_KM_COOKIE;
		kmc->sadb_x_kmc_len = SADB_8TO64(sizeof (*kmc));
		kmc->sadb_x_kmc_proto = SADB_X_KMP_IKE;
		/* See NOTE: at the beginning about p1 being NULL. */
		kmc->sadb_x_kmc_cookie = (p1 == NULL) ? 0 : p1->p1_rule->cookie;
		kmc->sadb_x_kmc_reserved = 0;
	}

	ALLOCP(assoc, sadb_sa_t);
	if (buf != NULL) {
		assoc->sadb_sa_len = SADB_8TO64(sizeof (*assoc));
		assoc->sadb_sa_exttype = SADB_EXT_SA;
		/* TODO: Can we get this from the neg?  Or the ACQUIRE? */
		assoc->sadb_sa_replay = 32;
		assoc->sadb_sa_state = SADB_SASTATE_MATURE;
		/* NOTE:  This'll also depend on pm_info or other goodies. */
		if (inbound)
			assoc->sadb_sa_flags = SADB_X_SAFLAGS_INBOUND;
		else
			assoc->sadb_sa_flags = SADB_X_SAFLAGS_OUTBOUND;
		(void) memcpy(&assoc->sadb_sa_spi, spip,
		    sizeof (assoc->sadb_sa_spi));
	}

	/*
	 * Only outbound SADB_ADD messages have a pair extension.
	 */
	if (!inbound) {
		ALLOCP(pair, sadb_x_pair_t);
		if (buf != NULL) {
			pair->sadb_x_pair_len = SADB_8TO64(sizeof (*pair));
			pair->sadb_x_pair_exttype = SADB_X_EXT_PAIR;
			(void) memcpy(&pair->sadb_x_pair_spi, ospip,
			    sizeof (assoc->sadb_sa_spi));
			assoc->sadb_sa_flags |= SADB_X_SAFLAGS_PAIRED;
		}
	}

	/*
	 * Determine key lengths, etc.
	 */
	switch (info->protocol_id) {
	case SSH_IKE_PROTOCOL_IPSEC_AH:
		auth_id = info->transform_id.ipsec_ah;
		auth_len = alg2keylen(B_FALSE, auth_id, attrs->key_length);
		encr_id = 0;
		encr_len = 0;
		if (buf != NULL) {
			assoc->sadb_sa_encrypt = 0;
			assoc->sadb_sa_auth = auth_id;
			samsg->sadb_msg_satype = SADB_SATYPE_AH;
		}
		break;
	case SSH_IKE_PROTOCOL_IPSEC_ESP:
		auth_id = auth_ext_to_doi(attrs->auth_algorithm);
		auth_len = alg2keylen(B_FALSE, auth_id, 0);
		encr_id = info->transform_id.ipsec_esp;
		/* alg2keylen() knows about salt */
		encr_len = alg2keylen(B_TRUE, encr_id, attrs->key_length);
		if (buf != NULL) {
			assoc->sadb_sa_encrypt = encr_id;
			assoc->sadb_sa_auth = auth_id;
			samsg->sadb_msg_satype = SADB_SATYPE_ESP;
			if (pm_info->phase_i->behind_nat)
				assoc->sadb_sa_flags |= SADB_X_SAFLAGS_NATTED;
		}
		break;
	default:
		EXIT_FATAL2("Unsupported ISAKMP protocol %d.",
		    info->protocol_id);
	}

	/*
	 * The library provides local and remote phase II identities,
	 * as picked by both initiator and responder, and provides no useful
	 * clues about how to select them..
	 * What we do is to pick the "more restrictive" of the two.
	 */
#define	EXT_PROTO_PORT(addr, sa, id)				\
	if ((id) != NULL) {						\
		SshIkePayloadID rid = (id);				\
		if (rid->protocol_id != 0)				\
			(addr)->sadb_address_proto = rid->protocol_id;	\
		if (rid->port_number != 0)				\
			((struct sockaddr_in *)(sa))->sin_port = 	\
				htons(rid->port_number);		\
	}

	/* Addresses. */
	ALLOCP(src, sadb_address_t);
	ALLOCP2(src_sa, struct sockaddr_storage, struct sockaddr_in6);
	ALLOCP(dst, sadb_address_t);
	ALLOCP2(dst_sa, struct sockaddr_storage, struct sockaddr_in6);

	if (buf != NULL) {
		src->sadb_address_len = SADB_8TO64(sizeof (*src) +
		    sizeof (struct sockaddr_in6));
		dst->sadb_address_len = SADB_8TO64(sizeof (*dst) +
		    sizeof (struct sockaddr_in6));
		src->sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
		dst->sadb_address_exttype = SADB_EXT_ADDRESS_DST;

		if (inbound) {
			remote = src;
			remote_sa = src_sa;
			local = dst;
			local_sa = dst_sa;
		} else {
			remote = dst;
			remote_sa = dst_sa;
			local = src;
			local_sa = src_sa;
		}

		(void) memset(remote_sa, 0, sizeof (struct sockaddr_in6));
		if (!string_to_sockaddr(pm_info->remote_ip, remote_sa))
			EXIT_FATAL("marshall_samsg: string_to_sockaddr(remote) "
			    "failed.");
		((struct sockaddr_in *)remote_sa)->sin_port = 0;
		remote->sadb_address_proto = 0;
		remote->sadb_address_prefixlen = 0;
		remote->sadb_address_reserved = 0;

		if (!tunnel_mode) {
			/*
			 * Since EXT_PROTO_PORT writes to the same place, order
			 * matters. If we are initiator prefer ports from the
			 * initiator id, otherwise prefer the responder id
			 */

			if (is_initiator) {
				EXT_PROTO_PORT(remote, remote_sa,
				    pm_info->remote_r_id);
				EXT_PROTO_PORT(remote, remote_sa,
				    pm_info->remote_i_id);
			} else {
				EXT_PROTO_PORT(remote, remote_sa,
				    pm_info->remote_i_id);
				EXT_PROTO_PORT(remote, remote_sa,
				    pm_info->remote_r_id);
			}
		}
		local->sadb_address_reserved = 0;

		(void) memset(local_sa, 0, sizeof (struct sockaddr_in6));
		if (!string_to_sockaddr(pm_info->local_ip, local_sa))
			EXIT_FATAL("marshall_samsg: string_to_sockaddr(local) "
			    "failed.");

		((struct sockaddr_in *)local_sa)->sin_port = 0;
		local->sadb_address_proto = 0;
		local->sadb_address_prefixlen = 0;
		local->sadb_address_reserved = 0;

		if (!tunnel_mode) {
			if (is_initiator) {
				EXT_PROTO_PORT(local, local_sa,
				    pm_info->local_r_id);
				EXT_PROTO_PORT(local, local_sa,
				    pm_info->local_i_id);
			} else {
				EXT_PROTO_PORT(local, local_sa,
				    pm_info->local_i_id);
				EXT_PROTO_PORT(local, local_sa,
				    pm_info->local_r_id);
			}
		}
	}

	if (pm_info->other_port != NULL ||
	    pm_info->phase_i->negotiation->sa->sa_other_port != NULL) {
		ipaddr_t local_cmpval, remote_cmpval;
		char *opp;

		/*
		 * There's at least one NAT box in between me and my peer.
		 *
		 * I need to store in the SA a NATT_REM and/or a NATT_LOC
		 * address extension.
		 *
		 * If I'm behind a NAT box, I need to store a NATT_LOC, where
		 * the NATT_LOC stores my NAT box's public (globally-routable)
		 * address.
		 *
		 * If my peer's behind a NAT box, I need to store a NATT_REM,
		 * where the NATT_REM stores what the peer thinks his/her
		 * IP address is.  (The SA's actual address for the peer
		 * will be the SA's source or dest addr, depending on the
		 * SA's direction.)
		 *
		 * The Quick Mode state provided by libike tells me who sent
		 * the first Quick Mode packet and is the QM "initiator".  We
		 * exploit this to map initiator and responder to local and
		 * remote.
		 *
		 * NOTE:  For tunnel-mode SAs, there may not have been a
		 * NAT-OA payload.  The cmpvals are *irrelevant* for actual
		 * checksumming.
		 */

		if (pm_info->other_port != NULL) {
			opp = pm_info->other_port;
			if (is_initiator) {
				local_cmpval =
				    (ipaddr_t)(pm_info->natt_init_ip);
				remote_cmpval =
				    (ipaddr_t)(pm_info->natt_resp_ip);
			} else {
				local_cmpval =
				    (ipaddr_t)(pm_info->natt_resp_ip);
				remote_cmpval =
				    (ipaddr_t)(pm_info->natt_init_ip);
			}
		} else {
			PRTDBG(D_PFKEY,
			    ("marshall_samsg: NAT-T but with no NAT-OA."));
			PRTDBG(D_PFKEY, ("  tunnel mode == %d (should be 1)",
			    tunnel_mode));
			if (!tunnel_mode) {
				PRTDBG(D_PFKEY,
				    ("  Expect a PF_KEY NAT error soon."));
			}
			/*
			 * Set the addresses to unspecified.  PF_KEY won't
			 * like it unless it's tunnel mode.  Make sure we
			 * don't abort in this case.
			 */
			local_cmpval = remote_cmpval = 0;
			opp = pm_info->phase_i->negotiation->sa->sa_other_port;
		}

		if (buf == NULL) {
			ALLOCP(natt, sadb_address_t);
			ALLOCP2(natt_sa, struct sockaddr_storage,
			    struct sockaddr_in6);
		} else if (local_cmpval !=
		    ((struct sockaddr_in *)local_sa)->sin_addr.s_addr ||
		    nat_t_port != IPPORT_IKE_NATT) {
			struct sockaddr_in6 *tsin6;

			/* IPv4-only for now. */
			assert(local_sa->ss_family == AF_INET);
			/*
			 * I'm behind a NAT box.  Store the NAT's public
			 * address in NATT_LOC.
			 */

			ALLOCP(natt, sadb_address_t);

			natt->sadb_address_exttype =
			    SADB_X_EXT_ADDRESS_NATT_LOC;
			assoc->sadb_sa_flags |= SADB_X_SAFLAGS_NATT_LOC;
			natt->sadb_address_reserved = 0;
			natt->sadb_address_proto = IPPROTO_UDP;
			natt->sadb_address_prefixlen = 0;
			natt->sadb_address_len = SADB_8TO64(sizeof (*natt) +
			    sizeof (struct sockaddr_in6));

			ALLOCP2(natt_sa, struct sockaddr_storage,
			    struct sockaddr_in6);

			(void) memset(natt_sa, 0, sizeof (struct sockaddr_in6));
			tsin6 = ((struct sockaddr_in6 *)natt_sa);

			if (local_cmpval !=
			    ((struct sockaddr_in *)local_sa)->sin_addr.s_addr) {
				IN6_IPADDR_TO_V4MAPPED(local_cmpval,
				    &tsin6->sin6_addr);
			} else {
				IN6_IPADDR_TO_V4MAPPED(INADDR_ANY,
				    &tsin6->sin6_addr);
			}
			tsin6->sin6_port = htons(nat_t_port);
			tsin6->sin6_family = AF_INET6;
		}

		if (buf == NULL) {
			ALLOCP(natt, sadb_address_t);
			ALLOCP2(natt_sa, struct sockaddr_storage,
			    struct sockaddr_in6);
		} else if (remote_cmpval !=
		    ((struct sockaddr_in *)remote_sa)->sin_addr.s_addr ||
		    nat_t_port != IPPORT_IKE_NATT) {
			struct sockaddr_in6 *tsin6;
			/*
			 * My peer's behind a NAT box.  Store his/her NAT's
			 * public address in NATT_REM.
			 */

			/* IPv4-only for now. */
			assert(remote_sa->ss_family == AF_INET);
			ALLOCP(natt, sadb_address_t);

			natt->sadb_address_exttype =
			    SADB_X_EXT_ADDRESS_NATT_REM;
			assoc->sadb_sa_flags |= SADB_X_SAFLAGS_NATT_REM;
			natt->sadb_address_reserved = 0;
			natt->sadb_address_len = SADB_8TO64(sizeof (*natt) +
			    sizeof (struct sockaddr_in6));

			ALLOCP2(natt_sa, struct sockaddr_storage,
			    struct sockaddr_in6);

			(void) memset(natt_sa, 0,
			    sizeof (struct sockaddr_in6));
			tsin6 = ((struct sockaddr_in6 *)natt_sa);

			IN6_IPADDR_TO_V4MAPPED(remote_cmpval,
			    &tsin6->sin6_addr);
			/* Use NAT-D discovered "other port" here. */
			tsin6->sin6_port = htons((unsigned short)atoi(opp));
			tsin6->sin6_family = AF_INET6;
		}

	}

	if (tunnel_mode) {
		/* Inner Addresses. */
		ALLOCP(innerdst, sadb_address_t);
		if (buf != NULL) {
			innerdst->sadb_address_exttype =
			    inbound ? SADB_X_EXT_ADDRESS_INNER_SRC :
			    SADB_X_EXT_ADDRESS_INNER_DST;
			innerdst->sadb_address_reserved = 0;
			innerdst->sadb_address_len =
			    SADB_8TO64(sizeof (*innerdst) +
			    sizeof (struct sockaddr_in6));
			assoc->sadb_sa_flags |= SADB_X_SAFLAGS_TUNNEL;
		}

		ALLOCP2(innerdst_sa, struct sockaddr_storage,
		    struct sockaddr_in6);
		if (buf != NULL) {
			innerdst->sadb_address_prefixlen =
			    idtoprefixlen(pm_info->remote_i_id);

			(void) memcpy(innerdst_sa, pmsg->pmsg_pdss,
			    sizeof (struct sockaddr_in6));

			((struct sockaddr_in *)innerdst_sa)->sin_port = 0;
			innerdst->sadb_address_proto = 0;
			innerdst->sadb_address_reserved = 0;
			/*
			 * Since EXT_PROTO_PORT writes to the same place, order
			 * matters. If we are initiator prefer ports from the
			 * initiator id, otherwise prefer the responder id
			 */

			if (is_initiator) {
				EXT_PROTO_PORT(innerdst, innerdst_sa,
				    pm_info->remote_r_id);
				EXT_PROTO_PORT(innerdst, innerdst_sa,
				    pm_info->remote_i_id);
			} else {
				EXT_PROTO_PORT(innerdst, innerdst_sa,
				    pm_info->remote_i_id);
				EXT_PROTO_PORT(innerdst, innerdst_sa,
				    pm_info->remote_r_id);
			}
		}

		ALLOCP(innersrc, sadb_address_t);
		if (buf != NULL) {
			innersrc->sadb_address_exttype =
			    inbound ? SADB_X_EXT_ADDRESS_INNER_DST :
			    SADB_X_EXT_ADDRESS_INNER_SRC;
			innersrc->sadb_address_reserved = 0;
			innersrc->sadb_address_len =
			    SADB_8TO64(sizeof (*innersrc) +
			    sizeof (struct sockaddr_in6));
			assoc->sadb_sa_flags |= SADB_X_SAFLAGS_TUNNEL;
		}
		ALLOCP2(innersrc_sa, struct sockaddr_storage,
		    struct sockaddr_in6);
		if (buf != NULL) {

			innersrc->sadb_address_prefixlen =
			    idtoprefixlen(pm_info->local_i_id);

			(void) memcpy(innersrc_sa, pmsg->pmsg_psss,
			    sizeof (struct sockaddr_in6));
			((struct sockaddr_in *)innersrc_sa)->sin_port = 0;
			innersrc->sadb_address_proto = 0;
			innersrc->sadb_address_reserved = 0;

			if (is_initiator) {
				EXT_PROTO_PORT(innersrc, innersrc_sa,
				    pm_info->local_r_id);
				EXT_PROTO_PORT(innersrc, innersrc_sa,
				    pm_info->local_i_id);
			} else {
				EXT_PROTO_PORT(innersrc, innersrc_sa,
				    pm_info->local_i_id);
				EXT_PROTO_PORT(innersrc, innersrc_sa,
				    pm_info->local_r_id);
			}
		}

	}

#undef EXT_PROTO_PORT

	/*
	 * Lifetimes:
	 *
	 * The phase1_t comes with complete with policy manager data
	 * in the form of a "rule" - this determines the parameters used for
	 * the SA generated as a result of the QM. The lifetime values used
	 * in the "rule" come pre-sanitized based on the following logic:
	 *
	 * If a lifetime value was defined in the configuration file then
	 * use this. (EG: p2_lifetime_secs = 600)
	 *
	 * If lifetime value is not defined in the config file, use
	 * values from the ACQUIRE message, these default to zero, but
	 * can be tweeked with ndd(1M).
	 *
	 * If still zero, use sensible defaults.
	 *
	 * Use the rule value unless the value proposed by the peer was
	 * smaller, in which case use that.
	 */

	if (buf != NULL) {
		(void) memset(&lifetimes, 0, sizeof (struct ike_rule));
		if (inbound)
			lifetimes.label = inlabel;
		else
			lifetimes.label = outlabel;

		if (p1 != NULL) {
			PRTDBG(D_PFKEY, ("ISAKMP: %u secs, rule %u secs, "
			    "p1 cache %u secs",
			    attrs->life_duration_secs,
			    p1->p1_rule->p2_lifetime_secs,
			    p1->p2_lifetime_secs));
			PRTDBG(D_PFKEY, ("ISAKMP: %u KB, rule %u KB, "
			    "p1 cache %u KB",
			    attrs->life_duration_kb,
			    p1->p1_rule->p2_lifetime_kb,
			    p1->p2_lifetime_kb));

			lifetimes.p2_lifetime_secs = lesser_of(
			    p1->p1_rule->p2_lifetime_secs,
			    attrs->life_duration_secs);
			lifetimes.p2_lifetime_secs = lesser_of(
			    lifetimes.p2_lifetime_secs,
			    p1->p2_lifetime_secs);

			lifetimes.p2_lifetime_kb = lesser_of(
			    p1->p1_rule->p2_lifetime_kb,
			    attrs->life_duration_kb);
			lifetimes.p2_lifetime_kb = lesser_of(
			    lifetimes.p2_lifetime_kb,
			    p1->p2_lifetime_kb);

			p1->p2_lifetime_secs = lifetimes.p2_lifetime_secs;
			p1->p2_lifetime_kb = lifetimes.p2_lifetime_kb;

			lifetimes.p2_softlife_secs =
			    p1->p1_rule->p2_softlife_secs;
			lifetimes.p2_softlife_kb =
			    p1->p1_rule->p2_softlife_kb;
			lifetimes.p2_idletime_secs =
			    p1->p1_rule->p2_idletime_secs;

		} else {
			lifetimes.p2_lifetime_secs = attrs->life_duration_secs;
			lifetimes.p2_lifetime_kb = attrs->life_duration_kb;
		}

		/* Sanity check */
		check_rule(&lifetimes, B_FALSE);
	}

	ALLOCP(hard, sadb_lifetime_t);
	if (buf != NULL) {
		hard->sadb_lifetime_len = SADB_8TO64(sizeof (*hard));
		hard->sadb_lifetime_exttype = SADB_EXT_LIFETIME_HARD;
		hard->sadb_lifetime_allocations = 0;
		hard->sadb_lifetime_usetime = 0;
		/* convert KB -> bytes. */
		hard->sadb_lifetime_bytes =
		    (uint64_t)lifetimes.p2_lifetime_kb << 10;
		hard->sadb_lifetime_addtime =
		    (uint64_t)lifetimes.p2_lifetime_secs;

		if (my_lifetime_secs != NULL)
			*my_lifetime_secs = hard->sadb_lifetime_addtime;
		if (my_lifetime_kb != NULL)
			*my_lifetime_kb = (uint64_t)lifetimes.p2_lifetime_kb;
	}

	ALLOCP(soft, sadb_lifetime_t);
	if (buf != NULL) {
		*soft = *hard;
		soft->sadb_lifetime_exttype = SADB_EXT_LIFETIME_SOFT;
		/* convert KB -> bytes. */
		soft->sadb_lifetime_bytes =
		    (uint64_t)lifetimes.p2_softlife_kb << 10;
		soft->sadb_lifetime_addtime =
		    (uint64_t)lifetimes.p2_softlife_secs;
	}

	sx = pmsg->pmsg_exts[SADB_EXT_SENSITIVITY];
	if (is_ike_labeled() && (sx != NULL)) {
		struct ike_rule *r = p1->p1_rule;
		sadb_sens_t *outer;

		if (buf != NULL) {
			(void) memcpy(curp, sx,
			    SADB_64TO8(sx->sadb_ext_len));
			if (!p1->label_aware) {
				sadb_sens_t *inner = (sadb_sens_t *)curp;
				inner->sadb_x_sens_flags |=
				    SADB_X_SENS_UNLABELED;
			}
		}
		ALLOCEXT(sx);

		if (buf != NULL) {
			if (r->p1_override_label)
				sx = (sadb_ext_t *)p1->outer_label;
			else
				sx = pmsg->pmsg_exts[SADB_EXT_SENSITIVITY];
			(void) memcpy(curp, sx, SADB_64TO8(sx->sadb_ext_len));

			outer = (sadb_sens_t *)curp;
			outer->sadb_sens_exttype = SADB_X_EXT_OUTER_SENS;

			if (r->p1_implicit_label) {
				outer->sadb_x_sens_flags |=
				    SADB_X_SENS_IMPLICIT;
			}
		}
		ALLOCEXT(sx);
	}

	if (buf != NULL) {
		/*
		 * Keying material.  Here's where the fun begins.
		 */

		(void) memset(keybuf, 0, sizeof (keybuf));
		len = auth_len + encr_len;

		rcc = ssh_ike_ipsec_keys(neg, keymat, info->spi_size_out,
		    spip, info->protocol_id, SADB_8TO1(len), keybuf);

		if (rcc != SSH_CRYPTO_OK) {
			EXIT_FATAL2("Could not calculate IPsec key, "
			    "error = %d.", rcc);
			/* TODO: add SshCryptoStatus num to string lookup */
		}
		key_so_far = (uint8_t *)&keybuf;
	}

	if (((phase1_t *)pm_info->phase_i->policy_manager_data)->p1_use_dpd) {
		/*
		 * For now, only add IDLE timeouts if we have a DPD capable
		 * Phase I.
		 */
		ALLOCP(idle, sadb_lifetime_t);
		if (buf != NULL) {
			/* lifetime string representation */
			char secs_str[SECS_STR_SIZE];
			int fuzz;

			idle->sadb_lifetime_len = SADB_8TO64(sizeof (*idle));
			idle->sadb_lifetime_exttype = SADB_X_EXT_LIFETIME_IDLE;
			idle->sadb_lifetime_allocations = 0;
			idle->sadb_lifetime_bytes = 0;
			idle->sadb_lifetime_addtime =
			    (uint64_t)lifetimes.p2_idletime_secs;
			idle->sadb_lifetime_usetime = 0;

			/*
			 * Fuzz out the idle expiration. Unlike PF_KEY soft
			 * lifetimes (which are fuzzed in-kernel) user-space
			 * may have more precise semantics for IDLE timeouts,
			 * so let user space do the fuzz. Use +/- 7 seconds
			 * for the fuzz value.
			 *
			 * NOTE: The fuzz must always be smaller than MINDIFF
			 *	 otherwise IDLE can be greater than SOFT
			 *	 lifetime. See check_rule() for the rules.
			 */
			fuzz = ((int)(ssh_random_get_byte() & 0xF)) - 8;

			/*
			 * Guard against values that would push us below the
			 * minimum and fuzz the minimum positively in such case.
			 */
			if (idle->sadb_lifetime_addtime + fuzz <
			    MIN_P2_LIFETIME_IDLE_SECS)
				idle->sadb_lifetime_addtime =
				    MIN_P2_LIFETIME_IDLE_SECS + abs(fuzz);
			else
				idle->sadb_lifetime_addtime += fuzz;

			PRTDBG(D_PFKEY, ("IDLE lifetime fuzzed to %" PRIu64
			    " seconds%s", idle->sadb_lifetime_addtime,
			    secs2out(idle->sadb_lifetime_addtime,
			    secs_str, sizeof (secs_str), SPC_BEGIN)));
		}
	}

	/*
	 * Note: all selectors must be added before keys.  Code in
	 * pfkey_resync_update may trim off any extensions at or after
	 * the key extensions to convert an UPDATE into a DELETE.
	 */

	if (encr_id != 0 && encr_id != SADB_EALG_NULL) {
		ALLOCP(encrypt, sadb_key_t);
		ALLOCXTRA(encr_len);
		if (buf != NULL)
			extract_key(p1, SADB_EXT_KEY_ENCRYPT, encr_len,
			    &key_so_far, encrypt);
	}

	if (auth_id != 0) {
		ALLOCP(auth, sadb_key_t);
		ALLOCXTRA(auth_len);
		if (buf != NULL) {
			extract_key(p1, SADB_EXT_KEY_AUTH, auth_len,
			    &key_so_far, auth);
		}
	}

	if (buf != NULL) {
		/* Burn key ASAP */
		(void) memset(keybuf, 0, sizeof (keybuf));
	}
	/*
	 * Add identity extensions if appropriate.
	 */
	len = payloadid_size(local_id);
	if (len != 0) {
		ALLOCP(locid, sadb_ident_t);
		ALLOCXTRA(len);
		if (buf != NULL)
			payloadid_to_pfreq(local_id, !inbound, locid);
	}
	len = payloadid_size(remote_id);
	if (len != 0) {
		ALLOCP(remid, sadb_ident_t);
		ALLOCXTRA(len);
		if (buf != NULL)
			payloadid_to_pfreq(remote_id, inbound, remid);
	}
#undef ALLOCP2
#undef ALLOCP
#undef ALLOCXTRA
#undef ALLOCEXT

	len = curp - buf;

	if (buf != NULL) {
		/* byte lifetime string representation */
		char byte_str[BYTE_STR_SIZE];
		/* seconds lifetime string representation */
		char secs_str[SECS_STR_SIZE];
		char pair_spi[32];

		samsg->sadb_msg_len = len;
		bzero(pair_spi, sizeof (pair_spi));
		if (samsg->sadb_msg_type == SADB_ADD)
			(void) ssh_snprintf(pair_spi, sizeof (pair_spi),
			    " (pair SPI = 0x%02x%02x%02x%02x)",
			    ospip[0], ospip[1], ospip[2], ospip[3]);
		PRTDBG(D_OP, ("%s %s P2 SA: %s -> %s",
		    samsg->sadb_msg_type == SADB_ADD ? "Adding" : "Updating",
		    inbound ? "Inbound" : "Outbound",
		    inbound ? pm_info->remote_ip : pm_info->local_ip,
		    inbound ? pm_info->local_ip : pm_info->remote_ip));
		PRTDBG(D_OP, ("  SPI = 0x%02x%02x%02x%02x%s",
		    spip[0], spip[1], spip[2], spip[3], pair_spi));
		PRTDBG(D_OP, ("  SA Lifetimes:"));
		PRTDBG(D_OP, ("    HARD = %" PRIu64 " seconds%s",
		    (uint64_t)hard->sadb_lifetime_addtime,
		    secs2out((uint64_t)hard->sadb_lifetime_addtime,
		    secs_str, sizeof (secs_str), SPC_BEGIN)));
		PRTDBG(D_OP, ("    SOFT = %" PRIu64 " seconds%s",
		    (uint64_t)soft->sadb_lifetime_addtime,
		    secs2out((uint64_t)soft->sadb_lifetime_addtime,
		    secs_str, sizeof (secs_str), SPC_BEGIN)));
		if (hard->sadb_lifetime_bytes == 0) {
			PRTDBG(D_OP, ("    Byte lifetimes not defined."));
		} else {
			PRTDBG(D_OP, ("    HARD = %" PRIu64 " bytes%s",
			    (uint64_t)hard->sadb_lifetime_bytes,
			    bytecnt2out((uint64_t)hard->sadb_lifetime_bytes,
			    byte_str, sizeof (byte_str), SPC_BEGIN)));
			PRTDBG(D_OP, ("    SOFT = %" PRIu64 " bytes%s",
			    (uint64_t)soft->sadb_lifetime_bytes,
			    bytecnt2out((uint64_t)soft->sadb_lifetime_bytes,
			    byte_str, sizeof (byte_str), SPC_BEGIN)));
		}
	}

	return (SADB_64TO8(len));
}

/*
 * Add a pair of IPsec SAs.
 *
 * Both PF_KEY messages look strikingly similar, differing only in the
 * keying material, SPI, and src vs dest of identities and addresses.
 *
 * We cheat.  We construct the outbound SA's message in the inbound
 * SA's buffer, copy it to the outbound buffer, then update the
 * message in the inbound buffer to be the proper inbound message
 * since we have pointers into the inbound buffer from constructing
 * the outbound message in it.
 *
 * This is simpler than playing pointer math games.
 */
static void
add_new_sa(SshIkeNegotiation neg, SshIkePMPhaseQm pm_info,
    SshIkeIpsecKeymat keymat, SshIkeIpsecSelectedProtocol info)
{
	SshIkeIpsecAttributes attrs = &info->attributes;
	phase1_t *p1 = (phase1_t *)pm_info->phase_i->policy_manager_data;
	pfkeyreq_t *outreq, *inreq;
	uint64_t *outsabuf, *insabuf;
	uint64_t my_lifetime_secs = 0, my_lifetime_kb = 0;
	size_t samsg_size, inlen, outlen;
	rl_act_t *rla;
	char byte_str[BYTE_STR_SIZE]; /* byte lifetime string representation */
	char secs_str[SECS_STR_SIZE]; /* buffer for seconds representation */
	char kb_str[40];

	/*
	 * This pair of SAs represents a new "suite" for the official
	 * count for the parent phase 1 SA...update it!
	 *
	 * NOTE:  That p1 can be NULL shows an interesting race condition,
	 * probably when two peers initiate near-simultaneously.
	 */
	if (p1 != NULL) {
		p1->p1_stats.p1stat_new_qm_sas++;
	}

	outreq = ssh_malloc(sizeof (*outreq));
	inreq = ssh_malloc(sizeof (*inreq));
	if (inreq == NULL || outreq == NULL) {
		PRTDBG(D_PFKEY, ("add_new_sa(): out of memory."));
		ssh_free(inreq);
		ssh_free(outreq);
		/*
		 * Question: How do we send a delete notification when
		 * we're out of memory?
		 */
		return;
	}

	/*
	 * Determine how much space we need for each SA
	 */
	samsg_size = marshall_samsg(NULL, neg, pm_info, keymat, info,
	    B_FALSE, NULL, NULL);

	outsabuf = ssh_malloc(samsg_size);
	insabuf = ssh_malloc(samsg_size);
	if (outsabuf == NULL || insabuf == NULL) {
		PRTDBG(D_PFKEY, ("add_new_sa(): out of memory."));
		ssh_free(inreq);
		ssh_free(outreq);
		ssh_free(outsabuf);
		ssh_free(insabuf);
		/*
		 * Question: How do we send a delete notification when
		 * we're out of memory?
		 */
		return;
	}
	outlen = marshall_samsg(outsabuf, neg, pm_info, keymat, info,
	    B_FALSE, &my_lifetime_secs, &my_lifetime_kb);
	if (outlen > samsg_size) {
		PRTDBG(D_PFKEY,
		    ("mismatch on remarshall: orig %lx outlen %lx\n",
		    (ulong_t)outlen, (ulong_t)samsg_size));
		abort();
	}

	/*
	 * Add the outbound SA using SADB_ADD
	 */
	outreq->pr_handler = add_complete;
	outreq->pr_context = NULL;
	outreq->pr_req = (sadb_msg_t *)outsabuf;

	DUMP_PFKEY(outsabuf);

	pfkey_request(outreq);

	inlen = marshall_samsg(insabuf, neg, pm_info, keymat, info,
	    B_TRUE, &my_lifetime_secs, &my_lifetime_kb);
	if (inlen > samsg_size) {
		PRTDBG(D_PFKEY,
		    ("mismatch on remarshall: orig %lx inlen %lx\n",
		    (ulong_t)outlen, (ulong_t)samsg_size));
		abort();
	}

	/*
	 * Add the inbound SA using SADB_UPDATE since it may have started
	 * life as an ACQUIRE
	 */
	inreq->pr_handler = update_complete;
	inreq->pr_context = NULL;
	inreq->pr_req = (sadb_msg_t *)insabuf;

	bzero(secs_str, sizeof (secs_str));
	bzero(kb_str, sizeof (kb_str));
	PRTDBG(D_OP, ("Incoming SA: PF_KEY lifetime %" PRIu64 " s, "
	    "%" PRIu64 " KB%s", my_lifetime_secs, my_lifetime_kb,
	    bytecnt2out(my_lifetime_kb << 10, byte_str, sizeof (byte_str),
	    SPC_BEGIN)));
	if (attrs->life_duration_secs != 0)
		(void) ssh_snprintf(secs_str, sizeof (secs_str),
		    "%"  PRIu64 " s",
		    (uint64_t)attrs->life_duration_secs);
	if (attrs->life_duration_kb != 0)
		(void) ssh_snprintf(kb_str, sizeof (kb_str),
		    "%" PRIu64 " KB%s",
		    (uint64_t)attrs->life_duration_kb,
		    bytecnt2out((uint64_t)attrs->life_duration_kb << 10,
		    byte_str, sizeof (byte_str), SPC_BEGIN));
	PRTDBG(D_OP, ("             ISAKMP lifetime %s%s%s",
	    secs_str, strlen(secs_str) > 0 && strlen(kb_str) > 0 ? ", " : "",
	    kb_str));

	/*
	 * Carry along a link to the parent negotiation to allow DELETE.
	 * and/or RESPONDER-LIFETIME notification to be sent.
	 *
	 * If the lifetime is smaller than the attrs, and I'm the responder,
	 * prepare to send RESPONDER-LIFETIME notification.  (And do so
	 * in the *_complete cases.
	 *
	 * If we fail to add the SA, prepare to send a DELETE.
	 */
	if (p1 != NULL) {
		rla = (rl_act_t *)ssh_malloc(sizeof (rl_act_t));
		if (rla != NULL) {
			(void) memcpy(&rla->my_spi, info->spi_in,
			    sizeof (uint32_t));
			rla->my_local = p1->p1_local;  /* struct assignment */
			rla->my_neg = p1->p1_negotiation;
			rla->my_lifetime_secs = 0;
			rla->my_lifetime_kb = 0;
			if (((my_lifetime_secs < attrs->life_duration_secs) ||
			    attrs->life_duration_secs == 0) &&
			    !(pm_info->this_end_is_initiator)) {
				rla->my_lifetime_secs = my_lifetime_secs;
				PRTDBG(D_OP, ("Using local policy defined "
				    "lifetime of %" PRIi64 " secs",
				    (uint64_t)my_lifetime_secs));
			}
			/*
			 * Only include kilobyte lifetime in RESPONDER-LIFETIME
			 * message if it was sent by the peer in Phase 2 and
			 * is greater than local policy value.
			 */
			if ((my_lifetime_kb < attrs->life_duration_kb) &&
			    !(pm_info->this_end_is_initiator)) {
				rla->my_lifetime_kb = my_lifetime_kb;
				PRTDBG(D_OP, ("Using local policy defined "
				    "lifetime of %" PRIi64 " KB%s",
				    (uint64_t)my_lifetime_kb,
				    bytecnt2out((uint64_t)my_lifetime_kb << 10,
				    byte_str, sizeof (byte_str), SPC_BEGIN)));
			}
			inreq->pr_context = rla;
		}
	}
	PRTDBG(D_OP, ("Updating Incoming P2 SA: %s -> %s",
	    pm_info->remote_ip, pm_info->local_ip));
	PRTDBG(D_OP, ("  SPI = 0x%02x%02x%02x%02x", info->spi_in[0],
	    info->spi_in[1], info->spi_in[2], info->spi_in[3]));
	PRTDBG(D_OP, ("  Lifetime = %" PRIu64 " s%s, %" PRIu64 " KB%s",
	    (uint64_t)my_lifetime_secs, secs2out((uint64_t)my_lifetime_secs,
	    secs_str, sizeof (secs_str), SPC_BEGIN), my_lifetime_kb,
	    bytecnt2out(my_lifetime_kb << 10, byte_str, sizeof (byte_str),
	    SPC_BEGIN)));

	DUMP_PFKEY(insabuf);

	pfkey_request(inreq);
}

/*
 * Called to inform the peer that a partially-constructed SA
 * should be deleted.
 */
static void
pfkey_resync_add(pfkeyreq_t *req, sadb_msg_t *retmsg)
{
	sadb_msg_t *samsg = req->pr_req;

	ssh_free(retmsg);

	if (req->pr_context != NULL)
		send_delete((rl_act_t *)req->pr_context,
		    samsg->sadb_msg_satype, "resync_add");

	ssh_free(req);
	ssh_free(samsg);
}

/*
 * Called to inform the peer that a partially-constructed SA
 * should be deleted.
 */
static void
handle_delete_reply(pfkeyreq_t *req, sadb_msg_t *retmsg)
{
	sadb_msg_t *samsg = req->pr_req;

	if (retmsg->sadb_msg_errno != 0) {
		pfkey_analyze_error(req, retmsg);
		return;
	}
	ssh_free(retmsg);

	if (req->pr_context != NULL)
		send_delete((rl_act_t *)req->pr_context,
		    samsg->sadb_msg_satype, "delete_reply");

	ssh_free(req);
	ssh_free(samsg);
}

/*
 * Called when we fail to update an SA locally to add a key; deletes the
 * half-constructed SA and notifies the peer when that's complete.
 */
static void
pfkey_resync_update(pfkeyreq_t *req, sadb_msg_t *retmsg)
{
	sadb_msg_t *reqmsg = req->pr_req;
	uint64_t *endmsg, *origend, *key;
	parsedmsg_t pmsg;

	/*
	 * We know the update failed.  retmsg is small and useless, so
	 * free it.  Turn req into a request to delete the SA - keep the
	 * selectors, toss the keying material.
	 */

	ssh_free(retmsg);

	/*
	 * Assumption: Keys occur after all extensions required for a
	 * DELETE (ordering is enforced in marshall_samsg() above).
	 */

	origend = ((uint64_t *)reqmsg) + reqmsg->sadb_msg_len;
	endmsg = origend;

	if (extract_exts(reqmsg, &pmsg, 1, SADB_EXT_KEY_ENCRYPT)) {
		key = (uint64_t *)pmsg.pmsg_exts[SADB_EXT_KEY_ENCRYPT];

		if (key < endmsg)
			endmsg = key;
	}

	if (extract_exts(reqmsg, &pmsg, 1, SADB_EXT_KEY_AUTH)) {
		key = (uint64_t *)pmsg.pmsg_exts[SADB_EXT_KEY_AUTH];

		if (key < endmsg)
			endmsg = key;

	}

	if (endmsg != origend) {
		reqmsg->sadb_msg_len = (endmsg - (uint64_t *)reqmsg);
		/*
		 * Burn keying material.
		 */
		(void) memset(endmsg, 0,
		    sizeof (uint64_t) * (origend - endmsg));
	}

	req->pr_handler = handle_delete_reply;
	reqmsg->sadb_msg_type = SADB_DELETE;
	reqmsg->sadb_msg_errno = 0;
	reqmsg->sadb_msg_seq = 0; /* auto-assign */

	pfkey_request(req);

}

/*
 * Deal with error conditions, log messages if appropriate.
 * This function needs to sshfree() the request and returned message, or
 * call something that does, or requeue the request.
 */
static void
pfkey_analyze_error(pfkeyreq_t *req, sadb_msg_t *retmsg)
{
	int error = 0;
	sadb_msg_t *samsg = req->pr_req;
	const char *keytype = "encryption";

	PRTDBG(D_PFKEY, ("PF_KEY error:\n"
	    "\tSADB msg: message type %d (%s), SA type %d (%s),\n"
	    "\terror code %d (%s), diag code %d (%s)",
	    retmsg->sadb_msg_type, pfkey_type(retmsg->sadb_msg_type),
	    retmsg->sadb_msg_satype, pfkey_satype(retmsg->sadb_msg_satype),
	    retmsg->sadb_msg_errno, strerror(retmsg->sadb_msg_errno),
	    retmsg->sadb_x_msg_diagnostic,
	    keysock_diag(retmsg->sadb_x_msg_diagnostic)));

	if (retmsg->sadb_x_msg_diagnostic != 0) {
		/*
		 * Diagnostics are listed in the order they appear in
		 * pfkeyv2.h . Most diagnostics indicate an error condidtion
		 * which the kernel does not like, some conditions can be
		 * recovered, others ignored. If something really bad is
		 * heppening, exit and let smf(5) restart in.iked . Any
		 * diagnostic not listed here should probably never occur, if
		 * it does, treat it as fatal.
		 */
		switch (retmsg->sadb_x_msg_diagnostic) {
		/* PF_KEY errors are fatal. */
		case SADB_X_DIAGNOSTIC_UNKNOWN_MSG:
		case SADB_X_DIAGNOSTIC_UNKNOWN_EXT:
			error = SERVICE_FATAL;
			break;

		/* Exit in.iked when debugging. */
		case SADB_X_DIAGNOSTIC_BAD_EXTLEN:
		case SADB_X_DIAGNOSTIC_UNKNOWN_SATYPE:
		case SADB_X_DIAGNOSTIC_SATYPE_NEEDED:
		case SADB_X_DIAGNOSTIC_NO_SADBS:
		/* These are address errors - not fatal. */
		case SADB_X_DIAGNOSTIC_NO_EXT:
		case SADB_X_DIAGNOSTIC_BAD_SRC_AF:
		case SADB_X_DIAGNOSTIC_BAD_DST_AF:
		case SADB_X_DIAGNOSTIC_BAD_INNER_SRC_AF:
		case SADB_X_DIAGNOSTIC_AF_MISMATCH:
		case SADB_X_DIAGNOSTIC_BAD_SRC:
		case SADB_X_DIAGNOSTIC_BAD_DST:
			error = DEBUG_FATAL;
			break;

		/* Lifetime errors - not fatal. */
		case SADB_X_DIAGNOSTIC_ALLOC_HSERR:
		case SADB_X_DIAGNOSTIC_BYTES_HSERR:
		case SADB_X_DIAGNOSTIC_ADDTIME_HSERR:
		case SADB_X_DIAGNOSTIC_USETIME_HSERR:
			break;

		/* PF_KEY extension errors. */
		case SADB_X_DIAGNOSTIC_MISSING_SRC:
		case SADB_X_DIAGNOSTIC_MISSING_DST:
		case SADB_X_DIAGNOSTIC_MISSING_SA:
		case SADB_X_DIAGNOSTIC_MISSING_EKEY:
		case SADB_X_DIAGNOSTIC_MISSING_AKEY:
		case SADB_X_DIAGNOSTIC_MISSING_RANGE:

		case SADB_X_DIAGNOSTIC_DUPLICATE_SRC:
		case SADB_X_DIAGNOSTIC_DUPLICATE_DST:
		case SADB_X_DIAGNOSTIC_DUPLICATE_SA:
		case SADB_X_DIAGNOSTIC_DUPLICATE_EKEY:
		case SADB_X_DIAGNOSTIC_DUPLICATE_AKEY:
		case SADB_X_DIAGNOSTIC_DUPLICATE_RANGE:

		case SADB_X_DIAGNOSTIC_MALFORMED_SRC:
		case SADB_X_DIAGNOSTIC_MALFORMED_DST:
		case SADB_X_DIAGNOSTIC_MALFORMED_SA:
			error = DEBUG_FATAL;
			break;

		case SADB_X_DIAGNOSTIC_MALFORMED_AKEY:
			keytype = "authentication";
			/* FALLTHROUGH */
		case SADB_X_DIAGNOSTIC_MALFORMED_EKEY:
			/* keytype defaults to "encryption" above */
			if (retmsg->sadb_msg_type == SADB_ADD) {
				PRTDBG(D_PFKEY, ("Malformed %s key on PF_KEY "
				    "ADD", keytype));
				pfkey_resync_add(req, retmsg);
				return;
			}
			if (retmsg->sadb_msg_type == SADB_UPDATE) {
				PRTDBG(D_PFKEY, ("Malformed %s key on PF_KEY "
				    "UPDATE", keytype));
				pfkey_resync_update(req, retmsg);
				return;
			}
			error = SERVICE_FATAL;
			break;

		case SADB_X_DIAGNOSTIC_MALFORMED_RANGE:
		case SADB_X_DIAGNOSTIC_EKEY_PRESENT:
			if (retmsg->sadb_msg_satype == SADB_SATYPE_AH) {
				PRTDBG(D_PFKEY, ("Encryption key in AH SA."));
				error = DEBUG_FATAL;
				break;
			}
			/* FALLTHROUGH */
		case SADB_X_DIAGNOSTIC_AKEY_PRESENT:
			if (retmsg->sadb_msg_type == SADB_UPDATE) {
				PRTDBG(D_PFKEY,
				    ("Discarding retransmitted SADB_UPDATE."));
				break;
			} else {
				PRTDBG(D_PFKEY, ("Keying material in UPDATE"));
				break;
			}
		case SADB_X_DIAGNOSTIC_PROP_PRESENT:
		case SADB_X_DIAGNOSTIC_SUPP_PRESENT:

		case SADB_X_DIAGNOSTIC_BAD_AALG:
		case SADB_X_DIAGNOSTIC_BAD_EALG:
			error = DEBUG_FATAL;
			break;

		case SADB_X_DIAGNOSTIC_BAD_SAFLAGS:
		case SADB_X_DIAGNOSTIC_BAD_SASTATE:
			error = SERVICE_FATAL;
			break;

		/* These are not fatal. */
		case SADB_X_DIAGNOSTIC_BAD_AKEYBITS:
		case SADB_X_DIAGNOSTIC_BAD_EKEYBITS:
		case SADB_X_DIAGNOSTIC_ENCR_NOTSUPP:
		case SADB_X_DIAGNOSTIC_WEAK_EKEY:
		case SADB_X_DIAGNOSTIC_WEAK_AKEY:
		case SADB_X_DIAGNOSTIC_DUPLICATE_KMP:
		case SADB_X_DIAGNOSTIC_DUPLICATE_KMC:

		case SADB_X_DIAGNOSTIC_MISSING_NATT_LOC:
		case SADB_X_DIAGNOSTIC_MISSING_NATT_REM:
		case SADB_X_DIAGNOSTIC_DUPLICATE_NATT_LOC:
		case SADB_X_DIAGNOSTIC_DUPLICATE_NATT_REM:
			break;

		case SADB_X_DIAGNOSTIC_MALFORMED_NATT_LOC:
			/*
			 * This can happen with broken peers who don't
			 * send NAT-OA when they should.  Don't halt.
			 */
			PRTDBG(D_PFKEY,
			    ("\tMalformed local NAT address on ADD/UPDATE.\n"
			"\tProbably peer not using NAT-OA on transport mode."));
			break;
		case SADB_X_DIAGNOSTIC_MALFORMED_NATT_REM:
			/*
			 * This can happen with broken peers who don't
			 * send NAT-OA when they should.  Don't halt.
			 */
			PRTDBG(D_PFKEY,
			    ("\tMalformed remote NAT address on ADD/UPDATE.\n"
			"\tProbably peer not using NAT-OA on transport mode."));
			break;

		case SADB_X_DIAGNOSTIC_MISSING_INNER_SRC:
		case SADB_X_DIAGNOSTIC_MISSING_INNER_DST:

		case SADB_X_DIAGNOSTIC_DUPLICATE_INNER_SRC:
		case SADB_X_DIAGNOSTIC_DUPLICATE_INNER_DST:

		case SADB_X_DIAGNOSTIC_MALFORMED_INNER_SRC:
		case SADB_X_DIAGNOSTIC_MALFORMED_INNER_DST:

		case SADB_X_DIAGNOSTIC_PREFIX_INNER_SRC:
		case SADB_X_DIAGNOSTIC_PREFIX_INNER_DST:
		case SADB_X_DIAGNOSTIC_BAD_INNER_DST_AF:
		case SADB_X_DIAGNOSTIC_INNER_AF_MISMATCH:

		case SADB_X_DIAGNOSTIC_BAD_NATT_REM_AF:
		case SADB_X_DIAGNOSTIC_BAD_NATT_LOC_AF:
		case SADB_X_DIAGNOSTIC_PROTO_MISMATCH:
		case SADB_X_DIAGNOSTIC_INNER_PROTO_MISMATCH:
		case SADB_X_DIAGNOSTIC_DUAL_PORT_SETS:
			break;

		case SADB_X_DIAGNOSTIC_PAIR_INAPPROPRIATE:
			PRTDBG(D_PFKEY, ("Inappropriate pairing attempt."));
			break;
		case SADB_X_DIAGNOSTIC_PAIR_ADD_MISMATCH:
			PRTDBG(D_PFKEY, ("Incomplete addresses don't work with "
			    "paired SA's"));
			break;
		case SADB_X_DIAGNOSTIC_PAIR_ALREADY:
			PRTDBG(D_PFKEY, ("SA already paired."));
			break;
		case SADB_X_DIAGNOSTIC_PAIR_SA_NOTFOUND:
			PRTDBG(D_PFKEY, ("Pairing failed, Pair SA not found."));
			break;

		case SADB_X_DIAGNOSTIC_BAD_SA_DIRECTION:
		case SADB_X_DIAGNOSTIC_SA_NOTFOUND:
		case SADB_X_DIAGNOSTIC_SA_EXPIRED:
		case SADB_X_DIAGNOSTIC_BAD_CTX:
		case SADB_X_DIAGNOSTIC_INVALID_REPLAY:
		case SADB_X_DIAGNOSTIC_MISSING_LIFETIME:
			error = DEBUG_FATAL;
			break;

		default:
			error = SERVICE_FATAL;
		}
	} else {
		switch (retmsg->sadb_msg_errno) {
		case EOPNOTSUPP:
			PRTDBG(D_PFKEY,
			    ("PF_KEY message contained unsupported extension"));
			break;
		case EHOSTUNREACH:
			/*
			 * This only happens on Trusted Extensions systems
			 * where labels in an SADB_{ADD,UPDATE} mismatch
			 * what the trusted networking databases in-kernel
			 * have to say.
			 *
			 * Treat like malformed keying material (see above)
			 * and tell the peer to lose the SA.
			 */
			if (retmsg->sadb_msg_type == SADB_ADD) {
				PRTDBG(D_PFKEY | D_LABEL, ("SADB_ADD SA label "
				    "mismatches trusted networking tables.\n"));
				pfkey_resync_add(req, retmsg);
				return;
			}
			if (retmsg->sadb_msg_type == SADB_UPDATE) {
				PRTDBG(D_PFKEY | D_LABEL, ("SADB_UPDATE SA "
				    "label mismatches trusted networking "
				    "tables.\n"));
				pfkey_resync_update(req, retmsg);
				return;
			}
			error = SERVICE_FATAL;
			break;
		case EEXIST:
			if (retmsg->sadb_msg_type == SADB_ADD) {
				PRTDBG(D_PFKEY,
				    ("SA exists or PF_KEY retransmit"));
				break;
			} else if (retmsg->sadb_msg_type == SADB_GETSPI) {
				PRTDBG(D_PFKEY,
				    ("SPI exists or PF_KEY retransmit"));
				break;
			}
			/* FALLTHROUGH */
		default:
			/*
			 * This will catch ENOMEM if the kernel ever manages to
			 * send this up!.
			 */
			error = SERVICE_FATAL;
		}
	}

	if (error) {
		/*
		 * Call ipsecutil_exit(), this may return if running under
		 * smf(5).
		 */
		ipsecutil_exit(error, my_fmri, debugfile,
		    gettext("Unexpected PF_KEY error:\n"
		    "\tmessage %d error code %d (%s), diag code %d (%s)"),
		    retmsg->sadb_msg_type,
		    retmsg->sadb_msg_errno, strerror(retmsg->sadb_msg_errno),
		    retmsg->sadb_x_msg_diagnostic,
		    keysock_diag(retmsg->sadb_x_msg_diagnostic));
	}

	/*
	 * Paranoia: Shred keys before anyone else sees them.
	 */
	(void) memset(samsg, 0, SADB_64TO8(samsg->sadb_msg_len));
	ssh_free(samsg);
	ssh_free(retmsg);
	ssh_free(req);
}

/*
 * Send RESPONDER-LIFETIME notification message (IPSEC DOI) to the peer.
 */
static void
send_rl(rl_act_t *rl, int protocol_id)
{
	ike_server_t *ikesrv;
	SshBuffer buffer = NULL;
	SshIkeErrorCode ssh_rc;
	char byte_str[BYTE_STR_SIZE]; /* byte lifetime string representation */
	char secs_str[SECS_STR_SIZE]; /* buffer for seconds representation */
	char secs_str_full[64];
	char kb_str_full[64];

	/* Nothing to send */
	if ((rl->my_lifetime_secs == 0) && (rl->my_lifetime_kb == 0)) {
		ssh_free(rl);
		return;
	}

	ikesrv = get_server_context(&rl->my_local);
	if (ikesrv == NULL) {
		ssh_free(rl);
		return;
	}

	buffer = ssh_buffer_allocate();
	if (buffer != NULL) {
		bzero(secs_str_full, sizeof (secs_str_full));
		bzero(kb_str_full, sizeof (kb_str_full));

		if (rl->my_lifetime_secs != 0) {
			if (ssh_ike_encode_data_attribute_int(buffer,
			    IPSEC_CLASSES_SA_LIFE_TYPE, TRUE,
			    IPSEC_VALUES_LIFE_TYPE_SECONDS, 0)
			    == (unsigned)-1) {
				PRTDBG(D_OP,
				    ("Data attribute encode #1 failed."));
				return;
			}
			if (ssh_ike_encode_data_attribute_int(buffer,
			    IPSEC_CLASSES_SA_LIFE_DURATION, FALSE,
			    rl->my_lifetime_secs, 0) == (unsigned)-1) {
				PRTDBG(D_OP,
				    ("Data attribute encode #2 failed."));
				return;
			}
			(void) ssh_snprintf(secs_str_full,
			    sizeof (secs_str_full),
			    "%" PRIu64 " s%s", rl->my_lifetime_secs,
			    secs2out(rl->my_lifetime_secs, secs_str,
			    sizeof (secs_str), SPC_BEGIN));
		}
		if (rl->my_lifetime_kb != 0) {
			if (ssh_ike_encode_data_attribute_int(buffer,
			    IPSEC_CLASSES_SA_LIFE_TYPE, TRUE,
			    IPSEC_VALUES_LIFE_TYPE_KILOBYTES, 0)
			    == (unsigned)-1) {
				PRTDBG(D_OP,
				    ("Data attribute encode #3 failed."));
				return;
			}
			if (ssh_ike_encode_data_attribute_int(buffer,
			    IPSEC_CLASSES_SA_LIFE_DURATION, FALSE,
			    rl->my_lifetime_kb, 0) == (unsigned)-1) {
				PRTDBG(D_OP,
				    ("Data attribute encode #4 failed."));
				return;
			}
			(void) ssh_snprintf(kb_str_full, sizeof (kb_str_full),
			    "%" PRIu64 " KB%s",
			    rl->my_lifetime_kb,
			    bytecnt2out(rl->my_lifetime_kb << 10,
			    byte_str, sizeof (byte_str), SPC_BEGIN));
		}

		PRTDBG(D_OP, ("Sending RESPONDER-LIFETIME of %s%s%s for SPI: "
		    "0x%x", secs_str_full,
		    strlen(secs_str_full) > 0 && strlen(kb_str_full) > 0
		    ? ", " : "", kb_str_full, ntohl(rl->my_spi)));

		if ((ssh_rc = ssh_ike_connect_notify(ikesrv->ikesrv_ctx,
		    rl->my_neg, NULL, NULL, SSH_IKE_NOTIFY_FLAGS_WANT_ISAKMP_SA,
		    SSH_IKE_DOI_IPSEC, protocol_id, (uint8_t *)&rl->my_spi,
		    sizeof (uint32_t),
		    SSH_IKE_NOTIFY_MESSAGE_RESPONDER_LIFETIME,
		    ssh_buffer_ptr(buffer), ssh_buffer_len(buffer))) !=
		    SSH_IKE_ERROR_OK) {
			PRTDBG(D_OP, ("Responder-Lifetime notify failed, "
			    "code %d (%s).", ssh_rc,
			    ike_connect_error_to_string(ssh_rc)));
		} else {
			PRTDBG(D_OP, ("Responder-Lifetime notify sent."));
		}

		ssh_buffer_free(buffer);
	}

	ssh_free(rl);
}

static void
send_delete(rl_act_t *rl, int protocol_id, const char *tag)
{
	ike_server_t *ikesrv;
	SshIkeErrorCode ssh_rc;
	uint8_t *spiptr = (uint8_t *)&rl->my_spi;

	PRTDBG(D_OP, ("Sending DELETE notification for %s", tag));

	ikesrv = get_server_context(&rl->my_local);

	if (ikesrv == NULL) {
		PRTDBG(D_OP, ("DELETE: in.iked context lookup failed"));
	} else {

		ssh_rc = ssh_ike_connect_delete(ikesrv->ikesrv_ctx,
		    rl->my_neg, NULL, NULL,
		    SSH_IKE_DELETE_FLAGS_WANT_ISAKMP_SA,
		    SSH_IKE_DOI_IPSEC, protocol_id, 1, &spiptr,
		    sizeof (uint32_t));

		if (ssh_rc != SSH_IKE_ERROR_OK) {
			PRTDBG(D_OP, ("DELETE notify failed, code %d (%s).\n",
			    ssh_rc, ike_connect_error_to_string(ssh_rc)));
		} else {
			PRTDBG(D_OP, ("DELETE notify succeeded!\n"));
		}
	}

	ssh_free(rl);
}

/*
 * Callback on completion of an UPDATE PF_KEY request.
 */
static void
update_complete(pfkeyreq_t *req, sadb_msg_t *retmsg)
{
	sadb_msg_t *samsg = req->pr_req;

	if (retmsg->sadb_msg_type != SADB_UPDATE ||
	    retmsg->sadb_msg_satype != samsg->sadb_msg_satype)
		EXIT_FATAL("Kernel bug or duplicate Key management daemon. "
		    "(PF_KEY UPDATE)");

	if (retmsg->sadb_msg_errno != 0) {
		if (retmsg->sadb_msg_errno != ESRCH) {
			pfkey_analyze_error(req, retmsg);
			return;
		}
		/*
		 * Handle ESRCH case by changing message to an
		 * ADD, then put it back on the queue.
		 */
		samsg->sadb_msg_type = SADB_ADD;

		req->pr_handler = add_complete;
		req->pr_context = NULL;

		ssh_free(retmsg);

		DUMP_PFKEY(samsg);
		pfkey_request(req);
		return;
	}

	if (req->pr_context != NULL)
		send_rl((rl_act_t *)req->pr_context, samsg->sadb_msg_satype);

	/*
	 * Paranoia: Shred keys before anyone else sees them.
	 */
	(void) memset(samsg, 0, SADB_64TO8(samsg->sadb_msg_len));
	ssh_free(samsg);
	ssh_free(retmsg);
	ssh_free(req);
}

/*
 * Callback on completion of an ADD PF_KEY request.
 */
static void
add_complete(pfkeyreq_t *req, sadb_msg_t *retmsg)
{
	sadb_msg_t *samsg = req->pr_req;

	if (retmsg->sadb_msg_errno != 0) {
		pfkey_analyze_error(req, retmsg);
		return;
	}

	if (retmsg->sadb_msg_type != SADB_ADD ||
	    retmsg->sadb_msg_satype != samsg->sadb_msg_satype) {
		EXIT_FATAL("Kernel bug or duplicate Key management daemon. "
		    "(PF_KEY ADD2)");
	}

	/* Otherwise I sucessfully added it. */

	if (req->pr_context != NULL)
		send_rl((rl_act_t *)req->pr_context, samsg->sadb_msg_satype);

	/*
	 * Paranoia: Shred keys before anyone else sees them.
	 */
	(void) memset(samsg, 0, SADB_64TO8(samsg->sadb_msg_len));
	ssh_free(samsg);
	ssh_free(retmsg);
	ssh_free(req);
}

/*
 * Add IPsec security associations after a Quick Mode negotiation has
 * finished.
 */
/* ARGSUSED */
void
our_sa_handler(SshIkeNegotiation negotiation, SshIkePMPhaseQm pm_info,
    int num_sas, SshIkeIpsecSelectedSA sas, SshIkeIpsecKeymat keymat,
    void *cookie)
{
	int i, j;

	/*
	 * Add/update the SAs.
	 */

	for (i = 0; i < num_sas; i++) {
		for (j = 0; j < sas[i].number_of_protocols; j++) {
			add_new_sa(negotiation, pm_info, keymat,
			    sas[i].protocols + j);
		}
	}

	/*
	 * Perhaps we could use the pm_info to our advantage.  Take
	 * the lifetime information and update pm_info accordingly.
	 * Then, in ssh_policy_qm_sa_freed(), restart a negotiation.
	 */
}

/*
 * Queue up an SADB_GETSPI request; arrange to call back sw_done when
 * it arrives..
 */
void
getspi(spiwait_t *swp, struct sockaddr_storage *local,
    struct sockaddr_storage *remote, uint8_t satype, uint8_t proto,
    uint8_t *spiptr, void (*sw_done)(spiwait_t *), void *context)
{
	sadb_msg_t *msg;
	sadb_address_t *srcaddr, *dstaddr;
	sadb_spirange_t *range;
	int salen, len;

	PRTDBG(D_PFKEY, ("SADB GETSPI type == \"%s\"", rparsesatype(satype)));
	PRTDBG(D_PFKEY, ("  local %s", sap(local)));
	PRTDBG(D_PFKEY, ("  remote %s", sap(remote)));

	switch (local->ss_family) {
	case AF_INET:
		salen = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		salen = sizeof (struct sockaddr_in6);
		break;
	default:
		salen = sizeof (struct sockaddr);
		break;
	}

	len = sizeof (*msg) +
	    2 * sizeof (*srcaddr) + 2 * roundup(salen, 8) + sizeof (*range);
	/*
	 * Construct SADB_GETSPI message.
	 */
	msg = (sadb_msg_t *)ssh_malloc(len);
	if (msg == NULL) {
		PRTDBG(D_PFKEY, ("SADB GETSPI: out of memory"));
		return;
	}

	msg->sadb_msg_version = PF_KEY_V2;
	msg->sadb_msg_type = SADB_GETSPI;
	msg->sadb_msg_errno = 0;
	msg->sadb_msg_satype = satype;
	msg->sadb_msg_len = SADB_8TO64(sizeof (*msg) +
	    2 * sizeof (*srcaddr) + 2 * roundup(salen, 8) + sizeof (*range));
	msg->sadb_msg_reserved = 0;
	msg->sadb_msg_pid = getpid();
	msg->sadb_msg_seq = 0;

	/*
	 * In IKE, we pick the SPI for the inbound (dst == us) SA, so
	 * we bind local to DST.
	 */
	srcaddr = (sadb_address_t *)(msg + 1);
	srcaddr->sadb_address_len = SADB_8TO64(sizeof (*srcaddr) +
	    roundup(salen, 8));
	srcaddr->sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
	srcaddr->sadb_address_proto = proto;
	srcaddr->sadb_address_prefixlen = 0;
	srcaddr->sadb_address_reserved = 0;
	(void) memcpy(srcaddr + 1, remote, salen);

	dstaddr = (sadb_address_t *)
	    ((uint64_t *)srcaddr) + srcaddr->sadb_address_len;
	dstaddr->sadb_address_len = SADB_8TO64(sizeof (*dstaddr) +
	    roundup(salen, 8));
	dstaddr->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	dstaddr->sadb_address_proto = proto;
	dstaddr->sadb_address_prefixlen = 0;
	dstaddr->sadb_address_reserved = 0;
	(void) memcpy(dstaddr + 1, local, salen);

	range = (sadb_spirange_t *)
	    (((uint64_t *)dstaddr) + dstaddr->sadb_address_len);
	range->sadb_spirange_len = SADB_8TO64(sizeof (*range));
	range->sadb_spirange_exttype = SADB_EXT_SPIRANGE;
	range->sadb_spirange_reserved = 0;
	range->sadb_spirange_min = 0;
	range->sadb_spirange_max = (uint32_t)-1;

	swp->sw_req.pr_handler = gotspi;
	swp->sw_req.pr_context = swp;
	swp->sw_req.pr_req = msg;
	swp->sw_spi_ptr = spiptr;
	swp->sw_done = sw_done;
	swp->sw_context = context;

	DUMP_PFKEY(msg);
	pfkey_request(&swp->sw_req);
}

static void
gotspi(pfkeyreq_t *req, sadb_msg_t *msg)
{
	spiwait_t *swp = req->pr_context;
	sadb_sa_t *assoc;

	/*
	 * We got a sucsessful SADB_GETSPI back.
	 */
	assoc = (sadb_sa_t *)(msg + 1);
	while (assoc != NULL) {
		if (assoc->sadb_sa_exttype == SADB_EXT_SA)
			break;	/* while loop. */
		assoc = (sadb_sa_t *)((uint64_t *)assoc + assoc->sadb_sa_len);
		if ((uintptr_t)assoc > (uintptr_t)((uint64_t *)msg +
		    msg->sadb_msg_len))
			assoc = NULL;
	}

	if (assoc == NULL)
		EXIT_FATAL("KERNEL ERROR, SADB GETSPI was bad.");

	if (in_cluster_mode) {
		(void) sendto(cluster_socket, msg,
		    SADB_64TO8(msg->sadb_msg_len), 0,
		    (struct sockaddr *)&cli_addr,
		    sizeof (cli_addr));
	}

	/*
	 * Assume PF_KEY give it to us in network order, and the library likes
	 * network order.  (So we don't need to convert.)
	 */
	(void) memcpy(swp->sw_spi_ptr, &assoc->sadb_sa_spi,
	    sizeof (assoc->sadb_sa_spi));
	ssh_free(msg);
	ssh_free(swp->sw_req.pr_req);
	(*swp->sw_done)(swp);
}

/*
 * The passed in parsedmsg_t looks like this (see defs.h):
 *
 * {
 *	*pmsg_next
 *	*pmsg_samsg
 *	*pmsg_exts[0][1][2][3].....[SADB_EXT_MAX + 2]
 *	*pmsg_sss  (struct sockaddr_storage *)
 *	*pmsg_dss  (struct sockaddr_storage *)
 *	*pmsg_psss (struct sockaddr_storage *)
 *	*pmsg_pdss (struct sockaddr_storage *)
 *	*pmsg_nlss (struct sockaddr_storage *)
 *	*pmsg_nrss (struct sockaddr_storage *)
 * } parsedmsg_t;
 *
 * This function parses through the whole samsg looking for valid PF_KEY
 * extensions. Each extension type found is saved in the pmsg_exts array.
 * As the parsedmsg_t is initialised as zero's when entering the function, it's
 * easy to check later to see which extensions exist in the samsg by
 * checking for NULL.
 *
 * Some extensions will have a sockaddr_storage associated with the type
 * EG: SADB_EXT_ADDRESS_SRC, in these cases a pointer to the appropriate
 * structure in samsg is set in the parsedmsg_t.
 *
 * After parsing the whole samsg, the optional arguments (which is a list
 * of required extensions) are checked for in the parsedmsg_t. If all of the
 * required extensions are valid then the function returns B_TRUE.
 *
 * Even if the required extensions are not in the samsg (and the function
 * returns B_FALSE) the pmsg->pmsg_exts array will still contain the headers
 * that were in the samsg.
 *
 * Assume the kernel knows what it's doing with messages that get passed up.
 * The variable arguments are a list of ints with SADB_EXT_* values.
 */
static boolean_t
extract_exts(sadb_msg_t *samsg, parsedmsg_t *pmsg, int numexts, ...)
{
	sadb_ext_t *ext;
	sadb_ext_t **exts = pmsg->pmsg_exts;
	int current_ext;
	va_list ap;
	boolean_t rc = B_TRUE;

	(void) memset(pmsg, 0, sizeof (parsedmsg_t));

	ext = (sadb_ext_t *)(samsg + 1);
	pmsg->pmsg_samsg = samsg;

	do {
		exts[ext->sadb_ext_type] = ext;
		if (ext->sadb_ext_type == SADB_EXT_ADDRESS_SRC)
			pmsg->pmsg_sss = (struct sockaddr_storage *)
			    (((sadb_address_t *)ext) + 1);
		if (ext->sadb_ext_type == SADB_EXT_ADDRESS_DST)
			pmsg->pmsg_dss = (struct sockaddr_storage *)
			    (((sadb_address_t *)ext) + 1);
		if (ext->sadb_ext_type == SADB_X_EXT_ADDRESS_INNER_SRC)
			pmsg->pmsg_psss = (struct sockaddr_storage *)
			    (((sadb_address_t *)ext) + 1);
		if (ext->sadb_ext_type == SADB_X_EXT_ADDRESS_INNER_DST)
			pmsg->pmsg_pdss = (struct sockaddr_storage *)
			    (((sadb_address_t *)ext) + 1);
		if (ext->sadb_ext_type == SADB_X_EXT_ADDRESS_NATT_REM)
			pmsg->pmsg_nrss = (struct sockaddr_storage *)
			    (((sadb_address_t *)ext) + 1);
		if (ext->sadb_ext_type == SADB_X_EXT_ADDRESS_NATT_LOC)
			pmsg->pmsg_nlss = (struct sockaddr_storage *)
			    (((sadb_address_t *)ext) + 1);

		ext = (sadb_ext_t *)(((uint64_t *)ext) + ext->sadb_ext_len);

	} while (((uint8_t *)ext) - ((uint8_t *)samsg) <
	    SADB_64TO8(samsg->sadb_msg_len));

	va_start(ap, numexts);
	while (numexts-- > 0) {
		current_ext = va_arg(ap, int);
		if (exts[current_ext] == NULL) {
			rc = B_FALSE;
			break;
		}
	}
	va_end(ap);

	return (rc);
}

void
free_pmsg(parsedmsg_t *pmsg)
{
	if (pmsg->pmsg_samsg != NULL)
		ssh_free(pmsg->pmsg_samsg);
	ssh_free(pmsg);
}

static void
init_addrext(sadb_address_t *addrext, uint_t type,
    struct sockaddr_storage *sa, uint_t salen)
{
	uint8_t *ptr;

	addrext->sadb_address_len =
	    SADB_8TO64(sizeof (*addrext) + roundup(salen, 8));
	addrext->sadb_address_exttype = type;
	addrext->sadb_address_proto = 0;
	addrext->sadb_address_prefixlen = 0;
	addrext->sadb_address_reserved = 0;
	ptr = (uint8_t *)(addrext + 1);
	(void) memcpy(ptr, sa, salen);
}

/*
 * Delete an SA.
 */
void
delete_assoc(uint32_t spi, struct sockaddr_storage *src,
    struct sockaddr_storage *dst, SshIkePayloadID sidpayload,
    SshIkePayloadID didpayload, uint8_t satype)
{
	sadb_msg_t *samsg;
	sadb_sa_t *assoc;
	sadb_address_t *dstext, *srcext;
	sadb_x_kmc_t *kmc;
	sadb_ident_t *sid, *did;
	uint64_t *end, *start;
	int salen;
	pid_t mypid = getpid();
	struct sockaddr_storage *sa;
	int len;
	pfkeyreq_t *req = ssh_malloc(sizeof (*req));

	PRTDBG(D_PFKEY, ("Deleting SA ..."));

	if (req == NULL) {
		PRTDBG(D_PFKEY, ("  Out of memory deleting SA"));
		return;
	}

	assert(src != NULL || dst != NULL);

	sa = (dst != NULL) ? dst : src;
	switch (sa->ss_family) {
	case AF_INET:
		salen = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		salen = sizeof (struct sockaddr_in6);
		break;
	default:
		salen = sizeof (struct sockaddr);
		break;
	}

	len = sizeof (*samsg);
	if (spi != 0)
		len += sizeof (*assoc);
	else {
		len += sizeof (*kmc);

		if (sidpayload != NULL) {
			sid = payloadid_to_pfkey(sidpayload, B_TRUE);
			if (sid != NULL)
				len += SADB_64TO8(sid->sadb_ident_len);
		} else {
			sid = NULL;
		}
		if (didpayload != NULL) {
			did = payloadid_to_pfkey(didpayload, B_FALSE);
			if (did != NULL)
				len += SADB_64TO8(did->sadb_ident_len);
		} else {
			did = NULL;
		}
	}
	if (dst != NULL)
		len += sizeof (*dstext) + roundup(salen, 8);
	if (src != NULL)
		len += sizeof (*srcext) + roundup(salen, 8);

	start = ssh_malloc(len);
	if (start == NULL) {
		ssh_free(req);
		PRTDBG(D_PFKEY, ("  Out of memory deleting SA"));
		return;
	}
	samsg = (sadb_msg_t *)start;
	samsg->sadb_msg_version = PF_KEY_V2;
	samsg->sadb_msg_type = SADB_DELETE;
	samsg->sadb_msg_errno = 0;
	samsg->sadb_msg_satype = satype;
	samsg->sadb_msg_reserved = 0;
	samsg->sadb_msg_pid = mypid;
	samsg->sadb_msg_seq = 0;
	end = (uint64_t *)(samsg + 1);

	if (spi != 0) {
		assoc = (sadb_sa_t *)end;
		assoc->sadb_sa_len = SADB_8TO64(sizeof (*assoc));
		assoc->sadb_sa_exttype = SADB_EXT_SA;
		assoc->sadb_sa_spi = spi;
		assoc->sadb_sa_replay = 0;
		assoc->sadb_sa_state = SADB_SASTATE_MATURE;
		assoc->sadb_sa_auth = 0;
		assoc->sadb_sa_encrypt = 0;
		assoc->sadb_sa_flags = 0;
		end = (uint64_t *)(assoc + 1);
	} else {
		kmc = (sadb_x_kmc_t *)end;
		kmc->sadb_x_kmc_exttype = SADB_X_EXT_KM_COOKIE;
		kmc->sadb_x_kmc_len = SADB_8TO64(sizeof (*kmc));
		kmc->sadb_x_kmc_proto = SADB_X_KMP_IKE;
		/*
		 * NOTE: Someday, may wish to assign a cookie value here for
		 * rule-nuking-driven SA-nuking.
		 */
		kmc->sadb_x_kmc_cookie = 0;
		kmc->sadb_x_kmc_reserved = 0;
		end = (uint64_t *)(kmc + 1);

		if (sid != NULL) {
			(void) memcpy(end, sid,
			    SADB_64TO8(sid->sadb_ident_len));
			end += sid->sadb_ident_len;
			ssh_free(sid);
		}

		if (did != NULL) {
			(void) memcpy(end, did,
			    SADB_64TO8(did->sadb_ident_len));
			end += did->sadb_ident_len;
			ssh_free(did);
		}
	}

	if (dst != NULL) {
		dstext = (sadb_address_t *)end;
		init_addrext(dstext, SADB_EXT_ADDRESS_DST, dst, salen);
		end = (uint64_t *)dstext + dstext->sadb_address_len;
	}

	if (src != NULL) {
		srcext = (sadb_address_t *)end;
		init_addrext(srcext, SADB_EXT_ADDRESS_SRC, src, salen);
		end = (uint64_t *)srcext + srcext->sadb_address_len;
	}

	samsg->sadb_msg_len = end - start;

	req->pr_handler = delete_finish;
	req->pr_context = NULL;
	req->pr_req = samsg;
	DUMP_PFKEY(samsg);
	pfkey_request(req);
}

static void
delete_finish(pfkeyreq_t *req, sadb_msg_t *rep)
{
	if (rep->sadb_msg_errno != 0 && rep->sadb_msg_errno != ESRCH) {
		EXIT_FATAL3("PF_KEY DELETE error: %s; Diagnostic %s",
		    strerror(rep->sadb_msg_errno),
		    keysock_diag(rep->sadb_x_msg_diagnostic));
	}
	ssh_free(rep);
	ssh_free(req->pr_req);
	ssh_free(req);
}

static void update_life_finish(pfkeyreq_t *, sadb_msg_t *);

/*
 * Update an SA's lifetime, new lifetimes have already been sanity checked.
 */
void
update_assoc_lifetime(uint32_t spi, uint64_t bytes, uint64_t s_bytes,
    uint64_t secs, uint64_t s_secs, struct sockaddr_storage *sa, uint8_t satype)
{
	uint64_t *ptr;
	sadb_msg_t *samsg;
	sadb_sa_t *assoc;
	sadb_lifetime_t *lt, *slt;
	sadb_address_t *dst, *src;
	int salen, len;
	ike_server_t *ikesrv;
	pid_t mypid = getpid();
	pfkeyreq_t *req = ssh_malloc(sizeof (*req));

	PRTDBG(D_OP, ("Updating %s SPI: 0x%x SA lifetime....",
	    rparsesatype(satype), spi));

	if (req == NULL) {
		PRTDBG(D_PFKEY, ("  Out of memory updating SA lifetime."));
		return;
	}

	/*
	 * Even though we've updated the QM pm_info which should do the right
	 * thing, attempt to update the SA anyway, just in case we get a very
	 * late RESPONDER-LIFETIME notification.
	 *
	 * Since this is a QM notification, we're under the Phase 1 SA's
	 * protection, and don't have to worry about this coming from out
	 * of the blue.
	 *
	 * NOTE: unlike add_new_sa(), we assume the RESPONDER-LIFETIME here
	 * is always to be used.  This could be bad.
	 */

	switch (sa->ss_family) {
	case AF_INET:
		salen = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		salen = sizeof (struct sockaddr_in6);
		break;
	default:
		salen = sizeof (struct sockaddr);
		break;
	}

	len = sizeof (*samsg) + sizeof (*assoc) + sizeof (*lt) + sizeof (*slt) +
	    2 * (sizeof (*dst) + roundup(salen, 8));

	samsg = (sadb_msg_t *)ssh_malloc(len);
	if (samsg == NULL) {
		ssh_free(req);
		PRTDBG(D_PFKEY, ("  Out of memory updating SA lifetime."));
		return;
	}

	ikesrv = get_server_context(sa);

	samsg->sadb_msg_version = PF_KEY_V2;
	samsg->sadb_msg_type = SADB_X_UPDATEPAIR;
	samsg->sadb_msg_errno = 0;
	samsg->sadb_msg_satype = satype;
	samsg->sadb_msg_len = SADB_8TO64(len);
	samsg->sadb_msg_reserved = 0;
	samsg->sadb_msg_seq = 0;
	samsg->sadb_msg_pid = mypid;

	lt = (sadb_lifetime_t *)(samsg + 1);
	lt->sadb_lifetime_len = SADB_8TO64(sizeof (*lt));
	lt->sadb_lifetime_exttype = SADB_EXT_LIFETIME_HARD;
	lt->sadb_lifetime_allocations = 0;
	lt->sadb_lifetime_bytes = bytes;
	lt->sadb_lifetime_addtime = secs;
	lt->sadb_lifetime_usetime = 0;

	slt = (sadb_lifetime_t *)(lt + 1);
	slt->sadb_lifetime_len = SADB_8TO64(sizeof (*slt));
	slt->sadb_lifetime_exttype = SADB_EXT_LIFETIME_SOFT;
	slt->sadb_lifetime_allocations = 0;
	slt->sadb_lifetime_bytes = s_bytes;
	slt->sadb_lifetime_addtime = s_secs;
	slt->sadb_lifetime_usetime = 0;

	assoc = (sadb_sa_t *)(slt + 1);
	assoc->sadb_sa_len = SADB_8TO64(sizeof (*assoc));
	assoc->sadb_sa_exttype = SADB_EXT_SA;
	assoc->sadb_sa_spi = spi;
	assoc->sadb_sa_replay = 0;
	assoc->sadb_sa_state = SADB_SASTATE_MATURE;
	assoc->sadb_sa_auth = 0;
	assoc->sadb_sa_encrypt = 0;
	if (ikesrv == NULL)
		assoc->sadb_sa_flags = SADB_X_SAFLAGS_OUTBOUND;
	else
		assoc->sadb_sa_flags = SADB_X_SAFLAGS_INBOUND;

	/* Unspecified source addr. */
	src = (sadb_address_t *)(assoc + 1);
	src->sadb_address_len = SADB_8TO64(sizeof (*src) + roundup(salen, 8));
	src->sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
	src->sadb_address_proto = 0;
	src->sadb_address_prefixlen = 0;
	src->sadb_address_reserved = 0;

	ptr = (uint64_t *)(src + 1);
	(void) memset(ptr, 0, salen);
	((struct sockaddr_storage *)ptr)->ss_family = sa->ss_family;

	dst = (sadb_address_t *)(ptr + SADB_8TO64(salen));
	dst->sadb_address_len = SADB_8TO64(sizeof (*dst) + roundup(salen, 8));
	dst->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	dst->sadb_address_proto = 0;
	dst->sadb_address_prefixlen = 0;
	dst->sadb_address_reserved = 0;

	ptr = (uint64_t *)(dst + 1);
	(void) memcpy(ptr, sa, salen);

	req->pr_handler = update_life_finish;
	req->pr_context = NULL;
	req->pr_req = samsg;

	DUMP_PFKEY(samsg);
	pfkey_request(req);
}

static void
update_life_finish(pfkeyreq_t *req, sadb_msg_t *rep)
{
	if (rep->sadb_msg_errno != 0) {
		PRTDBG(D_OP, ("PF_KEY UPDATE error: %s; Diagnostic %s.",
		    strerror(rep->sadb_msg_errno),
		    keysock_diag(rep->sadb_x_msg_diagnostic)));
	}
	ssh_free(rep);
	ssh_free(req->pr_req);
	ssh_free(req);
}


static void
send_early_negative_acquire(sadb_msg_t *samsg, int unix_errno)
{
	int rc;

	samsg->sadb_msg_errno = unix_errno;
	samsg->sadb_msg_len = SADB_8TO64(sizeof (*samsg));
	rc = write(handler_socket, (uint8_t *)samsg, sizeof (*samsg));
	if (rc == -1) {
		EXIT_FATAL2("Negative ACQUIRE write: %s", strerror(unix_errno));
	}
	if (rc != sizeof (*samsg))
		PRTDBG(D_PFKEY,
		    ("Short negative ACQUIRE write() returned %d.", rc));
}

/*
 * Helper function to turn a Phase-I-initiating expire (created by
 * dpd_new_phase1()) into one that'll be used by start_dpd_process().  This
 * involves swapping src and dst.
 */
void
rewhack_dpd_expire(parsedmsg_t *pmsg)
{
	sadb_ext_t *tmp;
	struct sockaddr_storage *tss;

	tmp = pmsg->pmsg_exts[SADB_EXT_ADDRESS_SRC];
	pmsg->pmsg_exts[SADB_EXT_ADDRESS_SRC] =
	    pmsg->pmsg_exts[SADB_EXT_ADDRESS_DST];
	pmsg->pmsg_exts[SADB_EXT_ADDRESS_DST] = tmp;

	pmsg->pmsg_exts[SADB_EXT_ADDRESS_SRC]->sadb_ext_type =
	    SADB_EXT_ADDRESS_SRC;
	pmsg->pmsg_exts[SADB_EXT_ADDRESS_DST]->sadb_ext_type =
	    SADB_EXT_ADDRESS_DST;

	/*
	 * Don't forget to swap the sockaddr pointers too!
	 */
	tss = pmsg->pmsg_sss;
	pmsg->pmsg_sss = pmsg->pmsg_dss;
	pmsg->pmsg_dss = tss;

	/* OKAY!  Swapped and ready to go! */
}

void
send_negative_acquire(parsedmsg_t *pmsg, int unix_errno)
{
	sadb_msg_t samsg;
	sadb_sa_t *sa = (sadb_sa_t *)pmsg->pmsg_exts[SADB_EXT_SA];
	int rc;

	samsg = *(pmsg->pmsg_samsg);
	if (samsg.sadb_msg_type != SADB_ACQUIRE) {
		/*
		 * Do not send negative ACQUIRE if the Phase I session was
		 * initiated for DPD handshake.  Use the samsg type to
		 * determine that (!= ACQUIRE means DPD).
		 *
		 * OTOH, check the unix_errno for ETIMEDOUT, in which case,
		 * delete the SA pair.
		 */
		if (unix_errno == ETIMEDOUT && sa != NULL) {
			rewhack_dpd_expire(pmsg);
			handle_dpd_action(pmsg, SADB_X_DELPAIR);
		}
		return;
	}

	PRTDBG(D_PFKEY, ("Sending negative ACQUIRE, errno %d...",
	    unix_errno));

	samsg.sadb_msg_errno = unix_errno;
	samsg.sadb_msg_len = SADB_8TO64(sizeof (samsg));
	rc = write(handler_socket, &samsg, sizeof (samsg));
	if (rc == -1) {
		EXIT_FATAL2("Negative ACQUIRE write: %s", strerror(unix_errno));
	}
	if (rc != sizeof (samsg))
		PRTDBG(D_PFKEY,
		    ("Short negative ACQUIRE write() returned %d.", rc));
}

/*
 * Deal with an SADB_REGISTER message coming up from the kernel.
 * The caller will free the storage "samsg" points to.
 */

/*
 * handle SADB_REGISTER message: i.e. save the algorithms
 * to a useful place and form.
 */
static void
handle_register(sadb_msg_t *samsg)
{
	parsedmsg_t *pmsg = ssh_malloc(sizeof (parsedmsg_t));
	sadb_sens_t *sens;
	sadb_supported_t *supped;
	sadb_alg_t *alg_vec;
	uint16_t count, i;

	PRTDBG(D_PFKEY, ("Handling SADB register message from kernel..."));
	DUMP_PFKEY(samsg);

	/* 0. reality check */
	if (pmsg == NULL) {
		PRTDBG(D_PFKEY, ("  Out of memory handling SADB register."));
		return;
	}
	if (samsg->sadb_msg_type != SADB_REGISTER) {
		PRTDBG(D_PFKEY,
		    ("  Bad SADB message type %d (%s), but continuing.",
		    samsg->sadb_msg_type,
		    pfkey_satype(samsg->sadb_msg_satype)));
		/* Plow ahead in this case, try and fake it. */
	}

	if (!extract_exts(samsg, pmsg, 0)) {
		PRTDBG(D_PFKEY,
		    ("  Failed to extract PF_KEY extension headers."));
		ssh_free(pmsg);
		return;
	}

	sens = (sadb_sens_t *)pmsg->pmsg_exts[SADB_EXT_SENSITIVITY];
	if (sens != NULL)
		init_system_label(sens);

	/*
	 * length of sadb_supported_t is 8B
	 * length of sadb_alg_t is 8B
	 * lengths are in 8B increments
	 * so, (sadb_supported_len - 1) is the number of algs
	 */

	if (samsg->sadb_msg_satype == SADB_SATYPE_AH) {
		if ((supped = (sadb_supported_t *)pmsg->pmsg_exts[
		    SADB_EXT_SUPPORTED_AUTH]) != NULL) {
			count = supped->sadb_supported_len - 1;
			alg_vec = (sadb_alg_t *)supped;
			alg_vec += 1;

			if (p2_ah_algs != NULL)
				ssh_free(p2_ah_algs);

			p2_ah_algs = (p2alg_t *)ssh_malloc(
			    (sizeof (p2alg_t))*(count + 1));
			if (p2_ah_algs == NULL) {
				ssh_free(pmsg);
				PRTDBG(D_PFKEY,
				    ("  Out of memory handling SADB "
				    "register."));
				return;
			}

			for (i = 0; i < count; i++) {
				p2_ah_algs[i].p2alg_doi_num =
				    alg_vec[i].sadb_alg_id;
				p2_ah_algs[i].p2alg_min_bits =
				    alg_vec[i].sadb_alg_minbits;
				p2_ah_algs[i].p2alg_max_bits =
				    alg_vec[i].sadb_alg_maxbits;
				p2_ah_algs[i].p2alg_key_len_incr =
				    alg_vec[i].sadb_x_alg_increment;
				p2_ah_algs[i].p2alg_salt_bits =
				    alg_vec[i].sadb_x_alg_saltbits;
				get_default_keylen(SADB_SATYPE_AH,
				    &p2_ah_algs[i]);
			}

			p2_ah_algs[i].p2alg_doi_num = 0; /* end marker */

		}
	} else if (samsg->sadb_msg_satype == SADB_SATYPE_ESP) {

		if ((supped = (sadb_supported_t *)pmsg->pmsg_exts[
		    SADB_EXT_SUPPORTED_AUTH]) != NULL) {

			count = supped->sadb_supported_len -1;
			alg_vec = (sadb_alg_t *)supped;
			alg_vec += 1;


			if (p2_esp_auth_algs != NULL)
				ssh_free(p2_esp_auth_algs);

			p2_esp_auth_algs = (p2alg_t *)ssh_malloc(
			    (sizeof (p2alg_t))*(count + 1));
			if (p2_esp_auth_algs == NULL) {
				ssh_free(pmsg);
				PRTDBG(D_PFKEY,
				    ("  Out of memory handling SADB "
				    "register."));
				return;
			}

			for (i = 0; i < count; i++) {
				p2_esp_auth_algs[i].p2alg_doi_num =
				    alg_vec[i].sadb_alg_id;
				p2_esp_auth_algs[i].p2alg_min_bits =
				    alg_vec[i].sadb_alg_minbits;
				p2_esp_auth_algs[i].p2alg_max_bits =
				    alg_vec[i].sadb_alg_maxbits;
				p2_esp_auth_algs[i].p2alg_key_len_incr =
				    alg_vec[i].sadb_x_alg_increment;
				get_default_keylen(SADB_SATYPE_ESP,
				    &p2_esp_auth_algs[i]);
			}

			p2_esp_auth_algs[i].p2alg_doi_num = 0; /* end marker */
		}

		if ((supped = (sadb_supported_t *)pmsg->pmsg_exts[
		    SADB_EXT_SUPPORTED_ENCRYPT]) != NULL) {

			count = supped->sadb_supported_len -1;
			alg_vec = (sadb_alg_t *)supped;
			alg_vec += 1;

			if (p2_esp_encr_algs != NULL)
				ssh_free(p2_esp_encr_algs);

			p2_esp_encr_algs = (p2alg_t *)ssh_malloc(
			    (sizeof (p2alg_t))*(count + 1));
			if (p2_esp_encr_algs == NULL) {
				ssh_free(pmsg);
				PRTDBG(D_PFKEY,
				    ("  Out of memory handling SADB "
				    "register."));
				return;
			}

			for (i = 0; i < count; i++) {
				p2_esp_encr_algs[i].p2alg_doi_num =
				    alg_vec[i].sadb_alg_id;
				p2_esp_encr_algs[i].p2alg_min_bits =
				    alg_vec[i].sadb_alg_minbits;
				p2_esp_encr_algs[i].p2alg_max_bits =
				    alg_vec[i].sadb_alg_maxbits;
				p2_esp_encr_algs[i].p2alg_key_len_incr =
				    alg_vec[i].sadb_x_alg_increment;
				p2_esp_encr_algs[i].p2alg_salt_bits =
				    alg_vec[i].sadb_x_alg_saltbits;
				get_default_keylen(SADB_SATYPE_ESP,
				    &p2_esp_encr_algs[i]);
			}
			p2_esp_encr_algs[i].p2alg_doi_num = 0; /* end marker */
		}

	} else {
		PRTDBG(D_PFKEY,
		    ("  UNKNOWN SATYPE received in SADB register."));
	}

	ssh_free(pmsg);
}

/*
 * Initiates a DPD handshake with the peer.
 */
void
start_dpd_process(parsedmsg_t *pmsg)
{
	phase1_t		*p1;
	ike_server_t		*ikesrv;
	SshIkeNegotiation 	neg;
	SshIkeErrorCode 	ssh_rc;
	sadb_ident_t 		*srcid, *dstid;
	sadb_x_kmc_t 		*cookie;
	unsigned char		spi[2 * SSH_IKE_COOKIE_LENGTH];
	uint32_t		seq_num;
	struct sockaddr_storage *src, *dst;

	ikesrv = get_server_context(pmsg->pmsg_dss);
	if (ikesrv == NULL) {
		PRTDBG(D_PFKEY, ("DPD request for local address %s "
		    "can't be processed - in.iked is unaware of "
		    "address.", sap(pmsg->pmsg_dss)));
		free_pmsg(pmsg);
		return;
	}

	PRTDBG(D_PFKEY, ("DPD request for remote address %s beginning.",
	    sap(pmsg->pmsg_sss)));

	srcid = (sadb_ident_t *)pmsg->pmsg_exts[SADB_EXT_IDENTITY_SRC];
	dstid = (sadb_ident_t *)pmsg->pmsg_exts[SADB_EXT_IDENTITY_DST];
	src = pmsg->pmsg_sss;
	dst = pmsg->pmsg_dss;
	cookie = (sadb_x_kmc_t *)pmsg->pmsg_exts[SADB_X_EXT_KM_COOKIE];
	if (cookie != NULL && cookie->sadb_x_kmc_cookie == 0)
		cookie = NULL;

	/* Remember, swap src/dst for inbound expiration! */
	p1 = match_phase1(dst, src, dstid, srcid, cookie, B_FALSE);

	if (p1 == NULL) {
		free_pmsg(pmsg);
		return;
	}

	if (p1->p1_dpd_pmsg == NULL)
		p1->p1_dpd_pmsg = pmsg;

	neg = p1->p1_negotiation;
	p1->p1_dpd_sent_seqnum++;
	p1->p1_num_dpd_reqsent++;

	(void) memcpy(spi, neg->sa->cookies.initiator_cookie,
	    SSH_IKE_COOKIE_LENGTH);
	(void) memcpy(spi + SSH_IKE_COOKIE_LENGTH,
	    neg->sa->cookies.responder_cookie,
	    SSH_IKE_COOKIE_LENGTH);
	seq_num = htonl(p1->p1_dpd_sent_seqnum);

	/*
	 * Initiate DPD handshake with the peer. We start a timer to
	 * handle retries. The timer will be cancelled on receiving
	 * R-U-THERE-ACK message from the peer. See handle_dpd_notification()
	 * for details.
	 */
	PRTDBG(D_OP, ("Sending R-U-THERE notify message to %s (request #%d)",
	    sap(src), p1->p1_num_dpd_reqsent));
	if ((ssh_rc = ssh_ike_connect_notify(ikesrv->ikesrv_ctx,
	    neg, NULL, NULL, SSH_IKE_NOTIFY_FLAGS_WANT_ISAKMP_SA,
	    SSH_IKE_DOI_IPSEC, SSH_IKE_PROTOCOL_ISAKMP, spi,
	    sizeof (spi), SSH_IKE_NOTIFY_MESSAGE_R_U_THERE,
	    (unsigned char *)&seq_num, sizeof (seq_num))) != SSH_IKE_ERROR_OK) {
		PRTDBG(D_OP, ("Sending R-U-THERE notify message failed, "
		    "code %d (%s).", ssh_rc,
		    ike_connect_error_to_string(ssh_rc)));
		free_pmsg(pmsg);
		return;
	}
	(void) ssh_register_timeout(NULL,
	    ikesrv->ikesrv_ctx->isakmp_context->base_retry_timer_max,
	    ikesrv->ikesrv_ctx->isakmp_context->base_retry_timer_usec,
	    pfkey_idle_timer, pmsg);
}

/*
 * Initiates a new Phase 1 session, to handle the locally generated DPD
 * request, if an existing Phase 1 session cannot be located. The function
 * does not initiate quick mode SA generation.
 *
 * NOTE: The pmsg points to an EXPIRE of an INBOUND SA.  We have to swap
 * src/dst.  We generate a new ACQUIRE-looking EXPIRE, that can be re-swapped
 * and used by other DPD routines.  This means the ACQUIRE-looking EXPIRE must
 * also contain an sadb_sa_t extension for the SPI, and an sadb_lifetime_t
 * extension for the IDLE time.
 */
static void
dpd_new_phase1(parsedmsg_t *pmsg)
{
	uint64_t *buf, *curp;
	sadb_msg_t *nsamsg, *samsg;
	struct sockaddr_storage *socaddr;
	sadb_sa_t *sadb, *nsa;
	sadb_x_ecomb_t *ecomb;
	sadb_x_propbase_t *prop;
	sadb_x_algdesc_t *algdesc;
	sadb_x_pair_t *pairinfo;
	sadb_address_t *local, *remote;
	sadb_lifetime_t *nidle;
	static const p2alg_t *alg_strength;

	nsamsg = NULL;
	sadb = (struct sadb_sa *)pmsg->pmsg_exts[SADB_EXT_SA];
	if (sadb == NULL) {
		PRTDBG(D_PFKEY, ("  No message extensions"));
		return;
	}

	samsg = pmsg->pmsg_samsg;

	buf = ssh_malloc(SAMSG_BUFF_LEN);
	if (buf == NULL) {
		PRTDBG(D_PFKEY, ("  No memory"));
		return;
	}
	curp = buf;
#define	ALLOCP2(x, t1, t2) x = (t1 *)curp; curp += SADB_8TO64(sizeof (t2));
#define	ALLOCP(x, t) ALLOCP2(x, t, t)

	ALLOCP(nsamsg, sadb_msg_t);
	nsamsg->sadb_msg_version = PF_KEY_V2;
	/*
	 * Using SADB_EXPIRE as the message type differentiates from ACQUIRE
	 * or actual IPsec SA expiration to trigger replacement.
	 */
	nsamsg->sadb_msg_type = SADB_EXPIRE;
	nsamsg->sadb_msg_errno = 0;
	nsamsg->sadb_msg_satype = pmsg->pmsg_samsg->sadb_msg_satype;
	nsamsg->sadb_msg_reserved = 0;
	nsamsg->sadb_msg_len = SADB_8TO64(sizeof (*nsamsg));
	nsamsg->sadb_msg_seq = 0;
	nsamsg->sadb_msg_pid = 0;

	/*
	 * Unlike your typical ACQUIRE, you'll need the SPI in case the
	 * Phase I times out, and you nuke the idle-expiring SA.
	 */
	ALLOCP(nsa, sadb_sa_t);
	(void) memset(nsa, 0, sizeof (*nsa));
	nsa->sadb_sa_exttype = SADB_EXT_SA;
	nsa->sadb_sa_len = SADB_8TO64(sizeof (*nsa));
	pairinfo = (sadb_x_pair_t *)pmsg->pmsg_exts[SADB_X_EXT_PAIR];
	/* If no pairing info available, wing it with the wrong SPI. */
	nsa->sadb_sa_spi = (pairinfo == NULL) ? sadb->sadb_sa_spi :
	    pairinfo->sadb_x_pair_spi;

	/*
	 * Also unlike your typical ACQUIRE, you'll need the IDLE lifetime.
	 */
	ALLOCP(nidle, sadb_lifetime_t);
	nidle->sadb_lifetime_len = SADB_8TO64(sizeof (*nidle));
	nidle->sadb_lifetime_exttype = SADB_X_EXT_LIFETIME_IDLE;
	nidle->sadb_lifetime_allocations = 0;
	nidle->sadb_lifetime_bytes = 0;
	nidle->sadb_lifetime_addtime = ((sadb_lifetime_t *)
	    (pmsg->pmsg_exts[SADB_X_EXT_LIFETIME_IDLE]))->sadb_lifetime_addtime;
	nidle->sadb_lifetime_usetime = 0;

	ALLOCP(local, sadb_address_t);
	local->sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
	local->sadb_address_len = SADB_8TO64(sizeof (*local) +
	    sizeof (struct sockaddr_in6));
	local->sadb_address_proto = ((struct sadb_address *)
	    pmsg->pmsg_exts[SADB_EXT_ADDRESS_SRC])->sadb_address_proto;
	local->sadb_address_prefixlen = 0;
	local->sadb_address_reserved = 0;

	nsamsg->sadb_msg_len += local->sadb_address_len;
	ALLOCP2(socaddr, struct sockaddr_storage, struct sockaddr_in6);
	/* Remember, swap dst and src! */
	(void) memcpy(socaddr, pmsg->pmsg_dss, sizeof (struct sockaddr_in6));

	ALLOCP(remote, sadb_address_t);
	remote->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	remote->sadb_address_len = SADB_8TO64(sizeof (*remote) +
	    sizeof (struct sockaddr_in6));
	remote->sadb_address_proto = local->sadb_address_proto;
	remote->sadb_address_prefixlen = 0;
	remote->sadb_address_reserved = 0;

	nsamsg->sadb_msg_len += remote->sadb_address_len;
	ALLOCP2(socaddr, struct sockaddr_storage, struct sockaddr_in6);
	/* Remember, swap dst and src! */
	(void) memcpy(socaddr, pmsg->pmsg_sss, sizeof (struct sockaddr_in6));

	/*
	 * Create Extended proposals
	 */
	ALLOCP(prop, sadb_x_propbase_t);
	prop->sadb_x_propb_exttype = SADB_X_EXT_EPROP;
	prop->sadb_x_propb_len = SADB_8TO64(sizeof (*prop));
	prop->sadb_x_propb_len += SADB_8TO64(sizeof (*ecomb));
	prop->sadb_x_propb_numecombs = 1;
	prop->sadb_x_propb_replay = sadb->sadb_sa_replay;

	ALLOCP(ecomb, sadb_x_ecomb_t);
	ecomb->sadb_x_ecomb_numalgs = 0;
	ecomb->sadb_x_ecomb_reserved = 0;
	ecomb->sadb_x_ecomb_flags = 0;
	ecomb->sadb_x_ecomb_flags = sadb->sadb_sa_flags;
	ecomb->sadb_x_ecomb_reserved2 = 0;
	ecomb->sadb_x_ecomb_soft_allocations = 0;
	ecomb->sadb_x_ecomb_hard_allocations = 0;
	/* There is no need to set Phase 2 specific values. */
	ecomb->sadb_x_ecomb_soft_bytes = 0;
	ecomb->sadb_x_ecomb_soft_addtime = 0;
	ecomb->sadb_x_ecomb_hard_bytes = 0;
	ecomb->sadb_x_ecomb_hard_addtime = 0;
	ecomb->sadb_x_ecomb_soft_usetime = 0;
	ecomb->sadb_x_ecomb_hard_usetime = 0;

	if (sadb->sadb_sa_auth) {
		PRTDBG(D_PFKEY, ("  Have Authentication Alg"));
		ecomb->sadb_x_ecomb_numalgs++;
		prop->sadb_x_propb_len +=
		    SADB_8TO64(sizeof (struct sadb_x_algdesc));

		ALLOCP(algdesc, sadb_x_algdesc_t);
		algdesc->sadb_x_algdesc_satype = samsg->sadb_msg_satype;
		algdesc->sadb_x_algdesc_algtype = SADB_X_ALGTYPE_AUTH;
		algdesc->sadb_x_algdesc_alg =  sadb->sadb_sa_auth;
		if ((alg_strength =
		    find_esp_auth_alg(algdesc->sadb_x_algdesc_alg)) != NULL) {
			algdesc->sadb_x_algdesc_minbits =
			    alg_strength->p2alg_min_bits;
			algdesc->sadb_x_algdesc_maxbits =
			    alg_strength->p2alg_max_bits;
		} else {
			algdesc->sadb_x_algdesc_minbits = 0;
			algdesc->sadb_x_algdesc_maxbits = 0;
		}
	}

	if (sadb->sadb_sa_encrypt) {
		PRTDBG(D_PFKEY, ("  Have Encryption Alg"));
		ecomb->sadb_x_ecomb_numalgs++;
		prop->sadb_x_propb_len +=
		    SADB_8TO64(sizeof (struct sadb_x_algdesc));

		ALLOCP(algdesc, sadb_x_algdesc_t);
		algdesc->sadb_x_algdesc_satype = samsg->sadb_msg_satype;
		algdesc->sadb_x_algdesc_algtype = SADB_X_ALGTYPE_CRYPT;
		algdesc->sadb_x_algdesc_alg = sadb->sadb_sa_encrypt;

		/*
		 * in.iked already knows the kernels algorithm capabilities
		 * this was passed up when in.iked sent SADB_REGISTER.
		 */

		if ((alg_strength =
		    find_esp_encr_alg(algdesc->sadb_x_algdesc_alg)) != NULL) {
			algdesc->sadb_x_algdesc_minbits =
			    alg_strength->p2alg_min_bits;
			algdesc->sadb_x_algdesc_maxbits =
			    alg_strength->p2alg_max_bits;
		} else {
			algdesc->sadb_x_algdesc_minbits = 0;
			algdesc->sadb_x_algdesc_maxbits = 0;
		}
	}

	if (ecomb->sadb_x_ecomb_numalgs < 1) {
		PRTDBG(D_PFKEY, ("  No algorithms defined"));
		ssh_free(nsamsg);
		return;
	}

	nsamsg->sadb_msg_len += prop->sadb_x_propb_len;
#undef ALLOCP
#undef ALLOCP2
	handle_acquire(nsamsg, B_FALSE);
}

/*
 * Handle the PF_KEY SADB_EXPIRE message for idle timeout.
 * This'll involve starting an ISAKMP Dead Peer Detection if we can
 * find a phase 1 SA.
 */
static void
handle_idle_timeout(sadb_msg_t *samsg)
{

	phase1_t		*p1;
	parsedmsg_t		*pmsg = ssh_malloc(sizeof (parsedmsg_t));
	struct sockaddr_storage *src, *dst;
	sadb_ident_t 		*srcid, *dstid;
	sadb_x_kmc_t 		*cookie;
	uint32_t		idle_time;
	sadb_sa_t		*sa;

	if (!extract_exts(samsg, pmsg, 3, SADB_EXT_ADDRESS_DST,
	    SADB_EXT_ADDRESS_SRC, SADB_EXT_SA)) {
		PRTDBG(D_PFKEY, ("  Extracting SADB extensions failed."));
		/* samsg and pmsg linkage always occur. */
		free_pmsg(pmsg);
		return;
	}

	sa = (sadb_sa_t *)pmsg->pmsg_exts[SADB_EXT_SA];

	/*
	 * If this is an OUTBOUND SA, stop the madness now before we set
	 * any DPD state.
	 */
	if (sa->sadb_sa_flags & SADB_X_SAFLAGS_OUTBOUND) {
		PRTDBG(D_PFKEY, ("  DPD Ignoring expiring OUTBOUND SA."));
		free_pmsg(pmsg);
		return;
	}

	srcid = (sadb_ident_t *)pmsg->pmsg_exts[SADB_EXT_IDENTITY_SRC];
	dstid = (sadb_ident_t *)pmsg->pmsg_exts[SADB_EXT_IDENTITY_DST];
	src = pmsg->pmsg_sss;
	dst = pmsg->pmsg_dss;
	cookie = (sadb_x_kmc_t *)pmsg->pmsg_exts[SADB_X_EXT_KM_COOKIE];
	if (cookie != NULL && cookie->sadb_x_kmc_cookie == 0)
		cookie = NULL;

	/*
	 * Check NAT flags to see if we should initiate DPD or not.  If the SA
	 * is marked for NAT-Traversal, only do DPD if we are the node behind
	 * the NAT.  No NAT flags means it's also okay.
	 */
	if ((sa->sadb_sa_flags &
	    (SADB_X_SAFLAGS_NATT_LOC | SADB_X_SAFLAGS_NATT_REM) &&
	    !(sa->sadb_sa_flags & SADB_X_SAFLAGS_NATTED))) {
		PRTDBG(D_PFKEY, ("DPD NAT-T SA expired, but we're not behind"
		    " the NAT.  Don't bother."));
		free_pmsg(pmsg);
		return;
	}

	p1 = match_phase1(dst, src, dstid, srcid, cookie, B_FALSE);
	if (p1 == NULL) {
		dpd_new_phase1(pmsg);
		free_pmsg(pmsg);
		return;
	}

	/*
	 * If the peer does not support DPD, update the SA idle expire time.
	 */
	if (p1->p1_use_dpd == B_FALSE) {
		handle_dpd_action(pmsg, SADB_X_UPDATEPAIR);
		free_pmsg(pmsg);
		return;
	}

	if (p1->p1_dpd_status == DPD_IN_PROGRESS) {
		free_pmsg(pmsg);
		return;
	}

	idle_time =
	    ((sadb_lifetime_t *)pmsg->pmsg_exts[SADB_X_EXT_LIFETIME_IDLE])->
	    sadb_lifetime_addtime;

	/* If DPD handshake has been completed do not inititiate it again */
	if ((ssh_time() - p1->p1_dpd_time) < idle_time) {
		uint8_t action;
		action = (p1->p1_dpd_status == DPD_SUCCESSFUL) ?
		    SADB_X_UPDATEPAIR : SADB_X_DELPAIR;
		handle_dpd_action(pmsg, action);
		free_pmsg(pmsg);
		return;
	}
	p1->p1_dpd_status = DPD_IN_PROGRESS;
	start_dpd_process(pmsg);
}


void
pfkey_idle_timer(void *arg)
{
	parsedmsg_t		*pmsg = (parsedmsg_t *)arg;
	phase1_t		*p1;
	struct sockaddr_storage *src, *dst;
	sadb_ident_t 		*srcid, *dstid;
	sadb_x_kmc_t 		*cookie;
	ike_server_t		*ikesrv;

	srcid = (sadb_ident_t *)pmsg->pmsg_exts[SADB_EXT_IDENTITY_SRC];
	dstid = (sadb_ident_t *)pmsg->pmsg_exts[SADB_EXT_IDENTITY_DST];
	src = pmsg->pmsg_sss;
	dst = pmsg->pmsg_dss;
	cookie = (sadb_x_kmc_t *)pmsg->pmsg_exts[SADB_X_EXT_KM_COOKIE];
	if (cookie != NULL && cookie->sadb_x_kmc_cookie == 0)
		cookie = NULL;

	/*
	 * Get the server context of the destination IP address. The
	 * server context is used to get the number of retries configured.
	 */
	ikesrv = get_server_context(dst);
	if (ikesrv == NULL) {
		PRTDBG(D_PFKEY, ("DPD request for local address %s "
		    "can't be processed - in.iked is unaware of "
		    "the address.", sap(dst)));
		free_pmsg(pmsg);
		return;
	}

	/* Remember, swap src/dst for inbound expiration! */
	p1 = match_phase1(dst, src, dstid, srcid, cookie, B_FALSE);
	if ((p1 != NULL) && (p1->p1_num_dpd_reqsent >
	    ikesrv->ikesrv_ctx->isakmp_context->base_retry_limit)) {
		PRTDBG(D_PFKEY, ("Peer %s is considered to be dead now. ",
		    sap(src)));
		PRTDBG(D_PFKEY, ("  Deleting the SA pair and Phase 1 "
		    "instance."));
		handle_dpd_action(pmsg, SADB_X_DELPAIR);
		free_pmsg(pmsg);
		/*
		 * Mark the Phase 1 with DPD failure so that free_phase1()
		 * can send the right error code in negative ACQUIRE.
		 */
		p1->p1_dpd_status = DPD_FAILURE;
		p1->p1_dpd_pmsg = NULL;
		p1->p1_dpd_time = ssh_time();
		p1->p1_num_dpd_reqsent = 0;
		delete_phase1(p1, B_TRUE);
	} else {
		start_dpd_process(pmsg);
	}
}

void
handle_dpd_action(parsedmsg_t *pmsg, uint8_t msg_action)
{
	uint64_t *ptr;
	sadb_msg_t *samsg;
	sadb_sa_t *assoc;
	sadb_lifetime_t *idle;
	sadb_address_t *dst;
	int salen, len;
	pid_t mypid = getpid();
	pfkeyreq_t *req = ssh_malloc(sizeof (*req));

	if (req == NULL) {
		PRTDBG(D_PFKEY, ("  Out of memory updating SA lifetime."));
		return;
	}

	switch (pmsg->pmsg_dss->ss_family) {
	case AF_INET:
		salen = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		salen = sizeof (struct sockaddr_in6);
		break;
	default:
		salen = sizeof (struct sockaddr);
		break;
	}

	len = sizeof (*samsg) + sizeof (*assoc) + 2 * (sizeof (*dst) +
	    roundup(salen, 8));

	if (msg_action == SADB_X_UPDATEPAIR) {
		len += sizeof (*idle);
	}

	samsg = (sadb_msg_t *)ssh_malloc(len);
	if (samsg == NULL) {
		ssh_free(req);
		PRTDBG(D_PFKEY, ("  Out of memory updating SA lifetime."));
		return;
	}
	samsg->sadb_msg_version = PF_KEY_V2;
	samsg->sadb_msg_type = msg_action;
	samsg->sadb_msg_errno = 0;
	samsg->sadb_msg_satype = pmsg->pmsg_samsg->sadb_msg_satype;
	samsg->sadb_msg_len = SADB_8TO64(len);
	samsg->sadb_msg_reserved = 0;
	samsg->sadb_msg_seq = 0;
	samsg->sadb_msg_pid = mypid;

	assoc = (sadb_sa_t *)(samsg + 1);
	assoc->sadb_sa_len = SADB_8TO64(sizeof (*assoc));
	assoc->sadb_sa_exttype = SADB_EXT_SA;
	assoc->sadb_sa_spi = ((sadb_sa_t *)
	    (pmsg->pmsg_exts[SADB_EXT_SA]))->sadb_sa_spi;
	assoc->sadb_sa_replay = 0;
	assoc->sadb_sa_state = SADB_SASTATE_MATURE;
	assoc->sadb_sa_auth = 0;
	assoc->sadb_sa_encrypt = 0;
	assoc->sadb_sa_flags = 0;

	dst = (sadb_address_t *)(assoc + 1);
	dst->sadb_address_len = SADB_8TO64(sizeof (*dst) + roundup(salen, 8));
	dst->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	dst->sadb_address_proto = 0;
	dst->sadb_address_reserved = 0;

	ptr = (uint64_t *)(dst + 1);
	(void) memcpy(ptr, pmsg->pmsg_dss, salen);

	dst = (sadb_address_t *)(ptr + SADB_8TO64(salen));
	dst->sadb_address_len = SADB_8TO64(sizeof (*dst) + roundup(salen, 8));
	dst->sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
	dst->sadb_address_proto = 0;
	dst->sadb_address_prefixlen = 0;
	dst->sadb_address_reserved = 0;

	ptr = (uint64_t *)(dst + 1);
	(void) memcpy(ptr, pmsg->pmsg_sss, salen);

	if (msg_action == SADB_X_UPDATEPAIR) {
		idle = (sadb_lifetime_t *)(ptr + SADB_8TO64(salen));
		idle->sadb_lifetime_len = SADB_8TO64(sizeof (*idle));
		idle->sadb_lifetime_exttype = SADB_X_EXT_LIFETIME_IDLE;
		idle->sadb_lifetime_allocations = 0;
		idle->sadb_lifetime_bytes = 0;
		idle->sadb_lifetime_addtime = ((sadb_lifetime_t *)
		    (pmsg->pmsg_exts[SADB_X_EXT_LIFETIME_IDLE]))->
		    sadb_lifetime_addtime;
		idle->sadb_lifetime_usetime = 0;
	}
	req->pr_handler = update_life_finish;
	req->pr_context = NULL;
	req->pr_req = samsg;
	DUMP_PFKEY(samsg);
	pfkey_request(req);
}

/*
 * Compare type/address in local_i_id and remote_i_id to the actual local and
 * remote ip addresses, as well as the NAT-Traversal versions.  If the QM
 * payloads are the same as either the P1 or the NAT-T ones, we will assume
 * transport mode, otherwise we'll assume tunnel mode.  QM ids are always some
 * type of IP address, so this is always a safe comparison.  (If we have a
 * prefix, then we can immediately assume Tunnel Mode.)
 *
 * Returns -1 if we don't like the QM IDs for some reason.
 * Returns 0 if the QM IDs indicate transport mode.
 * Returns 1 if the QM IDs indicate tunnel mode.
 *
 * XXX - this method misses the corner case of complete end-to-end
 * self-encapsulation, but checking the QM IDs is a much more efficient
 * indicator, allowing us to avoid drudging through the transform payloads,
 * which we only do *AFTER* inverse-ACQUIRE.
 */
static int
qm_id_check(SshIkePMPhaseQm pm_info, SshIkePayloadID local_idp,
    SshIkePayloadID remote_idp, boolean_t *s9_tunnel_peer)
{
	struct sockaddr_in6 *local_sock6, *remote_sock6;
	struct sockaddr_in *local_sock, *remote_sock;
	in_addr_t *lpa, *rpa, *lnatt, *rnatt;
	in6_addr_t *lpa6, *rpa6;

	*s9_tunnel_peer = B_FALSE;

	local_sock = (struct sockaddr_in *)
	    &((phase1_t *)pm_info->phase_i->policy_manager_data)->p1_local;
	remote_sock = (struct sockaddr_in *)
	    &((phase1_t *)pm_info->phase_i->policy_manager_data)->p1_remote;
	local_sock6 = (struct sockaddr_in6 *)local_sock;
	remote_sock6 = (struct sockaddr_in6 *)remote_sock;

	if (pm_info->other_port != NULL) {
		if (pm_info->this_end_is_initiator) {
			PRTDBG(D_PFKEY,
			    ("qm_id_check: Acting as initiator with NAT-T."));
			lnatt = (in_addr_t *)&pm_info->natt_init_ip;
			rnatt = (in_addr_t *)&pm_info->natt_resp_ip;
		} else {
			PRTDBG(D_PFKEY,
			    ("qm_id_check: Acting as responder with NAT-T."));
			lnatt = (in_addr_t *)&pm_info->natt_resp_ip;
			rnatt = (in_addr_t *)&pm_info->natt_init_ip;
		}
	} else {
		PRTDBG(D_PFKEY,
		    ("qm_id_check: Either no NAT-T using tunnel-mode."));
		lnatt = rnatt = NULL;
	}

	PRTDBG(D_PFKEY, ("    checking local_id..."));
	if (local_idp != NULL) {
		lpa = (in_addr_t *)(&local_idp->identification);
		lpa6 = (in6_addr_t *)lpa;

		PRTDBG(D_PFKEY, ("qm_id_check: local_id type %d",
		    local_idp->id_type));
		switch (local_idp->id_type) {
		case IPSEC_ID_IPV4_ADDR:
			if (local_sock->sin_addr.s_addr != *lpa &&
			    (lnatt == NULL || *lnatt != *lpa)) {
				return (1);
			}
			break;
		case IPSEC_ID_IPV6_ADDR:
			if (!IN6_ARE_ADDR_EQUAL(&local_sock6->sin6_addr, lpa6))
				return (1);
			break;
		case IPSEC_ID_IPV4_ADDR_SUBNET:
			if (local_idp->protocol_id == IPPROTO_ENCAP &&
			    *lpa == INADDR_ANY) {
				*s9_tunnel_peer = B_TRUE;
				break;	/* out of switch() */
			}
			/* FALLTHRU */
		case IPSEC_ID_IPV6_ADDR_SUBNET:
			return (1);
		default:
			/* Yike, it's an ID type we don't understand! */
			return (-1);
		}
	}

	PRTDBG(D_PFKEY, ("    checking remote_id..."));
	if (remote_idp != NULL) {
		rpa = (in_addr_t *)(&remote_idp->identification);
		rpa6 = (in6_addr_t *)rpa;

		PRTDBG(D_PFKEY, ("qm_id_check: remote_id type %d",
		    remote_idp->id_type));
		switch (remote_idp->id_type) {
		case IPSEC_ID_IPV4_ADDR:
			if (remote_sock->sin_addr.s_addr != *rpa &&
			    (rnatt == NULL || *rnatt != *rpa)) {
				return (1);
			}
			*s9_tunnel_peer = B_FALSE;
			break;
		case IPSEC_ID_IPV6_ADDR:
			if (!IN6_ARE_ADDR_EQUAL(&remote_sock6->sin6_addr, rpa6))
				return (1);
			*s9_tunnel_peer = B_FALSE;
			break;
		case IPSEC_ID_IPV4_ADDR_SUBNET:
			if (*s9_tunnel_peer &&
			    remote_idp->protocol_id == IPPROTO_ENCAP &&
			    *rpa == INADDR_ANY)
				break;	/* out of switch() */
			/* FALLTHRU */
		case IPSEC_ID_IPV6_ADDR_SUBNET:
			return (1);
		default:
			/* Yike, it's an ID type we don't understand! */
			return (-1);
		}
	}

	PRTDBG(D_PFKEY, ("    assuming transport mode."));
	return (0);
}


size_t
roundup_bits_to_64(size_t sens_bits)
{
	int bytes = SADB_1TO8(roundup(sens_bits, 8));

	return (roundup(bytes, 8));
}

/*
 * Generate an sadb_sens_t from an inbound situation including a
 * sensitivity label.
 * This is a mostly rote conversion.  Policy will intervene here and
 * decide whether (a) to keep this local or (b) push it on the wire.
 */
static int
construct_sens_ext(phase1_t *p1, SshIkeIpsecSituationPacket sit,
    sadb_sens_t *sens)
{
	int sens_bits, sens_bytes, sens_words;
	int integ_bits, integ_bytes, integ_words;
	int sens_len;
	uint32_t sitflags;
	uint64_t *sens_bitmask, *integ_bitmask;

	if (!is_ike_labeled())
		return (0);

	if (sit == NULL) {
		/*
		 * no interesting situations were proposed.
		 * fall back to wildcard label if peer is labeled but
		 * didn't give us a label, else use default label.
		 */
		if (p1->label_aware)
			return (0);
		return (ipsec_convert_sl_to_sens(p1->label_doi,
		    &p1->min_sl, sens));
	}

	sens_bits = sit->secrecy_category_bitmap_length;
	sens_bytes = roundup_bits_to_64(sens_bits);
	sens_words = SADB_8TO64(sens_bytes);
	integ_bits = sit->integrity_category_bitmap_length;
	integ_bytes = roundup_bits_to_64(integ_bits);
	integ_words = SADB_8TO64(integ_bytes);
	sens_len = sizeof (sadb_sens_t) + sens_bytes + integ_bytes;
	sitflags = sit->situation_flags;
	sens_bitmask = (uint64_t *)&(sens[1]);
	integ_bitmask = &(sens_bitmask[sens_words]);

	/* If we just need the size, we're done */
	if (sens == NULL)
		return (sens_len);

	(void) memset(sens, 0, sens_len);

	sens->sadb_sens_exttype = SADB_EXT_SENSITIVITY;
	sens->sadb_sens_len = SADB_8TO64(sens_len);
	sens->sadb_sens_dpd = sit->labeled_domain_identifier;

	if (sitflags & SSH_IKE_SIT_SECRECY) {
		sens->sadb_sens_sens_level = sit->secrecy_level_data[0];
		sens->sadb_sens_sens_len = sens_words;
		(void) memcpy(sens_bitmask,
		    sit->secrecy_category_bitmap_data, sens_bytes);
	}

	if (sitflags & SSH_IKE_SIT_INTEGRITY) {
		sens->sadb_sens_integ_level = sit->integrity_level_data[0];
		sens->sadb_sens_integ_len = integ_words;
		(void) memcpy(integ_bitmask,
		    sit->integrity_category_bitmap_data, integ_bytes);
	}

	return (sens_len);
}

static SshIkeIpsecSituationPacket
find_sit(saselect_t *ssap)
{
	SshIkeIpsecSituationPacket sit;
	SshIkePayload *sa_table_in;
	SshIkePayloadSA sa_payload;
	int idx = ssap->ssa_sit;

	if (idx < 0)
		return (NULL);

	sa_table_in = ssap->ssa_sas;
	sa_payload = &(sa_table_in[idx]->pl.sa);
	sit = &sa_payload->situation;
	return (sit);
}

static void finish_inverse_acquire(pfkeyreq_t *, sadb_msg_t *);

/*
 * Construct an inverse ACQUIRE for a QM exchange where I'm the receiver.
 */
void
inverse_acquire(saselect_t *ssap)
{
	pfkeyreq_t *req = &ssap->ssa_pfreq;
	uint64_t *buf, *curp;
	sadb_msg_t *samsg;
	struct sockaddr_storage *sa;
	sadb_address_t *local, *remote;
	sadb_address_t *local_inner, *remote_inner;
	sadb_sens_t *sens;
	SshIkePayloadID local_idp, remote_idp;
	pid_t mypid = getpid();
	SshIkePMPhaseQm pm_info = ssap->ssa_pm_info;
	phase1_t *p1;
	boolean_t tunnel_mode, s9_tunnel_peer;
	int rc, inverse_acquire_space, sens_space;
	SshIkeIpsecSituationPacket sit = find_sit(ssap);

	PRTDBG(D_PFKEY, ("Constructing inverse ACQUIRE..."));

	p1 = pm_info->phase_i->policy_manager_data;

	/*
	 * Use local/remote identities supplied by the initiator.
	 */
	local_idp = pm_info->local_i_id;
	remote_idp = pm_info->remote_i_id;

	PRTDBG(D_PFKEY, ("  %sInitiator Local ID = %s, Local IP = %s",
	    pm_info->this_end_is_initiator ? "** " : "",
	    pm_info->local_i_id_txt, pm_info->local_ip));
	PRTDBG(D_PFKEY, ("  %sInitiator Remote ID = %s, Remote IP = %s",
	    pm_info->this_end_is_initiator ? "** " : "",
	    pm_info->remote_i_id_txt, pm_info->remote_ip));

	rc = qm_id_check(pm_info, local_idp, remote_idp, &s9_tunnel_peer);
	if (rc < 0) {
		/*
		 * We didn't like what we saw in the QM IDs.
		 * Invoke the ssap callback with a failure.
		 */
		PRTDBG(D_P2,
		    ("We can't deal with these QM IDs.  Returning.\n"));
		ssap->ssa_complete(ssap, NULL);
	}
	tunnel_mode = (boolean_t)rc;

	PRTDBG(D_PFKEY, ("  %s%s%s",
	    pm_info->this_end_is_initiator ? "** " : "",
	    tunnel_mode ? "Tunnel Mode " : "Transport Mode ",
	    "[INVERSE ACQUIRE]"));

	ssap->tunnel_mode = tunnel_mode;

	/*
	 * Construct an inverse ACQUIRE and put the response in the return
	 * values pmsg.
	 */
	inverse_acquire_space = sizeof (sadb_msg_t) +
	    4 * (sizeof (sadb_address_t) + sizeof (struct sockaddr_storage));

	/*
	 * Add room for label here, so kernel will be able to check it
	 * and echo it back.
	 */
	sens_space = construct_sens_ext(p1, sit, NULL);

	inverse_acquire_space += sens_space;

	buf = ssh_calloc(1, inverse_acquire_space);
	if (buf == NULL) {
		ssap->ssa_complete(ssap, NULL);
		return;
	}

	curp = buf;

#define	ALLOCB2(x, t1, s2) x = (t1 *)curp; curp += SADB_8TO64(s2);
#define	ALLOCP2(x, t1, t2) ALLOCB2(x, t1, sizeof (t2))
#define	ALLOCP(x, t) ALLOCP2(x, t, t)

	ALLOCP(samsg, sadb_msg_t);
	samsg->sadb_msg_version = PF_KEY_V2;
	samsg->sadb_msg_type = SADB_X_INVERSE_ACQUIRE;
	samsg->sadb_msg_seq = 0;
	samsg->sadb_msg_pid = mypid;
	samsg->sadb_msg_len = SADB_8TO64(sizeof (*samsg));

	/*
	 * Construct local outer addresses...
	 */

	ALLOCP(local, sadb_address_t);
	local->sadb_address_exttype = SADB_EXT_ADDRESS_SRC;
	local->sadb_address_len = SADB_8TO64(sizeof (*local) +
	    sizeof (struct sockaddr_in6));
	samsg->sadb_msg_len += local->sadb_address_len;
	local->sadb_address_prefixlen = 0;
	local->sadb_address_reserved = 0;

	ALLOCP2(sa, struct sockaddr_storage, struct sockaddr_in6);
	sa = (struct sockaddr_storage *)(local + 1);
	if (!string_to_sockaddr(pm_info->local_ip, sa)) {
		ssh_free(buf);
		ssap->ssa_complete(ssap, NULL);
		return;
	}
	if (local_idp != NULL) {
		if (tunnel_mode) {
			PRTDBG(D_PFKEY, ("  tunnel-mode local ID."));
			/*
			 * Construct local inner addresses...
			 */

			ALLOCP(local_inner, sadb_address_t);
			local_inner->sadb_address_exttype =
			    SADB_X_EXT_ADDRESS_INNER_SRC;
			local_inner->sadb_address_len =
			    SADB_8TO64(sizeof (*local_inner) +
			    sizeof (struct sockaddr_in6));
			samsg->sadb_msg_len += local_inner->sadb_address_len;
			local_inner->sadb_address_prefixlen =
			    idtoprefixlen(local_idp);
			PRTDBG(D_PFKEY, ("  local_inner_prefixlen = %d",
			    local_inner->sadb_address_prefixlen));

			local_inner->sadb_address_reserved = 0;

			if (local_idp->id_type == IPSEC_ID_IPV4_ADDR_SUBNET ||
			    local_idp->id_type == IPSEC_ID_IPV4_ADDR) {
				/* sockaddr_in6 for alignment */
				ALLOCP2(sa, struct sockaddr_storage,
				    struct sockaddr_in6);
				sa = (struct sockaddr_storage *)
				    (local_inner + 1);
				(void) memcpy(
				    &((struct sockaddr_in *)sa)->sin_addr,
				    &local_idp->identification,
				    sizeof (struct in_addr));
				sa->ss_family = AF_INET;
				local->sadb_address_proto = IPPROTO_ENCAP;
			} else {
				ALLOCP2(sa, struct sockaddr_storage,
				    struct sockaddr_in6);
				sa = (struct sockaddr_storage *)
				    (local_inner + 1);
				(void) memcpy(
				    &((struct sockaddr_in6 *)sa)->sin6_addr,
				    &local_idp->identification,
				    sizeof (struct in6_addr));
				sa->ss_family = AF_INET6;
				local->sadb_address_proto = IPPROTO_IPV6;
			}
			local_inner->sadb_address_proto =
			    local_idp->protocol_id;
			((struct sockaddr_in *)sa)->sin_port =
			    htons(local_idp->port_number);
		} else if (s9_tunnel_peer) {
			local->sadb_address_proto = IPPROTO_ENCAP;
			((struct sockaddr_in *)sa)->sin_port = 0;
		} else {
			local->sadb_address_proto = local_idp->protocol_id;
			((struct sockaddr_in *)sa)->sin_port =
			    htons(local_idp->port_number);
		}
	} /* Else they are zeroed. */

	ALLOCP(remote, sadb_address_t);
	remote->sadb_address_exttype = SADB_EXT_ADDRESS_DST;
	remote->sadb_address_len = SADB_8TO64(sizeof (*remote) +
	    sizeof (struct sockaddr_in6));
	samsg->sadb_msg_len += remote->sadb_address_len;
	remote->sadb_address_prefixlen = 0;
	remote->sadb_address_reserved = 0;
	ALLOCP2(sa, struct sockaddr_storage, struct sockaddr_in6);
	if (!string_to_sockaddr(pm_info->remote_ip, sa)) {
		ssh_free(buf);
		ssap->ssa_complete(ssap, NULL);
		return;
	}
	if (remote_idp != NULL) {
		if (tunnel_mode) {
			PRTDBG(D_PFKEY, ("  tunnel-mode remote ID."));
			/*
			 * Construct remote inner addresses...
			 */

			ALLOCP(remote_inner, sadb_address_t);
			remote_inner->sadb_address_exttype =
			    SADB_X_EXT_ADDRESS_INNER_DST;
			remote_inner->sadb_address_len =
			    SADB_8TO64(sizeof (*remote_inner) +
			    sizeof (struct sockaddr_in6));
			samsg->sadb_msg_len += remote_inner->sadb_address_len;
			remote_inner->sadb_address_prefixlen =
			    idtoprefixlen(remote_idp);
			PRTDBG(D_PFKEY, ("  remote_inner_prefixlen = %d",
			    remote_inner->sadb_address_prefixlen));

			remote_inner->sadb_address_reserved = 0;

			if (remote_idp->id_type == IPSEC_ID_IPV4_ADDR_SUBNET ||
			    remote_idp->id_type == IPSEC_ID_IPV4_ADDR) {
				/* sockaddr_in6 for alignment */
				ALLOCP2(sa, struct sockaddr_storage,
				    struct sockaddr_in6);
				sa = (struct sockaddr_storage *)
				    (remote_inner + 1);
				(void) memcpy(
				    &((struct sockaddr_in *)sa)->sin_addr,
				    &remote_idp->identification,
				    sizeof (struct in_addr));
				sa->ss_family = AF_INET;
				remote->sadb_address_proto = IPPROTO_ENCAP;
			} else {
				ALLOCP2(sa, struct sockaddr_storage,
				    struct sockaddr_in6);
				sa = (struct sockaddr_storage *)
				    (remote_inner + 1);
				(void) memcpy(
				    &((struct sockaddr_in6 *)sa)->sin6_addr,
				    &remote_idp->identification,
				    sizeof (struct in6_addr));
				sa->ss_family = AF_INET6;
				remote->sadb_address_proto = IPPROTO_IPV6;
			}
			remote_inner->sadb_address_proto =
			    remote_idp->protocol_id;
			((struct sockaddr_in *)sa)->sin_port =
			    htons(remote_idp->port_number);
		} else if (s9_tunnel_peer) {
			remote->sadb_address_proto = IPPROTO_ENCAP;
			((struct sockaddr_in *)sa)->sin_port = 0;
		} else {
			remote->sadb_address_proto = remote_idp->protocol_id;
			((struct sockaddr_in *)sa)->sin_port =
			    htons(remote_idp->port_number);
		}
	} /* Else they are zeroed. */

	ALLOCB2(sens, sadb_sens_t, sens_space);
	(void) construct_sens_ext(p1, sit, sens);
	if (sens_space != 0)
		samsg->sadb_msg_len += sens->sadb_sens_len;

	DUMP_PFKEY(samsg);

#undef ALLOCP2
#undef ALLOCP

	/* TODO: IDENTITY STUFF! */
	req->pr_handler = finish_inverse_acquire;
	req->pr_context = ssap;
	req->pr_req = samsg;

	pfkey_request(req);
}

/*
 * Now that we have a response from the kernel, proceed to
 * finish processing the SA request..
 */
static void
finish_inverse_acquire(pfkeyreq_t *pr, sadb_msg_t *samsg)
{
	saselect_t *ssap = pr->pr_context;
	parsedmsg_t *pmsg;

	/* All cases don't need pr->pr_req. */
	ssh_free(pr->pr_req);
	pr->pr_req = NULL;

	DUMP_PFKEY(samsg);

	if (samsg->sadb_msg_errno != 0) {
		/*
		 * Return a non-NULL, but invalid pointer (-1 value)
		 * if the kernel indicates that the matching policy
		 * is ambiguous (indicate for now with ENOENT).
		 */
		pmsg = (samsg->sadb_msg_errno == ENOENT ?
		    (parsedmsg_t *)-1 : NULL);
		ssh_free(samsg);
		ssap->ssa_complete(ssap, NULL);
		return;
	}

	/*
	 * We have an inverse ACQUIRE now.  Let's return it as a parsed
	 * message.
	 */

	pmsg = ssh_malloc(sizeof (parsedmsg_t));
	if (pmsg == NULL) {
		ssh_free(samsg);
		ssap->ssa_complete(ssap, NULL);
		return;
	}

	if (!extract_exts(samsg, pmsg, 3, SADB_EXT_ADDRESS_SRC,
	    SADB_EXT_ADDRESS_DST, SADB_X_EXT_EPROP)) {
		free_pmsg(pmsg);
		ssap->ssa_complete(ssap, NULL);
		return;
	}

	ssap->ssa_complete(ssap, pmsg);
}

/*
 * Grumble... Values for the authentication algorithm _attribute_ in
 * IKE don't match the transform ID values in the DOI.  The latter is
 * what PF_KEY uses to select algorithm IDs.  We need this function to convert,
 * especially in the interactions with various ACQUIRE messages.
 *
 * Needing this function also limits our ability to dynamically deal with
 * new algorithms unless we hack the translation into the sadb_alg_t that
 * is in REGISTER messages.
 */
int
auth_id_to_attr(int algid)
{
	switch (algid) {
	case SADB_AALG_MD5HMAC:
		return (IPSEC_VALUES_AUTH_ALGORITHM_HMAC_MD5);
	case SADB_AALG_SHA1HMAC:
		return (IPSEC_VALUES_AUTH_ALGORITHM_HMAC_SHA_1);
	}
	/* Lucky us, SHA-2 ones map directly, but they won't in IKEv2. */
	return (algid);
}

/*
 * PF_KEY pending transaction queues.
 */

static int pfkey_timeout_sec = 1;
static int pfkey_timeout_usec = 0;
static pfkeyreq_t *pendingq, *ptail;
static uint32_t pfkey_seq = 4;	/* Why not? */

#ifdef DEBUG
/*
 * Patch these non-zero to test PF_KEY retransmissions.
 */
int lossy_tx, lossy_rx;
#endif

static void pfkey_timer(void *);

static void
dequeue_req(pfkeyreq_t *req)
{
	pfkeyreq_t *next, *prev;

	next = req->pr_next;
	prev = req->pr_prev;
	if (prev != NULL) {
		prev->pr_next = next;
	} else {
		pendingq = next;
	}
	if (next != NULL) {
		next->pr_prev = prev;
	} else {
		if (req != ptail) {
			EXIT_FATAL("dequeue PF_KEY request fatal error");
		}
		ptail = prev;
	}
}

static void
queue_req(pfkeyreq_t *req)
{
	req->pr_next = NULL;

	if (pendingq == NULL)
		pendingq = req;
	req->pr_prev = ptail;
	if (ptail != NULL) {
		ptail->pr_next = req;
	}
	ptail = req;
}

static struct SshTimeoutRec ssh_trec;

static void
pfkey_start_timer(void)
{
	/*
	 * We use the global ssh_trec safely because we guaranteed that
	 * this function only gets called after a previous timeout has
	 * fired.
	 */
	(void) ssh_register_timeout(&ssh_trec, pfkey_timeout_sec,
	    pfkey_timeout_usec, pfkey_timer, NULL);
}

static void
pfkey_stop_timer(void)
{
	ssh_cancel_timeouts(pfkey_timer, NULL);
}

#ifdef DEBUG
static int
randomly_lose()
{
	return ((lrand48() % 5) == 1);
}
#endif

static void
tx_req(pfkeyreq_t *req)
{
	sadb_msg_t *msg = req->pr_req;

	PRTDBG(D_PFKEY, ("PF_KEY transmit request:\n"
	    "\t\t\t\t\t posting sequence number %u, message type %d (%s),\n"
	    "\t\t\t\t\t SA type %d (%s)",
	    msg->sadb_msg_seq,
	    msg->sadb_msg_type, pfkey_type(msg->sadb_msg_type),
	    msg->sadb_msg_satype, pfkey_satype(msg->sadb_msg_satype)));

#ifdef DEBUG
	if (lossy_tx && randomly_lose()) {
		PRTDBG(D_PFKEY, ("  Transmit request: randomly lose "
		    "transmit!"));
		return;
	}
#endif

	(void) write(handler_socket, msg, SADB_64TO8(msg->sadb_msg_len));

	/*
	 * Send the message to Solaris Cluster
	 */

	if (in_cluster_mode) {
		(void) sendto(cluster_socket, msg,
		    SADB_64TO8(msg->sadb_msg_len), 0,
		    (struct sockaddr *)&cli_addr,
		    sizeof (cli_addr));
	}
}

/*
 * Send the next request down, if any.
 *
 * Note: our issue strategy is *extremely* simple for now: only send
 * the head of the queue down, and wait for either a reply or a
 * timeout.
 *
 * If we want to allow for multiple outstanding requests, we need to
 * be careful that we don't allow things to be reordered in a way
 * which changes semantics... in particular:
 *	- don't reorder UPDATE before ADD
 *	- don't reorder FLUSH after ADD or UPDATE
 *	- don't reorder DELETE after ADD or UPDATE
 *
 * Note also that:
 *  1) We assume that the kernel will always eventually respond.
 *  2) We do not place any bounds on the size of the queue in this module.
 *	We use a caller-allocation strategy for pending requests, so the
 * 	correct place for queue size limitation is the point where we
 *	decide to create state for a new ike/ipsec SA.  at that point
 *	we could attempt to preallocate the callback state, and drop the
 *	request if we don't have the resources to service that client.
 */

static void
send_next_req()
{
	if (pendingq != NULL) {
		pfkey_start_timer();
		tx_req(pendingq);
	}
}

static void /* ARGSUSED */
pfkey_timer(void *arg)
{
	PRTDBG(D_PFKEY, ("PF_KEY timer expired!"));
	send_next_req();
}

static void
handle_reply(sadb_msg_t *reply)
{
	pfkeyreq_t *req;

#ifdef DEBUG
	if (lossy_rx && randomly_lose()) {
		PRTDBG(D_PFKEY, ("Randomly lose receive handling SADB "
		    "message reply!"));
		ssh_free(reply);
		return;
	}
#endif

	/*
	 * Match the reply based on sequence number and PID.  ALSO, check for
	 * matching message type (for cases of a QM-driven ADD and UPDATE,
	 * both of which use the same sequence number) or an ACQUIRE answer to
	 * an INVERSE_ACQUIRE request.
	 */
	for (req = pendingq; req != NULL; req = req->pr_next) {
		if ((reply->sadb_msg_seq == req->pr_req->sadb_msg_seq) &&
		    (reply->sadb_msg_satype == req->pr_req->sadb_msg_satype) &&
		    ((reply->sadb_msg_type == req->pr_req->sadb_msg_type) ||
		    (reply->sadb_msg_type == SADB_ACQUIRE &&
		    req->pr_req->sadb_msg_type == SADB_X_INVERSE_ACQUIRE)))
			break;
	}

	if (!req) {
		ssh_free(reply);
		return;
	}

	PRTDBG(D_PFKEY, ("SADB message reply handler:\n"
	    "\t\t\t\t\t got sequence number %u, message type %d (%s),\n"
	    "\t\t\t\t\t SA type %d (%s)",
	    reply->sadb_msg_seq,
	    reply->sadb_msg_type, pfkey_type(reply->sadb_msg_type),
	    reply->sadb_msg_satype, pfkey_satype(reply->sadb_msg_satype)));

	pfkey_stop_timer();
	/* unthread from list */
	dequeue_req(req);

	/* ok, send another req down, if any */
	if (pendingq != NULL)
		send_next_req();

	(*req->pr_handler)(req, reply);
}

static void
pfkey_request(pfkeyreq_t *req)
{
	boolean_t was_empty = (pendingq == NULL);
	sadb_msg_t *sareq = req->pr_req;

	if (sareq->sadb_msg_seq == 0)
		sareq->sadb_msg_seq = ++pfkey_seq;

	PRTDBG(D_PFKEY, ("PF_KEY request:\n"
	    "\t\t\t\t\t queueing sequence number %u, message type %d (%s),\n"
	    "\t\t\t\t\t SA type %d (%s)",
	    sareq->sadb_msg_seq,
	    sareq->sadb_msg_type, pfkey_type(sareq->sadb_msg_type),
	    sareq->sadb_msg_satype, pfkey_satype(sareq->sadb_msg_satype)));

	queue_req(req);
	if (was_empty)
		send_next_req();
}

boolean_t
open_pf_key(void)
{
	int bootflags;

	handler_socket = socket(PF_KEY, SOCK_RAW, PF_KEY_V2);
	if ((_cladm(CL_INITIALIZE, CL_GET_BOOTFLAG, &bootflags) != 0) ||
	    (bootflags & CLUSTER_BOOTED)) {
		in_cluster_mode = B_TRUE;
		cluster_socket = socket(AF_INET, SOCK_DGRAM, 0);
		cli_addr.sin_family = AF_INET;
		cli_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		cli_addr.sin_port = htons(CLUSTER_UDP_PORT);
	}
	return (handler_socket >= 0);
}
