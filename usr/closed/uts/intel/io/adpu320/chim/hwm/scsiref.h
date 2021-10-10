/*$Header: /vobs/u320chim/src/chim/hwm/scsiref.h   /main/92   Fri Aug 22 16:19:59 2003   hu11135 $*/

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
*  Module Name:   SCSIREF.H
*
*  Description:   Definitions as interface to HIM internal layers
*    
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         1. This file is included from HIMDEF.H
*                 2. This file should be modified to fit a particular
*                    Common HIM implementation.
*
***************************************************************************/

#define  SCSI_DOMAIN_VALIDATION  ((SCSI_DOMAIN_VALIDATION_BASIC)+(SCSI_DOMAIN_VALIDATION_ENHANCED))


/***************************************************************************
* Miscellaneous
***************************************************************************/
#if !SCSI_DCH_U320_MODE
#define  SCSI_MAXSCBS      512         /* Maximum number of possible SCBs  */
#else
#define  SCSI_MAXSCBS      256         /* Maximum number of possible SCBs  */
#endif /* !SCSI_DCH_U320_MODE */
#if SCSI_TARGET_OPERATION
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_MINSCBS      4           /* minimum number of possible SCBs  */
                                       /* when target mode enabled         */  
#else
#define  SCSI_MINSCBS      3           /* minimum number of possible SCBs  */
                                       /* when target mode enabled         */  
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#else
#define  SCSI_MINSCBS      1           /* minimum number of possible SCBS  */
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_MINDONEQELEMENTS 4       /* Minimum number of Done Q elements */
#define  SCSI_MAXDEV     16            /* maximum number of target devs    */
                                       /* maximum value for AP_ResetDelay  */
                                       /* as uses a 500usec counter        */                       
#define  SCSI_MAX_RESET_DELAY   ((SCSI_UEXACT32) 0x7FFFFFFF)

#define  SCSI_DELAY_500_USECS      500    /* define for 500 usec */ 
#define  SCSI_RESET_HOLD_DEFAULT    50    /* reset hold time of 50 microseconds  */


#define  SCSI_HIOB_MAX_SENSE_LENGTH   255 /* Maximum ammount of SCSI sense data */
                                          /* to be returned. Limited by field   */
                                          /* size currently.                    */ 
typedef struct SCSI_DEVICE_ SCSI_DEVICE;

#if SCSI_TARGET_OPERATION 
#if SCSI_MULTIPLEID_SUPPORT
#define  SCSI_MAXADAPTERIDS 15         /* Maximum number of Adapter IDs    */
#define  SCSI_MAXNODES      (15 * SCSI_MAXADAPTERIDS) /* Maximum number of Nodes  */ 
#else
#define  SCSI_MAXNODES      15         /* Maximum number of Nodes          */ 
#endif /* SCSI_MULTIPLEID_SUPPORT */

/* Maximum number of nexus task set handles */
#define  SCSI_MAXNEXUSHANDLES ((0xFFFFFFFF)/(sizeof(SCSI_NEXUS))) 
#define  SCSI_DATA_GROUP_CRC_DEFAULT  0   /* Number of bytes transferred before
                                           * sending/requesting CRC when 
                                           * operating in target mode at 
                                           * DT data group transfer rate.
                                           * A value of 0 means the CRC is
                                           * sent/requested at the end of
                                           * the transfer.
                                           */
#define  SCSI_IU_CRC_DEFAULT  0           /* Number of bytes transferred before
                                           * sending/requesting CRC when 
                                           * operating in target mode at 
                                           * IU DT transfer rate.
                                           * A value of 0 means the iuCRC is
                                           * sent/requested at the end of
                                           * the transfer.
                                           */
#define  SCSI_MAX_CRC_VALUE   65535       /* Maximum CRC interval value supported
                                           * by the interface.
                                           */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_LEGACY_SELECTION_IN      0  /* Legacy selection-in indicator */
#define  SCSI_PACKETIZED_SELECTION_IN  1  /* Packetized selection-in indicator */
 
#define  SCSI_TM_SENSE_IU_SIZE        26  /* Size of area required for a target
                                           * mode generated STATUS IU packet with
                                           * Sense data.
                                           */
#define  SCSI_TM_PKT_FAILURE_IU_SIZE  16  /* Size of area required for a target
                                           * mode generated STATUS IU packet with
                                           * Packetized Failure data.
                                           */
#define  SCSI_TM_SENSE_IU_COUNT        1  /* The number of pre formed STATUS
                                           * IU packets with Sense data required by
                                           * the HWM layer.
                                           */
#define  SCSI_TM_PKT_FAILURE_IU_COUNT  3  /* The number of pre formed STATUS
                                           * IU packets with Packetized Failure data
                                           * required by the HWM layer.
                                           */

#define  SCSI_SENSE_IUCRC_ERROR_INDEX     0  /* Index of preformatted STATUS IU
                                              * packet for returning sense data
                                              * with Sense Key 'Aborted Command'
                                              * and ASC/ASCQ 'INFORMATION UNIT
                                              * iuCRC ERROR DETECTED'.
                                              */
#define  SCSI_PFC_INV_TYPE_INDEX          SCSI_TM_SENSE_IU_COUNT
                                             /* Index of preformatted STATUS IU
                                              * packet for returning 'INVALID TYPE
                                              * CODE RECEIVED IN SPI L_Q
                                              * INFORMATION UNIT' packetized
                                              * failure code.
                                              */
#define  SCSI_PFC_ILLEGAL_REQUEST_INDEX   (SCSI_TM_SENSE_IU_COUNT + 1)
                                             /* Index of preformatted STATUS IU
                                              * packet for returning 'ILLEGAL 
                                              * REQUEST RECEIVED IN SPI L_Q 
                                              * INFORMATION UNIT' packetized
                                              * failure code.
                                              */
#define  SCSI_PFC_TMF_NOT_SUPPORTED_INDEX (SCSI_TM_SENSE_IU_COUNT + 2)
                                             /* Index of preformatted STATUS IU
                                              * packet for returning 'TASK
                                              * MANAGEMENT FUNCTION FAILED'
                                              * packetized failure code.
                                              */
#define  SCSI_GOOD_STATUS_INDEX            0xFF
                                             /* Not really an index value but a 
                                              * special indicator to build good
                                              * status response in exception 
                                              * handling.
                                              */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
typedef struct SCSI_NEXUS_ SCSI_NEXUS; 
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_MODEPTR_UNDEFINED    0xFF    /* Used to indicate an undefined/
                                            * or uninitialized value in
                                            * SCSI_MODE_PTR register.
                                            * Normally set in SCSI_HP_originalMode
                                            * or SCSI_HP_currentMode.
                                            */

/***************************************************************************
* Definitions for memory management 
***************************************************************************/

