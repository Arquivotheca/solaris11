/*
 * Copyright (c) 1996, 2011, Oracle and/or its affiliates. All rights reserved.
 */

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
 * static char rcsid[] = "@(#)$RCSfile: sem_chr.c,v $ $Revision: 1.4.6.3 $"
 *	" (OSF) $Date: 1992/12/11 14:36:56 $";
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
 * 1.14  com/cmd/nls/sem_chr.c, cmdnls, bos320, 9138320 9/11/91 16:33:58
 */
#include <widec.h>
#include "locdef.h"


/*	One bit per process code point */
static unsigned char *defined_pcs = NULL;
static int	cur_pcs_size = 0;

static unsigned char	*t_cm_width;
static unsigned char	default_column_width = UNDEF_WIDTH;
static unsigned char	defwidth;

static uint64_t	fc_from_fc(uint64_t);

/*
 * for storing EUC min and max pc's for euc<-->dense pc conversions
 */
static wchar_t	euc_cs1_min = 0x30001020;
static wchar_t	euc_cs2_min = 0x10000020;
static wchar_t	euc_cs3_min = 0x20001020;
static wchar_t	euc_cs1_max = 0;
static wchar_t	euc_cs2_max = 0;
static wchar_t	euc_cs3_max = 0;

/*
 * for storing the code value of the space character
 */
static int	Is_space_character = 0;
int	Space_character_code = 0x20; /* default code is 0x20 */

/* becomes non-zero when WIDTH or WIDTH_DEFAULT specified */
int	width_flag = 0;

/*
 *  FUNCTION: define_wchar
 *
 *  DESCRIPTION:
 *  Adds a wchar_t to the list of characters which are defined in the
 *  codeset which is being defined.
 */
void
define_wchar(wchar_t wc)
{
	unsigned char	bt = (unsigned char)(1 << (wc % CHAR_BIT));
	int	idx = wc / CHAR_BIT;
	size_t	oldsize;

	if (wc > MAX_PC)
		INTERNAL_ERROR;
	oldsize = cur_pcs_size;
	while (idx >= cur_pcs_size) {
		cur_pcs_size += (INIT_MAX_PC + CHAR_BIT) / CHAR_BIT;
		defined_pcs = REALLOC(unsigned char, defined_pcs,
		    (size_t)cur_pcs_size);
		(void) memset(defined_pcs + oldsize, 0, cur_pcs_size - oldsize);
	}
	if (defined_pcs[idx] & bt) {
		if (warning)
			diag_error(gettext(ERR_PC_COLLISION), wc);
	} else {
		defined_pcs[idx] |= bt;
	}
}

/*
 *  FUNCTION: wchar_defined
 *
 *  DESCRIPTION:
 *  Checks if a wide character has been defined.
 *
 *  RETURNS
 *  TRUE if wide char defined, FALSE otherwise.
 */
int
wchar_defined(wchar_t wc)
{
	unsigned char	bt = (unsigned char)(1 << (wc % CHAR_BIT));
	int	idx = wc / CHAR_BIT;

	if (idx < cur_pcs_size) {
		return (defined_pcs[idx] & bt);
	}
	return (0);
}

/*
 *  FUNCTION: define_all_wchars
 *
 *  DESCRIPTION
 *	When there isn't a charmap, we permit all code points to be
 *	implicitly defined.
 *  RETURNS
 *	None
 */

void
define_all_wchars(void)
{
	wchar_t	wc;

	for (wc = 0; wc < 127; wc++) {
		define_wchar(wc);
	}
}


/*
 * cprint - copies a byte value 'v' to destination '*p', either directly if
 *		'csource' is FALSE, or converting non-printables to compilable
 *		values when 'csource' is TRUE.  The destination pointer is
 *		updated to point to the next free byte.
 */

static void
cprint(char **p, size_t *n, unsigned char v, int csource)
{
	if (!csource ||
	    (v != '\\' && v != '\"' && isascii(v) && isprint(v))) {
		if (*n < 2)
			INTERNAL_ERROR;
		**p = (char)v;
		*(*p + 1) = '\0';
		(*p)++;
		(*n)--;
	} else {
		int	r;
		r = snprintf(*p, *n, "\\x%02x\"\"", v);
		if (r < 0 || r >= *n)
			INTERNAL_ERROR;
		*p += r;
		*n -= r;
	}
}

/*
 * FUNCTION: evalsym
 *
 * DESCRIPTION:
 *	Takes the value of a character symbol <name> from the input source
 *	string and copies it into the destination buffer.  The 'csource' flag
 *	controls whether character values are converted into printable
 *	and compilable source form.
 *
 * RETURNS
 *	updated source pointer value.
 *	updates the dest pointer
 *
 */
static const char *
evalsym(char **dst, size_t *n, const char *src, int csource) {

	symbol_t 		*s;
	char	*id;
	char	*p;

	int i = 0;
	int	j;
	int	len = 0;

	/*
	 * Process '<' symbolname '>'.  First determine true length
	 * of the <symbolname> string
	 */
	for (;;) {
		for (; src[i] && (src[i] != escape_char) &&
			(src[i] != '>'); i++) {
			len++;
		}
		if (src[i] == escape_char) {
			i += 2;
			len++;
		} else if (src[i] == '>') {
			len++;
			i++;
			break;
		} else
			error(4, gettext(ERR_BAD_STR_FMT), src);
	}
	id = MALLOC(char, len + 1);
	p = id;
	for (j = 0; j < i; j++) {
		if (src[j] == escape_char) {
			j++;
		}
		*p++ = src[j];
	}
	*p = '\0';

	s = loc_symbol(id);

	if (s == NULL)
		error(4, gettext(ERR_SYM_UNDEF), id);
	else if (s->sym_type != ST_CHR_SYM) {
		error(4, gettext(ERR_WRONG_SYM_TYPE), id);
	} else {
		int	j;
		for (j = 0; j < s->data.chr->len; j++)
			cprint(dst, n, s->data.chr->str_enc[j], csource);
	}

	free(id);

	src += i;

	return (src);
}

char *
real_copy_string(const char *src, int csource)
{
	char	copybuf[8192];
	char	*s1;
	char 	*endptr;		/* pointer to the number */
	int 	value;
	size_t	buflen = sizeof (copybuf);

	s1 = copybuf;

	while (*src != '\0') {

		while (*src != escape_char && *src != '<' && *src != '\0') {
			cprint(&s1, &buflen, (unsigned char)*src, csource);
			src++;
		}

		if (*src == escape_char) {
			/*
			 * If the character pointed to is the escape_char
			 * see if it is the beginning of a character constant
			 * otherwise it is an escaped character that needs
			 * copied into the string
			 */

			switch (*++src) {

			case 'd':		/* decimal constant - \d999 */
				src++;
				value = (int)strtoul(src, &endptr, 10);

				if (endptr == src || (endptr-src) > 3)
					diag_error(gettext(ERR_ILL_DEC_CONST),
					    src);

				src = endptr;

				cprint(&s1, &buflen, (unsigned char)value,
				    csource);
				break;

			case 'x':		/* hex constant - \xdd */
				src++;

				/* Defend against C format 0xnnn */
				/*  should treat like \x0  'x' 'n' 'n' ... */

				if (src[0] == '0' &&
				    (src[1] == 'x' || src[1] == 'X')) {
					cprint(&s1, &buflen, 0, csource);
					break;
				}

				value = (int)strtoul((char *)src,
				    (char **)&endptr, 16);

				if (endptr == src || (endptr - src) > 2)
					diag_error(gettext(ERR_ILL_HEX_CONST),
					    src);

				src = endptr;

				cprint(&s1, &buflen, (unsigned char)value,
				    csource);
				break;

			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
				/* octal constant - \777 */
				value = (int)strtoul((char *)src,
				    (char **)&endptr, 8);

				if (src == endptr || (endptr-src) > 3)
					diag_error(gettext(ERR_ILL_OCT_CONST),
					    src);

				src = endptr;

				cprint(&s1, &buflen, (unsigned char)value,
				    csource);
				break;

			case '8':
			case '9':		/* \8.. and \9... are illegal */
				diag_error(gettext(ERR_ILL_OCT_CONST), src);
				src++;
				break;

			case 0:
				error(4, gettext(ERR_BAD_STR_FMT), src);
				break;

			case '>':
			case '<':
			case '\\':
			case '\"':
			case ';':
			case ',':
				if (buflen < 3)
					INTERNAL_ERROR;
				*s1++ = '\\';
				*s1++ = *src++;
				*s1 = '\0';
				buflen -= 2;
				break;

			default:
				cprint(&s1, &buflen, (unsigned char)*src,
				    csource);
				src++;
			}			/* end switch(*++src) */
		} else if (*src == '<') {
			src = evalsym(&s1, &buflen, src, csource);
		} else {
			/* *src == 0 */
			if (buflen < 1)
				INTERNAL_ERROR;
			*s1 = '\0';
			break;
		}
	}
	return (STRDUP(copybuf));
}

