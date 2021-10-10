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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This header file contains definations for utility routines
 * which can be used by all Solaris OFUV related kernel drivers
 * and misc modules. The kernel modules using these APIs, should
 * load sol_ofs using :
 *	ld -r -N misc/sol_ofs
 *
 * The APIs defined are :
 *	1. User Objects
 *	2. Linked Lists
 *	3. Debug Routines
 */
#ifndef	_SYS_IB_CLIENTS_OF_SOL_OFS_SOL_OFS_COMMON_H
#define	_SYS_IB_CLIENTS_OF_SOL_OFS_SOL_OFS_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/ksynch.h>
#include <sys/ib/ibtl/ibvti.h>

/*
 * User Objects functions and structures.
 */
typedef enum {
	/* User objects for sol_uverbs driver */
	SOL_UVERBS_UCTXT_UOBJ_TYPE	= 0,
	SOL_UVERBS_UPD_UOBJ_TYPE,
	SOL_UVERBS_UAH_UOBJ_TYPE,
	SOL_UVERBS_UMR_UOBJ_TYPE,
	SOL_UVERBS_UCQ_UOBJ_TYPE,
	SOL_UVERBS_USRQ_UOBJ_TYPE,
	SOL_UVERBS_UQP_UOBJ_TYPE,
	SOL_UVERBS_UFILE_UOBJ_TYPE,

	/* User Objects for sol_ucma driver */
	SOL_UCMA_EVT_FILE_TYPE,
	SOL_UCMA_CM_ID_TYPE,
	SOL_UCMA_MCAST_TYPE
} sol_ofs_uobj_type_t;

typedef struct {
	uint64_t		uo_user_handle;
	sol_ofs_uobj_type_t	uo_type;
	krwlock_t		uo_lock;
	uint32_t		uo_id;
	kmutex_t		uo_reflock;
	uint32_t		uo_refcnt;
	uint32_t		uo_live;
	size_t			uo_uobj_sz;
} sol_ofs_uobj_t;

/*
 * Objects are maintained in tables that allow an easy table ID to User Object
 * mapping and can grow as resources are created.
 */
#define	SOL_OFS_UO_BLKSZ	16

typedef struct {
	int		ofs_uo_blk_avail;
	sol_ofs_uobj_t	*ofs_uoblk_blks[SOL_OFS_UO_BLKSZ];
} sol_ofs_uobj_blk_t;

typedef struct {
	krwlock_t		uobj_tbl_lock;
	int			uobj_tbl_used_blks;
	uint_t			uobj_tbl_num_blks;
	size_t			uobj_tbl_uo_sz;
	int			uobj_tbl_uo_cnt;
	sol_ofs_uobj_blk_t	**uobj_tbl_uo_root;
} sol_ofs_uobj_table_t;

/* User object table management routines */
void sol_ofs_uobj_tbl_init(sol_ofs_uobj_table_t *, size_t);
void sol_ofs_uobj_tbl_fini(sol_ofs_uobj_table_t *);

void sol_ofs_uobj_init(sol_ofs_uobj_t *, uint64_t, sol_ofs_uobj_type_t);
void sol_ofs_uobj_ref(sol_ofs_uobj_t *);
void sol_ofs_uobj_deref(sol_ofs_uobj_t *,
void (*free_func)(sol_ofs_uobj_t *));
void sol_ofs_uobj_put(sol_ofs_uobj_t *);
void sol_ofs_uobj_free(sol_ofs_uobj_t *uobj);

int sol_ofs_uobj_add(sol_ofs_uobj_table_t *, sol_ofs_uobj_t *);
sol_ofs_uobj_t	*sol_ofs_uobj_remove(sol_ofs_uobj_table_t *,
    sol_ofs_uobj_t *);
sol_ofs_uobj_t	*sol_ofs_uobj_get_read(sol_ofs_uobj_table_t *, uint32_t);
sol_ofs_uobj_t	*sol_ofs_uobj_get_write(sol_ofs_uobj_table_t *, uint32_t);

/*
 * Generic linked list management functions
 */
typedef uchar_t		bool;
#define	FALSE		0
#define	TRUE		1
#define	INVALID_HANDLE	0xFFFFFFFF
#define	MAX_HASH_SIZE	1024

/*
 * Simple doubly linked list for opaque addresses.  Protection must occur
 * outside of the list.  These behavior very much like the linux kernel
 * lists, hence the familiar look of the API; but note there are
 * some signficant differences, mainly the list header is not embedded
 * in the element, so the container (typeof) constructs are not required.
 */
typedef struct llist_head {
	struct llist_head	*prv;
	struct llist_head	*nxt;
	void			*ptr;
} llist_head_t;


#define	LLIST_HEAD_INIT(x) { &(x), &(x), NULL }

static inline void llist_head_init(llist_head_t *list, void *ptr)
{
	list->prv = list->nxt = list;
	list->ptr = ptr;
}

static inline void __llist_add(llist_head_t  *new, llist_head_t  *prv,
							llist_head_t  *nxt)
{
	nxt->prv   = new;
	new->nxt   = nxt;
	new->prv   = prv;
	prv->nxt   = new;
}
static inline void llist_add(llist_head_t *new, llist_head_t *head)
{
	__llist_add(new, head, head->nxt);
}

static inline void llist_add_tail(llist_head_t *new, llist_head_t *head)
{
	__llist_add(new, head->prv, head);
}

static inline void llist_del(llist_head_t *entry)
{
	entry->nxt->prv = entry->prv;
	entry->prv->nxt = entry->nxt;
}

