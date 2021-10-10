/*
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This is the user-level program used to inject errors into the
 * cpu/memory subsystem using ioctls to the memtest driver.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_v.h>
#include <sys/physmem.h>
#include "mtst.h"
#include "mtst_vf.h"
#include "mtst_kt.h"

/*
 * These are used to specify the user and kernel debug levels.
 */
uint_t		user_debug = 0;			/* enables user debug code */
uint_t		kern_debug = 0;			/* enables kern debug code */

#define	DRIVER_NODE	"/devices/pseudo/memtest@0:memtest"
#define	DRIVER_U_CONF	"/platform/sun4u/kernel/drv/memtest.conf"
#define	DRIVER_V_CONF	"/platform/sun4v/kernel/drv/memtest.conf"

mdata_t		*mdatap_array[MAX_NTHREADS];
cpu_info_t	*cip_arrayp;
config_file_t	*cfilep;

/*
 * Miscellaneous global variables.
 */
caddr_t		displace = NULL;		/* displacement flush region */
char		*progname;			/* the name of this program */
int		sun4v_flag = FALSE;		/* differentiate sun4u/sun4v */
uint_t		verbose;			/* flag for verbose messaging */
uint_t		logging;			/* flag for syslogd logging */
int		post_test_sleep = 3;		/* sleep after each command */
int		usecloopcnt = 0;		/* microsecond delay loop */

/*
 * Global variables associated with signal catching/recovery.
 */
uint_t		catch_signals;			/* flag for signal catching */
sigjmp_buf	env;				/* sig{set,long}jmp environ */
sigjmp_buf	*envp = NULL;			/* gets initialized to env */

/*
 * Global variables associated with files.
 */
char		*iofile = NULL;			/* file to generate IO errors */
struct flock	flock;				/* lock for config file */
uint_t		config_file_success = 0;	/* flag for config write */

/*
 * Variables used by delay routines.
 */
uint64_t	calibrate_loops	= 1000000;	/* # of loops for calibration */
uint32_t	max_attempts = 5000;		/* # of random bit selections */

/*
 * The order of these defines corresponds to the entries and their placement
 * in cpu_table[] below.  Do NOT change one without changing the other and
 * make sure that CPUTYPE_MAX is defined as the last valid CPU type.
 */
#define	CPUTYPE_SPITFIRE	0
#define	CPUTYPE_BLACKBIRD	1
#define	CPUTYPE_SABRE		2
#define	CPUTYPE_HUMMBRD		3
#define	CPUTYPE_CHEETAH		4
#define	CPUTYPE_CHEETAH_PLUS	5
#define	CPUTYPE_JALAPENO	6
#define	CPUTYPE_JAGUAR		7
#define	CPUTYPE_PANTHER		8
#define	CPUTYPE_SERRANO		9
#define	CPUTYPE_NIAGARA		10
#define	CPUTYPE_NIAGARA2	11
#define	CPUTYPE_VFALLS		12
#define	CPUTYPE_RFALLS		13
#define	CPUTYPE_OLYMPUS_C	14
#define	CPUTYPE_JUPITER		15
#define	CPUTYPE_MAX		CPUTYPE_JUPITER
#define	CPUTYPE_UNKNOWN		(CPUTYPE_MAX + 1)

/*
 * This data structure is used by cpu_type_to_name()
 * and cpu_name_to_type().
 */
typedef struct cpu_type {
	int	c_impl;		/* CPU implementation value */
	char	*c_arg;		/* User argument form of name */
	char	*c_name;	/* CPU name */
} cpu_type_t;

static	cpu_type_t cpu_table[] =
	{
	{SPITFIRE_IMPL,		"sf",	"Spitfire (US-I)"},
	{BLACKBIRD_IMPL,	"bb",	"Blackbird (US-II)"},
	{SABRE_IMPL,		"sa",	"Sabre (US-IIi)"},
	{HUMMBRD_IMPL,		"hb",	"Hummingbird (US-IIe)"},
	{CHEETAH_IMPL,		"ch",	"Cheetah (US-III)"},
	{CHEETAH_PLUS_IMPL,	"chp",	"Cheetah Plus (US-III+)"},
	{JALAPENO_IMPL,		"ja",	"Jalapeno (US-IIIi)"},
	{JAGUAR_IMPL,		"jg",	"Jaguar (US-IV)"},
	{PANTHER_IMPL,		"pn",	"Panther (US-IV+)"},
	{SERRANO_IMPL,		"sr",	"Serrano (US-IIIi+)"},
	{NIAGARA_IMPL,		"ni",	"Niagara (US-T1)"},
	{NIAGARA2_IMPL,		"n2",	"Niagara II (US-T2)"},
	{NIAGARA2_IMPL,		"vf",	"Victoria Falls (US-T2+)"},
	{RFALLS_IMPL,		"kt",	"Rainbow Falls (US-T3)"},
	{OLYMPUS_C_IMPL,	"oc",	"Olympus-C (US64-VI)"},
	{JUPITER_IMPL,		"jp",	"Jupiter (US64-VII)"},
	{NULL,			NULL, 	"UNKNOWN"},
	};

/*
 * Test routines located in this file.
 */
int		do_enable_err(mdata_t *);	/* enable error(s) */
int		do_flush_cache(mdata_t *);	/* flush cache(s) */
int		do_io_err(mdata_t *);		/* io (dma) errors */
int		do_k_err(mdata_t *);		/* other kernel errors */
int		do_notimp(mdata_t *);		/* not supported/impl error */
int		do_u_cp_err(mdata_t *);		/* user copy-back errors */
int		do_u_err(mdata_t *);		/* other user errors */
int		do_ud_wb_err(mdata_t *);	/* user write-back errors */

/*
 * Support routines located in this file.
 */
static int	Sync();				/* flush output and IO */
static caddr_t	alloc_databuf(mdata_t *, int);	/* allocated data buffer */
int		assfail(char *, char *, int);	/* assertion failure */
static int	bind_thread(mdata_t *);		/* bind thread to a cpu */
static uint64_t	calibrate_usecdelay(void);	/* calibrate delay factor */
static int	catchsigs(void);		/* setup signal handling */
int		cmdcmp(char *, char *);		/* command string compare */
static int	cpu_get_type(cpu_info_t *);	/* get type from cpu info */
static int	cpu_init(mdata_t *);		/* call cpu init routine */
static int	cpu_name_to_type(char *);	/* convert cpu name to type */
static caddr_t	cpu_type_to_name(int);		/* convert cpu type to name */
static int	do_cmd(mdata_t *);		/* command frontend routine */
static int	do_ioctl(int, void *, char *);	/* execute an ioctl  */
static uint64_t do_strtoull(const char *,
			char **, char *);	/* call strtoull */
static void	dump_cpu_info(cpu_info_t *);	/* displays cpu info */
static int	fini_config(void);		/* decrement config file cnt */
int		gen_flushall_l2(mdata_t *);	/* flushes entire e$ */
static int	get_a_offset(ioc_t *);		/* get access offset */
static int	get_c_offset(ioc_t *);		/* get corruption offset */
static uint64_t	get_xorpat(mdata_t *);		/* get xor pattern */
static void	handler(int, siginfo_t *, void *); /* signal handler */
static uint64_t index_to_pa(mdata_t *, mem_req_t *,
			int);			/* get pa for index */
static int	init(mdata_t *);		/* initialization routine */
static int	init_config(void);		/* increment config file cnt */
static void	init_cpus_chosen_list(mdata_t *); /* init chosen list */
static int	init_cpu_info(mdata_t *);	/* init cpu related info */
static int	init_thread(mdata_t *);		/* init one thread */
static int	init_threads(mdata_t *);	/* init all threads */
static int	mem_is_local(cpu_info_t *, uint64_t); /* test if local mem */
void		msg(int, const char *, ...);	/* generic message logging */
static caddr_t	map_pa_to_va(uint64_t, int);	/* map paddr to vaddr */
static int	popc64(uint64_t data);		/* counts # of bits set */
static int	post_test(mdata_t *);		/* post-test specific init */
static int	pre_test(mdata_t *);		/* pre-test specific init */
static void	print_user_mode_buf_info(mdata_t *); /* print user info */
static void	release_physmem(void);		/* free /dev/physmem buffer */
static int	save_kvars(void);		/* save kernel vars */
static int	unbind_thread(mdata_t *);	/* unbind thread */
static void	usage(mdata_t *, int, int);	/* displays usage messages */
static void	usecdelay(mdata_t *, int);	/* delays for a # of usec */
int		wait_sync(mdata_t *, int, int, char *);

/*
 * These sun4u and sun4v Generic errors are grouped according to the
 * definitions in the header file.
 *
 * Because of the formatting that is done in usage(), continuation lines
 * should begin with three tabs and usage strings should not be more than
 * ~50 characters wide to display well on an 80 char wide terminal.
 */
cmd_t sun4u_generic_cmds[] = {

	/*
	 * System bus memory errors.
	 */
	"kdue",			do_k_err,		G4U_KD_UE,
	MASK(ALL_BITS),		BIT(3)|BIT(2),
	MASK(0x1ff),		BIT(3)|BIT(2),
	OFFSET(32),		OFFSET(0),
	"Cause a kern data uncorrectable memory error.",

	"kiue",			do_k_err,		G4U_KI_UE,
	MASK(ALL_BITS),		BIT(4)|BIT(3),
	MASK(0x1ff),		BIT(4)|BIT(3),
	OFFSET(16),		NULL,
	"Cause a kern instr uncorrectable memory error.",

	"udue",			do_u_err,		G4U_UD_UE,
	MASK(ALL_BITS),		BIT(5)|BIT(4),
	MASK(0x1ff),		BIT(5)|BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a user data uncorrectable memory error.",

	"uiue",			do_u_err,		G4U_UI_UE,
	MASK(ALL_BITS), 	BIT(6)|BIT(5),
	MASK(0x1ff),		BIT(6)|BIT(5),
	OFFSET(0),		NULL,
	"Cause a user instr uncorrectable memory error.",

	"ioue",			do_io_err,		G4U_IO_UE,
	MASK(ALL_BITS),		BIT(7)|BIT(6),
	MASK(0x1ff),		BIT(7)|BIT(6),
	OFFSET(0),		OFFSET(0),
	"Cause an I/O uncorrectable memory error.",

	"kdce",			do_k_err,		G4U_KD_CE,
	MASK(ALL_BITS),		BIT(7),
	MASK(0x1ff),		BIT(7),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data correctable memory error.",

	"kdcetl1",		do_k_err,		G4U_KD_CETL1,
	MASK(ALL_BITS),		BIT(8),
	MASK(0x1ff),		BIT(8),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data correctable memory error at\n"
	"\t\t\ttrap level 1.",

	"kdcestorm",		do_k_err,		G4U_KD_CESTORM,
	MASK(ALL_BITS),		BIT(9),
	MASK(0x1ff),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Cause a kern data correctable memory error storm.",

	"kice",			do_k_err,		G4U_KI_CE,
	MASK(ALL_BITS),		BIT(10),
	MASK(0x1ff),		BIT(1),
	OFFSET(0),		NULL,
	"Cause a kern instr correctable memory error.",

	"kicetl1",		do_k_err,		G4U_KI_CETL1,
	MASK(ALL_BITS),		BIT(11),
	MASK(0x1ff),		BIT(2),
	OFFSET(0),		NULL,
	"Cause a kern instr correctable memory error at\n"
	"\t\t\ttrap level 1.",

	"udce",			do_u_err,		G4U_UD_CE,
	MASK(ALL_BITS),		BIT(12),
	MASK(0x1ff),		BIT(3),
	OFFSET(0),		OFFSET(0),
	"Cause a user data correctable memory error.",

	"uice",			do_u_err,		G4U_UI_CE,
	MASK(ALL_BITS),		BIT(13),
	MASK(0x1ff),		BIT(4),
	OFFSET(0),		OFFSET(0),
	"Cause a user instr correctable memory error.",

	"ioce",			do_io_err,		G4U_IO_CE,
	MASK(ALL_BITS),		BIT(14),
	MASK(0x1ff),		BIT(5),
	OFFSET(0),		OFFSET(0),
	"Cause an I/O correctable memory error.",

	"mphys=paddr,xor",	do_k_err,		G4U_MPHYS,
	MASK(ALL_BITS),		BIT(15),
	MASK(0x1ff),		BIT(6),
	NULL,			NULL,
	"Insert an ecc error into memory at physical address\n"
	"\t\t\t\"paddr\" delayed by \"delay\" seconds if specified.",

	"kmvirt=kvaddr,xor",	do_k_err,		G4U_KMVIRT,
	MASK(ALL_BITS),		BIT(16),
	MASK(0x1ff),		BIT(7),
	NULL,			NULL,
	"Insert an ecc error into memory at the byte at\n"
	"\t\t\tkernel virtual address \"kvaddr\".",

	"umvirt=uvaddr,xor,pid", do_notimp,		G4U_UMVIRT,
	MASK(ALL_BITS),		BIT(17),
	MASK(0x1ff),		BIT(8),
	NULL,			NULL,
	"Insert an ecc error into memory at the byte at user\n"
	"\t\t\tvirtual address \"uvaddr\" for process \"pid\".",

	"kmpeek=paddr,,nc,size", do_k_err,		G4U_KMPEEK,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Peek at physical memory location \"paddr\".\n"
	"\t\t\tIf \"nc\" is non-zero the mapping is non-cacheable.\n"
	"\t\t\t\"size\" specifies the size of the access.",

	"kmpoke=paddr,data,nc,size", do_k_err,		G4U_KMPOKE,
	NULL,			NULL,
	NULL,			NULL,
	NULL,			NULL,
	"Poke \"data\" to physical memory location \"paddr\".\n"
	"\t\t\tIf \"nc\" is non-zero the mapping is non-cacheable.\n"
	"\t\t\t\"size\" specifies the size of the access.",

	/*
	 * L2$ physical and virtual errors.
	 */
	"ephys=addr,xor",	do_k_err,		G4U_L2PHYS,
	MASK(ALL_BITS),		BIT(0),
	MASK(0x1ff),		BIT(0),
	NULL,			NULL,
	"Insert an error into the ecache at offset\n"
	"\t\t\t\"addr\" delayed by \"delay\" seconds if specified.\n"
	"\t\t\tThis does not modify the cache line state.",

	"etphys=addr,xor,data",	do_k_err,		G4U_L2TPHYS,
	MASK(0x1fffffff),	BIT(1),
	MASK(0x1fffffff), 	BIT(1),
	NULL,			NULL,
	"Insert a simulated error into the ecache tag at\n"
	"\t\t\toffset \"addr\" delayed by \"delay\" seconds if specified.\n"
	"\t\t\tThis may modify the cache line state.",

	"kevirt=addr",		do_k_err,		G4U_KL2VIRT,
	MASK(ALL_BITS),		BIT(2),
	MASK(0x1ff),		BIT(2),
	NULL,			NULL,
	"Insert an error into the e$ at kernel\n"
	"\t\t\tvirtual address \"addr\".\n"
	"\t\t\tThis does modify the cache line state.",

	"uevirt=addr,xor,pid",	do_notimp,		G4U_UL2VIRT,
	MASK(ALL_BITS),		BIT(3),
	MASK(0x1ff),		BIT(3),
	NULL,			NULL,
	"Insert an error into the e$ at user virtual address\n"
	"\t\t\t\"addr\" for process \"pid\".\n"
	"\t\t\tThis does modify the cache line state.",

	/*
	 * D$ physical and virtual errors.
	 */
	"dphys=addr,xor,data",	do_k_err,		G4U_DPHYS,
	MASK(ALL_BITS),		BIT(4),
	MASK(0x1ff),		BIT(4),
	NULL,			NULL,
	"Insert a simulated error into the dcache at\n"
	"\t\t\t\toffset \"addr\" delayed by \"delay\" seconds\n"
	"\t\t\t\tif specified. This does not modify the\n"
	"\t\t\t\tcache line state.",

	/*
	 * I$ physical and virtual errors.
	 */
	"iphys=addr,xor,data",	do_k_err,		G4U_IPHYS,
	MASK(0x3ffffffffffull),	BIT(5),
	MASK(0x3ffffffffffull),	BIT(5),
	NULL,			NULL,
	"Insert a simulated error into the icache at\n"
	"\t\t\t\toffset \"addr\" delayed by \"delay\" seconds\n"
	"\t\t\t\tif specified. This does not modify the\n"
	"\t\t\t\tcache line state.\n",

	/*
	 * End of list.
	 */
	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL};

cmd_t sun4v_generic_cmds[] = {

	/*
	 * Utility to enable error registers from userland.
	 */
	"enable",		do_enable_err,		G4V_ENABLE,
	MASK(ALL_BITS),		BIT(0),
	MASK(0),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Enable the specified error types.",

	/*
	 * Utility to flush processor caches from userland.
	 */
	"flush",		do_flush_cache,		G4V_FLUSH,
	MASK(ALL_BITS),		BIT(0),
	MASK(0),		BIT(0),
	OFFSET(0),		OFFSET(0),
	"Flush the specified caches.",

	/*
	 * Memory (DRAM) errors injected by address.
	 *
	 * NOTE: sun4v processors prior to KT/RF used a 16-bit injection
	 *	 field however KT/RF forward uses a 32-bit field.
	 *	 This is represented below in the data (not check-bit)
	 *	 injections masks.
	 */
	"umvirt=uvaddr,xor,pid", do_k_err,		G4V_UMVIRT,
	MASK(0xffffffff),	BIT(12),
	MASK(0xffff),		BIT(3),
	NULL,			NULL,
	"Insert an ecc error into memory at the byte at user\n"
	"\t\t\tvirtual address \"uvaddr\" for process \"pid\".",

	"kmvirt=kvaddr,xor",	do_k_err,		G4V_KMVIRT,
	MASK(0xffffffff),	BIT(13),
	MASK(0xffff),		BIT(4),
	NULL,			NULL,
	"Insert an ecc error into memory at the byte at\n"
	"\t\t\tkernel virtual address \"kvaddr\".",

	"mreal=raddr,xor",	do_k_err,		G4V_MREAL,
	MASK(0xffffffff),	BIT(14),
	MASK(0xffff),		BIT(5),
	NULL,			NULL,
	"Insert an ecc error into memory at real address\n"
	"\t\t\t\"raddr\".",

	"mphys=paddr,xor",	do_k_err,		G4V_MPHYS,
	MASK(0xffffffff),	BIT(15),
	MASK(0xffff),		BIT(6),
	NULL,			NULL,
	"Insert an ecc error into memory at physical address\n"
	"\t\t\t\"paddr\".",

	/*
	 * Peek/poke commands for checking and modifying the contents of
	 * memory locations determined by a virtual, real, or physical
	 * address.
	 */
	"kvpeek=kvaddr",	do_k_err,		G4V_KVPEEK,
	MASK(ALL_BITS),		NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Peek at kernel virtual memory location \"kvaddr\".",

	"kvpoke=kvaddr,data",	do_k_err,		G4V_KVPOKE,
	MASK(ALL_BITS),		NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Poke \"data\" to kernel virtual memory location \"kvaddr\".",

	"krpeek=raddr,,nc",	do_k_err,		G4V_KMPEEK,
	MASK(ALL_BITS),		NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Peek at real memory location \"raddr\". If \"nc\"\n"
	"\t\t\t\tis non-zero the mapping is non-cacheable.",

	"krpoke=raddr,data,nc",	do_k_err,		G4V_KMPOKE,
	MASK(ALL_BITS),		NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Poke \"data\" to real memory location \"raddr\".\n"
	"\t\t\t\tIf \"nc\" is non-zero the mapping is non-cacheable.",

	"hvpeek=paddr",		do_k_err,		G4V_HVPEEK,
	MASK(ALL_BITS),		NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Peek physical memory location \"paddr\"\n"
	"\t\t\t\tvia hypervisor mode access.",

	"hvpoke=paddr,xor,data", do_k_err,		G4V_HVPOKE,
	MASK(ALL_BITS),		NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Poke \"data\" to physical memory location \"paddr\"\n"
	"\t\t\t\tvia hypervisor mode access.\n"
	"\t\t\t\tIf an optional xor is specified by \"xor\"\n"
	"\t\t\t\tthen \"data\" is ignored and a RMW is performed.",

	/*
	 * Utilities to read/write the contents of an HV ASI from userland.
	 */
	"asipeek=asi,,vaddr",	do_k_err,		G4V_ASIPEEK,
	MASK(ALL_BITS),		NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Peek arbitrary asi register \"asi\".\n"
	"\t\t\t\tThe asi vaddr is specified by \"vaddr\".",

	"asipoke=asi,xor,vaddr,data", do_k_err,		G4V_ASIPOKE,
	MASK(ALL_BITS),		NULL,
	NULL,			NULL,
	OFFSET(0),		OFFSET(0),
	"Poke asi register \"asi\" with \"data\".\n"
	"\t\t\t\tIf an optional xor is specified by \"xor\"\n"
	"\t\t\t\tthen \"data\" is ignored and a RMW is performed.\n"
	"\t\t\t\tThe asi vaddr is specified by \"vaddr\".",

	/*
	 * Commands to place an SP failed epacket on the error queue.
	 */
	"spfail",		do_k_err,		G4V_SPFAIL,
	MASK(0x3),		NULL,
	MASK(0x3),		NULL,
	NULL,			NULL,
	"Insert an SP failed epkt onto the resumable\n"
	"\t\t\t\terror queue.",

	"spgone",		do_k_err,		G4V_SPFAIL,
	MASK(0x3),		BIT(1),
	MASK(0x3),		BIT(1),
	NULL,			NULL,
	"Insert an SP not present in system epkt onto the\n"
	"\t\t\t\tresumable error queue.",

	/*
	 * End of list.
	 */
	NULL,			NULL,			NULL,
	NULL,			NULL,			NULL,
	NULL};

/*
 * Subopt definitions for "-A" option.
 * The order of these must be kept consistent with the cache
 * state definitions in mtst.h.
 */
char	*accesstype_subopts[] = {
	"load",			/* ACCESSTYPE_LOAD (default) */
	"store",		/* ACCESSTYPE_STORE */
	NULL
};

/*
 * Subopt definitions for "-B" option.
 * The order of these must be kept consistent with the data
 * buffer definitions in mtst.h.
 */
char	*buffer_subopts[] = {
	"default",		/* BUFFER_DEFAULT */
	"kern",			/* BUFFER_KERN */
	"user",			/* BUFFER_USER */
	NULL
};

/*
 * Subopt definitions for "-b" option.
 * The order of these must be kept consistent with the thread
 * binding definitions in mtst.h.
 */
char	*bind_subopts[] = {
	"default",		/* THREAD_BIND_DEFAULT */
	"cpuid",		/* THREAD_BIND_CPUID */
	"cputype",		/* THREAD_BIND_CPUTYPE */
	"local_mem",		/* THREAD_BIND_LOCAL_MEM */
	"remote_mem",		/* THREAD_BIND_REMOTE_MEM */
	NULL
};

/*
 * Subopt definitions for "-C" option.
 * The order of these must be kept consistent with the cache
 * definitions in mtst.h.
 */
char	*cache_subopts[] = {
	"default",		/* CACHE_DEFAULT */
	"clean",		/* CACHE_CLEAN */
	"dirty",		/* CACHE_DIRTY */
	"index",		/* CACHE_INDEX */
	"way",			/* CACHE_WAY */
	NULL
};

/*
 * Subopt definitions for "-i" option.
 * The order of these must be kept consistent with the iterate
 * argument option definitions in mtst.h.
 */
char	*iterate_subopts[] = {
	"count",		/* ITERATE_COUNT */
	"stride",		/* ITERATE_STRIDE */
	"infinite",		/* ITERATE_INFINITE */
	NULL
};

/*
 * Subopt definitions for the "-M" option.
 * These user commands invoke corresponding memory request ioctls.
 * The order of these should be kept consistent with the MEMTEST_MEMREQ
 * sub command definitions in memtestio.h.
 */
