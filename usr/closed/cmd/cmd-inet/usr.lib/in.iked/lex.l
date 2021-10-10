%{
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
 *
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 *	Solaris IKE daemon configuration file reader - lexical analyzer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libintl.h>
#include <locale.h>

#include <netinet/in.h>
#include <sys/socket.h>

#include <ike/sshincludes.h>
#include <ike/isakmp.h>
#include <ike/sshmp.h>
#include <ike/isakmp_doi.h>

#include <ike/certlib.h>
#include "ikerules.h"

#include "y.tab.h"

/* bypass ssh conventions */
#undef strdup

%}

%a3700
%e1400
%k600
%n600
%o4000
%p5600

/*
 * A basic v6 address, from the abnf in RFC2373:
 * hexseq  = hex4 *( ":" hex4)
 * hex4    = 1*4HEXDIG
 *
 * Various permutations of this block and "::" and a v4 address at the end
 * make up the full definition of a v6 address.
 *
 * Unfortunately, the abnf in RFC2373 was found to be broken (a single digit,
 * for example, would match), and it has been removed from the draft that's
 * in progress to update 2373.  So I made a small change that I think solves
 * the problem.  Basically, we need two different types of hexseq; one that
 * can stand alone (without a :: being involved somewhere) and one that can
 * be combined with the :: and (possibly) other hexseqs.  I've defined these
 * as HEXSEQC and HEXSEQ, respectively; where the former must have at least
 * one colon-hex4 pair in addition to the leading hex4, while the latter may
 * be just a single hex4.
 */
HEXDIG ([a-fA-F0-9])
HEXSEQ	(({HEXDIG}{1,4})(:({HEXDIG}{1,4}))*)
HEXSEQC	(({HEXDIG}{1,4})(:({HEXDIG}{1,4}))+)

/*
 * A v4 address: from the abnf in RFC2373.  Note that this is a limited
 * definition (does not accept shorthand notation), but is acceptable for
 * our purposes (parsing ike/config files).
 *
 * abnf:  IPv4address = 1*3DIGIT "." 1*3DIGIT "." 1*3DIGIT "." 1*3DIGIT
 */
V4	(([0-9]{1,3}\.){3}([0-9]{1,3}))

%%

#.*$ {						/* traditional comments */
}

\/\/.*$ {					/* C++-style comments */
}

\:			{ return COLON; }
-			{ return DASH; }
\.			{ return DOT; }
\{			{ return LCURLY; }
\}			{ return RCURLY; }
\/			{ return SLASH; }

