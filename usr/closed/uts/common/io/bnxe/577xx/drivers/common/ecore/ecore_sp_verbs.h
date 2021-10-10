#ifndef ECORE_SP_VERBS
#define ECORE_SP_VERBS

#define ETH_ALEN 6

#include "lm_defs.h"
#include "listq.h"
#include "eth_constants.h"

/* FIXME (MichalS): move to bcmtypes.h 26-Sep-10 */
typedef int BOOL;

#ifdef __LINUX
	#include <linux/time.h>
#else
	typedef volatile unsigned long atomic_t;
#endif

struct _lm_device_t;

enum {
	RAMROD_TX,
	RAMROD_RX,
	RAMROD_COMP_WAIT,
	RAMROD_DRV_CLR_ONLY,
};

typedef enum {
	ECORE_OBJ_TYPE_RX,
	ECORE_OBJ_TYPE_TX,
	ECORE_OBJ_TYPE_RX_TX,
} ecore_obj_type;

/* Filtering states */
enum {
	ECORE_FILTER_MAC_PENDING,
	ECORE_FILTER_VLAN_PENDING,
	ECORE_FILTER_VLAN_MAC_PENDING,
	ECORE_FILTER_RX_MODE_PENDING,
	ECORE_FILTER_RX_MODE_SCHED,
	ECORE_FILTER_ISCSI_ETH_START_SCHED,
	ECORE_FILTER_ISCSI_ETH_STOP_SCHED,
	ECORE_FILTER_MCAST_PENDING,
	ECORE_FILTER_MCAST_SCHED,
	ECORE_FILTER_RSS_CONF_PENDING,
	ECORE_FILTER_CL_UPDATE_PENDING,
	ECORE_FILTER_TX_SWITCH_MAC_PENDING,
};

struct ecore_raw_obj {
	u8		func_id;

	/* Client params */
	u8		cl_id;
	u32		cid;

	/* Ramrod data buffer params */
	void 		*rdata;
	lm_address_t	rdata_mapping;

	/* Ramrod state params */
	int		state;   /* "ramrod is pending" state bit */
	unsigned long	*pstate; /* pointer to state buffer */

	ecore_obj_type	obj_type;

	int (*wait_comp)(struct _lm_device_t *pdev,
			 struct ecore_raw_obj *o);

	BOOL (*check_pending)(struct ecore_raw_obj *o);
	void (*clear_pending)(struct ecore_raw_obj *o);
	void (*set_pending)(struct ecore_raw_obj *o);
};

/***************** Classification verbs: Set/Del MAC/VLAN/VLAN-MAC ************/

/**
 *  If entry is not NULL, it's valid - query on it.
 */
struct ecore_mac_list_query {
	u8 *mac;
	u8 *cl_id;
};

struct ecore_vlan_list_query {
	u16 *vlan;
	u8  *cl_id;
};

struct ecore_vlan_mac_list_query {
	u16 *vlan;
	u8  *mac;
	u8  *cl_id;
};

union ecore_list_query {
	struct ecore_mac_list_query mac;
	struct ecore_vlan_list_query vlan;
	struct ecore_vlan_mac_list_query vlan_mac;
};

struct ecore_mac_ramrod_data {
	u8 mac[ETH_ALEN];
	u8 cam_offset;
};

struct ecore_vlan_ramrod_data {
	u16 vlan;
};

struct ecore_vlan_mac_ramrod_data {
	u8 mac[ETH_ALEN];
	u16 vlan;
	u8 cam_offset;
};

/* TODO: Come up with a better name */
struct ecore_list_elem {
	d_list_entry_t link;
	/** used to recover the data related vlan_mac_flags bits from ramrod
	 *  parameters during del_next().
	 */
	unsigned long vlan_mac_flags;
	/** used to store the cam offset used for the mac below 
	 * relevant for 57710 and 57711 only
	 */
	u8 cam_offset;
	union {
		struct ecore_mac_ramrod_data mac;
		struct ecore_vlan_ramrod_data vlan;
		struct ecore_vlan_mac_ramrod_data vlan_mac;
	} data;
};


