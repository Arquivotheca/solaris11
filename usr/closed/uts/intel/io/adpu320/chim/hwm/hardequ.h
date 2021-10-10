/*$Header: /vobs/u320chim/src/chim/hwm/hardequ.h   /main/81   Sun Jun  8 20:39:17 2003   spal3094 $*/


/***************************************************************************
*                                                                          *
* Copyright 1995-2003 Adaptec, Inc.  All Rights Reserved.                  *
*                                                                          *
* This software contains the valuable trade secrets of Adaptec or its      *
* licensors.  The software is protected under international copyright      *
* laws and treaties.  This software may only be used in accordance with    *
* terms of its accompanying license agreement.                             *
*                                                                          *
***************************************************************************/

/***************************************************************************
*
*  Module Name:   HARDEQU.H
*
*  Description:   Definitions for hardware device register
*
*  Owners:        ECX IC Firmware Team
*
*  Notes:         Inorder to differentiate between mode dependent and
*                 mode independent registers, in the comment block on
*                 the right of the register definition will have either:
*
*                    M(0,1,2,3)  - mode independent.  There is the same
*                                  physical register when this register
*                                  address accessing in mode 0, 1, 2,
*                                  and 3.
*
*                             ---- OR ----
*
*                    M0,M1,M2,M3 - mode dependent.  There is a different
*                                  physical register when this register
*                                  address accessing in mode 0, 1, 2,
*                                  and 3.
*
***************************************************************************/

#define  SCSI_MULTI_HARDWARE (SCSI_AICU320) > 1

/****************************************************************************

 Equates and definitions to be used exclusively by the Lance Driver modules

****************************************************************************/
#define  SCSI_WIDE_WIDTH      1        /* maximum transfer width = 16 bits    */
#if SCSI_TARGET_OPERATION
#define  SCSI_FAST20_THRESHOLD   0x12  /* fast 20 threshold                   */
#else
#define  SCSI_FAST20_THRESHOLD   0x19  /* Any smaller will be double speed dev*/
#endif  /* SCSI_TARGET_OPERATION */

/****************************************************************************
*  Definitions for message protocol
****************************************************************************/
#define  SCSI_MSG00           0x00     /* - command complete                  */
#define  SCSI_MSG01           0x01     /* - extended message                  */
#define  SCSI_MSGMDP          0x00     /*     - modify data pointer msg       */
#define  SCSI_MSGSYNC         0x01     /*     - synchronous data transfer msg */
#define  SCSI_MSGWIDE         0x03     /*     - wide data transfer msg        */
#define  SCSI_MSGPPR          0x04     /*     - parallel protocol request msg */
#define  SCSI_MSG02           0x02     /* - save data pointers                */
#define  SCSI_MSG03           0x03     /* - restore data pointers             */
#define  SCSI_MSG04           0x04     /* - disconnect                        */
#define  SCSI_MSG05           0x05     /* - initiator detected error          */
#define  SCSI_MSG06           0x06     /* - abort                             */
#define  SCSI_MSG07           0x07     /* - message reject                    */
#define  SCSI_MSG08           0x08     /* - nop                               */
#define  SCSI_MSG09           0x09     /* - message parity error              */
#define  SCSI_MSG0A           0x0a     /* - linked command complete           */
#define  SCSI_MSG0B           0x0b     /* - linked command complete           */
#define  SCSI_MSG0C           0x0c     /* - bus device reset                  */
#define  SCSI_MSG0D           0x0d     /* - abort tag                         */
#define  SCSI_MSG0E           0x0e     /* - clear task set                    */
#define  SCSI_MSG11           0x11     /* - terminate task                    */ 
#define  SCSI_MSGCLRACA       0x16     /* - clear ACA                         */                                                 
#define  SCSI_MSGLUR          0x17     /* - logical unit reset                */ 
#define  SCSI_MSGTAG          0x20     /* - tag queuing                       */
#define  SCSI_MSGIWR          0x23     /* - ignore wide residue               */
#define  SCSI_MSGID           0x80     /* identify message, no disconnection  */
#define  SCSI_MSGID_D         0xc0     /* identify message, disconnection     */
#define  SCSI_MSGHEADOFQ      0x21     /* head of queue                       */             
#define  SCSI_MSGORDERED      0x22     /* ordered message                     */
/****************************************************************************

 ARP (Adaptec Risc Processor) REGISTERS - ARP Mode Selection Pointer
      Address Range: 0x00

****************************************************************************/

#define  SCSI_MODE_PTR        0x00     /* All modes */
                                       /* mode pointer                (rd/wr) */
#define     SCSI_DST_MODE5    0x50     /* destination mode 5                  */
#define     SCSI_DST_MODE4    0x40     /* destination mode 4                  */
#define     SCSI_DST_MODE3    0x30     /* destination mode 3                  */
#define     SCSI_DST_MODE2    0x20     /* destination mode 2                  */
#define     SCSI_DST_MODE1    0x10     /* destination mode 1                  */
#define     SCSI_DST_MODE0    0x00     /* destination mode 0                  */
#define     SCSI_SRC_MODE5    0x05     /* source mode 5                       */
#define     SCSI_SRC_MODE4    0x04     /* source mode 4                       */
#define     SCSI_SRC_MODE3    0x03     /* source mode 3                       */
#define     SCSI_SRC_MODE2    0x02     /* source mode 2                       */
#define     SCSI_SRC_MODE1    0x01     /* source mode 1                       */
#define     SCSI_SRC_MODE0    0x00     /* source mode 0                       */
#define     SCSI_MODE5        (SCSI_DST_MODE5+SCSI_SRC_MODE5)  /* Dst & Src   */
                                                               /* are Mode 5  */
#define     SCSI_MODE4        (SCSI_DST_MODE4+SCSI_SRC_MODE4)  /* Dst & Src   */
                                                               /* are Mode 4  */
#define     SCSI_MODE3        (SCSI_DST_MODE3+SCSI_SRC_MODE3)  /* Dst & Src   */
                                                               /* are Mode 3  */
#define     SCSI_MODE2        (SCSI_DST_MODE2+SCSI_SRC_MODE2)  /* Dst & Src   */
                                                               /* are Mode 2  */
#define     SCSI_MODE1        (SCSI_DST_MODE1+SCSI_SRC_MODE1)  /* Dst & Src   */
                                                               /* are Mode 1  */
#define     SCSI_MODE0        (SCSI_DST_MODE0+SCSI_SRC_MODE0)  /* Dst & Src   */
                                                               /* are Mode 0  */

/****************************************************************************

 HOST REGISTERS - CHIM interrupt and control
      Address Range: 0x01 - 0x05

****************************************************************************/

#if !SCSI_DCH_U320_MODE
#define  SCSI_HSTINTSTAT      0x01     /* M(0,1,2,3,4) */
                                       /* HST interrupt status        (rd/wr) */
#define     SCSI_HWERRINT     0x80     /* catastrophic hardware failure       */
#define     SCSI_BRKADRINT    0x40     /* program count = breakpoint          */
#define     SCSI_SWTMINT      0x20     /* timer interrupt (to host)           */
#define     SCSI_PCIINT       0x10     /* detect pci error                    */
#define     SCSI_SCSIINT      0x08     /* scsi event interrupt                */
#define     SCSI_ARPINT       0x04     /* ARP paused itself                   */
#define     SCSI_CMDCMPLT     0x02     /* scb command complete w/o error      */
#define     SCSI_SPLTINT      0x01     /* split completion interrupt for PCI-X*/

#else

#define  SCSI_HSTINTSTAT      0x01     /* All modes */
                                       /* HST interrupt status        (rd/wr) */
#define     SCSI_HWERRINT     0x80     /* catastrophic hardware failure       */
#define     SCSI_BRKADRINT    0x40     /* program count = breakpoint          */
#define     SCSI_SWERRINT     0x20     /* Software error interrupt            */
#define     SCSI_HDMAINT      0x10     /* Host DMA interrupt Error            */
#define     SCSI_SCSIINT      0x08     /* scsi event interrupt                */
#define     SCSI_ARPINT       0x04     /* ARP paused itself                   */
#define     SCSI_CMDCMPLT     0x02     /* scb command complete w/o error      */
#define     SCSI_TIMERINT     0x01     /* Timer interrupt                     */

/* For the DCH SCSI core use the HSTINT register to clear interrupt statuses  */ 
#define  SCSI_CLRHSTINT       0x01     /* All modes */
                                       /* HST interrupt status        (rd/wr) */
#define     SCSI_CLRHWERRINT  0x80     /* clear catastrophic hardware failure */
#define     SCSI_CLRBRKADRINT 0x40     /* clear program count = breakpoint    */
#define     SCSI_CLRSWERRINT  0x20     /* clear Software error interrupt      */
#define     SCSI_CLRHDMAINT   0x10     /* clear Host DMA interrupt Error      */
#define     SCSI_CLRSCSIINT   0x08     /* clear scsi event interrupt          */
#define     SCSI_CLRARPINT    0x04     /* clear ARP paused itself             */
#define     SCSI_CLRCMDINT    0x02     /* clear scb command complete w/o err  */
#define     SCSI_CLRTIMERINT  0x01     /* clear Timer interrupt               */

#endif /* !SCSI_DCH_U320_MODE */
#define  SCSI_ARPINTCODE      0x02     /* All modes */
                                       /* ARP interrupt code          (rd/wr) */
#define     SCSI_ARPINTCODE7  0x80     /* ARP interrupt code                  */
#define     SCSI_ARPINTCODE6  0x40     /*                                     */
#define     SCSI_ARPINTCODE5  0x20     /*                                     */
#define     SCSI_ARPINTCODE4  0x10     /*                                     */
#define     SCSI_ARPINTCODE3  0x08     /*                                     */
#define     SCSI_ARPINTCODE2  0x04     /*                                     */
#define     SCSI_ARPINTCODE1  0x02     /*                                     */
#define     SCSI_ARPINTCODE0  0x01     /*                                     */

#if !SCSI_DCH_U320_MODE
#define  SCSI_CLRHSTINT       0x03     /* M(0,1,2,3,4) */
                                       /* clear HST interrupt status     (wr) */
#define     SCSI_CLRBRKADRINT 0x40     /* clear breakpoint intr (brkadrint)   */
#define     SCSI_CLRSWTMINT   0x20     /* clear timer interrupt (swtmint)     */
#define     SCSI_CLRSCSIINT   0x08     /* clear scsi interrupt (scsiint)      */
#define     SCSI_CLRARPINT    0x04     /* clear ARP interrupt (arpint)        */
#define     SCSI_CLRCMDINT    0x02     /* clear command complete interrupt    */

#define     SCSI_CLRSPLTINT   0x01     /* clear split interrupt               */

#else 

/* DCH SCSI register for enabling 'HSTINT' interrupts */
#define  SCSI_HSTINTSTATEN    0x03     /* All modes */

#define     SCSI_ENHWERRINT   0x80     /* enable Hardware interrupt           */
#define     SCSI_ENBRKADRINT  0x40     /* enable Break address interrupt      */
#define     SCSI_ENSWERRINT   0x20     /* enable Software error interrupt     */
#define     SCSI_ENDMWINT     0x10     /* enable DMA error interrupt          */
#define     SCSI_ENSCSIINT    0x08     /* enable SCSI Interrupt               */
#define     SCSI_ENARPINT     0x04     /* enable ARP interrupt                */
#define     SCSI_ENCMDCMPLT   0x02     /* enable Command complete interrupt   */
#define     SCSI_ENTIMERINT   0x01     /* enable Timer interrupt              */
                                       /* Mask to enable all interrupts       */
#define     SCSI_HSTINTSTATMASK  (SCSI_ENHWERRINT+SCSI_ENBRKADRINT+\
                                  SCSI_ENSWERRINT+SCSI_ENDMWINT+\
                                  SCSI_ENSCSIINT+SCSI_ENARPINT+\
                                  SCSI_ENCMDCMPLT+SCSI_ENTIMERINT)
#define     SCSI_HSTINTSTATMASKOFF  0x00  
#endif /* !SCSI_DCH_U320_MODE */

#define  SCSI_ERROR           0x04     /* M(0,1,2,3,4) */
                                       /* hard error status              (rd) */
#define     SCSI_CIOPARERR    0x80     /* status = ciobus parity error        */
#define     SCSI_MPARERR      0x20     /* status = memory parity error        */
#define     SCSI_DPARERR      0x10     /* status = data parity error          */
#define     SCSI_SQPARERR     0x08     /* status = sequencer ram parity error */
#define     SCSI_ILLOPCODE    0x04     /* status = illegal opcode error       */

#if !SCSI_DCH_U320_MODE
#define     SCSI_DSCTMOUT     0x02     /* status = discard timeout in PCI/-X  */
#endif /* !SCSI_DCH_U320_MODE */

#define  SCSI_CLRERR          0x04     /* All modes */
                                       /* clear hard error               (wr) */
#define     SCSI_CLRCIOPARERR 0x80     /* clear ciobus parity error           */
#define     SCSI_CLRMPARERR   0x20     /* clear memory parity error           */
#define     SCSI_CLRDPARERR   0x10     /* clear data parity error             */
#define     SCSI_CLRSQPARERR  0x08     /* clear sequencer ram parity error    */
#define     SCSI_CLRILLOPCODE 0x04     /* clear illegal opcode error          */

#if !SCSI_DCH_U320_MODE
#define     SCSI_CLRDSCTMOUT  0x02     /* clear discard timeout in PCI/-X     */

#define  SCSI_HCNTRL          0x05     /* M(0,1,2,3,4) */
                                       /* host control                (rd/wr) */
#define     SCSI_POWRDN       0x40     /* power down                          */
#define     SCSI_SWINT        0x10     /* software interrupt                  */
#define     SCSI_PAUSE        0x04     /* pause ARP                      (wr) */
#define     SCSI_PAUSEACK     0x04     /* ARP is paused                  (rd) */
#define     SCSI_INTEN        0x02     /* enable hardware interrupt           */
#define     SCSI_CHIPRST      0x01     /* chip reset                     (wr) */
#define     SCSI_CHIPRSTACK   0x01     /* chip reset acknowledge         (rd) */

#else
/* DCH SCSI supports 'AutoPause' */
#define  SCSI_HCNTRL          0x05     /* All modes */
                                       /* host control                (rd/wr) */
#define     SCSI_AUTOPAUSE    0x80     /* When set the Host may access any    */
                                       /* AIC register with pausing.          */
#define     SCSI_POWRDN       0x40     /* power down                          */
#define     SCSI_REMMODE      0x20     /* Remember mode used in conjunction   */
                                       /* memmapped. ARP automatcally pauses  */
                                       /* and saves mode and restores mode if */
                                       /* during reg write or read.           */
#define     SCSI_MEMMAP       0x10     /* Memmapped mode. See DCH_SCSI doc for*/
                                       /* details                             */
#define     SCSI_PAUSEREQ     0x08     /* The ARP was requested to pause (RD) */
#define     SCSI_PAUSEACK     0x04     /* ARP paused                     (rd) */
#define     SCSI_PAUSE        0x04     /* Request to pause               (wr) */
#define     SCSI_STPWLEVEL    0x02     /* SCSI termination power level        */
#define     SCSI_DCHRST       0x01     /* DCH SCSI chip reset            (wr) */

#define     SCSI_HCNTRL_MASK (SCSI_AUTOPAUSE+SCSI_POWRDN+SCSI_REMMODE+ \
                              SCSI_MEMMAP+SCSI_STPWLEVEL+SCSI_DCHRST)

#endif /* !SCSI_DCH_U320_MODE */
/****************************************************************************

 HOST REGISTERS - CHIM SCB delivery
      Address Range: 0x06 - 0x0B

****************************************************************************/

#define  SCSI_HNSCB_QOFF      0x06     /* All modes */
                                       /* host new scb queue offset   (rd/wr) */
                                       /*   should access as 16-bit register  */

#define  SCSI_HESCB_QOFF      0x08     /* All modes */
                                       /* host empty scb queue offset (rd/wr) */

#if SCSI_DCH_U320_MODE

#define  SCSI_HDMAERR         0x09     /* Host DMA error status               */

#define     SCSI_OVLYERROR    0x10     /* Code Overlay DMA error              */
#define     SCSI_CSERROR      0x08     /* Command/Status FIFO 0 DMA error     */
#define     SCSI_SGERROR      0x04     /* Scatter/gather DMA error            */
#define     SCSI_DFF1ERROR    0x02     /* DFF1 DMA error                      */
#define     SCSI_DFF0ERROR    0x01     /* DFF0 DMA error                      */

#define  SCSI_HDMAERREN       0x0A     /* Host DMA error enable control       */

#define     SCSI_ENOVLYERROR  0x10     /* enable Code Overlay DMA error       */
#define     SCSI_ENCSERROR    0x08     /* enable Cmd/Sts FIFO 0 DMA error     */
#define     SCSI_ENSGERROR    0x04     /* enable Scatter/gather DMA error     */
#define     SCSI_ENDFF1ERROR  0x02     /* enable DFF1 DMA error               */
#define     SCSI_ENDFF0ERROR  0x01     /* enable DFF0 DMA error               */

#endif /* SCSI_DCH_U320_MODE */


#define  SCSI_HS_MAILBOX      0x0B     /* All modes */
                                       /* host mailbox                (rd/wr) */

/****************************************************************************

 HOST REGISTERS - ARP interrupt processing
      Address Range: 0x0C - 0x0D

****************************************************************************/

#if !SCSI_DCH_U320_MODE
#define  SCSI_ARPINTSTAT      0x0C     /* M(0,1,2,3,4) */
                                       /* ARP interrupt status           (rd) */
#define     SCSI_SWTMRTO      0x10     /* software timer timeout              */
#define     SCSI_ARP_ARPINT   0x08     /* ARP _ARP interrupt                  */
#define     SCSI_ARP_SCSIINT  0x04     /* ARP _SCSI interrupt                 */
#define     SCSI_ARP_PCIINT   0x02     /* ARP _PCI interrupt                  */
#define     SCSI_ARP_SPLTINT  0x01     /* ARP _Split interrupt                */

#define  SCSI_CLRARPINTSTAT      0x0C  /* M(0,1,2,3,4) */
                                       /* clear ARP interrupt            (wr) */
#define     SCSI_CLRARP_SWTMRTO  0x10  /* clear ARP _Software timer timeout   */
#define     SCSI_CLRARP_ARPINT   0x08  /* clear ARP _ARP interrupt            */
#define     SCSI_CLRARP_SCSIINT  0x04  /* clear ARP _SCSI interrupt           */
#define     SCSI_CLRARP_PCIINT   0x02  /* clear ARP _PCI interrupt            */
#define     SCSI_CLRARP_SPLTINT  0x01  /* clear ARP _Split interrupt          */
/****************************************************************************

 HOST REGISTERS - CHIM/ARP software timer
      Address Range: 0x0E - 0x0F

****************************************************************************/

#define  SCSI_SWTIMER0        0x0E     /* M(0,1,2,3,4) */
                                       /* software timer 0            (rd/wr) */
#define  SCSI_SWTIMER1        0x0F     /* M(0,1,2,3,4) */
                                       /* software timer 1            (rd/wr) */
#else

#define  SCSI_ARPINTSTAT      0x0C     /* All modes */

#define     SCSI_ARP_ARPINT   0x08     /* ARP _ARP interrupt                  */
#define     SCSI_ARP_SCSIINT  0x04     /* ARP _SCSI interrupt                 */
#define     SCSI_ARP_HDMAINT  0x02     /* ARP_DMA error interrupt             */
#define     SCSI_ARP_TIMERINT 0x01     /* ARP_Timer interrupt                 */

#define  SCSI_CLRARPINTSTAT      0x0C  /* All modes */
                                       /* clear ARP interrupt            (wr) */
#define     SCSI_CLRARP_ARPINT   0x08  /* clear ARP _ARP interrupt            */
#define     SCSI_CLRARP_SCSIINT  0x04  /* clear ARP _SCSI interrupt           */
#define     SCSI_CLRARP_HDMAINT  0x02  /* clear ARP_DMA error interrupt       */
#define     SCSI_CLRARP_TIMERINT 0x01  /* clear ARP_Timer interrupt           */

#define  SCSI_SFUNC           0x0D     /* Special Function register           */    
#define     SCSI_GROUP        0xF0     /* Test Group Select 0-F see DCH spec  */   
#define     SCSI_TEST         0x0F     /* Test Signal Select 0-F see DCH spec */   

/****************************************************************************

 HOST RESET REGISTER - Each block may be reset individually by writing a 1
                       to a chosen block or the entire dch_core may be reset
                       by writing 1 the hcntrl (05) register bit 0 (dchreset).
                       If the dchreset field is set (1) this indicates a reset
                       block(s) or power-on condition has happened. To clear 
                       this status write a 0 to the hcntrl (reg 05) dchreset
                       field (bit 0).
                       
****************************************************************************/

#define  SCSI_RESETBLOCK      0x0E     /* Reset Block register                */
#define     SCSI_HST_RST      0x80     /* Reset host block                    */
#define     SCSI_DFF0_RST     0x40     /* Reset data fifo 0 block             */
#define     SCSI_DFF1_RST     0x20     /* Reset data fifo 1 block             */
#define     SCSI_CMC_RST      0x10     /* Reset CMC block                     */
#define     SCSI_SCB_RST      0x08     /* Reset SCB block                     */
#define     SCSI_SEQ_RST      0x04     /* Reset Seqencer block                */
#define     SCSI_SCS_RST      0x02     /* Reset SCSI logic domain             */
#define     SCSI_SCSIIO_RST   0x01     /* Reset SCSI IO logic domain          */


/****************************************************************************

 HOST REGISTERS - ARP SCB delivery
      Address Range: 0x10 - 0x16

****************************************************************************/

#define  SCSI_M0DMAERR0       0x10     /* M0 DMA errors (10h-13h)          */

#define  SCSI_M1DMAERR0       0x10     /* M1 DMA errors (10h-13h)          */

#define  SCSI_M0DMAERR0EN     0x10     /* M3 enable DMA errors (10h-13h)   */

#define  SCSI_M1DMAERR0EN     0x10     /* M4 enable DMA errors (10h-13h)   */

#define  SCSI_M0DMAERR1       0x14     /* M0 - DMA errors                  */
#define     SCSI_M0DMASEL     0xC0     /* Data pipeline seg select         */
#define     SCSI_M0DMARRLS    0x02     /* Data DMA rewind release err      */
#define     SCSI_M0DMARCNT    0x01     /* Data DMA rewind release cnt      */

#define  SCSI_M1DMAERR1       0x14     /* M1 - DMA errors                  */
#define     SCSI_M1DMASEL     0xC0     /* Data pipeline seg select         */
#define     SCSI_M1DMARRLS    0x02     /* Data DMA rewind release err      */
#define     SCSI_M1DMARCNT    0x01     /* Data DMA rewind release cnt      */

#define  SCSI_M0DMAERR1EN     0x14     /* M3 enable DMA errors             */
#define     SCSI_M0DMARRSEN   0x02     /* M3 enable rewind release errors  */
#define     SCSI_M0DMARCNTEN  0x01     /* M3 enable rewind release count   */

#define  SCSI_M1DMAERR1EN     0x14     /* M4 enable DMA errors             */
#define     SCSI_M1DMARRSEN   0x02     /* M4 enable rewind release errors  */
#define     SCSI_M1DMARCNTEN  0x01     /* M4 enable rewind release count   */

#define  SCSI_M0DMACTL        0x15     /* M0 - Data DMA Control            */     
#define     SCSI_M0STHRESH    0xF0     /* Release Threshold                */     
#define     SCSI_M0THRESHEN   0x04     /* Threshold enable                 */     
#define     SCSI_M0REWIND     0x02     /* Rewind enable                    */     
#define     SCSI_M0INTKEN     0x01     /* Interlock enable                 */     

