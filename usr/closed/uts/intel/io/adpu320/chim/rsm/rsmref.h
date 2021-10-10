/*$Header: /vobs/u320chim/src/chim/rsm/rsmref.h   /main/24   Fri Aug 22 16:19:46 2003   hu11135 $*/

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
*  Module Name:   RSMREF.H
*
*  Description:   Definitions internal to resource management layer
*    
*  Owners:        ECX IC Firmware Team
*    
*  Notes:         1. This file should only be referenced by resource
*                    management layer.
*
***************************************************************************/


/***************************************************************************
* Macro definitions for resource management layer
***************************************************************************/
#if SCSI_TARGET_OPERATION
#define  SCSI_rFRESH_HIOB(hiob)  (hiob)->stat = 0;\
                                 (hiob)->haStat = 0;\
                                 (hiob)->trgStatus = 0;\
                                 (hiob)->SCSI_IP_mgrStat = 0;\
                                 (hiob)->SCSI_IP_flags = 0;\
                                 (hiob)->SCSI_IP_negoState = 0;

#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
#define  SCSI_rFRESH_NEXUS(nexus) (nexus)->SCSI_XF_lunTar = 0;\
                                  (nexus)->SCSI_XF_disconnectAllowed = 0;\
                                  (nexus)->SCSI_XF_busHeld = 0;\
                                  (nexus)->SCSI_XF_tagRequest = 0;\
                                  (nexus)->SCSI_XF_lastResource = 0;\
                                  (nexus)->SCSI_XF_available = 0;\
                                  (nexus)->SCSI_XF_scsi1Selection = 0;\
                                  (nexus)->SCSI_XF_packetized = 0;
#else
#define  SCSI_rFRESH_NEXUS(nexus) (nexus)->SCSI_XF_lunTar = 0;\
                                  (nexus)->SCSI_XF_disconnectAllowed = 0;\
                                  (nexus)->SCSI_XF_busHeld = 0;\
                                  (nexus)->SCSI_XF_tagRequest = 0;\
                                  (nexus)->SCSI_XF_lastResource = 0;\
                                  (nexus)->SCSI_XF_available = 0;\
                                  (nexus)->SCSI_XF_scsi1Selection = 0;
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */

#define  SCSI_rTARGETMODE_HIOB(hiob)  (hiob)->SCSI_IP_targetMode = 1; 
#define  SCSI_rCLEARALLDEVICEQUEUES(hhcb,haStat)\
            if ((hhcb)->SCSI_HF_initiatorMode)\
            {\
               SCSIrClearAllDeviceQueues((hhcb),(haStat));\
            }
/* Attempt to queue Establish Connection HIOBs which 
 * may have been queued to HIOB queue during reset.
 */
#define  SCSI_rQUEUEESTHIOBS(hhcb)\
            if ((hhcb)->SCSI_HF_targetMode && \
                (!((hhcb)->SCSI_HF_targetHoldEstHiobs)))\
            {\
               SCSIrQueueEstHiobs((hhcb));\
            }
#define  SCSI_rFREEBUSHELDSCB(hhcb)\
            if ((hhcb)->SCSI_HF_targetMode && \
                (!SCSI_IS_NULL_SCB((hhcb)->SCSI_HP_busHeldScbNumber))) \
            {\
               SCSIrFreeBusHeldScb((hhcb));\
            }          

/* Don't call SCSIrFreeCriteria for target mode hiobs */ 
#define  SCSI_rFREECRITERIA(hiob)\
            if (!(hiob)->SCSI_IP_targetMode)\
            {\
               SCSIrFreeCriteria((hiob));\
            }
#define  SCSI_rFREEZEALLDEVICEQUEUES(hhcb)\
            if ((hhcb)->SCSI_HF_initiatorMode)\
            {\
               SCSIrFreezeAllDeviceQueues((hhcb));\
            }
#define  SCSI_rUNFREEZEALLDEVICEQUEUES(hhcb,freezeVal)\
            if ((hhcb)->SCSI_HF_initiatorMode)\
            {\
               SCSIrUnFreezeAllDeviceQueues((hhcb),(freezeVal));\
            }

