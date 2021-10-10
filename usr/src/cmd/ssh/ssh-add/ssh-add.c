/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Adds an identity to the authentication server, or removes an identity.
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 *
 * SSH2 implementation,
 * Copyright (c) 2000, 2001 Markus Friedl.  All rights reserved.
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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "includes.h"
RCSID("$OpenBSD: ssh-add.c,v 1.63 2002/09/19 15:51:23 markus Exp $");

#include <openssl/evp.h>

#include "ssh.h"
#include "rsa.h"
#include "log.h"
#include "xmalloc.h"
#include "key.h"
#include "authfd.h"
#include "authfile.h"
#include "pathnames.h"
#include "readpass.h"
#include "misc.h"
#include "kmf.h"

#ifdef HAVE___PROGNAME
extern char *__progname;
#else
char *__progname;
#endif

/* argv0 */
extern char *__progname;

/* Default files to add */
static char *default_files[] = {
	_PATH_SSH_CLIENT_ID_RSA,
	_PATH_SSH_CLIENT_ID_DSA,
	_PATH_SSH_CLIENT_IDENTITY,
	NULL
};

/* Default lifetime (0 == forever) */
static int lifetime = 0;

/* we keep a cache of one passphrases */
static char *pass = NULL;
static void
clear_pass(void)
{
	if (pass) {
		memset(pass, 0, strlen(pass));
		xfree(pass);
		pass = NULL;
	}
}

static int
delete_file(AuthenticationConnection *ac, const char *filename)
{
	Key *public;
	char *comment = NULL;
	int result, ret = -1;

	public = key_load_public(filename, &comment, SSH_KMF_LOAD_CERT, &result);
	if (public == NULL) {
		switch (result) {
		case SSH_KMF_CANNOT_LOAD_OBJECT:
		case SSH_KMF_INVALID_PKCS11_URI:
		case SSH_KMF_MISSING_TOKEN_OR_OBJ:
			error("%s: %s", ssh_pk11_err(result), filename);
			break;
		case SSH_KMF_NOT_PKCS11_URI:
			error("Bad key file %s", filename);
			break;
		default:
			fatal("Invalid return code from key_load_public.");
		}
		return -1;
	}
	if (ssh_remove_identity(ac, public)) {
		fprintf(stderr, gettext("Identity removed: %s (%s)\n"),
			filename, comment);
		ret = 0;
	} else
		fprintf(stderr, gettext("Could not remove identity: %s\n"),
			filename);

	key_free(public);
	xfree(comment);

	return ret;
}

/* Send a request to remove all identities. */
static int
delete_all(AuthenticationConnection *ac)
{
	int ret = -1;

	if (ssh_remove_all_identities(ac, 1))
		ret = 0;
	/* ignore error-code for ssh2 */
	ssh_remove_all_identities(ac, 2);

	if (ret == 0)
		fprintf(stderr, gettext("All identities removed.\n"));
	else
		fprintf(stderr, gettext("Failed to remove all identities.\n"));

	return ret;
}

