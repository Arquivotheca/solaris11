/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Code to connect to a remote host, and to perform the client side of the
 * login (authentication) dialog.
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */
/*
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "includes.h"
RCSID("$OpenBSD: sshconnect.c,v 1.135 2002/09/19 01:58:18 djm Exp $");

#include <openssl/bn.h>

#include "ssh.h"
#include "xmalloc.h"
#include "rsa.h"
#include "buffer.h"
#include "packet.h"
#include "uidswap.h"
#include "compat.h"
#include "key.h"
#include "sshconnect.h"
#include "hostfile.h"
#include "log.h"
#include "readconf.h"
#include "atomicio.h"
#include "misc.h"
#include "readpass.h"
#include <langinfo.h>
#include "engine.h"
#include "kmf.h"
#include "kmf_openssl.h"

char *client_version_string = NULL;
char *server_version_string = NULL;

/* import */
extern Options options;
extern char *__progname;
extern uid_t original_real_uid;
extern uid_t original_effective_uid;
extern pid_t proxy_command_pid;
extern ENGINE *e;

#ifndef INET6_ADDRSTRLEN		/* for non IPv6 machines */
#define INET6_ADDRSTRLEN 46
#endif

static int show_other_keys(const char *, Key *);

/*
 * Connect to the given ssh server using a proxy command.
 */
static int
ssh_proxy_connect(const char *host, u_short port, const char *proxy_command)
{
	Buffer command;
	const char *cp;
	char *command_string;
	int pin[2], pout[2];
	pid_t pid;
	char strport[NI_MAXSERV];

	/* Convert the port number into a string. */
	snprintf(strport, sizeof strport, "%hu", port);

	/*
	 * Build the final command string in the buffer by making the
	 * appropriate substitutions to the given proxy command.
	 *
	 * Use "exec" to avoid "sh -c" processes on some platforms 
	 * (e.g. Solaris)
	 */
	buffer_init(&command);

#define EXECLEN (sizeof ("exec") - 1)
	for (cp = proxy_command; *cp && isspace(*cp) ; cp++)
		;
	if (strncmp(cp, "exec", EXECLEN) != 0 ||
	    (strlen(cp) >= EXECLEN && !isspace(*(cp + EXECLEN))))
		buffer_append(&command, "exec ", EXECLEN + 1);
#undef EXECLEN

	for (cp = proxy_command; *cp; cp++) {
		if (cp[0] == '%' && cp[1] == '%') {
			buffer_append(&command, "%", 1);
			cp++;
			continue;
		}
		if (cp[0] == '%' && cp[1] == 'h') {
			buffer_append(&command, host, strlen(host));
			cp++;
			continue;
		}
		if (cp[0] == '%' && cp[1] == 'p') {
			buffer_append(&command, strport, strlen(strport));
			cp++;
			continue;
		}
		buffer_append(&command, cp, 1);
	}
	buffer_append(&command, "\0", 1);

	/* Get the final command string. */
	command_string = buffer_ptr(&command);

	/* Create pipes for communicating with the proxy. */
	if (pipe(pin) < 0 || pipe(pout) < 0)
		fatal("Could not create pipes to communicate with the proxy: %.100s",
		    strerror(errno));

	debug("Executing proxy command: %.500s", command_string);

	/* Fork and execute the proxy command. */
	if ((pid = fork()) == 0) {
		char *argv[10];

		/* Child.  Permanently give up superuser privileges. */
		seteuid(original_real_uid);
		setuid(original_real_uid);

		/* Redirect stdin and stdout. */
		close(pin[1]);
		if (pin[0] != 0) {
			if (dup2(pin[0], 0) < 0)
				perror("dup2 stdin");
			close(pin[0]);
		}
		close(pout[0]);
		if (dup2(pout[1], 1) < 0)
			perror("dup2 stdout");
		/* Cannot be 1 because pin allocated two descriptors. */
		close(pout[1]);

		/* Stderr is left as it is so that error messages get
		   printed on the user's terminal. */
		argv[0] = _PATH_BSHELL;
		argv[1] = "-c";
		argv[2] = command_string;
		argv[3] = NULL;

		/* Execute the proxy command.  Note that we gave up any
		   extra privileges above. */
		execv(argv[0], argv);
		perror(argv[0]);
		exit(1);
	}
	/* Parent. */
	if (pid < 0)
		fatal("fork failed: %.100s", strerror(errno));
	else
		proxy_command_pid = pid; /* save pid to clean up later */

	/* Close child side of the descriptors. */
	close(pin[0]);
	close(pout[1]);

	/* Free the command name. */
	buffer_free(&command);

	/* Set the connection file descriptors. */
	packet_set_connection(pout[0], pin[1]);

	/* Indicate OK return */
	return 0;
}

/*
 * Creates a (possibly privileged) socket for use as the ssh connection.
 */
