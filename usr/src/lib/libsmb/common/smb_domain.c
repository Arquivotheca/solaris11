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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/list.h>
#include <sys/varargs.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <synch.h>
#include <pwd.h>
#include <grp.h>
#include <assert.h>
#include <syslog.h>

#include <smbsrv/smbinfo.h>
#include <smbsrv/string.h>
#include <smb/smb_sid.h>
#include <smbsrv/libsmb.h>

#define	SMB_DCACHE_UPDATE_WAIT	45	/* seconds */

#define	SMB_DCACHE_BUFSIZE	256

/*
 * Domain cache states
 */
#define	SMB_DCACHE_STATE_NONE		0
#define	SMB_DCACHE_STATE_READY		1
#define	SMB_DCACHE_STATE_UPDATING	2
#define	SMB_DCACHE_STATE_DESTROYING	3

/*
 * Cache lock modes
 */
#define	SMB_DCACHE_RDLOCK	0
#define	SMB_DCACHE_WRLOCK	1

typedef struct smb_domain_cache {
	list_t		dc_cache;
	rwlock_t	dc_cache_lck;
	mutex_t		dc_mtx;
	cond_t		dc_cv;
	uint32_t	dc_state;
	uint32_t	dc_nops;
} smb_domain_cache_t;

static smb_domain_cache_t smb_dcache;

static uint32_t smb_domain_add(smb_domain_type_t, smb_domain_t *);
static uint32_t smb_domain_add_local(void);
static uint32_t smb_domain_add_primary(uint32_t);

static void smb_dcache_create(void);
static void smb_dcache_destroy(void);
static uint32_t smb_dcache_lock(int);
static void smb_dcache_unlock(void);
static void smb_dcache_remove(smb_domain_t *);
static uint32_t smb_dcache_add(smb_domain_t *);
static boolean_t smb_dcache_wait(void);
static uint32_t smb_dcache_updating(void);
static void smb_dcache_ready(void);
static void smb_dcache_error(uint32_t, const char *, ...);

/*
 * domain cache one time initialization. This function should
 * only be called during service startup.
 *
 * Returns 0 on success and an error code on failure.
 */
void
smb_domain_init(void)
{
	smb_domain_t di;
	uint32_t secmode;
	uint32_t rc;

	smb_dcache_create();
	secmode = smb_config_get_secmode();

	if ((rc = smb_domain_add_local()) != 0) {
		smb_dcache_error(rc, "unable to add local domain");
		return;
	}

	smb_domain_set_basic_info(NT_BUILTIN_DOMAIN_SIDSTR, "BUILTIN", "", &di);
	rc = smb_domain_add(SMB_DOMAIN_BUILTIN, &di);
	if (rc != SMB_DOMAIN_SUCCESS) {
		smb_dcache_error(rc, "unable to add BUILTIN domain");
		return;
	}

	rc = smb_domain_add_primary(secmode);
	if (rc == SMB_DOMAIN_NOMACHINE_SID) {
		smb_dcache_error(rc, "check idmap configuration");
		return;
	}
}

/*
 * Destroys the cache upon service termination
 */
void
smb_domain_fini(void)
{
	smb_dcache_destroy();
}

/*
 * Add a domain structure to domain cache. There is no checking
 * for duplicates.
 */
static uint32_t
smb_domain_add(smb_domain_type_t type, smb_domain_t *di)
{
	uint32_t res;

	if ((di == NULL) || (di->di_sid == NULL))
		return (SMB_DOMAIN_INVALID_ARG);

	if ((res = smb_dcache_lock(SMB_DCACHE_WRLOCK)) == SMB_DOMAIN_SUCCESS) {
		di->di_type = type;
		res = smb_dcache_add(di);
		smb_dcache_unlock();
	}

	return (res);
}

/*
 * Lookup a domain by its name. The passed name is the NETBIOS or fully
 * qualified DNS name or non-qualified DNS name.
 *
 * If the requested domain is found and given 'di' pointer is not NULL
 * it'll be filled with the domain information and B_TRUE is returned.
 * If the caller only needs to check a domain existence it can pass
 * NULL for 'di' and just check the return value.
 *
 * If the domain is not in the cache B_FALSE is returned.
 */
