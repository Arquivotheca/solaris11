/*
 * Copyright (c) 1996, 2000, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_SCSI_ADAPTERS_ISPREG_H
#define	_SYS_SCSI_ADAPTERS_ISPREG_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 *
 * Hardware definitions for the Emulex ISP 1000 and 1040A chips.
 *
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
 * A BUS/bus in the define/variable name implies that it is
 * valid for both SBus and PCI. SBUS/sbus in the define/variable
 * name implies that it is valid only for SBus. PCI/pci in
 * the define/variable name implies that it is valid only
 * for PCI.
 */
/*
 * Bus Inteface Unit (BIU) registers are mapped at same address
 * for both Sbus and PCI.
 */
#define	ISP_BUS_BIU_REGS_OFF		0x0

struct isp_biu_regs {
	/*
	 * Bus interface registers.
	 */
	uint16_t isp_bus_id_lo;		/* R:    Hardwired Bus ID, Low */
	uint16_t isp_bus_id_hi;		/* R:    Hardwired Bus ID, High */
	uint16_t isp_bus_conf0;		/* R:    Bus Configuration #0 */
	uint16_t isp_bus_conf1;		/* RW:   Bus Configuration #1 */
	uint16_t isp_bus_icr;		/* RW:   Bus Interface Control */
	uint16_t isp_bus_isr;		/* R:    Bus Interface Status */
	uint16_t isp_bus_sema;		/* RW:   Bus Semaphore */
	uint16_t isp_pci_nvram;		/* RW:   PCI NVRAM */
	uint16_t gap0[0x8];


	/*
	 * Command DMA Channel registers.
	 */
	uint16_t isp_cdma_conf;		/* R(W): DMA Configuration */
	uint16_t isp_cdma_control;	/* R(W): DMA Control */
	uint16_t isp_cdma_status; 	/* R:    DMA Status */
	uint16_t isp_cdma_fifo_status;	/* R:    DMA FIFO Status */
	uint16_t isp_cdma_count;	/* R(W): DMA Transfer Count */
	uint16_t isp_cdma_reserved;
	uint16_t isp_cdma_addr0;	/* R(W): DMA Address, Word 0 */
	uint16_t isp_cdma_addr1;	/* R(W): DMA Address, Word 1 */
	uint16_t isp_pci_cdma_addr2;	/* R(W): DMA Address, Word 2 */
	uint16_t isp_pci_cdma_addr3;	/* R(W): DMA Address, Word 3 */
	uint16_t gap1[0x6];


	/*
	 * Data DMA Channel registers.
	 */
	uint16_t isp_dma_conf;		/* R(W): DMA Configuration */
	uint16_t isp_dma_control;	/* R(W): DMA Control */
	uint16_t isp_dma_status;	/* R:    DMA Status */
	uint16_t isp_dma_fifo_status;	/* R:	 DMA FIFO Status */
	uint16_t isp_dma_count_lo;	/* R(W): DMA Transfer Count, Low */
	uint16_t isp_dma_count_hi;	/* R(W): DMA Transfer Count, High */
	uint16_t isp_dma_addr0;		/* R(W): DMA Address, Word 0 */
	uint16_t isp_dma_addr1;		/* R(W): DMA Address, Word 1 */
	uint16_t isp_pci_dma_addr2;	/* R(W): DMA Address, Word 2 */
	uint16_t isp_pci_dma_addr3;	/* R(W): DMA Address, Word 3 */
	uint16_t gap2[0x6];


	/*
	 * Data DMA FIFO Channel registers.
	 */
	uint16_t isp_fifo_command;	/* RW: Command FIFO Access Port */
	uint16_t isp_fifo_data;		/* RW: Data FIFO Access Port */
};

#define	ISP_SBUS_MBOX_REGS_OFF		0x80
#define	ISP_PCI_MBOX_REGS_OFF		0x70

struct isp_mbox_regs {
	/*
	 * Mailbox registers.
	 */
	uint16_t isp_mailbox0;		/* R: Data from ISP */
					/* W: Data to ISP */
	uint16_t isp_mailbox1;		/* R: Data from ISP */
					/* W: Data to ISP */
	uint16_t isp_mailbox2;		/* R: Data from ISP */
					/* W: Data to ISP */
	uint16_t isp_mailbox3;		/* R: Data from ISP */
					/* W: Data to ISP */
	uint16_t isp_mailbox4;		/* R: Data from ISP */
					/* W: Data to ISP */
	uint16_t isp_mailbox5;		/* R: Data from ISP */
					/* W: Data to ISP */
	uint16_t isp_pci_mailbox6;	/* R: Data from ISP */
					/* W: Data to ISP */
	uint16_t isp_pci_mailbox7;	/* R: Data from ISP */
					/* W: Data to ISP */
};