/* VLAN_MAC specific flags */
enum {
	ECORE_ETH_MAC,
	ECORE_ISCSI_ETH_MAC,
	ECORE_NETQ_ETH_MAC,
	ECORE_DONT_CONSUME_CAM_CREDIT,
	ECORE_DONT_CONSUME_CAM_CREDIT_DEST,
};

struct ecore_vlan_mac_ramrod_params {
	struct ecore_vlan_mac_obj *vlan_mac_obj;
	unsigned long ramrod_flags;
	unsigned long vlan_mac_flags;

	union {
		struct ecore_mac_ramrod_data mac;
		struct ecore_vlan_ramrod_data vlan;
		struct ecore_vlan_mac_ramrod_data vlan_mac;
	} data;
};

struct ecore_vlan_mac_obj {
	struct ecore_raw_obj raw;

	/* Bookkeeping list: will prevent the addition of already existing
	 * entries.
	 */
	d_list_t head;

	/* MACs credit pool */
	struct ecore_credit_pool_obj *macs_pool;

	/* VLANs credit pool */
	struct ecore_credit_pool_obj *vlans_pool;

	/**
	 * Checks if ADD-ramrod with the given params may be performed.
	 *
	 * @return TRUE if the element may be added
	 */

	BOOL (*check_add)(struct ecore_vlan_mac_ramrod_params *p);

	/**
	 * Checks if DEL-ramrod with the given params may be performed.
	 *
	 * @return TRUE if the element may be deleted
	 */
	struct ecore_list_elem *
		(*check_del)(struct ecore_vlan_mac_ramrod_params *p);

	/**
	 *  Update the relevant credit object(s) (consume/return
	 *  correspondingly).
	 */
	BOOL (*get_credit)(struct ecore_vlan_mac_obj *o, u8 * cam_offset);
	BOOL (*put_credit)(struct ecore_vlan_mac_obj *o, u8 cam_offset);

	/**
	 * @param add Set to 0 if DELETE rule is requested
	 */
	int (*config_rule)(struct _lm_device_t *pdev,
			   struct ecore_vlan_mac_ramrod_params *p,
			   struct ecore_list_elem *pos,
			   BOOL add);

	int (*config_move_rules)(struct _lm_device_t *pdev,
			   struct ecore_vlan_mac_ramrod_params *p,
			   struct ecore_vlan_mac_obj * dest_o,
			   struct ecore_list_elem *pos
			   );

	/**
	 * Delete the next configured element.
	 *
	 * @param pdev
	 * @param p Command parameters (same as for a regular DELETE command).
	 *
	 * @return 0 if the last operation has completed successfully and there are no
	 *  	   more elements left, positive value if the last operation has
	 *  	   completed successfully and there are more previously configured
	 *  	   elements, negative value is current operation has failed.
	 */
	int (*del_next)(struct _lm_device_t *pdev,
			struct ecore_vlan_mac_ramrod_params *p);

	/**
	 * Reconfigures the next MAC/VLAN/VLAN-MAC element from the previously
	 * configured elements list.
	 *
	 * @param pdev
	 * @param p Command parameters (RAMROD_COMP_WAIT bit in ramrod_flags is only
	 *  		taken into an account)
	 * @param ppos a pointer to the cooky that should be given back in the next call
	 *  		  to make function handle the next element. If *ppos is set to NULL
	 *  		  it will restart the iterator. If returned *ppos == NULL this means
	 *  		  that the last element has been handled.
	 *
	 * @return int
	 */
	int (*restore)(struct _lm_device_t *pdev, struct ecore_vlan_mac_ramrod_params *p,
			   struct ecore_list_elem **ppos);
};

/*  RX_MODE verbs:DROP_ALL/ACCEPT_ALL/ACCEPT_ALL_MULTI/ACCEPT_ALL_VLAN/NORMA */

