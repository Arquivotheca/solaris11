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
#include <libdevinfo.h>
#include <strings.h>
#include <fmadm.h>


/* This sync will sync the users version of the aliases file */
/*ARGSUSED*/
int
cmd_alias_sync(fmd_adm_t *adm, int argc, char *argv[])
{
	di_pca_hdl_t	h;
	int		err = FMADM_EXIT_ERROR;

	/*
	 * Get handle to current pca records using user aliases. This will
	 * fail if there are syntax issues in the file, and with FLAG_PRINT
	 * the nature of the problem will be printed to stderr by the
	 * di_pca_init().
	 */
	if ((h = di_pca_init(DI_PCA_INIT_FLAG_PRINT |
	    DI_PCA_INIT_FLAG_ALIASES_FILE_USER)) == NULL)
		die("failed to initialize: %s",
		    DI_PCA_INIT_FLAG_ALIASES_FILE_USER ?
		    DI_PCA_ALIASES_FILE_USER : DI_PCA_ALIASES_FILE);

	/*
	 * Commit changes to disk and trigger fmd(1M)/di_cro to synchronize
	 * with updated chassis_aliases mappings.
	 */
	if ((err = di_pca_sync(h, DI_PCA_SYNC_FLAG_COMMIT)) != 0)
		goto out;

	err = FMADM_EXIT_SUCCESS;

out:	di_pca_fini(h);
	return (err);
}

/*
 * argv[0] should be "add-alias"
 * argv[1] should be <product-id>.<chassis-di>
 * argv[2] should be <alias-id>
 * argv[3] optional comment
 *
 * argc should be at least 3:
 *	add-alias <product-id>.<chassis-id> <alias-id>	comment
 *	add-alias <product-id>.<chassis-id> <alias-id>
 *	add-alias comment
 */
/*ARGSUSED*/
int
cmd_alias_add(fmd_adm_t *adm, int argc, char *argv[])
{
	di_pca_hdl_t	h;
	char		*seperator;
	char		*chassis;
	int		err = FMADM_EXIT_ERROR;

	if (argc < 3 || argc > 4)
		return (FMADM_EXIT_USAGE);

	seperator = strstr(argv[1], DI_CRODC_PC_SEP);

	if (seperator == NULL) {
		warn("no '" DI_CRODC_PC_SEP "' separator in '%s'\n", argv[1]);
		return (FMADM_EXIT_USAGE);
	}

	/* Check to make sure chassis-id supplied */
	if (strlen(seperator) < 2) {
		warn("no <chassis-id> in '%s'\n", argv[1]);
		return (FMADM_EXIT_USAGE);
	}

	/* Check to make sure <product-id> was supplied */
	if ((strlen(argv[1]) - strlen(seperator)) == 0) {
		warn("no <product-id> in '%s'\n", argv[1]);
		return (FMADM_EXIT_USAGE);
	}

	chassis = seperator + 1;
	*seperator = '\0';

	/* Get handle to current records */
	if ((h = di_pca_init(DI_PCA_INIT_FLAG_PRINT)) == NULL)
		die("failed to initialize: %s", DI_PCA_ALIASES_FILE);

	/*
	 * Add this new record to the list in memory.
	 *
	 * NOTE: Since the di_pca_init used DI_PCA_INIT_PRINT, if there is a
	 * problem, the library code will print a message to stderr detailing
	 * the issue.
	 */
	if (di_pca_rec_add(h, argv[1], chassis, argv[2], argv[3]))
		goto out;

	/* Comment changes to disk */
	if (di_pca_sync(h, DI_PCA_SYNC_FLAG_COMMIT)) {
		goto out;
	}

	if (!di_pca_alias_id_used(h, argv[2]))
		warn("<alias-id> '%s' added for <product-id>.<chassis-id> "
		    "'%s.%s', but currently unused\n",
		    argv[2], argv[1], chassis);

	err = FMADM_EXIT_SUCCESS;

out:	di_pca_fini(h);
	return (err);
}

/*
 * argc should be 2:
 *	delete-alias <alias-id>
 * or
 *	delete-alias <product-id>.<chassis-id>
 */
/*ARGSUSED*/
int
cmd_alias_remove(fmd_adm_t *adm, int argc, char *argv[])
{
	di_pca_hdl_t	h;
	int		err = FMADM_EXIT_ERROR;

	if (argc != 2)
		return (FMADM_EXIT_USAGE);

	/* Check to make sure no other options passed in */
	if (getopt(argc, argv, "") != EOF)
		return (FMADM_EXIT_USAGE);

	if ((h = di_pca_init(DI_PCA_INIT_FLAG_PRINT)) == NULL)
		die("failed to initialize: %s", DI_PCA_ALIASES_FILE);

	if (di_pca_rec_remove(h, argv[1]))
		die("failed to remove '%s'", argv[1]);

	if (di_pca_sync(h, DI_PCA_SYNC_FLAG_COMMIT)) {
		goto out;
	}
	err = FMADM_EXIT_SUCCESS;

out:	di_pca_fini(h);
	return (err);
}

/*
 * argc should be 2:
 *	lookup-alias <alias-id>
 * or
 *	lookup-alias <product-id>.<chassis-id>
 */
/*ARGSUSED*/
int
cmd_alias_lookup(fmd_adm_t *adm, int argc, char *argv[])
{
	di_pca_hdl_t	h;
	di_pca_rec_t	r;
	int		err = FMADM_EXIT_ERROR;

	if (argc != 2)
		return (FMADM_EXIT_USAGE);

	/* Check to make sure no other options passed in */
	if (getopt(argc, argv, "") != EOF)
		return (FMADM_EXIT_USAGE);

	if ((h = di_pca_init(DI_PCA_INIT_FLAG_PRINT)) == NULL)
		die("failed to initialize: %s", DI_PCA_ALIASES_FILE);

	if ((r = di_pca_rec_lookup(h, argv[1])) == NULL) {
		warn("no records match '%s'\n", argv[1]);
		goto out;
	}

	di_pca_rec_print(stdout, r);
	err = FMADM_EXIT_SUCCESS;

out:	di_pca_fini(h);
	return (err);
}

/*
 * argc should be 1:
 *	list-alias
 */
/*ARGSUSED*/
int
cmd_alias_list(fmd_adm_t *adm, int argc, char *argv[])
{
	di_pca_hdl_t	h;
	di_pca_rec_t	r;
	int		err = FMADM_EXIT_ERROR;

	if (argc != 1)
		return (FMADM_EXIT_USAGE);

	/* Check to make sure no other options passed in */
	if (getopt(argc, argv, "") != EOF)
		return (FMADM_EXIT_USAGE);

	if ((h = di_pca_init(DI_PCA_INIT_FLAG_PRINT)) == NULL)
		die("failed to initialize: %s", DI_PCA_ALIASES_FILE);

	for (r = di_pca_rec_next(h, NULL); r; r = di_pca_rec_next(h, r)) {
		/* Skip records that have no <alias-id> */
		if (di_pca_rec_get_alias_id(r))
			di_pca_rec_print(stdout, r);
	}

	err = FMADM_EXIT_SUCCESS;

out:	di_pca_fini(h);
	return (err);
}
