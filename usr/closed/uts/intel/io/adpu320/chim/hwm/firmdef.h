/*$Header: /vobs/u320chim/src/chim/hwm/firmdef.h   /main/88   Wed Mar 19 21:45:06 2003   spal3094 $*/

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
*  Module Name:   FIRMDEF.H
*
*  Description:   Definitions for firmware dependent structures 
*    
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         1. Definitions for SCB layout
*                 2. Depending on hardware or mode of operation the layout
*                    of SCB may be different. The layout is defined in
*                    hardware management layer. It's translation management
*                    layer's responsibility to make sure SCB information
*                    get filled properly.
*                 3. Modify CUSTOM.H to enable/disable a particular scb
*                    format
*
***************************************************************************/

#if defined(SCSI_DATA_DEFINE)
#if ((SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320_MODE) && (!SCSI_BIOS_SUPPORT))
#include "seqs320.h"
#endif /* ((SCSI_STANDARD_U320_MODE || SCSI_DOWNSHIFT_U320) && (!SCSI_BIOS_SUPPORT)) */
#if ((SCSI_STANDARD_ENHANCED_U320_MODE || SCSI_DOWNSHIFT_ENHANCED_U320_MODE) && (!SCSI_BIOS_SUPPORT))
#include "seqse320.h"
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE || SCSI_DOWNSHIFT_ENHANCED_U320_MODE */
#if SCSI_DCH_U320_MODE
#include "dchs320.h"
#endif /* SCSI_DCH_U320_MODE */
#endif /* defined(SCSI_DATA_DEFINE) */

/***************************************************************************
*  Standard Ultra 320 SCB format layout
***************************************************************************/
#if SCSI_STANDARD_U320_MODE

#if !defined(SCSI_INTERNAL)
/* This version is used when the compiler performs alignment
 * of elements in structures (especially unions).
 * All elements are single byte to avoid realignment. 
 */
/* Any new SCB fields or defines must be added to both the 
 * !defined(SCSI_INTERNAL) and defined(SCSI_INTERNAL) versions.
 */
/* structure definition */
typedef struct SCSI_SCB_STANDARD_U320_
{
   SCSI_UEXACT8      scbByte0;            /* next SCB address for 8 bytes  */
                                          /* OR q_exetarg_next for 2 bytes */
   SCSI_UEXACT8      scbByte1;
   SCSI_UEXACT8      scbByte2;
   SCSI_UEXACT8      scbByte3;
   SCSI_UEXACT8      scbByte4;
   SCSI_UEXACT8      scbByte5;
   SCSI_UEXACT8      scbByte6;
   SCSI_UEXACT8      scbByte7;
   SCSI_UEXACT8      scbByte8;            /* SCSI CDB for 12 bytes OR      */
                                          /* cdb pointer for 8 bytes OR    */
                                          /* residue for 3 bytes.          */
   SCSI_UEXACT8      scbByte9;
   SCSI_UEXACT8      scbByte10;           /* residue for 3 bytes.          */
   SCSI_UEXACT8      scbByte11;           /* working flags                 */
   SCSI_UEXACT8      scbByte12;
   SCSI_UEXACT8      scbByte13;
   SCSI_UEXACT8      scbByte14;
   SCSI_UEXACT8      scbByte15;           /* working cache                 */
   SCSI_UEXACT8      scbByte16;           /* working s/g list pointer for  */
   SCSI_UEXACT8      scbByte17;           /* 4 bytes                       */
   SCSI_UEXACT8      scbByte18;
   SCSI_UEXACT8      scbByte19;
   SCSI_UEXACT8      scbByte20;           /* array_site OR q_next          */
   SCSI_UEXACT8      scbByte21;           /* array_site OR q_next          */
   SCSI_UEXACT8      atn_length;          /* attention management          */
   SCSI_UEXACT8      scdb_length;         /* cdb length                    */
#if (OSD_BUS_ADDRESS_SIZE == 64)
   SCSI_UEXACT8      saddress0;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress1;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress2;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress3;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress4;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress5;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress6;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress7;           /* 1st s/g segment               */
   SCSI_UEXACT8      slength0;            /* 1st s/g segment               */
   SCSI_UEXACT8      slength1;            /* 1st s/g segment               */
   SCSI_UEXACT8      slength2;            /* 1st s/g segment               */
#else
   SCSI_UEXACT8      saddress0;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress1;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress2;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress3;           /* 1st s/g segment               */
   SCSI_UEXACT8      slength0;            /* 1st s/g segment               */
   SCSI_UEXACT8      slength1;            /* 1st s/g segment               */
   SCSI_UEXACT8      slength2;            /* 1st s/g segment               */
   SCSI_UEXACT8      padding0;            /* 1st s/g segment               */
   SCSI_UEXACT8      padding1;            /* 1st s/g segment               */
   SCSI_UEXACT8      padding2;            /* 1st s/g segment               */
   SCSI_UEXACT8      padding3;            /* 1st s/g segment               */
#endif /* OSD_BUS_ADDRESS_SIZE */
   SCSI_UEXACT8      task_attribute;      /* Cmd IU Task Attribute OR      */
                                          /* stypecode for reestablish_SCB */
   SCSI_UEXACT8      task_management;     /* Cmd IU Task Management Flags  */
   SCSI_UEXACT8      SCB_flags;           /* Flags                         */
   SCSI_UEXACT8      scontrol;            /* scb control bits              */
   SCSI_UEXACT8      sg_cache_SCB;        /* # of SCSI_UEXACT8s for each element */
   SCSI_UEXACT8      scbByte40;           /* pointer to s/g list for 8     */
                                          /* OR special_opcode for 1 byte  */
   SCSI_UEXACT8      scbByte41;           /* special_info                  */
   SCSI_UEXACT8      scbByte42;
   SCSI_UEXACT8      scbByte43;
   SCSI_UEXACT8      scbByte44;
   SCSI_UEXACT8      scbByte45;
   SCSI_UEXACT8      scbByte46;
   SCSI_UEXACT8      scbByte47;
   SCSI_UEXACT8      slun0;               /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun1;               /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun2;               /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun3;               /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun4;               /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun5;               /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun;                /* lun                           */
   SCSI_UEXACT8      starget;             /* target ID OR our ID for       */
                                          /* reestablish_SCB               */
   SCSI_UEXACT8      mirror_SCB;          /* SCB for mirrored operation    */
   SCSI_UEXACT8      mirror_SCB1;         /* SCB for mirrored operation    */
   SCSI_UEXACT8      mirror_slun;         /* Mirrored LUN                  */
   SCSI_UEXACT8      mirror_starget;      /* Mirrored target               */
   SCSI_UEXACT8      scontrol1;           /* scb control1 bits             */
   SCSI_UEXACT8      reserved[2];         /* reserved                      */
   SCSI_UEXACT8      busy_target;         /* busy target                   */
} SCSI_SCB_STANDARD_U320;

/* These scb field defines must only be used as offsets within the
 * scb. There is no field size associated with the define.
 */
#define  SCSI_SU320_sg_pointer_SCB0       scbByte40
#define  SCSI_SU320_sg_pointer_SCB1       scbByte41
#define  SCSI_SU320_sg_pointer_SCB2       scbByte42
#define  SCSI_SU320_sg_pointer_SCB3       scbByte43
#define  SCSI_SU320_sg_pointer_SCB4       scbByte44
#define  SCSI_SU320_sg_pointer_SCB5       scbByte45
#define  SCSI_SU320_sg_pointer_SCB6       scbByte46
#define  SCSI_SU320_sg_pointer_SCB7       scbByte47

#define  SCSI_SU320_special_opcode        scbByte40
#define  SCSI_SU320_special_info          scbByte41

#define  SCSI_SU320_scdb                  scbByte8
#define  SCSI_SU320_scdb0                 scbByte8 
#define  SCSI_SU320_scdb1                 scbByte9
#define  SCSI_SU320_scdb2                 scbByte10
#define  SCSI_SU320_scdb3                 scbByte11
#define  SCSI_SU320_scdb4                 scbByte12
#define  SCSI_SU320_scdb5                 scbByte13
#define  SCSI_SU320_scdb6                 scbByte14
#define  SCSI_SU320_scdb7                 scbByte15
#define  SCSI_SU320_scdb8                 scbByte16
#define  SCSI_SU320_scdb9                 scbByte17
#define  SCSI_SU320_scdb10                scbByte18
#define  SCSI_SU320_scdb11                scbByte19

#define  SCSI_SU320_scdb_pointer          scbByte8

#define  SCSI_SU320_scdb_pointer0         scbByte8
#define  SCSI_SU320_scdb_pointer1         scbByte9
#define  SCSI_SU320_scdb_pointer2         scbByte10
#define  SCSI_SU320_scdb_pointer3         scbByte11
#define  SCSI_SU320_scdb_pointer4         scbByte12
#define  SCSI_SU320_scdb_pointer5         scbByte13
#define  SCSI_SU320_scdb_pointer6         scbByte14
#define  SCSI_SU320_scdb_pointer7         scbByte15

#define  SCSI_SU320_sdata_residue0        scbByte8
#define  SCSI_SU320_sdata_residue1        scbByte9
#define  SCSI_SU320_sdata_residue2        scbByte10

#define  SCSI_SU320_sg_cache_work         scbByte15
#define  SCSI_SU320_sg_pointer_work0      scbByte16
#define  SCSI_SU320_sg_pointer_work1      scbByte17
#define  SCSI_SU320_sg_pointer_work2      scbByte18
#define  SCSI_SU320_sg_pointer_work3      scbByte19

#if SCSI_TARGET_OPERATION
#define  SCSI_SU320_sinitiator            scbByte8
#define  SCSI_SU320_starg_tagno           scbByte9
#define  SCSI_SU320_starg_status          scbByte11
#define  SCSI_SU320_stypecode             task_attribute
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_SU320_q_next                scbByte20
#define  SCSI_SU320_array_site            scbByte20

#define  SCSI_SU320_q_exetarg_next        scbByte0
#define  SCSI_SU320_next_SCB_addr0        scbByte0
#define  SCSI_SU320_next_SCB_addr1        scbByte1
#define  SCSI_SU320_next_SCB_addr2        scbByte2
#define  SCSI_SU320_next_SCB_addr3        scbByte3
#define  SCSI_SU320_next_SCB_addr4        scbByte4
#define  SCSI_SU320_next_SCB_addr5        scbByte5
#define  SCSI_SU320_next_SCB_addr6        scbByte6
#define  SCSI_SU320_next_SCB_addr7        scbByte7

#else /* defined(SCSI_INTERNAL) */ 
/* Any new SCB fields or defines must be added to both the 
 * !defined(SCSI_INTERNAL) and defined(SCSI_INTERNAL) versions.
 */
 /* structure definition */
typedef struct SCSI_SCB_STANDARD_U320_
{
   union
   {
      struct
      {
         SCSI_UEXACT8      next_SCB_addr0;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr1;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr2;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr3;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr4;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr5;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr6;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr7;   /* next SCB address              */
      } nextScbAddr;
      struct
      {
         SCSI_UEXACT16     q_exetarg_next;   /* EXE queue per target          */
         SCSI_UEXACT8      reserved[6];
      } qExetargNext;
#if SCSI_TARGET_OPERATION
      struct
      {
         SCSI_UEXACT16     q_tag_next;       /* 3D EXE queue init/target pair */
         SCSI_UEXACT8      reserved[6];
      } qTagNext;
      struct
      {
         SCSI_UEXACT16     q_targ_next;      /* 3D EXE queue for specific
                                              * initiator.
                                              */
         SCSI_UEXACT8      reserved[6];
      } qTargNext;
#endif /* SCSI_TARGET_OPERATION */ 
   } scb0to7;
   union
   {
      SCSI_UEXACT8      scdb[12];            /* SCSI CDB                      */
      struct 
      {
         SCSI_UEXACT8      scdb0;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb1;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb2;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb3;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb4;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb5;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb6;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb7;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb8;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb9;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb10;           /* SCSI CDB                      */
         SCSI_UEXACT8      scdb11;           /* SCSI CDB                      */
      } scbCDB;
      SCSI_UEXACT8      scdb_pointer[8];     /* cdb pointer                   */
      struct 
      {
         SCSI_UEXACT8      scdb_pointer0;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer1;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer2;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer3;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer4;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer5;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer6;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer7;    /* cdb pointer                   */   
      } scbCDBPTR;
      struct 
      {
         SCSI_UEXACT8      sdata_residue0;   /* total # of bytes not xfer     */
         SCSI_UEXACT8      sdata_residue1;   /* total # of bytes not xfer     */
         SCSI_UEXACT8      sdata_residue2;   /* total # of bytes not xfer     */
         SCSI_UEXACT8      reserved[4];      /* working flags                 */
         SCSI_UEXACT8      sg_cache_work;    /* working cache                 */
         SCSI_UEXACT8      sg_pointer_work0; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work1; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work2; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work3; /* working s/g list pointer      */
      } scbResidualAndWork;
#if SCSI_TARGET_OPERATION
      struct
      {
         SCSI_UEXACT8      sinitiator;       /* initiator ID - target mode    */
         SCSI_UEXACT8      starg_tagno0;     /* SCSI queue tag - target mode  */
         SCSI_UEXACT8      starg_tagno1;     /* SCSI queue tag - target mode  */  
         SCSI_UEXACT8      starg_status;     /* target status for target mode */
         SCSI_UEXACT8      reserved[3];      /* reserved for future allocation*/
         SCSI_UEXACT8      sg_cache_work;    /* working cache                 */
         SCSI_UEXACT8      sg_pointer_work0; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work1; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work2; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work3; /* working s/g list pointer      */
      } scbTargetMode;
#endif /* SCSI_TARGET_OPERATION */
   } scb8to19;
   union
   {
      SCSI_UEXACT16     q_next;              /* point to next for execution   */
      SCSI_UEXACT16     array_site;          /* SCB array # (SCB destination) */
#if SCSI_TARGET_OPERATION
      SCSI_UEXACT16     q_init_next;         /* links SCBs in the 3D execution
                                              * Q for different initiators
                                              */
#endif /* SCSI_TARGET_OPERATION */ 
   } scb5;
   SCSI_UEXACT8      atn_length;             /* attention management          */
   SCSI_UEXACT8      scdb_length;            /* cdb length                    */
#if (OSD_BUS_ADDRESS_SIZE == 64)
   SCSI_UEXACT8      saddress0;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress1;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress2;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress3;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress4;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress5;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress6;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress7;              /* 1st s/g segment               */
   SCSI_UEXACT8      slength0;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength1;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength2;               /* 1st s/g segment               */
#else
   SCSI_UEXACT8      saddress0;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress1;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress2;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress3;              /* 1st s/g segment               */
   SCSI_UEXACT8      slength0;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength1;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength2;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding0;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding1;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding2;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding3;               /* 1st s/g segment               */
#endif /* OSD_BUS_ADDRESS_SIZE */
   SCSI_UEXACT8      task_attribute;         /* Cmd IU Task Attribute or      */
                                             /* stypecode for reestablish     */
                                             /* SCBs.                         */
   SCSI_UEXACT8      task_management;        /* Cmd IU Task Management Flags  */
   SCSI_UEXACT8      SCB_flags;              /* Flags                         */
   SCSI_UEXACT8      scontrol;               /* scb control bits              */
   SCSI_UEXACT8      sg_cache_SCB;           /* # of SCSI_UEXACT8s for each element   */
   union 
   {
      struct 
      {
         SCSI_UEXACT8      sg_pointer_SCB0;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB1;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB2;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB3;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB4;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB5;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB6;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB7;  /* pointer to s/g list           */
      } scbSG;
      struct 
      {
         SCSI_UEXACT8      special_opcode;   /* opcode for special scb func   */
         SCSI_UEXACT8      special_info;     /* special info                  */
      } scbSpecial;
   } scb40to47;
   SCSI_UEXACT8      slun0;                  /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun1;                  /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun2;                  /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun3;                  /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun4;                  /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun5;                  /* reserved must be 0 for IUs    */
   SCSI_UEXACT8      slun;                   /* lun                           */
   SCSI_UEXACT8      starget;                /* target or our ID in target mode */
   SCSI_UEXACT16     mirror_SCB;             /* SCB for mirrored operation    */
   SCSI_UEXACT8      mirror_slun;            /* Mirrored LUN                  */
   SCSI_UEXACT8      mirror_starget;         /* Mirrored target               */
   SCSI_UEXACT8      scontrol1;              /* scb control1 bits             */
   SCSI_UEXACT8      reserved[2];            /* reserved                      */
   SCSI_UEXACT8      busy_target;            /* busy target                   */
} SCSI_SCB_STANDARD_U320;


#define  SCSI_SU320_sg_pointer_SCB0       scb40to47.scbSG.sg_pointer_SCB0
#define  SCSI_SU320_sg_pointer_SCB1       scb40to47.scbSG.sg_pointer_SCB1
#define  SCSI_SU320_sg_pointer_SCB2       scb40to47.scbSG.sg_pointer_SCB2
#define  SCSI_SU320_sg_pointer_SCB3       scb40to47.scbSG.sg_pointer_SCB3
#define  SCSI_SU320_sg_pointer_SCB4       scb40to47.scbSG.sg_pointer_SCB4
#define  SCSI_SU320_sg_pointer_SCB5       scb40to47.scbSG.sg_pointer_SCB5
#define  SCSI_SU320_sg_pointer_SCB6       scb40to47.scbSG.sg_pointer_SCB6
#define  SCSI_SU320_sg_pointer_SCB7       scb40to47.scbSG.sg_pointer_SCB7

#define  SCSI_SU320_special_opcode        scb40to47.scbSpecial.special_opcode
#define  SCSI_SU320_special_info          scb40to47.scbSpecial.special_info

#define  SCSI_SU320_scdb                  scb8to19.scdb
#define  SCSI_SU320_scdb0                 scb8to19.scbCDB.scdb0 
#define  SCSI_SU320_scdb1                 scb8to19.scbCDB.scdb1
#define  SCSI_SU320_scdb2                 scb8to19.scbCDB.scdb2
#define  SCSI_SU320_scdb3                 scb8to19.scbCDB.scdb3
#define  SCSI_SU320_scdb4                 scb8to19.scbCDB.scdb4
#define  SCSI_SU320_scdb5                 scb8to19.scbCDB.scdb5
#define  SCSI_SU320_scdb6                 scb8to19.scbCDB.scdb6
#define  SCSI_SU320_scdb7                 scb8to19.scbCDB.scdb7
#define  SCSI_SU320_scdb8                 scb8to19.scbCDB.scdb8
#define  SCSI_SU320_scdb9                 scb8to19.scbCDB.scdb9
#define  SCSI_SU320_scdb10                scb8to19.scbCDB.scdb10
#define  SCSI_SU320_scdb11                scb8to19.scbCDB.scdb11

#define  SCSI_SU320_scdb_pointer          scb8to19.scdb_pointer

