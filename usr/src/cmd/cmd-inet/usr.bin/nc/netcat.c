/* $OpenBSD: netcat.c,v 1.89 2007/02/20 14:11:17 jmc Exp $ */
/*
 * Copyright (c) 2001 Eric Jackson <ericj@monkey.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Re-written nc(1) for OpenBSD. Original implementation by
 * *Hobbit* <hobbit@avian.org>.
 */

/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Portions Copyright 2008 Erik Trauschke
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <arpa/telnet.h>

#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <termios.h>
#include <locale.h>
#include <libintl.h>

#include "atomicio.h"
#include "strtonum.h"

#ifdef __lint
#define	gettext(x)	x
#endif

#ifndef	SUN_LEN
#define	SUN_LEN(su) \
	(sizeof (*(su)) - sizeof ((su)->sun_path) + strlen((su)->sun_path))
#endif

#define	PORT_MIN	1
#define	PORT_MAX	65535
#define	PORT_MAX_LEN	6
#define	PLIST_SZ	32	/* initial capacity of the portlist */

#define	bool_t	boolean_t

/* Command Line Options */
bool_t	bflag;		/* buffer length set */
int	dflag;		/* detached, no stdin */
char	*eflag;		/* execute external program */
bool_t	Eflag;		/* exclusive bind */
bool_t	Fflag;		/* do not close network upon EOF on stdin */
int	Iflag;		/* size of socket receive buffer */
bool_t	iflag;		/* Interval Flag */
bool_t	kflag;		/* More than one connect */
bool_t	lflag;		/* Bind to local port */
char	*Lflag;		/* socket data linger settings */
int	mflag = -1;	/* Quit after receiving number of bytes */
bool_t	nflag;		/* Don't do name lookup */
int	Oflag;		/* size of socket send buffer */
char	*Pflag;		/* Proxy username */
char	*pflag;		/* Localport flag */
int	qflag;		/* timeout after EOF on stdin */
char	*Rflag;		/* redirect specification */
bool_t	rflag;		/* Random ports flag */
char	*sflag;		/* Source Address */
bool_t	tflag;		/* Telnet Emulation */
bool_t	uflag;		/* UDP - Default to TCP */
bool_t	vflag;		/* Verbosity */
bool_t	xflag;		/* Socks proxy */
bool_t	Xflag;		/* indicator of Socks version set */
bool_t	zflag;		/* Port Scan Flag */
bool_t	Dflag;		/* sodebug */
char	*Tflag;		/* IP Type of Service / Traffic Class */
bool_t	Zflag;		/* bind in all zones */

int	timeout = -1;
int	family = AF_UNSPEC;
size_t	plen = 1024;	/* default buffer length in bytes */
struct	timespec pause_tv;
struct	linger lng;
struct	termios orig_termios;

/*
 * portlist structure
 * Used to store a list of ports given by the user and maintaining
 * information about the number of ports stored.
 */
struct {
	uint16_t *list; /* list containing the ports */
	uint_t listsize;   /* capacity of the list (number of entries) */
	uint_t numports;   /* number of ports in the list */
} ports;

void	atelnet(int, unsigned char *, unsigned int);
int	build_ports(char *);
void	help(void);
int	local_listen(char *, char *, struct addrinfo);
void	readwrite(int, int, int);
int	remote_connect(const char *, const char *, struct addrinfo);
int	socks_connect(const char *, const char *,
	    const char *, const char *, struct addrinfo, int, const char *);
int	udptest(int, char *, size_t, char *, size_t);
int	unix_connect(char *);
int	unix_listen(char *);
void	set_common_sockopts(int, int);
int	parse_ipdscp(char *, int);
void	usage(int);
char	*print_addr(char *, size_t, struct sockaddr *, int, int);
void	delay(void);

