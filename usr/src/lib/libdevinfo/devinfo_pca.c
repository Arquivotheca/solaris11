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

#include <stdio.h>
#include <string.h>
#include "libdevinfo.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <libsysevent.h>
#include <sys/sysevent/eventdefs.h>
#include <unistd.h>
#include <sys/varargs.h>

/* pca: <product-id> <chassis-id> -> <alias-id> mapping */

struct di_pca_hdl {
	di_pca_rec_t	h_rec_ptr;	/* head record linked list of records */
	di_pca_rec_t	h_l_rec_ptr;	/* last linked list of records */
	int		h_init_flag;
	int		h_nrec;
};

/* implementation of a <product-id>.<chassis-id> to <alias-id> map */
struct di_pca_rec {
	di_pca_rec_t	r_next;
	di_pca_rec_t	r_prev;
	char		*r_product_id;
	char		*r_chassis_id;
	char		*r_alias_id;
	char		*r_comment;
	int		r_lnum;
};

static mutex_t di_pca_event_mutex = DEFAULTMUTEX;
static cond_t di_pca_event_completed_cv = DEFAULTCV;
static int di_pca_wait_sec = 2 * 60;

/*
 * Print out messages if flag is set to print
 */
#define	ERROR_BUFLEN	1024
static void
_pca_error_print(di_pca_hdl_t h, int lnum, const char *fmt, ...)
{
	va_list	ap;
	int	len;
	char	*buf0, *buf;

	if ((h == NULL) || !(h->h_init_flag & DI_PCA_INIT_FLAG_PRINT))
		return;

	len = ERROR_BUFLEN;
	if ((buf0 = buf = malloc(len)) == NULL)
		return;

	(void) snprintf(buf, len, "%s: ", DI_PCA_ALIASES);
	len -= strlen(buf);
	buf += strlen(buf);
	if (lnum > 0) {
		(void) snprintf(buf, len, "line %d: ", lnum);
		len -= strlen(buf);
		buf += strlen(buf);
	}

	va_start(ap, fmt);
	(void) vsnprintf(buf, len, fmt, ap);
	va_end(ap);
	len -= strlen(buf);
	buf += strlen(buf);
	if (len > 2) {
		*buf++ = '\n';
		*buf++ = 0;
	}
	/*
	 * NOTE: to improve diagnosis, we could use fmadm
	 * strerror trick based on newline in fmt string...
	 */

	(void) fprintf(stderr, "%s", buf0);
	free(buf0);
}

/*
 * Add a new record to the link list.
 * This will NOT check to see if this is a valid/duplicate record.
 */
/* ARGSUSED */
static void
_pca_rec_add(di_pca_hdl_t h, int lnum,
    char *product_id, char *chassis_id, char *alias_id, char *comment)
{
	di_pca_rec_t	r;
	di_pca_rec_t	lr;

	if ((r = calloc(1, sizeof (struct di_pca_rec))) == NULL)
		return;
	r->r_lnum = lnum;
	r->r_next = NULL;

	if (product_id)
		r->r_product_id = strdup(product_id);
	if (chassis_id)
		r->r_chassis_id = strdup(chassis_id);
	if (alias_id)
		r->r_alias_id = strdup(alias_id);
	if (comment)
		r->r_comment = strdup(comment);

	if (h->h_rec_ptr == NULL) {
		h->h_rec_ptr = r;
	} else {
		lr = h->h_l_rec_ptr;
		lr->r_next = r;
		r->r_prev = lr;
	}

	h->h_l_rec_ptr = r;
	h->h_nrec++;
}

static void
_pca_rec_free(di_pca_rec_t r)
{
	if (r == NULL)
		return;
	if (r->r_product_id)
		free(r->r_product_id);
	if (r->r_chassis_id)
		free(r->r_chassis_id);
	if (r->r_alias_id)
		free(r->r_alias_id);
	if (r->r_comment)
		free(r->r_comment);
	free(r);
}

/*
 * Check for a duplicate record entry
 */
