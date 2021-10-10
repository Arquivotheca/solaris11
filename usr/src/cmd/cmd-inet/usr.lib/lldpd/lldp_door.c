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
#include <alloca.h>
#include <pwd.h>
#include <auth_attr.h>
#include <secdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <libscf.h>
#include <libdladm.h>
#include <libdllink.h>
#include <sys/vlan.h>
#include "lldp_impl.h"
#include "dcbx_impl.h"
#include "dcbx_pfc.h"
#include "dcbx_appln.h"

#define	LLDP_ADMINISTRATION_AUTH	"solaris.network.lldp"

/* Handler declaration for each door command */
typedef void lldpd_door_handler_t(void *);

static	lldpd_door_handler_t	lldpd_get_stats, lldpd_set_prop,
				lldpd_get_prop, lldpd_update_vlinks,
				lldpd_get_info;

typedef struct lldpd_door_info_s {
	uint_t			ldi_cmd;
	boolean_t		ldi_set;
	lldpd_door_handler_t	*ldi_handler;
} lldpd_door_info_t;

/* maps door commands to door handler functions */
static lldpd_door_info_t lldpd_door_info_tbl[] = {
	{ LLDPD_CMD_GET_INFO,		B_FALSE,	lldpd_get_info },
	{ LLDPD_CMD_GET_STATS,		B_FALSE,	lldpd_get_stats },
	{ LLDPD_CMD_GET_PROP, 		B_FALSE,	lldpd_get_prop },
	{ LLDPD_CMD_SET_PROP, 		B_TRUE,		lldpd_set_prop },
	{ LLDPD_CMD_UPDATE_VLINKS, 	B_TRUE,		lldpd_update_vlinks },
	{ 0, 0, NULL }
};

/*
 * The main server procedure function that gets invoked for any of the incoming
 * door commands. Inside this function we identify the incoming command and
 * invoke the right door handler function.
 */
