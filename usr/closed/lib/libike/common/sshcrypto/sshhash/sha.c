/*

  sha.c

  SHA - Secure Hash Algorithm implementation

  Author: Antti Huima <huima@ssh.fi>
  Kenneth Oksanen <cessu@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

*/

#define SSH_ALLOW_CPLUSPLUS_KEYWORDS

#include "sshincludes.h"
#include "sshasmidioms.h"
#include "sshcrypt.h"
#include "sshhash_i.h"
#include "sha.h"
#include "sshgetput.h"

#define SSH_DEBUG_MODULE "SshSha"

static const unsigned char ssh_encoded_sha_oid1[] =
{0x30,0x21,0x30,0x09,0x06,0x05,0x2b,0x0e, \
 0x03,0x02,0x1a,0x05,0x00,0x04,0x14};

static const int ssh_encoded_sha_oid1_len =
sizeof(ssh_encoded_sha_oid1) / sizeof(unsigned char);

static const unsigned char ssh_encoded_sha_oid2[] =
{0x30,0x1f,0x30,0x07,0x06,0x05,0x2b,0x0e, \
 0x03,0x02,0x1a,0x04,0x14};

static const int ssh_encoded_sha_oid2_len =
sizeof(ssh_encoded_sha_oid2) / sizeof(unsigned char);

/* Define SHA-1 in transparent way. */
const SshHashDefStruct ssh_hash_sha_def =
{
  /* Name of the hash function. */
  "sha1",
  /* Certification status */



  0,
  /* ASN.1 Object identifier */
  "1.3.14.3.2.26",

  /* ISO/IEC dedicated hash identifier. */
  0x33,
  /* Digest size. */
  20,
  /* Input block length. */
  64,
  /* Context size */
  ssh_sha_ctxsize,
  /* Reset function, between long usage of one context. */
  ssh_sha_reset_context,
  /* Update function */
  ssh_sha_update,
  /* Final */
  ssh_sha_final,
  /* Asn1 compare function. */
  ssh_sha_asn1_compare,
  /* Asn1 generate function. */
  ssh_sha_asn1_generate
};

/* Define SHA-1 in transparent way. */
const SshHashDefStruct ssh_hash_sha_96_def =
{
  /* Name of the hash function. */
  "sha1-96",
  /* Certification status */
  0,
  /* ASN.1 Object identifier (not defined) */
  NULL,
  /* ISO/IEC dedicated hash identifier. */
  0, /* None */
  /* Digest size. */
  12,
  /* Input block length. */
  64,
  /* Context size */
  ssh_sha_ctxsize,
  /* Reset function, between long usage of one context. */
  ssh_sha_reset_context,
  /* Update function */
  ssh_sha_update,
  /* Final */
  ssh_sha_96_final,
  /* No ASN1. */
  NULL, NULL
};

/* Define SHA-1 in transparent way. */
const SshHashDefStruct ssh_hash_sha_80_def =
{
  /* Name of the hash function. */
  "sha1-80",
  /* Certification status */
  0,
  /* ASN.1 Object identifier (not defined) */
  NULL,
  /* ISO/IEC dedicated hash identifier. */
  0, /* None */
  /* Digest size. */
  10,
  /* Input block length. */
  64,
  /* Context size */
  ssh_sha_ctxsize,
  /* Reset function, between long usage of one context. */
  ssh_sha_reset_context,
  /* Update function */
  ssh_sha_update,
  /* Final */
  ssh_sha_80_final,
  /* No ASN1. */
  NULL, NULL
};

typedef struct {
  SshUInt32 A, B, C, D, E;
  unsigned char in[64];
  SshUInt32 total_length[2];
} SshSHAContext;


/* Functions are (with nicer notation, to me atleast):

   f1 = xy + ~xz
      = z ^ x(y ^ z)

   f2 = x ^ y ^ z

   f3 = xy + xz + yz
      = x(y + z) + yz

   f4 = x ^ y ^ z.
  */

#ifndef SSH_HAVE_AND_WITH_COMPLEMENT
#define F1(x,y,z)  ((z ^ (x & (y ^ z))) + imm)
#else
/* If we have an and-with-complement insn, then the code below
   executes just as many insns, but has more insn level parallellism
   than the above code. */
#define F1(x,y,z)  (((y & x) | (z & (~x))) + imm)
#endif


#define F2(x,y,z)  ((x ^ y ^ z) + imm)
#define F3(x,y,z)  (((x & (y | z)) | (y & z)) + imm)
#define F4(x,y,z)  ((x ^ y ^ z) + imm)


void ssh_sha_reset_context(void *c)
{
  SshSHAContext *context = c;
  context->A = 0x67452301UL;
  context->B = 0xefcdab89UL;
  context->C = 0x98badcfeUL;
  context->D = 0x10325476UL;
  context->E = 0xc3d2e1f0UL;
  context->total_length[0] = 0;
  context->total_length[1] = 0;
}

size_t ssh_sha_ctxsize()
{
  return sizeof(SshSHAContext);
}

#undef HAVE_SHA
/* Below come various implementations of sha, in a decreasing order of
   precedence.  The first one will be taken, and it shall define the
   macro `HAVE_SHA' so that no alternate
   implementation below it is used. */


#if defined(_MSC_VER) && defined (WIN32) && \
        defined(KERNEL) && !defined(HAVE_SHA)
/* Vesa Suontama: Added !defined(KERNEL), because assembler
   optimization does not work in user mode. (Some calling convention
   related bug maybe...)  Ask Niko when he comes back. */

/* Asm optimization for win32 & msvc. This increases the speed of sha
   about 40-50% on penthiums and penthiums IIs. The code is rather
   messy but in general it works pretty much like the optimized
   C-implementation below. */

#pragma auto_inline(off)

