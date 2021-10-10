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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "statcommon.h"
#include "dsr.h"
#include <sys/dklabel.h>
#include <sys/dktp/fdisk.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>

static void insert_iodev(struct snapshot *ss, struct iodev_snapshot *iodev);
static void get_path_info(struct iodev_snapshot *io, char *mod, size_t modlen,
    int *type, int *inst, char *name, size_t size);
static int disk_or_partition(enum iodev_type type);

extern struct snapshot_state ss_gl_info;

static struct iodev_snapshot *
make_controller(int cid)
{
	struct iodev_snapshot *new;

	new = safe_alloc(sizeof (struct iodev_snapshot));
	(void) memset(new, 0, sizeof (struct iodev_snapshot));
	new->is_type = IODEV_CONTROLLER;
	new->is_id.id = cid;
	new->is_parent_id.id = IODEV_NO_ID;

	(void) snprintf(new->is_name, sizeof (new->is_name), "c%d", cid);

	return (new);
}

static struct iodev_snapshot *
find_iodev_by_name(struct iodev_snapshot *list, const char *name)
{
	struct iodev_snapshot *pos;
	struct iodev_snapshot *pos2;

	for (pos = list; pos; pos = pos->is_next) {
		if (strcmp(pos->is_name, name) == 0)
			return (pos);

		pos2 = find_iodev_by_name(pos->is_children, name);
		if (pos2 != NULL)
			return (pos2);
	}

	return (NULL);
}

static enum iodev_type
parent_iodev_type(enum iodev_type type)
{
	switch (type) {
		case IODEV_CONTROLLER: return (0);
		case IODEV_IOPATH_LT: return (0);
		case IODEV_IOPATH_LI: return (0);
		case IODEV_NFS: return (0);
		case IODEV_TAPE: return (0);
		case IODEV_IOPATH_LTI: return (IODEV_DISK);
		case IODEV_DISK: return (IODEV_CONTROLLER);
		case IODEV_PARTITION: return (IODEV_DISK);
	}
	return (IODEV_UNKNOWN);
}

static int
id_match(struct iodev_id *id1, struct iodev_id *id2)
{
	return (id1->id == id2->id &&
	    strcmp(id1->tid, id2->tid) == 0);
}

static struct iodev_snapshot *
find_parent(struct snapshot *ss, struct iodev_snapshot *iodev)
{
	enum iodev_type parent_type = parent_iodev_type(iodev->is_type);
	struct iodev_snapshot *pos;
	struct iodev_snapshot *pos2;

	if (parent_type == 0 || parent_type == IODEV_UNKNOWN)
		return (NULL);

	if (iodev->is_parent_id.id == IODEV_NO_ID &&
	    iodev->is_parent_id.tid[0] == '\0')
		return (NULL);

	if (parent_type == IODEV_CONTROLLER) {
		for (pos = ss->s_iodevs; pos; pos = pos->is_next) {
			if (pos->is_type != IODEV_CONTROLLER)
				continue;
			if (pos->is_id.id != iodev->is_parent_id.id)
				continue;
			return (pos);
		}

		if (!(ss->s_types & SNAP_CONTROLLERS))
			return (NULL);

		pos = make_controller(iodev->is_parent_id.id);
		insert_iodev(ss, pos);
		return (pos);
	}

	/* IODEV_DISK parent */
	for (pos = ss->s_iodevs; pos; pos = pos->is_next) {
		if (id_match(&iodev->is_parent_id, &pos->is_id) &&
		    pos->is_type == IODEV_DISK)
			return (pos);
		if (pos->is_type != IODEV_CONTROLLER)
			continue;
		for (pos2 = pos->is_children; pos2; pos2 = pos2->is_next) {
			if (pos2->is_type != IODEV_DISK)
				continue;
			if (id_match(&iodev->is_parent_id, &pos2->is_id))
				return (pos2);
		}
	}

	return (NULL);
}

/*
 * Introduce an index into the list to speed up insert_into looking for the
 * right position in the list. This index is an AVL tree of all the
 * iodev_snapshot in the list.
 */

#define	offsetof(s, m)	(size_t)(&(((s *)0)->m))	/* for avl_create */

static int
avl_iodev_cmp(const void* is1, const void* is2)
{
	int c = iodev_cmp((struct iodev_snapshot *)is1,
	    (struct iodev_snapshot *)is2);

	if (c > 0)
		return (1);

	if (c < 0)
		return (-1);

	return (0);
}

static void
ix_new_list(struct iodev_snapshot *elem)
{
	avl_tree_t *l = malloc(sizeof (avl_tree_t));

	elem->avl_list = l;
	if (l == NULL)
		return;

	avl_create(l, avl_iodev_cmp, sizeof (struct iodev_snapshot),
	    offsetof(struct iodev_snapshot, avl_link));

	avl_add(l, elem);
}

static void
ix_list_del(struct iodev_snapshot *elem)
{
	avl_tree_t *l = elem->avl_list;

	if (l == NULL)
		return;

	elem->avl_list = NULL;

	avl_remove(l, elem);
	if (avl_numnodes(l) == 0) {
		avl_destroy(l);
		SAFE_FREE(l, sizeof (avl_tree_t));
	}
}

