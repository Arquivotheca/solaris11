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

#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/idmap.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <netdb.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libsmbns.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/smbinfo.h>
#include "smbd.h"

#define	SMBD_DC_MONITOR_ATTEMPTS		2
#define	SMBD_DC_MONITOR_RETRY_INTERVAL		3	/* seconds */
#define	SMBD_DC_MONITOR_INTERVAL		60	/* seconds */

extern smbd_t smbd;

static mutex_t smbd_dc_mutex;
static cond_t smbd_dc_cv;

static void *smbd_dc_monitor(void *);
static void smbd_dc_update(const char *);
static boolean_t smbd_set_netlogon_cred(void);
static uint32_t smbd_join_workgroup(smb_joininfo_t *);
static uint32_t smbd_join_domain(smb_joininfo_t *);
static uint32_t smbd_create_trust_account(smb_domainex_t *, char *, char *);

/*
 * NT domain join support (using MSRPC)
 */
static boolean_t smbd_ntjoin_support = B_FALSE;

#define	SMBD_DCLOCATOR_TIMEOUT			45	/* seconds */

typedef struct smb_dclocator {
	char		sdl_domain[SMB_PI_MAX_DOMAIN];
	char		sdl_dc[MAXHOSTNAMELEN];
	boolean_t	sdl_locate;
	mutex_t		sdl_mtx;
	cond_t		sdl_cv;
	uint32_t	sdl_status;
} smb_dclocator_t;

static smb_dclocator_t smb_dclocator;

typedef boolean_t (*smbd_locateop_t)(char *, char *, smb_domainex_t *);
typedef uint32_t (*smbd_queryop_t)(char *, char *, smb_domain_t *);

static boolean_t smbd_locate_dc(char *, const char *, smb_domainex_t *);
static void *smbd_ddiscover_service(void *);
static void smbd_ddiscover_main(char *, char *);
static boolean_t smbd_ddiscover_dns(char *, char *, smb_domainex_t *);
static boolean_t smbd_ddiscover_nbt(char *, char *, smb_domainex_t *);
static boolean_t smbd_ddiscover_cfg(char *, char *, smb_domainex_t *);
static boolean_t smbd_ddiscover_resolv(char *, char *, uint32_t);
static uint32_t smbd_ddiscover_query_info(char *, char *, smb_domainex_t *);
static uint32_t smbd_ddiscover_use_config(char *, char *, smb_domain_t *);
static void smbd_domainex_free(smb_domainex_t *);
static void smbd_remove_keytab_entries(void);
static boolean_t smbd_clear_dc(void);

/*
 * Launch the DC discovery and monitor thread.
 */
int
smbd_dc_monitor_init(void)
{
	int	rc;

	(void) smb_config_getstr(SMB_CI_ADS_SITE, smbd.s_site,
	    MAXHOSTNAMELEN);
	(void) smb_config_getip(SMB_CI_DOMAIN_SRV, &smbd.s_pdc);

	rc = smbd_thread_create("DC locator", smbd_ddiscover_service, NULL);
	if (rc != 0) {
		smbd_log(LOG_NOTICE, "DC locator initialization failed: %s",
		    strerror(rc));
		return (rc);
	}

	if (smbd.s_secmode != SMB_SECMODE_DOMAIN)
		return (0);

	rc = smbd_thread_create("DC monitor", smbd_dc_monitor, NULL);
	if (rc != 0) {
		smbd_log(LOG_NOTICE, "DC monitor initialization failed: %s",
		    strerror(rc));
	}

	return (rc);
}

