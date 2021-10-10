/*

DES implementation; 1995-2002 Tatu Ylonen <ylo@cs.hut.fi>

This implementation is derived from libdes-3.06, which is copyright
(c) 1993 Eric Young, and distributed under the GNU GPL or the ARTISTIC licence
(at the user's option).  The original distribution can be found e.g. from
ftp://ftp.dsi.unimi.it/pub/security/crypt/libdes/libdes-3.06.tar.gz.

This implementation is distributed under the same terms.  See
libdes-README, libdes-ARTISTIC, and libdes-COPYING for more
information.

A description of the DES algorithm can be found in every modern book on
cryptography and data security, including the following:

  Bruce Schneier: Applied Cryptography.  John Wiley & Sons, 1994.

  Jennifer Seberry and Josed Pieprzyk: Cryptography: An Introduction to
    Computer Security.  Prentice-Hall, 1989.

  Man Young Rhee: Cryptography and Secure Data Communications.  McGraw-Hill,
    1994.

*/

#include "sshincludes.h"

#ifndef WITHOUT_DES

#include "sshcrypt.h"
#include "sshgetput.h"
#include "des.h"

typedef struct
{
  SshUInt32 key_schedule[32];
  Boolean for_encryption;
} SshDESContext;

typedef struct
{
  SshUInt32 key_schedule[96];
  Boolean for_encryption;
} SshTripleDESContext;

/* Table for key generation.  This used to be in sk.h. */
/* Copyright (C) 1993 Eric Young - see README for more details */
const SshUInt32 SSH_CODE_SEGMENT ssh_des_skb[8][64]={
/* for C bits (numbered as per FIPS 46) 1 2 3 4 5 6 */
{ 0x00000000,0x00000010,0x20000000,0x20000010,
0x00010000,0x00010010,0x20010000,0x20010010,
0x00000800,0x00000810,0x20000800,0x20000810,
0x00010800,0x00010810,0x20010800,0x20010810,
0x00000020,0x00000030,0x20000020,0x20000030,
0x00010020,0x00010030,0x20010020,0x20010030,
0x00000820,0x00000830,0x20000820,0x20000830,
0x00010820,0x00010830,0x20010820,0x20010830,
0x00080000,0x00080010,0x20080000,0x20080010,
0x00090000,0x00090010,0x20090000,0x20090010,
0x00080800,0x00080810,0x20080800,0x20080810,
0x00090800,0x00090810,0x20090800,0x20090810,
0x00080020,0x00080030,0x20080020,0x20080030,
0x00090020,0x00090030,0x20090020,0x20090030,
0x00080820,0x00080830,0x20080820,0x20080830,
0x00090820,0x00090830,0x20090820,0x20090830 },
/* for C bits (numbered as per FIPS 46) 7 8 10 11 12 13 */
{ 0x00000000,0x02000000,0x00002000,0x02002000,
0x00200000,0x02200000,0x00202000,0x02202000,
0x00000004,0x02000004,0x00002004,0x02002004,
0x00200004,0x02200004,0x00202004,0x02202004,
0x00000400,0x02000400,0x00002400,0x02002400,
0x00200400,0x02200400,0x00202400,0x02202400,
0x00000404,0x02000404,0x00002404,0x02002404,
0x00200404,0x02200404,0x00202404,0x02202404,
0x10000000,0x12000000,0x10002000,0x12002000,
0x10200000,0x12200000,0x10202000,0x12202000,
0x10000004,0x12000004,0x10002004,0x12002004,
0x10200004,0x12200004,0x10202004,0x12202004,
0x10000400,0x12000400,0x10002400,0x12002400,
0x10200400,0x12200400,0x10202400,0x12202400,
0x10000404,0x12000404,0x10002404,0x12002404,
0x10200404,0x12200404,0x10202404,0x12202404 },
/* for C bits (numbered as per FIPS 46) 14 15 16 17 19 20 */
{ 0x00000000,0x00000001,0x00040000,0x00040001,
0x01000000,0x01000001,0x01040000,0x01040001,
0x00000002,0x00000003,0x00040002,0x00040003,
0x01000002,0x01000003,0x01040002,0x01040003,
0x00000200,0x00000201,0x00040200,0x00040201,
0x01000200,0x01000201,0x01040200,0x01040201,
0x00000202,0x00000203,0x00040202,0x00040203,
0x01000202,0x01000203,0x01040202,0x01040203,
0x08000000,0x08000001,0x08040000,0x08040001,
0x09000000,0x09000001,0x09040000,0x09040001,
0x08000002,0x08000003,0x08040002,0x08040003,
0x09000002,0x09000003,0x09040002,0x09040003,
0x08000200,0x08000201,0x08040200,0x08040201,
0x09000200,0x09000201,0x09040200,0x09040201,
0x08000202,0x08000203,0x08040202,0x08040203,
0x09000202,0x09000203,0x09040202,0x09040203 },
/* for C bits (numbered as per FIPS 46) 21 23 24 26 27 28 */
{ 0x00000000,0x00100000,0x00000100,0x00100100,
0x00000008,0x00100008,0x00000108,0x00100108,
0x00001000,0x00101000,0x00001100,0x00101100,
0x00001008,0x00101008,0x00001108,0x00101108,
0x04000000,0x04100000,0x04000100,0x04100100,
0x04000008,0x04100008,0x04000108,0x04100108,
0x04001000,0x04101000,0x04001100,0x04101100,
0x04001008,0x04101008,0x04001108,0x04101108,
0x00020000,0x00120000,0x00020100,0x00120100,
0x00020008,0x00120008,0x00020108,0x00120108,
0x00021000,0x00121000,0x00021100,0x00121100,
0x00021008,0x00121008,0x00021108,0x00121108,
0x04020000,0x04120000,0x04020100,0x04120100,
0x04020008,0x04120008,0x04020108,0x04120108,
0x04021000,0x04121000,0x04021100,0x04121100,
0x04021008,0x04121008,0x04021108,0x04121108 },
/* for D bits (numbered as per FIPS 46) 1 2 3 4 5 6 */
{ 0x00000000,0x10000000,0x00010000,0x10010000,
0x00000004,0x10000004,0x00010004,0x10010004,
0x20000000,0x30000000,0x20010000,0x30010000,
0x20000004,0x30000004,0x20010004,0x30010004,
0x00100000,0x10100000,0x00110000,0x10110000,
0x00100004,0x10100004,0x00110004,0x10110004,
0x20100000,0x30100000,0x20110000,0x30110000,
0x20100004,0x30100004,0x20110004,0x30110004,
0x00001000,0x10001000,0x00011000,0x10011000,
0x00001004,0x10001004,0x00011004,0x10011004,
0x20001000,0x30001000,0x20011000,0x30011000,
0x20001004,0x30001004,0x20011004,0x30011004,
0x00101000,0x10101000,0x00111000,0x10111000,
0x00101004,0x10101004,0x00111004,0x10111004,
0x20101000,0x30101000,0x20111000,0x30111000,
0x20101004,0x30101004,0x20111004,0x30111004 },
/* for D bits (numbered as per FIPS 46) 8 9 11 12 13 14 */
{ 0x00000000,0x08000000,0x00000008,0x08000008,
0x00000400,0x08000400,0x00000408,0x08000408,
0x00020000,0x08020000,0x00020008,0x08020008,
0x00020400,0x08020400,0x00020408,0x08020408,
0x00000001,0x08000001,0x00000009,0x08000009,
0x00000401,0x08000401,0x00000409,0x08000409,
0x00020001,0x08020001,0x00020009,0x08020009,
0x00020401,0x08020401,0x00020409,0x08020409,
0x02000000,0x0A000000,0x02000008,0x0A000008,
0x02000400,0x0A000400,0x02000408,0x0A000408,
0x02020000,0x0A020000,0x02020008,0x0A020008,
0x02020400,0x0A020400,0x02020408,0x0A020408,
0x02000001,0x0A000001,0x02000009,0x0A000009,
0x02000401,0x0A000401,0x02000409,0x0A000409,
0x02020001,0x0A020001,0x02020009,0x0A020009,
0x02020401,0x0A020401,0x02020409,0x0A020409 },
/* for D bits (numbered as per FIPS 46) 16 17 18 19 20 21 */
{ 0x00000000,0x00000100,0x00080000,0x00080100,
0x01000000,0x01000100,0x01080000,0x01080100,
0x00000010,0x00000110,0x00080010,0x00080110,
0x01000010,0x01000110,0x01080010,0x01080110,
0x00200000,0x00200100,0x00280000,0x00280100,
0x01200000,0x01200100,0x01280000,0x01280100,
0x00200010,0x00200110,0x00280010,0x00280110,
0x01200010,0x01200110,0x01280010,0x01280110,
0x00000200,0x00000300,0x00080200,0x00080300,
0x01000200,0x01000300,0x01080200,0x01080300,
0x00000210,0x00000310,0x00080210,0x00080310,
0x01000210,0x01000310,0x01080210,0x01080310,
0x00200200,0x00200300,0x00280200,0x00280300,
0x01200200,0x01200300,0x01280200,0x01280300,
0x00200210,0x00200310,0x00280210,0x00280310,
0x01200210,0x01200310,0x01280210,0x01280310 },
/* for D bits (numbered as per FIPS 46) 22 23 24 25 27 28 */
{ 0x00000000,0x04000000,0x00040000,0x04040000,
0x00000002,0x04000002,0x00040002,0x04040002,
0x00002000,0x04002000,0x00042000,0x04042000,
0x00002002,0x04002002,0x00042002,0x04042002,
0x00000020,0x04000020,0x00040020,0x04040020,
0x00000022,0x04000022,0x00040022,0x04040022,
0x00002020,0x04002020,0x00042020,0x04042020,
0x00002022,0x04002022,0x00042022,0x04042022,
0x00000800,0x04000800,0x00040800,0x04040800,
0x00000802,0x04000802,0x00040802,0x04040802,
0x00002800,0x04002800,0x00042800,0x04042800,
0x00002802,0x04002802,0x00042802,0x04042802,
0x00000820,0x04000820,0x00040820,0x04040820,
0x00000822,0x04000822,0x00040822,0x04040822,
0x00002820,0x04002820,0x00042820,0x04042820,
0x00002822,0x04002822,0x00042822,0x04042822 }
};

