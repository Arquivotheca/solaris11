/*

  sshpk.h

  Author: Santeri Paavolainen <santtu@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

*/

#ifndef SSHPK_H
#define SSHPK_H

#include "sshoperation.h"

/************************* Public key cryptography ************************/

/* Represents a public key type. */
typedef struct SshPkTypeRec SshPkType;

#ifdef SSHDIST_CRYPT_RSA
/* RSA key type which does not support key generation (all keys must be
   predefined). */
extern const SshPkType ssh_pk_if_modn;

/* RSA key type which supports key generation. */
extern const SshPkType ssh_pk_if_modn_generator;
#endif /* SSHDIST_CRYPT_RSA */

#ifdef SSHDIST_CRYPT_DSA
/* DSA key type over discrete logarithm modp groups which does not support
   key generation. Here the DSA algortihm is implemented strictly according
   to FIPS-186. */
extern const SshPkType ssh_pk_dl_modp;

/* DSA key type over discrete logarithm modp groups which supports key
   generation. Here the DSA algortihm is implemented strictly according
   to FIPS-186. */
extern const SshPkType ssh_pk_dl_modp_generator;

#if 0
/* The following two key types are no longer used. They correspond to DSA
   keys, but do not perform DSA key generation and DSA signature computations
   according to the standard specified in FIPS 186-2. */
extern const SshPkType ssh_pk_dl_modp_old;
extern const SshPkType ssh_pk_dl_modp_generator_old;
#endif

#endif /* SSHDIST_CRYPT_DSA */























/* Registers a key type to be used by the library. It is only necessary to
   call this function if the application needs to use elliptic curve key
   types. The other key types are automatically registered on library
   initialization.
*/
SshCryptoStatus ssh_pk_provider_register(const SshPkType *type);



/* SSH public key vararg list format identifiers. The basic idea is to
   allow the application using this interface to get hold on the actual
   private and public keys. Only some of the operations need to be
   known to use this interface.

   Not all combinations of identifiers are supported. Some care should
   be exercised when making combinations.

   Note: most input will be given in SshMPIntegers's because it is
       a reasonably convenient way of handling large strings of
       bits. However, not all values are integers, nor natural
       numbers. You should not assume that any SshMPInteger you get
       can be used for reasonable computations without knowledge of
       the meaning of the actual value.
   */

