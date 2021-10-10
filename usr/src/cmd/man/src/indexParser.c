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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <string.h>

#include "indexParser.h"
#include "indexUtil.h"

/*
 * Define stop words. Stop words don't need to be
 * stored and treat as query term.
 */
static const char *ENGLISH_STOP_WORDS[]  = {
	"a", "an", "and", "are", "as", "at", "be",
	"but", "by", "for", "if", "in", "into", "is", "it",
	"no", "not", "of", "on", "or", "s", "such",
	"t", "that", "the", "their", "then", "there",
	"these", "they", "this", "to", "was", "will", "with",
	NULL
};

int find_stopword(const char *term) {
	int i = 0;
	while (ENGLISH_STOP_WORDS[i]) {
		if (strcmp(term, ENGLISH_STOP_WORDS[i]) == 0) {
			return (1);
		}
		i++;
	}
	return (0);
}

/*
 * Generate a linked list for term frequency.
 * (document id, frequency)
 * frequency is how many time this word shown
 * in this document.
 */
static void update_freq(TermNode *link, unsigned int doc_id)
{
	DocFreq *temp, *priv;

	priv = link->term.link_freq;
	if (priv == NULL) {
		temp = (DocFreq *)malloc(sizeof (DocFreq));
		if (temp == NULL) {
			malloc_error();
		}
		temp->doc_id = doc_id;
		temp->freq = 1;
		temp->next = NULL;
		link->term.link_freq = temp;
	} else {
		while (priv != NULL) {
			temp = priv;
			if (doc_id == temp->doc_id) {
				temp->freq++;
				return;
			}
			priv = priv->next;
		}
		priv = (DocFreq *)malloc(sizeof (DocFreq));
		if (priv == NULL) {
			malloc_error();
		}
		priv->doc_id = doc_id;
		priv->freq = 1;
		priv->next = NULL;
		temp->next = priv;

	}
}

/*
 * Generate a linked list for term position.
 * (document id, row, colume)
 */
static void update_position(TermNode *link,
			unsigned int doc_id,
			int row,
			int col,
			int section)
{
	DocPosition *temp, *priv;

	priv = link->term.link_position;

	temp = (DocPosition *)malloc(sizeof (DocPosition));
	if (temp == NULL) {
		malloc_error();
	}
	temp->doc_id = doc_id;
	temp->row = row;
	temp->col = col;
	temp->section = section;
	temp->next = NULL;

	if (priv == NULL) {
		link->term.link_position = temp;
	} else {
		while (priv->next != NULL) {
			priv = priv->next;
		}
		priv->next = temp;
	}
}

static TermNode* add_term_node(char *term,
			unsigned int doc_id,
			int row,
			int col,
			int section) {
	TermNode* new_term;

	new_term = (TermNode *)malloc(sizeof (TermNode));
	if (new_term == NULL) {
		malloc_error();
	}

	(void) strlcpy(new_term->term.value, term, MAXTERMSIZE);
	new_term->term.freq = 1;
	new_term->term.link_position = NULL;
	new_term->term.link_freq = NULL;
	new_term->left = NULL;
	new_term->right = NULL;

	update_freq(new_term, doc_id);
	update_position(new_term, doc_id, row, col, section);

	return (new_term);
}

static int append_freq(TermNode *s, TermNode *t) {
	DocFreq *priv;

	priv = s->term.link_freq;
	while (priv->next) {
		priv = priv->next;
	}
	priv->next = t->term.link_freq;

	return (0);
}

static int append_pos(TermNode *s, TermNode *t) {
	DocPosition *priv;

	priv = s->term.link_position;
	while (priv->next) {
		priv = priv->next;
	}
	priv->next = t->term.link_position;

	return (0);
}

static TermNode* append_term_node(TermNode *p) {
	TermNode *t;

	t = (TermNode *)malloc(sizeof (TermNode));
	if (t == NULL) {
		malloc_error();
	}

	t->left = NULL;
	t->right = NULL;

	(void) strlcpy(t->term.value, p->term.value, MAXTERMSIZE);
	t->term.freq = p->term.freq;
	t->term.link_freq = p->term.link_freq;
	t->term.link_position = p->term.link_position;

	return (t);
}

/*
 * Merge two term linked lists into one. Merge factor is defined
 * by MERGEFACTOR.
 */
