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

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/pfkeyv2.h>
#include <errno.h>
#include <alloca.h>
#include <locale.h>
#include <strings.h>
#include <auth_attr.h>
#include <secdb.h>

#include <ike/sshincludes.h>
#include <ike/isakmp.h>
#include <ike/isakmp_policy.h>
#include <ike/isakmp_doi.h>
#include <ike/cmi.h>
#include <ipsec_util.h>

#include "defs.h"
#include "readps.h"
#include "lock.h"
#include "getssh.h"

/*
 * The SshIkeGroupDescRec structure definition comes from isakmp_groups.c
 * which is part of SafeNet QuickSec 2.1 OEM library (libike.so).
 */
typedef struct SshIkeGroupDescRec {
	SshIkeAttributeGrpDescValues descriptor;
	SshIkeAttributeGrpTypeValues type;
	const char *name;
	SshUInt32 strength;	/* (modp, ecp, ec2n) optional */
} *SshIkeGroupDesc, SshIkeGroupDescStruct;

/* global variables defined in libike.so */
extern struct SshIkeGroupDescRec const ssh_ike_default_group[];
extern int ssh_ike_default_group_cnt;

extern phase1_t	*phase1_head;
extern char	*cfile;		/* default config file location */
extern int	privilege;
extern FILE	*debugfile;

/* Need globals for libike iterator */
static char pkcs11_label_match[PKCS11_TOKSIZE];
static ike_pin_t *global_pin;

#define	CERTCACHE_TIMEOUT (hrtime_t)30
static hrtime_t certcache_timer;

#define	DOOR_FD_DESC_OK(dp)	(((dp) != NULL) && \
	((dp)->d_attributes & DOOR_DESCRIPTOR) && \
	((dp)->d_data.d_desc.d_descriptor >= 0))

static char *
svccmdstr(uint32_t cmd)
{
	switch (cmd) {
	case IKE_SVC_GET_DBG:
		return ("IKE_SVC_GET_DBG");
	case IKE_SVC_SET_DBG:
		return ("IKE_SVC_SET_DBG");

	case IKE_SVC_GET_PRIV:
		return ("IKE_SVC_GET_PRIV");
	case IKE_SVC_SET_PRIV:
		return ("IKE_SVC_SET_PRIV");

	case IKE_SVC_GET_STATS:
		return ("IKE_SVC_GET_STATS");
	case IKE_SVC_GET_DEFS:
		return ("IKE_SVC_GET_DEFS");

	case IKE_SVC_DUMP_P1S:
		return ("IKE_SVC_DUMP_P1S");
	case IKE_SVC_FLUSH_P1S:
		return ("IKE_SVC_FLUSH_P1S");
	case IKE_SVC_GET_P1:
		return ("IKE_SVC_GET_P1");
	case IKE_SVC_DEL_P1:
		return ("IKE_SVC_DEL_P1");

	case IKE_SVC_GET_RULE:
		return ("IKE_SVC_GET_RULE");
	case IKE_SVC_NEW_RULE:
		return ("IKE_SVC_NEW_RULE");
	case IKE_SVC_DEL_RULE:
		return ("IKE_SVC_DEL_RULE");
	case IKE_SVC_DUMP_RULES:
		return ("IKE_SVC_DUMP_RULES");
	case IKE_SVC_READ_RULES:
		return ("IKE_SVC_READ_RULES");
	case IKE_SVC_WRITE_RULES:
		return ("IKE_SVC_WRITE_RULES");

	case IKE_SVC_GET_PS:
		return ("IKE_SVC_GET_PS");
	case IKE_SVC_NEW_PS:
		return ("IKE_SVC_NEW_PS");
	case IKE_SVC_DEL_PS:
		return ("IKE_SVC_DEL_PS");
	case IKE_SVC_DUMP_PS:
		return ("IKE_SVC_DUMP_PS");
	case IKE_SVC_READ_PS:
		return ("IKE_SVC_READ_PS");
	case IKE_SVC_WRITE_PS:
		return ("IKE_SVC_WRITE_PS");

	case IKE_SVC_DBG_RBDUMP:
		return ("IKE_SVC_DBG_RBDUMP");

	case IKE_SVC_SET_PIN:
		return ("IKE_SVC_SET_PIN");
	case IKE_SVC_DEL_PIN:
		return ("IKE_SVC_DEL_PIN");

	case IKE_SVC_DUMP_CERTCACHE:
		return ("IKE_SVC_DUMP_CERTCACHE");
	case IKE_SVC_FLUSH_CERTCACHE:
		return ("IKE_SVC_FLUSH_CERTCACHE");

	case IKE_SVC_DUMP_GROUPS:
		return ("IKE_SVC_DUMP_GROUPS");
	case IKE_SVC_DUMP_ENCRALGS:
		return ("IKE_SVC_DUMP_ENCRALGS");
	case IKE_SVC_DUMP_AUTHALGS:
		return ("IKE_SVC_DUMP_AUTHALGS");

	case IKE_SVC_ERROR:
		return ("IKE_SVC_ERROR");

	default:
		return ("<unknown>");
	}
}

static phase1_t *
match_phase1_by_ckys(uint64_t cky_i, uint64_t cky_r)
{
	phase1_t	*walker;
	SshIkeCookies	ck;

	for (walker = phase1_head; walker != NULL; walker = walker->p1_next) {
		ck = walker->p1_pminfo->cookies;
		if ((memcmp(ck->initiator_cookie, (char *)&cky_i,
		    sizeof (uint64_t)) == 0) &&
		    (memcmp(ck->responder_cookie, (char *)&cky_r,
		    sizeof (uint64_t)) == 0))
			break;
	}

	return (walker);
}

static void
cpout_p1hdr(ike_p1_hdr_t *hdrp, SshIkePMPhaseI pm)
{
	phase1_t	*p1 = pm->policy_manager_data;
	int	cky_len = sizeof (pm->cookies->initiator_cookie);

	/*
	 * The cookie length is defined in the RFC (2408 3.1),
	 * better be what we expect...
	 */
	assert(cky_len == sizeof (uint64_t));

	(void) memcpy((char *)&hdrp->p1hdr_cookies.cky_i,
	    pm->cookies->initiator_cookie, cky_len);
	/* assume the responder cookie is the same length */
	(void) memcpy((char *)&hdrp->p1hdr_cookies.cky_r,
	    pm->cookies->responder_cookie, cky_len);

	hdrp->p1hdr_major = pm->major_version;
	hdrp->p1hdr_minor = pm->minor_version;
	hdrp->p1hdr_xchg = pm->exchange_type;
	hdrp->p1hdr_isinit = pm->this_end_is_initiator;
	hdrp->p1hdr_state = get_ssh_p1state(pm->negotiation);
	hdrp->p1hdr_support_dpd = p1->p1_use_dpd;
	hdrp->p1hdr_dpd_state = p1->p1_dpd_status;
	hdrp->p1hdr_dpd_time = p1->p1_dpd_time;
}

static int
cpout_group(char **rtnpp, int *bytes, const struct SshIkeGroupDescRec *walker,
    int ssize, int *uerr)
{
	ike_group_t *gp;

	*bytes = ssize + sizeof (ike_group_t);
	*rtnpp = ssh_malloc(*bytes);
	if (*rtnpp == NULL) {
		*uerr = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}

	gp = (ike_group_t *)((uintptr_t)*rtnpp + ssize);
	gp->group_number = walker->descriptor;
	gp->group_bits = walker->strength;
	(void) strlcpy(gp->group_label, walker->name,
	    sizeof (gp->group_label));

	return (0);
}

static int
cpout_encralg(char **rtnpp, int *bytes, const keywdtab_t *walker,
    int ssize, int *uerr)
{
	ike_encralg_t *ep;
	int rv, minlow, maxhigh;

	*bytes = ssize + sizeof (ike_encralg_t);
	*rtnpp = ssh_malloc(*bytes);
	if (*rtnpp == NULL) {
		*uerr = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}

	ep = (ike_encralg_t *)((uintptr_t)*rtnpp + ssize);
	ep->encr_value = sadb_to_sshencr(walker->kw_tag);
	(void) strlcpy(ep->encr_name, walker->kw_str,
	    sizeof (ep->encr_name));
	/* encr_alg_lookup will initialize minlow, maxhigh to 0 */
	rv = encr_alg_lookup(walker->kw_str, &minlow, &maxhigh);
	if (rv != 0) {
		ep->encr_keylen_min = minlow;
		ep->encr_keylen_max = maxhigh;
	}

	return (0);
}

static int
cpout_authalg(char **rtnpp, int *bytes, const keywdtab_t *walker,
    int ssize, int *uerr)
{
	ike_authalg_t *ep;

	*bytes = ssize + sizeof (ike_authalg_t);
	*rtnpp = ssh_malloc(*bytes);
	if (*rtnpp == NULL) {
		*uerr = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}

	ep = (ike_authalg_t *)((uintptr_t)*rtnpp + ssize);
	ep->auth_value = sadb_to_sshauth(walker->kw_tag);
	(void) strlcpy(ep->auth_name, walker->kw_str,
	    sizeof (ep->auth_name));

	return (0);
}

static void
cpout_p1xform(ike_p1_xform_t *xfp, SshIkePMPhaseI pm)
{
	phase1_t	*p1 = pm->policy_manager_data;

	xfp->p1xf_dh_group = (p1 != NULL) ? p1->p1_group : 0;
	xfp->p1xf_encr_alg = get_ssh_encralg(pm->negotiation);
	/* High bits is a placeholder for negotiated algorithm strength */
	xfp->p1xf_encr_high_bits = get_ssh_cipherkeylen(pm->negotiation);
	xfp->p1xf_encr_low_bits = 0;
	xfp->p1xf_auth_alg = get_ssh_authalg(pm->negotiation);
	xfp->p1xf_auth_meth = pm->auth_method;
	xfp->p1xf_prf = get_ssh_prf(pm->negotiation);
	xfp->p1xf_pfs = (p1 != NULL) ? p1->p1_p2_group : 0;
	xfp->p1xf_max_secs = pm->sa_expire_time - pm->sa_start_time;
	xfp->p1xf_max_kbytes = get_ssh_max_kbytes(pm->negotiation);
	xfp->p1xf_max_keyuses = 0;
}

static void
cpout_p1stats(ike_p1_stats_t *sp, SshIkePMPhaseI pm)
{
	phase1_t	*p1 = (phase1_t *)pm->policy_manager_data;

	sp->p1stat_start = pm->sa_start_time;
	sp->p1stat_kbytes = get_ssh_kbytes(pm->negotiation);
	sp->p1stat_keyuses = p1->p1_stats.p1stat_keyuses;
	sp->p1stat_new_qm_sas = p1->p1_stats.p1stat_new_qm_sas;
	sp->p1stat_del_qm_sas = p1->p1_stats.p1stat_del_qm_sas;
}

static void
cpout_p1errs(ike_p1_errors_t *errp, SshIkePMPhaseI pm)
{
	phase1_t	*p1 = (phase1_t *)pm->policy_manager_data;

	/*
	 * This could just be an assignment, but to make sure we
	 * don't hit compiler bug 4438087, do a copy here...
	 */

	(void) memcpy(errp, &p1->p1_errs, sizeof (ike_p1_errors_t));
}

/*
 * Return the total key size; also return a pointer to the preshared
 * entry that's looked up (if given a non-null pointer), since someone
 * interested in the size might also be interested in the object itself.
 * We might save the caller an extra lookup this way.
 */