/* ARGSUSED */
void
lldpd_handler(void *cookie, char *argp, size_t argsz, door_desc_t *dp,
    uint_t n_desc)
{
	lldpd_door_info_t	*infop = NULL;
	lldpd_retval_t		rval;
	int			i;
	uint_t			err;
	ucred_t			*cred = NULL;

	for (i = 0; lldpd_door_info_tbl[i].ldi_handler != NULL; i++) {
		if (lldpd_door_info_tbl[i].ldi_cmd ==
		    ((lldpd_door_arg_t *)(void *)argp)->ld_cmd) {
			infop = &lldpd_door_info_tbl[i];
			break;
		}
	}

	if (infop == NULL) {
		syslog(LOG_ERR, "Invalid door command specified");
		err = EINVAL;
		goto fail;
	}

	/* check for solaris.network.lldp */
	if (infop->ldi_set) {
		uid_t		uid;
		struct passwd	pwd;
		char		buf[1024];

		if (door_ucred(&cred) != 0) {
			err = errno;
			syslog(LOG_ERR, "Could not get user credentials.");
			goto fail;
		}
		uid = ucred_getruid(cred);
		if ((int)uid < 0) {
			err = errno;
			syslog(LOG_ERR, "Could not get user id.");
			goto fail;
		}
		if (getpwuid_r(uid, &pwd, buf, sizeof (buf)) ==
		    NULL) {
			err = errno;
			syslog(LOG_ERR, "Could not get password entry.");
			goto fail;
		}
		if (chkauthattr(LLDP_ADMINISTRATION_AUTH, pwd.pw_name) != 1) {
			err = EPERM;
			syslog(LOG_ERR, "Not authorized for operation.");
			goto fail;
		}
		ucred_free(cred);
	}

	/* individual handlers take care of calling door_return */
	infop->ldi_handler((void *)argp);
	return;
fail:
	ucred_free(cred);
	rval.lr_err = err;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

/* configure the operating status of the LLDP agent */
void
i_lldpd_set_mode(lldp_agent_t *lap, lldp_admin_status_t status)
{
	uint32_t	oldstate;

	if (lap->la_adminStatus != status) {
		oldstate = lap->la_adminStatus;
		lap->la_adminStatus = status;
		lldp_mode_changed(lap, oldstate);
		(void) pthread_cond_broadcast(&lap->la_rx_cv);
		(void) pthread_cond_broadcast(&lap->la_cond_var);
	}
	if (status == LLDP_MODE_DISABLE) {
		(void) lldp_agent_delete(lap);
		/*
		 * cancel the port monitor thread we started
		 * during the initialization of the lldp_agent_t.
		 */
		(void) pthread_cancel(lap->la_portmonitor);
	} else if (status == LLDP_MODE_RXTX) {
		/*
		 * if the underlying datalink supports DCB MODE then we
		 * enable DCB features by default.
		 */
		if (lldpd_islink_indcb(lap->la_linkid)) {
			/*
			 * enable PFC, if its supported and always enable
			 * application TLV.
			 */
			if (i_lldpd_set_tlv(lap, LLDP_PROPTYPE_8021TLV,
			    LLDP_8021_PFC_TLV|LLDP_8021_APPLN_TLV, NULL,
			    LLDP_OPT_APPEND) != 0) {
				syslog(LOG_WARNING, "Error enabling DCBx TLVs");
			}
		}
	}
}

static int
lldpd_get_lldpinfo(lldp_agent_t *lap, boolean_t neighbor, nvlist_t **nvl)
{
	int		err;
	nvlist_t	*mibnvl = NULL;

	if (neighbor) {
		uint32_t	timerid = 0;
		uint16_t	time = 0;
		nvpair_t	*nvp;

		lldp_rw_lock(&lap->la_rxmib_rwlock, LLDP_RWLOCK_READER);
		err = ENOENT;
		if (lap->la_remote_mib != NULL)
			err = nvlist_dup(lap->la_remote_mib, nvl, 0);
		lldp_rw_unlock(&lap->la_rxmib_rwlock);
		if (err != 0)
			return (err);

		if (lldp_nvlist_nelem(*nvl) == 0) {
			err = ENOENT;
			goto fail;
		}
		/*
		 * walk through every remote peer and determine the
		 * time for which the information will be valid.
		 */
		for (nvp = nvlist_next_nvpair(*nvl, NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(*nvl, nvp)) {
			if (nvpair_value_nvlist(nvp, &mibnvl) == 0 &&
			    nvlist_lookup_uint32(mibnvl,
			    LLDP_NVP_RXINFO_TIMER_ID, &timerid) == 0 &&
			    lldp_timerid2time(timerid, &time) == 0) {
				(void) nvlist_add_uint16(mibnvl,
				    LLDP_NVP_RXINFOVALID_FOR, time);
			}
		}
	} else {
		lldp_rw_lock(&lap->la_txmib_rwlock, LLDP_RWLOCK_READER);
		err = ENOENT;
		if (lap->la_local_mib != NULL)
			err = nvlist_dup(lap->la_local_mib, nvl, 0);
		lldp_rw_unlock(&lap->la_txmib_rwlock);
		if (err != 0)
			return (err);

		/* Add the txTTL, txTTR, maximum frame size info into mibnvl */
		if (nvlist_lookup_nvlist(*nvl, lap->la_msap, &mibnvl) == 0) {
			(void) lldp_add_ttl2nvlist(lap->la_txTTL, mibnvl);
			(void) nvlist_add_uint16(mibnvl, LLDP_NVP_NEXTTX_IN,
			    lap->la_txTTR);
		}
	}
	return (0);
fail:
	nvlist_free(*nvl);
	*nvl = NULL;
	return (err);
}

/* Get protocol info */
static void
lldpd_get_info(void *argp)
{
	lldpd_door_minfo_t	*infop = argp;
	lldpd_minfo_retval_t	rval, *rvalp;
	lldp_agent_t		*lap = NULL;
	datalink_id_t		linkid;
	char			*rbuf, *buf, *nbuf;
	size_t			rbufsize, buflen;
	nvlist_t		*nvl = NULL;
	int			err;

	/* set up for failure */
	rbuf = (char *)&rval;
	rbufsize = sizeof (rval);

	if (!lldpd_validate_link(dld_handle, infop->ldm_laname, &linkid,
	    &err)) {
		goto ret;
	}

	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
	/* Retrieve lldp_agent_t for the given data-link, if any. */
	lap = lldp_agent_get(linkid, &err);
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	if (err != 0)
		goto ret;

	err = lldpd_get_lldpinfo(lap, infop->ldm_neighbor, &nvl);
	if (err != 0)
		goto ret;

	if ((err = nvlist_size(nvl, &buflen, NV_ENCODE_NATIVE)) != 0)
		goto ret;

	buflen += sizeof (lldpd_minfo_retval_t);
	buf = alloca(buflen);
	rvalp = (lldpd_minfo_retval_t *)(void *)buf;
	rvalp->lmr_err = 0;
	rvalp->lmr_listsz = buflen - sizeof (lldpd_minfo_retval_t);
	nbuf = (char *)buf + sizeof (lldpd_minfo_retval_t);

	if ((err = nvlist_pack(nvl, &nbuf, &rvalp->lmr_listsz,
	    NV_ENCODE_NATIVE, 0)) != 0) {
		goto ret;
	}
	rbuf = buf;
	rbufsize = buflen;
ret:
	nvlist_free(nvl);
	if (lap != NULL)
		lldp_agent_refcnt_decr(lap);
	((lldpd_minfo_retval_t *)(void *)rbuf)->lmr_err = err;
	(void) door_return(rbuf, rbufsize, NULL, 0);
}

static void
lldpd_get_stats(void *argp)
{
	lldpd_door_lstats_t	*lstats = argp;
	lldpd_lstats_retval_t	rval;
	datalink_id_t		linkid;
	lldp_agent_t		*lap;
	int			err;

	if (!lldpd_validate_link(dld_handle, lstats->ld_laname, &linkid,
	    &err)) {
		goto ret;
	}

	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
	/* Retrieve lldp_agent_t for the given data-link, if any. */
	lap = lldp_agent_get(linkid, &err);
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	if (err == 0) {
		(void) memcpy(&rval.lr_stat, &lap->la_stats,
		    sizeof (rval.lr_stat));
	}
	if (lap != NULL)
		lldp_agent_refcnt_decr(lap);
ret:
	rval.lr_err = err;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

lldp_write2pdu_t *
i_lldp_get_write2pdu_nolock(lldp_agent_t *lap, const char *tlvname)
{
	lldp_write2pdu_t *wpdu;

	for (wpdu = list_head(&lap->la_write2pdu); wpdu != NULL;
	    wpdu = list_next(&lap->la_write2pdu, wpdu)) {
		lldp_tlv_info_t	*infop = wpdu->ltp_infop;

		if (infop->lti_name != NULL &&
		    strcmp(infop->lti_name, tlvname) == 0) {
			return (wpdu);
		}
	}

	return (NULL);
}

lldp_write2pdu_t *
i_lldp_get_write2pdu(lldp_agent_t *lap, const char *tlvname)
{
	lldp_write2pdu_t *wpdu;

	lldp_rw_lock(&lap->la_txmib_rwlock, LLDP_RWLOCK_READER);
	wpdu = i_lldp_get_write2pdu_nolock(lap, tlvname);
	lldp_rw_unlock(&lap->la_txmib_rwlock);
	return (wpdu);
}

/*
 * Reset operation. Remove all the TLV write callback function.
 */
int
i_lldpd_reset_tlv(lldp_agent_t *lap, const char *tlvgrpname,
    uint32_t tlvmask)
{
	lldp_tlv_info_t	*infop;
	nvlist_t	*nvl = NULL;
	nvpair_t	*nvp;
	char		tlvstr[LLDP_MAXPROPVALLEN], *tlvname;
	uint_t		tlvstrsz = sizeof (tlvstr);
	int		err;

	(void) lldp_mask2str(tlvgrpname, tlvmask, tlvstr, &tlvstrsz, B_TRUE);
	if ((err = lldp_str2nvlist(tlvstr, &nvl, B_FALSE)) != 0)
		return (err);

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		tlvname = nvpair_name(nvp);
		infop = lldp_get_tlvinfo_from_tlvname(tlvname);
		assert(infop != NULL);
		(void) lldp_write2pdu_remove(lap, infop->lti_writef);
	}
	nvlist_free(nvl);
	return (0);
}

int
i_lldpd_set_tlv(lldp_agent_t *lap, lldp_proptype_t ptype,
    uint32_t tlvmask, uint32_t *finalmask, uint32_t flags)
{
	nvlist_t	*nvl = NULL;
	nvpair_t	*nvp;
	lldp_tlv_info_t	*infop;
	lldp_write2pdu_t *wpdu;
	char		*tlvgrpname, *tlvname;
	char		tlvstr[LLDP_MAXPROPVALLEN];
	uint_t		tlvstrsz = sizeof (tlvstr);
	uint32_t	alltlvmask, oui;
	int		err = 0;

	if (finalmask != NULL)
		*finalmask = 0;

	if ((flags & (LLDP_OPT_APPEND|LLDP_OPT_REMOVE)) && tlvmask == 0)
		return (EINVAL);

	switch (ptype) {
	case LLDP_PROPTYPE_BASICTLV:
		alltlvmask = LLDP_BASIC_ALL_TLV;
		tlvgrpname = LLDP_BASICTLV_GRPNAME;
		oui = 0;
		break;
	case LLDP_PROPTYPE_8021TLV:
		alltlvmask = LLDP_8021_ALL_TLV;
		tlvgrpname = LLDP_8021TLV_GRPNAME;
		oui = LLDP_802dot1_OUI;
		break;
	case LLDP_PROPTYPE_8023TLV:
		alltlvmask = LLDP_8023_ALL_TLV;
		tlvgrpname = LLDP_8023TLV_GRPNAME;
		oui = LLDP_802dot3_OUI;
		break;
	case LLDP_PROPTYPE_VIRTTLV:
		alltlvmask = LLDP_VIRT_ALL_TLV;
		tlvgrpname = LLDP_VIRTTLV_GRPNAME;
		oui = LLDP_ORACLE_OUI;
		break;
	}

	lldp_rw_lock(&lap->la_txmib_rwlock, LLDP_RWLOCK_WRITER);
	if ((flags & LLDP_OPT_DEFAULT) ||
	    ((flags & LLDP_OPT_ACTIVE) && tlvmask == 0)) {
		err = i_lldpd_reset_tlv(lap, tlvgrpname, alltlvmask);
		goto ret;
	}

	(void) lldp_mask2str(tlvgrpname, tlvmask, tlvstr, &tlvstrsz, B_TRUE);
	if ((err = lldp_str2nvlist(tlvstr, &nvl, B_FALSE)) != 0)
		goto ret;
	/*
	 * check if we are trying to remove a non-existing value or
	 * adding an existing value.
	 */
	if (flags & (LLDP_OPT_REMOVE|LLDP_OPT_APPEND)) {
		for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(nvl, nvp)) {
			tlvname = nvpair_name(nvp);
			wpdu = i_lldp_get_write2pdu_nolock(lap, tlvname);

			if ((flags & LLDP_OPT_REMOVE) && wpdu == NULL) {
				err = EALREADY;
				goto ret;
			}
			if ((flags & LLDP_OPT_APPEND) && wpdu != NULL) {
				err = EEXIST;
				goto ret;
			}
		}
	} else if (flags & LLDP_OPT_ACTIVE) {
		/* we have to remove all the callback functions */
		err = i_lldpd_reset_tlv(lap, tlvgrpname, alltlvmask);
		if (err != 0)
			goto ret;
	}

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {
		tlvname = nvpair_name(nvp);
		infop = lldp_get_tlvinfo_from_tlvname(tlvname);
		if (flags & LLDP_OPT_REMOVE) {
			(void) lldp_write2pdu_remove(lap, infop->lti_writef);
		} else {
			(void) lldp_write2pdu_add(lap, infop,
			    infop->lti_writef, lap);
		}
	}

	/*
	 * If `finalmask' is non-null then the caller is interested in knowing
	 * the final mask, lets get it.
	 */
	if (finalmask != NULL) {
		char tlvstr[LLDP_MAXPROPVALLEN];

		bzero(&tlvstr, sizeof (tlvstr));
		for (wpdu = list_head(&lap->la_write2pdu); wpdu != NULL;
		    wpdu = list_next(&lap->la_write2pdu, wpdu)) {
			infop = wpdu->ltp_infop;
			if (infop != NULL && infop->lti_name != NULL &&
			    infop->lti_oui == oui) {
				if (tlvstr[0] != '\0') {
					(void) strlcat(tlvstr, ",",
					    sizeof (tlvstr));
				}
				(void) strlcat(tlvstr, infop->lti_name,
				    sizeof (tlvstr));
			}
		}
		(void) lldp_str2mask(tlvgrpname, tlvstr, finalmask);
	}
ret:
	lldp_rw_unlock(&lap->la_txmib_rwlock);
	/*
	 * Now that we have registered the right set of write callback
	 * functions, we call lldp_something_changed_local() to retransmit
	 * the pdu.
	 */
	if (err == 0)
		lldp_something_changed_local(lap);
	nvlist_free(nvl);
	return (err);
}

static int
i_lldpd_set_global_tlvprop(lldp_propclass_t pclass, lldp_proptype_t ptype,
    lldp_pval_t lpval, uint32_t flags)
{
	int		err = 0;
	lldp_syscapab_t	sc;
	uint16_t	*scapab, *ecapab, capab, ocapab;
	uint32_t	u32 = lpval.lpv_u32;
	char		*addrstr = lpval.lpv_strval;
	char		*tlvname = NULL, *nvpname = NULL;

	lldp_rw_lock(&lldp_sysinfo_rwlock, LLDP_RWLOCK_WRITER);
	switch (ptype) {
	case LLDP_PROPTYPE_SUP_SYSCAPAB:
		assert(pclass == LLDP_PROPCLASS_SYSCAPAB_TLV);
		/* get the current capabilities */
		bzero(&sc, sizeof (sc));
		if ((err = lldp_nvlist2syscapab(lldp_sysinfo, &sc)) != 0)
			break;
		scapab = &sc.ls_sup_syscapab;
		ecapab = &sc.ls_enab_syscapab;
		/*
		 * if we are modifying the supported capabilities make sure
		 * enabled capabilities are still valid and vice versa.
		 */
		ocapab = *scapab;
		capab = u32;
		if (flags & LLDP_OPT_APPEND) {
			*scapab |= capab;
		} else if (flags & LLDP_OPT_REMOVE) {
			uint16_t fcapab = (*scapab & ~capab);

			/*
			 * return error when removing non-existing value
			 * or when final capabilities does not include
			 * enabled capabilites.
			 */
			if ((*scapab & capab) != capab ||
			    (fcapab & *ecapab) != *ecapab) {
				err = EINVAL;
			} else {
				*scapab &= ~capab;
			}
		} else {
			if ((capab & *ecapab) != *ecapab)
				err = EINVAL;
			else
				*scapab = capab;
		}
		if (err == 0 && ocapab != *scapab) {
			/* add the modified system capabilities */
			err = lldp_add_syscapab2nvlist(&sc, lldp_sysinfo);
			if (err == 0) {
				tlvname = LLDP_BASIC_SYSCAPAB_TLVNAME;
				nvpname = LLDP_NVP_SYSCAPAB;
				/* persist the value */
				err = lldpd_persist_prop(pclass, ptype, NULL,
				    &sc.ls_sup_syscapab, DATA_TYPE_UINT16,
				    flags);
			}
		}
		break;
	case LLDP_PROPTYPE_ENAB_SYSCAPAB:
		assert(pclass == LLDP_PROPCLASS_SYSCAPAB_TLV);
		/* get the current capabilities */
		bzero(&sc, sizeof (sc));
		if ((err = lldp_nvlist2syscapab(lldp_sysinfo, &sc)) != 0)
			break;
		scapab = &sc.ls_sup_syscapab;
		ecapab = &sc.ls_enab_syscapab;
		/*
		 * if we are modifying the enabled capabilities make sure
		 * it is subset of supported capabilities.
		 */
		ocapab = *ecapab;
		capab = u32;
		if (flags & LLDP_OPT_APPEND) {
			uint16_t fcapab = (*ecapab | capab);

			if ((*scapab & fcapab) != fcapab) {
				err = EINVAL;
			} else if ((fcapab & LLDP_SYSCAPAB_STATION_ONLY) &&
			    (fcapab != LLDP_SYSCAPAB_STATION_ONLY)) {
				err = EINVAL;
			} else {
				*ecapab |= capab;
			}
		} else if (flags & LLDP_OPT_REMOVE) {
			/* return error when removing non-existing value */
			if ((*ecapab & capab) != capab)
				err = EINVAL;
			else
				*ecapab &= ~capab;
		} else if (flags & LLDP_OPT_ACTIVE) {
			if ((capab & LLDP_SYSCAPAB_STATION_ONLY) &&
			    capab != LLDP_SYSCAPAB_STATION_ONLY) {
				err = EINVAL;
			} else if ((*scapab & capab) != capab) {
				err = EINVAL;
			} else {
				*ecapab = capab;
			}
		} else {
			assert(flags & LLDP_OPT_DEFAULT);
			*ecapab = 0;
		}
		if (err == 0 && ocapab != *ecapab)  {
			/* add the modified system capabilities */
			err = lldp_add_syscapab2nvlist(&sc, lldp_sysinfo);
			if (err == 0) {
				tlvname = LLDP_BASIC_SYSCAPAB_TLVNAME;
				nvpname = LLDP_NVP_SYSCAPAB;
				/* persist the value */
				err = lldpd_persist_prop(pclass, ptype, NULL,
				    &sc.ls_enab_syscapab, DATA_TYPE_UINT16,
				    flags);
			}
		}
		break;
	case LLDP_PROPTYPE_IPADDR:
		assert(pclass == LLDP_PROPCLASS_MGMTADDR_TLV);
		if (flags == LLDP_OPT_DEFAULT) {
			/* reset the property */
			err = nvlist_remove(lldp_sysinfo, LLDP_NVP_MGMTADDR,
			    DATA_TYPE_NVLIST);
		} else if (flags == LLDP_OPT_ACTIVE) {
			err = i_lldpd_handle_mgmtaddr_prop(addrstr);
		} else {
			err = ENOTSUP;
		}
		if (err == 0) {
			tlvname = LLDP_BASIC_MGMTADDR_TLVNAME;
			nvpname = LLDP_NVP_MGMTADDR;
			/* persist the value */
			err = lldpd_persist_prop(pclass, ptype, NULL,
			    lpval.lpv_strval, DATA_TYPE_STRING, flags);
		}
		break;
	}
	lldp_rw_unlock(&lldp_sysinfo_rwlock);

	if (tlvname != NULL) {
		lldp_agent_t	*lap;
		nvlist_t	*nvl = NULL;

		/*
		 * Now that a global setting has been changed, we have to
		 * walk through every lldp_agent and call
		 * lldp_something_changed_local() on that agent only if
		 * the respective TLV is enabled for transmission.
		 */
		lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
		for (lap = list_head(&lldp_agents); lap != NULL;
		    lap = list_next(&lldp_agents, lap)) {
			lldp_rw_lock(&lap->la_txmib_rwlock, LLDP_RWLOCK_WRITER);
			if (i_lldp_get_write2pdu_nolock(lap, tlvname) == NULL) {
				lldp_rw_unlock(&lap->la_txmib_rwlock);
				continue;
			}
			/*
			 * We found an LLDP agent that is advertising this
			 * information. Lets update the agent's local mib.
			 */
			(void) nvlist_lookup_nvlist(lap->la_local_mib,
			    lap->la_msap, &nvl);
			lldp_rw_lock(&lldp_sysinfo_rwlock, LLDP_RWLOCK_READER);
			(void) lldp_merge_nested_nvl(nvl, lldp_sysinfo,
			    nvpname, NULL, NULL);
			lldp_rw_unlock(&lldp_sysinfo_rwlock);
			lldp_rw_unlock(&lap->la_txmib_rwlock);
			lldp_something_changed_local(lap);
		}
		lldp_rw_unlock(&lldp_agents_list_rwlock);
	}
	return (err);
}

static int
i_lldpd_set_agent_tlvprop(datalink_id_t linkid, lldp_propclass_t pclass,
    lldp_proptype_t ptype, lldp_pval_t pval, uint32_t flags)
{
	lldp_agent_t	*lap;
	void		*val;
	boolean_t	bval;
	data_type_t	dtype;
	char		buf[LLDP_MAXPROPVALLEN];
	int		err;

	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
	/* Retrieve lldp_agent_t for the given data-link, if any. */
	lap = lldp_agent_get(linkid, &err);
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	if (err != 0)
		return (err);
	lldp_mutex_lock(&lap->la_db_mutex);
	switch (pclass) {
	case LLDP_PROPCLASS_PFC_TLV:
		switch (ptype) {
		case LLDP_PROPTYPE_WILLING:
			err = dcbx_set_prop(lap, pclass, ptype,
			    &pval.lpv_u32, flags);
			bval = (pval.lpv_u32 == 0 ? B_FALSE : B_TRUE);
			val = &bval;
			dtype = DATA_TYPE_BOOLEAN_VALUE;
			break;
		default:
			err = ENOTSUP;
			break;
		}
		break;
	case LLDP_PROPCLASS_APPLN_TLV:
		switch (ptype) {
		case LLDP_PROPTYPE_APPLN:
			err = dcbx_set_prop(lap, pclass, ptype,
			    &pval.lpv_strval, flags);
			if (err != 0)
				break;
			/*
			 * this property is a multi-valued property and
			 * supports LLDP_OPT_APPEND/LLDP_OPT_REMOVE. Whenver
			 * these flags are used we need to get the complete
			 * list by calling dcbx_get_prop()
			 */
			if ((flags & (LLDP_OPT_APPEND|LLDP_OPT_REMOVE)) != 0) {
				err = dcbx_get_prop(lap, pclass, ptype, buf,
				    sizeof (buf));
				if (err != 0) {
					err = ENODATA;
					break;
				}
				val = buf;
			} else {
				val = pval.lpv_strval;
			}
			dtype = DATA_TYPE_STRING;
			break;
		case LLDP_PROPTYPE_WILLING:
			err = dcbx_set_prop(lap, pclass, ptype,
			    &pval.lpv_u32, flags);
			bval = (pval.lpv_u32 == 0 ? B_FALSE : B_TRUE);
			val = &bval;
			dtype = DATA_TYPE_BOOLEAN_VALUE;
			break;
		default:
			err = ENOTSUP;
			break;
		}
		break;
	default:
		err = ENOTSUP;
		break;
	}
	if (err == 0) {
		err = lldpd_persist_prop(pclass, ptype, lap->la_linkname,
		    val, dtype, flags);
	}
	lldp_mutex_unlock(&lap->la_db_mutex);
	lldp_agent_refcnt_decr(lap);
	return (err);
}

static int
i_lldpd_set_agent_prop(datalink_id_t linkid, lldp_proptype_t ptype,
    lldp_pval_t lpval, uint32_t flags)
{
	lldp_agent_t	*lap;
	uint32_t	mode, mask;
	int		err = 0;

	switch (ptype) {
	case LLDP_PROPTYPE_MODE:
		mode = lpval.lpv_u32;
		lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_WRITER);
		lap = lldp_agent_get(linkid, NULL);
		if (lap == NULL && mode == LLDP_MODE_DISABLE) {
			lldp_rw_unlock(&lldp_agents_list_rwlock);
			return (0);
		}
		if (lap == NULL) {
			if ((lap = lldp_agent_create(linkid, &err)) == NULL) {
				lldp_rw_unlock(&lldp_agents_list_rwlock);
				return (err);
			}
			lldp_agent_refcnt_incr(lap);
		}
		/*
		 * release the lldp_agents list lock since
		 * now we have reference to `lap'.
		 */
		lldp_rw_unlock(&lldp_agents_list_rwlock);

		/* acquire the `la_db_mutex' lock to serialize persistence */
		lldp_mutex_lock(&lap->la_db_mutex);
		i_lldpd_set_mode(lap, mode);
		err = lldpd_persist_prop(LLDP_PROPCLASS_AGENT, ptype,
		    lap->la_linkname, &mode, DATA_TYPE_UINT32, flags);
		lldp_mutex_unlock(&lap->la_db_mutex);
		lldp_agent_refcnt_decr(lap);
		break;
	case LLDP_PROPTYPE_BASICTLV:
	case LLDP_PROPTYPE_8021TLV:
	case LLDP_PROPTYPE_8023TLV:
	case LLDP_PROPTYPE_VIRTTLV:
		lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
		lap = lldp_agent_get(linkid, &err);
		lldp_rw_unlock(&lldp_agents_list_rwlock);
		if (err != 0)
			break;
		lldp_mutex_lock(&lap->la_db_mutex);
		err = i_lldpd_set_tlv(lap, ptype, lpval.lpv_u32, &mask, flags);
		if (err == 0) {
			/*
			 * Check if `mask' got properly set. If the final `mask'
			 * is 0 then we either reseted the value or we
			 * explicitly assigned 0.
			 */
			if (mask == 0 && !((flags & LLDP_OPT_DEFAULT) != 0 ||
			    ((flags & LLDP_OPT_ACTIVE) != 0 &&
			    lpval.lpv_u32 == 0))) {
				err = ENODATA;
			}
			if (err == 0) {
				err = lldpd_persist_prop(LLDP_PROPCLASS_AGENT,
				    ptype, lap->la_linkname, &mask,
				    DATA_TYPE_UINT32, flags);
			}
		}
		lldp_mutex_unlock(&lap->la_db_mutex);
		lldp_agent_refcnt_decr(lap);
		break;
	default:
		err = EINVAL;
		break;
	}
	return (err);
}