char	*mem_subopt[] = {
#define	MOPT_NULL		0	/* nop */
	"nop",
#define	MOPT_UVA_TO_PA		1	/* MREQ_UVA_TO_PA */
	"uva_to_pa",
#define	MOPT_UVA_GET_ATTR	2	/* MREQ_UVA_GET_ATTR */
	"uva_getattr",
#define	MOPT_UVA_SET_ATTR	3	/* MREQ_UVA_SET_ATTR */
	"uva_setattr",
#define	MOPT_UVA_LOCK		4	/* MREQ_UVA_LOCK */
	"uva_lock",
#define	MOPT_UVA_UNLOCK		5	/* MREQ_UVA_UNLOCK */
	"uva_unlock",
#define	MOPT_KVA_TO_PA		6	/* MREQ_KVA_TO_PA */
	"kva_to_pa",
#define	MOPT_PA_TO_UNUM		7	/* MREQ_PA_TO_UNUM */
	"pa_to_unum",
#define	MOPT_FIND_FREE_PAGES	8	/* MREQ_FIND_FREE_PAGES */
	"findfree_pages",
#define	MOPT_LOCK_FREE_PAGES	9	/* MREQ_LOCK_FREE_PAGES */
	"lockfree_pages",
#define	MOPT_LOCK_PAGES		10	/* MREQ_LOCK_PAGES */
	"lock_pa",
#define	MOPT_UNLOCK_PAGES	11	/* MREQ_UNLOCK_PAGES */
	"unlock_pa",
#define	MOPT_IDX_TO_PA		12	/* MREQ_IDX_TO_PA */
	"index_to_pa",
#define	MOPT_RA_TO_PA		13	/* MREQ_RA_TO_PA */
	"ra_to_pa",
#define	MOPT_PID		14	/* required for some other commands */
	"pid",
#define	MOPT_SYND		15	/* required for some other commands */
	"synd",
#define	MOPT_SIZE		16	/* required for some other commands */
	"size",
#define	MOPT_PADDR1		17	/* required for some other commands */
	"paddr1",
#define	MOPT_PADDR2		18	/* required for some other commands */
	"paddr2",
	NULL
};

/*
 * Debug decode strings for the sub command types for MEMTEST_MEMREQ ioctl.
 */
static	char		*m_subcmds[]	= {"INVALID", "UVA_TO_PA",
					"UVA_GET_ATTR", "UVA_SET_ATTR",
					"UVA_LOCK", "UVA_UNLOCK",
					"KVA_TO_PA", "PA_TO_UNUM",
					"FIND_FREE_PAGES", "LOCK_FREE_PAGES",
					"LOCK_PAGES", "UNLOCK_PAGES",
					"IDX_TO_PA", "RA_TO_PA"};

/*
 * Subopt definitions for "-m" option.
 * The order of these must be kept consistent with the misc
 * argument option definitions in mtst.h.
 */
char	*misc_subopts[] = {
	"misc1",		/* MISCOPT_MISC1 */
	"misc2",		/* MISCOPT_MISC2 */
	NULL
};

/*
 * Subopt definitions for "-Q" option.
 * The order of these must be kept consistent with the quiesce
 * definitions in mtst.h.
 */
char	*quiesce_subopts[] = {
	"default",		/* QUIESCE_DEFAULT */
	"offline",		/* QUIESCE_OFFLINE */
	"nooffline",		/* QUIESCE_NOOFFLINE */
	"siboffline",		/* QUIESCE_SIBOFFLINE */
	"unoffline",		/* QUIESCE_ONLINE_BF_INVOKE */
	"pause",		/* QUIESCE_PAUSE */
	"nopause",		/* QUIESCE_NOPAUSE */
	"unpause",		/* QUIESCE_UNPAUSE_BF_INVOKE */
	"park",			/* QUIESCE_PARK */
	"nopark",		/* QUIESCE_NOPARK */
	"unpark",		/* QUIESCE_UNPARK_BF_INVOKE */
	"cycsuspend",		/* QUIESCE_CYCLIC_SUSPEND */
	NULL
};

/*
 * Subopt definitions for "-S" option.
 * The order of these must be kept consistent with the misc
 * argument option definitions in mtst.h.
 */
char	*scrub_subopts[] = {
	"memscrub",		/* SCRUBOPT_MEMSCRUB */
	"l2scrub",		/* SCRUBOPT_L2SCRUB */
	NULL
};

/*
 * Subopt definitions for "-R" option.
 */
char	*reg_subopts[] = {
#define	ROPT_NULL		0
	"default",
#define	ROPT_SET_EER		1
	"eer",
#define	ROPT_SET_ECCR		2
	"ecr",
#define	ROPT_SET_DCR		3
	"dcr",
#define	ROPT_SET_DCUCR		4
	"dcucr",
	NULL
};

/*
 * Bit field definitions for flags parameter used by usage().
 */
#define	USAGE_VERBOSE	1		/* display verbose usage */
#define	USAGE_EXIT	2		/* exit to OS */

int
main(int argc, char **argv)
{
	mdata_t		*mdatap;
	cmd_t		*cmdp, **cmdpp;
	ioc_t		*iocp;
	system_info_t	*sip;
	mem_req_t	mr;
	uint64_t	addr = 0;		/* default address */
	uint64_t	xorpat = 0;		/* default corruption pat */
	uint64_t	datal;			/* temp data */
	uint64_t	flags = 0;		/* default flags */
	int		errcnt = 0;		/* count of failed commands */
	int		i, c, ret, found, idx;
	int		cpu_idx = 0;
	struct utsname	uname_struct;
	char		*argp, *options, *value, str[40];

	progname = argv[0];

	/*
	 * Check that we are running as root.
	 */
	if (geteuid() != 0) {
		msg(MSG_FATAL, "You must be root to run this program.\n");
		/* NOTREACHED */
	}

	/*
	 * Check if we are running on a sun4u or sun4v system.
	 */
	if (uname(&uname_struct) == -1) {
		msg(MSG_ERROR, "main: unable to determine system info "
		    "via uname function\n");
		return (-1);
	}

	if (strcmp(uname_struct.machine, "sun4v") == 0) {
		sun4v_flag = TRUE;
	}

	if (argc <= 1) {
		usage(NULL, USAGE_EXIT, 1);
		/* NOT REACHED */
	}

	/*
	 * Allocate some common data structures.
	 */
	if ((iocp = malloc(sizeof (ioc_t))) == NULL) {
		msg(MSG_ABORT, "malloc() failed");
		/* NOT REACHED */
	}
	bzero(iocp, sizeof (ioc_t));
	if ((sip = malloc(sizeof (system_info_t))) == NULL) {
		msg(MSG_ABORT, "malloc() failed");
		/* NOT REACHED */
	}
	bzero(sip, sizeof (system_info_t));

	/*
	 * Initialize thread specific data struct pointers,
	 * and some of the thread data and/or pointers.
	 * Some of the information in the thread data structure
	 * is common to all threads while other data is unique.
	 */
	for (i = 0; i < MAX_NTHREADS; i++) {
		if ((mdatap_array[i] = malloc(sizeof (mdata_t))) == NULL) {
			msg(MSG_ABORT, "malloc() failed");
			/* NOT REACHED */
		}
		bzero(mdatap_array[i], sizeof (mdata_t));
		mdatap_array[i]->m_iocp = iocp;
		mdatap_array[i]->m_cip = 0;
		mdatap_array[i]->m_sip = sip;
		mdatap_array[i]->m_threadno = i;
		mdatap_array[i]->m_bound = 0;
	}

	mdatap = mdatap_array[0];

	/*
	 * Process the options.
	 */
	while ((c = getopt(argc, argv,
	    "A:a:b:B:cC:D:d:Ff:hi:lM:m:Nno:p:Q:R:rS:svX:x:z:")) != EOF) {
		switch (c) {
		/*
		 * Enable forcing an access type of store (rather than load)
		 * to invoke certain error types.
		 */
		case 'A':
			options = optarg;
			idx = getsubopt(&options, accesstype_subopts, &value);
			if (idx != -1)
				(void) snprintf(str, sizeof (str), "Setting "
				    "invoke access type for test(s) "
				    "to %s", accesstype_subopts[idx]);
			switch (idx) {
			case ACCESSTYPE_LOAD:
				msg(MSG_VERBOSE, "%s\n", str);
				break;
			case ACCESSTYPE_STORE:
				IOC_FLAGS(iocp) |= FLAGS_STORE;
				msg(MSG_VERBOSE, "%s\n", str);
				break;
			default:
				msg(MSG_ERROR,
				    "Invalid -A suboption\n");
				usage(NULL, USAGE_EXIT, 1);
				/* NOT REACHED */
			}
			break;
		/*
		 * Physical/virtual address required by some commands or
		 * specific physical address to use for the data buffer.
		 * This is typically specified in the command (e.g. dphys=xxx)
		 * but this is another way of doing it.
		 */
		case 'a':
			addr = do_strtoull(optarg, NULL, "address");
			IOC_FLAGS(iocp) |= FLAGS_ADDR;
			msg(MSG_VERBOSE, "Address = 0x%llx\n", addr);
			break;
		/*
		 * Choose to use a data buffer which is allocated in userland
		 * or in the kernel driver. The user buffer is used by default.
		 */
		case 'B':
			options = optarg;
			idx = getsubopt(&options, buffer_subopts, &value);
			if (idx != -1) {
				(void) snprintf(str, sizeof (str),
				    "Forcing buffer type for test(s) to %s",
				    buffer_subopts[idx]);
			} else {
				idx = BUFFER_DEFAULT;
			}

			switch (idx) {
			case BUFFER_DEFAULT:
				msg(MSG_VERBOSE, "This test(s) will use the "
				    "default USER buffer\n");
			case BUFFER_USER:
				IOC_FLAGS(iocp) |= FLAGS_MAP_USER_BUF;
				msg(MSG_VERBOSE, "%s\n", str);
				break;
			case BUFFER_KERN:
				IOC_FLAGS(iocp) |= FLAGS_MAP_KERN_BUF;
				msg(MSG_VERBOSE, "%s\n", str);
				break;
			default:
				msg(MSG_ERROR, "Invalid -B suboption\n");
				usage(NULL, USAGE_EXIT, 1);
				/* NOT REACHED */
			}
			break;
		/*
		 * Set CPU binding(s).
		 */
		case 'b':
			options = optarg;
			while (*options != '\0') {
				idx = getsubopt(&options, bind_subopts, &value);
				if (idx != -1)
					(void) snprintf(str, sizeof (str),
					    "Thread=%d, Bind=%s", cpu_idx,
					    bind_subopts[idx]);
				switch (idx) {
				case THREAD_BIND_DEFAULT:
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case THREAD_BIND_CPUID:
					datal = do_strtoull(value, NULL,
					    "cpuid");
					iocp->ioc_bind_thr_data[cpu_idx] =
					    datal;
					msg(MSG_VERBOSE, "%s=%d\n",
					    str, (int)datal);
					break;
				case THREAD_BIND_LOCAL_MEM:
					msg(MSG_VERBOSE, "%s=local\n", str);
					break;
				case THREAD_BIND_REMOTE_MEM:
					msg(MSG_VERBOSE, "%s=remote\n", str);
					break;
				case THREAD_BIND_CPUTYPE:
					if (value == NULL) {
						usage(NULL, USAGE_EXIT, 1);
						/* NOT REACHED */
					}
					datal = cpu_name_to_type(value);
					if (datal == CPUTYPE_UNKNOWN) {
						msg(MSG_ERROR, "Unrecognized "
						    "CPU type = %s\n", value);
						exit(1);
						/* NOT REACHED */
					}
					iocp->ioc_bind_thr_data[cpu_idx] =
					    datal;
					msg(MSG_VERBOSE, "%s=%s\n", str, value);
					break;
				default:
					msg(MSG_ERROR,
					    "Invalid -b suboption\n");
					usage(NULL, USAGE_EXIT, 1);
					/* NOT REACHED */
				}
				iocp->ioc_bind_thr_criteria[cpu_idx++] = idx;
				IOC_FLAGS(iocp) |= FLAGS_BINDCPU;
			}
			break;
		/*
		 * Force a cache line state for tests and caches (such as L2
		 * or L3) which support this option.  Valid states are "clean"
		 * and "dirty" where dirty usually means the modified (M) bit
		 * is set in the cache-line state.
		 *
		 * The "index" and "way" suboptions allow a specific index
		 * and way to be targetted by the command for those processors
		 * and caches which support this option.
		 */
		case 'C':
			options = optarg;
			while (*options != '\0') {
				idx = getsubopt(&options, cache_subopts,
				    &value);
				if (idx != -1)
					(void) snprintf(str, sizeof (str),
					    "Forcing cacheline %s",
					    cache_subopts[idx]);
				switch (idx) {
				case CACHE_DEFAULT:
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case CACHE_CLEAN:
					IOC_FLAGS(iocp) |= FLAGS_CACHE_CLN;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case CACHE_DIRTY:
					IOC_FLAGS(iocp) |= FLAGS_CACHE_DIRTY;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case CACHE_INDEX:
					IOC_FLAGS(iocp) |= FLAGS_CACHE_INDEX;
					iocp->ioc_cache_idx = do_strtoull(value,
					    NULL, "cache index");
					msg(MSG_VERBOSE, "Setting -C index "
					    "flag, value 0x%llx\n",
					    iocp->ioc_cache_idx);
					break;
				case CACHE_WAY:
					IOC_FLAGS(iocp) |= FLAGS_CACHE_WAY;
					iocp->ioc_cache_way = do_strtoull(value,
					    NULL, "cache way");
					msg(MSG_VERBOSE, "Setting -C way "
					    "flag, value 0x%lx\n",
					    iocp->ioc_cache_way);
					break;
				default:
					msg(MSG_ERROR,
					    "Invalid -C suboption\n");
					usage(NULL, USAGE_EXIT, 1);
					/* NOT REACHED */
				}
			}

			if (F_CACHE_CLN(iocp) && F_CACHE_DIRTY(iocp)) {
				msg(MSG_ERROR, "Cannot combine clean and "
				    "dirty as -C suboptions");
				usage(NULL, USAGE_EXIT, 1);
			}
			break;
		/*
		 * Set flag to indicate that errors should be injected
		 * into check bits rather than data bits.
		 */
		case 'c':
			IOC_FLAGS(iocp) |= FLAGS_CHKBIT;
			msg(MSG_VERBOSE, "Check Bit Errors Enabled\n");
			break;
		/*
		 * Turn on debugging messages and code.
		 * Bits 3:0 of the debug value passed in apply to user level
		 * while bits 7:4 apply to kernel level.
		 */
		case 'D':
			user_debug = do_strtoull(optarg, NULL, "debug value");
			kern_debug = (user_debug >> 4) & 0xF;
			user_debug &= 0xF;
			if (user_debug) {
				IOC_FLAGS(iocp) |= FLAGS_USER_DEBUG;
				msg(MSG_VERBOSE, "User Debug Flag "
				    "= %d\n", user_debug);
			}
			if (kern_debug) {
				IOC_FLAGS(iocp) |= FLAGS_KERN_DEBUG;
				msg(MSG_VERBOSE, "Kern Debug flag "
				    "= %d\n", kern_debug);
			}
			break;
		/*
		 * Delay (in seconds) in the driver before executing some
		 * commands (e.g. ephys and kevirt).  Default is no delay.
		 */
		case 'd':
			iocp->ioc_delay = do_strtoull(optarg, NULL, "delay");
			IOC_FLAGS(iocp) |= FLAGS_DELAY;
			msg(MSG_VERBOSE, "Delay = %d\n", iocp->ioc_delay);
			break;
		/*
		 * Disable flushing of caches before injection for test
		 * types which support this option.
		 */
		case 'F':
			IOC_FLAGS(iocp) |= FLAGS_FLUSH_DIS;
			msg(MSG_VERBOSE, "Cache Flushing Disabled\n");
			break;
		/*
		 * Filename used by IO error commands.
		 * Default is to use a temporary file name.
		 */
		case 'f':
			iofile = optarg;
			msg(MSG_VERBOSE, "IO Filename = %s\n", iofile);
			break;
		/*
		 * Help. Display usage message.
		 */
		case 'h':
			if (init(mdatap) != 0) {
				msg(MSG_ERROR, "initialization failed\n");
				usage(mdatap, USAGE_EXIT, 1);
				/* NOT REACHED */
			} else {
				usage(mdatap, USAGE_VERBOSE | USAGE_EXIT, 0);
				/* NOT REACHED */
			}
			break;
		/*
		 * Enable infinite error injection mode for tests which
		 * support it (sun4v processors have certain error injection
		 * mechanisms that can be set to continuous mode), and
		 * specify an iteration count and stride.
		 */
		case 'i':
			options = optarg;
			while (*options != '\0') {
				idx = getsubopt(&options, iterate_subopts,
				    &value);
				switch (idx) {
				case ITERATE_COUNT:
					iocp->ioc_i_count = do_strtoull(value,
					    NULL, "count");
					IOC_FLAGS(iocp) |= FLAGS_I_COUNT;
					msg(MSG_VERBOSE, "Setting -i count "
					    "flag, value 0x%llx\n",
					    iocp->ioc_i_count);
					break;
				case ITERATE_STRIDE:
					iocp->ioc_i_stride = do_strtoull(value,
					    NULL, "stride");
					IOC_FLAGS(iocp) |= FLAGS_I_STRIDE;
					msg(MSG_VERBOSE, "Setting -i stride "
					    "flag, value 0x%llx\n",
					    iocp->ioc_i_stride);
					break;
				case ITERATE_INFINITE:
					if (sun4v_flag == FALSE) {
						msg(MSG_ERROR, "The -i "
						    "infinite option is not "
						    "available on %s "
						    "systems\n",
						    uname_struct.machine);
						usage(NULL, USAGE_EXIT, 1);
					}

					IOC_FLAGS(iocp) |= FLAGS_INF_INJECT;
					msg(MSG_VERBOSE, "Infinite error "
					    "injection mode enabled\n");
					break;
				default:
					msg(MSG_ERROR,
					    "Invalid -i suboption\n");
					usage(mdatap, USAGE_EXIT, 1);
					/* NOT REACHED */
				}
			}
			break;
		/*
		 * Enable logging of messages to syslogd.
		 */
		case 'l':
			logging++;
			IOC_FLAGS(iocp) |= FLAGS_SYSLOG;
			msg(MSG_VERBOSE, "Logging Enabled\n");
			break;
		/*
		 * User invoked memory commands.
		 */
		case 'M':
			options = optarg;
			bzero(&mr, sizeof (mem_req_t));
			mr.m_pid = -1;
			mr.m_synd = -1;
			mr.m_size = -1;
			while (*options != '\0') {
				idx = getsubopt(&options, mem_subopt, &value);
				switch (idx) {
				case MOPT_UVA_TO_PA:
					mr.m_vaddr = do_strtoull(value, NULL,
					    "uvaddr");
					mr.m_cmd = MREQ_UVA_TO_PA;
					break;
				case MOPT_UVA_LOCK:
					mr.m_vaddr = do_strtoull(value, NULL,
					    "uvaddr");
					mr.m_cmd = MREQ_UVA_LOCK;
					break;
				case MOPT_UVA_UNLOCK:
					mr.m_vaddr = do_strtoull(value, NULL,
					    "uvaddr");
					mr.m_cmd = MREQ_UVA_UNLOCK;
					break;
				case MOPT_KVA_TO_PA:
					mr.m_vaddr = do_strtoull(value, NULL,
					    "kvaddr");
					mr.m_cmd = MREQ_KVA_TO_PA;
					break;
				case MOPT_PA_TO_UNUM:
					mr.m_paddr1 = do_strtoull(value, NULL,
					    "paddr");
					mr.m_cmd = MREQ_PA_TO_UNUM;
					mr.m_str[0] = 0;
					break;
				case MOPT_PID:
					mr.m_pid = (long)do_strtoull(value,
					    NULL, "pid");
					break;
				case MOPT_SYND:
					mr.m_synd = do_strtoull(value, NULL,
					    "synd");
					break;
				case MOPT_SIZE:
					mr.m_size = do_strtoull(value, NULL,
					    "size");
					break;
				case MOPT_PADDR1:
					mr.m_paddr1 = do_strtoull(value, NULL,
					    "paddr1");
					break;
				case MOPT_PADDR2:
					mr.m_paddr2 = do_strtoull(value, NULL,
					    "paddr2");
					break;
				case MOPT_FIND_FREE_PAGES:
					mr.m_cmd = MREQ_FIND_FREE_PAGES;
					break;
				/*
				 * These are currently unsupported from
				 * the command line.
				 */
				case MOPT_UVA_GET_ATTR:
				case MOPT_UVA_SET_ATTR:
				case MOPT_LOCK_FREE_PAGES:
				case MOPT_UNLOCK_PAGES:
				case MOPT_IDX_TO_PA:
				case MOPT_RA_TO_PA:
					msg(MSG_ERROR,
					    "Unsupported -M suboption\n");
					usage(mdatap, USAGE_EXIT, 1);
					/* NOT REACHED */
				default:
					msg(MSG_ERROR,
					    "Invalid -M suboption\n");
					usage(mdatap, USAGE_EXIT, 1);
					/* NOT REACHED */
				}
			}
			msg(MSG_VERBOSE, "VA_TO_PA: sending: cmd=0x%x, "
			    "vaddr=0x%llx, pid=%d, "
			    "paddr1=0x%llx, paddr2=0x%llx, synd=0x%x\n",
			    mr.m_cmd, mr.m_vaddr, mr.m_pid,
			    mr.m_paddr1, mr.m_paddr2, mr.m_synd);

			if (do_ioctl(MEMTEST_MEMREQ, &mr, "main") == -1) {
				msg(MSG_ERROR, "main: ioctl failed\n");
				exit(1);
			}
			switch (mr.m_cmd) {
			case MREQ_PA_TO_UNUM:
				if (mr.m_synd == -1) {
					msg(MSG_ERROR, "PA_TO_UNUM: synd "
					    "needs to be specified\n");
					usage(mdatap, USAGE_EXIT, 1);
					/* NOT REACHED */
				}
				msg(MSG_INFO, "PA_TO_UNUM: paddr=0x%llx, "
				    "synd=0x%x, unum=0x%s\n",
				    mr.m_paddr1, mr.m_synd, mr.m_str);
				break;
			case MREQ_UVA_TO_PA:
				msg(MSG_INFO, "UVA_TO_PA: "
				    "vaddr=0x%p, pid=%d, paddr=0x%llx\n",
				    mr.m_vaddr, mr.m_pid, mr.m_paddr1);
				break;
			case MREQ_KVA_TO_PA:
				msg(MSG_INFO, "KVA_TO_PA: "
				    "vaddr=0x%p, paddr=0x%llx\n",
				    mr.m_vaddr, mr.m_paddr1);
				break;
			case MREQ_FIND_FREE_PAGES:
				msg(MSG_INFO, "FIND_FREE_PAGES: "
				    "size=0x%llx, paddr=0x%llx\n",
				    mr.m_size, mr.m_paddr1);
				break;
			default:
				usage(mdatap, USAGE_EXIT, 1);
				/* NOT REACHED */
			}
			exit(0);
			break;
		/*
		 * Allow the MISC arguments to be used with commands in the
		 * regular/normal format via this option.
		 */
		case 'm':
			options = optarg;
			while (*options != '\0') {
				idx = getsubopt(&options, misc_subopts, &value);
				switch (idx) {
				case MISCOPT_MISC1:
					iocp->ioc_misc1 = do_strtoull(value,
					    NULL, "misc1");
					IOC_FLAGS(iocp) |= FLAGS_MISC1;
					msg(MSG_VERBOSE, "Setting MISC1 flag, "
					    "value 0x%llx\n", iocp->ioc_misc1);
					break;
				case MISCOPT_MISC2:
					iocp->ioc_misc2 = do_strtoull(value,
					    NULL, "misc2");
					IOC_FLAGS(iocp) |= FLAGS_MISC2;
					msg(MSG_VERBOSE, "Setting MISC2 flag, "
					    "value 0x%llx\n", iocp->ioc_misc2);
					break;
				default:
					msg(MSG_ERROR,
					    "Invalid -m suboption\n");
					usage(mdatap, USAGE_EXIT, 1);
					/* NOT REACHED */
				}
			}
			break;
		/*
		 * Disable the setting of the error enable registers.
		 */
		case 'N':
			IOC_FLAGS(iocp) |= FLAGS_NOSET_EERS;
			msg(MSG_VERBOSE, "No Error Enable registers will "
			    "be modified\n");
			break;
		/*
		 * Debugging feature.
		 * Inject the error but don't invoke it.
		 */
		case 'n':
			IOC_FLAGS(iocp) |= FLAGS_NOERR;
			msg(MSG_VERBOSE, "No access to invoke error will be "
			    "performed (if supported)\n");
			break;
		/*
		 * Corruption offset.
		 * Default comes from command structure.
		 */
		case 'o':
			iocp->ioc_c_offset = do_strtoull(optarg, NULL,
			    "corruption offset");
			IOC_FLAGS(iocp) |= FLAGS_CORRUPT_OFFSET;
			msg(MSG_VERBOSE, "Corruption Offset = "
			    "0x%x\n", iocp->ioc_c_offset);
			break;
		/*
		 * Allow user control over how tests use the offline_cpu()
		 * and pause_cpus() calls during the injection and/or the
		 * invocation of certain error types.
		 *
		 * This option can be extended for other system quiecing
		 * such as IO disabling.
		 *
		 * Note that this option uses it's own qflags element in
		 * the ioc struct unlike most other options.
		 *
		 * Currently the injector defaults to offlining all unused
		 * cpus (threads are bound to separate cpus) for memory,
		 * cache (any), and internal (any) error types.  Many of
		 * the options below cannot be used simultaneously but this
		 * condition is not checked for.  Using offlining and
		 * pausing simultaneously is not supported (or of any use),
		 * but in order to use pausing instead of offlining both the
		 * "nooffline" and "pause" flags must be used.
		 */
		case 'Q':
			options = optarg;
			while (*options != '\0') {
				idx = getsubopt(&options, quiesce_subopts,
				    &value);
				if (idx != -1)
					(void) snprintf(str, sizeof (str),
					    "Forcing quiesce option to %s",
					    quiesce_subopts[idx]);
				switch (idx) {
				case QUIESCE_DEFAULT:
				case QUIESCE_OFFLINE:
					IOC_QFLAGS(iocp) |=
					    QFLAGS_OFFLINE_CPUS_EN;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case QUIESCE_NOOFFLINE:
					IOC_QFLAGS(iocp) |=
					    QFLAGS_OFFLINE_CPUS_DIS;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case QUIESCE_SIBOFFLINE:
					IOC_QFLAGS(iocp) |=
					    QFLAGS_OFFLINE_SIB_CPUS;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case QUIESCE_ONLINE_BF_INVOKE:
					IOC_QFLAGS(iocp) |=
					    QFLAGS_ONLINE_CPUS_BFI;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case QUIESCE_PAUSE:
					IOC_QFLAGS(iocp) |=
					    QFLAGS_PAUSE_CPUS_EN;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case QUIESCE_NOPAUSE:
					IOC_QFLAGS(iocp) |=
					    QFLAGS_PAUSE_CPUS_DIS;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case QUIESCE_UNPAUSE_BF_INVOKE:
					IOC_QFLAGS(iocp) |=
					    QFLAGS_UNPAUSE_CPUS_BFI;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case QUIESCE_PARK:
					IOC_QFLAGS(iocp) |=
					    QFLAGS_PARK_CPUS_EN;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case QUIESCE_NOPARK:
					IOC_QFLAGS(iocp) |=
					    QFLAGS_PARK_CPUS_DIS;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case QUIESCE_UNPARK_BF_INVOKE:
					IOC_QFLAGS(iocp) |=
					    QFLAGS_UNPARK_CPUS_BFI;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				case QUIESCE_CYCLIC_SUSPEND:
					IOC_QFLAGS(iocp) |=
					    QFLAGS_CYC_SUSPEND_EN;
					msg(MSG_VERBOSE, "%s\n", str);
					break;
				default:
					msg(MSG_ERROR,
					    "Invalid -Q suboption\n");
					usage(NULL, USAGE_EXIT, 1);
					/* NOT REACHED */
				}
			}

			if ((IOC_QFLAGS(iocp) & QFLAGS_PAUSE_CPUS_EN) &&
			    (IOC_QFLAGS(iocp) & QFLAGS_OFFLINE_CPUS_EN)) {
				IOC_QFLAGS(iocp) &= ~(QFLAGS_PAUSE_CPUS_EN);
				msg(MSG_WARN, "CPU offline and pause -Q "
				    "suboptions are mutually exclusive, "
				    "using offine for this test\n");
			}
			break;
		/*
		 * Access offset.
		 * Default comes from command structure.
		 */
		case 'p':
			iocp->ioc_a_offset = do_strtoull(optarg, NULL,
			    "access offset");
			IOC_FLAGS(iocp) |= FLAGS_ACCESS_OFFSET;
			msg(MSG_VERBOSE, "Access Offset = 0x%x\n",
			    iocp->ioc_a_offset);
			break;
		/*
		 * Set a register to a specific value before running tests.
		 */
		case 'R':
			if (sun4v_flag == FALSE) {
				options = optarg;
				while (*options != '\0') {
					idx = getsubopt(&options, reg_subopts,
					    &value);
					datal = do_strtoull(value, NULL,
					    "register value");
					switch (idx) {
					case ROPT_SET_EER:
						iocp->ioc_eer = datal;
						IOC_FLAGS(iocp) |=
						    FLAGS_SET_EER;
						msg(MSG_VERBOSE, "EER=0x%llx\n",
						    iocp->ioc_eer);
						break;
					case ROPT_SET_ECCR:
						iocp->ioc_ecr = datal;
						IOC_FLAGS(iocp) |=
						    FLAGS_SET_ECCR;
						msg(MSG_VERBOSE, "ECCR=0x%llx"
						    "\n", iocp->ioc_ecr);
						break;
					case ROPT_SET_DCR:
						iocp->ioc_dcr = datal;
						IOC_FLAGS(iocp) |=
						    FLAGS_SET_DCR;
						msg(MSG_VERBOSE, "DCR=0x%llx\n",
						    iocp->ioc_dcr);
						break;
					case ROPT_SET_DCUCR:
						iocp->ioc_dcucr = datal;
						IOC_FLAGS(iocp) |=
						    FLAGS_SET_DCUCR;
						msg(MSG_VERBOSE, "DCUCR=0x%llx"
						    "\n", iocp->ioc_dcucr);
						break;
					default:
						msg(MSG_ERROR, "Invalid -R "
						    "suboption\n");
						usage(mdatap, USAGE_EXIT, 1);
						/* NOT REACHED */
					}
				}
			} else {
				msg(MSG_ERROR, "The -R option is not available "
				    "on %s systems\n", uname_struct.machine);
					usage(NULL, USAGE_EXIT, 1);
			}
			break;
		/*
		 * Enable randomization.
		 */
		case 'r':
			IOC_FLAGS(iocp) |= FLAGS_RANDOM;
			msg(MSG_VERBOSE, "Randomization Enabled\n");
			break;
		/*
		 * Control the HW scrubbers during injection and invocation
		 * for test types which support this option.
		 */
		case 'S':
			if (sun4v_flag == FALSE) {
				msg(MSG_ERROR, "The -S option is not available "
				    "on %s systems\n", uname_struct.machine);
				usage(NULL, USAGE_EXIT, 1);
				/* NOT REACHED */
			}
			options = optarg;
			while (*options != '\0') {
				idx = getsubopt(&options, scrub_subopts,
				    &value);
				switch (idx) {
				case SCRUBOPT_MEMSCRUB:
					if (strcmp(value, "disable") == 0) {
						IOC_FLAGS(iocp) |=
						    FLAGS_MEMSCRUB_DISABLE;
					} else if (strcmp(value, "enable") ==
					    0) {
						IOC_FLAGS(iocp) |=
						    FLAGS_MEMSCRUB_ENABLE;
					} else if (strcmp(value, "asis") == 0) {
						IOC_FLAGS(iocp) |=
						    FLAGS_MEMSCRUB_ASIS;
					} else {
						msg(MSG_ERROR, "Invalid -S "
						    "suboption value %s\n",
						    value);
						usage(mdatap, USAGE_EXIT, 1);
						/* NOT REACHED */
					}

					msg(MSG_VERBOSE, "Setting MEM SCRUB "
					    "flag to %s\n", value);
					break;
				case SCRUBOPT_L2SCRUB:
					if (strcmp(value, "disable") == 0) {
						IOC_FLAGS(iocp) |=
						    FLAGS_L2SCRUB_DISABLE;
					} else if (strcmp(value, "enable") ==
					    0) {
						IOC_FLAGS(iocp) |=
						    FLAGS_L2SCRUB_ENABLE;
					} else if (strcmp(value, "asis") == 0) {
						IOC_FLAGS(iocp) |=
						    FLAGS_L2SCRUB_ASIS;
					} else {
						msg(MSG_ERROR, "Invalid -S "
						    "suboption value %s\n",
						    value);
						usage(mdatap, USAGE_EXIT, 1);
						/* NOT REACHED */
					}

					msg(MSG_VERBOSE, "Setting L2 SCRUB "
					    "flag to %s\n", value);
					break;
				default:
					msg(MSG_ERROR, "Invalid -S "
					    "suboption\n");
					usage(mdatap, USAGE_EXIT, 1);
					/* NOT REACHED */
				}
			}
			break;
		/*
		 * Enable signal catching.
		 */
		case 's':
			catch_signals++;
			msg(MSG_VERBOSE, "Signal Catching Enabled\n");
			break;
		/*
		 * Enable verbose messages in user program and kernel.
		 */
		case 'v':
			verbose++;
			IOC_FLAGS(iocp) |= FLAGS_VERBOSE;
			msg(MSG_VERBOSE, "Verbose Enabled\n");
			break;
		/*
		 * Use crosscalls to another CPU to inject/invoke errors.
		 */
		case 'X':
			iocp->ioc_xc_cpu = do_strtoull(optarg, NULL,
			    "cross-call CPU");
			IOC_FLAGS(iocp) |= FLAGS_XCALL;
			msg(MSG_VERBOSE, "Cross Call CPU = 0x%x\n",
			    iocp->ioc_xc_cpu);
			break;
		/*
		 * XOR pattern to use for corruption.
		 * Default comes from command structure.
		 */
		case 'x':
			xorpat = do_strtoull(optarg, NULL, "xor pattern");
			IOC_FLAGS(iocp) |= FLAGS_XORPAT;
			msg(MSG_VERBOSE, "XOR Pattern = 0x%llx\n", xorpat);
			break;
		/*
		 * Delay (in seconds) in user program after executing each
		 * command, flushing IO and syncing filesystems.
		 */
		case 'z':
			post_test_sleep = do_strtoull(optarg, NULL,
			    "sleep value");
			msg(MSG_VERBOSE, "Sleep Interval = %d\n",
			    post_test_sleep);
			break;
		default:
			msg(MSG_ERROR, "Unrecognized option \"%c\"\n", c);
			usage(mdatap, USAGE_EXIT, 1);
			/* NOT REACHED */
		}
	}

	/*
	 * Display info on thread data structs.
	 */
	for (i = 0; i < MAX_NTHREADS; i++) {
		msg(MSG_DEBUG2, "mdatap_array[%d]=0x%p\n", i, mdatap_array[i]);
		msg(MSG_DEBUG2, "mdatap_array[%d]->m_sip=0x%p\n",
		    i, mdatap_array[i]->m_sip);
	}

	/*
	 * Save the initial flags for later reference.
	 */
	flags = IOC_FLAGS(iocp);
	msg(MSG_VERBOSE, "%s system detected\n", uname_struct.machine);

	/*
	 * Do some general initialization.
	 */
	if (init(mdatap) != 0) {
		msg(MSG_FATAL, "initialization failed\n");
		/* NOTREACHED */
	}

	/*
	 * Make sure that at least one command was specified.
	 */
	if (optind == argc) {
		usage(mdatap, USAGE_EXIT, 1);
		/* NOT REACHED */
	}

	/*
	 * Save our environment so we can recover from signals.
	 */
	if ((ret = sigsetjmp(env, 1)) == 0) {
		envp = &env;
	} else {
		msg(MSG_DEBUG1, "returned from signal handler, ret=%d\n", ret);
	}

	/*
	 * Process and execute each command.
	 */
	while (optind < argc && !errcnt) {
		char	*addrp;

		/*
		 * Re-initialize the global options for the next command
		 * in case they were overridden by a previous command's
		 * specific options.
		 */
		iocp->ioc_flags  = flags;
		iocp->ioc_addr   = addr;
		iocp->ioc_xorpat = xorpat;

		argp = argv[optind++];

		msg(MSG_DEBUG1,
		    "###############################################\n");
		msg(MSG_DEBUG1,
		    "processing next argument %s\n", argp);
		msg(MSG_DEBUG1,
		    "###############################################\n\n");

		/*
		 * If an "=" is found in the command then parse the arguments
		 * since it is of the form:
		 *	command=<address>,<xorpat>,<misc1>,<misc2>
		 */
		if ((addrp = strchr(argp, '=')) != NULL) {
			char	*xorpatp = NULL, *misc1p = NULL, *misc2p = NULL;

			/*
			 * NULL terminate the command string and increment the
			 * address pointer to get to the first argument which
			 * is the address.
			 */
			*addrp++ = NULL;

			iocp->ioc_addr = do_strtoull(addrp, &xorpatp,
			    "address");
			msg(MSG_DEBUG3, "cmd arg addr=0x%llx\n",
			    iocp->ioc_addr);
			if (F_ADDR(iocp)) {
				msg(MSG_WARN, "main: an address/offset was "
				    "specified using the -a option and via the "
				    "<command>=<addr> format: ignoring the "
				    "address/offset from the -a option and "
				    "using addr=0x%llx\n", iocp->ioc_addr);
			}
			iocp->ioc_flags |= FLAGS_ADDR;

			/*
			 * Look for additional arguments separated by ","s.
			 */

			/*
			 * Second argument is xor pattern.
			 */
			if (*xorpatp++ != NULL) {
				iocp->ioc_xorpat = do_strtoull(xorpatp, &misc1p,
				    "xorpat");
				iocp->ioc_flags |= FLAGS_XORPAT;
				msg(MSG_DEBUG3, "cmd arg xorpat=0x%llx\n",
				    iocp->ioc_xorpat);
			}

			/*
			 * Third argument is miscellaneous data.
			 */
			if (F_XORPAT(iocp) && (misc1p != NULL) &&
			    (*misc1p++ != NULL)) {
				iocp->ioc_misc1 = do_strtoull(misc1p, &misc2p,
				    "misc1");
				iocp->ioc_flags |= FLAGS_MISC1;
				msg(MSG_DEBUG3, "cmd arg misc1=0x%llx\n",
				    iocp->ioc_misc1);
			}

			/*
			 * Fourth argument is also miscellaneous data.
			 */
			if (F_MISC1(iocp) && (misc2p != NULL) &&
			    (*misc2p++ != NULL)) {
				iocp->ioc_misc2 = do_strtoull(misc2p, NULL,
				    "misc2");
				iocp->ioc_flags |= FLAGS_MISC2;
				msg(MSG_DEBUG3, "cmd arg misc2=0x%llx\n",
				    iocp->ioc_misc2);
			}
		}

		/*
		 * Search thru the command arrays looking for a match.
		 */
		found = 0;
		msg(MSG_DEBUG4, "cmd=%s\n", argp);
		for (cmdpp = mdatap->m_cmdpp; *cmdpp != NULL && !found;
		    cmdpp++) {
			msg(MSG_DEBUG4, "cmdpp=0x%p\n", cmdpp);
			for (cmdp = *cmdpp; cmdp->c_name != NULL; cmdp++) {
				msg(MSG_DEBUG4, "cmd=%s (0x%llx)\n",
				    cmdp->c_name, cmdp->c_command);
				if (cmdcmp(argp, cmdp->c_name) == 0) {
					mdatap->m_cmdp = cmdp;
					found++;
					break;
				}
			}
		}

		/*
		 * If the command was not found, display an error
		 * message and usage message then exit.
		 */
		if (!found) {
			msg(MSG_ERROR, "unrecognized command \"%s\"\n", argp);
			usage(mdatap, USAGE_EXIT, 1);
			/* NOT REACHED */
		}

		/*
		 * Initialize command info in ioc structure.
		 */
		iocp->ioc_command = cmdp->c_command;
		(void) strncpy(iocp->ioc_cmdstr, argp,
		    sizeof (iocp->ioc_cmdstr));

		/*
		 * Check if command is not implemented/supported.
		 */
		if (ERR_MISC_ISNOTIMP(iocp->ioc_command) ||
		    ERR_MISC_ISNOTSUP(iocp->ioc_command)) {
			msg(MSG_ERROR, "command is not implemented or "
			    "not supported: %s\n", iocp->ioc_cmdstr);
			continue;
		}

		/*
		 * Check that the address argument was specified for
		 * any of the commands that require one. Otherwise
		 * if an address argument was specified it must
		 * refer to a memory binding that was requested.
		 */
		if (ERR_MISC_ISPHYS(iocp->ioc_command) ||
		    ERR_MISC_ISREAL(iocp->ioc_command) ||
		    ERR_MISC_ISVIRT(iocp->ioc_command) ||
		    ERR_MISC_ISPEEK(iocp->ioc_command) ||
		    ERR_MISC_ISPOKE(iocp->ioc_command)) {
			if (!F_ADDR(iocp) && !F_CACHE_INDEX(iocp)) {
				msg(MSG_ERROR, "address argument is "
				    "required for command %s\n",
				    iocp->ioc_cmdstr);
				usage(mdatap, USAGE_EXIT, 1);
				/* NOT REACHED */
			}
		} else {
			if (F_ADDR(iocp) || F_CACHE_INDEX(iocp)) {
				iocp->ioc_flags |= FLAGS_BINDMEM;
				if ((iocp->ioc_addr & PAGEMASK) !=
				    iocp->ioc_addr) {
					msg(MSG_ERROR, "address argument "
					    "for memory-bound test must "
					    "be 8k page-aligned\n");
					continue;
				}
			}
		}

		/*
		 * Do some pre-test initialization.
		 */
		if (pre_test(mdatap) != 0) {
			errcnt++;
			msg(MSG_ERROR, "pre-test initialization failed\n");
			/*
			 * We may need to do some clean up.
			 */
			if (post_test(mdatap) != 0)
				msg(MSG_ERROR, "post-test cleanup failed\n");
			continue;
		}

		/*
		 * Call the test routine.
		 */
		if (do_cmd(mdatap) != 0) {
			errcnt++;
			msg(MSG_ERROR, "command failed: %s\n", argp);
		} else {
			msg(MSG_VERBOSE, "command completed: %s\n", argp);
		}

		/*
		 * Do some post-test cleanup.
		 */
		if (post_test(mdatap) != 0) {
			errcnt++;
			msg(MSG_ERROR, "post-test cleanup failed\n");
		}
	}

	/*
	 * Decrement the usage count in config file.
	 */
	if (fini_config() != 0)
		errcnt++;

	msg(MSG_DEBUG1, "exiting with errcnt=%d\n", errcnt);

	return (errcnt);
}