#define	ISP_SBUS_SXP_REGS_OFF		0x200
#define	ISP_PCI_SXP_REGS_OFF		0x80

struct isp_sxp_regs {
	/*
	 * SXP (SCSI Executive Proccessor) registers.
	 */
	uint16_t isp_sxp_part_id;	/* R:    Part ID Code */
	uint16_t isp_sxp_config1;	/* R(W): Configuration Reg #1 */
	uint16_t isp_sxp_config2;	/* R(W): Configuration Reg #2 */
	uint16_t isp_sxp_config3;	/* R(W): Configuration Reg #2 */
	uint16_t isp_sxp_reserved0[2];

	uint16_t isp_sxp_instruction;	/* R(W): Instruction Pointer */
	uint16_t isp_sxp_reserved1;
	uint16_t isp_sxp_return_addr;	/* R(W): Return Address */
	uint16_t isp_sxp_reserved2;
	uint16_t isp_sxp_command;	/* R(W): Command */
	uint16_t isp_sxp_reserved3;

	uint16_t isp_sxp_interrupt;	/* R:    Interrupt */
	uint16_t isp_sxp_reserved4;
	uint16_t isp_sxp_sequence;	/* R(W): Sequence */
	uint16_t isp_sxp_gross_err;	/* R:    Gross Error */
	uint16_t isp_sxp_execption;	/* R(W): Exception Enable */
	uint16_t isp_sxp_reserved5;
	uint16_t isp_sxp_override;	/* R(W): Override */
	uint16_t isp_sxp_reserved6;

	uint16_t isp_sxp_literal_base;	/* R(W): Literal Base */
	uint16_t isp_sxp_reserved7;

	uint16_t isp_sxp_user_flags;	/* R(W): User Flags */
	uint16_t isp_sxp_reserved8;
	uint16_t isp_sxp_user_except;	/* R(W): User Exception */
	uint16_t isp_sxp_reserved9;
	uint16_t isp_sxp_breakpoint;	/* R(W): Breakpoint */
	uint16_t isp_sxp_reserved10[5];

	uint16_t isp_sxp_scsi_id;	/* R(W): SCSI ID */
	uint16_t isp_sxp_dev_config1;	/* R(W): Device Config Reg #1 */
	uint16_t isp_sxp_dev_config2;	/* R(W): Device Config Reg #2 */
	uint16_t isp_sxp_reserved11;
	uint16_t isp_sxp_phase_pointer;	/* R(W): SCSI Phase Pointer */
	uint16_t isp_sxp_reserved12;

	uint16_t isp_sxp_buf_pointer;	/* R(W): SCSI Buffer Pointer */
	uint16_t isp_sxp_reserved13;
	uint16_t isp_sxp_buf_counter;	/* R(W): SCSI Buffer Counter */
	uint16_t isp_sxp_buffer;		/* R(W): SCSI Buffer */
	uint16_t isp_sxp_buf_byte;	/* R(W): SCSI Buffer Byte */
	uint16_t isp_sxp_buf_word;	/* R(W): SCSI Buffer Word */
	uint16_t isp_sxp_buf_word_tran;	/* R(W): SCSI Buffer Word Translate */

	uint16_t isp_sxp_fifo;		/* R(W): SCSI FIFO */
	uint16_t isp_sxp_fifo_status;	/* R(W): SCSI FIFO Status */
	uint16_t isp_sxp_fifo_top;	/* R(W): SCSI FIFO Top Residue */
	uint16_t isp_sxp_fifo_bottom;	/* R(W): SCSI FIFO Bottom Residue */
	uint16_t isp_sxp_reserved15;

	uint16_t isp_sxp_tran_reg;		/* R(W): SCSI Transferr Reg */
	uint16_t isp_sxp_reserved16;
	uint16_t isp_sxp_tran_count_lo;		/* R(W): SCSI Trans Count */
	uint16_t isp_sxp_tran_count_hi;		/* R(W): SCSI Trans Count */
	uint16_t isp_sxp_tran_counter_lo;	/* R(W): SCSI Trans Counter */
	uint16_t isp_sxp_tran_counter_hi;	/* R(W): SCSI Trans Counter */

	uint16_t isp_sxp_arb_data;		/* R:    SCSI Arb Data */
	uint16_t isp_sxp_pins_control;		/* R(W): SCSI Control Pins */
	uint16_t isp_sxp_pins_data;		/* R(W): SCSI Data Pins */
	uint16_t isp_sxp_pins_diff;		/* R(W): SCSI Diff Pins */
};

#define	ISP_SBUS_RISC_REGS_OFF		0x400
#define	ISP_PCI_RISC_REGS_OFF		0x80