#define  SCSI_SU320_scdb_pointer0         scb8to19.scbCDBPTR.scdb_pointer0
#define  SCSI_SU320_scdb_pointer1         scb8to19.scbCDBPTR.scdb_pointer1
#define  SCSI_SU320_scdb_pointer2         scb8to19.scbCDBPTR.scdb_pointer2
#define  SCSI_SU320_scdb_pointer3         scb8to19.scbCDBPTR.scdb_pointer3
#define  SCSI_SU320_scdb_pointer4         scb8to19.scbCDBPTR.scdb_pointer4
#define  SCSI_SU320_scdb_pointer5         scb8to19.scbCDBPTR.scdb_pointer5
#define  SCSI_SU320_scdb_pointer6         scb8to19.scbCDBPTR.scdb_pointer6
#define  SCSI_SU320_scdb_pointer7         scb8to19.scbCDBPTR.scdb_pointer7

#define  SCSI_SU320_sdata_residue0        scb8to19.scbResidualAndWork.sdata_residue0
#define  SCSI_SU320_sdata_residue1        scb8to19.scbResidualAndWork.sdata_residue1
#define  SCSI_SU320_sdata_residue2        scb8to19.scbResidualAndWork.sdata_residue2
#define  SCSI_SU320_sg_cache_work         scb8to19.scbResidualAndWork.sg_cache_work  
#define  SCSI_SU320_sg_pointer_work0      scb8to19.scbResidualAndWork.sg_pointer_work0
#define  SCSI_SU320_sg_pointer_work1      scb8to19.scbResidualAndWork.sg_pointer_work1
#define  SCSI_SU320_sg_pointer_work2      scb8to19.scbResidualAndWork.sg_pointer_work2
#define  SCSI_SU320_sg_pointer_work3      scb8to19.scbResidualAndWork.sg_pointer_work3

#if SCSI_TARGET_OPERATION
#define  SCSI_SU320_sinitiator            scb8to19.scbTargetMode.sinitiator
#define  SCSI_SU320_starg_tagno           scb8to19.scbTargetMode.starg_tagno0
#define  SCSI_SU320_starg_status          scb8to19.scbTargetMode.starg_status
#define  SCSI_SU320_stypecode             task_attribute
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_SU320_q_next                scb5.q_next
#define  SCSI_SU320_array_site            scb5.array_site

#define  SCSI_SU320_q_exetarg_next        scb0to7.qExetargNext.q_exetarg_next
#define  SCSI_SU320_next_SCB_addr0        scb0to7.nextScbAddr.next_SCB_addr0
#define  SCSI_SU320_next_SCB_addr1        scb0to7.nextScbAddr.next_SCB_addr1
#define  SCSI_SU320_next_SCB_addr2        scb0to7.nextScbAddr.next_SCB_addr2
#define  SCSI_SU320_next_SCB_addr3        scb0to7.nextScbAddr.next_SCB_addr3
#define  SCSI_SU320_next_SCB_addr4        scb0to7.nextScbAddr.next_SCB_addr4
#define  SCSI_SU320_next_SCB_addr5        scb0to7.nextScbAddr.next_SCB_addr5
#define  SCSI_SU320_next_SCB_addr6        scb0to7.nextScbAddr.next_SCB_addr6
#define  SCSI_SU320_next_SCB_addr7        scb0to7.nextScbAddr.next_SCB_addr7

#endif /* !defined(SCSI_INTERNAL) */

/* definitions for scontrol   */
#define  SCSI_SU320_SIMPLETAG    0x00     /* simple tag                 */
#define  SCSI_SU320_HEADTAG      0x01     /* head of queue tag          */
#define  SCSI_SU320_ORDERTAG     0x02     /* ordered tag                */
#define  SCSI_SU320_SPECFUN      0x08     /* special function           */
#define  SCSI_SU320_ABORTED      0x10     /* scb aborted flag           */
#define  SCSI_SU320_TAGENB       0x20     /* tag enable                 */
#define  SCSI_SU320_DISCENB      0x40     /* disconnect enable          */
#define  SCSI_SU320_TAGMASK      0x03     /* mask for tags              */
#if SCSI_PARITY_PER_IOB
#define  SCSI_SU320_ENPAR        0x80     /* disable parity checking    */
#endif /* SCSI_PARITY_PER_IOB */
#if SCSI_TARGET_OPERATION
#define  SCSI_SU320_TARGETENB    0x04     /* target mode enable         */
#define  SCSI_SU320_HOLDONBUS    0x02     /* hold-on to SCSI BUS        */
#endif /* SCSI_TARGET_OPERATION */

/* definitions for starg_lun */
#define  SCSI_SU320_TARGETID     0xF0     /* target id                  */
#define  SCSI_SU320_TARGETLUN    0x07     /* target lun                 */

#if SCSI_TARGET_OPERATION
/* definitions for stypecode bits 0 - 2 */
#define  SCSI_SU320_DATAOUT              0x00
#define  SCSI_SU320_DATAIN               0x01
#define  SCSI_SU320_GOODSTATUS           0x02
#define  SCSI_SU320_BADSTATUS            0x03
#define  SCSI_SU320_DATAOUT_AND_STATUS   0x04
#define  SCSI_SU320_DATAIN_AND_STATUS    0x05
#define  SCSI_SU320_EMPTYSCB             0x07
#endif /* SCSI_TARGET_OPERATION */

#if (SCSI_DOMAIN_VALIDATION_BASIC+SCSI_DOMAIN_VALIDATION_ENHANCED)
/* definitions for dv_control */
#define  SCSI_SU320_DV_ENABLE  0x01        /* Identify domain validation SCB */
#endif /* (SCSI_DOMAIN_VALIDATION_BASIC+SCSI_DOMAIN_VALIDATION_ENHANCED) */

/* Definition for size of SCB area allocated for a CDB. */
/* When the CDB cannot be embedded in the SCB (i.e. greater 
 * than this size) the CDB is copied to the CDB field in the 
 * IOB reserve area and the physical memory address of this 
 * area is embedded in the SCB. This allows the sequencer
 * to DMA the CDB during Command phase.
 */
#define  SCSI_SU320_SCDB_SIZE    0x0C
     
#if SCSI_TARGET_OPERATION
/* definitions for starget */
#define  SCSI_SU320_OWNID_MASK   0x0F     /* Mask of starget field      */
#endif /* SCSI_TARGET_OPERATION */

/* definitions for scdb_length */
#define  SCSI_SU320_CDBLENMSK    0x1F     /* CDB length                 */
#define  SCSI_SU320_CACHE_ADDR_MASK 0x7C  /* Mask out rollover & lo 2 bits */

/* definitions for sg_cache_SCB */
#define  SCSI_SU320_NODATA       0x01     /* no data transfer involoved */
#define  SCSI_SU320_ONESGSEG     0x02     /* only one sg segment        */

/* definitions for special function opcode */
#define  SCSI_SU320_MSGTOTARG    0x00     /* special opcode, msg_to_targ */

/***************************************************************************
*  Standard Ultra 320 Establish Connection SCB format layout
***************************************************************************/
#if SCSI_TARGET_OPERATION

/* All elements are single byte to avoid realignment by compilers. */ 
/* structure definition */
typedef struct SCSI_EST_SCB_STANDARD_U320_
{
   SCSI_UEXACT8      scbByte0;           /* next SCB address for 8 bytes  */
   SCSI_UEXACT8      scbByte1;
   SCSI_UEXACT8      scbByte2;
   SCSI_UEXACT8      scbByte3;
   SCSI_UEXACT8      scbByte4;
   SCSI_UEXACT8      scbByte5;
   SCSI_UEXACT8      scbByte6;
   SCSI_UEXACT8      scbByte7;
   SCSI_UEXACT8      scbByte8;
   SCSI_UEXACT8      scbByte9;
   SCSI_UEXACT8      scbByte10;
   SCSI_UEXACT8      scbByte11;
   SCSI_UEXACT8      scbByte12;
   SCSI_UEXACT8      scbByte13;
   SCSI_UEXACT8      scbByte14;
   SCSI_UEXACT8      scbByte15;
   SCSI_UEXACT8      est_iid;            /* selecting initiator SCSI ID   */
   SCSI_UEXACT8      tag_type;           /* queue tag type received       */
   SCSI_UEXACT8      tag_num;            /* tag number received           */
   SCSI_UEXACT8      tag_num1;           /* not required for legacy       */
   SCSI_UEXACT8      scbByte20;          /* array_site OR q_next          */
   SCSI_UEXACT8      scbByte21;          /* array_site OR q_next          */
   SCSI_UEXACT8      scdb_len;           /* cbd length                    */
   SCSI_UEXACT8      id_msg;             /* identify msg byte received    */
   SCSI_UEXACT8      scbaddress0;        /* PCI address of area to receive request */
   SCSI_UEXACT8      scbaddress1;        /* PCI address of area to receive request */
   SCSI_UEXACT8      scbaddress2;        /* PCI address of area to receive request */
   SCSI_UEXACT8      scbaddress3;        /* PCI address of area to receive request */
#if (OSD_BUS_ADDRESS_SIZE == 64)
   SCSI_UEXACT8      scbaddress4;        /* PCI address of area to receive request */
   SCSI_UEXACT8      scbaddress5;        /* PCI address of area to receive request */
   SCSI_UEXACT8      scbaddress6;        /* PCI address of area to receive request */
   SCSI_UEXACT8      scbaddress7;        /* PCI address of area to receive request */
#else
   SCSI_UEXACT8      padding0;
   SCSI_UEXACT8      padding1;
   SCSI_UEXACT8      padding2;
   SCSI_UEXACT8      padding3;
#endif /* OSD_BUS_ADDRESS_SIZE */
   SCSI_UEXACT8      last_byte;          /* last byte received             */
   SCSI_UEXACT8      flags;              /* misc flags related to          */
                                         /* connection                     */
   SCSI_UEXACT8      eststatus;          /* status returned from sequencer */
   SCSI_UEXACT8      stypecode;          /* SCB type - empty SCB           */
   SCSI_UEXACT8      reserved1;
   SCSI_UEXACT8      SCB_flags;          /* Flags - packetized only        */
   SCSI_UEXACT8      scontrol;           /* scb control bits - only        */
                                         /* target_mode bit used.          */
   SCSI_UEXACT8      reserved[24];
   SCSI_UEXACT8      busy_target;        /* busy target                   */
} SCSI_EST_SCB_STANDARD_U320;

/* defines for cdb fields examined. */
#define  SCSI_EST_SU320_scdb0               scbByte0 
#define  SCSI_EST_SU320_scdb1               scbByte1

/* defines for flags */
#define  SCSI_EST_SU320_TAG_RCVD       0x80  /* queue tag message received */
#define  SCSI_EST_SU320_SCSI1_SEL      0x40  /* scsi1 selection received */
#define  SCSI_EST_SU320_BUS_HELD       0x20  /* scsi bus held */
#define  SCSI_EST_SU320_RSVD_BITS      0x10  /* reserved bit set in SCSI-2
                                              * identify message and
                                              * CHK_RSVD_BITS set
                                              */
#define  SCSI_EST_SU320_LUNTAR         0x08  /* luntar bit set in SCSI-2
                                              * identify message and
                                              * LUNTAR_EN = 0
                                              */
/* defines for last state */
/* are these still relevant ???? */
#define  SCSI_EST_SU320_CMD_PHASE      0x02
#define  SCSI_EST_SU320_MSG_OUT_PHASE  0x06
#define  SCSI_EST_SU320_MSG_IN_PHASE   0x07

/* definitions for eststatus */
#define  SCSI_EST_SU320_GOOD_STATE     0x00  /* command received without exception */
#define  SCSI_EST_SU320_SEL_STATE      0x01  /* exception during selection phase   */
#define  SCSI_EST_SU320_MSGID_STATE    0x02  /*     "        "   identifying phase */
#define  SCSI_EST_SU320_MSGOUT_STATE   0x03  /*     "        "   message out phase */
#define  SCSI_EST_SU320_CMD_STATE      0x04  /*     "        "   command phase     */
#define  SCSI_EST_SU320_DISC_STATE     0x05  /*     "        "   disconnect phase  */
#define  SCSI_EST_SU320_VENDOR_CMD     0x08  /* Vendor unique command */
/* These statuses are stored in the upper nibble of the returned status 
 * so we can handle multiple conditions at once.
 */
#define  SCSI_EST_SU320_LAST_RESOURCE  0x40  /* last resource used */
#define  SCSI_EST_SU320_PARITY_ERR     0x80  /* parity error       */
/* defines for estiid */
#define  SCSI_EST_SU320_SELID_MASK     0x0F   /* our selected ID */
/* misc defines */
#define  SCSI_EST_SU320_MAX_CDB_SIZE    16   /* maximum number of CDB
                                              * bytes that can be 
                                              * stored in the SCB.
                                              * Support of larger CDBs
                                              * requires HIM processing
                                              * (rather than command
                                              * complete handling).
                                              */
#endif /* SCSI_TARGET_OPERATION */

#endif /* SCSI_STANDARD_U320_MODE */

/***************************************************************************
*  Standard Enhanced Ultra 320 SCB format layout
***************************************************************************/
#if SCSI_STANDARD_ENHANCED_U320_MODE
/* structure definition */
typedef struct SCSI_SCB_STANDARD_ENH_U320_
{
   SCSI_UEXACT8      scbByte0;            /* next SCB address for 8 bytes  */
                                          /* OR q_exetarg_next for 2 bytes */
   SCSI_UEXACT8      scbByte1;
   SCSI_UEXACT8      scbByte2;
   SCSI_UEXACT8      scbByte3;
   SCSI_UEXACT8      scbByte4;
   SCSI_UEXACT8      scbByte5;
   SCSI_UEXACT8      scbByte6;
   SCSI_UEXACT8      scbByte7;
   SCSI_UEXACT8      scbByte8;            /* SCSI CDB for 12 bytes OR      */
                                          /* cdb pointer for 8 bytes OR    */
                                          /* residue for 3 bytes.          */
   SCSI_UEXACT8      scbByte9;
   SCSI_UEXACT8      scbByte10;           /* residue for 3 bytes.          */
   SCSI_UEXACT8      scbByte11;           /* working flags                 */
   SCSI_UEXACT8      scbByte12;
   SCSI_UEXACT8      scbByte13;
   SCSI_UEXACT8      scbByte14;
   SCSI_UEXACT8      scbByte15;           /* working cache                 */
   SCSI_UEXACT8      scbByte16;           /* working s/g list pointer for  */
   SCSI_UEXACT8      scbByte17;           /* 4 bytes                       */
   SCSI_UEXACT8      scbByte18;
   SCSI_UEXACT8      scbByte19;
   SCSI_UEXACT8      scbByte20;           /* array_site OR q_next          */
   SCSI_UEXACT8      scbByte21;           /* array_site OR q_next          */
   SCSI_UEXACT8      atn_length;          /* attention management          */
   SCSI_UEXACT8      scdb_length;         /* cdb length                    */
#if (OSD_BUS_ADDRESS_SIZE == 64)
   SCSI_UEXACT8      saddress0;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress1;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress2;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress3;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress4;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress5;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress6;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress7;           /* 1st s/g segment               */
   SCSI_UEXACT8      slength0;            /* 1st s/g segment               */
   SCSI_UEXACT8      slength1;            /* 1st s/g segment               */
   SCSI_UEXACT8      slength2;            /* 1st s/g segment               */
#else
   SCSI_UEXACT8      saddress0;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress1;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress2;           /* 1st s/g segment               */
   SCSI_UEXACT8      saddress3;           /* 1st s/g segment               */
   SCSI_UEXACT8      slength0;            /* 1st s/g segment               */
   SCSI_UEXACT8      slength1;            /* 1st s/g segment               */
   SCSI_UEXACT8      slength2;            /* 1st s/g segment               */
   SCSI_UEXACT8      padding0;            /* 1st s/g segment               */
   SCSI_UEXACT8      padding1;            /* 1st s/g segment               */
   SCSI_UEXACT8      padding2;            /* 1st s/g segment               */
   SCSI_UEXACT8      padding3;            /* 1st s/g segment               */
#endif /* OSD_BUS_ADDRESS_SIZE */
   SCSI_UEXACT8      task_attribute;      /* Cmd IU Task Attribute OR      */
                                          /* stypecode for reestablish_SCB */
   SCSI_UEXACT8      task_management;     /* Cmd IU Task Management Flags  */
   SCSI_UEXACT8      SCB_flags;           /* Flags                         */
   SCSI_UEXACT8      scontrol;            /* scb control bits              */
   SCSI_UEXACT8      sg_cache_SCB;        /* # of SCSI_UEXACT8s for each element */
   SCSI_UEXACT8      scbByte40;           /* pointer to s/g list for 8     */
                                          /* OR special_opcode for 1 byte  */
   SCSI_UEXACT8      scbByte41;           /* special_info                  */
   SCSI_UEXACT8      scbByte42;
   SCSI_UEXACT8      scbByte43;
   SCSI_UEXACT8      scbByte44;
   SCSI_UEXACT8      scbByte45;
   SCSI_UEXACT8      scbByte46;
   SCSI_UEXACT8      scbByte47;
   SCSI_UEXACT8      slun;                /* lun                           */
   SCSI_UEXACT8      starget;             /* target ID OR our ID for       */
                                          /* reestablish_SCB               */
   SCSI_UEXACT8      mirror_SCB;          /* SCB for mirrored operation    */
   SCSI_UEXACT8      mirror_SCB1;         /* SCB for mirrored operation    */
   SCSI_UEXACT8      mirror_slun;         /* Mirrored LUN                  */
   SCSI_UEXACT8      mirror_starget;      /* Mirrored target               */
   SCSI_UEXACT8      scontrol1;           /* scb control1 bits             */
   SCSI_UEXACT8      reserved[8];         /* reserved                      */
   SCSI_UEXACT8      busy_target;         /* busy target                   */
} SCSI_SCB_STANDARD_ENH_U320;

/* These scb field defines must only be used as offsets within the
 * scb. There is no field size associated with the define.
 */
#define  SCSI_SEU320_sg_pointer_SCB0      scbByte40
#define  SCSI_SEU320_sg_pointer_SCB1      scbByte41
#define  SCSI_SEU320_sg_pointer_SCB2      scbByte42
#define  SCSI_SEU320_sg_pointer_SCB3      scbByte43
#define  SCSI_SEU320_sg_pointer_SCB4      scbByte44
#define  SCSI_SEU320_sg_pointer_SCB5      scbByte45
#define  SCSI_SEU320_sg_pointer_SCB6      scbByte46
#define  SCSI_SEU320_sg_pointer_SCB7      scbByte47

#define  SCSI_SEU320_special_opcode       scbByte40
#define  SCSI_SEU320_special_info         scbByte41

#define  SCSI_SEU320_scdb                 scbByte8
#define  SCSI_SEU320_scdb0                scbByte8 
#define  SCSI_SEU320_scdb1                scbByte9
#define  SCSI_SEU320_scdb2                scbByte10
#define  SCSI_SEU320_scdb3                scbByte11
#define  SCSI_SEU320_scdb4                scbByte12
#define  SCSI_SEU320_scdb5                scbByte13
#define  SCSI_SEU320_scdb6                scbByte14
#define  SCSI_SEU320_scdb7                scbByte15
#define  SCSI_SEU320_scdb8                scbByte16
#define  SCSI_SEU320_scdb9                scbByte17
#define  SCSI_SEU320_scdb10               scbByte18
#define  SCSI_SEU320_scdb11               scbByte19

