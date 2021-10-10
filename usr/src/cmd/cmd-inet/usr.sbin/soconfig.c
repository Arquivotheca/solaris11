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
 * Copyright (c) 1995, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stddef.h>
#include <unistd.h>
#include <ofmt.h>
#include <libinetutil.h>

#define	MAXLINELEN	4096

/*
 * Usage:
 *	soconfig -d <dir>
 *		Reads input from files in dir.
 *
 *	soconfig -f <file>
 *		Reads input from file. The file is structured as
 *			 <fam> <type> <protocol> <path|module>
 *			 <fam> <type> <protocol>
 *		with the first line registering and the second line
 *		deregistering.
 *
 *	soconfig <fam> <type> <protocol> <path|module>
 *		registers
 *
 *	soconfig <fam> <type> <protocol>
 *		deregisters
 *
 *	soconfig -l [-np]
 *		Prints in-kernel socket table
 *		-n Do not show ascii representation of fields
 *		-p Machine parse-able output
 *
 * Filter Operations (Consolidation Private):
 *
 *	soconfig -F <name> <modname> {auto [top | bottom | before:filter |
 *		after:filter] | prog} <fam>:<type>:<proto>,...
 *		configure filter
 *
 *	soconfig -F <name>
 *		unconfigures filter
 */

static int	parse_files_in_dir(const char *dir);

static int	parse_file(char *filename);

static int	split_line(char *line, char *argvec[], int maxargvec);

static int	parse_params(char *famstr, char *typestr, char *protostr,
				char *path, const char *file, int line);

static int	parse_int(char *str);

static void	usage(void);

static int	parse_filter_params(int argc, char **argv);

static int	print_sock_params_table();

static boolean_t print_default_cb(ofmt_arg_t *, char *, uint_t);

#define	LIST_FLAG	0x0001
#define	STR_CONV_FLAG	0x0002
#define	PARSE_FLAG	0x0004
#define	DIR_FLAG	0x0008
#define	FILE_FLAG	0x0010
#define	FILTER_FLAG	0x0020

#define	EXCL_FLAGS	(DIR_FLAG | FILE_FLAG | FILTER_FLAG)
int
main(argc, argv)
	int argc;
	char *argv[];
{
	char *file_name, *dir_name;
	int ret;
	int c;
	int option_mask = 0;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	while ((c = getopt(argc, argv, "lnpd:f:F:")) != -1) {
		switch (c) {
		case 'l':
			if (option_mask & EXCL_FLAGS) {
				goto on_error;
			} else {
				option_mask |= LIST_FLAG;
			}
			break;
		case 'n':
			if (option_mask & EXCL_FLAGS) {
				goto on_error;
			} else {
				option_mask |= STR_CONV_FLAG;
			}
			break;
		case 'p':
			if (option_mask & EXCL_FLAGS) {
				goto on_error;
			} else {
				option_mask |= PARSE_FLAG;
			}
			break;
		case 'd':
			if (option_mask != 0 || (argc != optind)) {
				goto on_error;
			} else {
				option_mask |= DIR_FLAG;
				dir_name = optarg;
			}
			break;
		case 'f':
			if (option_mask != 0 || (argc != optind)) {
				goto on_error;
			} else {
				option_mask |= FILE_FLAG;
				file_name = optarg;
			}
			break;
		case 'F':
			if (option_mask != 0) {
				goto on_error;
			} else {
				option_mask |= FILTER_FLAG;
			}
			break;
		default:
			goto on_error;
		}
	}

	if (option_mask & (LIST_FLAG | STR_CONV_FLAG | PARSE_FLAG)) {
		if (argc != optind || !(option_mask & LIST_FLAG))
			goto on_error;
		ret = print_sock_params_table(option_mask);
		exit(ret);
	}
	if (option_mask == DIR_FLAG) {
		ret = parse_files_in_dir(dir_name);
		exit(ret);
	}
	if (option_mask == FILE_FLAG) {
		ret = parse_file(file_name);
		exit(ret);
	}

	argc--;
	argv++;

	if (option_mask == FILTER_FLAG) {
		argc--;
		argv++;
		ret = parse_filter_params(argc, argv);
		exit(ret);
	}

	if (argc == 3) {
		ret = parse_params(argv[0], argv[1], argv[2], NULL, NULL, -1);
		exit(ret);
	}
	if (argc == 4) {
		ret = parse_params(argv[0], argv[1], argv[2], argv[3],
		    NULL, -1);
		exit(ret);
	}
on_error:
	usage();
	exit(1);
	/* NOTREACHED */
}

