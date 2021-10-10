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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <nsswitch.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>
#include <unistd.h>

#include "ns_sldap.h"
#include <nss_dbdefs.h>
#include <nsswitch.h>
#include <pwd.h>
#include <shadow.h>
#include <rpcsvc/nis.h>

#include "passwdutil.h"

/*
 * name_to_int(rep)
 *
 * Translate the repository to a bitmask.
 * if we don't recognise the repository name, we return REP_ERANGE
 */
int
name_to_int(char *rep_name)
{
	int result = REP_ERANGE;

	if (strcmp(rep_name, "files") == 0)
		result = REP_FILES;
	else if (strcmp(rep_name, "nis") == 0)
		result = REP_NIS;
	else if (strcmp(rep_name, "ldap") == 0)
		result = REP_LDAP;
	else if (strcmp(rep_name, "compat") == 0) {
		struct __nsw_switchconfig *cfg;
		enum   __nsw_parse_err pserr;

		cfg = __nsw_getconfig("passwd_compat", &pserr);
		if (cfg == NULL) {
			result = REP_FILES | REP_NIS;
		} else {
			if (strcmp(cfg->lookups->service_name, "ldap") == 0)
				result = REP_FILES | REP_LDAP;
			else
				result = REP_ERANGE;
			(void) __nsw_freeconfig(cfg);
		}
	}

	return (result);
}

/*
 * Figure out which repository we use in compat mode.
 */
int
get_compat_mode(void)
{
	struct __nsw_switchconfig *cfg;
	enum   __nsw_parse_err pserr;
	int result = REP_COMPAT_NIS;

	if ((cfg = __nsw_getconfig("passwd_compat", &pserr)) != NULL) {
		if (strcmp(cfg->lookups->service_name, "ldap") == 0)
			result = REP_COMPAT_LDAP;
	}
	(void) __nsw_freeconfig(cfg);

	return (result);
}

/*
 * get_ns(rep, accesstype)
 *
 * returns a bitmask of repositories to use based on either
 *   1. the repository that is given as argument
 *   2. the nsswitch.conf file
 *   3. the type of access requested
 *
 * "accesstype" indicates whether we are reading from or writing to the
 * repository. We need to know this since "compat" will translate into
 * REP_NSS (the nss-switch) for READ access (needed to decode
 * the black-magic '+' entries) but it translates into a bitmask
 * on WRITE access.
 *
 * If we detect read-access in compat mode, we augment the result
 * with one of REP_COMPAT_{NIS,LDAP}. We need this in order to
 * implement ATTR_REP_NAME in nss_getpwnam.
 *
 * A return value of REP_NOREP indicates an error.
 */
