/*
 * Copyright (c) 1996, 1999, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_SCSI_ADAPTERS_IFPREG_H
#define	_SYS_SCSI_ADAPTERS_IFPREG_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Hardware definitions for the QLogic ISP2100.
 */

/*
 * Following are the register definitions for the ISP chip.  Note,
 * most of the registers are not accessable during normal operations
 * of the ISP chip.  The on-bard Risc CPU must be in pause mode in
 * order to access these registers.
 *
 *  The following Legend describes what each register is capable of:
 *
 *  	Read-Only:				R
 *  	Write-Only:				W
 *  	Read/Write:				RW
 *  	Read, Write if RISC Paused:		R(W)
 */

/*
 * ISP2100 uses 256 byte PCI I/O space or PCI 4k memory space.
 * Of this, the first 128 bytes are Bus Interface Unit (PBUI) regs.
 * The remaining 128 bytes can be RISC, FB or FPM0 and FPM1 register
 * banks depending on Module Selection bits in the control/status
 * register. The mailbox registers are at an offset of 0x70 in the
 * PBUI register map.
 */

#define	IFP_BUS_BIU_REGS_OFF		0x0
#define	IFP_PCI_MBOX_REGS_OFF		0x70
#define	IFP_PCI_RISC_REGS_OFF		0x80
#define	ISP_PCI_FPM0_REGS_OFF		0x80
#define	ISP_PCI_FPM1_REGS_OFF		0x80
#define	ISP_PCI_FB_REGS_OFF		0x80


struct ifp_risc_regs {
	/*
	 * RISC registers.
	 */
	ushort_t ifp_risc_acc;		/* R(W): Accumulator */
	ushort_t ifp_risc_r1;		/* R(W): GP Reg R1  */
	ushort_t ifp_risc_r2;		/* R(W): GP Reg R2  */
	ushort_t ifp_risc_r3;		/* R(W): GP Reg R3  */
	ushort_t ifp_risc_r4;		/* R(W): GP Reg R4  */
	ushort_t ifp_risc_r5;		/* R(W): GP Reg R5  */
	ushort_t ifp_risc_r6;		/* R(W): GP Reg R6  */
	ushort_t ifp_risc_r7;		/* R(W): GP Reg R7  */
	ushort_t ifp_risc_r8;		/* R(W): GP Reg R8  */
	ushort_t ifp_risc_r9;		/* R(W): GP Reg R9  */
	ushort_t ifp_risc_r10;		/* R(W): GP Reg R10 */
	ushort_t ifp_risc_r11;		/* R(W): GP Reg R11 */
	ushort_t ifp_risc_r12;		/* R(W): GP Reg R12 */
	ushort_t ifp_risc_r13;		/* R(W): GP Reg R13 */
	ushort_t ifp_risc_r14;		/* R(W): GP Reg R14 */
	ushort_t ifp_risc_r15;		/* R(W): GP Reg R15 */

	ushort_t ifp_risc_psr;		/* R(W): Processor Status Reg  */
	ushort_t ifp_risc_ivr;		/* R(W): Interrupt Vector Reg  */
	ushort_t ifp_risc_pcr;		/* R(W): Processor Control Reg */

	ushort_t ifp_risc_rar0;		/* R(W): Ram Address Reg #0 */
	ushort_t ifp_risc_rar1;		/* R(W): Ram Address Reg #1 */

	ushort_t ifp_risc_lcr;		/* R(W): Loop Counter Reg */
	ushort_t ifp_risc_pc;		/* R:    Program Counter */
	ushort_t gap0;
	ushort_t ifp_risc_mtr;		/* R(W): Memory Timing Reg */
	ushort_t ifp_risc_sp;		/* R(W): Stack Pointer */
	ushort_t gap1[2];
	ushort_t ifp_risc_fbc;		/* R(W): FB Command */
	ushort_t ifp_risc_fbr0;		/* R   : FB FIFO Read0 */
	ushort_t ifp_risc_fbr1;		/* R   : FB FIFO Read1 */
	ushort_t ifp_risc_hrl;		/* (R) :  Hardware Rev Level Reg */

	/*
	 * Host Command and Control registers.
	 */
	ushort_t ifp_hccr;		/* RW: Host Command & Control Reg */
	ushort_t ifp_bp0;		/* RW: Processor Breakpoint Reg #0 */
	ushort_t ifp_bp1;		/* RW: Processor Breakpoint Reg #1 */
	ushort_t ifp_tcr;		/* W:  Test Control Reg */
	ushort_t ifp_tmr;		/* W:  Test Mode Reg */
	ushort_t ifp_rhis;		/* RW: RISC to Host Interrupt Status */

	uint16_t ifp_fill[0x0a];

	uint16_t ifp_22mailbox8;	/* R: Data from IFP */
	uint16_t ifp_22mailbox9;	/* R: Data from IFP */
	uint16_t ifp_22mailbox10;	/* R: Data from IFP */
	uint16_t ifp_22mailbox11;	/* R: Data from IFP */
	uint16_t ifp_22mailbox12;	/* R: Data from IFP */
	uint16_t ifp_22mailbox13;	/* R: Data from IFP */
	uint16_t ifp_22mailbox14;	/* R: Data from IFP */
	uint16_t ifp_22mailbox15;	/* R: Data from IFP */
	uint16_t ifp_22mailbox16;	/* R: Data from IFP */
	uint16_t ifp_22mailbox17;	/* R: Data from IFP */
	uint16_t ifp_22mailbox18;	/* R: Data from IFP */
	uint16_t ifp_22mailbox19;	/* R: Data from IFP */
	uint16_t ifp_22mailbox20;	/* R: Data from IFP */
	uint16_t ifp_22mailbox21;	/* R: Data from IFP */
	uint16_t ifp_22mailbox22;	/* R: Data from IFP */
	uint16_t ifp_22mailbox23;	/* R: Data from IFP */
};

struct ifp_fb_regs {
	/*
	 * Frame Buffer registers.
	 */
	ushort_t ifp_fb_command;		/* R(W): Frame buffer command */
	ushort_t ifp_fb_rd_status;	/* R   : RD FIFO Status */
	ushort_t ifp_fb_rnd_status;	/* R   : RND FIFO Status */
	ushort_t ifp_fb_td_status;	/* R   : TD FIFO Status */
	ushort_t ifp_fb_rd_frame_count;	/* RW  : RD FIFO Frame Count */
	ushort_t ifp_fb_rnd_frame_count; /* RW  : RND FIFO Frame Count */
	ushort_t ifp_fb_td_frame_count;	/* RW  : TD FIFO Frame Count */
	ushort_t ifp_fb_intr_status;	/* R   : FB Interrupt Status */
	ushort_t ifp_fb_read_ptr;	/* RW  : FIFO Read Pointer */
	ushort_t ifp_fb_write_ptr;	/* RW  : FIFO Write Pointer */
	ushort_t ifp_fb_unload_data;	/* R   : Unload FIFO Data */
	ushort_t ifp_fb_load_data;	/* W   : Load FIFO Data */
	ushort_t ifp_fb_error_status;	/* RW  : FB Error Status */
	ushort_t ifp_fb_test_mode;	/* W   : Test Mode */
	ushort_t ifp_fb_reserved0;
	ushort_t ifp_fb_hrl;		/* (R):  Hardware Rev Level */
};

