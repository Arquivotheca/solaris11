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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <door.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vlan.h>
#include <fcntl.h>
#include <libdllink.h>
#include <libdlaggr.h>
#include <libdlvnic.h>
#include <libdlvlan.h>
#include <liblldp.h>
#include <liblldp_lldpd.h>

#define	LLDP_ONSTR	"on"
#define	LLDP_OFFSTR	"off"

void		lldp_bitmap2str(uint8_t, char *, uint_t);

struct lldp_prop_desc;
typedef struct lldp_prop_desc lldp_prop_desc_t;

/* property set() callback */
typedef lldp_status_t	lldp_pd_setf_t(const char *, lldp_prop_desc_t *,
    void *, uint_t);

/* property get() callback */
typedef lldp_status_t	lldp_pd_getf_t(const char *, lldp_prop_desc_t *,
    char *, uint_t *, uint_t);

struct lldp_prop_desc {
	const char	*lpd_name;	/* property name */
	lldp_propclass_t lpd_pclass;	/* property class */
	lldp_proptype_t	lpd_ptype;	/* property ID */
	lldp_val_desc_t	lpd_defval;
	lldp_val_desc_t	*lpd_optval;
	uint_t		lpd_noptval;
	lldp_pd_setf_t	*lpd_setf;	/* set callback function */
	lldp_pd_getf_t	*lpd_getf;	/* get value callback function */
	uint_t		lpd_flags;	/* see below */
};

/*
 * `lpd_flags' values
 * LLDP_PROP_MULVAL:	property is multi-valued
 * LLDP_PROP_GLOBAL:	property does not apply to a single agent or a
 *			single TLV.
 */
#define	LLDP_PROP_MULVAL	0x00000001
#define	LLDP_PROP_GLOBAL	0x00000002

lldp_val_desc_t	lldp_mode_vals[] = {
	{ "txonly",	LLDP_MODE_TXONLY },
	{ "rxonly",	LLDP_MODE_RXONLY },
	{ "both",	LLDP_MODE_RXTX },
	{ "disable",	LLDP_MODE_DISABLE }
};

/*
 * Note: LLDP_..._NONE_TLV must be the first and LLDP_..._ALL.._TLV must
 * be the last TLV in these tables. See lldp_mask2str().
 */
static lldp_val_desc_t	lldp_basic_tlvs_vals[] = {
	{ LLDP_BASIC_NONE_TLVNAME,	LLDP_BASIC_NONE_TLV },
	{ LLDP_BASIC_PORTDESC_TLVNAME,	LLDP_BASIC_PORTDESC_TLV },
	{ LLDP_BASIC_SYSNAME_TLVNAME,	LLDP_BASIC_SYSNAME_TLV },
	{ LLDP_BASIC_SYSDESC_TLVNAME,	LLDP_BASIC_SYSDESC_TLV },
	{ LLDP_BASIC_SYSCAPAB_TLVNAME,	LLDP_BASIC_SYSCAPAB_TLV },
	{ LLDP_BASIC_MGMTADDR_TLVNAME,	LLDP_BASIC_MGMTADDR_TLV },
	{ LLDP_BASIC_ALL_TLVNAME,	LLDP_BASIC_ALL_TLV }
};

static lldp_val_desc_t	lldp_8021_tlvs_vals[] = {
	{ LLDP_8021_NONE_TLVNAME,	LLDP_8021_NONE_TLV },
	{ LLDP_8021_VLAN_NAME_TLVNAME,	LLDP_8021_VLAN_NAME_TLV },
	{ LLDP_8021_PVID_TLVNAME,	LLDP_8021_PVID_TLV },
	{ LLDP_8021_LINK_AGGR_TLVNAME,	LLDP_8021_LINK_AGGR_TLV },
	{ LLDP_8021_PFC_TLVNAME,	LLDP_8021_PFC_TLV },
	{ LLDP_8021_APPLN_TLVNAME,	LLDP_8021_APPLN_TLV },
	{ LLDP_8021_ALL_TLVNAME,	LLDP_8021_ALL_TLV }
};

static lldp_val_desc_t	lldp_8023_tlvs_vals[] = {
	{ LLDP_8023_NONE_TLVNAME,	LLDP_8023_NONE_TLV },
	{ LLDP_8023_MAXFRAMESZ_TLVNAME,	LLDP_8023_MAXFRAMESZ_TLV },
	{ LLDP_8023_ALL_TLVNAME,	LLDP_8023_ALL_TLV }
};

static lldp_val_desc_t	lldp_oracle_tlvs_vals[] = {
	{ LLDP_VIRT_NONE_TLVNAME,	LLDP_VIRT_NONE_TLV },
	{ LLDP_VIRT_VNIC_TLVNAME,	LLDP_VIRT_VNIC_TLV },
	{ LLDP_VIRT_ALL_TLVNAME,	LLDP_VIRT_ALL_TLV }
};

static lldp_val_desc_t  lldp_onoff_vals[] = {
	{ LLDP_ONSTR,	B_TRUE },
	{ LLDP_OFFSTR,	B_FALSE }
};