/* Set LLDP properties */
static void
lldpd_set_prop(void *argp)
{
	lldpd_door_lprops_t	*props = argp;
	lldpd_sprops_retval_t	rval;
	lldp_propclass_t	pclass = props->lp_pclass;
	lldp_proptype_t		ptype = props->lp_ptype;
	lldp_pval_t		pval = props->lp_pval;
	uint32_t		flags = props->lp_flags;
	datalink_id_t		linkid;
	int			err;

	if ((pclass & (LLDP_PROPCLASS_AGENT|LLDP_PROPCLASS_AGENT_TLVS)) != 0) {
		if (!lldpd_validate_link(dld_handle, props->lp_laname,
		    &linkid, &err)) {
			goto ret;
		}
	}

	if (pclass & LLDP_PROPCLASS_AGENT) {
		err = i_lldpd_set_agent_prop(linkid, ptype, pval, flags);
	} else if (pclass & LLDP_PROPCLASS_GLOBAL_TLVS) {
		err = i_lldpd_set_global_tlvprop(pclass, ptype, pval, flags);
	} else if (pclass & LLDP_PROPCLASS_AGENT_TLVS) {
		err = i_lldpd_set_agent_tlvprop(linkid, pclass, ptype, pval,
		    flags);
	} else {
		err = ENOTSUP;
	}
ret:
	rval.lr_err = err;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

static int
i_lldpd_get_tlv(datalink_id_t linkid, lldp_proptype_t ptype,
    char *pval, uint_t psize)
{
	lldp_agent_t	*lap;
	uint32_t	oui;
	lldp_write2pdu_t *wpdu;

	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
	/* Retrieve lldp_agent_t for the given data-link, if any. */
	lap = lldp_agent_get(linkid, NULL);
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	if (lap == NULL) {
		(void) strlcat(pval, LLDP_NONESTR, psize);
		return (0);
	}
	switch (ptype) {
	case LLDP_PROPTYPE_BASICTLV:
		oui  = 0;
		break;
	case LLDP_PROPTYPE_8021TLV:
		oui = LLDP_802dot1_OUI;
		break;
	case LLDP_PROPTYPE_8023TLV:
		oui = LLDP_802dot3_OUI;
		break;
	case LLDP_PROPTYPE_VIRTTLV:
		oui = LLDP_ORACLE_OUI;
		break;
	}
	/*
	 * Walk through the list of write callback functions and capture
	 * all the TLVs being transmitted for the give oui.
	 */
	lldp_rw_lock(&lap->la_txmib_rwlock, LLDP_RWLOCK_READER);
	for (wpdu = list_head(&lap->la_write2pdu); wpdu != NULL;
	    wpdu = list_next(&lap->la_write2pdu, wpdu)) {
		lldp_tlv_info_t	*infop = wpdu->ltp_infop;

		if (infop != NULL && infop->lti_name != NULL &&
		    infop->lti_oui == oui) {
			if (pval[0] != '\0')
				(void) strlcat(pval, ",", psize);
			(void) strlcat(pval, infop->lti_name, psize);
		}
	}
	lldp_rw_unlock(&lap->la_txmib_rwlock);
	if (pval[0] == '\0')
		(void) strlcat(pval, LLDP_NONESTR, psize);

ret:
	if (lap != NULL)
		lldp_agent_refcnt_decr(lap);
	return (0);
}

static int
i_lldpd_get_global_tlvprop(lldp_propclass_t pclass, lldp_proptype_t ptype,
    char *buf, uint_t bufsize)
{
	uint32_t	pval = 0;
	int		err = 0, nmaddr, i;
	lldp_syscapab_t	sc;
	lldp_mgmtaddr_t	*maddr, *maddrp;
	char		addrstr[LLDP_STRSIZE];
	uint16_t	capab;

	switch (ptype) {
	case LLDP_PROPTYPE_SUP_SYSCAPAB:
	case LLDP_PROPTYPE_ENAB_SYSCAPAB:
		assert(pclass == LLDP_PROPCLASS_SYSCAPAB_TLV);
		bzero(&sc, sizeof (sc));
		lldp_rw_lock(&lldp_sysinfo_rwlock, LLDP_RWLOCK_READER);
		err = lldp_nvlist2syscapab(lldp_sysinfo, &sc);
		lldp_rw_unlock(&lldp_sysinfo_rwlock);
		if (err != 0 && err != ENOENT)
			break;
		capab = (ptype == LLDP_PROPTYPE_SUP_SYSCAPAB ?
		    sc.ls_sup_syscapab : sc.ls_enab_syscapab);
		lldp_syscapab2str(capab, buf, bufsize);
		return (0);
	case LLDP_PROPTYPE_IPADDR:
		assert(pclass == LLDP_PROPCLASS_MGMTADDR_TLV);
		lldp_rw_lock(&lldp_sysinfo_rwlock, LLDP_RWLOCK_READER);
		err = lldp_nvlist2mgmtaddr(lldp_sysinfo, NULL,
		    &maddr, &nmaddr);
		lldp_rw_unlock(&lldp_sysinfo_rwlock);
		if (err != 0 && err != ENOENT) {
			break;
		} else if (err == ENOENT) {
			buf[0] = '\0';
			return (0);
		}
		maddrp = maddr;
		buf[0] = '\0';
		for (i = 0; i < nmaddr; i++, maddrp++) {
			lldp_mgmtaddr2str(maddrp, addrstr, sizeof (addrstr));
			if (i > 0)
				(void) strlcat(buf, ",", bufsize);
			(void) strlcat(buf, addrstr, bufsize);
		}
		free(maddr);
		return (0);
	default:
		err = ENOTSUP;
		break;
	}
	if (err == 0)
		(void) snprintf(buf, bufsize, "%u", pval);
	return (err);
}

static int
i_lldpd_get_agent_tlvprop(datalink_id_t linkid, lldp_propclass_t pclass,
    lldp_proptype_t ptype, char *buf, uint_t bufsize)
{
	lldp_agent_t	*lap;
	int		err;

	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
	/* Retrieve lldp_agent_t for the given data-link, if any. */
	lap = lldp_agent_get(linkid, NULL);
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	if (lap == NULL) {
		(void) snprintf(buf, bufsize, "--");
		return (0);
	}
	switch (pclass) {
	case LLDP_PROPCLASS_PFC_TLV:
	case LLDP_PROPCLASS_APPLN_TLV:
		err = dcbx_get_prop(lap, pclass, ptype, buf, bufsize);
		break;
	default:
		err = ENOTSUP;
		break;
	}
	lldp_agent_refcnt_decr(lap);
	return (err);
}

static int
i_lldpd_get_agent_prop(datalink_id_t linkid, lldp_proptype_t ptype,
    char *buf, uint_t bufsize)
{
	lldp_agent_t	*lap;
	int		err = 0;

	switch (ptype) {
	case LLDP_PROPTYPE_MODE:
		lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_READER);
		/* Retrieve lldp_agent_t for the given data-link, if any. */
		lap = lldp_agent_get(linkid, NULL);
		lldp_rw_unlock(&lldp_agents_list_rwlock);
		if (lap == NULL) {
			(void) strlcat(buf, lldp_mode2str(LLDP_MODE_DISABLE),
			    bufsize);
		} else {
			(void) strlcat(buf, lldp_mode2str(lap->la_adminStatus),
			    bufsize);
			lldp_agent_refcnt_decr(lap);
		}
		break;
	case LLDP_PROPTYPE_BASICTLV:
	case LLDP_PROPTYPE_8021TLV:
	case LLDP_PROPTYPE_8023TLV:
	case LLDP_PROPTYPE_VIRTTLV:
		err = i_lldpd_get_tlv(linkid, ptype, buf, bufsize);
		break;
	default:
		err = EINVAL;
		break;
	}
	return (err);
}

