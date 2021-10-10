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

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/varargs.h>
#include <sys/types.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <tiuser.h>
#include <netconfig.h>
#include <netdir.h>
#include <sys/systeminfo.h>
#include <sys/utsname.h>
#include <libzfs.h>
#include <dlfcn.h>
#include <time.h>
#include <syslog.h>
#include <smbsrv/string.h>
#include <smbsrv/libsmb.h>

#define	SMB_LIB_ALT	"/usr/lib/smbsrv/libsmbex.so"

#define	SMB_FLOCK_WAIT	15 /* in seconds */

#define	SMB_TIMEBUF_SZ		16

#define	SMB_LOG_FILE_FMT	"/var/smb/%s_log.txt"

#define	SMB_ISCNTRL(c)	((((c) >= 0) && ((c) <= 0x1f)) || ((c) == 0x7f))

typedef struct smb_log_pri {
	char	*lp_name;
	int	lp_value;
} smb_log_pri_t;

static smb_log_pri_t smb_log_pri[] = {
	"panic",	LOG_EMERG,
	"emerg",	LOG_EMERG,
	"alert",	LOG_ALERT,
	"crit",		LOG_CRIT,
	"error",	LOG_ERR,
	"err",		LOG_ERR,
	"warn",		LOG_WARNING,
	"warning",	LOG_WARNING,
	"notice",	LOG_NOTICE,
	"info",		LOG_INFO,
	"debug",	LOG_DEBUG
};

static void smb_log_trace(int, const char *);
static smb_log_t *smb_log_get(smb_log_hdl_t);
static void smb_log_dump(smb_log_t *);

static uint_t smb_make_mask(char *, uint_t);
static boolean_t smb_netmatch(struct netbuf *, char *);
static boolean_t smb_netgroup_match(struct nd_hostservlist *, char *, int);
static int smb_getzfsmount(const char *, struct mnttab *);

extern  int __multi_innetgr();
extern int __netdir_getbyaddr_nosrv(struct netconfig *,
    struct nd_hostservlist **, struct netbuf *);

static smb_loglist_t smb_loglist;

#define	C2H(c)		"0123456789ABCDEF"[(c)]
#define	H2C(c)    (((c) >= '0' && (c) <= '9') ? ((c) - '0') :     \
	((c) >= 'a' && (c) <= 'f') ? ((c) - 'a' + 10) :         \
	((c) >= 'A' && (c) <= 'F') ? ((c) - 'A' + 10) :         \
	'\0')
#define	DEFAULT_SBOX_SIZE		256

/*
 *
 * hexdump
 *
 * Simple hex dump display function. Displays nbytes of buffer in hex and
 * printable format. Non-printing characters are shown as '.'. It is safe
 * to pass a null pointer. Each line begins with the offset. If nbytes is
 * 0, the line will be blank except for the offset. Example output:
 *
 * 00000000  54 68 69 73 20 69 73 20 61 20 70 72 6F 67 72 61  This is a progra
 * 00000010  6D 20 74 65 73 74 2E 00                          m test..
 *
 */
void
hexdump_offset(unsigned char *buffer, int nbytes, unsigned long *start)
{
	static char *hex = "0123456789ABCDEF";
	int i, count;
	int offset;
	unsigned char *p;
	char ascbuf[64];
	char hexbuf[64];
	char *ap = ascbuf;
	char *hp = hexbuf;

	if ((p = buffer) == NULL)
		return;

	offset = *start;

	*ap = '\0';
	*hp = '\0';
	count = 0;

	for (i = 0; i < nbytes; ++i) {
		if (i && (i % 16) == 0) {
			smb_tracef("%06X %s  %s", offset, hexbuf, ascbuf);
			ap = ascbuf;
			hp = hexbuf;
			count = 0;
			offset += 16;
		}

		ap += sprintf(ap, "%c",
		    (*p >= 0x20 && *p < 0x7F) ? *p : '.');
		hp += sprintf(hp, " %c%c",
		    hex[(*p >> 4) & 0x0F], hex[(*p & 0x0F)]);
		++p;
		++count;
	}

	if (count) {
		smb_tracef("%06X %-48s  %s", offset, hexbuf, ascbuf);
		offset += count;
	}

	*start = offset;
}

void
hexdump(unsigned char *buffer, int nbytes)
{
	unsigned long start = 0;

	hexdump_offset(buffer, nbytes, &start);
}

/*
 * bintohex
 *
 * Converts the given binary data (srcbuf) to
 * its equivalent hex chars (hexbuf).
 *
 * hexlen should be at least twice as srclen.
 * if hexbuf is not big enough returns 0.
 * otherwise returns number of valid chars in
 * hexbuf which is srclen * 2.
 */
size_t
bintohex(const char *srcbuf, size_t srclen,
    char *hexbuf, size_t hexlen)
{
	size_t outlen;
	char c;

	outlen = srclen << 1;

	if (hexlen < outlen)
		return (0);

	while (srclen-- > 0) {
		c = *srcbuf++;
		*hexbuf++ = C2H(c & 0xF);
		*hexbuf++ = C2H((c >> 4) & 0xF);
	}

	return (outlen);
}

