#ifndef ECORE_INIT_H
#define ECORE_INIT_H

/* RAM0 size in bytes */
#define STORM_INTMEM_SIZE_E1		0x8000
#define STORM_INTMEM_SIZE_E1H		0x10000
#define STORM_INTMEM_SIZE_E2		0x10000

#define STORM_INTMEM_SIZE(bp) ((CHIP_IS_E1(bp) ? STORM_INTMEM_SIZE_E1 : \
				CHIP_IS_E1H(bp) ? STORM_INTMEM_SIZE_E1H : \
						  STORM_INTMEM_SIZE_E2) / 4)


/* Init operation types and structures */
/* Common for both E1 and E1H */
enum {
	OP_RD = 0x1,	/* read a single register */
	OP_WR,		/* write a single register */
	OP_IW,		/* write a single register using mailbox */
	OP_SW,		/* copy a string to the device */
	OP_SI,		/* copy a string using mailbox */
	OP_ZR,		/* clear memory */
	OP_ZP,		/* unzip then copy with DMAE */
	OP_WR_64,	/* write 64 bit pattern */
	OP_WB,		/* copy a string using DMAE */
#ifndef FW_ZIP_SUPPORT
    OP_FW,      /* copy an array from fw data (only used with unzipped FW) */
#endif
	OP_WB_ZR,	/* Clear a string using DMAE or indirect-wr */
	OP_LAST_GEN = OP_WB_ZR,
	OP_WR_EMUL,	/* write a single register on Emulation */
	OP_WR_FPGA,	/* write a single register on FPGA */
	OP_WR_ASIC,	/* write a single register on ASIC */
};

/* FPGA and EMUL specific operations */


/* Init stages */
/* Never reorder stages !!! */
enum {
	COMMON_STAGE,
	PORT0_STAGE,
	PORT1_STAGE,
	FUNC0_STAGE,
	FUNC1_STAGE,
	FUNC2_STAGE,
	FUNC3_STAGE,
	FUNC4_STAGE,
	FUNC5_STAGE,
	FUNC6_STAGE,
	FUNC7_STAGE,
	STAGE_IDX_MAX,
};

enum {
	STAGE_START,
	STAGE_END,
};

/* Indices of blocks */
enum {
	PRS_BLOCK,
	SRCH_BLOCK,
	TSDM_BLOCK,
	TCM_BLOCK,
	BRB1_BLOCK,
	TSEM_BLOCK,
	PXPCS_BLOCK,
	EMAC0_BLOCK,
	EMAC1_BLOCK,
	DBU_BLOCK,
	MISC_BLOCK,
	DBG_BLOCK,
	NIG_BLOCK,
	MCP_BLOCK,
	UPB_BLOCK,
	CSDM_BLOCK,
	USDM_BLOCK,
	CCM_BLOCK,
	UCM_BLOCK,
	USEM_BLOCK,
	CSEM_BLOCK,
	XPB_BLOCK,
	DQ_BLOCK,
	TIMERS_BLOCK,
	XSDM_BLOCK,
	QM_BLOCK,
	PBF_BLOCK,
	XCM_BLOCK,
	XSEM_BLOCK,
	CDU_BLOCK,
	DMAE_BLOCK,
	PXP_BLOCK,
	CFC_BLOCK,
	HC_BLOCK,
	PXP2_BLOCK,
	MISC_AEU_BLOCK,
	PGLUE_B_BLOCK,
	IGU_BLOCK,
	ATC_BLOCK,
	QM_4PORT_BLOCK,
	XSEM_4PORT_BLOCK,
};

/* Returns the index of start or end of a specific block stage in ops array*/
#define BLOCK_OPS_IDX(block, stage, end) \
			(2*(((block)*STAGE_IDX_MAX) + (stage)) + (end))


struct raw_op {
	u32 op:8;
	u32 offset:24;
	u32 raw_data;
};

struct op_read {
	u32 op:8;
	u32 offset:24;
	u32 pad;
};

struct op_write {
	u32 op:8;
	u32 offset:24;
	u32 val;
};

struct op_string_write {
	u32 op:8;
	u32 offset:24;
#ifdef __LITTLE_ENDIAN
	u16 data_off;
	u16 data_len;
#else /* __BIG_ENDIAN */
	u16 data_len;
	u16 data_off;
#endif
};

