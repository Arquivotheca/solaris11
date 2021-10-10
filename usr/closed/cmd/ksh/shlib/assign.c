#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 *   ASSIGN.C
 *
 *   Programmer:  D. G. Korn
 *
 *        Owner:  D. A. Lambeth
 *
 *         Date:  April 17, 1980
 *
 *
 *   NAM_PUTVAL (NP, STRING)
 *
 *        Assign STRING to NP.
 *
 *   NAM_FPUTVAL (NP, STRING)
 *
 *        Assign STRING to NP even if readonly.
 *
 *
 *   See Also:  nam_longput(III), nam_free(III), nam_strval(III)
 */

#include	<limits.h>
#include	"sh_config.h"
#include	"defs.h"
#include	"name.h"

extern char	*strcpy();
extern char	*strchr();
extern void	utol(),ltou();
extern void	free();
extern void	nam_rjust();
extern void	sh_fail();


/*
 *   NAM_PUTVAL (NP, STRING)
 *
 *        struct namnod *NP;
 *     
 *        char *STRING;
 *
 *   Assign the string given by STRING to the namnod given by
 *   NP.  STRING is converted according to the namflg field
 *   of NP before assignment.  
 *
 *   If NP is an array, then the element given by the
 *   current index is assigned to.
 *   
 *   Any freeable space associated with the old value of NP
 *   is released.
 *
 *   If the copy on write,N_CWRITE flag is set then the assignment
 *   is made on a copy of the np created on the last shell tree.
 * 
 */

static char forced = 0;

void nam_fputval(np,string)
struct namnod *np;
char *string;
{
	forced++;
	nam_putval(np,string);
	forced = 0;
#ifdef apollo
	if(nam_istype(np,N_EXPORT))
	{
		short namlen, vallen;
		char *vp = nam_strval(np);
		namlen =strlen(np->namid);
		vallen = strlen(vp);
		ev_$set_var(np->namid,&namlen,vp,&vallen);
	}
#endif /* apollo */
}

/* CSI assumption1(ascii) made here. See csi.h. */
void nam_putval(np,string)
register struct namnod *np;
char *string;
{
	register char *sp=string;
	register union Namval *up;
	register char *cp;
	register int size = 0;
	int	ts = 0;
	register int dot;
#ifdef apollo
	/* reserve space for UNIX to host file name translation */
	char pathname[256];
	short pathlen;
#endif	/* apollo */
#ifdef NAME_SCOPE
	if (nam_istype (np,N_CWRITE))
		np = nam_copy(np,1);
#endif	/* NAME_SCOPE */
	if (forced==0 && nam_istype (np, N_RDONLY|N_RESTRICT))
	{
		if(nam_istype (np, N_RDONLY))
			sh_fail(np->namid,e_readonly);
		else
			sh_fail(np->namid,e_restricted);
		/* NOTREACHED */
	}
	if(nam_istype (np, N_ARRAY))
		up = &(array_find(np,A_ASSIGN)->namval);
	else
		up= &np->value.namval;
	if (nam_istype (np, N_INDIRECT))
		up = up->up;
	nam_offtype(np,~N_IMPORT);
	if (nam_istype (np, N_INTGER))
	{
		longlong_t l;
#ifdef FLOAT
		extern double sh_arith();
		if (nam_istype(np, N_DOUBLE))
		{
			if(up->dp==0)
				up->dp = new_of(double,0);
			*(up->dp) = sh_arith(sp);
			return;
		}
#else
		extern longlong_t sh_arith();
#endif /* FLOAT */
		if (nam_istype (np, N_CPOINTER))
		{
			up->cp = sp;
			return;
		}
		l = (sp? (longlong_t)sh_arith(sp) : (sh_lastbase=10,0));
		if(np->value.namsz == 0)
			np->value.namsz = sh_lastbase;
		if (nam_istype (np, N_BLTNOD))
		{
			(*up->fp->f_ap)((long) l);
			return;
		}
		if(up->lp==0)
			up->lp = new_of(longlong_t,0);
		*(up->lp) = l;
		return;
	}
#ifdef apollo
	if (nam_istype (np, N_HOST) && sp)
	{
		/* this routine returns the host file name given the UNIX name */
		/* other non-unix hosts that use file name mapping should change this */
		unix_fio_$get_name(sp,pathname,&pathlen);
		pathname[pathlen] = 0;
		sp = pathname;
	}
#endif	/* apollo */
	if ((nam_istype (np, N_RJUST|N_ZFILL|N_LJUST)) && sp)
	{
		wchar_t	*ws, *we, *wcs_save = NULL;
		int	l;

		wcs_save = ws = mbstowcs_alloc(sp);

		for(; sh_iswblank(*ws); ws++) ;

		we = ws + wcslen(ws);
		while (sh_iswblank(*--we));
		*(we + 1) = L'\0';

        	if ((nam_istype (np, N_ZFILL)) && (nam_istype (np, N_LJUST)))
			for(; *ws == '0'; ws++) ;
		
		if ((size = np->value.namsz) > 0) {
			ts = sh_wcswidth(ws);
			if (ts > size) {
				if (nam_istype(np, N_LJUST)) {
					for (we = ws; *we; we++) ;
					do {
						ts -= sh_wcwidth(*(--we));
						if (ts <= size)
							break;
					} while (we > ws);
					*we = L'\0';
				} else {
					do {
						ts -= sh_wcwidth(*ws++);
						if (ts <= size)
							break;
					} while (*ws);
				}
			}
			l = sh_wcstombs(sp, ws, strlen(sp));
		} else {
			ts = sh_wcswidth(ws);
		}

		xfree((void *)wcs_save);
	}
	if ((!nam_istype (np, N_FREE|N_ALLOC)) && (up->cp != NULL))
		free(up->cp);
	if (nam_istype (np, N_ALLOC))
		cp = up->cp;
	else
	{
        	np->value.namflg &= ~N_FREE;
        	if (sp)
		{
			dot = strlen(sp);
			ts = mbscolumns(sp);
			if(size==0 && nam_istype(np,N_LJUST|N_RJUST|N_ZFILL))
				np->value.namsz = size = ts;
			cp = (char *)malloc((unsigned)(
				size ? (size * MB_LEN_MAX + 1) : (dot + 1)));
			if (cp == NULL)
				sh_fail(np->namid, e_space);
		}
		else
			cp = NULL;
		up->cp = cp;
	}
	if (!sp)
		return;
	if (nam_istype (np, N_LTOU))
		ltou(sp,cp);
	else if (nam_istype (np, N_UTOL))
		utol(sp,cp);
	else
        	strcpy (cp, sp);
	if (nam_istype (np, N_RJUST) && nam_istype (np, N_ZFILL))
		nam_rjust(cp,size,'0');
	else if (nam_istype (np, N_RJUST))
		nam_rjust(cp,size,' ');
	else if (nam_istype (np, N_LJUST))
        {
		int	fills = size - ts;
         	sp = cp += strlen(cp);
		for (; (sp - cp) < fills; *sp++ = ' ');
		*sp = '\0';
         }
	return;
}

/* CSI assumption1(ascii) made here. See csi.h. */
void nam_putval_wcs(np,string)
register struct namnod *np;
wchar_t *string;
{
	char	*mbs;

	if (!(mbs = wcstombs_alloc(string)))
		return;
	nam_putval(np, mbs);
	xfree((void *)mbs);
	return;
}
