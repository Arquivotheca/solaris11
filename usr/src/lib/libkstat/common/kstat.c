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
 * Copyright (c) 1988, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <memory.h>
#include <strings.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include "kstat.h"

/*LINTLIBRARY*/

/*
 * Use a hash for the kstat records we have read in.  This is done
 * to reduce the number of records that have to be looked up when
 * dealing with a large configuration system.  Note this hash is
 * internal to libkstat only.  The pointer to the hash is associated
 * with the kstat_ctl_t data structure.  When the kstat_ctl_t is free, the
 * hash is removed.
 */
struct kstat_hashed_records {
		char *module_name;
		char *ks_name;
		int instance;
		int allocated;
		kstat_t *ksp;
		struct kstat_hashed_records *next_entry;
	};

#define	KSTAT_HASHED_ENTRIES 8192
#define	KSP_INST_HASH(x)  ((x) & (KSTAT_HASHED_ENTRIES-1))
static void kstat_hashed_add_ksp(kstat_t *, kstat_ctl_t *);

static int
string_path_hash(char *path)

{
	int val = 0;
	int index = 0;

	if (!path)
		return (0);

	/*
	 * To force a good hashing of device paths, we need
	 * to scatter the hash around based on the character
	 * placement in the string.
	 * If the character is not an integer value, we simply
	 * multiply it by the position value in the string.
	 * If the character is an integer, then we use 1 of
	 * 4 different calculations based on the position
	 * the string.  If we do not do this, then things like
	 * 1 and 2 can end up having the same hash position.
	 * Simple usage of a long or short value is no good
	 * because the length of names can get quite long.
	 */
	while (path[index]) {
		if (path[index] < '0' || path[index] > '9') {
			val += ((int)path[index])*(index+1);
			index++;
			continue;
		}
		switch (index & 3) {
			case 0:
				val += ((int)path[index])*(index+1);
			break;
			case 1:
				val += ((int)path[index])*(index+1)*3;
			break;
			case 2:
				val = val * ((int)path[index])*(index+1);
			break;
			case 3:
				val = val + val/((int)path[index]);
			break;
		}
		index++;
	}
	return (val);
}

static int
kstat_record_hash(char *name, char *module, int instance, char **tmp_name,
    int *allocated)

{
	int val;
	char *tmp;
/*
 * Extra chars in the string that we need to allow for
 * spaces, underscores and others.
 */
#define	EXTRA_CHARS 12

	if (!name || name[0] == '\0') {
		int len;

		if (!module || instance < 0)
			return (-1);
		/*
		 * Need to build a temporary string to use.
		 */
		len = strlen(module)+EXTRA_CHARS;
again:
		*tmp_name = tmp = malloc(len);
		if (!tmp)
			return (-1);
		*allocated = 1;
		snprintf(tmp, len, "%s_%d", module, instance);
	} else {
		*tmp_name = tmp = name;
	}

	val = string_path_hash(tmp);
	val += string_path_hash(module);
	if (instance > 0)
		val += KSP_INST_HASH(instance);

	if (val < 0)
		val = val * -1;
	val = (val & (KSTAT_HASHED_ENTRIES - 1));

	return (val);
}


static kstat_t *
kstat_hash_search(char *ks_module, int ks_instance, char *ks_name,
    kstat_ctl_t *kc)
{
	kstat_t *ksp;
	struct kstat_hashed_records *hash_entry;
	struct kstat_hashed_records **hash_head;
	int hash_index;
	char *tmp_name;
	int allocated = 0;

	hash_index = kstat_record_hash(ks_name, ks_module, ks_instance,
	    &tmp_name, &allocated);
	/*
	 * If the entry was allocated free it up we do not care
	 * about it here.
	 */
	if (allocated)
		free(tmp_name);

	if (hash_index >= 0 &&
	    (hash_head = (struct kstat_hashed_records **)kc->kc_private)) {
		hash_entry = hash_head[hash_index];
		while (hash_entry) {
			if ((!ks_module || (strcmp(hash_entry->module_name,
			    ks_module)) == 0) && ks_name &&
			    (strcmp(hash_entry->ks_name, ks_name) == 0)) {
				if (ks_instance < 0 || hash_entry->instance ==
				    ks_instance) {
					/* Entry matches, return. */
					return (hash_entry->ksp);
				}
			}
			hash_entry = hash_entry->next_entry;
		}
	}

out:
	/*
	 * We did not find the entry in the hash table.  Check the kstat_t list
	 * and see if it is there.  If it is add the entry to the hash.
	 */
	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
		if ((ksp->ks_instance == ks_instance) &&
		    (ks_module && strcmp(ksp->ks_module, ks_module) == 0) &&
		    (ks_name && strcmp(ksp->ks_name, ks_name) == 0)) {
			kstat_hashed_add_ksp(ksp, kc);
			return (ksp);
		}
	}

	errno = ENOENT;
	return (NULL);
}

