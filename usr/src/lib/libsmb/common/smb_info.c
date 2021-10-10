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

#include <assert.h>
#include <sys/types.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <synch.h>
#include <syslog.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <smbsrv/smbinfo.h>
#include <smbsrv/netbios.h>
#include <smbsrv/libsmb.h>

static mutex_t seqnum_mtx;
static void smb_getmachineguid(char *, size_t);

/*
 * IPC connection information that may be passed to the SMB Redirector.
 */
typedef struct {
	char	user[SMB_USERNAME_MAXLEN];
	uint8_t	passwd[SMBAUTH_HASH_SZ];
} smb_ipc_t;

static smb_ipc_t	ipc_info;
static smb_ipc_t	ipc_orig_info;
static rwlock_t		smb_ipc_lock;

/*
 * SMB revision
 */
typedef struct smb_revision {
	uint32_t	rev_major;
	uint32_t	rev_minor;
} smb_revision_t;

static void smb_str2rev(char *, smb_revision_t *);

/*
 * Some older clients (Windows 98) only handle the low byte
 * of the max workers value. If the low byte is less than
 * SMB_PI_MAX_WORKERS_MIN set it to SMB_PI_MAX_WORKERS_MIN.
 */
void
smb_load_kconfig(smb_kmod_cfg_t *kcfg)
{
	int64_t citem;
	char guid_str[UUID_PRINTABLE_STRING_LENGTH];

	bzero(kcfg, sizeof (smb_kmod_cfg_t));

	(void) smb_config_getnum(SMB_CI_MAX_WORKERS, &citem);
	kcfg->skc_maxworkers = (uint32_t)citem;
	if ((kcfg->skc_maxworkers & 0xFF) < SMB_PI_MAX_WORKERS_MIN) {
		kcfg->skc_maxworkers &= ~0xFF;
		kcfg->skc_maxworkers += SMB_PI_MAX_WORKERS_MIN;
	} else if (kcfg->skc_maxworkers > SMB_PI_MAX_WORKERS_MAX) {
		kcfg->skc_maxworkers = SMB_PI_MAX_WORKERS_MAX;
	}

	(void) smb_config_getnum(SMB_CI_KEEPALIVE, &citem);
	kcfg->skc_keepalive = (uint32_t)citem;
	if ((kcfg->skc_keepalive != 0) &&
	    (kcfg->skc_keepalive < SMB_PI_KEEP_ALIVE_MIN))
		kcfg->skc_keepalive = SMB_PI_KEEP_ALIVE_MIN;

	(void) smb_config_getnum(SMB_CI_MAX_CONNECTIONS, &citem);
	kcfg->skc_maxconnections = (uint32_t)citem;
	if ((kcfg->skc_maxconnections == 0) ||
	    (kcfg->skc_maxconnections > SMB_PI_MAX_CONNECTIONS_MAX))
		kcfg->skc_maxconnections = SMB_PI_MAX_CONNECTIONS_MAX;

	kcfg->skc_restrict_anon = smb_config_getbool(SMB_CI_RESTRICT_ANON);
	kcfg->skc_enforce_vczero = smb_config_getbool(SMB_CI_ENFORCE_VCZERO);
	kcfg->skc_signing_enable =
	    smb_config_getbool(SMB_CI_SVR_SIGNING_ENABLE);
	kcfg->skc_signing_required =
	    smb_config_getbool(SMB_CI_SVR_SIGNING_REQD);
	kcfg->skc_ipv6_enable = smb_config_getbool(SMB_CI_IPV6_ENABLE);
	kcfg->skc_print_enable = smb_config_getbool(SMB_CI_PRINT_ENABLE);
	kcfg->skc_oplock_enable = smb_config_getbool(SMB_CI_OPLOCK_ENABLE);
	kcfg->skc_sync_enable = smb_config_getbool(SMB_CI_SYNC_ENABLE);
	kcfg->skc_secmode = smb_config_get_secmode();
	(void) smb_getdomainname_nb(kcfg->skc_nbdomain,
	    sizeof (kcfg->skc_nbdomain));
	(void) smb_getnetbiosname(kcfg->skc_hostname,
	    sizeof (kcfg->skc_hostname));
	(void) smb_config_getstr(SMB_CI_SYS_CMNT, kcfg->skc_system_comment,
	    sizeof (kcfg->skc_system_comment));
	smb_config_get_version(&kcfg->skc_version);
	kcfg->skc_execflags = smb_config_get_execinfo(NULL, NULL, 0);
	kcfg->skc_extsec_enable = smb_config_getbool(SMB_CI_SVR_EXTSEC);
	smb_getmachineguid(guid_str, UUID_PRINTABLE_STRING_LENGTH);
	if (uuid_parse(guid_str, kcfg->skc_machine_guid) != 0)
		bzero(kcfg->skc_machine_guid, UUID_LEN);
}