static void sha_transform(SshSHAContext *context, const unsigned char *block)
{
#define PARAM_BLOCK [esp + 80 * 4 + 5 * 4 + 2 * 4]
#define PARAM_CONTEXT [esp + 80 * 4 + 5 * 4 + 1 * 4]
#define W_ADDR(i) [esp + 80 * 4 - (i) * 4]

#ifdef NO_386_COMPAT
#define ROUNDS_1_16(r1, r2, r3, r4, r5, r6, i) \
  __asm mov eax,r3                          \
  __asm mov r6, r1                          \
  __asm xor eax, r4                         \
  __asm rol r6, 5                           \
  __asm and eax, r2                         \
  __asm rol r2, 30                          \
  __asm xor eax, r4                         \
  __asm lea r6, [r6 + eax + 0x5a827999UL]    \
  __asm mov eax, PARAM_BLOCK                \
  __asm add r6, r5                          \
  __asm mov eax, [eax + i * 4]              \
  __asm bswap eax                           \
  __asm mov W_ADDR(i), eax                  \
  __asm add r6, eax
#else
#define ROUNDS_1_16(r1, r2, r3, r4, r5, r6, i) \
  __asm mov eax,r3                          \
  __asm mov r6, r1                          \
  __asm xor eax, r4                         \
  __asm rol r6, 5                           \
  __asm and eax, r2                         \
  __asm rol r2, 30                          \
  __asm xor eax, r4                         \
  __asm lea r6, [r6 + eax + 0x5a827999UL]    \
  __asm mov eax, PARAM_BLOCK                \
  __asm add r6, r5                          \
  __asm mov eax, [eax + i * 4]              \
  __asm rol ax,8                            \
  __asm rol eax, 16                         \
  __asm rol ax, 8                           \
  __asm mov W_ADDR(i), eax                  \
  __asm add r6, eax
#endif


#define ROUNDS_17_20(r1, r2, r3, r4, r5, r6, i) \
  __asm mov eax, r3                         \
  __asm mov r6, r1                          \
  __asm xor eax, r4                         \
  __asm rol r6, 5                           \
  __asm and eax, r2                         \
  __asm rol r2, 30                          \
  __asm xor eax, r4                         \
  __asm lea r6, [r6 + eax + 0x5a827999UL]    \
  __asm mov eax, W_ADDR(i - 3)              \
  __asm xor eax, W_ADDR(i - 8)              \
  __asm add r6, r5                          \
  __asm xor eax, W_ADDR(i - 14)             \
  __asm xor eax, W_ADDR(i - 16)             \
  __asm rol eax, 1                          \
  __asm mov W_ADDR(i), eax                  \
  __asm add r6, eax

#define ROUNDS_21_40(r1, r2, r3, r4, r5, r6, i) \
  __asm mov eax, r2                         \
  __asm mov r6, r1                          \
  __asm xor eax, r3                         \
  __asm rol r6, 5                           \
  __asm xor eax, r4                         \
  __asm lea r6, [r6 + eax + 0x6ed9eba1UL]    \
  __asm mov eax, W_ADDR(i - 3)              \
  __asm rol r2, 30                          \
  __asm xor eax, W_ADDR(i - 8)              \
  __asm xor eax, W_ADDR(i - 14)             \
  __asm add r6, r5                          \
  __asm xor eax, W_ADDR(i - 16)             \
  __asm rol eax, 1                          \
  __asm add r6, eax                         \
  __asm mov W_ADDR(i), eax


#define ROUNDS_41_60(r1, r2, r3, r4, r5, r6, i) \
  __asm mov eax, r3                         \
  __asm mov r6, r3                          \
  __asm or eax, r4                          \
  __asm and r6, r4                          \
  __asm and eax, r2                         \
  __asm or eax, r6                          \
  __asm mov r6, r1                          \
  __asm rol r2, 30                          \
  __asm rol r6, 5                           \
  __asm lea r6, [r6 + eax + 0x8f1bbcdcUL]    \
  __asm mov eax, W_ADDR(i - 3)              \
  __asm xor eax, W_ADDR(i - 8)              \
  __asm add r6, r5                          \
  __asm xor eax, W_ADDR(i - 14)             \
  __asm xor eax, W_ADDR(i - 16)             \
  __asm rol eax, 1                          \
  __asm add r6, eax                         \
  __asm mov W_ADDR(i), eax



#define ROUNDS_61_80(r1, r2, r3, r4, r5, r6, i) \
  __asm mov eax, r2                         \
  __asm mov r6, r1                          \
  __asm xor eax, r3                         \
  __asm rol r6, 5                           \
  __asm xor eax, r4                         \
  __asm lea r6, [r6 + eax + 0xca62c1d6UL]    \
  __asm rol r2, 30                          \
  __asm mov eax, W_ADDR(i - 3)              \
  __asm xor eax, W_ADDR(i - 8)              \
  __asm add r6, r5                          \
  __asm xor eax, W_ADDR(i - 14)             \
  __asm xor eax, W_ADDR(i - 16)             \
  __asm rol eax, 1                          \
  __asm mov W_ADDR(i), eax                  \
  __asm add r6, eax

  __asm push ebp
  __asm sub esp, 4*80

  __asm mov edx, PARAM_CONTEXT
  __asm mov esi, [edx]
  __asm mov ebx, [edx + 4]
  __asm mov ecx, [edx + 8]
  __asm mov edi, [edx + 16]
  __asm mov edx, [edx + 12]

  ROUNDS_1_16(esi, ebx, ecx, edx, edi, ebp,  0);
  ROUNDS_1_16(ebp, esi, ebx, ecx, edx, edi,  1);
  ROUNDS_1_16(edi, ebp, esi, ebx, ecx, edx,  2);
  ROUNDS_1_16(edx, edi, ebp, esi, ebx, ecx,  3);
  ROUNDS_1_16(ecx, edx, edi, ebp, esi, ebx,  4);
  ROUNDS_1_16(ebx, ecx, edx, edi, ebp, esi,  5);
  ROUNDS_1_16(esi, ebx, ecx, edx, edi, ebp,  6);
  ROUNDS_1_16(ebp, esi, ebx, ecx, edx, edi,  7);
  ROUNDS_1_16(edi, ebp, esi, ebx, ecx, edx,  8);
  ROUNDS_1_16(edx, edi, ebp, esi, ebx, ecx,  9);
  ROUNDS_1_16(ecx, edx, edi, ebp, esi, ebx, 10);
  ROUNDS_1_16(ebx, ecx, edx, edi, ebp, esi, 11);
  ROUNDS_1_16(esi, ebx, ecx, edx, edi, ebp, 12);
  ROUNDS_1_16(ebp, esi, ebx, ecx, edx, edi, 13);
  ROUNDS_1_16(edi, ebp, esi, ebx, ecx, edx, 14);
  ROUNDS_1_16(edx, edi, ebp, esi, ebx, ecx, 15);
  ROUNDS_17_20(ecx, edx, edi, ebp, esi, ebx, 16);
  ROUNDS_17_20(ebx, ecx, edx, edi, ebp, esi, 17);
  ROUNDS_17_20(esi, ebx, ecx, edx, edi, ebp, 18);
  ROUNDS_17_20(ebp, esi, ebx, ecx, edx, edi, 19);

  ROUNDS_21_40(edi, ebp, esi, ebx, ecx, edx, 20);
  ROUNDS_21_40(edx, edi, ebp, esi, ebx, ecx, 21);
  ROUNDS_21_40(ecx, edx, edi, ebp, esi, ebx, 22);
  ROUNDS_21_40(ebx, ecx, edx, edi, ebp, esi, 23);
  ROUNDS_21_40(esi, ebx, ecx, edx, edi, ebp, 24);
  ROUNDS_21_40(ebp, esi, ebx, ecx, edx, edi, 25);
  ROUNDS_21_40(edi, ebp, esi, ebx, ecx, edx, 26);
  ROUNDS_21_40(edx, edi, ebp, esi, ebx, ecx, 27);
  ROUNDS_21_40(ecx, edx, edi, ebp, esi, ebx, 28);
  ROUNDS_21_40(ebx, ecx, edx, edi, ebp, esi, 29);
  ROUNDS_21_40(esi, ebx, ecx, edx, edi, ebp, 30);
  ROUNDS_21_40(ebp, esi, ebx, ecx, edx, edi, 31);
  ROUNDS_21_40(edi, ebp, esi, ebx, ecx, edx, 32);
  ROUNDS_21_40(edx, edi, ebp, esi, ebx, ecx, 33);
  ROUNDS_21_40(ecx, edx, edi, ebp, esi, ebx, 34);
  ROUNDS_21_40(ebx, ecx, edx, edi, ebp, esi, 35);
  ROUNDS_21_40(esi, ebx, ecx, edx, edi, ebp, 36);
  ROUNDS_21_40(ebp, esi, ebx, ecx, edx, edi, 37);
  ROUNDS_21_40(edi, ebp, esi, ebx, ecx, edx, 38);
  ROUNDS_21_40(edx, edi, ebp, esi, ebx, ecx, 39);

  ROUNDS_41_60(ecx, edx, edi, ebp, esi, ebx, 40);
  ROUNDS_41_60(ebx, ecx, edx, edi, ebp, esi, 41);
  ROUNDS_41_60(esi, ebx, ecx, edx, edi, ebp, 42);
  ROUNDS_41_60(ebp, esi, ebx, ecx, edx, edi, 43);
  ROUNDS_41_60(edi, ebp, esi, ebx, ecx, edx, 44);
  ROUNDS_41_60(edx, edi, ebp, esi, ebx, ecx, 45);
  ROUNDS_41_60(ecx, edx, edi, ebp, esi, ebx, 46);
  ROUNDS_41_60(ebx, ecx, edx, edi, ebp, esi, 47);
  ROUNDS_41_60(esi, ebx, ecx, edx, edi, ebp, 48);
  ROUNDS_41_60(ebp, esi, ebx, ecx, edx, edi, 49);
  ROUNDS_41_60(edi, ebp, esi, ebx, ecx, edx, 50);
  ROUNDS_41_60(edx, edi, ebp, esi, ebx, ecx, 51);
  ROUNDS_41_60(ecx, edx, edi, ebp, esi, ebx, 52);
  ROUNDS_41_60(ebx, ecx, edx, edi, ebp, esi, 53);
  ROUNDS_41_60(esi, ebx, ecx, edx, edi, ebp, 54);
  ROUNDS_41_60(ebp, esi, ebx, ecx, edx, edi, 55);
  ROUNDS_41_60(edi, ebp, esi, ebx, ecx, edx, 56);
  ROUNDS_41_60(edx, edi, ebp, esi, ebx, ecx, 57);
  ROUNDS_41_60(ecx, edx, edi, ebp, esi, ebx, 58);
  ROUNDS_41_60(ebx, ecx, edx, edi, ebp, esi, 59);

  ROUNDS_61_80(esi, ebx, ecx, edx, edi, ebp, 60);
  ROUNDS_61_80(ebp, esi, ebx, ecx, edx, edi, 61);
  ROUNDS_61_80(edi, ebp, esi, ebx, ecx, edx, 62);
  ROUNDS_61_80(edx, edi, ebp, esi, ebx, ecx, 63);
  ROUNDS_61_80(ecx, edx, edi, ebp, esi, ebx, 64);
  ROUNDS_61_80(ebx, ecx, edx, edi, ebp, esi, 65);
  ROUNDS_61_80(esi, ebx, ecx, edx, edi, ebp, 66);
  ROUNDS_61_80(ebp, esi, ebx, ecx, edx, edi, 67);
  ROUNDS_61_80(edi, ebp, esi, ebx, ecx, edx, 68);
  ROUNDS_61_80(edx, edi, ebp, esi, ebx, ecx, 69);
  ROUNDS_61_80(ecx, edx, edi, ebp, esi, ebx, 70);
  ROUNDS_61_80(ebx, ecx, edx, edi, ebp, esi, 71);
  ROUNDS_61_80(esi, ebx, ecx, edx, edi, ebp, 72);
  ROUNDS_61_80(ebp, esi, ebx, ecx, edx, edi, 73);
  ROUNDS_61_80(edi, ebp, esi, ebx, ecx, edx, 74);
  ROUNDS_61_80(edx, edi, ebp, esi, ebx, ecx, 75);
  ROUNDS_61_80(ecx, edx, edi, ebp, esi, ebx, 76);
  ROUNDS_61_80(ebx, ecx, edx, edi, ebp, esi, 77);
  ROUNDS_61_80(esi, ebx, ecx, edx, edi, ebp, 78);
  ROUNDS_61_80(ebp, esi, ebx, ecx, edx, edi, 79);

  __asm mov edx, PARAM_CONTEXT
  __asm add [edx], edi
  __asm add [edx + 4], ebp
  __asm add [edx + 8], esi
  __asm add [edx + 12], ebx
  __asm add [edx + 16], ecx

  __asm add esp, 4*80
  __asm pop ebp

#undef PARAM_BLOCK
#undef PARAM_CONTEXT
#undef W_ADDR
#undef ROUNDS_1_16
#undef ROUNDS_17_20
#undef ROUNDS_21_40
#undef ROUNDS_41_60
#undef ROUNDS_61_80
}