static int
ssh_create_socket(int privileged, int family)
{
	int sock, gaierr;
	struct addrinfo hints, *res;

	/*
	 * If we are running as root and want to connect to a privileged
	 * port, bind our own socket to a privileged port.
	 */
	if (privileged) {
		int p = IPPORT_RESERVED - 1;
		PRIV_START;
		sock = rresvport_af(&p, family);
		PRIV_END;
		if (sock < 0)
			error("rresvport: af=%d %.100s", family, strerror(errno));
		else
			debug("Allocated local port %d.", p);
		return sock;
	}
	sock = socket(family, SOCK_STREAM, 0);
	if (sock < 0)
		error("socket: %.100s", strerror(errno));

	/* Bind the socket to an alternative local IP address */
	if (options.bind_address == NULL)
		return sock;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	gaierr = getaddrinfo(options.bind_address, "0", &hints, &res);
	if (gaierr) {
		error("getaddrinfo: %s: %s", options.bind_address,
		    gai_strerror(gaierr));
		close(sock);
		return -1;
	}
	if (bind(sock, res->ai_addr, res->ai_addrlen) < 0) {
		error("bind: %s: %s", options.bind_address, strerror(errno));
		close(sock);
		freeaddrinfo(res);
		return -1;
	}
	freeaddrinfo(res);
	return sock;
}

/*
 * Connect with timeout. Implements ConnectTimeout option.
 * Note: Caller processed errno from this function. It is important
 *       to protect it e.g. when debug is called.
 */
static int
timeout_connect(int sockfd, const struct sockaddr *serv_addr,
    socklen_t addrlen, int timeout)
{
	fd_set *fdset;
	struct timeval tv;
	socklen_t optlen;
	int optval, rc, olderrno, result = -1;

	if (timeout <= 0)
		return (connect(sockfd, serv_addr, addrlen));

	debug("timeout_connect: timeout %isec", timeout);
	set_nonblock(sockfd);
	rc = connect(sockfd, serv_addr, addrlen);
	if (rc == 0) {
		unset_nonblock(sockfd);
		return (0);
	}
	if (errno != EINPROGRESS)
		return (-1);

	fdset = (fd_set *)xcalloc(howmany(sockfd + 1, NFDBITS),
	    sizeof(fd_mask));
	FD_SET(sockfd, fdset);
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	for (;;) {
		rc = select(sockfd + 1, NULL, fdset, NULL, &tv);
		if (rc != -1 || errno != EINTR)
			break;
	}

	switch (rc) {
	case 0:
		/* Timed out */
		errno = ETIMEDOUT;
		break;
	case -1:
		/* Select error */
		olderrno = errno;
		debug("select: %s", strerror(errno));
		errno = olderrno;
		break;
	case 1:
		/* Completed or failed */
		optval = 0;
		optlen = sizeof(optval);
		if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval,
		    &optlen) == -1) {
			olderrno = errno;
			debug("getsockopt: %s", strerror(errno));
			errno = olderrno;
			break;
		}
		if (optval != 0) {
			errno = optval;
			break;
		}
		result = 0;
		unset_nonblock(sockfd);
		break;
	default:
		/* Should not occur */
		fatal("Bogus return (%d) from select()", rc);
	}

	xfree(fdset);
	return (result);
}

/*
 * Opens a TCP/IP connection to the remote server on the given host.
 * The address of the remote host will be returned in hostaddr.
 * If port is 0, the default port will be used.  If needpriv is true,
 * a privileged port will be allocated to make the connection.
 * This requires super-user privileges if needpriv is true.
 * Connection_attempts specifies the maximum number of tries (one per
 * second).  If proxy_command is non-NULL, it specifies the command (with %h
 * and %p substituted for host and port, respectively) to use to contact
 * the daemon.
 * Return values:
 *    0 for OK
 *    ECONNREFUSED if we got a "Connection Refused" by the peer on any address
 *    ECONNABORTED if we failed without a "Connection refused"
 * Suitable error messages for the connection failure will already have been
 * printed.
 */
int
ssh_connect(const char *host, struct sockaddr_storage * hostaddr,
    ushort_t port, int family, int connection_attempts,
    int needpriv, const char *proxy_command)
{
	int gaierr;
	int on = 1;
	int sock = -1, attempt;
	char ntop[NI_MAXHOST], strport[NI_MAXSERV];
	struct addrinfo hints, *ai, *aitop;
	struct servent *sp;
	/*
	 * Did we get only other errors than "Connection refused" (which
	 * should block fallback to rsh and similar), or did we get at least
	 * one "Connection refused"?
	 */
	int full_failure = 1;

	debug("ssh_connect: needpriv %d", needpriv);

	/* Get default port if port has not been set. */
	if (port == 0) {
		sp = getservbyname(SSH_SERVICE_NAME, "tcp");
		if (sp)
			port = ntohs(sp->s_port);
		else
			port = SSH_DEFAULT_PORT;
	}
	/* If a proxy command is given, connect using it. */
	if (proxy_command != NULL)
		return ssh_proxy_connect(host, port, proxy_command);

	/* No proxy command. */

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(strport, sizeof strport, "%u", port);
	if ((gaierr = getaddrinfo(host, strport, &hints, &aitop)) != 0)
		fatal("%s: %.100s: %s", __progname, host,
		    gai_strerror(gaierr));

	/*
	 * Try to connect several times.  On some machines, the first time
	 * will sometimes fail.  In general socket code appears to behave
	 * quite magically on many machines.
		 */
	for (attempt = 0; ;) {
		if (attempt > 0)
			debug("Trying again...");

		/* Loop through addresses for this host, and try each one in
		   sequence until the connection succeeds. */
		for (ai = aitop; ai; ai = ai->ai_next) {
			if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
				continue;
			if (getnameinfo(ai->ai_addr, ai->ai_addrlen,
			    ntop, sizeof(ntop), strport, sizeof(strport),
			    NI_NUMERICHOST|NI_NUMERICSERV) != 0) {
				error("ssh_connect: getnameinfo failed");
				continue;
			}
			debug("Connecting to %.200s [%.100s] port %s.",
				host, ntop, strport);

			/* Create a socket for connecting. */
			sock = ssh_create_socket(needpriv, ai->ai_family);
			if (sock < 0)
				/* Any error is already output */
				continue;

			if (timeout_connect(sock, ai->ai_addr, ai->ai_addrlen,
			    options.connection_timeout) >= 0) {
				/* Successful connection. */
				memcpy(hostaddr, ai->ai_addr, ai->ai_addrlen);
				break;
			} else {
				if (errno == ECONNREFUSED)
					full_failure = 0;
				debug("connect to address %s port %s: %s",
				    ntop, strport, strerror(errno));
				/*
				 * Close the failed socket; there appear to
				 * be some problems when reusing a socket for
				 * which connect() has already returned an
				 * error.
				 */
				close(sock);
			}
		}
		if (ai)
			break;	/* Successful connection. */

		attempt++;
		if (attempt >= connection_attempts)
			break;
		/* Sleep a moment before retrying. */
		sleep(1);
	}

	freeaddrinfo(aitop);

	/* Return failure if we didn't get a successful connection. */
	if (attempt >= connection_attempts) {
		log("ssh: connect to host %s port %s: %s",
		    host, strport, strerror(errno));
		return full_failure ? ECONNABORTED : ECONNREFUSED;
	}

	debug("Connection established.");

	/* Set keepalives if requested. */
	if (options.keepalives &&
	    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&on,
	    sizeof(on)) < 0)
		debug2("setsockopt SO_KEEPALIVE: %.100s", strerror(errno));

	/* Set the connection. */
	packet_set_connection(sock, sock);

	return 0;
}