void
smbd_dc_monitor_refresh(void)
{
	char		site[MAXHOSTNAMELEN];
	smb_inaddr_t	pdc;

	site[0] = '\0';
	bzero(&pdc, sizeof (smb_inaddr_t));
	(void) smb_config_getstr(SMB_CI_ADS_SITE, site, MAXHOSTNAMELEN);
	(void) smb_config_getip(SMB_CI_DOMAIN_SRV, &pdc);

	(void) mutex_lock(&smbd_dc_mutex);

	if ((bcmp(&smbd.s_pdc, &pdc, sizeof (smb_inaddr_t)) != 0) ||
	    (smb_strcasecmp(smbd.s_site, site, 0) != 0)) {
		bcopy(&pdc, &smbd.s_pdc, sizeof (smb_inaddr_t));
		(void) strlcpy(smbd.s_site, site, MAXHOSTNAMELEN);
		smbd.s_dscfg_changed = B_TRUE;
		(void) cond_signal(&smbd_dc_cv);
	}

	(void) mutex_unlock(&smbd_dc_mutex);
}

/*ARGSUSED*/
static void *
smbd_dc_monitor(void *arg)
{
	char		selected_dc[MAXHOSTNAMELEN];
	boolean_t	ds_not_responding = B_FALSE;
	boolean_t	ds_cfg_changed = B_FALSE;
	timestruc_t	delay;
	int		i;
	int		secmode;

	if (smb_config_getstr(SMB_CI_DC_SELECTED, selected_dc,
	    MAXHOSTNAMELEN) != 0)
		selected_dc[0] = '\0';

	smbd_dc_update(selected_dc);
	smbd_online_wait("smbd_dc_monitor");

	while (smbd_online()) {
		delay.tv_sec = SMBD_DC_MONITOR_INTERVAL;
		delay.tv_nsec = 0;

		(void) mutex_lock(&smbd_dc_mutex);
		(void) cond_reltimedwait(&smbd_dc_cv, &smbd_dc_mutex, &delay);

		if (smbd.s_dscfg_changed) {
			smbd.s_dscfg_changed = B_FALSE;
			ds_cfg_changed = B_TRUE;
		}

		(void) mutex_unlock(&smbd_dc_mutex);

		for (i = 0; i < SMBD_DC_MONITOR_ATTEMPTS; ++i) {
			if (dssetup_check_service() == 0) {
				ds_not_responding = B_FALSE;
				break;
			}

			ds_not_responding = B_TRUE;
			(void) sleep(SMBD_DC_MONITOR_RETRY_INTERVAL);
		}

		secmode = smb_config_get_secmode();
		if ((ds_not_responding) && (secmode == SMB_SECMODE_DOMAIN))
			smbd_log(LOG_NOTICE,
			    "smbd_dc_monitor: domain service not responding");

		if (ds_not_responding || ds_cfg_changed) {
			/*
			 * Trigger DC update only if the system is still in
			 * domain mode
			 */
			if (secmode == SMB_SECMODE_DOMAIN) {
				ds_cfg_changed = B_FALSE;
				smb_ads_refresh();
				smbd_dc_update("");
			}

			smb_krb5_cfg_update_kdcs();
		}
	}

	smbd_thread_exit();
	return (NULL);
}

/*
 * Locate a domain controller in the current resource domain and Update
 * the Netlogon credential chain.
 *
 * The domain configuration will be updated upon successful DC discovery.
 */
static void
smbd_dc_update(const char *dc)
{
	char		domain[MAXHOSTNAMELEN];
	smb_domainex_t	info;
	smb_domain_t	*primary;


	if (smb_getdomainname_ad(domain, MAXHOSTNAMELEN) != 0) {
		(void) smb_getdomainname_nb(domain, MAXHOSTNAMELEN);
		(void) smb_strupr(domain);
	}

	if (!smbd_locate_dc(domain, dc, &info)) {
		smbd_log(LOG_NOTICE,
		    "domain %s: cannot locate a domain controller", domain);
	} else {
		primary = &info.d_primary;

		smb_config_setdomaininfo(primary->di_nbname,
		    primary->di_fqname,
		    primary->di_sid,
		    primary->di_u.di_dns.ddi_forest,
		    primary->di_u.di_dns.ddi_guid);

		smbd_log(LOG_NOTICE, "domain %s: domain controller %s",
		    domain, info.d_dc);
	}

	if (smbd_set_netlogon_cred()) {
		/*
		 * Restart required because the domain changed
		 * or the credential chain setup failed.
		 */
		smbd_log(LOG_NOTICE,
		    "smbd_dc_update: smb/server restart required");

		if (smb_smf_restart_service() != 0)
			smbd_log(LOG_ERR,
			    "restart failed: run 'svcs -xv smb/server'"
			    " for more information");
	}
}

