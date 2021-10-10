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

#include <sys/param.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pwd.h>
#include <assert.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/idmap.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libntsvcs.h>
#include <smbsrv/smbinfo.h>

#define	SMB_AUTOHOME_KEYSIZ	128
#define	SMB_AUTOHOME_MAXARG	3
#define	SMB_AUTOHOME_BUFSIZ	2048

typedef struct smb_autohome_info {
	struct smb_autohome_info *magic1;
	FILE *fp;
	smb_autohome_t autohome;
	char buf[SMB_AUTOHOME_BUFSIZ];
	char *argv[SMB_AUTOHOME_MAXARG];
	int lineno;
	struct smb_autohome_info *magic2;
} smb_autohome_info_t;

static smb_autohome_info_t smb_ai;

static smb_autohome_t *smb_autohome_make_entry(smb_autohome_info_t *);
static char *smb_autohome_keysub(const char *, char *, int);
static smb_autohome_info_t *smb_autohome_getinfo(void);
static smb_autohome_t *smb_autohome_lookup(const char *);
static void smb_autohome_setent(void);
static void smb_autohome_endent(void);
static smb_autohome_t *smb_autohome_getent(const char *);
static uint32_t smb_autohome_parse_options(smb_share_t *);
static uint32_t smb_autohome_add_private(const char *, uid_t, gid_t);
static void smb_autohome_setflag(const char *, smb_share_t *, uint32_t);

/*
 * Add an autohome share.  See smb_autohome(4) for details.
 *
 * If share directory contains backslash path separators, they will
 * be converted to forward slash to support NT/DOS path style for
 * autohome shares.
 *
 * We need to serialize calls to smb_autohome_lookup because it
 * operates on the global smb_ai structure.
 */
void
smb_autohome_add(const smb_token_t *token)
{
	char		*username;
	uid_t		uid;
	gid_t		gid;

	uid = token->tkn_user.i_id;
	gid = token->tkn_primary_grp.i_id;

	username = (token->tkn_posix_name)
	    ? token->tkn_posix_name : token->tkn_account_name;

	if (smb_autohome_add_private(username, uid, gid) != NERR_Success) {
		if (!smb_isstrlwr(username)) {
			(void) smb_strlwr(username);
			(void) smb_autohome_add_private(username, uid, gid);
		}
	}
}

/*
 * An autohome share is not created if a static share using the same name
 * already exists.  Autohome shares will be added for each login attempt.
 *
 * The share lookup may return the first argument in all lower case so
 * a copy is passed in instead.
 *
 * We need to serialize calls to smb_autohome_lookup because it
 * operates on the global smb_ai structure.
 */
static uint32_t
smb_autohome_add_private(const char *username, uid_t uid, gid_t gid)
{
	static mutex_t	autohome_mutex;
	smb_share_t	si;
	smb_autohome_t	*ai;
	char 		shr_name[MAXNAMELEN];
	uint32_t	status;

	(void) strlcpy(shr_name, username, sizeof (shr_name));

	if ((status = smb_share_lookup(shr_name, &si)) == NERR_Success) {
		if (si.shr_flags & SMB_SHRF_AUTOHOME)
			status = smb_share_add(&si);

		smb_share_free(&si);
		return (status);
	}

	(void) mutex_lock(&autohome_mutex);

	if ((ai = smb_autohome_lookup(username)) == NULL) {
		(void) mutex_unlock(&autohome_mutex);
		return (NERR_ItemNotFound);
	}

	bzero(&si, sizeof (smb_share_t));
	si.shr_path = strdup(ai->ah_path);
	si.shr_name = strdup(username);
	si.shr_container = strdup(ai->ah_container);

	if (si.shr_path == NULL || si.shr_name == NULL ||
	    si.shr_container == NULL) {
		smb_share_free(&si);
		return (ERROR_NOT_ENOUGH_MEMORY);
	}

	if ((status = smb_autohome_parse_options(&si)) != ERROR_SUCCESS) {
		smb_share_free(&si);
		return (status);
	}

	if (si.shr_cmnt == NULL) {
		if ((si.shr_cmnt = strdup("Autohome")) == NULL) {
			smb_share_free(&si);
			return (ERROR_NOT_ENOUGH_MEMORY);
		}
	}

	(void) strsubst(si.shr_path, '\\', '/');
	si.shr_flags |= SMB_SHRF_TRANS | SMB_SHRF_AUTOHOME;
	si.shr_uid = uid;
	si.shr_gid = gid;
	si.shr_drive = 'B';

	(void) mutex_unlock(&autohome_mutex);

	status = smb_share_add(&si);
	smb_share_free(&si);
	return (status);
}