/*
 * ******************************************************************
 * The following block of routines are the high level test routines.
 * Note that these should all be in alphabetical order.
 * ******************************************************************
 */

static int
do_cmd(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;
	cmd_t	*cmdp = mdatap->m_cmdp;
	int	ret;
	char	*fname = "do_cmd";

	msg(MSG_VERBOSE, "%s: executing command: %s (0x%llx)\n",
	    fname, cmdp->c_name, cmdp->c_command);

	msg(MSG_VERBOSE, "%s: xorpat=0x%llx, c_offset=0x%x, a_offset=0x%x\n",
	    fname, iocp->ioc_xorpat, get_c_offset(iocp),
	    get_a_offset(iocp));

	/*
	 * Display some debug info.
	 */
	if (user_debug > 0) {
		msg(MSG_DEBUG1, "%s: xorpat=0x%llx, c_offset=0x%x, "
		    "a_offset=0x%x\n",
		    fname, iocp->ioc_xorpat, get_c_offset(iocp),
		    get_a_offset(iocp));
		if (F_ADDR(iocp))
			msg(MSG_DEBUG1, "%s: supplied addr=0x%llx\n",
			    fname, iocp->ioc_addr);
		if (F_MISC1(iocp))
			msg(MSG_DEBUG1, "%s: misc1=0x%llx\n",
			    fname, iocp->ioc_misc1);
		if (F_MISC2(iocp))
			msg(MSG_DEBUG1, "%s: misc2=0x%llx\n",
			    fname, iocp->ioc_misc2);
	}

	/*
	 * Give messages some time to get out.
	 */
	if (Sync() != 0)
		msg(MSG_WARN, "%s: Sync: failed\n", fname);

	ret = (*cmdp->c_func)(mdatap);

	return (ret);
}

/*
 * Enable specific error registers from the user command line.
 */
int
do_enable_err(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;

	/*
	 * Send ioctl to enable the specified error types.
	 */
	if (do_ioctl(MEMTEST_ENABLE_ERRORS, iocp, "do_enable_err") == -1) {
		msg(MSG_ERROR, "do_enable_err: ioctl failed");
		return (-1);
	}

	return (0);
}

/*
 * Flush specific processor caches from the user command line.
 */
int
do_flush_cache(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;

	/*
	 * Send ioctl to flush specified cache.
	 */
	if (do_ioctl(MEMTEST_FLUSH_CACHE, iocp, "do_cache_flush") == -1) {
		msg(MSG_ERROR, "do_cache_flush: ioctl failed");
		return (-1);
	}

	return (0);
}

/*
 * IO error test.
 */
int
do_io_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	caddr_t		vaddr;

	/*
	 * For I/O DMA errors the 8k memory page where the error is injected
	 * will need to be marked dirty to ensure that msync() will write it
	 * to the I/O device.  An address to use for this purpose is
	 * calculated to ensure that it does not inadvertently trip the
	 * injected error.  The address calculation is simple:  If the address
	 * to be used to access the error falls within the lower half of the
	 * page, then set vaddr to the middle of the upper half of the page.
	 * Otherwise, set vaddr to the beginning of the page.
	 */
	if (get_a_offset(iocp) < (PAGESIZE / 2)) {
		vaddr = (caddr_t)(uintptr_t)iocp->ioc_bufbase +
		    ((PAGESIZE / 2) + (PAGESIZE / 4));
	} else {
		vaddr = (caddr_t)(uintptr_t)iocp->ioc_databuf;
	}

	/*
	 * Inject the error.
	 */
	if (do_ioctl(MEMTEST_INJECT_ERROR, iocp, "do_io_err") == -1) {
		msg(MSG_ERROR, "do_io_err: ioctl failed");
		return (-1);
	}

	/*
	 * Cause the page to be marked dirty.
	 */
	*vaddr = *vaddr;

	/*
	 * Return now if we don't want to invoke the error.
	 */
	if (F_NOERR(iocp)) {
		msg(MSG_DEBUG2, "do_io_err: not invoking error\n");
		return (0);
	}

	/*
	 * Invoke the error.
	 */
	if (msync((caddr_t)(uintptr_t)iocp->ioc_bufbase, iocp->ioc_bufsize,
	    MS_SYNC) == -1) {
		if (ERR_PROT_ISCE(iocp->ioc_command)) {
			msg(MSG_ERROR, "do_io_err: msync %s failed", iofile);
			return (-1);
		}
	}

	return (0);
}

/*
 * Generalized kernel error test routine.
 */
int
do_k_err(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;

	/*
	 * Inject and invoke the error.
	 */
	if (do_ioctl(MEMTEST_INJECT_ERROR, iocp, "do_k_err") == -1) {
		msg(MSG_ERROR, "do_k_err: ioctl failed");
		return (-1);
	}

	return (0);
}

/*
 * This routine converts a string to an uint64_t integer. If an error
 * occurs, it displays an error message and a short usage message and
 * exits with 1.
 */
static uint64_t
do_strtoull(const char *opt_str, char **endptr, char *msg_str)
{
	uint64_t retval = 0;

	if ((opt_str == NULL) || (strlen(opt_str) == 0)) {
		msg(MSG_WARN, "%s is invalid\n", msg_str);
		usage(NULL, USAGE_EXIT, 1);
		/* NOT REACHED */
	}

	errno = 0;
	retval = (uint64_t)strtoull(opt_str, endptr, 0);
	if ((errno == ERANGE) || (errno == EINVAL)) {
		msg(MSG_WARN, "%s is invalid: %s\n", msg_str, opt_str);
		usage(NULL, USAGE_EXIT, 1);
		/* NOT REACHED */
	}

	return (retval);
}

static int
mem_is_local(cpu_info_t *cip, uint64_t paddr)
{
	if (cpu_get_type(cip) == CPUTYPE_RFALLS) {
		return (kt_mem_is_local(cip, paddr));
	} else if (cpu_get_type(cip) == CPUTYPE_VFALLS) {
		return (vf_mem_is_local(cip, paddr));
	} else if ((paddr >= cip->c_mem_start) &&
	    (paddr < cip->c_mem_start + cip->c_mem_size)) {
		return (1);
	} else {
		return (0);
	}
}

/*
 * The two routines below work together to produce an error that is
 * injected by one CPU and then invoked by another CPU.
 *
 * The function mtst_producer() is the producer thread and creates
 * an error in its cache or local memory.
 *
 * The function do_u_cp_err() is the consumer thread and accesses
 * the corrupted data.
 *
 * The function wait_sync() is used by the producer and consumer threads
 * to wait for the thread synchronization variable to become an expected
 * value. It returns 0 on success, otherwise it returns -1.
 *
 * The main thread was chosen to be the consumer rather than the
 * producer so that we can handle the interrupt/longjump properly.
 *
 * A variable is used to synchronize code execution between producer
 * and consumer threads and has the following values/meanings.
 *
 *	0	This is the initial value.
 *
 *	1	Prod:	Waits for this value before injecting the error.
 *		Cons:	Sets this value which tells the producer to go
 *			ahead and inject the error.
 *
 *	2	Prod:	Sets this value after injecting the error to tell
 *			the consumer that it can invoke the error.
 *		Con:	Waits for this value before invoking the error.
 *
 *	3	Prod:	Waits for this value after injecting the error
 *			and before exiting the thread.
 *		Con:	Sets this value after invoking the error to tell
 *			the producer that it may exit.
 *
 *	-1	This value may be set by either the producer or consumer
 *		and indicates that some sort of error has occurred. Both
 *		producer and consumer should abort the test immediately.
 */

