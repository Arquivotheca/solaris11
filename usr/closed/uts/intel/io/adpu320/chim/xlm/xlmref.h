/*$Header: /vobs/u320chim/src/chim/xlm/xlmref.h   /main/26   Mon Mar 17 18:24:41 2003   quan $*/

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
*  Module Name:   XLMREF.H
*
*  Description:   Definitions for data structure and function call
*                 reference under Translation Layer.
*    
*  Owners:        ECX IC Firmware Team
*    
*  Notes:
*
***************************************************************************/

/***************************************************************************
* Miscellaneous
***************************************************************************/
typedef struct SCSI_ADAPTER_TSCB_ SCSI_ADAPTER_TSCB;
typedef struct SCSI_TARGET_TSCB_ SCSI_TARGET_TSCB;


/****************************************************************************
* Definitions for scontrol values to be used in OSDBuildSCB routines
****************************************************************************/
#define SCSI_SIMPLETAG                    0x00
#define SCSI_HEADTAG                      0x01
#define SCSI_ORDERTAG                     0x02
#define SCSI_TAGENB                       0x20
#define SCSI_DISCENB                      0x40

/***************************************************************************
* Miscellaneous defines
***************************************************************************/
/* U320 ioHandle defines */
#define  SCSI_MEMORYMAPPED_INDEX          0x01  /* Uses MBAR 1 */
#define  SCSI_IOMAPPED_INDEX1             0x00  /* Uses MBAR 0 */ 
#define  SCSI_IOMAPPED_INDEX2             0x03  /* Uses MBAR 3 */
#define  SCSI_IOHANDLE_OFFSET             0x00
#define  SCSI_MEMMAPPED_HANDLE_LENGTH      512
#define  SCSI_IOMAPPED_HANDLE_LENGTH       256  
#define  SCSI_IOHANDLE_NO_PACING             0  /* No restriction on pacing */ 
#define  SCSI_HIGHREGBANK_START            256  /* Starting register of high
                                                 * bank for I/O mapped memory
                                                 */
/***************************************************************************
* Macros for general purposes
***************************************************************************/
#define  SCSI_INITSH(initializationTSH)  \
               ((SCSI_INITIALIZATION_TSCB HIM_PTR) (initializationTSH))
#define  SCSI_ADPTSH(adapterTSH) ((SCSI_ADAPTER_TSCB HIM_PTR)(adapterTSH))
#define  SCSI_TRGTSH(targetTSH)  ((SCSI_TARGET_TSCB HIM_PTR)(targetTSH))
#define  SCSI_NODETSH(nodeTSH)   ((SCSI_NODE HIM_PTR)(nodeTSH))
#define  SCSI_NEXUSTSH(nexusTSH) ((SCSI_NEXUS HIM_PTR)(nexusTSH))
#define  SCSI_OSMFP(adapterTSH) SCSI_ADPTSH((adapterTSH))->osmFuncPtrs
#define  SCSI_IOBRSV(iob)  ((SCSI_IOB_RESERVE HIM_PTR) (iob)->iobReserve.virtualAddress)
#define  SCSI_GETIOB(hiob) *((HIM_IOB HIM_PTR HIM_PTR) (((SCSI_UEXACT8 HIM_PTR)(hiob)) - \
                              OSMoffsetof(SCSI_IOB_RESERVE,hiob) + \
                              OSMoffsetof(SCSI_IOB_RESERVE,iob)))
#define  SCSI_GETADP(hhcb)    ((SCSI_ADAPTER_TSCB HIM_PTR) \
                              (((SCSI_UEXACT8 HIM_PTR)(hhcb)) - \
                              OSMoffsetof(SCSI_ADAPTER_TSCB,hhcb)))
#define  SCSI_GETTRGT(targetUnit) ((SCSI_TARGET_TSCB HIM_PTR) \
                                  (((SCSI_UEXACT8 HIM_PTR)(targetUnit)) - \
                                  OSMoffsetof(SCSI_TARGET_TSCB,unitControl)))
#define  SCSI_IS_U320_ID(productID)  \
                              (((productID) & 0xFF00FFFF) == 0x80009005)