typedef enum
{
  /* Identifier to mark the end of the vararg list. As always, vararg
     list should be written with care. It could be a good idea to
     write simple wrappers around the most used ways of using this
     library (e.g. macros). */
  SSH_PKF_END = 0,

  /* Identifiers that are of most use. */

  /* Basic operations for generation of new key or group. */

  /* Size of the key in bits. This usually means the number of bits in the
     integer or polynomial modulus.

     When generating:          unsigned int

         Size in bits (depends on algorithm specific details).

     When reading information: unsigned int *

         As above.
     */
  SSH_PKF_SIZE = 1,

  /* The entropy in bits used in the generation of Diffie-Hellman
     exponents. This parameter tells how many bits of random data
     will be used for the generation of the Diffie-Hellman exponent
     (or randomizer).  The effect of using less than the group size
     is less security, for which you gain faster speed in performing
     Diffie-Hellman key agreement.

     Don't use this option unless:
       1. You _know_ that the reduction (yes, you can only reduce the
          entropy) is not making your product unsecure.
       2. You need the extra speed up badly.

     When generating:       unsigned int

        Entropy in bits. Usually it will be rounded up to nearest
        multiple of 8. This happens within this library.

     When reading information: unsigned int *

        The actual bit count used to generate the entropy will be
        outputed.
     */
  SSH_PKF_RANDOMIZER_ENTROPY = 2,

  /* The library has defined some predefined groups for some of the
     algorithms. Using them speeds up the group
     generation. However, randomly generated groups might make
     attackers job a little bit harder. However, note that randomly
     generated groups cannot be verified as rigorously as these
     predefined groups. Thus using predefined groups is usually safest.

     When generating:          const char *

        Name of the predefined group one wants to use. See
        ssh_public_key_get_predefined_groups() for more information.

     When reading information: const char **

        Returns a pointer to a constant string. This must not be freed later.
     */

  SSH_PKF_PREDEFINED_GROUP = 3,

  /* Advanced identifiers. */

  /* Key type of the public/private key or group. */

  /* When generating a key or group one must give the key or group type.
     One can also get the type out of the key with these identifiers.

     When reading information: const char **.

     Returns a pointer to a constant string so the caller must not free the
     returned string.

     Following key types are used:

       if-modn:

         All schemes based on integer factorization.

       dl-modp:

         All schemes based on discrete logarithm modulo p.

       ec-modp:

         Schemes based on elliptic curves over integers (mod p)
         discrete logarithm problem.

       ec-gf2n:

         Schemes based on elliptic curves over Galois field GF(2^n)
         discrete logarithm problem.

     */
  SSH_PKF_KEY_TYPE = 4,

  /* This flag is used when for creating and accesing proxy (external)
     keys.  When used for create operation with a call to
     ssh_private_key_generate, the argument is single "void *" whose
     contents are meaningful for the underlying key type. This can
     also be used for accessing the underlying key parameters for
     external keys with function ssh_private_key_get_info. In this
     case the argument is single "void **", where the provider fills
     in the pointer to the parameters.

     If SSH_PKF_PROXY is requested for software keys, the call
     will fail with error SSH_CRYPTO_UNSUPPORTED_IDENTIFIER. */
  SSH_PKF_PROXY = 5,

  /* Scheme types defined. */

  /* This library divides public key methods into scheme types. For example
     we have

        signature schemes
        encryption schemes
        key exchange schemes

     and actually the key exchange schemes are here divided into their
     basic algorithms.

     The following identifiers can be used to select some particular
     algorithm of the scheme type.

     When generating:           const char *

       The algorithm name. See ssh_public_key_get_supported() for more
       information.

     When reading information:  const char **

       Returns a pointer to a constant string. It must not be freed.

     */

  /* Signature scheme type */
  SSH_PKF_SIGN = 6,
  /* Encryption scheme type */
  SSH_PKF_ENCRYPT = 7,
  /* Diffie-Hellman key exchange scheme type. This includes also
     the Unified approach. */
  SSH_PKF_DH = 8,

  /* Specific operations for each key and group type. */

  /* This identifier denotes the explicit public key in numeric form. For
     different key types it might be given in different forms.

       dl-modp:

          generation:  SshMPInteger
          reading:     SshMPInteger

       ec-modp:

          generation:  SshMPInteger, SshMPInteger
          reading:     SshMPInteger, SshMPInteger

          The comma denotes that we mean a pair or values. E.g. the first
          one is the x co-ordinate and the second y co-ordinate. We
          assume that the point is valid.

       ec-gf2n:

          generation: SshMPInteger, SshMPInteger
          reading:    SshMPInteger, SshMPInteger

     */

  SSH_PKF_PUBLIC_Y = 20,

  /* This identifier denotes the explicit secret key in numeric form.

       dl-modp:

         generation:   SshMPInteger
         reading:      SshMPInteger

       ec-modp:

         generation:   SshMPInteger
         reading:      SshMPInteger

       ec-gf2n:

         generation:   SshMPInteger
         reading:      SshMPInteger

     */

  SSH_PKF_SECRET_X = 21,

  /* This identifier has a dual meaning. Although it always should be
     a prime number. However, it is defined for all supported key types.

     For integer factorization (mainly RSA) based systems this identifier
     means the other of the primes for the modulus.

       if-modn:

         generation:    SshMPInteger
         reading:       SshMPInteger

     For the discrete logarithm problem based methods this identifier
     means the integer field modulus.

       dl-modp:

         generation:    SshMPInteger
         reading:       SshMPInteger

       ec-modp:

         generation:    SshMPInteger
         reading:       SshMPInteger

       ec-gf2n:

         generation:    SshMPInteger
         reading:       SshMPInteger

     */

  SSH_PKF_PRIME_P = 22,

  /* This identifier is in princible equivalent to SSH_PKF_PRIME_P, but
     instead of being in integer domain we are working with polynomials
     with terms taken (mod 2). E.g. this is the irreducible polynomial
     for the Galois field GF(2^n).

       ec-gf2n:

         generation:   SshMPInteger
         reading:      SshMPInteger

    */

  SSH_PKF_IRREDUCIBLE_P = 23,

  /* This identifier has a dual meaning. Although it always should be
     a prime number.

     For integer factorization based systems this identifier means the
     other of the prime for the modulus (other is the SSH_PKF_PRIME_P).

       if-modn:

         generation:    SshMPInteger
         reading:       SshMPInteger

     For the discrete logarithm problem bases system this identifier
     means the order of the group in which computation occurs.

       dl-modp:

         generation:    SshMPInteger
         reading:       SshMPInteger

       ec-modp:

         generation:    SshMPInteger
         reading:       SshMPInteger

       ec-gf2n:

         generation:    SshMPInteger
         reading:       SshMPInteger

     */

  SSH_PKF_PRIME_Q = 24,

  /* The generator for discrete logarithm based methods.

       dl-modp:

         generation:   SshMPInteger
         reading:      SshMPInteger

       ec-modp:

         generation:   SshMPInteger , SshMPInteger
         reading:      SshMPInteger , SshMPInteger

       Here we denote by comma the fact that you are supposed to give
       two values, a pair, as input. The first component will be x, and
       the second y, value of the point returned or used.

       ec-gf2n:

         generation:   SshMPInteger , SshMPInteger
         reading:      SshMPInteger , SshMPInteger


     In general generator is a value which generates the set of numbers
     over which we perform our crypto operations.

     */

  SSH_PKF_GENERATOR_G = 25,


  /* The following three identifiers are only defined for integer
     factorization based methods.

       if-modn:

         generation:    SshMPInteger
         reading:       SshMPInteger

     Currently implemented is the RSA which used n = pq and e = d^1 (mod n).

     */

  SSH_PKF_MODULO_N = 26,


  /* Value for public exponent in e.g. RSA style methods.

       if-modn:

         generation:    SshMPInteger
         reading:       SshMPInteger

       In context of RSA this value will set the public exponent
       explicitly to some value or make sure that the value set is the
       next larger value possible. (However, if other parameters are
       explicitly given such as SSH_PKF_SECRET_D then it will be used
       to generate public exponent).

       */

  SSH_PKF_PUBLIC_E = 27,

  /* Value for secret exponent in e.g. RSA style methods.

       if-modn:

         generation:     SshMPInteger
         reading:        SshMPInteger

       In RSA this value has priority over public exponent. Given this
       value and primes p and q, all the RSA parameters can be
       deduced.

       */
  SSH_PKF_SECRET_D = 28,

  /* This value is used mainly in RSA.

       if-modn:

         generation:     SshMPInteger
         reading:        SshMPInteger

       This value can be excluded because it is automatically computed
       within the parameter making utility. Although, if given among all
       other parameters it will be used as is.

       */
  SSH_PKF_INVERSE_U = 29,

  /* These four definitions are only defined for elliptic curve methods.

       ec-modp:

         generation:       Boolean
         reading:          Boolean *

       ec-gf2n:

         generation:   Boolean
         reading:      Boolean *

     Point compression can be applied whenever a point is linearized to
     a octet buffer. However, it takes time to reconstruct and thus is
     not suggested. Default is no point compression.

      */

  SSH_PKF_POINT_COMPRESS = 30,

  /*   ec-modp:

         generation:       SshMPInteger
         reading:          SshMPInteger

       ec-gf2n:

         generation:   SshMPInteger
         reading:      SshMPInteger

       The number of points that lie of elliptic curve your are defining,
       or reading. This is not important for the working of the system,
       only that you can make sure that your parameters are secure.

         */

  SSH_PKF_CARDINALITY = 31,

  /*   ec-modp:

         generation:       SshMPInteger
         reading:          SshMPInteger

       ec-gf2n:

         generation:   SshMPInteger
         reading:      SshMPInteger

       Another number which is used to define the elliptic curve.

         */
  SSH_PKF_CURVE_A = 32,
  /*   ec-modp:

         generation:       SshMPInteger
         reading:          SshMPInteger

       ec-gf2n:

         generation:   SshMPInteger
         reading:      SshMPInteger

       The another number which is used to define the elliptic curve.

         */
  SSH_PKF_CURVE_B = 33,



  /* These keywords are used only for the ssh_pk_{import,export}
     routines. */

  /* Private key.

     Export: SshPrivateKey
     Import: SshPrivateKey* */
  SSH_PKF_PRIVATE_KEY = 150,

  /* Public key

     Export: SshPublicKey
     Import: SshPublicKey* */
  SSH_PKF_PUBLIC_KEY = 151,

  /* Group

     Export: SshPkGroup
     Import: SshPkGroup* */
  SSH_PKF_PK_GROUP = 152,

  /* Randomizers. These are always part of an existing PK Group, so
     the argument is a SshPkGroup. However, we are not
     exporting/importing the group itself, just the randomizers
     associated with it. Notice that the import paramenter must be an
     *existing* pk group.

     Export: SshPkGroup
     Import: SshPkGroup */
  SSH_PKF_PK_GROUP_RANDOMIZERS = 153,

  /* Cipher name to be used for export, or cipher name to be returned
     about the envelope during import. Note that during import, the
     value can be either NULL, "none" or a valid cipher name. The
     first are roughly the same (eg. the export format contents are
     not encrypted) -- however NULL means that the envelope format
     does not even support encryption, where "none" means that it is
     just not employed. On import the caller must free the returned
     value using ssh_crypto_free.

     Import: char**
     Export: const char* */
  SSH_PKF_CIPHER_NAME = 154,

  /* Cipher key. On export this is the encryption key, for import it
     is the decryption key.

     Import: const unsigned char *, size_t
     Export: const unsigned char *, size_t */
  SSH_PKF_CIPHER_KEY = 155,

  /* Cipher key length. This is valid only for import. This returns a
     zero value if the key length used for encryption cannot be
     extracted (or is not present), otherwise the key length used for
     encryption. Notice that this returns also zero is the cipher is
     "none" (see SSH_PKF_CIPHER_NAME).

     Import: size_t* */
  SSH_PKF_CIPHER_KEY_LEN = 156,

  /* Hash routine. For export, specifies the hash name, for import,
     gives the hash used. Notice that for import the returned value
     can also be NULL or "none" in addition to the actual hash
     algorithm name. If this is NULL, then the export envelope does
     not support hashing for integrity check, if "none" then this is
     supported by the envelope type, but just not used for this
     particular exported data. On import the caller must free the
     returned value using ssh_crypto_free.

     Import: const char *
     Export: char ** */
  SSH_PKF_HASH_NAME = 157,

  /* Returns the type of data contained in the exported data. This
     option is valid only for import operation -- it is not possible
     to explicitly specify the used export data type. Notice the
     returned value is one of SSH_PKF_PRIVATE_KEY, SSH_PKF_PUBLIC_KEY,
     SSH_PKF_PK_GROUP and SSH_PKF_PK_GROUP_RANDOMIZERS.

     Import: SshPkFormat* */
  SSH_PKF_ENVELOPE_CONTENTS = 158,

  /* Either set or get export envelope version number. Note that you
     cannot export all formats with a given version number. Also,
     version numbers are data type (prv/pub/grp) specific. If the
     version is not supported for export, returns
     SSH_CRYPTO_UNSUPPORTED_VERSION.

     Import: SshUInt32
     Export: SshUInt32* */
  SSH_PKF_ENVELOPE_VERSION = 159,

  /* Specify padding size. This is taken as a lower limit for the
     padding size used, as the export routine can use a larger padding
     if necessary (with block ciphers, for example).

     Export: size_t */
  SSH_PKF_PAD = 160

} SshPkFormat;

