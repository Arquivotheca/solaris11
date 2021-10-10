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
 * Copyright (c) 1991, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains functions to implement the partition menu commands.
 */
#include "global.h"
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "label.h"
#include "partition.h"
#include "menu_partition.h"
#include "menu_command.h"
#include "menu_fdisk.h"
#include "misc.h"
#include "param.h"

#ifdef __STDC__

/* Function prototypes for ANSI C Compilers */
static void	nspaces(int);
static int	ndigits(uint64_t);

#else	/* __STDC__ */

/* Function prototypes for non-ANSI C Compilers */
static void	nspaces();
static int	ndigits();

#endif	/* __STDC__ */

/* Shell command line used by fdisk(1m) to Expand a disk's Solaris partition. */
#define	CMD_LINE_FDISK_EXPAND	"fdisk -x %s"

/* Flags to modify behavior of can_vtoc_disk_be_expanded */
#define	FMT_EXPAND_F_QUIET	1	/* do not show messages */
#define	FMT_EXPAND_F_MOD	2	/* Modify on-disk metadata */

static int get_capacity_from_driver(int fd, uint64_t *capacity);

/*
 * This routine implements the 'a' command.  It changes the 'a' partition.
 */
int
p_apart()
{

	change_partition(0);
	return (0);
}

/*
 * This routine implements the 'b' command.  It changes the 'b' partition.
 */
int
p_bpart()
{

	change_partition(1);
	return (0);
}

/*
 * This routine implements the 'c' command.  It changes the 'c' partition.
 */
int
p_cpart()
{

	change_partition(2);
	return (0);
}

/*
 * This routine implements the 'd' command.  It changes the 'd' partition.
 */
int
p_dpart()
{

	change_partition(3);
	return (0);
}

/*
 * This routine implements the 'e' command.  It changes the 'e' partition.
 */
int
p_epart()
{

	change_partition(4);
	return (0);
}

/*
 * This routine implements the 'f' command.  It changes the 'f' partition.
 */
int
p_fpart()
{

	change_partition(5);
	return (0);
}

/*
 * This routine implements the 'g' command.  It changes the 'g' partition.
 */
int
p_gpart()
{

	change_partition(6);
	return (0);
}

/*
 * This routine implements the 'h' command.  It changes the 'h' partition.
 */
int
p_hpart()
{

	change_partition(7);
	return (0);
}

/*
 * This routine implements the 'i' command. It is valid only for EFI
 * labeled disks. This can be used only in expert mode.
 */
int
p_ipart()
{
	change_partition(8);
	return (0);
}

#if defined(i386)
/*
 * This routine implements the 'j' command.  It changes the 'j' partition.
 */
int
p_jpart()
{

	change_partition(9);
	return (0);
}
#endif	/* defined(i386) */

/*
 * expand_capacity_mbr
 *
 * Compute the "capacity" (eg, maximum size) of the Solaris partition
 * that is found on an MBR-based device (MBR == Master Boot Record).
 *
 * This function executes fdisk(1m) to Expand the first Solaris
 * partition found on the current device (eg, cur_disk).  Note that
 * the Solaris partition can be found directly within the MBR or
 * indirectly within a "DOS Extended Partition" (that is itself found
 * within the MBR).
 *
 * Before executing fdisk(1m), this function calls copy_solaris_part
 * to save the Solaris partition's metadata.  fdisk(1m) is then
 * executed, and the difference in size resulting from fdisk(1m)'s
 * Expansion is computed.
 *
 * This function returns a boolean as to whether the Solaris partition
 * could be Expanded at least one sector.  The new size of the Solaris
 * partition is also returned.
 *
 * Inputs:
 *  cur_disk		- disk to operate on
 *
 * Outputs:
 *  return value	- non-zero => "Solaris partition was Expanded"
 *  *capacity		- number of sectors in Expanded partition
 */
