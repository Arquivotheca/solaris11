/*

  sshrandom.h

  Author: Santeri Paavolainen <santtu@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  In most cases, you should use the interace in sshcrypt.h. The
  previous API (ssh_random_add_noise, ssh_random_stir,
  ssh_random_get_byte and ssh_random_free) use this API on a lower
  level.

  Note: When speaking about RNG, in most cases the correct term would
  be PRNG for Pseudo Random number generators. However, because the
  implementation may also be a real RNG implementation using a
  hardware randomness, comments say RNG generally.

*/

#ifndef SSHRANDOM_H
#define SSHRANDOM_H

/******************* Random number generators **********************/

typedef struct SshRandomRec *SshRandom;

/* Return a comma-separated list of supported RNG names. The caller
   must free the returned value with ssh_crypto_free() */
char *
ssh_random_get_supported();

/* Return TRUE or FALSE dependiing whether the RNG called `name' is
   supported with this version of crypto library (and current fips
   mode). */
Boolean
ssh_random_supported(const char *name);

/* Returns TRUE or FALSE depending on whether the RNG is a FIPS approved
   RNG */
Boolean
ssh_random_is_fips_approved(const char *name);









/* Allocates and initializes a random number generator
   context. Notice: It is valid to pass NULL as `name': in that case
   some "default" RNG is allocated (however it is guaranteed it is
   FIPS compliant if FIPS mode is enabled). */
SshCryptoStatus
ssh_random_allocate(const char *name,
                    SshRandom *random_ret);

/* Frees a RNG. This can also be called when the library is in an
   error state. */
void
ssh_random_free(SshRandom random);

/* Returns name of the RNG. The name is equal to that used in
   ssh_random_allocate. The name points to an internal data structure and
   should NOT be freed, modified, or used after ssh_random_free is called. */
const char *
ssh_random_name(SshRandom random);

/* Fill a buffer with bytes from the RNG output. */
SshCryptoStatus
ssh_random_get_bytes(SshRandom random,
                     unsigned char *buffer, size_t bufferlen);

/* Add noise to the RNG */
SshCryptoStatus
ssh_random_add_entropy(SshRandom random,
                       const unsigned char *buf, size_t buflen);

/* Adds environmental noise to the given random number generator. Note
   that on embedded systems for example this might actually not do
   anything. If the NULL random is given, the default RNG (usable
   using sshcrypt.h API) will be targeter. */
void
ssh_random_add_light_noise(SshRandom random);

#endif /* SSHRANDOM_H */