int
main(int argc, char *argv[])
{
	int ch, s, ret, socksv, Nfd;
	char *host, *uport, *rhost, *rport, *rproto, *proxy, *delim;
	char pattern[BUFSIZ] = ""; /* allow empty pattern - empty UDP payload */
	int patlen = 0;
	struct addrinfo hints, rhints;
	struct servent *sv;
	socklen_t len;
	struct sockaddr_storage cliaddr;
	const char *errstr, *proxyhost = "", *proxyport = NULL;
	struct addrinfo proxyhints;
	char port[PORT_MAX_LEN];

	ret = 1;
	s = -1;
	socksv = 5;
	host = NULL;
	uport = NULL;
	rhost = rport = NULL;
	sv = NULL;
	iflag = B_FALSE;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	while ((ch = getopt(argc, argv,
	    "46b:DdEe:FhI:i:klL:m:N:nO:P:p:q:R:rs:T:tUuvw:X:x:zZ")) != -1) {
		switch (ch) {
		case '4':
			family = AF_INET;
			break;
		case '6':
			family = AF_INET6;
			break;
		case 'U':
			family = AF_UNIX;
			break;
		case 'X':
			Xflag = B_TRUE;
			if (strcasecmp(optarg, "connect") == 0)
				socksv = -1; /* HTTP proxy CONNECT */
			else if (strcmp(optarg, "4") == 0)
				socksv = 4; /* SOCKS v.4 */
			else if (strcmp(optarg, "5") == 0)
				socksv = 5; /* SOCKS v.5 */
			else
				errx(1, gettext("unsupported proxy protocol"));
			break;
		case 'b':
			/* read in the value and check the boundaries */
			bflag = B_TRUE;
			plen = strtonum(optarg, 1, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, gettext("buffer size %s: %s"),
				    errstr, optarg);
			break;
		case 'd':
			dflag = 1;
			break;
		case 'D':
			Dflag = B_TRUE;
			break;
		case 'e':
			if ((eflag = strdup(optarg)) == NULL)
				err(1, NULL);
			break;
		case 'E':
			Eflag = B_TRUE;
			break;
		case 'F':
			Fflag = B_TRUE;
			break;
		case 'h':
			help();
			break;
		case 'i':
			pause_tv.tv_nsec = 0;
			if ((delim = strchr(optarg, '.')) != NULL) {
				int i;
				char *endptr;
				char *beginptr = delim + 1;

				/* Parse the fraction-of-second part first. */
				endptr = beginptr + strlen(beginptr);
				/* Strip trailing zeroes. */
				while (*endptr-- == '0')
					*endptr = '\0';
				if (strlen(beginptr) > 9) {
					warnx(gettext("losing precision "
					    "of interval specification"));
					*(beginptr + 9) = '\0';
				}
				pause_tv.tv_nsec = strtonum(beginptr, 0,
				    LONG_MAX, &errstr);
				if (errstr != NULL) {
					errx(1, gettext("nsec interval %s: %s"),
					    errstr, beginptr);
				}
				/* tenths of second to nanosecs conversion */
				for (i = 0; i < 9 - strlen(beginptr); i++)
					pause_tv.tv_nsec *= 10;

				/* Now the part containing the seconds. */
				*delim = '\0';
			}
			pause_tv.tv_sec = strtonum(optarg, 0,
			    LONG_MAX, &errstr);
			if (errstr != NULL) {
				errx(1, gettext("sec interval %s: %s"),
				    errstr, optarg);
			}
			iflag = B_TRUE;
			break;
		case 'I':
			Iflag = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, gettext("interval %s: %s"),
				    errstr, optarg);
			break;
		case 'k':
			kflag = B_TRUE;
			break;
		case 'l':
			lflag = B_TRUE;
			break;
		case 'L':
			if ((Lflag = strdup(optarg)) == NULL)
				err(1, NULL);
			break;
		case 'm':
			mflag = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, gettext("maxbytes %s: %s"),
				    errstr, optarg);
			break;
		case 'n':
			nflag = B_TRUE;
			break;
		case 'N':
			/*
			 * Read the pattern from file. This is to make sure
			 * it's possible to embed \0 bytes in the pattern.
			 */
			if ((Nfd = open(optarg, O_RDONLY)) == -1)
				err(1, gettext("cannot open %s for reading"),
				    optarg);
			if ((patlen = read(Nfd, pattern, sizeof (pattern)))
			    == -1)
				err(1, gettext("read from %s failed"), optarg);
			(void) close(Nfd);
			break;
		case 'O':
			Oflag = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, gettext("interval %s: %s"),
				    errstr, optarg);
			break;
		case 'P':
			Pflag = optarg;
			break;
		case 'p':
			pflag = optarg;
			break;
		case 'q':
			qflag = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, gettext("timeout %s: %s"),
				    errstr, optarg);
			break;
		case 'r':
			rflag = B_TRUE;
			break;
		case 'R':
			if ((Rflag = strdup(optarg)) == NULL)
				err(1, NULL);
			break;
		case 's':
			sflag = optarg;
			break;
		case 't':
			tflag = B_TRUE;
			break;
		case 'T':
			if ((Tflag = strdup(optarg)) == NULL)
				err(1, NULL);
			break;
		case 'u':
			uflag = B_TRUE;
			break;
		case 'v':
			vflag = B_TRUE;
			break;
		case 'w':
			/* Timeout value is internally kept in miliseconds. */
			timeout = strtonum(optarg, 0, INT_MAX / 1000, &errstr);
			if (errstr != NULL)
				errx(1, gettext("timeout %s: %s"),
				    errstr, optarg);
			timeout *= 1000;
			break;
		case 'x':
			xflag = B_TRUE;
			if ((proxy = strdup(optarg)) == NULL)
				err(1, NULL);
			break;
		case 'z':
			zflag = B_TRUE;
			break;
		case 'Z':
			Zflag = B_TRUE;
			break;
		default:
			usage(1);
		}
	}
	argc -= optind;
	argv += optind;

	/* Cruft to make sure options are clean, and used properly. */
	if (argv[0] && !argv[1] && family == AF_UNIX) {
		if (uflag)
			errx(1, gettext("cannot use -u and -U"));
		host = argv[0];
		uport = NULL;
	} else if (argv[0] && !argv[1]) {
		if (!lflag)
			usage(1);
		uport = argv[0];
		host = NULL;
	} else if (argv[0] && argv[1]) {
		if (lflag && pflag)
			usage(1);
		if ((family == AF_UNIX) || (argc != 2))
			usage(1);
		host = argv[0];
		uport = argv[1];
	} else {
		if (!(lflag && pflag))
			usage(1);
	}

	if (argc > 2)
		usage(1);

	if (lflag && sflag)
		errx(1, gettext("cannot use -s and -l"));
	if (lflag && rflag)
		errx(1, gettext("cannot use -r and -l"));
	if (lflag && (timeout >= 0))
		warnx(gettext("-w has no effect with -l"));
	if (lflag && pflag) {
		if (uport)
			host = uport;
		uport = pflag;
	}
	if (lflag && zflag)
		errx(1, gettext("cannot use -z and -l"));
	if (!lflag && kflag)
		errx(1, gettext("must use -l with -k"));
	if (lflag && (Pflag || xflag || Xflag))
		errx(1, gettext("cannot use -l with -P, -X or -x"));

	if ((eflag != NULL) && kflag)
		errx(1, gettext("cannot use -e with -k"));
	if ((eflag != NULL) && iflag)
		errx(1, gettext("cannot use -e with -i"));
	if ((eflag != NULL) && (Rflag != NULL))
		errx(1, gettext("cannot use -e with -R"));

	if ((Rflag != NULL) && !lflag)
		errx(1, gettext("cannot use -R without -l"));

	if ((Eflag) && !lflag)
		errx(1, gettext("cannot use -E without -l"));

	if ((Eflag) && (family == AF_UNIX))
		warnx(gettext("-E has no effect with -U"));

	if ((Rflag != NULL) && zflag)
		errx(1, gettext("cannot use -R with -z"));

	if (Zflag && !lflag)
		errx(1, gettext("cannot use -Z without -l"));

	if ((Iflag > 0 || Oflag > 0) && family == AF_UNIX)
		warnx(gettext("-I/-O have no effect with -U"));

	if ((patlen > 0) && (!zflag || !uflag))
		warnx(gettext("-N does not have any effect without -z and -u"));

	if ((Lflag != NULL) && uflag)
		errx(1, gettext("cannot use -L with -u"));

	if (Lflag != NULL) {
		lng.l_onoff = 1;
		lng.l_linger = strtonum(Lflag, 0, INT_MAX, &errstr);
		if (errstr != NULL)
			errx(1, gettext("linger value %s: %s"),
			    errstr, Lflag);
	}

	if (qflag && !Fflag)
		warnx(gettext("using -q implies -F"));

	/* Initialize addrinfo structure. */
	if (family != AF_UNIX) {
		(void) memset(&hints, 0, sizeof (struct addrinfo));
		hints.ai_family = family;
		hints.ai_socktype = uflag ? SOCK_DGRAM : SOCK_STREAM;
		hints.ai_protocol = uflag ? IPPROTO_UDP : IPPROTO_TCP;
		if (nflag)
			hints.ai_flags |= AI_NUMERICHOST;
	}

	/* Setup redirection. */
	if (Rflag != NULL) {
		if ((rhost = strdup(Rflag)) == NULL)
			err(1, NULL);
		if ((rport = strchr(rhost, '/')) == NULL)
			errx(1, gettext("invalid redirect specification: %s"),
			    optarg);
		*rport = '\0';
		rport++;
		if ((rproto = strchr(rport, '/')) != NULL) {
			*rproto = '\0';
			rproto++;
			(void) memset(&rhints, 0, sizeof (struct addrinfo));
			if (strcmp(rproto, "tcp") == 0) {
				rhints.ai_socktype = SOCK_STREAM;
				rhints.ai_protocol = IPPROTO_TCP;
			} else if (strcmp(rproto, "udp") == 0) {
				rhints.ai_socktype = SOCK_DGRAM;
				rhints.ai_protocol = IPPROTO_UDP;
			} else {
				errx(1,
				    gettext("invalid redirect protocol: %s"),
				    rproto);
			}
			rhints.ai_family = family;
			if (nflag)
				rhints.ai_flags |= AI_NUMERICHOST;
		} else {
			rhints = hints;
		}

		(void) strtonum(rport, PORT_MIN, PORT_MAX, &errstr);
		if (errstr !=  NULL)
			errx(1, gettext("redirect port number %s: %s"),
			    errstr, rport);
	}

	/* Setup proxying. */
	if (xflag) {
		if (uflag)
			errx(1, gettext("no proxy support for UDP mode"));

		if (lflag)
			errx(1, gettext("no proxy support for listen"));

		if (family == AF_UNIX)
			errx(1, gettext("no proxy support for unix sockets"));

		if (family == AF_INET6)
			errx(1, gettext("no proxy support for IPv6"));

		if (sflag)
			errx(1, gettext("no proxy support for "
			    "local source address"));

		if ((proxyhost = strtok(proxy, ":")) == NULL)
			errx(1, gettext("missing port specification"));
		proxyport = strtok(NULL, ":");

		(void) memset(&proxyhints, 0, sizeof (struct addrinfo));
		proxyhints.ai_family = family;
		proxyhints.ai_socktype = SOCK_STREAM;
		proxyhints.ai_protocol = IPPROTO_TCP;
		if (nflag)
			proxyhints.ai_flags |= AI_NUMERICHOST;
	}

	if (lflag) {
		int connfd;
		int wfd = fileno(stdin);
		int lfd = fileno(stdout);
		char *buf = NULL;

		ret = 0;

		if (family == AF_UNIX) {
			if (host == NULL)
				usage(1);
			s = unix_listen(host);
		}

		/*
		 * For UDP allocate the buffer here. Other transports will
		 * use the buffer allocated in readwrite().
		 */
		if (uflag) {
			if ((buf = malloc(plen)) == NULL)
				err(1, gettext("failed to allocate %u bytes"),
				    plen);
		}

		/* Allow only one connection at a time, but stay alive. */
		for (;;) {
			if (family != AF_UNIX) {
				/* check if uport is valid */
				if (strtonum(uport, PORT_MIN, PORT_MAX,
				    &errstr) == 0)
					errx(1, gettext("port number %s: %s"),
					    uport, errstr);
				s = local_listen(host, uport, hints);
			}
			if (s < 0)
				err(1, gettext("could not listen on any "
				    "address/port"));

			/*
			 * For UDP, we will use recvfrom() initially
			 * to wait for a caller, then use the regular
			 * functions to talk to the caller.
			 */
			if (uflag) {
				int rv;
				struct sockaddr_storage z;

				len = sizeof (z);
				rv = recvfrom(s, buf, plen, MSG_PEEK,
				    (struct sockaddr *)&z, &len);
				if (rv < 0)
					err(1, "recvfrom");

				rv = connect(s, (struct sockaddr *)&z, len);
				if (rv < 0)
					err(1, "connect");

				connfd = s;
			} else {
				len = sizeof (cliaddr);
				connfd = accept(s, (struct sockaddr *)&cliaddr,
				    &len);
				if ((connfd != -1) && vflag) {
					char ntop[NI_MAXHOST + NI_MAXSERV];
					(void) fprintf(stderr,
					    gettext("Received connection "
					    "from %s\n"),
					    print_addr(ntop, sizeof (ntop),
					    (struct sockaddr *)&cliaddr, len,
					    nflag ? NI_NUMERICHOST : 0));
				}
			}

			/*
			 * Execute external program, replacing the memory
			 * image of nc and redirecting std{in,out,err}
			 * to the network descriptor.
			 */
			if (eflag != NULL) {
				(void) dup2(connfd, 0);
				(void) dup2(connfd, 1);
				(void) dup2(connfd, 2);
				/* Play on the safe side here. */
				if (connfd > 2)
					(void) close(connfd);
				(void) close(s);
				if (execl(eflag, basename(eflag),
				    NULL) == -1) {
					err(1, "execl");
				}
				/* never reached */
			}

			if (rhost != NULL) {
				int rfd;

				rfd = remote_connect(rhost,
				    rport, rhints);
				if (rfd < 0)
					err(1, gettext("redir connect failed"));

				wfd = rfd;
				lfd = rfd;
			}

			readwrite(wfd, lfd, connfd);
			(void) close(connfd);
			if (family != AF_UNIX)
				(void) close(s);

			if (!kflag)
				break;
		}

		if (buf != NULL)
			free(buf);
	} else if (family == AF_UNIX) {
		ret = 0;

		if ((s = unix_connect(host)) > 0) {
			if (zflag) {
				(void) fprintf(stderr,
				    gettext("Connection to %s succeeded!\n"),
				    host);
				(void) close(s);
				exit(ret);
			}
			readwrite(fileno(stdin), fileno(stdout), s);
			(void) close(s);
		} else {
			ret = 1;
		}

		exit(ret);

	} else {	/* client mode with AF_INET or AF_INET6 */
		int i;
		char *udpbuf = NULL;

		/* Construct the portlist. */
		if ((build_ports(uport) > 1) && (eflag != NULL))
			errx(1, gettext("cannot use -e with portlist"));

		/* For UDP scan we allocate the receiving buffer here. */
		if (uflag && zflag)
			if ((udpbuf = malloc(plen)) == NULL)
				err(1, gettext("failed to allocate %u bytes"),
				    plen);

		/* Cycle through portlist, connecting to each port. */
		for (i = 0; i < ports.numports; i++) {
			(void) snprintf(port, sizeof (port), "%u",
			    ports.list[i]);

			if (s != -1)
				(void) close(s);

			if (xflag)
				s = socks_connect(host, port,
				    proxyhost, proxyport, proxyhints, socksv,
				    Pflag);
			else
				s = remote_connect(host, port, hints);

			if (s < 0) {
				delay();
				continue;
			}

			ret = 0;
			if (vflag || zflag) {
				/* Don't look up port if -n. */
				if (nflag) {
					sv = NULL;
				} else {
					sv = getservbyport(
					    ntohs(ports.list[i]),
					    uflag ? "udp" : "tcp");
				}

				/*
				 * Perform UDP scan. remote_connect()
				 * ensured we are dealing with connected UDP
				 * socket so will get the asynchronous events.
				 */
				if (uflag && zflag) {
					int uret;

					uret = udptest(s, pattern, patlen,
					    udpbuf, plen);
					if (uret == -1) {
						ret = 1;
						(void) fprintf(stderr,
						    gettext("UDP port %s "
						    "at %s is closed\n"),
						    port, host);
						delay();
						continue;
					} else if (uret == 1) {
						(void) fprintf(stderr,
						    gettext("UDP port %s "
						    "at %s is open\n"),
						    port, host);
						delay();
						continue;
					}
				}

				/*
				 * We cannot be sure that UDP port is open
				 * when doing UDP scan and no response comes
				 * back.
				 */
				if (uflag)
					(void) fprintf(stderr,
					    gettext("UDP port %s at %s "
					    "might be open\n"), port, host);
				else
					(void) fprintf(stderr,
					    gettext("Connection to %s %s "
					    "port [%s/%s] succeeded!\n"),
					    host, port, uflag ? "udp" : "tcp",
					    sv ? sv->s_name : "*");

				if (zflag) {
					delay();
					continue;
				}
			}

			/* Execute external program similarly as for lflag. */
			if (eflag != NULL) {
				(void) dup2(s, 0);
				(void) dup2(s, 1);
				(void) dup2(s, 2);
				/* Play on the safe side here. */
				if (s > 2)
					(void) close(s);
				if (execl(eflag, basename(eflag),
				    NULL) == -1) {
					err(1, "execl");
				}
				/* NOT REACHED */
			} else {
				readwrite(fileno(stdin), fileno(stdout), s);
			}
		}
		free(ports.list);
		if (udpbuf != NULL)
			free(udpbuf);
	}

	if (s != -1)
		(void) close(s);

	return (ret);
}

