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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains private defines and declarations for the
 * Suspend and Resume (a.k.a. CPR) functionality.  It contains
 * the definitions that would be common to all architectures, as
 * well as prototype declarations to those functions that would
 * be in architecture-specific files.
 * This file should not be in a release and is only used to build
 * the kernel modules.
 */

#ifndef _SYS_CPR_PRIV_H
#define	_SYS_CPR_PRIV_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL

extern int	cpr_debug;

/* Default platform defines */
#if defined(__sparc)
#define	__S4
#undef	__S3
#endif	/* __sparc */

#if defined(__x86)
#define	__S3
#define	__S4
#endif	/* __x86 */

/* Legacy "S" definitions */
#define	CPR_TORAM	SYSTEM_POWER_S3
#define	CPR_TODISK	SYSTEM_POWER_S4

#ifndef _ASM
/*
 * These functions/values are used within the CPR module.
 */
extern int	cpr_is_zfs(struct vfs *);
extern int	cpr_check_spec_statefile(void);
extern int	cpr_reusable_mount_check(void);
extern int	cpr_init(int);
extern void	cpr_done(void);
extern void	cpr_forget_cprconfig(void);
extern int	cpr_suspend_succeeded;

/*
 * These functions need to be defined in the implementation-specific
 * source files.
 */
extern void	i_cpr_stop_other_cpus(void);
extern int	i_cpr_power_down(void);
extern int	i_cpr_checkargs(int fcn, void *mdep);
extern int	i_cpr_is_supported(int sleeptype);
extern int	i_cpr_check_cprinfo(void);
extern int	i_cpr_reusable_supported(void);
extern int	i_cpr_reuseinit(void);
extern int	i_cpr_reusefini(void);
extern int	i_cpr_get_config_path(char *);


/* Defined in cpr.h, and may need to move here. */
extern char *cpr_build_statefile_path(void);
extern char *cpr_enumerate_promprops(char **, size_t *);
extern char *cpr_get_statefile_prom_path(void);
extern int cpr_contig_pages(vnode_t *, int);
extern int cpr_default_setup(int);
extern int cpr_alloc_bufs(void);
extern int cpr_dump(vnode_t *);
extern int cpr_get_reusable_mode(void);
extern int cpr_isset(pfn_t, int);
extern int cpr_main(void);
extern int cpr_mp_offline(void);
extern int cpr_mp_online(void);
extern int cpr_nobit(pfn_t, int);
extern int cpr_open_deffile(int, vnode_t **);
extern int cpr_read_cdump(int, cdd_t *, ushort_t);
extern int cpr_read_cprinfo(int, char *, char *);
extern int cpr_read_machdep(int, caddr_t, size_t);
extern int cpr_read_phys_page(int, uint_t, int *);
extern int cpr_read_terminator(int, ctrm_t *, caddr_t);
extern int cpr_resume_devices(dev_info_t *, int);
extern int cpr_set_properties(int);
extern int cpr_statefile_is_spec(int);
extern int cpr_statefile_offset(void);
extern int cpr_stop_kernel_threads(void);
extern int cpr_threads_are_stopped(void);
extern int cpr_stop_user_threads(void);
extern int cpr_suspend_devices(dev_info_t *);
extern int cpr_validate_definfo(int);
extern int cpr_write(vnode_t *, caddr_t, size_t);
extern int cpr_update_nvram(cprop_t *);
extern int cpr_write_deffile(cdef_t *);
extern int i_cpr_alloc_bitmaps(void);
extern int i_cpr_dump_sensitive_kpages(vnode_t *);
extern int i_cpr_save_sensitive_kpages(void);
extern pgcnt_t cpr_count_kpages(int, bitfunc_t);
extern pgcnt_t cpr_count_pages(caddr_t, size_t, int, bitfunc_t, int);
extern pgcnt_t cpr_count_volatile_pages(int, bitfunc_t);
extern pgcnt_t i_cpr_count_sensitive_kpages(int, bitfunc_t);
extern pgcnt_t i_cpr_count_special_kpages(int, bitfunc_t);
extern pgcnt_t i_cpr_count_storage_pages(int, bitfunc_t);
extern ssize_t cpr_get_machdep_len(int);
extern void cpr_clear_definfo(void);
extern void cpr_restore_time(void);
extern void cpr_save_time(void);
extern void cpr_show_range(char *, size_t, int, bitfunc_t, pgcnt_t);
extern void cpr_signal_user(int sig);
extern void cpr_spinning_bar(void);
extern void cpr_print_val(int);
extern void cpr_start_user_threads(void);
extern void cpr_stat_cleanup(void);
extern void cpr_stat_event_end(char *, cpr_time_t *);
extern void cpr_stat_event_print(void);
extern void cpr_stat_event_start(char *, cpr_time_t *);
extern void cpr_stat_record_events(void);
extern void cpr_tod_get(cpr_time_t *ctp);
extern void cpr_tod_status_set(int);
extern void i_cpr_bitmap_cleanup(void);
extern void i_cpr_stop_other_cpus(void);
extern void i_cpr_alloc_cpus(void);
extern void i_cpr_free_cpus(void);
extern void i_cpr_save_machdep_info(void);
extern void i_cpr_machdep_setup(void);
extern void cpr_clear_bitmaps(void);
extern int cpr_setbit(pfn_t ppn, int mapflag);
extern int cpr_clrbit(pfn_t ppn, int mapflag);
extern pgcnt_t cpr_scan_kvseg(int mapflag, bitfunc_t bitfunc, struct seg *seg);
extern pgcnt_t cpr_count_seg_pages(int mapflag, bitfunc_t bitfunc);
extern int cpr_verify_statefile(void);
extern void cpr_set_bitmap_size(void);
extern void cpr_notice(char *);
extern void cpr_send_notice(void);
extern void cpr_resume_notice(void);
#ifdef DEBUG
extern boolean_t cpr_cpus_parked(void);
#endif


#endif /* _ASM */
#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CPR_PRIV_H */
