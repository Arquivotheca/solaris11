/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef ___KSH_NAME_H
#define	___KSH_NAME_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Definitions of structures for name-value pairs
 * These structures are used for named variables, functions and aliases
 */

#include	"csi.h"
#include	"sh_config.h"
#include	"flags.h"

/* Nodes can have all kinds of values */
union Namval
{
	char		*cp;
	int		*ip;
	char		c;
	int		i;
	unsigned	u;
	longlong_t	*lp;
	double		*dp;	/* for floating point arithmetic */
	struct Namaray	*aray;	/* for array node */
	union Namval	*up;	/* for indirect node */
	struct Bfunction *fp;	/* builtin-function like $RANDOM */
	struct Ufunction *rp;	/* shell user defined functions */
	int	(*ifp)();	/* integer function pointer */
};

/* each namnod and each array element has one of these */
struct Nodval
{
	unsigned	namflg; 	/* attributes */
	union Namval	namval; 	/* value field */
	char		*namenv;	/* pointer to environment name */
	short 		namsz;		/* size of item */
};

#ifdef MULTIDIM
#   define NDIM 7
#else
#   define NDIM 1
#endif /* MULTIDIM */

/* This is an array template */
struct Namaray
{
	unsigned short	cur[NDIM+1];	/* current element */
	unsigned short	maxi;		/* maximum index of array */
	unsigned short	nelem;		/* number of elements */
	struct Nodval	*val[1];	/* array of value holders */
};


/* this bits are or'd with adot for a[*] and a[@] */
#define	ARRAY_STAR	020000
#define	ARRAY_AT	040000
#define ARRAY_UNDEF	0100000
#define ARRAY_MASK	017777		/* ARRMAX cannot be larger than this */
#define ARRMAX	 4096	/* maximum number of elements in an array */
#define ARRINCR    16	/* number of elements to grow when array bound exceeded
				 Must be a power of 2 */

/* These flags are used as options to array_get() */
#define A_ASSIGN	0
#define A_LOOKUP	1
#define A_DELETE	2

/* This is a template for a storage tree */
struct Amemory
{
	struct Amemory  *nexttree;	/* search trees can be chained */
	short		memsize;	/* number of listheads */
	struct namnod	*memhead[1];	/* listhead pointers   */
};

/* This describes a named node */
struct namnod
{
	struct namnod	*namnxt;	/* pointer to next namnod  */
	char		*namid;		/* pointer to name of item */
	struct Nodval	value;		/* determines value of the item */
};

/* This describes a builtin function node */
struct Bfunction
{
	long	(*f_vp)();		/* value function */
	void	(*f_ap)();		/* assignment function */
};

/* This describes a user defined function node */
struct Ufunction
{
	off_t	hoffset;		/* offset into history file */
	int	*ptree;			/* address of parse tree */
};

#ifndef NULL
#   define NULL	0
#endif

/* types of namenode items */

#define N_DEFAULT 0
#define N_INTGER	I_FLAG	/* integer type */
#define N_AVAIL		B_FLAG	/* node is logically non-existent, blocked */
#define N_CWRIT		C_FLAG	/* make copy of node on assignment */
#define N_ARRAY		F_FLAG	/* node is an array */
#define N_INDIRECT	P_FLAG	/* value is a pointer to a value node */
#define N_ALLOC		V_FLAG	/* don't allocate space for the value */
#define N_FREE		G_FLAG	/* don't free the space when releasing value */
#define N_LTOU		U_FLAG	/* convert to uppercase */
#define N_UTOL		L_FLAG	/* convert to lowercase */
#define N_ZFILL		Z_FLAG	/* right justify and fill with leading zeros */
#define N_RJUST		W_FLAG	/* right justify and blank fill */
#define N_LJUST		S_FLAG	/* left justify and blank fill */
#define N_HOST		M_FLAG	/* convert to host file name in non-unix */
#define N_EXPORT	X_FLAG	/* export bit */
#define N_RDONLY	R_FLAG	/* readonly bit */
#define N_RESTRICT	V_FLAG	/* restricted bit */
#define N_IMPORT	N_FLAG	/* imported from environment */


/* The following are used with INT_FLG */
#define	N_BLTNOD	S_FLAG		/* builtin function flag */
#define N_DOUBLE	M_FLAG		/* for floating point */
#define N_EXPNOTE	L_FLAG		/* for scientific notation */
#define NO_LONG		L_FLAG		/* when integers are not long */
#define N_UNSIGN	U_FLAG		/* for unsigned quantities */
#define N_CPOINTER	W_FLAG		/* for pointer */
#define N_FUNCTION	(N_INTGER|Z_FLAG)/* for function trees */

#define NO_CHANGE	(N_EXPORT|N_IMPORT|N_RDONLY|E_FLAG|T_FLAG|N_FREE)
#define NO_PRINT	(N_LTOU|N_UTOL)
#define NO_ALIAS	(NO_PRINT|N_FLAG)
#define N_BLTIN		(NO_PRINT|N_EXPORT)
#define BLT_SPC		(R_FLAG)	/* errors cause script to terminate */
#define BLT_FSUB	(Z_FLAG)	/* fork for command subsitution */
#define BLT_ENV		(R_FLAG|Z_FLAG|M_FLAG)	/* no separate environment */
#define BLT_DCL		(S_FLAG)	/* declaration command */
#define is_abuiltin(n)	(nam_istype(n,N_BLTIN)==N_BLTIN)
#define is_afunction(n)	(nam_istype(n,N_FUNCTION)==N_FUNCTION)
#define is_asbuiltin(n) (nam_istype(n, BLT_SPC) == BLT_SPC) /* XPG4 */
#define	funtree(n)	((n)->value.namval.rp->ptree)
#define	funptr(n)	((n)->value.namval.ifp)