lldp_val_desc_t lldp_syscapab_vals[] = {
	{ LLDP_SYSCAPAB_OTHER_NAME,	LLDP_SYSCAPAB_OTHER },
	{ LLDP_SYSCAPAB_REPEATER_NAME,	LLDP_SYSCAPAB_REPEATER },
	{ LLDP_SYSCAPAB_MACBRIDGE_NAME,	LLDP_SYSCAPAB_MACBRIDGE },
	{ LLDP_SYSCAPAB_WLAN_AP_NAME,	LLDP_SYSCAPAB_WLAN_AP },
	{ LLDP_SYSCAPAB_ROUTER_NAME,	LLDP_SYSCAPAB_ROUTER },
	{ LLDP_SYSCAPAB_TELEPHONE_NAME,	LLDP_SYSCAPAB_TELEPHONE },
	{ LLDP_SYSCAPAB_DOCSIS_CD_NAME,	LLDP_SYSCAPAB_DOCSIS_CDEV },
	{ LLDP_SYSCAPAB_STATION_NAME,	LLDP_SYSCAPAB_STATION_ONLY },
	{ LLDP_SYSCAPAB_CVLAN_NAME,	LLDP_SYSCAPAB_CVLAN },
	{ LLDP_SYSCAPAB_SVLAN_NAME,	LLDP_SYSCAPAB_SVLAN },
	{ LLDP_SYSCAPAB_TPMR_NAME,	LLDP_SYSCAPAB_TPMR }
};

#define	LLDP_DEF_SUP_SYSCAPAB_NAME	"bridge,router,station"

static lldp_pd_setf_t	i_lldp_set_gstr, i_lldp_set_admin_status,
			i_lldp_set_onoff, i_lldp_set_syscapab, i_lldp_set_tlv,
			i_lldp_set_apt;

static lldp_pd_getf_t	i_lldp_get_str, i_lldp_get_admin_status, i_lldp_get_tlv,
			i_lldp_get_onoff, i_lldp_get_sup_syscapab,
			i_lldp_get_enab_syscapab, i_lldp_get_apt;

/*
 * All the supported agent/global_tlv/agent_tlv properties.
 */
static lldp_prop_desc_t lldp_prop_table[] = {
	{ "mode", LLDP_PROPCLASS_AGENT, LLDP_PROPTYPE_MODE,
	    { "disable", LLDP_MODE_DISABLE },
	    lldp_mode_vals, LLDP_VALCNT(lldp_mode_vals),
	    i_lldp_set_admin_status, i_lldp_get_admin_status, 0 },

	{ LLDP_BASICTLV_GRPNAME, LLDP_PROPCLASS_AGENT, LLDP_PROPTYPE_BASICTLV,
	    { LLDP_BASIC_NONE_TLVNAME, LLDP_BASIC_NONE_TLV},
	    lldp_basic_tlvs_vals, LLDP_VALCNT(lldp_basic_tlvs_vals),
	    i_lldp_set_tlv, i_lldp_get_tlv, LLDP_PROP_MULVAL },

	{ LLDP_8021TLV_GRPNAME, LLDP_PROPCLASS_AGENT, LLDP_PROPTYPE_8021TLV,
	    { LLDP_8021_NONE_TLVNAME, LLDP_8021_NONE_TLV },
	    lldp_8021_tlvs_vals, LLDP_VALCNT(lldp_8021_tlvs_vals),
	    i_lldp_set_tlv, i_lldp_get_tlv, LLDP_PROP_MULVAL },

	{ LLDP_8023TLV_GRPNAME, LLDP_PROPCLASS_AGENT, LLDP_PROPTYPE_8023TLV,
	    { LLDP_8023_NONE_TLVNAME, LLDP_8023_NONE_TLV },
	    lldp_8023_tlvs_vals, LLDP_VALCNT(lldp_8023_tlvs_vals),
	    i_lldp_set_tlv, i_lldp_get_tlv, LLDP_PROP_MULVAL },

	{ LLDP_VIRTTLV_GRPNAME, LLDP_PROPCLASS_AGENT, LLDP_PROPTYPE_VIRTTLV,
	    { LLDP_VIRT_NONE_TLVNAME, LLDP_VIRT_NONE_TLV },
	    lldp_oracle_tlvs_vals, LLDP_VALCNT(lldp_oracle_tlvs_vals),
	    i_lldp_set_tlv, i_lldp_get_tlv, LLDP_PROP_MULVAL },

	{ "supported", LLDP_PROPCLASS_SYSCAPAB_TLV, LLDP_PROPTYPE_SUP_SYSCAPAB,
	    { LLDP_DEF_SUP_SYSCAPAB_NAME, (LLDP_SYSCAPAB_ROUTER |
	    LLDP_SYSCAPAB_MACBRIDGE | LLDP_SYSCAPAB_STATION_ONLY) },
	    lldp_syscapab_vals, LLDP_VALCNT(lldp_syscapab_vals),
	    i_lldp_set_syscapab, i_lldp_get_sup_syscapab,
	    LLDP_PROP_MULVAL|LLDP_PROP_GLOBAL },

	{ "enabled", LLDP_PROPCLASS_SYSCAPAB_TLV, LLDP_PROPTYPE_ENAB_SYSCAPAB,
	    { "", 0 }, NULL, 0, i_lldp_set_syscapab, i_lldp_get_enab_syscapab,
	    LLDP_PROP_MULVAL|LLDP_PROP_GLOBAL },

	{ "ipaddr", LLDP_PROPCLASS_MGMTADDR_TLV, LLDP_PROPTYPE_IPADDR,
	    { "", 0 }, NULL, 0, i_lldp_set_gstr, i_lldp_get_str,
	    LLDP_PROP_GLOBAL },

	{ "willing", LLDP_PROPCLASS_PFC_TLV, LLDP_PROPTYPE_WILLING,
	    { "on", B_TRUE }, lldp_onoff_vals, LLDP_VALCNT(lldp_onoff_vals),
	    i_lldp_set_onoff, i_lldp_get_onoff, 0 },

	{ "apt", LLDP_PROPCLASS_APPLN_TLV, LLDP_PROPTYPE_APPLN,
	    { "", 0 }, NULL, 0, i_lldp_set_apt, i_lldp_get_apt,
	    LLDP_PROP_MULVAL },

	{ "willing", LLDP_PROPCLASS_APPLN_TLV, LLDP_PROPTYPE_WILLING,
	    { "off", B_FALSE }, lldp_onoff_vals, LLDP_VALCNT(lldp_onoff_vals),
	    i_lldp_set_onoff, i_lldp_get_onoff, 0 },

	{ NULL, LLDP_PROPCLASS_NONE, LLDP_PROPTYPE_NONE, {NULL, 0},
	    NULL, 0, NULL, NULL, 0}
};

