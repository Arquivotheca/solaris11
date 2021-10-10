/*

  crypto_tests.c

  Author: Santeri Paavolainen <santtu@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

*/

#include "sshincludes.h"
#include "sshcrypt.h"
#include "sshcrypt_i.h"
#include "sshrandom_i.h"
#include "sshpk_i.h"
#include "sshhash_i.h"
#include "sshmac_i.h"
#include "sshcipher_i.h"
#include "crypto_tests.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif /* HAVE_DLFCN_H */

#if defined(HAVE_DL_H) && defined(HAVE_SHL_GET)
#include <dl.h>
#endif /* HAVE_DL_H */

#if defined(HAVE_SYS_LDR_H) && defined(HAVE_LOADQUERY)
#include <sys/ldr.h>
#endif /* HAVE_SYS_LDR_H */

#define SSH_DEBUG_MODULE "SshCryptoTests"

/* The FIPS parameters for the RNG test */
#define NUM_BITS 20000
#define NUM_BYTES (NUM_BITS / 8 )
#define MIN_ONES 9725
#define MAX_ONES 10275
#define MIN_POKER 2.16
#define MAX_POKER 46.17
#define MAX_RUN 26

static int min_run[7] = { 0, 2315, 1114, 527, 240, 103, 103 };
static int max_run[7] = { 0, 2685, 1386, 723, 384, 209, 209 };

/* This table lists the number of ones (ones[i]) in the byte i. */
static unsigned char ones[] = {
  0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
  4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};

/* Size of the static random number tests in bytes. */
#define SSH_CRYPTO_STATIC_TEST_SIZE 128

/* Static data structure for the random number tests. */
typedef struct SshCryptoStaticTestVectorRec {
  const char *name;
  const unsigned char *test_data;
} *SshCryptoStaticTestVector, SshCryptoStaticTestVectorStruct;

/* Static data for the random number tests. Before random data is retrieved
   from the random number gererator the time used by the crypto library is set
   to 1041379200 (Jan  1 00:00:00 2003). */
