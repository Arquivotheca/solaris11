/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/ib/clients/sdpib/sdp_main.h>
#include <sys/ib/clients/sdpib/sdp_trace_util.h>

/*
 * data buffers managment API
 */

/* ========================================================================= */

/*
 * sdp_buff_pool_get - get a buffer from a specific pool
 */
sdp_buff_t *
sdp_buff_pool_get(sdp_pool_t *pool, int32_t fifo)
{
	sdp_buff_t *buff;

	SDP_CHECK_NULL(pool, NULL);

	buff = NULL;
	mutex_enter(&pool->sdp_pool_mutex);
	if (pool->head == NULL) {
		goto done;
	}
	if (fifo) {
		buff = pool->head;
	} else {
		buff = pool->head->prev;
	}

	if ((buff->next == buff) && (buff->prev == buff)) {
		pool->head = NULL;
	} else {
		buff->next->prev = buff->prev;
		buff->prev->next = buff->next;

		pool->head = buff->next;
	}

	pool->sdp_pool_size--;
	buff->next = NULL;
	buff->prev = NULL;
	buff->buff_pool = NULL;

done:
	mutex_exit(&pool->sdp_pool_mutex);
	return (buff);
}

/* ========================================================================= */

/*
 * sdp_buff_pool_put - place a buffer into a specific pool
 */
void
sdp_buff_pool_put(sdp_pool_t *pool, sdp_buff_t *buff, int32_t fifo)
{
	SDP_CHECK_NULL(pool, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);

	ASSERT(buff->buff_pool == NULL);

	mutex_enter(&pool->sdp_pool_mutex);

	if (pool->head == NULL) {
		buff->next = buff;
		buff->prev = buff;
		pool->head = buff;
	} else {
		buff->next = pool->head;
		buff->prev = pool->head->prev;

		buff->next->prev = buff;
		buff->prev->next = buff;

		if (fifo) {
			pool->head = buff;
		}
	}

	pool->sdp_pool_size++;
	buff->buff_pool = pool;

	mutex_exit(&pool->sdp_pool_mutex);
}


/* ========================================================================= */

/*
 * sdp_buff_pool_look - look at a buffer from a specific pool
 * Note: Was used by old OOB code Not being used currently.
 */
sdp_buff_t *
sdp_buff_pool_look(sdp_pool_t *pool, int32_t fifo)
{
	SDP_CHECK_NULL(pool, NULL);

	if (pool->head == NULL || fifo == B_TRUE) {
		return (pool->head);
	} else {
		return (pool->head->prev);
	}	/* else */
}   /* sdp_buff_pool_look */


/* ========================================================================= */

/*
 * sdp_buff_pool_remove - remove a specific buffer from a specific pool
 * Note: Was used by old OOB code Not being used currently.
 */
static int32_t
sdp_buff_pool_remove(sdp_pool_t *pool, sdp_buff_t *buff)
{
	sdp_buff_t *prev;
	sdp_buff_t *next;

	SDP_CHECK_NULL(pool, -EINVAL);
	SDP_CHECK_NULL(buff, -EINVAL);
	SDP_CHECK_NULL(buff->next, -EINVAL);
	SDP_CHECK_NULL(buff->prev, -EINVAL);
	SDP_CHECK_NULL(buff->pool, -EINVAL);

	ASSERT(pool == buff->buff_pool);
	mutex_enter(&pool->sdp_pool_mutex);

	if (buff->next == buff && buff->prev == buff) {
		pool->head = NULL;
	} else {
		next = buff->next;
		prev = buff->prev;
		next->prev = prev;
		prev->next = next;

		if (pool->head == buff) {
			pool->head = next;
		}	/* if */
	}

	pool->sdp_pool_size--;

	buff->buff_pool = NULL;
	buff->next = NULL;
	buff->prev = NULL;

	mutex_exit(&pool->sdp_pool_mutex);
	return (0);
}


/* ========================================================================= */

/*
 * sdp_buff_pool_init - init a pool
 */
void
sdp_buff_pool_init(sdp_pool_t *pool)
{
	pool->head = NULL;
	pool->sdp_pool_size = 0;
	mutex_init(&pool->sdp_pool_mutex, NULL, MUTEX_DRIVER, NULL);
}

void
sdp_buff_pool_destroy(sdp_pool_t *pool)
{
	sdp_buff_pool_clear(pool);
	ASSERT(pool->head == NULL && pool->sdp_pool_size == 0);
	mutex_destroy(&pool->sdp_pool_mutex);
}

/*
 * sdp_buff_pool_clear - clear the buffers out of a specific buffer pool
 */