/* Get LLDP properties */
static void
lldpd_get_prop(void *argp)
{
	lldpd_door_lprops_t	*props = argp;
	lldpd_gprops_retval_t	rval;
	lldp_propclass_t	pclass = props->lp_pclass;
	lldp_proptype_t		ptype = props->lp_ptype;
	char			*buf = rval.lpr_pval;
	uint_t			bufsize = sizeof (rval.lpr_pval);
	datalink_id_t		linkid;
	int			err = 0;

	bzero(&rval, sizeof (rval));
	if ((pclass & (LLDP_PROPCLASS_AGENT|LLDP_PROPCLASS_AGENT_TLVS)) != 0) {
		if (!lldpd_validate_link(dld_handle, props->lp_laname,
		    &linkid, &err)) {
			goto ret;
		}
	}
	if (pclass & LLDP_PROPCLASS_AGENT) {
		err = i_lldpd_get_agent_prop(linkid, ptype, buf, bufsize);
	} else if (pclass & LLDP_PROPCLASS_GLOBAL_TLVS) {
		err = i_lldpd_get_global_tlvprop(pclass, ptype, buf, bufsize);
	} else if (pclass & LLDP_PROPCLASS_AGENT_TLVS) {
		err = i_lldpd_get_agent_tlvprop(linkid, pclass, ptype, buf,
		    bufsize);
	} else {
		err = ENOTSUP;
	}
ret:
	rval.lpr_err = err;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}