static const SshCryptoStaticTestVectorStruct random_test_vectors[] = {
  { "ansi-x9.62",
    (const unsigned char *)
    "\x92\xb4\x04\xe5\x56\x58\x8c\xed"
    "\x6c\x1a\xcd\x4e\xbf\x05\x3f\x68"
    "\x09\xf7\x3a\x93\xc3\x23\x9b\xc2"
    "\x0f\x8b\xe8\xaa\xbc\xf1\x6e\x8d"
    "\x6b\xc9\x7a\xf4\x98\xc2\xd2\x4b"
    "\x47\x92\xb2\xcd\x96\x31\x7e\xc3"
    "\x5c\xbf\xa7\x2e\x82\xf7\x86\xab"
    "\xe1\x18\x84\xf5\x36\xc1\xe1\x4f"
    "\x3d\xd2\x43\xaa\x1a\xdd\x31\xe1"
    "\x6c\x6e\x2a\x13\x65\xca\xa4\x65"
    "\xd3\x04\xfd\x86\x3f\x9f\x81\xdd"
    "\x53\xbc\x6f\x7d\xad\x75\xa0\x6a"
    "\x21\x27\x53\x9f\xdf\xe5\x0d\xe5"
    "\x9c\x69\x2b\x9a\x77\x54\xfb\x34"
    "\x70\x06\x27\x57\x6a\x77\xdf\x3c"
    "\x5d\xd4\x49\xb8\x54\x0a\xc0\xa4"
  },
  { "ansi-dsa-key-gen",
    (const unsigned char *)
    "\x92\xb4\x04\xe5\x56\x58\x8c\xed"
    "\x6c\x1a\xcd\x4e\xbf\x05\x3f\x68"
    "\x09\xf7\x3a\x93\xc3\x23\x9b\xc2"
    "\x0f\x8b\xe8\xaa\xbc\xf1\x6e\x8d"
    "\x6b\xc9\x7a\xf4\x98\xc2\xd2\x4b"
    "\x47\x92\xb2\xcd\x96\x31\x7e\xc3"
    "\x5c\xbf\xa7\x2e\x82\xf7\x86\xab"
    "\xe1\x18\x84\xf5\x36\xc1\xe1\x4f"
    "\x3d\xd2\x43\xaa\x1a\xdd\x31\xe1"
    "\x6c\x6e\x2a\x13\x65\xca\xa4\x65"
    "\xd3\x04\xfd\x86\x3f\x9f\x81\xdd"
    "\x53\xbc\x6f\x7d\xad\x75\xa0\x6a"
    "\x21\x27\x53\x9f\xdf\xe5\x0d\xe5"
    "\x9c\x69\x2b\x9a\x77\x54\xfb\x34"
    "\x70\x06\x27\x57\x6a\x77\xdf\x3c"
    "\x5d\xd4\x49\xb8\x54\x0a\xc0\xa4"
  },
  { "ansi-dsa-sig-gen",
    (const unsigned char *)
    "\x92\xb4\x04\xe5\x56\x58\x8c\xed"
    "\x6c\x1a\xcd\x4e\xbf\x05\x3f\x68"
    "\x09\xf7\x3a\x93\xc3\x23\x9b\xc2"
    "\x0f\x8b\xe8\xaa\xbc\xf1\x6e\x8d"
    "\x6b\xc9\x7a\xf4\x98\xc2\xd2\x4b"
    "\x47\x92\xb2\xcd\x96\x31\x7e\xc3"
    "\x5c\xbf\xa7\x2e\x82\xf7\x86\xab"
    "\xe1\x18\x84\xf5\x36\xc1\xe1\x4f"
    "\x3d\xd2\x43\xaa\x1a\xdd\x31\xe1"
    "\x6c\x6e\x2a\x13\x65\xca\xa4\x65"
    "\xd3\x04\xfd\x86\x3f\x9f\x81\xdd"
    "\x53\xbc\x6f\x7d\xad\x75\xa0\x6a"
    "\x21\x27\x53\x9f\xdf\xe5\x0d\xe5"
    "\x9c\x69\x2b\x9a\x77\x54\xfb\x34"
    "\x70\x06\x27\x57\x6a\x77\xdf\x3c"
    "\x5d\xd4\x49\xb8\x54\x0a\xc0\xa4"
  },
  { "ansi-x9.17",
    (const unsigned char *)
    "\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x3e\x12\x2f\x80\x2e\x9a\x72\x9a"
    "\x33\x5f\x80\x05\x41\x59\x50\x10"
    "\xb2\x88\x32\x7a\x59\x0b\xca\x79"
    "\xf8\x29\x85\xb4\x3b\xff\xc9\x65"
    "\x7c\xc5\x75\xd3\xdc\xe4\x04\x2c"
    "\xe8\x6c\xba\x2b\x44\x01\xe7\x10"
    "\x45\xd5\x30\xa8\x05\xd4\x2c\x67"
    "\x07\x02\xfc\xae\x4e\x52\x1c\x89"
    "\xe4\xbe\xe4\x48\xce\x3c\xa6\xb3"
    "\xef\x7e\x5a\x22\xa3\x0b\x69\x10"
    "\x36\x7f\xd4\x37\x74\x3d\xf8\x0d"
    "\xe8\x37\x68\xd8\xf5\x81\x1a\xf8"
    "\x52\xe5\x70\x62\xac\xd3\x7f\x38"
    "\x39\xd2\xb3\x4e\x00\x3b\xe2\x9b"
    "\xcd\x4f\x8d\x05\xc1\x96\xf9\x4e"
  },
  { NULL, NULL }
};