boolean_t
smb_domain_lookup_name(char *name, smb_domain_t *di)
{
	boolean_t found = B_FALSE;
	smb_domain_t *dcnode;
	char *p;

	if (name == NULL || *name == '\0')
		return (B_FALSE);

	if (smb_dcache_lock(SMB_DCACHE_RDLOCK) != SMB_DOMAIN_SUCCESS)
		return (B_FALSE);

	dcnode = list_head(&smb_dcache.dc_cache);
	while (dcnode) {
		found = (smb_strcasecmp(dcnode->di_nbname, name, 0) == 0) ||
		    (smb_strcasecmp(dcnode->di_fqname, name, 0) == 0);

		if (found) {
			if (di)
				*di = *dcnode;
			break;
		}

		if ((p = strchr(dcnode->di_fqname, '.')) != NULL) {
			*p = '\0';
			found = (smb_strcasecmp(dcnode->di_fqname, name,
			    0) == 0);
			*p = '.';
			if (found) {
				if (di)
					*di = *dcnode;
				break;
			}
		}

		dcnode = list_next(&smb_dcache.dc_cache, dcnode);
	}

	smb_dcache_unlock();
	return (found);
}

/*
 * Lookup a domain by its SID.
 *
 * If the requested domain is found and given 'di' pointer is not NULL
 * it'll be filled with the domain information and B_TRUE is returned.
 * If the caller only needs to check a domain existence it can pass
 * NULL for 'di' and just check the return value.
 *
 * If the domain is not in the cache B_FALSE is returned.
 */
boolean_t
smb_domain_lookup_sid(smb_sid_t *sid, smb_domain_t *di)
{
	boolean_t found = B_FALSE;
	smb_domain_t *dcnode;
	char sidstr[SMB_SID_STRSZ];

	if (sid == NULL)
		return (B_FALSE);

	smb_sid_tostr(sid, sidstr);

	if (smb_dcache_lock(SMB_DCACHE_RDLOCK) != SMB_DOMAIN_SUCCESS)
		return (B_FALSE);

	dcnode = list_head(&smb_dcache.dc_cache);
	while (dcnode) {
		found = (strcmp(dcnode->di_sid, sidstr) == 0);
		if (found) {
			if (di)
				*di = *dcnode;
			break;
		}

		dcnode = list_next(&smb_dcache.dc_cache, dcnode);
	}

	smb_dcache_unlock();
	return (found);
}

/*
 * Lookup a domain by its type.
 *
 * If the requested domain is found and given 'di' pointer is not NULL
 * it'll be filled with the domain information and B_TRUE is returned.
 * If the caller only needs to check a domain existence it can pass
 * NULL for 'di' and just check the return value.
 *
 * If the domain is not in the cache B_FALSE is returned.
 */
boolean_t
smb_domain_lookup_type(smb_domain_type_t type, smb_domain_t *di)
{
	boolean_t found = B_FALSE;
	smb_domain_t *dcnode;

	if (smb_dcache_lock(SMB_DCACHE_RDLOCK) != SMB_DOMAIN_SUCCESS)
		return (B_FALSE);

	dcnode = list_head(&smb_dcache.dc_cache);
	while (dcnode) {
		if (dcnode->di_type == type) {
			found = B_TRUE;
			if (di)
				*di = *dcnode;
			break;
		}

		dcnode = list_next(&smb_dcache.dc_cache, dcnode);
	}

	smb_dcache_unlock();
	return (found);
}

/*
 * Returns primary domain information plus the name of
 * the selected domain controller.
 */
boolean_t
smb_domain_getinfo(smb_domainex_t *dxi)
{
	boolean_t success;

	success = smb_domain_lookup_type(SMB_DOMAIN_PRIMARY, &dxi->d_primary);
	if (success)
		smb_config_getdc(dxi->d_dc, sizeof (dxi->d_dc));

	return (success);
}

/*
 * Transfer the cache to updating state.
 * In this state any request for reading the cache would
 * be blocked until the update is finished.
 */
uint32_t
smb_domain_start_update(void)
{
	return (smb_dcache_updating());
}

/*
 * Transfer the cache from updating to ready state.
 */
void
smb_domain_end_update(void)
{
	smb_dcache_ready();
}

/*
 * Updates the cache with given information for the primary
 * domain, possible trusted domains and the selected domain
 * controller.
 *
 * Before adding the new entries existing entries of type
 * primary and trusted will be removed from cache.
 */
