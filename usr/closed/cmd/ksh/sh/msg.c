/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"	/* From AT&T Toolchest */


/*
 *	UNIX shell
 *	S. R. Bourne
 *	Rewritten by David Korn
 *
 *	AT&T Bell Laboratories
 *
 */


#include	<errno.h>
#include	"defs.h"
#include	"sym.h"
#include	"builtins.h"
#include	"test.h"
#include	"timeout.h"
#include	"history.h"

MSG e_version = "\n@(#)Version M-11/16/88i\0\n";

extern struct Bfunction sh_randnum;
extern struct Bfunction sh_seconds;
extern struct Bfunction sh_errno;
extern struct Bfunction line_numbers;

/* error messages */

/* TRANSLATION_NOTE
 * To be printed if no input has been done in TMOUT seconds.
 * Please keep 3 control characters ("\r\n\007") as they are. */
MSG	e_timewarn	= "\r\n\007shell will timeout in 60 seconds due to inactivity";

/* TRANSLATION_NOTE
 * To be printed after warning with the message above. */
MSG	e_timeout	= "timed out waiting for input";

/* TRANSLATION_NOTE
 * To be printed before prompting to tell mail arrived.
 * "$_" is shell variable which will be expanded
 * to MAIL (e.g. /var/mail/foo) */
MSG	e_mailmsg	= "you have mail in $_";

/* TRANSLATION_NOTE
 * To be printed when "print -p" or "read -p" is attempted but
 * spawned process (with "|&") is not found. */
MSG	e_query		= "no query process";

/* TRANSLATION_NOTE
 * To be printed when "fc" or "print -s" is attempted but either of:
 *	- the shell is invoked by root and neither HISTFILE nor /.sh_history
 *	  has been successfully opened.
 *	- failed in buffer allocation for reading history file */
MSG	e_history	= "no history file";

/* TRANSLATION_NOTE
 * To be printed when given option character is not supported by the
 * builtin command. (e.g. "print -z") */
MSG	e_option	= "bad option(s)";

/* TRANSLATION_NOTE
 * To be printed if enough memory is not available. */
MSG	e_space		= "no space";

/* TRANSLATION_NOTE
 * To be printed if a builtin command or a test operator expects argument
 * but not specified.
 * (e.g. "." or "[ 1 -a ]" */
MSG	e_argexp	= "argument expected";

/* TRANSLATION_NOTE
 * To be printed if a test expression has no closing square bracket.
 * (e.g. "[") */
MSG	e_bracket	= "] missing";

/* TRANSLATION_NOTE
 * To be printed if a number in arithmetic expression or argument of
 * a builtin command is invalid.
 * (e.g. "echo $((3a))" or "shift 100") */
MSG	e_number	= "bad number";

/* TRANSLATION_NOTE
 * To be printed if ${parameter:?[message]} is expanded when $parameter
 * is null or unset.
 * (e.g. "foo=; ${foo:?}" */
MSG	e_nullset	= "parameter null or not set";

/* TRANSLATION_NOTE
 * To be printed when unset parameter is attemted to expand.
 * (e.g. "unset foo; set -u foo; echo ${foo}") */
MSG	e_notset	= "parameter not set";

/* TRANSLATION_NOTE
 * To be printed when failed in substitucion.
 * (e.g. "cd foo bar", "fc -e - foo=bar", or "echo ${param/foo/bar}") */
MSG	e_subst		= "bad substitution";

/* TRANSLATION_NOTE
 * To be printed when failed in creating a file (incuding temporary file)
 * (e.g. "echo foo > /abc") */
MSG	e_create	= "cannot create";

/* TRANSLATION_NOTE
 * To be printed when restricted action is attempted. (rksh only)
 * (e.g. "/usr/bin/ksh" or "function export^J{^Jecho foo^J}") */
MSG	e_restricted	= "restricted";

/* TRANSLATION_NOTE
 * To be printed if fork() failed with EAGAIN. */
MSG	e_fork		= "cannot fork: too many processes";

/* TRANSLATION_NOTE
 * To be printed if "|&" is attempted when another spawned process
 * is still running.
 * (e.g. "cat > /tmp/foo |&" then "cat > /tmp/bar |&") */
MSG	e_pexists	= "process already exists";

/* TRANSLATION_NOTE
 * To be printed if "-C (noclobber)" option is on and stdout redirection
 * is attempted.
 * (e.g. "set -C; echo foo > /tmp/foo; echo bar > /tmp/foo") */
MSG	e_fexists	= "file already exists";

/* TRANSLATION_NOTE
 * To be printed if fork() failed with ENOMEM. */
MSG	e_swap		= "cannot fork: no swap space";

/* TRANSLATION_NOTE
 * To be printed if pipe() failed. */
MSG	e_pipe		= "cannot make pipe";

/* TRANSLATION_NOTE
 * To be printed if open() failed.
 * (e.g. "cat < nosuchfile") */
MSG	e_open		= "cannot open";

/* TRANSLATION_NOTE
 * To be printed if EOF is input when "ignoreeof" has been set.
 * (e.g. "set -o ignoreeof" then input EOF) */
MSG	e_logout	= "Use 'exit' to terminate this shell";

/* TRANSLATION_NOTE
 * To be printed if exec() failed with E2BIG. */
MSG	e_arglist	= "arg list too long";

/* TRANSLATION_NOTE
 * To be printed if exec() failed with ETXTBSY. */
MSG	e_txtbsy	= "text busy";

/* TRANSLATION_NOTE
 * To be printed if exec() failed with ENOMEM. */
MSG	e_toobig	= "too big";

/* TRANSLATION_NOTE
 * To be printed if both exec() and access(F_OK) failed
 * (e.g. "touch /tmp/foo; exec /tmp/foo") */
MSG	e_exec		= "cannot execute";

/* TRANSLATION_NOTE
 * To be printed if "pwd" failed to get current directory. */
MSG	e_pwd		= "cannot access parent directories";

/* TRANSLATION_NOTE
 * To be printed if failed in finding either of below.
 * (This message is not only for file.)
 *	- shell script for "."(dot) command (e.g. ". nosuchfile")
 *	- directory for "cd" (e.g. "cd nosuchdir")
 *	- command for "exec" (e.g. "exec nosuchcmd")
 *	- editor for "fc" (e.g. "fc -e nosucheditor")
 *	- command for "command" (e.g. "command nosuchcmd")
 *	- command for "whence" (e.g. "whence nosuchcmd") */
MSG	e_found		= " not found";

/* TRANSLATION_NOTE
 * To be printed when ran out of file descriptors. */
MSG	e_flimit	= "too many open files";

