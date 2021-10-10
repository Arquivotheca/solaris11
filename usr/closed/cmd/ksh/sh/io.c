/*
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved. 
 */

/*
 * UNIX shell
 *
 * S. R. Bourne
 * Rewritten by David Korn
 * AT&T Bell Laboratories
 *
 */

#include	<errno.h>
#include	<libintl.h>
#include	"defs.h"
#include	"sym.h"
#include	"history.h"
#include	<stropts.h>
#include	<sys/types.h>
#include	<sys/conf.h>
#include	<string.h>
#ifndef SIG_NORESTART
#   include	<signal.h>
    static jmp_buf readerr;
    static VOID interrupt();
#endif	/* SIG_NORESTART */
#ifdef SOCKET
#   include <sys/socket.h>
#   include <netinet/in.h>
    static int str2inet();
#endif /* SOCKET */

extern char *utos();

static int io_readbyte(void);
static void io_record_getc(wchar_t);
static void io_record_ungetc(int);
static void io_record_push(void);
static void io_record_pop(void);

static struct filesave *fdmap;
static size_t fdmap_size;
static struct fileblk *iofree = &io_stdin;
static int nosave;
static int serial;
static char *temp_suffix;

/* ======== input output and file copying ======== */

/*
 * initialize temp file names
 */
void io_settemp(pid)
pid_t pid;
{
	register char *sp = sh_copy(utos((ulong)pid,10),io_tmpname+7);
	*sp++ = '.';
	temp_suffix = sp;
	serial = 0;
}

/*
 * set up a fileblk associated with the file descriptor fd using buffer buf
 */

void	io_init(fd,fp,buf)
register int	fd;
register struct fileblk *fp;
char *buf;
{
	register unsigned size = IOBSIZE+1;
	if(!fp)
	{
		if(iofree)
		{
			fp = iofree;
			iofree = 0;
			fp->flag = 0;
			buf = fp->base;
		}
		else
		{
			/* buffer the stream if possible */
			fp = new_of(struct fileblk,size);
			buf = (char*)(fp+1);
			fp->fstak = 0;
			fp->flag = IOFREE;
		}
	}
	else
		fp->flag = 0;
	fp->fdes = fd;
	fp->base = fp->ptr = fp->last = buf;
	*fp->ptr = 0;
	fp->lptr = NULL;
	fp->flag |= IOREAD;
	fp->ftype=F_ISFILE;
	fp->flin=1;
	fp->fseek = 0;
	io_set_ftbl(fd, NULL);
	if(io_access(fd,W_OK)==0)
		fp->flag |= IORW;
	io_set_ftbl(fd, fp);
	if (ispipe(fd))
		fp->flag |= (IOSLOW|IONBF);
	else if( tty_check(fd))
		fp->flag |= IOSLOW;
}

/* 
 * push an iostream onto the input queue
 */

void io_push(fp)
struct fileblk *fp;
{
	fp->fstak=st.standin;
	st.standin=fp;

	io_record_push();
}

/*
 * pop an iostream from the input queue
 * close stream if flag==0, filenum>0 && filenum!=INIO and flag is a file.
 */

int io_pop(flag)
int flag;
{
	register struct fileblk *fp;
	register int fno;

	io_record_pop();

	if((fp=st.standin) && fp->fstak)
	{
		fno = filenum(fp);
		if(flag==0 && fno>0 && fno!=F_STRING && fno!=INIO && fno!=sh.cpipe[INPIPE])
			io_fclose(fno);
		st.standin=fp->fstak;
		input = filenum(st.standin);
		if(fp->ftype == F_ISALIAS)
		{
			if(fp->feval)
			{
				struct namnod *np = (struct namnod*)(fp->feval);
				nam_offtype(np,~M_FLAG);
			}
			free((char*)fp);
		}
		return(1);
	}
	return(0);
}


/*
 * clear iostack up to <iop>
 */

void io_clear(iop)
register struct fileblk *iop;
{
	while((iop != st.standin) &&  io_pop(0));
}

/*
 * given a file descriptor f1, move it to a new file descriptor number
 * f2.  If f2 is open then it is closed first.
 * If the MARK_CLOEXEC bit not set on f2, then close on exec will be set
 * for f2>2. The original stream is closed.
 * File numbers greater than 2 are marked close on exec if io_renumber is 
 *  invoked by a parent shell.
 *  The new file descriptor is returned;
 */

int io_renumber(f1,f2)
register int f1;
register int f2;
{
	register int flag = (f2&MARK_CLOEXEC);
	register int fs=0;
	struct fileblk *fp;

	f2 &= ~MARK_CLOEXEC;
	if(f2>2 && flag==0)
		fs = 1;
	if(f1!=f2)
	{
		io_fclose(f2);
		f2 = fcntl(f1,F_DUPFD, f2);
		if(f2 < 0)
			sh_fail((const char *)gettext(e_file), NIL);
		fp = io_get_ftbl(f1);
		io_set_ftbl(f2, fp);
		if (fp != NULL)
			fp->fdes = f2;
		io_set_ftbl(f1, NULL);
		close(f1);
		if(f2==0)
			st.ioset = 1;
	}
	if(fs==1)
	/* set close on exec */
		fcntl(f2,F_SETFD,1);
	return(f2);
}

int io_mktmp(fname)
register char *fname;
{
	register int maxtry = MAXTRY;
	register char *tmp_name = io_tmpname;
	register int fd;
	do
	{
		sh_copy(sh_itos(++serial),temp_suffix);
	}
	while((fd=open(tmp_name,O_CREAT|O_EXCL|O_RDWR, 0600))< 0 && maxtry--);
	if(fname)
	{
		sh_copy(tmp_name,fname);
		if(fd<0)
			sh_fail(tmp_name,e_create);
	}
	fd = io_movefd(fd);
	io_set_ftbl(fd, NULL);
	return(fd);
}
	 
