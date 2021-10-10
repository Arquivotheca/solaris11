/*

  sshmp-compat.h

  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created Tue Jan  2 22:35:10 2001.

  */

#ifndef SSHMP_COMPAT_H
#define SSHMP_COMPAT_H

/* !!!NOTE!!!

   The backward compatibility is not 100 %, and thus at places it is
   possible that a program will not compile properly. In such cases
   we suggest that the programmer uses the new interface to fix the
   problem, rather than extending the backward compatibility.

   The compatibility features may at some point be removed from the
   library (at least from the default compilation), and thus writing
   new code with the old interface is not recommended.
*/

/* This file contains compatibility definitions for a large class of
   functions. Observe that many specific functions (and function
   families) are not supported through this interface.
*/

/* Type definitions. */

typedef SshMPIntegerStruct          SshMPIntStruct;
typedef SshMPInteger                SshMPInt;

typedef SshMPIntegerStruct          SshMPIntC[1];
typedef SshMPIntegerStruct          SshMPIntegerC[1];

typedef SshSignedWord               SignedSshWord;

#ifdef SSHDIST_MATH_INTMOD
typedef SshMPIntMod                 SshMPIntModQ;
typedef SshMPIntModStruct           SshMPIntModQStruct;
typedef SshMPIntIdeal               SshMPIntModuli;
typedef SshMPIntIdealStruct         SshMPIntModuliStruct;

typedef SshMPIntModPowPrecomp       SshMPPowmBase;
typedef SshMPIntModPowPrecompStruct SshMPPowmBaseStruct;
#endif /* SSHDIST_MATH_INTMOD */

/* Function prototype redefinitions. */

#define ssh_mp_malloc   ssh_mprz_malloc
#define ssh_mp_free     ssh_mprz_free
#define ssh_mp_realloc  ssh_mprz_realloc

#define ssh_mp_init     ssh_mprz_init
#define ssh_mp_clear    ssh_mprz_clear

#define ssh_mp_init_set     ssh_mprz_init_set
#define ssh_mp_init_set_ui  ssh_mprz_init_set_ui
#define ssh_mp_init_set_si  ssh_mprz_init_set_si
#define ssh_mp_init_set_str ssh_mprz_init_set_str

#define ssh_mp_clr_bit  ssh_mprz_clr_bit

#define ssh_mp_get_ui   ssh_mprz_get_ui
#define ssh_mp_get_si   ssh_mprz_get_si
#define ssh_mp_get_bit  ssh_mprz_get_bit

/* A compatibility version of the get string. */
char *ssh_mprz_get_str_compat(char *str,
                              unsigned int base,
                              SshMPIntegerConst op);

#define ssh_mp_get_str  ssh_mprz_get_str_compat
#define ssh_mp_get_size ssh_mprz_get_size

#define ssh_mp_set      ssh_mprz_set
#define ssh_mp_set_ui   ssh_mprz_set_ui
#define ssh_mp_set_si   ssh_mprz_set_si
#define ssh_mp_set_bit  ssh_mprz_set_bit
#define ssh_mp_set_str  ssh_mprz_set_str

#define ssh_mp_scan0(op,bit) ssh_mprz_scan_bit((op), (bit), 0)
#define ssh_mp_scan1(op,bit) ssh_mprz_scan_bit((op), (bit), 1)

#define ssh_mp_out_str  ssh_mprz_out_str

#define ssh_mp_get_buf  ssh_mprz_get_buf
#define ssh_mp_set_buf  ssh_mprz_set_buf

#define ssh_mp_neg      ssh_mprz_neg
#define ssh_mp_abs      ssh_mprz_abs
#define ssh_mp_signum   ssh_mprz_signum

#define ssh_mp_and      ssh_mprz_and
#define ssh_mp_xor      ssh_mprz_xor
#define ssh_mp_or       ssh_mprz_or
#define ssh_mp_com      ssh_mprz_com

#define ssh_mp_cmp      ssh_mprz_cmp
#define ssh_mp_cmp_ui   ssh_mprz_cmp_ui
#define ssh_mp_cmp_si   ssh_mprz_cmp_si

#define ssh_mp_add      ssh_mprz_add
#define ssh_mp_sub      ssh_mprz_sub
#define ssh_mp_add_ui   ssh_mprz_add_ui
#define ssh_mp_sub_ui   ssh_mprz_sub_ui

#define ssh_mp_mul      ssh_mprz_mul
#define ssh_mp_mul_ui   ssh_mprz_mul_ui

#define ssh_mp_square   ssh_mprz_square

