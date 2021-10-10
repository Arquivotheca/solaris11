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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdlib.h>
#include <strings.h>
#include "libdevinfo.h"

/* record field options information */
static struct f {		/* field table */
	char	f_fchar;	/* start initialized */
	char	f_public;
	char	f_multi_value;	/* Can contain multiple values */
	char	*f_fname;
	char	*f_comment;
	char	*(*f_fgeti)(di_cro_rec_t, int, int *, char *);

	int	f_i;		/* start computed */
	int	f_w;
	char	*f_re;
} f_t[] = {
	{'P', 1, 0, DI_CRO_Q_PRODUCT_ID,
			"product id ",
			di_cro_rec_fgeti_product_id, },
	{'C', 1, 0, DI_CRO_Q_CHASSIS_ID,
			"chassis id (serial number)",
			di_cro_rec_fgeti_chassis_id, },
	{'A', 1, 0, DI_CRO_Q_ALIAS_ID,
			"fmadm(1M) 'managed' alias",
			di_cro_rec_fgeti_alias_id, },
	{'R', 1, 0, DI_CRO_Q_RECEPTACLE_NAME,
			"silk-screen label path: like 'DS_0/HDD_0'",
			di_cro_rec_fgeti_receptacle_name, },
	{'T', 1, 0, DI_CRO_Q_RECEPTACLE_TYPE,
			"like 'bay'",
			di_cro_rec_fgeti_receptacle_type, },
	{'F', 0, 0, DI_CRO_Q_RECEPTACLE_FMRI,
			"PRIVATE: libtopo FMRI",
			di_cro_rec_fgeti_receptacle_fmri, },
	{'t', 1, 0, DI_CRO_Q_OCCUPANT_TYPE,
			"type of current occupant",
			di_cro_rec_fgeti_occupant_type, },
	{'z', 0, 0, DI_CRO_Q_OCCUPANT_INSTANCE,
			"PRIVATE: instance of occupant type",
			di_cro_rec_fgeti_occupant_instance, },
	{'D', 1, 0, DI_CRO_Q_DEVCHASSIS_PATH,
			"/dev/chassis path to occupant",
			di_cro_rec_fgeti_devchassis_path, },
	{'d', 1, 1, DI_CRO_Q_OCCUPANT_DEVICES,
			"/devices link",
			di_cro_rec_fgeti_occupant_devices, },
	{'p', 1, 1, DI_CRO_Q_OCCUPANT_PATHS,
			"/devices paths",
			di_cro_rec_fgeti_occupant_paths, },
	{'c', 1, 1, DI_CRO_Q_OCCUPANT_COMPDEV,
			"/dev whole-device component: like c0t0d0",
			di_cro_rec_fgeti_occupant_compdev, },
	{'i', 1, 0, DI_CRO_Q_OCCUPANT_DEVID,
			"identity",
			di_cro_rec_fgeti_occupant_devid, },
	{'m', 1, 0, DI_CRO_Q_OCCUPANT_MFG,
			"manufacturer",
			di_cro_rec_fgeti_occupant_mfg, },
	{'e', 1, 0, DI_CRO_Q_OCCUPANT_MODEL,
			"model",
			di_cro_rec_fgeti_occupant_model, },
	{'n', 1, 0, DI_CRO_Q_OCCUPANT_PART,
			"part number",
			di_cro_rec_fgeti_occupant_part, },
	{'s', 1, 0, DI_CRO_Q_OCCUPANT_SERIAL,
			"serial number",
			di_cro_rec_fgeti_occupant_serial, },
	{'f', 1, 0, DI_CRO_Q_OCCUPANT_FIRM,
			"firmware version",
			di_cro_rec_fgeti_occupant_firm, },
	{'1', 1, 1, DI_CRO_Q_OCCUPANT_MISC_1,
			"misc",
			di_cro_rec_fgeti_occupant_misc_1, },
	{'2', 1, 1, DI_CRO_Q_OCCUPANT_MISC_2,
			"misc",
			di_cro_rec_fgeti_occupant_misc_2, },
	{'3', 1, 1, DI_CRO_Q_OCCUPANT_MISC_3,
			"misc",
			di_cro_rec_fgeti_occupant_misc_3, },

	{0, NULL, }	/* end of table */
};