static int
_pca_check_for_dups(di_pca_hdl_t h,
    char *product_id, char *chassis_id, char *alias_id)
{
	di_pca_rec_t	r;

	for (r = di_pca_rec_next(h, NULL); r; r = di_pca_rec_next(h, r)) {
		if (r->r_alias_id) {
			if (strcmp(r->r_alias_id, alias_id) == 0) {
				_pca_error_print(h, r->r_lnum,
				    "error: duplicate <alias-id>: %s",
				    r->r_alias_id);
				return (DI_PCA_FAILURE);
			}
		}

		if (r->r_product_id && r->r_chassis_id) {
			if ((strcmp(r->r_product_id, product_id) == 0) &&
			    strcmp(r->r_chassis_id, chassis_id) == 0) {
				_pca_error_print(h, r->r_lnum,
				    "error: duplicate " DI_CRODC_PC_FMT ": "
				    DI_CRODC_PC_FMT,
				    "<product-id>", "<chassis-id>",
				    product_id, chassis_id);
				return (DI_PCA_FAILURE);
			}
		}
	}
	return (DI_PCA_SUCCESS);
}

/* ARGSUSED */
static int
_pca_verify_valid_alias(di_pca_hdl_t h, char *alias_id)
{
	char *dup_str;

	if (strcmp(DI_CRODC_SYSALIAS, alias_id) == 0) {
		_pca_error_print(h, 0,
		    "error: reserved <alias-id> name: %s", DI_CRODC_SYSALIAS);
		return (DI_PCA_FAILURE);
	}
	dup_str = strdup(alias_id);
	(void) di_cro_strclean(dup_str, 1, 0);
	if (strcmp(dup_str, alias_id) != 0) {
		_pca_error_print(h, 0,
		    "error: reserved character being used in <alias-id>: '%s'",
		    DI_CRODC_RESERVED_CHARS);
		free(dup_str);
		return (DI_PCA_FAILURE);
	}

	free(dup_str);
	return (DI_PCA_SUCCESS);
}

/* ARGSUSED */
static int
_pca_verify_valid_pc(di_pca_hdl_t h, char *entry)
{
	char *dup_str;

	dup_str = strdup(entry);
	(void) di_cro_strclean(dup_str, 1, 1);
	if (strcmp(dup_str, entry) != 0) {
		_pca_error_print(h, 0,
		    "error: invalid character being used in name: '%s'",
		    entry);
		_pca_error_print(h, 0,
		    "invalid characters shown with _: '%s'",
		    dup_str);

		free(dup_str);
		return (DI_PCA_FAILURE);
	}

	free(dup_str);
	return (DI_PCA_SUCCESS);
}
/*
 * Add a record but verify that this is valid record(not a comment)
 * to add and that it doesn't already exist
 */
/* ARGSUSED */
int
di_pca_rec_add(di_pca_hdl_t h,
    char *product_id, char *chassis_id, char *alias_id, char *comment)
{
	char	comm[1025];
	int	err;

	/* If here, everything looks good, add this record */
	if (h == NULL || product_id == NULL || chassis_id == NULL)
		return (DI_PCA_FAILURE);

	err = _pca_verify_valid_pc(h, product_id);
	if (err)
		return (err);

	err = _pca_verify_valid_pc(h, chassis_id);
	if (err)
		return (err);

	err = _pca_verify_valid_alias(h, alias_id);
	if (err)
		return (err);

	err = _pca_check_for_dups(h, product_id, chassis_id, alias_id);
	if (err)
		return (err);

	if (!comment) {
		_pca_rec_add(h, -1, product_id, chassis_id, alias_id, comment);
	} else {
		if (*comment != '#')
			(void) snprintf(comm, sizeof (comm), "#%s", comment);
		else
			(void) snprintf(comm, sizeof (comm), "%s", comment);
		_pca_rec_add(h, -1, product_id, chassis_id, alias_id, comm);
	}

	return (DI_PCA_SUCCESS);
}