static int
expand_capacity_mbr(uint64_t *capacity)
{
	char		cmd_line[MAXPATHLEN];
	char		devpath[MAXPATHLEN];
	struct stat	statbuf;
	struct ipart	ipart_before;
	struct ipart	ipart_after;
	int		rv;

	/*
	 * Compute device path to "p0" device, which is the raw device
	 * for a Solaris-x86 device.  fdisk(1m) expects to operate on
	 * a "p0" device.
	 */
	(void) get_pname(devpath);
	if (stat(devpath, (struct stat *)&statbuf) == -1 ||
	    !S_ISCHR(statbuf.st_mode)) {
		err_print("Disk does not support fixed disk partition table\n");
		return (0);
	}

	/*
	 * Copy metadata of Solaris partition before calling fdisk(1m).
	 * This locates the metadata on the cur_disk device whether the
	 * Solaris partition is directly referenced by the MBR or
	 * indirectly by an Extended Partition.  After running
	 * fdisk(1m), compare the new metadata with this metadata.
	 */
	bzero(&ipart_before, sizeof (struct ipart));
	rv = copy_solaris_part(&ipart_before);
	if (rv != 0) {
		return (0);
	}

	/*
	 * Clear the metadata that fdisk(1m) will modify to ensure there are no
	 * false-positives or false-negatives when metadata is compared.
	 */
	bzero(&ipart_after, sizeof (struct ipart));

	/* Generate fdisk(1m)'s command line and execute it. */
	(void) snprintf(cmd_line, sizeof (cmd_line), CMD_LINE_FDISK_EXPAND,
	    devpath);
#ifdef DEBUG
	fmt_print("Executing fdisk as: ""%s""\n", cmd_line);
#endif
	rv = system(cmd_line);
	if (rv < 0) {
		err_print("Execution of \"%s\" had error: %d\n",
		    cmd_line, errno);
		return (0);
	}

	/*
	 * Find the Solaris partition's metadata on the "p0" device
	 * after fdisk(1m) performed the Expansion (if any).
	 */
	rv = copy_solaris_part(&ipart_after);
	if (rv != 0) {
		return (0);
	}
#ifdef DEBUG
	fmt_print("Solaris size before/after fdisk:  before: %u  after: %u\n",
	    ipart_before.numsect, ipart_after.numsect);
#endif

	if (ipart_after.numsect > ipart_before.numsect) {
		/* then disk was Expanded at least one sector. */
		*capacity = ipart_after.numsect;
		return (1);
	} else {
		return (0);
	}
}

/*
 * get_max_allowable_sol_size
 *
 * Compute the capacity (eg, maximum size) of the Solaris partition
 * that is found on an MBR-based device (MBR == Master Boot Record).
 *
 * Perform the computation without modifying the MBR.  Indirectly call
 * libfdisk functions that can locate a Solaris partition directly
 * referenced in the MBR or indirectly referenced in an Extended
 * Partition.
 *
 * If the Solaris partition (SP) has a "neighboring" partition (NP),
 * then the NP's starting address defines the SP's capacity boundary.
 * If no NP is found, the disk's highest address is used to compute
 * the SP's capacity.
 *
 * Arguments:
 *  fd		-
 *  *capacity	- addr to hold the computed capacity of Solaris partition
 *
 * Return value:
 *  !0		- a capacity for a Solaris partition was found
 */
static int
get_max_allowable_sol_size(int fd, uint64_t *capacity)
{
	int		found_solaris;
	int		found_neighbor;
	uint32_t	solaris_start;
	uint32_t	neighbor_start;

	if (get_solaris_part_neighbor(&found_solaris, &found_neighbor,
	    &solaris_start, &neighbor_start) != 0) {
		/* An MBR should have been found. */
		return (0);
	}

	if (found_solaris == 0) {
		/* there is no Solaris partition. */
		return (0);
	}

	if (found_neighbor != 0) {
		/*
		 * then we found both a Solaris partition and a Neighbor.
		 * The expansion limit is the Neighbor's start-addr.
		 */
		*capacity = neighbor_start - solaris_start;
#ifdef DEBUG
		fmt_print("MBR and neighbor found; capacity : %llu\n",
		    *capacity);
#endif
		return (1);
	}

	/*
	 * There is a Solaris partition but not a Neighbor.  The
	 * expansion limit is the size of the disk.
	 */
	if (get_capacity_from_driver(fd, capacity) < 0) {
		return (0);
	}

	*capacity -= solaris_start;
#ifdef DEBUG
	fmt_print("MBR found; capacity from ioctl: %llu\n", *capacity);
#endif
	return (1);
}

/*
 * get_capacity_from_driver
 *
 * Obtain the Capacity (in blocks) from the driver managing
 * the device referenced by 'fd'.
 */
static int
get_capacity_from_driver(int fd, uint64_t *capacity)
{
	struct dk_minfo	media_info;

	/* Find size of the current disk (in sectors). */
	if (ioctl(fd, DKIOCGMEDIAINFO, &media_info) < 0) {
		err_print("DKIOCGMEDIAINFO failed: %d\n", errno);
		return (-1);
	} else {
		*capacity = media_info.dki_capacity;
		return (0);
	}
}

