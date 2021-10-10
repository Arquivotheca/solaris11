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
 */

/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stropts.h>
#include <stdarg.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <net/if.h>

#include "netinet/ip_fil.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_pool.h"

#include "auth_match.h"
#include "auth_types.h"


#define	YYDEBUG	1

#define	DBG_PRINT(_x_)	printf_wrap _x_

#define	DBG_PRINTIP(_ip_cx_, _pkt_ip_)	do {				\
	    char	_buf_[40];					\
									\
	    printf_wrap("%s/",						\
		inet_ntop(((_ip_cx_)->ipcx_v == 6) ? AF_INET6 : AF_INET,\
		    &(_ip_cx_)->ipcx_addr, _buf_, sizeof (_buf_)));	\
	    printf_wrap("%s and",					\
		inet_ntop(((_ip_cx_)->ipcx_v == 6) ? AF_INET6 : AF_INET,\
		    &(_ip_cx_)->ipcx_mask, _buf_, sizeof (_buf_)));	\
	    printf_wrap("%s\n",						\
		inet_ntop(((_ip_cx_)->ipcx_v == 6) ? AF_INET6 : AF_INET,\
		    &(_pkt_ip_), _buf_, sizeof (_buf_)));		\
} while (0)

#define	AUTH_LAST_INDEX	(AUTH_T_LAST + ((int)AUTH_T_FAIL * -1))

#define	IS_TCPUDP(fin)	\
	(((fin)->fin_p == IPPROTO_UDP) || ((fin)->fin_p == IPPROTO_TCP))

/*
 * MARK_ACTION macro sets result in auth response. It assigns result when
 * fra_pass is 'empty', otherwise it uses bitwise or to set desired flag.
 */
#define	MARK_ACTION(fra, result)	do {				\
		if ((fra)->fra_pass == 0)				\
			(fra)->fra_pass = (result);			\
		else							\
			(fra)->fra_pass |= (result);			\
	} while (0)


/*
 * l1_elem is the element from the l1 list.  l2 list elements will be appended
 * after l1.  Elements in l2 are kept in order.
 * NOTE: after completion l1_elem points to the last element of l2
 */
#define	LIST_CAT(l1_elem, l2_head, field, type)				\
	 while (!LIST_EMPTY(l2_head))  {				\
		struct type	*tmp;					\
		tmp = LIST_FIRST(l2_head);				\
		LIST_REMOVE(tmp, field);				\
		LIST_INSERT_AFTER(l1_elem, tmp, field);			\
		l1_elem = tmp;						\
	}

#define	LIST_SET_HEAD(head, first, field)	do {			\
		(head)->lh_first = (first);				\
		(first)->field.le_prev = &(head)->lh_first;	\
	} while (0)

extern int yyparse(void);
extern int yylex(void);
extern FILE *yyin;
extern int yydebug;

/*
 * List of auth rules.
 */
static LIST_HEAD(auth_rules, auth_rule) Auth_Rules =
    LIST_HEAD_INITIALIZER(Auth_Rules);
static LIST_HEAD(optional_actions, auth_action) Optional_Actions =
    LIST_HEAD_INITIALIZER(Optional_Actions);
static LIST_HEAD(optional_tests, auth_test)	Optional_Tests =
    LIST_HEAD_INITIALIZER(Optional_Tests);
static auth_rule_t	*Last_Inserted = NULL;
/*
 * File descriptor for ippool device
 */
static int		Ippool_Fd = -1;


/*
 * Forward declarations for bison parser
 */
