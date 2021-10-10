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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/list.h>
#include <sys/io_tag.h>
#include <sys/sysmacros.h>

#ifdef	_KERNEL
#include <sys/kmem.h>
#include <sys/ksynch.h>
#else
#include <sys/zfs_context.h>
#endif


/* IO tag enable mask (bit offset is the tag value) */
#if defined(_KERNEL) || defined(__sparc)
uint32_t io_tag_type_enable = 0;
#else
uint32_t io_tag_type_enable = IO_TAG_TIMESTAMP | IO_TAG_STATE;
#endif


/* private per IO tag state */
typedef struct io_tag {
	const char	*it_label; /* label passed in */
	uint64_t	it_val; /* value passed in */
	uint64_t	it_timestamp; /* all tags get a timestamp */
	io_tag_type_t	it_type; /* tag type passed in */
	list_node_t	it_node;
} io_tag_t;

/* set of IO tags */
typedef struct io_tag_set {
	kmutex_t	tl_mutex;
	list_t		tl_list;
	uint_t		tl_cnt;
} io_tag_set_t;

/* global tag state */
typedef struct io_tag_state {
	kmem_cache_t	*ts_tag_set_cache;
	kmem_cache_t	*ts_tag_cache;
} io_tag_state_t;


io_tag_state_t	io_tag_state;


/*
 * io_tag_set_create()
 *    create a list to manage a set of IO tags
 */
static io_tag_set_t *
io_tag_set_create()
{
	io_tag_set_t *set;

	set = kmem_cache_alloc(io_tag_state.ts_tag_set_cache, KM_SLEEP);
	mutex_init(&set->tl_mutex, NULL, MUTEX_DRIVER, NULL);
	list_create(&set->tl_list, sizeof (io_tag_t),
	    offsetof(io_tag_t, it_node));
	set->tl_cnt = 0;

	return (set);
}


/*
 * io_tag_set_destroy()
 */
static void
io_tag_set_destroy(io_tag_set_t *set)
{
	list_destroy(&set->tl_list);
	mutex_destroy(&set->tl_mutex);
	kmem_cache_free(io_tag_state.ts_tag_set_cache, set);
}


/*
 * io_tag_alloc()
 */
static io_tag_t *
io_tag_alloc()
{
	io_tag_t *tag;
	tag = kmem_cache_alloc(io_tag_state.ts_tag_cache, KM_SLEEP);
	return (tag);
}


/*
 * io_tag_free()
 */
static void
io_tag_free(io_tag_t *tag)
{
	kmem_cache_free(io_tag_state.ts_tag_cache, tag);
}


/*
 * io_tag_add()
 */
static void
io_tag_add(io_tag_set_t *set, io_tag_t *tag)
{
	mutex_enter(&set->tl_mutex);
	list_insert_tail(&set->tl_list, tag);
	set->tl_cnt++;
	mutex_exit(&set->tl_mutex);
}


/*
 * io_tag()
 *    optionally record a tag on set of tags. If the set doesn't exist,
 *    create it first.
 */
void
io_tag(io_tags_t *tags, io_tag_type_t type, const char *label, uint64_t val)
{
	io_tag_set_t *set;
	io_tag_t *tag;


	/* if this tag type isn't enable, don't record this tag */
	if (((1 << type) & io_tag_type_enable) == 0)
		return;

	/*
	 * if this is the first tag in the set (io_tags_t), create a set to
	 * manage the tags. We want to be able to share a set of tags, or move
	 * the set around (i.e. NULL it in one data structure and pass it to
	 * another).
	 */
	set = *tags;
	if (set == NULL) {
		set = io_tag_set_create();
		*tags = set;
	}

	/* init the tag and add it to the set */
	tag = io_tag_alloc();
	tag->it_label = label;
	tag->it_val = val;
	tag->it_type = type;
	tag->it_timestamp = gethrtime();
	io_tag_add(set, tag);
}


/*
 * io_tag_destroy()
 *    destroy a set of tags, the individual tags and the set itself.
 */
void
io_tag_destroy(io_tags_t *tags)
{
	io_tag_set_t *set;
	io_tag_t *tag;

	/* check for NULL, since we can move a set of tags around */
	set = *tags;
	if (set == NULL)
		return;

	/* destroy all the tags */
	while ((tag = list_remove_head(&set->tl_list)) != NULL)
		io_tag_free(tag);

	/* destroy the tag set */
	io_tag_set_destroy(set);

	*tags = NULL;
}


/*
 * io_tag_init()
 *    initialize IO tag kmem caches
 */
void
io_tag_init()
{
	io_tag_state.ts_tag_set_cache = kmem_cache_create("io_tag_set_cache",
	    sizeof (io_tag_set_t), 64, NULL, NULL, NULL, NULL, NULL, 0);
	io_tag_state.ts_tag_cache = kmem_cache_create("io_tag_cache",
	    sizeof (struct io_tag), 64, NULL, NULL, NULL, NULL, NULL, 0);
}

/*
 * io_tag_fini()
 *    cleanup IO tag kmem caches
 */
void
io_tag_fini()
{
	kmem_cache_destroy(io_tag_state.ts_tag_cache);
	kmem_cache_destroy(io_tag_state.ts_tag_set_cache);
}