/* Tables used for executing des.  This used to be in spr.h. */
/* Copyright (C) 1993 Eric Young - see README for more details */
const SshUInt32 SSH_CODE_SEGMENT ssh_des_SPtrans[8][64]={
/* nibble 0 */
{ 0x00820200, 0x00020000, 0x80800000, 0x80820200,
0x00800000, 0x80020200, 0x80020000, 0x80800000,
0x80020200, 0x00820200, 0x00820000, 0x80000200,
0x80800200, 0x00800000, 0x00000000, 0x80020000,
0x00020000, 0x80000000, 0x00800200, 0x00020200,
0x80820200, 0x00820000, 0x80000200, 0x00800200,
0x80000000, 0x00000200, 0x00020200, 0x80820000,
0x00000200, 0x80800200, 0x80820000, 0x00000000,
0x00000000, 0x80820200, 0x00800200, 0x80020000,
0x00820200, 0x00020000, 0x80000200, 0x00800200,
0x80820000, 0x00000200, 0x00020200, 0x80800000,
0x80020200, 0x80000000, 0x80800000, 0x00820000,
0x80820200, 0x00020200, 0x00820000, 0x80800200,
0x00800000, 0x80000200, 0x80020000, 0x00000000,
0x00020000, 0x00800000, 0x80800200, 0x00820200,
0x80000000, 0x80820000, 0x00000200, 0x80020200 },

/* nibble 1 */
{ 0x10042004, 0x00000000, 0x00042000, 0x10040000,
0x10000004, 0x00002004, 0x10002000, 0x00042000,
0x00002000, 0x10040004, 0x00000004, 0x10002000,
0x00040004, 0x10042000, 0x10040000, 0x00000004,
0x00040000, 0x10002004, 0x10040004, 0x00002000,
0x00042004, 0x10000000, 0x00000000, 0x00040004,
0x10002004, 0x00042004, 0x10042000, 0x10000004,
0x10000000, 0x00040000, 0x00002004, 0x10042004,
0x00040004, 0x10042000, 0x10002000, 0x00042004,
0x10042004, 0x00040004, 0x10000004, 0x00000000,
0x10000000, 0x00002004, 0x00040000, 0x10040004,
0x00002000, 0x10000000, 0x00042004, 0x10002004,
0x10042000, 0x00002000, 0x00000000, 0x10000004,
0x00000004, 0x10042004, 0x00042000, 0x10040000,
0x10040004, 0x00040000, 0x00002004, 0x10002000,
0x10002004, 0x00000004, 0x10040000, 0x00042000 },

/* nibble 2 */
{ 0x41000000, 0x01010040, 0x00000040, 0x41000040,
0x40010000, 0x01000000, 0x41000040, 0x00010040,
0x01000040, 0x00010000, 0x01010000, 0x40000000,
0x41010040, 0x40000040, 0x40000000, 0x41010000,
0x00000000, 0x40010000, 0x01010040, 0x00000040,
0x40000040, 0x41010040, 0x00010000, 0x41000000,
0x41010000, 0x01000040, 0x40010040, 0x01010000,
0x00010040, 0x00000000, 0x01000000, 0x40010040,
0x01010040, 0x00000040, 0x40000000, 0x00010000,
0x40000040, 0x40010000, 0x01010000, 0x41000040,
0x00000000, 0x01010040, 0x00010040, 0x41010000,
0x40010000, 0x01000000, 0x41010040, 0x40000000,
0x40010040, 0x41000000, 0x01000000, 0x41010040,
0x00010000, 0x01000040, 0x41000040, 0x00010040,
0x01000040, 0x00000000, 0x41010000, 0x40000040,
0x41000000, 0x40010040, 0x00000040, 0x01010000 },

/* nibble 3 */
{ 0x00100402, 0x04000400, 0x00000002, 0x04100402,
0x00000000, 0x04100000, 0x04000402, 0x00100002,
0x04100400, 0x04000002, 0x04000000, 0x00000402,
0x04000002, 0x00100402, 0x00100000, 0x04000000,
0x04100002, 0x00100400, 0x00000400, 0x00000002,
0x00100400, 0x04000402, 0x04100000, 0x00000400,
0x00000402, 0x00000000, 0x00100002, 0x04100400,
0x04000400, 0x04100002, 0x04100402, 0x00100000,
0x04100002, 0x00000402, 0x00100000, 0x04000002,
0x00100400, 0x04000400, 0x00000002, 0x04100000,
0x04000402, 0x00000000, 0x00000400, 0x00100002,
0x00000000, 0x04100002, 0x04100400, 0x00000400,
0x04000000, 0x04100402, 0x00100402, 0x00100000,
0x04100402, 0x00000002, 0x04000400, 0x00100402,
0x00100002, 0x00100400, 0x04100000, 0x04000402,
0x00000402, 0x04000000, 0x04000002, 0x04100400 },

/* nibble 4 */
{ 0x02000000, 0x00004000, 0x00000100, 0x02004108,
0x02004008, 0x02000100, 0x00004108, 0x02004000,
0x00004000, 0x00000008, 0x02000008, 0x00004100,
0x02000108, 0x02004008, 0x02004100, 0x00000000,
0x00004100, 0x02000000, 0x00004008, 0x00000108,
0x02000100, 0x00004108, 0x00000000, 0x02000008,
0x00000008, 0x02000108, 0x02004108, 0x00004008,
0x02004000, 0x00000100, 0x00000108, 0x02004100,
0x02004100, 0x02000108, 0x00004008, 0x02004000,
0x00004000, 0x00000008, 0x02000008, 0x02000100,
0x02000000, 0x00004100, 0x02004108, 0x00000000,
0x00004108, 0x02000000, 0x00000100, 0x00004008,
0x02000108, 0x00000100, 0x00000000, 0x02004108,
0x02004008, 0x02004100, 0x00000108, 0x00004000,
0x00004100, 0x02004008, 0x02000100, 0x00000108,
0x00000008, 0x00004108, 0x02004000, 0x02000008 },

/* nibble 5 */
{ 0x20000010, 0x00080010, 0x00000000, 0x20080800,
0x00080010, 0x00000800, 0x20000810, 0x00080000,
0x00000810, 0x20080810, 0x00080800, 0x20000000,
0x20000800, 0x20000010, 0x20080000, 0x00080810,
0x00080000, 0x20000810, 0x20080010, 0x00000000,
0x00000800, 0x00000010, 0x20080800, 0x20080010,
0x20080810, 0x20080000, 0x20000000, 0x00000810,
0x00000010, 0x00080800, 0x00080810, 0x20000800,
0x00000810, 0x20000000, 0x20000800, 0x00080810,
0x20080800, 0x00080010, 0x00000000, 0x20000800,
0x20000000, 0x00000800, 0x20080010, 0x00080000,
0x00080010, 0x20080810, 0x00080800, 0x00000010,
0x20080810, 0x00080800, 0x00080000, 0x20000810,
0x20000010, 0x20080000, 0x00080810, 0x00000000,
0x00000800, 0x20000010, 0x20000810, 0x20080800,
0x20080000, 0x00000810, 0x00000010, 0x20080010 },

/* nibble 6 */
{ 0x00001000, 0x00000080, 0x00400080, 0x00400001,
0x00401081, 0x00001001, 0x00001080, 0x00000000,
0x00400000, 0x00400081, 0x00000081, 0x00401000,
0x00000001, 0x00401080, 0x00401000, 0x00000081,
0x00400081, 0x00001000, 0x00001001, 0x00401081,
0x00000000, 0x00400080, 0x00400001, 0x00001080,
0x00401001, 0x00001081, 0x00401080, 0x00000001,
0x00001081, 0x00401001, 0x00000080, 0x00400000,
0x00001081, 0x00401000, 0x00401001, 0x00000081,
0x00001000, 0x00000080, 0x00400000, 0x00401001,
0x00400081, 0x00001081, 0x00001080, 0x00000000,
0x00000080, 0x00400001, 0x00000001, 0x00400080,
0x00000000, 0x00400081, 0x00400080, 0x00001080,
0x00000081, 0x00001000, 0x00401081, 0x00400000,
0x00401080, 0x00000001, 0x00001001, 0x00401081,
0x00400001, 0x00401080, 0x00401000, 0x00001001 },

/* nibble 7 */
{ 0x08200020, 0x08208000, 0x00008020, 0x00000000,
0x08008000, 0x00200020, 0x08200000, 0x08208020,
0x00000020, 0x08000000, 0x00208000, 0x00008020,
0x00208020, 0x08008020, 0x08000020, 0x08200000,
0x00008000, 0x00208020, 0x00200020, 0x08008000,
0x08208020, 0x08000020, 0x00000000, 0x00208000,
0x08000000, 0x00200000, 0x08008020, 0x08200020,
0x00200000, 0x00008000, 0x08208000, 0x00000020,
0x00200000, 0x00008000, 0x08000020, 0x08208020,
0x00008020, 0x08000000, 0x00000000, 0x00208000,
0x08200020, 0x08008020, 0x08008000, 0x00200020,
0x08208000, 0x00000020, 0x00200020, 0x08008000,
0x08208020, 0x00200000, 0x08200000, 0x08000020,
0x00208000, 0x00008020, 0x08008020, 0x08200000,
0x00000020, 0x08208000, 0x00208020, 0x00000000,
0x08000000, 0x08200020, 0x00008000, 0x00208020 }};