void
smb_domain_update(smb_domainex_t *dxi)
{
	smb_domain_t *dcnode;
	int i;

	if (smb_dcache_lock(SMB_DCACHE_WRLOCK) != SMB_DOMAIN_SUCCESS)
		return;

	dcnode = list_head(&smb_dcache.dc_cache);
	while (dcnode) {
		if ((dcnode->di_type == SMB_DOMAIN_PRIMARY) ||
		    (dcnode->di_type == SMB_DOMAIN_TRUSTED)) {
			smb_dcache_remove(dcnode);
			dcnode = list_head(&smb_dcache.dc_cache);
		} else {
			dcnode = list_next(&smb_dcache.dc_cache, dcnode);
		}
	}

	if (smb_dcache_add(&dxi->d_primary) == SMB_DOMAIN_SUCCESS) {
		for (i = 0; i < dxi->d_trusted.td_num; i++)
			(void) smb_dcache_add(&dxi->d_trusted.td_domains[i]);

		smb_config_setdc(dxi->d_dc);
	}

	smb_dcache_unlock();
}

void
smb_domain_set_basic_info(char *sid, char *nb_domain, char *fq_domain,
    smb_domain_t *di)
{
	if (sid == NULL || nb_domain == NULL || fq_domain == NULL ||
	    di == NULL)
		return;

	(void) strlcpy(di->di_sid, sid, SMB_SID_STRSZ);
	(void) strlcpy(di->di_nbname, nb_domain, NETBIOS_NAME_SZ);
	(void) smb_strupr(di->di_nbname);
	(void) strlcpy(di->di_fqname, fq_domain, MAXHOSTNAMELEN);
	di->di_binsid = NULL;
}

void
smb_domain_set_dns_info(char *sid, char *nb_domain, char *fq_domain,
    char *forest, char *guid, smb_domain_t *di)
{
	if (di == NULL || forest == NULL || guid == NULL)
		return;

	smb_domain_set_basic_info(sid, nb_domain, fq_domain, di);
	(void) strlcpy(di->di_u.di_dns.ddi_forest, forest, MAXHOSTNAMELEN);
	(void) strlcpy(di->di_u.di_dns.ddi_guid, guid,
	    UUID_PRINTABLE_STRING_LENGTH);
}

void
smb_domain_set_trust_info(char *sid, char *nb_domain, char *fq_domain,
    uint32_t trust_dir, uint32_t trust_type, uint32_t trust_attrs,
    smb_domain_t *di)
{
	smb_domain_trust_t *ti;

	if (di == NULL)
		return;

	di->di_type = SMB_DOMAIN_TRUSTED;
	ti = &di->di_u.di_trust;
	smb_domain_set_basic_info(sid, nb_domain, fq_domain, di);
	ti->dti_trust_direction = trust_dir;
	ti->dti_trust_type = trust_type;
	ti->dti_trust_attrs = trust_attrs;
}

/*
 * Return a list of domains (local, primary, and trusted/untrusted)
 * from the domain cache.  Also return the SMB_CI_DC_SELECTED property
 * if the primary domain exists in the cache.
 */
int
smb_domains_getinfo(smb_domains_info_t *domains_info)
{
	smb_domain_t		*domain;
	smb_domain_info_t	*item;
	list_t			*domain_list;
	boolean_t		found_primary = B_FALSE;

	domain_list = &domains_info->d_domain_list;

	list_create(domain_list, sizeof (smb_domain_info_t),
	    offsetof(smb_domain_info_t, i_lnd));

	if (smb_dcache_lock(SMB_DCACHE_RDLOCK) != SMB_DOMAIN_SUCCESS)
		return (NT_STATUS_INTERNAL_ERROR);

	domain = list_head(&smb_dcache.dc_cache);
	while (domain) {
		switch (domain->di_type) {
		case SMB_DOMAIN_PRIMARY:

			found_primary = B_TRUE;
			/* LINTED: E_CASE_FALLTHRU */
		case SMB_DOMAIN_TRUSTED:
		case SMB_DOMAIN_UNTRUSTED:
		case SMB_DOMAIN_LOCAL:

			item = malloc(sizeof (smb_domain_info_t));
			if (item == NULL) {
				smb_dcache_unlock();
				return (NT_STATUS_NO_MEMORY);
			}

			item->i_type =  domain->di_type;
			(void) strlcpy(item->i_nbname, domain->di_nbname,
			    sizeof (item->i_nbname));
			(void) strlcpy(item->i_sid, domain->di_sid,
			    sizeof (item->i_sid));

			list_insert_tail(domain_list, item);
			break;

		default:
			break;
		}

		domain = list_next(&smb_dcache.dc_cache, domain);
	}

	smb_dcache_unlock();

	if (found_primary)
		smb_config_getdc(domains_info->d_dc_name,
		    sizeof (domains_info->d_dc_name));

	return (NT_STATUS_SUCCESS);
}