/*
 * Waits for the server identification string, and sends our own
 * identification string.
 */
static void
ssh_exchange_identification(void)
{
	char buf[256], remote_version[256];	/* must be same size! */
	int remote_major, remote_minor, i, mismatch;
	int connection_in = packet_get_connection_in();
	int connection_out = packet_get_connection_out();
	int minor1 = PROTOCOL_MINOR_1;

	/* Read other side\'s version identification. */
	for (;;) {
		for (i = 0; i < sizeof(buf) - 1; i++) {
			int len = atomicio(read, connection_in, &buf[i], 1);
			if (len < 0)
				fatal("ssh_exchange_identification: read: %.100s", strerror(errno));
			if (len != 1)
				fatal("ssh_exchange_identification: Connection closed by remote host");
			if (buf[i] == '\r') {
				buf[i] = '\n';
				buf[i + 1] = 0;
				continue;		/**XXX wait for \n */
			}
			if (buf[i] == '\n') {
				buf[i + 1] = 0;
				break;
			}
		}
		buf[sizeof(buf) - 1] = 0;
		if (strncmp(buf, "SSH-", 4) == 0)
			break;
		debug("ssh_exchange_identification: %s", buf);
	}
	server_version_string = xstrdup(buf);

	/*
	 * Check that the versions match.  In future this might accept
	 * several versions and set appropriate flags to handle them.
	 */
	if (sscanf(server_version_string, "SSH-%d.%d-%[^\n]\n",
	    &remote_major, &remote_minor, remote_version) != 3)
		fatal("Bad remote protocol version identification: '%.100s'", buf);
	debug("Remote protocol version %d.%d, remote software version %.100s",
	    remote_major, remote_minor, remote_version);

	compat_datafellows(remote_version);
	mismatch = 0;

	switch (remote_major) {
	case 1:
		if (remote_minor == 99 &&
		    (options.protocol & SSH_PROTO_2) &&
		    !(options.protocol & SSH_PROTO_1_PREFERRED)) {
			enable_compat20();
			break;
		}
		if (!(options.protocol & SSH_PROTO_1)) {
			mismatch = 1;
			break;
		}
		if (remote_minor < 3) {
			fatal("Remote machine has too old SSH software version.");
		} else if (remote_minor == 3 || remote_minor == 4) {
			/* We speak 1.3, too. */
			enable_compat13();
			minor1 = 3;
			if (options.forward_agent) {
				log("Agent forwarding disabled for protocol 1.3");
				options.forward_agent = 0;
			}
		}
		break;
	case 2:
		if (options.protocol & SSH_PROTO_2) {
			enable_compat20();
			break;
		}
		/* FALLTHROUGH */
	default:
		mismatch = 1;
		break;
	}
	if (mismatch)
		fatal("Protocol major versions differ: %d vs. %d",
		    (options.protocol & SSH_PROTO_2) ? PROTOCOL_MAJOR_2 : PROTOCOL_MAJOR_1,
		    remote_major);
	/* Send our own protocol version identification. */
	snprintf(buf, sizeof buf, "SSH-%d.%d-%.100s\n",
	    compat20 ? PROTOCOL_MAJOR_2 : PROTOCOL_MAJOR_1,
	    compat20 ? PROTOCOL_MINOR_2 : minor1,
	    SSH_VERSION);
	if (atomicio(write, connection_out, buf, strlen(buf)) != strlen(buf))
		fatal("write: %.100s", strerror(errno));
	client_version_string = xstrdup(buf);
	chop(client_version_string);
	chop(server_version_string);
	debug("Local version string %.100s", client_version_string);
}

