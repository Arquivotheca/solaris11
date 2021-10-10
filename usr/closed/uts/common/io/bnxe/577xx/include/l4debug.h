/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2009 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *
 ******************************************************************************/

#ifndef _L4DEBUG_H
#define _L4DEBUG_H

#include "bcmtype.h"

#define L4_DEBUG_BIT_FIELDS 0
// Use _l4_tcp_const_state_t _pad[0] for L4 debug
// PbfQueue		- bits 7-5
// VOQ          - bits 4-2
// cos			- bits 1-0
#define L4_DEBUG_PBF_Q_MASK 		0xE0
#define L4_DEBUG_PBF_Q_SHIFT 		5

#define L4_DEBUG_VOQ_MASK    		0x1C
#define L4_DEBUG_VOQ_SHIFT 	    	2

#define L4_DEBUG_COS_MASK 			0x3
#define L4_DEBUG_COS_SHIFT 			0
	


#define L4_DEBUG_FLAGS 1
// QmOpportunistic - bit 3
// TestingEn	   - bit 2
// LbEn 		   - bit 1
// CosEn 		   - bit 0
#define L4_DEBUG_QM_OPPORT_EN_MASK	0x08
#define L4_DEBUG_TESTING_EN_MASK 	0x04
#define L4_DEBUG_LB_EN_MASK 		0x02
#define L4_DEBUG_COS_EN_MASK 		0x01



#define L4_DEBUG_GET_COS(_data)		 		((_data[L4_DEBUG_BIT_FIELDS] & L4_DEBUG_COS_MASK) >> L4_DEBUG_COS_SHIFT)
#define L4_DEBUG_GET_PBFQUEUE(_data) 		((_data[L4_DEBUG_BIT_FIELDS] & L4_DEBUG_PBF_Q_MASK) >> L4_DEBUG_PBF_Q_SHIFT)
#define L4_DEBUG_GET_VOQ(_data) 		    ((_data[L4_DEBUG_BIT_FIELDS] & L4_DEBUG_VOQ_MASK) >> L4_DEBUG_VOQ_SHIFT)
#define L4_DEBUG_GET_FLAGS(_data)           (_data[L4_DEBUG_FLAGS])

#define L4_DEBUG_SET_COS(_data, cos) 		\
    (_data[L4_DEBUG_BIT_FIELDS] &= (~L4_DEBUG_COS_MASK)); \
    (_data[L4_DEBUG_BIT_FIELDS] |= ((cos << L4_DEBUG_COS_SHIFT) & L4_DEBUG_COS_MASK))

#define L4_DEBUG_SET_PBFQUEUE(_data, pbf_q) \
    (_data[L4_DEBUG_BIT_FIELDS] &= (~L4_DEBUG_PBF_Q_MASK)); \
    (_data[L4_DEBUG_BIT_FIELDS] |= ((pbf_q << L4_DEBUG_PBF_Q_SHIFT) & L4_DEBUG_PBF_Q_MASK))

#define L4_DEBUG_SET_VOQ(_data, voq)        \
    (_data[L4_DEBUG_BIT_FIELDS] &= (~L4_DEBUG_VOQ_MASK)); \
    (_data[L4_DEBUG_BIT_FIELDS] |= ((voq << L4_DEBUG_VOQ_SHIFT) & L4_DEBUG_VOQ_MASK))

#define L4_DEBUG_SET_FLAGS(_data, flags) (_data[L4_DEBUG_FLAGS] |= flags)
			


#endif /* _L4DEBUG_H */

