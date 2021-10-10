/*
  sshcrypt.h

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                All rights reserved.

  Public interface to the SSH cryptographic library.  This file
  defines the functions and interfaces available to applications.
*/

#ifndef SSHCRYPT_H
#define SSHCRYPT_H

/*************** STATE OF THE CRYPTO LIBRARY ***********************/

/* Indicates the current status of the crypto library. */
typedef enum
{
  /* The library is uninitialized. No cryptograhic operations can be performed
     until the library is successfully initialized. */
  SSH_CRYPTO_LIBRARY_STATUS_UNINITIALIZED = 0,

  /* The library is in the normal functioning state. Only when the library
     is in this state can the user obtain output from cryptographic
     operations. */
  SSH_CRYPTO_LIBRARY_STATUS_OK = 1,

  /* The library is in a self test state. This state is automatically
     entered on startup, it may also be entered after the library enters
     an error state. If the self tests succeed the library then enters
     the SSH_CRYPTO_LIBRARY_STATUS_OK state, otherwise the library reverts
     to the SSH_CRYPTO_LIBRARY_STATUS_UNINITIALIZED state. */
  SSH_CRYPTO_LIBRARY_STATUS_SELF_TEST = 2,

  /* The library's error state, if the error is irrecoverable the library
     will uninitialize or shutdown, the status will then revert to
     SSH_CRYPTO_LIBRARY_STATUS_UNINITIALIZED. If the error is recoverable,
     the library will enter a SSH_CRYPTO_LIBRARY_STATUS_SELF_TEST state to
     determine whether normal functioning of the library can continue. */
  SSH_CRYPTO_LIBRARY_STATUS_ERROR = 3
} SshCryptoLibraryStatus;

/******************************************************************/


/* Status/error codes.

   Numeric values of these codes are fixed. You must not alter any
   assigned values. The holes in between the groups may be filled
   later. */