void *
mtst_producer(void *ptr)
{
	mdata_t		*mdatap = ptr;
	ioc_t		*iocp = mdatap->m_iocp;
	volatile int	*syncp = mdatap->m_syncp;
	int		cpu = mdatap->m_cip->c_cpuid;
	int		stat = 0;

	/*
	 * Bind this thread to a processor.
	 */
	if (processor_bind(P_LWPID, P_MYID, cpu, NULL) == -1) {
		msg(MSG_ERROR, "mtst_producer: processor_bind() failed\n");
		*syncp = -1;
		stat = -1;
		thr_exit(&stat);
	}

	msg(MSG_DEBUG3, "mtst_producer: bound to cpu %d\n", cpu);

	/*
	 * Wait for OK to inject the error, but don't wait forever.
	 */
	if (wait_sync(mdatap, 1, SYNC_WAIT_MAX, "mtst_producer") !=
	    SYNC_STATUS_OK) {
		*syncp = -1;
		stat = -1;
		thr_exit(&stat);
	}

	msg(MSG_DEBUG3, "mtst_producer: injecting the error\n");

	/*
	 * Inject the error.
	 */
	if (do_ioctl(MEMTEST_INJECT_ERROR, iocp, "mtst_producer") == -1) {
		msg(MSG_ERROR, "mtst_producer: ioctl failed on cpu %d", cpu);
		*syncp = -1;
		stat = -1;
		thr_exit(&stat);
	}

	msg(MSG_DEBUG3, "mtst_producer: waiting for consumer to "
	    "invoke the error\n");

	/*
	 * For write-back to foreign memory tests, the producer thread
	 * is the one to invoke the error.
	 */
	if (ERR_CLASS_ISL2WB(iocp->ioc_command)) {
		OP_FLUSHALL_L2(mdatap);
	}

	/*
	 * Tell the consumer thread that we've injected the error.
	 */
	*syncp = 2;

	/*
	 * Wait for consumer to invoke the error, but don't wait forever.
	 */
	if (wait_sync(mdatap, 3, SYNC_WAIT_MAX, "mtst_producer") !=
	    SYNC_STATUS_OK) {
		*syncp = -1;
		stat = -1;
	}

	msg(MSG_DEBUG3, "mtst_producer: exiting with stat=0x%x\n", stat);

	thr_exit(&stat);

	return (0);
}

/*
 * User consumer/producer test.
 */
int
do_u_cp_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	thread_t	producer_thread;
	mdata_t		*consumer_mdatap, *producer_mdatap;
	int		consumer_cpu, producer_cpu;
	int		sync;
	caddr_t		vaddr_2access = (caddr_t)(uintptr_t)iocp->ioc_addr;

	/*
	 * Sanity check.
	 */
	if (iocp->ioc_nthreads != 2) {
		msg(MSG_ERROR, "do_u_cp_err: nthreads=%d should be 2\n",
		    iocp->ioc_nthreads);
		return (-1);
	}

	consumer_mdatap = mdatap_array[0];
	producer_mdatap = mdatap_array[1];
	consumer_mdatap->m_syncp = &sync;
	producer_mdatap->m_syncp = &sync;
	consumer_cpu = consumer_mdatap->m_cip->c_cpuid;
	producer_cpu = producer_mdatap->m_cip->c_cpuid;

	msg(MSG_VERBOSE, "consumer thread bound to CPU %d\n", consumer_cpu);
	msg(MSG_VERBOSE, "producer thread bound to CPU %d\n", producer_cpu);

	/*
	 * For I/O DMA errors the 8k memory page where the error is injected
	 * will need to be marked dirty to ensure that msync() will write it
	 * to the I/O device.  An address to use for this purpose is
	 * calculated to ensure that it does not inadvertently trip the
	 * injected error.
	 */
	if (ERR_MODE(iocp->ioc_command) == ERR_MODE_UDMA) {
		if (get_a_offset(iocp) < (PAGESIZE / 2)) {
			vaddr_2access += ((PAGESIZE / 2) + (PAGESIZE / 4));
		}
		/*
		 * Else don't change vaddr2access if access offset is >=
		 * PAGESIZE / 2.
		 */
	} else {
		vaddr_2access += get_a_offset(iocp);
	}

	/*
	 * Print addresses and contents of data buffer(s) to console.
	 */
	print_user_mode_buf_info(mdatap);

	/*
	 * Access the data and/or instructions to minimize the chances of
	 * unwanted MMU miss traps occuring when the error is invoked.
	 */
	mdatap->m_asmld(vaddr_2access);

	/*
	 * Create the producer thread.
	 */
	if (thr_create(NULL, NULL, mtst_producer, producer_mdatap, THR_BOUND,
	    &producer_thread) != 0) {
		msg(MSG_ERROR, "do_u_cp_err: thr_create() failed");
		return (-1);
	}

	/*
	 * Set the producer thread loose to inject the error.
	 */
	sync = 1;

	/*
	 * Wait for the producer thread to inject the error,
	 * but don't wait forever.
	 */
	if (wait_sync(consumer_mdatap, 2, SYNC_WAIT_MAX, "do_u_cp_err") !=
	    SYNC_STATUS_OK) {
		sync = -1;
		(void) thr_join(producer_thread, 0, 0);
		return (-1);
	}

	msg(MSG_DEBUG3, "consumer: invoking the error at vaddr=0x%p\n",
	    vaddr_2access);

	/*
	 * Invoke the error and store a value to the sync variable
	 * indicating that we've invoked the error.
	 */
	if (!F_NOERR(iocp)) {
		if (ERR_MODE(iocp->ioc_command) == ERR_MODE_UDMA) {
			/*
			 * Cause the page to be marked dirty.
			 */
			*vaddr_2access = *vaddr_2access;

			if (msync((caddr_t)(uintptr_t)iocp->ioc_bufbase,
			    iocp->ioc_bufsize, MS_SYNC) == -1) {
				if (ERR_PROT_ISCE(iocp->ioc_command)) {
					msg(MSG_ERROR,
					    "do_u_cp_err: msync %s failed",
					    iofile);
				}
			}
		} else if (!ERR_CLASS_ISL2WB(iocp->ioc_command)) {
			mdatap->m_asmldst(vaddr_2access, (caddr_t)&sync, 3);
		}

		sync = 3;

		/*
		 * Delay a little to force the error to occur here.
		 */
		usecdelay(mdatap, 10);
	} else {
		sync = 3;
	}

	(void) thr_join(producer_thread, 0, 0);

	return (0);
}

/*
 * Unimplemented/Unsupported errors.
 */
int
do_notimp(mdata_t *mdatap)
{
	ioc_t	*iocp = mdatap->m_iocp;

	msg(MSG_ERROR, "no test routine defined for command: %s\n",
	    iocp->ioc_cmdstr);
	return (-1);
}

/*
 * Generalized user error test routine.
 */
int
do_u_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		err_acc = ERR_ACC(iocp->ioc_command);
	caddr_t		uva_2access;
	char		*fname = "do_u_err";

	/*
	 * If the error should be on instruction fetch then set the
	 * corruption address to be in the assembly load routine.
	 */
	if (err_acc == ERR_ACC_FETCH)
		iocp->ioc_addr = (uint64_t)(uintptr_t)mdatap->m_asmld;

	uva_2access = (caddr_t)(uintptr_t)iocp->ioc_addr + get_a_offset(iocp);

	/*
	 * Print addresses and contents of data buffer(s) to console.
	 */
	print_user_mode_buf_info(mdatap);

	/*
	 * Access the data and/or instructions to minimize the chances of
	 * unwanted MMU miss traps occuring when the error is invoked.
	 */
	if (err_acc == ERR_ACC_BLOAD)
		mdatap->m_blkld(uva_2access);
	else
		mdatap->m_asmld(uva_2access);

	/*
	 * Inject the error.
	 */
	if (do_ioctl(MEMTEST_INJECT_ERROR, iocp, fname) == -1) {
		msg(MSG_ERROR, "%s: ioctl failed", fname);
		return (-1);
	}

	/*
	 * Return now if we don't want to invoke the error.
	 */
	if (F_NOERR(iocp)) {
		msg(MSG_DEBUG2, "%s: not invoking error\n", fname);
		return (0);
	}

	/*
	 * Invoke the error.
	 */
	switch (err_acc) {
	case ERR_ACC_BLOAD:
		mdatap->m_blkld(uva_2access);
		break;
	case ERR_ACC_STORE:
		/*
		 * This store should get merged with the corrupted
		 * data injected above and cause a store merge error.
		 */
		*uva_2access = (uchar_t)0xff;
		break;
	default:
		mdatap->m_asmld(uva_2access);
	}

	/*
	 * Delay a little to force the error to occur here.
	 */
	usecdelay(mdatap, 10);
	return (0);
}

/*
 * User data write-back test.
 */
int
do_ud_wb_err(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;

	/*
	 * Print addresses and contents of data buffer(s) to console.
	 */
	print_user_mode_buf_info(mdatap);

	/*
	 * Inject the error.
	 */
	if (do_ioctl(MEMTEST_INJECT_ERROR, iocp, "do_ud_wb_err") == -1) {
		msg(MSG_ERROR, "do_ud_wb_err: ioctl failed");
		return (-1);
	}

	/*
	 * Return now if we don't want to invoke the error.
	 */
	if (F_NOERR(iocp)) {
		msg(MSG_DEBUG2, "do_ud_wb_err: not invoking error\n");
		return (0);
	}

	/*
	 * Invoke the write-back error.
	 *
	 * NOTE: sun4v user mode L2 instruction WB errors are not supported
	 */
	OP_FLUSHALL_L2(mdatap);

	return (0);
}

/*
 * *********************************************************
 * The following block of routines are all the miscellaneous
 * support routines.  Note that these should all be in
 * alphabetical order.
 * *********************************************************
 */

/*
 * This routine is used to flush output and sync file IO.
 * It is called after each command.
 */
static int
Sync()
{
	if (fflush(stdout) == EOF) {
		msg(MSG_WARN, "Sync: couldn't fflush stdout");
		return (-1);
	}
	if (fflush(stderr) == EOF) {
		msg(MSG_WARN, "Sync: couldn't fflush stderr");
		return (-1);
	}
	if (fsync(fileno(stdout)) == -1) {
		msg(MSG_WARN, "Sync: couldn't fsync stdout");
		return (-1);
	}
	if (fsync(fileno(stderr)) == -1) {
		msg(MSG_WARN, "Sync: couldn't fsync stderr");
		return (-1);
	}
	sync();
	(void) sleep(2);

	return (0);
}

/*
 * This function allocates the data buffer used for injecting and
 * invoking errors in both user land and in the kernel. The kernel
 * uses the data buffer so that it can be easily freed and the
 * system can try to clean up kernel errors.
 */
static caddr_t
alloc_databuf(mdata_t *mdatap, int bufsize)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cpu_info_t	*cip = mdatap->m_cip;
	mem_req_t	mr;
	static	caddr_t	vaddr;
	uint64_t	paddr, raddr;
	uint64_t	*llptr, paddr_mask, data;
	int		i, fd;
	int		pid = getpid();
	char		*fname = "alloc_databuf";

	msg(MSG_DEBUG2, "%s(mdatap=0x%p, bufsize=0x%x)\n",
	    fname, mdatap, bufsize);

	/*
	 * If this is an IO (DMA read) test, memory map a file.
	 */
	if ((ERR_MODE(iocp->ioc_command) == ERR_MODE_DMA) ||
	    (ERR_MODE(iocp->ioc_command) == ERR_MODE_UDMA)) {
		/*
		 * If no iofile was specified, create a temporary one.
		 */
		if (!iofile)
			iofile = tmpnam(NULL);
		(void) unlink(iofile);

		/*
		 * Open the IO file.
		 */
		if ((fd = open(iofile, O_RDWR|O_CREAT|O_TRUNC, 0666)) == -1) {
			msg(MSG_ERROR, "%s: couldn't open %s",
			    fname, iofile);
			return (NULL);
		}

		if (unlink(iofile) == -1) {
			msg(MSG_ERROR, "%s: couldn't unlink %s",
			    fname, iofile);
			(void) close(fd);
			return (NULL);
		}

		if (ftruncate(fd, iocp->ioc_bufsize) == -1) {
			msg(MSG_ERROR, "%s: couldn't extend %s",
			    fname, iofile);
			(void) close(fd);
			return (NULL);
		}

		if (fsync(fd) == -1) {
			msg(MSG_ERROR, "%s: couldn't fsync %s",
			    fname, iofile);
			(void) close(fd);
			return (NULL);
		}

		/*
		 * Memory map the IO file.
		 */
		if ((vaddr = (void *)mmap(0, iocp->ioc_bufsize,
		    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED,
		    fd, 0)) == MAP_FAILED) {
			msg(MSG_ERROR, "%s: couldn't mmap %s",
			    fname, iofile);
			(void) close(fd);
			return (NULL);
		}

		if ((paddr = uva_to_pa((uint64_t)(uintptr_t)vaddr, pid)) == -1)
			return (NULL);

		if (close(fd) == -1) {
			msg(MSG_ERROR, "%s: couldn't close %s",
			    fname, iofile);
			return (NULL);
		}
	/*
	 * Else if there was a physical address specified then
	 * set up a mapping to it for the data buffer.
	 *
	 * Note that on sun4v systems the address inpar is a real
	 * address since that is all the kernel knows about.
	 */
	} else if (F_ADDR(iocp) && F_BINDMEM(iocp)) {
		paddr = raddr = IOC_ADDR(iocp);

		if (sun4v_flag == TRUE) {
			if ((paddr = ra_to_pa(raddr)) == -1) {
				return (NULL);
			}
		}

		msg(MSG_DEBUG2, "%s: allocating data "
		    "buffer using paddr/raddr=0x%llx\n", fname, raddr);

		vaddr = map_pa_to_va(raddr, bufsize);
		if (vaddr == (caddr_t)-1) {
			return (NULL);
		}

		/*
		 * Ensure that USER mode commands use the vaddr.
		 */
		if (ERR_MODE_ISUSER(iocp->ioc_command))
			iocp->ioc_addr = (uint64_t)(uintptr_t)vaddr;

	/*
	 * Else if there was a cache index specified the injector must
	 * find a physical address that will match to that index.
	 *
	 * We pass the request to the kernel which chooses a suitable
	 * paddr and returns both the paddr and its corresponding raddr
	 * in the mr struct then we proceed to map a uvaddr to it as usual
	 * (as if a paddr/raddr were specified by the user using -a <addr>).
	 *
	 * NOTE: the bufsize is ignored by the index_to_pa routine but is
	 *	 used below to map only bufsize within the found memory range.
	 */
	} else if (F_CACHE_INDEX(iocp)) {
		if (index_to_pa(mdatap, &mr, bufsize) == -1) {
			msg(MSG_ERROR, "%s: couldn't find physical "
			    "address for cache index=0x%llx\n",
			    fname, iocp->ioc_cache_idx);
			return (NULL);
		}

		/*
		 * The driver returns the paddr and the raddr on
		 * sun4v systems, for sun4u raddr = paddr.
		 */
		paddr = mr.m_paddr1;
		raddr = mr.m_paddr2;

		/*
		 * The ioc_addr struct member is overwritten with the physical
		 * address which corresponds to the specified cache index.
		 * This is done so that later code will pick up this value
		 * overriding the "-a" option (if it was specified).
		 */
		if (F_CACHE_INDEX(iocp) && F_ADDR(iocp)) {
			msg(MSG_WARN, "%s: both a cache index and "
			    "an address/offset were specified: ignoring "
			    "the address/offset from the -a option and "
			    "using paddr=0x%llx\n", fname, paddr);
		}

		mdatap->m_iocp->ioc_addr = raddr;
		msg(MSG_DEBUG2, "%s: allocating data "
		    "buffer for cache index=0x%llx with paddr=0x%llx, "
		    "raddr=0x%llx\n", fname,
		    iocp->ioc_cache_idx, paddr, raddr);

		vaddr = map_pa_to_va(raddr, bufsize);
		if (vaddr == (caddr_t)-1) {
			return (NULL);
		}

		/*
		 * Ensure that USER mode commands use the vaddr.
		 */
		if (ERR_MODE_ISUSER(iocp->ioc_command))
			iocp->ioc_addr = (uint64_t)(uintptr_t)vaddr;

	/*
	 * Else allocate a data buffer using /dev/zero.
	 */
	} else {
		msg(MSG_DEBUG2, "%s: allocating data "
		    "buffer using /dev/zero\n", fname);
		if ((fd = open("/dev/zero", O_RDWR, 0666)) == -1) {
			msg(MSG_ERROR, "%s: couldn't open "
			    "/dev/zero", fname);
			return (NULL);
		}
		if ((vaddr = mmap(0, bufsize,
		    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE, fd, 0))
		    == MAP_FAILED) {
			msg(MSG_ERROR, "%s: couldn't mmap "
			    "data buffer", fname);
			return (NULL);
		}

		if ((paddr = uva_to_pa((uint64_t)(uintptr_t)vaddr, pid)) == -1)
			return (NULL);

		if (sun4v_flag == TRUE) {
			if ((paddr = ra_to_pa(paddr)) == -1) {
				return (NULL);
			}
		}

		if (close(fd) == -1) {
			msg(MSG_ERROR, "%s: couldn't close "
			    "/dev/zero\n", fname);
			return (NULL);
		}
	}

	/*
	 * Lock the pages in memory to avoid them being paged out.
	 *
	 * Note that mlock() does not prevent pages from being
	 * relocated in memory.  A page must be exclusive-locked to
	 * to avoid this and this cannot be done from userland.
	 * This locking is done in the memtest driver.
	 *
	 * Also note that mappings via the /dev/physmem driver
	 * remain resident and do not need to be mlock'd.
	 */
	if (!F_BINDMEM(iocp)) {
		if (mlock(vaddr, bufsize) != 0) {
			msg(MSG_ERROR, "%s: mlock() failed", fname);
			return (NULL);
		} else {
			msg(MSG_DEBUG2, "%s: data buffer "
			    "locked using mlock()\n", fname);
		}
	}

	msg(MSG_DEBUG2, "%s: mmapped vaddr=0x%p, paddr=0x%llx, "
	    "bufsize=%d\n", fname, vaddr, paddr, bufsize);

	/*
	 * Initialize the data buffer to a debug friendly data pattern.
	 *
	 * Note that for sun4u-based processors injecting memory CEs
	 * requires the same data pattern throughout the cache line
	 * otherwise unwanted UEs occur.
	 */
	llptr = (void *)vaddr;
	paddr_mask = 0xffffffff & ~(cip->c_l2_sublinesize - 1);

	if ((ERR_MODE(iocp->ioc_command) == ERR_MODE_DMA) ||
	    (ERR_MODE(iocp->ioc_command) == ERR_MODE_UDMA)) {
		data = 0x0eccf11e00000000ULL;
	} else {
		data = 0x0eccf00d00000000ULL;
	}

	for (i = 0; i < (bufsize / sizeof (uint64_t)); i++,
	    paddr += sizeof (uint64_t))
		llptr[i] = data | (paddr & paddr_mask);

	if (user_debug > 3) {
		for (i = 0; i < 16; i++) {
			msg(MSG_DEBUG0, "%s: data[0x%p] = 0x%llx\n",
			    fname, llptr + i, llptr[i]);
		}
	}

	return (vaddr);
}

/*
 * This routine is used by the ASSERT macro.  It displays information
 * about the assertion and causes the program to exit.
 */
int
assfail(char *asstr, char *file, int line)
{
	msg(MSG_FATAL, "assertion failed: %s, file: %s, line: %d\n",
	    asstr, file, line);
	/* NOTREACHED */
	return (0);
}

/*
 * This routine binds a thread to its cpu.
 *
 * Return values are:
 *	 0	bind was successful
 *	-1	error
 */
static int
bind_thread(mdata_t *mdatap)
{
	int	threadno = mdatap->m_threadno;
	int	cpu = mdatap->m_cip->c_cpuid;
	char	str[40];

	(void) snprintf(str, sizeof (str), "bind_thread[thr=%d,cpu=%d]",
	    threadno, cpu);

	/*
	 * If already bound, then first unbind.
	 */
	if (mdatap->m_bound != 0) {
		if (unbind_thread(mdatap) != 0) {
			msg(MSG_ERROR, "%s: failed to unbind thread\n", str);
			return (-1);
		}
	}

	/*
	 * Bind the thread to its cpu.
	 */
	if (processor_bind(P_LWPID, P_MYID, cpu, NULL) == -1) {
		msg(MSG_ERROR, "%s: bind failed", str);
		return (-1);
	}
	msg(MSG_DEBUG3, "%s: bind succeeded\n", str);
	mdatap->m_bound = 1;
	return (0);
}

/*
 * This routine is used to calibrate the global delay variable used
 * by the delay() routine. It sets and returns the global variable
 * that indicates the number of loop iterations required to delay
 * for one microsecond.
 */
static uint64_t
calibrate_usecdelay(void)
{
	struct timeval		tp_before, tp_after, tp_overhead;
	uint64_t		usec_before, usec_after, usec_overhead;
	int			i;
	register uint64_t	loops = calibrate_loops;
	uint64_t		loops_per_usec;

	msg(MSG_DEBUG3, "calibrate_usecdelay(): loops=0x%llx\n", loops);

	/*
	 * Get the current time.
	 */
	if (gettimeofday(&tp_before, NULL) == -1) {
		msg(MSG_ERROR, "calibrate_usecdelay: "
		    "gettimeofday() failed");
		return (0);
	}

	for (i = 0; i < loops; i++)
		usecloopcnt++;

	/*
	 * See how long it took for the loop to execute.
	 */
	if (gettimeofday(&tp_after, NULL) == -1) {
		msg(MSG_ERROR, "calibrate_usecdelay: "
		    "gettimeofday() failed");
		return (0);
	}

	/*
	 * See how much the overhead for gettimeofday() is.
	 */
	if (gettimeofday(&tp_overhead, NULL) == -1) {
		msg(MSG_ERROR, "calibrate_usecdelay: "
		    "gettimeofday() failed");
		return (0);
	}

	usec_before = (tp_before.tv_sec * 1000000) + tp_before.tv_usec;
	usec_after = (tp_after.tv_sec * 1000000) + tp_after.tv_usec;
	usec_overhead = (tp_overhead.tv_sec * 1000000) + tp_overhead.tv_usec -
	    usec_after;

	/*
	 * Account for the system call overhead.
	 */
	usec_after -= usec_overhead;

	/*
	 * Something is not right if this is the case.
	 */
	if (usec_before >= usec_after) {
		msg(MSG_ERROR, "calibrate_usecdelay: "
		    "time is going backwards.\n");
		return (0);
	}

	loops_per_usec = calibrate_loops / (usec_after - usec_before);

	if (loops_per_usec == 0) {
		msg(MSG_DEBUG0, "loops_per_usec=0x%llx too small, "
		    "setting it to 1\n", loops_per_usec);
		loops_per_usec = 1;
	}

	msg(MSG_DEBUG3, "calibrate_usecdelay: usec_delta=0x%llx, "
	    "loops_per_usec=0x%llx\n",
	    (usec_after - usec_before), loops_per_usec);

	return (loops_per_usec);
}

/*
 * This routine sets up signal catching.
 */
static int
catchsigs(void)
{
	struct sigaction action;

	msg(MSG_DEBUG3, "catchsigs()\n");

	action.sa_sigaction = handler;
	action.sa_flags = SA_SIGINFO;

	if (sigemptyset(&action.sa_mask) == -1) {
		msg(MSG_ERROR, "catchsigs: sigemptyset() failed");
		return (-1);
	}

	if (sigaction(SIGBUS, &action, NULL) == -1) {
		msg(MSG_ERROR, "catchsigs: sigaction() failed");
		return (-1);
	}

	return (0);
}

/*
 * This function selects an appropriate cpu for the thread
 * based on user specified bindings, options, or test defaults.
 */