#define  SCSI_M1DMACTL        0x15     /* M1- Data DMA Control            */     
#define     SCSI_M1STHRESH    0xF0     /* Release Threshold                */     
#define     SCSI_M1THRESHEN   0x04     /* Threshold enable                 */     
#define     SCSI_M1REWIND     0x02     /* Rewind enable                    */     
#define     SCSI_M1INTKEN     0x01     /* Interlock enable                 */     

#endif /* !SCSI_DCH_U320_MODE */
#define  SCSI_SNSCB_QOFF      0x10     /* M2 */
                                       /* ARP new scb queue offset    (rd/wr) */

#define  SCSI_SESCB_QOFF      0x12     /* M2 */
                                       /* ARP empty scb queue offset  (rd/wr) */

#define  SCSI_SDSCB_QOFF      0x14     /* M2 */
                                       /* ARP done scb queue offset   (rd/wr) */

#define  SCSI_QOFF_CTLSTA        0x16  /* M2 */
                                       /* queue offset control& status(rd/wr) */
#define     SCSI_EMPTY_SCB_AVAIL 0x80  /* empty scb available            (rd) */
#define     SCSI_NEW_SCB_AVAIL   0x40  /* new scb available              (rd) */
#if SCSI_DCH_U320_MODE
#define     SCSI_SDCB_ROLLOVER   0x20  /* Sequencer done SCB Queue rollover   */
#endif /* !SCSI_DCH_U320_MODE */
#define     SCSI_HS_MAILBOX_ACT  0x10  /* host mailbox active                 */
#define     SCSI_SCB_QSIZE3      0x08  /* scb queue size                      */
#define     SCSI_SCB_QSIZE2      0x04  /* These bits set the scsi_snscb_qoff  */
#define     SCSI_SCB_QSIZE1      0x02  /* size and the scsi_sdscb_qoff size.  */
#define     SCSI_SCB_QSIZE0      0x01  /*                                     */
#define     SCSI_SCB_QSIZE       (SCSI_SCB_QSIZE3+SCSI_SCB_QSIZE2+ \
                                  SCSI_SCB_QSIZE1+SCSI_SCB_QSIZE0)
#if !SCSI_DCH_U320_MODE

#define  SCSI_INTCTL             0x18  /* M(0,1,2,3,4) */
                                       /* interrupt control           (rd/wr) */
#define     SCSI_SWTMINTMASK     0x80  /* timer interrupt mask                */
                                       /*     0 - INT/MSI is generated        */
                                       /*     1 - no INT/MSI is generated     */
                                       /*         only ARPINSTAT/HSTINSTAT    */
                                       /*         is updated.                 */
#define     SCSI_SWTMINTEN       0x40  /* timer interrupt direction           */
                                       /*   0 - expired timer directed to HOST*/
                                       /*   1 - expired timer directed to ARP */
#define     SCSI_SWTIMER_START   0x20  /* start swtimer counter               */
#define     SCSI_AUTOCLRCMDINT   0x10  /* auto clear command complete intr.   */
#define     SCSI_PCIINTEN        0x08  /* pci interrupt enable                */
#define     SCSI_SCSIINTEN       0x04  /* scsi interrupt enable               */
#define     SCSI_ARPINTEN        0x02  /* ARP interrupt enable                */
#define     SCSI_SPLTINTEN       0x01  /* split completion error intr. enable */

/****************************************************************************

 HOST REGISTERS - ARP DMA control
      Address Range: 0x19 - 0x1F

****************************************************************************/

#define  SCSI_DFCNTRL        0x19      /* M0,M1 */
                                       /* data fifo control           (rd/wr) */
#define     SCSI_PRELOADEN   0x80      /* preload enable                      */
#define     SCSI_SCSIEN      0x20      /* enable xfer: scsi <-> sfifo    (wr) */
#define     SCSI_SCSIENACK   0x20      /* SCSIEN clear acknowledge       (rd) */
#define     SCSI_HDMAEN      0x08      /* enable xfer: dfifo <-> host    (wr) */
#define     SCSI_HDMAENACK   0x08      /* HDMAEN clear acknowledge       (rd) */
#define     SCSI_DIRECTION   0x04      /* transfer direction = write          */
#define     SCSI_FIFOFLUSH   0x02      /* flush data fifo to host             */

#define  SCSI_DSCOMMAND0     0x19      /* M4 */
                                       /* device space command 0      (rd/wr) */
#define     SCSI_CACHETHEN   0x80      /* cache threshold enable              */
#define     SCSI_DPARCKEN    0x40      /* internal data parity check enable   */
#define     SCSI_MPARCKEN    0x20      /* memory parity check enable          */
#define     SCSI_EXTREQLCK   0x10      /* external request clock              */
#define     SCSI_CIOPARCKEN  0x01      /* ciobus parity check enable          */

#define  SCSI_CACHETHEN_DEFAULT 1      /* default cache threshold enable      */

#define  SCSI_DFSTATUS       0x1A      /* M0,M1 */
                                       /* data fifo status               (rd) */
#define     SCSI_PRELOAD_AVAIL   0x80  /* preload level is available          */
#define     SCSI_PKT_PRELOAD_AVAIL  0x40  /* packetized preload level is      */
                                          /* available, data channel is       */
                                          /* enabled, the last SG list element*/
                                          /* has not been preloaded, signed   */
                                          /* stcnt <= remaining IU byte count */
#define     SCSI_MREQPEND    0x10      /* master request pending              */
#define     SCSI_HDONE       0x08      /* host transfer done: hcnt=0 & bus handshake done */
#define     SCSI_DFTHRSH     0x04      /* threshold reached                   */
#define     SCSI_FIFOFULL    0x02      /* data fifo full                      */
#define     SCSI_FIFOEMP     0x01      /* data fifo empty                     */

#define  SCSI_SG_CACHEPTR        0x1B  /* M0,M1 */
                                       /* sg cache pointer            (rd/wr) */
#define     SCSI_SG_CACHEPTR1    0x02  /* identify last segment               */
#define     SCSI_SG_CACHEPTR0    0x01  /* last segment done                   */

#define     SCSI_LAST_SEG        SCSI_SG_CACHEPTR1
#define     SCSI_LAST_SEG_DONE   SCSI_SG_CACHEPTR0

#define  SCSI_ARBCTL         0x1B      /* M4 */
                                       /* arbiter control             (rd/wr) */

#else

/* DCH SCSI core */

#define  SCSI_INTCTL          0x18     /* M4                                  */
                                       /* interrupt control           (rd/wr) */
#define     SCSI_HTIMEREN        0x20  /* Host Timer interrupt enable         */
#define     SCSI_STIMEREN        0x10  /* Sequencer Timer interrupt enable    */
#define     SCSI_HDMAINTEN       0x08  /* Host DMA interrupt enable           */
#define     SCSI_SCSIINTEN       0x04  /* SCSI error interrupt enable         */
#define     SCSI_ARPINTEN        0x02  /* ARP interrupt enable                */
                                 
#define  SCSI_DFCNTRL         0x18     /* M0,M1 Data Fifo control             */

#define     SCSI_PRELOADEN       0x80  /* preload enable                      */
#define     SCSI_SETSCSIEN       0x40  /* Enable DFF to SCSI DMAs        (wr) */  
#define     SCSI_SCSIEN          0x40  /* SCSI DMA enable status         (rd) */
#define     SCSI_RSTSCSIEN       0x20  /* Enable SCSI DMA to reset       (wr) */
#define     SCSI_SCSIENACK       0x20  /* SCSIEN clear acknowledge       (rd) */
#define     SCSI_SETHDMAEN       0x10  /* enable xfer: dfifo <-> host    (wr) */
#define     SCSI_HDMAEN          0x10  /* Status for HDMA enable         (rd) */
#define     SCSI_RSTHDMAEN       0x08  /* Request for DMA disable        (wr) */
#define     SCSI_HDMAENACK       0x08  /* HDMAEN clear acknowledge       (rd) */
#define     SCSI_SETFIFOFLUSH    0x02  /* Flush data fifo to host        (wr) */
#define     SCSI_FIFOFLUSH       0x02  /* Status of the fifoflush        (rd) */
#define     SCSI_RSTFLUSH        0x01  /* Request to disable fifoflush   (wr) */
#define     SCSI_FIFOFLUSHACK    0x01  /* Status of the fifoflush        (rd) */

#define  SCSI_DFSTATUS        0x19     /* M0,M1 */
                                       /* data fifo status               (rd) */
#define     SCSI_PRELOAD_AVAIL   0x80  /* preload level is available          */
#define     SCSI_SETDIRECTIONWR  0x40  /* transfer direction = write     (wr) */
#define     SCSI_DIRECTION       0x40  /* direction state                (rd) */
#define     SCSI_SETDIRECTIONRD  0x20  /* transfer direction = read      (wr) */
#define     SCSI_MREQPEND        0x10  /* memory request pending              */
#define     SCSI_HDONE           0x08  /* hst xfrdone: hcnt=0 & bus handshake done */
#define     SCSI_DFTHRSH         0x04  /* threshold reached                   */
#define     SCSI_FIFOFULL        0x02  /* data fifo full                      */
#define     SCSI_FIFOEMP         0x01  /* data fifo empty                     */

#define  SCSI_DSCOMMAND0      0x19     /* M4 */
                                       /* device space command 0      (rd/wr) */
#define     SCSI_DPARCKEN        0x40  /* internal data parity check enable   */
#define     SCSI_MPARCKEN        0x20  /* memory parity check enable          */
#define     SCSI_CIOPARCKEN      0x01  /* ciobus parity check enable          */

#define  SCSI_SG_CACHEPTR     0x1A     /* M0,M1 */
                                          /* sg cache pointer (16 bits)       */
#define     SCSI_SG_CACHEPTR15   0x8000   
#define     SCSI_SG_CACHEPTR14   0x4000   
#define     SCSI_SG_CACHEPTR13   0x2000   
#define     SCSI_SG_CACHEPTR12   0x1000   
#define     SCSI_SG_CACHEPTR11   0x0800   
#define     SCSI_SG_CACHEPTR10   0x0400   
#define     SCSI_SG_CACHEPTR9    0x0200   
#define     SCSI_SG_CACHEPTR8    0x0100   
#define     SCSI_LAST_ELEMENT    0x0080   /* last element with a sub-list     */ 
#define     SCSI_LAST_LIST       0x0040   /* last list (last segment)         */
#define     SCSI_ASEL            0x0030   /* memory array location            */
#define     SCSI_SEGVALID        0x0008   /* rd only .. pipeline seg valid    */
#define     SCSI_FLAG2           0x0004   /* no assigned usage                */
#define     SCSI_FLAG1           0x0002   /* no assigned usage                */
#define     SCSI_LAST_SEG_DONE   0x0001   /* last segment done                */

#define  SCSI_HSTBIST         0x1A        /* M4 */
                                          /* Host 'built in self test'        */
#define     SCSI_HSTBISTFAIL     0x04     /* host BIST test failed.           */
#define     SCSI_HSTBISTDONE     0x02     /* host BIST test done.             */
#define     SCSI_HSTBISTEN       0x01     /* host BIST test enable.           */

#define  SCSI_HOSTACCESS      0x1C        /* All Modes - HostAccess register  */

#endif /* !SCSI_DCH_U320_MODE */
/****************************************************************************

 SCSI REGISTERS - SCSI's control and status registers
      Address Range: 0x20 - 0x6F

****************************************************************************/
                                       /* M(0,1,3) */
#define  SCSI_LQIN00          0x20     /* LQ packet in 0              (rd/wr) */
#define  SCSI_LQIN01          0x21     /* LQ packet in 1              (rd/wr) */
#define  SCSI_LQIN02          0x22     /* LQ packet in 2              (rd/wr) */
#define  SCSI_LQIN03          0x23     /* LQ packet in 3              (rd/wr) */
#define  SCSI_LQIN04          0x24     /* LQ packet in 4              (rd/wr) */
#define  SCSI_LQIN05          0x25     /* LQ packet in 5              (rd/wr) */
#define  SCSI_LQIN06          0x26     /* LQ packet in 6              (rd/wr) */
#define  SCSI_LQIN07          0x27     /* LQ packet in 7              (rd/wr) */
#define  SCSI_LQIN08          0x28     /* LQ packet in 8              (rd/wr) */
#define  SCSI_LQIN09          0x29     /* LQ packet in 9              (rd/wr) */
#define  SCSI_LQIN10          0x2A     /* LQ packet in 10             (rd/wr) */
#define  SCSI_LQIN11          0x2B     /* LQ packet in 11             (rd/wr) */
#define  SCSI_LQIN12          0x2C     /* LQ packet in 12             (rd/wr) */
#define  SCSI_LQIN13          0x2D     /* LQ packet in 13             (rd/wr) */
#define  SCSI_LQIN14          0x2E     /* LQ packet in 14             (rd/wr) */
#define  SCSI_LQIN15          0x2F     /* LQ packet in 15             (rd/wr) */
#define  SCSI_LQIN16          0x30     /* LQ packet in 16             (rd/wr) */
#define  SCSI_LQIN17          0x31     /* LQ packet in 17             (rd/wr) */
#define  SCSI_LQIN18          0x32     /* LQ packet in 18             (rd/wr) */
#define  SCSI_LQIN19          0x33     /* LQ packet in 19             (rd/wr) */

#define  SCSI_TYPEPTR         0x20     /* M4 */
                                       /* scb type pointer            (rd/wr) */

#define  SCSI_TAGPTR          0x21     /* M4 */
                                       /* queue tag pointer           (rd/wr) */

#define  SCSI_LUNPTR          0x22     /* M4 */
                                       /* LUN number pointer          (rd/wr) */

#define  SCSI_DATALENPTR      0x23     /* M4 */
                                       /* data length pointer         (rd/wr) */

#define  SCSI_STATLENPTR      0x24     /* M4 */
                                       /* status length pointer       (rd/wr) */

#define  SCSI_CDBLENPTR       0x25     /* M4 */
                                       /* command length pointer      (rd/wr) */

#define  SCSI_ATTRPTR         0x26     /* M4 */
                                       /* task attribute pointer      (rd/wr) */

#define  SCSI_FLAGPTR         0x27     /* M4 */
                                       /* task management flags ptr.  (rd/wr) */

#define  SCSI_CDBPTR          0x28     /* M4 */
                                       /* command pointer             (rd/wr) */

#define  SCSI_QNEXTPTR        0x29     /* M4 */
                                       /* queue next pointer          (rd/wr) */

#define  SCSI_IDPTR           0x2A     /* M4 */
                                       /* scsi id pointer             (rd/wr) */

#define  SCSI_ABRTBYTEPTR     0x2B     /* M4 */
                                       /* command aborted byte ptr.   (rd/wr) */

#define  SCSI_ABRTBITPTR      0x2C     /* M4 */
                                       /* command aborted bit ptr.    (rd/wr) */

/* The following are Rev B only defines used for Packetized Target Mode       */
#define  SCSI_MAXCMDBYTES     0x2D     /* M4 */
                                       /* max command bytes to receive        */
                                       /* (in 8 byte units)           (rd/wr) */

#define  SCSI_MAXCMD2RCV      0x2E     /* M4 */
                                       /* max number of commands to receive   */
                                       /* in one packetized target mode       */
                                       /* connection.                 (rd/wr) */

#define  SCSI_SHORTTHRESH     0x2F     /* M4 */
                                       /* shortage threshold indicator - only */
                                       /* used when HESCB_QOFF/SESCB_QOFF     */
                                       /* registers used.             (rd/wr) */
/* End of Rev B Packetized Target Mode defines                                */

#define  SCSI_LUNLEN          0x30     /* M4 */
                                       /* LUN number length           (rd/wr) */
/* Field defines for Rev B only */
#define     SCSI_TLUNLEN3       0x80   /* LUN length for target mode          */
#define     SCSI_TLUNLEN2       0x40   /*                                     */
#define     SCSI_TLUNLEN1       0x20   /*                                     */
#define     SCSI_TLUNLEN0       0x10   /*                                     */
#define     SCSI_TLUNLEN        (SCSI_TLUNLEN3+SCSI_TLUNLEN2+SCSI_TLUNLEN1+SCSI_TLUNLEN0)
#define     SCSI_TSINGLE_LEVEL_LUN  0xF0  /* Specifies to use single level    */
                                          /* format (i.e. One LUN byte copied */
                                          /* from SCB to byte 5 of the SPI    */
                                          /* L_Q.                             */   

#define     SCSI_ILUNLEN3       0x08   /* LUN length for initiator mode       */
#define     SCSI_ILUNLEN2       0x04   /*                                     */
#define     SCSI_ILUNLEN1       0x02   /*                                     */
#define     SCSI_ILUNLEN0       0x01   /*                                     */
#define     SCSI_ILUNLEN        (SCSI_ILUNLEN3+SCSI_ILUNLEN2+SCSI_ILUNLEN1+SCSI_ILUNLEN0)
#define     SCSI_ISINGLE_LEVEL_LUN  0x0F  /* Specifies to use single level    */
                                          /* format (i.e. One LUN byte copied */
                                          /* from SCB to byte 5 of the SPI    */
                                          /* L_Q.                             */   
/* End field defines for Rev B only */ 

#define  SCSI_CDBLIMIT        0x31     /* M4 */
                                       /* cmd descriptor block limit  (rd/wr) */

#define  SCSI_MAXCMD          0x32     /* M4 */
                                       /* maximum commands            (rd/wr) */

#define  SCSI_MAXCMDCNT       0x33     /* M4 */
                                       /* maximum command counter     (rd/wr) */

#define  SCSI_LQRSVD01        0x34     /* M4 */
                                       /* LQ packet reserved bytes    (rd/wr) */

#define  SCSI_LQRSVD16        0x35     /* M4 */
                                       /* LQ packet reserved bytes    (rd/wr) */

#define  SCSI_LQRSVD17        0x36     /* M4 */
                                       /* LQ packet reserved bytes    (rd/wr) */

#define  SCSI_CMDRSVD0        0x37     /* M4 */
                                       /* command reserved byte 0     (rd/wr) */

#define  SCSI_LQCTL0          0x38     /* M4 */
                                       /* LQxMGR control, byte 0      (rd/wr) */
#define     SCSI_LQITARGCTL1  0x80     /* control LQIMGR behaves in           */
#define     SCSI_LQITARGCTL0  0x40     /*   target mode                       */
#define     SCSI_LQIINTCTL1   0x20     /* control LQIMGR behaves in           */
#define     SCSI_LQIINTCTL0   0x10     /*   initiator mode                    */
#define     SCSI_LQOTARGCTL1  0x08     /* control LQOMGR behaves in           */
#define     SCSI_LQOTARGCTL0  0x04     /*   target mode                       */
#define     SCSI_LQOINTCTL1   0x02     /* control LQOMGR behaves in           */
#define     SCSI_LQOINTCTL0   0x01     /*   initiator mode                    */

#define  SCSI_LQCTL1          0x38     /* M(0,1,3) */
                                       /* LQxMGR control, byte 1      (rd/wr) */
#define     SCSI_SHORTAGE     0x08     /* Shortage of target mode resources   */ 
#if !SCSI_DCH_U320_MODE 
#define     SCSI_PCI2PCI      0x04     /* pci-to-pci copy function            */
#else
#define     SCSI_DFFWRAP      0x04     /* Enables Host copy function          */
#endif /* !SCSI_DCH_U320_MODE */

#define     SCSI_SINGLECMD    0x02     /* (targ) LQIMGR stop every command    */
#define     SCSI_ABORTPENDING 0x01     /* (init) LQIMGR check for abort cmd   */

#define  SCSI_LQCTL2          0x39     /* M(0,1,3) */
                                       /* LQxMGR control, byte 2      (rd/wr) */
#define     SCSI_LQIRETRY     0x80     /* LQIMGR retries last packet xferred  */
#define     SCSI_LQICONTINUE  0x40     /* LQIMGR xfers next expected packet   */
#define     SCSI_LQITOIDLE    0x20     /* force LQIMGR back to idle           */
#define     SCSI_LQIPAUSE     0x10     /* signals LQIMGR to pause asap   (wr) */
#define     SCSI_LQIPAUSEACK  0x10     /* acknowledge LQIMGR pause       (rd) */
#define     SCSI_LQORETRY     0x08     /* LQIMGR retries last packet xferred  */
#define     SCSI_LQOCONTINUE  0x04     /* LQOMGR xfers next expected packet   */
#define     SCSI_LQOTOIDLE    0x02     /* force LQOMGR back to idle           */
#define     SCSI_LQOPAUSE     0x01     /* signals LQOMGR to pause asap   (wr) */
#define     SCSI_LQOPAUSEACK  0x01     /* acknowledge LQOMGR pause       (rd) */

#define  SCSI_SCSBIST0        0x39     /* M4 */
                                       /* scsi ram bist, byte 0       (rd/wr) */
#define     SCSI_GSBISTERR    0x40     /* good status FIFO BIST error    (rd) */
#define     SCSI_GSBISTDONE   0x20     /* good status FIFO BIST done     (rd) */
#define     SCSI_GSBISTRUN    0x10     /* good status FIFO BIST run           */
#define     SCSI_OSBISTERR    0x04     /* output sync BIST error         (rd) */
#define     SCSI_OSBISTDONE   0x02     /* output sync BIST done          (rd) */
#define     SCSI_OSBISTRUN    0x01     /* output sync BIST run                */

#define  SCSI_SCSBIST1        0x3A     /* M4 */
                                       /* scsi ram bist, byte 1       (rd/wr) */
#define     SCSI_NTBISTERR    0x04     /* negotiation table BIST error   (rd) */
#define     SCSI_NTBISTDONE   0x02     /* negotiation table BIST done    (rd) */
#define     SCSI_NTBISTRUN    0x01     /* negotiation table BIST run          */

#define  SCSI_SCSISEQ0        0x3A     /* M(0,1,3) */
                                       /* scsi sequence control 0     (rd/wr) */
#define     SCSI_TEMODEO      0x80     /* target mode enable out              */
#define     SCSI_ENSELO       0x40     /* enable selection out                */
#define     SCSI_ENARBO       0x20     /* enable arbitration out              */
#define     SCSI_FORCEBUSFREE 0x10     /* force bus free                      */
#define     SCSI_SCSIRSTO     0x01     /* scsi reset out                      */

#define  SCSI_LQOSTAT3CTL     0x3B     /* M4 - Rev B ASIC Only                */

#define     SCSI_PHAERRCODE2  0x80     /* Phase Error Code (3 bits)           */ 
#define     SCSI_PHAERRCODE1  0x40     /*                                     */
#define     SCSI_PHAERRCODE0  0x20     /*                                     */
#define     SCSI_PHAERRCODE  (SCSI_PHAERRCODE2+SCSI_PHAERRCODE1+SCSI_PHAERRCODE0)

#define     SCSI_TYPE2        0x04     /* Type Code (3 bits)                  */ 
#define     SCSI_TYPE1        0x02     /*                                     */
#define     SCSI_TYPE0        0x01     /*                                     */
#define     SCSI_TYPE  (SCSI_TYPE2+SCSI_TYPE1+SCSI_TYPE0)

#define  SCSI_SCSISEQ1        0x3B     /* M(0,1,3) */
                                       /* scsi sequence control 1     (rd/wr) */
#define     SCSI_MANUALCTL    0x40     /* manual control                      */
#define     SCSI_ENSELI       0x20     /* enable selection in                 */
#define     SCSI_ENRSELI      0x10     /* enable reselection in               */
#define     SCSI_MANUALP1     0x08     /* manual control P1 signal            */
#define     SCSI_MANUALP0     0x04     /* manual control P0 signal            */
#define     SCSI_ENAUTOATNP   0x02     /* enable auto attention parity        */
#define     SCSI_ALTSTIM      0x01     /* alternate scsi timeout              */

                                       /* M(0,1) */