static int
get_p1key_size(SshIkePMPhaseI pm, preshared_entry_t **rtnpp)
{
	int			size = 0, add, ssize;
	preshared_entry_t	*pse;

	/*
	 * Check the length of each value we want to put into
	 * the key struct, and add to the tally if non-zero.
	 * Also round each one up if it's an odd number of
	 * bytes to maintain 16-bit alignment.
	 */

	ssize = sizeof (ike_p1_key_t);

#define	ADD(count)						\
	add = count;						\
	if (add != 0) {						\
		add = roundup(add, sizeof (ike_p1_key_t));	\
		size += add + ssize;				\
	}

	pse = lookup_pre_shared_key(pm);
	if (pse != NULL) {
		ADD(pse->pe_keybuf_bytes);
	}

	if (rtnpp != NULL)
		*rtnpp = pse;

	ADD(get_ssh_skeyid_len(pm->negotiation));
	ADD(get_ssh_skeyid_d_len(pm->negotiation));
	ADD(get_ssh_skeyid_a_len(pm->negotiation));
	ADD(get_ssh_skeyid_e_len(pm->negotiation));
	ADD(get_ssh_encrkey_len(pm->negotiation));
	ADD(get_ssh_iv_len(pm->negotiation));

#undef ADD

	return (size);
}

static void
cpout_p1keys(ike_p1_key_t *kp, SshIkePMPhaseI pm, preshared_entry_t *pse)
{
	ike_p1_key_t		*curp = kp;
	int			len, ssize, rlen;

	/*
	 * On each of these, bump the write pointer forward to
	 * maintain 64-bit alignment in the overall structure.  The
	 * len field will still reflect the *actual* length of the
	 * structure + data.
	 */

#define	PUTKEY(tag)	 					\
	curp->p1key_type = (tag);				\
	curp->p1key_len = len + ssize;				\
	rlen = roundup(len, sizeof (ike_p1_key_t));		\
	curp += ((ssize + rlen) >> 3);				\

#define	GETKEY(tag, name)					\
	len = get_ssh_ ## name ## _len(pm->negotiation);	\
	if (len != 0) {						\
		get_ssh_ ## name(pm->negotiation, len, (uint8_t *)(&curp[1]));\
		PUTKEY(tag);					\
	}

	ssize = sizeof (ike_p1_key_t);
	if (pse != NULL) {
		len = pse->pe_keybuf_bytes;
		(void) memcpy((uint8_t *)(curp + 1), pse->pe_keybuf, len);
		PUTKEY(IKE_KEY_PRESHARED);
	}

	GETKEY(IKE_KEY_SKEYID, skeyid);
	GETKEY(IKE_KEY_SKEYID_D, skeyid_d);
	GETKEY(IKE_KEY_SKEYID_A, skeyid_a);
	GETKEY(IKE_KEY_SKEYID_E, skeyid_e);
	GETKEY(IKE_KEY_ENCR, encrkey);
	GETKEY(IKE_KEY_IV, iv);
#undef GETKEY
#undef PUTKEY
}

/* ARGSUSED */
static int
cpin_pskey(preshared_entry_t *pep, ike_ps_t *psp, int *uerrp)
{
	uint8_t *p;

	pep->pe_keybuf = ssh_malloc(psp->ps_key_len);
	if (pep->pe_keybuf == NULL) {
		*uerrp = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}

	p = (uint8_t *)((uintptr_t)psp + psp->ps_key_off);
	(void) memcpy(pep->pe_keybuf, p, psp->ps_key_len);
	pep->pe_keybuf_bytes = psp->ps_key_len;
	pep->pe_keybuf_lbits = psp->ps_key_bits;

	return (0);
}

static void
cpout_pskey(ike_ps_t *psp, preshared_entry_t *pep)
{
	uint8_t	*p;

	p = (uint8_t *)((uintptr_t)psp + psp->ps_key_off);
	(void) memcpy(p, pep->pe_keybuf, pep->pe_keybuf_bytes);
	psp->ps_key_bits = pep->pe_keybuf_lbits;
}

static void
cp_ip(struct sockaddr_storage *dst, struct sockaddr_storage *src)
{
	(void) memcpy(dst, src, sizeof (struct sockaddr_storage));
}

static int
get_sadbid_size(sadb_ident_t *idp)
{
	if (idp == NULL)
		return (0);

	return (SADB_64TO8(idp->sadb_ident_len));
}

/*
 * Caller must ensure that idp points to a buffer
 * large enough to contain srcp->sadb_ident_len.
 */
static void
cp_sadbid(sadb_ident_t *dstp, sadb_ident_t *srcp)
{
	(void) memcpy(dstp, srcp, SADB_64TO8(srcp->sadb_ident_len));
}

/*
 * len should be (sadb_ident_t + identity data that follows), and sidp
 * should point to a block of memory of at least totallen bytes.  type
 * should be one of the PS_ID_* types defined in readps.h.
 */
static void
cpout_psid(sadb_ident_t *sidp, int type, char *id, int len)
{
	char	*p;
	int	idlen = len - sizeof (sadb_ident_t);

	if (idlen <= 0)
		return;

	sidp->sadb_ident_type = psid2sadb(type);
	if (sidp->sadb_ident_type == SADB_IDENTTYPE_RESERVED) {
		PRTDBG(D_DOOR, ("cpout_psid: invalid id type %d.", type));
		return;
	}

	sidp->sadb_ident_len = SADB_8TO64(len);
	sidp->sadb_ident_reserved = 0;
	sidp->sadb_ident_id = 0;

	p = (char *)(sidp + 1);
	(void) strlcpy(p, id, idlen);
}

/* ARGSUSED */
static int
cpin_psid(int *type, char **id, sadb_ident_t *sidp, int totallen, int *uerrp)
{
	int	idstrlen;
	char	*idstr, *newidstr;

	*type = sadb2psid(sidp->sadb_ident_type);
	idstr = (char *)(sidp + 1);
	idstrlen = strlen(idstr) + 1;
	/* sanity check */
	if ((totallen - sizeof (sadb_ident_t)) < idstrlen) {
		return (IKE_ERR_REQ_INVALID);
	}
	newidstr = ssh_malloc(idstrlen);
	if (newidstr == NULL) {
		*uerrp = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}
	if (strlcpy(newidstr, idstr, idstrlen) >= idstrlen)
		return (IKE_ERR_REQ_INVALID);
	*id = newidstr;

	return (0);
}

/*
 * cpout_xfs assumes that the buffer pointed to by dstp is at least
 * rp->num_xforms * sizeof (ike_p1_xform_t) bytes, and that the xforms
 * field of rp points to an array of at least rp->num_xforms ptrs.
 */
static void
cpout_xfs(ike_p1_xform_t *dstp, struct ike_rule *rp)
{
	int			i;
	ike_p1_xform_t		*p;
	struct ike_xform	**xfp = rp->xforms;

	for (i = 0, p = dstp; i < rp->num_xforms; i++, p++) {
		p->p1xf_dh_group = xfp[i]->oakley_group;
		p->p1xf_encr_alg = sshencr_to_sadb(xfp[i]->encr_alg);
		p->p1xf_encr_low_bits = xfp[i]->encr_low_bits;
		p->p1xf_encr_high_bits = xfp[i]->encr_high_bits;
		p->p1xf_auth_alg = sshauth_to_sadb(xfp[i]->auth_alg);
		p->p1xf_auth_meth = xfp[i]->auth_method;
		p->p1xf_pfs = rp->p2_pfs;
		p->p1xf_max_secs = xfp[i]->p1_lifetime_secs;
		/* we don't currently use the next three */
		p->p1xf_prf = 0;
		p->p1xf_max_kbytes = 0;
		p->p1xf_max_keyuses = 0;
	}
}

/*
 * cpin_xfs assumes that the buffer pointed to by srcp is at least cnt *
 * sizeof (ike_p1_xform_t) bytes.  It allocates an array of cnt pointers
 * to set in rp->xforms, as well as an individual buffer (sizeof (struct
 * ike_xform)) for each array element.  If a system error occurs (and
 * thus the return value is IKE_ERR_SYS_ERR), *uerrp is assigned the
 * appropriate errno.
 */
static int
cpin_xfs(struct ike_rule *rp, ike_p1_xform_t *srcp, int cnt, int *uerrp)
{
	int			i, j;
	ike_p1_xform_t		*p;
	struct ike_xform	**xfp;

	rp->num_xforms = cnt;
	rp->xforms = xfp = ssh_malloc(cnt * sizeof (struct ike_xform *));
	if (rp->xforms == NULL) {
		*uerrp = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}

	rp->p2_pfs = srcp->p1xf_pfs;

	for (i = 0, p = srcp; i < cnt; i++, p++) {
		xfp[i] = ssh_malloc(sizeof (struct ike_xform));
		if (xfp[i] == NULL) {
			/* free whatever we've alloc'd so far */
			for (j = --i; j >= 0; j--)
				ssh_free(xfp[j]);
			ssh_free(rp->xforms);
			*uerrp = ENOMEM;
			return (IKE_ERR_SYS_ERR);
		}
		xfp[i]->encr_alg = p->p1xf_encr_alg;
		xfp[i]->encr_low_bits = p->p1xf_encr_low_bits;
		xfp[i]->encr_high_bits = p->p1xf_encr_high_bits;
		xfp[i]->auth_alg = p->p1xf_auth_alg;
		xfp[i]->oakley_group = p->p1xf_dh_group;
		xfp[i]->auth_method = p->p1xf_auth_meth;
		xfp[i]->p1_lifetime_secs = p->p1xf_max_secs;
	}

	return (0);
}

/*
 * cpout_as assumes that the buffer pointed to by dstp is at least
 * asp->num_includes * sizeof (ike_addr_pr_t) bytes, and that the
 * includes field of ike_addrspec points to an array of at least
 * asp->num_includes ptrs to ike_addrrange structs.
 */
static void
cpout_as(ike_addr_pr_t *dstp, struct ike_addrspec *asp)
{
	int			i, cplen;
	ike_addr_pr_t		*p;
	struct ike_addrrange	**arp = asp->includes;

	for (i = 0, p = dstp; i < asp->num_includes; i++, p++) {

		if (arp[i]->beginaddr.ss.ss_family !=
		    arp[i]->endaddr.ss.ss_family) {
			PRTDBG(D_DOOR,
			    ("Address family mismatch in address spec output"
			    "range, skipping"));
			continue;
		}

		switch (arp[i]->beginaddr.ss.ss_family) {
		case AF_INET:
			cplen = sizeof (struct sockaddr_in);
			break;
		case AF_INET6:
			cplen = sizeof (struct sockaddr_in6);
			break;
		default:
			PRTDBG(D_DOOR, ("Unrecognized address family in output"
			    " address spec."));
			return;
		}

		(void) memcpy(&p->beg_iprange, &arp[i]->beginaddr.ss, cplen);
		(void) memcpy(&p->end_iprange, &arp[i]->endaddr.ss, cplen);
	}
}

/*
 * cpin_as assumes that the buffer pointed to by srcp is at least cnt *
 * sizeof (ike_addr_pr_t) bytes.  It allocates an array of cnt pointers
 * to set in asp->includes, as well as an individual buffer (sizeof (struct
 * ike_addrrange)) for each array element.  If the returned error value
 * is IKE_ERR_SYS_ERR, uerrp points to an appropriate unix errno.
 */