int
lldp_add_vlan_info(dladm_handle_t dh, datalink_id_t vlan_linkid, void *arg)
{
	lldp_agent_t		*lap = arg;
	lldp_vlan_info_t	lvi;
	dladm_vlan_attr_t	va;
	nvlist_t		*nvl = NULL;
	int			err;

	if (dladm_vlan_info(dh, vlan_linkid, &va, DLADM_OPT_ACTIVE) !=
	    DLADM_STATUS_OK) {
		return (-1);
	}
	if (va.dv_linkid != lap->la_linkid)
		return (0);
	/* caller has the lock for local mib */
	err = nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if (err != 0)
		return (err);
	bzero(&lvi, sizeof (lvi));
	/* Get the vlan name */
	if (dladm_datalink_id2info(dh, vlan_linkid, NULL, NULL, NULL,
	    lvi.lvi_name, sizeof (lvi.lvi_name)) != DLADM_STATUS_OK) {
		return (-1);
	}
	lvi.lvi_vlen = strlen(lvi.lvi_name);
	lvi.lvi_vid = va.dv_vid;
	return (lldp_add_vlan2nvlist(&lvi, nvl));
}

/* caller has the lock for la_local_mib */
static int
lldpd_update_vlans(lldp_agent_t *lap, lldp_vinfo_t *vinfo)
{
	lldp_vlan_info_t	*lvi = NULL, *lvip = NULL;
	int			i, err, cnt = 0;
	nvlist_t		*nvl = NULL, *vnvl = NULL;

	err = nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if (err != 0)
		return (err);
	err = lldp_nvlist2vlan(nvl, &lvi, &cnt);
	if (err != 0 && err != ENOENT)
		return (err);
	err = 0;
	lvip = lvi;
	for (i = 0; i < cnt; i++, lvip++) {
		if (lvip->lvi_vid == vinfo->lvi_vid)
			break;
	}
	switch (vinfo->lvi_operation) {
	case LLDP_ADD_OPERATION:
		if (i == cnt) {
			err = lldp_add_vlan_info(dld_handle,
			    vinfo->lvi_vlinkid, lap);
		} else {
			err = EEXIST;
		}
		break;
	case LLDP_DELETE_OPERATION:
		if (i != cnt) {
			char nvpname[LLDP_STRSIZE];

			if ((err = lldp_get_nested_nvl(nvl,
			    LLDP_NVP_ORGANIZATION, LLDP_8021_OUI_LIST,
			    LLDP_NVP_VLANNAME, &vnvl)) == 0) {
				(void) snprintf(nvpname, sizeof (nvpname),
				    "%s_%d", lvip->lvi_name, lvip->lvi_vid);
				err = nvlist_remove(vnvl, nvpname,
				    DATA_TYPE_STRING);
			}
		} else {
			err = ENOENT;
		}
		break;
	}
	free(lvi);
	return (err);
}

