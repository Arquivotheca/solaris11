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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* This file is the main module for the pcitool. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/inttypes.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/param.h>
#include <fcntl.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <libdevinfo.h>
#include <sys/sunddi.h>

#ifdef __x86
#include <sys/apic_ctlr.h>
#endif

#include <sys/pci.h>
#include <sys/pcie.h>
#include <sys/pci_tools.h>

#include "pcitool_ui.h"

/* First 16 longs of device PCI config header. */
typedef union {
	uint8_t bytes[16 * sizeof (uint32_t)];
	uint32_t dwords[16];
} pci_conf_hdr_t;

/* Used by probe printing functions. */
typedef struct {
	uint16_t cfg_offset;	/* Offset of data within config space. */
	uint8_t size;		/* Size of desired data field. */
	char *abbrev_hdr;	/* Abbreviated header for this data. */
	char *full_hdr;		/* Full header for this data, verbose option. */
} field_type_t;

/* Used to package many args into one arg for probe di_node walk function. */
typedef struct {
	pcitool_uiargs_t *input_args_p;
	char *pathname;
	di_prom_handle_t di_phdl;
} probe_walk_args_t;

/*
 * Read config space in native processor endianness.  Endian-neutral
 * processing can then take place.  On big endian machines, MSB and LSB
 * of little endian data end up switched if read as little endian.
 * They are in correct order if read as big endian.
 */
#if defined(__sparc)
#define	NATIVE_ENDIAN	PCITOOL_ACC_ATTR_ENDN_BIG
#elif defined(__x86)
#define	NATIVE_ENDIAN	PCITOOL_ACC_ATTR_ENDN_LTL
#else
#error "ISA is neither __sparc nor __x86"
#endif

#define	INIT_NUM_DEVS	0
#define	APIC_MAX_VECTOR 255

/* status error lookup table. */
static struct {
	pcitool_errno_t	value;
	char		*string;
} pcitool_stat_str[] = {
	{ PCITOOL_SUCCESS,
		"No error status returned from driver" },
	{ PCITOOL_INVALID_CPUID,
		"CPU is non-existent or not online" },
	{ PCITOOL_INVALID_INO,
		"INO is out of range or invalid" },
	{ PCITOOL_INVALID_MSI,
		"MSI is out of range or invalid" },
	{ PCITOOL_PENDING_INTRTIMEOUT,
		"Timeout waiting for pending interrupts to clear" },
	{ PCITOOL_REGPROP_NOTWELLFORMED,
		"Reg property has invalid format" },
	{ PCITOOL_INVALID_ADDRESS,
		"Address out of range or invalid" },
	{ PCITOOL_NOT_ALIGNED,
		"Improper address alignment for access attempted" },
	{ PCITOOL_OUT_OF_RANGE,
		"Argument out of range" },
	{ PCITOOL_END_OF_RANGE,
		"End of address range" },
	{ PCITOOL_ROM_DISABLED,
		"Device ROM is disabled.  Cannot read" },
	{ PCITOOL_ROM_WRITE,
		"Write to ROM not allowed" },
	{ PCITOOL_IO_ERROR,
		"IO error encountered" },
	{ PCITOOL_INVALID_SIZE,
		"Size is invalid for this platform" },
	{ PCITOOL_INVALID_ARG,
		"Argument is invalid for this operation" },
	{ PCITOOL_OPERATION_NOTSUP,
		"Device doesn't support the operation" },
	{ PCITOOL_DEVICE_BUSY,
		"Device is busy, try again later" },
	{ PCITOOL_LINK_OPERATION_FAIL,
		"Link retrain or equalization perform without"
		" link bandwidth change" },
	{ PCITOOL_LINK_DOWN,
		"Link is down during link retrain or equalization" },
	{ 0, NULL }
};

typedef struct {
	uint8_t		value;
	char		*string;
} pcitool_str_t;

static pcitool_str_t pcitool_cto_str[] = {
	{ 0,	"Default Range: 50us - 50ms" },
	{ 0x1,	"Range A: 50us - 100us(< 1ms)" },
	{ 0x2,	"Range A: 1ms - 10ms" },
	{ 0x5,	"Range B: 10ms - 55ms" },
	{ 0x6,	"Range B: 55ms - 250ms" },
	{ 0x9,	"Range C: 250ms - 900ms" },
	{ 0xa,	"Range C: 900ms - 4000ms" },
	{ 0xd,	"Range D: 4s - 13s" },
	{ 0xe,	"Range D: 17s - 64s" },
	{0xff,	"Device doesn't support cto"},
	{ 0, 	NULL }
};

static pcitool_str_t pcitool_ls_str[] = {
	{ 0x1,	"2.5GT/s(Gen1)" },
	{ 0x2,	"5.0GT/s(Gen2)" },
	{ 0x3,	"2.5GT/s(Gen1); 5.0GT/s(Gen2)" },
	{ 0x4,	"8.0GT/s(Gen3)" },
	{ 0x5,	"2.5GT/s(Gen1); 8.0GT/s(Gen3)" },
	{ 0x6,	"5.0GT/s(Gen2); 8.0GT/s(Gen3)" },
	{ 0x7,	"2.5GT/s(Gen1); 5.0GT/s(Gen2); 8.0GT/s(Gen3)" },
	{ 0, 	NULL }
};

/* Used with ^C handler to stop looping in repeat mode in do_device_or_nexus. */
static boolean_t keep_looping = B_TRUE;

static void signal_handler(int dummy);
static char *strstatus(pcitool_errno_t pcitool_status);
static int open_node(char *device, pcitool_uiargs_t *input_args_p);
static void print_probe_value(pci_conf_hdr_t *config_hdr_p, uint16_t offset,
    uint8_t size);
static void print_probe_info_verbose(pci_conf_hdr_t *config_hdr_p,
    pci_conf_hdr_t *pcie_conf_p, pcitool_reg_t *info_p);
static void print_probe_info_nonverbose(pci_conf_hdr_t *config_hdr_p,
    pci_conf_hdr_t *pcie_conf_p, pcitool_reg_t *info_p);
static void print_probe_info(pci_conf_hdr_t *config_hdr_p,
    pci_conf_hdr_t *pcie_conf_p, pcitool_reg_t *info_p, boolean_t verbose);
static int get_config_header(int fd, uint8_t bus_no, uint8_t dev_no,
    uint8_t func_no, pci_conf_hdr_t *config_hdr_p);
static int supports_ari(int fd, uint8_t bus_no);
static int probe_dev(int fd, pcitool_reg_t *prg_p,
    pcitool_uiargs_t *input_args_p);
static int do_probe(int fd, di_node_t di_node, di_prom_handle_t di_phdl,
    pcitool_uiargs_t *input_args_p);
static int process_nexus_node(di_node_t node, di_minor_t minor, void *arg);
static int do_probe_walk(pcitool_uiargs_t *input_args_p, char *pathname);
static void print_bytedump_header(boolean_t do_chardump);
static int bytedump_get(int fd, int cmd, pcitool_reg_t *prg_p,
    pcitool_uiargs_t *input_args_p);
static uint32_t set_acc_attr(pcitool_uiargs_t *input_args_p);
static int do_single_access(int fd, int cmd, pcitool_reg_t *prg_p,
    pcitool_uiargs_t *input_args_p);
static int do_device_or_nexus(int fd, pcitool_uiargs_t *input_args_p);
static void print_intr_info(pcitool_intr_get_t *iget_p, int type);
static pcitool_intr_get_t *get_single_intr(int fd,
    pcitool_uiargs_t *input_args_p);
static int get_interrupts(int fd, pcitool_uiargs_t *input_args_p);
static int set_interrupts(int fd, pcitool_uiargs_t *input_args_p);
static int do_interrupts(int fd, pcitool_uiargs_t *input_args_p);


/* *************** General ************** */

/*
 * Handler for ^C to stop looping.
 */
/*ARGSUSED*/
static void
signal_handler(int dummy)
{
	keep_looping = B_FALSE;
}


/*
 * Print string based on PCItool status returned from driver.
 */
static char *
strstatus(pcitool_errno_t pcitool_status)
{
	int i;

	for (i = 0; pcitool_stat_str[i].string != NULL; i++) {
		if (pcitool_stat_str[i].value == pcitool_status) {

			return (pcitool_stat_str[i].string);
		}
	}

	return ("Unknown status returned from driver.");
}

/*
 * Print string based on values.
 */
static char *
value_to_str(uint8_t value, pcitool_str_t *str_p)
{
	while (str_p->string != NULL && str_p->value != value)
		str_p++;

	return (str_p->string == NULL ? "Invalid value" : str_p->string);
}

static int
open_node(char *device, pcitool_uiargs_t *input_args_p)
{
	int fd;
	static char path[MAXPATHLEN];    /* For building full nexus pathname. */
	char *prefix;
	char *suffix;
	char *format;

	static char slash_devices[] = {"/devices"};
	static char wcolon[] = {"%s%s:%s"};
	static char wocolon[] = {"%s%s%s"};

	/* Check for names starting with /devices. */
	prefix = (strstr(device, slash_devices) == device) ? "" : slash_devices;

	format = wcolon;
	if (input_args_p->flags & INTR_FLAG) {
		if (strstr(device, PCI_MINOR_INTR) ==
		    device + (strlen(device) - strlen(PCI_MINOR_INTR))) {
			suffix = "";
			format = wocolon;
		} else {
			suffix = PCI_MINOR_INTR;
		}
	} else {
		if (strstr(device, PCI_MINOR_REG) ==
		    device + (strlen(device) - strlen(PCI_MINOR_REG))) {
			suffix = "";
			format = wocolon;
		} else {
			suffix = PCI_MINOR_REG;
		}
	}

	/*
	 * Build nexus pathname.
	 * User specified /pci@1f,700000 becomes /devices/pci@1f,700000:intr
	 * for interrupt nodes, and ...:reg for register nodes.
	 */
	/*LINTED*/
	if (snprintf(path, MAXPATHLEN, format, prefix, device, suffix) >
	    MAXPATHLEN) {
		(void) fprintf(stderr, "Nexus pathname is overlength.\n");
		return (-1);
	}

	/* Open the nexus. */
	if ((fd = open(path, O_RDWR)) == -1) {
		if (!(IS_QUIET(input_args_p->flags))) {
			(void) fprintf(stderr,
			    "Could not open nexus node %s: %s\n",
			    path, strerror(errno));
		}
	}

	return (fd);
}


