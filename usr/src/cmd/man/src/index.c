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
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "index.h"
#include "indexParser.h"
#include "indexUtil.h"
#include "indexWriter.h"
#include "indexReader.h"

#define	eq(a, b)	(strcmp(a, b) == 0)

static int cmp(const void *arg1, const void *arg2) {
	char **p1 = (char **)arg1;
	char **p2 = (char **)arg2;

	return (strcmp(*p1, *p2));
}

/*
 * check if it's a standard man path.
 * i.e., /usr/share/man/man1
 */
static int is_man_dir(const char *fname) {
	if (strlen(fname) > 3 &&
	(strncmp(fname, "man", 3) == 0) &&
	isdigit(fname[3])) {
		return (1);
	}
	return (0);
}

/*
 * get sorted directory list in given path.
 * dirv needs to be freed manually
 */
static int get_dirs(const char *path, char ***dirv) {
	DIR *dp;
	struct dirent *d;
	IN_int dir_count;
	char **dv;
	int entries;

	entries = MAXDIR;

	if ((dp = opendir(path)) == 0) {
		perror(path);
		return (0);
	}

	dir_count = 0;
	*dirv = (char **) malloc(sizeof (char *) * entries);
	if (*dirv == NULL) {
		malloc_error();
	}

	dv = *dirv;
	while (d = readdir(dp)) {
		if (eq(".", d->d_name) || eq("..", d->d_name)) {
			continue;
		}
		/* check if it's man dir */
		if (is_man_dir(d->d_name)) {
			dv[dir_count] = (char *)malloc(strlen(d->d_name) + 1);
			if (dv[dir_count] == NULL) {
				malloc_error();
			}
			(void) strcpy(dv[dir_count], d->d_name);
			dir_count++;
			if (dir_count == entries) {
				entries *= 2;
				*dirv = (char **)realloc(*dirv,
				sizeof (char *) * entries);
				if (dirv == NULL) {
					malloc_error();
				}
				dv = *dirv;
			}

		}
	}
	qsort((void *)dv, dir_count, sizeof (char *), cmp);

	(void) closedir(dp);
	return (dir_count);
}

/*
 * get sorted file ilist in given path.
 * filev needs to be freed manually
 */
static int get_files(const char *path, char ***filev) {
	DIR *dp;
	struct dirent *d;
	unsigned int file_count;
	char **fv;
	int entries;
	char tmp_file[MAXFILEPATH];

	entries = MAXFILE;
	if ((dp = opendir(path)) == 0) {
		perror(path);
		return (0);
	}

	file_count = 0;
	*filev = (char **) malloc(sizeof (char *) * entries);
	if (*filev == NULL) {
		malloc_error();
	}

	fv = *filev;
	while (d = readdir(dp)) {
		if (eq(".", d->d_name) || eq("..", d->d_name)) {
			continue;
	}
		(void) snprintf(tmp_file, MAXFILEPATH,
		    "%s/%s", path, d->d_name);
		/* check if the file exists and readable */
		if (access(tmp_file, R_OK) == 0) {
			fv[file_count] = (char *)malloc(strlen(d->d_name) + 1);
			if (fv[file_count] == NULL) {
				malloc_error();
			}

			(void) strlcpy(fv[file_count], d->d_name, MAXFILEPATH);
			file_count++;
			if (file_count == entries) {
				entries *= 2;
				*filev = (char **)realloc(*filev,
					sizeof (char *) * entries);
				if (filev == NULL) {
					malloc_error();
				}
				fv = *filev;
			}
		}
	}
	qsort((void *)fv, file_count, sizeof (char *), cmp);

	(void) closedir(dp);
	return (file_count);
}

static void free_v(char **v, int size) {
	int i;

	for (i = 0; i < size; i++) {
		free(v[i]);
	}
	free(v);
}

/*
 * generate a linked list to store doc infos.
 * rememebr to free doc_link after using.
 */
static int append_doc(char *file, unsigned int id, DocInfo **doc_link) {
	DocInfo *new_doc;
	FILE *fp;
	unsigned int file_size;
	static DocInfo *priv_doc = NULL;

	file_size = 0;

	if ((fp = fopen(file, "r")) == NULL) {
		perror(file);
		return (1);
	}

	if (fseek(fp, 0L, SEEK_END) != 0) {
		(void) fclose(fp);
		return (1);
	}
	file_size = ftell(fp);
	(void) fclose(fp);

	new_doc = (DocInfo *) malloc(sizeof (DocInfo));
	if (new_doc == NULL) {
		malloc_error();
	}

	(void) strlcpy(new_doc->value, file, MAXFILEPATH);

	new_doc->id = id;
	new_doc->size = file_size;
	new_doc->next = NULL;

	if (*doc_link == NULL) {
		*doc_link = new_doc;
		priv_doc = new_doc;
	} else {
		priv_doc->next = new_doc;
		priv_doc = priv_doc->next;
	}
	return (0);
}

static void free_linked_list(TermNode *link) {
	DocFreq *freq;
	DocFreq *pv_freq;
	DocPosition *pos;
	DocPosition *pv_pos;

	if (link == NULL) {
		return;
	}

	freq = link->term.link_freq;
	pos = link->term.link_position;

	while (freq != NULL) {
		pv_freq = freq->next;
		free(freq);
		freq = pv_freq;
	}
	while (pos != NULL) {
		pv_pos = pos->next;
		free(pos);
		pos = pv_pos;
	}
}

static void free_term_link(TermNode *link) {
	if (link == NULL) {
		return;
	}

	free_term_link(link->left);
	free_term_link(link->right);

	free_linked_list(link);
	free(link);
}