/*
 * can_vtoc_disk_be_expanded
 *
 * Compute whether and by how much the current disk (cur_disk) can
 * (or should) be Expanded, where "expandable" means that the disk's
 * on-disk metadata represents a disk that is smaller than what is
 * actually present.
 *
 * Caller can specify a flag to determine if on-disk metadata (eg, the
 * MBR (Master Boot Record) on x86) should be modified.  If the flag
 * is cleared, then this function computes the Expansion limit and
 * size without modifying on-disk metadata.
 *
 * This function searches for a Solaris partition in either the MBR
 * or a DOS Extended Partition.  If found, it computes the
 * expansion-limit, which is either the starting address of a
 * "neighboring" partition or the disk's highest address.  If an MBR
 * is not present, the disk's highest address is the expansion limit.
 *
 * Any identified, expandable space is translated into an integral
 * number of Cylinders, based on the specified sectors per Cylinder,
 * cylsz.  The function's return value compares whether the
 * expandable space is greater than a specified number representing
 * the current number of Cylinders.
 *
 * Inputs:
 *  cur_disk		- disk to operate on
 *  cur_disk->fdisk	- partition info of MBR-derived Solaris partition
 *  solaris_offset	- absolute, starting addr of Solaris partition
 *                         (if MBR is present)
 * Arguments:
 *  fd			- file descriptor of disk to operate on
 *  cylsz		- sectors-per-cylinder
 *  old_pcyl		- number of phys cyls that new num is compared to
 *  new_pcyl		- new num of phys cyls (modulo cyl_sz)
 *  flags		- FMT_EXPAND_F_xxx
 *
 * Outputs:
 *  return value	-   0 if cannot expand
 *			  !=0 if can expand
 *  *new_pcyl		- new num of physical cylinders
 */
static int
can_vtoc_disk_be_expanded(int fd, uint64_t cylsz, uint_t old_pcyl,
    uint_t *new_pcyl, int flags)
{
	uint64_t	capacity;
	uint64_t	phys_cyls;
	int		mbr_found;
	int		sol_part_found;

	/*
	 * Ensure the specified cylinder-size is not zero, as Expansion
	 * of a disk can only be calculated in terms of a (valid) number
	 * of sectors-per-cylinder.
	 */
	if (cylsz == 0) {
		return (0);
	}

	(void) characterize_solaris_part(&mbr_found, &sol_part_found);

	if (mbr_found != 0 && sol_part_found == 0) {
		/* then no Solaris partition is in MBR. */
		return (0);
	}

	if (mbr_found != 0) {
		/* then disk has an MBR with a Solaris Partition */

		if (flags & FMT_EXPAND_F_MOD) {
			/* then modify disk's MBR, if needed. */
			if (expand_capacity_mbr(&capacity) == 0) {
				/* then disk capacity has not changed. */
				return (0);
			}
		} else {
			/*
			 * Obtain disk's capacity without modifying on-disk
			 * data structures.
			 */
			if (get_max_allowable_sol_size(fd, &capacity) == 0) {
				/* then capacity has not changed. */
				return (0);
			}
		}
	} else {
		/*
		 * Find size of the current disk (in sectors).  We interpret
		 * this as the "new size" of the disk.
		 */
		if (get_capacity_from_driver(fd, &capacity) < 0) {
			/* map err to "disk is not expanded" */
			return (0);
		}
	}

	/* Ensure capacity is <= 2TB, as VTOC label cannot hold more. */
	if (capacity > DK_MAX_2TB) {
		capacity = DK_MAX_2TB;
		if ((flags & FMT_EXPAND_F_QUIET) == 0 && option_msg) {
			fmt_print("Truncate the new number of sectors, %llu, "
			    "to what a VTOC label can hold: %u\n", capacity,
			    DK_MAX_2TB);
		}
	}

	/* #Physical Cylinders based on new disk size */
	phys_cyls = capacity / cylsz;

	/*
	 * struct dk_label can only represent a disk having at most
	 * MAX_CYLS cylinders.  Ensure that the new number of physical
	 * cylinders is not larger than what the data structure can hold.
	 */
	if (phys_cyls > MAX_CYLS) {
		if ((flags & FMT_EXPAND_F_QUIET) == 0 && option_msg) {
			fmt_print("Truncate the new number of Cylinders, %llu, "
			    "to what a VTOC label can hold: %u\n", phys_cyls,
			    MAX_CYLS);
		}
		phys_cyls = MAX_CYLS;
	}

	if ((flags & FMT_EXPAND_F_QUIET) == 0 && option_msg && diag_msg) {
		fmt_print("Expansion of Solaris partition:\n");
		fmt_print("  capacity: %llu sectors, ", capacity);
		fmt_print("sectors per cyl: %llu\n", cylsz);
		fmt_print("  previous # pcyls: %u, ", old_pcyl);
		fmt_print("current # pcyls: %llu\n", phys_cyls);
	}

	*new_pcyl = (uint64_t)phys_cyls;

	return (phys_cyls > old_pcyl);
}

/*
 * Compute whether the capacity of a EFI-based disk is larger than
 * what is represented in its on-disk metadata.
 */
static int
can_efi_disk_be_expanded(struct partition_info *p)
{
	/*
	 * If altern_lba is 1, we are using the backup label.
	 * Since we can locate the backup label by disk capacity,
	 * there must be no space expanded after backup label.
	 */
	return ((p->etoc->efi_altern_lba != 1) &&
	    (p->etoc->efi_altern_lba <
	    p->etoc->efi_last_lba));
}

/*
 * Compute whether the capacity of the current disk is larger than
 * what is represented in its on-disk metadata.
 */