static int
cpin_as(struct ike_addrspec *asp, ike_addr_pr_t *srcp, int cnt, int *uerrp)
{
	int			i, j, cplen;
	ike_addr_pr_t		*p;
	struct ike_addrrange	**arp = asp->includes;
	struct sockaddr_storage *begsa, *endsa;

	asp->num_includes = cnt;
	asp->includes = ssh_malloc(cnt * sizeof (struct ike_addrrange *));
	if (asp->includes == NULL) {
		*uerrp = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}
	arp = asp->includes;

	for (i = 0, p = srcp; i < cnt; i++, p++) {
		begsa = &p->beg_iprange;
		endsa = &p->end_iprange;

		if (begsa->ss_family != endsa->ss_family) {
			PRTDBG(D_DOOR,
			    ("Address family mismatch in input address "
			    "range, skipping"));
			continue;
		}

		switch (begsa->ss_family) {
		case AF_INET:
			cplen = sizeof (struct sockaddr_in);
			break;
		case AF_INET6:
			cplen = sizeof (struct sockaddr_in6);
			break;
		default:
			PRTDBG(D_DOOR, ("Unrecognized address family in input"
			    " address spec."));
			return (IKE_ERR_DATA_INVALID);
		}

		arp[i] = ssh_malloc(sizeof (struct ike_addrrange));
		if (arp[i] == NULL) {
			/* free whatever we've malloc'd so far */
			for (j = --i; j >= 0; j--)
				ssh_free(arp[j]);
			ssh_free(asp->includes);
			*uerrp = ENOMEM;
			return (IKE_ERR_SYS_ERR);
		}
		(void) memcpy(&arp[i]->beginaddr.ss, &p->beg_iprange, cplen);
		(void) memcpy(&arp[i]->endaddr.ss, &p->end_iprange, cplen);
	}

	return (0);
}

/*
 * returns the total number of bytes needed (including null termination
 * and negation bytes) to contain all the strings in the includes and
 * excludes arrays in csp.
 */
static int
get_csid_size(struct certlib_certspec *csp)
{
	int	i, len = 0;

	for (i = 0; i < csp->num_includes; i++) {
		len += strlen(csp->includes[i]) + 1;
	}
	/* for excludes, add one for null, one for ! */
	for (i = 0; i < csp->num_excludes; i++) {
		len += strlen(csp->excludes[i]) + 2;
	}
	return (len);
}

/*
 * cpout_csid assumes that the buffer pointed to by dstp is at least len
 * bytes, and that the includes and excludes fields of csp point to arrays
 * of strings that contain no more than len bytes of data all together.
 */
static void
cpout_csid(char *dstp, struct certlib_certspec *csp, int len)
{
	int	i, cplen;
	char	*p = dstp;
	const char	**sp;

	sp = csp->includes;
	for (i = 0; i < csp->num_includes && len > 0; i++, len -= cplen) {
		cplen = strlcpy(p, sp[i], len) + 1;
		p += cplen;
	}
	if (i < csp->num_includes) {
		PRTDBG(D_DOOR, ("Ran out of buffer space during certspec "
		    "copy out!"));
		return;
	}

	sp = csp->excludes;
	for (i = 0; i < csp->num_excludes && len > 0; i++, len -= cplen) {
		cplen = strlcpy(p, sp[i], len) + 1;
		p += cplen;
	}
	if (i < csp->num_excludes) {
		PRTDBG(D_DOOR, ("Ran out of buffer space during certspec "
		    "copy out!"));
	}
}

/*
 * cpin_csid assumes that the buffer pointed to by srcp contains at least
 * incl + excl null-terminated strings.  It allocates arrays of incl and
 * excl pointers to set in csp->includes and csp->excludes, respectively,
 * as well as an individual buffer for each array element.  If the error
 * value returned is IKE_ERR_SYS_ERR, uerrp points to the unix errno value.
 */
static int
cpin_csid(struct certlib_certspec *csp, char *srcp, int incl, int excl,
    int *uerrp)
{
	int		i, len, icnt = 0, ecnt = 0;
	char		*p, *newp;
	boolean_t	is_incl;

	csp->includes = ssh_malloc(incl * sizeof (char *));
	if (csp->includes == NULL) {
		*uerrp = ENOMEM;
		goto free;
	}

	csp->excludes = ssh_malloc(excl * sizeof (char *));
	if (csp->excludes == NULL) {
		*uerrp = ENOMEM;
		goto free;
	}

	for (i = 0, p = srcp; i < incl + excl; i++, p += len) {
		if (p[0] == '!') {
			is_incl = FALSE;
			p++;
		} else {
			is_incl = TRUE;
		}
		len = strlen(p) + 1;
		newp = ssh_malloc(len);
		if (newp == NULL) {
			*uerrp = ENOMEM;
			goto free;
		}
		(void) strncpy(newp, p, len);
		if (is_incl)
			csp->includes[icnt++] = newp;
		else
			csp->excludes[ecnt++] = newp;
	}

	csp->num_includes = icnt;
	csp->num_excludes = ecnt;

	return (0);

free:
	/*
	 * error case; *uerrp must be set before jumping here
	 *
	 * NOTE:  free(NULL) is legit.
	 */
	for (i = 0; i < icnt; i++)
		ssh_free((char *)csp->includes[i]);
	for (i = 0; i < ecnt; i++)
		ssh_free((char *)csp->excludes[i]);
	ssh_free(csp->includes);
	ssh_free(csp->excludes);

	return (IKE_ERR_SYS_ERR);
}

static int
cpout_p1(char **rtnpp, int *bytes, phase1_t *walker, int ssize, int *uerr)
{
	int			lidbytes, ridbytes, keybytes;
	int			a_lidbytes, a_ridbytes, a_keybytes;
	ike_p1_sa_t		*sap;
	preshared_entry_t	*pep = NULL;

	PRTDBG(D_DOOR, ("Copying out Phase 1 list..."));

	/*
	 * Start by figuring out how much space we'll need,
	 * and then allocating our buffer.
	 *
	 * Note: 64-bit alignment is maintained, so for the
	 * variable-length add-ons, need to check size and
	 * round up if necessary.  Don't need to worry about
	 * this for stat and error structs, since they're
	 * defined to be 64-bit aligned.
	 */
	lidbytes = get_sadbid_size(walker->p1_localid);
	a_lidbytes = IKEDOORROUNDUP(lidbytes);
	ridbytes = get_sadbid_size(walker->p1_remoteid);
	a_ridbytes = IKEDOORROUNDUP(ridbytes);
	if (privilege >= IKE_PRIV_KEYMAT)
		keybytes = get_p1key_size(walker->p1_pminfo, &pep);
	else
		keybytes = 0;
	a_keybytes = IKEDOORROUNDUP(keybytes);
	*bytes = ssize + sizeof (ike_p1_sa_t) + sizeof (ike_p1_stats_t) +
	    sizeof (ike_p1_errors_t) + a_lidbytes + a_ridbytes + a_keybytes;
	*rtnpp = ssh_malloc(*bytes);
	if (*rtnpp == NULL) {
		*uerr = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}

	/*
	 * Set offsets and lengths.
	 */
	sap = (ike_p1_sa_t *)((uintptr_t)*rtnpp + ssize);
	sap->p1sa_stat_off = sizeof (ike_p1_sa_t);
	sap->p1sa_stat_len = sizeof (ike_p1_stats_t);
	sap->p1sa_error_off = sap->p1sa_stat_off + sap->p1sa_stat_len;
	sap->p1sa_error_len = sizeof (ike_p1_errors_t);
	sap->p1sa_localid_off = sap->p1sa_error_off + sap->p1sa_error_len;
	sap->p1sa_localid_len = lidbytes;
	sap->p1sa_remoteid_off = sap->p1sa_localid_off + a_lidbytes;
	sap->p1sa_remoteid_len = ridbytes;
	sap->p1sa_key_off = sap->p1sa_remoteid_off + a_ridbytes;
	sap->p1sa_key_len = keybytes;

	cpout_p1hdr(&sap->p1sa_hdr, walker->p1_pminfo);
	cpout_p1xform(&sap->p1sa_xform, walker->p1_pminfo);

	cp_ip(&sap->p1sa_ipaddrs.loc_addr, &walker->p1_local);
	cp_ip(&sap->p1sa_ipaddrs.rem_addr, &walker->p1_remote);

	/*
	 * now do variable-length structures...make sure length is
	 * non-zero before trying to copy!  We know stats and errors
	 * are non-zero, though, so don't bother checking on them.
	 */
	cpout_p1stats((ike_p1_stats_t *)((uintptr_t)sap + sap->p1sa_stat_off),
	    walker->p1_pminfo);
	cpout_p1errs((ike_p1_errors_t *)((uintptr_t)sap + sap->p1sa_error_off),
	    walker->p1_pminfo);

	if (lidbytes > 0)
		cp_sadbid((sadb_ident_t *)((uintptr_t)sap +
		    sap->p1sa_localid_off), walker->p1_localid);

	if (ridbytes > 0)
		cp_sadbid((sadb_ident_t *)((uintptr_t)sap +
		    sap->p1sa_remoteid_off), walker->p1_remoteid);

	if (keybytes > 0)
		cpout_p1keys((ike_p1_key_t *)((uintptr_t)sap +
		    sap->p1sa_key_off), walker->p1_pminfo, pep);

	return (0);
}

static int
cpin_ps(preshared_entry_t **pep, ike_ps_t *psp, int *uerrp)
{
	preshared_entry_t	*newpe;
	struct sockaddr_storage *sa;
	int			rtn;

	PRTDBG(D_DOOR, ("Copying in preshared list..."));

	/* make sure we have a key before proceeding */
	if (psp->ps_key_len == 0)
		return (IKE_ERR_REQ_INVALID);

	newpe = ssh_calloc(1, sizeof (preshared_entry_t));

	newpe->pe_ike_mode = psp->ps_ike_mode;
	newpe->pe_flds_mask |= PS_FLD_IKE_MODE;

	if ((rtn = cpin_pskey(newpe, psp, uerrp)) != 0)
		goto bail;

	/*
	 * IP addrs are a valid type of id for preshared entries,
	 * but not for sadb idents.  If there's an sadb ident present,
	 * grab the identity out of that; otherwise, use the ip addr.
	 */
	if (psp->ps_localid_len > 0) {
		rtn = cpin_psid(&newpe->pe_locidtype, &newpe->pe_locid,
		    (sadb_ident_t *)((uintptr_t)psp + psp->ps_localid_off),
		    psp->ps_localid_len, uerrp);
		if (rtn)
			goto bail;
	} else {
		cp_ip(&newpe->pe_locid_sa, &psp->ps_ipaddrs.loc_addr);
		sa = &psp->ps_ipaddrs.loc_addr;
		newpe->pe_locidtype = (sa->ss_family == AF_INET) ?
		    PS_ID_IP4 : PS_ID_IP6;
		newpe->pe_locid_plen = psp->ps_localid_plen;
	}
	newpe->pe_flds_mask |= PS_FLD_LOCID + PS_FLD_LOCID_TYPE;

	if (psp->ps_remoteid_len > 0) {
		rtn = cpin_psid(&newpe->pe_remidtype, &newpe->pe_remid,
		    (sadb_ident_t *)((uintptr_t)psp + psp->ps_remoteid_off),
		    psp->ps_remoteid_len, uerrp);
		if (rtn)
			goto bail;
	} else {
		cp_ip(&newpe->pe_remid_sa, &psp->ps_ipaddrs.rem_addr);
		sa = &psp->ps_ipaddrs.rem_addr;
		newpe->pe_remidtype = (sa->ss_family == AF_INET) ?
		    PS_ID_IP4 : PS_ID_IP6;
		newpe->pe_remid_plen = psp->ps_remoteid_plen;
	}
	newpe->pe_flds_mask |= PS_FLD_REMID + PS_FLD_REMID_TYPE;

	*pep = newpe;

	return (0);

bail:
	/* error case; uerrp must be set before jumping here */

	PRTDBG(D_DOOR, ("Copy in of preshared list failed."));
	/* NOTE:  free() works on NULL pointers. */
	ssh_free(newpe->pe_locid);
	ssh_free(newpe->pe_remid);
	ssh_free(newpe->pe_keybuf);
	ssh_free(newpe);

	return (rtn);
}