static void
smb_getmachineguid(char *guid_str, size_t len)
{
	uuid_t guid;
	int rc;

	bzero(guid_str, len);
	rc = smb_config_getstr(SMB_CI_MACHINE_GUID, guid_str, len);
	if ((rc != SMBD_SMF_OK) || (*guid_str == '\0')) {
		uuid_generate(guid);
		uuid_unparse(guid, guid_str);
		(void) smb_config_setstr(SMB_CI_MACHINE_GUID, guid_str);
	}
}

/*
 * Get the current system NetBIOS name.  The hostname is truncated at
 * the first `.` or 15 bytes, whichever occurs first, and converted
 * to uppercase (by smb_gethostname).  Text that appears after the
 * first '.' is considered to be part of the NetBIOS scope.
 *
 * Returns 0 on success, otherwise -1 to indicate an error.
 */
int
smb_getnetbiosname(char *buf, size_t buflen)
{
	if (smb_gethostname(buf, buflen, SMB_CASE_UPPER) != 0)
		return (-1);

	if (buflen >= NETBIOS_NAME_SZ)
		buf[NETBIOS_NAME_SZ - 1] = '\0';

	return (0);
}

/*
 * Get the SAM account of the current system.
 * Returns 0 on success, otherwise, -1 to indicate an error.
 */
int
smb_getsamaccount(char *buf, size_t buflen)
{
	if (smb_getnetbiosname(buf, buflen - 1) != 0)
		return (-1);

	(void) strlcat(buf, "$", buflen);
	return (0);
}

/*
 * Get the current system node name.  The returned name is guaranteed
 * to be null-terminated (gethostname may not null terminate the name).
 * If the hostname has been fully-qualified for some reason, the domain
 * part will be removed.  The returned hostname is converted to the
 * specified case (lower, upper, or preserved).
 *
 * If gethostname fails, the returned buffer will contain an empty
 * string.
 */
int
smb_gethostname(char *buf, size_t buflen, smb_caseconv_t which)
{
	char *p;

	if (buf == NULL || buflen == 0)
		return (-1);

	if (gethostname(buf, buflen) != 0) {
		*buf = '\0';
		return (-1);
	}

	buf[buflen - 1] = '\0';

	if ((p = strchr(buf, '.')) != NULL)
		*p = '\0';

	switch (which) {
	case SMB_CASE_LOWER:
		(void) smb_strlwr(buf);
		break;

	case SMB_CASE_UPPER:
		(void) smb_strupr(buf);
		break;

	case SMB_CASE_PRESERVE:
	default:
		break;
	}

	return (0);
}

/*
 * Obtain the fully-qualified DNS name for this machine in lower case.
 */
int
smb_getfqhostname(char *buf, size_t buflen)
{
	char hostname[MAXHOSTNAMELEN];
	char domain[MAXHOSTNAMELEN];

	hostname[0] = '\0';
	domain[0] = '\0';

	if (smb_gethostname(hostname, MAXHOSTNAMELEN,
	    SMB_CASE_LOWER) != 0)
		return (-1);

	if (smb_getdomainname_dns(domain, MAXHOSTNAMELEN) != 0)
		return (-1);

	if (hostname[0] == '\0')
		return (-1);

	if (domain[0] == '\0') {
		(void) strlcpy(buf, hostname, buflen);
		return (0);
	}

	(void) snprintf(buf, buflen, "%s.%s", hostname, domain);
	return (0);
}

/*
 * Returns NETBIOS name of the domain if the system is in domain
 * mode. Or returns workgroup name if the system is in workgroup
 * mode.
 */