static void
ix_insert_here(struct iodev_snapshot *pos, struct iodev_snapshot *elem, int ba)
{
	avl_tree_t *l = pos->avl_list;
	elem->avl_list = l;

	if (l == NULL)
		return;

	avl_insert_here(l, elem, pos, ba);
}

static void
list_del(struct iodev_snapshot **list, struct iodev_snapshot *pos)
{
	ix_list_del(pos);

	if (*list == pos)
		*list = pos->is_next;
	if (pos->is_next)
		pos->is_next->is_prev = pos->is_prev;
	if (pos->is_prev)
		pos->is_prev->is_next = pos->is_next;
	pos->is_prev = pos->is_next = NULL;
}

static void
insert_before(struct iodev_snapshot **list, struct iodev_snapshot *pos,
    struct iodev_snapshot *new)
{
	if (pos == NULL) {
		new->is_prev = new->is_next = NULL;
		*list = new;
		ix_new_list(new);
		return;
	}

	new->is_next = pos;
	new->is_prev = pos->is_prev;
	if (pos->is_prev)
		pos->is_prev->is_next = new;
	else
		*list = new;
	pos->is_prev = new;

	ix_insert_here(pos, new, AVL_BEFORE);
}

static void
insert_after(struct iodev_snapshot **list, struct iodev_snapshot *pos,
    struct iodev_snapshot *new)
{
	if (pos == NULL) {
		new->is_prev = new->is_next = NULL;
		*list = new;
		ix_new_list(new);
		return;
	}

	new->is_next = pos->is_next;
	new->is_prev = pos;
	if (pos->is_next)
		pos->is_next->is_prev = new;
	pos->is_next = new;

	ix_insert_here(pos, new, AVL_AFTER);
}

static void
insert_into(struct iodev_snapshot **list, struct iodev_snapshot *iodev)
{
	struct iodev_snapshot *tmp = *list;
	avl_tree_t *l;
	void *p;
	avl_index_t where;

	/*
	 * The iodev record information is treated as being persistent
	 * across snaps.  Most of the data we use will not change unless
	 * the snapshot changes.
	 */
	if (!iodev->have_devinfo) {
		int	type;
		int	inst;

		get_path_info(iodev, iodev->cnst_mod, sizeof (iodev->cnst_mod),
		    &type, &inst, iodev->cnst_name, sizeof (iodev->cnst_name));
		iodev->have_devinfo = 1;
		iodev->cnst_type = type;
		iodev->cnst_inst = inst;
		iodev->cnst_disk = disk_or_partition(type);
	}

	if (*list == NULL) {
		*list = iodev;
		ix_new_list(iodev);
		return;
	}

	/*
	 * Optimize the search: instead of walking the entire list
	 * (which can contain thousands of nodes), search in the AVL
	 * tree the nearest node and reposition the startup point to
	 * this node rather than always starting from the beginning
	 * of the list.
	 */
	l = tmp->avl_list;
	if (l != NULL) {
		p = avl_find(l, iodev, &where);
		if (p == NULL) {
			p = avl_nearest(l, where, AVL_BEFORE);
		}
		if (p != NULL) {
			tmp = (struct iodev_snapshot *)p;
		}
	}

	for (;;) {
		if (iodev_cmp(tmp, iodev) > 0) {
			insert_before(list, tmp, iodev);
			return;
		}

		if (tmp->is_next == NULL)
			break;

		tmp = tmp->is_next;
	}

	insert_after(list, tmp, iodev);
}

static int
disk_or_partition(enum iodev_type type)
{
	return (type == IODEV_DISK || type == IODEV_PARTITION);
}

static int
disk_or_partition_or_iopath_or_tape(enum iodev_type type)
{
	return (type == IODEV_DISK || type == IODEV_PARTITION ||
	    type == IODEV_IOPATH_LTI || type == IODEV_TAPE);
}

static void
insert_iodev(struct snapshot *ss, struct iodev_snapshot *iodev)
{
	struct iodev_snapshot *parent = find_parent(ss, iodev);
	struct iodev_snapshot **list;

	if (parent != NULL) {
		list = &parent->is_children;
		parent->is_nr_children++;
	} else {
		list = &ss->s_iodevs;
		ss->s_nr_iodevs++;
	}

	insert_into(list, iodev);
}