/*
 * returns the next character from file <fd>
 */

wchar_t
io_getc(fd)
int fd;
{
	register struct fileblk *fp = io_get_ftbl(fd);
	wchar_t c;
	int	l;
	int	i;

	if (!fp) {
		c = (wchar_t)WEOF;
		return (c);
	}
	fp->lptr = NULL;
	if (*fp->ptr == '\0' && fp->ptr < fp->last) {
		fp->ptr++;
		c = 0;
		return (c);
	}
	for (i = 1; i <= MB_CUR_MAX; i++) {
		if (fp->ptr + i > fp->last) {
			/* Enough bytes not available. Try to read more. */
			if (io_readbuff(fp) == EOF) {
				if (i == 1) {
					c = (wchar_t)WEOF;
				} else {
					sh_mbtowc(&c, fp->ptr++, 1);
				}
				return (c);
			} else
				/* unread returned value of io_readbuff */
				fp->ptr--;
		}

		if (l = sh_mbtowc(&c, fp->ptr, i), !IsWInvalid(c)) {
			fp->lptr = fp->ptr;
			fp->ptr += l;
			return (c);
		}
	}

	/* Enough bytes available but cannot be converted to wchar.
	   Buffer must contain illegal byte sequence. */
	fp->lptr = fp->ptr;
	sh_mbtowc(&c, fp->ptr++, 1);
	return (c);
}

int
io_getb(int fd)
{
	struct fileblk *fp = io_get_ftbl(fd);
	int c;
	if(!fp)
		return(EOF);
	fp->lptr = fp->ptr;
	if(c= *fp->ptr++)
		return(c&STRIP);
	if(fp->ptr <= fp->last)
		return(0);
	return(io_readbuff(fp));
}

void
io_ungetc(
	int	fd
)
{
	register struct fileblk *fp = io_get_ftbl(fd);
	if(!fp)
		return;
	if (fp->lptr)
		fp->ptr = fp->lptr;
}

/*
 * This special version does not handle ptrname==1
 * It also saves a lot of real seeks on history file
 */

off_t io_seek(fd, offset, ptrname)
int fd;
off_t	offset;
register int	ptrname;
{
	register struct fileblk *fp;
	register int c;
	off_t	p;

	if(!(fp=io_get_ftbl(fd)))
		return(lseek(fd,offset,ptrname));
	fp->lptr = NULL;
	fp->flag &= ~IOEOF;
	if(!(fp->flag&IOREAD) && fp->ptr!=fp->base)
	{
		c = output;
		output = fd;
		p_flush();
		output = c;
	}
	c = 0;
	/* check history file to see if already in the buffer */
	if(ptrname==0 && (fp->flag&IOREAD) && offset<fp->fseek)
	{
		p = fp->fseek - (fp->last - fp->base);
		if(offset >= p)
		{
			fp->ptr = fp->base + (int)(offset-p);
			return(offset);
		}
		else
		{
			c = offset&(IOBSIZE-1);
			offset -= c;
		}
	}
	if(fp->flag&IORW)
	{
		fp->flag &= ~(IOWRT|IOREAD);
		fp->last = fp->ptr = fp->base;
		*fp->last = 0;
	}
	p = lseek(fd, offset, ptrname);
	if(!(fp->flag&IOSLOW))
	{
		if(ptrname==0)
			fp->fseek = p;
		else
			fp->fseek = -1;
		if(c)
		{
			fp->last = fp->ptr;
			io_readbuff(fp);
			fp->ptr += (c-1);
		}
	}
	return(p);
}

/*
 * Read from file into fp
 * Call the edit routines if necessary
 * If any chars between fp->ptr and fp->last were left, copy those chars
 * to the beginning of fp->base and append the new data from file after them.
 */

