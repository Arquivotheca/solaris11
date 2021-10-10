/*******************************************************************************
 * The information contained in this file is confidential and proprietary to
 * Broadcom Corporation.  No part of this file may be reproduced or
 * distributed, in any form or by any means for any purpose, without the
 * express written permission of Broadcom Corporation.
 *
 * (c) COPYRIGHT 2001-2007 Broadcom Corporation, ALL RIGHTS RESERVED.
 *
 *
 * Module Description:
 *  This file defines the IDLE_CHK macros
 *
 * History:
 *    11/02/08 Miri Shitrit    Inception. 
 ******************************************************************************/

#ifndef _LM_DEBUG_H
#define _LM_DEBUG_H

// bits must be corralted to the values in idle_chk.csv
#define IDLE_CHK_CHIP_MASK_57710     0x01
#define IDLE_CHK_CHIP_MASK_57711     0x02
#define IDLE_CHK_CHIP_MASK_57731     0x04
// Added for E3
#define IDLE_CHK_CHIP_MASK_57773     0x08

enum {
    IDLE_CHK_ERROR = 1,
    IDLE_CHK_ERROR_NO_TRAFFIC, // indicates an error if test is not under traffic
    IDLE_CHK_WARNING     
} idle_chk_error_level;


#define CONDITION_CHK(condition, severity, fail_msg) \
        total++; \
		var_severity = severity; \
        if (condition) { \
            switch (var_severity) { \
                case IDLE_CHK_ERROR: \
                    DbgMessage2(pdev, FATAL, "idle_chk. Error   (level %d): %s\n", severity, fail_msg); \
                    errors++; \
                    break; \
                case IDLE_CHK_ERROR_NO_TRAFFIC: \
                    DbgMessage2(pdev, FATAL, "idle_chk. Error if no traffic (level %d):   %s\n", severity, fail_msg); \
                    errors++; \
                    break; \
                case IDLE_CHK_WARNING: \
                    DbgMessage2(pdev, WARN, "idle_chk. Warning (level %d): %s\n", severity, fail_msg); \
                    warnings++; \
                    break; \
            }\
        }


#define IDLE_CHK_CHIP_MASK_CHK(chip_mask) \
        b_test_chip=0; \
		var_chip_mask = 0; \
        val = REG_RD(pdev, MISC_REG_CHIP_NUM); \
        if (val == 5710) { \
            var_chip_mask = IDLE_CHK_CHIP_MASK_57710; \
        } else if (val == 5711 || val == 5712) { \
            var_chip_mask = IDLE_CHK_CHIP_MASK_57711; \
        } else if ((val == 5713) || (val == 5714) || (val == 5730) || (val == 5731))  { \
            var_chip_mask =  IDLE_CHK_CHIP_MASK_57731; \
        } else if ((val == 5773) || (val == 5774) || (val == 5770)) { \
            var_chip_mask =  IDLE_CHK_CHIP_MASK_57773; \
        } \
        if (var_chip_mask & chip_mask) { \
            b_test_chip = 1;\
        }

/* read one reg and check the condition */
#define IDLE_CHK_1(chip_mask, offset, condition, severity, fail_msg) \
        IDLE_CHK_CHIP_MASK_CHK(chip_mask); \
        if (b_test_chip) { \
            val = REG_RD(pdev, offset); \
            sprintf (prnt_str, "%s. Value is 0x%x\n", fail_msg, val); \
            CONDITION_CHK(condition, severity, prnt_str); \
        }

/* loop to read one reg and check the condition */
#define IDLE_CHK_2(chip_mask, offset, loop, inc, condition, severity, fail_msg) \
        IDLE_CHK_CHIP_MASK_CHK(chip_mask); \
        if (b_test_chip) { \
            for (i = 0; i < (loop); i++) { \
                val = REG_RD(pdev, offset + i*(inc)); \
                sprintf (prnt_str, "%s. Value is 0x%x\n", fail_msg, val); \
                CONDITION_CHK(condition, severity, prnt_str); \
            } \
        }

/* read two regs and check the condition */
#define IDLE_CHK_3(chip_mask, offset1, offset2, condition, severity, fail_msg) \
         IDLE_CHK_CHIP_MASK_CHK(chip_mask); \
         if (b_test_chip) { \
            val1 = REG_RD(pdev, offset1); \
            val2 = REG_RD(pdev, offset2); \
            sprintf (prnt_str, "%s. Values are 0x%x 0x%x\n", fail_msg, val1, val2); \
            CONDITION_CHK(condition, severity, prnt_str); \
         }