/* return 1 if dev passes filter */
static int
iodev_match(struct iodev_snapshot *dev, struct iodev_filter *df)
{
	int	is_floppy = (strncmp(dev->is_name, "fd", 2) == 0);
	char	*isn, *ispn, *ifn;
	char	*path;
	int	ifnl;
	size_t	i;

	/* no filter, pass */
	if (df == NULL)
		return (1);		/* pass */

	/* no filtered names, pass if not floppy and skipped */
	if (df->if_nr_names == NULL)
		return (!(df->if_skip_floppy && is_floppy));

	isn = dev->is_name;
	ispn = dev->is_pretty;
	for (i = 0; i < df->if_nr_names; i++) {
		ifn = df->if_names[i];
		ifnl = strlen(ifn);
		path = strchr(ifn, '.');

		if ((strcmp(isn, ifn) == 0) ||
		    (ispn && (strcmp(ispn, ifn) == 0)))
			return (1);	/* pass */

		/* if filter is a path allow partial match */
		if (path &&
		    ((strncmp(isn, ifn, ifnl) == 0) ||
		    (ispn && (strncmp(ispn, ifn, ifnl) == 0))))
			return (1);	/* pass */
	}

	return (0);			/* fail */
}

/* return 1 if path is an mpxio path associated with dev */
static int
iodev_path_match(struct iodev_snapshot *dev, struct iodev_snapshot *path)
{
	char	*dn, *pn;
	int	dnl;

	dn = dev->is_name;
	pn = path->is_name;
	dnl = strlen(dn);

	if ((pn[dnl] == '.') && (strncmp(pn, dn, dnl) == 0))
		return (1);			/* yes */

	return (0);				/* no */
}

/* select which I/O devices to collect stats for */
static void
choose_iodevs(struct snapshot *ss, struct iodev_snapshot *iodevs,
    struct iodev_filter *df, int select)
{
	struct iodev_snapshot	*pos, *ppos, *tmp, *ptmp;
	int			nr_iodevs;
	int			nr_iodevs_orig;

	nr_iodevs = df ? df->if_max_iodevs : UNLIMITED_IODEVS;
	nr_iodevs_orig = nr_iodevs;

	if (nr_iodevs == UNLIMITED_IODEVS)
		nr_iodevs = INT_MAX;

	/*
	 * First time through we will end up walking the entire list.
	 * After that,unless the snap changes we simply move the pieces
	 * we want to the list in use.
	 */
	pos = iodevs;
	while (pos && nr_iodevs) {
		tmp = pos;
		pos = pos->is_next;
		if (!select) {
			if (!iodev_match(tmp, df)) {
				ss_gl_info.io_records[tmp->io_index].record = 0;
				continue;	/* failed full match */
			}
			/*
			 * Mark this io record as being cared about.  If we care
			 * about it this interval then we will care about it the
			 * next interval.
			 */
			ss_gl_info.io_records[tmp->io_index].record = 1;
		} else {
			if (!ss_gl_info.io_records[tmp->io_index].record) {
				continue;
			}
		}

		list_del(&iodevs, tmp);
		insert_iodev(ss, tmp);

		/*
		 * Add all mpxio paths associated with match above. Added
		 * paths don't count against nr_iodevs.
		 */
		if (strchr(tmp->is_name, '.') == NULL) {
			ppos = iodevs;
			while (ppos) {
				ptmp = ppos;
				ppos = ppos->is_next;

				if (select &&
				    !ss_gl_info.
				    io_records[ptmp->io_index].record)
					continue;
				if (!iodev_path_match(tmp, ptmp))
					continue;	/* not an mpxio path */
				ss_gl_info.io_records[ptmp->io_index].record
				    = 1;
				list_del(&iodevs, ptmp);
				insert_iodev(ss, ptmp);
				if (pos == ptmp)
					pos = ppos;
			}
		}

		nr_iodevs--;
	}

	/*
	 * If we had a filter, and *nothing* passed the filter then we
	 * don't want to fill the  remaining slots - it is just confusing
	 * if we don that, it makes it look like the filter code is broken.
	 */
	if ((df->if_nr_names == NULL) || (nr_iodevs != nr_iodevs_orig)) {
		/* now insert any iodevs into the remaining slots */
		pos = iodevs;
		while (pos && nr_iodevs) {
			tmp = pos;
			pos = pos->is_next;

			if (select &&
			    !ss_gl_info.io_records[tmp->io_index].record)
				continue;
			if (df && df->if_skip_floppy &&
			    strncmp(tmp->is_name, "fd", 2) == 0)
				continue;

			ss_gl_info.io_records[tmp->io_index].record = 1;
			list_del(&iodevs, tmp);
			insert_iodev(ss, tmp);

			--nr_iodevs;
		}
	}

	/* clear the unwanted ones */
	pos = iodevs;
	while (pos) {
		struct iodev_snapshot *tmp = pos;
		pos = pos->is_next;
		free_iodev(tmp);
	}
}

static int
collate_controller(struct iodev_snapshot *controller,
    struct iodev_snapshot *disk)
{
	controller->is_stats.nread += disk->is_stats.nread;
	controller->is_stats.nwritten += disk->is_stats.nwritten;
	controller->is_stats.reads += disk->is_stats.reads;
	controller->is_stats.writes += disk->is_stats.writes;
	controller->is_stats.wtime += disk->is_stats.wtime;
	controller->is_stats.wlentime += disk->is_stats.wlentime;
	controller->is_stats.rtime += disk->is_stats.rtime;
	controller->is_stats.rlentime += disk->is_stats.rlentime;
	controller->is_crtime += disk->is_crtime;
	controller->is_snaptime += disk->is_snaptime;
	if (kstat_add(&disk->is_errors, &controller->is_errors))
		return (errno);
	return (0);
}