#if (!SCSI_BIOS_SUPPORT)
/* Memory table not used for HWM BIOS support. */
#if SCSI_DOWNSHIFT_MODE
/* Downshift mode only available to HWM layer. */
#if SCSI_SCBBFR_BUILTIN
#define  SCSI_HM_SCBBFRARRAY        0  /* scb buffer memory             */
#define  SCSI_HM_ACTIVEPTRARRAY     1  /* active pointer array memory   */
#define  SCSI_HM_FREEQUEUE          2  /* scb descriptor list memory    */
#define  SCSI_MAX_MEMORY            3
#else
#define  SCSI_HM_ACTIVEPTRARRAY     0  /* active pointer array memory   */
#define  SCSI_MAX_MEMORY            1
#endif /* SCSI_SCBBFR_BUILTIN */
#else /* !SCSI_DOWNSHIFT_MODE */
#if SCSI_SCBBFR_BUILTIN
#define  SCSI_HM_QOUTPTRARRAY       0  /* sequencer completion queue memory */
#define  SCSI_HM_SCBBFRARRAY        1  /* scb buffer memory             */
#define  SCSI_HM_ACTIVEPTRARRAY     2  /* active pointer array memory   */
#define  SCSI_HM_FREEQUEUE          3  /* scb descriptor list memory    */
#if SCSI_PACKETIZED_IO_SUPPORT
#define  SCSI_HM_SPISTATUS          4  /* SPI Status IU memory          */
#if SCSI_RESOURCE_MANAGEMENT
#define  SCSI_HM_FREEPACKETIZEDSTACK 5    /* packetized stack memory    */
#define  SCSI_HM_FREENONPACKETIZEDSTACK 6 /* non-packetized stack memory */
#if SCSI_TARGET_OPERATION
#define  SCSI_HM_NEXUSQUEUE         7  /* nexus memory                  */
#define  SCSI_HM_NODEQUEUE          8  /* node memory                   */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_HM_TM_SPISTATUS       9  /* Memory for automatic response */
                                       /* to exceptions.                */
#define  SCSI_HM_TM_ERROR_RECOVERY 10  /* Unlocked memory 
                                        * for packetized target mode
                                        * error handling.
                                        */
#define  SCSI_MAX_MEMORY           11
#else
#define  SCSI_MAX_MEMORY            9
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#else /* !SCSI_TARGET_OPERATION */
#define  SCSI_MAX_MEMORY            7
#endif /* SCSI_TARGET_OPERATION */
#else /* !SCSI_RESOURCE_MANAGEMENT */
#if SCSI_TARGET_OPERATION
#define  SCSI_HM_NEXUSQUEUE         5  /* nexus memory                  */
#define  SCSI_HM_NODEQUEUE          6  /* node memory                   */
#define  SCSI_MAX_MEMORY            7
#else 
#define  SCSI_MAX_MEMORY            5
#endif /* SCSI_TARGET_OPERATION */ 
#endif /* SCSI_RESOURCE_MANAGEMENT */
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_RESOURCE_MANAGEMENT
#define  SCSI_HM_FREENONPACKETIZEDSTACK 4 /* non-packetized stack memory */
#if SCSI_TARGET_OPERATION
#define  SCSI_HM_FREEPACKETIZEDSTACK 5 /* packetized stack memory       */
#define  SCSI_HM_NEXUSQUEUE         6  /* nexus memory                  */
#define  SCSI_HM_NODEQUEUE          7  /* node memory                   */
#define  SCSI_MAX_MEMORY            8
#else /* !SCSI_TARGET_OPERATION */
#define  SCSI_MAX_MEMORY            5
#endif /* SCSI_TARGET_OPERATION */
#else /* !SCSI_RESOURCE_MANAGEMENT */
#if SCSI_TARGET_OPERATION
#define  SCSI_HM_NEXUSQUEUE         4  /* nexus memory                  */
#define  SCSI_HM_NODEQUEUE          5  /* node memory                   */
#define  SCSI_MAX_MEMORY            6
#else 
#define  SCSI_MAX_MEMORY            4
#endif /* SCSI_RESOURCE_MANAGEMENT */
#endif /* SCSI_TARGET_OPERATION */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#else /* ! SCSI_SCBBFR_BUILTIN */
#define  SCSI_HM_QOUTPTRARRAY       0  /* sequencer completion queue memory */
#define  SCSI_HM_ACTIVEPTRARRAY     1  /* active pointer array memory   */
#if SCSI_PACKETIZED_IO_SUPPORT
#define  SCSI_HM_SPISTATUS          2 /* SPI Status IU memory          */
#if SCSI_RESOURCE_MANAGEMENT
#define  SCSI_HM_FREEPACKETIZEDSTACK 3    /* packetized stack memory    */
#define  SCSI_HM_FREENONPACKETIZEDSTACK 4 /* non-packetized stack memory */
#if SCSI_TARGET_OPERATION
#define  SCSI_HM_NEXUSQUEUE         5  /* nexus memory                  */
#define  SCSI_HM_NODEQUEUE          6  /* node memory                   */
#define  SCSI_MAX_MEMORY            7
#else /* !SCSI_TARGET_OPERATION */
#define  SCSI_MAX_MEMORY            7
#endif /* SCSI_TARGET_OPERATION */
#else /* !SCSI_RESOURCE_MANAGEMENT */
#if SCSI_TARGET_OPERATION
#define  SCSI_HM_NEXUSQUEUE         3  /* nexus memory                  */
#define  SCSI_HM_NODEQUEUE          4  /* node memory                   */
#define  SCSI_MAX_MEMORY            5
#else 
#define  SCSI_MAX_MEMORY            3
#endif /* SCSI_TARGET_OPERATION */ 
#endif /* SCSI_RESOURCE_MANAGEMENT */
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_TARGET_OPERATION
#define  SCSI_HM_NEXUSQUEUE         2  /* nexus memory                  */
#define  SCSI_HM_NODEQUEUE          3  /* node memory                   */
#define  SCSI_MAX_MEMORY            4
#else 
#define  SCSI_MAX_MEMORY            2
#endif /* SCSI_TARGET_OPERATION */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#endif /* SCSI_SCBBFR_BUILTIN */
#endif /* SCSI_DOWNSHIFT_MODE */
#endif /* (!SCSI_BIOS_SUPPORT) */

/***************************************************************************
*  SCSI_SCBFF definitions
*     This structures must be exactly 64 bytes
***************************************************************************/
typedef struct SCSI_SCBFF_
{
   SCSI_UEXACT32 signature;               /* SCBFF signature               */
   SCSI_UEXACT8  interfaceRev;            /* Interface Revision            */
   SCSI_UEXACT8  infoByte;                /* Information byte              */
   SCSI_UEXACT16 version;                 /* Version number                */
   SCSI_UEXACT16 int13Drive[16];          /* bios int 13 vector table      */
   SCSI_UEXACT16 noEmulationCDROM;        /* Bootable CD-ROM with no emulation (9f) */
   SCSI_UEXACT16 deviceMap;               /* Device bit map 0-15           */
                                          /*   Hi byte- device bitmap 8-15 */
                                          /*   Lo byte- device bitmap 0-7  */
   SCSI_UEXACT8  nvramScbNum;             /* NVRAM located Scb number      */
   SCSI_UEXACT8  alignment1;
   SCSI_UEXACT8  reserved[12];             /* ignored field                 */
   SCSI_UEXACT8  biosSignature[4];        /* BIOS signature                */
   SCSI_UEXACT8  biosVersion[2];          /* BIOS Version number           */
} SCSI_SCBFF;

/* Offsets for SCB FF information */
#define SCSI_SCBFF_SIGNATURE_OFST         0
#define SCSI_SCBFF_INTERFACE_REV_OFST     4
#define SCSI_SCBFF_INFO_BYTE_OFST         5
#define SCSI_SCBFF_VERSION_OFST           6
#define SCSI_SCBFF_INT13_DRIVE_OFST       8
#define SCSI_SCBFF_NO_EMUL_CDROM_OFST     40
#define SCSI_SCBFF_DEVICE_MAP_OFST        42
#define SCSI_SCBFF_NVRAM_SCB_NUM_OFST     44
#define SCSI_SCBFF_RESERVED_OFST          45
#define SCSI_SCBFF_BIOS_SIGNATURE_OFST    58
#define SCSI_SCBFF_BIOS_VERSION_OFST      62