/* Export envelope versions. All pre-2003 export formats are version
   1, 2003 version is version 2, and ... well later are obviously
   something else. */
#define SSH_CRYPTO_ENVELOPE_VERSION_1   1
#define SSH_CRYPTO_ENVELOPE_VERSION_2   2

/* We want to fix the minimum entropy used in the generation of
   Diffie-Hellman exponents to some reasonably large number,
   however, we don't want it to be too large. 160 bits has been
   mentioned in some places to be a good start. You should probably use
   > 200 bits for more conservative applications. The more entropy you
   give, the more secure the system is, however, the public key
   parameters define the largest number of bits possible. */
#define SSH_RANDOMIZER_MINIMUM_ENTROPY 160

/* A data structure for representing a public key group in main memory. */
typedef struct SshPkGroupRec *SshPkGroup;

/* A data structure for representing a public key in main memory. */
typedef struct SshPublicKeyRec *SshPublicKey;

/* A data structure for describing a private key in memory. */
typedef struct SshPrivateKeyRec *SshPrivateKey;

/* Function to get a comma separated list of all supported predefined
   groups of this particular key type. The caller must free the
   returned string with ssh_crypto_free. */
char *
ssh_public_key_get_predefined_groups(const char *key_type);

/* Returns a tree-like list, if you like, of public key algorithms
   supported. Format is defined as

     key-type{scheme-type{algorithm,...},...},...

   for example

     if-modn{sign{rsa-pkcs1-md5},encrypt{rsa-pkcs1-none}}

   The caller must free the returned string with ssh_crypto_free.
   */