/*
 *  Free the memory allocated for the domain list and destroy the list.
 */
void
smb_domains_info_free(list_t *domain_list)
{
	smb_domain_info_t	*item;

	while ((item = list_head(domain_list)) != NULL) {
		list_remove(domain_list, item);
		free(item);
	}

	list_destroy(domain_list);
}

/*
 * Add an entry for the local domain to the domain cache
 */
static uint32_t
smb_domain_add_local(void)
{
	char *lsidstr;
	char hostname[NETBIOS_NAME_SZ];
	char ad_domain[MAXHOSTNAMELEN];
	smb_domain_t di;

	if ((lsidstr = smb_config_get_localsid()) == NULL)
		return (SMB_DOMAIN_NOMACHINE_SID);

	if (smb_getnetbiosname(hostname, NETBIOS_NAME_SZ) != 0) {
		free(lsidstr);
		return (SMB_DOMAIN_NOMACHINE_SID);
	}

	*ad_domain = '\0';
	(void) smb_getfqhostname(ad_domain, MAXHOSTNAMELEN);
	smb_domain_set_basic_info(lsidstr, hostname, ad_domain, &di);
	(void) smb_domain_add(SMB_DOMAIN_LOCAL, &di);

	free(lsidstr);
	return (SMB_DOMAIN_SUCCESS);
}

/*
 * Add an entry for the primary domain to the domain cache
 */
static uint32_t
smb_domain_add_primary(uint32_t secmode)
{
	char sidstr[SMB_SID_STRSZ];
	char ad_domain[MAXHOSTNAMELEN];
	char nb_domain[NETBIOS_NAME_SZ];
	smb_domain_t di;
	int rc;

	if (secmode != SMB_SECMODE_DOMAIN)
		return (SMB_DOMAIN_SUCCESS);

	rc = smb_config_getstr(SMB_CI_DOMAIN_SID, sidstr, sizeof (sidstr));
	if (rc != SMBD_SMF_OK)
		return (SMB_DOMAIN_NODOMAIN_SID);

	if (smb_getdomainname_nb(nb_domain, NETBIOS_NAME_SZ) == -1)
		return (SMB_DOMAIN_NODOMAIN_NAME);

	(void) smb_getdomainname_ad(ad_domain, MAXHOSTNAMELEN);
	smb_domain_set_basic_info(sidstr, nb_domain, ad_domain, &di);
	(void) smb_domain_add(SMB_DOMAIN_PRIMARY, &di);
	return (SMB_DOMAIN_SUCCESS);
}

/*
 * Initialize the domain cache.
 * This function does not populate the cache.
 */
static void
smb_dcache_create(void)
{
	(void) mutex_lock(&smb_dcache.dc_mtx);
	if (smb_dcache.dc_state != SMB_DCACHE_STATE_NONE) {
		(void) mutex_unlock(&smb_dcache.dc_mtx);
		return;
	}

	list_create(&smb_dcache.dc_cache, sizeof (smb_domain_t),
	    offsetof(smb_domain_t, di_lnd));

	smb_dcache.dc_nops = 0;
	smb_dcache.dc_state = SMB_DCACHE_STATE_READY;
	(void) mutex_unlock(&smb_dcache.dc_mtx);
}

/*
 * Removes and frees all the cache entries
 */
static void
smb_dcache_flush(void)
{
	smb_domain_t *di;

	(void) rw_wrlock(&smb_dcache.dc_cache_lck);
	while ((di = list_head(&smb_dcache.dc_cache)) != NULL)
		smb_dcache_remove(di);
	(void) rw_unlock(&smb_dcache.dc_cache_lck);
}