#define  SCSI_rRESETTARGETMODE(hhcb)\
            if ((hhcb)->SCSI_HF_targetMode)\
            {\
               SCSIrResetTargetMode((hhcb));\
            }
#define  SCSI_rTARGETGETCONFIG(hhcb)\
            if ((hhcb)->SCSI_HF_targetMode)\
            {\
               SCSIrTargetGetConfiguration((hhcb));\
            }

#define  SCSI_rDECREMENT_NON_EST_SCBS_AVAIL(hhcb)\
            if ((hhcb)->SCSI_HF_targetMode)\
            {\
               (hhcb)->SCSI_NON_EST_SCBS_AVAIL--;\
            }

#define  SCSI_rINCREMENT_NON_EST_SCBS_AVAIL(hhcb)\
            if ((hhcb)->SCSI_HF_targetMode)\
            {\
               (hhcb)->SCSI_NON_EST_SCBS_AVAIL++;\
            }

#else /* !SCSI_TARGET_OPERATION */
#if SCSI_PACKETIZED_IO_SUPPORT
#define  SCSI_rFRESH_HIOB(hiob)  (hiob)->stat = 0;\
                                 (hiob)->haStat = 0;\
                                 (hiob)->trgStatus = 0;\
                                 (hiob)->tmfStatus = 0;\
                                 (hiob)->SCSI_IP_mgrStat = 0;\
                                 (hiob)->SCSI_IP_negoState = 0;
#else /* !SCSI_PACKETIZED_IO_SUPPORT */
#define  SCSI_rFRESH_HIOB(hiob)  (hiob)->stat = 0;\
                                 (hiob)->haStat = 0;\
                                 (hiob)->trgStatus = 0;\
                                 (hiob)->SCSI_IP_mgrStat = 0;\
                                 (hiob)->SCSI_IP_negoState = 0;
#endif /* SCSI_PACKETIZED_IO_SUPPORT */
#define  SCSI_rCLEARALLDEVICEQUEUES(hhcb,haStat)\
            SCSIrClearAllDeviceQueues((hhcb),(haStat));
#define  SCSI_rQUEUEESTHIOBS(hhcb)
#define  SCSI_rFREEBUSHELDSCB(hhcb)
#define  SCSI_rFREECRITERIA(hiob)  SCSIrFreeCriteria((hiob));
#define  SCSI_rFREEZEALLDEVICEQUEUES(hhcb)  SCSIrFreezeAllDeviceQueues((hhcb));
#define  SCSI_rUNFREEZEALLDEVICEQUEUES(hhcb,freezeVal)  SCSIrUnFreezeAllDeviceQueues((hhcb),(freezeVal));
#define  SCSI_rRESETTARGETMODE(hhcb)
#define  SCSI_rTARGETGETCONFIG(hhcb)
#define  SCSI_rDECREMENT_NON_EST_SCBS_AVAIL(hhcb)
#define  SCSI_rINCREMENT_NON_EST_SCBS_AVAIL(hhcb)
#endif /* SCSI_TARGET_OPERATION */

#if SCSI_MULTI_MODE
#define  SCSI_rREMOVESCBS(hhcb)\
               if ((hhcb)->SCSI_HP_firmwareDescriptor->SCSIrRemoveScbs)\
               {\
                  (hhcb)->SCSI_HP_firmwareDescriptor->SCSIrRemoveScbs((hhcb));\
               }
#define  SCSI_rGETSCB(hhcb,hiob)       (hhcb)->SCSI_HP_firmwareDescriptor->SCSIrGetScb((hhcb),(hiob))
#define  SCSI_rRETURNSCB(hhcb,hiob) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIrReturnScb((hhcb),(hiob))
#define  SCSI_rMAX_NONTAG_SCBS(hhcb) (hhcb)->SCSI_HP_firmwareDescriptor->MaxNonTagScbs
#if SCSI_TARGET_OPERATION
#define  SCSI_rGETESTSCB(hhcb,hiob)    (hhcb)->SCSI_HP_firmwareDescriptor->SCSIrGetEstScb((hhcb),(hiob))
#define  SCSI_rRETURNESTSCB(hhcb,hiob) (hhcb)->SCSI_HP_firmwareDescriptor->SCSIrReturnEstScb((hhcb),(hiob))
#endif /* SCSI_TARGET_OPERATION */

