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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Glue code between vbiosd and the x86emu engine. In particular we emulate
 * the real memory ranges we interested into (e.g. Video BIOS, System BIOS,
 * Interrupt Vector Table, etc.) and we provide the code to kick-off an
 * int 10h call and to emulate other interrupts during execution.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <fcntl.h>

#include "vbiosd.h"
#include "vbiosd_x86emu.h"

#define	LOW_MEM_BASE			(0x00000)
#define	LOW_MEM_SIZE			(0x01000)
#define	DEFAULT_STACK_SIZE		(0x02000)
#define	EBDA_BASE			(0x9fc00)
#define	EBDA_DEFAULT_END		(0x9ffff)
#define	REAL_MEM_BASE			(0x10000)
#define	REAL_MEM_SIZE			(EBDA_BASE - REAL_MEM_BASE)
#define	VRAM_BASE			(0xa0000)
#define	VRAM_SIZE			(0x20000)
#define	SBIOS_SIZE			(0x20000)
#define	SBIOS_BASE			(0xe0000)
#define	VBIOS_BASE			(0xc0000)

/* Within the BIOS Data Area. */
#define	EBDA_OFFSET			(0x40e)

/* Upper Memory Area mappings in the range 0xC0000 - 0xE0000. */
#define	UMA_MAP_ENTRIES			(32)
#define	UMA_PAGE_SIZE			(4096)
#define	UMA_PAGE_SHIFT			(12)
/*
 * 32 pages from 0xC0000 to 0xE0000. The first ones should never be hit because
 * they should contain the VBIOS ROM. If more than 32 are needed, uma_alloc_mask
 * must be bumped up to a 64-bit value.
 */
static char		*uma_mapping[UMA_MAP_ENTRIES];
static uint32_t		uma_alloc_mask;
static char		*ivt_bda;
static char		*vram;
static char		*vbios;
static char		*sbios;
static char		*ebda;
static char		real_mem[REAL_MEM_SIZE];
static char		fake_rw_frame[4];
static uint32_t		halt;
static uint32_t		stack;

static uint32_t		vbios_size;
static uint32_t		ebda_start;
static uint32_t		ebda_end;
static uint32_t		ebda_size;
static uint32_t		ebda_diff;

static boolean_t	ebda_initialized = B_FALSE;
static boolean_t	ebda_init_failed = B_FALSE;

static boolean_t	x86emu_engine_initialized = B_FALSE;
static int		xsvc_fd = -1;

#define	XSVC_DEVICE	"/dev/xsvc"

static int open_xsvc();
static void close_xsvc();
static void *translate_addr(uint32_t);
static int read_from_mem(uint32_t, uint32_t, void *);
static void *map_ebda_range();
static void *map_uma_page(uint32_t);
static void *handle_ebda_fault(uint32_t);
static void *handle_uma_fault(uint32_t);
static int vbiosd_emulated_mem_setup();
static void recap_real_mappings();
static void clear_mappings();
static void x86emu_emulate_int(int);
void vbiosd_x86emu_cleanup();
static void ctx_to_x86emu(struct vbiosd_ctx *);
static void x86emu_to_ctx(struct vbiosd_ctx *);


/* Operations to and from physical memory go through /dev/xsvc mappings. */
static int
open_xsvc()
{
	if (xsvc_fd != -1) {
		vbiosd_debug(NO_ERRNO, "%s already opened, nothing to do",
		    XSVC_DEVICE);
		return (VBIOSD_SUCCESS);
	}

	/* O_RDWR to catch all possible use cases. */
	xsvc_fd = open(XSVC_DEVICE, O_RDWR);
	if (xsvc_fd == -1) {
		vbiosd_error(LOG_ERRNO, "unable to open %s", XSVC_DEVICE);
		return (VBIOSD_ERROR);
	}

	return (VBIOSD_SUCCESS);
}

static void
close_xsvc()
{
	if (xsvc_fd != -1)
		(void) close(xsvc_fd);

	xsvc_fd = -1;
}

/*
 * Translate emulated memory accesses into the correct mapping.
 * For EBDA and high UMA addresses (0xC8000 - 0xE0000) do the mapping on
 * demand at the first access.
 */