#define  SCSI_DLCOUNT0        0x3C     /* data length counter 0          (rd) */
#define  SCSI_DLCOUNT1        0x3D     /* data length counter 1          (rd) */
#define  SCSI_DLCOUNT2        0x3E     /* data length counter 2          (rd) */
#define  SCSI_DLCOUNT3        0x3F     /* data length counter 3          (rd) */

#define  SCSI_SXFRCTL0        0x3C     /* M3 */
                                       /* scsi transfer control 0     (rd/wr) */
#define     SCSI_DFON         0x80     /* digital filtering on                */
#define     SCSI_DFPEXP       0x40     /* digital filtering period expanded   */
#define     SCSI_BIASCANCELEN 0x10     /* transmit bias cancellation enable   */
#define     SCSI_SPIOEN       0x08     /* enable auto scsi pio                */

#define  SCSI_SXFRCTL1        0x3D     /* M3 */
                                       /* scsi transfer control 1     (rd/wr) */
#define     SCSI_BITBUCKET    0x80     /* enable bit bucket mode              */
#define     SCSI_ENSACHK      0x20     /* enable scb array checking           */
#define     SCSI_ENSPCHK      0x20     /* enable scsi parity checking         */
#define     SCSI_STIMESEL1    0x10     /* select selection timeout: 00- 256ms,*/
#define     SCSI_STIMESEL0    0x08     /* 01- 128ms, 10- 64ms, 11- 32ms       */
#define     SCSI_ENSTIMER     0x04     /* enable selection timeout            */
#define     SCSI_ACTNEGEN     0x02     /* enable active negation              */
#define     SCSI_STPWEN       0x01     /* enable termination power            */
#define     SCSI_STIMESEL     (SCSI_STIMESEL0+SCSI_STIMESEL1)

#define  SCSI_BUSINITID0      0x3C     /* M4 */
                                       /* scsi bus initiator ids      (rd/wr) */
#define  SCSI_BUSINITID1      0x3D     /* M4 */
                                       /* scsi bus initiator ids      (rd/wr) */

#define  SCSI_SXFRCTL2        0x3E     /* M3 */
                                       /* scsi transfer control 2     (rd/wr) */
#define     SCSI_ENMULTID     0x20     /* Enable 2-D rate table for multiple  */
                                       /* target ID support           (rd/wr) */
                                       /* H1B only bit.                       */
#define     SCSI_AUTORSTDIS   0x10     /* auto reset fifo disable             */
#define     SCSI_CMDDMAEN     0x08     /* (target mode) cmd dma to fifo enable*/
#define     SCSI_ASU2         0x04     /* asynchronous setup                  */
#define     SCSI_ASU1         0x02     /* asynchronous setup                  */
#define     SCSI_ASU0         0x01     /* asynchronous setup                  */
#define     SCSI_ASU          (SCSI_ASU0+SCSI_ASU1+SCSI_ASU2)

#define  SCSI_DFFSTAT         0x3F     /* M3 */
                                       /* data fifo status               (rd) */
#define     SCSI_FIFO1FREE    0x20     /* copy of similar bit in M1DFFSTAT    */
#define     SCSI_FIFO0FREE    0x10     /* copy of similar bit in M0DFFSTAT    */
#define     SCSI_CURRFIFO     0x01     /* used for SCSI->FIFO or FIFO->SCSI   */
#define     SCSI_CURRFIFO1    0x02     /* 2nd bit of CURRFIFO (REV B only)    */
#define     SCSI_CURRFIFOBITS (SCSI_CURRFIFO+SCSI_CURRFIFO1)

#if !SCSI_DCH_U320_MODE
#define  SCSI_BUSTARGID0      0x3E     /* M4 */
                                       /* scsi bus target ids         (rd/wr) */
#define  SCSI_BUSTARGID1      0x3F     /* M4 */
#else
#define  SCSI_BUSTARGID       0x3E     /* M4  (3E-3F)*/
#endif /* !SCSI_DCH_U320_MODE */
                                       /* scsi bus target ids         (rd/wr) */

#define  SCSI_SCSISIGO        0x40     /* M(0,1,3) */
                                       /* scsi control signal out     (rd/wr) */
#define     SCSI_CDO          0x80     /* set c/d (target mode)               */
#define     SCSI_IOO          0x40     /* set i/o (target mode)               */
#define     SCSI_MSGO         0x20     /* set msg (target mode)               */
#define     SCSI_ATNO         0x10     /* set atn (initiator mode)            */
#define     SCSI_SELO         0x08     /* set sel                             */
#define     SCSI_BSYO         0x04     /* set busy                            */
#define     SCSI_REQO         0x02     /* set req (target mode)               */
#define     SCSI_ACKO         0x01     /* set ack (initiator mode)            */

#define     SCSI_BUSPHASE     (SCSI_CDO+SCSI_IOO+SCSI_MSGO) /* scsi bus phase */
#define     SCSI_DOPHASE      0x00     /* data out                            */
#define     SCSI_DIPHASE      SCSI_IOO /* data in                             */
#define     SCSI_CMDPHASE     SCSI_CDO /* command                             */
#define     SCSI_MIPHASE      (SCSI_CDO+SCSI_IOO+SCSI_MSGO) /* message in     */
#define     SCSI_MOPHASE      (SCSI_CDO+SCSI_MSGO)    /*  message out         */
#define     SCSI_STPHASE      (SCSI_CDO+SCSI_IOO)     /*  status              */
#define     SCSI_ERRPHASE     0xff                    /* error                */

#define  SCSI_SCSISIGI        0x41     /* M(0,1,3) */
                                       /* scsi control signal in         (rd) */
#define     SCSI_CDI          0x80     /* c/d                                 */
#define     SCSI_IOI          0x40     /* i/o                                 */
#define     SCSI_MSGI         0x20     /* msg                                 */
#define     SCSI_ATNI         0x10     /* atn                                 */
#define     SCSI_SELI         0x08     /* sel                                 */
#define     SCSI_BSYI         0x04     /* bsy                                 */
#define     SCSI_REQI         0x02     /* req                                 */
#define     SCSI_ACKI         0x01     /* ack                                 */

#define  SCSI_MULTARGID       0x40     /* M4 */
                                       /* multiple target id          (rd/wr) */
#define  SCSI_MULTARGID0      0x40     /* M4 */
                                       /* multiple target id, lo      (rd/wr) */
#define  SCSI_MULTARGID1      0x41     /* M4 */
                                       /* multiple target id, hi      (rd/wr) */

#define  SCSI_SCSIPHASE       0x42     /* M(0,1,3) */
                                       /* scsi bus phase                 (rd) */
#define     SCSI_STATUS       0x20     /* status phase                        */
#define     SCSI_COMMAND      0x10     /* command phase                       */
#define     SCSI_MSG_IN       0x08     /* message in phase                    */
#define     SCSI_MSG_OUT      0x04     /* message out phase                   */
#define     SCSI_DIN          0x02     /* data in phase                       */
#define     SCSI_DOUT         0x01     /* data out phase                      */

#define  SCSI_SCSIDATL_IMG    0x43     /* M(0,1,3) */
                                       /* scsidatl image              (rd/wr) */

#define  SCSI_SCSIDATL        0x44     /* M(0,1,3) */
                                       /* scsi latched data, lo       (rd/wr) */
#define  SCSI_SCSIDATH        0x45     /* M(0,1,3) */
                                       /* scsi latched data, hi       (rd/wr) */

#define  SCSI_SCSIBUSL        0x46     /* M(0,1,3) */
                                       /* scsi data bus, lo           (rd/wr) */
#define  SCSI_SCSIBUSH        0x47     /* M(0,1,3) */
                                       /* scsi data bus, hi           (rd/wr) */

#define  SCSI_TARBIDIPTR      0x47     /* M4 */
                                       /* bi-directional cmnd support (rd/wr) */
                                       /* target mode only                    */
#define     SCSI_TARGBIDEN    0x80     /* Bi-directional commands supported   */
                                       /* SCB offset of byte to be stored in  */
                                       /* SPI L_Q byte 16                     */            
#define     SCSI_TARGBIDIPTR5 0x20
#define     SCSI_TARGBIDIPTR4 0x10
#define     SCSI_TARGBIDIPTR3 0x08
#define     SCSI_TARGBIDIPTR2 0x04
#define     SCSI_TARGBIDIPTR1 0x02
#define     SCSI_TARGBIDIPTR0 0x01
#define     SCSI_TARGBIDIPTRS (SCSI_TARGBIDIPTR5+SCSI_TARGBIDIPTR4+SCSI_TARGBIDIPTR3+\
                               SCSI_TARGBIDIPTR2+SCSI_TARGBIDIPTR1+SCSI_TARGBIDIPTR0)
 
#define  SCSI_TARGIDIN        0x48     /* M(0,1,3) */
                                       /* target id in                   (rd) */
#define     SCSI_CLKOUT       0x80     /* clock out provides 102.4 us period  */
#define     SCSI_TARGID3      0x08     /* scsi target id (hex) on the bus     */
#define     SCSI_TARGID2      0x04     /*                                     */
#define     SCSI_TARGID1      0x02     /*                                     */
#define     SCSI_TARGID0      0x01     /*                                     */

#define  SCSI_MAXLEGALCMD0    0x48     /* M4 */
                                       /* target mode only - maximum legal    */
                                       /* command length - lsb (rd/wr)        */

#define  SCSI_SELID           0x49     /* M(0,1,3) */
                                       /* selection/reselection id       (rd) */
#define     SCSI_SELID3       0x80     /* binary id of other device           */
#define     SCSI_SELID2       0x40     /*                                     */
#define     SCSI_SELID1       0x20     /*                                     */
#define     SCSI_SELID0       0x10     /*                                     */
#define     SCSI_ONEBIT       0x08     /* non-arbitrating selection detection */
#define     SCSI_SELIDS       (SCSI_SELID3+SCSI_SELID2+SCSI_SELID1+SCSI_SELID0)

#define  SCSI_MAXLEGALCMD1    0x48     /* M4 */
                                       /* target mode only - maximum legal    */
                                       /* command length - msb (rd/wr)        */

#define  SCSI_SBLKCTL         0x4A     /* M(0,1,3) */
                                       /* scsi block control          (rd/wr) */
#define     SCSI_DIAGLEDEN    0x80     /* diagnostic led enable               */
#define     SCSI_DIAGLEDON    0x40     /* diagnostic led on                   */
#define     SCSI_AUTOFLUSHDIS 0x20     /* disable automatic flush             */
#define     SCSI_ENAB40       0x08     /* enable 40 Mtransfer mode            */
#define     SCSI_ENAB20       0x04     /* enable 20 Mtransfer mode            */
#define     SCSI_SELWIDE      0x02     /* scsi wide hardware configure        */
#define     SCSI_XCVR         0x01     /* external transceiver                */

#define  SCSI_OPTIONMODE         0x4A  /* M4 */
                                       /* option mode                 (rd/wr) */
#define     SCSI_AUTOACKEN       0x40  /* auto ack enable                     */
#define     SCSI_BUSFREEREV      0x10  /* bus free interrupt revision enable  */
#define     SCSI_ENDGFORMCHK     0x04  /* enable data group format checker    */
#define     SCSI_AUTO_MSGOUT_DE  0x02  /* auto msg-out in dual edge mode      */
#define     SCSI_DIS_MSGIN_DE    0x01  /* disable msg-in in dual edge mode    */

#define  SCSI_SSTAT0          0x4B     /* M(0,1,3) */
                                       /* scsi status 0                  (rd) */
#define     SCSI_TARGET       0x80     /* mode = target                       */
#define     SCSI_SELDO        0x40     /* selection out completed             */
#define     SCSI_SELDI        0x20     /* have been reselected                */
#define     SCSI_SELINGO      0x10     /* arbitration won, selection started  */
#define     SCSI_IOERR        0x08     /* i/o operating mode change           */
#define     SCSI_OVERRUN      0x04     /* scsi offset overrun detected        */
#define     SCSI_SPIORDY      0x02     /* auto pio enabled & ready to xfer data*/
#define     SCSI_ARBDO        0x01     /* arbitration started by ENARBO won   */

#define  SCSI_CLRSINT0        0x4B     /* M(0,1,3) */
                                       /* clear scsi interrupts 0        (wr) */
#define     SCSI_CLRSELDO     0x40     /* clear seldo interrupt & status      */
#define     SCSI_CLRSELDI     0x20     /* clear seldi interrupt & status      */
#define     SCSI_CLRSELINGO   0x10     /* clear selingo interrupt & status    */
#define     SCSI_CLRIOERR     0x08     /* clear ioerr interrupt & status      */
#define     SCSI_CLROVERRUN   0x04     /* clear overrun interrupt & status    */
#define     SCSI_CLRSPIORDY   0x02     /* clear spiordy interrupt & status    */
#define     SCSI_CLRARBDO     0x01     /* clear arbdo interrupt & status      */

#define  SCSI_SIMODE0         0x4B     /* M4 */
                                       /* scsi interrupt mask 0       (rd/wr) */
#define     SCSI_ENSELDO      0x40     /* enable seldo status to assert int   */
#define     SCSI_ENSELDI      0x20     /* enable seldi status to assert int   */
#define     SCSI_ENSELINGO    0x10     /* enable selingo status to assert int */
#define     SCSI_ENIOERR      0x08     /* enable ioerr status to assert int   */
#define     SCSI_ENOVERRUN    0x04     /* enable overrun status to assert int */
#define     SCSI_ENSPIORDY    0x02     /* enable spiordy status to assert int */
#define     SCSI_ENARBDO      0x01     /* enable arbdo status to assert int   */

#define  SCSI_SSTAT1          0x4C     /* M(0,1,3) */
                                       /* scsi status 1                  (rd) */
#define     SCSI_SELTIMO      0x80     /* selection timeout                   */
#define     SCSI_ATNTARG      0x40     /* mode = target:  initiator set atn   */
#define     SCSI_SCSIRSTI     0x20     /* other device asserted scsi reset    */
#define     SCSI_PHASEMIS     0x10     /* actual scsi phase <> expected       */
#define     SCSI_BUSFREE      0x08     /* bus free occurred                   */
#define     SCSI_SCSIPERR     0x04     /* scsi physical error                   */
#define     SCSI_STRB2FAST    0x02     /* incoming strobe (req/ack) too fast  */
#define     SCSI_REQINIT      0x01     /* latched req                         */

#define  SCSI_CLRSINT1        0x4C     /* M(0,1,3) */
                                       /* clear scsi interrupts 1        (wr) */
#define     SCSI_CLRSELTIMO   0x80     /* clear selto interrupt & status      */
#define     SCSI_CLRATNO      0x40     /* clear atno control signal           */
#define     SCSI_CLRSCSIRSTI  0x20     /* clear scsirsti interrupt & status   */
#define     SCSI_CLRBUSFREE   0x08     /* clear busfree interrupt & status    */
#define     SCSI_CLRSCSIPERR  0x04     /* clear scsiperr interrupt & status   */
#define     SCSI_CLRSTRB2FAST 0x02     /* clear strb2fast interrupt & status  */
#define     SCSI_CLRREQINIT   0x01     /* clear reqinit interrupt & status    */

#define  SCSI_SSTAT2          0x4D     /* M0,M1,M3 */
                                       /* scsi status 2                  (rd) */
#define     BFREETIME0        0x80     /* bus free time occurs                */
#define     BFREETIME1        0x40     /*   11: data xfer in fifo 1           */
                                       /*   10: data xfer in fifo 0           */
                                       /*   01: cmd xfer by LQOMGR            */
                                       /*   00: none of the above             */
#define     SCSI_NONPACKREQ   0x20     /* unexpected non-packetized req       */
#define     SCSI_EXP_ACTIVE   0x10     /* expander active                     */
#define     SCSI_BSYX         0x80     /* expander busy                       */
#define     SCSI_WIDE_RES     0x04     /* wide residue byte (M0 & M1)         */
#define     SCSI_SDONE        0x02     /* scsi dma done (M0 & M1)             */
#define     SCSI_DMADONE      0x01     /* transfer completely done (M0 & M1)  */
#define     BFREETIME         (BFREETIME0+BFREETIME1)

                                       /* default to 0 until implemented.     */ 
#define  SCSI_EXPACTIVE_DEFAULT  0     /* default expander active             */

#define  SCSI_CLRSINT2        0x4D     /* M0,M1,M3 */
                                       /* clear scsi interrupts 2        (wr) */
#define     SCSI_CLRNONPACKREQ 0x20    /* clear nonpackreq interrupt & status */
#define     SCSI_CLRWIDE_RES  0x04     /* clear wide_res intr & stat (M0 & M1)*/
#define     SCSI_CLRSDONE     0x02     /* clear sdone intr & stat (M0 & M1)   */
#define     SCSI_CLRDMADONE   0x01     /* clear dmadone intr & stat (M0 & M1) */

#define  SCSI_SIMODE2         0x4D     /* M4 */
                                       /* scsi interrupt mask 2       (rd/wr) */
#define     SCSI_ENWIDE_RES   0x04     /* enable wide_res status to assert int*/
#define     SCSI_ENSDONE      0x02     /* enable sdone status to assert int   */
#define     SCSI_ENDMADONE    0x01     /* enable dmadone status to assert int */

#define  SCSI_PERRDIAG        0x4E     /* M(0,1,3) */
                                       /* physical error diagnosis       (rd) */
#define     SCSI_HIZERO       0x80     /* high byte zero                      */
#define     SCSI_HIPERR       0x40     /* high byte parity error              */
#define     SCSI_LASTPHASE    0x20     /* error occurred in last phase        */
#define     SCSI_PARITYERR    0x10     /* parity error                        */
#define     SCSI_AIPERR       0x08     /* aip error                           */
#define     SCSI_CRCERR       0x04     /* crc error                           */
#define     SCSI_DGFORMERR    0x02     /* data group format error             */
#define     SCSI_DTERR        0x01     /* dual-transition error               */

#define  SCSI_SOFFCNT         0x4F     /* M(0,1,3) */
                                       /* synchronous scsi offset count  (rd) */

#define  SCSI_LQISTATE        0x4E     /* M4 */
                                       /* LQIMGR current state           (rd) */

#define  SCSI_LQOSTATE        0x4F     /* M4 */
                                       /* LQOMGR current state           (rd) */

#define  SCSI_LQISTAT0        0x50     /* M(0,1,3) */
                                       /* LQIMGR status 0                (rd) */
#define     SCSI_LQISHORTAGE  0x80     /* shortage of resources to receive    */
                                       /* commands                            */
#define     SCSI_LQICMD2LONG  0x40     /* command will not fit in MAXCMDBYTES */
                                       /* limit                               */ 
#define     SCSI_LQIATNQAS    0x20     /* disconnect QAS, but init raised ATN */
#define     SCSI_LQICRCT1     0x10     /* error in an LQ packet               */
#define     SCSI_LQICRCT2     0x08     /* error in a non-LQ packet            */
#define     SCSI_LQIBADLQT    0x04     /* unknown LQ packet received          */
#define     SCSI_LQIATN       0x02     /* init raised ATN during LQ or CMD    */
                                       /* packet                              */
#define     SCSI_LQIMANFLAG   0x01     /* TMF flag set in CMD IU              */

#define  SCSI_CLRLQIINT0      0x50     /* M(0,1,3) */
                                       /* clear LQI interrutps 0         (wr) */
#define     SCSI_CLRLQISHORTAGE 0x80   /* clear lqishortage interrupt and status */
#define     SCSI_CLRLQICMD2LONG 0x40   /* clear lqicmd2long interrupt and status */
#define     SCSI_CLRLQIATNQAS 0x20     /* clear lqiatnqas interrupt & status  */
#define     SCSI_CLRLQICRCT1  0x10     /* clear lqicrct1 interrupt & status   */
#define     SCSI_CLRLQICRCT2  0x08     /* clear lqicrct2 interrupt & status   */
#define     SCSI_CLRLQIBADLQT 0x04     /* clear lqibadlqt interrupt & status  */
#define     SCSI_CLRLQIATN    0x02     /* clear lqiatn interrupt & status     */
#define     SCSI_CLRLQIMANFLAG 0x01    /* clear lqimanflag interrupt & status */

#define  SCSI_LQIMODE0        0x50     /* M4 */
                                       /* LQIMGR interrupt mask 0     (rd/wr) */
#define     SCSI_LQISHORTAGEMSK 0x80   /* enable lqishortage stat to assert int */
#define     SCSI_LQICMD2LONGMSK 0x40   /* enable lqicmd2long stat to assert int */ 
#define     SCSI_LQIATNQASMSK 0x20     /* enable lqiatnqas stat to assert int */
#define     SCSI_LQICRCT1MSK  0x10     /* enable lqicrct1 stat to assert int  */
#define     SCSI_LQICRCT2MSK  0x08     /* enable lqicrct1 stat to assert int  */
#define     SCSI_LQIBADLQTMSK 0x04     /* enable lqibadlqt stat to assert int */
#define     SCSI_LQIATNMSK    0x02     /* enable lqiatn stat to assert int    */
#define     SCSI_LQIMANFLAGMSK 0x01    /* enable lqimanflag stat to assert int */

#define  SCSI_LQISTAT1        0x51     /* M(0,1,3) */
                                       /* LQIMGR status 1                (rd) */
#define     SCSI_LQIPHASE1    0x80     /* phase change on LQ packet           */
#define     SCSI_LQIPHASE2    0x40     /* phase change on non-LQ packet       */
#define     SCSI_LQIABORT     0x20     /* LQ packet is an aborted cmd         */
#define     SCSI_LQICRCI1     0x10     /* error in an LQ packet               */
#define     SCSI_LQICRCI2     0x08     /* error in a non-LQ packet            */
#define     SCSI_LQIBADLQI    0x04     /* unknown LQ packet received          */
#define     SCSI_LQIOVERI1    0x02     /* many REQs before last ACK of LQ     */
#define     SCSI_LQIOVERI2    0x01     /* many REQs before last ACK of non-LQ */

#define  SCSI_CLRLQIINT1      0x51     /* M(0,1,3) */
                                       /* clear LQI interrutps 1         (wr) */
#define     SCSI_CLRLQIPHASE1 0x80     /* clear lqiphase1 interrupt & status  */
#define     SCSI_CLRLQIPHASE2 0x40     /* clear lqiphase2 interrupt & status  */
#define     SCSI_CLRLQIABORT  0x20     /* clear lqiabort interrupt & status   */
#define     SCSI_CLRLQICRCI1  0x10     /* clear lqicrci1 interrupt & status   */
#define     SCSI_CLRLQICRCI2  0x08     /* clear lqicrci2 interrupt & status   */
#define     SCSI_CLRLQIBADLQI 0x04     /* clear lqibadlqi interrupt & status  */
#define     SCSI_CLRLQIOVERI1 0x02     /* clear lqioveri1 interrupt & status  */
#define     SCSI_CLRLQIOVERI2 0x01     /* clear lqioveri2 interrupt & status  */

#define  SCSI_LQIMODE1        0x51     /* M4 */
                                       /* LQIMGR interrupt mask 1     (rd/wr) */
#define     SCSI_LQIPHASE1MSK 0x80     /* enable lqiphase1 stat to assert int */
#define     SCSI_LQIPHASE2MSK 0x40     /* enable lqiphase2 stat to assert int */
#define     SCSI_LQIABORTMSK  0x20     /* enable lqiabort stat to assert int  */
#define     SCSI_LQICRCI1MSK  0x10     /* enable lqicrci1 stat to assert int  */
#define     SCSI_LQICRCI2MSK  0x08     /* enable lqicrci2 stat to assert int  */
#define     SCSI_LQIBADLQIMSK 0x04     /* enable lqibadlqi stat to assert int */
#define     SCSI_LQIOVERI1MSK 0x02     /* enable lqioveri1 stat to assert int */
#define     SCSI_LQIOVERI2MSK 0x01     /* enable lqioveri2 stat to assert int */

