/*
 * Fuse: Filesystem in Userspace
 *
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 * See the file COPYING.LIB
 */

#include "fuse.h"
#include "fuse_opt.h"
#include <libuvfs.h>
#include "fuse_impl.h"

#include <string.h>
#include <assert.h>

/*
 * Add an argument to the end of a fuse_args array.  The second argument
 * is copied.
 */

int
fuse_opt_add_arg(struct fuse_args *args, const char *arg)
{
	char **old_argv = args->argv;
	int i;

	args->argc++;

	args->argv = umem_alloc(sizeof (char *) * (args->argc + 1),
	    UMEM_NOFAIL);
	if (old_argv != NULL)
		if (args->allocated)
			(void) memcpy(args->argv, old_argv,
			    sizeof (char *) * args->argc);
		else
			for (i = 0; i < args->argc - 1; i++)
				args->argv[i] = libfuse_strdup(old_argv[i]);
	args->argv[args->argc] = NULL;
	args->argv[args->argc - 1] = libfuse_strdup(arg);

	if (args->allocated)
		umem_free(old_argv, sizeof (char *) * args->argc);
	args->allocated = B_TRUE;

	return (0);
}

/*
 * Insert an argument into an arbitrary slot in a fuse_args array.  The
 * third argument is copied.
 */

int
fuse_opt_insert_arg(struct fuse_args *args, int pos, const char *arg)
{
	(void) fuse_opt_add_arg(args, arg);

	if (pos < args->argc - 1) {
		void *src = args->argv + pos;
		void *dest = args->argv + pos + 1;
		int len = args->argc - pos;

		(void) memmove(dest, src, len * sizeof (char *));
		args->argv[pos] = args->argv[args->argc];
		args->argv[args->argc] = NULL;
	}

	return (0);
}

/*
 * Drop an arbitrary argument from a fuse_args array.
 */

static void
fuse_opt_drop_arg(struct fuse_args *args, uint_t which)
{
	char **oldargs = args->argv;
	int i, j;

	assert(args->argc > 0);

	args->argc--;
	args->argv = umem_alloc(sizeof (char *) * (args->argc + 1),
	    UMEM_NOFAIL);

	for (i = j = 0; i < args->argc + 1; i++) {
		if (i != which) {
			if (args->allocated)
				args->argv[j++] = oldargs[i];
			else
				args->argv[j++] = libfuse_strdup(oldargs[i]);
		} else if (args->allocated) {
			libfuse_strfree(oldargs[i]);
		}
	}
	args->argv[args->argc] = NULL;

	if (args->allocated)
		umem_free(oldargs, sizeof (char *) * (args->argc + 2));
	args->allocated = B_TRUE;
}

/*
 * Add an option to a comma-separated list of options.  Initially,
 * the first argument should be the address of a character pointer
 * pointing to NULL.  This may be reused, and eventually freed
 * with libfuse_strfree().
 */

int
fuse_opt_add_opt(char **opts, const char *opt)
{
	char *newopts, *oldopts;
	int oldlen, optlen, newlen;

	if (opts == NULL)
		return (-1);

	if (*opts == NULL) {
		*opts = libfuse_strdup(opt);
		return (0);
	}

	oldopts = *opts;

	oldlen = strlen(oldopts);
	optlen = strlen(opt);
	newlen = oldlen + optlen + 2;
	newopts = umem_alloc(newlen, UMEM_NOFAIL);

	(void) strlcpy(newopts, oldopts, newlen);
	(void) strlcat(newopts, ",", newlen);
	(void) strlcat(newopts, opt, newlen);

	umem_free(oldopts, oldlen + 1);
	*opts = newopts;
	return (0);
}

/*
 * Free a fuse_args structure.
 */

void
fuse_opt_free_args(struct fuse_args *args)
{
	int i;

	if ((args == NULL) || (! args->allocated))
		return;

	for (i = 0; i < args->argc; i++)
		if (args->argv[i] != NULL)
			libfuse_strfree(args->argv[i]);
	umem_free(args->argv, sizeof (char *) * (args->argc + 1));
}

/*
 * Shift the zeroth argument off of the fuse_args array, storing
 * it in the second argument.  A pointer to the second argument is
 * returned.
 */

