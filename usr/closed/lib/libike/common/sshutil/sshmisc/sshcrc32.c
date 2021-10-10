/* The implementation here was originally done by Gary S. Brown.  I have
   borrowed the tables directly, and made some minor changes to the
   crc32-function (including changing the interface). //ylo */

#include "sshincludes.h"
#include "sshcrc32.h"

  /* ============================================================= */
  /*  COPYRIGHT (C) 1986 Gary S. Brown.  You may use this program, or       */
  /*  code or tables extracted from it, as desired without restriction.     */
  /*                                                                        */
  /*  First, the polynomial itself and its table of feedback terms.  The    */
  /*  polynomial is                                                         */
  /*  X^32+X^26+X^23+X^22+X^16+X^12+X^11+X^10+X^8+X^7+X^5+X^4+X^2+X^1+X^0   */
  /*                                                                        */
  /*  Note that we take it "backwards" and put the highest-order term in    */
  /*  the lowest-order bit.  The X^32 term is "implied"; the LSB is the     */
  /*  X^31 term, etc.  The X^0 term (usually shown as "+1") results in      */
  /*  the MSB being 1.                                                      */
  /*                                                                        */
  /*  Note that the usual hardware shift register implementation, which     */
  /*  is what we're using (we're merely optimizing it by doing eight-bit    */
  /*  chunks at a time) shifts bits into the lowest-order term.  In our     */
  /*  implementation, that means shifting towards the right.  Why do we     */
  /*  do it this way?  Because the calculated CRC must be transmitted in    */
  /*  order from highest-order term to lowest-order term.  UARTs transmit   */
  /*  characters in order from LSB to MSB.  By storing the CRC this way,    */
  /*  we hand it to the UART in the order low-byte to high-byte; the UART   */
  /*  sends each low-bit to hight-bit; and the result is transmission bit   */
  /*  by bit from highest- to lowest-order term without requiring any bit   */
  /*  shuffling on our part.  Reception works similarly.                    */
  /*                                                                        */
  /*  The feedback terms table consists of 256, 32-bit entries.  Notes:     */
  /*                                                                        */
  /*      The table can be generated at runtime if desired; code to do so   */
  /*      is shown later.  It might not be obvious, but the feedback        */
  /*      terms simply represent the results of eight shift/xor opera-      */
  /*      tions for all combinations of data and CRC register values.       */
  /*                                                                        */
  /*      The values must be right-shifted by eight bits by the "updcrc"    */
  /*      logic; the shift must be unsigned (bring in zeroes).  On some     */
  /*      hardware you could probably optimize the shift in assembler by    */
  /*      using byte-swap instructions.                                     */
  /*      polynomial $edb88320                                              */
  /*                                                                        */
  /*  --------------------------------------------------------------------  */