static int printf_wrap(const char *, ...);
static void test_generic_destroy(auth_test_t *);
static ip_test_cx_t * new_ipcx(void);
static void test_nop_destroy(auth_test_t *);
static void mask_addr6(const i6addr_t *, const i6addr_t *, i6addr_t *);
static auth_tresult_t cmp_addr6(i6addr_t *, i6addr_t *);
static auth_tresult_t test_ip(ip_test_cx_t *, i6addr_t *);
static auth_tresult_t test_src_ip(auth_test_t *, frauth_t *);
static auth_tresult_t test_dst_ip(auth_test_t *, frauth_t *);
static auth_tresult_t test_src_port(auth_test_t *, frauth_t *);
static auth_tresult_t test_dst_port(auth_test_t *, frauth_t *);
static auth_tresult_t test_ifname(auth_test_t *, frauth_t *);
static auth_tresult_t test_proto(auth_test_t *, frauth_t *);
static auth_tresult_t test_dir(auth_test_t *, frauth_t *);
static auth_tresult_t test_any(auth_test_t *, frauth_t *);
static void action_generic_destroy(auth_action_t *);
static void action_nop_destroy(auth_action_t *);
static int pass_pkt(auth_action_t *, frauth_t *);
static int block_pkt(auth_action_t *, frauth_t *);
static int return_rst(auth_action_t *, frauth_t *);
static int add_state(auth_action_t *, frauth_t *);
static int use_rule_group(auth_action_t *, frauth_t *);
static int saddr_to_pool(auth_action_t *, frauth_t *);
static int daddr_to_pool(auth_action_t *, frauth_t *);
static auth_test_t * new_test(void);
static auth_action_t * new_action(void);
%}

%union {
	unsigned int		number;
	char			*string;
	i6addr_t		ip_addr;
	auth_pkt_action_t	action;
	auth_pkt_dir_t		direction;
	auth_test_t		*at;
	auth_action_t		*aa;
	int			proto;
	unsigned short int	port;
	ip_test_cx_t		*ipcx;
};

%token	TOKEN_FROM TOKEN_TO TOKEN_ON TOKEN_SLASH TOKEN_PORT TOKEN_ASSIGN
%token	TOKEN_RACTION TOKEN_USING TOKEN_SRC_TO_POOL TOKEN_DST_TO_POOL
%token	TOKEN_KEEP_STATE TOKEN_NUMBER TOKEN_IPV4 TOKEN_IPV6 TOKEN_STRING
%token	TOKEN_EOL TOKEN_COMMENT TOKEN_DIR TOKEN_UDP TOKEN_TCP TOKEN_PROTO
%token	TOKEN_ANY TOKEN_RETURN_RST

%type	<number>	TOKEN_NUMBER
%type	<string>	TOKEN_STRING
%type	<ip_addr>	TOKEN_IPV4 TOKEN_IPV6
%type	<action>	TOKEN_RACTION
%type	<direction>	TOKEN_DIR
%type	<at>		TOKEN_ANY rulematch pktdir fromsrc todst onifname proto
%type	<ipcx>		addr ipaddr
%type	<aa>		rulehdr
%type	<port>		port
%type	<ipcx>		addrmask

%%
lines		:
		| lines line
		;

line		: rule
		| comment

rule		: rulehdr rulematch ruleopts TOKEN_EOL {
	auth_rule_t	*nr;	/* new rule */
	auth_test_t	*scnd_test;	/* element following the first one */
	auth_action_t	*scnd_action;

	/*
	 * Semantic values for rulehdr and rulematch are stored in rule, while
	 * ruleopts are processed during parser actions for corresponding
	 * tokens. We are doing it in this way, becasuse rule options can
	 * define both kinds of data: actions or additional match parameters.
	 */

	nr = (auth_rule_t  *)calloc(1, sizeof (auth_rule_t));

	if (nr == NULL) {
		perror("Not enough memory to allocate rule");
		exit(1);
	}

	LIST_SET_HEAD(&nr->ar_actions, $1, aa_link);
	LIST_SET_HEAD(&nr->ar_tests, $2, at_link);

	if (!LIST_EMPTY(&Optional_Actions)) {
		auth_action_t	*aa = $1;
		LIST_CAT(aa, &Optional_Actions, aa_link, auth_action);
		LIST_INIT(&Optional_Actions);
	}

	if (!LIST_EMPTY(&Optional_Tests)) {
		auth_test_t	*at = $2;
		LIST_CAT(at, &Optional_Tests, at_link, auth_test);
		LIST_INIT(&Optional_Tests);
	}

	if (LIST_EMPTY(&Auth_Rules)) {
		LIST_INSERT_HEAD(&Auth_Rules, nr, ar_link);
	} else {
		LIST_INSERT_AFTER(Last_Inserted, nr, ar_link);
	}

	Last_Inserted = nr;
}
		;

