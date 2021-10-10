/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_ATA_DISK_H
#define	_ATA_DISK_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/dada/dada.h>


/*
 * ATA disk commands.
 */
#define	ATC_SEEK	0x70    /* seek cmd, bottom 4 bits step rate 	*/
#define	ATC_RDVER	0x40	/* read verify cmd			*/
#define	ATC_RDSEC	0x20    /* read sector cmd			*/
#define	ATC_RDLONG	0x23    /* read long without retry		*/
#define	ATC_WRSEC	0x30    /* write sector cmd			*/
#define	ATC_SETMULT	0xc6	/* set multiple mode			*/
#define	ATC_RDMULT	0xc4	/* read multiple			*/
#define	ATC_WRMULT	0xc5	/* write multiple			*/
#define	ATC_SETPARAM	0x91	/* set parameters command 		*/
#define	ATC_READPARMS	0xec    /* Read Parameters command 		*/
#define	ATC_READDEFECTS	0xa0    /* Read defect list			*/
#define	ATC_ACK_MC	0xdb	/* acknowledge media change		*/

/*
 * Low bits for Read/Write commands...
 */
#define	ATCM_ECCRETRY	0x01    /* Enable ECC and RETRY by controller 	*/
				/* enabled if bit is CLEARED!!! 	*/
#define	ATCM_LONGMODE	0x02    /* Use Long Mode (get/send data & ECC) 	*/

#ifdef  DADKIO_RWCMD_READ
#define	CMPKT   (APKT2CPKT(ata_pktp))
#define	RWCMDP  ((struct dadkio_rwcmd *)CMPKT->cp_bp->b_back)
#endif

/*
 * useful macros
 */
#define	CPKT2GCMD(cpkt)	((gcmd_t *)(cpkt)->cp_ctl_private)
#define	CPKT2APKT(cpkt)  (GCMD2APKT(CPKT2GCMD(cpkt)))

#define	GCMD2CPKT(cmdp)	((struct dcd_pkt  *)((cmdp)->cmd_pktp))
#define	APKT2CPKT(apkt) (GCMD2CPKT(APKT2GCMD(apkt)))

#define	DADR2CHNO(ap)	(((ap)->da_target > 1) ? 1 : 0)

/*
 * public function prototypes
 */
int ata_disk_init(struct ata_controller *ata_ctlp);
int ata_disk_init_reset(struct ata_controller *ata_ctlp);
void ata_disk_destroy(struct ata_controller *ata_ctlp);
int ata_disk_init_drive(struct ata_drive *ata_drvp);
void ata_disk_destroy_drive(struct ata_drive *ata_drvp);
int ata_disk_id(ddi_acc_handle_t handle, uint8_t *ioaddr,  ushort_t *buf);
int ata_disk_bus_ctl(dev_info_t *d, dev_info_t *r, ddi_ctl_enum_t o,
	void *a, void *v, struct ata_drive *ata_drvp);
void make_prd(gcmd_t *gcmdp, ddi_dma_cookie_t *cookie, int single_seg,
	int num_segs);
void write_prd(struct ata_controller *ata_ctlp, struct ata_pkt *ata_pktp);

#ifdef	__cplusplus
}
#endif

#endif /* _ATA_DISK_H */