static const SshUInt32 SSH_CODE_SEGMENT crc32_tab[] = {
      0x00000000UL, 0x77073096UL, 0xee0e612cUL, 0x990951baUL, 0x076dc419UL,
      0x706af48fUL, 0xe963a535UL, 0x9e6495a3UL, 0x0edb8832UL, 0x79dcb8a4UL,
      0xe0d5e91eUL, 0x97d2d988UL, 0x09b64c2bUL, 0x7eb17cbdUL, 0xe7b82d07UL,
      0x90bf1d91UL, 0x1db71064UL, 0x6ab020f2UL, 0xf3b97148UL, 0x84be41deUL,
      0x1adad47dUL, 0x6ddde4ebUL, 0xf4d4b551UL, 0x83d385c7UL, 0x136c9856UL,
      0x646ba8c0UL, 0xfd62f97aUL, 0x8a65c9ecUL, 0x14015c4fUL, 0x63066cd9UL,
      0xfa0f3d63UL, 0x8d080df5UL, 0x3b6e20c8UL, 0x4c69105eUL, 0xd56041e4UL,
      0xa2677172UL, 0x3c03e4d1UL, 0x4b04d447UL, 0xd20d85fdUL, 0xa50ab56bUL,
      0x35b5a8faUL, 0x42b2986cUL, 0xdbbbc9d6UL, 0xacbcf940UL, 0x32d86ce3UL,
      0x45df5c75UL, 0xdcd60dcfUL, 0xabd13d59UL, 0x26d930acUL, 0x51de003aUL,
      0xc8d75180UL, 0xbfd06116UL, 0x21b4f4b5UL, 0x56b3c423UL, 0xcfba9599UL,
      0xb8bda50fUL, 0x2802b89eUL, 0x5f058808UL, 0xc60cd9b2UL, 0xb10be924UL,
      0x2f6f7c87UL, 0x58684c11UL, 0xc1611dabUL, 0xb6662d3dUL, 0x76dc4190UL,
      0x01db7106UL, 0x98d220bcUL, 0xefd5102aUL, 0x71b18589UL, 0x06b6b51fUL,
      0x9fbfe4a5UL, 0xe8b8d433UL, 0x7807c9a2UL, 0x0f00f934UL, 0x9609a88eUL,
      0xe10e9818UL, 0x7f6a0dbbUL, 0x086d3d2dUL, 0x91646c97UL, 0xe6635c01UL,
      0x6b6b51f4UL, 0x1c6c6162UL, 0x856530d8UL, 0xf262004eUL, 0x6c0695edUL,
      0x1b01a57bUL, 0x8208f4c1UL, 0xf50fc457UL, 0x65b0d9c6UL, 0x12b7e950UL,
      0x8bbeb8eaUL, 0xfcb9887cUL, 0x62dd1ddfUL, 0x15da2d49UL, 0x8cd37cf3UL,
      0xfbd44c65UL, 0x4db26158UL, 0x3ab551ceUL, 0xa3bc0074UL, 0xd4bb30e2UL,
      0x4adfa541UL, 0x3dd895d7UL, 0xa4d1c46dUL, 0xd3d6f4fbUL, 0x4369e96aUL,
      0x346ed9fcUL, 0xad678846UL, 0xda60b8d0UL, 0x44042d73UL, 0x33031de5UL,
      0xaa0a4c5fUL, 0xdd0d7cc9UL, 0x5005713cUL, 0x270241aaUL, 0xbe0b1010UL,
      0xc90c2086UL, 0x5768b525UL, 0x206f85b3UL, 0xb966d409UL, 0xce61e49fUL,
      0x5edef90eUL, 0x29d9c998UL, 0xb0d09822UL, 0xc7d7a8b4UL, 0x59b33d17UL,
      0x2eb40d81UL, 0xb7bd5c3bUL, 0xc0ba6cadUL, 0xedb88320UL, 0x9abfb3b6UL,
      0x03b6e20cUL, 0x74b1d29aUL, 0xead54739UL, 0x9dd277afUL, 0x04db2615UL,
      0x73dc1683UL, 0xe3630b12UL, 0x94643b84UL, 0x0d6d6a3eUL, 0x7a6a5aa8UL,
      0xe40ecf0bUL, 0x9309ff9dUL, 0x0a00ae27UL, 0x7d079eb1UL, 0xf00f9344UL,
      0x8708a3d2UL, 0x1e01f268UL, 0x6906c2feUL, 0xf762575dUL, 0x806567cbUL,
      0x196c3671UL, 0x6e6b06e7UL, 0xfed41b76UL, 0x89d32be0UL, 0x10da7a5aUL,
      0x67dd4accUL, 0xf9b9df6fUL, 0x8ebeeff9UL, 0x17b7be43UL, 0x60b08ed5UL,
      0xd6d6a3e8UL, 0xa1d1937eUL, 0x38d8c2c4UL, 0x4fdff252UL, 0xd1bb67f1UL,
      0xa6bc5767UL, 0x3fb506ddUL, 0x48b2364bUL, 0xd80d2bdaUL, 0xaf0a1b4cUL,
      0x36034af6UL, 0x41047a60UL, 0xdf60efc3UL, 0xa867df55UL, 0x316e8eefUL,
      0x4669be79UL, 0xcb61b38cUL, 0xbc66831aUL, 0x256fd2a0UL, 0x5268e236UL,
      0xcc0c7795UL, 0xbb0b4703UL, 0x220216b9UL, 0x5505262fUL, 0xc5ba3bbeUL,
      0xb2bd0b28UL, 0x2bb45a92UL, 0x5cb36a04UL, 0xc2d7ffa7UL, 0xb5d0cf31UL,
      0x2cd99e8bUL, 0x5bdeae1dUL, 0x9b64c2b0UL, 0xec63f226UL, 0x756aa39cUL,
      0x026d930aUL, 0x9c0906a9UL, 0xeb0e363fUL, 0x72076785UL, 0x05005713UL,
      0x95bf4a82UL, 0xe2b87a14UL, 0x7bb12baeUL, 0x0cb61b38UL, 0x92d28e9bUL,
      0xe5d5be0dUL, 0x7cdcefb7UL, 0x0bdbdf21UL, 0x86d3d2d4UL, 0xf1d4e242UL,
      0x68ddb3f8UL, 0x1fda836eUL, 0x81be16cdUL, 0xf6b9265bUL, 0x6fb077e1UL,
      0x18b74777UL, 0x88085ae6UL, 0xff0f6a70UL, 0x66063bcaUL, 0x11010b5cUL,
      0x8f659effUL, 0xf862ae69UL, 0x616bffd3UL, 0x166ccf45UL, 0xa00ae278UL,
      0xd70dd2eeUL, 0x4e048354UL, 0x3903b3c2UL, 0xa7672661UL, 0xd06016f7UL,
      0x4969474dUL, 0x3e6e77dbUL, 0xaed16a4aUL, 0xd9d65adcUL, 0x40df0b66UL,
      0x37d83bf0UL, 0xa9bcae53UL, 0xdebb9ec5UL, 0x47b2cf7fUL, 0x30b5ffe9UL,
      0xbdbdf21cUL, 0xcabac28aUL, 0x53b39330UL, 0x24b4a3a6UL, 0xbad03605UL,
      0xcdd70693UL, 0x54de5729UL, 0x23d967bfUL, 0xb3667a2eUL, 0xc4614ab8UL,
      0x5d681b02UL, 0x2a6f2b94UL, 0xb40bbe37UL, 0xc30c8ea1UL, 0x5a05df1bUL,
      0x2d02ef8dUL
   };