static void
kstat_hashed_add_ksp(kstat_t *ksp, kstat_ctl_t *kc)

{
	struct kstat_hashed_records *ks_entry;
	int hash_index;
	struct kstat_hashed_records **hash_head;
	char *tmp_name;
	int allocated = 0;

	if (!ksp->ks_module || !ksp->ks_instance)
		return;

	if (!kc->kc_private) {
		kc->kc_private = (void **) (hash_head =
		    (struct kstat_hashed_records **)
		    calloc(KSTAT_HASHED_ENTRIES,
		    sizeof (struct kstat_hashed_records **)));

		if (!kc->kc_private)
			return;
	} else {
		hash_head = (struct kstat_hashed_records **)kc->kc_private;
	}


	hash_index = kstat_record_hash(ksp->ks_name, ksp->ks_module,
	    ksp->ks_instance, &tmp_name, &allocated);
	if (hash_index < 0)
		return;
	ks_entry = malloc(sizeof (struct kstat_hashed_records));
	if (!ks_entry)
		return;
	ks_entry->ksp = ksp;
	ks_entry->instance = ksp->ks_instance;
	ks_entry->module_name = ksp->ks_module;
	if (ksp->ks_name[0] != NULL) {
		ks_entry->ks_name = ksp->ks_name;
	} else  {
		ks_entry->ks_name = tmp_name;
	}
	ks_entry->next_entry = hash_head[hash_index];
	ks_entry->allocated = allocated;
	hash_head[hash_index] = ks_entry;
}

static void
kstat_hashed_remove_ksp(kstat_t *ksp, kstat_ctl_t *kc)

{
	int hash_index;
	struct kstat_hashed_records *hash_entry, *prev_entry = NULL;
	struct kstat_hashed_records **hash_head;
	char *tmp_name;
	int allocated = 0;

	if (!kc->kc_private || !ksp)
		return;

	hash_index = kstat_record_hash(ksp->ks_name, ksp->ks_module,
	    ksp->ks_instance, &tmp_name, &allocated);

	/*
	 * If hash_index < 0 then there is not enough information in the ksp to
	 * be able to figure out what to remove.
	 */
	if (hash_index < 0)
		return;

	hash_head = (struct kstat_hashed_records **)kc->kc_private;
	hash_entry = hash_head[hash_index];
	/* Walk the hash chain for the ksp. */
	while (hash_entry) {
		if (hash_entry->ksp == ksp)
			break;
		prev_entry = hash_entry;
		hash_entry = hash_entry->next_entry;
	}
	/* If we found an entry, then remove it. */
	if (hash_entry) {
		if (!prev_entry) {
			hash_head[hash_index] =
			    hash_head[hash_index]->next_entry;
		} else {
			prev_entry->next_entry = hash_entry->next_entry;
		}
		if (hash_entry->allocated)
			free(hash_entry->ks_name);
		free(hash_entry);
	}
}

static void
kstat_zalloc(void **ptr, size_t size, int free_first)
{
	if (free_first)
		free(*ptr);
	*ptr = calloc(size, 1);
}

static void
kstat_chain_free(kstat_ctl_t *kc)
{
	kstat_t *ksp, *nksp;

	ksp = kc->kc_chain;
	while (ksp) {
		nksp = ksp->ks_next;
		kstat_hashed_remove_ksp(ksp, kc);
		free(ksp->ks_data);
		free(ksp);
		ksp = nksp;
	}
	/* Free the kc_private data if present. */
	if (kc->kc_private) {
		free(kc->kc_private);
		kc->kc_private =  (void **) NULL;
	}
	kc->kc_chain = NULL;
	kc->kc_chain_id = 0;
}

kstat_ctl_t *
kstat_open(void)
{
	kstat_ctl_t *kc;
	int kd;

	kd = open("/dev/kstat", O_RDONLY);
	if (kd == -1)
		return (NULL);
	kstat_zalloc((void **)&kc, sizeof (kstat_ctl_t), 0);
	if (kc == NULL)
		return (NULL);

	kc->kc_kd = kd;
	kc->kc_chain = NULL;
	kc->kc_chain_id = 0;
	if (kstat_chain_update(kc) == -1) {
		int saved_err = errno;
		(void) kstat_close(kc);
		errno = saved_err;
		return (NULL);
	}
	return (kc);
}