static int
cpout_ps(char **rtnpp, int *bytes, preshared_entry_t *walker, int ssize,
    int *uerr)
{
	int		lidbytes = 0, ridbytes = 0, ltype, rtype;
	int		a_lidbytes = 0, a_ridbytes = 0;
	int		keybytes, keybits;
	ike_ps_t	*psp;

	PRTDBG(D_DOOR, ("Copying out preshared list..."));

	/*
	 * Start by figuring out how much space we'll need,
	 * and then allocating our buffer.
	 *
	 * Note: 64-bit alignment is maintained, so for the
	 * variable-length add-ons, need to check size and
	 * round up if necessary.
	 */
	ltype = walker->pe_locidtype;
	if (ltype != PS_ID_IP && ltype != PS_ID_IP4 && ltype != PS_ID_IP6) {
		lidbytes = sizeof (sadb_ident_t) + strlen(walker->pe_locid) + 1;
		a_lidbytes = IKEDOORROUNDUP(lidbytes);
	}
	rtype = walker->pe_remidtype;
	if (rtype != PS_ID_IP && rtype != PS_ID_IP4 && rtype != PS_ID_IP6) {
		ridbytes = sizeof (sadb_ident_t) + strlen(walker->pe_remid) + 1;
		a_ridbytes = IKEDOORROUNDUP(ridbytes);
	}
	if (privilege >= IKE_PRIV_KEYMAT) {
		keybytes = walker->pe_keybuf_bytes;
		keybits = walker->pe_keybuf_lbits;
	} else {
		keybytes = keybits = 0;
	}
	*bytes = ssize + sizeof (ike_ps_t) + a_lidbytes + a_ridbytes + keybytes;
	*rtnpp = ssh_malloc(*bytes);
	if (*rtnpp == NULL) {
		*uerr = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}

	/*
	 * Set offsets and lengths.
	 */
	psp = (ike_ps_t *)((uintptr_t)*rtnpp + ssize);
	psp->ps_localid_off = sizeof (ike_ps_t);
	psp->ps_localid_len = lidbytes;
	psp->ps_remoteid_off = psp->ps_localid_off + a_lidbytes;
	psp->ps_remoteid_len = ridbytes;
	psp->ps_key_off = psp->ps_remoteid_off + a_ridbytes;
	psp->ps_key_len = keybytes;
	psp->ps_key_bits = keybits;

	psp->ps_ike_mode = walker->pe_ike_mode;

	/*
	 * now do variable-length structures...make sure
	 * length is non-zero before trying to copy!
	 * If there's not an identity, there must be an addr.
	 */
	if (lidbytes > 0) {
		cpout_psid((sadb_ident_t *)((uintptr_t)psp +
		    psp->ps_localid_off), ltype, walker->pe_locid, lidbytes);
	} else {
		cp_ip(&psp->ps_ipaddrs.loc_addr, &walker->pe_locid_sa);
		psp->ps_localid_plen = walker->pe_locid_plen;
	}

	if (ridbytes > 0) {
		cpout_psid((sadb_ident_t *)((uintptr_t)psp +
		    psp->ps_remoteid_off), rtype, walker->pe_remid, ridbytes);
	} else {
		cp_ip(&psp->ps_ipaddrs.rem_addr, &walker->pe_remid_sa);
		psp->ps_remoteid_plen = walker->pe_remid_plen;
	}

	if (keybytes > 0)
		cpout_pskey(psp, walker);

	return (0);
}

static int
cpin_rule(struct ike_rule **irp, ike_rule_t *drp, int *uerrp)
{
	int		rtn;
	struct ike_rule	*newir;

	PRTDBG(D_DOOR, ("Copying in rule list..."));

	newir = ssh_malloc(sizeof (struct ike_rule));
	if (newir == NULL) {
		*uerrp = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}
	(void) memset(newir, 0, sizeof (struct ike_rule));
	newir->refcount = 1;

	newir->label = ssh_malloc(strlen(drp->rule_label) + 1);
	if (newir->label == NULL) {
		ssh_free(newir);
		*uerrp = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}

	newir->local_idtype = sadb_to_sshidtype(drp->rule_local_idtype);
	newir->mode = drp->rule_ike_mode;
	newir->p1_nonce_len = drp->rule_p1_nonce_len;
	newir->p2_lifetime_secs = drp->rule_p2_lifetime_secs;
	newir->p2_softlife_secs = drp->rule_p2_softlife_secs;
	newir->p2_idletime_secs = drp->rule_p2_idletime_secs;
	newir->p2_lifetime_kb = drp->rule_p2_lifetime_kb;
	newir->p2_softlife_kb = drp->rule_p2_softlife_kb;
	newir->p2_nonce_len = drp->rule_p2_nonce_len;
	newir->p2_pfs = drp->rule_p2_pfs;

	/*
	 * Now for the more complicated structures...
	 */
	if ((rtn = cpin_as(&newir->local_addr, (ike_addr_pr_t *)
	    ((uintptr_t)drp + drp->rule_locip_off), drp->rule_locip_cnt,
	    uerrp)) != 0) {
		rule_free(newir);
		return (rtn);
	}

	if ((rtn = cpin_as(&newir->remote_addr, (ike_addr_pr_t *)
	    ((uintptr_t)drp + drp->rule_remip_off), drp->rule_remip_cnt,
	    uerrp)) != 0) {
		rule_free(newir);
		return (rtn);
	}

	if ((rtn = cpin_csid(&newir->local_id,
	    (char *)((uintptr_t)drp + drp->rule_locid_off),
	    drp->rule_locid_inclcnt, drp->rule_locid_exclcnt, uerrp)) != 0) {
		rule_free(newir);
		return (rtn);
	}

	if ((rtn = cpin_csid(&newir->remote_id,
	    (char *)((uintptr_t)drp + drp->rule_remid_off),
	    drp->rule_remid_inclcnt, drp->rule_remid_exclcnt, uerrp)) != 0) {
		rule_free(newir);
		return (rtn);
	}

	if ((rtn = cpin_xfs(newir, (ike_p1_xform_t *)
	    ((uintptr_t)drp + drp->rule_xform_off), drp->rule_xform_cnt,
	    uerrp)) != 0) {
		rule_free(newir);
		return (rtn);
	}

	*irp = newir;

	return (0);
}

static int
cpout_rule(char **rtnpp, int *bytes, struct ike_rule *walker, int ssize,
    int *uerr)
{
	int		xfbytes, lipbytes, ripbytes, lidbytes, ridbytes;
	ike_rule_t	*rp;

	PRTDBG(D_DOOR, ("Copying out rule list..."));

	/*
	 * Start by figuring out how much space we'll need,
	 * and then allocating our buffer.
	 *
	 * Note: 64-bit alignment is maintained in the ike_rule_t struct
	 * and in all the variable-length add-ons, except for the final
	 * set (the locid and remid strings).  Since they are char strings
	 * and appear at the end of the data buffer, 64-bit alignment is
	 * not necessary for them.  No explicit work is needed to do this,
	 * though, as the first three chunks are all arrays of 64-bit
	 * aligned structures.
	 */
	xfbytes = walker->num_xforms * sizeof (ike_p1_xform_t);
	lipbytes = walker->local_addr.num_includes * sizeof (ike_addr_pr_t);
	ripbytes = walker->remote_addr.num_includes * sizeof (ike_addr_pr_t);
	lidbytes = get_csid_size(&walker->local_id);
	ridbytes = get_csid_size(&walker->remote_id);
	*bytes = ssize + sizeof (ike_rule_t) + xfbytes + lipbytes + ripbytes +
	    lidbytes + ridbytes;
	*rtnpp = ssh_malloc(*bytes);
	if (*rtnpp == NULL) {
		*uerr = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}

	/*
	 * Set offsets and counts.
	 */
	rp = (ike_rule_t *)((uintptr_t)*rtnpp + ssize);
	rp->rule_xform_cnt = walker->num_xforms;
	rp->rule_xform_off = sizeof (ike_rule_t);
	rp->rule_locip_cnt = walker->local_addr.num_includes;
	rp->rule_locip_off = rp->rule_xform_off + xfbytes;
	rp->rule_remip_cnt = walker->remote_addr.num_includes;
	rp->rule_remip_off = rp->rule_locip_off + lipbytes;
	rp->rule_locid_inclcnt = walker->local_id.num_includes;
	rp->rule_locid_exclcnt = walker->local_id.num_excludes;
	rp->rule_locid_off = rp->rule_remip_off + ripbytes;
	rp->rule_remid_inclcnt = walker->remote_id.num_includes;
	rp->rule_remid_exclcnt = walker->remote_id.num_excludes;
	rp->rule_remid_off = rp->rule_locid_off + lidbytes;

	/*
	 * Fill in the easy stuff...
	 */
	if (walker->label != NULL)
		(void) strlcpy(rp->rule_label, walker->label, MAX_LABEL_LEN);
	else
		rp->rule_label[0] = 0;
	rp->rule_kmcookie = walker->cookie;
	rp->rule_ike_mode = walker->mode;
	rp->rule_local_idtype = sshidtype_to_sadb(walker->local_idtype);
	rp->rule_p1_nonce_len = walker->p1_nonce_len;
	rp->rule_p2_nonce_len = walker->p2_nonce_len;
	rp->rule_p2_pfs = walker->p2_pfs;
	rp->rule_p2_lifetime_secs = walker->p2_lifetime_secs;
	rp->rule_p2_softlife_secs = walker->p2_softlife_secs;
	rp->rule_p2_idletime_secs = walker->p2_idletime_secs;
	rp->rule_p2_lifetime_kb = walker->p2_lifetime_kb;
	rp->rule_p2_softlife_kb = walker->p2_softlife_kb;

	/*
	 * now do variable-length structures...make sure
	 * length is non-zero before trying to copy!
	 */
	if (xfbytes > 0)
		cpout_xfs((ike_p1_xform_t *)((uintptr_t)rp +
		    rp->rule_xform_off), walker);

	if (lipbytes > 0)
		cpout_as((ike_addr_pr_t *)((uintptr_t)rp + rp->rule_locip_off),
		    &walker->local_addr);

	if (ripbytes > 0)
		cpout_as((ike_addr_pr_t *)((uintptr_t)rp + rp->rule_remip_off),
		    &walker->remote_addr);

	if (lidbytes > 0)
		cpout_csid((char *)((uintptr_t)rp + rp->rule_locid_off),
		    &walker->local_id, lidbytes);

	if (ridbytes > 0)
		cpout_csid((char *)((uintptr_t)rp + rp->rule_remid_off),
		    &walker->remote_id, ridbytes);

	return (0);
}