static char *
fuse_opt_shift_arg(struct fuse_args *args, char *buffer, int bufsiz)
{
	if ((args == NULL) || (args->argv[0] == NULL))
		return (NULL);

	(void) strlcpy(buffer, args->argv[0], bufsiz);
	fuse_opt_drop_arg(args, 0);

	return (buffer);
}

/*
 * Process an argument according to the key, calling proc if necessary.
 */

static int
fuse_opt_proc(struct fuse_args *args, const char *arg, int key,
    fuse_opt_proc_t proc, void *data)
{
	if (key == FUSE_OPT_KEY_DISCARD)
		return (0);
	if (key == FUSE_OPT_KEY_KEEP)
		return (1);
	if ((proc == NULL) || (data == NULL))
		return (1);

	return (proc(data, arg, key, args));
}

/*
 * Find the next matching option for the given argument.  If an equal
 * sign or a space separates, set *sepp to the index.
 */

static const struct fuse_opt *
fuse_opt_next_match(const struct fuse_opt *opts, const char *arg,
    uint_t *sepp)
{
	const struct fuse_opt *opt;
	char *chop;
	uint_t sep, cmplen;

	for (opt = opts; opt->templ != NULL; opt++) {
		chop = strrchr(opt->templ, '=');
		if (chop == NULL)
			chop = strrchr(opt->templ, ' ');
		if (chop != NULL) {
			cmplen = sep = (uintptr_t)chop - (uintptr_t)opt->templ;
			if (chop[0] == '=')
				++cmplen;
			if (strncmp(arg, opt->templ, cmplen) == 0) {
				*sepp = sep;
				break;
			}
		} else {
			if (strcmp(arg, opt->templ) == 0) {
				*sepp = 0;
				break;
			}
		}
	}

	if (opt->templ == NULL)
		opt = NULL;
	return (opt);
}

/*
 * Scan the input into the value, according to the format.  If the
 * format is %s, copy the string.
 */

static int
fuse_opt_scan(const char *input, const char *format, void *value)
{
	if (format[1] == 's') {
		char **cpp = value;
		*cpp = libfuse_strdup(input);
		return (0);
	}

	if (sscanf(input, format, value) != 1)
		return (-1);

	return (0);
}

/*
 * "apply" the given fuse_opt to the input.  That is, if the offset is
 * -1, call the callback; else, set the integer at the given offset
 * to the given value.
 */

static int
fuse_opt_apply(struct fuse_args *new, const char *arg,
    const struct fuse_opt *opt, fuse_opt_proc_t proc, void *data)
{
	uintptr_t valaddr;
	int rc;

	if ((opt->offset == -1U) || (opt->offset == -1UL)) {
		rc = fuse_opt_proc(new, arg, opt->value, proc, data);
	} else {
		valaddr = (uintptr_t)data + opt->offset;
		*(int *)valaddr = opt->value;
		rc = 0;
	}

	return (rc);
}

/*
 * Apply each fuse_opt that is applicable to the arg.  "apply" means to
 * scan or strdup if a %-format is present, or to "apply" it as in
 * fuse_opt_apply.  If the return value is one, we add the argument to
 * new.  And, if a template for an option contains a space, we may
 * concatenate the next argument before calling the callback.
 */

static int
fuse_opt_opt(struct fuse_args *old, struct fuse_args *new, const char *arg,
    const struct fuse_opt opts[], fuse_opt_proc_t proc, void *data)
{
	const struct fuse_opt *opt;
	const char *post;
	char buffy[BUFSIZ];
	uint_t sep;
	int save = 0;
	int rc = 0;

	opt = fuse_opt_next_match(opts, arg, &sep);
	if ((opt == NULL) || (opt->templ == NULL))
		return (fuse_opt_proc(new, arg, FUSE_OPT_KEY_OPT, proc, data));

	do {
		if (sep != 0) {
			post = opt->templ + sep;

			if (post[0] == '=') {
				++post;
				if (post[0] != '%') {
					rc = fuse_opt_proc(new, arg, opt->value,
					    proc, data);
					if (rc == 1)
						save = 1;
				} else {
					uintptr_t valp = (uintptr_t)data +
					    opt->offset;
					rc = fuse_opt_scan(arg + sep + 1, post,
					    (void *)valp);
				}
			} else {
				(void) strlcpy(buffy, arg, sizeof (buffy));
				if ((post[0] == ' ') && (arg[sep] == '\0') &&
				    (old->argv[0] != NULL)) {
					(void) strlcat(buffy, old->argv[0],
					    sizeof (buffy));
				}
				rc = fuse_opt_apply(new, buffy, opt,
				    proc, data);
			}
		} else {
			rc = fuse_opt_apply(new, arg, opt, proc, data);
			if (rc == 1)
				(void) fuse_opt_add_arg(new, arg);
		}

		++opt;
		opt = fuse_opt_next_match(opt, arg, &sep);
	} while ((rc != -1) && (opt != NULL) && (opt->templ != NULL));

	return (save);
}