int
kstat_close(kstat_ctl_t *kc)
{
	int rc;

	kstat_chain_free(kc);
	rc = close(kc->kc_kd);
	free(kc);
	return (rc);
}

kid_t
kstat_read(kstat_ctl_t *kc, kstat_t *ksp, void *data)
{
	kid_t kcid;

	if (ksp->ks_data == NULL && ksp->ks_data_size > 0) {
		kstat_zalloc(&ksp->ks_data, ksp->ks_data_size, 0);
		if (ksp->ks_data == NULL)
			return (-1);
	}
	while ((kcid = (kid_t)ioctl(kc->kc_kd, KSTAT_IOC_READ, ksp)) == -1) {
		if (errno == EAGAIN) {
			(void) poll(NULL, 0, 100);	/* back off a moment */
			continue;			/* and try again */
		}
		/*
		 * Mating dance for variable-size kstats.
		 * You start with a buffer of a certain size,
		 * which you hope will hold all the data.
		 * If your buffer is too small, the kstat driver
		 * returns ENOMEM and sets ksp->ks_data_size to
		 * the current size of the kstat's data.  You then
		 * resize your buffer and try again.  In practice,
		 * this almost always converges in two passes.
		 */
		if (errno == ENOMEM && (ksp->ks_flags & KSTAT_FLAG_VAR_SIZE)) {
			kstat_zalloc(&ksp->ks_data, ksp->ks_data_size, 1);
			if (ksp->ks_data != NULL)
				continue;
		}
		return (-1);
	}
	if (data != NULL) {
		(void) memcpy(data, ksp->ks_data, ksp->ks_data_size);
		if (ksp->ks_type == KSTAT_TYPE_NAMED &&
		    ksp->ks_data_size !=
		    ksp->ks_ndata * sizeof (kstat_named_t)) {
			/*
			 * Has KSTAT_DATA_STRING fields. Fix the pointers.
			 */
			uint_t i;
			kstat_named_t *knp = data;

			for (i = 0; i < ksp->ks_ndata; i++, knp++) {
				if (knp->data_type != KSTAT_DATA_STRING)
					continue;
				if (KSTAT_NAMED_STR_PTR(knp) == NULL)
					continue;
				/*
				 * The offsets of the strings within the
				 * buffers are the same, so add the offset of
				 * the string to the beginning of 'data' to fix
				 * the pointer so that strings in 'data' don't
				 * point at memory in 'ksp->ks_data'.
				 */
				KSTAT_NAMED_STR_PTR(knp) = (char *)data +
				    (KSTAT_NAMED_STR_PTR(knp) -
				    (char *)ksp->ks_data);
			}
		}
	}
	return (kcid);
}

kid_t
kstat_write(kstat_ctl_t *kc, kstat_t *ksp, void *data)
{
	kid_t kcid;

	if (ksp->ks_data == NULL && ksp->ks_data_size > 0) {
		kstat_zalloc(&ksp->ks_data, ksp->ks_data_size, 0);
		if (ksp->ks_data == NULL)
			return (-1);
	}
	if (data != NULL) {
		(void) memcpy(ksp->ks_data, data, ksp->ks_data_size);
		if (ksp->ks_type == KSTAT_TYPE_NAMED) {
			kstat_named_t *oknp = data;
			kstat_named_t *nknp = KSTAT_NAMED_PTR(ksp);
			uint_t i;

			for (i = 0; i < ksp->ks_ndata; i++, oknp++, nknp++) {
				if (nknp->data_type != KSTAT_DATA_STRING)
					continue;
				if (KSTAT_NAMED_STR_PTR(nknp) == NULL)
					continue;
				/*
				 * The buffer passed in as 'data' has string
				 * pointers that point within 'data'.  Fix the
				 * pointers so they point into the same offset
				 * within the newly allocated buffer.
				 */
				KSTAT_NAMED_STR_PTR(nknp) =
				    (char *)ksp->ks_data +
				    (KSTAT_NAMED_STR_PTR(oknp) - (char *)data);
			}
		}

	}
	while ((kcid = (kid_t)ioctl(kc->kc_kd, KSTAT_IOC_WRITE, ksp)) == -1) {
		if (errno == EAGAIN) {
			(void) poll(NULL, 0, 100);	/* back off a moment */
			continue;			/* and try again */
		}
		break;
	}
	return (kcid);
}