/* ****************** Probe **************** */

/* The following are used by the probe printing functions. */

/* Header 0 and 1 config space headers have these fields. */
static field_type_t first_fields[] = {
	{ PCI_CONF_VENID, 2, "Vend", "Vendor ID" },
	{ PCI_CONF_DEVID, 2, "Dev ", "Device ID" },
	{ PCI_CONF_COMM, 2, "Cmd ", "Command" },
	{ PCI_CONF_STAT, 2, "Stat", "Status" },
	{ PCI_CONF_REVID, 1, "Rv", "Revision ID" },
	{ PCI_CONF_PROGCLASS, 3, "Class ", "Class Code" },
	{ PCI_CONF_CACHE_LINESZ, 1, "Ca", "Cache Line Size" },
	{ PCI_CONF_LATENCY_TIMER, 1, "LT", "Latency Timer" },
	{ PCI_CONF_HEADER, 1, "Hd", "Header Type" },
	{ PCI_CONF_BIST, 1, "BI", "BIST" },
	{ PCIE_LS_OFF, 1, "LS", "Link Speed" },
	{ PCIE_LW_OFF, 1, "LW", "Link Width" },
	{ 0, 0, NULL, NULL }
};

/* Header 0 (for regular devices) have these fields. */
static field_type_t last_dev_fields[] = {
	{ PCI_CONF_BASE0, 4, "BAR0", "Base Address Register 0 (@10)" },
	{ PCI_CONF_BASE1, 4, "BAR1", "Base Address Register 1 (@14)" },
	{ PCI_CONF_BASE2, 4, "BAR2", "Base Address Register 2 (@18)" },
	{ PCI_CONF_BASE3, 4, "BAR3", "Base Address Register 3 (@1C)" },
	{ PCI_CONF_BASE4, 4, "BAR4", "Base Address Register 4 (@20)" },
	{ PCI_CONF_BASE5, 4, "BAR5", "Base Address Register 5 (@24)" },
	{ PCI_CONF_ROM, 4, "ROM", "Expansion ROM Base Address Register (@30)" },
	{ 0, 0, NULL, NULL }
};

/* Header 1 (PCI-PCI bridge devices) have these fields. */
static field_type_t last_pcibrg_fields[] = {
	{ PCI_CONF_BASE0, 4, "BAR0", "Base Address Register 0 (@10)" },
	{ PCI_CONF_BASE1, 4, "BAR1", "Base Address Register 1 (@14)" },
	{ PCI_BCNF_ROM, 4, "ROM", "Expansion ROM Base Address Register (@38)" },
	{ 0, 0, NULL, NULL }
};

/* Header 2 (PCI-Cardbus bridge devices) have these fields. */
static field_type_t last_cbbrg_fields[] = {
	{ PCI_CBUS_SOCK_REG, 4, "SCKT", "Socket/ExCA Base Address (@10)" },
	{ 0, 0, NULL, NULL }
};

/* PCIe devices have these fields. */
static field_type_t last_pcie_fields[] = {
	{PCIE_MLS_OFF, 1,	"MLS",	"Max Link Speed"},
	{PCIE_MLW_OFF, 1,	"MLW",	"Max Link Width"},
	{PCIE_SLS_OFF, 1,	"SLS",	"Supported Link Speeds"},
	{PCIE_CTO_OFF, 1,	"CTO",	"Completion Timeout Range"},
	{0, 0,	NULL,	NULL}
};

#define	FMT_SIZE 7

static void
print_probe_value(pci_conf_hdr_t *config_hdr_p, uint16_t offset, uint8_t size)
{

	char format[FMT_SIZE];


	/* Size cannot be any larger than 4 bytes.  This is not checked. */
	uint32_t value = 0;

	/* Build format of print, "%<size*2>.<size*2>x" */
	(void) snprintf(format, FMT_SIZE, "%%%d.%dx ", size * 2, size * 2);

	while (size-- > 0) {
		value = (value << 8) + config_hdr_p->bytes[offset + size];
	}

	/*LINTED*/
	(void) printf(format, value);
}

static void
print_probe_info_verbose(pci_conf_hdr_t *config_hdr_p,
    pci_conf_hdr_t *pcie_conf_p, pcitool_reg_t *info_p)
{
	field_type_t *last_fields = NULL;
	pci_conf_hdr_t *conf_p = NULL;
	int i;

	(void) printf("\n"
	    "Bus Number: %x Device Number: %x Function Number: %x\n",
	    info_p->bus_no, info_p->dev_no, info_p->func_no);
	if (info_p->phys_addr != 0) {
		(void) printf("Physical Address: 0x%" PRIx64 " \n",
		    info_p->phys_addr);
	}

	switch (config_hdr_p->bytes[PCI_CONF_HEADER] & PCI_HEADER_TYPE_M) {

	case PCI_HEADER_ZERO:	/* Header type 0 is a regular device. */
		last_fields = last_dev_fields;
		break;

	case PCI_HEADER_PPB:	/* Header type 1 is a PCI-PCI bridge. */
		last_fields = last_pcibrg_fields;
		(void) printf("PCI-PCI bridge\n");
		break;

	case PCI_HEADER_CARDBUS: /* Header type 2 is a cardbus bridge */
		last_fields = last_cbbrg_fields;
		(void) printf("PCI-Cardbus bridge\n");
		break;

	default:
		(void) printf("Unknown device\n");
		break;
	}

	if (last_fields != NULL) {

		for (i = 0; first_fields[i].size != 0; i++) {
			(void) printf("%s: ", first_fields[i].full_hdr);
			conf_p = (i >= PCI_FIRST_FIELDS_NUM ? pcie_conf_p :
			    config_hdr_p);
			print_probe_value(conf_p,
			    first_fields[i].cfg_offset, first_fields[i].size);
			(void) putchar('\n');
		}

		if (pcie_conf_p->dwords[0] != 0) {
			for (i = 0; last_pcie_fields[i].size != 0; i++) {
				(void) printf("%s: ",
				    last_pcie_fields[i].full_hdr);
				print_probe_value(pcie_conf_p,
				    last_pcie_fields[i].cfg_offset,
				    last_pcie_fields[i].size);

				/* Print string of CTO & Supported Link Speed */
				if (last_pcie_fields[i].cfg_offset ==
				    PCIE_CTO_OFF) {
					(void) printf("(%s)", value_to_str(
					    pcie_conf_p->bytes[PCIE_CTO_OFF],
					    pcitool_cto_str));
				} else if (last_pcie_fields[i].cfg_offset ==
				    PCIE_SLS_OFF) {
					(void) printf("(%s)", value_to_str(
					    pcie_conf_p->bytes[PCIE_SLS_OFF],
					    pcitool_ls_str));
				}
				(void) putchar('\n');
			}
		}

		for (i = 0; last_fields[i].size != 0; i++) {
			(void) printf("%s: ", last_fields[i].full_hdr);
			print_probe_value(config_hdr_p,
			    last_fields[i].cfg_offset, last_fields[i].size);
			(void) putchar('\n');
		}
	}
}

static void
print_probe_info_nonverbose(pci_conf_hdr_t *config_hdr_p,
    pci_conf_hdr_t *pcie_conf_p, pcitool_reg_t *info_p)
{
	int i;
	pci_conf_hdr_t *conf_p;

	(void) printf("%2.2x %2.2x %1.1x ",
	    info_p->bus_no, info_p->dev_no, info_p->func_no);
	for (i = 0; first_fields[i].size != 0; i++) {
		conf_p = (i >= PCI_FIRST_FIELDS_NUM ? pcie_conf_p :
		    config_hdr_p);
		print_probe_value(conf_p,
		    first_fields[i].cfg_offset, first_fields[i].size);
	}
	(void) putchar('\n');
}


/*
 * Print device information retrieved during probe mode.
 * Takes the PCI config header, plus address information retrieved from the
 * driver.
 *
 * When called with config_hdr_p == NULL, this function just prints a header
 * when not in verbose mode.
 */

static void
print_probe_info(
    pci_conf_hdr_t *config_hdr_p, pci_conf_hdr_t *pcie_conf_p,
    pcitool_reg_t *info_p, boolean_t verbose)
{
	int i;

	/* Print header if not in verbose mode. */
	if (config_hdr_p == NULL) {
		if (!verbose) {

			/* Bus dev func not from tble */
			(void) printf("B  D  F ");

			for (i = 0; first_fields[i].size != 0; i++) {
				(void) printf("%s ",
				    first_fields[i].abbrev_hdr);
			}
			(void) putchar('\n');
		}

		return;
	}

	if (verbose) {
		print_probe_info_verbose(config_hdr_p, pcie_conf_p, info_p);
	} else {
		print_probe_info_nonverbose(config_hdr_p, pcie_conf_p, info_p);
	}
}


/*
 * Retrieve first 16 dwords of device's config header, except for the first
 * dword.  First 16 dwords are defined by the PCI specification.
 */
static int
get_config_header(int fd, uint8_t bus_no, uint8_t dev_no, uint8_t func_no,
    pci_conf_hdr_t *config_hdr_p)
{
	pcitool_reg_t cfg_prg;
	int i;
	int rval = SUCCESS;

	/* Prepare a local pcitool_reg_t so as to not disturb the caller's. */
	cfg_prg.offset = 0;
	cfg_prg.acc_attr = PCITOOL_ACC_ATTR_SIZE_4 + NATIVE_ENDIAN;
	cfg_prg.bus_no = bus_no;
	cfg_prg.dev_no = dev_no;
	cfg_prg.func_no = func_no;
	cfg_prg.barnum = 0;
	cfg_prg.user_version = PCITOOL_VERSION;

	/* Get dwords 1-15 of config space. They must be read as uint32_t. */
	for (i = 1; i < (sizeof (pci_conf_hdr_t) / sizeof (uint32_t)); i++) {
		cfg_prg.offset += sizeof (uint32_t);
		if ((rval =
		    ioctl(fd, PCITOOL_DEVICE_GET_REG, &cfg_prg)) != SUCCESS) {
			break;
		}
		config_hdr_p->dwords[i] = (uint32_t)cfg_prg.data;
	}
	return (rval);
}