/*
 * hextobin
 *
 * Converts hex to binary.
 *
 * Assuming hexbuf only contains hex digits (chars)
 * this function convert every two bytes of hexbuf
 * to one byte and put it in dstbuf.
 *
 * hexlen should be an even number.
 * dstlen should be at least half of hexlen.
 *
 * Returns 0 if sizes are not correct, otherwise
 * returns the number of converted bytes in dstbuf
 * which is half of hexlen.
 */
size_t
hextobin(const char *hexbuf, size_t hexlen,
    char *dstbuf, size_t dstlen)
{
	size_t outlen;

	if ((hexlen % 2) != 0)
		return (0);

	outlen = hexlen >> 1;
	if (dstlen < outlen)
		return (0);

	while (hexlen > 0) {
		*dstbuf = H2C(*hexbuf) & 0x0F;
		hexbuf++;
		*dstbuf++ |= (H2C(*hexbuf) << 4) & 0xF0;
		hexbuf++;

		hexlen -= 2;
	}

	return (outlen);
}

/*
 * Trim leading and trailing characters in the set defined by class
 * from a buffer containing a null-terminated string.
 * For example, if the input buffer contained "ABtext23" and class
 * contains "ABC123", the buffer will contain "text" on return.
 *
 * This function modifies the contents of buf in place and returns
 * a pointer to buf.
 */
char *
strtrim(char *buf, const char *class)
{
	char *p = buf;
	char *q = buf;

	if (buf == NULL)
		return (NULL);

	p += strspn(p, class);

	if (p != buf) {
		while ((*q = *p++) != '\0')
			++q;
	}

	while (q != buf) {
		--q;
		if (strspn(q, class) == 0)
			return (buf);
		*q = '\0';
	}

	return (buf);
}

/*
 * Strip the characters in the set defined by class from a buffer
 * containing a null-terminated string.
 * For example, if the input buffer contained "XYA 1textZ string3"
 * and class contains "123XYZ", the buffer will contain "A text string"
 * on return.
 *
 * This function modifies the contents of buf in place and returns
 * a pointer to buf.
 */
char *
strstrip(char *buf, const char *class)
{
	char *p = buf;
	char *q = buf;

	if (buf == NULL)
		return (NULL);

	while (*p) {
		p += strspn(p, class);
		*q++ = *p++;
	}

	*q = '\0';
	return (buf);
}

/*
 * trim_whitespace
 *
 * Trim leading and trailing whitespace chars (as defined by isspace)
 * from a buffer. Example; if the input buffer contained "  text  ",
 * it will contain "text", when we return. We assume that the buffer
 * contains a null terminated string. A pointer to the buffer is
 * returned.
 */
char *
trim_whitespace(char *buf)
{
	char *p = buf;
	char *q = buf;

	if (buf == NULL)
		return (NULL);

	while (*p && isspace(*p))
		++p;

	while ((*q = *p++) != 0)
		++q;

	if (q != buf) {
		while ((--q, isspace(*q)) != 0)
			*q = '\0';
	}

	return (buf);
}

/*
 * randomize
 *
 * Randomize the contents of the specified buffer.
 */
void
randomize(char *data, unsigned len)
{
	unsigned dwlen = len / 4;
	unsigned remlen = len % 4;
	unsigned tmp;
	unsigned i; /*LINTED E_BAD_PTR_CAST_ALIGN*/
	unsigned *p = (unsigned *)data;

	for (i = 0; i < dwlen; ++i)
		*p++ = random();

	if (remlen) {
		tmp = random();
		(void) memcpy(p, &tmp, remlen);
	}
}

/*
 * This is the hash mechanism used to encrypt passwords for commands like
 * SamrSetUserInformation. It uses a 256 byte s-box.
 */
void
rand_hash(
    unsigned char *data,
    size_t datalen,
    unsigned char *key,
    size_t keylen)
{
	unsigned char sbox[DEFAULT_SBOX_SIZE];
	unsigned char tmp;
	unsigned char index_i = 0;
	unsigned char index_j = 0;
	unsigned char j = 0;
	int i;

	for (i = 0; i < DEFAULT_SBOX_SIZE; ++i)
		sbox[i] = (unsigned char)i;

	for (i = 0; i < DEFAULT_SBOX_SIZE; ++i) {
		j += (sbox[i] + key[i % keylen]);

		tmp = sbox[i];
		sbox[i] = sbox[j];
		sbox[j] = tmp;
	}

	for (i = 0; i < datalen; ++i) {
		index_i++;
		index_j += sbox[index_i];

		tmp = sbox[index_i];
		sbox[index_i] = sbox[index_j];
		sbox[index_j] = tmp;

		tmp = sbox[index_i] + sbox[index_j];
		data[i] = data[i] ^ sbox[tmp];
	}
}

/*
 * smb_chk_hostaccess
 *
 * Determines whether the specified host is in the given access list.
 *
 * We match on aliases of the hostname as well as on the canonical name.
 * Names in the access list may be either hosts or netgroups;  they're
 * not distinguished syntactically.  We check for hosts first because
 * it's cheaper (just M*N strcmp()s), then try netgroups.
 *
 * Function returns:
 *	-1 for "all" (list is empty "" or "*")
 *	0 not found  (host is not in the list or list is NULL)
 *	1 found
 *
 */
