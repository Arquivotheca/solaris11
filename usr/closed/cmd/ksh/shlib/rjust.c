#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 *   NAM_RJUST.C
 *
 *   Programmer:  D. G. Korn
 *
 *        Owner:  D. A. Lambeth
 *
 *         Date:  April 17, 1980
 *
 *
 *
 *   NAM_RJUST (STR, SIZE, FILL)
 *
 *      Right-justify STR so that it contains no more than 
 *      SIZE non-blank characters.  If necessary, pad with
 *      the character FILL.
 *
 *
 *
 *   See Also:  
 */

#include	"csi.h"
#include	<limits.h>
#ifdef KSHELL
#include	"shtype.h"
#else
#include	<ctype.h>
#endif	/* KSHELL */

/*
 *   NAM_RJUST (STR, SIZE, FILL)
 *
 *        char *STR;
 *
 *        int SIZE;
 *
 *        char FILL;
 *
 *   Right-justify STR so that it contains no more than
 *   SIZE characters.  If STR contains fewer than SIZE
 *   characters, left-pad with FILL.  Trailing blanks
 *   in STR will be ignored.
 *
 *   If the leftmost digit in STR is not a digit, FILL
 *   will default to a blank.
 */

void	nam_rjust(str,size,fill)
char *str,fill;
int size;
{
	register int n, i, l;
	register wchar_t *cp,*sp;

	wchar_t	*wcs, *wcs_save;

	wcs_save = wcs = mbstowcs_alloc(str);
	n = wcslen(wcs);

	/* ignore trailing blanks */

	for (cp = wcs + n; n && (*--cp == ' '); n--) ;
	if (n != 0) {
		wchar_t	savec;
		savec = *(cp + 1);
		*(cp + 1) = L'\0';
		n = sh_wcswidth(wcs);
		*(cp + 1) = savec;
	}

	if (n == size) {
		/* Original (non-CSI) code just ignores and doesn't
		   chop off tailing blanks. CSI code do so here. */
	} else if (n < size) {
		if (!sh_iswdigit(*wcs))
			fill = ' ';
		for (i = 0; i < (size - n); i++)
			str[i] = fill;
		*(cp + 1) = L'\0';
		l = sh_wcstombs(&str[i], wcs, (size * MB_LEN_MAX) - i);
		str[i + l] = '\0';
	} else {
		do {
			if ((n -= sh_wcwidth(*wcs++)) <= size)
				break;
		} while (*wcs);
		if (n < size) {
			if (!sh_iswdigit(*wcs))
				fill = ' ';
			for (i = 0; i < (size - n); i++)
				*str++ = fill;
		}
		l = sh_wcstombs(str, wcs, strlen(str));
		str[l] = '\0';
	}

	xfree((void *)wcs_save);
	return;
}