#define SCSI_SCBFF_SIZE                   sizeof(SCSI_SCBFF)

/* Bitmap for Information */
#define SCSI_INFOBYTE_BIOS_INSTALLED   (SCSI_UEXACT8)0x01   /* Bios installed */
#define SCSI_INFOBYTE_IGNORE15_1       (SCSI_UEXACT8)0xFE   /* ignored field  */

/* Bitmap for Version */
#define SCSI_VERSION_MAJOR       (SCSI_UEXACT16)0xF000   /* Version Major     */
#define SCSI_VERSION_MINOR       (SCSI_UEXACT16)0x0FF0   /* Version Minor     */
#define SCSI_VERSION_SUBMINOR    (SCSI_UEXACT16)0x000F   /* Version SubMinor  */

/* Bitmap for DeviceMap */                 
#define SCSI_DEVICEMAP_DRV_0     (SCSI_UEXACT16)0x0001   /* device id 0       */
#define SCSI_DEVICEMAP_DRV_1     (SCSI_UEXACT16)0x0002   /* device id 1       */
#define SCSI_DEVICEMAP_DRV_2     (SCSI_UEXACT16)0x0004   /* device id 2       */
#define SCSI_DEVICEMAP_DRV_3     (SCSI_UEXACT16)0x0008   /* device id 3       */
#define SCSI_DEVICEMAP_DRV_4     (SCSI_UEXACT16)0x0010   /* device id 4       */
#define SCSI_DEVICEMAP_DRV_5     (SCSI_UEXACT16)0x0020   /* device id 5       */
#define SCSI_DEVICEMAP_DRV_6     (SCSI_UEXACT16)0x0040   /* device id 6       */
#define SCSI_DEVICEMAP_DRV_7     (SCSI_UEXACT16)0x0080   /* device id 7       */
#define SCSI_DEVICEMAP_DRV_8     (SCSI_UEXACT16)0x0100   /* device id 8       */
#define SCSI_DEVICEMAP_DRV_9     (SCSI_UEXACT16)0x0200   /* device id 9       */
#define SCSI_DEVICEMAP_DRV_A     (SCSI_UEXACT16)0x0400   /* device id 10      */
#define SCSI_DEVICEMAP_DRV_B     (SCSI_UEXACT16)0x0800   /* device id 11      */
#define SCSI_DEVICEMAP_DRV_C     (SCSI_UEXACT16)0x1000   /* device id 12      */
#define SCSI_DEVICEMAP_DRV_D     (SCSI_UEXACT16)0x2000   /* device id 13      */                                      
#define SCSI_DEVICEMAP_DRV_E     (SCSI_UEXACT16)0x4000   /* device id 14      */
#define SCSI_DEVICEMAP_DRV_F     (SCSI_UEXACT16)0x8000   /* device id 15      */

/* Bitmap for Int13Drive */                 
#define SCSI_INT13DRIVE_ID_MASK     (SCSI_UEXACT16)0x000F /* Target ID number */
#define SCSI_INT13DRIVE_LUN_MASK    (SCSI_UEXACT16)0x00F0 /* LUN number       */
#define SCSI_INT13DRIVE_ATTRIB_MASK (SCSI_UEXACT16)0xFF00 /* attribute byte mask */
#define SCSI_INT13DRIVE_1GB         (SCSI_UEXACT16)0x0100 /* 1 GB translation */
#define SCSI_INT13DRIVE_REMOVABLE   (SCSI_UEXACT16)0x0200 /* removable flag   */
#define SCSI_INT13DRIVE_IGNOREA_E   (SCSI_UEXACT16)0x7C00 /* ignored field    */
#define SCSI_INT13DRIVE_VALID_FLAG  (SCSI_UEXACT16)0x8000 /* data valid flag  */


/***************************************************************************
* SCSI_STATE structures definitions
***************************************************************************/
typedef struct SCSI_STATE_
{
   SCSI_UEXACT8 scbFF[SCSI_MAX_SCBSIZE];
   SCSI_UEXACT8 scratch0[SCSI_SCRATCH0_SIZE];
   SCSI_UEXACT8 scratch1[4][SCSI_SCRATCH1_SIZE];
   SCSI_UEXACT8 scratch2[4][SCSI_SCRATCH2_SIZE];
#if SCSI_STANDARD_MODE
   SCSI_UEXACT8 Bta[SCSI_MAXSCBS+1];         /* busy target array             */
#endif /* SCSI_STANDARD_MODE */
   union
   {
      SCSI_UEXACT16 u16;
      struct 
      {
         SCSI_UEXACT16 chipPause:1;          /* chip was in pause state       */
         SCSI_UEXACT16 interruptEnable:1;    /* status if interrupt enable bit*/
         SCSI_UEXACT16 realMode:1;           /* =1, the state is for real mode*/
#if SCSI_TARGET_OPERATION
         SCSI_UEXACT16 selectionEnable:1;    /* status if selection enable bit*/
#endif /* SCSI_TARGET_OPERATION */
      } bits;
   } flags;
   SCSI_UEXACT16 hnscbQoff;
   SCSI_UEXACT16 snscbQoff;
   SCSI_UEXACT16 sdscbQoff;
   SCSI_UEXACT8 qoffCtlsta;
   SCSI_UEXACT8 iownid;
   SCSI_UEXACT8 townid;
   SCSI_UEXACT8 optionMode;
   SCSI_UEXACT8 scbautoptr;
   SCSI_UEXACT8 intvect1Addr0;
   SCSI_UEXACT8 intvect1Addr1;
   SCSI_UEXACT8 cmcRamBist;
   SCSI_UEXACT8 lqoScsCtl;
   SCSI_UEXACT8 scsChkn;
   SCSI_UEXACT8 alignment[2];
   /* NOTE: Remember to add alignment1[size] field            */
   /* if new field is added and it's out of 4-byte alignment. */

#if SCSI_PACKETIZED_IO_SUPPORT
   SCSI_UEXACT8 lunptr;
   SCSI_UEXACT8 cdblenptr;
   SCSI_UEXACT8 attrptr;
   SCSI_UEXACT8 flagptr;
   SCSI_UEXACT8 cdbptr;
   SCSI_UEXACT8 qnextptr;
   SCSI_UEXACT8 abrtbyteptr;
   SCSI_UEXACT8 abrtbitptr;
   SCSI_UEXACT8 lunlen;
   SCSI_UEXACT8 maxcmd;
   SCSI_UEXACT8 alignment2[2];
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

   SCSI_UEXACT8 simode0;
   SCSI_UEXACT8 simode1;
   SCSI_UEXACT8 simode2;
   SCSI_UEXACT8 simode3;
   SCSI_UEXACT8 lqimode0;
   SCSI_UEXACT8 lqimode1;
   SCSI_UEXACT8 lqctl0;
   SCSI_UEXACT8 lqctl1;
   SCSI_UEXACT8 lqctl2;
   SCSI_UEXACT8 hcntrl;
   SCSI_UEXACT8 dffthrsh;
   SCSI_UEXACT8 dscommand0;
   SCSI_UEXACT8 pcixctl;
   SCSI_UEXACT8 scsiseq0;
   SCSI_UEXACT8 scsiseq1;
   SCSI_UEXACT8 sxfrctl0;
   SCSI_UEXACT8 sxfrctl1;
   SCSI_UEXACT8 seqctl0;
   SCSI_UEXACT8 seqctl1;
   SCSI_UEXACT8 seqintctl;
   SCSI_UEXACT8 intctl;
   SCSI_UEXACT8 sblkctl;
   SCSI_UEXACT8 alignment3[2];

   SCSI_UEXACT32 devConfig0;
   SCSI_UEXACT32 devConfig1;
   SCSI_UEXACT32 statusCommand;
   SCSI_UEXACT32 cacheLatencyHeader;

   /* WARNING: seqRam must always be the last element in this structure for   */        
   /* AP_SaveRestoreSequencer to work correctly.                              */
   SCSI_UEXACT8 seqRam[SCSI_SEQCODE_SIZE];   /* use largest sequence ram size */
} SCSI_STATE;
#define  SCSI_SF_chipPause  flags.bits.chipPause
#define  SCSI_SF_interruptEnable  flags.bits.interruptEnable
#define  SCSI_SF_realMode   flags.bits.realMode
#if SCSI_TARGET_OPERATION
#define  SCSI_SF_selectionEnable  flags.bits.selectionEnable
#endif /* SCSI_TARGET_OPERATION */