int
get_ns(pwu_repository_t *rep, int accesstype)
{
	struct __nsw_switchconfig *conf = NULL;
	enum __nsw_parse_err pserr;
	struct __nsw_lookup *lkp;
	struct __nsw_lookup *lkp2;
	struct __nsw_lookup *lkp3;
	struct __nsw_lookup *lkpn;
	int result = REP_NOREP;

	if (rep != PWU_DEFAULT_REP) {
		result = name_to_int(rep->type);
		return (result);
	}

	conf = __nsw_getconfig("passwd", &pserr);
	if (conf == NULL) {
		/*
		 * No config found. The user didn't supply a repository,
		 * so we try to change the password in the default
		 * repositories (files and nis) even though we cannot
		 * find the name service switch entry. (Backward compat)
		 */
		syslog(LOG_ERR, "passwdutil.so: nameservice switch entry for "
		    "passwd not found.");
		result = REP_FILES | REP_NIS;
		return (result);
	}

	lkp = conf->lookups;

	/*
	 * Supported nsswitch.conf can have a maximum of 3 repositories.
	 * If we encounter an unsupported nsswitch.conf, we return REP_NSS
	 * to fall back to the nsswitch backend.
	 *
	 * Note that specifying 'ad' in the configuration is acceptable
	 * though changing AD users' passwords through passwd(1) is not.
	 * Therefore "ad" will be silently ignored.
	 */
	if (conf->num_lookups == 1) {
		/* files or compat */

		if (strcmp(lkp->service_name, "files") == 0) {
			result = name_to_int(lkp->service_name);
		} else if (strcmp(lkp->service_name, "compat") == 0) {
			if (accesstype == PWU_READ)
				result = REP_NSS | get_compat_mode();
			else
				result = name_to_int(lkp->service_name);
		} else
			result = REP_NSS;

	} else if (conf->num_lookups == 2) {
		lkp2 = lkp->next;
		if (strcmp(lkp->service_name, "files") == 0) {
			result = REP_FILES;
			if (strcmp(lkp2->service_name, "ldap") == 0)
				result |= REP_LDAP;
			else if (strcmp(lkp2->service_name, "nis") == 0)
				result |= REP_NIS;
			else if (strcmp(lkp2->service_name, "ad") != 0)
				result = REP_NSS;
			/* AD is ignored */
		} else {
			result = REP_NSS;
		}
	} else if (conf->num_lookups == 3) {
		/*
		 * Valid configurations with 3 repositories are:
		 *   files ad [nis | ldap ] OR
		 *   files [nis | ldap ] ad
		 */
		lkp2 = lkp->next;
		lkp3 = lkp2->next;
		if (strcmp(lkp2->service_name, "ad") == 0)
			lkpn = lkp3;
		else if (strcmp(lkp3->service_name, "ad") == 0)
			lkpn = lkp2;
		else
			lkpn = NULL;
		if (strcmp(lkp->service_name, "files") == 0 &&
		    lkpn != NULL) {
			result = REP_FILES;
			if (strcmp(lkpn->service_name, "ldap") == 0)
				result |= REP_LDAP;
			else if (strcmp(lkpn->service_name, "nis") == 0)
				result |= REP_NIS;
			else
				result = REP_NSS;
		} else {
			result = REP_NSS;
		}
	} else {
		result = REP_NSS;
	}

	(void) __nsw_freeconfig(conf);
	return (result);
}

static void
nss_ldap_passwd(p)
	nss_db_params_t	*p;
{
	p->name = NSS_DBNAM_PASSWD;
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = "ldap";
}

static void
nss_ldap_shadow(p)
	nss_db_params_t	*p;
{
	p->name = NSS_DBNAM_SHADOW;
	p->config_name    = NSS_DBNAM_PASSWD;	/* Use config for "passwd" */
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = "ldap";
}


#ifdef PAM_NIS
static void
nss_nis_passwd(p)
	nss_db_params_t	*p;
{
	p->name = NSS_DBNAM_PASSWD;
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = "nis";
}

static void
nss_nis_shadow(p)
	nss_db_params_t	*p;
{
	p->name = NSS_DBNAM_SHADOW;
	p->config_name    = NSS_DBNAM_PASSWD;	/* Use config for "passwd" */
	p->flags |= NSS_USE_DEFAULT_CONFIG;
	p->default_config = "nis";
}
#endif /* PAM_NIS */

static char *
gettok(nextpp)
	char	**nextpp;
{
	char	*p = *nextpp;
	char	*q = p;
	char	c;

	if (p == 0) {
		return (0);
	}
	while ((c = *q) != '\0' && c != ':') {
		q++;
	}
	if (c == '\0') {
		*nextpp = 0;
	} else {
		*q++ = '\0';
		*nextpp = q;
	}
	return (p);
}

/*
 * Return values: 0 = success, 1 = parse error, 2 = erange ...
 * The structure pointer passed in is a structure in the caller's space
 * wherein the field pointers would be set to areas in the buffer if
 * need be. instring and buffer should be separate areas.
 */