#define SCSI_xHARPOON_REV_B(productID,devRevID) \
           ((((((productID) >> 16) & SCSI_ID_MASK) == SCSI_HARPOON2_BASED_ID) && \
             ((devRevID) >= SCSI_HARPOON2_REV_10_CHIP)) || \
            (((((productID) >> 16) & SCSI_ID_MASK) == SCSI_HARPOON1_BASED_ID) && \
             ((devRevID) >= SCSI_HARPOON1_REV_10_CHIP)))

#define  SCSI_xVALIDATE_LUN(targetTSH) SCSIxNonScamValidateLun((targetTSH))

#if SCSI_RAID1 
#define  SCSI_RAID1_MIRROR_COMPLETE 1
#define  SCSI_RAID1_PRIMARY_COMPLETE 0
#define  SCSI_GET_MIRROR_HIOB(hiob)  hiob->mirrorHiob

#define  SCSI_GET_MIRROR_IOB(hiob)  (*((HIM_IOB HIM_PTR HIM_PTR) \
                              (((SCSI_UEXACT8 HIM_PTR)(hiob)) - \
                              OSMoffsetof(SCSI_IOB_RESERVE,hiob) + \
                              OSMoffsetof(SCSI_IOB_RESERVE,iob))))->relatedIob

#define SCSI_UPDATE_MIRROR_HIOB(hiob) hiob->mirrorHiob->cmd = hiob->cmd; \
   hiob->mirrorHiob->flags = hiob->flags; \
   hiob->mirrorHiob->SCSI_IF_raid1Primary = 0

#endif /* SCSI_RAID1 */

/***************************************************************************
* Macros for operating mode independent
***************************************************************************/

/***************************************************************************
* Enable dual address cycle for 64 bits bus addressing
***************************************************************************/
#if OSM_DMA_SWAP_WIDTH == 64
#define  SCSI_ENABLE_DAC(adapterTSH)  SCSIxEnableDAC((adapterTSH))
#else
#define  SCSI_ENABLE_DAC(adapterTSH)
#endif /* OSM_DMA_SWAP_WIDTH == 64 */

/***************************************************************************
*  SCSI_HOST_TYPE definitions for host devices supported
***************************************************************************/
typedef struct SCSI_HOST_TYPE_
{
   SCSI_UINT32 idHost;                       /* host id supported    */
   SCSI_UINT32 idMask;                       /* mask for the host id */
} SCSI_HOST_TYPE;

/***************************************************************************
*  SCSI_BIOS_INFO definitions
***************************************************************************/
typedef struct SCSI_BIOS_INFORMATION_
{
   struct
   {
      SCSI_UINT8 biosActive:1;               /* bios active          */
      SCSI_UINT8 extendedTranslation:1;      /* extended translation */
   } biosFlags;
   SCSI_UINT8 firstBiosDrive;                /* first bios drive     */
   SCSI_UINT8 lastBiosDrive;                 /* last bios drive      */
   SCSI_UINT8 numberDrives;                  /* number of bios drives*/
   SCSI_UINT8 versionFormat;                 /* bios version format  */
   SCSI_UINT8 majorNumber;                   /* major version number */
   SCSI_UINT8 minorNumber;                   /* minor version number */
   SCSI_UINT8 subMinorNumber;                /* sub minor ver number */
   struct
   {
      SCSI_UINT8 targetID;                   /* drive target id      */
      SCSI_UINT8 lunID;                      /* drive lun id         */
   } biosDrive[16];                          /* bios drives          */
} SCSI_BIOS_INFORMATION;

