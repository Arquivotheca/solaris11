/*
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MTST_H
#define	_MTST_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * These are defined in cpu module files but only for kernel code.
 */
#if !defined(CHEETAH_IMPL)
#define	CHEETAH_IMPL		0x14
#endif
#if !defined(CHEETAH_PLUS_IMPL)
#define	CHEETAH_PLUS_IMPL	0x15
#endif
#if !defined(JALAPENO_IMPL)
#define	JALAPENO_IMPL		0x16
#endif
#if !defined(JAGUAR_IMPL)
#define	JAGUAR_IMPL		0x18
#endif
#if !defined(PANTHER_IMPL)
#define	PANTHER_IMPL		0x19
#endif
#if !defined(SERRANO_IMPL)
#define	SERRANO_IMPL		0x22
#endif
#if !defined(NIAGARA_IMPL)
#define	NIAGARA_IMPL		0x23
#endif
#if !defined(NIAGARA2_IMPL)
#define	NIAGARA2_IMPL		0x24
#endif
#if !defined(RFALLS_IMPL)
#define	RFALLS_IMPL		0x28
#endif
#if !defined(OLYMPUS_C_IMPL)
#define	OLYMPUS_C_IMPL		0x06
#endif
#if !defined(JUPITER_IMPL)
#define	JUPITER_IMPL		0x07
#endif

#include <errno.h>
#include <fcntl.h>
#include <kstat.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <thread.h>
#include <unistd.h>
#include <stdarg.h>

#include <sys/ddi.h>
#include <sys/file.h>
#include <sys/int_const.h>
#include <sys/ioccom.h>
#include <sys/ioctl.h>
#include <sys/lock.h>
#include <sys/mman.h>
#include <sys/open.h>
#include <sys/param.h>
#include <sys/processor.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/systeminfo.h>
#include <sys/types.h>
#include <sys/utsname.h>

/*
 * Message types.
 */
#define	MSG_DEBUG0	0	/* debug level messages */
#define	MSG_DEBUG1	1
#define	MSG_DEBUG2	2
#define	MSG_DEBUG3	3
#define	MSG_DEBUG4	4
#define	MSG_INFO	5	/* informational messages */
#define	MSG_NOTICE	6	/* similar to INFO but different prefix */
#define	MSG_VERBOSE	7	/* only displayed if verbose is enabled */
#define	MSG_WARN	8	/* warning messages */
#define	MSG_ERROR	9	/* error messages */
#define	MSG_FATAL	10	/* exits the program */
#define	MSG_ABORT	11	/* exits the program with a core dump */

/*
 * These are the invoke access type suboptions.
 * The order of these must be kept consistent with the
 * buffer suboptions in mtst.c.
 */
#define	ACCESSTYPE_LOAD		0
#define	ACCESSTYPE_STORE	1

/*
 * These are the buffer (-B) option type suboptions.
 * The order of these must be kept consistent with the
 * buffer suboptions in mtst.c.
 */
#define	BUFFER_DEFAULT		0
#define	BUFFER_KERN		1
#define	BUFFER_USER		2

/*
 * These are thread to cpu binding (-b) option types.
 * The order of these must be kept consistent with the
 * cpu binding suboptions in mtst.c.
 */
#define	THREAD_BIND_DEFAULT	0
#define	THREAD_BIND_CPUID	1
#define	THREAD_BIND_CPUTYPE	2
#define	THREAD_BIND_LOCAL_MEM	3
#define	THREAD_BIND_REMOTE_MEM	4

/*
 * These are the cache (-C) option types.
 * The order of these must be kept consistent with the
 * cache state suboptions in mtst.c.
 */
#define	CACHE_DEFAULT	0
#define	CACHE_CLEAN	1
#define	CACHE_DIRTY	2
#define	CACHE_INDEX	3
#define	CACHE_WAY	4

/*
 * These are the iterate (-i) arugment option types.
 * The order of these must be kept consistent with the
 * iterate suboptions in mtst.c.
 */
#define	ITERATE_COUNT		0
#define	ITERATE_STRIDE		1
#define	ITERATE_INFINITE	2

/*
 * These are the misc argument (-m) option types.
 * The order of these must be kept consistent with the
 * misc suboptions in mtst.c.
 */
#define	MISCOPT_MISC1		0
#define	MISCOPT_MISC2		1

