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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * kssl mdb module.  Provides a collection of dcmds and walkers that
 * operate on core kssl data structures.  Dependencies on kssl internals
 * are described in $SRC/uts/common/inet/kssl/kssl.h.
 */

#include <mdb/mdb_modapi.h>
#include <mdb/mdb_ks.h>
#include <mdb/mdb_ctf.h>
#include <mdb/mdb_string.h>

#include <ctype.h>
#include <sys/types.h>
#include <sys/processor.h>
#include <sys/time.h>
#include <sys/clock_impl.h>
#include <netinet/in.h>
#include <netinet/ip6.h>

#include <inet/kssl/ksslimpl.h>
#include <inet/kssl/kssl.h>
#include <inet/kssl/ksslproto.h>

#define	PORT_MIN	1
#define	PORT_MAX	65535

#define	KSSL_ADDR_WIDTH	16	/* kernel addresses *shouldn't* be wider */
#define	MAX_V6ADDR_WIDTH	40	/* max width of IPv6 address string */
#define	AVG_V6ADDR_WIDTH	32	/* avg width of IPv6 address string */

#define	PRINT_YESNO(p)		(p) ? "yes" : "no"

#define	KSTAT_FMT_STR	"%-40s%u\n"
#define	KSTAT_FMT_HDR	"%<u>%-40s%-39s%</u>\n"
#define	PRINT_KSTAT(ks)	\
	mdb_printf(KSTAT_FMT_STR, ks.name, ks.value.ui64);

extern int inet_pton(int af, const char *src, void *dst);

/*
 * Structure for giving a name to a constant.
 */
typedef struct {
	const char *const_name;  /* name of constant */
	int	    const_value; /* constant itself */
} constname_t;

/*
 * Structure for passing options to ssl_t printing functions.
 */
typedef struct sslprint_cb_data_s {
	unsigned int	show_handshake;
	unsigned int	show_entry;
	unsigned int	show_alertinfo;
	unsigned int	show_sessionID;
	unsigned int	show_protocol;
	unsigned int	show_pending;
	char		*source_addr;
	unsigned char	*entry_spec;
	unsigned int	first;
} sslprint_cb_data_t;

/*
 * Structure for passing options to kssl_entry_t printing functions.
 */
typedef struct ksslprint_cb_data_s {
	unsigned int	show_addrs;
	unsigned int	show_stubs;
	unsigned int	show_csuites;
	unsigned int	show_cache;
	unsigned int	show_srvcert;
	unsigned int	first;
} ksslprint_cb_data_t;

/*
 * Convert from ticks to milliseconds.
 */
static uint64_t
tick2msec(uint64_t tick)
{
	int tick_per_msec, msec_per_tick;
	static uint64_t val = 0;
	static int once = 0;

	if (once == 0) {
		/*
		 * Any variable out of the two is usable but we prefer
		 * msec_per_tick since it provides better precision.
		 */
		if (mdb_readvar(&msec_per_tick, "msec_per_tick") != -1) {
			if (msec_per_tick == 0) {
				/* use the default if zero */
				msec_per_tick = MILLISEC / HZ_DEFAULT;
				mdb_warn("msec_per_tick is 0, using %d\n",
				    msec_per_tick);
			}
			val = tick * msec_per_tick;
			once++;
			return (val);
		}
		if (mdb_readvar(&tick_per_msec, "tick_per_msec") != -1) {
			if (tick_per_msec == 0) {
				/* use the default if zero */
				msec_per_tick = HZ_DEFAULT / MILLISEC;
				mdb_warn("msec_per_tick is 0, using %d\n",
				    tick_per_msec);
			}
			val = tick / tick_per_msec;
			once++;
			return (val);
		}
		mdb_warn("cannot read symbols tick_per_msec and msec_per_tick");
		return (0);
	}

	return (val);
}

/*
 * Print `len' bytes of buffer `buf' using given format string.
 */
static void
printbuf(const char *fmtstr, uint8_t *buf, size_t len)
{
	size_t	i;

	for (i = 0; i < len; i++)
		mdb_printf(fmtstr, buf[i]);

	mdb_printf("\n");
}

/*
 * Convert major and minor version combination into string.
 */
static const char *
version2string(int major_version, int minor_version)
{
	if (major_version == 3 && minor_version == 1)
		return ("TLSv1.0");
	else if (major_version == 3 && minor_version == 2)
		return ("TLSv1.1");
	else if (major_version == 3 && minor_version == 3)
		return ("TLSv1.2");
	else if (major_version == 3 && minor_version == 0)
		return ("SSLv3");
	else if (major_version == 2)
		return ("SSLv2");
	else if (major_version == 0 && minor_version == 0)
		return ("none");
	else
		return ("unknown");
}

/*
 * Return string representation of enum value or NULL on error.
 * arg1: enum name
 * arg2: enum value
 */
