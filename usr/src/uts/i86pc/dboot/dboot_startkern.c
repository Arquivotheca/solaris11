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
#include <sys/machparam.h>
#include <sys/x86_archext.h>
#include <sys/systm.h>
#include <sys/mach_mmu.h>
#include <sys/multiboot.h>
#include <sys/multiboot2.h>
#include <sys/multiboot2_impl.h>

#if defined(__xpv)

#include <sys/hypervisor.h>
uintptr_t xen_virt_start;
pfn_t *mfn_to_pfn_mapping;

#else /* !__xpv */

#if defined(_BOOT_TARGET_amd64)
extern uint32_t mb2_load_addr;	/* == mb_header.load_addr from multiboot1 */
#endif

extern char _end[];
extern multiboot_header_t mb_header;
extern int have_cpuid(void);
extern void *memmove(void *s1, const void *s2, size_t sz);

#endif /* !__xpv */

#include <sys/inttypes.h>
#include <sys/bootinfo.h>
#include <sys/mach_mmu.h>
#include <sys/boot_console.h>
#include <sys/vbe.h>

#include "dboot_asm.h"
#include "dboot_printf.h"
#include "dboot_xboot.h"
#include "dboot_elfload.h"

/*
 * This file contains code that runs to transition us from either a multiboot
 * compliant loader (32 bit non-paging) or a XPV domain loader to
 * regular kernel execution. Its task is to setup the kernel memory image
 * and page tables.
 *
 * The code executes as:
 *	- 32 bits under GRUB (for 32 or 64 bit Solaris)
 * 	- a 32 bit program for the 32-bit PV hypervisor
 *	- a 64 bit program for the 64-bit PV hypervisor (at least for now)
 *
 * Under the PV hypervisor, we must create mappings for any memory beyond the
 * initial start of day allocation (such as the kernel itself).
 *
 * When on the metal, the mapping between maddr_t and paddr_t is 1:1.
 * Since we are running in real mode, so all such memory is accessible.
 */

/*
 * Standard bits used in PTE (page level) and PTP (internal levels)
 */
x86pte_t ptp_bits = PT_VALID | PT_REF | PT_WRITABLE | PT_USER;
x86pte_t pte_bits = PT_VALID | PT_REF | PT_WRITABLE | PT_MOD | PT_NOCONSIST;

/*
 * This is the target addresses (physical) where the kernel text and data
 * nucleus pages will be unpacked. On the hypervisor this is actually a
 * virtual address.
 */
paddr_t ktext_phys;
uint32_t ksize = 2 * FOUR_MEG;	/* kernel nucleus is 8Meg */

static uint64_t target_kernel_text;	/* value to use for KERNEL_TEXT */

/*
 * The stack is setup in assembler before entering startup_kernel()
 */
char stack_space[STACK_SIZE];

/*
 * Use this buffer as the destination for relocating the multiboot info
 * structure (and related structures, except for the actual modules)
 */
char reloc_dest[4096];

/*
 * Multiboot v2-defined framebuffer structure.  Used if the structure isn't
 * provided directly (i.e. a VBE tag is present, but a FRAMEBUFFER tag is not)
 * with multiboot2 info, or if VBE information is provided in the multiboot1
 * info.
 */
char mbifb[sizeof (struct multiboot_tag_framebuffer) + \
	(sizeof (struct multiboot_color) * MULTIBOOT_MAX_COLORS) + 8];

/*
 * Used to track physical memory allocation
 */
static paddr_t next_avail_addr = 0;

#if defined(__xpv)
/*
 * Additional information needed for hypervisor memory allocation.
 * Only memory up to scratch_end is mapped by page tables.
 * mfn_base is the start of the hypervisor virtual image. It's ONE_GIG, so
 * to derive a pfn from a pointer, you subtract mfn_base.
 */

static paddr_t scratch_end = 0;	/* we can't write all of mem here */
static paddr_t mfn_base;		/* addr corresponding to mfn_list[0] */
start_info_t *xen_info;

#else	/* __xpv */

/*
 * If on the metal, then we have a multiboot (v1 or v2) loader.
 */
multiboot_info_t *mb_info;
struct multiboot2_info_header *mb2_info;

#endif	/* __xpv */

/*
 * This contains information passed to the kernel
 */
struct xboot_info __attribute__((__aligned__(16))) boot_info;
struct xboot_info *bi = &boot_info;

/*
 * Page table and memory stuff.
 */
static paddr_t max_mem;			/* maximum memory address */

/*
 * Information about processor MMU
 */
int amd64_support = 0;
int largepage_support = 0;
int pae_support = 0;
int pge_support = 0;
int NX_support = 0;
int PAT_support = 0;

/*
 * Low 32 bits of kernel entry address passed back to assembler.
 * When running a 64 bit kernel, the high 32 bits are 0xffffffff.
 */
uint32_t entry_addr_low;

/*
 * Memlists for the kernel. UEFI uses a TON.
 */
#define	MAX_MEMLIST (200)
struct boot_memlist memlists[MAX_MEMLIST];
uint_t memlists_used = 0;
struct boot_memlist pcimemlists[MAX_MEMLIST];
uint_t pcimemlists_used = 0;
struct boot_memlist rsvdmemlists[MAX_MEMLIST];
uint_t rsvdmemlists_used = 0;

#define	MAX_MODULES (10)
struct boot_modules modules[MAX_MODULES];
uint_t modules_used = 0;

/*
 * Caching variables for often-used state
 */
static int multiboot_version;
static int num_entries;
static int num_entries_initted;
static struct multiboot_tag_mmap *mb2_mmap_tagp;

#if defined(__xpv)
/*
 * See struct e820entry definition in Xen source.
 */
#define	MAXMAPS	100
static struct xen_mmap {
	uint32_t	base_addr_low;
	uint32_t	base_addr_high;
	uint32_t	length_low;
	uint32_t	length_high;
	uint32_t	type;
} map_buffer[MAXMAPS];

static xen_memory_map_t xen_map;
#endif

/*
 * Debugging macros
 */
uint_t prom_debug = 0;
uint_t map_debug = 0;

static void check_higher(paddr_t a);

#if !defined(__xpv)

#define	MBI_DRIVES_LENGTH_MAX	2128

#ifdef SUPERDEBUG
static void
port80_intwrite(unsigned i)
{
	const char *hextbl = "0123456789ABCDEF";
	char buf[11];
	int idx = 10;
	while (i != 0 && idx >= 0) {
		buf[idx--] = hextbl[ i & 0xF ];
		i >>= 4;
	}

	while (++idx < 11)
		outb(0x80, buf[idx]);
}
#endif

/*
 * The following two functions relocate loader-provided data structures
 * to the lowest-possible addresses.  This is essential because the boot
 * memory allocator is very simplistic -- it keeps track ONLY of the
 * boundary between used memory (addresses lower than next_avail) and
 * free memory (addresses higher than next_avail).  To gain the maximum
 * chunk of allocatable free memory, next_avail must be as small as
 * possible.  Loaders that load modules towards the end of memory are
 * particularly problematic, as that may leave little to no available
 * memory above next_avail, leading to memory exhaustion during early
 * boot.
 */

/* Helper macro for relocations -- beware of the side effects! */
#define	RELOC_NEXT(size, relocbasep, szexpr, src, newfieldtype, s) \
	size = RNDUP((uintptr_t)relocbasep + size, 8) - 		\
		(uintptr_t)relocbasep;					\
	cursz = (szexpr);						\
	if (doreloc) {							\
		(void) memmove(relocbasep + size, (void *)(src), cursz);\
		if (prom_debug)						\
			dboot_printf("%s [%p] => [%p] sz=0x%x\n", (s),	\
			    (void *)(src), (void *)(relocbasep + size),	\
			    cursz);					\
		src = (newfieldtype)relocbasep + size;			\
	}								\
	size += cursz

