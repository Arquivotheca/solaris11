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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#define	_LARGEFILE64_SOURCE

/*
 * ELF-centric version of file utility.
 */
#include	<fcntl.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<_libelf.h>
#include	<stdarg.h>
#include	<unistd.h>
#include	<libgen.h>
#include	<libintl.h>
#include	<locale.h>
#include	<errno.h>
#include	<strings.h>
#include	<conv.h>
#include	<msg.h>
#include	<elf_file.h>


const char *
_elffile_msg(Msg mid)
{
	return (gettext(MSG_ORIG(mid)));
}

/*
 * The full usage message
 */
static void
detail_usage()
{
	(void) fprintf(stderr, MSG_INTL(MSG_USAGE_DETAIL_L_S));
}

int
main(int argc, char **argv, char **envp)
{
	int		var, fd, ret;
	elf_file_ar_t	style;

	/*
	 * If we're on a 64-bit kernel, try to exec a full 64-bit version of
	 * the binary.  If successful, conv_check_native() won't return.
	 */
	(void) conv_check_native(argv, envp);

	/*
	 * Establish locale.
	 */
	(void) setlocale(LC_MESSAGES, MSG_ORIG(MSG_STR_EMPTY));
	(void) textdomain(MSG_ORIG(MSG_SUNW_OST_SGS));

	(void) setvbuf(stdout, NULL, _IOLBF, 0);
	(void) setvbuf(stderr, NULL, _IOLBF, 0);

	opterr = 0;
	style = ELF_FILE_AR_SUMMARY;
	while ((var = getopt(argc, argv, MSG_ORIG(MSG_STR_OPTIONS))) != EOF) {
		switch (var) {
		case 's':
			if (strcmp(optarg, MSG_ORIG(MSG_STR_SUMMARY)) == 0) {
				style = ELF_FILE_AR_SUMMARY;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_STR_DETAIL)) == 0) {
				style = ELF_FILE_AR_DETAIL;
			} else if (strcmp(optarg,
			    MSG_ORIG(MSG_STR_BASIC)) == 0) {
				style = ELF_FILE_AR_BASIC;
			} else {
				(void) fprintf(stderr,
				    MSG_INTL(MSG_ERR_BAD_STYLE),
				    basename(argv[0]), optarg);
				return (1);
			}
			break;

		case '?':
			(void) fprintf(stderr, MSG_INTL(MSG_USAGE_BRIEF),
			    basename(argv[0]));
			detail_usage();
			return (1);
		default:
			break;
		}
	}

	/* There needs to be at least 1 filename left following the options */
	if ((var = argc - optind) == 0) {
		(void) fprintf(stderr, MSG_INTL(MSG_USAGE_BRIEF),
		    basename(argv[0]));
		return (1);
	}

	(void) elf_version(EV_CURRENT);

	/*
	 * Open each input file and process it.
	 */
	ret = 0;
	for (; (optind < argc) && (ret == 0); optind++) {
		const char	*file = argv[optind];

		if ((fd = open(argv[optind], O_RDONLY)) == -1) {
			int err = errno;
			(void) fprintf(stderr, MSG_INTL(MSG_ERR_OPEN),
			    file, strerror(err));
			continue;
		}

		(void) printf(MSG_ORIG(MSG_FMT_FILE), file);
		switch (_elf_file(basename(argv[0]), file, 1,
		    style, fd, NULL, 0)) {
		case ELF_FILE_SUCCESS:
			(void) putchar('\n');
			break;
		case ELF_FILE_FATAL:
			ret = 1;
			break;
		case ELF_FILE_NOTELF:
			(void) printf(MSG_INTL(MSG_FILE_NONELF));
			break;
		}

		(void) close(fd);
	}

	return (ret);
}