char *
ssh_public_key_get_supported(void);

/* Function to get explicitly the name of the public key. Returns the name
   in a mallocated string. This string should be freed after
   use by the application with a call to ssh_crypto_free.

   The name returned is the full name extended with scheme fields. To get
   just the key type use ssh_public_key_get_info.
   */
char *
ssh_public_key_name(SshPublicKey key);

/* Function to get explicitly the name of the private key. Returns the name
   in a mallocated string. This string should be freed after
   use by the application with a call to ssh_crypto_free.

   The name returned is the full name extended with scheme fields. To get
   just the key type use ssh_private_key_get_info.
   */
char *
ssh_private_key_name(SshPrivateKey key);

/* Define a public key from predefined parameters. This function has its
   most important meaning in reconstructing values from certificates,
   however in general this function can be used to convert other public
   keys into SSH internal format. It's usage is similar to
   ssh_private_key_generate() in that the vararg list uses the
   SshPkFormat idea. */

SshCryptoStatus
ssh_public_key_define(SshPublicKey *key,
                      const char *key_type, ...);

/* Copy public key from 'key_src' to 'key_dest'. Returns SSH_CRYPTO_OK,
   if everything went fine. This copying is explicit, operation on one
   doesn't affect the other. */

SshCryptoStatus
ssh_public_key_copy(SshPublicKey key_src,
                    SshPublicKey *key_dest);

/* Clears and frees the public key. Cannot fail. This function can be
   called even if the library is in an error state. */
void
ssh_public_key_free(SshPublicKey key);

/* Returns the maximum number of bytes that can be encrypted using this key.
   If this key is not capable of encryption (it is for signature verification
   only), this returns 0. */

size_t
ssh_public_key_max_encrypt_input_len(SshPublicKey key);

/* Returns the size of the buffer required for encrypting data with this key.
   If the key is not capable of encryption, this returns 0. */

size_t
ssh_public_key_max_encrypt_output_len(SshPublicKey key);



/* Encrypts data using the key.  The caller must allocate a large
   enough buffer to contain the encrypted result.  The data to be
   encrypted will be padded according to the current encoding and
   algorithm type.

   The ciphertext_buffer_len argument is only used to verify that the buffer
   really is large enough.

   The function returns SSH_CRYPTO_OK, if everything went fine. Then
   *ciphertext_len_return contains the number of bytes actually
   written to ciphertext_buffer. For some PKCSs, this will always be
   ssh_public_key_max_encrypt_output_len(key). If the return value is
   not SSH_CRYPTO_OK, ciphertext_buffer and *ciphertext_len_return
   might contain garbage. */

SshCryptoStatus
ssh_public_key_encrypt(SshPublicKey key,
                       const unsigned char *plaintext,
                       size_t plaintext_len,
                       unsigned char *ciphertext_buffer,
                       size_t ciphertext_buffer_len,
                       size_t *ciphertext_len_return);


/* Callback function which is called when the public key encryption is done.
   If the status is SSH_CRYPTO_OK then operation was successful and the
   ciphertext_buffer and ciphertext_buffer_len contains the ciphertext and its
   length. This callback function must copy the ciphertext away from the
   buffer, because it is freed immediately when this callback returns. */
typedef void (*SshPublicKeyEncryptCB)(SshCryptoStatus status,
                                      const unsigned char *ciphertext_buffer,
                                      size_t ciphertext_buffer_len,
                                      void *context);


/* Start asyncronous public key encryption operation. The library will
   call given the callback when the operation is done. The callback may
   be called immediately during this call in which case the returned
   SshOperationHandle will be NULL. The ssh_operation_abort
   function may be called to abort this operation before it finishes,
   in which case the callback is not called. 'key' and the 'plaintext'
   must remain constant during the operation, and can only be freed
   after the callback is called. */
SshOperationHandle
ssh_public_key_encrypt_async(SshPublicKey key,
                             const unsigned char *plaintext,
                             size_t plaintext_len,
                             SshPublicKeyEncryptCB callback,
                             void *context);


/* Verifies that the given signature matches with the given data.  The
   exact relationship of the data to the signature depends on the
   algorithm and encoding used for the key.  The data must be exactly
   the same bytes that were supplied when generating the signature.
   This returns true if the signature is a valid signature generated
   by this key for the given data.  Otherways the function returns
   false. False is returned too, if some error is encountered (for
   example, if the key is uncapable of decrypting the data because of
   too short a key). In essence, this means that the signature is
   not valid. */

