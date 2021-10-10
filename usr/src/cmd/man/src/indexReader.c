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
#include <math.h>
#include <locale.h>
#include <unistd.h>

#include "indexReader.h"
#include "indexUtil.h"
#include "indexParser.h"

static Term *term_list;
static unsigned int term_count;
static Doc *docs;
static unsigned int docs_len;

/*
 * Calculate term weight.
 * tf: Term Frequency.
 * idf: Inverse Document Frequency.
 */
static TermScoreList* cal_term_weight(const char *path,
					const char *file,
					Term term) {
	unsigned int count;
	unsigned int doc_id;
	unsigned int doc_freq;
	unsigned int i;
	char f[MAXFILEPATH];

	float tf;
	float idf;

	FILE *fp;

	TermScoreList *score;
	TermScoreList *p;
	TermScoreList *n;

	(void) snprintf(f, MAXFILEPATH, "%s/%s/%s",
	    path, INDEXDIR, file);
	if ((fp = fopen(f, "rb")) == NULL) {
		perror(file);
		return (NULL);
	}
	if (fseek(fp, term.freq_abs, SEEK_SET) != 0) {
		(void) fclose(fp);
		return (NULL);
	}

	if (!fread(&count, sizeof (unsigned int), 1, fp)) {
		(void) fclose(fp);
		return (NULL);
	}

	score = NULL;
	p = NULL;
	n = NULL;
	for (i = 0; i < count; i++) {
		(void) fread(&doc_id, sizeof (unsigned int), 1, fp);
		(void) fread(&doc_freq, sizeof (unsigned int), 1, fp);

		/* don't add man3 pages if frequency is larger than NOMAN3 */
		if (count > NOMAN3 && strstr(docs[doc_id].value, "man3")) {
			continue;
		}

		tf = sqrt(doc_freq);
		idf = 1.0 + log(docs_len/(count + 1.0));

		n = (TermScoreList *)malloc(sizeof (TermScoreList));
		if (n == NULL) {
			malloc_error();
		}
		n->term_score.doc_id = doc_id;
		n->term_score.tf = tf;
		n->term_score.idf = idf;
		n->next = NULL;

		if (score == NULL) {
			score = n;
		} else {
			p->next = n;
		}
		p = n;
	}
	(void) fclose(fp);
	return (score);
}

static void free_doc_pos_link(DocPosition *list) {
	DocPosition *priv;
	DocPosition *tmp;

	priv = list;
	while (priv != NULL) {
		tmp = priv->next;
		free(priv);
		priv = tmp;
	}
}

static void free_overlap_link(OverLap *list) {
	OverLap *priv;
	OverLap *tmp;

	priv = list;
	while (priv != NULL) {
		tmp = priv->next;
		free(priv);
		priv = tmp;
	}
}

/*
 * calculate the distance for keywords in given document
 */
