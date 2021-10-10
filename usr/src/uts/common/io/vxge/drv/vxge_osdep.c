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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :	vxge_osdep.c
 *
 * Description:  OS dependent function definition
 *
 * Created:	  7 June 2004
 */

#include "vxgehal.h"
#include "vxge_osdep.h"
#include <sys/strsun.h>

u64
vxge_os_htonll(u64 host_longlong)
{
#ifdef _BIG_ENDIAN
	return (host_longlong);
#else
	return ((((u64)htonl(host_longlong)) << 32) +
	    htonl(host_longlong >> 32));
#endif
}

u64
vxge_os_ntohll(u64 host_longlong)
{
#ifdef _BIG_ENDIAN
	return (host_longlong);
#else
	return ((((u64)ntohl(host_longlong)) << 32) +
	    ntohl(host_longlong >> 32));
#endif

}

#ifndef __GNUC__
void vxge_os_printf(char *fmt, ...) {
	int len = vxge_os_strlen(fmt);
	char *ptr = kmem_alloc(len+2, KM_NOSLEEP);
	(void) sprintf(ptr, "?%s", fmt);
	vxge_os_vaprintfs(NULL, 0, ptr);
	kmem_free(ptr, len+2);
}

int vxge_os_sprintf(char *buf, char *fmt, ...) {
	vxge_os_vasprintf(NULL, 0, buf, fmt);
	return ((unsigned int)strlen(buf));
}
#endif

void vxge_os_dma_malloc_async(pci_dev_h pdev, void *devh,
	unsigned long size, int dma_flags)
{
	pci_dma_h dma_h;
	pci_dma_acc_h acc_handle;
	void *block_addr;

	block_addr = vxge_os_dma_malloc(pdev, size, dma_flags,
	    &dma_h, &acc_handle);

	vxge_hal_blockpool_block_add(devh, block_addr, size,
	    dma_h, acc_handle);
}

/*ARGSUSED*/
void vxge_os_dma_free_ex(pci_dev_h pdev, const void *vaddr, int size,
	u32 flags, pci_dma_h *p_dmah, pci_dma_acc_h *p_dma_acch,
	const char *file, int line)
{
	VXGE_OS_MEMORY_CHECK_FREE(vaddr, size, file, line);
	ddi_dma_mem_free(p_dma_acch);
	ddi_dma_free_handle(p_dmah);
}

dma_addr_t vxge_os_dma_map(pci_dev_h pdev, pci_dma_h dmah,
	void *vaddr, size_t size, int dir, int dma_flags) {
	int ret;
	uint_t flags;
	uint_t ncookies;
	ddi_dma_cookie_t dma_cookie;

	switch (dir) {
	case VXGE_OS_DMA_DIR_TODEVICE:
		flags = DDI_DMA_WRITE;
		break;
	case VXGE_OS_DMA_DIR_FROMDEVICE:
		flags = DDI_DMA_READ;
		break;
	case VXGE_OS_DMA_DIR_BIDIRECTIONAL:
		flags = DDI_DMA_RDWR;
		break;
	default:
		return (0);
	}

	flags |= (dma_flags & VXGE_OS_DMA_CONSISTENT) ?
		DDI_DMA_CONSISTENT : DDI_DMA_STREAMING;

	ret = ddi_dma_addr_bind_handle(dmah, NULL, vaddr, size, flags,
		DDI_DMA_DONTWAIT, 0, &dma_cookie, &ncookies);
	if (ret != DDI_SUCCESS) {
		return (0);
	}

	if (ncookies != 1 || dma_cookie.dmac_size < size) {
		(void) ddi_dma_unbind_handle(dmah);
		return (0);
	}

	return (dma_cookie.dmac_laddress);
}

/*ARGSUSED*/
void vxge_os_dma_unmap(pci_dev_h pdev, pci_dma_h dmah,
	dma_addr_t dma_addr, size_t size, int dir)
{
	(void) ddi_dma_unbind_handle(dmah);
}

/*ARGSUSED*/
void vxge_os_dma_sync(pci_dev_h pdev, pci_dma_h dmah,
	dma_addr_t dma_addr, u64 dma_offset, size_t length, int dir)
{
	(void) ddi_dma_sync(dmah, dma_offset, length, dir);
}

/*ARGSUSED*/
void *vxge_os_dma_malloc_ex(pci_dev_h pdev, unsigned long size,
	int dma_flags, pci_dma_h *p_dmah, pci_dma_acc_h *p_dma_acch,
	const char *file, int line)
{
	void *vaddr;
	int ret;
	size_t real_size;
	extern ddi_device_acc_attr_t *p_vxge_dev_attr;
	extern struct ddi_dma_attr *p_vxge_hal_dma_attr;

	ret = ddi_dma_alloc_handle(pdev, p_vxge_hal_dma_attr,
	    DDI_DMA_DONTWAIT, 0, p_dmah);
	if (ret != DDI_SUCCESS) {
		return (NULL);
	}

	ret = ddi_dma_mem_alloc(*p_dmah, size, p_vxge_dev_attr,
	    (dma_flags & VXGE_OS_DMA_CONSISTENT ?
	    DDI_DMA_CONSISTENT : DDI_DMA_STREAMING), DDI_DMA_DONTWAIT, 0,
	    (caddr_t *)(void *)&vaddr, &real_size, p_dma_acch);
	if (ret != DDI_SUCCESS) {
		ddi_dma_free_handle(p_dmah);
		return (NULL);
	}

	if (size > real_size) {
		ddi_dma_mem_free(p_dma_acch);
		ddi_dma_free_handle(p_dmah);
		return (NULL);
	}

	VXGE_OS_MEMORY_CHECK_MALLOC((void *) vaddr, size, file, line);

	return (vaddr);
}

void *
vxge_mem_alloc_ex(u32 size, const char *file, int line)
{
	void *vaddr = NULL;
	vaddr = kmem_alloc(size, KM_NOSLEEP);
	if (NULL != vaddr) {
		VXGE_OS_MEMORY_CHECK_MALLOC((void *) vaddr, size, file, line);
		vxge_os_memzero(vaddr, size);
	}
	return (vaddr);
}

void
vxge_mem_free_ex(const void *vaddr, u32 size,
	const char *file, int line)
{
	if (NULL != vaddr) {
		VXGE_OS_MEMORY_CHECK_FREE(vaddr, size, file, line);
		kmem_free((void *)vaddr, size);
	}
}

/*ARGSUSED*/
int
vxge_check_dma_handle(pci_dev_h pdev, ddi_dma_handle_t handle)
{
	ddi_fm_error_t err;

	ddi_fm_dma_err_get(handle, &err, DDI_FME_VERSION);
	if (err.fme_status != DDI_FM_OK) {
		ddi_fm_service_impact(pdev, DDI_SERVICE_DEGRADED);
		return (1);
	}
	return (0);
}