static int
acquire_iodev_stats(struct iodev_snapshot *list, kstat_ctl_t *kc)
{
	struct iodev_snapshot *pos;
	int err = 0;

	for (pos = list; pos; pos = pos->is_next) {
		/* controllers don't have stats (yet) */
		if (pos->is_ksp != NULL) {
			if (kstat_read(kc, pos->is_ksp, &pos->is_stats) == -1)
				return (errno);
			/* make sure crtime/snaptime is updated */
			pos->is_crtime = pos->is_ksp->ks_crtime;
			pos->is_snaptime = pos->is_ksp->ks_snaptime;
		}

		if ((err = acquire_iodev_stats(pos->is_children, kc)))
			return (err);

		if (pos->is_type == IODEV_CONTROLLER) {
			struct iodev_snapshot *pos2 = pos->is_children;

			for (; pos2; pos2 = pos2->is_next) {
				if ((err = collate_controller(pos, pos2)))
					return (err);
			}
		}
	}

	return (0);
}

static int
acquire_iodev_errors(struct snapshot *ss, kstat_ctl_t *kc)
{
	kstat_t *ksp;

	if (!(ss->s_types && SNAP_IODEV_ERRORS))
		return (0);

	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
		char kstat_name[KSTAT_STRLEN];
		char *dname = kstat_name;
		char *ename = ksp->ks_name;
		struct iodev_snapshot *iodev;

		if (ksp->ks_type != KSTAT_TYPE_NAMED)
			continue;
		if (strncmp(ksp->ks_class, "device_error", 12) != 0 &&
		    strncmp(ksp->ks_class, "iopath_error", 12) != 0)
			continue;

		/*
		 * Some drivers may not follow the naming convention
		 * for error kstats (i.e., drivername,err) so
		 * be sure we don't walk off the end.
		 */
		while (*ename && *ename != ',') {
			*dname = *ename;
			dname++;
			ename++;
		}
		*dname = '\0';

		iodev = find_iodev_by_name(ss->s_iodevs, kstat_name);

		if (iodev == NULL)
			continue;

		if (kstat_read(kc, ksp, NULL) == -1)
			return (errno);
		if (kstat_copy(ksp, &iodev->is_errors) == -1)
			return (errno);
	}

	return (0);
}

static void
get_ids(struct iodev_snapshot *iodev, const char *pretty)
{
	int ctr, disk, slice, ret;
	char *target;
	const char *p1;
	const char *p2;

	if (pretty == NULL)
		return;

	if (sscanf(pretty, "c%d", &ctr) != 1)
		return;

	p1 = pretty;
	while (*p1 && *p1 != 't')
		++p1;

	if (!*p1)
		return;
	++p1;

	p2 = p1;
	while (*p2 && *p2 != 'd')
		++p2;

	if (!*p2 || p2 == p1)
		return;

	target = safe_alloc(1 + p2 - p1);
	(void) strlcpy(target, p1, 1 + p2 - p1);

	ret = sscanf(p2, "d%d%*[sp]%d", &disk, &slice);

	if (ret == 2 && iodev->is_type == IODEV_PARTITION) {
		iodev->is_id.id = slice;
		iodev->is_parent_id.id = disk;
		(void) strlcpy(iodev->is_parent_id.tid, target, KSTAT_STRLEN);
	} else if (ret == 1) {
		if (iodev->is_type == IODEV_DISK) {
			iodev->is_id.id = disk;
			(void) strlcpy(iodev->is_id.tid, target, KSTAT_STRLEN);
			iodev->is_parent_id.id = ctr;
		} else if (iodev->is_type == IODEV_IOPATH_LTI) {
			iodev->is_parent_id.id = disk;
			(void) strlcpy(iodev->is_parent_id.tid,
			    target, KSTAT_STRLEN);
		}
	}

	SAFE_FREE(target, 1);
}

static void
get_pretty_name(enum snapshot_types types, struct iodev_snapshot *iodev,
	kstat_ctl_t *kc)
{
	disk_list_t	*dl;
	char		*pretty = NULL;

	if (iodev->is_type == IODEV_NFS) {
		if (!(types & SNAP_IODEV_PRETTY))
			return;

		iodev->is_pretty = lookup_nfs_name(iodev->is_name, kc);
		return;
	}

	/* lookup/translate the kstat name */
	dl = lookup_ks_name(iodev->is_name, (types & SNAP_IODEV_DEVID) ? 1 : 0);
	if (dl == NULL)
		return;

	if (dl->dsk)
		pretty = safe_strdup(dl->dsk);

	if (types & SNAP_IODEV_PRETTY) {
		if (dl->dname)
			iodev->is_dname = safe_strdup(dl->dname);
	}

	if (dl->devidstr)
		iodev->is_devid = safe_strdup(dl->devidstr);

	get_ids(iodev, pretty);

	/*
	 * we fill in pretty name wether it is asked for or not because
	 * it could be used in a filter by match_iodevs.
	 */
	iodev->is_pretty = pretty;
}