/* Return a 32-bit CRC of the contents of the buffer. */

SshUInt32 crc32_buffer(const unsigned char *s, size_t len)
{
  size_t i;
  SshUInt32 crc32val;

  crc32val = 0;
  for (i = 0;  i < len;  i ++)
    {
      crc32val =
        crc32_tab[(crc32val ^ s[i]) & 0xff] ^
          (crc32val >> 8);
    }
  return crc32val;
}

/* Return a 32-bit 'modified' CRC of the contents of the buffer. */

SshUInt32 crc32_buffer_altered(const unsigned char *s, size_t len)
{
  size_t i;
  SshUInt32 crc32val;

  crc32val = len;
  for (i = 0;  i < len;  i ++)
    {
      crc32val =
        crc32_tab[(crc32val ^ s[i]) & 0xff] ^
          (crc32val >> 8);
    }
  return crc32val;
}

/* Generates feedback terms table for crc32. Useful if table must be
   recreated later. */

void crc32_create_table(SshUInt32 *table)
{
  unsigned int i, j;
  SshUInt32 crc;

  for (i = 0; i < 256; i++)
    {
      crc = i;

      for (j = 0; j < 8; j++)
        crc = (crc >> 1) ^ ((crc & 0x1) ? 0xedb88320UL : 0);

      table[i] = crc;
    }
}

/* Following routines given are written by Mika Kojo for
   Applied Computing Research, Finland. All code below is

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                 All rights reserved.

   */

/* Our GF(2^n) modulus. */
#define MOD_32BIT 0xedb88320UL
#define MASK      0xffffffffUL

/* Polynomial arithmetics over GF(2^n).

   That is we are working under the given polynomial, and give
   routines to add, reduce and multiply within GF(2^n) etc. Code here
   is not exactly the fastest around, however, its polynomial time :)
   */

/* We'll give a bit abstraction here with simple polynomial of maximum
   size 32 bits (with some bits for extension due multiplication). */

typedef SshUInt32 GFPoly[2];

/* This is useful to know, however, we have inlined this always. */
void gf_add(GFPoly a, GFPoly b)
{
  a[0] ^= b[0];
  a[1] ^= b[1];
}

void gf_set(GFPoly a, GFPoly b)
{
  a[0] = b[0];
  a[1] = b[1];
}

void gf_set_ui(GFPoly a, SshUInt32 i)
{
  a[0] = i & MASK;
  a[1] = 0;
}

/* General division routine for polynomials. This is rather clumsy so
   for modular reduction use the gf_red instead. */