static float cal_pos(int size,
			int id,
			int section,
			int *print_row,
			int *len,
			DocPosition **p_list) {
	unsigned int doc_id;
	unsigned short int doc_row;
	unsigned short int doc_col;
	short int doc_sect;
	unsigned int i;
	unsigned int j;
	DocPosition **pos_array;
	DocPosition *t;
	DocPosition *priv_pos;

	OverLap *overlap;
	OverLap *pv_overlap;
	OverLap *pre_overlap;
	OverLap *t_overlap;

	int match_count;
	int match_phase;
	int match_tmp;
	int match_row;
	int match_tmp_row;
	int tmp_row;

	/*
	 * Generate a linked list to store position infos
	 * for document. This linked list will be used to
	 * generate the overlap linked list.
	 */
	pos_array = (DocPosition **) malloc(size * sizeof (DocPosition *));
	if (pos_array == NULL) {
		malloc_error();
	}
	for (i = 0; i < size; i++) {
		pos_array[i] = NULL;
		priv_pos = NULL;

		for (j = 0; j < len[i]; j++) {
			doc_id = p_list[i][j].doc_id;
			doc_sect = p_list[i][j].section;
			doc_row = p_list[i][j].row;
			doc_col = p_list[i][j].col;

			/* list is sorted */
			if (doc_id > id) {
				break;
			}
			if (section != NOSECTION && doc_sect != section) {
				continue;
			}

			if (doc_id == id) {
				t = (DocPosition *)malloc(sizeof (DocPosition));
				if (t == NULL) {
					malloc_error();
				}
				t->doc_id = doc_id;
				t->row = doc_row;
				t->col = doc_col;
				t->section = doc_sect;
				t->next = NULL;
				if (pos_array[i] == NULL) {
					pos_array[i] = t;
				} else {
					priv_pos->next = t;
				}
				priv_pos = t;
			}
		}
	}

	/*
	 * Generate a linked list for overlaps.
	 */
	overlap = NULL;
	for (i = 0; i < size; i++) {
		if (pos_array[i] == NULL) {
			continue;
		}
		priv_pos = pos_array[i];
		j = 0;
		while (priv_pos != NULL) {
			if (j != priv_pos->row) {
				t_overlap = (OverLap *)malloc(sizeof (OverLap));
				if (t_overlap == NULL) {
					malloc_error();
				}
				t_overlap->row = priv_pos->row;
				t_overlap->next = NULL;

				pv_overlap = overlap;
				pre_overlap = NULL;
				while (pv_overlap != NULL) {
					if (pv_overlap->row > priv_pos->row) {
						break;
					} else {
						pre_overlap = pv_overlap;
						pv_overlap = pv_overlap->next;
					}
				}
				if (pre_overlap == NULL) {
					t_overlap->next = pv_overlap;
					overlap = t_overlap;
				} else {
					t_overlap->next = pv_overlap;
					pre_overlap->next = t_overlap;
				}
			}
			j = priv_pos->row;
			priv_pos = priv_pos->next;
		}
	}
	match_count = (size == 1) ? 1 : 0;

	/*
	 * Calculate the overlaps from overlap linked list.
	 */
	pv_overlap = overlap;
	match_phase = 0;
	match_tmp = 0;
	match_row = 0;
	match_tmp_row = 0;
	tmp_row = 0;
	while (pv_overlap != NULL) {
		if (tmp_row == pv_overlap->row) {
			match_count++;
			match_tmp++;
			match_tmp_row = tmp_row;
		} else {
			match_tmp = 0;
		}
		if (match_tmp > match_phase) {
			match_phase = match_tmp;
			match_row = match_tmp_row;
		}
		tmp_row = pv_overlap->row;
		pv_overlap = pv_overlap->next;
	}
	if (match_row == 0 && overlap != NULL) {
		match_row = overlap->row;
	}

	*print_row = match_row;

	for (i = 0; i < size; i++) {
		free_doc_pos_link(pos_array[i]);
	}
	free(pos_array);
	free_overlap_link(overlap);

	return (match_count + 0.0);
}

/*
 * generate document list.  need to free the
 * retrun vaule after using
 */
Doc* read_doc(const char *path) {
	char line[MAXLINESIZE];
	char file[MAXFILEPATH];
	FILE *fp;
	unsigned int doc_len = 0;
	unsigned int i = 0;
	char *pch;

	(void) snprintf(file, MAXFILEPATH, "%s/%s/%s",
	    path, INDEXDIR, TERMDOCFILE);
	if ((fp = fopen(file, "rb")) == NULL) {
		perror(file);
		return (NULL);
	}
	if (fgets(line, sizeof (line), fp) == NULL) {
		(void) fclose(fp);
		return (NULL);
	}
	if (strstr(line, VERSIONSTR) == NULL) {
		(void) fprintf(stderr,
		    gettext("Incorrect index file version number.\n"));
		(void) fclose(fp);
		return (NULL);
	}
	if (fgets(line, sizeof (line), fp) == NULL) {
		(void) fclose(fp);
		return (NULL);
	}
	/* atoi return 0 for invalid conversion */
	if ((doc_len = atoi(line)) == 0) {
		(void) fclose(fp);
		return (NULL);
	}
	docs = (Doc *)malloc(doc_len * sizeof (Doc));
	if (docs == NULL) {
		malloc_error();
	}

	while (fgets(line, sizeof (line), fp) != NULL) {
		pch = strtok(line, "##");
		docs[i].id = i;
		(void) strlcpy(docs[i].value, pch, MAXFILEPATH);

		pch = strtok(NULL, "##");
		docs[i].size = atoi(pch);

		i++;
	}
	(void) fclose(fp);
	docs_len = doc_len;

	return (docs);
}