/* TRANSLATION_NOTE
 * To be printed when specified limit for "ulimit" is out of
 * range allowed by setrlimit().
 * (e.g. "ulimit -n 10000") */
MSG	e_ulimit	= "exceeds allowable limit";

/* TRANSLATION_NOTE
 * To be printed when specified subscript (index) for array is
 * out of range.
 * (e.g. "echo ${foo[10000]}") */
MSG	e_subscript	= "subscript out of range";

/* TRANSLATION_NOTE
 * To be printed when too many arguments specified.
 * (e.g. "cd a b c d e") */
MSG	e_nargs		= "bad argument count";
#ifdef ELIBACC
    /* shared library error messages */
/* TRANSLATION_NOTE
 * To be printed when exec() failed with ELIBACC. */
    MSG	e_libacc 	= "can't access a needed shared library";

/* TRANSLATION_NOTE
 * To be printed when exec() failed with ELIBBAD. */
    MSG	e_libbad	= "accessing a corrupted shared library";

/* TRANSLATION_NOTE
 * To be printed when exec() failed with ELIBSCN. */
    MSG	e_libscn	= ".lib section in a.out corrupted";

/* TRANSLATION_NOTE
 * To be printed when exec() failed with ELIBMAX. */
    MSG	e_libmax	= "attempting to link in too many libs";
#endif	/* ELIBACC */
#ifdef EMULTIHOP
/* TRANSLATION_NOTE
 * To be printed when chdir() failed with EMULTIHOP. */
    MSG   e_multihop	= "multihop attempted";
#endif /* EMULTIHOP */
#ifdef ENAMETOOLONG
/* TRANSLATION_NOTE
 * To be printed when exec() or chdir() failed with ENAMETOOLONG. */
    MSG   e_longname	= "name too long";
#endif /* ENAMETOOLONG */
#ifdef ENOLINK
/* TRANSLATION_NOTE
 * To be printed when chdir() failed with ENAMETOOLONG. */
    MSG	e_link		= "remote link inactive";
#endif /* ENOLINK */
/* TRANSLATION_NOTE
 * To be printed when chdir() failed with ELOOP. */
MSG	e_loop		= "too many symbolic links";
/* TRANSLATION_NOTE
 * To be printed when failed in cd or action against process.
 * (e.g. "cd directorywithnopermission" or "kill -HUP 1" by non-root) */
MSG	e_access	= "permission denied";

/* TRANSLATION_NOTE
 * To be printed when failed in cd to bad directory.
 * (e.g. "unset HOME; cd") */
MSG	e_direct	= "bad directory";

/* TRANSLATION_NOTE
 * To be printed when failed in cd to non-directory.
 * (e.g. "cd /etc/motd") */
MSG	e_notdir	= "not a directory";

/* TRANSLATION_NOTE
 * To be printed when specified file descriptor number is wrong.
 * (e.g. "echo foo 2>&99") */
MSG	e_file		= "bad file unit number";
MSG	e_trap		= "bad trap";

/* TRANSLATION_NOTE
 * To be printed when modification of readonly parameter is attempted .
 * (e.g. "readonly foo; foo=bar") */
MSG	e_readonly	= "is read only";

/* TRANSLATION_NOTE
 * To be printed when specified parameter name contains character
 * not allowed to be in identifider.
 * (e.g. "echo {<character_other_than_ascii_alnum_or_underscore>}") */
MSG	e_ident		= "is not an identifier";

/* TRANSLATION_NOTE
 * To be printed when specified parameter name contains character
 * not allowed to be in alias name.
 * (e.g. "alias =") */
MSG	e_aliname	= "invalid alias name";

/* TRANSLATION_NOTE
 * To be printed when failed to recognize test operator.
 * (e.g. "[ - foo ]") */
MSG	e_testop	= "unknown test operator";

/* TRANSLATION_NOTE
 * To be printed when specified alias is not found.
 * (e.g. "alias nosuchalias") */
MSG	e_alias		= " alias not found";

/* TRANSLATION_NOTE
 * To be printed when unknown function is used in arithmetic expression.
 * (e.g. "echo $((nosuchfunc(2)))") */
MSG	e_function	= "unknown function";

/* TRANSLATION_NOTE
 * To be printed when argument for "umask" is not recognizable.
 * (e.g. "umask abc") */
MSG	e_format	= "bad format";

/* TRANSLATION_NOTE
 * To be printed if failed in sig2str(). */
MSG	e_sigtrans  = "Signal translation failure";	/* XPG4 */

/* TRANSLATION_NOTE
 * To be printed if signal number specified for "kill -l" is out of range.
 * (e.g. "kill -l 1000") */
MSG	e_badarg    = "Bad argument";	/* XPG4 */
MSG	e_on		= "on";
MSG	e_off		= "off";

/* TRANSLATION_NOTE
 * To be printed to show argument of "type" is shell keyword.
 * (e.g. "type if") */
MSG	is_reserved	= " is a reserved shell keyword";

/* TRANSLATION_NOTE
 * To be printed to show argument of "type" is builtin but
 * isn't special builtin.
 * (e.g. "type fc") */
MSG	is_builtin	= " is a shell builtin";

/* TRANSLATION_NOTE
 * To be printed to show argument of "type" is special builtin.
 * (e.g. "type export") */
MSG	is_sbuiltin = " is a special builtin";	/* XPG4 */

/* TRANSLATION_NOTE
 * To be printed to show argument of "type" is alias and what
 * the alias is for.
 * (e.g. "type suspend") */
MSG	is_alias	= " is an alias for ";

/* TRANSLATION_NOTE
 * To be printed to show argument of "type" is function.
 * (e.g. "function foo {^Jecho bar^J}" then "type function") */
MSG	is_function	= " is a function";

/* TRANSLATION_NOTE
 * To be printed to show argument of "type" is exported alias and
 * what the alias is for.
 * (e.g. "alias -x foo='echo bar'; type foo") */
MSG	is_xalias	= " is an exported alias for ";

/* TRANSLATION_NOTE
 * To be printed to show argument of "type" is tracked alias
 * and what the alias is for.
 * Tracked alias is explicitly set by "hash" or automatically
 * remembered on commmand search.
 * (e.g. "who; type who") */
MSG	is_talias	= " is a tracked alias for ";

/* TRANSLATION_NOTE
 * To be printed to show argument of "type" is exported function. */
MSG	is_xfunction	= " is an exported function";

/* TRANSLATION_NOTE
 * To be printed to show argument of "type" is undefined function,
 * which name is marked by "autoload" but not found in FPATH.
 * (e.g. "autoload notinfpath; type notinfpath") */
MSG	is_ufunction	= " is an undefined function";