#define  SCSI_LQISTAT2        0x52     /* M(0,1,3) */
                                       /* LQIMGR status 2                (rd) */
#define     SCSI_LQPACKETIZED 0x80     /* connected device is packetized      */
#define     SCSI_LQIPHASE0    0x40     /* unexpected bus phase between packets*/
#define     SCSI_LQIWORKONLQ  0x20     /* current working LQ packet           */
#define     SCSI_LQIWAITFIFO  0x10     /* waiting for a free data fifo        */
#define     SCSI_LQISTOPPKT   0x08     /* stopped at the end of non-LQ packet */
#define     SCSI_LQISTOPLQ    0x04     /* stopped at the end of LQ packet     */
#define     SCSI_LQISTOPCMD   0x02     /* stopped on non-LQ of the singlecmd  */
#define     SCSI_LQIGSAVAIL   0x01     /* queue tag avail on good status fifo */

#define  SCSI_SSTAT3          0x53     /* M(0,1,3) */
                                       /* scsi status 3                  (rd) */
#define     SCSI_TRNERR       0x08     /* Training error detected             */
#define     SCSI_SARAMPERR    0x04     /* scb array ram parity error          */
#define     SCSI_NTRAMPERR    0x02     /* negotiation table ram parity error  */
#define     SCSI_OSRAMPERR    0x01     /* output sync ram parity error        */

#define  SCSI_CLRSINT3        0x53     /* M(0,1,3) */
                                       /* clear scsi status 3            (wr) */
#define     SCSI_CLRTRNERR    0x08     /* clear trnerr interrupt & status     */
#define     SCSI_CLRSARAMPERR 0x04     /* clear saramperr interrupt & status  */
#define     SCSI_CLRNTRAMPERR 0x02     /* clear ntramperr interrupt & status  */
#define     SCSI_CLROSRAMPERR 0x01     /* clear osramperr interrupt & status  */

#define  SCSI_SIMODE3         0x53     /* M4 */
                                       /* scsi interrupt mask 3          (rd) */
#define     SCSI_ENTRNERR     0x08     /* enable trnerr stat to assert int    */
#define     SCSI_SARAMPERR    0x04     /* enable saramperr stat to assert int */
#define     SCSI_NTRAMPERR    0x02     /* enable ntramperr stat to assert int */
#define     SCSI_OSRAMPERR    0x01     /* enable osramperr stat to assert int */

#define  SCSI_LQOSTAT0        0x54     /* M(0,1,3) */
                                       /* LQOMGR status 0                (rd) */
#define     SCSI_LQOSTOPT2    0x08     /* stopped due to degraded operation   */
#define     SCSI_LQOATNLQ     0x04     /* init raised ATN during LQ packet    */
#define     SCSI_LQOATNPKT    0x02     /* init raised ATN during non-LQ packet*/
#define     SCSI_LQOTCRC      0x01     /* CRC on an incoming non-LQ packet    */

#define  SCSI_CLRLQOINT0      0x54     /* M(0,1,3) */
                                       /* clear LQO interrutps 0         (wr) */
#define     SCSI_CLRLQOSTOPT2 0x08     /* clear lqostopt2 interrupt & status  */
#define     SCSI_CLRLQOATNLQ  0x04     /* clear lqoatnlq interrupt & status   */
#define     SCSI_CLRLQOATNPKT 0x02     /* clear lqoatnpkt interrupt & status  */
#define     SCSI_CLRLQOTCRC   0x01     /* clear lqotcrc interrupt & status    */

#define  SCSI_LQOMODE0        0x54     /* M4 */
                                       /* LQOMGR interrupt mask 0     (rd/wr) */
#define     SCSI_ENLQOSTOPT2  0x08     /* enable lqostopt2 stat to assert int */
#define     SCSI_ENLQOATNLQ   0x04     /* enable lqoatnlq stat to assert int  */
#define     SCSI_ENLQOATNPKT  0x02     /* enable lqoatnpkt stat to assert int */
#define     SCSI_ENLQOTCRC    0x01     /* enable lqotcrc stat to assert int   */

#define  SCSI_LQOSTAT1        0x55     /* M(0,1,3) */
                                       /* LQOMGR status 1                (rd) */
#define     SCSI_LQOSTOPI2    0x08     /* stopped due to degraded operation   */
#define     SCSI_LQOBADQAS    0x04     /* received unexpected QAS from target */
#define     SCSI_LQOBUSFREE1  0x02     /* unexpected bus free during packetized*/
#define     SCSI_LQOPHACHG1   0x01     /* scsi phase is not DT data out       */

#define  SCSI_CLRLQOINT1      0x55     /* M(0,1,3) */
                                       /* clear LQO interrutps 1         (wr) */
#define     SCSI_CLRLQOSTOPI2 0x08     /* clear lqostopi2 interrupt & status  */
#define     SCSI_CLRLQOBADQAS 0x04     /* clear lqobadqas interrupt & status  */
#define     SCSI_CLRLQOBUSFREE1 0x02   /* clear lqobusfree1 interrupt & status*/
#define     SCSI_CLRLQOPHACHG1  0x01   /* clear lqophachg1 interrupt & status */

#define  SCSI_LQOMODE1        0x55     /* M4 */
                                       /* LQOMGR interrupt mask 1     (rd/wr) */
#define     SCSI_ENLQOSTOPI2  0x08     /* enable lqostopi2 stat to assert int */
#define     SCSI_ENLQOBADQAS  0x04     /* enable lqobadqas stat to assert int */
#define     SCSI_ENLQOBUSFREE1 0x02    /* enable lqobusfree1 stat to assert int*/
#define     SCSI_ENLQOPHACHG1 0x01     /* enable lqophachg1 stat to assert int*/

#define  SCSI_LQOSTAT2        0x56     /* M(0,1,3) */
                                       /* LQOMGR status 2                (rd) */
#define     SCSI_LQOPKT2      0x80     /* six possible packets of an scb the  */
#define     SCSI_LQOPKT1      0x40     /* LQOMGR is working on                */
#define     SCSI_LQOPKT0      0x20     /*                                     */
#define     SCSI_LQOWAITFIFO  0x10     /* waiting for a free data fifo        */
#define     SCSI_LQOLASTAVAIL 0x04     /* (targ mode) complete scb# in LASTSCB*/
#define     SCSI_LQOPHACHG0   0x02     /* (init mode) phase isn't DT data out */
#define     SCSI_LQOSTOP0     0x01     /* stopped due to degraded operation   */
#define     SCSI_LQOPKT       (SCSI_LQOPKT0+SCSI_LQOPKT1+SCSI_LQOPKT2)

#define  SCSI_OS_SPACE_CNT    0x56     /* M4 */
                                       /* output synchronizer space count (rd)*/

#define  SCSI_SIMODE1         0x57     /* M(0,1,3,4) */
                                       /* scsi interrupt mask 1       (rd/wr) */
#define     SCSI_ENSELTIMO    0x80     /* enable selto stat to assert int     */
#define     SCSI_ENATNTARG    0x40     /* enable atntarg stat to assert int   */
#define     SCSI_ENSCSIRST    0x20     /* enable scsirst stat to assert int   */
#define     SCSI_ENPHASEMIS   0x10     /* enable phasemis stat to assert int  */
#define     SCSI_ENBUSFREE    0x08     /* enable busfree stat to assert int   */
#define     SCSI_ENSCSIPERR   0x04     /* enable scsiperr stat to assert int  */
#define     SCSI_ENSTRB2FAST  0x02     /* enable strb2fast stat to assert int */
#define     SCSI_ENREQINIT    0x01     /* enable reqinit stat to assert int   */

#define  SCSI_GSFIFO0         0x58     /* M(0,1,3) */
                                       /* good status fifo ports lo      (rd) */
#define  SCSI_GSFIFO1         0x59     /* M(0,1,3) */
                                       /* good status fifo ports hi      (rd) */

#define  SCSI_SXFRCTL         0x5A     /* M0,M1 */
                                       /* scsi xfer control           (rd/wr) */
#define     SCSI_M01DELAYSTAT 0x10     /* Packetized target mode status bit   */
                                       /* related to bit bucket of a command  */
                                       /* packet being received.              */
#define     SCSI_M01BITBUCKET 0x08     /* bit bucket (valid for H2 Rev B only)*/
#define     SCSI_CLRSHCNT     0x04     /* clear shadow scsi xfer counter      */
#define     SCSI_CLRCHN       0x02     /* clear channel n                     */
#define     SCSI_RSTCHN       0x01     /* reset channel n                     */


#define  SCSI_NEXTSCB0        0x5A     /* M3 */
                                       /* next scsi control block lo     (rd) */
#define  SCSI_NEXTSCB1        0x5B     /* M3 */
                                       /* next scsi control block hi     (rd) */

#define  SCSI_LQOSCSCTL       0x5A     /* M4, defined for Harpoon2 RevB only  */
                                       /* ???                         (rd/wr) */
#define     SCSI_H2AVERSION   0x80     /* enable backward compatibility       */
#define     SCSI_LQONOCHKOVR  0x01     /* enable overrun checking during      */
                                       /* LQ/CMD out.                         */

#define  SCSI_M01ARPINT       0x5B     /* M0,M1 */
                                       /* arp interrupts for fifo 0/1    (rd) */
#define     SCSI_CTXTDONE     0x40     /* context done                        */
#define     SCSI_SAVEPTRS     0x20     /* save data pointers                  */
#define     SCSI_CFG4DATA     0x10     /* configure for data                  */
#define     SCSI_CFG4ISTAT    0x08     /* configure for initiator mode status */
#define     SCSI_CFG4TSTAT    0x04     /* configure for target mode status    */
#define     SCSI_CFG4ICMD     0x02     /* configure for initiator mode cmd    */
#define     SCSI_CFG4TCMD     0x01     /* configure for target mode cmd       */

#define  SCSI_M01CLRARPINT    0x5B     /* M0,M1 */
                                       /* clear arp intr's for fifo 0/1  (wr) */
#define     SCSI_CLRCTXTDONE  0x40     /* context done                        */
#define     SCSI_CLRSAVEPTRS  0x20     /* save data pointers                  */
#define     SCSI_CLRCFG4DATA  0x10     /* configure for data                  */
#define     SCSI_CLRCFG4ISTAT 0x08     /* configure for initiator mode status */
#define     SCSI_CLRCFG4TSTAT 0x04     /* configure for target mode status    */
#define     SCSI_CLRCFG4ICMD  0x02     /* configure for initiator mode cmd    */
#define     SCSI_CLRCFG4TCMD  0x01     /* configure for target mode cmd       */

#define  SCSI_M01ARPMSK       0x5C     /* M0,M1 */
                                       /* arp intr's mask for fifo 0/1 (rd/wr)*/
#define     SCSI_CTXTDONEMSK  0x40     /* enable ctxtdone stat to assert int  */
#define     SCSI_SAVEPTRSMSK  0x20     /* enable saveptrs stat to assert int  */
#define     SCSI_CFG4DATAMSK  0x10     /* enable cfg4data stat to assert int  */
#define     SCSI_CFG4ISTATMSK 0x08     /* enable cfg4istat stat to assert int */
#define     SCSI_CFG4TSTATMSK 0x04     /* enable cfg4tstat stat to assert int */
#define     SCSI_CFG4ICMDMSK  0x02     /* enable cfg4icmd stat to assert int  */
#define     SCSI_CFG4TCMDMSK  0x01     /* enable cfg4tcmd stat to assert int  */

#define  SCSI_CURRSCB0        0x5C     /* M3 */
                                       /* current scsi control block, lo (rd) */
#define  SCSI_CURRSCB1        0x5D     /* M3 */
                                       /* current scsi control block, hi (rd) */
                              /* Interface defined name for seq/CHIM */
#define  SCSI_WAITING_SCB     SCSI_CURRSCB0

#define  SCSI_M01DFFSTAT      0x5D     /* M0,M1 */
                                       /* data fifo status            (rd/wr) */
#define     SCSI_SHCNTMINUS1  0x20     /* (valid for H2 Rev B only) shadow    */
                                       /* count is negative 1 (-1)            */
#define     SCSI_LASTSDONE    0x10     /* last sg segment done           (rd) */
#define     SCSI_SHVALID      0x08     /* shadow count valid                  */
#define     SCSI_DLZERO       0x04     /* data length counter reached zero(rd)*/
#define     SCSI_DATAINFIFO   0x02     /* fifo was last used for data info    */
#define     SCSI_FIFOFREE     0x01     /* fifo is free                        */

#define  SCSI_CRCCONTROL      0x5D     /* M4 */
                                       /* crc control                 (rd/wr) */
#define     SCSI_CRCVALCHKEN  0x40     /* enable crc value check              */

#define  SCSI_M01DFFTAG0      0x5E     /* M0,M1 */
                                       /* data fifo queue tag lo      (rd/wr) */
#define  SCSI_M01DFFTAG1      0x5F     /* M0,M1 */
                                       /* data fifo queue tag hi      (rd/wr) */

#define  SCSI_LASTSCB0        0x5E     /* M3 */
                                       /* last scsi control block lo     (rd) */
#define  SCSI_LASTSCB1        0x5F     /* M3 */
                                       /* last scsi control block hi     (rd) */

#define  SCSI_SCSITEST           0x5E  /* M4 */
                                       /* scsi test control           (rd/wr) */
#define     SCSI_CNTRTEST        0x08  /* counter test                        */
#define     SCSI_SEL_TXPLL_DEBUG 0x04  /* select transmit pll debug           */

#define  SCSI_IOPDNCTL        0x5F     /* M4 */
                                       /* scsi iocell powerdown control(rd/wr)*/
#define     SCSI_DISABLE_OE   0x80     /* enable crc on single-edge           */
#define     SCSI_DISABLE_OEA  0x40     /* enable crc value check              */
#define     SCSI_PDN_LBGDMTL  0x10     /* enable crc req check                */
#define     SCSI_PDN_VTBIAS   0x08     /* enable target-mode crc at end       */
#define     SCSI_PND_IDIST    0x04     /* enable target-mode crc at count     */
#define     SCSI_PND_BIAS1    0x02     /* enable target-mode crc at count     */
#define     SCSI_PND_DIFFSENSE 0x01    /* enable target-mode crc at count     */

#define  SCSI_DGRPCRC0        0x60     /* M4 */
                                       /* data group crc interval lo  (rd/wr) */
#define  SCSI_DGRPCRC1        0x61     /* M4 */
                                       /* data group crc interval hi  (rd/wr) */

#define  SCSI_PACKCRC0        0x62     /* M4 */
                                       /* packetized crc interval lo  (rd/wr) */
#define  SCSI_PACKCRC1        0x63     /* M4 */
                                       /* packetized crc interval hi  (rd/wr) */

#define  SCSI_SHADDR0         0x60     /* M0,M1 */
#define  SCSI_SHADDR1         0x61     /* shadow (scsi) host address     (rd) */
#define  SCSI_SHADDR2         0x62     /* incremented by scsi ack             */
#define  SCSI_SHADDR3         0x63     /*                                     */
#define  SCSI_SHADDR4         0x64     /*                                     */
#define  SCSI_SHADDR5         0x65     /*                                     */
#define  SCSI_SHADDR6         0x66     /*                                     */
#define  SCSI_SHADDR7         0x67     /*                                     */

#define  SCSI_SHCNT0          0x68     /* M0,M1 */
#define  SCSI_SHCNT1          0x69     /* shadow (scsi) transfer count (rd/wr)*/
#define  SCSI_SHCNT2          0x6A     /*                                     */

#define  SCSI_NEGOADDR        0x60     /* M3 */
                                       /* data xfer nego address      (rd/wr) */
/* The SCSI_OWN_NEGOADDRx fields only apply to Harpoon1B when Multi targid    */
/* enabled (SXFRCTL2 ENMULTID bit).                                           */    
#define     SCSI_OWN_NEGOADDR3 0x80    /* own address into negotiation table  */
#define     SCSI_OWN_NEGOADDR2 0x40    /*                                     */
#define     SCSI_OWN_NEGOADDR1 0x20    /*                                     */
#define     SCSI_OWN_NEGOADDR0 0x10    /*                                     */
#define     SCSI_OWN_NEGOADDR  (SCSI_NEGOADDR0+SCSI_NEGOADDR1+SCSI_NEGOADDR2+\
                                SCSI_NEGOADDR3)
#define     SCSI_NEGOADDR3    0x08     /* address into negotiation table      */
#define     SCSI_NEGOADDR2    0x04     /*                                     */
#define     SCSI_NEGOADDR1    0x02     /*                                     */
#define     SCSI_NEGOADDR0    0x01     /*                                     */

#define  SCSI_NEGODATA0       0x61     /* M3 */
                                       /* data xfer nego data 0       (rd/wr) */
#define     SCSI_PERIOD7      0x80     /* scsi data xfer period               */
#define     SCSI_PERIOD6      0x40     /*                                     */
#define     SCSI_PERIOD5      0x20     /*                                     */
#define     SCSI_PERIOD4      0x10     /*                                     */
#define     SCSI_PERIOD3      0x08     /*                                     */
#define     SCSI_PERIOD2      0x04     /*                                     */
#define     SCSI_PERIOD1      0x02     /*                                     */
#define     SCSI_PERIOD0      0x01     /*                                     */
#define     SCSI_PERIOD       (SCSI_PERIOD0+SCSI_PERIOD1+SCSI_PERIOD2+\
                               SCSI_PERIOD3+SCSI_PERIOD4+SCSI_PERIOD5+\
                               SCSI_PERIOD6+SCSI_PERIOD7)

#define  SCSI_MAX_SYNC_PERIOD_FACTOR 0x32 /* max xfer period factor to go sync*/
                                          /*   xfer period = 200ns or         */
                                          /*   xfer rate = 5.0MB/s (Narrow)   */
#define  SCSI_MAX_DT_PERIOD_FACTOR   0x19 /* max xfer period factor to go DT  */
                                          /*   xfer period = 100ns or         */
                                          /*   xfer rate = 20.0MB/s (Wide)    */

#define  SCSI_NEGODATA1       0x62     /* M3 */
                                       /* data xfer nego data 1       (rd/wr) */
#define     SCSI_OFFSET7      0x80     /* scsi data xfer offset               */
#define     SCSI_OFFSET6      0x40     /*                                     */
#define     SCSI_OFFSET5      0x20     /*                                     */
#define     SCSI_OFFSET4      0x10     /*                                     */
#define     SCSI_OFFSET3      0x08     /*                                     */
#define     SCSI_OFFSET2      0x04     /*                                     */
#define     SCSI_OFFSET1      0x02     /*                                     */
#define     SCSI_OFFSET0      0x01     /*                                     */
#define     SCSI_OFFSET       (SCSI_OFFSET7+SCSI_OFFSET6+SCSI_OFFSET5+\
                               SCSI_OFFSET4+SCSI_OFFSET3+SCSI_OFFSET2+\
                               SCSI_OFFSET1+SCSI_OFFSET0)

#define  SCSI_MAX_OFFSET      0xFE     /* Harpoon supports max offset is 254  */

#define  SCSI_NEGODATA2       0x63     /* M3 */
                                       /* data xfer nego data 2       (rd/wr) */
#define     SCSI_SPI4         0x08     /* free running clock/paced xfer       */
#define     SCSI_QUICKARB     0x04     /* QAS agreed with other device        */
#define     SCSI_DUALEDGE     0x02     /* DT agreed with other device         */
#define     SCSI_PACKETIZED   0x01     /* packetized agreed with other device */

#define  SCSI_NEGODATA3       0x64     /* M3 */
                                       /* data xfer nego data 3       (rd/wr) */
#define     SCSI_SNAPSHOT     0x40     /* Enable 54 word pause U320 streaming */
                                       /*   Rev B Only*/
#define     SCSI_SLOWCRC      0x08     /* Enable 4 word pause pre and post CRC*/
                                       /*   Rev B Only*/
#if !SCSI_DCH_U320_MODE                                       
#define     SCSI_ENAIP        0x08     /* enable AIP during non-data phases   */
#endif /* SCSI_DCH_U320_MODE */
#define     SCSI_ENAUTOATNI   0x04     /* auto assert ATN when reselected     */
#define     SCSI_ENAUTOATNO   0x02     /* auto assert ATN when select         */
#define     SCSI_WIDE         0x01     /* wide agreed with other device       */

#define  SCSI_ANNEXCOL        0x65     /* M3 */
                                       /* nego table annex column     (rd/wr) */
#define     SCSI_ANNEXCOL3    0x08     /* byte of info to be read/written     */
#define     SCSI_ANNEXCOL2    0x04     /* ANNEXDAT register                   */
#define     SCSI_ANNEXCOL1    0x02     /*                                     */
#define     SCSI_ANNEXCOL0    0x01     /*                                     */
#define     SCSI_PERDEVICE0   0x04     /* Column number for Per-device byte 0 */
#define     SCSI_PERDEVICE1   0x05     /* Column number for Per-device byte 1 */
#define     SCSI_PERDEVICE2   0x06     /* Column number for Per-device byte 2 */
#define     SCSI_PERDEVICE3   0x07     /* Column number for Per-device byte 3 */
#define     SCSI_PERBUS0      0x08     /* Column number for Per-bus    byte 0 */
#define     SCSI_PERBUS1      0x09     /* Column number for Per-bus    byte 1 */
#define     SCSI_PERBUS2      0x0A     /* Column number for Per-bus    byte 2 */
#define     SCSI_PERBUS3      0x0B     /* Column number for Per-bus    byte 3 */

/* The SCSI_ANNEXCOL values in Harpoon1B when Multi targid enabled (SXFRCTL2  */
/* ENMULTID bit).                                                             */    
#define     SCSI_MTID_PERDEVICE0  0x00 /* Column number for Per-device byte 0 */
#define     SCSI_MTID_PERDEVICE1  0x01 /* Column number for Per-device byte 0 */
#define     SCSI_MTID_PERDEVICE2  0x02 /* Column number for Per-device byte 0 */
#define     SCSI_MTID_PERDEVICE3  0x03 /* Column number for Per-device byte 0 */
/* 04h - 2Bh are RTI configuration data                                       */

#define  SCSI_SCSCHKN         0x66     /* M4, defined for Harpoon2 RevB only  */
                                       /* ???                         (rd/wr) */
#define     SCSI_WIDERESEN    0x10     /* Wide Residue Enable bit             */
#define     SCSI_SHVALIDSTDIS 0x02     /* Shadow Valid Stretch Disable        */

#define     SCSI_SLEWLEVEL          0x78
#define     SCSI_MAX_HARPOON2B_SLEWLEVEL 0x0F
#define     SCSI_MAX_HARPOON2B_AMPLITUDE 0x07
#define     SCSI_MAX_HARPOON2A_SLEWLEVEL 0x01
#define     SCSI_MAX_HARPOON2A_AMPLITUDE 0x00

#define     SCSI_AMPLITUDE    0x07       /* Amplitude is in Annexdat per device byte 1 */
#define     SCSI_SLEWFLAG_DEFAULT   0x00 /* Slew Rate is if default per adapter or negotiated settings */
#define     SCSI_SLEWFLAG_USER      0x01 /* The user has requested slew rate be set via profile */
#define     SCSI_SLEWRATE_FAST      0x01 /* Harpoon RevA, Single Bit Slew Rate */
#define     SCSI_SLEWRATE_SLOW      0x00 /* Harpoon RevA, Single Bit Slew Rate, off */
#define     SCSI_DEFAULT_OEM1_HARPOON2B_SLEWRATE   0x08
#define     SCSI_DEFAULT_OEM1_HARPOON2B_AMPLITUDE  0x07
#define     SCSI_OEM1_PRECOMP_DEFAULT     (SCSI_PCOMP2 | SCSI_PCOMP1)
#define     SCSI_U320_SLEWRATE_DEFAULT         0x08