int
smb_chk_hostaccess(smb_inaddr_t *ipaddr, char *access_list)
{
	int nentries;
	char *gr;
	char *lasts;
	char *host;
	int off;
	int i;
	int netgroup_match;
	int response;
	struct nd_hostservlist *clnames;
	struct in_addr inaddr;
	struct sockaddr_in sa;
	struct netbuf buf;
	struct netconfig *config;

	if (access_list == NULL)
		return (0);

	inaddr.s_addr = ipaddr->a_ipv4;

	/*
	 * If access list is empty or "*" - then it's "all"
	 */
	if (*access_list == '\0' || strcmp(access_list, "*") == 0)
		return (-1);

	nentries = 0;

	sa.sin_family = AF_INET;
	sa.sin_port = 0;
	sa.sin_addr = inaddr;

	buf.len = buf.maxlen = sizeof (sa);
	buf.buf = (char *)&sa;

	config = getnetconfigent("tcp");
	if (config == NULL)
		return (1);

	if (__netdir_getbyaddr_nosrv(config, &clnames, &buf)) {
		freenetconfigent(config);
		return (0);
	}
	freenetconfigent(config);

	for (gr = strtok_r(access_list, ":", &lasts);
	    gr != NULL; gr = strtok_r(NULL, ":", &lasts)) {

		/*
		 * If the list name has a '-' prepended
		 * then a match of the following name
		 * implies failure instead of success.
		 */
		if (*gr == '-') {
			response = 0;
			gr++;
		} else {
			response = 1;
		}

		/*
		 * The following loops through all the
		 * client's aliases.  Usually it's just one name.
		 */
		for (i = 0; i < clnames->h_cnt; i++) {
			host = clnames->h_hostservs[i].h_host;
			/*
			 * If the list name begins with a dot then
			 * do a domain name suffix comparison.
			 * A single dot matches any name with no
			 * suffix.
			 */
			if (*gr == '.') {
				if (*(gr + 1) == '\0') {  /* single dot */
					if (strchr(host, '.') == NULL)
						return (response);
				} else {
					off = strlen(host) - strlen(gr);
					if (off > 0 &&
					    strcasecmp(host + off, gr) == 0) {
						return (response);
					}
				}
			} else {

				/*
				 * If the list name begins with an at
				 * sign then do a network comparison.
				 */
				if (*gr == '@') {
					if (smb_netmatch(&buf, gr + 1))
						return (response);
				} else {
					/*
					 * Just do a hostname match
					 */
					if (strcasecmp(gr, host) == 0)
						return (response);
				}
			}
		}

		nentries++;
	}

	netgroup_match = smb_netgroup_match(clnames, access_list, nentries);

	return (netgroup_match);
}

/*
 * smb_make_mask
 *
 * Construct a mask for an IPv4 address using the @<dotted-ip>/<len>
 * syntax or use the default mask for the IP address.
 */
static uint_t
smb_make_mask(char *maskstr, uint_t addr)
{
	uint_t mask;
	uint_t bits;

	/*
	 * If the mask is specified explicitly then
	 * use that value, e.g.
	 *
	 *    @109.104.56/28
	 *
	 * otherwise assume a mask from the zero octets
	 * in the least significant bits of the address, e.g.
	 *
	 *   @109.104  or  @109.104.0.0
	 */
	if (maskstr) {
		bits = atoi(maskstr);
		mask = bits ? ~0 << ((sizeof (struct in_addr) * NBBY) - bits)
		    : 0;
		addr &= mask;
	} else {
		if ((addr & IN_CLASSA_HOST) == 0)
			mask = IN_CLASSA_NET;
		else if ((addr & IN_CLASSB_HOST) == 0)
			mask = IN_CLASSB_NET;
		else if ((addr & IN_CLASSC_HOST) == 0)
			mask = IN_CLASSC_NET;
		else
			mask = IN_CLASSE_NET;
	}

	return (mask);
}

/*
 * smb_netmatch
 *
 * Check to see if the address in the netbuf matches the "net"
 * specified by name.  The format of "name" can be:
 *	fully qualified domain name
 *	dotted IP address
 *	dotted IP address followed by '/<len>'
 *	See sharen_nfs(1M) for details.
 */

static boolean_t
smb_netmatch(struct netbuf *nb, char *name)
{
	uint_t claddr;
	struct netent n, *np;
	char *mp, *p;
	uint_t addr, mask;
	int i;
	char buff[256];

	/*
	 * Check if it's an IPv4 addr
	 */
	if (nb->len != sizeof (struct sockaddr_in))
		return (B_FALSE);

	(void) memcpy(&claddr,
	    &((struct sockaddr_in *)nb->buf)->sin_addr.s_addr,
	    sizeof (struct in_addr));
	claddr = ntohl(claddr);

	mp = strchr(name, '/');
	if (mp)
		*mp++ = '\0';

	if (isdigit(*name)) {
		/*
		 * Convert a dotted IP address
		 * to an IP address. The conversion
		 * is not the same as that in inet_addr().
		 */
		p = name;
		addr = 0;
		for (i = 0; i < 4; i++) {
			addr |= atoi(p) << ((3-i) * 8);
			p = strchr(p, '.');
			if (p == NULL)
				break;
			p++;
		}
	} else {
		/*
		 * Turn the netname into
		 * an IP address.
		 */
		np = getnetbyname_r(name, &n, buff, sizeof (buff));
		if (np == NULL) {
			return (B_FALSE);
		}
		addr = np->n_net;
	}

	mask = smb_make_mask(mp, addr);
	return ((claddr & mask) == addr);
}

