/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MTST_H
#define	_MTST_H

#include <sys/types.h>

#include <mtst_list.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX
#define	MAX(a, b)	((a) > (b) ? (a) : (b))
#endif

#if defined(__i386) || defined(__amd64)
#define	MTST_MACHDIR		"i86pc"
#define	MTST_64DIR		"amd64"
#else
#error	"MTST_MACHDIR and MTST_64DIR undefined"
#endif

#define	MTST_CPUMOD_BASEDIR	"/usr/platform/" MTST_MACHDIR "/lib/mtst"
#if defined(_LP64)
#define	MTST_CPUMOD_SUBDIR	MTST_CPUMOD_BASEDIR "/" MTST_64DIR
#else
#define	MTST_CPUMOD_SUBDIR	MTST_CPUMOD_BASEDIR
#endif

#define	MTST_F_DEBUG		0x1	/* enable debugging messages */
#define	MTST_F_ABORTONDIE	0x2	/* dump core on die() */
#define	MTST_F_HELP		0x4	/* dummy flag for options parser */
#define	MTST_F_DRYRUN		0x8	/* ask driver to dryrun */

struct mtst_cpu_info;
struct mtst_cmd_impl;

#define	MTST_HDLMODE_NATIVE	0x1	/* Bare metal, or looks like it */

typedef struct mtst {
	const char *mtst_rootdir;	/* all paths relative to this */
	uint_t mtst_flags;		/* global flags (MTST_F_*) */
	uint_t mtst_cmdflags;		/* flags for commands (MTST_CMD_F_*) */
	int mtst_hdlmode;		/* MTST_HDLMODE_* */
	struct mtst_cpu_info *mtst_cpuinfo; /* description of target CPU */
	struct mtst_cpuid *mtst_cpuid;	/* target chip/core/strand */
	mtst_list_t mtst_cpumods;	/* cpu mods, most specific first */
	mtst_list_t mtst_cmds;		/* available commands */
	int mtst_memtest_fd;		/* fd for memtest device */
	struct mtst_cmd_impl *mtst_curcmd; /* command currently executing */
	mtst_list_t mtst_memrsrvs;	/* list of memory reservations */
	uint_t mtst_memrsrvlastid;	/* last reservation ID */
} mtst_t;

extern mtst_t mtst;

extern void mtst_create(void);
extern void mtst_destroy(void);

extern int mtst_set_errno(int);

extern int mtst_strtonum(const char *, uint64_t *);
extern int mtst_strntok(const char *, const char *);
extern char *mtst_strdup(const char *);
extern void mtst_strfree(char *);

extern int mtst_options_parse(const char *);

#ifdef __cplusplus
}
#endif

#endif /* _MTST_H */