struct ifp_fpm0_regs {
	/*
	 * Fibre Protocol Module registers - bank 0
	 */
	ushort_t ifp_fpm0_part_id;	/* R:    Part ID Code */
	ushort_t ifp_fpm0_config;	/* R(W): Configuration Register */
	ushort_t ifp_fpm0_reserved0;
	ushort_t ifp_fpm0_validity_config; /* R(W): Validity Config Register */
	ushort_t ifp_fpm0_reserved1;
	ushort_t ifp_fpm0_xfer_control;	/* R(W): Transfer Control */
	ushort_t ifp_fpm0_reserved2[2];
	ushort_t ifp_fpm0_status;	/* R: Status */
	ushort_t ifp_fpm0_int_status;	/* R(W): Interrupt Status */
	ushort_t ifp_fpm0_int_enable;	/* R(W): Interrupt Enable */
	ushort_t ifp_fpm0_diag_config;	/* R(W): Diagnostic Configuration */
	ushort_t ifp_fpm0_loop_address;	/* R(W): Loop Physical Address */
	ushort_t ifp_fpm0_reserved3[3];
	ushort_t ifp_fpm0_li_control;	/* R(W): LI Control */
	ushort_t ifp_fpm0_li_status;	/* R: LI Status */
	ushort_t ifp_fpm0_tx_lip_type;	/* R(W): Transmit LIP Type */
	ushort_t ifp_fpm0_rx_lip_type;	/* R: Receive LIP Type */
	ushort_t ifp_fpm0_reserved4[4];
	ushort_t ifp_fpm0_td_rctl_d_id_hi; /* R(W):TDR_CT(15-8)/D_IDHigh(7-0) */
	ushort_t ifp_fpm0_td_d_id_lo;	/* R(W): TD D_ID Low */
	ushort_t ifp_fpm0_td_csctl;	/* R(W):TD CS_CT(15-8)/S_ID High(7-0) */
	ushort_t ifp_fpm0_td_sid_lo;	/* R(W): TD S_ID Low */
	ushort_t ifp_fpm0_td_type;	/* R(W):TD TYPE(15-8)/F_CTL High(7-0) */
	ushort_t ifp_fpm0_td_fctl_lo;	/* R(W): TD F_CTL Low */
	ushort_t ifp_fpm0_td_seq_id;	/* R(W): TD SEQ_ID(15-8)/DF_CTL */
	ushort_t ifp_fpm0_td_seq_cnt;	/* R(W): TD SEQ_CNT */
	ushort_t ifp_fpm0_td_ox_id;	/* R(W): TD OX_ID */
	ushort_t ifp_fpm0_td_rx_id;	/* R(W): TD RX_ID */
	ushort_t ifp_fpm0_off_hi;	/* R(W): TD Relative Offset High Ctr */
	ushort_t ifp_fpm0_off_lo;	/* R(W): TD Relative Offset Low Ctr */
	ushort_t ifp_fpm0_reserved5[2];
	ushort_t ifp_fpm0_ts_dest;	/* R(W): TS Physical Destination Addr */
	ushort_t ifp_fpm0_td_dest;	/* R(W): TD Physical Destination Addr */
	ushort_t ifp_fpm0_ts_eof_delim;	/* R(W): TS EOF Frame Delimiters */
	ushort_t ifp_fpm0_td_eof_delim;	/* R(W): TD EOF Frame Delimiters */
	ushort_t ifp_fpm0_reserved6[6];
	ushort_t ifp_fpm0_ts_xfer_ctr;	/* R(W): TS Transfer Counter */
	ushort_t ifp_fpm0_td_xfer_ctr_hi; /* R(W): TD Transfer Counter High */
	ushort_t ifp_fpm0_td_xfer_ctr_lo; /* R(W): TD Transfer Counter Low */
	ushort_t ifp_fpm0_td_max_frm_len; /* R(W): TD Maximum Frame Length */
	ushort_t ifp_fpm0_td_frm_len_ctr; /* R   : TD Frame Length Counter */
	ushort_t ifp_fpm0_td_data_thresh; /* R(W): TD Data Threshold */
	ushort_t ifp_fpm0_reserved7[2];
	ushort_t ifp_fpm0_max_bb_credit;	/* R(W): Maximum BB_Credit */
	ushort_t ifp_fpm0_rx_d_id_hi;	/* R(W): Receive D_ID High */
	ushort_t ifp_fpm0_rx_d_id_lo;	/* R(W): Receive D_ID Low */
	ushort_t ifp_fpm0_rnd_max_len;	/* R(W): RND Maximum Frame Length */
	ushort_t ifp_fpm0_rd_max_len;	/* R(W): RD Maximum Frame Length */
};

struct ifp_fpm1_regs {
	/*
	 * Fibre Protocol Module registers - bank 1
	 */
	ushort_t ifp_fpm1_control1;	/* R(W): Manual Control */
	ushort_t ifp_fpm1_status1;	/* R   : Manual Status 1 */
	ushort_t ifp_fpm1_status2; 	/* R   : Manual Status 2 */
	ushort_t ifp_fpm1_ctr_control;	/* R(W): Counter Control */
	ushort_t ifp_fpm1_reserved0[4];
	ushort_t ifp_fpm1_rx_rctl_d_id_hi; /* R:Rx R_CTL(15-8)/D_ID High(7-0) */
	ushort_t ifp_fpm1_rx_d_id_lo;	/* R   : Rx D_ID Low */
	ushort_t ifp_fpm1_rx_csctl;	/* R  :Rx CS_CTL(15-8)/S_ID High(7-0) */
	ushort_t ifp_fpm1_rx_sid_lo;	/* R   : Rx S_ID Low */
	ushort_t ifp_fpm1_rx_type;	/* R   :Rx TYPE(15-8)/F_CTL High(7-0) */
	ushort_t ifp_fpm1_rx_fctl_lo;	/* R   : Rx F_CTL Low */
	ushort_t ifp_fpm1_rx_seq_id;	/* R   : Rx SEQ_ID(15-8)/DF_CTL */
	ushort_t ifp_fpm1_rx_seq_cnt;	/* R   : Rx SEQ_CNT */
	ushort_t ifp_fpm1_rx_ox_id;	/* R   : Rx OX_ID */
	ushort_t ifp_fpm1_rx_rx_id;	/* R   : Rx RX_ID */
	ushort_t ifp_fpm1_param_hi;	/* R   : Received Parameter High */
	ushort_t ifp_fpm1_param_lo;	/* R   : Received Parameter Low */
	ushort_t ifp_fpm1_reserved1[4];
	ushort_t ifp_fpm1_rd_frm_len_ctr; /* R   : Rx Frame Length Counter */
	ushort_t ifp_fpm1_rd_seq_cnt_ctr; /* R(W): Rx SEQ_CNT Count Counter */
	ushort_t ifp_fpm1_rd_sid_lo;	/* R(W): RD S_ID Low */
	ushort_t ifp_fpm1_rd_sid_hi;	/* R(W): RD S_ID High */
	ushort_t ifp_fpm1_rd_seq_id;	/* R(W): RD SEQ_ID(15-8)/DF_CTL */
	ushort_t ifp_fpm1_rd_ox_id;	/* R(W): RD OX_ID */
	ushort_t ifp_fpm1_reserved2[2];
	ushort_t ifp_fpm1_tx_r_rdy_ctr;	/* R   : Transmitted R_RDY Counter */
	ushort_t ifp_fpm1_rx_r_rdy_ctr;	/* R   : Received R_RDY Counter */
};