void
delay(void)
{
	if (iflag)
		(void) nanosleep(&pause_tv, (struct timespec *)NULL);
}

/*
 * print IP address and (optionally) a port
 */
char *
print_addr(char *ntop, size_t ntlen, struct sockaddr *addr, int len, int flags)
{
	char port[NI_MAXSERV];
	int e;

	/* print port always as number */
	if ((e = getnameinfo(addr, len, ntop, ntlen,
	    port, sizeof (port), flags|NI_NUMERICSERV)) != 0) {
		return ((char *)gai_strerror(e));
	}

	(void) snprintf(ntop, ntlen, "%s port %s", ntop, port);

	return (ntop);
}

/*
 * unix_connect()
 * Returns a socket connected to a local unix socket. Returns -1 on failure.
 */
int
unix_connect(char *path)
{
	struct sockaddr_un sunaddr;
	int s;

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return (-1);

	(void) memset(&sunaddr, 0, sizeof (struct sockaddr_un));
	sunaddr.sun_family = AF_UNIX;

	if (strlcpy(sunaddr.sun_path, path, sizeof (sunaddr.sun_path)) >=
	    sizeof (sunaddr.sun_path)) {
		(void) close(s);
		errno = ENAMETOOLONG;
		return (-1);
	}
	if (connect(s, (struct sockaddr *)&sunaddr, SUN_LEN(&sunaddr)) < 0) {
		(void) close(s);
		return (-1);
	}
	return (s);
}