static int
get_pcie_capability(int fd, uint8_t bus_no, uint8_t dev_no, uint8_t func_no,
    pcitool_reg_t *cfg_prg_p)
{
	int deadcount = 0;
	uint32_t data, hdr_next_ptr, hdr_cap_id;

	/* Prepare a local pcitool_reg_t so as to not disturb the caller's. */
	cfg_prg_p->bus_no = bus_no;
	cfg_prg_p->dev_no = dev_no;
	cfg_prg_p->func_no = func_no;
	cfg_prg_p->barnum = 0;
	cfg_prg_p->user_version = PCITOOL_VERSION;
	cfg_prg_p->offset = 0;
	cfg_prg_p->acc_attr = PCITOOL_ACC_ATTR_SIZE_4 +
	    PCITOOL_ACC_ATTR_ENDN_LTL;

	if (ioctl(fd, PCITOOL_DEVICE_GET_REG, cfg_prg_p) != SUCCESS) {
		return (FAILURE);
	}

	data = (uint32_t)cfg_prg_p->data;
	if (data == (uint32_t)(-1))
		return (FAILURE);

	cfg_prg_p->offset = PCI_CONF_COMM;
	if (ioctl(fd, PCITOOL_DEVICE_GET_REG, cfg_prg_p) != SUCCESS) {
		return (FAILURE);
	}

	data = (uint32_t)cfg_prg_p->data;
	if (!((data >> 16) & PCI_STAT_CAP))
		return (FAILURE);

	cfg_prg_p->offset = PCI_CONF_CAP_PTR;
	if (ioctl(fd, PCITOOL_DEVICE_GET_REG, cfg_prg_p) != SUCCESS) {
		return (FAILURE);
	}
	data = (uint32_t)cfg_prg_p->data;
	hdr_next_ptr = data & 0xff;
	hdr_cap_id = 0;

	/*
	 * Find the PCIe capability.
	 */
	while ((hdr_next_ptr != PCI_CAP_NEXT_PTR_NULL) &&
	    (hdr_cap_id != PCI_CAP_ID_PCI_E)) {

		if (hdr_next_ptr < 0x40)
			break;

		cfg_prg_p->offset = hdr_next_ptr;

		if (ioctl(fd, PCITOOL_DEVICE_GET_REG, cfg_prg_p) != SUCCESS)
			return (FAILURE);

		data = (uint32_t)cfg_prg_p->data;

		hdr_next_ptr = (data >> 8) & 0xFF;
		hdr_cap_id = data & 0xFF;

		if (deadcount++ > 100)
			return (FAILURE);
	}

	if (hdr_cap_id != PCI_CAP_ID_PCI_E)
		return (FAILURE);

	return (SUCCESS);
}

static int
get_pcie_configuration(int fd, uint8_t bus_no, uint8_t dev_no, uint8_t func_no,
    pci_conf_hdr_t *pcie_conf_p)
{
	pcitool_reg_t cfg_prg;
	uint64_t offset;
	uint8_t	type, version;

	if (get_pcie_capability(fd, bus_no, dev_no, func_no, &cfg_prg) !=
	    SUCCESS)
		return (FAILURE);

	offset = cfg_prg.offset;

	/* get link status register */
	cfg_prg.offset = offset + 0x10;
	if (ioctl(fd, PCITOOL_DEVICE_GET_REG, &cfg_prg) != SUCCESS)
		return (FAILURE);

	/* Get link speed & width */
	pcie_conf_p->bytes[PCIE_LS_OFF] = (uint8_t)(cfg_prg.data >> 16) &
	    PCIE_LINKSTS_SPEED_MASK;
	pcie_conf_p->bytes[PCIE_LW_OFF] = (uint8_t)((cfg_prg.data >> 16) &
	    PCIE_LINKSTS_NEG_WIDTH_MASK) >> 4;

	/* get Link Capability register */
	cfg_prg.offset = offset + 0xc;
	if (ioctl(fd, PCITOOL_DEVICE_GET_REG, &cfg_prg) != SUCCESS)
		return (FAILURE);

	/* Get max link speed */
	pcie_conf_p->bytes[PCIE_MLS_OFF] = (uint8_t)(cfg_prg.data &
	    PCIE_LINKCAP_MAX_SPEED_MASK);
	/* Get max link width */
	pcie_conf_p->bytes[PCIE_MLW_OFF] = (uint8_t)(cfg_prg.data &
	    PCIE_LINKCAP_MAX_WIDTH_MASK) >> 4;
	/* Get supported link speeds */
	pcie_conf_p->bytes[PCIE_SLS_OFF] = (uint8_t)
	    (pcie_conf_p->bytes[PCIE_MLS_OFF] | 0x1);
	pcie_conf_p->bytes[PCIE_CTO_OFF] = 0xff;

	/* Get PCIe version and type */
	cfg_prg.offset = offset;
	if (ioctl(fd, PCITOOL_DEVICE_GET_REG, &cfg_prg) != SUCCESS)
		return (FAILURE);

	version = (uint8_t)((cfg_prg.data >> 16) & PCIE_PCIECAP_VER_MASK);
	type = (cfg_prg.data >> 16) & PCIE_PCIECAP_DEV_TYPE_MASK;

	if (version < 2)
		return (SUCCESS);

	/* Get Link Capability 2 register */
	cfg_prg.offset = offset + 0x2c;
	if (ioctl(fd, PCITOOL_DEVICE_GET_REG, &cfg_prg) == SUCCESS) {
		if (cfg_prg.data & PCIE_LINKCAP2_LINK_SPEED_VECTOR_MASK)
			pcie_conf_p->bytes[PCIE_SLS_OFF] = (uint8_t)
			    ((cfg_prg.data &
			    PCIE_LINKCAP2_LINK_SPEED_VECTOR_MASK) >> 1);
	}

	/* Check if CTO is supported */
	if (type == PCIE_PCIECAP_DEV_TYPE_PCIE_DEV ||
	    type == PCIE_PCIECAP_DEV_TYPE_ROOT ||
	    type == PCIE_PCIECAP_DEV_TYPE_PCIE2PCI) {
		/* Get Device Control 2 register */
		cfg_prg.offset = offset + 0x28;
		if (ioctl(fd, PCITOOL_DEVICE_GET_REG, &cfg_prg) != SUCCESS)
			return (FAILURE);
		/* Get CTO range */
		pcie_conf_p->bytes[PCIE_CTO_OFF] = (uint8_t)(cfg_prg.data &
		    PCIE_DEVCTL2_COM_TO_RANGE_MASK);
	}
	return (SUCCESS);
}

static int
supports_ari(int fd, uint8_t bus_no)
{
	pcitool_reg_t cfg_prg;
	int deadcount = 0;
	uint32_t data, hdr_next_ptr, hdr_cap_id;

	if (get_pcie_capability(fd, bus_no, 0, 0, &cfg_prg) != SUCCESS)
		return (FAILURE);

	/* Found a PCIe Capability */

	hdr_next_ptr = 0x100;
	hdr_cap_id = 0;

	/*
	 * Now find the ARI Capability.
	 */
	while ((hdr_next_ptr != PCI_CAP_NEXT_PTR_NULL) &&
	    (hdr_cap_id != 0xe)) {

		if (hdr_next_ptr < 0x40)
			break;

		cfg_prg.offset = hdr_next_ptr;

		if (ioctl(fd, PCITOOL_DEVICE_GET_REG, &cfg_prg) != SUCCESS) {
			return (FAILURE);
		}
		data = (uint32_t)cfg_prg.data;

		hdr_next_ptr = (data >> 20) & 0xFFF;
		hdr_cap_id = data & 0xFFFF;

		if (deadcount++ > 100)
			return (FAILURE);
	}

	if (hdr_cap_id != 0xe)
		return (FAILURE);

	return (SUCCESS);
}

/*
 * Identify problematic southbridges.  These have device id 0x5249 and
 * vendor id 0x10b9.  Check for revision ID 0 and class code 060400 as well.
 * Values are little endian, so they are reversed for SPARC.
 *
 * Check for these southbridges on all architectures, as the issue is a
 * southbridge issue, independent of processor.
 *
 * If one of these is found during probing, skip probing other devs/funcs on
 * the rest of the bus, since the southbridge and all devs underneath will
 * otherwise disappear.
 */
#if (NATIVE_ENDIAN == PCITOOL_ACC_ATTR_ENDN_BIG)
#define	U45_SB_DEVID_VID	0xb9104952
#define	U45_SB_CLASS_RID	0x00000406
#else
#define	U45_SB_DEVID_VID	0x524910b9
#define	U45_SB_CLASS_RID	0x06040000
#endif

/*
 * Probe device's functions.  Modifies many fields in the prg_p.
 */