struct op_zero {
	u32 op:8;
	u32 offset:24;
	u32 len;
};

union init_op {
	struct op_read		read;
	struct op_write		write;
	struct op_string_write	str_wr;
	struct op_zero		zero;
	struct raw_op		raw;
};

#define INITOP_SET		0	/* set the HW directly */
#define INITOP_CLEAR		1	/* clear the HW directly */
#define INITOP_INIT		2	/* set the init-value array */

/****************************************************************************
* ILT management
****************************************************************************/
struct ilt_line {
	lm_address_t page_mapping;
	void *page;
	u32 size;
};

struct ilt_client_info {
	u32 page_size;
	u16 start;
	u16 end;
	u16 client_num;
	u16 flags;
#define ILT_CLIENT_SKIP_INIT	0x1
#define ILT_CLIENT_SKIP_MEM	0x2
};

struct ecore_ilt {
	u32 start_line;
	struct ilt_line		*lines;
	struct ilt_client_info 	clients[4];
#define ILT_CLIENT_CDU	0
#define ILT_CLIENT_QM	1
#define ILT_CLIENT_SRC	2
#define ILT_CLIENT_TM	3
};

/****************************************************************************
* SRC configuration
****************************************************************************/
struct src_ent {
	u8 opaque[56];
	u64 next;
};

/****************************************************************************
* Parity configuration
****************************************************************************/
#define BLOCK_PRTY_INFO(block, en_mask, m1, m1h, m2) \
{ \
	block##_REG_##block##_PRTY_MASK, \
	block##_REG_##block##_PRTY_STS_CLR, \
	en_mask, {m1, m1h, m2}, #block \
}

#define BLOCK_PRTY_INFO_0(block, en_mask, m1, m1h, m2) \
{ \
	block##_REG_##block##_PRTY_MASK_0, \
	block##_REG_##block##_PRTY_STS_CLR_0, \
	en_mask, {m1, m1h, m2}, #block"_0" \
}

#define BLOCK_PRTY_INFO_1(block, en_mask, m1, m1h, m2) \
{ \
	block##_REG_##block##_PRTY_MASK_1, \
	block##_REG_##block##_PRTY_STS_CLR_1, \
	en_mask, {m1, m1h, m2}, #block"_1" \
}