/*
 *  FUNCTION: sem_set_str_list
 *
 *  DESCRIPTION:
 *  Assign the values of the 'n' strings on the top of the semantic
 *  stack to the array of strings 's' passed as a paramter. This routine
 *  is used to assign the values to items such as 'abday[7]', i.e. the
 *  seven days of the week.  The productions using this sem-action
 *  implement statements like:
 *
 *	abday  "Mon";"Tue";"Wed";"Thu";"Fri";"Sat";"Sun"
 *
 *  This routine performs a malloc() to acquire the memory to build
 *  the array of strings.
 *
 */
void
sem_set_str_lst(char **s, int n)
{
	item_t *it;
	int i;


	for (i = n-1, it = sem_pop(); it != NULL && i >= 0;
	    i--, it = sem_pop()) {
		if (it->type != SK_STR)
			INTERNAL_ERROR;

		if (it->value.str[0] != '\0')
			s[i] = copy_string(it->value.str);
		else
			s[i] = NULL;

		destroy_item(it);
	}

	if (i > 0)
		error(4, gettext(ERR_N_SARGS), n, i);
	else {
		if (it != NULL) {
			while (it != NULL) {
				destroy_item(it);
				it = sem_pop();
			}
			diag_error(gettext(ERR_TOO_MANY_ARGS), n);
		}
	}
}


/*
 *  FUNCTION: sem_set_str_cat
 *
 *  DESCRIPTION:
 *  Concatenate the values of the 'n' strings on the top of the semantic
 *  stack to the string 's' passed as a paramter. This routine
 *  is used to assign the values to items such as 'alt_digits'.
 *  The productions using this sem-action
 *  implement statements like:
 *
 *	abday  "<j4727>";"<j2929><j1676>";"<j2745><j2929><j2716>"
 *
 *  This routine performs a malloc() to acquire the memory to build
 *  the string.
 *
 */
void
sem_set_str_cat(char **s, int n)
{
	item_t *it;
	int i;
	char **arbp = MALLOC(char *, n + 1);
	char **strings;
	size_t  total_string_length = 0;


	for (i = n - 1, it = sem_pop(); it != NULL && i >= 0;
	    i--, it = sem_pop()) {
		if (it->type != SK_STR)
			INTERNAL_ERROR;

		if (it->value.str[0] != '\0')
			arbp[i] = copy_string(it->value.str);
		else
			arbp[i] = (char *)NULL;

		destroy_item(it);
	}
	arbp[n] = (char *)NULL;

	if (i > 0)
		error(4, gettext(ERR_N_SARGS), n, i);
	else {
		if (it != NULL) {
			while (it != NULL) {
				destroy_item(it);
				it = sem_pop();
			}
			diag_error(gettext(ERR_TOO_MANY_ARGS), n);
		}
	}

	/*
	 * now concatenate all the strings together into one string
	 */
	for (strings = arbp; *strings != NULL; strings++) { /* get the length */
		total_string_length += strlen(*strings);
		if ((strings + 1) != NULL)
			total_string_length++;   /* ; */
	}
	total_string_length++;	/* trailing \0 */
	*s = MALLOC(char, total_string_length);
	**s = (char)NULL;
	for (strings = arbp; *strings != NULL; strings++) { /* concat strings */
		(void) strcat(*s, *strings);
		if (*(strings + 1) != (char *)NULL)
			(void) strcat(*s, ";");
	}
	for (strings = arbp; *strings != NULL; strings++) {
		free(*strings);
	}
	free(arbp);
}


/*
 *  FUNCTION: sem_set_str
 *
 *  DESCRIPTION:
 *  Assign a value to a char pointer passed as a parameter from the value
 *  on the top of the stack.  This routine is used to assign string values
 *  to locale items such as 'd_t_fmt'.  The productions which use this
 *  sem-action implement statements like:
 *
 *	d_t_fmt    "%H:%M:%S"
 *
 *  This routine performs a malloc() to acquire the memory to contain
 *  the string.
 *
 */
void
sem_set_str(char **s)
{
	item_t *it;

	it = sem_pop();
	if (it == NULL || it->type != SK_STR)
		INTERNAL_ERROR;

	if (it->value.str[0] != '\0')
		*s = copy_string(it->value.str);
	else
		*s = NULL;

	destroy_item(it);
}


/*
 *	FUNCTION: sem_set_int
 *
 *	DESCRIPTION:
 *	Assign a value to a char pointer passed as a parameter from the value
 *	on the top of the stack. This routine is used to assign values to
 *	integer valued locale items such as 'int_frac_digits'.  The productions
 *	using this sem-action implement statements like:
 *
 *	int_frac_digits	-1
 *
 *	The memory to contain the integer is expected to have been alloc()ed
 *	by the caller.
 */
void
sem_set_int(char *i)
{
	item_t *it;

	it = sem_pop();
	if (it == NULL || it->type != SK_INT)
		INTERNAL_ERROR;

	if (it->value.int_no != -1)	    /* -1 means default */
		*i = it->value.int_no;

	destroy_item(it);
}


/*
 *  FUNCTION: sem_set_diglist
 *
 *  DESCRIPTION:
 *  Creates a string of digits (each less than CHAR_MAX) and sets the argument
 *  'group' to point to this string.
 *
 *  This routine calls malloc() to obtain the memory to contain the digit
 *  list.
 */
void
sem_set_diglist(char **group)
{
	item_t *n_digits;
	item_t *next_digit;
	char   *buf;
	int    i;

	/* pop digit count off stack */
	n_digits = sem_pop();
	if (n_digits->type != SK_INT)
		INTERNAL_ERROR;

	/* allocate string to contain digit list */
	/* return string holds up to six \x99, followed by \xff<nul> */
	/* Space for "\x99" */
	*group = MALLOC(char, (n_digits->value.int_no * 4) + 5);

	/* temp string big enough for all but the last \xff */
	buf   = MALLOC(char, (n_digits->value.int_no * 4) + 1);

	(*group)[0] = '\0';
	buf[0] = '\0';

	for (i = n_digits->value.int_no - 1; i >= 0; i--) {
		int value;

		next_digit = sem_pop();
		if (next_digit->type != SK_INT)
			INTERNAL_ERROR;

		value = next_digit->value.int_no;

		/*
		 * If -1 is present as last member, then use CHAR_MAX instead
		 */
		if (i == n_digits->value.int_no - 1 && value == -1)
			value = CHAR_MAX;

		/*
		 *	Covert grouping digit to a char constant
		 */
		(void) sprintf(buf, "\\x%02x", (unsigned char)value);

		/*
		 *	prepend this to grouping string
		 */
		(void) strcat(buf, *group);
		(void) strcpy(*group, buf);

		destroy_item(next_digit);
	}

	destroy_item(n_digits);
	free(buf);
}


/*
 *  FUNCTION: sem_set_sym_val
 *
 *  DESCRIPTION:
 *  Assigns a value to the symbol matching 'id'.  The type of the symbol
 *  is indicated by the 'type' parameter.  The productions using this
 *  sem-action implement statements like:
 *
 *	<code_set_name>    "ISO8859-1"
 *				or
 *	<mb_cur_max>	2
 *
 *  The function will perform a malloc() to contain the string 'type' is
 *  SK_STR.
 */
void
sem_set_sym_val(char *id, item_type_t type)
{
	item_t	*i;
	symbol_t	*s;

	i = sem_pop();
	if (i == NULL)
		INTERNAL_ERROR;

	s = loc_symbol(id);
	if (s == NULL)
		INTERNAL_ERROR;

	switch (type) {
	case SK_INT:
		s->data.ival = i->value.int_no;
		break;
	case SK_STR:
		s->data.str = copy_string(i->value.str);
		break;
	default:
		INTERNAL_ERROR;
		/* NOTREACHED */
	}

	destroy_item(i);
}


/*
 *  FUNCTION: sem_char_ref
 *
 *  DESCRIPTION:
 *  This function pops a symbol of the symbol stack, creates a semantic
 *  stack item which references the symbol and pushes the item on the
 *  semantic stack.
 */
void
sem_char_ref(void)
{
	symbol_t *s;
	item_t   *it;

	s = sym_pop();
	if (s == NULL)
		INTERNAL_ERROR;

	if (s->sym_type == ST_CHR_SYM)
		it = create_item(SK_CHR, s->data.chr);
	else if (s->sym_type == ST_UNDEF_SYM) {
		it = create_item(SK_UNDEF, s->sym_id);
	} else {
		it = create_item(SK_INT, 0);
		error(4, gettext(ERR_WRONG_SYM_TYPE), s->sym_id);
	}
	(void) sem_push(it);
}


/*
 *  FUNCTION: sem_symbol
 *
 *  DESCRIPTION:
 *  Attempts to locate a symbol in the symbol table - if the symbol is not
 *  found, then it creates a new symbol and pushes it on the symbol stack.
 *  Otherwise, the symbol located in the symbol table is pushed on the
 *  symbol stack.  This routine is used for productions which may define or
 *  redefine a symbol.
 */