int io_readbuff(fp)
register struct fileblk *fp;
{
	register int n;
	register int fno;
	int left_over;
	char *tmp_ptr;	/*  for preserving fp->ptr */

	if (fp->flag & IORW)
		fp->flag |= IOREAD;

	if (!(fp->flag&IOREAD))
		return(EOF);

	/* Calculate if there are any chars leftover. */
	left_over = fp->last - fp->ptr;
	if (left_over > 0)
		tmp_ptr = fp->ptr;
	else
		left_over = 0;	/* left_over should not be negative */

	/* The following line is needed in case an interrupt occurs */
	fp->ptr = fp->last;
	if(fp->flag&IOSLOW)
	{
		job_wait((pid_t)0);
#ifndef SIG_NORESTART
		st.intfn = interrupt;
		/* automatically restart with some systems */
		n = -1;
		*fp->base = 0;
		if(SETJMP(readerr))
		{
			n = -1;
			errno = EINTR;
			goto slowsig;
		}
#endif	/* SIG_NORESTART */
	}
	fno = filenum(fp);

	/* If chars were leftover, copy them to the beginning of fp->base. */
	if (left_over > 0)
		(void) memmove(fp->base, tmp_ptr, left_over);

retry:
	/* don't attempt editing mode for pipes with allocated buffers */
	if(!(fp->flag&IOEDIT) || (fp->flag&IONBF))
		goto skip;
#   ifdef ESH
	if(is_option(EMACS|GMACS))
		n = emacs_read(fno, fp->base + left_over,
			(unsigned)(MAXLINE - left_over - 2));
	else
#   endif	/* ESH */
#   ifdef VSH
	if(is_option(EDITVI))
		n = vi_read(fno, fp->base + left_over,
			(unsigned)(MAXLINE - left_over - 2));
	else
#   endif	/* VSH */
	{
		register int size;
	skip:
		/* unbuffered reads needed for pipe on standard input */
		if(fnobuff(fp) && fno==0)
		{
			struct strpeek pk;
			char buffer[BUFSIZ + 1];
			char *p;

			pk.ctlbuf.maxlen = 0;
			pk.ctlbuf.len = 0;
			pk.ctlbuf.buf = NULL;
			pk.flags = 0;
			pk.databuf.maxlen = BUFSIZ;
			pk.databuf.len = 0;

			pk.databuf.buf = buffer;

			/*
			 * If something in pipe, figure out how many
			 * characters until the new line.  That will
			 * be how many characters you will read in
			 * the read system call. The newline is
			 * also included.   If there is an error
			 * trying to "peek" into the pipe, then go
			 * back to the usual-reading a byte at a
			 * time in the pipe.
			 */

			if (ioctl(fno, I_PEEK, &pk) != -1)
			{
				if (pk.databuf.len > 0)
				{
					int index = 0;
					buffer[pk.databuf.len] = '\0';

					if ((p = strchr(buffer, '\n'))
!= NULL)

						size = p -buffer + 1;
					else
						size = BUFSIZ;
				}
				else
					size = 1;
			}
			else /* I_PEEK error */
				size = 1;
		}
		else
			size = IOBSIZE;
		if(fp->flag&(IOSLOW|IOEDIT))
		{
			p_flush();
		}
		/* to prevent overflow */
		if (size + left_over > IOBSIZE)
			size = IOBSIZE - left_over;
		/*
		 * Ignoring signals while blocking on read
		 */
		do {
			n = read(fno, fp->base + left_over, (unsigned)size);
		} while (n < 0 && errno == EINTR && fp==st.standin);
	}
#ifndef SIG_NORESTART
slowsig:
	if(fp->flag&IOSLOW)
		st.intfn = 0;
#endif	/* SIG_NORESTART */
	fp->ptr = fp->base;
	if(n > 0)
		fp->last = fp->base + n + left_over;
	else
		fp->last = fp->ptr + left_over;
	*fp->last = 0;
	if (n <= 0)
	{
		if (n == 0)
		{
#   ifndef FNDELAY
#	ifdef O_NDELAY
			if ((fp->flag & (IOEDIT|IOSLOW)) &&
			    isatty(fno) &&
			    (n = (fcntl(fno,F_GETFL,0))&O_NDELAY))
			{
				n &= ~O_NDELAY;
				fcntl(fno, F_SETFL, n);
				goto retry;
			}
#	endif /* O_NDELAY */
#   endif /* !FNDELAY */
			fp->flag |= IOEOF;
			if (fp->flag & IORW)
				fp->flag &= ~IOREAD;
		}
		else
		{
#   ifdef O_NONBLOCK
			if(errno == EAGAIN)
			{
				n = fcntl(fno,F_GETFL,0);
				n &= ~O_NONBLOCK;
				fcntl(fno, F_SETFL, n);
				goto retry;
			}
#   endif /* O_NONBLOCK */
			fp->flag |= IOERR;
		}
		return(EOF);
	}
	if(!(fp->flag&IOSLOW))
		fp->fseek += n;
	/* this is needed to handle read -s */
	if((st.states&RWAIT) && fno!=hist_ptr->fixfd)
	{
		n = output;
		p_setout(hist_ptr->fixfd);
		p_str(fp->base,0);
		output = n;
	}
	return(*fp->ptr++&STRIP);
}


/*
 * close file stream and reopen for reading and writing
 */

void io_fclose(fd)
register int fd;
{
	register struct fileblk *fp = io_get_ftbl(fd);
	/* reposition seek pointer if necessary */
	if(fp && !(fp->flag&IOREAD))
		p_flush();
	else if(fp)
	{
		register int count;
		if(!(st.states&FORKED) && (count=fp->last-fp->ptr))
			lseek(filenum(fp),-((off_t)count),SEEK_CUR);
		*(fp->last = fp->ptr = fp->base) = 0;
	}
	if(fd==COTPIPE)
		sh.cpid = 0;
	close(fd);
	if(fp == &io_stdin)
		iofree = fp;
	else if(fp && (fp->flag&IOFREE))
		free((char*)fp);
	io_set_ftbl(fd, NULL);
}

/*
 * Open a file for reading
 * On failure, print message.
 */

int io_open(path, flags, mode)
register const char	*path;
int			flags;
int			mode;
{
	register int		fd;
	sigset_t		omask;
#ifdef SOCKET
	struct sockaddr_in	addr;
	if (strmatch(path, "/dev/@(tcp|udp)/*/*") && str2inet(path + 9, &addr))
	{
		if ((fd = socket(AF_INET, path[5] == 't' ? SOCK_STREAM : SOCK_DGRAM, 0)) >= 0 && connect(fd, (struct sockaddr *)&addr, sizeof(addr)))
		{
			close(fd);
			fd = -1;
		}
	}
	else
#endif /* SOCKET */
	{
		(void) sigprocmask(SIG_BLOCK, &childmask, &omask);
		fd = open(path, flags, mode);
		(void) sigprocmask(SIG_SETMASK, &omask, NULL);
	}
	if (fd < 0)
	{
		close(fd);
		sh_fail(path, (flags & (O_WRONLY|O_RDWR|O_APPEND|O_CREAT)) ? e_create : e_open);
	}
	return(fd);
}

int io_fopen(name)
register const char *name;
{
	return (io_open(name,O_RDONLY,0));
}

int
io_safe_fopen(const char *name)
{
	int fd;

	fd = io_open(name,O_RDONLY,0);
	fd = io_movefd(fd);
	return (fd);
}

/*
 * set up an I/O stream that will cause reading from a string
 */

void	io_sopen(s)
register char *s;
{
	register struct fileblk *fp;
	(fp=st.standin)->fdes = input = F_STRING;
	fp->flag = IOREAD;
	fp->base = fp->ptr = s;
	fp->flin = 1;
	fp->ftype=F_ISSTRING;
	fp->flag|=(s==0?IOEOF:0);
}