typedef enum
{
  /* Operation was successfully completed. */
  SSH_CRYPTO_OK = 0,



  /******* Error codes relating to the overall library state. ***********/

  /* The cryptographic library is in error state, the requested
     service cannot be provided due to that. Important note: Whenever
     this code is returned, then *all* cryptographic operations
     returning SshCryptoStatus will hereafter return this value (apart
     from uninitialize, which might return ok). The free functions for
     objects may be called and will work normally even if the library
     is in error state. */
  SSH_CRYPTO_LIBRARY_ERROR = 10,

  /* A cryptographic operation was attempted while the library was
     in the uninitialized state. */
  SSH_CRYPTO_LIBRARY_UNINITIALIZED = 11,

  /* A cryptographic operation was attempted while the library was
     in the initializing (self-test) state. */
  SSH_CRYPTO_LIBRARY_INITIALIZING = 12,

  /* Cannot change library certification mode, since there are crypto
     objects in existence referring to the current state */
  SSH_CRYPTO_LIBRARY_OBJECTS_EXIST = 13,



  /******* Error codes relating to unsupported operations.  *************/

  /* The algorithm/key is not supported. */
  SSH_CRYPTO_UNSUPPORTED = 30,

  /* Identifier given is not supported. */
  SSH_CRYPTO_UNSUPPORTED_IDENTIFIER = 31,

  /* Given scheme name was not recognized, i.e. not supported. */
  SSH_CRYPTO_SCHEME_UNKNOWN = 32,

  /* Group type given was not recognized. */
  SSH_CRYPTO_UNKNOWN_GROUP_TYPE = 33,

  /* Key type given was not recognized. */
  SSH_CRYPTO_UNKNOWN_KEY_TYPE = 34,



  /******* Error codes relating to invalid data. *************************/

  /* The supplied data is too short for this operation. */
  SSH_CRYPTO_DATA_TOO_SHORT = 50,

  /* Given data too long for this operation. */
  SSH_CRYPTO_DATA_TOO_LONG = 51,

  /* When encrypting/decrypting with a block cipher, the input block's
     length is not a multiple of the ciphers block length. */
  SSH_CRYPTO_BLOCK_SIZE_ERROR  = 53,



  /******* Error codes relating to invalid keys. *************************/

  /* Given key context was uninitialized. Please note, that library does
     not, nor cannot, always verify that key was initialized properly.
     However, to avoid any problems one should not give NULL keys to
     functions that clearly cannot handle them, i.e. functions that
     use information in keys should not be called with NULL keys. */
  SSH_CRYPTO_KEY_UNINITIALIZED = 70,

  /* Key blob contained information that could not be parsed, i.e. it
     probably is corrupted (or is of a newer/older version). */
  SSH_CRYPTO_CORRUPTED_KEY_FORMAT = 71,

  /* The supplied key is too short. */
  SSH_CRYPTO_KEY_TOO_SHORT = 72,

  /* The supplied key is too long. */
  SSH_CRYPTO_KEY_TOO_LONG = 73,

  /* The supplied key is invalid. */
  SSH_CRYPTO_KEY_INVALID = 74,

  /* The supplied key is a weak key. */
  SSH_CRYPTO_KEY_WEAK = 75,

  /* The supplied key size is invalid in some arbitrary way */
  SSH_CRYPTO_KEY_SIZE_INVALID = 76,

  /* Private key import failed because of invalid passphrase. */
  SSH_CRYPTO_INVALID_PASSPHRASE = 77,



  /******* Error codes relating to operation failure. *******************/

  /* Signature check failed in the verify_signature operation */
  SSH_CRYPTO_SIGNATURE_CHECK_FAILED = 90,

  /* Encryption/decryption failed (wrong key). */
  SSH_CRYPTO_OPERATION_FAILED  = 91,




  /******* Error codes related to insufficient resources ****************/

  /* Not enough memory to perform the requested operation. */
  SSH_CRYPTO_NO_MEMORY = 100,

  /* Provider was not registered as the internal slot table was exhausted.
     It can be enlargened using the *_MAX_SLOTS define. */
  SSH_CRYPTO_PROVIDER_SLOTS_EXHAUSTED = 101,



  /**** Error codes which are due to errors external to the crypto library */

  /* Crypto operation failed because the token that was needed to
     compute the result was not inserted. */
  SSH_CRYPTO_TOKEN_MISSING = 110,

  /* Crypto operation failed because the external key provider was
     unable to compute the result. */
  SSH_CRYPTO_PROVIDER_ERROR = 111,


  /**** Error codes from library initialization & self tests *************/

  /* Math library initialization failed. */
  SSH_CRYPTO_MATH_INIT = 150,

  /* Math library self tests failed. */
  SSH_CRYPTO_TEST_MATH = 151,

  /* Cipher self test failed. */
  SSH_CRYPTO_TEST_CIPHER = 152,

  /* Hash self test failed. */
  SSH_CRYPTO_TEST_HASH = 153,

  /* Mac self test failed. */
  SSH_CRYPTO_TEST_MAC = 154,

  /* RNG self test failed. */
  SSH_CRYPTO_TEST_RNG = 155,

  /* the checksum of library is incorrect, integrity has been compromised */
  SSH_CRYPTO_TEST_INTEG_INVALID = 156,

   /* can't find an integrity checksum from disk (Unix) or registry
      (Windows) */
  SSH_CRYPTO_TEST_INTEG_LOAD = 157,

  /* can't calculate HMAC SHA1 digest */
  SSH_CRYPTO_TEST_INTEG_DIGEST = 158,

  /* Public key self tests failed. */
  SSH_CRYPTO_TEST_PK = 159,



  /************** Miscellaneous error codes. ***************/

  /* The operation was cancelled by the user. */
  SSH_CRYPTO_OPERATION_CANCELLED = 200,

  /* Internal error, which should occur only during development. */
  SSH_CRYPTO_INTERNAL_ERROR = 201,

  /* Invalid handle supplied to API function */
  SSH_CRYPTO_HANDLE_INVALID = 202,

  /* Version-related operation attempted, but the given or supplied
     version is not supported (key export) */
  SSH_CRYPTO_UNSUPPORTED_VERSION = 203,

  /* No match. This error code is internal to the cryptographic
     library and is never seen by applications. */
  SSH_CRYPTO_NO_MATCH = 204


} SshCryptoStatus;

/* Converts the status message to a string. */
const char *
ssh_crypto_status_message(SshCryptoStatus status);


/* Initialize the crypto library. The library cannot be used before this
   function is called. This function may call the library's self tests. It
   returns SSH_CRYPTO_OK if the library was properly initialized. The status
   of the library is then set to SSH_CRYPTO_LIBRARY_STATUS_OK. If the self
   tests fail, an error code is returned and the status of the library is
   reset to SSH_CRYPTO_LIBRARY_STATUS_UNINITIALIZED. */
SshCryptoStatus ssh_crypto_library_initialize(void);

/* Calls the self tests. This is may be called by the
   ssh_crypto_library_initialize() function but an application can call this
   function at any time to verify the library is functioning
   correctly. Returns SSH_CRYPTO_OK if all tests succeed. If any test fails,
   library will enter error state and an error code is returned. */
SshCryptoStatus
ssh_crypto_library_self_tests(void);

/* Returns the current status of the crypto library. */
SshCryptoLibraryStatus
ssh_crypto_library_get_status(void);

/* Uninitialize the crypto library. Notice that this function returns
   either SSH_CRYPTO_OK or SSH_CRYPTO_LIBRARY_ERROR. The latter is
   returned if there exists any crypto objects not released. If error
   is returned, then the crypto library state is changed to error
   state. If OK is returned, then crypto library state is changed to
   UNINITIALIZED. */
SshCryptoStatus
ssh_crypto_library_uninitialize(void);

/* Returns a version string that describes the version of the crypto library
   in english. */
const char *
ssh_crypto_library_get_version(void);