void
dboot_multiboot1_relocate(unsigned *sz, unsigned *modsz, int doreloc,
    char *relocbasep, char *modrelocbasep)
{
	/*
	 * Data structures / objects associated with Multiboot 1 are:
	 * (1)	The multiboot information structure (including pointers
	 * 	to data that may not be physically contiguous with the
	 *	multiboot information structure).  This structure will
	 *	be made physically contiguous (pointers to data will
	 *	point to areas directly following the main multiboot
	 *	information structure).
	 * (2)	Multiboot modules: Referenced from the multiboot information
	 *	structure.  Can be relocated, assuming contiguous memory can
	 *	be found at lower addresses.
	 * (3)	Multiboot kernel: The kernel is loaded at a specific
	 *	address and cannot be moved (since some code is not
	 *	PIC (i.e. dboot itself)).
	 *
	 * The general algorithm is to relocate all data structures to the
	 * special relocation buffer inside dboot's BSS and to relocate
	 * the modules immediately after dboot.  Note that the assumption is
	 * that kernel text and data/bss have been loaded BEFORE dboot in
	 * memory.  If that ever changes, this relocation algorithm will
	 * need to be adapted to start relocating at the highest used
	 * address.  After relocation, the largest address in use
	 * should be: &end_of_dboot + sizeof(multiboot modules) +
	 * alignment overhead (modules are aligned on page boundaries).
	 * The data is aligned on an 8-byte boundary.
	 *
	 * Once the data structures are relocated, the multiboot_info
	 * structure is updated with the new addresses and the global
	 * multiboot info structure is updated.
	 */

	multiboot_info_t *newmb = mb_info;
	unsigned size = 0, mod_size = 0;
	unsigned cursz;
	int i;

	if (doreloc) {
		relocbasep = (char *)RNDUP((uintptr_t)relocbasep, 8);
	}

	/*
	 * Relocate the multiboot_info structure
	 */
	RELOC_NEXT(size, relocbasep, sizeof (struct multiboot_info), newmb,
	    struct multiboot_info *, "Multiboot");

	/*
	 * Now that we've done a shallow copy of the multiboot_info
	 * structure, use the copy for all references below, otherwise
	 * when we relocate individual objects below, we can very
	 * likely overwrite the old multiboot_info, and referencing
	 * it would be catastrophic.
	 */
	if (newmb->flags & MB_INFO_CMDLINE) {
		RELOC_NEXT(size, relocbasep,
		    strlen((char *)newmb->cmdline) + 1,
		    newmb->cmdline, caddr32_t, "cmdline");
	}

	if (newmb->flags & MB_INFO_ELF_SHDR) {
		RELOC_NEXT(size, relocbasep, newmb->elf_sec.size,
		    newmb->elf_sec.addr, caddr32_t, "ELF SHDR");
	}

	if (newmb->flags & MB_INFO_MEM_MAP) {
		RELOC_NEXT(size, relocbasep, newmb->mmap_length,
		    newmb->mmap_addr, caddr32_t, "mmap");
	}

	/*
	 * Handle Solaris hack of passing DHCP ACK packet via the
	 * drives_addr field.   Sanity check drives_length, just
	 * in case the loader didn't initialize it.
	 */

	if (newmb->drives_length > 0) {
		if (newmb->drives_length <= MBI_DRIVES_LENGTH_MAX) {
			RELOC_NEXT(size, relocbasep, newmb->drives_length,
			    newmb->drives_addr, caddr32_t, "drives");
		} else {
			DBG_MSG("****> drive_length too large <****");
			DBG(newmb->drives_addr);
		}
	}

	if ((newmb->flags & MB_INFO_VIDEO_INFO) &&
	    newmb->vbe_control_info != NULL &&
	    newmb->vbe_mode_info != NULL) {

		RELOC_NEXT(size, relocbasep, sizeof (struct VbeInfoBlock),
		    newmb->vbe_control_info,  caddr32_t, "vbeinfoblock");

		RELOC_NEXT(size, relocbasep, sizeof (struct ModeInfoBlock),
		    newmb->vbe_mode_info,  caddr32_t, "modeinfoblock");
	}

	if (newmb->flags & MB_INFO_CONFIG_TABLE && newmb->config_table) {
		/*
		 * See http://www.ctyme.com/intr/rb-1594.htm
		 */
		RELOC_NEXT(size, relocbasep, *(uint16_t *)newmb->config_table,
		    newmb->config_table, caddr32_t, "cfgtbl");
	}

	if (newmb->flags & MB_INFO_BOOT_LOADER_NAME) {
		RELOC_NEXT(size, relocbasep,
		    strlen((char *)newmb->boot_loader_name) + 1,
		    newmb->boot_loader_name, caddr32_t, "ldrname");
	}

	/*
	 * Module relocation MUST be done last because there's high
	 * liklihood that copying them into place will blow away other
	 * data structures that were referenced from the old multiboot_info
	 * structure.
	 */

	if (newmb->flags & MB_INFO_MODS) {
		RELOC_NEXT(size, relocbasep,
		    newmb->mods_count * sizeof (struct mb_module),
		    newmb->mods_addr, caddr32_t, "mods");

		/*
		 * Relocate the module command-lines first, since the
		 * module itself will probably overwrite them if they were
		 * moved first.
		 */
		for (i = 0; i < newmb->mods_count; i++) {
			mb_module_t *modp =
			    &((mb_module_t *)newmb->mods_addr)[i];

			if (modp->mod_name != 0) {
				RELOC_NEXT(size, relocbasep,
				    strlen((char *)modp->mod_name) + 1,
				    modp->mod_name, caddr32_t, "modname");
			}
		}

		for (i = 0; i < newmb->mods_count; i++) {
			mb_module_t *modp =
			    &((mb_module_t *)newmb->mods_addr)[i];

			/*
			 * Modules *MUST* be aligned on page boundaries
			 * (otherwise, the ramdisk driver will fail to
			 * work properly).
			 */
			mod_size = RNDUP((uintptr_t)modrelocbasep + mod_size,
			    MMU_PAGESIZE) - (uintptr_t)modrelocbasep;
			DBG(mod_size);
			DBG((uintptr_t)modrelocbasep);
			RELOC_NEXT(mod_size, modrelocbasep,
			    modp->mod_end - modp->mod_start,
			    modp->mod_start, caddr32_t, "*mod");
			if (doreloc) {
				/*
				 * Pay attention!  The expression below
				 * works out to mod_end because the RELOC_NEXT
				 * macro already factored in cursz (the size
				 * of the module) in the `mod_size` variable.
				 */
				modp->mod_end = (caddr32_t)modrelocbasep +
				    mod_size;
			}
		}
	}

	/* Add worst-case padding to the sizes */
	if (sz)
		*sz = size + 8;
	if (modsz)
		*modsz = mod_size + MMU_PAGESIZE;

	if (doreloc)
		mb_info = newmb;
}

static void
dboot_multiboot2_relocate(unsigned *sz, unsigned *modsz, int doreloc,
    char *relocbasep, char *modrelocbasep)
{
	/*
	 * Relocating Multiboot 2 data structures is significantly easier than
	 * relocating those of multiboot 1.
	 * (1)	The multiboot information structure is contiguous, so a single
	 *	relocation is all that's needed.
	 * (2)	Modules are the only objects referenced from the multiboot info
	 *	structure.  As with multiboot 1, modules are contiguous.
	 */

	struct multiboot2_info_header *newmb2 = mb2_info;
	unsigned size = 0, mod_size = 0;
	unsigned cursz;
	unsigned modcnt, i;

	if (doreloc) {
		relocbasep = (char *)RNDUP((uintptr_t)relocbasep, 8);
	}

	RELOC_NEXT(size, relocbasep, newmb2->total_size, newmb2,
	    struct multiboot2_info_header *, "Multiboot2");

	/*
	 * Set mb2_info directly here so that the iteration functions below
	 * will traverse the relocated multiboot_info
	 */
	if (doreloc) {
		mb2_info = newmb2;
	}

	/*
	 * Module relocation MUST be done last because there's high
	 * liklihood that copying them into place will blow away other
	 * data structures that were referenced from the old multiboot_info
	 * structure.
	 */

	modcnt = dboot_multiboot2_modcount(mb2_info);

	if (modcnt) {

		for (i = 0; i < modcnt; i++) {

			struct multiboot_tag_module *modtagp = NULL;
			if (dboot_multiboot2_iterate(mb2_info,
			    MULTIBOOT_TAG_TYPE_MODULE, i, &modtagp) != 0) {
				/*
				 * Modules *MUST* be aligned on page boundaries
				 * (otherwise, the ramdisk driver will fail to
				 * work properly).
				 */
				mod_size = RNDUP((uintptr_t)modrelocbasep +
				    mod_size, MMU_PAGESIZE) -
				    (uintptr_t)modrelocbasep;
				RELOC_NEXT(mod_size, modrelocbasep,
				    modtagp->mod_end - modtagp->mod_start,
				    modtagp->mod_start, uint32_t, "*mod");
				if (doreloc) {
					/*
					 * Pay attention!  The expression
					 * below works out to mod_end because
					 * the RELOC_NEXT macro already factored
					 * in cursz (the size of the module) in
					 * the `mod_size` variable.
					 */
					modtagp->mod_end =
					    (caddr32_t)modrelocbasep + mod_size;
				}
			}
		}
	}

	if (sz)
		*sz = size + 8;
	if (modsz)
		*modsz = mod_size + MMU_PAGESIZE;

	/*
	 * Call init again to update state associated with the relocated
	 * multiboot info structure.
	 */
	if (doreloc)
		mb2_mmap_tagp = dboot_multiboot2_get_mmap_tagp(mb2_info);
}

static void
dboot_multiboot_relocate(void)
{
	char *relocstart = 0;
	int i, start_at_end_of_dboot = 0;
	unsigned sz = 0, modsz = 0;

	DBG_MSG("Relocating loader-supplied data structures...\n");

	/*
	 * Make sure there's a large enough block of memory we can use to
	 * perform the relocation.  The first preference is to start at
	 * &_end (the end of dboot).  If there is no available block
	 * of memory that includes the range (&_end, &_end + modsz),
	 * look explicitly for a block that is at least `modsz` bytes.
	 * If such a block doesn't exist, we cannot perform the relocation.
	 *
	 * XXX Note that if there is more than one module, we may have a
	 * problem -- we'd need to make sure that we relocate modules
	 * from the lowest address to the highest to avoid overwriting
	 * modules.
	 */

	if (multiboot_version == 1) {
		DBG((uintptr_t)mb_info);

		/* First get the size without actual modules */
		dboot_multiboot1_relocate(&sz, &modsz, 0, 0, 0);
		/*
		 * If the non-module size is greater than the size of the
		 * static relocation buffer, bail on relocation
		 */
		if (sz > sizeof (reloc_dest)) {
			DBG_MSG("mb1: structures are too big for relocation");
			return;
		}
	} else if (multiboot_version == 2) {
		DBG((uintptr_t)mb2_info);
		dboot_multiboot2_relocate(&sz, &modsz, 0, 0, 0);
		/*
		 * If the non-module size is greater than the size of the
		 * static relocation buffer, bail on relocation
		 */
		if (sz > sizeof (reloc_dest)) {
			DBG_MSG("mb2: structures are too big for relocation");
			return;
		}
	} else
		dboot_panic("! 1 <= multiboot_version <= 2\n");

	if (sz == 0) {
		DBG_MSG("Size returned from relocation check is 0 -- aborting"
		    " relocation\n");
		return;
	}

	if (prom_debug)
		dboot_printf("Memory usage of loader data: 0x%x\n",
		    sz);

	for (i = 0; i < memlists_used; i++) {
		/*
		 * The memory lists are in sorted order (and have
		 * been combined from adjacent loader-provided
		 * maps), so we can be sure that each map entry includes
		 * the largest contiguous block of available memory.
		 * Now we can make a single pass through, testing our
		 * constraints.
		 */
		if (_end >= (char *)(uintptr_t)memlists[i].addr &&
		    (((uintptr_t)_end - memlists[i].addr) + modsz
		    <= memlists[i].size)) {
			start_at_end_of_dboot = 1;
			break;
		} else if (memlists[i].size >= modsz &&
		    memlists[i].addr >= (uintptr_t)_end) {
			/*
			 * One last check:  The memory block must not
			 * overlap the load address of the kernel
			 * and dboot itself (4M to &_end).  As a shorthand
			 * check, >= _end is good enough.
			 */
			relocstart = (char *)(uintptr_t)memlists[i].addr;
		}
	}

	if (start_at_end_of_dboot) {

		/*
		 * If we are able to use memory after the end of dboot,
		 * override any other available memory chunk found.
		 */
		relocstart = _end;

	}

	if (relocstart != 0) {
		if (multiboot_version == 1) {
			dboot_multiboot1_relocate(0, 0, 1, reloc_dest,
			    relocstart);
			DBG((uintptr_t)mb_info);
		} else if (multiboot_version == 2) {
			dboot_multiboot2_relocate(0, 0, 1, reloc_dest,
			    relocstart);
			DBG((uintptr_t)mb2_info);
		}
	} else {

		/* Could not find a large enough block to do relocation! */
		DBG_MSG("Could not perform relocation: Not enough contiguous"
		    " available memory!\n");
	}
}