/*
 * move open file descriptor to a number > 2
 */
int
io_movefd(int fdold)
{
	int fdnew;

	/*
	 * We move fd number for the following two cases:
	 * 1) fdold < USERIO
	 *	Those fds can be duped from command line(ie 9>&2),
	 *	so we try to avoid use of those numbers.
	 * 2) MAXFILES < fdold <= NFILE:
	 *	These are reserved fds can be used to open history
	 *	files or pipes.
	 */
	if ((fdold >= USERIO && fdold <= MAXFILES) || fdold > NFILE ||
	    fdold < 0) {
		return (fdold);
	}

	/* fdold is in either [0-USERIO] or [MAXFILES, NFILE] */
	fdnew = fcntl(fdold, F_DUPFD,
		((fdold < USERIO) ? USERIO : NFILE + 1));

	/* fd might have been remapped to the higher reserved area */
	if (fdnew > MAXFILES && fdnew <= NFILE) {
		(void) close(fdnew);
		fdnew = fcntl(fdold, F_DUPFD, NFILE + 1);
	}

	/* fd allocation may have failed */
	if (fdnew < 0) {
		/*
		 * fd limit is too low or we've opened too many files.
		 * use USERIO area rather than returning -1.
		 */
		if (fdold > 2 && fdold < USERIO)
			return (fdold);
		fdnew = fcntl(fdold, F_DUPFD, 3);
		/*
		 * we've already failed to dup the fd to larger than
		 * NFILE + 1. Therefore, if fdnew is larger than MAXFILES,
		 * it will be in the higher reserved area, which is not
		 * allowed.
		 */
		if (fdnew < 0 || fdnew > MAXFILES) {
			if (fdnew > 0)
				(void) close(fdnew);
			/* we have no room in USERIO area */
			sh_fail((const char *)gettext(e_flimit), NIL);
		}
	}

	(void) close(fdold);
	return (fdnew);
}

/*
 * create a pipe and print message on failure
 */

void	io_popen(pv)
register int	pv[];
{
	if(pipe(pv)<0 || pv[INPIPE]<0 || pv[OTPIPE]<0)
		sh_fail((const char *)gettext(e_pipe), NIL);
	pv[INPIPE] = io_movefd(pv[INPIPE]);
	pv[OTPIPE] = io_movefd(pv[OTPIPE]);
	io_set_ftbl(pv[INPIPE], NULL);
	io_set_ftbl(pv[OTPIPE], NULL);
}

/*
 * close a pipe
 */

void io_pclose(pv)
register int pv[];
{
	if(pv[INPIPE]>=2)
		io_fclose(pv[INPIPE]);
	if(pv[OTPIPE]>=2)
		io_fclose(pv[OTPIPE]);
	pv[INPIPE] = pv[OTPIPE] = -1;
}
/*
 * io_sync - flushes output buffer and positions stdin if necessary
 */

void io_sync()
{
	register struct fileblk *fp = io_get_ftbl(0);
	register int count;
	p_setout(ERRIO);
	p_flush();
	/* position back the read-ahead characters */
	if(fp)
	{
		/*@ assert fp->base!=0; @*/
		if(count=fp->last-fp->ptr)
			lseek(filenum(fp),-((off_t)count),SEEK_CUR);
		fp->ptr = fp->last = fp->base;
		*fp->ptr = 0;
	}
}

/*
 * create a link to iodoc for child process to use
 */

void io_linkdoc(i)
register struct ionod	*i;
{
	register int maxtry;
	while(i)
	{
		/* link to a temporary file name */
		maxtry = MAXTRY;
		do
		{
			sh_copy(sh_itos(++serial),temp_suffix);
		}
		while((link(i->ioname,io_tmpname))< 0 && maxtry--);
		if(maxtry<=0)
			sh_fail(io_tmpname,e_create);
		if(i->iolink)
			free(i->iolink);
		i->iolink = sh_heap(io_tmpname);
		i = i->iolst;
	}
}


/*
 * rename the file with the link name of the parent
 */

void	io_swapdoc(i)
register struct ionod	*i;
{
	while(i)
	{
		i->ioname = i->iolink;
		i->iolink = 0;
		i = i->iolst;
	}
}

static  int
io_heredoc_copy(struct ionod *i)
{
	int	of, r;
	size_t	len, rd;
	char	tmp_name[PATH_MAX];
	char	inbuff[IOBSIZE];

	of = io_mktmp(tmp_name);
	(void) unlink(tmp_name);
	if (i->iofile & IOSTRG) {
		len = strlen(i->ioname);
		(void) write(of, i->ioname, len);
	} else {
		/*
		 * We use pread in order to not bother parent who may still put
		 * more heredocs.
		 */
		for (len = 0; len < i->ioheresz; len += r) {
			rd = i->ioheresz - len;
			rd = (rd > IOBSIZE) ? IOBSIZE : rd;
			r = pread(sh.herefd, inbuff, rd, i->iohereoff + len);
			if (r <= 0)
				break;
			(void) write(of, inbuff, r);
		}
	}
	(void) lseek(of, 0, SEEK_SET);
	return (of);
}

/*
 * I/O redirection
 * flag = 0 if files are to be restored
 * flag = 2 if files are to be closed on exec
 * flag = 3 when called from $( < ...), just open file and return
 */

