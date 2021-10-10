/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file may contain confidential information of LSI Corporation
 * and should not be distributed in source form without approval
 * from Sun Legal
 */

#ifndef _HDRS_LSILOGIC_H
#define	_HDRS_LSILOGIC_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Firmware image verification plugin definitions for LSI SAS Expanders
 */

#ifdef __cplusplus
extern "C" {
#endif


#define	MPI_FW_HEADER_SIGNATURE_0	(0x5AEAA55A)
#define	MPI_FW_HEADER_SIGNATURE_1	(0xA55AEAA5)
#define	MPI_FW_HEADER_SIGNATURE_2	(0x5AA55AEA)

/*
 * SEEPROM image identification data
 *
 * source: private email to RE
 *
 * SEEPROM Boot Record documentation:
 *
 * The seeprom contains a 64 byte header with an offset to the
 * boot record, and a 220 byte boot record.
 * The header is:
 *      0x01, 0x21, 0x41, 0x61 and 60 bytes of 0.
 * The boot record is located at offset 64.
 * See figure 2.7 and table 2.23 of the sasx36_tmv1_0.pdf
 * (the sasx36 technical manual)
 */


/*
 * Blade expander buffer notes
 *
 * source: private email to RE
 *
 * The SCSI Write Buffer IDs for the expander firmware are:
 *
 * 0  - The bootloader (not normally updated, offset 0 in the flash)
 * 2  - The firmware (the normal update image location)
 * 3  - The manufacturing data
 * 10 - The SEEPROM boot record image
 *
 * The SCSI Write Buffer mode value for the CDB is "2".
 */


#define	LSI_BOOTRECORD_BUFID	10
#define	LSI_STANDARD_BUFID	2
#define	LSI_MFGIMG_BUFID	3

/*
 * Product-specific identifiers follow.
 * NOTE: we need a reference document/url for these
 *
 * Until the next generation of expanders is delivered
 * (with useful firmware identifying data), we need to
 * keep track of each of these Product-specific identifiers
 * here. It is the responsibility of each product team that
 * wants support for their product in fwflash to log an RFE
 * requesting so, and provide the ProductID and scsi INQUIRY
 * data that they present to the OS.
 */

#define	VELANEMPLUS_PRODUCT_ID		0x40
#define	DORADO_PRODUCT_ID		0x41
#define	LOKI_PRODUCT_ID			0x42
#define	HYDRANEM_PRODUCT_ID		0x43
#define	GOAC48_PRODUCT_ID		0x44
#define	MONGO_PRODUCT_ID		0x45

/* Keep hassling Quanta for these values */
#define	ALAMO_PRODUCT_ID		0x00
#define	BERET_PRODUCT_ID		0x00
#define	WITTE_PRODUCT_ID		0x00
#define	RIVERWALK_PRODUCT_ID		0x00



#pragma pack(1)

struct MPI_FW_VERSION {
	uchar_t	Dev;
	uchar_t	Unit;
	uchar_t	Minor;
	uchar_t	Major;
};


#define	MPI_FW_HEADER_SIGNATURE_0	(0x5AEAA55A)
#define	MPI_FW_HEADER_SIGNATURE_1	(0xA55AEAA5)
#define	MPI_FW_HEADER_SIGNATURE_2	(0x5AA55AEA)


struct MPI_FW_HEADER {
	uint32_t	ArmBranchInstruction0;
	uint32_t	Signature0;
	uint32_t	Signature1;
	uint32_t	Signature2;
	uint32_t	ArmBranchInstruction1;
	uint32_t	ArmBranchInstruction2;
	uint32_t	Reserved;
	uint32_t	Checksum;
	uint16_t	VendorId;
	uint16_t	ProductId;
	struct MPI_FW_VERSION	FWVersion;
	uint32_t	SeqCodeVersion;
	uint32_t	ImageSize;
	uint32_t	NextImageHeaderOffset;
	uint32_t	LoadStartAddress;
	uint32_t	IopResetVectorValue;
	uint32_t	IopResetRegAddr;
	uint32_t	VersionNameWhat;
	uchar_t		VersionName[32];
	uint32_t	VendorNameWhat;
	uchar_t		VendorName[32];
};


/*
 * SEEPROM image identification data
 *
 * source: private email to RE
 *
 * SEEPROM Boot Record documentation:
 *
 * The seeprom contains a 64 byte header with an offset to the
 * boot record, and a 220 byte boot record.
 * The header is:
 *      0x01, 0x21, 0x41, 0x61 and 60 bytes of 0.
 * The boot record is located at offset 64.
 * See figure 2.7 and table 2.23 of the sasx36_tmv1_0.pdf
 * (the sasx36 technical manual)
 */

#define	ROUTEGRPSIZE_MASK	0xff00
#define	ROUTEGRPOFFSET_MASK	0x00ff

/* skip this number of bytes to get to the real boot record */
#define	MPI_BOOT_PROLOG		64

struct MPI_BOOT_HEADER {
	uint32_t	addr_base;
	uint8_t		reserved1[60];
	/* Signature is 'Y' 'e' 't' 'i' */
	uint8_t		Signature[4];
	uint64_t	exp_wwn;
	uint8_t		RptMfg04;
	uint8_t		RptMfg05;
	uint8_t		RptMfg06;
	uint8_t		RptMfg07;
	uint8_t		RptMfg08;
	uint8_t		RptMfg09;
	uint8_t		RptMfg0A;
	uint8_t		RptMfg0B;
	uchar_t		VendorId[8];
	uchar_t		ProductId[16];
	uchar_t		RevLevel[4];
	uint8_t		reserved2[3];
	uint8_t		RptMfgResv51;
	uint8_t		RptMfgVendorSpecific52;
	uint8_t		RptMfgVendorSpecific53;
	uint8_t		RptMfgVendorSpecific54;
	uint8_t		RptMfgVendorSpecific55;
	uint8_t		RptMfgVendorSpecific56;
	uint8_t		RptMfgVendorSpecific57;
	uint8_t		RptMfgVendorSpecific58;
	uint8_t		RptMfgVendorSpecific59;
	uint8_t		RptGenCfgreserved08;
	uint8_t		RptGenReserved1;
	uint8_t		RptGenCfgConfig;
	uint8_t		RptGenCfgreserved11;
	uint64_t	EnclLogIdent;
	uint8_t		RptGenRsvd20;
	uint8_t		RptGenRsvd21;
	uint8_t		RptGenRsvd22;
	uint8_t		RptGenRsvd23;
	uint8_t		RptGenRsvd24;
	uint8_t		RptGenRsvd25;
	uint8_t		RptGenRsvd26;
	uint8_t		RptGenRsvd27;
	uint32_t	SpinupCtrl;
	uint8_t		PhyEnables[8];
	uint8_t		reserved3[8];
	uint8_t		TxPolarity[8];
	uint8_t		RxPolarity[8];
	uint8_t		DefaultConnectorInfo[4];
#define	CONNTYPE_OFF		1
#define	CONNELEMIDX_OFF		2
#define	CONNPHYSLINK_OFF	3

	uint32_t	PhyCfg;
#define	GBConfig1	PhyCfg

	uint32_t	PhyRxCtrl;
#define	GBConfig2	PhyRxCtrl

	uint32_t	PhyTxSasCtrl;
#define	GBConfig3	PhyTxSasCtrl

	uint32_t	PhyTxSataCtrl;
#define	GBConfig4	PhyTxSataCtrl

	uint8_t		SubtractiveRouting[8];
	uint8_t		TableRouting[8];

	uint8_t		SGPIOClkDivHi;
	uint8_t		SGPIOClkDivLo;
	uint8_t		reserved4;
	uint8_t		SGPIODriveCount;

	uint16_t	RouteGrp0;
	uint16_t	RouteGrp1;
	uint16_t	RouteGrp2;
	uint16_t	RouteGrp3;
	uint16_t	RouteGrp4;
	uint16_t	RouteGrp5;
	uint16_t	RouteGrp6;
	uint16_t	RouteGrp7;
	uint16_t	RouteGrp8;
	uint16_t	RouteGrp9;

	uint8_t		RouteGrpPHY3;
	uint8_t		RouteGrpPHY2;
	uint8_t		RouteGrpPHY1;
	uint8_t		RouteGrpPHY0;

	uint8_t		RouteGrpPHY7;
	uint8_t		RouteGrpPHY6;
	uint8_t		RouteGrpPHY5;
	uint8_t		RouteGrpPHY4;

	uint8_t		RouteGrpPHY11;
	uint8_t		RouteGrpPHY10;
	uint8_t		RouteGrpPHY9;
	uint8_t		RouteGrpPHY8;

	uint8_t		RouteGrpPHY15;
	uint8_t		RouteGrpPHY14;
	uint8_t		RouteGrpPHY13;
	uint8_t		RouteGrpPHY12;

	uint8_t		RouteGrpPHY19;
	uint8_t		RouteGrpPHY18;
	uint8_t		RouteGrpPHY17;
	uint8_t		RouteGrpPHY16;

	uint8_t		RouteGrpPHY23;
	uint8_t		RouteGrpPHY22;
	uint8_t		RouteGrpPHY21;
	uint8_t		RouteGrpPHY20;

	uint8_t		RouteGrpPHY27;
	uint8_t		RouteGrpPHY26;
	uint8_t		RouteGrpPHY25;
	uint8_t		RouteGrpPHY24;

	uint8_t		RouteGrpPHY31;
	uint8_t		RouteGrpPHY30;
	uint8_t		RouteGrpPHY29;
	uint8_t		RouteGrpPHY28;

	uint8_t		RouteGrpPHY35;
	uint8_t		RouteGrpPHY34;
	uint8_t		RouteGrpPHY33;
	uint8_t		RouteGrpPHY32;

	uint8_t		reserved5[4];
	uint8_t		BootCtrl0;
	uint8_t		BootCtrl1;
	uint8_t		BootCtrl2;
#define	HEARTBEATGPIONUM_MASK	0x1C
#define	HEARTBEATENABLE_MASK	0x02
#define	EXTRECORDPRESENT_MASK	0x01

	uint8_t		BootChecksum;
	uint32_t	*OptionalCfgRecord;
};

#pragma pack()

#ifdef __cplusplus
}
#endif

#endif /* _HDRS_LSILOGIC_H */
