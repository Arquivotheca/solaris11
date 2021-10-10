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

#ifndef	_INDEXREADER_H
#define	_INDEXREADER_H

#include "indexUtil.h"
#include "indexParser.h"

typedef struct {
	unsigned char prefix_len;
	char value[MAXTERMSIZE];
	unsigned short int freq;
	unsigned int freq_abs;
	unsigned int pos_abs;
} Term;

typedef struct _Doc {
	char value[MAXFILEPATH];
	unsigned int id;
	unsigned int size;
} Doc;

typedef struct _TermScore {
	unsigned int doc_id;
	float idf;
	float tf;
	unsigned int pos_abs;
} TermScore;

typedef struct _TermScoreList {
	TermScore term_score;
	struct _TermScoreList *next;
} TermScoreList;

typedef struct _OverLap {
	int row;
	struct _OverLap *next;
} OverLap;

typedef struct _DocScore {
	unsigned int doc_id;
	int match_row;
	char path[MAXFILEPATH];
	float score;
} DocScore;

typedef struct _ScoreList {
	DocScore doc_score;
	struct _ScoreList *next;
} ScoreList;

int cal_score(const Keyword *, const char *, ScoreList **);

Doc* read_doc(const char *);

Term* read_term_index(const char *);

void print_score_list(ScoreList *, const char *);

void free_doc_score_list(ScoreList *);

#endif /* _INDEXREADER_H */