void
_pca_rec_remove(di_pca_hdl_t h, di_pca_rec_t r_del)
{
	di_pca_rec_t r_next;
	di_pca_rec_t r_prev;

	if (!h || !r_del)
		return;
	if (r_del->r_prev == NULL) {
		/* First record in list */

		if (r_del->r_next) {
			/* More records */
			r_next = r_del->r_next;
			h->h_rec_ptr = r_next;
			r_next->r_prev = NULL;
		} else {
			/* Only record */
			h->h_rec_ptr = NULL;
			h->h_l_rec_ptr = NULL;
		}
	} else if (r_del->r_next == NULL) {
		/*
		 * Last record in list when more than 1 record
		 * first if statement above took care of only 1 record
		 */
		r_prev = r_del->r_prev;
		h->h_l_rec_ptr = r_prev;
		r_prev->r_next = NULL;
	} else {
		/* In the middle of the list somewhere */
		r_prev = r_del->r_prev;
		r_next = r_del->r_next;
		r_prev->r_next = r_next;
		r_next->r_prev = r_prev;
	}

	_pca_rec_free(r_del);
}

/*
 * Remove a non comment record. The match_str can be
 *	<product-id>.<chassis-id> || <alias-id>
 * so when looking for matches, must check these fields.
 */
/* ARGSUSED */
int
di_pca_rec_remove(di_pca_hdl_t h, char *match_str)
{
	di_pca_rec_t	r;
	char		pc[1024];

	if (h == NULL)
		return (DI_PCA_FAILURE);

	for (r = di_pca_rec_next(h, NULL); r; r = di_pca_rec_next(h, r)) {
		if (r->r_product_id && r->r_chassis_id && r->r_alias_id) {
			(void) snprintf(pc, sizeof (pc), DI_CRODC_PC_FMT,
			    r->r_product_id, r->r_chassis_id);
			if (strcmp(match_str, r->r_alias_id) == 0)
				_pca_rec_remove(h, r);
			else if (strcmp(match_str, pc) == 0)
				_pca_rec_remove(h, r);
		}
	}
	return (DI_PCA_SUCCESS);
}

di_pca_rec_t
di_pca_rec_next(di_pca_hdl_t h, di_pca_rec_t r)
{
	if (h == NULL)
		return (NULL);
	if (r == NULL)
		return (h->h_rec_ptr);
	return (r->r_next);
}

char *
di_pca_rec_get_product_id(di_pca_rec_t r)
{
	if (r)
		return (r->r_product_id);
	return (NULL);
}

char *
di_pca_rec_get_chassis_id(di_pca_rec_t r)
{
	if (r)
		return (r->r_chassis_id);
	return (NULL);

}

char *
di_pca_rec_get_alias_id(di_pca_rec_t r)
{
	if (r)
		return (r->r_alias_id);
	return (NULL);
}

char *
di_pca_rec_get_comment(di_pca_rec_t r)
{
	if (r)
		return (r->r_comment);
	return (NULL);
}

/*
 * Create the list of records from DI_PCA_ALIASES_FILE file. Comments in
 * file will generate a record as well (to preserve line number and contents
 * on update). If there is an error in file, return a NULL handle.
 * If DI_PCA_INIT_FLAG_PRINT is set, we will print out messages describing
 * the problem.
 */
