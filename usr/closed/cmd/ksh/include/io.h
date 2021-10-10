/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *	UNIX shell
 *	S. R. Bourne
 *	rewritten by David Korn
 *
 */

#ifndef	___KSH_IO_H
#define	___KSH_IO_H

#include	"csi.h"
#include	"sh_config.h"
#include	<sys/stat.h>
#ifdef _unistd_
#   include	<unistd.h>
#endif /* _unistd_ */
#ifdef _fcntl_
#   include	<fcntl.h>
#endif /* _fcntl_ */
#ifndef	O_CREAT
#   ifdef _sys_file_
#	include	<sys/file.h>
#   endif /* _sys_file_ */
#endif	/* O_CREAT */

#ifndef S_ISDIR
#   define S_ISDIR(m)	(((m)&S_IFMT)==S_IFDIR)
#endif /* S_ISDIR */
#ifndef S_ISREG
#   define S_ISREG(m)	(((m)&S_IFMT)==S_IFREG)
#endif /* S_ISREG */
#ifndef S_ISCHR
#   define S_ISCHR(m)	(((m)&S_IFMT)==S_IFCHR)
#endif /* S_ISCHR */
#ifndef S_ISBLK
#   define S_ISBLK(m)	(((m)&S_IFMT)==S_IFBLK)
#endif /* S_ISBLK */
#ifdef S_IFIFO
#   ifndef S_ISFIFO
#	define S_ISFIFO(m)	(((m)&S_IFMT)==S_IFIFO)
#   endif /* S_ISFIFO */
#endif /* S_IFIFO */
#ifndef S_IRUSR
#   define S_IRUSR	(S_IREAD)
#endif /* S_IRUSR */
#ifndef S_IWUSR
#   define S_IWUSR	(S_IWRITE)
#endif /* S_IWUSR */
#ifndef S_IXUSR
#   define S_IXUSR	(S_IEXEC)
#endif /* S_IXUSR */
#ifndef S_IRGRP
#   define S_IRGRP	(S_IREAD>>3)
#endif /* S_IRGRP */
#ifndef S_IWGRP
#   define S_IWGRP	(S_IWRITE>>3)
#endif /* S_IWGRP */
#ifndef S_IXGRP
#   define S_IXGRP	(S_IEXEC>>3)
#endif /* S_IXGRP */
#ifndef S_IROTH
#   define S_IROTH	(S_IREAD>>6)
#endif /* S_IROTH */
#ifndef S_IWOTH
#   define S_IWOTH	(S_IWRITE>>6)
#endif /* S_IWOTH */
#ifndef S_IXOTH
#   define S_IXOTH	(S_IEXEC>>6)
#endif /* S_IXOTH */
#define RW_ALL		(S_IRUSR|S_IRGRP|S_IROTH|S_IWUSR|S_IWGRP|S_IWOTH)

#ifndef NFILE
#   define NFILE	20
#endif /* NFILE */
#ifndef IOBSIZE
#   define  IOBSIZE	1024
#endif /* IOBSIZE */
#define EOF		(-1)
#define MAXTRY		12
#ifdef SEVENBIT
#   define STRIP	0177
#else
#   define STRIP	0377
#endif /* SEVENBIT */

/* used for input and output of shell */
#define TMPSIZ		20
#define ERRIO		2
#define USERIO		10
#define FCIO		(NFILE-1) /* history file */
#ifdef KSHELL
#   define INIO		(NFILE-2) /* saved standard ioput */
#   define TMPIO	(NFILE-3) /* used for command substitution */
#   define CINPIPE	(NFILE-4) /* default inpipe for co-process */
#   define CINPIPE2	(NFILE-5) /* other end of inpipe for co-process */
#   define COTPIPE	(NFILE-6) /* default output pipe for co-process */
#   define MAXFILES	(NFILE-USERIO) /* maximum number of saved open files */

#   define F_STRING	((unsigned char)NFILE)	/* file number for incore files */
#   define F_INFINITE	0x7fff			/* effectively infinite */
#endif /* KSHELL */

#define MARK_CLOEXEC	0x40000000	/* close on exec mark */

/* SHELL file I/O structure */
struct fileblk
{
	char		*ptr;
	char		*base;
	char		*last;
	off_t		fseek;
	int		flag;
	int		fdes;
#ifdef KSHELL
	char		ftype;
	int		flast;
	char		**feval;
	struct fileblk	*fstak;
	unsigned	flin;
	char		*lptr;	/* last value of ptr to go back */
#endif /* KSHELL */
};

#define filenum(fp)	(fp->fdes)		/* file number */
#define fnobuff(fp)	((fp)->flag&IONBF)	/* file is unbuffered */

#define IOREAD	0001
#define IOWRT	0002
#define IONBF	0004
#define IOFREE	0010
#define IOEOF	0020
#define IOERR	0040
#define IORW	0100
#define IOSLOW	0200
#define IOEDIT	0400
/* 1192528: Add flag to mark output buffer has been flushed */
#define    IOFLUS    01000