static void *
translate_addr(uint32_t addr)
{
	void	*retptr = NULL;

	if (addr >= REAL_MEM_BASE && addr < REAL_MEM_BASE + REAL_MEM_SIZE)
		retptr = real_mem + addr - REAL_MEM_BASE;
	else if (addr >= VBIOS_BASE && addr < VBIOS_BASE + vbios_size)
		retptr = vbios + addr - VBIOS_BASE;
	else if (addr >= SBIOS_BASE && addr < SBIOS_BASE + SBIOS_SIZE)
		retptr = sbios + addr - SBIOS_BASE;
	else if (addr >= VRAM_BASE && addr < VRAM_BASE + VRAM_SIZE)
		retptr = vram + addr - VRAM_BASE;
	else if (addr < LOW_MEM_SIZE)
		retptr = ivt_bda + addr;
	else if (addr >= ebda_start && addr < ebda_end)
		retptr = handle_ebda_fault(addr);
	else if (addr >= VBIOS_BASE + vbios_size && addr < SBIOS_BASE)
		retptr = handle_uma_fault(addr);
	else
		vbiosd_error(NO_ERRNO, "the emulator attempted to access memory"
		    " outside the mapped range using addr %x", addr);

	if (retptr == NULL) {
		/*
		 * X86EMU_halt_sys() is not exactly a terminating function,
		 * it simply sets a flag which will be evaluated at the next
		 * instruction decode. In order to get to a clean exit,
		 * an address pointing to a dummy read/write aread is returned.
		 */
		retptr = fake_rw_frame;
		X86EMU_halt_sys();
	}

	return (retptr);
}

static int
read_from_mem(uint32_t addr, uint32_t len, void *buf)
{
	char		*tmp_addr;
	uint32_t	base, size, diff = 0;

	if (xsvc_fd == -1)
		if (open_xsvc() == VBIOSD_ERROR)
			return (VBIOSD_ERROR);

	size = len;
	base = P2ALIGN(addr, getpagesize());
	if (base != addr) {
		diff = addr - base;
		size += diff;
	}

	size = P2ROUNDUP(size, getpagesize());
	tmp_addr = mmap(NULL, size, PROT_READ, MAP_SHARED, xsvc_fd, addr);
	if (tmp_addr == MAP_FAILED) {
		vbiosd_debug(LOG_ERRNO, "mmap from %s, at %p of %d bytes "
		    "failed", XSVC_DEVICE, addr, size);
		return (VBIOSD_ERROR);
	}

	(void) memcpy(buf, tmp_addr + diff, len);
	(void) munmap(tmp_addr, size);

	return (VBIOSD_SUCCESS);
}

static void *
map_ebda_range()
{
	char		*addr;
	uint8_t		tmp[1];
	uint32_t	diff = 0;

	if (read_from_mem(ebda_start, 1, tmp) == VBIOSD_ERROR) {
		vbiosd_warn(NO_ERRNO, "failed to read EBDA size from %x."
		    " Ignoring EBDA.", ebda_start);
		return (NULL);
	}

	/* The first byte in the EBDA is its size in kB */
	ebda_size = ((uint32_t)tmp[0]) << 10;
	if (ebda_start + ebda_size > VRAM_BASE) {
		vbiosd_warn(NO_ERRNO, "EBDA too big (%x), truncating.",
		    ebda_size);
		ebda_size = VRAM_BASE - ebda_start;
	}
	ebda_end = ebda_start + ebda_size - 1;

	/* Map the EBDA */
	vbiosd_debug(NO_ERRNO, "found EBDA at %5x-%5x", ebda_start,
	    ebda_end);
	diff = ebda_start & -getpagesize();

	if (diff) {
		ebda_diff = ebda_start - diff;
	}

	addr = mmap(NULL, ebda_size + ebda_diff, PROT_READ | PROT_WRITE,
	    MAP_SHARED, xsvc_fd, ebda_start - ebda_diff);
	if (addr == MAP_FAILED) {
		vbiosd_warn(LOG_ERRNO, "failed to mmap EBDA");
		return (NULL);
	}
	return (addr);
}

static void *
handle_ebda_fault(uint32_t addr)
{
	if (ebda_init_failed == B_TRUE)
		return (NULL);

	if (ebda_initialized == B_FALSE) {
		vbiosd_debug(NO_ERRNO, "Access to the EBDA area requested");
		if ((ebda = map_ebda_range()) == NULL) {
			ebda_init_failed = B_TRUE;
			return (NULL);
		}
		vbiosd_debug(NO_ERRNO, "EBDA mapped successfully");
		ebda_initialized = B_TRUE;
	}
	return (ebda + addr - ebda_start + ebda_diff);
}