static int
fuse_opt_optgroup(struct fuse_args *old, struct fuse_args *new, const char *arg,
    const struct fuse_opt opts[], fuse_opt_proc_t proc, char *data,
    char **allopts)
{
	char buffy[BUFSIZ], *opt, *lasts;
	int rc = 0;

	(void) strlcpy(buffy, arg, sizeof (buffy));
	for (opt = strtok_r(buffy, ", ", &lasts); opt != NULL;
	    opt = strtok_r(NULL, ", ", &lasts)) {
		rc = fuse_opt_opt(old, new, opt, opts, proc, data);
		if (rc == -1)
			break;
		if (rc == 1)
			rc = fuse_opt_add_opt(allopts, opt);
	}

	return (rc);
}

static int
fuse_opt_parse_all(struct fuse_args *old, struct fuse_args *new,
    const struct fuse_opt opts[], fuse_opt_proc_t proc, void *data,
    char **allopts)
{
	char *arg, buffy[BUFSIZ];
	int rc = 1;

	while ((rc != -1) && (old->argv[0] != NULL)) {
		if (strcmp(old->argv[0], "--") == 0) {
			rc = 0;
			break;
		}

		arg = fuse_opt_shift_arg(old, buffy, sizeof (buffy));
		if (arg == NULL) {
			rc = 0;
			break;
		}
		if (arg[0] != '-') {
			rc = fuse_opt_proc(new, arg, FUSE_OPT_KEY_NONOPT,
			    proc, data);
			if (rc == 1)
				(void) fuse_opt_add_arg(new, arg);
		} else if (strcmp(arg, "-o") == 0) {
			arg = fuse_opt_shift_arg(old, buffy, sizeof (buffy));
			rc = fuse_opt_optgroup(old, new, arg, opts, proc, data,
			    allopts);
		} else if (strncmp(arg, "-o ", 3) == 0) {
			arg += 3;
			rc = fuse_opt_optgroup(old, new, arg, opts, proc, data,
			    allopts);
		} else {
			rc = fuse_opt_opt(old, new, arg, opts, proc, data);
			if (rc == 1)
				(void) fuse_opt_add_arg(new, arg);
		}
	}

	if (rc == 1)
		rc = 0;
	return (rc);
}

/*
 * Parse the arguments and return the result.  If the return is -1, there was
 * a problem.  Otherwise, args is altered such that the options, if any, are
 * moved to the front in a single "-o a,b,c" pattern, and other arguments are
 * kept or discarded according to the rules laid out in fuse_opt.h.
 */

int
fuse_opt_parse(struct fuse_args *args, void *data,
    const struct fuse_opt opts[], fuse_opt_proc_t proc)
{
	struct fuse_args *newargs;
	char buffy[BUFSIZ];
	char *allopts = NULL;
	int rc;

	if ((args == NULL) || (args->argc == 0) || (args->argv[0] == NULL) ||
	    (opts == NULL) || (opts[0].templ == NULL))
		return (0);

	assert(args->argv[args->argc] == NULL);

	newargs = umem_zalloc(sizeof (*newargs), UMEM_NOFAIL);

	(void) fuse_opt_add_arg(newargs, fuse_opt_shift_arg(args, buffy,
	    sizeof (buffy)));
	rc = fuse_opt_parse_all(args, newargs, opts, proc, data, &allopts);

	if (allopts != NULL) {
		(void) fuse_opt_insert_arg(newargs, 1, "-o");
		(void) fuse_opt_insert_arg(newargs, 2, allopts);
		libfuse_strfree(allopts);
	}

	if (rc != -1) {
		fuse_opt_free_args(args);
		*args = *newargs; // structure assign
	} else {
		fuse_opt_free_args(newargs);
	}

	umem_free(newargs, sizeof (*newargs));

	return (rc);
}