/* RX_MODE ramrod spesial flags: set in rx_mode_flags field in
 * a ecore_rx_mode_ramrod_params.
 */
enum {
	ECORE_RX_MODE_FCOE_ETH,
	ECORE_RX_MODE_ISCSI_ETH,
};

enum {
	ECORE_ACCEPT_UNICAST,
	ECORE_ACCEPT_MULTICAST,
	ECORE_ACCEPT_ALL_UNICAST,
	ECORE_ACCEPT_ALL_MULTICAST,
	ECORE_ACCEPT_BROADCAST,
	ECORE_ACCEPT_UNMATCHED,
	ECORE_ACCEPT_ANY_VLAN
};
struct ecore_rx_mode_ramrod_params {
	struct ecore_rx_mode_obj *rx_mode_obj;
	unsigned long *pstate;
	int state;
	u8 cl_id;
	u32 cid;
	u8 func_id;
	unsigned long ramrod_flags;
	unsigned long rx_mode_flags;
	/* rdata is either a pointer to eth_filter_rules_ramrod_data(e2) or to
	 * a tstorm_eth_mac_filter_config (e1x).
	 * */
	void *rdata;
	lm_address_t rdata_mapping;
	unsigned long rx_accept_flags;
	unsigned long tx_accept_flags;
};

struct ecore_rx_mode_obj {
	int (*config_rx_mode)(struct _lm_device_t *pdev,
				  struct ecore_rx_mode_ramrod_params *p);

	int (*wait_comp)(struct _lm_device_t *pdev,
			 struct ecore_rx_mode_ramrod_params *p);
};

/********************** Set multicast group ***********************************/

struct ecore_mcast_list_elem {
	d_list_entry_t link;
	u8 *mac;
};

union ecore_mcast_config_data {
	u8 *mac;
	u8 bin; /* used in a RESTORE flow */
};

struct ecore_mcast_ramrod_params {
	struct ecore_mcast_obj *mcast_obj;

	/* Relevant options are RAMROD_COMP_WAIT and RAMROD_DRV_CLR_ONLY */
	unsigned long ramrod_flags;

	d_list_t mcast_list; /* list of struct ecore_mcast_list_elem */
	/** TODO:
	 *  	- rename it to macs_num.
	 *  	- Add a new command type for handling pending commands
	 *  	  (remove "zero semantics").
	 *
	 *  Length of mcast_list. If zero and ADD_CONT command - post
	 *  pending commands.
	 */
	int mcast_list_len;
};

enum {
	ECORE_MCAST_CMD_ADD,
	ECORE_MCAST_CMD_CONT,
	ECORE_MCAST_CMD_DEL,
	ECORE_MCAST_CMD_RESTORE,
};

typedef int (*enqueue_cmd_func)(struct _lm_device_t *pdev, struct ecore_mcast_obj *o,
			   struct ecore_mcast_ramrod_params *p, int cmd);

typedef int (*hdl_restore_func)(struct _lm_device_t *pdev, struct ecore_mcast_obj *o,
			   int start_bin, int *rdata_idx);

typedef void (*set_one_rule_func)(struct _lm_device_t *pdev,
				 struct ecore_mcast_obj *o, int idx,
				 union ecore_mcast_config_data *cfg_data, int cmd);

struct ecore_mcast_obj {
	struct ecore_raw_obj raw;

	union {
		struct {
		#define ECORE_MCAST_BINS_NUM	256
		#define ECORE_MCAST_VEC_SZ  (ECORE_MCAST_BINS_NUM / 64)
			u64 vec[ECORE_MCAST_VEC_SZ];

			/** Number of BINs to clear. Should be updated
			 *  immediately when a command arrives in order to
			 *  properly create DEL commands.
			 */
			int num_bins_set;
		} aprox_match;

		struct {
			d_list_t macs;
			int num_macs_set;
		} exact_match;
	} registry;