#pragma auto_inline(on)

#define HAVE_SHA  1
#endif /* defined(_MSC_VER) && defined (WIN32) &&
          defined(KERNEL) && !defined(HAVE_SHA) */


#if !defined(HAVE_SHA)

/* This code was developed with the intention that the data in the `W'
   array should be register-allocated whenever possible.  This
   succeeds in most 32-register RISCs (PPC, Alpha, MIPS), but
   surprisingly this code seems to be more efficient also non a
   Pentium! */

static void sha_transform(SshSHAContext *context, const unsigned char *block)
{
  register SshUInt32 a, b, c, d, e, imm;
  SshUInt32 W_0, W_1, W_2, W_3, W_4, W_5, W_6, W_7, W_8, W_9;
  SshUInt32 W_10, W_11, W_12, W_13, W_14, W_15;

  a = context->A;
  b = context->B;
  c = context->C;
  d = context->D;
  e = context->E;

#define TABLE_IN(i)                             \
  W_ ## i = SSH_GET_32BIT(block);               \
  block += 4;

#define TABLE_MORE(i, i3, i8, i14)                                         \
  W_ ## i = SSH_ROL32_CONST(W_ ## i ^ W_ ## i3 ^ W_ ## i8 ^ W_ ## i14, 1);

#define NONLINEAR1(F, a, b, c, d, e, i)                 \
  TABLE_IN(i);                                          \
  e += W_ ## i + F(b, c, d) + SSH_ROL32_CONST(a, 5);    \
  b = SSH_ROL32_CONST(b, 30);

#define NONLINEAR2(F, a, b, c, d, e, i, i3, i8, i14)    \
  TABLE_MORE(i, i3, i8, i14);                           \
  e += W_ ## i + F(b, c, d) + SSH_ROL32_CONST(a, 5);    \
  b = SSH_ROL32_CONST(b, 30);

  imm = 0x5a827999UL;
  NONLINEAR1(F1, a, b, c, d, e,  0);
  NONLINEAR1(F1, e, a, b, c, d,  1);
  NONLINEAR1(F1, d, e, a, b, c,  2);
  NONLINEAR1(F1, c, d, e, a, b,  3);
  NONLINEAR1(F1, b, c, d, e, a,  4);
  NONLINEAR1(F1, a, b, c, d, e,  5);
  NONLINEAR1(F1, e, a, b, c, d,  6);
  NONLINEAR1(F1, d, e, a, b, c,  7);
  NONLINEAR1(F1, c, d, e, a, b,  8);
  NONLINEAR1(F1, b, c, d, e, a,  9);
  NONLINEAR1(F1, a, b, c, d, e, 10);
  NONLINEAR1(F1, e, a, b, c, d, 11);
  NONLINEAR1(F1, d, e, a, b, c, 12);
  NONLINEAR1(F1, c, d, e, a, b, 13);
  NONLINEAR1(F1, b, c, d, e, a, 14);
  NONLINEAR1(F1, a, b, c, d, e, 15);

  NONLINEAR2(F1, e, a, b, c, d,  0, 13,  8,  2);
  NONLINEAR2(F1, d, e, a, b, c,  1, 14,  9,  3);
  NONLINEAR2(F1, c, d, e, a, b,  2, 15, 10,  4);
  NONLINEAR2(F1, b, c, d, e, a,  3,  0, 11,  5);

  imm = 0x6ed9eba1UL;
  NONLINEAR2(F2, a, b, c, d, e,  4,  1, 12,  6);
  NONLINEAR2(F2, e, a, b, c, d,  5,  2, 13,  7);
  NONLINEAR2(F2, d, e, a, b, c,  6,  3, 14,  8);
  NONLINEAR2(F2, c, d, e, a, b,  7,  4, 15,  9);
  NONLINEAR2(F2, b, c, d, e, a,  8,  5,  0, 10);
  NONLINEAR2(F2, a, b, c, d, e,  9,  6,  1, 11);
  NONLINEAR2(F2, e, a, b, c, d, 10,  7,  2, 12);
  NONLINEAR2(F2, d, e, a, b, c, 11,  8,  3, 13);
  NONLINEAR2(F2, c, d, e, a, b, 12,  9,  4, 14);
  NONLINEAR2(F2, b, c, d, e, a, 13, 10,  5, 15);
  NONLINEAR2(F2, a, b, c, d, e, 14, 11,  6,  0);
  NONLINEAR2(F2, e, a, b, c, d, 15, 12,  7,  1);
  NONLINEAR2(F2, d, e, a, b, c,  0, 13,  8,  2);
  NONLINEAR2(F2, c, d, e, a, b,  1, 14,  9,  3);
  NONLINEAR2(F2, b, c, d, e, a,  2, 15, 10,  4);
  NONLINEAR2(F2, a, b, c, d, e,  3,  0, 11,  5);
  NONLINEAR2(F2, e, a, b, c, d,  4,  1, 12,  6);
  NONLINEAR2(F2, d, e, a, b, c,  5,  2, 13,  7);
  NONLINEAR2(F2, c, d, e, a, b,  6,  3, 14,  8);
  NONLINEAR2(F2, b, c, d, e, a,  7,  4, 15,  9);

  imm = 0x8f1bbcdcUL;
  NONLINEAR2(F3, a, b, c, d, e,  8,  5,  0, 10);
  NONLINEAR2(F3, e, a, b, c, d,  9,  6,  1, 11);
  NONLINEAR2(F3, d, e, a, b, c, 10,  7,  2, 12);
  NONLINEAR2(F3, c, d, e, a, b, 11,  8,  3, 13);
  NONLINEAR2(F3, b, c, d, e, a, 12,  9,  4, 14);
  NONLINEAR2(F3, a, b, c, d, e, 13, 10,  5, 15);
  NONLINEAR2(F3, e, a, b, c, d, 14, 11,  6,  0);
  NONLINEAR2(F3, d, e, a, b, c, 15, 12,  7,  1);
  NONLINEAR2(F3, c, d, e, a, b,  0, 13,  8,  2);
  NONLINEAR2(F3, b, c, d, e, a,  1, 14,  9,  3);
  NONLINEAR2(F3, a, b, c, d, e,  2, 15, 10,  4);
  NONLINEAR2(F3, e, a, b, c, d,  3,  0, 11,  5);
  NONLINEAR2(F3, d, e, a, b, c,  4,  1, 12,  6);
  NONLINEAR2(F3, c, d, e, a, b,  5,  2, 13,  7);
  NONLINEAR2(F3, b, c, d, e, a,  6,  3, 14,  8);
  NONLINEAR2(F3, a, b, c, d, e,  7,  4, 15,  9);
  NONLINEAR2(F3, e, a, b, c, d,  8,  5,  0, 10);
  NONLINEAR2(F3, d, e, a, b, c,  9,  6,  1, 11);
  NONLINEAR2(F3, c, d, e, a, b, 10,  7,  2, 12);
  NONLINEAR2(F3, b, c, d, e, a, 11,  8,  3, 13);

  imm = 0xca62c1d6UL;
  NONLINEAR2(F4, a, b, c, d, e, 12,  9,  4, 14);
  NONLINEAR2(F4, e, a, b, c, d, 13, 10,  5, 15);
  NONLINEAR2(F4, d, e, a, b, c, 14, 11,  6,  0);
  NONLINEAR2(F4, c, d, e, a, b, 15, 12,  7,  1);
  NONLINEAR2(F4, b, c, d, e, a,  0, 13,  8,  2);
  NONLINEAR2(F4, a, b, c, d, e,  1, 14,  9,  3);
  NONLINEAR2(F4, e, a, b, c, d,  2, 15, 10,  4);
  NONLINEAR2(F4, d, e, a, b, c,  3,  0, 11,  5);
  NONLINEAR2(F4, c, d, e, a, b,  4,  1, 12,  6);
  NONLINEAR2(F4, b, c, d, e, a,  5,  2, 13,  7);
  NONLINEAR2(F4, a, b, c, d, e,  6,  3, 14,  8);
  NONLINEAR2(F4, e, a, b, c, d,  7,  4, 15,  9);
  NONLINEAR2(F4, d, e, a, b, c,  8,  5,  0, 10);
  NONLINEAR2(F4, c, d, e, a, b,  9,  6,  1, 11);
  NONLINEAR2(F4, b, c, d, e, a, 10,  7,  2, 12);
  NONLINEAR2(F4, a, b, c, d, e, 11,  8,  3, 13);
  NONLINEAR2(F4, e, a, b, c, d, 12,  9,  4, 14);
  NONLINEAR2(F4, d, e, a, b, c, 13, 10,  5, 15);
  NONLINEAR2(F4, c, d, e, a, b, 14, 11,  6,  0);
  NONLINEAR2(F4, b, c, d, e, a, 15, 12,  7,  1);

  context->A += a;
  context->B += b;
  context->C += c;
  context->D += d;
  context->E += e;
}

