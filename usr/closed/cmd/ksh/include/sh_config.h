#ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */

#ifndef _sh_config_
#define	_sh_config_	1
/*
 * This has been generated from install/config
 * The information is based on the compile time environment.
 * It may be necessary to change values of some parameters for cross
 *  development environments.
 */

#include	<sys/types.h>

#define _sys_acct_	1
#define _dirent_ 1
#define _sys_dirent_	1
#define _fcntl_ 1
#define _sys_fcntl_	1
#define _sys_file_	1
#define _sys_filio_	1
#define _sys_ioctl_	1
#define _sys_mnttab_	1
#define _sgtty_ 1
#define _sys_times_	1
#define _termio_ 1
#define _sys_termio_	1
#define _termios_ 1
#define _sys_termios_	1
#define _wait_ 1
#define _sys_wait_	1
#define _unistd_ 1
#define _sys_unistd_	1
#define _sys_utsname_	1
#define _usr_ucb_	1
#define _bin_grep_	1
#define _usr_bin_lp_	1
#define _usr_bin_vi_	1
#define _bin_newgrp_	1
#define _sys_resource_	1
#define _poll_	1
#define VOID	void
#undef _sys_file_
#define signal  nsignal
/*#define SIG_NORESTART   1*/
extern VOID (*nsignal())();
#define sigrelease(s)	{ \
	sigset_t mask; \
	sigemptyset(&mask); \
	sigaddset(&mask, (s)); \
	sigprocmask(SIG_UNBLOCK, &mask, NULL); \
}
#define sig_begin()	{ \
	sigset_t mask; \
	sigemptyset(&mask); \
	sigprocmask(SIG_SETMASK, &mask, NULL); \
}
#define getpgid(a)	getpgrp()
#define ksh_killpg(a,b)	kill(-(a),b)
#define NFILE	64
#define sh_rand(x) ((x),rand())
#define PIPE_ERR	29
#define PROTO	1
#define SETJMP	setjmp
#define LONGJMP	longjmp
#define SOCKET	1
/*
	-lsocket -lnsl
	*/
#define MULTIGROUPS	0
#define SHELLMAGIC	1
#define PATH_MAX	1024
/* XPG4: TIC_SEC is CLK_TCK - times special builtin */
#define TIC_SEC CLK_TCK
#define LSTAT	1
#define SYSCALL	1
#include 	<sys/time.h>
#define included_sys_time_
#define DEVFD	1
#define MNTTAB	"/etc/mnttab"
/* #define	YELLOWP	1 */
#define ECHO_RAW	1
#define JOBS	1
#define WINSIZE	1
#define _SELECT5_	1

#define ESH	1
#define JOBS	1
#define NEWTEST	1
#define OLDTEST	1
#define SUID_EXEC	1
#define VSH	1
#define	SIGSTRINGS	1
#define	POSIX	1
#endif