#define  SCSI_SEU320_scdb_pointer         scbByte8

#define  SCSI_SEU320_scdb_pointer0        scbByte8
#define  SCSI_SEU320_scdb_pointer1        scbByte9
#define  SCSI_SEU320_scdb_pointer2        scbByte10
#define  SCSI_SEU320_scdb_pointer3        scbByte11
#define  SCSI_SEU320_scdb_pointer4        scbByte12
#define  SCSI_SEU320_scdb_pointer5        scbByte13
#define  SCSI_SEU320_scdb_pointer6        scbByte14
#define  SCSI_SEU320_scdb_pointer7        scbByte15

#define  SCSI_SEU320_sdata_residue0       scbByte8
#define  SCSI_SEU320_sdata_residue1       scbByte9
#define  SCSI_SEU320_sdata_residue2       scbByte10

#define  SCSI_SEU320_sg_cache_work        scbByte15
#define  SCSI_SEU320_sg_pointer_work0     scbByte16
#define  SCSI_SEU320_sg_pointer_work1     scbByte17
#define  SCSI_SEU320_sg_pointer_work2     scbByte18
#define  SCSI_SEU320_sg_pointer_work3     scbByte19

#if SCSI_TARGET_OPERATION
#define  SCSI_SEU320_sinitiator           scbByte14           
#define  SCSI_SEU320_starg_tagno          scbByte8
#define  SCSI_SEU320_starg_status         scbByte11
#define  SCSI_SEU320_stypecode            scbByte10
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_SEU320_dlcount0             scbByte11 
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_SEU320_q_next               scbByte20
#define  SCSI_SEU320_array_site           scbByte20

#define  SCSI_SEU320_q_exetarg_next       scbByte0
#define  SCSI_SEU320_next_SCB_addr0       scbByte0
#define  SCSI_SEU320_next_SCB_addr1       scbByte1
#define  SCSI_SEU320_next_SCB_addr2       scbByte2
#define  SCSI_SEU320_next_SCB_addr3       scbByte3
#define  SCSI_SEU320_next_SCB_addr4       scbByte4
#define  SCSI_SEU320_next_SCB_addr5       scbByte5
#define  SCSI_SEU320_next_SCB_addr6       scbByte6
#define  SCSI_SEU320_next_SCB_addr7       scbByte7

/* definitions for scontrol   */
#define  SCSI_SEU320_SIMPLETAG    0x00    /* simple tag                 */
#define  SCSI_SEU320_HEADTAG      0x01    /* head of queue tag          */
#define  SCSI_SEU320_ORDERTAG     0x02    /* ordered tag                */
#define  SCSI_SEU320_SPECFUN      0x08    /* special function           */
#define  SCSI_SEU320_ABORTED      0x10    /* scb aborted flag           */
#define  SCSI_SEU320_TAGENB       0x20    /* tag enable                 */
#define  SCSI_SEU320_DISCENB      0x40    /* disconnect enable          */
#define  SCSI_SEU320_TAGMASK      0x03    /* mask for tags              */
#if SCSI_PARITY_PER_IOB
#define  SCSI_SEU320_ENPAR        0x80    /* disable parity checking    */
#endif /* SCSI_PARITY_PER_IOB */
#if SCSI_TARGET_OPERATION
#define  SCSI_SEU320_TARGETENB    0x80    /* target mode enable         */
#define  SCSI_SEU320_HOLDONBUS    0x02    /* hold-on to SCSI BUS        */
#endif /* SCSI_TARGET_OPERATION */

/* definitions for starg_lun */
#define  SCSI_SEU320_TARGETID     0xF0    /* target id                  */
#define  SCSI_SEU320_TARGETLUN    0x07    /* target lun                 */

#if SCSI_TARGET_OPERATION
/* definitions for stypecode bits 0 - 2 */
#define  SCSI_SEU320_DATAIN              0x00
#define  SCSI_SEU320_DATAOUT             0x01
#define  SCSI_SEU320_GOODSTATUS          0x02
#define  SCSI_SEU320_BADSTATUS           0x03
#define  SCSI_SEU320_DATAIN_AND_STATUS   0x04
#define  SCSI_SEU320_DATAOUT_AND_STATUS  0x05
#define  SCSI_SEU320_EMPTYSCB            0x07
#endif /* SCSI_TARGET_OPERATION */

#if (SCSI_DOMAIN_VALIDATION_BASIC+SCSI_DOMAIN_VALIDATION_ENHANCED)
/* definitions for dv_control */
#define  SCSI_SEU320_DV_ENABLE  0x01       /* Identify domain validation SCB */
#endif /* (SCSI_DOMAIN_VALIDATION_BASIC+SCSI_DOMAIN_VALIDATION_ENHANCED) */

/* Definition for size of SCB area allocated for a CDB. */
/* When the CDB cannot be embedded in the SCB (i.e. greater 
 * than this size) the CDB is copied to the CDB field in the 
 * IOB reserve area and the physical memory address of this 
 * area is embedded in the SCB. This allows the sequencer
 * to DMA the CDB during Command phase.
 */
#define  SCSI_SEU320_SCDB_SIZE   0x0C
     
#if SCSI_TARGET_OPERATION
/* definitions for starget */
#define  SCSI_SEU320_OWNID_MASK  0x0F     /* Mask of starget field      */
#endif /* SCSI_TARGET_OPERATION */

/* definitions for scdb_length */
#define  SCSI_SEU320_CDBLENMSK   0x1F     /* CDB length                 */
#define  SCSI_SEU320_CACHE_ADDR_MASK 0x7C /* Mask out rollover & lo 2 bits */

/* definitions for sg_cache_SCB */
#define  SCSI_SEU320_NODATA      0x01     /* no data transfer involoved */
#define  SCSI_SEU320_ONESGSEG    0x02     /* only one sg segment        */

/* definitions for special function opcode */
#define  SCSI_SEU320_MSGTOTARG   0x00     /* special opcode, msg_to_targ */

/***************************************************************************
*  Standard Enhanced Ultra 320 Establish Connection SCB format layout
***************************************************************************/
#if SCSI_TARGET_OPERATION

/* All elements are single byte to avoid realignment by compilers. */ 
/* structure definition */
typedef struct SCSI_EST_SCB_STANDARD_ENH_U320_
{
   SCSI_UEXACT8      scbByte0;           /* next SCB address for 8 bytes  */
   SCSI_UEXACT8      scbByte1;
   SCSI_UEXACT8      scbByte2;
   SCSI_UEXACT8      scbByte3;
   SCSI_UEXACT8      scbByte4;
   SCSI_UEXACT8      scbByte5;
   SCSI_UEXACT8      scbByte6;
   SCSI_UEXACT8      scbByte7;
   SCSI_UEXACT8      scbByte8;
   SCSI_UEXACT8      scbByte9;
   SCSI_UEXACT8      stypecode;          /* SCB type - empty SCB          */
   SCSI_UEXACT8      scbByte11;
   SCSI_UEXACT8      scbByte12;
   SCSI_UEXACT8      scbByte13;
   SCSI_UEXACT8      scbByte14;
   SCSI_UEXACT8      scbByte15;
   SCSI_UEXACT8      scbByte16;
   SCSI_UEXACT8      scbByte17;
   SCSI_UEXACT8      scbByte18;
   SCSI_UEXACT8      scbByte19;
   SCSI_UEXACT8      scbByte20;          /* array_site OR q_next          */
   SCSI_UEXACT8      scbByte21;          /* array_site OR q_next          */
   SCSI_UEXACT8      scbByte22;
   SCSI_UEXACT8      scbByte23;
   SCSI_UEXACT8      scbaddress0;        /* PCI address of area to receive request(s) */
   SCSI_UEXACT8      scbaddress1;        /* PCI address of area to receive request(s) */
   SCSI_UEXACT8      scbaddress2;        /* PCI address of area to receive request(s) */
   SCSI_UEXACT8      scbaddress3;        /* PCI address of area to receive request(s) */
#if (OSD_BUS_ADDRESS_SIZE == 64)
   SCSI_UEXACT8      scbaddress4;        /* PCI address of area to receive request(s) */
   SCSI_UEXACT8      scbaddress5;        /* PCI address of area to receive request(s) */
   SCSI_UEXACT8      scbaddress6;        /* PCI address of area to receive request(s) */
   SCSI_UEXACT8      scbaddress7;        /* PCI address of area to receive request(s) */
#else
   SCSI_UEXACT8      padding0;
   SCSI_UEXACT8      padding1;
   SCSI_UEXACT8      padding2;
   SCSI_UEXACT8      padding3;
#endif /* OSD_BUS_ADDRESS_SIZE */
   SCSI_UEXACT8      scbByte32;
   SCSI_UEXACT8      scbByte33;
   SCSI_UEXACT8      scbByte34;
   SCSI_UEXACT8      scbByte35;
   SCSI_UEXACT8      scbByte36;
   SCSI_UEXACT8      SCB_flags;          /* For packetized only           */
   SCSI_UEXACT8      scontrol;           /* scb control bits - only       */
                                         /* target_mode bit used.         */
   SCSI_UEXACT8      sg_cache_SCB;       /* Set to 2 indicating single area */
   SCSI_UEXACT8      reserved[23];
   SCSI_UEXACT8      busy_target;        /* busy target                   */
} SCSI_EST_SCB_STANDARD_ENH_U320;

/* Format of Selection-in data returned when an Establish Connection SCB completes.
 * Legacy (non-packetized) format.
 * Note that; this format allows the Establish Connection SCB to be used
 * to return the data when SCSI_PACKETIZED_TARGET_SUPPORT is 0.
 */
typedef struct SCSI_EST_DATA_STANDARD_ENH_U320_
{
   SCSI_UEXACT8      est_legacy;
   SCSI_UEXACT8      dataByte1;
   SCSI_UEXACT8      scdb_len;           /* cbd length                    */
   SCSI_UEXACT8      cdbByte0;
   SCSI_UEXACT8      cdbByte1;
   SCSI_UEXACT8      cdbByte2;
   SCSI_UEXACT8      cdbByte3;
   SCSI_UEXACT8      cdbByte4;
   SCSI_UEXACT8      cdbByte5;
   SCSI_UEXACT8      cdbByte6;
   SCSI_UEXACT8      cdbByte7;
   SCSI_UEXACT8      cdbByte8;
   SCSI_UEXACT8      cdbByte9;
   SCSI_UEXACT8      cdbByte10;
   SCSI_UEXACT8      cdbByte11;
   SCSI_UEXACT8      cdbByte12;
   SCSI_UEXACT8      cdbByte13;
   SCSI_UEXACT8      cdbByte14;
   SCSI_UEXACT8      cdbByte15;
   SCSI_UEXACT8      id_msg;            /* identify msg byte received    */
   SCSI_UEXACT8      dataByte20;
   SCSI_UEXACT8      dataByte21;
   SCSI_UEXACT8      est_iid;           /* selecting initiator SCSI ID   */
   SCSI_UEXACT8      targidin;          /* selected target SCSI ID       */     
   SCSI_UEXACT8      dataByte24;
   SCSI_UEXACT8      dataByte25;
   SCSI_UEXACT8      dataByte26;
   SCSI_UEXACT8      dataByte27;
   SCSI_UEXACT8      dataByte28;
   SCSI_UEXACT8      dataByte29;
   SCSI_UEXACT8      dataByte30;
   SCSI_UEXACT8      dataByte31;
   SCSI_UEXACT8      tag_type;          /* queue tag type received        */
   SCSI_UEXACT8      tag_num;           /* tag number received            */
   SCSI_UEXACT8      last_byte;         /* last byte received             */
   SCSI_UEXACT8      flags;             /* misc flags related to          */
                                        /* connection                     */
   SCSI_UEXACT8      eststatus;         /* status returned from sequencer */
/* 37 bytes */
} SCSI_EST_DATA_STANDARD_ENH_U320;

/* Define for est_legacy field of establish data area. */
#define  SCSI_EST_SEU320_LEGACY_SELECTION_IN  0xFF
 
/* defines for cdb fields examined. */
#define  SCSI_EST_SEU320_scdb0              cdbByte0 
#define  SCSI_EST_SEU320_scdb1              cdbByte1

/* defines for flags */
#define  SCSI_EST_SEU320_TAG_RCVD      0x80  /* queue tag message received */
#define  SCSI_EST_SEU320_SCSI1_SEL     0x40  /* scsi1 selection received */
#define  SCSI_EST_SEU320_BUS_HELD      0x20  /* scsi bus held */
#define  SCSI_EST_SEU320_RSVD_BITS     0x10  /* reserved bit set in SCSI-2
                                              * identify message and
                                              * CHK_RSVD_BITS set
                                              */
#define  SCSI_EST_SEU320_LUNTAR        0x08  /* luntar bit set in SCSI-2
                                              * identify message and
                                              * LUNTAR_EN = 0
                                              */
/* defines for last state */
/* are these still relevant ???? */
#define  SCSI_EST_SEU320_CMD_PHASE     0x02
#define  SCSI_EST_SEU320_MSG_OUT_PHASE 0x06
#define  SCSI_EST_SEU320_MSG_IN_PHASE  0x07

/* definitions for eststatus */
#define  SCSI_EST_SEU320_GOOD_STATE    0x00  /* command received without exception */
#define  SCSI_EST_SEU320_SEL_STATE     0x01  /* exception during selection phase   */
#define  SCSI_EST_SEU320_MSGID_STATE   0x02  /*     "        "   identifying phase */
#define  SCSI_EST_SEU320_MSGOUT_STATE  0x03  /*     "        "   message out phase */
#define  SCSI_EST_SEU320_CMD_STATE     0x04  /*     "        "   command phase     */
#define  SCSI_EST_SEU320_DISC_STATE    0x05  /*     "        "   disconnect phase  */
#define  SCSI_EST_SEU320_VENDOR_CMD    0x08  /* Vendor unique command */
/* These statuses are stored in the upper nibble of the returned status 
 * so we can handle multiple conditions at once.
 */
#define  SCSI_EST_SEU320_LAST_RESOURCE 0x40  /* last resource used */
#define  SCSI_EST_SEU320_PARITY_ERR    0x80  /* parity error       */
/* defines for estiid */
#define  SCSI_EST_SEU320_SELID_MASK   0x0F   /* our selected ID */
/* misc defines */
#define  SCSI_EST_SEU320_MAX_CDB_SIZE   16   /* maximum number of CDB
                                              * bytes that can be 
                                              * stored in the SCB.
                                              * Support of larger CDBs
                                              * requires HIM processing
                                              * (rather than command
                                              * complete handling).
                                              */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT 
/* Format of Selection-in data returned when an Establish Connection SCB
 * completes. Packetized format.
 */
typedef struct SCSI_EST_IU_DATA_STANDARD_ENH_U320_
{
   SCSI_UEXACT8      selid;          /* contents of SELID register */
   SCSI_UEXACT8      targidin;       /* contents of TARGIDIN register */
   SCSI_UEXACT8      lqIUByte0;      /* 1st byte of SPI L_Q IU */   
   SCSI_UEXACT8      lqIUByte1;
   SCSI_UEXACT8      lqIUByte2;
   SCSI_UEXACT8      lqIUByte3;
   SCSI_UEXACT8      lqIUByte4;
   SCSI_UEXACT8      lqIUByte5;
   SCSI_UEXACT8      lqIUByte6;
   SCSI_UEXACT8      lqIUByte7;
   SCSI_UEXACT8      lqIUByte8;
   SCSI_UEXACT8      lqIUByte9;
   SCSI_UEXACT8      lqIUByte10;
   SCSI_UEXACT8      lqIUByte11;
   SCSI_UEXACT8      lqIUByte12;
   SCSI_UEXACT8      lqIUByte13;
   SCSI_UEXACT8      lqIUByte14;
   SCSI_UEXACT8      lqIUByte15;
   SCSI_UEXACT8      lqIUByte16;
   SCSI_UEXACT8      lqIUByte17;
   SCSI_UEXACT8      lqIUByte18;
   SCSI_UEXACT8      lqIUByte19;
   SCSI_UEXACT8      lqStatus;       /* one byte code placed after SPI L_Q */
   SCSI_UEXACT8      cmndIUByte0;    /* 1st byte of SPI Command IU */
   SCSI_UEXACT8      cmndIUByte1;
   SCSI_UEXACT8      cmndIUByte2;
   SCSI_UEXACT8      cmndIUByte3;
   SCSI_UEXACT8      cmndIUByte4;
   SCSI_UEXACT8      cmndIUByte5;
   SCSI_UEXACT8      cmndIUByte6;
   SCSI_UEXACT8      cmndIUByte7;
   SCSI_UEXACT8      cmndIUByte8;
   SCSI_UEXACT8      cmndIUByte9;
   SCSI_UEXACT8      cmndIUByte10;
   SCSI_UEXACT8      cmndIUByte11;
   SCSI_UEXACT8      cmndIUByte12;
   SCSI_UEXACT8      cmndIUByte13;
   SCSI_UEXACT8      cmndIUByte14;
   SCSI_UEXACT8      cmndIUByte15;
   SCSI_UEXACT8      cmndIUByte16;
   SCSI_UEXACT8      cmndIUByte17;
   SCSI_UEXACT8      cmndIUByte18;
   SCSI_UEXACT8      cmndIUByte19;
   SCSI_UEXACT8      cmndStatus;     /* one byte code placed after Command IU */
} SCSI_EST_IU_DATA_STANDARD_ENH_U320;

/* Defines for SPI L_Q IU fields. */
#define  SCSI_EST_LQ_tag0                   lqIUByte3
#define  SCSI_EST_LQ_tag1                   lqIUByte2

#define  SCSI_EST_LQ_sl_lun                 lqIUByte5
#define  SCSI_EST_LQ_dataLen                lqIUByte13

/* Defines for SPI CMND IU fields. */
#define  SCSI_EST_CMND_IU_taskAttribute     cmndIUByte1
#define  SCSI_EST_CMND_IU_tmf               cmndIUByte2
#define  SCSI_EST_CMND_IU_rdata_wdata       cmndIUByte3
#define  SCSI_EST_CMND_IU_cdb0              cmndIUByte4 

/* Defines for one byte status codes. */
#define  SCSI_EST_GOOD_CR                   0x00  /* Good, keep parsing           */ 
#define  SCSI_EST_TMF_CR                    0x01  /* Task Management Flag(s) set  */
#define  SCSI_EST_ATN_CR                    0x02  /* ATN set during L_Q or CMND   */
#define  SCSI_EST_BAD_LQ_TYPE_CR            0x04  /* Bad L_Q Type                 */
#define  SCSI_EST_CRC_ERROR_CR              0x08  /* CRC error on CMND IU         */
#define  SCSI_EST_CRC_LQ_ERROR_CR           0x10  /* CRC error on L_Q IU          */ 
#define  SCSI_EST_CMND_TOO_LONG_CR          0x40  /* Command was too long         */
#define  SCSI_EST_RESOURCE_SHORTAGE_CR      0x80  /* Resource shortage            */
#define  SCSI_EST_GOOD_END_CR               0xFF  /* Normal end of queue          */  
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */ 
#endif /* SCSI_TARGET_OPERATION */
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */
                                              