/* read one reg and check according to CID_CAM */
#define IDLE_CHK_4(chip_mask, offset1, offset2, loop, inc, condition, severity, fail_msg) \
        IDLE_CHK_CHIP_MASK_CHK(chip_mask); \
        if (b_test_chip) { \
            for (i = 0; i < (loop); i++) { \
                val1 = REG_RD(pdev, (offset1 + i*inc)); \
                val2 = REG_RD(pdev, (offset2 + i*(inc))); \
				val2 = val2 >> 1; \
                sprintf (prnt_str, "%s LCID %d CID_CAM 0x%x. Value is 0x%x\n", fail_msg, i, val2, val1);\
                CONDITION_CHK(condition, severity, prnt_str); \
            } \
        }


/* read one reg and check according to another reg */
#define IDLE_CHK_5(chip_mask, offset, offset1, offset2, condition, severity, fail_msg) \
        IDLE_CHK_CHIP_MASK_CHK(chip_mask); \
        if (b_test_chip) { \
            val = REG_RD(pdev, offset);\
            if (!val) \
                IDLE_CHK_3(chip_mask, offset1, offset2, condition, severity, fail_msg); \
        }

/* read wide-bus reg and check sub-fields */
#define IDLE_CHK_6(chip_mask, offset, loop, inc, severity) \
     { \
        u32 rd_ptr, wr_ptr, rd_bank, wr_bank; \
        IDLE_CHK_CHIP_MASK_CHK(chip_mask); \
        if (b_test_chip) { \
            for (i = 0; i < (loop); i++) { \
                val1 = REG_RD(pdev, offset + i*(inc)); \
                val2 = REG_RD(pdev, offset + i*(inc) + 4); \
                rd_ptr = ((val1 & 0x3FFFFFC0) >> 6); \
                wr_ptr = ((((val1 & 0xC0000000) >> 30) & 0x3) | ((val2 & 0x3FFFFF) << 2)); \
                sprintf (prnt_str, "QM: PTRTBL entry %d- rd_ptr is not equal to wr_ptr. Values are 0x%x 0x%x\n", i, rd_ptr, wr_ptr);\
                CONDITION_CHK((rd_ptr != wr_ptr), severity, prnt_str);\
                rd_bank = ((val1 & 0x30) >> 4); \
                wr_bank = (val1 & 0x03); \
                sprintf (prnt_str, "QM: PTRTBL entry %d- rd_bank is not equal to wr_bank. Values are 0x%x 0x%x\n", i, rd_bank, wr_bank); \
                CONDITION_CHK((rd_bank != wr_bank), severity, prnt_str); \
            } \
        } \
      }


/* loop to read wide-bus reg and check according to another reg */
#define IDLE_CHK_7(chip_mask, offset, offset1, offset2, loop, inc, condition, severity, fail_msg) \
	{ \
	    u32_t chip_num; \
		IDLE_CHK_CHIP_MASK_CHK(chip_mask); \
		if (b_test_chip) { \
			for (i = 0; i < (loop); i++) { \
				val = REG_RD(pdev, offset + i*(inc)); \
				if (val == 1) { \
					chip_num = REG_RD(pdev , MISC_REG_CHIP_NUM); \
					if ((chip_num == 0x1662) || (chip_num == 0x1663) || (chip_num == 0x1651) || (chip_num == 0x1652)) { \
						val1 = REG_RD(pdev, offset1 + i*(inc)); \
						val1 = REG_RD(pdev, offset1 + i*(inc) + 4); \
						val1 = REG_RD(pdev, offset1 + i*(inc) + 8); \
						REG_RD(pdev, offset1 + i*(inc) + 12); \
						val1 = (val1 & 0x1E000000) >> 25; \
					} else { \
						val1 = REG_RD(pdev, offset1 + i*(inc)); \
						val1 = REG_RD(pdev, offset1 + i*(inc) + 4); \
						val1 = REG_RD(pdev, offset1 + i*(inc) + 8); \
						val1 = (val1 & 0x00000078) >> 3; \
					} \
					val2 = REG_RD(pdev, offset2 + i*4); \
					val2 = (val2 >> 1); \
					sprintf (prnt_str, "%s - LCID %d CID_CAM 0x%x. Value is 0x%x\n", fail_msg, i, val2, val1); \
					CONDITION_CHK(condition, severity, prnt_str); \
				} \
			} \
		} \
	}

/* check PXP VQ occupancy according to condition */
#define IDLE_CHK_8(chip_mask, offset, condition, severity, fail_msg) \
        IDLE_CHK_CHIP_MASK_CHK(chip_mask); \
        if (b_test_chip) { \
            val = REG_RD(pdev, offset); \
            total++; \
            if (condition) { \
                sprintf (prnt_str, "%s. Value is 0x%x\n%s\n", fail_msg, val,_vq_hoq(pdev,#offset)); \
                CONDITION_CHK(1, severity, prnt_str); \
            } \
        }

#endif// _LM_DEBUG_H