Boolean
ssh_public_key_verify_signature(SshPublicKey key,
                                const unsigned char *signature,
                                size_t signature_len,
                                const unsigned char *data,
                                size_t data_len);


/* Callback function which is called when the public key verify
   operation is done.
   If the status is SSH_CRYPTO_OK then operation was successful,
   otherwise it indicates the reason why signature check
   failed.  */
typedef void (*SshPublicKeyVerifyCB)(SshCryptoStatus status,
                                     void *context);

/* Start asyncronous public key signature verify operation. The
   library will call the given callback when the operation is done.
   The callback may be called immediately during this call in which case the
   returned SshOperationHandle is NULL. The ssh_operation_abort function
   may be called to abort this operation before it finishes, in which case
   the callback is not called. 'key', 'signature', and 'data' must remain
   constant during the operation, and can only be freed after the callback
   is called. */
SshOperationHandle
ssh_public_key_verify_async(SshPublicKey key,
                            const unsigned char *signature,
                            size_t signature_len,
                            const unsigned char *data,
                            size_t data_len,
                            SshPublicKeyVerifyCB callback,
                            void *context);


/* As ssh_public_key_verify_signature but with this interface one can
   give the exact digest one self. Idea is that now the data can be
   gathered in pieces rather than in one big
   block. ssh_public_key_derive_signature_hash function is available
   for deriving the hash function. */
Boolean
ssh_public_key_verify_signature_with_digest(SshPublicKey key,
                                            const unsigned char *signature,
                                            size_t signature_len,
                                            const unsigned char *digest,
                                            size_t digest_len);


/* Start asyncronous public key signature verify operation. As
   ssh_public_key_verify_asycn but with this interface one can give
   the exact digest one self. The library will call given callback
   when operation is done. Callback may be called immediately during
   this call in which case the returned SshOperatinHandle is NULL. The
   function ssh_operation_abort function may be called to abort this
   operation before it finishes, in which case the callback is not
   called. Key, signature and the digest must remain constant during
   the operation, and can only be freed after the callback is
   called. */
SshOperationHandle
ssh_public_key_verify_digest_async(SshPublicKey key,
                                   const unsigned char *signature,
                                   size_t signature_len,
                                   const unsigned char *digest,
                                   size_t digest_len,
                                   SshPublicKeyVerifyCB callback,
                                   void *context);

/* The hash function used for signature verification computation. Note that
   this hash returned is compatible with the interface for generic
   hash functions. However, there is no need that this particular hash
   function is of some known type. Returns SSH_CRYPTO_OK on success,
   SSH_CRYPTO_UNSUPPORTED if no hash is associated with key, and some
   other value on errors/failures. */
SshCryptoStatus
ssh_public_key_derive_signature_hash(SshPublicKey key, SshHash *hash_ret);

/* A way to change scheme choice on an existing key. For one key type
   there can exists multiple algorithms (schemes) of same type (e.g. many
   signature algorithms exist for discrete logarithm based methods).
   By using the vararg list where SshPkFormat is used to identify the
   following elements we can select any possible scheme. Returns
   SSH_CRYPTO_OK if everything went fine. Care should be taken to
   see that the SSH_PKF_END tag is at the end of the vararg list. */

SshCryptoStatus
ssh_public_key_select_scheme(SshPublicKey key, ...);

/* Similar to the previous function. This function allows reading of
   exact details of the underlying public key. Using the vararg list
   and the define SshPkFormat type. */

SshCryptoStatus
ssh_public_key_get_info(SshPublicKey key, ...);

/* Private key interfaces. */

/* Copy private key 'key_src' to 'key_dest'. Explicit copying. */

SshCryptoStatus
ssh_private_key_copy(SshPrivateKey key_src,
                     SshPrivateKey *key_dest);

/* Clears and frees the private key from memory. This function can be
   called even if the library is in an error state. */
void
ssh_private_key_free(SshPrivateKey key);

/* Returns the public key corresponding to the private key.  If
   successful, this routine returns SSH_CRYPTO_OK. If the public key
   cannot be derived (e.g., if the private key derives on a smartcard,
   and no matching certificate is available on the card) then
   SSH_CRYPTO_UNSUPPORTED is returned. */
SshCryptoStatus
ssh_private_key_derive_public_key(SshPrivateKey key, SshPublicKey *public_ret);

/* This function allows same operation on a private key as
   ssh_public_key_select_scheme() on a public key. That is, it allows
   one to select an another method for doing private key operations without
   generating a new key. */

SshCryptoStatus
ssh_private_key_select_scheme(SshPrivateKey key, ...);

/* Get detailed information about the private key using SshPkFormat
   style vararg lists. */

SshCryptoStatus
ssh_private_key_get_info(SshPrivateKey key, ...);

/* Generate a public key cryptosystems private key. The basic usage is to
   generate a random key of some selected type. Other uses is to give
   explicit key values to be used through SSH interface.

   Vararg list must be handled with care. Returns SSH_CRYPTO_OK if
   everything went fine. */

SshCryptoStatus
ssh_private_key_generate(SshPrivateKey *key,
                         const char *key_type, ...);


/* Generate a public key cryptosystems private key from predefined
   explicit values. This interface should be preferred over
   ssh_private_key_generate, when keys used have been generated
   externally. */