/*
 * unix_listen()
 * Create a unix domain socket, and listen on it.
 */
int
unix_listen(char *path)
{
	struct sockaddr_un sunaddr;
	int s;

	/* Create unix domain socket. */
	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return (-1);

	(void) memset(&sunaddr, 0, sizeof (struct sockaddr_un));
	sunaddr.sun_family = AF_UNIX;

	if (strlcpy(sunaddr.sun_path, path, sizeof (sunaddr.sun_path)) >=
	    sizeof (sunaddr.sun_path)) {
		(void) close(s);
		errno = ENAMETOOLONG;
		return (-1);
	}

	if (bind(s, (struct sockaddr *)&sunaddr, SUN_LEN(&sunaddr)) < 0) {
		warn(gettext("bind failed"));
		(void) close(s);
		return (-1);
	}

	if (listen(s, 5) < 0) {
		(void) close(s);
		return (-1);
	}
	return (s);
}

/*
 * Returns a socket connected to a remote host. Properly binds to a local
 * port or source address if needed. Returns -1 on failure.
 */
int
remote_connect(const char *host, const char *port, struct addrinfo hints)
{
	struct addrinfo *res, *res0;
	int s, error;

	if ((error = getaddrinfo(host, port, &hints, &res)))
		errx(1, gettext("remote_connect: getaddrinfo: host %s "
		    "port %s (%s)"), host, port, gai_strerror(error));

	res0 = res;
	do {
		if ((s = socket(res0->ai_family, res0->ai_socktype,
		    res0->ai_protocol)) < 0) {
			warn(gettext("failed to create socket"));
			continue;
		}

		/* Bind to a local port or source address if specified. */
		if (sflag || pflag) {
			struct addrinfo ahints, *ares;

			(void) memset(&ahints, 0, sizeof (struct addrinfo));
			ahints.ai_family = res0->ai_family;
			ahints.ai_socktype = uflag ? SOCK_DGRAM : SOCK_STREAM;
			ahints.ai_protocol = uflag ? IPPROTO_UDP : IPPROTO_TCP;
			ahints.ai_flags = AI_PASSIVE;
			if ((error = getaddrinfo(sflag, pflag, &ahints, &ares)))
				errx(1, gettext("getaddrinfo for source "
				    "address/port: host %s port %s (%s)"),
				    sflag != NULL ? sflag : "N/A",
				    pflag != NULL ? pflag : "N/A",
				    gai_strerror(error));

			if (bind(s, (struct sockaddr *)ares->ai_addr,
			    ares->ai_addrlen) < 0)
				errx(1, gettext("bind failed for source "
				    "address/port: %s"), strerror(errno));
			freeaddrinfo(ares);

			if (vflag && !lflag) {
				if (sflag != NULL)
					(void) fprintf(stderr,
					    gettext("Using source address: "
					    "%s\n"), sflag);
				if (pflag != NULL)
					(void) fprintf(stderr,
					    gettext("Using source port: %s\n"),
					    pflag);
			}
		}

		set_common_sockopts(s, res0->ai_family);

		if (connect(s, res0->ai_addr, res0->ai_addrlen) == 0)
			break;
		else if (vflag) {
			char ntop[NI_MAXHOST + NI_MAXSERV];
			warn(gettext("connect to %s [host %s] (%s) failed"),
			    print_addr(ntop, sizeof (ntop),
			    res0->ai_addr, res0->ai_addrlen, NI_NUMERICHOST),
			    host, uflag ? "udp" : "tcp");
		}

		(void) close(s);
		s = -1;
	} while ((res0 = res0->ai_next) != NULL);

	freeaddrinfo(res);

	return (s);
}