static int
add_file(AuthenticationConnection *ac, const char *filename)
{
	struct stat st;
	Key *private;
	char *comment = NULL;
	char msg[1024];
	int ret;
	pkcs11_uri_t pkcs11_uri;

	ret = pkcs11_parse_uri(filename, &pkcs11_uri);
	if (ret == PK11_NOT_PKCS11_URI) {
		if (stat(filename, &st) < 0) {
			perror(filename);
			return (-1);
		}
	} else {
		if (ret != PK11_URI_OK) {
			/* It is an incorrectly specified PKCS#11 URI. */
			error("%s: %s",
			    ssh_pk11_err(SSH_KMF_INVALID_PKCS11_URI),
			    filename);
			return (-1);
		}
	}

	/* At first, try empty passphrase */
	private = key_load_private(filename, "", &comment,
	    SSH_KMF_ASK_FOR_TOKEN_PIN);

	/*
	 * Return an error if it was detected as a PKCS#11 URI but we could not
	 * access it.
	 */
	if (ret == PK11_URI_OK && private == NULL) {
		error("%s: %s",
		    ssh_pk11_err(SSH_KMF_CANNOT_LOAD_OBJECT), filename);
		return (-1);
	}

	/*
	 * If we get here we know it is either a valid PKCS#11 URI with an
	 * existing key (ie. 'private' is non-NULL) or the key is supposed to be
	 * in a file in which case we are going to read it now.
	 */
	ret = -1;
	if (comment == NULL)
		comment = xstrdup(filename);
	/* try last */
	if (private == NULL && pass != NULL)
		private = key_load_private(filename, pass, NULL,
		    SSH_KMF_DONT_ASK_FOR_TOKEN_PIN);
	if (private == NULL) {
		/* clear passphrase since it did not work */
		clear_pass();
		snprintf(msg, sizeof msg,
		     gettext("Enter passphrase for %.200s: "), comment);
		for (;;) {
			pass = read_passphrase(msg, RP_ALLOW_STDIN);
			if (strcmp(pass, "") == 0) {
				clear_pass();
				xfree(comment);
				return -1;
			}
			private = key_load_private(filename, pass, &comment,
			    SSH_KMF_DONT_ASK_FOR_TOKEN_PIN);
			if (private != NULL)
				break;
			clear_pass();
			strlcpy(msg, gettext("Bad passphrase, try again: "),
			    sizeof msg);
		}
	}

	if (ssh_add_identity_constrained(ac, private, comment, lifetime)) {
		fprintf(stderr, gettext("Identity added: %s (%s)\n"),
			filename, comment);
		ret = 0;
		if (lifetime != 0)
                        fprintf(stderr,
			    gettext("Lifetime set to %d seconds\n"), lifetime);
	} else if (ssh_add_identity(ac, private, comment)) {
		fprintf(stderr, gettext("Identity added: %s (%s)\n"),
			filename, comment);
		ret = 0;
	} else {
		fprintf(stderr, gettext("Could not add identity: %s\n"),
			filename);
	}

	xfree(comment);
	key_free(private);

	return ret;
}

static int
list_identities(AuthenticationConnection *ac, int do_fp)
{
	Key *key;
	char *comment, *fp;
	int had_identities = 0;
	int version;

	for (version = 1; version <= 2; version++) {
		for (key = ssh_get_first_identity(ac, &comment, version);
		    key != NULL;
		    key = ssh_get_next_identity(ac, &comment, version)) {
			had_identities = 1;
			if (do_fp) {
				fp = key_fingerprint(key, SSH_FP_MD5,
				    SSH_FP_HEX);
				printf("%d %s %s (%s)\n",
				    key_size(key), fp, comment, key_type(key));
				xfree(fp);
			} else {
				if (!key_write(key, stdout))
					fprintf(stderr,
						gettext("key_write failed"));
				fprintf(stdout, " %s\n", comment);
			}
			key_free(key);
			xfree(comment);
		}
	}
	if (!had_identities) {
		printf(gettext("The agent has no identities.\n"));
		return -1;
	}
	return 0;
}

static int
lock_agent(AuthenticationConnection *ac, int lock)
{
	char prompt[100], *p1, *p2;
	int passok = 1, ret = -1;

	strlcpy(prompt, "Enter lock password: ", sizeof(prompt));
	p1 = read_passphrase(prompt, RP_ALLOW_STDIN);
	if (lock) {
		strlcpy(prompt, "Again: ", sizeof prompt);
		p2 = read_passphrase(prompt, RP_ALLOW_STDIN);
		if (strcmp(p1, p2) != 0) {
			fprintf(stderr, gettext("Passwords do not match.\n"));
			passok = 0;
		}
		memset(p2, 0, strlen(p2));
		xfree(p2);
	}
	if (passok && ssh_lock_agent(ac, lock, p1)) {
		if (lock)
			fprintf(stderr, gettext("Agent locked.\n"));
		else
			fprintf(stderr, gettext("Agent unlocked.\n"));
		ret = 0;
	} else {
		if (lock)
			fprintf(stderr, gettext("Failed to lock agent.\n"));
		else
			fprintf(stderr, gettext("Failed to unlock agent.\n"));
	}
	memset(p1, 0, strlen(p1));
	xfree(p1);
	return (ret);
}