static int
str2passwd(const char *instr, int lenstr, void *ent, char *buffer, int buflen)
{
	struct passwd	*passwd	= (struct passwd *)ent;
	char		*p, *next;
	int		black_magic;	/* "+" or "-" entry */

	if (lenstr + 1 > buflen) {
		return (NSS_STR_PARSE_ERANGE);
	}
	/*
	 * We copy the input string into the output buffer and
	 * operate on it in place.
	 */
	(void) memcpy(buffer, instr, lenstr);
	buffer[lenstr] = '\0';

	next = buffer;

	passwd->pw_name = p = gettok(&next);		/* username */
	if (*p == '\0') {
		/* Empty username;  not allowed */
		return (NSS_STR_PARSE_PARSE);
	}
	black_magic = (*p == '+' || *p == '-');
	if (black_magic) {
		passwd->pw_uid	= UID_NOBODY;
		passwd->pw_gid	= GID_NOBODY;
		/*
		 * pwconv tests pw_passwd and pw_age == NULL
		 */
		passwd->pw_passwd = "";
		passwd->pw_age	= "";
		/*
		 * the rest of the passwd entry is "optional"
		 */
		passwd->pw_comment = "";
		passwd->pw_gecos = "";
		passwd->pw_dir	= "";
		passwd->pw_shell = "";
	}

	passwd->pw_passwd = p = gettok(&next);		/* password */
	if (p == 0) {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}
	for (; *p != '\0'; p++) {			/* age */
		if (*p == ',') {
			*p++ = '\0';
			break;
		}
	}
	passwd->pw_age = p;

	p = next;					/* uid */
	if (p == 0 || *p == '\0') {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}
	if (!black_magic) {
		passwd->pw_uid = strtol(p, &next, 10);
		if (next == p) {
			/* uid field should be nonempty */
			return (NSS_STR_PARSE_PARSE);
		}
		/*
		 * The old code (in 2.0 thru 2.5) would check
		 * for the uid being negative, or being greater
		 * than 60001 (the rfs limit).  If it met either of
		 * these conditions, the uid was translated to 60001.
		 *
		 * Now we just check for ephemeral uids; anything else
		 * is administrative policy
		 */
		if (passwd->pw_uid > MAXUID)
			passwd->pw_uid = UID_NOBODY;
	}
	if (*next++ != ':') {
		if (black_magic)
			p = gettok(&next);
		else
			return (NSS_STR_PARSE_PARSE);
	}
	p = next;					/* gid */
	if (p == 0 || *p == '\0') {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}
	if (!black_magic) {
		passwd->pw_gid = strtol(p, &next, 10);
		if (next == p) {
			/* gid field should be nonempty */
			return (NSS_STR_PARSE_PARSE);
		}
		/*
		 * gid should be non-negative; anything else
		 * is administrative policy.
		 */
		if (passwd->pw_gid > MAXUID)
			passwd->pw_gid = GID_NOBODY;
	}
	if (*next++ != ':') {
		if (black_magic)
			p = gettok(&next);
		else
			return (NSS_STR_PARSE_PARSE);
	}

	passwd->pw_gecos = passwd->pw_comment = p = gettok(&next);
	if (p == 0) {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}

	passwd->pw_dir = p = gettok(&next);
	if (p == 0) {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}

	passwd->pw_shell = p = gettok(&next);
	if (p == 0) {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}

	/* Better not be any more fields... */
	if (next == 0) {
		/* Successfully parsed and stored */
		return (NSS_STR_PARSE_SUCCESS);
	}
	return (NSS_STR_PARSE_PARSE);
}

typedef const char *constp;

/*
 * Return value 1 means success and more input, 0 means error or no more
 */