	/* Pending commands */
	d_list_t pending_cmds_head;

	/* A state that is set in raw.pstate, when there are pending commands */
	int sched_state;

	/* Maximal number of mcast MACs configured in one command */
	int max_cmd_len;

	/* Total number of currently pending MACs to configure: both
	 * in the pending commands list and in the current command.
	 */
	int total_pending_num;

	u8 engine_id;

	/**
	 * @param cmd command to execute (ECORE_MCAST_CMD_X, see above)
	 */
	int (*config_mcast)(struct _lm_device_t *pdev,
				struct ecore_mcast_ramrod_params *p, int cmd);

	/** 
	 * Fills the ramrod data during the RESTORE flow. 
	 *  
	 * @param pdev 
	 * @param o 
	 * @param start_idx Registry index to start from 
	 * @param rdata_idx Index in the ramrod data to start from 
	 *  
	 * @return -1 if we handled the whole registry or index of the last 
	 *  	   handled registry element.
	 */
	int (*hdl_restore)(struct _lm_device_t *pdev, struct ecore_mcast_obj *o,
			   int start_bin, int *rdata_idx);

	int (*enqueue_cmd)(struct _lm_device_t *pdev, struct ecore_mcast_obj *o,
			   struct ecore_mcast_ramrod_params *p, int cmd);

	void (*set_one_rule)(struct _lm_device_t *pdev,
				 struct ecore_mcast_obj *o, int idx,
				 union ecore_mcast_config_data *cfg_data, int cmd);

	/** Checks if there are more mcast MACs to be set or a previous
	 *  command is still pending.
	 */
	BOOL (*check_pending)(struct ecore_mcast_obj *o);

	/**
	 * Set/Clear/Check SCHEDULED state of the object
	 */
	void (*set_sched)(struct ecore_mcast_obj *o);
	void (*clear_sched)(struct ecore_mcast_obj *o);
	BOOL (*check_sched)(struct ecore_mcast_obj *o);

	/* Wait until all pending commands complete */
	int (*wait_comp)(struct _lm_device_t *pdev, struct ecore_mcast_obj *o);

	/**
	 * Handle the internal object counters needed for proper
	 * commands handling. Checks that the provided parameters are
	 * feasible.
	 */
	int (*preamble)(struct _lm_device_t *pdev, struct ecore_mcast_ramrod_params *p,
			int cmd);

	/**
	 * Restore the values of internal counters in case of a failure.
	 */
	void (*postmortem)(struct _lm_device_t *pdev,
			   struct ecore_mcast_ramrod_params *p,
			   int old_num_bins);

	int (*get_registry_size)(struct ecore_mcast_obj *o);
	void (*set_registry_size)(struct ecore_mcast_obj *o, int n);
};

/*************************** Credit handling **********************************/
#define MAX_CAM_TABLE_SIZE 192 //this is 96 addresses per port.

struct ecore_credit_pool_obj {

	/* Current amount of credit in the pool */
	atomic_t		credit;

	/* Maximum allowed credit. put() will check against it. */
	int 		cam_sz;

	u8 cam_mirror[MAX_CAM_TABLE_SIZE];
    

	/* Base cam offset (initialized differently */
	u8  		base_cam_offset;

	/**
	 * Looks for a new cam offset and occupies it.
	 *
	 * @return cam_offset, or ECORE_NOMEM if none available
	 */
	BOOL (*get)(struct ecore_credit_pool_obj *o, u8 * offset);

	/**
	 * Releases the offset
	 *
	 * @param offset Which offset to return
	 * @return TRUE if the operation is successful
	 */
	BOOL (*put)(struct ecore_credit_pool_obj *o, u8 offset);

	/**
	 * Get the requested amount of credit from the pool.
	 *
	 * @param cnt Amount of requested credit
	 * @return TRUE if the operation is successful
	 */
	BOOL (*get_mult)(struct ecore_credit_pool_obj *o, int cnt);