/* TRANSLATION_NOTE
 * To be printed to show argument of "type" is ordinary external
 * (i.e. non-builtin) command and where its location is.
 * (e.g. "hash -r; type ksh") */
MSG	is_		= " is ";
MSG	e_fnhdr		= "\n{\n";
MSG	e_runvi		= "fc -e \"${VISUAL:-${EDITOR:-vi}}\" ";
#ifdef JOBS
#   ifdef SIGTSTP
#ifdef NTTYDISC
	MSG	e_newtty	= "Switching to new tty driver...";
	MSG	e_oldtty	= "Reverting to old tty driver...";
#endif /* NTTYDISC */

/* TRANSLATION_NOTE
 * To be printed if shell itself is not foreground process on its
 * startup. */
	MSG	e_no_start	= "Cannot start job control";

/* TRANSLATION_NOTE
 * To be printed if job control is not available because monitor (-m)
 * is off.
 * (e.g. "set +m; sleep 100 &" then "fg %1") */
	MSG	e_no_jctl	= "No job control";

/* TRANSLATION_NOTE
 * To be printed if "exit" is attempted when stopped jobs exist.
 * (e.g. "sleep 100", ^Z, then "exit") */
	MSG	e_terminate	= "You have stopped jobs";
#   endif /*SIGTSTP */

/* TRANSLATION_NOTE
 * To be printed to show a job has been done.
 * (e.g. "sleep 1 &", wait 1 sec, then type return) */
   MSG	e_Done		= " Done";
   MSG	e_nlspace	= "\n      ";

/* TRANSLATION_NOTE
 * To be printed to show a job is running.
 * (e.g. "sleep 10 &" then "jobs") */
   MSG	e_Running	= " Running";

/* TRANSLATION_NOTE
 * To be printed if job name search matches two or more jobs.
 * (e.g. "sleep 50 &", "sleep 50 &", then "jobs %?sleep") */
   MSG	e_ambiguous	= "Ambiguous";

/* TRANSLATION_NOTE
 * To be printed if "exit" is attempted when running jobs exist.
 * (e.g. "sleep 20 &", then "exit") */
   MSG	e_running	= "You have running jobs";

/* TRANSLATION_NOTE
 * To be printed if job name search failed.
 * (e.g. "jobs %?nosuchjob") */
   MSG	e_no_job	= "no such job";

/* TRANSLATION_NOTE
 * To be printed if non-existent process ID is specified for "kill"
 * (e.g. "kill <non_existent_process_id>") */
   MSG	e_no_proc	= "no such process";
   MSG	e_killcolon	= "kill: ";

/* TRANSLATION_NOTE
 * To be printed if job specification is not in expected format.
 * Please keep "%" as it is. This string is not for printf().
 * (e.g. "jobs abc") */
   MSG	e_jobusage	= "Arguments must be %job or process ids";
   MSG	e_kill		= "kill";
#endif /* JOBS */

/* TRANSLATION_NOTE
 * To be printed if process of the job has core dumped.
 * (e.g. "sleep 100 &", "kill -ABRT %1", then type Return) */
MSG	e_coredump	= "(coredump)";
#ifdef DEVFD
    MSG	e_devfd		= "/dev/fd/";
#endif /* DEVFD */
#ifdef VPIX
    MSG	e_vpix		= "/vpix";
    MSG	e_vpixdir	= "/usr/bin";
#endif /* VPIX */
#ifdef apollo
    MSG e_rootnode 	= "Bad root node specification";
    MSG e_nover    	= "Version not defined";
    MSG e_badver   	= "Unrecognized version";
#endif /* apollo */
#ifdef LDYNAMIC
    MSG e_badinlib	= "Cannot inlib";
#endif /* LDYNAMIC */

/* string constants */
MSG	test_unops	= "HLSVOGCaohrwxdcbfugkpsnzte";

/* TRANSLATION_NOTE
 * Header of option setting list.
 * (e.g. "set -o") */
MSG	e_heading	= "Current option settings";
MSG	e_nullstr	= "";
MSG	e_sptbnl	= " \t\n";
MSG	e_defpath	= "/usr/bin:";
MSG	e_defedit	= "/bin/ed";
MSG	e_colon		= ": ";
MSG	e_minus		= "-";

/*
 * TRANSLATION_NOTE
 * formatted options status to be saved for resetting options
 * (e.g. "set +o")
 */
MSG	e_set		=       "set";  /* set command */
MSG	e_option_on	=       "-o";   /* option enabled - on */
MSG	e_option_off	=       "+o";   /* option disabled - off */

/* TRANSLATION_NOTE
 * To be printed in the syntax error message.
 * (e.g. "print -n 'echo foo >' > eof.ksh", then ". eof.ksh") */
MSG	e_endoffile	= "end of file";

/* TRANSLATION_NOTE
 * To be printed in the syntax error message.
 * (e.g. "if do") */
MSG	e_unexpected 	= " unexpected";

/* TRANSLATION_NOTE
 * To be printed in the syntax error message.
 * (e.g. prepare script below, chmod +x it, then run it)
 *	--- cut here ---
 *	#!/usr/bin/ksh
 *	echo 'foo
 *	bar
 *	--- cut here ---
 * ) */
MSG	e_unmatched 	= " unmatched";

/* TRANSLATION_NOTE
 * To be printed when 
 * (e.g. as root invoke ksh as "HISTFILE=/tmp/nosuchdir/hist ksh".
 *	 on the new shell, "sleep 100 &", then "jobs") */
MSG	e_unknown 	= "<command unknown>";

/* TRANSLATION_NOTE
 * To be printed in the syntax error message.
 * (e.g. prepare script below, chmod +x it, then run it)
 *	--- cut here ---
 *	echo 'foo
 *	bar
 *	--- cut here ---
 * ) */
MSG	e_atline	= " at line ";
MSG	e_devnull	= "/dev/null";
MSG	e_traceprompt	= "+ ";
MSG	e_supprompt	= "# ";
MSG	e_stdprompt	= "$ ";
MSG	e_profile	= "${HOME:-.}/.profile";
MSG	e_sysprofile	= "/etc/profile";
MSG	e_suidprofile	= "/etc/suid_profile";
MSG	e_crondir	= "/usr/spool/cron/atjobs";
#ifndef INT16

/* TRANSLATION_NOTE
 * To be printed when setuid/setgid script is used as login shell. */
   MSG	e_prohibited	= "login setuid/setgid shells prohibited";
#endif /* INT16 */
#ifdef SUID_EXEC
   MSG	e_suidexec	= "/etc/suid_exec";
#endif /* SUID_EXEC */
MSG	e_devfdNN	= "/dev/fd/+([0-9])";
MSG	hist_fname	= "/.sh_history";