Term* read_term_index(const char *file) {
	char term_size;
	char s[MAXTERMSIZE];
	char path[MAXFILEPATH];
	unsigned int freq_offset;
	unsigned int pos_offset;
	FILE *fp;

	unsigned int freq_abs;
	unsigned int pos_abs;
	unsigned int i;

	freq_abs = 0;
	pos_abs = 0;

	(void) snprintf(path, MAXFILEPATH, "%s/%s/%s",
	    file, INDEXDIR, TERMFILE);
	if ((fp = fopen(path, "rb")) == NULL) {
		perror(file);
		return (NULL);
	}

	(void) fread(&s, sizeof (VERSIONSTR), 1, fp);
	(void) fread(&term_count, sizeof (int), 1, fp);

	term_list = (Term *)malloc(term_count * sizeof (Term));
	if (term_list == NULL) {
		malloc_error();
	}

	for (i = 0; i < term_count; i++) {
		(void) fread(&term_size, sizeof (char), 1, fp);
		(void) fread(&s, 1, term_size, fp);
		s[term_size] = '\0';

		(void) strcpy(term_list[i].value, s);

		(void) fread(&freq_offset, sizeof (unsigned int), 1, fp);
		(void) fread(&pos_offset, sizeof (unsigned int), 1, fp);

		freq_abs += freq_offset;
		pos_abs += pos_offset;

		term_list[i].freq_abs = VERSIONSTRLEN+1 + 2*(i*2 + 4*freq_abs);
		term_list[i].pos_abs = VERSIONSTRLEN+1 + 2*(i*2 + 5*pos_abs);
	}
	(void) fclose(fp);
	return (term_list);
}

static TermScoreList* get_term_weight(const char *s,
			int *pos, const char *path) {
	Term *tmp;
	int res;
	TermScoreList *list = NULL;
	int i;

	tmp = term_list;
	if (tmp == NULL) {
		(void) fprintf(stderr,
		    gettext("Term List is NULL\n"));
	}
	*pos = 0;
	for (i = 0; i < term_count; i++) {
		res = compare_str(tmp[i].value, s);
		if (res == 0) {
			list = cal_term_weight(path, TERMFREQ, tmp[i]);
			*pos = tmp[i].pos_abs;
			return (list);
		} else if (res > 0) {
			break;
		}
	}
	return (NULL);
}

static int check_doc_exist(unsigned int doc_id, ScoreList *score_list) {
	while (score_list != NULL) {
		if (doc_id == score_list->doc_score.doc_id) {
			return (1);
		}
		score_list = score_list->next;
	}

	return (0);
}

static float cal_doc_score(TermScoreList **list,
			int size, unsigned int doc_id) {
	TermScoreList *term_score;
	int i;
	float idf;
	float tf;
	float sum_idf2;
	float sum_tfidf2;
	float res;
	int file_size;
	int fp;

	file_size = docs[doc_id].size;
	sum_idf2 = 0.0;
	sum_tfidf2 = 0.0;
	fp = 0;
	for (i = 0; i < size; i++) {
		term_score = list[i];
		while (term_score != NULL) {
			if (doc_id == term_score->term_score.doc_id) {

				idf = term_score->term_score.idf;
				tf = term_score->term_score.tf;

				sum_idf2 += idf*idf;
				sum_tfidf2 += tf*idf*idf/sqrt(file_size);
				fp++;
			}
			term_score = term_score->next;
		}
	}
	res = (fp+0.0)/size * sum_tfidf2 / sqrt(sum_idf2);

	return (res);
}

static float cal_distance(int size, unsigned int doc_id, int section,
			int *match, int *len, DocPosition **p) {
	float res = 1.0;

	res = cal_pos(size, doc_id, section, match, len, p);

	return (res);
}

