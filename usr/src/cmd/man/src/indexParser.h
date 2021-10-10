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

#ifndef _INDEX_PARSER_H
#define	_INDEX_PARSER_H

#include "indexUtil.h"

typedef struct _DocFreq {
	unsigned int doc_id;
	unsigned int freq;
	struct _DocFreq *next;
} DocFreq;

typedef struct _DocPosition {
	unsigned int doc_id;
	unsigned short int row;
	unsigned short int col;
	short int section;
	struct _DocPosition *next;
} DocPosition;

typedef struct {
	char value[MAXTERMSIZE];
	unsigned short int  freq;
	DocFreq *link_freq;
	DocPosition *link_position;
} TermInfo;

typedef struct _TermNode {
	TermInfo term;
	struct _TermNode *left;
	struct _TermNode *right;
} TermNode;

typedef struct _DocInfo {
	char value[MAXFILEPATH];
	unsigned int id;
	struct _DocInfo *next;
	unsigned int size;
} DocInfo;

TermNode* parse_roff(const char *, unsigned int, TermNode *);
TermNode* merge_link(TermNode *, TermNode *);

#endif /* _INDEX_PARSER_H */