/***************************************************************************
*  Downshift Ultra 320 SCB format layout
***************************************************************************/
#if SCSI_DOWNSHIFT_U320_MODE

/* structure definition */
typedef struct SCSI_SCB_DOWNSHIFT_U320_
{
   union
   {
      struct
      {
         SCSI_UEXACT8      next_SCB_addr0;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr1;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr2;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr3;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr4;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr5;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr6;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr7;   /* next SCB address              */
      } nextScbAddr;
      struct
      {
         SCSI_UEXACT16     q_exetarg_next;   /* EXE queue per target          */
         SCSI_UEXACT8      reserved[6];
      } qExetargNext;
   } scb0to7;
   union
   {
      SCSI_UEXACT8      scdb[12];            /* SCSI CDB                      */
      struct 
      {
         SCSI_UEXACT8      scdb0;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb1;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb2;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb3;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb4;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb5;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb6;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb7;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb8;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb9;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb10;           /* SCSI CDB                      */
         SCSI_UEXACT8      scdb11;           /* SCSI CDB                      */
      } scbCDB;
      SCSI_UEXACT8      scdb_pointer[8];     /* cdb pointer                   */
      struct 
      {
         SCSI_UEXACT8      scdb_pointer0;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer1;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer2;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer3;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer4;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer5;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer6;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer7;    /* cdb pointer                   */   
      } scbCDBPTR;
      struct 
      {
         SCSI_UEXACT8      sdata_residue0;   /* total # of bytes not xfer     */
         SCSI_UEXACT8      sdata_residue1;   /* total # of bytes not xfer     */
         SCSI_UEXACT8      sdata_residue2;   /* total # of bytes not xfer     */
         SCSI_UEXACT8      reserved[4];      /* working flags                 */
         SCSI_UEXACT8      sg_cache_work;    /* working cache                 */
         SCSI_UEXACT8      sg_pointer_work0; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work1; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work2; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work3; /* working s/g list pointer      */
      } scbResidualAndWork;
   } scb8to19;
   union
   {
      SCSI_UEXACT16     q_next;              /* point to next for execution   */
      SCSI_UEXACT16     array_site;          /* SCB array # (SCB destination) */
   } scb5;
   SCSI_UEXACT8      atn_length;             /* attention management          */
   SCSI_UEXACT8      scdb_length;            /* cdb length                    */
#if (OSD_BUS_ADDRESS_SIZE == 64)
   SCSI_UEXACT8      saddress0;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress1;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress2;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress3;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress4;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress5;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress6;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress7;              /* 1st s/g segment               */
   SCSI_UEXACT8      slength0;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength1;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength2;               /* 1st s/g segment               */
#else
   SCSI_UEXACT8      saddress0;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress1;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress2;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress3;              /* 1st s/g segment               */
   SCSI_UEXACT8      slength0;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength1;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength2;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding0;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding1;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding2;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding3;               /* 1st s/g segment               */
#endif /* OSD_BUS_ADDRESS_SIZE */
   SCSI_UEXACT8      task_attribute;         /* Cmd IU Task Attribute         */
   SCSI_UEXACT8      task_management;        /* Cmd IU Task Management Flags  */
   SCSI_UEXACT8      SCB_flags;              /* Flags                         */
   SCSI_UEXACT8      scontrol;               /* scb control bits              */
   SCSI_UEXACT8      sg_cache_SCB;           /* # of SCSI_UEXACT8s for each element   */
   union 
   {
      struct 
      {
         SCSI_UEXACT8      sg_pointer_SCB0;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB1;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB2;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB3;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB4;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB5;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB6;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB7;  /* pointer to s/g list           */
      } scbSG;
      struct 
      {
         SCSI_UEXACT8      special_opcode;   /* opcode for special scb func   */
         SCSI_UEXACT8      special_info;     /* special info                  */
      } scbSpecial;
   } scb40to47;
   SCSI_UEXACT8      slun;                   /* lun                           */
   SCSI_UEXACT8      starget;                /* target                        */
   SCSI_UEXACT8      reserved[14];           /* reserved                      */
} SCSI_SCB_DOWNSHIFT_U320;

#define  SCSI_DU320_sg_pointer_SCB0       scb40to47.scbSG.sg_pointer_SCB0
#define  SCSI_DU320_sg_pointer_SCB1       scb40to47.scbSG.sg_pointer_SCB1
#define  SCSI_DU320_sg_pointer_SCB2       scb40to47.scbSG.sg_pointer_SCB2
#define  SCSI_DU320_sg_pointer_SCB3       scb40to47.scbSG.sg_pointer_SCB3
#define  SCSI_DU320_sg_pointer_SCB4       scb40to47.scbSG.sg_pointer_SCB4
#define  SCSI_DU320_sg_pointer_SCB5       scb40to47.scbSG.sg_pointer_SCB5
#define  SCSI_DU320_sg_pointer_SCB6       scb40to47.scbSG.sg_pointer_SCB6
#define  SCSI_DU320_sg_pointer_SCB7       scb40to47.scbSG.sg_pointer_SCB7

#define  SCSI_DU320_special_opcode        scb40to47.scbSpecial.special_opcode
#define  SCSI_DU320_special_info          scb40to47.scbSpecial.special_info

#define  SCSI_DU320_scdb                  scb8to19.scdb
#define  SCSI_DU320_scdb0                 scb8to19.scbCDB.scdb0 
#define  SCSI_DU320_scdb1                 scb8to19.scbCDB.scdb1
#define  SCSI_DU320_scdb2                 scb8to19.scbCDB.scdb2
#define  SCSI_DU320_scdb3                 scb8to19.scbCDB.scdb3
#define  SCSI_DU320_scdb4                 scb8to19.scbCDB.scdb4
#define  SCSI_DU320_scdb5                 scb8to19.scbCDB.scdb5
#define  SCSI_DU320_scdb6                 scb8to19.scbCDB.scdb6
#define  SCSI_DU320_scdb7                 scb8to19.scbCDB.scdb7
#define  SCSI_DU320_scdb8                 scb8to19.scbCDB.scdb8
#define  SCSI_DU320_scdb9                 scb8to19.scbCDB.scdb9
#define  SCSI_DU320_scdb10                scb8to19.scbCDB.scdb10
#define  SCSI_DU320_scdb11                scb8to19.scbCDB.scdb11

#define  SCSI_DU320_scdb_pointer          scb8to19.scdb_pointer

#define  SCSI_DU320_scdb_pointer0         scb8to19.scbCDBPTR.scdb_pointer0
#define  SCSI_DU320_scdb_pointer1         scb8to19.scbCDBPTR.scdb_pointer1
#define  SCSI_DU320_scdb_pointer2         scb8to19.scbCDBPTR.scdb_pointer2
#define  SCSI_DU320_scdb_pointer3         scb8to19.scbCDBPTR.scdb_pointer3
#define  SCSI_DU320_scdb_pointer4         scb8to19.scbCDBPTR.scdb_pointer4
#define  SCSI_DU320_scdb_pointer5         scb8to19.scbCDBPTR.scdb_pointer5
#define  SCSI_DU320_scdb_pointer6         scb8to19.scbCDBPTR.scdb_pointer6
#define  SCSI_DU320_scdb_pointer7         scb8to19.scbCDBPTR.scdb_pointer7

#define  SCSI_DU320_sdata_residue0        scb8to19.scbResidualAndWork.sdata_residue0
#define  SCSI_DU320_sdata_residue1        scb8to19.scbResidualAndWork.sdata_residue1
#define  SCSI_DU320_sdata_residue2        scb8to19.scbResidualAndWork.sdata_residue2
#define  SCSI_DU320_sg_cache_work         scb8to19.scbResidualAndWork.sg_cache_work  
#define  SCSI_DU320_sg_pointer_work0      scb8to19.scbResidualAndWork.sg_pointer_work0
#define  SCSI_DU320_sg_pointer_work1      scb8to19.scbResidualAndWork.sg_pointer_work1
#define  SCSI_DU320_sg_pointer_work2      scb8to19.scbResidualAndWork.sg_pointer_work2
#define  SCSI_DU320_sg_pointer_work3      scb8to19.scbResidualAndWork.sg_pointer_work3

#define  SCSI_DU320_q_next                scb5.q_next
#define  SCSI_DU320_array_site            scb5.array_site

#define  SCSI_DU320_q_exetarg_next        scb0to7.qExetargNext.q_exetarg_next
#define  SCSI_DU320_next_SCB_addr0        scb0to7.nextScbAddr.next_SCB_addr0
#define  SCSI_DU320_next_SCB_addr1        scb0to7.nextScbAddr.next_SCB_addr1
#define  SCSI_DU320_next_SCB_addr2        scb0to7.nextScbAddr.next_SCB_addr2
#define  SCSI_DU320_next_SCB_addr3        scb0to7.nextScbAddr.next_SCB_addr3
#define  SCSI_DU320_next_SCB_addr4        scb0to7.nextScbAddr.next_SCB_addr4
#define  SCSI_DU320_next_SCB_addr5        scb0to7.nextScbAddr.next_SCB_addr5
#define  SCSI_DU320_next_SCB_addr6        scb0to7.nextScbAddr.next_SCB_addr6
#define  SCSI_DU320_next_SCB_addr7        scb0to7.nextScbAddr.next_SCB_addr7

/* definitions for scontrol   */
#define  SCSI_DU320_SIMPLETAG    0x00     /* simple tag                 */
#define  SCSI_DU320_HEADTAG      0x01     /* head of queue tag          */
#define  SCSI_DU320_ORDERTAG     0x02     /* ordered tag                */
#define  SCSI_DU320_SPECFUN      0x08     /* special function           */
#define  SCSI_DU320_ABORTED      0x10     /* aborted flag               */
#define  SCSI_DU320_TAGENB       0x20     /* tag enable                 */
#define  SCSI_DU320_DISCENB      0x40     /* disconnect enable          */
#define  SCSI_DU320_TAGMASK      0x03     /* mask for tags              */
#if SCSI_PARITY_PER_IOB
#define  SCSI_DU320_ENPAR        0x80     /* disable parity checking    */
#endif /* SCSI_PARITY_PER_IOB */

/* definitions for starg_lun */
#define  SCSI_DU320_TARGETID     0xF0     /* target id                  */
#define  SCSI_DU320_TARGETLUN    0x07     /* target lun                 */

/* Definition for size of SCB area allocated for a CDB. */
/* When the CDB cannot be embedded in the SCB (i.e. greater 
 * than this size) the CDB is copied to the CDB field in the 
 * IOB reserve area and the physical memory address of this 
 * area is embedded in the SCB. This allows the sequencer
 * to DMA the CDB during Command phase.
 */
#define  SCSI_DU320_SCDB_SIZE    0x0C
     
/* definitions for scdb_length */
#define  SCSI_DU320_CDBLENMSK    0x1F     /* CDB length                 */
#define  SCSI_DU320_CACHE_ADDR_MASK 0x7C  /* Mask out rollover & lo 2 bits */

/* definitions for sg_cache_SCB */
#define  SCSI_DU320_NODATA       0x01     /* no data transfer involoved */
#define  SCSI_DU320_ONESGSEG     0x02     /* only one sg segment        */

/* definitions for special function opcode */
#define  SCSI_DU320_MSGTOTARG    0x00     /* special opcode, msg_to_targ */

#endif /* SCSI_DOWNSHIFT_U320_MODE */

/* Fixed common share info in SCB use by BIOS and CHIM */
#define  SCSI_U320_SCBFF_PTR    0xFF


                                              
/***************************************************************************
*  Downshift Enhanced Ultra 320 SCB format layout
***************************************************************************/
#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE

/* structure definition */
typedef struct SCSI_SCB_DOWNSHIFT_ENH_U320_
{
   union
   {
      struct
      {
         SCSI_UEXACT8      next_SCB_addr0;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr1;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr2;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr3;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr4;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr5;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr6;   /* next SCB address              */
         SCSI_UEXACT8      next_SCB_addr7;   /* next SCB address              */
      } nextScbAddr;
      struct
      {
         SCSI_UEXACT16     q_exetarg_next;   /* EXE queue per target          */
         SCSI_UEXACT8      reserved[6];
      } qExetargNext;
   } scb0to7;
   union
   {
      SCSI_UEXACT8      scdb[12];            /* SCSI CDB                      */
      struct 
      {
         SCSI_UEXACT8      scdb0;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb1;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb2;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb3;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb4;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb5;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb6;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb7;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb8;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb9;            /* SCSI CDB                      */
         SCSI_UEXACT8      scdb10;           /* SCSI CDB                      */
         SCSI_UEXACT8      scdb11;           /* SCSI CDB                      */
      } scbCDB;
      SCSI_UEXACT8      scdb_pointer[8];     /* cdb pointer                   */
      struct 
      {
         SCSI_UEXACT8      scdb_pointer0;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer1;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer2;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer3;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer4;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer5;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer6;    /* cdb pointer                   */   
         SCSI_UEXACT8      scdb_pointer7;    /* cdb pointer                   */   
      } scbCDBPTR;
      struct 
      {
         SCSI_UEXACT8      sdata_residue0;   /* total # of bytes not xfer     */
         SCSI_UEXACT8      sdata_residue1;   /* total # of bytes not xfer     */
         SCSI_UEXACT8      sdata_residue2;   /* total # of bytes not xfer     */
         SCSI_UEXACT8      reserved[4];      /* working flags                 */
         SCSI_UEXACT8      sg_cache_work;    /* working cache                 */
         SCSI_UEXACT8      sg_pointer_work0; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work1; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work2; /* working s/g list pointer      */
         SCSI_UEXACT8      sg_pointer_work3; /* working s/g list pointer      */
      } scbResidualAndWork;
   } scb8to19;
   union
   {
      SCSI_UEXACT16     q_next;              /* point to next for execution   */
      SCSI_UEXACT16     array_site;          /* SCB array # (SCB destination) */
   } scb5;
   SCSI_UEXACT8      atn_length;             /* attention management          */
   SCSI_UEXACT8      scdb_length;            /* cdb length                    */
#if (OSD_BUS_ADDRESS_SIZE == 64)
   SCSI_UEXACT8      saddress0;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress1;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress2;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress3;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress4;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress5;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress6;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress7;              /* 1st s/g segment               */
   SCSI_UEXACT8      slength0;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength1;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength2;               /* 1st s/g segment               */
#else
   SCSI_UEXACT8      saddress0;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress1;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress2;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress3;              /* 1st s/g segment               */
   SCSI_UEXACT8      slength0;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength1;               /* 1st s/g segment               */
   SCSI_UEXACT8      slength2;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding0;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding1;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding2;               /* 1st s/g segment               */
   SCSI_UEXACT8      padding3;               /* 1st s/g segment               */
#endif /* OSD_BUS_ADDRESS_SIZE */
   SCSI_UEXACT8      task_attribute;         /* Cmd IU Task Attribute         */
   SCSI_UEXACT8      task_management;        /* Cmd IU Task Management Flags  */
   SCSI_UEXACT8      SCB_flags;              /* Flags                         */
   SCSI_UEXACT8      scontrol;               /* scb control bits              */
   SCSI_UEXACT8      sg_cache_SCB;           /* # of SCSI_UEXACT8s for each element   */
   union 
   {
      struct 
      {
         SCSI_UEXACT8      sg_pointer_SCB0;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB1;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB2;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB3;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB4;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB5;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB6;  /* pointer to s/g list           */
         SCSI_UEXACT8      sg_pointer_SCB7;  /* pointer to s/g list           */
      } scbSG;
      struct 
      {
         SCSI_UEXACT8      special_opcode;   /* opcode for special scb func   */
         SCSI_UEXACT8      special_info;     /* special info                  */
      } scbSpecial;
   } scb40to47;
   SCSI_UEXACT8      slun;                   /* lun                           */
   SCSI_UEXACT8      starget;                /* target                        */
   SCSI_UEXACT8      reserved[14];           /* reserved                      */
} SCSI_SCB_DOWNSHIFT_ENH_U320;

#define  SCSI_DEU320_sg_pointer_SCB0       scb40to47.scbSG.sg_pointer_SCB0
#define  SCSI_DEU320_sg_pointer_SCB1       scb40to47.scbSG.sg_pointer_SCB1
#define  SCSI_DEU320_sg_pointer_SCB2       scb40to47.scbSG.sg_pointer_SCB2
#define  SCSI_DEU320_sg_pointer_SCB3       scb40to47.scbSG.sg_pointer_SCB3
#define  SCSI_DEU320_sg_pointer_SCB4       scb40to47.scbSG.sg_pointer_SCB4
#define  SCSI_DEU320_sg_pointer_SCB5       scb40to47.scbSG.sg_pointer_SCB5
#define  SCSI_DEU320_sg_pointer_SCB6       scb40to47.scbSG.sg_pointer_SCB6
#define  SCSI_DEU320_sg_pointer_SCB7       scb40to47.scbSG.sg_pointer_SCB7

#define  SCSI_DEU320_special_opcode        scb40to47.scbSpecial.special_opcode
#define  SCSI_DEU320_special_info          scb40to47.scbSpecial.special_info

#define  SCSI_DEU320_scdb                  scb8to19.scdb
#define  SCSI_DEU320_scdb0                 scb8to19.scbCDB.scdb0 
#define  SCSI_DEU320_scdb1                 scb8to19.scbCDB.scdb1
#define  SCSI_DEU320_scdb2                 scb8to19.scbCDB.scdb2
#define  SCSI_DEU320_scdb3                 scb8to19.scbCDB.scdb3
#define  SCSI_DEU320_scdb4                 scb8to19.scbCDB.scdb4
#define  SCSI_DEU320_scdb5                 scb8to19.scbCDB.scdb5
#define  SCSI_DEU320_scdb6                 scb8to19.scbCDB.scdb6
#define  SCSI_DEU320_scdb7                 scb8to19.scbCDB.scdb7
#define  SCSI_DEU320_scdb8                 scb8to19.scbCDB.scdb8
#define  SCSI_DEU320_scdb9                 scb8to19.scbCDB.scdb9
#define  SCSI_DEU320_scdb10                scb8to19.scbCDB.scdb10
#define  SCSI_DEU320_scdb11                scb8to19.scbCDB.scdb11

#define  SCSI_DEU320_scdb_pointer          scb8to19.scdb_pointer

#define  SCSI_DEU320_scdb_pointer0         scb8to19.scbCDBPTR.scdb_pointer0
#define  SCSI_DEU320_scdb_pointer1         scb8to19.scbCDBPTR.scdb_pointer1
#define  SCSI_DEU320_scdb_pointer2         scb8to19.scbCDBPTR.scdb_pointer2
#define  SCSI_DEU320_scdb_pointer3         scb8to19.scbCDBPTR.scdb_pointer3
#define  SCSI_DEU320_scdb_pointer4         scb8to19.scbCDBPTR.scdb_pointer4
#define  SCSI_DEU320_scdb_pointer5         scb8to19.scbCDBPTR.scdb_pointer5
#define  SCSI_DEU320_scdb_pointer6         scb8to19.scbCDBPTR.scdb_pointer6
#define  SCSI_DEU320_scdb_pointer7         scb8to19.scbCDBPTR.scdb_pointer7