rulehdr		: TOKEN_RACTION {
	auth_action_t	*aa = new_action();
	aa->aa_destroy = action_nop_destroy;

	switch ($1) {
		case AUTH_PASS	:
			aa->aa_perform = pass_pkt;
			break;
		case AUTH_BLOCK	:
			aa->aa_perform = block_pkt;
			break;
		default:
			fprintf(stderr, "Unexpected rule action");
			exit(1);
	}

	$$ = aa;
}
		| TOKEN_RACTION TOKEN_RETURN_RST {
	auth_action_t	*aa = new_action();
	aa->aa_destroy = action_nop_destroy;

	if ($1 == AUTH_BLOCK) {
		auth_action_t	*ret_rst = new_action();

		aa->aa_perform = block_pkt;
		ret_rst->aa_destroy = action_nop_destroy;
		ret_rst->aa_perform = return_rst;
		LIST_INSERT_HEAD(&Optional_Actions, ret_rst, aa_link);
	} else {
		fprintf(stderr, "return-rst can be bound to block rule only\n");
		exit(1);
	}

	$$ = aa;

}
		;

rulematch	: pktdir onifname proto fromsrc todst {
	auth_test_t	*at = new_test();

	at->at_func = test_proto;
	at->at_destroy = test_nop_destroy;
	at->at_cx = $3;

	/*
	 * cheapest tests are portocol match and direction tests, we will keep
	 * them first.
	 */
	LIST_INSERT_AFTER(at, $1, at_link);

	/*
	 * next one is interface name test
	 */
	LIST_INSERT_AFTER($1, $2, at_link);

	/*
	 * the third test will be source address + port test
	 */
	LIST_INSERT_AFTER($2, $4, at_link);

	/*
	 * finaly we add dest address + port test
	 */
	LIST_INSERT_AFTER($4, $5, at_link);

	$$ = at;
}
		| pktdir onifname fromsrc todst {
	/*
	 * the first will be direction test, followed by ifname test
	 */
	LIST_INSERT_AFTER($1, $2, at_link);

	/*
	 * next one will be source address
	 */
	LIST_INSERT_AFTER($2, $3, at_link);

	/*
	 * followed by destination address test
	 */
	LIST_INSERT_AFTER($3, $4, at_link);

	$$ = $1;
}
		| pktdir fromsrc todst {
	/*
	 * first direction test, followed by source addr test
	 */
	LIST_INSERT_AFTER($1, $2, at_link);
	/*
	 * the last one is destination address test
	 */
	LIST_INSERT_AFTER($2, $3, at_link);
	$$ = $1;
}
		;

pktdir		: TOKEN_DIR {
	auth_test_t	*at = new_test();

	at->at_func = test_dir;
	at->at_destroy = test_nop_destroy;
	at->at_cx = (void *)$1;

	$$ = at;
}
onifname	: TOKEN_ON TOKEN_STRING {
	auth_test_t	*at = new_test();
	at->at_func = test_ifname;
	at->at_destroy = test_generic_destroy;
	at->at_cx = $2;
	$$ = at;
}
		;

proto		: TOKEN_PROTO TOKEN_ASSIGN TOKEN_TCP {
	auth_test_t	*at = new_test();

	at->at_destroy = test_nop_destroy;
	at->at_func = test_proto;
	at->at_cx = (void *)IPPROTO_TCP;

	$$ = at;
}
		| TOKEN_PROTO TOKEN_ASSIGN TOKEN_UDP {
	auth_test_t	*at = new_test();

	at->at_destroy = test_nop_destroy;
	at->at_func = test_proto;
	at->at_cx = (void *)IPPROTO_UDP;

	$$ = at;
}
		| TOKEN_PROTO TOKEN_ASSIGN TOKEN_NUMBER {
	auth_test_t	*at = new_test();

	at->at_destroy = test_nop_destroy;
	at->at_func = test_proto;
	at->at_cx = (void *)((size_t)$3);

	$$ = at;
}
		;

