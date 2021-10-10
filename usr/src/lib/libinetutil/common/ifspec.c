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
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains a routine used to validate a ifconfig-style interface
 * specification
 */

#include <stdlib.h>
#include <ctype.h>
#include <alloca.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <libinetutil.h>
#include <zone.h>

static int
extract_uint(const char *valstr, uint_t *val)
{
	char	*ep;
	unsigned long ul;

	errno = 0;
	ul = strtoul(valstr, &ep, 10);
	if (errno != 0 || *ep != '\0' || ul > UINT_MAX)
		return (-1);
	*val = (uint_t)ul;
	return (0);
}

/*
 * Given a token with a logical unit spec, return the logical unit converted
 * to a uint_t.
 *
 * Returns: 0 for success, nonzero if an error occurred. errno is set if
 * necessary.
 */
static int
getlun(const char *bp, int bpsize, uint_t *lun)
{
	const char	*ep = &bp[bpsize - 1];
	const char	*tp;

	/* Lun must be all digits */
	for (tp = ep; tp > bp && isdigit(*tp); tp--)
		/* Null body */;

	if (tp == ep || tp != bp || extract_uint(bp + 1, lun) != 0) {
		errno = EINVAL;
		return (-1);
	}
	return (0);
}

/*
 * Given a single token ending with a ppa spec, return the ppa spec converted
 * to a uint_t.
 *
 * Returns: 0 for success, nonzero if an error occurred. errno is set if
 * necessary.
 */
static int
getppa(const char *bp, int bpsize, uint_t *ppa)
{
	const char	*ep = &bp[bpsize - 1];
	const char	*tp;

	for (tp = ep; tp >= bp && isdigit(*tp); tp--)
		/* Null body */;

	/*
	 * If the device name does not end with a digit or the device
	 * name is a sequence of numbers or a PPA contains a leading
	 * zero, return error.
	 */
	if (tp == ep || tp < bp || ((ep - tp) > 1 && *(tp + 1) == '0'))
		goto fail;

	if (extract_uint(tp + 1, ppa) != 0)
		goto fail;

	/* max value of PPA is 4294967294, which is (UINT_MAX - 1) */
	if (*ppa > UINT_MAX - 1)
		goto fail;
	return (0);
fail:
	errno = EINVAL;
	return (-1);
}

/*
 * Given an IP interface name, which is either a
 *	- datalink name (which is driver name plus PPA), for e.g. bge0 or
 *	- datalink name plus a logical interface identifier (delimited by ':'),
 *		for e.g. bge0:34
 * the following function validates its form and decomposes the contents into
 * ifspec_t.
 *
 * Returns B_TRUE for success, otherwise B_FALSE is returned.
 */
boolean_t
ifparse_ifspec(const char *ifname, ifspec_t *ifsp)
{
	char	*lp;
	char	ifnamecp[LIFNAMSIZ];

	if (ifname == NULL || ifname[0] == '\0' ||
	    strlcpy(ifnamecp, ifname, LIFNAMSIZ) >= LIFNAMSIZ) {
		errno = EINVAL;
		return (B_FALSE);
	}

	ifsp->ifsp_lunvalid = B_FALSE;

	/* Any logical units? */
	lp = strchr(ifnamecp, ':');
	if (lp != NULL) {
		if (getlun(lp, strlen(lp), &ifsp->ifsp_lun) != 0)
			return (B_FALSE);
		*lp = '\0';
		ifsp->ifsp_lunvalid = B_TRUE;
	}

	return (dlparse_drvppa(ifnamecp, ifsp->ifsp_devnm,
	    sizeof (ifsp->ifsp_devnm), &ifsp->ifsp_ppa));
}

/*
 * Given a `linkname' of the form drv(ppa), parse it into `driver' and `ppa'.
 * If the `dsize' for the `driver' is not atleast MAXLINKNAMELEN then part of
 * the driver name will be copied to `driver'.
 *
 * This function also validates driver name and PPA and therefore callers can
 * call this function with `driver' and `ppa' set to NULL, to just verify the
 * linkname.
 */
boolean_t
dlparse_drvppa(const char *linknamep, char *driver, uint_t dsize, uint_t *ppa)
{
	char	*tp;
	char    linkname[MAXLINKNAMELEN];
	uint_t	lppa, len;

	if (linknamep == NULL || linknamep[0] == '\0')
		goto fail;

	len = strlcpy(linkname, linknamep, MAXLINKNAMELEN);
	if (len >= MAXLINKNAMELEN)
		goto fail;

	/* Get PPA */
	if (getppa(linkname, len, &lppa) != 0)
		return (B_FALSE);

	/* strip the ppa off of the linkname, if present */
	for (tp = &linkname[len - 1]; tp >= linkname && isdigit(*tp); tp--)
		*tp = '\0';

	/*
	 * Now check for the validity of the device name. The legal characters
	 * in a device name are: alphanumeric (a-z,  A-Z,  0-9),
	 * underscore ('_'), and '.'. The first character of the device
	 * name cannot be a digit and should be an alphabetic character.
	 */
	if (!isalpha(linkname[0]))
		goto fail;
	for (tp = linkname + 1; *tp != '\0'; tp++) {
		if (!isalnum(*tp) && *tp != '_' && *tp != '.')
			goto fail;
	}

	if (driver != NULL)
		(void) strlcpy(driver, linkname, dsize);

	if (ppa != NULL)
		*ppa = lppa;

	return (B_TRUE);
fail:
	errno = EINVAL;
	return (B_FALSE);
}

/*
 * Given a linkname that can be specified using a zonename prefix retrieve
 * the optional linkname and/or zone ID value. If no zonename prefix was
 * specified we set the optional linkname and set optional zone ID return
 * value to ALL_ZONES.
 */
boolean_t
dlparse_zonelinkname(const char *name, char *link_name, zoneid_t *zoneidp)
{
	char buffer[MAXLINKNAMESPECIFIER];
	char *search = "/";
	char *zonetoken;
	char *linktoken;
	char *last;
	size_t namelen;

	if (link_name != NULL)
		link_name[0] = '\0';
	if (zoneidp != NULL)
		*zoneidp = ALL_ZONES;

	if ((namelen = strlcpy(buffer, name, sizeof (buffer))) >=
	    sizeof (buffer))
		return (B_FALSE);

	if ((zonetoken = strtok_r(buffer, search, &last)) == NULL)
		return (B_FALSE);

	/* If there are no other strings, return given name as linkname */
	if ((linktoken = strtok_r(NULL, search, &last)) == NULL) {
		if (namelen >= MAXLINKNAMELEN)
			return (B_FALSE);
		if (link_name != NULL)
			(void) strlcpy(link_name, name, MAXLINKNAMELEN);
		return (B_TRUE);
	}

	/* First token is the zonename. Check zone and link lengths */
	if (strlen(zonetoken) >= ZONENAME_MAX || strlen(linktoken) >=
	    MAXLINKNAMELEN)
		return (B_FALSE);
	/*
	 * If there are more '/' separated strings in the input
	 * name  then we return failure. We only support a single
	 * zone prefix or a devnet directory (f.e. net/bge0).
	 */
	if (strtok_r(NULL, search, &last) != NULL)
		return (B_FALSE);

	if (link_name != NULL)
		(void) strlcpy(link_name, linktoken, MAXLINKNAMELEN);
	if (zoneidp != NULL) {
		if ((*zoneidp = getzoneidbyname(zonetoken)) < MIN_ZONEID)
			return (B_FALSE);
	}

	return (B_TRUE);
}
