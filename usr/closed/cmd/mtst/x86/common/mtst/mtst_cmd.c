/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <alloca.h>
#include <umem.h>

#include <mtst_debug.h>
#include <mtst_err.h>
#include <mtst_cmd.h>
#include <mtst_list.h>
#include <mtst.h>

static void
mtst_cmd_usage(mtst_cmd_impl_t *mcmd)
{
	int i;

	(void) fprintf(stderr, "Usage: %s %s", mtst_getpname(),
	    mcmd->mcmd_cmdname);

	for (i = 0; i < mcmd->mcmd_nargs; i++)
		(void) fprintf(stderr, " %s", mcmd->mcmd_args[i]);
	(void) fprintf(stderr, "\n");

	exit(2);
}

/*
 * Compare commands m1`s1 and m2`s2 first by module name then by cmd name.
 * m1 and m2 can be NULL.
 */
static int
mtst_cmdname_cmp(const char *m1, const char *s1, const char *m2, const char *s2)
{
	if (m1 != NULL && m2 != NULL) {
		int mres = strcmp(m1, m2);
		if (mres != 0)
			return (mres);
	}

	if (*s1 == '*')
		s1++;
	if (*s2 == '*')
		s2++;

	return (strcmp(s1, s2));
}

static char **
mtst_cmd_args_burst(const char *argstr, int *nargsp, int *nreqargsp)
{
	int ntok = mtst_strntok(argstr, ",");
	char **args = umem_zalloc(sizeof (char *) * ntok, UMEM_NOFAIL);
	char *str = umem_zalloc(strlen(argstr) + 1, UMEM_NOFAIL);
	int nreq = 0;
	char *tok;
	int i;

	(void) strcpy(str, argstr);
	for (i = 0, tok = strtok(str, ","); tok != NULL;
	    tok = strtok(NULL, ","), i++) {
		args[i] = tok;
		if (*tok == '*')
			nreq++;
	}

	*nargsp = ntok;
	*nreqargsp = nreq;
	return (args);
}

#ifdef __notyet
static void
mtst_cmd_args_free(char **args, int nargs)
{
	size_t sz = 0;
	int i;

	for (i = 0; i < nargs; i++)
		sz += strlen(args[i]) + 1;

	umem_free(args, sz);
}
#endif

static void
mtst_argspec_free(mtst_argspec_t *mas, uint_t nargs)
{
	int i;

	for (i = 0; i < nargs; i++) {
		if (mas[i].mas_argtype == MTST_ARGTYPE_STRING)
			mtst_strfree((char *)mas[i].mas_argstr);
	}

	umem_free(mas, sizeof (mtst_argspec_t) * nargs);
}

static void
mtst_cmd_register_one(mtst_cpumod_impl_t *mcpu, const mtst_cmd_t *apimcmd)
{
	mtst_cmd_impl_t *mcmd;
	int rc = -1;

	for (mcmd = mtst_list_next(&mtst.mtst_cmds); mcmd != NULL;
	    mcmd = mtst_list_next(mcmd)) {
		if ((rc = mtst_cmdname_cmp(mcmd->mcmd_module->mcpu_name,
		    mcmd->mcmd_cmdname, mcpu->mcpu_name,
		    apimcmd->mcmd_cmdname)) >= 0)
			break;
	}

	if (mcmd == NULL || rc > 0) {
		/* create a new command in the right place */
		mtst_cmd_impl_t *new =
		    umem_zalloc(sizeof (mtst_cmd_impl_t), UMEM_NOFAIL);
		if (mcmd == NULL)
			mtst_list_append(&mtst.mtst_cmds, new);
		else
			mtst_list_insert_before(&mtst.mtst_cmds, mcmd, new);
		mcmd = new;
	}

	mcmd->mcmd_cmdname = apimcmd->mcmd_cmdname;
	mcmd->mcmd_module = mcpu;
	mcmd->mcmd_desc = apimcmd->mcmd_desc;
	mcmd->mcmd_inject = apimcmd->mcmd_inject;
	mcmd->mcmd_injarg = apimcmd->mcmd_injarg;

	if (apimcmd->mcmd_args != NULL) {
		mcmd->mcmd_args = mtst_cmd_args_burst(apimcmd->mcmd_args,
		    &mcmd->mcmd_nargs, &mcmd->mcmd_nreqargs);
	}
}