/*
 * These are the system quiesce (-Q) option types.
 * The order of these must be kept consistent with the
 * related suboptions in mtst.c.
 */
#define	QUIESCE_DEFAULT			0
#define	QUIESCE_OFFLINE			1
#define	QUIESCE_NOOFFLINE		2
#define	QUIESCE_SIBOFFLINE		3
#define	QUIESCE_ONLINE_BF_INVOKE	4
#define	QUIESCE_PAUSE			5
#define	QUIESCE_NOPAUSE			6
#define	QUIESCE_UNPAUSE_BF_INVOKE	7
#define	QUIESCE_PARK			8
#define	QUIESCE_NOPARK			9
#define	QUIESCE_UNPARK_BF_INVOKE	10
#define	QUIESCE_CYCLIC_SUSPEND		11

/*
 * These are the scrub (-S) option types.
 * The order of these must be kept consistent with the
 * related suboptions in mtst.c.
 */
#define	SCRUBOPT_MEMSCRUB		0
#define	SCRUBOPT_L2SCRUB		1

/*
 * Assertion macro used for sanity checking.
 */
#define	ASSERT(EX)	((void)((EX) || assfail(#EX, __FILE__, __LINE__)))

/*
 * Macros for calling vector routines.
 */
#define	OP_FLUSHALL_L2(mdatap) \
	((mdatap)->m_opvp->op_flushall_l2)(mdatap)
#define	OP_PRE_TEST(mdatap) \
	((mdatap)->m_opvp->op_pre_test)(mdatap)
#define	OP_POST_TEST(mdatap) \
	((mdatap)->m_opvp->op_post_test)(mdatap)

typedef void asmld_t(caddr_t);
typedef void asmldst_t(caddr_t, caddr_t, int);
typedef void blkld_t(caddr_t);

/*
 * This is the data structure used to pass information in the user program.
 */
typedef struct mdata {
	ioc_t		*m_iocp;	/* user ioctl buffer */
	system_info_t	*m_sip;		/* system info structure */
	struct cpu_info *m_cip;		/* cpu info structure */
	struct opsvec	*m_opvp;	/* operations vector table */
	struct cmd	**m_cmdpp;	/* commands list */
	struct cmd	*m_cmdp;	/* command to execute */
	caddr_t		m_displace;	/* displacement flush area */
	asmld_t		*m_asmld;	/* routine to load data */
	asmldst_t	*m_asmldst;	/* routine to ld/st data */
	blkld_t		*m_blkld;	/* routint to block load data */
	caddr_t		m_databuf;	/* buf used for data corruption */
	caddr_t		m_instbuf;	/* buf used for inst corruption */
	int		m_threadno;	/* number of this thread */
	int		*m_syncp;	/* thread synchronization variable */
	int		m_usecloops;	/* # of loops in delay routine */
	int		m_bound;	/* is the thread currently bound? */
} mdata_t;

/*
 * Each CPU type supports a different set of user commands.
 * A cpu specific commands array is used to map commands to the appropriate
 * generic routine.  Command information is encoded and passed to the driver
 * where a similar commands array is used to map to the appropriate test
 * routine there.
 */
typedef struct cmd {
	char		*c_name;		/* what the user types in */
	int		(*c_func)(mdata_t *);	/* command function  */
	uint64_t	c_command;		/* encoded command info */
	uint64_t	c_d_mask;		/* mask for data bits */
	uint64_t	c_d_xorpat;		/* default data xor pattern */
	uint64_t	c_c_mask;		/* mask for check bits */
	uint64_t	c_c_xorpat;		/* check bits xor pattern */
	int		c_c_offset;		/* corruption offset */
	int		c_a_offset;		/* access offset */
	char		*c_usage;		/* usage info for command */
} cmd_t;

/*
 * Each CPU type may require unique routines to support common
 * operations.  This is the operations vector table used to call
 * the processor specific routines.
 */
typedef struct opsvec {
	int	(*op_flushall_l2)(mdata_t *);	/* displacement flush e$ */
	int	(*op_pre_test)(mdata_t *);	/* called before each test */
	int	(*op_post_test)(mdata_t *);	/* called after each test */
} opsvec_t;

/*
 * This bit position definition is used in the command structures
 * to specify default corruption (xor) patterns.
 */