static int
cpout_certs(char **rtnpp, int *bytes, ike_certcache_t *walker, int ssize,
    int *uerr)
{
	ike_certcache_t	*rp;

	PRTDBG(D_DOOR, ("Copying out cert list..."));

	*bytes = ssize + sizeof (ike_certcache_t);
	*rtnpp = ssh_malloc(*bytes);
	if (*rtnpp == NULL) {
		*uerr = ENOMEM;
		return (IKE_ERR_SYS_ERR);
	}

	/*
	 * Fill in values passed in from in.iked.
	 */
	rp = (ike_certcache_t *)((uintptr_t)*rtnpp + ssize);

	rp->certclass = walker->certclass;

	rp->cache_id = walker->cache_id;

	(void) strlcpy(rp->subject, walker->subject, DN_MAX);

	(void) strlcpy(rp->issuer, walker->issuer, DN_MAX);

	rp->linkage = walker->linkage;

	if (strncmp(rp->subject, rp->issuer, DN_MAX) == 0)
		(void) strlcpy(rp->issuer, gettext("Self-signed"), DN_MAX);

	return (0);
}

static int
do_dbg(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	ike_dbg_t	*dbgp = &reqp->svc_dbg;
	int		fd;
	uint32_t	olddebug;

	*rtnpp = NULL;
	*bytes = 0;

	switch (dbgp->cmd) {
	case IKE_SVC_GET_DBG:
		dbgp->dbg_level = debug;
		break;
	case IKE_SVC_SET_DBG:
		if (DOOR_FD_DESC_OK(descp)) {
			fd = descp->d_data.d_desc.d_descriptor;
			/*
			 * Use lseek as sanity check on descriptor
			 */
			if ((lseek(fd, 0, SEEK_END) == (off_t)-1) &&
			    (errno != ESPIPE)) {
				*uerr = errno;
				PRTDBG(D_DOOR,
				    ("New debug fd %u was bad!", fd));
				return (IKE_ERR_SYS_ERR);
			}
			PRTDBG(D_DOOR, ("User requested new debug logfile.\n"));
			(void) dup2(fd, fileno(debugfile));
		} else {
			if (debug == 0)
				return (IKE_ERR_NO_DESC);
		}
		PRTDBG(D_DOOR, ("User requested new debug level 0x%x",
		    dbgp->dbg_level));
		olddebug = debug;
		debug = dbgp->dbg_level;
		dbgp->dbg_level = olddebug;
		break;
	default:
		return (IKE_ERR_CMD_INVALID);
	}

	*rtnpp = (char *)reqp;
	*bytes = sizeof (ike_dbg_t);

	return (0);
}

/* ARGSUSED */
static int
do_priv(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	ike_priv_t	*privp = &reqp->svc_priv;
	uint32_t	oldpriv;

	*rtnpp = NULL;
	*bytes = 0;

	switch (privp->cmd) {
	case IKE_SVC_GET_PRIV:
		privp->priv_level = privilege;
		break;
	case IKE_SVC_SET_PRIV:
		if (privp->priv_level > privilege)
			return (IKE_ERR_NO_PRIV);
		PRTDBG(D_DOOR, ("User requested new privilege level 0x%x",
		    privp->priv_level));
		oldpriv = privilege;
		privilege = privp->priv_level;
		privp->priv_level = oldpriv;
		break;
	default:
		return (IKE_ERR_CMD_INVALID);
	}

	*rtnpp = (char *)privp;
	*bytes = sizeof (ike_priv_t);

	return (0);
}

/* ARGSUSED */
static int
do_stats(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	ike_statreq_t	*sreqp;
	ike_stats_t	*sp;
	int		cplen;

	cplen = sizeof (ike_stats_t);
	sreqp = (ike_statreq_t *)ssh_calloc(1, cplen + sizeof (ike_statreq_t));
	if (sreqp == NULL) {
		*rtnpp = NULL;
		*bytes = 0;
		*uerr = errno;
		return (IKE_ERR_SYS_ERR);
	}
	*rtnpp = (char *)sreqp;
	sp = (ike_stats_t *)(sreqp + 1);
	(void) memcpy(sp, &ikestats, cplen);

	sreqp->stat_len = cplen + sizeof (ike_statreq_t);
	*bytes = sreqp->stat_len;

	return (0);
}

/*
 * Return two struct ike_defaults_t; first one contains hard coded defaults
 * second contains any configuration defined values. If the configuration
 * does not specify a value, then '0' indicates the hard coded defaults are
 * to be used.
 */

/* ARGSUSED */
static int
do_defs(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	ike_defreq_t	*dreqp;
	ike_defaults_t	*dp, *ddp;
	int		cplen;

	cplen = (2 * sizeof (ike_defaults_t));
	dreqp = (ike_defreq_t *)ssh_calloc(1, cplen + sizeof (ike_defreq_t));
	if (dreqp == NULL) {
		*rtnpp = NULL;
		*bytes = 0;
		*uerr = errno;
		return (IKE_ERR_SYS_ERR);
	}

	*rtnpp = (char *)dreqp;

	/*
	 * First the hard coded defaults.
	 */
	dp = (ike_defaults_t *)(dreqp + 1);
	dp->rule_p1_lifetime_secs = DEF_P1_LIFETIME;
	dp->rule_p1_minlife = MIN_P1_LIFETIME;
	dp->rule_p1_nonce_len = DEF_NONCE_LENGTH;
	dp->rule_p2_lifetime_secs = DEF_P2_LIFETIME_HARD;
	dp->rule_p2_softlife_secs = DEF_P2_LIFETIME_SOFT;
	DEFIDLE(dp->rule_p2_idletime_secs, dp->rule_p2_softlife_secs);
	dp->rule_p2_minlife_hard_secs = MIN_P2_LIFETIME_HARD_SECS;
	dp->rule_p2_minlife_soft_secs = MIN_P2_LIFETIME_SOFT_SECS;
	dp->rule_p2_minlife_hard_kb = MIN_P2_LIFETIME_HARD_KB;
	dp->rule_p2_minlife_soft_kb = MIN_P2_LIFETIME_SOFT_KB;
	dp->rule_p2_minlife_idle_secs = MIN_P2_LIFETIME_IDLE_SECS;
	dp->rule_p2_maxlife_secs = MAX_P2_LIFETIME_SECS;
	dp->rule_p2_maxlife_kb = MAX_P2_LIFETIME_KB;
	dp->rule_p2_mindiff_secs = MINDIFF_SECS;
	dp->rule_p2_mindiff_kb = MINDIFF_KB;
	dp->rule_p2_nonce_len = DEF_NONCE_LENGTH;
	DEFKBYTES(dp->rule_p2_lifetime_kb, DEF_P2_LIFETIME_HARD);
	DEFKBYTES(dp->rule_p2_softlife_kb, DEF_P2_LIFETIME_SOFT);
	dp->conversion_factor = CONV_FACTOR;
	dp->rule_max_certs = DEFAULT_MAX_CERTS;
	dp->rule_ike_port = IPPORT_IKE;
	dp->rule_natt_port = nat_t_port;

	/*
	 * Now the defaults defined in the policy.
	 * Only modify values where the configuration has
	 * changed the value to non zero (IE: set)
	 */
	ddp = (ike_defaults_t *)(dp + 1);
	(void) memcpy(ddp, dp, sizeof (ike_defaults_t));
	if (ike_defs.rule_p1_lifetime_secs)
		ddp->rule_p1_lifetime_secs = ike_defs.rule_p1_lifetime_secs;
	if (ike_defs.rule_p1_nonce_len)
		ddp->rule_p1_nonce_len = ike_defs.rule_p1_nonce_len;
	if (ike_defs.rule_p2_lifetime_secs)
		ddp->rule_p2_lifetime_secs = ike_defs.rule_p2_lifetime_secs;
	if (ike_defs.sys_p2_lifetime_secs)
		ddp->sys_p2_lifetime_secs = ike_defs.sys_p2_lifetime_secs;
	if (ike_defs.rule_p2_softlife_secs)
		ddp->rule_p2_softlife_secs = ike_defs.rule_p2_softlife_secs;
	if (ike_defs.sys_p2_softlife_secs)
		ddp->sys_p2_softlife_secs = ike_defs.sys_p2_softlife_secs;
	if (ike_defs.rule_p2_idletime_secs)
		ddp->rule_p2_idletime_secs = ike_defs.rule_p2_idletime_secs;
	if (ike_defs.sys_p2_idletime_secs)
		ddp->sys_p2_idletime_secs = ike_defs.sys_p2_idletime_secs;
	if (ike_defs.rule_p2_lifetime_kb)
		ddp->rule_p2_lifetime_kb = ike_defs.rule_p2_lifetime_kb;
	if (ike_defs.sys_p2_lifetime_bytes)
		ddp->sys_p2_lifetime_bytes = ike_defs.sys_p2_lifetime_bytes;
	if (ike_defs.rule_p2_softlife_kb)
		ddp->rule_p2_softlife_kb = ike_defs.rule_p2_softlife_kb;
	if (ike_defs.sys_p2_softlife_bytes)
		ddp->sys_p2_softlife_bytes = ike_defs.sys_p2_softlife_bytes;
	if (ike_defs.rule_p2_pfs)
		ddp->rule_p2_pfs = ike_defs.rule_p2_pfs;
	if (ike_defs.rule_p2_nonce_len)
		ddp->rule_p2_nonce_len = ike_defs.rule_p2_nonce_len;
	if (ike_defs.rule_max_certs)
		ddp->rule_max_certs = ike_defs.rule_max_certs;

	dreqp->stat_len = cplen + (sizeof (ike_defreq_t));
	dreqp->version = DOORVER;
	*bytes = dreqp->stat_len;

	return (0);
}

/* ARGSUSED */
static int
do_p1dump(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	phase1_t	*walker;
	int		target, cnt, next, rtn;
	ike_dump_t	*dp = &reqp->svc_dump;

	PRTDBG(D_DOOR, ("Dumping Phase 1 list..."));

	*rtnpp = NULL;
	*bytes = 0;

	if (phase1_head == NULL)
		return (IKE_ERR_NO_OBJ);

	target = dp->dump_next;
	walker = phase1_head;
	for (cnt = 0; (cnt < target) && (walker != NULL); cnt++)
		walker = walker->p1_next;
	if (walker == NULL)
		return (IKE_ERR_ID_INVALID);
	next = (walker->p1_next == NULL) ? 0 : cnt + 1;
	PRTDBG(D_DOOR, ("  Found P1 target; index = %d, next = %d", cnt, next));

	rtn = cpout_p1(rtnpp, bytes, walker, sizeof (ike_dump_t), uerr);
	if (rtn != 0)
		return (rtn);

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	dp = (ike_dump_t *)*rtnpp;
	dp->cmd = IKE_SVC_DUMP_P1S;
	dp->dump_len = *bytes;
	dp->dump_next = next;

	return (0);
}

cachent_t *cache_walker = NULL;

/* ARGSUSED */
static int
do_certdump(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	cachent_t 	*walker;
	int		target, next, rtn;
	ike_dump_t	*dp = &reqp->svc_dump;

	PRTDBG(D_DOOR, ("Dumping cert cache..."));

	*rtnpp = NULL;
	*bytes = 0;

	if (cacheptr_head == NULL)
		return (IKE_ERR_NO_OBJ);
	if (cache_walker == NULL)
		cache_walker = cacheptr_head;

	target = dp->dump_next;
	walker = cache_walker;

	if (walker == NULL)
		return (IKE_ERR_ID_INVALID);
	next = (walker->next == NULL) ? 0 : dp->dump_next + 1;
	PRTDBG(D_DOOR,
	    ("  Found cert target; target = %d, next = %d", target, next));
	rtn = cpout_certs(rtnpp, bytes, walker->cache_ptr, sizeof (ike_dump_t),
	    uerr);
	if (rtn != 0)
		return (rtn);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	dp = (ike_dump_t *)*rtnpp;
	dp->cmd = IKE_SVC_DUMP_CERTCACHE;
	dp->dump_len = *bytes;
	dp->dump_next = next;
	cache_walker = walker->next;

	if (cache_walker == NULL)
		free_cert_cache();
	return (0);
}