void
sem_symbol(char *s)
{
	symbol_t	*sym;

	/* look for symbol in symbol table */
	sym = loc_symbol(s);

	/* if not found, create a symbol */
	if (sym == NULL) {
		sym = create_symbol(s);
		sym->sym_type = ST_CHR_SYM;
		sym->data.chr = MALLOC(chr_sym_t, 1);
		/* temporarily allocates fc_enc */
		sym->data.chr->fc_enc = (uint64_t)*s;
	}

	/* whether new or old symbol, push on symbol stack */
	(void) sym_push(sym);
}


/*
 *  FUNCTION: sem_existing_symbol
 *
 *  DESCRIPTION:
 *  This function locates a symbol in the symbol table, creates a
 *  semantic stack item from the symbol, and pushes the new item on
 *  the semantic stack.  If a symbol cannot be located in the symbol
 *  table, an error is reported and a dummy symbol is pushed on the stack.
 */
void
sem_existing_symbol(char *s)
{
	symbol_t	*sym;

	/* look for symbol in symbol table */
	sym = loc_symbol(s);

	/*
	 * if not found, create a symbol, write diagnostic, and set global
	 * error flag.
	 */
	if (sym == NULL) {
		diag_error(gettext(ERR_SYM_UNDEF), s);

		sym = create_symbol(s);
		sym->sym_type = ST_UNDEF_SYM;
	}

	/* whether new or old symbol, push on symbol stack */
	(void) sym_push(sym);
}


/*
 *  FUNCTION: sem_symbol_def
 *
 *  DESCRIPTION:
 *  This routine is map a codepoint to a character symbol to implement
 *  the
 *	<j0104>		\x81\x51
 *  construct.
 *
 *  The routine expects to find a symbol and a numeric constant on the
 *  stack.  From these two, the routine builds a character structure which
 *  contains the length of the character in bytes, the file code and
 *  process code representations of the character.  The character
 *  structure is then pushed onto the semantic stack, and the
 *  symbolic representation of the character added to the symbol table.
 *
 *  The routine also checks if this is the max process code yet
 *  encountered, and if so resets the value of max_wchar_enc;
 */
void
sem_symbol_def(void)
{
	symbol_t	*s, *t;
	item_t	*it;
	uint64_t	fc;	/* file code for character */
	wchar_t	pc;		/* process code for character */
	int	rc;		/* return value from mbtowc_xxx */
	wchar_t	eucpc;
	wchar_t	pc_from_bc;	/* pc from bc method */
	int	rt_rc;		/* round trip return code */
	char	rt_char[MB_LEN_MAX + 2];	/* round trip character */
	wchar_t	eucpc_from_dense;	/* convert dense back into eucpc */

	s = sym_pop();		/* pop symbol off stack */
	it = sem_pop();		/* pop uint64 to assign off stack */

	/* get file code for character off semantic stack */
	fc = it->value.uint64_no;
	if (code_conv) {
		/* -u has been specified */
		fc = fc_from_fc(fc);
	}

	t = loc_symbol(s->sym_id);
	if (t != NULL)
		if (t->data.chr->fc_enc != fc)
			diag_error(gettext(ERR_DUP_CHR_SYMBOL), s->sym_id);

	sym_free_chr(s);
	/* create symbol */
	s->sym_type = ST_CHR_SYM;
	s->data.chr = MALLOC(chr_sym_t, 1);

	/* save integral file code representation of character */
	s->data.chr->fc_enc = fc;

	/* turn integral file code into character string */
	s->data.chr->len = mbs_from_fc((char *)s->data.chr->str_enc, fc);

	/* default display width is -1 */
	s->data.chr->width = -1;

	if (s->data.chr->len > mb_cur_max)
		error(4, gettext(ERR_CHAR_TOO_LONG), s->sym_id);

	/* get process code for this character */
	rc = INT_METHOD(
	    (int (*)(_LC_charmap_t *, wchar_t *, const char *, size_t))
	    METH_OFFS(CHARMAP_MBTOWC_AT_NATIVE))(&charmap, &pc,
	    (const char *) s->data.chr->str_enc, MB_LEN_MAX);

	if (rc < 0)
		error(4, gettext(ERR_UNSUP_ENC), s->sym_id);

	s->data.chr->wc_enc = pc;

	/* reset max process code in codeset */
	if (pc > max_wchar_enc) {
		max_wchar_enc = pc;
		charmap.cm_eucinfo->dense_end = pc;
	}
	if (fc > max_fc_enc) {
		max_fc_enc = fc;
	}

	/*
	 * Check the round trip.  Can we get back to the character?
	 */

	rt_rc = INT_METHOD(
	    (int (*)(_LC_charmap_t *, char *, wchar_t))
	    METH_OFFS(CHARMAP_WCTOMB_AT_NATIVE))(&charmap,
	    rt_char, pc);

	if (rt_rc < 0)
		error(4, gettext(ERR_METHOD_FAILED),
		    METH_NAME(CHARMAP_WCTOMB_AT_NATIVE),
		    s->sym_id);

	if (strncmp((const char *)s->data.chr->str_enc, rt_char, rt_rc) != 0)
		diag_error(gettext(ERR_NOMATCH_MBTOWC_WCTOMB),
		    s->sym_id);

	/*
	 * if we are doing euc bc then check the pc against the bc methods
	 */
	if (single_layer == FALSE) {
		rc = INT_METHOD(
		    (int (*)(_LC_charmap_t *, wchar_t *,
		    const char *, size_t))
		    METH_OFFS(CHARMAP_MBTOWC))(&charmap, &eucpc,
		    (const char *)s->data.chr->str_enc, MB_LEN_MAX);
		if (rc < 0) {
			error(4, gettext(ERR_METHOD_FAILED),
			    METH_NAME(CHARMAP_MBTOWC),
			    s->data.chr->str_enc);
		} else {
			/*
			 * convert eucpc to dense pc and see if this dense pc
			 * matchs the one from above.
			 */
			pc_from_bc = INT_METHOD(
			    (wchar_t (*)(_LC_charmap_t *, wchar_t))
			    METH_OFFS(CHARMAP_EUCPCTOWC))
			    (&charmap, eucpc);
			if (pc != pc_from_bc) {
				error(4, gettext(ERR_PC_NOMATCH_WC),
				    s->sym_id);
			}
			/*
			 * convert this dense pc back into eucpc and see if it
			 * matches from  above
			 */
			eucpc_from_dense = INT_METHOD(
			    (wchar_t (*)(_LC_charmap_t *, wchar_t))
			    METH_OFFS(CHARMAP_WCTOEUCPC))
			    (&charmap, pc_from_bc);
			if (eucpc != eucpc_from_dense)
				error(4, gettext(ERR_EUCPC_NOMATCH_WC),
				    s->sym_id);
		}
	}

	/* check if this symbol is "<space>" */
	if ((Is_space_character == 0) &&
	    (strcasecmp(s->sym_id, "<space>") == 0)) {
		Is_space_character = 1;
		Space_character_code = (int)fc;
	}

	/* mark character as defined */
	define_wchar(pc);

	destroy_item(it);

	(void) add_symbol(s);
}

void
sem_symbol_def_euc(void)
{
	symbol_t *s, *t;
	item_t   *it;
	uint64_t	fc;	/* file code for character */
	wchar_t  pc;		/* process code for character */
	int	rc;		/* return value from mbtowc_xxx */

	s = sym_pop();		/* pop symbol off stack */
	it = sem_pop();		/* pop uint64 to assign off stack */

	/* get file code for character off semantic stack */
	fc = it->value.uint64_no;
	if (code_conv) {
		/* -u has been specified */
		fc = fc_from_fc(fc);
	}

	t = loc_symbol(s->sym_id);
	if (t != NULL)
		if (t->data.chr->fc_enc != fc)
			diag_error(gettext(ERR_DUP_CHR_SYMBOL), s->sym_id);

	sym_free_chr(s);
	/* create symbol */
	s->sym_type = ST_CHR_SYM;
	s->data.chr = MALLOC(chr_sym_t, 1);

	/* save integral file code representation of character */
	s->data.chr->fc_enc = fc;

	/* turn integral file code into character string */
	s->data.chr->len = mbs_from_fc((char *)s->data.chr->str_enc, fc);

	/* default display width is -1 */
	s->data.chr->width = -1;

	if (s->data.chr->len > mb_cur_max)
		error(4, gettext(ERR_CHAR_TOO_LONG), s->sym_id);

	/* get EUC process code for this character */
	rc = INT_METHOD(
	    (int (*)(_LC_charmap_t *, wchar_t *, const char *, size_t))
	    METH_OFFS(CHARMAP_MBTOWC))(&charmap, &pc,
	    (const char *)s->data.chr->str_enc, MB_LEN_MAX);

	if (rc < 0)
		error(4, gettext(ERR_UNSUP_ENC), s->sym_id);

	s->data.chr->wc_enc = pc;

	/*
	 * find min/max EUC codes
	 */
	switch (pc & WCHAR_CSMASK) {
	case WCHAR_CS1:
		if (pc > euc_cs1_max)
			euc_cs1_max = pc;
		if (pc < euc_cs1_min)
			euc_cs1_min = pc;
		break;
	case WCHAR_CS2:
		if (pc > euc_cs2_max)
			euc_cs2_max = pc;
		if (pc < euc_cs2_min)
			euc_cs2_min = pc;
		break;
	case WCHAR_CS3:
		if (pc > euc_cs3_max)
			euc_cs3_max = pc;
		if (pc < euc_cs3_min)
			euc_cs3_min = pc;
		break;
	}

	destroy_item(it);
	sym_free_all(s);
}