int
can_label_be_expanded(struct disk_info *disk)
{
	switch (disk->label_type) {
	case L_TYPE_EFI:
		return (can_efi_disk_be_expanded(cur_parts));

	case L_TYPE_SOLARIS: {
		uint_t	new_pcyl;

		/*
		 * Set F_QUIET bit, as menu-rendering code can indirectly
		 * call this function more than once in succession.  Do
		 * not set F_MOD, as this control-path must not modify
		 * on-disk structures.
		 */
		return (can_vtoc_disk_be_expanded(cur_file, nhead * nsect,
		    pcyl, &new_pcyl, FMT_EXPAND_F_QUIET));
	}

	default:
		/* "disk cannot be Expanded." */
		return (0);
	}
}

/*
 * Find Partition referenced by 'tag' and return its Partition Table index.
 *
 * Return values:
 *  *tag_index	: -1 if 'tag' not found, else table index of 'tag'
 */
static int
slice_find(struct partition_info *pinfo, int tag)
{
	int	i;

	for (i = 0; i < pinfo->vtoc.v_nparts; i++) {
		if ((PART_TAG_VTOC(pinfo, i)) == tag) {
			return (i);
		}
	}

	return (-1);
}

/*
 * p_expand_vtoc - 'expand' subcommand of 'partition' command
 *
 * If the VTOC disk label of the current disk represents a
 * Solaris partition that is smaller than the available space,
 * modify the label so that the Solaris partition can be "expanded"
 * into this space.
 *
 * The expansion will only be performed by amounts that are a
 * multiple of the disk's cylinder size.  This is mandatory, as
 * the disk's geometry must remain the same after the expansion
 * (except for the new number of physical cylinders).
 *
 * On x86, fdisk(1m) is executed non-interactively to write a
 * new MBR (Master Boot Record) that comprehends the disk's new
 * size.  In order to keep the MBR and disk label consistent,
 * p_expand_vtoc also writes the disk label. (p_expand_vtoc
 * writes the disk label on both x86 and  SPARC, but its motivation
 * was an x86 system's need to have the MBR and VTOC be
 * consistent).
 *
 * In general, compute the maximum number of cylinders that the
 * Solaris partition can expand and update global variables that
 * can be used by write_label to write the new disk label.
 *
 *
 * Inputs:
 *  cur_disk	- current disk
 *  cur_file	- File Descriptor
 *  cur_parts	- partition info
 *  nhead	- #Tracks per Cylinder
 *  nsect	- #Sectors per Track
 *
 * Outputs:
 *  pcyl			- number of Physical Cylinders
 *  ncyl			- number of Data Cylinders
 *  cur_parts->pinfo_map[]	- size of "s2" slice updated
 *  cur_parts->vtoc		- size of "s2" slice updated
 */