const char *
lldp_mode2str(lldp_admin_status_t mode)
{
	int	i, count = LLDP_VALCNT(lldp_mode_vals);

	for (i = 0; i < count; i++) {
		if (lldp_mode_vals[i].lvd_val == mode)
			return (lldp_mode_vals[i].lvd_name);
	}
	return (NULL);

}

static lldp_prop_desc_t *
i_lldp_get_propdesc(lldp_propclass_t pclass, const char *pname)
{
	lldp_prop_desc_t *pdp = lldp_prop_table;

	for (; pdp->lpd_name != NULL; pdp++) {
		if ((pdp->lpd_pclass == pclass) && pname != NULL &&
		    strcmp(pdp->lpd_name, pname) == 0) {
			break;
		}
	}

	return (pdp->lpd_name == NULL ? NULL : pdp);
}

static lldp_status_t
i_lldp_getvalue_from_daemon(const char *laname, lldp_prop_desc_t *pdp,
    char *pval, uint_t *psize)
{
	lldpd_door_lprops_t	lprop;
	lldpd_gprops_retval_t	rval;
	int			err;

	bzero(&lprop, sizeof (lldpd_door_lprops_t));
	bzero(&rval, sizeof (lldpd_gprops_retval_t));
	lprop.lp_cmd = LLDPD_CMD_GET_PROP;
	lprop.lp_pclass = pdp->lpd_pclass;
	lprop.lp_ptype = pdp->lpd_ptype;
	/* for global TLV properties `laname' will be NULL */
	if (laname != NULL) {
		(void) strlcpy(lprop.lp_laname, laname,
		    sizeof (lprop.lp_laname));
	}
	err = lldp_door_call(&lprop, sizeof (lprop), &rval,
	    sizeof (rval));
	if (err == 0)
		*psize = snprintf(pval, *psize, "%s", rval.lpr_pval);
	return (lldp_errno2status(err));
}

static lldp_status_t
i_lldp_setvalue_in_daemon(const char *laname, lldp_prop_desc_t *pdp,
    lldp_pval_t lpval, uint32_t flags)
{
	lldpd_door_lprops_t	lprop;
	lldpd_sprops_retval_t	rval;
	int			err;

	bzero(&lprop, sizeof (lldpd_door_lprops_t));
	lprop.lp_cmd = LLDPD_CMD_SET_PROP;
	lprop.lp_pclass = pdp->lpd_pclass;
	lprop.lp_ptype = pdp->lpd_ptype;
	/* for global TLV properties `laname' will be NULL */
	if (laname != NULL) {
		(void) strlcpy(lprop.lp_laname, laname,
		    sizeof (lprop.lp_laname));
	}
	lprop.lp_pval = lpval;
	lprop.lp_flags = flags;
	err = lldp_door_call(&lprop, sizeof (lprop), &rval, sizeof (rval));
	return (lldp_errno2status(err));
}

static lldp_status_t
i_lldp_get_tlv(const char *laname, lldp_prop_desc_t *pdp, char *pval,
    uint_t *psize, uint_t valtype)
{
	lldp_status_t	status = LLDP_STATUS_NOTSUP;

	if (valtype != LLDP_OPT_ACTIVE)
		return (status);

	return (i_lldp_getvalue_from_daemon(laname, pdp, pval, psize));
}

static lldp_status_t
i_lldp_get_apt(const char *laname, lldp_prop_desc_t *pdp, char *pval,
    uint_t *psize, uint_t valtype)
{
	lldp_status_t	status = LLDP_STATUS_NOTSUP;

	switch (valtype) {
	case LLDP_OPT_ACTIVE:
		status = i_lldp_getvalue_from_daemon(laname, pdp, pval,
		    psize);
		break;
	case LLDP_OPT_POSSIBLE:
		*psize = snprintf(pval, *psize, "--");
		status = LLDP_STATUS_OK;
		break;
	}
	return (status);
}

static lldp_status_t
i_lldp_set_apt(const char *laname, lldp_prop_desc_t *pdp, void *pval,
    uint_t flags)
{
	lldp_status_t	status;
	lldp_pval_t	lpval;

	(void) strlcpy(lpval.lpv_strval, pval, sizeof (lpval.lpv_strval));
	status = i_lldp_setvalue_in_daemon(laname, pdp, lpval, flags);
	if (status == LLDP_STATUS_NOTFOUND && (flags & LLDP_OPT_DEFAULT) != 0)
		status = LLDP_STATUS_OK;
	return (status);
}