static void
usage(void)
{
	fprintf(stderr, gettext(
	    "Usage:	soconfig -d <dir>\n"
	    "\tsoconfig -f <file>\n"
	    "\tsoconfig <fam> <type> <protocol> <path|module>\n"
	    "\tsoconfig <fam> <type> <protocol>\n"
	    "\tsoconfig -l [-np]\n"));
}

/*
 * Parse all files in the given directory.
 */
static int
parse_files_in_dir(const char *dirname)
{
	DIR		*dp;
	struct dirent 	*dirp;
	struct stat	stats;
	char		buf[MAXPATHLEN];
	int		rval = 0;

	if ((dp = opendir(dirname)) == NULL) {
		fprintf(stderr, gettext("failed to open directory '%s': %s\n"),
		    dirname, strerror(errno));
		return (1);
	}

	while ((dirp = readdir(dp)) != NULL) {
		if (dirp->d_name[0] == '.')
			continue;

		if (snprintf(buf, sizeof (buf), "%s/%s", dirname,
		    dirp->d_name) >= sizeof (buf)) {
			fprintf(stderr,
			    gettext("path name is too long: %s/%s\n"),
			    dirname, dirp->d_name);
			continue;
		}
		if (stat(buf, &stats) == -1) {
			fprintf(stderr,
			    gettext("failed to stat '%s': %s\n"), buf,
			    strerror(errno));
			continue;
		}
		if (!S_ISREG(stats.st_mode))
			continue;

		if (parse_file(buf) != 0)
			rval = 1;
	}

	closedir(dp);

	return (rval);
}

/*
 * Open the specified file and parse each line. Skip comments (everything
 * after a '#'). Return 1 if at least one error was encountered; otherwise 0.
 */
static int
parse_file(char *filename)
{
	char line[MAXLINELEN];
	char pline[MAXLINELEN];
	int argcount;
	char *argvec[20];
	FILE *fp;
	int linecount = 0;
	int numerror = 0;

	fp = fopen(filename, "r");
	if (fp == NULL) {
		perror("soconfig: open");
		fprintf(stderr, "\n");
		usage();
		return (1);
	}

	while (fgets(line, sizeof (line) - 1, fp) != NULL) {
		linecount++;
		strcpy(pline, line);
		argcount = split_line(pline, argvec,
		    sizeof (argvec) / sizeof (argvec[0]));
#ifdef DEBUG
		{
			int i;

			printf("scanned %d args\n", argcount);
			for (i = 0; i < argcount; i++)
				printf("arg[%d]: %s\n", i, argvec[i]);
		}
#endif /* DEBUG */
		switch (argcount) {
		case 0:
			/* Empty line - or comment only line */
			break;
		case 3:
			numerror += parse_params(argvec[0], argvec[1],
			    argvec[2], NULL, filename, linecount);
			break;
		case 4:
			numerror += parse_params(argvec[0], argvec[1],
			    argvec[2], argvec[3], filename, linecount);
			break;
		default:
			numerror++;
			fprintf(stderr,
			    gettext("Malformed line: <%s>\n"), line);
			fprintf(stderr,
			    gettext("\ton line %d in %s\n"), linecount,
			    filename);
			break;
		}
	}
	(void) fclose(fp);

	if (numerror > 0)
		return (1);
	else
		return (0);
}

/*
 * Parse a line splitting it off at whitspace characters.
 * Modifies the content of the string by inserting NULLs.
 */
static int
split_line(char *line, char *argvec[], int maxargvec)
{
	int i = 0;
	char *cp;

	/* Truncate at the beginning of a comment */
	cp = strchr(line, '#');
	if (cp != NULL)
		*cp = NULL;

	/* CONSTCOND */
	while (1) {
		/* Skip any whitespace */
		while (isspace(*line) && *line != NULL)
			line++;

		if (i >= maxargvec)
			return (i);

		argvec[i] = line;
		if (*line == NULL)
			return (i);
		i++;
		/* Skip until next whitespace */
		while (!isspace(*line) && *line != NULL)
			line++;
		if (*line != NULL) {
			/* Break off argument */
			*line++ = NULL;
		}
	}
	/* NOTREACHED */
}

/*
 * Parse the set of parameters and issues the sockconfig syscall.
 * If line is not -1 it is assumed to be the line number in the file.
 */