/*
 * Returns a socket listening on a local port, binds to specified source
 * address. Returns -1 on failure.
 */
int
local_listen(char *host, char *port, struct addrinfo hints)
{
	struct addrinfo *res, *res0;
	int s, ret, x = 1, y = 1;
	int error;

	/* Allow nodename to be null. */
	hints.ai_flags |= AI_PASSIVE;

	if ((error = getaddrinfo(host, port, &hints, &res)))
		errx(1, gettext("local_listen: getaddrinfo: host %s "
		    "port %s (%s)"), host == NULL ? "N/A" : host,
		    port, gai_strerror(error));

	res0 = res;
	do {
		if ((s = socket(res0->ai_family, res0->ai_socktype,
		    res0->ai_protocol)) < 0) {
			warn(gettext("failed to create socket"));
			continue;
		}

		ret = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &x, sizeof (x));
		if (ret == -1)
			err(1, gettext("setsockopt failed"));

		set_common_sockopts(s, res0->ai_family);

		if (Eflag) {
			if (setsockopt(s, SOL_SOCKET, SO_EXCLBIND, &x,
			    sizeof (x)) == -1)
				err(1, gettext("failed to set SO_EXCLBIND "
				    "socket option"));
		}
		if (Zflag) {
			if (setsockopt(s, SOL_SOCKET, SO_ALLZONES, &y,
			    sizeof (y)) == -1)
				err(1, gettext("failed to set SO_ALLZONES "
				    "socket option"));
		}
		if (bind(s, (struct sockaddr *)res0->ai_addr,
		    res0->ai_addrlen) == 0)
			break; /* bind was successful so break out */
		else if (vflag)
			warnx(gettext("bind failed for port %s [%s]: %s"),
			    port, res0->ai_family == AF_INET ? "AF_INET" :
			    "AF_INET6", strerror(errno));

		(void) close(s);
		s = -1;
	} while ((res0 = res0->ai_next) != NULL);

	if (!uflag && s != -1) {
		if (listen(s, 1) < 0)
			err(1, gettext("listen"));
	}

	freeaddrinfo(res);

	return (s);
}