struct isp_risc_regs {
	/*
	 * RISC registers.
	 */
	uint16_t isp_risc_acc;		/* R(W): Accumulator */
	uint16_t isp_risc_r1;		/* R(W): GP Reg R1  */
	uint16_t isp_risc_r2;		/* R(W): GP Reg R2  */
	uint16_t isp_risc_r3;		/* R(W): GP Reg R3  */
	uint16_t isp_risc_r4;		/* R(W): GP Reg R4  */
	uint16_t isp_risc_r5;		/* R(W): GP Reg R5  */
	uint16_t isp_risc_r6;		/* R(W): GP Reg R6  */
	uint16_t isp_risc_r7;		/* R(W): GP Reg R7  */
	uint16_t isp_risc_r8;		/* R(W): GP Reg R8  */
	uint16_t isp_risc_r9;		/* R(W): GP Reg R9  */
	uint16_t isp_risc_r10;		/* R(W): GP Reg R10 */
	uint16_t isp_risc_r11;		/* R(W): GP Reg R11 */
	uint16_t isp_risc_r12;		/* R(W): GP Reg R12 */
	uint16_t isp_risc_r13;		/* R(W): GP Reg R13 */
	uint16_t isp_risc_r14;		/* R(W): GP Reg R14 */
	uint16_t isp_risc_r15;		/* R(W): GP Reg R15 */

	uint16_t isp_risc_psr;		/* R(W): Processor Status Reg  */
	uint16_t isp_risc_ivr;		/* R(W): Interrupt Vector Reg  */
	uint16_t isp_risc_pcr;		/* R(W): Processor Control Reg */

	uint16_t isp_risc_rar0;		/* R(W): Ram Address Reg #0 */
	uint16_t isp_risc_rar1;		/* R(W): Ram Address Reg #1 */

	uint16_t isp_risc_lcr;		/* R(W): Loop Counter Reg */
	uint16_t isp_risc_pc;		/* R:    Program Counter */
	uint16_t isp_risc_mtr;		/* R(W): Memory Timing Reg */
	uint16_t isp_risc_emb;		/* R(W): External Memory Boundary Reg */
	uint16_t isp_risc_sp;		/* R(W): Stack Pointer */
	uint16_t gap[0x5];
	uint16_t isp_risc_hrl;		/* (R):  Hardware Rev Level Reg */


	/*
	 * Host Command and Control registers.
	 */
	uint16_t isp_hccr;		/* RW: Host Command & Control Reg */
	uint16_t isp_bp0;		/* RW: Processor Breakpoint Reg #0 */
	uint16_t isp_bp1;		/* RW: Processor Breakpoint Reg #1 */
	uint16_t isp_tcr;		/* W:  Test Control Reg */
	uint16_t isp_tmr;		/* W:  Test Mode Reg */
};

struct	isp_regs_off {
	offset_t isp_biu_regs_off;
	offset_t isp_mbox_regs_off;
	offset_t isp_sxp_regs_off;
	offset_t isp_risc_regs_off;
};

/*
 * Register number - this determines the register space where the
 * registers are accessed from.
 */
#define	ISP_SBUS_REG_NUMBER	0	/* SBus has only 1 register space */
#define	ISP_PCI_REG_NUMBER	2	/* Access registers in PCI memory */
					/* address space. Numering is:	  */
					/*  0 - configuration space	  */
					/*  1 - I/O space		  */
					/*  2 - memory space		  */


/*
 *
 * Defines for the Bus Interface Registers.
 *
 */
		/* BUS CONFIGURATION REGISTER #0 */
#define	ISP_BUS_CONF0_HW_MASK		0x000F	/* Hardware revision mask */


		/* BUS CONFIGURATION REGISTER #1 */
#define	ISP_SBUS_CONF1_PARITY		0x0100 	/* Enable parity checking */
#define	ISP_SBUS_CONF1_FCODE_MASK	0x00F0	/* Fcode cycle mask */
#define	ISP_PCI_CONF1_FIFO_128		0x0040	/* 128 bytes FIFO threshold */
#define	ISP_PCI_CONF1_FIFO_64		0x0030	/* 64 bytes FIFO threshold */
#define	ISP_PCI_CONF1_FIFO_32		0x0020	/* 32 bytes FIFO threshold */
#define	ISP_PCI_CONF1_FIFO_16		0x0010	/* 16 bytes FIFO threshold */
#define	ISP_BUS_CONF1_BURST_ENABLE	0x0004	/* Global enable Bus bursts */
#define	ISP_SBUS_CONF1_FIFO_64		0x0003	/* 64 bytes FIFO threshold */
#define	ISP_SBUS_CONF1_FIFO_32		0x0002	/* 32 bytes FIFO threshold */
#define	ISP_SBUS_CONF1_FIFO_16		0x0001	/* 16 bytes FIFO threshold */
#define	ISP_SBUS_CONF1_FIFO_8		0x0000	/* 8 bytes FIFO threshold */
#define	ISP_SBUS_CONF1_BURST8		0x0008 	/* Enable 8-byte  bursts */
#define	ISP_PCI_CONF1_SXP		0x0008	/* SXP register select */


		/* BUS CONTROL REGISTER */