/*
 * Destroys the cache.
 */
static void
smb_dcache_destroy(void)
{
	(void) mutex_lock(&smb_dcache.dc_mtx);
	if ((smb_dcache.dc_state == SMB_DCACHE_STATE_READY) ||
	    (smb_dcache.dc_state == SMB_DCACHE_STATE_UPDATING)) {
		smb_dcache.dc_state = SMB_DCACHE_STATE_DESTROYING;
		while (smb_dcache.dc_nops > 0)
			(void) cond_wait(&smb_dcache.dc_cv,
			    &smb_dcache.dc_mtx);

		smb_dcache_flush();
		list_destroy(&smb_dcache.dc_cache);

		smb_dcache.dc_state = SMB_DCACHE_STATE_NONE;
	}
	(void) mutex_unlock(&smb_dcache.dc_mtx);
}

/*
 * Lock the cache with the specified mode.
 * If the cache is in updating state and a read lock is
 * requested, the lock won't be granted until either the
 * update is finished or SMB_DCACHE_UPDATE_WAIT has passed.
 *
 * Whenever a lock is granted, the number of inflight cache
 * operations is incremented.
 */
static uint32_t
smb_dcache_lock(int mode)
{
	(void) mutex_lock(&smb_dcache.dc_mtx);
	switch (smb_dcache.dc_state) {
	case SMB_DCACHE_STATE_NONE:
	case SMB_DCACHE_STATE_DESTROYING:
		(void) mutex_unlock(&smb_dcache.dc_mtx);
		return (SMB_DOMAIN_INTERNAL_ERR);

	case SMB_DCACHE_STATE_UPDATING:
		if (mode == SMB_DCACHE_RDLOCK) {
			/*
			 * Read operations should wait until the update
			 * is completed.
			 */
			if (!smb_dcache_wait()) {
				(void) mutex_unlock(&smb_dcache.dc_mtx);
				return (SMB_DOMAIN_INTERNAL_ERR);
			}
		}

	default:
		smb_dcache.dc_nops++;
		break;
	}
	(void) mutex_unlock(&smb_dcache.dc_mtx);

	/*
	 * Lock has to be taken outside the mutex otherwise
	 * there could be a deadlock
	 */
	if (mode == SMB_DCACHE_RDLOCK)
		(void) rw_rdlock(&smb_dcache.dc_cache_lck);
	else
		(void) rw_wrlock(&smb_dcache.dc_cache_lck);

	return (SMB_DOMAIN_SUCCESS);
}

/*
 * Decrement the number of inflight operations and then unlock.
 */
static void
smb_dcache_unlock(void)
{
	(void) mutex_lock(&smb_dcache.dc_mtx);
	assert(smb_dcache.dc_nops > 0);
	smb_dcache.dc_nops--;
	(void) cond_broadcast(&smb_dcache.dc_cv);
	(void) mutex_unlock(&smb_dcache.dc_mtx);

	(void) rw_unlock(&smb_dcache.dc_cache_lck);
}

static uint32_t
smb_dcache_add(smb_domain_t *di)
{
	smb_domain_t *dcnode;

	if ((dcnode = malloc(sizeof (smb_domain_t))) == NULL)
		return (SMB_DOMAIN_NO_MEMORY);

	*dcnode = *di;
	dcnode->di_binsid = smb_sid_fromstr(dcnode->di_sid);
	if (dcnode->di_binsid == NULL) {
		free(dcnode);
		return (SMB_DOMAIN_NO_MEMORY);
	}

	list_insert_tail(&smb_dcache.dc_cache, dcnode);
	return (SMB_DOMAIN_SUCCESS);
}

static void
smb_dcache_remove(smb_domain_t *di)
{
	list_remove(&smb_dcache.dc_cache, di);
	smb_sid_free(di->di_binsid);
	free(di);
}

/*
 * Waits for SMB_DCACHE_UPDATE_WAIT seconds if cache is in
 * UPDATING state. Upon wake up returns true if cache is
 * ready to be used, otherwise it returns false.
 */