	/**
	 * Returns the credit to the pool.
	 *
	 * @param cnt Amount of credit to return
	 * @return TRUE if the operation is successful
	 */
	BOOL (*put_mult)(struct ecore_credit_pool_obj *o, int cnt);

	/**
	 * Reads the current amount of credit.
	 */
	int (*check)(struct ecore_credit_pool_obj *o);
};

/*************************** RSS configuration ********************************/

enum {
	/* RSS_MODE bits are mutually exclusive */
	ECORE_RSS_MODE_DISABLED,
	ECORE_RSS_MODE_REGULAR,
	ECORE_RSS_MODE_VLAN_PRI,
	ECORE_RSS_MODE_E1HOV_PRI,
	ECORE_RSS_MODE_IP_DSCP,

	ECORE_RSS_SET_SRCH, /* Setup searcher, E1x specific flag */

	ECORE_RSS_IPV4,
	ECORE_RSS_IPV4_TCP,
	ECORE_RSS_IPV6,
	ECORE_RSS_IPV6_TCP,
};

struct ecore_config_rss_params {
	struct ecore_rss_config_obj *rss_obj;

	/* may have RAMROD_COMP_WAIT set only */
	unsigned long   ramrod_flags;

	/* ECORE_RSS_X bits */
	unsigned long   rss_flags;

	/* Number hash bits to take into an account */
	u8  	rss_result_mask;

	/* Indirection table */
	u8  	ind_table[T_ETH_INDIRECTION_TABLE_SIZE];

	/* RSS hash values */
	u32 	rss_key[10];

	/* valid only iff ECORE_RSS_UPDATE_TOE is set */
	u16 	toe_rss_bitmap;
};

struct ecore_rss_config_obj {
	struct ecore_raw_obj raw;

	u8 engine_id;

	int (*config_rss)(struct _lm_device_t *pdev,
			  struct ecore_config_rss_params *p);
};

/********************** Client state update ***********************************/
enum {
	ECORE_CL_UPDATE_IN_VLAN_REM,
	ECORE_CL_UPDATE_IN_VLAN_REM_CHNG,
	ECORE_CL_UPDATE_OUT_VLAN_REM,
	ECORE_CL_UPDATE_OUT_VLAN_REM_CHNG,
	ECORE_CL_UPDATE_ANTI_SPOOF,
	ECORE_CL_UPDATE_ANTI_SPOOF_CHNG,
	ECORE_CL_UPDATE_ACTIVATE,
	ECORE_CL_UPDATE_ACTIVATE_CHNG,
	ECORE_CL_UPDATE_DEF_VLAN_EN,
	ECORE_CL_UPDATE_DEF_VLAN_EN_CHNG,
};

struct ecore_client_update_params {
	unsigned long   update_flags;
	u32 	cid;
	u16 	def_vlan;
	u8  	cl_id;

	void		*rdata;
	lm_address_t	rdata_mapping;
};


/********************** Interfaces ********************************************/
/********************* VLAN-MAC ****************/
void ecore_init_mac_obj(struct _lm_device_t *pdev,
			struct ecore_vlan_mac_obj *mac_obj,
			u8 cl_id, u32 cid, u8 func_id, void *rdata,
			lm_address_t rdata_mapping, int state,
			unsigned long *pstate, ecore_obj_type type,
			struct ecore_credit_pool_obj *macs_pool);

void ecore_init_vlan_obj(struct _lm_device_t *pdev,
			 struct ecore_vlan_mac_obj *vlan_obj,
			 u8 cl_id, u32 cid, u8 func_id, void *rdata,
			 lm_address_t rdata_mapping, int state,
			 unsigned long *pstate, ecore_obj_type type,
			 struct ecore_credit_pool_obj *vlans_pool);

void ecore_init_vlan_mac_obj(struct _lm_device_t *pdev,
				 struct ecore_vlan_mac_obj *vlan_mac_obj,
				 u8 cl_id, u32 cid, u8 func_id, void *rdata,
				 lm_address_t rdata_mapping, int state,
				 unsigned long *pstate, ecore_obj_type type,
				 struct ecore_credit_pool_obj *macs_pool,
				 struct ecore_credit_pool_obj *vlans_pool);