/* ARGSUSED */
di_pca_hdl_t
di_pca_init(int init_flag)
{
	di_pca_hdl_t	h = NULL;
	char		*aliases_file;
	FILE		*fp = NULL;
	int		lnum = 0;
	char		line[1024];
	char		lineori[1024];
	char		*prod, *chassis, *alias, *comment;
	char		*seperator;

	if ((h = calloc(1, sizeof (struct di_pca_hdl))) == NULL)
		return (NULL);

	h->h_init_flag = init_flag;

	/*
	 * Read in the /DI_PCA_ALIASES_FILE file.  A valid line contains
	 * "<product-id>.<chassis-id> <white-space> <alias-id>  [ # comment]"
	 */
	aliases_file = (init_flag & DI_PCA_INIT_FLAG_ALIASES_FILE_USER) ?
	    DI_PCA_ALIASES_FILE_USER : DI_PCA_ALIASES_FILE;
	if ((fp = fopen(aliases_file, "r")) == NULL)
		return (h);

	while (fgets(line, sizeof (line), fp) != NULL) {
		(void) strncpy(lineori, line, sizeof (lineori));
		lnum++;
		prod = NULL;
		chassis = NULL;
		comment = NULL;

		prod = strtok(line, " \t\n");
		if ((prod == NULL) || (*prod == '#')) {
			/* blank or comment line */
			_pca_rec_add(h, lnum, NULL, NULL, NULL, lineori);
			continue;
		}

		/* Check to make sure there is <product-id>.<chassis-id> */
		seperator = strstr(prod, DI_CRODC_PC_SEP);
		if (seperator == NULL) {
			_pca_error_print(h, lnum,
			    "error: no separator on line");
			goto err;
		}

		/* Check to make sure <chassis-id> supplied */
		if (strlen(seperator) < 2) {
			_pca_error_print(h, lnum,
			    "error: no <chassis-id> on line");
			goto err;
		}

		/* Check to make sure <product-id> was supplied */
		if ((strlen(prod) - strlen(seperator)) == 0) {
			_pca_error_print(h, lnum,
			    "error: no <product-id> on line");
			goto err;
		}

		/* If here, valid <product-id>.<chassis-id>. */
		chassis = seperator + 1;
		*seperator = '\0';

		if (((alias = strtok(NULL, " \t\n")) == NULL) ||
		    (*alias == '#')) {
			_pca_error_print(h, lnum,
			    "error: no <alias-id> on line");
			goto err;
		}

		comment = strtok(NULL, "\n");
		if (comment && (*comment != '#')) {
			_pca_error_print(h, lnum,
			    "error: too many fields on line");
			goto err;
		}
		if (_pca_check_for_dups(h, prod, chassis, alias) != 0) {
			_pca_error_print(h, lnum,
			    "error: duplicate information");
			goto err;
		}

		_pca_rec_add(h, lnum, prod, chassis, alias, comment);
	}
	(void) fclose(fp);
	return (h);

err:	if (fp)
		(void) fclose(fp);
	di_pca_fini(h);
	return (NULL);
}

void
di_pca_fini(di_pca_hdl_t h)
{
	di_pca_rec_t	r, rn;

	if (h == NULL)
		return;

	for (r = h->h_rec_ptr; r; r = rn) {
		rn = r->r_next;
		(void) _pca_rec_free(r);
	}
	free(h);
}

void
di_pca_rec_print(FILE *fp, di_pca_rec_t r)
{
	char		*pi;
	char		*ci;
	char		*nn;
	char		*cmt;

	if ((r == NULL) || (fp == NULL))
		return;

	pi = di_pca_rec_get_product_id(r);
	ci = di_pca_rec_get_chassis_id(r);
	nn = di_pca_rec_get_alias_id(r);
	cmt = di_pca_rec_get_comment(r);

	if (pi && cmt)
		(void) fprintf(fp, DI_CRODC_PC_FMT "\t%s\t%s\n",
		    pi, ci, nn, cmt);
	else if (pi)
		(void) fprintf(fp, DI_CRODC_PC_FMT "\t%s\n", pi, ci, nn);
	else if (cmt)
		(void) fprintf(fp, "%s", cmt);
	else
		(void) fprintf(fp, "\n");
}

static int
_pca_write_file(di_pca_hdl_t h, char *file_out)
{
	char		unique_name[256];
	int		fd;
	FILE		*tfp;
	di_pca_rec_t	r;
	struct stat	stat_buf;
	int		sok = 0;
	int		errno_s;

	(void) strcpy(unique_name, DI_PCA_ALIASES_FILE_TMP);
	fd = mkstemp(unique_name);
	if (fd == -1) {
		_pca_error_print(h, 0,
		    "error: failed to create temporary file");
		return (DI_PCA_FAILURE);
	}
	(void) close(fd);

	tfp = fopen(unique_name, "w+");
	if (tfp == NULL) {
		(void) unlink(unique_name);
		return (DI_PCA_FAILURE);
	}
	for (r = di_pca_rec_next(h, NULL); r; r = di_pca_rec_next(h, r))
		di_pca_rec_print(tfp, r);
	(void) fclose(tfp);

	/* Get current permissions on file */
	if (stat(file_out, &stat_buf) >= 0)
		sok = 1;
	if (rename(unique_name, file_out)) {
		errno_s = errno;
		(void) unlink(unique_name);
		errno = errno_s;
		return (DI_PCA_FAILURE);
	}

	if (sok)
		(void) chmod(file_out, stat_buf.st_mode);

	return (DI_PCA_SUCCESS);
}