#define     SCSI_DEFAULT_HARPOON2B_SLEWRATE    0x08
#define     SCSI_DEFAULT_HARPOON2B_AMPLITUDE   0x07
#define     SCSI_PRECOMP_DEFAULT     (SCSI_PCOMP2 | SCSI_PCOMP1)

#define  SCSI_ANNEXDAT        0x66     /* M4 */
                                       /* nego table annex data       (rd/wr) */
#define     SCSI_PCOMP0       0x01     /* Precomp bit 0 for Per-device byte 0 */
#define     SCSI_PCOMP1       0x02     /* Precomp bit 1 for Per-device byte 0 */
#define     SCSI_PCOMP2       0x04     /* Precomp bit 2 for Per-device byte 0 */
#define     SCSI_SLEWRATE     0x40     /* Slew rate bit 6 for Per-device byte 0 */
#define     SCSI_PCOMP        (SCSI_PCOMP0+SCSI_PCOMP1+SCSI_PCOMP2)

/* The SLEWRATE is in per device control 3 in Harpoon1B when Multi targid     */
/* enabled (SXFRCTL2 ENMULTID bit).                                           */    
#define     SCSI_MTID_SLEWRATE3  0x08  /* Slew rate bits when ENMULTID = 1    */       
#define     SCSI_MTID_SLEWRATE2  0x04  /* Slew rate bits when ENMULTID = 1    */       
#define     SCSI_MTID_SLEWRATE1  0x02  /* Slew rate bits when ENMULTID = 1    */       
#define     SCSI_MTID_SLEWRATE0  0x01  /* Slew rate bits when ENMULTID = 1    */       
#define     SCSI_MTID_SLEWRATE   (SCSI_MTID_SLEWRATE3+SCSI_MTID_SLEWRATE2+\
                                  SCSI_MTID_SLEWRATE1+SCSI_MTID_SLEWRATE0)

#define  SCSI_IOWNID          0x67     /* M3 */
                                       /* initiator own id            (rd/wr) */
#define     SCSI_IOID3        0x08     /* a binary representation of the      */
#define     SCSI_IOID2        0x04     /* chip's id on scsi bus during any    */
#define     SCSI_IOID1        0x02     /* selection-out/reselection-in        */
#define     SCSI_IOID0        0x01     /*                                     */
                                       /* our scsi device id mask             */
#define     SCSI_IOID         (SCSI_IOID0+SCSI_IOID1+SCSI_IOID2+SCSI_IOID3)

/* The SCSI_NEGODAT4 register only applies to Harpoon1B when Multi targid     */
/* enabled (SXFRCTL2 ENMULTID bit).                                           */    
#define  SCSI_NEGODAT4        0x68     /* M3 */
                                       /* training information        (rd/wr) */
#define     SCSI_INITVLDIN    0x08     /* RTI table updated due to DT DATA IN */
#define     SCSI_INITVLDOUT   0x04     /* RTI table updated due to DT DATA OUT*/
#define     SCSI_TARGVLDIN    0x02     /* RTI table updated due to DT DATA IN */
#define     SCSI_TARGVLDOUT   0x01     /* RTI table updated due to DT DATA OUT*/

#define  SCSI_TOWNID          0x69     /* M3 */
                                       /* target own id               (rd/wr) */
#define     SCSI_TOID3        0x08     /* a binary representation of the      */
#define     SCSI_TOID2        0x04     /* chip's id on scsi bus during any    */
#define     SCSI_TOID1        0x02     /* selection-in/reselection-out        */
#define     SCSI_TOID0        0x01     /*                                     */
                                       /* our scsi device id mask             */
#define     SCSI_TOID         (SCSI_TOID0+SCSI_TOID1+SCSI_TOID2+SCSI_TOID3)

#define  SCSI_XSIG            0x6A     /* M3 */
                                       /* expander signature          (rd/wr) */

#define  SCSI_SELOID          0x6B     /* M3 */
                                       /* select out id               (rd/wr) */
#define     SCSI_SELOID7      0x80     /* other scsi device id                */
#define     SCSI_SELOID6      0x40     /*                                     */
#define     SCSI_SELOID5      0x20     /*                                     */
#define     SCSI_SELOID4      0x10     /*                                     */
#define     SCSI_SELOID3      0x08     /*                                     */
#define     SCSI_SELOID2      0x04     /*                                     */
#define     SCSI_SELOID1      0x02     /*                                     */
#define     SCSI_SELOID0      0x01     /*                                     */
                                       /* other scsi device id mask           */
#define     SCSI_SELOIDS      (SCSI_SELOID0+SCSI_SELOID1+SCSI_SELOID2+\
                               SCSI_SELOID3+SCSI_SELOID4+SCSI_SELOID5+\
                               SCSI_SELOID6+SCSI_SELOID7)

#define  SCSI_FAIRNESS0       0x6C     /* M3 */
                                       /* arbitration fairness lo     (rd/wr) */
#define  SCSI_FAIRNESS1       0x6D     /* M3 */
                                       /* arbitration fairness hi     (rd/wr) */

#define  SCSI_UNFAIRNESS0     0x6E     /* M3 */
                                       /* arbitration unfairness lo   (rd/wr) */
#define  SCSI_UNFAIRNESS1     0x6F     /* M3 */
                                       /* arbitration unfairness hi   (rd/wr) */

#define  SCSI_MARGIN          0x6F     /* M(0,1) */
                                       /* margined DV control         (rd/wr) */
                                       /*     TBD                             */

/****************************************************************************
 PHASE LOCKED LOOP REGISTERS
****************************************************************************/

#define  SCSI_PLL960CTL0      0x68     /* M4 */
                                       /* 960MHz PLL control 0        (rd/wr) */
#define     SCSI_PLL960_VCOSEL 0x80    /*                                     */
#define     SCSI_PLL960_PWDN  0x40     /*                                     */
#define     SCSI_PLL960_NS1   0x20     /*                                     */
#define     SCSI_PLL960_NS0   0x10     /*                                     */
#define     SCSI_PLL960_ENLUD 0x08     /*                                     */
#define     SCSI_PLL960_ENLPF 0x04     /*                                     */
#define     SCSI_PLL960_DLPF  0x02     /*                                     */
#define     SCSI_PLL960_ENFBM 0x01     /*                                     */

#define  SCSI_PLL960CTL1      0x69     /* M4 */
                                       /* 960MHz PLL control 1        (rd/wr) */
#define     SCSI_PLL960_CNTEN 0x80     /* enable PLL960 count                 */
#define     SCSI_PLL960_CNTCLR 0x40    /* clear PLL960 count                  */
#define     SCSI_PLL960_RST   0x01     /* reset PLL960                        */

#define  SCSI_PLL960CNT0      0x6A     /* M4 */
                                       /* 960MHz PLL test count lo       (rd) */
#define  SCSI_PLL960CNT1      0x6B     /* M4 */
                                       /* 960MHz PLL test count hi       (rd) */

#define  SCSI_PLL400CTL0      0x6C     /* M4 */
                                       /* 400MHz PLL control 0        (rd/wr) */
#define     SCSI_PLL400_VCOSEL 0x80    /*                                     */
#define     SCSI_PLL400_PWDN  0x40     /*                                     */
#define     SCSI_PLL400_NS1   0x20     /*                                     */
#define     SCSI_PLL400_NS0   0x10     /*                                     */
#define     SCSI_PLL400_ENLUD 0x08     /*                                     */
#define     SCSI_PLL400_ENLPF 0x04     /*                                     */
#define     SCSI_PLL400_DLPF  0x02     /*                                     */
#define     SCSI_PLL400_ENFBM 0x01     /*                                     */

#define  SCSI_PLL400CTL1      0x6D     /* M4 */
                                       /* 400MHz PLL control 1        (rd/wr) */
#define     SCSI_PLL400_CNTEN 0x80     /* enable PLL400 count                 */
#define     SCSI_PLL400_CNTCLR 0x40    /* clear PLL400 count                  */
#define     SCSI_PLL400_RST   0x01     /* reset PLL400                        */

#define  SCSI_PLL400CNT0      0x6E     /* M4 */
                                       /* 400MHz PLL test count lo       (rd) */
#define  SCSI_PLL400CNT1      0x6F     /* M4 */
                                       /* 400MHz PLL test count hi       (rd) */

/****************************************************************************

 HOST REGISTERS - ARP DMA parameter registers
      Address Range: 0x70 - 0x7B

****************************************************************************/

#define  SCSI_DCHHADDR0       0x70     /* M0,M1 */
#define  SCSI_DCHHADDR1       0x71     /* data channel 0/1 host addr. (rd/wr) */
#define  SCSI_DCHHADDR2       0x72     /*                                     */
#define  SCSI_DCHHADDR3       0x73     /*                                     */
#define  SCSI_DCHHADDR4       0x74     /*                                     */
#define  SCSI_DCHHADDR5       0x75     /*                                     */
#define  SCSI_DCHHADDR6       0x76     /*                                     */
#define  SCSI_DCHHADDR7       0x77     /*                                     */

#define  SCSI_DCHHCNT0        0x78     /* M0,M1 */
#define  SCSI_DCHHCNT1        0x79     /* data channel 0/1 host count (rd/wr) */
#define  SCSI_DCHHCNT2        0x7A     /*                                     */
#if SCSI_DCH_U320_MODE
#define  SCSI_DCHHCNT3        0x7B     /* DCH_SCSI DFFnHCNT[31:0]             */
#endif /* SCSI_DCH_U320_MODE */

#define  SCSI_DCH0HADDR0      0x70     /* M0 */
#define  SCSI_DCH0HADDR1      0x71     /* data channel 0 host address (rd/wr) */
#define  SCSI_DCH0HADDR2      0x72     /*                                     */
#define  SCSI_DCH0HADDR3      0x73     /*                                     */
#define  SCSI_DCH0HADDR4      0x74     /*                                     */
#define  SCSI_DCH0HADDR5      0x75     /*                                     */
#define  SCSI_DCH0HADDR6      0x76     /*                                     */
#define  SCSI_DCH0HADDR7      0x77     /*                                     */

#define  SCSI_DCH0HCNT0       0x78     /* M0 */
#define  SCSI_DCH0HCNT1       0x79     /* data channel 0 host count   (rd/wr) */
#define  SCSI_DCH0HCNT2       0x7A     /*                                     */
#if SCSI_DCH_U320_MODE
#define  SCSI_DCH0HCNT3       0x7B     /* DCH_SCSI DFFnHCNT[31:0]             */
#endif /* SCSI_DCH_U320_MODE */

#define  SCSI_DCH1HADDR0      0x70     /* M1 */
#define  SCSI_DCH1HADDR1      0x71     /* data channel 1 host address (rd/wr) */
#define  SCSI_DCH1HADDR2      0x72     /*                                     */
#define  SCSI_DCH1HADDR3      0x73     /*                                     */
#define  SCSI_DCH1HADDR4      0x74     /*                                     */
#define  SCSI_DCH1HADDR5      0x75     /*                                     */
#define  SCSI_DCH1HADDR6      0x76     /*                                     */
#define  SCSI_DCH1HADDR7      0x77     /*                                     */

#define  SCSI_DCH1HCNT0       0x78     /* M0,M1 */
#define  SCSI_DCH1HCNT1       0x79     /* data channel 1 host count   (rd/wr) */
#define  SCSI_DCH1HCNT2       0x7A     /*                                     */

#define  SCSI_HODMAADR0       0x70     /* M3 */
#define  SCSI_HODMAADR1       0x71     /* host overlay DMA address    (rd/wr) */
#define  SCSI_HODMAADR2       0x72     /*                                     */
#define  SCSI_HODMAADR3       0x73     /*                                     */
#define  SCSI_HODMAADR4       0x74     /*                                     */
#define  SCSI_HODMAADR5       0x75     /*                                     */
#define  SCSI_HODMAADR6       0x76     /*                                     */
#define  SCSI_HODMAADR7       0x77     /*                                     */

#define  SCSI_HODMACNT0       0x78     /* M3 */
#define  SCSI_HODMACNT1       0x79     /* host overlay DMA count      (rd/wr) */

/****************************************************************************

 SCB/CMD REGISTERS - ARP DMA parameter registers
      Address Range: 0x7C - 0x84

****************************************************************************/

#define  SCSI_SGHADDR0        0x7C     /* M0,M1 */
#define  SCSI_SGHADDR1        0x7D     /* s/g host address            (rd/wr) */
#define  SCSI_SGHADDR2        0x7E     /*                                     */
#define  SCSI_SGHADDR3        0x7F     /*                                     */
#define  SCSI_SGHADDR4        0x80     /*                                     */
#define  SCSI_SGHADDR5        0x81     /*                                     */
#define  SCSI_SGHADDR6        0x82     /*                                     */
#define  SCSI_SGHADDR7        0x83     /*                                     */

#define  SCSI_SGHCNT          0x84     /* M0,M1 */
                                       /* s/g host count              (rd/wr) */

#define  SCSI_SCBHADDR0       0x7C     /* M2 */
#define  SCSI_SCBHADDR1       0x7D     /* scb host address            (rd/wr) */
#define  SCSI_SCBHADDR2       0x7E     /*                                     */
#define  SCSI_SCBHADDR3       0x7F     /*                                     */
#define  SCSI_SCBHADDR4       0x80     /*                                     */
#define  SCSI_SCBHADDR5       0x81     /*                                     */
#define  SCSI_SCBHADDR6       0x82     /*                                     */
#define  SCSI_SCBHADDR7       0x83     /*                                     */

#define  SCSI_SCBHCNT         0x84     /* M2 */
                                       /* scb host count              (rd/wr) */

#if SCSI_DCH_U320_MODE

#define  SCSI_SG_ASEL         0x88     /* M(0,1) */
#define     SCSI_SGASEL          0x30  /* ASEL bits for SG memory             */                                                                               

#define  SCSI_CMC_ASEL        0x88     /* M2 */
#define     SCSI_CMCASEL         0x30  /* ASEL bits for CMC memory            */

#define  SCSI_PLL_CTRL        0x89     /* M4 */

#define     SCSI_PLL960SWITCH    0x04  /* these fields are not to be altered */
#define     SCSI_PLL960BYPASS    0x02
#define     SCSI_PLL960ENABLE    0x01

#endif /* SCSI_DCH_U320_MODE */
#define  SCSI_DFF_THRSH       0x88     /* M4 */
                                       /* data fifo threshold control (rd/wr) */
#define     SCSI_WR_DFTHRSH2  0x40     /* write threshold                     */
#define     SCSI_WR_DFTHRSH1  0x20     /*                                     */
#define     SCSI_WR_DFTHRSH0  0x10     /*                                     */
#define     SCSI_WR_DFTHRSH   (SCSI_WR_DFTHRSH0+SCSI_WR_DFTHRSH1+\
                               SCSI_WR_DFTHRSH2)
#define     SCSI_RD_DFTHRSH2  0x04     /* read threshold                      */
#define     SCSI_RD_DFTHRSH1  0x02     /*                                     */
#define     SCSI_RD_DFTHRSH0  0x01     /*                                     */
#define     SCSI_RD_DFTHRSH   (SCSI_RD_DFTHRSH0+SCSI_RD_DFTHRSH1+\
                               SCSI_RD_DFTHRSH2)

                              /* write threshold default - 1024 bytes */
#define  SCSI_WR_DFT_DEFAULT  (SCSI_WR_DFTHRSH0+SCSI_WR_DFTHRSH1)

                              /* read threshold default  - 1024 bytes */
#define  SCSI_RD_DFT_DEFAULT  (SCSI_RD_DFTHRSH0+SCSI_RD_DFTHRSH1)


#if !SCSI_DCH_U320_MODE
/****************************************************************************

 SCB REGISTERS - CHIM/ARP FLASH access controller
      Address Range: 0x8A - 0x8E

****************************************************************************/

#define  SCSI_ROMADDR0        0x8A     /* M(0,1,2,3,4) */
                                       /* rom address 0               (rd/wr) */
#define  SCSI_ROMADDR1        0x8B     /* M(0,1,2,3,4) */
                                       /* rom address 1               (rd/wr) */
#define  SCSI_ROMADDR2        0x8C     /* M(0,1,2,3,4) */
                                       /* rom address 2               (rd/wr) */

#define  SCSI_ROMCNTRL        0x8D     /* M(0,1,2,3,4) */
                                       /* rom control                 (rd/wr) */
#define     SCSI_ROMOP2       0x80     /* ROM operation: 0-read byte(s),      */
#define     SCSI_ROMOP1       0x40     /* 1-read device id, 2-write byte mode,*/
#define     SCSI_ROMOP0       0x20     /* 3-chip erase, 4-write page bytes    */
#define     SCSI_ROMSPD1      0x10     /* ROM speed: 00 - 150ns, 01 - 120ns,  */
#define     SCSI_ROMSPD0      0x08     /* 10 - 90ns, and 11 - 70ns            */
#define     SCSI_REPEAT       0x02     /* repeat seletect ROM operation       */
#define     SCSI_RDY          0x01     /* operation is ready to start         */

#define  SCSI_ROMDAT          0x8E     /* M(0,1,2,3,4) */
                                       /* rom data                    (rd/wr) */

#else
/* DCH_SCSI Host Timers and Controls */
   /* this timer exist in rbiclk domain */
#define  SCSI_HST_TIMER_CTRL  0x8B     /* All Modes */
                                       /* HST Timer Control - DCH_SCSI */
#define     SCSI_HST_TIMERINT    0x80  /* Interupt mask (TBD)                 */
#define     SCSI_HST_TIMERINTEN  0x40  /* Interupt mask (TBD)                 */
#define     SCSI_HST_TIMERPAUSE  0x20  /* Interupt mask (TBD)                 */
#define     SCSI_HST_TIMERSTART  0x08  /* Start bit for timer                 */
#define     SCSI_HST_TIMERSTOP   0x04  /* Stop bit for timer                  */
#define     SCSI_HST_TIMERRUN    0x02  /* Timer running                       */
#define     SCSI_HST_TIMERDONE   0x01  /* Timer stopped                       */

#define  SCSI_HST_TIMER_TICK  0x8C     /* All Modes - HST timer TICK. This    */
                                       /* is a 16-bit register. Each count is */
                                       /* 7.5 ns. D05 = 25usec                */
#define  SCSI_HST_TIMER_DEFAULT 0x0D05 /* Timer default is set at 25usec per  */
                                       /* tick (0xD05).                       */
                 
#define  SCSI_HST_TIMER_COUNT 0x8E     /* All Modes - HST timer count. This   */
                                       /* is a 16 bit reg. It should be       */
                                       /* initialized with the desired number */
                                       /* of ticks required for the timer's   */
                                       /* purpose.                            */ 

#endif /* !SCSI_DCH_U320_MODE */
/****************************************************************************

 HOST REGISTERS - CHIM/ARP PCI-X Split Status
      Address Range: 0x90 - 0x9F

****************************************************************************/

#if !SCSI_DCH_U320_MODE
#define  SCSI_DCHRXMSG0       0x90     /* M0,M1 */
                                       /* data chnl 0/1 receive message 0 (rd)*/
#define  SCSI_DCH0RXMSG0      0x90     /* M0 */
                                       /* data chnl 0 receive message 0  (rd) */
#define  SCSI_DCH1RXMSG0      0x90     /* M1 */
                                       /* data chnl 1 receive message 0  (rd) */
#define  SCSI_CMCRXMSG0       0x90     /* M2 */
                                       /* cmd channel receive message 0  (rd) */
#define  SCSI_OVLYRXMSG0      0x90     /* M3 */
                                       /* over lay receive message 0     (rd) */
#define  SCSI_SG01RXMSG0      0x98     /* M0,M1 */
                                       /* s/g receive message 0          (rd) */
#define     SCSI_CDNUM4       0x80     /* completer device number 4-0         */
#define     SCSI_CDNUM3       0x40     /*                                     */
#define     SCSI_CDNUM2       0x20     /*                                     */
#define     SCSI_CDNUM1       0x10     /*                                     */
#define     SCSI_CDNUM0       0x08     /*                                     */
#define     SCSI_CFNUM2       0x04     /* completer function number 2-0       */
#define     SCSI_CFNUM1       0x02     /*                                     */
#define     SCSI_CFNUM0       0x01     /*                                     */
#define     SCSI_CDNUM        (SCSI_CDNUM0+SCSI_CDNUM1+SCSI_CDNUM2+\
                               SCSI_CDNUM3+SCSI_CDNUM4)
#define     SCSI_CFNUM        (SCSI_CFNUM0+SCSI_CFNUM1+SCSI_CFNUM2)

#define  SCSI_ROENABL         0x90     /* M4 */
                                       /* relaxed order enable        (rd/wr) */
#define     SCSI_MSIROEN      0x20     /* MSI relaxed order enable            */
#define     SCSI_OVLYROEN     0x10     /* over lay relaxed order enable       */
#define     SCSI_CMCROEN      0x08     /* cmd channel relaxed order enable    */
#define     SCSI_SG01ROEN     0x04     /* s/g 0, 1 relaxed order enable       */
#define     SCSI_DCH1ROEN     0x02     /* data channel 1 relaxed order enable */
#define     SCSI_DCH0ROEN     0x01     /* data channel 0 relaxed order enable */

#define  SCSI_DCHRXMSG1       0x91     /* M0,M1 */
                                       /* data chnl 0/1 receive message 1 (rd)*/
#define  SCSI_DCH0RXMSG1      0x91     /* M0 */
                                       /* data chnl 0 receive message 1  (rd) */
#define  SCSI_DCH1RXMSG1      0x91     /* M1 */
                                       /* data chnl 1 receive message 1  (rd) */
#define  SCSI_CMCRXMSG1       0x91     /* M2 */
                                       /* cmd channel receive message 1  (rd) */
#define  SCSI_OVLYRXMSG1      0x91     /* M3 */
                                       /* over lay receive message 1     (rd) */
#define  SCSI_SG01RXMSG1      0x99     /* M0,M1 */
                                       /* s/g receive message 1          (rd) */
#define     SCSI_CBNUM7       0x80     /* completer bus number 7-0            */
#define     SCSI_CBNUM6       0x40     /*                                     */
#define     SCSI_CBNUM5       0x20     /*                                     */
#define     SCSI_CBNUM4       0x10     /*                                     */
#define     SCSI_CBNUM3       0x08     /*                                     */
#define     SCSI_CBNUM2       0x04     /*                                     */
#define     SCSI_CBNUM1       0x02     /*                                     */
#define     SCSI_CBNUM0       0x01     /*                                     */
#define     SCSI_CBNUM        (SCSI_CBNUM0+SCSI_CBNUM1+SCSI_CBNUM2+\
                               SCSI_CBNUM3+SCSI_CBNUM4+SCSI_CBNUM5+\
                               SCSI_CBNUM6+SCSI_CBNUM7)

#define  SCSI_NSENABLE        0x91     /* M4 */
                                       /* no snoop enable             (rd/wr) */
#define     SCSI_MSINSEN      0x20     /* MSI no snoop enable                 */
#define     SCSI_OVLYNSEN     0x10     /* over lay no snoop enable            */
#define     SCSI_CMCNSEN      0x08     /* command channel no snoop enable     */
#define     SCSI_SG01NSEN     0x04     /* scatter gather 0, 1 no snoop enable */
#define     SCSI_DCH1NSEN     0x02     /* data channel 1 no snoop enable      */
#define     SCSI_DCH0NSEN     0x01     /* data channel 0 no snoop enable      */

#define  SCSI_DCHRXMSG2       0x92     /* M0,M1 */
                                       /* data chnl 0/1 receive message 2 (rd)*/
#define  SCSI_DCH0RXMSG2      0x92     /* M0 */
                                       /* data chnl 0 receive message 2  (rd) */