static lldp_status_t
i_lldp_set_tlv(const char *laname, lldp_prop_desc_t *pdp, void *pval,
    uint_t flags)
{
	uint32_t	tlvmask;
	lldp_pval_t	lpval;
	lldp_status_t	status;

	status = lldp_str2mask(pdp->lpd_name, pval, &tlvmask);
	if (status != LLDP_STATUS_OK)
		return (status);

	lpval.lpv_u32 = tlvmask;
	status = i_lldp_setvalue_in_daemon(laname, pdp, lpval, flags);
	if (status == LLDP_STATUS_NOTFOUND && (flags & LLDP_OPT_DEFAULT) != 0)
		status = LLDP_STATUS_OK;
	return (status);
}

static lldp_status_t
i_lldp_get_admin_status(const char *laname, lldp_prop_desc_t *pdp,
    char *pval, uint_t *psize, uint_t valtype)
{
	lldp_status_t		status = LLDP_STATUS_NOTSUP;

	if (valtype != LLDP_OPT_ACTIVE)
		return (status);

	return (i_lldp_getvalue_from_daemon(laname, pdp, pval, psize));
}

static lldp_status_t
i_lldp_set_admin_status(const char *laname, lldp_prop_desc_t *pdp, void *pval,
    uint_t flags)
{
	lldp_pval_t	lpval;
	lldp_val_desc_t	*vd;
	int		i;

	/* check if the pval is one of the possible values */
	vd = pdp->lpd_optval;
	for (i = 0; i < pdp->lpd_noptval; i++, vd++) {
		if (strcmp(vd->lvd_name, pval) == 0)
			break;
	}
	if (i == pdp->lpd_noptval)
		return (LLDP_STATUS_BADVAL);

	lpval.lpv_u32 = vd->lvd_val;
	return (i_lldp_setvalue_in_daemon(laname, pdp, lpval, flags));
}

static lldp_status_t
i_lldp_get_str(const char *laname, lldp_prop_desc_t *pdp, char *pval,
    uint_t *psize, uint_t valtype)
{
	lldp_status_t	status = LLDP_STATUS_NOTSUP;

	if (valtype != LLDP_OPT_ACTIVE)
		return (status);

	return (i_lldp_getvalue_from_daemon(laname, pdp, pval, psize));
}

static lldp_status_t
i_lldp_set_gstr(const char *laname, lldp_prop_desc_t *pdp, void *pval,
    uint_t flags)
{
	lldp_status_t	status;
	lldp_pval_t	lpval;

	(void) strlcpy(lpval.lpv_strval, pval, sizeof (lpval.lpv_strval));
	status = i_lldp_setvalue_in_daemon(laname, pdp, lpval, flags);
	if (status != LLDP_STATUS_OK) {
		if (status == LLDP_STATUS_NOTFOUND &&
		    (flags & LLDP_OPT_DEFAULT)) {
			status = LLDP_STATUS_OK;
		}
	}
	return (status);
}

static lldp_status_t
i_lldp_get_onoff(const char *laname, lldp_prop_desc_t *pdp, char *pval,
    uint_t *psize, uint_t valtype)
{
	lldp_status_t	status = LLDP_STATUS_NOTSUP;
	uint_t		valsize;
	int		bval;

	if (valtype != LLDP_OPT_ACTIVE)
		return (status);

	valsize = *psize;
	status = i_lldp_getvalue_from_daemon(laname, pdp, pval, &valsize);
	if (status == LLDP_STATUS_OK) {
		if (valsize < *psize) {
			bval = atoi(pval);
			*psize = snprintf(pval, *psize, "%s",
			    (bval == 1 ? LLDP_ONSTR : LLDP_OFFSTR));
		} else {
			*psize = valsize;
		}
	}
	return (status);
}

static lldp_status_t
i_lldp_set_onoff(const char *laname, lldp_prop_desc_t *pdp, void *pval,
    uint_t flags)
{
	lldp_status_t		status = LLDP_STATUS_OK;
	lldp_pval_t		lpval;
	lldp_val_desc_t		*vd;
	int			i;

	/* check if the pval is one of the possible values */
	vd = pdp->lpd_optval;
	for (i = 0; i < pdp->lpd_noptval; i++, vd++) {
		if (strcmp(vd->lvd_name, pval) == 0)
			break;
	}
	if (i == pdp->lpd_noptval)
		return (LLDP_STATUS_BADVAL);

	lpval.lpv_u32 = vd->lvd_val;
	status = i_lldp_setvalue_in_daemon(laname, pdp, lpval, flags);
	if (status == LLDP_STATUS_NOTFOUND && (flags & LLDP_OPT_DEFAULT) != 0)
		status = LLDP_STATUS_OK;
	return (status);
}

static lldp_status_t
i_lldp_get_sup_syscapab(const char *laname, lldp_prop_desc_t *pdp,
    char *pval, uint_t *psize, uint_t valtype)
{
	lldp_status_t	status = LLDP_STATUS_NOTSUP;

	if (valtype != LLDP_OPT_ACTIVE)
		return (status);

	return (i_lldp_getvalue_from_daemon(laname, pdp, pval, psize));
}

static lldp_status_t
i_lldp_set_syscapab(const char *laname, lldp_prop_desc_t *pdp, void *pval,
    uint_t flags)
{
	lldp_status_t	status;
	lldp_pval_t	lpval;
	uint16_t	capab;

	if ((status = lldp_str2syscapab(pval, &capab)) != LLDP_STATUS_OK)
		return (status);

	lpval.lpv_u32 = capab;
	return (i_lldp_setvalue_in_daemon(laname, pdp, lpval, flags));
}