static int
p_expand_vtoc(void)
{
	struct partition_info	*pinfo;
	uint_t			cyl_sz;
	uint64_t		whole_disk_size;
	int			whole_disk_index;
	uint_t			new_pcyl;
	uint_t			new_ncyl;
	uint_t			old_ncyl;
	uint64_t		new_capacity;
	int			rv;


	/*
	 * Ensure user wants to Expand the label, as Expansion on x86
	 * modifies both the MBR and VTOC; these must be kept in-sync.
	 */
	if (check("Expansion of label cannot be undone; continue (y/n) ")) {
		/* then user entered "n" or "no". */
		return (0);
	}

	/* partition info of current disk */
	pinfo = cur_parts;

	/* Compute number of sectors per cylinder. */
	cyl_sz = nhead * nsect;

	/* Save number of Data Cylinders */
	old_ncyl = ncyl;

#ifdef DEBUG
	fmt_print("Before expansion: nhead: %u  nsect: %u  pcyl: %u  "
	    "cyl_sz: %u\n", nhead, nsect, pcyl, cyl_sz);
#endif

	/* Can disk be Expanded ? */
	rv = can_vtoc_disk_be_expanded(cur_file, cyl_sz, pcyl, &new_pcyl,
	    FMT_EXPAND_F_MOD);
	if (rv == 0) {
		/* then disk's capacity matches its on-disk metadata */
		fmt_print("No expanded capacity is found.\n");
		return (0);
	}

#ifdef DEBUG
	fmt_print("After expansion: nhead: %u  nsect: %u  pcyl: %u  "
	    "cyl_sz: %u\n", nhead, nsect, pcyl, cyl_sz);
#endif

	/* compute new number of Data Cylinders and new Capacity */
	new_ncyl = new_pcyl - acyl;
	new_capacity = new_ncyl * cyl_sz;

	/*
	 * Search the in-memory state for the slice representing the
	 * "whole disk".
	 */
	whole_disk_index = slice_find(pinfo, V_BACKUP);

	/*
	 * Compute new size of Whole Disk slice if it is present and if
	 * its current size covers the full disk size.  (If either
	 * condition fails, then we do not want to modify the Whole Disk
	 * slice, as user might be applying different semantics).
	 */
	whole_disk_size = 0;
	if (whole_disk_index >= 0) {
#ifdef DEBUG
		fmt_print("s%d: addr: %u  actual-sz: %u  expected-sz: %u"
		    "  new-sz: %llu\n", whole_disk_index,
		    PART_ADDR_MAP(pinfo, whole_disk_index),
		    PART_SIZE_MAP(pinfo, whole_disk_index),
		    cyl_sz * old_ncyl, new_capacity);
#endif
		if ((PART_ADDR_MAP(pinfo, whole_disk_index) == 0) &&
		    (PART_SIZE_MAP(pinfo, whole_disk_index) ==
		    cyl_sz * old_ncyl)) {
			/*
			 * then slice covers the whole disk; compute
			 * its new size.
			 */

			/* subtract start-addr from Capacity */
			whole_disk_size = new_capacity -
			    PART_ADDR_MAP(pinfo, whole_disk_index);
#ifdef DEBUG
			fmt_print("Expansion: s%d size:  before: %u  "
			    "after: %llu\n", whole_disk_index,
			    PART_SIZE_MAP(pinfo, whole_disk_index),
			    whole_disk_size);
#endif
		}
	}
#ifdef DEBUG
	if (whole_disk_index < 0) {
		fmt_print("Whole-disk slice is not in Partition table.\n");
	}
#endif

	/* Atomically modify global state */
	enter_critical();

	if (whole_disk_size > 0) {
		PART_SIZE_MAP(pinfo, whole_disk_index) = whole_disk_size;
#if defined(_SUNOS_VTOC_16)
		PART_SIZE_VTOC(pinfo, whole_disk_index) = whole_disk_size;
#endif
	}

	pcyl = new_pcyl;  /* #Physical Cylinders */
	ncyl = new_ncyl;  /* #Data Cylinders */

	exit_critical();

#ifdef DEBUG
	fmt_print("End of expansion:  pcyl: %u  capacity: %llu  "
	    "solaris_offset: ( 0x%x = %u )\n", pcyl, new_capacity,
	    solaris_offset, solaris_offset);
#endif

	fmt_print("The expanded capacity was added to the disk label");
	if (whole_disk_size > 0) {
		fmt_print(" and \"s%u\"", whole_disk_index);
	}
	fmt_print(".\n");

	/* Write VTOC label to disk. */
	if ((rv = write_label()) != 0) {
		err_print("Writing of VTOC label failed.\n");
	} else {
		fmt_print("Disk label was written to disk.\n");
	}

	return (rv);
}

static int
p_expand_efi(void)
{
	uint64_t delta;
	uint_t nparts;
	struct dk_gpt *efi_label;

	if (can_efi_disk_be_expanded(cur_parts) == 0) {
		err_print("Warning: No expanded capacity is found.\n");
		return (0);
	}

	efi_label = cur_parts->etoc;

	delta = efi_label->efi_last_lba - efi_label->efi_altern_lba;
	nparts = efi_label->efi_nparts;

	enter_critical();
	efi_label->efi_parts[nparts - 1].p_start += delta;
	efi_label->efi_last_u_lba += delta;
	efi_label->efi_altern_lba = efi_label->efi_last_lba;
	exit_critical();

	fmt_print("The expanded capacity is added to the unallocated space.\n");
	return (0);
}

/*
 * p_expand - implement the 'expand' command
 *
 * If the capacity of the current disk has been increased, modify
 * the disk's on-disk metadata to be aware of the new size.
 */
int
p_expand()
{
	if (cur_disk->label_type == L_TYPE_SOLARIS) {
		return (p_expand_vtoc());
	} else if (cur_disk->label_type == L_TYPE_EFI) {
		return (p_expand_efi());
	} else {
		return (0);
	}
}

/*
 * This routine implements the 'select' command.  It allows the user
 * to make a pre-defined partition map the current map.
 */