int ecore_config_vlan_mac(struct _lm_device_t *pdev,
			  struct ecore_vlan_mac_ramrod_params *p, BOOL add);

int ecore_vlan_mac_move(struct _lm_device_t *pdev,
			  struct ecore_vlan_mac_ramrod_params *p, 
			  struct ecore_vlan_mac_obj *dest_o);

/********************* RX MODE ****************/

void ecore_init_rx_mode_obj(struct _lm_device_t *pdev, struct ecore_rx_mode_obj *o);

/**
 * Send and RX_MODE ramrod according to the provided parameters.
 *
 * @param pdev
 * @param p Command parameters
 *
 * @return 0 - if operation was successfull and there is no pending completions,
 *  	   positive number - if there are pending completions,
 *  	   negative - if there were errors
 */
int ecore_config_rx_mode(struct _lm_device_t *pdev, struct ecore_rx_mode_ramrod_params *p);

/****************** MULTICASTS ****************/

void ecore_init_mcast_obj(struct _lm_device_t *pdev,
			  struct ecore_mcast_obj *mcast_obj,
			  u8 mcast_cl_id, u32 mcast_cid, u8 func_id,
			  u8 engine_id, void *rdata, lm_address_t rdata_mapping,
			  int state, unsigned long *pstate,
			  ecore_obj_type type);

/**
 * Configure multicast MACs list. May configure a new list
 * provided in p->mcast_list (ECORE_MCAST_CMD_ADD), clean up 
 * (ECORE_MCAST_CMD_DEL) or restore (ECORE_MCAST_CMD_RESTORE) a current 
 * configuration, continue to execute the pending commands 
 * (ECORE_MCAST_CMD_CONT). 
 *
 * If previous command is still pending or if number of MACs to
 * configure is more that maximum number of MACs in one command,
 * the current command will be enqueued to the tail of the
 * pending commands list.
 *
 * @param pdev
 * @param p
 * @param command to execute: ECORE_MCAST_CMD_X
 *
 * @return 0 is operation was sucessfull and there are no pending completions,
 *  	   negative if there were errors, positive if there are pending
 *  	   completions.
 */
int ecore_config_mcast(struct _lm_device_t *pdev,
			   struct ecore_mcast_ramrod_params *p,
			   int cmd);

/****************** CREDIT POOL ****************/
void ecore_init_mac_credit_pool(struct _lm_device_t *pdev,
				struct ecore_credit_pool_obj *p, u8 func_id, 
				u8 func_num);
void ecore_init_vlan_credit_pool(struct _lm_device_t *pdev,
				struct ecore_credit_pool_obj *p, u8 func_id,
				u8 func_num);


/****************** RSS CONFIGURATION ****************/
void ecore_init_rss_config_obj(struct _lm_device_t *pdev,
				   struct ecore_rss_config_obj *rss_obj,
				   u8 cl_id, u32 cid, u8 func_id, u8 engine_id,
				   void *rdata, lm_address_t rdata_mapping, int state,
				   unsigned long *pstate, ecore_obj_type type);

/**
 * Updates RSS configuration according to provided parameters.
 *
 * @param pdev
 * @param p
 *
 * @return 0 in case of success
 */
int ecore_config_rss(struct _lm_device_t *pdev, struct ecore_config_rss_params *p);

/****************** CLIENT STATE UPDATE ****************/

/**
 * Update a state of the existing Client according to the
 * provided parameters.
 *
 * @param pdev
 * @param params Set of Client parameters to update.
 *
 * @return int
 */
#if 0 // not used yet
int ecore_fw_cl_update(struct _lm_device_t *pdev,
			   struct ecore_client_update_params *params);
#endif

#endif /* ECORE_SP_VERBS */
