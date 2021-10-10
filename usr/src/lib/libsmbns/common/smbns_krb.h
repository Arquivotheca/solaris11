/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SMBSRV_SMB_KRB_H
#define	_SMBSRV_SMB_KRB_H

#include <kerberosv5/krb5.h>
#include <kt_solaris.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	SMBNS_KRB5_KEYTAB_ENV	"KRB5_KTNAME"
#define	SMBNS_KRB5_KEYTAB	"/etc/krb5/krb5.keytab"

#define	SMB_PN_SPN_ATTR			0x0001 /* w/o REALM portion */
#define	SMB_PN_UPN_ATTR			0x0002 /* w/  REALM */
#define	SMB_PN_KEYTAB_ENTRY		0x0004 /* w/  REALM */
#define	SMB_PN_SALT			0x0008 /* w/  REALM */
/* Service principals that are required for Kerberos user authentication */
#define	SMB_PN_KERBERIZED_CIFS		0x0010
/*
 * Service principals that are required when server's extended security is
 * disabled
 */
#define	SMB_PN_NONKERBERIZED_CIFS	0x0020

#define	SMB_PN_SVC_HOST			"host"
#define	SMB_PN_SVC_NFS			"nfs"
#define	SMB_PN_SVC_HTTP			"HTTP"
#define	SMB_PN_SVC_ROOT			"root"
#define	SMB_PN_SVC_CIFS			"cifs"
#define	SMB_PN_SVC_DNS			"DNS"

/* Assign an identifier for each principal name format */
typedef enum smb_krb5_pn_id {
	SMB_KRB5_PN_ID_SALT,
	SMB_KRB5_PN_ID_HOST_FQHN,
	SMB_KRB5_PN_ID_HOST_NETBIOS,
	SMB_KRB5_PN_ID_CIFS_FQHN,
	SMB_KRB5_PN_ID_CIFS_NETBIOS,
	SMB_KRB5_PN_ID_SAM_ACCT,
	SMB_KRB5_PN_ID_NFS_FQHN,
	SMB_KRB5_PN_ID_HTTP_FQHN,
	SMB_KRB5_PN_ID_ROOT_FQHN
} smb_krb5_pn_id_t;

/*
 * A principal name can be constructed based on the following:
 *
 * p_id    - identifier for a principal name.
 * p_svc   - service with which the principal is associated.
 * p_flags - usage of the principal is identified - whether it can be used as a
 *           SPN attribute, UPN attribute, or/and keytab entry, etc.
 */
typedef struct smb_krb5_pn {
	smb_krb5_pn_id_t	p_id;
	char			*p_svc;
	uint32_t		p_flags;
} smb_krb5_pn_t;

/*
 * A set of principal names
 *
 * ps_cnt - the number of principal names in the array.
 * ps_set - An array of principal names terminated with a NULL pointer.
 */
typedef struct smb_krb5_pn_set {
	uint32_t	s_cnt;
	char		**s_pns;
} smb_krb5_pn_set_t;

char *smb_krb5_get_pn_by_id(smb_krb5_pn_id_t, uint32_t, const char *);
uint32_t smb_krb5_get_pn_set(smb_krb5_pn_set_t *, uint32_t, char *);
void smb_krb5_free_pn_set(smb_krb5_pn_set_t *);

char *smb_krb5_kt_getpath(void);
int smb_krb5_kt_update_adjoin(krb5_context, char *, krb5_kvno, char *,
    uint32_t);
int smb_krb5_kt_update_startup(char *, krb5_kvno, uint32_t);
boolean_t smb_krb5_kt_find(smb_krb5_pn_id_t, const char *, char *);

int smb_kinit(char *, char *);
int smb_krb5_setpwd(krb5_context, const char *, char *);
char *smb_krb5_domain2realm(const char *);

#ifdef __cplusplus
}
#endif

#endif /* _SMBSRV_SMB_KRB_H */