static void free_doc_link(DocInfo *link) {
	DocInfo *priv;
	DocInfo *tmp;

	priv = link;
	while (priv != NULL) {
		tmp = priv->next;
		free(priv);
		priv = tmp;
	}
}

/*
 * check if directory is writeable
 * return 1 if writable
 */
static int check_dir_writeable(const char *path) {
	return (access(path, W_OK) ? 0 : 1);
}

/*
 * A symbolic link to third part man pages can be
 * put under /usr/share/man/index.d directory.
 * makesymbindex() will generate index file for all
 * symbol links there.
 */
int makesymbindex() {
	int dirs;
	char **dv;
	char f[MAXFILEPATH];
	char d_path[MAXFILEPATH];
	ssize_t len;
	int i;

	if (!check_dir_writeable(THIRDPARTMAN)) {
		return (1);
	}

	dirs = get_files(THIRDPARTMAN, &dv);
	for (i = 0; i < dirs; i++) {
		(void) snprintf(f, MAXFILEPATH, "%s/%s", THIRDPARTMAN, dv[i]);
		if ((len = readlink(f, d_path, sizeof (d_path))) == -1) {
			continue;
		}
		/* readlink() doesn't put null */
		d_path[len] = '\0';
		(void) makeindex(d_path);
	}
	free_v(dv, dirs);

	return (0);
}

/*
 * Create path/man-index directory.
 * return 1 if there is no error,
 * ortherwise return 0;
 */
static int create_index_dir(const char *path) {
	char file_path[MAXFILEPATH];

	(void) snprintf(file_path, MAXFILEPATH, "%s/%s", path, INDEXDIR);
	if (access(file_path, W_OK) == 0) {
		return (1);
	}
	if (mkdir(file_path, 00755) == -1) {
		(void) printf(gettext("Can not create index dir:%s\n"),
		    file_path);
		return (0);
	}
	return (1);
}

/*
 * Generate index file for speciafied path
 */
int makeindex(const char *path) {
	int dirs;
	char **dv;
	int files;
	char **fv;
	char d_path[MAXFILEPATH];
	char f_path[MAXFILEPATH];
	unsigned int doc_count;
	int i, j;
	DocInfo *doc_link;

	TermNode *term_link = NULL;
	TermNode *tmp_link = NULL;

	doc_link = NULL;

	if (!check_dir_writeable(path)) {
		(void) fprintf(stderr,
			gettext("Cann't write index file to %s\n"), path);
		return (1);
	}

	if (create_index_dir(path) == 0) {
		return (1);
	}

	dirs = get_dirs(path, &dv);
	doc_count = 0;

	for (i = 0; i < dirs; i++) {
		(void) snprintf(d_path, MAXFILEPATH, "%s/%s\0", path, dv[i]);
		files = get_files(d_path, &fv);
		for (j = 0; j < files; j++) {
			(void) snprintf(f_path, MAXFILEPATH,
			    "%s/%s\0", d_path, fv[j]);
			tmp_link = parse_roff(f_path, doc_count, tmp_link);
			if (append_doc(f_path, doc_count, &doc_link) == 1) {
				free_v(fv, files);
				free_v(dv, dirs);
				free_term_link(term_link);
				free_doc_link(doc_link);
				return (1);
			}
			doc_count++;
			if (doc_count % MERGEFACTOR == 0) {
				term_link = merge_link(term_link, tmp_link);
				tmp_link = NULL;
			}
		}
		free_v(fv, files);
	}

	if (tmp_link) {
		term_link = merge_link(term_link, tmp_link);
	}
	free_v(dv, dirs);

	(void) write_term(TERMFILE, TERMFREQ, TERMPOSITION, term_link, path);
	(void) write_doc(TERMDOCFILE, doc_link, path);

	free_term_link(term_link);
	free_doc_link(doc_link);

	return (0);
}

/*
 * Return 1 if path is readable,
 * ortherwise return 0;
 */
static int check_dir_readable(const char *path) {
	char file_path[MAXFILEPATH];

	(void) snprintf(file_path, MAXFILEPATH, "%s/%s/%s",
	    path, INDEXDIR, TERMDOCFILE);
	if (access(file_path, R_OK) == 0) {
		return (1);
	}
	return (0);
}

/*
 * Read all symbolic links under /usr/share/man/index.d
 * direcory, and query the key words.
 */
int querysymbindex(char *words, ScoreList **score, const char *msc) {
	int dirs;
	char **dv;
	char f[MAXFILEPATH];
	char d_path[MAXFILEPATH];
	int len;
	int i;

	if (access(THIRDPARTMAN, R_OK) != 0) {
		return (1);
	}

	dirs = get_files(THIRDPARTMAN, &dv);
	for (i = 0; i < dirs; i++) {
		(void) snprintf(f, MAXFILEPATH, "%s/%s", THIRDPARTMAN, dv[i]);
		if ((len = readlink(f, d_path, sizeof (d_path))) == -1) {
			continue;
		}
		/* readlink() doesn't put null */
		d_path[len] = '\0';
		(void) queryindex(words, d_path, score, msc);
	}
	free_v(dv, dirs);

	return (0);
}

/*
 * Read index file under specified path and query the key words.
 */
int queryindex(char *words, char *path, ScoreList **score, const char *msc) {
	Doc *docs;
	Term *terms;
	Keyword *k;

	docs = NULL;
	terms = NULL;
	k = NULL;

	if (check_dir_readable(path) == 0) {
		return (0);
	}
	if (get_words(words, &k, msc, 0) == NOTINSECTION) {
		return (1);
	}

	docs = read_doc(path);
	terms = read_term_index(path);
	(void) cal_score(k, path, score);

	(void) free_keyword(k);

	if (docs)
		free(docs);
	if (terms)
		free(terms);
	return (0);
}