Boolean ssh_crypto_test_rng(const char *name, SshRandomObject random)
{
  unsigned char bytes[(NUM_BITS + 7) / 8];
  unsigned char initial_values[SSH_CRYPTO_STATIC_TEST_SIZE];
  int bit, max, run, c, i;
  int poker[16], runs[2][7];
  double chi = 0.0;

  memset(bytes, 0, sizeof(bytes));

  /* Do the static random number tests. */
  for (i = 0; random_test_vectors[i].name != NULL; i++)
    {
      if (strcmp(random_test_vectors[i].name, name) == 0)
        break;
    }
  if (random_test_vectors[i].name == NULL)
    {
      SSH_DEBUG(0, ("Unknown random number generator = %s\n", name));
      return FALSE;
    }

  if (random_test_vectors[i].test_data != NULL)
    {
      ssh_crypto_set_time(1041379200L);
      if ((*random->ops->get_bytes)(random->context, initial_values,
                                    SSH_CRYPTO_STATIC_TEST_SIZE) !=
          SSH_CRYPTO_OK)
        {
          ssh_crypto_set_time(0L);
          return FALSE;
        }
      ssh_crypto_set_time(0L);

      if (memcmp(initial_values, random_test_vectors[i].test_data,
                 SSH_CRYPTO_STATIC_TEST_SIZE) != 0)
        {
          /* Didn't match, return error */
#if 0
          /* Put this code in to generate the static random number test tables
             defined above. */
          printf("  { \"%s\",\n", name);
          printf("    (const unsigned char *)\n");
          printf("   \"");
          for (i = 0; i < SSH_CRYPTO_STATIC_TEST_SIZE; i++)
            {
              printf("\\x%02x", initial_values[i]);
              if (i % 8 == 7)
                printf("\"\n");
              if (i % 8 == 7 && i < SSH_CRYPTO_STATIC_TEST_SIZE - 1)
                printf("    \"");
            }
          printf("  },\n");
#endif
          return FALSE;
        }
    }

  for (i = 0; i < 5; i++)
    if ((*random->ops->get_bytes)(random->context, bytes, sizeof(bytes))
        != SSH_CRYPTO_OK)
      return FALSE;

  /* The Monobit Test */
  c = 0;

  /* Count the number of ones in the sample */
  for (i = 0; i < sizeof(bytes); i++)
    c += ones[bytes[i]];

  /* The condition for an error */
  if (c <= MIN_ONES || c >= MAX_ONES)
    {
      SSH_DEBUG(0, ("Monobit test failed"));
      return FALSE;
    }

  /* The Poker Test */
  for (i = 0; i < 16; i++)
    poker[i] = 0;

  for (i = 0; i < sizeof(bytes); i++)
    {
      poker[bytes[i] & 0xf]++;
      poker[(bytes[i] >> 4) & 0xf]++;
    }

  for (i = 0; i < 16; i++)
    chi += (double) poker[i] * poker[i];

  chi = (16.0 * chi / 5000.0) - 5000.0;

  /* The condition for an error */
  if (chi <= MIN_POKER || chi >= MAX_POKER)
    {
      SSH_DEBUG(0, ("Poker test failed"));
      return FALSE;
    }

  /* The Run Test. */
  for (i = 0; i < 7; i++)
    runs[0][i] = runs[1][i] = 0;

  bit = bytes[0] & 1;
  max = 1;
  run = 0;

  /* Get the run frequency of the sample. */
  for (i = 0; i < sizeof(bytes); i++)
    {
      int k;

      c = bytes[i];

      for (k = 0; k < 8; k++)
        {
          if ((c & 1) == bit)
            run++;
          else
            {
              if (run > 6)
                runs[bit][6]++;
              else
                runs[bit][run]++;

              if (run > max)
                max = run;

              run = 1;
            }

          bit = c & 1;
          c >>= 1;
        }
    }

  /* Treat the last byte */
  if (run > 6)
    runs[bit][6]++;
  else
    runs[bit][run]++;

  if (run > max)
    max = run;

  for (run = 1; run < 7; run++)
    for (bit = 0; bit <= 1; bit++)
      if (runs[bit][run] < min_run[run])
        {
          SSH_DEBUG(0, ("Runs test failed for run length %d", run));
          return FALSE;
        }
      else
        if (runs[bit][run] > max_run[run])
          {
            SSH_DEBUG(0, ("Runs test failed for run length %d", run));
            return FALSE;
          }

  if (max > MAX_RUN)
    {
      SSH_DEBUG(0, ("Long runs test failed, maximum run length %d", max));
      return FALSE;
    }

  /* No errors encountered */
  return TRUE;
}