/***************************************************************************
* SCSI_TARGET_CONTROL structures definitions
***************************************************************************/
typedef struct SCSI_TARGET_CONTROL_ SCSI_TARGET_CONTROL;
struct SCSI_TARGET_CONTROL_
{
   SCSI_HIOB SCSI_IPTR queueHead;            /* head of waiting queue         */
   SCSI_HIOB SCSI_IPTR queueTail;            /* head of waiting queue         */
   SCSI_UINT16 maxNonTagScbs;                /* maximum non tagged scbs       */
   SCSI_UINT16 maxTagScbs;                   /* maximum tagged scbs           */
   SCSI_UINT16 activeScbs;                   /* scbs associated in use        */
   SCSI_UINT16 origMaxNonTagScbs;            /* save original maxNonTagScbs   */
   SCSI_UINT16 origMaxTagScbs;               /* save original maxTagScbs      */
   SCSI_UINT16 origActiveScbs;               /* scbs associated in use        */
   SCSI_UINT8  activeTRs;                    /* number of active Target Reset */
   SCSI_UINT8  activeABTTSKSETs;             /* number of active ABTTSKSETs   */
   SCSI_UINT8  freezeMap;                    /* freeze device queue bit map   */
#if SCSI_NEGOTIATION_PER_IOB
   SCSI_UINT8  pendingNego;                  /* negotiation is pending        */
#else
   SCSI_UINT8  alignment;                    /* padded for alignment          */
#endif /* SCSI_NEGOTIATION_PER_IOB */
   SCSI_HIOB SCSI_IPTR freezeTail;           /* to remember device queue tail */
                                             /* during freeze logic           */
   SCSI_HIOB SCSI_IPTR freezeHostHead;       /* to remember head of host queue*/
   SCSI_HIOB SCSI_IPTR freezeHostTail;       /* to remember tail of host queue*/
   SCSI_UINT16 activeHiPriScbs;              /* hi priority scbs in rsm layer */
   SCSI_UINT16 deviceQueueHiobs;             /* number of hiobs in device q   */
};

/* definitions for freezeMap */
#define  SCSI_FREEZE_QFULL          0x01     /* queue full                    */
#define  SCSI_FREEZE_TR             0x02     /* target reset                  */
#define  SCSI_FREEZE_ERRORBUSY      0x04     /* freezeOnError flag or busy    */
#define  SCSI_FREEZE_EVENT          0x08     /* freeze/unfreeze async event   */
#define  SCSI_FREEZE_ABTTSKSET      0x10     /* abort task set                */
#define  SCSI_FREEZE_HIPRIEXISTS    0x20     /* hi priority scb handling      */
#define  SCSI_FREEZE_PACKSWITCH     0x40     /* pack to non pack handling     */

#define  SCSI_FREEZE_MAPS           (SCSI_FREEZE_QFULL+SCSI_FREEZE_TR+\
                                     SCSI_FREEZE_ERRORBUSY+SCSI_FREEZE_EVENT+\
                                     SCSI_FREEZE_ABTTSKSET+SCSI_FREEZE_HIPRIEXISTS+\
                                     SCSI_FREEZE_PACKSWITCH)


/***************************************************************************
* SCSI_UNIT_CONTROL structures definitions
***************************************************************************/
struct SCSI_UNIT_CONTROL_
{
   SCSI_HHCB SCSI_HPTR hhcb;                 /* host control block         */
   SCSI_UEXACT8 scsiID;                      /* device scsi id             */
   SCSI_UEXACT8 lunID;                       /* logical unit number        */
   SCSI_UEXACT8 alignment1;                  /* padding for alignment      */
   SCSI_UEXACT8 alignment2;                  /* padding for alignment      */
   SCSI_DEVICE SCSI_DPTR deviceTable;        /* device table               */
   SCSI_TARGET_CONTROL SCSI_HPTR targetControl; /* ptr to target control   */
#if SCSI_PACKETIZED_IO_SUPPORT              
#if (SCSI_TASK_SWITCH_SUPPORT != 1)
   SCSI_UINT8 senseBuffer[252];              /* sense buffer for non-auto*/                                                                                         
                                             /* sense command            */
#endif /* SCSI_TASK_SWITCH_SUPPORT != 1 */  
#endif /* SCSI_PACKETIZED_IO_SUPPORT */   
};

#if SCSI_EFI_BIOS_SUPPORT
#define  SCSI_NULL_UNIT_CONTROL    ((SCSI_UNIT_CONTROL SCSI_UPTR) NULL)
#else
#define  SCSI_NULL_UNIT_CONTROL    ((SCSI_UNIT_CONTROL SCSI_UPTR)-1)
#endif


/***************************************************************************
* SCSI_SHARE structures definitions
***************************************************************************/
typedef struct SCSI_SHARE_
{
   union                               /* pointer to array of active */
   {                                   /* pointer to HIOB            */
      SCSI_UINT32 u32;
      SCSI_HIOB SCSI_IPTR SCSI_HPTR activePointer;
   } activePtrArray;
   union
   {
      SCSI_UINT32 u32;                 /* free scb queue   */
      SCSI_SCB_DESCRIPTOR SCSI_HPTR freePointer;
   } freeQueue;
#if SCSI_RESOURCE_MANAGEMENT
#if (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION)
   union
   {
      SCSI_UINT32 u32;                 /* free stack for packetized scbs */
      SCSI_UEXACT16 SCSI_HPTR freePointer;
   } freePacketizedStack;
#endif /* (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION) */
   union
   {
      SCSI_UINT32 u32;                 /* free stack for non-packetized scbs */
      SCSI_UEXACT16 SCSI_HPTR freePointer;
   } freeNonPacketizedStack;
#endif /* SCSI_RESOURCE_MANAGEMENT */
   union                               /* done queue                 */
   {                                   /* each array element will    */
      SCSI_UINT32 u32;                 /* be one byte (scb number)   */
      SCSI_HIOB SCSI_IPTR SCSI_HPTR donePointer;
   } doneQueue;
#if SCSI_SCBBFR_BUILTIN
   SCSI_SCB_DESCRIPTOR SCSI_HPTR freeHead;    /* head of free scb queue       */
   SCSI_SCB_DESCRIPTOR SCSI_HPTR freeTail;    /* tail of free scb queue       */
   SCSI_UEXACT16 freeScbAvail;                /* number of free scbs available */
#endif /* SCSI_SCBBFR_BUILTIN */
#if SCSI_RESOURCE_MANAGEMENT
   SCSI_HIOB SCSI_IPTR headDone;              /* head of done queue           */
   SCSI_HIOB SCSI_IPTR tailDone;              /* tail of done queue           */
#if (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION)
   SCSI_UEXACT8 freePacketizedPtr;            /* index into packetized stack  */
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
   SCSI_UEXACT8 alignment1;                    /* padding for alignment        */
#endif /* (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION) */
   SCSI_UEXACT8 freeNonPacketizedPtr;         /* index into non-packetized stack*/
   SCSI_UEXACT8 waitFreezeMap;                /* freeze host queue bit map    */ 
   SCSI_UEXACT8 alignment2;                   /* padding for alignment        */
   SCSI_HIOB SCSI_IPTR waitHead;              /* head of waiting queue        */
   SCSI_HIOB SCSI_IPTR waitTail;              /* tail of waiting queue        */
#if SCSI_TARGET_OPERATION
   SCSI_UEXACT16 nonEstScbsAvail;             /* count of scbs that are       */
                                              /* available for non Establish  */
                                              /* Connection use.              */
   SCSI_UEXACT16 estScbsAvail;                /* count of scbs that are       */
                                              /* available for Establish      */
                                              /* Connection use.              */
#endif /* SCSI_TARGET_OPERATION */
#endif /* SCSI_RESOURCE_MANAGEMENT */
} SCSI_SHARE;