/*
 * Put terminal in raw mode. This is necessary if we want to set custom data
 * buffering (plen) otherwise terminal driver will buffer differently.
 */
void
tty_raw(int fd) {
	struct termios raw_termios;

	/* Fill the termios structure with current setting. */
	if (tcgetattr(fd, &orig_termios) < 0) {
		warn(gettext("cannot get terminal properties"));
		return;
	}

	/* structure assignment */
	raw_termios = orig_termios;

	/* Clean some of the input modes. */
	raw_termios.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON);
	/* control modes - 8-bit characters */
	raw_termios.c_cflag |= (CS8);
	/*
	 * Clear local modes. We still want signal characters so that
	 * the program can be interrupted by Ctrl-C. Also, the characters
	 * should be echoed back so we do not suppress ECHO.
	 */
	raw_termios.c_lflag &= ~(ICANON | IEXTEN);

	/* Set minimum number of input characters to buffer length. */
	raw_termios.c_cc[VMIN] = plen;
	raw_termios.c_cc[VTIME] = 0;

	/* Set the terminal properties. */
	if (tcsetattr(fd, TCSAFLUSH, &raw_termios) < 0)
		warn(gettext("cannot put terminal into raw mode"));
}

/*
 * Restore terminal properties to the state before calling tty_raw().
 */
void
tty_restore(int fd)
{
	if (tcsetattr(fd, TCSAFLUSH, &orig_termios) < 0)
		warn(gettext("cannot put terminal into original mode"));
}

void
alarm_handler(int s)
{
	exit(0);
}

/*
 * Loop that polls on the network file descriptor and stdin.
 */
void
readwrite(int wfd, int lfd, int nfd)
{
	struct pollfd pfd[2];
	unsigned char *buf = NULL;
	int n;
	unsigned int total_bytes = 0;

	/* Only set raw mode if -b was used. */
	if (isatty(wfd) && bflag)
		tty_raw(wfd);

	if ((buf = malloc(plen)) == NULL)
		err(1, gettext("failed to allocate %u bytes"), plen);

	/* Setup Network FD */
	pfd[0].fd = nfd;
	pfd[0].events = POLLIN;

	/* Set up STDIN FD. */
	pfd[1].fd = wfd;
	pfd[1].events = POLLIN;

	while (pfd[0].fd != -1) {
		delay();

		if ((n = poll(pfd, 2 - dflag, timeout)) < 0) {
			(void) close(nfd);
			err(1, gettext("Polling Error"));
		}

		/*
		 * This is what makes -w <timeout> to work. If timeout is set
		 * and poll() returns indicating that no descriptor changed
		 * it is time to bail out.
		 */
		if (n == 0)
			goto cleanup;

		if (pfd[0].revents & (POLLIN|POLLHUP)) {
			if ((n = read(nfd, buf, plen)) < 0)
				goto cleanup;
			else if (n == 0) {
				if (Lflag != NULL)
					close(nfd);
				else
					(void) shutdown(nfd, SHUT_RD);
				pfd[0].fd = -1;
				pfd[0].events = 0;
			} else {
				total_bytes += n;
				if (tflag)
					atelnet(nfd, buf, n);
				if (atomicio(vwrite, lfd, buf, n) != n)
					goto cleanup;
				if ((mflag != -1) && (total_bytes >= mflag))
					goto cleanup;
			}
		}

		/*
		 * handle the case of disconnected pipe: after pipe
		 * is closed (indicated by POLLHUP) there may still
		 * be some data lingering (POLLIN). After we read
		 * the data, only POLLHUP remains, read() returns 0
		 * and we are finished.
		 */
		if (!dflag && (pfd[1].revents & (POLLIN|POLLHUP))) {
			if ((n = read(wfd, buf, plen)) < 0)
				goto cleanup;
			else if (n == 0) {
				if (qflag > 0) {
					struct sigaction sa;
					sa.sa_flags = 0;
					sa.sa_handler = alarm_handler;
					(void) sigemptyset(&sa.sa_mask);
					(void) sigaddset(&sa.sa_mask, SIGALRM);
					(void) sigaction(SIGALRM, &sa, NULL);
					if (vflag)
						(void) fprintf(stderr,
						    gettext("EOF on stdin,"
						    " alarm in %d seconds\n"),
						    qflag);
					(void) alarm(qflag);
				}
				if (!Fflag) {
					if (Lflag != NULL) {
						close(nfd);
						pfd[0].fd = -1;
						pfd[0].events = 0;
					} else {
						(void) shutdown(nfd, SHUT_WR);
					}
				}
				pfd[1].fd = -1;
				pfd[1].events = 0;
			} else {
				if (atomicio(vwrite, nfd, buf, n) != n)
					goto cleanup;
			}
		}
	}

cleanup:
	free(buf);

	/* Restore terminal properties. */
	if (isatty(wfd) && bflag)
		tty_restore(wfd);
}