struct ifp_biu_regs {
	/*
	 * Bus interface registers.
	 */
	ushort_t ifp_bus_flash_addr;	/* RW:   Flash BIOS address */
	ushort_t ifp_bus_flash_data;	/* RW:   Flash BIOS data */
	ushort_t ifp_bus_reserved;
	ushort_t ifp_bus_icsr;		/* RW:   ISP Control/Status */
	ushort_t ifp_bus_icr;		/* RW:   ISP to PCI Interrupt Control */
	ushort_t ifp_bus_isr;		/* R:    ISP to PCI Interrupt Status */
	ushort_t ifp_bus_sema;		/* RW:   Bus Semaphore */
	ushort_t ifp_pci_nvram;		/* RW:   PCI NVRAM */

	/*
	 * Mailbox registers.
	 */
	ushort_t ifp_mailbox0;		/* R: Data from IFP */
					/* W: Data to IFP */
	ushort_t ifp_mailbox1;		/* R: Data from IFP */
					/* W: Data to IFP */
	ushort_t ifp_mailbox2;		/* R: Data from IFP */
					/* W: Data to IFP */
	ushort_t ifp_mailbox3;		/* R: Data from IFP */
					/* W: Data to IFP */
	ushort_t ifp_mailbox4;		/* R: Data from IFP */
					/* W: Data to IFP */
	ushort_t ifp_mailbox5;		/* R: Data from IFP */
					/* W: Data to IFP */
	ushort_t ifp_pci_mailbox6;	/* R: Data from IFP */
					/* W: Data to IFP */
	ushort_t ifp_pci_mailbox7;	/* R: Data from IFP */
					/* W: Data to IFP */
	/*
	 * Command DMA Channel registers.
	 */
	ushort_t ifp_cdma_control;	/* R(W): DMA Control */
	ushort_t ifp_cdma_status; 	/* R:    DMA Status */
	ushort_t ifp_cdma_gap0[2];
	ushort_t ifp_cdma_fifo_port;	/* RW:   DMA FIFO read/wrie port */
	ushort_t ifp_cdma_gap1[3];
	ushort_t ifp_cdma_count;		/* RW: DMA Transfer Count */
	ushort_t ifp_cdma_gap2[3];
	ushort_t ifp_cdma_addr0;		/* R(W): DMA Address, Word 0 */
	ushort_t ifp_cdma_addr1;		/* R(W): DMA Address, Word 1 */
	ushort_t ifp_pci_cdma_addr2;	/* R(W): DMA Address, Word 2 */
	ushort_t ifp_pci_cdma_addr3;	/* R(W): DMA Address, Word 3 */

	/*
	 * Transmit DMA Channel registers.
	 */
	ushort_t ifp_tdma_control;	/* R(W): DMA Control */
	ushort_t ifp_tdma_status;	/* R:    DMA Status */
	ushort_t ifp_tdma_gap0[2];
	ushort_t ifp_tdma_frame_size;	/* R(W): Frame byte size */
	ushort_t ifp_tdma_frame_counter; /* R(W): Frame byte counter */
	ushort_t ifp_tdma_seq_count_lo;	/* R(W): Sequence byte counter 0 */
	ushort_t ifp_tdma_seq_count_hi;	/* R(W): Sequence byte counter 1 */
	ushort_t ifp_tdma_count_lo;	/* R(W): DMA Transfer Count, Low */
	ushort_t ifp_tdma_count_hi;	/* R(W): DMA Transfer Count, High */
	ushort_t ifp_tdma_gap1[2];
	ushort_t ifp_tdma_addr0;	/* R(W): DMA Address, Word 0 */
	ushort_t ifp_tdma_addr1;	/* R(W): DMA Address, Word 1 */
	ushort_t ifp_pci_tdma_addr2;	/* R(W): DMA Address, Word 2 */
	ushort_t ifp_pci_tdma_addr3;	/* R(W): DMA Address, Word 3 */

	/*
	 * Receive DMA Channel registers.
	 */
	ushort_t ifp_rdma_control;	/* R(W): DMA Control */
	ushort_t ifp_rdma_status;	/* R:    DMA Status */
	ushort_t ifp_rdma_rds_lo;	/* R:    RDS low register */
	ushort_t ifp_rdma_rds_hi;	/* R(W): RDS hi reg/frame err enable */
	ushort_t ifp_rdma_gap0;
	ushort_t ifp_rdma_payload_count; /* R(W): Payload byte counter */
	ushort_t ifp_rdma_gap1[2];
	ushort_t ifp_rdma_count_lo;	/* R(W): DMA Transfer Count, Low */
	ushort_t ifp_rdma_count_hi;	/* R(W): DMA Transfer Count, High */
	ushort_t ifp_rdma_gap2[2];
	ushort_t ifp_rdma_addr0;	/* R(W): DMA Address, Word 0 */
	ushort_t ifp_rdma_addr1;	/* R(W): DMA Address, Word 1 */
	ushort_t ifp_pci_rdma_addr2;	/* R(W): DMA Address, Word 2 */
	ushort_t ifp_pci_rdma_addr3;	/* R(W): DMA Address, Word 3 */

	union {
		struct	ifp_risc_regs	__risc_regs;
		struct	ifp_fb_regs	__fb_regs;
		struct	ifp_fpm0_regs	__fpm0_regs;
		struct	ifp_fpm1_regs	__fpm1_regs;
	} ifp_un;
#define	ifp_risc_reg		ifp_un.__risc_regs
#define	ifp_pci_fb_regs		ifp_un.__fb_regs
#define	ifp_pci_fpm0_regs	ifp_un.__fpm0_regs
#define	ifp_pci_fpm1_regs	ifp_un.__fpm1_regs
};

/*
 * Register number - this determines the register space where the
 * registers are accessed from.
 */
#define	IFP_PCI_REG_NUMBER	2	/* Access registers in PCI I/O    */
					/* address space. Numering is:	  */
					/*  0 - configuration space	  */
					/*  1 - I/O space		  */
					/*  2 - memory space		  */

/*
 * Defines for the PCI Bus Interface Registers.
 */
		/* Flash BIOS Data Register */
#define	IFP_BUS_FLASH_BIOS_DATA_MASK	0x00FF	/* Flash BIOS Data */

		/* BUS Control/Status REGISTER */
#define	IFP_BUS_ICSR_MODULE_SELECT	0x0030	/* Register bank selection */
#define	IFP_BUS_ICSR_FLASH_UP_BANK	0x0008	/* Upper 64K Flash bank */
#define	IFP_PCI_ICSR_64BIT_SLOT		0x0004	/* PCI 64-bit Bus Slot */
#define	IFP_BUS_ICSR_FLASH_ENABLE	0x0002	/* Enable Flash BIOS */
#define	IFP_BUS_ICSR_SOFT_RESET		0x0001
						/*
						 * Force soft reset of ifp,
						 * ISP clears bit 0 when done
						 */

		/* Register bank selection values */
#define	IFP_MODULE_SELECT_RISC_REGS	0	/* Select RISC registers */
#define	IFP_MODULE_SELECT_FB_REGS	1	/* Select frame buffer regs */
#define	IFP_MODULE_SELECT_FPM0_REGS	2	/* Select FPM bank 0	*/
#define	IFP_MODULE_SELECT_FPM1_REGS	3	/* Select FPM bank 1	*/

		/* PCI CONTROL REGISTER */