#define  SCSI_ACTIVE_PTR_ARRAY     hhcbReserve.shareControl.activePtrArray.activePointer
#define  SCSI_FREE_SCB_AVAIL       hhcbReserve.shareControl.freeScbAvail
#define  SCSI_FREE_HEAD            hhcbReserve.shareControl.freeHead
#define  SCSI_FREE_TAIL            hhcbReserve.shareControl.freeTail
#define  SCSI_FREE_QUEUE           hhcbReserve.shareControl.freeQueue.freePointer
#if SCSI_RESOURCE_MANAGEMENT
#if (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION)
#define  SCSI_FREE_PACKETIZED_STACK hhcbReserve.shareControl.freePacketizedStack.freePointer
#define  SCSI_FREE_PACKETIZED_PTR  hhcbReserve.shareControl.freePacketizedPtr
#endif /* (SCSI_PACKETIZED_IO_SUPPORT || SCSI_TARGET_OPERATION) */
#define  SCSI_FREE_NON_PACKETIZED_STACK hhcbReserve.shareControl.freeNonPacketizedStack.freePointer
#define  SCSI_FREE_NON_PACKETIZED_PTR hhcbReserve.shareControl.freeNonPacketizedPtr
#endif /* SCSI_RESOURCE_MANAGEMENT */
#define  SCSI_DONE_QUEUE           hhcbReserve.shareControl.doneQueue.donePointer
#define  SCSI_DONE_HEAD            hhcbReserve.shareControl.headDone
#define  SCSI_DONE_TAIL            hhcbReserve.shareControl.tailDone
#define  SCSI_WAIT_HEAD            hhcbReserve.shareControl.waitHead
#define  SCSI_WAIT_TAIL            hhcbReserve.shareControl.waitTail
#define  SCSI_WAIT_FREEZEMAP       hhcbReserve.shareControl.waitFreezeMap
#if SCSI_TARGET_OPERATION
#define  SCSI_NON_EST_SCBS_AVAIL   hhcbReserve.shareControl.nonEstScbsAvail
#define  SCSI_EST_SCBS_AVAIL       hhcbReserve.shareControl.estScbsAvail
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_FREE_SCB             SCSI_FREE_HI_SCB

#define  SCSI_ACTPTR               hhcb->SCSI_ACTIVE_PTR_ARRAY

/* under downshift the SCB number is not used to index into active ptr array */
#if SCSI_DOWNSHIFT_MODE
#define  SCSI_ACTPTR_DOWNSHIFT_OFFSET  0
#endif /* SCSI_DOWNSHIFT_MODE */