/***************************************************************************
*  Function prototype internal to translation management layer
***************************************************************************/
SCSI_UINT8 OSDGetNVData (SCSI_HHCB SCSI_HPTR, void HIM_PTR, HIM_UINT32, HIM_UINT32);
void SCSIxMaximizeMemory(SCSI_MEMORY_TABLE HIM_PTR,SCSI_MEMORY_TABLE HIM_PTR);
void SCSIxHandledvThrottleOSMReset(SCSI_HHCB SCSI_HPTR);
void OSDBuildSCB(SCSI_HIOB SCSI_IPTR);
void OSDStandardU320BuildSCB(SCSI_HIOB HIM_PTR,HIM_IOB HIM_PTR,SCSI_TARGET_TSCB HIM_PTR,SCSI_ADAPTER_TSCB HIM_PTR);
void OSDStandardEnhU320BuildSCB(SCSI_HIOB HIM_PTR,HIM_IOB HIM_PTR,SCSI_TARGET_TSCB HIM_PTR,SCSI_ADAPTER_TSCB HIM_PTR);
void OSDDchU320BuildSCB(SCSI_HIOB HIM_PTR,HIM_IOB HIM_PTR,SCSI_TARGET_TSCB HIM_PTR,SCSI_ADAPTER_TSCB HIM_PTR);
SCSI_BUFFER_DESCRIPTOR HIM_PTR OSDGetSGList (SCSI_HIOB SCSI_IPTR);
void OSDCompleteHIOB(SCSI_HIOB SCSI_IPTR);
void OSDCompleteSpecialHIOB(SCSI_HIOB SCSI_IPTR);
void SCSIxTranslateError(HIM_IOB HIM_PTR,SCSI_HIOB HIM_PTR);
SCSI_BUS_ADDRESS OSDGetBusAddress(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,void SCSI_IPTR);
void OSDAdjustBusAddress(SCSI_HHCB SCSI_HPTR,SCSI_BUS_ADDRESS SCSI_IPTR,SCSI_UINT);