/*
 *  FUNCTION: extract_digit_list
 *
 *  DESCRIPTION:
 *  This function returns the digit list which may be present in a symbol
 *  of the form <j0102> where 0102 is the desired digit list.
 *
 *  RETURNS:
 *  Number of digits in digit list or 0 if no digit list is present.
 */
static int
extract_digit_list(char *s, int *value)
{
	char *endstr;
	int  i;

	/* skip to first digit in list */
	for (i = 0; s[i] != '\0' && !isdigit(s[i]); i++)
		;

	/* digit list present? */
	if (s[i] == '\0')
		return (0);

	/* determine value of digit list */
	*value = (int)strtol(&(s[i]), &endstr, 10);

	/* make sure '>' immediately follows digit list */
	if (*endstr != '>')
		return (0);

	/* return length of digit list */
	return ((int)(endstr - &(s[i])));
}


/*
 *  FUNCTION: build_symbol_fmt
 *
 *  DESCRIPTION:
 *  This function builds a format strings which describes the symbol
 *  passed as an argument.  This format is used to build intermediary
 *  symbols required to fill the gaps in charmap statements like:
 *		<j0104>...<j0106>
 *
 *  RETURNS:
 *  Format string and 'start/end' which when used with sprintf() results
 *  in symbol that looks like sym0->sym_id.
 */
static char *
build_symbol_fmt(symbol_t *sym0, symbol_t *sym1, int *start, int *end)
{
	size_t	fmtlen;
	char	*fmt;
	char *s;
	int  i;
	int  n_dig0;
	int  n_dig1;

	n_dig0 = extract_digit_list(sym0->sym_id, start);
	n_dig1 = extract_digit_list(sym1->sym_id, end);

	/* digit list present and same length in both symbols ? */
	if (n_dig0 != n_dig1 || n_dig0 == 0)
		return (NULL);

	/* the starting symbol is greater than the ending symbol */
	if (*start > *end)
		return (NULL);

	/* 3: '%' '0' num 'd' '>', 10: num (INT_MAX) */
	fmtlen = strlen(sym0->sym_id) - n_dig0 + 4 + 10 + 1;
	fmt = MALLOC(char, fmtlen);
	/* build format from the start symbol */
	for (i = 0, s = sym0->sym_id; !isdigit(s[i]); i++) {
		fmt[i] = s[i];
	}

	/* add to end of format "%0nd>" where n is no. of digits in list" */
	fmt[i++] = '%';
	(void) snprintf(&fmt[i], fmtlen - i, "0%dd>", n_dig0);
	return (fmt);
}


/*
 *  FUNCTION: sem_symbol_range_def
 *
 *  DESCRIPTION:
 *  This routine defines a range of symbol values which are defined via
 *  the
 *		<j0104> ... <j0106>   \x81\x50
 *  construct.
 */
void
sem_symbol_range_def(void)
{
	symbol_t *s, *s0, *s1;	/* symbols pointers */
	item_t	*it;		/* pointer to mb encoding */
	uint64_t	fc;	/* file code for character */
	wchar_t	pc;		/* process code for character */
	char	*fmt;		/* symbol format, e.g. "<%s%04d>" */
	size_t	idlen;
	int	start;		/* starting symbol number */
	int	end;		/* ending symbol number */
	int	rc;
	int	i;
	wchar_t	eucpc;
	wchar_t	pc_from_bc;
	int	rt_rc;		/* round trip return code */
	char	rt_char[MB_LEN_MAX + 2];	/* round trip character */
	wchar_t	eucpc_from_dense;
	char	*tmp_name;

	s1 = sym_pop();		/* symbol at end of symbol range */
	s0 = sym_pop();		/* symbol at start of symbol range */
	it = sem_pop();		/* starting encoding */


	/*
	 * get file code for character off semantic stack
	 */
	fc = it->value.uint64_no;
	if (code_conv) {
		/* -u has been specified */
		fc = fc_from_fc(fc);
	}

	/*
	 * Check if beginning symbol has already been seen
	 */
	s = loc_symbol(s0->sym_id);
	if (s != NULL)
		if (s->data.chr->fc_enc != fc)
			diag_error(gettext(ERR_DUP_CHR_SYMBOL), s0->sym_id);

	/*
	 * Determine symbol format for building intermediary symbols
	 */
	fmt = build_symbol_fmt(s0, s1, &start, &end);

	/*
	 * Check if ending symbol has already been seen
	 */
	s = loc_symbol(s1->sym_id);
	if (s != NULL)
		if (s->data.chr->fc_enc != fc + (uint64_t)(end - start))
			diag_error(gettext(ERR_DUP_CHR_SYMBOL), s1->sym_id);

	/*
	 * invalid symbols in range ?
	 */
	if (fmt == NULL)
		error(4, gettext(ERR_INVALID_SYM_RNG), s0->sym_id, s1->sym_id);

	idlen = strlen(s0->sym_id) + 1;
	tmp_name = MALLOC(char, idlen);
	for (i = start; i <= end; i++) {

		/*
		 * reuse previously allocated symbol
		 */
		if (i == start) {
			s = s0;
			sym_free_chr(s0);
		} else if (i == end) {
			s = s1;
			sym_free_chr(s1);
		} else {
			/* LINTED E_SEC_PRINTF_VAR_FMT */
			(void) snprintf(tmp_name, idlen, fmt, i);

			s = loc_symbol(tmp_name);
			if (s != NULL) {
				if (s->data.chr->fc_enc != fc) {
					diag_error(gettext(ERR_DUP_CHR_SYMBOL),
					    tmp_name);
				}
			} else {
				s = create_symbol(tmp_name);
			}
		}

		/*
		 * flesh out symbol definition
		 */
		s->sym_type = ST_CHR_SYM;
		s->data.chr = MALLOC(chr_sym_t, 1);

		/* save file code */
		s->data.chr->fc_enc = fc;

		/*
		 * turn ordinal file code into character string
		 */
		s->data.chr->len =
		    mbs_from_fc((char *)s->data.chr->str_enc, fc);

		if (s->data.chr->len > mb_cur_max)
			error(4, gettext(ERR_CHAR_TOO_LONG), s->sym_id);

		/* default display width is -1 */
		s->data.chr->width = -1;

		/*
		 * get process code for this character
		 */
		rc = INT_METHOD(
		    (int (*)(_LC_charmap_t *, wchar_t *,
		    const char *, size_t))
		    METH_OFFS(CHARMAP_MBTOWC_AT_NATIVE))(&charmap, &pc,
		    (const char *)s->data.chr->str_enc, MB_LEN_MAX);

		if (rc >= 0) {

			s->data.chr->wc_enc = pc;

			/*
			 * reset max process code in codeset
			 */
			if (pc > max_wchar_enc) {
				max_wchar_enc = pc;
				charmap.cm_eucinfo->dense_end = pc;
			}
			if (fc > max_fc_enc) {
				max_fc_enc = fc;
			}

			/*
			 * if we are doing euc bc then check the pc against
			 * the bc methods
			 */
			if (single_layer == FALSE) {
				rc = INT_METHOD(
				    (int (*)(_LC_charmap_t *, wchar_t *,
				    const char *, size_t))
				    METH_OFFS(CHARMAP_MBTOWC))(
				    &charmap, &eucpc,
				    (const char *)s->data.chr->str_enc,
				    MB_LEN_MAX);
				if (rc < 0) {
					error(4, gettext(ERR_METHOD_FAILED),
					    METH_NAME(CHARMAP_MBTOWC),
					    s->data.chr->str_enc);
				}
				/*
				 * convert eucpc to dense pc and see if
				 * this dense pc matches the one from
				 * above
				 */
				pc_from_bc = INT_METHOD(
				    (wchar_t (*)(_LC_charmap_t *, wchar_t))
				    METH_OFFS(CHARMAP_EUCPCTOWC))
				    (&charmap, eucpc);
				if (pc != pc_from_bc) {
					error(4, gettext(ERR_PC_NOMATCH_WC),
					    s->sym_id);
				}
				/*
				 * convert this dense pc back into eucpc
				 * and see if it matches from  above
				 */
				eucpc_from_dense = INT_METHOD(
				    (wchar_t (*)(_LC_charmap_t *, wchar_t))
				    METH_OFFS(CHARMAP_WCTOEUCPC))
				    (&charmap, pc_from_bc);
				if (eucpc != eucpc_from_dense)
					error(4, gettext(ERR_EUCPC_NOMATCH_WC),
					    s->sym_id);
			}
			/*
			 * mark character as defined
			 */
			define_wchar(pc);

			(void) add_symbol(s);
		} else {
			diag_error(gettext(ERR_ILL_CHAR),
			    s->data.chr->str_enc[0]);
		}

		/*
		 * Check the round trip.  Can we get back to the character?
		 */
		rt_rc = INT_METHOD(
		    (int (*)(_LC_charmap_t *, char *, wchar_t))
		    METH_OFFS(CHARMAP_WCTOMB_AT_NATIVE))
		    (&charmap, rt_char, pc);

		if (rt_rc < 0) {
			error(4, gettext(ERR_METHOD_FAILED),
			    METH_NAME(CHARMAP_WCTOMB_AT_NATIVE),
			    s->sym_id);
		}

		if (strncmp((const char *)s->data.chr->str_enc,
		    rt_char, rt_rc) != 0)
			error(4, gettext(ERR_NOMATCH_MBTOWC_WCTOMB),
			    s->sym_id);

		/* get next file code */
		fc++;
	}
	free(tmp_name);
	free(fmt);
	destroy_item(it);
}