#define	BIT(x)	(1ULL << x)

/*
 * While this macro actually does nothing it is used
 * in the event definitions to make them more readable.
 */
#define	MASK(x)	(x)
#define	ALL_BITS	0xFFFFFFFFFFFFFFFFull

/*
 * These corruption and access offset definitions (in bytes) are used in
 * the command structures to specify the defaults.  The corruption and
 * access addresses are different in order to verify that the hw and sw
 * report the error correctly.
 *
 * Note the following when choosing/changing these offsets in
 * command structures.
 *
 *	- Both addresses should fall within a chunk of data that is covered
 *	  by the same protection bit(s). (e.g. 8 bytes for US-IIi/IIe,
 *	  16 bytes for US-I/II and 32bytes for US-III E$ errors) or the
 *	  error may not occur.
 *	- Block load must be 64 byte aligned therefore the offsets must be 0.
 *	- For instruction fetch errors, the access offset is meaningless
 *	  since the instructions are always executed from the beginning
 *	  of the routines.
 *	- For kiue error case on US-III, a corruption offset within the
 *	  first 32 bytes will generate just a UE, while an offset beyond
 *	  32 bytes will cause a UCU (orphaned UCU) as well.
 */
#define	OFFSET(x)	(x)

/*
 * Test routines located in mtst.c.
 */
extern	int	do_io_err(mdata_t *);
extern	int	do_k_err(mdata_t *);
extern	int	do_notimp(mdata_t *);
extern	int	do_u_cp_err(mdata_t *);
extern	int	do_u_err(mdata_t *);
extern	int	do_ud_wb_err(mdata_t *);

/*
 * Support routines located in mtst.c.
 */
extern	int		assfail(char *, char *, int);
extern	int		choose_thr_cpu(mdata_t *);
extern	int		gen_flushall_l2(mdata_t *);
extern	uint64_t	kva_to_pa(uint64_t);
extern	void		msg(int, const char *, ...);
extern	uint64_t	ra_to_pa(uint64_t);
extern	uint64_t	uva_to_pa(uint64_t, int);
extern	int		wait_sync(mdata_t *, int, int, char *);

/*
 * Support data structures located in mtst.c.
 */
extern	mdata_t		*mdatap_array[MAX_NTHREADS];

/*
 * Initialization routines located in sun4u processor specific files.
 */
extern	void	ch_init(mdata_t *);
extern	void	chp_init(mdata_t *);
extern	void	ja_init(mdata_t *);
extern	void	jg_init(mdata_t *);
extern	void	pn_init(mdata_t *);
extern	void	sf_init(mdata_t *);
extern	void	sr_init(mdata_t *);
extern	void	oc_init(mdata_t *);

/*
 * Initialization routines located in sun4v processor specific files.
 */
extern	void	kt_init(mdata_t *);
extern	void	ni_init(mdata_t *);
extern	void	n2_init(mdata_t *);
extern	void	vf_init(mdata_t *);

/*
 * Support routines located in mtst_asm.s.
 */
extern	void	asmld(caddr_t);
extern	void	asmld_quick(caddr_t);
extern	void	asmldst(caddr_t, caddr_t, int);
extern	void	blkld(caddr_t);

/*
 * Generic sun4u commands structures.
 */
extern	cmd_t	sun4u_generic_cmds[];
extern	cmd_t	us3_generic_cmds[];
extern	cmd_t	us3i_generic_cmds[];

/*
 * Processor specific sun4u commands structures.
 */
extern	cmd_t	cheetah_cmds[];
extern	cmd_t	cheetahp_cmds[];
extern	cmd_t	jaguar_cmds[];
extern	cmd_t	jalapeno_cmds[];
extern	cmd_t	panther_cmds[];
extern	cmd_t	spitfire_cmds[];
extern	cmd_t	serrano_cmds[];
extern	cmd_t	olympusc_cmds[];

/*
 * Generic sun4v commands structures.
 */
extern	cmd_t	sun4v_generic_cmds[];

/*
 * Processor specific sun4v commands structures.
 */
extern	cmd_t	kt_cmds[];
extern	cmd_t	niagara_cmds[];
extern	cmd_t	niagara2_cmds[];
extern	cmd_t	vfalls_cmds[];

#ifdef	__cplusplus
}
#endif

#endif	/* _MTST_H */