static int
probe_dev(int fd, pcitool_reg_t *prg_p, pcitool_uiargs_t *input_args_p)
{
	pci_conf_hdr_t	config_hdr;
	pci_conf_hdr_t	pcie_conf;
	boolean_t	multi_function_device = B_FALSE;
	int		func;
	int		first_func = 0;
	int		last_func = PCI_REG_FUNC_M >> PCI_REG_FUNC_SHIFT;
	int		rval = SUCCESS;

	if (input_args_p->flags & FUNC_SPEC_FLAG) {
		first_func = last_func = input_args_p->function;
	} else if (supports_ari(fd, prg_p->bus_no) == SUCCESS) {
		multi_function_device = B_TRUE;
		if (!(input_args_p->flags & DEV_SPEC_FLAG))
			last_func = 255;
	}

	/*
	 * Loop through at least func=first_func.  Continue looping through
	 * functions if there are no errors and the device is a multi-function
	 * device.
	 *
	 * (Note, if first_func == 0, header will show whether multifunction
	 * device and set multi_function_device.  If first_func != 0, then we
	 * will force the loop as the user wants a specific function to be
	 * checked.
	 */
	for (func = first_func;  ((func <= last_func) &&
	    ((func == first_func) || (multi_function_device)));
	    func++) {
		if (last_func > 7) {
			prg_p->func_no = func & 0x7;
			prg_p->dev_no = (func >> 3) & 0x1f;
		} else
			prg_p->func_no = func;

		/*
		 * Four things can happen here:
		 *
		 * 1) ioctl comes back as EFAULT and prg_p->status is
		 *    PCITOOL_INVALID_ADDRESS.  There is no device at this
		 *    location.
		 *
		 * 2) ioctl comes back successful and the data comes back as
		 *    zero.  Config space is mapped but no device responded.
		 *
		 * 3) ioctl comes back successful and the data comes back as
		 *    non-zero.  We've found a device.
		 *
		 * 4) Some other error occurs in an ioctl.
		 */

		prg_p->status = PCITOOL_SUCCESS;
		prg_p->offset = 0;
		prg_p->data = 0;
		prg_p->user_version = PCITOOL_VERSION;
		if (((rval = ioctl(fd, PCITOOL_DEVICE_GET_REG, prg_p)) != 0) ||
		    (prg_p->data == 0xffffffff)) {

			/*
			 * Accept errno == EINVAL along with status of
			 * PCITOOL_OUT_OF_RANGE because some systems
			 * don't implement the full range of config space.
			 * Leave the loop quietly in this case.
			 */
			if ((errno == EINVAL) ||
			    (prg_p->status == PCITOOL_OUT_OF_RANGE)) {
				break;
			}

			/*
			 * Exit silently with ENXIO as this means that there are
			 * no devices under the pci root nexus.
			 */
			else if ((errno == ENXIO) &&
			    (prg_p->status == PCITOOL_IO_ERROR)) {
				break;
			}

			/*
			 * Expect errno == EFAULT along with status of
			 * PCITOOL_INVALID_ADDRESS because there won't be
			 * devices at each stop.  Quit on any other error.
			 */
			else if (((errno != EFAULT) ||
			    (prg_p->status != PCITOOL_INVALID_ADDRESS)) &&
			    (prg_p->data != 0xffffffff)) {

				if (!(IS_QUIET(input_args_p->flags))) {
					(void) fprintf(stderr,
					    "Ioctl error: %s\n",
					    strerror(errno));
				}
				break;

			/*
			 * If no function at this location,
			 * just advance to the next function.
			 */
			} else {
				rval = SUCCESS;
			}

		/*
		 * Data came back as 0.
		 * Treat as unresponsive device amd check next device.
		 */
		} else if (prg_p->data == 0) {
			rval = SUCCESS;
		/* Found something. */
		} else {
			config_hdr.dwords[0] = (uint32_t)prg_p->data;

			/* Get the rest of the PCI header. */
			if ((rval = get_config_header(fd, prg_p->bus_no,
			    prg_p->dev_no, prg_p->func_no, &config_hdr)) !=
			    SUCCESS) {
				break;
			}

			if (get_pcie_configuration(fd, prg_p->bus_no,
			    prg_p->dev_no, prg_p->func_no, &pcie_conf) !=
			    SUCCESS) {
				pcie_conf.dwords[0] = 0;
				pcie_conf.dwords[1] = 0;
			}

			/* Print the found information. */
			print_probe_info(&config_hdr, &pcie_conf, prg_p,
			    IS_VERBOSE(input_args_p->flags));

			/*
			 * Special case for the type of Southbridge found on
			 * Ultra-45 and other sun4u fire workstations.
			 */
			if ((config_hdr.dwords[0] == U45_SB_DEVID_VID) &&
			    (config_hdr.dwords[2] == U45_SB_CLASS_RID)) {
				rval = ECANCELED;
				break;
			}

			/*
			 * Accomodate devices which state their
			 * multi-functionality only in their function 0 config
			 * space.  Note multi-functionality throughout probing
			 * of all of this device's functions.
			 */
			if (config_hdr.bytes[PCI_CONF_HEADER] &
			    PCI_HEADER_MULTI) {
				multi_function_device = B_TRUE;
			}
		}
	}

	return (rval);
}


/*
 * Probe a given nexus config space for devices.
 *
 * fd is the file descriptor of the nexus.
 * input_args contains commandline options as specified by the user.
 */
static int
do_probe(int fd, di_node_t di_node, di_prom_handle_t di_phdl,
    pcitool_uiargs_t *input_args_p)
{
	pcitool_reg_t prg;
	int bus;
	int dev;
	int last_bus = PCI_REG_BUS_M >> PCI_REG_BUS_SHIFT;
	int last_dev = PCI_REG_DEV_M >> PCI_REG_DEV_SHIFT;
	int first_bus = 0;
	int first_dev = 0;
	int rval = SUCCESS;

	prg.barnum = 0;	/* Config space. */

	/* Must read in 4-byte quantities. */
	prg.acc_attr = PCITOOL_ACC_ATTR_SIZE_4 + NATIVE_ENDIAN;

	prg.data = 0;

	/* If an explicit bus was specified by the user, go with it. */
	if (input_args_p->flags & BUS_SPEC_FLAG) {
		first_bus = last_bus = input_args_p->bus;

	} else if (input_args_p->flags & PROBERNG_FLAG) {
		/* Otherwise get the bus range from properties. */
		int len;
		uint32_t *rangebuf = NULL;

		len = di_prop_lookup_ints(DDI_DEV_T_ANY, di_node,
		    "bus-range", (int **)&rangebuf);

		/* Try PROM property */
		if (len <= 0) {
			len = di_prom_prop_lookup_ints(di_phdl, di_node,
			    "bus-range", (int **)&rangebuf);
		}

		/* Take full range for default if cannot get property. */
		if (len > 0) {
			first_bus = rangebuf[0];
			last_bus = rangebuf[1];
		}
	}

	/* Take full range for default if not PROBERNG and not BUS_SPEC. */

	if (last_bus == first_bus) {
		if (input_args_p->flags & DEV_SPEC_FLAG) {
			/* Explicit device given.  Not probing a whole bus. */
			(void) puts("");
		} else {
			(void) printf("*********** Probing bus %x "
			    "***********\n\n", first_bus);
		}
	} else {
		(void) printf("*********** Probing buses %x through %x "
		    "***********\n\n", first_bus, last_bus);
	}

	/* Print header. */
	print_probe_info(NULL, NULL, NULL, IS_VERBOSE(input_args_p->flags));

	/* Device number explicitly specified. */
	if (input_args_p->flags & DEV_SPEC_FLAG) {
		first_dev = last_dev = input_args_p->device;
	}

	/*
	 * Loop through all valid bus / dev / func combinations to check for
	 * all devices, with the following exceptions:
	 *
	 * When nothing is found at function 0 of a bus / dev combination, skip
	 * the other functions of that bus / dev combination.
	 *
	 * When a found device's function 0 is probed and it is determined that
	 * it is not a multifunction device, skip probing of that device's
	 * other functions.
	 */
	for (bus = first_bus; ((bus <= last_bus) && (rval == SUCCESS)); bus++) {
		prg.bus_no = bus;

		/* Device number explicitly specified. */
		if (input_args_p->flags & DEV_SPEC_FLAG) {
			first_dev = last_dev = input_args_p->device;
		} else if (supports_ari(fd, bus) == SUCCESS) {
			last_dev = 0;
			first_dev = 0;
		} else {
			last_dev = PCI_REG_DEV_M >> PCI_REG_DEV_SHIFT;
		}

		for (dev = first_dev;
		    ((dev <= last_dev) && (rval == SUCCESS)); dev++) {
			prg.dev_no = dev;
			rval = probe_dev(fd, &prg, input_args_p);
		}

		/*
		 * Ultra-45 southbridge workaround:
		 * ECANCELED tells to skip to the next bus.
		 */
		if (rval == ECANCELED) {
			rval = SUCCESS;
		}
	}

	return (rval);
}

/*
 * This function is called-back from di_walk_minor() when any PROBE is processed
 */
/*ARGSUSED*/
static int
process_nexus_node(di_node_t di_node, di_minor_t minor, void *arg)
{
	int fd;
	char *trunc;
	probe_walk_args_t *walk_args_p = (probe_walk_args_t *)arg;
	char *pathname = walk_args_p->pathname;
	char *nexus_path = di_devfs_minor_path(minor);

	if (nexus_path == NULL) {
		(void) fprintf(stderr, "Error getting nexus path: %s\n",
		    strerror(errno));
		return (DI_WALK_CONTINUE);
	}

	/*
	 * Display this node if pathname not specified (as all nodes are
	 * displayed) or if the current node matches the single specified
	 * pathname. Pathname form: xxx, nexus form: xxx:reg
	 */
	if ((pathname != NULL) &&
	    ((strstr(nexus_path, pathname) != nexus_path) ||
	    (strlen(nexus_path) !=
	    (strlen(pathname) + strlen(PCI_MINOR_REG) + 1)))) {
		di_devfs_path_free(nexus_path);
		return (DI_WALK_CONTINUE);
	}

	if ((fd = open_node(nexus_path, walk_args_p->input_args_p)) >= 0) {

		/* Strip off the suffix at the end of the nexus path. */
		if ((trunc = strstr(nexus_path, PCI_MINOR_REG)) != NULL) {
			trunc--;	/* Get the : just before too. */
			*trunc = '\0';
		}

		/* Show header only if no explicit nexus node name given. */
		(void) puts("");
		if (pathname == NULL) {
			(void) printf("********** Devices in tree under %s "
			    "**********\n", nexus_path);
		}

		/*
		 * Exit silently with ENXIO as this means that there are
		 * no devices under the pci root nexus.
		 */
		if ((do_probe(fd, di_node, walk_args_p->di_phdl,
		    walk_args_p->input_args_p) != SUCCESS) &&
		    (errno != ENXIO)) {
			(void) fprintf(stderr, "Error probing node %s: %s\n",
			    nexus_path, strerror(errno));
		}

		(void) close(fd);
	}
	di_devfs_path_free(nexus_path);

	/*
	 * If node was explicitly specified, it has just been displayed
	 * and no more looping is required.
	 * Otherwise, keep looping for more nodes.
	 */
	return ((pathname == NULL) ? DI_WALK_CONTINUE : DI_WALK_TERMINATE);
}


/*
 * Start of probe.  If pathname is NULL, search all devices.
 *
 * di_walk_minor() walks all DDI_NT_REGACC (PCItool register access) nodes
 * and calls process_nexus_node on them.  process_nexus_node will then check
 * the pathname for a match, unless it is NULL which works like a wildcard.
 */
