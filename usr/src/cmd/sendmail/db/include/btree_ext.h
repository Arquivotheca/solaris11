/* DO NOT EDIT: automatically built by dist/distrib. */
#ifndef _btree_ext_h_
#define _btree_ext_h_
int __bam_cmp __P((DB *, const DBT *,
   PAGE *, u_int32_t, int (*)(const DBT *, const DBT *)));
int __bam_defcmp __P((const DBT *, const DBT *));
size_t __bam_defpfx __P((const DBT *, const DBT *));
int __bam_pgin __P((db_pgno_t, void *, DBT *));
int __bam_pgout __P((db_pgno_t, void *, DBT *));
int __bam_mswap __P((PAGE *));
int __bam_cprint __P((DB *));
int __bam_ca_delete __P((DB *, db_pgno_t, u_int32_t, int));
void __bam_ca_di __P((DB *, db_pgno_t, u_int32_t, int));
void __bam_ca_dup __P((DB *,
   db_pgno_t, u_int32_t, u_int32_t, db_pgno_t, u_int32_t));
void __bam_ca_rsplit __P((DB *, db_pgno_t, db_pgno_t));
void __bam_ca_split __P((DB *,
   db_pgno_t, db_pgno_t, db_pgno_t, u_int32_t, int));
int __bam_c_init __P((DBC *));
int __bam_dup __P((DBC *, CURSOR *, u_int32_t, int));
int __bam_delete __P((DB *, DB_TXN *, DBT *, u_int32_t));
int __bam_ditem __P((DBC *, PAGE *, u_int32_t));
int __bam_adjindx __P((DBC *, PAGE *, u_int32_t, u_int32_t, int));
int __bam_dpage __P((DBC *, const DBT *));
int __bam_dpages __P((DBC *));
int __bam_open __P((DB *, DB_INFO *));
int __bam_close __P((DB *));
void __bam_setovflsize __P((DB *));
int __bam_read_root __P((DB *));
int __bam_new __P((DBC *, u_int32_t, PAGE **));
int __bam_lput __P((DBC *, DB_LOCK));
int __bam_free __P((DBC *, PAGE *));
int __bam_lt __P((DBC *));
int __bam_lget
   __P((DBC *, int, db_pgno_t, db_lockmode_t, DB_LOCK *));
int __bam_iitem __P((DBC *,
   PAGE **, db_indx_t *, DBT *, DBT *, u_int32_t, u_int32_t));
int __bam_ritem __P((DBC *, PAGE *, u_int32_t, DBT *));
int __bam_pg_alloc_recover
  __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_pg_free_recover
  __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_split_recover
  __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_rsplit_recover
  __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_adj_recover
  __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_cadjust_recover
  __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_cdel_recover
  __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_repl_recover
  __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __ram_open __P((DB *, DB_INFO *));
int __ram_close __P((DB *));
int __ram_c_del __P((DBC *, u_int32_t));
int __ram_c_get __P((DBC *, DBT *, DBT *, u_int32_t));
int __ram_c_put __P((DBC *, DBT *, DBT *, u_int32_t));
void __ram_ca __P((DB *, db_recno_t, ca_recno_arg));
int __ram_getno __P((DBC *, const DBT *, db_recno_t *, int));
int __bam_rsearch __P((DBC *, db_recno_t *, u_int32_t, int, int *));
int __bam_adjust __P((DBC *, int32_t));
int __bam_nrecs __P((DBC *, db_recno_t *));
db_recno_t __bam_total __P((PAGE *));
int __bam_search __P((DBC *,
    const DBT *, u_int32_t, int, db_recno_t *, int *));
int __bam_stkrel __P((DBC *, int));
int __bam_stkgrow __P((CURSOR *));
int __bam_split __P((DBC *, void *));
int __bam_copy __P((DB *, PAGE *, PAGE *, u_int32_t, u_int32_t));
int __bam_stat __P((DB *, void *, void *(*)(size_t), u_int32_t));
int __bam_pg_alloc_log
    __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
    u_int32_t, DB_LSN *, DB_LSN *, db_pgno_t,
    u_int32_t, db_pgno_t));
int __bam_pg_alloc_print
   __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_pg_alloc_read __P((void *, __bam_pg_alloc_args **));
int __bam_pg_free_log
    __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
    u_int32_t, db_pgno_t, DB_LSN *, const DBT *,
    db_pgno_t));
int __bam_pg_free_print
   __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_pg_free_read __P((void *, __bam_pg_free_args **));
int __bam_split_log
    __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
    u_int32_t, db_pgno_t, DB_LSN *, db_pgno_t,
    DB_LSN *, u_int32_t, db_pgno_t, DB_LSN *,
    const DBT *));
int __bam_split_print
   __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_split_read __P((void *, __bam_split_args **));
int __bam_rsplit_log
    __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
    u_int32_t, db_pgno_t, const DBT *, db_pgno_t,
    const DBT *, DB_LSN *));
int __bam_rsplit_print
   __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_rsplit_read __P((void *, __bam_rsplit_args **));
int __bam_adj_log
    __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
    u_int32_t, db_pgno_t, DB_LSN *, u_int32_t,
    u_int32_t, u_int32_t));
int __bam_adj_print
   __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_adj_read __P((void *, __bam_adj_args **));
int __bam_cadjust_log
    __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
    u_int32_t, db_pgno_t, DB_LSN *, u_int32_t,
    int32_t, int32_t));
int __bam_cadjust_print
   __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_cadjust_read __P((void *, __bam_cadjust_args **));
int __bam_cdel_log
    __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
    u_int32_t, db_pgno_t, DB_LSN *, u_int32_t));
int __bam_cdel_print
   __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_cdel_read __P((void *, __bam_cdel_args **));
int __bam_repl_log
    __P((DB_LOG *, DB_TXN *, DB_LSN *, u_int32_t,
    u_int32_t, db_pgno_t, DB_LSN *, u_int32_t,
    u_int32_t, const DBT *, const DBT *, u_int32_t,
    u_int32_t));
int __bam_repl_print
   __P((DB_LOG *, DBT *, DB_LSN *, int, void *));
int __bam_repl_read __P((void *, __bam_repl_args **));
int __bam_init_print __P((DB_ENV *));
int __bam_init_recover __P((DB_ENV *));
#endif /* _btree_ext_h_ */
