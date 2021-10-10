/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * LEGAL NOTICE
 *
 * This file contains source code that implements the Sockets Direct
 * Protocol (SDP) as defined by the InfiniBand Architecture Specification,
 * Volume 1, Annex A4, Version 1.1.  Due to restrictions in the SDP license,
 * source code contained in this file may not be distributed outside of
 * Sun Microsystems without further legal review to ensure compliance with
 * the license terms.
 *
 * Sun employees and contactors are cautioned not to extract source code
 * from this file and use it for other purposes.  The SDP implementation
 * code in this and other files must be kept separate from all other source
 * code.
 *
 * As required by the license, the following notice is added to the source
 * code:
 *
 * This source code may incorporate intellectual property owned by
 * Microsoft Corporation.  Our provision of this source code does not
 * include any licenses or any other rights to you under any Microsoft
 * intellectual property.  If you would like a license from Microsoft
 * (e.g., to rebrand, redistribute), you need to contact Microsoft
 * directly.
 */

#include <sys/ib/clients/sdpib/sdp_main.h>
#include <sys/ib/clients/sdpib/sdp_trace_util.h>

static kmem_cache_t *sdp_generic_table = NULL;

/*
 * module specific functions
 */

/*
 * sdp_generic_table_get - get an element from a specific table
 */
static sdp_generic_t *
sdp_generic_table_get(sdp_generic_table_t *table, int32_t fifo)
{
	sdp_generic_t *element;

	SDP_CHECK_NULL(table, NULL);

	mutex_enter(&table->sgt_mutex);
	if (table->head == NULL) {
		mutex_exit(&table->sgt_mutex);
		return (NULL);
	}
	if (fifo) {
		element = table->head;
	} else {
		element = table->head->prev;
	}	/* else */

	if (element->next == element && element->prev == element) {
		table->head = NULL;
	} else {
		element->next->prev = element->prev;
		element->prev->next = element->next;

		table->head = element->next;
	}	/* else */

	table->size--;
	table->count[element->type] -=
	    ((SDP_GENERIC_TYPE_NONE > element->type) ? 1 : 0);

	element->next = NULL;
	element->prev = NULL;
	element->table = NULL;

	mutex_exit(&table->sgt_mutex);
	return (element);
}   /* sdp_generic_table_get */

/* ========================================================================= */

/*
 * sdp_generic_table_put - place an element into a specific table
 */
static void
sdp_generic_table_put(sdp_generic_table_t *table,
		sdp_generic_t *element, int32_t fifo)
{
	SDP_CHECK_NULL(table, -EINVAL);
	SDP_CHECK_NULL(element, -EINVAL);

	ASSERT(element->table == NULL);

	mutex_enter(&table->sgt_mutex);

	if (table->head ==  NULL) {
		element->next = element;
		element->prev = element;
		table->head = element;
	} else {
		element->next = table->head;
		element->prev = table->head->prev;

		element->next->prev = element;
		element->prev->next = element;

		if (fifo) {
			table->head = element;
		}
	}	/* else */

	table->size++;
	table->count[element->type] +=
	    ((SDP_GENERIC_TYPE_NONE > element->type) ? 1 : 0);
	element->table = table;
	mutex_exit(&table->sgt_mutex);
}

/*
 * public advertisment object functions for FIFO object table
 */

/* ========================================================================= */

/*
 * sdp_generic_table_remove - remove a specific element from a table
 */
void
sdp_generic_table_remove(sdp_generic_t *element)
{
	sdp_generic_table_t *table;
	sdp_generic_t *prev;
	sdp_generic_t *next;

	SDP_CHECK_NULL(element, -EINVAL);
	SDP_CHECK_NULL(element->table, -EINVAL);
	SDP_CHECK_NULL(element->next, -EINVAL);
	SDP_CHECK_NULL(element->prev, -EINVAL);

	table = element->table;

	mutex_enter(&table->sgt_mutex);

	if (element->next == element && element->prev == element) {
		table->head = NULL;
	} else {
		next = element->next;
		prev = element->prev;
		next->prev = prev;
		prev->next = next;

		if (table->head == element) {

			table->head = next;
		}
	}

	table->size--;
	table->count[element->type] -=
	    ((SDP_GENERIC_TYPE_NONE > element->type) ? 1 : 0);

	element->table = NULL;
	element->next = NULL;
	element->prev = NULL;

	mutex_exit(&table->sgt_mutex);
}

/* ========================================================================= */

/*
 * sdp_generic_table_lookup - search and return an element from the table
 */