static int
do_probe_walk(pcitool_uiargs_t *input_args_p, char *pathname)
{
	di_node_t di_node;
	di_prom_handle_t di_phdl = DI_PROM_HANDLE_NIL;
	probe_walk_args_t walk_args;

	int rval = SUCCESS;

	if ((di_node = di_init("/", DINFOCPYALL)) == DI_NODE_NIL) {
		(void) fprintf(stderr, "di_init() failed: %s\n",
		    strerror(errno));
		rval = errno;

	} else if ((input_args_p->flags & PROBERNG_FLAG) &&
	    ((di_phdl = di_prom_init()) == DI_PROM_HANDLE_NIL)) {
		(void) fprintf(stderr, "di_prom_init failed: %s\n",
		    strerror(errno));
		rval = errno;

	} else {
		walk_args.input_args_p = input_args_p;
		walk_args.di_phdl = di_phdl;
		walk_args.pathname = pathname;
		(void) di_walk_minor(di_node, DDI_NT_REGACC, 0,
		    &walk_args, process_nexus_node);
	}

	if (di_phdl != DI_PROM_HANDLE_NIL) {
		di_prom_fini(di_phdl);
	}

	if (di_node != DI_NODE_NIL) {
		di_fini(di_node);
	}

	return (rval);
}


/* **************** Byte dump specific **************** */

static void
print_bytedump_header(boolean_t do_chardump)
{
	static char header1[] = {"                    "
	    "0F 0E 0D 0C 0B 0A 09 08 07 06 05 04 03 02 01 00"};
	static char header2[] = {"                    "
	    "-----------------------------------------------"};
	static char cheader1[] = {" 0123456789ABCDEF"};
	static char cheader2[] = {" ----------------"};

	(void) puts("");
	(void) printf(header1);
	if (do_chardump) {
		(void) printf(cheader1);
	}
	(void) puts("");
	(void) printf(header2);
	if (do_chardump) {
		(void) printf(cheader2);
	}
}


/* Number of bytes per line in a dump. */
#define	DUMP_BUF_SIZE		16
#define	LINES_BTWN_HEADER	16

/*
 * Retrieve several bytes over several reads, and print a formatted byte-dump
 *
 * fd is the nexus by which device is accessed.
 * prg provided has bus, dev, func, bank, initial offset already specified,
 * as well as size and endian attributes.
 *
 * No checking is made that this is a read operation, although only read
 * operations are allowed.
 */
static int
bytedump_get(int fd, int cmd, pcitool_reg_t *prg_p,
    pcitool_uiargs_t *input_args_p)
{
	typedef union {
		uint8_t	bytes[DUMP_BUF_SIZE];
		uint16_t shorts[DUMP_BUF_SIZE / sizeof (uint16_t)];
		uint32_t dwords[DUMP_BUF_SIZE / sizeof (uint32_t)];
		uint64_t longs[DUMP_BUF_SIZE / sizeof (uint64_t)];
	} buffer_t;

	/*
	 * Local copy of pcitool_reg_t, since offset and phys_addrs are
	 * modified.
	 */
	pcitool_reg_t local_prg;

	/* Loop parameters. */
	uint32_t dump_end = prg_p->offset + input_args_p->bytedump_amt;
	uint32_t dump_curr = prg_p->offset;

	int read_size = input_args_p->size;

	/* How many stores to the buffer before it is full. */
	int wrap_size = DUMP_BUF_SIZE / read_size;

	/* Address prints at the beginning of each line. */
	uint64_t print_addr = 0;

	/* Skip this num bytes at the beginning of the first dump. */
	int skip_begin;

	/* Skip this num bytes at the end of the last dump. */
	int skip_end = 0;

	/* skip_begin and skip_end are needed twice. */
	int skip_begin2;
	int skip_end2;

	/* Number of lines between headers */
	int lines_since_header = 0;

	boolean_t do_chardump = input_args_p->flags & CHARDUMP_FLAG;
	boolean_t continue_on_errs = input_args_p->flags & ERRCONT_FLAG;

	int rval = SUCCESS;	/* Return status. */

	int next;
	int i;

	buffer_t buffer;
	uint16_t error_mask = 0; /* 1 bit/byte in buf.  Err when set */

	bzero(buffer.bytes, sizeof (uint8_t) * DUMP_BUF_SIZE);

	local_prg = *prg_p;	/* Make local copy. */

	/*
	 * Flip the bytes to proper order if reading on a big endian machine.
	 * Do this by reading big as little and vs.
	 */
#if (NATIVE_ENDIAN == PCITOOL_ACC_ATTR_ENDN_BIG)
		local_prg.acc_attr =
		    (PCITOOL_ACC_IS_BIG_ENDIAN(local_prg.acc_attr) ?
		    (local_prg.acc_attr & ~PCITOOL_ACC_ATTR_ENDN_BIG) :
		    (local_prg.acc_attr | PCITOOL_ACC_ATTR_ENDN_BIG));
#endif

	/*
	 * Get offset into buffer for first store.  Assumes the buffer size is
	 * a multiple of the read size.  "next" is the next buffer index to do
	 * a store.
	 */
	skip_begin = local_prg.offset % DUMP_BUF_SIZE;
	next = skip_begin / read_size;

	print_bytedump_header(do_chardump);

	while (dump_curr < dump_end) {

		/* For reading from the next location. */
		local_prg.offset = dump_curr;

		/* Access the device.  Abort on error. */
		if (((rval = ioctl(fd, cmd, &local_prg)) != SUCCESS) &&
		    (!(continue_on_errs))) {
			if (!(IS_QUIET(input_args_p->flags))) {
				(void) fprintf(stderr,
				    "Ioctl failed:\n errno: %s\n status: %s\n",
				    strerror(errno),
				    strstatus(local_prg.status));
			}
			break;
		}

		/*
		 * Initialize print_addr first time through, in case printing
		 * is starting in the middle of the buffer.  Also reinitialize
		 * when wrap.
		 */
		if (print_addr == 0) {

			/*
			 * X86 config space doesn't return phys addr.
			 * Use offset instead in this case.
			 */
			if (local_prg.phys_addr == 0) {	/* No phys addr ret */
				print_addr = local_prg.offset -
				    (local_prg.offset % DUMP_BUF_SIZE);
			} else {
				print_addr = local_prg.phys_addr -
				    (local_prg.phys_addr % DUMP_BUF_SIZE);
			}
		}

		/*
		 * Read error occurred.
		 * Shift the right number of error bits ((1 << read_size) - 1)
		 * into the right place (next * read_size)
		 */
		if (rval != SUCCESS) {	/* Read error occurred */
			error_mask |=
			    ((1 << read_size) - 1) << (next * read_size);

		} else {	/* Save data to the buffer. */

			switch (read_size) {
			case 1:
				buffer.bytes[next] = (uint8_t)local_prg.data;
				break;
			case 2:
				buffer.shorts[next] = (uint16_t)local_prg.data;
				break;
			case 4:
				buffer.dwords[next] = (uint32_t)local_prg.data;
				break;
			case 8:
				buffer.longs[next] = (uint64_t)local_prg.data;
				break;
			default:
				rval = EIO;
				break;
			}
		}
		next++;

		/* Increment index for next store, and wrap. */
		next %= wrap_size;
		dump_curr += read_size;

		/* Zero out the remainder of the buffer if done. */
		if (dump_curr >= dump_end) {
			if (next != 0) {
				bzero(&buffer.bytes[next * read_size],
				    (wrap_size - next) * read_size);
				skip_end = (wrap_size - next) * read_size;
				next = 0;	/* For printing below. */
			}
		}

		/* Dump the buffer if full or if done. */
		if (next == 0) {

			skip_begin2 = skip_begin;
			skip_end2 = skip_end;

			(void) printf("\n0x%16.16" PRIx64 ":", print_addr);
			for (i = DUMP_BUF_SIZE - 1; i >= 0; i--) {
				if (skip_end) {
					skip_end--;
					(void) printf(" --");
				} else if (skip_begin > i) {
					skip_begin--;
					(void) printf(" --");
				} else if (error_mask & (1 << i)) {
					(void) printf(" XX");
				} else {
					(void) printf(" %2.2x",
					    buffer.bytes[i]);
				}
			}

			if (do_chardump) {
				(void) putchar(' ');
				for (i = 0; i < DUMP_BUF_SIZE; i++) {
					if (skip_begin2) {
						skip_begin2--;
						(void) printf("-");
					} else if (
					    (DUMP_BUF_SIZE - skip_end2) <= i) {
						(void) printf("-");
					} else if (error_mask & (1 << i)) {
						(void) putchar('X');
					} else if (isprint(buffer.bytes[i])) {
						(void) putchar(buffer.bytes[i]);
					} else {
						(void) putchar('@');
					}
				}
			}

			if ((++lines_since_header == LINES_BTWN_HEADER) &&
			    (dump_curr < dump_end)) {
				lines_since_header = 0;
				(void) puts("");
				print_bytedump_header(do_chardump);
			}

			print_addr += DUMP_BUF_SIZE;
			error_mask = 0;
		}
	}
	(void) printf("\n");

	return (rval);
}


/* ************** Device and nexus access commands ************** */

/*
 * Helper function to set access attributes.  Assumes size is valid.
 */
static uint32_t
set_acc_attr(pcitool_uiargs_t *input_args_p)
{
	uint32_t access_attrs;

	switch (input_args_p->size) {
	case 1:
		access_attrs = PCITOOL_ACC_ATTR_SIZE_1;
		break;
	case 2:
		access_attrs = PCITOOL_ACC_ATTR_SIZE_2;
		break;
	case 4:
		access_attrs = PCITOOL_ACC_ATTR_SIZE_4;
		break;
	case 8:
		access_attrs = PCITOOL_ACC_ATTR_SIZE_8;
		break;
	}

	if (input_args_p->big_endian) {
		access_attrs |= PCITOOL_ACC_ATTR_ENDN_BIG;
	}

	return (access_attrs);
}

