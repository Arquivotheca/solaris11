/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "iconv_int.h"


symbol_t *
create_symbol(char *id, uint64_t val)
{
	symbol_t	*sym;

	sym = MALLOC(symbol_t, 1);
	sym->id = id;
	sym->next = NULL;
	sym->val = val;

	return (sym);
}

static int
hash(const char *id, int size)
{
	unsigned char	*__cp = (unsigned char *)id;
	unsigned char	__c;
	unsigned int	__id = 0;
	while ((__c = *__cp++) != '\0') {
		__id = (__id >> 5) + (__id << 27) + __c;
	}
	return (((__id % 899981) + 100000) % size);
}

void
add_symbol(symbol_t *sym, symtab_t *sym_tab)
{
	symbol_t	*p, *q;
	int	hdx, c;

	hdx = hash(sym->id, sym_tab->size);

	p = sym_tab->symbols[hdx];
	q = NULL;
	while (p) {
		if ((c = strcmp(p->id, sym->id)) > 0) {
			break;
		} else if (c < 0) {
			q = p;
			p = p->next;
		} else {
			INTERNAL_ERROR;
		}
	}
	sym->next = p;
	if (q) {
		q->next = sym;
	} else {
		sym_tab->symbols[hdx] = sym;
	}
}

symbol_t *
loc_symbol(char *id, symtab_t *sym_tab)
{
	symbol_t	*p;
	int	c;
	int	hdx;

	hdx = hash(id, sym_tab->size);
	p = sym_tab->symbols[hdx];

	while (p) {
		if ((c = strcmp(p->id, id)) == 0) {
			return (p);
		} else if (c > 0) {
			return (NULL);
		}
		p = p->next;
	}

	return (NULL);
}