fromsrc		: TOKEN_FROM addr port {
	auth_test_t	*addr_test = new_test();
	auth_test_t	*port_test = new_test();

	if ($2 == NULL) {
		addr_test->at_func = test_any;
		addr_test->at_destroy = test_nop_destroy;
	} else {
		addr_test->at_func = test_src_ip;
		addr_test->at_destroy = test_generic_destroy;
		addr_test->at_cx = $2;
	}

	port_test->at_func = test_src_port;
	port_test->at_destroy = test_nop_destroy;
	port_test->at_cx = (void *)((size_t)$3);
	LIST_INSERT_AFTER(port_test, addr_test, at_link);
	$$ = port_test;
}
		| TOKEN_FROM addr {
	auth_test_t	*at = new_test();

	if ($2 == NULL) {
		at->at_func = test_any;
		at->at_destroy = test_nop_destroy;
	} else {
		at->at_func = test_src_ip;
		at->at_destroy = test_generic_destroy;
		at->at_cx = $2;
	}
	$$ = at;
}
		;

todst		: TOKEN_TO addr port {
	auth_test_t	*addr_test = new_test();
	auth_test_t	*port_test = new_test();

	if ($2 == NULL) {
		addr_test->at_func = test_any;
		addr_test->at_destroy = test_nop_destroy;
	} else {
		addr_test->at_func = test_dst_ip;
		addr_test->at_destroy = test_generic_destroy;
		addr_test->at_cx = $2;
	}

	port_test->at_func = test_dst_port;
	port_test->at_destroy = test_nop_destroy;
	port_test->at_cx = (void *)((size_t)$3);
	LIST_INSERT_AFTER(port_test, addr_test, at_link);
	$$ = port_test;
}
		| TOKEN_TO addr {
	auth_test_t	*at = new_test();

	if ($2 == NULL) {
		at->at_func = test_any;
		at->at_destroy = test_nop_destroy;
	} else {
		at->at_func = test_dst_ip;
		at->at_destroy = test_generic_destroy;
		at->at_cx = $2;
	}
	$$ = at;
}
		;

addr		: ipaddr
		| addrmask
		| TOKEN_ANY {
	$$ = NULL;
}
		;

addrmask	: ipaddr TOKEN_SLASH ipaddr	{
	ip_test_cx_t	*ipcx = $1;

	ipcx->ipcx_mask = $3->ipcx_mask;
	free($3);
	$$ = ipcx;
}
		| ipaddr TOKEN_SLASH TOKEN_NUMBER {
	ip_test_cx_t	*ipcx = $1;
	uint32_t	number = $3;
	uint32_t	mask;

	if (ipcx->ipcx_v == 4) {
		number = number % 32;

		if (number == 0)
			mask = 0xffffffff;
		else
			mask = htonl((1 << number) - 1);

		ipcx->ipcx_mask.in4_addr = mask;
	} else {
		int	i = 0;
		char	*m = (char *)&number;
		int	j;

		/*
		 * create mask from number it's equivalent of
		 * (1 << number - 1)
		 */
		for (i = 0; i < number / 8; i++)
			ipcx->ipcx_mask.in6_addr8[i] = 0xff;

		number = number % 32;
		for (j = 0; j < i; j++)
			ipcx->ipcx_mask.in6_addr8[j] = m[j];
	}

	$$ = ipcx;
}
		;

ipaddr		: TOKEN_IPV4 {
	ip_test_cx_t	*ipcx = new_ipcx();

	ipcx->ipcx_v = 4;
	ipcx->ipcx_addr = $1;
	ipcx->ipcx_mask.in4_addr = 0xffffffff;
	$$ = ipcx;
}
		| TOKEN_IPV6 {
	ip_test_cx_t	*ipcx = new_ipcx();

	ipcx->ipcx_v = 6;
	ipcx->ipcx_addr = $1;
	memset(&ipcx->ipcx_mask, 0xff, sizeof (i6addr_t));
	$$ = ipcx;
}
		;

port		: TOKEN_PORT TOKEN_ASSIGN TOKEN_NUMBER {
	$$ = htons((uint16_t)$3);
}

ruleopts	:
		| ruleopts ruleopt
		;