#else 

#if SCSI_STANDARD_MODE
#define  SCSI_rGETSCB(hhcb,hiob) SCSIrGetScb((hhcb),(hiob))
#define  SCSI_rRETURNSCB(hhcb,hiob) SCSIrReturnScb((hhcb),(hiob))

#if (SCSI_STANDARD_U320_MODE || SCSI_DCH_U320_MODE)
#define  SCSI_rREMOVESCBS(hhcb) SCSIrStandardRemoveScbs((hhcb))
#endif /* (SCSI_STANDARD_U320_MODE || SCSI_DCH_U320_MODE) */

#if SCSI_STANDARD_ENHANCED_U320_MODE
#define  SCSI_rREMOVESCBS(hhcb) SCSIrStandardEnhU320RemoveScbs((hhcb))
#endif /* SCSI_STANDARD_ENHANCED_U320_MODE */

#if SCSI_STANDARD_U320_MODE || SCSI_STANDARD_ENHANCED_U320_MODE
#define  SCSI_rMAX_NONTAG_SCBS(hhcb) SCSI_SU320_MAX_NONTAG_SCBS
#elif SCSI_DCH_U320_MODE
#define  SCSI_rMAX_NONTAG_SCBS(hhcb) SCSI_DCHU320_MAX_NONTAG_SCBS
#endif /* SCSI_STANDARD_U320_MODE || SCSI_STANDARD_ENHANCED_U320_MODE */
#if SCSI_TARGET_OPERATION
#define  SCSI_rGETESTSCB(hhcb,hiob)  SCSIrGetEstScb((hhcb),(hiob))
#define  SCSI_rRETURNESTSCB(hhcb,hiob)  SCSIrReturnEstScb((hhcb),(hiob))
#endif /* SCSI_TARGET_OPERATION */
#endif /* SCSI_STANDARD_MODE */

#endif /* SCSI_MULTI_MODE */

/***************************************************************************
* Function prototype internal to resource management layer
***************************************************************************/
/* RSMINIT.C */
void SCSIrReset(SCSI_HHCB SCSI_HPTR);
void SCSIrStandardRemoveScbs(SCSI_HHCB SCSI_HPTR);
void SCSIrStandardEnhU320RemoveScbs(SCSI_HHCB SCSI_HPTR);
/* SCSI_TARGET_OPERATION prototypes */
void SCSIrResetTargetMode(SCSI_HHCB SCSI_HPTR);
void SCSIrTargetGetConfiguration(SCSI_HHCB SCSI_HPTR);
void SCSIrInitializeNexusQueue(SCSI_HHCB SCSI_HPTR);
void SCSIrInitializeNodeQueue(SCSI_HHCB SCSI_HPTR);

/* RSMTASK.C */
void SCSIrCompleteHIOB(SCSI_HIOB SCSI_IPTR);
void SCSIrCompleteSpecialHIOB(SCSI_HIOB SCSI_IPTR);
void SCSIrAsyncEvent(SCSI_HHCB SCSI_HPTR,SCSI_UINT16,...);
void SCSIrScsiBusReset(SCSI_HIOB SCSI_IPTR);
void SCSIrTargetReset(SCSI_HIOB SCSI_IPTR);
void SCSIrAbortTask(SCSI_HIOB SCSI_IPTR);
void SCSIrAbortTaskSet(SCSI_HIOB SCSI_IPTR);
void SCSIrAsyncEventCommand(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR,SCSI_UINT16,...);