/* defaults to 'no' */
static int
confirm(const char *prompt)
{
	const char *msg;
	char *p, *again = NULL;
	int n, ret = -1;

	if (options.batch_mode)
		return 0;
	n = snprintf(NULL, 0, gettext("Please type '%s' or '%s': "),
		nl_langinfo(YESSTR), nl_langinfo(NOSTR));
	again = xmalloc(n + 1);
	(void) snprintf(again, n + 1, gettext("Please type '%s' or '%s': "),
		    nl_langinfo(YESSTR), nl_langinfo(NOSTR));

	for (msg = prompt;;msg = again) {
		p = read_passphrase(msg, RP_ECHO);
		if (p == NULL ||
		    (p[0] == '\0') || (p[0] == '\n') ||
		    strcasecmp(p, nl_langinfo(NOSTR)) == 0)
			ret = 0;
		if (p && strcasecmp(p, nl_langinfo(YESSTR)) == 0)
			ret = 1;
		if (p)
			xfree(p);
		if (ret != -1)
			return ret;
	}
}

/*
 * check whether the supplied host key is valid, return -1 if the key
 * is not valid. the user_hostfile will not be updated if 'readonly' is true.
 */
static int
check_host_key(char *host, struct sockaddr *hostaddr, Key *host_key, int
	validated, int readonly, const char *user_hostfile, const char
	*system_hostfile, char *wrn)
{
	Key *file_key;
	char *type = key_type(host_key);
	char *ip = NULL;
	char hostline[1000], *hostp, *fp;
	HostStatus host_status;
	HostStatus ip_status;
	int r, local = 0, host_ip_differ = 0;
	int salen;
	char ntop[NI_MAXHOST];
	char msg[1024];
	int len, host_line, ip_line, has_keys;
	const char *host_file = NULL, *ip_file = NULL;

	/*
	 * Force accepting of the host key for loopback/localhost. The
	 * problem is that if the home directory is NFS-mounted to multiple
	 * machines, localhost will refer to a different machine in each of
	 * them, and the user will get bogus HOST_CHANGED warnings.  This
	 * essentially disables host authentication for localhost; however,
	 * this is probably not a real problem.
	 */
	/**  hostaddr == 0! */
	switch (hostaddr->sa_family) {
	case AF_INET:
		/* LINTED */
		local = (ntohl(((struct sockaddr_in *)hostaddr)->
		   sin_addr.s_addr) >> 24) == IN_LOOPBACKNET;
		salen = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		/* LINTED */
		local = IN6_IS_ADDR_LOOPBACK(
		    &(((struct sockaddr_in6 *)hostaddr)->sin6_addr));
		salen = sizeof(struct sockaddr_in6);
		break;
	default:
		local = 0;
		salen = sizeof(struct sockaddr_storage);
		break;
	}
	if (options.no_host_authentication_for_localhost == 1 && local &&
	    options.host_key_alias == NULL) {
		debug("Forcing accepting of host key for "
		    "loopback/localhost.");
		if (wrn != NULL)
			xfree(wrn);
		return 0;
	}

	/*
	 * We don't have the remote ip-address for connections
	 * using a proxy command
	 */
	if (options.proxy_command == NULL) {
		if (getnameinfo(hostaddr, salen, ntop, sizeof(ntop),
		    NULL, 0, NI_NUMERICHOST) != 0)
			fatal("check_host_key: getnameinfo failed");
		ip = xstrdup(ntop);
	} else {
		ip = xstrdup("<no hostip for proxy command>");
	}
	/*
	 * Turn off check_host_ip if the connection is to localhost, via proxy
	 * command or if we don't have a hostname to compare with
	 */
	if (options.check_host_ip &&
	    (local || strcmp(host, ip) == 0 || options.proxy_command != NULL))
		options.check_host_ip = 0;

	/*
	 * Allow the user to record the key under a different name. This is
	 * useful for ssh tunneling over forwarded connections or if you run
	 * multiple sshd's on different ports on the same machine.
	 */
	if (options.host_key_alias != NULL) {
		host = options.host_key_alias;
		debug("using hostkeyalias: %s", host);
	}

	/*
	 * Store the host key from the known host file in here so that we can
	 * compare it with the key for the IP address.
	 */
	file_key = key_new(host_key->type);

	/*
	 * Check if the host key is present in the user's list of known
	 * hosts or in the systemwide list.
	 */
	host_file = user_hostfile;
	host_status = check_host_in_hostfile(host_file, host, host_key,
	    file_key, &host_line);
	if (host_status == HOST_NEW) {
		host_file = system_hostfile;
		host_status = check_host_in_hostfile(host_file, host, host_key,
		    file_key, &host_line);
	}
	/*
	 * Also perform check for the ip address, skip the check if we are
	 * localhost or the hostname was an ip address to begin with
	 */
	if (options.check_host_ip) {
		Key *ip_key = key_new(host_key->type);

		ip_file = user_hostfile;
		ip_status = check_host_in_hostfile(ip_file, ip, host_key,
		    ip_key, &ip_line);
		if (ip_status == HOST_NEW) {
			ip_file = system_hostfile;
			ip_status = check_host_in_hostfile(ip_file, ip,
			    host_key, ip_key, &ip_line);
		}
		if (host_status == HOST_CHANGED &&
		    (ip_status != HOST_CHANGED || !key_equal(ip_key, file_key)))
			host_ip_differ = 1;

		key_free(ip_key);
	} else
		ip_status = host_status;

	key_free(file_key);

	switch (host_status) {
	case HOST_OK:
		/* The host is known and the key matches. */
		if (validated)
			debug("Host '%.200s' is known and matches the %s host key.",
			    host, type);
		else
			debug("Host '%.200s' is known and matches the %s host "
				"key.", host, type);
		debug("Found key in %s:%d", host_file, host_line);
		if (options.check_host_ip && ip_status == HOST_NEW) {
			if (readonly)
				log("%s host key for IP address "
				    "'%.128s' not in list of known hosts.",
				    type, ip);
			else if (!add_host_to_hostfile(user_hostfile, ip,
			    host_key, options.hash_known_hosts))
				log("Failed to add the %s host key for IP "
				    "address '%.128s' to the list of known "
				    "hosts (%.30s).", type, ip, user_hostfile);
			else
				log("Warning: Permanently added the %s host "
				    "key for IP address '%.128s' to the list "
				    "of known hosts.", type, ip);
		}
		break;
	case HOST_NEW:
		if (readonly)
			goto fail;
		/* The host is new. */
		if (!validated && options.strict_host_key_checking == 1) {
			/*
			 * User has requested strict host key checking.  We
			 * will not add the host key automatically.  The only
			 * alternative left is to abort.
			 */
			error("No %s host key is known for %.200s and you "
			    "have requested strict checking.", type, host);
			goto fail;
		} else if (!validated &&
			    options.strict_host_key_checking == 2) {
			has_keys = show_other_keys(host, host_key);
			/* The default */
			fp = key_fingerprint(host_key, SSH_FP_MD5, SSH_FP_HEX);
			/*
			 * We may want to print an additional X.509-related
			 * message. The certificate might have been self-signed,
			 * for example, it's good to let the user know.
			 */
			if (wrn != NULL)
				log(wrn);
			snprintf(msg, sizeof(msg),
			    gettext("The authenticity of host '%.200s (%s)' "
			    "can't be established%s\n%s key fingerprint "
			    "is %s.\n"
			    "Are you sure you want to continue connecting "
			    "(%s/%s)? "),
			     host, ip,
			     has_keys ? gettext(",\nbut keys of different type "
				    "are already known for this host.") : ".",
			     type, fp, nl_langinfo(YESSTR), nl_langinfo(NOSTR));
			xfree(fp);
			if (!confirm(msg))
				goto fail;
		}
		/*
		 * If not in strict mode, add the key automatically to the
		 * local known_hosts file.
		 */
		if (options.check_host_ip && ip_status == HOST_NEW) {
			snprintf(hostline, sizeof(hostline), "%s,%s",
			    host, ip);
			hostp = hostline;
			if (options.hash_known_hosts) {
				/* Add hash of host and IP separately */
				r = add_host_to_hostfile(user_hostfile, host,
				    host_key, options.hash_known_hosts) &&
				    add_host_to_hostfile(user_hostfile, ip,
				    host_key, options.hash_known_hosts);
			} else {
				/* Add unhashed "host,ip" */
				r = add_host_to_hostfile(user_hostfile,
				    hostline, host_key,
				    options.hash_known_hosts);
			}
		} else {
			r = add_host_to_hostfile(user_hostfile, host, host_key,
			    options.hash_known_hosts);
			hostp = host;
		}

		if (!r)
			log("Failed to add the host to the list of known "
			    "hosts (%.500s).", user_hostfile);
		else
			log("Warning: Permanently added '%.200s' (%s) to the "
			    "list of known hosts.", hostp, type);
		break;
	case HOST_CHANGED:
		if (validated) {
			log("Warning: The host key for host %s has changed; "
				"please update your known hosts file(s) "
				"(%s:%d)", host, host_file, host_line);
			if (options.check_host_ip && host_ip_differ) {
				log("Warning: The host key for host %s has "
					"changed; please update your known "
					"hosts file(s) (%s:%d)", ip, host_file,
					host_line);

			}
			break;
		}
		if (options.check_host_ip && host_ip_differ) {
			char *msg;
			if (ip_status == HOST_NEW)
				msg = "is unknown";
			else if (ip_status == HOST_OK)
				msg = "is unchanged";
			else
				msg = "has a different value";
			error("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
			      "@       WARNING: POSSIBLE DNS SPOOFING DETECTED!          @\n"
			      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
			      "The %s host key for %s has changed,\n"
			      "and the key for the according IP address %s\n"
			      "%s. This could either mean that\n"
			      "DNS SPOOFING is happening or the IP address for the host\n"
			      "and its host key have changed at the same time.\n",
			      type, host, ip, msg);
			if (ip_status != HOST_NEW)
				error("Offending key for IP in %s:%d", ip_file, ip_line);
		}
		/* The host key has changed. */
		fp = key_fingerprint(host_key, SSH_FP_MD5, SSH_FP_HEX);
		error("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
		      "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @\n"
		      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
		      "IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!\n"
		      "Someone could be eavesdropping on you right now (man-in-the-middle attack)!\n"
		      "It is also possible that the %s host key has just been changed.\n"
		      "The fingerprint for the %s key sent by the remote host is\n%s.\n"
		      "Please contact your system administrator.\n"
		      "Add correct host key in %.100s to get rid of this message.\n"
		      "Offending key in %s:%d\n",
		      type, type, fp, user_hostfile, host_file, host_line);
		xfree(fp);

		/*
		 * If strict host key checking is in use, the user will have
		 * to edit the key manually and we can only abort.
		 */
		if (options.strict_host_key_checking) {
			error("%s host key for %.200s has changed and you have "
			    "requested strict checking.", type, host);
			goto fail;
		}

		/*
		 * If strict host key checking has not been requested, allow
		 * the connection but without password authentication or
		 * agent forwarding.
		 */
		if (options.password_authentication) {
			error("Password authentication is disabled to avoid "
			    "man-in-the-middle attacks.");
			options.password_authentication = 0;
		}
		if (options.forward_agent) {
			error("Agent forwarding is disabled to avoid "
			    "man-in-the-middle attacks.");
			options.forward_agent = 0;
		}
		if (options.forward_x11) {
			error("X11 forwarding is disabled to avoid "
			    "man-in-the-middle attacks.");
			options.forward_x11 = 0;
		}
		if (options.num_local_forwards > 0 ||
		    options.num_remote_forwards > 0) {
			error("Port forwarding is disabled to avoid "
			    "man-in-the-middle attacks.");
			options.num_local_forwards =
			    options.num_remote_forwards = 0;
		}
		/*
		 * XXX Should permit the user to change to use the new id.
		 * This could be done by converting the host key to an
		 * identifying sentence, tell that the host identifies itself
		 * by that sentence, and ask the user if he/she whishes to
		 * accept the authentication.
		 */
		break;
	case HOST_FOUND:
		fatal("internal error");
		break;
	}

	if (options.check_host_ip && host_status != HOST_CHANGED &&
	    ip_status == HOST_CHANGED) {
		snprintf(msg, sizeof(msg),
		    gettext("Warning: the %s host key for '%.200s' "
			    "differs from the key for the IP address '%.128s'"
			    "\nOffending key for IP in %s:%d"),
		    type, host, ip, ip_file, ip_line);
		if (host_status == HOST_OK) {
			len = strlen(msg);
			snprintf(msg + len, sizeof(msg) - len,
			    "\nMatching host key in %s:%d",
			    host_file, host_line);
		}
		if (!validated && options.strict_host_key_checking == 1) {
			log(msg);
			error("Exiting, you have requested strict checking.");
			goto fail;
		} else if (!validated &&
			    options.strict_host_key_checking == 2) {
			snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg),
				gettext("\nAre you sure you want to continue "
					"connecting (%s/%s)"),
				nl_langinfo(YESSTR), nl_langinfo(NOSTR));
			if (!confirm(msg))
				goto fail;
		} else {
			log(msg);
		}
	}

	xfree(ip);
	if (wrn != NULL)
		xfree(wrn);
	return 0;