static lldp_status_t
i_lldp_get_enab_syscapab(const char *laname, lldp_prop_desc_t *pdp,
    char *pval, uint_t *psize, uint_t valtype)
{
	lldp_status_t		status = LLDP_STATUS_NOTSUP;
	lldp_prop_desc_t	*tmp;

	switch (valtype) {
	case LLDP_OPT_POSSIBLE:
		/*
		 * Possible value for this property is the current value of the
		 * "supported" propertye.
		 */
		tmp = i_lldp_get_propdesc(LLDP_PROPCLASS_SYSCAPAB_TLV,
		    "supported");
		status = i_lldp_getvalue_from_daemon(laname, tmp, pval, psize);
		break;
	case LLDP_OPT_ACTIVE:
		status = i_lldp_getvalue_from_daemon(laname, pdp, pval, psize);
		break;
	default:
		break;
	}
	return (status);
}

lldp_status_t
i_lldp_setprop_common(const char *laname, const char *pname, char *pval,
    lldp_propclass_t pclass, uint_t pflags)
{
	lldp_prop_desc_t	*pdp;
	boolean_t		reset = (pflags & LLDP_OPT_DEFAULT);
	uint32_t		flagsmask = (LLDP_OPT_ACTIVE|LLDP_OPT_DEFAULT|\
	    LLDP_OPT_APPEND|LLDP_OPT_REMOVE);

	if (pname == NULL || (!reset && pval == NULL) ||
	    ((pflags & flagsmask) != LLDP_OPT_ACTIVE &&
	    (pflags & flagsmask) != LLDP_OPT_DEFAULT &&
	    (pflags & flagsmask) != LLDP_OPT_APPEND &&
	    (pflags & flagsmask) != LLDP_OPT_REMOVE)) {
		return (LLDP_STATUS_BADARG);
	}

	if ((pdp = i_lldp_get_propdesc(pclass, pname)) == NULL)
		return (LLDP_STATUS_PROPUNKNOWN);

	/*
	 * if it's not a multi-valued property then there should be no
	 * LLDP_OPT_APPEND or LLDP_OPT_REMOVE flag.
	 */
	if (!(pdp->lpd_flags & LLDP_PROP_MULVAL) &&
	    (pflags & (LLDP_OPT_APPEND|LLDP_OPT_REMOVE))) {
		return (LLDP_STATUS_BADARG);
	}

	/*
	 * If its a global property and `laname' was provided, return error.
	 * If its a per-agent property and `laname' was not provided, return
	 * error.
	 */
	if (pdp->lpd_flags & LLDP_PROP_GLOBAL) {
		if (laname != NULL)
			return (LLDP_STATUS_BADARG);
	} else {
		if (laname == NULL)
			return (LLDP_STATUS_BADARG);
	}

	if (pflags & LLDP_OPT_DEFAULT) {
		/*
		 * reset operation, see if a default value is specified. If
		 * specified we use that value as the value to be set. If
		 * it's NULL then we let the daemon `lldpd', to handle the
		 * default case.
		 */
		if (pdp->lpd_defval.lvd_name != NULL)
			pval = pdp->lpd_defval.lvd_name;
	}

	return (pdp->lpd_setf(laname, pdp, pval, pflags));
}

lldp_status_t
lldp_set_agentprop(const char *laname, const char *pname, char *pval,
    uint_t pflags)
{
	if (laname == NULL || *laname == '\0')
		return (LLDP_STATUS_BADARG);

	return (i_lldp_setprop_common(laname, pname, pval, LLDP_PROPCLASS_AGENT,
	    pflags));
}

lldp_status_t
lldp_set_global_tlvprop(const char *tlvname, const char *pname, char *pval,
    uint_t pflags)
{
	lldp_propclass_t	pclass;

	if ((pclass = lldp_tlvname2pclass(tlvname)) == LLDP_PROPCLASS_NONE)
		return (LLDP_STATUS_BADARG);

	return (i_lldp_setprop_common(NULL, pname, pval, pclass, pflags));
}

lldp_status_t
lldp_set_agent_tlvprop(const char *laname, const char *tlvname,
    const char *pname, char *pval, uint_t pflags)
{
	lldp_propclass_t	pclass;

	if (laname == NULL || *laname == '\0')
		return (LLDP_STATUS_BADARG);

	if ((pclass = lldp_tlvname2pclass(tlvname)) == LLDP_PROPCLASS_NONE)
		return (LLDP_STATUS_BADARG);

	return (i_lldp_setprop_common(laname, pname, pval, pclass, pflags));
}

static lldp_status_t
i_lldp_pd2permstr(lldp_prop_desc_t *pdp, char *buf, uint_t *bufsize)
{
	uint_t	perm;

	perm = 0;
	if (pdp->lpd_setf != NULL)
		perm |= MAC_PROP_PERM_WRITE;
	if (pdp->lpd_getf != NULL)
		perm |= MAC_PROP_PERM_READ;

	*bufsize = snprintf(buf, *bufsize, "%c%c",
	    ((perm & MAC_PROP_PERM_READ) != 0) ? 'r' : '-',
	    ((perm & MAC_PROP_PERM_WRITE) != 0) ? 'w' : '-');

	return (LLDP_STATUS_OK);
}

void
lldp_bitmap2str(uint8_t val, char *buf, uint_t bufsize)
{
	uint_t	mask = 128;
	int	i;

	for (i = 0; i < 8; i++, mask >>= 1) {
		if (mask & val)
			(void) strlcat(buf, "1", bufsize);
		else
			(void) strlcat(buf, "0", bufsize);
	}
}