void
sem_symbol_range_def_euc(void)
{
	symbol_t *s, *s0, *s1;	/* symbols pointers */
	item_t   *it;		/* pointer to mb encoding */
	uint64_t	fc;	/* file code for character */
	wchar_t	pc;		/* process code for character */
	char	*fmt;		/* symbol format, e.g. "<%s%04d>" */
	size_t	idlen;
	int	start;		/* starting symbol number */
	int	end;		/* ending symbol number */
	int	rc;
	int	i;

	s1 = sym_pop();		/* symbol at end of symbol range */
	s0 = sym_pop();		/* symbol at start of symbol range */
	it = sem_pop();		/* starting encoding */


	/*
	 * get file code for character off semantic stack
	 */
	fc = it->value.uint64_no;
	if (code_conv) {
		/* -u has been specified */
		fc = fc_from_fc(fc);
	}

	/*
	 * Check if beginning symbol has already been seen
	 */
	s = loc_symbol(s0->sym_id);
	if (s != NULL)
		if (s->data.chr->fc_enc != fc)
			diag_error(gettext(ERR_DUP_CHR_SYMBOL), s0->sym_id);

	/*
	 * Determine symbol format for building intermediary symbols
	 */
	fmt = build_symbol_fmt(s0, s1, &start, &end);

	/*
	 * Check if ending symbol has already been seen
	 */
	s = loc_symbol(s1->sym_id);
	if (s != NULL)
		if (s->data.chr->fc_enc != fc + (uint64_t)(end - start))
			diag_error(gettext(ERR_DUP_CHR_SYMBOL), s1->sym_id);

	/*
	 * invalid symbols in range ?
	 */
	if (fmt == NULL)
		error(4, gettext(ERR_INVALID_SYM_RNG), s0->sym_id, s1->sym_id);

	idlen = strlen(s0->sym_id) + 1;
	for (i = start; i <= end; i++) {
		/*
		 * reuse previously allocated symbol
		 */
		if (i == start) {
			s = s0;
			sym_free_chr(s0);
		} else if (i == end) {
			s = s1;
			sym_free_chr(s1);
		} else {
			char	*tmp_name;

			tmp_name = MALLOC(char, idlen);
			/* LINTED E_SEC_PRINTF_VAR_FMT */
			(void) snprintf(tmp_name, idlen, fmt, i);
			s = loc_symbol(tmp_name);
			if (s != NULL) {
				if (s->data.chr->fc_enc != fc) {
					diag_error(gettext(ERR_DUP_CHR_SYMBOL),
					    tmp_name);
				}
			} else {
				s = create_symbol(tmp_name);
			}
			free(tmp_name);
		}

		/*
		 * flesh out symbol definition
		 */
		s->sym_type = ST_CHR_SYM;
		s->data.chr = MALLOC(chr_sym_t, 1);

		/* save file code */
		s->data.chr->fc_enc = fc;

		/*
		 * turn ordinal file code into character string
		 */
		s->data.chr->len =
		    mbs_from_fc((char *)s->data.chr->str_enc, fc);

		if (s->data.chr->len > mb_cur_max)
			error(4, gettext(ERR_CHAR_TOO_LONG), s->sym_id);

		/* default display width is -1 */
		s->data.chr->width = -1;

		/*
		 * get process code for this character
		 */
		rc = INT_METHOD(
		    (int (*)(_LC_charmap_t *, wchar_t *,
		    const char *, size_t))
		    METH_OFFS(CHARMAP_MBTOWC))(&charmap, &pc,
		    (const char *)s->data.chr->str_enc, MB_LEN_MAX);

		if (rc >= 0)
			s->data.chr->wc_enc = pc;
		else
			diag_error(gettext(ERR_ILL_CHAR),
			    s->data.chr->str_enc[0]);

		/*
		 * find min/max EUC codes
		 */
		switch (pc & WCHAR_CSMASK) {
		case WCHAR_CS1:
			if (pc > euc_cs1_max)
				euc_cs1_max = pc;
			if (pc < euc_cs1_min)
				euc_cs1_min = pc;
			break;
		case WCHAR_CS2:
			if (pc > euc_cs2_max)
				euc_cs2_max = pc;
			if (pc < euc_cs2_min)
				euc_cs2_min = pc;
			break;
		case WCHAR_CS3:
			if (pc > euc_cs3_max)
				euc_cs3_max = pc;
			if (pc < euc_cs3_min)
				euc_cs3_min = pc;
			break;
		}

		/* get next file code */
		fc++;

		if (loc_symbol(s->sym_id) == NULL) {
			/*
			 * this symbol hasn't been added.
			 * can be removed here.
			 */
			sym_free_all(s);
		}
	}
	free(fmt);
	destroy_item(it);
}

/*
 *  FUNCTION: wc_from_fc
 *
 *  DESCRIPTION
 *  Convert a character encoding (as an integer) into a wide char
 */
int
wc_from_fc(uint64_t fc)
{
	int	ret, len;
	wchar_t	pc;
	unsigned char	s[sizeof (uint64_t) + 1];

	len = mbs_from_fc((char *)s, fc);
	if (len >= 2) {
		/*
		 * multibyte char
		 * if first byte doesn't have high bit
		 */
		if (s[0] < 0x80)
			return (-1);
	}

	ret = INT_METHOD(
	    (int (*)(_LC_charmap_t *, wchar_t *, const char *, size_t))
	    METH_OFFS(CHARMAP_MBTOWC_AT_NATIVE))(&charmap, &pc,
	    (const char *)s, (size_t)len);

	if (ret < 0) {
		return (ret);
	} else {
		return (pc);
	}
}

/*
 * fc_from_fc() converts the UCS-4 value specified as 'fc'
 * to the corresponding UTF-8 value.  Then converts the UTF-8
 * value to the user specified encoding using iconv().
 *
 * The following is the conversion table between
 * UCS-4 and UTF-8:
 *
 * UCS-4 (hex format)	UTF-8 (bin format)
 * 00000000-0000007f	0xxxxxxx
 * 00000080-000007ff	110xxxxx 10xxxxxx
 * 00000800-0000ffff	1110xxxx 10xxxxxx 10xxxxxx
 * 00010000-001fffff	11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
 * 00200000-03ffffff	111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
 * 04000000-7fffffff	1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
 */
static uint64_t
fc_from_fc(uint64_t fc)
{
	uint64_t	cfc;
	uint64_t	u4 = fc;
	unsigned char	inbuf[sizeof (uint64_t)];
	unsigned char	outbuf[sizeof (uint64_t)];
	unsigned char	*result;
	char	*in;
	char	*out;
	size_t	inbytes, outbytes, ret, resultlen;
	int	i;

	if (u4 <= 0x7f) {
		inbuf[0] = (unsigned char)u4;
		inbytes = 1;
	} else if (u4 <= 0x07ff) {
		inbuf[0] = (unsigned char)(0xc0 | (u4 >> 6));
		inbuf[1] = (unsigned char)(0x80 | (u4 & 0x3f));
		inbytes = 2;
	} else if (u4 <= 0xffff) {
		inbuf[0] = (unsigned char)(0xe0 | (u4 >> 12));
		inbuf[1] = (unsigned char)(0x80 | ((u4 >> 6) & 0x3f));
		inbuf[2] = (unsigned char)(0x80 | (u4 & 0x3f));
		inbytes = 3;
	} else if (u4 <= 0x1fffff) {
		inbuf[0] = (unsigned char)(0xf0 | (u4 >> 18));
		inbuf[1] = (unsigned char)(0x80 | ((u4 >> 12) & 0x3f));
		inbuf[2] = (unsigned char)(0x80 | ((u4 >> 6) & 0x3f));
		inbuf[3] = (unsigned char)(0x80 | (u4 & 0x3f));
		inbytes = 4;
	} else if (u4 <= 0x3ffffff) {
		inbuf[0] = (unsigned char)(0xf8 | (u4 >> 24));
		inbuf[1] = (unsigned char)(0x80 | ((u4 >> 18) & 0x3f));
		inbuf[2] = (unsigned char)(0x80 | ((u4 >> 12) & 0x3f));
		inbuf[3] = (unsigned char)(0x80 | ((u4 >> 6) & 0x3f));
		inbuf[4] = (unsigned char)(0x80 | (u4 & 0x3f));
		inbytes = 5;
	} else if (u4 <= 0x7fffffff) {
		inbuf[0] = (unsigned char)(0xfc | (u4 >> 30));
		inbuf[1] = (unsigned char)(0x80 | ((u4 >> 24) & 0x3f));
		inbuf[2] = (unsigned char)(0x80 | ((u4 >> 18) & 0x3f));
		inbuf[3] = (unsigned char)(0x80 | ((u4 >> 12) & 0x3f));
		inbuf[4] = (unsigned char)(0x80 | ((u4 >> 6) & 0x3f));
		inbuf[5] = (unsigned char)(0x80 | (u4 & 0x3f));
		inbytes = 6;
	} else {
		error(4, gettext(ERR_NO_SUPPORT_UCS4), u4);
	}
	if (code_conv->cd) {
		in = (char *)inbuf;
		out = (char *)outbuf;
		outbytes = sizeof (outbuf);
		ret = iconv(code_conv->cd, &in, &inbytes, &out, &outbytes);
		if (ret == (size_t)-1 || inbytes != 0) {
			/*
			 * iconv failed, which should not.
			 */
			error(4, gettext(ERR_ICONV_FAIL), u4);
		}
		result = outbuf;
		resultlen = sizeof (outbuf) - outbytes;
	} else {
		result = inbuf;
		resultlen = inbytes;
	}
	cfc = 0;
	for (i = 0; i < resultlen; i++) {
		cfc <<= 8;
		cfc |= result[i];
	}
	return (cfc);
}