/* TRANSLATION_NOTE
 * To be printed if current ulimit setting is unlimited.
 * (e.g. "ulimit -a") */
MSG	e_unlimited	= "unlimited";
MSG	e_echoucb   = "/usr/ucb/echo";
#ifdef ECHO_N
   MSG	e_echobin	= "/bin/echo";
   MSG	e_echoflag	= "-R";
#else
#ifdef ECHO_RAW
   MSG	e_echobin	= "/usr/bin/echo";
   MSG	e_echoflag	= "-R";
#endif	/* ECHO_RAW */
#endif	/* ECHO_N */
MSG	e_test		= "test";
MSG	e_dot		= ".";
MSG	e_bltfn		= "function ";
MSG	e_intbase	= "base";
MSG	e_envmarker	= "A__z";
#ifdef FLOAT
    MSG	e_precision	= "precision";
#endif /* FLOAT */
#ifdef PDUBIN
        MSG	e_setpwd	= "PWD=`/usr/pdu/bin/pwd 2>/dev/null`";
#else
        MSG	e_setpwd	= "PWD=`/bin/pwd 2>/dev/null`";
#endif /* PDUBIN */
MSG	e_real		= "\nreal";
MSG	e_user		= "user";
MSG	e_sys		= "sys";

/* TRANSLATION_NOTE
 * To be printed if setlocale() failed.
 * (e.g. "export LANG=no_SUCH.locale") */
MSG	e_locale	= "couldn't set locale correctly";
WMSG	we_nullstr	= L"";
WMSG	we_sptbnl	= L" \t\n";

/* TRANSLATION_NOTE
 * To be printed if -c was specified but there is no command string.
 * (e.g. "ksh -c -x") */
MSG	e_cmdstring	= "command string required";

#ifdef apollo
#   undef NULL
#   define NULL ""
#   define e_nullstr	""
#endif	/* apollo */

/* built in names */
const struct name_value node_names[] =
{
	"PATH",		NULL,	0,
	"PS1",		NULL,	0,
	"PS2",		"> ",	N_FREE,
#ifdef apollo
	"IFS",		" \t\n",	N_FREE,
#else
	"IFS",		e_sptbnl,	N_FREE,
#endif	/* apollo */
	"PWD",		NULL,	0,
	"HOME",		NULL,	0,
	"MAIL",		NULL,	0,
	"REPLY",	NULL,	0,
	"SHELL",	"/bin/sh",	N_FREE,
	"EDITOR",	NULL,	0,
#ifdef apollo
	"MAILCHECK",	NULL,	N_FREE|N_INTGER,
	"RANDOM",	NULL,	N_FREE|N_INTGER,
#else
	"MAILCHECK",	(char*)(&sh_mailchk),	N_FREE|N_INTGER,
	"RANDOM",	(char*)(&sh_randnum),	N_FREE|N_INTGER|N_BLTNOD,
#endif	/* apollo */
	"ENV",		NULL,	0,
	"HISTFILE",	NULL,	0,
	"HISTSIZE",	NULL,	0,
	"FCEDIT",	"/bin/ed",	N_FREE,
	"CDPATH",	NULL,	0,
	"MAILPATH",	NULL,	0,
	"PS3",		"#? ",	N_FREE,
	"OLDPWD",	NULL,	0,
	"VISUAL",	NULL,	0,
	"COLUMNS",	NULL,	0,
	"LINES",	NULL,	0,
#ifdef apollo
	"PPID",		NULL,	N_FREE|N_INTGER,
	"_",		NULL,	N_FREE|N_INDIRECT|N_EXPORT,
	"TMOUT",	NULL,	N_FREE|N_INTGER,
	"SECONDS",	NULL,	N_FREE|N_INTGER|N_BLTNOD,
	"ERRNO",	NULL,	N_FREE|N_INTGER|N_BLTNOD,
	"LINENO",	NULL,	N_FREE|N_INTGER|N_BLTNOD,
	"OPTIND",	NULL,	N_FREE|N_INTGER,
#else
	"PPID",		(char*)(&sh.ppid),	N_FREE|N_INTGER,
	"_",		(char*)(&sh.lastarg),	N_FREE|N_INDIRECT|N_EXPORT,
	"TMOUT",	(char*)(&sh_timeout),	N_FREE|N_INTGER,
	"SECONDS",	(char*)(&sh_seconds),	N_FREE|N_INTGER|N_BLTNOD,
	"ERRNO",	(char*)(&sh_errno),	N_FREE|N_INTGER|N_BLTNOD,
	"LINENO",	(char*)(&line_numbers),	N_FREE|N_INTGER|N_BLTNOD,
	"OPTIND",	(char*)(&opt_index),	N_FREE|N_INTGER,
#endif	/* apollo */
	"OPTARG",	NULL,	0,
	"PS4",		NULL,	0,
	"FPATH",	NULL,	0,
	"LANG",		NULL,	0,
	"LC_CTYPE",	NULL,	0,
	"VPATH",	NULL,	0,
	"LC_COLLATE",	NULL,	0,
	"LC_MESSAGES",	NULL,	0,
	"LC_ALL",	NULL,	0,
#ifdef VPIX
	"DOSPATH",	NULL,	0,
	"VPIXDIR",	NULL,	0,
#endif	/* VPIX */
#ifdef ACCT
	"SHACCT",	NULL,	0,
#endif	/* ACCT */
#ifdef apollo
	"SYSTYPE",	NULL,	0,
#endif /* apollo */
	e_nullstr,	NULL,	0
};

#ifdef VPIX
   const char *suffix_list[] = { ".com", ".exe", ".bat", e_nullstr };
#endif	/* VPIX */

/* built in aliases - automatically exported */
const struct name_value alias_names[] =
{
#ifdef FS_3D
	"2d",		"set -f;_2d ",	N_FREE|N_EXPORT,
#endif /* FS_3D */
	"autoload",	"typeset -fu",	N_FREE|N_EXPORT,
#ifdef POSIX
	"command",	"command ",	N_FREE|N_EXPORT,
#endif /* POSIX */
	"functions",	"typeset -f",	N_FREE|N_EXPORT,
	"history",	"fc -l",	N_FREE|N_EXPORT,
	"integer",	"typeset -i",	N_FREE|N_EXPORT,
#ifdef POSIX
	"local",	"typeset",	N_FREE|N_EXPORT,
#endif /* POSIX */
	"nohup",	"nohup ",	N_FREE|N_EXPORT,
	"r",		"fc -e -",	N_FREE|N_EXPORT,
#ifdef SIGTSTP
	"stop",		"kill -STOP",	N_FREE|N_EXPORT,
	"suspend",	"kill -STOP $$",	N_FREE|N_EXPORT,
#endif /*SIGTSTP */
	e_nullstr,	NULL,	0
};