/* Frees memory allocated by the crypto lib. Call this for dynamic
   strings and memblocks allocated by the lib, like for the result of
   function ssh_cipher_get_supported(). Do not call this to free
   crypto objects, they have respective *_free() functions (like
   ssh_private_key_free, etc).

   Care should be taken the cryptolibrary and the other libraries are
   compiled with the same compilation options to make sure the memory
   returned by the cryptolibrary and the other libraries is
   compatible, but it is a recommended practise to free the memory
   allocated by the crypto library using ssh_crypto_free.

   (Reasoning for the func: The FIPS shared cryptolib cannot link to
   other SSH libs. Hence there is no ssh_free() directly available.)  */
void
ssh_crypto_free(void *ptr);










/* Crypto library progress monitoring. For those operations that are time
   consuming such as the key generation (in particular prime search),
   one can get progress information by registering a progress function.

   The following operations are defined which are time consuming, and thus
   need progress monitoring. */

typedef enum
{
  SSH_CRYPTO_PRIME_SEARCH
} SshCryptoProgressID;

/* The progress monitor callback is called with an id of the operation
   type and a time index which is an increasing counter indicating that
   library is working on something. */

typedef void (*SshCryptoProgressMonitor)(SshCryptoProgressID id,
                                         unsigned int time_value,
                                         void *context);

/* To register, call this function with the progress monitor and a context
   structure which will be given to the function when called. To
   unregister the progress monitor this function should be called
   with NULL parameters. */

void
ssh_crypto_library_register_progress_func(SshCryptoProgressMonitor
                                          monitor_function,
                                          void *context);

/********************* Pseudo-Random numbers ******************************/

/* Initializes a random number generator.  The generator is used as follows:
     1. Add a sufficient amount of randomness (noise).
     2. Stir noise into the generator..
     3. Use the generator to obtain random numbers. It is recommended to
        periodically add more noise during normal use.

   Note that it is very important to add enough noise into the generator
   to get good quality random numbers.

   A commonly used technique is to collect a large amount of true randomness
   when the program is first started, and save a few hundred bits worth of
   randomness (obtained by calling ssh_random_get_byte repeatedly) in
   a file, and add that noise into the pool whenever the program is started
   again.  Note that it is also important to update the saved random seed
   every time it is used.

   Notice: The default RNG used is usually enough for most
   applications. However if you have need to either use a specific RNG
   use the RNG API defined in sshrandom.h instead. */

/* Mixes the bytes from the buffer into the pool.  The pool should be stirred
   after a sufficient amount of noise has been added. */
void
ssh_random_add_noise(const unsigned char *buf, size_t bytes);

/* Stirs the pool of randomness, making every bit of the internal state
   depend on every other bit.  This should be called after adding new
   randomness.  The stirring operation is irreversible, and a few bits of
   new randomness are automatically added before every stirring operation
   to make it even more difficult to reverse. */
void ssh_random_stir(void);

/* Returns a random byte. The application is highly recommended to add
   noise to the pool once a while. (e.g. using a periodic timeout
   which calls ssh_random_add_noise. */
unsigned int
ssh_random_get_byte(void);

#include "sshrandom.h"

#include "sshhash.h"

#include "sshcipher.h"

#include "sshmac.h"

#include "sshpk.h"

/********************* Certification status *******************************/

/* The sshcrypto library has been FIPS 140-2 certified. However, a specific
   distribution of sshcrypto may not include FIPS logic. To find out whether a
   sshcrypto is certified, see supported modes below or call
   ssh_crypto_library_get_version(). This function returns a string that
   reports "FIPS-certified" for a FIPS 140-2-certified version of sshcrypto
   library.

   The regular non-certified version of sshcrypto library does not support
   certification modes. The only valid mode is thus
   SSH_CRYPTO_CERTIFICATION_NONE. */


/* Implementation note: Keep the values of this enum in powers of two,
   since they are used internally in bitmasks. */
typedef enum {
  SSH_CRYPTO_CERTIFICATION_NONE = 0,
  SSH_CRYPTO_CERTIFICATION_FIPS_140_2 = 1
} SshCryptoCertificationMode;

/* Set crypto library certification mode. When the crypto library is
   initialized, it is in the "none", or regular, certification mode. In this
   mode basically "everything" works as-is. If a specific certification mode
   is set, then certain functionality of the crypto library can be restricted
   or otherwise modified to conform to the requirements of that specific
   certification.

   This function can be called after library initialization, not before.

   Changes to the library certification mode are allowed only if no
   crypto objects exist, if not then SSH_CRYPTO_OBJECTS_EXIST error
   code is returned. If mode could be changed successfully, then
   SSH_CRYPTO_OK is returned. */
SshCryptoStatus
ssh_crypto_set_certification_mode(SshCryptoCertificationMode mode);

/* Returns the current certification mode. If the crypto library is
   uninitialized, this routine will cause a fatal error. */
SshCryptoCertificationMode
ssh_crypto_get_certification_mode(void);

#endif /* SSHCRYPT_H */