static int
getfield(nextp, limit, uns, valp)
	constp		*nextp;
	constp		limit;
	int		uns;
	void		*valp;
{
	constp		p = *nextp;
	char		*endfield;
	char		numbuf[12];  /* Holds -2^31 and trailing ':' */
	int		len;
	long		x;
	unsigned long	ux;

	if (p == 0 || p >= limit) {
		return (0);
	}
	if (*p == ':') {
		p++;
		*nextp = p;
		return (p < limit);
	}
	if ((len = limit - p) > sizeof (numbuf) - 1) {
		len = sizeof (numbuf) - 1;
	}
	/*
	 * We want to use strtol() and we have a readonly non-zero-terminated
	 *   string, so first we copy and terminate the interesting bit.
	 *   Ugh.  (It's convenient to terminate with a colon rather than \0).
	 */
	if ((endfield = memccpy(numbuf, p, ':', len)) == 0) {
		if (len != limit - p) {
			/* Error -- field is too big to be a legit number */
			return (0);
		}
		numbuf[len] = ':';
		p = limit;
	} else {
		p += (endfield - numbuf);
	}
	if (uns) {
		ux = strtoul(numbuf, &endfield, 10);
		if (*endfield != ':') {
			/* Error -- expected <integer><colon> */
			return (0);
		}
		*((unsigned int *)valp) = (unsigned int)ux;
	} else {
		x = strtol(numbuf, &endfield, 10);
		if (*endfield != ':') {
			/* Error -- expected <integer><colon> */
			return (0);
		}
		*((int *)valp) = (int)x;
	}
	*nextp = p;
	return (p < limit);
}

/*
 *  str2spwd() -- convert a string to a shadow passwd entry.  The parser is
 *	more liberal than the passwd or group parsers;  since it's legitimate
 *	for almost all the fields here to be blank, the parser lets one omit
 *	any number of blank fields at the end of the entry.  The acceptable
 *	forms for '+' and '-' entries are the same as those for normal entries.
 *  === Is this likely to do more harm than good?
 *
 * Return values: 0 = success, 1 = parse error, 2 = erange ...
 * The structure pointer passed in is a structure in the caller's space
 * wherein the field pointers would be set to areas in the buffer if
 * need be. instring and buffer should be separate areas.
 */
int
str2spwd(instr, lenstr, ent, buffer, buflen)
	const char	*instr;
	int		lenstr;
	void	*ent; /* really (struct spwd *) */
	char	*buffer;
	int	buflen;
{
	struct spwd	*shadow	= (struct spwd *)ent;
	const char	*p = instr, *limit;
	char		*bufp;
	int	lencopy, black_magic;

	limit = p + lenstr;
	if ((p = memchr(instr, ':', lenstr)) == 0 ||
		++p >= limit ||
		(p = memchr(p, ':', limit - p)) == 0) {
		lencopy = lenstr;
		p = 0;
	} else {
		lencopy = p - instr;
		p++;
	}
	if (lencopy + 1 > buflen) {
		return (NSS_STR_PARSE_ERANGE);
	}
	(void) memcpy(buffer, instr, lencopy);
	buffer[lencopy] = 0;

	black_magic = (*instr == '+' || *instr == '-');
	shadow->sp_namp = bufp = buffer;
	shadow->sp_pwdp	= 0;
	shadow->sp_lstchg = -1;
	shadow->sp_min	= -1;
	shadow->sp_max	= -1;
	shadow->sp_warn	= -1;
	shadow->sp_inact = -1;
	shadow->sp_expire = -1;
	shadow->sp_flag	= 0;

	if ((bufp = strchr(bufp, ':')) == 0) {
		if (black_magic)
			return (NSS_STR_PARSE_SUCCESS);
		else
			return (NSS_STR_PARSE_PARSE);
	}
	*bufp++ = '\0';

	shadow->sp_pwdp = bufp;
	if (instr == 0) {
		if ((bufp = strchr(bufp, ':')) == 0) {
			if (black_magic)
				return (NSS_STR_PARSE_SUCCESS);
			else
				return (NSS_STR_PARSE_PARSE);
		}
		*bufp++ = '\0';
		p = bufp;
	} /* else p was set when we copied name and passwd into the buffer */

	if (!getfield(&p, limit, 0, &shadow->sp_lstchg))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 0, &shadow->sp_min))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 0, &shadow->sp_max))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 0, &shadow->sp_warn))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 0, &shadow->sp_inact))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 0, &shadow->sp_expire))
			return (NSS_STR_PARSE_SUCCESS);
	if (!getfield(&p, limit, 1, &shadow->sp_flag))
			return (NSS_STR_PARSE_SUCCESS);
	if (p != limit) {
		/* Syntax error -- garbage at end of line */
		return (NSS_STR_PARSE_PARSE);
	}
	return (NSS_STR_PARSE_SUCCESS);
}

