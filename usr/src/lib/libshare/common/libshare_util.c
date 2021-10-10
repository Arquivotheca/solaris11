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

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <fcntl.h>
#include <strings.h>
#include <syslog.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <libnvpair.h>
#include <libintl.h>
#include <zone.h>
#include <note.h>
#include <iconv.h>
#include <langinfo.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/varargs.h>

#include <sharefs/share.h>
#include <sharefs/sharetab.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>

#include "libshare.h"
#include "libshare_impl.h"


#define	CONV_TO_UTF8	0
#define	CONV_FROM_UTF8	1

static int conv_utf8_common(int, const char *, char **);

/*
 * non-shareable file system types. Add new types here.
 */
static char *sa_fstype_table[] = {
	MNTTYPE_AUTOFS,
	MNTTYPE_NFS,
	MNTTYPE_DEV,
	MNTTYPE_MNTFS,
	MNTTYPE_SHAREFS,
	MNTTYPE_OBJFS,
	MNTTYPE_SMBFS,
	MNTTYPE_CTFS,
	MNTTYPE_SWAP,
	"devfs",
	"fd",
	"proc",
	"fifofs",
	"namefs",
	"sockfs",
	"specfs",
	"dcfs",
	NULL
};

/*
 * sa_fstype(path)
 *
 * Given path, return the string representing the path's file system
 * type. This is used to discover ZFS shares.
 */
int
sa_fstype(const char *path, char **result)
{
	int err;
	struct stat st;

	err = stat(path, &st);
	if (err < 0) {
		switch (errno) {
		case ENOENT:
		case ENOTDIR:
			/*
			 * Distinquish between path not found and
			 * other errors since we want to know this in
			 * order to remove a share after it has been
			 * removed (deleted) from the system without
			 * unsharing.
			 */
			err = SA_PATH_NOT_FOUND;
			break;
		case EACCES:
			err = SA_NO_PERMISSION;
			break;
		default:
			err = SA_SYSTEM_ERR;
			break;
		}
	} else {
		err = SA_OK;
	}
	/*
	 * If we have a valid path at this point, return the fstype
	 * via result (if not NULL)
	 */
	if (result != NULL && err == SA_OK) {
		if ((*result = strdup(st.st_fstype)) == NULL)
			err = SA_NO_MEMORY;
	}

	return (err);
}

void
sa_free_fstype(char *type)
{
	free(type);
}

/*
 * sa_fixup_path
 *
 * Normalize a string by reducing all the repeated slash characters in
 * path and removing all trailing slashes.
 *
 *	char *buf = strdup("//d1//d2//d3///d4////");
 *	sa_fixup_path(buf);
 *
 * Would result in path containing the following string:
 *
 *		/d1/d2/d3/d4
 *
 * If path contains all slashes, then remove all but first /.
 *
 * This function modifies the contents of path in place and returns
 * a pointer to path.
 */
char *
sa_fixup_path(char *path)
{
	char *p, *q;

	if (path == NULL || *path == '\0')
		return (path);

	/* reduce all repeating slashes in path */
	p = q = path;
	while (*p) {
		*q++ = *p;
		if (*p == '/')
			while (*p == '/')
				++p;
		else
			++p;
	}
	*q = '\0';

	/* now remove any trailing slash */
	p = path + strlen(path) - 1;
	if (p != path && *p == '/')
		*p = '\0';

	return (path);
}

