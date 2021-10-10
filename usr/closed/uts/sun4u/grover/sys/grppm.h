/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_GRPPM_H
#define	_SYS_GRPPM_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * External function declarations
 */
extern int	ppm_init(struct modlinkage *, size_t, char *);
extern int	ppm_open(dev_t *, int, int, cred_t *);
extern int	ppm_close(dev_t, int, int, cred_t *);
extern int	ppm_ioctl(dev_t, int, intptr_t, int, cred_t *, int *);
extern int	ppm_getinfo(dev_info_t *, ddi_info_cmd_t, void *, void **);

/*
 * Driver registers
 */
struct grppm_reg {
	volatile uint8_t *sus_led;	/* reg controlling LED */
	volatile uint8_t *power_fet;	/* GPO reg controlling power FET */
};

/*
 * Driver register handles
 */
struct grppm_hndl {
	ddi_acc_handle_t	sus_led;	/* handler for LED */
	ddi_acc_handle_t	power_fet;	/* handler for power FET */
};

/*
 * Driver private data
 */
typedef struct grppm_unit {
	dev_info_t		*dip;		/* dev_info pointer */
	kmutex_t		lock;		/* global driver lock */
	uint_t			states;		/* driver states */
	struct grppm_reg	regs;		/* registers */
	struct grppm_hndl	handles;	/* handlers for registers */
	timeout_id_t		grppm_led_tid;	/* timeout id for LED */
} grppm_unit_t;

/*
 * Device states
 */
#define	GRPPM_STATE_SUSPEND	0x1		/* driver is suspended */

/*
 * Acer's Southbridge register bits used by driver.
 */
#define	POWER_FET_CTL		0	/* GPO36 bit in GPO_BLK5 reg */
#define	SUS_LED_CTL		0	/* SPLED[0] of SUS_LED reg */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_GRPPM_H */