#define	IFP_BUS_ICR_ENABLE_INTS		0x8000	/* Global enable all inter */
#define	IFP_BUS_ICR_ENABLE_FPM_INT	0x0020	/* Enable FPM interrupts */
#define	IFP_BUS_ICR_ENABLE_FB_INT	0x0010	/* Enable Frame Buffer ints */
#define	IFP_BUS_ICR_ENABLE_RISC_INT	0x0008	/* Enable RISC interrupt */
#define	IFP_BUS_ICR_ENABLE_CDMA_INT	0x0004	/* Enable Command DMA ints */
#define	IFP_BUS_ICR_ENABLE_RDMA_INT	0x0002	/* Enable Rx DMA interrupts */
#define	IFP_BUS_ICR_ENABLE_TDMA_INT	0x0001	/* Enable Tx DMA interrupts */
#define	IFP_BUS_ICR_DISABLE_INTS	0x0000	/* Global Disable all inter */

		/* PCI STATUS REGISTER */
#define	ISP_BUS_ISR_INT_PENDING		0x8000	/* Global interrupt pending */
#define	IFP_BUS_ISR_FPM_INT		0x0020	/* FPM interrupt pending */
#define	IFP_BUS_ISR_FB_INT		0x0010	/* Frame buffer int pending */
#define	IFP_BUS_ISR_RISC_INT		0x0008	/* Risc interrupt pending */
#define	IFP_BUS_ISR_CDMA_INT		0x0004	/* CDMA interrupt pending */
#define	IFP_BUS_ISR_RDMA_INT		0x0002	/* Rx DMA interrupt pending */
#define	IFP_BUS_ISR_TDMA_INT		0x0001	/* Tx DMA interrupt pending */

		/* BUS SEMAPHORE REGISTER */
#define	IFP_BUS_SEMA_STATUS		0x0002	/* Semaphore Status Bit */
#define	IFP_BUS_SEMA_LOCK  		0x0001	/* Semaphore Lock Bit */

		/* ISP NVRAM Interface REGISTER */
#define	IFP_PCI_NVRAM_DATA_IN		0x0008	/* NVRAM Data In */
#define	IFP_PCI_NVRAM_DATA_OUT 		0x0004	/* NVRAM Data Out */
#define	IFP_PCI_NVRAM_CHIP_SELECT	0x0002	/* NVRAM Chip Select */
#define	IFP_PCI_NVRAM_CLOCK 		0x0001	/* NVRAM Clock */

/*
 * Defines for the Command DMA Channel Registers.
 *
 * These defines are not used during normal operation, execpt possibly
 * to read the status of the DMA registers.
 */

		/* Command DMA CONTROL REGISTER */
#define	IFP_CDMA_CON_DMA_DIRECTION	0x0040
						/*
						 * Set DMA direction;
						 * 0 - DMA FIFO to host,
						 * 1 - Host to DMA FIFO
						 */
#define	IFP_CDMA_CON_CLEAR_FIFO		0x0020 	/* Clear FIFO - resets regs */
#define	IFP_CDMA_CON_CLEAR_DMA_CHAN	0x0004	/* Clear DMA Chan-clears stat */
#define	IFP_CDMA_CON_RESET_INT		0x0002	/* Clear DMA interrupt */
#define	IFP_CDMA_CON_STROBE		0x0001	/* Start DMA transfer */

		/* Command DMA Channel Status Register */

#define	IFP_PCI_CDMA_STATUS_INTERRUPT	0x8000	/* DMA Interrupt Request */
#define	IFP_PCI_CDMA_STATUS_CHAN_MASK	0x6000	/* Channel status mask */
#define	IFP_PCI_CDMA_STATUS_RETRY_STAT	0x1000	/* Retry status */
#define	IFP_PCI_CDMA_STATUS_MASTER_ABRT	0x0400	/* PCI Master Abort status */
#define	IFP_PCI_CDMA_STATUS_TARGET_ABRT	0x0200	/* Received Target Abort */
#define	IFP_PCI_CDMA_STATUS_BUS_PARITY	0x0100	/* Parity Error on bus */
#define	IFP_PCI_CDMA_STATUS_DIRECTION	0x0040	/* DMA Direction */
#define	IFP_PCI_CDMA_STATUS_CLR_PEND	0x0020	/* DMA clear pending */
#define	IFP_PCI_CDMA_STATUS_TERM_COUNT	0x0004	/* DMA Transfer Completed */
#define	IFP_PCI_CDMA_STATUS_STROBE	0x0001	/* DMA Strobe */

		/* Channel Status */
#define	IFP_PCI_CDMA_STATUS_IDLE	0x0	/* Idle (normal completion) */
#define	IFP_PCI_CDMA_STATUS_REQ_ACTIVE	0x1	/* PCI Bus is Being Requested */
#define	IFP_PCI_CDMA_STATUS_IN_PROGRESS	0x2	/* Transfer In Progress */
#define	IFP_PCI_CDMA_STATUS_ERROR	0x3	/* Terminated Due to Error */

		/* Command DMA Channel Transfer Byte Counter */
#define	IFP_PCI_CDMA_XFER_BYTE_CNT_MASK	0x000F	/* Transfer Byte Counter */

/*
 * Defines for the Transmit DMA Channel Registers.
 *
 * These defines are not used during normal operation, execpt possibly
 * to read the status of the DMA registers.
 */

		/* Transmit DMA CONTROL REGISTER */
#define	IFP_TDMA_CON_FAST_FORWARD	0x0008 	/* Incr Frame Count in FB */
#define	IFP_TDMA_CON_CLEAR_DMA_CHAN	0x0004	/* Clear DMA Chan-clears stat */
#define	IFP_TDMA_CON_RESET_INT		0x0002	/* Clear DMA interrupt */
#define	IFP_TDMA_CON_STROBE		0x0001	/* Start DMA transfer */

		/* Transmit DMA Status Register */
#define	IFP_PCI_TDMA_STATUS_INTERRUPT	0x8000	/* DMA Interrupt Request */
#define	IFP_PCI_TDMA_STATUS_CHAN_MASK	0x6000	/* Channel status mask */
#define	IFP_PCI_TDMA_STATUS_RETRY_STAT	0x1000	/* Retry status */
#define	IFP_PCI_TDMA_STATUS_MASTER_ABRT	0x0400	/* PCI Master Abort status */
#define	IFP_PCI_TDMA_STATUS_TARGET_ABRT	0x0200	/* Received Target Abort */
#define	IFP_PCI_TDMA_STATUS_BUS_PARITY	0x0100	/* Parity Error on bus */
#define	IFP_PCI_TDMA_STATUS_CLR_PEND	0x0020	/* DMA clear pending */
#define	IFP_PCI_TDMA_STATUS_SEQ_CNT_0	0x0008	/* Sequence Byte Count Zero */
#define	IFP_PCI_TDMA_STATUS_TERM_COUNT	0x0004	/* DMA Transfer Completed */
#define	IFP_PCI_TDMA_STATUS_PIPE_MASK	0x0003	/* DMA Pipeline status mask */

		/* Channel Status */
#define	IFP_PCI_TDMA_STATUS_IDLE	0x0	/* Idle (normal completion) */
#define	IFP_PCI_TDMA_STATUS_REQ_ACTIVE	0x1	/* PCI Bus is Being Requested */
#define	IFP_PCI_TDMA_STATUS_IN_PROGRESS	0x2	/* Transfer In Progress */
#define	IFP_PCI_TDMA_STATUS_ERROR	0x3	/* Terminated Due to Error */

		/* Pipeline Status */
