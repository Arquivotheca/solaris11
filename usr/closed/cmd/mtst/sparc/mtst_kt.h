/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MTST_KT_H
#define	_MTST_KT_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Support routine(s) located in mtst_kt.c
 */
extern	int	kt_adjust_buf_to_local(mdata_t *);
extern	int	kt_mem_is_local(cpu_info_t *, uint64_t);

/*
 * MACROs and definitions for determining memory locality and interleave.
 */
#define	KT_SYS_MODE_ID_MASK	0xf0		/* local node_id in bits[7:4] */
#define	KT_SYS_MODE_ID_SHIFT	4
#define	KT_SYS_MODE_GET_NODEID(reg)	(((reg) & KT_SYS_MODE_ID_MASK) \
					>> KT_SYS_MODE_ID_SHIFT)

#define	KT_SYS_MODE_MODE_MASK	(0x3 << 29)	/* mode in bits[30:29] */
#define	KT_SYS_MODE_MODE_SHIFT	29
#define	KT_SYS_MODE_GET_MODE(reg)	(((reg) & KT_SYS_MODE_MODE_MASK) \
					>> KT_SYS_MODE_MODE_SHIFT)
#define	KT_SYS_MODE_8MODE	0x3
#define	KT_SYS_MODE_4MODE	0x2
#define	KT_SYS_MODE_2MODE	0x1
#define	KT_SYS_MODE_1MODE	0x0

#define	KT_SYS_MODE_COUNT_MASK	(0x7 << 26)	/* node count (n - 1) [28:26] */
#define	KT_SYS_MODE_COUNT_SHIFT	26

#define	KT_COU_CEILING_MASK	0x3ff
#define	KT_COU_CEILING_SHIFT	34		/* effective on PA[43:34] */

#define	KT_PADDR_INTERLEAVE_MASK	(KT_COU_CEILING_MASK << \
						KT_COU_CEILING_SHIFT)
#define	KT_PADDR_INTERLEAVE_BITS(paddr)	\
			(((paddr) >> KT_COU_CEILING_SHIFT) & \
			KT_COU_CEILING_MASK)

#define	KT_IS_FINE_INTERLEAVE(reg, paddr) \
			((KT_PADDR_INTERLEAVE_BITS(paddr) >= \
			((reg) & KT_COU_CEILING_MASK)) ? 0 : 1)

#define	KT_2NODE_FINE_PADDR_NODEID_MASK	0x400		/* bit[10] */
#define	KT_4NODE_FINE_PADDR_NODEID_MASK	0xc00		/* bits[11:10] */
#define	KT_FINE_PADDR_NODEID_SHIFT	10

#define	KT_2NODE_FINE_PADDR_NODEID(paddr) \
		(((paddr) & KT_2NODE_FINE_PADDR_NODEID_MASK) >> \
		KT_FINE_PADDR_NODEID_SHIFT)

#define	KT_4NODE_FINE_PADDR_NODEID(paddr) \
		(((paddr) & KT_4NODE_FINE_PADDR_NODEID_MASK) >> \
		KT_FINE_PADDR_NODEID_SHIFT)

#define	KT_2NODE_COARSE_PADDR_NODEID_MASK	0x200000000ULL /* bit[33] */
#define	KT_4NODE_COARSE_PADDR_NODEID_MASK	0x600000000ULL /* bits[34:33] */
#define	KT_COARSE_PADDR_NODEID_SHIFT		33

#define	KT_2NODE_COARSE_PADDR_NODEID(paddr) \
		(((paddr) & KT_2NODE_COARSE_PADDR_NODEID_MASK) >> \
		KT_COARSE_PADDR_NODEID_SHIFT)

#define	KT_4NODE_COARSE_PADDR_NODEID(paddr) \
		(((paddr) & KT_4NODE_COARSE_PADDR_NODEID_MASK) >> \
		KT_COARSE_PADDR_NODEID_SHIFT)

#ifdef	__cplusplus
}
#endif

#endif	/* _MTST_KT_H */