static int
do_file(AuthenticationConnection *ac, int deleting, char *file)
{
	if (deleting) {
		if (delete_file(ac, file) == -1)
			return -1;
	} else {
		if (add_file(ac, file) == -1)
			return -1;
	}
	return 0;
}

static void
usage(void)
{
	fprintf(stderr,
		gettext( "Usage: %s [options]\n"
		"Options:\n"
		"  -l          List fingerprints of all identities.\n"
		"  -L          List public key parameters of all identities.\n"
		"  -d          Delete identity.\n"
		"  -D          Delete all identities.\n"
		"  -x          Lock agent.\n"
		"  -X          Unlock agent.\n"
		"  -t life     Set lifetime (seconds) when adding identities.\n"
		), __progname);
}

int
main(int argc, char **argv)
{
	extern char *optarg;
	extern int optind;
	AuthenticationConnection *ac = NULL;
	int i, ch, deleting = 0, ret = 0;

	__progname = get_progname(argv[0]);

	(void) g11n_setlocale(LC_ALL, "");

	init_rng();
	seed_rng();

	SSLeay_add_all_algorithms();

	/* At first, get a connection to the authentication agent. */
	ac = ssh_get_authentication_connection();
	if (ac == NULL) {
		fprintf(stderr, gettext("Could not open a connection "
			    "to your authentication agent.\n"));
		exit(2);
	}

	/*
	 * In case we use PKCS#11 URIs we will need a global KMF handle to
	 * extract certificates returned from the agent. Get it now.
	 */
	ssh_kmf_init(NULL, NULL, NULL, NULL);

	while ((ch = getopt(argc, argv, "lLdDxXe:s:t:")) != -1) {
		switch (ch) {
		case 'l':
		case 'L':
			if (list_identities(ac, ch == 'l' ? 1 : 0) == -1)
				ret = 1;
			goto done;
		case 'x':
		case 'X':
			if (lock_agent(ac, ch == 'x' ? 1 : 0) == -1)
				ret = 1;
			goto done;
		case 'd':
			deleting = 1;
			break;
		case 'D':
			if (delete_all(ac) == -1)
				ret = 1;
			goto done;
		case 't':
			if ((lifetime = convtime(optarg)) == -1) {
				fprintf(stderr, gettext("Invalid lifetime\n"));
				ret = 1;
				goto done;
			}
			break;
		default:
			usage();
			ret = 1;
			goto done;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 0) {
		char buf[MAXPATHLEN];
		struct passwd *pw;
		struct stat st;
		int count = 0;

		if ((pw = getpwuid(getuid())) == NULL) {
			fprintf(stderr, gettext("No user found with uid %u\n"),
			    (u_int)getuid());
			ret = 1;
			goto done;
		}

		for(i = 0; default_files[i]; i++) {
			snprintf(buf, sizeof(buf), "%s/%s", pw->pw_dir,
			    default_files[i]);
			if (stat(buf, &st) < 0)
				continue;
			if (do_file(ac, deleting, buf) == -1)
				ret = 1;
			else
				count++;
		}
		if (count == 0)
			ret = 1;
	} else {
		for(i = 0; i < argc; i++) {
			if (do_file(ac, deleting, argv[i]) == -1)
				ret = 1;
		}
	}
	clear_pass();

done:
	ssh_close_authentication_connection(ac);
	return ret;
}