#define  SCSI_DEU320_sdata_residue0        scb8to19.scbResidualAndWork.sdata_residue0
#define  SCSI_DEU320_sdata_residue1        scb8to19.scbResidualAndWork.sdata_residue1
#define  SCSI_DEU320_sdata_residue2        scb8to19.scbResidualAndWork.sdata_residue2
#define  SCSI_DEU320_sg_cache_work         scb8to19.scbResidualAndWork.sg_cache_work  
#define  SCSI_DEU320_sg_pointer_work0      scb8to19.scbResidualAndWork.sg_pointer_work0
#define  SCSI_DEU320_sg_pointer_work1      scb8to19.scbResidualAndWork.sg_pointer_work1
#define  SCSI_DEU320_sg_pointer_work2      scb8to19.scbResidualAndWork.sg_pointer_work2
#define  SCSI_DEU320_sg_pointer_work3      scb8to19.scbResidualAndWork.sg_pointer_work3

#define  SCSI_DEU320_q_next                scb5.q_next
#define  SCSI_DEU320_array_site            scb5.array_site

#define  SCSI_DEU320_q_exetarg_next        scb0to7.qExetargNext.q_exetarg_next
#define  SCSI_DEU320_next_SCB_addr0        scb0to7.nextScbAddr.next_SCB_addr0
#define  SCSI_DEU320_next_SCB_addr1        scb0to7.nextScbAddr.next_SCB_addr1
#define  SCSI_DEU320_next_SCB_addr2        scb0to7.nextScbAddr.next_SCB_addr2
#define  SCSI_DEU320_next_SCB_addr3        scb0to7.nextScbAddr.next_SCB_addr3
#define  SCSI_DEU320_next_SCB_addr4        scb0to7.nextScbAddr.next_SCB_addr4
#define  SCSI_DEU320_next_SCB_addr5        scb0to7.nextScbAddr.next_SCB_addr5
#define  SCSI_DEU320_next_SCB_addr6        scb0to7.nextScbAddr.next_SCB_addr6
#define  SCSI_DEU320_next_SCB_addr7        scb0to7.nextScbAddr.next_SCB_addr7

/* definitions for scontrol   */
#define  SCSI_DEU320_SIMPLETAG    0x00     /* simple tag                 */
#define  SCSI_DEU320_HEADTAG      0x01     /* head of queue tag          */
#define  SCSI_DEU320_ORDERTAG     0x02     /* ordered tag                */
#define  SCSI_DEU320_SPECFUN      0x08     /* special function           */
#define  SCSI_DEU320_ABORTED      0x10     /* aborted flag               */
#define  SCSI_DEU320_TAGENB       0x20     /* tag enable                 */
#define  SCSI_DEU320_DISCENB      0x40     /* disconnect enable          */
#define  SCSI_DEU320_TAGMASK      0x03     /* mask for tags              */
#if SCSI_PARITY_PER_IOB
#define  SCSI_DEU320_ENPAR        0x80     /* disable parity checking    */
#endif /* SCSI_PARITY_PER_IOB */

/* definitions for starg_lun */
#define  SCSI_DEU320_TARGETID     0xF0     /* target id                  */
#define  SCSI_DEU320_TARGETLUN    0x07     /* target lun                 */

/* Definition for size of SCB area allocated for a CDB. */
/* When the CDB cannot be embedded in the SCB (i.e. greater 
 * than this size) the CDB is copied to the CDB field in the 
 * IOB reserve area and the physical memory address of this 
 * area is embedded in the SCB. This allows the sequencer
 * to DMA the CDB during Command phase.
 */
#define  SCSI_DEU320_SCDB_SIZE    0x0C
     
/* definitions for scdb_length */
#define  SCSI_DEU320_CDBLENMSK    0x1F     /* CDB length                 */
#define  SCSI_DEU320_CACHE_ADDR_MASK 0x7C  /* Mask out rollover & lo 2 bits */

/* definitions for sg_cache_SCB */
#define  SCSI_DEU320_NODATA       0x01     /* no data transfer involoved */
#define  SCSI_DEU320_ONESGSEG     0x02     /* only one sg segment        */

/* definitions for special function opcode */
#define  SCSI_DEU320_MSGTOTARG    0x00     /* special opcode, msg_to_targ */

#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */






/***************************************************************************
*  DCH_SCSI Ultra 320 SCB format layout
***************************************************************************/
#if SCSI_DCH_U320_MODE

/* structure definition */
typedef struct SCSI_SCB_DCH_U320_
{
   SCSI_UEXACT8      scbByte0;            /* next SCB address for 8 bytes  */
                                          /* OR q_exetarg_next for 2 bytes */
   SCSI_UEXACT8      scbByte1;
   SCSI_UEXACT8      scbByte2;
   SCSI_UEXACT8      scbByte3;
   SCSI_UEXACT8      scbByte4;
   SCSI_UEXACT8      scbByte5;
   SCSI_UEXACT8      scbByte6;
   SCSI_UEXACT8      scbByte7;
   SCSI_UEXACT8      scbByte8;            /* SCSI CDB for 12 bytes OR      */
                                          /* cdb pointer for 8 bytes OR    */
                                          /* residue for 4 bytes.          */
   SCSI_UEXACT8      scbByte9;            
   SCSI_UEXACT8      scbByte10;           
   SCSI_UEXACT8      scbByte11;           /* residue for 4 bytes or working flags */
   SCSI_UEXACT8      scbByte12;
   SCSI_UEXACT8      scbByte13;
   SCSI_UEXACT8      scbByte14;           /* working cache ptr             */
   SCSI_UEXACT8      scbByte15;           /* working cache flags           */
   SCSI_UEXACT8      scbByte16;           /* working s/g list pointer for  */
   SCSI_UEXACT8      scbByte17;           /* 4 bytes                       */
   SCSI_UEXACT8      scbByte18;
   SCSI_UEXACT8      scbByte19;
   SCSI_UEXACT8      scbByte20;           /* array_site OR q_next          */
   SCSI_UEXACT8      scbByte21;           /* array_site OR q_next          */
   SCSI_UEXACT8      atn_length;          /* attention management          */
   SCSI_UEXACT8      scdb_length;         /* cdb length                    */
   
/* 64 bit SGL address */   
   SCSI_UEXACT8      saddress0;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress1;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress2;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress3;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress4;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress5;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress6;              /* 1st s/g segment               */
   SCSI_UEXACT8      saddress7;              /* 1st s/g segment               */

/* sgl length 32 bits... */   

   SCSI_UEXACT8      slength0;               /* 1st s/g count              */
   SCSI_UEXACT8      slength1;               /* 1st s/g count              */
   SCSI_UEXACT8      slength2;               /* 1st s/g count              */
   SCSI_UEXACT8      slength3;               /* 1st s/g count              */
   
/* task attribute moved to byte 39 */
   
/* SCSI_UEXACT8      stypecode                  typecode for target mode SCB  */
                                             /* defined in establish SCB      */
   SCSI_UEXACT8      task_management;        /* Cmd IU Task Management Flags  */
   SCSI_UEXACT8      SCB_flags;              /* Flags                         */
   SCSI_UEXACT8      scontrol;               /* scb control bits              */
   
   SCSI_UEXACT8      task_attribute;         /* Cmd IU Task Attribute         */

         /* sg_cache_SCB increased to 16 bits and moved to offset 60   */
         
   SCSI_UEXACT8      scbByte40;           /* pointer to s/g list for 8     */
                                          /* OR special_opcode for 1 byte  */
   SCSI_UEXACT8      scbByte41;           /* special_info                  */
   SCSI_UEXACT8      scbByte42;
   SCSI_UEXACT8      scbByte43;
   SCSI_UEXACT8      scbByte44;
   SCSI_UEXACT8      scbByte45;
   SCSI_UEXACT8      scbByte46;
   SCSI_UEXACT8      scbByte47;
   SCSI_UEXACT8      slun;                /* lun                           */
   SCSI_UEXACT8      starget;             /* target ID OR our ID for       */
                                          /* reestablish_SCB               */
   SCSI_UEXACT8      mirror_SCB;          /* SCB for mirrored operation */
   SCSI_UEXACT8      mirror_SCB1;         /* SCB for mirrored operation    */
   SCSI_UEXACT8      mirror_slun;         /* Mirrored LUN */
   SCSI_UEXACT8      mirror_starget;      /* Mirrored target */
   SCSI_UEXACT8      scontrol1;           /* scb control1 bits             */
   SCSI_UEXACT8      reserved[5];         /* reserved                      */
   
      /* NEW Layout for sg_cache_SCB */
      /* bit 0 = Last Segment done  */
      /* bit 2:1 = reserved         */
      /* bits 3 = SEGVALID          */
      /* bits 5:4 = ASEL            */
      /* bits 6 = LL (last list)    */
      /* bits 7 = LE (last element) */
      /* bits 15:8 = SG cache ptr   */
                                             
   SCSI_UEXACT8     sg_cache_SCB;         /* flags & ASEL bits  */
   SCSI_UEXACT8     sg_cache_ptr;         /* cache ptr */
   SCSI_UEXACT8      reserved1;           /* reserved                      */
   SCSI_UEXACT8      busy_target;         /* busy target                   */
} SCSI_SCB_DCH_U320;

/* These scb field defines must only be used as offsets within the
 * scb. There is no field size associated with the define.
 */
#define  SCSI_DCHU320_sg_pointer_SCB0       scbByte40
#define  SCSI_DCHU320_sg_pointer_SCB1       scbByte41
#define  SCSI_DCHU320_sg_pointer_SCB2       scbByte42
#define  SCSI_DCHU320_sg_pointer_SCB3       scbByte43
#define  SCSI_DCHU320_sg_pointer_SCB4       scbByte44
#define  SCSI_DCHU320_sg_pointer_SCB5       scbByte45
#define  SCSI_DCHU320_sg_pointer_SCB6       scbByte46
#define  SCSI_DCHU320_sg_pointer_SCB7       scbByte47

#define  SCSI_DCHU320_special_opcode        scbByte40
#define  SCSI_DCHU320_special_info          scbByte41

#define  SCSI_DCHU320_scdb                  scbByte8
#define  SCSI_DCHU320_scdb0                 scbByte8 
#define  SCSI_DCHU320_scdb1                 scbByte9
#define  SCSI_DCHU320_scdb2                 scbByte10
#define  SCSI_DCHU320_scdb3                 scbByte11
#define  SCSI_DCHU320_scdb4                 scbByte12
#define  SCSI_DCHU320_scdb5                 scbByte13
#define  SCSI_DCHU320_scdb6                 scbByte14
#define  SCSI_DCHU320_scdb7                 scbByte15
#define  SCSI_DCHU320_scdb8                 scbByte16
#define  SCSI_DCHU320_scdb9                 scbByte17
#define  SCSI_DCHU320_scdb10                scbByte18
#define  SCSI_DCHU320_scdb11                scbByte19

#define  SCSI_DCHU320_scdb_pointer          scbByte8

#define  SCSI_DCHU320_scdb_pointer0         scbByte8
#define  SCSI_DCHU320_scdb_pointer1         scbByte9
#define  SCSI_DCHU320_scdb_pointer2         scbByte10
#define  SCSI_DCHU320_scdb_pointer3         scbByte11
#define  SCSI_DCHU320_scdb_pointer4         scbByte12
#define  SCSI_DCHU320_scdb_pointer5         scbByte13
#define  SCSI_DCHU320_scdb_pointer6         scbByte14
#define  SCSI_DCHU320_scdb_pointer7         scbByte15

#define  SCSI_DCHU320_sdata_residue0        scbByte8
#define  SCSI_DCHU320_sdata_residue1        scbByte9
#define  SCSI_DCHU320_sdata_residue2        scbByte10
#define  SCSI_DCHU320_sdata_residue3        scbByte11


#define  SCSI_DCHU320_sg_pointer_work0      scbByte16
#define  SCSI_DCHU320_sg_pointer_work1      scbByte17
#define  SCSI_DCHU320_sg_pointer_work2      scbByte18
#define  SCSI_DCHU320_sg_pointer_work3      scbByte19

#if SCSI_TARGET_OPERATION
#define  SCSI_DCHU320_sinitiator            starget
#define  SCSI_DCHU320_starg_tagno           scbByte8
#define  SCSI_DCHU320_starg_status          scbByte11
#define  SCSI_DCHU320_stypecode             task_attribute

#endif /* SCSI_TARGET_OPERATION */

#define  SCSI_DCHU320_q_next                scbByte20
#define  SCSI_DCHU320_array_site            scbByte20

#define  SCSI_DCHU320_q_exetarg_next        scbByte0
#define  SCSI_DCHU320_next_SCB_addr0        scbByte0
#define  SCSI_DCHU320_next_SCB_addr1        scbByte1
#define  SCSI_DCHU320_next_SCB_addr2        scbByte2
#define  SCSI_DCHU320_next_SCB_addr3        scbByte3
#define  SCSI_DCHU320_next_SCB_addr4        scbByte4
#define  SCSI_DCHU320_next_SCB_addr5        scbByte5
#define  SCSI_DCHU320_next_SCB_addr6        scbByte6
#define  SCSI_DCHU320_next_SCB_addr7        scbByte7


/* definitions for scontrol   */
#define  SCSI_DCHU320_SIMPLETAG    0x00     /* simple tag                 */
#define  SCSI_DCHU320_HEADTAG      0x01     /* head of queue tag          */
#define  SCSI_DCHU320_ORDERTAG     0x02     /* ordered tag                */
#define  SCSI_DCHU320_SPECFUN      0x08     /* special function           */
#define  SCSI_DCHU320_ABORTED      0x10     /* scb aborted flag           */
#define  SCSI_DCHU320_TAGENB       0x20     /* tag enable                 */
#define  SCSI_DCHU320_DISCENB      0x40     /* disconnect enable          */
#define  SCSI_DCHU320_TAGMASK      0x03     /* mask for tags              */
#if SCSI_PARITY_PER_IOB
#define  SCSI_DCHU320_ENPAR        0x80     /* disable parity checking    */
#endif /* SCSI_PARITY_PER_IOB */
#if SCSI_TARGET_OPERATION
#define  SCSI_DCHU320_TARGETENB    0x80     /* target mode enable         */
#define  SCSI_DCHU320_HOLDONBUS    0x02     /* hold-on to SCSI BUS        */
#endif /* SCSI_TARGET_OPERATION */

/* definitions for starg_lun */
#define  SCSI_DCHU320_TARGETID     0xF0     /* target id                  */
#define  SCSI_DCHU320_TARGETLUN    0x07     /* target lun                 */

#if SCSI_TARGET_OPERATION
/* definitions for stypecode bits 0 - 2 */
#define  SCSI_DCHU320_DATAOUT              0x00
#define  SCSI_DCHU320_DATAIN               0x01
#define  SCSI_DCHU320_GOODSTATUS           0x02
#define  SCSI_DCHU320_BADSTATUS            0x03
#define  SCSI_DCHU320_DATAOUT_AND_STATUS   0x04
#define  SCSI_DCHU320_DATAIN_AND_STATUS    0x05
#define  SCSI_DCHU320_EMPTYSCB             0x07
#endif /* SCSIDCH_TARGET_OPERATION */

#if (SCSI_DOMAIN_VALIDATION_BASIC+SCSI_DOMAIN_VALIDATION_ENHANCED)
/* definitions for dv_control */
#define  SCSI_DCHU320_DV_ENABLE  0x01        /* Identify domain validation SCB */
#endif /* (SCSI_DOMAIN_VALIDATION_BASIC+SCSI_DOMAIN_VALIDATION_ENHANCED) */

/* Definition for size of SCB area allocated for a CDB. */
/* When the CDB cannot be embedded in the SCB (i.e. greater 
 * than this size) the CDB is copied to the CDB field in the 
 * IOB reserve area and the physical memory address of this 
 * area is embedded in the SCB. This allows the sequencer
 * to DMA the CDB during Command phase.
 */
#define  SCSI_DCHU320_SCDB_SIZE    0x0C
     
#if SCSI_TARGET_OPERATION
/* definitions for starget */
#define  SCSI_DCHU320_OWNID_MASK   0x0F     /* Mask of starget field      */
#endif /* SCSI_TARGET_OPERATION */

/* definitions for scdb_length */
#define  SCSI_DCHU320_CDBLENMSK    0x1F     /* CDB length                 */
#define  SCSI_DCHU320_CACHE_ADDR_MASK 0x7C  /* Mask out rollover & lo 2 bits */

/* definitions for sg_cache_SCB */
#define  SCSI_DCHU320_NODATA       0x0001     /* no data transfer involoved */
#define  SCSI_DCHU320_ONESGSEG     0x0040     /* Changed to use Last List bit  */
#define  SCSI_DCHU320_LAST_ELEMENT 0x0080     /* Last Element flag bit         */
#define  SCSI_DCHU320_ASEL_MASK    0x30       /* Memory select field mask */
#define  SCSI_DCHU320_3BYTE        0x18       /* shift count for ASEL fields */
#define  SCSI_DCHU320_SG_CACHE_MASK 0xff00     /* SG cache ptr fields.          */

/* definitions for special function opcode */
#define  SCSI_DCHU320_MSGTOTARG    0x00     /* special opcode, msg_to_targ */

/***************************************************************************
*  DCH Ultra 320 Establish Connection SCB format layout
***************************************************************************/
#if SCSI_TARGET_OPERATION

/* All elements are single byte to avoid realignment by compilers. */ 
/* structure definition */
typedef struct SCSI_EST_SCB_DCH_U320_
{
   SCSI_UEXACT8      scbByte0;               /* next SCB address for 8 bytes  */
   SCSI_UEXACT8      scbByte1;
   SCSI_UEXACT8      scbByte2;
   SCSI_UEXACT8      scbByte3;
   SCSI_UEXACT8      scbByte4;
   SCSI_UEXACT8      scbByte5;
   SCSI_UEXACT8      scbByte6;
   SCSI_UEXACT8      scbByte7;
   SCSI_UEXACT8      scbByte8;
   SCSI_UEXACT8      scbByte9;
   SCSI_UEXACT8      stypecode;              /* SCB type - empty SCB          */
   SCSI_UEXACT8      scbByte11;
   SCSI_UEXACT8      scbByte12;
   SCSI_UEXACT8      scbByte13;
   SCSI_UEXACT8      scbByte14;
   SCSI_UEXACT8      scbByte15;
   SCSI_UEXACT8      est_iid;                /* selecting initiator SCSI ID   */
   SCSI_UEXACT8      tag_type;               /* queue tag type received       */
   SCSI_UEXACT8      tag_num;                /* tag number received           */
   SCSI_UEXACT8      tag_num1;               /* not required for legacy       */
   SCSI_UEXACT8      scbByte20;              /* array_site OR q_next          */
   SCSI_UEXACT8      scbByte21;              /* array_site OR q_next          */
   SCSI_UEXACT8      scdb_len;               /* cbd length                    */
   SCSI_UEXACT8      id_msg;                 /* identify msg byte received    */
   SCSI_UEXACT8      scbaddress0;            /* RBI address of this scb       */
   SCSI_UEXACT8      scbaddress1;            /* RBI address of this scb       */
   SCSI_UEXACT8      scbaddress2;            /* RBI address of this scb       */
   SCSI_UEXACT8      scbaddress3;            /* RBI address of this scb       */
   SCSI_UEXACT8      scbaddress4;            /* RBI address of this scb       */
   SCSI_UEXACT8      scbaddress5;            /* RBI address of this scb       */
   SCSI_UEXACT8      scbaddress6;            /* RBI address of this scb       */
   SCSI_UEXACT8      scbaddress7;            /* RBI address of this scb       */
   SCSI_UEXACT8      last_byte;             /* last byte received             */
   SCSI_UEXACT8      flags;                 /* misc flags related to          */
                                            /* connection                     */
   SCSI_UEXACT8      eststatus;             /* status returned from sequencer */
   SCSI_UEXACT8      scbByte35;
   SCSI_UEXACT8      reserved1;
   SCSI_UEXACT8      SCB_flags;             /* Flags - packetized only        */
   SCSI_UEXACT8      scontrol;              /* scb control bits - only        */
                                            /* target_mode bit used.          */
   SCSI_UEXACT8      reserved[25];
} SCSI_EST_SCB_DCH_U320;

