#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 *   UNASSIGN.C
 *
 *   Programmer:  D. G. Korn
 *
 *        Owner:  D. A. Lambeth
 *
 *         Date:  April 17, 1980
 *
 *
 *   UNASSIGN (NP)
 *        
 *        Nullify the value and the attributes of the namnod
 *        given by NP.
 *
 *   See Also:  nam_putval(III), nam_longput(III), nam_strval(III)
 */

#include	"defs.h"
#include	"builtins.h"
#include	"name.h"


/*
 *   NAM_FREE (NP)
 *
 *       struct namnod *NP;
 * 
 *   Set the value of NP to NULL, and nullify any attributes
 *   that NP may have had.  Free any freeable space occupied
 *   by the value of NP.  If NP denotes an array member, it
 *   will retain its attributes.  Any node that has the
 *   indirect (N_INDIRECT) attribute will retain that attribute.
 */

extern void	free();

void	nam_free(np)
register struct namnod *np;
{
	register union Namval *up = &np->value.namval;
	register struct Nodval *nv = 0;
	int next;
#ifdef NAME_SCOPE
	if (nam_istype (np, N_CWRITE))
	{
		np->value.namflg |= N_AVAIL;
		return;
	}
#endif /* NAME_SCOPE */
	do
	{
		if (nam_istype (np, N_ARRAY))
		{
			if((nv=array_find(np,A_DELETE))==0)
				return;
			up = &nv->namval;
		}
		if (nam_istype (np, N_INDIRECT))
			up = up->up;
		if ((!nam_istype (np, N_FREE)) && (!isnull (np)))
			free(up->cp);
		up->cp = NULL;
		if (nam_istype (np, N_ARRAY))
			next = array_next(np);
		else
		{
			np->value.namflg &= N_INDIRECT;
			np->value.namsz = 0;
			next = 0;
		}
		if(nv)
			free((char*)nv);
	}
	while(next);

	/* It's better to have more generic callback mechanism. */
	if (np == LANGNOD)
		cb_lang();
	else if (np == LCTYPENOD)
		cb_lcctype();
	else if (np == LCCOLLNOD)
		cb_lccollate();
	else if (np == LCMESGNOD)
		cb_lcmessages();
	else if (np == LCALLNOD)
		cb_lcall();
}