#define ssh_mp_div      ssh_mprz_divrem
#define ssh_mp_div_q    ssh_mprz_div
#define ssh_mp_mod      ssh_mprz_mod
#define ssh_mp_div_ui   ssh_mprz_divrem_ui
#define ssh_mp_mod_ui   ssh_mprz_mod_ui
#define ssh_mp_mod_ui2  SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED

#define ssh_mp_mod_2exp ssh_mprz_mod_2exp
#define ssh_mp_div_2exp ssh_mprz_div_2exp
#define ssh_mp_mul_2exp ssh_mprz_mul_2exp

#define ssh_mp_rand     ssh_mprz_rand

#define ssh_mp_pow      ssh_mprz_pow
#define ssh_mp_gcd      ssh_mprz_gcd
#define ssh_mp_gcdext   ssh_mprz_gcdext

#define ssh_mp_invert   ssh_mprz_invert

#define ssh_mp_kronecker ssh_mprz_kronecker
#define ssh_mp_jacobi    ssh_mprz_kronecker
#define ssh_mp_legendre  ssh_mprz_kronecker

#define ssh_mp_mod_sqrt  ssh_mprz_mod_sqrt

#define ssh_mp_sqrt      ssh_mprz_sqrt

#define ssh_mp_is_perfect_square ssh_mprz_is_perfect_square

#define ssh_mp_dump      SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED

/* Remark. Following routines doesn't actually work without
   SSHDIST_MATH_INTMOD. */
#define ssh_mp_powm_naive            SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED
#define ssh_mp_powm_bsw              SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED
#define ssh_mp_powm_naive_mont       SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED
#define ssh_mp_powm_bsw_mont         SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED
#define ssh_mp_powm_naive_mont_gg    SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED

#define ssh_mp_powm_naive_ui         SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED
#define ssh_mp_powm_naive_mont_ui    SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED
#define ssh_mp_powm_naive_mont_base2 SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED

#define ssh_mp_powm_naive_expui      SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED

#define ssh_mp_powm_with_base_dv     SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED

#define ssh_mp_powm_with_base_init(__g,__m,__o,__b) \
  ssh_mprz_powm_precomp_init(__b,__g,__m,__o)
#define ssh_mp_powm_with_base_clear  ssh_mprz_powm_precomp_clear

#define ssh_mp_powm           ssh_mprz_powm
#define ssh_mp_powm_ui        ssh_mprz_powm_ui_g
#define ssh_mp_powm_base2(__r,__e,__p) ssh_mprz_powm_ui_g(__r,2,__e,__p)
#define ssh_mp_powm_expui     ssh_mprz_powm_ui_exp
#define ssh_mp_powm_with_base ssh_mprz_powm_with_precomp
#define ssh_mp_powm_gg        ssh_mprz_powm_gg

#define ssh_mp_is_probable_prime ssh_mprz_is_probable_prime

#define ssh_mp_next_prime ssh_mprz_next_prime

#ifdef SSHDIST_MATH_INTMOD
#define ssh_mpm_init_m  ssh_mprzm_init_ideal
#define ssh_mpm_clear_m ssh_mprzm_clear_ideal

#define ssh_mpm_init    ssh_mprzm_init
#define ssh_mpm_clear   ssh_mprzm_clear

#define ssh_mpm_set_mp  ssh_mprzm_set_mprz
#define ssh_mpm_set     ssh_mprzm_set
#define ssh_mpm_set_ui  ssh_mprzm_set_ui

#define ssh_mp_set_mpm  ssh_mprz_set_mprzm
#define ssh_mp_set_m    ssh_mprz_set_mprzm_ideal

#define ssh_mpm_cmp     ssh_mprzm_cmp
#define ssh_mpm_cmp_ui  ssh_mprzm_cmp_ui

#define ssh_mpm_add     ssh_mprzm_add
#define ssh_mpm_sub     ssh_mprzm_sub

#define ssh_mpm_mul     ssh_mprzm_mul
#define ssh_mpm_mul_ui  ssh_mprzm_mul_ui
#define ssh_mpm_square  ssh_mprzm_square

#define ssh_mpm_div_2exp ssh_mprzm_div_2exp
#define ssh_mpm_mul_2exp ssh_mprzm_mul_2exp

#define ssh_mpm_invert   ssh_mprzm_invert

#define ssh_mpm_kronecker SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED
#define ssh_mpm_gcd       SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED
#define ssh_mpm_sqrt      ssh_mprzm_sqrt

#define ssh_mpm_dump SSH_MP_COMPAT_ERROR_FUNC_NOT_SUPPORTED
#endif /* SSHDIST_MATH_INTMOD */

#endif /* SSHMP_COMPAT_H */