int
choose_thr_cpu(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	system_info_t	*sip = mdatap->m_sip;
	cpu_info_t	*cip;
	uint64_t	uvaddr;
	uint64_t	kvaddr;
	uint64_t	paddr;
	int		i, cpuid, criteria;
	int		found = 0;
	int		threadno = mdatap->m_threadno;
	char		str[40];

	criteria = iocp->ioc_bind_thr_criteria[threadno];

	/*
	 * If the bind criteria is a memory binding then figure
	 * out the physical address of the memory to bind to.
	 * Note that memory (DRAM) injections are automatically
	 * set to THREAD_BIND_LOCAL_MEM so the injection will work.
	 *
	 * Note that this routine is called multiple times, once
	 * from init() and one time for each thread from init_threads().
	 * At the time of the first call the command info has not been
	 * filled in yet so this routine can't key on the command attributes.
	 */
	if (((criteria == THREAD_BIND_LOCAL_MEM) ||
	    (criteria == THREAD_BIND_REMOTE_MEM)) &&
	    iocp->ioc_command != 0) {
		switch (ERR_MODE(iocp->ioc_command)) {
		case ERR_MODE_NONE:
			if (ERR_MISC_ISPHYS(iocp->ioc_command)) {
				paddr = iocp->ioc_addr;
			} else if (ERR_MISC_ISREAL(iocp->ioc_command)) {
				paddr = iocp->ioc_addr;
				if (sun4v_flag == TRUE) {
					if ((paddr = ra_to_pa(paddr)) == -1) {
						return (-1);
					}
				}
			} else if (ERR_MISC_ISVIRT(iocp->ioc_command)) {
				kvaddr = iocp->ioc_addr;

				if ((paddr = kva_to_pa(kvaddr)) == -1) {
					return (-1);
				}

				if (sun4v_flag == TRUE) {
					if ((paddr = ra_to_pa(paddr)) == -1) {
						return (-1);
					}
				}
			} else {
				msg(MSG_ABORT, "choose_thr_cpu: "
				    "no mode case ERR_MISC=0x%x "
				    "must be \"low impact\" mode\n",
				    ERR_MISC(IOC_COMMAND(iocp)));
				/* NOT REACHED */
			}
			break;
		case ERR_MODE_HYPR:
		case ERR_MODE_KERN:
		case ERR_MODE_OBP:
		case ERR_MODE_DMA:
		case ERR_MODE_UDMA:
			if (ERR_MISC_ISVIRT(iocp->ioc_command)) {
				msg(MSG_ABORT, "choose_thr_cpu: "
				    "non-no_mode memory binding to kernel "
				    "virtual address is not supported\n");
				/* NOT REACHED */
			} else if (ERR_ACC_ISFETCH(iocp->ioc_command)) {
				uvaddr = (uint64_t)(uintptr_t)mdatap->m_instbuf;
			} else {
				uvaddr = (uint64_t)(uintptr_t)mdatap->m_databuf;
			}

			if ((paddr = uva_to_pa(uvaddr, iocp->ioc_pid)) == -1) {
				return (-1);
			}

			if (sun4v_flag == TRUE) {
				if ((paddr = ra_to_pa(paddr)) == -1) {
					return (-1);
				}
			}
			break;
		case ERR_MODE_USER:
			if (ERR_MISC_ISVIRT(iocp->ioc_command)) {
				uvaddr = iocp->ioc_addr;

				if (F_MISC1(iocp) && (iocp->ioc_misc1)) {
					IOC_FLAGS(iocp) |= FLAGS_PID;
					iocp->ioc_pid = iocp->ioc_misc1;

					/*
					 * Clear the MISC1 info in case a
					 * processor specific routine uses it.
					 */
					IOC_FLAGS(iocp) &=
					    ~((uint64_t)FLAGS_MISC1);
					iocp->ioc_misc1 = NULL;
				}
			} else if (ERR_ACC_ISFETCH(iocp->ioc_command)) {
				uvaddr = (uint64_t)(uintptr_t)mdatap->m_instbuf;
			} else {
				uvaddr = (uint64_t)(uintptr_t)mdatap->m_databuf;
			}

			if ((paddr = uva_to_pa(uvaddr, iocp->ioc_pid)) == -1) {
				return (-1);
			}

			if (sun4v_flag == TRUE) {
				if ((paddr = ra_to_pa(paddr)) == -1) {
					return (-1);
				}
			}
			break;
		default:
			msg(MSG_ABORT, "choose_thr_cpu: invalid "
			    "ERR_MODE=0x%x\n",
			    ERR_MODE(iocp->ioc_command));
			/* NOT REACHED */
		}
	}

	msg(MSG_DEBUG3, "choose_thr_cpu: threadno=%d, criteria=%s\n",
	    threadno, bind_subopts[criteria]);

	/*
	 * If this thread is already bound to a cpu then retain the
	 * binding as long as no binding criteria were specified.
	 */
	if ((mdatap->m_bound == 1) && (criteria == THREAD_BIND_DEFAULT)) {
		msg(MSG_DEBUG3, "choose_thr_cpu: thread=%d retained cpu=%d\n",
		    threadno, mdatap->m_cip->c_cpuid);

		/*
		 * This will be needed in the driver to bind threads
		 * based on the selections made here.
		 */
		iocp->ioc_thr2cpu_binding[threadno] = mdatap->m_cip->c_cpuid;
		return (0);
	}

	/*
	 * Search thru the cpu list looking for one that meets
	 * the required criteria.
	 */
	for (i = 0; (i < sip->s_ncpus_online) && !found; i++) {
		cip = (cip_arrayp + i);
		(void) snprintf(str, sizeof (str),
		    "choose_thr_cpu[t=%d,i=%d,c=%d]",
		    threadno, i, cip->c_cpuid);

		/*
		 * If this cpu has already been chosen then skip it,
		 * otherwise get the memory information for it.
		 */
		if (cip->c_already_chosen != 0) {
			msg(MSG_DEBUG3, "%s: cpu is already chosen, "
			    "skipping\n", str);
			continue;
		}

		msg(MSG_DEBUG3, "%s: evaluating cpu: i=%d, cpu=%d, "
		    "mem_start=0x%llx, mem_size=0x%llx\n", str, i,
		    cip->c_cpuid, cip->c_mem_start, cip->c_mem_size);

		switch (criteria) {
		case THREAD_BIND_DEFAULT:
			/*
			 * Simply return the next cpu on the available list
			 * (lowest available cpuid).
			 */
			found++;
			break;
		case THREAD_BIND_CPUID:
			/*
			 * See if this cpu matches the required cpuid.
			 */
			if (cip->c_cpuid == iocp->ioc_bind_thr_data[threadno]) {
				found++;
			}
			break;
		case THREAD_BIND_CPUTYPE:
			/*
			 * See if this cpu matches the required type.
			 */
			if (cpu_get_type(cip) ==
			    iocp->ioc_bind_thr_data[threadno]) {
				found++;
			}
			break;
		case THREAD_BIND_LOCAL_MEM:
			/*
			 * If there are no local/remote memory error
			 * injection restrictions then return found,
			 * otherwise see if this is local memory.
			 */
			if (iocp->ioc_command != 0) {
				if ((cip->c_mem_flags & MEMFLAGS_LOCAL) == 0) {
					found++;
				}
				if (mem_is_local(cip, paddr) == 1) {
					found++;
				}
			} else {
				/*
				 * If the command has not been filled in yet
				 * then temporarily choose the current cpuid.
				 */
				cpuid = cip->c_cpuid;
			}
			break;
		case THREAD_BIND_REMOTE_MEM:
			/*
			 * If there are no local/remote memory error
			 * injection restrictions then return found,
			 * otherwise see if this is remote memory.
			 */
			if (iocp->ioc_command != 0) {
				if ((cip->c_mem_flags & MEMFLAGS_LOCAL) == 0) {
					found++;
				}
				if (mem_is_local(cip, paddr) == 0) {
					found++;
				}
			} else {
				/*
				 * If the command has not been filled in yet
				 * then temporarily choose the current cpuid.
				 */
				cpuid = cip->c_cpuid;
			}
			break;
		default:
			msg(MSG_FATAL, "%s: unsupported bind criteria %d\n",
			    str, criteria);
			/* NOT REACHED */
			break;
		}

		if (found) {
			/*
			 * Indicate that this cpu has been chosen and
			 * should not be chosen again.
			 */
			cip->c_already_chosen = 1;
			cpuid = cip->c_cpuid;
		}
	}

	if ((found == 0) && (iocp->ioc_command != 0)) {
		msg(MSG_ERROR, "choose_thr_cpu: could not find a cpu "
		    "for thread %d matching required criteria!\n", threadno);
		return (-1);
	}

	msg(MSG_DEBUG3, "choose_thr_cpu: thread=%d chose cpu=%d\n",
	    threadno, cpuid);

	mdatap->m_cip = cip;

	/*
	 * This will be needed in the driver to bind threads
	 * based on the selections made here.
	 */
	iocp->ioc_thr2cpu_binding[threadno] = cpuid;

	return (0);
}

/*
 * This routine does a string comparison like strcmp(3C) but
 * stops comparing if it sees a '=' so it can be used to
 * match command strings that may have a '=' in them.
 */
int
cmdcmp(char *str, char *cmd)
{
	if (str == cmd)
		return (0);

	while (*str == *cmd) {
		if (*str == '\0')
			return (0);
		str++;
		cmd++;
		if ((*str == '\0') && (*cmd == '='))
			return (0);
	}
	return (*(unsigned char *)str - *(unsigned char *)cmd);
}

/*
 * This routine returns the CPU type for a given cpu_info_t.
 */
static int
cpu_get_type(cpu_info_t *cip)
{
	switch (CPU_IMPL(cip->c_cpuver)) {
		case SPITFIRE_IMPL:
			return (CPUTYPE_SPITFIRE);
		case BLACKBIRD_IMPL:
			return (CPUTYPE_BLACKBIRD);
		case SABRE_IMPL:
			return (CPUTYPE_SABRE);
		case HUMMBRD_IMPL:
			return (CPUTYPE_HUMMBRD);
		case CHEETAH_IMPL:
			return (CPUTYPE_CHEETAH);
		case CHEETAH_PLUS_IMPL:
			return (CPUTYPE_CHEETAH_PLUS);
		case PANTHER_IMPL:
			return (CPUTYPE_PANTHER);
		case JALAPENO_IMPL:
			return (CPUTYPE_JALAPENO);
		case JAGUAR_IMPL:
			return (CPUTYPE_JAGUAR);
		case SERRANO_IMPL:
			return (CPUTYPE_SERRANO);
		case NIAGARA_IMPL:
			return (CPUTYPE_NIAGARA);
		case NIAGARA2_IMPL:
			if (CPU_ISVFALLS(cip)) {
				return (CPUTYPE_VFALLS);
			} else {
				return (CPUTYPE_NIAGARA2);
			}
		case RFALLS_IMPL:
			return (CPUTYPE_RFALLS);
		case OLYMPUS_C_IMPL:
			return (CPUTYPE_OLYMPUS_C);
		case JUPITER_IMPL:
			return (CPUTYPE_JUPITER);
		default:
			return (CPUTYPE_UNKNOWN);
	}
}

/*
 * This routine calls the cpu specific initialization routine
 * which in turn fills in some pointers in the mdata_t struct.
 */
static int
cpu_init(mdata_t *mdatap)
{
	cpu_info_t	*cip = mdatap->m_cip;

	msg(MSG_DEBUG3, "cpu_init: cpu_ver=0x%llx, cpu_impl=0x%llx\n",
	    cip->c_cpuver, CPU_IMPL(cip->c_cpuver));

	if ((cip)->c_cpuver == 0) {
		msg(MSG_ABORT, "cpu_init: NULL cpu_impl\n");
		/* NOT REACHED */
	}

	/*
	 * Call processor specific routine to initialize command list.
	 *
	 * NOTE: this list is in chronological order of the projects
	 *	 except for the OPL processors which are at the end.
	 */
	switch (cpu_get_type(cip)) {
		case CPUTYPE_SPITFIRE:
			sf_init(mdatap);
			break;
		case CPUTYPE_BLACKBIRD:
			sf_init(mdatap);
			break;
		case CPUTYPE_SABRE:
			sf_init(mdatap);
			break;
		case CPUTYPE_HUMMBRD:
			sf_init(mdatap);
			break;
		case CPUTYPE_CHEETAH:
			ch_init(mdatap);
			break;
		case CPUTYPE_CHEETAH_PLUS:
			chp_init(mdatap);
			break;
		case CPUTYPE_PANTHER:
			pn_init(mdatap);
			break;
		case CPUTYPE_JALAPENO:
			ja_init(mdatap);
			break;
		case CPUTYPE_JAGUAR:
			jg_init(mdatap);
			break;
		case CPUTYPE_SERRANO:
			sr_init(mdatap);
			break;
		case CPUTYPE_NIAGARA:
			ni_init(mdatap);
			break;
		case CPUTYPE_NIAGARA2:
			n2_init(mdatap);
			break;
		case CPUTYPE_VFALLS:
			vf_init(mdatap);
			break;
		case CPUTYPE_RFALLS:
			kt_init(mdatap);
			break;
		case CPUTYPE_OLYMPUS_C:
		case CPUTYPE_JUPITER:
			oc_init(mdatap);
			break;
		default:
			msg(MSG_ERROR, "cpu_init: unsupported CPU type, "
			    "impl=0x%llx ver=0x%llx\n",
			    CPU_IMPL(cip->c_cpuver), cip->c_cpuver);
			return (-1);
	}

	/*
	 * Check that the ops vector and command list got filled in.
	 */
	if ((mdatap->m_opvp == NULL) || (mdatap->m_cmdpp == NULL)) {
		msg(MSG_ERROR, "cpu_init: main data structure "
		    "failed to initialize properly!\n");
		return (-1);
	}

	return (0);
}

/*
 * This routine returns a cpu type value which corresponds to
 * the name string passed in.
 */
static int
cpu_name_to_type(char *name)
{
	cpu_type_t	*p;
	int		i;

	for (i = 0, p = cpu_table; p->c_arg != NULL; i++, p++) {
		if ((strcmp(name, p->c_arg) == 0) ||
		    (strcmp(name, p->c_name) == 0)) {
			return (i);
		}
	}

	return (CPUTYPE_UNKNOWN);
}

/*
 * This routine returns a pointer to a CPU name string
 * which corresponds to the type value passed in.
 */
static caddr_t
cpu_type_to_name(int type)
{
	return (cpu_table[type].c_name);
}

/*
 * This routine dumps the cpu_info_t structure passed in.
 */
static void
dump_cpu_info(cpu_info_t *cip)
{
	char	*cpuname, str[80];

	(void) snprintf(str, sizeof (str), "cpu%d", cip->c_cpuid);

	cpuname = cpu_type_to_name(cpu_get_type(cip));
	msg(MSG_VERBOSE, "%s: CPU: name=%s\n", str, cpuname);
	msg(MSG_VERBOSE, "%s: CPU: ver=0x%llx "
	    "(impl=0x%llx, mask=0x%llx, mfg=0x%llx)\n",
	    str, cip->c_cpuver, CPU_IMPL(cip->c_cpuver),
	    CPU_MASK(cip->c_cpuver), CPU_MFG(cip->c_cpuver));
	if (CPU_IMPL(cip->c_cpuver) == JAGUAR_IMPL) {
		msg(MSG_VERBOSE,
		    "%s: CPU: ecr=0x%llx, secr=0x%llx, eer=0x%llx, "
		    "dcr=0x%llx, dcucr=0x%llx\n", str, cip->c_ecr,
		    cip->c_secr, cip->c_eer, cip->c_dcr, cip->c_dcucr);
	} else {
		msg(MSG_VERBOSE, "%s: CPU: ecr=0x%llx, eer=0x%llx, "
		    "dcr=0x%llx, dcucr=0x%llx\n", str, cip->c_ecr,
		    cip->c_eer, cip->c_dcr, cip->c_dcucr);
	}
	msg(MSG_VERBOSE, "%s: D$: size=0x%x, linesize=0x%x, "
	    "associativity=%d\n",
	    str, cip->c_dc_size, cip->c_dc_linesize, cip->c_dc_assoc);
	msg(MSG_VERBOSE, "%s: I$: size=0x%x, linesize=0x%x, "
	    "associativity=%d\n",
	    str, cip->c_ic_size, cip->c_ic_linesize, cip->c_ic_assoc);
	msg(MSG_VERBOSE, "%s: L2$: size=0x%x, linesize=0x%x, "
	    "sublinesize=0x%x\n", str, cip->c_l2_size,
	    cip->c_l2_linesize, cip->c_l2_sublinesize);
	msg(MSG_VERBOSE, "%s: L2$: associativity=%d, flushsize=0x%x\n",
	    str, cip->c_l2_assoc, cip->c_l2_flushsize);

	if (CPU_IMPL(cip->c_cpuver) == PANTHER_IMPL) {
		msg(MSG_VERBOSE, "%s: L3$: size=0x%x, linesize=0x%x, "
		    "sublinesize=0x%x\n", str, cip->c_l3_size,
		    cip->c_l3_linesize, cip->c_l3_sublinesize);
		msg(MSG_VERBOSE, "%s: L3$: associativity=%d, flushsize=0x%x\n",
		    str, cip->c_l3_assoc, cip->c_l3_flushsize);
	}

	msg(MSG_VERBOSE, "%s: Mem: flags=0x%llx, start=0x%llx, size=0x%llx\n",
	    str, cip->c_mem_flags, cip->c_mem_start, cip->c_mem_size);
}

/*
 * This function executes an ioctl to the memtest driver.
 */
int
do_ioctl(int cmd, void *ptr, char *str)
{
	system_info_t	*sip;
	cpu_info_t	*cip;
	mem_req_t	*mr;
	config_file_t	*cfilep;
	ioc_t		*iocp;
	int		fd;

	switch (cmd) {
	case MEMTEST_SETDEBUG:
		msg(MSG_DEBUG3, "do_ioctl: %s: SETDEBUG: debug=0x%x\n",
		    str, *(uint_t *)ptr);
		break;
	case MEMTEST_GETSYSINFO:
		sip = ptr;
		msg(MSG_DEBUG3, "do_ioctl: %s: GETSYSINFO: sip=0x%p\n",
		    str, sip);
		break;
	case MEMTEST_GETCPUINFO:
		cip = ptr;
		msg(MSG_DEBUG3, "do_ioctl: %s: GETCPUINFO: cip=0x%p\n",
		    str, cip);
		break;
	case MEMTEST_INJECT_ERROR:
		iocp = ptr;
		msg(MSG_DEBUG3, "do_ioctl: %s: INJECT_ERROR: iocp=0x%p\n",
		    str, iocp);
		break;
	case MEMTEST_MEMREQ:
		mr = ptr;
		if (mr->m_cmd > MREQ_MAX) {
			msg(MSG_ERROR, "do_ioctl: %s: MEMREQ: cmd=%d, "
			    "is out of range\n", str, mr->m_cmd);
			return (-1);
		}
		msg(MSG_DEBUG3, "do_ioctl: %s: MEMREQ: subcmd=%s, "
		    "vaddr=0x%llx, pid=%d\n",
		    str, m_subcmds[mr->m_cmd], mr->m_vaddr, mr->m_pid);
		msg(MSG_DEBUG3, "do_ioctl: %s: MEMREQ: paddr1=0x%llx, "
		    "paddr2=0x%llx, size=0x%llx, attr=0x%x\n",
		    str, mr->m_paddr1, mr->m_paddr2,
		    mr->m_size, mr->m_attr);
		break;
	case MEMTEST_SETKVARS:
		cfilep = ptr;
		msg(MSG_DEBUG3, "do_ioctl: %s: SETKVARS: cfilep=0x%p\n",
		    str, cfilep);
		break;
	/*
	 * NOTE: The enable and flush commands should be enhanced to take
	 *	 suboptions to be more useful. Also these can be made
	 *	 sub-commands of a single (expandable) MEMTEST_UTILITY ioctl.
	 */
	case MEMTEST_ENABLE_ERRORS:
		iocp = ptr;
		msg(MSG_DEBUG3, "do_ioctl: %s: ENABLE_ERRORS: iocp=0x%p\n",
		    str, iocp);
		break;
	case MEMTEST_FLUSH_CACHE:
		iocp = ptr;
		msg(MSG_DEBUG3, "do_ioctl: %s: FLUSH_CACHE: iocp=0x%p\n",
		    str, iocp);
		break;
	default:
		msg(MSG_FATAL, "do_ioctl: %s: invalid cmd=0x%x\n", str, cmd);
		/* NOT REACHED */
		break;
	}

	if ((fd = open(DRIVER_NODE, O_RDWR)) == -1) {
		msg(MSG_ERROR, "do_ioctl: %s: couldn't open memtest driver",
		    str);
		return (-1);
	}

	if (ioctl(fd, cmd, ptr) == -1) {
		(void) close(fd);
		return (-1);
	}

	if (close(fd) == -1) {
		msg(MSG_ERROR, "do_ioctl: %s: close failed", str);
		return (-1);
	}

	switch (cmd) {
	case MEMTEST_GETSYSINFO:
		/*
		 * Sanity check.
		 */
		ASSERT(sip->s_ncpus);
		ASSERT(sip->s_ncpus_online);
		ASSERT(sip->s_maxcpuid);
		break;
	case MEMTEST_GETCPUINFO:
		/*
		 * Sanity check.
		 * This may catch injector/kernel incompatibilities.
		 */
		ASSERT(cip->c_l2_size > 0);
		ASSERT(cip->c_l2_sublinesize == 64);
		ASSERT((cip->c_l2_size & 0xFFFF) == 0);
		break;
	case MEMTEST_SETKVARS:
		/*
		 * Sanity check.
		 */
		ASSERT(cfilep->kv_ce_debug < 0x8);
		ASSERT(cfilep->kv_ce_show_data < 0x8);
		ASSERT(cfilep->kv_ce_verbose_memory < 0x8);
		ASSERT(cfilep->kv_ce_verbose_other < 0x8);
		ASSERT(cfilep->kv_ue_debug < 0x8);
		ASSERT(cfilep->kv_aft_verbose < 0x8);
		break;
	}

	return (0);
}

/*
 * This function is used to decrement the usage count in the mtst
 * config file and remove the file if no other mtst process is running.
 */
static int
fini_config(void)
{
	int		cfgerror = 0;
	FILE		*fp;

	/*
	 * Only touch the config file if this process sucessfully incremented
	 * the config file count in init_config().
	 */
	if (config_file_success != 1)
		return (0);

	/*
	 * Open the config file to decrement count and get kvars.
	 */
	if ((fp = fopen(CFG_FNAME, "rb+")) == NULL) {
		msg(MSG_ERROR, "fini_config: unable to open config file: %s"
		    CFG_FNAME);
		return (-1);
	}

	/*
	 * Lock the config file.
	 */
	flock.l_whence	= 0;
	flock.l_start	= 0;
	flock.l_len	= 0;		/* entire file */
	flock.l_pid	= getpid();
	flock.l_type	= F_WRLCK;
	if (fcntl(fileno(fp), F_SETLKW, &flock) == -1) {
		msg(MSG_ERROR, "fini_config: unable to aquire lock on "
		    "config file: %s", CFG_FNAME);
		(void) fclose(fp);
		return (-1);
	}

	/*
	 * Read the existing config file contents into struct.
	 */
	if ((fread(cfilep, sizeof (config_file_t), 1, fp)) != 1) {
		msg(MSG_ERROR, "fini_config: unable to read config file: %s",
		    CFG_FNAME);
		cfgerror = -1;
	}

	/*
	 * Decrement usage count and update config file.
	 */
	cfilep->count--;
	if (((int)cfilep->count <= 0) && (cfilep->saved == B_TRUE) &&
	    (cfgerror == 0)) {
		/*
		 * Restore the kernel vars to those in config file.
		 */
		cfilep->rw = B_TRUE;
		if (do_ioctl(MEMTEST_SETKVARS, cfilep, "fini_config") != 0) {
			msg(MSG_ERROR, "fini_config: SETKVARS ioctl failed");
			cfgerror = -1;
		}
	}

	/*
	 * Update the config file access count.
	 */
	if (cfgerror == 0) {
		rewind(fp);
		if ((fwrite(cfilep, sizeof (config_file_t), 1, fp)) != 1) {
			msg(MSG_ERROR, "fini_config: unable to write "
			    "config file: %s", CFG_FNAME);
			cfgerror = -1;
		}

		msg(MSG_DEBUG1, "fini_config: decremented instance count in "
		    "config file %s to %d\n", CFG_FNAME, cfilep->count);
	}

	/*
	 * If zero references remain remove the config file.
	 */
	if (((int)cfilep->count <= 0) && (cfgerror == 0)) {
		msg(MSG_DEBUG1, "fini_config: removing config file %s\n",
		    CFG_FNAME);
		if (remove(CFG_FNAME) == -1) {
			msg(MSG_ERROR, "fini_config: unable to remove "
			    "config file: %s", CFG_FNAME);
			cfgerror = -1;
		}
	}

	/*
	 * Unlock the config file AFTER we have potentially removed it.
	 */
	flock.l_type = F_UNLCK;
	if (fcntl(fileno(fp), F_SETLKW, &flock) == -1) {
		msg(MSG_ERROR, "fini_config: unable to release lock on "
		    "config file: %s", CFG_FNAME);
		cfgerror = -1;
	}

	(void) fclose(fp);
	return (cfgerror);
}

/*
 * Displacement flush the entire L2-cache.
 */