/* namnod lookup options */

#define N_ADD		1	/* add node if not found */
#define N_NULL		2	/* null value returns NULL */
#define N_NOSCOPE	G_FLAG	/* look only in current scope */

/* NAMNOD MACROS */

/* ... for attributes */

#define namflag(n)	(n)->value.namflg
#define nam_istype(n,f)	(namflag(n) & (f))
#ifdef KSHELL
#   define nam_ontype(n,f)	((n)->value.namflg |= f)
#   define nam_typeset(n,f)	((n)->value.namflg = f)
#   define nam_offtype(n,f)	((n)->value.namflg &= f)
#   ifdef PROTO
	extern char	*nam_fstrval(struct namnod*);
#   else
	extern char	*nam_fstrval();
#   endif /* PROTO */
#else
#   define nam_ontype(n,f)	(nam_newtype (n, namflag(n)|(f)))
#   define nam_typeset(n,f)	(nam_newtype (n, f))
#   define nam_offtype(n,f)	(nam_newtype (n, namflag(n)&(f)))
#endif	/* KSHELL */

/* ... etc */

#define isnull(n)	((n)->value.namval.cp == NULL)  /* strings only */
/* This macro must be used only when isnull(n) is true. */
#define isempty(n)	(*(n)->value.namval.cp == '\0')	/* strings only */

#ifdef cray
#   define	_MARK_BIT_	0x2000000000000000
#else
#   define	_MARK_BIT_	01
#endif /* cray */
#define freeble(nv)	(((int)(nv)) & _MARK_BIT_)
#define mrkfree(nv)	((struct Nodval*)(((int)(nv)) | _MARK_BIT_))
#define unmark(nv)	((struct Nodval*)(((int)(nv)) & ~_MARK_BIT_))

/* ...	for arrays */

#define array_ptr(n)	((n)->value.namval.aray)
#define array_elem(n)	array_ptr(n)->nelem
#ifdef PROTO
    extern void 		array_dotset(struct namnod*,int);
    extern struct Nodval	*array_find(struct namnod*,int);
    extern struct Namaray	*array_grow(struct Namaray*,int);
    extern char 		*array_subscript(struct namnod*,char*);
    extern int			array_next(struct namnod*);
#else
    extern void 		array_dotset();
    extern struct Nodval	*array_find();
    extern struct Namaray	*array_grow();
    extern char 		*array_subscript();
    extern int			array_next();
#endif /* PROTO */

#ifdef NAME_SCOPE
   extern struct namnod *nam_copy();
#endif /* NAME_SCOPE */
#define new_of(type,x)	((type*)malloc((unsigned)sizeof(type)+(x)))
#ifdef PROTO
    extern struct namnod	*nam_alloc(const char*);
    extern void 		nam_free(struct namnod*);
    extern void 		nam_fputval(struct namnod*,char*);
    extern int			nam_hash(const char*);
    extern void 		nam_init(void);
    extern void 		nam_link(struct namnod*,struct Amemory*);
    extern void 		nam_longput(struct namnod*,longlong_t);
    extern void 		nam_newtype(struct namnod*,unsigned,int);
    extern void 		nam_putval(struct namnod*,char*);
    struct argnod;		/* struct not declared yet */
    extern void 		nam_scope(struct argnod*);
    extern struct namnod	*nam_search(const char*,struct Amemory*,int);
    extern struct namnod	*nam_search_wcs(const wchar_t*,struct Amemory*,int);
    extern char 		*nam_strval(struct namnod*);
    extern void 		nam_unscope(void);
#else
    extern struct namnod	*nam_alloc();
    extern void 		nam_free();
    extern void 		nam_fputval();
    extern int			nam_hash();
    extern void 		nam_init();
    extern void 		nam_link();
    extern void 		nam_longput();
    extern void 		nam_newtype();
    extern void 		nam_putval();
    extern void 		nam_scope();
    extern struct namnod	*nam_search();
    extern struct namnod	*nam_search_wcs();
    extern char 		*nam_strval();
    extern void 		nam_unscope();
#endif /* PROTO */

extern const char e_synbad[];
extern const char e_subscript[];
extern const char e_number[];
extern const char e_nullset[];
extern const char e_notset[];
extern const char e_readonly[];
extern const char e_restricted[];
extern const char e_ident[];
extern const char e_intbase[];
extern const char e_format[];
extern const char e_aliname[];
extern int sh_lastbase;

extern void cb_lcall();		/* callback for LC_ALL change */
extern void cb_lcctype();	/* callback for LC_CTYPE change */
extern void cb_lccollate();	/* callback for LC_COLLATE change */
extern void cb_lcmessages();	/* callback for LC_MESSAGES change */
extern void cb_lang();		/* callback for LC_LANG change */

#endif /* !___KSH_NAME_H */
