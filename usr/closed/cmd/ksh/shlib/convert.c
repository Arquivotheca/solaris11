#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 *   CONVERT.C
 *
 *
 *   LTOU (STR1, STR2)
 *        Copy STR1 to STR2, changing lower case to upper case.
 *
 *   UTOL (STR1, STR2)
 *        Copy STR1 to STR2, changing upper case to lower case.
 */

#include	"csi.h"
#ifdef KSHELL
#include	"shtype.h"
#else
#include	<ctype.h>
#endif	/* KSHELL */

/* 
 *   LTOU (STR1, STR2)
 *        char *STR1;
 *        char *STR2;
 *
 *   Copy STR1 to STR2, converting uppercase alphabetics to
 *   lowercase.  STR2 should be big enough to hold STR1.
 *
 *   STR1 and STR2 may point to the same place.
 *
 */

void ltou(str1,str2)
register char *str1,*str2;
{
	wchar_t	*wcs, *wcs_save;

	if ((wcs_save = wcs = mbstowcs_alloc(str1)) == NULL)
		return;
	for(; *wcs; wcs++) {
		if (sh_iswlower(*wcs))
			str2 += sh_wctomb(str2, sh_towupper(*wcs));
		else
			str2 += sh_wctomb(str2, *wcs);
	}
	*str2 = 0;
	xfree((void *)wcs_save);
}


/*
 *   UTOL (STR1, STR2)
 *        char *STR1;
 *        char *STR2;
 *
 *   Copy STR1 to STR2, converting lowercase alphabetics to
 *   uppercase.  STR2 should be big enough to hold STR1.
 *
 *   STR1 and STR2 may point to the same place.
 *
 */

void utol(str1,str2)
register char *str1,*str2;
{
	wchar_t	*wcs, *wcs_save;

	if ((wcs_save = wcs = mbstowcs_alloc(str1)) == NULL)
		return;
	for(; *wcs; wcs++) {
		if (sh_iswupper(*wcs))
			str2 += sh_wctomb(str2, sh_towlower(*wcs));
		else
			str2 += sh_wctomb(str2, *wcs);
	}
	*str2 = 0;
	xfree((void *)wcs_save);
}