int
lldp_add_vnic_info(dladm_handle_t dh, datalink_id_t vnic_linkid, void *arg)
{
	lldp_agent_t		*lap = arg;
	lldp_vnic_info_t	lvi;
	lldp_portid_t		*pid;
	dladm_vnic_attr_t	va;
	nvlist_t		*nvl = NULL;
	int			err;

	if (dladm_vnic_info(dh, vnic_linkid, &va, DLADM_OPT_ACTIVE) !=
	    DLADM_STATUS_OK) {
		return (-1);
	}
	if (va.va_link_id != lap->la_linkid)
		return (0);
	/* caller has the lock for local mib */
	err = nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if (err != 0)
		return (err);
	bzero(&lvi, sizeof (lvi));
	/* Get the vnic name */
	if (dladm_datalink_id2info(dh, vnic_linkid, NULL, NULL, NULL,
	    lvi.lvni_name, sizeof (lvi.lvni_name)) != DLADM_STATUS_OK) {
		return (-1);
	}
	lvi.lvni_vid = va.va_vid;
	lvi.lvni_linkid = vnic_linkid;
	pid = &lvi.lvni_portid;
	bcopy(va.va_mac_addr, pid->lp_pid, va.va_mac_len);
	pid->lp_pidlen = va.va_mac_len;
	pid->lp_subtype = LLDP_PORT_ID_MACADDRESS;

	return (lldp_add_vnic2nvlist(&lvi, nvl));
}