/*
 * smb_netgroup_match
 *
 * Check whether any of the hostnames in clnames are
 * members (or non-members) of the netgroups in glist.
 * Since the innetgr lookup is rather expensive, the
 * result is cached. The cached entry is valid only
 * for VALID_TIME seconds.  This works well because
 * typically these lookups occur in clusters when
 * a client is mounting.
 *
 * Note that this routine establishes a host membership
 * in a list of netgroups - we've no idea just which
 * netgroup in the list it is a member of.
 *
 * glist is a character array containing grc strings
 * representing netgroup names (optionally prefixed
 * with '-'). Each string is ended with '\0'  and
 * followed immediately by the next string.
 */
static boolean_t
smb_netgroup_match(struct nd_hostservlist *clnames, char  *glist, int grc)
{
	char **grl;
	char *gr;
	int nhosts = clnames->h_cnt;
	char *host;
	int i, j, n;
	boolean_t response;
	boolean_t belong = B_FALSE;
	static char *domain = NULL;

	if (domain == NULL) {
		int	ssize;

		domain = malloc(SYS_NMLN);
		if (domain == NULL)
			return (B_FALSE);

		ssize = sysinfo(SI_SRPC_DOMAIN, domain, SYS_NMLN);
		if (ssize > SYS_NMLN) {
			free(domain);
			domain = malloc(ssize);
			if (domain == NULL)
				return (B_FALSE);
			ssize = sysinfo(SI_SRPC_DOMAIN, domain, ssize);
		}
		/* Check for error in syscall or NULL domain name */
		if (ssize <= 1)
			return (B_FALSE);
	}

	grl = calloc(grc, sizeof (char *));
	if (grl == NULL)
		return (B_FALSE);

	for (i = 0, gr = glist; i < grc && !belong; ) {
		/*
		 * If the netgroup name has a '-' prepended
		 * then a match of this name implies a failure
		 * instead of success.
		 */
		response = (*gr != '-') ? B_TRUE : B_FALSE;

		/*
		 * Subsequent names with or without a '-' (but no mix)
		 * can be grouped together for a single check.
		 */
		for (n = 0; i < grc; i++, n++, gr += strlen(gr) + 1) {
			if ((response && *gr == '-') ||
			    (!response && *gr != '-'))
				break;

			grl[n] = response ? gr : gr + 1;
		}

		/*
		 * Check the netgroup for each
		 * of the hosts names (usually just one).
		 */
		for (j = 0; j < nhosts && !belong; j++) {
			host = clnames->h_hostservs[j].h_host;
			if (__multi_innetgr(n, grl, 1, &host, 0, NULL,
			    1, &domain))
				belong = B_TRUE;
		}
	}

	free(grl);
	return (belong ? response : B_FALSE);
}

/*
 * Resolve the ZFS dataset from a path.
 * Ensure that there are no leading slashes (required for zfs_open).
 */
int
smb_getdataset(const char *path, char *dataset, size_t len)
{
	struct mnttab	mnttab;
	char		*resource;
	int		rc;

	if ((rc = smb_getzfsmount(path, &mnttab)) == 0) {
		resource = mnttab.mnt_special;
		resource += strspn(resource, "/");
		(void) strlcpy(dataset, resource, len);
	}

	return (rc);
}

/*
 * Resolve the ZFS mountpoint from a path.  The ZFS mountpoint property
 * is not reliable: it is not always updated and can remain pointing at
 * a previously used temporary mount point.  For example, even though a
 * dataset is mounted on / the mountpoint property may still contain some
 * temporary path such as /tmp/onu.swaWuc.
 *
 * So we have to search the MNTTAB and obtain the mount point from the
 * mnttab entry.
 */
int
smb_getmountpoint(const char *path, char *mountpoint, size_t len)
{
	struct mnttab	mnttab;
	int		rc;

	if ((rc = smb_getzfsmount(path, &mnttab)) == 0)
		(void) strlcpy(mountpoint, mnttab.mnt_mountp, len);

	return (rc);
}

/*
 * Get the ZFS mnttab entry from a path.  If the specified path isn't a ZFS
 * mount point, repeatedly remove the last component of the path until it
 * matches a ZFS mnttab entry.
 *
 * Returns: 0 on success, -1 on failure.
 */
