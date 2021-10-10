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
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* #define DEBUG */
/*
 *	Solaris IKE daemon configuration file reader - LALR(1) parser.
 *
 *			ToDo's:
 *			- algorithms are currently named strings; translation/
 *			  validation and storage as alg #s
 *			- real I18N support (detrivialize LS macros)
 *			- support 'loose string' (unquoted string that should
 *			  be quoted) lexical input
 *			- define LDAP server storage structure; parse and
 *			  manipulate these
 *			- IPv6 address syntax, handling, masks, etc.
 *			- review orthogonality of defaults / per-rule settings
 *			- *investigate* better error handling (because there's
 *			  no re-anchor (e.g. C's ';'), this may be futile)
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <libintl.h>
#include <door.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socket_impl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/pfkeyv2.h>

#include <ike/sshincludes.h>
#include <ike/isakmp.h>
#include <ike/sshmp.h>
#include <ike/isakmp_doi.h>
#include <ike/cmi.h>
#include <ipsec_util.h>

#include <ikedoor.h>
#include "defs.h"
#include "getssh.h"

/* bypass ssh conventions */
#undef free
#undef calloc
#undef realloc
#undef malloc
#undef sprintf
#undef strdup

/* a little brevity ... */
#define	METHPRESHARED	SSH_IKE_VALUES_AUTH_METH_PRE_SHARED_KEY

#define	TNEW(T, OP, N)	((OP) == NULL ? (T *)calloc(N, sizeof (T)) \
			    : (T *)realloc(OP, (N) * sizeof (T)))

/* No system header has this... odd. */
#define	DECIMAL_UINT32_WIDTH	10	/* # of chars needed to print a uint */

extern char yytext[];

typedef struct {
	int		n__allocinit;	/* # of items to initially allocated */
	int		n__allocext;	/* # of items to extend by */
	size_t		thing_size;	/* size of each thing */
	int		n__alloc;	/* # of items allocated */
	int		n__used;	/* # of items used */
	void		*thingsP;	/* ptr to allocated things */
} allocthingT;

/* IPv4 masks by # of bits */
#define	IPv4MASKS(N)	(htonl(ipv4masks[N]))
/* fornow: avoid 32/64 bit issues */
static in_addr_t ipv4masks[] = {
	0x00000000,	0x80000000,	0xC0000000,	0xE0000000,
	0xF0000000,	0xF8000000,	0xFC000000,	0xFE000000,
	0xFF000000,	0xFF800000,	0xFFC00000,	0xFFE00000,
	0xFFF00000,	0xFFF80000,	0xFFFC0000,	0xFFFE0000,
	0xFFFF0000,	0xFFFF8000,	0xFFFFC000,	0xFFFFE000,
	0xFFFFF000,	0xFFFFF800,	0xFFFFFC00,	0xFFFFFE00,
	0xFFFFFF00,	0xFFFFFF80,	0xFFFFFFC0,	0xFFFFFFE0,
	0xFFFFFFF0,	0xFFFFFFF8,	0xFFFFFFFC,	0xFFFFFFFE,
	0xFFFFFFFF
};


/* IPv6 masks by # of bits */
static in6_addr_t  ipv6masks(int);

/* Global parameters that, for lint reasons, are here. */
boolean_t use_http = B_FALSE;
boolean_t ignore_crls = B_FALSE;

char ldap_pbuf[PATH_MAX];
char *ldap_path = NULL;

char *pkcs11_path = NULL;

SshCMLocalNetworkStruct proxy_info = {NULL, NULL, 0};

uint32_t max_certs;

uint16_t nat_t_port = IPPORT_IKE_NATT;

/* externally-visible (resultant) certspecs */
struct certlib_certspec requested_certs;
struct certlib_certspec root_certs;
struct certlib_certspec trusted_certs;
/* externally-visible (resultant) list of rules */
struct ike_rulebase rules;
/*
 * temporary versions for loading (read from the file into these vars;
 * move to the real names when the file's been read successfully
 */
static struct certlib_certspec	requested_certs_ld;
static struct certlib_certspec	root_certs_ld;
static struct certlib_certspec	trusted_certs_ld;
static struct ike_rulebase	rules_ld;

/*
 * internal default and rule accumulators - 'nullrule' is a convenient
 * accumulator for default settings and global xforms; only 'p1_lifetime_secs'
 * is in a separate structure (because it trickles into the xforms themselves);
 * this is convenient because grammar actions below can deposit via 'ruleP',
 * regardless of if this is a global or per-rule reduction; where actions need
 * to assert rule-ness, they use 'rulechk'
 */
static struct ike_rule nullrule;	/* for default values */
static unsigned _p1_lifetime_secs;	/* default value */
static struct ike_rule *ruleP = &nullrule; /* current rule being collected */
static struct ike_xform *xformP = NULL;	/* current xform being collected */

/* forward functions */
int rulebase_add(struct ike_rulebase *rbp, struct ike_rule *rp);
void rulebase_datafree(struct ike_rulebase *x);
static void rulebase_makeactive(struct ike_rulebase *new);
static int rulebase_append(struct ike_rulebase *dst, struct ike_rulebase *src);
static int xform_add(struct ike_rule *rp, struct ike_xform *xp);
static int append_spec(const char ***arrayp, int *countp, const char *newspec);
static void CERTSPEC_INIT(struct certlib_certspec *csp);
static void RULEBASE_INIT(struct ike_rulebase *rbp);
static int certspec_add(struct certlib_certspec *csp, const char *s);
static void _dump(void);
static void _final(void);
static void _initial(void);
static int idtypeadr(SshIkeIpsecIdentificationType idtype);
static boolean_t p1_xforms_dup(struct ike_rule *dst, struct ike_rule *src);
struct ike_rule *rule_dup(struct ike_rule *s);
void rule_free(struct ike_rule *x);
static int certspec_dup(struct certlib_certspec *dst,
    const struct certlib_certspec *src);
static void certspec_move(struct certlib_certspec *dst,
    struct certlib_certspec *src);
static int certspec_append(struct certlib_certspec *dst,
    struct certlib_certspec *src);
static void certspec_datafree(struct certlib_certspec *x);
static int addrspec_dup(struct ike_addrspec *dst,
    const struct ike_addrspec *src);
static void addrspec_datafree(struct ike_addrspec *x);
static int rulechk(int wantsrule);
static int ruleidchk(struct certlib_certspec *csp, const char *newspec);
static int ruleaddrchk(struct ike_addrspec *iasp,
    const struct ike_addrrange *iarp);
static int xformchk(struct ike_rule *rp);
static int xformpreshared(struct ike_rule *rp);
static uint8_t auth_alg_lookup(const char *name);
static boolean_t get_alg_parts(const char *, char **, char **, char **);
static boolean_t group_reality_check(int);
#ifdef	DEBUG
static void allocthing_dump(allocthingT *atP,
    void (*dumpFP)(char *thingP, const char *ctx), const char *ctx);
static void addrspec_dump(const struct ike_addrspec *asP, const char *ctx);
static void certspec_dump(const struct certlib_certspec *ccsP, const char *ctx);
static void rule_dump(const struct ike_rule *rP, const char *ctx);
static void rulebase_dump(const struct ike_rulebase *rbP, const char *ctx);
static void xform_dump(const struct ike_xform *xfP, const char *ctx);
#endif	/* DEBUG */


extern void yyerror(const char *);

%}

%union {
	struct ike_addrrange	_yyadr;
	char			*_yycP;
	int			_yyint;
	struct sockaddr_in	_yysin;
	struct sockaddr_in6	_yysin6;
	uint32_t		_yyu32;
	double			_yydouble;
	struct {
		int alg;	/* Algorithm number */
		int low;	/* Low key size (in bits) */
		int high;	/* High key size (in bits) */
	} _yyrange;
	bslabel_t		*_yyslabel;
}

/* general token types */
%token		LEXERROR
%token		COLON
%token		DASH
%token		DOT
%token		LCURLY
%token		RCURLY
%token <_yycP>	ALGRANGE
%token		SLASH
%token <_yycP>	STRING
%token <_yyu32>	U32
%token <_yycP>	V4ADDR
%token <_yycP>	V6ADDR
%token <_yydouble>	YDOUBLE
%token	YYLEXERR

/* phase 1 identity types - from RFC 2409 */
%token <_yyint>	DN
%token <_yyint>	DNS
%token <_yyint>	yFALSE
%token <_yyint>	FQDN
%token <_yyint>	GN
%token <_yyint>	IP
%token <_yyint>	IPv4
%token <_yyint>	IPv6
%token <_yyint> IPv4_PREFIX
%token <_yyint> IPv6_PREFIX
%token <_yyint> IPv4_RANGE
%token <_yyint>	IPv6_RANGE
%token <_yyint>	MBOX
%token <_yyint>	NO
%token <_yyint>	yTRUE
%token <_yyint>	USER_FQDN
%token <_yyint>	YES

/* additions to phase 1 id types which connote certificate types */
%token <_yyint>	ISSUER

/* reserved words */

/* parameter keywords */
%token		AUTH_ALG
%token		AUTH_METHOD
%token		CERT_REQUEST
%token		CERT_ROOT
%token		CERT_TRUST
%token		OAKLEY_GROUP
%token		ENCR_ALG
%token		EXPIRE_TIMER
%token		IGNORE_CRLS
%token		LABEL
%token		LOCAL_ADDR
%token		LOCAL_ID
%token		LOCAL_ID_TYPE
%token		LDAP_SERVER
%token		MAX_CERTS
%token		NAT_T_PORT
%token		P1_XFORM
%token		P1_LIFETIME_SECS
%token		P1_NONCE_LEN
%token		P1_MODE
%token		P2_LIFETIME_SECS
%token		P2_SOFTLIFE_SECS
%token		P2_IDLETIME_SECS
%token		P2_LIFETIME_KB
%token		P2_SOFTLIFE_KB
%token		P2_NONCE_LEN
%token		P2_PFS
%token		P2_XFORM
%token		PKCS11_PATH
%token		PROXY
%token		REMOTE_ADDR
%token		REMOTE_ID
%token		RETRY_LIMIT
%token		RETRY_TIMER_INIT
%token		RETRY_TIMER_MAX
%token		SOCKS
%token		USE_HTTP
%token		D_U_M_P
%token		WIRE_LABEL
%token		MULTI_LABEL
%token		SINGLE_LABEL
%token		INNER
%token		NONE
%token		LABEL_AWARE
%token <_yycP> 	HEXLABEL

/* phase 1 modes */
%token <_yyint>	MAIN
%token <_yyint>	AGGRESSIVE

/* phase 1 key types */
%token <_yyint>	DSS_SIG
%token <_yyint>	IMPROVED_RSA_ENCRYPT
%token <_yyint>	PRESHARED
%token <_yyint>	RSA_ENCRYPT
%token <_yyint>	RSA_SIG