/* Some stuff that used to be in des_locl.h.  Heavily modified. */
        /* IP and FP
         * The problem is more of a geometric problem that random bit fiddling.
         0  1  2  3  4  5  6  7      62 54 46 38 30 22 14  6
         8  9 10 11 12 13 14 15      60 52 44 36 28 20 12  4
        16 17 18 19 20 21 22 23      58 50 42 34 26 18 10  2
        24 25 26 27 28 29 30 31  to  56 48 40 32 24 16  8  0

        32 33 34 35 36 37 38 39      63 55 47 39 31 23 15  7
        40 41 42 43 44 45 46 47      61 53 45 37 29 21 13  5
        48 49 50 51 52 53 54 55      59 51 43 35 27 19 11  3
        56 57 58 59 60 61 62 63      57 49 41 33 25 17  9  1

        The output has been subject to swaps of the form
        0 1 -> 3 1 but the odd and even bits have been put into
        2 3    2 0
        different words.  The main trick is to remember that
        t=((l>>size)^r)&(mask);
        r^=t;
        l^=(t<<size);
        can be used to swap and move bits between words.

        So l =  0  1  2  3  r = 16 17 18 19
                4  5  6  7      20 21 22 23
                8  9 10 11      24 25 26 27
               12 13 14 15      28 29 30 31
        becomes (for size == 2 and mask == 0x3333)
           t =   2^16  3^17 -- --   l =  0  1 16 17  r =  2  3 18 19
                 6^20  7^21 -- --        4  5 20 21       6  7 22 23
                10^24 11^25 -- --        8  9 24 25      10 11 24 25
                14^28 15^29 -- --       12 13 28 29      14 15 28 29

        Thanks for hints from Richard Outerbridge - he told me IP&FP
        could be done in 15 xor, 10 shifts and 5 ands.
        When I finally started to think of the problem in 2D
        I first got ~42 operations without xors.  When I remembered
        how to use xors :-) I got it to its final state.
        */