/*
 * Search the autohome database for the specified name. The name cannot
 * be an empty string or begin with * or +.
 * 1. Search the file for the specified name.
 * 2. Check for the wildcard rule and, if present, treat it as a match.
 * 3. Check for the nsswitch rule and, if present, lookup the name
 *    via the name services. Note that the nsswitch rule will never
 *    be applied if the wildcard rule is present.
 *
 * Returns a pointer to the entry on success or null on failure.
 */
static smb_autohome_t *
smb_autohome_lookup(const char *name)
{
	struct passwd *pw;
	smb_autohome_t *ah = NULL;

	if (name == NULL)
		return (NULL);

	if (*name == '\0' || *name == '*' || *name == '+')
		return (NULL);

	smb_autohome_setent();

	while ((ah = smb_autohome_getent(name)) != NULL) {
		if (strcasecmp(ah->ah_name, name) == 0)
			break;
	}

	if (ah == NULL) {
		smb_autohome_setent();

		while ((ah = smb_autohome_getent(name)) != NULL) {
			if (strcasecmp(ah->ah_name, "*") == 0) {
				ah->ah_name = (char *)name;
				break;
			}
		}
	}

	if (ah == NULL) {
		smb_autohome_setent();

		while ((ah = smb_autohome_getent("+nsswitch")) != NULL) {
			if (strcasecmp("+nsswitch", ah->ah_name) != 0)
				continue;
			if ((pw = getpwnam(name)) == NULL) {
				ah = NULL;
				break;
			}

			ah->ah_name = pw->pw_name;

			if (ah->ah_path)
				ah->ah_container = ah->ah_path;

			ah->ah_path = pw->pw_dir;
			break;
		}
	}

	smb_autohome_endent();
	return (ah);
}

/*
 * Open or rewind the autohome database.
 */
static void
smb_autohome_setent(void)
{
	smb_autohome_info_t *si;
	char path[MAXNAMELEN];
	char filename[MAXNAMELEN];
	int rc;

	if ((si = smb_autohome_getinfo()) != 0) {
		(void) fseek(si->fp, 0L, SEEK_SET);
		si->lineno = 0;
		return;
	}

	if ((si = &smb_ai) == 0)
		return;

	rc = smb_config_getstr(SMB_CI_AUTOHOME_MAP, path, sizeof (path));
	if (rc != SMBD_SMF_OK)
		return;

	(void) snprintf(filename, MAXNAMELEN, "%s/%s", path,
	    SMB_AUTOHOME_FILE);

	if ((si->fp = fopen(filename, "r")) == NULL)
		return;

	si->magic1 = si;
	si->magic2 = si;
	si->lineno = 0;
}

/*
 * Close the autohome database and invalidate the autohome info.
 * We can't zero the whole info structure because the application
 * should still have access to the data after the file is closed.
 */
static void
smb_autohome_endent(void)
{
	smb_autohome_info_t *si;

	if ((si = smb_autohome_getinfo()) != 0) {
		(void) fclose(si->fp);
		si->fp = 0;
		si->magic1 = 0;
		si->magic2 = 0;
	}
}