/***************************************************************************
* SCSI_HHCB_RESERVE structures definitions
***************************************************************************/
typedef struct SCSI_HHCB_RESERVE_
{
   union                               /* queue in pointer array     */
   {                                   /* element depending on       */
      SCSI_UINT32 u32;                 /* operating mode             */
      void SCSI_HPTR qinPointer;
   } qinPtrArray;
   union                               /* queue out pointer array     */
   {                                   /* element depending on       */
      SCSI_UINT32 u32;                 /* operating mode             */
      void SCSI_HPTR qoutPointer;
   } qoutPtrArray;
#if SCSI_SCBBFR_BUILTIN
   SCSI_BUFFER_DESCRIPTOR scbBuffer;   /* scb buffer                 */
#endif /* SCSI_SCBBFR_BUILTIN */
   union                               
   {
      SCSI_UEXACT16 u16;
      struct
      {
         SCSI_UEXACT16 initNeeded:1;         /* initialization needed         */
         SCSI_UEXACT16 autoTerminationMode:2;/* auto termination op mode      */
         SCSI_UEXACT16 expRequest:1;         /* expander status requested     */
         SCSI_UEXACT16 nonExpBreak:1;        /* non expander break int enabled*/
         SCSI_UEXACT16 inISRContext:1;       /* inside interrupt context      */
         SCSI_UEXACT16 biosPresent:1;        /* bios present                  */
         SCSI_UEXACT16 reserved:9;           /* reserved for allignment       */
      } bit;
   } flags;
   union                               
   {
      SCSI_UEXACT16 u16;
      struct
      {
#if !SCSI_BIOS_SUPPORT
         SCSI_UEXACT16 hstIntsCleared:1;     /* hstintstat ints cleared        */
#endif /* !SCSI_BIOS_SUPPORT */
         SCSI_UEXACT16 disconnectOffForHDD:1;/* disable disconnect for HDD     */
         SCSI_UEXACT16 cmdCompleteThresholdSupport:1; /* Set by HIM if cmd     */
                                             /* complete threshold supported   */
#if SCSI_TARGET_OPERATION

#if SCSI_RESOURCE_MANAGEMENT
         SCSI_UEXACT16 estScb:1;             /* set if est SCB is used for     */
                                             /* bus held case                  */
#endif /* SCSI_RESOURCE_MANAGEMENT */
#if SCSI_MULTIPLEID_SUPPORT
         SCSI_UEXACT16 multiIdEnable:1;      /* set if CHIM supports multi id */
                                             /* target mode.                  */
#endif /* SCSI_MULTIPLEID_SUPPORT */

#if (SCSI_RESOURCE_MANAGEMENT && SCSI_MULTIPLEID_SUPPORT)
         SCSI_UEXACT16 reserved:11;          /* reserved for expansion         */
#else
#if (SCSI_RESOURCE_MANAGEMENT || SCSI_MULTIPLEID_SUPPORT)
         SCSI_UEXACT16 reserved:12;          /* reserved for expansion         */
#else
         SCSI_UEXACT16 reserved:13;          /* reserved for expansion         */
#endif /* (SCSI_RESOURCE_MANAGEMENT || SCSI_MULTIPLEID_SUPPORT) */
#endif /* (SCSI_RESOURCE_MANAGEMENT && SCSI_MULTIPLEID_SUPPORT) */

#else /* !SCSI_TARGET_OPERATION */

#if !SCSI_BIOS_SUPPORT
         SCSI_UEXACT16 reserved:13;          /* reserved for expansion         */
#else
         SCSI_UEXACT16 reserved:14;          /* reserved for expansion         */
#endif /* !SCSI_BIOS_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */
      } bitMore;
   } flagsMore;
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
   union
   {
      SCSI_UEXACT32 u32;             
      void SCSI_HPTR spiStatus;              /* SPI status pointer            */
   } iuStatus;
   SCSI_UEXACT16 iuScbNumber[2];             /* iuStatus SCB number            */
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */
   SCSI_UEXACT16 hnscbQoff;                  /* hnscbQoff copy for HIM        */
   SCSI_UEXACT16 qDoneElement;               /* q_done_element for HIM        */
   SCSI_UEXACT8 qDonePass;                   /* q_done_pass copy for HIM      */
   SCSI_UEXACT8 originalMode;                /* mode when the chip interrupts */
   SCSI_UEXACT8 currentMode;                 /* current value in the          */
                                             /* SCSI_MODE_PTR register        */
   SCSI_UEXACT8 hstintstat;                  /* interrupt state saved         */
   SCSI_UEXACT8 hcntrl;                      /* host control register         */
   SCSI_UEXACT8 qoutcnt;                     /* qoutcnt saved                 */
   SCSI_UEXACT8 sblkctl;                     /* scsi block control register   */
   SCSI_UEXACT8 alignment1;                  /* padding for alignment         */
#if (SCSI_MULTI_MODE || SCSI_MULTI_DOWNSHIFT_MODE)
   SCSI_FIRMWARE_DESCRIPTOR SCSI_LPTR firmwareDescriptor; /* firmware descriptor  */
#endif /* (SCSI_MULTI_MODE || SCSI_MULTI_DOWNSHIFT_MODE) */
#if SCSI_MULTI_HARDWARE
   SCSI_HARDWARE_DESCRIPTOR SCSI_LPTR hardwareDescriptor;  /* hardware descriptor  */
#endif /* SCSI_MULTI_HARDWARE */
#if SCSI_RESOURCE_MANAGEMENT
   SCSI_TARGET_CONTROL targetControl[SCSI_MAXDEV]; /* target control table    */
#endif /* SCSI_RESOURCE_MANAGEMENT */
   SCSI_SHARE shareControl;/* shared control              */
#if SCSI_TARGET_OPERATION
   SCSI_NODE SCSI_NPTR nodeTable[SCSI_MAXDEV];   /* Node table                 */
   SCSI_NEXUS SCSI_XPTR nexusHoldingBus;         /* Nexus Holding the SCSI bus */
#if SCSI_SCBBFR_BUILTIN
   SCSI_SCB_DESCRIPTOR SCSI_HPTR busHeldScbDescriptor;
                                                 /* Scb descriptor being used  */
                                                 /* for a target mode bus held */
                                                 /* case.                      */
#endif /* SCSI_SCBBFR_BUILTIN */
#if SCSI_MULTIPLEID_SUPPORT
   SCSI_HIOB SCSI_IPTR disableHiob;              /* HIOB associated with a     */
                                                 /* DISABLE_ID request         */ 
#endif /* SCSI_MULTIPLEID_SUPPORT */
#if (SCSI_PACKETIZED_TARGET_MODE_SUPPORT && SCSI_PACKETIZED_IO_SUPPORT)
   union
   {
      SCSI_UEXACT32 u32;             
      void SCSI_HPTR tmSpiStatus;               /* IU status packets pointer   */
   } tmIUStatus;
   union
   {
      SCSI_UEXACT32 u32;             
      void SCSI_HPTR tmErrorUnlocked;           /* Unlocked memory for         */
                                                /* packetized error handling   */
   } tmUnlocked;
   SCSI_HIOB SCSI_IPTR errorHiob;               /* HIOB for packetized target  */
                                                /* mode error handling         */
   SCSI_NEXUS SCSI_XPTR errorNexus;             /* Nexus for packetized target */
                                                /* mode error handling         */
   SCSI_SCB_DESCRIPTOR SCSI_HPTR errorScbDescriptor; /* SCB descriptor for     */
                                                /* packetized target mode      */
                                                /* error handling              */
#endif /* (SCSI_PACKETIZED_TARGET_MODE_SUPPORT && SCSI_PACKETIZED_IO_SUPPORT) */
#if SCSI_RESOURCE_MANAGEMENT
   SCSI_UEXACT16 busHeldScbNumber;               /* scb # being used for a     */
                                                 /* target mode bus held case  */
   SCSI_UEXACT8  alignment2[2];                  /* padding for alignment      */
#endif /* SCSI_RESOURCE_MANAGEMENT */
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_BIOS_SUPPORT
   SCSI_UEXACT8 SCSI_HPTR seqAddress;            /* BIOS to define sequencer   */
                                                 /* start address              */
   SCSI_UEXACT16  seqSize;                       /* BIOS to define sequencer   */
                                                 /* size                       */
#endif /* SCSI_BIOS_SUPPORT */
} SCSI_HHCB_RESERVE;
#if SCSI_BIOS_SUPPORT
#define  SCSI_HP_seqSize            hhcbReserve.seqSize
#define  SCSI_HP_seqAddress         hhcbReserve.seqAddress
#endif /* SCSI_BIOS_SUPPORT */
#define  SCSI_HP_firmwareDescriptor hhcbReserve.firmwareDescriptor
#define  SCSI_HP_hardwareDescriptor hhcbReserve.hardwareDescriptor
#define  SCSI_HP_hnscbQoff          hhcbReserve.hnscbQoff
#define  SCSI_HP_qDoneElement       hhcbReserve.qDoneElement
#define  SCSI_HP_qDonePass          hhcbReserve.qDonePass   
#define  SCSI_HP_originalMode       hhcbReserve.originalMode  
#define  SCSI_HP_currentMode        hhcbReserve.currentMode
#define  SCSI_HP_hstintstat         hhcbReserve.hstintstat  
#define  SCSI_HP_hcntrl             hhcbReserve.hcntrl   
#define  SCSI_HP_qoutcnt            hhcbReserve.qoutcnt  
#define  SCSI_HP_sblkctl            hhcbReserve.sblkctl
#define  SCSI_HP_targetControl      hhcbReserve.targetControl
#define  SCSI_HP_initNeeded         hhcbReserve.flags.bit.initNeeded
#define  SCSI_HP_autoTerminationMode hhcbReserve.flags.bit.autoTerminationMode
#define  SCSI_HP_expRequest         hhcbReserve.flags.bit.expRequest
#define  SCSI_HP_nonExpBreak        hhcbReserve.flags.bit.nonExpBreak
#define  SCSI_HP_inISRContext       hhcbReserve.flags.bit.inISRContext
#define  SCSI_HP_biosPresent        hhcbReserve.flags.bit.biosPresent
#if !SCSI_BIOS_SUPPORT
#define  SCSI_HP_hstIntsCleared     hhcbReserve.flagsMore.bitMore.hstIntsCleared
#endif /* !SCSI_BIOS_SUPPORT */
#define  SCSI_HP_disconnectOffForHDD hhcbReserve.flagsMore.bitMore.disconnectOffForHDD
#define  SCSI_HF_cmdCompleteThresholdSupport hhcbReserve.flagsMore.bitMore.cmdCompleteThresholdSupport