const char *
enum_lookup(char *str, int val)
{
		mdb_ctf_id_t id;
		const char *cp;

		if (mdb_ctf_lookup_by_name(str, &id) == -1) {
			return (NULL);
		}

		if ((cp = mdb_ctf_enum_name(id, val)) != NULL) {
			return (cp);
		}

		return (NULL);
}

/*
 * enum_lookup() wrapper.
 */
const char *
enum_print(char *str, int val)
{
	const char *cp;

	if ((cp = enum_lookup(str, val)) != NULL)
		return (cp);
	else
		return ("<unknown>");
}

/*
 * Convert SSL alert level code to string.
 */
static const char *
alert_level2name(SSL3AlertLevel level)
{
	unsigned int i;
	static constname_t alert_levels[] = {
		{ "warning",	alert_warning },
		{ "fatal",	alert_fatal },
		{ NULL }
	};

	for (i = 0; alert_levels[i].const_name != NULL; i++) {
		if (level == alert_levels[i].const_value)
			return (alert_levels[i].const_name);
	}

	return ("<unknown>");
}

/*
 * Convert SSL alert description code to string.
 */
static const char *
alert_desc2name(SSL3AlertDescription desc)
{
	return (enum_print("SSL3AlertDescription", desc));
}

/*
 * Convert SSL hanshake state `state' to a name.
 */
static const char *
hs_state2name(int state)
{
	unsigned int i;
	static constname_t handshake_states[] = {
		{ "waiting for ClientHello (0/5)",	 wait_client_hello },
		{ "waiting for ClientKeyExchange (1/5)", wait_client_key },
		{ "waiting for crypto processing (2/5)", wait_client_key_done },
		{ "waiting for ChangeCipherSpec (3/5)",  wait_change_cipher },
		{ "waiting for Finished (4/5)",		 wait_finished },
		{ "handshake completed (5/5)",		 idle_handshake },
		{ NULL }
	};

	for (i = 0; handshake_states[i].const_name != NULL; i++) {
		if (state == handshake_states[i].const_value)
			return (handshake_states[i].const_name);
	}

	return ("<unknown>");
}

/*
 * Convert SSL cipher suite to corresponding name.
 */
static const char *
suite2name(uint16_t suite)
{
	unsigned int i;
	static constname_t cipher_suites[] = {
		{ "SSL_RSA_WITH_NULL_SHA",
		    SSL_RSA_WITH_NULL_SHA },
		{ "SSL_RSA_WITH_RC4_128_MD5",
		    SSL_RSA_WITH_RC4_128_MD5 },
		{ "SSL_RSA_WITH_RC4_128_SHA",
		    SSL_RSA_WITH_RC4_128_SHA },
		{ "SSL_RSA_WITH_DES_CBC_SHA",
		    SSL_RSA_WITH_DES_CBC_SHA },
		{ "SSL_RSA_WITH_3DES_EDE_CBC_SHA",
		    SSL_RSA_WITH_3DES_EDE_CBC_SHA },
		{ "TLS_RSA_WITH_AES_128_CBC_SHA",
		    TLS_RSA_WITH_AES_128_CBC_SHA },
		{ "TLS_RSA_WITH_AES_256_CBC_SHA",
		    TLS_RSA_WITH_AES_256_CBC_SHA },
		{ NULL }
	};

	for (i = 0; cipher_suites[i].const_name != NULL; i++) {
		if (suite == cipher_suites[i].const_value)
			return (cipher_suites[i].const_name);
	}

	return ("<unknown>");
}

/* Print session ID and master secret. */
static void
print_sessID_master(uchar_t *session_id, size_t sess_len,
	uchar_t *master_secret, size_t master_len)
{
	/*
	 * Print as hexadecimal numbers. This is useful mainly for displaying
	 * sessionID and master key values so that they can be immediately
	 * compared to output produced by openssl s_client(1)).
	 */
	mdb_printf("session_id:    ");
	printbuf("%02X", session_id, sess_len);
	mdb_printf("master_secret: ");
	printbuf("%02X", master_secret, master_len);
}

/*
 * Print sslSessionID structure contents.
 */
static int
print_sessionID(sslSessionID sid)
{
	clock_t	lbolt;

	print_sessID_master(sid.session_id,
	    sizeof (sid.session_id) /
	    sizeof (uchar_t),
	    sid.master_secret,
	    sizeof (sid.master_secret) /
	    sizeof (uchar_t));
	if ((lbolt = (clock_t)mdb_get_lbolt()) == -1)
		return (DCMD_ERR);
	mdb_printf("client_addr:   %N\n"
	    "time:          %ld msec\n"
	    "cached:        %s\n"
	    "cipher_suite:  0x%04x (%s)\n",
	    &sid.client_addr,
	    tick2msec(lbolt - sid.time),
	    sid.cached == 0 ? "no" : "yes",
	    sid.cipher_suite,
	    suite2name(sid.cipher_suite));

	return (DCMD_OK);
}

/*
 * Convert argument vector to sslprint_cb_data_t structure.
 */