sdp_generic_t *
sdp_generic_table_lookup(sdp_generic_table_t *table,
		sdp_generic_lookup_func_t lookup_func, void *arg)
{
	sdp_generic_t *element;
	int32_t counter;

	SDP_CHECK_NULL(table, NULL);
	SDP_CHECK_NULL(lookup_func, NULL);

	mutex_enter(&table->sgt_mutex);
	for (counter = 0, element = table->head; counter < table->size;
	    counter++, element = element->next) {
		if (lookup_func(element, arg) == 0) {
			mutex_exit(&table->sgt_mutex);
			return (element);
		}
	}

	mutex_exit(&table->sgt_mutex);
	return (NULL);
}

/* ========================================================================= */

/*
 * sdp_generic_table_get_all - get the element at the front of the table
 */
sdp_generic_t *
sdp_generic_table_get_all(sdp_generic_table_t *table)
{
	sdp_generic_t *head;

	mutex_enter(&table->sgt_mutex);
	head = table->head;

	table->head = NULL;
	table->size = 0;

	bzero(table->count, sizeof (table->count));
	mutex_exit(&table->sgt_mutex);

	return (head);
}

/* ========================================================================= */

/*
 * sdp_generic_table_get_head - get the element at the front of the table
 */
sdp_generic_t *
sdp_generic_table_get_head(sdp_generic_table_t *table)
{
	return (sdp_generic_table_get(table, B_TRUE));
}

/* ========================================================================= */

/*
 * sdp_generic_table_get_tail - get the element at the end of the table
 */
sdp_generic_t *
sdp_generic_table_get_tail(sdp_generic_table_t *table)
{
	return (sdp_generic_table_get(table, B_FALSE));
}

/* ========================================================================= */

/*
 * sdp_generic_table_put_head - place an element into the head of a table
 */
void
sdp_generic_table_put_head(sdp_generic_table_t *table, sdp_generic_t *element)
{
	sdp_generic_table_put(table, element, B_TRUE);
}

/* ========================================================================= */

/*
 * sdp_generic_table_put_tail - place an element into the tail of a table
 */
void
sdp_generic_table_put_tail(sdp_generic_table_t *table, sdp_generic_t *element)
{
	sdp_generic_table_put(table, element, B_FALSE);
}

/* ========================================================================= */

/*
 * sdp_generic_table_look_head - look at the front of the table
 */
sdp_generic_t *
sdp_generic_table_look_head(sdp_generic_table_t *table)
{
	SDP_CHECK_NULL(table, NULL);

	return (table->head);
}

/* ========================================================================= */

/*
 * sdp_generic_table_look_tail - look at the end of the table
 */
sdp_generic_t *
sdp_generic_table_look_tail(sdp_generic_table_t *table)
{
	sdp_generic_t *ret;

	SDP_CHECK_NULL(table, NULL);

	mutex_enter(&table->sgt_mutex);
	ret = (table->head == NULL) ? NULL : table->head->prev;
	mutex_exit(&table->sgt_mutex);

	return (ret);
}

/* ========================================================================= */

/*
 * sdp_generic_table_type_head - look at the type at the front of the table
 */
int32_t
sdp_generic_table_type_head(sdp_generic_table_t *table)
{
	int32_t ret;

	SDP_CHECK_NULL(table, -EINVAL);

	mutex_enter(&table->sgt_mutex);
	if (table->head == NULL) {

		ret = SDP_GENERIC_TYPE_NONE;
	} else {
		ret = table->head->type;
	}
	mutex_exit(&table->sgt_mutex);

	return (ret);
}

/* ========================================================================= */

/*
 * sdp_generic_table_type_tail - look at the type at the end of the table
 */
int32_t
sdp_generic_table_type_tail(sdp_generic_table_t *table)
{
	int32_t ret;

	SDP_CHECK_NULL(table, -EINVAL);

	mutex_enter(&table->sgt_mutex);
	if (table->head == NULL) {
		ret = SDP_GENERIC_TYPE_NONE;
	} else {
		ret = table->head->prev->type;
	}
	mutex_exit(&table->sgt_mutex);

	return (ret);
}

/* ========================================================================= */

/*
 * sdp_generic_table_look_type_head - look at a specific object
 */
sdp_generic_t *
sdp_generic_table_look_type_head(sdp_generic_table_t *table,
		sdp_generic_type_t type)
{
	sdp_generic_t *ret = NULL;

	SDP_CHECK_NULL(table, NULL);

	mutex_enter(&table->sgt_mutex);
	if (table->head == NULL) {
		ret = NULL;
	} else {
		ret = (type == table->head->type) ? table->head : NULL;
	}
	mutex_exit(&table->sgt_mutex);

	return (ret);
}

/* ========================================================================= */

/*
 * sdp_generic_table_look_type_tail - look at the type at the end of the
 * table
 */