int
gen_flushall_l2(mdata_t *mdatap)
{
	uint_t	l2_linesize = mdatap->m_cip->c_l2_linesize;
	uint_t	l2_sublinesize = mdatap->m_cip->c_l2_sublinesize;
	int	l2_flushsize = (int)mdatap->m_cip->c_l2_flushsize;
	int	i;
	caddr_t	displace = mdatap->m_displace;

	msg(MSG_DEBUG3, "gen_flushall_l2(): l2_flushsize=0x%x, "
	    "l2_lsize=0x%x, l2_sublsize=0x%x, displace=0x%p\n",
	    l2_flushsize, l2_linesize, l2_sublinesize, displace);

	ASSERT(l2_flushsize > 0);
	ASSERT(l2_sublinesize == 64);

	if (!displace) {
		msg(MSG_ABORT, "gen_flushall_l2: displacement area has "
		    "not been initialized!\n");
		/* NOT REACHED */
	}

	/*
	 * Flush region may need to be larger then the cache to
	 * deal with page coloring. The system info structure
	 * specifies the minimum flush size required.
	 */
	for (i = 0; i < l2_flushsize; i += l2_sublinesize)
		asmld_quick((caddr_t)(displace + i));

	return (0);
}

/*
 * This routine returns the access offset for a particular command.
 */
int
get_a_offset(ioc_t *iocp)
{
	int	ret;

	if (F_A_OFFSET(iocp))
		ret = iocp->ioc_a_offset;
	else
		ret = 0;

	msg(MSG_DEBUG3, "get_a_offset: returning offset=0x%x\n", ret);

	return (ret);
}

/*
 * This routine returns the corruption offset for a particular command.
 */
int
get_c_offset(ioc_t *iocp)
{
	int	ret;

	if (F_C_OFFSET(iocp))
		ret = iocp->ioc_c_offset;
	else
		ret = 0;

	msg(MSG_DEBUG3, "get_c_offset: returning offset=0x%x\n", ret);

	return (ret);
}

/*
 * This routine returns the XOR pattern that will be used for corruption.
 */
static uint64_t
get_xorpat(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cmd_t		*cmdp = mdatap->m_cmdp;
	uint64_t	default_xorpat, xorpat, mask;
	uint32_t	iter = 0;
	int		shift, shift_1;
	int		bit_count;

	/*
	 * Select the data or check bit mask
	 */
	mask = ((F_CHKBIT(iocp) ? cmdp->c_c_mask : cmdp->c_d_mask));

	/*
	 * Get the default xorpat for the command.
	 */
	default_xorpat = ((F_CHKBIT(iocp) ?
	    cmdp->c_c_xorpat : cmdp->c_d_xorpat));

	/*
	 * If xor pattern is already set, then just use it.
	 */
	if (F_XORPAT(iocp))
		xorpat = iocp->ioc_xorpat;
	/*
	 * If the random flag is set then select bits at random
	 * from the valid ranges. Otherwise just return the default
	 * pattern.
	 */
	else if (F_RANDOM(iocp) && (default_xorpat != NULL)) {
		srand((unsigned int)time(NULL));
		shift = shift_1 = (rand() % 64);

		/*
		 * Select single or multiple random bits from within
		 * the valid range.
		 */
		while ((((mask >> shift) & 1) == 0) && (iter < max_attempts)) {
			shift = (rand() % 64);
			iter++;
		}

		xorpat = (1ULL << shift);

		/*
		 * If UE we search for a second bit to set which
		 * is different from the first.
		 */
		if (ERR_PROT_ISUE(iocp->ioc_command)) {
			for (iter = 0; iter < max_attempts; iter++) {
				if ((((mask >> shift_1) & 1) == 1) &&
				    (shift_1 != shift)) {
					break;
				}
				shift_1 = (rand() % 64);
				iter++;
			}

			xorpat |= (1ULL << shift_1);
		}

		if (iter == max_attempts) {
			msg(MSG_WARN, "get_xorpat: unable to generate random"
			    " xor pattern using default value\n");
			xorpat = ((F_CHKBIT(iocp) ?
			    cmdp->c_c_xorpat : cmdp->c_d_xorpat));
		}
	/*
	 * Otherwise, return the default pattern.
	 */
	} else {
		/*
		 * If default_xorpat is zero, don't bother with sanity check.
		 */
		if ((xorpat = default_xorpat) == 0)
			return (xorpat);
	}

	/*
	 * Prevent illegal xor patterns by anding with mask.
	 */
	xorpat = xorpat & mask;

	/*
	 * Check for a suspicious xor pattern and display
	 * a warning message if necessary.
	 */
	bit_count = popc64(xorpat);
	msg(MSG_DEBUG3, "get_xorpat: default_xorpat=0x%llx, xorpat=0x%llx, "
	    "bit_count=%d\n", default_xorpat, xorpat, bit_count);
	if ((bit_count == 0) && (default_xorpat != 0)) {
		msg(MSG_WARN, "get_xorpat: no bits set in xorpat\n");
	} else {
		switch (ERR_PROT(iocp->ioc_command)) {
		case ERR_PROT_PE:
			if ((bit_count & 1) == 0) {
				msg(MSG_WARN, "get_xorpat: even number "
				    "of bits set in xorpat=0x%llx "
				    "for parity case\n", xorpat);
			}
			break;
		case ERR_PROT_CE:
			if (bit_count > 1) {
				/*
				 * There are valid CE err types in sun4v
				 * which can have more than one bit set.
				 */
				msg(MSG_DEBUG1, "get_xorpat: more than one bit "
				    "set in xorpat=0x%llx for ce case\n",
				    xorpat);
			}
			break;
		case ERR_PROT_UE:
			if (bit_count == 1) {
				/*
				 * There are valid UE err types in sun4v
				 * which can have one bit set.
				 */
				msg(MSG_DEBUG1, "get_xorpat: only one bit set "
				    "in xorpat=0x%llx for ue case\n",
				    xorpat);
			}
			break;
		default:
			break;
		}
	}

	return (xorpat);
}

/*
 * This routine catches signals and does a siglongjmp() back to the
 * main command processing loop so we can continue to execute any
 * remaining commands.
 */
/*ARGSUSED*/
static void
handler(int sig, siginfo_t *si, void *ucontext)
{
	/*
	 * Display some signal information.
	 */
	if (verbose) {
		char	sigstr[SIG2STR_MAX];

		msg(MSG_INFO, "handler: caught ");
		if (sig2str(sig, sigstr))
			msg(MSG_INFO, "UNRECOGNIZED signal: %d\n", sig);
		else
			msg(MSG_INFO, "signal: %s\n", sigstr);
	}

	/*
	 * Restore the environment if it was previously saved.
	 */
	if (envp) {
		siglongjmp(env, sig);
		msg(MSG_FATAL, "handler: shouldn't get here after "
		    "longjmp\n");
		/* NOT REACHED */
	} else {
		msg(MSG_FATAL, "handler: environ pointer not initialized\n");
		/* NOT REACHED */
	}
}

/*
 * Perform an ioctl to determine a suitable physical address for a
 * supplied cache index and type of command (to determine cache attributes).
 */
/*ARGSUSED*/
static uint64_t
index_to_pa(mdata_t *mdatap, mem_req_t *mr, int bufsize)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		cache_level;
	char		*fname = "index_to_pa";

	msg(MSG_DEBUG2, "%s: mdatap=0x%p, index=0x%llx, way=0x%lx\n",
	    fname, mdatap, mdatap->m_iocp->ioc_cache_idx);

	/*
	 * Determine the cache type in order to request the correct
	 * physical address range (determined from the command's CLASS).
	 */
	cache_level = ERR_CLASS(mdatap->m_iocp->ioc_command);

	/*
	 * Fill in the mem_req struct to send to driver.
	 */
	bzero(mr, sizeof (mem_req_t));

	mr->m_cmd = MREQ_IDX_TO_PA;
	mr->m_subcmd = cache_level;
	mr->m_index = mdatap->m_iocp->ioc_cache_idx;
	mr->m_pid = getpid();

	/*
	 * The way option is required on sun4v systems but not sun4u
	 * systems.  The way is not used on sun4u to find the buffer
	 * but only later when performing the processor specific
	 * injection.
	 */
	if ((sun4v_flag == TRUE) && !F_CACHE_WAY(iocp)) {
		msg(MSG_ERROR, "%s: both a target index and a target "
		    "way are required on sun4v systems due to index "
		    "hashing\n", fname);
		return ((uint64_t)-1);
	} else {
		mr->m_way = mdatap->m_iocp->ioc_cache_way;
	}

	/*
	 * Call down to the driver to find the matching physical address.
	 */
	if (do_ioctl(MEMTEST_MEMREQ, mr, "index_to_pa") == -1) {
		msg(MSG_DEBUG2, "%s: ioctl failed", fname);
		return (-1);
	}

	/*
	 * The address we want of the returned physical range is returned
	 * in mr->paddr1, but it may not be page aligned.  So it is aligned
	 * here and any offset into the page stored in the mdata struct
	 * corruption and access offset members.
	 *
	 * Note that for sun4v the real address is returned in mr->paddr2,
	 * the offset within the page is assumed to be the same as the paddr.
	 * On sun4u mr->m_paddr1 = mr->m_paddr2 = paddr.
	 */
	iocp->ioc_c_offset += (mr->m_paddr1 & ((uint64_t)PAGESIZE - 1));
	iocp->ioc_a_offset += (mr->m_paddr1 & ((uint64_t)PAGESIZE - 1));
	mr->m_paddr1 = (mr->m_paddr1 & ~((uint64_t)PAGESIZE - 1));
	mr->m_paddr2 = (mr->m_paddr2 & ~((uint64_t)PAGESIZE - 1));

	msg(MSG_DEBUG2, "%s: driver returned paddr_aligned=0x%llx, "
	    "raddr_aligned=0x%llx, ioc_c_offset=0x%x, ioc_a_offset=0x%x\n",
	    fname, mr->m_paddr1, mr->m_paddr2,
	    iocp->ioc_c_offset, iocp->ioc_a_offset);

	return (mr->m_paddr1);
}

/*
 * This routine does some general initialization.
 * It returns 0 on success, and -1 on failure.
 */
static int
init(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	system_info_t	*sip = mdatap->m_sip;
	cpu_info_t	*cip;
	uint64_t	templ;
	int		i, tmp, obind;

	msg(MSG_DEBUG2, "init(mdatap=0x%p)\n", mdatap);

	/*
	 * We need to check for this because the driver may have
	 * been installed via a BFU rather than a package.
	 */
	if ((access(DRIVER_NODE, F_OK) == -1) &&
	    ((access(DRIVER_U_CONF, F_OK) == 0) ||
	    (access(DRIVER_V_CONF, F_OK) == 0))) {
		/*
		 * Try to add the driver.
		 */
		if (system("add_drv -m '* 0600 bin bin' "
		    "memtest > /dev/null") == 0) {
			sync();
			msg(MSG_VERBOSE, "Added memtest driver to system.\n");
		} else {
			/*
			 * It is possible for the add_drv to fail because
			 * the node is missing even thou the driver was
			 * already added. In this case removing and re-adding
			 * the driver may work.
			 */
			(void) system("rem_drv memtest > /dev/null");
			if (system("add_drv -m '* 0600 bin bin' "
			    "memtest > /dev/null") == 0) {
				sync();
				msg(MSG_VERBOSE, "Removed and added memtest "
				    "driver to system.\n");
			} else {
				msg(MSG_ERROR, "init: add_drv failed");
				return (-1);
			}
		}
	}

	/*
	 * Enable DEBUG messages and code in driver if requested.
	 *
	 * Note this will override settings the driver has even if
	 * multiple mtsts are running.
	 */
	if (do_ioctl(MEMTEST_SETDEBUG, &kern_debug, "init") != 0) {
		msg(MSG_ERROR, "init: ioctl to set debug level failed");
		return (-1);
	}

	/*
	 * Initialize global system info data struct.
	 */
	if (do_ioctl(MEMTEST_GETSYSINFO, sip, "init") != 0) {
		msg(MSG_ERROR, "init: ioctl to get system info failed");
		return (-1);
	}

	if (F_VERBOSE(iocp)) {
		msg(MSG_VERBOSE, "#CPUs=%d, #CPUs_online=%d, max_CPUid=%d\n",
		    sip->s_ncpus, sip->s_ncpus_online, sip->s_maxcpuid);
	}

	/*
	 * Open the mtst config file to increment the count, if it
	 * does not exist create and initialize the file.
	 */
	if (init_config() != 0) {
		msg(MSG_ERROR, "init: config file init/increment failed\n");
		return (-1);
	}

	/*
	 * Save kernel vars since they may be overwritten and will
	 * need to be restored later.
	 */
	if (save_kvars() != 0)
		msg(MSG_WARN, "init: unable to save kernel vars\n");

	/*
	 * Sanity check.
	 * Check that the nuber of online CPUs returned by the sysconf()
	 * call equals the number returned by the ioctl above.
	 */
	tmp = sysconf(_SC_NPROCESSORS_ONLN);
	if (sip->s_ncpus_online != tmp) {
		msg(MSG_ERROR, "init: (# online cpus from ioctl()=%d) != "
		    "(# online cpus from sysconf()=%d)!\n",
		    sip->s_ncpus_online, tmp);
		return (-1);
	}

	/*
	 * Create a list of cpus that we can use for thread binding
	 * and fill in each cpu's data structure.
	 */
	if (init_cpu_info(mdatap) != 0) {
		msg(MSG_ERROR, "init: couldn't init cpu info\n");
		return (-1);
	}

	/*
	 * Make sure the primary thread is bound to a processor.
	 * This is necessary in order to do the initial command lookup.
	 * If not already bound, then bind to a CPU that matches any
	 * binding criteria that may have been specified.
	 * Else find the corresponding cpu info struct for the cpu
	 * currently bound to.
	 */
	if (processor_bind(P_LWPID, P_MYID, PBIND_QUERY,
	    (processorid_t *)&obind) == -1) {
		msg(MSG_ERROR, "init: processor_bind() query failed");
		return (-1);
	}

	if (obind == PBIND_NONE) {
		/*
		 * Initialize the data buffer pointer so that
		 * choose_thr_cpu() will not fail.
		 */
		iocp->ioc_databuf = (uint64_t)(uintptr_t)(&templ);
		if (choose_thr_cpu(mdatap) == -1)
			return (-1);
		msg(MSG_DEBUG3, "init: not already bound, binding to cpu %d\n",
		    mdatap->m_cip->c_cpuid);
		if (bind_thread(mdatap) != 0) {
			return (-1);
		}
	} else {
		msg(MSG_DEBUG2, "init: already bound to cpu %d\n", obind);
		for (i = 0, cip = NULL; i < sip->s_ncpus_online; i++) {
			if ((cip_arrayp + i)->c_cpuid == obind)
				cip = (cip_arrayp + i);
		}
		if (cip == NULL) {
			msg(MSG_ERROR, "init: couldn't find cpu info "
			    "structure for cpu %d\n", obind);
			return (-1);
		}
		mdatap->m_bound = 1;
		mdatap->m_cip = cip;
	}

	/*
	 * Save process id in ioc struct.
	 */
	iocp->ioc_pid = getpid();

	/*
	 * Dump the info about the currently bound CPU here because tools
	 * (such as dossre) use an "mtst -v" command to determine the cpu
	 * type on which they are running.  Note that the injector thread(s)
	 * may later move to other CPUs based on specific test requirements.
	 */
	msg(MSG_VERBOSE, "init: injector initially bound to cpu %d with "
	    "the following attributes:\n", mdatap->m_cip->c_cpuid);
	dump_cpu_info(mdatap->m_cip);

	/*
	 * Call processor specific initialize routine for the
	 * CPU that we're currently running on.
	 */
	if (cpu_init(mdatap) != 0) {
		msg(MSG_ERROR, "init: failed to initialize processor "
		    "specific information\n");
		return (-1);
	}

	/*
	 * Enable signal catching if requested to do so.
	 */
	if (catch_signals && (catchsigs() != 0)) {
		msg(MSG_ERROR, "init: couldn't set up signal catcher\n");
		return (-1);
	}

	return (0);
}

/*
 * This function is used to open the mtst config file and increment the
 * mtst instance count, if the file does not exist it creates and initializes
 * the file.
 */
static int
init_config(void)
{
	size_t		filesize;
	mode_t		fchmod_mode;
	int		cfgerror = 0;
	FILE		*fp;

	if ((cfilep = calloc(1, sizeof (config_file_t))) == NULL) {
		msg(MSG_ERROR, "init_config: calloc() of config_file_t failed");
		return (-1);
	}

	errno = 0;
	if ((fp = fopen(CFG_FNAME, "rb+")) == NULL) {
		if ((errno) && (errno != ENOENT)) {
			msg(MSG_ERROR, "init_config: error opening config "
			    "file: %s", CFG_FNAME);
			return (-1);
		}
		msg(MSG_DEBUG1, "init_config: unable to open config "
		    "file: %s, creating...\n", CFG_FNAME);
		if ((fp = fopen(CFG_FNAME, "wb+")) == NULL) {
			msg(MSG_ERROR, "init_config: unable to create "
			    "config file: %s", CFG_FNAME);
			return (-1);
		}
	}

	/*
	 * Lock the config file before reads/writes.
	 */
	flock.l_whence	= 0;
	flock.l_start	= 0;
	flock.l_len	= 0;		/* entire file */
	flock.l_pid	= getpid();
	flock.l_type	= F_WRLCK;

	if (fcntl(fileno(fp), F_SETLKW, &flock) == -1) {
		msg(MSG_ERROR, "init_config: unable to aquire lock on "
		    "config file: %s", CFG_FNAME);
		(void) fclose(fp);
		return (-1);
	}

	/*
	 * Check size of config file to ensure we are first to create it.
	 */
	rewind(fp);
	errno = 0;
	filesize = fread(cfilep, sizeof (config_file_t), 1, fp);
	rewind(fp);

	switch (filesize) {
	case 0:
		/*
		 * Config file was just created, set perms and initialize it.
		 */
		if (errno) {
			msg(MSG_ERROR, "init_config: error reading config "
			    "file: %s", CFG_FNAME);
			cfgerror = -1;
			break;
		}

		cfilep->saved = B_FALSE;
		cfilep->rw = B_FALSE;

		fchmod_mode = S_IRUSR | S_IWUSR | S_IRGRP;
		if (chmod(CFG_FNAME, fchmod_mode) == -1) {
			msg(MSG_ERROR, "init_config: unable to change mode "
			    "on config file: %s", CFG_FNAME);
			cfgerror = -1;
			break;
		}

		if ((fwrite(cfilep, sizeof (config_file_t), 1, fp)) != 1) {
			msg(MSG_ERROR, "init_config: unable to write "
			    "to config file: %s", CFG_FNAME);
			cfgerror = -1;
		}
		rewind(fp);
		break;
	case 1:
		msg(MSG_DEBUG3, "init_config: using existing config "
		    "file: %s\n", CFG_FNAME);

		/*
		 * Read the existing config file contents into struct.
		 */
		if ((fread(cfilep, sizeof (config_file_t), 1, fp))
		    != 1) {
			msg(MSG_ERROR, "init_config: unable to read "
			    "config file: %s", CFG_FNAME);
			cfgerror = -1;
		}
		rewind(fp);
		break;
	default:
		msg(MSG_ERROR, "init_config: unsupported size %d for "
		    "config file: %s %d\n",
		    filesize * sizeof (config_file_t), CFG_FNAME);
		cfgerror = -1;
		break;
	}

	/*
	 * Increment config file usage count only if no error encountered.
	 */
	if (cfgerror == 0) {
		cfilep->count++;
		if ((fwrite(cfilep, sizeof (config_file_t), 1, fp)) != 1) {
			msg(MSG_ERROR, "init_config: unable to write "
			    "config file: %s", CFG_FNAME);
			cfgerror = -1;
		}
		config_file_success = 1;
		msg(MSG_DEBUG1, "init_config: incremented instance count in "
		    "config file %s to %d\n", CFG_FNAME, cfilep->count);
	}

	/*
	 * Unlock and close the config file.
	 */
	flock.l_type = F_UNLCK;
	if (fcntl(fileno(fp), F_SETLKW, &flock) == -1) {
		msg(MSG_ERROR, "init_config: unable to release lock on "
		    "config file: %s", CFG_FNAME);
		cfgerror = -1;
	}

	(void) fclose(fp);
	return (cfgerror);
}

/*
 * This function initializes the array that keeps track of
 * what cpus have already been chosen (bound) to threads.
 * This is needed to prevent thread(s) from choosing the
 * same CPU to bind to.
 */
static void
init_cpus_chosen_list(mdata_t *mdatap)
{
	system_info_t	*sip = mdatap->m_sip;
	int		i;

	msg(MSG_DEBUG3, "init_cpus_chosen_list()\n");

	for (i = 0; (i < sip->s_ncpus); i++)
		(cip_arrayp + i)->c_already_chosen = 0;
}

/*
 * This routine initializes the cpu info structures.
 */
static int
init_cpu_info(mdata_t *mdatap)
{
	system_info_t	*sip = mdatap->m_sip;
	int		i, found, stat;

	/*
	 * Allocate the cpu info structs based on the number
	 * of cpus in the system.
	 */
	cip_arrayp = malloc(sip->s_ncpus * sizeof (cpu_info_t));

	/*
	 * For each online cpu found, do an ioctl to fill in
	 * its data structure.
	 */
	for (i = 0, found = 0;
	    (found < sip->s_ncpus_online) && (i <= sip->s_maxcpuid); i++) {
		stat = p_online(i, P_STATUS);
		msg(MSG_DEBUG3, "init_cpu_info: i=%d, stat=%d\n", i, stat);
		if ((stat == P_ONLINE) || (stat == P_NOINTR)) {
			msg(MSG_DEBUG3, "init_cpu_info: cip_arrayp[%d]=%p\n",
			    found, (cip_arrayp + found));
			(cip_arrayp + found)->c_cpuid = i;
			msg(MSG_DEBUG3, "init_cpu_info: "
			    "cip_arrayp[%d]->c_cpuid=%d\n",
			    found, (cip_arrayp + found)->c_cpuid);
			/*
			 * Initialize cpu info data struct.
			 */
			if (do_ioctl(MEMTEST_GETCPUINFO, (cip_arrayp + found),
			    "init_cpu_info") != 0) {
				msg(MSG_ERROR, "init_cpu_info: ioctl to get "
				    "cpu info failed");
				return (-1);
			}
			found++;
		}
	}

	/*
	 * Make sure we found at least 1 CPU.
	 */
	if (found == 0) {
		msg(MSG_ERROR, "init_cpu_info: couldn't find any "
		    "online CPUs!?\n");
		return (-1);
	}

	/*
	 * Check that the number of cpus found is equal to
	 * the number of online CPUs.
	 */
	if (found != sip->s_ncpus_online) {
		msg(MSG_ERROR, "init_cpu_info: (# online cpus found = %d) "
		    "!= (# online cpus expected = %d)!\n",
		    found, sip->s_ncpus_online);
		return (-1);
	}

	return (0);
}

/*
 * This function initializes thread specific information.
 * The bindings for the thread have already been chosen.
 */
static int
init_thread(mdata_t *mdatap)
{
	system_info_t	*sip;
	cpu_info_t	*cip;
	uint64_t	*llptr, i, paddr, paddr_mask;
	int		threadno = mdatap->m_threadno;
	int		cpuid = mdatap->m_cip->c_cpuid;
	int		fd;
	int		temp_binding_flag = 0;
	char		str[40];

	sip = mdatap_array[threadno]->m_sip;
	cip = mdatap_array[threadno]->m_cip;

	(void) snprintf(str, sizeof (str), "init_thread[thr=%d,cpu=%d]",
	    threadno, cpuid);
	msg(MSG_DEBUG3, "%s: mdatap=0x%p, sip=0x%p\n", str, mdatap, sip);

	/*
	 * If this thread is not already bound, temporarily bind to the
	 * cpu specified in the mdata struct so that we can fill in
	 * information which requires that the thread be running on a
	 * particular CPU.
	 */
	if (mdatap->m_bound == 0) {
		if (bind_thread(mdatap) != 0) {
			msg(MSG_ERROR, "%s: failed to bind thread to cpu %d\n",
			    str, mdatap_array[threadno]->m_cip->c_cpuid);
			return (-1);
		}
		temp_binding_flag++;
	}

	/*
	 * Calibrate the delay variable used by the delay routine.
	 */
	if ((mdatap->m_usecloops = calibrate_usecdelay()) == 0) {
		msg(MSG_ERROR, "%s: couldn't calibrate delay factor\n", str);
		return (-1);
	}

	/*
	 * Unbind now.
	 */
	if (temp_binding_flag != 0) {
		msg(MSG_DEBUG3, "%s: unbinding the temporarily bound "
		    "thread\n", str);

		if (unbind_thread(mdatap) != 0) {
			msg(MSG_ERROR, "%s: failed to unbind thread\n", str);
			return (-1);
		}
	}

	/*
	 * Call processor specific initialize routine.
	 */
	if (cpu_init(mdatap) != 0) {
		msg(MSG_ERROR, "%s: failed to initialize processor "
		    "specific information", str);
		return (-1);
	}

	/*
	 * Allocate flush pool and initialize it.
	 */
	if ((fd = open("/dev/zero", O_RDWR, 0666)) == -1) {
		msg(MSG_ERROR, "%s: couldn't open /dev/zero", str);
		return (-1);
	}
	if ((displace = mmap(0, cip->c_l2_flushsize,
	    PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0))
	    == MAP_FAILED) {
		msg(MSG_ERROR, "%s: couldn't mmap flush region", str);
		return (-1);
	}
	if (close(fd) == -1) {
		msg(MSG_ERROR, "%s: couldn't close /dev/zero\n", str);
		return (-1);
	}

	mdatap->m_displace = displace;
	if ((paddr = uva_to_pa((uint64_t)(uintptr_t)displace, getpid())) == -1)
		return (-1);
	msg(MSG_DEBUG3, "%s: l2_flush_vaddr=0x%p, paddr=0x%llx, size=0x%x\n",
	    str, displace, paddr, cip->c_l2_flushsize);

	/*
	 * Initialize flush pool to a unique value also as a debug aid.
	 */
	llptr = (void *)displace;
	paddr_mask = 0xffffffff & ~(cip->c_l2_sublinesize - 1);
	for (i = 0; i < (cip->c_l2_flushsize / sizeof (*llptr));
	    i++, paddr += sizeof (*llptr))
		llptr[i] = 0x0eccbeef00000000ULL | (paddr & paddr_mask);

	return (0);
}