#define  SCSI_DCH1RXMSG2      0x92     /* M1 */
                                       /* data chnl 1 receive message 2  (rd) */
#define  SCSI_CMCRXMSG2       0x92     /* M2 */
                                       /* cmd channel receive message 2  (rd) */
#define  SCSI_OVLYRXMSG2      0x92     /* M3 */
                                       /* over lay receive message 2     (rd) */
#define  SCSI_SG01RXMSG2      0x9A     /* M0,M1 */
                                       /* s/g receive message 2          (rd) */
#define     SCSI_MINDEX7      0x80     /* message index 7-0                   */
#define     SCSI_MINDEX6      0x40     /*                                     */
#define     SCSI_MINDEX5      0x20     /*                                     */
#define     SCSI_MINDEX4      0x10     /*                                     */
#define     SCSI_MINDEX3      0x08     /*                                     */
#define     SCSI_MINDEX2      0x04     /*                                     */
#define     SCSI_MINDEX1      0x02     /*                                     */
#define     SCSI_MINDEX0      0x01     /*                                     */
#define     SCSI_MINDEX       (SCSI_MINDEX0+SCSI_MINDEX1+SCSI_MINDEX2+\
                               SCSI_MINDEX3+SCSI_MINDEX4+SCSI_MINDEX5+\
                               SCSI_MINDEX6+SCSI_MINDEX7)

#define  SCSI_OST             0x92     /* M4 */
                                       /* outstanding split transaction(rd/wr)*/
#define     SCSI_OSTCNT2      0x04     /* OST count                           */
#define     SCSI_OSTCNT1      0x02     /*                                     */
#define     SCSI_OSTCNT0      0x01     /*                                     */
#define     SCSI_OSTCNT       (SCSI_OSTCNT0+SCSI_OSTCNT1+SCSI_OSTCNT2)

#define  SCSI_DCHRXMSG3       0x93     /* M0,M1 */
                                       /* data chnl 0/1 receive message 3 (rd)*/
#define  SCSI_DCH0RXMSG3      0x93     /* M0 */
                                       /* data chnl 0 receive message 3  (rd) */
#define  SCSI_DCH1RXMSG3      0x93     /* M1 */
                                       /* data chnl 1 receive message 3  (rd) */
#define  SCSI_CMCRXMSG3       0x93     /* M2 */
                                       /* cmd channel receive message 3  (rd) */
#define  SCSI_OVLYRXMSG3      0x93     /* M3 */
                                       /* over lay receive message 3     (rd) */
#define  SCSI_SG01RXMSG3      0x9B     /* M0,M1 */
                                       /* s/g receive message 3          (rd) */
#define     SCSI_MCCLASS3     0x08     /* message class 3-0                   */
#define     SCSI_MCCLASS2     0x04     /*                                     */
#define     SCSI_MCCLASS1     0x02     /*                                     */
#define     SCSI_MCCLASS0     0x01     /*                                     */
#define     SCSI_MCCLASS      (SCSI_MCCLASS0+SCSI_MCCLASS1+\
                               SCSI_MCCLASS2+SCSI_MCCLASS3)

#define  SCSI_PCIXCTL         0x93     /* M4 */
                                       /* pci-x control               (rd/wr) */
#define     SCSI_SERRPULSE    0x80     /* output SERR pulse in PCI-X mode     */
#define     SCSI_UNEXPSCIEN   0x20     /* unexpected split cmplt interrupt enb*/
#define     SCSI_SPLTSMADIS   0x10     /* split cmplt signal master abort dis */
#define     SCSI_SPLTSTADIS   0x08     /* split cmplt signal target abort dis */
#define     SCSI_SRSPDPEEN    0x04     /* split response data parity err enb  */
#define     SCSI_TSCSERREN    0x02     /* target split completion SERR# enb   */
#define     SCSI_CMPABCDIS    0x01     /* compare address byte count disable  */


#define  SCSI_DCHSEQBCNT0     0x94     /* M0,M1 */
                                       /* data chnl 0/1 sequence byte cnt (rd)*/
#define  SCSI_DCH0SEQBCNT0    0x94     /* M0 */
                                       /* data chnl 0 sequence byte count (rd)*/
#define  SCSI_DCH1SEQBCNT0    0x94     /* M1 */
                                       /* data chnl 1 sequence byte count (rd)*/
#define     SCSI_SEQBCNT07    0x80     /* sequence byte count 00-07           */
#define     SCSI_SEQBCNT06    0x40     /*                                     */
#define     SCSI_SEQBCNT05    0x20     /*                                     */
#define     SCSI_SEQBCNT04    0x10     /*                                     */
#define     SCSI_SEQBCNT03    0x08     /*                                     */
#define     SCSI_SEQBCNT02    0x04     /*                                     */
#define     SCSI_SEQBCNT01    0x02     /*                                     */
#define     SCSI_SEQBCNT00    0x01     /*                                     */
#define     SCSI_SEQBCNTLO    (SCSI_SEQBCNT00+SCSI_SEQBCNT01+SCSI_SEQBCNT02+\
                               SCSI_SEQBCNT03+SCSI_SEQBCNT04+SCSI_SEQBCNT05+\
                               SCSI_SEQBCNT06+SCSI_SEQBCNT07)

#define  SCSI_DCHSEQBCNT1     0x95     /* M0,M1 */
                                       /* data chnl 0/1 sequence byte cnt (rd)*/
#define  SCSI_DCH0SEQBCNT1    0x95     /* M0 */
                                       /* data chnl 0 sequence byte count (rd)*/
#define  SCSI_DCH1SEQBCNT1    0x95     /* M1 */
                                       /* data chnl 1 sequence byte count (rd)*/
#define     SCSI_SEQBCNT10    0x04     /* sequence byte count 08-10           */
#define     SCSI_SEQBCNT09    0x02     /*                                     */
#define     SCSI_SEQBCNT08    0x01     /*                                     */
#define     SCSI_SEQBCNTHI    (SCSI_SEQBCNT08+SCSI_SEQBCNT09+SCSI_SEQBCNT10)

#define  SCSI_CMCSEQBCNT      0x94     /* M2 */
                                       /* cmd channel sequence byte count (rd)*/
#define  SCSI_OVLYSEQBCNT     0x94     /* M3 */
                                       /* over lay sequence byte count   (rd) */
#define  SCSI_SG01SEQBCNT     0x9C     /* M0,M1 */
                                       /* s/g channel sequence byte count (rd)*/
#define     SCSI_SEQBCNT7     0x80     /* sequence byte count                 */
#define     SCSI_SEQBCNT6     0x40     /*                                     */
#define     SCSI_SEQBCNT5     0x20     /*                                     */
#define     SCSI_SEQBCNT4     0x10     /*                                     */
#define     SCSI_SEQBCNT3     0x08     /*                                     */
#define     SCSI_SEQBCNT2     0x04     /*                                     */
#define     SCSI_SEQBCNT1     0x02     /*                                     */
#define     SCSI_SEQBCNT0     0x01     /*                                     */
                                       /* over lay sequence byte count 0-5    */
#define     SCSI_OVLYSEQBCNTS (SCSI_SEQBCNT0+SCSI_SEQBCNT1+SCSI_SEQBCNT2+\
                               SCSI_SEQBCNT3+SCSI_SEQBCNT4+SCSI_SEQBCNT5)
                                       /* cmc sequence byte count 0-6         */
#define     SCSI_CMCSEQBCNTS  (SCSI_SEQBCNT0+SCSI_SEQBCNT1+SCSI_SEQBCNT2+\
                               SCSI_SEQBCNT3+SCSI_SEQBCNT4+SCSI_SEQBCNT5+\
                               SCSI_SEQBCNT6)
                                       /* s/g sequence byte count 0-7         */
#define     SCSI_SG01SEQBCNTS (SCSI_SEQBCNT0+SCSI_SEQBCNT1+SCSI_SEQBCNT2+\
                               SCSI_SEQBCNT3+SCSI_SEQBCNT4+SCSI_SEQBCNT5+\
                               SCSI_SEQBCNT6+SCSI_SEQBCNT7)

#define  SCSI_DCHSPLTSTAT0    0x96     /* M0,M1 */
                                       /* data chnl 0/1 split status 0 (rd/wr)*/
#define  SCSI_DCH0SPLTSTAT0   0x96     /* M0 */
                                       /* data chnl 0 split status 0  (rd/wr) */
#define  SCSI_DCH1SPLTSTAT0   0x96     /* M1 */
                                       /* data chnl 1 split status 0  (rd/wr) */
#define  SCSI_CMCSPLTSTAT0    0x96     /* M2 */
                                       /* cmd channel split status 0  (rd/wr) */
#define  SCSI_OVLYSPLTSTAT0   0x96     /* M3 */
                                       /* over lay split status 0     (rd/wr) */
#define  SCSI_SG01SPLTSTAT0   0x9E     /* M0,M1 */
                                       /* s/g channel split status 0  (rd/wr) */
#define     SCSI_STAETERM     0x80     /* single targ-abort early termination */
#define     SCSI_SCBCERR      0x40     /* split completion byte count error   */
#define     SCSI_SCADERR      0x20     /* split completion address error      */
#define     SCSI_SCDATBUCKET  0x10     /* split completion data bucket        */
#define     SCSI_CNTNOTCMPLT  0x08     /* count not complete                  */
#define     SCSI_RXOVRUN      0x04     /* receive over run                    */
#define     SCSI_RXSCEMSG     0x02     /* received split completion error msg */
#define     SCSI_RXSPLTRSP    0x01     /* receive split response              */
#define     SCSI_PCIX_SPLIT_ERRORS  (SCSI_SCBCERR+SCSI_SCADERR+SCSI_RXOVRUN+ \
                                     SCSI_RXSCEMSG)

#define  SCSI_DCHSPLTSTAT1    0x97     /* M0,M1 */
                                       /* data chnl 0/1 split status 1 (rd/wr)*/
#define  SCSI_DCH0SPLTSTAT1   0x97     /* M0 */
                                       /* data chnl 0 split status 1  (rd/wr) */
#define  SCSI_DCH1SPLTSTAT1   0x97     /* M1 */
                                       /* data chnl 1 split status 1  (rd/wr) */
#define  SCSI_CMCSPLTSTAT1    0x97     /* M2 */
                                       /* cmd channel split status 1  (rd/wr) */
#define  SCSI_OVLYSPLTSTAT1   0x97     /* M3 */
                                       /* over lay split status 1     (rd/wr) */
#define  SCSI_SG01SPLTSTAT1   0x9F     /* M0,M1 */
                                       /* s/g channel split status 1  (rd/wr) */
#define     SCSI_RXDATABUCKET 0x01     /* receive data bucket                 */

#define  SCSI_SLVSPLTOUTADR0  0x98     /* M3 */
                                       /* split cmplt out cycle addr 0   (rd) */
#define     SCSI_LOWER_ADDR6  0x40     /*                                     */
#define     SCSI_LOWER_ADDR5  0x20     /*                                     */
#define     SCSI_LOWER_ADDR4  0x10     /*                                     */
#define     SCSI_LOWER_ADDR3  0x08     /*                                     */
#define     SCSI_LOWER_ADDR2  0x04     /*                                     */
#define     SCSI_LOWER_ADDR1  0x02     /*                                     */
#define     SCSI_LOWER_ADDR0  0x01     /*                                     */

#define  SCSI_SLVSPLTOUTADR1  0x99     /* M3 */
                                       /* split cmplt out cycle addr 1   (rd) */
#define     SCSI_REQ_DNUM4    0x80     /*                                     */
#define     SCSI_REQ_DNUM3    0x40     /*                                     */
#define     SCSI_REQ_DNUM2    0x20     /*                                     */
#define     SCSI_REQ_DNUM1    0x10     /*                                     */
#define     SCSI_REQ_DNUM0    0x08     /*                                     */
#define     SCSI_REQ_FNUM2    0x04     /*                                     */
#define     SCSI_REQ_FNUM1    0x02     /*                                     */
#define     SCSI_REQ_FNUM0    0x01     /*                                     */

#define  SCSI_SLVSPLTOUTADR2  0x9A     /* M3 */
                                       /* split cmplt out cycle addr 2   (rd) */
#define     SCSI_REQ_BNUM7    0x80     /*                                     */
#define     SCSI_REQ_BNUM6    0x40     /*                                     */
#define     SCSI_REQ_BNUM5    0x20     /*                                     */
#define     SCSI_REQ_BNUM4    0x10     /*                                     */
#define     SCSI_REQ_BNUM3    0x08     /*                                     */
#define     SCSI_REQ_BNUM2    0x04     /*                                     */
#define     SCSI_REQ_BNUM1    0x02     /*                                     */
#define     SCSI_REQ_BNUM0    0x01     /*                                     */

#define  SCSI_SLVSPLTOUTADR3  0x9B     /* M3 */
                                       /* split cmplt out cycle addr 3   (rd) */
#define     SCSI_TAG4         0x10     /*                                     */
#define     SCSI_TAG3         0x08     /*                                     */
#define     SCSI_TAG2         0x04     /*                                     */
#define     SCSI_TAG1         0x02     /*                                     */
#define     SCSI_TAG0         0x01     /*                                     */

#define  SCSI_SLVSPLTOUTATTR0 0x9C     /* M3 */
                                       /* split cmplt out cycle addr 0   (rd) */
#define     SCSI_LOWER_BCNT7  0x80     /*                                     */
#define     SCSI_LOWER_BCNT6  0x40     /*                                     */
#define     SCSI_LOWER_BCNT5  0x20     /*                                     */
#define     SCSI_LOWER_BCNT4  0x10     /*                                     */
#define     SCSI_LOWER_BCNT3  0x08     /*                                     */
#define     SCSI_LOWER_BCNT2  0x04     /*                                     */
#define     SCSI_LOWER_BCNT1  0x02     /*                                     */
#define     SCSI_LOWER_BCNT0  0x01     /*                                     */

#define  SCSI_SLVSPLTOUTATTR1 0x9D     /* M3 */
                                       /* split cmplt out cycle addr 1   (rd) */
#define     SCSI_CMPLT_DNUM4  0x80     /*                                     */
#define     SCSI_CMPLT_DNUM3  0x40     /*                                     */
#define     SCSI_CMPLT_DNUM2  0x20     /*                                     */
#define     SCSI_CMPLT_DNUM1  0x10     /*                                     */
#define     SCSI_CMPLT_DNUM0  0x08     /*                                     */
#define     SCSI_CMPLT_FNUM2  0x04     /*                                     */
#define     SCSI_CMPLT_FNUM1  0x02     /*                                     */
#define     SCSI_CMPLT_FNUM0  0x01     /*                                     */

#define  SCSI_SLVSPLTOUTATTR2 0x9E     /* M3 */
                                       /* split cmplt out cycle addr 2   (rd) */
#define     SCSI_CMPLT_BNUM7  0x80     /*                                     */
#define     SCSI_CMPLT_BNUM6  0x40     /*                                     */
#define     SCSI_CMPLT_BNUM5  0x20     /*                                     */
#define     SCSI_CMPLT_BNUM4  0x10     /*                                     */
#define     SCSI_CMPLT_BNUM3  0x08     /*                                     */
#define     SCSI_CMPLT_BNUM2  0x04     /*                                     */
#define     SCSI_CMPLT_BNUM1  0x02     /*                                     */
#define     SCSI_CMPLT_BNUM0  0x01     /*                                     */

#define  SCSI_SLVSPLTOUTATTR3 0x9F     /* M3 */
                                       /* split cmplt out cycle addr 3   (rd) */
#define     SCSI_BCM          0x80     /*                                     */
#define     SCSI_SCE          0x40     /*                                     */
#define     SCSI_SCM          0x20     /*                                     */

#define  SCSI_SFUNCTION       0x9F     /* M4 */
                                       /* special function            (rd/wr) */
#define     SCSI_GROUP3       0x80     /* group 0-3, select internal function */
#define     SCSI_GROUP2       0x40     /* for special operation               */
#define     SCSI_GROUP1       0x20     /*                                     */ 
#define     SCSI_GROUP0       0x10     /*                                     */ 
#define     SCSI_TEST3        0x08     /* test 0-3, selects internal function */
#define     SCSI_TEST2        0x04     /* specific tests                      */
#define     SCSI_TEST1        0x02     /*                                     */ 
#define     SCSI_TEST0        0x01     /*                                     */ 
#define     SCSI_GROUP        (SCSI_GROUP0+SCSI_GROUP1+SCSI_GROUP2+SCSI_GROUP3)
#define     SCSI_TEST         (SCSI_TEST0+SCSI_TEST1+SCSI_TEST2+SCSI_TEST3)

/****************************************************************************

 SCB REGISTERS  - 8 bytes of mode dependent SCRATCH memory
 HOST REGISTERS - ARP PCI bus status by DMA channel and slave
      Address Range: 0xA0 - 0xA7

****************************************************************************/

#define  SCSI_SCRATCH2        0xA0     /* M0,M1,M2,M3 */
                                       /* mode dependent scratch RAM  (rd/wr) */

#define  SCSI_SCRATCH2_SIZE   8        /* scratch 2 size                      */

#define  SCSI_PCISTATDF0      0xA0     /* M4 */
                                     
#define  SCSI_PCISTATDF1      0xA1     /* M4 */
                                     
#define  SCSI_PCISTATSG       0xA2     /* M4 */

#define  SCSI_PCISTATCMC      0xA3     /* M4 */

#define  SCSI_PCISTATOVLY     0xA4     /* M4 */
                    
#define  SCSI_PCISTATMSI      0xA6     /* M4 */

#define  SCSI_PCISTATTARG     0xA7     /* M4 */
                                       
#define     SCSI_PCI_DATPERR    0x80     /* data parity error detected          */
#define     SCSI_PCI_SGNLSYSERR 0x40     /* signaled system error detected      */
#define     SCSI_PCI_RCVDMABRT  0x20     /* received master abort               */
#define     SCSI_PCI_RCVDTABRT  0x10     /* received target abort               */
#define     SCSI_PCI_SCAAPERR   0x08     /* splt cmplt addr & attrib parity err */
#define     SCSI_STA_TARG       0x08     /* signal target abort                 */
#define     SCSI_PCI_RDPERR     0x04     /* read data parity err of splt cmplt  */
#define     SCSI_PCI_TWATERR    0x02     /* target intial event wait state err  */
#define     SCSI_PCI_DATPRPRTD  0x01     /* data parity erro reported           */
#define     SCSI_PCI_MASTER_ERRORS  (SCSI_PCI_DATPERR+SCSI_PCI_DATPRPRTD+    \
                                     SCSI_PCI_SGNLSYSERR+SCSI_PCI_RCVDMABRT+ \
                                     SCSI_PCI_RCVDTABRT)  
#define     SCSI_PCI_TARGET_ERRORS  (SCSI_STA_TARG+SCSI_PCI_SGNLSYSERR+ \
                                     SCSI_PCI_SGNLSYSERR)

#else

#define  SCSI_SCRATCH2        0x1C0    /* M0,M1,M2,M3 */
                                       /* mode dependent scratch RAM  (rd/wr) */
#define  SCSI_SCRATCH2_SIZE   128       /* scratch 2 size                      */

/* The DCH_SCSI core contains a vendor id, device id and revision level         */

#define  SCSI_DCHVENDORID     0x90     /* M4 This is a word wide register that
                                          contains the Vendor ID for the DCH 
                                          SCSI core (90-91) */
#define  SCSI_DCHDEVICEID     0x92     /* M4 - This is a word wide register 
                                         that contains the Device ID for the 
                                         DCH SCSI core (92-93) */

#define  SCSI_DCHREVISION     0x94     /* M4 - This contains the DCH_SCSI core
                                          revision level */
                                          
/* this timer exist in dchclk domain */
   
#define  SCSI_SEQ_TIMER_TICK  0xA0     /* Mode 4 - Seq timer TICK count. This */
                                       /* is a 16-bit register with a default */
                                       /* of 25usec per tick (0xD05). This    */
                                       /* may be changed based on needs       */
                 
#define  SCSI_SEQ_TIMER_COUNT 0xA2     /* Mode 4 - Seq timer count. This is a */
                                       /* 16- register. It should be loaded   */
                                       /* with the desired number of ticks    */
                                       /* required for the timer purpose.     */ 
                                       
#define  SCSI_SEQ_TIMER_CTRL  0xA4     /* Mode 4 Seq Timer Control - DCH_SCSI */

#define     SCSI_SEQ_TIMERINT    0x80  /* timer interrupt                     */
#define     SCSI_SEQ_TIMERINTEN  0x40  /* timer interrupt enable              */
#define     SCSI_SEQ_TIMERPAUSE  0x20  /* timer paused                        */
#define     SCSI_SEQ_TIMERSTART  0x08  /* Start bit for timer                 */
#define     SCSI_SEQ_TIMERSTOP   0x04  /* Stop bit for timer                  */
#define     SCSI_SEQ_TIMERRUN    0x02  /* Timer running                       */
#define     SCSI_SEQ_TIMERDONE   0x01  /* Timer completed (done)              */


#define  SCSI_CMCBUFADR       0xAA     /* M2 */
#define  SCSI_CMCHSTADR       0xAB     /* M2 */

#endif /* SCSI_DCH_U320_MODE */
/****************************************************************************

 SCB REGISTERS - ARP SCB pointer registers
      Address Range: 0xA8 - 0xA9

****************************************************************************/

#define  SCSI_SCBPTR          0xA8     /* M0,M1,M3 */
                                       /* scb pointer                 (rd/wr) */
#define  SCSI_M0SCBPTR        0xA8     /* M0 */
                                       /* mode 0 scb pointer          (rd/wr) */
#define  SCSI_M1SCBPTR        0xA8     /* M1 */
                                       /* mode 1 scb pointer          (rd/wr) */
#define  SCSI_M3SCBPTR        0xA8     /* M3 */
                                       /* mode 3 scb pointer          (rd/wr) */

#define  SCSI_CCSCBPTR        0xA8     /* M2 */
                                       /* command channel SCB pointer (rd/wr) */
#define  SCSI_M2SCBPTR        0xA8     /* M2 */
                                       /* mode 2 SCB pointer          (rd/wr) */

/****************************************************************************

 SCB/CMC REGISTERS - ARP DMA control
      Address Range: 0xAA - 0xAE

****************************************************************************/

#define  SCSI_SCBAUTOPTR      0xAB     /* M4 */
                                       /* auto scb pointer            (rd/wr) */
#define     SCSI_AUSCBPTR_EN  0x80     /* enable snoop of SCBPTR during DMA   */

#if !SCSI_DCH_U320_MODE
#define  SCSI_CCSGADR         0xAC     /* M(0,1) */
                                       /* cmc S/G ram address pointer (rd/wr) */
#else                                       
#define  SCSI_SGBUFADR        0xAC     /* M(0,1) */
                                       /* cmc S/G ram address pointer (rd/wr) */
                                       
#define  SCSI_SGMODE       0xAC        /* M4  16 bits (AC-AD)                 */
                                       /* SG mode select                      */
#define     SCSI_DISABLEAND   0x1000   /* Disable SGBuf address               */
#define     SCSI_AUTOPRELOAD  0x0800   /* Automatic SG segment preload        */
#define     SCSI_AUTOINDEX    0x0400   /* Automatic SG list ptr indexing      */
#define     SCSI_SGTHRSH64    0x0000   /* SG fetch threshold 64 bytes         */
#define     SCSI_SGTHRSH128   0x0100   /* SG fetch threshold 128 bytes        */
#define     SCSI_SGTHRSH256   0x0200   /* SG fetch threshold 256 bytes        */
#define     SCSI_DYNASEL      0x0080   /* Dynamic address select              */
#define     SCSI_SGBUFSIZE    0x0040   /* SG buffer size                      */
#define     SCSI_SGCACHESIZE1 0x0020   /* SG cache size                       */