sdp_generic_t *
sdp_generic_table_look_type_tail(sdp_generic_table_t *table,
		sdp_generic_type_t type)
{
	sdp_generic_t *ret = NULL;

	SDP_CHECK_NULL(table, NULL);

	mutex_enter(&table->sgt_mutex);
	if (table->head == NULL) {
		ret = NULL;
	} else {
		ret = (type == table->head->prev->type) ?
		    table->head->prev : NULL;
	}
	mutex_exit(&table->sgt_mutex);

	return (ret);
}

/* ========================================================================= */

/*
 * public table functions
 */

/* ========================================================================= */

/*
 * sdp_generic_table_create - create/allocate a generic table
 */
sdp_generic_table_t *
sdp_generic_table_create(int32_t *result)
{
	sdp_generic_table_t *table = NULL;

	SDP_CHECK_NULL(result, NULL);
	*result = -EINVAL;
	SDP_CHECK_NULL(sdp_generic_table, NULL);

	table = kmem_cache_alloc(sdp_generic_table, KM_SLEEP);
	bzero(table, sizeof (sdp_generic_table));
	if (table == NULL) {
		*result = -ENOMEM;
		return (NULL);
	}
	table->head = NULL;
	table->size = 0;

	bzero(table, sizeof (sdp_generic_table_t));

	*result = 0;
	return (table);
}   /* sdp_generic_table_create */

/* ========================================================================= */

/*
 * sdp_generic_table_init - initialize a new empty generic table
 */
int32_t
sdp_generic_table_init(sdp_generic_table_t *table)
{
	SDP_CHECK_NULL(table, -EINVAL);

	table->head = NULL;
	table->size = 0;

	bzero(table, sizeof (sdp_generic_table_t));
	mutex_init(&table->sgt_mutex, NULL, MUTEX_DRIVER, NULL);

	return (0);
}

void
sdp_generic_table_destroy(sdp_generic_table_t *table)
{
	sdp_generic_table_clear(table);
	mutex_destroy(&table->sgt_mutex);
}


/* ========================================================================= */

/*
 * sdp_generic_table_clear - clear the contents of a generic table
 */
void
sdp_generic_table_clear(sdp_generic_table_t *table)
{
	sdp_generic_t *element;

	SDP_CHECK_NULL(table, -EINVAL);

	/*
	 * drain the table of any objects
	 */
	while ((element = sdp_generic_table_get_head(table)) != NULL) {
		sdp_buff_free((sdp_buff_t *)element);
	}
	ASSERT(table->head == NULL && table->size == 0);
}

/* ARGSUSED */
static int
sdp_generic_table_constructor(void *buf, void *arg, int kmflags)
{
	sdp_generic_table_t *table = buf;

	mutex_init(&table->sgt_mutex, NULL, MUTEX_DRIVER, NULL);

	return (0);
}

/* ARGSUSED */
static void
sdp_generic_table_destructor(void *buf, void *arg)
{
	sdp_generic_table_t *table = buf;

	mutex_destroy(&table->sgt_mutex);
}
/* ========================================================================= */

/*
 * sdp_generic_main_init -- initialize the generic table caches.
 * creates a kmem_cache object that points to the static sdp_generic_table.
 */
int32_t
sdp_generic_main_init(void)
{

	int result = 0;

	/*
	 * initialize the caches only once.
	 */
	if (sdp_generic_table != NULL) {
		sdp_print_warn(NULL, "sdp_generic_table != NULL %p",
		    (void *)sdp_generic_table);
		return (-EINVAL);
	}

	/*
	 * constructors/destructors
	 */
	sdp_generic_table = kmem_cache_create("sdp_generic_table",
	    sizeof (sdp_generic_table_t), 0, sdp_generic_table_constructor,
	    sdp_generic_table_destructor, NULL, NULL, 0, KM_SLEEP);
	if (sdp_generic_table == NULL) {
		result = -ENOMEM;
	}
	return (result);
}

/* ========================================================================= */

/* ..sdp_generic_main_cleanup -- cleanup the generic table caches. */
int32_t
sdp_generic_main_cleanup(void) {
	/*
	 * cleanup the caches
	 */
	kmem_cache_destroy(sdp_generic_table);
	/*
	 * null out entries.
	 */
	sdp_generic_table = NULL;

	return (0);
}

/*
 * Return the number of elements in the table
 */
int32_t
sdp_generic_table_size(sdp_generic_table_t *table)
{
	SDP_CHECK_NULL(table, -EINVAL);

	return (table->size);
}   /* sdp_generic_table_size */

/*
 * Return non-zero if element is in a table
 */
int32_t
sdp_generic_table_member(sdp_generic_t *element)
{
	SDP_CHECK_NULL(element, -EINVAL);

	return ((element->table == NULL) ? 0 : 1);
}   /* sdp_generic_table_member */