%{
#ifdef	NYET
%type <_yyint>	p1_addr_type
#endif
%}

%type <_yycP>	simple_cert_sel
%type <_yysin>	ipv4addr
%type <_yysin6>	ipv6addr
%type <_yyadr>	p1_addr
%type <_yyint>	p1_auth_alg
%type <_yyrange> p1_encr_alg
%type <_yyint>	p1_auth_meth
%type <_yyint>	p1_id_type
%type <_yyu32>	p1_lifetime_secs
%type <_yyint>	p1_mode_type
%type <_yyu32>	u32
%type <_yydouble>	ydouble
%type <_yyslabel>	label

%%

file :
				{ _initial(); }
    gen_param_list p1_rule_list	{ _final(); }

gen_param_list :
    /* empty */
  | gen_param_list gen_param d_u_m_p

gen_param :
    CERT_TRUST simple_cert_sel	{
				char *s = $2;
				if (s == NULL ||
				    certspec_add(&trusted_certs_ld, s))
					yyerror(gettext("no more memory "
					    "for trusted_certs"));
				free(s);
				}
  | CERT_REQUEST simple_cert_sel {
				char *s = $2;
				if (s == NULL ||
				    certspec_add(&requested_certs_ld, s))
					yyerror(gettext("no more memory "
					    "for requested_certs"));
				free(s);
				}
  | CERT_ROOT simple_cert_sel	{
				char *s = $2;
				if (s == NULL ||
				    certspec_add(&root_certs_ld, s))
					yyerror(gettext("no more memory "
					    "for root_certs"));
				free(s);
				}
  | IGNORE_CRLS			{
				extern boolean_t ignore_crls;

				if (ignore_crls)
					yyerror(gettext("Already specified "
						    "ignore_crls"));
				else
					ignore_crls = B_TRUE;
  				}

  | USE_HTTP			{
				extern boolean_t use_http;

				if (use_http)
					yyerror(gettext("Already specified "
						    "use_http"));
				else
					use_http = B_TRUE;
  				}

  | SOCKS STRING		{
				if (proxy_info.socks != NULL)
					yyerror("Already specified socks "
					    "host");
				else
					proxy_info.socks = $2;
				/* Don't worry about freeing $2. */
				}

  | PROXY STRING		{
				extern SshCMLocalNetworkStruct proxy_info;

				if (proxy_info.proxy != NULL)
					yyerror("Already specified proxy "
					    "host");
				else
					proxy_info.proxy = $2;
				/* Don't worry about freeing $2. */
				}

  | LDAP_SERVER STRING		{
				ldap_path = ldap_pbuf;
				(void) strncpy(ldap_path, $2, PATH_MAX);
				}
  | PKCS11_PATH STRING		{
				if (pkcs11_path != NULL &&
				    strcmp(pkcs11_path, $2) != 0)
					yyerror(gettext("Already specified "
					    "different PKCS#11 path"));

				pkcs11_path = ikestats.st_pkcs11_libname;
				(void) strncpy(pkcs11_path, $2, PATH_MAX);
				ssh_free($2);
				}
  | MAX_CERTS u32		{
				max_certs = $2;
				if (!ike_defs.rule_max_certs)
					ike_defs.rule_max_certs = $2;
				}
  | NAT_T_PORT u32		{
				if (($2 & 0xFFFF) != $2 || $2 == 0)
					yyerror(gettext("Port number must "
					    "be in range (1 - 65535)"));
				nat_t_port = $2;
				}
  | EXPIRE_TIMER u32		{ ike_params.base_expire_timer = $2; }
  | RETRY_LIMIT u32		{ ike_params.base_retry_limit = $2; }
  | RETRY_TIMER_INIT u32	{ ike_params.base_retry_timer = $2; }
  | RETRY_TIMER_INIT ydouble	{
				ike_params.base_retry_timer = (int)$2;
				ike_params.base_retry_timer_usec =
				    (int)(($2 - ike_params.base_retry_timer) *
					1000000);
				}
  | RETRY_TIMER_MAX u32		{ ike_params.base_retry_timer_max = $2; }
  | RETRY_TIMER_MAX ydouble	{
				ike_params.base_retry_timer_max = (int)$2;
				ike_params.base_retry_timer_max_usec =
				    (int)(
					($2 - ike_params.base_retry_timer_max) *
					1000000);
				}
  | LABEL_AWARE			{ set_ike_label_aware(); }
  | WIRE_LABEL INNER		{ set_outer_label(&nullrule, B_FALSE, NULL); }
  | WIRE_LABEL label 		{ set_outer_label(&nullrule, B_FALSE, $2); }
  | WIRE_LABEL NONE label	{ set_outer_label(&nullrule, B_TRUE, $3); }

  | p1_nonce_len
  | p2_nonce_len
  | p1_lifetime_secs		{
  				if ($1 < MIN_P1_LIFETIME) {
					PRTDBG(D_OP, ("phase 1 lifetime less "
					    "than minimum (%d < %d) in global "
					    "rule", $1, MIN_P1_LIFETIME));
					PRTDBG(D_OP, ("  -> setting to "
					    "minimum value"));
					_p1_lifetime_secs = MIN_P1_LIFETIME;
				} else {
					_p1_lifetime_secs = $1;
				}
				}
  | p1_rule_elt

d_u_m_p :
    /* empty */
  | D_U_M_P			{
				_dump();
				}

label :
    STRING			{ $$ = string_to_label($1); free($1); }
  | HEXLABEL 			{ $$ = string_to_label(yytext); }

p1_rule_list :
    /* empty */
  | p1_rule_list p1_rule

p1_rule :
    LCURLY d_u_m_p		{  /* allocate another rule structure */
				ruleP = TNEW(struct ike_rule, 0, 1);
				if (ruleP == NULL) {
					yyerror(gettext("no more memory "
					    "for rules"));
					ruleP = &nullrule;
					YYERROR;
				}
				init_rule_label(&nullrule, ruleP);
				}
    p1_rule_body RCURLY		{
				/* handle pre-defaults if needed */
				if (ruleP->p2_pfs == 0)
					ruleP->p2_pfs = nullrule.p2_pfs;

				if (rulebase_add(&rules_ld, ruleP) != 0) {
					free(ruleP);
					ruleP = &nullrule;
					YYERROR;
				}
				ruleP = &nullrule;
				}

p1_rule_body :
    /* empty */
  | p1_rule_body p1_rule_elt d_u_m_p

p1_rule_elt :
    LABEL STRING		{
				if (rulechk(1)) {
					free($2);
					YYERROR;
				} else if (ruleP->label != NULL) {
					free($2);
					yyerror(gettext("multiple rule labels"));
					YYERROR;
				} else if (strlen($2) > IBUF_SIZE -
				    (DECIMAL_UINT32_WIDTH + 1)) {
					/*
					 * Account for cookie number
					 * (uint32_t in decimal) and a tab.
					 */
					free($2);
					yyerror(gettext("Label name too long"));
					YYERROR;
				} else {
					ruleP->label = $2;
				}
				}
  | LOCAL_ADDR p1_addr		{
				if (ruleaddrchk(&ruleP->local_addr, &$2))
					YYERROR;
				}
  | REMOTE_ADDR p1_addr		{
				if (ruleaddrchk(&ruleP->remote_addr, &$2))
					YYERROR;
				}
  | LOCAL_ID simple_cert_sel	{
    				if (ruleidchk(&ruleP->local_id, $2)) {
					free($2);
					YYERROR;
				}
				free($2);
  				}
  | REMOTE_ID simple_cert_sel	{
				if (ruleidchk(&ruleP->remote_id, $2)) {
					free($2);
					YYERROR;
				}
				free($2);
				}
  | LOCAL_ID_TYPE p1_id_type	{
				if (ruleP != &nullrule &&
				    ruleP->local_idtype != 0) {
					yyerror(gettext("rule already has a local id type"));
				}
				ruleP->local_idtype = $2;
				}
  | WIRE_LABEL INNER		{ set_outer_label(ruleP, B_FALSE, NULL); }
  | WIRE_LABEL label 		{ set_outer_label(ruleP, B_FALSE, $2); }
  | WIRE_LABEL NONE label	{ set_outer_label(ruleP, B_TRUE, $3); }
  | p1_multi_label
  | p1_single_label
  | p1_mode
  | p1_xform
  | p2_lifetime_secs
  | p2_softlife_secs
  | p2_idletime_secs
  | p2_lifetime_kb
  | p2_softlife_kb
  | p2_pfs

p1_xform :
    P1_XFORM LCURLY d_u_m_p	{  /* allocate a new xform structure */
				xformP = TNEW(struct ike_xform, 0, 1);
				if (xformP == NULL) {
					yyerror(gettext("no more memory for phase 1 transforms"));
					xformP = NULL;
					YYERROR;
				}
				}
    p1_xform_list RCURLY	{
				if (xform_add(ruleP, xformP) != 0) {
					free(xformP);
					xformP = NULL;
					YYERROR;
				}
				xformP = NULL;
				}

p1_xform_list :
    p1_xform_elt d_u_m_p
  | p1_xform_list p1_xform_elt d_u_m_p

p1_xform_elt :
    ENCR_ALG p1_encr_alg	{
				if (xformP->encr_alg != NULL) {
					yyerror(gettext("phase 1 transform already has an encr algorithm"));
					YYERROR;
				} else {
					xformP->encr_alg = $2.alg;
					xformP->encr_low_bits = $2.low;
					xformP->encr_high_bits = $2.high;
				}
				}
  | AUTH_ALG p1_auth_alg	{
				if (xformP->auth_alg != NULL) {
					yyerror(gettext("phase 1 transform already has an auth algorithm"));
					YYERROR;
				} else {
					xformP->auth_alg = $2;
				}
				}
  | AUTH_METHOD p1_auth_meth	{
				if (xformP->auth_method) {
					yyerror(gettext("phase 1 transform already has an auth method"));
					YYERROR;
				} else {
					xformP->auth_method = $2;
				}
				}
  | OAKLEY_GROUP u32		{
				if (!group_reality_check($2)) {
					yyerror(gettext("Unsupported Oakley group"));
					YYERROR;
				}
				if (xformP->oakley_group) {
					yyerror(gettext("phase 1 transform already has an Oakley group"));
					YYERROR;
				} else {
					xformP->oakley_group = $2;
				}
				}
  | p1_lifetime_secs		{
				if (xformP->p1_lifetime_secs) {
					yyerror(gettext("phase 1 transform already has a maximum lifetime"));
					YYERROR;
				} else {
					xformP->p1_lifetime_secs = $1;
				}
				}

