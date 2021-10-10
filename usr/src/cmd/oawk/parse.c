/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#ident	"%Z%%M%	%I%	%E% SMI"

#include "awk.def"
#include "awk.h"
#include "stdio.h"

NODE *nodealloc(n)
{
	register NODE *x;
	x = (NODE *) malloc(sizeof (NODE) + (n-1)*sizeof (NODE *));
	if (x == NULL)
		error(FATAL, "out of space in nodealloc");
	return (x);
}




NODE *exptostat(a) NODE *a;
{
	a->ntype = NSTAT;
	return (a);
}




NODE *node0(a)
{
	register NODE *x;
	x = nodealloc(0);
	x->nnext = NULL;
	x->nobj = a;
	return (x);
}




NODE *node1(a, b) NODE *b;
{
	register NODE *x;
	x = nodealloc(1);
	x->nnext = NULL;
	x->nobj = a;
	x->narg[0]=b;
	return (x);
}




NODE *node2(a, b, c) NODE *b, *c;
{
	register NODE *x;
	x = nodealloc(2);
	x->nnext = NULL;
	x->nobj = a;
	x->narg[0] = b;
	x->narg[1] = c;
	return (x);
}




NODE *node3(a, b, c, d) NODE *b, *c, *d;
{
	register NODE *x;
	x = nodealloc(3);
	x->nnext = NULL;
	x->nobj = a;
	x->narg[0] = b;
	x->narg[1] = c;
	x->narg[2] = d;
	return (x);
}




NODE *node4(a, b, c, d, e) NODE *b, *c, *d, *e;
{
	register NODE *x;
	x = nodealloc(4);
	x->nnext = NULL;
	x->nobj = a;
	x->narg[0] = b;
	x->narg[1] = c;
	x->narg[2] = d;
	x->narg[3] = e;
	return (x);
}




NODE *stat3(a, b, c, d) NODE *b, *c, *d;
{
	register NODE *x;
	x = node3(a, b, c, d);
	x->ntype = NSTAT;
	return (x);
}




NODE *op2(a, b, c) NODE *b, *c;
{
	register NODE *x;
	x = node2(a, b, c);
	x->ntype = NEXPR;
	return (x);
}




NODE *op1(a, b) NODE *b;
{
	register NODE *x;
	x = node1(a, b);
	x->ntype = NEXPR;
	return (x);
}




NODE *stat1(a, b) NODE *b;
{
	register NODE *x;
	x = node1(a, b);
	x->ntype = NSTAT;
	return (x);
}




NODE *op3(a, b, c, d) NODE *b, *c, *d;
{
	register NODE *x;
	x = node3(a, b, c, d);
	x->ntype = NEXPR;
	return (x);
}




NODE *stat2(a, b, c) NODE *b, *c;
{
	register NODE *x;
	x = node2(a, b, c);
	x->ntype = NSTAT;
	return (x);
}




NODE *stat4(a, b, c, d, e) NODE *b, *c, *d, *e;
{
	register NODE *x;
	x = node4(a, b, c, d, e);
	x->ntype = NSTAT;
	return (x);
}




NODE *valtonode(a, b) CELL *a;
{
	register NODE *x;
	x = node0(a);
	x->ntype = NVALUE;
	x->subtype = b;
	return (x);
}




NODE *pa2stat(a, b, c) NODE *a, *b, *c;
{
	register NODE *x;
	x = node4(PASTAT2, a, b, c, (NODE *) paircnt);
	paircnt++;
	x->ntype = NSTAT;
	return (x);
}




NODE *linkum(a, b) NODE *a, *b;
{
	register NODE *c;
	if (a == NULL) return (b);
	else if (b == NULL) return (a);
	for (c = a; c->nnext != NULL; c=c->nnext)
		;
	c->nnext = b;
	return (a);
}




NODE *genprint()
{
	register NODE *x;
	static wchar_t L_record[] = L"$record";
	x = stat2(PRINT, valtonode(lookup(L_record, symtab, 0), CFLD), NULL);
	return (x);
}