int
io_redirect(struct ionod *iop, int flag)
{
	register char *ion;
	register int 	iof;
	register int	fd;
	char	*ion_save = NULL;
	int o_mode;
	static char io_op[5];
	int fn;
	int mark = MARK_CLOEXEC;
	int indx = sh.topfd;
	int traceon;
	if(flag==2)
		mark = 0;
	if(iop)
		traceon = sh_trace((char**)0,0);
	for(;iop;iop=iop->ionxt)
	{
		iof=iop->iofile;
		if(flag==0)
		{
			/* save current file descriptor */
			io_save(iof&IOUFD,indx);
		}
		ion=iop->ioname;
		io_op[0] = '0'+(iof&IOUFD);
		if(iof&IOPUT)
		{
			io_op[1] = '>';
			o_mode = O_WRONLY|O_CREAT;
		}
		else
		{
			io_op[1] = '<';
			o_mode = O_RDONLY;
		}
		io_op[2] = 0;
		io_op[3] = 0;
		if(!(iof&IORAW))
			ion=mac_trim(ion, 1);
		if(*ion)
		{
			if(iof&IODOC)
			{
				fd = io_heredoc_copy(iop);
				if(traceon)
				{
					p_setout(ERRIO);
					io_op[2] = '<';
					p_str(io_op,NL);
				}
				if (iof & IOMDOC)
				{
					int nfd = mac_here(fd);
					(void) close(fd);
					fd = nfd;
				}
				ion = 0;
			}
			else if(iof&IOMOV)
			{
				io_op[2] = '&';
				if(ion[1]!=0)
					goto fail;
				fd = (iof&IOUFD);
				fn = *ion;
				if(fn=='-')
				{
					io_fclose(fd);
					continue;
				}
				if(fn == 'p')
				{
					if(iof&IOPUT)
						fn = COTPIPE;
					else
						fn =  CINPIPE;
				}
				else if(isdigit(fn))
					fn -= '0';
				else
					goto fail;
				if((fd=dup(fn))<0)
					goto fail;
				if(fn==COTPIPE)
					io_fclose(COTPIPE);
				else if(fn==CINPIPE)
					io_pclose(sh.cpipe);
			}
			else if(iof&IORDW)
			{
				io_op[2] = '>';
				o_mode = O_RDWR|O_CREAT;
				goto openit;
			}
			else if((iof&IOPUT)==0)
				fd=io_open(ion,O_RDONLY,0);
			else if(is_option(RSHFLG))
				sh_fail(ion,e_restricted);
			else
			{
				if(iof&IOAPP)
				{
					io_op[2] = '>';
					o_mode |= O_APPEND;
				}
				else
				{
					o_mode |= O_TRUNC;
					if(iof&IOCLOB)
						io_op[2] = '|';
					else if(is_option(NOCLOB))
					{
						struct stat st;
#ifdef FS_3D
						st.st_rdev=0;
						if(stat(ion,&st)>=0 && S_ISREG(st.st_mode) && (mount(NULL,NULL,10)<0 || st.st_rdev==0))
#else
						if(stat(ion,&st)>=0 && S_ISREG(st.st_mode))
#endif /* FS_3D */
							sh_fail(ion,e_fexists);
					}
				}
			openit:
				fd = io_open(ion,o_mode,RW_ALL);
			}
			if(traceon && ion)
			{
				p_setout(ERRIO);
				p_str(io_op,' ');
				p_str(ion,iop->ionxt?SP:NL);
			}
			if(flag==3) {
				if (ion_save)
					xfree(ion_save);
				fd = io_movefd(fd);
				return(fd);
			}
			io_renumber(fd,(iof&IOUFD)|mark);
		}
	}
	if (ion_save)
		xfree(ion_save);
	return(indx);
fail:
	sh_fail(ion,e_file);
	/* NOTREACHED */
}

/*
 * copy file fd into a save place
 * fd should not exceed IOUFD.
 */

void io_save(fd,oldtop)
register int fd;
{
	register int	f = sh.topfd;

	if (f > fdmap_size)
		f = fdmap_size;

	/* see if already saved, only save once */
	while(f > oldtop)
	{
		if((fdmap[--f].org_fd)>>1 == fd)
			return;
	}
	if(fd==output)
		p_flush();
	else if(fd==0)
		io_sync();
	f = fcntl(fd, F_DUPFD, USERIO);
	if (f > MAXFILES && f <= NFILE) {
		/* reserved area. Don't use this number */
		(void) close(f);
		f = fcntl(fd, F_DUPFD, NFILE + 1);
	}
	/*
	 * If fd is not an opened fd, f < 0 is both valid and expected.
	 * However, if fd is an opened fd, f < 0 indicates that we failed
	 * to dup fd, which is invalid.
	 */
	if (f < 0 && fcntl(fd, F_GETFD) != -1)
		sh_fail((const char *)gettext(e_flimit), NIL);
	if(f >= 0)
	{
		io_set_ftbl(f, io_get_ftbl(fd));
		io_set_ftbl(fd, NULL);
		fd <<= 1;
		/* make sure saved file close-on-exec */
		if(fcntl(f,F_GETFD,0)==0)
		{
			/* the low bit on fd set to restore close-on-exec */
			fd |=  1;
			fcntl(f,F_SETFD,1);
		}
	}
	while (sh.topfd >= fdmap_size) {
		struct filesave *sp;

		sp = realloc(fdmap, (fdmap_size + NFILE) * sizeof (*sp));
		if (sp == NULL) {
			sh_fail((const char *)gettext(e_space), NIL);
			exit(1);
		}
		(void) memset(sp + fdmap_size, 0, NFILE * sizeof (*sp));
		fdmap = sp;
		fdmap_size += NFILE;
	}
	fdmap[sh.topfd].org_fd = fd;
	fdmap[sh.topfd++].dup_fd = f;
	return;
}


/*
 *  restore saved file descriptors from <last> on
 */

