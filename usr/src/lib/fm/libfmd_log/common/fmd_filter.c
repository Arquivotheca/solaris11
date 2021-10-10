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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/fm/protocol.h>

#include <strings.h>
#include <libgen.h>
#include <regex.h>
#include <libnvpair.h>

#include <fmd_log_impl.h>
#include <fmd_log.h>

/*ARGSUSED*/
int
fmd_log_filter_class(fmd_log_t *lp, const fmd_log_record_t *rp, void *arg)
{
	nvlist_t **nva;
	uint32_t i, size;
	char *class;

	if (gmatch(rp->rec_class, arg))
		return (1);

	/* return false if the record doesn't contain valid fault list */
	if (! gmatch(rp->rec_class, FM_LIST_EVENT ".*") ||
	    nvlist_lookup_uint32(rp->rec_nvl, FM_SUSPECT_FAULT_SZ,
	    &size) != 0 || size == 0 ||
	    nvlist_lookup_nvlist_array(rp->rec_nvl, FM_SUSPECT_FAULT_LIST,
	    &nva, &size) != 0)
		return (0);

	/* return true if any fault in the list matches */
	for (i = 0; i < size; i++) {
		if (nvlist_lookup_string(nva[i], FM_CLASS, &class) == 0 &&
		    gmatch(class, arg))
			return (1);
	}

	return (0);
}

/*ARGSUSED*/
int
fmd_log_filter_uuid(fmd_log_t *lp, const fmd_log_record_t *rp, void *arg)
{
	char *uuid;

	/*
	 * Note: the uuid filter matches *any* member whose name is 'uuid'.
	 * This permits us to match not only a list.suspect uuid but any
	 * other event that decides to embed uuids, too, using the same name.
	 */
	return (nvlist_lookup_string(rp->rec_nvl,
	    "uuid", &uuid) == 0 && strcmp(uuid, arg) == 0);
}

/*ARGSUSED*/
uint64_t
fm_ena_id_get(uint64_t ena)
{
	uint64_t id;

	switch (ENA_FORMAT(ena)) {
	case FM_ENA_FMT1:
		id = (ena & ENA_FMT1_ID_MASK) >> ENA_FMT1_ID_SHFT;
		break;
	case FM_ENA_FMT2:
		id = (ena & ENA_FMT2_ID_MASK) >> ENA_FMT2_ID_SHFT;
		break;
	default:
		id = 0;
	}

	return (id);
}

/*ARGSUSED*/
int
fmd_log_filter_ena(fmd_log_t *lp, const fmd_log_record_t *rp, void *arg)
{
	uint64_t ena_filter = *(uint64_t *)arg;
	uint64_t ena_rec;

	if (nvlist_lookup_uint64(rp->rec_nvl, "ena", &ena_rec) != 0)
		return (0);

	if (ENA_FORMAT(ena_rec) != ENA_FORMAT(ena_filter))
		return (0);

	if (fm_ena_id_get(ena_rec) != fm_ena_id_get(ena_filter))
		return (0);

	return (1);
}

/*ARGSUSED*/
int
fmd_log_filter_before(fmd_log_t *lp, const fmd_log_record_t *rp, void *arg)
{
	uint64_t sec = ((struct timeval *)arg)->tv_sec;
	uint64_t nsec = ((struct timeval *)arg)->tv_usec * (NANOSEC / MICROSEC);
	return (rp->rec_sec == sec ? rp->rec_nsec <= nsec : rp->rec_sec <= sec);
}

/*ARGSUSED*/
int
fmd_log_filter_after(fmd_log_t *lp, const fmd_log_record_t *rp, void *arg)
{
	uint64_t sec = ((struct timeval *)arg)->tv_sec;
	uint64_t nsec = ((struct timeval *)arg)->tv_usec * (NANOSEC / MICROSEC);
	return (rp->rec_sec == sec ? rp->rec_nsec >= nsec : rp->rec_sec >= sec);
}

/*ARGSUSED*/
int
fmd_log_filter_nv(fmd_log_t *lp, const fmd_log_record_t *rp, void *arg)
{
	fmd_log_filter_nvarg_t *argt = (fmd_log_filter_nvarg_t *)arg;
	char		*name = argt->nvarg_name;
	char		*value = argt->nvarg_value;
	regex_t		*value_regex = argt->nvarg_value_regex;
	nvpair_t	*nvp;
	int		ai;

	/* see if nvlist has named member */
	if (nvlist_lookup_nvpair_embedded_index(rp->rec_nvl, name,
	    &nvp, &ai, NULL) != 0)
		return (0);		/* name filter failure */

	/* check value match for matching nvpair */
	if ((value == NULL) ||
	    (nvpair_value_match_regex(nvp, ai, value, value_regex, NULL) == 1))
		return (1);		/* name/value filter pass */

	return (0);			/* value filter failure */
}