/*
 * This routine initializes the mdata_t data structure for each thread.
 * It expects to be called with the data pointer for thread 0.
 */
static int
init_threads(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	cmd_t		*cmdp, **cmdpp;
	int		i, found;

	msg(MSG_DEBUG2, "init_threads(mdatap=0x%p): nthreads=%d\n",
	    mdatap, iocp->ioc_nthreads);

	/*
	 * Sanity check.
	 */
	if (mdatap != mdatap_array[0]) {
		msg(MSG_ABORT, "init_threads: not called with pointer "
		    "to thread 0 data structure\n");
		/* NOT REACHED */
	}

	/*
	 * Initialize common info for secondary threads by
	 * copying it from the primary thread.
	 */
	for (i = 1; i < iocp->ioc_nthreads; i++) {
		mdatap_array[i]->m_asmld = mdatap->m_asmld;
		mdatap_array[i]->m_asmldst = mdatap->m_asmldst;
		mdatap_array[i]->m_blkld = mdatap->m_blkld;
		mdatap_array[i]->m_databuf = mdatap->m_databuf;
		mdatap_array[i]->m_instbuf = mdatap->m_instbuf;
	}

	init_cpus_chosen_list(mdatap);

	/*
	 * Initialize unique info for each thread.
	 */
	for (i = 0; i < iocp->ioc_nthreads; i++) {
		/*
		 * Choose the cpu binding for each thread;
		 */
		if (choose_thr_cpu(mdatap_array[i]) == -1) {
			msg(MSG_ERROR, "init_threads: could not find a cpu "
			    "for thread %d matching required criteria!\n", i);
			return (-1);
		}
		msg(MSG_DEBUG2, "init_threads: thread %d: cpu=%d, "
		    "&systeminfo=0x%p, syncp=0x%p\n",
		    i, mdatap_array[i]->m_cip->c_cpuid,
		    mdatap_array[i]->m_sip, mdatap_array[i]->m_syncp);
		if (init_thread(mdatap_array[i]) != 0) {
			msg(MSG_ERROR, "init_thread: failed for "
			    "thread %d!\n", i);
			return (-1);
		}
		/*
		 * Check that the command is supported on the thread.
		 * Search thru the command list looking for a match
		 * of the command we originally looked up.
		 */
		found = 0;
		msg(MSG_DEBUG3, "init_threads: looking up command=0x%llx\n",
		    iocp->ioc_command);
		for (cmdpp = mdatap_array[i]->m_cmdpp; *cmdpp != NULL && !found;
		    cmdpp++) {
			msg(MSG_DEBUG4, "init_threads: cmdpp=0x%p\n", cmdpp);
			for (cmdp = *cmdpp; cmdp->c_name != NULL; cmdp++) {
				msg(MSG_DEBUG4, "init_threads: cmd=%s "
				    "(0x%llx)\n", cmdp->c_name,
				    cmdp->c_command);
				if (cmdp->c_command == iocp->ioc_command) {
					msg(MSG_DEBUG4, "init_threads: "
					    "match found\n");
					found++;
					break;
				}
			}
		}
		if (!found) {
			msg(MSG_ERROR, "unsupported command \"%s\" on "
			    "thread %d\n", iocp->ioc_cmdstr, i);
			return (-1);
		}

		if (F_VERBOSE(iocp)) {
			msg(MSG_VERBOSE, "init_threads: injector thread %d "
			    "bound to cpu %d with the following attributes:\n",
			    i, mdatap_array[i]->m_cip->c_cpuid);
			dump_cpu_info(mdatap_array[i]->m_cip);
		}
	}

	return (0);
}

/*
 * This function translates a kernel virtual address
 * to a physical one via a call down to the driver.
 *
 * NOTE: for sun4v the physical address is actually a real address.
 */
uint64_t
kva_to_pa(uint64_t vaddr)
{
	mem_req_t	mr;
	char		*fname = "kva_to_pa";

	if (vaddr == NULL) {
		msg(MSG_ERROR, "%s: vaddr is NULL\n", fname);
		return (-1);
	}

	bzero(&mr, sizeof (mem_req_t));
	mr.m_cmd = MREQ_KVA_TO_PA;
	mr.m_vaddr = vaddr;
	if (do_ioctl(MEMTEST_MEMREQ, &mr, "kva_to_pa") == -1) {
		msg(MSG_ERROR, "%s: translation failed for "
		    "vaddr=0x%llx\n", fname, vaddr);
		return (-1);
	}

	msg(MSG_DEBUG2, "%s: kvaddr=0x%llx is mapped to paddr/raddr=0x%llx\n",
	    fname, vaddr, mr.m_paddr1);

	return (mr.m_paddr1);
}

/*
 * Generic message routine used to display/log all messages.
 */
void
msg(int msgtype, const char *format, ...)
{
	va_list alist;
	int	priority;
	int	err = errno;
	char	outbuf[256], tmpbuf[128], *prefix;

	switch (msgtype) {
	case MSG_DEBUG0:
		prefix = "debug0";
		priority = LOG_DEBUG;
		break;
	case MSG_DEBUG1:
		if (user_debug < 1)
			return;
		prefix = "debug1";
		priority = LOG_DEBUG;
		break;
	case MSG_DEBUG2:
		if (user_debug < 2)
			return;
		prefix = "debug2";
		priority = LOG_DEBUG;
		break;
	case MSG_DEBUG3:
		if (user_debug < 3)
			return;
		prefix = "debug3";
		priority = LOG_DEBUG;
		break;
	case MSG_DEBUG4:
		if (user_debug < 4)
			return;
		prefix = "debug4";
		priority = LOG_DEBUG;
		break;
	case MSG_INFO:
		prefix = "info";
		priority = LOG_INFO;
		break;
	case MSG_NOTICE:
		prefix = "notice";
		priority = LOG_NOTICE;
		break;
	case MSG_VERBOSE:
		if (!verbose)
			return;
		prefix = "info";
		priority = LOG_INFO;
		break;
	case MSG_WARN:
		prefix = "warning";
		priority = LOG_WARNING;
		break;
	case MSG_ERROR:
		prefix = "error";
		priority = LOG_ERR;
		break;
	case MSG_FATAL:
		prefix = "fatal";
		priority = LOG_ERR;
		break;
	case MSG_ABORT:
		prefix = "abort";
		priority = LOG_ERR;
		break;
	default:
		/*
		 * Display a warning message and generate a core dump.
		 */
		msg(MSG_ABORT, "msg: unknown msgtype=%d, format=%s\n",
		    msgtype, format);
		/* NOT REACHED */
		break;
	}

	/*
	 * Format what came in.
	 */
	va_start(alist, format);
	(void) vsnprintf(tmpbuf, sizeof (tmpbuf), format, alist);
	va_end(alist);

	/*
	 * Add on the prefix(s) and generate the whole message.
	 */
	(void) snprintf(outbuf, sizeof (outbuf), "%s: %s: %s",
	    progname, prefix, tmpbuf);

	/*
	 * If it's one of these messages, then check for a
	 * terminating "\n".  If it's not found then the user
	 * wanted us to append an "errno" string.
	 */
	if ((msgtype >= MSG_WARN) && (strrchr(format, '\n') == NULL)) {
		caddr_t strp;
		strp = strerror(err);
		if (strp == NULL)
			(void) snprintf(tmpbuf, sizeof (tmpbuf),
			    ": errno=%d\n", err);
		else
			(void) snprintf(tmpbuf, sizeof (tmpbuf),
			    ": errno=%s\n", strerror(err));
		(void) strcat(outbuf, tmpbuf);
	}

	/*
	 * Display the message to stderr.
	 */
	(void) fprintf(stderr, "%s", outbuf);

	/*
	 * Also send the message to the log daemon if logging is enabled.
	 */
	if (logging)
		syslog(priority, "%s", outbuf);

	/*
	 * Finally, check if we need to terminate the program.
	 */
	if (msgtype == MSG_FATAL) {
		(void) fini_config();
		exit(1);
	} else if (msgtype == MSG_ABORT) {
		(void) fini_config();
		abort();
	}
}

struct physmem_setup_param	phys_setup_param;

/*
 * This routine uses /dev/physmem to allocate a segment and create virtual
 * mapping for the specified physical address and size. It returns -1 on
 * error.
 */
caddr_t
map_pa_to_va(uint64_t paddr, int size)
{
	int				i, fd, npages;
	caddr_t				va, vaddr;
	uint64_t			pa;
	struct physmem_map_param	phys_map_param;
	char				*fname = "map_pa_to_va";

	msg(MSG_DEBUG2, "%s: paddr=0x%llx, size=0x%x\n", fname, paddr, size);

	bzero(&phys_setup_param, sizeof (struct physmem_setup_param));
	phys_setup_param.req_paddr = paddr;
	phys_setup_param.len = size;

	if ((fd = open("/dev/physmem", O_RDWR)) == -1) {
		msg(MSG_ERROR, "%s: couldn't open /dev/physmem", fname);
		return ((caddr_t)-1);
	}

	/*
	 * Reserve virtual address space.
	 */
	if (ioctl(fd, PHYSMEM_SETUP, &phys_setup_param) == -1) {
		msg(MSG_ERROR, "%s: PHYSMEM_SETUP ioctl failed: %s", fname,
		    strerror(errno));
		(void) close(fd);
		return ((caddr_t)-1);
	}

	/*
	 * Map physical pages.
	 */
	npages = size / PAGESIZE;
	vaddr = va = (caddr_t)(uintptr_t)phys_setup_param.user_va;
	pa = paddr;
	for (i = 0; i < npages; i++, va += PAGESIZE, pa += PAGESIZE) {

		bzero(&phys_map_param, sizeof (phys_map_param));
		phys_map_param.req_paddr = pa;
		phys_map_param.flags = PHYSMEM_CAGE | PHYSMEM_RETIRED;

		if (ioctl(fd, PHYSMEM_MAP, &phys_map_param) == -1) {
			msg(MSG_ERROR, "%s: PHYSMEM_MAP ioctl failed for "
			    "pa=0x%llx: %s\n", fname, pa, strerror(errno));
			(void) ioctl(fd, PHYSMEM_DESTROY, &phys_setup_param);
			(void) close(fd);
			return ((caddr_t)-1);
		}

		/*
		 * Sanity check.
		 */
		if (va != (caddr_t)(uintptr_t)phys_map_param.ret_va) {
			msg(MSG_ERROR, "%s: va(0x%llx) !=  "
			    "phys_map_param.ret_va(0x%llx)", fname, va,
			    phys_map_param.ret_va);
			(void) ioctl(fd, PHYSMEM_DESTROY, &phys_setup_param);
			(void) close(fd);
			return ((caddr_t)-1);
		}
	}

	(void) close(fd);

	msg(MSG_DEBUG2, "%s: pa=0x%llx has been mapped to va=0x%p for "
	    "%d pages\n", fname,  paddr, vaddr, npages);

	return (vaddr);
}

/*
 * This routine counts the number of bits set in a 64 bit value.
 * 64-bit population count, use well-known popcnt trick.
 */
static int
popc64(uint64_t val)
{
	int	cnt;

	for (cnt = 0; val != 0; val &= val - 1)
		cnt++;
	return (cnt);
}

/*
 * This routine does some post-test cleanup.
 */
static int
post_test(mdata_t *mdatap)
{
	caddr_t	databuf = (caddr_t)(uintptr_t)mdatap->m_iocp->ioc_bufbase;
	int	bufsize = mdatap->m_iocp->ioc_bufsize;

	msg(MSG_DEBUG2, "post_test(mdatap=0x%p)\n", mdatap);

	/*
	 * Sleep for the required amount of time.
	 */
	if (post_test_sleep) {
		msg(MSG_VERBOSE, "sleeping for %d seconds\n",
		    post_test_sleep);
		(void) sleep(post_test_sleep);
	}

	if (databuf != NULL) {
		if (F_BINDMEM(mdatap->m_iocp)) {
			release_physmem();
		} else if (munmap(databuf, bufsize) == -1) {
			msg(MSG_ERROR, "post_test: munmap failed");
			return (-1);
		}
		msg(MSG_DEBUG2, "post_test: unmapped vaddr=0x%p, bufsize=%d\n",
		    databuf, bufsize);
	}

	return (0);
}

/*
 * The pointers to the data and instruction buffers (m_databuf and m_instbuf)
 * may need platform-specific adjustments due to multi-node memory
 * interleave settings.
 */
static int
pre_test_adjust_buffers(mdata_t *mdatap)
{
	if (cpu_get_type(mdatap->m_cip) == CPUTYPE_RFALLS) {
		return (kt_adjust_buf_to_local(mdatap));
	} else if (cpu_get_type(mdatap->m_cip) == CPUTYPE_VFALLS) {
		return (vf_adjust_buf_to_local(mdatap));
	} else {
		return (0);
	}
}

/*
 * Copy the assembly routines to the allocated buffer. In the
 * instance(s) where they are used (I$ corruption), this area is
 * taken out of anon memory so that it will never be written back.
 * This alleviates the risk of the page (with the inserted error)
 * being picked up later by fsflush (et al); where a user error
 * could turn into a kernel error (causing a UE panic).
 *
 * NOTE: if the processor is of a type which can be in a multi-way
 *	 system then the memory interleave must be taken into account.
 *	 Since KT has a fine interleave of 1K, all three routines will
 *	 fit into a single "slice" so no special copying needs to be done
 *	 as long as the buffer is aligned properly (which is performed
 *	 by the previously run *_adjust_buffers() routines).
 */
static int
pre_test_copy_asm(mdata_t *mdatap)
{
	int		len;
	caddr_t		tmpbuf;
	char		*fname = "pre_test_copy_asm";

	msg(MSG_DEBUG2, "%s: copying asm routines to buffer at 0x%p\n",
	    fname, mdatap->m_instbuf);

	if (cpu_get_type(mdatap->m_cip) == CPUTYPE_VFALLS) {
		if (vf_pre_test_copy_asm(mdatap) < 0)
			return (-1);
	} else {
		len = 256;
		tmpbuf = mdatap->m_instbuf;

		bcopy((caddr_t)asmld, tmpbuf, len);
		mdatap->m_asmld = (asmld_t *)(tmpbuf);

		tmpbuf += len;
		bcopy((caddr_t)asmldst, tmpbuf, len);
		mdatap->m_asmldst = (asmldst_t *)(tmpbuf);

		tmpbuf += len;
		bcopy((caddr_t)blkld, tmpbuf, len);
		mdatap->m_blkld = (blkld_t *)(tmpbuf);
	}

	msg(MSG_DEBUG2, "%s: m_asmld = 0x%p\n", fname, mdatap->m_asmld);
	msg(MSG_DEBUG2, "%s: m_asmldst = 0x%p\n", fname,
	    mdatap->m_asmldst);
	msg(MSG_DEBUG2, "%s: m_blkld = 0x%p\n", fname, mdatap->m_blkld);

	return (0);
}

/*
 * This routine does some test specific initialization.
 * It returns 0 on success, and -1 on failure.
 */
static int
pre_test(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	system_info_t	*sip = mdatap->m_sip;
	cmd_t		*cmdp = mdatap->m_cmdp;

	msg(MSG_DEBUG2, "pre_test(mdatap=0x%p)\n", mdatap);

	/*
	 * If this is not one of the bus specific errors then set
	 * xor pattern if it has not already been set and check it
	 * for unusual settings.
	 */
	if (!F_XORPAT(iocp) && !ERR_PROT_ISBUS(iocp->ioc_command)) {
		iocp->ioc_xorpat = get_xorpat(mdatap);
		iocp->ioc_flags |= FLAGS_XORPAT;
	}

	/*
	 * Set corruption and access offsets from command
	 * structure if they were not specified on command line.
	 */
	if (!F_C_OFFSET(iocp)) {
		iocp->ioc_c_offset = cmdp->c_c_offset;
		iocp->ioc_flags |= FLAGS_CORRUPT_OFFSET;
	}

	if (!F_A_OFFSET(iocp)) {
		iocp->ioc_a_offset = cmdp->c_a_offset;
		iocp->ioc_flags |= FLAGS_ACCESS_OFFSET;
	}

	/*
	 * If the command is of type UTIL or the target is not
	 * to be owned by the EI then do not allocate a user memory
	 * buffer for the injection.
	 */
	if (ERR_CLASS_ISUTIL(iocp->ioc_command) ||
	    ERR_MISC_ISLOWIMPACT(iocp->ioc_command)) {
		iocp->ioc_bufbase = NULL;
		iocp->ioc_databuf = NULL;
		mdatap->m_databuf = NULL;
	} else {
		/*
		 * Allocate the data buffer.
		 */
		iocp->ioc_bufsize = MIN_DATABUF_SIZE;
		if ((iocp->ioc_databuf =
		    (uint64_t)(uintptr_t)alloc_databuf(mdatap,
		    iocp->ioc_bufsize)) == NULL) {
			msg(MSG_ERROR, "pre_test: couldn't allocate data "
			    "buffer\n");
			return (-1);
		}

		iocp->ioc_bufbase = iocp->ioc_databuf;
		mdatap->m_databuf = (caddr_t)(uintptr_t)iocp->ioc_databuf;

		/*
		 * Use the second half of the data buffer for copying/executing
		 * instructions to/from.
		 */
		mdatap->m_instbuf =
		    (caddr_t)(uintptr_t)iocp->ioc_databuf +
		    (iocp->ioc_bufsize / 2);

		if (pre_test_adjust_buffers(mdatap) < 0) {
			msg(MSG_DEBUG1, "pre_test: adjust_buffers failed\n");
			return (-1);
		}

		/*
		 * Copy asm routines into instruction buffer.
		 */
		if (pre_test_copy_asm(mdatap) < 0) {
			msg(MSG_ERROR, "pre_test: copy asm failed");
			return (-1);
		}

		/*
		 * Initialize the corruption address if it was not
		 * already set in the alloc_databuf() routine.
		 */
		if (!F_ADDR(iocp) || (F_ADDR(iocp) && F_BINDMEM(iocp)) &&
		    !F_CACHE_INDEX(iocp)) {
			if (ERR_ACC_ISFETCH(iocp->ioc_command)) {
				iocp->ioc_addr =
				    (uint64_t)(uintptr_t)mdatap->m_asmldst;
			} else {
				iocp->ioc_addr =
				    (uint64_t)(uintptr_t)mdatap->m_databuf;
			}
		}
	}

	/*
	 * Determine how many threads are needed to run this test.
	 *
	 * CPU bindings have already been initialized to default and
	 * will be overridden by the "-b" option if specified.
	 *
	 * Thread 0 is always the consumer.
	 * If the test is single threaded, then the producer
	 * is also thread 0, otherwise it is thread 1.
	 */
	if (ERR_CLASS_ISL2CP(iocp->ioc_command) ||
	    ERR_CLASS_ISL3CP(iocp->ioc_command)) {
		iocp->ioc_nthreads = 2;
	} else {
		iocp->ioc_nthreads = 1;
	}

	/*
	 * Call the processor specific pre-test routine if one is
	 * available. This is where additional initialization
	 * would be done such as determining thread bindings
	 * for processor specific tests.
	 */
	if (mdatap->m_opvp->op_pre_test != NULL) {
		msg(MSG_DEBUG2, "pre_test: calling processor specific "
		    "pre-test routine\n");
		if (OP_PRE_TEST(mdatap) != 0)
			return (-1);
	}

	/*
	 * Do this check after the call to OP_PRE_TEST() since
	 * in some rare cases ioc_nthreads is modified for processor
	 * specific needs.
	 */
	if ((QF_PAUSE_CPUS_EN(iocp) || QF_OFFLINE_CPUS_EN(iocp)) &&
	    iocp->ioc_nthreads > 1) {
		msg(MSG_ERROR, "pre_test: command uses multiple threads "
		    "and cannot be used with the \"-Q pause\" or "
		    "\"-Q offline\" options\n");
		return (-1);
	}

	/*
	 * Make sure we have enough cpus to run this test.
	 */
	if (sip->s_ncpus_online < iocp->ioc_nthreads) {
		msg(MSG_WARN, "pre_test: command requires at least %d "
		    "on-line cpus\n", iocp->ioc_nthreads);
		return (-1);
	}

	/*
	 * Initialize data for all threads.
	 */
	if (init_threads(mdatap) != 0) {
		msg(MSG_ERROR, "pre_test: failed to initialize threads\n");
		return (-1);
	}

	/*
	 * Bind the primary thread now.
	 */
	if (bind_thread(mdatap) != 0) {
		msg(MSG_ERROR, "pre_test: failed to bind primary thread");
		return (-1);
	}

	return (0);
}

/*
 * Print the addresses and contents of the buffer(s) being used for
 * the injection of user mode commands.  Note that this information
 * for command in other modes is printed from driver.
 *
 * NOTE: the actual corruption and access addresses are printed
 *	 from the driver after any offsets have been applied.
 */
static void
print_user_mode_buf_info(mdata_t *mdatap)
{
	int	pid = getpid();
	char	*fname = "print_user_mode_buf_info";

	msg(MSG_VERBOSE, "%s: buffer addresses and data contents "
	    "being used in user mode:\n", fname);
	msg(MSG_VERBOSE, "asmld routine: uvaddr=0x%p "
	    "(*=0x%llx, paddr/raddr=0x%llx)\n",
	    mdatap->m_asmld,
	    mdatap->m_asmld ? *(uint64_t *)asmld : 0,
	    mdatap->m_asmld ? uva_to_pa((uint64_t)(uintptr_t)mdatap->m_asmld,
	    pid) : 0);
	msg(MSG_VERBOSE, "asmldst routine: uvaddr=0x%p "
	    "(*=0x%llx, paddr/raddr=0x%llx)\n",
	    mdatap->m_asmldst,
	    mdatap->m_asmldst ? *(uint64_t *)asmldst : 0,
	    mdatap->m_asmldst ? uva_to_pa((uint64_t)(uintptr_t)
	    mdatap->m_asmldst, pid) : 0);
	msg(MSG_VERBOSE, "m_databuf: uvaddr=0x%p "
	    "(*=0x%llx, paddr/raddr=0x%llx)\n",
	    mdatap->m_databuf,
	    *((uint64_t *)(uintptr_t)mdatap->m_databuf),
	    uva_to_pa((uint64_t)(uintptr_t)mdatap->m_databuf, pid));
	msg(MSG_VERBOSE, "m_instbuf: uvaddr=0x%p "
	    "(*=0x%llx, paddr/raddr=0x%llx)\n",
	    mdatap->m_instbuf,
	    *((uint64_t *)(uintptr_t)mdatap->m_instbuf),
	    uva_to_pa((uint64_t)(uintptr_t)mdatap->m_instbuf, pid));
}

/*
 * This function translates a sun4v real address to the actual
 * underlying physical one.
 */
uint64_t
ra_to_pa(uint64_t raddr)
{
	mem_req_t	mr;
	char		*fname = "ra_to_pa";

	if (sun4v_flag != TRUE) {
		msg(MSG_ERROR, "%s: function not supported on this "
		    "architecture\n", fname);
		return (-1);
	}

	bzero(&mr, sizeof (mem_req_t));
	mr.m_cmd = MREQ_RA_TO_PA;
	mr.m_vaddr = raddr;
	if (do_ioctl(MEMTEST_MEMREQ, &mr, "ra_to_pa") == -1) {
		msg(MSG_ERROR, "%s: translation failed for "
		    "raddr=0x%llx\n", fname, raddr);
		return (-1);
	}

	msg(MSG_DEBUG2, "%s: raddr=0x%llx is mapped to paddr=0x%llx\n",
	    fname, raddr, mr.m_paddr1);

	return (mr.m_paddr1);
}

/*
 * Release /dev/physmem allocations
 */