#define HAVE_SHA  1
#endif


#if !defined(HAVE_SHA)

static void sha_transform(SshSHAContext *context, const unsigned char *block)
{
  SshUInt32 W[80];
  SshUInt32 a, b, c, d, e, f, imm;

  a = context->A;
  b = context->B;
  c = context->C;
  d = context->D;
  e = context->E;

  /* Unroll as much as one can, removing unneccessary copying etc.

     What actually happens is that the compiler must interleave all
     these operations in some efficient way. On processors with only
     few registers it might be better to implement the table
     generation before actual 'nonlinear' operations. On Intel
     processors that might be the case, although one never knows
     without trying. */

#define TABLE_IN(i)                             \
  W[i] = SSH_GET_32BIT(block); block += 4;

#define TABLE_MORE(i, t)                                \
  t = W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16];      \
  W[i] = SSH_ROL32_CONST(t, 1);

#define NONLINEAR1(F, a, b, c, d, e, f, i)      \
  TABLE_IN(i);                                  \
  f = SSH_ROL32_CONST(a, 5);                    \
  f += F(b, c, d);                              \
  b = SSH_ROL32_CONST(b, 30);                   \
  f += e + W[i];

#define NONLINEAR2(F, a, b, c, d, e, f, i)      \
  TABLE_MORE(i, f);                             \
  f = SSH_ROL32_CONST(a, 5);                    \
  f += F(b, c, d);                              \
  b = SSH_ROL32_CONST(b, 30);                   \
  f += e + W[i];

  imm = 0x5a827999UL;
  NONLINEAR1(F1, a, b, c, d, e, f,  0);
  NONLINEAR1(F1, f, a, b, c, d, e,  1);
  NONLINEAR1(F1, e, f, a, b, c, d,  2);
  NONLINEAR1(F1, d, e, f, a, b, c,  3);
  NONLINEAR1(F1, c, d, e, f, a, b,  4);
  NONLINEAR1(F1, b, c, d, e, f, a,  5);
  NONLINEAR1(F1, a, b, c, d, e, f,  6);
  NONLINEAR1(F1, f, a, b, c, d, e,  7);
  NONLINEAR1(F1, e, f, a, b, c, d,  8);
  NONLINEAR1(F1, d, e, f, a, b, c,  9);
  NONLINEAR1(F1, c, d, e, f, a, b, 10);
  NONLINEAR1(F1, b, c, d, e, f, a, 11);
  NONLINEAR1(F1, a, b, c, d, e, f, 12);
  NONLINEAR1(F1, f, a, b, c, d, e, 13);
  NONLINEAR1(F1, e, f, a, b, c, d, 14);
  NONLINEAR1(F1, d, e, f, a, b, c, 15);
  NONLINEAR2(F1, c, d, e, f, a, b, 16);
  NONLINEAR2(F1, b, c, d, e, f, a, 17);
  NONLINEAR2(F1, a, b, c, d, e, f, 18);
  NONLINEAR2(F1, f, a, b, c, d, e, 19);

  imm = 0x6ed9eba1UL;
  NONLINEAR2(F2, e, f, a, b, c, d, 20);
  NONLINEAR2(F2, d, e, f, a, b, c, 21);
  NONLINEAR2(F2, c, d, e, f, a, b, 22);
  NONLINEAR2(F2, b, c, d, e, f, a, 23);
  NONLINEAR2(F2, a, b, c, d, e, f, 24);
  NONLINEAR2(F2, f, a, b, c, d, e, 25);
  NONLINEAR2(F2, e, f, a, b, c, d, 26);
  NONLINEAR2(F2, d, e, f, a, b, c, 27);
  NONLINEAR2(F2, c, d, e, f, a, b, 28);
  NONLINEAR2(F2, b, c, d, e, f, a, 29);
  NONLINEAR2(F2, a, b, c, d, e, f, 30);
  NONLINEAR2(F2, f, a, b, c, d, e, 31);
  NONLINEAR2(F2, e, f, a, b, c, d, 32);
  NONLINEAR2(F2, d, e, f, a, b, c, 33);
  NONLINEAR2(F2, c, d, e, f, a, b, 34);
  NONLINEAR2(F2, b, c, d, e, f, a, 35);
  NONLINEAR2(F2, a, b, c, d, e, f, 36);
  NONLINEAR2(F2, f, a, b, c, d, e, 37);
  NONLINEAR2(F2, e, f, a, b, c, d, 38);
  NONLINEAR2(F2, d, e, f, a, b, c, 39);

  imm = 0x8f1bbcdcUL;
  NONLINEAR2(F3, c, d, e, f, a, b, 40);
  NONLINEAR2(F3, b, c, d, e, f, a, 41);
  NONLINEAR2(F3, a, b, c, d, e, f, 42);
  NONLINEAR2(F3, f, a, b, c, d, e, 43);
  NONLINEAR2(F3, e, f, a, b, c, d, 44);
  NONLINEAR2(F3, d, e, f, a, b, c, 45);
  NONLINEAR2(F3, c, d, e, f, a, b, 46);
  NONLINEAR2(F3, b, c, d, e, f, a, 47);
  NONLINEAR2(F3, a, b, c, d, e, f, 48);
  NONLINEAR2(F3, f, a, b, c, d, e, 49);
  NONLINEAR2(F3, e, f, a, b, c, d, 50);
  NONLINEAR2(F3, d, e, f, a, b, c, 51);
  NONLINEAR2(F3, c, d, e, f, a, b, 52);
  NONLINEAR2(F3, b, c, d, e, f, a, 53);
  NONLINEAR2(F3, a, b, c, d, e, f, 54);
  NONLINEAR2(F3, f, a, b, c, d, e, 55);
  NONLINEAR2(F3, e, f, a, b, c, d, 56);
  NONLINEAR2(F3, d, e, f, a, b, c, 57);
  NONLINEAR2(F3, c, d, e, f, a, b, 58);
  NONLINEAR2(F3, b, c, d, e, f, a, 59);

  imm = 0xca62c1d6UL;
  NONLINEAR2(F4, a, b, c, d, e, f, 60);
  NONLINEAR2(F4, f, a, b, c, d, e, 61);
  NONLINEAR2(F4, e, f, a, b, c, d, 62);
  NONLINEAR2(F4, d, e, f, a, b, c, 63);
  NONLINEAR2(F4, c, d, e, f, a, b, 64);
  NONLINEAR2(F4, b, c, d, e, f, a, 65);
  NONLINEAR2(F4, a, b, c, d, e, f, 66);
  NONLINEAR2(F4, f, a, b, c, d, e, 67);
  NONLINEAR2(F4, e, f, a, b, c, d, 68);
  NONLINEAR2(F4, d, e, f, a, b, c, 69);
  NONLINEAR2(F4, c, d, e, f, a, b, 70);
  NONLINEAR2(F4, b, c, d, e, f, a, 71);
  NONLINEAR2(F4, a, b, c, d, e, f, 72);
  NONLINEAR2(F4, f, a, b, c, d, e, 73);
  NONLINEAR2(F4, e, f, a, b, c, d, 74);
  NONLINEAR2(F4, d, e, f, a, b, c, 75);
  NONLINEAR2(F4, c, d, e, f, a, b, 76);
  NONLINEAR2(F4, b, c, d, e, f, a, 77);
  NONLINEAR2(F4, a, b, c, d, e, f, 78);
  NONLINEAR2(F4, f, a, b, c, d, e, 79);

  /* Remember the correct order of rotated variables. */
  context->A += e;
  context->B += f;
  context->C += a;
  context->D += b;
  context->E += c;

  /* Cessu: These should be useless, since the fields are SshUInt32's:
     context->A &= 0xFFFFFFFFL;
     context->B &= 0xFFFFFFFFL;
     context->C &= 0xFFFFFFFFL;
     context->D &= 0xFFFFFFFFL;
     context->E &= 0xFFFFFFFFL; */
}