/*
 * If the addresses of firmware tables were provided by the loader, store
 * them in the xboot_info structure.
 */
static void
dboot_multiboot_get_fwtables(void)
{
	struct multiboot_tag_uefi_info *uefitagp;
	struct multiboot_tag_acpi_info *acpitagp;
	struct multiboot_tag_smbios_info *smbiostagp;

	/* Multiboot 1 does not pass firmware table information */
	if (multiboot_version != 2)
		return;

	uefitagp = (struct multiboot_tag_uefi_info *)
	    dboot_multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_UEFI_INFO);
	if (uefitagp) {
		bi->bi_uefi_systab = uefitagp->uefi_systab_addr;
		bi->bi_uefi_arch = (uefitagp->flags & GRUB_UEFI_INFO_UEFI32) ?
		    XBI_UEFI_ARCH32 :
		    ((uefitagp->flags & GRUB_UEFI_INFO_UEFI64) ?
		    XBI_UEFI_ARCH64 : XBI_UEFI_ARCH_UNKNOWN);
	} else {
		bi->bi_uefi_systab = 0;
		bi->bi_uefi_arch = 0;
	}
	DBG(bi->bi_uefi_systab);
	DBG(bi->bi_uefi_arch);

	acpitagp = (struct multiboot_tag_acpi_info *)
	    dboot_multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_ACPI_INFO);
	bi->bi_acpi_rsdp = acpitagp ? acpitagp->acpi_rsdp_addr : 0;
	DBG(bi->bi_acpi_rsdp);

	smbiostagp = (struct multiboot_tag_smbios_info *)
	    dboot_multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_SMBIOS_INFO);
	bi->bi_smbios = smbiostagp ? smbiostagp->smbios_addr : 0;
	DBG(bi->bi_smbios);
}

#endif /* if !defined(__xpv) */


static void
dboot_loader_init(void)
{
#if !defined(__xpv)
	num_entries = 0;
	num_entries_initted = 0;

	if (mb_info) {
		DBG((uintptr_t)mb_info);
		DBG(mb_info->flags);
		multiboot_version = 1;
	} else if (mb2_info) {
		DBG((uintptr_t)mb2_info);
		multiboot_version = 2;
		mb2_mmap_tagp = dboot_multiboot2_get_mmap_tagp(mb2_info);
	} else {
		dboot_panic("Multiboot Information structure not passed by boot"
		    " loader!\n");
	}
#endif
}

static char *
dboot_loader_cmdline(void)
{
#if defined(__xpv)
	return ((char *)xen_info->cmd_line);
#else /* __xpv */

	if (multiboot_version == 1) {
		if (mb_info->flags & MB_INFO_CMDLINE)
			return ((char *)mb_info->cmdline);
		else
			return (NULL);
	} else if (multiboot_version == 2) {
		return (dboot_multiboot2_cmdline(mb2_info));
	} else
		dboot_panic("! 1 <= multiboot_version <= 2\n");
	return (0);
#endif
}

#if !defined(__xpv)

static int
dboot_multiboot_modcount(void)
{
	if (multiboot_version == 1)
		return (mb_info->mods_count);
	else if (multiboot_version == 2)
		return (dboot_multiboot2_modcount(mb2_info));
	else
		dboot_panic("! 1 <= multiboot_version <= 2\n");
	return (0);
}

static uint32_t
dboot_multiboot_modstart(int index)
{
#ifdef DEBUG
	if (index >= dboot_multiboot_modcount())
		dboot_panic("dboot_multiboot_modstart: index >= #mods\n");
#endif
	if (multiboot_version == 1) {
		return (((mb_module_t *)mb_info->mods_addr)[index].mod_start);
	} else if (multiboot_version == 2) {
		return (dboot_multiboot2_modstart(mb2_info, index));
	} else
		dboot_panic("! 1 <= multiboot_version <= 2\n");
	return (0);
}

static uint32_t
dboot_multiboot_modend(int index)
{
#ifdef DEBUG
	if (index >= dboot_multiboot_modcount())
		dboot_panic("dboot_multiboot_modend: index >= #mods\n");
#endif
	if (multiboot_version == 1) {
		return (((mb_module_t *)mb_info->mods_addr)[index].mod_end);
	} else if (multiboot_version == 2) {
		return (dboot_multiboot2_modend(mb2_info, index));
	} else
		dboot_panic("! 1 <= multiboot_version <= 2\n");
	return (0);
}

static char *
dboot_multiboot_modcmdline(int index)
{
#ifdef DEBUG
	if (index >= dboot_multiboot_modcount())
		dboot_panic("dboot_multiboot_modcmdline: index >= #mods\n");
#endif
	if (multiboot_version == 1) {
		return ((char *)((mb_module_t *)
		    mb_info->mods_addr)[index].mod_name);
	} else if (multiboot_version == 2) {
		return (dboot_multiboot2_modcmdline(mb2_info, index));
	} else
		dboot_panic("! 1 <= multiboot_version <= 2\n");
	return (0);
}

static int
dboot_multiboot_basicmeminfo(uint32_t *low, uint32_t *high)
{
	if (multiboot_version == 1) {
		if (mb_info->flags & 0x01) {
			DBG(mb_info->mem_lower);
			DBG(mb_info->mem_upper);
			*low = mb_info->mem_lower;
			*high = mb_info->mem_upper;
			return (1);
		} else
			return (0);
	} else if (multiboot_version == 2) {
		return (dboot_multiboot2_basicmeminfo(mb2_info, low, high));
	} else
		dboot_panic("! 1 <= multiboot_version <= 2\n");
	return (0);
}

static paddr_t
dboot_multiboot1_highest_addr(void)
{
	char *cmdl = (char *)mb_info->cmdline;
	if (mb_info->flags & MB_INFO_CMDLINE)
		return ((paddr_t)((uintptr_t)cmdl + strlen(cmdl) + 1));
	if (mb_info->flags & MB_INFO_MEM_MAP)
		return ((paddr_t)(mb_info->mmap_addr + mb_info->mmap_length));
	return (NULL);
}

static void
dboot_multiboot_highest_addr(void)
{
	paddr_t addr;
	if (multiboot_version == 1) {
		addr = dboot_multiboot1_highest_addr();
		if (addr != NULL)
			check_higher(addr);
	} else if (multiboot_version == 2) {
		addr = dboot_multiboot2_highest_addr(mb2_info);
		if (addr != NULL)
			check_higher(addr);
	} else
		dboot_panic("! 1 <= multiboot_version <= 2\n");
}

static void
dboot_multiboot1_xboot_consinfo(void)
{
	struct multiboot_tag_framebuffer *fbip;
	struct VbeInfoBlock *vinfo;
	struct ModeInfoBlock *minfo;

	if (mb_info->flags & MB_INFO_VIDEO_INFO) {
		/* VBE info present */
		vinfo =
		    (struct VbeInfoBlock *)(uintptr_t)mb_info->vbe_control_info;
		minfo =
		    (struct ModeInfoBlock *)(uintptr_t)mb_info->vbe_mode_info;

		fbip = (struct multiboot_tag_framebuffer *)&mbifb[0];
		(void) memset(&mbifb[0], 0, sizeof (mbifb));

		dboot_vesa_info_to_fb_info(vinfo, minfo, fbip);

		bi->bi_vesa_mode = mb_info->vbe_mode;
		bi->bi_framebuffer_info = (native_ptr_t)(uintptr_t)fbip;
	} else {
		bi->bi_vesa_mode = XBI_VBE_MODE_INVALID;
		bi->bi_framebuffer_info = 0;
	}
}