#define  SCSI_QOUT_PTR_ARRAY        hhcbReserve.qoutPtrArray.qoutPointer
#define  SCSI_QOUT_PTR_ARRAY_NEW(hhcb) ((SCSI_QOUTFIFO_NEW SCSI_HPTR) (hhcb)->SCSI_QOUT_PTR_ARRAY)
#define  SCSI_SCB_BUFFER            hhcbReserve.scbBuffer
#if (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT)
#define  SCSI_SPI_STATUS            hhcbReserve.iuStatus.spiStatus
#define  SCSI_SPI_STATUS_PTR(hhcb)  ((SCSI_UEXACT8 SCSI_HPTR) (hhcb)->SCSI_SPI_STATUS)
#define  SCSI_SPI_STATUS_IU_SCB     hhcbReserve.iuScbNumber
#endif /* (SCSI_INITIATOR_OPERATION && SCSI_PACKETIZED_IO_SUPPORT) */
#if SCSI_TARGET_OPERATION
#define  SCSI_HP_nodeTable          hhcbReserve.nodeTable
#define  SCSI_HP_nexusHoldingBus    hhcbReserve.nexusHoldingBus 
#if SCSI_SCBBFR_BUILTIN
#define  SCSI_HP_busHeldScbDescriptor hhcbReserve.busHeldScbDescriptor
#endif /* SCSI_SCBBFR_BUILTIN */
#if SCSI_MULTIPLEID_SUPPORT
#define  SCSI_HP_multiIdEnable      hhcbReserve.flagsMore.bitMore.multiIdEnable
#define  SCSI_HP_disableHiob        hhcbReserve.disableHiob 
#endif /* SCSI_MULTIPLEID_SUPPORT */
#if SCSI_RESOURCE_MANAGEMENT
#define  SCSI_HP_estScb             hhcbReserve.flagsMore.bitMore.estScb
#define  SCSI_HP_busHeldScbNumber   hhcbReserve.busHeldScbNumber
#endif /* SCSI_RESOURCE_MANAGEMENT */
#if (SCSI_PACKETIZED_TARGET_MODE_SUPPORT && SCSI_PACKETIZED_IO_SUPPORT)
#define  SCSI_TM_IU_STATUS_PKTS     hhcbReserve.tmIUStatus.tmSpiStatus
#define  SCSI_TM_ERROR_UNLOCKED     hhcbReserve.tmUnlocked.tmErrorUnlocked 
#define  SCSI_HP_errorHiob          hhcbReserve.errorHiob
#define  SCSI_HP_errorNexus         hhcbReserve.errorNexus
#define  SCSI_HP_errorScbDescriptor hhcbReserve.errorScbDescriptor
#endif /* (SCSI_PACKETIZED_TARGET_MODE_SUPPORT && SCSI_PACKETIZED_IO_SUPPORT) */
#endif /* SCSI_TARGET_OPERATION */

/***************************************************************************
* SCSI_HIOB_RESERVE structures definitions
***************************************************************************/
#define  SCSI_MAX_WORKAREA   10              /* maximum working area length   */
typedef struct SCSI_HIOB_RESERVE_
{
   SCSI_UEXACT8 mgrStat;                     /* manager status                */
   SCSI_UEXACT8 negoState;                   /* state of negotiation process  */
   SCSI_UEXACT8 ackLastMsg;                  /* flag to check if ack is needed*/
   SCSI_UEXACT8 workArea[SCSI_MAX_WORKAREA]; /* protocol working area         */
#if SCSI_TARGET_OPERATION
   union 
   {
      SCSI_UEXACT8 u8;
      struct
      {
         SCSI_UEXACT8 targetMode:1;          /* when set to 1 indicates that  */
                                             /* this is a target mode HIOB    */
         SCSI_UEXACT8 estScb:1;              /* when set to 1 indicates that  */
                                             /* the scb being used for this   */
                                             /* HIOB is an establish          */
                                             /* connection scb. An est scbd   */
                                             /* is used when an establish     */
                                             /* connection completes holding  */
                                             /* the SCSI bus. The purpose     */
                                             /* is to know which queue to     */
                                             /* return the scb when the HIOB  */
                                             /* is complete.                  */
         SCSI_UEXACT8 estQueuedToHwm:1;      /* Establish Connection HIOB     */
                                             /* queued to hardware.           */
      } bits;
   } flags;
   SCSI_UEXACT8 alignment[2];                /* padded for alignment          */
#else
   SCSI_UEXACT8 alignment[3];                /* padded for alignment          */
#endif /* SCSI_TARGET_OPERATION */         
} SCSI_HIOB_RESERVE;

/* definitions for negoState */
#define  SCSI_NOT_NEGOTIATING   0            /* not in the state of nego      */
#define  SCSI_INITIATE_WIDE     1            /* initiate wide negotiation     */
#define  SCSI_INITIATE_SYNC     2            /* initiate sync negotiation     */
#define  SCSI_RESPONSE_WIDE     3            /* respond to wide negotiation   */
#define  SCSI_RESPONSE_SYNC     4            /* respond to sync negotiation   */
#define  SCSI_INITIATE_PPR      5      /* initiate Parallel Protocol Request  */
#define  SCSI_RESPONSE_PPR      6      /* respond to Parallel Protocol Request*/
#define  SCSI_NEGOTIATE_NEEDED  7            /* negotiation needed            */
#define  SCSI_RESPONSE_MSG07_WIDE   8 /* respond to wide nego after msg reject*/
#define  SCSI_RESPONSE_MSG07_SYNC   9 /* respond to sync nego after msg reject*/

/* definitions for number of bytes in SDTR, WDTR and PPR message */
#define  SCSI_SDTR_MSG_LENGTH    5
#define  SCSI_WDTR_MSG_LENGTH    4
#define  SCSI_PPR_MSG_LENGTH     8

/* definitions for number of bytes for other messages */
#define  SCSI_IWR_MSG_LENGTH   2        /* Ignore Wide Residue */
 
/* definitions for PPR's protocol options field */
#define  SCSI_IU_REQ             0x01
#define  SCSI_DT_REQ             0x02
#define  SCSI_QAS_REQ            0x04
#define  SCSI_HOLD_MCS           0x08
#define  SCSI_WR_FLOW            0x10
#define  SCSI_RD_STRM            0x20
#define  SCSI_RTI                0x40
#define  SCSI_PCOMP_EN           0x80
#define  SCSI_MAXPROTOCOL_MASK_DEFAULT 0x000000BF

/* definitions for ackLastMsg */
#define  SCSI_ACK_NOT_NEEDED  0
#define  SCSI_ACK_NEEDED      1

#define  SCSI_IP_mgrStat      hiobReserve.mgrStat
#define  SCSI_IP_negoState    hiobReserve.negoState
#define  SCSI_IP_ackLastMsg   hiobReserve.ackLastMsg
#define  SCSI_IP_workArea     hiobReserve.workArea
#if SCSI_TARGET_OPERATION
#define  SCSI_IP_targetMode   hiobReserve.flags.bits.targetMode
#define  SCSI_IP_estScb       hiobReserve.flags.bits.estScb 
#define  SCSI_IP_estQueuedToHwm  hiobReserve.flags.bits.estQueuedToHwm
#define  SCSI_IP_flags        hiobReserve.flags.u8

/* definitions for Establish Connection fields saved in */
/* hiob workarea                                        */
#define  SCSI_EST_IID         0
#define  SCSI_EST_IDMSG       1
#define  SCSI_EST_FLAGS       2
#define  SCSI_EST_LASTBYTE    3 
#define  SCSI_EST_STATUS      4 

/* definitions for SCSI_EST_STATUS */
#define SCSI_EST_GOOD_STATE   0x00
#define SCSI_EST_SEL_STATE    0x01  /* exception during selection phase   */
#define SCSI_EST_ID_STATE     0x02  /*     "        "   identifying phase */
#define SCSI_EST_MSGOUT_STATE 0x03  /*     "        "   message out phase */
#define SCSI_EST_CMD_STATE    0x04  /*     "        "   command phase     */
#define SCSI_EST_DISC_STATE   0x05  /*     "        "   disconnect phase  */
#define SCSI_EST_VENDOR_CMD   0x08  /* Vendor unique command              */

/* definition of status mask */
#define SCSI_EST_STATUS_MASK  0x0F
 