/*
 * Function to retrieve property value for a given lldp agent represented by
 * link `laname'. If `laname' is set to NULL then the`pname' represents a
 * global property.
 *
 * `valtype' determines the type of value that will be retrieved.
 * 	LLDP_OPT_ACTIVE -	current value of the property (active config)
 *	LLDP_OPT_DEFAULT -	default hard coded value (boot-time value)
 *	LLDP_OPT_PERM -		read/write permissions for the value
 *	LLDP_OPT_POSSIBLE -	range of values
 *
 * Note: Since we do not support temporary operation, the current value is also
 * the persistent value.
 */
lldp_status_t
i_lldp_getprop_common(const char *laname, const char *pname, char *pval,
    uint_t *psize, uint_t valtype, lldp_propclass_t pclass)
{
	lldp_prop_desc_t	*pdp;
	lldp_status_t		status = LLDP_STATUS_OK;
	uint_t			cnt, i, bufsize;

	if (pname == NULL || (pval == NULL && *psize != 0))
		return (LLDP_STATUS_BADARG);

	if ((pdp = i_lldp_get_propdesc(pclass, pname)) == NULL)
		return (LLDP_STATUS_PROPUNKNOWN);

	/*
	 * If its a global property and `laname' was provided, return error.
	 * If its a per-agent property and `laname' was not provided, return
	 * error.
	 */
	if (pdp->lpd_flags & LLDP_PROP_GLOBAL) {
		if (laname != NULL)
			return (LLDP_STATUS_BADARG);
	} else {
		if (laname == NULL)
			return (LLDP_STATUS_BADARG);
	}

	bzero(pval, *psize);
	bufsize = *psize;
	switch (valtype) {
	case LLDP_OPT_PERM:
		status = i_lldp_pd2permstr(pdp, pval, &bufsize);
		break;
	case LLDP_OPT_ACTIVE:
		status = pdp->lpd_getf(laname, pdp, pval, &bufsize,
		    LLDP_OPT_ACTIVE);
		break;
	case LLDP_OPT_DEFAULT:
		if (pdp->lpd_defval.lvd_name == NULL) {
			status = LLDP_STATUS_NOTSUP;
			break;
		}
		bufsize = strlcpy(pval, pdp->lpd_defval.lvd_name, bufsize);
		break;
	case LLDP_OPT_POSSIBLE:
		cnt = pdp->lpd_noptval;
		if (cnt == 0) {
			status = pdp->lpd_getf(laname, pdp, pval, &bufsize,
			    LLDP_OPT_POSSIBLE);
		} else {
			uint_t nbytes = 0, tbytes = 0;

			for (i = 0; i < cnt; i++) {
				if (i == cnt - 1) {
					nbytes = snprintf(pval, bufsize, "%s",
					    pdp->lpd_optval[i].lvd_name);
				} else {
					nbytes = snprintf(pval, bufsize, "%s,",
					    pdp->lpd_optval[i].lvd_name);
				}
				tbytes += nbytes;
				if (tbytes >= *psize) {
					/*
					 * lets still continue and find the
					 * actual buffer size needed.
					 */
					pval = NULL;
					bufsize = 0;
				} else {
					pval += nbytes;
					bufsize -= nbytes;
				}
			}
			bufsize = tbytes;
		}
		break;
	default:
		status = LLDP_STATUS_BADARG;
		break;
	}

	if (status == LLDP_STATUS_OK && bufsize >= *psize) {
		status = LLDP_STATUS_TOOSMALL;
		*psize = bufsize;
	}

	return (status);
}

lldp_status_t
lldp_get_agentprop(const char *laname, const char *pname, char *pval,
    uint_t *psize, uint_t valtype)
{
	if (laname == NULL || *laname == '\0')
		return (LLDP_STATUS_BADARG);

	return (i_lldp_getprop_common(laname, pname, pval, psize,
	    valtype, LLDP_PROPCLASS_AGENT));
}

lldp_status_t
lldp_get_global_tlvprop(const char *tlvname, const char *pname, char *pval,
    uint_t *psize, uint_t valtype)
{
	lldp_propclass_t	pclass;

	if ((pclass = lldp_tlvname2pclass(tlvname)) == LLDP_PROPCLASS_NONE)
		return (LLDP_STATUS_BADARG);

	return (i_lldp_getprop_common(NULL, pname, pval, psize, valtype,
	    pclass));
}

lldp_status_t
lldp_get_agent_tlvprop(const char *laname, const char *tlvname,
    const char *pname, char *pval, uint_t *psize, uint_t valtype)
{
	lldp_propclass_t	pclass;

	if (laname == NULL || *laname == '\0')
		return (LLDP_STATUS_BADARG);

	if ((pclass = lldp_tlvname2pclass(tlvname)) == LLDP_PROPCLASS_NONE)
		return (LLDP_STATUS_BADARG);

	return (i_lldp_getprop_common(laname, pname, pval, psize, valtype,
	    pclass));
}

/*
 * Parses the buffer, for name-value pairs and creates nvlist. The boolean
 * `rvalue' defines if the `inbuf' contains just names or both names
 * and values
 */