/* command information */
static struct c {		/* hard-linked commands... */
	char	*c_cmd;		/* command: croinfo/diskinfo/etc... */
	char	c_f_fchar;	/* flag filter associated with command */
	char	*c_f_re;	/* flag filter RE associated with command */
	char	*c_f_o_fchar;	/* default -o <fields> for command output */
	char	*c_f_o_fname;	/* default -O <fields> for command output */
	char	*c_misc_1;	/* header for misc_1 */
	char	*c_misc_2;	/* header for misc_2 */
	char	*c_misc_3;	/* header for misc_3 */
} c_t[] = {
	/* generic command: */
	{"croinfo",	0,	NULL,	"Dtc",
			"devchassis-path,occupant-type,occupant-compdev",
			NULL, NULL, NULL},
	/* disk-specific command: disks occupy bays */
	{"diskinfo",	'T',	"bay",	 "Dc",
			"devchassis-path,occupant-compdev",
			"occupant-capacity", "occupant-target-ports", NULL},
	/* future expansion.... */
	{NULL}			/* end of table */
};

char	*cmd;		/* cmd name */
char	*o_fchar;	/* output -o <field-char> form */
char	*o_fname;	/* output -O <field-name> form */
char	*o_d_fchar;	/* output -o <field-char> form */
char	*o_d_fname;	/* output -O <field-name> form */
char	opt[128];	/* field flag options */
char	gopt[256];	/* field flag options - getopt form */
int	ff = 0;		/* field flags specified to CLI */
int	rec_flag = 0;
char	*query = NULL;
char	*cro_db_file = NULL;
char	*dash = "------------------------------------------------------------"
	"--------------------------------------------------------------------";

static void
usage()
{
	char		fname[64];
	struct f	*f;

	(void) fprintf(stderr, "%s\nUsage\n"
	    "  %-6.6s  %-25.25s  %-43.43s\n"
	    "  %-6.6s  %-25.25s  %-43.43s\n"
	    "  %-6.6s  %-25.25s  %-43.43s\n", cmd,
	    "<field", "<field-name>", "",
	    "-char>", "Regular_Expression", "Description",
	    dash, dash, dash);
	for (f = f_t; f->f_fname; f++) {
		(void) snprintf(fname, sizeof (fname), "<%s>", f->f_fname);
		if (f->f_public)
			(void) fprintf(stderr, "      -%c  %-25.25s  %s\n",
			    f->f_fchar, fname, f->f_comment);
	}
	(void) fprintf(stderr, "\n\n");

	/* non-field-flag options */
	(void) fprintf(stderr,
	    "      -o %-8.8s output '<field-char>[...]' "
	    "or '<field-name>[,...]' <fields>\n"
	    "         %-8.8s in aligned, white-space separated, "
	    "human-readable format.\n"
	    "         %-8.8s default: '-o %s'\n"
	    "      -O %-8.8s output '<field-char>[...]' "
	    "or '<field-name>[,...]' <fields>\n"
	    "         %-8.8s in ':' separated, parsable format.\n"
	    "         %-8.8s default: '-O %s'\n"
	    "      -h %-8.8s don't print headers\n"
	    "      -v %-8.8s print verbose header\n"
	    "      -I %-8.8s data file to obtain information from\n"
	    "      -? %-8.8s this 'usage' information\n",
	    "<fields>", "", "", o_d_fchar,
	    "<fields>", "", "", o_d_fname,
	    "", "", "<cro_db>", "");

	/* NOTE:  -Z		is private */
	/* NOTE:  -q <query>	is private */

	exit(0);
	/*NOTREACHED*/
}

