/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 QLogic Corporation. All rights reserved.
 */

#ifndef _QLCNIC_HW_REGS_H_
#define	_QLCNIC_HW_REGS_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 *		PCI related defines.
 */

/*
 * Interrupt related defines.
 */
#define	PCIX_TARGET_STATUS	(0x10118)
#define	PCIX_TARGET_STATUS_F1	(0x10160)
#define	PCIX_TARGET_STATUS_F2	(0x10164)
#define	PCIX_TARGET_STATUS_F3	(0x10168)
#define	PCIX_TARGET_STATUS_F4	(0x10360)
#define	PCIX_TARGET_STATUS_F5	(0x10364)
#define	PCIX_TARGET_STATUS_F6	(0x10368)
#define	PCIX_TARGET_STATUS_F7	(0x1036c)

#define	PCIX_TARGET_MASK	(0x10128)
#define	PCIX_TARGET_MASK_F1	(0x10170)
#define	PCIX_TARGET_MASK_F2	(0x10174)
#define	PCIX_TARGET_MASK_F3	(0x10178)
#define	PCIX_TARGET_MASK_F4	(0x10370)
#define	PCIX_TARGET_MASK_F5	(0x10374)
#define	PCIX_TARGET_MASK_F6	(0x10378)
#define	PCIX_TARGET_MASK_F7	(0x1037c)

/*
 * Message Signaled Interrupts
 */
#define	PCIX_MSI_F0		(0x13000)
#define	PCIX_MSI_F1		(0x13004)
#define	PCIX_MSI_F2		(0x13008)
#define	PCIX_MSI_F3		(0x1300c)
#define	PCIX_MSI_F4		(0x13010)
#define	PCIX_MSI_F5		(0x13014)
#define	PCIX_MSI_F6		(0x13018)
#define	PCIX_MSI_F7		(0x1301c)
#define	PCIX_MSI_F(FUNC)	(0x13000 +((FUNC) * 4))

/*
 *
 */
#define	PCIX_INT_VECTOR		(0x10100)
#define	PCIX_INT_MASK		(0x10104)

/*
 * Interrupt state machine and other bits.
 */
#define	PCIE_MISCCFG_RC		(0x1206c)


#define	ISR_INT_TARGET_STATUS	  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS))
#define	ISR_INT_TARGET_STATUS_F1  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F1))
#define	ISR_INT_TARGET_STATUS_F2  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F2))
#define	ISR_INT_TARGET_STATUS_F3  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F3))
#define	ISR_INT_TARGET_STATUS_F4  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F4))
#define	ISR_INT_TARGET_STATUS_F5  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F5))
#define	ISR_INT_TARGET_STATUS_F6  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F6))
#define	ISR_INT_TARGET_STATUS_F7  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_STATUS_F7))

#define	ISR_INT_TARGET_MASK	  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK))
#define	ISR_INT_TARGET_MASK_F1	  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F1))
#define	ISR_INT_TARGET_MASK_F2	  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F2))
#define	ISR_INT_TARGET_MASK_F3	  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F3))
#define	ISR_INT_TARGET_MASK_F4	  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F4))
#define	ISR_INT_TARGET_MASK_F5	  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F5))
#define	ISR_INT_TARGET_MASK_F6	  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F6))
#define	ISR_INT_TARGET_MASK_F7	  (QLCNIC_PCIX_PS_REG(PCIX_TARGET_MASK_F7))

#define	ISR_INT_VECTOR		  (QLCNIC_PCIX_PS_REG(PCIX_INT_VECTOR))
#define	ISR_INT_MASK		  (QLCNIC_PCIX_PS_REG(PCIX_INT_MASK))
#define	ISR_INT_STATE_REG	  (QLCNIC_PCIX_PS_REG(PCIE_MISCCFG_RC))

#define	ISR_MSI_INT_TRIGGER(FUNC) (QLCNIC_PCIX_PS_REG(PCIX_MSI_F(FUNC)))


#define	ISR_IS_LEGACY_INTR_IDLE(VAL)		(((VAL) & 0x300) == 0)
#define	ISR_IS_LEGACY_INTR_TRIGGERED(VAL)	(((VAL) & 0x300) == 0x200)

/*
 * PCI Interrupt Vector Values.
 */
#define	PCIX_INT_VECTOR_BIT_F0	0x0080
#define	PCIX_INT_VECTOR_BIT_F1	0x0100
#define	PCIX_INT_VECTOR_BIT_F2	0x0200
#define	PCIX_INT_VECTOR_BIT_F3	0x0400
#define	PCIX_INT_VECTOR_BIT_F4	0x0800
#define	PCIX_INT_VECTOR_BIT_F5	0x1000
#define	PCIX_INT_VECTOR_BIT_F6	0x2000
#define	PCIX_INT_VECTOR_BIT_F7	0x4000

struct qlcnic_legacy_intr_set {
	uint32_t	int_vec_bit;
	uint32_t	tgt_status_reg;
	uint32_t	tgt_mask_reg;
	uint32_t	pci_int_reg;
};

#define	QLCNIC_LEGACY_INTR_CONFIG			\
{						\
	{	PCIX_INT_VECTOR_BIT_F0,		\
		ISR_INT_TARGET_STATUS,		\
		ISR_INT_TARGET_MASK,		\
		ISR_MSI_INT_TRIGGER(0) },	\
						\
	{	PCIX_INT_VECTOR_BIT_F1,		\
		ISR_INT_TARGET_STATUS_F1,	\
		ISR_INT_TARGET_MASK_F1,		\
		ISR_MSI_INT_TRIGGER(1) },	\
						\
	{	PCIX_INT_VECTOR_BIT_F2,		\
		ISR_INT_TARGET_STATUS_F2,	\
		ISR_INT_TARGET_MASK_F2,		\
		ISR_MSI_INT_TRIGGER(2) },	\
						\
	{	PCIX_INT_VECTOR_BIT_F3,		\
		ISR_INT_TARGET_STATUS_F3,	\
		ISR_INT_TARGET_MASK_F3,		\
		ISR_MSI_INT_TRIGGER(3) },	\
						\
	{	PCIX_INT_VECTOR_BIT_F4,		\
		ISR_INT_TARGET_STATUS_F4,	\
		ISR_INT_TARGET_MASK_F4,		\
		ISR_MSI_INT_TRIGGER(4) },	\
						\
	{	PCIX_INT_VECTOR_BIT_F5,		\
		ISR_INT_TARGET_STATUS_F5,	\
		ISR_INT_TARGET_MASK_F5,		\
		ISR_MSI_INT_TRIGGER(5) },	\
						\
	{	PCIX_INT_VECTOR_BIT_F6,		\
		ISR_INT_TARGET_STATUS_F6,	\
		ISR_INT_TARGET_MASK_F6,		\
		ISR_MSI_INT_TRIGGER(6) },	\
						\
	{	PCIX_INT_VECTOR_BIT_F7,		\
		ISR_INT_TARGET_STATUS_F7,	\
		ISR_INT_TARGET_MASK_F7,		\
		ISR_MSI_INT_TRIGGER(7) },	\
}

#ifdef __cplusplus
}
#endif

#endif /* !_QLCNIC_HW_REGS_H_ */