const struct name_value tracked_names[] =
{
	"cat",		"/bin/cat",	N_FREE|N_EXPORT|T_FLAG,
	"chmod",	"/bin/chmod",	N_FREE|N_EXPORT|T_FLAG,
	"cc",		"/bin/cc",	N_FREE|N_EXPORT|T_FLAG,
	"cp",		"/bin/cp",	N_FREE|N_EXPORT|T_FLAG,
	"date",		"/bin/date",	N_FREE|N_EXPORT|T_FLAG,
	"ed",		"/bin/ed",	N_FREE|N_EXPORT|T_FLAG,
#ifdef _bin_grep_
	"grep",		"/bin/grep",	N_FREE|N_EXPORT|T_FLAG,
#else
#  ifdef _usr_ucb_
	"grep",		"/usr/ucb/grep",N_FREE|N_EXPORT|T_FLAG,
#  endif /* _usr_ucb_ */
#endif	/* _bin_grep */
#ifdef _usr_bin_lp
	"lp",		"/usr/bin/lp",	N_FREE|N_EXPORT|T_FLAG,
#endif /* _usr_bin_lpr */
#ifdef _usr_bin_lpr
	"lpr",		"/usr/bin/lpr",	N_FREE|N_EXPORT|T_FLAG,
#endif /* _usr_bin_lpr */
	"ls",		"/bin/ls",	N_FREE|N_EXPORT|T_FLAG,
	"make",		"/bin/make",	N_FREE|N_EXPORT|T_FLAG,
	"mail",		"/bin/mail",	N_FREE|N_EXPORT|T_FLAG,
	"mv",		"/bin/mv",	N_FREE|N_EXPORT|T_FLAG,
	"pr",		"/bin/pr",	N_FREE|N_EXPORT|T_FLAG,
	"rm",		"/bin/rm",	N_FREE|N_EXPORT|T_FLAG,
	"sed",		"/bin/sed",	N_FREE|N_EXPORT|T_FLAG,
	"sh",		"/bin/sh",	N_FREE|N_EXPORT|T_FLAG,
#ifdef _usr_bin_vi_
	"vi",		"/usr/bin/vi",	N_FREE|N_EXPORT|T_FLAG,
#else
#  ifdef _usr_ucb_
	"vi",		"/usr/ucb/vi",	N_FREE|N_EXPORT|T_FLAG,
#  endif /* _usr_ucb_ */
#endif	/* _usr_bin_vi_ */
	"who",		"/bin/who",	N_FREE|N_EXPORT|T_FLAG,
	e_nullstr,	NULL,	0
};

/* tables */
SYSTAB tab_reserved =
{
		/* XPG4: Requires '!' to be a reserved word */
#ifdef POSIX
		{"!",		NOTSYM},
#endif /* POSIX */
#ifdef NEWTEST
		{"[[",		BTSTSYM},
#endif /* NEWTEST */
		{"case",	CASYM},
		{"do",		DOSYM},
		{"done",	ODSYM},
		{"elif",	EFSYM},
		{"else",	ELSYM},
		{"esac",	ESSYM},
		{"fi",		FISYM},
		{"for",		FORSYM},
		{"function",	PROCSYM},
		{"if",		IFSYM},
		{"in",		INSYM},
		{"select",	SELSYM},
		{"then",	THSYM},
		{"time",	TIMSYM},
		{"until",	UNSYM},
		{"while",	WHSYM},
		{"{",		BRSYM},
		{"}",		KTSYM},
		{e_nullstr,	0},
};

/*
 * The signal numbers go in the low bits and the attributes go in the high bits
 */