/*
 * BIOS Upper Memory Area handling.
 *
 * Some cards use more than the "reserved" space for the VBIOS (0xC0000 -
 * 0xC8000) and access other portions in the Upper Memory Area. To prevent
 * mapping unneeded regions, only the page containing the request address is
 * mapped, and its translation is mantained in the uma_mapping array.
 * A bitmap stored in uma_alloc_mask keeps track of the pages for which a
 * valid translation is already present.
 */

static void *
handle_uma_fault(uint32_t addr)
{
	int		index;
	uint32_t	page;
	uint32_t	offset;

	page = P2ALIGN(addr, UMA_PAGE_SIZE);
	offset = addr - page;
	index = (page >> UMA_PAGE_SHIFT) - 0xC0;

	if (!(uma_alloc_mask & (1 << index))) {
		vbiosd_debug(NO_ERRNO, "Hit UMA fault at %x", addr);
		uma_mapping[index] = map_uma_page(page);
		vbiosd_debug(NO_ERRNO, "UMA %x maps to %p", page,
		    uma_mapping[index]);
		uma_alloc_mask |= (1 << index);
	}

	if (uma_mapping[index] == NULL)
		return (NULL);

	return (uma_mapping[index] + offset);
}

static void *
map_uma_page(uint32_t page)
{
	void	*addr;

	addr = mmap(NULL, UMA_PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
	    xsvc_fd, page);
	if (addr == MAP_FAILED) {
		vbiosd_warn(LOG_ERRNO, "failed to mmap UMA page %x", page);
		return (NULL);
	}

	return (addr);
}

/* Setup mappings for the ranges we are interested into. */
static int
vbiosd_emulated_mem_setup()
{
	uint8_t		tmp[4];

	bzero(tmp, sizeof (tmp));
	if (open_xsvc() == VBIOSD_ERROR)
		goto out;

	ivt_bda = mmap(NULL, LOW_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
	    xsvc_fd, LOW_MEM_BASE);
	if (ivt_bda == MAP_FAILED) {
		vbiosd_error(LOG_ERRNO, "unable to map low memory");
		goto out_fd;
	}

	/*
	 * Check the Video BIOS signature (0xAA55) and, if a valid VBIOS is
	 * present, read its size and map it in memory.
	 */
	read_from_mem(VBIOS_BASE, 4, tmp);
	if (tmp[0] != 0x55 || tmp[1] != 0xAA) {
		vbiosd_error(NO_ERRNO, "video BIOS not found at %x",
		    VBIOS_BASE);
		goto out_mappings;
	}

	vbios_size = tmp[2] << 9;
	vbios = mmap(NULL, vbios_size, PROT_READ, MAP_SHARED, xsvc_fd,
	    VBIOS_BASE);

	if (vbios == MAP_FAILED) {
		vbiosd_error(LOG_ERRNO, "failed to mmap the Video BIOS");
		goto out_mappings;
	}

	/* Map the System BIOS */
	sbios = mmap(NULL, SBIOS_SIZE, PROT_READ, MAP_SHARED, xsvc_fd,
	    SBIOS_BASE);
	if (sbios == MAP_FAILED) {
		vbiosd_error(LOG_ERRNO, "failed to mmap the System BIOS");
		goto out_mappings;
	}

	/* Map the Video RAM area in memory. */
	vram = mmap(NULL, VRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
	    xsvc_fd, VRAM_BASE);
	if (vram == MAP_FAILED) {
		vbiosd_error(LOG_ERRNO, "failed to mmap the Video RAM");
		goto out_mappings;
	}

	/*
	 * EBDA might or might not be available, but we cannot afford to panic
	 * on a free page mapping in /dev/xsvc. Record the 'usual' EBDA span and
	 * defer mapping it only if the card accesses it.
	 */
	ebda_start = (* (uint16_t *)(ivt_bda + EBDA_OFFSET)) << 4;
	if (!ebda_start || ebda_start > EBDA_BASE)
		ebda_start = EBDA_BASE;

	ebda_end = EBDA_DEFAULT_END;
	if (vbiosd_debug_report)
		recap_real_mappings();

	x86emu_engine_initialized = B_TRUE;
	return (VBIOSD_SUCCESS);

out_mappings:
	clear_mappings();
out_fd:
	close_xsvc();
out:
	return (VBIOSD_ERROR);
}


