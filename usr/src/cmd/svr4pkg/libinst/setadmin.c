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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/* Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */


#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pkglocs.h>
#include <locale.h>
#include <libintl.h>
#include <pkglib.h>
#include <pkgerr.h>
#include <pkgweb.h>
#include <install.h>
#include <libinst.h>
#include <libadm.h>
#include <messages.h>

#define	DEFMAIL	"root"

extern struct admin	adm;		/* holds info about install admin */
extern int		warnflag;	/* != 0 non-fatal error occurred 2 */

static struct {
	char	**memloc;
	char	*tag;
} admlist[] = {
	&adm.action,		"action",
	&adm.authentication,	"authentication",
	&adm.basedir,		"basedir",
	&adm.conflict,		"conflict",
	&adm.idepend,		"idepend",
	&adm.instance,		"instance",
	&adm.keystore,		"keystore",
	&adm.mail,		"mail",
	&adm.networkretries,	"networkretries",
	&adm.networktimeout,	"networktimeout",
	&adm.partial,		"partial",
	&adm.proxy,		"proxy",
	&adm.rdepend,		"rdepend",
	&adm.RSCRIPTALT,	RSCRIPTALT_KEYWORD,
	&adm.runlevel,		"runlevel",
	&adm.setuid,		"setuid",
	&adm.space,		"space",
	/* MUST BE LAST ENTRY IN LIST */
	(char **)NULL,		(char *)NULL
};

/*
 * Name:	setadminSetting
 * Description:	set one administration parameter setting
 * Arguments:	a_paramName - pointer to string representing the name of
 *			the administration parameter to set
 *		a_paramValue - pointer to string representing the value
 *			to set the specified administration parameter to
 * Returns:	char *
 *			- old value the parameter had before being set
 *			== NULL - the old paramter was not set
 */

char *
setadminSetting(char *a_paramName, char *a_paramValue)
{
	char	*oldValue = (char *)NULL;
	int	i;

	/* locate and update the specified admin setting */

	for (i = 0; admlist[i].memloc; i++) {
		if (strcmp(a_paramName, admlist[i].tag) == 0) {
			oldValue = *admlist[i].memloc;
			*admlist[i].memloc = a_paramValue;
			break;
		}
	}

	if (admlist[i].memloc == (char **)NULL) {
		logerr(WRN_UNKNOWN_ADM_PARAM, a_paramName);
	}

	return (oldValue);
}

/*
 * Name:	setadminFile
 * Description:	read and remember settings from administration file
 * Arguments:	file - pointer to string representing the path to the
 *			administration file to read - if this is NULL
 *			then the name "default" is used - if this is
 *			the string "none" then the admin "basedir"
 *			setting is set to "ask" so that the location
 *			of the administration file will be interactively
 *			asked at the appropriate time
 * Returns:	void
 */

void
setadminFile(char *file)
{
	FILE	*fp;
	int	i;
	char	param[MAX_PKG_PARAM_LENGTH];
	char	*value;
	char	path[PATH_MAX];
	int	mail = 0;

	if (file == NULL)
		file = "default";
	else if (strcmp(file, "none") == 0) {
		adm.basedir = "ask";
		return;
	}

	if (file[0] == '/')
		(void) strcpy(path, file);
	else {
		(void) snprintf(path, sizeof (path), "%s/admin/%s",
				get_PKGADM(), file);
		if (access(path, R_OK)) {
			(void) snprintf(path, sizeof (path), "%s/admin/%s",
				PKGADM, file);
		}
	}

	if ((fp = fopen(path, "r")) == NULL) {
		progerr(ERR_OPEN_ADMIN_FILE, file, strerror(errno));
		quit(99);
	}

	param[0] = '\0';
	while (value = fpkgparam(fp, param)) {
		if (strcmp(param, "mail") == 0) {
			mail = 1;
		}
		if (value[0] == '\0') {
			param[0] = '\0';
			continue; /* same as not being set at all */
		}
		for (i = 0; admlist[i].memloc; i++) {
			if (strcmp(param, admlist[i].tag) == 0) {
				*admlist[i].memloc = value;
				break;
			}
		}
		if (admlist[i].memloc == NULL) {
			logerr(WRN_UNKNOWN_ADM_PARAM, param);
			free(value);
		}
		param[0] = '\0';
	}

	(void) fclose(fp);

	if (!mail) {
		adm.mail = DEFMAIL; 	/* if we don't assign anything to it */
	}
}