/* Deal with RFC 854 WILL/WONT DO/DONT negotiation. */
void
atelnet(int nfd, unsigned char *buf, unsigned int size)
{
	unsigned char *p, *end;
	unsigned char obuf[4];

	end = buf + size;
	obuf[0] = '\0';

	for (p = buf; p < end; p++) {
		if (*p != IAC)
			break;

		obuf[0] = IAC;
		obuf[1] = 0;
		p++;
		/* refuse all options */
		if ((*p == WILL) || (*p == WONT))
			obuf[1] = DONT;
		if ((*p == DO) || (*p == DONT))
			obuf[1] = WONT;
		if (obuf[1]) {
			p++;
			obuf[2] = *p;
			obuf[3] = '\0';
			if (atomicio(vwrite, nfd, obuf, 3) != 3)
				warn(gettext("Write error!"));
			obuf[0] = '\0';
		}
	}
}

/*
 * Build an array of ports in ports.list[], listing each port that we should
 * try to connect to. Return number of ports added to the list.
 */
int
build_ports(char *p)
{
	const char *errstr;
	const char *token;
	char *n;
	int lo, hi, cp;
	int i;

	/* Set up initial portlist. */
	ports.list = malloc(PLIST_SZ * sizeof (uint16_t));
	if (ports.list == NULL)
		err(1, NULL);
	ports.listsize = PLIST_SZ;
	ports.numports = 0;

	/* Cycle through list of given ports sep. by "," */
	while ((token = strsep(&p, ",")) != NULL) {
		if (*token == '\0')
			errx(1, gettext("Invalid port/portlist format: "
			    "zero length port"));

		/* check if it is a range */
		if ((n = strchr(token, '-')) != NULL)
			*n++ = '\0';

		lo = strtonum(token, PORT_MIN, PORT_MAX, &errstr);
		if (errstr != NULL)
			errx(1, gettext("port number %s: %s"), errstr, token);

		if (n == NULL) {
			hi = lo;
		} else {
			hi = strtonum(n, PORT_MIN, PORT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, gettext("port number %s: %s"),
				    errstr, n);
			if (lo > hi) {
				cp = hi;
				hi = lo;
				lo = cp;
			}
		}

		/*
		 * Grow the portlist if needed.
		 * We double the size and add size of current range
		 * to make sure we don't have to resize that often.
		 */
		if (hi - lo + ports.numports + 1 >= ports.listsize) {
			ports.listsize = ports.listsize * 2 + hi - lo;
			ports.list = realloc(ports.list,
			    ports.listsize * sizeof (uint16_t));
			if (ports.list == NULL)
				err(1, NULL);
		}

		/* Load ports sequentially. */
		for (i = lo; i <= hi; i++)
			ports.list[ports.numports++] = i;
	}

	/* Randomly swap ports. */
	if (rflag) {
		int y;
		uint16_t u;

		if (ports.numports < 2) {
			warnx(gettext("can not swap %d port randomly"),
			    ports.numports);
			return (ports.numports);
		}
		srandom(time(NULL));
		for (i = 0; i < ports.numports; i++) {
			y = random() % (ports.numports - 1);
			u = ports.list[i];
			ports.list[i] = ports.list[y];
			ports.list[y] = u;
		}
	}

	return (ports.numports);
}

/* Dump out character buffer in hex. */
static void
hexout(char *buf, size_t len)
{
	int i;

	for (i = 1; i <= len; i++) {
		(void) fprintf(stderr, "%.2x", (uchar_t)buf[i - 1]);
		if (i % 16 == 0)
			(void) fprintf(stderr, "\n");
		else
			(void) fprintf(stderr, " ");
	}
	if (i % 16 != 0)
		(void) fprintf(stderr, "\n");
}

/*
 * Perform simple UDP scan to provide an estimate whether a port might be open.
 * The socket passed to this function has to be connected UDP socket.
 *
 * Return -1 if the port is closed for real (ICMP Destination Port Unreachable
 * is received), -2 if we failed to send the data, 1 if we received any data,
 * 0 otherwise.
 */
int
udptest(int s, char *pattern, size_t len, char *buf, size_t buflen)
{
	int ret = 0, recvlen;
	struct pollfd pfd[1];

	pfd[0].fd = s;
	pfd[0].events = POLLIN;

	if (send(s, pattern, len, 0) == -1) {
		warn(gettext("udptest send failed"));
		return (-2);
	}

	/* wait for any messages to come */
	if ((poll(pfd, 1, timeout)) < 0) {
		warn(gettext("Polling Error"));
		return (ret);
	}

	if (!(pfd[0].revents & POLLIN))
		return (ret);

	if ((recvlen = recv(s, buf, buflen, 0)) == -1) {
		/* connected IPv4 and IPv6 datagram sockets */
		if (errno == ECONNREFUSED)
			ret = -1;
	} else {
		/* write the received data to stderr */
		if (vflag)
			hexout(buf, recvlen);
		ret = 1;
	}

	return (ret);
}

