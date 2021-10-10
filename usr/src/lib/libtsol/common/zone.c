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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include	<stdlib.h>
#include	<strings.h>
#include	<zone.h>
#include	<errno.h>
#include	<sys/types.h>
#include 	<sys/tsol/label_macro.h>

/*
 * Get label from zone name
 */
m_label_t *
getzonelabelbyname(const char *zone)
{
	zoneid_t	zoneid;

	if ((zoneid = getzoneidbyname(zone)) == -1) {
		errno = EINVAL;
		return (NULL);
	}
	return (getzonelabelbyid(zoneid));
}

/*
 * Get label from zone id
 */
m_label_t *
getzonelabelbyid(zoneid_t zoneid)
{
	m_label_t 	*slabel;

	if ((slabel = m_label_alloc(MAC_LABEL)) == NULL)
		return (NULL);

	if (zone_getattr(zoneid, ZONE_ATTR_SLBL, slabel,
	    sizeof (m_label_t)) < 0) {
		m_label_free(slabel);
		errno = EINVAL;
		return (NULL);
	}

	return (slabel);
}

/*
 * Get zone id from label
 */

zoneid_t
getzoneidbylabel(const m_label_t *label)
{
	m_label_t	admin_low;
	m_label_t	admin_high;
	zoneid_t	zoneid;
	zoneid_t 	*zids;
	uint_t		nzents;
	int		i;

	bsllow(&admin_low);
	bslhigh(&admin_high);

	/* Check for admin_low or admin_high; both are global zone */
	if (blequal(label, &admin_low) || blequal(label, &admin_high))
		return (GLOBAL_ZONEID);

	/* Fetch zoneids from the kernel */
	if (zone_get_zoneids(&zids, &nzents) != 0)
		return (-1);

	for (i = 0; i < nzents; i++) {
		m_label_t	test_sl;

		if (zids[i] == GLOBAL_ZONEID)
			continue;

		if (zone_getattr(zids[i], ZONE_ATTR_SLBL, &test_sl,
		    sizeof (m_label_t)) < 0)
			continue;	/* Badly configured zone info */

		if (blequal(label, &test_sl) != 0) {
			zoneid = zids[i];
			free(zids);
			return (zoneid);
		}
	}
	free(zids);
	errno = EINVAL;
	return (-1);
}

/*
 * Get zoneroot for a zoneid
 */

char *
getzonerootbyid(zoneid_t zoneid)
{
	char zoneroot[MAXPATHLEN];

	if (zone_getattr(zoneid, ZONE_ATTR_ROOT, zoneroot,
	    sizeof (zoneroot)) == -1) {
		return (NULL);
	}

	return (strdup(zoneroot));
}

/*
 * Get zoneroot for a zonename
 */

char *
getzonerootbyname(const char *zone)
{
	zoneid_t	zoneid;

	if ((zoneid = getzoneidbyname(zone)) == -1)
		return (NULL);
	return (getzonerootbyid(zoneid));
}

/*
 * Get zoneroot for a label
 */

char *
getzonerootbylabel(const m_label_t *label)
{
	zoneid_t	zoneid;

	if ((zoneid = getzoneidbylabel(label)) == -1)
		return (NULL);
	return (getzonerootbyid(zoneid));
}

/*
 * Get label of path relative to global zone
 *
 * This function must be called from the global zone
 */

m_label_t *
getlabelbypath(const char *path)
{
	m_label_t	*slabel;
	zoneid_t 	*zids;
	uint_t		nzents;
	int		i;

	if (getzoneid() != GLOBAL_ZONEID) {
		errno = EINVAL;
		return (NULL);
	}

	/* Fetch zoneids from the kernel */
	if (zone_get_zoneids(&zids, &nzents) != 0)
		return (NULL);

	slabel = m_label_alloc(MAC_LABEL);
	if (slabel == NULL) {
		free(zids);
		return (NULL);
	}

	for (i = 0; i < nzents; i++) {
		char	zoneroot[MAXPATHLEN];
		int	zonerootlen;

		if (zids[i] == GLOBAL_ZONEID)
			continue;

		if (zone_getattr(zids[i], ZONE_ATTR_ROOT, zoneroot,
		    sizeof (zoneroot)) == -1)
			continue;	/* Badly configured zone info */

		/*
		 * Need to handle the case for the /dev directory which is
		 * parallel to the zone's root directory.  So we back up
		 * 4 bytes - the strlen of "root".
		 */
		if ((zonerootlen = strlen(zoneroot)) <= 4)
			continue;	/* Badly configured zone info */
		if (strncmp(path, zoneroot, zonerootlen - 4) == 0) {
			/*
			 * If we get a match, the file is in a labeled zone.
			 * Return the label of that zone.
			 */
			if (zone_getattr(zids[i], ZONE_ATTR_SLBL, slabel,
			    sizeof (m_label_t)) < 0)
				continue;	/* Badly configured zone info */

			free(zids);
			return (slabel);
		}
	}
	free(zids);
	bsllow(slabel);
	return (slabel);
}