fail:
	xfree(ip);
	if (wrn != NULL)
		xfree(wrn);
	return -1;
}

/*
 * Validate a host key against our KMF policy. Return SSH_KMF_CERT_VALIDATED,
 * SSH_KMF_CERT_VALIDATION_ERROR, or SSH_KMF_USE_KNOWN_HOSTS. The last return
 * code means that we could not validate the certificate but should fall back to
 * use the known_hosts database. The difference between
 * SSH_KMF_CERT_VALIDATION_ERROR and SSH_KMF_USE_KNOWN_HOSTS is, for example,
 * that in the former case the certificate could be corrupted or expired.
 *
 * Caller is responsible to free "wrn" which is allocated in this function. For
 * SSH_KMF_USE_KNOWN_HOSTS we return a newly allocated Key structure in *pubkey
 * so that the caller can use it for known_host checks. The caller is
 * responsible for freeing the key after use.
 */
static int
validate_x509_host_key(char *host, Key *host_key, Key **pubkey, char **wrn)
{
	int validated, is_selfsigned, ret = SSH_KMF_CERT_VALIDATION_ERROR;
	char *subj, *issuer;
	KMF_DATA name, mapped;

	/*
	 * Optional output argument for a warning to print later if we are
	 * accepting a new X.509 key into the known_hosts file.
	 */
	if (wrn != NULL)
		*wrn = NULL;

	/*
	 * This is a special case since self-signed certificates would
	 * silently pass kmf_validate_cert().
	 */
	is_selfsigned = ssh_kmf_is_cert_self_signed(kmf_global_handle,
	    &host_key->kmf_key->cert.certificate, &subj, &issuer);

	/* Could not process the certificate, bail out now. */
	if (is_selfsigned == SSH_KMF_CORRUPTED_CERT) {
		error("Corrupted host certificate.");
		return (SSH_KMF_CERT_VALIDATION_ERROR);
	}

	/*
	 * Even when the certificate is self-signed, we must try to validate it
	 * in order to find out about expired certificates, for example, or bad
	 * signatures. We get the certificate from the peer so it is not in the
	 * keystore, that means we will use our global KMF session.
	 */
	host_key->kmf_key->h = kmf_global_handle;
	validated = ssh_kmf_validate_cert(host_key->kmf_key,
	    options.trusted_anchor_keystore);

	if (is_selfsigned == SSH_KMF_NOT_SELF_SIGNED_CERT &&
	    validated == SSH_KMF_CERT_VALIDATED) {
		KMF_RETURN rv;

		/*
		 * It is not a self-signed cert and we did validate it. This is
		 * the "best" case.
		 */
		debug3("Host certificate validated.");
		name.Length = strlen(host);
		name.Data = (unsigned char *)xstrdup(host);

		/* Initialize the output parameter. */
		mapped.Length = 0;
		mapped.Data = NULL;

		/*
		 * The host can not use just any valid certificate. The name the
		 * certificate is mapped to must match the hostname name used in
		 * the KEX packet.
		 */
		rv = kmf_match_cert_to_name(kmf_global_handle,
		    &host_key->kmf_key->cert.certificate, &name,
		    &mapped);
		/*
		 * For the first two branches, ret has been already set on
		 * initialization.
		 */
		if (rv == KMF_ERR_NAME_NOT_MATCHED) {
			error("Hostnames from the certificate "
			    "and the command line do not match: "
			    "%s != %s", mapped.Data, name.Data);
		} else if (rv != KMF_OK) {
			debug3("Hostname '%s', cert mapped "
			    "to '%s'.", host, mapped.Data);
			ssh_kmf_debug(kmf_global_handle,
			    "kmf_match_cert_to_name", rv);
		} else {
			ret = SSH_KMF_CERT_VALIDATED;
		}
		kmf_free_data(&name);
		kmf_free_data(&mapped);
	} else if ((is_selfsigned == SSH_KMF_SELF_SIGNED_CERT &&
	    validated == SSH_KMF_CERT_VALIDATED) ||
	    (is_selfsigned == SSH_KMF_NOT_SELF_SIGNED_CERT &&
	    validated == SSH_KMF_MISSING_TA)) {
		KMF_DATA *cert = &(host_key->kmf_key->cert.certificate);

		/*
		 * It is (A) a validated self-signed certificate or (B) it is
		 * not a self-signed cert and we could not find the trusted
		 * anchor needed to validate it. In both cases, we fall back to
		 * the known_hosts file.
		 */
		if (validated == SSH_KMF_CERT_VALIDATED)
			debug("Validated self-signed certificate.");
		else
			debug("Certificate with missing TA.");

		/*
		 * Now we know that we will need the public key from the
		 * certificate to work with the known_hosts file.
		 */
		if ((*pubkey = kmf_openssl_get_key_from_cert(cert)) ==
		    NULL) {
			error("Could not convert the certificate into "
			    "an OpenSSH internal form.");
			ret = SSH_KMF_CERT_VALIDATION_ERROR;
			goto finish;
		}

		ret = SSH_KMF_USE_KNOWN_HOSTS;

		if (wrn == NULL)
			goto finish;

#define	SSH_X509_KNOWN_HOSTS_WARN_LEN	1024
		*wrn = xmalloc(SSH_X509_KNOWN_HOSTS_WARN_LEN);
		/*
		 * If this is the first time we are getting this certificate we
		 * will print an additional warning aside from the one that
		 * warns about adding the key into the known_hosts file.
		 */
		if (is_selfsigned == SSH_KMF_NOT_SELF_SIGNED_CERT) {
			snprintf(*wrn, SSH_X509_KNOWN_HOSTS_WARN_LEN,
			    "Warning: could not validate "
			    "the host certificate with the subject:\n\n  %s"
			    "\n\nWill use known_hosts "
			    "file as a fall-back measure. You "
			    "should consider\nadding the trusted "
			    "anchor certificate with the "
			    "subject:\n\n  %s\n\ninto the "
			    "directory of trusted anchors. See the "
			    "TrustedAnchorKeystore\noption in "
			    "ssh_config(4) for more information.\n",
			    subj, issuer);
		} else {
			/*
			 * is_selfsigned must be SSH_KMF_SELF_SIGNED_CERT here.
			 */
			snprintf(*wrn, SSH_X509_KNOWN_HOSTS_WARN_LEN,
			    "Warning: self-signed host "
			    "certificate used with the subject:\n\n\t %s"
			    "\n\n\t Will use known_hosts "
			    "file as a fall-back measure. See the\n\t "
			    "TrustedAnchorKeystore option in "
			    "ssh_config(4) for more\n\t information.\n",
			    subj);
		}
	} else {
		/*
		 * Return code 'validated' must have indicated some kind of
		 * validation error. We already printed the error in
		 * ssh_kmf_validate_cert(). Given that we always bail out if
		 * is_selfsigned is equal to SSH_KMF_CORRUPTED_CERT then there
		 * is only one combination left:
		 *
		 *	(is_selfsigned == SSH_KMF_SELF_SIGNED_CERT &&
		 *		validated == SSH_KMF_MISSING_TA)
		 *
		 * which is not possible. The reason is that if the certificate
		 * is self-signed then kmf_validate_cert() never returns
		 * KMF_CERT_VALIDATE_ERR_TA which means that in that case,
		 * ssh_kmf_validate_cert() never returns SSH_KMF_MISSING_TA
		 * either.
		 */
		ret = SSH_KMF_CERT_VALIDATION_ERROR;
	}

finish:
	free(subj);
	free(issuer);

	return (ret);
}