int
p_select()
{
	struct partition_info	*pptr, *parts;
	u_ioparam_t		ioparam;
	int			i, index, deflt, *defltptr = NULL;
	blkaddr_t		b_cylno;
#if defined(i386)
	blkaddr_t		cyl_offset;
#endif

	parts = cur_dtype->dtype_plist;
	/*
	 * If there are no pre-defined maps for this disk type, it's
	 * an error.
	 */
	if (parts == NULL) {
		err_print("No defined partition tables.\n");
		return (-1);
	}

	/*
	 * Loop through the pre-defined maps and list them by name.  If
	 * the current map is one of them, make it the default.  If any
	 * the maps are unnamed, label them as such.
	 */
	for (i = 0, pptr = parts; pptr != NULL; pptr = pptr->pinfo_next) {
		if (cur_parts == pptr) {
			deflt = i;
			defltptr = &deflt;
		}
		if (pptr->pinfo_name == NULL)
			fmt_print("        %d. unnamed\n", i++);
		else
			fmt_print("        %d. %s\n", i++, pptr->pinfo_name);
	}
	ioparam.io_bounds.lower = 0;
	ioparam.io_bounds.upper = i - 1;
	/*
	 * Ask which map should be made current.
	 */
	index = input(FIO_INT, "Specify table (enter its number)", ':',
	    &ioparam, defltptr, DATA_INPUT);
	for (i = 0, pptr = parts; i < index; i++, pptr = pptr->pinfo_next)
		;
	if (cur_label == L_TYPE_EFI) {
		enter_critical();
		cur_disk->disk_parts = cur_parts = pptr;
		exit_critical();
		fmt_print("\n");
		return (0);
	}
#if defined(i386)
	/*
	 * Adjust for the boot and alternate sectors partition - assuming that
	 * the alternate sectors partition physical location follows
	 * immediately the boot partition and partition sizes are
	 * expressed in multiple of cylinder size.
	 */
	cyl_offset = pptr->pinfo_map[I_PARTITION].dkl_cylno + 1;
	if (pptr->pinfo_map[J_PARTITION].dkl_nblk != 0) {
		cyl_offset = pptr->pinfo_map[J_PARTITION].dkl_cylno +
		    ((pptr->pinfo_map[J_PARTITION].dkl_nblk +
		    (spc() - 1)) / spc());
	}
#else	/* !defined(i386) */

	b_cylno = 0;

#endif	/* defined(i386) */

	/*
	 * Before we blow the current map away, do some limits checking.
	 */
	for (i = 0; i < NDKMAP; i++)  {

#if defined(i386)
		if (i == I_PARTITION || i == J_PARTITION || i == C_PARTITION) {
			b_cylno = 0;
		} else if (pptr->pinfo_map[i].dkl_nblk == 0) {
			/*
			 * Always accept starting cyl 0 if the size is 0 also
			 */
			b_cylno = 0;
		} else {
			b_cylno = cyl_offset;
		}
#endif		/* defined(i386) */
		if (pptr->pinfo_map[i].dkl_cylno < b_cylno ||
		    pptr->pinfo_map[i].dkl_cylno > (ncyl-1)) {
			err_print("partition %c: starting cylinder "
			    "%d is out of range\n",
			    (PARTITION_BASE+i),
			    pptr->pinfo_map[i].dkl_cylno);
			return (0);
		}
		if (pptr->pinfo_map[i].dkl_nblk > ((ncyl -
		    pptr->pinfo_map[i].dkl_cylno) * spc())) {
			err_print(
			    "partition %c: specified # of blocks, %u, "
			    "is out of range\n",
			    (PARTITION_BASE+i),
			    pptr->pinfo_map[i].dkl_nblk);
			return (0);
		}
	}
	/*
	 * Lock out interrupts so the lists don't get mangled.
	 */
	enter_critical();
	/*
	 * If the old current map is unnamed, delete it.
	 */
	if (cur_parts != NULL && cur_parts != pptr &&
	    cur_parts->pinfo_name == NULL)
		delete_partition(cur_parts);
	/*
	 * Make the selected map current.
	 */
	cur_disk->disk_parts = cur_parts = pptr;

#if defined(_SUNOS_VTOC_16)
	for (i = 0; i < NDKMAP; i++)  {
		cur_parts->vtoc.v_part[i].p_start =
		    (blkaddr_t)(cur_parts->pinfo_map[i].dkl_cylno *
		    (nhead * nsect));
		cur_parts->vtoc.v_part[i].p_size =
		    (blkaddr_t)cur_parts->pinfo_map[i].dkl_nblk;
	}
#endif	/* defined(_SUNOS_VTOC_16) */

	exit_critical();
	fmt_print("\n");
	return (0);
}

/*
 * This routine implements the 'name' command.  It allows the user
 * to name the current partition map.  If the map was already named,
 * the name is changed.  Once a map is named, the values of the partitions
 * cannot be changed.  Attempts to change them will cause another map
 * to be created.
 */
int
p_name()
{
	char	*name;

	/*
	 * check if there exists a partition table for the disk.
	 */
	if (cur_parts == NULL) {
		err_print("Current Disk has no partition table.\n");
		return (-1);
	}


	/*
	 * Ask for the name.  Note that the input routine will malloc
	 * space for the name since we are using the OSTR input type.
	 */
	name = (char *)(uintptr_t)input(FIO_OSTR,
	    "Enter table name (remember quotes)",
	    ':', (u_ioparam_t *)NULL, (int *)NULL, DATA_INPUT);
	/*
	 * Lock out interrupts.
	 */
	enter_critical();
	/*
	 * If it was already named, destroy the old name.
	 */
	if (cur_parts->pinfo_name != NULL)
		destroy_data(cur_parts->pinfo_name);
	/*
	 * Set the name.
	 */
	cur_parts->pinfo_name = name;
	exit_critical();
	fmt_print("\n");
	return (0);
}