SshCryptoStatus
ssh_private_key_define(SshPrivateKey *key,
                       const char *key_type, ...);

/* Returns the maximum number of bytes that can be signed using this key.
   If this key is not capable of signing, this returns 0. It returns
   (size_t)-1 if signature scheme does its own hashing (i.e.
   any length of input can be given). */

size_t
ssh_private_key_max_signature_input_len(SshPrivateKey key);

/* Returns the maximum number size of a signature generated by this key
   (in bytes).  If this key is not capable of signing, this returns 0. */

size_t
ssh_private_key_max_signature_output_len(SshPrivateKey key);

/* Returns the maximum number of bytes that can be decrypted using this key.
   If this key is not capable of decryption, this returns 0. */

size_t
ssh_private_key_max_decrypt_input_len(SshPrivateKey key);

/* Returns the size of the output buffer required for decrypting data with
   this key. If the key is not capable of decryption, this returns 0. */

size_t
ssh_private_key_max_decrypt_output_len(SshPrivateKey key);

/* Returns TRUE or FALSE depending whether the public key is operating
   in a FIPS approved mode of operation (DSA is approved). */
Boolean
ssh_public_key_is_fips_approved(const char *key_type);

/* Returns TRUE or FALSE depending whether the private key is operating
   in a FIPS approved mode of operation (DSA is approved). */
Boolean
ssh_private_key_is_fips_approved(const char *key_type);


/* Decrypts data encrypted with the corresponding public key.  The caller
   must allocate a large enough buffer to contain the decrypted result.
   This will strip any algorithm/encoding-specific padding from the
   encrypted data.

   The plaintext_buffer_len argument is only used to verify that the
   buffer really is large enough.

   The function returns SSH_CRYPTO_OK, if everything went fine. Then
   *plaintext_length_return will contain the number of actual
   plaintext in the beginning of *plaintext_buffer. Otherways
   plaintext_buffer and *plaintext_length_return might contain
   garbage. */

SshCryptoStatus
ssh_private_key_decrypt(SshPrivateKey key,
                        const unsigned char *ciphertext,
                        size_t ciphertext_len,
                        unsigned char *plaintext_buffer,
                        size_t plaintext_buffer_len,
                        size_t *plaintext_length_return);


/* Callback function which is called when the private key decryption is done.
   If the status is SSH_CRYPTO_OK then operation was successful and the
   plaintext_buffer and plaintext_buffer_len contains the plaintext and its
   length. This callback function must copy the plaintext away from the buffer,
   because it is freed immediately when this callback returns. */
typedef void (*SshPrivateKeyDecryptCB)(SshCryptoStatus status,
                                       const unsigned char *plaintext_buffer,
                                       size_t plaintext_buffer_len,
                                       void *context);

/* Start asyncronous private key decryption operation. The library
   will call given callback when operation is done. Callback may be
   called immediately during this call in which case the returned
   SshOperatinHandle is NULL. The ssh_operation_abort
   function may be called to abort this operation before it finishes,
   in which case the callback is not called. Key and the ciphertext
   must remain constant during the operation, and can only be freed
   after the callback is called. */
SshOperationHandle
ssh_private_key_decrypt_async(SshPrivateKey key,
                              const unsigned char *ciphertext,
                              size_t ciphertext_len,
                              SshPrivateKeyDecryptCB callback,
                              void *context);


/* Signs the given data using the private key.  The data will be padded
   and encoded depending on the type of the key before signing.  To verify
   the signature, the same data must be supplied to the verification function
   (along with the corresponding public key).

   The signature_buffer_len argument is only used to verify that the
   buffer really is large enough.

   Most supported methods do their own hashing and formatting.

   The function returns SSH_CRYPTO_OK, if everything went fine. Then
   *signature_length_return will contain the length of the actual
   signature, which resides in the beginning of
   signature_buffer. Otherways signature_buffer and
   *signature_length_return might contain garbage. */

SshCryptoStatus
ssh_private_key_sign(SshPrivateKey key,
                     const unsigned char *data,
                     size_t data_len,
                     unsigned char *signature_buffer,
                     size_t signature_buffer_len,
                     size_t *signature_length_return);


/* Callback function which is called when the private key signing is done. If
   the status is SSH_CRYPTO_OK then operation was successful and the
   signature_buffer and signature_buffer_len contains the signature and its
   length. This callback function must copy the signature away from the buffer,
   because it is freed immediately when this callback returns. */
typedef void (*SshPrivateKeySignCB)(SshCryptoStatus status,
                                    const unsigned char *signature_buffer,
                                    size_t signature_buffer_len,
                                    void *context);

/* Start asyncronous private key signing operation. The library will
   call given callback when operation is done. Callback may be called
   immediately during this call in which case the returned
   SshOperatinHandle is NULL. The ssh_operation_abort
   function may be called to abort this operation before it finishes,
   in which case the callback is not called. Key and the data must
   remain constant during the operation, and can only be freed after
   the callback is called. */
SshOperationHandle
ssh_private_key_sign_async(SshPrivateKey key,
                           const unsigned char *data,
                           size_t data_len,
                           SshPrivateKeySignCB callback,
                           void *context);


/* As ssh_private_key_sign but here one can give the hash digest directly. The
   hash which to use can be requested using
   ssh_private_key_derive_signature_hash function. */

SshCryptoStatus
ssh_private_key_sign_digest(SshPrivateKey key,
                            const unsigned char *digest,
                            size_t digest_len,
                            unsigned char *signature_buffer,
                            size_t signature_buffer_len,
                            size_t *signature_length_return);