p1_encr_alg :
    STRING			{
				  if (xformP->encr_alg != NULL) {
					yyerror(gettext("phase 1 transform already has an encr algorithm"));
					free($1);
					YYERROR;
				  }
				  $$.alg = encr_alg_lookup($1, &($$.low),
				      &($$.high));
				  free($1);
				}
  | ALGRANGE			{
				if (xformP->encr_alg != NULL) {
					yyerror(gettext("phase 1 transform already has an encr algorithm"));
					free($1);
					YYERROR;
				}

				$$.alg = pluck_out_low_high($1, &($$.low),
				    &($$.high));
				if ($$.alg == -1) {
					rule_free(ruleP);
					/*
					 * Need to free xform structure
					 * because it has not been
					 * associated with ruleP yet
					 */
					free(xformP);
					free($1);
					YYERROR;
				}

				free($1);
				}

p1_auth_alg :
    STRING			{
				$$ = auth_alg_lookup($1);
				free($1);
				}

p1_auth_meth :
    DSS_SIG			{ $$ = SSH_IKE_VALUES_AUTH_METH_DSS_SIGNATURES; }
  | IMPROVED_RSA_ENCRYPT	{ $$ = SSH_IKE_VALUES_AUTH_METH_RSA_ENCRYPTION_REVISED; }
  | PRESHARED			{ $$ = SSH_IKE_VALUES_AUTH_METH_PRE_SHARED_KEY; }
  | RSA_ENCRYPT			{ $$ = SSH_IKE_VALUES_AUTH_METH_RSA_ENCRYPTION; }
  | RSA_SIG			{ $$ = SSH_IKE_VALUES_AUTH_METH_RSA_SIGNATURES; }

p1_addr :
    ipv4addr			{
				$$.beginaddr.v4 = $1;
				$$.endaddr.v4 = $1;
				}
  | ipv4addr DASH ipv4addr	{
				if (ntohl($1.sin_addr.s_addr) >
				    ntohl($3.sin_addr.s_addr)) {
					yyerror(gettext("invalid IPv4 range"));
				} else {
					$$.beginaddr.v4 = $1;
					$$.endaddr.v4 = $3;
				}
				}
  | ipv4addr SLASH ipv4addr	{
				in_addr_t ibeg, imsk;

				ibeg = $1.sin_addr.s_addr;
				imsk = ~$3.sin_addr.s_addr;
				if (ibeg & imsk)
					yyerror(gettext("invalid IPv4 mask"));
				else {
					$$.beginaddr.v4 = $1;
					$$.endaddr.v4 = $1;
					$$.endaddr.v4.sin_addr.s_addr |= imsk;
				}
				}
  | ipv4addr SLASH u32		{
				in_addr_t ibeg, imsk;

				ibeg = $1.sin_addr.s_addr;

				if ($3 > 32 ) {
					yyerror(gettext("invalid IPv4 prefix length"));
				} else {
					imsk = ~IPv4MASKS($3);

					if (ibeg & imsk) {
						yyerror(gettext("invalid IPv4 netmask"));
					} else {
						$$.beginaddr.v4 = $1;
						$$.endaddr.v4 = $1;
						$$.endaddr.v4.sin_addr.s_addr |= imsk;
					}
				}
				}
  | ipv6addr			{
				$$.beginaddr.v6 = $1;
				$$.endaddr.v6 = $1;
				}
  | ipv6addr DASH ipv6addr	{
				/* Need to verify that $1 < $3 */
	  			in6_addr_t start = $1.sin6_addr;
	  			in6_addr_t end = $3.sin6_addr;

				if (!in6_addr_cmp(&start, &end)){
					yyerror(gettext("invalid IPv6 range"));
				} else {
					$$.beginaddr.v6 = $1;
					$$.endaddr.v6 = $3;
				}
				}
  | ipv6addr SLASH u32		{
	            in6_addr_t ibeg;
				in6_addr_t imsk;
				uint32_t valid;

				ibeg = $1.sin6_addr;

				if ($3 > 128) {
					yyerror(gettext("invalid IPv6 prefix length"));
				} else {
					imsk = ipv6masks($3);

					valid = (~imsk._S6_un._S6_u32[0] & ibeg._S6_un._S6_u32[0]) ||
						(~imsk._S6_un._S6_u32[1] & ibeg._S6_un._S6_u32[1]) ||
						(~imsk._S6_un._S6_u32[2] & ibeg._S6_un._S6_u32[2]) ||
						(~imsk._S6_un._S6_u32[3] & ibeg._S6_un._S6_u32[3]);

					if (valid != 0) {
						yyerror(gettext("invalid IPv6 netmask"));
					} else {
						$$.beginaddr.v6 = $1;
						$$.endaddr.v6 = $1;
						$$.endaddr.v6.sin6_addr._S6_un._S6_u32[0] |=
							~imsk._S6_un._S6_u32[0];
						$$.endaddr.v6.sin6_addr._S6_un._S6_u32[1] |=
							~imsk._S6_un._S6_u32[1];
						$$.endaddr.v6.sin6_addr._S6_un._S6_u32[2] |=
							~imsk._S6_un._S6_u32[2];
						$$.endaddr.v6.sin6_addr._S6_un._S6_u32[3] |=
							~imsk._S6_un._S6_u32[3];
					}
				}
                }


p1_id_type :
    DN				{ $$ = IPSEC_ID_DER_ASN1_DN; }
  | DNS				{ $$ = IPSEC_ID_FQDN; }
  | FQDN			{ $$ = IPSEC_ID_FQDN; }
  | GN				{ $$ = IPSEC_ID_DER_ASN1_GN; }
  | IP				{ $$ = IPSEC_ID_IPV4_ADDR; }
  | IPv4			{ $$ = IPSEC_ID_IPV4_ADDR; }
  | IPv4_PREFIX			{ $$ = IPSEC_ID_IPV4_ADDR_SUBNET; }
  | IPv4_RANGE			{ $$ = IPSEC_ID_IPV4_ADDR_RANGE; }
  | IPv6			{ $$ = IPSEC_ID_IPV6_ADDR; }
  | IPv6_PREFIX			{ $$ = IPSEC_ID_IPV6_ADDR_SUBNET; }
  | IPv6_RANGE			{ $$ = IPSEC_ID_IPV6_ADDR_RANGE; }
  | MBOX			{ $$ = IPSEC_ID_USER_FQDN; }
  | USER_FQDN			{ $$ = IPSEC_ID_USER_FQDN; }

p1_lifetime_secs :
   P1_LIFETIME_SECS u32		{
				$$ = $2;
				if (!ike_defs.rule_p1_lifetime_secs)
					ike_defs.rule_p1_lifetime_secs = $2;
				}

p1_mode :
    P1_MODE p1_mode_type	{ ruleP->mode = $2; }

p1_mode_type :
    AGGRESSIVE			{ $$ = SSH_IKE_XCHG_TYPE_AGGR; }
  | MAIN			{ $$ = SSH_IKE_XCHG_TYPE_IP; }

p1_nonce_len :
    P1_NONCE_LEN u32		{
				ruleP->p1_nonce_len = $2;
				if (!ike_defs.rule_p1_nonce_len)
					ike_defs.rule_p1_nonce_len = $2;
				}

p1_multi_label :
    MULTI_LABEL 		{
				if (label_already(ruleP)) {
					yyerror(gettext("Only one label indicator allowed per rule"));
					YYERROR;
				}
				ruleP->p1_multi_label = B_TRUE;
    				}

p1_single_label :
    SINGLE_LABEL 		{
				if (label_already(ruleP)) {
					yyerror(gettext("Only one label indicator allowed per rule"));
					YYERROR;
				}
				ruleP->p1_single_label = B_TRUE;
    				}

p2_lifetime_secs :
   P2_LIFETIME_SECS u32		{
				ruleP->p2_lifetime_secs = $2;
				if (!ike_defs.rule_p2_lifetime_secs)
					ike_defs.rule_p2_lifetime_secs = $2;
				}

p2_softlife_secs :
   P2_SOFTLIFE_SECS u32		{
				ruleP->p2_softlife_secs = $2;
				if (!ike_defs.rule_p2_softlife_secs)
					ike_defs.rule_p2_softlife_secs = $2;
				}

p2_idletime_secs :
   P2_IDLETIME_SECS u32		{
				ruleP->p2_idletime_secs = $2;
				if (!ike_defs.rule_p2_idletime_secs)
					ike_defs.rule_p2_idletime_secs = $2;
				}

p2_lifetime_kb :
   P2_LIFETIME_KB u32		{
				ruleP->p2_lifetime_kb = $2;
				if (!ike_defs.rule_p2_lifetime_kb)
					ike_defs.rule_p2_lifetime_kb = $2;
				}

p2_softlife_kb :
   P2_SOFTLIFE_KB u32		{
				ruleP->p2_softlife_kb = $2;
				if (!ike_defs.rule_p2_softlife_kb)
					ike_defs.rule_p2_softlife_kb = $2;
				}

p2_nonce_len :
   P2_NONCE_LEN u32		{
				ruleP->p2_nonce_len = $2;
				if (!ike_defs.rule_p2_nonce_len)
					ike_defs.rule_p2_nonce_len = $2;
				}

p2_pfs :
   P2_PFS u32			{
				if (ruleP->p2_pfs != 0) {
					yyerror(gettext("Rule already has a PFS group"));
					YYERROR;
				}
				/* Value of 0 is used to disable Phase 2 PFS. */
				if (($2 != 0) && !group_reality_check($2)) {
					yyerror(gettext("Unsupported Oakley group"));
					YYERROR;
				} else {
					ruleP->p2_pfs = $2;
					ike_defs.rule_p2_pfs = $2;
				}
				}

simple_cert_sel :
   STRING			{ $$ = $1; }

ipv4addr :
    V4ADDR			{
				in_addr_t sin;

				if (inet_pton(AF_INET, yytext, (void *)&sin) != 1) {
					yyerror(gettext("illegal IPv4 address"));
					YYERROR;
				}
				$$.sin_family = AF_INET;
				$$.sin_addr.s_addr = sin;
				}

ipv6addr :
    V6ADDR			{
				in6_addr_t sin6;

				if (inet_pton(AF_INET6, yytext, (void *)&sin6) != 1) {
					yyerror(gettext("illegal IPv6 address"));
					YYERROR;
				}
				$$.sin6_family = AF_INET6;
				$$.sin6_addr = sin6;
				}

u32 :
   U32				{ $$ = strtoul(yytext, 0, 0); }

ydouble :
   YDOUBLE			{
	   			errno = 0;
				$$ = strtod(yytext, NULL);
				if (errno != 0) {
					yyerror(
					    gettext("illegal timer value.\n"));
					YYERROR;
				}
				}

%%

