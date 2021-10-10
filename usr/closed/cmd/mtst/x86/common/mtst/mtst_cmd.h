/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MTST_CMD_H
#define	_MTST_CMD_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Mtst error injection commands
 */

#include <mtst_cpu.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	MTST_CMD_DESC_F_DESC	0x1
#define	MTST_CMD_DESC_F_MODNAME	0x2
#define	MTST_CMD_DESC_F_ARGS	0x4
#define	MTST_CMD_DESC_F_ALL	0x7

typedef struct mtst_cmd_impl {
	mtst_list_t mcmd_list;			/* list of commands */
	const char *mcmd_cmdname;		/* short name of command */
	char **mcmd_args;			/* argspec for command */
	int mcmd_nargs;				/* number of args */
	int mcmd_nreqargs;			/* number of required args */
	uint64_t mcmd_injarg;
	mtst_cpumod_impl_t *mcmd_module;	/* implementing module */
	const char *mcmd_desc;			/* command description */
	int (*mcmd_inject)(mtst_cpuid_t *,	/* Injection function */
	    uint_t, const mtst_argspec_t *,
	    int, uint64_t);
} mtst_cmd_impl_t;

extern void mtst_cmd_register(mtst_cpumod_impl_t *, const mtst_cmd_t *);
extern int mtst_cmd_describe(const char *, uint_t);
extern int mtst_cmd_process(int, char *[]);

#ifdef __cplusplus
}
#endif

#endif /* _MTST_CMD_H */
