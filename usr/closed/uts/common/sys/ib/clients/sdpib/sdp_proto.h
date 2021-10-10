/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_IB_CLIENTS_SDP_PROTO_H
#define	_SYS_IB_CLIENTS_SDP_PROTO_H

#ifdef	__cplusplus
extern "C" {
#endif

typedef sdp_dev_root_t *t_root_devp;

int sdp_ibt_alloc_open_rc_channel(sdp_conn_t *conn, ibt_path_info_t *path,
    sdp_msg_hello_t *hello);
int sdp_ibt_init(sdp_dev_root_t *global_state);
int sdp_ibt_fini(sdp_state_t *state);
int sdp_ibt_hca_attach(sdp_state_t *state, ibt_async_event_t *eventp);
int sdp_ibt_hca_detach(sdp_state_t *state, ibt_hca_hdl_t hca);

/*
 * Buffer management.
 */
sdp_buff_t *sdp_buff_get(sdp_conn_t *conn);
sdp_buff_t *sdp_buff_pool_get(sdp_pool_t *pool, int32_t fifo);
void sdp_buff_pool_put(sdp_pool_t *pool, sdp_buff_t *buff, int32_t fifo);
sdp_buff_t *sdp_buff_pool_look(sdp_pool_t *pool, int32_t fifo);
void sdp_buff_free(sdp_buff_t *buff);
void sdp_buff_pool_init(sdp_pool_t *pool);
void sdp_buff_pool_destroy(sdp_pool_t *pool);
void sdp_buff_pool_clear(sdp_pool_t *pool);
int sdp_buff_mine_data(sdp_pool_t *pool,
    int (*mine_func)(sdp_buff_t *buff, void *arg), void *usr_arg);
sdp_buff_t *sdp_buff_main_find(sdp_pool_t *pool,
    int (*test)(sdp_buff_t *buff, void *arg), void *usr_arg);

void sdp_buff_cache_create(sdp_dev_hca_t *hca);
void sdp_buff_cache_destroy(sdp_dev_hca_t *hca);

/*
 * SDP protocol processing
 */
int32_t sdp_inet_release(sdp_conn_t *conn);
void sdp_conn_abort(sdp_conn_t *conn);
int32_t sdp_sock_connect(sdp_conn_t *conn, ibt_cm_event_t *ibt_cm_event);
int32_t sdp_actv_connect(sdp_conn_t *conn);
void sdp_sock_buff_recv(sdp_conn_t *conn, sdp_buff_t *buff);
int32_t sdp_sock_abort(sdp_conn_t *conn);
void sdp_abort(sdp_conn_t *conn);
void sdp_conn_report_error(sdp_conn_t *conn, int error);
int32_t sdp_recv_post(sdp_conn_t *conn);
int32_t sdp_recv_post_buff_chain(sdp_conn_t *conn, sdp_buff_t *head);
void sdp_send_flush(sdp_conn_t *conn);

/*
 * Zcopy advertisement management
 */
int32_t sdp_conn_advt_init(void);
int32_t sdp_conn_advt_cleanup(void);
sdp_advt_table_t *sdp_conn_advt_table_create(int32_t *result);
int32_t sdp_conn_advt_table_init(sdp_advt_table_t *table);
void sdp_conn_advt_table_destroy(sdp_advt_table_t *table);
int32_t sdp_conn_advt_table_clear(sdp_advt_table_t *table);
sdp_advt_t *sdp_conn_advt_create(void);
int32_t sdp_conn_advt_destroy(sdp_advt_t *advt);
sdp_advt_t *sdp_conn_advt_table_get(sdp_advt_table_t *table);
sdp_advt_t *sdp_conn_advt_table_look(sdp_advt_table_t *table);
int32_t sdp_conn_advt_table_put(sdp_advt_table_t *table, sdp_advt_t *advt);

/*
 * Generic object managment
 */
void sdp_generic_table_remove(sdp_generic_t *element);
sdp_generic_t *sdp_generic_table_get_all(sdp_generic_table_t *table);
sdp_generic_t *sdp_generic_table_get_head(sdp_generic_table_t *table);
sdp_generic_t *sdp_generic_table_get_tail(sdp_generic_table_t *table);
void sdp_generic_table_put_head(sdp_generic_table_t *table,
    sdp_generic_t *element);
void sdp_generic_table_put_tail(sdp_generic_table_t *table,
    sdp_generic_t *element);
sdp_generic_t *sdp_generic_table_look_head(sdp_generic_table_t *table);
sdp_generic_t *sdp_generic_table_look_tail(sdp_generic_table_t *table);
int32_t sdp_generic_table_type_head(sdp_generic_table_t *table);
int32_t sdp_generic_table_type_tail(sdp_generic_table_t *table);
sdp_generic_t *sdp_generic_table_look_type_head(sdp_generic_table_t *table,
    sdp_generic_type_t type);
sdp_generic_t *sdp_generic_table_look_type_tail(sdp_generic_table_t *table,
    sdp_generic_type_t type);
sdp_generic_t *sdp_generic_table_lookup(sdp_generic_table_t *table,
    sdp_generic_lookup_func_t lookup_func, void *arg);
int32_t sdp_generic_table_size(sdp_generic_table_t *table);
sdp_generic_table_t *sdp_generic_table_create(int32_t *result);
int32_t sdp_generic_table_init(sdp_generic_table_t *table);
void sdp_generic_table_destroy(sdp_generic_table_t *table);
void sdp_generic_table_clear(sdp_generic_table_t *table);
int32_t sdp_generic_main_init(void);
int32_t sdp_generic_main_cleanup(void);
int32_t sdp_init(sdp_dev_root_t **global_state);

/*
 * Connection table
 */
int32_t sdp_conn_table_init(int32_t proto_family, int32_t conn_size,
    int32_t buff_size, int32_t buff_num, sdp_dev_root_t **global_state);
int32_t sdp_conn_table_clear(void);
sdp_conn_t *sdp_conn_allocate(boolean_t);
void sdp_conn_deallocate(sdp_conn_t *conn);
boolean_t sdp_conn_init_cred(sdp_conn_t *conn, cred_t *credp);
void sdp_conn_set_buff_limits(sdp_conn_t *);
int32_t sdp_conn_allocate_ib(sdp_conn_t *conn, ib_guid_t *hca_guid,
    uint8_t hw_port, boolean_t where);
void sdp_conn_deallocate_ib(sdp_conn_t *conn);
void sdp_conn_destruct(sdp_conn_t *conn);

/*
 * SDP port/queue management
 */
int32_t sdp_inet_accept_queue_put(sdp_conn_t *listen_conn,
    sdp_conn_t *accept_conn);
sdp_conn_t *sdp_inet_accept_queue_get(sdp_conn_t *listen_conn);
int32_t    sdp_inet_accept_queue_remove(sdp_conn_t *accept_conn,
    boolean_t return_locked);
int32_t    sdp_inet_listen_start(sdp_conn_t *listen_conn);
int32_t    sdp_inet_listen_stop(sdp_conn_t *listen_conn);
sdp_conn_t *sdp_inet_listen_lookup(sdp_conn_t *eager_conn, ibt_cm_event_t *ev);
int32_t    sdp_inet_port_get(sdp_conn_t *conn, sdp_binding_addr ver,
    uint16_t port);
int32_t    sdp_inet_port_put(sdp_conn_t *conn);
int32_t    sdp_inet_port_inherit(sdp_conn_t *parent, sdp_conn_t *child);

/*
 * SDP post functions
 */
int32_t    sdp_post_msg_hello(sdp_conn_t *conn);
int32_t    sdp_post_msg_hello_ack(sdp_conn_t *conn);

/*
 * SDP CM routines
 */
void    sdp_cm_disconnect(sdp_conn_t *conn);
int sdp_cm_error(sdp_conn_t *conn, int error);
void sdp_cm_to_error(sdp_conn_t *conn);
int32_t sdp_cm_accept(sdp_conn_t *conn);

/*
 * SDP RDMA/CTRL routines
 */
int32_t sdp_send_ctrl_ack(sdp_conn_t *conn);
void sdp_send_ctrl_disconnect(sdp_conn_t *conn);
int32_t sdp_send_ctrl_abort(sdp_conn_t *conn);
int32_t sdp_send_ctrl_send_sm(sdp_conn_t *conn);
int32_t sdp_send_ctrl_snk_avail(sdp_conn_t *conn,
    uint32_t size, uint32_t rkey, uint64_t addr);
int32_t sdp_send_ctrl_resize_buff_ack(sdp_conn_t *conn, uint32_t size);
int32_t sdp_send_ctrl_rdma_rd_comp(sdp_conn_t *conn, int32_t size);
int32_t sdp_send_ctrl_rdma_wr_comp(sdp_conn_t *conn, uint32_t size);
int32_t sdp_send_ctrl_mode_change(sdp_conn_t *conn, uchar_t mode);
int32_t sdp_send_ctrl_src_cancel(sdp_conn_t *conn);
int32_t sdp_send_ctrl_snk_cancel(sdp_conn_t *conn);
int32_t sdp_send_ctrl_snk_cancel_ack(sdp_conn_t *conn);

/*
 * Event functions
 */
int32_t sdp_event_send(sdp_conn_t *conn, ibt_wc_t *wc);
int32_t sdp_event_read(sdp_conn_t *conn, ibt_wc_t *wc);
int32_t sdp_event_write(sdp_conn_t *conn, ibt_wc_t *wc);
void sdp_rcq_handler(ibt_cq_hdl_t cq_hdl, void *arg);
void sdp_process_scq(sdp_conn_t *conn, ibt_cq_hdl_t cq_hdl);
void sdp_scq_handler(ibt_cq_hdl_t cq_hdl, void *arg);

/*
 * Inet functions
 */
int32_t sdp_inet_connect4(sdp_conn_t *conn, sin_t *sin, int32_t size);
int32_t sdp_inet_connect6(sdp_conn_t *conn, sin6_t *sin6, int32_t size);
int32_t sdp_inet_shutdown(int32_t flag, sdp_conn_t *conn);
int32_t sdp_inet_bind4(sdp_conn_t *conn, sin_t *uaddr, int32_t size);
int32_t sdp_inet_bind6(sdp_conn_t *conn, sin6_t *uaddr6, int32_t size);
int sdp_inet_listen(sdp_conn_t *conn, int backlog);
int sdp_pass_establish(sdp_conn_t *conn);

/*
 * Arp resolution
 */
int sdp_pr_lookup_init(void);
int sdp_pr_lookup_cleanup(void);
int sdp_pr_lookup(sdp_conn_t *daddr, uint8_t localroute,
	uint32_t bound_dev_if, sdp_pr_comp_func_t func, void *user_arg,
	sdp_lookup_id_t *lid);
int sdp_pr_lookup_cancel(sdp_lookup_id_t lid);
void sdp_prcache_cleanup(void);

/*
 * kstat related
 */
boolean_t sdp_param_register(caddr_t *, sdpparam_t *sdppa, int cnt);
boolean_t sdp_is_extra_priv_port(sdp_stack_t *sdps, uint16_t port);
kstat_t *sdp_kstat_init(netstackid_t, sdp_named_kstat_t *);
void sdp_kstat_fini(netstackid_t, kstat_t *);

/*
 * Misc
 */
void sdp_exit();
int32_t sdp_do_linger(sdp_conn_t *conn);
void sdp_clear_delete_list(void);
void sdp_wput_ioctl(queue_t *q, mblk_t *mp);
minor_t sdp_get_new_inst();
sdp_dev_hca_t *get_hcap_by_hdl(sdp_state_t *state, ibt_hca_hdl_t hdl);
#pragma inline(sdp_get_new_inst)

void *sdp_stack_init(netstackid_t, netstack_t *);
void sdp_stack_destroy(netstackid_t, void *);

int sdp_ioc_hca_offline(ib_guid_t, boolean_t, boolean_t);
int sdp_ioc_hca_online(ib_guid_t);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_PROTO_H */