/* Returns AF_INET or AF_INET6 if valid, 0 otherwise*/
static sa_family_t
verify_ike_addrspec(struct ike_addrspec *ap)
{
	sa_family_t family;
	int i;
	struct ike_addrrange *ar;

#define	IN6_IS_ADDR_ALLONES(addr) \
	((addr._S6_un._S6_u32[3] == 0xffffffff) && \
	( addr._S6_un._S6_u32[2] == 0xffffffff) && \
	( addr._S6_un._S6_u32[1] == 0xffffffff) && \
	( addr._S6_un._S6_u32[0] == 0xffffffff))


	if (ap->num_includes == 0)
		return (0);

	ar = ap->includes[0];
	family = ar->beginaddr.ss.ss_family;

	if (family == AF_INET) {
		for (i=0; i< ap->num_includes; i++) {
			ar = ap->includes[i];

			/* begin and end addresses must be AF_INET */
			if (ar->beginaddr.ss.ss_family != AF_INET ||
				ar->endaddr.ss.ss_family != AF_INET) {
				yyerror(gettext("Invalid combination of IPv4"
				                " and IPv6 addresses"));
				return (0);
			}
			/* begin and end addresses must be unicast */
			if (IN_MULTICAST(htonl(ar->beginaddr.v4.sin_addr.s_addr)) ||
				IN_MULTICAST((htonl(ar->endaddr.v4.sin_addr.s_addr)))) {
				yyerror(gettext("IPv4 multicast address not valid for"
				                "begin or end address"));
				return (0);
			}
		}
	} else if (family == AF_INET6) {
		for (i=0; i< ap->num_includes; i++) {
			ar = ap->includes[i];

			/* begin and end addresses must be AF_INET6 */
			if (ar->beginaddr.ss.ss_family != AF_INET6 ||
				ar->endaddr.ss.ss_family != AF_INET6) {
				yyerror(gettext("Invalid combination of IPv4"
				                " and IPv6 addresses"));
				return (0);
			}
			/* begin and end addresses must be unicast */
			if (IN6_IS_ADDR_MULTICAST(&ar->beginaddr.v6.sin6_addr) ||
			       (IN6_IS_ADDR_MULTICAST(&ar->endaddr.v6.sin6_addr) &&
			       (!IN6_IS_ADDR_ALLONES(ar->endaddr.v6.sin6_addr)))) {
				yyerror(gettext("IPv6 multicast address not valid for"
				                "begin or end address"));
				return (0);
			}
			/* begin and end addresses may not be mapped 4in6 */
			if (IN6_IS_ADDR_V4MAPPED(&ar->beginaddr.v6.sin6_addr) ||
				IN6_IS_ADDR_V4MAPPED(&ar->endaddr.v6.sin6_addr)) {
				yyerror(gettext("IPv4 mapped in IPv6 address not valid"));
				return (0);
			}
			/* begin and end addresses may not be compat 4in6 */
			if (IN6_IS_ADDR_V4COMPAT(&ar->beginaddr.v6.sin6_addr) ||
				IN6_IS_ADDR_V4COMPAT(&ar->endaddr.v6.sin6_addr)) {
				yyerror(gettext("IPv4 compatible in IPv6 address not valid"));
				return (0);
			}
		}
	} else {
		/* invalid family */
		yyerror(gettext("Invalid address family"));
		return (0);
	}

	return (family);
#undef IN6_IS_ADDR_ALLONES
}

/*
 * Handle post-defaults, complain about things that can't be handled.
 */
int
rulebase_add(struct ike_rulebase *rbp, struct ike_rule *rp)
{
	int i, rc;
	sa_family_t family;

	if (rp->label == NULL) {
		yyerror(gettext("rule requires a label"));
		return (1);
	}
	if (rp->local_addr.num_includes == 0) {
		if (nullrule.local_addr.num_includes == 0) {
			yyerror(gettext("rule doesn't specify local address"));
			return (1);
		}
		(void) addrspec_dup(&rp->local_addr, &nullrule.local_addr);
	}
	if (rp->remote_addr.num_includes == 0) {
		if (nullrule.remote_addr.num_includes == 0) {
			yyerror(gettext("rule doesn't specify remote address"));
			return (1);
		}
		(void) addrspec_dup(&rp->remote_addr, &nullrule.remote_addr);
	}

	/* Verify validity of addresses */
	if ((family = verify_ike_addrspec(&rp->local_addr)) == 0) {
		return (1);
	}
	if (verify_ike_addrspec(&rp->remote_addr) != family) {
		return (1);
	}

	if (rp->mode == 0) {
		if (nullrule.mode == 0)
			rp->mode = SSH_IKE_XCHG_TYPE_ANY;
		else
			rp->mode = nullrule.mode;
	}
	if (rp->p1_nonce_len < 1) {
		rp->p1_nonce_len = nullrule.p1_nonce_len;
	}
	/*
	 * Set default Phase II lifetimes in seconds and bytes.
	 */

	check_rule(&nullrule, B_FALSE);

	if (rp->p2_lifetime_secs == 0)
		rp->p2_lifetime_secs = nullrule.p2_lifetime_secs;
	if (rp->p2_softlife_secs == 0)
		rp->p2_softlife_secs = nullrule.p2_softlife_secs;
	if (rp->p2_idletime_secs == 0)
		rp->p2_idletime_secs = nullrule.p2_idletime_secs;
	if (rp->p2_lifetime_kb == 0)
		rp->p2_lifetime_kb = nullrule.p2_lifetime_kb;
	if (rp->p2_softlife_kb == 0)
		rp->p2_softlife_kb = nullrule.p2_softlife_kb;

	check_rule(rp, B_TRUE);

	/*
	 * It's not required to define p2_softlife_{secs,kb} in the config
	 * file, so ike_defs.rule_p2_softlife_{secs,kb} can be zero, if this
	 * is the case, set this to the derived value so that
	 * 'ikeadm get defaults' works correctly.
	 */
	if (ike_defs.rule_p2_softlife_secs == 0)
		ike_defs.rule_p2_softlife_secs = rp->p2_softlife_secs;
	if (ike_defs.rule_p2_softlife_kb == 0)
		ike_defs.rule_p2_softlife_kb = rp->p2_softlife_kb;
	/* The same for p2_idletime_secs */
	if (ike_defs.rule_p2_idletime_secs == 0)
		ike_defs.rule_p2_idletime_secs = rp->p2_idletime_secs;

	if (rp->p2_nonce_len < 1) {
		rp->p2_nonce_len = nullrule.p2_nonce_len;
	}
	if (rp->num_xforms < 1) {
		if (nullrule.num_xforms < 1) {
			yyerror(gettext(
			    "rule doesn't specify any phase 1 transforms"));
			return (1);
		}

		if (!p1_xforms_dup(rp, &nullrule))
			/* p1_xforms_dup() sets errno appropriately */
			return (1);

		if (xformchk(rp))
			/* xformchk() sets errno appropriately */
			return (1);
	}

	if (xformpreshared(rp)) {
		if (rp->local_idtype == 0) {
			rp->local_idtype = IPSEC_ID_IPV4_ADDR;
		} else if (!idtypeadr(rp->local_idtype)) {
			yyerror(gettext(
			    "preshared key authentication requires identity "
			    "type to be IP"));
			return (1);
		}

		if (rp->local_id.num_includes > 0 ||
		    rp->remote_id.num_includes > 0) {
			yyerror(gettext(
			    "preshared key authentication doesn't allow "
			    "identity specification"));
			return (1);
		}
	} else {
		if (rp->local_idtype == 0) {
			if (nullrule.local_idtype == 0) {
				yyerror(gettext(
				    "rule doesn't specify local id type"));
				return (1);
			}
			rp->local_idtype = nullrule.local_idtype;
		}
		if (rp->local_id.num_includes == 0) {
			if (rp->local_id.num_excludes > 0 ||
			    nullrule.local_id.num_includes == 0) {
				yyerror(gettext(
				    "rule doesn't specify local identity"));
				return (1);
			}
			(void) certspec_dup(&rp->local_id, &nullrule.local_id);
		}
		if (rp->remote_id.num_includes == 0) {
			if (rp->remote_id.num_excludes > 0 ||
			    nullrule.remote_id.num_includes == 0) {
				yyerror(gettext(
				    "rule doesn't specify remote identity"));
				return (1);
			}
			(void) certspec_dup(&rp->remote_id, &nullrule.remote_id);
		}
	}

	/*
	 * Make sure that the rule label is not a duplicate.  This
	 * code block only checks the current set of rules that we
	 * are dealing with.  When reading in a file, it will be
	 * all rules, but when doing command line operations, there
	 * will only be one rule here at a time, so we need to also
	 * check when doing an append.
	 */

	for (i = 0; i < rbp->num_rules; i++) {
		if (strcmp(rp->label, rbp->rules[i]->label) == 0) {
			yyerror(gettext("rule label not unique"));
			errno = EEXIST;
			return (1);
		}
	}

	/*
	 * At this point, it's a legitimate rule to be appended to the
	 * specified rulebase.  Also make sure there's an entry in the
	 * kmcookie file, inserting a new one if needed. This last step
	 * is skipped if the config is just being checked, the kmcookie
	 * file may already be in use by a running daemon.
	 */

	if (!cflag) {
		if ((rc = kmc_insert_mapping(rp->label)) == -1) {
			int	rtnerr = errno;
			PRTDBG(D_CONFIG, ("Failed to add rule '%s'; cannot add "
			    "to kmcookie file!", rp->label));
			errno = rtnerr;
			return (1);
		}
		rp->cookie = (uint32_t)rc;
	}
	if (rbp->num_rules == rbp->allocnum_rules) {
		/* Allocate 16 rules at a time */
		void *newlist = realloc(rbp->rules,
		    (rbp->allocnum_rules + 16) * sizeof (struct ike_rule *));
		if (newlist == NULL) {
			yyerror(gettext("no more memory for rules"));
			/*
			 * yyerror() assumes a parsing error and sets
			 * errno to EINVAL; overwrite that for this case.
			 */
			errno = ENOMEM;
			return (1);
		}
		rbp->rules = newlist;
		/* make sure all the pointers are null */
		for (i = 0; i < 16; i++)
			rbp->rules[rbp->allocnum_rules + i] = NULL;
		rbp->allocnum_rules += 16;
	}
	rbp->rules[rbp->num_rules++] = rp;
	rp->refcount = 1;

	PRTDBG(D_CONFIG,
	    ("Adding rule \"%s\" to IKE configuration;", rp->label));
	PRTDBG(D_CONFIG, ("  mode %d (%s), cookie %u, slot %u; total rules %u",
	    rp->mode, ike_xchg_type_to_string(rp->mode), rp->cookie,
	    rbp->num_rules - 1, rbp->num_rules));

	return (0);
}