void
sdp_buff_pool_clear(sdp_pool_t *pool)
{
	sdp_buff_t *buff;

	while ((buff = sdp_buff_pool_get(pool, B_FALSE)) != NULL) {
		sdp_buff_free(buff);
	}
	ASSERT(pool->head == NULL && pool->sdp_pool_size == 0);
}

/* ========================================================================= */

/*
 * sdp_buff_main_put - return a buffer to the main buffer pool
 */
void
sdp_buff_free(sdp_buff_t *buff)
{
	if (buff == NULL)
		return;
	buff->sdp_buff_data = buff->sdp_buff_tail = buff->sdp_buff_head;
	buff->next = NULL;
	buff->prev = NULL;
	kmem_cache_free(buff->sdp_buff_cache, buff);
}

static void
sdp_buff_destructor(void *buf, void *arg)
{
	int result;
	sdp_buff_t *buff = buf;

	ASSERT((buff->sdp_buff_mr_hdl != NULL && buff->sdp_mi_hdl == NULL) ||
	    (buff->sdp_buff_mr_hdl == NULL && buff->sdp_mi_hdl != NULL));
	if (buff->sdp_buff_mr_hdl) {
		result = ibt_deregister_mr(buff->sdp_buff_hca_hdl,
		    buff->sdp_buff_mr_hdl);
		buff->sdp_buff_mr_hdl = NULL;
	}
	if (buff->sdp_mi_hdl) {
		result = ibt_unmap_mem_iov(buff->sdp_buff_hca_hdl,
		    buff->sdp_mi_hdl);
		if (buff->sdp_rwr.wr_sgl != buff->sdp_sgl) {
			/*
			 * sgl was allocated
			 */
			kmem_free(buff->sdp_rwr.wr_sgl,
			    ((sdp_dev_hca_t *)arg)->hca_nds *
			    sizeof (ibt_wr_ds_t));
		}
		buff->sdp_mi_hdl = NULL;
	}
	ASSERT(result == IBT_SUCCESS);
	kmem_free(buff->sdp_buff_head, buff->sdp_buff_size);
}

static int
sdp_buff_constructor(void *buf, void *arg, int kmflags)
{
	sdp_dev_hca_t *hcap = arg;
	sdp_buff_t *buff = buf;
	ibt_mr_attr_t mem_attr;
	ibt_mr_desc_t mem_desc;
	ibt_iov_attr_t iov_attr;
	ibt_iov_t iov;
	ibt_recv_wr_t *wr;
	int32_t result;

	buff->sdp_buff_head = kmem_zalloc(hcap->hca_sdp_buff_size, kmflags);
	if (buff->sdp_buff_head == NULL)
		return (-1);

	wr = &buff->sdp_rwr;
	if (hcap->hca_use_reserved_lkey) {
		ASSERT(hcap->hca_nds > 0);
		buff->sdp_buff_mr_hdl = NULL;
		if (hcap->hca_nds <= SDP_QP_LIMIT_SG_BUFF)
			wr->wr_sgl = buff->sdp_sgl;
		else {
			wr->wr_sgl = kmem_zalloc(hcap->hca_nds *
			    sizeof (ibt_wr_ds_t), kmflags);
			if (wr->wr_sgl == NULL) {
				kmem_free(buff->sdp_buff_head,
				    hcap->hca_sdp_buff_size);
				return (-1);
			}
		}
		iov.iov_addr = buff->sdp_buff_head;
		iov.iov_len = hcap->hca_sdp_buff_size;
		iov_attr.iov_as = NULL;
		iov_attr.iov = &iov;
		iov_attr.iov_buf = NULL;
		iov_attr.iov_list_len = 1;
		iov_attr.iov_wr_nds = hcap->hca_nds;
		iov_attr.iov_lso_hdr_sz = 0;
		iov_attr.iov_flags = ((kmflags == KM_SLEEP) ? IBT_IOV_SLEEP :
		    IBT_IOV_NOSLEEP) | IBT_IOV_RECV;
		result = ibt_map_mem_iov(hcap->hca_hdl, &iov_attr,
		    (ibt_all_wr_t *)wr, &buff->sdp_mi_hdl);
		if (result != IBT_SUCCESS) {
			if (wr->wr_sgl != buff->sdp_sgl) {
				/*
				 * sgl was allocated
				 */
				kmem_free(wr->wr_sgl, hcap->hca_nds *
				    sizeof (ibt_wr_ds_t));
			}
			kmem_free(buff->sdp_buff_head, hcap->hca_sdp_buff_size);
			return (-1);
		}
	} else {
		buff->sdp_mi_hdl = NULL;
		mem_attr.mr_vaddr = (uint64_t)(uintptr_t)buff->sdp_buff_head;
		mem_attr.mr_len = hcap->hca_sdp_buff_size;
		mem_attr.mr_as = NULL;
		mem_attr.mr_flags = ((kmflags == KM_SLEEP) ? IBT_MR_SLEEP :
		    IBT_MR_NOSLEEP) | IBT_MR_ENABLE_LOCAL_WRITE;

		result = ibt_register_mr(hcap->hca_hdl, hcap->pd_hdl,
		    &mem_attr, &buff->sdp_buff_mr_hdl, &mem_desc);
		if (result != IBT_SUCCESS) {
			kmem_free(buff->sdp_buff_head, hcap->hca_sdp_buff_size);
			return (-1);
		}
		wr->wr_sgl = buff->sdp_sgl;
		wr->wr_sgl[0].ds_len = hcap->hca_sdp_buff_size;
		wr->wr_sgl[0].ds_va =
		    (ib_vaddr_t)(uintptr_t)buff->sdp_buff_head;
		wr->wr_sgl[0].ds_key = mem_desc.md_lkey;
		wr->wr_nds = 1;
	}
	buff->sdp_buff_data = buff->sdp_buff_tail = buff->sdp_buff_head;
	buff->sdp_buff_end = (caddr_t)buff->sdp_buff_head +
	    hcap->hca_sdp_buff_size;
	buff->type = SDP_GENERIC_TYPE_BUFF;
	buff->sdp_buff_hca_hdl = hcap->hca_hdl;
	buff->sdp_buff_size = hcap->hca_sdp_buff_size;
	buff->sdp_buff_cache = hcap->sdp_hca_buff_cache;
	return (0);
}

