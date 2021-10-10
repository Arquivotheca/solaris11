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
#include <fcntl.h>
#include <unistd.h>

#include "indexWriter.h"
#include "indexUtil.h"

static unsigned int term_count = 0;
static unsigned int f_offset = 0;
static unsigned int p_offset = 0;

static int  read_write_unlock(int fd) {
	struct flock fl;

	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	return (fcntl(fd, F_SETLK, &fl));
}

static int  read_write_lock(int fd) {
	struct flock fl;

	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	return (fcntl(fd, F_SETLK, &fl));
}

static unsigned int write_term_freq(FILE *fp, TermNode *p) {
	DocFreq *freq;
	unsigned int count = 0;

	freq = p->term.link_freq;
	while (freq != NULL) {
		freq = freq->next;
		count++;
	}

	(void) fwrite(&count, sizeof (unsigned int), 1, fp);

	freq = p->term.link_freq;
	while (freq != NULL) {
		(void) fwrite(&freq->doc_id, sizeof (unsigned int), 1, fp);
		(void) fwrite(&freq->freq, sizeof (unsigned int), 1, fp);
		freq = freq->next;
	}

	return (count);
}

static unsigned int write_term_pos(FILE *fp, TermNode *p) {
	DocPosition *pos;
	unsigned int count = 0;

	pos = p->term.link_position;
	while (pos != NULL) {
		pos = pos->next;
		count++;
	}

	(void) fwrite(&count, sizeof (unsigned int), 1, fp);

	pos = p->term.link_position;
	while (pos != NULL) {
		(void) fwrite(&pos->doc_id, sizeof (unsigned int), 1, fp);
		(void) fwrite(&pos->row, sizeof (unsigned short int), 1, fp);
		(void) fwrite(&pos->col, sizeof (unsigned short int), 1, fp);
		(void) fwrite(&pos->section, sizeof (short int), 1, fp);
		pos = pos->next;
	}

	return (count);
}

static int write_term_dic(FILE *fp,
			char *s,
			unsigned int freq_offset,
			unsigned int pos_offset) {
	unsigned char term_size = 0;

	term_size = strlen(s);

	(void) fwrite(&term_size, sizeof (char), 1, fp);
	(void) fwrite(s, 1, term_size, fp);
	(void) fwrite(&freq_offset, sizeof (unsigned int), 1, fp);
	(void) fwrite(&pos_offset, sizeof (unsigned int), 1, fp);

	return (0);
}

static int write_file(FILE *fp_term, FILE *fp_freq, FILE *fp_pos, TermNode *p) {
	if (p == NULL)
		return (1);

	(void) write_file(fp_term, fp_freq, fp_pos, p->left);

	(void) write_term_dic(fp_term, p->term.value, f_offset, p_offset);

	f_offset = write_term_freq(fp_freq, p);

	p_offset = write_term_pos(fp_pos, p);

	(void) write_file(fp_term, fp_freq, fp_pos, p->right);

	term_count++;

	return (0);

}

int write_term(const char *term_file, const char *freq_file,
		const char *pos_file, TermNode *p, const char *path) {
	FILE *fp_term;
	FILE *fp_freq;
	FILE *fp_pos;

	char file_path[MAXFILEPATH];

	int f_term;
	int f_freq;
	int f_pos;

	(void) snprintf(file_path, MAXFILEPATH, "%s/%s/%s",
	    path, INDEXDIR, term_file);
	f_term = open(file_path, O_RDWR);
	(void) read_write_lock(f_term);
	if ((fp_term = fopen(file_path, "wb")) == NULL) {
		perror(term_file);
		return (0);
	}
	(void) snprintf(file_path, MAXFILEPATH, "%s/%s/%s",
	    path, INDEXDIR, freq_file);
	f_freq = open(file_path, O_RDWR);
	(void) read_write_lock(f_freq);
	if ((fp_freq = fopen(file_path, "wb")) == NULL) {
		perror(freq_file);
		return (0);
	}
	(void) snprintf(file_path, MAXFILEPATH, "%s/%s/%s",
	    path, INDEXDIR, pos_file);
	f_pos = open(file_path, O_RDWR);
	(void) read_write_lock(f_pos);
	if ((fp_pos = fopen(file_path, "wb")) == NULL) {
		perror(pos_file);
		return (0);
	}

	f_offset = 0;
	p_offset = 0;
	term_count = 0;

	(void) fwrite(VERSIONSTR, sizeof (VERSIONSTR), 1, fp_term);
	(void) fwrite(VERSIONSTR, sizeof (VERSIONSTR), 1, fp_freq);
	(void) fwrite(VERSIONSTR, sizeof (VERSIONSTR), 1, fp_pos);

	(void) fwrite(&term_count, sizeof (int), 1, fp_term);
	(void) write_file(fp_term, fp_freq, fp_pos, p);

	if (fseek(fp_term, sizeof (VERSIONSTR), SEEK_SET) != 0) {
		return (1);
	}
	(void) fwrite(&term_count, sizeof (int), 1, fp_term);

	(void) fclose(fp_term);
	(void) fclose(fp_freq);
	(void) fclose(fp_pos);

	(void) read_write_unlock(f_term);
	(void) read_write_unlock(f_freq);
	(void) read_write_unlock(f_pos);

	(void) close(f_term);
	(void) close(f_freq);
	(void) close(f_pos);

	return (0);
}

int write_doc(const char *doc_file, DocInfo *p, const char *path) {
	FILE *fp;
	DocInfo *q = p;
	char file_path[MAXFILEPATH];
	int i = 0;

	(void) snprintf(file_path, MAXFILEPATH, "%s/%s/%s",
	    path, INDEXDIR, doc_file);
	if ((fp = fopen(file_path, "wb")) == NULL) {
		perror(doc_file);
		return (0);
	}

	while (q) {
		q = q->next;
		i++;
	}

	(void) fprintf(fp, "%s\n", VERSIONSTR);
	(void) fprintf(fp, "%d\n", i);
	q = p;
	while (q) {
		(void) fprintf(fp, "%s##%d\n", q->value, q->size);
		q = q->next;
	}

	(void) fclose(fp);

	return (0);
}
