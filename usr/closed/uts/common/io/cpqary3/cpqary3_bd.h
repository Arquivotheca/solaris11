/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 2009, 2011, Hewlett-Packard Development Company, L.P.
 */

/*
 * Module Name:
 * CPQARY3_BD.H
 * Abstract:
 * This file collects various info about each supported
 * controller that the driver needs to know in order to
 * properly support the board.  during device attach, the
 * driver can use cpqary3_bd_getbybid() to fetch the board
 * definition for the device to which it has attached.
 * the source for the board definitions themselves is kept
 * in controllers, which is used to generate the c code to
 * define a static array of structs.  this array and its
 * search functions are defined in cpqary3_bd.c
 * NOTE: if new fields are added or if the order of the
 * fields is altered, then the cpqary3_bd_defs.h.sacdf
 * template must be updated!
 */

#ifndef	CPQARY3_BD_H
#define	CPQARY3_BD_H


struct cpqary3_bd {
	char		*bd_dispname;		/* display name */
	offset_t	bd_maplen;	/* register map length */
	uint16_t	bd_pci_subvenid;  /* PCI subvendor ID */
	uint16_t	bd_pci_subsysid;  /* PCI subsystem ID */
	uint32_t	bd_intrpendmask;  /* interrupt pending mask */
	uint32_t	bd_flags;	/* flags */
	uint32_t	bd_is_e200;
	uint32_t	bd_intrmask;
	uint32_t	bd_lockup_intrmask;
	uint32_t   bd_is_p410;
};
typedef struct cpqary3_bd   cpqary3_bd_t;

/* bd_flags */
#define	SA_BD_SAS   0x00000001  /* board is a sas controller */


extern cpqary3_bd_t *
cpqary3_bd_getbybid(uint32_t bid);

#endif /* CPQARY3_BD_H */