#define	ISP_BUS_ICR_ENABLE_DMA_INT	0x0020	/* Enable DMA interrupts */
#define	ISP_BUS_ICR_ENABLE_CDMA_INT	0x0010	/* Enable CDMA interrupts */
#define	ISP_BUS_ICR_ENABLE_SXP_INT	0x0008	/* Enable SXP interrupts */
#define	ISP_BUS_ICR_ENABLE_RISC_INT	0x0004	/* Enable Risc interrupts */
#define	ISP_BUS_ICR_ENABLE_ALL_INTS	0x0002	/* Global enable all inter */
#define	ISP_BUS_ICR_DISABLE_ALL_INTS	0x0000	/* Global Disable all inter */
#define	ISP_BUS_ICR_SOFT_RESET		0x0001
						/*
						 * Force soft reset of isp,
						 * Isp clears bit 0 when done
						 */


		/* BUS STATUS REGISTER */
#define	ISP_BUS_ISR_DMA_INT		0x0020	/* DMA interrupt pending */
#define	ISP_BUS_ISR_CDMA_INT		0x0010	/* CDMA interrupt pending */
#define	ISP_BUS_ISR_SXP_INT		0x0008	/* SXP interrupt pending */
#define	ISP_BUS_ISR_RISC_INT		0x0004	/* Risc interrupt pending */
#define	ISP_BUS_ISR_INT_PENDING		0x0002	/* Global interrupt pending */


		/* BUS SEMAPHORE REGISTER */
#define	ISP_BUS_SEMA_STATUS		0x0002	/* Semaphore Status Bit */
#define	ISP_BUS_SEMA_LOCK  		0x0001	/* Semaphore Lock Bit */



/*
 *
 * Defines for the Command and Data DMA Channel Registers.
 *
 * These defines are not used during normal operation, execpt possibly
 * to read the status of the DMA registers.
 *
 */
		/* DMA CONFIGURATION REGISTER */
#define	ISP_DMA_CONF_ENABLE_SXP_DMA	0x0008	/* Enable SXP to DMA Data */
#define	ISP_DMA_CONF_ENABLE_INTS	0x0004	/* Enable interrupts to RISC */
#define	ISP_DMA_CONF_ENABLE_BURST	0x0002	/* Enable Bus burst trans */
#define	ISP_DMA_CONF_DMA_DIRECTION	0x0001
						/*
						 * Set DMA direction;
						 * 0 - DMA FIFO to host,
						 * 1 - Host to DMA FIFO
						 */


		/* DMA CONTROL REGISTER */
#define	ISP_DMA_CON_SUSPEND_CHAN	0x0010	/* Suspend DMA transfer */
#define	ISP_DMA_CON_CLEAR_CHAN		0x0008
						/*
						 * Clear FIFO and DMA Channel,
						 * reset DMA registers
						 */
#define	ISP_DMA_CON_CLEAR_FIFO		0x0004	/* Clear DMA FIFO */
#define	ISP_DMA_CON_RESET_INT		0x0002	/* Clear DMA interrupt */
#define	ISP_DMA_CON_STROBE		0x0001	/* Start DMA transfer */


		/* DMA STATUS REGISTER */
#define	ISP_SBUS_DMA_STATUS_PIPE_MASK	0x00C0	/* DMA Pipeline status mask */
#define	ISP_SBUS_DMA_STATUS_CHAN_MASK	0x0030	/* Channel status mask */
#define	ISP_SBUS_DMA_STATUS_BUS_PARITY	0x0008	/* Parity Error on bus */
#define	ISP_SBUS_DMA_STATUS_BUS_ERR	0x0004	/* Error Detected on bus */
#define	ISP_SBUS_DMA_STATUS_TERM_COUNT	0x0002	/* DMA Transfer Completed */
#define	ISP_SBUS_DMA_STATUS_INTERRUPT	0x0001	/* Enable DMA channel inter */

#define	ISP_PCI_DMA_STATUS_INTERRUPT	0x8000	/* Enable DMA channel inter */
#define	ISP_PCI_DMA_STATUS_RETRY_STAT	0x4000	/* Retry status */
#define	ISP_PCI_DMA_STATUS_CHAN_MASK	0x3000	/* Channel status mask */
#define	ISP_PCI_DMA_STATUS_FIFO_OVR	0x0100	/* DMA FIFO overrun cond */
#define	ISP_PCI_DMA_STATUS_FIFO_UDR	0x0080	/* DMA FIFO underrun cond */
#define	ISP_PCI_DMA_STATUS_BUS_ERR	0x0040	/* Error Detected on bus */
#define	ISP_PCI_DMA_STATUS_BUS_PARITY	0x0020	/* Parity Error on bus */
#define	ISP_PCI_DMA_STATUS_CLR_PEND	0x0010	/* DMA clear pending */
#define	ISP_PCI_DMA_STATUS_TERM_COUNT	0x0008	/* DMA Transfer Completed */
#define	ISP_PCI_DMA_STATUS_DMA_SUSP	0x0004	/* DMA suspended */
#define	ISP_PCI_DMA_STATUS_PIPE_MASK	0x0003	/* DMA Pipeline status mask */

		/* DMA Status Register, pipeline status bits */
