/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *	description:	SDP zcopy advertisement table. used to manage buffer
 *			advertisements made by the peer. double linked list.
 *			FIFO table.
 */
#include <sys/ib/clients/sdpib/sdp_main.h>

static kmem_cache_t *sdp_conn_advt_cache = NULL;
static kmem_cache_t *sdp_conn_advt_table = NULL;

/*
 * public advertisement object functions for FIFO object tablei
 */

/* ========================================================================= */

/*
 * sdp_conn_advt_create - create an advertisement object
 */
sdp_advt_t *
sdp_conn_advt_create(void)
{
	sdp_advt_t *advt;

	SDP_CHECK_NULL(sdp_conn_advt_cache, NULL);

	advt = kmem_cache_alloc(sdp_conn_advt_cache, KM_SLEEP);

	if (advt != NULL) {
		advt->next = NULL;
		advt->prev = NULL;
		advt->size = 0;
		advt->post = 0;
		advt->addr = 0;
		advt->rkey = TS_IB_HANDLE_INVALID;

		advt->type = SDP_GENERIC_TYPE_ADVT;
		advt->release =
		    (sdp_gen_destruct_fn_t *)sdp_conn_advt_destroy;
	}
	return (advt);
}   /* sdp_conn_advt_create */

/* ========================================================================= */

/*
 * sdp_conn_advt_destroy - destroy an advertisement object
 */
int32_t
sdp_conn_advt_destroy(sdp_advt_t *advt)
{
	SDP_CHECK_NULL(advt, -EINVAL);

	if (advt->next != NULL || advt->prev != NULL)
		return (-EACCES);

	/*
	 * return the object to its cache
	 */
	kmem_cache_free(sdp_conn_advt_cache, advt);

	return (0);
}   /* sdp_conn_advt_destroy */

/* ========================================================================= */

/*
 * sdp_conn_advt_table_get - get, and remove, the object at the tables head
 */
sdp_advt_t *
sdp_conn_advt_table_get(sdp_advt_table_t *table)
{
	sdp_advt_t *advt;
	sdp_advt_t *next;
	sdp_advt_t *prev;

	SDP_CHECK_NULL(table, NULL);

	mutex_enter(&table->sat_mutex);

	if ((advt = table->head) == NULL) {
		mutex_exit(&table->sat_mutex);
		return (NULL);
	}

	if (advt->next == advt && advt->prev == advt) {
		table->head = NULL;
	} else {
		next = advt->next;
		prev = advt->prev;
		next->prev = prev;
		prev->next = next;

		table->head = next;
	}	/* else */

	table->size--;

	advt->next = NULL;
	advt->prev = NULL;

	mutex_exit(&table->sat_mutex);

	return (advt);
}   /* sdp_conn_advt_table_get */

/* ========================================================================= */

/*
 * sdp_conn_advt_table_look - get, without removing, the object at the head
 */
sdp_advt_t *
sdp_conn_advt_table_look(sdp_advt_table_t *table)
{
	SDP_CHECK_NULL(table, NULL);

	return (table->head);
}   /* sdp_conn_advt_table_look */

/* ========================================================================= */

/*
 * sdp_conn_advt_table_put - put the advertisement object at the tables tail
 */
int32_t
sdp_conn_advt_table_put(sdp_advt_table_t *table, sdp_advt_t *advt)
{
	sdp_advt_t *next;
	sdp_advt_t *prev;

	SDP_CHECK_NULL(table, -EINVAL);
	SDP_CHECK_NULL(advt, -EINVAL);

	mutex_enter(&table->sat_mutex);

	if (table->head == NULL) {
		advt->next = advt;
		advt->prev = advt;
		table->head = advt;
	} else {
		next = table->head;
		prev = next->prev;

		prev->next = advt;
		advt->prev = prev;
		advt->next = next;
		next->prev = advt;
	}	/* else */

	table->size++;

	mutex_exit(&table->sat_mutex);

	return (0);
}   /* sdp_conn_advt_table_put */

/* --------------------------------------------------------------------- */


/* public table functions						*/


/* --------------------------------------------------------------------- */

/* ========================================================================= */

/*
 * sdp_conn_advt_table_create - create an advertisement table
 */