/*
 * Function:	web_ck_retries
 * Description:	Reads admin file setting for networkretries, or uses default
 * Parameters:	None
 * Returns:	admin file setting for networkretries, or the default if no
 *		admin file setting exists or if it is outside the
 *		allowable range.
 */
int
web_ck_retries(void)
{
	int retries = NET_RETRIES_DEFAULT;

	if (ADMSET(networkretries)) {
		/* Make sure value is within valid range */
		if ((retries = atoi(adm.networkretries)) == 0) {
			return (NET_RETRIES_DEFAULT);
		} else if (retries <= NET_RETRIES_MIN ||
			retries > NET_RETRIES_MAX) {
			return (NET_RETRIES_DEFAULT);
		}
	}
	return (retries);
}

/*
 * Function:	web_ck_authentication
 * Description:	Retrieves admin file setting for authentication
 * Parameters:	None
 * Returns:	admin file policy for authentication - AUTH_QUIT
 *		or AUTH_NOCHECK.
 *		non-zero failure
 */
int
web_ck_authentication(void)
{
	if (ADM(authentication, "nocheck"))
		return (AUTH_NOCHECK);

	return (AUTH_QUIT);
}

/*
 * Function:	web_ck_timeout
 * Description:	Retrieves admin file policy for networktimeout's
 * Parameters:	NONE
 * Returns:	Admin file setting for networktimeout, or default
 *		timeout value if admin file does not specify one,
 *		or specifies one that is out of the allowable range.
 */
int
web_ck_timeout(void)
{
	int timeout = NET_TIMEOUT_DEFAULT;

	if (ADMSET(networktimeout)) {
		/* Make sure value is within valid range */
		if ((timeout = atoi(adm.networktimeout)) == 0) {
			return (NET_TIMEOUT_DEFAULT);
		} else if (timeout <= NET_TIMEOUT_MIN ||
			timeout > NET_TIMEOUT_MAX) {
			return (NET_TIMEOUT_DEFAULT);
		}
	}
	return (timeout);
}

/*
 * Function:	check_keystore_admin
 * Description:	Retrieves security keystore setting from admin file,
 *		or validates user-supplied keystore policy.
 * Parameters:	keystore - Where to store resulting keystore policy
 * Returns:	B_TRUE - admin file contained valid keystore, or
 *		user-supplied keystore passed in "keystore" was
 *		valid.  Resulting keystore stored in "keystore"
 *
 *		B_FALSE - No location supplied to store result,
 *		or user-supplied keystore was not valid.
 */
boolean_t
check_keystore_admin(char **keystore)
{

	if (!keystore) {
		/* no location to store keystore */
		return (B_FALSE);
	}

	if (*keystore != NULL) {
	    if (!path_valid(*keystore)) {
		    /* the given keystore is invalid */
		    return (B_FALSE);
	    }

	    /* the user-supplied keystore was valid */
	    return (B_TRUE);
	}

	/* no user-supplied, so use default */
	if ((*keystore = set_keystore_admin()) == NULL) {
		*keystore = PKGSEC;
	}
	return (B_TRUE);
}

/*
 * Function:	get_proxy_port_admin
 * Description:	Retrieves proxy setting from admin file
 * Parameters:	proxy - where to store resulting proxy (host:port or URL)
 *		port - Where to store resulting proxy port
 * Returns:	B_TRUE - admin file had a valid proxy setting,
 *		and it is stored in "proxy".
 *		B_FALSE - no proxy setting in admin file, or
 *		invalid setting in admin file.
 */
boolean_t
get_proxy_port_admin(char **proxy, ushort_t *port)
{
	if (ADMSET(proxy) && !path_valid(adm.proxy)) {
		/* admin file has bad keystore */
		return (B_FALSE);
	} else if (ADMSET(proxy)) {
		*proxy = strdup(adm.proxy);
		*port = strip_port(adm.proxy);
	}
	return (B_TRUE);
}

/*
 * Function:	set_keystore_admin
 * Description:	Retrieves security keystore setting from admin file,
 * Parameters:	NONE
 * Returns:	Keystore file policy from admin file, if set
 *		and valid.  NULL otherwise.
 */
char *
set_keystore_admin(void)
{
	if (ADMSET(keystore) && !path_valid(adm.keystore)) {
		return (NULL);
	}

	if (!ADMSET(keystore)) {
		return (NULL);
	}

	return (adm.keystore);
}