static int
parse_params(char *famstr, char *typestr, char *protostr, char *path,
    const char *file, int line)
{
	int cmd, fam, type, protocol;

	fam = parse_int(famstr);
	if (fam == -1) {
		fprintf(stderr, gettext("Bad family number: %s\n"), famstr);
		if (line != -1)
			fprintf(stderr,
			    gettext("\ton line %d in %s\n"), line, file);
		else {
			fprintf(stderr, "\n");
			usage();
		}
		return (1);
	}

	type = parse_int(typestr);
	if (type == -1) {
		fprintf(stderr,
		    gettext("Bad socket type number: %s\n"), typestr);
		if (line != -1)
			fprintf(stderr,
			    gettext("\ton line %d in %s\n"), line, file);
		else {
			fprintf(stderr, "\n");
			usage();
		}
		return (1);
	}

	protocol = parse_int(protostr);
	if (protocol == -1) {
		fprintf(stderr,
		    gettext("Bad protocol number: %s\n"), protostr);
		if (line != -1)
			fprintf(stderr,
			    gettext("\ton line %d in %s\n"), line, file);
		else {
			fprintf(stderr, "\n");
			usage();
		}
		return (1);
	}


	if (path != NULL) {
		struct stat stats;

		if (strncmp(path, "/dev", strlen("/dev")) == 0 &&
		    stat(path, &stats) == -1) {
			perror(path);
			if (line != -1)
				fprintf(stderr,
				    gettext("\ton line %d in %s\n"), line,
				    file);
			else {
				fprintf(stderr, "\n");
				usage();
			}
			return (1);
		}

		cmd = SOCKCONFIG_ADD_SOCK;
	} else {
		cmd = SOCKCONFIG_REMOVE_SOCK;
	}

#ifdef DEBUG
	printf("not calling sockconfig(%d, %d, %d, %d, %s)\n",
	    cmd, fam, type, protocol, path == NULL ? "(null)" : path);
#else
	if (_sockconfig(cmd, fam, type, protocol, path) == -1) {
		char *s;

		switch (errno) {
		case EALREADY:
			/*
			 * The parameters already map to the given
			 * module/device. Do not treat that as an error.
			 */
			return (0);
		case EEXIST:
			s = gettext("Mapping exists");
			break;
		default:
			s = strerror(errno);
			break;
		}

		fprintf(stderr,
		    gettext("warning: socket configuration failed "
		    "for family %d type %d protocol %d: %s\n"),
		    fam, type, protocol, s);
		if (line != -1) {
			fprintf(stderr,
			    gettext("\ton line %d in %s\n"), line, file);
		}
		return (1);
	}
#endif
	return (0);
}

static int
parse_int(char *str)
{
	char *end;
	int res;

	res = strtol(str, &end, 0);
	if (end == str)
		return (-1);
	return (res);
}

/*
 * Add and remove socket filters.
 */