/* caller has the lock for la_local_mib */
static int
lldpd_update_vnics(lldp_agent_t *lap, lldp_vinfo_t *vinfo)
{
	lldp_vnic_info_t	*lvi = NULL, *lvip = NULL;
	int			i, err, cnt = 0;
	nvlist_t		*nvl = NULL, *vnvl = NULL;

	err = nvlist_lookup_nvlist(lap->la_local_mib, lap->la_msap, &nvl);
	if (err != 0)
		return (err);
	err = lldp_nvlist2vnic(nvl, &lvi, &cnt);
	if (err != 0 && err != ENOENT)
		return (err);
	err = 0;
	lvip = lvi;
	for (i = 0; i < cnt; i++, lvip++) {
		if (lvip->lvni_linkid == vinfo->lvi_vlinkid)
			break;
	}
	switch (vinfo->lvi_operation) {
	case LLDP_ADD_OPERATION:
		if (i == cnt) {
			(void) lldp_add_vnic_info(dld_handle,
			    vinfo->lvi_vlinkid, lap);
		} else {
			err = EEXIST;
		}
		break;
	case LLDP_DELETE_OPERATION:
		if (i != cnt) {
			lldp_portid_t	*pid;
			char		nvpname[LLDP_STRSIZE];

			if ((err = lldp_get_nested_nvl(nvl,
			    LLDP_NVP_ORGANIZATION, LLDP_ORACLE_OUI_LIST,
			    LLDP_NVP_VNICNAME, &vnvl)) == 0) {
				pid = &lvip->lvni_portid;
				(void) lldp_bytearr2hexstr(pid->lp_pid,
				    pid->lp_pidlen, nvpname, sizeof (nvpname));
				err = nvlist_remove(vnvl, nvpname,
				    DATA_TYPE_NVLIST);
			}
		} else {
			err = ENOENT;
		}
		break;
	}
	free(lvi);
	return (err);
}

