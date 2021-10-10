/*
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
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
RCSID("$OpenBSD: auth2-pubkey.c,v 1.2 2002/05/31 11:35:15 markus Exp $");

#include "ssh2.h"
#include "xmalloc.h"
#include "packet.h"
#include "buffer.h"
#include "log.h"
#include "servconf.h"
#include "compat.h"
#include "bufaux.h"
#include "auth.h"
#include "key.h"
#include "pathnames.h"
#include "uidswap.h"
#include "auth-options.h"
#include "canohost.h"
#include "kmf.h"

#ifdef USE_PAM
#include <security/pam_appl.h>
#include "auth-pam.h"
#endif /* USE_PAM */

/* import */
extern ServerOptions options;
extern u_char *session_id2;
extern int session_id2_len;

static void
userauth_pubkey(Authctxt *authctxt)
{
	Buffer b;
	Key *key = NULL;
	char *pkalg;
	u_char *pkblob, *sig;
	u_int alen, blen, slen;
	int have_sig, pktype;
	int authenticated = 0;

	if (!authctxt || !authctxt->method)
		fatal("%s: missing context", __func__);

	/*
	 * SSH_MSG_USERAUTH_REQUEST may not contain a signature because the
	 * packet may serve as a query whether the server accepts the public key
	 * at all, thus saving the client CPU cycles if not accepted. See RFC
	 * 4252 for more information.
	 */
	have_sig = packet_get_char();
	if (datafellows & SSH_BUG_PKAUTH) {
		debug2("userauth_pubkey: SSH_BUG_PKAUTH");
		/* no explicit pkalg given */
		pkblob = packet_get_string(&blen);
		buffer_init(&b);
		buffer_append(&b, pkblob, blen);
		/* so we have to extract the pkalg from the pkblob */
		pkalg = buffer_get_string(&b, &alen);
		buffer_free(&b);
	} else {
		pkalg = packet_get_string(&alen);
		pkblob = packet_get_string(&blen);
	}
	pktype = key_type_from_name(pkalg);
	if (pktype == KEY_UNSPEC) {
		/* this is perfectly legal */
		log("userauth_pubkey: unsupported public key algorithm: %s",
		    pkalg);
		goto done;
	}
	key = key_from_blob(pkblob, blen);
	if (key == NULL) {
		error("userauth_pubkey: cannot decode key: %s", pkalg);
		goto done;
	}
	if (key->type != pktype) {
		error("userauth_pubkey: type mismatch for decoded key "
		    "(received %d, expected %d)", key->type, pktype);
		goto done;
	}

	/*
	 * If X.509 pubkey auth method is used, check whether the user names
	 * match. Validation of the certificate itself is done later in
	 * user_key_allowed().
	 */
	if (key->type == KEY_X509_RSA || key->type == KEY_X509_DSS) {
		KMF_RETURN rv;
		KMF_DATA name, mapped;

		name.Length = strlen(authctxt->user);
		name.Data = (unsigned char *)xstrdup(authctxt->user);

		/* Safety belt if mapped is not filled out. */
		mapped.Length = 0;
		mapped.Data = NULL;
		rv = kmf_match_cert_to_name(kmf_global_handle,
		    &key->kmf_key->cert.certificate, &name, &mapped);
		if (rv == KMF_ERR_NAME_NOT_MATCHED) {
			error("Usernames from the certificate and "
			    "the client packet do not match: %s != %s",
			    mapped.Data, name.Data);
			xfree(name.Data);
			kmf_free_data(&mapped);
			goto done;
		/* Other than the certs-do-not-match error. */
		} else if (rv != KMF_OK) {
			debug3("Username from the packet is '%s', cert was "
			    "mapped to '%s'.", authctxt->user, mapped.Data);
			ssh_kmf_error(kmf_global_handle,
			    "kmf_match_cert_to_name", rv);
			xfree(name.Data);
			kmf_free_data(&mapped);
			goto done;
		}
		xfree(name.Data);
		kmf_free_data(&mapped);
	}

	/* Detect and count abandonment */
	if (authctxt->method->method_data) {
		Key	*prev_key;
		unsigned char	*prev_pkblob;
		int	 prev_blen;

		/*
		 * Check for earlier test of a key that was allowed but
		 * not followed up with a pubkey req for the same pubkey
		 * and with a signature.
		 */
		prev_key = authctxt->method->method_data;
		if ((prev_blen = key_to_blob(prev_key,
			    &prev_pkblob, NULL))) {
			if (prev_blen != blen ||
			    memcmp(prev_pkblob, pkblob, blen) != 0) {
				authctxt->method->abandons++;
				authctxt->method->attempts++;
			}
		}
		key_free(prev_key);
		authctxt->method->method_data = NULL;
	}

	if (have_sig) {
		debug("We received a signature in the user auth packet.");
		sig = packet_get_string(&slen);
		packet_check_eom();
		buffer_init(&b);
		if (datafellows & SSH_OLD_SESSIONID) {
			buffer_append(&b, session_id2, session_id2_len);
		} else {
			buffer_put_string(&b, session_id2, session_id2_len);
		}
		/* reconstruct packet */
		buffer_put_char(&b, SSH2_MSG_USERAUTH_REQUEST);
		buffer_put_cstring(&b, authctxt->user);
		buffer_put_cstring(&b,
		    datafellows & SSH_BUG_PKSERVICE ?
		    "ssh-userauth" :
		    authctxt->service);
		if (datafellows & SSH_BUG_PKAUTH) {
			buffer_put_char(&b, have_sig);
		} else {
			buffer_put_cstring(&b, "publickey");
			buffer_put_char(&b, have_sig);
			buffer_put_cstring(&b, pkalg);
		}
		buffer_put_string(&b, pkblob, blen);
#ifdef DEBUG_PK
		buffer_dump(&b);
#endif
		/* test for correct signature */
		if (user_key_allowed(authctxt->pw, key) &&
		    key_verify(key, sig, slen, buffer_ptr(&b),
		    buffer_len(&b)) == 1) {
			authenticated = 1;
		}
		authctxt->method->postponed = 0;
		buffer_free(&b);
		xfree(sig);
	} else {
		debug("Test whether the public key is acceptable.");
		packet_check_eom();

		/* XXX fake reply and always send PK_OK ? */
		/*
		 * XXX this allows testing whether a user is allowed
		 * to login: if you happen to have a valid pubkey this
		 * message is sent. the message is NEVER sent at all
		 * if a user is not allowed to login. is this an
		 * issue? -markus
		 */
		if (user_key_allowed(authctxt->pw, key)) {
			packet_start(SSH2_MSG_USERAUTH_PK_OK);
			packet_put_string(pkalg, alen);
			packet_put_string(pkblob, blen);
			packet_send();
			packet_write_wait();
			authctxt->method->postponed = 1;
			/*
			 * Remember key that was tried so we can
			 * correctly detect abandonment.  See above.
			 */
			authctxt->method->method_data = (void *) key;
			key = NULL;
		}
	}
	if (authenticated != 1)
		auth_clear_options();

done:
	/*
	 * XXX TODO: add config options for specifying users for whom
	 * this userauth is insufficient and what userauths may
	 * continue.
	 */
#ifdef USE_PAM
	if (authenticated) {
		if (!do_pam_non_initial_userauth(authctxt))
			authenticated = 0;
	}
#endif /* USE_PAM */

	debug2("userauth_pubkey: authenticated %d pkalg %s", authenticated, pkalg);
	if (key != NULL)
		key_free(key);
	xfree(pkalg);
	xfree(pkblob);
#ifdef HAVE_CYGWIN
	if (check_nt_auth(0, authctxt->pw) == 0)
		return;
#endif
	if (authenticated)
		authctxt->method->authenticated = 1;
}