static int
smb_getzfsmount(const char *path, struct mnttab *mnttab)
{
	char tmppath[MAXPATHLEN];
	char *cp;
	FILE *fp;
	struct mnttab mntpref;
	int rc = -1;

	if ((fp = fopen(MNTTAB, "r")) == NULL)
		return (-1);

	(void) memset(mnttab, '\0', sizeof (mnttab));
	(void) strlcpy(tmppath, path, MAXPATHLEN);
	cp = tmppath;

	while (*cp != '\0') {
		resetmnttab(fp);
		(void) memset(&mntpref, '\0', sizeof (mntpref));
		mntpref.mnt_mountp = tmppath;

		if (getmntany(fp, mnttab, &mntpref) == 0) {
			if (mnttab->mnt_fstype == NULL)
				break;

			if (strcmp(mnttab->mnt_fstype, MNTTYPE_ZFS) == 0)
				rc = 0;
			break;
		}

		if (strcmp(tmppath, "/") == 0)
			break;

		if ((cp = strrchr(tmppath, '/')) == NULL)
			break;

		/*
		 * The path has multiple components.
		 * Remove the last component and try again.
		 */
		*cp = '\0';
		if (tmppath[0] == '\0')
			(void) strcpy(tmppath, "/");

		cp = tmppath;
	}

	(void) fclose(fp);
	return (rc);
}

/*
 * smb_dlopen
 *
 * Check to see if an interposer library exists.  If it exists
 * and reports a valid version number and key (UUID), return
 * a handle to the library.  Otherwise, return NULL.
 */
void *
smb_dlopen(void)
{
	uuid_t uuid;
	void *interposer_hdl;
	typedef int (*smbex_versionfn_t)(smbex_version_t *);
	smbex_versionfn_t getversion;
	smbex_version_t *version;

	bzero(&uuid, sizeof (uuid_t));
	if (uuid_parse(SMBEX_KEY, uuid) < 0)
		return (NULL);

	interposer_hdl = dlopen(SMB_LIB_ALT, RTLD_NOW | RTLD_LOCAL);
	if (interposer_hdl == NULL)
		return (NULL);

	bzero(&getversion, sizeof (smbex_versionfn_t));
	getversion = (smbex_versionfn_t)dlsym(interposer_hdl,
	    "smbex_get_version");
	if ((getversion == NULL) ||
	    (version = malloc(sizeof (smbex_version_t))) == NULL) {
		(void) dlclose(interposer_hdl);
		return (NULL);
	}
	bzero(version, sizeof (smbex_version_t));

	if ((getversion(version) != 0) ||
	    (version->v_version != SMBEX_VERSION) ||
	    (uuid_compare(version->v_uuid, uuid) != 0)) {
		free(version);
		(void) dlclose(interposer_hdl);
		return (NULL);
	}

	free(version);
	return (interposer_hdl);
}

/*
 * smb_dlclose
 *
 * Closes handle to the interposed library.
 */
void
smb_dlclose(void *handle)
{
	if (handle)
		(void) dlclose(handle);
}

/*
 * This function is a wrapper for getnameinfo() to look up a hostname given an
 * IP address. The hostname returned by this function is used for constructing
 * the service principal name field of KRB AP-REQs. Hence, it should be
 * converted to lowercase for RFC 4120 section 6.2.1 conformance.
 */
int
smb_getnameinfo(smb_inaddr_t *ip, char *hostname, int hostlen, int flags)
{
	socklen_t salen;
	struct sockaddr_in6 sin6;
	struct sockaddr_in sin;
	void *sp;
	int rc;

	if (ip->a_family == AF_INET) {
		salen = sizeof (struct sockaddr_in);
		sin.sin_family = ip->a_family;
		sin.sin_port = 0;
		sin.sin_addr.s_addr = ip->a_ipv4;
		sp = &sin;
	} else {
		salen = sizeof (struct sockaddr_in6);
		sin6.sin6_family = ip->a_family;
		sin6.sin6_port = 0;
		(void) memcpy(&sin6.sin6_addr.s6_addr, &ip->a_ipv6,
		    sizeof (sin6.sin6_addr.s6_addr));
		sp = &sin6;
	}

	if ((rc = (getnameinfo((struct sockaddr *)sp, salen,
	    hostname, hostlen, NULL, 0, flags))) == 0)
		(void) smb_strlwr(hostname);

	return (rc);
}

/*
 * A share name is considered invalid if it contains control
 * characters or any of the following characters (MSDN 236388).
 *
 *	" / \ [ ] : | < > + ; , ? * =
 *
 * Control characters are defined as 0x00 - 0x1F inclusiv eand 0x7f.
 * Using SMB_ISCNTRL here to restrict test to these characters.
 * The behavior of iscntrl() is affected by the current locale
 * and may contain additional characters (ie 0x80-0x9f).
 */
uint32_t
smb_name_validate_share(const char *sharename)
{
	const char *invalid = "\"/\\[]:|<>+;,?*=";
	const char *p;

	if (sharename == NULL)
		return (ERROR_INVALID_PARAMETER);

	if (strlen(sharename) > SMB_SHARE_NTNAME_MAX)
		return (ERROR_INVALID_NAME);

	if (strpbrk(sharename, invalid) != NULL)
		return (ERROR_INVALID_NAME);

	for (p = sharename; *p != '\0'; p++) {
		if (SMB_ISCNTRL(*p))
			return (ERROR_INVALID_NAME);
	}

	return (ERROR_SUCCESS);
}