/* Private/public key encryption/decryption test */
SshCryptoStatus
ssh_crypto_test_pk_encrypt(SshPublicKeyObject public_key,
                           SshPrivateKeyObject priv_key)
{
  int i;
  Boolean differ;
  unsigned char *a, *b, *c;
  size_t a_len, b_len, c_len, len;
  SshCryptoStatus status = SSH_CRYPTO_OPERATION_FAILED;

  /* Find the maximum encryption input buffer length. */
  a_len = ssh_public_key_object_max_encrypt_input_len(public_key);

  /* The key is not an encryption key, this test does not apply */
  if (a_len == 0)
    return SSH_CRYPTO_OK;

  /* Find the maximum encryption ouptut buffer length */
  b_len = ssh_public_key_object_max_encrypt_output_len(public_key);

  if (a_len == -1)
    a_len = 128;

  /* Allocate buffers for the plain text and ciphertext */
  a = ssh_malloc(a_len);
  b = ssh_malloc(b_len);

  if (!a || !b)
    {
      SSH_DEBUG(2, ("Memory allocation failure"));
      ssh_free(a);
      ssh_free(b);
      return SSH_CRYPTO_NO_MEMORY;
    }

  /* Give some value to the plaintext buffer */
  for (i = 0; i < a_len; i++)
    {
      a[i] = i & 0xff;
    }

  /* Encrypt to get the ciphertext b */
  if ((status = ssh_public_key_object_encrypt(public_key, a, a_len, b, b_len,
                                              &len)) != SSH_CRYPTO_OK)
    {
      SSH_DEBUG(2, ("Encrypt operation failed: %s (%d)",
                    ssh_crypto_status_message(status), status));
      goto fail;
    }

  /* Verify the plaintext is different to the ciphertext, otherwise
     the test fails (FIPS specification) */
  differ = FALSE;
  for (i = 0; i < len; i++)
    {
      if (b[i] != a[i])
        {
          differ = TRUE;
          break;
        }
    }

  if (differ == FALSE && len == a_len)
    {
      SSH_DEBUG(2, ("Ciphertext is identical to plaintext"));
      goto fail;
    }

  /* Check output length consistency */
  if (len > b_len ||
      len > ssh_private_key_object_max_decrypt_input_len(priv_key))
    {
      SSH_DEBUG(2, ("Encryption output length is longer than excpected."));
      goto fail;
    }

  /* Allocate a buffer for the decrypted ciphertext */
  c_len = ssh_private_key_object_max_decrypt_output_len(priv_key);

  c = ssh_malloc(c_len);

  if (!c)
    {
      SSH_DEBUG(2, ("Memory allocation failure"));
      ssh_free(a);
      ssh_free(b);
      return SSH_CRYPTO_NO_MEMORY;
    }

  /* Decrypt the ciphertext we just encrypted */
  if ((status = ssh_private_key_object_decrypt(priv_key,
                                               b, b_len, c,
                                               c_len, &len)) != SSH_CRYPTO_OK)
    {
      SSH_DEBUG(2, ("Decrypt operation failed: %s (%d)",
                    ssh_crypto_status_message(status), status));

      ssh_free(c);
      goto fail;
    }

  /* Check output length consistency */
  if (len > c_len  || len != a_len)
    {
      SSH_DEBUG(2, ("Decryption output length is not what was excepted."));
      ssh_free(c);
      goto fail;
    }

   /* Check the decrypted ciphertext is identical to the original
     plaintext, if not the test fails */
  for (i = 0; i < len; i++)
    {
      if (c[i] != a[i])
        {
          SSH_DEBUG(2, ("Plaintext and decrypted values differ "
                        "(index %d, values %02x and %02x).",
                        i, a[i], c[i]));
          ssh_free(c);
          goto fail;
        }
    }

  /* Free the buffers */
  ssh_free(b);
  ssh_free(a);
  ssh_free(c);
  return SSH_CRYPTO_OK;

  /* Test failed */
 fail:
  ssh_free(a);
  ssh_free(b);

  /* Never return 'status' (it may be SSH_CRYPTO_OK), always return 
     an explicit error. Treat SSH_CRYPTO_NO_MEMORY as a special case, 
     the library does not need to go into an error state due to this 
     error. All other error status are treated as equivalent and can
     cause the library to enter an error state. */
  if (status == SSH_CRYPTO_NO_MEMORY)
    return SSH_CRYPTO_NO_MEMORY;
 
  return SSH_CRYPTO_OPERATION_FAILED;
}