#define	IFP_PCI_TDMA_PIPE_OVERRUN	0x0003	/* Pipeline overrun */
#define	IFP_PCI_TDMA_PIPE_FULL		0x0002	/* Both pipeline stages full */
#define	IFP_PCI_TDMA_PIPE_STAGE1	0x0001
						/*
						 * Pipeline stage 1 Loaded,
						 * stage 2 empty
						 */
#define	IFP_PCI_TDMA_PIPE_EMPTY		0x0000	/* Both pipeline stages empty */

/*
 * Defines for the Receive DMA Channel Registers.
 *
 * These defines are not used during normal operation, execpt possibly
 * to read the status of the DMA registers.
 */

		/* Receive DMA CONTROL REGISTER */
#define	IFP_RDMA_CON_NEW_CONTEXT	0x0010 	/* Xfer New Context to Host */
#define	IFP_RDMA_CON_FAST_FORWARD	0x0008 	/* Remove Extra Frms From FB */
#define	IFP_RDMA_CON_CLEAR_DMA_CHAN	0x0004	/* Clear DMA Chan-clears stat */
#define	IFP_RDMA_CON_RESET_INT		0x0002	/* Clear DMA interrupt */
#define	IFP_RDMA_CON_STROBE		0x0001	/* Start DMA transfer */

		/* Receive DMA Status Register */
#define	IFP_PCI_RDMA_STATUS_INTERRUPT	0x8000	/* DMA Interrupt Request */
#define	IFP_PCI_RDMA_STATUS_CHAN_MASK	0x6000	/* Channel status mask */
#define	IFP_PCI_RDMA_STATUS_RETRY_STAT	0x1000	/* Retry status */
#define	IFP_PCI_RDMA_STATUS_MISMATCH	0x0800	/* Context Mismatch Error */
#define	IFP_PCI_RDMA_STATUS_MASTER_ABRT	0x0400	/* PCI Master Abort status */
#define	IFP_PCI_RDMA_STATUS_TARGET_ABRT	0x0200	/* Received Target Abort */
#define	IFP_PCI_RDMA_STATUS_BUS_PARITY	0x0100	/* Parity Error on bus */
#define	IFP_PCI_RDMA_STATUS_FRAME_ERR	0x0080	/* Frame Error */
#define	IFP_PCI_RDMA_STATUS_CLR_PEND	0x0020	/* DMA clear pending */
#define	IFP_PCI_RDMA_STATUS_TERM_COUNT	0x0004	/* DMA Transfer Completed */
#define	IFP_PCI_RDMA_STATUS_PIPE_MASK	0x0003	/* DMA Pipeline status mask */

		/* Channel Status */
#define	IFP_PCI_RDMA_STATUS_IDLE	0x0	/* Idle (normal completion) */
#define	IFP_PCI_RDMA_STATUS_REQ_ACTIVE	0x1	/* PCI Bus is Being Requested */
#define	IFP_PCI_RDMA_STATUS_IN_PROGRESS	0x2	/* Transfer In Progress */
#define	IFP_PCI_RDMA_STATUS_ERROR	0x3	/* Terminated Due to Error */

		/* Pipeline Status */
#define	IFP_PCI_RDMA_PIPE_OVERRUN	0x0003	/* Pipeline overrun */
#define	IFP_PCI_RDMA_PIPE_FULL		0x0002	/* Both pipeline stages full */
#define	IFP_PCI_RDMA_PIPE_STAGE1	0x0001
						/*
						 * Pipeline stage 1 Loaded,
						 * stage 2 empty
						 */
#define	IFP_PCI_RDMA_PIPE_EMPTY		0x0000	/* Both pipeline stages empty */

/*
 * Defines for the FPM0 registers.
 */
		/* FPM0 Config REGISTER */
#define	IFP_CONF_ENABLE_OTHER_CLASS	0x1000	/* Enable Other Class */
#define	IFP_CONF_ENABLE_OTHER_PROTOCOL	0x0800	/* Enable Other Protocol */
#define	IFP_CONF_ENABLE_PUBLIC		0x0400	/* Enable Public */
#define	IFP_CONF_ENABLE_OUT_OF_ORDER	0x0200	/* Enable Out Of Order */
#define	IFP_CONF_ENABLE_TXMT_RETRY	0x0100	/* Enable Transmit Retry */
#define	IFP_CONF_ENABLE_DELIM_SUB	0x0080	/* Enable Delim Substitution */
#define	IFP_CONF_PORT_BYPASS_ALLOW	0x0040	/* Port Bypass Allow */
#define	IFP_CONF_HALF_DUPLEX		0x0020	/* Half Duplex */
#define	IFP_CONF_FAIRNESS		0x0010	/* Fairness */
#define	IFP_CONF_ENABLE_LOSS_OF_SYNC	0x0008	/* Enable Loss Of Sync Timer */
#define	IFP_CONF_DISCARD_ARB_PRIMITIVE	0x0004	/* Discard Arbitrate Prim */
#define	IFP_CONF_ALLOW_LOOP_OPENED	0x0002	/* Allow Loop To Be Opened */
#define	IFP_CONF_ENABLE_CRC_CHECK	0x0001	/* Enable CRC Check */


		/* FPM0 Validity Configuration REGISTER */
#define	IFP_VALID_CONF_ROUTING_CHECK	0x0080	/* Enable Routing Control Chk */
#define	IFP_VALID_CONF_DEST_ID_CHECK	0x0040	/* Enable Dest ID Valid Check */
#define	IFP_VALID_CONF_SRC_ID_CHECK	0x0020	/* Enable Source ID Check */
#define	IFP_VALID_CONF_TYPE_CHECK	0x0010	/* Enable data struct typ chk */
#define	IFP_VALID_CONF_SEQ_ID_CHECK	0x0008	/* Sequence ID Validity Check */
#define	IFP_VALID_CONF_DATA_CONTROL	0x0004	/* Data Field Ctrl Validity */
#define	IFP_VALID_CONF_SEQ_CNT_CHECK	0x0002	/* Sequence Cnt Validity Chk */
#define	IFP_VALID_CONF_OX_ID_CHECK	0x0001	/* Origniator Ex Id Validity */


		/* FPM0 Transfer Control Register */
#define	IFP_TX_CTL_TD_STROBE_WR		0x0010	/* TD Strobe Write Enable */
#define	IFP_TX_CTL_TS_STROBE_WR		0x0008	/* TS Strobe Write Enable */
#define	IFP_TX_CTL_TD_PASS_SEQ		0x0004	/* TD Pass Seq Initiative */
#define	IFP_TX_CTL_TD_STROBE		0x0002	/* TD Strobe */
#define	IFP_TX_CTL_TS_STROBE		0x0001	/* TS Strobe */

		/* FPM0 Transfer Control Register */
#define	IFP_STAT_TD_XFER_ACTIVE		0x0080	/* TD Transfer Acitve */
#define	IFP_STAT_TS_XFER_ACTIVE		0x0040	/* TS Transfer Acitve */
#define	IFP_STAT_PAUSE_COMPLETE		0x0020	/* Pause Complete */
#define	IFP_STAT_BUSY_COMPLETE		0x0010	/* Busy Complete */
#define	IFP_STAT_BYPASS_DISABLE		0x0008	/* Bypass Diable Status */
#define	IFP_STAT_LOSS_OF_WORD_SYNC	0x0004	/* Loss of Word Sync */
#define	IFP_STAT_OPENED_HALF_DUPLEX	0x0002	/* Opened Half Duplex */

		/* FPM0 Interrupt Status  Register */