void gf_div(GFPoly q, GFPoly r, GFPoly a, GFPoly b)
{
  GFPoly t, h;
  unsigned int k, i;
  if (b[0] == 0 && b[1] == 0)
    ssh_fatal("gf_div: division by zero.");

  gf_set(t, a);
  gf_set(h, b);

  if (h[1])
    {
      for (k = 0; k < 32; k++)
        {
          if (h[1] & 0x1)
            break;
          h[1] = (((h[1] & MASK) >> 1) | (h[0] << 31)) & MASK;
          h[0] = (h[0] & MASK) >> 1;
        }
    }
  else
    {
      for (k = 0; k < 32; k++)
        {
          if (h[0] & 0x1)
            break;
          h[0] = (h[0] & MASK) >> 1;
        }
      h[1] = h[0];
      h[0] = 0;
      k += 32;
    }

  /* Shift the highest bit out. It is more implied than needed. */
  h[1] = (((h[1] & MASK) >> 1) | (h[0] << 31)) & MASK;
  h[0] = (h[0] & MASK) >>  1;
  k++;

  gf_set_ui(q, 0);

  for (i = 0; i < k; i++)
    {
      if (t[1] & 0x1)
        {
          t[1] = ((((t[1] & MASK) >> 1) | (t[0] << 31)) ^ h[1]) & MASK;
          t[0] = (((t[0] & MASK) >> 1) ^ h[0]);

          q[1] = (((q[1] & MASK) >> 1) | (q[0] << 31)) & MASK;
          q[0] = ((q[0] & MASK) >> 1) | ((SshUInt32)1 << 31);
        }
      else
        {
          t[1] = (((t[1] & MASK) >> 1) | (t[0] << 31)) & MASK;
          t[0] = (t[0] & MASK) >> 1;

          q[1] = (((q[1] & MASK) >> 1) | (q[0] << 31)) & MASK;
          q[0] = (q[0] & MASK) >> 1;
        }
    }

  /* Set the remainder, which is not as easy as it seems. */
  if (k >= 32)
    {
      r[0] = (t[1] << (k - 32)) & MASK;
      r[1] = 0;
    }
  else
    {
      r[0] = ((t[0] << k) | (t[1] << (31 - k))) & MASK;
      r[1] = (t[1] << k) & MASK;
    }
}

/* Reduce b (mod p) and output a. The p is the our irreducible (or not)
   polynomial in GF(2^n). If one changes the polynomial one should
   change this also. */
void gf_red(GFPoly a, GFPoly b)
{
  GFPoly c;
  int i;

  if (b[1] == 0)
    {
      c[0] = b[0];
      a[0] = c[0];
      a[1] = 0;
      return;
    }

  gf_set(c, b);

  for (i = 0; i < 32; i++)
    {
      if (c[1] & 0x1)
        {
          c[1] = ((((c[1] & MASK) >> 1) | (c[0] << 31)) ^ MOD_32BIT) & MASK;
          c[0] = (c[0] & MASK) >> 1;
        }
      else
        {
          c[1] = (((c[1] & MASK) >> 1) | (c[0] << 31)) & MASK;
          c[0] = (c[0] & MASK) >> 1;
        }
    }

  gf_set_ui(a, c[1]);
}

/* Multiplication of two elements in GF(2^n). That is a*b = out (mod p).
   Must be in reduced form. */
void gf_mul(GFPoly out, GFPoly a, GFPoly b)
{
  SshUInt32 c = b[0];
  GFPoly h, r;

  gf_set(h, a);

  gf_set_ui(r, 0);

  while (c)
    {
      if (c & ((SshUInt32)1 << 31))
        {
          r[0] ^= h[0];
          r[1] ^= h[1];
        }

      c = (c << 1) & MASK;
      h[1] = (((h[1] & MASK) >> 1) | (h[0] << 31)) & MASK;
      h[0] = (h[0] & MASK) >> 1;
    }
  gf_set(out, r);
}

/* Handy functions. One might like to write comparison function too
   but that is too much trouble? */
int gf_zero(GFPoly a)
{
  if (a[0] == 0 && a[1] == 0)
    return 1;
  return 0;
}

int gf_one(GFPoly a)
{
  if (a[0] == ((SshUInt32)1 << 31) && a[1] == 0)
    return 1;
  return 0;
}

/* Simple gcd algorithm for polynomials. Isn't used here, but implemented
   because it is so simple. */
void gf_gcd(GFPoly gcd, GFPoly a, GFPoly b)
{
  GFPoly h, g, r, q;

  gf_set(h, a);
  gf_set(g, b);

  while (!gf_zero(h))
    {
      gf_div(q, r, g, h);
      gf_set(g, h);
      gf_set(h, r);
    }
  gf_set(gcd, g);
}

/* Extended gcd computation for the inversion. We have removed some
   cases, which can be computed outside if neccessary. */
void gf_gcdext(GFPoly gcd, GFPoly sx, GFPoly gx, GFPoly hx)
{
  GFPoly s, q, r, g, h, s1, s2;

  if (gf_zero(hx))
    {
      gf_set(gcd, g);
      gf_set_ui(sx, 1);
      return;
    }

  gf_set(h, hx);
  gf_set(g, gx);

  gf_set_ui(s2, (((SshUInt32)1) << 31));
  gf_set_ui(s1, 0);
  while (!gf_zero(h))
    {
      gf_div(q, r, g, h);
      gf_mul(s, q, s1);
      gf_add(s, s2);

      gf_set(g, h);
      gf_set(h, r);

      gf_set(s2, s1);
      gf_set(s1, s);
    }

  gf_set(gcd, g);
  gf_set(sx, s2);
}