/* Private/public key signature verification test */
SshCryptoStatus
ssh_crypto_test_pk_signature(SshPublicKeyObject public_key,
                             SshPrivateKeyObject priv_key)
{
  int i;
  unsigned char *a, *b;
  size_t a_len, b_len, len;
  SshCryptoStatus status = SSH_CRYPTO_OPERATION_FAILED;

  a_len = ssh_private_key_object_max_signature_input_len(priv_key);

  /* The key is not an signature key, this test does not apply. */
  if (a_len == 0)
    return SSH_CRYPTO_OK;

  if (a_len == -1)
    a_len = 128;

  /* Find the maximum signature ouptut buffer length */
  b_len = ssh_private_key_object_max_signature_output_len(priv_key);

  /* Allocate buffers for the input buffer and signature buffer */
  a = ssh_malloc(a_len);
  b = ssh_malloc(b_len);

  if (!a || !b)
    {
      SSH_DEBUG(2, ("Memory allocation failure"));
      ssh_free(a);
      ssh_free(b);
      return SSH_CRYPTO_NO_MEMORY;
    }

  /* Give some value to the input buffer */
  for (i = 0; i < a_len; i++)
    {
      a[i] = i & 0xf;
    }

  /* Sign the buffer 'a', the signature is 'b'  */
  if ((status = ssh_private_key_object_sign(priv_key, a, a_len,
                                            b, b_len, &len)) != SSH_CRYPTO_OK)
    {
      SSH_DEBUG(2, ("Sign operation failed: %s (%d)",
                    ssh_crypto_status_message(status), status));
      goto fail;
    }

  /* Check output length consistency */
  if (len > b_len)
    {
      SSH_DEBUG(2, ("Signature is longer than expected"));
      goto fail;
    }

  /* Verify the signature */
  if (ssh_public_key_object_verify_signature(public_key,
                                             b, len,
                                             a, a_len) == FALSE)
    {
      SSH_DEBUG(2, ("Signature verification failed"));
      goto fail;
    }

  ssh_free(a);
  ssh_free(b);

  /* Signature has verified correctly, test passed */
  return SSH_CRYPTO_OK;

 fail:
  ssh_free(a);
  ssh_free(b);

  /* Never return 'status' (it may be SSH_CRYPTO_OK), always return 
     an explicit error  */
  if (status == SSH_CRYPTO_NO_MEMORY)
    return SSH_CRYPTO_NO_MEMORY;
 
  return SSH_CRYPTO_OPERATION_FAILED;
}

SshCryptoStatus
ssh_crypto_test_pk_group(SshPkGroupObject pk_group)
{














































































































































































































  return  SSH_CRYPTO_OK;

}

/* Key pair consistency check. Runs encrypt and signature tests on the
   key, and returns FALSE if either failed, and TRUE if both
   succeeded. */
SshCryptoStatus
ssh_crypto_test_pk_consistency(SshPublicKeyObject public_key,
                               SshPrivateKeyObject priv_key)
{
  SshCryptoStatus status; 

  if ((status = ssh_crypto_test_pk_encrypt(public_key, priv_key)) 
      != SSH_CRYPTO_OK)
    {
      SSH_DEBUG(2, ("Failed encryption test"));
      return status;
    }

  if ((status = ssh_crypto_test_pk_signature(public_key, priv_key)) 
      != SSH_CRYPTO_OK)
    {
      SSH_DEBUG(2, ("Failed signature test"));
      return status;
    }

  return SSH_CRYPTO_OK;
}

/* Private key consistency test. This routine derives a public key
   from the private key, and runs encryption
   (ssh_crypto_test_pk_encrypt) and signature
   (ssh_crypto_test_pk_signature) tests on the private/public key
   pair. If either of the individual tests fail, or no public key can
   be derived, this test returns FALSE. Otherwise it returns TRUE. */