static enum iodev_type
get_iodev_type(kstat_t *ksp)
{
	if (strcmp(ksp->ks_class, "disk") == 0)
		return (IODEV_DISK);
	if (strcmp(ksp->ks_class, "partition") == 0)
		return (IODEV_PARTITION);
	if (strcmp(ksp->ks_class, "nfs") == 0)
		return (IODEV_NFS);
	if (strcmp(ksp->ks_class, "iopath") == 0)
		return (IODEV_IOPATH_LTI);
	if (strcmp(ksp->ks_class, "tape") == 0)
		return (IODEV_TAPE);
	return (IODEV_UNKNOWN);
}

/* get the lun/target/initiator from the name, return 1 on success */
static int
get_lti(char *s,
	char *lname, int *l, char *tname, int *t, char *iname, int *i)
{
	int  num = 0;

	num = sscanf(s, "%[a-z]%d%*[.]%[a-z]%d%*[.]%[a-z_]%d", lname, l,
	    tname, t, iname, i);
	return ((num == 6) ? 1 : 0);
}

static void
get_path_info(struct iodev_snapshot *io, char *mod, size_t modlen, int *type,
    int *inst, char *name, size_t size)
{

	/*
	 * If it is iopath or ssd then pad the name with i/t/l so we can sort
	 * by alpha order and set type for IOPATH to DISK since we want to
	 * have it grouped with its ssd parent. The lun can be 5 digits,
	 * the target can be 4 digits, and the initiator can be 3 digits and
	 * the padding is done appropriately for string comparisons.
	 */
	if (disk_or_partition_or_iopath_or_tape(io->is_type)) {
		int i1, t1, l1;
		char tname[KSTAT_STRLEN], iname[KSTAT_STRLEN];
		char *ptr, lname[KSTAT_STRLEN];

		i1 = t1 = l1 = 0;
		(void) get_lti(io->is_name, lname, &l1, tname, &t1, iname, &i1);
		*type = io->is_type;
		if (io->is_type == IODEV_DISK) {
			(void) snprintf(name, size, "%s%05d", lname, l1);
		} else if (io->is_type == IODEV_TAPE) {
			(void) snprintf(name, size, "%s%05d", lname, l1);
			/* set to disk so we sort with disks */
			*type = IODEV_DISK;
		} else if (io->is_type == IODEV_PARTITION) {
			ptr = strchr(io->is_name, ',');
			(void) snprintf(name, size, "%s%05d%s", lname, l1, ptr);
		} else {
			(void) snprintf(name, size, "%s%05d.%s%04d.%s%03d",
			    lname, l1, tname, t1, iname, i1);
			/* set to disk so we sort with disks */
			*type = IODEV_DISK;
		}
		(void) strlcpy(mod, lname, modlen);
		*inst = l1;
	} else {
		(void) strlcpy(mod, io->is_module, modlen);
		(void) strlcpy(name, io->is_name, size);
		*type = io->is_type;
		*inst = io->is_instance;
	}
}

int
iodev_cmp(struct iodev_snapshot *io1, struct iodev_snapshot *io2)
{
	int	rc;

	if (!io1->cnst_disk || !io2->cnst_disk) {
		/*
		 * In case one of the compared devs is neither disk
		 * nor partition we sort it using type value.
		 * Please note that the other dev may still be a disk
		 * or partition according to get_path_info logic.
		 */

		rc = io1->cnst_type - io2->cnst_type;
		if (rc)
			return (rc);
	}

	/* controller doesn't have ksp */
	if (io1->is_ksp && io2->is_ksp) {
		if ((rc = strcmp(io1->cnst_mod, io2->cnst_mod)) != 0)
			return (rc);
		rc = io1->cnst_inst - io2->cnst_inst;
		if (rc)
			return (rc);
	} else {
		rc = io1->is_id.id - io2->is_id.id;
		if (rc)
			return (rc);
	}

	return (strcmp(io1->cnst_name, io2->cnst_name));
}

/* update the target reads and writes */
static void
update_target(struct iodev_snapshot *tgt, struct iodev_snapshot *path)
{
	tgt->is_stats.reads += path->is_stats.reads;
	tgt->is_stats.writes += path->is_stats.writes;
	tgt->is_stats.nread += path->is_stats.nread;
	tgt->is_stats.nwritten += path->is_stats.nwritten;
	tgt->is_stats.wcnt += path->is_stats.wcnt;
	tgt->is_stats.rcnt += path->is_stats.rcnt;

	/*
	 * Stash the t_delta in the crtime for use in show_disk
	 * NOTE: this can't be done in show_disk because the
	 * itl entry is removed for the old format
	 */
	tgt->is_crtime += hrtime_delta(path->is_crtime, path->is_snaptime);
	tgt->is_snaptime += path->is_snaptime;
	tgt->is_nr_children += 1;
}