SYSTAB	sig_names =
{
#ifdef SIGABRT
		{"ABRT",	(SIGABRT+1)|(SIGDONE<<SIGBITS)},
#endif /*SIGABRT */
		{"ALRM",	(SIGALRM+1)|((SIGCAUGHT|SIGFAULT)<<SIGBITS)},
#ifdef SIGAPOLLO
		{"APOLLO",	(SIGAPOLLO+1)},
#endif /* SIGAPOLLO */
#ifdef SIGBUS
		{"BUS",		(SIGBUS+1)|(SIGDONE<<SIGBITS)},
#endif /* SIGBUS */
#ifdef SIGCHLD
		{"CHLD",	(SIGCHLD+1)|((SIGCAUGHT|SIGFAULT)<<SIGBITS)},
#   ifdef SIGCLD
#	if SIGCLD!=SIGCHLD
		{"CLD",		(SIGCLD+1)|((SIGCAUGHT|SIGFAULT)<<SIGBITS)},
#	endif
#   endif	/* SIGCLD */
#else
#   ifdef SIGCLD
		{"CLD",		(SIGCLD+1)|((SIGCAUGHT|SIGFAULT)<<SIGBITS)},
#   endif	/* SIGCLD */
#endif	/* SIGCHLD */
#ifdef SIGCONT
		{"CONT",	(SIGCONT+1)},
#endif	/* SIGCONT */
		{"DEBUG",	(DEBUGTRAP+1)},
#ifdef SIGEMT
		{"EMT",		(SIGEMT+1)|(SIGDONE<<SIGBITS)},
#endif	/* SIGEMT */
		{"ERR",		(ERRTRAP+1)},
		{"EXIT",	1},
		{"FPE",		(SIGFPE+1)|(SIGDONE<<SIGBITS)},
		{"HUP",		(SIGHUP+1)|(SIGDONE<<SIGBITS)},
		{"ILL",		(SIGILL+1)|(SIGDONE<<SIGBITS)},
		{"INT",		(SIGINT+1)|(SIGCAUGHT<<SIGBITS)},
#ifdef SIGIO
		{"IO",		(SIGIO+1)},
#endif	/* SIGIO */
#ifdef SIGIOT
		{"IOT",		(SIGIOT+1)|(SIGDONE<<SIGBITS)},
#endif	/* SIGIOT */
		{"KILL",	(SIGKILL+1)},
#ifdef SIGLAB
		{"LAB",		(SIGLAB+1)},
#endif	/* SIGLAB */
#ifdef SIGLOST
		{"LOST",	(SIGLOST+1)},
#endif	/* SIGLOST */
#ifdef SIGLWP
		{"LWP",		(SIGLWP+1)},
#endif	/* SIGLWP */
#ifdef SIGPHONE
		{"PHONE",	(SIGPHONE+1)},
#endif	/* SIGPHONE */
#ifdef SIGPIPE
		{"PIPE",	(SIGPIPE+1)|(SIGDONE<<SIGBITS)},
#endif	/* SIGPIPE */
#ifdef SIGPOLL
		{"POLL",	(SIGPOLL+1)},
#endif	/* SIGPOLL */
#ifdef SIGPROF
		{"PROF",	(SIGPROF+1)},
#endif	/* SIGPROF */
#ifdef SIGPWR
#   if SIGPWR>0
		{"PWR",		(SIGPWR+1)},
#   endif
#endif	/* SIGPWR */
		{"QUIT",	(SIGQUIT+1)|((SIGCAUGHT|SIGIGNORE)<<SIGBITS)},
		{"SEGV",	(SIGSEGV+1)},
#ifdef SIGSTOP
		{"STOP",	(SIGSTOP+1)},
#endif	/* SIGSTOP */
#ifdef SIGSYS
		{"SYS",		(SIGSYS+1)|(SIGDONE<<SIGBITS)},
#endif	/* SIGSYS */
		{"TERM",	(SIGTERM+1)|(SIGDONE<<SIGBITS)},
#ifdef SIGTINT
		{"TINT",	(SIGTINT+1)},
#endif	/* SIGTINT */
#ifdef SIGTRAP
		{"TRAP",	(SIGTRAP+1)|(SIGDONE<<SIGBITS)},
#endif	/* SIGTRAP */
#ifdef SIGTSTP
		{"TSTP",	(SIGTSTP+1)},
#endif	/* SIGTSTP */
#ifdef SIGTTIN
		{"TTIN",	(SIGTTIN+1)},
#endif	/* SIGTTIN */
#ifdef SIGTTOU
		{"TTOU",	(SIGTTOU+1)},
#endif	/* SIGTTOU */
#ifdef SIGURG
		{"URG",		(SIGURG+1)},
#endif	/* SIGURG */
#ifdef SIGUSR1
		{"USR1",	(SIGUSR1+1)|(SIGDONE<<SIGBITS)},
#endif	/* SIGUSR1 */
#ifdef SIGUSR2
		{"USR2",	(SIGUSR2+1)|(SIGDONE<<SIGBITS)},
#endif	/* SIGUSR2 */
#ifdef SIGVTALRM
		{"VTALRM",	(SIGVTALRM+1)},
#endif	/* SIGVTALRM */
#ifdef SIGWAITING
		{"WAITING",	(SIGWAITING+1)},
#endif	/* SIGWAITING */
#ifdef SIGWINCH
		{"WINCH",	(SIGWINCH+1)},
#endif	/* SIGWINCH */
#ifdef SIGWINDOW
		{"WINDOW",	(SIGWINDOW+1)},
#endif	/* SIGWINDOW */
#ifdef SIGWIND
		{"WIND",	(SIGWIND+1)},
#endif	/* SIGWIND */
#ifdef SIGXCPU
		{"XCPU",	(SIGXCPU+1)|(SIGDONE<<SIGBITS)},
#endif	/* SIGXCPU */
#ifdef SIGXFSZ
		{"XFSZ",	(SIGXFSZ+1)|((SIGCAUGHT|SIGIGNORE)<<SIGBITS)},
#endif	/* SIGXFSZ */
		{e_nullstr,	0}
};

#ifndef	SIGSTRINGS
SYSTAB	sig_messages =
{
#ifdef SIGABRT
		{"Abort",			(SIGABRT+1)},
#endif /*SIGABRT */
		{"Alarm call",			(SIGALRM+1)},
#ifdef SIGBUS
		{"Bus error",			(SIGBUS+1)},
#endif /* SIGBUS */
#ifdef SIGCHLD
		{"Child stopped or terminated",	(SIGCHLD+1)},
#   ifdef SIGCLD
#	if SIGCLD!=SIGCHLD
		{"Death of Child", 		(SIGCLD+1)},
#	endif
#   endif	/* SIGCLD */
#else
#   ifdef SIGCLD
		{"Death of Child", 		(SIGCLD+1)},
#   endif	/* SIGCLD */
#endif	/* SIGCHLD */
#ifdef SIGCONT
		{"Stopped process continued",	(SIGCONT+1)},
#endif	/* SIGCONT */
#ifdef SIGEMT
		{"EMT trap",			(SIGEMT+1)},
#endif	/* SIGEMT */
		{"Floating exception",		(SIGFPE+1)},
		{"Hangup",			(SIGHUP+1)},
		{"Illegal instruction",		(SIGILL+1)},
#ifdef JOBS
		{"Interrupt",			(SIGINT+1)},
#else
		{e_nullstr,			(SIGINT+1)},
#endif	/* JOBS */
#ifdef SIGIO
		{"IO signal",			(SIGIO+1)},
#endif	/* SIGIO */
#ifdef SIGIOT
		{"Abort",			(SIGIOT+1)},
#endif	/* SIGIOT */
		{"Killed",			(SIGKILL+1)},
#ifdef SIGLWP
		{"LWP signal",			(SIGLWP+1)},
#endif	/* SIGLWP */
		{"Quit",			(SIGQUIT+1)},
#ifdef JOBS
		{"Broken Pipe",			(SIGPIPE+1)},
#else
		{e_nullstr,			(SIGPIPE+1)},
#endif	/* JOBS */
#ifdef SIGPROF
		{"Profiling time alarm",	(SIGPROF+1)},
#endif	/* SIGPROF */
#ifdef SIGPWR
#   if SIGPWR>0
		{"Power fail",			(SIGPWR+1)},
#   endif
#endif	/* SIGPWR */
		{"Memory fault",		(SIGSEGV+1)},
#ifdef SIGSTOP
		{"Stopped (signal)",		(SIGSTOP+1)},
#endif	/* SIGSTOP */
#ifdef SIGSYS
		{"Bad system call", 		(SIGSYS+1)},
#endif	/* SIGSYS */
		{"Terminated",			(SIGTERM+1)},
#ifdef SIGTINT
#   ifdef JOBS
		{"Interrupt",			(SIGTINT+1)},
#   else
		{e_nullstr,			(SIGTINT+1)},
#   endif /* JOBS */
#endif	/* SIGTINT */
#ifdef SIGTRAP
		{"Trace/BPT trap",		(SIGTRAP+1)},
#endif	/* SIGTRAP */
#ifdef SIGTSTP
		{"Stopped",			(SIGTSTP+1)},
#endif	/* SIGTSTP */
#ifdef SIGTTIN
		{"Stopped (tty input)",		(SIGTTIN+1)},
#endif	/* SIGTTIN */
#ifdef SIGTTOU
		{"Stopped(tty output)",		(SIGTTOU+1)},
#endif	/* SIGTTOU */
#ifdef SIGURG
		{"Socket interrupt",		(SIGURG+1)},
#endif	/* SIGURG */
#ifdef SIGUSR1
		{"User signal 1",		(SIGUSR1+1)},
#endif	/* SIGUSR1 */
#ifdef SIGUSR2
		{"User signal 2",		(SIGUSR2+1)},
#endif	/* SIGUSR2 */
#ifdef SIGVTALRM
		{"Virtual time alarm",		(SIGVTALRM+1)},
#endif	/* SIGVTALRM */
#ifdef SIGWAITING
		{"All lwps blocked", 		(SIGWAITING+1)},
#endif	/* SIGWAITING */
#ifdef SIGWINCH
		{"Window size change", 		(SIGWINCH+1)},
#endif	/* SIGWINCH */
#ifdef SIGXCPU
		{"Exceeded CPU time limit",	(SIGXCPU+1)},
#endif	/* SIGXCPU */
#ifdef SIGXFSZ
		{"Exceeded file size limit",	(SIGXFSZ+1)},
#endif	/* SIGXFSZ */
#ifdef SIGLOST
		{"Resources lost", 		(SIGLOST+1)},
#endif	/* SIGLOST */
#ifdef SIGLAB
		{"Security label changed",	(SIGLAB+1)},
#endif	/* SIGLAB */
		{e_nullstr,	0}
};
#endif	/* SIGSTRINGS */

