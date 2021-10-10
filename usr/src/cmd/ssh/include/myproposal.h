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
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*	$OpenBSD: myproposal.h,v 1.14 2002/04/03 09:26:11 markus Exp $	*/

#ifndef	_MYPROPOSAL_H
#define	_MYPROPOSAL_H

#ifdef __cplusplus
extern "C" {
#endif


#define	KEX_DEFAULT_KEX			"diffie-hellman-group-exchange-sha1," \
					"diffie-hellman-group14-sha1," \
					"diffie-hellman-group1-sha1"

#define	KEX_DEFAULT_PK_ALG		"ssh-rsa,ssh-dss"

/*
 * Keep CBC modes in the back of the client default cipher list for backward
 * compatibility but remove them from the server side because there are some
 * potential security issues with those modes regarding SSH protocol version 2.
 * Since the client is the one who picks the cipher from the list offered by the
 * server the only way to force the client not to use CBC modes is not to
 * advertise those at all. Note that we still support all such CBC modes in the
 * server code, this is about the default server cipher list only. The list can
 * be changed in the Ciphers option in the sshd_config(4) file.
 *
 * Note that the ordering of ciphers on the server side is not relevant but we
 * must do it properly even here so that we can use the macro for the client
 * list as well.
 */
#define	KEX_DEFAULT_SERVER_ENCRYPT	"aes128-ctr,aes192-ctr,aes256-ctr," \
					"arcfour128,arcfour256,arcfour"

#define	KEX_DEFAULT_CLIENT_ENCRYPT	KEX_DEFAULT_SERVER_ENCRYPT \
					",aes128-cbc,aes192-cbc,aes256-cbc," \
					"blowfish-cbc,3des-cbc"

#define	KEX_DEFAULT_MAC			"hmac-md5,hmac-sha1,hmac-sha1-96," \
					"hmac-md5-96"

#define	KEX_DEFAULT_COMP		"none,zlib"
#define	KEX_DEFAULT_LANG		""


static char *my_srv_proposal[PROPOSAL_MAX] = {
	KEX_DEFAULT_KEX,
	KEX_DEFAULT_PK_ALG,
	KEX_DEFAULT_SERVER_ENCRYPT,
	KEX_DEFAULT_SERVER_ENCRYPT,
	KEX_DEFAULT_MAC,
	KEX_DEFAULT_MAC,
	KEX_DEFAULT_COMP,
	KEX_DEFAULT_COMP,
	KEX_DEFAULT_LANG,
	KEX_DEFAULT_LANG
};

static char *my_clnt_proposal[PROPOSAL_MAX] = {
	KEX_DEFAULT_KEX,
	KEX_DEFAULT_PK_ALG,
	KEX_DEFAULT_CLIENT_ENCRYPT,
	KEX_DEFAULT_CLIENT_ENCRYPT,
	KEX_DEFAULT_MAC,
	KEX_DEFAULT_MAC,
	KEX_DEFAULT_COMP,
	KEX_DEFAULT_COMP,
	KEX_DEFAULT_LANG,
	KEX_DEFAULT_LANG
};

#ifdef __cplusplus
}
#endif

#endif /* _MYPROPOSAL_H */
