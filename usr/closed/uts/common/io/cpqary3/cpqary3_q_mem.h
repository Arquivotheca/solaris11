/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 */

/*
 * Copyright 2009, 2011, Hewlett-Packard Development Company, L.P.
 */

/*
 * Module Name:
 *	CPQARY3_Q_MEM.H
 * Abstract:
 *  The generic Header file for CPQARY3 Driver.
 *	This file contains all definitions and declarations related to the
 *	Memory Management and Q Management of this driver.
 */

#ifndef CPQARY3_Q_MEM_H
#define	CPQARY3_Q_MEM_H

#define	CPQARY3_GET_MEM_TAG    		4

#define	CPQARY3_FREE			0
#define	CPQARY3_OCCUPIED		1
#define	CPQARY3_SELF_OCCUPIED		2
#define	CPQARY3_POLL_OCCUPIED		3

#define	CPQARY3_PHYCTGS_DONE		0x01
#define	CPQARY3_CMDMEM_DONE		0x02
#define	CPQARY3_MEMLIST_DONE		0x04

#define	CPQARY3_SUBMITTED_Q		0x0100
#define	CPQARY3_APPEND_RETRIEVE		1
#define	CPQARY3_REMOVE_SUBMITTED	0

#define	CPQARY3_Q_UNMASK		0x0300

/*
 * No. of Commands that would be accomodated in each Command Pool
 * Each command needs a physical contigous memory of 564 Bytes.
 * The number of blocks to be allocated would be decided at run time
 * depending upon the maximum outstanding commands supported by that controller.
 */
#define	NO_OF_CMDLIST_IN_A_BLK		3


/*
 * This structure is meant to store the handle of the physically contiguous
 * memory blcoks that will be allocated during the _meminit().
 * The no. of blocks that will be allocated will be decide at run time
 * depending upon the maximum outstanding commands supported by the controller.
 * each block is physically contiguous & can hold 3 commands.
 */
typedef struct cpqary3_phyctg {
	size_t				real_size;
	ddi_dma_handle_t		cpqary3_dmahandle;
	ddi_acc_handle_t		cpqary3_acchandle;
	ddi_dma_cookie_t		cpqary3_dmacookie;
} cpqary3_phyctg_t;

typedef struct cpqary3_command_private CMDLIST;

/* Linked List Structure to hold Command List Details */
typedef struct cpqary3_command_private {
	uint8_t					occupied;
	uint8_t					cmdpvt_flag;
	uint32_t				cmdlist_phyaddr;
	uint32_t				cmdlist_erraddr;
	cpqary3_tag_t 				tag;
	ErrorInfo_t				*errorinfop;
	CommandList_t			    *cmdlist_memaddr;
	struct cpqary3_command_private  *next;	/* next Command Memory	 */
	struct cpqary3_command_private  *prev;	/* previous Command Memory */
	struct cpqary3_per_controller 	*ctlr;	/* to its controller	 */
	struct cpqary3_pkt 		*pvt_pkt; /* Driver Private Packet */
	struct cpqary3_driver_private 	*driverdata; /* command private data */
	struct cpqary3_command_private 	*snext;	/* to maintain Submitted Q & */
	struct cpqary3_command_private 	*sprev;	/* Retrieved Q.	 */
	cpqary3_phyctg_t 		*cpqary3_phyctgp;
	void   (*complete)(CMDLIST *);
}cpqary3_cmdpvt_t;

/* structure to maintain a linked list of the memory blocks */
typedef struct physical_handle_address {
	cpqary3_phyctg_t 		*blk_addr;
	struct physical_handle_address  *next;
} cpqary3_phys_hdl_addr_t;

/* Structure to hold Memory Pool Details */
typedef struct cpqary3_cmdmemlist {
	uint16_t		max_memcnt;
	cpqary3_cmdpvt_t	*pool;
	cpqary3_cmdpvt_t	*head;
	cpqary3_cmdpvt_t	*tail;
	/* Head pointer to the linked list of the memory blocks */
	cpqary3_phys_hdl_addr_t *cpqary3_phyctgp;
} cpqary3_cmdmemlist_t;


#endif /* CPQARY3_Q_MEM_H */
