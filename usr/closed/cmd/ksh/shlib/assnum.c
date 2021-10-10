#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Copyright (c) 1996, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*
 *   ASSNUM.C
 *
 *   Programmer:  D. G. Korn
 *
 *        Owner:  D. A. Lambeth
 *
 *         Date:  April 17, 1980
 *
 *
 *   NAM_LONGPUT (NP, NUM)
 *
 *        Assign the long integer NUM to NP.  NP should have
 *        the L_FLAG and N_INTGER attributes.
 *
 *
 *   See Also:  nam_putval(III), nam_free(III), nam_strval(III), ltos(III)
 */

#include	"name.h"

#ifdef FLOAT
#   define ltos 	etos
#endif /* FLOAT */
extern char *ltos();
extern char *lltos();

/*
 *   ASSLONG (NP, NUM)
 *
 *        struct namnod *NP;
 *
 *        int NUM;
 *
 *   Assign the value NUM to the namnod given by NP.  All 
 *   appropriate conversions are made.
 */

void nam_longput(np,num)
register struct namnod *np;
#ifdef FLOAT
   double num;
#else
   longlong_t num;
#endif /* FLOAT */
{
	register union Namval *up = &np->value.namval;
	if (nam_istype (np, N_INTGER))
	{
		if (nam_istype (np, N_ARRAY))
			up = &(array_find(np,A_ASSIGN)->namval);
#ifdef NAME_SCOPE
		if (nam_istype (np, N_CWRITE))
			np = nam_copy(np,1);
#endif	/* NAME_SCOPE */
        	if (nam_istype (np, N_INDIRECT))
			up = up->up;
		nam_offtype(np,~N_IMPORT);
        	if (nam_istype (np, N_BLTNOD))
			(*up->fp->f_ap)((long) num);
#ifdef FLOAT
		else if (nam_istype (np, N_DOUBLE))
		{
			if(up->dp==0)
				up->dp = new_of(double,0);
			*(up->dp) = num;
		}
#endif /* FLOAT */
		else
		{
			if(up->lp==0)
				up->lp = new_of(longlong_t,0);
			*(up->lp) = num;
			if(np->value.namsz == 0)
				np->value.namsz = sh_lastbase;
		}
	}
	else
		nam_putval(np,lltos(num,10));
}