static void
recap_real_mappings()
{
	vbiosd_debug(NO_ERRNO, "IVT and BDA at %5x-%5x (mapped at: %p)",
	    LOW_MEM_BASE, LOW_MEM_BASE + LOW_MEM_SIZE - 1, ivt_bda);
	vbiosd_debug(NO_ERRNO, "VBIOS at %5x-%5x (mapped at: %p)", VBIOS_BASE,
	    VBIOS_BASE + vbios_size - 1, vbios);
	vbiosd_debug(NO_ERRNO, "SBIOS at %5x-%5x (mapped at: %p)", SBIOS_BASE,
	    SBIOS_BASE + SBIOS_SIZE - 1, sbios);
	vbiosd_debug(NO_ERRNO, "VRAM at %5x-%5x (mapped at: %p)", VRAM_BASE,
	    VRAM_BASE + VRAM_SIZE - 1, vram);
	vbiosd_debug(NO_ERRNO, "Scratch Real Memory at %5x-%5x (mapped at: %p)",
	    REAL_MEM_BASE, REAL_MEM_BASE + REAL_MEM_SIZE - 1, real_mem);
}

static void
clear_mappings()
{
	int	i;

	if (ivt_bda) {
		(void) munmap(ivt_bda, LOW_MEM_SIZE);
		ivt_bda = NULL;
	}

	if (ebda) {
		(void) munmap(ebda, ebda_size + ebda_diff);
		ebda = NULL;
	}

	if (vram) {
		(void) munmap(vram, VRAM_SIZE);
		vram = NULL;
	}

	if (vbios) {
		(void) munmap(vbios, vbios_size);
		vbios = NULL;
	}

	if (sbios) {
		(void) munmap(sbios, SBIOS_SIZE);
		sbios = NULL;
	}

	for (i = 0; i < UMA_MAP_ENTRIES; i++) {
		if (uma_mapping[i] != NULL) {
			(void) munmap(uma_mapping[i], UMA_PAGE_SIZE);
			uma_mapping[i] = NULL;
		}
	}
	uma_alloc_mask = 0;
}


/* Memory access stubs for X86EMU_MemFuncs. */
static uint8_t
mem_rdb(uint32_t addr)
{
	uint8_t	val;

	val = *(uint8_t *)translate_addr(addr);
	return (val);
}

static uint16_t
mem_rdw(uint32_t addr)
{
	uint16_t val;

	val = *(uint16_t *)translate_addr(addr);
	return (val);
}

static uint32_t
mem_rdl(uint32_t addr)
{
	uint32_t val;

	val = *(uint32_t *)translate_addr(addr);
	return (val);
}

static void
mem_wrb(uint32_t addr, uint8_t val)
{
	uint8_t *t = translate_addr(addr);
	*t = val;
}

static void
mem_wrw(uint32_t addr, uint16_t val)
{
	uint16_t *t = translate_addr(addr);
	*t = val;
}

static void
mem_wrl(uint32_t addr, uint32_t val)
{
	uint32_t *t = translate_addr(addr);
	*t = val;
}

/* in/out helper stubs fro X86EMU_PioFuncs. */

static void
pio_outb(uint16_t port, uint8_t value)
{
	__asm__("outb %b0, %w1" :: "a"(value), "d"(port));
}

static uint8_t
pio_inb(uint16_t port)
{
	uint8_t 	value;
	__asm__("inb %w1, %b0" : "=a"(value) : "d"(port));
	return (value);
}

static void
pio_outw(uint16_t port, uint16_t value)
{
	__asm__("outw %w0, %w1" :: "a"(value), "d"(port));
}

static uint16_t
pio_inw(uint16_t port)
{
	uint16_t 	value;
	__asm__("inw %w1, %w0" : "=a"(value) : "d"(port));
	return (value);
}

static void
pio_outl(uint16_t port, uint32_t value)
{
	__asm__("outl %0, %w1" :: "a"(value), "d"(port));
}

static uint32_t
pio_inl(uint16_t port)
{
	uint32_t 	value;
	__asm__("inl %w1, %0" : "=a"(value) : "d"(port));
	return (value);
}

static void
pushw(uint16_t val)
{
	X86_ESP -= 2;
	mem_wrw(((uint32_t)X86_SS << 4) + X86_SP, val);
}

static void
x86emu_emulate_int(int num)
{
	uint32_t eflags;

	eflags = X86_EFLAGS;

	/* Return address and flags */
	pushw(eflags);
	pushw(X86_CS);
	pushw(X86_IP);

	X86_EFLAGS = X86_EFLAGS & ~(X86_VIF_MASK | X86_TF_MASK);
	X86_CS = mem_rdw((num << 2) + 2);
	X86_IP = mem_rdw((num << 2));
}