int
smb_getdomainname_nb(char *buf, size_t buflen)
{
	int rc;

	if (buf == NULL || buflen == 0)
		return (-1);

	*buf = '\0';
	rc = smb_config_getstr(SMB_CI_DOMAIN_NB, buf, buflen);

	if ((rc != SMBD_SMF_OK) || (*buf == '\0'))
		return (-1);

	return (0);
}

/*
 * Returns fully-qualified DNS name of the AD domain.
 *
 * The domain name returned by this function should not be used for generating
 * a fully-qualified hostname of either the local system or domain controllers
 * if disjoint namespace is intended.
 */
int
smb_getdomainname_ad(char *buf, size_t buflen)
{
	if (buf == NULL || buflen == 0)
		return (-1);

	*buf = '\0';
	if (smb_config_get_secmode() != SMB_SECMODE_DOMAIN)
		return (-1);

	if ((smb_config_getstr(SMB_CI_DOMAIN_AD, buf, buflen) != 0) ||
	    (*buf == '\0'))
		return (-1);

	return (0);
}

/* Primary DNS suffix of the local system */
int
smb_get_dns_suffix(char *buf, size_t buflen)
{
	if (smb_config_getstr(SMB_CI_DNS_SUFFIX, buf, buflen) != SMBD_SMF_OK ||
	    *buf == '\0')
		return (-1);

	return (0);
}

/*
 * Fully qualified DNS domain name of the local system.
 *
 * If primary DNS suffix is configured, that's the fully qualified domain name
 * of the local system regardless of whether it's in domain or workgroup mode.
 * Otherwise, if the local system is in domain mode, the fully-qualified name
 * of the AD domain is returned. If the system is in workgroup mode, the local
 * domain obtained via resolver is returned
 *
 * The domain name returned by this function should not be used for generating
 * a fully-qualified hostname of a domain controller if disjoint namespace is
 * intended.
 *
 * Returns 0 upon success.  Otherwise, returns -1.
 */
int
smb_getdomainname_dns(char *buf, size_t buflen)
{
	struct __res_state res_state;

	if (buf == NULL || buflen == 0)
		return (-1);

	*buf = '\0';

	if (smb_get_dns_suffix(buf, buflen) == 0)
		return (0);

	if (smb_getdomainname_ad(buf, buflen) == 0)
		return (0);

	bzero(&res_state, sizeof (struct __res_state));
	if (res_ninit(&res_state))
		return (-1);

	if (*res_state.defdname == '\0') {
		res_ndestroy(&res_state);
		return (-1);
	}

	(void) strlcpy(buf, res_state.defdname, buflen);
	res_ndestroy(&res_state);

	return (0);
}


/*
 * smb_set_machine_passwd
 *
 * This function should be used when setting the machine password property.
 * The associated sequence number is incremented.
 */
static int
smb_set_machine_passwd(char *passwd)
{
	int64_t num;
	int rc = -1;

	if (smb_config_setstr(SMB_CI_MACHINE_PASSWD, passwd) != SMBD_SMF_OK)
		return (-1);

	(void) mutex_lock(&seqnum_mtx);
	(void) smb_config_getnum(SMB_CI_KPASSWD_SEQNUM, &num);
	if (smb_config_setnum(SMB_CI_KPASSWD_SEQNUM, ++num)
	    == SMBD_SMF_OK)
		rc = 0;
	(void) mutex_unlock(&seqnum_mtx);
	return (rc);
}

static int
smb_get_machine_passwd(uint8_t *buf, size_t buflen)
{
	char pwd[SMB_PASSWD_MAXLEN + 1];
	int rc;

	if (buflen < SMBAUTH_HASH_SZ)
		return (-1);

	rc = smb_config_getstr(SMB_CI_MACHINE_PASSWD, pwd, sizeof (pwd));
	if ((rc != SMBD_SMF_OK) || *pwd == '\0')
		return (-1);

	if (smb_auth_ntlm_hash(pwd, buf) != 0)
		return (-1);

	return (rc);
}

/*
 * Set up IPC connection credentials.
 */