static void
dboot_multiboot2_xboot_consinfo(void)
{
	struct multiboot_tag_vbe *vesap;
	struct multiboot_tag_framebuffer *fbip;

	vesap = (struct multiboot_tag_vbe *)
	    dboot_multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_VBE);

	fbip = (struct multiboot_tag_framebuffer *)
	    dboot_multiboot2_find_tag(mb2_info, MULTIBOOT_TAG_TYPE_FRAMEBUFFER);

	bi->bi_vesa_mode = vesap ? (uint32_t)vesap->vbe_mode :
	    XBI_VBE_MODE_INVALID;

	/*
	 * Use the information from the framebuffer tag, if present.
	 * Otherwise, fall back to using the info from the VBE tag.
	 */
	if (fbip) {
		DBG_MSG("Found native framebuffer info:\n");
		DBG((uintptr_t)fbip);
		/*
		 * We CANNOT just save a pointer to the framebuffer info here
		 * because if the mbi gets relocated later, the pointer may
		 * point to junk.
		 */
		if (fbip->common.size > sizeof (mbifb)) {
			DBG_MSG("*** framebuffer info TOO BIG\n");
			DBG(fbip->common.size);
			DBG(fbip->common.framebuffer_type);
			bi->bi_framebuffer_info = 0;
		} else {
			(void) memcpy(&mbifb[0], fbip, fbip->common.size);
			bi->bi_framebuffer_info = (uintptr_t)&mbifb[0];
		}
	} else if (vesap) {
		struct VbeInfoBlock *vinfo =
		    (struct VbeInfoBlock *)&vesap->vbe_control_info;
		struct ModeInfoBlock *minfo =
		    (struct ModeInfoBlock *)&vesap->vbe_mode_info;

		fbip = (struct multiboot_tag_framebuffer *)&mbifb[0];
		(void) memset(&mbifb[0], 0, sizeof (mbifb));

		dboot_vesa_info_to_fb_info(vinfo, minfo, fbip);

		bi->bi_framebuffer_info = (native_ptr_t)(uintptr_t)fbip;
	} else {
		bi->bi_framebuffer_info = 0;
	}
}

#endif /* if !defined(__xpv) */

static void
dboot_init_xboot_consinfo(void)
{
#if defined(__xpv)
	/* Text console only for XPV */
	bi->bi_vesa_mode = XBI_VBE_MODE_INVALID;
	bi->bi_framebuffer_info = 0;
#else
	if (multiboot_version == 1) {
		dboot_multiboot1_xboot_consinfo();
	} else if (multiboot_version == 2) {
		dboot_multiboot2_xboot_consinfo();
	}
#endif
}

static int
dboot_loader_mmap_entries(void)
{
	/*
	 * Use a cache of the entry count, as it's often-requested and
	 * expensive.
	 */
	if (num_entries_initted)
		return (num_entries);

#if !defined(__xpv)
	if (multiboot_version == 1) {
		if (mb_info->flags & 0x40) {

			int cnt = 0;
			mb_memory_map_t *mmap;

			DBG(mb_info->mmap_addr);
			DBG(mb_info->mmap_length);

			for (mmap = (mb_memory_map_t *)mb_info->mmap_addr;
			    (uint32_t)mmap < mb_info->mmap_addr +
			    mb_info->mmap_length;
			    mmap = (mb_memory_map_t *)((uint32_t)mmap +
			    mmap->size + sizeof (mmap->size))) {

				++cnt;
			}
			num_entries_initted = 1;
			num_entries = cnt;
			return (cnt);
		} else
			return (0);
	} else if (multiboot_version == 2) {
		num_entries_initted = 1;
		num_entries = dboot_multiboot2_mmap_entries(mb2_info,
		    mb2_mmap_tagp);
		return (num_entries);
	} else
		dboot_panic("! 1 <= multiboot_version <= 2\n");
	return (0);
#else
	return (MAXMAPS);
#endif
}

static uint64_t
dboot_loader_mmap_get_base(int index)
{
#ifdef DEBUG
	if (index >= dboot_loader_mmap_entries())
		dboot_panic("dboot_loader_mmap_get_base: index >= #entries\n");
#endif

#if !defined(__xpv)
	if (multiboot_version == 1) {
		int i;
		mb_memory_map_t *mp = (mb_memory_map_t *)mb_info->mmap_addr;
		mb_memory_map_t *mpend = (mb_memory_map_t *)
		    (mb_info->mmap_addr + mb_info->mmap_length);

		for (i = 0; mp < mpend && i != index; i++)
			mp = (mb_memory_map_t *)((uint32_t)mp + mp->size +
			    sizeof (mp->size));

		if (mp >= mpend) /* index out of bounds */
			dboot_panic("index out of bounds in "
			    "dboot_loader_mmap_get_base() index: %d", index);
		return (((uint64_t)mp->base_addr_high << 32) +
		    (uint64_t)mp->base_addr_low);
	} else if (multiboot_version == 2) {
		return (dboot_multiboot2_mmap_get_base(mb2_info,
		    mb2_mmap_tagp, index));
	} else
		dboot_panic("! 1 <= multiboot_version <= 2\n");
	return (0);
#else
	return (((uint64_t)map_buffer[index].base_addr_high << 32) +
	    (uint64_t)map_buffer[index].base_addr_low);
#endif
}

static uint64_t
dboot_loader_mmap_get_length(int index)
{
#ifdef DEBUG
	if (index >= dboot_loader_mmap_entries())
		dboot_panic("dboot_loader_mmap_get_length: index >= "
		    "#entries\n");
#endif

#if !defined(__xpv)
	if (multiboot_version == 1) {
		int i;
		mb_memory_map_t *mp = (mb_memory_map_t *)mb_info->mmap_addr;
		mb_memory_map_t *mpend = (mb_memory_map_t *)
		    (mb_info->mmap_addr + mb_info->mmap_length);

		for (i = 0; mp < mpend && i != index; i++)
			mp = (mb_memory_map_t *)((uint32_t)mp + mp->size +
			    sizeof (mp->size));

		if (mp >= mpend) /* index out of bounds */
			dboot_panic("index out of bounds in "
			    "dboot_loader_mmap_get_length() index: %d", index);
		return (((uint64_t)mp->length_high << 32) +
		    (uint64_t)mp->length_low);
	} else if (multiboot_version == 2) {
		return (dboot_multiboot2_mmap_get_length(mb2_info,
		    mb2_mmap_tagp, index));
	} else
		dboot_panic("! 1 <= multiboot_version <= 2\n");
	return (0);
#else
	return (((uint64_t)map_buffer[index].length_high << 32) +
	    (uint64_t)map_buffer[index].length_low);
#endif
}

static uint32_t
dboot_loader_mmap_get_type(int index)
{
#ifdef DEBUG
	if (index >= dboot_loader_mmap_entries())
		dboot_panic("dboot_loader_mmap_get_type: index >= #entries\n");
#endif

#if !defined(__xpv)
	if (multiboot_version == 1) {
		int i;
		mb_memory_map_t *mp = (mb_memory_map_t *)mb_info->mmap_addr;
		mb_memory_map_t *mpend = (mb_memory_map_t *)
		    (mb_info->mmap_addr + mb_info->mmap_length);

		for (i = 0; mp < mpend && i != index; i++)
			mp = (mb_memory_map_t *)((uint32_t)mp + mp->size +
			    sizeof (mp->size));

		if (mp >= mpend) /* index out of bounds */
			dboot_panic("index out of bounds in "
			    "dboot_loader_mmap_get_type() index: %d", index);
		return (mp->type);
	} else if (multiboot_version == 2) {
		return (dboot_multiboot2_mmap_get_type(mb2_info,
		    mb2_mmap_tagp, index));
	} else
		dboot_panic("! 1 <= multiboot_version <= 2\n");
	return (0);
#else
	return (map_buffer[index].type);
#endif
}

/*
 * Either hypervisor-specific or grub-specific code builds the initial
 * memlists. This code does the sort/merge/link for final use.
 */
static void
sort_physinstall(void)
{
	int i;
#if !defined(__xpv)
	int j;
	struct boot_memlist tmp;

	/*
	 * Now sort the memlists, in case they weren't in order.
	 * Yeah, this is a bubble sort; small, simple and easy to get right.
	 */
	DBG_MSG("Sorting phys-installed list\n");
	for (j = memlists_used - 1; j > 0; --j) {
		for (i = 0; i < j; ++i) {
			if (memlists[i].addr < memlists[i + 1].addr)
				continue;
			tmp = memlists[i];
			memlists[i] = memlists[i + 1];
			memlists[i + 1] = tmp;
		}
	}

	/*
	 * Merge any memlists that don't have holes between them.
	 */
	for (i = 0; i <= memlists_used - 1; ++i) {
		if (memlists[i].addr + memlists[i].size != memlists[i + 1].addr)
			continue;

		if (prom_debug)
			dboot_printf(
			    "merging mem segs %" PRIx64 "...%" PRIx64
			    " w/ %" PRIx64 "...%" PRIx64 "\n",
			    memlists[i].addr,
			    memlists[i].addr + memlists[i].size,
			    memlists[i + 1].addr,
			    memlists[i + 1].addr + memlists[i + 1].size);

		memlists[i].size += memlists[i + 1].size;
		for (j = i + 1; j < memlists_used - 1; ++j)
			memlists[j] = memlists[j + 1];
		--memlists_used;
		DBG(memlists_used);
		--i;	/* after merging we need to reexamine, so do this */
	}
#endif	/* __xpv */

	if (prom_debug) {
		dboot_printf("\nFinal memlists:\n");
		for (i = 0; i < memlists_used; ++i) {
			dboot_printf("\t%d: addr=%" PRIx64 " size=%"
			    PRIx64 "\n", i, memlists[i].addr, memlists[i].size);
		}
	}

	/*
	 * link together the memlists with native size pointers
	 */
	memlists[0].next = 0;
	memlists[0].prev = 0;
	for (i = 1; i < memlists_used; ++i) {
		memlists[i].prev = (native_ptr_t)(uintptr_t)(memlists + i - 1);
		memlists[i].next = 0;
		memlists[i - 1].next = (native_ptr_t)(uintptr_t)(memlists + i);
	}
	bi->bi_phys_install = (native_ptr_t)memlists;
	DBG(bi->bi_phys_install);
}

/*
 * build bios reserved memlists
 */
static void
build_rsvdmemlists(void)
{
	int i;

	rsvdmemlists[0].next = 0;
	rsvdmemlists[0].prev = 0;
	for (i = 1; i < rsvdmemlists_used; ++i) {
		rsvdmemlists[i].prev =
		    (native_ptr_t)(uintptr_t)(rsvdmemlists + i - 1);
		rsvdmemlists[i].next = 0;
		rsvdmemlists[i - 1].next =
		    (native_ptr_t)(uintptr_t)(rsvdmemlists + i);
	}
	bi->bi_rsvdmem = (native_ptr_t)rsvdmemlists;
	DBG(bi->bi_rsvdmem);
}