/* definitions for flag fields of SCSI_EST_STATUS */
#define SCSI_EST_LAST_RESOURCE 0x40  /* last resource used */
#define SCSI_EST_PARITY_ERR    0x80  /* parity error detected */
 
#endif /* SCSI_TARGET_MODE */ 

/****************************************************************************
* definitions for mgrStat of HIOB
****************************************************************************/
#define  SCSI_MGR_PROCESS     0        /* SCB needs to be processed           */
#define  SCSI_MGR_DONE        1        /* SCB finished without error (hwm)    */
#define  SCSI_MGR_DONE_ABT    2        /* SCB finished due to abort from host */
#define  SCSI_MGR_DONE_ERR    3        /* SCB finished with error             */
#define  SCSI_MGR_DONE_ILL    4        /* SCB finished due to illegal command */
#define  SCSI_MGR_ABORTINPROG 5        /* Abort special function in progress  */
#define  SCSI_MGR_TR          6        /* Target Reset special function       */
#define  SCSI_MGR_AUTOSENSE   7        /* SCB w/autosense in progress         */
#define  SCSI_MGR_ABORTED     8        /* aborted                             */
#define  SCSI_MGR_ABORTINREQ  9        /* Abort HIOB in request               */
#define  SCSI_MGR_ENQUE_TR    10       /* Enqueue Target Reset flag           */
#if SCSI_TARGET_OPERATION
#define  SCSI_MGR_IGNOREWIDEINPROG 11  /* Ignore Wide Residue msg in progress */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_MGR_AUTORESPONSE 12      /* Auto response SCB in progress       */
#define  SCSI_MGR_BADLQT_AUTORESPONSE 13 /* Auto response Bad LQ Type SCB in  */
                                       /* progress                            */       
#define  SCSI_MGR_LQOATNPKT   14       /* LQOATNPKT interrupt handling        */
                                       /* required.                           */
#define  SCSI_MGR_LQOATNLQ    15       /* LQOATNLQ interrupt handling         */
                                       /* required.                           */
#define  SCSI_MGR_LQOTCRC_AUTORESPONSE 16 /* LQOTCRC interrupt handling       */
                                       /* required.                           */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */ 
#endif /* SCSI_TARGET_OPERATION */

/***************************************************************************
* SCSI_SCB_DESCRIPTOR structure definition
***************************************************************************/
struct SCSI_SCB_DESCRIPTOR_
{
   SCSI_UEXACT16 scbNumber;                /* scb Number                    */
   SCSI_UEXACT8 reserved[2];              /* for alignment                 */
   SCSI_BUFFER_DESCRIPTOR scbBuffer;      /* scb buffer descriptor         */
   SCSI_SCB_DESCRIPTOR SCSI_HPTR queueNext;    /* next scb queue element   */
};

#if SCSI_EFI_BIOS_SUPPORT
#define  SCSI_NULL_SCB_DESCRIPTOR    ((SCSI_SCB_DESCRIPTOR SCSI_HPTR) NULL)
#else
#define  SCSI_NULL_SCB_DESCRIPTOR    ((SCSI_SCB_DESCRIPTOR SCSI_HPTR) -1)
#endif

#if SCSI_TARGET_OPERATION
/***************************************************************************
* SCSI_NEXUS_RESERVE structures definitions
***************************************************************************/
typedef struct SCSI_NEXUS_RESERVE_
{
   union
   {
      SCSI_UEXACT8 u8;
      struct
      {
         SCSI_UEXACT8 abortInProgress:1;      /* abort in progress             */  
         SCSI_UEXACT8 deviceResetInProgress:1;/* device reset in progress      */ 
         SCSI_UEXACT8 nexusActive:1;          /* nexus active i.e. not avail   */   
         SCSI_UEXACT8 parityErr:1;    
         SCSI_UEXACT8 taskManagementFunction:1;
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
         SCSI_UEXACT8 iuReqChange:1;          /* Protocol option IU_REQ      */
                                              /* changed - requires nexus to */
                                              /* be invalidated.             */  
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#if SCSI_RESOURCE_MANAGEMENT
         SCSI_UEXACT8 hiobActive:1;           /* used by SCSIClearNexus for    */
                                              /* clearing HIOBs                */ 
#endif /* SCSI_RESOURCE_MANAGEMENT */
      } bits;
   } nexusFlags;
   SCSI_UEXACT8 lunMask;
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
   SCSI_UEXACT8 tmfCode;                      /* task management fuction       */
   SCSI_UEXACT8 alignment;                    /* padded for alignment          */
#else
   SCSI_UEXACT8 alignment[2];                 /* padded for alignment          */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
 } SCSI_NEXUS_RESERVE;

#define  SCSI_XP_abortInProgress       nexusReserve.nexusFlags.bits.abortInProgress
#define  SCSI_XP_deviceResetInProgress nexusReserve.nexusFlags.bits.deviceResetInProgress
#define  SCSI_XP_parityErr             nexusReserve.nexusFlags.bits.parityErr
#define  SCSI_XP_nexusActive           nexusReserve.nexusFlags.bits.nexusActive
#define  SCSI_XP_taskManagementFunction  nexusReserve.nexusFlags.bits.taskManagementFunction
#define  SCSI_XP_lunMask               nexusReserve.lunMask
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_XP_iuReqChange           nexusReserve.nexusFlags.bits.iuReqChange
#define  SCSI_XP_tmfCode               nexusReserve.tmfCode
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

#if SCSI_RESOURCE_MANAGEMENT
#define  SCSI_XP_hiobActive            nexusReserve.nexusFlags.bits.hiobActive
#endif /* SCSI_RESOURCE_MANAGEMENT */

/* lunmask field defines */
#define  SCSI_3BIT_LUN_MASK      0x07
#define  SCSI_5BIT_LUN_MASK      0x1f
#define  SCSI_6BIT_LUN_MASK      0x3f
#define  SCSI_8BIT_LUN_MASK      0xFF

/***************************************************************************
* SCSI_NODE_RESERVE structures definitions
***************************************************************************/
typedef struct SCSI_NODE_RESERVE_
{
   union 
   {
      SCSI_UEXACT8 u8;
      struct 
      {
         SCSI_UEXACT8 forceNegotiation:1; /* used only if initiator  */ 
                                          /* mode is enabled. Force  */
                                          /* target negotiation.     */
                                          /* Special bit used when a */
                                          /* check condition is      */
                                          /* received on an          */
                                          /* Initiator I/O.          */
         SCSI_UEXACT8 negotiated:1;     /* negotiated with initiator */ 
         SCSI_UEXACT8 retry:1;          /* retry the SCSI phase      */
      } bits;
   } flags;
   SCSI_UEXACT8  scsiState;             /* current SCSI state        */
   SCSI_UEXACT8  scsiAtnState;          /* last SCSI state after ATN */
   SCSI_UEXACT8  scsiMsgSize;           /* last message in size      */
   SCSI_UEXACT8  scsiMsgIn[8];          /* last entire message       */
 } SCSI_NODE_RESERVE;

#define  SCSI_NP_negotiated           nodeReserve.flags.bits.negotiated
#define  SCSI_NP_forceTargetNegotiation nodeReserve.flags.bits.forceNegotiation
#define  SCSI_NP_retry                nodeReserve.flags.bits.retry
#define  SCSI_NP_scsiState            nodeReserve.scsiState
#define  SCSI_NP_scsiAtnState         nodeReserve.scsiAtnState
#define  SCSI_NP_scsiMsgSize          nodeReserve.scsiMsgSize
#define  SCSI_NP_scsiMsgIn            nodeReserve.scsiMsgIn
#endif /* SCSI_TARGET_OPERATION */