#define HAVE_SHA  1
#endif /* !defined(HAVE_SHA) */


#if !defined(HAVE_SHA)
/* A last alternative, also a reference implementation.  Apparently
   only slightly slower than the above version, where the loops are
   essentially unrolled. */

static void sha_transform(SshSHAContext *context, const unsigned char *block)
{
  SshUInt32 W[80];
  SshUInt32 a, b, c, d, e, f, imm;
  int t;

  a = context->A;
  b = context->B;
  c = context->C;
  d = context->D;
  e = context->E;

  for (t = 0; t < 16; t++)
    {
      W[t] = SSH_GET_32BIT(block);
      block += 4;
    }

  for (SSH_HEAVY_ASSERT(t == 16); t < 80; t++)
    {
      f = W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16];
      W[t] = SSH_ROL32_CONST(f, 1);
    }

  imm = 0x5a827999L;
  for (t = 0; t < 20; t++)
    {
      f = SSH_ROL32_CONST(a, 5);

      f += F1(b, c, d);

      f += e + W[t];
      /* Cessu: This is be useless since f is SshUInt32
         f &= 0xFFFFFFFFL; */
      e = d;
      d = c;
      c = SSH_ROL32_CONST(b, 30);
      b = a;
      a = f;
    }

  imm = 0x6ed9eba1UL;
  for (SSH_HEAVY_ASSERT(t == 20); t < 40; t++)
    {
      f = SSH_ROL32_CONST(a, 5);

      f += F2(b, c, d);

      f += e + W[t];
      /* Cessu: This is be useless since f is SshUInt32
         f &= 0xFFFFFFFFL; */
      e = d;
      d = c;
      c = SSH_ROL32_CONST(b, 30);
      b = a;
      a = f;
    }

  imm = 0x8f1bbcdcUL;
  for (SSH_HEAVY_ASSERT(t == 40); t < 60; t++)
    {
      f = SSH_ROL32_CONST(a, 5);

      f += F3(b, c, d);

      f += e + W[t];
      /* Cessu: This is be useless since f is SshUInt32
         f &= 0xFFFFFFFFL; */
      e = d;
      d = c;
      c = SSH_ROL32_CONST(b, 30);
      b = a;
      a = f;
    }


  imm = 0xca62c1d6UL;
  for (SSH_HEAVY_ASSERT(t == 60); t < 80; t++)
    {
      f = SSH_ROL32_CONST(a, 5);

      f += F4(b, c, d);

      f += e + W[t];
      /* Cessu: This is be useless since f is SshUInt32
         f &= 0xFFFFFFFFL; */
      e = d;
      d = c;
      c = SSH_ROL32_CONST(b, 30);
      b = a;
      a = f;
    }