#if defined(__xpv)

/*
 * halt on the hypervisor after a delay to drain console output
 */
void
dboot_halt(void)
{
	uint_t i = 10000;

	while (--i)
		(void) HYPERVISOR_yield();
	(void) HYPERVISOR_shutdown(SHUTDOWN_poweroff);
}

/*
 * From a machine address, find the corresponding pseudo-physical address.
 * Pseudo-physical address are contiguous and run from mfn_base in each VM.
 * Machine addresses are the real underlying hardware addresses.
 * These are needed for page table entries. Note that this routine is
 * poorly protected. A bad value of "ma" will cause a page fault.
 */
paddr_t
ma_to_pa(maddr_t ma)
{
	ulong_t pgoff = ma & MMU_PAGEOFFSET;
	ulong_t pfn = mfn_to_pfn_mapping[mmu_btop(ma)];
	paddr_t pa;

	if (pfn >= xen_info->nr_pages)
		return (-(paddr_t)1);
	pa = mfn_base + mmu_ptob((paddr_t)pfn) + pgoff;
#ifdef DEBUG
	if (ma != pa_to_ma(pa))
		dboot_printf("ma_to_pa(%" PRIx64 ") got %" PRIx64 ", "
		    "pa_to_ma() says %" PRIx64 "\n", ma, pa, pa_to_ma(pa));
#endif
	return (pa);
}

/*
 * From a pseudo-physical address, find the corresponding machine address.
 */
maddr_t
pa_to_ma(paddr_t pa)
{
	pfn_t pfn;
	ulong_t mfn;

	pfn = mmu_btop(pa - mfn_base);
	if (pa < mfn_base || pfn >= xen_info->nr_pages)
		dboot_panic("pa_to_ma(): illegal address 0x%lx", (ulong_t)pa);
	mfn = ((ulong_t *)xen_info->mfn_list)[pfn];
#ifdef DEBUG
	if (mfn_to_pfn_mapping[mfn] != pfn)
		dboot_printf("pa_to_ma(pfn=%lx) got %lx ma_to_pa() says %lx\n",
		    pfn, mfn, mfn_to_pfn_mapping[mfn]);
#endif
	return (mfn_to_ma(mfn) | (pa & MMU_PAGEOFFSET));
}

#endif	/* __xpv */

x86pte_t
get_pteval(paddr_t table, uint_t index)
{
	if (pae_support)
		return (((x86pte_t *)(uintptr_t)table)[index]);
	return (((x86pte32_t *)(uintptr_t)table)[index]);
}

/*ARGSUSED*/
void
set_pteval(paddr_t table, uint_t index, uint_t level, x86pte_t pteval)
{
#ifdef __xpv
	mmu_update_t t;
	maddr_t mtable = pa_to_ma(table);
	int retcnt;

	t.ptr = (mtable + index * pte_size) | MMU_NORMAL_PT_UPDATE;
	t.val = pteval;
	if (HYPERVISOR_mmu_update(&t, 1, &retcnt, DOMID_SELF) || retcnt != 1)
		dboot_panic("HYPERVISOR_mmu_update() failed");
#else /* __xpv */
	uintptr_t tab_addr = (uintptr_t)table;

	if (pae_support)
		((x86pte_t *)tab_addr)[index] = pteval;
	else
		((x86pte32_t *)tab_addr)[index] = (x86pte32_t)pteval;
	if (level == top_level && level == 2)
		reload_cr3();
#endif /* __xpv */
}

paddr_t
make_ptable(x86pte_t *pteval, uint_t level)
{
	paddr_t new_table = (paddr_t)(uintptr_t)mem_alloc(MMU_PAGESIZE);

	if (level == top_level && level == 2)
		*pteval = pa_to_ma((uintptr_t)new_table) | PT_VALID;
	else
		*pteval = pa_to_ma((uintptr_t)new_table) | ptp_bits;

#ifdef __xpv
	/* Remove write permission to the new page table. */
	if (HYPERVISOR_update_va_mapping(new_table,
	    *pteval & ~(x86pte_t)PT_WRITABLE, UVMF_INVLPG | UVMF_LOCAL))
		dboot_panic("HYP_update_va_mapping error");
#endif

	if (map_debug)
		dboot_printf("new page table lvl=%d paddr=0x%lx ptp=0x%"
		    PRIx64 "\n", level, (ulong_t)new_table, *pteval);
	return (new_table);
}

x86pte_t *
map_pte(paddr_t table, uint_t index)
{
	return ((x86pte_t *)(uintptr_t)(table + index * pte_size));
}

/*
 * dump out the contents of page tables...
 */
static void
dump_tables(void)
{
	uint_t save_index[4];	/* for recursion */
	char *save_table[4];	/* for recursion */
	uint_t	l;
	uint64_t va;
	uint64_t pgsize;
	int index;
	int i;
	x86pte_t pteval;
	char *table;
	static char *tablist = "\t\t\t";
	char *tabs = tablist + 3 - top_level;
	uint_t pa, pa1;
#if !defined(__xpv)
#define	maddr_t paddr_t
#endif /* !__xpv */

	dboot_printf("Finished pagetables:\n");
	table = (char *)(uintptr_t)top_page_table;
	l = top_level;
	va = 0;
	for (index = 0; index < ptes_per_table; ++index) {
		pgsize = 1ull << shift_amt[l];
		if (pae_support)
			pteval = ((x86pte_t *)table)[index];
		else
			pteval = ((x86pte32_t *)table)[index];
		if (pteval == 0)
			goto next_entry;

		dboot_printf("%s %p[0x%x] = %" PRIx64 ", va=%" PRIx64,
		    tabs + l, (void *)table, index, (uint64_t)pteval, va);
		pa = ma_to_pa(pteval & MMU_PAGEMASK);
		dboot_printf(" physaddr=%x\n", pa);

		/*
		 * Don't try to walk hypervisor private pagetables
		 */
		if ((l > 1 || (l == 1 && (pteval & PT_PAGESIZE) == 0))) {
			save_table[l] = table;
			save_index[l] = index;
			--l;
			index = -1;
			table = (char *)(uintptr_t)
			    ma_to_pa(pteval & MMU_PAGEMASK);
			goto recursion;
		}

		/*
		 * shorten dump for consecutive mappings
		 */
		for (i = 1; index + i < ptes_per_table; ++i) {
			if (pae_support)
				pteval = ((x86pte_t *)table)[index + i];
			else
				pteval = ((x86pte32_t *)table)[index + i];
			if (pteval == 0)
				break;
			pa1 = ma_to_pa(pteval & MMU_PAGEMASK);
			if (pa1 != pa + i * pgsize)
				break;
		}
		if (i > 2) {
			dboot_printf("%s...\n", tabs + l);
			va += pgsize * (i - 2);
			index += i - 2;
		}
next_entry:
		va += pgsize;
		if (l == 3 && index == 256)	/* VA hole */
			va = 0xffff800000000000ull;
recursion:
		;
	}
	if (l < top_level) {
		++l;
		index = save_index[l];
		table = save_table[l];
		goto recursion;
	}
}

/*
 * Add a mapping for the machine page at the given virtual address.
 */
static void
map_ma_at_va(maddr_t ma, native_ptr_t va, uint_t level)
{
	x86pte_t *ptep;
	x86pte_t pteval;

	pteval = ma | pte_bits;
	if (level > 0)
		pteval |= PT_PAGESIZE;
	if (va >= target_kernel_text && pge_support)
		pteval |= PT_GLOBAL;

	if (map_debug && ma != va)
		dboot_printf("mapping ma=0x%" PRIx64 " va=0x%" PRIx64
		    " pte=0x%" PRIx64 " l=%d\n",
		    (uint64_t)ma, (uint64_t)va, pteval, level);

#if defined(__xpv)
	/*
	 * see if we can avoid find_pte() on the hypervisor
	 */
	if (HYPERVISOR_update_va_mapping(va, pteval,
	    UVMF_INVLPG | UVMF_LOCAL) == 0)
		return;
#endif

	/*
	 * Find the pte that will map this address. This creates any
	 * missing intermediate level page tables
	 */
	ptep = find_pte(va, NULL, level, 0);

	/*
	 * When paravirtualized, we must use hypervisor calls to modify the
	 * PTE, since paging is active. On real hardware we just write to
	 * the pagetables which aren't in use yet.
	 */
#if defined(__xpv)
	ptep = ptep;	/* shut lint up */
	if (HYPERVISOR_update_va_mapping(va, pteval, UVMF_INVLPG | UVMF_LOCAL))
		dboot_panic("mmu_update failed-map_pa_at_va va=0x%" PRIx64
		    " l=%d ma=0x%" PRIx64 ", pte=0x%" PRIx64 "",
		    (uint64_t)va, level, (uint64_t)ma, pteval);
#else
	if (va < 1024 * 1024)
		pteval |= PT_NOCACHE;		/* for video RAM */
	if (pae_support)
		*ptep = pteval;
	else
		*((x86pte32_t *)ptep) = (x86pte32_t)pteval;
#endif
}

/*
 * Add a mapping for the physical page at the given virtual address.
 */
static void
map_pa_at_va(paddr_t pa, native_ptr_t va, uint_t level)
{
	map_ma_at_va(pa_to_ma(pa), va, level);
}

/*
 * This is called to remove start..end from the
 * possible range of PCI addresses.
 */