void	io_restore(last)
int	last;
{
	register int 	i, top;
	register int	dupfd;
	register int	fd;
	int flag;

	if ((top = sh.topfd) > fdmap_size) 
		top = fdmap_size;

	for (i = top - 1; i >= last; i--)
	{
		fd = fdmap[i].org_fd;
		if ((dupfd = fdmap[i].dup_fd) > 0)
		{
			flag = fd&1;
			fd >>= 1;
			io_renumber(dupfd, fd);
			if(fd==0)
				st.ioset = 0;
			/* turn off close-on-exec if flag is set */
			if(flag)
				fcntl(fd,F_SETFD,0);
		}
		else if (fd >= 0)
			io_fclose(fd);
	}
	if (last < sh.topfd)
		sh.topfd = last;
}


/*
 * This routine returns 1 if fd corresponds to a pipe, 0 otherwise.
 * PIPE_ERR ( which is ESPIPE ) is returned for pipes AND sockets.
 */

int ispipe(fno)
register int fno;
{
#ifdef PIPE_ERR
	if(lseek(fno,(off_t)0,SEEK_CUR)>=0)
		return(0);
	if(errno==PIPE_ERR)
		return(!tty_check(fno));
	else
		return(0);
#else
	return(0);
#endif /* PIPE_ERR */
}




/*
 * This routine returns the next input character but strips shell
 * line continuations and issues prompts at end of line
 * Otherwise this routine is the same as io_readc()
 */
/* CSI assumption1(ascii) made here. See csi.h. */
wchar_t
io_nextc()
{
	wchar_t		c, d;

retry:
	nosave = 0;
	if ((c = io_readc()) == ESCAPE)  {
		struct peekn savepeekn = st.peekn;
		nosave = 1;
		/*
		 * call io_readc() instead of io_readbyte() in order
		 * to track the input which is done in io_readc().
		 */
		if ((d = io_readc()) == NL) {
			sh_prompt(0);
			io_record_ungetc(2);
			goto retry;
		}
		io_unreadc(d);
	}
	nosave = 0;
	return (c);
}

int io_intr(fp)
register struct fileblk *fp;
{
	io_clearerr(fp);
	if(sh.trapnote&SIGSET)
	{
		p_setout(ERRIO);
		newline();
		sh_exit(SIGFAIL);
	}
	if(fp->flag&IOSLOW)
	{
#ifdef JOBS
		job_wait((pid_t)0);
#endif /* JOBS */
		if(sh.trapnote&TRAPSET)
		{
			int savtop = staktell();
			off_t savptr = staksave();
			(void) stakfreeze(0);
			p_setout(ERRIO);
			newline();
			fp = st.standin;
			sh_chktrap();
			io_clear(fp);
			stakrestore(savptr, savtop);
			return(0);
		}
	}
	return(-1);
}

#ifndef SIG_NORESTART
/*
 * This routine is for systems that automatically restarts read
 */

static VOID interrupt()
{
	if(sh.trapnote&(SIGSET|TRAPSET))
	{
		st.intfn = 0;
		tty_cooked(ERRIO);
		LONGJMP(readerr,1);
	}
}
#endif	/* SIG_NORESTART */

void
io_unreadc(c)
wchar_t	c;
{
	int	l;
	char	mbbuf[MB_LEN_MAX + 1];

	l = sh_wctomb(mbbuf, c);
	while (--l >= 0)
		st.peekn.buf[st.peekn.cnt++] = mbbuf[l] | MARK;

	io_record_ungetc(1);
}

/*
 * read a byte
 */

static int
io_readbyte(void)
{
	register int c;
	register struct fileblk	*fp;
	int maxtry;
	/* Take a byte from peekn buffer if not empty.
	   Note that io_nextc() has a code depends on this
	   implementation. */
	if (st.peekn.cnt > 0) {
		c = st.peekn.buf[--st.peekn.cnt] & ~MARK;
		return (c);
	}
	maxtry = MAXTRY;
	fp = st.standin;
retry:
	c = (*fp->ptr++)&STRIP;
retry2:
	if(c==0)
	{
		switch(fp->ftype)
		{
		case F_ISALIAS:
			if(fp->flast == 0)
			{
			popit:
				io_pop(1);
				return(io_readbyte());
			}
			/* does it end in whitespace? */
			c = *(--fp->ptr-1);
			if(sh_isblank(c))
				sh.wdset |= CAN_ALIAS;
			else if(c==ESCAPE && fp->flast==NL)
				goto popit;
			if(fp->flast)
			{
				c = (fp->flast&~MARK);
				fp->flast = 0;
				if(c)
					goto retry2;
				io_pop(1);
			}
			return(ENDOF);

		case F_ISEVAL:
		{
			static char space[2] = " ";
			io_sopen(space);
			fp->ftype = F_ISEVAL2;
			goto retry;
		}

		case F_ISEVAL2:
			io_sopen(*fp->feval++);
			if(*fp->feval)
				fp->ftype = F_ISEVAL;
			goto retry;

		case F_ISSTRING:
			io_sopen(NULLSTR);
			fp->flag |= IOEOF;
			break;

		case F_ISFILE:
			/* check for end-of-buffer */
			if(fp->ptr>fp->last)
			{
				if((c = io_readbuff(fp)) != EOF)
					goto retry2;
				c = ENDOF;
				if(!fiseof(fp))
				{
					if(io_intr(fp)<0)
					{
						if(--maxtry > 0)
							goto retry;
						else
							fp->flag |= IOERR;
					}
				}
				break;
			}
			/* treat a zero byte as eof for TMPIO */
			if(filenum(fp) == TMPIO)
			{
				lseek(TMPIO,(off_t)0,SEEK_SET);
				io_set_ftbl(TMPIO, NULL);
				break;
			}
			/* assume foreign binary and don't execute */
			if(sh.readscript)
			{
				register char *cp = st.cmdadr;
				st.cmdadr = sh.readscript;
				sh_fail(cp,e_exec);
			}
		default:
			/* skip over null bytes in files */
			goto retry;
		}
	}
	else
	{
		if(c==NL)
		{
			fp->flin++;
			nosave = 0;
		}
		if(!nosave)
		{
			if((st.states&READPR) && (fp->ftype!=F_ISALIAS) &&
				(fp->fstak==0||(st.states&FIXFLG)))
			{
				p_setout(ERRIO);
				p_char(c);
			}
			if((st.states&FIXFLG)&&fp->ftype==F_ISFILE && hist_ptr)
			{
				fp = hist_ptr->fixfp;
				if(fp->ptr >= fp->last)
				{
					int savout = output;
					output = filenum(fp);
					p_flush();
					output = savout;
					hist_ptr->fixflush++;
				}
				*fp->ptr++ = c;
			}
		}
	}
	return(c);
}

