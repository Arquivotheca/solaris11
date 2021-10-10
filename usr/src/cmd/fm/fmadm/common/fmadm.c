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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <strings.h>
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>

#include <fmadm.h>

static const char *g_pname;
static fmd_adm_t *g_adm;
static int g_quiet;

/*PRINTFLIKE1*/
void
note(const char *format, ...)
{
	va_list ap;

	if (g_quiet)
		return; /* suppress notices if -q specified */

	(void) fprintf(stdout, "%s: ", g_pname);
	va_start(ap, format);
	(void) vfprintf(stdout, format, ap);
	va_end(ap);
}

static void
vwarn(const char *format, va_list ap)
{
	int err = errno;

	(void) fprintf(stderr, "%s: ", g_pname);

	if (format != NULL)
		(void) vfprintf(stderr, format, ap);

	errno = err; /* restore errno for fmd_adm_errmsg() */

	if (format == NULL)
		(void) fprintf(stderr, "%s\n", fmd_adm_errmsg(g_adm));
	else if (strchr(format, '\n') == NULL)
		(void) fprintf(stderr, ": %s\n", fmd_adm_errmsg(g_adm));
}

/*PRINTFLIKE1*/
void
warn(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vwarn(format, ap);
	va_end(ap);
}

/*PRINTFLIKE1*/
void
die(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vwarn(format, ap);
	va_end(ap);

	fmd_adm_close(g_adm);
	exit(FMADM_EXIT_ERROR);
}

/*
 * NOTE: A cmd with a NULL cmd_desc is private, and not shown in usage.
 * NOTE: A cmd with a NULL name is used to group things in sections.
 */
static const struct cmd {
	int (*cmd_func)(fmd_adm_t *, int, char *[]);
	const char *cmd_name;
	const char *cmd_args;
	const char *cmd_desc;
} cmds[] = {


{ NULL, "", NULL, "Fault Status and Administration"},
{ cmd_faulty, "faulty",
	"[-afgiprsv] [-n <max_fault>] [-u <uuid>]",
	"display list of faulty resources" },
{ cmd_acquit, "acquit",
	"<fmri> [<uuid>] | <label> [<uuid>] | <uuid>",
	"acquit resource or acquit case" },
{ cmd_replaced, "replaced",
	"<fmri> | <label>",
	"notify fault manager that resource has been replaced" },
{ cmd_repaired, "repaired",
	"<fmri> | <label>",
	"notify fault manager that resource has been repaired" },

{ NULL, "", NULL, "Chassis Alias Administration"},
{ cmd_alias_add, "add-alias",
	"<product-id>.<chassis-id> <alias-id> [\'comment\']",
	"add alias to /etc/dev/chassis_aliases database" },
{ cmd_alias_remove, "remove-alias",
	"<alias-id> | <product-id>.<chassis-id>",
	"remove mapping from /etc/dev/chassis_aliases database" },
{ cmd_alias_lookup, "lookup-alias",
	"<alias-id> | <product-id>.<chassis-id>",
	"lookup mapping in /etc/dev/chassis_aliases database" },
{ cmd_alias_list, "list-alias",
	NULL,
	"list current /etc/dev/chassis_aliases database" },
{ cmd_alias_sync, "sync-alias",
	NULL,
	"verify /etc/dev/chassis_aliases contents and sync" },


{ NULL, "", NULL, "Caution: Documented Fault Repair Procedures Only..."},
{ NULL, "", NULL, "  Module Administration"},
{ cmd_config, "config",
	NULL,
	"display fault manager configuration" },
{ cmd_load, "load",
	"<path>",
	"load specified fault manager module" },
{ cmd_unload, "unload",
	"<module>",
	"unload specified fault manager module" },
{ cmd_reset, "reset",
	"[-s serd] <module>",
	"reset module or sub-component" },
{ NULL, "", NULL, "  Log Administration"},
{ cmd_rotate, "rotate",
	"<logname>",
	"rotate log file" },
{ NULL, "", NULL, "  Fault Administration"},
{ cmd_flush, "flush",
	"<fmri> ...",
	"flush cached state for resource" },

{ NULL, "", NULL, NULL},	/* PRIVATE */
{ cmd_repair, "repair",
	"<fmri> | <label> | <uuid>",
	NULL },		/* description: TBS repair .vs. repaired ? */
{ cmd_gc, "gc",
	"<module>",
	NULL },		/* description: TBS */

{ NULL, NULL, NULL }				/* END: (cmd_name == NULL) */
};

static int
usage(FILE *fp)
{
	const struct cmd *cp;

	(void) fprintf(fp,
	    "Usage: %s [-P prog] [-q] [cmd [args ... ]]\n", g_pname);

	for (cp = cmds; cp->cmd_name != NULL; cp++) {
		/* Skip private commands. */
		if (cp->cmd_desc == NULL)
			continue;

		/* Print out an administration section header */
		if (*(cp->cmd_name) == '\0') {
			(void) fprintf(fp, "%s    %s\n",
			    cp->cmd_desc[0] == ' ' ? "" : "\n", cp->cmd_desc);
			continue;
		}

		(void) fprintf(fp, "\t%s %s %s\n\t\t%s\n",
		    g_pname, cp->cmd_name, cp->cmd_args ? cp->cmd_args : "",
		    cp->cmd_desc);
	}
	return (FMADM_EXIT_USAGE);
}

static uint32_t
getu32(const char *name, const char *s)
{
	u_longlong_t val;
	char *p;

	errno = 0;
	val = strtoull(s, &p, 0);

	if (errno != 0 || p == s || *p != '\0' || val > UINT32_MAX) {
		(void) fprintf(stderr, "%s: invalid %s argument -- %s\n",
		    g_pname, name, s);
		exit(FMADM_EXIT_USAGE);
	}

	return ((uint32_t)val);
}

int
main(int argc, char *argv[])
{
	const struct cmd *cp;
	uint32_t program;
	const char *p;
	int c, err;

	if ((p = strrchr(argv[0], '/')) == NULL)
		g_pname = argv[0];
	else
		g_pname = p + 1;

	if ((p = getenv("FMD_PROGRAM")) != NULL)
		program = getu32("$FMD_PROGRAM", p);
	else
		program = FMD_ADM_PROGRAM;

	while ((c = getopt(argc, argv, "P:q")) != EOF) {
		switch (c) {
		case 'P':
			program = getu32("program", optarg);
			break;
		case 'q':
			g_quiet++;
			break;
		default:
			return (usage(stderr));
		}
	}

	if (optind >= argc)
		return (usage(stdout));

	for (cp = cmds; cp->cmd_name != NULL; cp++) {
		if (strcmp(cp->cmd_name, argv[optind]) == 0)
			break;
	}

	if (cp->cmd_name == NULL) {
		(void) fprintf(stderr, "%s: illegal subcommand -- %s\n",
		    g_pname, argv[optind]);
		return (usage(stderr));
	}

	if ((g_adm = fmd_adm_open(NULL, program, FMD_ADM_VERSION)) == NULL)
		die(NULL); /* fmd_adm_errmsg() has enough info */

	argc -= optind;
	argv += optind;

	optind = 1; /* reset optind so subcommands can getopt() */

	err = cp->cmd_func(g_adm, argc, argv);
	fmd_adm_close(g_adm);
	return (err == FMADM_EXIT_USAGE ? usage(stderr) : err);
}
