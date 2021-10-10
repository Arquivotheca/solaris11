/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_IXGB_GLD_H
#define	_IXGB_GLD_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

/*
 * Name of the driver
 */
#define	IXGB_DRIVER_NAME		"ixgb"

#define	IXGB_SUCCESS			0
#define	IXGB_NORESOURCES		1
#define	IXGB_NOTSUPPORTED		2
#define	IXGB_BADARG			3
#define	IXGB_NOLINK			4
#define	IXGB_RETRY			5
#define	IXGB_FAILURE			(-1)
/*
 * The driver supports the NDD ioctls ND_GET/ND_SET, and the loopback
 * ioctls LB_GET_INFO_SIZE/LB_GET_INFO/LB_GET_MODE/LB_SET_MODE
 *
 * These are the values to use with LD_SET_MODE.
 * Note: they may not all be supported on any given chip/driver.
 */
#define	IXGB_LOOP_NONE			0
#define	IXGB_LOOP_EXTERNAL_XAUI		1
#define	IXGB_LOOP_EXTERNAL_XGMII	2
#define	IXGB_LOOP_INTERNAL_XAUI		3
#define	IXGB_LOOP_INTERNAL_XGMII	4

/*
 * IXGB-specific ioctls ...
 */
#define	IXGB_IOC			((((((('I' << 4) + 'X') << 4) \
					    + 'G') << 4) + 'B')<< 4)

/*
 * PHY register read/write ioctls, used by cable test software
 */
#define	IXGB_MII_READ		(IXGB_IOC|1)
#define	IXGB_MII_WRITE		(IXGB_IOC|2)

struct ixgb_mii_rw {
	uint32_t	mii_reg;	/* PHY register number [0..31]	*/
	uint32_t	mii_data;	/* data to write/data read	*/
};

/*
 * SEEPROM read/write ioctls, for use by SEEPROM upgrade utility
 *
 * Note: SEEPROMs can only be accessed as 32-bit words, so <see_addr>
 * must be a multiple of 4.  Not all systems have a SEEPROM fitted!
 */
#define	IXGB_SEE_READ		(IXGB_IOC|3)
#define	IXGB_SEE_WRITE		(IXGB_IOC|4)

struct ixgb_see_rw {
	uint32_t	see_addr;	/* Byte offset within SEEPROM	*/
	uint32_t	see_data;	/* Data read/data to write	*/
};


/*
 * These diagnostic IOCTLS are enabled only in DEBUG drivers
 */
#define	IXGB_DIAG		(IXGB_IOC|5)	/* currently a no-op	*/
#define	IXGB_PEEK		(IXGB_IOC|6)
#define	IXGB_POKE		(IXGB_IOC|7)
#define	IXGB_PHY_RESET		(IXGB_IOC|8)
#define	IXGB_SOFT_RESET		(IXGB_IOC|9)
#define	IXGB_HARD_RESET		(IXGB_IOC|10)

typedef struct {
	uint64_t		pp_acc_size;	/* in bytes: 1,2,4,8	*/
	uint64_t		pp_acc_space;	/* See #defines below	*/
	uint64_t		pp_acc_offset;
	uint64_t		pp_acc_data;	/* output for peek	*/
						/* input for poke	*/
} ixgb_peekpoke_t;

#define	IXGB_PP_SPACE_CFG		0	/* PCI config space	*/
#define	IXGB_PP_SPACE_REG		1	/* PCI memory space	*/
#define	IXGB_PP_SPACE_NIC		2	/* on-chip memory	*/
#define	IXGB_PP_SPACE_MII		3	/* PHY's MII registers	*/
#define	IXGB_PP_SPACE_IXGB		4	/* driver's soft state	*/
#define	IXGB_PP_SPACE_TXDESC		5	/* TX descriptors	*/
#define	IXGB_PP_SPACE_TXBUFF		6	/* TX buffers		*/
#define	IXGB_PP_SPACE_RXDESC		7	/* RX descriptors	*/
#define	IXGB_PP_SPACE_RXBUFF		8	/* RX buffers		*/
#define	IXGB_PP_SPACE_STATUS		9	/* status block		*/
#define	IXGB_PP_SPACE_STATISTICS	10	/* statistics block	*/
#define	IXGB_PP_SPACE_SEEPROM		11	/* SEEPROM (if fitted)	*/
#define	IXGB_PP_SPACE_FLASH		12	/* FLASH (if fitted)    */

#ifdef __cplusplus
}
#endif

#endif	/* _IXGB_GLD_H */
