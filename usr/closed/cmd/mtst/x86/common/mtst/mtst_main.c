/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/processor.h>

#include <mtst_cmd.h>
#include <mtst_cpu.h>
#include <mtst_err.h>
#include <mtst.h>

mtst_t mtst;

#define	CPUIDSTRLEN	64

/* BEGIN CSTYLED */
static const char *usage =
    "Usage: %1$s [-v] [-o opt] [-R rootdir] [-c cpuid] <cmd> [-o args] ...\n"
    "       %1$s -o help\n"
    "       %1$s -l [-v]\n"
    "\n"
    "      The first synopsis performs the specified command(s) on the\n"
    "      indicated cpuid, or a randomly chosen cpu if none is specified.\n"
    "\n"
    "      The second synopsis lists valid generic (applying to all commands)\n"
    "      options.\n"
    "\n"
    "      The third synopsis lists all known commands; with -v added it\n"
    "      also lists which command-specific arguments each command accepts.\n"
    "\n"
    "        -v		Verbose output.\n"
    "        -o opt	A comma separated list of options; use the\n"
    "      		second synopsis to list valid options.\n"
    "        -R rootdir	Paths to injector command module plugin are\n"
    "      		relative to this if specified.\n"
    "        -c cpuid	Perform commands on the given cpuid;\n"
    "      		the default is to use the lowest-numbered\n"
    "      		online cpu.\n"
    "	     -c chip,core,strand\n"
    "			Alternate form of -c option.  Perform\n"
    "			commands on the given (chip, core, strand)\n"
    "			id tuple.\n"
    "        <cmd>	A command to perform; use the third synopsis\n"
    "      		to list known commands.  Valid args options\n"
    "      		depend on the individual command; multiple\n"
    "      		commands by follow one-another, separated by\n"
    "      		spaces.\n"
    "	     -o args	Options for <cmd>;  use %1$s -lv to see the options\n"
    "			supported for each command.\n";
/* END CSTYLED */

static void
mtst_usage(void)
{
	(void) fprintf(stderr, usage, mtst_getpname());
}

static int
mtst_findlowcpu(void)
{
	int maxcpu = sysconf(_SC_CPUID_MAX);
	int id;

	for (id = 0; id <= maxcpu; id++) {
		processor_info_t info;
		if (processor_info(id, &info) == 0 &&
		    info.pi_state == P_ONLINE)
			return (id);
	}

	return (-1);
}

static int
mtst_cpuid_parse(char *spec)
{
	mtst_cpu_info_t *cpuinfo;
	char *cs1, *cs2, *cs3;
	uint64_t c1, c2, c3;

	if (spec == NULL || strnlen(spec, CPUIDSTRLEN) >= CPUIDSTRLEN)
		return (-1);

	if ((cs1 = strtok(spec, ",:")) == NULL || mtst_strtonum(cs1, &c1) < 0)
		return (-1);

	if ((cs2 = strtok(NULL, ",:")) == NULL) {
		/* logical cpuid specification */
		cpuinfo = mtst_cpuinfo_read_logicalid(c1);
	} else {
		/* id tuple specification */
		if ((cs3 = strtok(NULL, ",:")) == NULL ||
		    mtst_strtonum(cs2, &c2) < 0 || mtst_strtonum(cs3, &c3) < 0)
			return (-1);

		cpuinfo = mtst_cpuinfo_read_idtuple(c1, c2, c3);
	}

	if (cpuinfo == NULL)
		return (-1);

	mtst.mtst_cpuinfo = cpuinfo;
	mtst.mtst_cpuid = mtst_cpuid();

	return (0);
}

int
main(int argc, char *argv[])
{
	mtst_cpu_info_t *cpuinfo;
	int docmdlist = 0;
	int opt_c_used = 0;
	int bound = 0;
	int c;

	mtst_create();

	while ((c = getopt(argc, argv, "c:hlo:R:v")) != EOF) {
		switch (c) {
		case 'c': {
			if (mtst_cpuid_parse(optarg) < 0)
				mtst_die("failed to parse CPU ID - "
				    "use <cpuid> or <chip>,<core>,<strand>");
			opt_c_used = 1;
			break;
		}
		case 'l':
			docmdlist = 1;
			break;
		case 'o':
			if (mtst_options_parse(optarg) < 0) {
				mtst_destroy();
				return (2);
			}
			break;
		case 'R':
			mtst.mtst_rootdir = optarg;
			break;
		case 'v':
			mtst.mtst_cmdflags |= MTST_CMD_F_VERBOSE;
			mtst.mtst_cmdflags |= MTST_CMD_DESC_F_ARGS;
			break;
		case 'h':
			mtst_usage();
			exit(0);
		default:
			mtst_usage();
			exit(2);
		}
	}

	if (argc - optind == 0) {
		if (!docmdlist) {
			mtst_usage();
			exit(2);
		}
	} else if (argc - optind == 1 && strcmp(argv[optind], "list") == 0) {
		docmdlist = 1;
	}

	switch (mtst.mtst_hdlmode) {
	case MTST_HDLMODE_NATIVE: {
		processorid_t bind;

		if (opt_c_used) {
			bind = mtst.mtst_cpuid->mci_cpuid;
		} else {
			/* Choose a cpu to inject on. */

			if (processor_bind(P_PID, P_MYID, PBIND_QUERY,
			    &bind) == 0 && bind != PBIND_NONE) {
				/* already bound - use that */
				bound = 1;
			} else {
				/* choose lowest numbered online cpu */
				if ((bind = mtst_findlowcpu()) == -1) {
					mtst_die("failed to find online CPU; "
					    "please try again\n");
				}
			}
			cpuinfo = mtst_cpuinfo_read_logicalid(bind);
			if (cpuinfo == NULL)
				mtst_die("failed to read cpuinfo for %s %d\n",
				    bound ? "cpu already bound to" :
				    "lowest numbered online cpu", bind);

			mtst.mtst_cpuinfo = cpuinfo;
			mtst.mtst_cpuid = mtst_cpuid();
		}

		/* Bind to cpu for injection */
		if (!bound &&
		    processor_bind(P_PID, P_MYID, bind, NULL) != 0)
			mtst_die("failed to bind to CPU %d", bind);

		break;
	}

	default:
		mtst_die("Unexpected injection handle mode!\n");
	}

	/* Load module(s) supporting the vendor/family/model/stepping present */
	mtst_cpumod_load();

	if (docmdlist) {
		uint_t flags = (mtst.mtst_cmdflags & MTST_CMD_F_VERBOSE) ?
		    MTST_CMD_DESC_F_ALL : MTST_CMD_DESC_F_DESC;
		(void) mtst_cmd_describe(NULL, flags);
	} else {
		argc -= optind;
		argv += optind;

		while (argc > 0) {
			int rc = mtst_cmd_process(argc, argv);
			argc -= rc;
			argv += rc;
		}
	}

	mtst_destroy();
	return (0);
}