#define     SCSI_SGCACHESIZE0 0x0010   /* 00 = 64 bytes,                      */
                                       /* 01 = 128 bytes,                     */
                                       /* 10 = 256 bytes                      */
                                       /* 11 = reserved                       */
                                       
#define     SCSI_SGFORMAT1    0x0008   /* SG buffer size                      */
#define     SCSI_SGFORMAT0    0x0004   /* 00 = Common SG                      */
                                       /* 01 = Legacy 16 bytes                */         
                                       /* 10 = Legacy 8 bytes                 */         
                                       /* 11 = reserved                       */         
#define     SCSI_SGAUTOSUB    0x0002   /* Auto fetch next sub list            */
#define     SCSI_SGAUTOFETCH  0x0001   /* Auto next SG list                   */
#define     SCSI_SGMODE_MASK  0x0000   /* tbd */
                                      
#define  SCSI_CMC_RAMBIST     0xAE     /* M4 */
#define     SCSI_CMCBIST_FAIL    0x40  /* CMC Buffer Ram BIST fail           */ 
#define     SCSI_CMCBIST_DONE    0x20  /* CMC Buffer Ram BIST done           */ 
#define     SCSI_CMCBISTEN       0x10  /* CMC Buffer Ram BIST enable         */ 
#define     SCSI_SGRAMBIST_FAIL  0x04  /* SG Ram BIST fail                   */ 
#define     SCSI_SGRAMBIST_DONE  0x02  /* SG Ram BIST done                   */ 
#define     SCSI_SGRAMBISTEN     0x01  /* SG Ram BIST enable                 */ 
                         
#endif /* !SCSI_DCH_U320_MODE */

#define  SCSI_CCSGCTL         0xAD     /* M(0,1) */
                                       /* command channel S/G control (rd/wr) */
#define     SCSI_CCSGDONE     0x80     /* command channel s/g prefetch done   */
#if SCSI_DCH_U320_MODE
#define     SCSI_SGEMPTY      0x40     /* s/g empty                           */
#define     SCSI_SGACTIVE     0x20     /* s/g active                          */
#endif /* SCSI_DCH_U320_MODE */
#define     SCSI_SG_CACHE_AVAIL 0x10   /* s/g cache available                 */
#define     SCSI_CCSGEN       0x08     /* command channel s/g xfer enable     */
#define     SCSI_CCSGENACK    0x08     /* command channel s/g xfer acknowledge*/
#if !SCSI_DCH_U320_MODE
#define     SCSI_SG_FETCH_REQ 0x02     /* s/g fetch request                   */
#else  /* SCSI DCH U320 */

#define     SCSI_RSTSGEN      0x04     /* reset request for SG DMA       (wr) */
#define     SCSI_SGENACK      0x04     /* SG DMA enabled status          (rd) */
#define     SCSI_RSTCMPLT     0x02     /* Force SG complete              (wr) */
#define     SCSI_SGCMPLT      0x02     /* Indicates SG element loaded has the */
                                       /* 'LL' bit set.                  (rd) */
#define     SCSI_CCSGRESETREQ 0x01     /* incidates SG DMA is requesting a    */
                                       /* transfer from the host              */
#endif /* !SCSI_DCH_U320_MODE */
#define     SCSI_CCSGRESET    0x01     /* command channel s/g reset           */

#define  SCSI_CCSGRAM         0xB0     /* M(0,1) */
                                       /* cmc S/G ram data port       (rd/wr) */

#if !SCSI_DCH_U320_MODE
#define  SCSI_CCSCBACNT       0xAB     /* M2 */
                                       /* cmc SCB array count         (rd/wr) */
#endif /* !SCSI_DCH_U320_MODE */

#define  SCSI_CCSCBADR        0xAC     /* M2 */
                                       /* cmc SCB ram address pointer (rd/wr) */

#define  SCSI_CCSCBCTL        0xAD     /* M2 */
                                       /* command channel SCB control (rd/wr) */
#define     SCSI_CCSCBDONE    0x80     /* scb command channel done            */        
#define     SCSI_ARRDONE      0x40     /* scb array prefetch done             */
#define     SCSI_CCARREN      0x10     /* scb array dma enable                */
#define     SCSI_CCARRENACK   0x10     /* scb array dma enable acknowledge    */
#define     SCSI_CCSCBEN      0x08     /* cmd channel scb ram xfer enable     */
#define     SCSI_CCSCBENACK   0x08     /* cmd channel scb ram xfer enable ack */
#define     SCSI_CCSCBDIR     0x04     /* command channel scb SRAM direction  */
#define     SCSI_CCSCBDIRACK  0x04     /* cmd channel scb SRAM direction ack  */
#define     SCSI_CCSCBRESET   0x01     /* command channel scb SRAM reset      */

#if !SCSI_DCH_U320_MODE
#define  SCSI_CMC_RAMBIST      0xAD     /* M4 */
#define     SCSI_SG_ELEMENT_SIZE 0x80  /* sg element size                     */ 

#define  SCSI_CCSCBRAM        0xB0     /* M2 */
                                       /* cmc SCB ram data port       (rd/wr) */


/****************************************************************************

 SCB REGISTERS - ARP Flex port/SEEPROM port and control
      Address Range: 0xB0 - 0xBF

****************************************************************************/

#define  SCSI_FLEXADR         0xB0     /* M3 */
                                       /* FLEXDMA address             (rd/wr) */
#define  SCSI_FLEXADR0        0xB0     /* M3 */
                                       /* FLEXDMA address 0           (rd/wr) */
#define  SCSI_FLEXADR1        0xB1     /* M3 */
                                       /* FLEXDMA address 1           (rd/wr) */
#define  SCSI_FLEXADR2        0xB2     /* M3 */
                                       /* FLEXDMA address 2           (rd/wr) */

#define  SCSI_FLEXCNT         0xB3     /* M3 */
                                       /* FLEXDMA byte count          (rd/wr) */
#define  SCSI_FLEXCNT0        0xB3     /* M3 */
                                       /* FLEXDMA byte count 0        (rd/wr) */
#define  SCSI_FLEXCNT1        0xB4     /* M3 */
                                       /* FLEXDMA byte count 1        (rd/wr) */

#define  SCSI_FLEXDMASTAT     0xB5     /* M3 */
                                       /* FLEXDMA status              (rd/wr) */
#define     SCSI_FLEXDMAERR   0x02     /* flex dma fail                       */
#define     SCSI_FLEXDMADONE  0x01     /* flex dma done                       */

#define  SCSI_FLEXDATA        0xB6     /* M3 */
                                       /* FLEXDMA data port              (rd) */

#define  SCSI_BRDDAT          0xB8     /* M3 */
                                       /* board data                  (rd/wr) */
#define     SCSI_BRDDAT7      0x80     /* data bit 7                          */
#define     SCSI_BRDDAT6      0x40     /* data bit 6                          */
#define     SCSI_BRDDAT5      0x20     /* data bit 5                          */
#define     SCSI_BRDDAT4      0x10     /* data bit 4                          */
#define     SCSI_BRDDAT3      0x08     /* data bit 3                          */
#define     SCSI_BRDDAT2      0x04     /* data bit 2                          */
#define     SCSI_BRDDAT1      0x02     /* data bit 1                          */
#define     SCSI_BRDDAT0      0x01     /* data bit 0                          */
                                      
#define  SCSI_BRDCTL          0xB9     /* M3 */
                                       /* board control               (rd/wr) */
#define     SCSI_FLXARBACK    0x80     /* flexport arbitration acknowledge    */
#define     SCSI_FLXARBREQ    0x40     /* flexport arbitration request        */
#define     SCSI_BRDA2        0x20     /* board address 0-2                   */
#define     SCSI_BRDA1        0x10     /*                                     */
#define     SCSI_BRDA0        0x08     /*                                     */
#define     SCSI_BRDEN        0x04     /* board enable                        */
#define     SCSI_BRDRW        0x02     /* board read/write                    */
#define     SCSI_BRDSTB       0x01     /* board strobe                        */

                                       /* Flexport Board Address decode 0-2   */
#define     SCSI_FLXCSTREG7   (SCSI_BRDA0+SCSI_BRDA1+SCSI_BRDA2)     
#define     SCSI_FLXCSTREG6   (SCSI_BRDA1+SCSI_BRDA2)     
#define     SCSI_FLXCSTREG5   (SCSI_BRDA0+SCSI_BRDA2)     
#define     SCSI_FLXCSTREG4   (SCSI_BRDA2)
#define     SCSI_FLXCSTREG3   (SCSI_BRDA0+SCSI_BRDA1)     
#define     SCSI_FLXCSTREG2   (SCSI_BRDA1)
#define     SCSI_FLXCSTREG1   (SCSI_BRDA0)
#define     SCSI_FLXCSTREG0   0x00     


/*  Flexport Control/Status Register 0 definitiions */        
/* AutoTermination definitions for Harpoon based host adapters */
#define     SCSI_CHANNEL_CFG_MSB      0x80
#define     SCSI_CHANNEL_CFG_2        0x40
#define     SCSI_CHANNEL_CFG_1        0x20
#define     SCSI_CHANNEL_CFG_LSB      0x10
#define     SCSI_TERM_SECONDARY_HIGH  0x08
#define     SCSI_TERM_SECONDARY_LOW   0x04
#define     SCSI_TERM_PRIMARY_HIGH    0x02
#define     SCSI_TERM_PRIMARY_LOW     0x01

#define  SCSI_AUTOTERM_MODE0_CABLE_SENSING   0x00  /* cable sensing mode 0 undefined*/
#define  SCSI_AUTOTERM_MODE1_CABLE_SENSING   0x01  /* cable sensing mode 1 undefined*/
#define  SCSI_AUTOTERM_MODE2_CABLE_SENSING   0x02  /* cable sensing mode 2 Harpoon  */

/*  Flexport Control Register 1 */  
#define     SCSI_CURRENT_SENSE_CTL  0x01   /*Current sensing control */

/*  Flexport Status Register 1 */
#define     SCSI_SEEPROM_CFG        0xF0    
#define     SCSI_EEPROM_CFG         0x0F    
#define     SCSI_SEETYPE_93C66      0x00   /*Table decode for SEEPROM C66*/
#define     SCSI_SEETYPE_NONE       0x00   /*Table decode for NO SEEPROM */
#define     SCSI_EEPROM_512x8       0x00   /*Table decode for EEPROM*/ 
#define     SCSI_EEPROM_1Mx8        0x01   /*Table decode for EEPROM*/ 
#define     SCSI_EEPROM_2Mx8        0x02   /*Table decode for EEPROM*/ 
#define     SCSI_EEPROM_4Mx8        0x03   /*Table decode for EEPROM*/ 
#define     SCSI_EEPROM_16Mx8       0x04   /*Table decode for EEPROM*/ 
#define     SCSI_EEPROM_NONE        0xF0

/*  Flexport Status Register 2 */
#define     SCSI_REV_BIT3           0x80
#define     SCSI_REV_BIT2           0x40
#define     SCSI_REV_BIT1           0x20
#define     SCSI_REV_BIT0           0x10
#define     SCSI_REV_BITS           0xF0
#define     SCSI_FLXSTAT2_BUSY      0x01

/*  Flexport Status Register 3 */
/*  Current Sensing Status */
#define     SCSI_CSS_SECONDARY_HIGH1  0x80   /*CH A Current sensing bit         */
#define     SCSI_CSS_SECONDARY_HIGH0  0x40   /*CH A current sensing bit         */
#define     SCSI_CSS_SECONDARY_LOW1   0x20   /*CH A current sensing bit         */
#define     SCSI_CSS_SECONDARY_LOW0   0x10   /*CH A current sensing bit         */
#define     SCSI_CSS_PRIMARY_HIGH1    0x08   /*CH A current sensing bit         */
#define     SCSI_CSS_PRIMARY_HIGH0    0x04   /*CH A current sensing bit         */
#define     SCSI_CSS_PRIMARY_LOW1     0x02   /*CH A current sensing bit         */
#define     SCSI_CSS_PRIMARY_LOW0     0x01   /*CH A current sensing bit         */

#define     SCSI_CURRENT_SENSING_OVER_BIT  0x02
#define     SCSI_CURRENT_SENSING_UNDER_BIT 0x01

#define  SCSI_SE2ADR          0xBA     /* M3 */
                                       /* serial eeprom address       (rd/wr) */
#define     SCSI_SE2ADR7      0x80     /* address bits                        */
#define     SCSI_SE2ADR6      0x40     /*                                     */
#define     SCSI_SE2ADR5      0x20     /*                                     */
#define     SCSI_SE2ADR4      0x10     /*                                     */
#define     SCSI_SE2ADR3      0x08     /*                                     */
#define     SCSI_SE2ADR2      0x04     /*                                     */
#define     SCSI_SE2ADR1      0x02     /*                                     */
#define     SCSI_SE2ADR0      0x01     /*                                     */

#define  SCSI_SE2DAT0         0xBC     /* M3 */
                                       /* serial eeprom data 0        (rd/wr) */
#define  SCSI_SE2DAT1         0xBD     /* M3 */
                                       /* serial eeprom data 1        (rd/wr) */

#define  SCSI_SE2CST_STAT     0xBE     /* M3 */
                                       /* serial eeprom status           (rd) */
#define     SCSI_INIT_DONE    0x80     /* done with POR auto fetching process */
#define     SCSI_SE2OPCODE2   0x40     /* 3-bit opcode for controlling  the   */
#define     SCSI_SE2OPCODE1   0x20     /* read/write operation of SEEPROM     */
#define     SCSI_SE2OPCODE0   0x10     /*                                     */
#define     SCSI_LDALTID_L    0x08     /* 12-byte data is regular/alter ID    */
#define     SCSI_SEEARBACK    0x04     /* arbitration acknowledge             */
#define     SCSI_BUSY         0x02     /* internal control logic is busy      */
#define     SCSI_START        0x01     /* start rd/wr operation status        */

#define  SCSI_SE2CST_CTL      0xBE     /* M3 */
                                       /* serial eeprom control          (wr) */
#define     SCSI_SE2RST       0x02     /* reset seeprom control               */

/* SE2OPCODE */
#define     SCSI_SE2_READ           0x60  /* read data                          */
#define     SCSI_SE2_ERASE          0x70  /* erase memory                       */
#define     SCSI_SE2_WRITE          0x50  /* write memory                       */
#define     SCSI_SE2_ERAL           0x40  /* erase all                          */
#define     SCSI_SE2_WRAL           0x40  /* write all                          */
#define     SCSI_SE2_EWDS           0x40  /* disable write                      */
#define     SCSI_SE2_EWEN           0x40  /* enable write                       */

 /*EWEN, ERAL, WRAL and EWDS require specific addresses for the instruction to execute */
#define     SCSI_SE2_EWEN_ADDR      0xC0  /* enable write address               */
#define     SCSI_SE2_ERAL_ADDR      0x80  /* erase all address                  */ 
#define     SCSI_SE2_WRAL_ADDR      0x40  /* write all address                  */
#define     SCSI_SE2_EWDS_ADDR      0x00  /* diasable write address             */

/* Definition for SEEPROM type */
#define SCSI_EETYPE_C56C66   0           /* NM93C56 or NM93C66 type SEEPROM */


#define  SCSI_SCBCNT          0xBF     /* M3 */
                                       /* SCB counter                 (rd/wr) */

#else   /* DCH definitions */

#define  SCSI_SGBUFFER_PORT   0xB0     /* M (0,1) 32 bits                     */
#define  SCSI_CMCBUFFER_PORT  0xB0     /* M2      32 bits                     */
#define  SCSI_CMCDMAERR0      0xB0     /* M3      32 bits                     */
#define  SCSI_SGDMAERR        0xB0     /* M4      32 bits                     */

#define  SCSI_CMCSCBCNT       0xB2     /* M2 Size of SCB to DMA (16 bits)     */

#define  SCSI_CMCDMAERREN0    0xB2     /* M3      32 bits                     */

#define  SCSI_SGDMAERREN      0xB2     /* M4      32 bits                     */

#define  SCSI_CMCDMAERR1      0xB8     /* M3  CMC DMA error 1 reg             */
#define     SCSI_CMCDMARCNT      0x01  /* CMC DMA rewind count                */

#define  SCSI_CMCDMAERREN1    0xB9     /* M3 */
#define     SCSI_CMCDMARRLSEN 0x02     /* CMD/STS DMA rewind enable           */
#define     SCSI_CMCDMARCNTEN 0x01     /* CMD/STS DMA rewind error enable     */

#define  SCSI_MSCRBIST      0xBC        /* M3  Mode Scratch BIST              */
#define     SCSI_MSCRBISTFAIL 0x04     /* */
#define     SCSI_MSCRBISTDONE 0x02     /* */
#define     SCSI_MSCRBISTEN   0x01     /* */

#define  SCSI_SCRBIST      0xBD        /* M3  Scratch BIST                    */
#define     SCSI_SCRBISTFAIL  0x04     /* */
#define     SCSI_SCRBISTDONE  0x02     /* */
#define     SCSI_SCRBISTEN    0x01     /* */

#define  SCSI_SCBBIST      0xBD        /* M3  SCB BIST                        */
#define     SCSI_SCBBISTFAIL  0x04     /* */
#define     SCSI_SCBBISTDONE  0x02     /* */
#define     SCSI_SCBBISTEN    0x01     /* */

#endif /* !SCSI_DCH_U320_MODE */
/****************************************************************************

 DFF REGISTERS - ARP DFF control and addressing
      Address Range: 0xC0 - 0xCF

****************************************************************************/

#define  SCSI_DFWADDR         0xC0     /* M0,M1 */
                                       /* data fifo 0/1 write address (rd/wr) */
#define  SCSI_M0DFWADDR       0xC0     /* M0 */
                                       /* data fifo 0 write address   (rd/wr) */
#define  SCSI_M1DFWADDR       0xC0     /* M1 */
                                       /* data fifo 1 write address   (rd/wr) */

#define  SCSI_DFRADDR         0xC2     /* M0,M1 */
                                       /* data fifo 0/1 read address  (rd/wr) */
#define  SCSI_M0DFRADDR       0xC2     /* M0 */
                                       /* data fifo 0 read address    (rd/wr) */
#define  SCSI_M1DFRADDR       0xC2     /* M1 */
                                       /* data fifo 1 read address    (rd/wr) */

#define  SCSI_DFDAT           0xC4     /* M0,M1 */
                                       /* data fifo 0/1 data          (rd/wr) */
#define  SCSI_M0DFDAT         0xC4     /* M0 */
                                       /* data fifo 0 data            (rd/wr) */
#define  SCSI_M1DFDAT         0xC4     /* M1 */
                                       /* data fifo 1 data            (rd/wr) */

#define  SCSI_DFPTRS          0xC8     /* M0,M1 */
                                       /* data fifo 0/1 pointer       (rd/wr) */
#define  SCSI_M0DFPTRS        0xC8     /* M0 */
                                       /* data fifo 0 pointer         (rd/wr) */
#define  SCSI_M1DFPTRS        0xC8     /* M1 */
                                       /* data fifo 1 pointer         (rd/wr) */
#define     SCSI_DFRPTR2      0x40     /* data fifo read pointer              */
#define     SCSI_DFRPTR1      0x20     /*                                     */
#define     SCSI_DFRPTR0      0x10     /*                                     */
#define     SCSI_DFRPTR       (SCSI_DFRPTR0+SCSI_DFRPTR1+SCSI_DFRPTR2)
#define     SCSI_DFWPTR2      0x04     /* data fifo write pointer             */
#define     SCSI_DFWPTR1      0x02     /*                                     */
#define     SCSI_DFWPTR0      0x01     /*                                     */
#define     SCSI_DFWPTR       (SCSI_DFWPTR0+SCSI_DFWPTR1+SCSI_DFWPTR2)

#define  SCSI_DFBKPTR         0xC9     /* M0,M1 */
                                       /* data fifo 0/1 backup ptr    (rd/wr) */
#define  SCSI_M0DFBKPTR       0xC9     /* M0 */
                                       /* data fifo 0 backup pointer  (rd/wr) */
#define  SCSI_M1DFBKPTR       0xC9     /* M1 */
                                       /* data fifo 1 backup pointer  (rd/wr) */

#define  SCSI_DFDBCTL         0xCB     /* M0,M1 */
                                       /* data fifo 0/1 debug control (rd/wr) */
#define  SCSI_M0DFDBCTL       0xCB     /* M0 */
                                       /* data fifo 0 debug control   (rd/wr) */
#define  SCSI_M1DFDBCTL       0xCB     /* M1 */
                                       /* data fifo 1 debug control   (rd/wr) */

#define  SCSI_DFSCNT          0xCC     /* M0,M1 */
                                       /* data fifo 0/1 space count      (rd) */
#define  SCSI_M0DFSCNT        0xCC     /* M0 */
                                       /* data fifo 0 space count        (rd) */
#define  SCSI_M1DFSCNT        0xCC     /* M1 */
                                       /* data fifo 1 space count        (rd) */

#define  SCSI_DFBCNT          0xCE     /* M0,M1 */
                                       /* data fifo 0/1 byte count       (rd) */
#define  SCSI_M0DFBCNT        0xCE     /* M0 */
                                       /* data fifo 0 byte count         (rd) */
#define  SCSI_M1DFBCNT        0xCE     /* M1 */
                                       /* data fifo 1 byte count         (rd) */

#define  SCSI_DSPFLTRCTL      0xC0     /* M4 */
                                       /* dsp filter control          (rd/wr) */

#define  SCSI_DSPDATACTL      0xC1     /* M4 */
                                       /* dsp data channel control    (rd/wr) */
#define     SCSI_BYPASSENAB   0x80     /* enable bypass                       */
#define     SCSI_RCVROFFSTDIS 0x04     /* disable receiver offset cancellation*/
#define     SCSI_XMITOFFSTDIS 0x02     /* disable transmit offset cancellation*/

#define  SCSI_DSPREQCTL       0xC2     /* M4 */
                                       /* dsp req control             (rd/wr) */
#define     SCSI_MANREQCTL1   0x80     /* manual REQ control bits             */
#define     SCSI_MANREQCTL0   0x40
#define     SCSI_MANREQDLY5   0x20     /* manual REQ delay bit 5              */
#define     SCSI_MANREQDLY4   0x10     /* manual REQ delay bit 4              */
#define     SCSI_MANREQDLY3   0x08     /* manual REQ delay bit 3              */
#define     SCSI_MANREQDLY2   0x04     /* manual REQ delay bit 2              */
#define     SCSI_MANREQDLY1   0x02     /* manual REQ delay bit 1              */
#define     SCSI_MANREQDLY0   0x01     /* manual REQ delay bit 0              */
#define     SCSI_REQDLY_NEGATIVE2 0x3E /* manual REQ delay of -0.6ns          */
#define     SCSI_REQDLY_NEGATIVE1 0x3F /* manual REQ delay of -0.3ns          */
#define     SCSI_REQDLY_POSITIVE1 0x01 /* manual REQ delay of +0.3ns          */
#define     SCSI_REQDLY_POSITIVE2 0x02 /* manual REQ delay of +0.6ns          */

#define  SCSI_DSPACKCTL       0xC3     /* M4 */
                                       /* dsp ack control             (rd/wr) */

#define  SCSI_DSPSELECT       0xC4     /* M4 */
                                       /* dsp channel select          (rd/wr) */

#define  SCSI_WRTBIASCTL      0xC5     /* M4 */
                                       /* write bias current control     (wr) */

#define  SCSI_MAX_WRTBIASCTL     0x1F  /* Bits 1 - 5 */
#define  SCSI_BIASPOLARITY       0x01  /* Bit 5 must be set to get correct polarity */
#define  SCSI_AUTO_WRTBIASCTL    0x00
#define  SCSI_DEFAULT_WRTBIASCTL SCSI_AUTO_WRTBIASCTL
#define  SCSI_AUTOEXBCDIS        0x80  /* Automatic Transmit Bias Control Disable */