void
mtst_cmd_register(mtst_cpumod_impl_t *mcpu, const mtst_cmd_t *apimcmd)
{
	while (apimcmd->mcmd_cmdname != NULL)
		mtst_cmd_register_one(mcpu, apimcmd++);
}

static void
mtst_cmd_describe_one(mtst_cmd_impl_t *mcmd, uint_t flags)
{
	int didargs = 0;

	(void) printf("%2s%-25s", " ", mcmd->mcmd_cmdname);

	if (mcmd->mcmd_args != NULL && (flags & MTST_CMD_DESC_F_ARGS)) {
		int i;

		(void) printf("\n\targs: ");
		for (i = 0; i < mcmd->mcmd_nargs; i++) {
			(void) printf("%s%s", (i ? ", " : ""),
			    mcmd->mcmd_args[i]);
		}
		(void) printf("\n");
		didargs = 1;
	}

	if (flags & MTST_CMD_DESC_F_MODNAME)
		(void) printf("\tmodule: %s\n", mcmd->mcmd_module->mcpu_name);
	if ((flags & MTST_CMD_DESC_F_DESC) && mcmd->mcmd_desc != NULL)
		(void) printf("\t%s\n", mcmd->mcmd_desc);

	if (didargs)
		(void) printf("\n");
}

static mtst_cmd_impl_t *
mtst_cmd_lookup(const char *cmdname)
{
	mtst_cmd_impl_t *mcmd;

	for (mcmd = mtst_list_next(&mtst.mtst_cmds); mcmd != NULL;
	    mcmd = mtst_list_next(mcmd)) {
		if (mtst_cmdname_cmp(NULL, mcmd->mcmd_cmdname,
		    NULL, cmdname) == 0)
			return (mcmd);
	}

	return (NULL);
}

int
mtst_cmd_describe(const char *name, uint_t flags)
{
	mtst_cmd_impl_t *mcmd;
	const char *mname, *last = NULL;

	if (name != NULL) {
		if ((mcmd = mtst_cmd_lookup(name)) == NULL)
			return (mtst_set_errno(ENOENT));
		(void) printf("%s`%s:\n", mcmd->mcmd_module->mcpu_name, name);
		mtst_cmd_describe_one(mcmd, flags);
	} else {
		for (mcmd = mtst_list_next(&mtst.mtst_cmds); mcmd != NULL;
		    mcmd = mtst_list_next(mcmd)) {
			if ((mname = mcmd->mcmd_module->mcpu_name) != last) {
				(void) printf("%s:\n", mname);
				last = mname;
			}
			mtst_cmd_describe_one(mcmd, flags);
		}
	}

	return (0);
}

static int
mtst_cmd_parse_one_arg(mtst_cmd_impl_t *mcmd, const char *key,
    mtst_argspec_t *mas)
{
	const char *val;
	int keylen, i;
	int isbool = 0;

	if ((val = (const char *)strchr(key, '=')) == NULL) {
		isbool = 1;
		keylen = strlen(key);
	} else if (key == val || *++val == '\0') {
		return (-1);
	} else {
		keylen = (int)(val - 1 - key);
	}

	for (i = 0; i < mcmd->mcmd_nargs; i++) {
		const char *argnm = mcmd->mcmd_args[i];
		int required = 0;

		/* skip over "required argument" indicator */
		if (*argnm == '*') {
			argnm++;
			required = 1;
		}

		if (strncmp(key, argnm, keylen) == 0 &&
		    *(argnm + keylen) == '\0') {
			if (isbool) {
				mas->mas_argtype = MTST_ARGTYPE_BOOLEAN;
			} else if (mtst_strtonum(val, &mas->mas_argval) == 0) {
				mas->mas_argtype = MTST_ARGTYPE_VALUE;
			} else {
				mas->mas_argtype = MTST_ARGTYPE_STRING;
				mas->mas_argstr = mtst_strdup(val);
			}

			mas->mas_argnm = argnm;
			return (required);
		}
	}

	return (-1);
}