#define PERM_OP(a,b,t,n,m) ((t)=((((a)>>(n))^(b))&(m)),\
        (b)^=(t),\
        (a)^=((t)<<(n)))

#if !defined(ASM_DES)

#define IP(l,r,t) \
        PERM_OP(r,l,t, 4,0x0f0f0f0f); \
        PERM_OP(l,r,t,16,0x0000ffff); \
        PERM_OP(r,l,t, 2,0x33333333); \
        PERM_OP(l,r,t, 8,0x00ff00ff); \
        PERM_OP(r,l,t, 1,0x55555555);

#define FP(l,r,t) \
        PERM_OP(l,r,t, 1,0x55555555); \
        PERM_OP(r,l,t, 8,0x00ff00ff); \
        PERM_OP(l,r,t, 2,0x33333333); \
        PERM_OP(r,l,t,16,0x0000ffff); \
        PERM_OP(l,r,t, 4,0x0f0f0f0f);

#define D_ENCRYPT(L,R,S)        \
        u=(R^s[S  ]); \
        t=R^s[S+1]; \
        t=((t>>4)+(t<<28)); \
        L^=     ssh_des_SPtrans[1][(t    )&0x3f]| \
                ssh_des_SPtrans[3][(t>> 8)&0x3f]| \
                ssh_des_SPtrans[5][(t>>16)&0x3f]| \
                ssh_des_SPtrans[7][(t>>24)&0x3f]| \
                ssh_des_SPtrans[0][(u    )&0x3f]| \
                ssh_des_SPtrans[2][(u>> 8)&0x3f]| \
                ssh_des_SPtrans[4][(u>>16)&0x3f]| \
                ssh_des_SPtrans[6][(u>>24)&0x3f];


/* This part is based on code that used to be in ecb_enc.c. */
/* Copyright (C) 1993 Eric Young - see README for more details */

void ssh_des_encrypt(SshUInt32 l, SshUInt32 r, SshUInt32 *output, SshUInt32 *s,
                 int for_encryption)
{
  register SshUInt32 t,u;
  register int i;

  IP(l,r,t);

  /* Things have been modified so that the initial rotate is
   * done outside the loop.  This required the
   * des_SPtrans values in sp.h to be rotated 1 bit to the right.
   * One perl script later and things have a 5% speed up on a sparc2.
   * Thanks to Richard Outerbridge <71755.204@CompuServe.COM>
   * for pointing this out. */
  t=(r<<1)|(r>>31);
  r=(l<<1)|(l>>31);
  l=t;

  /* I don't know if it is worth the effort of loop unrolling the
   * inner loop */
  if (for_encryption)
    {
      for (i=0; i<32; i+=4)
        {
          D_ENCRYPT(l,r,i+0); /*  1 */
          D_ENCRYPT(r,l,i+2); /*  2 */
        }
    }
  else
    {
      for (i=30; i>0; i-=4)
        {
          D_ENCRYPT(l,r,i-0); /* 16 */
          D_ENCRYPT(r,l,i-2); /* 15 */
        }
    }
  l=(l>>1)|(l<<31);
  r=(r>>1)|(r<<31);

  FP(r,l,t);

  output[0] = l;
  output[1] = r;
}