/* defines for cdb fields examined. */
#define  SCSI_EST_DCHU320_scdb0               scbByte0 
#define  SCSI_EST_DCHU320_scdb1               scbByte1

/* defines for flags */
#define  SCSI_EST_DCHU320_TAG_RCVD       0x80  /* queue tag message received */
#define  SCSI_EST_DCHU320_SCSI1_SEL      0x40  /* scsi1 selection received */
#define  SCSI_EST_DCHU320_BUS_HELD       0x20  /* scsi bus held */
#define  SCSI_EST_DCHU320_RSVD_BITS      0x10  /* reserved bit set in SCSI-2
                                              * identify message and
                                              * CHK_RSVD_BITS set
                                              */
#define  SCSI_EST_DCHU320_LUNTAR         0x08  /* luntar bit set in SCSI-2
                                              * identify message and
                                              * LUNTAR_EN = 0
                                              */
/* defines for last state */
/* are these still relevant ???? */
#define  SCSI_EST_DCHU320_CMD_PHASE      0x02
#define  SCSI_EST_DCHU320_MSG_OUT_PHASE  0x06
#define  SCSI_EST_DCHU320_MSG_IN_PHASE   0x07

/* definitions for eststatus */
#define  SCSI_EST_DCHU320_GOOD_STATE     0x00  /* command received without exception */
#define  SCSI_EST_DCHU320_SEL_STATE      0x01  /* exception during selection phase   */
#define  SCSI_EST_DCHU320_MSGID_STATE    0x02  /*     "        "   identifying phase */
#define  SCSI_EST_DCHU320_MSGOUT_STATE   0x03  /*     "        "   message out phase */
#define  SCSI_EST_DCHU320_CMD_STATE      0x04  /*     "        "   command phase     */
#define  SCSI_EST_DCHU320_DISC_STATE     0x05  /*     "        "   disconnect phase  */
#define  SCSI_EST_DCHU320_VENDOR_CMD     0x08  /* Vendor unique command */
/* These statuses are stored in the upper nibble of the returned status 
 * so we can handle multiple conditions at once.
 */
#define  SCSI_EST_DCHU320_LAST_RESOURCE  0x40  /* last resource used */
#define  SCSI_EST_DCHU320_PARITY_ERR     0x80  /* parity error       */
/* defines for estiid */
#define  SCSI_EST_DCHU320_SELID_MASK     0x0F   /* our selected ID */
/* misc defines */
#define  SCSI_EST_DCHU320_MAX_CDB_SIZE  16   /* maximum number of CDB
                                              * bytes that can be 
                                              * stored in the SCB.
                                              * Support of larger CDBs
                                              * requires HIM processing
                                              * (rather than command
                                              * complete handling).
                                              */
#endif /* SCSI_TARGET_OPERATION */
#endif /* SCSI_DCH_U320_MODE */


/***************************************************************************
* New mode SCSI_QOUTFIFO element structure
***************************************************************************/
typedef struct SCSI_QOUTFIFO_NEW_
{
   SCSI_UEXACT16 scbNumber;               /* scb number                    */
   SCSI_UEXACT8 padding[5];               /* padding to make DWORD allign  */
   SCSI_UEXACT8 quePassCnt;               /* queue pass count              */
} SCSI_QOUTFIFO_NEW;

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*  All Firmware Descriptor defintions should be referenced only if
*  SCSI_MULTI_MODE is enabled
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
#if SCSI_MULTI_MODE
/***************************************************************************
* FIRMWARE_DESCRIPTOR definition
***************************************************************************/
typedef struct SCSI_FIRMWARE_DESCRIPTOR_
{
   SCSI_INT firmwareVersion;              /* firmware version              */
   SCSI_INT ScbSize;                      /* SCB size                      */
   SCSI_UINT16  Siostr3Entry;             /* siostr3 entry                 */
   SCSI_UINT16  RetryIdentifyEntry;       /* retry identify entry          */
   SCSI_UINT16  StartLinkCmdEntry;        /* start link command entry addr */
   SCSI_UINT16  IdleLoopEntry;            /* idle loop entry               */
   SCSI_UINT16  IdleLoopTop;              /* idle loop top                 */
   SCSI_UINT16  IdleLoop0;                /* idle loop0                    */
   SCSI_UINT16  Sio204Entry;              /* sio 204 entry                 */
   SCSI_UINT16  ExpanderBreak;            /* bus expander break address    */
   SCSI_UINT16  Isr;                      /* sequencer isr address         */
#if SCSI_TARGET_OPERATION          
   SCSI_UINT16  TargetDataEntry;          /* target data entry             */
   SCSI_UINT16  TargetHeldBusEntry;       /* target held scsi bus entry    */
#endif /* SCSI_TARGET_OPERATION */  
   SCSI_UEXACT16 BtaTable;                /* busy target array             */
   SCSI_UEXACT16 BtaSize;                 /* busy target array size        */
#if !SCSI_SEQ_LOAD_USING_PIO
   SCSI_UEXACT16 LoadSize;                /* Loader code size              */
   SCSI_UEXACT8 SCSI_LPTR LoadCode;       /* Loader Code                   */
   SCSI_UEXACT16 LoadAddr;                /* address of seq buffer         */
   SCSI_UEXACT16 LoadCount;               /* number of bytes to be DMAed   */
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
   SCSI_UEXACT16 PassToDriver;            /* pass to driver from sequencer */
   SCSI_UEXACT16 ActiveScb;               /* active scb                    */
   SCSI_UEXACT16 RetAddr;                 /* return address                */
   SCSI_UEXACT16 QNewPointer;             /* pointer to new SCB            */
   SCSI_UEXACT16 EntPtBitmap;             /* bitmap for various ent points */
   SCSI_UEXACT16 SgStatus;                /* S/G status                    */
   SCSI_UEXACT16 FirstScb;                /* first_scb scratch RAM location*/  
   SCSI_UEXACT16 DvTimerLo;               /* dv_timerlo scratch RAM        */
                                          /* location                      */
   SCSI_UEXACT16 DvTimerHi;               /* dv_timerhi scratch RAM        */
                                          /* location                      */
#if SCSI_PACKETIZED_IO_SUPPORT
   SCSI_UEXACT16 SlunLength;              /* value for LUNLEN register     */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */

#if SCSI_TARGET_OPERATION 
   /* Scratch RAM entries specific to target mode */
   SCSI_UEXACT16 Reg0;                    /* reg0 scratch location         */ 
   SCSI_UEXACT16 QEstHead;                /* host est scb queue head offset */
   SCSI_UEXACT16 QEstTail;                /* host est scb queue tail offset */
   SCSI_UEXACT16 QExeTargHead;            /* target mode execution queue   */
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_RESOURCE_MANAGEMENT
   SCSI_UEXACT8 MaxNonTagScbs;            /* Max Non Tag SCBs              */
   SCSI_UEXACT8 reserved2[3];             /* for alignment and future use  */
#endif /* SCSI_RESOURCE_MANAGEMENT */
   SCSI_UEXACT16 ArpintValidReg;
   SCSI_UEXACT8 ArpintValidBit; 
   SCSI_UEXACT8 reserved3;
   SCSI_UEXACT8 (*SCSIhSetupSequencer)(SCSI_HHCB SCSI_HPTR); /* set driver sequencer   */
   void (*SCSIhResetSoftware)(SCSI_HHCB SCSI_HPTR); /* reset software         */
   void (*SCSIhDeliverScb)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* deliver scb */
   void (*SCSIhSetupAtnLength)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* setup atn_length */
   void (*SCSIhTargetClearBusy)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* target clear busy */
   void (*SCSIhRequestSense)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* request sense setup */
   void (*SCSIhResetBTA)(SCSI_HHCB SCSI_HPTR);     /* reset busy target array */
   void (*SCSIhGetConfig)(SCSI_HHCB SCSI_HPTR);     /* get configuration */
#if SCSI_SCBBFR_BUILTIN
   void (*SCSIhSetupAssignScbBuffer)(SCSI_HHCB SCSI_HPTR);
   void (*SCSIhAssignScbDescriptor)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   void (*SCSIhFreeScbDescriptor)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_SCBBFR_BUILTIN */
   void (*SCSIhXferRateAssign)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
   void (*SCSIhGetNegoXferRate)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
   void (*SCSIhAbortChannel)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);

#if !SCSI_ASPI_SUPPORT
#if !SCSI_ASPI_SUPPORT_GROUP1  
   void (*SCSIhAbortHIOB)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
   void (*SCSIhActiveAbort)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
   void (*SCSIhNonPackActiveAbort)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#if SCSI_PACKETIZED_IO_SUPPORT
   void (*SCSIhPackActiveAbort)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
   void (*SCSIhRemoveActiveAbort)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   
#endif /* !SCSI_ASPI_SUPPORT */
#if !SCSI_ASPI_SUPPORT_GROUP1
   void (*SCSIhUpdateAbortBitHostMem)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); 
#endif  /* !SCSI_ASPI_SUPPORT_GROUP1 */
   void (*SCSIhUpdateNextScbAddress)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_BUS_ADDRESS);
#if !SCSI_ASPI_SUPPORT_GROUP1   
   void (*SCSIhSetupTR)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
   SCSI_UEXACT32 (*SCSIhResidueCalc)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   void (*SCSIhIgnoreWideResidueCalc)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
   SCSI_UEXACT8 (*SCSIhEvenIOLength)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   SCSI_UEXACT8 (*SCSIhUnderrun) (SCSI_HHCB SCSI_HPTR);
   void (*SCSIhSetBreakPoint)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
   void (*SCSIhClearBreakPoint)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
#if SCSI_PACKETIZED_IO_SUPPORT
   void (*SCSIhPackNonPackQueueHIOB)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
   void (*SCSIhSetIntrFactorThreshold)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
   void (*SCSIhSetIntrThresholdCount)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
   SCSI_UEXACT8 (*SCSIhUpdateExeQAtnLength)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
   void (*SCSIhUpdateNewQAtnLength)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
   void (*SCSIhQHeadPNPSwitchSCB)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#if (OSD_BUS_ADDRESS_SIZE == 64)
   SCSI_UINT8 (*SCSIhCompareBusSGListAddress)(SCSI_HHCB SCSI_HPTR,SCSI_BUS_ADDRESS,SCSI_BUS_ADDRESS); 
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */
   void (*SCSIhSetDisconnectDelay)(SCSI_HHCB SCSI_HPTR);
   SCSI_UEXACT8 (*SCSIhQExeTargNextOffset)(void);
   SCSI_UEXACT8 (*SCSIhQNextOffset)(void);
   void (*SCSIhTargetResetSoftware)(SCSI_HHCB SCSI_HPTR); /* reset target mode software */
#if SCSI_RESOURCE_MANAGEMENT
   SCSI_UEXACT16 (*SCSIrGetScb)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* get scb */
   void (*SCSIrReturnScb)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* return scb */
   void (*SCSIrRemoveScbs)(SCSI_HHCB SCSI_HPTR); /* remove scb */
#if SCSI_TARGET_OPERATION
   SCSI_UEXACT16 (*SCSIrGetEstScb)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* get est scb */  
   void (*SCSIrReturnEstScb)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* return est scb */ 
#endif /* SCSI_TARGET_OPERATION */
#endif /* SCSI_RESOURCE_MANAGEMENT */
#if SCSI_TARGET_OPERATION 
   void (*SCSIhTargetSetIgnoreWideMsg)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   void (*SCSIhTargetSendHiobSpecial)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);    /* send hiob special */
   void (*SCSIhTargetGetEstScbFields)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8); /* get est scb fields */
   void (*SCSIhTargetDeliverEstScb)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* deliver est scb */
   void (*SCSIhTargetSetFirmwareProfile)(SCSI_HHCB SCSI_HPTR); /* set firmware profile for target mode */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
   SCSI_UEXACT8 (*SCSIhTargetGetSelectionInType)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   void (*SCSIhTargetIUSelectionIn)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   void (*SCSIhTargetBuildStatusIU)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   SCSI_UINT8 (*SCSIhTargetAssignErrorScbDescriptor)(SCSI_HHCB SCSI_HPTR);
   void (*SCSIhTargetInitErrorHIOB)(SCSI_HHCB SCSI_HPTR);  /* Initialize Error HIOB    */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */   
} SCSI_FIRMWARE_DESCRIPTOR;