/* Start asyncronous private key signing operation. As
   ssh_private_key_sign_async, but here one can give the hash digest
   directly.  The library will call given callback when operation is
   done. Callback may be called immediately during this call in which
   case the returned SshOperatinHandle is NULL. The
   ssh_operation_abort function may be called to abort this operation
   before it finishes, in which case the callback is not called.  Key
   and the digest must remain constant during the operation, and can
   only be freed after the callback is called. */
SshOperationHandle
ssh_private_key_sign_digest_async(SshPrivateKey key,
                                  const unsigned char *digest,
                                  size_t digest_len,
                                  SshPrivateKeySignCB callback,
                                  void *context);

/* With this interface we can derive a hash function to gather the
   signature data for signing. Hash function context returned is
   compatible with the generic hash interface of this library. Returns
   SSH_CRYPTO_OK if successful, SSH_CRYPTO_UNSUPPORTED if no such
   hash is available and some other value in error/failure cases. */
SshCryptoStatus
ssh_private_key_derive_signature_hash(SshPrivateKey key, SshHash *hash_ret);

/* Public key group. */

/* Function to generate public key group. Using the SshPkFormat vararg
   construction. */
SshCryptoStatus
ssh_pk_group_generate(SshPkGroup *group,
                      const char *group_type, ...);

/* Copy the group. */
SshCryptoStatus
ssh_pk_group_copy(SshPkGroup group_src, SshPkGroup *group_dest);

/* Free public key group context.  This function can be
   called even if the library is in error state.*/
void
ssh_pk_group_free(SshPkGroup group);

/* Select a scheme for public key group. Basically only possibilitity
   seems to be Diffie-Hellman. */
SshCryptoStatus
ssh_pk_group_select_scheme(SshPkGroup group, ...);

/* Get the group parameters out of the public key group. Equivalent to
   what was explained with private and public key variants. */
SshCryptoStatus
ssh_pk_group_get_info(SshPkGroup group, ...);

/* Precomputation. */

/* In many situations application program wants to be able to do many
   operations quickly with one key. Usually there is a method to
   use a bit more time beforehand to do less when e.g. making signatures.
   The idea of precomputation is the build suitable data structures, and
   precomputed data, that gives sometimes even significant speed-up
   for multiple operations with the given key.

   This is effective at least with DSA style keys where precomputation
   yields often 2 to 4 times speed-up.

   These functions should be used only when it is guaranteed that multiple
   operations are done (or there is a lot of idle time). The precomputation
   takes about same time as one or two usual operations. */

SshCryptoStatus
ssh_private_key_precompute(SshPrivateKey key);

SshCryptoStatus
ssh_public_key_precompute(SshPublicKey key);

SshCryptoStatus
ssh_pk_group_precompute(SshPkGroup group);


/* Randomizers. */

/* Count the number of randomizers available through this public
   key group. */
unsigned int
ssh_pk_group_count_randomizers(SshPkGroup group);

/* Generate randomizer, a value which can be used in speeding up some
   specific algorithms. Not all methods support randomizers, however.
   Using randomizers can give significant speed-ups for secure
   communications protocols. */
SshCryptoStatus
ssh_pk_group_generate_randomizer(SshPkGroup group);

/* Diffie-Hellman interface. */

/* Number of octets needed for the setup function. Returns 0 if
   Diffie-Hellman is not supported. */
size_t
ssh_pk_group_dh_setup_max_output_length(SshPkGroup group);


/* Diffie-Hellman secret data */
typedef struct SshPkGroupDHSecretRec *SshPkGroupDHSecret;


/* Free Diffie-Hellman secret data structure (in case we abort
   Diffie-Hellman protocol).  This function can be called even if the
   library is in error state. */
void
ssh_pk_group_dh_secret_free(SshPkGroupDHSecret secret);


/* Number of octets needed for the agree function. Returns 0 if
   Diffie-Hellman is not supported. */
size_t
ssh_pk_group_dh_agree_max_output_length(SshPkGroup group);


/* Generate an exchange value with the Diffie-Hellman protocol. The
   generated public exchange value is placed to the user-supplied
   exchange_buffer. Also a secret is generated which should be hold in
   secret until the agree operation. The secret can be freed with
   ssh_pk_group_secret_free() if the agree operation cannot or shall
   not be called.

   Diffie-Hellman can be visualized as a communication between entities
   A and B, as follows:

     A                            B
   setup                         setup
   send exchange --------->
                <----------    send exchange
   agree                          agree


   Be aware that Diffie-Hellman is suspectible to man-in-the-middle
   attacks. To gain authenticated connection you should also use digital
   signatures.
   */
SshCryptoStatus
ssh_pk_group_dh_setup(SshPkGroup group,
                      SshPkGroupDHSecret *secret,
                      unsigned char *exchange_buffer,
                      size_t exchange_buffer_length,
                      size_t *return_length);

/* Compute a secret value from an exchange value and a secret. Secret will
   be freed and deleted (thus no need to free it otherways). This function
   destroys the secret even when an error occurs. */

SshCryptoStatus
ssh_pk_group_dh_agree(SshPkGroup group,
                      SshPkGroupDHSecret secret,
                      const unsigned char *exchange_buffer,
                      size_t exchange_buffer_length,
                      unsigned char *secret_value_buffer,
                      size_t secret_value_buffer_length,
                      size_t *return_length);


