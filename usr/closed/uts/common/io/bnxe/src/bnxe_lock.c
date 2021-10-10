
/*
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2008-2011 Broadcom Corporation, ALL RIGHTS RESERVED.
 */

#include "bnxe.h"

#ifndef BNXE_LOCKS_INLINE

void BNXE_LOCK_ENTER_INTR          (um_device_t * pUM, int idx)  { mutex_enter(&pUM->intrMutex[idx]);          }
void BNXE_LOCK_EXIT_INTR           (um_device_t * pUM, int idx)  { mutex_exit(&pUM->intrMutex[idx]);           }
void BNXE_LOCK_ENTER_INTR_FLIP     (um_device_t * pUM, int idx)  { mutex_enter(&pUM->intrFlipMutex[idx]);      }
void BNXE_LOCK_EXIT_INTR_FLIP      (um_device_t * pUM, int idx)  { mutex_exit(&pUM->intrFlipMutex[idx]);       }
void BNXE_LOCK_ENTER_TX            (um_device_t * pUM, int idx)  { mutex_enter(&pUM->txq[idx].txMutex);        }
void BNXE_LOCK_EXIT_TX             (um_device_t * pUM, int idx)  { mutex_exit(&pUM->txq[idx].txMutex);         }
void BNXE_LOCK_ENTER_FREETX        (um_device_t * pUM, int idx)  { mutex_enter(&pUM->txq[idx].freeTxDescMutex);}
void BNXE_LOCK_EXIT_FREETX         (um_device_t * pUM, int idx)  { mutex_exit(&pUM->txq[idx].freeTxDescMutex); }
void BNXE_LOCK_ENTER_RX            (um_device_t * pUM, int idx)  { mutex_enter(&pUM->rxq[idx].rxMutex);        }
void BNXE_LOCK_EXIT_RX             (um_device_t * pUM, int idx)  { mutex_exit(&pUM->rxq[idx].rxMutex);         }
void BNXE_LOCK_ENTER_DONERX        (um_device_t * pUM, int idx)  { mutex_enter(&pUM->rxq[idx].doneRxMutex);    }
void BNXE_LOCK_EXIT_DONERX         (um_device_t * pUM, int idx)  { mutex_exit(&pUM->rxq[idx].doneRxMutex);     }
void BNXE_LOCK_ENTER_SB            (um_device_t * pUM, int idx)  { mutex_enter(&pUM->sbMutex[idx]);            }
void BNXE_LOCK_EXIT_SB             (um_device_t * pUM, int idx)  { mutex_exit(&pUM->sbMutex[idx]);             }
void BNXE_LOCK_ENTER_MCP           (um_device_t * pUM)           { mutex_enter(&pUM->mcpMutex);                }
void BNXE_LOCK_EXIT_MCP            (um_device_t * pUM)           { mutex_exit(&pUM->mcpMutex);                 }
void BNXE_LOCK_ENTER_PHY           (um_device_t * pUM)           { mutex_enter(&pUM->phyMutex);                }
void BNXE_LOCK_EXIT_PHY            (um_device_t * pUM)           { mutex_exit(&pUM->phyMutex);                 }
void BNXE_LOCK_ENTER_DMAE_STATS    (um_device_t * pUM)           { mutex_enter(&pUM->dmaeStatsMutex);          }
void BNXE_LOCK_EXIT_DMAE_STATS     (um_device_t * pUM)           { mutex_exit(&pUM->dmaeStatsMutex);           }
void BNXE_LOCK_ENTER_DMAE_MISC     (um_device_t * pUM)           { mutex_enter(&pUM->dmaeMiscMutex);           }
void BNXE_LOCK_EXIT_DMAE_MISC      (um_device_t * pUM)           { mutex_exit(&pUM->dmaeMiscMutex);            }
void BNXE_LOCK_ENTER_IND           (um_device_t * pUM)           { mutex_enter(&pUM->indMutex);                }
void BNXE_LOCK_EXIT_IND            (um_device_t * pUM)           { mutex_exit(&pUM->indMutex);                 }
void BNXE_LOCK_ENTER_CID           (um_device_t * pUM)           { mutex_enter(&pUM->cidMutex);                }
void BNXE_LOCK_EXIT_CID            (um_device_t * pUM)           { mutex_exit(&pUM->cidMutex);                 }
void BNXE_LOCK_ENTER_SPQ           (um_device_t * pUM)           { mutex_enter(&pUM->spqMutex);                }
void BNXE_LOCK_EXIT_SPQ            (um_device_t * pUM)           { mutex_exit(&pUM->spqMutex);                 }
void BNXE_LOCK_ENTER_SPREQ         (um_device_t * pUM)           { mutex_enter(&pUM->spReqMutex);              }
void BNXE_LOCK_EXIT_SPREQ          (um_device_t * pUM)           { mutex_exit(&pUM->spReqMutex);               }
void BNXE_LOCK_ENTER_RRREQ         (um_device_t * pUM)           { mutex_enter(&pUM->rrReqMutex);              }
void BNXE_LOCK_EXIT_RRREQ          (um_device_t * pUM)           { mutex_exit(&pUM->rrReqMutex);               }
void BNXE_LOCK_ENTER_ISLES_CONTROL (um_device_t * pUM)           { mutex_enter(&pUM->islesCtrlMutex);          }
void BNXE_LOCK_EXIT_ISLES_CONTROL  (um_device_t * pUM)           { mutex_exit(&pUM->islesCtrlMutex);           }
void BNXE_LOCK_ENTER_TOE           (um_device_t * pUM)           { mutex_enter(&pUM->toeMutex);                }
void BNXE_LOCK_EXIT_TOE            (um_device_t * pUM)           { mutex_exit(&pUM->toeMutex);                 }
void BNXE_LOCK_ENTER_MEM           (um_device_t * pUM)           { mutex_enter(&pUM->memMutex);                }
void BNXE_LOCK_EXIT_MEM            (um_device_t * pUM)           { mutex_exit(&pUM->memMutex);                 }
void BNXE_LOCK_ENTER_OFFLOAD       (um_device_t * pUM)           { mutex_enter(&pUM->offloadMutex);            }
void BNXE_LOCK_EXIT_OFFLOAD        (um_device_t * pUM)           { mutex_exit(&pUM->offloadMutex);             }
void BNXE_LOCK_ENTER_HWINIT        (um_device_t * pUM)           { mutex_enter(&pUM->hwInitMutex);             }
void BNXE_LOCK_EXIT_HWINIT         (um_device_t * pUM)           { mutex_exit(&pUM->hwInitMutex);              }
void BNXE_LOCK_ENTER_GLD           (um_device_t * pUM)           { mutex_enter(&pUM->gldMutex);                }
void BNXE_LOCK_EXIT_GLD            (um_device_t * pUM)           { mutex_exit(&pUM->gldMutex);                 }
void BNXE_LOCK_ENTER_GLDTX         (um_device_t * pUM, krw_t rw) { rw_enter(&pUM->gldTxMutex, rw);             }
void BNXE_LOCK_EXIT_GLDTX          (um_device_t * pUM)           { rw_exit(&pUM->gldTxMutex);                  }
void BNXE_LOCK_ENTER_TIMER         (um_device_t * pUM)           { mutex_enter(&pUM->timerMutex);              }
void BNXE_LOCK_EXIT_TIMER          (um_device_t * pUM)           { mutex_exit(&pUM->timerMutex);               }
void BNXE_LOCK_ENTER_STATS         (um_device_t * pUM)           { mutex_enter(&pUM->kstatMutex);              }
void BNXE_LOCK_EXIT_STATS          (um_device_t * pUM)           { mutex_exit(&pUM->kstatMutex);               }

#endif /* BNXE_LOCKS_INLINE */