/*
 * Update VLAN/VNIC information. If we successfully modified and
 * we are advertising this information then we need to do an immediate
 * transmission of this information via lldp_something_changed_local()
 */
static void
lldpd_update_vlinks(void *argp)
{
	lldp_vinfo_t	*vinfo = argp;
	lldpd_retval_t	rval;
	datalink_id_t	linkid = vinfo->lvi_plinkid;
	lldp_agent_t	*lap;
	boolean_t	vlink_updated = B_FALSE;

	lldp_rw_lock(&lldp_agents_list_rwlock, LLDP_RWLOCK_WRITER);
	lap = lldp_agent_get(linkid, NULL);
	lldp_rw_unlock(&lldp_agents_list_rwlock);
	if (lap == NULL)
		goto ret;

	/*
	 * We only update the VLAN/VNIC information only if we have enabled
	 * advertisements of VLAN/VNIC TLVs for a given LLDP agent.
	 */
	lldp_rw_lock(&lap->la_txmib_rwlock, LLDP_RWLOCK_WRITER);
	if (!vinfo->lvi_isvnic && i_lldp_get_write2pdu_nolock(lap,
	    LLDP_8021_VLAN_NAME_TLVNAME) != NULL) {
		if (lldpd_update_vlans(lap, vinfo) == 0)
			vlink_updated = B_TRUE;
	} else if (vinfo->lvi_isvnic && i_lldp_get_write2pdu_nolock(lap,
	    LLDP_VIRT_VNIC_TLVNAME) != NULL) {
		if (lldpd_update_vnics(lap, vinfo) == 0)
			vlink_updated = B_TRUE;
	}
	lldp_rw_unlock(&lap->la_txmib_rwlock);

	if (vlink_updated)
		lldp_something_changed_local(lap);
	lldp_agent_refcnt_decr(lap);
ret:
	rval.lr_err = 0;
	(void) door_return((char *)&rval, sizeof (rval), NULL, 0);
}