/*
 * "Free" the allocated data in a rulebase.  If this rulebase is the
 * only reference to a rule, go ahead and free it; otherwise, just
 * decrement the rule's refcount.
 */
void
rulebase_datafree(struct ike_rulebase *x)
{
	int	i;

	for (i = 0; i < x->allocnum_rules; i++) {
		if (x->rules[i] == NULL)
			continue;
		if (--(x->rules[i]->refcount) == 0)
			rule_free(x->rules[i]);
		x->rules[i] = NULL;
	}
	free(x->rules);
	x->rules = NULL;
	x->num_rules = 0;
	x->allocnum_rules = 0;
}

/*
 * Take the passed in rulebase and make it the active one for the daemon
 * (well, actually, move its data to the active rulebase).  The current
 * active rulebase (global "rules") must be cleared out first!
 *
 * SAs using the rules that are being deleted will not be torn down.  If
 * such SAs exist, the individual rule info won't be deleted here, because
 * its refcount will be > 1.  When the negotiation info is deleted later,
 * and refcount goes to 0, the rule will eventually be freed.
 */
static void
rulebase_makeactive(struct ike_rulebase *new)
{
	rulebase_datafree(&rules);
	rules.rules = new->rules;
	new->rules = NULL;
	rules.num_rules = new->num_rules;
	new->num_rules = 0;
	rules.allocnum_rules = new->allocnum_rules;
	new->allocnum_rules = 0;
}

/*
 * Append the rules in the src rulebase to those in the dst rulebase.
 * NOTE: this is destructive to src.  It will have no data upon
 * successful return.
 */
static int
rulebase_append(struct ike_rulebase *dst, struct ike_rulebase *src)
{
	int	i, j, total;

	if (src->num_rules == 0) {
		/* nothing to append! */
		return (0);
	}

	/* do we have enough space? */
	total = dst->num_rules + src->num_rules;
	if (total > dst->allocnum_rules) {
		void *newlist = realloc(dst->rules,
		    (total * sizeof (struct ike_rule *)));
		if (newlist == NULL) {
			PRTDBG(D_CONFIG, ("no more memory for rules"));
			errno = ENOMEM;
			return (-1);
		}
		dst->rules = newlist;
		for (i = dst->allocnum_rules; i < total; i++)
			dst->rules[i] = NULL;
		dst->allocnum_rules = total;
	}

	for (i = 0; i < src->num_rules; i++) {
		/* XXX - Does not scale well for large numbers of IKE rules */
		for (j = 0; j < dst->num_rules; j++) {
			if (strcmp(src->rules[i]->label,
			    dst->rules[j]->label) == 0) {
				PRTDBG(D_CONFIG, ("  Rule label not unique"));
				errno = EEXIST;
				return (-1);
			}
		}
		dst->rules[dst->num_rules++] = src->rules[i];
		src->rules[i]->refcount++;
	}

	rulebase_datafree(src);

	return (0);
}
/*
 * Handle post-defaults, complain about things that can't be handled.
 */
static int
xform_add(struct ike_rule *rp, struct ike_xform *xp)
{
	void *newlist;
	unsigned global_p1_lifetime_secs;

	if (xp->encr_alg == NULL) {
		yyerror(gettext("phase 1 transform must specify an encr algorithm"));
		return (1);
	}
	if (xp->auth_alg == NULL) {
		yyerror(gettext("phase 1 transform must specify an auth algorithm"));
		return (1);
	}
	if (xp->oakley_group == 0) {
		yyerror(gettext("phase 1 transform must specify a Oakley group"));
		return (1);
	}
	if (xp->auth_method == 0) {
		yyerror(gettext("phase 1 transform must specify an auth method"));
		return (1);
	}

	if (_p1_lifetime_secs == 0)
		global_p1_lifetime_secs = DEF_P1_LIFETIME;
	else
		global_p1_lifetime_secs = _p1_lifetime_secs;

	/* Inherit from global if not set */
	if (xp->p1_lifetime_secs == 0)
		xp->p1_lifetime_secs = global_p1_lifetime_secs;

	/* Check for too small values */
	if (xp->p1_lifetime_secs < MIN_P1_LIFETIME) {
		PRTDBG(D_OP, ("phase 1 lifetime less than minimum (%d < %d) "
		    "in rule \"%s\"", xp->p1_lifetime_secs, MIN_P1_LIFETIME,
		    rp->label));
		PRTDBG(D_OP, ("  -> setting to minimum value"));
		xp->p1_lifetime_secs = MIN_P1_LIFETIME;
	}

	newlist =
	    realloc(rp->xforms,
	    (rp->num_xforms+1) * sizeof (struct ike_xform *));
	if (newlist == NULL) {
		yyerror(gettext("no more memory for rules"));
		return (1);
	}
	rp->xforms = newlist;
	rp->xforms[rp->num_xforms++] = xp;

	return (0);
}


static int
append_spec(const char ***arrayp, int *countp, const char *newspec)
{
	const char **array = *arrayp;
	int count = *countp;
	const char *s = strdup(newspec);

	if (s == NULL)
		return (1);
	array = realloc(array, sizeof (char *) * ++count);
	if (array == NULL) {
		free((void *)s);
		return (1);
	}

	array[count-1] = s;
	*countp = count;
	*arrayp = array;
	return (0);
}

static int
certspec_add(struct certlib_certspec *csp, const char *s)
{
	if (s[0] == '!')
		return (append_spec(&csp->excludes, &csp->num_excludes, s+1));
	else
		return (append_spec(&csp->includes, &csp->num_includes, s));
}

static void
CERTSPEC_INIT(struct certlib_certspec *csp)
{
	int i;

	assert(csp->includes != NULL || csp->num_includes == 0);
	assert(csp->excludes != NULL || csp->num_excludes == 0);

	for (i = 0; i < csp->num_includes; i++)
		free((void *)csp->includes[i]);

	for (i = 0; i < csp->num_excludes; i++)
		free((void *)csp->excludes[i]);

	csp->num_includes = 0;
	csp->num_excludes = 0;
}

static void
RULEBASE_INIT(struct ike_rulebase *rbp)
{
	if (rbp->rules == NULL)
		rbp->rules = calloc(0, sizeof (*rbp->rules));
	rbp->num_rules = 0;
	rbp->allocnum_rules = 0;
}

static void
_dump(void)
{
#ifdef	DEBUG
	certspec_dump(&requested_certs, "requested_certs");
	certspec_dump(&root_certs, "root_certs");
	certspec_dump(&trusted_certs, "trusted_certs");
	printf("default p1_lifetime_secs == %u\n", _p1_lifetime_secs);
	rule_dump(&nullrule, "default (rule) settings");
	rulebase_dump(&rules_ld, "configured");
#else
	yyerror(gettext("illegal use of reserved word D_U_M_P"));
#endif	/* DEBUG */
}

/*
 * Initialise anything that could be changed in the config file to
 * the default, these get reset when the configuration is reinitialised.
 * Remember that a configuration token may be removed before
 * reinitialisation, so make sure defaults are used.
 */
static void
_initial(void)
{
	max_certs = DEFAULT_MAX_CERTS;
	use_http = B_FALSE;
	ignore_crls = B_FALSE;

	/* initialize global rule */
	nullrule.p1_nonce_len = DEF_NONCE_LENGTH;
	nullrule.p2_lifetime_secs = 0;
	nullrule.p2_nonce_len = DEF_NONCE_LENGTH;
	nullrule.p2_softlife_secs = 0;
	nullrule.p2_idletime_secs = 0;
	nullrule.p2_lifetime_kb = 0;
	nullrule.p2_softlife_kb = 0;
	nullrule.p2_pfs = 0;

	/* initialize defaults structure */
	bzero(&ike_defs, sizeof (ike_defaults_t));
}

static void
_final(void)
{
#ifdef	DEBUG
	printf("=========================  final dump  =========================\n");
	_dump();
#endif	/* DEBUG */
}

extern FILE *yyin;

/*
 * The caller has the option of passing either a filename or a file desc-
 * riptor to identify the config file to be read.  If configfile is non-NULL,
 * it will be used (and configfd is ignored); otherwise, configfd is used.
 */
int
config_update(const char *configfile, int configfd)
{
	int rtn, rtnerr;

	if (configfile != NULL)
		yyin = fopen(configfile, "r");
	else
		yyin = fdopen(configfd, "r");
	if (yyin != NULL) {
		rtn = yyparse();
		rtnerr = errno;
		(void) fclose(yyin);
		/* in case yyparse didn't set errno... */
		if (rtn != 0 && rtnerr == 0)
			rtnerr = EINVAL;
	} else {
		rtn = -1;
		rtnerr = errno;
		PRTDBG(D_OP, ("Failed to open config file %s",
		    (configfile == NULL) ? "" : configfile));
	}

	/* cookie file updates are done as each rule is read... */

	errno = rtnerr;
	return (rtn);
}

/*
 * This function tweaks the global state; therefore, make sure you're
 * protected by the event loop lock when calling it.  The only exception
 * to this is when it's called at startup, when the process is still
 * single-threaded (neither the event loop nor the door server has been
 * kicked off), so the lock is not necessary.
 */
int
config_load(const char *configfile, int configfd, boolean_t replace)
{
	int		rtn, rtnerr;

	CERTSPEC_INIT(&requested_certs_ld);
	CERTSPEC_INIT(&root_certs_ld);
	CERTSPEC_INIT(&trusted_certs_ld);
	RULEBASE_INIT(&rules_ld);

	PRTDBG(D_CONFIG, ("Loading configuration..."));
	rtn = config_update(configfile, configfd);

	if (rtn != 0) {
		/*
		 * the update failed; leave the old dbs as-is, and free
		 * the newly-loaded (invalid) dbs.  Also save the errno
		 * generated by the config_update() failure.
		 */
		rtnerr = errno;
		PRTDBG(D_CONFIG, ("Configuration update failed, active "
		    "databases won't be changed"));
		certspec_datafree(&requested_certs_ld);
		certspec_datafree(&root_certs_ld);
		certspec_datafree(&trusted_certs_ld);
		rulebase_datafree(&rules_ld);
		goto fail;
	} else {
		/*
		 * everything worked, make newly-loaded copies active
		 */
		PRTDBG(D_CONFIG, ("Configuration update succeeded! Updating "
		    "active databases."));
		if (replace) {
			certspec_move(&requested_certs, &requested_certs_ld);
			certspec_move(&root_certs, &root_certs_ld);
			certspec_move(&trusted_certs, &trusted_certs_ld);
			rulebase_makeactive(&rules_ld);
		} else {
			/*
			 * the append functions empty out the source
			 * structures, so no memory needs to be freed.
			 */
			if ((rtn = certspec_append(&requested_certs,
			    &requested_certs_ld)) < 0) {
				rtnerr = errno;
				goto fail;
			}
			if ((rtn = certspec_append(&root_certs,
			    &root_certs_ld)) < 0) {
				rtnerr = errno;
				goto fail;
			}
			if ((rtn = certspec_append(&trusted_certs,
			    &trusted_certs_ld)) < 0) {
				rtnerr = errno;
				goto fail;
			}
			if ((rtn = rulebase_append(&rules, &rules_ld)) < 0) {
				rtnerr = errno;
				goto fail;
			}
		}
	}
	PRTDBG(D_OP, ("Configuration ok."));
	return (0);

fail:
	PRTDBG(D_OP, ("Configuration error."));
	PRTDBG(D_CONFIG, ("Database update failed loading configuration"));

	if (ignore_errors) {
		EXIT_DEGRADE("Configuration error. To ensure correct "
		    "operation of in.iked, use ikeadm(1M) to configure the "
		    "running daemon.");
	} else {
		EXIT_BADCONFIG("Configuration error. Fix the configuration "
		    "and clear maintenance state using svcadm(1M).");
	}
	errno = rtnerr;
	return (rtn);
}