static void
setup(char *argv[])
{
	struct c	*c;
	struct f	*f;
	char		*o, *go;
	int		i;

	cmd = strrchr(argv[0], '/');
	if (cmd)
		cmd++;
	else
		cmd = argv[0];
	for (c = c_t; c->c_cmd; c++)
		if (strcmp(c->c_cmd, cmd) == 0)
			break;
	if (c->c_cmd == NULL)
		c = &c_t[0];		/* force generic command table */

	o_d_fchar = c->c_f_o_fchar;	/* default -o <field-char> output */
	o_d_fname = c->c_f_o_fname;	/* default -O <field-name> output */
	o_fchar = o_d_fchar;
	o_fname = o_d_fname;

	/* initialize field-flag opt and gopt from field table */
	for (f = f_t, o = opt, go = gopt, i = 0; f->f_fname; f++, i++) {
		f->f_i = i;

		/* apply cmd-specific filter */
		if (c->c_f_fchar && (f->f_fchar == c->c_f_fchar))
			f->f_re = c->c_f_re;	/* cmd implicit filter */
		else
			f->f_re = NULL;

		/* adjust to cmd-specific misc fields */
		if ((strcmp(f->f_fname, DI_CRO_Q_OCCUPANT_MISC_1) == 0) &&
		    c->c_misc_1)
			f->f_fname = f->f_comment = c->c_misc_1;
		if ((strcmp(f->f_fname, DI_CRO_Q_OCCUPANT_MISC_2) == 0) &&
		    c->c_misc_2)
			f->f_fname = f->f_comment = c->c_misc_2;
		if ((strcmp(f->f_fname, DI_CRO_Q_OCCUPANT_MISC_3) == 0) &&
		    c->c_misc_3)
			f->f_fname = f->f_comment = c->c_misc_3;

		if (f->f_public)
			*o++ = f->f_fchar;
		*go++ = f->f_fchar;
		*go++ = ':';
	}

	/* add non-field-flag getopt options */
	*go++ = 'h';			/* print field header */
	*go++ = 'I'; *go++ = ':';	/* -I <cro_db> file */
	*go++ = 'o'; *go++ = ':';	/* field-output filter */
	*go++ = 'O'; *go++ = ':';	/* field-output filter */
	*go++ = 'v';			/* print field verbose header */
	*go++ = '?';			/* print usage */

	*go++ = 'q'; *go++ = ':';	/* private: -q <query> */
	*go++ = 'Z';			/* private: -Z print private records */

	*o = *go = '\0';		/* terminate */
}

static int
find_fname_add_fchar(char *field, char *fchar, int len)
{
	struct f	*f;
	int		l;
	int		n;

	while ((*field == ' ') || (*field == '\t'))
		field++;
	l = strcspn(field, " \t");
	for (f = f_t; f->f_fname; f++) {
		n = strlen(f->f_fname);
		n = (n > l) ? n : l;
		if (strncmp(field, f->f_fname, n) == 0) {
			if (fchar) {
				l = strlen(fchar);
				if (l > len)
					return (0);
				fchar[l] = f->f_fchar;
			}
			return (1);
		}
	}
	return (0);
}

static char *
fname_to_fchar(char *optarg)
{
	char	*sep;
	char	*fname;
	char	fchar[256];
	char	*tmp_str;
	char	*s;

	if (optarg == NULL)
		return (NULL);

	(void) memset(fchar, 0, sizeof (fchar));

	/* remove white-space */
	tmp_str = strdup(optarg);
	if (tmp_str == NULL)
		return (NULL);
	for (s = optarg, fname = tmp_str; *s; s++) {
		if ((*s == ' ') || (*s == '\t'))
			continue;
		*fname++ = *s;
	}
	*fname = '\0';

	/*
	 * convert <field-name> to <field-char> one field at a time while
	 * building the fchar <field-char> equivalent.
	 */
	fname = tmp_str;
	sep = strchr(tmp_str, ',');
	while (sep) {
		*sep = NULL;
		if (!find_fname_add_fchar(fname, fchar, sizeof (fchar)))
			return (NULL);
		fname = sep+1;
		sep = strchr(fname, ',');
	}
	if (!find_fname_add_fchar(fname, fchar, sizeof (fchar))) {
		free(tmp_str);
		return (NULL);
	}
	free(tmp_str);
	return (strdup(fchar));
}

static char *
escape(char *si0)
{
	int	nc;
	char	*si;
	char	*so0, *so;

	if (si0 == NULL)
		return (NULL);		/* caller uses si0 */

	/* count number of characters we need to escape in input */
	for (si = si0, nc = 0; *si; si++)
		if ((*si == ':') || (*si == ','))
			nc++;
	if (nc == 0)
		return (NULL);		/* none - caller uses si0 */

	so0 = malloc(strlen(si0) + nc + 1);
	if (so0 == NULL)
		return (NULL);

	for (si = si0, so = so0; *si; si++, so++) {
		if ((*si == ':') || (*si == ','))
			*so++ = '\\';	/* escape ':' and ',' */
		*so = *si;
	}
	*so = '\0';
	return (so0);			/* caller uses so0, and must free it */
}

