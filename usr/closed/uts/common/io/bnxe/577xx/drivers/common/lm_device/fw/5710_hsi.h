#ifndef __5710_HSI_VBD__ 
#define __5710_HSI_VBD__ 

/*
 * attention bits $$KEEP_ENDIANNESS$$
 */
struct atten_sp_status_block
{
	u32_t attn_bits /* 16 bit of attention signal lines */;
	u32_t attn_bits_ack /* 16 bit of attention signal ack */;
	u8_t status_block_id /* status block id */;
	u8_t reserved0 /* resreved for padding */;
	u16_t attn_bits_index /* attention bits running index */;
	u32_t reserved1 /* resreved for padding */;
};


/*
 * The eth aggregative context of Cstorm
 */
struct cstorm_eth_ag_context
{
	u32_t __reserved0[10];
};


/*
 * The iscsi aggregative context of Cstorm
 */
struct cstorm_iscsi_ag_context
{
	u32_t agg_vars1;
		#define CSTORM_ISCSI_AG_CONTEXT_STATE                                                (0xFF<<0) /* BitField agg_vars1 Various aggregative variables	The state of the connection */
		#define CSTORM_ISCSI_AG_CONTEXT_STATE_SHIFT                                          0
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                      (0x1<<8) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                8
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                      (0x1<<9) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                9
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                      (0x1<<10) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                10
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                      (0x1<<11) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define __CSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                11
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_SE_CF_EN                           (0x1<<12) /* BitField agg_vars1 Various aggregative variables	ULP Rx SE counter flag enable */
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_SE_CF_EN_SHIFT                     12
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_INV_CF_EN                          (0x1<<13) /* BitField agg_vars1 Various aggregative variables	ULP Rx invalidate counter flag enable */
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED_ULP_RX_INV_CF_EN_SHIFT                    13
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX4_CF                                            (0x3<<14) /* BitField agg_vars1 Various aggregative variables	Aux 4 counter flag */
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX4_CF_SHIFT                                      14
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED66                                         (0x3<<16) /* BitField agg_vars1 Various aggregative variables	The connection QOS */
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED66_SHIFT                                   16
		#define __CSTORM_ISCSI_AG_CONTEXT_FIN_RECEIVED_CF_EN                                 (0x1<<18) /* BitField agg_vars1 Various aggregative variables	Enable decision rule for fin_received_cf */
		#define __CSTORM_ISCSI_AG_CONTEXT_FIN_RECEIVED_CF_EN_SHIFT                           18
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN                                         (0x1<<19) /* BitField agg_vars1 Various aggregative variables	Enable decision rule for auxiliary counter flag 1 */
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN_SHIFT                                   19
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX2_CF_EN                                         (0x1<<20) /* BitField agg_vars1 Various aggregative variables	Enable decision rule for auxiliary counter flag 2 */
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX2_CF_EN_SHIFT                                   20
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN                                         (0x1<<21) /* BitField agg_vars1 Various aggregative variables	Enable decision rule for auxiliary counter flag 3 */
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN_SHIFT                                   21
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX4_CF_EN                                         (0x1<<22) /* BitField agg_vars1 Various aggregative variables	Enable decision rule for auxiliary counter flag 4 */
		#define __CSTORM_ISCSI_AG_CONTEXT_AUX4_CF_EN_SHIFT                                   22
		#define __CSTORM_ISCSI_AG_CONTEXT_REL_SEQ_RULE                                       (0x7<<23) /* BitField agg_vars1 Various aggregative variables	0-NOP, 1-EQ, 2-NEQ, 3-GT, 4-GE, 5-LS, 6-LE */
		#define __CSTORM_ISCSI_AG_CONTEXT_REL_SEQ_RULE_SHIFT                                 23
		#define CSTORM_ISCSI_AG_CONTEXT_HQ_PROD_RULE                                         (0x3<<26) /* BitField agg_vars1 Various aggregative variables	0-NOP, 1-EQ, 2-NEQ */
		#define CSTORM_ISCSI_AG_CONTEXT_HQ_PROD_RULE_SHIFT                                   26
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED52                                         (0x3<<28) /* BitField agg_vars1 Various aggregative variables	0-NOP, 1-EQ, 2-NEQ */
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED52_SHIFT                                   28
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED53                                         (0x3<<30) /* BitField agg_vars1 Various aggregative variables	0-NOP, 1-EQ, 2-NEQ */
		#define __CSTORM_ISCSI_AG_CONTEXT_RESERVED53_SHIFT                                   30
#if defined(__BIG_ENDIAN)
	u8_t __aux1_th /* Aux1 threhsold for the decision */;
	u8_t __aux1_val /* Aux1 aggregation value */;
	u16_t __agg_vars2 /* Various aggregative variables */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_vars2 /* Various aggregative variables */;
	u8_t __aux1_val /* Aux1 aggregation value */;
	u8_t __aux1_th /* Aux1 threhsold for the decision */;
#endif
	u32_t rel_seq /* The sequence to release */;
	u32_t rel_seq_th /* The threshold for the released sequence */;
#if defined(__BIG_ENDIAN)
	u16_t hq_cons /* The HQ Consumer */;
	u16_t hq_prod /* The HQ producer */;
#elif defined(__LITTLE_ENDIAN)
	u16_t hq_prod /* The HQ producer */;
	u16_t hq_cons /* The HQ Consumer */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t __reserved62 /* Mask value for the decision algorithm of the general flags */;
	u8_t __reserved61 /* General flags */;
	u8_t __reserved60 /* ORQ consumer updated by the completor */;
	u8_t __reserved59 /* ORQ ULP Rx consumer */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __reserved59 /* ORQ ULP Rx consumer */;
	u8_t __reserved60 /* ORQ consumer updated by the completor */;
	u8_t __reserved61 /* General flags */;
	u8_t __reserved62 /* Mask value for the decision algorithm of the general flags */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __reserved64 /* RQ consumer kept by the completor */;
	u16_t cq_u_prod /* Ustorm producer of CQ */;
#elif defined(__LITTLE_ENDIAN)
	u16_t cq_u_prod /* Ustorm producer of CQ */;
	u16_t __reserved64 /* RQ consumer kept by the completor */;
#endif
	u32_t __cq_u_prod1 /* Ustorm producer of CQ 1 */;
#if defined(__BIG_ENDIAN)
	u16_t __agg_vars3 /* Various aggregative variables */;
	u16_t cq_u_pend /* Ustorm pending completions of CQ */;
#elif defined(__LITTLE_ENDIAN)
	u16_t cq_u_pend /* Ustorm pending completions of CQ */;
	u16_t __agg_vars3 /* Various aggregative variables */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __aux2_th /* Aux2 threhsold for the decision */;
	u16_t aux2_val /* Aux2 aggregation value */;
#elif defined(__LITTLE_ENDIAN)
	u16_t aux2_val /* Aux2 aggregation value */;
	u16_t __aux2_th /* Aux2 threhsold for the decision */;
#endif
};


/*
 * The toe aggregative context of Cstorm
 */
struct cstorm_toe_ag_context
{
	u32_t __agg_vars1 /* Various aggregative variables */;
#if defined(__BIG_ENDIAN)
	u8_t __aux1_th /* Aux1 threhsold for the decision */;
	u8_t __aux1_val /* Aux1 aggregation value */;
	u16_t __agg_vars2 /* Various aggregative variables */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_vars2 /* Various aggregative variables */;
	u8_t __aux1_val /* Aux1 aggregation value */;
	u8_t __aux1_th /* Aux1 threhsold for the decision */;
#endif
	u32_t rel_seq /* The sequence to release */;
	u32_t __rel_seq_threshold /* The threshold for the released sequence */;
#if defined(__BIG_ENDIAN)
	u16_t __reserved58 /* The HQ Consumer */;
	u16_t bd_prod /* The HQ producer */;
#elif defined(__LITTLE_ENDIAN)
	u16_t bd_prod /* The HQ producer */;
	u16_t __reserved58 /* The HQ Consumer */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t __reserved62 /* Mask value for the decision algorithm of the general flags */;
	u8_t __reserved61 /* General flags */;
	u8_t __reserved60 /* ORQ consumer updated by the completor */;
	u8_t __completion_opcode /* ORQ ULP Rx consumer */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __completion_opcode /* ORQ ULP Rx consumer */;
	u8_t __reserved60 /* ORQ consumer updated by the completor */;
	u8_t __reserved61 /* General flags */;
	u8_t __reserved62 /* Mask value for the decision algorithm of the general flags */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __reserved64 /* RQ consumer kept by the completor */;
	u16_t __reserved63 /* RQ consumer updated by the ULP RX */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __reserved63 /* RQ consumer updated by the ULP RX */;
	u16_t __reserved64 /* RQ consumer kept by the completor */;
#endif
	u32_t snd_max /* The ACK sequence number received in the last completed DDP */;
#if defined(__BIG_ENDIAN)
	u16_t __agg_vars3 /* Various aggregative variables */;
	u16_t __reserved67 /* A counter for the number of RQ WQEs with invalidate the the USTORM encountered */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __reserved67 /* A counter for the number of RQ WQEs with invalidate the the USTORM encountered */;
	u16_t __agg_vars3 /* Various aggregative variables */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __aux2_th /* Aux2 threhsold for the decision */;
	u16_t __aux2_val /* Aux2 aggregation value */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __aux2_val /* Aux2 aggregation value */;
	u16_t __aux2_th /* Aux2 threhsold for the decision */;
#endif
};


/*
 * dmae command structure
 */
struct dmae_cmd
{
	u32_t opcode;
		#define DMAE_CMD_SRC                                                                 (0x1<<0) /* BitField opcode 	Whether the source is the PCIe or the GRC. 0- The source is the PCIe 1- The source is the GRC. */
		#define DMAE_CMD_SRC_SHIFT                                                           0
		#define DMAE_CMD_DST                                                                 (0x3<<1) /* BitField opcode 	The destination of the DMA can be: 0-None 1-PCIe 2-GRC 3-None  */
		#define DMAE_CMD_DST_SHIFT                                                           1
		#define DMAE_CMD_C_DST                                                               (0x1<<3) /* BitField opcode 	The destination of the completion: 0-PCIe 1-GRC */
		#define DMAE_CMD_C_DST_SHIFT                                                         3
		#define DMAE_CMD_C_TYPE_ENABLE                                                       (0x1<<4) /* BitField opcode 	Whether to write a completion word to the completion destination: 0-Do not write a completion word 1-Write the completion word  */
		#define DMAE_CMD_C_TYPE_ENABLE_SHIFT                                                 4
		#define DMAE_CMD_C_TYPE_CRC_ENABLE                                                   (0x1<<5) /* BitField opcode 	Whether to write a CRC word to the completion destination 0-Do not write a CRC word 1-Write a CRC word  */
		#define DMAE_CMD_C_TYPE_CRC_ENABLE_SHIFT                                             5
		#define DMAE_CMD_C_TYPE_CRC_OFFSET                                                   (0x7<<6) /* BitField opcode 	The CRC word should be taken from the DMAE GRC space from address 9+X, where X is the value in these bits. */
		#define DMAE_CMD_C_TYPE_CRC_OFFSET_SHIFT                                             6
		#define DMAE_CMD_ENDIANITY                                                           (0x3<<9) /* BitField opcode 	swapping mode. */
		#define DMAE_CMD_ENDIANITY_SHIFT                                                     9
		#define DMAE_CMD_PORT                                                                (0x1<<11) /* BitField opcode 	Which network port ID to present to the PCI request interface */
		#define DMAE_CMD_PORT_SHIFT                                                          11
		#define DMAE_CMD_CRC_RESET                                                           (0x1<<12) /* BitField opcode 	reset crc result */
		#define DMAE_CMD_CRC_RESET_SHIFT                                                     12
		#define DMAE_CMD_SRC_RESET                                                           (0x1<<13) /* BitField opcode 	reset source address in next go */
		#define DMAE_CMD_SRC_RESET_SHIFT                                                     13
		#define DMAE_CMD_DST_RESET                                                           (0x1<<14) /* BitField opcode 	reset dest address in next go */
		#define DMAE_CMD_DST_RESET_SHIFT                                                     14
		#define DMAE_CMD_E1HVN                                                               (0x3<<15) /* BitField opcode 	vnic number E2 and onwards source vnic */
		#define DMAE_CMD_E1HVN_SHIFT                                                         15
		#define DMAE_CMD_DST_VN                                                              (0x3<<17) /* BitField opcode 	E2 and onwards dest vnic */
		#define DMAE_CMD_DST_VN_SHIFT                                                        17
		#define DMAE_CMD_C_FUNC                                                              (0x1<<19) /* BitField opcode 	E2 and onwards which function gets the completion src_vn(e1hvn)-0 dst_vn-1 */
		#define DMAE_CMD_C_FUNC_SHIFT                                                        19
		#define DMAE_CMD_ERR_POLICY                                                          (0x3<<20) /* BitField opcode 	E2 and onwards what to do when theres a completion and a PCI error regular-0 error indication-1 no completion-2 */
		#define DMAE_CMD_ERR_POLICY_SHIFT                                                    20
		#define DMAE_CMD_RESERVED0                                                           (0x3FF<<22) /* BitField opcode 	 */
		#define DMAE_CMD_RESERVED0_SHIFT                                                     22
	u32_t src_addr_lo /* source address low/grc address */;
	u32_t src_addr_hi /* source address hi */;
	u32_t dst_addr_lo /* dest address low/grc address */;
	u32_t dst_addr_hi /* dest address hi */;
#if defined(__BIG_ENDIAN)
	u16_t opcode_iov;
		#define DMAE_CMD_SRC_VFID                                                            (0x3F<<0) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	source VF id */
		#define DMAE_CMD_SRC_VFID_SHIFT                                                      0
		#define DMAE_CMD_SRC_VFPF                                                            (0x1<<6) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	selects the source function PF-0, VF-1 */
		#define DMAE_CMD_SRC_VFPF_SHIFT                                                      6
		#define DMAE_CMD_RESERVED1                                                           (0x1<<7) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	 */
		#define DMAE_CMD_RESERVED1_SHIFT                                                     7
		#define DMAE_CMD_DST_VFID                                                            (0x3F<<8) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	destination VF id */
		#define DMAE_CMD_DST_VFID_SHIFT                                                      8
		#define DMAE_CMD_DST_VFPF                                                            (0x1<<14) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	selects the destination function PF-0, VF-1 */
		#define DMAE_CMD_DST_VFPF_SHIFT                                                      14
		#define DMAE_CMD_RESERVED2                                                           (0x1<<15) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	 */
		#define DMAE_CMD_RESERVED2_SHIFT                                                     15
	u16_t len /* copy length */;
#elif defined(__LITTLE_ENDIAN)
	u16_t len /* copy length */;
	u16_t opcode_iov;
		#define DMAE_CMD_SRC_VFID                                                            (0x3F<<0) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	source VF id */
		#define DMAE_CMD_SRC_VFID_SHIFT                                                      0
		#define DMAE_CMD_SRC_VFPF                                                            (0x1<<6) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	selects the source function PF-0, VF-1 */
		#define DMAE_CMD_SRC_VFPF_SHIFT                                                      6
		#define DMAE_CMD_RESERVED1                                                           (0x1<<7) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	 */
		#define DMAE_CMD_RESERVED1_SHIFT                                                     7
		#define DMAE_CMD_DST_VFID                                                            (0x3F<<8) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	destination VF id */
		#define DMAE_CMD_DST_VFID_SHIFT                                                      8
		#define DMAE_CMD_DST_VFPF                                                            (0x1<<14) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	selects the destination function PF-0, VF-1 */
		#define DMAE_CMD_DST_VFPF_SHIFT                                                      14
		#define DMAE_CMD_RESERVED2                                                           (0x1<<15) /* BitField opcode_iov E2 and onward, set to 0 for backward compatibility	 */
		#define DMAE_CMD_RESERVED2_SHIFT                                                     15
#endif
	u32_t comp_addr_lo /* completion address low/grc address */;
	u32_t comp_addr_hi /* completion address hi */;
	u32_t comp_val /* value to write to completion address */;
	u32_t crc32 /* crc32 result */;
	u32_t crc32_c /* crc32_c result */;
#if defined(__BIG_ENDIAN)
	u16_t crc16_c /* crc16_c result */;
	u16_t crc16 /* crc16 result */;
#elif defined(__LITTLE_ENDIAN)
	u16_t crc16 /* crc16 result */;
	u16_t crc16_c /* crc16_c result */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t reserved3;
	u16_t crc_t10 /* crc_t10 result */;
#elif defined(__LITTLE_ENDIAN)
	u16_t crc_t10 /* crc_t10 result */;
	u16_t reserved3;
#endif
#if defined(__BIG_ENDIAN)
	u16_t xsum8 /* checksum8 result */;
	u16_t xsum16 /* checksum16 result */;
#elif defined(__LITTLE_ENDIAN)
	u16_t xsum16 /* checksum16 result */;
	u16_t xsum8 /* checksum8 result */;
#endif
};


/*
 * common data for all protocols
 */
struct doorbell_hdr_t
{
	u8_t data;
		#define DOORBELL_HDR_T_RX                                                            (0x1<<0) /* BitField data 	1 for rx doorbell, 0 for tx doorbell */
		#define DOORBELL_HDR_T_RX_SHIFT                                                      0
		#define DOORBELL_HDR_T_DB_TYPE                                                       (0x1<<1) /* BitField data 	0 for normal doorbell, 1 for advertise wnd doorbell */
		#define DOORBELL_HDR_T_DB_TYPE_SHIFT                                                 1
		#define DOORBELL_HDR_T_DPM_SIZE                                                      (0x3<<2) /* BitField data 	rdma tx only: DPM transaction size specifier (64/128/256/512 bytes) */
		#define DOORBELL_HDR_T_DPM_SIZE_SHIFT                                                2
		#define DOORBELL_HDR_T_CONN_TYPE                                                     (0xF<<4) /* BitField data 	connection type */
		#define DOORBELL_HDR_T_CONN_TYPE_SHIFT                                               4
};

/*
 * Ethernet doorbell
 */
struct eth_tx_doorbell
{
#if defined(__BIG_ENDIAN)
	u16_t npackets /* number of data bytes that were added in the doorbell */;
	u8_t params;
		#define ETH_TX_DOORBELL_NUM_BDS                                                      (0x3F<<0) /* BitField params 	number of buffer descriptors that were added in the doorbell */
		#define ETH_TX_DOORBELL_NUM_BDS_SHIFT                                                0
		#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG                                         (0x1<<6) /* BitField params 	tx fin command flag */
		#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG_SHIFT                                   6
		#define ETH_TX_DOORBELL_SPARE                                                        (0x1<<7) /* BitField params 	doorbell queue spare flag */
		#define ETH_TX_DOORBELL_SPARE_SHIFT                                                  7
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	u8_t params;
		#define ETH_TX_DOORBELL_NUM_BDS                                                      (0x3F<<0) /* BitField params 	number of buffer descriptors that were added in the doorbell */
		#define ETH_TX_DOORBELL_NUM_BDS_SHIFT                                                0
		#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG                                         (0x1<<6) /* BitField params 	tx fin command flag */
		#define ETH_TX_DOORBELL_RESERVED_TX_FIN_FLAG_SHIFT                                   6
		#define ETH_TX_DOORBELL_SPARE                                                        (0x1<<7) /* BitField params 	doorbell queue spare flag */
		#define ETH_TX_DOORBELL_SPARE_SHIFT                                                  7
	u16_t npackets /* number of data bytes that were added in the doorbell */;
#endif
};


/*
 * 3 lines. status block $$KEEP_ENDIANNESS$$
 */
struct hc_status_block_e1x
{
	u16_t index_values[HC_SB_MAX_INDICES_E1X] /* indices reported by cstorm */;
	u16_t running_index[HC_SB_MAX_SM] /* Status Block running indices */;
	u32_t rsrv;
};

/*
 * host status block
 */
struct host_hc_status_block_e1x
{
	struct hc_status_block_e1x sb /* fast path indices */;
};


/*
 * 3 lines. status block $$KEEP_ENDIANNESS$$
 */
struct hc_status_block_e2
{
	u16_t index_values[HC_SB_MAX_INDICES_E2] /* indices reported by cstorm */;
	u16_t running_index[HC_SB_MAX_SM] /* Status Block running indices */;
	u32_t reserved;
};

/*
 * host status block
 */
struct host_hc_status_block_e2
{
	struct hc_status_block_e2 sb /* fast path indices */;
};


/*
 * 5 lines. slow-path status block $$KEEP_ENDIANNESS$$
 */
struct hc_sp_status_block
{
	u16_t index_values[HC_SP_SB_MAX_INDICES] /* indices reported by cstorm */;
	u16_t running_index /* Status Block running index */;
	u16_t rsrv;
	u32_t rsrv1;
};

/*
 * host status block
 */
struct host_sp_status_block
{
	struct atten_sp_status_block atten_status_block /* attention bits section */;
	struct hc_sp_status_block sp_sb /* slow path indices */;
};


/*
 * IGU driver acknowledgment register
 */
struct igu_ack_register
{
#if defined(__BIG_ENDIAN)
	u16_t sb_id_and_flags;
		#define IGU_ACK_REGISTER_STATUS_BLOCK_ID                                             (0x1F<<0) /* BitField sb_id_and_flags 	0-15: non default status blocks, 16: default status block */
		#define IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT                                       0
		#define IGU_ACK_REGISTER_STORM_ID                                                    (0x7<<5) /* BitField sb_id_and_flags 	0-3:storm id, 4: attn status block (valid in default sb only) */
		#define IGU_ACK_REGISTER_STORM_ID_SHIFT                                              5
		#define IGU_ACK_REGISTER_UPDATE_INDEX                                                (0x1<<8) /* BitField sb_id_and_flags 	if set, acknowledges status block index */
		#define IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT                                          8
		#define IGU_ACK_REGISTER_INTERRUPT_MODE                                              (0x3<<9) /* BitField sb_id_and_flags 	interrupt enable/disable/nop: use IGU_INT_xxx constants */
		#define IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT                                        9
		#define IGU_ACK_REGISTER_RESERVED                                                    (0x1F<<11) /* BitField sb_id_and_flags 	 */
		#define IGU_ACK_REGISTER_RESERVED_SHIFT                                              11
	u16_t status_block_index /* status block index acknowledgement */;
#elif defined(__LITTLE_ENDIAN)
	u16_t status_block_index /* status block index acknowledgement */;
	u16_t sb_id_and_flags;
		#define IGU_ACK_REGISTER_STATUS_BLOCK_ID                                             (0x1F<<0) /* BitField sb_id_and_flags 	0-15: non default status blocks, 16: default status block */
		#define IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT                                       0
		#define IGU_ACK_REGISTER_STORM_ID                                                    (0x7<<5) /* BitField sb_id_and_flags 	0-3:storm id, 4: attn status block (valid in default sb only) */
		#define IGU_ACK_REGISTER_STORM_ID_SHIFT                                              5
		#define IGU_ACK_REGISTER_UPDATE_INDEX                                                (0x1<<8) /* BitField sb_id_and_flags 	if set, acknowledges status block index */
		#define IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT                                          8
		#define IGU_ACK_REGISTER_INTERRUPT_MODE                                              (0x3<<9) /* BitField sb_id_and_flags 	interrupt enable/disable/nop: use IGU_INT_xxx constants */
		#define IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT                                        9
		#define IGU_ACK_REGISTER_RESERVED                                                    (0x1F<<11) /* BitField sb_id_and_flags 	 */
		#define IGU_ACK_REGISTER_RESERVED_SHIFT                                              11
#endif
};


/*
 * IGU driver acknowledgement register
 */
struct igu_backward_compatible
{
	u32_t sb_id_and_flags;
		#define IGU_BACKWARD_COMPATIBLE_SB_INDEX                                             (0xFFFF<<0) /* BitField sb_id_and_flags 	 */
		#define IGU_BACKWARD_COMPATIBLE_SB_INDEX_SHIFT                                       0
		#define IGU_BACKWARD_COMPATIBLE_SB_SELECT                                            (0x1F<<16) /* BitField sb_id_and_flags 	 */
		#define IGU_BACKWARD_COMPATIBLE_SB_SELECT_SHIFT                                      16
		#define IGU_BACKWARD_COMPATIBLE_SEGMENT_ACCESS                                       (0x7<<21) /* BitField sb_id_and_flags 	0-3:storm id, 4: attn status block (valid in default sb only) */
		#define IGU_BACKWARD_COMPATIBLE_SEGMENT_ACCESS_SHIFT                                 21
		#define IGU_BACKWARD_COMPATIBLE_BUPDATE                                              (0x1<<24) /* BitField sb_id_and_flags 	if set, acknowledges status block index */
		#define IGU_BACKWARD_COMPATIBLE_BUPDATE_SHIFT                                        24
		#define IGU_BACKWARD_COMPATIBLE_ENABLE_INT                                           (0x3<<25) /* BitField sb_id_and_flags 	interrupt enable/disable/nop: use IGU_INT_xxx constants */
		#define IGU_BACKWARD_COMPATIBLE_ENABLE_INT_SHIFT                                     25
		#define IGU_BACKWARD_COMPATIBLE_RESERVED_0                                           (0x1F<<27) /* BitField sb_id_and_flags 	 */
		#define IGU_BACKWARD_COMPATIBLE_RESERVED_0_SHIFT                                     27
	u32_t reserved_2;
};


/*
 * IGU driver acknowledgement register
 */
struct igu_regular
{
	u32_t sb_id_and_flags;
		#define IGU_REGULAR_SB_INDEX                                                         (0xFFFFF<<0) /* BitField sb_id_and_flags 	 */
		#define IGU_REGULAR_SB_INDEX_SHIFT                                                   0
		#define IGU_REGULAR_RESERVED0                                                        (0x1<<20) /* BitField sb_id_and_flags 	 */
		#define IGU_REGULAR_RESERVED0_SHIFT                                                  20
		#define IGU_REGULAR_SEGMENT_ACCESS                                                   (0x7<<21) /* BitField sb_id_and_flags 	21-23 (use enum igu_seg_access) */
		#define IGU_REGULAR_SEGMENT_ACCESS_SHIFT                                             21
		#define IGU_REGULAR_BUPDATE                                                          (0x1<<24) /* BitField sb_id_and_flags 	 */
		#define IGU_REGULAR_BUPDATE_SHIFT                                                    24
		#define IGU_REGULAR_ENABLE_INT                                                       (0x3<<25) /* BitField sb_id_and_flags 	interrupt enable/disable/nop (use enum igu_int_cmd) */
		#define IGU_REGULAR_ENABLE_INT_SHIFT                                                 25
		#define IGU_REGULAR_RESERVED_1                                                       (0x1<<27) /* BitField sb_id_and_flags 	 */
		#define IGU_REGULAR_RESERVED_1_SHIFT                                                 27
		#define IGU_REGULAR_CLEANUP_TYPE                                                     (0x3<<28) /* BitField sb_id_and_flags 	 */
		#define IGU_REGULAR_CLEANUP_TYPE_SHIFT                                               28
		#define IGU_REGULAR_CLEANUP_SET                                                      (0x1<<30) /* BitField sb_id_and_flags 	 */
		#define IGU_REGULAR_CLEANUP_SET_SHIFT                                                30
		#define IGU_REGULAR_BCLEANUP                                                         (0x1<<31) /* BitField sb_id_and_flags 	 */
		#define IGU_REGULAR_BCLEANUP_SHIFT                                                   31
	u32_t reserved_2;
};

/*
 * IGU driver acknowledgement register
 */
union igu_consprod_reg
{
	struct igu_regular regular;
	struct igu_backward_compatible backward_compatible;
};


/*
 * Igu control commands
 */
enum igu_ctrl_cmd
{
	IGU_CTRL_CMD_TYPE_RD=0,
	IGU_CTRL_CMD_TYPE_WR=1,
	MAX_IGU_CTRL_CMD
};


/*
 * Control register for the IGU command register
 */
struct igu_ctrl_reg
{
	u32_t ctrl_data;
		#define IGU_CTRL_REG_ADDRESS                                                         (0xFFF<<0) /* BitField ctrl_data 	 */
		#define IGU_CTRL_REG_ADDRESS_SHIFT                                                   0
		#define IGU_CTRL_REG_FID                                                             (0x7F<<12) /* BitField ctrl_data 	 */
		#define IGU_CTRL_REG_FID_SHIFT                                                       12
		#define IGU_CTRL_REG_RESERVED                                                        (0x1<<19) /* BitField ctrl_data 	 */
		#define IGU_CTRL_REG_RESERVED_SHIFT                                                  19
		#define IGU_CTRL_REG_TYPE                                                            (0x1<<20) /* BitField ctrl_data 	taken (use enum igu_ctrl_cmd) */
		#define IGU_CTRL_REG_TYPE_SHIFT                                                      20
		#define IGU_CTRL_REG_UNUSED                                                          (0x7FF<<21) /* BitField ctrl_data 	 */
		#define IGU_CTRL_REG_UNUSED_SHIFT                                                    21
};


/*
 * Igu interrupt command
 */
enum igu_int_cmd
{
	IGU_INT_ENABLE=0,
	IGU_INT_DISABLE=1,
	IGU_INT_NOP=2,
	IGU_INT_NOP2=3,
	MAX_IGU_INT_CMD
};



/*
 * Igu segments
 */
enum igu_seg_access
{
	IGU_SEG_ACCESS_NORM=0,
	IGU_SEG_ACCESS_DEF=1,
	IGU_SEG_ACCESS_ATTN=2,
	MAX_IGU_SEG_ACCESS
};


/*
 * iscsi doorbell
 */
struct iscsi_tx_doorbell
{
#if defined(__BIG_ENDIAN)
	u16_t reserved /* number of data bytes that were added in the doorbell */;
	u8_t params;
		#define ISCSI_TX_DOORBELL_NUM_WQES                                                   (0x3F<<0) /* BitField params 	number of buffer descriptors that were added in the doorbell */
		#define ISCSI_TX_DOORBELL_NUM_WQES_SHIFT                                             0
		#define ISCSI_TX_DOORBELL_RESERVED_TX_FIN_FLAG                                       (0x1<<6) /* BitField params 	tx fin command flag */
		#define ISCSI_TX_DOORBELL_RESERVED_TX_FIN_FLAG_SHIFT                                 6
		#define ISCSI_TX_DOORBELL_SPARE                                                      (0x1<<7) /* BitField params 	doorbell queue spare flag */
		#define ISCSI_TX_DOORBELL_SPARE_SHIFT                                                7
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	u8_t params;
		#define ISCSI_TX_DOORBELL_NUM_WQES                                                   (0x3F<<0) /* BitField params 	number of buffer descriptors that were added in the doorbell */
		#define ISCSI_TX_DOORBELL_NUM_WQES_SHIFT                                             0
		#define ISCSI_TX_DOORBELL_RESERVED_TX_FIN_FLAG                                       (0x1<<6) /* BitField params 	tx fin command flag */
		#define ISCSI_TX_DOORBELL_RESERVED_TX_FIN_FLAG_SHIFT                                 6
		#define ISCSI_TX_DOORBELL_SPARE                                                      (0x1<<7) /* BitField params 	doorbell queue spare flag */
		#define ISCSI_TX_DOORBELL_SPARE_SHIFT                                                7
	u16_t reserved /* number of data bytes that were added in the doorbell */;
#endif
};


/*
 * Parser parsing flags field
 */
struct parsing_flags
{
	u16_t flags;
		#define PARSING_FLAGS_ETHERNET_ADDRESS_TYPE                                          (0x1<<0) /* BitField flags context flags	0=non-unicast, 1=unicast - use enum prs_flags_eth_addr_type */
		#define PARSING_FLAGS_ETHERNET_ADDRESS_TYPE_SHIFT                                    0
		#define PARSING_FLAGS_INNER_VLAN_EXIST                                               (0x1<<1) /* BitField flags context flags	0 or 1 */
		#define PARSING_FLAGS_INNER_VLAN_EXIST_SHIFT                                         1
		#define PARSING_FLAGS_OUTER_VLAN_EXIST                                               (0x1<<2) /* BitField flags context flags	0 or 1 */
		#define PARSING_FLAGS_OUTER_VLAN_EXIST_SHIFT                                         2
		#define PARSING_FLAGS_OVER_ETHERNET_PROTOCOL                                         (0x3<<3) /* BitField flags context flags	0=un-known, 1=Ipv4, 2=Ipv6,3=LLC SNAP un-known. LLC SNAP here refers only to LLC/SNAP packets that do not have Ipv4 or Ipv6 above them. Ipv4 and Ipv6 indications are even if they are over LLC/SNAP and not directly over Ethernet - use enum prs_flag_over_eth */
		#define PARSING_FLAGS_OVER_ETHERNET_PROTOCOL_SHIFT                                   3
		#define PARSING_FLAGS_IP_OPTIONS                                                     (0x1<<5) /* BitField flags context flags	0=no IP options / extension headers. 1=IP options / extension header exist */
		#define PARSING_FLAGS_IP_OPTIONS_SHIFT                                               5
		#define PARSING_FLAGS_FRAGMENTATION_STATUS                                           (0x1<<6) /* BitField flags context flags	0=non-fragmented, 1=fragmented */
		#define PARSING_FLAGS_FRAGMENTATION_STATUS_SHIFT                                     6
		#define PARSING_FLAGS_OVER_IP_PROTOCOL                                               (0x3<<7) /* BitField flags context flags	0=un-known, 1=TCP, 2=UDP - use enum prs_flags_over_ip */
		#define PARSING_FLAGS_OVER_IP_PROTOCOL_SHIFT                                         7
		#define PARSING_FLAGS_PURE_ACK_INDICATION                                            (0x1<<9) /* BitField flags context flags	0=packet with data, 1=pure-ACK - use enum prs_flags_ack_type */
		#define PARSING_FLAGS_PURE_ACK_INDICATION_SHIFT                                      9
		#define PARSING_FLAGS_TCP_OPTIONS_EXIST                                              (0x1<<10) /* BitField flags context flags	0=no TCP options. 1=TCP options */
		#define PARSING_FLAGS_TCP_OPTIONS_EXIST_SHIFT                                        10
		#define PARSING_FLAGS_TIME_STAMP_EXIST_FLAG                                          (0x1<<11) /* BitField flags context flags	According to the TCP header options parsing */
		#define PARSING_FLAGS_TIME_STAMP_EXIST_FLAG_SHIFT                                    11
		#define PARSING_FLAGS_CONNECTION_MATCH                                               (0x1<<12) /* BitField flags context flags	connection match in searcher indication */
		#define PARSING_FLAGS_CONNECTION_MATCH_SHIFT                                         12
		#define PARSING_FLAGS_LLC_SNAP                                                       (0x1<<13) /* BitField flags context flags	LLC SNAP indication */
		#define PARSING_FLAGS_LLC_SNAP_SHIFT                                                 13
		#define PARSING_FLAGS_RESERVED0                                                      (0x3<<14) /* BitField flags context flags	 */
		#define PARSING_FLAGS_RESERVED0_SHIFT                                                14
};


/*
 * Parsing flags for TCP ACK type
 */
enum prs_flags_ack_type
{
	PRS_FLAG_PUREACK_PIGGY=0,
	PRS_FLAG_PUREACK_PURE=1,
	MAX_PRS_FLAGS_ACK_TYPE
};


/*
 * Parsing flags for Ethernet address type
 */
enum prs_flags_eth_addr_type
{
	PRS_FLAG_ETHTYPE_NON_UNICAST=0,
	PRS_FLAG_ETHTYPE_UNICAST=1,
	MAX_PRS_FLAGS_ETH_ADDR_TYPE
};


/*
 * Parsing flags for over-ethernet protocol
 */
enum prs_flags_over_eth
{
	PRS_FLAG_OVERETH_UNKNOWN=0,
	PRS_FLAG_OVERETH_IPV4=1,
	PRS_FLAG_OVERETH_IPV6=2,
	PRS_FLAG_OVERETH_LLCSNAP_UNKNOWN=3,
	MAX_PRS_FLAGS_OVER_ETH
};


/*
 * Parsing flags for over-IP protocol
 */
enum prs_flags_over_ip
{
	PRS_FLAG_OVERIP_UNKNOWN=0,
	PRS_FLAG_OVERIP_TCP=1,
	PRS_FLAG_OVERIP_UDP=2,
	MAX_PRS_FLAGS_OVER_IP
};


/*
 * SDM operation gen command (generate aggregative interrupt)
 */
struct sdm_op_gen
{
	u32_t command;
		#define SDM_OP_GEN_COMP_PARAM                                                        (0x1F<<0) /* BitField command comp_param and comp_type	thread ID/aggr interrupt number/counter depending on the completion type */
		#define SDM_OP_GEN_COMP_PARAM_SHIFT                                                  0
		#define SDM_OP_GEN_COMP_TYPE                                                         (0x7<<5) /* BitField command comp_param and comp_type	Direct messages to CM / PCI switch are not supported in operation_gen completion */
		#define SDM_OP_GEN_COMP_TYPE_SHIFT                                                   5
		#define SDM_OP_GEN_AGG_VECT_IDX                                                      (0xFF<<8) /* BitField command comp_param and comp_type	bit index in aggregated interrupt vector */
		#define SDM_OP_GEN_AGG_VECT_IDX_SHIFT                                                8
		#define SDM_OP_GEN_AGG_VECT_IDX_VALID                                                (0x1<<16) /* BitField command comp_param and comp_type	 */
		#define SDM_OP_GEN_AGG_VECT_IDX_VALID_SHIFT                                          16
		#define SDM_OP_GEN_RESERVED                                                          (0x7FFF<<17) /* BitField command comp_param and comp_type	 */
		#define SDM_OP_GEN_RESERVED_SHIFT                                                    17
};


/*
 * Timers connection context
 */
struct timers_block_context
{
	u32_t __client0 /* data of client 0 of the timers block */;
	u32_t __client1 /* data of client 1 of the timers block */;
	u32_t __client2 /* data of client 2 of the timers block */;
	u32_t flags;
		#define __TIMERS_BLOCK_CONTEXT_NUM_OF_ACTIVE_TIMERS                                  (0x3<<0) /* BitField flags context flags	number of active timers running */
		#define __TIMERS_BLOCK_CONTEXT_NUM_OF_ACTIVE_TIMERS_SHIFT                            0
		#define TIMERS_BLOCK_CONTEXT_CONN_VALID_FLG                                          (0x1<<2) /* BitField flags context flags	flag: is connection valid (should be set by driver to 1 in toe/iscsi connections) */
		#define TIMERS_BLOCK_CONTEXT_CONN_VALID_FLG_SHIFT                                    2
		#define __TIMERS_BLOCK_CONTEXT_RESERVED0                                             (0x1FFFFFFF<<3) /* BitField flags context flags	 */
		#define __TIMERS_BLOCK_CONTEXT_RESERVED0_SHIFT                                       3
};


/*
 * advertise window doorbell
 */
struct toe_adv_wnd_doorbell
{
#if defined(__BIG_ENDIAN)
	u16_t wnd_sz_lsb /* Less significant bits of advertise window update value */;
	u8_t wnd_sz_msb /* Most significant bits of advertise window update value */;
	struct doorbell_hdr_t hdr /* See description of the appropriate type */;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr /* See description of the appropriate type */;
	u8_t wnd_sz_msb /* Most significant bits of advertise window update value */;
	u16_t wnd_sz_lsb /* Less significant bits of advertise window update value */;
#endif
};


/*
 * toe rx BDs update doorbell
 */
struct toe_rx_bds_doorbell
{
#if defined(__BIG_ENDIAN)
	u16_t nbds /* BDs update value */;
	u8_t params;
		#define TOE_RX_BDS_DOORBELL_RESERVED                                                 (0x1F<<0) /* BitField params 	reserved */
		#define TOE_RX_BDS_DOORBELL_RESERVED_SHIFT                                           0
		#define TOE_RX_BDS_DOORBELL_OPCODE                                                   (0x7<<5) /* BitField params 	BDs update doorbell opcode (2) */
		#define TOE_RX_BDS_DOORBELL_OPCODE_SHIFT                                             5
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	u8_t params;
		#define TOE_RX_BDS_DOORBELL_RESERVED                                                 (0x1F<<0) /* BitField params 	reserved */
		#define TOE_RX_BDS_DOORBELL_RESERVED_SHIFT                                           0
		#define TOE_RX_BDS_DOORBELL_OPCODE                                                   (0x7<<5) /* BitField params 	BDs update doorbell opcode (2) */
		#define TOE_RX_BDS_DOORBELL_OPCODE_SHIFT                                             5
	u16_t nbds /* BDs update value */;
#endif
};


/*
 * toe rx bytes and BDs update doorbell
 */
struct toe_rx_bytes_and_bds_doorbell
{
#if defined(__BIG_ENDIAN)
	u16_t nbytes /* nbytes */;
	u8_t params;
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_NBDS                                           (0x1F<<0) /* BitField params 	producer delta from the last doorbell */
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_NBDS_SHIFT                                     0
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_OPCODE                                         (0x7<<5) /* BitField params 	rx bytes and BDs update doorbell opcode (1) */
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_OPCODE_SHIFT                                   5
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	u8_t params;
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_NBDS                                           (0x1F<<0) /* BitField params 	producer delta from the last doorbell */
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_NBDS_SHIFT                                     0
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_OPCODE                                         (0x7<<5) /* BitField params 	rx bytes and BDs update doorbell opcode (1) */
		#define TOE_RX_BYTES_AND_BDS_DOORBELL_OPCODE_SHIFT                                   5
	u16_t nbytes /* nbytes */;
#endif
};


/*
 * toe rx bytes doorbell
 */
struct toe_rx_byte_doorbell
{
#if defined(__BIG_ENDIAN)
	u16_t nbytes_lsb /* bits [0:15] of nbytes */;
	u8_t params;
		#define TOE_RX_BYTE_DOORBELL_NBYTES_MSB                                              (0x1F<<0) /* BitField params 	bits [20:16] of nbytes */
		#define TOE_RX_BYTE_DOORBELL_NBYTES_MSB_SHIFT                                        0
		#define TOE_RX_BYTE_DOORBELL_OPCODE                                                  (0x7<<5) /* BitField params 	rx bytes doorbell opcode (0) */
		#define TOE_RX_BYTE_DOORBELL_OPCODE_SHIFT                                            5
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	u8_t params;
		#define TOE_RX_BYTE_DOORBELL_NBYTES_MSB                                              (0x1F<<0) /* BitField params 	bits [20:16] of nbytes */
		#define TOE_RX_BYTE_DOORBELL_NBYTES_MSB_SHIFT                                        0
		#define TOE_RX_BYTE_DOORBELL_OPCODE                                                  (0x7<<5) /* BitField params 	rx bytes doorbell opcode (0) */
		#define TOE_RX_BYTE_DOORBELL_OPCODE_SHIFT                                            5
	u16_t nbytes_lsb /* bits [0:15] of nbytes */;
#endif
};


/*
 * toe rx consume GRQ doorbell
 */
struct toe_rx_grq_doorbell
{
#if defined(__BIG_ENDIAN)
	u16_t nbytes_lsb /* bits [0:15] of nbytes */;
	u8_t params;
		#define TOE_RX_GRQ_DOORBELL_NBYTES_MSB                                               (0x1F<<0) /* BitField params 	bits [20:16] of nbytes */
		#define TOE_RX_GRQ_DOORBELL_NBYTES_MSB_SHIFT                                         0
		#define TOE_RX_GRQ_DOORBELL_OPCODE                                                   (0x7<<5) /* BitField params 	rx GRQ doorbell opcode (4) */
		#define TOE_RX_GRQ_DOORBELL_OPCODE_SHIFT                                             5
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	u8_t params;
		#define TOE_RX_GRQ_DOORBELL_NBYTES_MSB                                               (0x1F<<0) /* BitField params 	bits [20:16] of nbytes */
		#define TOE_RX_GRQ_DOORBELL_NBYTES_MSB_SHIFT                                         0
		#define TOE_RX_GRQ_DOORBELL_OPCODE                                                   (0x7<<5) /* BitField params 	rx GRQ doorbell opcode (4) */
		#define TOE_RX_GRQ_DOORBELL_OPCODE_SHIFT                                             5
	u16_t nbytes_lsb /* bits [0:15] of nbytes */;
#endif
};


/*
 * toe doorbell
 */
struct toe_tx_doorbell
{
#if defined(__BIG_ENDIAN)
	u16_t nbytes /* number of data bytes that were added in the doorbell */;
	u8_t params;
		#define TOE_TX_DOORBELL_NUM_BDS                                                      (0x3F<<0) /* BitField params 	number of buffer descriptors that were added in the doorbell */
		#define TOE_TX_DOORBELL_NUM_BDS_SHIFT                                                0
		#define TOE_TX_DOORBELL_TX_FIN_FLAG                                                  (0x1<<6) /* BitField params 	tx fin command flag */
		#define TOE_TX_DOORBELL_TX_FIN_FLAG_SHIFT                                            6
		#define TOE_TX_DOORBELL_FLUSH                                                        (0x1<<7) /* BitField params 	doorbell queue spare flag */
		#define TOE_TX_DOORBELL_FLUSH_SHIFT                                                  7
	struct doorbell_hdr_t hdr;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t hdr;
	u8_t params;
		#define TOE_TX_DOORBELL_NUM_BDS                                                      (0x3F<<0) /* BitField params 	number of buffer descriptors that were added in the doorbell */
		#define TOE_TX_DOORBELL_NUM_BDS_SHIFT                                                0
		#define TOE_TX_DOORBELL_TX_FIN_FLAG                                                  (0x1<<6) /* BitField params 	tx fin command flag */
		#define TOE_TX_DOORBELL_TX_FIN_FLAG_SHIFT                                            6
		#define TOE_TX_DOORBELL_FLUSH                                                        (0x1<<7) /* BitField params 	doorbell queue spare flag */
		#define TOE_TX_DOORBELL_FLUSH_SHIFT                                                  7
	u16_t nbytes /* number of data bytes that were added in the doorbell */;
#endif
};


/*
 * The eth aggregative context of Tstorm
 */
struct tstorm_eth_ag_context
{
	u32_t __reserved0[14];
};


/*
 * The fcoe extra aggregative context section of Tstorm
 */
struct tstorm_fcoe_extra_ag_context_section
{
	u32_t __agg_val1 /* aggregated value 1 */;
#if defined(__BIG_ENDIAN)
	u8_t __tcp_agg_vars2 /* Various aggregative variables */;
	u8_t __agg_val3 /* aggregated value 3 */;
	u16_t __agg_val2 /* aggregated value 2 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val2 /* aggregated value 2 */;
	u8_t __agg_val3 /* aggregated value 3 */;
	u8_t __tcp_agg_vars2 /* Various aggregative variables */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_val5;
	u8_t __agg_val6;
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
	u8_t __agg_val6;
	u16_t __agg_val5;
#endif
	u32_t __lcq_prod /* Next sequence number to transmit, given by Tx */;
	u32_t rtt_seq /* Rtt recording – sequence number */;
	u32_t rtt_time /* Rtt recording – real time clock */;
	u32_t __reserved66;
	u32_t wnd_right_edge /* The right edge of the receive window. Updated by the XSTORM when a segment with ACK is transmitted */;
	u32_t tcp_agg_vars1;
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_FIN_SENT_FLAG                           (0x1<<0) /* BitField tcp_agg_vars1 Various aggregative variables	Sticky bit that is set when FIN is sent and remains set */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT                     0
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG                    (0x1<<1) /* BitField tcp_agg_vars1 Various aggregative variables	The Tx indicates that it sent a FIN packet */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG_SHIFT              1
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF                              (0x3<<2) /* BitField tcp_agg_vars1 Various aggregative variables	Counter flag to indicate a window update */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF_SHIFT                        2
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF                              (0x3<<4) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that a timeout expired */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF_SHIFT                        4
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF_EN                           (0x1<<6) /* BitField tcp_agg_vars1 Various aggregative variables	Enable the decision rule that considers the WndUpd counter flag */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_WND_UPD_CF_EN_SHIFT                     6
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF_EN                           (0x1<<7) /* BitField tcp_agg_vars1 Various aggregative variables	Enable the decision rule that considers the Timeout counter flag */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TIMEOUT_CF_EN_SHIFT                     7
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN                       (0x1<<8) /* BitField tcp_agg_vars1 Various aggregative variables	If 1 then the Rxmit sequence decision rule is enabled */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN_SHIFT                 8
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LCQ_SND_EN                            (0x1<<9) /* BitField tcp_agg_vars1 Various aggregative variables	If set then the SendNext decision rule is enabled */
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_LCQ_SND_EN_SHIFT                      9
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG                               (0x1<<10) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                         10
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_FLAG                               (0x1<<11) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_FLAG_SHIFT                         11
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF_EN                              (0x1<<12) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF_EN_SHIFT                        12
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF_EN                              (0x1<<13) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF_EN_SHIFT                        13
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF                                 (0x3<<14) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_CF_SHIFT                           14
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF                                 (0x3<<16) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX2_CF_SHIFT                           16
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_BLOCKED                              (0x1<<18) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that Tx has more to send, but has not enough window to send it */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_BLOCKED_SHIFT                        18
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX10_CF_EN                           (0x1<<19) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT                     19
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX11_CF_EN                           (0x1<<20) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX11_CF_EN_SHIFT                     20
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX12_CF_EN                           (0x1<<21) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX12_CF_EN_SHIFT                     21
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED1                             (0x3<<22) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED1_SHIFT                       22
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ                     (0xF<<24) /* BitField tcp_agg_vars1 Various aggregative variables	The sequence of the last fast retransmit or goto SS comand sent */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ_SHIFT               24
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ                     (0xF<<28) /* BitField tcp_agg_vars1 Various aggregative variables	The sequence of the last fast retransmit or Goto SS command performed by the XSTORM */
		#define TSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ_SHIFT               28
	u32_t snd_max /* Maximum sequence number that was ever transmitted */;
	u32_t __lcq_cons /* Last ACK sequence number sent by the Tx */;
	u32_t __reserved2;
};

/*
 * The fcoe aggregative context of Tstorm
 */
struct tstorm_fcoe_ag_context
{
#if defined(__BIG_ENDIAN)
	u16_t ulp_credit;
	u8_t agg_vars1;
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                         (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                   0
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                         (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                   1
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2                                         (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                   2
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3                                         (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                   3
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF                                     (0x3<<4) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_SHIFT                               4
		#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG                                           (0x1<<6) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG_SHIFT                                     6
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG                                           (0x1<<7) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG_SHIFT                                     7
	u8_t state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	u8_t state /* The state of the connection */;
	u8_t agg_vars1;
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                         (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                   0
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                         (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                   1
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2                                         (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                   2
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3                                         (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                   3
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF                                     (0x3<<4) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_SHIFT                               4
		#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG                                           (0x1<<6) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX3_FLAG_SHIFT                                     6
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG                                           (0x1<<7) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_FLAG_SHIFT                                     7
	u16_t ulp_credit;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_val4;
	u16_t agg_vars2;
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG                                           (0x1<<0) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG_SHIFT                                     0
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG                                           (0x1<<1) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG_SHIFT                                     1
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF                                             (0x3<<2) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF_SHIFT                                       2
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF                                             (0x3<<4) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF_SHIFT                                       4
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF                                             (0x3<<6) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF_SHIFT                                       6
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF                                             (0x3<<8) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF_SHIFT                                       8
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG                                           (0x1<<10) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG_SHIFT                                     10
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN                                  (0x1<<11) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN_SHIFT                            11
		#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN                                            (0x1<<12) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN_SHIFT                                      12
		#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN                                            (0x1<<13) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN_SHIFT                                      13
		#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN                                            (0x1<<14) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN_SHIFT                                      14
		#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN                                            (0x1<<15) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN_SHIFT                                      15
#elif defined(__LITTLE_ENDIAN)
	u16_t agg_vars2;
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG                                           (0x1<<0) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_FLAG_SHIFT                                     0
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG                                           (0x1<<1) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_FLAG_SHIFT                                     1
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF                                             (0x3<<2) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX4_CF_SHIFT                                       2
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF                                             (0x3<<4) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX5_CF_SHIFT                                       4
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF                                             (0x3<<6) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX6_CF_SHIFT                                       6
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF                                             (0x3<<8) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_CF_SHIFT                                       8
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG                                           (0x1<<10) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_AUX7_FLAG_SHIFT                                     10
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN                                  (0x1<<11) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_FCOE_AG_CONTEXT_QUEUE0_FLUSH_CF_EN_SHIFT                            11
		#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN                                            (0x1<<12) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX4_CF_EN_SHIFT                                      12
		#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN                                            (0x1<<13) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX5_CF_EN_SHIFT                                      13
		#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN                                            (0x1<<14) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX6_CF_EN_SHIFT                                      14
		#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN                                            (0x1<<15) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_FCOE_AG_CONTEXT_AUX7_CF_EN_SHIFT                                      15
	u16_t __agg_val4;
#endif
	struct tstorm_fcoe_extra_ag_context_section __extra_section /* Extra context section */;
};



/*
 * The tcp aggregative context section of Tstorm
 */
struct tstorm_tcp_tcp_ag_context_section
{
	u32_t __agg_val1 /* aggregated value 1 */;
#if defined(__BIG_ENDIAN)
	u8_t __tcp_agg_vars2 /* Various aggregative variables */;
	u8_t __agg_val3 /* aggregated value 3 */;
	u16_t __agg_val2 /* aggregated value 2 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val2 /* aggregated value 2 */;
	u8_t __agg_val3 /* aggregated value 3 */;
	u8_t __tcp_agg_vars2 /* Various aggregative variables */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_val5;
	u8_t __agg_val6;
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
	u8_t __agg_val6;
	u16_t __agg_val5;
#endif
	u32_t snd_nxt /* Next sequence number to transmit, given by Tx */;
	u32_t rtt_seq /* Rtt recording – sequence number */;
	u32_t rtt_time /* Rtt recording – real time clock */;
	u32_t __reserved66;
	u32_t wnd_right_edge /* The right edge of the receive window. Updated by the XSTORM when a segment with ACK is transmitted */;
	u32_t tcp_agg_vars1;
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG                              (0x1<<0) /* BitField tcp_agg_vars1 Various aggregative variables	Sticky bit that is set when FIN is sent and remains set */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT                        0
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG                       (0x1<<1) /* BitField tcp_agg_vars1 Various aggregative variables	The Tx indicates that it sent a FIN packet */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG_SHIFT                 1
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF                                 (0x3<<2) /* BitField tcp_agg_vars1 Various aggregative variables	Counter flag to indicate a window update */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_SHIFT                           2
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF                                 (0x3<<4) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that a timeout expired */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_SHIFT                           4
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_EN                              (0x1<<6) /* BitField tcp_agg_vars1 Various aggregative variables	Enable the decision rule that considers the WndUpd counter flag */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_WND_UPD_CF_EN_SHIFT                        6
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN                              (0x1<<7) /* BitField tcp_agg_vars1 Various aggregative variables	Enable the decision rule that considers the Timeout counter flag */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN_SHIFT                        7
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN                          (0x1<<8) /* BitField tcp_agg_vars1 Various aggregative variables	If 1 then the Rxmit sequence decision rule is enabled */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN_SHIFT                    8
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_SND_NXT_EN                                 (0x1<<9) /* BitField tcp_agg_vars1 Various aggregative variables	If set then the SendNext decision rule is enabled */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_SND_NXT_EN_SHIFT                           9
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG                                  (0x1<<10) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                            10
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_FLAG                                  (0x1<<11) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_FLAG_SHIFT                            11
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF_EN                                 (0x1<<12) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF_EN_SHIFT                           12
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF_EN                                 (0x1<<13) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF_EN_SHIFT                           13
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF                                    (0x3<<14) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_CF_SHIFT                              14
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF                                    (0x3<<16) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX2_CF_SHIFT                              16
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_BLOCKED                                 (0x1<<18) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that Tx has more to send, but has not enough window to send it */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_BLOCKED_SHIFT                           18
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN                              (0x1<<19) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT                        19
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN                              (0x1<<20) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN_SHIFT                        20
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN                              (0x1<<21) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN_SHIFT                        21
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED1                                (0x3<<22) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED1_SHIFT                          22
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ                        (0xF<<24) /* BitField tcp_agg_vars1 Various aggregative variables	The sequence of the last fast retransmit or goto SS comand sent */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ_SHIFT                  24
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ                        (0xF<<28) /* BitField tcp_agg_vars1 Various aggregative variables	The sequence of the last fast retransmit or Goto SS command performed by the XSTORM */
		#define TSTORM_TCP_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ_SHIFT                  28
	u32_t snd_max /* Maximum sequence number that was ever transmitted */;
	u32_t snd_una /* Last ACK sequence number sent by the Tx */;
	u32_t __reserved2;
};

/*
 * The iscsi aggregative context of Tstorm
 */
struct tstorm_iscsi_ag_context
{
#if defined(__BIG_ENDIAN)
	u16_t ulp_credit;
	u8_t agg_vars1;
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                        (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                  0
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF                                 (0x3<<4) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_SHIFT                           4
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG                                          (0x1<<6) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG_SHIFT                                    6
		#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG                               (0x1<<7) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG_SHIFT                         7
	u8_t state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	u8_t state /* The state of the connection */;
	u8_t agg_vars1;
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                        (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                  0
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF                                 (0x3<<4) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_SHIFT                           4
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG                                          (0x1<<6) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX3_FLAG_SHIFT                                    6
		#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG                               (0x1<<7) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_ACK_ON_FIN_SENT_FLAG_SHIFT                         7
	u16_t ulp_credit;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_val4;
	u16_t agg_vars2;
		#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG                                 (0x1<<0) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG_SHIFT                           0
		#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG                                (0x1<<1) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG_SHIFT                          1
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF                                        (0x3<<2) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_SHIFT                                  2
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF                                     (0x3<<4) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_SHIFT                               4
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF                                            (0x3<<6) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_SHIFT                                      6
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF                                            (0x3<<8) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_SHIFT                                      8
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG                                          (0x1<<10) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG_SHIFT                                    10
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                              (0x1<<11) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                        11
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN                                     (0x1<<12) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN_SHIFT                               12
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN                                  (0x1<<13) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN_SHIFT                            13
		#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN                                           (0x1<<14) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN_SHIFT                                     14
		#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN                                           (0x1<<15) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN_SHIFT                                     15
#elif defined(__LITTLE_ENDIAN)
	u16_t agg_vars2;
		#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG                                 (0x1<<0) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_MSL_TIMER_SET_FLAG_SHIFT                           0
		#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG                                (0x1<<1) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_FIN_SENT_FIRST_FLAG_SHIFT                          1
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF                                        (0x3<<2) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_SHIFT                                  2
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF                                     (0x3<<4) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_SHIFT                               4
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF                                            (0x3<<6) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_SHIFT                                      6
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF                                            (0x3<<8) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_SHIFT                                      8
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG                                          (0x1<<10) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_AUX7_FLAG_SHIFT                                    10
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                              (0x1<<11) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                        11
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN                                     (0x1<<12) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_RST_SENT_CF_EN_SHIFT                               12
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN                                  (0x1<<13) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_ISCSI_AG_CONTEXT_WAKEUP_CALL_CF_EN_SHIFT                            13
		#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN                                           (0x1<<14) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_ISCSI_AG_CONTEXT_AUX6_CF_EN_SHIFT                                     14
		#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN                                           (0x1<<15) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_ISCSI_AG_CONTEXT_AUX7_CF_EN_SHIFT                                     15
	u16_t __agg_val4;
#endif
	struct tstorm_tcp_tcp_ag_context_section tcp /* TCP context section, shared in TOE and iSCSI */;
};



/*
 * The toe aggregative context section of Tstorm
 */
struct tstorm_toe_tcp_ag_context_section
{
	u32_t __agg_val1 /* aggregated value 1 */;
#if defined(__BIG_ENDIAN)
	u8_t __tcp_agg_vars2 /* Various aggregative variables */;
	u8_t __agg_val3 /* aggregated value 3 */;
	u16_t __agg_val2 /* aggregated value 2 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val2 /* aggregated value 2 */;
	u8_t __agg_val3 /* aggregated value 3 */;
	u8_t __tcp_agg_vars2 /* Various aggregative variables */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_val5;
	u8_t __agg_val6;
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
	u8_t __agg_val6;
	u16_t __agg_val5;
#endif
	u32_t snd_nxt /* Next sequence number to transmit, given by Tx */;
	u32_t rtt_seq /* Rtt recording – sequence number */;
	u32_t rtt_time /* Rtt recording – real time clock */;
	u32_t __reserved66;
	u32_t wnd_right_edge /* The right edge of the receive window. Updated by the XSTORM when a segment with ACK is transmitted */;
	u32_t tcp_agg_vars1;
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG                              (0x1<<0) /* BitField tcp_agg_vars1 Various aggregative variables	Sticky bit that is set when FIN is sent and remains set */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT                        0
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG                       (0x1<<1) /* BitField tcp_agg_vars1 Various aggregative variables	The Tx indicates that it sent a FIN packet */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_LAST_PACKET_FIN_FLAG_SHIFT                 1
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED52                                 (0x3<<2) /* BitField tcp_agg_vars1 Various aggregative variables	Counter flag to indicate a window update */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED52_SHIFT                           2
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF                                 (0x3<<4) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that a timeout expired */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_SHIFT                           4
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_WND_UPD_CF_EN                     (0x1<<6) /* BitField tcp_agg_vars1 Various aggregative variables	Enable the decision rule that considers the WndUpd counter flag */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_WND_UPD_CF_EN_SHIFT               6
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN                              (0x1<<7) /* BitField tcp_agg_vars1 Various aggregative variables	Enable the decision rule that considers the Timeout counter flag */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TIMEOUT_CF_EN_SHIFT                        7
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN                          (0x1<<8) /* BitField tcp_agg_vars1 Various aggregative variables	If 1 then the Rxmit sequence decision rule is enabled */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_SEQ_EN_SHIFT                    8
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_SND_NXT_EN                                 (0x1<<9) /* BitField tcp_agg_vars1 Various aggregative variables	If set then the SendNext decision rule is enabled */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_SND_NXT_EN_SHIFT                           9
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_NEWRTTSAMPLE                               (0x1<<10) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_NEWRTTSAMPLE_SHIFT                         10
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED55                                 (0x1<<11) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED55_SHIFT                           11
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_AUX1_CF_EN                        (0x1<<12) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_AUX1_CF_EN_SHIFT                  12
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_AUX2_CF_EN                        (0x1<<13) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED_AUX2_CF_EN_SHIFT                  13
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED56                                 (0x3<<14) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED56_SHIFT                           14
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED57                                 (0x3<<16) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED57_SHIFT                           16
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_BLOCKED                                 (0x1<<18) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that Tx has more to send, but has not enough window to send it */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_BLOCKED_SHIFT                           18
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN                              (0x1<<19) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT                        19
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN                              (0x1<<20) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX11_CF_EN_SHIFT                        20
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN                              (0x1<<21) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX12_CF_EN_SHIFT                        21
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED1                                (0x3<<22) /* BitField tcp_agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED1_SHIFT                          22
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ                        (0xF<<24) /* BitField tcp_agg_vars1 Various aggregative variables	The sequence of the last fast retransmit or goto SS comand sent */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_PEND_SEQ_SHIFT                  24
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ                        (0xF<<28) /* BitField tcp_agg_vars1 Various aggregative variables	The sequence of the last fast retransmit or Goto SS command performed by the XSTORM */
		#define TSTORM_TOE_TCP_AG_CONTEXT_SECTION_RETRANSMIT_DONE_SEQ_SHIFT                  28
	u32_t snd_max /* Maximum sequence number that was ever transmitted */;
	u32_t snd_una /* Last ACK sequence number sent by the Tx */;
	u32_t __reserved2;
};

/*
 * The toe aggregative context of Tstorm
 */
struct tstorm_toe_ag_context
{
#if defined(__BIG_ENDIAN)
	u16_t reserved54;
	u8_t agg_vars1;
		#define TSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0                                          (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                    0
		#define TSTORM_TOE_AG_CONTEXT_RESERVED51                                             (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED51_SHIFT                                       1
		#define TSTORM_TOE_AG_CONTEXT_RESERVED52                                             (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED52_SHIFT                                       2
		#define TSTORM_TOE_AG_CONTEXT_RESERVED53                                             (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED53_SHIFT                                       3
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF                                   (0x3<<4) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_SHIFT                             4
		#define __TSTORM_TOE_AG_CONTEXT_AUX3_FLAG                                            (0x1<<6) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX3_FLAG_SHIFT                                      6
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_FLAG                                            (0x1<<7) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_FLAG_SHIFT                                      7
	u8_t __state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __state /* The state of the connection */;
	u8_t agg_vars1;
		#define TSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0                                          (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define TSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                    0
		#define TSTORM_TOE_AG_CONTEXT_RESERVED51                                             (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED51_SHIFT                                       1
		#define TSTORM_TOE_AG_CONTEXT_RESERVED52                                             (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED52_SHIFT                                       2
		#define TSTORM_TOE_AG_CONTEXT_RESERVED53                                             (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED53_SHIFT                                       3
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF                                   (0x3<<4) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_SHIFT                             4
		#define __TSTORM_TOE_AG_CONTEXT_AUX3_FLAG                                            (0x1<<6) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX3_FLAG_SHIFT                                      6
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_FLAG                                            (0x1<<7) /* BitField agg_vars1 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_FLAG_SHIFT                                      7
	u16_t reserved54;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_val4;
	u16_t agg_vars2;
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_FLAG                                            (0x1<<0) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_FLAG_SHIFT                                      0
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_FLAG                                            (0x1<<1) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_FLAG_SHIFT                                      1
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_CF                                              (0x3<<2) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_CF_SHIFT                                        2
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_CF                                              (0x3<<4) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_CF_SHIFT                                        4
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_CF                                              (0x3<<6) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_CF_SHIFT                                        6
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_CF                                              (0x3<<8) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_CF_SHIFT                                        8
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_FLAG                                            (0x1<<10) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_FLAG_SHIFT                                      10
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                                (0x1<<11) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                          11
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX4_CF_EN                                    (0x1<<12) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX4_CF_EN_SHIFT                              12
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX5_CF_EN                                    (0x1<<13) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX5_CF_EN_SHIFT                              13
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX6_CF_EN                                    (0x1<<14) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX6_CF_EN_SHIFT                              14
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX7_CF_EN                                    (0x1<<15) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX7_CF_EN_SHIFT                              15
#elif defined(__LITTLE_ENDIAN)
	u16_t agg_vars2;
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_FLAG                                            (0x1<<0) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_FLAG_SHIFT                                      0
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_FLAG                                            (0x1<<1) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_FLAG_SHIFT                                      1
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_CF                                              (0x3<<2) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX4_CF_SHIFT                                        2
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_CF                                              (0x3<<4) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX5_CF_SHIFT                                        4
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_CF                                              (0x3<<6) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX6_CF_SHIFT                                        6
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_CF                                              (0x3<<8) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_CF_SHIFT                                        8
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_FLAG                                            (0x1<<10) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_AUX7_FLAG_SHIFT                                      10
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                                (0x1<<11) /* BitField agg_vars2 Various aggregative variables	 */
		#define __TSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                          11
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX4_CF_EN                                    (0x1<<12) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX4_CF_EN_SHIFT                              12
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX5_CF_EN                                    (0x1<<13) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX5_CF_EN_SHIFT                              13
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX6_CF_EN                                    (0x1<<14) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX6_CF_EN_SHIFT                              14
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX7_CF_EN                                    (0x1<<15) /* BitField agg_vars2 Various aggregative variables	 */
		#define TSTORM_TOE_AG_CONTEXT_RESERVED_AUX7_CF_EN_SHIFT                              15
	u16_t __agg_val4;
#endif
	struct tstorm_toe_tcp_ag_context_section tcp /* TCP context section, shared in TOE and iSCSI */;
};



/*
 * The eth aggregative context of Ustorm
 */
struct ustorm_eth_ag_context
{
	u32_t __reserved0;
#if defined(__BIG_ENDIAN)
	u8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
	u8_t __reserved2;
	u16_t __reserved1;
#elif defined(__LITTLE_ENDIAN)
	u16_t __reserved1;
	u8_t __reserved2;
	u8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
#endif
	u32_t __reserved3[6];
};


/*
 * The fcoe aggregative context of Ustorm
 */
struct ustorm_fcoe_ag_context
{
#if defined(__BIG_ENDIAN)
	u8_t __aux_counter_flags /* auxiliary counter flags */;
	u8_t agg_vars2;
		#define USTORM_FCOE_AG_CONTEXT_TX_CF                                                 (0x3<<0) /* BitField agg_vars2 various aggregation variables	Set when a message was received from the Tx STORM. For future use. */
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_SHIFT                                           0
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF                                            (0x3<<2) /* BitField agg_vars2 various aggregation variables	Set when a message was received from the Timer. */
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_SHIFT                                      2
		#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE                                        (0x7<<4) /* BitField agg_vars2 various aggregation variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE_SHIFT                                  4
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK                                       (0x1<<7) /* BitField agg_vars2 various aggregation variables	Used to mask the decision rule of AggVal2. Used in iSCSI. Should be 0 in all other protocols */
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK_SHIFT                                 7
	u8_t agg_vars1;
		#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                       (0x1<<0) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 0 */
		#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                 0
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                         (0x1<<1) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 1 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                   1
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2                                         (0x1<<2) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 2 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                   2
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3                                         (0x1<<3) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 3 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                   3
		#define USTORM_FCOE_AG_CONTEXT_INV_CF                                                (0x3<<4) /* BitField agg_vars1 various aggregation variables	Indicates a valid invalidate request. Set by the CMP STORM. */
		#define USTORM_FCOE_AG_CONTEXT_INV_CF_SHIFT                                          4
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF                                         (0x3<<6) /* BitField agg_vars1 various aggregation variables	Set when a message was received from the CMP STORM. For future use. */
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_SHIFT                                   6
	u8_t state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	u8_t state /* The state of the connection */;
	u8_t agg_vars1;
		#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                       (0x1<<0) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 0 */
		#define __USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                 0
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                         (0x1<<1) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 1 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                   1
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2                                         (0x1<<2) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 2 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                   2
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3                                         (0x1<<3) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 3 */
		#define USTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                   3
		#define USTORM_FCOE_AG_CONTEXT_INV_CF                                                (0x3<<4) /* BitField agg_vars1 various aggregation variables	Indicates a valid invalidate request. Set by the CMP STORM. */
		#define USTORM_FCOE_AG_CONTEXT_INV_CF_SHIFT                                          4
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF                                         (0x3<<6) /* BitField agg_vars1 various aggregation variables	Set when a message was received from the CMP STORM. For future use. */
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_SHIFT                                   6
	u8_t agg_vars2;
		#define USTORM_FCOE_AG_CONTEXT_TX_CF                                                 (0x3<<0) /* BitField agg_vars2 various aggregation variables	Set when a message was received from the Tx STORM. For future use. */
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_SHIFT                                           0
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF                                            (0x3<<2) /* BitField agg_vars2 various aggregation variables	Set when a message was received from the Timer. */
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_SHIFT                                      2
		#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE                                        (0x7<<4) /* BitField agg_vars2 various aggregation variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define USTORM_FCOE_AG_CONTEXT_AGG_MISC4_RULE_SHIFT                                  4
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK                                       (0x1<<7) /* BitField agg_vars2 various aggregation variables	Used to mask the decision rule of AggVal2. Used in iSCSI. Should be 0 in all other protocols */
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL2_MASK_SHIFT                                 7
	u8_t __aux_counter_flags /* auxiliary counter flags */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
	u8_t agg_misc2;
	u16_t pbf_tx_seq_ack /* Sequence number of the last sequence transmitted by PBF. */;
#elif defined(__LITTLE_ENDIAN)
	u16_t pbf_tx_seq_ack /* Sequence number of the last sequence transmitted by PBF. */;
	u8_t agg_misc2;
	u8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
#endif
	u32_t agg_misc4;
#if defined(__BIG_ENDIAN)
	u8_t agg_val3_th;
	u8_t agg_val3;
	u16_t agg_misc3;
#elif defined(__LITTLE_ENDIAN)
	u16_t agg_misc3;
	u8_t agg_val3;
	u8_t agg_val3_th;
#endif
	u32_t expired_task_id /* Timer expiration task id */;
	u32_t agg_misc4_th;
#if defined(__BIG_ENDIAN)
	u16_t cq_prod /* CQ producer updated by FW */;
	u16_t cq_cons /* CQ consumer updated by driver via doorbell */;
#elif defined(__LITTLE_ENDIAN)
	u16_t cq_cons /* CQ consumer updated by driver via doorbell */;
	u16_t cq_prod /* CQ producer updated by FW */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __reserved2;
	u8_t decision_rules;
		#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE                                           (0x7<<0) /* BitField decision_rules Various decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE_SHIFT                                     0
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE                                       (0x7<<3) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                 3
		#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG                                         (0x1<<6) /* BitField decision_rules Various decision rules	CQ negative arm indication updated via doorbell */
		#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG_SHIFT                                   6
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED1                                           (0x1<<7) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED1_SHIFT                                     7
	u8_t decision_rule_enable_bits;
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN                                  (0x1<<0) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN_SHIFT                            0
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN                                      (0x1<<1) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN_SHIFT                                1
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN                                              (0x1<<2) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN_SHIFT                                        2
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN                                         (0x1<<3) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN_SHIFT                                   3
		#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN                                          (0x1<<4) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT                                    4
		#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN                                        (0x1<<5) /* BitField decision_rule_enable_bits Enable bits for various decision rules	The flush queues counter flag en.  */
		#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN_SHIFT                                  5
		#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN                                          (0x1<<6) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN_SHIFT                                    6
		#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN                                            (0x1<<7) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                      7
#elif defined(__LITTLE_ENDIAN)
	u8_t decision_rule_enable_bits;
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN                                  (0x1<<0) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED_INV_CF_EN_SHIFT                            0
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN                                      (0x1<<1) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_COMPLETION_CF_EN_SHIFT                                1
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN                                              (0x1<<2) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_TX_CF_EN_SHIFT                                        2
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN                                         (0x1<<3) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_TIMER_CF_EN_SHIFT                                   3
		#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN                                          (0x1<<4) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT                                    4
		#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN                                        (0x1<<5) /* BitField decision_rule_enable_bits Enable bits for various decision rules	The flush queues counter flag en.  */
		#define __USTORM_FCOE_AG_CONTEXT_QUEUE0_CF_EN_SHIFT                                  5
		#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN                                          (0x1<<6) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AUX3_CF_EN_SHIFT                                    6
		#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN                                            (0x1<<7) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                      7
	u8_t decision_rules;
		#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE                                           (0x7<<0) /* BitField decision_rules Various decision rules	 */
		#define USTORM_FCOE_AG_CONTEXT_CQ_DEC_RULE_SHIFT                                     0
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE                                       (0x7<<3) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                 3
		#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG                                         (0x1<<6) /* BitField decision_rules Various decision rules	CQ negative arm indication updated via doorbell */
		#define USTORM_FCOE_AG_CONTEXT_CQ_ARM_N_FLAG_SHIFT                                   6
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED1                                           (0x1<<7) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_FCOE_AG_CONTEXT_RESERVED1_SHIFT                                     7
	u16_t __reserved2;
#endif
};


/*
 * The iscsi aggregative context of Ustorm
 */
struct ustorm_iscsi_ag_context
{
#if defined(__BIG_ENDIAN)
	u8_t __aux_counter_flags /* auxiliary counter flags */;
	u8_t agg_vars2;
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF                                                (0x3<<0) /* BitField agg_vars2 various aggregation variables	Set when a message was received from the Tx STORM. For future use. */
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_SHIFT                                          0
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF                                           (0x3<<2) /* BitField agg_vars2 various aggregation variables	Set when a message was received from the Timer. */
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_SHIFT                                     2
		#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE                                       (0x7<<4) /* BitField agg_vars2 various aggregation variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE_SHIFT                                 4
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK                                      (0x1<<7) /* BitField agg_vars2 various aggregation variables	Used to mask the decision rule of AggVal2. Used in iSCSI. Should be 0 in all other protocols */
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK_SHIFT                                7
	u8_t agg_vars1;
		#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                      (0x1<<0) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 0 */
		#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                0
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 1 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 2 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 3 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF                                               (0x3<<4) /* BitField agg_vars1 various aggregation variables	Indicates a valid invalidate request. Set by the CMP STORM. */
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_SHIFT                                         4
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF                                        (0x3<<6) /* BitField agg_vars1 various aggregation variables	Set when a message was received from the CMP STORM. For future use. */
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_SHIFT                                  6
	u8_t state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	u8_t state /* The state of the connection */;
	u8_t agg_vars1;
		#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                      (0x1<<0) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 0 */
		#define __USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                0
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 1 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 2 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1 various aggregation variables	The connection is currently registered to the QM with queue index 3 */
		#define USTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF                                               (0x3<<4) /* BitField agg_vars1 various aggregation variables	Indicates a valid invalidate request. Set by the CMP STORM. */
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_SHIFT                                         4
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF                                        (0x3<<6) /* BitField agg_vars1 various aggregation variables	Set when a message was received from the CMP STORM. For future use. */
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_SHIFT                                  6
	u8_t agg_vars2;
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF                                                (0x3<<0) /* BitField agg_vars2 various aggregation variables	Set when a message was received from the Tx STORM. For future use. */
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_SHIFT                                          0
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF                                           (0x3<<2) /* BitField agg_vars2 various aggregation variables	Set when a message was received from the Timer. */
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_SHIFT                                     2
		#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE                                       (0x7<<4) /* BitField agg_vars2 various aggregation variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_MISC4_RULE_SHIFT                                 4
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK                                      (0x1<<7) /* BitField agg_vars2 various aggregation variables	Used to mask the decision rule of AggVal2. Used in iSCSI. Should be 0 in all other protocols */
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_MASK_SHIFT                                7
	u8_t __aux_counter_flags /* auxiliary counter flags */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
	u8_t agg_misc2;
	u16_t __cq_local_comp_itt_val /* The local completion ITT to complete. Set by the CMP STORM RO for USTORM. */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __cq_local_comp_itt_val /* The local completion ITT to complete. Set by the CMP STORM RO for USTORM. */;
	u8_t agg_misc2;
	u8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
#endif
	u32_t agg_misc4;
#if defined(__BIG_ENDIAN)
	u8_t agg_val3_th;
	u8_t agg_val3;
	u16_t agg_misc3;
#elif defined(__LITTLE_ENDIAN)
	u16_t agg_misc3;
	u8_t agg_val3;
	u8_t agg_val3_th;
#endif
	u32_t agg_val1;
	u32_t agg_misc4_th;
#if defined(__BIG_ENDIAN)
	u16_t agg_val2_th;
	u16_t agg_val2;
#elif defined(__LITTLE_ENDIAN)
	u16_t agg_val2;
	u16_t agg_val2_th;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __reserved2;
	u8_t decision_rules;
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE                                        (0x7<<0) /* BitField decision_rules Various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE_SHIFT                                  0
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE                                      (0x7<<3) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                3
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG                                  (0x1<<6) /* BitField decision_rules Various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG_SHIFT                            6
		#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1                                          (0x1<<7) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1_SHIFT                                    7
	u8_t decision_rule_enable_bits;
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN                                            (0x1<<0) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN_SHIFT                                      0
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN                                     (0x1<<1) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN_SHIFT                               1
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN                                             (0x1<<2) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN_SHIFT                                       2
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN                                        (0x1<<3) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN_SHIFT                                  3
		#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN                                (0x1<<4) /* BitField decision_rule_enable_bits Enable bits for various decision rules	The local completion counter flag enable. Enabled by USTORM at the beginning. */
		#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN_SHIFT                          4
		#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                              (0x1<<5) /* BitField decision_rule_enable_bits Enable bits for various decision rules	The flush queues counter flag en.  */
		#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                        5
		#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN                                         (0x1<<6) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN_SHIFT                                   6
		#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN                                           (0x1<<7) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT                                     7
#elif defined(__LITTLE_ENDIAN)
	u8_t decision_rule_enable_bits;
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN                                            (0x1<<0) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_INV_CF_EN_SHIFT                                      0
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN                                     (0x1<<1) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_COMPLETION_CF_EN_SHIFT                               1
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN                                             (0x1<<2) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_TX_CF_EN_SHIFT                                       2
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN                                        (0x1<<3) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_TIMER_CF_EN_SHIFT                                  3
		#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN                                (0x1<<4) /* BitField decision_rule_enable_bits Enable bits for various decision rules	The local completion counter flag enable. Enabled by USTORM at the beginning. */
		#define __USTORM_ISCSI_AG_CONTEXT_CQ_LOCAL_COMP_CF_EN_SHIFT                          4
		#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN                              (0x1<<5) /* BitField decision_rule_enable_bits Enable bits for various decision rules	The flush queues counter flag en.  */
		#define __USTORM_ISCSI_AG_CONTEXT_QUEUES_FLUSH_Q0_CF_EN_SHIFT                        5
		#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN                                         (0x1<<6) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_AUX3_CF_EN_SHIFT                                   6
		#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN                                           (0x1<<7) /* BitField decision_rule_enable_bits Enable bits for various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT                                     7
	u8_t decision_rules;
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE                                        (0x7<<0) /* BitField decision_rules Various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_RULE_SHIFT                                  0
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE                                      (0x7<<3) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                3
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG                                  (0x1<<6) /* BitField decision_rules Various decision rules	 */
		#define USTORM_ISCSI_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG_SHIFT                            6
		#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1                                          (0x1<<7) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_ISCSI_AG_CONTEXT_RESERVED1_SHIFT                                    7
	u16_t __reserved2;
#endif
};


/*
 * The toe aggregative context of Ustorm
 */
struct ustorm_toe_ag_context
{
#if defined(__BIG_ENDIAN)
	u8_t __aux_counter_flags /* auxiliary counter flags */;
	u8_t __agg_vars2 /* various aggregation variables */;
	u8_t __agg_vars1 /* various aggregation variables */;
	u8_t __state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __state /* The state of the connection */;
	u8_t __agg_vars1 /* various aggregation variables */;
	u8_t __agg_vars2 /* various aggregation variables */;
	u8_t __aux_counter_flags /* auxiliary counter flags */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
	u8_t __agg_misc2;
	u16_t __agg_misc1;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_misc1;
	u8_t __agg_misc2;
	u8_t cdu_usage /* Will be used by the CDU for validation of the CID/connection type on doorbells. */;
#endif
	u32_t __agg_misc4;
#if defined(__BIG_ENDIAN)
	u8_t __agg_val3_th;
	u8_t __agg_val3;
	u16_t __agg_misc3;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_misc3;
	u8_t __agg_val3;
	u8_t __agg_val3_th;
#endif
	u32_t driver_doorbell_info_ptr_lo /* the host pointer that consist the struct of info updated */;
	u32_t driver_doorbell_info_ptr_hi /* the host pointer that consist the struct of info updated */;
#if defined(__BIG_ENDIAN)
	u16_t __agg_val2_th;
	u16_t rq_prod /* The RQ producer */;
#elif defined(__LITTLE_ENDIAN)
	u16_t rq_prod /* The RQ producer */;
	u16_t __agg_val2_th;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __reserved2;
	u8_t decision_rules;
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL2_RULE                                        (0x7<<0) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL2_RULE_SHIFT                                  0
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL3_RULE                                        (0x7<<3) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                  3
		#define USTORM_TOE_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG                                    (0x1<<6) /* BitField decision_rules Various decision rules	 */
		#define USTORM_TOE_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG_SHIFT                              6
		#define __USTORM_TOE_AG_CONTEXT_RESERVED1                                            (0x1<<7) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_RESERVED1_SHIFT                                      7
	u8_t __decision_rule_enable_bits /* Enable bits for various decision rules */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __decision_rule_enable_bits /* Enable bits for various decision rules */;
	u8_t decision_rules;
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL2_RULE                                        (0x7<<0) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL2_RULE_SHIFT                                  0
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL3_RULE                                        (0x7<<3) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_AGG_VAL3_RULE_SHIFT                                  3
		#define USTORM_TOE_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG                                    (0x1<<6) /* BitField decision_rules Various decision rules	 */
		#define USTORM_TOE_AG_CONTEXT_AGG_VAL2_ARM_N_FLAG_SHIFT                              6
		#define __USTORM_TOE_AG_CONTEXT_RESERVED1                                            (0x1<<7) /* BitField decision_rules Various decision rules	 */
		#define __USTORM_TOE_AG_CONTEXT_RESERVED1_SHIFT                                      7
	u16_t __reserved2;
#endif
};


/*
 * The eth aggregative context of Xstorm
 */
struct xstorm_eth_ag_context
{
	u32_t reserved0;
#if defined(__BIG_ENDIAN)
	u8_t cdu_reserved /* Used by the CDU for validation and debugging */;
	u8_t reserved2;
	u16_t reserved1;
#elif defined(__LITTLE_ENDIAN)
	u16_t reserved1;
	u8_t reserved2;
	u8_t cdu_reserved /* Used by the CDU for validation and debugging */;
#endif
	u32_t reserved3[30];
};


/*
 * The fcoe aggregative context section of Xstorm
 */
struct xstorm_fcoe_extra_ag_context_section
{
#if defined(__BIG_ENDIAN)
	u8_t tcp_agg_vars1;
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51                            (0x3<<0) /* BitField tcp_agg_vars1 Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51_SHIFT                      0
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                     (0x3<<2) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT               2
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                        (0x3<<4) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                  4
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN            (0x1<<6) /* BitField tcp_agg_vars1 Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN_SHIFT      6
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG           (0x1<<7) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG_SHIFT     7
	u8_t __reserved_da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	u16_t __mtu /* MSS used for nagle algorithm and for transmission */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __mtu /* MSS used for nagle algorithm and for transmission */;
	u8_t __reserved_da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	u8_t tcp_agg_vars1;
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51                            (0x3<<0) /* BitField tcp_agg_vars1 Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED51_SHIFT                      0
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                     (0x3<<2) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT               2
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                        (0x3<<4) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                  4
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN            (0x1<<6) /* BitField tcp_agg_vars1 Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_CLEAR_DA_TIMER_EN_SHIFT      6
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG           (0x1<<7) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_DA_EXPIRATION_FLAG_SHIFT     7
#endif
	u32_t snd_nxt /* The current sequence number to send */;
	u32_t tx_wnd /* The Current transmission window in bytes */;
	u32_t __reserved55 /* The current Send UNA sequence number */;
	u32_t local_adv_wnd /* The current local advertised window to FE. */;
#if defined(__BIG_ENDIAN)
	u8_t __agg_val8_th /* aggregated value 8 - threshold */;
	u8_t __tx_dest /* aggregated value 8 */;
	u16_t tcp_agg_vars2;
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57                            (0x1<<0) /* BitField tcp_agg_vars2 Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57_SHIFT                      0
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58                            (0x1<<1) /* BitField tcp_agg_vars2 Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58_SHIFT                      1
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59                            (0x1<<2) /* BitField tcp_agg_vars2 Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59_SHIFT                      2
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG                             (0x1<<3) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                       3
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG                             (0x1<<4) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                       4
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60                            (0x1<<5) /* BitField tcp_agg_vars2 Various aggregative variables	Enable DA for the specific connection */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60_SHIFT                      5
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN         (0x1<<6) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN_SHIFT   6
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                     (0x1<<7) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT               7
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN               (0x1<<8) /* BitField tcp_agg_vars2 Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN_SHIFT         8
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG                             (0x1<<9) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                       9
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF                            (0x3<<10) /* BitField tcp_agg_vars2 Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                      10
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                 (0x3<<12) /* BitField tcp_agg_vars2 Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT           12
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                    (0x3<<14) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT              14
#elif defined(__LITTLE_ENDIAN)
	u16_t tcp_agg_vars2;
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57                            (0x1<<0) /* BitField tcp_agg_vars2 Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED57_SHIFT                      0
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58                            (0x1<<1) /* BitField tcp_agg_vars2 Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED58_SHIFT                      1
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59                            (0x1<<2) /* BitField tcp_agg_vars2 Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED59_SHIFT                      2
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG                             (0x1<<3) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                       3
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG                             (0x1<<4) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                       4
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60                            (0x1<<5) /* BitField tcp_agg_vars2 Various aggregative variables	Enable DA for the specific connection */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED60_SHIFT                      5
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN         (0x1<<6) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_ACK_TO_FE_UPDATED_EN_SHIFT   6
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                     (0x1<<7) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT               7
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN               (0x1<<8) /* BitField tcp_agg_vars2 Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_RESERVED_TX_FIN_FLAG_EN_SHIFT         8
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG                             (0x1<<9) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                       9
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF                            (0x3<<10) /* BitField tcp_agg_vars2 Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                      10
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                 (0x3<<12) /* BitField tcp_agg_vars2 Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT           12
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                    (0x3<<14) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_FCOE_EXTRA_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT              14
	u8_t __tx_dest /* aggregated value 8 */;
	u8_t __agg_val8_th /* aggregated value 8 - threshold */;
#endif
	u32_t __sq_base_addr_lo /* The low page address which the SQ resides in host memory */;
	u32_t __sq_base_addr_hi /* The high page address which the SQ resides in host memory */;
	u32_t __xfrq_base_addr_lo /* The low page address which the XFRQ resides in host memory */;
	u32_t __xfrq_base_addr_hi /* The high page address which the XFRQ resides in host memory */;
#if defined(__BIG_ENDIAN)
	u16_t __xfrq_cons /* The XFRQ consumer */;
	u16_t __xfrq_prod /* The XFRQ producer, updated by Ustorm */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __xfrq_prod /* The XFRQ producer, updated by Ustorm */;
	u16_t __xfrq_cons /* The XFRQ consumer */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t __tcp_agg_vars5 /* Various aggregative variables */;
	u8_t __tcp_agg_vars4 /* Various aggregative variables */;
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
	u8_t __reserved_force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __reserved_force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
	u8_t __tcp_agg_vars4 /* Various aggregative variables */;
	u8_t __tcp_agg_vars5 /* Various aggregative variables */;
#endif
	u32_t __tcp_agg_vars6 /* Various aggregative variables */;
#if defined(__BIG_ENDIAN)
	u16_t __agg_misc6 /* Misc aggregated variable 6 */;
	u16_t __tcp_agg_vars7 /* Various aggregative variables */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __tcp_agg_vars7 /* Various aggregative variables */;
	u16_t __agg_misc6 /* Misc aggregated variable 6 */;
#endif
	u32_t __agg_val10 /* aggregated value 10 */;
	u32_t __agg_val10_th /* aggregated value 10 - threshold */;
#if defined(__BIG_ENDIAN)
	u16_t __reserved3;
	u8_t __reserved2;
	u8_t __da_only_cnt /* counts delayed acks and not window updates */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __da_only_cnt /* counts delayed acks and not window updates */;
	u8_t __reserved2;
	u16_t __reserved3;
#endif
};

/*
 * The fcoe aggregative context of Xstorm
 */
struct xstorm_fcoe_ag_context
{
#if defined(__BIG_ENDIAN)
	u16_t agg_val1 /* aggregated value 1 */;
	u8_t agg_vars1;
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                       (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                 0
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                       (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                 1
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51                                          (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51_SHIFT                                    2
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52                                          (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52_SHIFT                                    3
		#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN                                     (0x1<<4) /* BitField agg_vars1 Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                               4
		#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN                                              (0x1<<5) /* BitField agg_vars1 Various aggregative variables	Enables the nagle decision */
		#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN_SHIFT                                        5
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG                                       (0x1<<6) /* BitField agg_vars1 Various aggregative variables	Used for future indication by the Driver on a doorbell */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT                                 6
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN                              (0x1<<7) /* BitField agg_vars1 Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN_SHIFT                        7
	u8_t __state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __state /* The state of the connection */;
	u8_t agg_vars1;
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0                                       (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                 0
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1                                       (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define __XSTORM_FCOE_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                 1
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51                                          (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED51_SHIFT                                    2
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52                                          (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED52_SHIFT                                    3
		#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN                                     (0x1<<4) /* BitField agg_vars1 Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_FCOE_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                               4
		#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN                                              (0x1<<5) /* BitField agg_vars1 Various aggregative variables	Enables the nagle decision */
		#define XSTORM_FCOE_AG_CONTEXT_NAGLE_EN_SHIFT                                        5
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG                                       (0x1<<6) /* BitField agg_vars1 Various aggregative variables	Used for future indication by the Driver on a doorbell */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT                                 6
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN                              (0x1<<7) /* BitField agg_vars1 Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED_UNA_GT_NXT_EN_SHIFT                        7
	u16_t agg_val1 /* aggregated value 1 */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t cdu_reserved /* Used by the CDU for validation and debugging */;
	u8_t __agg_vars4 /* Various aggregative variables */;
	u8_t agg_vars3;
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                   (0x3F<<0) /* BitField agg_vars3 Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                             0
		#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF                                            (0x3<<6) /* BitField agg_vars3 Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF_SHIFT                                      6
	u8_t agg_vars2;
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF                                               (0x3<<0) /* BitField agg_vars2 Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_SHIFT                                         0
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN                                    (0x1<<2) /* BitField agg_vars2 Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT                              2
		#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG                                           (0x1<<3) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG_SHIFT                                     3
		#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG                                           (0x1<<4) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG_SHIFT                                     4
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1                                        (0x3<<5) /* BitField agg_vars2 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1_SHIFT                                  5
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN                                            (0x1<<7) /* BitField agg_vars2 Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                      7
#elif defined(__LITTLE_ENDIAN)
	u8_t agg_vars2;
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF                                               (0x3<<0) /* BitField agg_vars2 Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_SHIFT                                         0
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN                                    (0x1<<2) /* BitField agg_vars2 Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT                              2
		#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG                                           (0x1<<3) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX8_FLAG_SHIFT                                     3
		#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG                                           (0x1<<4) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX9_FLAG_SHIFT                                     4
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1                                        (0x3<<5) /* BitField agg_vars2 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE1_SHIFT                                  5
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN                                            (0x1<<7) /* BitField agg_vars2 Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_FCOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                      7
	u8_t agg_vars3;
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                   (0x3F<<0) /* BitField agg_vars3 Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                             0
		#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF                                            (0x3<<6) /* BitField agg_vars3 Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX19_CF_SHIFT                                      6
	u8_t __agg_vars4 /* Various aggregative variables */;
	u8_t cdu_reserved /* Used by the CDU for validation and debugging */;
#endif
	u32_t more_to_send /* The number of bytes left to send */;
#if defined(__BIG_ENDIAN)
	u16_t agg_vars5;
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5                                        (0x3<<0) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5_SHIFT                                  0
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                   (0x3F<<2) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                             2
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                   (0x3F<<8) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                             8
		#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE                                      (0x3<<14) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE_SHIFT                                14
	u16_t sq_cons /* The SQ consumer updated by Xstorm after consuming aother WQE */;
#elif defined(__LITTLE_ENDIAN)
	u16_t sq_cons /* The SQ consumer updated by Xstorm after consuming aother WQE */;
	u16_t agg_vars5;
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5                                        (0x3<<0) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE5_SHIFT                                  0
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                   (0x3F<<2) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                             2
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                   (0x3F<<8) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_FCOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                             8
		#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE                                      (0x3<<14) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_FCOE_AG_CONTEXT_CONFQ_DEC_RULE_SHIFT                                14
#endif
	struct xstorm_fcoe_extra_ag_context_section __extra_section /* Extra context section */;
#if defined(__BIG_ENDIAN)
	u16_t agg_vars7;
		#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE                             (0x7<<0) /* BitField agg_vars7 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT                       0
		#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG                                          (0x1<<3) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 13 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG_SHIFT                                    3
		#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF                                           (0x3<<4) /* BitField agg_vars7 Various aggregative variables	auxiliary counter flag 18 */
		#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF_SHIFT                                     4
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3                                        (0x3<<6) /* BitField agg_vars7 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3_SHIFT                                  6
		#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF                                               (0x3<<8) /* BitField agg_vars7 Various aggregative variables	auxiliary counter flag 1 */
		#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF_SHIFT                                         8
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62                                          (0x1<<10) /* BitField agg_vars7 Various aggregative variables	Mask the check of the completion sequence on retransmit */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62_SHIFT                                    10
		#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN                                          (0x1<<11) /* BitField agg_vars7 Various aggregative variables	Enable decision rules based on aux1_cf */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT                                    11
		#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG                                          (0x1<<12) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 10 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG_SHIFT                                    12
		#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG                                          (0x1<<13) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 11 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG_SHIFT                                    13
		#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG                                          (0x1<<14) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 12 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG_SHIFT                                    14
		#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG                                           (0x1<<15) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 2 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG_SHIFT                                     15
	u8_t agg_val3_th /* Aggregated value 3 - threshold */;
	u8_t agg_vars6;
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6                                        (0x7<<0) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6_SHIFT                                  0
		#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE                                       (0x7<<3) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE_SHIFT                                 3
		#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE                                         (0x3<<6) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE_SHIFT                                   6
#elif defined(__LITTLE_ENDIAN)
	u8_t agg_vars6;
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6                                        (0x7<<0) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE6_SHIFT                                  0
		#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE                                       (0x7<<3) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_FCOE_AG_CONTEXT_XFRQ_DEC_RULE_SHIFT                                 3
		#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE                                         (0x3<<6) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_FCOE_AG_CONTEXT_SQ_DEC_RULE_SHIFT                                   6
	u8_t agg_val3_th /* Aggregated value 3 - threshold */;
	u16_t agg_vars7;
		#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE                             (0x7<<0) /* BitField agg_vars7 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_FCOE_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT                       0
		#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG                                          (0x1<<3) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 13 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX13_FLAG_SHIFT                                    3
		#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF                                           (0x3<<4) /* BitField agg_vars7 Various aggregative variables	auxiliary counter flag 18 */
		#define __XSTORM_FCOE_AG_CONTEXT_QUEUE0_CF_SHIFT                                     4
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3                                        (0x3<<6) /* BitField agg_vars7 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_FCOE_AG_CONTEXT_DECISION_RULE3_SHIFT                                  6
		#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF                                               (0x3<<8) /* BitField agg_vars7 Various aggregative variables	auxiliary counter flag 1 */
		#define XSTORM_FCOE_AG_CONTEXT_AUX1_CF_SHIFT                                         8
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62                                          (0x1<<10) /* BitField agg_vars7 Various aggregative variables	Mask the check of the completion sequence on retransmit */
		#define __XSTORM_FCOE_AG_CONTEXT_RESERVED62_SHIFT                                    10
		#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN                                          (0x1<<11) /* BitField agg_vars7 Various aggregative variables	Enable decision rules based on aux1_cf */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX1_CF_EN_SHIFT                                    11
		#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG                                          (0x1<<12) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 10 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX10_FLAG_SHIFT                                    12
		#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG                                          (0x1<<13) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 11 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX11_FLAG_SHIFT                                    13
		#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG                                          (0x1<<14) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 12 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX12_FLAG_SHIFT                                    14
		#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG                                           (0x1<<15) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 2 */
		#define __XSTORM_FCOE_AG_CONTEXT_AUX2_FLAG_SHIFT                                     15
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_val11_th /* aggregated value 11 - threshold */;
	u16_t __agg_val11 /* aggregated value 11 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val11 /* aggregated value 11 */;
	u16_t __agg_val11_th /* aggregated value 11 - threshold */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t __reserved1;
	u8_t __agg_val6_th /* aggregated value 6 - threshold */;
	u16_t __agg_val9 /* aggregated value 9 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val9 /* aggregated value 9 */;
	u8_t __agg_val6_th /* aggregated value 6 - threshold */;
	u8_t __reserved1;
#endif
#if defined(__BIG_ENDIAN)
	u16_t confq_cons /* CONFQ Consumer */;
	u16_t confq_prod /* CONFQ Producer, updated by Ustorm - AggVal2 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t confq_prod /* CONFQ Producer, updated by Ustorm - AggVal2 */;
	u16_t confq_cons /* CONFQ Consumer */;
#endif
	u32_t agg_vars8;
		#define XSTORM_FCOE_AG_CONTEXT_AGG_MISC2                                             (0xFFFFFF<<0) /* BitField agg_vars8 Various aggregative variables	Misc aggregated variable 2 */
		#define XSTORM_FCOE_AG_CONTEXT_AGG_MISC2_SHIFT                                       0
		#define XSTORM_FCOE_AG_CONTEXT_AGG_MISC3                                             (0xFF<<24) /* BitField agg_vars8 Various aggregative variables	Misc aggregated variable 3 */
		#define XSTORM_FCOE_AG_CONTEXT_AGG_MISC3_SHIFT                                       24
#if defined(__BIG_ENDIAN)
	u16_t agg_misc0 /* Misc aggregated variable 0 */;
	u16_t sq_prod /* The SQ Producer updated by Xstorm after reading a bunch of WQEs into the context */;
#elif defined(__LITTLE_ENDIAN)
	u16_t sq_prod /* The SQ Producer updated by Xstorm after reading a bunch of WQEs into the context */;
	u16_t agg_misc0 /* Misc aggregated variable 0 */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t agg_val3 /* Aggregated value 3 */;
	u8_t agg_val6 /* Aggregated value 6 */;
	u8_t agg_val5_th /* Aggregated value 5 - threshold */;
	u8_t agg_val5 /* Aggregated value 5 */;
#elif defined(__LITTLE_ENDIAN)
	u8_t agg_val5 /* Aggregated value 5 */;
	u8_t agg_val5_th /* Aggregated value 5 - threshold */;
	u8_t agg_val6 /* Aggregated value 6 */;
	u8_t agg_val3 /* Aggregated value 3 */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
	u16_t agg_limit1 /* aggregated limit 1 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t agg_limit1 /* aggregated limit 1 */;
	u16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
#endif
	u32_t completion_seq /* The sequence number of the start completion point (BD) */;
	u32_t confq_pbl_base_lo /* The CONFQ PBL base low address resides in host memory */;
	u32_t confq_pbl_base_hi /* The CONFQ PBL base hihj address resides in host memory */;
};



/*
 * The tcp aggregative context section of Xstorm
 */
struct xstorm_tcp_tcp_ag_context_section
{
#if defined(__BIG_ENDIAN)
	u8_t tcp_agg_vars1;
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF                          (0x3<<0) /* BitField tcp_agg_vars1 Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF_SHIFT                    0
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                        (0x3<<2) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT                  2
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                           (0x3<<4) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                     4
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN                        (0x1<<6) /* BitField tcp_agg_vars1 Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN_SHIFT                  6
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG                       (0x1<<7) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG_SHIFT                 7
	u8_t __da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	u16_t mss /* MSS used for nagle algorithm and for transmission */;
#elif defined(__LITTLE_ENDIAN)
	u16_t mss /* MSS used for nagle algorithm and for transmission */;
	u8_t __da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	u8_t tcp_agg_vars1;
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF                          (0x3<<0) /* BitField tcp_agg_vars1 Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF_SHIFT                    0
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                        (0x3<<2) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT                  2
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                           (0x3<<4) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                     4
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN                        (0x1<<6) /* BitField tcp_agg_vars1 Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN_SHIFT                  6
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG                       (0x1<<7) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG_SHIFT                 7
#endif
	u32_t snd_nxt /* The current sequence number to send */;
	u32_t tx_wnd /* The Current transmission window in bytes */;
	u32_t snd_una /* The current Send UNA sequence number */;
	u32_t local_adv_wnd /* The current local advertised window to FE. */;
#if defined(__BIG_ENDIAN)
	u8_t __agg_val8_th /* aggregated value 8 - threshold */;
	u8_t __tx_dest /* aggregated value 8 */;
	u16_t tcp_agg_vars2;
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG                                (0x1<<0) /* BitField tcp_agg_vars2 Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_SHIFT                          0
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED                             (0x1<<1) /* BitField tcp_agg_vars2 Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED_SHIFT                       1
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE                          (0x1<<2) /* BitField tcp_agg_vars2 Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE_SHIFT                    2
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG                                (0x1<<3) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                          3
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG                                (0x1<<4) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                          4
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE                                  (0x1<<5) /* BitField tcp_agg_vars2 Various aggregative variables	Enable DA for the specific connection */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE_SHIFT                            5
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN                     (0x1<<6) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN_SHIFT               6
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                        (0x1<<7) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT                  7
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN                           (0x1<<8) /* BitField tcp_agg_vars2 Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN_SHIFT                     8
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG                                (0x1<<9) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                          9
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF                               (0x3<<10) /* BitField tcp_agg_vars2 Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                         10
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                    (0x3<<12) /* BitField tcp_agg_vars2 Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT              12
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                       (0x3<<14) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT                 14
#elif defined(__LITTLE_ENDIAN)
	u16_t tcp_agg_vars2;
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG                                (0x1<<0) /* BitField tcp_agg_vars2 Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_SHIFT                          0
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED                             (0x1<<1) /* BitField tcp_agg_vars2 Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED_SHIFT                       1
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE                          (0x1<<2) /* BitField tcp_agg_vars2 Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE_SHIFT                    2
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG                                (0x1<<3) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                          3
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG                                (0x1<<4) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                          4
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE                                  (0x1<<5) /* BitField tcp_agg_vars2 Various aggregative variables	Enable DA for the specific connection */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DA_ENABLE_SHIFT                            5
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN                     (0x1<<6) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN_SHIFT               6
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                        (0x1<<7) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT                  7
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN                           (0x1<<8) /* BitField tcp_agg_vars2 Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN_SHIFT                     8
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG                                (0x1<<9) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                          9
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF                               (0x3<<10) /* BitField tcp_agg_vars2 Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                         10
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                    (0x3<<12) /* BitField tcp_agg_vars2 Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT              12
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                       (0x3<<14) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT                 14
	u8_t __tx_dest /* aggregated value 8 */;
	u8_t __agg_val8_th /* aggregated value 8 - threshold */;
#endif
	u32_t ack_to_far_end /* The ACK sequence to send to far end */;
	u32_t rto_timer /* The RTO timer value */;
	u32_t ka_timer /* The KA timer value */;
	u32_t ts_to_echo /* The time stamp value to echo to far end */;
#if defined(__BIG_ENDIAN)
	u16_t __agg_val7_th /* aggregated value 7 - threshold */;
	u16_t __agg_val7 /* aggregated value 7 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val7 /* aggregated value 7 */;
	u16_t __agg_val7_th /* aggregated value 7 - threshold */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t __tcp_agg_vars5 /* Various aggregative variables */;
	u8_t __tcp_agg_vars4 /* Various aggregative variables */;
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
	u8_t __force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
	u8_t __tcp_agg_vars4 /* Various aggregative variables */;
	u8_t __tcp_agg_vars5 /* Various aggregative variables */;
#endif
	u32_t tcp_agg_vars6;
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_CF_EN                         (0x1<<0) /* BitField tcp_agg_vars6 Various aggregative variables	Enable decision rules based on aux7_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_CF_EN_SHIFT                   0
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_EN                    (0x1<<1) /* BitField tcp_agg_vars6 Various aggregative variables	Enable decision rules based on aux8_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_EN_SHIFT              1
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF_EN                               (0x1<<2) /* BitField tcp_agg_vars6 Various aggregative variables	Enable decision rules based on aux9_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF_EN_SHIFT                         2
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN                              (0x1<<3) /* BitField tcp_agg_vars6 Various aggregative variables	Enable decision rules based on aux10_cf */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT                        3
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX6_FLAG                                (0x1<<4) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary flag 6 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX6_FLAG_SHIFT                          4
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX7_FLAG                                (0x1<<5) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary flag 7 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX7_FLAG_SHIFT                          5
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX5_CF                                  (0x3<<6) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 5 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX5_CF_SHIFT                            6
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF                                  (0x3<<8) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 9 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX9_CF_SHIFT                            8
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF                                 (0x3<<10) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 10 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX10_CF_SHIFT                           10
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF                                 (0x3<<12) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 11 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX11_CF_SHIFT                           12
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF                                 (0x3<<14) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 12 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX12_CF_SHIFT                           14
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX13_CF                                 (0x3<<16) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 13 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX13_CF_SHIFT                           16
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX14_CF                                 (0x3<<18) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 14 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX14_CF_SHIFT                           18
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX15_CF                                 (0x3<<20) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 15 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX15_CF_SHIFT                           20
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX16_CF                                 (0x3<<22) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 16 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX16_CF_SHIFT                           22
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX17_CF                                 (0x3<<24) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 17 */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_AUX17_CF_SHIFT                           24
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ECE_FLAG                                   (0x1<<26) /* BitField tcp_agg_vars6 Various aggregative variables	Can be also used as general purpose if ECN is not used */
		#define XSTORM_TCP_TCP_AG_CONTEXT_SECTION_ECE_FLAG_SHIFT                             26
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED71                               (0x1<<27) /* BitField tcp_agg_vars6 Various aggregative variables	Can be also used as general purpose if ECN is not used */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_RESERVED71_SHIFT                         27
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_FORCE_PURE_ACK_CNT_DIRTY                 (0x1<<28) /* BitField tcp_agg_vars6 Various aggregative variables	This flag is set if the Force ACK count is set by the TSTORM. On QM output it is cleared. */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_FORCE_PURE_ACK_CNT_DIRTY_SHIFT           28
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TCP_AUTO_STOP_FLAG                       (0x1<<29) /* BitField tcp_agg_vars6 Various aggregative variables	Indicates that the connection is in autostop mode */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_TCP_AUTO_STOP_FLAG_SHIFT                 29
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DO_TS_UPDATE_FLAG                        (0x1<<30) /* BitField tcp_agg_vars6 Various aggregative variables	This bit uses like a one shot that the TSTORM fires and the XSTORM arms. Used to allow a single TS update for each transmission */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_DO_TS_UPDATE_FLAG_SHIFT                  30
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CANCEL_RETRANSMIT_FLAG                   (0x1<<31) /* BitField tcp_agg_vars6 Various aggregative variables	This bit is set by the TSTORM when need to cancel precious fast retransmit */
		#define __XSTORM_TCP_TCP_AG_CONTEXT_SECTION_CANCEL_RETRANSMIT_FLAG_SHIFT             31
#if defined(__BIG_ENDIAN)
	u16_t __agg_misc6 /* Misc aggregated variable 6 */;
	u16_t __tcp_agg_vars7 /* Various aggregative variables */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __tcp_agg_vars7 /* Various aggregative variables */;
	u16_t __agg_misc6 /* Misc aggregated variable 6 */;
#endif
	u32_t __agg_val10 /* aggregated value 10 */;
	u32_t __agg_val10_th /* aggregated value 10 - threshold */;
#if defined(__BIG_ENDIAN)
	u16_t __reserved3;
	u8_t __reserved2;
	u8_t __da_only_cnt /* counts delayed acks and not window updates */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __da_only_cnt /* counts delayed acks and not window updates */;
	u8_t __reserved2;
	u16_t __reserved3;
#endif
};

/*
 * The iscsi aggregative context of Xstorm
 */
struct xstorm_iscsi_ag_context
{
#if defined(__BIG_ENDIAN)
	u16_t agg_val1 /* aggregated value 1 */;
	u8_t agg_vars1;
		#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                      (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                0
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN                                    (0x1<<4) /* BitField agg_vars1 Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                              4
		#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN                                             (0x1<<5) /* BitField agg_vars1 Various aggregative variables	Enables the nagle decision */
		#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN_SHIFT                                       5
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG                                      (0x1<<6) /* BitField agg_vars1 Various aggregative variables	Used for future indication by the Driver on a doorbell */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT                                6
		#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN                                      (0x1<<7) /* BitField agg_vars1 Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN_SHIFT                                7
	u8_t state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	u8_t state /* The state of the connection */;
	u8_t agg_vars1;
		#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0                                      (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                0
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1                                        (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM1_SHIFT                                  1
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2                                        (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM2_SHIFT                                  2
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3                                        (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define XSTORM_ISCSI_AG_CONTEXT_EXISTS_IN_QM3_SHIFT                                  3
		#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN                                    (0x1<<4) /* BitField agg_vars1 Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_ISCSI_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                              4
		#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN                                             (0x1<<5) /* BitField agg_vars1 Various aggregative variables	Enables the nagle decision */
		#define XSTORM_ISCSI_AG_CONTEXT_NAGLE_EN_SHIFT                                       5
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG                                      (0x1<<6) /* BitField agg_vars1 Various aggregative variables	Used for future indication by the Driver on a doorbell */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_SHIFT                                6
		#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN                                      (0x1<<7) /* BitField agg_vars1 Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_ISCSI_AG_CONTEXT_UNA_GT_NXT_EN_SHIFT                                7
	u16_t agg_val1 /* aggregated value 1 */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t cdu_reserved /* Used by the CDU for validation and debugging */;
	u8_t __agg_vars4 /* Various aggregative variables */;
	u8_t agg_vars3;
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                  (0x3F<<0) /* BitField agg_vars3 Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                            0
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF                                        (0x3<<6) /* BitField agg_vars3 Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF_SHIFT                                  6
	u8_t agg_vars2;
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF                                              (0x3<<0) /* BitField agg_vars2 Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_SHIFT                                        0
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN                                   (0x1<<2) /* BitField agg_vars2 Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT                             2
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG                                          (0x1<<3) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG_SHIFT                                    3
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG                                          (0x1<<4) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG_SHIFT                                    4
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1                                       (0x3<<5) /* BitField agg_vars2 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1_SHIFT                                 5
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN                                           (0x1<<7) /* BitField agg_vars2 Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT                                     7
#elif defined(__LITTLE_ENDIAN)
	u8_t agg_vars2;
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF                                              (0x3<<0) /* BitField agg_vars2 Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_SHIFT                                        0
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN                                   (0x1<<2) /* BitField agg_vars2 Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_SPARE_FLAG_EN_SHIFT                             2
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG                                          (0x1<<3) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX8_FLAG_SHIFT                                    3
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG                                          (0x1<<4) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX9_FLAG_SHIFT                                    4
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1                                       (0x3<<5) /* BitField agg_vars2 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE1_SHIFT                                 5
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN                                           (0x1<<7) /* BitField agg_vars2 Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_ISCSI_AG_CONTEXT_DQ_CF_EN_SHIFT                                     7
	u8_t agg_vars3;
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                  (0x3F<<0) /* BitField agg_vars3 Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                            0
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF                                        (0x3<<6) /* BitField agg_vars3 Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_TS_EN_CF_SHIFT                                  6
	u8_t __agg_vars4 /* Various aggregative variables */;
	u8_t cdu_reserved /* Used by the CDU for validation and debugging */;
#endif
	u32_t more_to_send /* The number of bytes left to send */;
#if defined(__BIG_ENDIAN)
	u16_t agg_vars5;
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5                                       (0x3<<0) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5_SHIFT                                 0
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                  (0x3F<<2) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                            2
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                  (0x3F<<8) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                            8
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2                                       (0x3<<14) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2_SHIFT                                 14
	u16_t sq_cons /* aggregated value 4 - threshold */;
#elif defined(__LITTLE_ENDIAN)
	u16_t sq_cons /* aggregated value 4 - threshold */;
	u16_t agg_vars5;
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5                                       (0x3<<0) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE5_SHIFT                                 0
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                  (0x3F<<2) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                            2
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                  (0x3F<<8) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                            8
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2                                       (0x3<<14) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE2_SHIFT                                 14
#endif
	struct xstorm_tcp_tcp_ag_context_section tcp /* TCP context section, shared in TOE and ISCSI */;
#if defined(__BIG_ENDIAN)
	u16_t agg_vars7;
		#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE                            (0x7<<0) /* BitField agg_vars7 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT                      0
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG                                         (0x1<<3) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 13 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG_SHIFT                                   3
		#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF                                     (0x3<<4) /* BitField agg_vars7 Various aggregative variables	Sync Tstorm and Xstorm */
		#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF_SHIFT                               4
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3                                       (0x3<<6) /* BitField agg_vars7 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3_SHIFT                                 6
		#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF                                              (0x3<<8) /* BitField agg_vars7 Various aggregative variables	auxiliary counter flag 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_SHIFT                                        8
		#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK                       (0x1<<10) /* BitField agg_vars7 Various aggregative variables	Mask the check of the completion sequence on retransmit */
		#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK_SHIFT                 10
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN                                         (0x1<<11) /* BitField agg_vars7 Various aggregative variables	Enable decision rules based on aux1_cf */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN_SHIFT                                   11
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG                                         (0x1<<12) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 10 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG_SHIFT                                   12
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG                                         (0x1<<13) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 11 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG_SHIFT                                   13
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG                                         (0x1<<14) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 12 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG_SHIFT                                   14
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN                                      (0x1<<15) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 2 */
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN_SHIFT                                15
	u8_t agg_val3_th /* Aggregated value 3 - threshold */;
	u8_t agg_vars6;
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6                                       (0x7<<0) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6_SHIFT                                 0
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7                                       (0x7<<3) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7_SHIFT                                 3
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4                                       (0x3<<6) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4_SHIFT                                 6
#elif defined(__LITTLE_ENDIAN)
	u8_t agg_vars6;
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6                                       (0x7<<0) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE6_SHIFT                                 0
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7                                       (0x7<<3) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE7_SHIFT                                 3
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4                                       (0x3<<6) /* BitField agg_vars6 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE4_SHIFT                                 6
	u8_t agg_val3_th /* Aggregated value 3 - threshold */;
	u16_t agg_vars7;
		#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE                            (0x7<<0) /* BitField agg_vars7 Various aggregative variables	0-NOP,1-EQ,2-NEQ,3-GT_CYC,4-GT_ABS,5-LT_CYC,6-LT_ABS */
		#define __XSTORM_ISCSI_AG_CONTEXT_AGG_VAL11_DECISION_RULE_SHIFT                      0
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG                                         (0x1<<3) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 13 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX13_FLAG_SHIFT                                   3
		#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF                                     (0x3<<4) /* BitField agg_vars7 Various aggregative variables	Sync Tstorm and Xstorm */
		#define __XSTORM_ISCSI_AG_CONTEXT_STORMS_SYNC_CF_SHIFT                               4
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3                                       (0x3<<6) /* BitField agg_vars7 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_ISCSI_AG_CONTEXT_DECISION_RULE3_SHIFT                                 6
		#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF                                              (0x3<<8) /* BitField agg_vars7 Various aggregative variables	auxiliary counter flag 1 */
		#define XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_SHIFT                                        8
		#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK                       (0x1<<10) /* BitField agg_vars7 Various aggregative variables	Mask the check of the completion sequence on retransmit */
		#define __XSTORM_ISCSI_AG_CONTEXT_COMPLETION_SEQ_DECISION_MASK_SHIFT                 10
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN                                         (0x1<<11) /* BitField agg_vars7 Various aggregative variables	Enable decision rules based on aux1_cf */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX1_CF_EN_SHIFT                                   11
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG                                         (0x1<<12) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 10 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX10_FLAG_SHIFT                                   12
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG                                         (0x1<<13) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 11 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX11_FLAG_SHIFT                                   13
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG                                         (0x1<<14) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 12 */
		#define __XSTORM_ISCSI_AG_CONTEXT_AUX12_FLAG_SHIFT                                   14
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN                                      (0x1<<15) /* BitField agg_vars7 Various aggregative variables	auxiliary flag 2 */
		#define __XSTORM_ISCSI_AG_CONTEXT_RX_WND_SCL_EN_SHIFT                                15
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_val11_th /* aggregated value 11 - threshold */;
	u16_t __gen_data /* Used for Iscsi. In connection establishment, it uses as rxMss, and in connection termination, it uses as command Id: 1=L5CM_TX_ACK_ON_FIN_CMD 2=L5CM_SET_MSL_TIMER_CMD 3=L5CM_TX_RST_CMD */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __gen_data /* Used for Iscsi. In connection establishment, it uses as rxMss, and in connection termination, it uses as command Id: 1=L5CM_TX_ACK_ON_FIN_CMD 2=L5CM_SET_MSL_TIMER_CMD 3=L5CM_TX_RST_CMD */;
	u16_t __agg_val11_th /* aggregated value 11 - threshold */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t __reserved1;
	u8_t __agg_val6_th /* aggregated value 6 - threshold */;
	u16_t __agg_val9 /* aggregated value 9 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val9 /* aggregated value 9 */;
	u8_t __agg_val6_th /* aggregated value 6 - threshold */;
	u8_t __reserved1;
#endif
#if defined(__BIG_ENDIAN)
	u16_t hq_prod /* The HQ producer threashold to compare the HQ consumer, which is the current HQ producer +1 - AggVal2Th */;
	u16_t hq_cons /* HQ Consumer, updated by Cstorm - AggVal2 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t hq_cons /* HQ Consumer, updated by Cstorm - AggVal2 */;
	u16_t hq_prod /* The HQ producer threashold to compare the HQ consumer, which is the current HQ producer +1 - AggVal2Th */;
#endif
	u32_t agg_vars8;
		#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC2                                            (0xFFFFFF<<0) /* BitField agg_vars8 Various aggregative variables	Misc aggregated variable 2 */
		#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC2_SHIFT                                      0
		#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC3                                            (0xFF<<24) /* BitField agg_vars8 Various aggregative variables	Misc aggregated variable 3 */
		#define XSTORM_ISCSI_AG_CONTEXT_AGG_MISC3_SHIFT                                      24
#if defined(__BIG_ENDIAN)
	u16_t r2tq_prod /* Misc aggregated variable 0 */;
	u16_t sq_prod /* SQ Producer */;
#elif defined(__LITTLE_ENDIAN)
	u16_t sq_prod /* SQ Producer */;
	u16_t r2tq_prod /* Misc aggregated variable 0 */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t agg_val3 /* Aggregated value 3 */;
	u8_t agg_val6 /* Aggregated value 6 */;
	u8_t agg_val5_th /* Aggregated value 5 - threshold */;
	u8_t agg_val5 /* Aggregated value 5 */;
#elif defined(__LITTLE_ENDIAN)
	u8_t agg_val5 /* Aggregated value 5 */;
	u8_t agg_val5_th /* Aggregated value 5 - threshold */;
	u8_t agg_val6 /* Aggregated value 6 */;
	u8_t agg_val3 /* Aggregated value 3 */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
	u16_t agg_limit1 /* aggregated limit 1 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t agg_limit1 /* aggregated limit 1 */;
	u16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
#endif
	u32_t hq_cons_tcp_seq /* TCP sequence of the HQ BD pointed by hq_cons */;
	u32_t exp_stat_sn /* expected status SN, updated by Ustorm */;
	u32_t rst_seq_num /* spare aggregated variable 5 */;
};



/*
 * The toe aggregative context section of Xstorm
 */
struct xstorm_toe_tcp_ag_context_section
{
#if defined(__BIG_ENDIAN)
	u8_t tcp_agg_vars1;
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF                          (0x3<<0) /* BitField tcp_agg_vars1 Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF_SHIFT                    0
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                        (0x3<<2) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT                  2
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                           (0x3<<4) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                     4
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN                        (0x1<<6) /* BitField tcp_agg_vars1 Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN_SHIFT                  6
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG                       (0x1<<7) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG_SHIFT                 7
	u8_t __da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	u16_t mss /* MSS used for nagle algorithm and for transmission */;
#elif defined(__LITTLE_ENDIAN)
	u16_t mss /* MSS used for nagle algorithm and for transmission */;
	u8_t __da_cnt /* Counts the number of ACK requests received from the TSTORM with no registration to QM. */;
	u8_t tcp_agg_vars1;
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF                          (0x3<<0) /* BitField tcp_agg_vars1 Various aggregative variables	Counter flag used to rewind the DA timer */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_DA_TIMER_CF_SHIFT                    0
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED                        (0x3<<2) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 2 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_SHIFT                  2
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF                           (0x3<<4) /* BitField tcp_agg_vars1 Various aggregative variables	auxiliary counter flag 3 */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_SHIFT                     4
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN                        (0x1<<6) /* BitField tcp_agg_vars1 Various aggregative variables	If set enables sending clear commands as port of the DA decision rules */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CLEAR_DA_TIMER_EN_SHIFT                  6
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG                       (0x1<<7) /* BitField tcp_agg_vars1 Various aggregative variables	Indicates that there was a delayed ack timer expiration */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_EXPIRATION_FLAG_SHIFT                 7
#endif
	u32_t snd_nxt /* The current sequence number to send */;
	u32_t tx_wnd /* The Current transmission window in bytes */;
	u32_t snd_una /* The current Send UNA sequence number */;
	u32_t local_adv_wnd /* The current local advertised window to FE. */;
#if defined(__BIG_ENDIAN)
	u8_t __agg_val8_th /* aggregated value 8 - threshold */;
	u8_t __tx_dest /* aggregated value 8 */;
	u16_t tcp_agg_vars2;
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG                                (0x1<<0) /* BitField tcp_agg_vars2 Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_SHIFT                          0
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED                             (0x1<<1) /* BitField tcp_agg_vars2 Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED_SHIFT                       1
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE                          (0x1<<2) /* BitField tcp_agg_vars2 Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE_SHIFT                    2
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX3_FLAG                                (0x1<<3) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                          3
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX4_FLAG                                (0x1<<4) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                          4
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_ENABLE                                  (0x1<<5) /* BitField tcp_agg_vars2 Various aggregative variables	Enable DA for the specific connection */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_ENABLE_SHIFT                            5
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN                     (0x1<<6) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN_SHIFT               6
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                        (0x1<<7) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT                  7
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN                           (0x1<<8) /* BitField tcp_agg_vars2 Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN_SHIFT                     8
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX1_FLAG                                (0x1<<9) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                          9
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_RTO_CF                               (0x3<<10) /* BitField tcp_agg_vars2 Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                         10
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                    (0x3<<12) /* BitField tcp_agg_vars2 Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT              12
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                       (0x3<<14) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT                 14
#elif defined(__LITTLE_ENDIAN)
	u16_t tcp_agg_vars2;
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG                                (0x1<<0) /* BitField tcp_agg_vars2 Various aggregative variables	Used in TOE to indicate that FIN is sent on a BD to bypass the naggle rule */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_SHIFT                          0
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED                             (0x1<<1) /* BitField tcp_agg_vars2 Various aggregative variables	Enables the tx window based decision */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_UNBLOCKED_SHIFT                       1
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE                          (0x1<<2) /* BitField tcp_agg_vars2 Various aggregative variables	The DA Timer status. If set indicates that the delayed ACK timer is active. */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_TIMER_ACTIVE_SHIFT                    2
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX3_FLAG                                (0x1<<3) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 3 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX3_FLAG_SHIFT                          3
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX4_FLAG                                (0x1<<4) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 4 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX4_FLAG_SHIFT                          4
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_ENABLE                                  (0x1<<5) /* BitField tcp_agg_vars2 Various aggregative variables	Enable DA for the specific connection */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DA_ENABLE_SHIFT                            5
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN                     (0x1<<6) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux2_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ACK_TO_FE_UPDATED_EN_SHIFT               6
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN                        (0x1<<7) /* BitField tcp_agg_vars2 Various aggregative variables	Enable decision rules based on aux3_cf */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SIDEBAND_SENT_CF_EN_SHIFT                  7
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN                           (0x1<<8) /* BitField tcp_agg_vars2 Various aggregative variables	Enable Decision rule based on tx_fin_flag */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_FIN_FLAG_EN_SHIFT                     8
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX1_FLAG                                (0x1<<9) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary flag 1 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX1_FLAG_SHIFT                          9
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_RTO_CF                               (0x3<<10) /* BitField tcp_agg_vars2 Various aggregative variables	counter flag for setting the rto timer */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_SET_RTO_CF_SHIFT                         10
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF                    (0x3<<12) /* BitField tcp_agg_vars2 Various aggregative variables	timestamp was updated counter flag */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_UPDATED_CF_SHIFT              12
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF                       (0x3<<14) /* BitField tcp_agg_vars2 Various aggregative variables	auxiliary counter flag 8 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_SHIFT                 14
	u8_t __tx_dest /* aggregated value 8 */;
	u8_t __agg_val8_th /* aggregated value 8 - threshold */;
#endif
	u32_t ack_to_far_end /* The ACK sequence to send to far end */;
	u32_t rto_timer /* The RTO timer value */;
	u32_t ka_timer /* The KA timer value */;
	u32_t ts_to_echo /* The time stamp value to echo to far end */;
#if defined(__BIG_ENDIAN)
	u16_t __agg_val7_th /* aggregated value 7 - threshold */;
	u16_t __agg_val7 /* aggregated value 7 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val7 /* aggregated value 7 */;
	u16_t __agg_val7_th /* aggregated value 7 - threshold */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t __tcp_agg_vars5 /* Various aggregative variables */;
	u8_t __tcp_agg_vars4 /* Various aggregative variables */;
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
	u8_t __force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __force_pure_ack_cnt /* The number of force ACK commands arrived from the TSTORM */;
	u8_t __tcp_agg_vars3 /* Various aggregative variables */;
	u8_t __tcp_agg_vars4 /* Various aggregative variables */;
	u8_t __tcp_agg_vars5 /* Various aggregative variables */;
#endif
	u32_t tcp_agg_vars6;
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_CF_EN                         (0x1<<0) /* BitField tcp_agg_vars6 Various aggregative variables	Enable decision rules based on aux7_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TS_TO_ECHO_CF_EN_SHIFT                   0
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_EN                    (0x1<<1) /* BitField tcp_agg_vars6 Various aggregative variables	Enable decision rules based on aux8_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TX_DEST_UPDATED_CF_EN_SHIFT              1
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX9_CF_EN                               (0x1<<2) /* BitField tcp_agg_vars6 Various aggregative variables	Enable decision rules based on aux9_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX9_CF_EN_SHIFT                         2
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN                              (0x1<<3) /* BitField tcp_agg_vars6 Various aggregative variables	Enable decision rules based on aux10_cf */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF_EN_SHIFT                        3
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX6_FLAG                                (0x1<<4) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary flag 6 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX6_FLAG_SHIFT                          4
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX7_FLAG                                (0x1<<5) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary flag 7 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX7_FLAG_SHIFT                          5
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX5_CF                                  (0x3<<6) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 5 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX5_CF_SHIFT                            6
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX9_CF                                  (0x3<<8) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 9 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX9_CF_SHIFT                            8
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF                                 (0x3<<10) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 10 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX10_CF_SHIFT                           10
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX11_CF                                 (0x3<<12) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 11 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX11_CF_SHIFT                           12
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX12_CF                                 (0x3<<14) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 12 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX12_CF_SHIFT                           14
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX13_CF                                 (0x3<<16) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 13 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX13_CF_SHIFT                           16
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX14_CF                                 (0x3<<18) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 14 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX14_CF_SHIFT                           18
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX15_CF                                 (0x3<<20) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 15 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX15_CF_SHIFT                           20
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX16_CF                                 (0x3<<22) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 16 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX16_CF_SHIFT                           22
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX17_CF                                 (0x3<<24) /* BitField tcp_agg_vars6 Various aggregative variables	auxiliary counter flag 17 */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_AUX17_CF_SHIFT                           24
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ECE_FLAG                                   (0x1<<26) /* BitField tcp_agg_vars6 Various aggregative variables	Can be also used as general purpose if ECN is not used */
		#define XSTORM_TOE_TCP_AG_CONTEXT_SECTION_ECE_FLAG_SHIFT                             26
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED71                               (0x1<<27) /* BitField tcp_agg_vars6 Various aggregative variables	Can be also used as general purpose if ECN is not used */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_RESERVED71_SHIFT                         27
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_FORCE_PURE_ACK_CNT_DIRTY                 (0x1<<28) /* BitField tcp_agg_vars6 Various aggregative variables	This flag is set if the Force ACK count is set by the TSTORM. On QM output it is cleared. */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_FORCE_PURE_ACK_CNT_DIRTY_SHIFT           28
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TCP_AUTO_STOP_FLAG                       (0x1<<29) /* BitField tcp_agg_vars6 Various aggregative variables	Indicates that the connection is in autostop mode */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_TCP_AUTO_STOP_FLAG_SHIFT                 29
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DO_TS_UPDATE_FLAG                        (0x1<<30) /* BitField tcp_agg_vars6 Various aggregative variables	This bit uses like a one shot that the TSTORM fires and the XSTORM arms. Used to allow a single TS update for each transmission */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_DO_TS_UPDATE_FLAG_SHIFT                  30
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CANCEL_RETRANSMIT_FLAG                   (0x1<<31) /* BitField tcp_agg_vars6 Various aggregative variables	This bit is set by the TSTORM when need to cancel precious fast retransmit */
		#define __XSTORM_TOE_TCP_AG_CONTEXT_SECTION_CANCEL_RETRANSMIT_FLAG_SHIFT             31
#if defined(__BIG_ENDIAN)
	u16_t __agg_misc6 /* Misc aggregated variable 6 */;
	u16_t __tcp_agg_vars7 /* Various aggregative variables */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __tcp_agg_vars7 /* Various aggregative variables */;
	u16_t __agg_misc6 /* Misc aggregated variable 6 */;
#endif
	u32_t __agg_val10 /* aggregated value 10 */;
	u32_t __agg_val10_th /* aggregated value 10 - threshold */;
#if defined(__BIG_ENDIAN)
	u16_t __reserved3;
	u8_t __reserved2;
	u8_t __da_only_cnt /* counts delayed acks and not window updates */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __da_only_cnt /* counts delayed acks and not window updates */;
	u8_t __reserved2;
	u16_t __reserved3;
#endif
};

/*
 * The toe aggregative context of Xstorm
 */
struct xstorm_toe_ag_context
{
#if defined(__BIG_ENDIAN)
	u16_t agg_val1 /* aggregated value 1 */;
	u8_t agg_vars1;
		#define __XSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0                                        (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                  0
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED50                                           (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED50_SHIFT                                     1
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED51                                           (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED51_SHIFT                                     2
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED52                                           (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED52_SHIFT                                     3
		#define __XSTORM_TOE_AG_CONTEXT_MORE_TO_SEND_EN                                      (0x1<<4) /* BitField agg_vars1 Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_TOE_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                                4
		#define XSTORM_TOE_AG_CONTEXT_NAGLE_EN                                               (0x1<<5) /* BitField agg_vars1 Various aggregative variables	Enables the nagle decision */
		#define XSTORM_TOE_AG_CONTEXT_NAGLE_EN_SHIFT                                         5
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG                                        (0x1<<6) /* BitField agg_vars1 Various aggregative variables	used to indicate last doorbell for specific connection */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_SHIFT                                  6
		#define __XSTORM_TOE_AG_CONTEXT_UNA_GT_NXT_EN                                        (0x1<<7) /* BitField agg_vars1 Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_TOE_AG_CONTEXT_UNA_GT_NXT_EN_SHIFT                                  7
	u8_t __state /* The state of the connection */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __state /* The state of the connection */;
	u8_t agg_vars1;
		#define __XSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0                                        (0x1<<0) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 0 */
		#define __XSTORM_TOE_AG_CONTEXT_EXISTS_IN_QM0_SHIFT                                  0
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED50                                           (0x1<<1) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 1 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED50_SHIFT                                     1
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED51                                           (0x1<<2) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 2 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED51_SHIFT                                     2
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED52                                           (0x1<<3) /* BitField agg_vars1 Various aggregative variables	The connection is currently registered to the QM with queue index 3 */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED52_SHIFT                                     3
		#define __XSTORM_TOE_AG_CONTEXT_MORE_TO_SEND_EN                                      (0x1<<4) /* BitField agg_vars1 Various aggregative variables	Enables the decision rule of more_to_Send > 0 */
		#define __XSTORM_TOE_AG_CONTEXT_MORE_TO_SEND_EN_SHIFT                                4
		#define XSTORM_TOE_AG_CONTEXT_NAGLE_EN                                               (0x1<<5) /* BitField agg_vars1 Various aggregative variables	Enables the nagle decision */
		#define XSTORM_TOE_AG_CONTEXT_NAGLE_EN_SHIFT                                         5
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG                                        (0x1<<6) /* BitField agg_vars1 Various aggregative variables	used to indicate last doorbell for specific connection */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_SHIFT                                  6
		#define __XSTORM_TOE_AG_CONTEXT_UNA_GT_NXT_EN                                        (0x1<<7) /* BitField agg_vars1 Various aggregative variables	Enable decision rules based on equality between snd_una and snd_nxt */
		#define __XSTORM_TOE_AG_CONTEXT_UNA_GT_NXT_EN_SHIFT                                  7
	u16_t agg_val1 /* aggregated value 1 */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t cdu_reserved /* Used by the CDU for validation and debugging */;
	u8_t __agg_vars4 /* Various aggregative variables */;
	u8_t agg_vars3;
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                    (0x3F<<0) /* BitField agg_vars3 Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                              0
		#define __XSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q1_CF                                   (0x3<<6) /* BitField agg_vars3 Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q1_CF_SHIFT                             6
	u8_t agg_vars2;
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF                                                (0x3<<0) /* BitField agg_vars2 Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_SHIFT                                          0
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_EN                                     (0x1<<2) /* BitField agg_vars2 Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_EN_SHIFT                               2
		#define __XSTORM_TOE_AG_CONTEXT_AUX8_FLAG                                            (0x1<<3) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_TOE_AG_CONTEXT_AUX8_FLAG_SHIFT                                      3
		#define __XSTORM_TOE_AG_CONTEXT_AUX9_FLAG                                            (0x1<<4) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_TOE_AG_CONTEXT_AUX9_FLAG_SHIFT                                      4
		#define XSTORM_TOE_AG_CONTEXT_RESERVED53                                             (0x3<<5) /* BitField agg_vars2 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_TOE_AG_CONTEXT_RESERVED53_SHIFT                                       5
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_EN                                             (0x1<<7) /* BitField agg_vars2 Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                       7
#elif defined(__LITTLE_ENDIAN)
	u8_t agg_vars2;
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF                                                (0x3<<0) /* BitField agg_vars2 Various aggregative variables	auxiliary counter flag 4 */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_SHIFT                                          0
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_EN                                     (0x1<<2) /* BitField agg_vars2 Various aggregative variables	Enable decision rule based on dq_spare_flag */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_FLUSH_FLAG_EN_SHIFT                               2
		#define __XSTORM_TOE_AG_CONTEXT_AUX8_FLAG                                            (0x1<<3) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 8 */
		#define __XSTORM_TOE_AG_CONTEXT_AUX8_FLAG_SHIFT                                      3
		#define __XSTORM_TOE_AG_CONTEXT_AUX9_FLAG                                            (0x1<<4) /* BitField agg_vars2 Various aggregative variables	auxiliary flag 9 */
		#define __XSTORM_TOE_AG_CONTEXT_AUX9_FLAG_SHIFT                                      4
		#define XSTORM_TOE_AG_CONTEXT_RESERVED53                                             (0x3<<5) /* BitField agg_vars2 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define XSTORM_TOE_AG_CONTEXT_RESERVED53_SHIFT                                       5
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_EN                                             (0x1<<7) /* BitField agg_vars2 Various aggregative variables	Enable decision rules based on aux4_cf */
		#define __XSTORM_TOE_AG_CONTEXT_DQ_CF_EN_SHIFT                                       7
	u8_t agg_vars3;
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2                                    (0x3F<<0) /* BitField agg_vars3 Various aggregative variables	The physical queue number of queue index 2 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM2_SHIFT                              0
		#define __XSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q1_CF                                   (0x3<<6) /* BitField agg_vars3 Various aggregative variables	auxiliary counter flag 19 */
		#define __XSTORM_TOE_AG_CONTEXT_QUEUES_FLUSH_Q1_CF_SHIFT                             6
	u8_t __agg_vars4 /* Various aggregative variables */;
	u8_t cdu_reserved /* Used by the CDU for validation and debugging */;
#endif
	u32_t more_to_send /* The number of bytes left to send */;
#if defined(__BIG_ENDIAN)
	u16_t agg_vars5;
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED54                                           (0x3<<0) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED54_SHIFT                                     0
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                    (0x3F<<2) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                              2
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                    (0x3F<<8) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                              8
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED56                                           (0x3<<14) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED56_SHIFT                                     14
	u16_t __agg_val4_th /* aggregated value 4 - threshold */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val4_th /* aggregated value 4 - threshold */;
	u16_t agg_vars5;
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED54                                           (0x3<<0) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED54_SHIFT                                     0
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0                                    (0x3F<<2) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 0 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM0_SHIFT                              2
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1                                    (0x3F<<8) /* BitField agg_vars5 Various aggregative variables	The physical queue number of queue index 1 */
		#define XSTORM_TOE_AG_CONTEXT_PHYSICAL_QUEUE_NUM1_SHIFT                              8
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED56                                           (0x3<<14) /* BitField agg_vars5 Various aggregative variables	0-NOP,1-EQ,2-NEQ */
		#define __XSTORM_TOE_AG_CONTEXT_RESERVED56_SHIFT                                     14
#endif
	struct xstorm_toe_tcp_ag_context_section tcp /* TCP context section, shared in TOE and ISCSI */;
#if defined(__BIG_ENDIAN)
	u16_t __agg_vars7 /* Various aggregative variables */;
	u8_t __agg_val3_th /* Aggregated value 3 - threshold */;
	u8_t __agg_vars6 /* Various aggregative variables */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __agg_vars6 /* Various aggregative variables */;
	u8_t __agg_val3_th /* Aggregated value 3 - threshold */;
	u16_t __agg_vars7 /* Various aggregative variables */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_val11_th /* aggregated value 11 - threshold */;
	u16_t __agg_val11 /* aggregated value 11 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val11 /* aggregated value 11 */;
	u16_t __agg_val11_th /* aggregated value 11 - threshold */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t __reserved1;
	u8_t __agg_val6_th /* aggregated value 6 - threshold */;
	u16_t __agg_val9 /* aggregated value 9 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val9 /* aggregated value 9 */;
	u8_t __agg_val6_th /* aggregated value 6 - threshold */;
	u8_t __reserved1;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_val2_th /* Aggregated value 2 - threshold */;
	u16_t cmp_bd_cons /* BD Consumer from the Completor */;
#elif defined(__LITTLE_ENDIAN)
	u16_t cmp_bd_cons /* BD Consumer from the Completor */;
	u16_t __agg_val2_th /* Aggregated value 2 - threshold */;
#endif
	u32_t __agg_vars8 /* Various aggregative variables */;
#if defined(__BIG_ENDIAN)
	u16_t __agg_misc0 /* Misc aggregated variable 0 */;
	u16_t __agg_val4 /* aggregated value 4 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __agg_val4 /* aggregated value 4 */;
	u16_t __agg_misc0 /* Misc aggregated variable 0 */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t __agg_val3 /* Aggregated value 3 */;
	u8_t __agg_val6 /* Aggregated value 6 */;
	u8_t __agg_val5_th /* Aggregated value 5 - threshold */;
	u8_t __agg_val5 /* Aggregated value 5 */;
#elif defined(__LITTLE_ENDIAN)
	u8_t __agg_val5 /* Aggregated value 5 */;
	u8_t __agg_val5_th /* Aggregated value 5 - threshold */;
	u8_t __agg_val6 /* Aggregated value 6 */;
	u8_t __agg_val3 /* Aggregated value 3 */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
	u16_t __bd_ind_max_val /* modulo value for bd_prod */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __bd_ind_max_val /* modulo value for bd_prod */;
	u16_t __agg_misc1 /* Spare value for aggregation. NOTE: this value is used in the retransmit decision rule if CmpSeqDecMask is 0. In that case it is intended to be CmpBdSize. */;
#endif
	u32_t cmp_bd_start_seq /* The sequence number of the start completion point (BD) */;
	u32_t cmp_bd_page_0_to_31 /* Misc aggregated variable 4 */;
	u32_t cmp_bd_page_32_to_63 /* spare aggregated variable 5 */;
};



/*
 * doorbell message sent to the chip
 */
struct doorbell
{
#if defined(__BIG_ENDIAN)
	u16_t zero_fill2 /* driver must zero this field! */;
	u8_t zero_fill1 /* driver must zero this field! */;
	struct doorbell_hdr_t header;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t header;
	u8_t zero_fill1 /* driver must zero this field! */;
	u16_t zero_fill2 /* driver must zero this field! */;
#endif
};



/*
 * doorbell message sent to the chip
 */
struct doorbell_set_prod
{
#if defined(__BIG_ENDIAN)
	u16_t prod /* Producer index to be set */;
	u8_t zero_fill1 /* driver must zero this field! */;
	struct doorbell_hdr_t header;
#elif defined(__LITTLE_ENDIAN)
	struct doorbell_hdr_t header;
	u8_t zero_fill1 /* driver must zero this field! */;
	u16_t prod /* Producer index to be set */;
#endif
};


struct regpair_t
{
	u32_t lo /* low word for reg-pair */;
	u32_t hi /* high word for reg-pair */;
};






/*
 * Classify rule opcodes in E2/E3
 */
enum classify_rule
{
	CLASSIFY_RULE_OPCODE_MAC=0 /* Add/remove a MAC address */,
	CLASSIFY_RULE_OPCODE_VLAN=1 /* Add/remove a VLAN */,
	CLASSIFY_RULE_OPCODE_PAIR=2 /* Add/remove a MAC-VLAN pair */,
	MAX_CLASSIFY_RULE
};


/*
 * Classify rule types in E2/E3
 */
enum classify_rule_action_type
{
	CLASSIFY_RULE_REMOVE=0,
	CLASSIFY_RULE_ADD=1,
	MAX_CLASSIFY_RULE_ACTION_TYPE
};


/*
 * client init fc data $$KEEP_ENDIANNESS$$
 */
struct client_init_fc_data
{
	u16_t cqe_pause_thr_low /* number of remaining cqes under which, we send pause message */;
	u16_t cqe_pause_thr_high /* number of remaining cqes above which, we send un-pause message */;
	u16_t bd_pause_thr_low /* number of remaining bds under which, we send pause message */;
	u16_t bd_pause_thr_high /* number of remaining bds above which, we send un-pause message */;
	u16_t sge_pause_thr_low /* number of remaining sges under which, we send pause message */;
	u16_t sge_pause_thr_high /* number of remaining sges above which, we send un-pause message */;
	u16_t rx_cos_mask /* the bits that will be set on pfc/ safc paket whith will be genratet when this ring is full. for regular flow control set this to 1 */;
	u8_t safc_group_num /* When safc_group_en is set, this is the physical queue num that will be choosen. Otherwise, it is the regular ETh queue (Only three bits are used) */;
	u8_t safc_group_en_flg;
	u8_t traffic_type /* use enum traffic_type */;
	u8_t reserved0;
	u16_t reserved1;
	u32_t reserved2;
};


/*
 * client init ramrod data $$KEEP_ENDIANNESS$$
 */
struct client_init_general_data
{
	u8_t client_id /* client_id */;
	u8_t statistics_counter_id /* statistics counter id */;
	u8_t statistics_en_flg /* statistics en flg */;
	u8_t is_fcoe_flg /* is this an fcoe connection. (1 bit is used) */;
	u8_t activate_flg /* if 0 - the client is deactivate else the client is activate client (1 bit is used) */;
	u8_t sp_client_id /* the slow path rings client Id. */;
	u16_t mtu /* Host MTU from client config */;
	u8_t statistics_zero_flg /* if set FW will reset the statistic counter of this client */;
	u8_t func_id /* PCI function ID (0-71) */;
	u16_t reserved0;
	u32_t reserved1;
};


/*
 * client init rx data $$KEEP_ENDIANNESS$$
 */
struct client_init_rx_data
{
	u8_t tpa_en;
		#define CLIENT_INIT_RX_DATA_TPA_EN_IPV4                                              (0x1<<0) /* BitField tpa_en tpa_enable	tpa enable flg ipv4 */
		#define CLIENT_INIT_RX_DATA_TPA_EN_IPV4_SHIFT                                        0
		#define CLIENT_INIT_RX_DATA_TPA_EN_IPV6                                              (0x1<<1) /* BitField tpa_en tpa_enable	tpa enable flg ipv6 */
		#define CLIENT_INIT_RX_DATA_TPA_EN_IPV6_SHIFT                                        1
		#define CLIENT_INIT_RX_DATA_RESERVED5                                                (0x3F<<2) /* BitField tpa_en tpa_enable	 */
		#define CLIENT_INIT_RX_DATA_RESERVED5_SHIFT                                          2
	u8_t vmqueue_mode_en_flg /* If set, working in VMQueue mode (always consume one sge) */;
	u8_t extra_data_over_sgl_en_flg /* if set, put over sgl data from end of input message */;
	u8_t cache_line_alignment_log_size /* The log size of cache line alignment in bytes. Must be a power of 2. */;
	u8_t enable_dynamic_hc /* If set, dynamic HC is enabled */;
	u8_t max_sges_for_packet /* The maximal number of SGEs that can be used for one packet. depends on MTU and SGE size. must be 0 if SGEs are disabled */;
	u8_t client_qzone_id /* used in E2 only, to specify the HW queue zone ID used for this client rx producers */;
	u8_t drop_ip_cs_err_flg /* If set, this client drops packets with IP checksum error */;
	u8_t drop_tcp_cs_err_flg /* If set, this client drops packets with TCP checksum error */;
	u8_t drop_ttl0_flg /* If set, this client drops packets with TTL=0 */;
	u8_t drop_udp_cs_err_flg /* If set, this client drops packets with UDP checksum error */;
	u8_t inner_vlan_removal_enable_flg /* If set, inner VLAN removal is enabled for this client */;
	u8_t outer_vlan_removal_enable_flg /* If set, outer VLAN removal is enabled for this client */;
	u8_t status_block_id /* rx status block id */;
	u8_t rx_sb_index_number /* status block indices */;
	u8_t reserved0;
	u8_t max_tpa_queues /* maximal TPA queues allowed for this client */;
	u8_t silent_vlan_removal_flg /* if set, and the vlan is equal to requested vlan according to mask, the vlan will be remove without notifying the driver */;
	u16_t max_bytes_on_bd /* Maximum bytes that can be placed on a BD. The BD allocated size should include 2 more bytes (ip alignment) and alignment size (in case the address is not aligned) */;
	u16_t sge_buff_size /* Size of the buffers pointed by SGEs */;
	u8_t approx_mcast_engine_id /* In Everest2, if is_approx_mcast is set, this field specified which approximate multicast engine is associate with this client */;
	u8_t rss_engine_id /* In Everest2, if rss_mode is set, this field specified which RSS engine is associate with this client */;
	struct regpair_t bd_page_base /* BD page base address at the host */;
	struct regpair_t sge_page_base /* SGE page base address at the host */;
	struct regpair_t cqe_page_base /* Completion queue base address */;
	u8_t is_leading_rss;
	u8_t is_approx_mcast;
	u16_t max_agg_size /* maximal size for the aggregated TPA packets, reprted by the host */;
	u16_t state;
		#define CLIENT_INIT_RX_DATA_UCAST_DROP_ALL                                           (0x1<<0) /* BitField state rx filters state	drop all unicast packets */
		#define CLIENT_INIT_RX_DATA_UCAST_DROP_ALL_SHIFT                                     0
		#define CLIENT_INIT_RX_DATA_UCAST_ACCEPT_ALL                                         (0x1<<1) /* BitField state rx filters state	accept all unicast packets (subject to vlan) */
		#define CLIENT_INIT_RX_DATA_UCAST_ACCEPT_ALL_SHIFT                                   1
		#define CLIENT_INIT_RX_DATA_UCAST_ACCEPT_UNMATCHED                                   (0x1<<2) /* BitField state rx filters state	accept all unmatched unicast packets (subject to vlan) */
		#define CLIENT_INIT_RX_DATA_UCAST_ACCEPT_UNMATCHED_SHIFT                             2
		#define CLIENT_INIT_RX_DATA_MCAST_DROP_ALL                                           (0x1<<3) /* BitField state rx filters state	drop all multicast packets */
		#define CLIENT_INIT_RX_DATA_MCAST_DROP_ALL_SHIFT                                     3
		#define CLIENT_INIT_RX_DATA_MCAST_ACCEPT_ALL                                         (0x1<<4) /* BitField state rx filters state	accept all multicast packets (subject to vlan) */
		#define CLIENT_INIT_RX_DATA_MCAST_ACCEPT_ALL_SHIFT                                   4
		#define CLIENT_INIT_RX_DATA_BCAST_ACCEPT_ALL                                         (0x1<<5) /* BitField state rx filters state	accept all broadcast packets (subject to vlan) */
		#define CLIENT_INIT_RX_DATA_BCAST_ACCEPT_ALL_SHIFT                                   5
		#define CLIENT_INIT_RX_DATA_ACCEPT_ANY_VLAN                                          (0x1<<6) /* BitField state rx filters state	accept packets matched only by MAC (without checking vlan) */
		#define CLIENT_INIT_RX_DATA_ACCEPT_ANY_VLAN_SHIFT                                    6
		#define CLIENT_INIT_RX_DATA_RESERVED2                                                (0x1FF<<7) /* BitField state rx filters state	 */
		#define CLIENT_INIT_RX_DATA_RESERVED2_SHIFT                                          7
	u16_t silent_vlan_value /* The vlan to compare, in case, silent vlan is set */;
	u16_t silent_vlan_mask /* The vlan mask, in case, silent vlan is set */;
	u16_t reserved3;
	u32_t reserved4;
};

/*
 * client init tx data $$KEEP_ENDIANNESS$$
 */
struct client_init_tx_data
{
	u8_t enforce_security_flg /* if set, security checks will be made for this connection */;
	u8_t tx_status_block_id /* the number of status block to update */;
	u8_t tx_sb_index_number /* the index to use inside the status block */;
	u8_t tss_leading_client_id /* client ID of the leading TSS client, for TX classification source knock out */;
	u8_t tx_switching_flg /* if set, tx switching will be done to packets on this connection */;
	u8_t anti_spoofing_flg /* if set, anti spoofing check will be done to packets on this connection */;
	u16_t default_vlan /* default vlan tag (id+pri). (valid if default_vlan_flg is set) */;
	struct regpair_t tx_bd_page_base /* BD page base address at the host for TxBdCons */;
	u16_t state;
		#define CLIENT_INIT_TX_DATA_UCAST_ACCEPT_ALL                                         (0x1<<0) /* BitField state tx filters state	accept all unicast packets (subject to vlan) */
		#define CLIENT_INIT_TX_DATA_UCAST_ACCEPT_ALL_SHIFT                                   0
		#define CLIENT_INIT_TX_DATA_MCAST_ACCEPT_ALL                                         (0x1<<1) /* BitField state tx filters state	accept all multicast packets (subject to vlan) */
		#define CLIENT_INIT_TX_DATA_MCAST_ACCEPT_ALL_SHIFT                                   1
		#define CLIENT_INIT_TX_DATA_BCAST_ACCEPT_ALL                                         (0x1<<2) /* BitField state tx filters state	accept all broadcast packets (subject to vlan) */
		#define CLIENT_INIT_TX_DATA_BCAST_ACCEPT_ALL_SHIFT                                   2
		#define CLIENT_INIT_TX_DATA_ACCEPT_ANY_VLAN                                          (0x1<<3) /* BitField state tx filters state	accept packets matched only by MAC (without checking vlan) */
		#define CLIENT_INIT_TX_DATA_ACCEPT_ANY_VLAN_SHIFT                                    3
		#define CLIENT_INIT_TX_DATA_RESERVED1                                                (0xFFF<<4) /* BitField state tx filters state	 */
		#define CLIENT_INIT_TX_DATA_RESERVED1_SHIFT                                          4
	u8_t default_vlan_flg /* is default vlan valid for this client. */;
	u8_t reserved2;
	u32_t reserved3;
};

/*
 * client init ramrod data $$KEEP_ENDIANNESS$$
 */
struct client_init_ramrod_data
{
	struct client_init_general_data general /* client init general data */;
	struct client_init_rx_data rx /* client init rx data */;
	struct client_init_tx_data tx /* client init tx data */;
	struct client_init_fc_data fc /* client init flow control data */;
};




/*
 * client update ramrod data $$KEEP_ENDIANNESS$$
 */
struct client_update_ramrod_data
{
	u8_t client_id /* the client to update */;
	u8_t func_id /* PCI function ID this client belongs to (0-71) */;
	u8_t inner_vlan_removal_enable_flg /* If set, inner VLAN removal is enabled for this client, will be change according to change flag */;
	u8_t inner_vlan_removal_change_flg /* If set, inner VLAN removal flag will be set according to the enable flag */;
	u8_t outer_vlan_removal_enable_flg /* If set, outer VLAN removal is enabled for this client, will be change according to change flag */;
	u8_t outer_vlan_removal_change_flg /* If set, outer VLAN removal flag will be set according to the enable flag */;
	u8_t anti_spoofing_enable_flg /* If set, anti spoofing is enabled for this client, will be change according to change flag */;
	u8_t anti_spoofing_change_flg /* If set, anti spoofing flag will be set according to anti spoofing flag */;
	u8_t activate_flg /* if 0 - the client is deactivate else the client is activate client (1 bit is used) */;
	u8_t activate_change_flg /* If set, activate_flg will be checked */;
	u16_t default_vlan /* default vlan tag (id+pri). (valid if default_vlan_flg is set) */;
	u8_t default_vlan_enable_flg;
	u8_t default_vlan_change_flg;
	u16_t silent_vlan_value /* The vlan to compare, in case, silent vlan is set */;
	u16_t silent_vlan_mask /* The vlan mask, in case, silent vlan is set */;
	u8_t silent_vlan_removal_flg /* if set, and the vlan is equal to requested vlan according to mask, the vlan will be remove without notifying the driver */;
	u8_t silent_vlan_change_flg;
	u32_t echo /* echo value to be sent to driver on event ring */;
};


/*
 * The eth storm context of Cstorm
 */
struct cstorm_eth_st_context
{
	u32_t __reserved0[4];
};


struct double_regpair
{
	u32_t regpair0_lo /* low word for reg-pair0 */;
	u32_t regpair0_hi /* high word for reg-pair0 */;
	u32_t regpair1_lo /* low word for reg-pair1 */;
	u32_t regpair1_hi /* high word for reg-pair1 */;
};


/*
 * Ethernet address typesm used in ethernet tx BDs
 */
enum eth_addr_type
{
	UNKNOWN_ADDRESS=0,
	UNICAST_ADDRESS=1,
	MULTICAST_ADDRESS=2,
	BROADCAST_ADDRESS=3,
	MAX_ETH_ADDR_TYPE
};


/*
 *  $$KEEP_ENDIANNESS$$
 */
struct eth_classify_cmd_header
{
	u8_t cmd_general_data;
		#define ETH_CLASSIFY_CMD_HEADER_RX_CMD                                               (0x1<<0) /* BitField cmd_general_data 	should this cmd be applied for Rx */
		#define ETH_CLASSIFY_CMD_HEADER_RX_CMD_SHIFT                                         0
		#define ETH_CLASSIFY_CMD_HEADER_TX_CMD                                               (0x1<<1) /* BitField cmd_general_data 	should this cmd be applied for Tx */
		#define ETH_CLASSIFY_CMD_HEADER_TX_CMD_SHIFT                                         1
		#define ETH_CLASSIFY_CMD_HEADER_OPCODE                                               (0x3<<2) /* BitField cmd_general_data 	command opcode for MAC/VLAN/PAIR - use enum classify_rule */
		#define ETH_CLASSIFY_CMD_HEADER_OPCODE_SHIFT                                         2
		#define ETH_CLASSIFY_CMD_HEADER_IS_ADD                                               (0x1<<4) /* BitField cmd_general_data 	use enum classify_rule_action_type */
		#define ETH_CLASSIFY_CMD_HEADER_IS_ADD_SHIFT                                         4
		#define ETH_CLASSIFY_CMD_HEADER_RESERVED0                                            (0x7<<5) /* BitField cmd_general_data 	 */
		#define ETH_CLASSIFY_CMD_HEADER_RESERVED0_SHIFT                                      5
	u8_t func_id /* the function id */;
	u8_t client_id;
	u8_t reserved1;
};


/*
 * header for eth classification config ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_classify_header
{
	u8_t rule_cnt /* number of rules in classification config ramrod */;
	u8_t reserved0;
	u16_t reserved1;
	u32_t echo /* echo value to be sent to driver on event ring */;
};


/*
 * Command for adding/removing a MAC classification rule $$KEEP_ENDIANNESS$$
 */
struct eth_classify_mac_cmd
{
	struct eth_classify_cmd_header header;
	u32_t reserved0;
	u16_t mac_lsb;
	u16_t mac_mid;
	u16_t mac_msb;
	u16_t reserved1;
};


/*
 * Command for adding/removing a MAC-VLAN pair classification rule $$KEEP_ENDIANNESS$$
 */
struct eth_classify_pair_cmd
{
	struct eth_classify_cmd_header header;
	u32_t reserved0;
	u16_t mac_lsb;
	u16_t mac_mid;
	u16_t mac_msb;
	u16_t vlan;
};


/*
 * Command for adding/removing a VLAN classification rule $$KEEP_ENDIANNESS$$
 */
struct eth_classify_vlan_cmd
{
	struct eth_classify_cmd_header header;
	u32_t reserved0;
	u32_t reserved1;
	u16_t reserved2;
	u16_t vlan;
};

/*
 * union for eth classification rule $$KEEP_ENDIANNESS$$
 */
union eth_classify_rule_cmd
{
	struct eth_classify_mac_cmd mac;
	struct eth_classify_vlan_cmd vlan;
	struct eth_classify_pair_cmd pair;
};

/*
 * parameters for eth classification configuration ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_classify_rules_ramrod_data
{
	struct eth_classify_header header;
	union eth_classify_rule_cmd rules[CLASSIFY_RULES_COUNT];
};




/*
 * The data contain client ID need to the ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_common_ramrod_data
{
	u32_t client_id /* id of this client. (5 bits are used) */;
	u32_t reserved1;
};


/*
 * The eth storm context of Ustorm
 */
struct ustorm_eth_st_context
{
	u32_t reserved0[52];
};

/*
 * The eth storm context of Tstorm
 */
struct tstorm_eth_st_context
{
	u32_t __reserved0[28];
};

/*
 * The eth storm context of Xstorm
 */
struct xstorm_eth_st_context
{
	u32_t reserved0[60];
};

/*
 * Ethernet connection context 
 */
struct eth_context
{
	struct ustorm_eth_st_context ustorm_st_context /* Ustorm storm context */;
	struct tstorm_eth_st_context tstorm_st_context /* Tstorm storm context */;
	struct xstorm_eth_ag_context xstorm_ag_context /* Xstorm aggregative context */;
	struct tstorm_eth_ag_context tstorm_ag_context /* Tstorm aggregative context */;
	struct cstorm_eth_ag_context cstorm_ag_context /* Cstorm aggregative context */;
	struct ustorm_eth_ag_context ustorm_ag_context /* Ustorm aggregative context */;
	struct timers_block_context timers_context /* Timers block context */;
	struct xstorm_eth_st_context xstorm_st_context /* Xstorm storm context */;
	struct cstorm_eth_st_context cstorm_st_context /* Cstorm storm context */;
};


/*
 * union for sgl and raw data.
 */
union eth_sgl_or_raw_data
{
	u16_t sgl[8] /* Scatter-gather list of SGEs used by this packet. This list includes the indices of the SGEs. */;
	u32_t raw_data[4] /* raw data from Tstorm to the driver. */;
};

/*
 * eth FP end aggregation CQE parameters struct $$KEEP_ENDIANNESS$$
 */
struct eth_end_agg_rx_cqe
{
	u8_t type_error_flags;
		#define ETH_END_AGG_RX_CQE_TYPE                                                      (0x3<<0) /* BitField type_error_flags 	use enum eth_rx_cqe_type */
		#define ETH_END_AGG_RX_CQE_TYPE_SHIFT                                                0
		#define ETH_END_AGG_RX_CQE_SGL_RAW_SEL                                               (0x1<<2) /* BitField type_error_flags 	use enum eth_rx_fp_sel */
		#define ETH_END_AGG_RX_CQE_SGL_RAW_SEL_SHIFT                                         2
		#define ETH_END_AGG_RX_CQE_RESERVED0                                                 (0x1F<<3) /* BitField type_error_flags 	 */
		#define ETH_END_AGG_RX_CQE_RESERVED0_SHIFT                                           3
	u8_t reserved1;
	u8_t queue_index /* The aggregation queue index of this packet */;
	u8_t reserved2;
	u32_t timestamp_delta /* timestamp delta between first packet to last packet in aggregation */;
	u16_t num_of_coalesced_segs /* Num of coalesced segments. */;
	u16_t pkt_len /* Packet length */;
	u8_t pure_ack_count /* Number of pure acks coalesced. */;
	u8_t reserved3;
	u16_t reserved4;
	union eth_sgl_or_raw_data sgl_or_raw_data /* union for sgl and raw data. */;
};


/*
 * regular eth FP CQE parameters struct $$KEEP_ENDIANNESS$$
 */
struct eth_fast_path_rx_cqe
{
	u8_t type_error_flags;
		#define ETH_FAST_PATH_RX_CQE_TYPE                                                    (0x3<<0) /* BitField type_error_flags 	use enum eth_rx_cqe_type */
		#define ETH_FAST_PATH_RX_CQE_TYPE_SHIFT                                              0
		#define ETH_FAST_PATH_RX_CQE_SGL_RAW_SEL                                             (0x1<<2) /* BitField type_error_flags 	use enum eth_rx_fp_sel */
		#define ETH_FAST_PATH_RX_CQE_SGL_RAW_SEL_SHIFT                                       2
		#define ETH_FAST_PATH_RX_CQE_PHY_DECODE_ERR_FLG                                      (0x1<<3) /* BitField type_error_flags 	Physical layer errors */
		#define ETH_FAST_PATH_RX_CQE_PHY_DECODE_ERR_FLG_SHIFT                                3
		#define ETH_FAST_PATH_RX_CQE_IP_BAD_XSUM_FLG                                         (0x1<<4) /* BitField type_error_flags 	IP checksum error */
		#define ETH_FAST_PATH_RX_CQE_IP_BAD_XSUM_FLG_SHIFT                                   4
		#define ETH_FAST_PATH_RX_CQE_L4_BAD_XSUM_FLG                                         (0x1<<5) /* BitField type_error_flags 	TCP/UDP checksum error */
		#define ETH_FAST_PATH_RX_CQE_L4_BAD_XSUM_FLG_SHIFT                                   5
		#define ETH_FAST_PATH_RX_CQE_RESERVED0                                               (0x3<<6) /* BitField type_error_flags 	 */
		#define ETH_FAST_PATH_RX_CQE_RESERVED0_SHIFT                                         6
	u8_t status_flags;
		#define ETH_FAST_PATH_RX_CQE_RSS_HASH_TYPE                                           (0x7<<0) /* BitField status_flags 	use enum eth_rss_hash_type */
		#define ETH_FAST_PATH_RX_CQE_RSS_HASH_TYPE_SHIFT                                     0
		#define ETH_FAST_PATH_RX_CQE_RSS_HASH_FLG                                            (0x1<<3) /* BitField status_flags 	RSS hashing on/off */
		#define ETH_FAST_PATH_RX_CQE_RSS_HASH_FLG_SHIFT                                      3
		#define ETH_FAST_PATH_RX_CQE_BROADCAST_FLG                                           (0x1<<4) /* BitField status_flags 	if set to 1, this is a broadcast packet */
		#define ETH_FAST_PATH_RX_CQE_BROADCAST_FLG_SHIFT                                     4
		#define ETH_FAST_PATH_RX_CQE_MAC_MATCH_FLG                                           (0x1<<5) /* BitField status_flags 	if set to 1, the MAC address was matched in the tstorm CAM search */
		#define ETH_FAST_PATH_RX_CQE_MAC_MATCH_FLG_SHIFT                                     5
		#define ETH_FAST_PATH_RX_CQE_IP_XSUM_NO_VALIDATION_FLG                               (0x1<<6) /* BitField status_flags 	IP checksum validation was not performed (if packet is not IPv4) */
		#define ETH_FAST_PATH_RX_CQE_IP_XSUM_NO_VALIDATION_FLG_SHIFT                         6
		#define ETH_FAST_PATH_RX_CQE_L4_XSUM_NO_VALIDATION_FLG                               (0x1<<7) /* BitField status_flags 	TCP/UDP checksum validation was not performed (if packet is not TCP/UDP or IPv6 extheaders exist) */
		#define ETH_FAST_PATH_RX_CQE_L4_XSUM_NO_VALIDATION_FLG_SHIFT                         7
	u8_t queue_index /* The aggregation queue index of this packet */;
	u8_t placement_offset /* Placement offset from the start of the BD, in bytes */;
	u32_t rss_hash_result /* RSS toeplitz hash result */;
	u16_t vlan_tag /* Ethernet VLAN tag field */;
	u16_t pkt_len /* Packet length */;
	u16_t len_on_bd /* Number of bytes placed on the BD */;
	struct parsing_flags pars_flags;
	union eth_sgl_or_raw_data sgl_or_raw_data /* union for sgl and raw data. */;
};


/*
 * Command for setting classification flags for a client $$KEEP_ENDIANNESS$$
 */
struct eth_filter_rules_cmd
{
	u8_t cmd_general_data;
		#define ETH_FILTER_RULES_CMD_RX_CMD                                                  (0x1<<0) /* BitField cmd_general_data 	should this cmd be applied for Rx */
		#define ETH_FILTER_RULES_CMD_RX_CMD_SHIFT                                            0
		#define ETH_FILTER_RULES_CMD_TX_CMD                                                  (0x1<<1) /* BitField cmd_general_data 	should this cmd be applied for Tx */
		#define ETH_FILTER_RULES_CMD_TX_CMD_SHIFT                                            1
		#define ETH_FILTER_RULES_CMD_RESERVED0                                               (0x3F<<2) /* BitField cmd_general_data 	 */
		#define ETH_FILTER_RULES_CMD_RESERVED0_SHIFT                                         2
	u8_t func_id /* the function id */;
	u8_t client_id /* the client id */;
	u8_t reserved1;
	u16_t state;
		#define ETH_FILTER_RULES_CMD_UCAST_DROP_ALL                                          (0x1<<0) /* BitField state 	drop all unicast packets */
		#define ETH_FILTER_RULES_CMD_UCAST_DROP_ALL_SHIFT                                    0
		#define ETH_FILTER_RULES_CMD_UCAST_ACCEPT_ALL                                        (0x1<<1) /* BitField state 	accept all unicast packets (subject to vlan) */
		#define ETH_FILTER_RULES_CMD_UCAST_ACCEPT_ALL_SHIFT                                  1
		#define ETH_FILTER_RULES_CMD_UCAST_ACCEPT_UNMATCHED                                  (0x1<<2) /* BitField state 	accept all unmatched unicast packets */
		#define ETH_FILTER_RULES_CMD_UCAST_ACCEPT_UNMATCHED_SHIFT                            2
		#define ETH_FILTER_RULES_CMD_MCAST_DROP_ALL                                          (0x1<<3) /* BitField state 	drop all multicast packets */
		#define ETH_FILTER_RULES_CMD_MCAST_DROP_ALL_SHIFT                                    3
		#define ETH_FILTER_RULES_CMD_MCAST_ACCEPT_ALL                                        (0x1<<4) /* BitField state 	accept all multicast packets (subject to vlan) */
		#define ETH_FILTER_RULES_CMD_MCAST_ACCEPT_ALL_SHIFT                                  4
		#define ETH_FILTER_RULES_CMD_BCAST_ACCEPT_ALL                                        (0x1<<5) /* BitField state 	accept all broadcast packets (subject to vlan) */
		#define ETH_FILTER_RULES_CMD_BCAST_ACCEPT_ALL_SHIFT                                  5
		#define ETH_FILTER_RULES_CMD_ACCEPT_ANY_VLAN                                         (0x1<<6) /* BitField state 	accept packets matched only by MAC (without checking vlan) */
		#define ETH_FILTER_RULES_CMD_ACCEPT_ANY_VLAN_SHIFT                                   6
		#define ETH_FILTER_RULES_CMD_RESERVED2                                               (0x1FF<<7) /* BitField state 	 */
		#define ETH_FILTER_RULES_CMD_RESERVED2_SHIFT                                         7
	u16_t reserved3;
	struct regpair_t reserved4;
};


/*
 * parameters for eth classification filters ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_filter_rules_ramrod_data
{
	struct eth_classify_header header;
	struct eth_filter_rules_cmd rules[FILTER_RULES_COUNT];
};


/*
 * parameters for eth classification configuration ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_general_rules_ramrod_data
{
	struct eth_classify_header header;
	union eth_classify_rule_cmd rules[CLASSIFY_RULES_COUNT];
};


/*
 * The data for halt ramrod 
 */
struct eth_halt_ramrod_data
{
	u32_t client_id /* id of this client. (5 bits are used) */;
	u32_t reserved0;
};


/*
 * Command for setting multicast classification for a client $$KEEP_ENDIANNESS$$
 */
struct eth_multicast_rules_cmd
{
	u8_t cmd_general_data;
		#define ETH_MULTICAST_RULES_CMD_RX_CMD                                               (0x1<<0) /* BitField cmd_general_data 	should this cmd be applied for Rx */
		#define ETH_MULTICAST_RULES_CMD_RX_CMD_SHIFT                                         0
		#define ETH_MULTICAST_RULES_CMD_TX_CMD                                               (0x1<<1) /* BitField cmd_general_data 	should this cmd be applied for Tx */
		#define ETH_MULTICAST_RULES_CMD_TX_CMD_SHIFT                                         1
		#define ETH_MULTICAST_RULES_CMD_IS_ADD                                               (0x1<<2) /* BitField cmd_general_data 	1 for add rule, 0 for remove rule */
		#define ETH_MULTICAST_RULES_CMD_IS_ADD_SHIFT                                         2
		#define ETH_MULTICAST_RULES_CMD_RESERVED0                                            (0x1F<<3) /* BitField cmd_general_data 	 */
		#define ETH_MULTICAST_RULES_CMD_RESERVED0_SHIFT                                      3
	u8_t func_id /* the function id */;
	u8_t bin_id /* the bin to add this function to (0-255) */;
	u8_t engine_id /* the approximate multicast engine id */;
	u32_t reserved2;
	struct regpair_t reserved3;
};


/*
 * parameters for multicast classification ramrod $$KEEP_ENDIANNESS$$
 */
struct eth_multicast_rules_ramrod_data
{
	struct eth_classify_header header;
	struct eth_multicast_rules_cmd rules[MULTICAST_RULES_COUNT];
};


/*
 * Place holder for ramrods protocol specific data
 */
struct ramrod_data
{
	u32_t data_lo;
	u32_t data_hi;
};

/*
 * union for ramrod data for Ethernet protocol (CQE) (force size of 16 bits)
 */
union eth_ramrod_data
{
	struct ramrod_data general;
};


/*
 * RSS toeplitz hash type, as reported in CQE
 */
enum eth_rss_hash_type
{
	DEFAULT_HASH_TYPE=0,
	IPV4_HASH_TYPE=1,
	TCP_IPV4_HASH_TYPE=2,
	IPV6_HASH_TYPE=3,
	TCP_IPV6_HASH_TYPE=4,
	VLAN_PRI_HASH_TYPE=5,
	E1HOV_PRI_HASH_TYPE=6,
	DSCP_HASH_TYPE=7,
	MAX_ETH_RSS_HASH_TYPE
};


/*
 * Ethernet RSS mode
 */
enum eth_rss_mode
{
	ETH_RSS_MODE_DISABLED=0,
	ETH_RSS_MODE_REGULAR=1 /* Regular (ndis-like) RSS */,
	ETH_RSS_MODE_VLAN_PRI=2 /* RSS based on inner-vlan priority field */,
	ETH_RSS_MODE_E1HOV_PRI=3 /* RSS based on outer-vlan priority field */,
	ETH_RSS_MODE_IP_DSCP=4 /* RSS based on IPv4 DSCP field */,
	MAX_ETH_RSS_MODE
};


/*
 * parameters for RSS update ramrod (E2) $$KEEP_ENDIANNESS$$
 */
struct eth_rss_update_ramrod_data
{
	u8_t rss_engine_id;
	u8_t capabilities;
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_CAPABILITY                                   (0x1<<0) /* BitField capabilities Function RSS capabilities	configuration of the IpV4 2-tupple capability */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_CAPABILITY_SHIFT                             0
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_TCP_CAPABILITY                               (0x1<<1) /* BitField capabilities Function RSS capabilities	configuration of the IpV4 4-tupple capability for TCP */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_TCP_CAPABILITY_SHIFT                         1
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_UDP_CAPABILITY                               (0x1<<2) /* BitField capabilities Function RSS capabilities	configuration of the IpV4 4-tupple capability for UDP */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV4_UDP_CAPABILITY_SHIFT                         2
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_CAPABILITY                                   (0x1<<3) /* BitField capabilities Function RSS capabilities	configuration of the IpV6 2-tupple capability */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_CAPABILITY_SHIFT                             3
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_TCP_CAPABILITY                               (0x1<<4) /* BitField capabilities Function RSS capabilities	configuration of the IpV6 4-tupple capability for TCP */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_TCP_CAPABILITY_SHIFT                         4
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_UDP_CAPABILITY                               (0x1<<5) /* BitField capabilities Function RSS capabilities	configuration of the IpV6 4-tupple capability for UDP */
		#define ETH_RSS_UPDATE_RAMROD_DATA_IPV6_UDP_CAPABILITY_SHIFT                         5
		#define ETH_RSS_UPDATE_RAMROD_DATA_UPDATE_RSS_KEY                                    (0x1<<6) /* BitField capabilities Function RSS capabilities	if set update the rss keys */
		#define ETH_RSS_UPDATE_RAMROD_DATA_UPDATE_RSS_KEY_SHIFT                              6
		#define __ETH_RSS_UPDATE_RAMROD_DATA_RESERVED0                                       (0x1<<7) /* BitField capabilities Function RSS capabilities	 */
		#define __ETH_RSS_UPDATE_RAMROD_DATA_RESERVED0_SHIFT                                 7
	u8_t rss_result_mask /* The mask for the lower byte of RSS result - defines which section of the indirection table will be used. To enable all table put here 0x7F */;
	u8_t rss_mode /* The RSS mode for this function - use enum eth_rss_mode */;
	u32_t __reserved2;
	u8_t indirection_table[T_ETH_INDIRECTION_TABLE_SIZE] /* RSS indirection table */;
	u32_t rss_key[T_ETH_RSS_KEY] /* RSS key supplied as by OS */;
	u32_t echo;
	u32_t reserved3;
};


/*
 * The eth Rx Buffer Descriptor
 */
struct eth_rx_bd
{
	u32_t addr_lo /* Single continuous buffer low pointer */;
	u32_t addr_hi /* Single continuous buffer high pointer */;
};


struct eth_rx_bd_next_page
{
	u32_t addr_lo /* Next page low pointer */;
	u32_t addr_hi /* Next page high pointer */;
	u8_t reserved[8];
};


/*
 * Eth Rx Cqe structure- general structure for ramrods $$KEEP_ENDIANNESS$$
 */
struct common_ramrod_eth_rx_cqe
{
	u8_t ramrod_type;
		#define COMMON_RAMROD_ETH_RX_CQE_TYPE                                                (0x3<<0) /* BitField ramrod_type 	use enum eth_rx_cqe_type */
		#define COMMON_RAMROD_ETH_RX_CQE_TYPE_SHIFT                                          0
		#define COMMON_RAMROD_ETH_RX_CQE_ERROR                                               (0x1<<2) /* BitField ramrod_type 	 */
		#define COMMON_RAMROD_ETH_RX_CQE_ERROR_SHIFT                                         2
		#define COMMON_RAMROD_ETH_RX_CQE_RESERVED0                                           (0x1F<<3) /* BitField ramrod_type 	 */
		#define COMMON_RAMROD_ETH_RX_CQE_RESERVED0_SHIFT                                     3
	u8_t conn_type /* only 3 bits are used */;
	u16_t reserved1 /* protocol specific data */;
	u32_t conn_and_cmd_data;
		#define COMMON_RAMROD_ETH_RX_CQE_CID                                                 (0xFFFFFF<<0) /* BitField conn_and_cmd_data 	 */
		#define COMMON_RAMROD_ETH_RX_CQE_CID_SHIFT                                           0
		#define COMMON_RAMROD_ETH_RX_CQE_CMD_ID                                              (0xFF<<24) /* BitField conn_and_cmd_data 	command id of the ramrod- use RamrodCommandIdEnum */
		#define COMMON_RAMROD_ETH_RX_CQE_CMD_ID_SHIFT                                        24
	struct ramrod_data protocol_data /* protocol specific data */;
	u32_t echo;
	u32_t reserved2[3];
};

/*
 * Rx Last CQE in page (in ETH)
 */
struct eth_rx_cqe_next_page
{
	u32_t addr_lo /* Next page low pointer */;
	u32_t addr_hi /* Next page high pointer */;
	u32_t reserved[6];
};

/*
 * union for all eth rx cqe types (fix their sizes)
 */
union eth_rx_cqe
{
	struct eth_fast_path_rx_cqe fast_path_cqe;
	struct common_ramrod_eth_rx_cqe ramrod_cqe;
	struct eth_rx_cqe_next_page next_page_cqe;
	struct eth_end_agg_rx_cqe end_agg_cqe;
};



/*
 * Values for RX ETH CQE type field
 */
enum eth_rx_cqe_type
{
	RX_ETH_CQE_TYPE_ETH_FASTPATH=0 /* Fast path CQE */,
	RX_ETH_CQE_TYPE_ETH_RAMROD=1 /* Slow path CQE */,
	RX_ETH_CQE_TYPE_ETH_START_AGG=2 /* Fast path CQE */,
	RX_ETH_CQE_TYPE_ETH_STOP_AGG=3 /* Slow path CQE */,
	MAX_ETH_RX_CQE_TYPE
};


/*
 * Type of SGL/Raw field in ETH RX fast path CQE
 */
enum eth_rx_fp_sel
{
	ETH_FP_CQE_REGULAR=0 /* Regular CQE- no extra data */,
	ETH_FP_CQE_RAW=1 /* Extra data is raw data- iscsi OOO */,
	MAX_ETH_RX_FP_SEL
};


/*
 * The eth Rx SGE Descriptor
 */
struct eth_rx_sge
{
	u32_t addr_lo /* Single continuous buffer low pointer */;
	u32_t addr_hi /* Single continuous buffer high pointer */;
};



/*
 * common data for all protocols $$KEEP_ENDIANNESS$$
 */
struct spe_hdr_t
{
	u32_t conn_and_cmd_data;
		#define SPE_HDR_T_CID                                                                (0xFFFFFF<<0) /* BitField conn_and_cmd_data 	 */
		#define SPE_HDR_T_CID_SHIFT                                                          0
		#define SPE_HDR_T_CMD_ID                                                             (0xFFUL<<24) /* BitField conn_and_cmd_data 	command id of the ramrod- use enum common_spqe_cmd_id/eth_spqe_cmd_id/toe_spqe_cmd_id  */
		#define SPE_HDR_T_CMD_ID_SHIFT                                                       24
	u16_t type;
		#define SPE_HDR_T_CONN_TYPE                                                          (0xFF<<0) /* BitField type 	connection type. (3 bits are used) - use enum connection_type */
		#define SPE_HDR_T_CONN_TYPE_SHIFT                                                    0
		#define SPE_HDR_T_FUNCTION_ID                                                        (0xFF<<8) /* BitField type 	 */
		#define SPE_HDR_T_FUNCTION_ID_SHIFT                                                  8
	u16_t reserved1;
};

/*
 * specific data for ethernet slow path element
 */
union eth_specific_data
{
	u8_t protocol_data[8] /* to fix this structure size to 8 bytes */;
	struct regpair_t client_update_ramrod_data /* The address of the data for client update ramrod */;
	struct regpair_t client_init_ramrod_init_data /* The data for client setup ramrod */;
	struct eth_halt_ramrod_data halt_ramrod_data /* Includes the client id to be deleted */;
	struct regpair_t update_data_addr /* physical address of the eth_rss_update_ramrod_data struct, as allocated by the driver */;
	struct eth_common_ramrod_data common_ramrod_data /* The data contain client ID need to the ramrod */;
	struct regpair_t classify_cfg_addr /* physical address of the eth_classify_rules_ramrod_data struct, as allocated by the driver */;
	struct regpair_t filter_cfg_addr /* physical address of the eth_filter_cfg_ramrod_data struct, as allocated by the driver */;
	struct regpair_t mcast_cfg_addr /* physical address of the eth_mcast_cfg_ramrod_data struct, as allocated by the driver */;
};

/*
 * Ethernet slow path element
 */
struct eth_spe
{
	struct spe_hdr_t hdr /* common data for all protocols */;
	union eth_specific_data data /* data specific to ethernet protocol */;
};



/*
 * Ethernet command ID for slow path elements
 */
enum eth_spqe_cmd_id
{
	RAMROD_CMD_ID_ETH_UNUSED,
	RAMROD_CMD_ID_ETH_CLIENT_SETUP /* Setup a new L2 client */,
	RAMROD_CMD_ID_ETH_HALT /* Halt an L2 client */,
	RAMROD_CMD_ID_ETH_FORWARD_SETUP /* Setup a new FW channel */,
	RAMROD_CMD_ID_ETH_CLIENT_UPDATE /* Update an L2 client configuration */,
	RAMROD_CMD_ID_ETH_EMPTY /* Empty ramrod - used to synchronize iSCSI OOO */,
	RAMROD_CMD_ID_ETH_TERMINATE /* Terminate an L2 client */,
	RAMROD_CMD_ID_ETH_TPA_UPDATE /* update the tpa roles in L2 client */,
	RAMROD_CMD_ID_ETH_CLASSIFICATION_RULES=8 /* Add/remove classification filters for L2 client (in E2/E3 only) */,
	RAMROD_CMD_ID_ETH_FILTER_RULES /* Add/remove classification filters for L2 client (in E2/E3 only) */,
	RAMROD_CMD_ID_ETH_MULTICAST_RULES /* Add/remove multicast classification bin (in E2/E3 only) */,
	RAMROD_CMD_ID_ETH_RSS_UPDATE /* Update RSS configuration */,
	RAMROD_CMD_ID_ETH_SET_MAC /* Update RSS configuration */,
	MAX_ETH_SPQE_CMD_ID
};


/*
 * eth tpa update command
 */
enum eth_tpa_update_command
{
	TPA_UPDATE_NONE_COMMAND=0 /* nop command */,
	TPA_UPDATE_ENABLE_COMMAND=1 /* enable command */,
	TPA_UPDATE_DISABLE_COMMAND=2 /* disable command */,
	MAX_ETH_TPA_UPDATE_COMMAND
};


/*
 * Tx regular BD structure $$KEEP_ENDIANNESS$$
 */
struct eth_tx_bd
{
	u32_t addr_lo /* Single continuous buffer low pointer */;
	u32_t addr_hi /* Single continuous buffer high pointer */;
	u16_t total_pkt_bytes /* Size of the entire packet, valid for non-LSO packets */;
	u16_t nbytes /* Size of the data represented by the BD */;
	u8_t reserved[4] /* keeps same size as other eth tx bd types */;
};


/*
 * structure for easy accessibility to assembler
 */
struct eth_tx_bd_flags
{
	u8_t as_bitfield;
		#define ETH_TX_BD_FLAGS_IP_CSUM                                                      (0x1<<0) /* BitField as_bitfield 	IP CKSUM flag,Relevant in START */
		#define ETH_TX_BD_FLAGS_IP_CSUM_SHIFT                                                0
		#define ETH_TX_BD_FLAGS_L4_CSUM                                                      (0x1<<1) /* BitField as_bitfield 	L4 CKSUM flag,Relevant in START */
		#define ETH_TX_BD_FLAGS_L4_CSUM_SHIFT                                                1
		#define ETH_TX_BD_FLAGS_VLAN_MODE                                                    (0x3<<2) /* BitField as_bitfield 	00 - no vlan; 01 - inband Vlan; 10 outband Vlan (use enum eth_tx_vlan_type) */
		#define ETH_TX_BD_FLAGS_VLAN_MODE_SHIFT                                              2
		#define ETH_TX_BD_FLAGS_START_BD                                                     (0x1<<4) /* BitField as_bitfield 	Start of packet BD */
		#define ETH_TX_BD_FLAGS_START_BD_SHIFT                                               4
		#define ETH_TX_BD_FLAGS_IS_UDP                                                       (0x1<<5) /* BitField as_bitfield 	flag that indicates that the current packet is a udp packet */
		#define ETH_TX_BD_FLAGS_IS_UDP_SHIFT                                                 5
		#define ETH_TX_BD_FLAGS_SW_LSO                                                       (0x1<<6) /* BitField as_bitfield 	LSO flag, Relevant in START */
		#define ETH_TX_BD_FLAGS_SW_LSO_SHIFT                                                 6
		#define ETH_TX_BD_FLAGS_IPV6                                                         (0x1<<7) /* BitField as_bitfield 	set in case ipV6 packet, Relevant in START */
		#define ETH_TX_BD_FLAGS_IPV6_SHIFT                                                   7
};

/*
 * The eth Tx Buffer Descriptor $$KEEP_ENDIANNESS$$
 */
struct eth_tx_start_bd
{
	u32_t addr_lo /* Single continuous buffer low pointer */;
	u32_t addr_hi /* Single continuous buffer high pointer */;
	u16_t nbd /* Num of BDs in packet: include parsInfoBD, Relevant in START(only in Everest) */;
	u16_t nbytes /* Size of the data represented by the BD */;
	u16_t vlan_or_ethertype /* Vlan structure: vlan_id is in lsb, then cfi and then priority
							vlan_id	12 bits (lsb), cfi 1 bit, priority 3 bits. In E2, this field should be set with etherType for VFs with no vlan */;
	struct eth_tx_bd_flags bd_flags;
	u8_t general_data;
		#define ETH_TX_START_BD_HDR_NBDS                                                     (0xF<<0) /* BitField general_data 	contains the number of BDs that contain Ethernet/IP/TCP headers, for full/partial LSO modes */
		#define ETH_TX_START_BD_HDR_NBDS_SHIFT                                               0
		#define ETH_TX_START_BD_FORCE_VLAN_MODE                                              (0x1<<4) /* BitField general_data 	force vlan mode according to bds (vlan mode can change accroding to global configuration) */
		#define ETH_TX_START_BD_FORCE_VLAN_MODE_SHIFT                                        4
		#define ETH_TX_START_BD_RESREVED                                                     (0x1<<5) /* BitField general_data 	 */
		#define ETH_TX_START_BD_RESREVED_SHIFT                                               5
		#define ETH_TX_START_BD_ETH_ADDR_TYPE                                                (0x3<<6) /* BitField general_data 	marks ethernet address type - use enum eth_addr_type */
		#define ETH_TX_START_BD_ETH_ADDR_TYPE_SHIFT                                          6
};

/*
 * Tx parsing BD structure for ETH E1/E1h $$KEEP_ENDIANNESS$$
 */
struct eth_tx_parse_bd_e1x
{
	u8_t global_data;
		#define ETH_TX_PARSE_BD_E1X_IP_HDR_START_OFFSET_W                                    (0xF<<0) /* BitField global_data 	IP header Offset in WORDs from start of packet */
		#define ETH_TX_PARSE_BD_E1X_IP_HDR_START_OFFSET_W_SHIFT                              0
		#define ETH_TX_PARSE_BD_E1X_RESERVED0                                                (0x1<<4) /* BitField global_data 	reserved bit, should be set with 0 */
		#define ETH_TX_PARSE_BD_E1X_RESERVED0_SHIFT                                          4
		#define ETH_TX_PARSE_BD_E1X_PSEUDO_CS_WITHOUT_LEN                                    (0x1<<5) /* BitField global_data 	 */
		#define ETH_TX_PARSE_BD_E1X_PSEUDO_CS_WITHOUT_LEN_SHIFT                              5
		#define ETH_TX_PARSE_BD_E1X_LLC_SNAP_EN                                              (0x1<<6) /* BitField global_data 	 */
		#define ETH_TX_PARSE_BD_E1X_LLC_SNAP_EN_SHIFT                                        6
		#define ETH_TX_PARSE_BD_E1X_NS_FLG                                                   (0x1<<7) /* BitField global_data 	an optional addition to ECN that protects against 
																				 accidental or malicious concealment of marked packets 
																				 from the TCP sender. */
		#define ETH_TX_PARSE_BD_E1X_NS_FLG_SHIFT                                             7
	u8_t tcp_flags;
		#define ETH_TX_PARSE_BD_E1X_FIN_FLG                                                  (0x1<<0) /* BitField tcp_flags State flags	End of data flag */
		#define ETH_TX_PARSE_BD_E1X_FIN_FLG_SHIFT                                            0
		#define ETH_TX_PARSE_BD_E1X_SYN_FLG                                                  (0x1<<1) /* BitField tcp_flags State flags	Synchronize sequence numbers flag */
		#define ETH_TX_PARSE_BD_E1X_SYN_FLG_SHIFT                                            1
		#define ETH_TX_PARSE_BD_E1X_RST_FLG                                                  (0x1<<2) /* BitField tcp_flags State flags	Reset connection flag */
		#define ETH_TX_PARSE_BD_E1X_RST_FLG_SHIFT                                            2
		#define ETH_TX_PARSE_BD_E1X_PSH_FLG                                                  (0x1<<3) /* BitField tcp_flags State flags	Push flag */
		#define ETH_TX_PARSE_BD_E1X_PSH_FLG_SHIFT                                            3
		#define ETH_TX_PARSE_BD_E1X_ACK_FLG                                                  (0x1<<4) /* BitField tcp_flags State flags	Acknowledgment number valid flag */
		#define ETH_TX_PARSE_BD_E1X_ACK_FLG_SHIFT                                            4
		#define ETH_TX_PARSE_BD_E1X_URG_FLG                                                  (0x1<<5) /* BitField tcp_flags State flags	Urgent pointer valid flag */
		#define ETH_TX_PARSE_BD_E1X_URG_FLG_SHIFT                                            5
		#define ETH_TX_PARSE_BD_E1X_ECE_FLG                                                  (0x1<<6) /* BitField tcp_flags State flags	ECN-Echo */
		#define ETH_TX_PARSE_BD_E1X_ECE_FLG_SHIFT                                            6
		#define ETH_TX_PARSE_BD_E1X_CWR_FLG                                                  (0x1<<7) /* BitField tcp_flags State flags	Congestion Window Reduced */
		#define ETH_TX_PARSE_BD_E1X_CWR_FLG_SHIFT                                            7
	u8_t ip_hlen_w /* IP header length in WORDs */;
	s8_t reserved;
	u16_t total_hlen_w /* IP+TCP+ETH */;
	u16_t tcp_pseudo_csum /* Checksum of pseudo header with “length” field=0 */;
	u16_t lso_mss /* for LSO mode */;
	u16_t ip_id /* for LSO mode */;
	u32_t tcp_send_seq /* for LSO mode */;
};

/*
 * Tx parsing BD structure for ETH E2 $$KEEP_ENDIANNESS$$
 */
struct eth_tx_parse_bd_e2
{
	u16_t dst_mac_addr_lo /* destination mac address 16 low bits */;
	u16_t dst_mac_addr_mid /* destination mac address 16 middle bits */;
	u16_t dst_mac_addr_hi /* destination mac address 16 high bits */;
	u16_t src_mac_addr_lo /* source mac address 16 low bits */;
	u16_t src_mac_addr_mid /* source mac address 16 middle bits */;
	u16_t src_mac_addr_hi /* source mac address 16 high bits */;
	u32_t parsing_data;
		#define ETH_TX_PARSE_BD_E2_TCP_HDR_START_OFFSET_W                                    (0x1FFF<<0) /* BitField parsing_data 	TCP header Offset in WORDs from start of packet */
		#define ETH_TX_PARSE_BD_E2_TCP_HDR_START_OFFSET_W_SHIFT                              0
		#define ETH_TX_PARSE_BD_E2_TCP_HDR_LENGTH_DW                                         (0xF<<13) /* BitField parsing_data 	TCP header size in DOUBLE WORDS */
		#define ETH_TX_PARSE_BD_E2_TCP_HDR_LENGTH_DW_SHIFT                                   13
		#define ETH_TX_PARSE_BD_E2_LSO_MSS                                                   (0x3FFF<<17) /* BitField parsing_data 	for LSO mode */
		#define ETH_TX_PARSE_BD_E2_LSO_MSS_SHIFT                                             17
		#define ETH_TX_PARSE_BD_E2_IPV6_WITH_EXT_HDR                                         (0x1<<31) /* BitField parsing_data 	a flag to indicate an ipv6 packet with extension headers. If set on LSO packet, pseudo CS should be placed in TCP CS field without length field */
		#define ETH_TX_PARSE_BD_E2_IPV6_WITH_EXT_HDR_SHIFT                                   31
};

/*
 * The last BD in the BD memory will hold a pointer to the next BD memory
 */
struct eth_tx_next_bd
{
	u32_t addr_lo /* Single continuous buffer low pointer */;
	u32_t addr_hi /* Single continuous buffer high pointer */;
	u8_t reserved[8] /* keeps same size as other eth tx bd types */;
};

/*
 * union for 4 Bd types
 */
union eth_tx_bd_types
{
	struct eth_tx_start_bd start_bd /* the first bd in a packets */;
	struct eth_tx_bd reg_bd /* the common bd */;
	struct eth_tx_parse_bd_e1x parse_bd_e1x /* parsing info BD for e1/e1h */;
	struct eth_tx_parse_bd_e2 parse_bd_e2 /* parsing info BD for e2 */;
	struct eth_tx_next_bd next_bd /* Bd that contains the address of the next page */;
};

/*
 * array of 13 bds as appears in the eth xstorm context
 */
struct eth_tx_bds_array
{
	union eth_tx_bd_types bds[13];
};








/*
 * VLAN mode on TX BDs
 */
enum eth_tx_vlan_type
{
	X_ETH_NO_VLAN=0,
	X_ETH_OUTBAND_VLAN=1,
	X_ETH_INBAND_VLAN=2,
	X_ETH_FW_ADDED_VLAN=3 /* Driver should not use this! */,
	MAX_ETH_TX_VLAN_TYPE
};


/*
 * Ethernet VLAN filtering mode in E1x
 */
enum eth_vlan_filter_mode
{
	ETH_VLAN_FILTER_ANY_VLAN=0 /* Dont filter by vlan */,
	ETH_VLAN_FILTER_SPECIFIC_VLAN=1 /* Only the vlan_id is allowed */,
	ETH_VLAN_FILTER_CLASSIFY=2 /* Vlan will be added to CAM for classification */,
	MAX_ETH_VLAN_FILTER_MODE
};


/*
 * MAC filtering configuration command header $$KEEP_ENDIANNESS$$
 */
struct mac_configuration_hdr
{
	u8_t length /* number of entries valid in this command (6 bits) */;
	u8_t offset /* offset of the first entry in the list */;
	u16_t client_id /* the client id which this ramrod is sent on. 5b is used. */;
	u32_t echo /* echo value to be sent to driver on event ring */;
};

/*
 * MAC address in list for ramrod $$KEEP_ENDIANNESS$$
 */
struct mac_configuration_entry
{
	u16_t lsb_mac_addr /* 2 LSB of MAC address (should be given in big endien - driver should do hton to this number!!!) */;
	u16_t middle_mac_addr /* 2 middle bytes of MAC address (should be given in big endien - driver should do hton to this number!!!) */;
	u16_t msb_mac_addr /* 2 MSB of MAC address (should be given in big endien - driver should do hton to this number!!!) */;
	u16_t vlan_id /* The inner vlan id (12b). Used either in vlan_in_cam for mac_valn pair or for vlan filtering */;
	u8_t pf_id /* The pf id, for multi function mode */;
	u8_t flags;
		#define MAC_CONFIGURATION_ENTRY_ACTION_TYPE                                          (0x1<<0) /* BitField flags 	configures the action to be done in cam (used only is slow path handlers) - use enum set_mac_action_type */
		#define MAC_CONFIGURATION_ENTRY_ACTION_TYPE_SHIFT                                    0
		#define MAC_CONFIGURATION_ENTRY_RDMA_MAC                                             (0x1<<1) /* BitField flags 	If set, this MAC also belongs to RDMA client */
		#define MAC_CONFIGURATION_ENTRY_RDMA_MAC_SHIFT                                       1
		#define MAC_CONFIGURATION_ENTRY_VLAN_FILTERING_MODE                                  (0x3<<2) /* BitField flags 	use enum eth_vlan_filter_mode */
		#define MAC_CONFIGURATION_ENTRY_VLAN_FILTERING_MODE_SHIFT                            2
		#define MAC_CONFIGURATION_ENTRY_OVERRIDE_VLAN_REMOVAL                                (0x1<<4) /* BitField flags 	BitField flags 	0 - cant remove vlan 1 - can remove vlan. relevant only to everest1 */
		#define MAC_CONFIGURATION_ENTRY_OVERRIDE_VLAN_REMOVAL_SHIFT                          4
		#define MAC_CONFIGURATION_ENTRY_BROADCAST                                            (0x1<<5) /* BitField flags 	BitField flags 	 0 - not broadcast 1 - broadcast. relevant only to everest1 */
		#define MAC_CONFIGURATION_ENTRY_BROADCAST_SHIFT                                      5
		#define MAC_CONFIGURATION_ENTRY_RESERVED1                                            (0x3<<6) /* BitField flags 	 */
		#define MAC_CONFIGURATION_ENTRY_RESERVED1_SHIFT                                      6
	u16_t reserved0;
	u32_t clients_bit_vector /* Bit vector for the clients which should receive this MAC. */;
};

/*
 * MAC filtering configuration command
 */
struct mac_configuration_cmd
{
	struct mac_configuration_hdr hdr /* header */;
	struct mac_configuration_entry config_table[64] /* table of 64 MAC configuration entries: addresses and target table entries */;
};




/*
 * Set-MAC command type (in E1x)
 */
enum set_mac_action_type
{
	T_ETH_MAC_COMMAND_INVALIDATE=0,
	T_ETH_MAC_COMMAND_SET=1,
	MAX_SET_MAC_ACTION_TYPE
};


/*
 * tpa update ramrod data $$KEEP_ENDIANNESS$$
 */
struct tpa_update_ramrod_data
{
	u8_t update_ipv4 /* none, enable or disable (use enum eth_tpa_update_command) */;
	u8_t update_ipv6 /* none, enable or disable (use enum eth_tpa_update_command) */;
	u8_t client_id /* client init flow control data */;
	u8_t max_tpa_queues /* maximal TPA queues allowed for this client */;
	u8_t max_sges_for_packet /* The maximal number of SGEs that can be used for one packet. depends on MTU and SGE size. must be 0 if SGEs are disabled */;
	u8_t reserved0;
	u16_t reserved1;
	u16_t sge_buff_size /* Size of the buffers pointed by SGEs */;
	u16_t max_agg_size /* maximal size for the aggregated TPA packets, reprted by the host */;
	u32_t sge_page_base_lo /* The address to fetch the next sges from (low) */;
	u32_t sge_page_base_hi /* The address to fetch the next sges from (high) */;
	u16_t sge_pause_thr_low /* number of remaining sges under which, we send pause message */;
	u16_t sge_pause_thr_high /* number of remaining sges above which, we send un-pause message */;
};


/*
 * approximate-match multicast filtering for E1H per function in Tstorm 
 */
struct tstorm_eth_approximate_match_multicast_filtering
{
	u32_t mcast_add_hash_bit_array[8] /* Bit array for multicast hash filtering.Each bit supports a hash function result if to accept this multicast dst address. */;
};


/*
 * Common configuration parameters per function in Tstorm $$KEEP_ENDIANNESS$$
 */
struct tstorm_eth_function_common_config
{
	u16_t config_flags;
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_CAPABILITY                        (0x1<<0) /* BitField config_flags General configuration flags	configuration of the port RSS IpV4 2-tupple capability */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_CAPABILITY_SHIFT                  0
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_TCP_CAPABILITY                    (0x1<<1) /* BitField config_flags General configuration flags	configuration of the port RSS IpV4 4-tupple capability */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV4_TCP_CAPABILITY_SHIFT              1
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_CAPABILITY                        (0x1<<2) /* BitField config_flags General configuration flags	configuration of the port RSS IpV4 2-tupple capability */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_CAPABILITY_SHIFT                  2
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_TCP_CAPABILITY                    (0x1<<3) /* BitField config_flags General configuration flags	configuration of the port RSS IpV6 4-tupple capability */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_IPV6_TCP_CAPABILITY_SHIFT              3
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_MODE                                   (0x7<<4) /* BitField config_flags General configuration flags	RSS mode of operation: use enum eth_rss_mode */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_RSS_MODE_SHIFT                             4
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_VLAN_FILTERING_ENABLE                      (0x1<<7) /* BitField config_flags General configuration flags	0 - Dont filter by vlan, 1 - Filter according to the vlans specificied in mac_filter_config */
		#define TSTORM_ETH_FUNCTION_COMMON_CONFIG_VLAN_FILTERING_ENABLE_SHIFT                7
		#define __TSTORM_ETH_FUNCTION_COMMON_CONFIG_RESERVED0                                (0xFF<<8) /* BitField config_flags General configuration flags	 */
		#define __TSTORM_ETH_FUNCTION_COMMON_CONFIG_RESERVED0_SHIFT                          8
	u8_t rss_result_mask /* The mask for the lower byte of RSS result - defines which section of the indirection table will be used. To enable all table put here 0x7F */;
	u8_t reserved1;
	u16_t vlan_id[2] /* VLANs of this function. VLAN filtering is determine according to vlan_filtering_enable. */;
};


/*
 * MAC filtering configuration parameters per port in Tstorm $$KEEP_ENDIANNESS$$
 */
struct tstorm_eth_mac_filter_config
{
	u32_t ucast_drop_all /* bit vector in which the clients which drop all unicast packets are set */;
	u32_t ucast_accept_all /* bit vector in which clients that accept all unicast packets are set */;
	u32_t mcast_drop_all /* bit vector in which the clients which drop all multicast packets are set */;
	u32_t mcast_accept_all /* bit vector in which clients that accept all multicast packets are set */;
	u32_t bcast_accept_all /* bit vector in which clients that accept all broadcast packets are set */;
	u32_t vlan_filter[2] /* bit vector for VLAN filtering. Clients which enforce filtering of vlan[x] should be marked in vlan_filter[x]. In E1 only vlan_filter[1] is checked. The primary vlan is taken from the CAM target table. */;
	u32_t unmatched_unicast /* bit vector in which clients that accept unmatched unicast packets are set */;
};



/*
 * Three RX producers for ETH
 */
struct ustorm_eth_rx_producers
{
#if defined(__BIG_ENDIAN)
	u16_t bd_prod /* Producer of the RX BD ring */;
	u16_t cqe_prod /* Producer of the RX CQE ring */;
#elif defined(__LITTLE_ENDIAN)
	u16_t cqe_prod /* Producer of the RX CQE ring */;
	u16_t bd_prod /* Producer of the RX BD ring */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t reserved;
	u16_t sge_prod /* Producer of the RX SGE ring */;
#elif defined(__LITTLE_ENDIAN)
	u16_t sge_prod /* Producer of the RX SGE ring */;
	u16_t reserved;
#endif
};




/*
 * ABTS info $$KEEP_ENDIANNESS$$
 */
struct fcoe_abts_info
{
	u16_t aborted_task_id /* Task ID to be aborted */;
	u16_t reserved0;
	u32_t reserved1;
};


/*
 * Fixed size structure in order to plant it in Union structure $$KEEP_ENDIANNESS$$
 */
struct fcoe_abts_rsp_union
{
	u8_t r_ctl /* Only R_CTL part of the FC header in ABTS ACC or BA_RJT messages is placed */;
	u8_t rsrv[3];
	u32_t abts_rsp_payload[7] /* The payload of  the ABTS ACC (12B) or the BA_RJT (4B) */;
};


/*
 * 4 regs size $$KEEP_ENDIANNESS$$
 */
struct fcoe_bd_ctx
{
	u32_t buf_addr_hi /* Higher buffer host address */;
	u32_t buf_addr_lo /* Lower buffer host address */;
	u16_t buf_len /* Buffer length (in bytes) */;
	u16_t rsrv0;
	u16_t flags /* BD flags */;
	u16_t rsrv1;
};


/*
 * FCoE cached sges context $$KEEP_ENDIANNESS$$
 */
struct fcoe_cached_sge_ctx
{
	struct regpair_t cur_buf_addr /* Current buffer address (in initialization it is the first cached buffer) */;
	u16_t cur_buf_rem /* Remaining data in current buffer (in bytes) */;
	u16_t second_buf_rem /* Remaining data in second buffer (in bytes) */;
	struct regpair_t second_buf_addr /* Second cached buffer address */;
};


/*
 * Cleanup info $$KEEP_ENDIANNESS$$
 */
struct fcoe_cleanup_info
{
	u16_t cleaned_task_id /* Task ID to be cleaned */;
	u16_t rolled_tx_seq_cnt /* Tx sequence count */;
	u32_t rolled_tx_data_offset /* Tx data offset */;
};


/*
 * Fcp RSP flags $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_rsp_flags
{
	u8_t flags;
		#define FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID                                         (0x1<<0) /* BitField flags 	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_RSP_LEN_VALID_SHIFT                                   0
		#define FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID                                         (0x1<<1) /* BitField flags 	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_SNS_LEN_VALID_SHIFT                                   1
		#define FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER                                            (0x1<<2) /* BitField flags 	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_RESID_OVER_SHIFT                                      2
		#define FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER                                           (0x1<<3) /* BitField flags 	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_RESID_UNDER_SHIFT                                     3
		#define FCOE_FCP_RSP_FLAGS_FCP_CONF_REQ                                              (0x1<<4) /* BitField flags 	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_CONF_REQ_SHIFT                                        4
		#define FCOE_FCP_RSP_FLAGS_FCP_BIDI_FLAGS                                            (0x7<<5) /* BitField flags 	 */
		#define FCOE_FCP_RSP_FLAGS_FCP_BIDI_FLAGS_SHIFT                                      5
};

/*
 * Fcp RSP payload $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_rsp_payload
{
	struct regpair_t reserved0;
	u32_t fcp_resid;
	u8_t scsi_status_code;
	struct fcoe_fcp_rsp_flags fcp_flags;
	u16_t retry_delay_timer;
	u32_t fcp_rsp_len;
	u32_t fcp_sns_len;
};

/*
 * Fixed size structure in order to plant it in Union structure $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_rsp_union
{
	struct fcoe_fcp_rsp_payload payload;
	struct regpair_t reserved0;
};

/*
 * FC header $$KEEP_ENDIANNESS$$
 */
struct fcoe_fc_hdr
{
	u8_t s_id[3];
	u8_t cs_ctl;
	u8_t d_id[3];
	u8_t r_ctl;
	u16_t seq_cnt;
	u8_t df_ctl;
	u8_t seq_id;
	u8_t f_ctl[3];
	u8_t type;
	u32_t parameters;
	u16_t rx_id;
	u16_t ox_id;
};

/*
 * FC header union $$KEEP_ENDIANNESS$$
 */
struct fcoe_mp_rsp_union
{
	struct fcoe_fc_hdr fc_hdr /* FC header copied into task context (middle path flows) */;
	u32_t mp_payload_len /* Length of the MP payload that was placed */;
	u32_t rsrv;
};

/*
 * Completion information $$KEEP_ENDIANNESS$$
 */
union fcoe_comp_flow_info
{
	struct fcoe_fcp_rsp_union fcp_rsp /* FCP_RSP payload */;
	struct fcoe_abts_rsp_union abts_rsp /* ABTS ACC R_CTL part of the FC header ABTS ACC or BA_RJT payload frame */;
	struct fcoe_mp_rsp_union mp_rsp /* FC header copied into task context (middle path flows) */;
	u32_t opaque[8];
};


/*
 * External ABTS info $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_abts_info
{
	u32_t rsrv0[6];
	struct fcoe_abts_info ctx /* ABTS information. Initialized by Xstorm */;
};


/*
 * External cleanup info $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_cleanup_info
{
	u32_t rsrv0[6];
	struct fcoe_cleanup_info ctx /* Cleanup information */;
};


/*
 * Fcoe FW Tx sequence context $$KEEP_ENDIANNESS$$
 */
struct fcoe_fw_tx_seq_ctx
{
	u32_t data_offset /* The amount of data transmitted so far (equal to FCP_DATA PARAMETER field) */;
	u16_t seq_cnt /* The last SEQ_CNT transmitted */;
	u16_t rsrv0;
};

/*
 * Fcoe external FW Tx sequence context $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_fw_tx_seq_ctx
{
	u32_t rsrv0[6];
	struct fcoe_fw_tx_seq_ctx ctx /* TX sequence context */;
};


/*
 * FCoE multiple sges context $$KEEP_ENDIANNESS$$
 */
struct fcoe_mul_sges_ctx
{
	struct regpair_t cur_sge_addr /* Current BD address */;
	u16_t cur_sge_off /* Offset in current BD (in bytes) */;
	u8_t cur_sge_idx /* Current BD index in BD list */;
	u8_t sgl_size /* Total number of BDs */;
};

/*
 * FCoE external multiple sges context $$KEEP_ENDIANNESS$$
 */
struct fcoe_ext_mul_sges_ctx
{
	struct fcoe_mul_sges_ctx mul_sgl /* SGL context */;
	struct regpair_t rsrv0;
};


/*
 * FCP CMD payload $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_cmd_payload
{
	u32_t opaque[8];
};





/*
 * Fcp xfr rdy payload $$KEEP_ENDIANNESS$$
 */
struct fcoe_fcp_xfr_rdy_payload
{
	u32_t burst_len;
	u32_t data_ro;
};


/*
 * FC frame $$KEEP_ENDIANNESS$$
 */
struct fcoe_fc_frame
{
	struct fcoe_fc_hdr fc_hdr;
	u32_t reserved0[2];
};




/*
 * FCoE KCQ CQE parameters $$KEEP_ENDIANNESS$$
 */
union fcoe_kcqe_params
{
	u32_t reserved0[4];
};

/*
 * FCoE KCQ CQE $$KEEP_ENDIANNESS$$
 */
struct fcoe_kcqe
{
	u32_t fcoe_conn_id /* Drivers connection ID (only 16 bits are used) */;
	u32_t completion_status /* 0=command completed succesfuly, 1=command failed */;
	u32_t fcoe_conn_context_id /* Context ID of the FCoE connection */;
	union fcoe_kcqe_params params /* command-specific parameters */;
	u16_t qe_self_seq /* Self identifying sequence number */;
	u8_t op_code /* FCoE KCQ opcode */;
	u8_t flags;
		#define FCOE_KCQE_RESERVED0                                                          (0x7<<0) /* BitField flags 	 */
		#define FCOE_KCQE_RESERVED0_SHIFT                                                    0
		#define FCOE_KCQE_RAMROD_COMPLETION                                                  (0x1<<3) /* BitField flags 	Everest only - indicates whether this KCQE is a ramrod completion */
		#define FCOE_KCQE_RAMROD_COMPLETION_SHIFT                                            3
		#define FCOE_KCQE_LAYER_CODE                                                         (0x7<<4) /* BitField flags 	protocol layer (L2,L3,L4,L5,iSCSI,FCoE) */
		#define FCOE_KCQE_LAYER_CODE_SHIFT                                                   4
		#define FCOE_KCQE_LINKED_WITH_NEXT                                                   (0x1<<7) /* BitField flags 	Indicates whether this KCQE is linked with the next KCQE */
		#define FCOE_KCQE_LINKED_WITH_NEXT_SHIFT                                             7
};



/*
 * FCoE KWQE header $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_header
{
	u8_t op_code /* FCoE KWQE opcode */;
	u8_t flags;
		#define FCOE_KWQE_HEADER_RESERVED0                                                   (0xF<<0) /* BitField flags 	 */
		#define FCOE_KWQE_HEADER_RESERVED0_SHIFT                                             0
		#define FCOE_KWQE_HEADER_LAYER_CODE                                                  (0x7<<4) /* BitField flags 	protocol layer (L2,L3,L4,L5) */
		#define FCOE_KWQE_HEADER_LAYER_CODE_SHIFT                                            4
		#define FCOE_KWQE_HEADER_RESERVED1                                                   (0x1<<7) /* BitField flags 	 */
		#define FCOE_KWQE_HEADER_RESERVED1_SHIFT                                             7
};

/*
 * FCoE firmware init request 1 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_init1
{
	u16_t num_tasks /* Number of tasks in global task list */;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	u32_t task_list_pbl_addr_lo /* Lower 32-bit of Task List page table */;
	u32_t task_list_pbl_addr_hi /* Higher 32-bit of Task List page table */;
	u32_t dummy_buffer_addr_lo /* Lower 32-bit of dummy buffer */;
	u32_t dummy_buffer_addr_hi /* Higher 32-bit of dummy buffer */;
	u16_t sq_num_wqes /* Number of entries in the Send Queue */;
	u16_t rq_num_wqes /* Number of entries in the Receive Queue */;
	u16_t rq_buffer_log_size /* Log of the size of a single buffer (entry) in the RQ */;
	u16_t cq_num_wqes /* Number of entries in the Completion Queue */;
	u16_t mtu /* Max transmission unit */;
	u8_t num_sessions_log /* Log of the number of sessions */;
	u8_t flags;
		#define FCOE_KWQE_INIT1_LOG_PAGE_SIZE                                                (0xF<<0) /* BitField flags 	log of page size value */
		#define FCOE_KWQE_INIT1_LOG_PAGE_SIZE_SHIFT                                          0
		#define FCOE_KWQE_INIT1_LOG_CACHED_PBES_PER_FUNC                                     (0x7<<4) /* BitField flags 	 */
		#define FCOE_KWQE_INIT1_LOG_CACHED_PBES_PER_FUNC_SHIFT                               4
		#define FCOE_KWQE_INIT1_RESERVED1                                                    (0x1<<7) /* BitField flags 	 */
		#define FCOE_KWQE_INIT1_RESERVED1_SHIFT                                              7
};

/*
 * FCoE firmware init request 2 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_init2
{
	u8_t hsi_major_version /* Implies on a change broken previous HSI */;
	u8_t hsi_minor_version /* Implies on a change which does not broken previous HSI */;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	u32_t hash_tbl_pbl_addr_lo /* Lower 32-bit of Hash table PBL */;
	u32_t hash_tbl_pbl_addr_hi /* Higher 32-bit of Hash table PBL */;
	u32_t t2_hash_tbl_addr_lo /* Lower 32-bit of T2 Hash table */;
	u32_t t2_hash_tbl_addr_hi /* Higher 32-bit of T2 Hash table */;
	u32_t t2_ptr_hash_tbl_addr_lo /* Lower 32-bit of T2 ptr Hash table */;
	u32_t t2_ptr_hash_tbl_addr_hi /* Higher 32-bit of T2 ptr Hash table */;
	u32_t free_list_count /* T2 free list count */;
};

/*
 * FCoE firmware init request 3 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_init3
{
	u16_t reserved0;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	u32_t error_bit_map_lo /* 32 lower bits of error bitmap: 1=error, 0=warning */;
	u32_t error_bit_map_hi /* 32 upper bits of error bitmap: 1=error, 0=warning */;
	u8_t perf_config /* 0= no performance acceleration, 1=cached connection, 2=cached tasks, 3=both */;
	u8_t reserved21[3];
	u32_t reserved2[4];
};

/*
 * FCoE connection offload request 1 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload1
{
	u16_t fcoe_conn_id /* Drivers connection ID. Should be sent in KCQEs to speed-up drivers access to connection data. */;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	u32_t sq_addr_lo /* Lower 32-bit of SQ */;
	u32_t sq_addr_hi /* Higher 32-bit of SQ */;
	u32_t rq_pbl_addr_lo /* Lower 32-bit of RQ page table */;
	u32_t rq_pbl_addr_hi /* Higher 32-bit of RQ page table */;
	u32_t rq_first_pbe_addr_lo /* Lower 32-bit of first RQ pbe */;
	u32_t rq_first_pbe_addr_hi /* Higher 32-bit of first RQ pbe */;
	u16_t rq_prod /* Initial RQ producer */;
	u16_t reserved0;
};

/*
 * FCoE connection offload request 2 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload2
{
	u16_t tx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by target, received during both FLOGI and PLOGI, minimum value should be taken */;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	u32_t cq_addr_lo /* Lower 32-bit of CQ */;
	u32_t cq_addr_hi /* Higher 32-bit of CQ */;
	u32_t xferq_addr_lo /* Lower 32-bit of XFERQ */;
	u32_t xferq_addr_hi /* Higher 32-bit of XFERQ */;
	u32_t conn_db_addr_lo /* Lower 32-bit of Conn DB (RQ prod and CQ arm bit) */;
	u32_t conn_db_addr_hi /* Higher 32-bit of Conn DB (RQ prod and CQ arm bit) */;
	u32_t reserved1;
};

/*
 * FCoE connection offload request 3 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload3
{
	u16_t vlan_tag;
		#define FCOE_KWQE_CONN_OFFLOAD3_VLAN_ID                                              (0xFFF<<0) /* BitField vlan_tag 	Vlan id */
		#define FCOE_KWQE_CONN_OFFLOAD3_VLAN_ID_SHIFT                                        0
		#define FCOE_KWQE_CONN_OFFLOAD3_CFI                                                  (0x1<<12) /* BitField vlan_tag 	Canonical format indicator */
		#define FCOE_KWQE_CONN_OFFLOAD3_CFI_SHIFT                                            12
		#define FCOE_KWQE_CONN_OFFLOAD3_PRIORITY                                             (0x7<<13) /* BitField vlan_tag 	Vlan priority */
		#define FCOE_KWQE_CONN_OFFLOAD3_PRIORITY_SHIFT                                       13
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	u8_t s_id[3] /* Source ID, received during FLOGI */;
	u8_t tx_max_conc_seqs_c3 /* Maximum concurrent Sequences for Class 3 supported by target, received during PLOGI */;
	u8_t d_id[3] /* Destination ID, received after inquiry of the fabric network */;
	u8_t flags;
		#define FCOE_KWQE_CONN_OFFLOAD3_B_MUL_N_PORT_IDS                                     (0x1<<0) /* BitField flags 	Supporting multiple N_Port IDs indication, received during FLOGI */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_MUL_N_PORT_IDS_SHIFT                               0
		#define FCOE_KWQE_CONN_OFFLOAD3_B_E_D_TOV_RES                                        (0x1<<1) /* BitField flags 	E_D_TOV resolution (0 - msec, 1 - nsec), negotiated in PLOGI */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_E_D_TOV_RES_SHIFT                                  1
		#define FCOE_KWQE_CONN_OFFLOAD3_B_CONT_INCR_SEQ_CNT                                  (0x1<<2) /* BitField flags 	Continuously increasing SEQ_CNT indication, received during PLOGI */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_CONT_INCR_SEQ_CNT_SHIFT                            2
		#define FCOE_KWQE_CONN_OFFLOAD3_B_CONF_REQ                                           (0x1<<3) /* BitField flags 	Confirmation request supported */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_CONF_REQ_SHIFT                                     3
		#define FCOE_KWQE_CONN_OFFLOAD3_B_REC_VALID                                          (0x1<<4) /* BitField flags 	REC allowed */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_REC_VALID_SHIFT                                    4
		#define FCOE_KWQE_CONN_OFFLOAD3_B_C2_VALID                                           (0x1<<5) /* BitField flags 	Class 2 valid, received during PLOGI */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_C2_VALID_SHIFT                                     5
		#define FCOE_KWQE_CONN_OFFLOAD3_B_ACK_0                                              (0x1<<6) /* BitField flags 	ACK_0 capability supporting by target, received furing PLOGI */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_ACK_0_SHIFT                                        6
		#define FCOE_KWQE_CONN_OFFLOAD3_B_VLAN_FLAG                                          (0x1<<7) /* BitField flags 	Is inner vlan exist */
		#define FCOE_KWQE_CONN_OFFLOAD3_B_VLAN_FLAG_SHIFT                                    7
	u32_t reserved;
	u32_t confq_first_pbe_addr_lo /* The first page used when handling CONFQ - low address */;
	u32_t confq_first_pbe_addr_hi /* The first page used when handling CONFQ - high address */;
	u16_t tx_total_conc_seqs /* Total concurrent Sequences for all Classes supported by target, received during PLOGI */;
	u16_t rx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by us, sent during FLOGI/PLOGI */;
	u16_t rx_total_conc_seqs /* Total concurrent Sequences for all Classes supported by us, sent during PLOGI */;
	u8_t rx_max_conc_seqs_c3 /* Maximum Concurrent Sequences for Class 3 supported by us, sent during PLOGI */;
	u8_t rx_open_seqs_exch_c3 /* Maximum Open Sequences per Exchange for Class 3 supported by us, sent during PLOGI */;
};

/*
 * FCoE connection offload request 4 $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_offload4
{
	u8_t e_d_tov_timer_val /* E_D_TOV timer value in milliseconds/20, negotiated in PLOGI */;
	u8_t reserved2;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	u8_t src_mac_addr_lo[2] /* Lower 16-bit of source MAC address  */;
	u8_t src_mac_addr_mid[2] /* Mid 16-bit of source MAC address  */;
	u8_t src_mac_addr_hi[2] /* Higher 16-bit of source MAC address */;
	u8_t dst_mac_addr_hi[2] /* Higher 16-bit of destination MAC address */;
	u8_t dst_mac_addr_lo[2] /* Lower 16-bit destination MAC address */;
	u8_t dst_mac_addr_mid[2] /* Mid 16-bit destination MAC address */;
	u32_t lcq_addr_lo /* Lower 32-bit of LCQ */;
	u32_t lcq_addr_hi /* Higher 32-bit of LCQ */;
	u32_t confq_pbl_base_addr_lo /* CONFQ PBL low address */;
	u32_t confq_pbl_base_addr_hi /* CONFQ PBL high address */;
};

/*
 * FCoE connection enable request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_enable_disable
{
	u16_t reserved0;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	u8_t src_mac_addr_lo[2] /* Lower 16-bit of source MAC address (HBAs MAC address) */;
	u8_t src_mac_addr_mid[2] /* Mid 16-bit of source MAC address (HBAs MAC address) */;
	u8_t src_mac_addr_hi[2] /* Higher 16-bit of source MAC address (HBAs MAC address) */;
	u16_t vlan_tag;
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_VLAN_ID                                        (0xFFF<<0) /* BitField vlan_tag Vlan tag	Vlan id */
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_VLAN_ID_SHIFT                                  0
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_CFI                                            (0x1<<12) /* BitField vlan_tag Vlan tag	Canonical format indicator */
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_CFI_SHIFT                                      12
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_PRIORITY                                       (0x7<<13) /* BitField vlan_tag Vlan tag	Vlan priority */
		#define FCOE_KWQE_CONN_ENABLE_DISABLE_PRIORITY_SHIFT                                 13
	u8_t dst_mac_addr_lo[2] /* Lower 16-bit of destination MAC address (FCFs MAC address) */;
	u8_t dst_mac_addr_mid[2] /* Mid 16-bit of destination MAC address (FCFs MAC address) */;
	u8_t dst_mac_addr_hi[2] /* Higher 16-bit of destination MAC address (FCFs MAC address) */;
	u16_t reserved1;
	u8_t s_id[3] /* Source ID, received during FLOGI */;
	u8_t vlan_flag /* Vlan flag */;
	u8_t d_id[3] /* Destination ID, received after inquiry of the fabric network */;
	u8_t reserved3;
	u32_t context_id /* Context ID (cid) of the connection */;
	u32_t conn_id /* FCoE Connection ID */;
	u32_t reserved4;
};

/*
 * FCoE connection destroy request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_conn_destroy
{
	u16_t reserved0;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	u32_t context_id /* Context ID (cid) of the connection */;
	u32_t conn_id /* FCoE Connection ID */;
	u32_t reserved1[5];
};

/*
 * FCoe destroy request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_destroy
{
	u16_t reserved0;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	u32_t reserved1[7];
};

/*
 * FCoe statistics request $$KEEP_ENDIANNESS$$
 */
struct fcoe_kwqe_stat
{
	u16_t reserved0;
	struct fcoe_kwqe_header hdr /* KWQ WQE header */;
	u32_t stat_params_addr_lo /* Statistics host address */;
	u32_t stat_params_addr_hi /* Statistics host address */;
	u32_t reserved1[5];
};

/*
 * FCoE KWQ WQE $$KEEP_ENDIANNESS$$
 */
union fcoe_kwqe
{
	struct fcoe_kwqe_init1 init1;
	struct fcoe_kwqe_init2 init2;
	struct fcoe_kwqe_init3 init3;
	struct fcoe_kwqe_conn_offload1 conn_offload1;
	struct fcoe_kwqe_conn_offload2 conn_offload2;
	struct fcoe_kwqe_conn_offload3 conn_offload3;
	struct fcoe_kwqe_conn_offload4 conn_offload4;
	struct fcoe_kwqe_conn_enable_disable conn_enable_disable;
	struct fcoe_kwqe_conn_destroy conn_destroy;
	struct fcoe_kwqe_destroy destroy;
	struct fcoe_kwqe_stat statistics;
};
















/*
 * TX SGL context $$KEEP_ENDIANNESS$$
 */
union fcoe_sgl_union_ctx
{
	struct fcoe_cached_sge_ctx cached_sge /* Cached SGEs context */;
	struct fcoe_ext_mul_sges_ctx sgl /* SGL context */;
	u32_t opaque[5];
};

/*
 * Data-In/ELS/BLS information $$KEEP_ENDIANNESS$$
 */
struct fcoe_read_flow_info
{
	union fcoe_sgl_union_ctx sgl_ctx /* The SGL that would be used for data placement (20 bytes) */;
	u32_t rsrv0[3];
};


/*
 * Fcoe stat context $$KEEP_ENDIANNESS$$
 */
struct fcoe_s_stat_ctx
{
	u8_t flags;
		#define FCOE_S_STAT_CTX_ACTIVE                                                       (0x1<<0) /* BitField flags 	Active Sequence indication (0 - not avtive; 1 - active) */
		#define FCOE_S_STAT_CTX_ACTIVE_SHIFT                                                 0
		#define FCOE_S_STAT_CTX_ACK_ABORT_SEQ_COND                                           (0x1<<1) /* BitField flags 	Abort Sequence requested indication */
		#define FCOE_S_STAT_CTX_ACK_ABORT_SEQ_COND_SHIFT                                     1
		#define FCOE_S_STAT_CTX_ABTS_PERFORMED                                               (0x1<<2) /* BitField flags 	ABTS (on Sequence) protocol complete indication (0 - not completed; 1 -completed by Recipient) */
		#define FCOE_S_STAT_CTX_ABTS_PERFORMED_SHIFT                                         2
		#define FCOE_S_STAT_CTX_SEQ_TIMEOUT                                                  (0x1<<3) /* BitField flags 	E_D_TOV timeout indication */
		#define FCOE_S_STAT_CTX_SEQ_TIMEOUT_SHIFT                                            3
		#define FCOE_S_STAT_CTX_P_RJT                                                        (0x1<<4) /* BitField flags 	P_RJT transmitted indication */
		#define FCOE_S_STAT_CTX_P_RJT_SHIFT                                                  4
		#define FCOE_S_STAT_CTX_ACK_EOFT                                                     (0x1<<5) /* BitField flags 	ACK (EOFt) transmitted indication (0 - not tranmitted; 1 - transmitted) */
		#define FCOE_S_STAT_CTX_ACK_EOFT_SHIFT                                               5
		#define FCOE_S_STAT_CTX_RSRV1                                                        (0x3<<6) /* BitField flags 	 */
		#define FCOE_S_STAT_CTX_RSRV1_SHIFT                                                  6
};

/*
 * Fcoe rx seq context $$KEEP_ENDIANNESS$$
 */
struct fcoe_rx_seq_ctx
{
	u8_t seq_id /* The Sequence ID */;
	struct fcoe_s_stat_ctx s_stat /* The Sequence status */;
	u16_t seq_cnt /* The lowest SEQ_CNT received for the Sequence */;
	u32_t low_exp_ro /* Report on the offset at the beginning of the Sequence */;
	u32_t high_exp_ro /* The highest expected relative offset. The next buffer offset to be received in case of XFER_RDY or in FCP_DATA */;
};


/*
 * Fcoe rx_wr union context $$KEEP_ENDIANNESS$$
 */
union fcoe_rx_wr_union_ctx
{
	struct fcoe_read_flow_info read_info /* Data-In/ELS/BLS information */;
	union fcoe_comp_flow_info comp_info /* Completion information */;
	u32_t opaque[8];
};



/*
 * FCoE SQ element $$KEEP_ENDIANNESS$$
 */
struct fcoe_sqe
{
	u16_t wqe;
		#define FCOE_SQE_TASK_ID                                                             (0x7FFF<<0) /* BitField wqe 	The task ID (OX_ID) to be processed */
		#define FCOE_SQE_TASK_ID_SHIFT                                                       0
		#define FCOE_SQE_TOGGLE_BIT                                                          (0x1<<15) /* BitField wqe 	Toggle bit updated by the driver */
		#define FCOE_SQE_TOGGLE_BIT_SHIFT                                                    15
};



/*
 * 14 regs $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_tx_only
{
	union fcoe_sgl_union_ctx sgl_ctx /* TX SGL context */;
	u32_t rsrv0;
};

/*
 * 32 bytes (8 regs) used for TX only purposes $$KEEP_ENDIANNESS$$
 */
union fcoe_tx_wr_rx_rd_union_ctx
{
	struct fcoe_fc_frame tx_frame /* Middle-path/ABTS/Data-Out information */;
	struct fcoe_fcp_cmd_payload fcp_cmd /* FCP_CMD payload */;
	struct fcoe_ext_cleanup_info cleanup /* Task ID to be cleaned */;
	struct fcoe_ext_abts_info abts /* Task ID to be aborted */;
	struct fcoe_ext_fw_tx_seq_ctx tx_seq /* TX sequence information */;
	u32_t opaque[8];
};

/*
 * tce_tx_wr_rx_rd_const $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_tx_wr_rx_rd_const
{
	u8_t init_flags;
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TASK_TYPE                                         (0x7<<0) /* BitField init_flags 	Task type - Write / Read / Middle / Unsolicited / ABTS / Cleanup */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TASK_TYPE_SHIFT                                   0
		#define FCOE_TCE_TX_WR_RX_RD_CONST_DEV_TYPE                                          (0x1<<3) /* BitField init_flags 	Tape/Disk device indication */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_DEV_TYPE_SHIFT                                    3
		#define FCOE_TCE_TX_WR_RX_RD_CONST_CLASS_TYPE                                        (0x1<<4) /* BitField init_flags 	Class 3/2 indication */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_CLASS_TYPE_SHIFT                                  4
		#define FCOE_TCE_TX_WR_RX_RD_CONST_CACHED_SGE                                        (0x3<<5) /* BitField init_flags 	Num of cached sge (0 - not cached sge) */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_CACHED_SGE_SHIFT                                  5
		#define FCOE_TCE_TX_WR_RX_RD_CONST_SUPPORT_REC_TOV                                   (0x1<<7) /* BitField init_flags 	Support REC_TOV flag, for FW use only */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_SUPPORT_REC_TOV_SHIFT                             7
	u8_t tx_flags;
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_VALID                                          (0x1<<0) /* BitField tx_flags Both TX and RX processing could read but only the TX could write	Indication of TX valid task */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_VALID_SHIFT                                    0
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_STATE                                          (0xF<<1) /* BitField tx_flags Both TX and RX processing could read but only the TX could write	The TX state of the task */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_STATE_SHIFT                                    1
		#define FCOE_TCE_TX_WR_RX_RD_CONST_RSRV1                                             (0x1<<5) /* BitField tx_flags Both TX and RX processing could read but only the TX could write	 */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_RSRV1_SHIFT                                       5
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_SEQ_INIT                                       (0x1<<6) /* BitField tx_flags Both TX and RX processing could read but only the TX could write	TX Sequence initiative indication */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_TX_SEQ_INIT_SHIFT                                 6
		#define FCOE_TCE_TX_WR_RX_RD_CONST_RSRV2                                             (0x1<<7) /* BitField tx_flags Both TX and RX processing could read but only the TX could write	 */
		#define FCOE_TCE_TX_WR_RX_RD_CONST_RSRV2_SHIFT                                       7
	u16_t rsrv3;
	u32_t verify_tx_seq /* Sequence counter snapshot in order to verify target did not send FCP_RSP before the actual transmission of PBF from the SGL */;
};

/*
 * tce_tx_wr_rx_rd $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_tx_wr_rx_rd
{
	union fcoe_tx_wr_rx_rd_union_ctx union_ctx /* 32 (8 regs) bytes used for TX only purposes */;
	struct fcoe_tce_tx_wr_rx_rd_const const_ctx /* Constant TX_WR_RX_RD */;
};

/*
 * tce_rx_wr_tx_rd_const $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_wr_tx_rd_const
{
	u32_t data_2_trns /* The maximum amount of data that would be transferred in this task */;
	u32_t init_flags;
		#define FCOE_TCE_RX_WR_TX_RD_CONST_CID                                               (0xFFFFFF<<0) /* BitField init_flags 	The CID of the connection (used by the CHIP) */
		#define FCOE_TCE_RX_WR_TX_RD_CONST_CID_SHIFT                                         0
		#define FCOE_TCE_RX_WR_TX_RD_CONST_RSRV0                                             (0xFF<<24) /* BitField init_flags 	 */
		#define FCOE_TCE_RX_WR_TX_RD_CONST_RSRV0_SHIFT                                       24
};

/*
 * tce_rx_wr_tx_rd_var $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_wr_tx_rd_var
{
	u16_t rx_flags;
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV1                                               (0xF<<0) /* BitField rx_flags 	 */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV1_SHIFT                                         0
		#define FCOE_TCE_RX_WR_TX_RD_VAR_NUM_RQ_WQE                                          (0x7<<4) /* BitField rx_flags 	The number of RQ WQEs that were consumed (for sense data only) */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_NUM_RQ_WQE_SHIFT                                    4
		#define FCOE_TCE_RX_WR_TX_RD_VAR_CONF_REQ                                            (0x1<<7) /* BitField rx_flags 	Confirmation request indication */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_CONF_REQ_SHIFT                                      7
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_STATE                                            (0xF<<8) /* BitField rx_flags 	The RX state of the task */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_STATE_SHIFT                                      8
		#define FCOE_TCE_RX_WR_TX_RD_VAR_EXP_FIRST_FRAME                                     (0x1<<12) /* BitField rx_flags 	Indication on expecting to receive the first frame from target */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_EXP_FIRST_FRAME_SHIFT                               12
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_SEQ_INIT                                         (0x1<<13) /* BitField rx_flags 	RX Sequence initiative indication */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_SEQ_INIT_SHIFT                                   13
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV2                                               (0x1<<14) /* BitField rx_flags 	 */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RSRV2_SHIFT                                         14
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_VALID                                            (0x1<<15) /* BitField rx_flags 	Indication of RX valid task */
		#define FCOE_TCE_RX_WR_TX_RD_VAR_RX_VALID_SHIFT                                      15
	u16_t rx_id /* The RX_ID read from incoming frame and to be used in subsequent transmitting frames */;
	struct fcoe_fcp_xfr_rdy_payload fcp_xfr_rdy /* Data-In/ELS/BLS information */;
};

/*
 * tce_rx_wr_tx_rd $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_wr_tx_rd
{
	struct fcoe_tce_rx_wr_tx_rd_const const_ctx /* The RX_ID read from incoming frame and to be used in subsequent transmitting frames */;
	struct fcoe_tce_rx_wr_tx_rd_var var_ctx /* The RX_ID read from incoming frame and to be used in subsequent transmitting frames */;
};

/*
 * tce_rx_only $$KEEP_ENDIANNESS$$
 */
struct fcoe_tce_rx_only
{
	struct fcoe_rx_seq_ctx rx_seq_ctx /* The context of current receiving Sequence */;
	union fcoe_rx_wr_union_ctx union_ctx /* Read flow info/ Completion flow info */;
};

/*
 * task_ctx_entry $$KEEP_ENDIANNESS$$
 */
struct fcoe_task_ctx_entry
{
	struct fcoe_tce_tx_only txwr_only /* TX processing shall be the only one to read/write to this section */;
	struct fcoe_tce_tx_wr_rx_rd txwr_rxrd /* TX processing shall write and RX shall read from this section */;
	struct fcoe_tce_rx_wr_tx_rd rxwr_txrd /* RX processing shall write and TX shall read from this section */;
	struct fcoe_tce_rx_only rxwr_only /* RX processing shall be the only one to read/write to this section */;
};










/*
 * FCoE XFRQ element $$KEEP_ENDIANNESS$$
 */
struct fcoe_xfrqe
{
	u16_t wqe;
		#define FCOE_XFRQE_TASK_ID                                                           (0x7FFF<<0) /* BitField wqe 	The task ID (OX_ID) to be processed */
		#define FCOE_XFRQE_TASK_ID_SHIFT                                                     0
		#define FCOE_XFRQE_TOGGLE_BIT                                                        (0x1<<15) /* BitField wqe 	Toggle bit updated by the driver */
		#define FCOE_XFRQE_TOGGLE_BIT_SHIFT                                                  15
};


/*
 * Cached SGEs $$KEEP_ENDIANNESS$$
 */
struct common_fcoe_sgl
{
	struct fcoe_bd_ctx sge[3];
};


/*
 * FCoE SQ\XFRQ element
 */
struct fcoe_cached_wqe
{
	struct fcoe_sqe sqe /* SQ WQE */;
	struct fcoe_xfrqe xfrqe /* XFRQ WQE */;
};


/*
 * FCoE connection enable\disable params passed by driver to FW in FCoE enable ramrod $$KEEP_ENDIANNESS$$
 */
struct fcoe_conn_enable_disable_ramrod_params
{
	struct fcoe_kwqe_conn_enable_disable enable_disable_kwqe;
};


/*
 * FCoE connection offload params passed by driver to FW in FCoE offload ramrod $$KEEP_ENDIANNESS$$
 */
struct fcoe_conn_offload_ramrod_params
{
	struct fcoe_kwqe_conn_offload1 offload_kwqe1;
	struct fcoe_kwqe_conn_offload2 offload_kwqe2;
	struct fcoe_kwqe_conn_offload3 offload_kwqe3;
	struct fcoe_kwqe_conn_offload4 offload_kwqe4;
};


struct ustorm_fcoe_mng_ctx
{
#if defined(__BIG_ENDIAN)
	u8_t mid_seq_proc_flag /* Middle Sequence received processing */;
	u8_t tce_in_cam_flag /* TCE in CAM indication */;
	u8_t tce_on_ior_flag /* TCE on IOR indication (TCE on IORs but not necessarily in CAM) */;
	u8_t en_cached_tce_flag /* TCE cached functionality enabled indication */;
#elif defined(__LITTLE_ENDIAN)
	u8_t en_cached_tce_flag /* TCE cached functionality enabled indication */;
	u8_t tce_on_ior_flag /* TCE on IOR indication (TCE on IORs but not necessarily in CAM) */;
	u8_t tce_in_cam_flag /* TCE in CAM indication */;
	u8_t mid_seq_proc_flag /* Middle Sequence received processing */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t tce_cam_addr /* CAM address of task context */;
	u8_t cached_conn_flag /* Cached locked connection indication */;
	u16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	u16_t rsrv0;
	u8_t cached_conn_flag /* Cached locked connection indication */;
	u8_t tce_cam_addr /* CAM address of task context */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t dma_tce_ram_addr /* RAM address of task context when executing DMA operations (read/write) */;
	u16_t tce_ram_addr /* RAM address of task context (might be in cached table or in scratchpad) */;
#elif defined(__LITTLE_ENDIAN)
	u16_t tce_ram_addr /* RAM address of task context (might be in cached table or in scratchpad) */;
	u16_t dma_tce_ram_addr /* RAM address of task context when executing DMA operations (read/write) */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t ox_id /* Last OX_ID that has been used */;
	u16_t wr_done_seq /* Last task write done in the specific connection */;
#elif defined(__LITTLE_ENDIAN)
	u16_t wr_done_seq /* Last task write done in the specific connection */;
	u16_t ox_id /* Last OX_ID that has been used */;
#endif
	struct regpair_t task_addr /* Last task address in used */;
};

/*
 * Parameters initialized during offloaded according to FLOGI/PLOGI/PRLI and used in FCoE context section
 */
struct ustorm_fcoe_params
{
#if defined(__BIG_ENDIAN)
	u16_t fcoe_conn_id /* The connection ID that would be used by driver to identify the conneciton */;
	u16_t flags;
		#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS                                          (0x1<<0) /* BitField flags 	Supporting multiple N_Port IDs indication, received during FLOGI */
		#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS_SHIFT                                    0
		#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES                                             (0x1<<1) /* BitField flags 	E_D_TOV resolution (0 - msec, 1 - nsec), negotiated in PLOGI */
		#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES_SHIFT                                       1
		#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT                                       (0x1<<2) /* BitField flags 	Continuously increasing SEQ_CNT indication, received during PLOGI */
		#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT_SHIFT                                 2
		#define USTORM_FCOE_PARAMS_B_CONF_REQ                                                (0x1<<3) /* BitField flags 	Confirmation request supported */
		#define USTORM_FCOE_PARAMS_B_CONF_REQ_SHIFT                                          3
		#define USTORM_FCOE_PARAMS_B_REC_VALID                                               (0x1<<4) /* BitField flags 	REC allowed */
		#define USTORM_FCOE_PARAMS_B_REC_VALID_SHIFT                                         4
		#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT                                           (0x1<<5) /* BitField flags 	CQ toggle bit */
		#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT_SHIFT                                     5
		#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT                                         (0x1<<6) /* BitField flags 	XFRQ toggle bit */
		#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT_SHIFT                                   6
		#define USTORM_FCOE_PARAMS_RSRV0                                                     (0x1FF<<7) /* BitField flags 	 */
		#define USTORM_FCOE_PARAMS_RSRV0_SHIFT                                               7
#elif defined(__LITTLE_ENDIAN)
	u16_t flags;
		#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS                                          (0x1<<0) /* BitField flags 	Supporting multiple N_Port IDs indication, received during FLOGI */
		#define USTORM_FCOE_PARAMS_B_MUL_N_PORT_IDS_SHIFT                                    0
		#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES                                             (0x1<<1) /* BitField flags 	E_D_TOV resolution (0 - msec, 1 - nsec), negotiated in PLOGI */
		#define USTORM_FCOE_PARAMS_B_E_D_TOV_RES_SHIFT                                       1
		#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT                                       (0x1<<2) /* BitField flags 	Continuously increasing SEQ_CNT indication, received during PLOGI */
		#define USTORM_FCOE_PARAMS_B_CONT_INCR_SEQ_CNT_SHIFT                                 2
		#define USTORM_FCOE_PARAMS_B_CONF_REQ                                                (0x1<<3) /* BitField flags 	Confirmation request supported */
		#define USTORM_FCOE_PARAMS_B_CONF_REQ_SHIFT                                          3
		#define USTORM_FCOE_PARAMS_B_REC_VALID                                               (0x1<<4) /* BitField flags 	REC allowed */
		#define USTORM_FCOE_PARAMS_B_REC_VALID_SHIFT                                         4
		#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT                                           (0x1<<5) /* BitField flags 	CQ toggle bit */
		#define USTORM_FCOE_PARAMS_B_CQ_TOGGLE_BIT_SHIFT                                     5
		#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT                                         (0x1<<6) /* BitField flags 	XFRQ toggle bit */
		#define USTORM_FCOE_PARAMS_B_XFRQ_TOGGLE_BIT_SHIFT                                   6
		#define USTORM_FCOE_PARAMS_RSRV0                                                     (0x1FF<<7) /* BitField flags 	 */
		#define USTORM_FCOE_PARAMS_RSRV0_SHIFT                                               7
	u16_t fcoe_conn_id /* The connection ID that would be used by driver to identify the conneciton */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t hc_csdm_byte_en /* Host coalescing Cstorm RAM address byte enable */;
	u8_t func_id /* Function id */;
	u8_t port_id /* Port id */;
	u8_t vnic_id /* Vnic id */;
#elif defined(__LITTLE_ENDIAN)
	u8_t vnic_id /* Vnic id */;
	u8_t port_id /* Port id */;
	u8_t func_id /* Function id */;
	u8_t hc_csdm_byte_en /* Host coalescing Cstorm RAM address byte enable */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t rx_total_conc_seqs /* Total concurrent Sequences for all Classes supported by us, sent during PLOGI */;
	u16_t rx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by us, sent during FLOGI/PLOGI */;
#elif defined(__LITTLE_ENDIAN)
	u16_t rx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by us, sent during FLOGI/PLOGI */;
	u16_t rx_total_conc_seqs /* Total concurrent Sequences for all Classes supported by us, sent during PLOGI */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t task_pbe_idx_off /* The first PBE for this specific task list in RAM */;
	u8_t task_in_page_log_size /* Number of tasks in page (log 2) */;
	u16_t rx_max_conc_seqs /* Maximum Concurrent Sequences for Class 3 supported by us, sent during PLOGI */;
#elif defined(__LITTLE_ENDIAN)
	u16_t rx_max_conc_seqs /* Maximum Concurrent Sequences for Class 3 supported by us, sent during PLOGI */;
	u8_t task_in_page_log_size /* Number of tasks in page (log 2) */;
	u8_t task_pbe_idx_off /* The first PBE for this specific task list in RAM */;
#endif
};

/*
 * FCoE 16-bits index structure
 */
struct fcoe_idx16_fields
{
	u16_t fields;
		#define FCOE_IDX16_FIELDS_IDX                                                        (0x7FFF<<0) /* BitField fields 	 */
		#define FCOE_IDX16_FIELDS_IDX_SHIFT                                                  0
		#define FCOE_IDX16_FIELDS_MSB                                                        (0x1<<15) /* BitField fields 	 */
		#define FCOE_IDX16_FIELDS_MSB_SHIFT                                                  15
};

/*
 * FCoE 16-bits index union
 */
union fcoe_idx16_field_union
{
	struct fcoe_idx16_fields fields /* Parameters field */;
	u16_t val /* Global value */;
};

/*
 * Parameters required for placement according to SGL
 */
struct ustorm_fcoe_data_place_mng
{
#if defined(__BIG_ENDIAN)
	u16_t sge_off;
	u8_t num_sges /* Number of SGEs left to be used on context */;
	u8_t sge_idx /* 0xFF value indicated loading SGL */;
#elif defined(__LITTLE_ENDIAN)
	u8_t sge_idx /* 0xFF value indicated loading SGL */;
	u8_t num_sges /* Number of SGEs left to be used on context */;
	u16_t sge_off;
#endif
};

/*
 * Parameters required for placement according to SGL
 */
struct ustorm_fcoe_data_place
{
	struct ustorm_fcoe_data_place_mng cached_mng /* 0xFF value indicated loading SGL */;
	struct fcoe_bd_ctx cached_sge[2];
};

/*
 * TX processing shall write and RX processing shall read from this section
 */
union fcoe_u_tce_tx_wr_rx_rd_union
{
	struct fcoe_abts_info abts /* ABTS information */;
	struct fcoe_cleanup_info cleanup /* Cleanup information */;
	struct fcoe_fw_tx_seq_ctx tx_seq_ctx /* TX sequence context */;
	u32_t opaque[2];
};

/*
 * TX processing shall write and RX processing shall read from this section
 */
struct fcoe_u_tce_tx_wr_rx_rd
{
	union fcoe_u_tce_tx_wr_rx_rd_union union_ctx /* FW DATA_OUT/CLEANUP information */;
	struct fcoe_tce_tx_wr_rx_rd_const const_ctx /* TX processing shall write and RX shall read from this section */;
};

struct ustorm_fcoe_tce
{
	struct fcoe_u_tce_tx_wr_rx_rd txwr_rxrd /* TX processing shall write and RX shall read from this section */;
	struct fcoe_tce_rx_wr_tx_rd rxwr_txrd /* RX processing shall write and TX shall read from this section */;
	struct fcoe_tce_rx_only rxwr /* RX processing shall be the only one to read/write to this section */;
};

struct ustorm_fcoe_cache_ctx
{
	u32_t rsrv0;
	struct ustorm_fcoe_data_place data_place;
	struct ustorm_fcoe_tce tce /* Task context */;
};

/*
 * Ustorm FCoE Storm Context
 */
struct ustorm_fcoe_st_context
{
	struct ustorm_fcoe_mng_ctx mng_ctx /* Managing the processing of the flow */;
	struct ustorm_fcoe_params fcoe_params /* Align to 128 bytes */;
	struct regpair_t cq_base_addr /* CQ current page host address */;
	struct regpair_t rq_pbl_base /* PBL host address for RQ */;
	struct regpair_t rq_cur_page_addr /* RQ current page host address */;
	struct regpair_t confq_pbl_base_addr /* Base address of the CONFQ page list */;
	struct regpair_t conn_db_base /* Connection data base address in host memory where RQ producer and CQ arm bit reside in */;
	struct regpair_t xfrq_base_addr /* XFRQ base host address */;
	struct regpair_t lcq_base_addr /* LCQ base host address */;
#if defined(__BIG_ENDIAN)
	union fcoe_idx16_field_union rq_cons /* RQ consumer advance for each RQ WQE consuming */;
	union fcoe_idx16_field_union rq_prod /* RQ producer update by driver and read by FW (should be initialized to RQ size)  */;
#elif defined(__LITTLE_ENDIAN)
	union fcoe_idx16_field_union rq_prod /* RQ producer update by driver and read by FW (should be initialized to RQ size)  */;
	union fcoe_idx16_field_union rq_cons /* RQ consumer advance for each RQ WQE consuming */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t xfrq_prod /* XFRQ producer (No consumer is needed since Q can not be overloaded) */;
	u16_t cq_cons /* CQ consumer copy of last update from driver (Q can not be overloaded) */;
#elif defined(__LITTLE_ENDIAN)
	u16_t cq_cons /* CQ consumer copy of last update from driver (Q can not be overloaded) */;
	u16_t xfrq_prod /* XFRQ producer (No consumer is needed since Q can not be overloaded) */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t lcq_cons /* lcq consumer */;
	u16_t hc_cram_address /* Host coalescing Cstorm RAM address */;
#elif defined(__LITTLE_ENDIAN)
	u16_t hc_cram_address /* Host coalescing Cstorm RAM address */;
	u16_t lcq_cons /* lcq consumer */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t sq_xfrq_lcq_confq_size /* SQ/XFRQ/LCQ/CONFQ size */;
	u16_t confq_prod /* CONFQ producer */;
#elif defined(__LITTLE_ENDIAN)
	u16_t confq_prod /* CONFQ producer */;
	u16_t sq_xfrq_lcq_confq_size /* SQ/XFRQ/LCQ/CONFQ size */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t hc_csdm_agg_int /* Host coalescing CSDM aggregative interrupts */;
	u8_t rsrv2;
	u8_t available_rqes /* Available RQEs */;
	u8_t sp_q_flush_cnt /* The remain number of queues to be flushed (in QM) */;
#elif defined(__LITTLE_ENDIAN)
	u8_t sp_q_flush_cnt /* The remain number of queues to be flushed (in QM) */;
	u8_t available_rqes /* Available RQEs */;
	u8_t rsrv2;
	u8_t hc_csdm_agg_int /* Host coalescing CSDM aggregative interrupts */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t num_pend_tasks /* Number of pending tasks */;
	u16_t pbf_ack_ram_addr /* PBF TX sequence ACK ram address */;
#elif defined(__LITTLE_ENDIAN)
	u16_t pbf_ack_ram_addr /* PBF TX sequence ACK ram address */;
	u16_t num_pend_tasks /* Number of pending tasks */;
#endif
	struct ustorm_fcoe_cache_ctx cache_ctx /* Cached context */;
};

/*
 * The FCoE non-aggregative context of Tstorm
 */
struct tstorm_fcoe_st_context
{
	struct regpair_t reserved0;
	struct regpair_t reserved1;
};

/*
 * Ethernet context section
 */
struct xstorm_fcoe_eth_context_section
{
#if defined(__BIG_ENDIAN)
	u8_t remote_addr_4 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_5 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_0 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_1 /* Local Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	u8_t local_addr_1 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_0 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_5 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_4 /* Remote Mac Address, used in PBF Header Builder Command */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t remote_addr_0 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_1 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_2 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_3 /* Remote Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	u8_t remote_addr_3 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_2 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_1 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_0 /* Remote Mac Address, used in PBF Header Builder Command */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t reserved_vlan_type /* this field is not an absolute must, but the reseved was here */;
	u16_t params;
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID                                      (0xFFF<<0) /* BitField params 	part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT                                0
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI                                          (0x1<<12) /* BitField params 	Canonical format indicator, part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI_SHIFT                                    12
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY                                     (0x7<<13) /* BitField params 	part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY_SHIFT                               13
#elif defined(__LITTLE_ENDIAN)
	u16_t params;
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID                                      (0xFFF<<0) /* BitField params 	part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT                                0
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI                                          (0x1<<12) /* BitField params 	Canonical format indicator, part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_CFI_SHIFT                                    12
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY                                     (0x7<<13) /* BitField params 	part of PBF Header Builder Command */
		#define XSTORM_FCOE_ETH_CONTEXT_SECTION_PRIORITY_SHIFT                               13
	u16_t reserved_vlan_type /* this field is not an absolute must, but the reseved was here */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t local_addr_2 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_3 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_4 /* Loca lMac Address, used in PBF Header Builder Command */;
	u8_t local_addr_5 /* Local Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	u8_t local_addr_5 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_4 /* Loca lMac Address, used in PBF Header Builder Command */;
	u8_t local_addr_3 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_2 /* Local Mac Address, used in PBF Header Builder Command */;
#endif
};

/*
 * Flags used in FCoE context section - 1 byte
 */
struct xstorm_fcoe_context_flags
{
	u8_t flags;
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_PROC_Q                                           (0x3<<0) /* BitField flags 	The current queue in process */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_PROC_Q_SHIFT                                     0
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_MID_SEQ                                          (0x1<<2) /* BitField flags 	Middle of Sequence indication */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_MID_SEQ_SHIFT                                    2
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_BLOCK_SQ                                         (0x1<<3) /* BitField flags 	Indicates whether the SQ is blocked since we are in the middle of ABTS/Cleanup procedure */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_BLOCK_SQ_SHIFT                                   3
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_REC_SUPPORT                                      (0x1<<4) /* BitField flags 	REC support */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_REC_SUPPORT_SHIFT                                4
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_SQ_TOGGLE                                        (0x1<<5) /* BitField flags 	SQ toggle bit */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_SQ_TOGGLE_SHIFT                                  5
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_XFRQ_TOGGLE                                      (0x1<<6) /* BitField flags 	XFRQ toggle bit */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_XFRQ_TOGGLE_SHIFT                                6
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_VNTAG_VLAN                                       (0x1<<7) /* BitField flags 	Are we using VNTag inner vlan - in this case we have to read it on every VNTag version change */
		#define XSTORM_FCOE_CONTEXT_FLAGS_B_VNTAG_VLAN_SHIFT                                 7
};

struct xstorm_fcoe_tce
{
	struct fcoe_tce_tx_only txwr /* TX processing shall be the only one to read/write to this section */;
	struct fcoe_tce_tx_wr_rx_rd txwr_rxrd /* TX processing shall write and RX processing shall read from this section */;
};

/*
 * FCP_DATA parameters required for transmission
 */
struct xstorm_fcoe_fcp_data
{
	u32_t io_rem /* IO remainder */;
#if defined(__BIG_ENDIAN)
	u16_t cached_sge_off;
	u8_t cached_num_sges /* Number of SGEs on context */;
	u8_t cached_sge_idx /* 0xFF value indicated loading SGL */;
#elif defined(__LITTLE_ENDIAN)
	u8_t cached_sge_idx /* 0xFF value indicated loading SGL */;
	u8_t cached_num_sges /* Number of SGEs on context */;
	u16_t cached_sge_off;
#endif
	u32_t buf_addr_hi_0 /* Higher buffer host address */;
	u32_t buf_addr_lo_0 /* Lower buffer host address */;
#if defined(__BIG_ENDIAN)
	u16_t num_of_pending_tasks /* Num of pending tasks */;
	u16_t buf_len_0 /* Buffer length (in bytes) */;
#elif defined(__LITTLE_ENDIAN)
	u16_t buf_len_0 /* Buffer length (in bytes) */;
	u16_t num_of_pending_tasks /* Num of pending tasks */;
#endif
	u32_t buf_addr_hi_1 /* Higher buffer host address */;
	u32_t buf_addr_lo_1 /* Lower buffer host address */;
#if defined(__BIG_ENDIAN)
	u16_t task_pbe_idx_off /* Task pbe index offset */;
	u16_t buf_len_1 /* Buffer length (in bytes) */;
#elif defined(__LITTLE_ENDIAN)
	u16_t buf_len_1 /* Buffer length (in bytes) */;
	u16_t task_pbe_idx_off /* Task pbe index offset */;
#endif
	u32_t buf_addr_hi_2 /* Higher buffer host address */;
	u32_t buf_addr_lo_2 /* Lower buffer host address */;
#if defined(__BIG_ENDIAN)
	u16_t ox_id /* OX_ID */;
	u16_t buf_len_2 /* Buffer length (in bytes) */;
#elif defined(__LITTLE_ENDIAN)
	u16_t buf_len_2 /* Buffer length (in bytes) */;
	u16_t ox_id /* OX_ID */;
#endif
};

/*
 * FCoE 16-bits vlan structure
 */
struct fcoe_vlan_fields
{
	u16_t fields;
		#define FCOE_VLAN_FIELDS_VID                                                         (0xFFF<<0) /* BitField fields 	 */
		#define FCOE_VLAN_FIELDS_VID_SHIFT                                                   0
		#define FCOE_VLAN_FIELDS_CLI                                                         (0x1<<12) /* BitField fields 	 */
		#define FCOE_VLAN_FIELDS_CLI_SHIFT                                                   12
		#define FCOE_VLAN_FIELDS_PRI                                                         (0x7<<13) /* BitField fields 	 */
		#define FCOE_VLAN_FIELDS_PRI_SHIFT                                                   13
};

/*
 * FCoE 16-bits vlan union
 */
union fcoe_vlan_field_union
{
	struct fcoe_vlan_fields fields /* Parameters field */;
	u16_t val /* Global value */;
};

/*
 * FCoE 16-bits vlan, vif union
 */
union fcoe_vlan_vif_field_union
{
	union fcoe_vlan_field_union vlan /* Vlan */;
	u16_t vif /* VIF */;
};

/*
 * FCoE context section
 */
struct xstorm_fcoe_context_section
{
#if defined(__BIG_ENDIAN)
	u8_t cs_ctl /* cs ctl */;
	u8_t s_id[3] /* Source ID, received during FLOGI */;
#elif defined(__LITTLE_ENDIAN)
	u8_t s_id[3] /* Source ID, received during FLOGI */;
	u8_t cs_ctl /* cs ctl */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t rctl /* rctl */;
	u8_t d_id[3] /* Destination ID, received after inquiry of the fabric network */;
#elif defined(__LITTLE_ENDIAN)
	u8_t d_id[3] /* Destination ID, received after inquiry of the fabric network */;
	u8_t rctl /* rctl */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t sq_xfrq_lcq_confq_size /* SQ/XFRQ/LCQ/CONFQ size */;
	u16_t tx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by target, received during both FLOGI and PLOGI, minimum value should be taken */;
#elif defined(__LITTLE_ENDIAN)
	u16_t tx_max_fc_pay_len /* The maximum acceptable FC payload size (Buffer-to-buffer Receive Data_Field size) supported by target, received during both FLOGI and PLOGI, minimum value should be taken */;
	u16_t sq_xfrq_lcq_confq_size /* SQ/XFRQ/LCQ/CONFQ size */;
#endif
	u32_t lcq_prod /* LCQ producer value */;
#if defined(__BIG_ENDIAN)
	u8_t port_id /* Port ID */;
	u8_t func_id /* Function ID */;
	u8_t seq_id /* SEQ ID counter to be used in transmitted FC header */;
	struct xstorm_fcoe_context_flags tx_flags;
#elif defined(__LITTLE_ENDIAN)
	struct xstorm_fcoe_context_flags tx_flags;
	u8_t seq_id /* SEQ ID counter to be used in transmitted FC header */;
	u8_t func_id /* Function ID */;
	u8_t port_id /* Port ID */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t mtu /* MTU */;
	u8_t func_mode /* Function mode */;
	u8_t vnic_id /* Vnic ID */;
#elif defined(__LITTLE_ENDIAN)
	u8_t vnic_id /* Vnic ID */;
	u8_t func_mode /* Function mode */;
	u16_t mtu /* MTU */;
#endif
	struct regpair_t confq_curr_page_addr /* The current page of CONFQ to be processed */;
	struct fcoe_cached_wqe cached_wqe[8] /* Up to 8 SQ/XFRQ WQEs read in one shot */;
	struct regpair_t lcq_base_addr /* The page address which the LCQ resides in host memory */;
	struct xstorm_fcoe_tce tce /* TX section task context */;
	struct xstorm_fcoe_fcp_data fcp_data /* The parameters required for FCP_DATA Sequences transmission */;
#if defined(__BIG_ENDIAN)
	u8_t tx_max_conc_seqs_c3 /* Maximum concurrent Sequences for Class 3 supported by traget, received during PLOGI */;
	u8_t vlan_flag /* Is any inner vlan exist */;
	u8_t dcb_val /* DCB val - let us know if dcb info changes */;
	u8_t data_pb_cmd_size /* Data pb cmd size */;
#elif defined(__LITTLE_ENDIAN)
	u8_t data_pb_cmd_size /* Data pb cmd size */;
	u8_t dcb_val /* DCB val - let us know if dcb info changes */;
	u8_t vlan_flag /* Is any inner vlan exist */;
	u8_t tx_max_conc_seqs_c3 /* Maximum concurrent Sequences for Class 3 supported by traget, received during PLOGI */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t fcoe_tx_stat_params_ram_addr /* stat Ram Addr */;
	u16_t fcoe_tx_fc_seq_ram_addr /* Tx FC sequence Ram Addr */;
#elif defined(__LITTLE_ENDIAN)
	u16_t fcoe_tx_fc_seq_ram_addr /* Tx FC sequence Ram Addr */;
	u16_t fcoe_tx_stat_params_ram_addr /* stat Ram Addr */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t fcp_cmd_line_credit;
	u8_t eth_hdr_size /* Ethernet header size without eth type */;
	u16_t pbf_addr /* PBF addr */;
#elif defined(__LITTLE_ENDIAN)
	u16_t pbf_addr /* PBF addr */;
	u8_t eth_hdr_size /* Ethernet header size without eth type */;
	u8_t fcp_cmd_line_credit;
#endif
#if defined(__BIG_ENDIAN)
	union fcoe_vlan_vif_field_union multi_func_val /* Outer vlan vif union */;
	u8_t page_log_size /* Page log size */;
	u8_t vntag_version /* vntag counter */;
#elif defined(__LITTLE_ENDIAN)
	u8_t vntag_version /* vntag counter */;
	u8_t page_log_size /* Page log size */;
	union fcoe_vlan_vif_field_union multi_func_val /* Outer vlan vif union */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t fcp_cmd_frame_size /* FCP_CMD frame size */;
	u16_t pbf_addr_ff /* PBF addr with ff */;
#elif defined(__LITTLE_ENDIAN)
	u16_t pbf_addr_ff /* PBF addr with ff */;
	u16_t fcp_cmd_frame_size /* FCP_CMD frame size */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t vlan_num /* Vlan number */;
	u8_t cos /* Cos */;
	u8_t cache_xfrq_cons /* Cache xferq consumer */;
	u8_t cache_sq_cons /* Cache sq consumer */;
#elif defined(__LITTLE_ENDIAN)
	u8_t cache_sq_cons /* Cache sq consumer */;
	u8_t cache_xfrq_cons /* Cache xferq consumer */;
	u8_t cos /* Cos */;
	u8_t vlan_num /* Vlan number */;
#endif
	u32_t verify_tx_seq /* Sequence number of last transmitted sequence in order to verify target did not send FCP_RSP before the actual transmission of PBF from the SGL */;
};

/*
 * Xstorm FCoE Storm Context
 */
struct xstorm_fcoe_st_context
{
	struct xstorm_fcoe_eth_context_section eth;
	struct xstorm_fcoe_context_section fcoe;
};

/*
 * Fcoe connection context 
 */
struct fcoe_context
{
	struct ustorm_fcoe_st_context ustorm_st_context /* Ustorm storm context */;
	struct tstorm_fcoe_st_context tstorm_st_context /* Tstorm storm context */;
	struct xstorm_fcoe_ag_context xstorm_ag_context /* Xstorm aggregative context */;
	struct tstorm_fcoe_ag_context tstorm_ag_context /* Tstorm aggregative context */;
	struct ustorm_fcoe_ag_context ustorm_ag_context /* Ustorm aggregative context */;
	struct timers_block_context timers_context /* Timers block context */;
	struct xstorm_fcoe_st_context xstorm_st_context /* Xstorm storm context */;
};




/*
 * FCoE init params passed by driver to FW in FCoE init ramrod $$KEEP_ENDIANNESS$$
 */
struct fcoe_init_ramrod_params
{
	struct fcoe_kwqe_init1 init_kwqe1;
	struct fcoe_kwqe_init2 init_kwqe2;
	struct fcoe_kwqe_init3 init_kwqe3;
	struct regpair_t eq_pbl_base /* Physical address of PBL */;
	u32_t eq_pbl_size /* PBL size */;
	u32_t reserved2;
	u16_t eq_prod /* EQ prdocuer */;
	u16_t sb_num /* Status block number */;
	u8_t sb_id /* Status block id (EQ consumer) */;
	u8_t reserved0;
	u16_t reserved1;
};


/*
 * FCoE statistics params buffer passed by driver to FW in FCoE statistics ramrod $$KEEP_ENDIANNESS$$
 */
struct fcoe_stat_ramrod_params
{
	struct fcoe_kwqe_stat stat_kwqe;
};





















/*
 * CQ DB CQ producer and pending completion counter
 */
struct iscsi_cq_db_prod_pnd_cmpltn_cnt
{
#if defined(__BIG_ENDIAN)
	u16_t cntr /* CQ pending completion counter */;
	u16_t prod /* Ustorm CQ producer , updated by Ustorm */;
#elif defined(__LITTLE_ENDIAN)
	u16_t prod /* Ustorm CQ producer , updated by Ustorm */;
	u16_t cntr /* CQ pending completion counter */;
#endif
};

/*
 * CQ DB pending completion ITT array
 */
struct iscsi_cq_db_prod_pnd_cmpltn_cnt_arr
{
	struct iscsi_cq_db_prod_pnd_cmpltn_cnt prod_pend_comp[8] /* CQ pending completion ITT array */;
};

/*
 * CQ DB pending completion ITT array
 */
struct iscsi_cq_db_pnd_comp_itt_arr
{
	u16_t itt[8] /* CQ pending completion ITT array */;
};

/*
 * Cstorm CQ sequence to notify array, updated by driver
 */
struct iscsi_cq_db_sqn_2_notify_arr
{
	u16_t sqn[8] /* Cstorm CQ sequence to notify array, updated by driver */;
};

/*
 * CQ DB
 */
struct iscsi_cq_db
{
	struct iscsi_cq_db_prod_pnd_cmpltn_cnt_arr cq_u_prod_pend_comp_ctr_arr /* Ustorm CQ producer and pending completion counter array, updated by Ustorm */;
	struct iscsi_cq_db_pnd_comp_itt_arr cq_c_pend_comp_itt_arr /* Cstorm CQ pending completion ITT array, updated by Cstorm */;
	struct iscsi_cq_db_sqn_2_notify_arr cq_drv_sqn_2_notify_arr /* Cstorm CQ sequence to notify array, updated by driver */;
	u32_t reserved[4] /* 16 byte allignment */;
};






/*
 * iSCSI KCQ CQE parameters
 */
union iscsi_kcqe_params
{
	u32_t reserved0[4];
};

/*
 * iSCSI KCQ CQE
 */
struct iscsi_kcqe
{
	u32_t iscsi_conn_id /* Drivers connection ID (only 16 bits are used) */;
	u32_t completion_status /* 0=command completed succesfuly, 1=command failed */;
	u32_t iscsi_conn_context_id /* Context ID of the iSCSI connection */;
	union iscsi_kcqe_params params /* command-specific parameters */;
#if defined(__BIG_ENDIAN)
	u8_t flags;
		#define ISCSI_KCQE_RESERVED0                                                         (0x7<<0) /* BitField flags 	 */
		#define ISCSI_KCQE_RESERVED0_SHIFT                                                   0
		#define ISCSI_KCQE_RAMROD_COMPLETION                                                 (0x1<<3) /* BitField flags 	Everest only - indicates whether this KCQE is a ramrod completion */
		#define ISCSI_KCQE_RAMROD_COMPLETION_SHIFT                                           3
		#define ISCSI_KCQE_LAYER_CODE                                                        (0x7<<4) /* BitField flags 	protocol layer (L2,L3,L4,L5,iSCSI) */
		#define ISCSI_KCQE_LAYER_CODE_SHIFT                                                  4
		#define ISCSI_KCQE_LINKED_WITH_NEXT                                                  (0x1<<7) /* BitField flags 	Indicates whether this KCQE is linked with the next KCQE */
		#define ISCSI_KCQE_LINKED_WITH_NEXT_SHIFT                                            7
	u8_t op_code /* iSCSI KCQ opcode */;
	u16_t qe_self_seq /* Self identifying sequence number */;
#elif defined(__LITTLE_ENDIAN)
	u16_t qe_self_seq /* Self identifying sequence number */;
	u8_t op_code /* iSCSI KCQ opcode */;
	u8_t flags;
		#define ISCSI_KCQE_RESERVED0                                                         (0x7<<0) /* BitField flags 	 */
		#define ISCSI_KCQE_RESERVED0_SHIFT                                                   0
		#define ISCSI_KCQE_RAMROD_COMPLETION                                                 (0x1<<3) /* BitField flags 	Everest only - indicates whether this KCQE is a ramrod completion */
		#define ISCSI_KCQE_RAMROD_COMPLETION_SHIFT                                           3
		#define ISCSI_KCQE_LAYER_CODE                                                        (0x7<<4) /* BitField flags 	protocol layer (L2,L3,L4,L5,iSCSI) */
		#define ISCSI_KCQE_LAYER_CODE_SHIFT                                                  4
		#define ISCSI_KCQE_LINKED_WITH_NEXT                                                  (0x1<<7) /* BitField flags 	Indicates whether this KCQE is linked with the next KCQE */
		#define ISCSI_KCQE_LINKED_WITH_NEXT_SHIFT                                            7
#endif
};



/*
 * iSCSI KWQE header
 */
struct iscsi_kwqe_header
{
#if defined(__BIG_ENDIAN)
	u8_t flags;
		#define ISCSI_KWQE_HEADER_RESERVED0                                                  (0xF<<0) /* BitField flags 	 */
		#define ISCSI_KWQE_HEADER_RESERVED0_SHIFT                                            0
		#define ISCSI_KWQE_HEADER_LAYER_CODE                                                 (0x7<<4) /* BitField flags 	protocol layer (L2,L3,L4,L5,iSCSI) */
		#define ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT                                           4
		#define ISCSI_KWQE_HEADER_RESERVED1                                                  (0x1<<7) /* BitField flags 	 */
		#define ISCSI_KWQE_HEADER_RESERVED1_SHIFT                                            7
	u8_t op_code /* iSCSI KWQE opcode */;
#elif defined(__LITTLE_ENDIAN)
	u8_t op_code /* iSCSI KWQE opcode */;
	u8_t flags;
		#define ISCSI_KWQE_HEADER_RESERVED0                                                  (0xF<<0) /* BitField flags 	 */
		#define ISCSI_KWQE_HEADER_RESERVED0_SHIFT                                            0
		#define ISCSI_KWQE_HEADER_LAYER_CODE                                                 (0x7<<4) /* BitField flags 	protocol layer (L2,L3,L4,L5,iSCSI) */
		#define ISCSI_KWQE_HEADER_LAYER_CODE_SHIFT                                           4
		#define ISCSI_KWQE_HEADER_RESERVED1                                                  (0x1<<7) /* BitField flags 	 */
		#define ISCSI_KWQE_HEADER_RESERVED1_SHIFT                                            7
#endif
};

/*
 * iSCSI firmware init request 1
 */
struct iscsi_kwqe_init1
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
	u8_t hsi_version /* HSI version number */;
	u8_t num_cqs /* Number of completion queues */;
#elif defined(__LITTLE_ENDIAN)
	u8_t num_cqs /* Number of completion queues */;
	u8_t hsi_version /* HSI version number */;
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
#endif
	u32_t dummy_buffer_addr_lo /* Lower 32-bit of dummy buffer - Teton only */;
	u32_t dummy_buffer_addr_hi /* Higher 32-bit of dummy buffer - Teton only */;
#if defined(__BIG_ENDIAN)
	u16_t num_ccells_per_conn /* Number of ccells per connection */;
	u16_t num_tasks_per_conn /* Number of tasks per connection */;
#elif defined(__LITTLE_ENDIAN)
	u16_t num_tasks_per_conn /* Number of tasks per connection */;
	u16_t num_ccells_per_conn /* Number of ccells per connection */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t sq_wqes_per_page /* Number of work entries in a single page of SQ */;
	u16_t sq_num_wqes /* Number of entries in the Send Queue */;
#elif defined(__LITTLE_ENDIAN)
	u16_t sq_num_wqes /* Number of entries in the Send Queue */;
	u16_t sq_wqes_per_page /* Number of work entries in a single page of SQ */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t cq_log_wqes_per_page /* Log of number of work entries in a single page of CQ */;
	u8_t flags;
		#define ISCSI_KWQE_INIT1_PAGE_SIZE                                                   (0xF<<0) /* BitField flags 	page size code */
		#define ISCSI_KWQE_INIT1_PAGE_SIZE_SHIFT                                             0
		#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE                                          (0x1<<4) /* BitField flags 	if set, delayed ack is enabled */
		#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE_SHIFT                                    4
		#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE                                           (0x1<<5) /* BitField flags 	if set, keep alive is enabled */
		#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE_SHIFT                                     5
		#define ISCSI_KWQE_INIT1_RESERVED1                                                   (0x3<<6) /* BitField flags 	 */
		#define ISCSI_KWQE_INIT1_RESERVED1_SHIFT                                             6
	u16_t cq_num_wqes /* Number of entries in the Completion Queue */;
#elif defined(__LITTLE_ENDIAN)
	u16_t cq_num_wqes /* Number of entries in the Completion Queue */;
	u8_t flags;
		#define ISCSI_KWQE_INIT1_PAGE_SIZE                                                   (0xF<<0) /* BitField flags 	page size code */
		#define ISCSI_KWQE_INIT1_PAGE_SIZE_SHIFT                                             0
		#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE                                          (0x1<<4) /* BitField flags 	if set, delayed ack is enabled */
		#define ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE_SHIFT                                    4
		#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE                                           (0x1<<5) /* BitField flags 	if set, keep alive is enabled */
		#define ISCSI_KWQE_INIT1_KEEP_ALIVE_ENABLE_SHIFT                                     5
		#define ISCSI_KWQE_INIT1_RESERVED1                                                   (0x3<<6) /* BitField flags 	 */
		#define ISCSI_KWQE_INIT1_RESERVED1_SHIFT                                             6
	u8_t cq_log_wqes_per_page /* Log of number of work entries in a single page of CQ */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t cq_num_pages /* Number of pages in CQ page table */;
	u16_t sq_num_pages /* Number of pages in SQ page table */;
#elif defined(__LITTLE_ENDIAN)
	u16_t sq_num_pages /* Number of pages in SQ page table */;
	u16_t cq_num_pages /* Number of pages in CQ page table */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t rq_buffer_size /* Size of a single buffer (entry) in the RQ */;
	u16_t rq_num_wqes /* Number of entries in the Receive Queue */;
#elif defined(__LITTLE_ENDIAN)
	u16_t rq_num_wqes /* Number of entries in the Receive Queue */;
	u16_t rq_buffer_size /* Size of a single buffer (entry) in the RQ */;
#endif
};

/*
 * iSCSI firmware init request 2
 */
struct iscsi_kwqe_init2
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
	u16_t max_cq_sqn /* CQ wraparound value */;
#elif defined(__LITTLE_ENDIAN)
	u16_t max_cq_sqn /* CQ wraparound value */;
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
#endif
	u32_t error_bit_map[2] /* bit per error type, 0=error, 1=warning */;
	u32_t tcp_keepalive /* TCP keepalive time in seconds */;
	u32_t reserved1[4];
};

/*
 * Initial iSCSI connection offload request 1
 */
struct iscsi_kwqe_conn_offload1
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
	u16_t iscsi_conn_id /* Drivers connection ID. Should be sent in KCQEs to speed-up drivers access to connection data. */;
#elif defined(__LITTLE_ENDIAN)
	u16_t iscsi_conn_id /* Drivers connection ID. Should be sent in KCQEs to speed-up drivers access to connection data. */;
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
#endif
	u32_t sq_page_table_addr_lo /* Lower 32-bit of the SQs page table address */;
	u32_t sq_page_table_addr_hi /* Higher 32-bit of the SQs page table address */;
	u32_t cq_page_table_addr_lo /* Lower 32-bit of the CQs page table address */;
	u32_t cq_page_table_addr_hi /* Higher 32-bit of the CQs page table address */;
	u32_t reserved0[3];
};

/*
 * iSCSI Page Table Entry (PTE)
 */
struct iscsi_pte
{
	u32_t hi /* Higher 32 bits of address */;
	u32_t lo /* Lower 32 bits of address */;
};

/*
 * Initial iSCSI connection offload request 2
 */
struct iscsi_kwqe_conn_offload2
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQE header */;
	u16_t reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16_t reserved0;
	struct iscsi_kwqe_header hdr /* KWQE header */;
#endif
	u32_t rq_page_table_addr_lo /* Lower 32-bits of the RQs page table address */;
	u32_t rq_page_table_addr_hi /* Higher 32-bits of the RQs page table address */;
	struct iscsi_pte sq_first_pte /* first SQ page table entry (for FW caching) */;
	struct iscsi_pte cq_first_pte /* first CQ page table entry (for FW caching) */;
	u32_t num_additional_wqes /* Everest specific - number of offload3 KWQEs that will follow this KWQE */;
};

/*
 * Everest specific - Initial iSCSI connection offload request 3
 */
struct iscsi_kwqe_conn_offload3
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQE header */;
	u16_t reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16_t reserved0;
	struct iscsi_kwqe_header hdr /* KWQE header */;
#endif
	u32_t reserved1;
	struct iscsi_pte qp_first_pte[3] /* first page table entry of some iSCSI ring (for FW caching) */;
};

/*
 * iSCSI connection update request
 */
struct iscsi_kwqe_conn_update
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQE header */;
	u16_t reserved0;
#elif defined(__LITTLE_ENDIAN)
	u16_t reserved0;
	struct iscsi_kwqe_header hdr /* KWQE header */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t session_error_recovery_level /* iSCSI Error Recovery Level negotiated on this connection */;
	u8_t max_outstanding_r2ts /* Maximum number of outstanding R2ts that a target can send for a command */;
	u8_t reserved2;
	u8_t conn_flags;
		#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST                                         (0x1<<0) /* BitField conn_flags 	0=off, 1=on */
		#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST_SHIFT                                   0
		#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST                                           (0x1<<1) /* BitField conn_flags 	0=off, 1=on */
		#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST_SHIFT                                     1
		#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T                                           (0x1<<2) /* BitField conn_flags 	0=no, 1=yes */
		#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T_SHIFT                                     2
		#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA                                        (0x1<<3) /* BitField conn_flags 	0=no, 1=yes */
		#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA_SHIFT                                  3
		#define ISCSI_KWQE_CONN_UPDATE_OOO_SUPPORT_MODE                                      (0x3<<4) /* BitField conn_flags 	use enum tcp_tstorm_ooo */
		#define ISCSI_KWQE_CONN_UPDATE_OOO_SUPPORT_MODE_SHIFT                                4
		#define ISCSI_KWQE_CONN_UPDATE_RESERVED1                                             (0x3<<6) /* BitField conn_flags 	 */
		#define ISCSI_KWQE_CONN_UPDATE_RESERVED1_SHIFT                                       6
#elif defined(__LITTLE_ENDIAN)
	u8_t conn_flags;
		#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST                                         (0x1<<0) /* BitField conn_flags 	0=off, 1=on */
		#define ISCSI_KWQE_CONN_UPDATE_HEADER_DIGEST_SHIFT                                   0
		#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST                                           (0x1<<1) /* BitField conn_flags 	0=off, 1=on */
		#define ISCSI_KWQE_CONN_UPDATE_DATA_DIGEST_SHIFT                                     1
		#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T                                           (0x1<<2) /* BitField conn_flags 	0=no, 1=yes */
		#define ISCSI_KWQE_CONN_UPDATE_INITIAL_R2T_SHIFT                                     2
		#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA                                        (0x1<<3) /* BitField conn_flags 	0=no, 1=yes */
		#define ISCSI_KWQE_CONN_UPDATE_IMMEDIATE_DATA_SHIFT                                  3
		#define ISCSI_KWQE_CONN_UPDATE_OOO_SUPPORT_MODE                                      (0x3<<4) /* BitField conn_flags 	use enum tcp_tstorm_ooo */
		#define ISCSI_KWQE_CONN_UPDATE_OOO_SUPPORT_MODE_SHIFT                                4
		#define ISCSI_KWQE_CONN_UPDATE_RESERVED1                                             (0x3<<6) /* BitField conn_flags 	 */
		#define ISCSI_KWQE_CONN_UPDATE_RESERVED1_SHIFT                                       6
	u8_t reserved2;
	u8_t max_outstanding_r2ts /* Maximum number of outstanding R2ts that a target can send for a command */;
	u8_t session_error_recovery_level /* iSCSI Error Recovery Level negotiated on this connection */;
#endif
	u32_t context_id /* Context ID of the iSCSI connection */;
	u32_t max_send_pdu_length /* Maximum length of a PDU that the target can receive */;
	u32_t max_recv_pdu_length /* Maximum length of a PDU that the Initiator can receive */;
	u32_t first_burst_length /* Maximum length of the immediate and unsolicited data that Initiator can send */;
	u32_t max_burst_length /* Maximum length of the data that Initiator and target can send in one burst */;
	u32_t exp_stat_sn /* Expected Status Serial Number */;
};

/*
 * iSCSI destroy connection request
 */
struct iscsi_kwqe_conn_destroy
{
#if defined(__BIG_ENDIAN)
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
	u16_t iscsi_conn_id /* Drivers connection ID. Should be sent in KCQEs to speed-up drivers access to connection data. */;
#elif defined(__LITTLE_ENDIAN)
	u16_t iscsi_conn_id /* Drivers connection ID. Should be sent in KCQEs to speed-up drivers access to connection data. */;
	struct iscsi_kwqe_header hdr /* KWQ WQE header */;
#endif
	u32_t context_id /* Context ID of the iSCSI connection */;
	u32_t reserved1[6];
};

/*
 * iSCSI KWQ WQE
 */
union iscsi_kwqe
{
	struct iscsi_kwqe_init1 init1;
	struct iscsi_kwqe_init2 init2;
	struct iscsi_kwqe_conn_offload1 conn_offload1;
	struct iscsi_kwqe_conn_offload2 conn_offload2;
	struct iscsi_kwqe_conn_offload3 conn_offload3;
	struct iscsi_kwqe_conn_update conn_update;
	struct iscsi_kwqe_conn_destroy conn_destroy;
};











struct iscsi_rq_db
{
#if defined(__BIG_ENDIAN)
	u16_t reserved1;
	u16_t rq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16_t rq_prod;
	u16_t reserved1;
#endif
	u32_t __fw_hdr[15] /* Used by FW for partial header placement */;
};


struct iscsi_sq_db
{
#if defined(__BIG_ENDIAN)
	u16_t reserved0 /* Pad structure size to 16 bytes */;
	u16_t sq_prod;
#elif defined(__LITTLE_ENDIAN)
	u16_t sq_prod;
	u16_t reserved0 /* Pad structure size to 16 bytes */;
#endif
	u32_t reserved1[3] /* Pad structure size to 16 bytes */;
};


/*
 * Cstorm iSCSI Storm Context
 */
struct cstorm_iscsi_st_context
{
	struct iscsi_cq_db_prod_pnd_cmpltn_cnt_arr cq_c_prod_pend_comp_ctr_arr /* Cstorm CQ producer and CQ pending completion array, updated by Cstorm */;
	struct iscsi_cq_db_sqn_2_notify_arr cq_c_prod_sqn_arr /* Cstorm CQ producer sequence, updated by Cstorm */;
	struct iscsi_cq_db_sqn_2_notify_arr cq_c_sqn_2_notify_arr /* Event Coalescing CQ sequence to notify driver, copied by Cstorm from CQ DB that is updated by Driver */;
	struct regpair_t hq_pbl_base /* HQ PBL base */;
	struct regpair_t hq_curr_pbe /* HQ current PBE */;
	struct regpair_t task_pbl_base /* Task Context Entry PBL base */;
	struct regpair_t cq_db_base /* pointer to CQ DB array. each CQ DB entry consists of CQ PBL, arm bit and idx to notify */;
#if defined(__BIG_ENDIAN)
	u16_t hq_bd_itt /* copied from HQ BD */;
	u16_t iscsi_conn_id;
#elif defined(__LITTLE_ENDIAN)
	u16_t iscsi_conn_id;
	u16_t hq_bd_itt /* copied from HQ BD */;
#endif
	u32_t hq_bd_data_segment_len /* copied from HQ BD */;
	u32_t hq_bd_buffer_offset /* copied from HQ BD */;
#if defined(__BIG_ENDIAN)
	u8_t rsrv;
	u8_t cq_proc_en_bit_map /* CQ processing enable bit map, 1 bit per CQ */;
	u8_t cq_pend_comp_itt_valid_bit_map /* CQ pending completion ITT valid bit map, 1 bit per CQ */;
	u8_t hq_bd_opcode /* copied from HQ BD */;
#elif defined(__LITTLE_ENDIAN)
	u8_t hq_bd_opcode /* copied from HQ BD */;
	u8_t cq_pend_comp_itt_valid_bit_map /* CQ pending completion ITT valid bit map, 1 bit per CQ */;
	u8_t cq_proc_en_bit_map /* CQ processing enable bit map, 1 bit per CQ */;
	u8_t rsrv;
#endif
	u32_t hq_tcp_seq /* TCP sequence of next BD to release */;
#if defined(__BIG_ENDIAN)
	u16_t flags;
		#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN                                       (0x1<<0) /* BitField flags 	 */
		#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN_SHIFT                                 0
		#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN                                        (0x1<<1) /* BitField flags 	 */
		#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN_SHIFT                                  1
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID                                     (0x1<<2) /* BitField flags 	copied from HQ BD */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID_SHIFT                               2
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG                                  (0x1<<3) /* BitField flags 	copied from HQ BD */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG_SHIFT                            3
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK                                     (0x1<<4) /* BitField flags 	calculated using HQ BD opcode and write flag */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK_SHIFT                               4
		#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV                                      (0x7FF<<5) /* BitField flags 	 */
		#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV_SHIFT                                5
	u16_t hq_cons /* HQ consumer */;
#elif defined(__LITTLE_ENDIAN)
	u16_t hq_cons /* HQ consumer */;
	u16_t flags;
		#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN                                       (0x1<<0) /* BitField flags 	 */
		#define CSTORM_ISCSI_ST_CONTEXT_DATA_DIGEST_EN_SHIFT                                 0
		#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN                                        (0x1<<1) /* BitField flags 	 */
		#define CSTORM_ISCSI_ST_CONTEXT_HDR_DIGEST_EN_SHIFT                                  1
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID                                     (0x1<<2) /* BitField flags 	copied from HQ BD */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_CTXT_VALID_SHIFT                               2
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG                                  (0x1<<3) /* BitField flags 	copied from HQ BD */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_LCL_CMPLN_FLG_SHIFT                            3
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK                                     (0x1<<4) /* BitField flags 	calculated using HQ BD opcode and write flag */
		#define CSTORM_ISCSI_ST_CONTEXT_HQ_BD_WRITE_TASK_SHIFT                               4
		#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV                                      (0x7FF<<5) /* BitField flags 	 */
		#define CSTORM_ISCSI_ST_CONTEXT_CTRL_FLAGS_RSRV_SHIFT                                5
#endif
	struct regpair_t rsrv1;
};


/*
 * SCSI read/write SQ WQE
 */
struct iscsi_cmd_pdu_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	u8_t opcode;
	u8_t op_attr;
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_ATTRIBUTES                                   (0x7<<0) /* BitField op_attr 	Attributes of the SCSI command. To be sent with the outgoing command PDU. */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_ATTRIBUTES_SHIFT                             0
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_RSRV1                                        (0x3<<3) /* BitField op_attr 	 */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                  3
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_WRITE_FLAG                                   (0x1<<5) /* BitField op_attr 	Write bit. Initiator is expected to send the data to the target */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_WRITE_FLAG_SHIFT                             5
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_READ_FLAG                                    (0x1<<6) /* BitField op_attr 	Read bit. Data from target is expected */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_READ_FLAG_SHIFT                              6
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG                                   (0x1<<7) /* BitField op_attr 	Final bit. Firmware can change this bit based on the command before putting it into the outgoing PDU. */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG_SHIFT                             7
	u16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	u16_t rsrv0;
	u8_t op_attr;
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_ATTRIBUTES                                   (0x7<<0) /* BitField op_attr 	Attributes of the SCSI command. To be sent with the outgoing command PDU. */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_ATTRIBUTES_SHIFT                             0
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_RSRV1                                        (0x3<<3) /* BitField op_attr 	 */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                  3
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_WRITE_FLAG                                   (0x1<<5) /* BitField op_attr 	Write bit. Initiator is expected to send the data to the target */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_WRITE_FLAG_SHIFT                             5
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_READ_FLAG                                    (0x1<<6) /* BitField op_attr 	Read bit. Data from target is expected */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_READ_FLAG_SHIFT                              6
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG                                   (0x1<<7) /* BitField op_attr 	Final bit. Firmware can change this bit based on the command before putting it into the outgoing PDU. */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG_SHIFT                             7
	u8_t opcode;
#endif
	u32_t data_fields;
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                          (0xFFFFFF<<0) /* BitField data_fields 	 */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                    0
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                             (0xFF<<24) /* BitField data_fields 	 */
		#define ISCSI_CMD_PDU_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                       24
	struct regpair_t lun;
	u32_t itt;
	u32_t expected_data_transfer_length;
	u32_t cmd_sn;
	u32_t exp_stat_sn;
	u32_t scsi_command_block[4];
};


/*
 * Buffer per connection, used in Tstorm
 */
struct iscsi_conn_buf
{
	struct regpair_t reserved[8];
};


/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_rq_db
{
	struct regpair_t pbl_base /* Pointer to the rq page base list. */;
	struct regpair_t curr_pbe /* Pointer to the current rq page base. */;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_r2tq_db
{
	struct regpair_t pbl_base /* Pointer to the r2tq page base list. */;
	struct regpair_t curr_pbe /* Pointer to the current r2tq page base. */;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_cq_db
{
#if defined(__BIG_ENDIAN)
	u16_t cq_sn /* CQ serial number */;
	u16_t prod /* CQ producer */;
#elif defined(__LITTLE_ENDIAN)
	u16_t prod /* CQ producer */;
	u16_t cq_sn /* CQ serial number */;
#endif
	struct regpair_t curr_pbe /* Pointer to the current cq page base. */;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct rings_db
{
	struct ustorm_iscsi_rq_db rq /* RQ db. */;
	struct ustorm_iscsi_r2tq_db r2tq /* R2TQ db. */;
	struct ustorm_iscsi_cq_db cq[8] /* CQ db. */;
#if defined(__BIG_ENDIAN)
	u16_t rq_prod /* RQ prod */;
	u16_t r2tq_prod /* R2TQ producer. */;
#elif defined(__LITTLE_ENDIAN)
	u16_t r2tq_prod /* R2TQ producer. */;
	u16_t rq_prod /* RQ prod */;
#endif
	struct regpair_t cq_pbl_base /* Pointer to the cq page base list. */;
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct ustorm_iscsi_placement_db
{
	u32_t sgl_base_lo /* SGL base address lo */;
	u32_t sgl_base_hi /* SGL base address hi */;
	u32_t local_sge_0_address_hi /* SGE address hi */;
	u32_t local_sge_0_address_lo /* SGE address lo */;
#if defined(__BIG_ENDIAN)
	u16_t curr_sge_offset /* Current offset in the SGE */;
	u16_t local_sge_0_size /* SGE size */;
#elif defined(__LITTLE_ENDIAN)
	u16_t local_sge_0_size /* SGE size */;
	u16_t curr_sge_offset /* Current offset in the SGE */;
#endif
	u32_t local_sge_1_address_hi /* SGE address hi */;
	u32_t local_sge_1_address_lo /* SGE address lo */;
#if defined(__BIG_ENDIAN)
	u8_t exp_padding_2b /* Number of padding bytes not yet processed */;
	u8_t nal_len_3b /* Non 4 byte aligned bytes in the previous iteration */;
	u16_t local_sge_1_size /* SGE size */;
#elif defined(__LITTLE_ENDIAN)
	u16_t local_sge_1_size /* SGE size */;
	u8_t nal_len_3b /* Non 4 byte aligned bytes in the previous iteration */;
	u8_t exp_padding_2b /* Number of padding bytes not yet processed */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t sgl_size /* Number of SGEs remaining till end of SGL */;
	u8_t local_sge_index_2b /* Index to the local SGE currently used */;
	u16_t reserved7;
#elif defined(__LITTLE_ENDIAN)
	u16_t reserved7;
	u8_t local_sge_index_2b /* Index to the local SGE currently used */;
	u8_t sgl_size /* Number of SGEs remaining till end of SGL */;
#endif
	u32_t rem_pdu /* Number of bytes remaining in PDU */;
	u32_t place_db_bitfield_1;
		#define USTORM_ISCSI_PLACEMENT_DB_REM_PDU_PAYLOAD                                    (0xFFFFFF<<0) /* BitField place_db_bitfield_1 place_db_bitfield_1	Number of bytes remaining in PDU payload */
		#define USTORM_ISCSI_PLACEMENT_DB_REM_PDU_PAYLOAD_SHIFT                              0
		#define USTORM_ISCSI_PLACEMENT_DB_CQ_ID                                              (0xFF<<24) /* BitField place_db_bitfield_1 place_db_bitfield_1	Temp task context - determines the CQ index for CQE placement */
		#define USTORM_ISCSI_PLACEMENT_DB_CQ_ID_SHIFT                                        24
	u32_t place_db_bitfield_2;
		#define USTORM_ISCSI_PLACEMENT_DB_BYTES_2_TRUNCATE                                   (0xFFFFFF<<0) /* BitField place_db_bitfield_2 place_db_bitfield_2	Bytes to truncate from the payload. */
		#define USTORM_ISCSI_PLACEMENT_DB_BYTES_2_TRUNCATE_SHIFT                             0
		#define USTORM_ISCSI_PLACEMENT_DB_HOST_SGE_INDEX                                     (0xFF<<24) /* BitField place_db_bitfield_2 place_db_bitfield_2	Sge index on host */
		#define USTORM_ISCSI_PLACEMENT_DB_HOST_SGE_INDEX_SHIFT                               24
	u32_t nal;
		#define USTORM_ISCSI_PLACEMENT_DB_REM_SGE_SIZE                                       (0xFFFFFF<<0) /* BitField nal Non aligned db	Number of bytes remaining in local SGEs */
		#define USTORM_ISCSI_PLACEMENT_DB_REM_SGE_SIZE_SHIFT                                 0
		#define USTORM_ISCSI_PLACEMENT_DB_EXP_DIGEST_3B                                      (0xFF<<24) /* BitField nal Non aligned db	Number of digest bytes not yet processed */
		#define USTORM_ISCSI_PLACEMENT_DB_EXP_DIGEST_3B_SHIFT                                24
};

/*
 * Ustorm iSCSI Storm Context
 */
struct ustorm_iscsi_st_context
{
	u32_t exp_stat_sn /* Expected status sequence number, incremented with each response/middle path/unsolicited received. */;
	u32_t exp_data_sn /* Expected Data sequence number, incremented with each data in */;
	struct rings_db ring /* rq, r2tq ,cq */;
	struct regpair_t task_pbl_base /* Task PBL base will be read from RAM to context */;
	struct regpair_t tce_phy_addr /* Pointer to the task context physical address */;
	struct ustorm_iscsi_placement_db place_db;
	u32_t reserved8 /* reserved */;
	u32_t rem_rcv_len /* Temp task context - Remaining bytes to end of task */;
#if defined(__BIG_ENDIAN)
	u16_t hdr_itt /* field copied from PDU header */;
	u16_t iscsi_conn_id;
#elif defined(__LITTLE_ENDIAN)
	u16_t iscsi_conn_id;
	u16_t hdr_itt /* field copied from PDU header */;
#endif
	u32_t nal_bytes /* nal bytes read from BRB */;
#if defined(__BIG_ENDIAN)
	u8_t hdr_second_byte_union /* field copied from PDU header */;
	u8_t bitfield_0;
		#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU                                         (0x1<<0) /* BitField bitfield_0 bitfield_0	marks that processing of payload has started */
		#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU_SHIFT                                   0
		#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE                                            (0x1<<1) /* BitField bitfield_0 bitfield_0	marks that fence is need on the next CQE */
		#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE_SHIFT                                      1
		#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC                                            (0x1<<2) /* BitField bitfield_0 bitfield_0	marks that a RESET should be sent to CRC machine. Used in NAL condition in the beginning of a PDU. */
		#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC_SHIFT                                      2
		#define USTORM_ISCSI_ST_CONTEXT_RESERVED1                                            (0x1F<<3) /* BitField bitfield_0 bitfield_0	reserved */
		#define USTORM_ISCSI_ST_CONTEXT_RESERVED1_SHIFT                                      3
	u8_t task_pdu_cache_index;
	u8_t task_pbe_cache_index;
#elif defined(__LITTLE_ENDIAN)
	u8_t task_pbe_cache_index;
	u8_t task_pdu_cache_index;
	u8_t bitfield_0;
		#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU                                         (0x1<<0) /* BitField bitfield_0 bitfield_0	marks that processing of payload has started */
		#define USTORM_ISCSI_ST_CONTEXT_BMIDDLEOFPDU_SHIFT                                   0
		#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE                                            (0x1<<1) /* BitField bitfield_0 bitfield_0	marks that fence is need on the next CQE */
		#define USTORM_ISCSI_ST_CONTEXT_BFENCECQE_SHIFT                                      1
		#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC                                            (0x1<<2) /* BitField bitfield_0 bitfield_0	marks that a RESET should be sent to CRC machine. Used in NAL condition in the beginning of a PDU. */
		#define USTORM_ISCSI_ST_CONTEXT_BRESETCRC_SHIFT                                      2
		#define USTORM_ISCSI_ST_CONTEXT_RESERVED1                                            (0x1F<<3) /* BitField bitfield_0 bitfield_0	reserved */
		#define USTORM_ISCSI_ST_CONTEXT_RESERVED1_SHIFT                                      3
	u8_t hdr_second_byte_union /* field copied from PDU header */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t reserved3 /* reserved */;
	u8_t reserved2 /* reserved */;
	u8_t acDecrement /* Manage the AC decrement that should be done by USDM */;
#elif defined(__LITTLE_ENDIAN)
	u8_t acDecrement /* Manage the AC decrement that should be done by USDM */;
	u8_t reserved2 /* reserved */;
	u16_t reserved3 /* reserved */;
#endif
	u32_t task_stat /* counts dataIn for read and holds data outs, r2t for write */;
#if defined(__BIG_ENDIAN)
	u8_t hdr_opcode /* field copied from PDU header */;
	u8_t num_cqs /* Number of CQs supported by this connection */;
	u16_t reserved5 /* reserved */;
#elif defined(__LITTLE_ENDIAN)
	u16_t reserved5 /* reserved */;
	u8_t num_cqs /* Number of CQs supported by this connection */;
	u8_t hdr_opcode /* field copied from PDU header */;
#endif
	u32_t negotiated_rx;
		#define USTORM_ISCSI_ST_CONTEXT_MAX_RECV_PDU_LENGTH                                  (0xFFFFFF<<0) /* BitField negotiated_rx 	 */
		#define USTORM_ISCSI_ST_CONTEXT_MAX_RECV_PDU_LENGTH_SHIFT                            0
		#define USTORM_ISCSI_ST_CONTEXT_MAX_OUTSTANDING_R2TS                                 (0xFF<<24) /* BitField negotiated_rx 	 */
		#define USTORM_ISCSI_ST_CONTEXT_MAX_OUTSTANDING_R2TS_SHIFT                           24
	u32_t negotiated_rx_and_flags;
		#define USTORM_ISCSI_ST_CONTEXT_MAX_BURST_LENGTH                                     (0xFFFFFF<<0) /* BitField negotiated_rx_and_flags 	Negotiated maximum length of sequence */
		#define USTORM_ISCSI_ST_CONTEXT_MAX_BURST_LENGTH_SHIFT                               0
		#define USTORM_ISCSI_ST_CONTEXT_B_CQE_POSTED_OR_HEADER_CACHED                        (0x1<<24) /* BitField negotiated_rx_and_flags 	Marks that unvalid CQE was already posted or PDU header was cachaed in RAM */
		#define USTORM_ISCSI_ST_CONTEXT_B_CQE_POSTED_OR_HEADER_CACHED_SHIFT                  24
		#define USTORM_ISCSI_ST_CONTEXT_B_HDR_DIGEST_EN                                      (0x1<<25) /* BitField negotiated_rx_and_flags 	Header digest support enable */
		#define USTORM_ISCSI_ST_CONTEXT_B_HDR_DIGEST_EN_SHIFT                                25
		#define USTORM_ISCSI_ST_CONTEXT_B_DATA_DIGEST_EN                                     (0x1<<26) /* BitField negotiated_rx_and_flags 	Data digest support enable */
		#define USTORM_ISCSI_ST_CONTEXT_B_DATA_DIGEST_EN_SHIFT                               26
		#define USTORM_ISCSI_ST_CONTEXT_B_PROTOCOL_ERROR                                     (0x1<<27) /* BitField negotiated_rx_and_flags 	 */
		#define USTORM_ISCSI_ST_CONTEXT_B_PROTOCOL_ERROR_SHIFT                               27
		#define USTORM_ISCSI_ST_CONTEXT_B_TASK_VALID                                         (0x1<<28) /* BitField negotiated_rx_and_flags 	temp task context */
		#define USTORM_ISCSI_ST_CONTEXT_B_TASK_VALID_SHIFT                                   28
		#define USTORM_ISCSI_ST_CONTEXT_TASK_TYPE                                            (0x3<<29) /* BitField negotiated_rx_and_flags 	Task type: 0 = slow-path (non-RW) 1 = read 2 = write */
		#define USTORM_ISCSI_ST_CONTEXT_TASK_TYPE_SHIFT                                      29
		#define USTORM_ISCSI_ST_CONTEXT_B_ALL_DATA_ACKED                                     (0x1<<31) /* BitField negotiated_rx_and_flags 	Set if all data is acked */
		#define USTORM_ISCSI_ST_CONTEXT_B_ALL_DATA_ACKED_SHIFT                               31
};

/*
 * TCP context region, shared in TOE, RDMA and ISCSI
 */
struct tstorm_tcp_st_context_section
{
	u32_t flags1;
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_SRTT                                       (0xFFFFFF<<0) /* BitField flags1 various state flags	20b only, Smoothed Rount Trip Time */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_SRTT_SHIFT                                 0
		#define TSTORM_TCP_ST_CONTEXT_SECTION_PAWS_INVALID                                   (0x1<<24) /* BitField flags1 various state flags	PAWS asserted as invalid in KA flow */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_PAWS_INVALID_SHIFT                             24
		#define TSTORM_TCP_ST_CONTEXT_SECTION_TIMESTAMP_EXISTS                               (0x1<<25) /* BitField flags1 various state flags	Timestamps supported on this connection */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_TIMESTAMP_EXISTS_SHIFT                         25
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RESERVED0                                      (0x1<<26) /* BitField flags1 various state flags	 */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RESERVED0_SHIFT                                26
		#define TSTORM_TCP_ST_CONTEXT_SECTION_STOP_RX_PAYLOAD                                (0x1<<27) /* BitField flags1 various state flags	stop receiving rx payload */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_STOP_RX_PAYLOAD_SHIFT                          27
		#define TSTORM_TCP_ST_CONTEXT_SECTION_KA_ENABLED                                     (0x1<<28) /* BitField flags1 various state flags	Keep Alive enabled */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_KA_ENABLED_SHIFT                               28
		#define TSTORM_TCP_ST_CONTEXT_SECTION_FIRST_RTO_ESTIMATE                             (0x1<<29) /* BitField flags1 various state flags	First Retransmition Timout Estimation */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_FIRST_RTO_ESTIMATE_SHIFT                       29
		#define TSTORM_TCP_ST_CONTEXT_SECTION_MAX_SEG_RETRANSMIT_EN                          (0x1<<30) /* BitField flags1 various state flags	per connection flag, signals whether to check if rt count exceeds max_seg_retransmit */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_MAX_SEG_RETRANSMIT_EN_SHIFT                    30
		#define TSTORM_TCP_ST_CONTEXT_SECTION_LAST_ISLE_HAS_FIN                              (0x1<<31) /* BitField flags1 various state flags	last isle ends with FIN. FIN is counted as 1 byte for isle end sequence */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_LAST_ISLE_HAS_FIN_SHIFT                        31
	u32_t flags2;
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_VARIATION                                  (0xFFFFFF<<0) /* BitField flags2 various state flags	20b only, Round Trip Time variation */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_RTT_VARIATION_SHIFT                            0
		#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_EN                                          (0x1<<24) /* BitField flags2 various state flags	 */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_EN_SHIFT                                    24
		#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_COUNTER_EN                                  (0x1<<25) /* BitField flags2 various state flags	per GOS flags, but duplicated for each context */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_DA_COUNTER_EN_SHIFT                            25
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_KA_PROBE_SENT                                (0x1<<26) /* BitField flags2 various state flags	keep alive packet was sent */
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_KA_PROBE_SENT_SHIFT                          26
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_PERSIST_PROBE_SENT                           (0x1<<27) /* BitField flags2 various state flags	persist packet was sent */
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_PERSIST_PROBE_SENT_SHIFT                     27
		#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L2_STATSTICS                            (0x1<<28) /* BitField flags2 various state flags	determines wheather or not to update l2 statistics */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L2_STATSTICS_SHIFT                      28
		#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L4_STATSTICS                            (0x1<<29) /* BitField flags2 various state flags	determines wheather or not to update l4 statistics */
		#define TSTORM_TCP_ST_CONTEXT_SECTION_UPDATE_L4_STATSTICS_SHIFT                      29
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_RST_ATTACK                         (0x1<<30) /* BitField flags2 various state flags	possible blind-in-window RST attack detected */
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_RST_ATTACK_SHIFT                   30
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_SYN_ATTACK                         (0x1<<31) /* BitField flags2 various state flags	possible blind-in-window SYN attack detected */
		#define __TSTORM_TCP_ST_CONTEXT_SECTION_IN_WINDOW_SYN_ATTACK_SHIFT                   31
#if defined(__BIG_ENDIAN)
	u16_t mss;
	u8_t tcp_sm_state /* 3b only, Tcp state machine state */;
	u8_t rto_exp /* 3b only, Exponential Backoff index */;
#elif defined(__LITTLE_ENDIAN)
	u8_t rto_exp /* 3b only, Exponential Backoff index */;
	u8_t tcp_sm_state /* 3b only, Tcp state machine state */;
	u16_t mss;
#endif
	u32_t rcv_nxt /* Receive sequence: next expected */;
	u32_t timestamp_recent /* last timestamp from segTS */;
	u32_t timestamp_recent_time /* time at which timestamp_recent has been set */;
	u32_t cwnd /* Congestion window */;
	u32_t ss_thresh /* Slow Start Threshold */;
	u32_t cwnd_accum /* Congestion window accumilation */;
	u32_t prev_seg_seq /* Sequence number used for last sndWnd update (was: snd_wnd_l1) */;
	u32_t expected_rel_seq /* the last update of rel_seq */;
	u32_t recover /* Recording of sndMax when we enter retransmit */;
#if defined(__BIG_ENDIAN)
	u8_t retransmit_count /* Number of times a packet was retransmitted */;
	u8_t ka_max_probe_count /* Keep Alive maximum probe counter */;
	u8_t persist_probe_count /* Persist probe counter */;
	u8_t ka_probe_count /* Keep Alive probe counter */;
#elif defined(__LITTLE_ENDIAN)
	u8_t ka_probe_count /* Keep Alive probe counter */;
	u8_t persist_probe_count /* Persist probe counter */;
	u8_t ka_max_probe_count /* Keep Alive maximum probe counter */;
	u8_t retransmit_count /* Number of times a packet was retransmitted */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t statistics_counter_id /* The ID of the statistics client for counting common/L2 statistics */;
	u8_t ooo_support_mode /* use enum tcp_tstorm_ooo */;
	u8_t snd_wnd_scale /* 4b only, Far-end window (Snd.Wind.Scale) scale */;
	u8_t dup_ack_count /* Duplicate Ack Counter */;
#elif defined(__LITTLE_ENDIAN)
	u8_t dup_ack_count /* Duplicate Ack Counter */;
	u8_t snd_wnd_scale /* 4b only, Far-end window (Snd.Wind.Scale) scale */;
	u8_t ooo_support_mode /* use enum tcp_tstorm_ooo */;
	u8_t statistics_counter_id /* The ID of the statistics client for counting common/L2 statistics */;
#endif
	u32_t retransmit_start_time /* Used by retransmit as a recording of start time */;
	u32_t ka_timeout /* Keep Alive timeout */;
	u32_t ka_interval /* Keep Alive interval */;
	u32_t isle_start_seq /* First Out-of-order isle start sequence */;
	u32_t isle_end_seq /* First Out-of-order isle end sequence */;
#if defined(__BIG_ENDIAN)
	u16_t second_isle_address /* address of the second isle (if exists) in internal RAM */;
	u16_t recent_seg_wnd /* Last far end window received (not scaled!) */;
#elif defined(__LITTLE_ENDIAN)
	u16_t recent_seg_wnd /* Last far end window received (not scaled!) */;
	u16_t second_isle_address /* address of the second isle (if exists) in internal RAM */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t max_isles_ever_happened /* for statistics only - max number of isles ever happened on this connection */;
	u8_t isles_number /* number of isles */;
	u16_t last_isle_address /* address of the last isle (if exists) in internal RAM */;
#elif defined(__LITTLE_ENDIAN)
	u16_t last_isle_address /* address of the last isle (if exists) in internal RAM */;
	u8_t isles_number /* number of isles */;
	u8_t max_isles_ever_happened /* for statistics only - max number of isles ever happened on this connection */;
#endif
	u32_t max_rt_time;
#if defined(__BIG_ENDIAN)
	u16_t lsb_mac_address /* TX source MAC LSB-16 */;
	u16_t vlan_id /* Connection-configured VLAN ID */;
#elif defined(__LITTLE_ENDIAN)
	u16_t vlan_id /* Connection-configured VLAN ID */;
	u16_t lsb_mac_address /* TX source MAC LSB-16 */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t msb_mac_address /* TX source MAC MSB-16 */;
	u16_t mid_mac_address /* TX source MAC MID-16 */;
#elif defined(__LITTLE_ENDIAN)
	u16_t mid_mac_address /* TX source MAC MID-16 */;
	u16_t msb_mac_address /* TX source MAC MSB-16 */;
#endif
	u32_t rightmost_received_seq /* The maximum sequence ever recieved - used for The New Patent */;
};

/*
 * Termination variables
 */
struct iscsi_term_vars
{
	u8_t BitMap;
		#define ISCSI_TERM_VARS_TCP_STATE                                                    (0xF<<0) /* BitField BitMap 	tcp state for the termination process */
		#define ISCSI_TERM_VARS_TCP_STATE_SHIFT                                              0
		#define ISCSI_TERM_VARS_FIN_RECEIVED_SBIT                                            (0x1<<4) /* BitField BitMap 	fin received sticky bit */
		#define ISCSI_TERM_VARS_FIN_RECEIVED_SBIT_SHIFT                                      4
		#define ISCSI_TERM_VARS_ACK_ON_FIN_RECEIVED_SBIT                                     (0x1<<5) /* BitField BitMap 	ack on fin received stick bit */
		#define ISCSI_TERM_VARS_ACK_ON_FIN_RECEIVED_SBIT_SHIFT                               5
		#define ISCSI_TERM_VARS_TERM_ON_CHIP                                                 (0x1<<6) /* BitField BitMap 	termination on chip ( option2 ) */
		#define ISCSI_TERM_VARS_TERM_ON_CHIP_SHIFT                                           6
		#define ISCSI_TERM_VARS_RSRV                                                         (0x1<<7) /* BitField BitMap 	 */
		#define ISCSI_TERM_VARS_RSRV_SHIFT                                                   7
};

/*
 * iSCSI context region, used only in iSCSI
 */
struct tstorm_iscsi_st_context_section
{
	u32_t nalPayload /* Non-aligned payload */;
	u32_t b2nh /* Number of bytes to next iSCSI header */;
#if defined(__BIG_ENDIAN)
	u16_t rq_cons /* RQ consumer */;
	u8_t flags;
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN                              (0x1<<0) /* BitField flags 	header digest enable, set at login stage */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN_SHIFT                        0
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN                             (0x1<<1) /* BitField flags 	data digest enable, set at login stage */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN_SHIFT                       1
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER                             (0x1<<2) /* BitField flags 	partial header flow indication */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER_SHIFT                       2
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE                               (0x1<<3) /* BitField flags 	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE_SHIFT                         3
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS                              (0x1<<4) /* BitField flags 	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS_SHIFT                        4
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_NALLEN                                       (0x3<<5) /* BitField flags 	Non-aligned length */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_NALLEN_SHIFT                                 5
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_RSRV0                                        (0x1<<7) /* BitField flags 	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_RSRV0_SHIFT                                  7
	u8_t hdr_bytes_2_fetch /* Number of bytes left to fetch to complete iSCSI header */;
#elif defined(__LITTLE_ENDIAN)
	u8_t hdr_bytes_2_fetch /* Number of bytes left to fetch to complete iSCSI header */;
	u8_t flags;
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN                              (0x1<<0) /* BitField flags 	header digest enable, set at login stage */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_HDR_DIGEST_EN_SHIFT                        0
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN                             (0x1<<1) /* BitField flags 	data digest enable, set at login stage */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DATA_DIGEST_EN_SHIFT                       1
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER                             (0x1<<2) /* BitField flags 	partial header flow indication */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_PARTIAL_HEADER_SHIFT                       2
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE                               (0x1<<3) /* BitField flags 	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_FULL_FEATURE_SHIFT                         3
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS                              (0x1<<4) /* BitField flags 	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_B_DROP_ALL_PDUS_SHIFT                        4
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_NALLEN                                       (0x3<<5) /* BitField flags 	Non-aligned length */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_NALLEN_SHIFT                                 5
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_RSRV0                                        (0x1<<7) /* BitField flags 	 */
		#define TSTORM_ISCSI_ST_CONTEXT_SECTION_RSRV0_SHIFT                                  7
	u16_t rq_cons /* RQ consumer */;
#endif
	struct regpair_t rq_db_phy_addr;
#if defined(__BIG_ENDIAN)
	struct iscsi_term_vars term_vars /* Termination variables */;
	u8_t rsrv1;
	u16_t iscsi_conn_id;
#elif defined(__LITTLE_ENDIAN)
	u16_t iscsi_conn_id;
	u8_t rsrv1;
	struct iscsi_term_vars term_vars /* Termination variables */;
#endif
	u32_t process_nxt /* next TCP sequence to be processed by the iSCSI layer. */;
};

/*
 * The iSCSI non-aggregative context of Tstorm
 */
struct tstorm_iscsi_st_context
{
	struct tstorm_tcp_st_context_section tcp /* TCP  context region, shared in TOE, RDMA and iSCSI */;
	struct tstorm_iscsi_st_context_section iscsi /* iSCSI context region, used only in iSCSI */;
};

/*
 * Ethernet context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_eth_context_section
{
#if defined(__BIG_ENDIAN)
	u8_t remote_addr_4 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_5 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_0 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_1 /* Local Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	u8_t local_addr_1 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_0 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_5 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_4 /* Remote Mac Address, used in PBF Header Builder Command */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t remote_addr_0 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_1 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_2 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_3 /* Remote Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	u8_t remote_addr_3 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_2 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_1 /* Remote Mac Address, used in PBF Header Builder Command */;
	u8_t remote_addr_0 /* Remote Mac Address, used in PBF Header Builder Command */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t reserved_vlan_type /* this field is not an absolute must, but the reseved was here */;
	u16_t params;
		#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID                                           (0xFFF<<0) /* BitField params 	part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT                                     0
		#define XSTORM_ETH_CONTEXT_SECTION_CFI                                               (0x1<<12) /* BitField params 	Canonical format indicator, part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_CFI_SHIFT                                         12
		#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY                                          (0x7<<13) /* BitField params 	part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY_SHIFT                                    13
#elif defined(__LITTLE_ENDIAN)
	u16_t params;
		#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID                                           (0xFFF<<0) /* BitField params 	part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_VLAN_ID_SHIFT                                     0
		#define XSTORM_ETH_CONTEXT_SECTION_CFI                                               (0x1<<12) /* BitField params 	Canonical format indicator, part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_CFI_SHIFT                                         12
		#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY                                          (0x7<<13) /* BitField params 	part of PBF Header Builder Command */
		#define XSTORM_ETH_CONTEXT_SECTION_PRIORITY_SHIFT                                    13
	u16_t reserved_vlan_type /* this field is not an absolute must, but the reseved was here */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t local_addr_2 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_3 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_4 /* Loca lMac Address, used in PBF Header Builder Command */;
	u8_t local_addr_5 /* Local Mac Address, used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	u8_t local_addr_5 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_4 /* Loca lMac Address, used in PBF Header Builder Command */;
	u8_t local_addr_3 /* Local Mac Address, used in PBF Header Builder Command */;
	u8_t local_addr_2 /* Local Mac Address, used in PBF Header Builder Command */;
#endif
};

/*
 * IpV4 context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_ip_v4_context_section
{
#if defined(__BIG_ENDIAN)
	u16_t __pbf_hdr_cmd_rsvd_id;
	u16_t __pbf_hdr_cmd_rsvd_flags_offset;
#elif defined(__LITTLE_ENDIAN)
	u16_t __pbf_hdr_cmd_rsvd_flags_offset;
	u16_t __pbf_hdr_cmd_rsvd_id;
#endif
#if defined(__BIG_ENDIAN)
	u8_t __pbf_hdr_cmd_rsvd_ver_ihl;
	u8_t tos /* Type Of Service, used in PBF Header Builder Command */;
	u16_t __pbf_hdr_cmd_rsvd_length;
#elif defined(__LITTLE_ENDIAN)
	u16_t __pbf_hdr_cmd_rsvd_length;
	u8_t tos /* Type Of Service, used in PBF Header Builder Command */;
	u8_t __pbf_hdr_cmd_rsvd_ver_ihl;
#endif
	u32_t ip_local_addr /* used in PBF Header Builder Command */;
#if defined(__BIG_ENDIAN)
	u8_t ttl /* Time to live, used in PBF Header Builder Command */;
	u8_t __pbf_hdr_cmd_rsvd_protocol;
	u16_t __pbf_hdr_cmd_rsvd_csum;
#elif defined(__LITTLE_ENDIAN)
	u16_t __pbf_hdr_cmd_rsvd_csum;
	u8_t __pbf_hdr_cmd_rsvd_protocol;
	u8_t ttl /* Time to live, used in PBF Header Builder Command */;
#endif
	u32_t __pbf_hdr_cmd_rsvd_1 /* places the ip_remote_addr field in the proper place in the regpair */;
	u32_t ip_remote_addr /* used in PBF Header Builder Command */;
};

/*
 * context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_padded_ip_v4_context_section
{
	struct xstorm_ip_v4_context_section ip_v4;
	u32_t reserved1[4];
};

/*
 * IpV6 context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_ip_v6_context_section
{
#if defined(__BIG_ENDIAN)
	u16_t pbf_hdr_cmd_rsvd_payload_len;
	u8_t pbf_hdr_cmd_rsvd_nxt_hdr;
	u8_t hop_limit /* used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	u8_t hop_limit /* used in PBF Header Builder Command */;
	u8_t pbf_hdr_cmd_rsvd_nxt_hdr;
	u16_t pbf_hdr_cmd_rsvd_payload_len;
#endif
	u32_t priority_flow_label;
		#define XSTORM_IP_V6_CONTEXT_SECTION_FLOW_LABEL                                      (0xFFFFF<<0) /* BitField priority_flow_label 	used in PBF Header Builder Command */
		#define XSTORM_IP_V6_CONTEXT_SECTION_FLOW_LABEL_SHIFT                                0
		#define XSTORM_IP_V6_CONTEXT_SECTION_TRAFFIC_CLASS                                   (0xFF<<20) /* BitField priority_flow_label 	used in PBF Header Builder Command */
		#define XSTORM_IP_V6_CONTEXT_SECTION_TRAFFIC_CLASS_SHIFT                             20
		#define XSTORM_IP_V6_CONTEXT_SECTION_PBF_HDR_CMD_RSVD_VER                            (0xF<<28) /* BitField priority_flow_label 	 */
		#define XSTORM_IP_V6_CONTEXT_SECTION_PBF_HDR_CMD_RSVD_VER_SHIFT                      28
	u32_t ip_local_addr_lo_hi /* second 32 bits of Ip local Address, used in PBF Header Builder Command */;
	u32_t ip_local_addr_lo_lo /* first 32 bits of Ip local Address, used in PBF Header Builder Command */;
	u32_t ip_local_addr_hi_hi /* fourth 32 bits of Ip local Address, used in PBF Header Builder Command */;
	u32_t ip_local_addr_hi_lo /* third 32 bits of Ip local Address, used in PBF Header Builder Command */;
	u32_t ip_remote_addr_lo_hi /* second 32 bits of Ip remoteinsation Address, used in PBF Header Builder Command */;
	u32_t ip_remote_addr_lo_lo /* first 32 bits of Ip remoteinsation Address, used in PBF Header Builder Command */;
	u32_t ip_remote_addr_hi_hi /* fourth 32 bits of Ip remoteinsation Address, used in PBF Header Builder Command */;
	u32_t ip_remote_addr_hi_lo /* third 32 bits of Ip remoteinsation Address, used in PBF Header Builder Command */;
};

union xstorm_ip_context_section_types
{
	struct xstorm_padded_ip_v4_context_section padded_ip_v4;
	struct xstorm_ip_v6_context_section ip_v6;
};

/*
 * TCP context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_tcp_context_section
{
	u32_t snd_max;
#if defined(__BIG_ENDIAN)
	u16_t remote_port /* used in PBF Header Builder Command */;
	u16_t local_port /* used in PBF Header Builder Command */;
#elif defined(__LITTLE_ENDIAN)
	u16_t local_port /* used in PBF Header Builder Command */;
	u16_t remote_port /* used in PBF Header Builder Command */;
#endif
#if defined(__BIG_ENDIAN)
	u8_t original_nagle_1b;
	u8_t ts_enabled /* Only 1 bit is used */;
	u16_t tcp_params;
		#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE                                 (0xFF<<0) /* BitField tcp_params Tcp parameters	for ease of pbf command construction */
		#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE_SHIFT                           0
		#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT                                         (0x1<<8) /* BitField tcp_params Tcp parameters	 */
		#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT_SHIFT                                   8
		#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED                                     (0x1<<9) /* BitField tcp_params Tcp parameters	 */
		#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED_SHIFT                               9
		#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED                                      (0x1<<10) /* BitField tcp_params Tcp parameters	Selective Ack Enabled */
		#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED_SHIFT                                10
		#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV                                     (0x1<<11) /* BitField tcp_params Tcp parameters	window smaller than initial window was advertised to far end */
		#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV_SHIFT                               11
		#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG                                     (0x1<<12) /* BitField tcp_params Tcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT                               12
		#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED                                  (0x1<<13) /* BitField tcp_params Tcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED_SHIFT                            13
		#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER                     (0x3<<14) /* BitField tcp_params Tcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER_SHIFT               14
#elif defined(__LITTLE_ENDIAN)
	u16_t tcp_params;
		#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE                                 (0xFF<<0) /* BitField tcp_params Tcp parameters	for ease of pbf command construction */
		#define XSTORM_TCP_CONTEXT_SECTION_TOTAL_HEADER_SIZE_SHIFT                           0
		#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT                                         (0x1<<8) /* BitField tcp_params Tcp parameters	 */
		#define __XSTORM_TCP_CONTEXT_SECTION_ECT_BIT_SHIFT                                   8
		#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED                                     (0x1<<9) /* BitField tcp_params Tcp parameters	 */
		#define __XSTORM_TCP_CONTEXT_SECTION_ECN_ENABLED_SHIFT                               9
		#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED                                      (0x1<<10) /* BitField tcp_params Tcp parameters	Selective Ack Enabled */
		#define XSTORM_TCP_CONTEXT_SECTION_SACK_ENABLED_SHIFT                                10
		#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV                                     (0x1<<11) /* BitField tcp_params Tcp parameters	window smaller than initial window was advertised to far end */
		#define XSTORM_TCP_CONTEXT_SECTION_SMALL_WIN_ADV_SHIFT                               11
		#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG                                     (0x1<<12) /* BitField tcp_params Tcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_FIN_SENT_FLAG_SHIFT                               12
		#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED                                  (0x1<<13) /* BitField tcp_params Tcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_WINDOW_SATURATED_SHIFT                            13
		#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER                     (0x3<<14) /* BitField tcp_params Tcp parameters	 */
		#define XSTORM_TCP_CONTEXT_SECTION_SLOWPATH_QUEUES_FLUSH_COUNTER_SHIFT               14
	u8_t ts_enabled /* Only 1 bit is used */;
	u8_t original_nagle_1b;
#endif
#if defined(__BIG_ENDIAN)
	u16_t pseudo_csum /* the precaluclated pseudo checksum header for pbf command construction */;
	u16_t window_scaling_factor /*  local_adv_wnd by this variable to reach the advertised window to far end */;
#elif defined(__LITTLE_ENDIAN)
	u16_t window_scaling_factor /*  local_adv_wnd by this variable to reach the advertised window to far end */;
	u16_t pseudo_csum /* the precaluclated pseudo checksum header for pbf command construction */;
#endif
	u32_t reserved2;
	u32_t ts_time_diff /* Time Stamp Offload, used in PBF Header Builder Command */;
	u32_t __next_timer_expir /* Last Packet Real Time Clock Stamp */;
};

/*
 * Common context section, shared in TOE, RDMA and ISCSI
 */
struct xstorm_common_context_section
{
	struct xstorm_eth_context_section ethernet;
	union xstorm_ip_context_section_types ip_union;
	struct xstorm_tcp_context_section tcp;
#if defined(__BIG_ENDIAN)
	u8_t conf_version /* holds the latest configuration version, corresponding to dcb version and vntag version */;
	u8_t dcb_val;
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_PRIORITY                                   (0x7<<0) /* BitField dcb_val 	the priority from dcb */
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_PRIORITY_SHIFT                             0
		#define XSTORM_COMMON_CONTEXT_SECTION_PBF_PORT                                       (0x7<<3) /* BitField dcb_val 	the port on which to store the pbf command */
		#define XSTORM_COMMON_CONTEXT_SECTION_PBF_PORT_SHIFT                                 3
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_EXISTS                                     (0x1<<6) /* BitField dcb_val 	flag that states wheter dcb is enabled */
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_EXISTS_SHIFT                               6
		#define XSTORM_COMMON_CONTEXT_SECTION_DONT_ADD_PRI_0                                 (0x1<<7) /* BitField dcb_val 	flag that states whter to add vlan in case both vlan id and priority are 0 */
		#define XSTORM_COMMON_CONTEXT_SECTION_DONT_ADD_PRI_0_SHIFT                           7
	u8_t flags;
		#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L2_STATSTICS                            (0x1<<0) /* BitField flags Tcp flags	set by the driver, determines wheather or not to update l2 statistics */
		#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L2_STATSTICS_SHIFT                      0
		#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L4_STATSTICS                            (0x1<<1) /* BitField flags Tcp flags	set by the driver, determines wheather or not to update l4 statistics */
		#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L4_STATSTICS_SHIFT                      1
		#define XSTORM_COMMON_CONTEXT_SECTION_STATISTICS_COUNTER_ID                          (0x1F<<2) /* BitField flags Tcp flags	The ID of the statistics client for counting common/L2 statistics */
		#define XSTORM_COMMON_CONTEXT_SECTION_STATISTICS_COUNTER_ID_SHIFT                    2
		#define XSTORM_COMMON_CONTEXT_SECTION_PHYSQ_INITIALIZED                              (0x1<<7) /* BitField flags Tcp flags	part of the tx switching state machine */
		#define XSTORM_COMMON_CONTEXT_SECTION_PHYSQ_INITIALIZED_SHIFT                        7
	u8_t ip_version_1b /* use enum ip_ver */;
#elif defined(__LITTLE_ENDIAN)
	u8_t ip_version_1b /* use enum ip_ver */;
	u8_t flags;
		#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L2_STATSTICS                            (0x1<<0) /* BitField flags Tcp flags	set by the driver, determines wheather or not to update l2 statistics */
		#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L2_STATSTICS_SHIFT                      0
		#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L4_STATSTICS                            (0x1<<1) /* BitField flags Tcp flags	set by the driver, determines wheather or not to update l4 statistics */
		#define XSTORM_COMMON_CONTEXT_SECTION_UPDATE_L4_STATSTICS_SHIFT                      1
		#define XSTORM_COMMON_CONTEXT_SECTION_STATISTICS_COUNTER_ID                          (0x1F<<2) /* BitField flags Tcp flags	The ID of the statistics client for counting common/L2 statistics */
		#define XSTORM_COMMON_CONTEXT_SECTION_STATISTICS_COUNTER_ID_SHIFT                    2
		#define XSTORM_COMMON_CONTEXT_SECTION_PHYSQ_INITIALIZED                              (0x1<<7) /* BitField flags Tcp flags	part of the tx switching state machine */
		#define XSTORM_COMMON_CONTEXT_SECTION_PHYSQ_INITIALIZED_SHIFT                        7
	u8_t dcb_val;
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_PRIORITY                                   (0x7<<0) /* BitField dcb_val 	the priority from dcb */
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_PRIORITY_SHIFT                             0
		#define XSTORM_COMMON_CONTEXT_SECTION_PBF_PORT                                       (0x7<<3) /* BitField dcb_val 	the port on which to store the pbf command */
		#define XSTORM_COMMON_CONTEXT_SECTION_PBF_PORT_SHIFT                                 3
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_EXISTS                                     (0x1<<6) /* BitField dcb_val 	flag that states wheter dcb is enabled */
		#define XSTORM_COMMON_CONTEXT_SECTION_DCB_EXISTS_SHIFT                               6
		#define XSTORM_COMMON_CONTEXT_SECTION_DONT_ADD_PRI_0                                 (0x1<<7) /* BitField dcb_val 	flag that states whter to add vlan in case both vlan id and priority are 0 */
		#define XSTORM_COMMON_CONTEXT_SECTION_DONT_ADD_PRI_0_SHIFT                           7
	u8_t conf_version /* holds the latest configuration version, corresponding to dcb version and vntag version */;
#endif
};

/*
 * Flags used in ISCSI context section
 */
struct xstorm_iscsi_context_flags
{
	u8_t flags;
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_IMMEDIATE_DATA                                  (0x1<<0) /* BitField flags 	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_IMMEDIATE_DATA_SHIFT                            0
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_INITIAL_R2T                                     (0x1<<1) /* BitField flags 	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_INITIAL_R2T_SHIFT                               1
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_HEADER_DIGEST                                (0x1<<2) /* BitField flags 	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_HEADER_DIGEST_SHIFT                          2
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_DATA_DIGEST                                  (0x1<<3) /* BitField flags 	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_EN_DATA_DIGEST_SHIFT                            3
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_HQ_BD_WRITTEN                                   (0x1<<4) /* BitField flags 	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_HQ_BD_WRITTEN_SHIFT                             4
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_LAST_OP_SQ                                      (0x1<<5) /* BitField flags 	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_LAST_OP_SQ_SHIFT                                5
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_UPDATE_SND_NXT                                  (0x1<<6) /* BitField flags 	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_B_UPDATE_SND_NXT_SHIFT                            6
		#define XSTORM_ISCSI_CONTEXT_FLAGS_RESERVED4                                         (0x1<<7) /* BitField flags 	 */
		#define XSTORM_ISCSI_CONTEXT_FLAGS_RESERVED4_SHIFT                                   7
};

struct iscsi_task_context_entry_x
{
	u32_t data_out_buffer_offset;
	u32_t itt;
	u32_t data_sn;
};

struct iscsi_task_context_entry_xuc_x_write_only
{
	u32_t tx_r2t_sn /* Xstorm increments for every data-out seq sent. */;
};

struct iscsi_task_context_entry_xuc_xu_write_both
{
	u32_t sgl_base_lo;
	u32_t sgl_base_hi;
#if defined(__BIG_ENDIAN)
	u8_t sgl_size;
	u8_t sge_index;
	u16_t sge_offset;
#elif defined(__LITTLE_ENDIAN)
	u16_t sge_offset;
	u8_t sge_index;
	u8_t sgl_size;
#endif
};

/*
 * iSCSI context section
 */
struct xstorm_iscsi_context_section
{
	u32_t first_burst_length;
	u32_t max_send_pdu_length;
	struct regpair_t sq_pbl_base;
	struct regpair_t sq_curr_pbe;
	struct regpair_t hq_pbl_base;
	struct regpair_t hq_curr_pbe_base;
	struct regpair_t r2tq_pbl_base;
	struct regpair_t r2tq_curr_pbe_base;
	struct regpair_t task_pbl_base;
#if defined(__BIG_ENDIAN)
	u16_t data_out_count;
	struct xstorm_iscsi_context_flags flags;
	u8_t task_pbl_cache_idx /* All-ones value stands for PBL not cached */;
#elif defined(__LITTLE_ENDIAN)
	u8_t task_pbl_cache_idx /* All-ones value stands for PBL not cached */;
	struct xstorm_iscsi_context_flags flags;
	u16_t data_out_count;
#endif
	u32_t seq_more_2_send;
	u32_t pdu_more_2_send;
	struct iscsi_task_context_entry_x temp_tce_x;
	struct iscsi_task_context_entry_xuc_x_write_only temp_tce_x_wr;
	struct iscsi_task_context_entry_xuc_xu_write_both temp_tce_xu_wr;
	struct regpair_t lun;
	u32_t exp_data_transfer_len_ttt /* Overloaded with ttt in multi-pdu sequences flow. */;
	u32_t pdu_data_2_rxmit;
	u32_t rxmit_bytes_2_dr;
#if defined(__BIG_ENDIAN)
	u16_t rxmit_sge_offset;
	u16_t hq_rxmit_cons;
#elif defined(__LITTLE_ENDIAN)
	u16_t hq_rxmit_cons;
	u16_t rxmit_sge_offset;
#endif
#if defined(__BIG_ENDIAN)
	u16_t r2tq_cons;
	u8_t rxmit_flags;
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD                                     (0x1<<0) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD_SHIFT                               0
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR                                 (0x1<<1) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR_SHIFT                           1
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU                                 (0x1<<2) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU_SHIFT                           2
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR                                      (0x1<<3) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR_SHIFT                                3
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR                                (0x1<<4) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR_SHIFT                          4
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING                                 (0x3<<5) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING_SHIFT                           5
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT                         (0x1<<7) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT_SHIFT                   7
	u8_t rxmit_sge_idx;
#elif defined(__LITTLE_ENDIAN)
	u8_t rxmit_sge_idx;
	u8_t rxmit_flags;
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD                                     (0x1<<0) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_NEW_HQ_BD_SHIFT                               0
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR                                 (0x1<<1) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PDU_HDR_SHIFT                           1
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU                                 (0x1<<2) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_END_PDU_SHIFT                           2
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR                                      (0x1<<3) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_DR_SHIFT                                3
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR                                (0x1<<4) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_START_DR_SHIFT                          4
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING                                 (0x3<<5) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_RXMIT_PADDING_SHIFT                           5
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT                         (0x1<<7) /* BitField rxmit_flags 	 */
		#define XSTORM_ISCSI_CONTEXT_SECTION_B_ISCSI_CONT_FAST_RXMIT_SHIFT                   7
	u16_t r2tq_cons;
#endif
	u32_t hq_rxmit_tcp_seq;
};

/*
 * Xstorm iSCSI Storm Context
 */
struct xstorm_iscsi_st_context
{
	struct xstorm_common_context_section common;
	struct xstorm_iscsi_context_section iscsi;
};

/*
 * Iscsi connection context 
 */
struct iscsi_context
{
	struct ustorm_iscsi_st_context ustorm_st_context /* Ustorm storm context */;
	struct tstorm_iscsi_st_context tstorm_st_context /* Tstorm storm context */;
	struct xstorm_iscsi_ag_context xstorm_ag_context /* Xstorm aggregative context */;
	struct tstorm_iscsi_ag_context tstorm_ag_context /* Tstorm aggregative context */;
	struct cstorm_iscsi_ag_context cstorm_ag_context /* Cstorm aggregative context */;
	struct ustorm_iscsi_ag_context ustorm_ag_context /* Ustorm aggregative context */;
	struct timers_block_context timers_context /* Timers block context */;
	struct regpair_t upb_context /* UPb context */;
	struct xstorm_iscsi_st_context xstorm_st_context /* Xstorm storm context */;
	struct regpair_t xpb_context /* XPb context (inside the PBF) */;
	struct cstorm_iscsi_st_context cstorm_st_context /* Cstorm storm context */;
};


/*
 * PDU header of an iSCSI DATA-OUT
 */
struct iscsi_data_pdu_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	u8_t opcode;
	u8_t op_attr;
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_RSRV1                                       (0x7F<<0) /* BitField op_attr 	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                 0
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG                                  (0x1<<7) /* BitField op_attr 	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG_SHIFT                            7
	u16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	u16_t rsrv0;
	u8_t op_attr;
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_RSRV1                                       (0x7F<<0) /* BitField op_attr 	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                 0
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG                                  (0x1<<7) /* BitField op_attr 	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_FINAL_FLAG_SHIFT                            7
	u8_t opcode;
#endif
	u32_t data_fields;
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                         (0xFFFFFF<<0) /* BitField data_fields 	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                   0
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                            (0xFF<<24) /* BitField data_fields 	 */
		#define ISCSI_DATA_PDU_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                      24
	struct regpair_t lun;
	u32_t itt;
	u32_t ttt;
	u32_t rsrv2;
	u32_t exp_stat_sn;
	u32_t rsrv3;
	u32_t data_sn;
	u32_t buffer_offset;
	u32_t rsrv4;
};


/*
 * PDU header of an iSCSI login request
 */
struct iscsi_login_req_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	u8_t opcode;
	u8_t op_attr;
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_NSG                                        (0x3<<0) /* BitField op_attr 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_NSG_SHIFT                                  0
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CSG                                        (0x3<<2) /* BitField op_attr 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CSG_SHIFT                                  2
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_RSRV0                                      (0x3<<4) /* BitField op_attr 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_RSRV0_SHIFT                                4
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG                               (0x1<<6) /* BitField op_attr 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG_SHIFT                         6
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TRANSIT                                    (0x1<<7) /* BitField op_attr 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TRANSIT_SHIFT                              7
	u8_t version_max;
	u8_t version_min;
#elif defined(__LITTLE_ENDIAN)
	u8_t version_min;
	u8_t version_max;
	u8_t op_attr;
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_NSG                                        (0x3<<0) /* BitField op_attr 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_NSG_SHIFT                                  0
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CSG                                        (0x3<<2) /* BitField op_attr 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CSG_SHIFT                                  2
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_RSRV0                                      (0x3<<4) /* BitField op_attr 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_RSRV0_SHIFT                                4
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG                               (0x1<<6) /* BitField op_attr 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG_SHIFT                         6
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TRANSIT                                    (0x1<<7) /* BitField op_attr 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TRANSIT_SHIFT                              7
	u8_t opcode;
#endif
	u32_t data_fields;
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                        (0xFFFFFF<<0) /* BitField data_fields 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                  0
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                           (0xFF<<24) /* BitField data_fields 	 */
		#define ISCSI_LOGIN_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                     24
	u32_t isid_lo;
#if defined(__BIG_ENDIAN)
	u16_t isid_hi;
	u16_t tsih;
#elif defined(__LITTLE_ENDIAN)
	u16_t tsih;
	u16_t isid_hi;
#endif
	u32_t itt;
#if defined(__BIG_ENDIAN)
	u16_t cid;
	u16_t rsrv1;
#elif defined(__LITTLE_ENDIAN)
	u16_t rsrv1;
	u16_t cid;
#endif
	u32_t cmd_sn;
	u32_t exp_stat_sn;
	u32_t rsrv2[4];
};

/*
 * PDU header of an iSCSI logout request
 */
struct iscsi_logout_req_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	u8_t opcode;
	u8_t op_attr;
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_REASON_CODE                               (0x7F<<0) /* BitField op_attr 	 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_REASON_CODE_SHIFT                         0
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_RSRV1_1                                   (0x1<<7) /* BitField op_attr 	this value must be 1 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_RSRV1_1_SHIFT                             7
	u16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	u16_t rsrv0;
	u8_t op_attr;
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_REASON_CODE                               (0x7F<<0) /* BitField op_attr 	 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_REASON_CODE_SHIFT                         0
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_RSRV1_1                                   (0x1<<7) /* BitField op_attr 	this value must be 1 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_RSRV1_1_SHIFT                             7
	u8_t opcode;
#endif
	u32_t data_fields;
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                       (0xFFFFFF<<0) /* BitField data_fields 	 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                 0
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                          (0xFF<<24) /* BitField data_fields 	 */
		#define ISCSI_LOGOUT_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                    24
	u32_t rsrv2[2];
	u32_t itt;
#if defined(__BIG_ENDIAN)
	u16_t cid;
	u16_t rsrv1;
#elif defined(__LITTLE_ENDIAN)
	u16_t rsrv1;
	u16_t cid;
#endif
	u32_t cmd_sn;
	u32_t exp_stat_sn;
	u32_t rsrv3[4];
};

/*
 * PDU header of an iSCSI TMF request
 */
struct iscsi_tmf_req_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	u8_t opcode;
	u8_t op_attr;
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_FUNCTION                                     (0x7F<<0) /* BitField op_attr 	 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_FUNCTION_SHIFT                               0
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_RSRV1_1                                      (0x1<<7) /* BitField op_attr 	this value must be 1 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_RSRV1_1_SHIFT                                7
	u16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	u16_t rsrv0;
	u8_t op_attr;
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_FUNCTION                                     (0x7F<<0) /* BitField op_attr 	 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_FUNCTION_SHIFT                               0
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_RSRV1_1                                      (0x1<<7) /* BitField op_attr 	this value must be 1 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_RSRV1_1_SHIFT                                7
	u8_t opcode;
#endif
	u32_t data_fields;
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                          (0xFFFFFF<<0) /* BitField data_fields 	 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                    0
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                             (0xFF<<24) /* BitField data_fields 	 */
		#define ISCSI_TMF_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                       24
	struct regpair_t lun;
	u32_t itt;
	u32_t referenced_task_tag;
	u32_t cmd_sn;
	u32_t exp_stat_sn;
	u32_t ref_cmd_sn;
	u32_t exp_data_sn;
	u32_t rsrv2[2];
};

/*
 * PDU header of an iSCSI Text request
 */
struct iscsi_text_req_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	u8_t opcode;
	u8_t op_attr;
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_RSRV1                                       (0x3F<<0) /* BitField op_attr 	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                 0
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG                                (0x1<<6) /* BitField op_attr 	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG_SHIFT                          6
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_FINAL                                       (0x1<<7) /* BitField op_attr 	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_FINAL_SHIFT                                 7
	u16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	u16_t rsrv0;
	u8_t op_attr;
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_RSRV1                                       (0x3F<<0) /* BitField op_attr 	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                 0
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG                                (0x1<<6) /* BitField op_attr 	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_CONTINUE_FLG_SHIFT                          6
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_FINAL                                       (0x1<<7) /* BitField op_attr 	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_FINAL_SHIFT                                 7
	u8_t opcode;
#endif
	u32_t data_fields;
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                         (0xFFFFFF<<0) /* BitField data_fields 	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                   0
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                            (0xFF<<24) /* BitField data_fields 	 */
		#define ISCSI_TEXT_REQ_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                      24
	struct regpair_t lun;
	u32_t itt;
	u32_t ttt;
	u32_t cmd_sn;
	u32_t exp_stat_sn;
	u32_t rsrv3[4];
};

/*
 * PDU header of an iSCSI Nop-Out
 */
struct iscsi_nop_out_hdr_little_endian
{
#if defined(__BIG_ENDIAN)
	u8_t opcode;
	u8_t op_attr;
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV1                                        (0x7F<<0) /* BitField op_attr 	 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                  0
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV2_1                                      (0x1<<7) /* BitField op_attr 	this reserved bit must be set to 1 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV2_1_SHIFT                                7
	u16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	u16_t rsrv0;
	u8_t op_attr;
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV1                                        (0x7F<<0) /* BitField op_attr 	 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV1_SHIFT                                  0
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV2_1                                      (0x1<<7) /* BitField op_attr 	this reserved bit must be set to 1 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_RSRV2_1_SHIFT                                7
	u8_t opcode;
#endif
	u32_t data_fields;
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH                          (0xFFFFFF<<0) /* BitField data_fields 	 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_DATA_SEGMENT_LENGTH_SHIFT                    0
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH                             (0xFF<<24) /* BitField data_fields 	 */
		#define ISCSI_NOP_OUT_HDR_LITTLE_ENDIAN_TOTAL_AHS_LENGTH_SHIFT                       24
	struct regpair_t lun;
	u32_t itt;
	u32_t ttt;
	u32_t cmd_sn;
	u32_t exp_stat_sn;
	u32_t rsrv3[4];
};

/*
 * iscsi pdu headers in little endian form.
 */
union iscsi_pdu_headers_little_endian
{
	u32_t fullHeaderSize[12] /* The full size of the header. protects the union size */;
	struct iscsi_cmd_pdu_hdr_little_endian command_pdu_hdr /* PDU header of an iSCSI command - read,write  */;
	struct iscsi_data_pdu_hdr_little_endian data_out_pdu_hdr /* PDU header of an iSCSI DATA-IN and DATA-OUT PDU  */;
	struct iscsi_login_req_hdr_little_endian login_req_pdu_hdr /* PDU header of an iSCSI Login request */;
	struct iscsi_logout_req_hdr_little_endian logout_req_pdu_hdr /* PDU header of an iSCSI Logout request */;
	struct iscsi_tmf_req_hdr_little_endian tmf_req_pdu_hdr /* PDU header of an iSCSI TMF request */;
	struct iscsi_text_req_hdr_little_endian text_req_pdu_hdr /* PDU header of an iSCSI Text request */;
	struct iscsi_nop_out_hdr_little_endian nop_out_pdu_hdr /* PDU header of an iSCSI Nop-Out */;
};

struct iscsi_hq_bd
{
	union iscsi_pdu_headers_little_endian pdu_header;
#if defined(__BIG_ENDIAN)
	u16_t reserved1;
	u16_t lcl_cmp_flg;
#elif defined(__LITTLE_ENDIAN)
	u16_t lcl_cmp_flg;
	u16_t reserved1;
#endif
	u32_t sgl_base_lo;
	u32_t sgl_base_hi;
#if defined(__BIG_ENDIAN)
	u8_t sgl_size;
	u8_t sge_index;
	u16_t sge_offset;
#elif defined(__LITTLE_ENDIAN)
	u16_t sge_offset;
	u8_t sge_index;
	u8_t sgl_size;
#endif
};


/*
 * CQE data for L2 OOO connection $$KEEP_ENDIANNESS$$
 */
struct iscsi_l2_ooo_data
{
	u32_t iscsi_cid /* iSCSI context ID  */;
	u8_t drop_isle /* isle number of the first isle to drop */;
	u8_t drop_size /* number of isles to drop */;
	u8_t ooo_opcode /* Out Of Order opcode - use enum tcp_ooo_event */;
	u8_t ooo_isle /* OOO isle number to add the packet to */;
	u8_t reserved[8];
};






struct iscsi_task_context_entry_xuc_c_write_only
{
	u32_t total_data_acked /* Xstorm inits to zero. C increments. U validates  */;
};

struct iscsi_task_context_r2t_table_entry
{
	u32_t ttt;
	u32_t desired_data_len;
};

struct iscsi_task_context_entry_xuc_u_write_only
{
	u32_t exp_r2t_sn /* Xstorm inits to zero. U increments. */;
	struct iscsi_task_context_r2t_table_entry r2t_table[4] /* U updates. X reads */;
#if defined(__BIG_ENDIAN)
	u16_t data_in_count /* X inits to zero. U increments. */;
	u8_t cq_id /* X inits to zero. U uses. */;
	u8_t valid_1b /* X sets. U resets. */;
#elif defined(__LITTLE_ENDIAN)
	u8_t valid_1b /* X sets. U resets. */;
	u8_t cq_id /* X inits to zero. U uses. */;
	u16_t data_in_count /* X inits to zero. U increments. */;
#endif
};

struct iscsi_task_context_entry_xuc
{
	struct iscsi_task_context_entry_xuc_c_write_only write_c /* Cstorm only inits data here, without further change by any storm. */;
	u32_t exp_data_transfer_len /* Xstorm only inits data here. */;
	struct iscsi_task_context_entry_xuc_x_write_only write_x /* only Xstorm writes data here. */;
	u32_t lun_lo /* Xstorm only inits data here. */;
	struct iscsi_task_context_entry_xuc_xu_write_both write_xu /* Both X and U update this struct, but in different flow. */;
	u32_t lun_hi /* Xstorm only inits data here. */;
	struct iscsi_task_context_entry_xuc_u_write_only write_u /* Ustorm only inits data here, without further change by any storm. */;
};

struct iscsi_task_context_entry_u
{
	u32_t exp_r2t_buff_offset;
	u32_t rem_rcv_len;
	u32_t exp_data_sn;
};

struct iscsi_task_context_entry
{
	struct iscsi_task_context_entry_x tce_x;
#if defined(__BIG_ENDIAN)
	u16_t data_out_count;
	u16_t rsrv0;
#elif defined(__LITTLE_ENDIAN)
	u16_t rsrv0;
	u16_t data_out_count;
#endif
	struct iscsi_task_context_entry_xuc tce_xuc;
	struct iscsi_task_context_entry_u tce_u;
	u32_t rsrv1[7] /* increase the size to 128 bytes */;
};








struct iscsi_task_context_entry_xuc_x_init_only
{
	struct regpair_t lun /* X inits. U validates */;
	u32_t exp_data_transfer_len /* Xstorm inits to SQ WQE data. U validates */;
};


















/*
 * cfc delete event data  $$KEEP_ENDIANNESS$$
 */
struct cfc_del_event_data
{
	u32_t cid /* cid of deleted connection */;
	u32_t reserved0;
	u32_t reserved1;
};


/*
 * per-port SAFC demo variables
 */
struct cmng_flags_per_port
{
	u32_t cmng_enables;
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_VN                                              (0x1<<0) /* BitField cmng_enables enables flag for fairness and rate shaping between protocols, vnics and COSes	if set, enable fairness between vnics */
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_VN_SHIFT                                        0
		#define CMNG_FLAGS_PER_PORT_RATE_SHAPING_VN                                          (0x1<<1) /* BitField cmng_enables enables flag for fairness and rate shaping between protocols, vnics and COSes	if set, enable rate shaping between vnics */
		#define CMNG_FLAGS_PER_PORT_RATE_SHAPING_VN_SHIFT                                    1
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_COS                                             (0x1<<2) /* BitField cmng_enables enables flag for fairness and rate shaping between protocols, vnics and COSes	if set, enable fairness between COSes */
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_COS_SHIFT                                       2
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_COS_MODE                                        (0x1<<3) /* BitField cmng_enables enables flag for fairness and rate shaping between protocols, vnics and COSes	use enum fairness_mode */
		#define CMNG_FLAGS_PER_PORT_FAIRNESS_COS_MODE_SHIFT                                  3
		#define __CMNG_FLAGS_PER_PORT_RESERVED0                                              (0xFFFFFFF<<4) /* BitField cmng_enables enables flag for fairness and rate shaping between protocols, vnics and COSes	reserved */
		#define __CMNG_FLAGS_PER_PORT_RESERVED0_SHIFT                                        4
	u32_t __reserved1;
};


/*
 * per-port rate shaping variables
 */
struct rate_shaping_vars_per_port
{
	u32_t rs_periodic_timeout /* timeout of periodic timer */;
	u32_t rs_threshold /* threshold, below which we start to stop queues */;
};

/*
 * per-port fairness variables
 */
struct fairness_vars_per_port
{
	u32_t upper_bound /* Quota for a protocol/vnic */;
	u32_t fair_threshold /* almost-empty threshold */;
	u32_t fairness_timeout /* timeout of fairness timer */;
	u32_t reserved0;
};

/*
 * per-port SAFC variables
 */
struct safc_struct_per_port
{
#if defined(__BIG_ENDIAN)
	u16_t __reserved1;
	u8_t __reserved0;
	u8_t safc_timeout_usec /* timeout to stop queues on SAFC pause command */;
#elif defined(__LITTLE_ENDIAN)
	u8_t safc_timeout_usec /* timeout to stop queues on SAFC pause command */;
	u8_t __reserved0;
	u16_t __reserved1;
#endif
	u8_t cos_to_traffic_types[MAX_COS_NUMBER] /* translate cos to service traffics types */;
	u16_t cos_to_pause_mask[NUM_OF_SAFC_BITS] /* QM pause mask for each class of service in the SAFC frame */;
};

/*
 * Per-port congestion management variables
 */
struct cmng_struct_per_port
{
	struct rate_shaping_vars_per_port rs_vars;
	struct fairness_vars_per_port fair_vars;
	struct safc_struct_per_port safc_vars;
	struct cmng_flags_per_port flags;
};



/*
 * Protocol-common command ID for slow path elements
 */
enum common_spqe_cmd_id
{
	RAMROD_CMD_ID_COMMON_UNUSED=0,
	RAMROD_CMD_ID_COMMON_FUNCTION_START=1 /* Start a function (for PFs only) */,
	RAMROD_CMD_ID_COMMON_FUNCTION_STOP=2 /* Stop a function (for PFs only) */,
	RAMROD_CMD_ID_COMMON_CFC_DEL=3 /* Delete a connection from CFC */,
	RAMROD_CMD_ID_COMMON_CFC_DEL_WB=4 /* Delete a connection from CFC (with write back) */,
	RAMROD_CMD_ID_COMMON_STAT_QUERY=5 /* Collect statistics counters */,
	RAMROD_CMD_ID_COMMON_STOP_TRAFFIC=6 /* Stop Tx traffic (before DCB updates) */,
	RAMROD_CMD_ID_COMMON_START_TRAFFIC=7 /* Start Tx traffic (after DCB updates) */,
	RAMROD_CMD_ID_COMMON_NIV_FUNCTION_UPDATE=8 /* niv update function */,
	RAMROD_CMD_ID_COMMON_NIV_VIF_LISTS=9 /* niv vif lists */,
	MAX_COMMON_SPQE_CMD_ID
};


/*
 * Per-protocol connection types
 */
enum connection_type
{
	ETH_CONNECTION_TYPE=0 /* Ethernet */,
	TOE_CONNECTION_TYPE=1 /* TOE */,
	RDMA_CONNECTION_TYPE=2 /* RDMA */,
	ISCSI_CONNECTION_TYPE=3 /* iSCSI */,
	FCOE_CONNECTION_TYPE=4 /* FCoE */,
	RESERVED_CONNECTION_TYPE_0=5,
	RESERVED_CONNECTION_TYPE_1=6,
	RESERVED_CONNECTION_TYPE_2=7,
	NONE_CONNECTION_TYPE=8 /* General- used for common slow path */,
	MAX_CONNECTION_TYPE
};


/*
 * Dynamic HC counters set by the driver
 */
struct hc_dynamic_drv_counter
{
	u32_t val[HC_SB_MAX_DYNAMIC_INDICES] /* 4 bytes * 4 indices = 2 lines */;
};

/*
 * zone A per-queue data
 */
struct cstorm_queue_zone_data
{
	struct hc_dynamic_drv_counter hc_dyn_drv_cnt /* 4 bytes * 4 indices = 2 lines */;
	struct regpair_t reserved[2];
};


/*
 * Vf-PF channel data in cstorm ram (non-triggered zone)
 */
struct vf_pf_channel_zone_data
{
	u32_t msg_addr_lo /* the message address on VF memory */;
	u32_t msg_addr_hi /* the message address on VF memory */;
};

/*
 * zone for VF non-triggered data
 */
struct non_trigger_vf_zone
{
	struct vf_pf_channel_zone_data vf_pf_channel /* vf-pf channel zone data */;
};

/*
 * Vf-PF channel trigger zone in cstorm ram
 */
struct vf_pf_channel_zone_trigger
{
	u8_t addr_valid /* indicates that a vf-pf message is pending. MUST be set AFTER the message address.  */;
};

/*
 * zone that triggers the in-bound interrupt
 */
struct trigger_vf_zone
{
#if defined(__BIG_ENDIAN)
	u16_t reserved1;
	u8_t reserved0;
	struct vf_pf_channel_zone_trigger vf_pf_channel;
#elif defined(__LITTLE_ENDIAN)
	struct vf_pf_channel_zone_trigger vf_pf_channel;
	u8_t reserved0;
	u16_t reserved1;
#endif
	u32_t reserved2;
};

/*
 * zone B per-VF data
 */
struct cstorm_vf_zone_data
{
	struct non_trigger_vf_zone non_trigger /* zone for VF non-triggered data */;
	struct trigger_vf_zone trigger /* zone that triggers the in-bound interrupt */;
};


/*
 * Dynamic host coalescing init parameters, per state machine
 */
struct dynamic_hc_sm_config
{
	u32_t threshold[3] /* thresholds of number of outstanding bytes */;
	u8_t shift_per_protocol[HC_SB_MAX_DYNAMIC_INDICES] /* bytes difference of each protocol is shifted right by this value */;
	u8_t hc_timeout0[HC_SB_MAX_DYNAMIC_INDICES] /* timeout for level 0 for each protocol, in units of usec */;
	u8_t hc_timeout1[HC_SB_MAX_DYNAMIC_INDICES] /* timeout for level 1 for each protocol, in units of usec */;
	u8_t hc_timeout2[HC_SB_MAX_DYNAMIC_INDICES] /* timeout for level 2 for each protocol, in units of usec */;
	u8_t hc_timeout3[HC_SB_MAX_DYNAMIC_INDICES] /* timeout for level 3 for each protocol, in units of usec */;
};

/*
 * Dynamic host coalescing init parameters
 */
struct dynamic_hc_config
{
	struct dynamic_hc_sm_config sm_config[HC_SB_MAX_SM] /* Configuration per state machine */;
};



struct e2_integ_data
{
#if defined(__BIG_ENDIAN)
	u8_t flags;
		#define E2_INTEG_DATA_TESTING_EN                                                     (0x1<<0) /* BitField flags 	integration testing enabled */
		#define E2_INTEG_DATA_TESTING_EN_SHIFT                                               0
		#define E2_INTEG_DATA_LB_TX                                                          (0x1<<1) /* BitField flags 	flag indicating this connection will transmit on loopback */
		#define E2_INTEG_DATA_LB_TX_SHIFT                                                    1
		#define E2_INTEG_DATA_COS_TX                                                         (0x1<<2) /* BitField flags 	flag indicating this connection will transmit according to cos field */
		#define E2_INTEG_DATA_COS_TX_SHIFT                                                   2
		#define E2_INTEG_DATA_OPPORTUNISTICQM                                                (0x1<<3) /* BitField flags 	flag indicating this connection will activate the opportunistic QM credit flow */
		#define E2_INTEG_DATA_OPPORTUNISTICQM_SHIFT                                          3
		#define E2_INTEG_DATA_DPMTESTRELEASEDQ                                               (0x1<<4) /* BitField flags 	flag indicating this connection will release the door bell queue (DQ) */
		#define E2_INTEG_DATA_DPMTESTRELEASEDQ_SHIFT                                         4
		#define E2_INTEG_DATA_RESERVED                                                       (0x7<<5) /* BitField flags 	 */
		#define E2_INTEG_DATA_RESERVED_SHIFT                                                 5
	u8_t cos /* cos of the connection (relevant only in cos transmitting connections, when cosTx is set */;
	u8_t voq /* voq to return credit on. Normally equal to port (i.e. always 0 in E2 operational connections). in cos tests equal to cos. in loopback tests equal to LB_PORT (=4) */;
	u8_t pbf_queue /* pbf queue to transmit on. Normally equal to port (i.e. always 0 in E2 operational connections). in cos tests equal to cos. in loopback tests equal to LB_PORT (=4) */;
#elif defined(__LITTLE_ENDIAN)
	u8_t pbf_queue /* pbf queue to transmit on. Normally equal to port (i.e. always 0 in E2 operational connections). in cos tests equal to cos. in loopback tests equal to LB_PORT (=4) */;
	u8_t voq /* voq to return credit on. Normally equal to port (i.e. always 0 in E2 operational connections). in cos tests equal to cos. in loopback tests equal to LB_PORT (=4) */;
	u8_t cos /* cos of the connection (relevant only in cos transmitting connections, when cosTx is set */;
	u8_t flags;
		#define E2_INTEG_DATA_TESTING_EN                                                     (0x1<<0) /* BitField flags 	integration testing enabled */
		#define E2_INTEG_DATA_TESTING_EN_SHIFT                                               0
		#define E2_INTEG_DATA_LB_TX                                                          (0x1<<1) /* BitField flags 	flag indicating this connection will transmit on loopback */
		#define E2_INTEG_DATA_LB_TX_SHIFT                                                    1
		#define E2_INTEG_DATA_COS_TX                                                         (0x1<<2) /* BitField flags 	flag indicating this connection will transmit according to cos field */
		#define E2_INTEG_DATA_COS_TX_SHIFT                                                   2
		#define E2_INTEG_DATA_OPPORTUNISTICQM                                                (0x1<<3) /* BitField flags 	flag indicating this connection will activate the opportunistic QM credit flow */
		#define E2_INTEG_DATA_OPPORTUNISTICQM_SHIFT                                          3
		#define E2_INTEG_DATA_DPMTESTRELEASEDQ                                               (0x1<<4) /* BitField flags 	flag indicating this connection will release the door bell queue (DQ) */
		#define E2_INTEG_DATA_DPMTESTRELEASEDQ_SHIFT                                         4
		#define E2_INTEG_DATA_RESERVED                                                       (0x7<<5) /* BitField flags 	 */
		#define E2_INTEG_DATA_RESERVED_SHIFT                                                 5
#endif
#if defined(__BIG_ENDIAN)
	u16_t reserved3;
	u8_t reserved2;
	u8_t ramEn /* context area reserved for reading enable bit from ram */;
#elif defined(__LITTLE_ENDIAN)
	u8_t ramEn /* context area reserved for reading enable bit from ram */;
	u8_t reserved2;
	u16_t reserved3;
#endif
};


/*
 * set mac event data  $$KEEP_ENDIANNESS$$
 */
struct eth_event_data
{
	u32_t echo /* set mac echo data to return to driver */;
	u32_t reserved0;
	u32_t reserved1;
};


/*
 * pf-vf event data  $$KEEP_ENDIANNESS$$
 */
struct vf_pf_event_data
{
	u8_t vf_id /* VF ID (0-63) */;
	u8_t reserved0;
	u16_t reserved1;
	u32_t msg_addr_lo /* message address on Vf (low 32 bits) */;
	u32_t msg_addr_hi /* message address on Vf (high 32 bits) */;
};

/*
 * VF FLR event data  $$KEEP_ENDIANNESS$$
 */
struct vf_flr_event_data
{
	u8_t vf_id /* VF ID (0-63) */;
	u8_t reserved0;
	u16_t reserved1;
	u32_t reserved2;
	u32_t reserved3;
};

/*
 * malicious VF event data  $$KEEP_ENDIANNESS$$
 */
struct malicious_vf_event_data
{
	u8_t vf_id /* VF ID (0-63) */;
	u8_t reserved0;
	u16_t reserved1;
	u32_t reserved2;
	u32_t reserved3;
};

/*
 * vif list event data  $$KEEP_ENDIANNESS$$
 */
struct vif_list_event_data
{
	u8_t func_bit_map /* bit map of pf indice */;
	u8_t echo;
	u16_t reserved0;
	u32_t reserved1;
	u32_t reserved2;
};

/*
 * union for all event ring message types
 */
union event_data
{
	struct vf_pf_event_data vf_pf_event /* pf-vf event data */;
	struct eth_event_data eth_event /* set mac event data */;
	struct cfc_del_event_data cfc_del_event /* cfc delete event data */;
	struct vf_flr_event_data vf_flr_event /* vf flr event data */;
	struct malicious_vf_event_data malicious_vf_event /* malicious vf event data */;
	struct vif_list_event_data vif_list_event /* vif list event data */;
};


/*
 * per PF event ring data
 */
struct event_ring_data
{
	struct regpair_t base_addr /* ring base address */;
#if defined(__BIG_ENDIAN)
	u8_t index_id /* index ID within the status block */;
	u8_t sb_id /* status block ID */;
	u16_t producer /* event ring producer */;
#elif defined(__LITTLE_ENDIAN)
	u16_t producer /* event ring producer */;
	u8_t sb_id /* status block ID */;
	u8_t index_id /* index ID within the status block */;
#endif
	u32_t reserved0;
};


/*
 * event ring message element (each element is 128 bits) $$KEEP_ENDIANNESS$$
 */
struct event_ring_msg
{
	u8_t opcode /* use enum event_ring_opcode */;
	u8_t error /* error on the mesasage */;
	u16_t reserved1;
	union event_data data /* message data (96 bits data) */;
};

/*
 * event ring next page element (128 bits)
 */
struct event_ring_next
{
	struct regpair_t addr /* Address of the next page of the ring */;
	u32_t reserved[2];
};

/*
 * union for event ring element types (each element is 128 bits)
 */
union event_ring_elem
{
	struct event_ring_msg message /* event ring message */;
	struct event_ring_next next_page /* event ring next page */;
};




/*
 * Common event ring opcodes
 */
enum event_ring_opcode
{
	EVENT_RING_OPCODE_VF_PF_CHANNEL=0,
	EVENT_RING_OPCODE_FUNCTION_START=1 /* Start a function (for PFs only) */,
	EVENT_RING_OPCODE_FUNCTION_STOP=2 /* Stop a function (for PFs only) */,
	EVENT_RING_OPCODE_CFC_DEL=3 /* Delete a connection from CFC */,
	EVENT_RING_OPCODE_CFC_DEL_WB=4 /* Delete a connection from CFC (with write back) */,
	EVENT_RING_OPCODE_STAT_QUERY=5 /* Collect statistics counters */,
	EVENT_RING_OPCODE_STOP_TRAFFIC=6 /* Stop Tx traffic (before DCB updates) */,
	EVENT_RING_OPCODE_START_TRAFFIC=7 /* Start Tx traffic (after DCB updates) */,
	EVENT_RING_OPCODE_VF_FLR=8 /* VF FLR indication for PF */,
	EVENT_RING_OPCODE_MALICIOUS_VF=9 /* Malicious VF operation detected */,
	EVENT_RING_OPCODE_FORWARD_SETUP=10 /* Initialize forward channel */,
	EVENT_RING_OPCODE_RSS_UPDATE_RULES=11 /* Update RSS configuration */,
	EVENT_RING_OPCODE_NIV_FUNCTION_UPDATE=12 /* niv update function */,
	EVENT_RING_OPCODE_NIV_VIF_LISTS=13 /* event ring opcode niv vif lists */,
	EVENT_RING_OPCODE_SET_MAC=14 /* Add/remove MAC (in E1x only) */,
	EVENT_RING_OPCODE_CLASSIFICATION_RULES=15 /* Add/remove MAC or VLAN (in E2/E3 only) */,
	EVENT_RING_OPCODE_FILTERS_RULES=16 /* Add/remove classification filters for L2 client (in E2/E3 only) */,
	EVENT_RING_OPCODE_MULTICAST_RULES=17 /* Add/remove multicast classification bin (in E2/E3 only) */,
	MAX_EVENT_RING_OPCODE
};


/*
 * Modes for fairness algorithm
 */
enum fairness_mode
{
	FAIRNESS_COS_WRR_MODE=0 /* Weighted round robin mode (used in Google) */,
	FAIRNESS_COS_ETS_MODE=1 /* ETS mode (used in FCoE) */,
	MAX_FAIRNESS_MODE
};



/*
 * per-vnic fairness variables
 */
struct fairness_vars_per_vn
{
	u32_t cos_credit_delta[MAX_COS_NUMBER] /* used for incrementing the credit */;
	u32_t vn_credit_delta /* used for incrementing the credit */;
	u32_t __reserved0;
};


/*
 * Priority and cos $$KEEP_ENDIANNESS$$
 */
struct priority_cos
{
	u8_t priority /* Priority */;
	u8_t cos /* Cos */;
	u16_t reserved1;
};

/*
 * The data for flow control configuration $$KEEP_ENDIANNESS$$
 */
struct flow_control_configuration
{
	struct priority_cos traffic_type_to_priority_cos[MAX_TRAFFIC_TYPES] /* traffic_type to priority cos */;
	u8_t dcb_enabled /* If DCB mode is enabled then traffic class to priority array is fully initialized and there must be inner VLAN */;
	u8_t dcb_version /* DCB version Increase by one on each DCB update */;
	u8_t dont_add_pri_0 /* In case, the priority is 0, and the packet has no vlan, the firmware wont add vlan */;
	u8_t reserved1;
	u32_t reserved2;
};


/*
 *  $$KEEP_ENDIANNESS$$
 */
struct function_niv_update_data
{
	u16_t vif_id /* value of VIF id in case of NIV multi-function mode */;
	u16_t niv_default_vlan /* value of default Vlan in case of NIV mf */;
	u8_t allowed_priorities /* bit vector of allowed Vlan priorities for this VIF */;
	u8_t reserved1;
	u16_t reserved2;
};


/*
 *  $$KEEP_ENDIANNESS$$
 */
struct function_start_data
{
	u16_t function_mode /* the function mode (use enum mf_mode) */;
	u16_t sd_vlan_tag /* value of Vlan in case of switch depended multi-function mode */;
	u16_t vif_id /* value of VIF id in case of NIV multi-function mode */;
	u8_t path_id;
	u8_t reserved;
};


/*
 * FW version stored in the Xstorm RAM
 */
struct fw_version
{
#if defined(__BIG_ENDIAN)
	u8_t engineering /* firmware current engineering version */;
	u8_t revision /* firmware current revision version */;
	u8_t minor /* firmware current minor version */;
	u8_t major /* firmware current major version */;
#elif defined(__LITTLE_ENDIAN)
	u8_t major /* firmware current major version */;
	u8_t minor /* firmware current minor version */;
	u8_t revision /* firmware current revision version */;
	u8_t engineering /* firmware current engineering version */;
#endif
	u32_t flags;
		#define FW_VERSION_OPTIMIZED                                                         (0x1<<0) /* BitField flags 	if set, this is optimized ASM */
		#define FW_VERSION_OPTIMIZED_SHIFT                                                   0
		#define FW_VERSION_BIG_ENDIEN                                                        (0x1<<1) /* BitField flags 	if set, this is big-endien ASM */
		#define FW_VERSION_BIG_ENDIEN_SHIFT                                                  1
		#define FW_VERSION_CHIP_VERSION                                                      (0x3<<2) /* BitField flags 	0 - E1, 1 - E1H */
		#define FW_VERSION_CHIP_VERSION_SHIFT                                                2
		#define __FW_VERSION_RESERVED                                                        (0xFFFFFFF<<4) /* BitField flags 	 */
		#define __FW_VERSION_RESERVED_SHIFT                                                  4
};



/*
 * Dynamic Host-Coalescing - Driver(host) counters 
 */
struct hc_dynamic_sb_drv_counters
{
	u32_t dynamic_hc_drv_counter[HC_SB_MAX_DYNAMIC_INDICES] /* Dynamic HC counters written by drivers */;
};


/*
 * 2 bytes. configuration/state parameters for a single protocol index
 */
struct hc_index_data
{
#if defined(__BIG_ENDIAN)
	u8_t flags;
		#define HC_INDEX_DATA_SM_ID                                                          (0x1<<0) /* BitField flags 	Index to a state machine. Can be 0 or 1 */
		#define HC_INDEX_DATA_SM_ID_SHIFT                                                    0
		#define HC_INDEX_DATA_HC_ENABLED                                                     (0x1<<1) /* BitField flags 	if set, host coalescing would be done for this index */
		#define HC_INDEX_DATA_HC_ENABLED_SHIFT                                               1
		#define HC_INDEX_DATA_DYNAMIC_HC_ENABLED                                             (0x1<<2) /* BitField flags 	if set, dynamic HC will be done for this index */
		#define HC_INDEX_DATA_DYNAMIC_HC_ENABLED_SHIFT                                       2
		#define HC_INDEX_DATA_RESERVE                                                        (0x1F<<3) /* BitField flags 	 */
		#define HC_INDEX_DATA_RESERVE_SHIFT                                                  3
	u8_t timeout /* the timeout values for this index. Units are 4 usec */;
#elif defined(__LITTLE_ENDIAN)
	u8_t timeout /* the timeout values for this index. Units are 4 usec */;
	u8_t flags;
		#define HC_INDEX_DATA_SM_ID                                                          (0x1<<0) /* BitField flags 	Index to a state machine. Can be 0 or 1 */
		#define HC_INDEX_DATA_SM_ID_SHIFT                                                    0
		#define HC_INDEX_DATA_HC_ENABLED                                                     (0x1<<1) /* BitField flags 	if set, host coalescing would be done for this index */
		#define HC_INDEX_DATA_HC_ENABLED_SHIFT                                               1
		#define HC_INDEX_DATA_DYNAMIC_HC_ENABLED                                             (0x1<<2) /* BitField flags 	if set, dynamic HC will be done for this index */
		#define HC_INDEX_DATA_DYNAMIC_HC_ENABLED_SHIFT                                       2
		#define HC_INDEX_DATA_RESERVE                                                        (0x1F<<3) /* BitField flags 	 */
		#define HC_INDEX_DATA_RESERVE_SHIFT                                                  3
#endif
};


/*
 * HC state-machine
 */
struct hc_status_block_sm
{
#if defined(__BIG_ENDIAN)
	u8_t igu_seg_id /* use enum hc_segmenet */;
	u8_t igu_sb_id /* sb_id within the IGU */;
	u8_t timer_value /* Determines the time_to_expire */;
	u8_t __flags;
#elif defined(__LITTLE_ENDIAN)
	u8_t __flags;
	u8_t timer_value /* Determines the time_to_expire */;
	u8_t igu_sb_id /* sb_id within the IGU */;
	u8_t igu_seg_id /* use enum hc_segmenet */;
#endif
	u32_t time_to_expire /* The time in which it expects to wake up */;
};

/*
 * hold PCI identification variables- used in various places in firmware
 */
struct pci_entity
{
#if defined(__BIG_ENDIAN)
	u8_t vf_valid /* If set, this is a VF, otherwise it is PF */;
	u8_t vf_id /* VF ID (0-63). Value of 0xFF means VF not valid */;
	u8_t vnic_id /* Virtual NIC ID (0-3) */;
	u8_t pf_id /* PCI physical function number (0-7). The LSB of this field is the port ID */;
#elif defined(__LITTLE_ENDIAN)
	u8_t pf_id /* PCI physical function number (0-7). The LSB of this field is the port ID */;
	u8_t vnic_id /* Virtual NIC ID (0-3) */;
	u8_t vf_id /* VF ID (0-63). Value of 0xFF means VF not valid */;
	u8_t vf_valid /* If set, this is a VF, otherwise it is PF */;
#endif
};

/*
 * The fast-path status block meta-data, common to all chips
 */
struct hc_sb_data
{
	struct regpair_t host_sb_addr /* Host status block address */;
	struct hc_status_block_sm state_machine[HC_SB_MAX_SM] /* Holds the state machines of the status block */;
	struct pci_entity p_func /* vnic / port of the status block to be set by the driver */;
#if defined(__BIG_ENDIAN)
	u16_t rsrv0;
	u8_t dhc_qzone_id /* used in E2 only, to specify the HW queue zone ID used for this status block dynamic HC counters */;
	u8_t same_igu_sb_1b /* Indicate that both state-machines acts like single sm */;
#elif defined(__LITTLE_ENDIAN)
	u8_t same_igu_sb_1b /* Indicate that both state-machines acts like single sm */;
	u8_t dhc_qzone_id /* used in E2 only, to specify the HW queue zone ID used for this status block dynamic HC counters */;
	u16_t rsrv0;
#endif
	struct regpair_t rsrv1[2];
};


/*
 * Segment types for host coaslescing
 */
enum hc_segment
{
	HC_REGULAR_SEGMENT=0,
	HC_DEFAULT_SEGMENT=1,
	MAX_HC_SEGMENT
};



/*
 * The fast-path status block meta-data
 */
struct hc_sp_status_block_data
{
	struct regpair_t host_sb_addr /* Host status block address */;
#if defined(__BIG_ENDIAN)
	u16_t rsrv;
	u8_t igu_seg_id /* segment id of the IGU - use enum hc_segmenet */;
	u8_t igu_sb_id /* sb_id within the IGU */;
#elif defined(__LITTLE_ENDIAN)
	u8_t igu_sb_id /* sb_id within the IGU */;
	u8_t igu_seg_id /* segment id of the IGU - use enum hc_segmenet */;
	u16_t rsrv;
#endif
	struct pci_entity p_func /* vnic / port of the status block to be set by the driver */;
};


/*
 * The fast-path status block meta-data
 */
struct hc_status_block_data_e1x
{
	struct hc_index_data index_data[HC_SB_MAX_INDICES_E1X] /* configuration/state parameters for a single protocol index */;
	struct hc_sb_data common /* The fast-path status block meta-data, common to all chips */;
};


/*
 * The fast-path status block meta-data
 */
struct hc_status_block_data_e2
{
	struct hc_index_data index_data[HC_SB_MAX_INDICES_E2] /* configuration/state parameters for a single protocol index */;
	struct hc_sb_data common /* The fast-path status block meta-data, common to all chips */;
};





/*
 * IGU block operartion modes (in Everest2)
 */
enum igu_mode
{
	HC_IGU_BC_MODE=0 /* Backward compatible mode */,
	HC_IGU_NBC_MODE=1 /* Non-backward compatible mode */,
	MAX_IGU_MODE
};


/*
 * IP versions
 */
enum ip_ver
{
	IP_V4=0,
	IP_V6=1,
	MAX_IP_VER
};


/*
 * Link layer flow control modes
 */
enum llfc_mode
{
	LLFC_MODE_NONE=0,
	LLFC_MODE_PFC=1,
	LLFC_MODE_SAFC=2,
	MAX_LLFC_MODE
};



/*
 * Multi-function modes
 */
enum mf_mode
{
	SINGLE_FUNCTION=0,
	MULTI_FUNCTION_SD=1 /* Switch dependent (vlan based) */,
	MULTI_FUNCTION_SI=2 /* Switch independent (mac based) */,
	MULTI_FUNCTION_NIV=3 /* Switch dependent (vntag based) */,
	MAX_MF_MODE
};


/*
 * The data niv vif list ramrod need 
 */
struct niv_vif_list_ramrod_data
{
#if defined(__BIG_ENDIAN)
	u16_t vif_list_index /* the VIF list, in a per pf vector  to add this function to */;
	u8_t func_bit_map /* the function bit map to set */;
	u8_t niv_vif_list_command /* set get, clear all a VIF list id defined by enum vif_list_rule_kind */;
#elif defined(__LITTLE_ENDIAN)
	u8_t niv_vif_list_command /* set get, clear all a VIF list id defined by enum vif_list_rule_kind */;
	u8_t func_bit_map /* the function bit map to set */;
	u16_t vif_list_index /* the VIF list, in a per pf vector  to add this function to */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t reserved1;
	u8_t echo;
	u8_t func_to_clear /* the func id to clear in case of clear func mode */;
#elif defined(__LITTLE_ENDIAN)
	u8_t func_to_clear /* the func id to clear in case of clear func mode */;
	u8_t echo;
	u16_t reserved1;
#endif
};




/*
 * Protocol-common statistics collected by the Tstorm (per pf) $$KEEP_ENDIANNESS$$
 */
struct tstorm_per_pf_stats
{
	struct regpair_t rcv_error_bytes /* number of bytes received with errors */;
};

/*
 *  $$KEEP_ENDIANNESS$$
 */
struct per_pf_stats
{
	struct tstorm_per_pf_stats tstorm_pf_statistics;
};


/*
 * Protocol-common statistics collected by the Tstorm (per port) $$KEEP_ENDIANNESS$$
 */
struct tstorm_per_port_stats
{
	u32_t mac_discard /* number of packets with mac errors */;
	u32_t mac_filter_discard /* the number of good frames dropped because of no perfect match to MAC/VLAN address */;
	u32_t brb_truncate_discard /* the number of packtes that were dropped because they were truncated in BRB */;
	u32_t mf_tag_discard /* the number of good frames dropped because of no match to the outer vlan/VNtag */;
};

/*
 *  $$KEEP_ENDIANNESS$$
 */
struct per_port_stats
{
	struct tstorm_per_port_stats tstorm_port_statistics;
};


/*
 * Protocol-common statistics collected by the Tstorm (per client) $$KEEP_ENDIANNESS$$
 */
struct tstorm_per_queue_stats
{
	struct regpair_t rcv_ucast_bytes /* number of bytes in unicast packets received without errors and pass the filter */;
	u32_t rcv_ucast_pkts /* number of unicast packets received without errors and pass the filter */;
	u32_t checksum_discard /* number of total packets received with checksum error */;
	struct regpair_t rcv_bcast_bytes /* number of bytes in broadcast packets received without errors and pass the filter */;
	u32_t rcv_bcast_pkts /* number of packets in broadcast packets received without errors and pass the filter */;
	u32_t pkts_too_big_discard /* number of too long packets received */;
	struct regpair_t rcv_mcast_bytes /* number of bytes in multicast packets received without errors and pass the filter */;
	u32_t rcv_mcast_pkts /* number of packets in multicast packets received without errors and pass the filter */;
	u32_t ttl0_discard /* the number of good frames dropped because of TTL=0 */;
	u16_t no_buff_discard;
	u16_t reserved0;
	u32_t reserved1;
};

/*
 * Protocol-common statistics collected by the Ustorm (per client) $$KEEP_ENDIANNESS$$
 */
struct ustorm_per_queue_stats
{
	struct regpair_t ucast_no_buff_bytes /* the number of unicast bytes received from network dropped because of no buffer at host */;
	struct regpair_t mcast_no_buff_bytes /* the number of multicast bytes received from network dropped because of no buffer at host */;
	struct regpair_t bcast_no_buff_bytes /* the number of broadcast bytes received from network dropped because of no buffer at host */;
	u32_t ucast_no_buff_pkts /* the number of unicast frames received from network dropped because of no buffer at host */;
	u32_t mcast_no_buff_pkts /* the number of unicast frames received from network dropped because of no buffer at host */;
	u32_t bcast_no_buff_pkts /* the number of unicast frames received from network dropped because of no buffer at host */;
	u32_t coalesced_pkts /* the number of packets coalesced in all aggregations */;
	struct regpair_t coalesced_bytes /* the number of bytes coalesced in all aggregations */;
	u32_t coalesced_events /* the number of aggregations */;
	u32_t coalesced_aborts /* the number of exception which avoid aggregation */;
};

/*
 * Protocol-common statistics collected by the Xstorm (per client)  $$KEEP_ENDIANNESS$$
 */
struct xstorm_per_queue_stats
{
	struct regpair_t ucast_bytes_sent /* number of total bytes sent without errors */;
	struct regpair_t mcast_bytes_sent /* number of total bytes sent without errors */;
	struct regpair_t bcast_bytes_sent /* number of total bytes sent without errors */;
	u32_t ucast_pkts_sent /* number of total packets sent without errors */;
	u32_t mcast_pkts_sent /* number of total packets sent without errors */;
	u32_t bcast_pkts_sent /* number of total packets sent without errors */;
	u32_t error_drop_pkts /* number of total packets drooped due to errors */;
};

/*
 *  $$KEEP_ENDIANNESS$$
 */
struct per_queue_stats
{
	struct tstorm_per_queue_stats tstorm_queue_statistics;
	struct ustorm_per_queue_stats ustorm_queue_statistics;
	struct xstorm_per_queue_stats xstorm_queue_statistics;
};


/*
 * FW version stored in first line of pram $$KEEP_ENDIANNESS$$
 */
struct pram_fw_version
{
	u8_t major /* firmware current major version */;
	u8_t minor /* firmware current minor version */;
	u8_t revision /* firmware current revision version */;
	u8_t engineering /* firmware current engineering version */;
	u8_t flags;
		#define PRAM_FW_VERSION_OPTIMIZED                                                    (0x1<<0) /* BitField flags 	if set, this is optimized ASM */
		#define PRAM_FW_VERSION_OPTIMIZED_SHIFT                                              0
		#define PRAM_FW_VERSION_STORM_ID                                                     (0x3<<1) /* BitField flags 	storm_id identification */
		#define PRAM_FW_VERSION_STORM_ID_SHIFT                                               1
		#define PRAM_FW_VERSION_BIG_ENDIEN                                                   (0x1<<3) /* BitField flags 	if set, this is big-endien ASM */
		#define PRAM_FW_VERSION_BIG_ENDIEN_SHIFT                                             3
		#define PRAM_FW_VERSION_CHIP_VERSION                                                 (0x3<<4) /* BitField flags 	0 - E1, 1 - E1H */
		#define PRAM_FW_VERSION_CHIP_VERSION_SHIFT                                           4
		#define __PRAM_FW_VERSION_RESERVED0                                                  (0x3<<6) /* BitField flags 	 */
		#define __PRAM_FW_VERSION_RESERVED0_SHIFT                                            6
};



/*
 * Ethernet slow path element
 */
union protocol_common_specific_data
{
	u8_t protocol_data[8] /* to fix this structure size to 8 bytes */;
	struct regpair_t phy_address /* SPE physical address */;
	struct regpair_t mac_config_addr /* physical address of the MAC configuration command, as allocated by the driver */;
	struct niv_vif_list_ramrod_data niv_vif_list_data /* The data niv vif list ramrod need */;
};

/*
 * The send queue element
 */
struct protocol_common_spe
{
	struct spe_hdr_t hdr /* SPE header */;
	union protocol_common_specific_data data /* data specific to common protocol */;
};




/*
 * a single rate shaping counter. can be used as protocol or vnic counter
 */
struct rate_shaping_counter
{
	u32_t quota /* Quota for a protocol/vnic */;
#if defined(__BIG_ENDIAN)
	u16_t __reserved0;
	u16_t rate /* Vnic/Protocol rate in units of Mega-bits/sec */;
#elif defined(__LITTLE_ENDIAN)
	u16_t rate /* Vnic/Protocol rate in units of Mega-bits/sec */;
	u16_t __reserved0;
#endif
};



/*
 * per-vnic rate shaping variables
 */
struct rate_shaping_vars_per_vn
{
	struct rate_shaping_counter vn_counter /* per-vnic counter */;
};



/*
 * The send queue element
 */
struct slow_path_element
{
	struct spe_hdr_t hdr /* common data for all protocols */;
	struct regpair_t protocol_data /* additional data specific to the protocol */;
};



/*
 * Protocol-common statistics counter $$KEEP_ENDIANNESS$$
 */
struct stats_counter
{
	u16_t xstats_counter /* xstorm statistics counter */;
	u16_t reserved0;
	u32_t reserved1;
	u16_t tstats_counter /* tstorm statistics counter */;
	u16_t reserved2;
	u32_t reserved3;
	u16_t ustats_counter /* ustorm statistics counter */;
	u16_t reserved4;
	u32_t reserved5;
	u16_t cstats_counter /* ustorm statistics counter */;
	u16_t reserved6;
	u32_t reserved7;
};


/*
 *  $$KEEP_ENDIANNESS$$
 */
struct stats_query_entry
{
	u8_t kind /* use enum stats_query_type */;
	u8_t index /* queue index */;
	u16_t funcID /* the func the statistic will send to */;
	u32_t reserved;
	struct regpair_t address /* pxp address */;
};

/*
 * statistic command $$KEEP_ENDIANNESS$$
 */
struct stats_query_cmd_group
{
	struct stats_query_entry query[STATS_QUERY_CMD_COUNT];
};



/*
 * statistic command header $$KEEP_ENDIANNESS$$
 */
struct stats_query_header
{
	u8_t cmd_num /* command number */;
	u8_t reserved0;
	u16_t drv_stats_counter;
	u32_t reserved1;
	struct regpair_t stats_counters_addrs /* stats counter */;
};


/*
 * Types of statistcis query entry
 */
enum stats_query_type
{
	STATS_TYPE_QUEUE=0,
	STATS_TYPE_PORT=1,
	STATS_TYPE_PF=2,
	STATS_TYPE_TOE=3,
	STATS_TYPE_FCOE=4,
	MAX_STATS_QUERY_TYPE
};


/*
 * per-port PFC variables
 */
struct storm_pfc_struct_per_port
{
#if defined(__BIG_ENDIAN)
	u16_t mid_mac_addr /* 2 MID of MAC address (The driver provides source MAC address of PFC packet) */;
	u16_t msb_mac_addr /* 2 MSB of MAC address (The driver provides source MAC address of PFC packet) */;
#elif defined(__LITTLE_ENDIAN)
	u16_t msb_mac_addr /* 2 MSB of MAC address (The driver provides source MAC address of PFC packet) */;
	u16_t mid_mac_addr /* 2 MID of MAC address (The driver provides source MAC address of PFC packet) */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t pfc_pause_quanta_in_nanosec /* The time in nanosecond for a single PFC pause quanta unit time (equals to the time required to transmit 512 bits of a frame at the data rate of the MAC) */;
	u16_t lsb_mac_addr /* 2 LSB of MAC address (The driver provides source MAC address of PFC packet) */;
#elif defined(__LITTLE_ENDIAN)
	u16_t lsb_mac_addr /* 2 LSB of MAC address (The driver provides source MAC address of PFC packet) */;
	u16_t pfc_pause_quanta_in_nanosec /* The time in nanosecond for a single PFC pause quanta unit time (equals to the time required to transmit 512 bits of a frame at the data rate of the MAC) */;
#endif
};

/*
 * Per-port congestion management variables
 */
struct storm_cmng_struct_per_port
{
	struct storm_pfc_struct_per_port pfc_vars;
};


/*
 * Storm IDs (including attentions for IGU related enums)
 */
enum storm_id
{
	USTORM_ID=0,
	CSTORM_ID=1,
	XSTORM_ID=2,
	TSTORM_ID=3,
	ATTENTION_ID=4,
	MAX_STORM_ID
};



/*
 * Taffic types used in ETS and flow control algorithms
 */
enum traffic_type
{
	LLFC_TRAFFIC_TYPE_NW=0 /* Networking */,
	LLFC_TRAFFIC_TYPE_FCOE=1 /* FCoE */,
	LLFC_TRAFFIC_TYPE_ISCSI=2 /* iSCSI */,
	LLFC_TRAFFIC_TYPE_NW_COS1_E2INTEG=3 /* E2 integration mode */,
	MAX_TRAFFIC_TYPE
};






/*
 * zone A per-queue data
 */
struct tstorm_queue_zone_data
{
	struct regpair_t reserved[4];
};


/*
 * zone B per-VF data
 */
struct tstorm_vf_zone_data
{
	struct regpair_t reserved;
};



/*
 * zone A per-queue data
 */
struct ustorm_queue_zone_data
{
	struct ustorm_eth_rx_producers eth_rx_producers /* ETH RX rings producers */;
	struct regpair_t reserved[3];
};


/*
 * zone B per-VF data
 */
struct ustorm_vf_zone_data
{
	struct regpair_t reserved;
};



/*
 * data per VF-PF channel
 */
struct vf_pf_channel_data
{
#if defined(__BIG_ENDIAN)
	u16_t reserved0;
	u8_t valid /* flag for channel validity. (cleared when identify a VF as malicious) */;
	u8_t state /* channel state (ready / waiting for ack) */;
#elif defined(__LITTLE_ENDIAN)
	u8_t state /* channel state (ready / waiting for ack) */;
	u8_t valid /* flag for channel validity. (cleared when identify a VF as malicious) */;
	u16_t reserved0;
#endif
	u32_t reserved1;
};


/*
 * State of VF-PF channel
 */
enum vf_pf_channel_state
{
	VF_PF_CHANNEL_STATE_READY=0 /* Channel is ready to accept a message from VF */,
	VF_PF_CHANNEL_STATE_WAITING_FOR_ACK=1 /* Channel waits for an ACK from PF */,
	MAX_VF_PF_CHANNEL_STATE
};






/*
 * vif_list_rule_kind
 */
enum vif_list_rule_kind
{
	VIF_LIST_RULE_SET=0,
	VIF_LIST_RULE_GET=1,
	VIF_LIST_RULE_CLEAR_ALL=2,
	VIF_LIST_RULE_CLEAR_FUNC=3,
	MAX_VIF_LIST_RULE_KIND
};



/*
 * zone A per-queue data
 */
struct xstorm_queue_zone_data
{
	struct regpair_t reserved[4];
};


/*
 * zone B per-VF data
 */
struct xstorm_vf_zone_data
{
	struct regpair_t reserved;
};


/*
 * Out-of-order states
 */
enum tcp_ooo_event
{
	TCP_EVENT_ADD_PEN=0,
	TCP_EVENT_ADD_NEW_ISLE=1,
	TCP_EVENT_ADD_ISLE_RIGHT=2,
	TCP_EVENT_ADD_ISLE_LEFT=3,
	TCP_EVENT_JOIN=4,
	TCP_EVENT_NOP=5,
	MAX_TCP_OOO_EVENT
};


/*
 * OOO support modes
 */
enum tcp_tstorm_ooo
{
	TCP_TSTORM_OOO_DROP_AND_PROC_ACK=0,
	TCP_TSTORM_OOO_SEND_PURE_ACK=1,
	TCP_TSTORM_OOO_SUPPORTED=2,
	MAX_TCP_TSTORM_OOO
};










/*
 * toe statistics collected by the Cstorm (per port)
 */
struct cstorm_toe_stats
{
	u32_t no_tx_cqes /* count the number of time storm find that there are no more CQEs */;
	u32_t reserved;
};


/*
 * The toe storm context of Cstorm
 */
struct cstorm_toe_st_context
{
	u32_t bds_ring_page_base_addr_lo /* Base address of next page in host bds ring */;
	u32_t bds_ring_page_base_addr_hi /* Base address of next page in host bds ring */;
	u32_t free_seq /* Sequnce number of the last byte that was free including */;
	u32_t __last_rel_to_notify /* Accumulated release size for the next Chimney completion msg */;
#if defined(__BIG_ENDIAN)
	u16_t __rss_params_ram_line /* The ram line containing the rss params */;
	u16_t bd_cons /* The bd’s ring consumer  */;
#elif defined(__LITTLE_ENDIAN)
	u16_t bd_cons /* The bd’s ring consumer  */;
	u16_t __rss_params_ram_line /* The ram line containing the rss params */;
#endif
	u32_t cpu_id /* CPU id for sending completion for TSS (only 8 bits are used) */;
	u32_t prev_snd_max /* last snd_max that was used for dynamic HC producer update */;
	u32_t __reserved4 /* reserved */;
};

/*
 * Cstorm Toe Storm Aligned Context
 */
struct cstorm_toe_st_aligned_context
{
	struct cstorm_toe_st_context context /* context */;
};



/*
 * prefetched isle bd
 */
struct ustorm_toe_prefetched_isle_bd
{
	u32_t __addr_lo /* receive payload base address  - Single continuous buffer (page) pointer */;
	u32_t __addr_hi /* receive payload base address  - Single continuous buffer (page) pointer */;
#if defined(__BIG_ENDIAN)
	u8_t __reserved1 /* reserved */;
	u8_t __isle_num /* isle_number of the pre-fetched BD */;
	u16_t __buf_un_used /* Number of bytes left for placement in the pre fetched  application/grq bd – 0 size for buffer is not valid */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __buf_un_used /* Number of bytes left for placement in the pre fetched  application/grq bd – 0 size for buffer is not valid */;
	u8_t __isle_num /* isle_number of the pre-fetched BD */;
	u8_t __reserved1 /* reserved */;
#endif
};

/*
 * ring params
 */
struct ustorm_toe_ring_params
{
	u32_t rq_cons_addr_lo /* A pointer to the next to consume application bd */;
	u32_t rq_cons_addr_hi /* A pointer to the next to consume application bd */;
#if defined(__BIG_ENDIAN)
	u8_t __rq_local_cons /* consumer of the local rq ring */;
	u8_t __rq_local_prod /* producer of the local rq ring */;
	u16_t rq_cons /* RQ consumer is the index of the next to consume application bd */;
#elif defined(__LITTLE_ENDIAN)
	u16_t rq_cons /* RQ consumer is the index of the next to consume application bd */;
	u8_t __rq_local_prod /* producer of the local rq ring */;
	u8_t __rq_local_cons /* consumer of the local rq ring */;
#endif
};

/*
 * prefetched bd
 */
struct ustorm_toe_prefetched_bd
{
	u32_t __addr_lo /* receive payload base address  - Single continuous buffer (page) pointer */;
	u32_t __addr_hi /* receive payload base address  - Single continuous buffer (page) pointer */;
#if defined(__BIG_ENDIAN)
	u16_t flags;
		#define __USTORM_TOE_PREFETCHED_BD_START                                             (0x1<<0) /* BitField flags bd command flags	this bd is the begining of an application buffer */
		#define __USTORM_TOE_PREFETCHED_BD_START_SHIFT                                       0
		#define __USTORM_TOE_PREFETCHED_BD_END                                               (0x1<<1) /* BitField flags bd command flags	this bd is the end of an application buffer */
		#define __USTORM_TOE_PREFETCHED_BD_END_SHIFT                                         1
		#define __USTORM_TOE_PREFETCHED_BD_NO_PUSH                                           (0x1<<2) /* BitField flags bd command flags	this application buffer must not be partially completed */
		#define __USTORM_TOE_PREFETCHED_BD_NO_PUSH_SHIFT                                     2
		#define USTORM_TOE_PREFETCHED_BD_SPLIT                                               (0x1<<3) /* BitField flags bd command flags	this application buffer is part of a bigger buffer and this buffer is not the last */
		#define USTORM_TOE_PREFETCHED_BD_SPLIT_SHIFT                                         3
		#define __USTORM_TOE_PREFETCHED_BD_RESERVED1                                         (0xFFF<<4) /* BitField flags bd command flags	reserved */
		#define __USTORM_TOE_PREFETCHED_BD_RESERVED1_SHIFT                                   4
	u16_t __buf_un_used /* Number of bytes left for placement in the pre fetched  application/grq bd – 0 size for buffer is not valid */;
#elif defined(__LITTLE_ENDIAN)
	u16_t __buf_un_used /* Number of bytes left for placement in the pre fetched  application/grq bd – 0 size for buffer is not valid */;
	u16_t flags;
		#define __USTORM_TOE_PREFETCHED_BD_START                                             (0x1<<0) /* BitField flags bd command flags	this bd is the begining of an application buffer */
		#define __USTORM_TOE_PREFETCHED_BD_START_SHIFT                                       0
		#define __USTORM_TOE_PREFETCHED_BD_END                                               (0x1<<1) /* BitField flags bd command flags	this bd is the end of an application buffer */
		#define __USTORM_TOE_PREFETCHED_BD_END_SHIFT                                         1
		#define __USTORM_TOE_PREFETCHED_BD_NO_PUSH                                           (0x1<<2) /* BitField flags bd command flags	this application buffer must not be partially completed */
		#define __USTORM_TOE_PREFETCHED_BD_NO_PUSH_SHIFT                                     2
		#define USTORM_TOE_PREFETCHED_BD_SPLIT                                               (0x1<<3) /* BitField flags bd command flags	this application buffer is part of a bigger buffer and this buffer is not the last */
		#define USTORM_TOE_PREFETCHED_BD_SPLIT_SHIFT                                         3
		#define __USTORM_TOE_PREFETCHED_BD_RESERVED1                                         (0xFFF<<4) /* BitField flags bd command flags	reserved */
		#define __USTORM_TOE_PREFETCHED_BD_RESERVED1_SHIFT                                   4
#endif
};

/*
 * Ustorm Toe Storm Context
 */
struct ustorm_toe_st_context
{
	u32_t __pen_rq_placed /* Number of bytes that were placed in the RQ and not completed yet. */;
	u32_t pen_grq_placed_bytes /* The number of in-order bytes (peninsula) that were placed in the GRQ (excluding bytes that were already “copied” to RQ BDs or RQ dummy BDs) */;
#if defined(__BIG_ENDIAN)
	u8_t flags2;
		#define USTORM_TOE_ST_CONTEXT_IGNORE_GRQ_PUSH                                        (0x1<<0) /* BitField flags2 various state flags	we will ignore grq push unless it is ping pong test */
		#define USTORM_TOE_ST_CONTEXT_IGNORE_GRQ_PUSH_SHIFT                                  0
		#define USTORM_TOE_ST_CONTEXT_PUSH_FLAG                                              (0x1<<1) /* BitField flags2 various state flags	indicates if push timer is set */
		#define USTORM_TOE_ST_CONTEXT_PUSH_FLAG_SHIFT                                        1
		#define USTORM_TOE_ST_CONTEXT_RSS_UPDATE_ENABLED                                     (0x1<<2) /* BitField flags2 various state flags	indicates if RSS update is supported */
		#define USTORM_TOE_ST_CONTEXT_RSS_UPDATE_ENABLED_SHIFT                               2
		#define USTORM_TOE_ST_CONTEXT_RESERVED0                                              (0x1F<<3) /* BitField flags2 various state flags	 */
		#define USTORM_TOE_ST_CONTEXT_RESERVED0_SHIFT                                        3
	u8_t __indirection_shift /* Offset in bits of the cupid of this connection on the 64Bits fetched from internal memoy */;
	u16_t indirection_ram_offset /* address offset in internal memory  from the begining of the table  consisting the cpu id of this connection (Only 12 bits are used) */;
#elif defined(__LITTLE_ENDIAN)
	u16_t indirection_ram_offset /* address offset in internal memory  from the begining of the table  consisting the cpu id of this connection (Only 12 bits are used) */;
	u8_t __indirection_shift /* Offset in bits of the cupid of this connection on the 64Bits fetched from internal memoy */;
	u8_t flags2;
		#define USTORM_TOE_ST_CONTEXT_IGNORE_GRQ_PUSH                                        (0x1<<0) /* BitField flags2 various state flags	we will ignore grq push unless it is ping pong test */
		#define USTORM_TOE_ST_CONTEXT_IGNORE_GRQ_PUSH_SHIFT                                  0
		#define USTORM_TOE_ST_CONTEXT_PUSH_FLAG                                              (0x1<<1) /* BitField flags2 various state flags	indicates if push timer is set */
		#define USTORM_TOE_ST_CONTEXT_PUSH_FLAG_SHIFT                                        1
		#define USTORM_TOE_ST_CONTEXT_RSS_UPDATE_ENABLED                                     (0x1<<2) /* BitField flags2 various state flags	indicates if RSS update is supported */
		#define USTORM_TOE_ST_CONTEXT_RSS_UPDATE_ENABLED_SHIFT                               2
		#define USTORM_TOE_ST_CONTEXT_RESERVED0                                              (0x1F<<3) /* BitField flags2 various state flags	 */
		#define USTORM_TOE_ST_CONTEXT_RESERVED0_SHIFT                                        3
#endif
	u32_t __rq_available_bytes;
#if defined(__BIG_ENDIAN)
	u8_t isles_counter /* signals that dca is enabled */;
	u8_t __push_timer_state /* indicates if push timer is set */;
	u16_t rcv_indication_size /* The chip will release the current GRQ buffer to the driver when it knows that the driver has no knowledge of other GRQ payload that it can indicate and the current GRQ buffer has at least RcvIndicationSize bytes. */;
#elif defined(__LITTLE_ENDIAN)
	u16_t rcv_indication_size /* The chip will release the current GRQ buffer to the driver when it knows that the driver has no knowledge of other GRQ payload that it can indicate and the current GRQ buffer has at least RcvIndicationSize bytes. */;
	u8_t __push_timer_state /* indicates if push timer is set */;
	u8_t isles_counter /* signals that dca is enabled */;
#endif
	u32_t __min_expiration_time /* if the timer will expire before this time it will be considered as a race */;
	u32_t initial_rcv_wnd /* the maximal advertized window */;
	u32_t __bytes_cons /* the last rq_available_bytes producer that was read from host - used to know how many bytes were added */;
	u32_t __prev_consumed_grq_bytes /* the last rq_available_bytes producer that was read from host - used to know how many bytes were added */;
	u32_t prev_rcv_win_right_edge /* siquence of the last bytes that can be recieved - used to know how many bytes were added */;
	u32_t rcv_nxt /* Receive sequence: next expected - of the right most recieved packet */;
	struct ustorm_toe_prefetched_isle_bd __isle_bd /* prefetched bd for the isle */;
	struct ustorm_toe_ring_params pen_ring_params /* peninsula ring params */;
	struct ustorm_toe_prefetched_bd __pen_bd_0 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_1 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_2 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_3 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_4 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_5 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_6 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_7 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_8 /* peninsula prefetched bd for the peninsula */;
	struct ustorm_toe_prefetched_bd __pen_bd_9 /* peninsula prefetched bd for the peninsula */;
	u32_t __reserved3 /* reserved */;
};

/*
 * Ustorm Toe Storm Aligned Context
 */
struct ustorm_toe_st_aligned_context
{
	struct ustorm_toe_st_context context /* context */;
};

/*
 * TOE context region, used only in TOE
 */
struct tstorm_toe_st_context_section
{
	u32_t reserved0[3];
};

/*
 * The TOE non-aggregative context of Tstorm
 */
struct tstorm_toe_st_context
{
	struct tstorm_tcp_st_context_section tcp /* TCP context region, shared in TOE, RDMA and ISCSI */;
	struct tstorm_toe_st_context_section toe /* TOE context region, used only in TOE */;
};

/*
 * The TOE non-aggregative aligned context of Tstorm
 */
struct tstorm_toe_st_aligned_context
{
	struct tstorm_toe_st_context context /* context */;
	u8_t padding[16] /* padding to 64 byte aligned */;
};

/*
 * TOE context section
 */
struct xstorm_toe_context_section
{
	u32_t tx_bd_page_base_lo /* BD page base address at the host for TxBdCons */;
	u32_t tx_bd_page_base_hi /* BD page base address at the host for TxBdCons */;
#if defined(__BIG_ENDIAN)
	u16_t tx_bd_offset /* The offset within the BD */;
	u16_t tx_bd_cons /* The transmit BD cons pointer to the host ring */;
#elif defined(__LITTLE_ENDIAN)
	u16_t tx_bd_cons /* The transmit BD cons pointer to the host ring */;
	u16_t tx_bd_offset /* The offset within the BD */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t bd_prod;
	u16_t reserved;
#elif defined(__LITTLE_ENDIAN)
	u16_t reserved;
	u16_t bd_prod;
#endif
	u32_t driver_doorbell_info_ptr_lo;
	u32_t driver_doorbell_info_ptr_hi;
};

/*
 * Xstorm Toe Storm Context
 */
struct xstorm_toe_st_context
{
	struct xstorm_common_context_section common;
	struct xstorm_toe_context_section toe;
};

/*
 * Xstorm Toe Storm Aligned Context
 */
struct xstorm_toe_st_aligned_context
{
	struct xstorm_toe_st_context context /* context */;
};

/*
 * Ethernet connection context 
 */
struct toe_context
{
	struct ustorm_toe_st_aligned_context ustorm_st_context /* Ustorm storm context */;
	struct tstorm_toe_st_aligned_context tstorm_st_context /* Tstorm storm context */;
	struct xstorm_toe_ag_context xstorm_ag_context /* Xstorm aggregative context */;
	struct tstorm_toe_ag_context tstorm_ag_context /* Tstorm aggregative context */;
	struct cstorm_toe_ag_context cstorm_ag_context /* Cstorm aggregative context */;
	struct ustorm_toe_ag_context ustorm_ag_context /* Ustorm aggregative context */;
	struct timers_block_context timers_context /* Timers block context */;
	struct xstorm_toe_st_aligned_context xstorm_st_context /* Xstorm storm context */;
	struct cstorm_toe_st_aligned_context cstorm_st_context /* Cstorm storm context */;
};


/*
 * ramrod data for toe protocol initiate offload ramrod (CQE)
 */
struct toe_initiate_offload_ramrod_data
{
	u32_t flags;
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_SEARCH_CONFIG_FAILED                        (0x1<<0) /* BitField flags 	error in searcher configuration */
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_SEARCH_CONFIG_FAILED_SHIFT                  0
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_LICENSE_FAILURE                             (0x1<<1) /* BitField flags 	license errors */
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_LICENSE_FAILURE_SHIFT                       1
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_RESERVED0                                   (0x3FFFFFFF<<2) /* BitField flags 	 */
		#define TOE_INITIATE_OFFLOAD_RAMROD_DATA_RESERVED0_SHIFT                             2
	u32_t reserved1;
};


/*
 * union for ramrod data for TOE protocol (CQE) (force size of 16 bits)
 */
struct toe_init_ramrod_data
{
#if defined(__BIG_ENDIAN)
	u16_t reserved1;
	u8_t reserved0;
	u8_t rss_num /* the rss num in its rqr to complete this ramrod */;
#elif defined(__LITTLE_ENDIAN)
	u8_t rss_num /* the rss num in its rqr to complete this ramrod */;
	u8_t reserved0;
	u16_t reserved1;
#endif
	u32_t reserved2;
};


/*
 * next page pointer bd used in toe CQs and tx/rx bd chains
 */
struct toe_page_addr_bd
{
	u32_t addr_lo /* page pointer */;
	u32_t addr_hi /* page pointer */;
	u8_t reserved[8] /* resereved for driver use */;
};


/*
 * union for ramrod data for TOE protocol (CQE) (force size of 16 bits)
 */
union toe_ramrod_data
{
	struct ramrod_data general;
	struct toe_initiate_offload_ramrod_data initiate_offload;
};


/*
 * TOE_RX_CQES_OPCODE_RSS_UPD results
 */
enum toe_rss_update_opcode
{
	TOE_RSS_UPD_QUIET=0,
	TOE_RSS_UPD_SLEEPING=1,
	TOE_RSS_UPD_DELAYED=2,
	MAX_TOE_RSS_UPDATE_OPCODE
};


/*
 * union for ramrod data for TOE protocol (CQE) (force size of 16 bits)
 */
struct toe_rss_update_ramrod_data
{
	u8_t indirection_table[128] /* RSS indirection table */;
#if defined(__BIG_ENDIAN)
	u16_t reserved0;
	u16_t toe_rss_bitmap /* The bitmap specifies which toe rss chains to complete the ramrod on (0 bitmap is not valid option). The port is gleaned from the CID */;
#elif defined(__LITTLE_ENDIAN)
	u16_t toe_rss_bitmap /* The bitmap specifies which toe rss chains to complete the ramrod on (0 bitmap is not valid option). The port is gleaned from the CID */;
	u16_t reserved0;
#endif
	u32_t reserved1;
};


/*
 * The toe Rx Buffer Descriptor
 */
struct toe_rx_bd
{
	u32_t addr_lo /* receive payload base address  - Single continuous buffer (page) pointer */;
	u32_t addr_hi /* receive payload base address  - Single continuous buffer (page) pointer */;
#if defined(__BIG_ENDIAN)
	u16_t flags;
		#define TOE_RX_BD_START                                                              (0x1<<0) /* BitField flags bd command flags	this bd is the begining of an application buffer */
		#define TOE_RX_BD_START_SHIFT                                                        0
		#define TOE_RX_BD_END                                                                (0x1<<1) /* BitField flags bd command flags	this bd is the end of an application buffer */
		#define TOE_RX_BD_END_SHIFT                                                          1
		#define TOE_RX_BD_NO_PUSH                                                            (0x1<<2) /* BitField flags bd command flags	this application buffer must not be partially completed */
		#define TOE_RX_BD_NO_PUSH_SHIFT                                                      2
		#define TOE_RX_BD_SPLIT                                                              (0x1<<3) /* BitField flags bd command flags	this application buffer is part of a bigger buffer and this buffer is not the last */
		#define TOE_RX_BD_SPLIT_SHIFT                                                        3
		#define TOE_RX_BD_RESERVED1                                                          (0xFFF<<4) /* BitField flags bd command flags	reserved */
		#define TOE_RX_BD_RESERVED1_SHIFT                                                    4
	u16_t size /* Size of the buffer pointed by the BD */;
#elif defined(__LITTLE_ENDIAN)
	u16_t size /* Size of the buffer pointed by the BD */;
	u16_t flags;
		#define TOE_RX_BD_START                                                              (0x1<<0) /* BitField flags bd command flags	this bd is the begining of an application buffer */
		#define TOE_RX_BD_START_SHIFT                                                        0
		#define TOE_RX_BD_END                                                                (0x1<<1) /* BitField flags bd command flags	this bd is the end of an application buffer */
		#define TOE_RX_BD_END_SHIFT                                                          1
		#define TOE_RX_BD_NO_PUSH                                                            (0x1<<2) /* BitField flags bd command flags	this application buffer must not be partially completed */
		#define TOE_RX_BD_NO_PUSH_SHIFT                                                      2
		#define TOE_RX_BD_SPLIT                                                              (0x1<<3) /* BitField flags bd command flags	this application buffer is part of a bigger buffer and this buffer is not the last */
		#define TOE_RX_BD_SPLIT_SHIFT                                                        3
		#define TOE_RX_BD_RESERVED1                                                          (0xFFF<<4) /* BitField flags bd command flags	reserved */
		#define TOE_RX_BD_RESERVED1_SHIFT                                                    4
#endif
	u32_t dbg_bytes_prod /* a cyclic parameter that caounts how many byte were available for placement till no not including this bd */;
};


/*
 * ramrod data for toe protocol General rx completion
 */
struct toe_rx_completion_ramrod_data
{
#if defined(__BIG_ENDIAN)
	u16_t reserved0;
	u16_t hash_value /* information for ustorm to use in completion */;
#elif defined(__LITTLE_ENDIAN)
	u16_t hash_value /* information for ustorm to use in completion */;
	u16_t reserved0;
#endif
	u32_t reserved1;
};


/*
 * OOO params in union for TOE rx cqe data
 */
struct toe_rx_cqe_ooo_params
{
	u32_t ooo_params;
		#define TOE_RX_CQE_OOO_PARAMS_NBYTES                                                 (0xFFFFFF<<0) /* BitField ooo_params data params for OOO cqe	connection nbytes */
		#define TOE_RX_CQE_OOO_PARAMS_NBYTES_SHIFT                                           0
		#define TOE_RX_CQE_OOO_PARAMS_ISLE_NUM                                               (0xFF<<24) /* BitField ooo_params data params for OOO cqe	isle number for OOO completions */
		#define TOE_RX_CQE_OOO_PARAMS_ISLE_NUM_SHIFT                                         24
};

/*
 * in order params in union for TOE rx cqe data
 */
struct toe_rx_cqe_in_order_params
{
	u32_t in_order_params;
		#define TOE_RX_CQE_IN_ORDER_PARAMS_NBYTES                                            (0xFFFFFFFF<<0) /* BitField in_order_params data params for in order cqe	connection nbytes */
		#define TOE_RX_CQE_IN_ORDER_PARAMS_NBYTES_SHIFT                                      0
};

/*
 * union for TOE rx cqe data
 */
union toe_rx_cqe_data_union
{
	struct toe_rx_cqe_ooo_params ooo_params /* data params for OOO cqe - nbytes and isle number */;
	struct toe_rx_cqe_in_order_params in_order_params /* data params for in order cqe - nbytes */;
	u32_t raw_data /* global data param */;
};

/*
 * The toe Rx cq element
 */
struct toe_rx_cqe
{
	u32_t params1;
		#define TOE_RX_CQE_CID                                                               (0xFFFFFF<<0) /* BitField params1 completion cid and opcode	connection id */
		#define TOE_RX_CQE_CID_SHIFT                                                         0
		#define TOE_RX_CQE_COMPLETION_OPCODE                                                 (0xFF<<24) /* BitField params1 completion cid and opcode	completion opcode - use enum toe_rx_cqe_type or toe_rss_update_opcode */
		#define TOE_RX_CQE_COMPLETION_OPCODE_SHIFT                                           24
	union toe_rx_cqe_data_union data /* completion cid and opcode */;
};





/*
 * TOE RX CQEs opcodes (opcode 0 is illegal)
 */
enum toe_rx_cqe_type
{
	TOE_RX_CQES_OPCODE_GA=1,
	TOE_RX_CQES_OPCODE_GR=2,
	TOE_RX_CQES_OPCODE_GNI=3,
	TOE_RX_CQES_OPCODE_GAIR=4,
	TOE_RX_CQES_OPCODE_GAIL=5,
	TOE_RX_CQES_OPCODE_GRI=6,
	TOE_RX_CQES_OPCODE_GJ=7,
	TOE_RX_CQES_OPCODE_DGI=8,
	TOE_RX_CQES_OPCODE_CMP=9,
	TOE_RX_CQES_OPCODE_REL=10,
	TOE_RX_CQES_OPCODE_SKP=11,
	TOE_RX_CQES_OPCODE_FIN_RCV=16,
	TOE_RX_CQES_OPCODE_RST_RCV=17,
	TOE_RX_CQES_OPCODE_RST_CMP=18,
	TOE_RX_CQES_OPCODE_URG=19,
	TOE_RX_CQES_OPCODE_RT_TO=20,
	TOE_RX_CQES_OPCODE_KA_TO=21,
	TOE_RX_CQES_OPCODE_INV_CMP=23,
	TOE_RX_CQES_OPCODE_QRY_CMP=24,
	TOE_RX_CQES_OPCODE_UPD_CMP=25,
	TOE_RX_CQES_OPCODE_TRM_CMP=26,
	TOE_RX_CQES_OPCODE_MAX_RT=28,
	TOE_RX_CQES_OPCODE_DBT_RE=29,
	TOE_RX_CQES_OPCODE_SRC_CMP=30,
	TOE_RX_CQES_OPCODE_SRC_ERR=31,
	TOE_RX_CQES_OPCODE_DRV_RSV=32,
	TOE_RX_CQES_OPCODE_SYN=33,
	TOE_RX_CQES_OPCODE_OPT_ERR=34,
	TOE_RX_CQES_OPCODE_FW2_TO=35,
	TOE_RX_CQES_OPCODE_OFL_CMP=36,
	TOE_RX_CQES_OPCODE_LCN_ERR=37,
	TOE_RX_CQES_OPCODE_FIN_UPL=38,
	TOE_RX_CQES_OPCODE_2WY_CLS=39,
	TOE_RX_CQES_OPCODE_RSS_UPD=40,
	TOE_RX_CQES_OPCODE_TOE_START_CMP=100,
	TOE_RX_CQES_OPCODE_DRIVER_RSVD_1=200,
	TOE_RX_CQES_OPCODE_DRIVER_RSVD_2=201,
	MAX_TOE_RX_CQE_TYPE
};


/*
 * toe rx doorbell data in host memory
 */
struct toe_rx_db_data
{
	u32_t rcv_win_right_edge /* siquence of the last bytes that can be recieved */;
	u32_t bytes_prod /* cyclic counter of posted bytes */;
#if defined(__BIG_ENDIAN)
	u8_t reserved1 /* reserved */;
	u8_t flags;
		#define TOE_RX_DB_DATA_IGNORE_WND_UPDATES                                            (0x1<<0) /* BitField flags 	ustorm ignores window updates when this flag is set */
		#define TOE_RX_DB_DATA_IGNORE_WND_UPDATES_SHIFT                                      0
		#define TOE_RX_DB_DATA_PARTIAL_FILLED_BUF                                            (0x1<<1) /* BitField flags 	indicates if to set push timer due to partially filled receive request after offload */
		#define TOE_RX_DB_DATA_PARTIAL_FILLED_BUF_SHIFT                                      1
		#define TOE_RX_DB_DATA_RESERVED0                                                     (0x3F<<2) /* BitField flags 	 */
		#define TOE_RX_DB_DATA_RESERVED0_SHIFT                                               2
	u16_t bds_prod /* cyclic counter of bds to post */;
#elif defined(__LITTLE_ENDIAN)
	u16_t bds_prod /* cyclic counter of bds to post */;
	u8_t flags;
		#define TOE_RX_DB_DATA_IGNORE_WND_UPDATES                                            (0x1<<0) /* BitField flags 	ustorm ignores window updates when this flag is set */
		#define TOE_RX_DB_DATA_IGNORE_WND_UPDATES_SHIFT                                      0
		#define TOE_RX_DB_DATA_PARTIAL_FILLED_BUF                                            (0x1<<1) /* BitField flags 	indicates if to set push timer due to partially filled receive request after offload */
		#define TOE_RX_DB_DATA_PARTIAL_FILLED_BUF_SHIFT                                      1
		#define TOE_RX_DB_DATA_RESERVED0                                                     (0x3F<<2) /* BitField flags 	 */
		#define TOE_RX_DB_DATA_RESERVED0_SHIFT                                               2
	u8_t reserved1 /* reserved */;
#endif
	u32_t consumed_grq_bytes /* cyclic counter of consumed grq bytes */;
};


/*
 * The toe Rx Generic Buffer Descriptor
 */
struct toe_rx_grq_bd
{
	u32_t addr_lo /* receive payload base address  - Single continuous buffer (page) pointer */;
	u32_t addr_hi /* receive payload base address  - Single continuous buffer (page) pointer */;
};


/*
 * toe slow path element
 */
union toe_spe_data
{
	u8_t protocol_data[8] /* to fix this structure size to 8 bytes */;
	struct regpair_t phys_addr /* used in initiate offload ramrod */;
	struct toe_rx_completion_ramrod_data rx_completion /* used in all ramrods that have a general rx completion */;
	struct toe_init_ramrod_data toe_init /* used in toe init ramrod */;
};

/*
 * toe slow path element
 */
struct toe_spe
{
	struct spe_hdr_t hdr /* common data for all protocols */;
	union toe_spe_data toe_data /* data specific to toe protocol */;
};



/*
 * TOE command ID for slow path elements
 */
enum toe_spqe_cmd_id
{
	RAMROD_CMD_ID_TOE_START=1,
	RAMROD_CMD_ID_TOE_INITIATE_OFFLOAD=2,
	RAMROD_CMD_ID_TOE_SEARCHER_DELETE=3,
	RAMROD_CMD_ID_TOE_TERMINATE=4,
	RAMROD_CMD_ID_TOE_QUERY=5,
	RAMROD_CMD_ID_TOE_RESET=6,
	RAMROD_CMD_ID_TOE_DRIVER_RESERVED=7,
	RAMROD_CMD_ID_TOE_INVALIDATE=8,
	RAMROD_CMD_ID_TOE_UPDATE=9,
	RAMROD_CMD_ID_TOE_RSS_UPDATE=10,
	MAX_TOE_SPQE_CMD_ID
};


/*
 * Toe statistics collected by the Xstorm (per port)
 */
struct xstorm_toe_stats_section
{
	u32_t tcp_out_segments;
	u32_t tcp_retransmitted_segments;
	struct regpair_t ip_out_octets;
	u32_t ip_out_requests;
	u32_t reserved;
};

/*
 * Toe statistics collected by the Xstorm (per port)
 */
struct xstorm_toe_stats
{
	struct xstorm_toe_stats_section statistics[2] /* 0 - ipv4 , 1 - ipv6 */;
	u32_t reserved[2];
};

/*
 * Toe statistics collected by the Tstorm (per port)
 */
struct tstorm_toe_stats_section
{
	u32_t ip_in_receives;
	u32_t ip_in_delivers;
	struct regpair_t ip_in_octets;
	u32_t tcp_in_errors /* all discards except discards already counted by Ipv4 stats */;
	u32_t ip_in_header_errors /* IP checksum */;
	u32_t ip_in_discards /* no resources */;
	u32_t ip_in_truncated_packets;
};

/*
 * Toe statistics collected by the Tstorm (per port)
 */
struct tstorm_toe_stats
{
	struct tstorm_toe_stats_section statistics[2] /* 0 - ipv4 , 1 - ipv6 */;
	u32_t reserved[2];
};

/*
 * Eth statistics query structure for the eth_stats_query ramrod
 */
struct toe_stats_query
{
	struct xstorm_toe_stats xstorm_toe /* Xstorm Toe statistics structure */;
	struct tstorm_toe_stats tstorm_toe /* Tstorm Toe statistics structure */;
	struct cstorm_toe_stats cstorm_toe /* Cstorm Toe statistics structure */;
};


/*
 * The toe Tx Buffer Descriptor
 */
struct toe_tx_bd
{
	u32_t addr_lo /* tranasmit payload base address  - Single continuous buffer (page) pointer */;
	u32_t addr_hi /* tranasmit payload base address  - Single continuous buffer (page) pointer */;
#if defined(__BIG_ENDIAN)
	u16_t flags;
		#define TOE_TX_BD_PUSH                                                               (0x1<<0) /* BitField flags bd command flags	End of data flag */
		#define TOE_TX_BD_PUSH_SHIFT                                                         0
		#define TOE_TX_BD_NOTIFY                                                             (0x1<<1) /* BitField flags bd command flags	notify driver with released data bytes including this bd */
		#define TOE_TX_BD_NOTIFY_SHIFT                                                       1
		#define TOE_TX_BD_FIN                                                                (0x1<<2) /* BitField flags bd command flags	send fin request */
		#define TOE_TX_BD_FIN_SHIFT                                                          2
		#define TOE_TX_BD_LARGE_IO                                                           (0x1<<3) /* BitField flags bd command flags	this bd is part of an application buffer larger than mss */
		#define TOE_TX_BD_LARGE_IO_SHIFT                                                     3
		#define TOE_TX_BD_RESERVED1                                                          (0xFFF<<4) /* BitField flags bd command flags	reserved */
		#define TOE_TX_BD_RESERVED1_SHIFT                                                    4
	u16_t size /* Size of the data represented by the BD */;
#elif defined(__LITTLE_ENDIAN)
	u16_t size /* Size of the data represented by the BD */;
	u16_t flags;
		#define TOE_TX_BD_PUSH                                                               (0x1<<0) /* BitField flags bd command flags	End of data flag */
		#define TOE_TX_BD_PUSH_SHIFT                                                         0
		#define TOE_TX_BD_NOTIFY                                                             (0x1<<1) /* BitField flags bd command flags	notify driver with released data bytes including this bd */
		#define TOE_TX_BD_NOTIFY_SHIFT                                                       1
		#define TOE_TX_BD_FIN                                                                (0x1<<2) /* BitField flags bd command flags	send fin request */
		#define TOE_TX_BD_FIN_SHIFT                                                          2
		#define TOE_TX_BD_LARGE_IO                                                           (0x1<<3) /* BitField flags bd command flags	this bd is part of an application buffer larger than mss */
		#define TOE_TX_BD_LARGE_IO_SHIFT                                                     3
		#define TOE_TX_BD_RESERVED1                                                          (0xFFF<<4) /* BitField flags bd command flags	reserved */
		#define TOE_TX_BD_RESERVED1_SHIFT                                                    4
#endif
	u32_t reserved2;
};


/*
 * The toe Tx cqe
 */
struct toe_tx_cqe
{
	u32_t params;
		#define TOE_TX_CQE_CID                                                               (0xFFFFFF<<0) /* BitField params completion cid and opcode	connection id */
		#define TOE_TX_CQE_CID_SHIFT                                                         0
		#define TOE_TX_CQE_COMPLETION_OPCODE                                                 (0xFF<<24) /* BitField params completion cid and opcode	completion opcode - use enum toe_tx_cqe_type */
		#define TOE_TX_CQE_COMPLETION_OPCODE_SHIFT                                           24
	u32_t len /* the more2release in Bytes */;
};


/*
 * TOE TX CQEs opcodes (opcode 0 is illegal)
 */
enum toe_tx_cqe_type
{
	TOE_TX_CQES_OPCODE_CMP=1,
	TOE_TX_CQES_OPCODE_RST_RCV=10,
	TOE_TX_CQES_OPCODE_RST_CMP=11,
	TOE_TX_CQES_OPCODE_INV_CMP=12,
	TOE_TX_CQES_OPCODE_TRM_CMP=13,
	TOE_TX_CQES_OPCODE_DRV_RSV=14,
	MAX_TOE_TX_CQE_TYPE
};


/*
 * toe tx doorbell data in host memory
 */
struct toe_tx_db_data
{
	u32_t bytes_prod_seq /* greatest sequence the chip can transmit */;
#if defined(__BIG_ENDIAN)
	u16_t flags;
		#define TOE_TX_DB_DATA_FIN                                                           (0x1<<0) /* BitField flags 	flag for post FIN request */
		#define TOE_TX_DB_DATA_FIN_SHIFT                                                     0
		#define TOE_TX_DB_DATA_FLUSH                                                         (0x1<<1) /* BitField flags 	flag for last doorbell - flushing doorbell queue */
		#define TOE_TX_DB_DATA_FLUSH_SHIFT                                                   1
		#define TOE_TX_DB_DATA_RESERVE                                                       (0x3FFF<<2) /* BitField flags 	 */
		#define TOE_TX_DB_DATA_RESERVE_SHIFT                                                 2
	u16_t bds_prod /* cyclic counter of posted bds */;
#elif defined(__LITTLE_ENDIAN)
	u16_t bds_prod /* cyclic counter of posted bds */;
	u16_t flags;
		#define TOE_TX_DB_DATA_FIN                                                           (0x1<<0) /* BitField flags 	flag for post FIN request */
		#define TOE_TX_DB_DATA_FIN_SHIFT                                                     0
		#define TOE_TX_DB_DATA_FLUSH                                                         (0x1<<1) /* BitField flags 	flag for last doorbell - flushing doorbell queue */
		#define TOE_TX_DB_DATA_FLUSH_SHIFT                                                   1
		#define TOE_TX_DB_DATA_RESERVE                                                       (0x3FFF<<2) /* BitField flags 	 */
		#define TOE_TX_DB_DATA_RESERVE_SHIFT                                                 2
#endif
};


/*
 * sturct used in update ramrod. Driver notifies chip which fields have changed via the bitmap  $$KEEP_ENDIANNESS$$
 */
struct toe_update_ramrod_cached_params
{
	u16_t changed_fields;
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_DEST_ADDR_CHANGED                            (0x1<<0) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_DEST_ADDR_CHANGED_SHIFT                      0
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_MSS_CHANGED                                  (0x1<<1) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_MSS_CHANGED_SHIFT                            1
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_TIMEOUT_CHANGED                           (0x1<<2) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_TIMEOUT_CHANGED_SHIFT                     2
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_INTERVAL_CHANGED                          (0x1<<3) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_INTERVAL_CHANGED_SHIFT                    3
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_MAX_RT_CHANGED                               (0x1<<4) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_MAX_RT_CHANGED_SHIFT                         4
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_RCV_INDICATION_SIZE_CHANGED                  (0x1<<5) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_RCV_INDICATION_SIZE_CHANGED_SHIFT            5
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_FLOW_LABEL_CHANGED                           (0x1<<6) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_FLOW_LABEL_CHANGED_SHIFT                     6
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_ENABLE_KEEPALIVE_CHANGED                     (0x1<<7) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_ENABLE_KEEPALIVE_CHANGED_SHIFT               7
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_ENABLE_NAGLE_CHANGED                         (0x1<<8) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_ENABLE_NAGLE_CHANGED_SHIFT                   8
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TTL_CHANGED                                  (0x1<<9) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TTL_CHANGED_SHIFT                            9
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_HOP_LIMIT_CHANGED                            (0x1<<10) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_HOP_LIMIT_CHANGED_SHIFT                      10
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TOS_CHANGED                                  (0x1<<11) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TOS_CHANGED_SHIFT                            11
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TRAFFIC_CLASS_CHANGED                        (0x1<<12) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_TRAFFIC_CLASS_CHANGED_SHIFT                  12
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_MAX_PROBE_COUNT_CHANGED                   (0x1<<13) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_KA_MAX_PROBE_COUNT_CHANGED_SHIFT             13
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_USER_PRIORITY_CHANGED                        (0x1<<14) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_USER_PRIORITY_CHANGED_SHIFT                  14
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_INITIAL_RCV_WND_CHANGED                      (0x1<<15) /* BitField changed_fields bitmap for indicating changed fields	 */
		#define TOE_UPDATE_RAMROD_CACHED_PARAMS_INITIAL_RCV_WND_CHANGED_SHIFT                15
	u8_t ka_restart /* Only 1 bit is used */;
	u8_t retransmit_restart /* Only 1 bit is used */;
	u8_t dest_addr[6];
	u16_t mss;
	u32_t ka_timeout;
	u32_t ka_interval;
	u32_t max_rt;
	u32_t flow_label /* Only 20 bits are used */;
	u16_t rcv_indication_size;
	u8_t enable_keepalive /* Only 1 bit is used */;
	u8_t enable_nagle /* Only 1 bit is used */;
	u8_t ttl;
	u8_t hop_limit;
	u8_t tos;
	u8_t traffic_class;
	u8_t ka_max_probe_count;
	u8_t user_priority /* Only 4 bits are used */;
	u16_t reserved2;
	u32_t initial_rcv_wnd;
	u32_t reserved1;
};










/*
 * rx rings pause data for E1h only
 */
struct ustorm_toe_rx_pause_data_e1h
{
#if defined(__BIG_ENDIAN)
	u16_t grq_thr_low /* number of remaining grqes under which, we send pause message */;
	u16_t cq_thr_low /* number of remaining cqes under which, we send pause message */;
#elif defined(__LITTLE_ENDIAN)
	u16_t cq_thr_low /* number of remaining cqes under which, we send pause message */;
	u16_t grq_thr_low /* number of remaining grqes under which, we send pause message */;
#endif
#if defined(__BIG_ENDIAN)
	u16_t grq_thr_high /* number of remaining grqes above which, we send un-pause message */;
	u16_t cq_thr_high /* number of remaining cqes above which, we send un-pause message */;
#elif defined(__LITTLE_ENDIAN)
	u16_t cq_thr_high /* number of remaining cqes above which, we send un-pause message */;
	u16_t grq_thr_high /* number of remaining grqes above which, we send un-pause message */;
#endif
};








#endif /* __5710_HSI_VBD__ */