/* generate array of position list for given term */
static int gen_doc_pos(int pos_abs, const char *path, DocPosition **dp) {
	FILE *fp;
	char f[MAXFILEPATH];
	DocPosition *t;
	unsigned int count;
	unsigned int doc_id;
	short int doc_sect;
	unsigned short int doc_row;
	unsigned short int doc_col;
	int i;

	*dp = NULL;
	if (pos_abs == 0) {
		return (0);
	}

	(void) snprintf(f, MAXFILEPATH, "%s/%s/%s",
	    path, INDEXDIR, TERMPOSITION);
	if ((fp = fopen(f, "rb")) == NULL) {
		perror(f);
		return (0);
	}
	if (fseek(fp, pos_abs, SEEK_SET) != 0) {
		(void) fclose(fp);
		return (1);
	}

	if (fread(&count, sizeof (unsigned int), 1, fp) == NULL) {
		(void) fclose(fp);
		return (1);
	}
	t = (DocPosition *)malloc(count * sizeof (DocPosition));
	if (t == NULL) {
		malloc_error();
	}
	for (i = 0; i < count; i++) {
		(void) fread(&doc_id, sizeof (unsigned int), 1, fp);
		(void) fread(&doc_row, sizeof (unsigned short int), 1, fp);
		(void) fread(&doc_col, sizeof (unsigned short int), 1, fp);
		(void) fread(&doc_sect, sizeof (short int), 1, fp);

		t[i].doc_id = doc_id;
		t[i].row = doc_row;
		t[i].col = doc_col;
		t[i].section = doc_sect;
	}
	(void) fclose(fp);
	*dp = t;

	return (count);
}


static ScoreList* get_doc_score(TermScoreList **list,
				int *pos_list,
				const Keyword *w,
				const char *path) {
	int i;
	ScoreList *res;
	ScoreList *p;
	ScoreList *priv;
	TermScoreList *term_score;
	unsigned int id;
	DocPosition **dp;
	char msc[MAXSEC];
	int *len;
	float score;
	float fdisit;
	float fman3;
	int match_row;
	int size;
	int section;

	size = w->size;
	section = w->sid;

	dp = (DocPosition **)malloc(size * sizeof (DocPosition *));
	if (dp == NULL) {
		malloc_error();
	}

	if ((len = (int *)malloc(size * sizeof (int))) == NULL) {
		malloc_error();
	}

	for (i = 0; i < size; i++) {
		len[i] = gen_doc_pos(pos_list[i], path, &dp[i]);
	}

	res = NULL;
	priv = NULL;
	for (i = 0; i < size; i++) {
		term_score = list[i];
		while (term_score != NULL) {
			id = term_score->term_score.doc_id;
			/*
			 * Only search for specified section if -s is set.
			 */
			if (w->msc[0] != '\0') {
				(void) snprintf(msc, MAXSEC, "/man%s/", w->msc);
				if (strstr(docs[id].value, msc) == NULL) {
					term_score = term_score->next;
					continue;
				}
			}

			if (check_doc_exist(id, res) == 0) {
				score = cal_doc_score(list, size, id);
				fdisit = cal_distance(size, id, section,
				    &match_row, len, dp);
				if (strstr(docs[id].value, "/man3") != NULL) {
					fman3 = MAN3FACTOR;
				} else {
					fman3 = NONMAN3FACTOR;
				}
				p = (ScoreList *)malloc(sizeof (ScoreList));
				if (p == NULL) {
					malloc_error();
				}
				p->doc_score.doc_id = id;
				(void) strlcpy(p->doc_score.path,
				    docs[id].value, MAXFILEPATH);
				p->doc_score.match_row = match_row;
				p->doc_score.score = fman3 * (fdisit + score);
				p->next = NULL;

				if (res == NULL) {
					res = p;
				} else {
					priv->next = p;
				}
				priv = p;
			}
			term_score = term_score->next;
		}
	}
	for (i = 0; i < size; i++) {
		if (dp[i]) {
			free(dp[i]);
		}
	}
	if (dp) {
		free(dp);
	}
	if (len) {
		free(len);
	}

	return (res);
}

