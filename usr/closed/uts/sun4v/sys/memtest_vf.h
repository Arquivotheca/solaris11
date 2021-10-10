/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_VF_H
#define	_MEMTEST_VF_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Victoria Falls (UltraSPARC-T2plus) specific header file for memtest loadable
 * driver.
 */

/*
 * Test routines located in memtest_vf.c.
 */
extern	int	vf_inject_clc_err(mdata_t *);
extern	int	vf_inject_fbr_failover(mdata_t *);
extern	int	vf_inject_lfu_rtf_err(mdata_t *);
extern	int	vf_inject_lfu_to_err(mdata_t *);
extern	int	vf_inject_lfu_lf_err(mdata_t *);
extern	int	vf_inject_ncx_err(mdata_t *);

/*
 * Support routines located in memtest_vf.c.
 */
extern	void	vf_init(mdata_t *);
extern	int	vf_is_local_mem(mdata_t *, uint64_t);
extern	int	vf_pre_test_copy_asm(mdata_t *);
extern	int	vf_debug_print_esrs(mdata_t *);
extern	int	vf_debug_clear_esrs(mdata_t *);
extern	int	vf_debug_set_errorsteer(mdata_t *);
extern	int	vf_enable_errors(mdata_t *);
extern	int	vf_control_scrub(mdata_t *, uint64_t);

extern	int	vf_l2_h_invoke(mdata_t *);
extern	int	vf_l2_k_invoke(mdata_t *);
extern	int	vf_l2wb_invoke(mdata_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTEST_VF_H */