/*
 *  FUNCTION: mbs_from_fc
 *
 *  DESCRIPTION
 *  Convert an integral file code to a string.  The length of the character is
 *  returned. The returned string is null-terminated.
 */
int
mbs_from_fc(char *s, uint64_t fc)
{
#define	LAST_INDEX	(sizeof (uint64_t) - 1)
	int	i, last_non_null;
	size_t	len;
	unsigned char	ts[sizeof (uint64_t)];

	last_non_null = LAST_INDEX;
	for (i = LAST_INDEX; i >= 0; i--) {
		ts[i] = (unsigned char)(fc & 0x00ff);
		if (ts[i] != 0) {
			last_non_null = i;
		}
		fc >>= 8;
	}

	len = LAST_INDEX - last_non_null + 1;
	(void) memcpy(s, &ts[last_non_null], len);
	s[len] = '\0';
	return (len);
}


/* check for digits */
void
check_digit_values(void)
{
	symbol_t *s;
	uint64_t fc0, fc1;
	wchar_t pc0, pc1;
	int i;
	const char *digits[] = {
		"<zero>",
		"<one>",
		"<two>",
		"<three>",
		"<four>",
		"<five>",
		"<six>",
		"<seven>",
		"<eight>",
		"<nine>"
	};

	s = loc_symbol("<zero>");
	if (s == NULL)
		INTERNAL_ERROR;
	fc0 = s->data.chr->fc_enc;
	pc0 = s->data.chr->wc_enc;
	for (i = 1; i <= 9; i++) {
		s = loc_symbol((char *)digits[i]);
		if (s == NULL)
			INTERNAL_ERROR;
		fc1 = s->data.chr->fc_enc;
		pc1 = s->data.chr->wc_enc;
		if ((fc0 + 1) != fc1)
			diag_error(gettext(ERR_DIGIT_FC_BAD),
			    digits[i], digits[i - 1]);
		if ((pc0 + 1) != pc1)
			diag_error(gettext(ERR_DIGIT_PC_BAD),
			    digits[i], digits[i - 1]);
		fc0 = fc1;
		pc0 = pc1;
	}
}


void
fill_euc_info(_LC_euc_info_t *euc_info_ptr)
{
	/*
	 * set proper minimum EUC pc values
	 */
	switch (euc_info_ptr->euc_bytelen1) {
	case 0:
		euc_cs1_min = 0x0;
		break;
	case 1:
		euc_cs1_min = 0x30000020;
		break;
	case 2:
		euc_cs1_min = 0x30001020;
		break;
	case 3:
		euc_cs1_min = 0x30081020;
		break;
	default:
		diag_error(gettext(ERR_UNSUPPORTED_LEN),
		    1, euc_info_ptr->euc_bytelen1);
	}
	switch (euc_info_ptr->euc_bytelen2) {
	case 0:
		euc_cs2_min = 0x0;
		break;
	case 1:
		euc_cs2_min = 0x10000020;
		break;
	case 2:
		euc_cs2_min = 0x10001020;
		break;
	case 3:
		euc_cs2_min = 0x10081020;
		break;
	default:
		diag_error(gettext(ERR_UNSUPPORTED_LEN),
		    2, euc_info_ptr->euc_bytelen2);
	}
	switch (euc_info_ptr->euc_bytelen3) {
	case 0:
		euc_cs3_min = 0x0;
		break;
	case 1:
		euc_cs3_min = 0x20000020;
		break;
	case 2:
		euc_cs3_min = 0x20001020;
		break;
	case 3:
		euc_cs3_min = 0x20081020;
		break;
	default:
		diag_error(gettext(ERR_UNSUPPORTED_LEN),
		    3, euc_info_ptr->euc_bytelen3);
	}

	/*
	 * EUC CS2 base value
	 */
	euc_info_ptr->cs2_base = 0x100;		/* assumed */
	if (euc_cs2_max == 0)		/* EUC CS2 not used */
		euc_cs2_min = 0;

	/*
	 * EUC CS3 base value
	 */
	if (euc_cs2_max == 0)		/* EUC CS2 not used */
		euc_info_ptr->cs3_base = euc_info_ptr->cs2_base;
	else				/* EUC CS2 used */
		euc_info_ptr->cs3_base =
		    euc_cs2_max - euc_cs2_min + euc_info_ptr->cs2_base + 1;
	if (euc_cs3_max == 0)		/* EUC CS3 not used */
		euc_cs3_min = 0;

	/*
	 * EUC CS1 base value
	 */
	if ((euc_cs2_max == 0) &&
	    (euc_cs3_max == 0) &&
	    (mb_cur_max == 1))
		euc_info_ptr->cs1_base = 0x00a0; /* single byte codeset */
	else if (euc_cs3_max == 0)		/* EUC CS3 not used */
		euc_info_ptr->cs1_base = euc_info_ptr->cs3_base;
	else				/* EUC CS3 used */
		euc_info_ptr->cs1_base =
		    euc_cs3_max - euc_cs3_min + euc_info_ptr->cs3_base + 1;

	if (euc_cs1_max == 0)		/* EUC CS1 not used */
		euc_cs1_min = 0;

	/*
	 * now compute the adjustments
	 */
	if (euc_cs1_max == 0)
		euc_info_ptr->cs1_adjustment = 0;
	else
		euc_info_ptr->cs1_adjustment =
		    -(euc_cs1_min) + euc_info_ptr->cs1_base;
	if (euc_cs2_max == 0)
		euc_info_ptr->cs2_adjustment = 0;
	else
		euc_info_ptr->cs2_adjustment =
		    -(euc_cs2_min) + euc_info_ptr->cs2_base;
	if (euc_cs3_max == 0)
		euc_info_ptr->cs3_adjustment = 0;
	else
		euc_info_ptr->cs3_adjustment =
		    -(euc_cs3_min) + euc_info_ptr->cs3_base;

#ifdef	EUC_COMPAT_DEBUG
	printf("cs1_min = 0x%x\n", euc_cs1_min);
	printf("cs1_max = 0x%x\n", euc_cs1_max);
	printf("cs2_min = 0x%x\n", euc_cs2_min);
	printf("cs2_max = 0x%x\n", euc_cs2_max);
	printf("cs3_min = 0x%x\n", euc_cs3_min);
	printf("cs3_max = 0x%x\n", euc_cs3_max);
	printf("cs1_base = 0x%x\n", euc_info_ptr->cs1_base);
	printf("cs2_base = 0x%x\n", euc_info_ptr->cs2_base);
	printf("cs3_base = 0x%x\n", euc_info_ptr->cs3_base);
	printf("cs1_adjustment = 0x%x %d\n",
	    euc_info_ptr->cs1_adjustment, euc_info_ptr->cs1_adjustment);
	printf("cs2_adjustment = 0x%x %d\n",
	    euc_info_ptr->cs2_adjustment, euc_info_ptr->cs2_adjustment);
	printf("cs3_adjustment = 0x%x %d\n",
	    euc_info_ptr->cs3_adjustment, euc_info_ptr->cs3_adjustment);
	printf("dense_end = 0x%x %d\n",
	    euc_info_ptr->dense_end, euc_info_ptr->dense_end);
#endif
}

/*
 * FUNCTION: init_width_table
 */
void
init_width_table(void)
{
	if (Charmap_pass == 1) {
		/*
		 * Do nothing in the 1st pass
		 */
		return;
	}

	/*
	 * 2nd pass
	 * This locale defines the width table.
	 */
	width_flag |= F_WIDTH;
	t_cm_width = MALLOC(unsigned char, max_wchar_enc + 1);
	(void) memset(t_cm_width, UNDEF_WIDTH, (size_t)max_wchar_enc + 1);
}