TermNode* merge_link(TermNode *source, TermNode *target) {
	TermNode *priv_source;
	TermNode *priv_target;
	char s[MAXTERMSIZE];
	char t[MAXTERMSIZE];
	int comp;

	if (target == NULL) {
		return (source);
	}

	if (source == NULL) {
		return (target);
	}

	(void) merge_link(source, target->left);
	priv_source = source;
	priv_target = target;
	while (priv_source) {
		(void) strlcpy(s, priv_source->term.value, MAXTERMSIZE);
		(void) strlcpy(t, priv_target->term.value, MAXTERMSIZE);
		comp = compare_str(s, t);
		if (comp == 0) {
			priv_source->term.freq += priv_target->term.freq;
			(void) append_freq(priv_source, priv_target);
			(void) append_pos(priv_source, priv_target);
			break;
		} else if (comp < 0) {
			if (priv_source->right == NULL) {
				priv_source->right = append_term_node(
				    priv_target);
				break;
		} else {
				priv_source = priv_source->right;
			}
		} else {
			if (priv_source->left == NULL) {
				priv_source->left = append_term_node(
				    priv_target);
				break;
			} else {
				priv_source = priv_source->left;
			}
		}
	}
	(void) merge_link(source, target->right);
	free(priv_target);

	return (source);
}

/*
 * List term into linked list. This linked list is a
 * sorted binary tree.
 * 	left node < root < right node
 * This binary tree grows quickly. In order to increase
 * the formance, merge factor is defined. For every
 * MERGEFACTOR documents, new list will be merge into
 * main trunk.
 */

static TermNode* insert_term(char *term,
			unsigned int doc_id,
			int row,
			int col,
			int section,
			TermNode *term_list) {
	TermNode* term_node;
	TermNode* priv;
	char s[MAXTERMSIZE];
	int comp;

	static int tmp_doc_id = -1;

	priv = term_list;

	if (priv == NULL) {
		term_node = add_term_node(term, doc_id, row, col, section);
		term_list = term_node;
		return (term_list);
	}

	while (priv) {
		(void) strlcpy(s, priv->term.value, MAXTERMSIZE);

		comp = compare_str(s, term);

		if (comp == 0) {
			if (tmp_doc_id != doc_id) {
				priv->term.freq++;
				tmp_doc_id = doc_id;
			}
			update_freq(priv, doc_id);
			update_position(priv, doc_id, row, col, section);
			return (term_list);
		} else if (comp < 0) {
			if (priv->right == NULL) {
				term_node = add_term_node(term, doc_id,
						row, col, section);
				priv->right = term_node;

				return (term_list);
			} else {
				priv = priv->right;
			}
		} else {
			if (priv->left == NULL) {
				term_node = add_term_node(term, doc_id,
				    row, col, section);
				priv->left = term_node;
				return (term_list);
			} else {
				priv = priv->left;
			}
		}
	}
	return (term_list);
}

TermNode *parse_roff(const char *path,
			unsigned int doc_id,
			TermNode *term_link) {
	FILE *fp;
	char line[MAXLINESIZE];
	unsigned int line_num;
	char term[MAXTERMSIZE];
	char s[MAXSECTIONSIZE];
	int sid = 0;
	char *p;

	if ((fp = fopen(path, "r")) == NULL) {
		perror(path);
		return (0);
	}

	line_num = 0;
	while (fgets(line, sizeof (line), fp) != NULL) {
		line_num++;
		/*
		 * it's a section line if starts with ".SH"
		 */
		if (strlen(line) > 0 && line[0] == '.') {
			if (strlen(line) > 5 && line[0] == '.' &&
			    line[1] == 'S' && line[2] == 'H') {
			    (void) strlcpy(s, &line[4], MAXSECTIONSIZE);
				(void) replace_str(s, "\n", "");
				sid = find_section(s);
			}
			continue;
		}
		p = strtok(line, " ");
		while (p != NULL) {
			if (strlen(p) > 2 && (*p == '\\' || isalpha(*p))) {
				(void) strlcpy(term, p, MAXTERMSIZE);
				(void) normalize(term);
				if (strlen(term) > 2 && !find_stopword(term)) {
					term_link = insert_term(term, doc_id,
					    line_num, 0, sid, term_link);
				}
			}
			p = strtok(NULL, " ");
		}
	}
	(void) fclose(fp);

	return (term_link);
}