ruleopt		: TOKEN_USING TOKEN_STRING {
	/*
	 * we will insert actions into rule directly, since rule option might
	 * represent additional match parameter for packet.
	 */
	auth_action_t	*aa = new_action();

	aa->aa_destroy = action_generic_destroy;
	aa->aa_perform = use_rule_group;
	aa->aa_cx = $2;

	LIST_INSERT_HEAD(&Optional_Actions, aa, aa_link);
}
		| TOKEN_SRC_TO_POOL TOKEN_NUMBER {
	/*
	 * we will insert actions into rule directly, since rule option might
	 * represent additional match parameter for packet.
	 */
	auth_action_t	*aa = new_action();
	char		buf[80];

	snprintf(buf, sizeof (buf) - 1, "%u", $2);
	aa->aa_destroy = action_generic_destroy;
	aa->aa_perform = saddr_to_pool;
	aa->aa_cx = strdup(buf);

	printf("%s\n", (const char *)aa->aa_cx);
	LIST_INSERT_HEAD(&Optional_Actions, aa, aa_link);
}
		| TOKEN_DST_TO_POOL TOKEN_NUMBER {
	/*
	 * we will insert actions into rule directly, since rule option might
	 * represent additional match parameter for packet.
	 */
	auth_action_t	*aa = new_action();
	char		buf[80];

	snprintf(buf, sizeof (buf) - 1, "%u", $2);
	aa->aa_destroy = action_generic_destroy;
	aa->aa_perform = daddr_to_pool;
	aa->aa_cx = strdup(buf);

	printf("%s\n", (const char *)aa->aa_cx);
	LIST_INSERT_HEAD(&Optional_Actions, aa, aa_link);
}
		| TOKEN_KEEP_STATE {
	/*
	 * we will insert actions into rule directly, since rule option might
	 * represent additional match parameter for packet.
	 */
	auth_action_t	*aa = new_action();

	aa->aa_destroy = action_nop_destroy;
	aa->aa_perform = add_state;

	LIST_INSERT_HEAD(&Optional_Actions, aa, aa_link);
}
		;

comment		:	TOKEN_COMMENT TOKEN_EOL
		;
%%

static int
printf_wrap(const char *format, ...)
{
	va_list	ap;
	int	rv;
	va_start(ap, format);

	if (yydebug == 1)
		rv = vprintf(format, ap);
	else
		rv = 0;

	va_end(ap);

	return (rv);
}

static const char *
get_result_str(auth_tresult_t r)
{
	static const char *results[] = {
		"Failed",
		"No match",
		"Match",
	};
	unsigned int	index;

	/*
	 * We need to emulate -1 index for results array
	 */
	index = (unsigned int)((int)r + ((int)AUTH_T_FAIL * -1));
	return (results[index % AUTH_LAST_INDEX]);
}

/*
 * releases auth test context
 */
static void
test_generic_destroy(auth_test_t *at)
{
	if (at->at_cx)
		free(at->at_cx);

	at->at_cx = NULL;
}

static ip_test_cx_t *
new_ipcx(void)
{
	ip_test_cx_t *ret_val;

	ret_val = (ip_test_cx_t *)calloc(1, sizeof (ip_test_cx_t));

	if (ret_val == NULL) {
		perror("Not enough memory");
		exit(1);
	}

	return (ret_val);
}

/*
 * in case there is no context allocated
 */
static void
test_nop_destroy(auth_test_t *at)
{
	at->at_cx = NULL;
}

/*
 * masks IPv6 address with mask.
 * 	addr	pointer IPv6 address
 *	mask	pointer IPv6 mask
 *	result	pointer where to store result
 * returns void
 */
static void
mask_addr6(const i6addr_t *addr, const i6addr_t *mask, i6addr_t *result)
{
	unsigned int	i;

	bzero(result, sizeof (result));
	for (i = 0; i < sizeof (result); i++)
		result->in6_addr8[i] =
		    addr->in6_addr8[i] & mask->in6_addr8[i];
}

/*
 * compares two IPv6 addresses addr_1, and addr_2.
 * returns AUTH_T_MATCH on match,
 */
static auth_tresult_t
cmp_addr6(i6addr_t *addr_1, i6addr_t *addr_2)
{
	unsigned int	i;

	for (i = 0; i < sizeof (i6addr_t); i++)
		if (addr_1->in6_addr8[i] != addr_2->in6_addr8[i])
			return (AUTH_T_NOMATCH);

	return (AUTH_T_MATCH);
}