/*
 * Create a new synthetic device entry of the specified type. The supported
 * synthetic types are IODEV_IOPATH_LT and IODEV_IOPATH_LI.
 */

static struct iodev_snapshot *
make_extended_device(int type, struct iodev_snapshot *old)
{
	struct iodev_snapshot	*tptr = NULL;
	char			*ptr, *iptr;
	int			lun, tgt, initiator;
	int			pretty_len, ptr_len;
	char			lun_name[KSTAT_STRLEN];
	char			tgt_name[KSTAT_STRLEN];
	char			ctl_name[KSTAT_STRLEN];
	char			initiator_name[KSTAT_STRLEN];

	if (old == NULL)
		return (NULL);
	if (get_lti(old->is_name,
	    lun_name, &lun, tgt_name, &tgt, initiator_name, &initiator) != 1) {
		return (NULL);
	}
	tptr = safe_alloc(sizeof (*old));
	bzero(tptr, sizeof (*old));
	if (old->is_pretty != NULL) {
		pretty_len = strlen(old->is_pretty);
		tptr->is_pretty = safe_alloc(pretty_len + 1);
		(void) strlcpy(tptr->is_pretty, old->is_pretty, pretty_len + 1);
	}
	bcopy(&old->is_parent_id, &tptr->is_parent_id,
	    sizeof (old->is_parent_id));

	tptr->is_type = type;

	if (type == IODEV_IOPATH_LT) {
		/* make new synthetic entry that is the LT */
		/* set the id to the target id */
		tptr->is_id.id = tgt;
		(void) snprintf(tptr->is_id.tid, sizeof (tptr->is_id.tid),
		    "%s%d", tgt_name, tgt);
		(void) snprintf(tptr->is_name, sizeof (tptr->is_name),
		    "%s%d.%s%d", lun_name, lun, tgt_name, tgt);

		if (old->is_pretty) {
			ptr = strrchr(tptr->is_pretty, '.');
			if (ptr)
				*ptr = '\0';
		}
	} else if (type == IODEV_IOPATH_LI) {
		/* make new synthetic entry that is the LI */
		/* set the id to the initiator number */
		tptr->is_id.id = initiator;
		(void) snprintf(tptr->is_id.tid, sizeof (tptr->is_id.tid),
		    "%s%d", initiator_name, initiator);
		(void) snprintf(tptr->is_name, sizeof (tptr->is_name),
		    "%s%d.%s%d", lun_name, lun, initiator_name, initiator);

		if (old->is_pretty != NULL) {
			ptr = strchr(tptr->is_pretty, '.');
			if (ptr != NULL) {
				ptr_len = pretty_len - (ptr - tptr->is_pretty);
				/* We need to get the c# from old name */
				iptr = strrchr(tptr->is_pretty, '.');
				if (iptr != ptr) {
					iptr ++; /* skip the '.' */
					(void) strlcpy(ctl_name, iptr,
					    KSTAT_STRLEN);
					(void) strlcpy(ptr + 1, ctl_name,
					    ptr_len - 1);
				} else {
					(void) snprintf(ptr + 1, ptr_len - 1,
					    "%s%d", initiator_name, initiator);
				}
			}
		}
	}
	return (tptr);
}

/*
 * This is to get the original -X LI format (e.g. ssd1.fp0). When an LTI kstat
 * is found - traverse the children looking for the same initiator and sum
 * them up. Add an LI entry and delete all of the LTI entries with the same
 * initiator.
 */
static int
create_li_delete_lti(struct snapshot *ss, struct iodev_snapshot *list)
{
	struct iodev_snapshot	*pos, *entry, *parent;
	int			lun, tgt, initiator;
	char			lun_name[KSTAT_STRLEN];
	char			tgt_name[KSTAT_STRLEN];
	char			initiator_name[KSTAT_STRLEN];
	int			err;

	for (entry = list; entry; entry = entry->is_next) {
		if ((err = create_li_delete_lti(ss, entry->is_children)) != 0)
			return (err);

		if (entry->is_type == IODEV_IOPATH_LTI) {
			parent = find_parent(ss, entry);
			if (get_lti(entry->is_name, lun_name, &lun,
			    tgt_name, &tgt, initiator_name, &initiator) != 1) {
				return (1);
			}

			pos = (parent == NULL) ? NULL : parent->is_children;
			for (; pos; pos = pos->is_next) {
				if (pos->is_id.id != -1 &&
				    pos->is_id.id == initiator &&
				    pos->is_type == IODEV_IOPATH_LI) {
					/* found the same initiator */
					update_target(pos, entry);
					list_del(&parent->is_children, entry);
					free_iodev(entry);
					parent->is_nr_children--;
					entry = pos;
					break;
				}
			}

			if (!pos) {
				/* make the first LI entry */
				pos = make_extended_device(
				    IODEV_IOPATH_LI, entry);
				update_target(pos, entry);

				if (parent) {
					insert_before(&parent->is_children,
					    entry, pos);
					list_del(&parent->is_children, entry);
					free_iodev(entry);
				} else {
					insert_before(&ss->s_iodevs, entry,
					    pos);
					list_del(&ss->s_iodevs, entry);
					free_iodev(entry);
				}
				entry = pos;
			}
		}
	}
	return (0);
}