/*
 * Return the next entry in the autohome database, opening the file
 * if necessary.  Returns null on EOF or error.
 *
 * Note that we are not looking for the specified name. The name is
 * only used for key substitution, so that the caller sees the entry
 * in expanded form.
 */
static smb_autohome_t *
smb_autohome_getent(const char *name)
{
	smb_autohome_info_t *si;
	char *bp;

	if ((si = smb_autohome_getinfo()) == 0) {
		smb_autohome_setent();

		if ((si = smb_autohome_getinfo()) == 0)
			return (0);
	}

	/*
	 * Find the next non-comment, non-empty line.
	 * Anything after a # is a comment and can be discarded.
	 * Discard a newline to avoid it being included in the parsing
	 * that follows.
	 * Leading and training whitespace is discarded, and replicated
	 * whitespace is compressed to simplify the token parsing,
	 * although strsep() deals with that better than strtok().
	 */
	do {
		if (fgets(si->buf, SMB_AUTOHOME_BUFSIZ, si->fp) == 0)
			return (0);

		++si->lineno;

		if ((bp = strpbrk(si->buf, "#\r\n")) != 0)
			*bp = '\0';

		(void) trim_whitespace(si->buf);
		bp = strcanon(si->buf, " \t");
	} while (*bp == '\0');

	(void) smb_autohome_keysub(name, si->buf, SMB_AUTOHOME_BUFSIZ);
	return (smb_autohome_make_entry(si));
}

/*
 * Set up an autohome entry from the line buffer. The line should just
 * contain tokens separated by single whitespace. The line format is:
 *
 *	<username> <home-dir-path> <description,ADScontainerOptions>
 *	i.e., admin path description="bla bla",ou=testou
 */
static smb_autohome_t *
smb_autohome_make_entry(smb_autohome_info_t *si)
{
	char *bp;
	int i;

	bp = si->buf;

	for (i = 0; i < SMB_AUTOHOME_MAXARG; ++i)
		si->argv[i] = NULL;

	for (i = 0; i < SMB_AUTOHOME_MAXARG - 1; ++i) {
		do {
			if ((si->argv[i] = strsep(&bp, " \t")) == NULL)
				break;
		} while (*(si->argv[i]) == '\0');

		if (si->argv[i] == NULL)
			break;
	}

	if (i == (SMB_AUTOHOME_MAXARG - 1))
		si->argv[i] = bp;

	if ((si->autohome.ah_name = si->argv[0]) == NULL) {
		/*
		 * Sanity check: the name could be an empty
		 * string but it can't be a null pointer.
		 */
		return (0);
	}

	if ((si->autohome.ah_path = si->argv[1]) == NULL)
		si->autohome.ah_path = "";

	if ((si->autohome.ah_container = si->argv[2]) == NULL)
		si->autohome.ah_container = "";

	return (&si->autohome);
}

/*
 * Substitute the ? and & map keys.
 * ? is replaced by the first character of the name
 * & is replaced by the whole name.
 */
static char *
smb_autohome_keysub(const char *name, char *buf, int buflen)
{
	char key[SMB_AUTOHOME_KEYSIZ];
	char *ampersand;
	char *tmp;
	int bufsize = buflen;

	(void) strlcpy(key, buf, SMB_AUTOHOME_KEYSIZ);

	if ((tmp = strpbrk(key, " \t")) == NULL)
		return (NULL);

	*tmp = '\0';

	/*
	 * Substitution characters are not allowed in the key.
	 */
	if (strpbrk(key, "?&") != NULL)
		return (NULL);

	if (strcmp(key, "*") == 0 && name != NULL)
		(void) strlcpy(key, name, SMB_AUTOHOME_KEYSIZ);

	(void) strsubst(buf, '?', *key);

	while ((ampersand = strchr(buf, '&')) != NULL) {
		if ((tmp = strdup(ampersand + 1)) == NULL)
			return (0);

		bufsize = buflen - (ampersand - buf);
		(void) strlcpy(ampersand, key, bufsize);
		(void) strlcat(ampersand, tmp, bufsize);
		free(tmp);
	}

	return (buf);
}