static auth_tresult_t
test_ip(ip_test_cx_t *ip_cx, i6addr_t *i6addr)
{
	auth_tresult_t	rv;

	switch (ip_cx->ipcx_v) {
		case 4: {
			unsigned int	m_addr;	/* addr & addr_mask */
			m_addr =
			    i6addr->in4.s_addr & ip_cx->ipcx_mask.in4.s_addr;
			rv = (auth_tresult_t)
			    (ip_cx->ipcx_addr.in4.s_addr == m_addr);
		} break;
		case 6: {
			i6addr_t	m_addr;
			mask_addr6(i6addr, &ip_cx->ipcx_mask, &m_addr);
			rv = cmp_addr6(&m_addr, &ip_cx->ipcx_addr);
		} break;
		default:
			rv = AUTH_T_FAIL;
	}

	return (rv);
}

static auth_tresult_t
test_src_ip(auth_test_t *at, frauth_t *fra)
{
	ip_test_cx_t	*ip_cx = (ip_test_cx_t *)at->at_cx;
	auth_tresult_t	ret_val;

	if (fra->fra_info.fin_v != ip_cx->ipcx_v) {
		DBG_PRINT(("Protocol version does not match"));
		return (AUTH_T_NOMATCH);
	}

	ret_val = test_ip(ip_cx, &fra->fra_info.fin_src6);

	DBG_PRINT(("Source address test %s for\n",
	    get_result_str(ret_val)));
	DBG_PRINTIP(ip_cx, fra->fra_info.fin_src6);

	return (ret_val);
}


static auth_tresult_t
test_dst_ip(auth_test_t *at, frauth_t *fra)
{
	ip_test_cx_t	*ip_cx = (ip_test_cx_t *)at->at_cx;
	auth_tresult_t	ret_val;

	if (fra->fra_info.fin_v != ip_cx->ipcx_v)
		return (AUTH_T_NOMATCH);

	ret_val = test_ip(ip_cx, &fra->fra_info.fin_dst6);

	DBG_PRINT(("Destination address test %s for\n",
	    get_result_str(ret_val)));
	DBG_PRINTIP(ip_cx, fra->fra_info.fin_src6);

	return (ret_val);
}

static auth_tresult_t
test_src_port(auth_test_t *at, frauth_t *fra)
{
	unsigned short int	port = (unsigned short int)((size_t)at->at_cx);
	fr_info_t		*fin = &fra->fra_info;
	auth_tresult_t		ret_val;

	ret_val = (auth_tresult_t)(IS_TCPUDP(fin) && (port == fin->fin_sport));

	DBG_PRINT(("Source port address %s for %d %d\n",
	    get_result_str(ret_val), htons(port), htons(fin->fin_sport)));

	return (ret_val);
}

static auth_tresult_t
test_dst_port(auth_test_t *at, frauth_t *fra)
{
	unsigned short int	port = (unsigned short int)((size_t)at->at_cx);
	fr_info_t		*fin = &fra->fra_info;
	auth_tresult_t		ret_val;

	ret_val = (auth_tresult_t)(IS_TCPUDP(fin) && (port == fin->fin_dport));
	DBG_PRINT(("Source port address %s for %d %d\n",
	    get_result_str(ret_val), htons(port), htons(fin->fin_sport)));

	return (ret_val);
}

static auth_tresult_t
test_ifname(auth_test_t *at, frauth_t *fra)
{
	auth_tresult_t	ret_val;

	ret_val = (auth_tresult_t)(strncasecmp(
	    (const char *)at->at_cx, fra->fra_ifname, LIFNAMSIZ) == 0);

	DBG_PRINT(("Interface name test %s (%s vs. %s)\n",
	    get_result_str(ret_val),
	    (const char *) at->at_cx, fra->fra_ifname));

	return (ret_val);
}