/* Yet we need an inversion algorithm for polynomials. */
int gf_inv(GFPoly inv, GFPoly a)
{
  GFPoly b, g;

  /* Our modulus polynomial. */
  b[0] = MOD_32BIT;
  b[1] = (((SshUInt32)1) << 31);

  gf_gcdext(g, inv, a, b);
  if (!gf_one(g))
    return 0;
  return 1;
}

/* Exponentiation modulo a irreducible (or not) polynomial. The exponent
   is best to kept as a standard integer. */
void gf_exp(GFPoly r, GFPoly g, size_t n)
{
  GFPoly t, h;

  gf_set(h, g);

  gf_set_ui(t, (((SshUInt32)1) << 31));

  while (n)
    {
      if (n & 0x1)
        {
          gf_mul(t, t, h);
          gf_red(t, t);
        }
      n >>= 1;
      gf_mul(h, h, h);
      gf_red(h, h);
    }
  gf_set(r, t);
}

/* Crc32 computations using the GF(2^n) arithmetic routines. Runs in
   polynomial time, which is quite nice when having to update long
   buffers. */

/* Compute the x^n (mod p) which is needed for crc scam. */
SshUInt32 crc32_blank(SshUInt32 mask_crc, size_t len)
{
  GFPoly t, g;

  gf_set_ui(t, mask_crc & MASK);
  gf_set_ui(g, (1 << (31 - 8)));

  gf_exp(g, g, len);

  gf_mul(t, t, g);
  gf_red(t, t);


  return t[0] & MASK;
}

/* Compute the x^(-n) (mod p) which is need for crc scam 2. */
SshUInt32 crc32_divide(SshUInt32 mask_crc, size_t len)
{
  GFPoly t, g;

  gf_set_ui(t, mask_crc & MASK);
  gf_set_ui(g, (1 << (31 - 8)));
  gf_exp(g, g, len);

  if (gf_inv(g, g) == 0)
    {
      ssh_fatal("crc32_divide: polynomial modulus not irreducible.");
    }

  gf_mul(t, t, g);
  gf_red(t, t);

  return t[0];
}

/* The main function which allows us to keep the crc32 updated even though
   we are not computing it entirely again. This works using GF(2^n)
   arithmetics and actually computes crc32 again only of the mask given.

   This works because given original message m and its crc m_crc we get

     m = m_crc (mod p)

   and yet if have a mask k for m that is m + k = n, where n is the new
   message then,

     m + k = n (mod p)

   and indeed

     k = m + n (mod p)

   and this is indeed equivalent to

     k_crc = m_crc + n_crc (mod p) <=>
     k_crc + m_crc = n_crc (mod p)

   Very trivial indeed. Then we notice that if there is a lot of
   zeros before actual changes we need to compute only from the start
   of the non-zero elements in the mask for k_crc. Also by noting that

     m*x^n = (m_crc)*((x^n)_crc) (mod p)

   we get using our GF(2^n) implementation polynomial time algorithm
   for getting rid of the possible trailing zeros. Thus we can
   update the CRC in polynomial time, after just computing crc32 out of
   the part of the mask k which is non-zero (or mostly non-zero and
   contiguous).

   Function input is the mask buffer of length mask_len. It's offset
   is the place to where to xor it one gets the new message. Total_len
   is the total lenght of the message computed with crc32. Prev_crc32
   is the previously computed crc32.

   Function returns the new crc32 that is indeed same as the one that
   is computed over the full message.

   */
SshUInt32 crc32_mask(const unsigned char *mask, size_t mask_len,
                     size_t offset,
                     size_t total_len,
                     SshUInt32 prev_crc32)
{
  return crc32_blank(crc32_buffer(mask, mask_len),
                     total_len - (offset + mask_len)) ^ prev_crc32;
}

/* Extend the crc32 computed (prev_crc32) to also number of zeroes
   after it. This could be useful with filesystems enlarging a file which
   one has computed crc32 before. Thus with a polynomial time algorithm
   one can compute new correct crc32. */
SshUInt32 crc32_extend(SshUInt32 prev_crc32, size_t len)
{
  return crc32_blank(prev_crc32, len);
}

/* Function to truncate crc32 of a buffer containing atleast len
   trailing zeroes. With this computation one can actually truncate
   the buffer in question by len bytes and keep the crc32 correct. */

SshUInt32 crc32_truncate(SshUInt32 prev_crc32,
                         size_t len)
{
  return crc32_divide(prev_crc32, len);
}