#if SCSI_OEM1_SUPPORT
#define  SCSI_OEM1_WRTBIASCTL_DEFAULT  SCSI_AUTO_WRTBIASCTL
#endif /* SCSI_OEM1_SUPPORT */

#define  SCSI_RCVRBIASCTL     0xC6     /* M4 */
                                       /* receiver bias current control  (wr) */

#define  SCSI_WRTBIASCALC     0xC7     /* M4 */
                                       /* write bias current calc. read  (rd) */

#define  SCSI_RCVRBIASCALC    0xC8     /* M4 */
                                       /* receiver bias current calc. rd (rd) */

#define  SCSI_SKEWCALC        0xC9     /* M4 */
                                       /* skew calculator read           (rd) */

/****************************************************************************

 ARP REGISTERS - ARP's control and status
      Address Range: 0xD4 - 0xF7

****************************************************************************/

#define  SCSI_OVLYADDR0       0xD4     /* M3 */
                                       /* overlay address, lo         (rd/wr) */
#define  SCSI_OVLYADDR1       0xD5     /* M3 */
                                       /* overlay address, hi         (rd/wr) */

#define  SCSI_SEQCTL0         0xD6     /* M(0,1,2,3,4) */
                                       /* sequencer control 0         (rd/wr) */
#define     SCSI_PERRORDIS    0x80     /* enable sequencer parity errors      */
#define     SCSI_PAUSEDIS     0x40     /* disable pause by driver             */
#define     SCSI_FAILDIS      0x20     /* disable illegal opcode/address int  */
#define     SCSI_FASTMODE     0x10     /* sequencer clock select              */
                                       /*   reserved in Harpoon 2 Rev B       */
#define     SCSI_BRKADRINTEN  0x08     /* break address interrupt enable      */
#define     SCSI_STEP         0x04     /* single step sequencer program       */
#define     SCSI_SEQRESET     0x02     /* put program counter to 0            */
#define     SCSI_LOADRAM      0x01     /* enable sequencer ram loading        */
                                       /*   reserved in Harpoon 2 Rev B       */

#define  SCSI_SEQCTL1         0xD7     /* M(0,1,2,3,4) */
                                       /* sequencer control 1         (rd/wr) */
#define     SCSI_OVLY_DATA_CHK 0x08    /* overlay data check                  */
                                       /*   reserved in Harpoon 2 Rev B       */
#define     SCSI_RAMBIST_DONE 0x04     /* RAM BIST completion status          */
#define     SCSI_RAMBIST_FAIL 0x02     /* RAM BIST fail status                */
#define     SCSI_RAMBIST_EN   0x01     /* RAM BIST enable                     */

#define  SCSI_SEQCODE_SIZE    8192     /* U320 (Harpoon) sequencer ram size   */

#define  SCSI_FLAGS           0xD8     /* M(0,1,2,3,4) */
                                       /* flags                          (rd) */
#define     SCSI_ZERO         0x02     /* sequencer 'zero' flag               */
#define     SCSI_CARRY        0x01     /* sequencer 'carry' flag              */

#define  SCSI_SEQINTCTL       0xD9     /* M(0,1,2,3,4) */
                                       /* sequencer interrupt control (rd/wr) */
#define     SCSI_INTVEC1DSL   0x80     /* interrupt vector 1 disable          */
#define     SCSI_SCS_SEQ_INT1M1  0x10  /* SCS to SEQ intr vector 1 in mode 1  */
#define     SCSI_SCS_SEQ_INT1M0  0x08  /* SCS to SEQ intr vector 1 in mode 0  */
#define     SCSI_INTMASK2     0x04     /* interrupt vector 2 mask             */
#define     SCSI_INTMASK1     0x02     /* interrupt vector 1 mask             */
#define     SCSI_IRET         0x01     /* interrupt return                    */

#define  SCSI_SEQRAM          0xDA     /* M(0,1,2,3,4) */
                                       /* sequencer ram data          (rd/wr) */

#define  SCSI_PRGMCNT0        0xDE     /* M(0,1,2,3,4) */
                                       /* seq program counter, lo     (rd/wr) */
#define  SCSI_PRGMCNT1        0xDF     /* M(0,1,2,3,4) */
                                       /* seq program counter, hi     (rd/wr) */

#define  SCSI_ACCUM           0xE0     /* M(0,1,2,3,4) */
                                       /* accumulator                 (rd/wr) */

#define  SCSI_SINDEX          0xE2     /* M(0,1,2,3,4) */
                                       /* source index register       (rd/wr) */

#define  SCSI_DINDEX          0xE4     /* M(0,1,2,3,4) */
                                       /* destination index register  (rd/wr) */

#define  SCSI_BRKADDR0        0xE6     /* M(0,1,2,3,4) */
                                       /* break address, lo           (rd/wr) */

#define  SCSI_BRKADDR1        0xE7     /* M(0,1,2,3,4) */
                                       /* break address, hi           (rd/wr) */
#define     SCSI_BRKDIS       0x80     /* disable breakpoint                  */
#define     SCSI_BRKADDR08    0x01     /* breakpoint addr, bit 8              */

#define  SCSI_ALLONES         0xE8     /* M(0,1,2,3,4) */
                                       /* all ones, src reg = 0ffh       (rd) */

#define  SCSI_ALLZEROS        0xEA     /* M(0,1,2,3,4) */
                                       /* all zeros, src reg = 00h       (rd) */

#define  SCSI_NONE            0xEA     /* M(0,1,2,3,4) */
                                       /* no destination, No reg altered (wr) */

#define  SCSI_SINDIR          0xEC     /* M(0,1,2,3,4) */
                                       /* source index reg, indirect     (rd) */

#define  SCSI_DINDIR          0xED     /* M(0,1,2,3,4) */
                                       /* destination index reg, indirect(rd) */

#define  SCSI_JUMLDIR         0xEE     /* M(0,1,2,3,4) */
                                       /* long jump direction            (rd) */

#define  SCSI_FUNCTION1       0xF0     /* M(0,1,2,3,4) */
                                       /* funct: bits 6-4 -> 1-of-8   (rd/wr) */

#define  SCSI_STACK           0xF2     /* M(0,1,2,3,4) */
                                       /* stack, for subroutine returns  (rd) */

#define  SCSI_CURADDR0        0xF4     /* M3 */
                                       /* current address, lo            (rd) */
#define  SCSI_CURADDR1        0xF5     /* M3 */
                                       /* current address, hi            (rd) */

#define  SCSI_LASTADDR0       0xF6     /* M3 */
                                       /* last address, lo               (rd) */
#define  SCSI_LASTADDR1       0xF7     /* M3 */
                                       /* last address, hi               (rd) */

#define  SCSI_INTVECT1_ADDR0   0xF4    /* M4 */
                                       /* interrupt vector 1 addr, lo (rd/wr) */
#define  SCSI_INTVECT1_ADDR1   0xF5    /* M4 */
                                       /* interrupt vector 1 addr, hi (rd/wr) */

#define  SCSI_INTVECT2_ADDR0   0xF6    /* M4 */
                                       /* interrupt vector 2 addr, lo (rd/wr) */
#define  SCSI_INTVECT2_ADDR1   0xF7    /* M4 */
                                       /* interrupt vector 2 addr, hi (rd/wr) */

/****************************************************************************

 SCB REGISTERS - mode dependent scratch memory in modes 0-3
      Address Range: 0xF8 - 0xFF

****************************************************************************/

#define  SCSI_SCRATCH1        0xF8     /* M0,M1,M2,M3 */
                                       /* scratch dependent base addr (rd/wr) */
#define  SCSI_M0SCRATCH1      0xF8     /* M0 */
                                       /* scratch mode 0 dependent    (rd/wr) */
#define  SCSI_M1SCRATCH1      0xF8     /* M1 */
                                       /* scratch mode 1 dependent    (rd/wr) */
#define  SCSI_M2SCRATCH1      0xF8     /* M2 */
                                       /* scratch mode 2 dependent    (rd/wr) */
#define  SCSI_M3SCRATCH1      0xF8     /* M3 */
                                       /* scratch mode 3 dependent    (rd/wr) */

#define  SCSI_SCRATCH1_SIZE   8        /* scratch 1 size                      */

/****************************************************************************

 SCB REGISTERS - mode independent scratch memory in modes 0-3
      Address Range: 0x100 - 0x17F

****************************************************************************/

#define  SCSI_SCRATCH0        0x100    /* M(0,1,2,3) */
                                       /* scratch base address        (rd/wr) */

#define  SCSI_SCRATCH0_SIZE   128      /* scratch 0 size                      */

/****************************************************************************

 SCB REGISTERS - mode dependent SCB memory in modes 0-3
      Address Range: 0x180 - 0x1BF

****************************************************************************/

#define  SCSI_SCB_BASE        0x180    /* M0,M1,M2,M3 */
                                       /* scsi control block base addr(rd/wr) */

#define  SCSI_MAX_SCBSIZE     64       /* scsi control block size             */


/****************************************************************************

 PCI CONFIGURATION SPACE REGISTERS

****************************************************************************/
#if !SCSI_DCH_U320_MODE
#define  SCSI_CONFIG_BASE     0x100    /* M4 */
                                       /* for backdoor access of config space */
#define  SCSI_COMMAND_LO_REG  0x0004   /* Low byte of command register        */
#define     SCSI_PCI_PERRESPEN 0x40    /* Parity error response enable        */
#define     SCSI_PCI_MWRICEN  0x10     /* MWIC enable                         */
#define     SCSI_PCI_MASTEREN 0x04     /* Master enable                       */
#define     SCSI_PCI_MSPACEEN 0x02     /* Memory space enable                 */
#define     SCSI_PCI_ISPACEEN 0x01     /* IO space enable                     */ 

#define  SCSI_COMMAND_HI_REG  0x0005   /* High byte of command register       */
#define     SCSI_PCI_SERRESPEN 0x01    /* System error response enable        */

#define  SCSI_STATUS_HI_REG   0x0007   /* High byte of status register        */
#define     SCSI_PCI_DPE      0x80     /* Detected parity error               */
#define     SCSI_PCI_SSE      0x40     /* Signaled system error               */
#define     SCSI_PCI_RMA      0x20     /* Received master abort               */
#define     SCSI_PCI_RTA      0x10     /* Received target abort               */
#define     SCSI_PCI_STA      0x08     /* Signaled target abort               */
#define     SCSI_PCI_DPR      0x01     /* Data parity reported                */ 
#define     SCSI_PCIERR_MASK  (SCSI_PCI_DPE+SCSI_PCI_SSE+SCSI_PCI_RMA+\
                               SCSI_PCI_RTA+SCSI_PCI_STA+SCSI_PCI_DPR)

#define  SCSI_CONFIG_ADDRESS  0x0CF8   /* System Config Double word address 1 */

#define  SCSI_FORWARD_REG     0x0CFA   /* System Config Forward Address       */

#define  SCSI_CONFIG_DATA     0x0CFC   /* System Config Double word address 2 */

                                       /* for OSM function call to access of  */
                                       /* config space */
#define  SCSI_ID_REG          0x0000   /* Device Indentification register     */

#define  SCSI_EMBEDDED_ID     0x0008   /* Bit 3 of Device ID: 0=HBA and 1=MB  */
#define  SCSI_ID_MASK         0xFF10   /* Id mask for multiple devices        */
#define  SCSI_PN_MASK         0xFF00   /* Part Number mask for Device ID      */
#define  SCSI_39320A          0x8016   /* Id for Harpoon 2B HBA; ASC-39320A   */
#define     SCSI_39320D_REVB  0x801C   /* Id for Harpoon 2B HBA: ASC-39320D   */
#if SCSI_IROC
#define     SCSI_39320D_REVB_IROC  0x809C /* Id for iROC Harpoon 2B HBA:      */
                                          /* ASC-39320D                       */
#endif /* SCSI_IROC */

#define  SCSI_STATUS_CMD_REG  0x0004   /* Status/Command registers            */
#define     SCSI_DPE          0x80000000  /* Detected parity error            */
#define     SCSI_DPE_MASK     0x8000FFFF  /*                                  */
#define     SCSI_SSE          0x40000000  /* Signal system error              */
#define     SCSI_SSE_MASK     0x4000FFFF  /*                                  */
#define     SCSI_RMA          0x20000000  /* Received master abort            */
#define     SCSI_RMA_MASK     0x2000FFFF  /*                                  */
#define     SCSI_RTA          0x10000000  /* Received target abort            */
#define     SCSI_RTA_MASK     0x1000FFFF  /*                                  */
#define     SCSI_STA          0x08000000  /* Signal target abort              */
#define     SCSI_STA_MASK     0x0800FFFF  /*                                  */
#define     SCSI_DPR          0x01000000  /* Data parity reported             */
#define     SCSI_DPR_MASK     0x0100FFFF  /*                                  */
#define     SCSI_SERRESPEN    0x00000100  /* System error response enable     */
#define     SCSI_PERRESPEN    0x00000040  /* Parity error response enable     */
#define     SCSI_MWRICEN      0x00000010  /* MWIC enable                      */
#define     SCSI_MASTEREN     0x00000004  /* Master enable                    */
#define     SCSI_MSPACEEN     0x00000002  /* Memory space enable              */
#define     SCSI_ISPACEEN     0x00000001  /* IO space enable                  */
#define     SCSI_ENABLE_MASK  (SCSI_MASTEREN + SCSI_ISPACEEN)

#define  SCSI_DEV_REV_ID_REG    0x0008 /* Device revision id                  */

#define  SCSI_CACHE_LAT_HDR_REG 0x000C /* Cache Line Size, Latency Timer,     */
                                       /* Header Type and Built-In-Self-Test  */
#define     SCSI_CDWDSIZE       0x000000FC    /* Cache Line Size in units of 32-bit*/
#define     SCSI_HDRTYPE_MASK      0x007F0000 /* Header Type byte mask        */
#define     SCSI_HDRTYPE_MULTIFUNC 0x00800000 /* Header Type MultiFunction flag*/

#define  SCSI_BASE_ADDR_REG   0x0010   /* Base Port Address for I/O space     */

#define  SCSI_SSID_SVID_REG   0x002C   /* SubSystem ID and SubVendor ID       */
#define     SCSI_SVID_MASK    0x0000FFFF  /* SubVendor ID bytes mask          */
#define     SCSI_OEM1_SUBVID  0x0E11   /* SubVendor ID for OEM1               */

#define  SCSI_DEVCONFIG0_REG  0x0040      /* Device configuration 0 register  */
#define     SCSI_QWENDIANSEL  0x00000001  /* QWORD big endian conversion sel  */
#define     SCSI_STPWLEVEL    0x00000002  /* SCSI termination power level sel */
#define     SCSI_DACEN        0x00000004  /* Dual address cycle enable        */
#define     SCSI_MIXQWENDIANEN 0x00000008 /* Mix QWORD big endian conversion sel */
#define     SCSI_ENDIANSEL    0x00000020  /* Big endian select                */
#define     SCSI_MRDCEN       0x00000040  /* Memory read command enable       */
#define     SCSI_64BITS_PCI   0x00000080  /* 64 bits pci                      */
#define     SCSI_FRAME_RST    0x00000100  /* Frame reset                      */
#define     SCSI_IRDY_RST     0x00000200  /* Initialization ready reset       */
#define     SCSI_MPORTMODE    0x00000400  /* Memory port mode: single/multi user */
#define     SCSI_TESTMODE     0x00000800  /* Test mode pin                    */

#define  SCSI_DEVSTATUS0_REG  0x0041   /* Device Status 0 register            */
#define     SCSI_PCI33_66     0xE0     /* PCI    33 or 66  MHz                */
#define     SCSI_PCIX50_66    0xC0     /* PCI-X  50 or 66  MHz                */
#define     SCSI_PCIX66_100   0xA0     /* PCI-X  66 or 100 MHz                */
#define     SCSI_PCIX100_133  0x80     /* PCI-X 100 or 133 MHz                */

#define  SCSI_PCIERRGEN_REG   0x0043   /* PCI Error Generation register       */
#define     SCSI_PCIERRGENDIS 0x80     /* PCI Error Generation Disable        */

#define  SCSI_DEVCONFIG1_REG  0x0044   /* Device configuration 1 register     */
#define     SCSI_PREQDIS      0x01     /* PREQ# Deassertion                   */

#define     SCSI_ONE_CHNL    0x00      /* One channel    00b                  */
#define     SCSI_TWO_CHNL    0x01      /* Two channels   01b                  */

/******************************************************************************

                      PCI Bus Test Register 0x143
                      
 ******************************************************************************/

#define     SCSI_PARERRGEN   0x43      /* generate DPEs for PCIINT testing    */  

/******************************************************************************

                      PCI-X SLave Split Status Regs 0x19D

 ******************************************************************************/
#define     SCSI_SLVSPLTSTAT     0x9D

#define        SCSI_DPR_SPLT     0x80     /* Perr for Target split master     */
#define        SCSI_RXSCITA      0x40     /* Rcvd splt Cmpltn Immed Trgabrt   */
#define        SCSI_RXSCIMA      0x20     /* Rcvd splt Cmpltn Immed Mstrabrt  */
#define        SCSI_RSCDISCERR   0x10     /* Rcvd splt Cmpltn discard error   */
#define        SCSI_WSCDISCERR   0x08     /* Wrt splt Cmpltn discard error    */
#define        SCSI_TXSCEMWDP    0x04     /* Transmitted Splt Cmpltn WDatPerr */
#define        SCSI_TXSPLTRSPR   0x02     /* Transmitted Splt Response Read   */
#define        SCSI_TXSPLTRSPW   0x01     /* Transmitted Splt Response Write  */
#define        SCSI_PCIX_SLAVE_ERRORS  (SCSI_DPR_SPLT+SCSI_RXSCITA+SCSI_RXSCIMA)

/******************************************************************************

                      PCI-X Status Regs 0x19b - 0x198

 ******************************************************************************/

#define     SCSI_PCIX_STATUS3    0x9B        /* bits 31:24        */
#define        SCSI_RSCEM        0x20        /* Received split completion err */
               
#define     SCSI_PCIX_STATUS2    0x9A        /* bits 23:16        */
#define        SCSI_UNEXPSC      0x08        /* Split completion received
                                                with no matching tag.         */
#define        SCSI_133CAPABLE   0x02
#define        SCSI_64BITDEV E   0x01

#define     SCSI_PCIX_STATUS1    0x99        /* bits 15:8         */
#define     SCSI_PCIX_STATUS0    0x98        /* bits 7:0          */

#else

/******************************************************************************

                      Some coverage for DCH ID infomation

 ******************************************************************************/
#define  SCSI_PN_MASK         0xFF00   /* Part Number mask for Device ID      */
#define  SCSI_ID_MASK         0xFF10   /* Id mask for multiple devices        */

#endif /* !SCSI_DCH_U320_MODE */


/****************************************************************************

 ARP interface - ARP Interrupt Codes identify action to be taken
                 by the CHIM.

****************************************************************************/

#define  SCSI_SYNC_NEGO_NEEDED   0x00  /* initiate synchronous negotiation    */
#define  SCSI_ATN_TIMEOUT        0x00  /* timeout in atn_tmr routine          */
#define  SCSI_CDB_XFER_PROBLEM   0x01  /* possible parity error in cdb:  retry*/
#define  SCSI_HANDLE_MSG_OUT     0x02  /* handle Message Out phase            */
#define  SCSI_DATA_OVERRUN       0x03  /* data overrun detected               */
#define  SCSI_UNKNOWN_MSG        0x04  /* handle the message in from target   */
#define  SCSI_CHECK_CONDX        0x05  /* Check Condition from target         */
#define  SCSI_PHASE_ERROR        0x06  /* unexpected scsi bus phase           */
#define  SCSI_EXTENDED_MSG       0x07  /* handle Extended Message from target */
#define  SCSI_ABORT_TARGET       0x08  /* abort connected target              */
#define  SCSI_NO_ID_MSG          0x09  /* reselection with no id message      */
#define  SCSI_IDLE_LOOP_BREAK    0x0A  /* idle loop breakpoint                */
#define  SCSI_EXPANDER_BREAK     0x0B  /* expander breakpoint                 */
#if SCSI_TARGET_OPERATION  
#define  SCSI_TARGET_SELECTION_IN 0x0C /* target mode: selection-in           */
#define  SCSI_TARGET_BUS_HELD    0x0D  /* target mode: SCSI BUS is held       */
#define  SCSI_TARGET_ATN_DETECTED 0x0E /* target mode: ATN detected           */
#endif  /* SCSI_TARGET_OPERATION */   
#define  SCSI_DV_TIMEOUT         0x0F  /* DV timer expired.                   */
#define  SCSI_DATA_OVERRUN_BUSFREE 0x10/* data overrun interrupt is generated */
                                       /* after command complete message has  */
                                       /* been acked.  So, HIM won't need to  */
                                       /* set the last_seg_done bit.          */
#define  SCSI_SPECIAL_FUNCTION   0x11  /* special_funct SCB has been detected */
                                       /* by sequencer after it appended a    */
                                       /* SCB into target's execution queue.  */
#define  SCSI_HWERR_DETECTED     0x12  /* Internal hardware error detected.   */
#if !SCSI_SEQ_LOAD_USING_PIO
#define  SCSI_LOAD_DONE          0x01  /* seq loading using DMA is done       */
#endif /* !SCSI_SEQ_LOAD_USING_PIO */

/* Delay factor based on sequencer instruction speed */
#define  SCSI_DELAY_FACTOR_20MIPS  10     /* 1 usec delay for 20 mips         */
                                          /* sequencer (bayonet)              */
#define  SCSI_DELAY_FACTOR_10MIPS   5     /* 1 usec delay for 10 mips         */
                                          /* sequencer (aic78xx)              */

#define  SCSI_U320_TIMER_GRANULARITY 25   /* in uSec                          */

/****************************************************************************

 DEFINITIONS USE FOR HOST ADAPTER'S WORLD WIDE ID (WWID)

****************************************************************************/

#define SCSI_MAX_ID_CODE  0x30   /* bit 5 & 4 for the Max ID code */
#define    SCSI_UPTO_1F   0x00        /* Maximum assignable ID - 0x1f */
#define    SCSI_UPTO_0F   0x10        /* Maximum assignable ID - 0x0f */
#define    SCSI_UPTO_07   0x20        /* Maximum assignable ID - 0x07 */

#define SCSI_ID_VALID     0x06   /* bit 2 & 1 for the ID valid info */
#define    SCSI_NOT_VALID          0x00   /* ID field bit 4-0 not valid */
#define    SCSI_VALID_BUT_UNASSIGN 0x02   /* ID field bit 4-0 valid but not assigned */
#define    SCSI_VALID_AND_ASSIGN   0x04   /* ID field bit 4-0 valid and assigned */

#define SCSI_SNA          0x01   /* bit 0 for Serial Number Available */


/****************************************************************************

 Hardware Chip Device ID and Revision definitions

****************************************************************************/
#define SCSI_U320_ASIC            0x8000  /* DID=0x80XX is U320 chip          */
#define SCSI_HARPOON1_BASED_ID    0x8000  /* Based ID for AIC7901 (Harpoon 1) */
#define SCSI_HARPOON2_BASED_ID    0x8010  /* Based ID for AIC7902 (Harpoon 2) */

#define SCSI_HARPOON1_REV_3_CHIP  3       /* It based on H2A4 chip            */
#define SCSI_HARPOON1_REV_10_CHIP 0x10

#define SCSI_HARPOON2_REV_0_CHIP  0
#define SCSI_HARPOON2_REV_1_CHIP  1
#define SCSI_HARPOON2_REV_2_CHIP  2
#define SCSI_HARPOON2_REV_3_CHIP  3
#define SCSI_HARPOON2_REV_10_CHIP 0x10