static void
release_physmem(void)
{
	int	fd;
	char	*fname = "release_physmem";

	if ((fd = open("/dev/physmem", O_RDWR)) == -1) {
		msg(MSG_ERROR, "%s: couldn't open /dev/physmem", fname);
		return;
	}

	(void) ioctl(fd, PHYSMEM_DESTROY, &phys_setup_param);

	(void) close(fd);
}

/*
 * This function is used to save the kernel vars since they can be
 * overwritten by the memtest driver and will need to be restored later.
 */
static int
save_kvars(void)
{
	int		tmp;
	int		cfgerror = 0;
	FILE		*fp;

	msg(MSG_DEBUG3, "save_kvars: determining if save required\n");

	if ((fp = fopen(CFG_FNAME, "rb+")) == NULL) {
		msg(MSG_ERROR, "save_kvars: unable to open config file: %s",
		    CFG_FNAME);
		return (-1);
	}

	/*
	 * Lock the config file.
	 */
	flock.l_whence	= 0;
	flock.l_start	= 0;
	flock.l_len	= 0;		/* entire file */
	flock.l_pid	= getpid();
	flock.l_type	= F_WRLCK;
	if (fcntl(fileno(fp), F_SETLKW, &flock) == -1) {
		msg(MSG_ERROR, "save_kvars: unable to aquire lock on "
		    "config file: %s", CFG_FNAME);
		(void) fclose(fp);
		return (-1);
	}

	/*
	 * Read/write the locked config file.
	 */
	if ((tmp = fread(cfilep, sizeof (config_file_t), 1, fp)) != 1) {
		msg(MSG_ERROR, "save_kvars: unable to read config file: %s",
		    CFG_FNAME);
		cfgerror = -1;
	}
	msg(MSG_DEBUG3, "save_kvars: config file count = %d\n", cfilep->count);

	/*
	 * If required get kernel vars from driver and save in config file.
	 */
	if ((cfilep->count == 1) && (cfilep->saved == B_FALSE) &&
	    (cfgerror == 0)) {
		cfilep->rw = B_FALSE;
		if ((tmp = do_ioctl(MEMTEST_SETKVARS, cfilep, "save_kvars"))
		    != 0) {
			msg(MSG_ERROR, "SETKVARS ioctl failed: %d", tmp);
			cfgerror = -1;
		}

		if (cfgerror == 0) {
			rewind(fp);
			if ((fwrite(cfilep, sizeof (config_file_t), 1, fp))
			    != 1) {
				msg(MSG_ERROR, "save_kvars: unable to write "
				    "config file: %s", CFG_FNAME);
				cfgerror = -1;
			}
		}
	}

	/*
	 * Unlock and close the config file.
	 */
	flock.l_type = F_UNLCK;
	if (fcntl(fileno(fp), F_SETLKW, &flock) == -1) {
		msg(MSG_ERROR, "save_kvars: unable to release lock on "
		    "config file: %s", CFG_FNAME);
		cfgerror = -1;
	}

	(void) fclose(fp);
	return (cfgerror);
}

/*
 * This routine unbinds the currently running thread from its cpu.
 */
static int
unbind_thread(mdata_t *mdatap)
{
	msg(MSG_DEBUG3, "unbind_thread(mdatap=0x%p): bound=%d\n",
	    mdatap, mdatap->m_bound);

	if (mdatap->m_bound == 0) {
		msg(MSG_ABORT, "unbind_thread: bound flag is already 0!\n");
		/* NOT REACHED */
	}

	if ((processor_bind(P_LWPID, P_MYID, PBIND_NONE, NULL) == -1)) {
		msg(MSG_ERROR, "unbind_thread: unbind from cpu failed\n");
		return (-1);
	} else {
		mdatap->m_bound = 0;
		return (0);
	}
}

/*
 * This function displays either a short or long usage message depending
 * on whether the commands array has been initialized. Output is sent
 * to either stdout or stderr depending on whether this routine was
 * called due to an error or intentionally due to the "-h" option.
 *
 * Options are:
 *
 *	-A	Specifies the data access type used to invoke the error.
 *		Supported access types are:
 *
 *			load	- a load access is used to invoke the error
 *			store	- a store access is used to invoke the error
 *
 *		e.g.
 *		"mtst -Astore kdue"
 *		will do a store access to invoke the memory error.
 *
 *		Stores cause different behavior to loads for L2-cache errors
 *		since the cache lines will be in the dirty (M,O) rather
 *		than clean (E,S) state.
 *
 *	-a	Specifies a global address to use in commands that accept
 *		addresses. This is overridden by local addresses which
 *		may be specified in the commands themselves.
 *
 *		e.g.
 *		"mtst -a 0x1000 dphys"
 *		is equivalent to "mtst dphys=0x1000"
 *
 *	-B	Specifies the buffer type to use for error injection.
 *		By default the injector uses the user allocated buffer,
 *		and this option allows kernel/hypervisor tests to use a kernel
 *		allocated buffer.
 *
 *		User tests cannot be forced to use the kernel buffer, and
 *		an error message will be printed.
 *
 *		Suported buffer types are:
 *
 *			default	- the default buffer type is used.
 *			kern	- use a kernel buffer for injection.
 *			user	- use a user buffer for injection.
 *
 *		e.g.
 *		"mtst -Buser kdldwu"
 *		will execute the kernel L2 test using a user buffer.
 *
 *		e.g.
 *		"mtst -Bkern kdue"
 *		will execute the kernel memory test using a kernel buffer.
 *
 *	-b	Specifies bindings for thread(s).
 *		Threads may be bound to CPUs based on cpu id, cpu type,
 *		and memory locality. Only a single binding type may be
 *		specified for a thread, but binding for multiple
 *		threads may be specified by using a "," in between.
 *
 *		Note that for single threaded tests, thread 0 is
 *		both the consumer and producer of the error. For
 *		dual threaded tests, thread 0 is the consumer and
 *		thread 1 is the producer.
 *
 *		Supported options are:
 *
 *			cpuid=<cpu-virtual-id>
 *
 *			cputype=<cpu-type>
 *				where <cpu-type> may be:
 *					sf	(Spitfire = US-I))
 *					bb	(Blackbird = US-II)
 *					sa	(Sabre = US-IIi)
 *					hb	(Hummingbird = US-IIe)
 *					ch	(Cheetah = US-III)
 *					chp	(Cheetah Plus = US-III+)
 *					ja	(Jalapeno = US-IIIi))
 *					jg	(Jaguar = US-IV)
 *					pn	(Panther = US-IV+)
 *					sr	(Serrano = US-IIIi+)
 *					ni	(Niagara = US-T1)
 *					n2	(Niagara = US-T2)
 *					oc	(Olympus-C = US64-VI)
 * 					vf	(Victoria Falls = US-T2+)
 *					jp	(Jupiter = US64-VII)
 *
 *			local_mem
 *				A CPU will be chosen such that the memory
 *				used is local to it. This suboption is only
 *				applicable to cpu types "ja" and" sr".
 *
 *			remote_mem
 *				A CPU will be chosen such that the memory
 *				used is remote to it.  This suboption is only
 *				applicable to cpu types "ja" and" sr".
 *
 *		e.g.
 *		"mtst -bcpuid=0,cputype=pn kducu"
 *		will bind the consumer thread of a test to cpu id 0,
 *		and the producer thread to cpu type US-IV.
 *
 *		e.g.
 *		"mtst -blocal_mem kdce"
 *		will bind thread 0 to a cpu such that the memory being tested
 *		is considered "local" to that cpu.
 *
 *	-C	Specify cacheline options for error injection.
 *
 *		Supported options are:
 *
 * 			default	- use the default cache line and state for the
 *				  test. Most cache tests put the line into a
 *				  dirty state (most instruction tests do not).
 *
 *			clean	- use a clean cache state for the test.
 *				  i.e. Do not modify the cache line.
 *
 *			dirty	- use a dirty cache state for the test, which
 *				  will generally cause associated writeback
 *				  errors.
 *				  i.e. Modify the cache line.
 *
 *			index	- use a specific cache index to inject into.
 *
 *			way	- use a specific cache way to inject into.
 *
 *		e.g.
 *		"mtst -Cclean kdldau"	forces the cacheline state to clean
 *		"mtst -Cdirty kdldau"	forces the cacheline state to dirty
 *		"mtst -Cway=2 kdldac"	forces the cacheline way to way 2
 *
 *	-c	Enable corruption of checkbits (data is corrupted by default).
 *		Not all commands support this option.
 *
 *		e.g.
 *		"mtst kducu"	will inject an error into L2$ data bits.
 *		"mtst -c kducu"  will inject an error into L2$ check bits.
 *
 *	-D 	Specifies the debug level to run at.
 *		This option takes a data value where bits 0-3 specify the
 *		user debug level and bits 4-7 specify the kernel level
 *		(Default level is 0 = off).
 *
 *		Debug messages are displayed on the console.
 *
 *		e.g.
 *		"mtst -D0x33 kdue"
 *		turns on a whole bunch of debug messages for both user
 *		and kernel code.
 *
 *	-d	Specifies delay in seconds that some commands (e.g. ephys)
 *		delay before injecting an error (default is no delay).
 *
 *		This delay helps to prevent "pollution" of the L2 cache by
 *		the injector itself so that more meaningful statistics
 *		can be collected.
 *
 *		e.g.
 *		"mtst -d 3 ephys=1234"
 *		will sleep for 3 seconds then inject the error.
 *
 *	-F	Disables flushing of (usually all) caches prior to the
 *		injection of an error for test types that support this option.
 *
 *		Normally caches are flushed before an error is injected
 *		in order to remove all references to memory locations.
 *
 *	-f	Specifies the name of the file to use for IO errors.
 *		(A temporary file is used by default.)
 *
 *		e.g.
 *		"mtst -f /tmp/foo ioue"
 *		will create a file called "/tmp/foo" and use it for the test.
 *
 *	-h	Display help (usage) information.
 *		A list of all supported commands for the cpu type we are
 *		running on or was specified are listed.
 *
 *	-i	Iteration options to control how mutiple injections of the
 *		same type are injected with a single command:
 *
 *			count=<value>	Certain commands can be injected a
 *					number of times in a row using the
 *					same address/buffer.
 *
 *			stride=<value>	Used with the count subopt to modify
 *					the target address by a certain amount
 *					between each injection/invocation.
 *
 *			infinite	Use infinite injection mode for the
 *					tests  which support multi-shot
 *					injection mode (gernerally tests which
 *					use a processors *_ERR_INJECT register).
 *					This suboption is sun4v ONLY.
 *
 *	-l	Enable logging via syslogd.
 *		User level messages go to both the controlling terminal
 *		and syslogd.
 *
 *	-M	Execute a memory related command.
 *
 *			uva_to_pa=<vaddr>	Display the physical address
 *						for the specified user virtual
 *						and pid. This option requires
 *						the "pid" suboption to also
 *						have been specified.
 *
 *			kva_to_pa=<kvaddr>	Display the physical address
 *						for the specified kernel
 *						virtual address.
 *
 *			pa_to_unum=<paddr>	Display the UNUM for the
 *						specified physical address
 *						and syndrome.  This suboption
 *						requires the "synd" suboption
 *						to have been specified.
 *
 *			pid=<pid>		This suboption is required
 *						by some other suboptions.
 *
 *		e.g.
 *		"mtst -Msynd=0x1f,pa_to_unum=0x10000"
 *		will translate the specified syndrom and physical address
 *		to a unum location by calling the kernel mapping routine.
 *
 *		e.g.
 *		"mtst -Muva_to_pa=addr"
 *		will return the physical address to which the specified
 *		user virtual address maps.
 *
 *	-m	Allows the two available miscellaneous arguments to be set.
 *		The functional specification for various injectors should
 *		describe what misc options are supported by which commands.
 *		Supported options are:
 *
 *			misc1=<value>		Specifies misc argument 1.
 *
 *			misc2=<value>		Specifies misc argument 2.
 *
 *		e.g.
 *		"mtst -m misc1=0x41, misc2=0x6002 foo"
 *		will run test foo with the specified misc arguments.
 *
 *	-N	Disables the setting of error enable registers.
 *		By default the injector checks and enables error registers
 *		as necessary, this option will leave them as-is.
 *
 *	-n	Do not trigger errors, just inject them.
 *
 *	-o	Specifies a global corruption offset in bytes.  Data and
 *		instructions are corrupted at this offset relative to their
 *		beginning. (Each command has its own default offset which
 *		can be overridden by this option.)
 *
 *		e.g.
 *		"mtst -o 0x10 kdue"
 *		will put the error at a 16 byte offset into the data
 *
 *	-p	Specifies a global access offset in bytes. This has no
 *		meaning for instruction errors. (Each command has its own
 *		default offset which can be overridden by this option.)
 *
 *		e.g.
 *		"mtst -o 0x38 -p 0 kdue"
 *		will put the error at the end of the 64byte data block but
 *		access offset 0 to generate the error.
 *
 *	-Q	Specifies the quiesce mode in which to run tests.
 *		The effect of this optio is currently limited to
 *		the calls to cpu_offline/online and pause_cpus() which
 *		occur for certain tests around the injection and
 *		invocation of errors.
 *
 *		Supported options are (not all commands support every
 *		suboption):
 *
 *			default		Run the test in the default mode.
 *
 *			offline		Offline all CPUs except the ones
 *					being used for the test.
 *
 *			nooffline	Do not offline any CPUs.
 *
 *			siboffline	Offline only sibling processors in a
 *					CMP chip.
 *
 *			unoffline	Un-offline CPUs before invoking errors.
 *
 *			pause		Pause CPUs instead of offlining them.
 *
 *			nopause		Do not pause CPUs.
 *
 *			unpause		Unpause CPUs before invoking errors.
 *
 *			park		Park CPUs as well as offlining them.
 *
 *			nopark		Do not park CPUs.
 *
 *			unpark		Unpark CPUs before invoking errors.
 *
 *		e.g.
 *		"mtst -Q unoffline foo"
 *		which offlines other system cpus during injection, but
 *		brings them online them before error invocation.
 *
 *	-R	Sets a CPU register(s) to specified value.
 *		The registers currently supported are:
 *			ecr	e$ control register
 *			eer	e$ error enable register
 *			dcr	dispatch control register
 *			dcucr	d$ unit control register
 *
 *		This option is sun4u ONLY.
 *
 *		e.g.
 *		"mtst -Reer=0x1b foo"
 *		will set the e$ error enable register to 0x1B before
 *		executing the command foo.
 *
 *	-r	Enable randomization flag. Some things (e.g. xor pattern)
 *		may be generated randomly (disabled by default).
 *
 *		e.g.
 *		"mtst -r kducu"
 *		will cause random valid bit positions to be corrupted instead
 *		of the default.
 *
 *	-S	Control (enable/disable/etc) the HW scrubbers.
 *
 *		By default memory tests disable the srubber on a single
 *		DRAM bank, and L2-cache tests disable the scrubber on a
 *		single L2 bank. This option allows the memory (DRAM) and
 *		L2-cache scrubbers on all banks to be enabled, disabled,
 *		or left as-is. Note that the scrub frequency/rate is
 *		left untouched. Both the memory and L2 state may be
 *		specified by using a "," in between.
 *
 *		Supported options are:
 *
 *		memscrub=disable	disable all HW DRAM scrubbers
 *		memscrub=enable		enable all HW DRAM scrubbers
 *		memscrub=asis		leave DRAM scrubbers alone
 *
 *		l2scrub=disable		disable all HW L2$ scrubbers
 *		l2scrub=enable		enable all HW L2$ scrubbers
 *		l2scrub=asis		leave L2$ scrubbers alone
 *
 *		This option is currently sun4v ONLY.
 *
 *	-s	Enable signal catching (disabled by default).
 *
 *		This option allows the OS to recover from user errors
 *		(assuming the kernel also allows this) when executing
 *		multiple commands.
 *
 *	-v	Enable verbose messages (disabled by default).
 *
 *	-X	Forces a cross-call of the error injection routine
 *		on the specified CPU.
 *
 *	-x	Specifies a global XOR pattern to use for corruption.
 *
 *		The global xor pattern will be overridden by any local xor
 *		pattern which may be specified in the commands themselves.
 *		The global xor pattern will override any default xor
 *		pattern that tests use.
 *
 *		e.g.
 *		"mtst kdce"
 *		will use the default xor pattern for the test.
 *
 *		e.g.
 *		"mtst -x 1 kdce"
 *		will cause bit 0 to be corrupted (global xorpat = 1).
 *
 *		e.g.
 *		"mtst -x 1 kdce=0,2"
 *		will cause bit 1 to be corrupted (local xorpat = 2)
 *		and not bit 0 since the global pattern is overridden.
 *		Note that the first argument 0 is always an address and the
 *		second argument 2 is the xor pattern.
 *
 *	-z	Specifies time in seconds to sleep in the user program after
 *		executing a command.
 *
 * Commands are entered after any options and may be entered by themselves
 * or with parameters like this:
 *
 *	command=<addr>,<xorpat>,<misc1>,<misc2>
 */
static void
usage(mdata_t *mdatap, int flags, int exitval)
{
	cmd_t	*cmdp, **cmdpp;

	(void) fprintf(exitval ? stderr : stdout, "Usage:\n"
	    "\t%s [-cFhlnrsv] [-D <debug> (kernel/user)]\n", progname);

	(void) fprintf(exitval ? stderr : stdout,
	    "\t[-A {load|store}] [-a {addr|offset}] [-B {kern|user}]\n"
	    "\t[-b {cpuid=<id>,cputype=<type>,local_mem,remote_mem},...]\n"
	    "\t[-C {clean|dirty,index=<value>,way=<value>}]\n"
	    "\t[-d <delay>] [-f <iofile>]\n");

	if (sun4v_flag == TRUE) {
		(void) fprintf(exitval ? stderr : stdout,
		    "\t[-i {count=<value>,stride=<value>,infinte}]\n");
	} else { /* sun4u */
		(void) fprintf(exitval ? stderr : stdout,
		    "\t[-i {count=<value>,stride=<value>}]\n");
	}

	(void) fprintf(exitval ? stderr : stdout,
	    "\t[-M {uva_to_pa=<addr>,uva_lock=<addr>,uva_unlock=<addr>\n"
	    "\t     kva_to_pa=<addr>,pa_to_unum=<addr>}]\n"
	    "\t[-m {misc1=<value>,misc2=<value>}]\n"
	    "\t[-N (disable setting of all HW error regs)]\n"
	    "\t[-o <corrupt_offset>] [-p <access_offset>]\n"
	    "\t[-Q {offline|nooffline|siboffline|unoffline|\n"
	    "\t     pause|nopause|unpause|park|nopark|unpark}]\n");

	if (sun4v_flag == FALSE) {
		(void) fprintf(exitval ? stderr : stdout,
		    "\t[-R {ecr=<data>,eer=<data>,dcr=<data>,dcucr=<data>}]\n");
	} else {
		(void) fprintf(exitval ? stderr : stdout,
		    "\t[-S {memscrub={disable|enable|asis},\n"
		    "\t     l2scrub={disable|enable|asis}}]\n");
	}

	(void) fprintf(exitval ? stderr : stdout,
	    "\t[-X <xc_cpu>] [-x <xorpat>] [-z <sleep>]\n"
	    "\tcommand ...\n");

	(void) fprintf(exitval ? stderr : stdout, "\n"
	    "Where command can be of the form:\n"
	    "\tcommand=<address>,<xorpat>,<misc1>,<misc2>\n");

	/*
	 * If this is not an explicit request for help "-h option"
	 * then only the short version is provided.
	 */
	if (!(flags & USAGE_VERBOSE)) {
		if (flags & USAGE_EXIT) {
			(void) fini_config();
			exit(exitval);
		} else {
			return;
		}
	}
	if (mdatap == NULL) {
		if (flags & USAGE_EXIT) {
			(void) fini_config();
			exit(exitval);
		} else {
			return;
		}
	}

	/*
	 * This shouldn't happen.
	 */
	if (mdatap->m_cmdpp == NULL) {
		msg(MSG_ABORT, "usage: commands array not initialized\n");
		/* NOT REACHED */
	}

	/*
	 * Display additional command usage information.
	 */
	(void) printf("\nWhere command is one of:\n\n");
	for (cmdpp = mdatap->m_cmdpp; *cmdpp != NULL; cmdpp++) {
		for (cmdp = *cmdpp; cmdp->c_name != NULL; cmdp++) {
			/*
			 * Skip the command if it is not
			 * implemented/supported.
			 */
			if (ERR_MISC_ISNOTIMP(cmdp->c_command) ||
			    ERR_MISC_ISNOTSUP(cmdp->c_command))
				continue;
			(void) printf("\t%s\t", cmdp->c_name);
			if (strlen(cmdp->c_name) < 8)
				(void) printf("\t");
			if (cmdp->c_usage)
				(void) printf("%s\n", cmdp->c_usage);
			else
				(void) printf("Undefined.\n");
		}
		(void) printf("\n");
	}

	if (flags & USAGE_EXIT) {
		(void) fini_config();
		exit(exitval);
	}
}

/*
 * This routine does a busy/wait delay for the specified
 * number of microseconds.
 */
static void
usecdelay(mdata_t *mdatap, int usec)
{
	uint64_t		loops_per_usec = mdatap->m_usecloops;
	register uint64_t	loops = loops_per_usec * usec;

	while (loops-- > 0)
		usecloopcnt++;
}

/*
 * This function translates a user virtual address
 * to a physical one via call down to the driver.
 *
 * NOTE: for sun4v the physical address is actually a real address.
 */
uint64_t
uva_to_pa(uint64_t vaddr, int pid)
{
	mem_req_t		mr;
	/* LINTED */
	volatile uchar_t	c;
	char			*fname = "uva_to_pa";

	if (vaddr == NULL) {
		msg(MSG_ERROR, "%s: vaddr is NULL\n", fname);
		return (-1);
	}

	if (pid == NULL) {
		pid = getpid();
	}

	/*
	 * Touch the vaddr to make sure it is mapped in before
	 * asking the driver what the paddr is for the mapping
	 * but ONLY if the user vaddr is owned by the injector.
	 */
	if (pid == getpid()) {
		c = *(caddr_t)(uintptr_t)vaddr;
	}

	/*
	 * Get the physical mapping for the virtual address.
	 */
	bzero(&mr, sizeof (mem_req_t));
	mr.m_cmd = MREQ_UVA_TO_PA;
	mr.m_vaddr = vaddr;
	mr.m_pid = pid;
	if (do_ioctl(MEMTEST_MEMREQ, &mr, "uva_to_pa") == -1) {
		msg(MSG_ERROR, "%s: translation failed for vaddr=0x%llx\n",
		    fname, vaddr);
		return (-1);
	}

	msg(MSG_DEBUG2, "%s: vaddr=0x%llx is mapped to paddr/raddr=0x%llx\n",
	    fname, vaddr, mr.m_paddr1);

	return (mr.m_paddr1);
}

/*
 * This function is used to synchronize between threads.
 */
int
wait_sync(mdata_t *mdatap, int exp_sync, int usec_timeout, char *str)
{
	volatile int	*syncp = mdatap->m_syncp;
	int		obs_sync;
	int		ret = SYNC_STATUS_OK;

	msg(MSG_DEBUG3, "wait_sync: syncp=0x%p, exp_sync=%d, "
	    "usec_timeout=0x%x, char=%s\n",
	    syncp, exp_sync, usec_timeout, str);
	/*
	 * If wait == 0 then wait forever.
	 */
	if (usec_timeout == 0) {
		obs_sync = *syncp;
		while ((obs_sync != exp_sync) && (obs_sync != -1)) {
			usecdelay(mdatap, 1);
			obs_sync = *syncp;
		}
		if (obs_sync == exp_sync)
			ret = SYNC_STATUS_OK;
		else
			ret = SYNC_STATUS_ERROR;
	} else {
		obs_sync = *syncp;
		while ((obs_sync != exp_sync) && (obs_sync != -1)) {
			usecdelay(mdatap, 1);
			if (usec_timeout-- <= 0)
				break;
			obs_sync = *syncp;
		}
		if (obs_sync == exp_sync)
			ret = SYNC_STATUS_OK;
		else if (obs_sync == -1)
			ret = SYNC_STATUS_ERROR;
		else
			ret = SYNC_STATUS_TIMEOUT;
	}

	switch (ret) {
	case SYNC_STATUS_OK:
		msg(MSG_DEBUG3, "wait_sync: %s: SYNC_STATUS_OK\n", str);
		break;
	case SYNC_STATUS_TIMEOUT:
		msg(MSG_ERROR, "wait_sync: %s: SYNC_STATUS_TIMEOUT waiting for "
		    "sync=%d\n", str, exp_sync);
		break;
	case SYNC_STATUS_ERROR:
		msg(MSG_ERROR, "wait_sync: %s: SYNC_STATUS_ERROR waiting for "
		    "sync=%d\n", str, exp_sync);
		break;
	default:
		msg(MSG_ERROR, "wait_sync: %s: invalid sync value %d "
		    "waiting for sync=%d\n", str, ret, exp_sync);
		break;
	}
	return (ret);
}