void free_doc_score_list(ScoreList *list) {
	ScoreList *priv;
	ScoreList *tmp;

	priv = list;
	while (priv) {
		tmp = priv->next;
		free(priv);
		priv = tmp;
	}
}

static ScoreList* sort_list(ScoreList *list) {
	ScoreList *new_list = NULL;
	ScoreList *priv;
	ScoreList *pre;
	ScoreList *p;

	while (list != NULL) {
		p = (ScoreList *)malloc(sizeof (ScoreList));
		if (p == NULL) {
			malloc_error();
		}
		p->doc_score.doc_id = list->doc_score.doc_id;
		(void) strlcpy(p->doc_score.path,
		    list->doc_score.path, MAXFILEPATH);
		p->doc_score.score = list->doc_score.score;
		p->doc_score.match_row = list->doc_score.match_row;
		p->next = NULL;

		if (new_list == NULL) {
			new_list = p;
			list = list->next;
			continue;
		}
		priv = new_list;
		pre = NULL;
		while (priv != NULL) {
			if (strcmp(priv->doc_score.path,
			    p->doc_score.path) == 0) {
				free(p);
				break;
			}
			if (priv->doc_score.score < p->doc_score.score) {
				if (pre == NULL) {
					new_list = p;
					p->next = priv;
					break;
				}
				pre->next = p;
				p->next = priv;
				break;
			}
			pre = priv;
			priv = priv->next;
		}
		if (priv == NULL) {
			pre->next = p;
		}
		list = list->next;
	}
	free_doc_score_list(list);

	return (new_list);
}

static void free_term_score_list(TermScoreList *list) {
	TermScoreList *priv;
	TermScoreList *tmp;

	priv = list;
	while (priv != NULL) {
		tmp = priv->next;
		free(priv);
		priv = tmp;
	}
}

static void highlight_str(char *s_str, const char *s_pattern) {
	int  str_len;
	char tmp_str[MAXLINESIZE];
	char *find_pos;
	int i;

	(void) replace_str(s_str, s_pattern, "##");

	find_pos = strstr(s_str, "##");
	if ((!find_pos) || (!s_pattern)) {
		return;
	}

	while (find_pos) {
		(void) memset(tmp_str, 0, sizeof (tmp_str));
		str_len = find_pos - s_str;
		(void) strncpy(tmp_str, s_str, str_len);
		for (i = 0; i < strlen(s_pattern); i++) {
			(void) strlcat(tmp_str, "_", MAXLINESIZE);
			tmp_str[strlen(tmp_str)] = s_pattern[i];
		}
		(void) strlcat(tmp_str, find_pos + strlen("##"),
		    MAXLINESIZE);
		(void) strlcpy(s_str, tmp_str, MAXLINESIZE);

		find_pos = strstr(s_str, "##");
	}
}

/*
 * print out a summary of matched content.
 * format:
 * 	COMMAND(SECTION) FIELD MANPAGE_PATH
 *	SUMMARY
 */