static int
do_pcie_operations(int fd, int cmd, pcitool_reg_t *prg_p,
    pcitool_uiargs_t *input_args_p)
{
	pci_conf_hdr_t	pcie_conf;
	uint8_t		old_cto, new_cto, old_speed, new_speed;
	int		rval;

	old_cto = new_cto = 0xff;
	old_speed = new_speed = 0;

	/* Get PCIe config values before performing opertions */
	if (get_pcie_configuration(fd, prg_p->bus_no, prg_p->dev_no,
	    prg_p->func_no, &pcie_conf) == SUCCESS) {
		old_cto = pcie_conf.bytes[PCIE_CTO_OFF];
		old_speed = pcie_conf.bytes[PCIE_LS_OFF];
	}
	/* Do the access.  Return on error. */
	if ((rval = ioctl(fd, cmd, prg_p)) != SUCCESS) {
		if (!(IS_QUIET(input_args_p->flags))) {
			(void) fprintf(stderr,
			    "errno: %s\nstatus: %s\n",
			    strerror(errno), strstatus(prg_p->status));
		}
	}

	/* Get PCIe config values after performing opertions */
	if (get_pcie_configuration(fd, prg_p->bus_no, prg_p->dev_no,
	    prg_p->func_no, &pcie_conf) == SUCCESS) {
		new_cto = pcie_conf.bytes[PCIE_CTO_OFF];
		new_speed = pcie_conf.bytes[PCIE_LS_OFF];
	}

	/* Set Completion Timeout Range */
	if (cmd == PCITOOL_DEVICE_SET_CTO) {
		(void) printf("CTO: 0x%x (%s) => 0x%x (%s)\n",
		    old_cto,
		    value_to_str(old_cto, pcitool_cto_str),
		    new_cto,
		    value_to_str(new_cto, pcitool_cto_str));
	/* Perform Link Retrain or Equalization */
	} else if (cmd == PCITOOL_DEVICE_LINK_RETRAIN ||
	    cmd == PCITOOL_DEVICE_LINK_EQUALIZATION) {
		(void) printf("Link Speed: 0x%x (%s%x) => 0x%x (%s%x)\n",
		    old_speed, "Gen", old_speed,
		    new_speed, "Gen", new_speed);
	}

	return (rval);

}

static int
do_single_access(int fd, int cmd, pcitool_reg_t *prg_p,
    pcitool_uiargs_t *input_args_p)
{
	boolean_t is_write = B_FALSE;
	int rval;

	switch (cmd) {
		case PCITOOL_DEVICE_LINK_RETRAIN:
		case PCITOOL_DEVICE_LINK_EQUALIZATION:
		case PCITOOL_DEVICE_SET_CTO:
			return (do_pcie_operations(fd, cmd,
			    prg_p, input_args_p));
		case PCITOOL_NEXUS_SET_REG:
		case PCITOOL_DEVICE_SET_REG:
			is_write = B_TRUE;
			break;
		default:
			break;
	}

	/* Do the access.  Return on error. */
	if ((rval = ioctl(fd, cmd, prg_p)) != SUCCESS) {
		if (!(IS_QUIET(input_args_p->flags))) {
			(void) fprintf(stderr,
			    "%s ioctl failed:\n errno: %s\n status: %s\n",
			    is_write ? "write" : "read",
			    strerror(errno), strstatus(prg_p->status));
		}

		return (rval);
	}

	/* Print on all verbose requests. */
	if (IS_VERBOSE(input_args_p->flags)) {

		/*
		 * Return offset on platforms which return phys_addr == 0
		 * for config space.
		 */
		if (prg_p->phys_addr == 0)
			prg_p->phys_addr = input_args_p->offset;

		(void) printf("Addr:0x%" PRIx64 ", %d-byte %s endian "
		    "register value: 0x%" PRIx64 "\n",
		    prg_p->phys_addr, input_args_p->size,
		    (input_args_p->big_endian ? "big" : "little"), prg_p->data);

	/* Non-verbose, read requests. */
	} else if (!(is_write)) {
		(void) printf("0x%" PRIx64 "\n", prg_p->data);
	}

	return (rval);
}


/*
 * fd is the file descriptor of the nexus to access, either to get its
 * registers or to access a device through that nexus.
 *
 * input args are commandline arguments specified by the user.
 */
static int
do_device_or_nexus(int fd, pcitool_uiargs_t *input_args_p)
{
	pcitool_reg_t prg;	/* Request details given to the driver. */
	uint32_t write_cmd = 0;	/* Command given to the driver. */
	uint32_t read_cmd = 0;	/* Command given to the driver. */
	uint32_t ioctl_cmd = 0;
	int rval = SUCCESS;	/* Return status. */

	if (input_args_p->flags & WRITE_FLAG) {
		prg.data = input_args_p->write_value;
		if (input_args_p->flags & NEXUS_FLAG) {
			write_cmd = PCITOOL_NEXUS_SET_REG;
		} else {
			write_cmd = PCITOOL_DEVICE_SET_REG;
		}
	}
	if (input_args_p->flags & READ_FLAG) {
		if (input_args_p->flags & NEXUS_FLAG) {
			read_cmd = PCITOOL_NEXUS_GET_REG;
		} else {
			read_cmd = PCITOOL_DEVICE_GET_REG;
		}
	}
	if (input_args_p->flags & RETRAIN_FLAG) {
		prg.data = input_args_p->link_speed;
		ioctl_cmd = PCITOOL_DEVICE_LINK_RETRAIN;
	}

	if (input_args_p->flags & EQUA_FLAG) {
		prg.data = input_args_p->link_speed;
		ioctl_cmd = PCITOOL_DEVICE_LINK_EQUALIZATION;
	}

	/* handle completion timeout */
	if (input_args_p->flags & CTO_FLAG) {
		prg.data = input_args_p->cto;
		ioctl_cmd = PCITOOL_DEVICE_SET_CTO;
	}

	/* Finish initializing access details for driver. */

	/*
	 * For nexus, barnum is the exact bank number, unless it is 0xFF, which
	 * indicates that it is inactive and a base_address should be read from
	 * the input_args instead.
	 *
	 * For devices, barnum is the offset to the desired BAR, or 0 for
	 * config space.
	 */
	if ((input_args_p->flags & (BASE_SPEC_FLAG | NEXUS_FLAG)) ==
	    (BASE_SPEC_FLAG | NEXUS_FLAG)) {
		prg.barnum = PCITOOL_BASE;
		prg.phys_addr = input_args_p->base_address;
	} else
		prg.barnum = input_args_p->bank;

	prg.offset = input_args_p->offset;
	prg.acc_attr = set_acc_attr(input_args_p);
	prg.bus_no = input_args_p->bus;
	prg.dev_no = input_args_p->device;
	prg.func_no = input_args_p->function;
	prg.user_version = PCITOOL_VERSION;

	do {
		/* Do a bytedump if desired, or else do single ioctl access. */
		if (input_args_p->flags & BYTEDUMP_FLAG) {

			if (IS_VERBOSE(input_args_p->flags)) {
				(void) printf(
				    "\nDoing %d-byte %s endian reads:",
				    input_args_p->size,
				    input_args_p->big_endian ?
				    "big" : "little");
			}
			rval = bytedump_get(fd, read_cmd, &prg, input_args_p);

		} else {

			/* Single write and/or read. */
			if (write_cmd != 0) {
				rval = do_single_access(
				    fd, write_cmd, &prg, input_args_p);
			}

			if ((rval == SUCCESS) && (read_cmd != 0)) {
				rval = do_single_access(
				    fd, read_cmd, &prg, input_args_p);
			}

			/* other ioctls */
			if (ioctl_cmd != 0) {
				rval = do_single_access(
				    fd, ioctl_cmd, &prg, input_args_p);
			}
		}
	} while ((IS_LOOP(input_args_p->flags)) && (rval == SUCCESS) &&
	    (keep_looping));

	return (rval != SUCCESS ? errno : SUCCESS);
}

/* *************** Interrupt routing ************** */

static void intr_print_banner(int ctlr_type, int flags)
{
	if (ctlr_type == PCITOOL_CTLR_TYPE_RISC) {
		if (flags & DEC_FLAG)
			(void) printf("CPU  INO  MSG  Type   PIL Mondo     "
			    "Share Driver                Path (Display "
			    "Mode: Decimal)\n");
		else
			(void) printf("CPU  INO  MSG  Type   PIL Mondo     "
			    "Share Driver                 Path (Display "
			    "Mode: Hexadecimal)\n");
	} else {
		if (flags & DEC_FLAG)
			(void) printf("CPU  Vect IRQ  Type   PIL APIC/INT# "
			    "Share Driver                Path (Display "
			    "Mode: Decimal)\n");
		else
			(void) printf("CPU  Vect IRQ  Type   PIL APIC/INT# "
			    "Share Driver                Path (Display "
			    "Mode: Hexadecimal)\n");
	}
}

static void
copy_intr_dev(pcitool_intr_dev_t *dst, pcitool_intr_dev_t *src)
{
	(void) strlcpy(dst->driver_name, src->driver_name, MAXMODCONFNAME);
	(void) strlcpy(dst->path, src->path, MAXPATHLEN);
	(void) strlcpy(dst->type, src->type, MODMAXNAMELEN);
	dst->dev_inst = src->dev_inst;
	dst->pri = src->pri;
	dst->inum = src->inum;
	dst->msg = src->msg;
}

/*
 * Sort device array in iget structure by driver name when one interrupt is
 * shared by many devices.
 */
static void dev_sort(pcitool_intr_get_t *iget_p)
{
	int i, j, mix;
	pcitool_intr_dev_t *tmp_dev;

	tmp_dev = malloc(sizeof (pcitool_intr_dev_t));

	for (i = 0; i < iget_p->num_devs; i++) {
		mix = i;
		for (j = i + 1; j < iget_p->num_devs; j++) {
			if (strcmp(iget_p->dev[j].driver_name,
			    iget_p->dev[mix].driver_name) < 0)
				mix = j;
		}
		copy_intr_dev(tmp_dev, &iget_p->dev[mix]);
		copy_intr_dev(&iget_p->dev[mix], &iget_p->dev[i]);
		copy_intr_dev(&iget_p->dev[i], tmp_dev);
	}
	free(tmp_dev);
}

/*
 * Display interrupt information.
 * iget is filled in with the info to display
 */