SYSTAB tab_options=
{
	{"allexport",		Allexp},
	{"bgnice",		Bgnice},
	{"emacs",		Emacs},
	{"errexit",		Errflg},
	{"gmacs",		Gmacs},
	{"ignoreeof",		Noeof},
	{"interactive",		Intflg},
	{"keyword",		Keyflg},
	{"markdirs",		Markdir},
	{"monitor",		Monitor},
	{"noexec",		Noexec},
	{"noclobber",		Noclob},
	{"noglob",		Noglob},
	{"nolog",		Nolog},
	{"notify",              Notify},
	{"nounset",		Noset},
#ifdef apollo
	{"physical",		Aphysical},
#endif /* apollo */
	{"privileged",		Privmod},
	{"restricted",		Rshflg},
	{"trackall",		Hashall},
	{"verbose",		Readpr},
	{"vi",			Editvi},
	{"viraw",		Viraw},
	{"xtrace",		Execpr},
	{e_nullstr,		0}
};

#ifdef _sys_resource_
#   ifndef included_sys_time_
#	include <sys/time.h>
#   endif
#   include	<sys/resource.h>/* needed for ulimit */
#   define	LIM_FSIZE	RLIMIT_FSIZE
#   define	LIM_DATA	RLIMIT_DATA
#   define	LIM_STACK	RLIMIT_STACK
#   define	LIM_CORE	RLIMIT_CORE
#   define	LIM_CPU		RLIMIT_CPU
#   ifdef RLIMIT_RSS
#	define	LIM_MAXRSS	RLIMIT_RSS
#   endif /* RLIMIT_RSS */
#else
#   ifdef VLIMIT
#	include	<sys/vlimit.h>
#   endif /* VLIMIT */
#endif	/* _sys_resource_ */

#ifdef LIM_CPU
#   define size_resource(a,b) ((a)|((b)<<11))	
SYSTAB limit_names =
{
/* TRANSLATION_NOTE
 * To be printed in result of "ulimit -a". */
	{"time(seconds)       ",	size_resource(1,LIM_CPU)},

/* TRANSLATION_NOTE
 * To be printed in result of "ulimit -a". */
	{"file(blocks)        ",	size_resource(512,LIM_FSIZE)},

/* TRANSLATION_NOTE
 * To be printed in result of "ulimit -a". */
	{"data(kbytes)        ",	size_resource(1024,LIM_DATA)},

/* TRANSLATION_NOTE
 * To be printed in result of "ulimit -a". */
	{"stack(kbytes)       ",	size_resource(1024,LIM_STACK)},

#   ifdef LIM_MAXRSS

/* TRANSLATION_NOTE
 * To be printed in result of "ulimit -a".
 * This message is not used because current getrlimit() doesn't support
 * RLIMIT_RSS. */
	{"memory(kbytes)      ",	size_resource(1024,LIM_MAXRSS)},
#   else
	{"memory(kbytes)      ",	size_resource(1024,0)},
#   endif /* LIM_MAXRSS */

/* TRANSLATION_NOTE
 * To be printed in result of "ulimit -a". */
	{"coredump(blocks)    ",	size_resource(512,LIM_CORE)},
#   ifdef RLIMIT_NOFILE

/* TRANSLATION_NOTE
 * To be printed in result of "ulimit -a". */
	{"nofiles(descriptors)",	size_resource(1,RLIMIT_NOFILE)},
#   else
	{"nofiles(descriptors)",	size_resource(1,0)},
#   endif /* RLIMIT_NOFILE */
#   ifdef RLIMIT_VMEM
/* TRANSLATION_NOTE
 * To be printed in result of "ulimit -a". */
	{"vmemory(kbytes)     ",	size_resource(1024,RLIMIT_VMEM)}
#   else
	{"vmemory(kbytes)     ",	size_resource(1024,0)}
#   endif /* RLIMIT_VMEM */
};
#endif	/* LIM_CPU */

#ifdef cray
    const struct name_fvalue built_ins[] =
#   define VALPTR(x)	x
#else
#   define VALPTR(x)	((char*)x)
    const struct name_value built_ins[] =