aggressive		{ return AGGRESSIVE; }	/* reserved words */
auth_alg		{ return AUTH_ALG; }
auth_method		{ return AUTH_METHOD; }
cert_request		{ return CERT_REQUEST; }
cert_root		{ return CERT_ROOT; }
cert_trust		{ return CERT_TRUST; }
oakley_group		{ return OAKLEY_GROUP; }
dh_group		{ return OAKLEY_GROUP; }
DN			{ return DN; }
dn			{ return DN; }
DNS			{ return DNS; }
dns			{ return DNS; }
dss_sig			{ return DSS_SIG; }
email			{ return MBOX; }
EMAIL			{ return MBOX; }
encr_alg		{ return ENCR_ALG; }
expire_timer		{ return EXPIRE_TIMER; }
false			{ return yFALSE; }
FQDN			{ return FQDN; }
fqdn			{ return FQDN; }
GN			{ return GN; }
gn			{ return GN; }
ignore_crls		{ return IGNORE_CRLS; }
improved_rsa_encrypt	{ return IMPROVED_RSA_ENCRYPT; }
ip			{ return IP; }
IP			{ return IP; }
ipv4			{ return IPv4; }
ipv4_prefix		{ return IPv4_PREFIX; }
ipv4_range		{ return IPv4_RANGE; }
ipv6			{ return IPv6; }
ipv6_prefix		{ return IPv6_PREFIX; }
ipv6_range		{ return IPv6_RANGE; }
issuer			{ return ISSUER; }
label			{ return LABEL; }
local_addr		{ return LOCAL_ADDR; }
local_id		{ return LOCAL_ID; }
local_id_type		{ return LOCAL_ID_TYPE; }
ldap_server		{ return LDAP_SERVER; }
main			{ return MAIN; }
MBOX			{ return MBOX; }
mbox			{ return MBOX; }
nat_t_port		{ return NAT_T_PORT; }
no			{ return NO; }
max_certs		{ return MAX_CERTS; }
p1_lifetime_secs	{ return P1_LIFETIME_SECS; }
p1_mode			{ return P1_MODE; }
p1_nonce_len		{ return P1_NONCE_LEN; }
p1_xform		{ return P1_XFORM; }
p2_lifetime_secs	{ return P2_LIFETIME_SECS; }
p2_softlife_secs	{ return P2_SOFTLIFE_SECS; }
p2_idletime_secs	{ return P2_IDLETIME_SECS; }
p2_lifetime_kb		{ return P2_LIFETIME_KB; }
p2_softlife_kb		{ return P2_SOFTLIFE_KB; }
p2_nonce_len		{ return P2_NONCE_LEN; }
p2_pfs			{ return P2_PFS; }
p2_xform		{ return P2_XFORM; }
pkcs11_path		{ return PKCS11_PATH; }
preshared		{ return PRESHARED; }
proxy			{ return PROXY; }
remote_addr		{ return REMOTE_ADDR; }
remote_id		{ return REMOTE_ID; }
retry_limit		{ return RETRY_LIMIT; }
retry_timer_init	{ return RETRY_TIMER_INIT; }
retry_timer_max		{ return RETRY_TIMER_MAX; }
rsa_encrypt		{ return RSA_ENCRYPT; }
rsa_sig			{ return RSA_SIG; }
socks			{ return SOCKS; }
true			{ return yTRUE; }
user_fqdn		{ return USER_FQDN; }
use_http		{ return USE_HTTP; }
yes			{ return YES; }
wire_label		{ return WIRE_LABEL; }
multi_label		{ return MULTI_LABEL; }
single_label		{ return SINGLE_LABEL; }
inner			{ return INNER; }
none			{ return NONE; }
label_aware		{ return LABEL_AWARE; }
D_U_M_P			{ return D_U_M_P; }

(({HEXSEQC})|({HEXSEQ}::{HEXSEQ}?)|(::{HEXSEQ}?))(:{V4})? {
	return (V6ADDR);
}

{V4} {
	return (V4ADDR);
}

[0-9]+ {
	return (U32);
}

[0-9]+\.[0-9]+ {
	return (YDOUBLE);
}

((0x{HEXDIG}+(\-{HEXDIG}+)+)) {
	return (HEXLABEL);
}

\" {
	register size_t n = 0;
	register char c;

	for (n = 0; ; ) {
		c = yyinput();
		if (n == YYLMAX) {
			/* LOCO[FMT]: string overflow */
			(void) fprintf(stderr,
			    gettext("string overflow after %u bytes\n"), n);
			return (YYLEXERR);
		}
		if (c == '"')
			break;
		if (c == '\n') {
newliner:;
			(void) fprintf(stderr,
			    gettext("newline in quoted string\n"));
			break;
		}
		if (c == '\\') {
			/* todo: more complex escapes */
			if ((c = yyinput()) == '\n')
				goto newliner;
		}
		yytext[n++] = c;
	}
	yytext[n] = '\0';
	yylval._yycP = strdup(yytext);
	return (STRING);
}

3?[a-zA-Z_][-a-zA-Z0-9_]+ {
	/*
	 * Ugly.  This is to handle 3des-cbc as STRING,
	 * but 0-5 as U32 DASH U32.
	 */
	yylval._yycP = strdup(yytext);
	return (STRING);
}

[a-zA-Z_][a-zA-Z0-9_\-]+\([0-9]*\.\.[0-9]*\) {
	/*
	 * Return the case of algorithm(low..high).
	 */
	yylval._yycP = strdup(yytext);
	return (ALGRANGE);
}

[ \t\n]+ {					/* kill whitespace */
}

. {
	return (LEXERROR);
}

%%

/* eof */