static void
print_intr_info(pcitool_intr_get_t *iget_p, int type)
{
	int i;
	int intx;
	char cpu[MODMAXNAMELEN];
	char vector[MODMAXNAMELEN];
	char irq[MODMAXNAMELEN];
	char pri[MODMAXNAMELEN];
	char share[MODMAXNAMELEN];
	char apic[MODMAXNAMELEN];
	char ints[MODMAXNAMELEN];
	char dev[MAXMODCONFNAME];

	/* It's for -h/-d switch */
	const char *fmt = (iget_p->flags & DEC_FLAG) ? "%d" : "%x";

	SNPRINTF(vector, fmt, iget_p->ino);
	SNPRINTF(cpu, fmt, iget_p->cpu_id);
	SNPRINTF(share, fmt, iget_p->share);

	if (iget_p->num_devs > 1)
		(void) dev_sort(iget_p);

	for (i = 0; i < iget_p->num_devs; i++) {

		SNPRINTF(pri, fmt, iget_p->dev[i].pri);

		/*
		 * IRQ#/MSG# output format control:
		 * x86 platform, MSI/MSI-X doesn't output IRQ#,
		 *   Fixed output IRQ#.
		 * sparc platform, MSI/MSI-X output MSG#,
		 *   Fixed doesn't output MSG#.
		 */
		if (type != PCITOOL_CTLR_TYPE_RISC) {
			if (strcmp(iget_p->dev[i].type, "MSI") == 0 ||
			    strcmp(iget_p->dev[i].type, "MSI-X") == 0) {
				SNPRINTF(irq, "%s", "-");
				SNPRINTF(apic, "%s", "-");
				SNPRINTF(ints, "%s", "-");
			}
			if (strcmp(iget_p->dev[i].type, "Fixed") == 0) {

				intx = (iget_p->irq > 23) ?
				    iget_p->irq % (24 * iget_p->sysino) :
				    iget_p->irq;

				SNPRINTF(irq, fmt, iget_p->irq);
				SNPRINTF(apic, fmt, iget_p->sysino);
				SNPRINTF(ints, fmt, intx);
			}

			(void) strcat(apic, "/");
			(void) strcat(apic, ints);
		} else {
			if (strcmp(iget_p->dev[i].type, "Fixed") == 0) {
				SNPRINTF(irq, "%s", "-");
			} else {
				SNPRINTF(irq, fmt, iget_p->dev[i].msg);
			}
			SNPRINTF(apic, fmt, iget_p->sysino);
		}

		/* Intr with device output format control */
		if (iget_p->dev[i].path[0] != '\0') {
			(void) snprintf(dev, sizeof (dev), "%s#%d[%d]\t\t%s",
			    iget_p->dev[i].driver_name,
			    iget_p->dev[i].dev_inst,
			    iget_p->dev[i].inum,
			    iget_p->dev[i].path);
		/* ISR without device output format control */
		} else {
			(void) snprintf(dev, sizeof (dev), "%-15s\t%s",
			    iget_p->dev[i].driver_name, "--");

			/* IPI output format control */
			if (strcmp(iget_p->dev[i].type, "Fixed") != 0) {
				SNPRINTF(cpu, "%s", "all");
				SNPRINTF(irq, "%s", "-");
				SNPRINTF(apic, "%s", "-");
				SNPRINTF(ints, "%s", "-");
				(void) strcat(apic, "/");
				(void) strcat(apic, ints);
			}
		}

		(void) printf("%-5s%-5s%-5s%-7s%-4s%-10s%-6s%s\n",
		    cpu, vector, irq, iget_p->dev[i].type, pri,
		    apic, share, dev);
	}
}

/*
 * Interrupt command support.
 *
 * fd is the file descriptor of the nexus being probed.
 * input_args are commandline options entered by the user.
 */
static pcitool_intr_get_t *
get_single_intr(int fd, pcitool_uiargs_t *input_args_p)
{
	pcitool_intr_get_t *iget_p;
	const char	*str_type = NULL;
	uint32_t	intr;

	/*
	 * Start with a struct with space for info of interrupt
	 * to be returned.
	 */
	iget_p = malloc(sizeof (pcitool_intr_get_t));
	bzero(iget_p,  sizeof (pcitool_intr_get_t));

	iget_p->num_devs_ret = INIT_NUM_DEVS;
	iget_p->user_version = PCITOOL_VERSION;
	iget_p->cpu_id = input_args_p->old_cpu;
	iget_p->ino =  input_args_p->intr_ino;
	iget_p->msi = input_args_p->intr_msi;
	iget_p->flags = (input_args_p->flags & MSI_ALL_FLAG ||
	    input_args_p->flags & MSI_SPEC_FLAG) ?
	    PCITOOL_INTR_FLAG_GET_MSI : 0;

	if (input_args_p->flags & MSI_SPEC_FLAG) {
		intr = input_args_p->intr_msi;
		str_type = "msi";
	} else {
		intr = input_args_p->intr_ino;
		str_type = "ino";
	}

	/*
	 * Check if interrupts are active on this ino/msi. Get as much
	 * device info as there is room for at the moment. If there
	 * is not enough room for all devices, will call again with a
	 * larger buffer.
	 */
	if (ioctl(fd, PCITOOL_DEVICE_GET_INTR, iget_p) != 0) {
		/*
		 * Let EIO errors silently slip through, as
		 * some inos may not be viewable by design.
		 * We don't want to stop or print an error for these.
		 */
		if (errno == EIO) {
			return (iget_p);
		}

		if (!(IS_QUIET(input_args_p->flags))) {
			(void) fprintf(stderr, "Ioctl to get %s 0x%x "
			    "info failed: %s\n", str_type, intr,
			    strerror(errno));

			if (errno != EFAULT) {
				(void) fprintf(stderr, "Pcitool status: %s\n",
				    strstatus(iget_p->status));
			}
		}
		free(iget_p);
		return (NULL);
	}

	/* Need more room to return additional device info. */
	if (iget_p->num_devs_ret < iget_p->num_devs) {
		iget_p =
		    realloc(iget_p, PCITOOL_IGET_SIZE(iget_p->num_devs));
		iget_p->num_devs_ret = iget_p->num_devs;

		if (ioctl(fd, PCITOOL_DEVICE_GET_INTR, iget_p) != 0) {
			if (!(IS_QUIET(input_args_p->flags))) {
				(void) fprintf(stderr, "Ioctl to get %s 0x%x"
				    "device info failed: %s\n", str_type,
				    intr, strerror(errno));
				if (errno != EFAULT) {
					(void) fprintf(stderr,
					    "Pcitool status: %s\n",
					    strstatus(iget_p->status));
				}
			}
			free(iget_p);
			return (NULL);
		}
	}

	iget_p->flags |= input_args_p->flags;

	return (iget_p);
}


void
insert_intr_list(pcitool_intr_node_t *head, pcitool_intr_node_t *node, int flag)
{
	pcitool_intr_node_t *p, *prep;

	/* find where it goes in list */
	prep = NULL;
	for (p = head->link; p != NULL; p = p->link) {
		if (flag == SORT_CPU_FLAG &&
		    node->node->cpu_id < p->node->cpu_id)
			break;
		if (flag == SORT_DRV_FLAG &&
		    strcmp(node->node->dev[0].driver_name,
		    p->node->dev[0].driver_name) < 0)
			break;
		prep = p;
	}

	if (prep == NULL) {
		head->link = node;
		node->link = p;
	} else if (p == NULL) {
		prep->link = node;
		node->link = NULL;
	} else {
		prep->link = node;
		node->link = p;
	}
}

static void
walk_all_intr(int fd, pcitool_uiargs_t *input_args_p, pcitool_intr_node_t **end,
    int cpuid, int begin, int num)
{
	int ino, i;
	pcitool_intr_get_t *iget_p, *tmp;
	pcitool_intr_node_t *p;

	input_args_p->old_cpu = cpuid;
	for (ino = begin; ino < num;  ino++) {
		input_args_p->intr_ino = ino;
		input_args_p->intr_msi = ino;
		if ((iget_p = get_single_intr(fd, input_args_p)) == NULL ||
		    iget_p->num_devs == 0)
			continue;

		/* create list sorted by cpu */
		if (input_args_p->flags & SORT_CPU_FLAG) {
			p = malloc(sizeof (pcitool_intr_node_t));
			p->node = iget_p;
			p->link = NULL;
			insert_intr_list(*end, p, SORT_CPU_FLAG);
		/* create list sorted by driver name */
		} else if (input_args_p->flags & SORT_DRV_FLAG) {
			for (i = 0; i < iget_p->num_devs; i++) {
				tmp = malloc(
				    sizeof (pcitool_intr_get_t));
				(void) memcpy(tmp, iget_p,
				    sizeof (pcitool_intr_get_t));
				(void) memcpy((char *)tmp +
				    PCITOOL_IGET_SIZE(INIT_NUM_DEVS),
				    (char *)iget_p +
				    PCITOOL_IGET_SIZE(i),
				    sizeof (pcitool_intr_dev_t));
				tmp->num_devs = 1;
				p = malloc(sizeof (pcitool_intr_node_t));
				p->node = tmp;
				p->link = NULL;
				insert_intr_list(*end, p,
				    SORT_DRV_FLAG);
			}
			free(iget_p);
		/* create list sorted by default order */
		} else {
			p = malloc(sizeof (pcitool_intr_node_t));
			p->node = iget_p;
			p->link = NULL;
			(*end)->link = p;
			(*end) = p;
		}
	}
}

