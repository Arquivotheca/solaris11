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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _AMD_IOMMU_PAGE_TABLES_H
#define	_AMD_IOMMU_PAGE_TABLES_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL

/* Common to PTEs and PDEs */
#define	AMD_IOMMU_PTDE_IW		(62 << 16 | 62)
#define	AMD_IOMMU_PTDE_IR		(61 << 16 | 61)
#define	AMD_IOMMU_PTDE_ADDR		(51 << 16 | 12)
#define	AMD_IOMMU_PTDE_NXT_LVL		(11 << 16 | 9)
#define	AMD_IOMMU_PTDE_PR		(0 << 16 | 0)

#define	AMD_IOMMU_PTE_FC		(60 << 16 | 60)
#define	AMD_IOMMU_PTE_U			(59 << 16 | 59)

/* Easier defines for the above */
#define	AMD_IOMMU_PTE_WMASK		((uint64_t)1 << 62)
#define	AMD_IOMMU_PTE_RMASK		((uint64_t)1 << 61)
#define	AMD_IOMMU_PTE_FCMASK		((uint64_t)1 << 60)
#define	AMD_IOMMU_PTE_NEXTLEV(n)	((uint64_t)n << 9)
#define	AMD_IOMMU_PTE_PMASK		((uint64_t)1 << 0)

#define	AMD_IOMMU_VA_NBITS(l)		((l) == 6 ? 7 : 9)
#define	AMD_IOMMU_VA_BITMASK(l)		((1 << AMD_IOMMU_VA_NBITS(l)) - 1)
#define	AMD_IOMMU_VA_SHIFT(v, l)	\
	((v) >> (MMU_PAGESHIFT + (AMD_IOMMU_VA_NBITS(l - 1) * (l - 1))))
#define	AMD_IOMMU_VA_BITS(v, l)		\
	(AMD_IOMMU_VA_SHIFT(v, l) & AMD_IOMMU_VA_BITMASK(l))
#define	AMD_IOMMU_VA_TOTBITS(l)		\
	(((l) == 6 ? 7 + (l - 1) * 9: l*9) + MMU_PAGESHIFT)
#define	AMD_IOMMU_VA_TOTMASK(l)		((1 << AMD_IOMMU_VA_TOTBITS(l)) - 1)
#define	AMD_IOMMU_VA_INVAL_SETMASK(l)	\
	(((1 << AMD_IOMMU_VA_TOTBITS(l)) - 1) >> 1)
#define	AMD_IOMMU_VA_INVAL_CLRMASK(l)	\
	(~(1 << (AMD_IOMMU_VA_TOTBITS(l) - 1)))
#define	AMD_IOMMU_VA_INVAL(v, l)	\
	(((v) & AMD_IOMMU_VA_INVAL_CLRMASK(l)) | AMD_IOMMU_VA_INVAL_SETMASK(l))

#define	AMD_IOMMU_PGTABLE_SZ		(4096)
#define	AMD_IOMMU_PGTABLE_MAXLEVEL	(6)
#define	AMD_IOMMU_PGTABLE_HASH_SZ	(256)

#define	AMD_IOMMU_PGTABLE_ALIGN		((1ULL << 12) - 1)
#define	AMD_IOMMU_PGTABLE_SIZE		(1ULL << 12)

#define	AMD_IOMMU_MAX_PDTE		(1ULL << AMD_IOMMU_VA_NBITS(1))
#define	PT_REF_VALID(p)			((p)->pt_ref >= 0 && \
					(p)->pt_ref <= AMD_IOMMU_MAX_PDTE)

#define	AMD_IOMMU_DOMAIN_HASH_SZ	(256)
#define	AMD_IOMMU_PGTABLE_FREELIST_MAX	(256)
#define	AMD_IOMMU_PA2VA_HASH_SZ		(256)

#define	AMD_IOMMU_SIZE_4G		((uint64_t)1 << 32)
#define	AMD_IOMMU_VMEM_NAMELEN		(30)

typedef enum {
	AMD_IOMMU_INVALID_DOMAIN = 0,
	AMD_IOMMU_IDENTITY_DOMAIN = 0xFFFD,
	AMD_IOMMU_PASSTHRU_DOMAIN = 0xFFFE,
	AMD_IOMMU_SYS_DOMAIN = 0xFFFF
} domain_id_t;

typedef enum {
	AMD_IOMMU_INVALID_MAP = 0,
	AMD_IOMMU_UNITY_MAP,
	AMD_IOMMU_VMEM_MAP
} map_type_t;

void amd_iommu_init_page_tables(amd_iommu_t *iommu);
void amd_iommu_fini_page_tables(amd_iommu_t *iommu);
void amd_iommu_set_passthru(amd_iommu_t *iommu, dev_info_t *rdip);
int amd_iommu_set_devtbl_entry(amd_iommu_t *iommu, iommu_domain_t *domain,
    uint16_t deviceid);

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _AMD_IOMMU_PAGE_TABLES_H */