#define	IFP_INT_OLS_PRIMITIVE_RECVD	0x1000	/* OLS Primitive Received */
#define	IFP_INT_NOS_PRIMITIVE_RECVD	0x0800	/* NOS Primitive Received */
#define	IFP_INT_LR_PRIMITIVE_RECVD	0x0400	/* LR Primitive Received */
#define	IFP_INT_LRR_PRIMITIVE_RECVD	0x0200	/* LRR Primitive received */
#define	IFP_INT_RRDY_PRIMITIVE_RECVD	0x0100	/* R_RDY Primitive Received */
#define	IFP_INT_OPEN_INIT_STATE_ENTERED	0x0020	/* OPEN INIT State Entered */
#define	IFP_INT_BYPASS_PRIMITIVE_RECVD	0x0010	/* Bypass Primitive received */
#define	IFP_INT_LOSS_OF_SYNC		0x0008	/* Loss Of Sync */
#define	IFP_INT_NOT_OPENED		0x0004	/* Not Opened */
#define	IFP_INT_TD_XFER_COMPLETE	0x0002	/* TD Transer Complete Intr */
#define	IFP_INT_TS_XFER_COMPLETE	0x0001	/* TS Transer Complete Intr */

		/* FPM0 Interrupt Enable  Register */
#define	IFP_IEN_OLS_PRIMITIVE_RECVD	0x1000	/* OLS Primitive Received */
#define	IFP_IEN_NOS_PRIMITIVE_RECVD	0x0800	/* NOS Primitive Received */
#define	IFP_IEN_LR_PRIMITIVE_RECVD	0x0400	/* LR Primitive Received */
#define	IFP_IEN_LRR_PRIMITIVE_RECVD	0x0200	/* LRR Primitive received */
#define	IFP_IEN_RRDY_PRIMITIVE_RECVD	0x0100	/* R_RDY Primitive Received */
#define	IFP_IEN_OPEN_INIT_STATE_ENTERED	0x0020	/* OPEN INIT State Entered */
#define	IFP_IEN_BYPASS_PRIMITIVE_RECVD	0x0010	/* Bypass Primitive received */
#define	IFP_IEN_LOSS_OF_SYNC		0x0008	/* Loss Of Sync */
#define	IFP_IEN_NOT_OPENED		0x0004	/* Not Opened */
#define	IFP_IEN_TD_XFER_COMPLETE	0x0002	/* TD Transer Complete Intr */
#define	IFP_IEN_TS_XFER_COMPLETE	0x0001	/* TS Transer Complete Intr */

		/* FPM0 Diagnostic Configuration Register */
#define	IFP_DIAG_BYPASS_ENABLE		0x0080	/* Port Bypass Enable */
#define	IFP_DIAG_BYPASS_DISABLE		0x0040	/* Port Bypass Disnable */
#define	IFP_DIAG_LOCK_TO_REFERENCE	0x0020	/* Lock To Reference */
#define	IFP_DIAG_ENABLE_WRAP		0x0010	/* Enable Wrap */
#define	IFP_DIAG_REQUEST_BUSY		0x0008	/* Request Busy */
#define	IFP_DIAG_REQUEST_PAUSE		0x0004	/* Request Pause */
#define	IFP_DIAG_HALF_WORD_SYNC_ENABLE	0x0002	/* Halfword Sync Enable */
#define	IFP_DIAG_FORCE_CRC_ERROR	0x0001	/* Force CRC Error */

		/* FPM0 Loop Physical Address Register */
#define	IFP_LOOP_PHYSICAL_ADDRESS_MASK	0x00FF	/* AL_PA Valid Bits */

		/* FPM0 LI Control Register */
#define	IFP_LI_CTL_REQ_INIT		0x0080	/* Request Initializing */
#define	IFP_LI_CTL_REQ_OPEN_INIT_IDLE	0x0040	/* Req Open Init Idle Phase */
#define	IFP_LI_CTL_REQ_OPEN_INIT_LISM	0x0020	/* Request Open Init LISM */
#define	IFP_LI_CTL_FORCE_LIP_TYPE	0x0010	/* Force LIP Type Enable */
#define	IFP_LI_CTL_ALLOW_FRAME_IN	0x0008	/* Processor Allow Frame In */
#define	IFP_LI_CTL_MASTER_AT_LOOP_INIT	0x0004	/* Master During Loop Init */
#define	IFP_LI_CTL_CONTINUOUS_PRIMITIVE	0x0002	/* Continuous Prim Enabled */
#define	IFP_LI_CTL_ONE_PRIMITIVE	0x0001	/* One Primitive Enabled */

		/* FPM0 LI Status Register - Current Loop Status */
#define	IFP_LI_STAT_INITING		0x0010	/* Initing Phase */
#define	IFP_LI_STAT_OPEN_INIT_RECVD	0x0008	/* Open Init Received Phase */
#define	IFP_LI_STAT_OPEN_INIT_IDLE	0x0004	/* Open Init Idle Phase */
#define	IFP_LI_STAT_OPEN_INIT_LISM	0x0002	/* Open Init LISM Phase */
#define	IFP_LI_STAT_OPEN_INIT_AL_PA	0x0001	/* Open Init AL_PA Phase */

		/* FPM0 Transmit LIP Type Register */
#define	IFP_TX_LIP_TYPE_BYTE3_MASK	0xFF00	/* Transmit LIP Type Byte 3 */
#define	IFP_TX_LIP_TYPE_BYTE4_MASK	0x00FF	/* Transmit LIP Type Byte 4 */

		/* FPM0 Receive LIP Type Register */
#define	IFP_RX_LIP_TYPE_BYTE3_MASK	0xFF00	/* Receive LIP Type Byte 3 */
#define	IFP_RX_LIP_TYPE_BYTE4_MASK	0x00FF	/* Receive LIP Type Byte 4 */

		/* FPM0 TD R_CTL/D_ID High Registers */
#define	IFP_TD_R_CTL_MASK		0xFF00	/* TD R_CTL */
#define	IFP_TD_D_ID_HI_MASK		0x00FF	/* Bits 23-16 of TD D_ID */

		/* FPM0 TD CS_CTL/S_ID High Registers */
#define	IFP_TD_CS_CTL_MASK		0xFF00	/* TD CS_CTL */
#define	IFP_TD_S_ID_HI_MASK		0x00FF	/* Bits 23-16 of TD S_ID */

		/* FPM0 TD TYPE/F_CTL High Registers */
#define	IFP_TD_TYPE_MASK		0xFF00	/* TD TYPE */
#define	IFP_TD_F_CTL_HI_MASK		0x00FF	/* Bits 23-16 of TD F_CTL */


		/* FPM0 TD SEQ_ID/DF_CTL Registers */
#define	IFP_TD_SEQ_ID_MASK		0xFF00	/* TD SEQ_ID */
#define	IFP_TD_DF_CTL_MASK		0x00FF	/* TD DF_CTL */

		/* FPM0 TS Physical Destination Address Register */
#define	IFP_TS_AL_PD_MASK		0x00FF	/* TS Physical Dest Address */

		/* FPM0 TD Physical Destination Address Register */
#define	IFP_TD_AL_PD_MASK		0x00FF	/* TD Physical Dest Address */

		/* FPM0 TS SOF Frame Delimiter Register */
#define	IFP_TS_SOF_MASK			0x00FF	/* TS SOF Frame Delimiter */

		/* FPM0 TD SOF Frame Delimiter Register */
#define	IFP_TD_SOF_MASK			0x00FF	/* TD SOF Frame Delimiter */

		/* FPM0 TS Transfer Counter Register */