/*
 * User and group names are limited to 256 characters, cannot be terminated
 * by '.' and must not contain control characters or any of the following
 * characters.
 *
 *	" / \ [ ] < > + ; , ? * = @
 */
uint32_t
smb_name_validate_account(const char *name)
{
	const char	*invalid = "\"/\\[]<>+;,?*=@";
	const char	*p;
	int		len;

	if ((name == NULL) || (*name == '\0'))
		return (ERROR_INVALID_PARAMETER);

	len = strlen(name);
	if ((len > MAXNAMELEN) || (name[len - 1] == '.'))
		return (ERROR_INVALID_NAME);

	if (strpbrk(name, invalid) != NULL)
		return (ERROR_INVALID_NAME);

	for (p = name; *p != '\0'; p++) {
		if (iscntrl(*p))
			return (ERROR_INVALID_NAME);
	}

	return (ERROR_SUCCESS);
}

/*
 * Check a domain name for RFC 1035 and 1123 compliance.  Domain names may
 * contain alphanumeric characters, hyphens and dots.  The first and last
 * character of a label must be alphanumeric.  Interior characters may be
 * alphanumeric or hypens.
 *
 * Domain names should not contain underscores but we allow them because
 * Windows names are often in non-compliance with this rule.
 */
uint32_t
smb_name_validate_domain(const char *domain)
{
	boolean_t new_label = B_TRUE;
	const char *p;
	char label_terminator;

	if (domain == NULL)
		return (ERROR_INVALID_PARAMETER);

	if (*domain == '\0')
		return (ERROR_INVALID_NAME);

	label_terminator = *domain;

	for (p = domain; *p != '\0'; ++p) {
		if (new_label) {
			if (!isalnum(*p))
				return (ERROR_INVALID_NAME);
			new_label = B_FALSE;
			label_terminator = *p;
			continue;
		}

		if (*p == '.') {
			if (!isalnum(label_terminator))
				return (ERROR_INVALID_NAME);
			new_label = B_TRUE;
			label_terminator = *p;
			continue;
		}

		label_terminator = *p;

		if (isalnum(*p) || *p == '-' || *p == '_')
			continue;

		return (ERROR_INVALID_NAME);
	}

	if (!isalnum(label_terminator))
		return (ERROR_INVALID_NAME);

	return (ERROR_SUCCESS);
}

/*
 * A NetBIOS domain name can contain letters (a-zA-Z), numbers (0-9) and
 * hyphens.
 *
 * It cannot:
 * 	- be blank or longer than 15 chracters
 * 	- contain all numbers
 * 	- be the same as the computer name
 */
uint32_t
smb_name_validate_nbdomain(const char *name)
{
	char		netbiosname[NETBIOS_NAME_SZ];
	const char	*p;
	int		len;

	if (name == NULL)
		return (ERROR_INVALID_PARAMETER);

	len = strlen(name);
	if (len == 0 || len >= NETBIOS_NAME_SZ)
		return (ERROR_INVALID_NAME);

	if (strspn(name, "0123456789") == len)
		return (ERROR_INVALID_NAME);

	if (smb_getnetbiosname(netbiosname, NETBIOS_NAME_SZ) == 0) {
		if (smb_strcasecmp(name, netbiosname, 0) == 0)
			return (ERROR_INVALID_NAME);
	}

	for (p = name; *p != '\0'; ++p) {
		if (isalnum(*p) || *p == '-' || *p == '_')
			continue;

		return (ERROR_INVALID_NAME);
	}

	return (ERROR_SUCCESS);
}

/*
 * A workgroup name can contain 1 to 15 characters but cannot be the same
 * as the NetBIOS name.  The name must begin with a letter or number.
 *
 * The name cannot consist entirely of spaces or dots, which is covered
 * by the requirement that the name must begin with an alphanumeric
 * character.
 *
 * The name must not contain control characters or any of the following
 * characters.
 *
 *	" / \ [ ] : | < > + = ; , ?
 */
uint32_t
smb_name_validate_workgroup(const char *workgroup)
{
	char netbiosname[NETBIOS_NAME_SZ];
	const char *invalid = "\"/\\[]:|<>+=;,?";
	const char *p;

	if (workgroup == NULL)
		return (ERROR_INVALID_PARAMETER);

	if (*workgroup == '\0' || (!isalnum(*workgroup)))
		return (ERROR_INVALID_NAME);

	if (strlen(workgroup) >= NETBIOS_NAME_SZ)
		return (ERROR_INVALID_NAME);

	if (smb_getnetbiosname(netbiosname, NETBIOS_NAME_SZ) == 0) {
		if (smb_strcasecmp(workgroup, netbiosname, 0) == 0)
			return (ERROR_INVALID_NAME);
	}

	if (strpbrk(workgroup, invalid) != NULL)
		return (ERROR_INVALID_NAME);

	for (p = workgroup; *p != '\0'; p++) {
		if (iscntrl(*p))
			return (ERROR_INVALID_NAME);
	}

	return (ERROR_SUCCESS);
}

/*
 * Check for invalid characters in the given path.  The list of invalid
 * characters includes control characters and the following:
 *
 * " / \ [ ] : | < > + ; , ? * =
 *
 * Since this is checking a path not each component, '/' is accepted
 * as separator not an invalid character, except as the first character
 * since this is supposed to be a relative path.
 */
