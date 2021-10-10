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

#ifndef	_SYS_VBE_H
#define	_SYS_VBE_H

#ifdef __cplusplus
extern "C" {
#endif

struct VbeInfoBlock {
	uint8_t		VbeSignature[4];
	uint16_t 	VbeVersion;
	uint32_t 	OemStringPtr;
	uint32_t 	Capabilities;
	uint32_t 	ModeListPtr;
	uint16_t 	TotalMemory;
	uint16_t 	OemSoftwareRev;
	uint32_t 	OemVendorNamePtr;
	uint32_t 	OemProductNamePtr;
	uint32_t 	OemProductRevPtr;
	uint8_t  	Reserved[222];
	uint8_t  	OemData[256];
} __attribute__((packed));

struct ModeInfoBlock {
	uint16_t 	ModeAttributes;
	uint8_t  	WinAAttributes;
	uint8_t  	WinBAttributes;
	uint16_t 	WinGranularity;
	uint16_t 	WinSize;
	uint16_t 	WinASegment;
	uint16_t 	WinBSegment;
	uint32_t 	WinFuncPtr;
	uint16_t 	BytesPerScanLine;

	/* >= 1.2 */
	uint16_t 	XResolution;
	uint16_t 	YResolution;
	uint8_t  	XCharSize;
	uint8_t  	YCharSize;
	uint8_t  	NumberOfPlanes;
	uint8_t  	BitsPerPixel;
	uint8_t  	NumberOfBanks;
	uint8_t  	MemoryModel;
	uint8_t  	BankSize;
	uint8_t  	NumberOfImagePages;
	uint8_t  	Reserved1;

	/* direct color */
	uint8_t  	RedMaskSize;
	uint8_t  	RedFieldPosition;
	uint8_t  	GreenMaskSize;
	uint8_t  	GreenFieldPosition;
	uint8_t  	BlueMaskSize;
	uint8_t  	BlueFieldPosition;
	uint8_t  	RsvdMaskSize;
	uint8_t  	RsvdFieldPosition;
	uint8_t  	DirectColorModeInfo;

	/* >= 2.0 */
	uint32_t 	PhysBasePtr;
	uint8_t  	Reserved2[6];

	/* >= 3.0 */
	uint16_t 	LinBytesPerScanLine;
	uint8_t  	BnkNumberOfImagePages;
	uint8_t  	LinNumberOfImagePages;
	uint8_t  	LinRedMaskSize;
	uint8_t  	LinRedFieldPosition;
	uint8_t  	LinGreenMaskSize;
	uint8_t  	LinGreenFieldPosition;
	uint8_t  	LinBlueMaskSize;
	uint8_t  	LinBlueFieldPosition;
	uint8_t  	LinRsvdMaskSize;
	uint8_t  	LinRsvdFieldPosition;
	uint32_t 	MaxPixelClock;
	uint8_t  	Reserved3[189];
} __attribute__((packed));

#define	VBE_VERSION_MAJOR(x)    (((x) >> 8) & 0xff)

#define	VBE_MM_TEXT		0
#define	VBE_MM_PACKED_PIXEL	4
#define	VBE_MM_DIRECT_COLOR	6

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_VBE_H */
