/*
 * Copyright 1996-2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */
/*
 * OSF/1 1.2
 */
/*
 * #if !defined(lint) && !defined(_NOIDENT)
 * static char rcsid[] = "@(#)$RCSfile: symtab.c,v $ $Revision: 1.3.2.3 $"
 *	" (OSF) $Date: 1992/02/18 20:26:18 $";
 * #endif
 */
/*
 * COMPONENT_NAME: (CMDLOC) Locale Database Commands
 *
 * FUNCTIONS:
 *
 *
 * (C) COPYRIGHT International Business Machines Corp. 1991
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * 1.2  com/cmd/nls/symtab.c, bos320 5/10/91 10:18:07
 */
#include <stdlib.h>
#include <limits.h>
#include "locdef.h"

static symbol_t	*cm_symtab[HASH_TBL_SIZE]; /* symbol table */

/*
 *  FUNCTION: create_symbol
 *
 *  DESCRIPTION:
 *  Creates an instance of a symbol_t.  This routine malloc()s the space
 *  necessary to contain the symbol_t but not for it's data.
 */
symbol_t *
create_symbol(char *id)
{
	symbol_t *sym;

	sym = MALLOC(symbol_t, 1);
	sym->sym_id = STRDUP(id);

	sym->data.str = NULL;
	sym->next = NULL;

	return (sym);
}


/*
 * FUNCTION: hash
 *
 * DESCRIPTION:
 * This hash algorithm is based on the one used in <sys/strlog.h>.
 */
static int
hash(char *id)
{
	unsigned char	*cp = (unsigned char *)id;
	unsigned char	c;
	unsigned int	hashval = 0;

	while ((c = *cp++) != '\0') {
		hashval = (hashval >> 5) + (hashval << 27) + c;
	}
	return ((int)(((hashval % 899981) + 100000) % HASH_TBL_SIZE));
}


/*
 *  FUNCTION: add_symbol
 *
 *  DESCRIPTION:
 *  Adds a symbol to the symbol table.  The symbol table is implemented
 *  as a hash table pointing to linked lists of symbol entries.
 *
 *      +--- array of pointers to lists indexed by hash(symbol->sym_id)
 *      |
 *      |
 *      V      /--linked list of symbols --\
 *  +------+   +------+-+         +------+-+
 *  |    --+-->|      |-+-- ...-->|      |^|
 *  +------+   +------+_+         +------+-+
 *  |    --+--> . . .
 *  +------+
 *     .
 *     .
 *     .
 *  +------+   +------+-+         +------+-+
 *  |    --+-->|      |-+-- ...-->|      |^|
 *  +------+   +------+-+         +------+-+
 */
int
add_symbol(symbol_t *sym)
{
	symbol_t *p, *q;
	int	c;
	int	hdx;

	hdx = hash(sym->sym_id);

	p = cm_symtab[hdx];
	q = NULL;
	while (p) {
		if ((c = strcmp(p->sym_id, sym->sym_id)) > 0) {
			break;
		} else if (c < 0) {
			q = p;
			p = p->next;
		} else {
			return (ST_DUP_SYMBOL);
		}
	}
	sym->next = p;
	if (q) {
		q->next = sym;
	} else {
		cm_symtab[hdx] = sym;
	}
	return (ST_OK);
}

/*
 *  FUNCTION: loc_symbol
 *
 *  DESCRIPTION:
 *  Locates a symbol with sym_id matching 'id' in the symbol table 'sym_tab'.
 *  The functions hashes 'id' and searches the linked list indexed for
 *  a matching symbol.  See comment for add_symbol for detail of symbol
 *  table structure.
 */
symbol_t *
loc_symbol(char *id)
{
	symbol_t *p;
	int	c;
	int	hdx;

	hdx = hash(id);
	p = cm_symtab[hdx];

	while (p) {
		if ((c = strcmp(p->sym_id, id)) == 0) {
			/* MATCH */
			return (p);
		} else if (c > 0) {
			/* OVER-RUN, none possible */
			return (NULL);
		}
		p = p->next;
	}

	return (NULL);
}

/*
 * FUNCTION: clear_symtab
 */
void
clear_symtab(void)
{
	int	i;

	for (i = 0; i < HASH_TBL_SIZE; i++) {
		cm_symtab[i] = NULL;
	}
}

#ifdef	ODEBUG
/*
 * FUNCTION: dump_symtab
 */
void
dump_symtab(void)
{
	int	i, j;
	symbol_t	*p;

	for (i = 0; i < HASH_TBL_SIZE; i++) {
		j = 0;
		p = cm_symtab[i];
		while (p) {
			(void) printf("bucket #%d - %d\n", i, j);
			j++;
			p = p->next;
		}
	}
}
#endif

/* static implementing symbol stack */
static size_t	stack_top = 0;
static symbol_t	**stack = NULL;
static size_t	max_stack = 0;
#define	DEF_STACK_SIZE	4
#define	MAX_STACK_SIZE	(SIZE_MAX / (sizeof (symbol_t *)))
/*
 *  FUNCTION: sym_push
 *
 *  DESCRIPTION:
 *  Pushes a symbol on the symbol stack.
 */
int
sym_push(symbol_t *sym)
{
	if (stack_top < max_stack) {
		stack[stack_top++] = sym;
		return (ST_OK);
	} else {	/* stack overflow or not initialized */
		if (max_stack == 0) {	/* not initialized */
			stack = MALLOC(symbol_t *, DEF_STACK_SIZE);
			max_stack = DEF_STACK_SIZE;
#ifdef SDEBUG
			(void) fprintf(stderr, "sym stack initialized: %d\n",
				max_stack);
#endif
		} else if (max_stack == MAX_STACK_SIZE) {
			/* stack cannot be expanded any more */
			error(2, gettext(ERR_NO_MORE_STACK));
		} else {		/* stack overflow */
			size_t	new_size;
			symbol_t	**new_stack;

			if (MAX_STACK_SIZE - max_stack > max_stack) {
				new_size = max_stack * 2;
			} else {
				new_size = MAX_STACK_SIZE;
			}
			new_stack = REALLOC(symbol_t *, stack, new_size);
#ifdef SDEBUG
			(void) fprintf(stderr,
			    "sym stack realloced: %d\n", new_size);
#endif
			stack = new_stack;
			max_stack = new_size;
		}
		stack[stack_top++] = sym;
		return (ST_OK);
	}
}


/*
 *  FUNCTION: sym_pop
 *
 *  DESCRIPTION:
 *  Pops a symbol off the symbol stack, returning it's address to the caller.
 */
symbol_t *
sym_pop(void)
{
	if (stack_top != 0)
		return (stack[--stack_top]);
	else
		return (NULL);
}

/*
 * FUNCTION: sym_free_chr
 */
void
sym_free_chr(symbol_t *s)
{
	if (s->sym_type == ST_CHR_SYM) {
		if (s->data.chr) {
			free(s->data.chr);
			s->data.chr = NULL;
		}
	}
}

/*
 * FUNCTION: sym_free_all
 */
void
sym_free_all(symbol_t *s)
{
	sym_free_chr(s);
	free(s->sym_id);
	free(s);
}