#endif /* cray */
{
		{"login",	VALPTR(b_login),	N_BLTIN|BLT_ENV},
		/* XPG4: exec is a special built-in */
		{"exec",	VALPTR(b_exec),	N_BLTIN|BLT_SPC|BLT_ENV},
		/* XPG4: set is a special built-in */
		{"set",	VALPTR(b_set),	N_BLTIN|BLT_SPC|I_FLAG},
		{":",		VALPTR(b_null),		N_BLTIN|BLT_SPC},
		{"true",	VALPTR(b_null),		N_BLTIN},
#ifdef _bin_newgrp_
		{"newgrp",	VALPTR(b_login),	N_BLTIN|BLT_ENV},
#endif	/* _bin_newgrp_ */
		{"false",	VALPTR(b_null),		N_BLTIN},
#ifdef apollo
		{"rootnode",	VALPTR(b_rootnode),	N_BLTIN},
		{"ver",		VALPTR(b_ver),		N_BLTIN},
#endif	/* apollo */
#ifdef LDYNAMIC
		{"inlib",	VALPTR(b_inlib),	N_BLTIN},
#   ifndef apollo
		{"builtin",	VALPTR(b_builtin),	N_BLTIN},
#   endif	/* !apollo */
#endif	/* LDYNAMIC */
		{".",		VALPTR(b_dot),		N_BLTIN|BLT_SPC|BLT_FSUB},
		{"readonly",	VALPTR(b_readonly),	N_BLTIN|BLT_SPC|BLT_DCL},
		{"typeset",	VALPTR(b_typeset),	N_BLTIN|BLT_SPC|BLT_DCL},
		{"return",	VALPTR(b_ret_exit),	N_BLTIN|BLT_SPC},
		{"export",	VALPTR(b_export),	N_BLTIN|BLT_SPC|BLT_DCL},
		{"eval",	VALPTR(b_eval),		N_BLTIN|BLT_SPC|BLT_FSUB},
		{"fc",		VALPTR(b_fc),		N_BLTIN|BLT_FSUB},
		{"shift",	VALPTR(b_shift),	N_BLTIN|BLT_SPC},
		{"cd",		VALPTR(b_chdir),	N_BLTIN},
#ifdef OLDTEST
		{"[",		VALPTR(b_test),		N_BLTIN},
#endif /* OLDTEST */
		/* XPG4: Make alias builtin : Remove BLT_SPC */
		{ "alias",	VALPTR(b_alias),	N_BLTIN|BLT_DCL},
		{"break",	VALPTR(b_break),	N_BLTIN|BLT_SPC},
		{"continue",	VALPTR(b_continue),	N_BLTIN|BLT_SPC},
#ifdef ECHOPRINT
		{"echo",	VALPTR(b_print),	N_BLTIN},
#else
		{"echo",	VALPTR(b_echo),		N_BLTIN},
#endif /* ECHOPRINT */
		{"exit",	VALPTR(b_ret_exit),	N_BLTIN|BLT_SPC},
#ifdef JOBS
# ifdef SIGTSTP
		{"bg",		VALPTR(b_bgfg),		N_BLTIN},
		{"fg",		VALPTR(b_bgfg),		N_BLTIN},
# endif	/* SIGTSTP */
		{"jobs",	VALPTR(b_jobs),		N_BLTIN},
		{"kill",	VALPTR(b_kill),		N_BLTIN},
#endif	/* JOBS */
		{"let",		VALPTR(b_let),		N_BLTIN},
		{"print",	VALPTR(b_print),	N_BLTIN},
		{"pwd",		VALPTR(b_pwd),		N_BLTIN},
		{"read",	VALPTR(b_read),		N_BLTIN},
#ifdef SYSCOMPILE
		{"shcomp",	VALPTR(b_shcomp),	N_BLTIN},
#endif /* SYSCOMPILE */
#ifdef SYSSLEEP
		{"sleep",	VALPTR(b_sleep),	N_BLTIN},
#endif /* SYSSLEEP */
#ifdef OLDTEST
		{"test",	VALPTR(b_test),		N_BLTIN},
#endif /* OLDTEST */
		{"times",	VALPTR(b_times),	N_BLTIN|BLT_SPC},
		{"trap",	VALPTR(b_trap),		N_BLTIN|BLT_SPC},
		{"ulimit",	VALPTR(b_ulimit),	N_BLTIN},
		{"umask",	VALPTR(b_umask),	N_BLTIN},
		{"unalias",	VALPTR(b_unalias),	N_BLTIN},
		/* XPG4: unset is a special built-in */
		{"unset",	VALPTR(b_unset),	N_BLTIN|BLT_SPC|I_FLAG},
		/* XPG4: Make wait builtin : Remove BLT_SPC */
		{"wait",	VALPTR(b_wait),		N_BLTIN},
		{"whence",	VALPTR(b_whence),	N_BLTIN},
		{"getopts",	VALPTR(b_getopts),	N_BLTIN},
#ifdef UNIVERSE
		{"universe",	VALPTR(b_universe),	N_BLTIN},
#endif /* UNIVERSE */
#ifdef FS_3D
		{"vpath",	VALPTR(b_vpath_map),	N_BLTIN},
		{"vmap",	VALPTR(b_vpath_map),	N_BLTIN},
#endif /* FS_3D */
		{"command",	VALPTR(b_command),	N_BLTIN},
		{"hash",	VALPTR(b_hash),	N_BLTIN},
		{"type",	VALPTR(b_type),	N_BLTIN},
		{e_nullstr,		0, 0 }
};

/*
 * Test operator table for ksh "test" command. This table follows
 * ASCII precedence order for operators used by "test". 
 */
SYSTAB	test_optable =
{
		{"!=",		TEST_SNE},
		{"-a",		TEST_AND},
		{"-ef",		TEST_EF},
		{"-eq",		TEST_EQ},
		{"-ge",		TEST_GE},
		{"-gt",		TEST_GT},
		{"-le",		TEST_LE},
		{"-lt",		TEST_LT},
		{"-ne",		TEST_NE},
		{"-nt",		TEST_NT},
		{"-o",		TEST_OR},
		{"-ot",		TEST_OT},
#ifdef NEWTEST
		 {"<",		TEST_SLT}, 
#endif /* NEWTEST */
		{"=",		TEST_SEQ},
		{"==",		TEST_SEQ},
#ifdef NEWTEST 
		{">",		TEST_SGT},
		{"]]",		TEST_END},
#endif /* NEWTEST */
		{e_nullstr,	0}
};

SYSTAB	tab_attributes =
{
		{"export",	N_EXPORT},
		{"readonly",	N_RDONLY},
		{"tagged",	T_FLAG},
#ifdef FLOAT
		{"exponential",	(N_DOUBLE|N_INTGER|N_EXPNOTE)},
		{"float",	(N_DOUBLE|N_INTGER)},
#endif /* FLOAT */
		{"long",	(L_FLAG|N_INTGER)},
		{"unsigned",	(N_UNSIGN|N_INTGER)},
		{"function",	(N_BLTNOD|N_INTGER)},
		{"integer",	N_INTGER},
		{"filename",	N_HOST},
		{"lowercase",	N_UTOL},
		{"zerofill",	N_ZFILL},
		{"leftjust",	N_LJUST},
		{"rightjust",	N_RJUST},
		{"uppercase",	N_LTOU},
		{e_nullstr,	0}
};


#ifndef IODELAY
#   undef _SELECT5_
#endif /* IODELAY */
#ifdef _sgtty_
#   ifdef _SELECT5_
	const int tty_speeds[] = {0, 50, 75, 110, 134, 150, 200, 300,
			600,1200,1800,2400,9600,19200,0};
#   endif /* _SELECT5_ */
#endif /* _sgtty_ */
