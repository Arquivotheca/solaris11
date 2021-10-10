/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_ICONV_ICONV_INT_H
#define	_ICONV_ICONV_INT_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <libintl.h>
#include <dirent.h>
#include <string.h>
#include <locale.h>
#include <iconv.h>
#include <langinfo.h>
#include <sys/localedef.h>
#include <stdarg.h>
#include <wchar.h>
#include <limits.h>
#include <regex.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stropts.h>
#include <termios.h>
#include <fcntl.h>
#include "usl_iconv.h"

#ifndef _LP64
#define	ICONV_PATH	"/usr/lib/iconv/"    /* default database */
#else
#define	ICONV_PATH	"/usr/lib/iconv/sparcv9/"    /* default database */
#endif
#define	GENTBL_DB	"geniconvtbl/binarytables/"
#define	GENTBL_PATH	ICONV_PATH GENTBL_DB
#define	ICONV_ALIAS_PATH	"/usr/lib/iconv/alias"
#define	FILE_DATABASE	"iconv_data"	    /* default database */

#define	F_NO_INVALID_OUTPUT	0x01
#define	F_SUPPRESS_ERR_MSG	0x02

#define	HASH_TBL_SIZE_CHARMAP	60611
#define	HASH_TBL_SIZE_ICONVLIST	139

#define	STRDUP(s)	safe_strdup(s)

#define	MALLOC(t, n)	safe_malloc(sizeof (t) * (n))

#define	REALLOC(t, p, n)	safe_realloc(p, sizeof (t) * (n))

#define	INTERNAL_ERROR	\
	error("Internal error. [file %s - line %d].\n", __FILE__, __LINE__)

#define	MAX_BYTES	(sizeof (uint64_t))

struct conv_info {
	int	(*conv_init)(struct conv_info *);
	int	(*conv_main)(struct conv_info *);
	int	(*conv_fini)(struct conv_info *);
	FILE	*fp;
	union {
		iconv_t	cd;
		struct kbd_tab	*t;
	} fd;
	const char	*from;
	const char	*to;
	const char	*cmdname;
	int	flags;
};

typedef enum {
	FROMMAP,
	TOMAP
} map_t;

typedef struct {
	int	no;		/* the number of IDs */
	char	**ids;		/* IDs */
	int	cached;		/* corresponding value cached or not */
	uint64_t	val;	/* cached corresponding value */
} idlist_t;

#define	S_NONE		0
#define	S_TERMINAL	1
#define	S_NONTERMINAL	2

typedef struct _from_tbl_t {
	int	status;		/* S_NONE, S_TERMINAL, S_NONTERMINAL */
	union {
		idlist_t	*id;
		struct _from_tbl_t	**tp;
	} data;
} from_tbl_t;

typedef struct _symbol_t {
	struct _symbol_t	*next;
	char	*id;
	uint64_t	val;
} symbol_t;

typedef struct {
	int	size;
	symbol_t	**symbols;
} symtab_t;

typedef struct _conv_list {
	char	*name;
	char	*vnames;
	size_t	clen;
	size_t	alen;
	struct _conv_list	*next;
} conv_list_t;

/* err.c */
extern void	error_invalid(int);
extern void	error(const char *, ...);
extern char	*safe_strdup(const char *);
extern void	*safe_malloc(size_t);
extern void	*safe_realloc(void *, size_t);
extern void	add_symbol_def(char *, uint64_t);
extern void	add_symbol_range_def(char *, char *, uint64_t);
extern void	yyerror(const char *);

/* symtab.c */
extern void	add_symbol(symbol_t *, symtab_t *);
extern symbol_t	*loc_symbol(char *, symtab_t *);
extern symbol_t	*create_symbol(char *, uint64_t);

/* iconv.c */
extern int	search_dbase(char *, char *, char *, char *,
    char *, const char *, const char *);

/* use_charmap.c */
extern int	use_charmap_init(struct conv_info *);
extern int	use_charmap(struct conv_info *);
extern int	use_charmap_fini(struct conv_info *);
extern void	set_mbcurmax(int);
extern map_t	curr_map;
extern char	*yyfilenm;

/* list_conv.c */
extern int	list_all_conversion(void);

/* gettab.c */
extern struct kbd_tab *gettab(char *, char *, char *, char *, int);

/* process.c */
extern int	process(struct kbd_tab *, int, int);

/* scan.c */
extern void	initlex();
extern FILE	*infp;
extern int	skip_to_EOL;
extern int	yycharno;
extern int	yylineno;
extern int	maxbytes;

/* charmap.y */
extern void	inityacc();
extern int	yyparse(void);

#endif	/* _ICONV_ICONV_INT_H */
