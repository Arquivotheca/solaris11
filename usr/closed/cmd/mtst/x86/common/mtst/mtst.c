/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <strings.h>
#include <umem.h>
#include <sys/systeminfo.h>
#include <sys/param.h>

#include <mtst_cmd.h>
#include <mtst_cpu.h>
#include <mtst_debug.h>
#include <mtst_err.h>
#include <mtst_memtest.h>
#include <mtst.h>

void
mtst_create(void)
{
	char buf[MAXNAMELEN];

	bzero(&mtst, sizeof (mtst_t));

	(void) sysinfo(SI_PLATFORM, buf, sizeof (buf));
	mtst.mtst_hdlmode = MTST_HDLMODE_NATIVE;
	mtst.mtst_rootdir = "";
	mtst.mtst_memtest_fd = -1;
}

void
mtst_destroy(void)
{
	if (mtst.mtst_memtest_fd != -1)
		mtst_memtest_close();

	mtst_cpumod_unload();
	mtst_ntv_cpuinfo_destroy();
}

#define	MTST_OPTION_GLOBAL	0x1	/* MTST_F_* */
#define	MTST_OPTION_COMMAND	0x2	/* MTST_CMD_F_* */

#define	GLOBAL(flag)	MTST_OPTION_GLOBAL, MTST_F_##flag
#define	GLOBAL2(flag1, flag2)	MTST_OPTION_GLOBAL, \
    MTST_F_##flag1 | MTST_F_##flag2
#define	COMMAND(flag)	MTST_OPTION_COMMAND, MTST_CMD_F_##flag

typedef struct mtst_option {
	const char *mo_name;	/* name for user to specify */
	const char *mo_desc;	/* description of option for help message */
	uint_t mo_target;	/* MTST_OPTION_* */
	uint_t mo_flag;		/* bit to be set in mtst_flags to enable */
} mtst_option_t;

static const mtst_option_t mtst_options[] = {
	{ "abort",	"abort on die()",
	    GLOBAL(ABORTONDIE) },
	{ "debug",	"turn on command and driver debugging mode",
	    GLOBAL(DEBUG) },
	{ "dryrun",	"do all processing but do not inject to driver",
	    GLOBAL2(DRYRUN, DEBUG) },
	{ "int18",	"force #mc exception on inject",
	    COMMAND(INT18) },
	{ "polled",	"force discovery by poller at next wakeup",
	    COMMAND(POLLED) },
	{ "merge",	"trigger errors simultaneously",
	    COMMAND(MERGE) },
	{ "forcemsrwr",	"use wrmsr if no model support",
	    COMMAND(FORCEMSRWR) },
	{ "interposeok", "interpose if MSR write fails",
	    COMMAND(INTERPOSEOK) },
	{ "interpose", "only use interposition",
	    COMMAND(INTERPOSE) },
	{ "help",	NULL,
	    GLOBAL(HELP) },
	NULL
};

static void
mtst_options_help(void)
{
	int i;

	(void) printf("Options for common -o (separate with commas):\n");

	for (i = 0; mtst_options[i].mo_name != NULL; i++) {
		if (mtst_options[i].mo_desc == NULL)
			continue;
		(void) printf("    %-15s%s\n", mtst_options[i].mo_name,
		    mtst_options[i].mo_desc);
	}
}

int
mtst_options_parse(const char *cstr)
{
	size_t cstrsz = strlen(cstr) + 1;
	char *str, *tok;
	int i, rc = 0;

	str = umem_alloc(cstrsz, UMEM_NOFAIL);
	(void) strcpy(str, cstr);

	for (tok = strtok(str, ","); tok != NULL; tok = strtok(NULL, ",")) {
		for (i = 0; mtst_options[i].mo_name != NULL; i++) {
			if (strcmp(tok, mtst_options[i].mo_name) == 0) {
				uint_t val = mtst_options[i].mo_flag;
				if (mtst_options[i].mo_target ==
				    MTST_OPTION_GLOBAL)
					mtst.mtst_flags |= val;
				else
					mtst.mtst_cmdflags |= val;
				break;
			}
		}

		if (mtst_options[i].mo_name == NULL) {
			mtst_warn("failed to parse option \"%s\"; try -o "
			    "help\n", tok);
			rc = -1;
		}
	}

	if (mtst.mtst_flags & MTST_F_HELP) {
		mtst_options_help();
		rc = -1;
	}

	umem_free(str, cstrsz);

	switch (mtst.mtst_cmdflags & (MTST_CMD_F_INT18 | MTST_CMD_F_POLLED |
	    MTST_CMD_F_INT_CMCI)) {
	case 0:
	case MTST_CMD_F_INT18:
	case MTST_CMD_F_INT_CMCI:
	case MTST_CMD_F_POLLED:
		break;

	default:
		mtst_warn("Options int18 and polled are mutually exclusive\n");
		rc = -1;
	}

	return (rc);
}