void
idtype_write(char *prefix, SshIkeIpsecIdentificationType type, FILE *ofile)
{
	(void) fprintf(ofile, "%s\t%s\n", prefix, sshidtype_to_string(type));
}

void
mode_write(char *prefix, SshIkeExchangeType mode, FILE *ofile)
{
	/*
	 * If the exchange type is unspecified, don't print anything.
	 * Also make the simplifying assumption that the only other
	 * possible values are aggressive and main (id protect), since
	 * those are the only values the grammar recognizes.
	 */
	if (mode == SSH_IKE_XCHG_TYPE_ANY)
		return;

	(void) fprintf(ofile, "%s\t%s\n", prefix,
	    (mode == SSH_IKE_XCHG_TYPE_IP) ? "main" : "aggressive");
}

void
addrrange_write(char *prefix, struct ike_addrrange *ar, FILE *ofile)
{
	int			af = ar->beginaddr.ss.ss_family;
	struct sockaddr_in	*bsin, *esin;
	struct sockaddr_in6	*bsin6, *esin6;
	uint8_t			*beg, *end;
	char			beg_buf[INET6_ADDRSTRLEN];
	char			end_buf[INET6_ADDRSTRLEN];
	boolean_t		do_both;

	switch (af) {
	case AF_INET:
		bsin = &ar->beginaddr.v4;
		esin = &ar->endaddr.v4;
		beg = (uint8_t *)&bsin->sin_addr;
		end = (uint8_t *)&esin->sin_addr;
		do_both = (bsin->sin_addr.s_addr != esin->sin_addr.s_addr);
		break;
	case AF_INET6:
		bsin6 = &ar->beginaddr.v6;
		esin6 = &ar->endaddr.v6;
		beg = (uint8_t *)&bsin6->sin6_addr;
		end = (uint8_t *)&esin6->sin6_addr;
		do_both = !IN6_ARE_ADDR_EQUAL(&bsin6->sin6_addr,
		    &esin6->sin6_addr);
		break;
	default:
		return;
	}

	if ((inet_ntop(af, beg, beg_buf, INET6_ADDRSTRLEN) == NULL) ||
	    (do_both && inet_ntop(af, end, end_buf, INET6_ADDRSTRLEN)
	    == NULL)) {
		PRTDBG(D_CONFIG, ("Error converting address!"));
		return;
	} else {
		if (do_both) {
			(void) fprintf(ofile, "%s\t%s - %s\n", prefix, beg_buf,
			    end_buf);
		} else {
			(void) fprintf(ofile, "%s\t%s\n", prefix, beg_buf);
		}
	}
}

void
addrspec_write(char *prefix, struct ike_addrspec *as, FILE *ofile)
{
	int	i;

	for (i = 0; i < as->num_includes; i++) {
		if (as->includes[i] == NULL)
			continue;
		addrrange_write(prefix, as->includes[i], ofile);
	}
}

void
certspec_write(char *prefix, struct certlib_certspec *cs, FILE *ofile)
{
	int	i;

	for (i = 0; i < cs->num_includes; i++) {
		if (cs->includes[i] == NULL)
			continue;
		(void) fprintf(ofile, "%s\t\"%s\"\n", prefix, cs->includes[i]);
	}

	for (i = 0; i < cs->num_excludes; i++) {
		if (cs->excludes[i] == NULL)
			continue;
		(void) fprintf(ofile, "%s\t\"!%s\"\n", prefix, cs->excludes[i]);
	}
}

/*
 * This might be better off in getssh.c...but the ssh code does not
 * define string constants for the auth meth values, as it does for
 * the auth and encr algs; so these strings are only meaningful in
 * the context of the config file grammar parser.  So we include the
 * conversion function here.
 */
static keywdtab_t authmethtab[] = {
	{ SSH_IKE_VALUES_AUTH_METH_DSS_SIGNATURES,	"dss_sig" },
	{ SSH_IKE_VALUES_AUTH_METH_RSA_ENCRYPTION_REVISED,
	    "improved_rsa_encrypt" },
	{ SSH_IKE_VALUES_AUTH_METH_PRE_SHARED_KEY,	"preshared" },
	{ SSH_IKE_VALUES_AUTH_METH_RSA_ENCRYPTION,	"rsa_encrypt" },
	{ SSH_IKE_VALUES_AUTH_METH_RSA_SIGNATURES,	"rsa_sig" },
};

char *
authmeth_to_string(SshIkeAttributeAuthMethValues val)
{
	keywdtab_t	*tp;

	for (tp = authmethtab; tp < A_END(authmethtab); tp++) {
		if (val == tp->kw_tag)
			return (tp->kw_str);
	}
	return ("");
}

void
xforms_write(struct ike_xform **xf, int count, FILE *ofile)
{
	int	i;

	for (i = 0; i < count; i++) {
		if (xf[i] == NULL)
			continue;
		(void) fprintf(ofile, "\tp1_xform { "
		    "encr_alg %s "
		    "auth_alg %s "
		    "oakley_group %u "
		    "auth_method %s "
		    "p1_lifetime_secs %u }\n",
		    sshencr_to_string(xf[i]->encr_alg),
		    sshauth_to_string(xf[i]->auth_alg),
		    xf[i]->oakley_group,
		    authmeth_to_string(xf[i]->auth_method),
		    xf[i]->p1_lifetime_secs);
	}
}

void
rule_write(struct ike_rule *rp, FILE *ofile)
{
	(void) fprintf(ofile, "{\n\tlabel\t\"%s\"\n", rp->label);
	idtype_write("\tlocal_id_type", rp->local_idtype, ofile);
	mode_write("\tp1_mode", rp->mode, ofile);
	addrspec_write("\tlocal_addr", &rp->local_addr, ofile);
	addrspec_write("\tremote_addr", &rp->remote_addr, ofile);
	certspec_write("\tlocal_id", &rp->local_id, ofile);
	certspec_write("\tremote_id", &rp->remote_id, ofile);
	(void) fprintf(ofile, "\tp2_lifetime_secs\t%u\n", rp->p2_lifetime_secs);
	(void) fprintf(ofile, "\tp2_softlife_secs\t%u\n", rp->p2_softlife_secs);
	(void) fprintf(ofile, "\tp2_idletime_secs\t%u\n", rp->p2_idletime_secs);
	(void) fprintf(ofile, "\tp2_lifetime_kb\t%u\n", rp->p2_lifetime_kb);
	(void) fprintf(ofile, "\tp2_softlife_kb\t%u\n", rp->p2_softlife_kb);
	(void) fprintf(ofile, "\tp2_pfs\t%u\n", rp->p2_pfs);
	xforms_write(rp->xforms, rp->num_xforms, ofile);
	(void) fprintf(ofile, "}\n");
}

/* return -1 on fail, number of rules written on success */
int
config_write(int configfd)
{
	int	i, written = 0;
	FILE	*ofile;

	if ((ofile = fdopen(configfd, "w+")) == NULL)
		return (-1);

	/* write global certspec info (requested, root, trusted) */

	/* write other global variables */

	for (i = 0; i < rules.num_rules; i++) {
		/* don't write deleted or dead rules... */
		if ((rules.rules[i] == NULL) ||
		    (rules.rules[i]->label == NULL))
			continue;
		rule_write(rules.rules[i], ofile);
		written++;
	}
	(void) fclose(ofile);

	return (written);
}

static int
idtypeadr(SshIkeIpsecIdentificationType idtype)
{
	switch (idtype) {
	case IPSEC_ID_IPV4_ADDR:
	case IPSEC_ID_IPV4_ADDR_SUBNET:
	case IPSEC_ID_IPV4_ADDR_RANGE:
	case IPSEC_ID_IPV6_ADDR:
	case IPSEC_ID_IPV6_ADDR_SUBNET:
	case IPSEC_ID_IPV6_ADDR_RANGE:
		return (1);
	}
	return (0);
}


/*
 * N.B.: we must actually clone the xform, identity, and address structures from
 * the current default rule, rather than merely copying the pointers, because
 * the default can be augmented after this rule and we don't want those
 * changes affecting prior rules.
 */
static boolean_t
p1_xforms_dup(struct ike_rule *dst, struct ike_rule *src)
{
	struct ike_xform *xfP;
	unsigned ix;
	struct ike_xform **newlist;

	newlist = TNEW(struct ike_xform *, 0, src->num_xforms);
	if (newlist == NULL) {
		yyerror(gettext("no more memory for phase 1 transforms"));
		errno = ENOMEM;
		return (B_FALSE);
	}

	for (ix = 0; ix < src->num_xforms; ++ix) {
		xfP = TNEW(struct ike_xform, 0, 1);
		if (xfP == NULL) {
			yyerror(gettext(
			    "no more memory for phase 1 transforms"));
			free(newlist);
			errno = ENOMEM;
			return (B_FALSE);
		}
		*xfP = *src->xforms[ix];
		newlist[ix] = xfP;
	}

	dst->xforms = newlist;
	dst->num_xforms = src->num_xforms;

	return (B_TRUE);
}