#if !SCSI_DCH_U320_MODE
SCSI_UEXACT32 OSDReadPciConfiguration(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void OSDWritePciConfiguration(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_UEXACT32);
#endif /* !SCSI_DCH_U320_MODE */

void OSDAsyncEvent(SCSI_HHCB SCSI_HPTR,SCSI_UINT16,...);
SCSI_HOST_ADDRESS SCSI_LPTR OSDGetHostAddress(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 OSDInExact8High(SCSI_REGISTER,SCSI_UINT32);
void OSDOutExact8High(SCSI_REGISTER,SCSI_UINT32,SCSI_UEXACT8);
#if SCSI_DCH_U320_MODE 
SCSI_UEXACT16 OSDInExact16High(SCSI_REGISTER,SCSI_UINT32);
void OSDOutExact16High(SCSI_REGISTER,SCSI_UINT32,SCSI_UEXACT16);
#endif /* SCSI_DCH_U320_MODE */
void OSDSynchronizeRange(SCSI_REGISTER);
void SCSIxGetBiosInformation(HIM_TASK_SET_HANDLE);
SCSI_UINT8 SCSIxChkLunExist(SCSI_UEXACT8 HIM_PTR,SCSI_UINT8,SCSI_UINT8);
void SCSIxSetLunExist(SCSI_UEXACT8 HIM_PTR,SCSI_UINT8,SCSI_UINT8);
void SCSIxClearLunExist(SCSI_UEXACT8 HIM_PTR,SCSI_UINT8,SCSI_UINT8);
SCSI_UINT8 SCSIxSetupBusScan(SCSI_ADAPTER_TSCB HIM_PTR);
void SCSIxSetupLunProbe (SCSI_ADAPTER_TSCB HIM_PTR,SCSI_UEXACT8,SCSI_UEXACT8);
#if SCSI_PACKETIZED_IO_SUPPORT           
#if (SCSI_TASK_SWITCH_SUPPORT == 2)
SCSI_UEXACT8 OSDGetTargetCmd (SCSI_HIOB SCSI_IPTR );
#endif /* SCSI_TASK_SWITCH_SUPPORT == 2 */
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#if SCSI_PAC_SEND_SSU
void SCSIxQueueSSU(SCSI_ADAPTER_TSCB HIM_PTR);
HIM_UINT32 SCSIxPostSSU(HIM_IOB HIM_PTR);
void SCSIxQueueTUR(SCSI_ADAPTER_TSCB HIM_PTR);
HIM_UINT32 SCSIxPostTUR(HIM_IOB HIM_PTR);
#endif /* SCSI_PAC_SEND_SSU */
void SCSIxQueueBusScan(SCSI_ADAPTER_TSCB HIM_PTR);
HIM_UINT32 SCSIxPostBusScan(HIM_IOB HIM_PTR);
#if !SCSI_DISABLE_PROBE_SUPPORT
void SCSIxPostProbe(HIM_IOB HIM_PTR,HIM_UEXACT32,HIM_UEXACT8);
#endif /* !SCSI_DISABLE_PROBE_SUPPORT */

#if SCSI_DOMAIN_VALIDATION
void SCSIxRestoreDefaultXfer(SCSI_ADAPTER_TSCB HIM_PTR,SCSI_TARGET_TSCB HIM_PTR);
void SCSIxDvForceNegotiation(SCSI_ADAPTER_TSCB HIM_PTR,SCSI_TARGET_TSCB HIM_PTR);
void SCSIxSetupDomainValidation(SCSI_ADAPTER_TSCB HIM_PTR);
void SCSIxQueueDomainValidation(SCSI_ADAPTER_TSCB HIM_PTR);

void SCSIxDvForceAsyncNegotiation(SCSI_ADAPTER_TSCB HIM_PTR,SCSI_TARGET_TSCB HIM_PTR);
void SCSIxQueueDomainValidationSM (SCSI_ADAPTER_TSCB HIM_PTR);
SCSI_UEXACT8 SCSIxDomainValidationPatternGenerator (SCSI_UEXACT8);
HIM_UINT32 SCSIxPostDomainValidationSM(HIM_IOB HIM_PTR);

HIM_UINT32 SCSIxThrottleTransfer (SCSI_ADAPTER_TSCB HIM_PTR,SCSI_TARGET_TSCB HIM_PTR);

#endif /* SCSI_DOMAIN_VALIDATION */

void SCSIxQueueSpecialIOB(SCSI_ADAPTER_TSCB HIM_PTR,SCSI_UINT8);
HIM_UINT32 SCSIxPostQueueSpecialIOB(HIM_IOB HIM_PTR);
SCSI_UEXACT8 SCSIxDetermineFirmwareMode(SCSI_ADAPTER_TSCB HIM_PTR);
void SCSIxEnableDAC(HIM_TASK_SET_HANDLE);
HIM_HOST_ID SCSIxU320GetNextSubSystemSubVendorID (HIM_UINT16,HIM_HOST_ID HIM_PTR);

#if SCSI_TARGET_OPERATION
void OSDBuildTargetSCB(SCSI_HIOB SCSI_IPTR);
void OSDStandardU320BuildTargetSCB(SCSI_HIOB HIM_PTR,HIM_IOB HIM_PTR,SCSI_NEXUS SCSI_XPTR,SCSI_ADAPTER_TSCB HIM_PTR);
void OSDStandardEnhU320BuildTargetSCB(SCSI_HIOB HIM_PTR,HIM_IOB HIM_PTR,SCSI_NEXUS SCSI_XPTR,SCSI_ADAPTER_TSCB HIM_PTR);
void OSDDchU320BuildTargetSCB(SCSI_HIOB HIM_PTR,HIM_IOB HIM_PTR,SCSI_NEXUS SCSI_XPTR,SCSI_ADAPTER_TSCB HIM_PTR);
void SCSIxTargetTaskManagementRequest(HIM_IOB HIM_PTR,SCSI_HIOB HIM_PTR);
#if SCSI_MULTIPLEID_SUPPORT
HIM_BOOLEAN SCSIxEnableID(HIM_IOB HIM_PTR);
HIM_BOOLEAN SCSIxDisableID(HIM_IOB HIM_PTR);
#endif /* SCSI_MULTIPLEID_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */


#if (SCSI_PROFILE_INFORMATION && SCSI_INITIATOR_OPERATION && (!SCSI_DISABLE_ADJUST_TARGET_PROFILE))
void SCSIxAdjustTransferOption( HIM_TASK_SET_HANDLE targetTSH, \
                                HIM_TARGET_PROFILE HIM_PTR profile, \
                                SCSI_HW_INFORMATION SCSI_LPTR phwInformation);
#endif

HIM_BOOLEAN SCSIxIsInvalidRelatedIob(HIM_IOB HIM_PTR iob);