/* Callback function which is called when the Diffie-Hellman setup is
   done. If the status is SSH_CRYPTO_OK then operation was successful
   and the exchange_buffer and exchange_buffer_len contains the
   exchange data and its length, which should be sent to the other
   end. The secret contains the Diffie-Hellman secret part, and it
   must be given to the corresponding ssh_pk_group_dh_agree_async
   function, or freed using ssh_pk_group_dh_secret_free.  This
   callback function must copy the exchange data away from the buffer,
   because it is freed immediately when this callback returns. */
typedef void (*SshPkGroupDHSetup)(SshCryptoStatus status,
                                  SshPkGroupDHSecret secret,
                                  const unsigned char *exchange_buffer,
                                  size_t exchange_buffer_len,
                                  void *context);

/* Start asyncronous Diffie-Hellman setup operation. The library will
   call given callback when operation is done. Callback may be called
   immediately during this call in which case the returned
   SshOperatinHandle is NULL. The ssh_operation_abort
   function may be called to abort this operation before it finishes,
   in which case the callback is not called. The group must remain
   constant during the operation and can only be freed after the
   callback is called. */
SshOperationHandle
ssh_pk_group_dh_setup_async(SshPkGroup group,
                            SshPkGroupDHSetup callback,
                            void *context);

/* Callback function which is called when the Diffie-Hellman agree is done. If
   the status is SSH_CRYPTO_OK then operation was successful and the
   shared_secret_buffer and shared_secret_buffer_len contains the shared secret
   value and its length. This callback function must copy the shared secret
   data away from the buffer, because it is freed immediately when this
   callback returns. */
typedef void (*SshPkGroupDHAgree)(SshCryptoStatus status,
                                  const unsigned char *shared_secret_buffer,
                                  size_t shared_secret_buffer_len,
                                  void *context);

/* Start asyncronous Diffie-Hellman agree operation. The library will
   call given callback when operation is done. Callback may be called
   immediately during this call in which case the returned
   SshOperatinHandle is NULL. The ssh_operation_abort
   function may be called to abort this operation before it finishes,
   in which case the callback is not called.  Group, secret and the
   exchange buffer must remain constant during the operation, and can
   only be freed after the callback is called. */
SshOperationHandle
ssh_pk_group_dh_agree_async(SshPkGroup group,
                            SshPkGroupDHSecret secret,
                            const unsigned char *exchange_buffer,
                            size_t exchange_buffer_len,
                            SshPkGroupDHAgree callback,
                            void *context);

/* Perform prv/pub/grp/randomizer import operation on a buffer `buf'
   of length `buflen'. Returns SSH_CRYPTO_OK if the data requested was
   successfully imported, otherwise returns some error code. On
   successful operations the `*envelope_len_ret' will contain number
   of bytes in the envelope starting at `buf'. Note that the
   `envelope_len_ret' value is guaranteed to be non-zero only if you
   are actually importing a key/group -- for envelope query operations
   it can be zero or non-zero (if it is non-zero, it is however the
   correct value).

   The variable length argument list contains list of import-specific
   SSH_PKF_* keywords terminated with SSH_PKF_END. For example the
   following will import a public key (and also return information
   about the public key encoding version):

   status = ssh_pk_import(buf, buflen, &used,
                SSH_PKF_PUBLIC_KEY, &pub,
                SSH_PKF_ENVELOPE_VERSION, &vers,
                SSH_PKF_END);

   This function works also as a query API by leaving out the
   type-specific flags:

   status = ssh_pk_import(buf, buflen, NULL,
                SSH_PKF_ENVELOPE_VERSION, &vers,
                SSH_PKF_ENVELOPE_CONTENTS, &type,
                SSH_PKF_CIPHER_NAME, &cipher,
                SSH_PKF_CIPHER_KEY_LEN, &cipher_key_len,
                SSH_PKF_HASH_NAME, &hash,
                SSH_PKF_END);

   printf("Envelope version %d, type %d. Cipher %s, hash %s.\n",
        vers, type, cipher ? "not supported" : cipher,
        hash ? "not supported" : hash);

   Note: If you specify SSH_PKF_PRIVATE_KEY, SSH_PKF_PUBLIC_KEY,
   SSH_PKF_PK_GROUP and SSH_PKF_PK_GROUP_RANDOMIZERS, then the call
   can fail for quite a lot of reasons (such as decryption failure,
   invalid type etc.). If none of those are present, you are really
   just querying what is inside the buffer -- the call can fail if the
   contents are not recognized, but that is the only reason why it can
   fail. If the envelope does not contain some information (such as no
   cipher name), then that value is NULL, but the call itself will
   succeed. */
SshCryptoStatus
ssh_pk_import(const unsigned char *buf, size_t buflen,
              size_t *envelope_len_ret, ...);

/* Perform prv/pub/grp export operation. The result is written to
   `*buf_ret' and points to an allocate memory which must be freed by
   the caller. The variable length argument list is a list of
   export-specific SSH_PKF_* keywords terminated with a
   SSH_PKF_END. For example, to export a private key with encryption:

   status = ssh_pk_export(&buf, &buflen,
        SSH_PKF_PRIVATE_KEY, &prv,
        SSH_PKF_CIPHER_NAME, "aes-cbc",
        SSH_PKF_CIPHER_KEY, key_buf, key_len,
        SSH_PKF_END); */
SshCryptoStatus
ssh_pk_export(unsigned char **buf_ret, size_t *len_ret, ...);

#endif /* SSHPK_H */