/*
 * Return 0 if the host key was verified or accepted and -1 otherwise.
 */
int
verify_host_key(char *host, struct sockaddr *hostaddr, Key *host_key)
{
	int ret;
	Key *key, *cert_key = NULL;
	struct stat st;
	char *wrn = NULL;

	/* Let's assume the host key is RSA/DSA until proved otherwise. */
	key = host_key;

	/* Check X.509 keys first. */ 
	if (key->type == KEY_X509_RSA || key->type == KEY_X509_DSS) {
		ret = validate_x509_host_key(host, key, &cert_key, &wrn);
		switch (ret) {
		case SSH_KMF_CERT_VALIDATION_ERROR:
			return (-1);
		case SSH_KMF_CERT_VALIDATED:
			return (0);
		case SSH_KMF_USE_KNOWN_HOSTS:
			break;
		default:
			fatal("Incorrect return code from "
			    "validate_x509_host_key (%d).", ret);
		}
	}

	/*
	 * If we verify the X.509 key against the known_hosts database we will
	 * work with a pubkey extracted from the certificate to maintain
	 * backward compatibility with the current format of known_hosts file.
	 */
	if (cert_key != NULL)
		key = cert_key;

	/* return ok if the key can be found in an old keyfile */
	if (stat(options.system_hostfile2, &st) == 0 ||
	    stat(options.user_hostfile2, &st) == 0) {
		if (check_host_key(host, hostaddr, key, 0, /*readonly*/ 1,
		    options.user_hostfile2, options.system_hostfile2,
		    wrn) == 0) {
			if (cert_key != NULL)
				key_free(cert_key);
			return 0;
		}
	}

	ret = check_host_key(host, hostaddr, key, 0, /*readonly*/ 0,
	    options.user_hostfile, options.system_hostfile, wrn);
	if (cert_key != NULL)
		key_free(cert_key);
	return (ret);
}