#define	IFP_TS_XFER_COUNTER_MASK	0x0FFF	/* TS Transfer Counter */
						/* Bits 1-0 are always 0 */
		/* FPM0 TD Maximum Frame Length Register */
#define	IFP_TD_MAX_FRAME_LEN_MASK   	0x0FFF	/* TD Maximum Frame Length */
						/* Bits 2-0 are always 0 */
		/* FPM0 TD Frame Length Counter Register */
#define	IFP_TD_FRAME_LEN_COUNTER_MASK	0x0FFF	/* TD Frame Length Counter */

		/* FPM0 TD Data Threshold Register */
#define	IFP_TD_DATA_THRESHOLD_MASK	0x000F	/* TD Data Threshold */

		/* FPM0 TD Maximum BB_Credit Register */
#define	IFP_MAX_BB_CREDIT_MASK		0x000F	/* TD Max BB_Credit Control */

		/* FPM0 Receive D_ID High Register */
#define	IFP_RX_D_ID_HI_MASK		0x00FF	/* RX D_ID Bits 23-16 */

		/* FPM0 RND Maximum Frame Length Register */
#define	IFP_RND_MAX_FRAME_LEN_MASK	0x0FFF	/* RND Max Payload Frame Len */

		/* FPM0 RD Maximum Frame Length Register */
#define	IFP_RD_MAX_FRAME_LEN_MASK	0x1FFF	/* RD Max Frame Length */

/*
 * Defines for FPM1 Registers
 */
		/* FPM1 Manual Control Register */
#define	IFP_MAN_REQ_CLOSE		0x1000	/* Request Close */
#define	IFP_MAN_REQ_XFER		0x0800	/* Request Transfer */
#define	IFP_MAN_REQ_OPEN		0x0400	/* Request Open */
#define	IFP_MAN_REQ_ARBITRATE		0x0200	/* Request To Arbitrate */
#define	IFP_MAN_REQ_MONITORING		0x0100	/* Request Monitoring */
#define	IFP_MAN_REQ_OLD_PORT		0x0080	/* Request Old Port */
#define	IFP_MAN_SEND_OLS		0x0040	/* Send OLS Primitive */
#define	IFP_MAN_SEND_NOS		0x0020	/* Send NOS Primitive */
#define	IFP_MAN_SEND_LR			0x0010	/* Send LR Primitive */
#define	IFP_MAN_SEND_LRR		0x0008	/* Send LRR Primitive */
#define	IFP_MAN_SEND_R_RDY		0x0004	/* Send R_RDY Enable */
#define	IFP_MAN_R_RDY_COUNTER_DECR	0x0002	/* Block Rcvd R_RDY Ctr-- */
#define	IFP_MAN_DIS_HW_LOOP_REQUESTS	0x0001	/* Disable HW Loop Requests */

		/* FPM1 Manual Status 1 Register */
#define	IFP_STAT1_XFER			0x8000	/* Loop State Is Transfer */
#define	IFP_STAT1_OPEN			0x4000	/* Loop State Is Open */
#define	IFP_STAT1_OPENED		0x2000	/* Loop State Is Opened */
#define	IFP_STAT1_ARBITRATING		0x1000	/* Loop State Is Arbitrating */
#define	IFP_STAT1_MONTIORING		0x0800	/* Loop State Is Monitoring */
#define	IFP_STAT1_OLD_PORT		0x0400	/* Loop State Is Old Port */
#define	IFP_STAT1_TRANSMITTED_CLOSE	0x0200	/* State Is Transmitted Close */
#define	IFP_STAT1_RECEIVED_CLOSE	0x0100	/* State Is Received Close */
#define	IFP_STAT1_SOF_TYPE		0x0070	/* SOF Type of Last Frame Rxd */
#define	IFP_STAT1_OTHER_PROT		0x0004	/* Other Protocol Detected */
#define	IFP_STAT1_NEW_CONTEXT		0x0002	/* New Context Detected */
#define	IFP_STAT1_LAST_FRAME		0x0001	/* Last Frame Detected */

	/* SOF Types */
#define	IFP_SOF_TYPE_SOFn3		0x0	/* Normal Class 3 */
#define	IFP_SOF_TYPE_SOFi3		0x1	/* Initiate Class 3 */
#define	IFP_SOF_TYPE_SOFn2		0x2	/* Normal Class 2 */
#define	IFP_SOF_TYPE_SOFi2		0x3	/* Initiate Class 2 */
#define	IFP_SOF_TYPE_SOFn1		0x4	/* Normal Class 1 */
#define	IFP_SOF_TYPE_SOFi1		0x5	/* Initiate Class 1 */
#define	IFP_SOF_TYPE_SOFc1		0x6	/* Connect Class 1 */

		/* FPM1 Manual Status 2 Register */
#define	IFP_STAT2_INVALID_R_CTL		0x8000	/* Invalid R_CTL Detected */
#define	IFP_STAT2_INVALID_D_ID		0x4000	/* Invalid D_ID Detected */
#define	IFP_STAT2_INVALID_S_ID		0x2000	/* Invalid S_ID Detected */
#define	IFP_STAT2_INVALID_TYPE		0x1000	/* Invalid TYPE Detected */
#define	IFP_STAT2_INVALID_SEQ_ID	0x0800	/* Invalid SEQ_ID Detected */
#define	IFP_STAT2_INVALID_DF_CTL	0x0400	/* Invalid DF_CTL Detected */
#define	IFP_STAT2_INVALID_SEQ_CNT	0x0200	/* Invalid SEQ_CNT Detected */
#define	IFP_STAT2_INVALID_OX_ID		0x0100	/* Invalid OX_ID Detected */
#define	IFP_STAT2_RUNNING_DISPARITY_ERR	0x0080	/* Disparity Err On Last Frm */
#define	IFP_STAT2_CRC_ERROR		0x0040	/* CRC Error On Last Frame */
#define	IFP_STAT2_XFER_LEN_ERROR	0x0020	/* Xfer Len Err on Last Frame */
#define	IFP_STAT2_EOF_TYPE		0x0007	/* EOF Type Of Last Frame */

	/* EOF Types Of The Last Frame */
#define	IFP_EOF_TYPE_EOFn		0x0	/* Normal */
#define	IFP_EOF_TYPE_EOFt		0x1	/* Termintate */
#define	IFP_EOF_TYPE_EOFdt		0x2	/* Disconnect, Termintate */
#define	IFP_EOF_TYPE_EOFdti		0x3	/* Disc, Terminate, Invalid */
#define	IFP_EOF_TYPE_EOFni		0x4	/* Normal, Invalid */
#define	IFP_EOF_TYPE_EOFa		0x5	/* Abort */

		/* FPM1 Counter Control Register */
#define	IFP_CTR_RX_FRAME_LEN_COUNTER	0x0800	/* Rx Frame Length Counter */
#define	IFP_CTR_TX_R_RDY_COUNTER	0x0400	/* Rx R_RDY Counter */
#define	IFP_CTR_RX_R_RDY_COUNTER	0x0200	/* Tx R_RDY Counter */
#define	IFP_CTR_SEQ_COUNTER		0x0100	/* Sequence Counters */
#define	IFP_CTR_TD_REL_OFF_COUNTER	0x0080	/* TD Relative Offset Counter */
#define	IFP_CTR_TS_XFER_COUNTER		0x0040	/* TS Transfer Counter */
#define	IFP_CTR_TD_XFER_COUNTER		0x0020	/* TD Transfer Counter */
#define	IFP_CTR_TD_FRAME_LEN_COUNTER	0x0010	/* TD Frame Length Counter */
#define	IFP_CTR_DECREMENT_COUNTERS	0x0008	/* Decrement Accessed Cntrs */
#define	IFP_CTR_INCREMENT_COUNTERS	0x0004	/* Increment Accessed Cntrs */
#define	IFP_CTR_LOAD_COUNTERS		0x0002	/* Load Accessed Counters */
#define	IFP_CTR_DIAG_MODE_ENABLE	0x0001	/* Diagnostic Mode Enable */

		/* FPM1 Received R_CTL/D_ID High Registers */