/* Triple des encrypt/decrypt/encrypt and decrypt/encrypt/decrypt */

void ssh_des_ede_encrypt(SshUInt32 l, SshUInt32 r, SshUInt32 output[2],
                     SshUInt32 *s, Boolean for_encryption)
{
  register SshUInt32 t,u;
  register int i;

  IP(l,r,t);

  /* Things have been modified so that the initial rotate is
   * done outside the loop.  This required the
   * ssh_des_SPtrans values in sp.h to be rotated 1 bit to the right.
   * One perl script later and things have a 5% speed up on a sparc2.
   * Thanks to Richard Outerbridge <71755.204@CompuServe.COM>
   * for pointing this out. */
  t=(r<<1)|(r>>31);
  r=(l<<1)|(l>>31);
  l=t;

  /* I don't know if it is worth the effort of loop unrolling the
   * inner loop */
  if (for_encryption)
    {
      for (i=0; i<32; i+=4)
        {
          D_ENCRYPT(l,r,i+0); /*  1 */
          D_ENCRYPT(r,l,i+2); /*  2 */
        }
      for (i = 62; i > 32; i-=4)
        {
          D_ENCRYPT(r,l,i-0);
          D_ENCRYPT(l,r,i-2);
        }
      for (i = 64; i < 96; i+=4)
        {
          D_ENCRYPT(l,r,i+0);
          D_ENCRYPT(r,l,i+2);
        }
    }
  else
    {
      for (i=94; i>64; i-=4)
        {
          D_ENCRYPT(l,r,i-0); /*  127 */
          D_ENCRYPT(r,l,i-2); /*  126 */
        }
      for (i = 32; i < 64; i+=4)
        {
          D_ENCRYPT(r,l,i+0);
          D_ENCRYPT(l,r,i+2);
        }
      for (i = 30; i > 0; i-=4)
        {
          D_ENCRYPT(l,r,i-0);
          D_ENCRYPT(r,l,i-2);
        }
    }
  l=(l>>1)|(l<<31);
  r=(r>>1)|(r<<31);

  FP(r,l,t);
  output[0] = l;
  output[1] = r;
}

#else

/* Prototypes for assembler code. */

void ssh_des_encrypt(SshUInt32 l, SshUInt32 r, SshUInt32 *output, SshUInt32 *s,
                 int for_encryption);

void ssh_des_ede_encrypt(SshUInt32 l, SshUInt32 r, SshUInt32 output[2],
                     SshUInt32 *s, int for_encryption);

#endif /* !ASM_DES */

/* Code based on set_key.c. */
/* Copyright (C) 1993 Eric Young - see README for more details */

#define HPERM_OP(a,t,n,m) ((t)=((((a)<<(16-(n)))^(a))&(m)),\
        (a)=(a)^(t)^(t>>(16-(n))))

void ssh_des_set_key(const unsigned char *key, SshUInt32 *schedule)
{
  register SshUInt32 c, d, t, s, shifts;
  register int i;

  c = SSH_GET_32BIT_LSB_FIRST(key);
  d = SSH_GET_32BIT_LSB_FIRST(key + 4);

  /* I now do it in 47 simple operations :-)
   * Thanks to John Fletcher (john_fletcher@lccmail.ocf.llnl.gov)
   * for the inspiration. :-) */
  PERM_OP(d,c,t,4,0x0f0f0f0f);
  HPERM_OP(c,t,-2,0xcccc0000);
  HPERM_OP(d,t,-2,0xcccc0000);
  PERM_OP(d,c,t,1,0x55555555);
  PERM_OP(c,d,t,8,0x00ff00ff);
  PERM_OP(d,c,t,1,0x55555555);
  d = ((d & 0xff) << 16) | (d & 0xff00) |
    ((d >> 16) & 0xff) | ((c >> 4) & 0xf000000);
  c&=0x0fffffff;

  shifts = 0x7efc;
  for (i=0; i < 16; i++)
    {
      if (shifts & 1)
        { c=((c>>2)|(c<<26)); d=((d>>2)|(d<<26)); }
      else
        { c=((c>>1)|(c<<27)); d=((d>>1)|(d<<27)); }
      shifts >>= 1;
      c&=0x0fffffff;
      d&=0x0fffffff;

      /* could be a few less shifts but I am to lazy at this
       * point in time to investigate */

      s = ssh_des_skb[0][ (c    )&0x3f                ] |
          ssh_des_skb[1][((c>> 6)&0x03)|((c>> 7)&0x3c)] |
          ssh_des_skb[2][((c>>13)&0x0f)|((c>>14)&0x30)] |
          ssh_des_skb[3][((c>>20)&0x01)|((c>>21)&0x06)|((c>>22)&0x38)];

      t = ssh_des_skb[4][ (d    )&0x3f                ] |
          ssh_des_skb[5][((d>> 7)&0x03)|((d>> 8)&0x3c)] |
          ssh_des_skb[6][ (d>>15)&0x3f                ] |
          ssh_des_skb[7][((d>>21)&0x0f)|((d>>22)&0x30)];

      /* table contained 0213 4657 */
      *schedule++ = ((t << 16) | (s & 0xffff));
      s = ((s >> 16) | (t & 0xffff0000));
      *schedule++ = (s << 4) | (s >> 28);
    }
}

/* Single des */

size_t ssh_des_ctxsize()
{
  return sizeof(SshDESContext);
}


SshCryptoStatus ssh_des_init(void *ptr,
                             const unsigned char *key, size_t keylen,
                             Boolean for_encryption)
{
  SshDESContext *ctx = (SshDESContext *)ptr;
  
  if (keylen < 8)
    return SSH_CRYPTO_KEY_TOO_SHORT;
  
  ctx->for_encryption = for_encryption;
  ssh_des_set_key(key, ctx->key_schedule);

  return SSH_CRYPTO_OK;
}