void
smb_ipc_init(void)
{
	int rc;

	(void) rw_wrlock(&smb_ipc_lock);
	bzero(&ipc_info, sizeof (smb_ipc_t));
	bzero(&ipc_orig_info, sizeof (smb_ipc_t));

	(void) smb_getsamaccount(ipc_info.user, SMB_USERNAME_MAXLEN);
	rc = smb_get_machine_passwd(ipc_info.passwd, SMBAUTH_HASH_SZ);
	if (rc != 0)
		*ipc_info.passwd = 0;
	(void) rw_unlock(&smb_ipc_lock);

}

/*
 * Set the IPC username and password hash in memory.  If the domain
 * join succeeds, the credentials will be committed for use with
 * authenticated IPC.  Otherwise, they should be rolled back.
 */
void
smb_ipc_set(char *plain_user, uint8_t *passwd_hash)
{
	(void) rw_wrlock(&smb_ipc_lock);
	(void) strlcpy(ipc_info.user, plain_user, sizeof (ipc_info.user));
	(void) memcpy(ipc_info.passwd, passwd_hash, SMBAUTH_HASH_SZ);
	(void) rw_unlock(&smb_ipc_lock);

}

/*
 * Save the host credentials to be used for authenticated IPC.
 * The credentials are also saved to the original IPC info as
 * rollback data in case the join domain process fails later.
 */
void
smb_ipc_commit(void)
{
	(void) rw_wrlock(&smb_ipc_lock);
	(void) smb_getsamaccount(ipc_info.user, SMB_USERNAME_MAXLEN);
	(void) smb_get_machine_passwd(ipc_info.passwd, SMBAUTH_HASH_SZ);
	(void) memcpy(&ipc_orig_info, &ipc_info, sizeof (smb_ipc_t));
	(void) rw_unlock(&smb_ipc_lock);
}

/*
 * Restore the original credentials
 */
void
smb_ipc_rollback(void)
{
	(void) rw_wrlock(&smb_ipc_lock);
	(void) strlcpy(ipc_info.user, ipc_orig_info.user,
	    sizeof (ipc_info.user));
	(void) memcpy(ipc_info.passwd, ipc_orig_info.passwd,
	    sizeof (ipc_info.passwd));
	(void) rw_unlock(&smb_ipc_lock);
}

void
smb_ipc_get_user(char *buf, size_t buflen)
{
	(void) rw_rdlock(&smb_ipc_lock);
	(void) strlcpy(buf, ipc_info.user, buflen);
	(void) rw_unlock(&smb_ipc_lock);
}

void
smb_ipc_get_passwd(uint8_t *buf, size_t buflen)
{
	if (buflen < SMBAUTH_HASH_SZ)
		return;

	(void) rw_rdlock(&smb_ipc_lock);
	(void) memcpy(buf, ipc_info.passwd, SMBAUTH_HASH_SZ);
	(void) rw_unlock(&smb_ipc_lock);
}

/*
 * Ensure the directory for the Kerberos ccache file exists
 * and set KRB5CCNAME in the environment.
 */
int
smb_ccache_init(void)
{
	static char buf[MAXPATHLEN];

	if ((mkdir(SMB_VARRUN_DIR, 0700) < 0) && (errno != EEXIST)) {
		syslog(LOG_ERR, "%s: mkdir failed: %s", SMB_CCACHE_PATH,
		    strerror(errno));
		return (-1);
	}

	(void) snprintf(buf, MAXPATHLEN, "KRB5CCNAME=%s", SMB_CCACHE_PATH);
	if (putenv(buf) != 0) {
		syslog(LOG_ERR, "unable to set KRB5CCNAME");
		return (-1);
	}

	return (0);
}

void
smb_ccache_remove(void)
{
	if ((remove(SMB_CCACHE_PATH) < 0) && (errno != ENOENT))
		syslog(LOG_ERR, "%s: remove failed: %s", SMB_CCACHE_PATH,
		    strerror(errno));
}

/*
 * smb_match_netlogon_seqnum
 *
 * A sequence number is associated with each machine password property
 * update and the netlogon credential chain setup. If the
 * sequence numbers don't match, a NETLOGON credential chain
 * establishment is required.
 *
 * Returns 0 if kpasswd_seqnum equals to netlogon_seqnum. Otherwise,
 * returns -1.
 */