int
accept_host_key(char *host, struct sockaddr *hostaddr, Key *host_key)
{
	struct stat st;

	/* return ok if the key can be found in an old keyfile */
	if (stat(options.system_hostfile2, &st) == 0 ||
	    stat(options.user_hostfile2, &st) == 0) {
		if (check_host_key(host, hostaddr, host_key, 1, /*readonly*/ 1,
		    options.user_hostfile2, options.system_hostfile2,
		    NULL) == 0)
			return 0;
	}
	return check_host_key(host, hostaddr, host_key, 1, /*readonly*/ 0,
	    options.user_hostfile, options.system_hostfile, NULL);
}
/*
 * Starts a dialog with the server, and authenticates the current user on the
 * server.  This does not need any extra privileges.  The basic connection
 * to the server must already have been established before this is called.
 * If login fails, this function prints an error and never returns.
 * This function does not require super-user privileges.
 */
void
ssh_login(Sensitive *sensitive, const char *orighost,
    struct sockaddr *hostaddr, char *pw_name)
{
	char *host, *cp;
	char *server_user, *local_user;

	local_user = xstrdup(pw_name);
	server_user = options.user ? options.user : local_user;

	/* Convert the user-supplied hostname into all lowercase. */
	host = xstrdup(orighost);
	for (cp = host; *cp; cp++)
		if (isupper(*cp))
			*cp = tolower(*cp);