sdp_advt_table_t *
sdp_conn_advt_table_create(int32_t *result)
{
	sdp_advt_table_t *table = NULL;

	SDP_CHECK_NULL(result, NULL);
	*result = -EINVAL;
	SDP_CHECK_NULL(sdp_conn_advt_table, NULL);

	table = kmem_cache_alloc(sdp_conn_advt_table, KM_SLEEP);
	if (table == NULL) {
		*result = -ENOMEM;
		return (NULL);
	}
	table->head = NULL;
	table->size = 0;

	*result = 0;
	return (table);
}   /* sdp_conn_advt_table_create */

/* ========================================================================= */

/*
 * sdp_conn_advt_table_init - initialize a new empty advertisement table
 */
int32_t
sdp_conn_advt_table_init(sdp_advt_table_t *table)
{
	SDP_CHECK_NULL(sdp_conn_advt_cache, -EINVAL);
	SDP_CHECK_NULL(table, -EINVAL);

	table->head = NULL;
	table->size = 0;
	mutex_init(&table->sat_mutex, NULL, MUTEX_DRIVER, NULL);

	return (0);
}   /* sdp_conn_advt_table_init */

void
sdp_conn_advt_table_destroy(sdp_advt_table_t *table)
{
	(void) sdp_conn_advt_table_clear(table);
	ASSERT(table->head == NULL && table->size == 0);
	mutex_destroy(&table->sat_mutex);
}

/* ========================================================================= */

/*
 * sdp_conn_advt_table_clear - clear the contents of an advertisement table
 */
int32_t
sdp_conn_advt_table_clear(sdp_advt_table_t *table)
{
	sdp_advt_t *advt;
	int32_t result;

	SDP_CHECK_NULL(sdp_conn_advt_cache, -EINVAL);
	SDP_CHECK_NULL(table, -EINVAL);
	/*
	 * drain the table of any objects
	 */
	while ((advt = sdp_conn_advt_table_get(table)) != NULL) {
		result = sdp_conn_advt_destroy(advt);
	}	/* while */
	ASSERT(table->head == NULL && table->size == 0);

	return (result);
}   /* sdp_conn_advt_table_clear */

/*ARGSUSED*/
static int
sdp_conn_advt_table_constructor(void *buf, void *arg, int kmflags)
{
	sdp_advt_table_t *table = buf;

	mutex_init(&table->sat_mutex, NULL, MUTEX_DRIVER, NULL);

	return (0);
}

/*ARGSUSED*/
static void
sdp_conn_advt_table_destructor(void *buf, void *arg)
{
	sdp_advt_table_t *table = buf;
	mutex_destroy(&table->sat_mutex);
}

/* --------------------------------------------------------------------- */

/* primary initialization/cleanup functions				*/

/* --------------------------------------------------------------------- */

/* ========================================================================= */

/*
 * sdp_conn_advt_init -- initialize the advertisement caches.
 */
int32_t
sdp_conn_advt_init(void)
{

	int result = 0;

	/*
	 * initialize the caches only once.
	 */
	if (sdp_conn_advt_cache != NULL || sdp_conn_advt_table != NULL) {
		return (-EINVAL);
	}
	sdp_conn_advt_cache = kmem_cache_create("sdp_advt_cache",
	    sizeof (sdp_advt_t), 0, NULL, NULL, NULL, NULL, NULL, KM_SLEEP);
	if (sdp_conn_advt_cache == NULL) {
		result = -ENOMEM;
		goto error_advt_c;
	}
	sdp_conn_advt_table = kmem_cache_create("sdp_advt_table",
	    sizeof (sdp_advt_table_t), 0, sdp_conn_advt_table_constructor,
	    sdp_conn_advt_table_destructor, NULL, NULL, NULL, KM_SLEEP);
	if (sdp_conn_advt_table == NULL) {
		result = -ENOMEM;
		goto error_advt_t;
	}
	return (0);
error_advt_t:
	kmem_cache_destroy(sdp_conn_advt_cache);
	sdp_conn_advt_cache = NULL;
error_advt_c:
	return (result);
}   /* sdp_conn_advt_init */

/* ========================================================================= */

/*
 * sdp_conn_advt_main_cleanup -- cleanup the advertisement caches.
 */
int32_t
sdp_conn_advt_cleanup(void)
{
	/*
	 * cleanup the caches
	 */
	kmem_cache_destroy(sdp_conn_advt_cache);
	kmem_cache_destroy(sdp_conn_advt_table);
	/*
	 * null out entries.
	 */
	sdp_conn_advt_cache = NULL;
	sdp_conn_advt_table = NULL;

	return (0);
}   /* sdp_conn_advt_cleanup */