#ifdef	FNDELAY
#   ifdef EAGAIN
#	if EAGAIN!=EWOULDBLOCK
#	    undef EAGAIN
#	    define EAGAIN       EWOULDBLOCK
#	endif
#   else
#	define EAGAIN   EWOULDBLOCK
#   endif /* EAGAIN */
#   ifndef O_NONBLOCK
#	define O_NONBLOCK	FNDELAY
#   endif /* !O_NONBLOCK */
#endif	/* FNDELAY */
#ifndef	O_CREAT
#   define	O_CREAT		0400
#   define	O_TRUNC		01000
#   define	O_APPEND	010
#endif	/* O_CREAT */
#ifndef O_RDWR
#   define	O_RDONLY	0
#   define	O_WRONLY	01
#   define	O_RDWR		02
#endif /* O_RDWR */
#ifdef NOFCNTL
#   define	open		myopen
#   define	F_DUPFD		0
#   define	F_GETFD		1
#   define	F_SETFD		2
#endif /* F_DUPFD */
#ifndef R_OK
#   define F_OK 	   0	   /* does file exist */
#   define X_OK 	   1	   /* is it executable by caller */
#   define W_OK 	   2	   /* writable by caller */
#   define R_OK 	   4	   /* readable by caller */
#endif /* R_OK */
#ifndef SEEK_SET
#   define SEEK_SET	   0	   /* absolute offset */
#   define SEEK_CUR	   1	   /* relative offset */
#   define SEEK_END	   2	   /* EOF offset */
#endif /* SEEK_SET */


/*io nodes*/
#define INPIPE	0
#define OTPIPE	1
#define DUPFLG	0100

/*
 * The remainder of this file is only used when compiled with shell
 */

#ifdef KSHELL
/* possible values for ftype */
#define	F_ISSTRING	1
#define F_ISFILE	2
#define F_ISALIAS	3
#define F_ISEVAL	4
#define F_ISEVAL2	5


/* The following union is used for argument to sh_eval */
union io_eval
{
	int 	fd;
	char	**com;
};

extern void	io_unreadc(wchar_t);

#define input		(st.curin)
#define output		(sh.curout)
#define newline()	p_char(NL)
#define fisopen(fd)	(io_access(fd,F_OK)==0)
#define fiswrite(fd)	(io_access(fd,W_OK)==0)
#define fisread(fd)	(io_access(fd,R_OK)==0)
#define fiseof(fp)	((fp)->flag&IOEOF)
#define fiserror(fp)	((fp)->flag&IOERR)
#define nextchar(fp)	(*((fp)->ptr))
#define finbuff(fp)	((fp)->last - (fp)->ptr)
#define io_clearerr(fp)	((fp)->flag &= ~(IOERR|IOEOF))

struct filesave
{
	int	org_fd;	/* user provided fds never go beyond IOUFD */
	int	dup_fd;
};


extern off_t	lseek(int,off_t,int);
extern void	io_clear(struct fileblk*);
extern void 	io_fclose(int);
extern wchar_t	io_getc(int);
extern int	io_getb(int);
extern void	io_ungetc(int);
extern void 	io_init(int,struct fileblk*,char*);
extern int	io_intr(struct fileblk*);
extern void 	io_push(struct fileblk*);
extern int	io_pop(int);
extern int	io_mktmp(char*);
extern off_t	io_seek(int,off_t,int);
extern int	io_readbuff(struct fileblk*);
extern wchar_t	io_readc(void);
extern int	io_renumber(int,int);
extern void 	io_sync(void);
extern int	io_movefd(int);
extern void 	io_popen(int[]);
extern void 	io_pclose(int[]);
extern void 	io_restore(int);
struct ionod;
extern int	io_redirect(struct ionod*,int);
extern void 	io_save(int,int);
extern void 	io_linkdoc(struct ionod*);
extern void 	io_swapdoc(struct ionod*);
extern int	io_fopen(const char*);
extern int	io_safe_fopen(const char *);
extern void 	io_sopen(char*);
extern int	io_access(int,int);
extern wchar_t	io_nextc(void);
extern int	ispipe(int);
extern struct fileblk *io_get_ftbl(int);
extern void	io_set_ftbl(int, struct fileblk *);
extern struct fileblk **io_ftbl_save(int *);
extern void	io_ftbl_restore(struct fileblk **, int);
extern int	io_record_start(void);
extern void	io_record_copy(int);
extern void	io_record_end(int);
extern void	io_settemp(pid_t);

extern unsigned char _sibuf[];
extern unsigned char	_sobuf[];
extern struct fileblk	io_stdin;
extern struct fileblk	io_stdout;
extern char	io_tmpname[];

/* the following are readonly */
extern const char	e_create[];
extern const char	e_file[];
extern const char	e_open[];
extern const char	e_pipe[];
extern const char	e_flimit[];
extern const char	e_fexists[];
extern const char	e_unknown[];
extern const char	e_endoffile[];
extern const char	e_devnull[];
extern const char	e_profile[];
extern const char	e_suidprofile[];
extern const char	e_sysprofile[];
extern const char	e_devfdNN[];
#ifdef SUID_EXEC
    extern const char	e_suidexec[];
#endif /* SUID_EXEC */
#endif /* KSHELL */

#endif	/* !___KSH_IO_H */