const uint64_t pci_lo_limit = 0x00100000ul;
const uint64_t pci_hi_limit = 0xfff00000ul;
static void
exclude_from_pci(uint64_t start, uint64_t end)
{
	int i;
	int j;
	struct boot_memlist *ml;

	for (i = 0; i < pcimemlists_used; ++i) {
		ml = &pcimemlists[i];

		/* delete the entire range? */
		if (start <= ml->addr && ml->addr + ml->size <= end) {
			--pcimemlists_used;
			for (j = i; j < pcimemlists_used; ++j)
				pcimemlists[j] = pcimemlists[j + 1];
			--i;	/* to revisit the new one at this index */
		}

		/* split a range? */
		else if (ml->addr < start && end < ml->addr + ml->size) {

			++pcimemlists_used;
			if (pcimemlists_used > MAX_MEMLIST)
				dboot_panic("too many pcimemlists");

			for (j = pcimemlists_used - 1; j > i; --j)
				pcimemlists[j] = pcimemlists[j - 1];
			ml->size = start - ml->addr;

			++ml;
			ml->size = (ml->addr + ml->size) - end;
			ml->addr = end;
			++i;	/* skip on to next one */
		}

		/* cut memory off the start? */
		else if (ml->addr < end && end < ml->addr + ml->size) {
			ml->size -= end - ml->addr;
			ml->addr = end;
		}

		/* cut memory off the end? */
		else if (ml->addr <= start && start < ml->addr + ml->size) {
			ml->size = start - ml->addr;
		}
	}
}

static void
build_pcimemlists(void)
{
	uint64_t page_offset = MMU_PAGEOFFSET;	/* needs to be 64 bits */
	uint64_t start;
	uint64_t end;
	int i, num;

	/*
	 * initialize
	 */
	pcimemlists[0].addr = pci_lo_limit;
	pcimemlists[0].size = pci_hi_limit - pci_lo_limit;
	pcimemlists_used = 1;

	num = dboot_loader_mmap_entries();

	/*
	 * Fill in PCI memlists.
	 */
	for (i = 0; i < num; ++i) {
		start = dboot_loader_mmap_get_base(i);
		end = start + dboot_loader_mmap_get_length(i);

		if (prom_debug)
			dboot_printf("\ttype: %u %" PRIx64 "..%"
			    PRIx64 "\n", dboot_loader_mmap_get_type(i), start,
			    end);

		/*
		 * page align start and end
		 */
		start = (start + page_offset) & ~page_offset;
		end &= ~page_offset;
		if (end <= start)
			continue;

		exclude_from_pci(start, end);
	}

	/*
	 * Finish off the pcimemlist
	 */
	if (prom_debug) {
		for (i = 0; i < pcimemlists_used; ++i) {
			dboot_printf("pcimemlist entry 0x%" PRIx64 "..0x%"
			    PRIx64 "\n", pcimemlists[i].addr,
			    pcimemlists[i].addr + pcimemlists[i].size);
		}
	}
	pcimemlists[0].next = 0;
	pcimemlists[0].prev = 0;
	for (i = 1; i < pcimemlists_used; ++i) {
		pcimemlists[i].prev =
		    (native_ptr_t)(uintptr_t)(pcimemlists + i - 1);
		pcimemlists[i].next = 0;
		pcimemlists[i - 1].next =
		    (native_ptr_t)(uintptr_t)(pcimemlists + i);
	}
	bi->bi_pcimem = (native_ptr_t)pcimemlists;
	DBG(bi->bi_pcimem);
}

#if defined(__xpv)
/*
 * Initialize memory allocator stuff from hypervisor-supplied start info.
 *
 * There is 512KB of scratch area after the boot stack page.
 * We'll use that for everything except the kernel nucleus pages which are too
 * big to fit there and are allocated last anyway.
 */

static void
init_mem_alloc(void)
{
	int	local;	/* variables needed to find start region */
	paddr_t	scratch_start;

	DBG_MSG("Entered init_mem_alloc()\n");

	/*
	 * Free memory follows the stack. There's at least 512KB of scratch
	 * space, rounded up to at least 2Mb alignment.  That should be enough
	 * for the page tables we'll need to build.  The nucleus memory is
	 * allocated last and will be outside the addressible range.  We'll
	 * switch to new page tables before we unpack the kernel
	 */
	scratch_start = RNDUP((paddr_t)(uintptr_t)&local, MMU_PAGESIZE);
	DBG(scratch_start);
	scratch_end = RNDUP((paddr_t)scratch_start + 512 * 1024, TWO_MEG);
	DBG(scratch_end);

	/*
	 * For paranoia, leave some space between hypervisor data and ours.
	 * Use 500 instead of 512.
	 */
	next_avail_addr = scratch_end - 500 * 1024;
	DBG(next_avail_addr);

	/*
	 * The domain builder gives us at most 1 module
	 */
	DBG(xen_info->mod_len);
	if (xen_info->mod_len > 0) {
		DBG(xen_info->mod_start);
		modules[0].bm_addr = xen_info->mod_start;
		modules[0].bm_size = xen_info->mod_len;
		bi->bi_module_cnt = 1;
		bi->bi_modules = (native_ptr_t)modules;
	} else {
		bi->bi_module_cnt = 0;
		bi->bi_modules = NULL;
	}
	DBG(bi->bi_module_cnt);
	DBG(bi->bi_modules);

	DBG(xen_info->mfn_list);
	DBG(xen_info->nr_pages);
	max_mem = (paddr_t)xen_info->nr_pages << MMU_PAGESHIFT;
	DBG(max_mem);

	/*
	 * Using pseudo-physical addresses, so only 1 memlist element
	 */
	memlists[0].addr = 0;
	DBG(memlists[0].addr);
	memlists[0].size = max_mem;
	DBG(memlists[0].size);
	memlists_used = 1;
	DBG(memlists_used);

	/*
	 * finish building physinstall list
	 */
	sort_physinstall();

	/*
	 * build bios reserved memlists
	 */
	build_rsvdmemlists();

}

#else	/* !__xpv */

/*
 * During memory allocation, find the highest address not used yet.
 */
static void
check_higher(paddr_t a)
{
	if (a < next_avail_addr)
		return;
	next_avail_addr = RNDUP(a + 1, MMU_PAGESIZE);
	DBG(next_avail_addr);
}

static void
dboot_process_mmap(void)
{
	uint64_t page_offset = MMU_PAGEOFFSET;	/* needs to be 64 bits */
	uint64_t start;
	uint64_t end;
	uint32_t lower, upper;
	int i, mmap_entries;

	/*
	 * Walk through the memory map from multiboot and build our memlist
	 * structures. Note these will have native format pointers.
	 */
	DBG_MSG("\nFinding Memory Map\n");
	max_mem = 0;
	if ((mmap_entries = dboot_loader_mmap_entries()) > 0) {

		for (i = 0; i < mmap_entries; i++) {
			uint32_t type = dboot_loader_mmap_get_type(i);

			start = dboot_loader_mmap_get_base(i);
			end = start + dboot_loader_mmap_get_length(i);

			if (prom_debug)
				dboot_printf("\ttype: %d %" PRIx64 "..%"
				    PRIx64 "\n", type, start, end);

			/*
			 * page align start and end
			 */
			start = (start + page_offset) & ~page_offset;
			end &= ~page_offset;
			if (end <= start)
				continue;

			/*
			 * only type 1 is usable RAM
			 */
			switch (type) {
			case 1:
				if (end > max_mem)
					max_mem = end;
				memlists[memlists_used].addr = start;
				memlists[memlists_used].size = end - start;
				++memlists_used;
				if (memlists_used > MAX_MEMLIST)
					dboot_panic("too many memlists");
				break;
			default: /* All other types treated as reserved */
				rsvdmemlists[rsvdmemlists_used].addr = start;
				rsvdmemlists[rsvdmemlists_used].size =
				    end - start;
				++rsvdmemlists_used;
				if (rsvdmemlists_used > MAX_MEMLIST)
					dboot_panic("too many rsvdmemlists");
				break;
			}
		}
		build_pcimemlists();

	} else if (dboot_multiboot_basicmeminfo(&lower, &upper)) {

		memlists[memlists_used].addr = 0;
		memlists[memlists_used].size = lower * 1024;
		++memlists_used;
		memlists[memlists_used].addr = 1024 * 1024;
		memlists[memlists_used].size = upper * 1024;
		++memlists_used;

		/*
		 * Old platform - assume I/O space at the end of memory.
		 */
		pcimemlists[0].addr = (upper * 1024) + (1024 * 1024);
		pcimemlists[0].size = pci_hi_limit - pcimemlists[0].addr;
		pcimemlists[0].next = 0;
		pcimemlists[0].prev = 0;
		bi->bi_pcimem = (native_ptr_t)pcimemlists;
		DBG(bi->bi_pcimem);
	} else {
		dboot_panic("No memory info from boot loader!!!");
	}

	/*
	 * finish processing the physinstall list
	 */
	sort_physinstall();
}

/*
 * Walk through the module information finding the last used address.
 * The first available address will become the top level page table.
 *
 * We then build the phys_install memlist from the multiboot information.
 */
static void
init_mem_alloc(void)
{
	int i, modcount;

	DBG_MSG("Entered init_mem_alloc()\n");

	modcount = dboot_multiboot_modcount();

	if (modcount > MAX_MODULES) {
		dboot_panic("Too many modules (%d) -- the maximum is %d.",
		    modcount, MAX_MODULES);
	}
	/*
	 * search the modules to find the last used address
	 * we'll build the module list while we're walking through here
	 */
	DBG_MSG("\nFinding Modules\n");
	check_higher((paddr_t)&_end);
	for (i = 0; i < modcount; ++i) {
		uint32_t curmod_start = dboot_multiboot_modstart(i);
		uint32_t curmod_end = dboot_multiboot_modend(i);

		if (prom_debug) {
			char *cmdl = dboot_multiboot_modcmdline(i);
			dboot_printf("\tmodule #%d: %s at: 0x%lx, len 0x%lx\n",
			    i, (cmdl == NULL || strlen(cmdl) == 0) ?
			    "[<no module cmdline>]" : cmdl,
			    (long)curmod_start,
			    (long)(curmod_end - curmod_start));
		}
		modules[i].bm_addr = curmod_start;
		if (curmod_start > curmod_end) {
			dboot_panic("module[%d]: Invalid module start address "
			    "(0x%llx)", i, (uint64_t)curmod_start);
		}
		modules[i].bm_size = curmod_end - curmod_start;

		check_higher(curmod_end);
	}
	bi->bi_modules = (native_ptr_t)modules;
	DBG(bi->bi_modules);
	bi->bi_module_cnt = modcount;
	DBG(bi->bi_module_cnt);

	/* Memory maps were already processed in startup_kernel() */

	/*
	 * build bios reserved mem lists
	 */
	build_rsvdmemlists();
}
#endif /* !__xpv */