/*
 * FUNCTION: sem_column_width_def
 */
void
sem_column_width_def(void)
{
	symbol_t	*s;
	item_t	*it;
	int	width;		/* display width for character */
	wchar_t	wc;

	s = sym_pop();		/* pop symbol off stack */
	it = sem_pop();		/* pop integer to assign off stack */

	if (Charmap_pass == 1) {
		/* do nothing */
		sym_free_all(s);
		destroy_item(it);
		return;
	}

	/* get display width for character off semantic stack */
	width = it->value.int_no;

	/*
	 * we allocate only 8bit to store the width information
	 * in the locale ojbect.  So, the supported width is
	 * between 0 and 254.  255 is used for UNDEF_WIDTH.
	 */
	if (width > MAX_WIDTH) {
		error(4, gettext(ERR_TOO_LARGE_WIDTH), width);
	}

	if (loc_symbol(s->sym_id) == NULL) {
		error(4, gettext(ERR_SYM_UNDEF), s->sym_id);
	}
	wc = s->data.chr->wc_enc;

	/* sanity check */
	if (wc > max_wchar_enc) {
		INTERNAL_ERROR;
	}
	if (wc == 0 && width != 0) {
		diag_error(gettext(ERR_NULL_IS_NOT_0W));
		width = 0;
	}

	if (t_cm_width[wc] != UNDEF_WIDTH &&
	    t_cm_width[wc] != (unsigned char)width) {
		/* already width has been set for this character */
		diag_error(gettext(ERR_DUP_CHR_SYMBOL), s->sym_id);
	}
	t_cm_width[wc] = (unsigned char)width;
	destroy_item(it);
}


/*
 * FUNCTION: sem_column_width_range_def
 */
void
sem_column_width_range_def(void)
{
	symbol_t	*s0, *s1, *s;
	item_t	*it;
	char	*fmt;
	char	*tmp_name;
	wchar_t	wc;
	size_t	idlen;
	int	i, start, end;
	int	width;

	s1 = sym_pop();
	s0 = sym_pop();
	it = sem_pop();

	if (Charmap_pass == 1) {
		/* do nothing */
		sym_free_all(s0);
		sym_free_all(s1);
		destroy_item(it);
		return;
	}

	/* get display width for character off semantic stack */
	width = it->value.int_no;
	/*
	 * we allocate only 8bit to store the width information
	 * in the locale ojbect.  So, the supported width is
	 * between 0 and 254.  255 is used for UNDEF_WIDTH.
	 */
	if (width > MAX_WIDTH) {
		error(4, gettext(ERR_TOO_LARGE_WIDTH), width);
	}

	if (loc_symbol(s0->sym_id) == NULL) {
		error(4, gettext(ERR_SYM_UNDEF), s0->sym_id);
	}

	if (loc_symbol(s1->sym_id) == NULL) {
		error(4, gettext(ERR_SYM_UNDEF), s1->sym_id);
	}

	fmt = build_symbol_fmt(s0, s1, &start, &end);
	if (fmt == NULL)
		error(4, gettext(ERR_INVALID_SYM_RNG), s0->sym_id, s1->sym_id);

	idlen = strlen(s0->sym_id) + 1;
	tmp_name = MALLOC(char, idlen);
	for (i = start; i <= end; i++) {
		if (i == start) {
			s = s0;
		} else if (i == end) {
			s = s1;
		} else {
			/* LINTED E_SEC_PRINTF_VAR_FMT */
			(void) snprintf(tmp_name, idlen, fmt, i);

			s = loc_symbol(tmp_name);
			if (s == NULL) {
				error(4, gettext(ERR_SYM_UNDEF), tmp_name);
			}
		}
		wc = s->data.chr->wc_enc;
		/* sanity check */
		if (wc > max_wchar_enc) {
			INTERNAL_ERROR;
		}
		if (wc == 0 && width != 0) {
			diag_error(gettext(ERR_NULL_IS_NOT_0W));
			width = 0;
		}

		if (t_cm_width[wc] != UNDEF_WIDTH &&
		    t_cm_width[wc] != (unsigned char)width) {
			diag_error(gettext(ERR_DUP_CHR_SYMBOL), s->sym_id);
		}
		t_cm_width[wc] = (unsigned char)width;
	}
	free(tmp_name);
	free(fmt);
	destroy_item(it);
}

/*
 * FUNCTION: sem_column_width_default_def
 */
void
sem_column_width_default_def(void)
{
	item_t	*it;
	int	width;

	it = sem_pop();

	if (Charmap_pass == 1) {
		/* do nothing */
		destroy_item(it);
		return;
	}

	width = it->value.int_no;
	/*
	 * we allocate only 8bit to store the width information
	 * in the locale ojbect.  So, the supported width is
	 * between 0 and 254. 255 is used for UNDEF_WIDTH.
	 */
	if (width > MAX_WIDTH) {
		error(4, gettext(ERR_TOO_LARGE_WIDTH), width);
	}

	default_column_width = (unsigned char)width;
	destroy_item(it);
	width_flag |= F_WIDTH_DEF;
}

typedef struct {
	int	no_chars;	/* # of characters of this width */
	unsigned char	width;	/* width of this chunk */
	int	entries;	/* # of entries */
	int	cur_idx;	/* current index */
	_LC_width_range_t	*ranges; /* defined <sys/localedef.h> */
} idx_table_t;

typedef struct __t_LC_widthtabs_t {
	unsigned char	width;		/* width of this chunk */
	int	no_chars;	/* # of characters in this chunk */
	wchar_t	min;		/* minimum wchar */
	wchar_t	max;		/* maximum wchar */
	struct __t_LC_widthtabs_t	*next;
} _t_LC_widthtabs_t;

#define	INTERNAL_ISWCTYPE(wc, mk)	(ctype.mask[wc] & (mk))

#ifdef	WDEBUG
static void	dump_cm_width(_LC_charmap_t *);
static void	dump_cm_t_ext(_LC_charmap_t *, _t_LC_widthtabs_t *);
#endif

static int
cmp_idx_table(const void *a, const void *b)
{
	int	ia = ((const idx_table_t *)a)->no_chars;
	int	ib = ((const idx_table_t *)b)->no_chars;

	return (ib - ia);
}

/*
 * FUNCTION: sem_column_width
 */