#define	ISP_SBUS_DMA_PIPE_FULL		0x00C0	/* Both pipeline stages full */
#define	ISP_SBUS_DMA_PIPE_OVERRUN	0x0080	/* Pipeline overrun */
#define	ISP_SBUS_DMA_PIPE_STAGE1	0x0040
						/*
						 * Pipeline stage 1 Loaded,
						 * stage 2 empty
						 */
#define	ISP_PCI_DMA_PIPE_FULL		0x0003	/* Both pipeline stages full */
#define	ISP_PCI_DMA_PIPE_OVERRUN	0x0002	/* Pipeline overrun */
#define	ISP_PCI_DMA_PIPE_STAGE1		0x0001
						/*
						 * Pipeline stage 1 Loaded,
						 * stage 2 empty
						 */
#define	ISP_DMA_PIPE_EMPTY		0x0000	/* Both pipeline stages empty */

		/* DMA Status Register, channel status bits */
#define	ISP_SBUS_DMA_CHAN_SUSPEND	0x0030	/* Channel error or suspended */
#define	ISP_SBUS_DMA_CHAN_TRANSFER	0x0020	/* Chan transfer in progress */
#define	ISP_SBUS_DMA_CHAN_ACTIVE	0x0010	/* Chan trans to host active */
#define	ISP_PCI_DMA_CHAN_TRANSFER	0x3000	/* Chan transfer in progress */
#define	ISP_PCI_DMA_CHAN_SUSPEND	0x2000	/* Channel error or suspended */
#define	ISP_PCI_DMA_CHAN_ACTIVE		0x1000	/* Chan trans to host active */
#define	ISP_DMA_CHAN_IDLE		0x0000	/* Chan idle (normal comp) */


		/* DMA FIFO STATUS REGISTER */
#define	ISP_DMA_FIFO_STATUS_OVERRUN	0x0200	/* FIFO Overrun Condition */
#define	ISP_DMA_FIFO_STATUS_UNDERRUN	0x0100	/* FIFO Underrun Condition */
#define	ISP_SBUS_DMA_FIFO_COUNT_MASK	0x007F	/* FIFO Byte count mask */
#define	ISP_PCI_DMA_FIFO_COUNT_MASK	0x00FF	/* FIFO Byte count mask */


/*
 *
 * Defines for the SXP registers.
 *
 */
		/* SXP CONF1 REGISTER */
#define	ISP_SXP_CONF1_ASYNCH_SETUP	0xF000	/* Asynchronous setup time */
#define	ISP_SXP_CONF1_SELECTION_UNIT	0x0000	/* Selection time unit */
#define	ISP_SXP_CONF1_SELECTION_TIMEOUT	0x0600	/* Selection timeout */
#define	ISP_SXP_CONF1_CLOCK_FACTOR	0x00E0	/* Clock factor */
#define	ISP_SXP_CONF1_SCSI_ID		0x000F	/* SCSI id */


		/* SXP CONF2 REGISTER */
#define	ISP_SXP_CONF2_DISABLE_FILTER	0x0040	/* Disable SCSI rec filters */
#define	ISP_SXP_CONF2_REQ_ACK_PULLUPS	0x0020	/* Enable req/ack pullups */
#define	ISP_SXP_CONF2_DATA_PULLUPS	0x0010	/* Enable data active pullups */
#define	ISP_SXP_CONF2_CONFIG_AUTOLOAD	0x0008	/* Enable dev conf auto-load */
#define	ISP_SXP_CONF2_RESELECT		0x0002	/* Enable reselection */
#define	ISP_SXP_CONF2_SELECT		0x0001	/* Enable selection */


		/* SXP INTERRUPT REGISTER */