/*
 * Get a pointer to the context buffer and validate it.
 */
static smb_autohome_info_t *
smb_autohome_getinfo(void)
{
	smb_autohome_info_t *si;

	if ((si = &smb_ai) == 0)
		return (0);

	if ((si->magic1 == si) && (si->magic2 == si) && (si->fp != NULL))
		return (si);

	return (0);
}

/*
 * Parse the options string, which contains a comma separated list of
 * name-value pairs.  One of the options may be an AD container, which
 * is also a comma separated list of name-value pairs.  For example,
 * dn=ad,dn=sun,dn=com,ou=users
 *
 * All options other than the AD container will be extracted from
 * shr_container and used to set share properties.
 * On return, shr_container will contain the AD container string.
 */
static uint32_t
smb_autohome_parse_options(smb_share_t *si)
{
	char buf[MAXPATHLEN];
	char **argv;
	char **ap;
	char *bp;
	char *value;
	boolean_t separator = B_FALSE;
	int argc;
	int i;

	if (strlcpy(buf, si->shr_container, MAXPATHLEN) == 0)
		return (ERROR_SUCCESS);

	for (argc = 1, bp = si->shr_container; *bp != '\0'; ++bp)
		if (*bp == ',')
			++argc;

	if ((argv = calloc(argc + 1, sizeof (char *))) == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	ap = argv;
	for (bp = buf, i = 0; i < argc; ++i) {
		do {
			if ((value = strsep(&bp, ",")) == NULL)
				break;
		} while (*value == '\0');

		if (value == NULL)
			break;

		*ap++ = value;
	}
	*ap = NULL;

	si->shr_container[0] = '\0';
	bp = si->shr_container;

	for (ap = argv; *ap != NULL; ++ap) {
		value = *ap;

		if (strncasecmp(value, "catia=", 6) == 0) {
			smb_autohome_setflag((value + 6), si, SMB_SHRF_CATIA);
			continue;
		}

		if (strncasecmp(value, "csc=", 4) == 0) {
			smb_share_csc_option((value + 4), si);
			continue;
		}

		if (strncasecmp(value, "abe=", 4) == 0) {
			smb_autohome_setflag((value + 4), si, SMB_SHRF_ABE);
			continue;
		}

		if (strncasecmp(value, "description=", 12) == 0) {
			if ((si->shr_cmnt = strdup(value + 12)) == NULL) {
				free(argv);
				return (ERROR_NOT_ENOUGH_MEMORY);
			}
			continue;
		}

		if (strncasecmp(value, "rw=", 3) == 0) {
			if ((si->shr_access_rw = strdup(value + 3)) == NULL) {
				free(argv);
				return (ERROR_NOT_ENOUGH_MEMORY);
			}
			continue;
		}

		if (strncasecmp(value, "ro=", 3) == 0) {
			if ((si->shr_access_ro = strdup(value + 3)) == NULL) {
				free(argv);
				return (ERROR_NOT_ENOUGH_MEMORY);
			}
			continue;
		}

		if (strncasecmp(value, "none=", 5) == 0) {
			if ((si->shr_access_none = strdup(value + 5)) == NULL) {
				free(argv);
				return (ERROR_NOT_ENOUGH_MEMORY);
			}
			continue;
		}

		if (separator)
			(void) strlcat(bp, ",", MAXPATHLEN);
		(void) strlcat(bp, value, MAXPATHLEN);
		separator = B_TRUE;
	}

	free(argv);
	return (ERROR_SUCCESS);
}

/*
 * Set/clear the specified flag based on a boolean share property value.
 */
static void
smb_autohome_setflag(const char *value, smb_share_t *si, uint32_t flag)
{
	if (strcasecmp(value, "true") == 0)
		si->shr_flags |= flag;
	else
		si->shr_flags &= ~flag;
}