void
set_column_width(void)
{
	int	curindx, base_max, last_valid;
	wchar_t	wc;
	_t_LC_widthtabs_t	*t_tbl, *p, *q;
	_LC_widthtabs_t	*cm_tbl;
	idx_table_t	*idx_table, *idxt;
	int	count, i, width_max;

	/*
	 * If processing a UTF-8 locale, the max of the base table
	 * is 127, because characters in latin-1 won't map to
	 * a single byte.  Otherwise, the max of the base table
	 * is 255.
	 */
	if (charmap.cm_fc_type == _FC_UTF8 &&
	    charmap.cm_pc_type == _PC_UCS4) {
		/*
		 * base area max is 127
		 */
		/* sanity check */
		if (max_wchar_enc < 127) {
			error(4, gettext(ERR_NOT_ENOUGH_CHARS));
		}
		base_max = 127;
	} else {
		/*
		 * base area max is min(255, max_wchar_enc)
		 */
		base_max = (max_wchar_enc < 255) ? max_wchar_enc : 255;
	}
	/*
	 * if no WIDTH_DEFAULT is specified, the default
	 * width is 1.  See IEEE Std 1003.1-2001 Base Definitions,
	 * Issue 6, at page 120.
	 */
	defwidth = (default_column_width != UNDEF_WIDTH) ?
	    default_column_width : 1;

	charmap.cm_base_max = (unsigned char)base_max;
	charmap.cm_def_width = (unsigned char)defwidth;

	if ((width_flag & F_WIDTH) == 0) {
		/*
		 * Only WIDTH_DEFAULT is specifed and WIDTH is not specified.
		 * In this case, this locale will have neither
		 * base table nor extended table.
		 */
		charmap.cm_tbl_ent = 0;		/* no ext table */
		charmap.cm_tbl = NULL;

		return;
	}


	/*
	 * In th base area, if the printable character whose width is defined
	 * as the one that is different from the default is found,
	 * the base area cannot be flat.
	 */
	for (curindx = 0; curindx <= base_max; curindx++) {
		if (t_cm_width[curindx] != defwidth &&
		    t_cm_width[curindx] != UNDEF_WIDTH &&
		    INTERNAL_ISWCTYPE(curindx, _ISPRINT)) {
			curindx = 0;
			break;
		}
	}

	if (curindx == 0) {
		/*
		 * base area cannot be flat
		 * width table contains the base area
		 */
		charmap.cm_base_max = 0;
	} else {
		/*
		 * base area can be flat
		 */
		if (max_wchar_enc <= 255) {
			/* no width table */
			charmap.cm_tbl_ent = 0;
			charmap.cm_tbl = NULL;
#ifdef	WDEBUG
			dump_cm_width(&charmap);
#endif
			free(t_cm_width);
			return;
		}
		/* width table won't contain the base area */
		curindx = base_max + 1;
	}

	/*
	 * Set the default width for defined printable wchars whose width
	 * has not been defined.
	 */
	for (wc = curindx; wc <= max_wchar_enc; wc++) {
		/*
		 * If the width of wc has not been defined and
		 * the wc is printable, then set the default width.
		 */
		if (t_cm_width[wc] == UNDEF_WIDTH &&
		    INTERNAL_ISWCTYPE(wc, _ISPRINT)) {
			t_cm_width[wc] = defwidth;
		}
	}

	/* Find the first defined printable entry */
	while (curindx <= max_wchar_enc) {
		if (t_cm_width[curindx] != UNDEF_WIDTH &&
		    INTERNAL_ISWCTYPE(curindx, _ISPRINT)) {
			break;
		}
		curindx++;
	}
	if (curindx > max_wchar_enc) {
		/* no defined extended table */
		charmap.cm_tbl_ent = 0;
		charmap.cm_tbl = NULL;
#ifdef	WDEBUG
		dump_cm_width(&charmap);
#endif
		free(t_cm_width);
		return;
	}

	t_tbl = MALLOC(_t_LC_widthtabs_t, 1);
	t_tbl->width = t_cm_width[curindx];
	t_tbl->min = curindx;
	t_tbl->max = curindx;
	p = t_tbl;
	curindx++;
	width_max = p->width;
	while (curindx <= max_wchar_enc) {
		if (t_cm_width[curindx] == p->width) {
			/*
			 * width of this character is the same as
			 * that of the current chunk.  So, this chunk
			 * still growing.
			 */
			p->max = curindx;
			last_valid = curindx;
			curindx++;
			continue;
		}
		/*
		 * width of this character is different from that of
		 * the current chunk.
		 */
		if (t_cm_width[curindx] == UNDEF_WIDTH ||
		    INTERNAL_ISWCTYPE(curindx, _ISPRINT) == 0) {
			/*
			 * This character is either invalid or unprintable.
			 * So, it can be considered to be having the same
			 * width as the current chunk.  The current chunk
			 * may be growing.  If no further valid printable
			 * character whose width is the same as the current
			 * chunk found, this character will be omitted
			 * from the current chunk.
			 */
			p->max = curindx;
			curindx++;
			continue;
		}
		/*
		 * New chunk begins.
		 * Current chunk may need to trim because
		 * the last portion of the current chunk
		 * may be invalid chars.
		 */
		p->max = last_valid;
		p->no_chars = p->max - p->min + 1;

		q = MALLOC(_t_LC_widthtabs_t, 1);
		q->width = t_cm_width[curindx];
		if (q->width > width_max) {
			width_max = q->width;
		}
		q->min = curindx;
		q->max = curindx;
		q->next = NULL;
		p->next = q;
		p = q;
		last_valid = curindx;
		curindx++;
	}
	p->max = last_valid;
	p->no_chars = p->max - p->min + 1;
	free(t_cm_width);

#ifdef	WDEBUG
	dump_cm_t_ext(&charmap, t_tbl);
#endif

	idx_table = MALLOC(idx_table_t, width_max + 1);
	/*
	 * count the number of entries and characters in each width.
	 */
	for (p = t_tbl; p != NULL; p = p->next) {
		idxt = &idx_table[p->width];
		idxt->no_chars += p->no_chars;
		idxt->entries++;
	}

	/*
	 * allocate the range data area for each width.
	 */
	count = 0;
	for (i = 0; i <= width_max; i++) {
		idxt = &idx_table[i];
		if (idxt->entries != 0) {
			idxt->ranges = MALLOC(_LC_width_range_t,
			    idxt->entries);
			idxt->width = (unsigned char)i;
			count++;
		}
	}

	/*
	 * store the range data for each width, and free the tbl entries
	 */
	for (p = t_tbl; p != NULL; p = q) {
		idxt = &idx_table[p->width];
		idxt->ranges[idxt->cur_idx].min = p->min;
		idxt->ranges[idxt->cur_idx].max = p->max;
		idxt->cur_idx++;
		q = p->next;
		free(p);
	}

	/*
	 * sort the idx_table according to the number of characters
	 * in each width.
	 */
	qsort(idx_table, width_max + 1, sizeof (idx_table_t),
	    cmp_idx_table);

	/*
	 * all the valid data should have been sorted and moved to the
	 * top of the array within the range of [0, count].  Link the first
	 * 'count' entries to cm_tbl.
	 */
	cm_tbl = MALLOC(_LC_widthtabs_t, count);
	for (i = 0; i < count; i++) {
		cm_tbl[i].width = idx_table[i].width;
		cm_tbl[i].entries = idx_table[i].entries;
		cm_tbl[i].ranges = idx_table[i].ranges;
	}
	free(idx_table);
	charmap.cm_tbl_ent = count;
	charmap.cm_tbl = cm_tbl;
#ifdef	WDEBUG
	dump_cm_width(&charmap);
#endif
}

#ifdef	WDEBUG
static void
dump_cm_width(_LC_charmap_t *cm)
{
	int	i, j, k;
	int	base_max;
	_LC_widthtabs_t	*cm_tbl;

	(void) fprintf(stderr, "        reserved: %d\n",
	    cm->cm_reserved);
	(void) fprintf(stderr, "        def_width: %d\n",
	    cm->cm_def_width);
	(void) fprintf(stderr, "        base_max: %d\n",
	    cm->cm_base_max);
	(void) fprintf(stderr, "        tbl_ent: %d\n",
	    cm->cm_tbl_ent);
	(void) fprintf(stderr, "        tbl:     0x%p\n",
	    (void *)cm->cm_tbl);

	if (!cm->cm_tbl)
		return;

	(void) fprintf(stderr, "0x%p:   cm_tbl[] = {\n", (void *)cm->cm_tbl);
	cm_tbl = cm->cm_tbl;
	for (i = 0; i < cm->cm_tbl_ent; i++) {
		(void) fprintf(stderr, "{ %d, %d, \n",
		    cm_tbl[i].width, cm_tbl[i].entries);
		for (j = 0; j < cm_tbl[i].entries; j++) {
			char	minstr[MB_LEN_MAX];
			char	maxstr[MB_LEN_MAX];
			int	rcmin, rcmax;

			rcmin = INT_METHOD(
			    (int (*)(_LC_charmap_t *, char *, wchar_t))
			    METH_OFFS(CHARMAP_WCTOMB_AT_NATIVE))(cm,
			    minstr, cm_tbl[i].ranges[j].min);
			rcmax = INT_METHOD(
			    (int (*)(_LC_charmap_t *, char *, wchar_t))
			    METH_OFFS(CHARMAP_WCTOMB_AT_NATIVE))(cm,
			    maxstr, cm_tbl[i].ranges[j].max);

			(void) fprintf(stderr,
			    "   { 0x%08x, 0x%08x }, ",
			    (int)cm_tbl[i].ranges[j].min,
			    (int)cm_tbl[i].ranges[j].max);
			for (k = 0; k < rcmin; k++) {
				(void) fprintf(stderr, "%02x ",
				    (unsigned char)minstr[k]);
			}
			(void) fprintf(stderr, ", ");
			for (k = 0; k < rcmax; k++) {
				(void) fprintf(stderr, "%02x ",
				    (unsigned char)maxstr[k]);
			}
			(void) fprintf(stderr, "\n");
		}
		(void) fprintf(stderr, "},\n");
	}
	(void) fprintf(stderr, "};\n");
}

static void
dump_cm_t_ext(_LC_charmap_t *cm, _t_LC_widthtabs_t *tbl)
{
	int	i, j, k;
	_t_LC_widthtabs_t	*p;
	if (!tbl)
		return;

	(void) fprintf(stderr, "0x%p:   tbl[] = {\n", (void *)tbl);
	p = tbl;
	while (p) {
		char	minstr[MB_LEN_MAX];
		char	maxstr[MB_LEN_MAX];
		int	rcmin, rcmax;
		(void) fprintf(stderr, "{ %d, %d, \n",
		    p->width, p->no_chars);
		rcmin = INT_METHOD(
		    (int (*)(_LC_charmap_t *, char *, wchar_t))
		    METH_OFFS(CHARMAP_WCTOMB_AT_NATIVE))(cm,
		    minstr, p->min);
		rcmax = INT_METHOD(
		    (int (*)(_LC_charmap_t *, char *, wchar_t))
		    METH_OFFS(CHARMAP_WCTOMB_AT_NATIVE))(cm,
		    maxstr, p->max);

		(void) fprintf(stderr,
		    "   0x%08x, 0x%08x, /* ", p->min, p->max);
		for (k = 0; k < rcmin; k++) {
			(void) fprintf(stderr, "%02x ",
			    (unsigned char)minstr[k]);
		}
		(void) fprintf(stderr, ", ");
		for (k = 0; k < rcmax; k++) {
			(void) fprintf(stderr, "%02x ",
			    (unsigned char)maxstr[k]);
		}
		(void) fprintf(stderr, "*/\n");
		(void) fprintf(stderr, "    0x%p},\n", p->next);
		p = p->next;
	}
	(void) fprintf(stderr, "};\n");
}
#endif