#if 0
  /* This is the original implementation of the four loops above.
     That code is constructed by simply moving four-way branch on the
     value of `t' out of the loop and thereby creating four loops
     instead of one. */

  for (t = 0; t < 80; t++)
    {
      f = SSH_ROL32_CONST(a, 5);

      if (t < 40)
        {
          if (t < 20) {
            imm = 0x5a827999UL;
            f += F1(b, c, d);
          } else {
            imm = 0x6ed9eba1UL;
            f += F2(b, c, d);
          }
        }
      else
        {
          if (t < 60) {
            imm = 0x8f1bbcdcUL;
            f += F3(b, c, d);
          } else {
            imm = 0xca62c1d6UL;
            f += F4(b, c, d);
          }
        }

      f += e + W[t];
      /* Cessu: This is be useless since f is SshUInt32
         f &= 0xFFFFFFFFL; */

      e = d;
      d = c;
      c = SSH_ROL32_CONST(b, 30);
      b = a;
      a = f;
    }

#endif

  context->A += a;
  context->B += b;
  context->C += c;
  context->D += d;
  context->E += e;

  /* Cessu: These should be useless, since the fields are SshUInt32's:
     context->A &= 0xFFFFFFFFL;
     context->B &= 0xFFFFFFFFL;
     context->C &= 0xFFFFFFFFL;
     context->D &= 0xFFFFFFFFL;
     context->E &= 0xFFFFFFFFL; */
}