static int
process_sslprint_opts(int argc, const mdb_arg_t *argv, uint_t flags,
	sslprint_cb_data_t *cbdata)
{
	/* Initialize the structure with default values. */
	cbdata->show_handshake = TRUE;
	cbdata->source_addr = NULL;
	cbdata->entry_spec = NULL;
	cbdata->show_entry = FALSE;
	cbdata->show_alertinfo = FALSE;
	cbdata->show_sessionID = FALSE;
	cbdata->show_protocol = FALSE;
	cbdata->show_pending = FALSE;
	cbdata->first = FALSE;

	if (mdb_getopts(argc, argv,
	    'A', MDB_OPT_SETBITS, TRUE, &cbdata->show_alertinfo,
	    'h', MDB_OPT_CLRBITS, TRUE, &cbdata->show_handshake,
	    'e', MDB_OPT_SETBITS, TRUE, &cbdata->show_entry,
	    'E', MDB_OPT_STR, &cbdata->entry_spec,
	    'p', MDB_OPT_SETBITS, TRUE, &cbdata->show_protocol,
	    'P', MDB_OPT_SETBITS, TRUE, &cbdata->show_pending,
	    's', MDB_OPT_STR, &cbdata->source_addr,
	    'S', MDB_OPT_SETBITS, TRUE, &cbdata->show_sessionID,
	    NULL) != argc)
		return (DCMD_USAGE);

	if (DCMD_HDRSPEC(flags))
		cbdata->first = TRUE;

	return (DCMD_OK);
}

