/*
 * Copyright (C) 1997-2001 by Darren Reed & Guido Van Rooij.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: ip_auth.h,v 2.16 2003/07/25 12:29:56 darrenr Exp $
 *
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */


#ifndef	_IP_AUTH_H_
#define	_IP_AUTH_H_

#include <sys/queue.h>

#include "netinet/ip_compat.h"
#include "netinet/ip_fil.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The length of queue for packet authorisation requests
 */
#define	FR_NUMAUTH	32000

/*
 * frauth_t structure holding data, which are
 * exported to userspace
 */
typedef struct  frauth {
	int	fra_len;
	u_32_t	fra_key;
	u_32_t	fra_pass;
	char	fra_rgroup[FR_GROUPLEN];
	minor_t	fra_grp_unit;	/* IPL_LOGIPF : IPL_LOGCOUNT(IPL_LOGMAX) */
	char	fra_ifname[LIFNAMSIZ];
	fr_info_t	fra_info;
	char	*fra_buf;
#ifdef	MENTAT
	queue_t	*fra_q;
#endif
} frauth_t;

/*
 * this is an internal wrapper it keeps data for userspace and meta data
 * (related to list/queue management) separate
 */
typedef struct frauth_private {
	LIST_ENTRY(frauth_private)	fri_glist;
	LIST_ENTRY(frauth_private)	fri_requests;
	LIST_ENTRY(frauth_private)	fri_answers;
	int		fri_removed;
	frauth_t	fri_auth;
	ipftqent_t	fri_tq;
	mb_t		*fri_pkt;
	u_32_t		fri_ref;
	ipfmutex_t	fri_lock;	/* protects auth entry consistency */
	u_32_t		fri_resolved;	/* unresolved entries will have 0 */
} frauth_priv_t;

typedef LIST_HEAD(frauth_htab, frauth_private) frauth_htab_t;

LIST_HEAD(auth_list_head_s, frauth_private);
typedef struct auth_list_head_s	auth_list_head_t;

#define	fri_key		fri_auth.fra_key
#define	fri_info	fri_auth.fra_info
#define	fri_pass	fri_auth.fra_pass
#define	fri_rgroup	fri_auth.fra_rgroup
#define	fri_ifname	fri_auth.fra_ifname
#define	fri_ifp		fri_auth.fra_info.fin_ifp
#define	fri_unit	fri_auth.fra_grp_unit

typedef	struct	frauthent  {
	struct	frentry		fae_fr;
	struct	frauthent	*fae_next;
	ulong_t			fae_age;
	int			fae_ref;
} frauthent_t;

typedef struct  fr_authstat {
	U_QUAD_T	fas_hits;
	ulong_t		fas_nospace;
	ulong_t		fas_added;
	ulong_t		fas_sendfail;
	ulong_t		fas_sendok;
	ulong_t		fas_queok;
	ulong_t		fas_quefail;
	ulong_t		fas_expire;
	ulong_t		fas_entries;
	frauthent_t	*fas_faelist;
} fr_authstat_t;


extern	frentry_t *fr_checkauth(fr_info_t *, u_32_t *);
extern	void fr_authexpire(ipf_stack_t *);
extern	int fr_authinit(ipf_stack_t *);
extern	void fr_authunload(ipf_stack_t *);
extern	int fr_newauth(mb_t *, fr_info_t *);
extern	int fr_auth_ioctl(caddr_t, int, int, int, void *, ipf_stack_t *);
extern	int fr_authflush_rule(frentry_t *, ipf_stack_t *);
extern	void fr_auth_free_token(ipftoken_t *);

#ifdef __cplusplus
}
#endif

#endif	/* _IP_AUTH_H_ */