/*
 * This routine implements the 'print' command.  It lists the values
 * for all the partitions in the current partition map.
 */
int
p_print()
{
	/*
	 * check if there exists a partition table for the disk.
	 */
	if (cur_parts == NULL) {
		err_print("Current Disk has no partition table.\n");
		return (-1);
	}

	/*
	 * Print the volume name, if it appears to be set
	 */
	if (chk_volname(cur_disk)) {
		fmt_print("Volume:  ");
		print_volname(cur_disk);
		fmt_print("\n");
	}
	/*
	 * Print the name of the current map.
	 */
	if ((cur_parts->pinfo_name != NULL) && (cur_label == L_TYPE_SOLARIS)) {
		fmt_print("Current partition table (%s):\n",
		    cur_parts->pinfo_name);
		fmt_print("Total disk cylinders available: %d + %d "
		    "(reserved cylinders)\n\n", ncyl, acyl);
	} else if (cur_label == L_TYPE_SOLARIS) {
		fmt_print("Current partition table (unnamed):\n");
		fmt_print("Total disk cylinders available: %d + %d "
		    "(reserved cylinders)\n\n", ncyl, acyl);
	} else if (cur_label == L_TYPE_EFI) {
		fmt_print("Current partition table (%s):\n",
		    cur_parts->pinfo_name != NULL ?
		    cur_parts->pinfo_name : "unnamed");
		fmt_print("Total disk sectors available: %llu + %d "
		    "(reserved sectors)\n\n",
		    cur_parts->etoc->efi_last_u_lba - EFI_MIN_RESV_SIZE -
		    cur_parts->etoc->efi_first_u_lba + 1, EFI_MIN_RESV_SIZE);
	}


	/*
	 * Print the partition map itself
	 */
	print_map(cur_parts);
	return (0);
}


/*
 * Print a partition map
 */
void
print_map(struct partition_info *map)
{
	int	i;
	int	want_header;
	struct	dk_gpt *vtoc64;

	if (cur_label == L_TYPE_EFI) {
		vtoc64 = map->etoc;
		want_header = 1;
		for (i = 0; i < vtoc64->efi_nparts; i++) {
		/*
		 * we want to print partitions above 7 in expert mode only
		 * or if the partition is reserved
		 */
			if (i >= 7 && !expert_mode &&
			    ((int)vtoc64->efi_parts[i].p_tag !=
			    V_RESERVED)) {
				continue;
			}

			print_efi_partition(vtoc64, i, want_header);
			want_header = 0;
		}
		fmt_print("\n");
		return;
	}
	/*
	 * Loop through each partition, printing the header
	 * the first time.
	 */
	want_header = 1;
	for (i = 0; i < NDKMAP; i++) {
		if (i > 9) {
			break;
		}
		print_partition(map, i, want_header);
		want_header = 0;
	}

	fmt_print("\n");
}

/*
 * Print out one line of partition information,
 * with optional header for EFI type disks.
 */
/*ARGSUSED*/
void
print_efi_partition(struct dk_gpt *map, int partnum, int want_header)
{
	int		ncyl2_digits = 0;
	float		scaled;
	char		*s;
	uint64_t	secsize;

	ncyl2_digits = ndigits(map->efi_last_u_lba);
	if (want_header) {
		fmt_print("Part      ");
		fmt_print("Tag    Flag     ");
		fmt_print("First Sector");
		nspaces(ncyl2_digits);
		fmt_print("Size");
		nspaces(ncyl2_digits);
		fmt_print("Last Sector\n");
	}

	fmt_print("  %d ", partnum);
	s = find_string(ptag_choices,
	    (int)map->efi_parts[partnum].p_tag);
	if (s == (char *)NULL)
		s = "-";
	nspaces(10 - (int)strlen(s));
	fmt_print("%s", s);

	s = find_string(pflag_choices,
	    (int)map->efi_parts[partnum].p_flag);
	if (s == (char *)NULL)
		s = "-";
	nspaces(6 - (int)strlen(s));
	fmt_print("%s", s);

	nspaces(2);

	secsize = map->efi_parts[partnum].p_size;
	if (secsize == 0) {
		fmt_print("%16llu", map->efi_parts[partnum].p_start);
		nspaces(ncyl2_digits);
		fmt_print("  0     ");
	} else {
		fmt_print("%16llu", map->efi_parts[partnum].p_start);
		scaled = bn2mb(secsize);
		nspaces(ncyl2_digits - 5);
		if (scaled >= (float)1024.0 * 1024) {
			fmt_print("%8.2fTB", scaled/((float)1024.0 * 1024));
		} else if (scaled >= (float)1024.0) {
			fmt_print("%8.2fGB", scaled/(float)1024.0);
		} else {
			fmt_print("%8.2fMB", scaled);
		}
	}
	nspaces(ncyl2_digits);
	if ((map->efi_parts[partnum].p_start+secsize - 1) ==
	    UINT_MAX64) {
		fmt_print(" 0    \n");
	} else {
		fmt_print(" %llu    \n",
		    map->efi_parts[partnum].p_start+secsize - 1);
	}
}