static int
parse_filter_params(int argc, char **argv)
{
	struct sockconfig_filter_props filprop;
	sof_socktuple_t *socktuples;
	size_t tupcnt, nalloc;
	char *hintarg, *socktup, *tupstr;
	int i;

	if (argc == 1) {
		if (_sockconfig(SOCKCONFIG_REMOVE_FILTER, argv[0], 0,
		    0, 0) < 0) {
			switch (errno) {
			case ENXIO:
				fprintf(stderr,
				    gettext("socket filter is not configured "
				    "'%s'\n"), argv[0]);
				break;
			default:
				perror("sockconfig");
				break;
			}
			return (1);
		}
		return (0);
	}

	if (argc < 4 || argc > 5)
		return (1);


	if (strlen(argv[1]) >= MODMAXNAMELEN) {
		fprintf(stderr,
		    gettext("invalid module name '%s': name too long\n"),
		    argv[1]);
		return (1);
	}
	filprop.sfp_modname = argv[1];

	/* Check the attach semantics */
	if (strcmp(argv[2], "auto") == 0) {
		filprop.sfp_autoattach = B_TRUE;
		if (argc == 5) {
			/* placement hint */
			if (strcmp(argv[3], "top") == 0) {
				filprop.sfp_hint = SOF_HINT_TOP;
			} else if (strcmp(argv[3], "bottom") == 0) {
				filprop.sfp_hint = SOF_HINT_BOTTOM;
			} else {
				if (strncmp(argv[3], "before", 6) == 0) {
					filprop.sfp_hint = SOF_HINT_BEFORE;
				} else if (strncmp(argv[3], "after", 5) == 0) {
					filprop.sfp_hint = SOF_HINT_AFTER;
				} else {
					fprintf(stderr,
					    gettext("invalid placement hint "
					    "'%s'\n"), argv[3]);
					return (1);
				}

				hintarg = strchr(argv[3], ':');
				if (hintarg == NULL ||
				    (strlen(++hintarg) == 0) ||
				    (strlen(hintarg) >= FILNAME_MAX)) {
					fprintf(stderr,
					    gettext("invalid placement hint "
					    "argument '%s': name too long\n"),
					    argv[3]);
					return (1);
				}

				filprop.sfp_hintarg = hintarg;
			}
		} else {
			filprop.sfp_hint = SOF_HINT_NONE;
		}
	} else if (strcmp(argv[2], "prog") == 0) {
		filprop.sfp_autoattach = B_FALSE;
		filprop.sfp_hint = SOF_HINT_NONE;
		/* cannot specify placement hint for programmatic filter */
		if (argc == 5) {
			fprintf(stderr,
			    gettext("placement hint specified for programmatic "
			    "filter\n"));
			return (1);
		}
	} else {
		fprintf(stderr, gettext("invalid attach semantic '%s'\n"),
		    argv[2]);
		return (1);
	}

	/* parse the socket tuples */
	nalloc = 4;
	socktuples = calloc(nalloc, sizeof (sof_socktuple_t));
	if (socktuples == NULL) {
		perror("calloc");
		return (1);
	}

	tupcnt = 0;
	tupstr = argv[(argc == 4) ? 3 : 4];
	while ((socktup = strsep(&tupstr, ",")) != NULL) {
		int val;
		char *valstr;

		if (tupcnt == nalloc) {
			sof_socktuple_t *new;

			nalloc *= 2;
			new = realloc(socktuples,
			    nalloc * sizeof (sof_socktuple_t));
			if (new == NULL) {
				perror("realloc");
				free(socktuples);
				return (1);
			}
			socktuples = new;
		}
		i = 0;
		while ((valstr = strsep(&socktup, ":")) != NULL && i < 3) {
			val = parse_int(valstr);
			if (val == -1) {
				fprintf(stderr, gettext("bad socket tuple\n"));
				free(socktuples);
				return (1);
			}
			switch (i) {
			case 0:	socktuples[tupcnt].sofst_family = val; break;
			case 1:	socktuples[tupcnt].sofst_type = val; break;
			case 2:	socktuples[tupcnt].sofst_protocol = val; break;
			}
			i++;
		}
		if (i != 3) {
			fprintf(stderr, gettext("bad socket tuple\n"));
			free(socktuples);
			return (1);
		}
		tupcnt++;
	}
	if (tupcnt == 0) {
		fprintf(stderr, gettext("no socket tuples specified\n"));
		free(socktuples);
		return (1);
	}
	filprop.sfp_socktuple_cnt = tupcnt;
	filprop.sfp_socktuple = socktuples;

	if (_sockconfig(SOCKCONFIG_ADD_FILTER, argv[0], &filprop, 0, 0) < 0) {
		switch (errno) {
		case EINVAL:
			fprintf(stderr,
			    gettext("invalid socket filter configuration\n"));
			break;
		case EEXIST:
			fprintf(stderr,
			    gettext("socket filter is already configured "
			    "'%s'\n"), argv[0]);
			break;
		case ENOSPC:
			fprintf(stderr, gettext("unable to satisfy placement "
			    "constraint\n"));
			break;
		default:
			perror("sockconfig");
			break;
		}
		free(socktuples);
		return (1);
	}
	free(socktuples);
	return (0);
}


/*
 * Since the socket filters are Consolidation Private, their information is
 * not printed.
 */

#define	FAM_PRNT_LEN 17
#define	TYP_PRNT_LEN 15
#define	PRO_PRNT_LEN 15
#define	MOD_PRNT_LEN 20
#define	LOD_PRNT_LEN 5

enum field {
	F_FAMILY = 1,
	F_TYPE,
	F_PROTO,
	F_MOD,
	F_LOAD
};

static const ofmt_field_t soconfig_fields[] = {
{"FAMILY",	FAM_PRNT_LEN,	F_FAMILY,	print_default_cb},
{"TYPE", 	TYP_PRNT_LEN,	F_TYPE,		print_default_cb},
{"PROTO",	PRO_PRNT_LEN,	F_PROTO,	print_default_cb},
{"MOD/DEV",	MOD_PRNT_LEN,	F_MOD,		print_default_cb},
{"LOADED",	LOD_PRNT_LEN,	F_LOAD,		print_default_cb},
{NULL,		0,		0,		NULL}
};