/*
 * Simple memory allocator, allocates aligned physical memory.
 * Note that startup_kernel() only allocates memory, never frees.
 * Memory usage just grows in an upward direction.
 */
static void *
do_mem_alloc(uint32_t size, uint32_t align)
{
	uint_t i;
	uint64_t best;
	uint64_t start;
	uint64_t end;

	/*
	 * make sure size is a multiple of pagesize
	 */
	size = RNDUP(size, MMU_PAGESIZE);
	next_avail_addr = RNDUP(next_avail_addr, align);

	/*
	 * XXPV fixme joe
	 *
	 * a really large bootarchive that causes you to run out of memory
	 * may cause this to blow up
	 */
	/* LINTED E_UNEXPECTED_UINT_PROMOTION */
	best = (uint64_t)-size;
	for (i = 0; i < memlists_used; ++i) {
		start = memlists[i].addr;
#if defined(__xpv)
		start += mfn_base;
#endif
		end = start + memlists[i].size;

		/*
		 * did we find the desired address?
		 */
		if (start <= next_avail_addr && next_avail_addr + size <= end) {
			best = next_avail_addr;
			goto done;
		}

		/*
		 * if not is this address the best so far?
		 */
		if (start > next_avail_addr && start < best &&
		    RNDUP(start, align) + size <= end)
			best = RNDUP(start, align);
	}

	/*
	 * We didn't find exactly the address we wanted, due to going off the
	 * end of a memory region. Return the best found memory address.
	 */
done:
	next_avail_addr = best + size;
#if defined(__xpv)
	if (next_avail_addr > scratch_end)
		dboot_panic("Out of mem next_avail: 0x%lx, scratch_end: "
		    "0x%lx", (ulong_t)next_avail_addr,
		    (ulong_t)scratch_end);
#endif
	(void) memset((void *)(uintptr_t)best, 0, size);
	return ((void *)(uintptr_t)best);
}

void *
mem_alloc(uint32_t size)
{
	return (do_mem_alloc(size, MMU_PAGESIZE));
}

/*
 * Build page tables for the linear framebuffer.
 * XXX Move this function to be "VESA independent" as we do in fakebop.c
 */
#if !defined(__xpv)
static void
build_fb_page_tables()
{
	uint64_t		start;
	uint64_t		end;
	struct multiboot_tag_framebuffer *fbip;

	if (bi->bi_framebuffer_info == 0)	/* Should not happen */
		return;

	fbip = (struct multiboot_tag_framebuffer *)(uintptr_t)
	    bi->bi_framebuffer_info;

	start = fbip->common.framebuffer_addr;
	end = start + (fbip->common.framebuffer_width *
	    fbip->common.framebuffer_height *
	    (fbip->common.framebuffer_bpp / 8));

	/*
	 * The correct WC bit for PAT will only be enforced later on during
	 * boot, when our PAT table is loaded. We need Write Combining
	 * for performance reasons wrt accessing the framebuffer.
	 */
	pte_bits |= PT_NOCACHE;
	if (PAT_support)
		pte_bits |= PT_PAT_4K;

	while (start < end) {
		map_pa_at_va(start, start, 0);
		start += MMU_PAGESIZE;
	}

	pte_bits &= ~PT_NOCACHE;
	if (PAT_support)
		pte_bits &= ~PT_PAT_4K;
}
#endif	/* __xpv */
/*
 * Build page tables to map all of memory used so far as well as the kernel.
 */
static void
build_page_tables(void)
{
	uint32_t psize;
	uint32_t level;
	uint32_t off;
#if !defined(__xpv)
	uint64_t start;
	uint32_t i;
	uint64_t end;
#endif	/* __xpv */

	/*
	 * If we're on metal, we need to create the top level pagetable.
	 */
#if defined(__xpv)
	top_page_table = (paddr_t)(uintptr_t)xen_info->pt_base;
#else /* __xpv */
	top_page_table = (paddr_t)(uintptr_t)mem_alloc(MMU_PAGESIZE);
#endif /* __xpv */
	DBG((uintptr_t)top_page_table);

	/*
	 * Determine if we'll use large mappings for kernel, then map it.
	 */
	if (largepage_support) {
		psize = lpagesize;
		level = 1;
	} else {
		psize = MMU_PAGESIZE;
		level = 0;
	}

	DBG_MSG("Mapping kernel\n");
	DBG(ktext_phys);
	DBG(target_kernel_text);
	DBG(ksize);
	DBG(psize);
	for (off = 0; off < ksize; off += psize)
		map_pa_at_va(ktext_phys + off, target_kernel_text + off, level);

	/*
	 * The kernel will need a 1 page window to work with page tables
	 */
	bi->bi_pt_window = (uintptr_t)mem_alloc(MMU_PAGESIZE);
	DBG(bi->bi_pt_window);
	bi->bi_pte_to_pt_window =
	    (uintptr_t)find_pte(bi->bi_pt_window, NULL, 0, 0);
	DBG(bi->bi_pte_to_pt_window);

#if !defined(__xpv)
	/*
	 * We need 1:1 mappings for the lower 1M of memory to access
	 * BIOS tables used by a couple of drivers during boot.
	 *
	 * The following code works because our simple memory allocator
	 * only grows usage in an upwards direction.
	 *
	 * Note that by this point in boot some mappings for low memory
	 * may already exist because we've already accessed device in low
	 * memory.  (Specifically the video frame buffer and keyboard
	 * status ports.)  If we're booting on raw hardware then GRUB
	 * created these mappings for us.  If we're booting under a
	 * hypervisor then we went ahead and remapped these devices into
	 * memory allocated within dboot itself.
	 */
	if (map_debug)
		dboot_printf("1:1 map pa=0..1Meg\n");
	for (start = 0; start < 1024 * 1024; start += MMU_PAGESIZE) {
		map_pa_at_va(start, start, 0);
	}

	for (i = 0; i < memlists_used; ++i) {
		start = memlists[i].addr;

		end = start + memlists[i].size;

		if (map_debug)
			dboot_printf("1:1 map pa=%" PRIx64 "..%" PRIx64 "\n",
			    start, end);

		while (start < end && start < next_avail_addr) {
			map_pa_at_va(start, start, 0);
			start += MMU_PAGESIZE;
		}
	}

	/*
	 * At this point, we already know from bcons_init() if the information
	 * contained in the framebuffer parameters passed through multiboot
	 * are valid.
	 * If this is not the case, 'console' has been moved to
	 * CONS_SCREEN_TEXT so this check is enough.
	 */
	if (console == CONS_SCREEN_FB || console == CONS_SCREEN_GRAPHICS)
		build_fb_page_tables();

#endif /* __xpv */
	DBG_MSG("\nPage tables constructed\n");

}

#define	NO_MULTIBOOT	\
"multiboot is no longer used to boot the Solaris Operating System.\n\
The grub entry should be changed to:\n\
kernel$ /platform/i86pc/kernel/$ISADIR/unix\n\
module$ /platform/i86pc/$ISADIR/boot_archive\n\
See http://www.sun.com/msg/SUNOS-8000-AK for details.\n"

/*
 * startup_kernel has a pretty simple job. It builds pagetables which reflect
 * 1:1 mappings for all memory in use. It then also adds mappings for
 * the kernel nucleus at virtual address of target_kernel_text using large page
 * mappings. The page table pages are also accessible at 1:1 mapped
 * virtual addresses.
 */