struct ike_rule *
rule_dup(struct ike_rule *s)
{
	struct ike_rule	*n;

	n = malloc(sizeof (struct ike_rule));

	if (n != NULL) {
		n->refcount = 0;
		n->label = strdup(s->label);
		if (n->label == NULL) {
			free(n);
			return (NULL);
		}
		n->cookie = s->cookie;
		(void) addrspec_dup(&n->local_addr, &s->local_addr);
		(void) addrspec_dup(&n->remote_addr, &s->remote_addr);
		(void) certspec_dup(&n->local_id, &s->local_id);
		(void) certspec_dup(&n->remote_id, &s->remote_id);
		n->local_idtype = s->local_idtype;
		n->mode = s->mode;
		if (!p1_xforms_dup(n, s)) {
			rule_free(n);
			return (NULL);
		}
		n->p1_nonce_len = s->p1_nonce_len;
		n->p2_lifetime_secs = s->p2_lifetime_secs;
		n->p2_softlife_secs = s->p2_softlife_secs;
		n->p2_idletime_secs = s->p2_idletime_secs;
		n->p2_lifetime_kb = s->p2_lifetime_kb;
		n->p2_softlife_kb = s->p2_softlife_kb;
		n->p2_nonce_len = s->p2_nonce_len;
		n->p2_pfs = s->p2_pfs;

		n->outer_bslabel = s->outer_bslabel;
		n->outer_label = s->outer_label;
		n->outer_ucred = s->outer_ucred;
	}

	return (n);
}

void
rule_free(struct ike_rule *x)
{
	int	i;

	if (--(x->refcount) > 0)
		return;

	if (x->label != NULL)
		free(x->label);

	addrspec_datafree(&x->local_addr);
	addrspec_datafree(&x->remote_addr);

	certspec_datafree(&x->local_id);
	certspec_datafree(&x->remote_id);

	for (i = 0; i < x->num_xforms; i++) {
		if (x->xforms[i] != NULL)
			free(x->xforms[i]);
	}
	free(x->xforms);

	free(x);
}

static int
certspec_dup(struct certlib_certspec *dst, const struct certlib_certspec *src)
{
	unsigned ix;
	const char **newlist;

	if (dst == NULL)
		return (1);

	/* if src is null or empty, make dst empty and return */
	if ((src == NULL) ||
	    ((src->includes == NULL) && (src->excludes == NULL))) {
		dst->includes = NULL;
		dst->num_includes = 0;
		dst->excludes = NULL;
		dst->num_excludes = 0;
		return (0);
	}

	newlist = TNEW(const char *, 0, src->num_includes);
	if (newlist == NULL) {
		yyerror(gettext("no more memory for rules"));
		return (1);
	}

	for (ix = 0; ix < src->num_includes; ++ix) {
		const char *ns = strdup(src->includes[ix]);
		if (ns == NULL) {
			yyerror(gettext("no more memory for rules"));
			free(newlist);
			return (1);
		}
		newlist[ix] = ns;
	}

	dst->includes = newlist;
	dst->num_includes = src->num_includes;

	newlist = TNEW(const char *, 0, src->num_excludes);
	if (newlist == NULL) {
		yyerror(gettext("no more memory for rules"));
		return (1);
	}

	for (ix = 0; ix < src->num_excludes; ++ix) {
		const char *ns = strdup(src->excludes[ix]);
		if (ns == NULL) {
			yyerror(gettext("no more memory for rules"));
			free(newlist);
			return (1);
		}
		newlist[ix] = ns;
	}

	dst->excludes = newlist;
	dst->num_excludes = src->num_excludes;

	return (0);
}

/*
 * Move source into dest by simply copying pointers; don't alloc
 * any memory.  Also make sure dst is empty before doing the copy,
 * and clear out src after, to avoid alloc'd blobs with no pointer
 * (if dst isn't cleaned out) and duplicate pointers to the same
 * blob (if src isn't cleaned out).
 */
static void
certspec_move(struct certlib_certspec *dst, struct certlib_certspec *src)
{
	int i;

	if (dst->includes != NULL) {
		for (i = 0; i < dst->num_includes; i++)
			free((void *)dst->includes[i]);
		free(dst->includes);
	}

	dst->includes = src->includes;
	src->includes = NULL;
	dst->num_includes = src->num_includes;
	src->num_includes = 0;

	if (dst->excludes != NULL) {
		for (i = 0; i < dst->num_excludes; i++)
			free((void *)dst->excludes[i]);
		free(dst->excludes);
	}
	dst->excludes = src->excludes;
	src->excludes = NULL;
	dst->num_excludes = src->num_excludes;
	src->num_excludes = 0;
}

/*
 * Append the includes and excludes from src to dst's arrays.  NOTE: this
 * is destructive to src.  It will have no data upon successful return.
 */
static int
certspec_append(struct certlib_certspec *dst, struct certlib_certspec *src)
{
	int	i, total;
	void	*newlist;

	if (src->num_includes > 0) {
		total = dst->num_includes + src->num_includes;
		newlist = realloc(dst->includes, (total * sizeof (char *)));
		if (newlist == NULL) {
			PRTDBG(D_CONFIG, ("no more memory for certspec"));
			errno = ENOMEM;
			return (-1);
		}
		dst->includes = newlist;
		for (i = 0; i < src->num_includes; i++) {
			dst->includes[dst->num_includes++] = src->includes[i];
			src->includes[i] = NULL;
			src->num_includes--;
		}
	}

	if (src->num_excludes > 0) {
		total = dst->num_excludes + src->num_excludes;
		newlist = realloc(dst->excludes, (total * sizeof (char *)));
		if (newlist == NULL) {
			PRTDBG(D_CONFIG, ("no more memory for certspec"));
			errno = ENOMEM;
			return (-1);
		}
		dst->excludes = newlist;
		for (i = 0; i < src->num_excludes; i++) {
			dst->excludes[dst->num_excludes++] = src->excludes[i];
			src->excludes[i] = NULL;
			src->num_excludes--;
		}
	}

	certspec_datafree(src);

	return (0);
}

/* frees all the alloc'd memory in a certspec, but not the certspec itself */
static void
certspec_datafree(struct certlib_certspec *x)
{
	unsigned	i;

	for (i = 0; i < x->num_includes; ++i) {
		if (x->includes[i] != NULL)
			free((void *)x->includes[i]);
	}
	free(x->includes);
	x->includes = NULL;
	x->num_includes = 0;

	for (i = 0; i < x->num_excludes; ++i) {
		if (x->excludes[i] != NULL)
			free((void *)x->excludes[i]);
	}
	free(x->excludes);
	x->excludes = NULL;
	x->num_excludes = 0;
}

static int
addrspec_dup(struct ike_addrspec *dst, const struct ike_addrspec *src)
{
	unsigned ix;
	struct ike_addrrange **newlist;

	if (src == NULL) {
		dst = NULL;
		return (0);
	}

	newlist = TNEW(struct ike_addrrange *, 0, src->num_includes);
	if (newlist == NULL) {
		yyerror(gettext("no more memory for rules"));
		return (1);
	}

	for (ix = 0; ix < src->num_includes; ++ix) {
		struct ike_addrrange *nr = TNEW(struct ike_addrrange, 0, 1);
		if (nr == NULL) {
			yyerror(gettext("no more memory for rules"));
			free(newlist);
			return (1);
		}
		*nr = *(src->includes[ix]);
		newlist[ix] = nr;
	}

	dst->includes = newlist;
	dst->num_includes = src->num_includes;

	return (0);
}

/* frees all the alloc'd memory in a addrspec, but not the addrspec itself */
static void
addrspec_datafree(struct ike_addrspec *x)
{
	unsigned	i;

	for (i = 0; i < x->num_includes; ++i) {
		if (x->includes[i] != NULL)
			free(x->includes[i]);
	}
	free(x->includes);

	x->includes = NULL;
	x->num_includes = 0;
}

static int
rulechk(int wantsrule)
{
	if (ruleP == &nullrule) {
		if (!wantsrule)
			return (0);
		yyerror(gettext("cannot appear outside rule"));
	} else if (wantsrule) {
		return (0);
	} else {
		yyerror(gettext("cannot appear inside rule"));
	}
	return (1);		/* error */
}

static int
ruleaddrchk(struct ike_addrspec *iasp, const struct ike_addrrange *iarp)
{
	void *newlist;
	struct ike_addrrange *newitem;

	newlist = realloc(iasp->includes,
	    (iasp->num_includes+1) * sizeof (struct ike_addrrange *));
	newitem = TNEW(struct ike_addrrange, 0, 1);
	if (newlist == NULL || newitem == NULL) {
		if (newlist != NULL)
			free(newlist);
		if (newitem != NULL)
			free(newitem);
		yyerror(gettext("no more memory for rules"));
		return (1);
	}
	*newitem = *iarp;

	iasp->includes = newlist;
	iasp->includes[iasp->num_includes++] = newitem;

	return (0);
}

static int
ruleidchk(struct certlib_certspec *csp, const char *newspec)
{
	if (certspec_add(csp, newspec)) {
		yyerror(gettext("no more memory for rules"));
		return (1);
	}
	return (0);
}

static int
xformchk(struct ike_rule *rp)
{
	unsigned ix, preshared = xformpreshared(rp);

	if (preshared) {
		if (rp->remote_id.num_includes > 0 ||
		    rp->remote_id.num_excludes > 0) {
			yyerror(gettext("preshared key authentication "
			    "doen't allow identity specification"));
			return (1);
		}
		if (rp->local_idtype != 0 &&
		    !idtypeadr(rp->local_idtype)) {
			yyerror(gettext("preshared key authentication requires "
			    "identity type to be IP"));
			return (1);
		}
	}

	for (ix = 0; ix < rp->num_xforms; ++ix) {
		if (preshared &&
		    rp->xforms[ix]->auth_method != METHPRESHARED ||
		    !preshared &&
		    rp->xforms[ix]->auth_method == METHPRESHARED) {
			yyerror(gettext("all or no preshared phase 1 "
			    "auth methods"));
			return (1);
		}
	}

	return (0);
}

static int
xformpreshared(struct ike_rule *rp)
{
	return rp->num_xforms > 0 &&
	    rp->xforms[0]->auth_method == METHPRESHARED;
}

static uint8_t
auth_alg_lookup(const char *name)
{
	if (strcasecmp(name, "MD5") == 0)
		return (SSH_IKE_VALUES_HASH_ALG_MD5);
	if (strcasecmp(name, "SHA") == 0 ||
	    strcasecmp(name, "SHA-1") == 0 ||
	    strcasecmp(name, "SHA1") == 0)
		return (SSH_IKE_VALUES_HASH_ALG_SHA);
	if (strcasecmp(name, "SHA256") == 0 ||
	    strcasecmp(name, "SHA-256") == 0)
		return (SSH_IKE_VALUES_HASH_ALG_SHA2_256);
	if (strcasecmp(name, "SHA384") == 0 ||
	    strcasecmp(name, "SHA-384") == 0)
		return (SSH_IKE_VALUES_HASH_ALG_SHA2_384);
	if (strcasecmp(name, "SHA512") == 0 ||
	    strcasecmp(name, "SHA-512") == 0)
		return (SSH_IKE_VALUES_HASH_ALG_SHA2_512);
	return (0);
}