#define	ISP_SXP_INT_PARITY_ERR		0x8000	/* Parity error detected */
#define	ISP_SXP_INT_GROSS_ERR		0x4000	/* Gross error detected */
#define	ISP_SXP_INT_FUNCTION_ABORT	0x2000	/* Last cmd aborted */
#define	ISP_SXP_INT_CONDITION_FAILED	0x1000	/* Last cond failed test cond */
#define	ISP_SXP_INT_FIFO_EMPTY		0x0800	/* SCSI FIFO is empty */
#define	ISP_SXP_INT_BUF_COUNTER_ZERO	0x0400	/* SCSI buf count == zero */
#define	ISP_SXP_INT_XFER_ZERO		0x0200	/* SCSI trans count == zero */
#define	ISP_SXP_INT_INT_PENDING		0x0080	/* SXP interrupt pending */
#define	ISP_SXP_INT_CMD_RUNNING		0x0040	/* SXP is running a command */
#define	ISP_SXP_INT_INT_RETURN_CODE	0x000F	/* Interrupt return code */


		/* SXP GROSS ERROR REGISTER */
#define	ISP_SXP_GROSS_OFFSET_RESID	0x0040	/* Req/Ack offset wasn't zero */
#define	ISP_SXP_GROSS_OFFSET_UNDERFLOW	0x0020	/* Req/Ack offset underflowed */
#define	ISP_SXP_GROSS_OFFSET_OVERFLOW	0x0010	/* Req/Ack offset overflowed */
#define	ISP_SXP_GROSS_FIFO_UNDERFLOW	0x0008	/* SCSI FIFO unload when emp */
#define	ISP_SXP_GROSS_FIFO_OVERFLOW	0x0004	/* SCSI FIFO loaded when full */
#define	ISP_SXP_GROSS_WRITE_ERR		0x0002	/* SXP and RISC wrote to reg */
#define	ISP_SXP_GROSS_ILLEGAL_INST	0x0001	/* Bad inst loaded into SXP */


		/* SXP EXCEPTION REGISTER */
#define	ISP_SXP_EXCEPT_USER_0		0x8000	/* Enable user exception #0 */
#define	ISP_SXP_EXCEPT_USER_1		0x4000	/* Enable user exception #1 */
#define	ISP_PCI_SXP_EXCEPT_SCAM		0x0400	/* SCAM selection enable */
#define	ISP_SXP_EXCEPT_BUS_FREE		0x0200	/* Enable init bus free det */
#define	ISP_SXP_EXCEPT_TARGET_ATN	0x0100	/* Enable tar mode atten det */
#define	ISP_SXP_EXCEPT_RESELECTED	0x0080	/* Enable resel excep hand */
#define	ISP_SXP_EXCEPT_SELECTED		0x0040	/* Enable sel except handling */
#define	ISP_SXP_EXCEPT_ARBITRATION	0x0020	/* Enable arb except handling */
#define	ISP_SXP_EXCEPT_GROSS_ERR	0x0010	/* Enable gross error except */
#define	ISP_SXP_EXCEPT_BUS_RESET	0x0008	/* Enable bus reset except */



		/* SXP OVERRIDE REGISTER */
#define	ISP_SXP_ORIDE_EXT_TRIGGER	0x8000	/* Enable external trigger */
#define	ISP_SXP_ORIDE_STEP		0x4000	/* Enable single step mode */
#define	ISP_SXP_ORIDE_BREAKPOINT	0x2000	/* Enable breakpoint reg */
#define	ISP_SXP_ORIDE_PIN_WRITE		0x1000	/* Enable writes to SCSI pins */
#define	ISP_SXP_ORIDE_FORCE_OUTPUTS	0x0800	/* Force SCSI outputs on */
#define	ISP_SXP_ORIDE_LOOPBACK		0x0400	/* Enable SCSI loopback mode */
#define	ISP_SXP_ORIDE_PARITY_TEST	0x0200	/* Enable parity test mode */
#define	ISP_SXP_ORIDE_TRISTATE_ENA_PINS	0x0100	/* Tristate SCSI enable pins */
#define	ISP_SXP_ORIDE_TRISTATE_PINS	0x0080	/* Tristate SCSI pins */
#define	ISP_SXP_ORIDE_FIFO_RESET	0x0008	/* Reset SCSI FIFO */
#define	ISP_SXP_ORIDE_CMD_TERMINATE	0x0004	/* Terminate cur SXP com */
#define	ISP_SXP_ORIDE_RESET_REG		0x0002	/* Reset SXP registers */
#define	ISP_SXP_ORIDE_RESET_MODULE	0x0001	/* Reset SXP module */

		/* SXP COMMANDS */
#define	ISP_SXP_RESET_BUS_CMD		0x300b

		/* SXP USER EXCEPTION REGISTER */
#define	ISP_SXP_EXCEPT_1		0x0002	/* User exception #1 */
#define	ISP_SXP_EXCEPT_0		0x0001	/* User exception #0 */


		/* SXP SCSI ID REGISTER */
#define	ISP_SXP_SELECTING_ID		0x0F00	/* (Re)Selecting id */
#define	ISP_SXP_SELECT_ID		0x000F	/* Select id */


		/* SXP DEV CONFIG1 REGISTER */