SshCryptoStatus
ssh_crypto_test_pk_private_consistency(SshPrivateKeyObject priv_key)
{
  SshCryptoStatus status;
  SshPublicKeyObject public_key;
  const char *sign, *encrypt;
  const char *temp_sign, *temp_encrypt;

  /* If no encryption or signature scheme is defined, test with a default
     scheme. */
  status = ssh_private_key_get_scheme_name(priv_key,
                                           SSH_PKF_SIGN, &sign);
  if (status != SSH_CRYPTO_OK)
    return status;

  status = ssh_private_key_get_scheme_name(priv_key,
                                           SSH_PKF_ENCRYPT, &encrypt);
  if (status != SSH_CRYPTO_OK)
    return status;

  if (sign == NULL)
    {
      temp_sign = ssh_private_key_find_default_scheme(priv_key,
                                                      SSH_PKF_SIGN);

      status = ssh_private_key_set_scheme(priv_key,
                                          SSH_PKF_SIGN, temp_sign);
      if (status != SSH_CRYPTO_OK)
        return status;
    }

  if (encrypt == NULL)
    {
      temp_encrypt = ssh_private_key_find_default_scheme(priv_key,
                                                         SSH_PKF_ENCRYPT);

      status = ssh_private_key_set_scheme(priv_key,
                                          SSH_PKF_ENCRYPT, temp_encrypt);

      if (status != SSH_CRYPTO_OK)
        return status;
    }

  /* Derive the public key. The internal derive function *does not* do
     consistency test */
  status = ssh_private_key_derive_public_key_internal(priv_key, &public_key);

  if (status == SSH_CRYPTO_UNSUPPORTED)
    return SSH_CRYPTO_OK;

  if (status != SSH_CRYPTO_OK)
    {
      SSH_DEBUG(2, ("Could not derive public key"));
      return status;
    }

  /* Test key consistency */
  status = ssh_crypto_test_pk_consistency(public_key, priv_key);

  if (status != SSH_CRYPTO_OK)
    {
      ssh_public_key_object_free(public_key);
      return status;
    }

  /* If default schemes have been chosen, reset the scheme to its original
     value. */
  if (encrypt == NULL)
    {
      status = ssh_private_key_set_scheme(priv_key,
                                          SSH_PKF_ENCRYPT, NULL);
      
      if (status != SSH_CRYPTO_OK)
	{
	  ssh_public_key_object_free(public_key);
	  return status;
	} 
    }      
  
  if (sign == NULL)
    {
      status = ssh_private_key_set_scheme(priv_key,
					  SSH_PKF_SIGN, sign);
      
      if (status != SSH_CRYPTO_OK)
	{
	  ssh_public_key_object_free(public_key);
	  return status;
	} 
    }
  
  ssh_public_key_object_free(public_key);
  return SSH_CRYPTO_OK;
}

/* A fixed DSA key of 512 bits */
static const char *dsa_p = "0x8e8b41418a966ce8e446c83e1e686ba727fe338ba848"
"32818ef1dd4360a29392501a519d532a2890042951cf5a039655e22aff789372a60631613"
"1043b5822d5";
static const char *dsa_q = "0xe45f10869d74d58961cf19a1ad3ad343ec90cdcf";
static const char *dsa_g = "0x26eac8732f977c6f60c9622f346cf4c3d47dff2d516b"
"b5c67b87f34ab1b1c26d1518af52e98813875c210c4eda3f27dff4f18779d2670b94ad0e2"
"5b840f4034c";
static const char *dsa_x = "0xa13866c07e2246820b7ae22706a754c7cab47c57";
static const char *dsa_y = "0x45511cc5f42600e8b43eab41caddb3bd140093646975"
"2db58688f636fbf5529bbd4b0f091d17c71907d68e8c36ae0bde1315a9e97517f92051dcf"
"46d14db8f2d";

/* A fixed RSA key of 512 bits */
static const char *rsa_p = "0xdfb38633ef8453b2093b29f687dc48ffd2ab3eb1e84b"
"8464b764c30478a74e6f";
static const char *rsa_q = "0x11049042e02b7085e8af4e243c99b0a570db504da3e7"
"f822d8289825198d40089";
static const char *rsa_d = "0x69a36bcbc3199ab5e90568961d969117f0ebe3451aff"
"e7a49fda9f87599606119f80cb90d19249448922aebf2ce2a767a77233f5de927f6ba0fca"
"24750e0c929";
static const char *rsa_e = "0x10001";
static const char *rsa_n = "0xedee8c6f76062a6db3323a906bba927702c1b517fd79"
"9db8dd1ad9b95a5f3c443344bdf3ffad182d829fd7cedb2bc794d2698ee18b9735b1c66ec"
"da26d74f967";
static const char *rsa_u = "0x2f6fe6745ac5bd59eb8b64e55696da8a5497c8ef0d16"
"c10ba3322369f6b63a7b";


/* This function performs the private key consistency test and the group
   consistency test on fixed DSA and RSA private keys and a fixed
   Diffie-Hellman group. This test is performed on power-up and returns
   TRUE on success, otherwise returns FALSE . */