/*
 * If the current KCID is the same as kc->kc_chain_id, return 0;
 * if different, update the chain and return the new KCID.
 * This operation is non-destructive for unchanged kstats.
 */
kid_t
kstat_chain_update(kstat_ctl_t *kc)
{
	kstat_t k0, *headers, *oksp, *nksp, **okspp, *next;
	int i;
	kid_t kcid;

	kcid = (kid_t)ioctl(kc->kc_kd, KSTAT_IOC_CHAIN_ID, NULL);
	if (kcid == -1)
		return (-1);
	if (kcid == kc->kc_chain_id)
		return (0);

	/*
	 * kstat 0's data is the kstat chain, so we can get the chain
	 * by doing a kstat_read() of this kstat.  The only fields the
	 * kstat driver needs are ks_kid (this identifies the kstat),
	 * ks_data (the pointer to our buffer), and ks_data_size (the
	 * size of our buffer).  By zeroing the struct we set ks_data = NULL
	 * and ks_data_size = 0, so that kstat_read() will automatically
	 * determine the size and allocate space for us.  We also fill in the
	 * name, so that truss can print something meaningful.
	 */
	bzero(&k0, sizeof (k0));
	(void) strcpy(k0.ks_name, "kstat_headers");

	kcid = kstat_read(kc, &k0, NULL);
	if (kcid == -1) {
		free(k0.ks_data);
		/* errno set by kstat_read() */
		return (-1);
	}
	headers = k0.ks_data;

	/*
	 * Chain the new headers together
	 */
	for (i = 1; i < k0.ks_ndata; i++)
		headers[i - 1].ks_next = &headers[i];

	headers[k0.ks_ndata - 1].ks_next = NULL;

	/*
	 * Remove all deleted kstats from the chain.
	 */
	nksp = headers;
	okspp = &kc->kc_chain;
	oksp = kc->kc_chain;
	while (oksp != NULL) {
		next = oksp->ks_next;
		if (nksp != NULL && oksp->ks_kid == nksp->ks_kid) {
			okspp = &oksp->ks_next;
			nksp = nksp->ks_next;
		} else {
			kstat_hashed_remove_ksp(oksp, kc);
			*okspp = oksp->ks_next;
			free(oksp->ks_data);
			free(oksp);
		}
		oksp = next;
	}

	/*
	 * Add all new kstats to the chain.
	 */
	while (nksp != NULL) {
		kstat_zalloc((void **)okspp, sizeof (kstat_t), 0);
		if ((oksp = *okspp) == NULL) {
			free(headers);
			return (-1);
		}
		*oksp = *nksp;
		okspp = &oksp->ks_next;
		oksp->ks_next = NULL;
		oksp->ks_data = NULL;
		kstat_hashed_add_ksp(oksp, kc);
		nksp = nksp->ks_next;
	}

	free(headers);
	kc->kc_chain_id = kcid;
	return (kcid);
}

kstat_t *
kstat_lookup(kstat_ctl_t *kc, char *ks_module, int ks_instance, char *ks_name)
{
	kstat_t *ksp;

	if (ks_instance != -1 && ks_module && ks_name) {
		return (kstat_hash_search(ks_module, ks_instance, ks_name, kc));
	}
	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
		if ((ks_instance == -1 || ksp->ks_instance == ks_instance) &&
		    (ks_module == NULL ||
		    strcmp(ksp->ks_module, ks_module) == 0) &&
		    (ks_name == NULL || strcmp(ksp->ks_name, ks_name) == 0)) {
			/* Attempt to add the entry to the hash. */
			kstat_hashed_add_ksp(ksp, kc);
			return (ksp);
		}
	}

	errno = ENOENT;
	return (NULL);
}

void *
kstat_data_lookup(kstat_t *ksp, char *name)
{
	int i, size;
	char *namep, *datap;

	switch (ksp->ks_type) {

	case KSTAT_TYPE_NAMED:
		size = sizeof (kstat_named_t);
		namep = KSTAT_NAMED_PTR(ksp)->name;
		break;

	case KSTAT_TYPE_TIMER:
		size = sizeof (kstat_timer_t);
		namep = KSTAT_TIMER_PTR(ksp)->name;
		break;

	default:
		errno = EINVAL;
		return (NULL);
	}

	datap = ksp->ks_data;
	for (i = 0; i < ksp->ks_ndata; i++) {
		if (strcmp(name, namep) == 0)
			return (datap);
		namep += size;
		datap += size;
	}
	errno = ENOENT;
	return (NULL);
}