static const struct {
	u32 mask_addr;
	u32 sts_clr_addr;
	u32 en_mask;		/* Mask to enable parity attentions */
	struct {
		u32 e1;		/* 57710 */
		u32 e1h;	/* 57711 */
		u32 e2;		/* 57712 */
	} reg_mask;		/* Register mask (all valid bits) */
	char name[7];		/* Block's longest name is 6 characters long
				 * (name + suffix)
				 */
} ecore_blocks_parity_data[] = {
	/* bit 19 masked */
	/* REG_WR(bp, PXP_REG_PXP_PRTY_MASK, 0x80000); */
	/* bit 5,18,20-31 */
	/* REG_WR(bp, PXP2_REG_PXP2_PRTY_MASK_0, 0xfff40020); */
	/* bit 5 */
	/* REG_WR(bp, PXP2_REG_PXP2_PRTY_MASK_1, 0x20);	*/
	/* REG_WR(bp, HC_REG_HC_PRTY_MASK, 0x0); */
	/* REG_WR(bp, MISC_REG_MISC_PRTY_MASK, 0x0); */

	/* Block IGU, MISC, PXP and PXP2 parity errors as long as we don't
	 * want to handle "system kill" flow at the moment.
	 */
	BLOCK_PRTY_INFO(PXP, 0x7ffffff, 0x3ffffff, 0x3ffffff, 0x7ffffff),
	BLOCK_PRTY_INFO_0(PXP2,	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(PXP2,	0x7ff, 0x7f, 0x7f, 0x7ff),
	BLOCK_PRTY_INFO(HC, 0x7, 0x7, 0x7, 0),
	BLOCK_PRTY_INFO(NIG, 0xffffffff, 0x3fffffff, 0xffffffff, 0),
	BLOCK_PRTY_INFO_0(NIG,	0xffffffff, 0, 0, 0xffffffff),
	BLOCK_PRTY_INFO_1(NIG,	0xffff, 0, 0, 0xffff),
	BLOCK_PRTY_INFO(IGU, 0x7ff, 0, 0, 0x7ff),
	BLOCK_PRTY_INFO(MISC, 0x1, 0x1, 0x1, 0x1),
	BLOCK_PRTY_INFO(QM, 0, 0x1ff, 0xfff, 0xfff),
	BLOCK_PRTY_INFO(DORQ, 0, 0x3, 0x3, 0x3),
	{GRCBASE_UPB + PB_REG_PB_PRTY_MASK,
	/* Temp until FCoE fix the parity error they have */
		GRCBASE_UPB + PB_REG_PB_PRTY_STS_CLR, 0xf,
		{0xf, 0xf, 0xf}, "UPB"},
	{GRCBASE_XPB + PB_REG_PB_PRTY_MASK,
		GRCBASE_XPB + PB_REG_PB_PRTY_STS_CLR, 0,
		{0xf, 0xf, 0xf}, "XPB"},
	BLOCK_PRTY_INFO(SRC, 0x4, 0x7, 0x7, 0x7),
	BLOCK_PRTY_INFO(CDU, 0, 0x1f, 0x1f, 0x1f),
	BLOCK_PRTY_INFO(CFC, 0, 0xf, 0xf, 0xf),
	BLOCK_PRTY_INFO(DBG, 0, 0x1, 0x1, 0x1),
	BLOCK_PRTY_INFO(DMAE, 0, 0xf, 0xf, 0xf),
	BLOCK_PRTY_INFO(BRB1, 0, 0xf, 0xf, 0xf),
	BLOCK_PRTY_INFO(PRS, (1<<6), 0xff, 0xff, 0xff),
	BLOCK_PRTY_INFO(PBF, 0, 0, 0x3ffff, 0xfffffff),
	BLOCK_PRTY_INFO(TM, 0, 0, 0x7f, 0x7f),
	BLOCK_PRTY_INFO(TSDM, 0x18, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(CSDM, 0x8, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(USDM, 0x38, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(XSDM, 0x8, 0x7ff, 0x7ff, 0x7ff),
	BLOCK_PRTY_INFO(TCM, 0, 0, 0x7ffffff, 0x7ffffff),
	BLOCK_PRTY_INFO(CCM, 0, 0, 0x7ffffff, 0x7ffffff),
	BLOCK_PRTY_INFO(UCM, 0, 0, 0x7ffffff, 0x7ffffff),
	BLOCK_PRTY_INFO(XCM, 0, 0, 0x3fffffff, 0x3fffffff),
	BLOCK_PRTY_INFO_0(TSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(TSEM, 0, 0x3, 0x1f, 0x3f),
	BLOCK_PRTY_INFO_0(USEM, 0, 0xffffffff, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(USEM, 0, 0x3, 0x1f, 0x1f),
	BLOCK_PRTY_INFO_0(CSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(CSEM, 0, 0x3, 0x1f, 0x1f),
	BLOCK_PRTY_INFO_0(XSEM, 0, 0xffffffff, 0xffffffff, 0xffffffff),
	BLOCK_PRTY_INFO_1(XSEM, 0, 0x3, 0x1f, 0x3f),
};


/* [28] MCP Latched rom_parity
 * [29] MCP Latched ump_rx_parity
 * [30] MCP Latched ump_tx_parity
 * [31] MCP Latched scpad_parity
 */
#define MISC_AEU_ENABLE_MCP_PRTY_BITS	\
	(AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY | \
	 AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY)

/* Below registers control the MCP parity attention output. When
 * MISC_AEU_ENABLE_MCP_PRTY_BITS are set - attentions are
 * enabled, when cleared - disabled.
 */
static const u32 mcp_attn_ctl_regs[] = {
	MISC_REG_AEU_ENABLE4_FUNC_0_OUT_0,
	MISC_REG_AEU_ENABLE4_NIG_0,
	MISC_REG_AEU_ENABLE4_PXP_0,
	MISC_REG_AEU_ENABLE4_FUNC_1_OUT_0,
	MISC_REG_AEU_ENABLE4_NIG_1,
	MISC_REG_AEU_ENABLE4_PXP_1
};

static __inline void ecore_set_mcp_parity(struct _lm_device_t *pdev, u8 enable)
{
	int i;
	u32 reg_val;

	for (i = 0; i < ARRSIZE(mcp_attn_ctl_regs); i++) {
		reg_val = REG_RD(pdev, mcp_attn_ctl_regs[i]);

		if (enable)
			reg_val |= MISC_AEU_ENABLE_MCP_PRTY_BITS;
		else
			reg_val &= ~MISC_AEU_ENABLE_MCP_PRTY_BITS;

		REG_WR(pdev, mcp_attn_ctl_regs[i], reg_val);
	}
}

static __inline u32 ecore_parity_reg_mask(struct _lm_device_t *pdev, int idx)
{
	if (CHIP_IS_E1(pdev))
		return ecore_blocks_parity_data[idx].reg_mask.e1;
	else if (CHIP_IS_E1H(pdev))
		return ecore_blocks_parity_data[idx].reg_mask.e1h;
	else
		return ecore_blocks_parity_data[idx].reg_mask.e2;
}

static __inline void ecore_disable_blocks_parity(struct _lm_device_t *pdev)
{
	int i;

	for (i = 0; i < ARRSIZE(ecore_blocks_parity_data); i++) {
		u32 dis_mask = ecore_parity_reg_mask(pdev, i);

		if (dis_mask) {
			REG_WR(pdev, ecore_blocks_parity_data[i].mask_addr,
			       dis_mask);
			DbgMessage2(pdev, WARNi, "Setting parity mask "
						 "for %s to\t\t0x%x\n",
				    ecore_blocks_parity_data[i].name, dis_mask);
		}
	}

	/* Disable MCP parity attentions */
	ecore_set_mcp_parity(pdev, FALSE);
}

/**
 * Clear the parity error status registers.
 */
static __inline void ecore_clear_blocks_parity(struct _lm_device_t *pdev)
{
	int i;
	u32 reg_val, mcp_aeu_bits =
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_ROM_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_SCPAD_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_RX_PARITY |
		AEU_INPUTS_ATTN_BITS_MCP_LATCHED_UMP_TX_PARITY;

	/* Clear SEM_FAST parities */
	REG_WR(pdev, XSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(pdev, TSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(pdev, USEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);
	REG_WR(pdev, CSEM_REG_FAST_MEMORY + SEM_FAST_REG_PARITY_RST, 0x1);

	for (i = 0; i < ARRSIZE(ecore_blocks_parity_data); i++) {
		u32 reg_mask = ecore_parity_reg_mask(pdev, i);

		if (reg_mask) {
			reg_val = REG_RD(pdev, ecore_blocks_parity_data[i].
					 sts_clr_addr);
			if (reg_val & reg_mask)
				DbgMessage2(pdev, WARNi,
					    "Parity errors in %s: 0x%x\n",
					    ecore_blocks_parity_data[i].name,
					    reg_val & reg_mask);
		}
	}

	/* Check if there were parity attentions in MCP */
	reg_val = REG_RD(pdev, MISC_REG_AEU_AFTER_INVERT_4_MCP);
	if (reg_val & mcp_aeu_bits)
		DbgMessage1(pdev, WARNi, "Parity error in MCP: 0x%x\n",
		   reg_val & mcp_aeu_bits);

	/* Clear parity attentions in MCP:
	 * [7]  clears Latched rom_parity
	 * [8]  clears Latched ump_rx_parity
	 * [9]  clears Latched ump_tx_parity
	 * [10] clears Latched scpad_parity (both ports)
	 */
	REG_WR(pdev, MISC_REG_AEU_CLR_LATCH_SIGNAL, 0x780);
}

static __inline void ecore_enable_blocks_parity(struct _lm_device_t *pdev)
{
	int i;

	for (i = 0; i < ARRSIZE(ecore_blocks_parity_data); i++) {
		u32 reg_mask = ecore_parity_reg_mask(pdev, i);

		if (reg_mask)
			REG_WR(pdev, ecore_blocks_parity_data[i].mask_addr,
				ecore_blocks_parity_data[i].en_mask & reg_mask);
	}

	/* Enable MCP parity attentions */
	ecore_set_mcp_parity(pdev, TRUE);
}


#endif /* ECORE_INIT_H */