int
lldp_str2nvlist(const char *inbuf, nvlist_t **nvl, boolean_t rvalue)
{
	char	*nv, *name, *val, *buf, *cp, *sep, *dupbuf;
	int	err;

	if (inbuf == NULL || inbuf[0] == '\0' || nvl == NULL)
		return (EINVAL);
	*nvl = NULL;

	/*
	 * If `rval' is B_FALSE, then `inbuf' should not contain values
	 */
	if (!rvalue && strchr(inbuf, '=') != NULL)
		return (EINVAL);

	if ((dupbuf = buf = strdup(inbuf)) == NULL)
		return (errno);

	while (isspace(*buf))
		buf++;

	if (*buf == '\0') {
		err = EINVAL;
		goto fail;
	}
	/*
	 * Note that the values can be multi-valued with comma as the
	 * separator. For example:
	 * 	prop1=va11,val2,val3,prop2=val4
	 * We will convert the above to,
	 *	prop1=val1,val2,val3;prop2=val4
	 */
	if (rvalue) {
		char	*tmp;

		cp = buf;
		while ((tmp = strchr(cp, '=')) != NULL) {
			*tmp = '\0';
			if ((cp = strrchr(cp, ',')) != NULL)
				*cp = ';';
			cp = tmp;
			*cp++ = '=';
		}
	}

	/*
	 * work on one nvpair at a time and extract the name and value.
	 */
	sep = (rvalue ? ";" : ",");
	while ((nv = strsep(&buf, sep)) != NULL) {
		name = nv;
		if ((val = strchr(nv, '=')) != NULL)
			*val++ = '\0';
		if (*nvl == NULL &&
		    (err = nvlist_alloc(nvl, NV_UNIQUE_NAME, 0)) != 0)
			goto fail;
		if (nvlist_exists(*nvl, name)) {
			err = EEXIST;
			goto fail;
		}
		if (val == NULL)
			err = nvlist_add_string(*nvl, name, "");
		else
			err = nvlist_add_string(*nvl, name, val);
		if (err != 0)
			goto fail;
	}
	free(dupbuf);
	return (0);
fail:
	free(dupbuf);
	nvlist_free(*nvl);
	*nvl = NULL;
	return (err);
}

lldp_status_t
lldp_mask2str(const char *tlvgrpname, uint32_t tlvmask, char *buf,
    uint_t *bufsize, boolean_t expand)
{
	lldp_prop_desc_t	*pdp;
	int			i, count = 0;
	uint32_t		mask, allmask;

	pdp = i_lldp_get_propdesc(LLDP_PROPCLASS_AGENT, tlvgrpname);
	if (pdp == NULL)
		return (LLDP_STATUS_BADARG);

	allmask = pdp->lpd_optval[pdp->lpd_noptval - 1].lvd_val;
	*buf = '\0';
	if (tlvmask == 0) {
		*bufsize = snprintf(buf, *bufsize, "%s",
		    pdp->lpd_optval[0].lvd_name);
	} else if (tlvmask == allmask && !expand) {
		*bufsize = snprintf(buf, *bufsize, "%s",
		    pdp->lpd_optval[pdp->lpd_noptval - 1].lvd_name);
	} else {
		uint_t	nbytes = 0, sz = *bufsize, tbytes = 0;

		count = pdp->lpd_noptval - 1;
		for (i = 1, mask = 1; i < count; i++, mask <<= 1) {
			if (!(tlvmask & mask))
				continue;
			if (tbytes == 0) {
				nbytes = snprintf(buf, sz, "%s",
				    pdp->lpd_optval[i].lvd_name);
			} else {
				nbytes = snprintf(buf, sz, ",%s",
				    pdp->lpd_optval[i].lvd_name);
			}
			tbytes += nbytes;
			if (tbytes >= *bufsize) {
				/*
				 * lets still continue and find the
				 * actual buffer size needed.
				 */
				buf = NULL;
				sz = 0;
			} else {
				buf += nbytes;
				sz -= nbytes;
			}
		}
		*bufsize = nbytes;
	}
	return (LLDP_STATUS_OK);
}

lldp_status_t
lldp_str2mask(const char *tlvgrpname, char *tlvstr, uint32_t *tlvmask)
{
	lldp_prop_desc_t	*pdp;
	uint32_t		mask;
	int			i, err;
	nvlist_t		*nvl;
	nvpair_t		*nvp;
	uint_t			tlvcnt = 0;
	char			*tlvname;

	pdp = i_lldp_get_propdesc(LLDP_PROPCLASS_AGENT, tlvgrpname);
	if (pdp == NULL)
		return (LLDP_STATUS_BADARG);

	if ((err = lldp_str2nvlist(tlvstr, &nvl, B_FALSE)) != 0)
		return (lldp_errno2status(err));

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		tlvcnt++;
	}

	/*
	 * you cannot possibly specify more values then the possible number
	 * of TLV values.
	 */
	if (tlvcnt > pdp->lpd_noptval) {
		nvlist_free(nvl);
		return (LLDP_STATUS_BADARG);
	}

	*tlvmask = 0;
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		tlvname = nvpair_name(nvp);
		/*
		 * one cannot have `none' or `all' and at the
		 * same time specify other tlvs.
		 */
		if ((strcmp(tlvname, "none") == 0 ||
		    strcmp(tlvname, "all") == 0) && tlvcnt > 1) {
			nvlist_free(nvl);
			return (LLDP_STATUS_BADARG);
		}
		for (i = 0; i < pdp->lpd_noptval; i++) {
			if (strcmp(pdp->lpd_optval[i].lvd_name, tlvname) == 0)
				break;
		}
		if (i == pdp->lpd_noptval) {
			nvlist_free(nvl);
			return (LLDP_STATUS_BADVAL);
		}
		mask = pdp->lpd_optval[i].lvd_val;
		if (*tlvmask == 0)
			*tlvmask = mask;
		else
			*tlvmask |= mask;
	}
	nvlist_free(nvl);
	return (LLDP_STATUS_OK);
}