#if SCSI_TARGET_OPERATION   
void SCSIrAbortNexus(SCSI_HIOB SCSI_IPTR);
SCSI_NODE SCSI_NPTR SCSIrAllocateNode(SCSI_HHCB SCSI_HPTR);
SCSI_HIOB SCSI_IPTR  SCSIrHiobPoolRemove(SCSI_HHCB SCSI_HPTR);
void SCSIrReturnToHiobPool(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_NEXUS SCSI_XPTR SCSIrAllocateNexus(SCSI_HHCB SCSI_HPTR);
void SCSIrFreeNexus(SCSI_HHCB SCSI_HPTR,SCSI_NEXUS SCSI_XPTR);  
SCSI_UINT8 SCSIrClearNexus(SCSI_NEXUS SCSI_XPTR,SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIrFindAndAbortTargetHIOBs(SCSI_HHCB SCSI_HPTR,SCSI_NEXUS SCSI_XPTR,SCSI_UEXACT8);
void SCSIrTargetTaskManagementRequest(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8,SCSI_NEXUS SCSI_XPTR);
void SCSIrQueueEstHiobPool(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrQueueEstHiobToHwm(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrQueueEstHiobs(SCSI_HHCB SCSI_HPTR);
#endif /* SCSI_TARGET_OPERATION */ 

/* RSMUTIL.C */
void SCSIrCheckCriteria(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrAllocateScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_UEXACT16 SCSIrGetScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrReturnScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrDeviceQueueAppend(SCSI_TARGET_CONTROL SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_HIOB SCSI_IPTR SCSIrDeviceQueueRemove(SCSI_TARGET_CONTROL SCSI_UPTR);
SCSI_UEXACT8 SCSIrAbortHIOBInDevQ(SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIrClearDeviceQueue(SCSI_TARGET_CONTROL SCSI_HPTR,SCSI_UEXACT8);
void SCSIrClearAllDeviceQueues(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIrHostQueueAppend(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
SCSI_HIOB SCSI_IPTR SCSIrHostQueueRemove(SCSI_HHCB SCSI_HPTR);
SCSI_UEXACT8 SCSIrAbortHIOBInHostQ(SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIrFindAndAbortHIOB(SCSI_HHCB SCSI_HPTR,SCSI_UNIT_CONTROL SCSI_UPTR,SCSI_UEXACT8);
void SCSIrFindAndAbortDevQ(SCSI_UNIT_CONTROL SCSI_UPTR,SCSI_UEXACT8);
void SCSIrClearHostQueue(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIrFreeScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrFreeCriteria(SCSI_HIOB SCSI_IPTR);
void SCSIrPostAbortedHIOB(SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
void SCSIrPostDoneQueue(SCSI_HHCB SCSI_HPTR);
void SCSIrAdjustQueue(SCSI_HIOB SCSI_IPTR SCSI_IPTR,SCSI_HIOB SCSI_IPTR SCSI_IPTR,
                      SCSI_HIOB SCSI_IPTR SCSI_IPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrFinishQueueFull(SCSI_HIOB SCSI_IPTR);
void SCSIrFreezeAllDeviceQueues(SCSI_HHCB SCSI_HPTR);
void SCSIrUnFreezeAllDeviceQueues(SCSI_HHCB SCSI_HPTR, SCSI_UINT8);
void SCSIrFreezeDeviceQueue(SCSI_TARGET_CONTROL SCSI_HPTR, SCSI_UINT8);
void SCSIrUnFreezeDeviceQueue(SCSI_TARGET_CONTROL SCSI_HPTR, SCSI_UINT8);
void SCSIrDispatchQueuedHIOB(SCSI_HIOB SCSI_IPTR);
void SCSIrFreezeHostQueue(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
void SCSIrUnFreezeHostQueue(SCSI_HHCB SCSI_HPTR,SCSI_UEXACT8);
SCSI_UEXACT8 SCSIrSwitchToPacketized (SCSI_UNIT_CONTROL SCSI_UPTR);
SCSI_UEXACT8 SCSIrSwitchToNonPacketized (SCSI_UNIT_CONTROL SCSI_UPTR);

#if SCSI_TARGET_OPERATION
SCSI_UEXACT8 SCSIrAbortHIOBInHiobQ(SCSI_HIOB SCSI_IPTR,SCSI_UEXACT8);
SCSI_UEXACT16 SCSIrGetEstScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrReturnEstScb(SCSI_HHCB SCSI_HPTR,SCSI_HIOB SCSI_IPTR);
void SCSIrFreeBusHeldScb(SCSI_HHCB SCSI_HPTR);
#if SCSI_PACKETIZED_TARGET_MODE_SUPPORT
void SCSIrTargetFindAndSetNexusIuReqChange(SCSI_HHCB SCSI_HPTR,SCSI_NEXUS SCSI_XPTR);
#endif /* SCSI_PACKETIZED_TARGET_MODE_SUPPORT */
#endif /* SCSI_TARGET_OPERATION */




