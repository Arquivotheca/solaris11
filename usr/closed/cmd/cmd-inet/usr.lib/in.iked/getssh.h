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
 * Copyright (c) 2001, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_IKED_COMMON_GETSSH_H
#define	_IKED_COMMON_GETSSH_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <ike/isakmp.h>

uint16_t sshencr_to_sadb(int sshval);
uint16_t sadb_to_sshencr(int sshval);

uint16_t sshencrstr_to_sadb(const char *str);

char *sshencr_to_string(int sshval);

uint16_t get_ssh_encralg(SshIkeNegotiation nego);

uint16_t sshauth_to_sadb(int sshval);
uint16_t sadb_to_sshauth(int sshval);

uint16_t sshauthstr_to_sadb(const char *str);

char *sshauth_to_string(int sshval);

uint16_t get_ssh_authalg(SshIkeNegotiation nego);

uint16_t sshprfstr_to_const(const char *name);

uint16_t get_ssh_prf(SshIkeNegotiation nego);

uint16_t get_ssh_cipherkeylen(SshIkeNegotiation nego);

uint16_t get_ssh_dhgroup(SshIkeNegotiation nego);

uint32_t get_ssh_p1state(SshIkeNegotiation nego);

uint32_t sshidtype_to_sadb(int sshval);

char *sshidtype_to_string(int sshval);

uint32_t sadb_to_sshidtype(int sadbval);

uint32_t get_ssh_max_kbytes(SshIkeNegotiation nego);

uint32_t get_ssh_kbytes(SshIkeNegotiation nego);

uint16_t get_ssh_skeyid_len(SshIkeNegotiation nego);

void get_ssh_skeyid(SshIkeNegotiation nego, int len, uint8_t *p);

uint16_t get_ssh_skeyid_d_len(SshIkeNegotiation nego);

void get_ssh_skeyid_d(SshIkeNegotiation nego, int len, uint8_t *p);

uint16_t get_ssh_skeyid_a_len(SshIkeNegotiation nego);

void get_ssh_skeyid_a(SshIkeNegotiation nego, int len, uint8_t *p);

uint16_t get_ssh_skeyid_e_len(SshIkeNegotiation nego);

void get_ssh_skeyid_e(SshIkeNegotiation nego, int len, uint8_t *p);

uint16_t get_ssh_encrkey_len(SshIkeNegotiation nego);

void get_ssh_encrkey(SshIkeNegotiation nego, int len, uint8_t *p);

uint16_t get_ssh_iv_len(SshIkeNegotiation nego);

void get_ssh_iv(SshIkeNegotiation nego, int len, uint8_t *p);

SshIkePMPhaseI get_ssh_pminfo(SshIkeNegotiation nego);

#ifdef	__cplusplus
}
#endif

#endif	/* _IKED_COMMON_GETSSH_H */