#define	ISP_SXP_DCONF1_SYNC_HOLD	0x7000	/* Synchronous data hold */
#define	ISP_SXP_DCONF1_SYNC_SETUP	0x0F00	/* Synchronous data setup */
#define	ISP_SXP_DCONF1_SYNC_OFFSET	0x000F	/* Synchronous data offset */


		/* SXP DEV CONFIG2 REGISTER */
#define	ISP_SXP_DCONF2_FLAGS_MASK	0xF000	/* Device flags */
#define	ISP_SXP_DCONF2_WIDE		0x0400	/* Enable wide SCSI data tran */
#define	ISP_SXP_DCONF2_PARITY		0x0200	/* Enable parity checking */
#define	ISP_SXP_DCONF2_BLOCK_MODE	0x0100	/* Enable blk mode tran count */
#define	ISP_SXP_DCONF2_ASSERTION_MASK	0x0007	/* Assersion period mask */


		/* SXP PHASE POINTER REGISTER */
#define	ISP_SXP_PHASE_STATUS_PTR	0x1000	/* Status buffer offset */
#define	ISP_SXP_PHASE_MSG_IN_PTR	0x0700	/* Msg in buffer offset */
#define	ISP_SXP_PHASE_COM_PTR		0x00F0	/* Command buffer offset */
#define	ISP_SXP_PHASE_MSG_OUT_PTR	0x0007	/* Msg out buffer offset */


		/* SXP FIFO STATUS REGISTER */
#define	ISP_SXP_FIFO_TOP_RESID		0x8000	/* Top residue reg full */
#define	ISP_SXP_FIFO_ACK_RESID		0x4000	/* Wide transfers odd residue */
#define	ISP_SXP_FIFO_COUNT_MASK		0x001C	/* Words in SXP FIFO */
#define	ISP_SXP_FIFO_BOTTOM_RESID	0x0001	/* Bottom residue reg full */


		/* SXP CONTROL PINS REGISTER */
#define	ISP_SXP_PINS_CON_PHASE		0x8000	/* Scsi phase valid */
#define	ISP_SXP_PINS_CON_PARITY_HI	0x0400	/* Parity pin */
#define	ISP_SXP_PINS_CON_PARITY_LO	0x0200	/* Parity pin */
#define	ISP_SXP_PINS_CON_REQ		0x0100	/* SCSI bus REQUEST */
#define	ISP_SXP_PINS_CON_ACK		0x0080	/* SCSI bus ACKNOWLEDGE */
#define	ISP_SXP_PINS_CON_RST		0x0040	/* SCSI bus RESET */
#define	ISP_SXP_PINS_CON_BSY		0x0020	/* SCSI bus BUSY */
#define	ISP_SXP_PINS_CON_SEL		0x0010	/* SCSI bus SELECT */
#define	ISP_SXP_PINS_CON_ATN		0x0008	/* SCSI bus ATTENTION */
#define	ISP_SXP_PINS_CON_MSG		0x0004	/* SCSI bus MESSAGE */
#define	ISP_SXP_PINS_CON_CD 		0x0002	/* SCSI bus COMMAND */
#define	ISP_SXP_PINS_CON_IO 		0x0001	/* SCSI bus INPUT */

/*
 * Set the hold time for the SCSI Bus Reset to be 250 ms
 */
#define	ISP_SXP_SCSI_BUS_RESET_HOLD_TIME	250
/*
 * settings of status to reflect different information transfer phases
 */
#define	ISP_SXP_PHASE_MASK	\
	(ISP_SXP_PINS_CON_MSG | ISP_SXP_PINS_CON_CD | ISP_SXP_PINS_CON_IO)

#define	ISP_SXP_PHASE_DATA_OUT	\
	0

#define	ISP_SXP_PHASE_DATA_IN	\
	(ISP_SXP_PINS_CON_IO)

#define	ISP_SXP_PHASE_COMMAND	\
	(ISP_SXP_PINS_CON_CD)

#define	ISP_SXP_PHASE_STATUS	\
	(ISP_SXP_PINS_CON_CD | ISP_SXP_PINS_CON_IO)

#define	ISP_SXP_PHASE_MSG_OUT	\
	(ISP_SXP_PINS_CON_MSG | ISP_SXP_PINS_CON_CD)

#define	ISP_SXP_PHASE_MSG_IN	\
	(ISP_SXP_PINS_CON_MSG | ISP_SXP_PINS_CON_CD | ISP_SXP_PINS_CON_IO)

#define	ISP_SXP_BUS_BUSY	\
	(ISP_SXP_PINS_CON_BSY | ISP_SXP_PINS_CON_SEL)




		/* SXP DIFF PINS REGISTER */