/*
 * We have the LTI kstat, now add an entry for the LT that sums up all of
 * the LTI's with the same target(t).
 */
static int
create_lt(struct snapshot *ss, struct iodev_snapshot *list)
{
	struct iodev_snapshot	*entry, *parent, *pos;
	int			lun, tgt, initiator;
	char			lun_name[KSTAT_STRLEN];
	char			tgt_name[KSTAT_STRLEN];
	char			initiator_name[KSTAT_STRLEN];
	int			err;

	for (entry = list; entry; entry = entry->is_next) {
		if ((err = create_lt(ss, entry->is_children)) != 0)
			return (err);

		if (entry->is_type == IODEV_IOPATH_LTI) {
			parent = find_parent(ss, entry);
			if (get_lti(entry->is_name, lun_name, &lun,
			    tgt_name, &tgt, initiator_name, &initiator) != 1) {
				return (1);
			}

			pos = (parent == NULL) ? NULL : parent->is_children;
			for (; pos; pos = pos->is_next) {
				if (pos->is_id.id != -1 &&
				    pos->is_id.id == tgt &&
				    pos->is_type == IODEV_IOPATH_LT) {
					/* found the same target */
					update_target(pos, entry);
					break;
				}
			}

			if (!pos) {
				pos = make_extended_device(
				    IODEV_IOPATH_LT, entry);
				update_target(pos, entry);

				if (parent) {
					insert_before(&parent->is_children,
					    entry, pos);
					parent->is_nr_children++;
				} else {
					insert_before(&ss->s_iodevs,
					    entry, pos);
				}
			}
		}
	}
	return (0);
}

/* Find the longest is_name field to aid formatting of output */
static int
iodevs_is_name_maxlen(struct iodev_snapshot *list)
{
	struct iodev_snapshot	*entry;
	int			max = 0, cmax, len;

	for (entry = list; entry; entry = entry->is_next) {
		cmax = iodevs_is_name_maxlen(entry->is_children);
		max = (cmax > max) ? cmax : max;
		len = strlen(entry->is_name);
		max = (len > max) ? len : max;
	}
	return (max);
}