int
vbiosd_x86emu_setup()
{
	int			i = 0;
	X86EMU_intrFuncs 	intFuncs[256];
	X86EMU_pioFuncs 	pioFuncs = {
	    .inb = &pio_inb,
	    .inw = &pio_inw,
	    .inl = &pio_inl,
	    .outb = &pio_outb,
	    .outw = &pio_outw,
	    .outl = &pio_outl,
	};

	X86EMU_memFuncs 	memFuncs = {
	    .rdb = &mem_rdb,
	    .rdw = &mem_rdw,
	    .rdl = &mem_rdl,
	    .wrb = &mem_wrb,
	    .wrw = &mem_wrw,
	    .wrl = &mem_wrl,
	};

	if (vbiosd_emulated_mem_setup() == VBIOSD_ERROR) {
		vbiosd_error(NO_ERRNO, "unable to set up memory emulation");
		return (VBIOSD_ERROR);
	}

	/*
	 * Place the stack somewhere in real memory. We place it at the start
	 * of the real memory segment.
	 */
	stack = REAL_MEM_BASE;

	X86_SS = stack >> 4;
	X86_ESP = DEFAULT_STACK_SIZE;

	/* Create a fake 'HLT' instruction to stop the emulation. */
	halt = REAL_MEM_BASE + DEFAULT_STACK_SIZE;
	mem_wrb(halt, 0xF4);

	X86EMU_setupPioFuncs(&pioFuncs);
	X86EMU_setupMemFuncs(&memFuncs);

	/* Setup interrupt handlers */
	for (i = 0; i < 256; i++) {
		intFuncs[i] = x86emu_emulate_int;
	}
	X86EMU_setupIntrFuncs(intFuncs);

	/* Set the default flags */
	X86_EFLAGS = X86_IF_MASK | X86_IOPL_MASK;

	return (VBIOSD_SUCCESS);
}

void
vbiosd_x86emu_cleanup()
{
	if (x86emu_engine_initialized == B_FALSE)
		return;

	close_xsvc();
	clear_mappings();
}

static void
ctx_to_x86emu(struct vbiosd_ctx *ctx)
{
	X86_EAX = ctx->eax;
	X86_EBX = ctx->ebx;
	X86_ECX = ctx->ecx;
	X86_EDX = ctx->edx;
	X86_EDI = ctx->edi;
	X86_ESI = ctx->esi;
	X86_EBP = ctx->ebp;
	X86_ESP = ctx->esp;
	X86_EFLAGS = ctx->eflags;
	X86_EIP = ctx->eip;
	X86_CS  = ctx->cs;
	X86_DS  = ctx->ds;
	X86_ES  = ctx->es;
	X86_FS  = ctx->fs;
	X86_GS  = ctx->gs;
}

static void
x86emu_to_ctx(struct vbiosd_ctx *ctx)
{
	ctx->eax = X86_EAX;
	ctx->ebx = X86_EBX;
	ctx->ecx = X86_ECX;
	ctx->edx = X86_EDX;
	ctx->edi = X86_EDI;
	ctx->esi = X86_ESI;
	ctx->ebp = X86_EBP;
	ctx->esp = X86_ESP;
	ctx->eflags = X86_EFLAGS;
	ctx->eip = X86_EIP;
	ctx->cs  = X86_CS;
	ctx->ds  = X86_DS;
	ctx->es  = X86_ES;
	ctx->fs  = X86_FS;
	ctx->gs  = X86_GS;
}

/* Emulate an INT 10h BIOS call. */
int
vbiosd_do_int10(struct vbiosd_ctx *ctx)
{
	ctx_to_x86emu(ctx);

	X86_GS = 0;
	X86_FS = 0;
	X86_DS = 0x0040;
	X86_CS  = mem_rdw((0x10 << 2) + 2);
	X86_EIP = mem_rdw((0x10 << 2));
	X86_SS = stack >> 4;
	X86_ESP = DEFAULT_STACK_SIZE;
	X86_EFLAGS = X86_IF_MASK | X86_IOPL_MASK;

	pushw(X86_EFLAGS);
	pushw((halt >> 4));
	pushw(0x0);

	X86EMU_exec();

	x86emu_to_ctx(ctx);
	return (0);
}
