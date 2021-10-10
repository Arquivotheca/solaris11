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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#include	<libintl.h>
#include	<sys/types.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<sys/param.h>
#include	<unistd.h>
#include	<users.h>
#include	<userdefs.h>
#include	"messages.h"

extern void exit();
extern char *strtok();

static struct group_entry **grplist;
static int ngroups_max = 0;
extern int _getgroupsbymember(const char *, gid_t[], int, int);

static struct group_entry *
create_group_entry(char *group_name, gid_t gid)
{
	struct group_entry *entry = malloc(sizeof (struct group_entry));

	if (entry == NULL) {
		errmsg(M_MEM_ALLOCATE);
		exit(EX_FAILURE);
	}

	entry->gid = gid;
	entry->group_name = isalldigit(group_name) ? NULL : strdup(group_name);

	return (entry);
}

/* Validate a list of groups */
struct group_entry **
valid_lgroup(char *list, gid_t gid, sec_repository_t *rep,
	nss_XbyY_buf_t *b)
{
	int n_invalid = 0, i = 0, j;
	char *ptr;
	struct group *g_ptr;
	int warning;
	int dup_prim = 0; /* we don't duplicate our primary as a supplemental */

	if (!list || !*list)
		return ((struct group_entry **)NULL);

	if (ngroups_max == 0) {
		ngroups_max = sysconf(_SC_NGROUPS_MAX);
		grplist = malloc((ngroups_max + 1) *
		    sizeof (struct group_entry *));
		if (grplist == NULL) {
			errmsg(M_MEM_ALLOCATE);
			exit(EX_FAILURE);
		}
	}

	while (ptr = strtok(((i || n_invalid || dup_prim)? NULL: list), ",")) {

		switch (valid_group_check(ptr, &g_ptr, &warning, rep, b)) {
		case INVALID:
			errmsg(M_INVALID, ptr, "group id");
			n_invalid++;
			break;
		case TOOBIG:
			errmsg(M_TOOBIG, "gid", ptr);
			n_invalid++;
			break;
		case UNIQUE:
			errmsg(M_GRP_NOTUSED, ptr);
			n_invalid++;
			break;
		case NOTUNIQUE:
			/* ignore duplicated primary */
			if (g_ptr->gr_gid == gid) {
				if (!dup_prim)
					dup_prim++;
				continue;
			}

			if (!i) {
				grplist[i++] = create_group_entry(
				    g_ptr->gr_name, g_ptr->gr_gid);
			} else {
				/* Keep out duplicates */
				for (j = 0; j < i; j++)
					if (g_ptr->gr_gid == grplist[j]->gid &&
					    strcmp(g_ptr->gr_name,
					    grplist[j]->group_name) == 0) {
						break;
					}

				if (j == i) {
					/* Not a duplicate */
					grplist[i++] = create_group_entry(
					    g_ptr->gr_name, g_ptr->gr_gid);
				}
			}
			break;

		}
		if (warning)
			warningmsg(warning, ptr);

		if (i >= ngroups_max) {
			errmsg(M_MAXGROUPS, ngroups_max);
			break;
		}
	}

	/* Terminate the list */
	grplist[i] = NULL;

	if (n_invalid)
		exit(EX_BADARG);

	return ((struct group_entry **)grplist);
}
/*
 * Perform add/remove (+/-) operation specified by opchar
 * Inputs
 *	gidlist - input gid list
 *	name - user name
 *	repo - repository
 *	opchar - +/-
 * Returns
 *	0 - Success
 *	-1 - error
 */

int
update_gids(struct group_entry **gidlist, char *name, char opchar)
{
	struct group *gr;
	static gid_t *groups = NULL;
	static gid_t *updgids = NULL;
	int ngroups;
	int deletecnt;
	int groupcnt;
	int i;
	int j;
	int k;

	if (opchar == OP_REPLACE_CHAR) {
		return (0); /* nothing to do */
	}

	if (groups == NULL) {
		groups = (gid_t *)calloc((uint_t)ngroups_max, sizeof (gid_t));
		updgids = (gid_t *)calloc((uint_t)ngroups_max, sizeof (gid_t));

		if (groups == NULL || updgids == NULL) {
			errmsg(M_MEM_ALLOCATE);
			exit(EX_FAILURE);
		}
	}

	/* Get current groups */
	ngroups = _getgroupsbymember(name, groups, ngroups_max, 1);

	if (opchar == OP_ADD_CHAR) {
		if (ngroups == 1) {
			return (0); /* nothing to do if no groups */
		}

		for (i = 1; i < ngroups; i++) {
			j = 0;
			while (j < ngroups_max && gidlist[j] != NULL) {
				if (gidlist[j]->gid == groups[i]) {
					break; /* match found */
				}
				j++;
			}
			if (j == ngroups_max) {
				return (-1); /* overflow */
			}

			if (gidlist[j] != NULL) {
				continue; /* skip duplicates */
			}

			if ((gr = getgrgid(groups[j])) == NULL) {
				return (-1); /* can't find group? */
			}

			gidlist[j++] = create_group_entry(
			    gr->gr_name, gr->gr_gid);
			gidlist[j] = NULL;
		}
	} else {
		if (ngroups == 1) {
			return (-1); /* Removing from empty group */
		}

		/* Count how may groups to delete */
		groupcnt = 0;
		while (gidlist[groupcnt] != NULL) {
			groupcnt++;
		}

		k = 0;
		deletecnt = 0;
		for (i = 1; i < ngroups; i++) {
			j = 0;
			for (j = 0; j < ngroups_max && gidlist[j] != NULL;
			    j++) {
				if (gidlist[j]->gid == groups[i]) {
					deletecnt++;
					break; /* skip matching one */
				}
			}

			if (j == ngroups_max || gidlist[j] == NULL) {
				/* Not found, copy it */
				updgids[k++] = groups[i];
			}
		}

		if (groupcnt != deletecnt) {
			return (-1);
		}

		/* Update the gidlist */
		for (i = 0; i < k; i++) {

			if ((gr = getgrgid(updgids[i])) == NULL) {
				return (-1); /* can't find group? */
			}

			if (gidlist[i] != NULL) {
				free(gidlist[i]);
				gidlist[i] = create_group_entry(
				    gr->gr_name, gr->gr_gid);
			}
		}
		gidlist[i] = NULL;
	}

	return (0);
}