static auth_tresult_t
test_proto(auth_test_t *at, frauth_t *fra)
{
	char		proto = (char)((size_t)at->at_cx);
	auth_tresult_t	ret_val;

	ret_val = (auth_tresult_t)(fra->fra_info.fin_p == proto);

	DBG_PRINT(("Interface name test %s (%d vs. %d)",
	    get_result_str(ret_val),
	    proto, fra->fra_info.fin_p));

	return (ret_val);
}

static auth_tresult_t
test_dir(auth_test_t *at, frauth_t *fra)
{
	int		dir = (int)((size_t)at->at_cx);
	auth_tresult_t	ret_val;

	ret_val = (auth_tresult_t)(fra->fra_info.fin_out == dir);

	DBG_PRINT(("Pkt direction test %s (%d vs. %d)\n",
	    get_result_str(ret_val), dir, fra->fra_info.fin_out));

	return (ret_val);
}

static auth_tresult_t
test_any(auth_test_t *at, frauth_t *fra)
{
	return (AUTH_T_MATCH);
}

static void
action_generic_destroy(auth_action_t *aa)
{
	if (aa->aa_cx)
		free(aa->aa_cx);

	aa->aa_cx = NULL;
}

static void
action_nop_destroy(auth_action_t *aa)
{
	aa->aa_cx = NULL;
}

static int
pass_pkt(auth_action_t *aa, frauth_t *fra)
{
	MARK_ACTION(fra, FR_PASS);
	DBG_PRINT(("Passing packet"));

	return (0);
}

static int
block_pkt(auth_action_t *aa, frauth_t *fra)
{
	MARK_ACTION(fra, FR_BLOCK);
	DBG_PRINT(("Blocking packet"));

	return (0);
}

static int
return_rst(auth_action_t *aa, frauth_t *fra)
{
	MARK_ACTION(fra, FR_RETRST);
	DBG_PRINT(("Return RST"));

	return (0);
}

static int
add_state(auth_action_t *aa, frauth_t *fra)
{
	MARK_ACTION(fra, FR_KEEPSTATE);
	DBG_PRINT(("Adding state"));

	return (0);
}

static int
use_rule_group(auth_action_t *aa, frauth_t *fra)
{
	fra->fra_grp_unit = IPL_LOGIPF;
	strlcpy(fra->fra_rgroup, (const char *)aa->aa_cx, FR_GROUPLEN);
	DBG_PRINT(("Will use rule group %s to decide about packet",
	    (const char *)aa->aa_cx));

	return (0);
}

static int
addr_to_pool(auth_action_t *aa, frauth_t *fra, i6addr_t *addr)
{
	int	rv;
	ip_pool_node_t	pn;
	iplookupop_t	op;
	fr_info_t	*fin = &fra->fra_info;

	if (Ippool_Fd != -1) {
		bzero(&pn, sizeof (ip_pool_node_t));
		bzero(&op, sizeof (iplookupop_t));

		pn.ipn_addr.adf_family =
		    (fin->fin_v == 6) ? AF_INET6 : AF_INET;
		pn.ipn_addr.adf_len = sizeof (i6addr_t);
		pn.ipn_addr.adf_addr = *addr;

		pn.ipn_mask.adf_family = pn.ipn_addr.adf_family;
		pn.ipn_mask.adf_len = sizeof (i6addr_t);
		memset(&pn.ipn_mask.adf_addr, 0xff, sizeof (i6addr_t));

		op.iplo_type = IPLT_POOL;
		op.iplo_arg = 0;
		op.iplo_struct = &pn;
		op.iplo_size = sizeof (pn);
		strncpy(op.iplo_name, (const char *)aa->aa_cx,
		    sizeof (op.iplo_name));
		rv = ioctl(Ippool_Fd, SIOCLOOKUPADDNODE, &op);
	} else {
		rv = -1;
	}

	DBG_PRINT(("%s\n",
	    (rv == -1) ? "Failed to add address" : "Address added"));

	return (rv);
}

static int
saddr_to_pool(auth_action_t *aa, frauth_t *fra)
{
	DBG_PRINT(("Adding pkt source address to pool\n"));
	return (addr_to_pool(aa, fra, &fra->fra_info.fin_src6));
}

static int
daddr_to_pool(auth_action_t *aa, frauth_t *fra)
{
	DBG_PRINT(("Adding pkt destination address to pool\n"));

	return (addr_to_pool(aa, fra, &fra->fra_info.fin_dst6));
}