#define HAVE_SHA  1
#endif


#ifndef HAVE_SHA
#error No implementation of SHA!
#endif


void ssh_sha_update(void *c, const unsigned char *buf, size_t len)
{
  SshSHAContext *context = c;
  unsigned int to_copy = 0;
  unsigned int in_buffer;

  SshUInt32 old_length = context->total_length[0];

  in_buffer = old_length % 64;

  context->total_length[0] += len;
  context->total_length[0] &= 0xFFFFFFFFUL;

  if (context->total_length[0] < old_length) /* carry */
    context->total_length[1]++;

  while (len > 0)
    {
      if (in_buffer == 0 && len >= 64)
        {
          sha_transform(context, buf);
          buf += 64;
          len -= 64;
          continue;
        }

      /* do copy? */
      to_copy = 64 - in_buffer;
      if (to_copy > 0)
        {
          if (to_copy > len)
            to_copy = len;
          memcpy(&context->in[in_buffer],
                 buf, to_copy);
          buf += to_copy;
          len -= to_copy;
          in_buffer += to_copy;
          if (in_buffer == 64)
            {
              sha_transform(context, context->in);
              in_buffer = 0;
            }
        }
    }
}

void ssh_sha_final(void *c, unsigned char *digest)
{
  SshSHAContext *context = c;
  int padding;
  unsigned char temp = 0x80;
  unsigned int in_buffer;
  SshUInt32 total_low, total_high;

  total_low = context->total_length[0];
  total_high = context->total_length[1];

  ssh_sha_update(context, &temp, 1);

  in_buffer = context->total_length[0] % 64;
  padding = (64 - (in_buffer + 9) % 64) % 64;

  if (in_buffer > 56)
    {
      memset(&context->in[in_buffer], 0, 64 - in_buffer);
      padding -= (64 - in_buffer);
      sha_transform(context, context->in);
      in_buffer = 0;
    }

  /* change the byte count to bits count */
  total_high <<= 3;
  total_high += (total_low >> 29);
  total_low <<= 3;

  SSH_PUT_32BIT(context->in + 56, total_high);
  SSH_PUT_32BIT(context->in + 60, total_low);

  if ((64 - in_buffer - 8) > 0)
    {
      memset(&context->in[in_buffer],
             0, 64 - in_buffer - 8);
    }

  sha_transform(context, context->in);

  SSH_PUT_32BIT(digest,      context->A);
  SSH_PUT_32BIT(digest + 4,  context->B);
  SSH_PUT_32BIT(digest + 8,  context->C);
  SSH_PUT_32BIT(digest + 12, context->D);
  SSH_PUT_32BIT(digest + 16, context->E);

  memset(context, 0, sizeof(SshSHAContext));
}

