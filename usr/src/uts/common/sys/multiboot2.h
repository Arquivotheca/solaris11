/*  multiboot2.h - Multiboot 2 header file.  */
/*  Copyright (C) 1999,2003,2007,2008,2009,2010  Free Software Foundation, Inc.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL ANY
 *  DEVELOPER OR DISTRIBUTOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
 *  IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _MULTIBOOT2_H
#define _MULTIBOOT2_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <sys/multiboot_types.h>

/* How many bytes from the start of the file we search for the header.  */
#define MULTIBOOT2_SEARCH			32768
#define MULTIBOOT2_HEADER_ALIGN			8

/* The magic field should contain this.  */
#define MULTIBOOT2_HEADER_MAGIC			0xe85250d6

#define	SOLARIS_MB2_CHECKSUM			0x17adaf1a	/* -(magic + length) */

/* This should be in %eax.  */
#define MULTIBOOT2_BOOTLOADER_MAGIC		0x36d76289

/* Alignment of multiboot modules.  */
#define MULTIBOOT2_MOD_ALIGN			0x00001000

/* Alignment of the multiboot info structure.  */
#define MULTIBOOT2_INFO_ALIGN			0x00000008

/* Flags set in the 'flags' member of the multiboot header.  */

#define MULTIBOOT_TAG_ALIGN                  8
#define MULTIBOOT_TAG_TYPE_END               0
#define MULTIBOOT_TAG_TYPE_CMDLINE           1
#define MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME  2
#define MULTIBOOT_TAG_TYPE_MODULE            3
#define MULTIBOOT_TAG_TYPE_BASIC_MEMINFO     4
#define MULTIBOOT_TAG_TYPE_BOOTDEV           5
#define MULTIBOOT_TAG_TYPE_MMAP              6
#define MULTIBOOT_TAG_TYPE_VBE               7
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER       8
#define MULTIBOOT_TAG_TYPE_ELF_SECTIONS      9
#define MULTIBOOT_TAG_TYPE_APM               10
#define	MULTIBOOT_TAG_TYPE_UEFI_INFO         11
#define	MULTIBOOT_TAG_TYPE_ACPI_INFO         12
#define	MULTIBOOT_TAG_TYPE_SMBIOS_INFO       13


#define MULTIBOOT_HEADER_TAG_END  0
#define MULTIBOOT_HEADER_TAG_INFORMATION_REQUEST  1
#define MULTIBOOT_HEADER_TAG_ADDRESS  2
#define MULTIBOOT_HEADER_TAG_ENTRY_ADDRESS  3
#define MULTIBOOT_HEADER_TAG_CONSOLE_FLAGS  4
#define MULTIBOOT_HEADER_TAG_FRAMEBUFFER  5
#define MULTIBOOT_HEADER_TAG_MODULE_ALIGN  6

#define MULTIBOOT_ARCHITECTURE_I386  0
#define MULTIBOOT_ARCHITECTURE_MIPS32  4
#define MULTIBOOT_HEADER_TAG_OPTIONAL 1

#define MULTIBOOT_CONSOLE_FLAGS_CONSOLE_REQUIRED 1
#define MULTIBOOT_CONSOLE_FLAGS_EGA_TEXT_SUPPORTED 2

#define	MULTIBOOT_MAX_COLORS	256

#ifndef _ASM

struct multiboot2_header
{
  /* Must be MULTIBOOT_MAGIC - see above.  */
  multiboot_uint32_t magic;

  /* ISA */
  multiboot_uint32_t architecture;

  /* Total header length.  */
  multiboot_uint32_t header_length;

  /* The above fields plus this one must equal 0 mod 2^32. */
  multiboot_uint32_t checksum;
};

struct multiboot_header_tag
{
  multiboot_uint16_t type;
  multiboot_uint16_t flags;
  multiboot_uint32_t size;
};

struct multiboot_header_tag_information_request
{
  multiboot_uint16_t type;
  multiboot_uint16_t flags;
  multiboot_uint32_t size;
  multiboot_uint32_t requests[1];
};

struct multiboot_header_tag_address
{
  multiboot_uint16_t type;
  multiboot_uint16_t flags;
  multiboot_uint32_t size;
  multiboot_uint32_t header_addr;
  multiboot_uint32_t load_addr;
  multiboot_uint32_t load_end_addr;
  multiboot_uint32_t bss_end_addr;
};

struct multiboot_header_tag_entry_address
{
  multiboot_uint16_t type;
  multiboot_uint16_t flags;
  multiboot_uint32_t size;
  multiboot_uint32_t entry_addr;
};

struct multiboot_header_tag_console_flags
{
  multiboot_uint16_t type;
  multiboot_uint16_t flags;
  multiboot_uint32_t size;
  multiboot_uint32_t console_flags;
};

struct multiboot_header_tag_framebuffer
{
  multiboot_uint16_t type;
  multiboot_uint16_t flags;
  multiboot_uint32_t size;
  multiboot_uint32_t width;
  multiboot_uint32_t height;
  multiboot_uint32_t depth;
};