wchar_t
io_readc()
{
	wchar_t c;
	wchar_t	w;
	char	mbbuf[MB_LEN_MAX];
	int	i;

	for (i = 1; i <= MB_CUR_MAX; i++) {
		int	b;

		if ((b = io_readbyte()) == EOF) {
			if (i == 1) {
				c = (wchar_t)WEOF;
			} else {
				sh_mbtowc(&c, mbbuf, 1);
				while (--i > 0) {
					st.peekn.buf[st.peekn.cnt++] =
						mbbuf[i] | MARK;
				}
			}
			goto out;
		}
#ifdef	CSI_ASCIIACCEL
		if (i == 1 && isascii(b)) {
			c = ShByteToWChar(b);
			goto out;
		}
#endif	/* CSI_ASCIIACCEL */
		mbbuf[i - 1] = b;
		mbbuf[i] = '\0';

		if (mbtowc(&w, mbbuf, i) >= 0) {
			c = w;
			goto out;
		}
	}

	/* Enough bytes available but cannot be converted to wchar.
	   Buffer must contain illegal byte sequence. */
	i--;	/* reset i to MB_CUR_MAX */
	sh_mbtowc(&c, mbbuf, 1);
	while (--i > 0) {
		st.peekn.buf[st.peekn.cnt++] = mbbuf[i] | MARK;
	}
out:
	io_record_getc(c);
	return (c);
}

/*
 * returns access information on open file <fd>
 * returns -1 for failure, 0 for success
 * <mode> is the same as for access()
 */

int
io_access(int fd, int mode)
{
	int flags;
	struct fileblk *fp;
#ifndef F_GETFL
	struct stat statb;
#endif /* F_GETFL */
	if(mode==X_OK)
		return(-1);
	if(fp=io_get_ftbl(fd))
	{
		if(mode==F_OK)
			return(0);
		if(mode==R_OK && fp->flag&(IORW|IOREAD))
			return(0);
		if(mode==W_OK && fp->flag&(IORW|IOWRT))
			return(0);
		return(-1);
	}
#ifdef F_GETFL
	flags = fcntl(fd,F_GETFL,0);
#else
	flags = fstat(fd,&statb);
#endif /* F_GETFL */
	if(flags < 0)
		return(-1);
#ifdef F_GETFL
	if(mode==R_OK && (flags&O_WRONLY))
		return(-1);
	if(mode==W_OK && !(flags&(O_RDWR|O_WRONLY)))
		return(-1);
#endif /* F_GETFL */
	return(0);
}

#ifdef NOFCNTL
#   ifdef _sys_ioctl_
#	include	<sys/ioctl.h>
#   endif /* _sys_ioctl_ */

int fcntl(f1,type,arg)
register int arg;
{
	struct stat statbuf;
	if(type==F_DUPFD)
	{
		register int fd;
		/* find first non-open file */
		while(arg < NFILE &&  (fstat(arg,&statbuf)>=0))
			arg++;
		if(arg >= NFILE)
			return(-1);
		fd = dup2(f1, arg);
		return(fd);
	   }
#   ifdef FIOCLEX
	else if(type==F_SETFD)
		ioctl(f1, arg?FIOCLEX:FIONCLEX, 0);
#   endif /* FIOCLEX */
	else 
		return(0);
}

#undef open

/*
 * Some brain damaged systems don't have O_CREAT
 */

int myopen(name,flag,mode)
register char *name;
register int flag;
{
	register int fd;
	int created = 0;
	if(flag&O_TRUNC)
	{
	filecreate:
		created++;
		fd = creat(name,(flag&O_CREAT)?mode:0644);
	}
	else
	{
		fd = open(name,flag&(O_RDWR|O_RDONLY|O_WRONLY));
		if(fd<0 && (flag&O_CREAT))
			goto filecreate;
	}
	if(fd<0)
		return(fd);
	if(created&&(flag&O_RDWR))
	{
		close(fd);
		return(myopen(name,flag&~(O_CREAT|O_TRUNC)));
	}
	else if(flag&O_APPEND)
		lseek(fd,(off_t)0,SEEK_END);
	return(fd);
}
#endif	/* _fcntl_ */

#ifdef DBUG
/*
 * dump an io structure using only writes
 */

int_write(n,base)
{
	register char *cp;
	extern char *utos();
	cp = utos(n,base);
	write(2,cp,strlen(cp));
}

io_dump(n)
int n;
{
	struct fileblk *fp = io_get_ftbl(n);
	write(2,"dump ",5);
	int_write(n,10);
	if(fp)
	{
		write(2,":fp=",4);
		int_write(fp,16);
		write(2," base=",6);
		int_write(fp->base,16);
		write(2," ptr=",5);
		int_write(fp->ptr,16);
		write(2," last=",6);
		int_write(fp->last,16);
		write(2," flag=",6);
		int_write(fp->flag,8);
		write(2," *last=",7);
		int_write(*fp->last&0xff,10);
		write(2,"\n",1);
	}
	else
		write(2,":no file structure\n",20);
}
#endif /* DBUG */
	
#ifdef SOCKET
/*
 * convert string to sockaddr_in
 * 0 returned on error
 */