static boolean_t
smb_dcache_wait(void)
{
	timestruc_t to;
	int err;

	to.tv_sec = SMB_DCACHE_UPDATE_WAIT;
	to.tv_nsec = 0;
	while (smb_dcache.dc_state == SMB_DCACHE_STATE_UPDATING) {
		err = cond_reltimedwait(&smb_dcache.dc_cv,
		    &smb_dcache.dc_mtx, &to);
		if (err == ETIME)
			break;
	}

	return (smb_dcache.dc_state == SMB_DCACHE_STATE_READY);
}

/*
 * Transfers the cache into UPDATING state, this will ensure
 * any read access to the cache will be stalled until the
 * update is finished. This is to avoid providing incomplete,
 * inconsistent or stale information.
 *
 * If another thread is already updating the cache, other
 * callers will wait until cache is no longer in UPDATING
 * state. The return code is decided based on the new
 * state of the cache.
 */
static uint32_t
smb_dcache_updating(void)
{
	uint32_t rc;

	(void) mutex_lock(&smb_dcache.dc_mtx);
	switch (smb_dcache.dc_state) {
	case SMB_DCACHE_STATE_READY:
		smb_dcache.dc_state = SMB_DCACHE_STATE_UPDATING;
		rc = SMB_DOMAIN_SUCCESS;
		break;

	case SMB_DCACHE_STATE_UPDATING:
		while (smb_dcache.dc_state == SMB_DCACHE_STATE_UPDATING)
			(void) cond_wait(&smb_dcache.dc_cv,
			    &smb_dcache.dc_mtx);

		if (smb_dcache.dc_state == SMB_DCACHE_STATE_READY) {
			smb_dcache.dc_state = SMB_DCACHE_STATE_UPDATING;
			rc = SMB_DOMAIN_SUCCESS;
		} else {
			rc = SMB_DOMAIN_NO_CACHE;
		}
		break;

	case SMB_DCACHE_STATE_NONE:
	case SMB_DCACHE_STATE_DESTROYING:
		rc = SMB_DOMAIN_NO_CACHE;
		break;

	default:
		break;
	}

	(void) mutex_unlock(&smb_dcache.dc_mtx);
	return (rc);
}

/*
 * Transfers the cache from UPDATING to READY state.
 *
 * Nothing will happen if the cache is no longer available
 * or it is being destroyed.
 */
static void
smb_dcache_ready(void)
{
	(void) mutex_lock(&smb_dcache.dc_mtx);
	switch (smb_dcache.dc_state) {
	case SMB_DCACHE_STATE_UPDATING:
		smb_dcache.dc_state = SMB_DCACHE_STATE_READY;
		(void) cond_broadcast(&smb_dcache.dc_cv);
		break;

	case SMB_DCACHE_STATE_NONE:
	case SMB_DCACHE_STATE_DESTROYING:
		break;

	default:
		assert(0);
	}
	(void) mutex_unlock(&smb_dcache.dc_mtx);
}

static void
smb_dcache_error(uint32_t errnum, const char *fmt, ...)
{
	static struct {
		uint32_t	errnum;
		char		*errtxt;
	} errtab[] = {
		{ SMB_DOMAIN_SUCCESS,		"success" },
		{ SMB_DOMAIN_NOMACHINE_SID,	"no machine SID" },
		{ SMB_DOMAIN_NODOMAIN_SID,	"no domain SID" },
		{ SMB_DOMAIN_NODOMAIN_NAME,	"no domain name" },
		{ SMB_DOMAIN_INTERNAL_ERR,	"internal error" },
		{ SMB_DOMAIN_INVALID_ARG,	"invalid parameter" },
		{ SMB_DOMAIN_NO_MEMORY,		"no memory" },
		{ SMB_DOMAIN_NO_CACHE,		"no cache" }
	};

	va_list	ap;
	char	buf[SMB_DCACHE_BUFSIZE];
	char	*errtxt = "unknown error";
	int	tablesz = sizeof (errtab) / sizeof errtab[0];
	int	i;

	va_start(ap, fmt);
	(void) vsnprintf(buf, SMB_DCACHE_BUFSIZE, fmt, ap);
	va_end(ap);

	for (i = 0; i < tablesz; ++i) {
		if (errnum == errtab[i].errnum) {
			errtxt = errtab[i].errtxt;
			break;
		}
	}

	syslog(LOG_DEBUG, "smb domain cache: %s: %s", buf, errtxt);
}
