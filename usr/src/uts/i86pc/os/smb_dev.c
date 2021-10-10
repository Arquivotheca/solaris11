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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Platform-Specific SMBIOS Subroutines
 *
 * The routines in this file form part of <sys/smbios_impl.h> and combine with
 * the usr/src/common/smbios code to form an in-kernel SMBIOS decoding service.
 * The SMBIOS entry point is locating by scanning a range of physical memory
 * assigned to BIOS as described in Section 2 of the DMTF SMBIOS specification.
 */

#include <sys/smbios_impl.h>
#include <sys/sysmacros.h>
#include <sys/errno.h>
#include <sys/psm.h>
#include <sys/smp_impldefs.h>

smbios_hdl_t *ksmbios;
int ksmbios_flags;

smbios_hdl_t *
smb_open_error(smbios_hdl_t *shp, int *errp, int err)
{
	if (shp != NULL)
		smbios_close(shp);

	if (errp != NULL)
		*errp = err;

	if (ksmbios == NULL)
		cmn_err(CE_CONT, "?SMBIOS not loaded (%s)", smbios_errmsg(err));

	return (NULL);
}

smbios_hdl_t *
smbios_open(const char *file, int version, int flags, int *errp)
{
	smbios_hdl_t *shp = NULL;
	smbios_entry_t *ep;
	caddr_t stbuf, pmem, p, q;
	size_t pmemlen;
	int err;
	uint64_t startaddr, startoff = 0;

	if (file != NULL || (flags & ~SMB_O_MASK))
		return (smb_open_error(shp, errp, ESMB_INVAL));

	/*
	 * If there is a smbios-address property, use the physical address
	 * specified there as the entry-point structure.
	 */
	if ((startaddr = ddi_prop_get_int64(DDI_DEV_T_ANY, ddi_root_node(),
	    DDI_PROP_DONTPASS, "smbios-address", 0)) == 0) {
		startaddr = SMB_RANGE_START;
		pmemlen = SMB_RANGE_LIMIT - SMB_RANGE_START + 1;
	} else {
		/*
		 * Never attempt to map more than a single page if the
		 * smbios anchor address is specified, since memory
		 * beyond a single page may not be reserved (i.e. it
		 * may be in use or in the free list).
		 */
		pmemlen = MMU_PAGESIZE;

		startoff = startaddr & MMU_PAGEOFFSET;

		/*
		 * Make sure that startaddr starts on a page boundary so that
		 * psm_map_phys does not map more than one page.
		 */
		startaddr &= MMU_PAGEMASK;

#if defined(__i386)
		if (startaddr > UINT32_MAX) {
			startaddr = SMB_RANGE_START;
			pmemlen = SMB_RANGE_LIMIT - SMB_RANGE_START + 1;
		}
#endif
	}

	pmem = psm_map_phys_new(startaddr, pmemlen, PSM_PROT_READ);
	if (pmem == NULL)
		return (smb_open_error(shp, errp, ESMB_MAPDEV));

	for (p = pmem + startoff, q = pmem + pmemlen - startoff; p < q;
	    p += 16) {
		if (strncmp(p, SMB_ENTRY_EANCHOR, SMB_ENTRY_EANCHORLEN) == 0)
			break;
	}

	if (p >= q) {
		psm_unmap_phys(pmem, pmemlen);
		return (smb_open_error(shp, errp, ESMB_NOTFOUND));
	}

	ep = smb_alloc(SMB_ENTRY_MAXLEN);
	bcopy(p, ep, sizeof (smbios_entry_t));
	ep->smbe_elen = MIN(ep->smbe_elen, SMB_ENTRY_MAXLEN);
	bcopy(p, ep, ep->smbe_elen);

	psm_unmap_phys(pmem, pmemlen);
	pmem = psm_map_phys(ep->smbe_staddr, ep->smbe_stlen, PSM_PROT_READ);

	if (pmem == NULL) {
		smb_free(ep, SMB_ENTRY_MAXLEN);
		return (smb_open_error(shp, errp, ESMB_MAPDEV));
	}

	stbuf = smb_alloc(ep->smbe_stlen);
	bcopy(pmem, stbuf, ep->smbe_stlen);
	psm_unmap_phys(pmem, ep->smbe_stlen);
	shp = smbios_bufopen(ep, stbuf, ep->smbe_stlen, version, flags, &err);

	if (shp == NULL) {
		smb_free(stbuf, ep->smbe_stlen);
		smb_free(ep, SMB_ENTRY_MAXLEN);
		return (smb_open_error(shp, errp, err));
	}

	if (ksmbios == NULL) {
		cmn_err(CE_CONT, "?SMBIOS v%u.%u loaded (%u bytes)",
		    ep->smbe_major, ep->smbe_minor, ep->smbe_stlen);
	}

	shp->sh_flags |= SMB_FL_BUFALLOC;
	smb_free(ep, SMB_ENTRY_MAXLEN);

	return (shp);
}

/*ARGSUSED*/
smbios_hdl_t *
smbios_fdopen(int fd, int version, int flags, int *errp)
{
	return (smb_open_error(NULL, errp, ENOTSUP));
}

/*ARGSUSED*/
int
smbios_write(smbios_hdl_t *shp, int fd)
{
	return (smb_set_errno(shp, ENOTSUP));
}