/* Table of weak keys that are checked for. This includes the usual
   weak and semi-weak keys. */
#define SSH_DES_WEAK_KEYS  (4 + 6*2)
static const unsigned char ssh_des_weak_keys[SSH_DES_WEAK_KEYS][8] =
{
  /* The weak keys. */
  { 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 },
  { 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe },
  { 0x1f, 0x1f, 0x1f, 0x1f, 0x0e, 0x0e, 0x0e, 0x0e },
  { 0xe0, 0xe0, 0xe0, 0xe0, 0xf1, 0xf1, 0xf1, 0xf1 },

  /* The semi-weak keys. */
  { 0x01, 0xfe, 0x01, 0xfe, 0x01, 0xfe, 0x01, 0xfe },
  { 0xfe, 0x01, 0xfe, 0x01, 0xfe, 0x01, 0xfe, 0x01 },

  { 0x1f, 0xe0, 0x1f, 0xe0, 0x0e, 0xf1, 0x0e, 0xf1 },
  { 0xe0, 0x1f, 0xe0, 0x1f, 0xf1, 0x0e, 0xf1, 0x0e },

  { 0x01, 0xe0, 0x01, 0xe0, 0x01, 0xf1, 0x01, 0xf1 },
  { 0xe0, 0x01, 0xe0, 0x01, 0xf1, 0x01, 0xf1, 0x01 },

  { 0x1f, 0xfe, 0x1f, 0xfe, 0x0e, 0xfe, 0x0e, 0xfe },
  { 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x0e, 0xfe, 0x0e },

  { 0x01, 0x1f, 0x01, 0x1f, 0x01, 0x0e, 0x01, 0x0e },
  { 0x1f, 0x01, 0x1f, 0x01, 0x0e, 0x01, 0x0e, 0x01 },

  { 0xe0, 0xfe, 0xe0, 0xfe, 0xf1, 0xfe, 0xf1, 0xfe },
  { 0xfe, 0xe0, 0xfe, 0xe0, 0xfe, 0xf1, 0xfe, 0xf1 }
};

Boolean ssh_des_init_is_weak_key(const unsigned char *key)
{
  int i;

  /* Do weak key checks. */
  for (i = 0; i < SSH_DES_WEAK_KEYS; i++)
    {
      int j, match;

      for (j = 0, match = 0; j < 8; j++)
        {
          if ((key[j] & 0xfe) != (ssh_des_weak_keys[i][j] & 0xfe))
            break;      

          match++;
        }

      /* Was a weak key? */
      if (match == 8)
        return TRUE;
    }
  return FALSE;
}


SshCryptoStatus
ssh_des_init_with_key_check(void *ptr,
                            const unsigned char *key, size_t keylen,
                            Boolean for_encryption)
{
  SshDESContext *ctx = (SshDESContext *)ptr;

  if (keylen < 8)
    return SSH_CRYPTO_KEY_TOO_SHORT;

  if (ssh_des_init_is_weak_key(key))
    return SSH_CRYPTO_KEY_WEAK;

  /* Not a weak key continue. */
  ctx->for_encryption = for_encryption;
  ssh_des_set_key(key, ctx->key_schedule);

  return SSH_CRYPTO_OK;
}

void ssh_des_ecb(void *context, unsigned char *dest,
                 const unsigned char *src, size_t len,
                 unsigned char *iv)
{
  SshDESContext *ctx = (SshDESContext *)context;
  SshUInt32 output[2], l, r;
  Boolean for_encryption = ctx->for_encryption;

  while (len)
    {
      l = SSH_GET_32BIT_LSB_FIRST(src);
      r = SSH_GET_32BIT_LSB_FIRST(src + 4);

      ssh_des_encrypt(l, r, output, ctx->key_schedule, for_encryption);

      SSH_PUT_32BIT_LSB_FIRST(dest, output[0]);
      SSH_PUT_32BIT_LSB_FIRST(dest + 4, output[1]);

      src += 8;
      dest += 8;
      len -= 8;
    }
}

void ssh_des_cbc(void *context, unsigned char *dest,
                 const unsigned char *src, size_t len,
                 unsigned char *iv_arg)
{
  SshDESContext *ctx = (SshDESContext *)context;
  SshUInt32 l, r, iv[2], temp[2];
  Boolean for_encryption = ctx->for_encryption;

  iv[0] = SSH_GET_32BIT_LSB_FIRST(iv_arg);
  iv[1] = SSH_GET_32BIT_LSB_FIRST(iv_arg + 4);

  if (for_encryption)
    {
      while (len)
        {
          l = SSH_GET_32BIT_LSB_FIRST(src) ^ iv[0];
          r = SSH_GET_32BIT_LSB_FIRST(src + 4) ^ iv[1];

          ssh_des_encrypt(l, r, iv, ctx->key_schedule, for_encryption);

          SSH_PUT_32BIT_LSB_FIRST(dest, iv[0]);
          SSH_PUT_32BIT_LSB_FIRST(dest + 4, iv[1]);

          src += 8;
          dest += 8;
          len -= 8;
        }
    }
  else
    {
      while (len)
        {
          l = SSH_GET_32BIT_LSB_FIRST(src);
          r = SSH_GET_32BIT_LSB_FIRST(src + 4);

          ssh_des_encrypt(l, r, temp, ctx->key_schedule, for_encryption);

          temp[0] ^= iv[0];
          temp[1] ^= iv[1];

          SSH_PUT_32BIT_LSB_FIRST(dest, temp[0]);
          SSH_PUT_32BIT_LSB_FIRST(dest + 4, temp[1]);

          iv[0] = l;
          iv[1] = r;

          src += 8;
          dest += 8;
          len -= 8;
        }
    }

  SSH_PUT_32BIT_LSB_FIRST(iv_arg, iv[0]);
  SSH_PUT_32BIT_LSB_FIRST(iv_arg + 4, iv[1]);
}