#define	IFP_RX_R_CTL_MASK		0xFF00	/* Received R_CTL */
#define	IFP_RX_D_ID_HI_MASK		0x00FF	/* Bits 23-16 of Rcvd D_ID */

		/* FPM1 Received CS_CTL/S_ID High Registers */
#define	IFP_RX_CS_CTL_MASK		0xFF00	/* Received CS_CTL */
#define	IFP_RX_S_ID_HI_MASK		0x00FF	/* Bits 23-16 of Rcvd S_ID */

		/* FPM1 Received TYPE/F_CTL High Registers */
#define	IFP_RX_TYPE_MASK		0xFF00	/* Received TYPE */
#define	IFP_RX_F_CTL_HI_MASK		0x00FF	/* Bits 23-16 of Rcvd F_CTL */

		/* FPM1 Received SEQ_ID/DF_CTL Registers */
#define	IFP_RX_SEQ_ID_MASK		0xFF00	/* Received SEQ_ID */
#define	IFP_RX_DF_CTL_MASK		0x00FF	/* Received DF_CTL */

		/* FPM1 Receive Frame Length Counter Register */
#define	IFP_RX_FRAME_LEN_COUNTER_MASK	0x1FFF	/* Receive Frame Length Cntr */

		/* FPM1 RD S_ID High Register */
#define	IFP_RD_S_ID_HI_MASK		0x00FF	/* RD S_ID High (23-16) */

		/* FPM1 RD SEQ_ID/DF_CTL Registers */
#define	IFP_RD_SEQ_ID_MASK		0xFF00	/* RD SEQ_ID */
#define	IFP_RD_DF_CTL_MASK		0x00FF	/* RD DF_CTL */

		/* FPM1 Transmitted R_RDY Counter Register */
#define	IFP_TX_R_RDY_COUNTER_MASK	0x000F	/* Transmit R_RDY Counter */

		/* FPM1 Received R_RDY Counter Register */
#define	IFP_RX_R_RDY_COUNTER_MASK	0x1FFF	/* Received R_RDY Counter */

/*
 * Defines for the RISC CPU registers.
 */
		/* PROCESSOR STATUS REGISTER */
#define	IFP_RISC_PSR_FORCE_TRUE		0x8000
#define	IFP_RISC_PSR_LOOP_COUNT_DONE	0x4000
#define	IFP_RISC_PSR_RISC_INT		0x2000
#define	IFP_RISC_PSR_TIMER_ROLLOVER	0x1000
#define	IFP_RISC_PSR_ALU_OVERFLOW	0x0800
#define	IFP_RISC_PSR_ALU_MSB		0x0400
#define	IFP_RISC_PSR_ALU_CARRY		0x0200
#define	IFP_RISC_PSR_ALU_ZERO		0x0100
#define	IFP_RISC_PSR_DMA_INT		0x0010
#define	IFP_RISC_PSR_SXP_INT		0x0008
#define	IFP_RISC_PSR_HOST_INT		0x0004
#define	IFP_RISC_PSR_INT_PENDING	0x0002
#define	IFP_RISC_PSR_FORCE_FALSE  	0x0001

		/* PROCESSOR CONTROL REGISTER */
#define	IFP_RISC_PCR_NOP  		0x0000
#define	IFP_RISC_PCR_RESTORE_PCR  	0x1000

		/* MEMORY TIMING REGISTER */
#define	IFP_RISC_MTR_PAGE1_DEFAULT	0x1200	/* Page1 default rd/wr timing */
#define	IFP_RISC_MTR_PAGE0_DEFAULT	0x0012	/* Page0 default rd/wr timing */

/*
 * Defines for the Host Command and Control Register.
 */
					/* Command field defintions */
#define	IFP_HCCR_CMD_NOP		0x0000	/* NOP */
#define	IFP_HCCR_CMD_RESET		0x1000	/* Reset RISC */
#define	IFP_HCCR_CMD_PAUSE		0x2000	/* Pause RISC */
#define	IFP_HCCR_CMD_RELEASE		0x3000	/* Release Paused RISC */
#define	IFP_HCCR_CMD_STEP		0x4000	/* Single Step RISC */
#define	IFP_HCCR_CMD_SET_HOST_INT	0x5000	/* Set Host Interrupt */
#define	IFP_HCCR_CMD_CLEAR_HOST_INT	0x6000	/* Clear Host Interrupt */
#define	IFP_HCCR_CMD_CLEAR_RISC_INT	0x7000	/* Clear RISC interrupt */
#define	IFP_HCCR_CMD_BREAKPOINT		0x8000	/* Change breakpoint enables */
#define	IFP_PCI_HCCR_CMD_BIOS		0x9000	/* Write BIOS enable */
#define	IFP_PCI_HCCR_CMD_PARITY		0xA000	/* Write parity enable */
#define	IFP_PCI_HCCR_CMD_PARITY_ERR	0xE000	/* Generate parity error */
#define	IFP_HCCR_CMD_TEST_MODE		0xF000	/* Set Test Mode */

					/* Status bit defintions */
#define	IFP_PCI_HCCR_PARITY_ERROR	0x0800	/* RISC Data Parity Error */
#define	IFP_PCI_HCCR_PARITY_ENABLE_23	0x0400	/* Parity enable banks 2 & 3 */
#define	IFP_PCI_HCCR_PARITY_ENABLE_1	0x0200	/* Parity enable bank 1 */
#define	IFP_PCI_HCCR_PARITY_ENABLE_0	0x0100	/* Parity enable bank 0 */
#define	IFP_HCCR_HOST_INT		0x0080	/* R: Host interrupt set */
#define	IFP_HCCR_RESET			0x0040	/* R: RISC reset in progress */
#define	IFP_HCCR_PAUSE			0x0020	/* R: RISC paused */

					/* Breakpoint defintions */
#define	IFP_HCCR_BREAKPOINT_EXT		0x0010	/* Enable external breakpoint */
#define	IFP_HCCR_BREAKPOINT_1		0x0008	/* Enable breakpoint #1 */
#define	IFP_HCCR_BREAKPOINT_0		0x0004	/* Enable breakpoint #0 */
#define	IFP_PCI_HCCR_BREAKPOINT_INT	0x0002  /* Enable intr on breakpoint */

/*
 * Bus dma burst sizes
 */
#ifndef BURSTSIZE
#define	BURSTSIZE
#define	BURST1			0x01
#define	BURST2			0x02
#define	BURST4			0x04
#define	BURST8			0x08
#define	BURST16			0x10
#define	BURST32			0x20
#define	BURST64			0x40
#define	BURST128		0x80
#define	BURSTSIZE_MASK		0xff
#define	DEFAULT_BURSTSIZE	BURST16|BURST8|BURST4|BURST2|BURST1
#endif  /* BURSTSIZE */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_IFPREG_H */