static int
str2inet(s, addr)
register char*		s;
struct sockaddr_in*	addr;
{
	register int	n;
	register int	c;
	register int	v;
	unsigned long	a;
	unsigned short	p;

	a = 0;
	n = 0;
	for (;;)
	{
		v = 0;
		while ((c = *s++) >= '0' && c <= '9')
			v = v * 10 + c - '0';
		if (++n <= 4) a = (a << 8) | (v & 0xff);
		else
		{
			if (c) return(0);
			p = v;
			break;
		}
		if (c != '.' && c != '/') return(0);
	}
	memset(addr, 0, sizeof(*addr));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = htonl(a);
	addr->sin_port = htons(p);
	return(1);
}
#endif /* SOCKET */

/*
 * io_ftable should have size NFILE; I don't see any other reason it
 * requires + USERIO. However, keep this size to ensure we don't fail
 * to open under memory pressure.
 */
static struct fileblk *io_ftable[NFILE+USERIO] = { 0, &io_stdout, &io_stdout};
static struct fileblk **io_extftable = NULL;
static size_t io_extftable_size = 0;

#define	IO_FTABLE_SIZE	(sizeof (io_ftable)/ sizeof (io_ftable[0]))

struct fileblk *
io_get_ftbl(int fd)
{
	if (fd < 0)
		return (NULL);

	if (fd < IO_FTABLE_SIZE)
		return (io_ftable[fd]);

	fd -= IO_FTABLE_SIZE;
	if (fd < io_extftable_size)
		return (io_extftable[fd]);
	else
		return (NULL);
}

void
io_set_ftbl(int fd, struct fileblk *fp)
{
	struct fileblk **fpp;

	if (fd < 0)
		return;

	if (fd < IO_FTABLE_SIZE) {
		io_ftable[fd] = fp;
		return;
	}
	fd -= IO_FTABLE_SIZE;
	while (fd >= io_extftable_size) {
		fpp = realloc(io_extftable,
		    (io_extftable_size + NFILE) * sizeof (struct fileblk *));
		if (fpp == NULL) {
			sh_fail((const char *)gettext(e_space), NIL);
			exit(1);
		}
		(void) memset(fpp + io_extftable_size, 0,
				sizeof (struct fileblk *) * NFILE);
		io_extftable = fpp;
		io_extftable_size += NFILE;
	}
	io_extftable[fd] = fp;
}

#ifdef VFORK

/*
 * just thought we might turn on VFORK in the future. The code below
 * hasn't been tested as our ksh cannot be compiled with VFORK.
*/
struct fileblk **
io_ftbl_save(int *nentp)
{
	struct fileblk *np;
	size_t nent;

	nent = IO_FTABLE_SIZE;
	nent += io_extftable_size;

	np = malloc(nent * sizeof (struct fileblk *));
	if (np == NULL) {
		sh_fail((const char *)gettext(e_space), NIL);
		exit(1);
	}

	(void) memcpy(np, io_ftable, sizeof (io_ftable));
	(void) memcpy(np + IO_FTABLE_SIZE, io_extftable,
		io_extftable_size * sizeof (strcut fileblk *));
	*nentp = nent;
	return (np);
}

void
io_ftbl_restore(struct fileblk **np, int nent)
{
	int fd;

	(void) memcpy(io_ftable, np, sizeof (io_ftable));
	fd = IO_FTABLE_SIZE;
	while (fd < nent) {
		io_set_ftbl(fd, np[fd]);
		fd++;
	}
	free(np);
	/* clear the rest */
	for (; nent < io_extftable_size; nent++)
		io_extftable[nent] = NULL;
}
#endif /* VFORK */

#define	IOREC_INCR	128

int
io_record_start(void)
{
	int	ostart;

	if (sh.ior.buf != NULL) {
		sh.ior.rcnt++;
		ostart = sh.ior.start;
		sh.ior.start = sh.ior.index;
		return (ostart);
	}
	(void) memset(&sh.ior, 0, sizeof (sh.ior));
	sh.ior.bsize = IOREC_INCR;
	if ((sh.ior.buf = malloc(sh.ior.bsize * sizeof (wchar_t))) == NULL)
		sh_fail((const char *)gettext(e_space), NIL);
	return (0);
}

void
io_record_copy(int staktype)
{
	int	i;

	for (i = sh.ior.start; i < sh.ior.index; i++) {
		if (staktype == UseWStak)
			wstakputwc(sh.ior.buf[i]);
		else
			stakputwc(sh.ior.buf[i]);
	}
}

void
io_record_end(int sstart)
{
	if (sh.ior.rcnt != 0) {
		sh.ior.start = sstart;
		sh.ior.rcnt--;
		return;
	}
	free(sh.ior.buf);
	sh.ior.buf = NULL;
}

static void
io_record_getc(wchar_t c)
{
	wchar_t	*np;

	if (sh.ior.buf == NULL || sh.ior.push)
		return;

	if (sh.ior.index == sh.ior.bsize) {
		sh.ior.bsize += IOREC_INCR;
		np = realloc(sh.ior.buf, sh.ior.bsize * sizeof (wchar_t));
		if (np == NULL)
			sh_fail((const char *)gettext(e_space), NIL);
		sh.ior.buf = np;
	}
	sh.ior.buf[sh.ior.index++] = c;
}

static void
io_record_ungetc(int n)
{
	if (sh.ior.buf == NULL || sh.ior.index == 0 || sh.ior.push)
		return;
	if (sh.ior.index >= n)
		sh.ior.index -= n;
	else
		sh.ior.index = 0;
}

static void
io_record_push(void)
{
	if (sh.ior.buf != NULL)
		sh.ior.push++;
}

static void
io_record_pop(void)
{
	if (sh.ior.buf != NULL)
		sh.ior.push--;
}