/*ARGSUSED*/
void
startup_kernel(void)
{
	char *cmdline;
#if !defined(__xpv)
	int doreloc;
#endif /* !__xpv */

	/*
	 * At this point we are executing in 32-bit protected mode
	 */
	dboot_loader_init();
	cmdline = dboot_loader_cmdline();

	prom_debug = (strstr(cmdline, "prom_debug") != NULL);
	map_debug = (strstr(cmdline, "map_debug") != NULL);
#if !defined(__xpv)
	doreloc = (strstr(cmdline, "no_mb_reloc") == NULL);
#endif


	DBG((uintptr_t)bi);

	/*
	 * Initialize graphical console information in xboot_info (bi)
	 */
	dboot_init_xboot_consinfo();

	bi->bi_cmdline = (native_ptr_t)(uintptr_t)cmdline;

#if !defined(__xpv)
	/* Populate xboot_info with firmware table information */
	dboot_multiboot_get_fwtables();
#endif

	/*
	 * All xbootinfo fields relating to the console, cmdline, and
	 * firmware type (UEFI or BIOS) must be filled-in before calling
	 * bcons_init.
	 */
	bcons_init(bi);

#if !defined(__xpv)
	ktext_phys = FOUR_MEG;		/* from UNIX Mapfile */
#endif

#if !defined(__xpv) && defined(_BOOT_TARGET_amd64)
	/*
	 * For grub, copy kernel bits from the ELF64 file to final place.
	 */
#if 0
	DBG_MSG("\nAllocating nucleus pages.\n");
	ktext_phys = (uintptr_t)do_mem_alloc(ksize, FOUR_MEG);
	if (ktext_phys == 0)
		dboot_panic("failed to allocate aligned kernel memory");
#endif
	if (dboot_elfload64(mb_header.load_addr) != 0)
		dboot_panic("failed to parse kernel ELF image, rebooting");
#endif


#if !defined(__xpv)
	/*
	 * The memory maps must be available for the relocator
	 */
	dboot_process_mmap();

	if (doreloc) {
		dboot_multiboot_relocate();

		/*
		 * Refresh cmdline, since it may have changed due to relocation
		 */
		cmdline = dboot_loader_cmdline();
		bi->bi_cmdline = (native_ptr_t)(uintptr_t)cmdline;
	}

	/*
	 * Now that relocation is complete, calculate the highest address used
	 * (for the memory allocator).
	 */
	dboot_multiboot_highest_addr();

	DBG((uintptr_t)mb_info);
	DBG((uintptr_t)mb2_info);
#endif

	DBG_MSG("\n\nSolaris prekernel set: ");
	DBG_MSG(cmdline);
	DBG_MSG("\n");

	if (strstr(cmdline, "multiboot") != NULL) {
		dboot_panic(NO_MULTIBOOT);
	}

	/*
	 * Need correct target_kernel_text value
	 */
#if defined(_BOOT_TARGET_amd64)
	target_kernel_text = KERNEL_TEXT_amd64;
#elif defined(__xpv)
	target_kernel_text = KERNEL_TEXT_i386_xpv;
#else
	target_kernel_text = KERNEL_TEXT_i386;
#endif

#if defined(__xpv)

	/*
	 * XXPV	Derive this stuff from CPUID / what the hypervisor has enabled
	 */

#if defined(_BOOT_TARGET_amd64)
	/*
	 * 64-bit hypervisor.
	 */
	amd64_support = 1;
	pae_support = 1;

#else	/* _BOOT_TARGET_amd64 */

	/*
	 * See if we are running on a PAE Hypervisor
	 */
	{
		xen_capabilities_info_t caps;

		if (HYPERVISOR_xen_version(XENVER_capabilities, &caps) != 0)
			dboot_panic("HYPERVISOR_xen_version(caps) failed");
		caps[sizeof (caps) - 1] = 0;
		if (prom_debug)
			dboot_printf("xen capabilities %s\n", caps);
		if (strstr(caps, "x86_32p") != NULL)
			pae_support = 1;
	}

#endif	/* _BOOT_TARGET_amd64 */
	{
		xen_platform_parameters_t p;

		if (HYPERVISOR_xen_version(XENVER_platform_parameters, &p) != 0)
			dboot_panic("HYPERVISOR_xen_version(parms) failed");
		DBG(p.virt_start);
		mfn_to_pfn_mapping = (pfn_t *)(xen_virt_start = p.virt_start);
	}

	/*
	 * The hypervisor loads stuff starting at 1Gig
	 */
	mfn_base = ONE_GIG;
	DBG(mfn_base);

	/*
	 * enable writable page table mode for the hypervisor
	 */
	if (HYPERVISOR_vm_assist(VMASST_CMD_enable,
	    VMASST_TYPE_writable_pagetables) < 0)
		dboot_panic("HYPERVISOR_vm_assist(writable_pagetables) failed");

	/*
	 * check for NX support
	 */
	if (pae_support) {
		uint32_t eax = 0x80000000;
		uint32_t edx = get_cpuid_edx(&eax);

		if (eax >= 0x80000001) {
			eax = 0x80000001;
			edx = get_cpuid_edx(&eax);
			if (edx & CPUID_AMD_EDX_NX)
				NX_support = 1;
		}
	}

	/*
	 * check for PAT support
	 */
	{
		uint32_t eax = 1;
		uint32_t edx = get_cpuid_edx(&eax);
		if (edx & CPUID_INTC_EDX_PAT)
			PAT_support = 1;
	}

#if !defined(_BOOT_TARGET_amd64)

	/*
	 * The 32-bit hypervisor uses segmentation to protect itself from
	 * guests. This means when a guest attempts to install a flat 4GB
	 * code or data descriptor the 32-bit hypervisor will protect itself
	 * by silently shrinking the segment such that if the guest attempts
	 * any access where the hypervisor lives a #gp fault is generated.
	 * The problem is that some applications expect a full 4GB flat
	 * segment for their current thread pointer and will use negative
	 * offset segment wrap around to access data. TLS support in linux
	 * brand is one example of this.
	 *
	 * The 32-bit hypervisor can catch the #gp fault in these cases
	 * and emulate the access without passing the #gp fault to the guest
	 * but only if VMASST_TYPE_4gb_segments is explicitly turned on.
	 * Seems like this should have been the default.
	 * Either way, we want the hypervisor -- and not Solaris -- to deal
	 * to deal with emulating these accesses.
	 */
	if (HYPERVISOR_vm_assist(VMASST_CMD_enable,
	    VMASST_TYPE_4gb_segments) < 0)
		dboot_panic("HYPERVISOR_vm_assist(4gb_segments) failed");
#endif	/* !_BOOT_TARGET_amd64 */

#else	/* __xpv */

	/*
	 * use cpuid to enable MMU features
	 */
	if (have_cpuid()) {
		uint32_t eax, edx;

		eax = 1;
		edx = get_cpuid_edx(&eax);
		if (edx & CPUID_INTC_EDX_PSE)
			largepage_support = 1;
		if (edx & CPUID_INTC_EDX_PGE)
			pge_support = 1;
		if (edx & CPUID_INTC_EDX_PAE)
			pae_support = 1;
		if (edx & CPUID_INTC_EDX_PAT)
			PAT_support = 1;

		eax = 0x80000000;
		edx = get_cpuid_edx(&eax);
		if (eax >= 0x80000001) {
			eax = 0x80000001;
			edx = get_cpuid_edx(&eax);
			if (edx & CPUID_AMD_EDX_LM)
				amd64_support = 1;
			if (edx & CPUID_AMD_EDX_NX)
				NX_support = 1;
		}
	} else {
		dboot_printf("cpuid not supported\n");
	}
#endif /* __xpv */

	DBG(PAT_support);


#if defined(_BOOT_TARGET_amd64)
	if (amd64_support == 0)
		dboot_panic("long mode not supported, rebooting");
	else if (pae_support == 0)
		dboot_panic("long mode, but no PAE; rebooting");
#else
	/*
	 * Allow the command line to over-ride use of PAE for 32 bit.
	 */
	if (strstr(cmdline, "disablePAE=true") != NULL) {
		pae_support = 0;
		NX_support = 0;
		amd64_support = 0;
	}
#endif

	/*
	 * initialize the simple memory allocator
	 */
	init_mem_alloc();

#if !defined(__xpv) && !defined(_BOOT_TARGET_amd64)
	/*
	 * disable PAE on 32 bit h/w w/o NX and < 4Gig of memory
	 */
	if (max_mem < FOUR_GIG && NX_support == 0)
		pae_support = 0;
#endif

	/*
	 * configure mmu information
	 */
	if (pae_support) {
		shift_amt = shift_amt_pae;
		ptes_per_table = 512;
		pte_size = 8;
		lpagesize = TWO_MEG;
#if defined(_BOOT_TARGET_amd64)
		top_level = 3;
#else
		top_level = 2;
#endif
	} else {
		pae_support = 0;
		NX_support = 0;
		shift_amt = shift_amt_nopae;
		ptes_per_table = 1024;
		pte_size = 4;
		lpagesize = FOUR_MEG;
		top_level = 1;
	}

	DBG(pge_support);
	DBG(NX_support);
	DBG(largepage_support);
	DBG(amd64_support);
	DBG(top_level);
	DBG(pte_size);
	DBG(ptes_per_table);
	DBG(lpagesize);

#if defined(__xpv)
	ktext_phys = ONE_GIG;		/* from UNIX Mapfile */
#endif

	/*
	 * Allocate page tables.
	 */
	build_page_tables();

	/*
	 * return to assembly code to switch to running kernel
	 */
	entry_addr_low = (uint32_t)target_kernel_text;
	DBG(entry_addr_low);
	bi->bi_use_largepage = largepage_support;
	bi->bi_use_pae = pae_support;
	bi->bi_use_pge = pge_support;
	bi->bi_use_nx = NX_support;

#if defined(__xpv)

	bi->bi_next_paddr = next_avail_addr - mfn_base;
	DBG(bi->bi_next_paddr);
	bi->bi_next_vaddr = (native_ptr_t)next_avail_addr;
	DBG(bi->bi_next_vaddr);

	/*
	 * unmap unused pages in start area to make them available for DMA
	 */
	while (next_avail_addr < scratch_end) {
		(void) HYPERVISOR_update_va_mapping(next_avail_addr,
		    0, UVMF_INVLPG | UVMF_LOCAL);
		next_avail_addr += MMU_PAGESIZE;
	}

	bi->bi_xen_start_info = (uintptr_t)xen_info;
	DBG((uintptr_t)HYPERVISOR_shared_info);
	bi->bi_shared_info = (native_ptr_t)HYPERVISOR_shared_info;
	bi->bi_top_page_table = (uintptr_t)top_page_table - mfn_base;

#else /* __xpv */

	bi->bi_next_paddr = next_avail_addr;
	DBG(bi->bi_next_paddr);
	bi->bi_next_vaddr = (uintptr_t)next_avail_addr;
	DBG(bi->bi_next_vaddr);

	bi->bi_mb_version = multiboot_version;
	if (multiboot_version == 1)
		bi->bi_mb_info = (uintptr_t)mb_info;
	else if (multiboot_version == 2)
		bi->bi_mb_info = (uintptr_t)mb2_info;
	else
		dboot_panic("UNKNOWN MULTIBOOT VERSION\n");

	bi->bi_top_page_table = (uintptr_t)top_page_table;

#endif /* __xpv */

	bi->bi_kseg_size = FOUR_MEG;
	DBG(bi->bi_kseg_size);

#ifndef __xpv
	if (map_debug)
		dump_tables();
#endif

	DBG_MSG("\n\n*** DBOOT DONE -- back to asm to jump to kernel\n\n");
}