void ssh_sha_of_buffer(unsigned char digest[20],
                       const unsigned char *buf, size_t len)
{
  SshSHAContext context;
  ssh_sha_reset_context(&context);
  ssh_sha_update(&context, buf, len);
  ssh_sha_final(&context, digest);
}

/* Extra routines. */
void ssh_sha_96_final(void *c, unsigned char *digest)
{
  unsigned char tmp_digest[20];
  ssh_sha_final(c, tmp_digest);
  memcpy(digest, tmp_digest, 12);
}

void ssh_sha_96_of_buffer(unsigned char digest[12],
                          const unsigned char *buf, size_t len)
{
  SshSHAContext context;
  ssh_sha_reset_context(&context);
  ssh_sha_update(&context, buf, len);
  ssh_sha_96_final(&context, digest);
}

void ssh_sha_80_final(void *c, unsigned char *digest)
{
  unsigned char tmp_digest[20];
  ssh_sha_final(c, tmp_digest);
  memcpy(digest, tmp_digest, 10);
}

void ssh_sha_80_of_buffer(unsigned char digest[10],
                          const unsigned char *buf, size_t len)
{
  SshSHAContext context;
  ssh_sha_reset_context(&context);
  ssh_sha_update(&context, buf, len);
  ssh_sha_80_final(&context, digest);
}


/* 'buf' is initialized to the usual initialization state of the SHA-1, 
   in hexadecimal, 67452301 EFCDAB89 98BADCFE 10325476 C3D2E1F0. 
   'in' is 64 bytes to be added to the internal state. The output 
   value is stored in 'buf'. */
void ssh_sha_transform(SshUInt32 buf[5], const unsigned char in[64])
{
  SshSHAContext context;

  memset(&context, 0, sizeof(SshSHAContext));
  context.A = 0x67452301UL;
  context.B = 0xefcdab89UL;
  context.C = 0x98badcfeUL;
  context.D = 0x10325476UL;
  context.E = 0xc3d2e1f0UL;

  sha_transform(&context, in);

  buf[0] = context.A;
  buf[1] = context.B;
  buf[2] = context.C;
  buf[3] = context.D;
  buf[4] = context.E;
}

/* 'buf' is initialized to a permutation of the usual initialization state 
   of the SHA-1, in hexadecimal, EFCDAB89 98BADCFE 10325476 C3D2E1F0 67452301. 
   'in' is 64 bytes to be added to the internal state. The output 
   value is stored in 'buf'. */
void ssh_sha_permuted_transform(SshUInt32 buf[5], const unsigned char in[64])
{
  SshSHAContext context;

  memset(&context, 0, sizeof(SshSHAContext));
  context.A = 0xefcdab89UL;
  context.B = 0x98badcfeUL;
  context.C = 0x10325476UL;
  context.D = 0xc3d2e1f0UL;
  context.E = 0x67452301UL;

  sha_transform(&context, in);

  buf[0] = context.A;
  buf[1] = context.B;
  buf[2] = context.C;
  buf[3] = context.D;
  buf[4] = context.E;
}

/* Compares the given oid with max size of max_len to the oid
   defined for the hash. If they match, then return the number
   of bytes actually used by the oid. If they do not match, return
   0. */
size_t ssh_sha_asn1_compare(const unsigned char *oid, size_t max_len)
{
  if (max_len >= ssh_encoded_sha_oid1_len &&
      (memcmp(oid, ssh_encoded_sha_oid1, ssh_encoded_sha_oid1_len) == 0))
    return ssh_encoded_sha_oid1_len;
  if (max_len >= ssh_encoded_sha_oid2_len &&
      (memcmp(oid, ssh_encoded_sha_oid2, ssh_encoded_sha_oid2_len) == 0))
    return ssh_encoded_sha_oid2_len;
  return 0;
}

/* Generate encoded asn1 oid. Returns the pointer to the staticly
   allocated buffer of the oid. Sets the len to be the length
   of the oid. */
const unsigned char *ssh_sha_asn1_generate(size_t *len)
{
  if (len) *len = ssh_encoded_sha_oid1_len;
  return ssh_encoded_sha_oid1;
}