/* return 1 if user allows given key */
static int
user_key_allowed2(struct passwd *pw, Key *key, char *file)
{
	char line[8192];
	int found_key = 0;
	FILE *f;
	u_long linenum = 0;
	struct stat st;
	Key *found;
	char *fp;

	if (pw == NULL)
		return 0;

	/* Temporarily use the user's uid. */
	temporarily_use_uid(pw);

	debug("trying public key file %s", file);

	/* Fail quietly if file does not exist */
	if (stat(file, &st) < 0) {
		/* Restore the privileged uid. */
		restore_uid();
		return 0;
	}
	/* Open the file containing the authorized keys. */
	f = fopen(file, "r");
	if (!f) {
		/* Restore the privileged uid. */
		restore_uid();
		return 0;
	}
	if (options.strict_modes &&
	    secure_filename(f, file, pw, line, sizeof(line)) != 0) {
		(void) fclose(f);
		log("Authentication refused: %s", line);
		restore_uid();
		return 0;
	}

	found_key = 0;
	found = key_new(key->type);

	while (fgets(line, sizeof(line), f)) {
		char *cp, *options = NULL;
		linenum++;
		/* Skip leading whitespace, empty and comment lines. */
		for (cp = line; *cp == ' ' || *cp == '\t'; cp++)
			;
		if (!*cp || *cp == '\n' || *cp == '#')
			continue;

		if (key_read(found, &cp) != 1) {
			/* no key?  check if there are options for this key */
			int quoted = 0;
			debug2("user_key_allowed: check options: '%s'", cp);
			options = cp;
			for (; *cp && (quoted || (*cp != ' ' && *cp != '\t')); cp++) {
				if (*cp == '\\' && cp[1] == '"')
					cp++;	/* Skip both */
				else if (*cp == '"')
					quoted = !quoted;
			}
			/* Skip remaining whitespace. */
			for (; *cp == ' ' || *cp == '\t'; cp++)
				;
			if (key_read(found, &cp) != 1) {
				debug2("user_key_allowed: advance: '%s'", cp);
				/* still no key?  advance to next line*/
				continue;
			}
		}
		if (key_equal(found, key) &&
		    auth_parse_options(pw, options, file, linenum) == 1) {
			found_key = 1;
			debug("matching key found: file %s, line %lu",
			    file, linenum);
			fp = key_fingerprint(found, SSH_FP_MD5, SSH_FP_HEX);
			verbose("Found matching %s key: %s",
			    key_type(found), fp);
			xfree(fp);
			break;
		}
	}
	restore_uid();
	(void) fclose(f);
	key_free(found);
	if (!found_key)
		debug2("key not found");
	return found_key;
}