void
sdp_buff_cache_create(sdp_dev_hca_t *hcap)
{
	char cache_name[24];

	(void) sprintf(cache_name, "%s_%llx_%s", "sdp_hca",
	    (unsigned long long) (hcap->guid), "cache");
	sdp_print_warn(NULL, "sdp hca cache name %s \n", cache_name);
	hcap->sdp_hca_buff_cache =
	    kmem_cache_create(cache_name, sizeof (sdp_buff_t), 0,
	    sdp_buff_constructor, sdp_buff_destructor, NULL, hcap,
	    NULL, NULL);
}

void
sdp_buff_cache_destroy(sdp_dev_hca_t *hcap)
{
	kmem_cache_destroy(hcap->sdp_hca_buff_cache);
}

/*
 * sdp_buff_main_get - get a buffer from the main buffer pool
 */
sdp_buff_t *
sdp_buff_get(sdp_conn_t *connp)
{
	sdp_buff_t *buff;

	if (connp->sdp_conn_buff_cache == NULL) {
		ASSERT(0);
		return (NULL);
	}

	buff = kmem_cache_alloc(connp->sdp_conn_buff_cache, KM_NOSLEEP);
	if (buff == NULL)
		return (buff);
	buff->buff_pool = NULL;
	buff->flags = 0;
	buff->next = buff->prev = NULL;
	buff->sdp_buff_data_size = 0;
	buff->sdp_buff_ib_wrid = 0;
	return (buff);
}

/*
 * sdp_buff_main_find - Get the first matching buffer from the pool
 * Note: Was used by old OOB code Not being used currently.
 */
sdp_buff_t *sdp_buff_main_find(sdp_pool_t *pool,
    int (*test)(sdp_buff_t *buff, void *arg), void *usr_arg)
{
	sdp_buff_t *buff;
	int result = 0;
	int counter;

	/*
	 * check to see if there is anything to traverse.
	 */
	if (pool->head)
		/*
		 * lock to prevent corruption of table
		 */
		for (counter = 0, buff = pool->head;
		    counter < pool->sdp_pool_size;
		    counter++, buff = buff->next) {
			result = test(buff, usr_arg);
			if (result > 0) {
				(void) sdp_buff_pool_remove(pool, buff);
				return (buff);
			}
		}
	return (NULL);
}

/*
 * sdp_buff_mine_data: traverse buffers in pool, from the head
 * Note: Was used by old OOB code Not being used currently.
 */
int sdp_buff_mine_data(sdp_pool_t *pool,
    int (*mine_func)(sdp_buff_t *buff, void *arg), void *usr_arg)
{
	sdp_buff_t *buff;
	int result = 0;
	int counter;

	/*
	 * check to see if there is anything to traverse.
	 */
	if (pool->head)
		/*
		 * lock to prevent corruption of table
		 */
		for (counter = 0, buff = pool->head;
		    counter < pool->sdp_pool_size;
		    counter++, buff = buff->next) {
			result = mine_func(buff, usr_arg);
			if (result > 0)
				break;
		}
	return (result);
}