static auth_test_t *
new_test(void)
{
	auth_test_t	*ret_val;

	ret_val = (auth_test_t *)calloc(1, sizeof (auth_test_t));

	if (ret_val == NULL) {
		perror("not enough memory");
		exit(1);
	}

	return (ret_val);
}

static auth_action_t *
new_action(void)
{
	auth_action_t	*ret_val;

	ret_val = (auth_action_t *)calloc(1, sizeof (auth_action_t));

	if (ret_val == NULL) {
		perror("not enough memory");
		exit(1);
	}

	return (ret_val);
}

static void
auth_release_rule(auth_rule_t *ar)
{
	auth_action_t	*aa;
	auth_test_t	*at;

	while (!LIST_EMPTY(&ar->ar_actions)) {
		aa = LIST_FIRST(&ar->ar_actions);
		LIST_REMOVE(aa, aa_link);
		aa->aa_destroy(aa);
		free(aa);
	}

	while (!LIST_EMPTY(&ar->ar_tests)) {
		at = LIST_FIRST(&ar->ar_tests);
		LIST_REMOVE(at, at_link);
		at->at_destroy(at);
		free(at);
	}

	free(ar);
}


void
auth_unload_rules(void)
{
	auth_rule_t	*ar;

	while (!LIST_EMPTY(&Auth_Rules)) {
		ar = LIST_FIRST(&Auth_Rules);
		LIST_REMOVE(ar, ar_link);
		auth_release_rule(ar);
	}

	if (Ippool_Fd != -1) {
		close(Ippool_Fd);
		Ippool_Fd = -1;
	}
}


static auth_tresult_t
auth_rule_test(const auth_rule_t *ar, frauth_t *auth_request)
{
	auth_tresult_t	ret_val = AUTH_T_NOMATCH;
	auth_test_t	*at = NULL;

	LIST_FOREACH(at, &ar->ar_tests, at_link) {
		ret_val = at->at_func(at, auth_request);
		if (ret_val == AUTH_T_NOMATCH || ret_val == AUTH_T_FAIL)
			break;
	}

	return (ret_val);
}

static int
auth_rule_perform(const auth_rule_t *ar, frauth_t *auth_request)
{
	auth_action_t	*aa;
	int		ret_val = 0;

	LIST_FOREACH(aa, &ar->ar_actions, aa_link) {
		ret_val |= aa->aa_perform(aa, auth_request);
		printf("@\n");
	}

	return (ret_val);
}

int
auth_check(frauth_t *auth_request)
{
	auth_rule_t	*ar = NULL;
	auth_rule_t	*match = NULL;
	int		ret_val = 0;

	/*
	 * Find matching rule.
	 */
	LIST_FOREACH(ar, &Auth_Rules, ar_link) {
		if (auth_rule_test(ar, auth_request) == AUTH_T_MATCH)
			match = ar;
	}

	/*
	 * Found match, we need to perform all actions associated with rule
	 */
	if (match != NULL) {
		DBG_PRINT(("Found matching rule\n"));
		ret_val = auth_rule_perform(match, auth_request);
	}

	DBG_PRINT(("----------------------------------------\n"));

	return (ret_val);
}

int
auth_load_rules(const char *fpath)
{
	FILE	*f;
	int	rv;

	if (!LIST_EMPTY(&Auth_Rules)) {
		fprintf(stderr, "Rules already loaded\n");
		return (-1);
	}

	f = fopen(fpath, "r");
	if (f == NULL) {
		fprintf(stderr, "Unable to open file (%s)", strerror(errno));
		return (-1);
	}

	Ippool_Fd = open(IPLOOKUP_NAME, O_RDWR);
	if (Ippool_Fd == -1) {
		fprintf(stderr, "Unable to open %s (%s)\n",
		    IPLOOKUP_NAME, strerror(errno));
		fclose(f);
		return (-1);
	}

	yyin = f;
	rv = yyparse();
	fclose(f);

	return (rv);
}

int
yyerror(char * e)
{
	fprintf(stderr, "%s\n", e);
	exit(1);

	/* NOTREACHED */
	return (-1);
}