void ssh_des_cfb(void *context, unsigned char *dest,
                 const unsigned char *src, size_t len,
                 unsigned char *iv)
{
  SshDESContext *ctx = (SshDESContext *)context;
  SshUInt32 l, r, temp[2];

  l = SSH_GET_32BIT_LSB_FIRST(iv);
  r = SSH_GET_32BIT_LSB_FIRST(iv + 4);

  if (ctx->for_encryption)
    {
      while (len)
        {
          ssh_des_encrypt(l, r, temp, ctx->key_schedule, TRUE);

          l = SSH_GET_32BIT_LSB_FIRST(src) ^ temp[0];
          r = SSH_GET_32BIT_LSB_FIRST(src + 4) ^ temp[1];

          temp[0] = l;
          temp[1] = r;

          SSH_PUT_32BIT_LSB_FIRST(dest, temp[0]);
          SSH_PUT_32BIT_LSB_FIRST(dest + 4, temp[1]);

          src += 8;
          dest += 8;
          len -= 8;
        }
    }
  else
    {
      while (len)
        {
          ssh_des_encrypt(l, r, temp, ctx->key_schedule, TRUE);

          l = SSH_GET_32BIT_LSB_FIRST(src);
          r = SSH_GET_32BIT_LSB_FIRST(src + 4);

          temp[0] ^= l;
          temp[1] ^= r;

          SSH_PUT_32BIT_LSB_FIRST(dest, temp[0]);
          SSH_PUT_32BIT_LSB_FIRST(dest + 4, temp[1]);

          src += 8;
          dest += 8;
          len -= 8;
        }
    }

  SSH_PUT_32BIT_LSB_FIRST(iv, l);
  SSH_PUT_32BIT_LSB_FIRST(iv + 4, r);
}

void ssh_des_ofb(void *context, unsigned char *dest,
                 const unsigned char *src, size_t len,
                 unsigned char *iv_arg)
{
  SshDESContext *ctx = (SshDESContext *)context;
  SshUInt32 iv[2], l, r;

  iv[0] = SSH_GET_32BIT_LSB_FIRST(iv_arg);
  iv[1] = SSH_GET_32BIT_LSB_FIRST(iv_arg + 4);

  while (len)
    {
      l = iv[0];
      r = iv[1];

      ssh_des_encrypt(l, r, iv, ctx->key_schedule, TRUE);

      l = SSH_GET_32BIT_LSB_FIRST(src) ^ iv[0];
      r = SSH_GET_32BIT_LSB_FIRST(src + 4) ^ iv[1];

      SSH_PUT_32BIT_LSB_FIRST(dest, l);
      SSH_PUT_32BIT_LSB_FIRST(dest + 4, r);

      src += 8;
      dest += 8;
      len -= 8;
    }

  SSH_PUT_32BIT_LSB_FIRST(iv_arg, iv[0]);
  SSH_PUT_32BIT_LSB_FIRST(iv_arg + 4, iv[1]);
}

void ssh_des_cbc_mac(void *context, const unsigned char *src, size_t len,
                     unsigned char *iv_arg)
{
  SshDESContext *ctx = (SshDESContext *)context;
  SshUInt32 l, r, iv[2];

  iv[0] = SSH_GET_32BIT_LSB_FIRST(iv_arg);
  iv[1] = SSH_GET_32BIT_LSB_FIRST(iv_arg + 4);

  while (len)
    {
      l = SSH_GET_32BIT_LSB_FIRST(src) ^ iv[0];
      r = SSH_GET_32BIT_LSB_FIRST(src + 4) ^ iv[1];

      ssh_des_encrypt(l, r, iv, ctx->key_schedule, ctx->for_encryption);

      src += 8;
      len -= 8;
    }

  SSH_PUT_32BIT_LSB_FIRST(iv_arg, iv[0]);
  SSH_PUT_32BIT_LSB_FIRST(iv_arg + 4, iv[1]);
}

/* Triple des */

size_t ssh_des3_ctxsize()
{
  return sizeof(SshTripleDESContext);
}

