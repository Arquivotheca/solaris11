#include <stdio.h>
void
tpt_regerror(s)
char *s;
{
#ifdef ERRAVAIL
	error("tpt_regexp: %s", s);
#else
	fprintf(stderr, "tpt_regexp(3): %s", s);
	exit(1);
#endif
	/* NOTREACHED */
}