static nss_XbyY_buf_t *buffer;
static DEFINE_NSS_DB_ROOT(db_root);

#define	GETBUF()	\
	NSS_XbyY_ALLOC(&buffer, sizeof (struct passwd), NSS_BUFLEN_PASSWD)

#pragma fini(endutilpwent)

static void
endutilpwent(void)
{
	NSS_XbyY_FREE(&buffer);
	nss_delete(&db_root);
}

/*ARGSUSED*/
struct passwd *
getpwnam_from(const char *name, pwu_repository_t *rep, int reptype)
{
	nss_XbyY_buf_t  *b = GETBUF();
	nss_XbyY_args_t arg;

	if (b == 0)
		return (0);

	NSS_XbyY_INIT(&arg, b->result, b->buffer, b->buflen, str2passwd);
	arg.key.name = name;

	switch (reptype) {
	case REP_LDAP:
		(void) nss_search(&db_root, nss_ldap_passwd,
		    NSS_DBOP_PASSWD_BYNAME, &arg);
		break;
#ifdef PAM_NIS
	case REP_NIS:
		(void) nss_search(&db_root, nss_nis_passwd,
		    NSS_DBOP_PASSWD_BYNAME, &arg);
		break;
#endif
	default:
		return (NULL);
	}

	return (struct passwd *)NSS_XbyY_FINI(&arg);
}

/*ARGSUSED*/
struct passwd *
getpwuid_from(uid_t uid, pwu_repository_t *rep, int reptype)
{
	nss_XbyY_buf_t  *b = GETBUF();
	nss_XbyY_args_t arg;

	if (b == 0)
		return (0);

	NSS_XbyY_INIT(&arg, b->result, b->buffer, b->buflen, str2passwd);
	arg.key.uid = uid;

	switch (reptype) {
	case REP_LDAP:
		(void) nss_search(&db_root, nss_ldap_passwd,
		    NSS_DBOP_PASSWD_BYUID, &arg);
		break;
#ifdef PAM_NIS
	case REP_NIS:
		(void) nss_search(&db_root, nss_nis_passwd,
		    NSS_DBOP_PASSWD_BYUID, &arg);
		break;
#endif
	default:
		return (NULL);
	}

	return (struct passwd *)NSS_XbyY_FINI(&arg);
}

static nss_XbyY_buf_t *spbuf;
static DEFINE_NSS_DB_ROOT(spdb_root);

#define	GETSPBUF()	\
	NSS_XbyY_ALLOC(&spbuf, sizeof (struct spwd), NSS_BUFLEN_SHADOW)

#pragma fini(endutilspent)

static void
endutilspent(void)
{
	NSS_XbyY_FREE(&spbuf);
	nss_delete(&spdb_root);
}

/*ARGSUSED*/
struct spwd *
getspnam_from(const char *name, pwu_repository_t *rep, int reptype)
{
	nss_XbyY_buf_t  *b = GETSPBUF();
	nss_XbyY_args_t arg;

	if (b == 0)
		return (0);

	NSS_XbyY_INIT(&arg, b->result, b->buffer, b->buflen, str2spwd);
	arg.key.name = name;
	switch (reptype) {
	case REP_LDAP:
		(void) nss_search(&spdb_root, nss_ldap_shadow,
		    NSS_DBOP_SHADOW_BYNAME, &arg);
		break;
#ifdef PAM_NIS
	case REP_NIS:
		(void) nss_search(&spdb_root, nss_nis_shadow,
		    NSS_DBOP_SHADOW_BYNAME, &arg);
		break;
#endif
	default:
		return (NULL);
	}
	return (struct spwd *)NSS_XbyY_FINI(&arg);
}