/* ARGSUSED */
static int
do_ruledump(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	struct ike_rule	*walker;
	int		ruleid, next, rtn;
	ike_dump_t	*dp = &reqp->svc_dump;

	PRTDBG(D_DOOR, ("Dumping rules..."));

	*rtnpp = NULL;
	*bytes = 0;

	PRTDBG(D_DOOR, ("  Looking up rule %u (num=%u, alloc'd=%u)",
	    dp->dump_next, rules.num_rules, rules.allocnum_rules));
	ruleid = rulebase_lookup_nth(&rules, dp->dump_next);
	if (ruleid == -1) {
		PRTDBG(D_DOOR,
		    ("  Rulebase lookup failed."));
		return (IKE_ERR_NO_OBJ);
	}
	PRTDBG(D_DOOR, ("  Rulebase lookup returned rule id %u",
	    ruleid));
	walker = rules.rules[ruleid];
	next = (dp->dump_next >= rules.num_rules) ? 0 : dp->dump_next + 1;
	PRTDBG(D_DOOR, ("  Will return next dump = %u", next));

	rtn = cpout_rule(rtnpp, bytes, walker, sizeof (ike_dump_t), uerr);
	walker->refcount--;
	if (rtn != 0)
		return (rtn);

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	dp = (ike_dump_t *)*rtnpp;
	dp->cmd = IKE_SVC_DUMP_RULES;
	dp->dump_len = *bytes;
	dp->dump_next = next;

	return (0);
}

/* ARGSUSED */
static int
do_psdump(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	preshared_entry_t	*walker;
	int		next, rtn;
	ike_dump_t	*dp = &reqp->svc_dump;

	PRTDBG(D_DOOR, ("Dumping preshared..."));

	*rtnpp = NULL;
	*bytes = 0;
	if (privilege < IKE_PRIV_MODKEYS)
		return (IKE_ERR_NO_PRIV);

	walker = lookup_nth_ps(dp->dump_next);
	if (walker == NULL)
		return (IKE_ERR_NO_OBJ);
	next = (walker->pe_next == NULL) ? 0 : dp->dump_next + 1;

	rtn = cpout_ps(rtnpp, bytes, walker, sizeof (ike_dump_t), uerr);
	if (rtn != 0)
		return (rtn);

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	dp = (ike_dump_t *)*rtnpp;
	dp->cmd = IKE_SVC_DUMP_PS;
	dp->dump_len = *bytes;
	dp->dump_next = next;

	return (0);
}

/* ARGSUSED */
static int
do_groupdump(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	const struct SshIkeGroupDescRec *walker;
	int		target, next, rtn;
	ike_dump_t	*dp = &reqp->svc_dump;

	target = dp->dump_next;
	PRTDBG(D_DOOR, ("Dumping groups (%u of %u)...", target + 1,
	    ssh_ike_default_group_cnt));

	*rtnpp = NULL;
	*bytes = 0;

	if (target >= ssh_ike_default_group_cnt)
		return (IKE_ERR_ID_INVALID);
	walker = &ssh_ike_default_group[target];
	next = (target + 1 >= ssh_ike_default_group_cnt) ? 0 : target + 1;
	PRTDBG(D_DOOR, ("  Found group target; index = %d, next = %d",
	    target, next));

	rtn = cpout_group(rtnpp, bytes, walker, sizeof (ike_dump_t), uerr);
	if (rtn != 0)
		return (rtn);

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	dp = (ike_dump_t *)*rtnpp;
	dp->cmd = IKE_SVC_DUMP_GROUPS;
	dp->dump_len = *bytes;
	dp->dump_next = next;

	return (0);
}

/* ARGSUSED */
static int
do_algsdump(ike_service_t *reqp, door_desc_t *descp, char **rtnpp,
    int *bytes, int *uerr)
{
	const keywdtab_t *entry;
	int		target, next, rtn;
	ike_dump_t	*dp = &reqp->svc_dump;
	boolean_t encralgs = (dp->cmd == IKE_SVC_DUMP_ENCRALGS);

	target = dp->dump_next;
	PRTDBG(D_DOOR, ("Dumping %s algorithms (index %u)...",
	    encralgs ? "encryption" : "authentication", target));

	*rtnpp = NULL;
	*bytes = 0;

	if (encralgs)
		rtn = ike_get_cipher(target, &entry);
	else
		rtn = ike_get_hash(target, &entry);
	if (rtn == -1)
		return (IKE_ERR_ID_INVALID);
	next = (rtn == 0) ? 0 : target + 1;
	PRTDBG(D_DOOR, ("  Found %s target; index = %d, next = %d",
	    encralgs ? "encralg" : "authalg", target, next));

	if (encralgs)
		rtn = cpout_encralg(rtnpp, bytes, entry, sizeof (ike_dump_t),
		    uerr);
	else
		rtn = cpout_authalg(rtnpp, bytes, entry, sizeof (ike_dump_t),
		    uerr);
	if (rtn != 0)
		return (rtn);

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	dp = (ike_dump_t *)*rtnpp;
	if (encralgs)
		dp->cmd = IKE_SVC_DUMP_ENCRALGS;
	else
		dp->cmd = IKE_SVC_DUMP_AUTHALGS;
	dp->dump_len = *bytes;
	dp->dump_next = next;

	return (0);
}

/*
 * Returns 0 if success, 1 if fail, -1 if label not found
 * If global_pin is NULL, token locked
 * This function uses globals pkcs11_label_match and global_pin
 * because of limitations in libike's certlib
 */
static int
lock_unlock_pkcs11_tokens(struct certlib_cert *p)
{
	if (strncmp(pkcs11_label_match, p->pkcs11_label,
	    PKCS11_TOKSIZE - 1) != 0) {
		return (-1);
	} else {
		if (p->keys == NULL) {
			return (1);
		}

		if (global_pin == NULL) {
			return (del_private(p->keys));
		} else {
			p->keys->pkcs11_pin = global_pin->token_pin;
			return (accel_private(p->keys));
		}
	}
}


/* Set or delete a pin for a PKCS#11 Token object */
/* ARGSUSED */
static int
do_setdelpin(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	ike_pin_t *pin = &reqp->svc_pin;
	int n;


	*rtnpp = NULL;
	*bytes = 0;

	PRTDBG(D_DOOR, ("Trying to access PKCS#11 device \"%s\".",
	    pin->pkcs11_token));

	bzero(pkcs11_label_match, PKCS11_TOKSIZE);
	pkcs11_pad_out(pkcs11_label_match, pin->pkcs11_token);
	if (pin->cmd == IKE_SVC_SET_PIN) {
		global_pin = pin;
		certlib_token_pin = pin->token_pin;
		cmi_reload(); /* Tries pin if none available */
		n = certlib_iterate_certs_count(lock_unlock_pkcs11_tokens);
		certlib_token_pin = NULL;
		bzero(pin->token_pin, MAX_PIN_LEN);
		PRTDBG(D_DOOR, ("%d token objects unlocked", n));
	} else {
		global_pin = NULL;
		n = certlib_iterate_certs_count(lock_unlock_pkcs11_tokens);
		PRTDBG(D_DOOR, ("%d token objects locked", n));
	}

	*rtnpp = (char *)reqp;
	*bytes = sizeof (ike_service_t);

	if (n == 0)
		return (IKE_ERR_NO_TOKEN);
	return (0);
}

/*
 * Nuke a list of phase 1's based on IP address.
 */
static void
delete_all_p1_addrpair(phase1_t *walker, ike_get_t *rp)
{
	ike_addr_pr_t	*apr;

	do {
		/* Notify IKE library of P1 deletion. */
		delete_phase1(walker, B_TRUE);
		apr = (ike_addr_pr_t *)(rp + 1);
		walker = match_phase1(&apr->loc_addr, &apr->rem_addr,
		    NULL, NULL, NULL, B_TRUE);
	} while (walker != NULL);
}

/*
 * WARNING: The do_*getdel() functions rely on the fact that ike_get_t
 * and ike_del_t are identical (only the field names differ, their use
 * and size are the same).  If for some reason those structures change,
 * this code will need to be modified to accomodate that differenece.
 */
/* ARGSUSED */
static int
do_p1getdel(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	phase1_t	*walker;
	ike_addr_pr_t	*apr;
	ike_cky_pr_t	*cpr;
	ike_get_t	*rp = &reqp->svc_get;
	int		rtn;

	*rtnpp = NULL;
	*bytes = 0;

	PRTDBG(D_DOOR, ("Trying to delete p1."));

	switch (rp->get_idtype) {
	case IKE_ID_ADDR_PAIR:
		if (rp->get_len < (sizeof (ike_get_t) + sizeof (ike_addr_pr_t)))
			return (IKE_ERR_REQ_INVALID);
		apr = (ike_addr_pr_t *)(rp + 1);
		walker = match_phase1(&apr->loc_addr, &apr->rem_addr,
		    NULL, NULL, NULL, B_TRUE);
		break;
	case IKE_ID_CKY_PAIR:
		if (rp->get_len < (sizeof (ike_get_t) + sizeof (ike_cky_pr_t)))
			return (IKE_ERR_REQ_INVALID);
		cpr = (ike_cky_pr_t *)(rp + 1);
		walker = match_phase1_by_ckys(cpr->cky_i, cpr->cky_r);
		break;
	default:
		PRTDBG(D_DOOR, ("  Invalid id type %d", rp->get_idtype));
		return (IKE_ERR_ID_INVALID);
	}

	if (walker == NULL)
		return (IKE_ERR_NO_OBJ);

	if (rp->cmd == IKE_SVC_GET_P1) {
		rtn = cpout_p1(rtnpp, bytes, walker, sizeof (ike_get_t), uerr);
		if (rtn != 0)
			return (rtn);
	} else {
		if (rp->get_idtype == IKE_ID_ADDR_PAIR) {
			delete_all_p1_addrpair(walker, rp);
		} else {
			/* Notify IKE library of deletion. */
			delete_phase1(walker, B_TRUE);
		}
		*rtnpp = (char *)rp;
		*bytes = sizeof (ike_del_t);
	}

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	rp = (ike_get_t *)*rtnpp;
	rp->cmd = reqp->svc_cmd.cmd;
	rp->get_len = *bytes;
	rp->get_idtype = 0;

	return (0);
}

/* ARGSUSED */
static int
do_rulegetdel(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	char		*lp;
	int		llen, ruleid, rtn;
	ike_get_t	*rp = &reqp->svc_get;

	PRTDBG(D_DOOR, ("Deleting rule..."));

	*rtnpp = NULL;
	*bytes = 0;

	if (rp->get_idtype != IKE_ID_LABEL) {
		PRTDBG(D_DOOR, ("  Invalid id type %d", rp->get_idtype));
		return (IKE_ERR_ID_INVALID);
	}

	lp = (char *)(rp + 1);
	llen = rp->get_len - sizeof (ike_get_t);
	ruleid = rulebase_lookup(&rules, lp, llen);

	PRTDBG(D_DOOR, ("  Rulebase_lookup gives rule id %u", ruleid));

	if (ruleid == -1)
		return (IKE_ERR_NO_OBJ);

	if (rp->cmd == IKE_SVC_GET_RULE) {
		rtn = cpout_rule(rtnpp, bytes, rules.rules[ruleid],
		    sizeof (ike_get_t), uerr);
		rules.rules[ruleid]->refcount--;
		if (rtn != 0)
			return (rtn);
	} else {
		rules.rules[ruleid]->refcount--;
		(void) rulebase_delete_rule(&rules, ruleid);
		*rtnpp = (char *)rp;
		*bytes = sizeof (ike_del_t);
	}

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	rp = (ike_get_t *)*rtnpp;
	rp->cmd = reqp->svc_cmd.cmd;
	rp->get_len = *bytes;
	rp->get_idtype = 0;

	return (0);
}