static struct so_ofmt {
	sock_config_t	*sofmt_configp;
	boolean_t	sofmt_cvt;
} so_ofmt_s;

static boolean_t
print_default_cb(ofmt_arg_t *ofarg, char *buf, uint_t bufsize)
{
	struct so_ofmt *so_ofmt_p = (struct so_ofmt *)ofarg->ofmt_cbarg;
	sock_config_t *configp = so_ofmt_p->sofmt_configp;
	const char *cvt_str;

	switch (ofarg->ofmt_id) {
	case F_FAMILY:
		if (so_ofmt_p->sofmt_cvt &&
		    (cvt_str = soaf2str(configp->sc_family)) != NULL) {
			(void) snprintf(buf, bufsize, "%s", cvt_str);
		} else {
			(void) sprintf(buf, "%d", configp->sc_family);
		}
		break;
	case F_TYPE:
		if (so_ofmt_p->sofmt_cvt &&
		    (cvt_str = sotype2str(configp->sc_type)) != NULL) {
			(void) snprintf(buf, bufsize, "%s", cvt_str);
		} else {
			(void) sprintf(buf, "%d", configp->sc_type);
		}
		break;
	case F_PROTO:
		if (so_ofmt_p->sofmt_cvt &&
		    (cvt_str = ipproto2str(configp->sc_family,
		    configp->sc_protocol)) != NULL) {
			(void) snprintf(buf, bufsize, "%s", cvt_str);
		} else {
			(void) sprintf(buf, "%d", configp->sc_protocol);
		}
		break;
	case F_MOD: {
		int offset;

		offset = strlen(configp->sc_modname) - MOD_PRNT_LEN;
		if (offset > 0)
			(void) snprintf(buf, MOD_PRNT_LEN, "%s",
			    (configp->sc_modname + offset));
		else
			(void) snprintf(buf, MOD_PRNT_LEN, "%s",
			    configp->sc_modname);
		}
		break;
	case F_LOAD:
		(void) sprintf(buf, "%c", configp->sc_loaded ? 'Y':'N');
		break;
	default:
		return (B_FALSE);
	}
	return (B_TRUE);
}

static int
print_sock_params_table(int option_mask)
{
	int num_of_entries;
	sock_config_t *configp;
	int size;
	uint_t ofmtflags = 0;
	ofmt_status_t oferr;
	ofmt_handle_t ofmt;
	char *all_fields = "family,type,proto,mod/dev,loaded";
	boolean_t cvt2ascii = B_TRUE;

	if (STR_CONV_FLAG & option_mask)
		cvt2ascii = B_FALSE;
	if (PARSE_FLAG & option_mask)
		ofmtflags |= OFMT_PARSABLE;

	if (_sockconfig(SOCKCONFIG_GET_NUM_ENTRIES, &num_of_entries,
	    sizeof (num_of_entries), 0, 0) < 0) {
		fprintf(stderr,
		    gettext("Could not retrieve params table: %s\n"),
		    strerror(errno));
		return (1);
	}

	if (num_of_entries <= 0) {
		return (0);
	}

	size = num_of_entries * sizeof (sock_config_t);
	configp = malloc(size);
	if (configp == NULL) {
		fprintf(stderr, gettext("Could not allocate memory\n"));
		return (1);
	}
	num_of_entries = 0;
	if (_sockconfig(SOCKCONFIG_GET_ENTRIES, configp, size,
	    &num_of_entries, sizeof (num_of_entries)) < 0) {
		fprintf(stderr,
		    gettext("Could not retrieve params table: %s\n"),
		    strerror(errno));
		return (1);
	}

	oferr = ofmt_open(all_fields, soconfig_fields, ofmtflags, 0, &ofmt);
	if (oferr != OFMT_SUCCESS) {
		char buf[80];
		ofmt_strerror(ofmt, oferr, buf, sizeof (buf));
		fprintf(stderr, gettext("ofmt_open failed: %s"), buf);
		return (1);
	}

	while (num_of_entries > 0) {
		so_ofmt_s.sofmt_configp = configp;
		so_ofmt_s.sofmt_cvt = cvt2ascii;
		ofmt_print(ofmt, &so_ofmt_s);
		configp++;
		num_of_entries--;
	}
	ofmt_close(ofmt);
	return (0);
}