SshCryptoStatus
ssh_des3_init(void *ptr,
              const unsigned char *key, size_t keylen,
              Boolean for_encryption)
{
  SshTripleDESContext *ctx = (SshTripleDESContext *)ptr;

  if (keylen < 24)
    return SSH_CRYPTO_KEY_TOO_SHORT;

  ctx->for_encryption = for_encryption;

  ssh_des_set_key(key, &ctx->key_schedule[0]);
  ssh_des_set_key(&key[8], &ctx->key_schedule[1*32]);
  ssh_des_set_key(&key[16], &ctx->key_schedule[2*32]);

  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_des3_init_with_key_check(void *ptr,
                             const unsigned char *key, size_t keylen,
                             Boolean for_encryption)
{
  SshTripleDESContext *ctx = (SshTripleDESContext *)ptr;

  ctx->for_encryption = for_encryption;

  if (keylen < 24)
    return SSH_CRYPTO_KEY_TOO_SHORT;

  /* Check des weak keys. Is this really needed? Better put it here. */
  if (ssh_des_init_is_weak_key(key))
    return SSH_CRYPTO_KEY_WEAK;
  if (ssh_des_init_is_weak_key(key + 8))
    return SSH_CRYPTO_KEY_WEAK;
  if (ssh_des_init_is_weak_key(key + 16))
    return SSH_CRYPTO_KEY_WEAK;

  /* Check if K1 is same than K2, or K2 is same than K3. */
  if (memcmp(key, key + 8, 8) == 0 ||
      memcmp(key + 8, key + 16, 8) == 0)
    return SSH_CRYPTO_KEY_INVALID;

  ssh_des_set_key(key, &ctx->key_schedule[0]);
  ssh_des_set_key(&key[8], &ctx->key_schedule[1*32]);
  ssh_des_set_key(&key[16], &ctx->key_schedule[2*32]);

  return SSH_CRYPTO_OK;
}

void ssh_des3_ecb(void *context, unsigned char *dest,
              const unsigned char *src, size_t len,
              unsigned char *iv)
{
  SshTripleDESContext *ctx = (SshTripleDESContext *)context;
  SshUInt32 output[2], l, r;
  Boolean for_encryption = ctx->for_encryption;

  while (len)
    {
      l = SSH_GET_32BIT_LSB_FIRST(src);
      r = SSH_GET_32BIT_LSB_FIRST(src + 4);

      ssh_des_ede_encrypt(l, r, output, ctx->key_schedule, for_encryption);

      SSH_PUT_32BIT_LSB_FIRST(dest, output[0]);
      SSH_PUT_32BIT_LSB_FIRST(dest + 4, output[1]);

      src += 8;
      dest += 8;
      len -= 8;
    }
}

void ssh_des3_cbc(void *context, unsigned char *dest,
              const unsigned char *src, size_t len,
              unsigned char *iv_arg)
{
  SshTripleDESContext *ctx = (SshTripleDESContext *)context;
  SshUInt32 l, r, iv[2], temp[2];
  Boolean for_encryption = ctx->for_encryption;

  iv[0] = SSH_GET_32BIT_LSB_FIRST(iv_arg);
  iv[1] = SSH_GET_32BIT_LSB_FIRST(iv_arg + 4);

  if (for_encryption)
    {
      while (len)
        {
          l = SSH_GET_32BIT_LSB_FIRST(src) ^ iv[0];
          r = SSH_GET_32BIT_LSB_FIRST(src + 4) ^ iv[1];

          ssh_des_ede_encrypt(l, r, iv, ctx->key_schedule, for_encryption);

          SSH_PUT_32BIT_LSB_FIRST(dest, iv[0]);
          SSH_PUT_32BIT_LSB_FIRST(dest + 4, iv[1]);

          src += 8;
          dest += 8;
          len -= 8;
        }
    }
  else
    {
      while (len)
        {
          l = SSH_GET_32BIT_LSB_FIRST(src);
          r = SSH_GET_32BIT_LSB_FIRST(src + 4);

          ssh_des_ede_encrypt(l, r, temp, ctx->key_schedule, for_encryption);

          temp[0] ^= iv[0];
          temp[1] ^= iv[1];

          SSH_PUT_32BIT_LSB_FIRST(dest, temp[0]);
          SSH_PUT_32BIT_LSB_FIRST(dest + 4, temp[1]);

          iv[0] = l;
          iv[1] = r;

          src += 8;
          dest += 8;
          len -= 8;
        }
    }

  SSH_PUT_32BIT_LSB_FIRST(iv_arg, iv[0]);
  SSH_PUT_32BIT_LSB_FIRST(iv_arg + 4, iv[1]);
}

void ssh_des3_cfb(void *context, unsigned char *dest,
              const unsigned char *src, size_t len,
              unsigned char *iv)
{
  SshTripleDESContext *ctx = (SshTripleDESContext *)context;
  SshUInt32 l, r, temp[2];

  l = SSH_GET_32BIT_LSB_FIRST(iv);
  r = SSH_GET_32BIT_LSB_FIRST(iv + 4);

  if (ctx->for_encryption)
    {
      while (len)
        {
          ssh_des_ede_encrypt(l, r, temp, ctx->key_schedule, TRUE);

          l = temp[0] ^= SSH_GET_32BIT_LSB_FIRST(src);
          r = temp[1] ^= SSH_GET_32BIT_LSB_FIRST(src + 4);

          SSH_PUT_32BIT_LSB_FIRST(dest, temp[0]);
          SSH_PUT_32BIT_LSB_FIRST(dest + 4, temp[1]);

          src += 8;
          dest += 8;
          len -= 8;
        }
    }
  else
    {
      while (len)
        {
          ssh_des_ede_encrypt(l, r, temp, ctx->key_schedule, TRUE);

          l = SSH_GET_32BIT_LSB_FIRST(src);
          r = SSH_GET_32BIT_LSB_FIRST(src + 4);

          temp[0] ^= l;
          temp[1] ^= r;

          SSH_PUT_32BIT_LSB_FIRST(dest, temp[0]);
          SSH_PUT_32BIT_LSB_FIRST(dest + 4, temp[1]);

          src += 8;
          dest += 8;
          len -= 8;
        }
    }

  SSH_PUT_32BIT_LSB_FIRST(iv, l);
  SSH_PUT_32BIT_LSB_FIRST(iv + 4, r);
}

void ssh_des3_ofb(void *context, unsigned char *dest,
                  const unsigned char *src, size_t len,
                  unsigned char *iv_arg)
{
  SshTripleDESContext *ctx = (SshTripleDESContext *)context;
  SshUInt32 iv[2], l, r;

  iv[0] = SSH_GET_32BIT_LSB_FIRST(iv_arg);
  iv[1] = SSH_GET_32BIT_LSB_FIRST(iv_arg + 4);

  while (len)
    {
      l = iv[0];
      r = iv[1];

      ssh_des_ede_encrypt(l, r, iv, ctx->key_schedule, TRUE);

      l = SSH_GET_32BIT_LSB_FIRST(src) ^ iv[0];
      r = SSH_GET_32BIT_LSB_FIRST(src + 4) ^ iv[1];

      SSH_PUT_32BIT_LSB_FIRST(dest, l);
      SSH_PUT_32BIT_LSB_FIRST(dest + 4, r);

      src += 8;
      dest += 8;
      len -= 8;
    }

  SSH_PUT_32BIT_LSB_FIRST(iv_arg, iv[0]);
  SSH_PUT_32BIT_LSB_FIRST(iv_arg + 4, iv[1]);
}

void ssh_des3_cbc_mac(void *context, const unsigned char *src, size_t len,
                      unsigned char *iv_arg)
{
  SshTripleDESContext *ctx = (SshTripleDESContext *)context;
  SshUInt32 l, r, iv[2];

  iv[0] = SSH_GET_32BIT_LSB_FIRST(iv_arg);
  iv[1] = SSH_GET_32BIT_LSB_FIRST(iv_arg + 4);

  while (len)
    {
      l = SSH_GET_32BIT_LSB_FIRST(src) ^ iv[0];
      r = SSH_GET_32BIT_LSB_FIRST(src + 4) ^ iv[1];

      ssh_des_ede_encrypt(l, r, iv, ctx->key_schedule, ctx->for_encryption);

      src += 8;
      len -= 8;
    }

  SSH_PUT_32BIT_LSB_FIRST(iv_arg, iv[0]);
  SSH_PUT_32BIT_LSB_FIRST(iv_arg + 4, iv[1]);
}

#endif /* WITHOUT_DES */
