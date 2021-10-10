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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>

#include <sys/hypervisor.h>
#include <sys/machparam.h>
#include <xen/public/io/console.h>
#include <sys/mach_mmu.h>

shared_info_t *HYPERVISOR_shared_info;
void *HYPERVISOR_console_page;

#if defined(_BOOT)
#include "dboot/dboot_printf.h"
char big_empty[MMU_PAGESIZE * 3];	/* room for 2 page aligned page */
#endif /* _BOOT */

unsigned short video_fb_buf[32 * 1024 + MMU_PAGESIZE];
unsigned char kb_status_buf[MMU_PAGESIZE * 2];
unsigned short *video_fb = NULL;
unsigned char *kb_status = NULL;

static volatile struct xencons_interface *cons_ifp;

#define	XR_FULL(r)	((r)->xr_in_cnt - (r)->xr_out_cnt == XR_SIZE)
#define	XR_EMPTY(r)	((r)->xr_in_cnt == (r)->xr_out_cnt)

#define	PTE_BITS	(PT_VALID | PT_WRITABLE)
#define	PTE_DEV_BITS	(PT_VALID | PT_WRITABLE | PT_NOCACHE | PT_NOCONSIST | \
			PT_FOREIGN)

/*
 * For some unfortunate reason, the hypervisor doesn't bother to include the
 * shared info in the original virtual address space.  This means we can't
 * do any console I/O until we have manipulated some pagetables. So we have to
 * do this bit of code with no ability to get debug output.
 */
/*ARGSUSED*/
void
bcons_init_xen(char *cmdline)
{
#ifdef _BOOT
	uintptr_t vaddr;

	/*
	 * find a page aligned virtual address in "big_empty"
	 */
	vaddr = (uintptr_t)&big_empty;
	vaddr = (vaddr + MMU_PAGEOFFSET) & MMU_PAGEMASK;
	HYPERVISOR_shared_info = (shared_info_t *)vaddr;

	/*
	 * Sets the "present" and "writable" bits in the PTE
	 * plus user for amd64.
	 */
	(void) HYPERVISOR_update_va_mapping(vaddr,
	    xen_info->shared_info | PTE_BITS, UVMF_INVLPG | UVMF_LOCAL);

	/*
	 * map the xen console ring buffers
	 */
	(void) HYPERVISOR_update_va_mapping(vaddr + MMU_PAGESIZE,
	    mmu_ptob((x86pte_t)xen_info->console.domU.mfn) | PTE_BITS,
	    UVMF_INVLPG | UVMF_LOCAL);

#endif /* _BOOT */
	HYPERVISOR_console_page =
	    (void *)((uintptr_t)HYPERVISOR_shared_info + MMU_PAGESIZE);
}


/*
 * This is the equivalent of polled I/O across the hypervisor CONSOLE
 * channel to output 1 character at a time.
 */
void
bcons_putchar_xen(int c)
{
	evtchn_send_t send;

	cons_ifp = (volatile struct xencons_interface *)HYPERVISOR_console_page;

	/*
	 * need to add carriage return for new lines
	 */
	if (c == '\n')
		bcons_putchar_xen('\r');

	/*
	 * We have to wait till we have an open transmit slot.
	 */
	while (cons_ifp->out_prod - cons_ifp->out_cons >=
	    sizeof (cons_ifp->out))
		(void) HYPERVISOR_yield();

	cons_ifp->out[MASK_XENCONS_IDX(cons_ifp->out_prod, cons_ifp->out)] =
	    (char)c;
	++cons_ifp->out_prod;

	/*
	 * Signal Domain 0 that it has something to do for us.
	 */
	send.port = xen_info->console.domU.evtchn;
	(void) HYPERVISOR_event_channel_op(EVTCHNOP_send, &send);
}

static uint_t have_char = 0;
static char buffered;

/*
 * See if there is a character on input.
 */
int
bcons_ischar_xen(void)
{

	cons_ifp = (volatile struct xencons_interface *)HYPERVISOR_console_page;
	if (cons_ifp->in_cons == cons_ifp->in_prod)
		return (0);
	return (1);
}

/*
 * get a console input character
 */
int
bcons_getchar_xen(void)
{
	evtchn_send_t send;
	char c;

	cons_ifp = (volatile struct xencons_interface *)HYPERVISOR_console_page;
	while (cons_ifp->in_cons == cons_ifp->in_prod)
		(void) HYPERVISOR_yield();

	c = cons_ifp->in[MASK_XENCONS_IDX(cons_ifp->in_cons, cons_ifp->in)];
	++cons_ifp->in_cons;

	/*
	 * Signal Domain 0 that we ate a character.
	 */
	send.port = xen_info->console.domU.evtchn;
	(void) HYPERVISOR_event_channel_op(EVTCHNOP_send, &send);
	return (c);
}