boolean_t
smb_match_netlogon_seqnum(void)
{
	int64_t setpasswd_seqnum;
	int64_t netlogon_seqnum;

	(void) mutex_lock(&seqnum_mtx);
	(void) smb_config_getnum(SMB_CI_KPASSWD_SEQNUM, &setpasswd_seqnum);
	(void) smb_config_getnum(SMB_CI_NETLOGON_SEQNUM, &netlogon_seqnum);
	(void) mutex_unlock(&seqnum_mtx);
	return (setpasswd_seqnum == netlogon_seqnum);
}

/*
 * smb_setdomainprops
 *
 * This function should be called after joining an AD to
 * set all the domain related SMF properties.
 *
 * The kpasswd_domain property is the AD domain to which the system
 * is joined via kclient. If this function is invoked by the SMB
 * daemon, fqdn should be set to NULL.
 */
int
smb_setdomainprops(char *fqdn, char *server, char *passwd)
{
	if (server == NULL || passwd == NULL)
		return (-1);

	if ((*server == '\0') || (*passwd == '\0'))
		return (-1);

	if (fqdn && (smb_config_setstr(SMB_CI_KPASSWD_DOMAIN, fqdn) != 0))
		return (-1);

	if (smb_config_setstr(SMB_CI_KPASSWD_SRV, server) != 0)
		return (-1);

	if (smb_set_machine_passwd(passwd) != 0) {
		syslog(LOG_ERR, "smb_setdomainprops: failed to set"
		    " machine account password");
		return (-1);
	}

	return (0);
}

/*
 * smb_update_netlogon_seqnum
 *
 * This function should only be called upon a successful netlogon
 * credential chain establishment to set the sequence number of the
 * netlogon to match with that of the kpasswd.
 */
void
smb_update_netlogon_seqnum(void)
{
	int64_t num;

	(void) mutex_lock(&seqnum_mtx);
	(void) smb_config_getnum(SMB_CI_KPASSWD_SEQNUM, &num);
	(void) smb_config_setnum(SMB_CI_NETLOGON_SEQNUM, num);
	(void) mutex_unlock(&seqnum_mtx);
}


/*
 * Temporary fbt for dtrace until user space sdt enabled.
 */
void
smb_tracef(const char *fmt, ...)
{
	va_list ap;
	char buf[128];

	va_start(ap, fmt);
	(void) vsnprintf(buf, 128, fmt, ap);
	va_end(ap);

	smb_trace(buf);
}

/*
 * Temporary fbt for dtrace until user space sdt enabled.
 */
void
smb_trace(const char *s)
{
	syslog(LOG_DEBUG, "%s", s);
}

/*
 * smb_tonetbiosname
 *
 * Creates a NetBIOS name based on the given name and suffix.
 * NetBIOS name is 15 capital characters, padded with space if needed
 * and the 16th byte is the suffix.
 */
void
smb_tonetbiosname(char *name, char *nb_name, char suffix)
{
	char tmp_name[NETBIOS_NAME_SZ];
	smb_wchar_t wtmp_name[NETBIOS_NAME_SZ];
	int len;
	size_t rc;

	len = 0;
	rc = smb_mbstowcs(wtmp_name, (const char *)name, NETBIOS_NAME_SZ);

	if (rc != (size_t)-1) {
		wtmp_name[NETBIOS_NAME_SZ - 1] = 0;
		rc = ucstooem(tmp_name, wtmp_name, NETBIOS_NAME_SZ,
		    OEM_CPG_850);
		if (rc > 0)
			len = strlen(tmp_name);
	}

	(void) memset(nb_name, ' ', NETBIOS_NAME_SZ - 1);
	if (len) {
		(void) smb_strupr(tmp_name);
		(void) memcpy(nb_name, tmp_name, len);
	}
	nb_name[NETBIOS_NAME_SZ - 1] = suffix;
}