static int
mtst_cmd_parse_args(mtst_cmd_impl_t *mcmd, int argc, char *argv[],
    mtst_argspec_t **argsp, int *nargsp)
{
	char *argstr;
	mtst_argspec_t *args, *argp;
	int nreqparsed = 0;
	int nargs, i;
	int arglen = 0;
	const char *c;
	char *tok;

	for (nargs = i = 0; i < argc; i += 2) {
		if (*argv[i] != '-')
			break; /* found the end of the arguments */

		if (strcmp(argv[i], "-o") != 0) {
			mtst_warn("failed to parse option \"%s\"\n", argv[i]);
			return (-1);
		}

		if (argc - i < 2) {
			mtst_warn("no argument for -o option\n");
			return (-1);
		}

		nargs++;

		arglen = MAX(arglen, strlen(argv[i + 1]) + 1);

		for (c = strchr(argv[i + 1], ','); c != NULL;
		    c = strchr(c + 1, ','))
			nargs++;
	}

	if (nargs == 0) {
		if (mcmd->mcmd_nreqargs == 0) {
			return (0);
		} else {
			mtst_warn("Required args absent; see mtst -lv\n");
			return (-1);
		}
	}

	/*
	 * readjust to discount past the end of the arguments for this
	 * command
	 */
	if (i < argc)
		argc = i;

	args = argp = umem_zalloc(sizeof (mtst_argspec_t) * nargs, UMEM_NOFAIL);
	argstr = alloca(arglen);

	for (i = 0; i < argc; i += 2) {
		(void) strcpy(argstr, argv[i + 1]);

		for (tok = strtok(argstr, ","); tok != NULL;
		    tok = strtok(NULL, ",")) {
			int rc;

			if ((rc = mtst_cmd_parse_one_arg(mcmd, tok,
			    argp)) < 0) {
				mtst_warn("failed to parse argument \"%s\"\n",
				    tok);
				return (-1);
			} else {
				nreqparsed += rc;
			}

			argp++;
		}
	}

	if (nreqparsed != mcmd->mcmd_nreqargs) {
		mtst_warn("One or more required command options are "
		    "missing;  see mtst -lv.\n");
		return (-1);
	}

	*argsp = args;
	*nargsp = nargs;

	return (argc);
}

int
mtst_cmd_process(int argc, char *argv[])
{
	mtst_cmd_impl_t *mcmd;
	const char *cmd = argv[0];
	mtst_argspec_t *args = NULL;
	int nargs = 0;
	int used = 1;
	int rv, n, cmdflags;

	ASSERT(argc > 0);
	ASSERT(mtst.mtst_curcmd == NULL);

	if ((mcmd = mtst_cmd_lookup(cmd)) == NULL) {
		mtst_warn("unknown command \"%s\"\n", cmd);
		return (used);
	}

	argc--;
	argv++;

	if ((n = mtst_cmd_parse_args(mcmd, argc, argv, &args, &nargs)) < 0) {
		(void) printf("Command args for");
		mtst_cmd_describe_one(mcmd, MTST_CMD_DESC_F_ARGS);
		mtst_die("failed to parse arguments for \"%s\"\n",
		    mcmd->mcmd_cmdname);
	}

	used += n;

	mtst_dprintf("injecting %s from %s\n",
	    mcmd->mcmd_cmdname, mcmd->mcmd_module->mcpu_name);

	mtst.mtst_curcmd = mcmd;
	cmdflags = mtst.mtst_cmdflags;

	/*
	 * If more errors remain on the command-line and we're being asked to
	 * merge everything into one event, clear any int18 #mc or #cmci request
	 * and pass POLLED to this error, delaying the triggering until the end.
	 */
	if (argc > 0 && (cmdflags & MTST_CMD_F_MERGE)) {
		cmdflags &= ~(MTST_CMD_F_INT18 | MTST_CMD_F_INT_CMCI);
		cmdflags |= MTST_CMD_F_POLLED;
	}

	rv = mcmd->mcmd_inject(mtst.mtst_cpuid, cmdflags, args, nargs,
	    mcmd->mcmd_injarg);

	if (rv == MTST_CMD_ERR)
		mtst_warn("command failed");
	else if (rv == MTST_CMD_USAGE)
		mtst_cmd_usage(mcmd);

	mtst.mtst_curcmd = NULL;
	mtst_argspec_free(args, nargs);

	return (used);
}