/*
 * These socket options are settable in both client and server mode.
 */
void
set_common_sockopts(int s, int af)
{
	int x = 1;
	int tos;

	if (Dflag) {
		if (setsockopt(s, SOL_SOCKET, SO_DEBUG, &x, sizeof (x)) == -1)
			err(1, gettext("set SO_DEBUG socket option"));
	}
	if (Tflag != NULL) {
		if ((tos = parse_ipdscp(Tflag, af)) < 0)
			err(1, NULL);

		if (setsockopt(s,
		    af == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
		    af == AF_INET ? IP_TOS : IPV6_TCLASS,
		    &tos, sizeof (tos)) == -1)
			err(1, af == AF_INET ? gettext("set IP ToS") :
			    gettext("set Traffic Class"));
	}
	if (Oflag) {
		if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &Oflag,
		    sizeof (Oflag)) == -1)
			err(1, gettext("set SO_SNDBUF socket option"));
	}
	if (Iflag) {
		if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, &Iflag,
		    sizeof (Iflag)) == -1)
			err(1, gettext("set SO_RCVBUF socket option"));
	}
	if (Lflag != NULL) {
		if (setsockopt(s, SOL_SOCKET, SO_LINGER, &lng,
		    sizeof (lng)) == -1)
			err(1, gettext("set SO_LINGER socket option"));
	}
}

/*
 * IP ToS/Traffic Class header field was superseded by Differentiated
 * Services Field (RFC 2474) but let's allow to set it anyway.
 */
int
parse_ipdscp(char *s, int af)
{
	int tos = -1;

	/* The following ToS definitions are IPv4 specific */
	if (af != AF_INET6) {
		if (strcmp(s, "lowdelay") == 0)
			return (IPTOS_LOWDELAY);
		if (strcmp(s, "throughput") == 0)
			return (IPTOS_THROUGHPUT);
		if (strcmp(s, "reliability") == 0)
			return (IPTOS_RELIABILITY);
	}

	if (sscanf(s, "0x%x", (unsigned int *) &tos) != 1 ||
	    tos < 0 || tos > 0xff)
		errx(1, af == AF_INET ?
		    gettext("invalid IP Type of Service value") :
		    gettext("invalid Traffic Class value"));

	return (tos);
}

void
help(void)
{
	usage(0);
	(void) fprintf(stderr, gettext("\tCommand Summary:\n\
	\t-4		Use IPv4\n\
	\t-6		Use IPv6\n\
	\t-D		Enable the debug socket option\n\
	\t-b bufsize	buffer length to use for IO\n\
	\t-d		Detach from stdin\n\
	\t-E		Use exclusive bind for listening socket\n\
	\t-F		Do not close network after seeing EOF on stdin\n\
	\t-e program	Execute external program\n\
	\t-h		This help text\n\
	\t-I bufsize	Set receive buffer size\n\
	\t-i interval	Delay interval for lines sent, ports scanned\n\
	\t-k		Keep inbound sockets open for multiple connects\n\
	\t-l		Listen mode, for inbound connects\n\
	\t-L timeout	Linger on close timeout\n\
	\t-m bytecnt	Quit after receiving at least bytecnt bytes\n\
	\t-N file\t	file with pattern for UDP scan\n\
	\t-n		Suppress name/port resolutions\n\
	\t-O bufsize	Set send buffer size\n\
	\t-o		Stop I/O after EOF on stdin\n\
	\t-P proxyuser	Username for proxy authentication\n\
	\t-p port\t	Specify local port or listen port\n\
	\t-q timeout	Wait for timeout after seeing EOF and exit\n\
	\t-R rdrspec	Port redirection\n\
	\t-r		Randomize remote ports\n\
	\t-s addr\t	Local source address\n\
	\t-T ToS\t	Set IP Type of Service\n\
	\t-t		Answer TELNET negotiation\n\
	\t-U		Use UNIX domain socket\n\
	\t-u		UDP mode\n\
	\t-v		Verbose\n\
	\t-w secs\t	Timeout for connects and final net reads\n\
	\t-X proto	Proxy protocol: \"4\", \"5\" (SOCKS) or \"connect\"\n\
	\t-x addr[:port]\tSpecify proxy address and port\n\
	\t-z		Zero-I/O mode [used for scanning]\n\
	\t-Z		bind in all zones\n\
	Port numbers can be individuals, ranges (lo-hi; inclusive) and\n\
	combinations of both separated by comma (e.g. 10,22-25,80)\n"));
	exit(1);
}

void
usage(int ret)
{
	(void) fprintf(stderr,
	    gettext("usage: nc [-46DdEFhklnortUuvzZ] [-i interval] "
	    "[-I bufsiz] [-O bufsiz]\n"));
	(void) fprintf(stderr,
	    gettext("\t  [-P proxy_username] [-p port] "
	    "[-R address/port[/proto]]\n"));
	(void) fprintf(stderr, gettext("\t  [-s source_ip_address]"
	    " [-T ToS] [-w timeout] [-L timeout]\n"));
	(void) fprintf(stderr, gettext("\t  [-X proxy_protocol] [-e program]"
	    " [-b bufsize] [-q timeout]\n"));
	(void) fprintf(stderr, gettext("\t  [-x proxy_address[:port]]"
	    " [-m bytes] [-N file] [hostname] [port[s]]\n"));
	if (ret)
		exit(1);
}