	debug("Logging to host: %s", host);
	debug("Local user: %s Remote user: %s", local_user, server_user);
	/* Exchange protocol version identification strings with the server. */
	ssh_exchange_identification();

	/*
	 * See comment at definition of will_daemonize for information why we
	 * don't support the PKCS#11 engine with protocol 1.
	 */
	if (compat20 == 1 && options.use_openssl_engine == 1) {
		/*
		 * If this fails then 'e' will be NULL which means we do not use
		 * the engine, as if UseOpenSSLEngine was set to "no". This is
		 * important in case we go to the background after the
		 * authentication.
		 */
		e = pkcs11_engine_load(options.use_openssl_engine);
	}

	/* Put the connection into non-blocking mode. */
	packet_set_nonblocking();

	/* key exchange */
	/* authenticate user */
	if (compat20) {
		/*
		 * Note that the host pointer is saved in ssh_kex2() for later
		 * use during the key re-exchanges so we must not xfree() it.
		 */
		ssh_kex2(host, hostaddr);
		ssh_userauth2(local_user, server_user, host, sensitive);
	} else {
		ssh_kex(host, hostaddr);
		ssh_userauth1(local_user, server_user, host, sensitive);
	}

	xfree(local_user);
}

void
ssh_put_password(char *password)
{
	int size;
	char *padded;

	if (datafellows & SSH_BUG_PASSWORDPAD) {
		packet_put_cstring(password);
		return;
	}
	size = roundup(strlen(password) + 1, 32);
	padded = xmalloc(size);
	memset(padded, 0, size);
	strlcpy(padded, password, size);
	packet_put_string(padded, size);
	memset(padded, 0, size);
	xfree(padded);
}

static int
show_key_from_file(const char *file, const char *host, int keytype)
{
	Key *found;
	char *fp;
	int line, ret;

	found = key_new(keytype);
	if ((ret = lookup_key_in_hostfile_by_type(file, host,
	    keytype, found, &line))) {
		fp = key_fingerprint(found, SSH_FP_MD5, SSH_FP_HEX);
		log("WARNING: %s key found for host %s\n"
		    "in %s:%d\n"
		    "%s key fingerprint %s.",
		    key_type(found), host, file, line,
		    key_type(found), fp);
		xfree(fp);
	}
	key_free(found);
	return (ret);
}

/* print all known host keys for a given host, but skip keys of given type */
static int
show_other_keys(const char *host, Key *key)
{
	int type[] = { KEY_RSA1, KEY_RSA, KEY_DSA, -1};
	int i, found = 0;

	for (i = 0; type[i] != -1; i++) {
		if (type[i] == key->type)
			continue;
		if (type[i] != KEY_RSA1 &&
		    show_key_from_file(options.user_hostfile2, host, type[i])) {
			found = 1;
			continue;
		}
		if (type[i] != KEY_RSA1 &&
		    show_key_from_file(options.system_hostfile2, host, type[i])) {
			found = 1;
			continue;
		}
		if (show_key_from_file(options.user_hostfile, host, type[i])) {
			found = 1;
			continue;
		}
		if (show_key_from_file(options.system_hostfile, host, type[i])) {
			found = 1;
			continue;
		}
		debug2("no key of type %d for host %s", type[i], host);
	}
	return (found);
}