/*
 * Take a string of one of these forms:
 *    alg-name(..)	(NOP == alg-name)
 *    alg-name(..high)
 *    alg-name(low..)
 *    alg-name(low..high)
 * and return char pointers where appropriate.  The caller's subsequent
 * atoi() should stop conversions at the appropriate parts.
 *
 * Return B_FALSE if there's some sort of parsing problem.  The regexp for
 * ALGRANGE in lex.l should be enough for most of this, though.
 *
 * SIDE EFFECT:  *algname is an allocated value that must be free()d by the
 * caller.
 */
static boolean_t
get_alg_parts(const char *string, char **algname, char **low, char **high)
{
	char *current;

	*algname = strdup(string);
	if (*algname == NULL) {
		/* No memory */
		return (B_FALSE);
	}
	current = strchr(*algname, '(');
	assert(current != NULL);
	*current = '\0';
	current++;
	*low = current;
	/* Grab first number, if available. */
	while (isdigit(*current))
		current++;
	assert(*current == '.');
	*current = '\0';	/* Terminates low. */
	assert(*(current + 1) == '.');
	current += 2;
	*high = current;
	/* Grab second number, if available. */
	while (isdigit(*current))
		current++;
	assert(*current == ')');
	*current = '\0';	/* Terminates high. */
	return (B_TRUE);
}

static int
pluck_out_low_high(char *token, int *low, int *high)
{
	int rv, minlow, maxhigh;
	char *algname, *lowstring, *highstring;

	if (!get_alg_parts(token, &algname, &lowstring, &highstring)) {
		yyerror(gettext("malformed or invalid encryption algorithm"));
		rv = -1;
	} else {
		rv = encr_alg_lookup(algname, &minlow, &maxhigh);
		if (*lowstring == '\0')
			*low = minlow;
		else
			*low = atoi(lowstring);
		if (*highstring == '\0')
			*high = maxhigh;
		else
			*high = atoi(highstring);
		if (rv == SSH_IKE_VALUES_ENCR_ALG_AES_CBC) {
			/* AES is special, allow aes(low..high). */
			if (*high < *low) {
				yyerror("malformed encryption key range");
				rv = -1;
				goto bail;
			}
			switch (*low) {
			case 128:
			case 192:
			case 256:
				break;
			default:
				yyerror("Invalid key size for AES");
				rv = -1;
				goto bail;
			}
			switch (*high) {
			case 128:
			case 192:
			case 256:
				break;
			default:
				yyerror("Invalid key size for AES");
				rv = -1;
				goto bail;
			}
		} else {
			/*
			 * The rest aren't.  Disallow unless a new algorithm
			 * comes along, or someone REALLY wants to say
			 * des(64..).  :-P
			 */
			yyerror("Cannot specify key size on non-AES ciphers.");
			rv = -1;
		}
	}
bail:
	free(algname);	/* Can even free() NULL pointers. */
	return (rv);
}

/*
 * Convert a prefix length to a mask.  Doesn't check validity of
 * prefixlen.
 */
static in6_addr_t
ipv6masks(int prefixlen)
{
	in6_addr_t out_sock;
	unsigned char *mask = (unsigned char*)&out_sock.s6_addr;

	(void) memset((void*)mask, 0, sizeof (struct in6_addr));

	while (prefixlen >= 8) {
		*mask++ = 0xff;
		prefixlen -= 8;
	}

	if (prefixlen > 0)
		*mask = (0xff << (8-prefixlen));

	return (out_sock);
}

/*
 * returns true if less <= more.
 * Assumes net order
 */
boolean_t
in6_addr_cmp(in6_addr_t *less, in6_addr_t *more)
{
	uint32_t i;

	for (i = 0; i < 16; i++) {
		if (less->s6_addr[i] < more->s6_addr[i])
			return (B_TRUE);
		if (less->s6_addr[i] > more->s6_addr[i])
			return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Sanity check Oakley Group number. This function only checks to
 * see if the group number is one defined by rfc2409/rfc3526, it
 * does not check for support by libike or PKCS#11.
 */
static boolean_t
group_reality_check(int group)
{
	switch (group) {

	case IKE_GRP_DESC_MODP_768:
	case IKE_GRP_DESC_MODP_1024:
	case IKE_GRP_DESC_EC2N_155:
	case IKE_GRP_DESC_EC2N_185:
	case IKE_GRP_DESC_MODP_1536:
	case IKE_GRP_DESC_MODP_2048:
	case IKE_GRP_DESC_MODP_3072:
	case IKE_GRP_DESC_MODP_4096:
	case IKE_GRP_DESC_MODP_6144:
	case IKE_GRP_DESC_MODP_8192:
	case IKE_GRP_DESC_ECP_256:
	case IKE_GRP_DESC_ECP_384:
	case IKE_GRP_DESC_ECP_521:
	case IKE_GRP_DESC_MODP_1024_160:
	case IKE_GRP_DESC_MODP_2048_224:
	case IKE_GRP_DESC_MODP_2048_256:
	case IKE_GRP_DESC_ECP_192:
	case IKE_GRP_DESC_ECP_224:
		return (B_TRUE);

	default:
		return (B_FALSE);
	}
}

#ifdef	DEBUG

/* debug dumpers */

static void
allocthing_dump(allocthingT *atP, void (*dumpFP)(char *thingP, const char *ctx),
    const char *ctx)
{
	char *cP;
	unsigned ix;
	char ctxbuf[256];
	printf("%s @ 0x%x: %u alloc'ed %u used, %u byte things @ 0x%x\n",
	    ctx, (unsigned)atP, atP->n__alloc, atP->n__used,
	    atP->thing_size, (unsigned)atP->thingsP);
	for (ix = 0, cP = atP->thingsP;
	    ix < atP->n__used; ++ix, cP += atP->thing_size) {
		sprintf(ctxbuf, "%s[%u] @ 0x%x", ctx, ix, (unsigned)cP);
		(*dumpFP)(cP, ctxbuf);
	}
}

static void
addrspec_dump(const struct ike_addrspec *asP, const char *ctx)
{
	unsigned ix;
	struct ike_addrrange *arP;
	if (asP == NULL) {
		printf("%s (nil)\n", ctx);
		return;
	}
	printf("%s @ 0x%x: %u address%s\n",
	    ctx, (unsigned)asP,
	    asP->num_includes, asP->num_includes == 1 ? "" : "es");
	for (ix = 0; ix < asP->num_includes; ++ix) {
		arP = asP->includes[ix];
		printf("\t\t: addr[%u] @ 0x%x: %s",
		    ix, (unsigned)arP, inet_ntoa(arP->beginaddr.v4.sin_addr));
		if (arP->endaddr.v4.sin_family == AF_INET)
			printf(" - %s", inet_ntoa(arP->endaddr.v4.sin_addr));
		printf("\n");
	}
}

static void
certspec_dump(const struct certlib_certspec *ccsP, const char *ctx)
{
	const char *ccP;
	unsigned ix;
	if (ccsP->num_includes == 0) {
		printf("%s (empty)\n", ctx);
		return;
	}
	printf("%s @ 0x%x: %u include%s %u exclude%s\n",
	    ctx, (unsigned)ccsP, ccsP->num_includes,
	    ccsP->num_includes == 1 ? "" : "s", ccsP->num_excludes,
	    ccsP->num_excludes == 1 ? "" : "s");
	for (ix = 0; ix < ccsP->num_includes; ++ix) {
		ccP = ccsP->includes[ix];
		printf("\t\t: include[%u] @ 0x%x == '%s'\n",
		    ix, (unsigned)ccP, ccP?ccP:"(nil)");
	}
	for (ix = 0; ix < ccsP->num_excludes; ++ix) {
		ccP = ccsP->excludes[ix];
		printf("\t\t: exclude[%u] @ 0x%x == '%s'\n",
		    ix, (unsigned)ccP, ccP?ccP:"(nil)");
	}
}

static void
rule_dump(const struct ike_rule *rP, const char *ctx)
{
	unsigned ix;
	char ctxbuf[256];
	printf("%s @ 0x%x: label '%s', local_idtype == %u, mode == %u\n",
	    ctx, (unsigned)rP, rP->label?rP->label:"(nil)",
	    rP->local_idtype, rP->mode);
	sprintf(ctxbuf, "%s local_addr", ctx);
	addrspec_dump(&rP->local_addr, ctxbuf);
	sprintf(ctxbuf, "%s remote_addr", ctx);
	addrspec_dump(&rP->remote_addr, ctxbuf);
	sprintf(ctxbuf, "%s local_id", ctx);
	certspec_dump(&rP->local_id, ctxbuf);
	sprintf(ctxbuf, "%s remote_id", ctx);
	certspec_dump(&rP->remote_id, ctxbuf);
	printf("\t\t: p1_nonce_len == %u\n",
	    rP->p1_nonce_len);
	printf("\t\t: p2_lifetime_secs == %u, p2_softlife_secs == %u\n",
	    rP->p2_lifetime_secs, rP->p2_softlife_secs);
	printf("\t\t:p2_idletime_secs == %u\n", rP->p2_idletime_secs);
	printf("\t\t: p2_lifetime_kb == %u, p2_softlife_kb == %u\n",
	    rP->p2_lifetime_kb, rP->p2_softlife_kb);
	printf("\t\t: p2_nonce_len == %u, p2_pfs == %u\n",
	    rP->p2_nonce_len, rP->p2_pfs);
	printf("\t\t: %u xforms @ 0x%x\n",
	    rP->num_xforms, (unsigned)rP->xforms);
	for (ix = 0; ix < rP->num_xforms; ++ix) {
		sprintf(ctxbuf, "%x xform[%u]", ctx, ix);
		xform_dump(rP->xforms[ix], ctxbuf);
	}
}

static void
rulebase_dump(const struct ike_rulebase *rbP, const char *ctx)
{
	unsigned ix;
	char ctxbuf[256];
	printf("%s rulebase @ 0x%x: %u alloc'ed %u used\n",
	    ctx, (unsigned)rbP, rbP->allocnum_rules, rbP->num_rules);
	for (ix = 0; ix < rbP->num_rules; ++ix) {
		sprintf(ctxbuf, "%s rule[%u]", ctx, ix);
		rule_dump(rbP->rules[ix], ctxbuf);
	}
}

static void
xform_dump(const struct ike_xform *xfP, const char *ctx)
{
	printf("%x @ 0x%x:\n\t\t: auth_alg == '%x', encr_alg == '%x'\n",
	    ctx, (unsigned)xfP,
	    xfP->auth_alg?xfP->auth_alg:234,
	    xfP->encr_alg?xfP->encr_alg:234);
	printf("\t\t: oakley_group == %u, auth_method == %u, "
	    "p1_lifetime_secs == %u\n",
	    xfP->oakley_group, (unsigned)xfP->auth_method,
	    xfP->p1_lifetime_secs);
}

#endif	/* DEBUG */