/* Do the actual printing of ssl_t structure. */
/* ARGSUSED */
static int
print_ssl_internal(uintptr_t addr, const void *walk_data, void *cb_data)
{
	sslprint_cb_data_t *scb = cb_data;
	ssl_t		ssl;
	kssl_entry_t	entry;
	char		*fspec;

	if (scb->first) {
		mdb_printf("%<u>%-*s %*s %5s %7s %5s%</u>\n",
		    KSSL_ADDR_WIDTH, "ADDR", MAX_V6ADDR_WIDTH, "FADDR", "FPORT",
		    "VERSION", "STATE");
		scb->first = FALSE;
	}

	if (mdb_vread(&ssl, sizeof (ssl_t), addr) == -1) {
		mdb_warn("cannot read kssl_entry_t at %p", addr);
		return (DCMD_ERR);
	}

	/* If requested skip sessions with handshake in progress. */
	if (!scb->show_handshake && ssl.hs_waitstate < idle_handshake)
		return (DCMD_OK);

	/* If requested, only print sessions with given source address. */
	if (scb->source_addr != NULL) {
		char		buf[INET6_ADDRSTRLEN];
		in_addr_t	addr4;
		in6_addr_t	addr6;

		/*
		 * IPv4 source addresses are represented in KSSL as mapped-v4
		 * IPv6 addresses so add the prefix so that users can enter
		 * plain IPv4 address as argument.
		 */
		if (inet_pton(AF_INET, scb->source_addr, &addr4) == 1) {
			mdb_snprintf(buf, sizeof (buf), "::ffff:%s",
			    scb->source_addr);

			(void) inet_pton(AF_INET6, buf, &addr6);
		} else {
			if (inet_pton(AF_INET6, scb->source_addr,
			    &addr6) != 1) {
				mdb_warn("Invalid address specification: %s\n",
				    scb->source_addr);
				return (DCMD_ERR);
			}
		}

		/* Skip if no address match. */
		if (!IN6_ARE_ADDR_EQUAL(&addr6, &ssl.faddr))
			return (DCMD_OK);
	}

	/* If requested, only print sessions matching given KSSL entry. */
	if (scb->entry_spec != NULL) {
		char *spec;
		char *port_str;
		u_longlong_t port;
		in6_addr_t addr;
		size_t spec_size;
		char port_str_base[16];

		/*
		 * Cannot call strfree() because spec will be modified so store
		 * the size of the string now to use later with mdb_free().
		 */
		spec_size = strlen((const char *)(scb->entry_spec)) + 1;

		/*
		 * We cannot modify entry_spec because it is only initialized
		 * once during walk so allocate and copy.
		 */
		if ((spec = mdb_alloc(spec_size, UM_SLEEP)) == NULL) {
			mdb_warn("failed to allocate memory for entry spec");
			return (DCMD_ERR);
		}
		strcpy(spec, (const char *)(scb->entry_spec));

		if ((port_str = (char *)strchr((const char *)spec, '/'))
		    == NULL) {
			mdb_warn("Invalid ip/port specification: %s\n", spec);
			return (DCMD_ERR);
		}
		*port_str = '\0';
		port_str += 1;

		/* Convert the port string to number. */
		mdb_snprintf(port_str_base, sizeof (port_str_base),
		    "0t%s", port_str);
		port = mdb_strtoull(port_str_base);
		if (port < PORT_MIN || port > PORT_MAX) {
			mdb_warn("Invalid port specification: %s\n", port_str);
			mdb_free(spec, spec_size);
			return (DCMD_ERR);
		}

		/* For the comparison we need IP address of the entry. */
		if (mdb_vread(&entry, sizeof (kssl_entry_t),
		    (uintptr_t)ssl.kssl_entry) == -1) {
			mdb_warn("cannot read kssl_entry_t at %p",
			    (uintptr_t)ssl.kssl_entry);
			mdb_free(spec, spec_size);
			return (DCMD_ERR);
		}

		/* Skip if no port match. */
		if (entry.ke_ssl_port != (in_port_t)port) {
			mdb_free(spec, spec_size);
			return (DCMD_OK);
		}

		/*
		 * IPv4 addresses are represented as IPv4 mapped IPv6
		 * addresses so if we get IPv4 address we need to convert it
		 * to this format first.
		 */
		if (inet_pton(AF_INET, (char *)spec, &addr) == 1) {
			char myspec[INET6_ADDRSTRLEN];

			mdb_snprintf(myspec, sizeof (myspec), "::ffff:%s",
			    spec);
			fspec = myspec;
		} else {
			fspec = spec;
		}

		/* Convert the IP string to internal representation. */
		if (inet_pton(AF_INET6, (char *)fspec, &addr) != 1) {
			mdb_warn("Invalid address specification: %s\n", fspec);
			mdb_free(spec, spec_size);
			return (DCMD_ERR);
		}

		mdb_free(spec, spec_size);

		/* Skip if no address match. */
		if (!IN6_ARE_ADDR_EQUAL(&addr, &entry.ke_laddr))
			return (DCMD_OK);
	}

	mdb_nhconvert(&ssl.fport, &ssl.fport, sizeof (in_port_t));
	mdb_printf("%0*p %*N %5u %7s %5d\n", KSSL_ADDR_WIDTH, addr,
	    MAX_V6ADDR_WIDTH, &ssl.faddr, ssl.fport,
	    version2string(ssl.major_version, ssl.minor_version),
	    ssl.hs_waitstate);

	/*
	 * We are done with single line print for this structure. Onto
	 * specific indented prints.
	 */
	mdb_inc_indent(2);

	/*
	 * Print basic info of corresponding KSSL entry so we can see which
	 * entry received the connection.
	 */
	if (scb->show_entry) {
		if (mdb_vread(&entry, sizeof (kssl_entry_t),
		    (uintptr_t)ssl.kssl_entry) == -1) {
			mdb_warn("cannot read kssl_entry_t at %p", addr);
			return (DCMD_ERR);
		}

		mdb_printf("associated KSSL entry: %p\n", ssl.kssl_entry);
		mdb_inc_indent(2);
		mdb_printf("server address: %N\n", &entry.ke_laddr);
		mdb_printf("SSL port: %hu\n", entry.ke_ssl_port);
		mdb_printf("proxy port: %hu\n", entry.ke_proxy_port);
		mdb_dec_indent(2);
	}

	if (scb->show_protocol) {
		mdb_printf("protocol info:\n");
		mdb_inc_indent(2);
		mdb_printf("version (major/minor): %d/%d [%s]\n",
		    ssl.major_version, ssl.minor_version,
		    version2string(ssl.major_version, ssl.minor_version));
		mdb_printf("TCP MSS: %d\n", ssl.tcp_mss);
		mdb_printf("handshake state: %s\n",
		    hs_state2name(ssl.hs_waitstate));
		mdb_printf("number of handshake messages received: %d\n",
		    ssl.sslcnt);
		mdb_printf("client support for secure renegotiation: %s\n",
		    PRINT_YESNO(ssl.secure_renegotiation));
		mdb_printf("resumed: %s\n", PRINT_YESNO(ssl.resumed));
		mdb_printf("active input: %s\n", PRINT_YESNO(ssl.activeinput));
		mdb_printf("application data sent: %s\n",
		    PRINT_YESNO(ssl.appdata_sent));
		mdb_dec_indent(2);
	}

	if (scb->show_alertinfo) {
		mdb_printf("alert info:\n");
		mdb_inc_indent(2);
		mdb_printf("close_notify alert received: %s\n",
		    PRINT_YESNO(ssl.close_notify_clnt));
		mdb_printf("close_notify alert sent: %s\n",
		    PRINT_YESNO(ssl.close_notify_srvr));
		mdb_printf("fatal alert: %s\n", PRINT_YESNO(ssl.fatal_alert));
		mdb_printf("fatal error: %s\n", PRINT_YESNO(ssl.fatal_error));
		mdb_printf("alert sent: %s\n", PRINT_YESNO(ssl.alert_sent));
		mdb_printf("alert level: %s\n",
		    alert_level2name(ssl.sendalert_level));
		mdb_printf("alert description: %s\n",
		    alert_desc2name(ssl.sendalert_desc));
		mdb_dec_indent(2);
	}

	if (scb->show_sessionID) {
		mdb_printf("session info:\n");
		mdb_inc_indent(2);
		(void) print_sessionID(ssl.sid);
		mdb_dec_indent(2);
	}

	if (scb->show_pending) {
		mdb_printf("client proposals:\n");
		mdb_inc_indent(2);
		mdb_printf("cipher suite: 0x%04x [%s]\n",
		    ssl.pending_cipher_suite,
		    suite2name(ssl.pending_cipher_suite));
		mdb_printf("MAC algorithm: %d [%s]\n", ssl.pending_malg,
		    enum_print("SSL3MACAlgorithm", ssl.pending_malg));
		mdb_printf("Cipher algorithm: %d [%s]\n", ssl.pending_calg,
		    enum_print("SSL3BulkCipher", ssl.pending_calg));
		mdb_printf("key block size: %d bytes\n", ssl.pending_keyblksz);
		mdb_dec_indent(2);
	}

	mdb_dec_indent(2);

	return (DCMD_OK);
}

