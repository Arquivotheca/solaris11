/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Ssh client program.  This program can be used to log into a remote machine.
 * The software supports strong authentication, encryption, and forwarding
 * of X11, TCP/IP, and authentication connections.
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 *
 * Copyright (c) 1999 Niels Provos.  All rights reserved.
 * Copyright (c) 2000, 2001, 2002 Markus Friedl.  All rights reserved.
 *
 * Modified to work with SSL by Niels Provos <provos@citi.umich.edu>
 * in Canada (German citizen).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "includes.h"
RCSID("$OpenBSD: ssh.c,v 1.186 2002/09/19 01:58:18 djm Exp $");

#include <openssl/err.h>

#include "ssh.h"
#include "ssh1.h"
#include "ssh2.h"
#include "compat.h"
#include "cipher.h"
#include "xmalloc.h"
#include "packet.h"
#include "buffer.h"
#include "channels.h"
#include "key.h"
#include "authfd.h"
#include "authfile.h"
#include "pathnames.h"
#include "clientloop.h"
#include "log.h"
#include "readconf.h"
#include "sshconnect.h"
#include "tildexpand.h"
#include "dispatch.h"
#include "misc.h"
#include "kex.h"
#include "mac.h"
#include "sshtty.h"
#include "g11n.h"
#include "kmf.h"

#ifdef HAVE___PROGNAME
extern char *__progname;
#else
char *__progname;
#endif

/* Flag indicating whether IPv4 or IPv6.  This can be set on the command line.
   Default value is AF_UNSPEC means both IPv4 and IPv6. */
#ifdef IPV4_DEFAULT
int IPv4or6 = AF_INET;
#else
int IPv4or6 = AF_UNSPEC;
#endif

/* Flag indicating whether debug mode is on.  This can be set on the command line. */
int debug_flag = 0;

/* Flag indicating whether a tty should be allocated */
int tty_flag = 0;
int no_tty_flag = 0;
int force_tty_flag = 0;

/* don't exec a shell */
int no_shell_flag = 0;

/*
 * Flag indicating that nothing should be read from stdin.  This can be set
 * on the command line.
 */
int stdin_null_flag = 0;

/*
 * Flag indicating that ssh should fork after authentication.  This is useful
 * so that the passphrase can be entered manually, and then ssh goes to the
 * background.
 */
int fork_after_authentication_flag = 0;

/*
 * General data structure for command line options and options configurable
 * in configuration files.  See readconf.h.
 */
Options options;

/* optional user configfile */
char *config = NULL;

/*
 * Name of the host we are connecting to.  This is the name given on the
 * command line, or the HostName specified for the user-supplied name in a
 * configuration file.
 */
char *host;

/* socket address the host resolves to */
struct sockaddr_storage hostaddr;

/* Private host keys. */
Sensitive sensitive_data;

/* Original real UID. */
uid_t original_real_uid;
uid_t original_effective_uid;

/* command to be executed */
Buffer command;

/* Should we execute a command or invoke a subsystem? */
int subsystem_flag = 0;

/* # of replies received for global requests */
static int client_global_request_id = 0;

/* pid of proxycommand child process */
pid_t proxy_command_pid = 0;

/* Prints a help message to the user.  This function never returns. */

static void
usage(void)
{
	fprintf(stderr,
		gettext("Usage: %s [options] host [command]\n"
		"Options:\n"
		"  -l user     Log in using this user name.\n"
		"  -n          Redirect input from /dev/null.\n"
		"  -F config   Config file (default: ~/%s).\n"
		"  -A          Enable authentication agent forwarding.\n"
		"  -a          Disable authentication agent forwarding "
		"(default).\n"
#ifdef AFS
		"  -k          Disable Kerberos ticket and AFS token "
		"forwarding.\n"
#endif				/* AFS */
		"  -X          Enable X11 connection forwarding.\n"
		"  -x          Disable X11 connection forwarding (default).\n"
		"  -i file     Identity for public key authentication "
			    "(default: ~/.ssh/identity)\n"
		"  -t          Tty; allocate a tty even if command is given.\n"
		"  -T          Do not allocate a tty.\n"
		"  -v          Verbose; display verbose debugging messages.\n"
		"              Multiple -v increases verbosity.\n"
		"  -V          Display version number only.\n"
		"  -q          Quiet; don't display any warning messages.\n"
		"  -f          Fork into background after authentication.\n"
		"  -e char     Set escape character; ``none'' = disable "
		"(default: ~).\n"
		"  -c cipher   Select encryption algorithm\n"
		"  -m macs     Specify MAC algorithms for protocol version 2.\n"
		"  -p port     Connect to this port.  Server must be "
		"on the same port.\n"
		"  -L listen-port:host:port   Forward local port to "
		"remote address\n"
		"  -R listen-port:host:port   Forward remote port to "
		"local address\n"
		"              These cause %s to listen for connections "
		"on a port, and\n"
		"              forward them to the other side by "
		"connecting to host:port.\n"
		"  -D port     Enable dynamic application-level "
		"port forwarding.\n"
		"  -C          Enable compression.\n"
		"  -N          Do not execute a shell or command.\n"
		"  -g          Allow remote hosts to connect to forwarded "
		"ports.\n"
		"  -1          Force protocol version 1.\n"
		"  -2          Force protocol version 2.\n"
		"  -4          Use IPv4 only.\n"
		"  -6          Use IPv6 only.\n"
		"  -o 'option' Process the option as if it was read "
		"from a configuration file.\n"
		"  -s          Invoke command (mandatory) as SSH2 subsystem.\n"
		"  -b addr     Local IP address.\n"),
		__progname, _PATH_SSH_USER_CONFFILE, __progname);
	exit(1);
}

