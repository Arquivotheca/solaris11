#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

/*
 *	UNIX shell
 *
 *	S. R. Bourne
 *	AT&T Bell Laboratories
 *
 */

#define TGRACE		60	/* grace period before termination */
				/* The time_warn message contains this number */
extern longlong_t	sh_timeout;
extern const char	e_timeout[];
extern const char	e_timewarn[];