/* check whether given key is in .ssh/authorized_keys* */
int
user_key_allowed(struct passwd *pw, Key *key)
{
	int success, ret;
	char *file, *subj = NULL;

	if (pw == NULL)
		return 0;

	/*
	 * Special case first. Let's check whether the user certificate is valid
	 * according to KMF policy.
	 */
	if (key->type == KEY_X509_RSA || key->type == KEY_X509_DSS) {
		debug3("Checking validity of the client certificate.");

		/*
		 * For the X.509 pubkey user authentication we do not use
		 * authorized_keys. User authentication is restartable, users
		 * should use plain public key authentication method if they can
		 * not set up a PKI.
		 */
		ret = ssh_kmf_is_cert_self_signed(kmf_global_handle,
		    &key->kmf_key->cert.certificate, &subj, NULL);

		switch (ret) {
		case SSH_KMF_CORRUPTED_CERT:
			/* The only case when 'subj' is not allocated. */
			error("Corrupted user certificate.");
			return (0);
		case SSH_KMF_SELF_SIGNED_CERT:
			error("User certificate '%s' is self-signed which "
			    "is not supported.", subj);
			free(subj);
			return (0);
		case SSH_KMF_NOT_SELF_SIGNED_CERT:
			break;
		default:
			free(subj);
			fatal("Invalid return code from "
			    "ssh_kmf_is_cert_self_signed().");
		}

		free(subj);
		success = ssh_kmf_validate_cert(key->kmf_key,
		    options.trusted_anchor_keystore);
		
		/*
		 * In case of an error a more specific error might have been
		 * already printed inside of ssh_kmf_validate_cert. For example,
		 * the function might have reported an expired certificate.
		 */
		switch (success) {
		case SSH_KMF_CERT_VALIDATED:
			debug3("User certificate validated.");
			return (1);
		case SSH_KMF_CERT_VALIDATION_ERROR:
			error("Validation of the client certificate "
			    "failed.");
			return (0);
		case SSH_KMF_MISSING_TA:
			error("Trusted anchor not found. Can not validate "
			    "user certificate.");
			return (0);
		default:
			fatal("Invalid return code from "
			    "ssh_kmf_validate_cert (%d).", success);
		}
	}

	file = authorized_keys_file(pw);
	success = user_key_allowed2(pw, key, file);
	xfree(file);
	if (success)
		return success;

	/* try suffix "2" for backward compat, too */
	file = authorized_keys_file2(pw);
	success = user_key_allowed2(pw, key, file);
	xfree(file);
	return success;
}

static
void
userauth_pubkey_abandon(Authctxt *authctxt, Authmethod *method)
{
	if (!authctxt || !method)
		return;

	if (method->method_data) {
		method->abandons++;
		method->attempts++;
		key_free((Key *) method->method_data);
		method->method_data = NULL;
	}
}

Authmethod method_pubkey = {
	"publickey",
	&options.pubkey_authentication,
	userauth_pubkey,
	userauth_pubkey_abandon,
	NULL, NULL,	    /* method data and hist data */
	0,		    /* not initial userauth */
	0, 0, 0,	    /* counters */
	0, 0, 0, 0, 0, 0    /* state */
};
