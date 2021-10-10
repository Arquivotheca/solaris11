/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_USB_USBSER_EDGE_USBVEND_H
#define	_SYS_USB_USBSER_EDGE_USBVEND_H


/*
 * Vendor-specific USB definitions for Edgeport
 *
 * Copyright (c) 1998 Digi International, Inc., Inside Out
 * Networks, Inc.  All rights reserved.
 */


#ifdef	__cplusplus
extern "C" {
#endif

#define	ION_USB_VENDOR_ID	0x1608	/* I/O Networks VID */

/*
 * USB product IDs (PID) is broken into an OEM Id field (upper 6 bits)
 * and a Device Id (bottom 10 bits).
 *
 * OEM IDs:
 */
#define	ION_OEM_ID_ION		0	/* Inside Out Networks */
#define	ION_OEM_ID_NLYNX	1	/* NLynx Systems */
#define	ION_OEM_ID_GENERIC	2	/* Generic OEM */
#define	ION_OEM_ID_MAC		3	/* Mac Version */
#define	ION_OEM_ID_MEGAWOLF	4	/* Lupusb OEM Mac version (MegaWolf) */
#define	ION_OEM_ID_MULTITECH	5	/* Multitech Rapidports */

/*
 * The ION_DEVICE_ID_GENERATION_2 bit (0x20) will be ORed into the existing
 * edgeport PIDs to identify 80251+Netchip hardware.  This will guarantee
 * that if a second generation edgeport device is plugged into a PC with an
 * older (pre 2.0) driver, it will not enumerate.
 */
#define	ION_DEVICE_ID_GENERATION_2	0x020
#define	EDGEPORT_DEVICE_ID_MASK		0x3df	/* Not including GEN_2 bit */

/*
 * ION Device IDs
 */
#define	ION_DEVICE_ID_UNCONFIGURED	0x000	/* manufacturing only */
#define	ION_DEVICE_ID_EDGEPORT_4	0x001	/* Edgeport/4 RS232 */
#define	ION_DEVICE_ID_RAPIDPORT_4	0x003	/* Rapidport/4 */
#define	ION_DEVICE_ID_EDGEPORT_4T	0x004	/* Edgeport/4 RS232 */
#define	ION_DEVICE_ID_EDGEPORT_2	0x005	/* Edgeport/2 RS232 */
#define	ION_DEVICE_ID_EDGEPORT_4I	0x006	/* Edgeport/4 RS422 */
#define	ION_DEVICE_ID_EDGEPORT_2I	0x007	/* Edgeport/2 RS422/RS485 */
#define	ION_DEVICE_ID_EDGEPORT_421	0x00C	/* Edgeport/421 Hub+RS232+PP */
#define	ION_DEVICE_ID_EDGEPORT_21	0x00D	/* Edgeport/21 RS232+PP */
#define	ION_DEVICE_ID_EDGEPORT_8_DUAL_CPU 0x00E	/* Half of Edgeport/8 */
#define	ION_DEVICE_ID_EDGEPORT_8	0x00F	/* Edgeport/8 */
#define	ION_DEVICE_ID_EDGEPORT_2_DIN	0x010	/* Edgeport/2 RS232 Apple */
#define	ION_DEVICE_ID_EDGEPORT_4_DIN	0x011	/* Edgeport/4 RS232 Apple */
#define	ION_DEVICE_ID_EDGEPORT_16_DUAL_CPU 0x012 /* Half of Edgeport/16 */
#define	ION_DEVICE_ID_EDGEPORT_COMPATIBLE 0x013	/* Edgeport Compatible */
#define	ION_DEVICE_ID_EDGEPORT_8I	0x014	/* Edgeport/8 RS422 */

/* Edgeport TI based devices */
#define	ION_DEVICE_ID_TI_EDGEPORT_4	 0x0201  /* Edgeport/4 RS232 */
#define	ION_DEVICE_ID_TI_EDGEPORT_2	 0x0205  /* Edgeport/2 RS232 */
#define	ION_DEVICE_ID_TI_EDGEPORT_4I	 0x0206  /* Edgeport/4i RS422 */
#define	ION_DEVICE_ID_TI_EDGEPORT_2I	 0x0207  /* Edgeport/2i RS422/RS485 */
#define	ION_DEVICE_ID_TI_EDGEPORT_421	 0x020C  /* Edgeport/421 Hub+RS232+PP */
#define	ION_DEVICE_ID_TI_EDGEPORT_21	 0x020D  /* Edgeport/21 RS232+PP */
#define	ION_DEVICE_ID_TI_EDGEPORT_1	 0x0215  /* Edgeport/1 RS232 */
#define	ION_DEVICE_ID_TI_EDGEPORT_42	 0x0217  /* Edgeport/42 Hub+RS232 */
#define	ION_DEVICE_ID_TI_EDGEPORT_22	 0x021A  /* Edgeport/22  Edgeport/22I */
		/* is an Edgeport/4 with ports 1&2 RS422 and ports 3&4 RS232 */

#define	ION_DEVICE_ID_TI_EDGEPORT_421_BOOT 0x0240 /* Edgeport/421 boot mode */
#define	ION_DEVICE_ID_TI_EDGEPORT_421_DOWN 0x0241 /* Edgeport/421 downld mode */
#define	ION_DEVICE_ID_TI_EDGEPORT_21_BOOT 0x0242 /* Edgeport/21 boot mode */
#define	ION_DEVICE_ID_TI_EDGEPORT_21_DOWN 0x0243 /* Edgeport/42 downld mode */

#define	ION_PID_IS_TI(pid)		(((pid) & 0x0200) != 0)

/*
 * Parameters for download code
 *
 * max packet sizes for endpoints
 */
#define	EDGE_FW_BULK_MAX_PACKET_SIZE	64	/* bulk in endpoint (EP1) */
#define	EDGE_FW_INT_MAX_PACKET_SIZE	32	/* interrupt in endpoint */


/*
 * I/O Networks vendor-specific requests for default endpoint
 *
 *	bmRequestType = 00100000	Set vendor-specific, to device
 *	bmRequestType = 10100000	Get vendor-specific, to device
 *
 * These are the definitions for the bRequest field for the
 * above bmRequestTypes.
 *
 * For the read/write Edgeport memory commands, the parameters
 * are as follows:
 *		wValue = 16-bit address
 *		wIndex = unused (though we could put segment 00: or FF: here)
 *		wLength = # bytes to read/write (max 64)
 */

/* Warm reboot Edgeport, retaining USB address */
#define	USB_REQUEST_ION_RESET_DEVICE	0

/* Get Edgeport Compatibility Descriptor */
#define	USB_REQUEST_ION_GET_EPIC_DESC	1

/* Read  EdgePort RAM at specified addr */
#define	USB_REQUEST_ION_READ_RAM	3

/* Write EdgePort RAM at specified addr */
#define	USB_REQUEST_ION_WRITE_RAM	4

/* Read  EdgePort ROM at specified addr */
#define	USB_REQUEST_ION_READ_ROM	5

/* Write EdgePort ROM at specified addr */
#define	USB_REQUEST_ION_WRITE_ROM	6

/*
 * Begin execution of RAM-based download code
 * by jumping to address in wIndex:wValue
 */
#define	USB_REQUEST_ION_EXEC_DL_CODE	7

/* Enable/Disable suspend feature */
#define	USB_REQUEST_ION_ENABLE_SUSPEND	9

/*
 * Version 2 format of DeviceParams. This format is longer (3C0h)
 * and starts lower in memory, at the uppermost 1K in ROM
 */
#define	EDGE_MANUF_DESC_ADDR		0x00FF7C00
#define	EDGE_MANUF_DESC_LEN		(sizeof (edge_manuf_descriptor_t))

/* Boot params descriptor */
#define	EDGE_BOOT_DESC_ADDR		0x00FF7FC0
#define	EDGE_BOOT_DESC_LEN		(sizeof (edge_boot_descriptor_t))

/*
 * Define the max block size that may be read or written
 * in a read/write RAM/ROM command.
 */
#define	MAX_SIZE_REQ_ION_READ_MEM	((uint16_t)64)
#define	MAX_SIZE_REQ_ION_WRITE_MEM	((uint16_t)64)

/*
 * Notes for the following two ION vendor-specific param descriptors:
 *
 * 1.	These have a standard USB descriptor header so they look like a
 *	normal descriptor.
 * 2.	Any strings in the structures are in USB-defined string
 *	descriptor format, so that they may be separately retrieved,
 *	if necessary, with a minimum of work on the 930. This also
 *	requires them to be in UNICODE format, which, for English at
 *	least, simply means extending each UCHAR into a USHORT.
 * 3.	For all fields, 00 means 'uninitialized'.
 * 4.	All unused areas should be set to 00 for future expansion.
 *
 * This structure is ver 2 format. It contains ALL USB descriptors as
 * well as the configuration parameters that were in the original V1
 * structure. It is NOT modified when new boot code is downloaded; rather,
 * these values are set or modified by manufacturing. It is located at
 * xC00-xFBF (length 3C0h) in the ROM.
 * This structure is a superset of the v1 structure and is arranged so
 * that all of the v1 fields remain at the same address. We are just
 * adding more room to the front of the structure to hold the descriptors.
 *
 * The actual contents of this structure are defined in a 930 assembly
 * file, converted to a binary image, and then written by the serialization
 * program. The C definition of this structure just defines a dummy
 * area for general USB descriptors and the descriptor tables (the root
 * descriptor starts at xC00). At the bottom of the structure are the
 * fields inherited from the v1 structure.
 */
#define	MAX_SERIALNUMBER_LEN	12
#define	MAX_ASSEMBLYNUMBER_LEN	14

typedef struct edge_manuf_descriptor {
	uint16_t	RootDescTable[0x10];	/* C00 Root of descriptor */
						/* tables (placeholder) */
	uint8_t		DescriptorArea[0x2E0];	/* C20 Descriptors go here */
					/* up to 2E0h (just a placeholder) */

	/* Start of v1-compatible section */
	uint8_t		Length;		/* F00 Desc length for what follows */
					/* per USB (=C0h) */
	uint8_t		DescType;  /* F01 Desc type, per USB (=DEVICE type) */
	uint8_t		DescVer;	/* Desc version/format (currently 2) */
	uint8_t		NumRootDescEntries; /* F03 # entries in RootDescTable */

	uint8_t		RomSize;	/* F04 Size of ROM/E2PROM in K */
	uint8_t		RamSize;	/* F05 Size of external RAM in K */
	uint8_t		CpuRev;		/* F06 CPU revision level */
					/* (chg only if s/w visible) */
	uint8_t		BoardRev;	/* F07 PCB revision level */
					/* (chg only if s/w visible) */

	uint8_t		NumPorts;	/* F08 Number of ports */
	uint8_t		DescDate[3];	/* F09 MMDDYY when descr template */
					/* was compiled, so host can track */
					/* changes to USB-only descriptors */

	uint8_t		SerNumLength;	/* F0C USB string descriptor len */
	uint8_t		SerNumDescType;	/* F0D USB descriptor type (=STRING) */
	uint16_t	SerialNumber[MAX_SERIALNUMBER_LEN]; /* F0E Unicode */
					/* Serial Number "01-01-000100" */

	uint8_t		AssemblyNumLength;	/* F26 USB string descr len */
	uint8_t		AssemblyNumDescType;	/* F27 descr type (=STRING) */
	uint16_t	AssemblyNumber[MAX_ASSEMBLYNUMBER_LEN];	/* F28 */
					/* "350-1000-01-A " assembly number */

	uint8_t		OemAssyNumLength;	/* F44 USB string descr len */
	uint8_t		OemAssyNumDescType;	/* F45 descr type (=STRING) */
	uint16_t	OemAssyNumber[MAX_ASSEMBLYNUMBER_LEN];	/* F46 */
				/* "xxxxxxxxxxxxxx" OEM assembly number */

	uint8_t		ManufDateLength;	/* F62 string descr len */
	uint8_t		ManufDateDescType;	/* F63 descr type (=STRING) */
	uint16_t	ManufDate[6];		/* F64 "MMDDYY" */
						/* manufacturing date */

	uint8_t		Reserved3[0x4D];	/* F70 unused, set to 0 */

	uint8_t		UartType;		/* FBD Uart Type */
	uint8_t		IonPid;			/* FBE Product ID, */
					/* == LSB of USB DevDesc.PID */
					/* (Note: Edgeport/4s before 11/98 */
					/* will have 00 here instead of 01) */

	uint8_t		IonConfig;	/* FBF Config byte for ION */
					/* manufacturing use */

	/* FBF end of structure, total len = 3C0h */
} edge_manuf_descriptor_t;

#define	EDGE_MANUF_DESC_FORMAT	"16s736c9c3c2c12s2c14s2c14s2c6s77c3c"

#define	MANUF_DESC_VER_1	1	/* Original definition of MANUF_DESC */
#define	MANUF_DESC_VER_2	2	/* Ver 2, starts at xC00h len 3C0h */

/*
 * Uart Types
 * Note: Since this field was added only recently,
 * all Edgeport/4 units shipped before 11/98 will have 00 in this field.
 * Therefore, both 00 and 01 values mean '654.
 */

#define	MANUF_UART_EXAR_654_EARLY	0	/* Exar 16C654 */
#define	MANUF_UART_EXAR_654		1	/* Exar 16C654 */
#define	MANUF_UART_EXAR_2852		2	/* Exar 16C2852 */

/*
 * Note: The CpuRev and BoardRev values do not conform to manufacturing
 * revisions; they are to be incremented only when the CPU or hardware
 * changes in a software-visible way, such that the 930 software or
 * the host driver needs to handle the hardware differently.
 */
#define	MANUF_CPU_REV_AD4	1    /* 930 AD4 with EP1 Rx bug (needs RXSPM) */
#define	MANUF_CPU_REV_AD5	2    /* 930 AD5 with above bug */
					/* (supposedly) fixed */

#define	MANUF_BOARD_REV_A	1    /* Original version, == Manuf Rev A */
#define	MANUF_BOARD_REV_B	2    /* Manuf Rev B, wakeup interrupt works */
#define	MANUF_BOARD_REV_C	3    /* Manuf Rev C, 2/4 ports, rs232/rs422 */


#define	MANUF_SERNUM_LENGTH	\
		(sizeof (((edge_manuf_descriptor_t *)0)->SerialNumber))
#define	MANUF_ASSYNUM_LENGTH	\
		(sizeof (((edge_manuf_descriptor_t *)0)->AssemblyNumber))
#define	MANUF_OEMASSYNUM_LENGTH	\
		(sizeof (((edge_manuf_descriptor_t *)0)->OemAssyNumber))
#define	MANUF_MANUFDATE_LENGTH	\
		(sizeof (((edge_manuf_descriptor_t *)0)->ManufDate))

#define	MANUF_ION_CONFIG_MASTER		0x80	/* 1=Master mode, 0=Normal */
#define	MANUF_ION_CONFIG_DIAG		0x40	/* 1=Run h/w diags, 0=norm */
#define	MANUF_ION_CONFIG_DIAG_NO_LOOP	0x20	/* As above but no external */
						/* loopback test */

/*
 * This structure describes parameters for the boot code, and
 * is programmed along with new boot code. These are values
 * which are specific to a given build of the boot code. It
 * is exactly 64 bytes long and is fixed at address FF:xFC0
 * - FF:xFFF. Note that the 930-mandated UCONFIG bytes are
 * included in this structure.
 */
typedef struct edge_boot_descriptor {
	uint8_t		Length;		/* C0 Desc length, per USB (= 40h) */
	uint8_t		DescType;	/* C1 Desc type, per USB (= DEVICE) */
	uint8_t		DescVer;	/* C2 Desc version/format */
	uint8_t		Reserved1;	/* C3 -- unused, set to 0 -- */

	uint16_t	BootCodeLength;	/* C4 Boot code goes from FF:0000 */
					/* to FF:(len-1) (LE format) */

	uint8_t		MajorVersion;	/* C6 Firmware version: xx. */
	uint8_t		MinorVersion;	/* C7	yy. */
	uint16_t	BuildNumber;	/* C8	zzzz (LE format) */

	uint16_t	EnumRootDescTable; /* CA Root of ROM-based */
					/* descriptor table */
	uint8_t		NumDescTypes;	/* CC Number of supported */
					/* descriptor types */

	uint8_t		Reserved4;	/* CD Fix Compiler Packing */

	uint16_t	Capabilities;	/* CE-CF Caps flags (LE) */
	uint8_t		Reserved2[0x28]; /* D0 -- unused, set to 0 -- */
	uint8_t		UConfig0;	/* F8 930-defined CPU con- */
	uint8_t		UConfig1;	/* F9 figuration bytes 0 & 1 */
	uint8_t		Reserved3[6];	/* FA -- unused, set to 0 -- */

	/* FF end of structure, total len = 80 */
} edge_boot_descriptor_t;

#define	EDGE_BOOT_DESC_FORMAT	"4cs2c2s2cs40c2c6c"

#define	BOOT_DESC_VER_1		1	/* Original definition of BOOT_PARAMS */
#define	BOOT_DESC_VER_2		2	/* 2nd definition, descriptors */
					/* not included in boot */

/* Capabilities flags */
#define	BOOT_CAP_RESET_CMD	0x0001	/* If set, boot correctly */
					/* supports ION_RESET_DEVICE */


/*
 * TI I2C Format Definitions
 */
#define	TI_I2C_DESC_TYPE_INFO_BASIC	1
#define	TI_I2C_DESC_TYPE_FIRMWARE_BASIC	2
#define	TI_I2C_DESC_TYPE_DEVICE		3
#define	TI_I2C_DESC_TYPE_CONFIG		4
#define	TI_I2C_DESC_TYPE_STRING		5
#define	TI_I2C_DESC_TYPE_FIRMWARE_BLANK 0xf2

#define	TI_I2C_DESC_TYPE_MAX		5
/* 3410 may define types 6, 7 for other firmware downloads */

/* Special section defined by ION */
#define	TI_I2C_DESC_TYPE_ION		0	/* Not defined by TI */

typedef struct edgeti_i2c_desc {
	uint8_t		Type;		/* Type of descriptor */
	uint16_t	Size;		/* Size of data not including header */
	uint8_t		CheckSum;	/* Checksum (8 bit sum of data only) */
					/* Data starts here */
} edgeti_i2c_desc_t;

#define	TI_I2C_DESC_FORMAT		"1c1s1c"
#define	TI_I2C_DESC_LEN			4

typedef struct edgeti_i2c_firmware_rec {
	uint8_t		Ver_Major;	/* Firmware Major version number */
	uint8_t		Ver_Minor;	/* Firmware Minor version number */
					/* Download starts here */
} edgeti_i2c_firmware_rec_t;


/* Structure of header of download image */
typedef struct edgeti_i2c_image_header {
	uint8_t		Length[2];
	uint8_t		CheckSum;
} edgeti_i2c_image_header_t;

typedef struct edgeti_basic_descriptor {
	/*
	 * Power spec
	 *	bit 7:	1/0 - power switching supported/not supported
	 *	bit 0:	1/0 - self powered/bus powered
	 */
	uint8_t		Power;
	uint8_t		HubVid[2];	/* VID HUB */
	uint8_t		HubPid[2];	/* PID HUB */
	uint8_t		DevPid[2];	/* PID Edgeport */
	uint8_t		HubTime;	/* Time for power on to power good */
	uint8_t		HubCurrent;	/* HUB Current = 100ma */
} edgeti_basic_descriptor_t;

#define	TI_GET_CPU_REVISION(x)		(uint8_t)((((x)>>4)&0x0f))
#define	TI_GET_BOARD_REVISION(x)	(uint8_t)(((x)&0x0f))

#define	TI_I2C_SIZE_MASK		0x1f
#define	TI_GET_I2C_SIZE(x)		((((x) & TI_I2C_SIZE_MASK) + 1) * 256)

#define	TI_MAX_I2C_SIZE			(16 * 1024)

/* TI USB 5052 definitions */
typedef struct edgeti_manuf_descriptor {
	uint8_t		IonConfig;	/* Config byte, ION manufacturing use */
	uint8_t 	IonConfig2;	/* Expansion */
	uint8_t 	Version;	/* Version */
	uint8_t 	CpuRev_BoardRev; /* CPU revision level (0xF0) */
					/* and Board Rev Level (0x0F) */
	uint8_t 	NumPorts;	/* Number of ports for this UMP */
	uint8_t 	NumVirtualPorts; /* Number of Virtual ports */
	uint8_t 	HubConfig1;	/* Used to configure the Hub */
	uint8_t 	HubConfig2;	/* Used to configure the Hub */
	uint8_t 	TotalPorts;	/* Total Number of Com Ports for */
					/* the entire device (All UMPs) */
	uint8_t 	Reserved;
} edgeti_manuf_descriptor_t;

#define	TI_MANUF_DESC_FORMAT		"10c"
#define	TI_MANUF_DESC_LEN		10

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_USB_USBSER_EDGE_USBVEND_H */