int
_pca_write_files(di_pca_hdl_t h)
{
	int		err;

	err = _pca_write_file(h, DI_PCA_ALIASES_FILE);
	if (err == DI_PCA_SUCCESS)
		err = _pca_write_file(h, DI_PCA_ALIASES_FILE_USER);

	return (err);
}

/*ARGSUSED*/
static void
_pca_dbupdate_finish_handler(sysevent_t *evp)
{
	(void) mutex_lock(&di_pca_event_mutex);
	(void) cond_broadcast(&di_pca_event_completed_cv);
	(void) mutex_unlock(&di_pca_event_mutex);
}

int
di_pca_sync(di_pca_hdl_t h, int flag)
{
	sysevent_id_t	eid;
	const char		*esc[1];
	sysevent_handle_t	*shp = NULL;
	timestruc_t to;
	int err;

	if (h == NULL)
		return (DI_PCA_FAILURE);

	if ((flag & DI_PCA_SYNC_FLAG_COMMIT) &&
	    ((err = _pca_write_files(h)) != DI_PCA_SUCCESS))
		return (err);

	/*  Setup receiver for topo refresh finish */
	shp = sysevent_bind_handle(_pca_dbupdate_finish_handler);
	if (shp == NULL) {
		_pca_error_print(h, 0,
		    "error: failed to bind to sysevent handler");
		return (DI_PCA_FAILURE);
	}
	esc[0] = ESC_CRO_DBUPDATE_FINISH;
	if (sysevent_subscribe_event(shp, EC_CRO, esc, 1)) {
		_pca_error_print(h, 0,
		    "error: failed to subscribe to CRO events");
		return (DI_PCA_FAILURE);
	}

	/* send event to notify users */
	(void) mutex_lock(&di_pca_event_mutex);
	(void) sysevent_post_event(EC_CRO, ESC_CRO_TOPOREFRESH,
	    SUNW_VENDOR, "fmd", NULL, &eid);

	/* Wait for event completion confirmation from devchassisd */
	to.tv_sec = di_pca_wait_sec;
	to.tv_nsec = 0;
	err = cond_reltimedwait(&di_pca_event_completed_cv,
	    &di_pca_event_mutex, &to);
	(void) mutex_unlock(&di_pca_event_mutex);
	if (err == ETIME) {
		_pca_error_print(h, 0, "WARNING: /dev/chassis namespace "
		    "update still in progress...");
		return (DI_PCA_FAILURE);
	}
	return (DI_PCA_SUCCESS);
}

di_pca_rec_t
di_pca_rec_lookup(di_pca_hdl_t h, char *match_str)
{
	di_pca_rec_t r;
	char pc[1024];

	for (r = di_pca_rec_next(h, NULL); r; r = di_pca_rec_next(h, r)) {
		if (r->r_product_id && r->r_chassis_id && r->r_alias_id) {
			(void) snprintf(pc, sizeof (pc), DI_CRODC_PC_FMT,
			    r->r_product_id, r->r_chassis_id);
			if (strcmp(match_str, r->r_alias_id) == 0) {
				return (r);
			} else if (strcmp(match_str, pc) == 0) {
				return (r);
			}
		}

	}
	return (NULL);
}

/*ARGSUSED*/
int
di_pca_alias_id_used(di_pca_hdl_t h, char *alias_id)
{
	di_cro_hdl_t	cro;
	static const char	*query_fmt = DI_CRO_Q_ALIAS_ID	DI_CRO_QREFMT;
	int		query_len;
	char		*query;
	di_cro_reca_t	ra;
	di_cro_rec_t	r = NULL;

	if ((alias_id == NULL) || (cro = di_cro_init(NULL, 0)) == NULL)
		return (0);

	/* see if there is a cro record associated with the alias */
	query_len = strlen(query_fmt) + strlen(alias_id);
	if ((query = malloc(query_len)) == NULL)
		goto out;
	(void) snprintf(query, query_len, query_fmt, alias_id);
	ra = di_cro_reca_create_query(cro, 0, query);
	r = di_cro_reca_next(ra, NULL);
	di_cro_reca_destroy(ra);
	free(query);
out:	di_cro_fini(cro);
	return (r ? 1 : 0);
}