#define	ISP_SXP_PINS_DIFF_SENSE		0x0200	/* DIFFSENS sig on SCSI bus */
#define	ISP_SXP_PINS_DIFF_MODE		0x0100	/* DIFFM signal */
#define	ISP_SXP_PINS_DIFF_ENABLE_OUTPUT	0x0080	/* Enable SXP SCSI data drv */
#define	ISP_SXP_PINS_DIFF_PINS_MASK	0x007C	/* Differential control pins */
#define	ISP_SXP_PINS_DIFF_TARGET	0x0002	/* Enable SXP target mode */
#define	ISP_SXP_PINS_DIFF_INITIATOR	0x0001	/* Enable SXP initiator mode */


/*
 *
 * Defines for the RISC CPU registers.
 *
 */
		/* PROCESSOR STATUS REGISTER */
#define	ISP_RISC_PSR_FORCE_TRUE		0x8000
#define	ISP_RISC_PSR_LOOP_COUNT_DONE	0x4000
#define	ISP_RISC_PSR_RISC_INT		0x2000
#define	ISP_RISC_PSR_TIMER_ROLLOVER	0x1000
#define	ISP_RISC_PSR_ALU_OVERFLOW	0x0800
#define	ISP_RISC_PSR_ALU_MSB		0x0400
#define	ISP_RISC_PSR_ALU_CARRY		0x0200
#define	ISP_RISC_PSR_ALU_ZERO		0x0100
#define	ISP_RISC_PSR_DMA_INT		0x0010
#define	ISP_RISC_PSR_SXP_INT		0x0008
#define	ISP_RISC_PSR_HOST_INT		0x0004
#define	ISP_RISC_PSR_INT_PENDING	0x0002
#define	ISP_RISC_PSR_FORCE_FALSE  	0x0001

		/* PROCESSOR CONTROL REGISTER */
#define	ISP_RISC_PCR_NOP  		0x0000
#define	ISP_RISC_PCR_RESTORE_PCR  	0x1000

		/* MEMORY TIMING REGISTER */
#define	ISP_RISC_MTR_PAGE1_DEFAULT	0x1200	/* Page1 default rd/wr timing */
#define	ISP_RISC_MTR_PAGE0_DEFAULT	0x0012	/* Page0 default rd/wr timing */


/*
 *
 * Defines for the Host Command and Control Register.
 *
 */
					/* Command field defintions */
#define	ISP_HCCR_CMD_NOP		0x0000	/* NOP */
#define	ISP_HCCR_CMD_RESET		0x1000	/* Reset RISC */
#define	ISP_HCCR_CMD_PAUSE		0x2000	/* Pause RISC */
#define	ISP_HCCR_CMD_RELEASE		0x3000	/* Release Paused RISC */
#define	ISP_HCCR_CMD_STEP		0x4000	/* Single Step RISC */
#define	ISP_HCCR_CMD_SET_HOST_INT	0x5000	/* Set Host Interrupt */
#define	ISP_HCCR_CMD_CLEAR_HOST_INT	0x6000	/* Clear Host Interrupt */
#define	ISP_HCCR_CMD_CLEAR_RISC_INT	0x7000	/* Clear RISC interrupt */
#define	ISP_HCCR_CMD_BREAKPOINT		0x8000	/* Change breakpoint enables */
#define	ISP_PCI_HCCR_CMD_BIOS		0x9000	/* Write BIOS enable */
#define	ISP_PCI_HCCR_CMD_PARITY		0xA000	/* Write parity enable */
#define	ISP_PCI_HCCR_CMD_PARITY_ERR	0xE000	/* Generate parity error */
#define	ISP_HCCR_CMD_TEST_MODE		0xF000	/* Set Test Mode */

					/* Status bit defintions */
#define	ISP_PCI_HCCR_PARITY		0x0400	/* Parity error flag */
#define	ISP_PCI_HCCR_PARITY_ENABLE_1	0x0200	/* Parity enable bank 1 */
#define	ISP_PCI_HCCR_PARITY_ENABLE_0	0x0100	/* Parity enable bank 0 */
#define	ISP_HCCR_HOST_INT		0x0080	/* R: Host interrupt set */
#define	ISP_HCCR_RESET			0x0040	/* R: RISC reset in progress */
#define	ISP_HCCR_PAUSE			0x0020	/* R: RISC paused */

					/* Breakpoint defintions */
#define	ISP_HCCR_BREAKPOINT_EXT		0x0010	/* Enable external breakpoint */
#define	ISP_HCCR_BREAKPOINT_1		0x0008	/* Enable breakpoint #1 */
#define	ISP_HCCR_BREAKPOINT_0		0x0004	/* Enable breakpoint #0 */
#define	ISP_PCI_HCCR_BREAKPOINT_INT	0x0002
						/*
						 * Enable interrupt
						 * on breakpoint
						 */
#define	ISP_PCI_HCCR_BIOS		0x0001	/* BIOS enable */


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

#endif	/* _SYS_SCSI_ADAPTERS_ISPREG_H */