/*
 * Print out one line of partition information,
 * with optional header.
 */
/*ARGSUSED*/
void
print_partition(struct partition_info *pinfo, int partnum, int want_header)
{
	int		i;
	blkaddr_t	nblks;
	int		cyl1;
	int		cyl2;
	float		scaled;
	int		maxcyl2;
	int		ncyl2_digits;
	char		*s;
	blkaddr_t	maxnblks = 0;
	blkaddr_t	len;

	/*
	 * To align things nicely, we need to know the maximum
	 * width of the number of cylinders field.
	 */
	maxcyl2 = 0;
	for (i = 0; i < NDKMAP; i++) {
		nblks	= (uint_t)pinfo->pinfo_map[i].dkl_nblk;
		cyl1	= pinfo->pinfo_map[i].dkl_cylno;
		cyl2	= cyl1 + (nblks / spc()) - 1;
		if (nblks > 0) {
			maxcyl2 = max(cyl2, maxcyl2);
			maxnblks = max(nblks, maxnblks);
		}
	}
	/*
	 * Get the number of digits required
	 */
	ncyl2_digits = ndigits(maxcyl2);

	/*
	 * Print the header, if necessary
	 */
	if (want_header) {
		fmt_print("Part      ");
		fmt_print("Tag    Flag     ");
		fmt_print("Cylinders");
		nspaces(ncyl2_digits);
		fmt_print("    Size            Blocks\n");
	}

	/*
	 * Print the partition information
	 */
	nblks	= pinfo->pinfo_map[partnum].dkl_nblk;
	cyl1	= pinfo->pinfo_map[partnum].dkl_cylno;
	cyl2	= cyl1 + (nblks / spc()) - 1;

	fmt_print("  %x ", partnum);

	/*
	 * Print the partition tag.  If invalid, print -
	 */
	s = find_string(ptag_choices,
	    (int)pinfo->vtoc.v_part[partnum].p_tag);
	if (s == (char *)NULL)
		s = "-";
	nspaces(10 - (int)strlen(s));
	fmt_print("%s", s);

	/*
	 * Print the partition flag.  If invalid print -
	 */
	s = find_string(pflag_choices,
	    (int)pinfo->vtoc.v_part[partnum].p_flag);
	if (s == (char *)NULL)
		s = "-";
	nspaces(6 - (int)strlen(s));
	fmt_print("%s", s);

	nspaces(2);

	if (nblks == 0) {
		fmt_print("%6d      ", cyl1);
		nspaces(ncyl2_digits);
		fmt_print("     0         ");
	} else {
		fmt_print("%6d - ", cyl1);
		nspaces(ncyl2_digits - ndigits(cyl2));
		fmt_print("%d    ", cyl2);
		scaled = bn2mb(nblks);
		if (scaled > (float)1024.0 * 1024.0) {
			fmt_print("%8.2fTB    ",
			    scaled/((float)1024.0 * 1024.0));
		} else if (scaled > (float)1024.0) {
			fmt_print("%8.2fGB    ", scaled/(float)1024.0);
		} else {
			fmt_print("%8.2fMB    ", scaled);
		}
	}
	fmt_print("(");
	pr_dblock(fmt_print, nblks);
	fmt_print(")");

	nspaces(ndigits(maxnblks/spc()) - ndigits(nblks/spc()));
	/*
	 * Allocates size of the printf format string.
	 * ndigits(ndigits(maxblks)) gives the byte size of
	 * the printf width field for maxnblks.
	 */
	len = strlen(" %") + ndigits(ndigits(maxnblks)) + strlen("d\n") + 1;
	s = zalloc(len);
	(void) snprintf(s, len, "%s%u%s", " %", ndigits(maxnblks), "u\n");
	fmt_print(s, nblks);
	(void) free(s);
}


/*
 * Return true if a disk has a volume name
 */
int
chk_volname(disk)
	struct disk_info	*disk;
{
	return (disk->v_volume[0] != 0);
}


/*
 * Print the volume name, if it appears to be set
 */
void
print_volname(disk)
	struct disk_info	*disk;
{
	int	i;
	char	*p;

	p = disk->v_volume;
	for (i = 0; i < LEN_DKL_VVOL; i++, p++) {
		if (*p == 0)
			break;
		fmt_print("%c", *p);
	}
}


/*
 * Print a number of spaces
 */
static void
nspaces(n)
	int	n;
{
	while (n-- > 0)
		fmt_print(" ");
}

/*
 * Return the number of digits required to print a number
 */
static int
ndigits(n)
	uint64_t	n;
{
	int	i;

	i = 0;
	while (n > 0) {
		n /= 10;
		i++;
	}

	return (i == 0 ? 1 : i);
}