Boolean ssh_pk_tests(void)
{
  SshMPIntegerStruct p, q, g, x, y, d, e, n, u;
  SshPrivateKeyObject key;
  SshPkGroupObject group;
  SshCryptoStatus status;

  ssh_mprz_init(&p);
  ssh_mprz_init(&q);
  ssh_mprz_init(&g);
  ssh_mprz_init(&x);
  ssh_mprz_init(&y);
  ssh_mprz_init(&d);
  ssh_mprz_init(&e);
  ssh_mprz_init(&n);
  ssh_mprz_init(&u);

  if (!ssh_mprz_set_str(&p, rsa_p, 16) ||
      !ssh_mprz_set_str(&q, rsa_q, 16) ||
      !ssh_mprz_set_str(&d, rsa_d, 16) ||
      !ssh_mprz_set_str(&e, rsa_e, 16) ||
      !ssh_mprz_set_str(&n, rsa_n, 16) ||
      !ssh_mprz_set_str(&u, rsa_u, 16))
    goto failed;

  /* Make a RSA private key, the signature and encryption consistency
     tests are performed during key generation. The tests pass iff. the
     return status is SSH_CRYPTO_OK. */
  status = ssh_private_key_object_define(&key, "if-modn",
                                         SSH_PKF_PRIME_P, &p,
                                         SSH_PKF_PRIME_Q, &q,
                                         SSH_PKF_SECRET_D, &d,
                                         SSH_PKF_PUBLIC_E, &e,
                                         SSH_PKF_MODULO_N, &n,
                                         SSH_PKF_INVERSE_U, &u,
                                         SSH_PKF_END);
  if (status != SSH_CRYPTO_OK)
    {
      SSH_DEBUG(3, ("RSA prv key generation return status is %d", status));
      goto failed;
    }

  /* Now perform the private key consistency test. */
  if (ssh_crypto_test_pk_private_consistency(key) != SSH_CRYPTO_OK)
    {
      ssh_private_key_object_free(key);
      goto failed;
    }

  ssh_private_key_object_free(key);

  if (!ssh_mprz_set_str(&p, dsa_p, 16) ||
      !ssh_mprz_set_str(&q, dsa_q, 16) ||
      !ssh_mprz_set_str(&g, dsa_g, 16) ||
      !ssh_mprz_set_str(&x, dsa_x, 16) ||
      !ssh_mprz_set_str(&y, dsa_y, 16))
    goto failed;

  /* Make a DSA private key, the signature and encryption consistency
   tests are performed during key generation. The tests pass iff. the
   return status is SSH_CRYPTO_OK. */
  status = ssh_private_key_object_define(&key, "dl-modp",
                                         SSH_PKF_PRIME_P, &p,
                                         SSH_PKF_PRIME_Q, &q,
                                         SSH_PKF_GENERATOR_G, &g,
                                         SSH_PKF_SECRET_X, &x,
                                         SSH_PKF_PUBLIC_Y, &y,
                                         SSH_PKF_END);
  if (status != SSH_CRYPTO_OK)
    {
      SSH_DEBUG(3, ("DSA prv key generation return status is %d", status));
      goto failed;
    }

  /* Now perform the private key consistency test. */
  if (ssh_crypto_test_pk_private_consistency(key) != SSH_CRYPTO_OK)
    {
      ssh_private_key_object_free(key);
      goto failed;
    }

  ssh_private_key_object_free(key);

  /* Generate a Diffie-Hellman group */
  status = ssh_pk_group_object_generate(&group, "dl-modp",
                                        SSH_PKF_PREDEFINED_GROUP,
                                        "ssh-dl-modp-group-1024bit-1",
                                        SSH_PKF_RANDOMIZER_ENTROPY, 160,
                                        SSH_PKF_END);

  if (status != SSH_CRYPTO_OK)
    {
      SSH_DEBUG(3, ("DH group generation return status is %d", status));
      ssh_pk_group_object_free(group);
      goto failed;
    }

  /* Perform the group consistency test. */
  if (ssh_crypto_test_pk_group(group) != SSH_CRYPTO_OK)
    {
      ssh_pk_group_object_free(group);
      goto failed;
    }

  ssh_pk_group_object_free(group);

  /* All tests passed, return OK */
  ssh_mprz_clear(&p);
  ssh_mprz_clear(&q);
  ssh_mprz_clear(&g);
  ssh_mprz_clear(&x);
  ssh_mprz_clear(&y);
  ssh_mprz_clear(&d);
  ssh_mprz_clear(&e);
  ssh_mprz_clear(&n);
  ssh_mprz_clear(&u);
  return TRUE;


 failed:
  ssh_mprz_clear(&p);
  ssh_mprz_clear(&q);
  ssh_mprz_clear(&g);
  ssh_mprz_clear(&x);
  ssh_mprz_clear(&y);
  ssh_mprz_clear(&d);
  ssh_mprz_clear(&e);
  ssh_mprz_clear(&n);
  ssh_mprz_clear(&u);
  return FALSE;
}




























































































































































































































































































































































































































































































































































































































































































































































































