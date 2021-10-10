/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright(c) 2009 Digi International, Inc., Inside Out
 * Networks, Inc.  All rights reserved.
 */


#ifndef _SYS_USB_USBSER_EDGE_FW_H
#define	_SYS_USB_USBSER_EDGE_FW_H


/*
 * Definitions for Edgeport downloadable firmware
 */

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct edge_fw_image_record {
	uint16_t	ExtAddr;
	uint16_t	Addr;
	uint16_t	Len;
} edge_fw_image_record_t;

#define	EDGE_FW_IMAGE_RECORD_FORMAT	"sss"

typedef struct edge_fw_version {
	uint16_t	MajorVersion;
	uint16_t	MinorVersion;
	uint16_t	BuildNumber;
} edge_fw_version_t;

extern uint8_t			edge_fw_down_image[];
extern uint16_t			edge_fw_down_image_size;
extern edge_fw_version_t	edge_fw_down_version;

extern uint8_t			edge_fw_down_g2_image[];
extern uint16_t			edge_fw_down_g2_image_size;
extern edge_fw_version_t	edge_fw_down_g2_version;

#define	REBOOT_EXEC_ADDRESS		0x00FF0000	/* FF:0000 */
#define	OPERATIONAL_EXEC_ADDRESS	0x00014000	/* 01:4000 */

/* TI-based products */
extern uint8_t			edge_fw_down_ti_image[];
extern uint16_t			edge_fw_down_ti_image_size;
extern edge_fw_version_t	edge_fw_down_ti_version;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_USB_USBSER_EDGE_FW_H */