/* ARGSUSED */
static int
do_psgetdel(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	preshared_entry_t	*walker;
	ike_addr_pr_t		*apr;
	struct sockaddr_in	*lsin, *rsin;
	struct sockaddr_in6	*lsin6, *rsin6;
	sadb_ident_t		*sid1, *sid2;
	ike_get_t		*rp = &reqp->svc_get;
	int			rtn;

	PRTDBG(D_DOOR, ("Looking up preshared entry..."));

	*rtnpp = NULL;
	*bytes = 0;

	if (privilege < IKE_PRIV_MODKEYS)
		return (IKE_ERR_NO_PRIV);

	switch (rp->get_idtype) {
	case IKE_ID_ADDR_PAIR:
		if (rp->get_len < (sizeof (ike_get_t) + sizeof (ike_addr_pr_t)))
			return (IKE_ERR_REQ_INVALID);
		apr = (ike_addr_pr_t *)(rp + 1);
		if (apr->loc_addr.ss_family != apr->rem_addr.ss_family)
			return (IKE_ERR_ID_INVALID);
		switch (apr->loc_addr.ss_family) {
		case AF_INET:
			lsin = (struct sockaddr_in *)&apr->loc_addr;
			rsin = (struct sockaddr_in *)&apr->rem_addr;
			walker = lookup_ps_by_in_addr(&lsin->sin_addr,
			    &rsin->sin_addr);
			break;
		case AF_INET6:
			lsin6 = (struct sockaddr_in6 *)&apr->loc_addr;
			rsin6 = (struct sockaddr_in6 *)&apr->rem_addr;
			walker = lookup_ps_by_in6_addr(&lsin6->sin6_addr,
			    &rsin6->sin6_addr);
			break;
		default:
			return (IKE_ERR_ID_INVALID);
		}
		break;
	case IKE_ID_IDENT_PAIR:
		if (rp->get_len <
		    (sizeof (ike_get_t) + (2 * sizeof (sadb_ident_t))))
			return (IKE_ERR_REQ_INVALID);
		sid1 = (sadb_ident_t *)(rp + 1);
		sid2 = (sadb_ident_t *)((uintptr_t)sid1 +
		    SADB_64TO8(sid1->sadb_ident_len));
		walker = lookup_ps_by_ident(sid1, sid2);
		break;
	default:
		PRTDBG(D_DOOR, ("  Invalid id type %d", rp->get_idtype));
		return (IKE_ERR_ID_INVALID);
	}

	if (walker == NULL)
		return (IKE_ERR_NO_OBJ);

	if (rp->cmd == IKE_SVC_GET_PS) {
		rtn = cpout_ps(rtnpp, bytes, walker, sizeof (ike_get_t), uerr);
		if (rtn != 0)
			return (rtn);
	} else {
		rtn = delete_ps(walker);
		if (rtn == 0) {
			return (IKE_ERR_NO_OBJ);
		} else {
			*rtnpp = (char *)rp;
			*bytes = sizeof (ike_del_t);
		}
	}

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	rp = (ike_get_t *)*rtnpp;
	rp->cmd = reqp->svc_cmd.cmd;
	rp->get_len = *bytes;
	rp->get_idtype = 0;

	return (0);
}

static int
do_rulenew(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	ike_new_t		*newp = &reqp->svc_new;
	int			rtn, fd;
	struct ike_rule		*irp;

	PRTDBG(D_DOOR, ("Creating new rule..."));

	*rtnpp = NULL;
	*bytes = 0;

	if (newp->new_len == 0) {
		/* need to read from file */
		if (!DOOR_FD_DESC_OK(descp))
			return (IKE_ERR_NO_DESC);
		fd = dup(descp->d_data.d_desc.d_descriptor);
		if (fd < 0)
			return (IKE_ERR_NO_DESC);

		if (config_load(NULL, fd, B_FALSE) != 0) {
			PRTDBG(D_DOOR, ("  Failed to load config."));
			*uerr = errno;
			return (IKE_ERR_SYS_ERR);
		}

		cmi_reload();
	} else {
		/* copy in from the included ike_rule_t struct */
		if (newp->new_len < sizeof (ike_rule_t))
			return (IKE_ERR_REQ_INVALID);
		if ((rtn = cpin_rule(&irp, (ike_rule_t *)(newp + 1), uerr))
		    != 0) {
			return (rtn);
		}

		if (rulebase_add(&rules, irp) != 0) {
			*uerr = errno;
			PRTDBG(D_DOOR, ("  Failed to add new rules."));
			/*
			 * need to free struct that was malloc'd in cpin_rule
			 */
			ssh_free(irp);
			return (IKE_ERR_SYS_ERR);
		}
	}

	*rtnpp = (char *)newp;
	*bytes = sizeof (ike_new_t);

	return (0);
}

static int
do_psnew(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	ike_new_t		*newp = &reqp->svc_new;
	preshared_entry_t	*pe;
	int			rtn, fd;
	char			*errstr;

	PRTDBG(D_DOOR, ("Creating new preshared..."));

	*rtnpp = NULL;
	*bytes = 0;

	if (privilege < IKE_PRIV_MODKEYS)
		return (IKE_ERR_NO_PRIV);

	if (newp->new_len == 0) {
		/* need to read from file */
		if (!DOOR_FD_DESC_OK(descp))
			return (IKE_ERR_NO_DESC);
		fd = dup(descp->d_data.d_desc.d_descriptor);
		if (fd < 0)
			return (IKE_ERR_NO_DESC);
		if ((errstr = preshared_load(NULL, fd, B_FALSE)) != NULL) {
			if (strncmp(errstr, "DUP", 3) == 0) {
				PRTDBG(D_DOOR,
				    ("  Ignored one or more duplicates when "
				    "loading preshared entries"));
				return (IKE_ERR_DUP_IGNORED);
			}
			PRTDBG(D_DOOR,
			    ("  Loading preshared failed: %s", errstr));
			return (IKE_ERR_DATA_INVALID);
		}
	} else {
		/* copy in from the included ike_ps_t struct */
		if (newp->new_len < sizeof (ike_ps_t))
			return (IKE_ERR_REQ_INVALID);
		if ((rtn = cpin_ps(&pe, (ike_ps_t *)(newp + 1), uerr)) != 0)
			return (rtn);
		if (!append_preshared_entry(pe)) {
			PRTDBG(D_DOOR, ("  Appending preshared failed."));
			return (IKE_ERR_DUP_IGNORED);
		}
	}

	*rtnpp = (char *)newp;
	*bytes = sizeof (ike_new_t);

	return (0);
}

static int
do_ruleread(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	ike_rw_t	*rwp = &reqp->svc_rw;
	char		*cfgfile = NULL;
	int		cfgfd = -1;

	PRTDBG(D_DOOR, ("Reading rule..."));

	*rtnpp = NULL;
	*bytes = 0;

	if (rwp->rw_loc == IKE_RW_LOC_DEFAULT) {
		if (cfile == NULL) {
			cfgfile = CONFIG_FILE;
		} else {
			cfgfile = cfile;
		}
	} else {
		if (!DOOR_FD_DESC_OK(descp))
			return (IKE_ERR_NO_DESC);
		cfgfd = dup(descp->d_data.d_desc.d_descriptor);
		if (cfgfd < 0)
			return (IKE_ERR_NO_DESC);
	}

	if (config_load(cfgfile, cfgfd, B_TRUE) != 0) {
		*uerr = errno;
		PRTDBG(D_DOOR, ("  Failed to load config."));
		return (IKE_ERR_SYS_ERR);
	}
	cmi_reload();

	*rtnpp = (char *)rwp;
	*bytes = sizeof (ike_rw_t);

	return (0);
}

/* ARGSUSED */
static int
do_psread(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	ike_rw_t	*rwp = &reqp->svc_rw;
	char		*errstr = NULL, *file = NULL;
	int		fd = 0;

	PRTDBG(D_DOOR, ("Reading preshared..."));

	*rtnpp = NULL;
	*bytes = 0;

	if (privilege < IKE_PRIV_MODKEYS)
		return (IKE_ERR_NO_PRIV);

	if (rwp->rw_loc == IKE_RW_LOC_DEFAULT) {
		file = PRESHARED_KEY_FILE;
	} else {
		if (!DOOR_FD_DESC_OK(descp))
			return (IKE_ERR_NO_DESC);
		fd = dup(descp->d_data.d_desc.d_descriptor);
		if (fd < 0)
			return (IKE_ERR_NO_DESC);
	}
	if ((errstr = preshared_load(file, fd, B_TRUE)) != NULL) {
		if (strncmp(errstr, "DUP", 3) == 0) {
			PRTDBG(D_DOOR,
			    ("  Ignored one or more duplicates when "
			    "loading preshared entries"));
			return (IKE_ERR_DUP_IGNORED);
		}
		PRTDBG(D_DOOR, ("  Loading preshared failed: %s",
		    errstr));
		return (IKE_ERR_DATA_INVALID);
	}

	*rtnpp = (char *)rwp;
	*bytes = sizeof (ike_rw_t);

	return (0);
}

static int
do_rulewrite(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	ike_rw_t	*rwp = &reqp->svc_rw;
	int		fd, rtn;

	PRTDBG(D_DOOR, ("Writing rule..."));

	*rtnpp = NULL;
	*bytes = 0;

	if (rwp->rw_loc == IKE_RW_LOC_DEFAULT)
		return (IKE_ERR_LOC_INVALID);

	if (!DOOR_FD_DESC_OK(descp))
		return (IKE_ERR_NO_DESC);

	fd = dup(descp->d_data.d_desc.d_descriptor);
	if (fd < 0)
		return (IKE_ERR_NO_DESC);

	if ((rtn = config_write(fd)) < 0) {
		*uerr = errno;
		PRTDBG(D_DOOR, ("  Writing config failed"));
		return (IKE_ERR_SYS_ERR);
	} else if (rtn == 0) {
		PRTDBG(D_DOOR, ("  No rules to write!"));
		return (IKE_ERR_NO_OBJ);
	}

	*rtnpp = (char *)rwp;
	*bytes = sizeof (ike_rw_t);

	return (0);
}

static int
do_pswrite(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	ike_rw_t	*rwp = &reqp->svc_rw;
	int		fd, rtn;
	char		*errstr;

	PRTDBG(D_DOOR, ("Writing preshared..."));

	*rtnpp = NULL;
	*bytes = 0;

	if (privilege < IKE_PRIV_KEYMAT)
		return (IKE_ERR_NO_PRIV);

	if (rwp->rw_loc == IKE_RW_LOC_DEFAULT)
		return (IKE_ERR_LOC_INVALID);

	if (!DOOR_FD_DESC_OK(descp))
		return (IKE_ERR_NO_DESC);

	fd = dup(descp->d_data.d_desc.d_descriptor);
	if (fd < 0)
		return (IKE_ERR_NO_DESC);

	if ((rtn = write_preshared(fd, &errstr)) < 0) {
		*uerr = errno;
		PRTDBG(D_DOOR, ("  Writing preshared failed: %s", errstr));
		return (IKE_ERR_SYS_ERR);
	} else if (rtn == 0) {
		PRTDBG(D_DOOR, ("  No preshareds to write!"));
		return (IKE_ERR_NO_OBJ);
	}

	*rtnpp = (char *)rwp;
	*bytes = sizeof (ike_rw_t);

	return (0);
}