uint32_t
smb_name_validate_rpath(const char *relpath)
{
	char *invalid = "\"\\[]:|<>+;,?*=";
	char *cp;

	if ((relpath == NULL) || (*relpath == '\0') || (*relpath == '/'))
		return (ERROR_INVALID_NAME);

	if (strpbrk(relpath, invalid))
		return (ERROR_INVALID_NAME);

	for (cp = (char *)relpath; *cp != '\0'; cp++) {
		if (iscntrl(*cp))
			return (ERROR_INVALID_NAME);
	}

	return (ERROR_SUCCESS);
}

/*
 * Parse a string to obtain the account and domain names as separate strings.
 *
 * Names containing a backslash ('\') are known as qualified or composite
 * names.  The string preceding the backslash should be the domain name
 * and the string following the slash should be a name within that domain.
 *
 * Names that do not contain a backslash are known as isolated names.
 * An isolated name may be a single label, such as john, or may be in
 * user principal name (UPN) form, such as john@example.com.
 *
 *	domain\name
 *	domain/name
 *	name
 *	name@domain
 *
 * If we encounter any of the forms above in arg, the @, / or \ separator
 * is replaced by \0 and the name and domain pointers are set to point to
 * the appropriate components in arg.  Otherwise, name and domain pointers
 * will be set to NULL.
 */
void
smb_name_parse(char *arg, char **account, char **domain)
{
	char *p;

	*account = NULL;
	*domain = NULL;

	if ((p = strpbrk(arg, "/\\@")) != NULL) {
		if (*p == '@') {
			*p = '\0';
			++p;
			*domain = p;
			*account = arg;
		} else {
			*p = '\0';
			++p;
			*account = p;
			*domain = arg;
		}
	}
}

/*
 * The txid is an arbitrary transaction.  A new txid is returned on each call.
 *
 * 0 or -1 are not assigned so that they can be used to detect
 * invalid conditions.
 */
uint32_t
smb_get_txid(void)
{
	static mutex_t	txmutex;
	static uint32_t	txid;
	uint32_t	txid_ret;

	(void) mutex_lock(&txmutex);

	if (txid == 0)
		txid = time(NULL);

	do {
		++txid;
	} while (txid == 0 || txid == (uint32_t)-1);

	txid_ret = txid;
	(void) mutex_unlock(&txmutex);

	return (txid_ret);
}

/*
 *  Creates a log object and inserts it into a list of logs.
 */
smb_log_hdl_t
smb_log_create(int max_cnt, char *name)
{
	smb_loglist_item_t *log_node;
	smb_log_t *log = NULL;
	smb_log_hdl_t handle = 0;

	if (max_cnt <= 0 || name == NULL)
		return (0);

	(void) mutex_lock(&smb_loglist.ll_mtx);

	log_node = malloc(sizeof (smb_loglist_item_t));

	if (log_node != NULL) {
		log = &log_node->lli_log;

		bzero(log, sizeof (smb_log_t));

		handle = log->l_handle = smb_get_txid();
		log->l_max_cnt = max_cnt;
		(void) snprintf(log->l_file, sizeof (log->l_file),
		    SMB_LOG_FILE_FMT, name);

		list_create(&log->l_list, sizeof (smb_log_item_t),
		    offsetof(smb_log_item_t, li_lnd));

		if (smb_loglist.ll_list.list_size == 0)
			list_create(&smb_loglist.ll_list,
			    sizeof (smb_loglist_item_t),
			    offsetof(smb_loglist_item_t, lli_lnd));

		list_insert_tail(&smb_loglist.ll_list, log_node);
	}

	(void) mutex_unlock(&smb_loglist.ll_mtx);

	return (handle);
}

/*
 * Keep the most recent log entries, based on max count.
 * If the priority is LOG_ERR or higher then the entire log is
 * dumped to a file.
 *
 * The date format for each message is the same as a syslog entry.
 *
 * The log is also added to syslog via smb_log_trace().
 */
void
smb_log(smb_log_hdl_t hdl, int priority, const char *fmt, ...)
{
	va_list		ap;
	smb_log_t	*log;
	smb_log_item_t	*msg;
	time_t		now;
	struct		tm *tm;
	char		timebuf[SMB_TIMEBUF_SZ];
	char		buf[SMB_LOG_LINE_SZ];
	char		netbiosname[NETBIOS_NAME_SZ];
	char		*pri_name;
	int		i;

	va_start(ap, fmt);
	(void) vsnprintf(buf, SMB_LOG_LINE_SZ, fmt, ap);
	va_end(ap);

	priority &= LOG_PRIMASK;
	smb_log_trace(priority, buf);

	if ((log = smb_log_get(hdl)) == NULL)
		return;

	(void) mutex_lock(&log->l_mtx);

	(void) time(&now);
	tm = localtime(&now);
	(void) strftime(timebuf, SMB_TIMEBUF_SZ, "%b %d %H:%M:%S", tm);

	if (smb_getnetbiosname(netbiosname, NETBIOS_NAME_SZ) != 0)
		(void) strlcpy(netbiosname, "unknown", NETBIOS_NAME_SZ);

	if (log->l_cnt == log->l_max_cnt) {
		msg = list_head(&log->l_list);
		list_remove(&log->l_list, msg);
	} else {
		if ((msg = malloc(sizeof (smb_log_item_t))) == NULL) {
			(void) mutex_unlock(&log->l_mtx);
			return;
		}
		log->l_cnt++;
	}

	pri_name = "info";
	for (i = 0; i < sizeof (smb_log_pri) / sizeof (smb_log_pri[0]); i++) {
		if (priority == smb_log_pri[i].lp_value) {
			pri_name = smb_log_pri[i].lp_name;
			break;
		}
	}

	(void) snprintf(msg->li_msg, SMB_LOG_LINE_SZ,
	    "%s %s smb[%d]: [ID 0 daemon.%s] %s",
	    timebuf, netbiosname, getpid(), pri_name, buf);
	list_insert_tail(&log->l_list, msg);

	if (priority <= LOG_ERR)
		smb_log_dump(log);

	(void) mutex_unlock(&log->l_mtx);
}