uint32_t
smbd_discover_dc(smb_joininfo_t *info)
{
	uint32_t status = NT_STATUS_SUCCESS;
	unsigned char passwd_hash[SMBAUTH_HASH_SZ];
	smb_domainex_t dxi;

	if (smb_auth_ntlm_hash(info->domain_passwd, passwd_hash)
	    != SMBAUTH_SUCCESS) {
		syslog(LOG_ERR, "smbd: could not compute ntlm hash for '%s'",
		    info->domain_username);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	/* Write the username & password to IPC cache temporarily */
	smb_ipc_set(info->domain_username, passwd_hash);

	/* info->domain_name could either be NetBIOS domain name or FQDN */
	if (!smbd_locate_dc(info->domain_name, info->dc, &dxi)) {
		syslog(LOG_ERR, "smbd: failed locating domain controller "
		    "for %s", info->domain_name);
		status = NT_STATUS_DOMAIN_CONTROLLER_NOT_FOUND;
	}

	smb_ipc_rollback();
	return (status);
}

/*
 * smbd_join
 *
 * Joins the specified domain/workgroup.
 *
 * If the security mode or domain name is being changed,
 * the caller must restart the service.
 */
uint32_t
smbd_join(smb_joininfo_t *info)
{
	uint32_t status;

	dssetup_clear_domain_info();
	if (info->mode == SMB_SECMODE_WORKGRP)
		status = smbd_join_workgroup(info);
	else
		status = smbd_join_domain(info);

	return (status);
}

/*
 * smbd_set_netlogon_cred
 *
 * If the system is joined to an AD domain via kclient, SMB daemon will need
 * to establish the NETLOGON credential chain.
 *
 * Since the kclient has updated the machine password stored in SMF
 * repository, the cached ipc_info must be updated accordingly by calling
 * smb_ipc_commit.
 *
 * Due to potential replication delays in a multiple DC environment, the
 * NETLOGON rpc request must be sent to the DC, to which the KPASSWD request
 * is sent. If the DC discovered by the SMB daemon is different than the
 * kpasswd server, the current connection with the DC will be torn down
 * and a DC discovery process will be triggered to locate the kpasswd
 * server.
 *
 * If joining a new domain, the domain_name property must be set after a
 * successful credential chain setup.
 */
static boolean_t
smbd_set_netlogon_cred(void)
{
	char kpasswd_srv[MAXHOSTNAMELEN];
	char kpasswd_domain[MAXHOSTNAMELEN];
	char sam_acct[SMB_SAMACCT_MAXLEN];
	char ipc_usr[SMB_USERNAME_MAXLEN];
	char *dom;
	boolean_t new_domain = B_FALSE;
	smb_domainex_t dxi;
	smb_domain_t *di;

	if (smb_match_netlogon_seqnum())
		return (B_FALSE);

	(void) smb_config_getstr(SMB_CI_KPASSWD_SRV, kpasswd_srv,
	    sizeof (kpasswd_srv));

	if (*kpasswd_srv == '\0')
		return (B_FALSE);

	/*
	 * If the domain join initiated by smbadm join CLI is in
	 * progress, don't do anything.
	 */
	(void) smb_getsamaccount(sam_acct, sizeof (sam_acct));
	smb_ipc_get_user(ipc_usr, SMB_USERNAME_MAXLEN);
	if (smb_strcasecmp(ipc_usr, sam_acct, 0))
		return (B_FALSE);

	di = &dxi.d_primary;
	if (!smb_domain_getinfo(&dxi))
		(void) smb_getdomainname_ad(di->di_fqname, MAXHOSTNAMELEN);

	(void) smb_config_getstr(SMB_CI_KPASSWD_DOMAIN, kpasswd_domain,
	    sizeof (kpasswd_domain));

	if (*kpasswd_domain != '\0' &&
	    smb_strcasecmp(kpasswd_domain, di->di_fqname, 0)) {
		dom = kpasswd_domain;
		new_domain = B_TRUE;
	} else {
		dom = di->di_fqname;
	}

	/*
	 * DC discovery will be triggered if the domain info is not
	 * currently cached or the SMB daemon has previously discovered a DC
	 * that is different than the kpasswd server.
	 */
	if (new_domain || smb_strcasecmp(dxi.d_dc, kpasswd_srv, 0)
	    != 0) {
		if (!smbd_locate_dc(dom, kpasswd_srv, &dxi)) {
			if (!smbd_locate_dc(di->di_fqname, "", &dxi)) {
				smb_ipc_commit();
				return (B_FALSE);
			}
		}
	}

	smb_ipc_commit();

	if (netlogon_setup_auth(dxi.d_dc, di->di_nbname)) {
		smbd_log(LOG_NOTICE, "NETLOGON credential chain setup failed");
		return (B_TRUE);
	} else {
		if (new_domain) {
			smb_config_setdomaininfo(di->di_nbname, di->di_fqname,
			    di->di_sid,
			    di->di_u.di_dns.ddi_forest,
			    di->di_u.di_dns.ddi_guid);
			(void) smb_config_setstr(SMB_CI_KPASSWD_DOMAIN, "");
		}
	}

	return (new_domain);
}

/*
 * Domain cache will be cleared as part of joining a workgroup.
 */
static uint32_t
smbd_join_workgroup(smb_joininfo_t *info)
{
	char nb_domain[SMB_PI_MAX_DOMAIN];

	smbd_remove_keytab_entries();
	(void) smb_getdomainname_nb(nb_domain, sizeof (nb_domain));
	smbd_set_secmode(SMB_SECMODE_WORKGRP);
	smb_config_setdomaininfo(info->domain_name, "", "", "", "");

	if (strcasecmp(nb_domain, info->domain_name))
		smb_browser_reconfig();

	if (smb_config_set_idmap_domain("") != 0)
		smbd_log(LOG_NOTICE, "idmap configuration update failed");

	if (smf_restart_instance(IDMAP_FMRI_DEFAULT) != 0)
		smbd_log(LOG_ERR, "idmap restart failed");

	if (!smbd_clear_dc())
		smbd_log(LOG_ERR,
		    "Unable to clear the prior domain controller info");

	return (NT_STATUS_SUCCESS);
}

/*
 * Discard any Kerberos keys associated with the prior domain in the local
 * keytab.
 */
static void
smbd_remove_keytab_entries(void)
{
	char ad_domain[MAXHOSTNAMELEN];

	if (smb_config_get_secmode() == SMB_SECMODE_DOMAIN) {
		(void) smb_getdomainname_ad(ad_domain, sizeof (ad_domain));
		if (smb_krb5_kt_remove(ad_domain) != 0)
			smbd_log(LOG_NOTICE, "Unable to remove Kerberos keys "
			    "associated with previous domain \"%s\". Please "
			    "manually remove keys by running ktutil CLI.",
			    ad_domain);
	}
}

/*
 * If the system is not configured to be a domain member prior to upgrading
 * to a release that supports Kerberos user authentication, the service
 * principals introduced by PSARC/2009/673 case will be registered with AD and
 * the associated keys will also be added to the local keytab during the
 * first AD join attempt.
 */
static uint32_t
smbd_join_domain(smb_joininfo_t *info)
{
	uint32_t	status;
	unsigned char	passwd_hash[SMBAUTH_HASH_SZ];
	smb_domainex_t	dxi;
	smb_domain_t	*di;

	if (smb_auth_ntlm_hash(info->domain_passwd, passwd_hash)
	    != SMBAUTH_SUCCESS) {
		smbd_log(LOG_ERR, "NTLM hash for %s failed",
		    info->domain_username);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	smb_ipc_set(info->domain_username, passwd_hash);
	if (!smb_domain_getinfo(&dxi)) {
		smb_ipc_rollback();
		status = NT_STATUS_CANT_ACCESS_DOMAIN_INFO;
		smbd_log(LOG_ERR, "unable to join %s (%s)",
		    info->domain_name, xlate_nt_status(status));
		return (status);
	}

	status = smbd_create_trust_account(&dxi, info->domain_username,
	    info->domain_passwd);
	if (status != NT_STATUS_SUCCESS) {
		smb_ipc_rollback();
		smbd_log(LOG_ERR, "unable to join %s (%s)",
		    info->domain_name, xlate_nt_status(status));
		return (status);
	}

	di = &dxi.d_primary;

	if (smb_config_set_idmap_domain(di->di_fqname) != 0) {
		smbd_log(LOG_ERR, "idmap configuration update failed");
		return (NT_STATUS_UNSUCCESSFUL);
	}

	if (smf_restart_instance(IDMAP_FMRI_DEFAULT) != 0) {
		smbd_log(LOG_ERR, "idmap restart failed");
		return (NT_STATUS_UNSUCCESSFUL);
	}

	smb_ipc_commit();
	smbd_set_secmode(SMB_SECMODE_DOMAIN);
	smb_config_setdomaininfo(di->di_nbname, di->di_fqname, di->di_sid,
	    di->di_u.di_dns.ddi_forest, di->di_u.di_dns.ddi_guid);
	return (NT_STATUS_SUCCESS);
}

/*
 * Create a machine trust account on the selected domain controller.
 *
 * Returns NT status codes.
 */
static uint32_t
smbd_create_trust_account(smb_domainex_t *dxi, char *user, char *plain_text)
{
	char machine_passwd[NETR_MACHINE_ACCT_PASSWD_MAX];
	smb_domain_t *domain;
	smb_adjoin_status_t adrc;
	uint32_t status;
	int rc;

	machine_passwd[0] = '\0';
	domain = &dxi->d_primary;

	if (smbd_ntjoin_support == B_FALSE) {
		adrc = smb_ads_join(domain->di_fqname, user, plain_text,
		    machine_passwd, sizeof (machine_passwd));

		smb_ccache_remove();

		if (adrc == SMB_ADJOIN_SUCCESS) {
			status = NT_STATUS_SUCCESS;
		} else {
			smb_ads_join_errmsg(adrc);
			status = NT_STATUS_UNSUCCESSFUL;
		}
	} else {
		status = sam_create_trust_account(dxi->d_dc,
		    domain->di_nbname);

		if (status == NT_STATUS_SUCCESS) {
			(void) smb_getnetbiosname(machine_passwd,
			    sizeof (machine_passwd));
			(void) smb_strlwr(machine_passwd);
		}
	}

	if (status == NT_STATUS_SUCCESS) {
		rc = smb_setdomainprops(NULL, dxi->d_dc, machine_passwd);
		if (rc != 0) {
			smbd_log(LOG_NOTICE, "domain property update failed");
			bzero(machine_passwd, sizeof (machine_passwd));
			return (NT_STATUS_UNSUCCESSFUL);
		}

		status = netlogon_setup_auth(dxi->d_dc, domain->di_nbname);
	}

	bzero(machine_passwd, sizeof (machine_passwd));
	return (status);
}

/*
 * Request DC locator thread to clear both the domain controller and the
 * primary domain from the domain cache.
 */
static boolean_t
smbd_clear_dc(void)
{
	int rc;
	timestruc_t to;
	smb_domainex_t domain_info;
	boolean_t clear;

	(void) mutex_lock(&smb_dclocator.sdl_mtx);

	if (!smb_dclocator.sdl_locate) {
		smb_dclocator.sdl_locate = B_TRUE;
		(void) strlcpy(smb_dclocator.sdl_domain, "",
		    SMB_PI_MAX_DOMAIN);
		(void) strlcpy(smb_dclocator.sdl_dc, "", MAXHOSTNAMELEN);
		(void) cond_broadcast(&smb_dclocator.sdl_cv);
	}

	while (smb_dclocator.sdl_locate) {
		to.tv_sec = SMBD_DCLOCATOR_TIMEOUT;
		to.tv_nsec = 0;
		rc = cond_reltimedwait(&smb_dclocator.sdl_cv,
		    &smb_dclocator.sdl_mtx, &to);

		if (rc == ETIME)
			break;
	}

	clear = !smb_domain_getinfo(&domain_info);
	(void) mutex_unlock(&smb_dclocator.sdl_mtx);

	return (clear);
}

/*
 * This is the entry point for discovering a domain controller for the
 * specified domain.
 *
 * The actual work of discovering a DC is handled by DC locator thread.
 * All we do here is signal the request and wait for a DC or a timeout.
 *
 * Input parameters:
 *  domain - domain to be discovered (can either be NetBIOS or DNS domain)
 *  dc - preferred DC. If the preferred DC is set to empty string, it
 *       will attempt to discover any DC in the specified domain.
 *
 * Output parameter:
 *  dp - on success, dp will be filled with the discovered DC and domain
 *       information.
 * Returns B_TRUE if the DC/domain info is available.
 */
static boolean_t
smbd_locate_dc(char *domain, const char *dc, smb_domainex_t *dp)
{
	int rc;
	boolean_t found;
	timestruc_t to;
	smb_domainex_t domain_info;

	if (domain == NULL || *domain == '\0')
		return (B_FALSE);

	(void) mutex_lock(&smb_dclocator.sdl_mtx);

	if (!smb_dclocator.sdl_locate) {
		smb_dclocator.sdl_locate = B_TRUE;
		(void) strlcpy(smb_dclocator.sdl_domain, domain,
		    SMB_PI_MAX_DOMAIN);
		(void) strlcpy(smb_dclocator.sdl_dc, dc, MAXHOSTNAMELEN);
		(void) cond_broadcast(&smb_dclocator.sdl_cv);
	}

	while (smb_dclocator.sdl_locate) {
		to.tv_sec = SMBD_DCLOCATOR_TIMEOUT;
		to.tv_nsec = 0;
		rc = cond_reltimedwait(&smb_dclocator.sdl_cv,
		    &smb_dclocator.sdl_mtx, &to);

		if (rc == ETIME)
			break;
	}

	if (dp == NULL)
		dp = &domain_info;
	found = smb_domain_getinfo(dp);

	(void) mutex_unlock(&smb_dclocator.sdl_mtx);

	return (found);
}

/*
 * This is the domain and DC discovery service: it gets woken up whenever
 * there is need to locate a domain controller.
 *
 * Upon success, the SMB domain cache will be populated with the discovered
 * DC and domain info.
 */
/*ARGSUSED*/
static void *
smbd_ddiscover_service(void *arg)
{
	char domain[SMB_PI_MAX_DOMAIN];
	char sought_dc[MAXHOSTNAMELEN];

	for (;;) {
		(void) mutex_lock(&smb_dclocator.sdl_mtx);

		while (!smb_dclocator.sdl_locate)
			(void) cond_wait(&smb_dclocator.sdl_cv,
			    &smb_dclocator.sdl_mtx);

		(void) strlcpy(domain, smb_dclocator.sdl_domain,
		    SMB_PI_MAX_DOMAIN);
		(void) strlcpy(sought_dc, smb_dclocator.sdl_dc, MAXHOSTNAMELEN);
		(void) mutex_unlock(&smb_dclocator.sdl_mtx);

		smbd_ddiscover_main(domain, sought_dc);

		(void) mutex_lock(&smb_dclocator.sdl_mtx);
		smb_dclocator.sdl_locate = B_FALSE;
		(void) cond_broadcast(&smb_dclocator.sdl_cv);
		(void) mutex_unlock(&smb_dclocator.sdl_mtx);
	}

	/*NOTREACHED*/
	return (NULL);
}

/*
 * Attempt to find a domain controller for the specified domain.
 * Various methods are attempted: DNS, NetBIOS, reading from SMF.
 *
 * If a domain controller is discovered successfully it will be
 * queried for primary and trusted domain information.
 * If everything is successful the domain cache will be updated.
 *
 * This function can be called with an empty-string domain to clear
 * both primary domain and located DC from the domain cache,
 * which is done as part of joining a workgroup.
 */
static void
smbd_ddiscover_main(char *domain, char *server)
{
	static smbd_locateop_t ops[] = {
		smbd_ddiscover_dns,
		smbd_ddiscover_nbt,
		smbd_ddiscover_cfg
	};

	smb_domainex_t	dxi;
	boolean_t	discovered = B_FALSE;
	int		n_op = (sizeof (ops) / sizeof (ops[0]));
	int		i;

	bzero(&dxi, sizeof (smb_domainex_t));

	if (smb_domain_start_update() != SMB_DOMAIN_SUCCESS)
		return;

	if (*domain == '\0') {
		smb_domain_update(&dxi);
		discovered = B_TRUE;
	} else {
		for (i = 0; i < n_op; ++i) {
			discovered = (*ops[i])(domain, server, &dxi);

			if (discovered) {
				smb_domain_update(&dxi);
				break;
			}
		}
	}

	smb_domain_end_update();
	smbd_domainex_free(&dxi);
}

/*
 * Find a DC for the specified domain via DNS.
 *
 * On success, query the primary and trusted domain information.
 */
static boolean_t
smbd_ddiscover_dns(char *domain, char *server, smb_domainex_t *dxi)
{
	uint32_t status;

	if (!SMB_IS_FQDN(domain))
		return (B_FALSE);

	if (!smb_ads_lookup_msdcs(domain, server, dxi->d_dc, MAXHOSTNAMELEN))
		return (B_FALSE);

	status = smbd_ddiscover_query_info(domain, dxi->d_dc, dxi);
	return (status == NT_STATUS_SUCCESS);
}

/*
 * Find a DC for the specified domain using the NETLOGON protocol.
 * The input domain name should be a NetBIOS domain name (it must
 * not contain a dot).
 *
 * We do a DNS lookup on the IP address returned by the browser
 * because the DC name may have been truncated to fit the NetBIOS
 * name size limit, which may not be correct if the DC's NetBIOS
 * name is not derived from its hostname.  The IP address will
 * always be IPv4 here because it was returned via NetBIOS.
 *
 * If the DNS lookup succeeds, we use the DNS name.  Otherwise we
 * assume the NetBIOS name is usable.
 *
 * The NetBIOS domain name returned by the query info should be the
 * same as the specified NetBIOS domain name.
 */
static boolean_t
smbd_ddiscover_nbt(char *domain, char *server, smb_domainex_t *dxi)
{
	char		nb_domain[NETBIOS_NAME_SZ];
	struct hostent	*h;
	uint32_t	ipaddr;
	uint32_t	status;
	int		rc;

	if (SMB_IS_FQDN(domain))
		return (B_FALSE);

	(void) strlcpy(nb_domain, domain, NETBIOS_NAME_SZ);
	(void) smb_strupr(nb_domain);

	if (!smb_browser_netlogon(nb_domain, dxi->d_dc, MAXHOSTNAMELEN,
	    &ipaddr))
		return (B_FALSE);

	h = getipnodebyaddr((char *)&ipaddr, sizeof (uint32_t), AF_INET, &rc);
	if (h != NULL)
		(void) strlcpy(dxi->d_dc, h->h_name, MAXHOSTNAMELEN);

	status = smbd_ddiscover_query_info(nb_domain, dxi->d_dc, dxi);
	if (status != NT_STATUS_SUCCESS)
		return (B_FALSE);

	if (smb_strcasecmp(nb_domain, dxi->d_primary.di_nbname, 0) != 0)
		return (B_FALSE);

	if (smb_ads_lookup_msdcs(dxi->d_primary.di_fqname, server,
	    dxi->d_dc, MAXHOSTNAMELEN) == 0)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Given a NetBIOS domain name, try to derive the DNS name from the
 * resolver configuration and then proceed as for DNS discovery.
 *
 * The NetBIOS domain name returned by the query info should be the
 * same as the specified NetBIOS domain name.
 */
static boolean_t
smbd_ddiscover_cfg(char *nb_domain, char *server, smb_domainex_t *dxi)
{
	char dns_domain[MAXHOSTNAMELEN];
	uint32_t status;

	if (SMB_IS_FQDN(nb_domain))
		return (B_FALSE);

	if (!smbd_ddiscover_resolv(nb_domain, dns_domain, MAXHOSTNAMELEN))
		return (B_FALSE);

	if (!smb_ads_lookup_msdcs(dns_domain, server, dxi->d_dc,
	    MAXHOSTNAMELEN))
		return (B_FALSE);

	status = smbd_ddiscover_query_info(nb_domain, dxi->d_dc, dxi);
	if (status != NT_STATUS_SUCCESS)
		return (B_FALSE);

	if (smb_strcasecmp(nb_domain, dxi->d_primary.di_nbname, 0) != 0)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Try to derive the DNS domain from the given NetBIOS domain by checking
 * the first label of entries in the resolver configuration.
 */
static boolean_t
smbd_ddiscover_resolv(char *nb_domain, char *buf, uint32_t len)
{
	struct __res_state res_state;
	int i;
	char *entry, *p;
	char first_label[NETBIOS_NAME_SZ];
	boolean_t found;

	bzero(&res_state, sizeof (struct __res_state));
	if (res_ninit(&res_state))
		return (B_FALSE);

	found = B_FALSE;
	entry = res_state.defdname;
	for (i = 0; entry != NULL; i++) {
		(void) strlcpy(first_label, entry, NETBIOS_NAME_SZ);
		if ((p = strchr(first_label, '.')) != NULL)
			*p = '\0';

		if (smb_strcasecmp(nb_domain, first_label, 0) == 0) {
			found = B_TRUE;
			(void) strlcpy(buf, entry, len);
			break;
		}

		entry = res_state.dnsrch[i];
	}

	res_ndestroy(&res_state);
	return (found);
}

/*
 * Obtain primary and trusted domain information using MSRPC queries or by
 * reading the SMF configuration.  The domain parameter can be a NetBIOS
 * or DNS domain name.
 *
 * MSRPC queries have been added to Windows over time to expand on the
 * information returned.  Requests are made preferential order, falling
 * back to older requests if the newer ones fail.
 */
static uint32_t
smbd_ddiscover_query_info(char *domain, char *server, smb_domainex_t *dxi)
{
	static smbd_queryop_t ops[] = {
		lsa_query_dns_domain_info,
		dssetup_query_domain_info,
		smbd_ddiscover_use_config,
		lsa_query_primary_domain_info
	};

	smb_trusted_domains_t	*list;
	smb_domain_t		*dinfo;
	uint32_t		status;
	int			n_op = (sizeof (ops) / sizeof (ops[0]));
	int			i;

	dinfo = &dxi->d_primary;

	for (i = 0; i < n_op; ++i) {
		bzero(dinfo, sizeof (smb_domain_t));

		status = (*ops[i])(server, domain, dinfo);

		if (status == NT_STATUS_SUCCESS) {
			list = &dxi->d_trusted;
			(void) lsa_enum_trusted_domains(server, domain, list);
			break;
		}
	}

	return (status);
}

/*
 * Read the primary domain information from SMF and check whether or not the
 * domain to be discovered matches the current (NetBIOS or DNS) domain name.
 */
/*ARGSUSED*/
static uint32_t
smbd_ddiscover_use_config(char *server, char *domain, smb_domain_t *dinfo)
{
	dinfo->di_type = SMB_DOMAIN_PRIMARY;

	smb_config_getdomaininfo(dinfo->di_nbname, dinfo->di_fqname,
	    dinfo->di_sid,
	    dinfo->di_u.di_dns.ddi_forest,
	    dinfo->di_u.di_dns.ddi_guid);

	if ((smb_strcasecmp(dinfo->di_fqname, domain, 0) == 0) ||
	    (smb_strcasecmp(dinfo->di_nbname, domain, 0) == 0))
		return (NT_STATUS_SUCCESS);
	else
		return (NT_STATUS_UNSUCCESSFUL);
}

static void
smbd_domainex_free(smb_domainex_t *dxi)
{
	free(dxi->d_trusted.td_domains);
}
