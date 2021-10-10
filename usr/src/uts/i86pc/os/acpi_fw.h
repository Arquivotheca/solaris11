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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2011, Intel Corporation.
 * All rights reserved.
 */

#ifndef _ACPI_FW_H
#define	_ACPI_FW_H

#ifdef __cplusplus
extern "C" {
#endif

extern void process_acpi_properties();

#define	ACPI_RSDP_SIG		"RSD PTR "
#define	ACPI_RSDP_SIG_LEN	(8)
#define	ACPI_TABLE_SIG_LEN	(4)
#define	ACPI_EBDA_SEG_ADDR	(0x40e)
#define	ACPI_EBDA_LEN		(1024)

#pragma pack(1)

struct rsdp_v1 {
	char	sig[8];
	uint8_t	checksum;
	char	oem_id[6];
	char	revision;
	uint32_t rsdt;
};

struct rsdp {
	struct rsdp_v1 v1;
	uint32_t len;
	uint64_t xsdt;
	uint8_t  ext_checksum;
	char	reserved[3];
};

struct table_header {
	char	 sig[4];
	uint32_t len;
	uint8_t	 revision;
	uint8_t	 checksum;
	char	 oem_id[6];
	char	 oem_table_id[8];
	uint32_t oem_revision;
	uint32_t creator_id;
	uint32_t creator_revision;
};

struct xsdt {
	struct table_header hdr;
	union {
		uint32_t r[1];
		uint64_t x[1];
	} p;
};


#define	MADT_PROCESSOR_LAPIC	0
#define	MADT_PROCESSOR_X2APIC	9

/* APIC flags field values */
#define	MADT_APIC_ENABLED	1

struct madt_entry_header {
	uint8_t	type;
	uint8_t	len;
};

struct madt_processor_lapic {
	struct madt_entry_header hdr;
	uint8_t	acpi_processor_id;
	uint8_t apic_id;
	uint32_t flags;
};

struct madt_processor_x2apic {
	struct madt_entry_header hdr;
	uint16_t	reserved;
	uint32_t	x2apic_id;
	uint32_t	flags;
	uint32_t	acpi_processor_uid;
};

struct madt {
	struct table_header	hdr;
	uint32_t		lapic_addr;
	uint32_t		flags;
	char			list[1];
};

struct srat_processor {
	uint8_t	domain1;
	uint8_t	apic_id;
	uint32_t flags;
	uint8_t  local_sapic_eid;
	uint8_t  domain2[3];
	uint8_t  reserved[4];
};

struct srat_x2apic {
	uint8_t reserved[2];
	uint32_t domain;
	uint32_t x2apic_id;
	uint32_t flags;
};

struct srat_memory {
	uint32_t domain;
	uint8_t	 reserved1[2];
	uint64_t base_addr;
	uint64_t len;
	uint8_t  reserved2[4];
	uint32_t flags;
	uint8_t  reserved3[8];
};

struct srat_item {
	uint8_t	type;
	uint8_t len;
	union {
		struct srat_processor p;
		struct srat_memory m;
		struct srat_x2apic xp;
	} i;
};

struct srat {
	struct table_header hdr;
	uint32_t reserved1;
	uint8_t  reserved2[8];
	struct srat_item list[1];
};

#define	SRAT_PROCESSOR	  (0)
#define	SRAT_MEMORY	  (1)
#define	SRAT_X2APIC	  (2)

#define	SRAT_ENABLED	  (1)
#define	SRAT_HOT_PLUG	  (2)
#define	SRAT_NON_VOLATILE (4)

/*
 * Pointer to System Resource Affinity Table (SRAT)
 */
extern struct srat	*srat_ptr;

struct slit {
	struct table_header hdr;
	uint64_t number;
	uint8_t  entry[1];
};

/*
 * Pointer to System Locality Information Table (SLIT)
 */
extern struct slit	*slit_ptr;

struct msct_proximity_domain {
	uint8_t revision;
	uint8_t length;
	uint32_t domain_min;
	uint32_t domain_max;
	uint32_t processor_max;
	uint64_t memory_max;
};

struct msct {
	struct table_header hdr;
	uint32_t proximity_domain_offset;
	uint32_t maximum_proximity_domains;
	uint32_t maximum_power_domains;
	uint64_t maximum_physical_address;
};

/*
 * Pointer to Maximum System Capability Table (MSCT)
 */
extern struct msct	*msct_ptr;

#define	PRCT_TYPE_MODULE	0x0
#define	PRCT_TYPE_MEMORY	0x1
#define	PRCT_TYPE_PROCESSOR	0x2

struct prct_device_cap {
	uint16_t		cap_type;
	uint32_t		cap_flags;
	uint8_t			cap_uid;
};

struct prct {
	struct table_header	hdr;
	uint32_t		number;
	struct prct_device_cap	entry[1];
};

struct cfg_base_addr_alloc {
	uint64_t	base_addr;
	uint16_t	segment;
	uint8_t		start_bno;
	uint8_t		end_bno;
	uint32_t	reserved;
};

struct mcfg {
	char		Signature[4];
	uint32_t	Length;
	uint8_t		Revision;
	uint8_t		Checksum;
	char		OemId[6];
	char		OemTableId[8];
	uint32_t	OemRevision;
	char		CreatorId[4];
	uint32_t	CreatorRevision;
	uint8_t		Reserved[8];
	struct cfg_base_addr_alloc	CfgBaseAddrAllocList[1];
};

struct dmar {
	struct table_header hdr;
	uint8_t width;
	uint8_t flags;
	uint8_t rsvd[10];
};


/*
 * Arbitrary limit on number of localities we handle; if
 * this limit is raised to more than UINT16_MAX, make sure
 * process_slit() knows how to handle it.
 */
#define	SLIT_LOCALITIES_MAX	(4096)

#define	SLIT_NUM_PROPNAME	"acpi-slit-localities"
#define	SLIT_PROPNAME		"acpi-slit"

#pragma pack()

#ifdef __cplusplus
}
#endif

#endif /* _ACPI_FW_H */