/*
 * Dumps all the logs in the log list.
 */
void
smb_log_dumpall()
{
	smb_loglist_item_t *log_node;

	(void) mutex_lock(&smb_loglist.ll_mtx);

	log_node = list_head(&smb_loglist.ll_list);

	while (log_node != NULL) {
		smb_log_dump(&log_node->lli_log);
		log_node = list_next(&smb_loglist.ll_list, log_node);
	}

	(void) mutex_unlock(&smb_loglist.ll_mtx);
}

static void
smb_log_trace(int priority, const char *s)
{
	syslog(priority, "%s", s);
}

static smb_log_t *
smb_log_get(smb_log_hdl_t hdl)
{
	smb_loglist_item_t *log_node;
	smb_log_t *log;

	(void) mutex_lock(&smb_loglist.ll_mtx);

	log_node = list_head(&smb_loglist.ll_list);

	while (log_node != NULL) {
		if (log_node->lli_log.l_handle == hdl) {
			log = &log_node->lli_log;
			(void) mutex_unlock(&smb_loglist.ll_mtx);
			return (log);
		}
		log_node = list_next(&smb_loglist.ll_list, log_node);
	}

	(void) mutex_unlock(&smb_loglist.ll_mtx);
	return (NULL);
}

/*
 * Dumps the log to a file.
 */
static void
smb_log_dump(smb_log_t *log)
{
	smb_log_item_t *msg;
	FILE *fp;

	if ((fp = fopen(log->l_file, "w")) == NULL)
		return;

	msg = list_head(&log->l_list);

	while (msg != NULL) {
		(void) fprintf(fp, "%s\n", msg->li_msg);
		msg = list_next(&log->l_list, msg);
	}

	(void) fclose(fp);
}

/*
 * Creates a lock file and grabs an exclusive (write) lock on it.
 */
int
smb_file_lock(char *lock_file, smb_lockinfo_t *lockinfo)
{
	int seconds = 0;

	(void) mutex_lock(&lockinfo->l_mtx);
	for (;;) {
		if (lockinfo->l_pid != 0 && lockinfo->l_pid != getpid()) {
			/* somebody forked */
			lockinfo->l_pid = 0;
			lockinfo->l_tid = 0;
		}

		if (lockinfo->l_tid == 0) {
			if ((lockinfo->l_fildes = creat(lock_file, 0600)) == -1)
				break;
			lockinfo->l_flock.l_type = F_WRLCK;
			if (fcntl(lockinfo->l_fildes,
			    F_SETLK, &lockinfo->l_flock) != -1) {
				lockinfo->l_pid = getpid();
				lockinfo->l_tid = thr_self();
				(void) mutex_unlock(&lockinfo->l_mtx);
				return (0);
			}
			(void) close(lockinfo->l_fildes);
			lockinfo->l_fildes = -1;
		}

		if (seconds++ >= SMB_FLOCK_WAIT) {
			/*
			 * For compatibility with the past, pretend
			 * that we were interrupted by SIGALRM.
			 */
			errno = EINTR;
			break;
		}

		(void) mutex_unlock(&lockinfo->l_mtx);
		(void) sleep(1);
		(void) mutex_lock(&lockinfo->l_mtx);
	}
	(void) mutex_unlock(&lockinfo->l_mtx);

	return (-1);
}

/*
 * Unlocks file for operations done via this library APIs.
 */
int
smb_file_unlock(smb_lockinfo_t *lockinfo)
{
	(void) mutex_lock(&lockinfo->l_mtx);
	if (lockinfo->l_tid == thr_self() && lockinfo->l_fildes >= 0) {
		lockinfo->l_flock.l_type = F_UNLCK;
		(void) fcntl(lockinfo->l_fildes, F_SETLK, &lockinfo->l_flock);
		(void) close(lockinfo->l_fildes);
		lockinfo->l_fildes = -1;
		lockinfo->l_pid = 0;
		lockinfo->l_tid = 0;
		(void) mutex_unlock(&lockinfo->l_mtx);
		return (0);
	}
	(void) mutex_unlock(&lockinfo->l_mtx);
	return (-1);
}