struct multiboot_header_tag_module_align
{
  multiboot_uint16_t type;
  multiboot_uint16_t flags;
  multiboot_uint32_t size;
  multiboot_uint32_t width;
  multiboot_uint32_t height;
  multiboot_uint32_t depth;
};

struct multiboot_tag
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;
};

struct multiboot2_info_header
{
  multiboot_uint32_t total_size;
  multiboot_uint32_t reserved;
  struct multiboot_tag tags[1];
};

struct multiboot_tag_string
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;
  char string[1];
};

struct multiboot_tag_module
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;
  multiboot_uint32_t mod_start;
  multiboot_uint32_t mod_end;
  char cmdline[1];
};

struct multiboot_tag_basic_meminfo
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;
  multiboot_uint32_t mem_lower;
  multiboot_uint32_t mem_upper;
};

struct multiboot_tag_bootdev
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;
  multiboot_uint32_t biosdev;
  multiboot_uint32_t slice;
  multiboot_uint32_t part;
};

struct multiboot2_mmap_entry
{
  multiboot_uint64_t addr;
  multiboot_uint64_t len;
#define MULTIBOOT_MEMORY_AVAILABLE		1
#define MULTIBOOT_MEMORY_RESERVED		2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE       3
#define MULTIBOOT_MEMORY_NVS                    4
#define	MULTIBOOT_MEMORY_BADRAM			5
#define	MULTIBOOT_MEMORY_RUNTIME		6
  multiboot_uint32_t type;
  multiboot_uint32_t zero;
} __attribute__((packed));
typedef struct multiboot2_mmap_entry multiboot2_memory_map_t;

struct multiboot_tag_mmap
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;
  multiboot_uint32_t entry_size;
  multiboot_uint32_t entry_version;
  struct multiboot2_mmap_entry entries[1];  
};

struct multiboot_vbe_info_block
{
  multiboot_uint8_t external_specification[512];
};

struct multiboot_vbe_mode_info_block
{
  multiboot_uint8_t external_specification[256];
};

struct multiboot_tag_vbe
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;

  multiboot_uint16_t vbe_mode;
  multiboot_uint16_t vbe_interface_seg;
  multiboot_uint16_t vbe_interface_off;
  multiboot_uint16_t vbe_interface_len;

  struct multiboot_vbe_info_block vbe_control_info;
  struct multiboot_vbe_mode_info_block vbe_mode_info;
};

struct multiboot_tag_framebuffer_common
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;

  multiboot_uint64_t framebuffer_addr;
  multiboot_uint32_t framebuffer_pitch;
  multiboot_uint32_t framebuffer_width;
  multiboot_uint32_t framebuffer_height;
  multiboot_uint8_t framebuffer_bpp;
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB     1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT	2
  multiboot_uint8_t framebuffer_type;
  multiboot_uint16_t reserved;
};

struct multiboot_tag_framebuffer
{
  struct multiboot_tag_framebuffer_common common;

  union
  {
    struct
    {
      multiboot_uint16_t framebuffer_palette_num_colors;
      struct multiboot_color framebuffer_palette[1];
    } fb1;
    struct
    {
      multiboot_uint8_t framebuffer_red_field_position;
      multiboot_uint8_t framebuffer_red_mask_size;
      multiboot_uint8_t framebuffer_green_field_position;
      multiboot_uint8_t framebuffer_green_mask_size;
      multiboot_uint8_t framebuffer_blue_field_position;
      multiboot_uint8_t framebuffer_blue_mask_size;
    } fb2;
  } u1;
};

struct multiboot_tag_elf_sections
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;
  multiboot_uint32_t num;
  multiboot_uint32_t entsize;
  multiboot_uint32_t shndx;
  char sections[1];
};

struct multiboot_tag_apm
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;
  multiboot_uint16_t version;
  multiboot_uint16_t cseg;
  multiboot_uint32_t offset;
  multiboot_uint16_t cseg_16;
  multiboot_uint16_t dseg;
  multiboot_uint16_t flags;
  multiboot_uint16_t cseg_len;
  multiboot_uint16_t cseg_16_len;
  multiboot_uint16_t dseg_len;
};

struct multiboot_tag_uefi_info
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;
  multiboot_uint64_t uefi_systab_addr;
  multiboot_uint32_t flags;
#define	GRUB_UEFI_INFO_UEFI32	0x00000001
#define	GRUB_UEFI_INFO_UEFI64	0x00000002
};

struct multiboot_tag_acpi_info
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;
  multiboot_uint64_t acpi_rsdp_addr;
};

struct multiboot_tag_smbios_info
{
  multiboot_uint32_t type;
  multiboot_uint32_t size;
  multiboot_uint64_t smbios_addr;
};

#endif /* _ASM */


#ifdef  __cplusplus
}
#endif

#endif  /* _MULTIBOOT_H */