static inline int llist_is_last(llist_head_t *list, llist_head_t *head)
{
	return (list->nxt == head);
}

static inline int llist_empty(llist_head_t *head)
{
	return (head->nxt == head);
}

#define	list_for_each(_pos, _head)                 \
	for (_pos = (_head)->nxt; _pos != (_head); _pos = _pos->nxt)
#define	list_for_each_safe(_pos, n, _head) \
	for (_pos = (_head)->nxt, n = _pos->nxt; _pos != (_head); \
		_pos = n, n = _pos->nxt)

/*
 * Doubly linked per user context IB resource list definitions
 * Protection must occur * outside of the list.
 */
typedef struct genlist_entry_s {
	uintptr_t		data;
	void			*data_context;
	struct genlist_entry_s	*next;
	struct genlist_entry_s	*prev;
} genlist_entry_t;

typedef struct genlist_s {
	uint32_t	count;
	genlist_entry_t	*head;
	genlist_entry_t	*tail;
} genlist_t;


genlist_entry_t *add_genlist(genlist_t *list, uintptr_t data,
    void *data_context);

#define	genlist_for_each(_pos, _head)	\
	for (_pos = (_head)->head; _pos; _pos = _pos->next)

void delete_genlist(genlist_t *list, genlist_entry_t *entry);

genlist_entry_t *remove_genlist_head(genlist_t *list);

void insert_genlist_tail(genlist_t *list, genlist_entry_t *entry);
void insert_genlist_head(genlist_t *list, genlist_entry_t *entry);

void flush_genlist(genlist_t *list);

bool genlist_empty(genlist_t *list);

static inline void init_genlist(genlist_t *list)
{
	list->head = list->tail = NULL;
	list->count = 0;
}


/*
 * Debug printfs defines
 */
void sol_ofs_dprintf_l5(char *name, char *fmt, ...);
void sol_ofs_dprintf_l4(char *name, char *fmt, ...);
void sol_ofs_dprintf_l3(char *name, char *fmt, ...);
void sol_ofs_dprintf_l2(char *name, char *fmt, ...);
void sol_ofs_dprintf_l1(char *name, char *fmt, ...);
void sol_ofs_dprintf_l0(char *name, char *fmt, ...);

#define	SOL_OFS_DPRINTF_L5	sol_ofs_dprintf_l5
#define	SOL_OFS_DPRINTF_L4	sol_ofs_dprintf_l4
#define	SOL_OFS_DPRINTF_L3	sol_ofs_dprintf_l3
#define	SOL_OFS_DPRINTF_L2	sol_ofs_dprintf_l2
#define	SOL_OFS_DPRINTF_L1	sol_ofs_dprintf_l1
#define	SOL_OFS_DPRINTF_L0	sol_ofs_dprintf_l0

/*
 * Global list of HCAs.
 */
extern list_t		sol_ofs_dev_list;
extern kmutex_t		sol_ofs_dev_mutex;
extern ibt_clnt_hdl_t	sol_ofs_ibt_clnt;

typedef enum {
	SOL_OFS_HCA_DRVR_TAVOR = 1,
	SOL_OFS_HCA_DRVR_HERMON
} sol_ofs_hca_drvr_nm_t;

typedef struct sol_ofs_dev_s {
	kmutex_t		ofs_dev_mutex;
	/* Ptr in the global sol_ofs_dev_list */
	list_node_t		ofs_dev_list;
	/* Private hca handle for sol_ofs internal use. */
	ibt_hca_hdl_t		ofs_dev_hca_hdl;
	ib_guid_t		ofs_dev_guid;
	uint8_t			ofs_dev_nports;

	/*
	 * Private fields for OFED user name computation.
	 */
	sol_ofs_hca_drvr_nm_t	ofs_dev_drvr_nm;
	char			ofs_dev_hca_drv_name[MAXNAMELEN];
	int			ofs_dev_drvr_inst;

	enum {
		SOL_OFS_DEV_ADDED,
		SOL_OFS_DEV_REM_IN_PROGRESS,
		SOL_OFS_DEV_REMOVED
	} ofs_dev_state;

	/*
	 * Fields used by kverbs to fill OFED user
	 * name related fields for the ib_device.
	 */
	char			ofs_dev_ofusr_name[MAXNAMELEN];
	uint8_t			ofs_dev_ofusr_hca_idx;
	uint16_t		ofs_dev_ofusr_port_index;

	/*
	 * Ptr to struct ib_device.
	 */
	void			*ofs_dev_ib_device;

	/*
	 * Number of devices referencing this sol_ofs device
	 */
	uint_t			ofs_dev_client_cnt;
} sol_ofs_dev_t;

typedef struct ib_device_impl_s {
	kmutex_t		dev_impl_mutex;
	kcondvar_t		dev_impl_cv;

	/*
	 * No of CQ handlers currently invoked for the
	 * device for the client + 1. Decrment and check
	 * for zero before calling (*remove), so that no
	 * CQ handlers are in progress when (*remove) is
	 * called.
	 */
	uint32_t		dev_impl_evt_ref;

	/*
	 * Ptr to sol_ofs ofs_devp for sol_ofs client only.
	 */
	void			*dev_impl_ofs_devp;
} ib_device_impl_t;

extern sol_ofs_dev_t *sol_ofs_find_dev(dev_info_t *, int,
    boolean_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_OF_SOL_OFS_SOL_OFS_COMMON_H */