static int
sa_check_sh_list(char *name, nvlist_t **sh_list, int cnt)
{
	int i;
	nvpair_t *nvp;
	char *sh_name;

	for (i = 0; i < cnt; ++i) {
		if (sh_list[i] == NULL || nvlist_empty(sh_list[i]))
			continue;

		for (nvp = nvlist_next_nvpair(sh_list[i], NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(sh_list[i], nvp)) {
			sh_name = nvpair_name(nvp);

			if (strcmp(name, sh_name) == 0)
				return (SA_DUPLICATE_NAME);
		}
	}

	return (SA_OK);
}

/*
 * sa_resolve_share_name_conflict
 *
 * This share has a name conflict with an existing share.
 * Adjust the name by adding a number to the end of the name (~<cnt>)
 * incrementing until a unique name is found.
 *
 * If a unique name is found, update the share and write to disk.
 */
int
sa_resolve_share_name_conflict(nvlist_t *share, nvlist_t **sh_list, int nlist)
{
	char *sh_name;
	char *sh_path;
	char *old_name;
	int cnt = 0;
	int len;
	int rc;
	sa_proto_t p, proto = SA_PROT_NONE;
	char new_name[SA_MAX_SHARE_NAME+1];
	char errbuf[512];
	acl_t *sh_acl = NULL;
	boolean_t acl_valid = B_FALSE;

	if ((sh_name = sa_share_get_name(share)) == NULL)
		return (SA_NO_SHARE_NAME);
	if ((sh_path = sa_share_get_path(share)) == NULL)
		return (SA_NO_SHARE_PATH);
	if ((old_name = strdup(sh_name)) == NULL)
		return (SA_NO_MEMORY);

	for (p = sa_proto_first(); p != SA_PROT_NONE; p = sa_proto_next(p)) {
		if (sa_share_get_proto(share, p))
			proto |= p;
	}

	len = strlen(old_name);
	do {
		++cnt;
		while (snprintf(new_name, sizeof (new_name), "%s~%d",
		    old_name, cnt) > SA_MAX_SHARE_NAME) {
			if (len == 0) {
				free(old_name);
				return (SA_INVALID_SHARE_NAME);
			}
			old_name[len - 1] = '\0';
			len--;
		}
		rc = sa_share_validate_name(new_name, sh_path, B_TRUE, proto,
		    errbuf, sizeof (errbuf));

		if (rc == SA_OK)
			rc = sa_check_sh_list(new_name, sh_list, nlist);

	} while (rc == SA_DUPLICATE_NAME);
	free(old_name);

	if (rc != SA_OK)
		return (rc);

	if ((old_name = strdup(sh_name)) == NULL)
		return (SA_NO_MEMORY);

	if ((rc = sa_share_set_name(share, new_name)) != SA_OK) {
		free(old_name);
		return (rc);
	}

	if (sa_share_get_acl(old_name, sh_path, &sh_acl) == SA_OK)
		acl_valid = B_TRUE;

	if ((rc = safs_share_remove(old_name, sh_path)) == SA_OK &&
	    (rc = safs_share_write(share)) == SA_OK) {
		if (acl_valid) {
			rc = sa_share_set_acl(new_name, sh_path, sh_acl);
			if (rc == SA_NOT_SUPPORTED)
				rc = SA_OK;
			if (rc != SA_OK)
				rc = SA_ACL_SET_ERROR;
		}
	}

	if (acl_valid)
		acl_free(sh_acl);
	free(old_name);
	return (rc);
}

/*
 * sa_name_adjust(path, count)
 *
 * Add a ~<count> in place of last few characters. The total number of
 * characters is dependent on count.
 */
static int
sa_name_adjust(char *path, int count)
{
	size_t len;

	len = strlen(path) - 2;
	if (count > 10)
		len--;
	if (count > 100)
		len--;
	if (count > 1000)
		len--;
	if (len > 0)
		(void) sprintf(path + len, "~%d", count);
	else
		return (SA_INVALID_SHARE_NAME);

	return (SA_OK);
}

/*
 * sa_path_to_shr_name(path)
 *
 * change all illegal characters to something else.  For now, all get
 * converted to '_' and the leading and trailing '/' are stripped off.
 * This is used to construct a valid share name from a path.
 *
 * The list of invalid characters includes control characters
 * and the following:
 *
 *	" / \ [ ] : | < > + ; , ? * =
 *
 * Control characters are defined as 0x00 - 0x1F inclusive and 0x7f.
 * Using specific test here instead of iscntrl macro because the
 * behavior of iscntrl() is affected by the current locale and may
 * contain additional characters (ie 0x80-0x9f).
 *
 * Caller must pass a valid path.
 */
void
sa_path_to_shr_name(char *path)
{
	char *invalid = "\"/\\[]:|<>+;,?*=";
	char *p = path;
	char *q;
	size_t len;

	assert(path != NULL);
	assert(*path == '/');

	/*
	 * Strip leading and trailing /'s.
	 */
	p += strspn(p, "/");
	q = strchr(p, '\0');
	if (q != NULL && q != path) {
		while ((--q, *q == '/'))
			*q = '\0';
	}

	if (*p == '\0') {
		(void) strcpy(path, "_");
		return;
	}

	/*
	 * Stride over path components until the remaining
	 * path is no longer than SA_MAX_SHARE_NAME.
	 */
	q = p;
	while ((q != NULL) && (strlen(q) > SA_MAX_SHARE_NAME)) {
		if ((q = strchr(q, '/')) != NULL) {
			++q;
			p = q;
		}
	}

	/*
	 * If the path is still longer than SA_MAX_SHARE_NAME,
	 * take the trailing SA_MAX_SHARE_NAME characters.
	 */
	if ((len = strlen(p)) > SA_MAX_SHARE_NAME) {
		len = SA_MAX_SHARE_NAME;
		p = strchr(p, '\0') - (SA_MAX_SHARE_NAME - 1);
	}

	(void) memmove(path, p, len);
	path[len] = '\0';

	/*
	 * convert any illegal characters to underscore
	 */
	for (p = path; *p != '\0'; ++p) {
		if (strchr(invalid, *p) || (*p == 0x7f) ||
		    ((*p >= 0) && (*p <= 0x1f)))
			*p = '_';
	}
}

/*
 * sa_share_from_path
 *
 * This routine will create a default share name from the given path.
 * It will either return a new share with name and path set or
 * an existing share with the default share name for the specified path.
 */
int
sa_share_from_path(const char *sh_path, nvlist_t **share, boolean_t *new)
{
	int rc;
	int count;
	char *sh_name;
	char *path;
	nvlist_t *shr = NULL;

	if ((sh_name = strdup(sh_path)) == NULL)
		return (SA_NO_MEMORY);

	assert(share != NULL);

	*new = B_FALSE;
	sa_path_to_shr_name(sh_name);
	count = 0;
	do {
		/*
		 * Check for the existance of a share with the new name.
		 * If a share exists, is it for this path?
		 * If so return existing share else
		 * try to fixup the name and search again.
		 * If no share is found, create a new share and set the
		 * name and path properties.
		 */
		rc = sa_share_read(sh_path, sh_name, &shr);
		if (rc == SA_OK) {
			path = sa_share_get_path(shr);
			if (strcmp(sh_path, path) != 0) {
				/*
				 * this share is for a different path
				 * try to fix up name and try again
				 */
				sa_share_free(shr);
				shr = NULL;
				rc = sa_name_adjust(sh_name, count);
				count++;
				if (rc != SA_OK || count >= MAX_MANGLE_NUMBER) {
					free(sh_name);
					return (SA_INVALID_SHARE_NAME);
				}
			}
		} else {
			/*
			 * share does not exist, create new one
			 */
			shr = sa_share_alloc(sh_name, sh_path);
			if (shr == NULL) {
				free(sh_name);
				return (SA_NO_MEMORY);
			}
			*new = B_TRUE;
		}
	} while (shr == NULL);

	free(sh_name);

	*share = shr;

	return (SA_OK);

}

/*
 * sa_prop_cmp_list
 *
 * This function tries to find the null-terminated string key in
 * the string vector plist. The string vector must be terminated
 * with a NULL pointer.
 *
 * If a match is found, the vector index is returned. If no match
 * is found, -1 is returned.
 *
 */
int
sa_prop_cmp_list(const char *key, char *const *plist)
{
	int i;

	if (key == NULL || plist == NULL)
		return (-1);

	for (i = 0; plist[i] != NULL; ++i) {
		if (strcmp(key, plist[i]) == 0)
			return (i);
	}

	return (-1);
}

void
sa_trace(const char *s)
{
	NOTE(ARGUNUSED(s))
	/* syslog(LOG_DEBUG, "%s", s); */
}

void
sa_tracef(const char *fmt, ...)
{
	va_list ap;
	char buf[128];

	va_start(ap, fmt);
	(void) vsnprintf(buf, 128, fmt, ap);
	va_end(ap);

	sa_trace(buf);
}

char *
sa_strerror(int err)
{
	switch (err) {
	case SA_OK:
		return (dgettext(TEXT_DOMAIN, "ok"));
	case SA_INTERNAL_ERR:
		return (dgettext(TEXT_DOMAIN, "internal error"));
	case SA_SYSTEM_ERR:
		return (dgettext(TEXT_DOMAIN, "system error"));
	case SA_NO_MEMORY:
		return (dgettext(TEXT_DOMAIN, "no memory"));
	case SA_SYNTAX_ERR:
		return (dgettext(TEXT_DOMAIN, "syntax error"));
	case SA_NOT_IMPLEMENTED:
		return (dgettext(TEXT_DOMAIN, "operation not implemented"));
	case SA_NOT_SUPPORTED:
		return (dgettext(TEXT_DOMAIN, "operation not supported"));
	case SA_BUSY:
		return (dgettext(TEXT_DOMAIN, "service is busy"));
	case SA_CONFIG_ERR:
		return (dgettext(TEXT_DOMAIN, "configuration error"));
	case SA_SHARE_NOT_FOUND:
		return (dgettext(TEXT_DOMAIN, "share not found"));
	case SA_DUPLICATE_NAME:
		return (dgettext(TEXT_DOMAIN, "share name exists"));
	case SA_DUPLICATE_PATH:
		return (dgettext(TEXT_DOMAIN, "multiple shares with "
		    "same path exist"));
	case SA_DUPLICATE_PROP:
		return (dgettext(TEXT_DOMAIN,
		    "property specified more than once"));
	case SA_DUPLICATE_PROTO:
		return (dgettext(TEXT_DOMAIN,
		    "protocol specified more than once"));
	case SA_NO_SHARE_NAME:
		return (dgettext(TEXT_DOMAIN, "missing share name"));
	case SA_NO_SHARE_PATH:
		return (dgettext(TEXT_DOMAIN, "missing share path"));
	case SA_NO_SHARE_DESC:
		return (dgettext(TEXT_DOMAIN, "missing share description"));
	case SA_NO_SHARE_PROTO:
		return (dgettext(TEXT_DOMAIN, "missing share protocol"));
	case SA_NO_SECTION:
		return (dgettext(TEXT_DOMAIN, "missing section name"));
	case SA_NO_SUCH_PROTO:
		return (dgettext(TEXT_DOMAIN,
		    "share not configured for protocol"));
	case SA_NO_SUCH_PROP:
		return (dgettext(TEXT_DOMAIN, "property not found"));
	case SA_NO_SUCH_SECURITY:
		return (dgettext(TEXT_DOMAIN, "security mode not found"));
	case SA_NO_SUCH_SECTION:
		return (dgettext(TEXT_DOMAIN, "section not found"));
	case SA_NO_PERMISSION:
		return (dgettext(TEXT_DOMAIN, "permission denied"));
	case SA_INVALID_SHARE:
		return (dgettext(TEXT_DOMAIN, "invalid share"));
	case SA_INVALID_SHARE_NAME:
		return (dgettext(TEXT_DOMAIN, "invalid share name"));
	case SA_INVALID_SHARE_PATH:
		return (dgettext(TEXT_DOMAIN, "invalid share path"));
	case SA_INVALID_SHARE_MNTPNT:
		return (dgettext(TEXT_DOMAIN, "invalid share mntpnt"));
	case SA_INVALID_PROP:
		return (dgettext(TEXT_DOMAIN, "invalid property"));
	case SA_INVALID_SMB_PROP:
		return (dgettext(TEXT_DOMAIN, "invalid smb property"));
	case SA_INVALID_NFS_PROP:
		return (dgettext(TEXT_DOMAIN, "invalid nfs property"));
	case SA_INVALID_PROP_VAL:
		return (dgettext(TEXT_DOMAIN, "invalid property value"));
	case SA_INVALID_PROTO:
		return (dgettext(TEXT_DOMAIN, "invalid protocol specified"));
	case SA_INVALID_SECURITY:
		return (dgettext(TEXT_DOMAIN, "invalid security mode"));
	case SA_INVALID_UNAME:
		return (dgettext(TEXT_DOMAIN, "invalid username"));
	case SA_INVALID_UID:
		return (dgettext(TEXT_DOMAIN, "invalid uid"));
	case SA_INVALID_FNAME:
		return (dgettext(TEXT_DOMAIN, "invalid filename"));
	case SA_PARTIAL_PUBLISH:
		return (dgettext(TEXT_DOMAIN, "partial dataset publish"));
	case SA_PARTIAL_UNPUBLISH:
		return (dgettext(TEXT_DOMAIN, "partial dataset unpublish"));
	case SA_INVALID_READ_HDL:
		return (dgettext(TEXT_DOMAIN, "invalid read handle"));
	case SA_INVALID_PLUGIN:
		return (dgettext(TEXT_DOMAIN, "invalid plugin"));
	case SA_INVALID_PLUGIN_TYPE:
		return (dgettext(TEXT_DOMAIN, "invalid plugin type"));
	case SA_INVALID_PLUGIN_OPS:
		return (dgettext(TEXT_DOMAIN, "invalid plugin ops"));
	case SA_INVALID_PLUGIN_NAME:
		return (dgettext(TEXT_DOMAIN, "invalid plugin name"));
	case SA_NO_PLUGIN_DIR:
		return (dgettext(TEXT_DOMAIN, "no plugin directory"));
	case SA_NO_SHARE_DIR:
		return (dgettext(TEXT_DOMAIN, "no share directory"));
	case SA_PATH_NOT_FOUND:
		return (dgettext(TEXT_DOMAIN, "share path not found"));
	case SA_MNTPNT_NOT_FOUND:
		return (dgettext(TEXT_DOMAIN, "mountpoint not found"));
	case SA_NOT_SHARED_PROTO:
		return (dgettext(TEXT_DOMAIN, "not shared for protocol"));
	case SA_ANCESTOR_SHARED:
		return (dgettext(TEXT_DOMAIN, "ancestor of path is shared"));
	case SA_DESCENDANT_SHARED:
		return (dgettext(TEXT_DOMAIN, "descendant of path is shared"));
	case SA_XDR_ENCODE_ERR:
		return (dgettext(TEXT_DOMAIN, "XDR encode error"));
	case SA_XDR_DECODE_ERR:
		return (dgettext(TEXT_DOMAIN, "XDR decode error"));
	case SA_PASSWORD_ENC:
		return (dgettext(TEXT_DOMAIN, "passwords must be encrypted"));
	case SA_SCF_ERROR:
		return (dgettext(TEXT_DOMAIN, "service configuration facility "
		    "error"));
	case SA_DOOR_ERROR:
		return (dgettext(TEXT_DOMAIN, "shared door call failed"));
	case SA_STALE_HANDLE:
		return (dgettext(TEXT_DOMAIN, "stale handle"));
	case SA_INVALID_ACCLIST_PROP_VAL:
		return (dgettext(TEXT_DOMAIN, "invalid access list property "
		    "value"));
	case SA_SHARE_OTHERZONE:
		return (dgettext(TEXT_DOMAIN, (getzoneid() == GLOBAL_ZONEID ?
		    "shares managed from non-global zone." :
		    "shares managed from global zone.")));
	case SA_INVALID_ZONE:
		return (dgettext(TEXT_DOMAIN, "not supported in non-global "
		    "zone"));
	case SA_PROTO_NOT_INSTALLED:
		return (dgettext(TEXT_DOMAIN, "protocol not installed"));
	case SA_INVALID_FSTYPE:
		return (dgettext(TEXT_DOMAIN, "invalid file system type"));
	case SA_READ_ONLY:
		return (dgettext(TEXT_DOMAIN, "read only file system"));
	case SA_LOCALE_NOT_SUPPORTED:
		return (dgettext(TEXT_DOMAIN, "locale not supported"));
	case SA_ACL_SET_ERROR:
		return (dgettext(TEXT_DOMAIN, "error setting share ACL"));
	default:
		return (dgettext(TEXT_DOMAIN, "unknown error"));
	}
}

static void
salog_message(int pri, int err, const char *fmt, va_list ap)
{
	char errbuf[1024];

	(void) vsnprintf(errbuf, sizeof (errbuf), fmt, ap);
	va_end(ap);

	if (err == 0)
		syslog(pri, "%s", errbuf);
	else
		syslog(pri, "%s: %s", errbuf, sa_strerror(err));
}

void
salog_error(int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	salog_message(LOG_ERR, err, fmt, ap);
	va_end(ap);
}

void
salog_notice(int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	salog_message(LOG_NOTICE, err, fmt, ap);
	va_end(ap);
}

void
salog_info(int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	salog_message(LOG_INFO, err, fmt, ap);
	va_end(ap);
}

void
salog_debug(int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	salog_message(LOG_DEBUG, err, fmt, ap);
	va_end(ap);
}

boolean_t
sa_fstype_is_shareable(const char *fstype)
{
	int i;

	for (i = 0; sa_fstype_table[i] != NULL; i++)
		if (strcmp(fstype, sa_fstype_table[i]) == 0)
			return (B_FALSE);
	return (B_TRUE);
}

static int
sa_mntpnt_is_zoned(const char *mntpnt)
{
	return (safs_is_zoned(mntpnt));
}

static int
sa_get_sharing_zone(char *mntopts)
{
	char *opts;
	char *namep;
	char *valp;
	char *nextp;
	zoneid_t zone_ctx = GLOBAL_ZONEID;

	if ((opts = strdup(mntopts)) == NULL)
		return (GLOBAL_ZONEID);

	if ((namep = strstr(opts, MNTOPT_SHRZONE"=")) != NULL) {
		if ((valp = strchr(namep, '=')) != NULL) {
			++valp;
			if ((nextp = strchr(valp, ',')) != NULL)
				*nextp = '\0';
			zone_ctx = atoi(valp);
		}
	}

	free(opts);
	return (zone_ctx);
}

/*
 * sa_mntpnt_in_current_zone
 *
 * This routine determines if the mntpnt is shareable in
 * the current zone. Each mnttab entry has an mountpoint
 * option (sharezone) that specifies the zoneid it is mounted in.
 *
 * Currently there are a couple of exceptions. If the mount option
 * does not exist, the global zone is assumed. Also any dataset with
 * the 'zoned' property set to 'on' cannot be shared in GZ.
 */
boolean_t
sa_mntpnt_in_current_zone(char *mntpnt, char *mntopts)
{
	zoneid_t shr_zone;
	zoneid_t cur_zone = getzoneid();

	if (mntpnt == NULL || mntopts == NULL)
		return (B_FALSE);

	shr_zone = sa_get_sharing_zone(mntopts);
	if (cur_zone == GLOBAL_ZONEID) {

		if (shr_zone != GLOBAL_ZONEID)
			return (B_FALSE);

		if (sa_mntpnt_is_zoned(mntpnt))
			return (B_FALSE);

		return (B_TRUE);
	} else {
		if (cur_zone != shr_zone)
			return (B_FALSE);

		return (B_TRUE);
	}
}

/*
 * Equivalent to the strchr(3C) function except it honors the back
 * slash escape convention.
 */
char *
sa_strchr_escape(char *string, char c)
{
	char *curp;

	if (string == NULL)
		return (NULL);

	for (curp = string; *curp != '\0'; curp++) {
		if (*curp == c)
			break;
		else if (*curp == '\\')
			curp++;

		/* make sure we don't go off the end of the string */
		if (*curp == '\0')
			break;
	}
	if (*curp == '\0')
		curp = NULL;
	return (curp);
}

/*
 * Strip back slash escape from string if there are any. Always
 * returns new string unless memory failure.
 */
char *
sa_strip_escape(char *string)
{
	char *curp;
	char *newstring;
	char *newcp;

	if (string == NULL)
		return (NULL);

	newcp = newstring = calloc(strlen(string) + 1, sizeof (char));
	if (newcp == NULL)
		return (NULL);

	curp = string;
	while (*curp != '\0') {
		if (*curp == '\\') {
			curp++;
			/* make sure we don't go off the end of the string */
			if (*curp == '\0')
				break;
		}
		*newcp++ = *curp++;
	}
	return (newstring);
}

/*
 * Convert the input string to utf8 from the current locale.
 * If the current locale codeset cannot be determined, use the
 * "C" locale.
 * On success, the converted string is stored in the newly
 * allocated output buffer and the pointer is returned in 'outbufp'
 * It is the responsibilty of the caller to free this memory.
 */
int
sa_locale_to_utf8(const char *inbuf, char **outbufp)
{
	return (conv_utf8_common(CONV_TO_UTF8, inbuf, outbufp));
}

/*
 * Convert the input string from utf8 to current locale.
 * If the current locale codeset cannot be determined, use the
 * "C" locale.
 * On success, the converted string is stored in the newly
 * allocated output buffer and the pointer is returned in 'outbufp'
 * It is the responsibilty of the caller to free this memory.
 */
int
sa_utf8_to_locale(const char *inbuf, char **outbufp)
{
	return (conv_utf8_common(CONV_FROM_UTF8, inbuf, outbufp));
}

static int
conv_utf8_common(int direction, const char *inbuf, char **outbufp)
{
	iconv_t cd;
	char *curlocale;
	char *inptr = (char *)inbuf;
	char *outptr;
	size_t outbytesleft;
	size_t inbytesleft;
	size_t ret;

	if (outbufp == NULL)
		return (SA_INTERNAL_ERR);

	curlocale = nl_langinfo(CODESET);
	if (curlocale == NULL || *curlocale == '\0')
		curlocale = "646";

	if (direction == CONV_TO_UTF8)
		cd = iconv_open("UTF-8", curlocale);
	else
		cd = iconv_open(curlocale, "UTF-8");

	if (cd == (iconv_t)-1) {
		switch (errno) {
		case EINVAL:
			return (SA_LOCALE_NOT_SUPPORTED);
		case ENOMEM:
			return (SA_NO_MEMORY);
		default:
			return (SA_SYSTEM_ERR);
		}
	}

	inbytesleft = strlen(inbuf);
	/* Assume worst case of characters expanding to 4 bytes. */
	outbytesleft = inbytesleft * 4 + 1;
	*outbufp = calloc(outbytesleft, 1);
	if (*outbufp == NULL) {
		(void) iconv_close(cd);
		return (SA_NO_MEMORY);
	}

	outptr = *outbufp;
	ret = iconv(cd, &inptr, &inbytesleft, &outptr, &outbytesleft);
	(void) iconv_close(cd);

	if (ret == (size_t)-1 || inbytesleft != 0) {
		free(*outbufp);
		*outbufp = NULL;
		return (SA_SYSTEM_ERR);
	}

	return (SA_OK);
}

uint32_t
sa_protocol_valid(char *protocol)
{
	sa_proto_t proto;
	char *status;

	if (protocol == NULL)
		return (SA_INVALID_PROTO);

	if ((proto = sa_val_to_proto(protocol)) == SA_PROT_NONE)
		return (SA_INVALID_PROTO);

	status = sa_proto_get_status(proto);
	if (status == NULL)
		return (SA_PROTO_NOT_INSTALLED);
	free(status);

	return (SA_OK);
}