static void
print_parseable_fields(char *o_fchar, di_cro_rec_t r, int num_fields)
{
	int		i, more;
	char		*s;
	struct f	*f;
	int 		col;
	char		*v;
	char		*ev;

	for (col = 0, s = o_fchar; *s; s++) {
		for (i = 0, more = 1; more; i++) {
			for (f = f_t; f->f_fname; f++) {
				if (*s != f->f_fchar)
					continue;
				v = f->f_fgeti(r, i, &more, "");
				/*
				 * Check to see if multiple fields are being
				 * printed or if this field can have multiple
				 * values. Must escape special chars to make it
				 * parsable.
				 */
				ev = NULL;
				if ((num_fields > 1) ||
				    (f->f_multi_value == 1)) {
					ev = escape(v);
				}

				if (col) {
					(void) printf(":%s", ev ? ev : v);
				} else {
					(void) printf("%s", ev ? ev : v);
				}

				if (ev)
					free(ev);
				if (more) {
					(void) printf(",");
					col = -1;
				}
				col++;
			}
		}
	}
	(void) printf("\n");
}

static void
print_human_readable_fields(char *o_fchar, di_cro_rec_t	r)
{
	int		i, ma, more;
	char		*s;
	struct f	*f;
	int 		col;
	char		*v;

	/* Human readable output */
	for (i = 0, ma = 1; ma; i++) {
		for (ma = 0, col = 0, s = o_fchar; *s; s++) {
			for (f = f_t; f->f_fname; f++) {
				if (*s != f->f_fchar)
					continue;
				v = f->f_fgeti(r, i, &more, i ? " :" : "-");
				ma |= more;

				(void) printf("%s%*s", col ? "  " : "",
				    -f->f_w, v);
				col++;
			}
		}
		(void) printf("\n");
	}


}
int
main(int argc, char *argv[])
{
	extern char	*optarg;
	char		c;
	struct f	*f;
	int		iff;
	int		oheader = 1;
	int		verbose = 0;
	int		parse = 0;
	int		o_o = 0;
	char		*s;
	int		col;
	int		w;
	int		i, ma, m;
	di_cro_hdl_t	h;
	di_cro_reca_t	ra;
	di_cro_rec_t	r;
	int		num_fields = 0;

	setup(argv);

	while ((c = getopt(argc, argv, gopt)) != EOF) {
		iff = ff;
		for (f = f_t; f->f_fname; f++) {
			if (c != f->f_fchar)
				continue;
			if (ff & (1 << f->f_i))
				usage();	/* field flag only once */
			ff |= 1 << f->f_i;
			f->f_re = strdup(optarg);
		}
		if (iff != ff)
			continue;		/* was a field flag */

		switch (c) {
		default:
		case '?':
			usage();
			/*NOTREACHED*/

		case 'O':
			/* produce parsable output */
			parse = 1;
			oheader = 0;
			/*FALLTHRU*/
		case 'o':
			o_o++;
			if (o_o > 1) {
				(void) fprintf(stderr, "%s: too many -o and/or "
				    "-O flags specified\n", cmd);
				usage();
			}

			/*
			 * Determine if we have output fields specified in
			 * '<field-name>[,...]' or '<field-char>[...]' form.
			 *
			 * If we have a ',' or one <field-name> that makes sense
			 * then we are in <field-name> form, otherwise
			 * assume <field-char> form.
			 */
			if (strchr(optarg, ',') ||
			    find_fname_add_fchar(optarg, NULL, 0))
				o_fchar = fname_to_fchar(optarg);
			else
				o_fchar = strdup(optarg);
			if (o_fchar == NULL)
				usage();	/* bad <field-name> */

			/* verify/sanity-check <field-fchar> form */
			for (s = o_fchar; *s; s++) {
				for (f = f_t; f->f_fname; f++)
					if (*s == f->f_fchar) {
						num_fields++;
						break;
					}
				if (f->f_fname == NULL)
					usage();	/* bad <field-char> */
			}
			break;

		case 'h':			/* -h (no header) */
			oheader = 0;
			break;

		case 'I':			/* -I <cro_db_file> */
			cro_db_file = optarg;
			break;

		case 'q':			/* private: -q <query> */
			query = optarg;
			break;

		case 'v':			/* -v (verbose header) */
			verbose++;
			break;

		case 'Z':			/* private: -Z */
			rec_flag = DI_CRO_REC_FLAG_PRIV;
			break;
		}
	}

	/* Take a Chassis-Receptacle-Occupant snapshot with specified filters */
	h = di_cro_init(cro_db_file, 0);
	if (h == NULL) {
		(void) fprintf(stderr,
		    "%s: di_cro_init() failed: does '%s' exist?%s\n", cmd,
		    cro_db_file ? cro_db_file : DI_CRO_DB_FILE,
		    cro_db_file ? "" : " is 'svc:/system/fmd' running?");
		return (0);
	}
	if (verbose) {
		(void) printf("  %-12.12s %s\n",
		    "date", di_cro_get_date(h));
		(void) printf("  %-12.12s %s\n",
		    "server-id", di_cro_get_server_id(h));
		(void) printf("  %-12.12s %s\n",
		    "product-id", di_cro_get_product_id(h));
		(void) printf("  %-12.12s %s\n",
		    "chassis-id", di_cro_get_chassis_id(h));
		(void) printf("  %-12.12s %s\n",
		    "chksum", di_cro_get_fletcher(h));
		(void) printf("\n");
	}

	if (query)
		ra = di_cro_reca_create_query(h, rec_flag, query);
	else
		ra = di_cro_reca_create(h, rec_flag,
		    f_t[0].f_re, f_t[1].f_re, f_t[2].f_re,
		    f_t[3].f_re, f_t[4].f_re, f_t[5].f_re,
		    f_t[6].f_re, f_t[7].f_re, f_t[8].f_re,
		    f_t[9].f_re, f_t[10].f_re, f_t[11].f_re,
		    f_t[12].f_re, f_t[13].f_re, f_t[14].f_re,
		    f_t[15].f_re, f_t[16].f_re, f_t[17].f_re,
		    f_t[18].f_re, f_t[19].f_re, f_t[20].f_re);

	r = di_cro_reca_next(ra, NULL);
	if (r) {
		/* walk snapshot computing max output field widths */
		for (; r; r = di_cro_reca_next(ra, r)) {
			for (i = 0, ma = 1; ma; i++) {
				for (ma = 0, f = f_t; f->f_fname; f++) {
					s = f->f_fgeti(r, i, &m, " :");
					ma |= m;
					w = s ? strlen(s) : 0;
					if (w > f->f_w)
						f->f_w = w;
				}
			}
		}

		/* print header for desired output fields */
		if (oheader) {
			for (f = f_t; f->f_fname; f++) {
				w = 2 + strlen(f->f_fname);	/* %c: */
				if (w > f->f_w)
					f->f_w = w;
			}
			for (col = 0, s = o_fchar; *s; s++) {
				for (f = f_t; f->f_fname; f++) {
					if (*s != f->f_fchar)
						continue;
					(void) printf("%s%c:%*s",
					    col ? "  " : "", f->f_fchar,
					    -(f->f_w - 2), f->f_fname);
					col++;
				}
			}
			(void) printf("\n");
			for (col = 0, s = o_fchar; *s; s++) {
				for (f = f_t; f->f_fname; f++) {
					if (*s != f->f_fchar)
						continue;
					(void) printf("%s%.*s",
					    col ? "  " : "", f->f_w, dash);
					col++;
				}
			}
			(void) printf("\n");
		}

		/* walk snapshot printing desired output fields */
		for (r = di_cro_reca_next(ra, NULL); r;
		    r = di_cro_reca_next(ra, r)) {

			/* skip record if output fields are all empty */
			for (s = o_fchar; *s; s++) {
				for (f = f_t; f->f_fname; f++) {
					if ((*s == f->f_fchar) &&
					    f->f_fgeti(r, 0, NULL, NULL))
						break;
				}
				if (f->f_fname)
					break;
			}
			if (*s == 0)
				continue;

			/* output record fields */
			if (parse) {
				print_parseable_fields(o_fchar, r, num_fields);
			} else {
				print_human_readable_fields(o_fchar, r);
			}

		}
	}
	di_cro_reca_destroy(ra);

	di_cro_fini(h);
	exit(0);
	/*NOTREACHED*/
}