int
acquire_iodevs(struct snapshot *ss, kstat_ctl_t *kc, struct iodev_filter *df)
{
	kstat_t	*ksp;
	struct	iodev_snapshot *pos;
	struct	iodev_snapshot *list = NULL;
	struct io_information *tptr;
	int	err = 0, nrecs = 0, ndevs = 0, select;

	/*
	 * Call cleanup_iodevs_snapshot() so that a cache miss in
	 * lookup_ks_name() will result in a fresh snapshot.
	 */
	if (ss->snap_changed || !ss_gl_info.io_records) {
		select = 0;
		ss->s_nr_iodevs = 0;
		ss->s_iodevs_is_name_maxlen = 0;
		cleanup_iodevs_snapshot();

		if (ss_gl_info.io_records) {
			for (ndevs = 0; ndevs < ss_gl_info.io_records_cnt;
			    ndevs++) {
				tptr = &ss_gl_info.io_records[ndevs];
				SAFE_FREE(tptr->pos.is_pretty, sizeof (char *));
				SAFE_FREE(tptr->pos.is_dname, sizeof (char *));
				SAFE_FREE(tptr->pos.is_devid, sizeof (char *));
			}
			SAFE_FREE(ss_gl_info.io_records,
			    sizeof (struct io_information));
		}

		ndevs = 0;

		for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
			enum iodev_type type;

			if (ksp->ks_type != KSTAT_TYPE_IO) {
				continue;
			}

			/* e.g. "usb_byte_count" is not handled */
			if ((type = get_iodev_type(ksp)) == IODEV_UNKNOWN)
				continue;

			if (df && !(type & df->if_allowed_types))
				continue;
			nrecs++;
		}

		if (nrecs) {
			ss_gl_info.io_records = (struct io_information *)
			    calloc(1, nrecs * sizeof (struct io_information));
			if (!ss_gl_info.io_records) {
				err = errno;
				goto out;
			}
		} else {
			ss_gl_info.io_records = NULL;
			goto out;
		}

		for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
			enum iodev_type type;

			if (ksp->ks_type != KSTAT_TYPE_IO) {
				continue;
			}

			/* e.g. "usb_byte_count" is not handled */
			if ((type = get_iodev_type(ksp)) == IODEV_UNKNOWN)
				continue;

			if (df && !(type & df->if_allowed_types))
				continue;

			if ((pos = calloc(1, sizeof (struct iodev_snapshot)))
			    == NULL) {
				err = errno;
				goto out;
			}

			ss_gl_info.io_records[ndevs].io_rec_ptr = ksp;

			pos->io_index = ndevs;
			pos->is_type = type;
			pos->is_crtime = ksp->ks_crtime;
			pos->is_snaptime = ksp->ks_snaptime;
			pos->is_id.id = IODEV_NO_ID;
			pos->is_parent_id.id = IODEV_NO_ID;
			pos->is_ksp = ksp;
			pos->is_instance = ksp->ks_instance;

			(void) strlcpy(pos->is_module, ksp->ks_module,
			    KSTAT_STRLEN);
			(void) strlcpy(pos->is_name, ksp->ks_name,
			    KSTAT_STRLEN);
			get_pretty_name(ss->s_types, pos, kc);
			/*
			 * Save the data to be used in future snaps.
			 */
			bcopy(pos, &ss_gl_info.io_records[ndevs].pos,
			    sizeof (struct iodev_snapshot));
			/*
			 * Now handle the strings.
			 */
			pos->is_pretty = safe_strdup(
			    ss_gl_info.io_records[ndevs].pos.is_pretty);
			pos->is_dname = safe_strdup(
			    ss_gl_info.io_records[ndevs].pos.is_dname);
			pos->is_devid = safe_strdup(
			    ss_gl_info.io_records[ndevs].pos.is_devid);
			ndevs++;

			/*
			 * We must insert in sort order so e.g. vmstat -l
			 * chooses in order.
			 */
			insert_into(&list, pos);
		}
		ss_gl_info.io_records_cnt = ndevs;
	} else {
		select = 1;
		for (ndevs = 0; ndevs < ss_gl_info.io_records_cnt; ndevs++) {
			enum iodev_type type;

			/*
			 * Check to see if we care about this record. Skip
			 * the record if we do not care about it.
			 */
			if (!ss_gl_info.io_records[ndevs].record)
				continue;
			ksp = ss_gl_info.io_records[ndevs].io_rec_ptr;
			if ((type = get_iodev_type(ksp)) == IODEV_UNKNOWN)
				continue;
			if (df && !(type & df->if_allowed_types))
				continue;

			/*
			 * Create a shadow entry for the record, and populate it
			 * accordingly.
			 */
			if ((pos = calloc(1, sizeof (struct iodev_snapshot)))
			    == NULL) {
				err = errno;
				goto out;
			}

			bcopy(&ss_gl_info.io_records[ndevs].pos, pos,
			    sizeof (struct iodev_snapshot));
			/*
			 * Set the stuff that changes.
			 */
			pos->is_crtime = ksp->ks_crtime;
			pos->is_snaptime = ksp->ks_snaptime;
			pos->is_instance = ksp->ks_instance;
			pos->is_pretty = safe_strdup(
			    ss_gl_info.io_records[ndevs].pos.is_pretty);
			pos->is_dname = safe_strdup(
			    ss_gl_info.io_records[ndevs].pos.is_dname);
			pos->is_devid = safe_strdup(
			    ss_gl_info.io_records[ndevs].pos.is_devid);

			/*
			 * We must insert in sort order so e.g. vmstat -l
			 * chooses in order.
			 */
			insert_into(&list, pos);
		}
	}

	choose_iodevs(ss, list, df, select);

	/* before acquire_stats for collate_controller()'s benefit */
	if (ss->s_types & SNAP_IODEV_ERRORS) {
		if ((err = acquire_iodev_errors(ss, kc)) != 0)
			goto out;
	}

	if ((err = acquire_iodev_stats(ss->s_iodevs, kc)) != 0)
		goto out;

	if (ss->s_types & SNAP_IOPATHS_LTI) {
		/*
		 * -Y: kstats are LTI, need to create a synthetic LT
		 * for -Y output.
		 */
		if ((err = create_lt(ss, ss->s_iodevs)) != 0) {
			return (err);
		}
	}
	if (ss->s_types & SNAP_IOPATHS_LI) {
		/*
		 * -X: kstats are LTI, need to create a synthetic LI and
		 * delete the LTI for -X output
		 */
		if ((err = create_li_delete_lti(ss, ss->s_iodevs)) != 0) {
			return (err);
		}
	}

	/* determine width of longest is_name */
	ss->s_iodevs_is_name_maxlen = iodevs_is_name_maxlen(ss->s_iodevs);

	err = 0;
out:
	return (err);
}

void
free_iodev(struct iodev_snapshot *iodev)
{
	while (iodev->is_children) {
		struct iodev_snapshot *tmp = iodev->is_children;
		iodev->is_children = iodev->is_children->is_next;
		free_iodev(tmp);
	}

	if (iodev->avl_list) {
		avl_remove(iodev->avl_list, iodev);
		if (avl_numnodes(iodev->avl_list) == 0) {
			avl_destroy(iodev->avl_list);
			SAFE_FREE(iodev->avl_list, sizeof (avl_tree_t));
		}
	}

	SAFE_FREE(iodev->is_errors.ks_data, sizeof (iodev->is_errors.ks_data));
	SAFE_FREE(iodev->is_pretty, sizeof (char *));
	SAFE_FREE(iodev->is_dname, sizeof (char *));
	SAFE_FREE(iodev->is_devid, sizeof (char *));
	SAFE_FREE(iodev, sizeof (struct iodev_snapshot *));
}