static int ssh_session(void);
static int ssh_session2(void);
static void load_public_identity_files(void);
static void rsh_connect(char *host, char *user, Buffer * command);

/*
 * Main program for the ssh client.
 */
int
main(int ac, char **av)
{
	int i, opt, exit_status;
	char *p, *cp, buf[256], *pw_name, *pw_dir;
	struct stat st;
	struct passwd *pw;
	int dummy;
	extern int optind, optreset;
	extern char *optarg;
	Forward fwd;

	__progname = get_progname(av[0]);

	(void) g11n_setlocale(LC_ALL, "");

	init_rng();

	/*
	 * Save the original real uid.  It will be needed later (uid-swapping
	 * may clobber the real uid).
	 */
	original_real_uid = getuid();
	original_effective_uid = geteuid();
 
	/*
	 * Use uid-swapping to give up root privileges for the duration of
	 * option processing.  We will re-instantiate the rights when we are
	 * ready to create the privileged port, and will permanently drop
	 * them when the port has been created (actually, when the connection
	 * has been made, as we may need to create the port several times).
	 */
	PRIV_END;

#ifdef HAVE_SETRLIMIT
	/* If we are installed setuid root be careful to not drop core. */
	if (original_real_uid != original_effective_uid) {
		struct rlimit rlim;
		rlim.rlim_cur = rlim.rlim_max = 0;
		if (setrlimit(RLIMIT_CORE, &rlim) < 0)
			fatal("setrlimit failed: %.100s", strerror(errno));
	}
#endif
	/*
	 * Get user data. It may happen that NIS or LDAP connection breaks down
	 * during the user's session. We should try to do our best and use the
	 * HOME and LOGNAME variables. Remember that the SSH client might be the
	 * only tool available to fix the problem with the naming services.
	 */
	pw = getpwuid(original_real_uid);
	if (pw == NULL) {
		if ((pw_dir = getenv("HOME")) == NULL) {
			log("User account's password entry not found and HOME "
			    "not set. Set it manually and try again. "
			    "Exiting.");
			exit(1);
		}
		log("User account's password entry not found, using "
		    "the HOME variable.");

		if ((pw_name = getenv("LOGNAME")) == NULL) {
			log("Need a local user name but LOGNAME is not set. "
			   "Set it manually and try again. Exiting.");
			exit(1);
		}
		log("Local user name '%s' set from the LOGNAME variable.",
		    pw_name);

		pw_dir = xstrdup(pw_dir);
		pw_name = xstrdup(pw_name);
	} else {
		pw_name = xstrdup(pw->pw_name);
		pw_dir = xstrdup(pw->pw_dir);
	}

	/*
	 * Set our umask to something reasonable, as some files are created
	 * with the default umask.  This will make them world-readable but
	 * writable only by the owner, which is ok for all files for which we
	 * don't set the modes explicitly.
	 */
	umask(022);

	/* Initialize option structure to indicate that no values have been set. */
	initialize_options(&options);

	/* Parse command-line arguments. */
	host = NULL;

again:
	while ((opt = getopt(ac, av,
	    "1246ab:c:e:fgi:kl:m:no:p:qstvxACD:F:I:L:NPR:TVX")) != -1) {
		switch (opt) {
		case '1':
			options.protocol = SSH_PROTO_1;
			break;
		case '2':
			options.protocol = SSH_PROTO_2;
			break;
		case '4':
			IPv4or6 = AF_INET;
			break;
		case '6':
			IPv4or6 = AF_INET6;
			break;
		case 'n':
			stdin_null_flag = 1;
			break;
		case 'f':
			fork_after_authentication_flag = 1;
			stdin_null_flag = 1;
			break;
		case 'x':
			options.forward_x11 = 0;
			break;
		case 'X':
			options.forward_x11 = 1;
			break;
		case 'g':
			options.gateway_ports = 1;
			break;
		case 'P':	/* deprecated */
			fprintf(stderr, gettext("Warning: Option -P has "
						"been deprecated\n"));
			options.use_privileged_port = 0;
			break;
		case 'a':
			options.forward_agent = 0;
			break;
		case 'A':
			options.forward_agent = 1;
			break;
#ifdef AFS
		case 'k':
			options.kerberos_tgt_passing = 0;
			options.afs_token_passing = 0;
			break;
#endif
		case 'i':
			if (stat(optarg, &st) < 0) {
				fprintf(stderr,
					gettext("Warning: Identity file %s "
					"does not exist.\n"), optarg);
				break;
			}
			if (options.num_identity_files >=
			    SSH_MAX_IDENTITY_FILES)
				fatal("Too many identity files specified "
				    "(max %d)", SSH_MAX_IDENTITY_FILES);
			options.identity_files[options.num_identity_files++] =
			    xstrdup(optarg);
			break;
		case 'I':
			fprintf(stderr, "no support for smartcards.\n");
			break;
		case 't':
			if (tty_flag)
				force_tty_flag = 1;
			tty_flag = 1;
			break;
		case 'v':
			if (0 == debug_flag) {
				debug_flag = 1;
				options.log_level = SYSLOG_LEVEL_DEBUG1;
			} else if (options.log_level < SYSLOG_LEVEL_DEBUG3) {
				options.log_level++;
				break;
			} else
				fatal("Too high debugging level.");
			/* FALLTHROUGH */
		case 'V':
			fprintf(stderr,
			    gettext("%s, SSH protocols %d.%d/%d.%d, "
				    "OpenSSL 0x%8.8lx\n"),
			    SSH_VERSION,
			    PROTOCOL_MAJOR_1, PROTOCOL_MINOR_1,
			    PROTOCOL_MAJOR_2, PROTOCOL_MINOR_2,
			    SSLeay());
			if (opt == 'V')
				exit(0);
			break;
		case 'q':
			options.log_level = SYSLOG_LEVEL_QUIET;
			break;
		case 'e':
			if (optarg[0] == '^' && optarg[2] == 0 &&
			    (u_char) optarg[1] >= 64 &&
			    (u_char) optarg[1] < 128)
				options.escape_char = (u_char) optarg[1] & 31;
			else if (strlen(optarg) == 1)
				options.escape_char = (u_char) optarg[0];
			else if (strcmp(optarg, "none") == 0)
				options.escape_char = SSH_ESCAPECHAR_NONE;
			else {
				fprintf(stderr,
					gettext("Bad escape character '%s'.\n"),
				    optarg);
				exit(1);
			}
			break;
		case 'c':
			if (ciphers_valid(optarg)) {
				/* SSH2 only */
				options.ciphers = xstrdup(optarg);
				options.cipher = SSH_CIPHER_ILLEGAL;
			} else {
				/* SSH1 only */
				options.cipher = cipher_number(optarg);
				if (options.cipher == -1) {
					fprintf(stderr,
						gettext("Unknown cipher "
							"type '%s'\n"),
					    optarg);
					exit(1);
				}
				if (options.cipher == SSH_CIPHER_3DES)
					options.ciphers = "3des-cbc";
				else if (options.cipher == SSH_CIPHER_BLOWFISH)
					options.ciphers = "blowfish-cbc";
				else
					options.ciphers = (char *)-1;
			}
			break;
		case 'm':
			if (mac_valid(optarg))
				options.macs = xstrdup(optarg);
			else {
				fprintf(stderr,
					gettext("Unknown mac type '%s'\n"),
				    optarg);
				exit(1);
			}
			break;
		case 'p':
			options.port = a2port(optarg);
			if (options.port == 0) {
				fprintf(stderr, gettext("Bad port '%s'\n"),
					optarg);
				exit(1);
			}
			break;
		case 'l':
			options.user = optarg;
			break;

		case 'L':
			if (parse_forward(1, &fwd, optarg))
				add_local_forward(&options, &fwd);
			else
				fatal("Bad local forwarding specification "
				"'%s'\n", optarg);
			break;

		case 'R':
			if (parse_forward(1, &fwd, optarg))
				add_remote_forward(&options, &fwd);
			else
				fatal("Bad remote forwarding specification "
				    "'%s'\n", optarg);
			break;

		case 'D':
			if (parse_forward(0, &fwd, optarg) == 0)
				fatal("Bad dynamic forwarding specification "
				    "'%s'\n", optarg);
			fwd.connect_host = "socks";
			add_local_forward(&options, &fwd);
			break;

		case 'C':
			options.compression = 1;
			break;
		case 'N':
			no_shell_flag = 1;
			no_tty_flag = 1;
			break;
		case 'T':
			no_tty_flag = 1;
			break;
		case 'o':
			dummy = 1;
			if (process_config_line(&options, host ? host : "",
			    optarg, "command-line", 0, &dummy) != 0)
				exit(1);
			break;
		case 's':
			subsystem_flag = 1;
			break;
		case 'b':
			options.bind_address = optarg;
			break;
		case 'F':
			config = optarg;
			break;
		default:
			usage();
		}
	}

	ac -= optind;
	av += optind;

	if (ac > 0 && !host && **av != '-') {
		if (strrchr(*av, '@')) {
			p = xstrdup(*av);
			cp = strrchr(p, '@');
			if (cp == NULL || cp == p) {
				fprintf(stderr, gettext("You have to specify host.\n"));
				usage();
			}
			options.user = p;
			*cp = '\0';
			host = ++cp;
		} else
			host = *av;
		ac--, av++;
		if (ac > 0) {
			optind = 0;
			optreset = 1;
			goto again;
		}
	}

	/* Check that we got a host name. */
	if (!host) {
		fprintf(stderr, gettext("You have to specify host.\n"));
		usage();
	}

	SSLeay_add_all_algorithms();
	ERR_load_crypto_strings();
	channel_set_af(IPv4or6);

	/* Initialize the command to execute on remote host. */
	buffer_init(&command);

	/*
	 * Save the command to execute on the remote host in a buffer. There
	 * is no limit on the length of the command, except by the maximum
	 * packet size.  Also sets the tty flag if there is no command.
	 */
	if (!ac) {
		/* No command specified - execute shell on a tty. */
		tty_flag = 1;
		if (subsystem_flag) {
			fprintf(stderr,
				gettext("You must specify a subsystem "
					"to invoke.\n"));
			usage();
		}
	} else {
		/* A command has been specified.  Store it into the buffer. */
		for (i = 0; i < ac; i++) {
			if (i)
				buffer_append(&command, " ", 1);
			buffer_append(&command, av[i], strlen(av[i]));
		}
	}

	/* Cannot fork to background if no command. */
	if (fork_after_authentication_flag && buffer_len(&command) == 0 && !no_shell_flag)
		fatal("Cannot fork into background without a command to execute.");

	/* Allocate a tty by default if no command specified. */
	if (buffer_len(&command) == 0)
		tty_flag = 1;

	/* Force no tty */
	if (no_tty_flag)
		tty_flag = 0;
	/* Do not allocate a tty if stdin is not a tty. */
	if (!isatty(fileno(stdin)) && !force_tty_flag) {
		if (tty_flag)
			log("Pseudo-terminal will not be allocated because stdin is not a terminal.");
		tty_flag = 0;
	}

	/*
	 * Initialize "log" output.  Since we are the client all output
	 * actually goes to stderr.
	 */
	log_init(av[0], options.log_level == -1 ? SYSLOG_LEVEL_INFO : options.log_level,
	    SYSLOG_FACILITY_USER, 1);

	/*
	 * Read per-user configuration file.  Ignore the system wide config
	 * file if the user specifies a config file on the command line.
	 */
	if (config != NULL) {
		if (!read_config_file(config, host, &options))
			fatal("Can't open user config file %.100s: "
			    "%.100s", config, strerror(errno));
	} else  {
		snprintf(buf, sizeof buf, "%.100s/%.100s", pw_dir,
		    _PATH_SSH_USER_CONFFILE);
		(void)read_config_file(buf, host, &options);

		/* Read systemwide configuration file after use config. */
		(void)read_config_file(_PATH_HOST_CONFIG_FILE, host, &options);
	}

	process_unknown_options(&options);

	/* Fill configuration defaults. */
	fill_default_options(&options);

	/* reinit */
	log_init(av[0], options.log_level, SYSLOG_FACILITY_USER, 1);

	seed_rng();

	if (options.user == NULL)
		options.user = xstrdup(pw_name);

	if (options.hostname != NULL)
		host = options.hostname;

	if (options.proxy_command != NULL &&
	    strcmp(options.proxy_command, "none") == 0) {
		xfree(options.proxy_command);
		options.proxy_command = NULL;
	}

	/* Disable rhosts authentication if not running as root. */
#ifdef HAVE_CYGWIN
	/* Ignore uid if running under Windows */
	if (!options.use_privileged_port) {
#else
	if (original_effective_uid != 0 || !options.use_privileged_port) {
#endif
		debug("Rhosts Authentication disabled, "
		    "originating port will not be trusted.");
		options.rhosts_authentication = 0;
	}
	/* Open a connection to the remote host. */

	/* XXX OpenSSH has deprecated UseRsh */
	if (options.use_rsh) {
		seteuid(original_real_uid);
		setuid(original_real_uid);
		rsh_connect(host, options.user, &command);
		fatal("rsh_connect returned");
	}

	if (ssh_connect(host, &hostaddr, options.port, IPv4or6,
	    options.connection_attempts,
#ifdef HAVE_CYGWIN
	    options.use_privileged_port,
#else
	    original_effective_uid == 0 && options.use_privileged_port,
#endif
	    options.proxy_command) != 0) {
		/* XXX OpenSSH has deprecated FallbackToRsh */
		if (options.fallback_to_rsh) {
			seteuid(original_real_uid);
			setuid(original_real_uid);
			rsh_connect(host, options.user, &command);
			fatal("rsh_connect returned");
		}
		exit(1);
	}

	/*
	 * If we successfully made the connection, load the host private key
	 * in case we will need it later for combined rsa-rhosts
	 * authentication. This must be done before releasing extra
	 * privileges, because the file is only readable by root.
	 * If we cannot access the private keys, load the public keys
	 * instead and try to execute the ssh-keysign helper instead.
	 */
	sensitive_data.nkeys = 0;
	sensitive_data.keys = NULL;
	sensitive_data.external_keysign = 0;
	if (options.rhosts_rsa_authentication ||
	    options.hostbased_authentication) {
		sensitive_data.nkeys = 3;
		sensitive_data.keys = xmalloc(sensitive_data.nkeys *
		    sizeof(Key));

		PRIV_START;
		sensitive_data.keys[0] = key_load_private_type(KEY_RSA1,
		    _PATH_HOST_KEY_FILE, "", NULL);
		sensitive_data.keys[1] = key_load_private_type(KEY_DSA,
		    _PATH_HOST_DSA_KEY_FILE, "", NULL);
		sensitive_data.keys[2] = key_load_private_type(KEY_RSA,
		    _PATH_HOST_RSA_KEY_FILE, "", NULL);
		PRIV_END;

		if (options.hostbased_authentication == 1 &&
		    sensitive_data.keys[0] == NULL &&
		    sensitive_data.keys[1] == NULL &&
		    sensitive_data.keys[2] == NULL) {
			/*
			 * Here it actually does not matter whether we use
			 * SSH_KMF_IGNORE_CERT or SSH_KMF_LOAD_CERT since these
			 * are hardcoded plain files.
			 */
			sensitive_data.keys[1] = key_load_public(
			    _PATH_HOST_DSA_KEY_FILE, NULL, SSH_KMF_IGNORE_CERT,
			    NULL);
			sensitive_data.keys[2] = key_load_public(
			    _PATH_HOST_RSA_KEY_FILE, NULL, SSH_KMF_IGNORE_CERT,
			    NULL);
			sensitive_data.external_keysign = 1;
		}
	}
	/*
	 * Get rid of any extra privileges that we may have.  We will no
	 * longer need them.  Also, extra privileges could make it very hard
	 * to read identity files and other non-world-readable files from the
	 * user's home directory if it happens to be on a NFS volume where
	 * root is mapped to nobody.
	 */
	seteuid(original_real_uid);
	setuid(original_real_uid);

	/*
	 * Now that we are back to our own permissions, create ~/.ssh
	 * directory if it doesn\'t already exist.
	 */
	snprintf(buf, sizeof buf, "%.100s%s%.100s", pw_dir,
	    strcmp(pw_dir, "/") ? "/" : "", _PATH_SSH_USER_DIR);
	xfree(pw_dir);
	if (stat(buf, &st) < 0)
		if (mkdir(buf, 0700) < 0)
			error("Could not create directory '%.200s'.", buf);

	/* load options.identity_files */
	load_public_identity_files();

	/* Expand ~ in known host file names. */
	/* XXX mem-leaks: */
	options.system_hostfile =
	    tilde_expand_filename(options.system_hostfile, original_real_uid);
	options.user_hostfile =
	    tilde_expand_filename(options.user_hostfile, original_real_uid);
	options.system_hostfile2 =
	    tilde_expand_filename(options.system_hostfile2, original_real_uid);
	options.user_hostfile2 =
	    tilde_expand_filename(options.user_hostfile2, original_real_uid);

	signal(SIGPIPE, SIG_IGN); /* ignore SIGPIPE early */

	/* Log into the remote system.  This never returns if the login fails. */
	ssh_login(&sensitive_data, host, (struct sockaddr *)&hostaddr, pw_name);
	xfree(pw_name);

	/* We no longer need the private host keys.  Clear them now. */
	if (sensitive_data.nkeys != 0) {
		for (i = 0; i < sensitive_data.nkeys; i++) {
			if (sensitive_data.keys[i] != NULL) {
				/* Destroys contents safely */
				debug3("clear hostkey %d", i);
				key_free(sensitive_data.keys[i]);
				sensitive_data.keys[i] = NULL;
			}
		}
		xfree(sensitive_data.keys);
	}
	for (i = 0; i < options.num_identity_files; i++) {
		if (options.identity_files[i]) {
			xfree(options.identity_files[i]);
			options.identity_files[i] = NULL;
		}
		if (options.public_identity_keys[i]) {
			key_free(options.public_identity_keys[i]);
			options.public_identity_keys[i] = NULL;
		}
	}

	exit_status = compat20 ? ssh_session2() : ssh_session();
	packet_close();

	/*
	 * Send SIGHUP to proxy command if used. We don't wait() in 
	 * case it hangs and instead rely on init to reap the child
	 */
	if (proxy_command_pid > 1)
		kill(proxy_command_pid, SIGHUP);

	return exit_status;
}

static void
ssh_init_forwarding(void)
{
	int success = 0;
	int i;

	/* Initiate local TCP/IP port forwardings. */
	for (i = 0; i < options.num_local_forwards; i++) {
		debug("Local connections to %.200s:%d forwarded to remote "
		    "address %.200s:%d",
		    (options.local_forwards[i].listen_host == NULL) ?
		    (options.gateway_ports ? "*" : "LOCALHOST") :
		    options.local_forwards[i].listen_host,
		    options.local_forwards[i].listen_port,
		    options.local_forwards[i].connect_host,
		    options.local_forwards[i].connect_port);
		success += channel_setup_local_fwd_listener(
		    options.local_forwards[i].listen_host,
		    options.local_forwards[i].listen_port,
		    options.local_forwards[i].connect_host,
		    options.local_forwards[i].connect_port,
		    options.gateway_ports);
	}
	if (i > 0 && success == 0)
		error("Could not request local forwarding.");

	/* Initiate remote TCP/IP port forwardings. */
	for (i = 0; i < options.num_remote_forwards; i++) {
		debug("Remote connections from %.200s:%d forwarded to "
		    "local address %.200s:%d",
		    (options.remote_forwards[i].listen_host == NULL) ?
		    "LOCALHOST" : options.remote_forwards[i].listen_host,
		    options.remote_forwards[i].listen_port,
		    options.remote_forwards[i].connect_host,
		    options.remote_forwards[i].connect_port);
		if (channel_request_remote_forwarding(
		    options.remote_forwards[i].listen_host,
		    options.remote_forwards[i].listen_port,
		    options.remote_forwards[i].connect_host,
		    options.remote_forwards[i].connect_port) < 0) {
			log("Warning: Could not request remote forwarding.");
		    }
	}
}

static void
check_agent_present(void)
{
	if (options.forward_agent) {
		/* Clear agent forwarding if we don't have an agent. */
		if (!ssh_agent_present())
			options.forward_agent = 0;
	}
}

static int
ssh_session(void)
{
	int type;
	int interactive = 0;
	int have_tty = 0;
	struct winsize ws;
	char *cp;
	const char *display;

	/* Enable compression if requested. */
	if (options.compression) {
		debug("Requesting compression at level %d.", options.compression_level);

		if (options.compression_level < 1 || options.compression_level > 9)
			fatal("Compression level must be from 1 (fast) to 9 (slow, best).");

		/* Send the request. */
		packet_start(SSH_CMSG_REQUEST_COMPRESSION);
		packet_put_int(options.compression_level);
		packet_send();
		packet_write_wait();
		type = packet_read();
		if (type == SSH_SMSG_SUCCESS)
			packet_start_compression(options.compression_level);
		else if (type == SSH_SMSG_FAILURE)
			log("Warning: Remote host refused compression.");
		else
			packet_disconnect("Protocol error waiting for compression response.");
	}
	/* Allocate a pseudo tty if appropriate. */
	if (tty_flag) {
		debug("Requesting pty.");

		/* Start the packet. */
		packet_start(SSH_CMSG_REQUEST_PTY);

		/* Store TERM in the packet.  There is no limit on the
		   length of the string. */
		cp = getenv("TERM");
		if (!cp)
			cp = "";
		packet_put_cstring(cp);

		/* Store window size in the packet. */
		if (ioctl(fileno(stdin), TIOCGWINSZ, &ws) < 0)
			memset(&ws, 0, sizeof(ws));
		packet_put_int(ws.ws_row);
		packet_put_int(ws.ws_col);
		packet_put_int(ws.ws_xpixel);
		packet_put_int(ws.ws_ypixel);

		/* Store tty modes in the packet. */
		tty_make_modes(fileno(stdin), NULL);

		/* Send the packet, and wait for it to leave. */
		packet_send();
		packet_write_wait();

		/* Read response from the server. */
		type = packet_read();
		if (type == SSH_SMSG_SUCCESS) {
			interactive = 1;
			have_tty = 1;
		} else if (type == SSH_SMSG_FAILURE)
			log("Warning: Remote host failed or refused to allocate a pseudo tty.");
		else
			packet_disconnect("Protocol error waiting for pty request response.");
	}
	/* Request X11 forwarding if enabled and DISPLAY is set. */
	display = getenv("DISPLAY");
	if (options.forward_x11 && display != NULL) {
		char *proto, *data;
		/* Get reasonable local authentication information. */
		client_x11_get_proto(display, options.xauth_location,
		    options.forward_x11_trusted, &proto, &data);
		/* Request forwarding with authentication spoofing. */
		debug("Requesting X11 forwarding with authentication spoofing.");
		x11_request_forwarding_with_spoofing(0, display, proto, data);

		/* Read response from the server. */
		type = packet_read();
		if (type == SSH_SMSG_SUCCESS) {
			interactive = 1;
		} else if (type == SSH_SMSG_FAILURE) {
			log("Warning: Remote host denied X11 forwarding.");
		} else {
			packet_disconnect("Protocol error waiting for X11 forwarding");
		}
	}
	/* Tell the packet module whether this is an interactive session. */
	packet_set_interactive(interactive);

	/* Request authentication agent forwarding if appropriate. */
	check_agent_present();

	if (options.forward_agent) {
		debug("Requesting authentication agent forwarding.");
		auth_request_forwarding();

		/* Read response from the server. */
		type = packet_read();
		packet_check_eom();
		if (type != SSH_SMSG_SUCCESS)
			log("Warning: Remote host denied authentication agent forwarding.");
	}

	/* Initiate port forwardings. */
	ssh_init_forwarding();

	/* If requested, let ssh continue in the background. */
	if (fork_after_authentication_flag)
		if (daemon(1, 1) < 0)
			fatal("daemon() failed: %.200s", strerror(errno));

	/*
	 * If a command was specified on the command line, execute the
	 * command now. Otherwise request the server to start a shell.
	 */
	if (buffer_len(&command) > 0) {
		int len = buffer_len(&command);
		if (len > 900)
			len = 900;
		debug("Sending command: %.*s", len, (u_char *)buffer_ptr(&command));
		packet_start(SSH_CMSG_EXEC_CMD);
		packet_put_string(buffer_ptr(&command), buffer_len(&command));
		packet_send();
		packet_write_wait();
	} else {
		debug("Requesting shell.");
		packet_start(SSH_CMSG_EXEC_SHELL);
		packet_send();
		packet_write_wait();
	}

	/* Enter the interactive session. */
	return client_loop(have_tty, tty_flag ?
	    options.escape_char : SSH_ESCAPECHAR_NONE, 0);
}

static void
client_subsystem_reply(int type, u_int32_t seq, void *ctxt)
{
	int id, len;

	id = packet_get_int();
	len = buffer_len(&command);
	if (len > 900)
		len = 900;
	packet_check_eom();
	if (type == SSH2_MSG_CHANNEL_FAILURE)
		fatal("Request for subsystem '%.*s' failed on channel %d",
		    len, (u_char *)buffer_ptr(&command), id);
}

void
client_global_request_reply_fwd(int type, u_int32_t seq, void *ctxt)
{
	int i;

	i = client_global_request_id++;
	if (i >= options.num_remote_forwards)
		return;
	debug("remote forward %s for: listen %d, connect %s:%d",
	    type == SSH2_MSG_REQUEST_SUCCESS ? "success" : "failure",
	    options.remote_forwards[i].listen_port,
	    options.remote_forwards[i].connect_host,
	    options.remote_forwards[i].connect_port);
	if (type == SSH2_MSG_REQUEST_FAILURE)
		log("Warning: remote port forwarding failed for listen port %d",
		    options.remote_forwards[i].listen_port);
}

static
void
ssh_session2_setlocale(int id)
{
	char *loc;

#define remote_setlocale(envvar) \
	if ((loc = getenv(envvar)) != NULL) {\
		channel_request_start(id, "env", 0);\
		debug2("Sent request for environment variable %s=%s", envvar, \
			loc); \
		packet_put_cstring(envvar);\
		packet_put_cstring(loc);\
		packet_send();\
	}

	remote_setlocale("LANG")

	remote_setlocale("LC_CTYPE")
	remote_setlocale("LC_COLLATE")
	remote_setlocale("LC_TIME")
	remote_setlocale("LC_NUMERIC")
	remote_setlocale("LC_MONETARY")
	remote_setlocale("LC_MESSAGES")

	remote_setlocale("LC_ALL")
}

/* request pty/x11/agent/tcpfwd/shell for channel */
static void
ssh_session2_setup(int id, void *arg)
{
	int len;
	const char *display;
	int interactive = 0;
	struct termios tio;

	debug("ssh_session2_setup: id %d", id);

	ssh_session2_setlocale(id);

	if (tty_flag) {
		struct winsize ws;
		char *cp;
		cp = getenv("TERM");
		if (!cp)
			cp = "";
		/* Store window size in the packet. */
		if (ioctl(fileno(stdin), TIOCGWINSZ, &ws) < 0)
			memset(&ws, 0, sizeof(ws));

		channel_request_start(id, "pty-req", 0);
		packet_put_cstring(cp);
		packet_put_int(ws.ws_col);
		packet_put_int(ws.ws_row);
		packet_put_int(ws.ws_xpixel);
		packet_put_int(ws.ws_ypixel);
		tio = get_saved_tio();
		tty_make_modes(/*ignored*/ 0, &tio);
		packet_send();
		interactive = 1;
		/* XXX wait for reply */
	}

	display = getenv("DISPLAY");
	if (options.forward_x11 && display != NULL) {
		char *proto, *data;
		/* Get reasonable local authentication information. */
		client_x11_get_proto(display, options.xauth_location,
		    options.forward_x11_trusted, &proto, &data);
		/* Request forwarding with authentication spoofing. */
		debug("Requesting X11 forwarding with authentication spoofing.");
		x11_request_forwarding_with_spoofing(id, display, proto, data);
		interactive = 1;
		/* XXX wait for reply */
	}

	check_agent_present();
	if (options.forward_agent) {
		debug("Requesting authentication agent forwarding.");
		channel_request_start(id, "auth-agent-req@openssh.com", 0);
		packet_send();
	}

	len = buffer_len(&command);
	if (len > 0) {
		if (len > 900)
			len = 900;
		if (subsystem_flag) {
			debug("Sending subsystem: %.*s", len, (u_char *)buffer_ptr(&command));
			channel_request_start(id, "subsystem", /*want reply*/ 1);
			/* register callback for reply */
			/* XXX we assume that client_loop has already been called */
			dispatch_set(SSH2_MSG_CHANNEL_FAILURE, &client_subsystem_reply);
			dispatch_set(SSH2_MSG_CHANNEL_SUCCESS, &client_subsystem_reply);
		} else {
			debug("Sending command: %.*s", len, (u_char *)buffer_ptr(&command));
			channel_request_start(id, "exec", 0);
		}
		packet_put_string(buffer_ptr(&command), buffer_len(&command));
		packet_send();
	} else {
		channel_request_start(id, "shell", 0);
		packet_send();
	}

	packet_set_interactive(interactive);
}

/* open new channel for a session */
static int
ssh_session2_open(void)
{
	Channel *c;
	int window, packetmax, in, out, err;

	if (stdin_null_flag) {
		in = open(_PATH_DEVNULL, O_RDONLY);
	} else {
		in = dup(STDIN_FILENO);
	}
	out = dup(STDOUT_FILENO);
	err = dup(STDERR_FILENO);

	if (in < 0 || out < 0 || err < 0)
		fatal("dup() in/out/err failed");

	/* enable nonblocking unless tty */
	if (!isatty(in))
		set_nonblock(in);
	if (!isatty(out))
		set_nonblock(out);
	if (!isatty(err))
		set_nonblock(err);

	window = CHAN_SES_WINDOW_DEFAULT;
	packetmax = CHAN_SES_PACKET_DEFAULT;
	if (tty_flag) {
		window >>= 1;
		packetmax >>= 1;
	}
	c = channel_new(
	    "session", SSH_CHANNEL_OPENING, in, out, err,
	    window, packetmax, CHAN_EXTENDED_WRITE,
	    xstrdup("client-session"), /*nonblock*/0);

	debug3("ssh_session2_open: channel_new: %d", c->self);

	channel_send_open(c->self);
	if (!no_shell_flag)
		channel_register_confirm(c->self, ssh_session2_setup);

	return c->self;
}

static int
ssh_session2(void)
{
	int id = -1;

	/* XXX should be pre-session */
	ssh_init_forwarding();

	if (!no_shell_flag || (datafellows & SSH_BUG_DUMMYCHAN))
		id = ssh_session2_open();

	/* If requested, let ssh continue in the background. */
	if (fork_after_authentication_flag)
		client_daemonize();

	return (client_loop(tty_flag, tty_flag ?
	    options.escape_char : SSH_ESCAPECHAR_NONE, id));
}

static void
load_public_identity_files(void)
{
	char *filename;
	int i = 0;
	Key *public;

	for (; i < options.num_identity_files; i++) {
		filename = tilde_expand_filename(options.identity_files[i],
		    original_real_uid);
		public = key_load_public(filename, NULL, SSH_KMF_IGNORE_CERT, NULL);
		debug("Identity file/URI '%s' pubkey type %s", filename,
		    public ? key_ssh_name(public) : "UNKNOWN");
		xfree(options.identity_files[i]);
		options.identity_files[i] = filename;
		options.public_identity_keys[i] = public;
	}
}

/*
 * Connects to the given host using rsh(or prints an error message and exits
 * if rsh is not available).  This function never returns.
 */
static void
rsh_connect(char *host, char *user, Buffer * command)
{
	char *args[10];
	int i;

	log("Using rsh.  WARNING: Connection will not be encrypted.");
	/* Build argument list for rsh. */
	i = 0;
	args[i++] = _PATH_RSH;
	/* host may have to come after user on some systems */
	args[i++] = host;
	if (user) {
		args[i++] = "-l";
		args[i++] = user;
	}
	if (buffer_len(command) > 0) {
		buffer_append(command, "\0", 1);
		args[i++] = buffer_ptr(command);
	}
	args[i++] = NULL;
	if (debug_flag) {
		for (i = 0; args[i]; i++) {
			if (i != 0)
				(void) fprintf(stderr, " ");
			(void) fprintf(stderr, "%s", args[i]);
		}
		(void) fprintf(stderr, "\n");
	}
	(void) execv(_PATH_RSH, args);
	perror(_PATH_RSH);
	exit(1);
}