/* ARGSUSED */
static int
do_p1flush(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	PRTDBG(D_DOOR, ("Flushing p1s..."));

	flush_cache_and_p1s();

	*rtnpp = (char *)reqp;
	*bytes = sizeof (ike_flush_t);

	return (0);
}

/* ARGSUSED */
static int
do_certflush(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	PRTDBG(D_DOOR, ("Flushing cert cache..."));

	flush_cert_cache();

	cmi_reload();

	*rtnpp = (char *)reqp;
	*bytes = sizeof (ike_flush_t);

	return (0);
}

/* ARGSUSED */
static int
do_rbdump(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	PRTDBG(D_DOOR, ("Dumping rules for debugging..."));

	rulebase_dbg_walk(&rules);

	*rtnpp = (char *)reqp;
	*bytes = sizeof (ike_service_t);

	return (0);
}

/* ARGSUSED */
static int
do_notsup(ike_service_t *reqp, door_desc_t *descp, char **rtnpp, int *bytes,
    int *uerr)
{
	*rtnpp = NULL;
	*bytes = 0;

	PRTDBG(D_DOOR, ("IKE Door Service: Unsupported cmd (%s)",
	    svccmdstr(reqp->svc_cmd.cmd)));

	return (IKE_ERR_CMD_NOTSUP);
}

#define	SETPIN_AUTH	"solaris.network.ipsec.ike.token.login"
#define	DELPIN_AUTH	"solaris.network.ipsec.ike.token.logout"
#define	GET_AUTH	"solaris.network.ipsec.ike.get"
#define	SET_AUTH	"solaris.network.ipsec.ike.set"
#define	DEL_AUTH	"solaris.network.ipsec.ike.del"
#define	ADD_AUTH	"solaris.network.ipsec.ike.add"
#define	ALL_IKE_AUTH	"solaris.network.ipsec.ike"
static const struct door_op {
	ike_svccmd_t	d_cmd;
	int  (*d_func)(ike_service_t *, door_desc_t *, char **, int *, int *);
	const char *authname;
} door_ops[] = {
	{ IKE_SVC_GET_DBG,		do_dbg, GET_AUTH },
	{ IKE_SVC_SET_DBG,		do_dbg, SET_AUTH },
	{ IKE_SVC_GET_PRIV,		do_priv, GET_AUTH },
	{ IKE_SVC_SET_PRIV,		do_priv, SET_AUTH },
	{ IKE_SVC_GET_STATS,		do_stats, GET_AUTH },
	{ IKE_SVC_GET_P1,		do_p1getdel, GET_AUTH },
	{ IKE_SVC_DEL_P1,		do_p1getdel, DEL_AUTH },
	{ IKE_SVC_DUMP_P1S,		do_p1dump, GET_AUTH },
	{ IKE_SVC_FLUSH_P1S,		do_p1flush, DEL_AUTH },
	{ IKE_SVC_GET_RULE,		do_rulegetdel, GET_AUTH  },
	{ IKE_SVC_NEW_RULE,		do_rulenew, ADD_AUTH  },
	{ IKE_SVC_DEL_RULE,		do_rulegetdel, DEL_AUTH  },
	{ IKE_SVC_DUMP_RULES,		do_ruledump, GET_AUTH  },
	{ IKE_SVC_READ_RULES,		do_ruleread, GET_AUTH  },
	{ IKE_SVC_WRITE_RULES,		do_rulewrite, ADD_AUTH  },
	{ IKE_SVC_GET_PS,		do_psgetdel, GET_AUTH  },
	{ IKE_SVC_NEW_PS,		do_psnew, ADD_AUTH  },
	{ IKE_SVC_DEL_PS,		do_psgetdel, DEL_AUTH  },
	{ IKE_SVC_DUMP_PS,		do_psdump, GET_AUTH  },
	{ IKE_SVC_READ_PS,		do_psread, GET_AUTH  },
	{ IKE_SVC_WRITE_PS,		do_pswrite, ALL_IKE_AUTH  },
	{ IKE_SVC_DBG_RBDUMP,		do_rbdump, ALL_IKE_AUTH  },
	{ IKE_SVC_GET_DEFS,		do_defs, GET_AUTH  },
	{ IKE_SVC_SET_PIN,		do_setdelpin, SETPIN_AUTH },
	{ IKE_SVC_DEL_PIN,		do_setdelpin, DELPIN_AUTH },
	{ IKE_SVC_DUMP_CERTCACHE,	do_certdump, GET_AUTH  },
	{ IKE_SVC_FLUSH_CERTCACHE,	do_certflush, DEL_AUTH  },
	{ IKE_SVC_DUMP_GROUPS,		do_groupdump, GET_AUTH },
	{ IKE_SVC_DUMP_ENCRALGS,	do_algsdump, GET_AUTH },
	{ IKE_SVC_DUMP_AUTHALGS,	do_algsdump, GET_AUTH },
	{ IKE_SVC_ERROR,		do_notsup, ALL_IKE_AUTH  },
};

static void
close_door_descs(door_desc_t *dp, uint_t ndesc)
{
	while (ndesc > 0) {
		int fd = dp->d_data.d_desc.d_descriptor;
		if (dp->d_attributes & DOOR_DESCRIPTOR)
			(void) close(fd);
		dp++;
		ndesc--;
	}
}

/* ARGSUSED */
static void
ike_door_service(void *cookie, char *dataptr, size_t datasize,
    door_desc_t *descptr, uint_t ndesc)
{
	int		eval, ueval, rtnsize;
	ike_service_t	*reqp;
	ike_err_t	err_rtn;
	ike_svccmd_t	cmd;
	char		*rtnp, *rtn1;
	const struct door_op	*op;
	ucred_t *dcred = NULL;
	uid_t	uid;
	struct passwd	pwd;
	char buf[1024];

	(void) mutex_lock(&door_lock);

	PRTDBG(D_DOOR, ("Running IKE door service..."));

	if (datasize < sizeof (ike_cmd_t)) {
		PRTDBG(D_DOOR, ("  argument too small (%lu)",
		    (ulong_t)datasize));
		eval = IKE_ERR_REQ_INVALID;
		goto bail;
	}

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	reqp = (ike_service_t *)dataptr;
	cmd = reqp->svc_cmd.cmd;
	if (cmd > IKE_SVC_MAX) {
		PRTDBG(D_DOOR,
		    ("  unrecognized cmd (%d)", cmd));
		eval = IKE_ERR_CMD_INVALID;
		goto bail;
	}
	op = &door_ops[cmd];
	PRTDBG(D_DOOR, ("  matched door operation table entry '%s'",
	    svccmdstr(op->d_cmd)));
	if (door_ucred(&dcred) != 0) {
		PRTDBG(D_DOOR,
		    ("  Could not get user creds."));
		eval = IKE_ERR_SYS_ERR;
		goto bail;
	}

	uid = ucred_getruid(dcred);
	if ((int)uid < 0) {
		PRTDBG(D_DOOR,
		    ("  Could not get user id."));
		eval = IKE_ERR_SYS_ERR;
		goto bail;
	}

	if (getpwuid_r(uid, &pwd, buf, sizeof (buf)) == NULL) {
		PRTDBG(D_DOOR,
		    ("  Could not get password entry."));
		eval = IKE_ERR_SYS_ERR;
		goto bail;
	}

	if (chkauthattr(op->authname, pwd.pw_name) != 1) {
		PRTDBG(D_DOOR,
		    ("  Not authorized for operation."));
		eval = IKE_ERR_NO_AUTH;
		goto bail;
	}

	/* Special case for certcache */
	if (cmd == IKE_SVC_DUMP_CERTCACHE) {
		ike_dump_t *dp = &reqp->svc_dump;

		if (cacheptr_head != NULL && dp->dump_next == 0) {
			if (gethrtime() < certcache_timer) {
				PRTDBG(D_DOOR,
				    ("  Certcache dump already in progress."));
				eval = IKE_ERR_IN_PROGRESS;
				goto bail;
			}
			free_cert_cache();
		}
		if (cacheptr_head == NULL) {
			get_cert_cache();
			if (cache_error) {
				eval = IKE_ERR_NO_MEM;
				free_cert_cache();
				cache_error = B_FALSE;
				goto bail;
			}
		}
		/* Reset timer for each operation */
		certcache_timer = gethrtime() +
		    (CERTCACHE_TIMEOUT * (hrtime_t)NANOSEC);
	}

	/* Privs and auths checked - perform operation now */
	eval = (*op->d_func)(reqp, descptr, &rtnp, &rtnsize, &ueval);
	if (eval != 0)
		goto bail;

	PRTDBG(D_DOOR, ("  return pointer = %p, return size = %u",
	    (void *)rtnp, rtnsize));

	(void) mutex_unlock(&door_lock);

	if ((rtnp != NULL) && (rtnp != dataptr)) {
		rtn1 = alloca(rtnsize);
		if (rtn1 != NULL)  {
			(void) memcpy(rtn1, rtnp, rtnsize);
			ssh_free(rtnp);
			rtnp = rtn1;
		}
	}

	close_door_descs(descptr, ndesc);
	if (dcred != NULL)
		ucred_free(dcred);

	if (door_return(rtnp, rtnsize, NULL, 0) == -1) {
		PRTDBG(D_DOOR, ("  door_return #1: error %s",
		    strerror(errno)));
		perror("door_return #1");
	}
	return;

bail:
	close_door_descs(descptr, ndesc);

	if (dcred != NULL)
		ucred_free(dcred);
	err_rtn.cmd = IKE_SVC_ERROR;
	err_rtn.ike_err = eval;
	err_rtn.ike_err_unix = (eval == IKE_ERR_SYS_ERR) ? ueval : 0;
	(void) mutex_unlock(&door_lock);
	if (door_return((char *)&err_rtn, sizeof (ike_err_t), NULL, 0) == -1) {
		PRTDBG(D_DOOR, ("  door_return #2: error %s",
		    strerror(errno)));
		perror("door_return #2");
	}
}

void
ike_door_init()
{
	int	fd, rtn, tries = 20;

	fd = door_create(ike_door_service, NULL, DOOR_NO_CANCEL);
	if (fd < 0)
		goto bail;

	/* we allow at most one argument descriptor per call */
	(void) door_setparam(fd, DOOR_PARAM_DESC_MAX, 1);

	/* a request is at least an ike_cmd_t */
	(void) door_setparam(fd, DOOR_PARAM_DATA_MIN, sizeof (ike_cmd_t));

	do {
		if ((unlink(DOORNM) < 0) && (errno != ENOENT))
			goto bail;

		/* create door with 0444 permissions */
		rtn = open(DOORNM, O_CREAT|O_EXCL, S_IRUSR|S_IRGRP|S_IROTH);

	} while ((rtn < 0) && (errno == EEXIST) && (tries-- > 0));

	if ((rtn < 0) || (tries == 0))
		goto bail;

	if (close(rtn) < 0)
		goto bail;

	if (fattach(fd, DOORNM) < 0)
		goto bail;

	return;

bail:
	if (fd >= 0)
		(void) close(fd);
	EXIT_FATAL2("IKE door initialization failed: %s", strerror(errno));
}

void
ike_door_destroy()
{
	(void) unlink(DOORNM);
}