static int print_match_row(char *file_name, int row, const char *w) {
	FILE *man_file;
	int i;
	int j;
	char line[MAXLINESIZE];
	char sect[MAXSECTIONSIZE];
	Keyword *k;

	if ((man_file = fopen(file_name, "r")) == NULL) {
		perror(file_name);
		return (0);
	}

	(void) get_words(w, &k, NULL, 1);

	i = 0;
	while (i < row && fgets(line, sizeof (line), man_file) != NULL) {
		if (strlen(line) > 5 && line[0] == '.' &&
		    line[1] == 'S' && line[2] == 'H') {
			(void) strlcpy(sect, &line[4], MAXSECTIONSIZE);
		}
		i++;
	}
	(void) replace_str(line, "\\fB", "");
	(void) replace_str(line, "\\fR", "");
	(void) replace_str(line, "\\fP", "");
	(void) replace_str(line, "\\fI", "");
	(void) replace_str(line, "\n", "");
	(void) replace_str(line, "\\", "");
	(void) replace_str(line, "&", "");
	(void) replace_str(line, "*(lq", "\"");
	(void) replace_str(line, "*(rq", "\"");

	for (i = 0; i < k->size; i++) {
		highlight_str(line, k->word[i]);
	}

	(void) replace_str(sect, "\n", "");
	(void) replace_str(sect, "\\fB", "");
	(void) replace_str(sect, "\\fR", "");
	(void) replace_str(sect, "\\fP", "");
	(void) replace_str(sect, "\\fI", "");

	(void) printf("\t%s\t%s\n", sect, file_name);
	/*
	 * wrap the line for every LINEWRAP characters.
	 * need to consider not to split word.
	 */
	j = 0;
	for (i = 0; i < strlen(line); i++) {
		if ((i % LINEWRAP) == 0 && i != 0) {
			j = 1;
		}
		if (j == 1 && line[i] == ' ') {
			(void) printf("\n");
			j = 0;
			continue;
		}
		(void) printf("%c", line[i]);
	}
	(void) printf("\n\n");

	(void) free_keyword(k);

	(void) fclose(man_file);
	return (0);
}

static void get_command(const char *f, char *com) {
	char tmp[MAXFILEPATH];
	char *q;

	(void) strlcpy(tmp, f, MAXFILEPATH);
	if (q = strrchr(tmp, '/')) {
		(void) strlcpy(com, q+1, MAXFILEPATH);
	}
	if (q = strrchr(com, '.')) {
		*q = '\0';
	}
}

static void get_sect(const char *f, char *sec) {
	char tmp[MAXFILEPATH];
	char *q;

	(void) strlcpy(tmp, f, MAXFILEPATH);
	if (q = strrchr(tmp, '.')) {
		(void) strlcpy(sec, q+1, MAXFILEPATH);
	}
}

void print_score_list(ScoreList *list, const char *w) {
	ScoreList *l;
	int i;
	char com[MAXFILEPATH];
	char sec[MAXFILEPATH];
	char op[MAXLINESIZE];

	l = list;
	for (i = 0; l != NULL; l = l->next) {
		if (l->doc_score.match_row == 0) {
			continue;
		}
		get_command(l->doc_score.path, com);
		get_sect(l->doc_score.path, sec);
		(void) snprintf(op, MAXLINESIZE, "\n%d. %s(%s)", ++i, com, sec);

		(void) printf("%s", op);
		(void) print_match_row(l->doc_score.path,
		    l->doc_score.match_row, w);
	}
}

/* combine current socre list with existing list */
static int gen_result_list(ScoreList *list, ScoreList **score_list) {
	ScoreList *doc_score;
	doc_score = list;

	/* keep first RESULTLEN node */
	while (list != NULL && list->next != NULL) {
		list = list->next;
	}
	if (list != NULL) {
		list->next = *score_list;
	}
	*score_list = list ? sort_list(doc_score) : *score_list;
	return (0);
}

/* calculate score for the documents which contain given keywords. */
int cal_score(const Keyword *w, const char *path, ScoreList **score_list) {
	char m[MAXTERMSIZE];
	TermScoreList **term_list;
	ScoreList *doc_list;
	ScoreList *doc_list_sorted;
	int i;
	int *pos_list;

	term_list = (TermScoreList**)malloc(w->size * sizeof (TermScoreList*));
	if (term_list == NULL) {
		malloc_error();
	}
	pos_list = (int *)malloc(w->size * sizeof (int));
	if (pos_list == NULL) {
		malloc_error();
	}

	for (i = 0; i < w->size; i++) {
		(void) strlcpy(m, w->word[i], MAXTERMSIZE);
		term_list[i] = get_term_weight(m, &pos_list[i], path);
	}
	doc_list = get_doc_score(term_list, pos_list, w, path);
	doc_list_sorted = sort_list(doc_list);
	(void) gen_result_list(doc_list_sorted, score_list);

	for (i = 0; i < w->size; i++) {
		free_term_score_list(term_list[i]);
	}
	if (term_list != NULL)
		free(term_list);
	if (pos_list != NULL)
		free(pos_list);

	return (0);
}