int
smb_get_nameservers(smb_inaddr_t *ips, int sz)
{
	union res_sockaddr_union set[MAXNS];
	int i, cnt;
	struct __res_state res_state;
	char ipstr[INET6_ADDRSTRLEN];

	if (ips == NULL)
		return (0);

	bzero(&res_state, sizeof (struct __res_state));
	if (res_ninit(&res_state) < 0)
		return (0);

	cnt = res_getservers(&res_state, set, MAXNS);
	for (i = 0; i < cnt; i++) {
		if (i >= sz)
			break;
		ips[i].a_family = AF_INET;
		bcopy(&set[i].sin.sin_addr, &ips[i].a_ipv4, INADDRSZ);
		if (inet_ntop(AF_INET, &ips[i].a_ipv4, ipstr,
		    INET_ADDRSTRLEN)) {
			syslog(LOG_DEBUG, "Found %s name server\n", ipstr);
			continue;
		}
		ips[i].a_family = AF_INET6;
		bcopy(&set[i].sin.sin_addr, &ips[i].a_ipv6, IPV6_ADDR_LEN);
		if (inet_ntop(AF_INET6, &ips[i].a_ipv6, ipstr,
		    INET6_ADDRSTRLEN)) {
			syslog(LOG_DEBUG, "Found %s name server\n", ipstr);
		}
	}
	res_ndestroy(&res_state);
	return (i);
}

/*
 * smb_gethostbyname
 *
 * Looks up a host by the given name. The host entry can come
 * from any of the sources for hosts specified in the
 * /etc/nsswitch.conf and the NetBIOS cache.
 *
 * XXX Invokes nbt_name_resolve API once the NBTD is integrated
 * to look in the NetBIOS cache if getipnodebyname fails.
 *
 * Caller should invoke freehostent to free the returned hostent.
 */
struct hostent *
smb_gethostbyname(const char *name, int *err_num)
{
	struct hostent *h;

	h = getipnodebyname(name, AF_INET, 0, err_num);
	if ((h == NULL) || h->h_length != INADDRSZ)
		h = getipnodebyname(name, AF_INET6, AI_DEFAULT, err_num);
	return (h);
}

/*
 * smb_gethostbyaddr
 *
 * Looks up a host by the given IP address. The host entry can come
 * from any of the sources for hosts specified in the
 * /etc/nsswitch.conf and the NetBIOS cache.
 *
 * XXX Invokes nbt API to resolve name by IP once the NBTD is integrated
 * to look in the NetBIOS cache if getipnodebyaddr fails.
 *
 * Caller should invoke freehostent to free the returned hostent.
 */
struct hostent *
smb_gethostbyaddr(const char *addr, int *err_num)
{
	struct hostent *h = NULL;
	char inetaddr[NS_IN6ADDRSZ];

	if (inet_pton(AF_INET, addr, inetaddr) > 0)
		h = getipnodebyaddr(inetaddr, sizeof (struct in_addr),
		    AF_INET, err_num);
	else if (inet_pton(AF_INET6, addr, inetaddr) > 0)
		h = getipnodebyaddr(inetaddr, sizeof (struct in6_addr),
		    AF_INET6, err_num);

	return (h);
}

/*
 * Compares the specified revision with the current revision.
 * Returns:
 *  <0: rev is < current rev
 *   0: rev == current rev
 *  >0: rev > current rev
 */
int
smb_revision_cmp(char *revstr, smb_cfg_id_t current_revid)
{
	char current_revstr[8];
	smb_revision_t current_rev, rev;

	if (smb_config_getstr(current_revid, current_revstr,
	    sizeof (current_revstr)) != SMBD_SMF_OK)
		(void) strlcpy(current_revstr, SMB_REV_DEFAULT,
		    sizeof (current_revstr));

	smb_str2rev(current_revstr, &current_rev);
	smb_str2rev(revstr, &rev);

	if (rev.rev_major == current_rev.rev_major)
		return (rev.rev_minor - current_rev.rev_minor);

	return (rev.rev_major - current_rev.rev_major);
}

/*
 * Sets major and minor revisions by parsing the revision string buffer in
 * the format of <major>.<minor> . If the specified revision string doesn't
 * contain a '.', the minor revision will be default to 0.
 */
static void
smb_str2rev(char *str, smb_revision_t *rev)
{
	char buf[8];
	char *p;
	int len;

	(void) strlcpy(buf, str, sizeof (buf));
	if ((p = strchr(buf, '.')) == NULL) {
		rev->rev_major = atoi(str);
		rev->rev_minor = 0;
	} else {
		len = strlen(buf);
		*p = '\0';
		rev->rev_major = atoi(buf);
		if (len == (strlen(str) + 1))
			rev->rev_minor = 0;
		else
			rev->rev_minor = atoi(++p);
	}
}