/*
 * Print the core fields in an ssl_t entry from protocol level.
 *
 * Arguments:
 *   -A		show SSL alert related info
 *   -e		show KSSL entry info
 *   -E <spec>	only show sessions for KSSL entry given by ip/sslport spec
 *   -h		skip all ssl_t's that are in handshake
 *   -p		show detailed protocol information
 *   -P		show information about client proposal
 *   -s <addr>	only show sessions from given source address
 *   -S		print session ID info
 */
static int
print_conn(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	sslprint_cb_data_t scb;

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (process_sslprint_opts(argc, argv, flags, &scb) != DCMD_OK)
		return (DCMD_USAGE);

	return (print_ssl_internal(addr, NULL, &scb));
}

/*
 * Alias for '::walk kssl_cache | ::kssl_conn [options]'
 */
/* ARGSUSED */
static int
ksslstat(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	sslprint_cb_data_t cbdata;

	if (process_sslprint_opts(argc, argv, flags, &cbdata) != DCMD_OK)
		return (DCMD_USAGE);

	if (mdb_walk("kssl_cache", print_ssl_internal, &cbdata) == -1) {
		mdb_warn("failed to walk kssl_cache");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

/*
 * Convert argument vector to ksslprint_cb_data_t structure.
 */
static int
process_ksslentry_opts(int argc, const mdb_arg_t *argv, uint_t flags,
	ksslprint_cb_data_t *cbdata)
{
	/* Initialize the structure with default values. */
	cbdata->show_addrs = FALSE;
	cbdata->show_stubs = TRUE;
	cbdata->show_csuites = FALSE;
	cbdata->show_cache = FALSE;
	cbdata->show_srvcert = FALSE;
	cbdata->first = FALSE;

	if (mdb_getopts(argc, argv,
	    'a', MDB_OPT_SETBITS, TRUE, &cbdata->show_addrs,
	    'l', MDB_OPT_CLRBITS, TRUE, &cbdata->show_stubs,
	    'c', MDB_OPT_SETBITS, TRUE, &cbdata->show_csuites,
	    'C', MDB_OPT_SETBITS, TRUE, &cbdata->show_cache,
	    'S', MDB_OPT_SETBITS, TRUE, &cbdata->show_srvcert,
	    NULL) != argc)
		return (DCMD_USAGE);

	if (DCMD_HDRSPEC(flags))
		cbdata->first = TRUE;

	return (DCMD_OK);
}

/* Do the actual printing of kssl_entry_t structure. */
/* ARGSUSED */
static int
print_ksslentry_internal(uintptr_t addr, const void *walk_data, void *cb_data)
{
	ksslprint_cb_data_t *kcb = cb_data;
	unsigned int	i;
	kssl_entry_t 	entry;
	Certificate_t	cert;

	/* Skip NULL entries if walking kssl_entry_tab with "holes". */
	if (addr == NULL)
		return (DCMD_OK);

	if (kcb->first) {
		mdb_printf("%<u>%-*s %*s %5s %5s %8s %8s%</u>\n",
		    KSSL_ADDR_WIDTH, "ADDR", AVG_V6ADDR_WIDTH, "LADDR",
		    "PROXY", "SSL", "LISTENER", "FALLBACK");
		kcb->first = FALSE;
	}

	if (mdb_vread(&entry, sizeof (kssl_entry_t), addr) == -1) {
		mdb_warn("cannot read kssl_entry_t at %p", addr);
		return (DCMD_ERR);
	}

	if (!kcb->show_stubs && entry.ke_proxy_head == NULL)
		return (DCMD_OK);

	/*
	 * No need to convert port numbers since they are always in network
	 * host order in kssl_entry_t.
	 * Also, all IP addresses are represented as IPv6 addresses.
	 */
	mdb_printf("%0*p %*N %5hu %5hu %8s %8s\n", KSSL_ADDR_WIDTH, addr,
	    AVG_V6ADDR_WIDTH, &entry.ke_laddr,
	    entry.ke_proxy_port, entry.ke_ssl_port,
	    entry.ke_proxy_head == NULL ? "missing" : "present",
	    entry.ke_fallback_head == NULL ? "no" : "yes");

	mdb_inc_indent(2);

	if (kcb->show_addrs) {
		mdb_printf("addresses:\n");
		mdb_inc_indent(2);
		mdb_printf("proxy head: 0x%p\n", entry.ke_proxy_head);
		mdb_printf("fallback head: 0x%p\n", entry.ke_fallback_head);
		mdb_dec_indent(2);
	}

	if (kcb->show_csuites) {
		mdb_printf("cipher suites:\n");
		mdb_inc_indent(2);
		for (i = 0; i < entry.kssl_cipherSuites_nentries; i++) {
			mdb_printf("0x%04x %s\n",
			    entry.kssl_cipherSuites[i],
			    suite2name(entry.kssl_cipherSuites[i]));
		}
		mdb_dec_indent(2);
	}

	if (kcb->show_cache) {
		kssl_sid_ent_t centry;

		mdb_printf("cache info:\n");
		mdb_inc_indent(2);
		mdb_printf("timeout: %d\n", entry.sid_cache_timeout);
		mdb_printf("entries: %d\n", entry.sid_cache_nentries);

		if (entry.sid_cache != NULL) {
			/*
			 * Read the cache entries one by one. This way we
			 * avoid unnecessary allocation of the whole array.
			 */
			for (i = 0; i < entry.sid_cache_nentries; i++) {
				clock_t	lbolt;

				if (mdb_vread(&centry, sizeof (kssl_sid_ent_t),
				    (uintptr_t)(entry.sid_cache + i)) == -1) {
					mdb_warn("cannot read kssl_sid_ent_t"
					    "at %p", entry.sid_cache + i);
					return (DCMD_ERR);
				}
				/* skip unused slots */
				if (centry.se_used == 0)
					continue;
				mdb_printf("[%d] used: %d\n",
				    i, centry.se_used);
				mdb_inc_indent(2);
				print_sessID_master(centry.se_sid.session_id,
				    sizeof (centry.se_sid.session_id) /
				    sizeof (uchar_t),
				    centry.se_sid.master_secret,
				    sizeof (centry.se_sid.master_secret) /
				    sizeof (uchar_t));
				if ((lbolt = (clock_t)mdb_get_lbolt()) == -1)
					return (DCMD_ERR);
				mdb_printf("client_addr:   %N\n"
				    "time:          %ld msec\n"
				    "cached:        %s\n"
				    "cipher_suite:  0x%04x (%s)\n",
				    &centry.se_sid.client_addr,
				    tick2msec(lbolt - centry.se_sid.time),
				    centry.se_sid.cached == 0 ? "no" : "yes",
				    centry.se_sid.cipher_suite,
				    suite2name(centry.se_sid.cipher_suite));
				mdb_dec_indent(2);
			}
		}
		mdb_dec_indent(2);
	}

	if (kcb->show_srvcert && entry.ke_server_certificate != NULL) {
		uchar_t *buf;

		if (mdb_vread(&cert, sizeof (Certificate_t),
		    (uintptr_t)entry.ke_server_certificate) == -1) {
			mdb_warn("cannot read Certificate_t at %p",
			    entry.ke_server_certificate);
			return (DCMD_ERR);
		}

		if ((buf = (uchar_t *)mdb_alloc(cert.len, UM_NOSLEEP))
		    == NULL) {
			mdb_warn("cannot read allocate memory for cert");
			return (DCMD_ERR);
		}
		if (mdb_vread(buf, cert.len,
		    (uintptr_t)cert.msg) == -1) {
			mdb_warn("cannot read certificate body at %p",
			    cert.msg);
			return (DCMD_ERR);
		}
		mdb_printf("server certificate (DER):\n");
		/*
		 * Skip 10 bytes at the beginning and 4 bytes at the end.
		 * These constitute certificate and server_hello_done SSL
		 * message headers, respectively, which are stored in the
		 * certificate buffer.
		 */
		mdb_inc_indent(2);
		/*
		 * Print the buffer so that it can be used for creating data
		 * file which can be then read by openssl x509(1) application
		 * (using "-inform der" argument).
		 */
		printbuf("\\x%02x", buf + 10, cert.len - 10 - 4);
		mdb_dec_indent(2);
		mdb_free(buf, cert.len);
	}

	mdb_dec_indent(2);

	return (DCMD_OK);
}

/*
 * Alias for '::walk kssl_entry_tab | ::kssl_entry [options]'
 */
/* ARGSUSED */
static int
ksslcfg(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	ksslprint_cb_data_t cbdata;

	if (process_ksslentry_opts(argc, argv, flags, &cbdata) != DCMD_OK)
		return (DCMD_USAGE);

	if (mdb_walk("kssl_entry_tab", print_ksslentry_internal,
	    &cbdata) == -1) {
		mdb_warn("failed to walk kssl_entry_tab");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

/*
 * Print the core fields in an KSSL kssl_entry_t and optionally also the fields
 * of its member structures.
 *
 * Arguments:
 *   -a		print addresses of listener/fallback
 *   -l		skip all kssl_entry_t's that have no listener app
 *   -c		print cipher suite info
 *   -C		print cache info
 */
static int
print_kssl_entry(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	ksslprint_cb_data_t kcb;

	if (!(flags & DCMD_ADDRSPEC))
		return (DCMD_USAGE);

	if (process_ksslentry_opts(argc, argv, flags, &kcb) != DCMD_OK)
		return (DCMD_USAGE);

	return (print_ksslentry_internal(addr, NULL, &kcb));
}

/*
 * Print all KSSL kstats.
 */
/* ARGSUSED */
static int
print_ksslctr(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	kssl_stats_t kst;
	uintptr_t statp;

	if ((flags & DCMD_ADDRSPEC) || argc != 0)
		return (DCMD_USAGE);

	/* Dereference kssl_statp */
	if (mdb_readvar(&statp, "kssl_statp") == -1) {
		mdb_warn("cannot read kssl_statp");
		return (DCMD_ERR);
	}

	/* Read the contents of the structure */
	if (mdb_vread(&kst, sizeof (kssl_stats_t), statp) == -1) {
		mdb_warn("cannot read *kssl_statp at %p", statp);
		return (DCMD_ERR);
	}

	mdb_printf(KSTAT_FMT_HDR, "NAME", "VALUE");

	/* Keep this sorted (like kstat output). */
	PRINT_KSTAT(kst.alloc_fails);
	PRINT_KSTAT(kst.appdata_record_ins);
	PRINT_KSTAT(kst.appdata_record_outs);
	PRINT_KSTAT(kst.bad_pre_master_secret);
	PRINT_KSTAT(kst.compute_mac_failure);
	PRINT_KSTAT(kst.fallback_connections);
	PRINT_KSTAT(kst.fatal_alerts);
	PRINT_KSTAT(kst.full_handshakes);
	PRINT_KSTAT(kst.internal_errors);
	PRINT_KSTAT(kst.no_suite_found);
	PRINT_KSTAT(kst.premature_close);
	PRINT_KSTAT(kst.proxy_fallback_failed);
	PRINT_KSTAT(kst.record_decrypt_failure);
	PRINT_KSTAT(kst.resumed_sessions);
	PRINT_KSTAT(kst.sid_cache_hits);
	PRINT_KSTAT(kst.sid_cache_lookups);
	PRINT_KSTAT(kst.sid_cached);
	PRINT_KSTAT(kst.sid_uncached);
	PRINT_KSTAT(kst.verify_mac_failure);
	PRINT_KSTAT(kst.warning_alerts);

	return (DCMD_OK);
}

static void
ssloptions_help(void)
{
	mdb_printf("Options:\n");
	mdb_printf("%t%-10s%tshow SSL alert related info\n", "-A");
	mdb_printf("%t%-10s%tskip sessions in handshake phase\n", "-h");
	mdb_printf("%t%-10s%tshow KSSL entry info\n", "-e");
	mdb_printf("%t%-10s%tonly show sessions for entry given by "
	    "ip/sslport spec\n", "-E <spec>");
	mdb_printf("%t%-10s%tshow detailed protocol information\n", "-p");
	mdb_printf("%t%-10s%tshow info about client proposal\n", "-P");
	mdb_printf("%t%-10s%tonly show sessions from given source "
	    "address\n", "-s <addr>");
	mdb_printf("%t%-10s%tprint session ID info\n", "-S");
}

static void
ksslstat_help(void)
{
	mdb_printf("Show KSSL connections.\n\n");
	ssloptions_help();
}

static void
print_ksslctr_help(void)
{
	mdb_printf("Show KSSL kstat counters.\n\n");
	mdb_printf("Note: always takes *kssl_kstatp as %<u>addr%</u>\n\n");
}

static void
print_conn_help(void)
{
	mdb_printf("Print the core information for a given KSSL ssl_t.\n\n");
	ssloptions_help();
}

static void
kssl_entry_options_help(void)
{
	mdb_printf("Print the core information for a given KSSL "
	    "kssl_entry_t.\n\n");
	mdb_printf("Options:\n");
	mdb_printf("\t-a\tprint addresses of listener/fallback\n");
	mdb_printf("\t-l\tskip entries without listener\n");
	mdb_printf("\t-c\tprint cipher suite info\n");
	mdb_printf("\t-C\tprint session cache info\n");
	mdb_printf("\t-S\tdump server certiticate\n");
}

static void
ksslcfg_help(void)
{
	mdb_printf("Show KSSL configuration entries\n\n");
	mdb_printf("Note: always takes kssl_entry_tab as %<u>addr%</u>\n\n");
	kssl_entry_options_help();
}

static void
print_kssl_entry_help(void)
{
	mdb_printf("Print the core information for a given KSSL "
	    "kssl_entry_t.\n\n");
	kssl_entry_options_help();
}

/*
 * Initialize a walk for the KSSL entries table. Note that local walks are not
 * supported since they're more trouble than they're worth.
 */
static int
kssl_entry_walk_init(mdb_walk_state_t *wsp)
{
	int	tab_nitems;	/* number of items in the table */

	if (wsp->walk_addr != 0) {
		mdb_warn("kssl_entry_walk does not support local walks\n");
		return (WALK_DONE);
	}

	if (mdb_readvar(&wsp->walk_data, "kssl_entry_tab") == -1) {
		mdb_warn("cannot read symbol kssl_entry_tab");
		return (WALK_ERR);
	}

	if (mdb_vread(&wsp->walk_addr, sizeof (uintptr_t),
	    (uintptr_t)wsp->walk_data) == -1) {
		mdb_warn("cannot read first entry of kssl_entry_tab from %p",
		    wsp->walk_data);
		return (WALK_ERR);
	}

	if (mdb_readvar(&tab_nitems, "kssl_entry_tab_nentries") == -1) {
		mdb_warn("cannot read symbol kssl_entry_tab_nentries");
		return (WALK_ERR);
	}

	wsp->walk_arg = (void *)(uintptr_t)tab_nitems;

	return (WALK_NEXT);
}

/*
 * Read an entry from the kssl entry table and prepare the walker for reading
 * of the next one. This exposes internal representation of the table by also
 * printing the "holes", i.e. NULL entries in the table.
 * wsp->walk_arg is used to store the remaining count of entries,
 * wsp->walk_data is the table entry pointer which has to be dereferenced
 * in order to get the address of kssl_entry_t structure.
 */
static int
kssl_entry_walk_step(mdb_walk_state_t *wsp)
{
	kssl_entry_t	entry;
	int		status;
	intptr_t	i = (intptr_t)wsp->walk_arg;
	uintptr_t	entry_ptr, table_ptr;

	if (i-- <= 0)
		return (WALK_DONE);

	/*
	 * Note that the kssl_entry_tab can contain "holes" so we skip over
	 * such entries.
	 */
	if (wsp->walk_addr != NULL) {
		if (mdb_vread(&entry, sizeof (kssl_entry_t), wsp->walk_addr)
		    == -1) {
			mdb_warn("cannot read kssl_entry_t at %p",
			    wsp->walk_addr);
			return (WALK_ERR);
		}
		status = wsp->walk_callback(wsp->walk_addr, &entry,
		    wsp->walk_cbdata);
	} else {
		status = wsp->walk_callback(wsp->walk_addr, NULL,
		    wsp->walk_cbdata);
		/*
		 * Bump the counter again since kssl_entry_tab_nentries
		 * variable counts the number of non-empty entries.
		 */
		i++;
	}

	/* Read the next one. */
	table_ptr = (uintptr_t)wsp->walk_data;
	table_ptr += sizeof (uintptr_t);
	wsp->walk_data = (void *)table_ptr;

	/* Dereference the pointer. */
	if (mdb_vread(&entry_ptr, sizeof (uintptr_t),
	    (uintptr_t)wsp->walk_data) == -1) {
		mdb_warn("cannot read pointer to kssl_entry_t at %p",
		    wsp->walk_addr);
		return (WALK_ERR);
	}

	wsp->walk_addr = entry_ptr;
	wsp->walk_arg = (void *)i;

	return (status);
}

/* Note: d-commands which require an address have underscore in them. */
static const mdb_dcmd_t dcmds[] = {
#define	KSSLENT_OPTS	":[-acClS]"
	{ "kssl_entry",	KSSLENT_OPTS, "print core KSSL kssl_entry_t info",
	    print_kssl_entry, print_kssl_entry_help },
	{ "ksslcfg",	KSSLENT_OPTS, "show KSSL configuration entries",
	    ksslcfg, ksslcfg_help },
#undef KSSLENT_OPTS
#define	KSSLCONN_OPTS	":[-AeEhpPsS]"
	{ "kssl_conn",	KSSLCONN_OPTS, "print core KSSL ssl_t info",
	    print_conn, print_conn_help },
	{ "ksslstat",	KSSLCONN_OPTS, "show KSSL connections",
	    ksslstat, ksslstat_help },
#undef KSSLCONN_OPTS
	{ "ksslctr",	NULL, "show KSSL kstat counters",
	    print_ksslctr, print_ksslctr_help },

	{ NULL }
};

static const mdb_walker_t walkers[] = {
	{ "kssl_entry_tab",	"walk the kssl entries table",
	    kssl_entry_walk_init,   kssl_entry_walk_step },

	{ NULL }
};

static const mdb_modinfo_t modinfo = { MDB_API_VERSION, dcmds, walkers };

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}