/* Search through all interrupts. Display info on enabled ones. */
static void
get_all_intr(int fd, pcitool_uiargs_t *input_args_p,
    pcitool_intr_info_t *intr_info)
{
	int	cpu_id;
	pcitool_intr_node_t *head, *end, *p, *tmp;

	head = malloc(sizeof (pcitool_intr_node_t));
	head->link = NULL;
	end = head;

	if (intr_info->ctlr_type == PCITOOL_CTLR_TYPE_APIX) {
		/*
		 * If platform is APIX, walk through 0x0-0xef vectors
		 * in all cpus firstly.
		 */
		for (cpu_id = 0; cpu_id < intr_info->num_cpu;
		    cpu_id++) {
			walk_all_intr(fd, input_args_p, &end, cpu_id,
			    0, intr_info->num_intr);
		}
		/*
		 * Then, walk all IPI info from 0xf0 to 0xff
		 * without regard to cpu.
		 */
		walk_all_intr(fd, input_args_p, &end, intr_info->num_cpu - 1,
		    intr_info->num_intr, APIC_MAX_VECTOR);
	} else {
		/*
		 * Other platform, walk through all vectors
		 * without regard to cpu.
		 */
		walk_all_intr(fd, input_args_p, &end, 0, 0,
		    intr_info->num_intr);
	}
	p = head->link;
	while (p != NULL) {
		print_intr_info(p->node, intr_info->ctlr_type);
		tmp = p;
		p = p->link;
		free(tmp->node);
		free(tmp);
	}
}

static int
get_interrupts(int fd, pcitool_uiargs_t *input_args_p)
{
	int rval = SUCCESS;	/* Return status. */
	pcitool_intr_get_t *iget_p;
	pcitool_intr_info_t intr_info;

	intr_info.flags = (input_args_p->flags & MSI_ALL_FLAG) ?
	    PCITOOL_INTR_FLAG_GET_MSI : 0;

	if (ioctl(fd, PCITOOL_SYSTEM_INTR_INFO, &intr_info) != 0) {
		if (!(IS_QUIET(input_args_p->flags))) {
			(void) fprintf(stderr,
			    "intr info ioctl failed: %s\n",
			    strerror(errno));
		}
		return (errno);
	}

	(void) intr_print_banner(intr_info.ctlr_type, input_args_p->flags);

	/* Return single INO or MSI. */
	if (input_args_p->flags & MSI_SPEC_FLAG ||
	    input_args_p->flags & INO_SPEC_FLAG) {
		if ((iget_p = get_single_intr(fd, input_args_p)) != NULL) {
			switch (intr_info.ctlr_type) {
			case PCITOOL_CTLR_TYPE_APIX:
			case PCITOOL_CTLR_TYPE_PCPLUSMP:
				if (iget_p->cpu_id != input_args_p->old_cpu)
					return (EINVAL);
				break;
			/*FALLTHROUGH*/
			}

			print_intr_info(iget_p, intr_info.ctlr_type);
			free(iget_p);
		}
		rval = errno;
	/* Return all INOs or MSIs. */
	} else if (input_args_p->flags & INO_ALL_FLAG ||
	    input_args_p->flags & MSI_ALL_FLAG) {
		get_all_intr(fd, input_args_p, &intr_info);
	}
	return (rval);
}


static int
get_interrupt_ctlr(int fd, pcitool_uiargs_t *input_args_p)
{
	pcitool_intr_info_t intr_info;
	char *ctlr_type = NULL;
	int rval = SUCCESS;

	intr_info.flags = 0;
	if (ioctl(fd, PCITOOL_SYSTEM_INTR_INFO, &intr_info) != 0) {
		if (!(IS_QUIET(input_args_p->flags))) {
			(void) perror("Ioctl to get intr ctlr info failed");
		}
		rval = errno;

	} else {
		(void) fputs("Controller type: ", stdout);
		switch (intr_info.ctlr_type) {
		case PCITOOL_CTLR_TYPE_RISC:
			ctlr_type = "RISC";
			break;
		case PCITOOL_CTLR_TYPE_UPPC:
			ctlr_type = "UPPC";
			break;
		case PCITOOL_CTLR_TYPE_PCPLUSMP:
			ctlr_type = "PCPLUSMP";
			break;
		case PCITOOL_CTLR_TYPE_APIX:
			ctlr_type = "APIX";
			break;

		default:
			break;
		}

		if (ctlr_type == NULL) {
			(void) printf("Unknown or new (%d)",
			    intr_info.ctlr_type);
		} else {
			(void) fputs(ctlr_type, stdout);
		}

#ifdef __x86
		if (intr_info.ctlr_type == PCITOOL_CTLR_TYPE_PCPLUSMP)
			(void) printf(", IO APIC version: 0x%x, "
			    "local APIC version: 0x%x\n",
			    PSMAT_IO_APIC_VER(intr_info.ctlr_version),
			    PSMAT_LOCAL_APIC_VER(intr_info.ctlr_version));
		else
#endif /* __x86 */
			(void) printf(", version: %2.2x.%2.2x.%2.2x.%2.2x\n",
			    ((intr_info.ctlr_version >> 24) & 0xff),
			    ((intr_info.ctlr_version >> 16) & 0xff),
			    ((intr_info.ctlr_version >> 8) & 0xff),
			    (intr_info.ctlr_version & 0xff));
	}

	return (rval);
}

/*
 *
 * fd is the file descriptor of the nexus being changed.
 * input_args are commandline options entered by the user.
 */
static int
set_interrupts(int fd, pcitool_uiargs_t *input_args_p)
{
	pcitool_intr_info_t	intr_info;
	pcitool_intr_get_t	*iget_p;
	pcitool_intr_set_t	iset;
	const char		*str_type = NULL;
	uint32_t		intr;
	int			rval = SUCCESS;	/* Return status. */

	/* Load interrupt number and cpu from commandline. */
	if (input_args_p->flags & MSI_SPEC_FLAG) {
		iset.msi = intr = input_args_p->intr_msi;
		iset.flags = PCITOOL_INTR_FLAG_SET_MSI;
		str_type = "msi";
	} else {
		iset.ino = intr = input_args_p->intr_ino;
		iset.flags = 0;
		str_type = "ino";
	}

	iset.cpu_id = input_args_p->intr_cpu;
	iset.old_cpu = input_args_p->old_cpu;
	iset.user_version = PCITOOL_VERSION;
	iset.flags |= (input_args_p->flags & SETGRP_FLAG) ?
	    PCITOOL_INTR_FLAG_SET_GROUP : 0;

	intr_info.flags = 0;

	if (ioctl(fd, PCITOOL_SYSTEM_INTR_INFO, &intr_info) != 0) {
		if (!(IS_QUIET(input_args_p->flags))) {
			(void) fprintf(stderr,
			    "intr info ioctl failed: %s\n",
			    strerror(errno));
		}
		return (errno);
	}

	if ((iget_p = get_single_intr(fd, input_args_p)) != NULL) {
		switch (intr_info.ctlr_type) {
		case PCITOOL_CTLR_TYPE_APIX:
		case PCITOOL_CTLR_TYPE_PCPLUSMP:
			if (iget_p->cpu_id != input_args_p->old_cpu)
				return (EINVAL);
			break;
		/*FALLTHROUGH*/
		}
	}

	/* Do the deed. */
	if (ioctl(fd, PCITOOL_DEVICE_SET_INTR, &iset) != 0) {
		if (!(IS_QUIET(input_args_p->flags))) {
			(void) fprintf(stderr,
			    "Ioctl to set %s 0x%x failed: %s\n",
			    str_type, intr, strerror(errno));
			(void) fprintf(stderr, "pcitool status: %s\n",
			    strstatus(iset.status));
		}
		rval = errno;
	} else {
		if (input_args_p->flags & SETGRP_FLAG) {
			if (iset.flags == PCITOOL_INTR_FLAG_SET_MSI)
				(void) printf("0x%x,0x%x => 0x%x,0x%x\n",
				    iset.cpu_id,
				    (input_args_p->intr_msi) & 0xff,
				    input_args_p->intr_cpu, iset.msi);
			else
				(void) printf("0x%x,0x%x => 0x%x,0x%x\n",
				    iset.cpu_id,
				    (input_args_p->intr_ino) & 0xff,
				    input_args_p->intr_cpu, iset.ino);
		} else {
			if (iset.flags == PCITOOL_INTR_FLAG_SET_MSI)
				(void) printf("0x%x,0x%x -> 0x%x,0x%x\n",
				    iset.cpu_id,
				    (input_args_p->intr_msi) & 0xff,
				    input_args_p->intr_cpu, iset.msi);
			else
				(void) printf("0x%x,0x%x -> 0x%x,0x%x\n",
				    iset.cpu_id,
				    (input_args_p->intr_ino) & 0xff,
				    input_args_p->intr_cpu, iset.ino);
		}

	}

	return (rval);
}


static int
do_interrupts(int fd, pcitool_uiargs_t *input_args_p)
{
	if (input_args_p->flags & READ_FLAG) {

		int gic_rval = SUCCESS;
		int gi_rval = SUCCESS;

		if (input_args_p->flags &  SHOWCTLR_FLAG) {
			gic_rval = get_interrupt_ctlr(fd, input_args_p);
		}

		gi_rval = get_interrupts(fd, input_args_p);
		return ((gi_rval != SUCCESS) ? gi_rval : gic_rval);

	} else {

		return (set_interrupts(fd, input_args_p));
	}
}


/* *********** Where it all begins... ************* */

int
main(int argc, char **argv)
{
	pcitool_uiargs_t input_args;	/* Commandline args. */
	int fd;				/* Nexus file descriptor. */
	int rval = SUCCESS;		/* Return status value. */


	/* Get commandline args and options from user. */
	if (get_commandline_args(argc, argv, &input_args) != SUCCESS) {
		return (EINVAL);
	}

	/* Help. */
	if (!(input_args.flags & ALL_COMMANDS))
		return (SUCCESS);

	/*
	 * Probe mode.
	 * Nexus is provided as argv[1] unless PROBEALL mode.
	 */
	if (input_args.flags & PROBE_FLAGS) {
		rval = do_probe_walk(&input_args,
		    ((input_args.flags & PROBEALL_FLAG) ? NULL : argv[1]));

	} else if ((fd = open_node(argv[1], &input_args)) >= 0) {
		if (input_args.flags & (NEXUS_FLAG | LEAF_FLAG)) {
			(void) signal(SIGINT, signal_handler);
			(void) signal(SIGTERM, signal_handler);
			rval = do_device_or_nexus(fd, &input_args);
		} else if (input_args.flags & INTR_FLAG) {
			rval = do_interrupts(fd, &input_args);
		} else {
			/* Should never see this. */
			(void) fprintf(stderr, "Nothing to do.\n");
			rval = ENOTTY;
		}

		(void) close(fd);
	}

	return (rval);
}