lldp_status_t
lldp_walk_prop(lldp_prop_wfunc_t *func, void *arg, lldp_propclass_t pclass)
{
	lldp_prop_desc_t	*pdp = lldp_prop_table;

	/*
	 * We walk through all the properties for a given property class
	 *  `pclass'.
	 */
	for (; pdp->lpd_name != NULL; pdp++) {
		if ((pdp->lpd_pclass & pclass)) {
			if (!func(lldp_pclass2tlvname(pdp->lpd_pclass),
			    pdp->lpd_name, arg))
				break;
		}
	}
	return (LLDP_STATUS_OK);
}

boolean_t
lldp_is_enabled(const char *laname)
{
	lldp_prop_desc_t *pdp;
	lldp_status_t	status;
	char		pval[LLDP_MAXPROPVALLEN];
	uint_t		psize = sizeof (pval);

	pdp = i_lldp_get_propdesc(LLDP_PROPCLASS_AGENT, "mode");
	assert(pdp != NULL);
	status = i_lldp_getvalue_from_daemon(laname, pdp, pval, &psize);
	if (status != LLDP_STATUS_OK)
		return (B_FALSE);
	return (strcmp(pval, "disable") != 0);
}

/*
 * convert the comma separated string capability values into a mask
 */
lldp_status_t
lldp_str2syscapab(const char *capabstr, uint16_t *capab)
{
	int		err;
	nvlist_t	*nvl;
	nvpair_t	*nvp;
	uint_t		cnt = 0, i;
	char		*capabname;
	lldp_prop_desc_t *pdp;

	if (capabstr != NULL && strlen(capabstr) == 0) {
		*capab = 0;
		return (LLDP_STATUS_OK);
	}

	if ((err = lldp_str2nvlist(capabstr, &nvl, B_FALSE)) != 0)
		return (lldp_errno2status(err));

	if ((pdp = i_lldp_get_propdesc(LLDP_PROPCLASS_SYSCAPAB_TLV,
	    "supported")) == NULL) {
		nvlist_free(nvl);
		return (LLDP_STATUS_BADARG);
	}

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		cnt++;
	}

	if (cnt > pdp->lpd_noptval) {
		nvlist_free(nvl);
		return (LLDP_STATUS_BADARG);
	}

	*capab = 0;
	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		capabname = nvpair_name(nvp);
		for (i = 0; i < pdp->lpd_noptval; i++) {
			if (strcmp(pdp->lpd_optval[i].lvd_name, capabname) == 0)
				break;
		}
		if (i == pdp->lpd_noptval) {
			nvlist_free(nvl);
			return (LLDP_STATUS_BADARG);
		}
		*capab |= pdp->lpd_optval[i].lvd_val;
	}
	nvlist_free(nvl);
	return (LLDP_STATUS_OK);
}

uint_t
lldp_tlvname2pclass(const char *tlvname)
{
	if (tlvname == NULL)
		return (LLDP_PROPCLASS_NONE);
	else if (strcmp(tlvname, LLDP_BASIC_SYSCAPAB_TLVNAME) == 0)
		return (LLDP_PROPCLASS_SYSCAPAB_TLV);
	else if (strcmp(tlvname, LLDP_BASIC_MGMTADDR_TLVNAME) == 0)
		return (LLDP_PROPCLASS_MGMTADDR_TLV);
	else if (strcmp(tlvname, LLDP_8021_PFC_TLVNAME) == 0)
		return (LLDP_PROPCLASS_PFC_TLV);
	else if (strcmp(tlvname, LLDP_8021_APPLN_TLVNAME) == 0)
		return (LLDP_PROPCLASS_APPLN_TLV);
	return (LLDP_PROPCLASS_NONE);
}

char *
lldp_pclass2tlvname(lldp_propclass_t pclass)
{
	char	*str = NULL;

	switch (pclass) {
	case LLDP_PROPCLASS_SYSCAPAB_TLV:
		str = LLDP_BASIC_SYSCAPAB_TLVNAME;
		break;
	case LLDP_PROPCLASS_MGMTADDR_TLV:
		str = LLDP_BASIC_MGMTADDR_TLVNAME;
		break;
	case LLDP_PROPCLASS_PFC_TLV:
		str = LLDP_8021_PFC_TLVNAME;
		break;
	case LLDP_PROPCLASS_APPLN_TLV:
		str = LLDP_8021_APPLN_TLVNAME;
		break;
	default:
		break;
	}
	return (str);
}

char *
lldp_ptype2pname(lldp_proptype_t ptype)
{
	char	*str = NULL;

	switch (ptype) {
	case LLDP_PROPTYPE_MODE:
		str = "mode";
		break;
	case LLDP_PROPTYPE_BASICTLV:
		str = LLDP_BASICTLV_GRPNAME;
		break;
	case LLDP_PROPTYPE_8021TLV:
		str = LLDP_8021TLV_GRPNAME;
		break;
	case LLDP_PROPTYPE_8023TLV:
		str = LLDP_8023TLV_GRPNAME;
		break;
	case LLDP_PROPTYPE_VIRTTLV:
		str = LLDP_VIRTTLV_GRPNAME;
		break;
	case LLDP_PROPTYPE_SUP_SYSCAPAB:
		str = "supported";
		break;
	case LLDP_PROPTYPE_ENAB_SYSCAPAB:
		str = "enabled";
		break;
	case LLDP_PROPTYPE_IPADDR:
		str = "ipaddr";
		break;
	case LLDP_PROPTYPE_WILLING:
		str = "willing";
		break;
	case LLDP_PROPTYPE_APPLN:
		str = "apt";
		break;
	default:
		break;
	}
	return (str);
}