/***************************************************************************
*  SCSI_FIRMWARE_DESCRIPTOR for Standard Ultra 320 mode 
***************************************************************************/
#if (SCSI_STANDARD_U320_MODE && defined(SCSI_DATA_DEFINE))
SCSI_UEXACT8 SCSIhStandardU320SetupSequencer(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardDeliverScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320SetupAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320TargetClearBusy(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320RequestSense(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320ResetBTA(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardGetConfig(SCSI_HHCB SCSI_HPTR);
#if SCSI_SCBBFR_BUILTIN
void SCSIhSetupAssignScbBuffer(SCSI_HHCB SCSI_HPTR);
void SCSIhAssignScbDescriptor(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhFreeScbDescriptor(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_SCBBFR_BUILTIN */
SCSI_UEXACT16 SCSIrGetScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrReturnScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrStandardRemoveScbs(SCSI_HHCB SCSI_HPTR);
void SCSIhXferRateAssign(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhGetNegoXferRate(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhStandardAbortChannel(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
#if !SCSI_ASPI_SUPPORT
#if !SCSI_ASPI_SUPPORT_GROUP1
void SCSIhStandardAbortHIOB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
void SCSIhStandardU320ActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhStandardU320NonPackActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#if SCSI_PACKETIZED_IO_SUPPORT
void SCSIhStandardU320PackActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
void SCSIhStandardRemoveActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);

#endif /* !SCSI_ASPI_SUPPORT */
#if !SCSI_ASPI_SUPPORT_GROUP1
void SCSIhStandardU320UpdateAbortBitHostMem(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); 
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
void SCSIhStandardU320UpdateNextScbAddress(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_BUS_ADDRESS);
#if !SCSI_ASPI_SUPPORT_GROUP1
void SCSIhStandardU320SetupTR(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
SCSI_UEXACT32 SCSIhStandardU320ResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320IgnoreWideResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhEvenIOLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhStandardU320Underrun(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardSetBreakPoint(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhStandardClearBreakPoint(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
#if SCSI_PACKETIZED_IO_SUPPORT
void SCSIhStandardPackNonPackQueueHIOB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
void SCSIhStandardU320SetIntrFactorThreshold(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhStandardU320SetIntrThresholdCount(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhStandardU320UpdateExeQAtnLength(SCSI_HHCB SCSI_HPTR, SCSI_UEXACT8);
void SCSIhStandardUpdateNewQAtnLength(SCSI_HHCB SCSI_HPTR, SCSI_UEXACT8);
void SCSIhStandardU320QHeadPNPSwitchSCB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#if (OSD_BUS_ADDRESS_SIZE == 64)
SCSI_UINT8 SCSIhCompareBusSGListAddress(SCSI_HHCB SCSI_HPTR,SCSI_BUS_ADDRESS,SCSI_BUS_ADDRESS); 
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */
SCSI_UEXACT8 SCSIhStandardU320QExeTargNextOffset(void);
SCSI_UEXACT8 SCSIhStandardU320QNextOffset(void);
#if SCSI_TARGET_OPERATION
#if SCSI_RESOURCE_MANAGEMENT
SCSI_UEXACT16 SCSIrGetEstScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrReturnEstScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_RESOURCE_MANAGEMENT */ 
void SCSIhTargetStandardU320SetIgnoreWideMsg(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardSendHiobSpecial(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardU320GetEstScbFields(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhTargetStandardU320DeliverEstScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetStandardU320SetFirmwareProfile(SCSI_HHCB SCSI_HPTR);
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
SCSI_UEXACT8 SCSIhTargetStandardU320SelInType(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UINT8 SCSIhTargetStandardU320AssignErrorScbDescriptor(SCSI_HHCB SCSI_HPTR); 
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */

SCSI_FIRMWARE_DESCRIPTOR SCSIFirmwareStandardU320 = 
{
   SCSI_SU320_FIRMWARE_VERSION,        /* firmware version              */
   SCSI_SU320_SIZE_SCB,                /* SCB size                      */
   SCSI_SU320_SIOSTR3_ENTRY,           /* siostr3_entry                 */
   SCSI_SU320_RETRY_IDENTIFY_ENTRY,    /* retry identify message        */
   SCSI_SU320_START_LINK_CMD_ENTRY,    /* start_link_cmd_entry          */
   SCSI_SU320_IDLE_LOOP_ENTRY,         /* idle_loop_entry               */
   SCSI_SU320_IDLE_LOOP_TOP,           /* idle_loop_top                 */
   SCSI_SU320_IDLE_LOOP0,              /* idle_loop0                    */
   SCSI_SU320_SIO204_ENTRY,            /* sio204_entry                  */
   SCSI_SU320_EXPANDER_BREAK,          /* expander_break                */
   SCSI_SU320_ISR,                     /* sequencer isr address         */
#if SCSI_TARGET_OPERATION
   SCSI_SU320_TARGET_DATA_ENTRY,       /* target data entry             */
   SCSI_SU320_TARGET_HELD_BUS_ENTRY,   /* target held scsi bus entry    */
#endif /* SCSI_TARGET_OPERATION */
   SCSI_SU320_BTATABLE,                /* busy target array table       */
   SCSI_SU320_BTASIZE,                 /* busy target array size        */
#if !SCSI_SEQ_LOAD_USING_PIO
   sizeof(Seqload),                    /* Loader code size              */
   Seqload,                            /* Loader Code                   */
   SCSI_SU320_LOAD_ADDR,               /* address of seq buffer         */
   SCSI_SU320_LOAD_COUNT,              /* number of bytes to be DMAed   */
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
   SCSI_SU320_PASS_TO_DRIVER,          /* pass_to_driver                */
   SCSI_SU320_ACTIVE_SCB,              /* active scb                    */
   SCSI_SU320_RET_ADDR,                /* return address                */
   SCSI_SU320_Q_NEW_POINTER,           /* pointer to new SCB            */
   SCSI_SU320_ENT_PT_BITMAP,           /* bitmap for various brk points */
   SCSI_SU320_SG_STATUS,               /* S/G status                    */
   SCSI_SU320_FIRST_SCB,               /* first_scb scratch RAM location*/  
   SCSI_SU320_DV_TIMERLO,              /* dv_timerlo scratch RAM        */
                                       /* location                      */
   SCSI_SU320_DV_TIMERHI,              /* dv_timerhi scratch RAM        */
                                       /* location                      */
#if SCSI_PACKETIZED_IO_SUPPORT
   SCSI_SU320_SLUN_LENGTH,             /* value for LUNLEN register     */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_TARGET_OPERATION
   SCSI_SU320_REG0,                    /* reg0 scratch location         */
   SCSI_SU320_Q_EST_HEAD,              /* establish scb queue head offset */
   SCSI_SU320_Q_EST_TAIL,              /* establish scb queue tail offset */
   SCSI_SU320_Q_EXE_TARG_HEAD,         /* target mode execution queue   */
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_RESOURCE_MANAGEMENT
   SCSI_SU320_MAX_NONTAG_SCBS,         /* Max Non Tag SCBs              */
   {
      0,0,0                            /* reserved for future use       */
   },
#endif /* SCSI_RESOURCE_MANAGEMENT */
   SCSI_SU320_ARPINTVALID_REG,
   SCSI_SU320_ARPINTVALID_BIT,
   0,
   SCSIhStandardU320SetupSequencer,    /* setup sequencer               */
   SCSIhStandardU320ResetSoftware,     /* reset software                */
   SCSIhStandardDeliverScb,            /* deliver scb                   */
   SCSIhStandardU320SetupAtnLength,    /* setup atn_length              */
   SCSIhStandardU320TargetClearBusy,   /* clear busy target             */
   SCSIhStandardU320RequestSense,      /* request sense setup           */
   SCSIhStandardU320ResetBTA,          /* reste busy target array       */
   SCSIhStandardGetConfig,             /* get driver config info        */
#if SCSI_SCBBFR_BUILTIN
   SCSIhSetupAssignScbBuffer,          /* set assign scb buffer         */
   SCSIhAssignScbDescriptor,           /* assign scb buffer descriptor  */
   SCSIhFreeScbDescriptor,             /* free scb buffer descriptor    */
#endif /* SCSI_SCBBFR_BUILTIN */
   SCSIhXferRateAssign,                /* assign xfer rate              */
   SCSIhGetNegoXferRate,               /* get negotiation xfer rate     */
   SCSIhStandardAbortChannel,          /* aborting HIOBs for a channel  */
#if !SCSI_ASPI_SUPPORT
#if !SCSI_ASPI_SUPPORT_GROUP1
   SCSIhStandardAbortHIOB,             /* aborting HIOB in queues       */
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
   SCSIhStandardU320ActiveAbort,       /* aborting active HIOB          */
   SCSIhStandardU320NonPackActiveAbort,/* non-packetized active abort   */ 
#if SCSI_PACKETIZED_IO_SUPPORT
   SCSIhStandardU320PackActiveAbort,   /* packetized active abort       */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
   SCSIhStandardRemoveActiveAbort,     /* remove active abort           */
#endif /* !SCSI_ASPI_SUPPORT */
#if !SCSI_ASPI_SUPPORT_GROUP1
   SCSIhStandardU320UpdateAbortBitHostMem, /* update abort bit in host mem */
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
   SCSIhStandardU320UpdateNextScbAddress, /* update next SCB address in SCB */
#if !SCSI_ASPI_SUPPORT_GROUP1 
   SCSIhStandardU320SetupTR,           /* setup Target Reset scb        */
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
   SCSIhStandardU320ResidueCalc,       /* calculate residual length     */
   SCSIhStandardU320IgnoreWideResidueCalc, /* update residual length    */
   SCSIhEvenIOLength,                  /* determine wide residue        */
   SCSIhStandardU320Underrun,          /* sequencer reported underrun/overrun */   
   SCSIhStandardSetBreakPoint,         /* set breakpoint                */
   SCSIhStandardClearBreakPoint,       /* clear breakpoint              */
#if SCSI_PACKETIZED_IO_SUPPORT
   SCSIhStandardPackNonPackQueueHIOB,  /* potential pack to non pack    */
                                       /* switch detected when target   */
                                       /* still has packetized          */
                                       /* commands outstanding.         */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
   SCSIhStandardU320SetIntrFactorThreshold,/* set intr factor threshold value*/
   SCSIhStandardU320SetIntrThresholdCount, /* set intr threshold count value*/
   SCSIhStandardU320UpdateExeQAtnLength,/* update atn_length in Execution Queue*/
   SCSIhStandardUpdateNewQAtnLength,   /* update atn_length in New Queue*/
   SCSIhStandardU320QHeadPNPSwitchSCB, /* queue PNP switched SCB to head of Execution Queue */
#if (OSD_BUS_ADDRESS_SIZE == 64)
   SCSIhCompareBusSGListAddress,       /* compare bus address           */  
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */
   0,                                  /* set disconnect delay value    */
   SCSIhStandardU320QExeTargNextOffset,/* offset of q_exe_targ_next     */
   SCSIhStandardU320QNextOffset,       /* offset if q_next              */ 
#if SCSI_TARGET_OPERATION
   SCSIhTargetStandardU320ResetSoftware /* reset target mode software   */
#else
   0                                   /* reset target mode software    */
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_RESOURCE_MANAGEMENT
   ,
   SCSIrGetScb,                        /* get scb                       */
   SCSIrReturnScb,                     /* return scb                    */
   SCSIrStandardRemoveScbs             /* remove scbs                   */
#if SCSI_TARGET_OPERATION
   ,
   SCSIrGetEstScb,                     /* get est scb                   */              
   SCSIrReturnEstScb                   /* return est scb                */
#endif /* SCSI_TARGET_OPERATION */
#endif /* SCSI_RESOURCE_MANAGEMENT */
#if SCSI_TARGET_OPERATION              
   , 
   SCSIhTargetStandardU320SetIgnoreWideMsg,/* set ignore wide residue   */ 
   SCSIhTargetStandardSendHiobSpecial, /* send hiob special             */
   SCSIhTargetStandardU320GetEstScbFields,/* get est scb fields         */
   SCSIhTargetStandardU320DeliverEstScb,  /* deliver est scb            */
   SCSIhTargetStandardU320SetFirmwareProfile /* set firmware profile    */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
   ,
   SCSIhTargetStandardU320SelInType,   /* selection in type             */
   0,                                  /* IU selection in handling      */
   0,                                  /* Build Status IU               */
   SCSIhTargetStandardU320AssignErrorScbDescriptor, /* Assign error SCB descriptor */
   0                                   /* Initialize Error HIOB    */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */
};
#endif /* (SCSI_STANDARD_U320_MODE && defined(SCSI_DATA_DEFINE)) */

/***************************************************************************
*  SCSI_FIRMWARE_DESCRIPTOR for Standard Enhanced Ultra 320 mode 
***************************************************************************/

#if (SCSI_STANDARD_ENHANCED_U320_MODE && defined(SCSI_DATA_DEFINE))
SCSI_UEXACT8 SCSIhStandardEnhU320SetupSequencer(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardDeliverScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardEnhU320SetupAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320TargetClearBusy(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardEnhU320RequestSense(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardU320ResetBTA(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardGetConfig(SCSI_HHCB SCSI_HPTR);
#if SCSI_SCBBFR_BUILTIN
void SCSIhSetupAssignScbBuffer(SCSI_HHCB SCSI_HPTR);
void SCSIhAssignScbDescriptor(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhFreeScbDescriptor(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_SCBBFR_BUILTIN */
SCSI_UEXACT16 SCSIrGetScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrReturnScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_HIOB SCSI_IPTR SCSIrHostQueueRemove(SCSI_HHCB SCSI_HPTR);
void SCSIrStandardEnhU320RemoveScbs(SCSI_HHCB SCSI_HPTR);
void SCSIhXferRateAssign(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhGetNegoXferRate(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhStandardAbortChannel(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);

#if !SCSI_ASPI_SUPPORT
#if !SCSI_ASPI_SUPPORT_GROUP1
void SCSIhStandardAbortHIOB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
void SCSIhStandardEnhU320ActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhStandardEnhU320NonPackActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#if SCSI_PACKETIZED_IO_SUPPORT
void SCSIhStandardEnhU320PackActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
void SCSIhStandardRemoveActiveAbort(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* !SCSI_ASPI_SUPPORT */
#if !SCSI_ASPI_SUPPORT_GROUP1
void SCSIhStandardEnhU320UpdateAbortBitHostMem(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); 
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
void SCSIhStandardEnhU320UpdateNextScbAddress(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_BUS_ADDRESS);
#if !SCSI_ASPI_SUPPORT_GROUP1
void SCSIhStandardEnhU320SetupTR(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
SCSI_UEXACT32 SCSIhStandardEnhU320ResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhStandardEnhU320IgnoreWideResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhEvenIOLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhStandardEnhU320Underrun(SCSI_HHCB SCSI_HPTR);
void SCSIhStandardSetBreakPoint(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhStandardClearBreakPoint(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
#if SCSI_PACKETIZED_IO_SUPPORT
void SCSIhStandardPackNonPackQueueHIOB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
void SCSIhStandardU320SetIntrFactorThreshold(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhStandardU320SetIntrThresholdCount(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhStandardEnhU320UpdateExeQAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhStandardUpdateNewQAtnLength(SCSI_HHCB SCSI_HPTR, SCSI_UEXACT8);
void SCSIhStandardEnhU320QHeadPNPSwitchSCB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#if (OSD_BUS_ADDRESS_SIZE == 64)
SCSI_UINT8 SCSIhCompareBusSGListAddress(SCSI_HHCB SCSI_HPTR,SCSI_BUS_ADDRESS,SCSI_BUS_ADDRESS); 
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */
SCSI_UEXACT8 SCSIhStandardEnhU320QExeTargNextOffset(void);
SCSI_UEXACT8 SCSIhStandardEnhU320QNextOffset(void);
#if SCSI_TARGET_OPERATION
#if SCSI_RESOURCE_MANAGEMENT
SCSI_UEXACT16 SCSIrGetEstScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrReturnEstScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_RESOURCE_MANAGEMENT */ 
void SCSIhTargetStandardEnhU320SetIgnoreWideMsg(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardSendHiobSpecial(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardEnhU320GetEstScbFields(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIhTargetStandardU320DeliverEstScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardEnhU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetStandardEnhU320SetFirmwareProfile(SCSI_HHCB SCSI_HPTR);
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
SCSI_UEXACT8 SCSIhTargetStandardEnhU320SelInType(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardEnhU320IUSelIn(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhTargetStandardEnhU320BuildStatusIU(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UINT8 SCSIhTargetStandardEnhU320AssignErrorScbDescriptor(SCSI_HHCB SCSI_HPTR);
void SCSIhTargetStandardEnhU320InitErrorHIOB(SCSI_HHCB SCSI_HPTR);
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */

SCSI_FIRMWARE_DESCRIPTOR SCSIFirmwareStandardEnhancedU320 = 
{
   SCSI_SU320_FIRMWARE_VERSION,        /* firmware version              */
   SCSI_SU320_SIZE_SCB,                /* SCB size                      */
   SCSI_SU320_SIOSTR3_ENTRY,           /* siostr3_entry                 */
   SCSI_SU320_RETRY_IDENTIFY_ENTRY,    /* retry identify message        */
   SCSI_SU320_START_LINK_CMD_ENTRY,    /* start_link_cmd_entry          */
   SCSI_SU320_IDLE_LOOP_ENTRY,         /* idle_loop_entry               */
   SCSI_SU320_IDLE_LOOP_TOP,           /* idle_loop_top                 */
   SCSI_SEU320_IDLE_LOOP0,             /* idle_loop0                    */
   SCSI_SU320_SIO204_ENTRY,            /* sio204_entry                  */
   SCSI_SEU320_EXPANDER_BREAK,         /* expander_break                */
   SCSI_SU320_ISR,                     /* sequencer isr address         */
#if SCSI_TARGET_OPERATION
   SCSI_SU320_TARGET_DATA_ENTRY,       /* target data entry             */
   SCSI_SU320_TARGET_HELD_BUS_ENTRY,   /* target held scsi bus entry    */
#endif /* SCSI_TARGET_OPERATION */
   SCSI_SU320_BTATABLE,                /* busy target array table       */
   SCSI_SU320_BTASIZE,                 /* busy target array size        */
#if !SCSI_SEQ_LOAD_USING_PIO
   sizeof(Seqeload),                   /* Loader code size              */
   Seqeload,                           /* Loader Code                   */
   SCSI_SU320_LOAD_ADDR,               /* address of seq buffer         */
   SCSI_SU320_LOAD_COUNT,              /* number of bytes to be DMAed   */
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
   SCSI_SU320_PASS_TO_DRIVER,          /* pass_to_driver                */
   SCSI_SU320_ACTIVE_SCB,              /* active scb                    */
   SCSI_SU320_RET_ADDR,                /* return address                */
   SCSI_SU320_Q_NEW_POINTER,           /* pointer to new SCB            */
   SCSI_SU320_ENT_PT_BITMAP,           /* bitmap for various brk points */
   SCSI_SU320_SG_STATUS,               /* S/G status                    */
   SCSI_SEU320_FIRST_SCB,              /* first_scb scratch RAM location*/  
   SCSI_SEU320_DV_TIMERLO,             /* dv_timerlo scratch RAM        */
                                       /* location                      */
   SCSI_SEU320_DV_TIMERHI,             /* dv_timerhi scratch RAM        */
                                       /* location                      */
#if SCSI_PACKETIZED_IO_SUPPORT
   SCSI_SEU320_SLUN_LENGTH,            /* value for LUNLEN register     */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_TARGET_OPERATION
   SCSI_SEU320_REG0,                   /* reg0 scratch location         */
   SCSI_SEU320_Q_EST_HEAD,             /* establish scb queue head offset */
   SCSI_SEU320_Q_EST_TAIL,             /* establish scb queue tail offset */
   SCSI_SEU320_Q_EXE_TARG_HEAD,        /* target mode execution queue   */
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_RESOURCE_MANAGEMENT
   SCSI_SU320_MAX_NONTAG_SCBS,         /* Max Non Tag SCBs              */
   {
      0,0,0                            /* reserved for future use       */
   },
#endif /* SCSI_RESOURCE_MANAGEMENT */
   SCSI_SEU320_ARPINTVALID_REG,
   SCSI_SEU320_ARPINTVALID_BIT,
   0,
   SCSIhStandardEnhU320SetupSequencer, /* setup sequencer               */
   SCSIhStandardU320ResetSoftware,     /* reset software                */
   SCSIhStandardDeliverScb,            /* deliver scb                   */
   SCSIhStandardEnhU320SetupAtnLength, /* setup atn_length              */
   SCSIhStandardU320TargetClearBusy,   /* clear busy target             */
   SCSIhStandardEnhU320RequestSense,   /* request sense setup           */
   SCSIhStandardU320ResetBTA,          /* reste busy target array       */
   SCSIhStandardGetConfig,             /* get driver config info        */
#if SCSI_SCBBFR_BUILTIN
   SCSIhSetupAssignScbBuffer,          /* set assign scb buffer         */
   SCSIhAssignScbDescriptor,           /* assign scb buffer descriptor  */
   SCSIhFreeScbDescriptor,             /* free scb buffer descriptor    */
#endif /* SCSI_SCBBFR_BUILTIN */
   SCSIhXferRateAssign,                /* assign xfer rate              */
   SCSIhGetNegoXferRate,               /* get negotiation xfer rate     */
   SCSIhStandardAbortChannel,          /* aborting HIOBs for a channel  */
   
#if !SCSI_ASPI_SUPPORT
#if !SCSI_ASPI_SUPPORT_GROUP1
   SCSIhStandardAbortHIOB,             /* aborting HIOB in queues       */
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
   SCSIhStandardEnhU320ActiveAbort,    /* aborting active HIOB          */
   SCSIhStandardEnhU320NonPackActiveAbort,/* non-packetized active abort */
#if SCSI_PACKETIZED_IO_SUPPORT
   SCSIhStandardEnhU320PackActiveAbort,/* packetized active abort       */   
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
   SCSIhStandardRemoveActiveAbort,     /* remove active abort           */
#endif /* !SCSI_ASPI_SUPPORT */
#if !SCSI_ASPI_SUPPORT_GROUP1
   SCSIhStandardEnhU320UpdateAbortBitHostMem, /* update abort bit in host mem */
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
   SCSIhStandardEnhU320UpdateNextScbAddress, /* update next SCB address in SCB */
#if !SCSI_ASPI_SUPPORT_GROUP1 
   SCSIhStandardEnhU320SetupTR,        /* setup Target Reset scb        */
#endif /* !SCSI_ASPI_SUPPORT_GROUP1 */
   SCSIhStandardEnhU320ResidueCalc,       /* calculate residual length  */
   SCSIhStandardEnhU320IgnoreWideResidueCalc, /* update residual length */
   SCSIhEvenIOLength,                  /* determine wide residue        */
   SCSIhStandardEnhU320Underrun,       /* sequencer reported underrun/overrun */
   SCSIhStandardSetBreakPoint,         /* set breakpoint                */
   SCSIhStandardClearBreakPoint,       /* clear breakpoint              */
#if SCSI_PACKETIZED_IO_SUPPORT
   SCSIhStandardPackNonPackQueueHIOB, /* potential pack to non pack     */
                                      /* switch detected when target    */
                                      /* still has packetized           */
                                      /* commands outstanding.          */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
   SCSIhStandardU320SetIntrFactorThreshold,/* set intr factor threshold value*/
   SCSIhStandardU320SetIntrThresholdCount, /* set intr threshold count value*/
   SCSIhStandardEnhU320UpdateExeQAtnLength,/* update atn_length in Execution Queue*/
   SCSIhStandardUpdateNewQAtnLength,   /* update atn_length in New Queue*/
   SCSIhStandardEnhU320QHeadPNPSwitchSCB, /* queue PNP switched SCB to head
                                           * of Execution Queue
                                           */
#if (OSD_BUS_ADDRESS_SIZE == 64)
   SCSIhCompareBusSGListAddress,       /* compare bus address           */
#endif /* (OSD_BUS_ADDRESS_SIZE == 64) */
   0,                                  /* set disconnect delay value    */
   SCSIhStandardEnhU320QExeTargNextOffset,/* offset of q_exe_targ_next  */
   SCSIhStandardEnhU320QNextOffset,    /* offset of q_next              */
#if SCSI_TARGET_OPERATION
   SCSIhTargetStandardEnhU320ResetSoftware /* reset target mode software */
#else
   0                                   /* reset target mode software    */
#endif /* SCSI_TARGET_OPERATION */
#if SCSI_RESOURCE_MANAGEMENT
   ,
   SCSIrGetScb,                        /* get scb                       */
   SCSIrReturnScb,                     /* return scb                    */
   SCSIrStandardEnhU320RemoveScbs      /* remove scbs                   */
#if SCSI_TARGET_OPERATION
   ,
   SCSIrGetEstScb,                     /* get est scb                   */              
   SCSIrReturnEstScb                   /* return est scb                */
#endif /* SCSI_TARGET_OPERATION */
#endif /* SCSI_RESOURCE_MANAGEMENT */
#if SCSI_TARGET_OPERATION              
   , 
   SCSIhTargetStandardEnhU320SetIgnoreWideMsg,/* set ignore wide residue */ 
   SCSIhTargetStandardSendHiobSpecial, /* send hiob special             */
   SCSIhTargetStandardEnhU320GetEstScbFields,/* get est scb fields      */
   SCSIhTargetStandardU320DeliverEstScb,  /* deliver est scb            */
   SCSIhTargetStandardEnhU320SetFirmwareProfile /* set firmware profile */
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
   ,
   SCSIhTargetStandardEnhU320SelInType, /* selection in type            */
   SCSIhTargetStandardEnhU320IUSelIn,   /* IU selection in handling     */
   SCSIhTargetStandardEnhU320BuildStatusIU, /* build Status IU          */ 
   SCSIhTargetStandardEnhU320AssignErrorScbDescriptor, /* Assign error SCB desc */
   SCSIhTargetStandardEnhU320InitErrorHIOB  /* Initialize Error HIOB    */
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */
};
#endif /* (SCSI_STANDARD_ENHANCED_U320_MODE && defined(SCSI_DATA_DEFINE)) */
#endif /* SCSI_MULTI_MODE */

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
*  All Firmware Descriptor defintions should be referenced only if
*  SCSI_MULTI_DOWNSHIFT_MODE is enabled
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/
#if SCSI_MULTI_DOWNSHIFT_MODE
/***************************************************************************
* FIRMWARE_DESCRIPTOR definition
***************************************************************************/
typedef struct SCSI_FIRMWARE_DESCRIPTOR_
{
   SCSI_INT firmwareVersion;              /* firmware version              */
   SCSI_INT ScbSize;                      /* SCB size                      */
   SCSI_UINT16  Siostr3Entry;             /* siostr3 entry                 */
   SCSI_UINT16  RetryIdentifyEntry;       /* retry identify entry          */
   SCSI_UINT16  StartLinkCmdEntry;        /* start link command entry addr */
   SCSI_UINT16  IdleLoopEntry;            /* idle loop entry               */
   SCSI_UINT16  IdleLoopTop;              /* idle loop top                 */
   SCSI_UINT16  IdleLoop0;                /* idle loop0                    */
   SCSI_UINT16  Sio204Entry;              /* sio 204 entry                 */
   SCSI_UINT16  ExpanderBreak;            /* bus expander break address    */
   SCSI_UEXACT16 BtaTable;                /* busy target array             */
   SCSI_UEXACT16 BtaSize;                 /* busy target array size        */
#if !SCSI_SEQ_LOAD_USING_PIO
   SCSI_UEXACT16 LoadSize;                /* Loader code size              */
   SCSI_UEXACT8 SCSI_LPTR LoadCode;       /* Loader Code                   */
   SCSI_UEXACT16 LoadAddr;                /* address of seq buffer         */
   SCSI_UEXACT16 LoadCount;               /* number of bytes to be DMAed   */
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
   SCSI_UEXACT16 PassToDriver;            /* pass to driver from sequencer */
   SCSI_UEXACT16 ActiveScb;               /* active scb                    */
   SCSI_UEXACT16 RetAddr;                 /* return address                */
   SCSI_UEXACT16 QNewPointer;             /* pointer to new SCB            */
   SCSI_UEXACT16 SgStatus;                /* S/G status                    */
   SCSI_UEXACT16 ArpintValidReg;
   SCSI_UEXACT8 ArpintValidBit; 
   SCSI_UEXACT8 reserved3;
   SCSI_UEXACT8 (*SCSIhSetupSequencer)(SCSI_HHCB SCSI_HPTR); /* set driver sequencer   */
   void (*SCSIhResetSoftware)(SCSI_HHCB SCSI_HPTR); /* reset software         */
   void (*SCSIhDeliverScb)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* deliver scb */
   void (*SCSIhSetupAtnLength)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* setup atn_length */
   void (*SCSIhTargetClearBusy)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* target clear busy */
   void (*SCSIhRequestSense)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR); /* request sense setup */
   void (*SCSIhResetBTA)(SCSI_HHCB SCSI_HPTR);     /* reset busy target array */
   void (*SCSIhGetConfig)(SCSI_HHCB SCSI_HPTR);     /* get configuration */
#if SCSI_SCBBFR_BUILTIN
   void (*SCSIhSetupAssignScbBuffer)(SCSI_HHCB SCSI_HPTR);
   void (*SCSIhAssignScbDescriptor)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   void (*SCSIhFreeScbDescriptor)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_SCBBFR_BUILTIN */
   void (*SCSIhXferRateAssign)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
   void (*SCSIhGetNegoXferRate)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
   void (*SCSIhAbortChannel)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
   SCSI_UEXACT32 (*SCSIhResidueCalc)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   void (*SCSIhIgnoreWideResidueCalc)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
   SCSI_UEXACT8 (*SCSIhEvenIOLength)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   SCSI_UEXACT8 (*SCSIhUnderrun) (SCSI_HHCB SCSI_HPTR);
   SCSI_UEXACT8 (*SCSIhUpdateExeQAtnLength)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
   void (*SCSIhUpdateNewQAtnLength)(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
   void (*SCSIhQHeadPNPSwitchSCB)(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
   void (*SCSIhSetDisconnectDelay)(SCSI_HHCB SCSI_HPTR);
} SCSI_FIRMWARE_DESCRIPTOR;

/***************************************************************************
*  SCSI_FIRMWARE_DESCRIPTOR for Downshift Ultra 320 mode 
***************************************************************************/
#if (SCSI_DOWNSHIFT_U320_MODE && defined(SCSI_DATA_DEFINE))
SCSI_UEXACT8 SCSIhDownshiftU320SetupSequencer(SCSI_HHCB SCSI_HPTR);
void SCSIhDownshiftU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhDownshiftDeliverScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320SetupAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320TargetClearBusy(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320RequestSense(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320ResetBTA(SCSI_HHCB SCSI_HPTR);
void SCSIhDownshiftGetConfig(SCSI_HHCB SCSI_HPTR);
#if SCSI_SCBBFR_BUILTIN
void SCSIhSetupAssignScbBuffer(SCSI_HHCB SCSI_HPTR);
void SCSIhAssignScbDescriptor(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhFreeScbDescriptor(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_SCBBFR_BUILTIN */
void SCSIhXferRateAssign(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhGetNegoXferRate(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhDownshiftAbortChannel(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT32 SCSIhDownshiftU320ResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320IgnoreWideResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhEvenIOLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhDownshiftU320Underrun(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhDownshiftU320UpdateExeQAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhDownshiftUpdateNewQAtnLength(SCSI_HHCB SCSI_HPTR, SCSI_UEXACT8);
void SCSIhDownshiftU320QHeadPNPSwitchSCB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);

SCSI_FIRMWARE_DESCRIPTOR SCSIFirmwareDownshiftU320 = 
{
   SCSI_DU320_FIRMWARE_VERSION,        /* firmware version              */
   SCSI_DU320_SIZE_SCB,                /* SCB size                      */
   SCSI_DU320_SIOSTR3_ENTRY,           /* siostr3_entry                 */
   SCSI_DU320_RETRY_IDENTIFY_ENTRY,    /* retry identify message        */
   SCSI_DU320_START_LINK_CMD_ENTRY,    /* start_link_cmd_entry          */
   SCSI_DU320_IDLE_LOOP_ENTRY,         /* idle_loop_entry               */
   SCSI_DU320_IDLE_LOOP_TOP,           /* idle_loop_top                 */
   SCSI_DU320_IDLE_LOOP0,             /* idle_loop0                    */
   SCSI_DU320_SIO204_ENTRY,            /* sio204_entry                  */
   SCSI_DU320_EXPANDER_BREAK,         /* expander_break                */
   SCSI_DU320_BTATABLE,                /* busy target array table       */
   SCSI_DU320_BTASIZE,                 /* busy target array size        */
#if !SCSI_SEQ_LOAD_USING_PIO
   sizeof(Seqload),                   /* Loader code size              */
   Seqload,                           /* Loader Code                   */
   SCSI_DU320_LOAD_ADDR,               /* address of seq buffer         */
   SCSI_DU320_LOAD_COUNT,              /* number of bytes to be DMAed   */
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
   SCSI_DU320_PASS_TO_DRIVER,          /* pass_to_driver                */
   SCSI_DU320_ACTIVE_SCB,              /* active scb                    */
   SCSI_DU320_RET_ADDR,                /* return address                */
   SCSI_DU320_Q_NEW_POINTER,           /* pointer to new SCB            */
   SCSI_DU320_SG_STATUS,               /* S/G status                    */
   SCSI_DU320_ARPINTVALID_REG,
   SCSI_DU320_ARPINTVALID_BIT,
   0,
   SCSIhDownshiftU320SetupSequencer, /* setup sequencer               */
   SCSIhDownshiftU320ResetSoftware,     /* reset software                */
   SCSIhDownshiftDeliverScb,            /* deliver scb                   */
   SCSIhDownshiftU320SetupAtnLength, /* setup atn_length              */
   SCSIhDownshiftU320TargetClearBusy,   /* clear busy target             */
   SCSIhDownshiftU320RequestSense,   /* request sense setup           */
   SCSIhDownshiftU320ResetBTA,          /* reste busy target array       */
   SCSIhDownshiftGetConfig,             /* get driver config info        */
#if (!SCSI_BIOS_SUPPORT && SCSI_SCBBFR_BUILTIN)
   SCSIhSetupAssignScbBuffer,          /* set assign scb buffer         */
   SCSIhAssignScbDescriptor,           /* assign scb buffer descriptor  */
   SCSIhFreeScbDescriptor,             /* free scb buffer descriptor    */
#else
   0,
   0,
   0,
#endif /* (!SCSI_BIOS_SUPPORT &  SCSI_SCBBFR_BUILTIN) */
   SCSIhXferRateAssign,                /* assign xfer rate              */
   SCSIhGetNegoXferRate,               /* get negotiation xfer rate     */
   SCSIhDownshiftAbortChannel,
   SCSIhDownshiftU320ResidueCalc,       /* calculate residual length  */
   SCSIhDownshiftU320IgnoreWideResidueCalc, /* update residual length */
   SCSIhEvenIOLength,                   /* determine wide residue        */
   SCSIhDownshiftU320Underrun,       /* sequencer reported underrun/overrun */
   
#if SCSI_BIOS_SUPPORT
   0,
   0,
#else
   SCSIhDownshiftU320UpdateExeQAtnLength,/* update atn_length in Execution Queue*/
   SCSIhDownshiftUpdateNewQAtnLength,   /* update atn_length in New Queue*/
#endif /* SCSI_BIOS_SUPPORT */
   SCSIhDownshiftU320QHeadPNPSwitchSCB, /* queue PNP switched SCB to head */
   0,                                  /* set disconnect delay value    */
};
#endif /* (SCSI_DOWNSHIFT_U320_MODE && defined(SCSI_DATA_DEFINE)) */


/***************************************************************************
*  SCSI_FIRMWARE_DESCRIPTOR for Downshift Enhanced Ultra 320 mode 
***************************************************************************/
#if (SCSI_DOWNSHIFT_ENHANCED_U320_MODE && defined(SCSI_DATA_DEFINE))

SCSI_UEXACT8 SCSIhDownshiftU320SetupSequencer(SCSI_HHCB SCSI_HPTR);
void SCSIhDownshiftU320ResetSoftware(SCSI_HHCB SCSI_HPTR);
void SCSIhDownshiftDeliverScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320SetupAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320TargetClearBusy(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftEnhU320RequestSense(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320ResetBTA(SCSI_HHCB SCSI_HPTR);
void SCSIhDownshiftGetConfig(SCSI_HHCB SCSI_HPTR);
#if SCSI_SCBBFR_BUILTIN
void SCSIhSetupAssignScbBuffer(SCSI_HHCB SCSI_HPTR);
void SCSIhAssignScbDescriptor(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhFreeScbDescriptor(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
#endif /* SCSI_SCBBFR_BUILTIN */
void SCSIhXferRateAssign(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhGetNegoXferRate(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT8 SCSI_LPTR);
void SCSIhDownshiftAbortChannel(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT32 SCSIhDownshiftU320ResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIhDownshiftU320IgnoreWideResidueCalc(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIhEvenIOLength(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT8 SCSIhDownshiftEnhU320Underrun(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIhDownshiftEnhU320UpdateExeQAtnLength(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIhDownshiftUpdateNewQAtnLength(SCSI_HHCB SCSI_HPTR, SCSI_UEXACT8);
void SCSIhDownshiftEnhU320QHeadPNPSwitchSCB(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);

SCSI_FIRMWARE_DESCRIPTOR SCSIFirmwareDownshiftEnhancedU320 = 
{
   SCSI_DU320_FIRMWARE_VERSION,        /* firmware version              */
   SCSI_DU320_SIZE_SCB,                /* SCB size                      */
   SCSI_DU320_SIOSTR3_ENTRY,           /* siostr3_entry                 */
   SCSI_DU320_RETRY_IDENTIFY_ENTRY,    /* retry identify message        */
   SCSI_DU320_START_LINK_CMD_ENTRY,    /* start_link_cmd_entry          */
   SCSI_DU320_IDLE_LOOP_ENTRY,         /* idle_loop_entry               */
   SCSI_DU320_IDLE_LOOP_TOP,           /* idle_loop_top                 */
   SCSI_DU320_IDLE_LOOP0,             /* idle_loop0                    */
   SCSI_DU320_SIO204_ENTRY,            /* sio204_entry                  */
   SCSI_DU320_EXPANDER_BREAK,         /* expander_break                */
   SCSI_DU320_BTATABLE,                /* busy target array table       */
   SCSI_DU320_BTASIZE,                 /* busy target array size        */
#if !SCSI_SEQ_LOAD_USING_PIO
   sizeof(Seqeload),                   /* Loader code size              */
   Seqeload,                           /* Loader Code                   */
   SCSI_DU320_LOAD_ADDR,               /* address of seq buffer         */
   SCSI_DU320_LOAD_COUNT,              /* number of bytes to be DMAed   */
#endif /* !SCSI_SEQ_LOAD_USING_PIO */
   SCSI_DU320_PASS_TO_DRIVER,          /* pass_to_driver                */
   SCSI_DU320_ACTIVE_SCB,              /* active scb                    */
   SCSI_DU320_RET_ADDR,                /* return address                */
   SCSI_DU320_Q_NEW_POINTER,           /* pointer to new SCB            */
   SCSI_DU320_SG_STATUS,               /* S/G status                    */
   SCSI_DEU320_ARPINTVALID_REG,
   SCSI_DEU320_ARPINTVALID_BIT,
   0,
   SCSIhDownshiftU320SetupSequencer, /* setup sequencer               */
   SCSIhDownshiftU320ResetSoftware,     /* reset software                */
   SCSIhDownshiftDeliverScb,            /* deliver scb                   */
   SCSIhDownshiftU320SetupAtnLength, /* setup atn_length              */
   SCSIhDownshiftU320TargetClearBusy,   /* clear busy target             */
   SCSIhDownshiftEnhU320RequestSense,   /* request sense setup           */
   SCSIhDownshiftU320ResetBTA,          /* reste busy target array       */
   SCSIhDownshiftGetConfig,             /* get driver config info        */
#if(!SCSI_BIOS_SUPPORT &&  SCSI_SCBBFR_BUILTIN)
   SCSIhSetupAssignScbBuffer,          /* set assign scb buffer         */
   SCSIhAssignScbDescriptor,           /* assign scb buffer descriptor  */
   SCSIhFreeScbDescriptor,             /* free scb buffer descriptor    */
#else
   0,
   0,
   0,
#endif /* (!SCSI_BIOS_SUPPORT &&  SCSI_SCBBFR_BUILTIN) */
   SCSIhXferRateAssign,                /* assign xfer rate              */
   SCSIhGetNegoXferRate,               /* get negotiation xfer rate     */
   SCSIhDownshiftAbortChannel,          /* aborting HIOBs for a channel  */
   SCSIhDownshiftU320ResidueCalc,       /* calculate residual length  */
   SCSIhDownshiftU320IgnoreWideResidueCalc, /* update residual length */
   SCSIhEvenIOLength,                   /* determine wide residue        */
   SCSIhDownshiftEnhU320Underrun,       /* sequencer reported underrun/overrun */
   
#if !SCSI_BIOS_SUPPORT
   SCSIhDownshiftU320UpdateExeQAtnLength,/* update atn_length in Execution Queue*/
   SCSIhDownshiftUpdateNewQAtnLength,   /* update atn_length in New Queue*/
#else
   0,
   0,
#endif /* SCSI_BIOS_SUPPORT */
   SCSIhDownshiftEnhU320QHeadPNPSwitchSCB, /* queue PNP switched SCB to head */
   0                                  /* set disconnect delay value    */
  };
#endif /* (SCSI_DOWNSHIFT_ENHANCED_U320_MODE && defined(SCSI_DATA_DEFINE)) */
#endif /* SCSI_MULTI_DOWNSHIFT_MODE */

#if (SCSI_MULTI_MODE || SCSI_MULTI_DOWNSHIFT_MODE)
/***************************************************************************
*  SCSI_FIRMWARE_DESCRIPTOR table for indexing
***************************************************************************/
#define  SCSI_MAX_MODES    5
#if  defined(SCSI_DATA_DEFINE)
SCSI_FIRMWARE_DESCRIPTOR SCSI_LPTR SCSIFirmware[SCSI_MAX_MODES] =
{

#if SCSI_DCH_U320_MODE 
   &SCSIFirmwareDCHU320,
#else
#if SCSI_STANDARD_U320_MODE
   &SCSIFirmwareStandardU320,
#else
   (SCSI_FIRMWARE_DESCRIPTOR SCSI_LPTR) 0,
#endif /* SCSI_STANDARD_U320_MODE */
#if SCSI_DOWNSHIFT_U320_MODE
   &SCSIFirmwareDownshiftU320,
#else
   (SCSI_FIRMWARE_DESCRIPTOR SCSI_LPTR) 0,
#endif /* SCSI_DOWNSHIFT_U320_MODE */
#if SCSI_STANDARD_ENHANCED_U320_MODE
   &SCSIFirmwareStandardEnhancedU320,
#else
   (SCSI_FIRMWARE_DESCRIPTOR SCSI_LPTR) 0,
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

#if SCSI_DOWNSHIFT_ENHANCED_U320_MODE
   &SCSIFirmwareDownshiftEnhancedU320
#else
   (SCSI_FIRMWARE_DESCRIPTOR SCSI_LPTR) 0
#endif /* SCSI_DOWNSHIFT_ENHANCED_U320_MODE */
#endif /* SCSI_DCH_U320_MODE  */
};
#else  
extern SCSI_FIRMWARE_DESCRIPTOR SCSI_LPTR SCSIFirmware[SCSI_MAX_MODES];
#endif /* defined(SCSI_DATA_DEFINE) */
#endif /* SCSI_MULTI_MODE || SCSI_MULTI_DOWNSHIFT_MODE */

