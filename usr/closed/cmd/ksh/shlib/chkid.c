#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 *   NAM_HASH (NAME)
 *
 *        char *NAME;
 *
 *	Return a hash value of the string given by name.
 *      Trial and error has shown this hash function to perform well
 *
 */

#include	"sh_config.h"

int nam_hash(name)
register const char *name;
{
	register int h = *name;
	register int c;
	while(c= *++name)
	{
		if((h = (h>>2) ^ (h<<3) ^ c) < 0)
			h = ~h;
	}
	return (h);
}

