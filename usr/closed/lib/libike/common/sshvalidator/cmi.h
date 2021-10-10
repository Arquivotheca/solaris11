/*
  cmi.h

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Implementation of the SSH Certificate Validator (former Certificate
  Manager, thus still called SSH CMi).
*/

#ifndef CMI_H
#define CMI_H

#include "sshmp.h"
#include "sshcrypt.h"
#include "x509.h"
#include "sshpkcs7.h"

/* Certificate manager API return codes. */

typedef enum
{
  /* Operation was performed correctly. This will be returned if there
     has not been any errors which should indicate the library or
     application failure. */
  SSH_CM_STATUS_OK,

  /* The given item already exists in the local database and was not
     inserted. There cannot exist duplicate data. */
  SSH_CM_STATUS_ALREADY_EXISTS,

  /* The item searched for was not found from the local database, nor
     from the external databases available at the time. This doesn't
     neccessarily mean that the data is not available in some external
     database configured to the system. It may occur that after some
     time has passed the item will be found. This cannot guaranteed
     however.

     Also there is a configurable time period which will keep the CM
     from trying to seek for the item again until that period is up. */
  SSH_CM_STATUS_NOT_FOUND,

  /* Input invalid. The data submitted to the library has been found
     to be incomplete or cannot be parsed. */
  SSH_CM_STATUS_INVALID_DATA,

  /* Indicates that the searching is still in process for the
     initiated operation. Application can handle this as
     SSH_CM_STATUS_OK. */
  SSH_CM_STATUS_SEARCHING,

  /* Could not delinearize data for the local DB. */
  SSH_CM_STATUS_COULD_NOT_DELINEARIZE,

  /* Decoding failed for the given input. */
  SSH_CM_STATUS_DECODE_FAILED,

  /* The local database is not writable. This should never occur for
     the memory cache, but can occur for the disk based system (once
     available). */
  SSH_CM_STATUS_LOCAL_DB_NWRITE,

  /* Database signaled an error while operation was processed. */
  SSH_CM_STATUS_DB_ERROR,

  /* The validity time was not long enough. */
  SSH_CM_STATUS_VALIDITY_TIME_TOO_SHORT,

  /* The certificate was not found valid. That is, the authentication
     chains tried were not complete. */
  SSH_CM_STATUS_NOT_VALID,

  /* The certificate cannot be a valid certificate. */
  SSH_CM_STATUS_CANNOT_BE_VALID,

  /* Operation signaled expired and no sufficient result was found. */
  SSH_CM_STATUS_OPERATION_EXPIRED,

  /* Allocation failed. */
  SSH_CM_STATUS_COULD_NOT_ALLOCATE,

  /* Not available. */
  SSH_CM_STATUS_NOT_AVAILABLE,

  /* The certificate class number is too large. */
  SSH_CM_STATUS_CLASS_TOO_LARGE,

  /* The certificate's class number was not changed by the operation. */
  SSH_CM_STATUS_CLASS_UNCHANGED,

  /* Generic failure of operation. */
  SSH_CM_STATUS_FAILURE

} SshCMStatus;

/* These flags are used to determine the internal state of the
   current search. */

typedef unsigned int SshCMSearchState;
/* Nothing special happened. */
#define SSH_CM_SSTATE_VOID                     0x00000000
/* Algorithm mismatch between the certificate and the search constraints. */
#define SSH_CM_SSTATE_CERT_ALG_MISMATCH        0x00000001
/* Key usage mismatch between the certificate and the search constraints. */
#define SSH_CM_SSTATE_CERT_KEY_USAGE_MISMATCH  0x00000002
/* Certificate was not valid in the time interval. */
#define SSH_CM_SSTATE_CERT_NOT_IN_INTERVAL     0x00000004
/* Certificate is not valid. */
#define SSH_CM_SSTATE_CERT_INVALID             0x00000010
/* Certificate signature was not verified correctly. */
#define SSH_CM_SSTATE_CERT_INVALID_SIGNATURE   0x00000020
/* Certificate was revoked by a CRL. */
#define SSH_CM_SSTATE_CERT_REVOKED             0x00000040
/* Certificate was not added to the cache. */
#define SSH_CM_SSTATE_CERT_NOT_ADDED           0x00000080
/* Certificate decoding failed. */
#define SSH_CM_SSTATE_CERT_DECODE_FAILED       0x00000100
/* Certificate was not found (anywhere). */
#define SSH_CM_SSTATE_CERT_NOT_FOUND           0x00000200
/* Certificate chain looped (did not find trusted root). */
#define SSH_CM_SSTATE_CERT_CHAIN_LOOP          0x00000400
/* Certificate contains critical extension that was not handled. */
#define SSH_CM_SSTATE_CERT_CRITICAL_EXT        0x00000800
/* Certificate issuer was not valid (CA specific information missing). */
#define SSH_CM_SSTATE_CERT_CA_INVALID          0x00001000
/* CRL is too old. */
#define SSH_CM_SSTATE_CRL_OLD                  0x00004000
/* CRL is not valid. */
#define SSH_CM_SSTATE_CRL_INVALID              0x00008000
/* CRL signature was not verified correctly. */
#define SSH_CM_SSTATE_CRL_INVALID_SIGNATURE    0x00010000
/* CRL was not found (anywhere). */
#define SSH_CM_SSTATE_CRL_NOT_FOUND            0x00020000
/* CRL was not added to the cache. */
#define SSH_CM_SSTATE_CRL_NOT_ADDED            0x00040000
/* CRL decoding failed. */
#define SSH_CM_SSTATE_CRL_DECODE_FAILED        0x00080000
/* CRL is not currently valid, but in the future. */
#define SSH_CM_SSTATE_CRL_IN_FUTURE            0x00100000
/* CRL contains duplicate serial numbers. */
#define SSH_CM_SSTATE_CRL_DUPLICATE_SERIAL_NO  0x00200000
/* Time interval is not continuous. */
#define SSH_CM_SSTATE_INTERVAL_NOT_VALID       0x00400000
/* Time information not available. */
#define SSH_CM_SSTATE_TIMES_UNAVAILABLE        0x00800000
/* Database method failed due to timeout. */
#define SSH_CM_SSTATE_DB_METHOD_TIMEOUT        0x01000000
/* Database method failed. */
#define SSH_CM_SSTATE_DB_METHOD_FAILED         0x02000000
/* Path was not verified. */
#define SSH_CM_SSTATE_PATH_NOT_VERIFIED        0x04000000
/* Maximum path length reached. */
#define SSH_CM_SSTATE_PATH_LENGTH_REACHED      0x08000000
/* Policy failed. */
#define SSH_CM_SSTATE_INVALID_POLICY           0x10000000
/* Naming failed. */
#define SSH_CM_SSTATE_INVALID_NAME             0x20000000

/* The status context. */
typedef struct SshCMSearchInfoRec
{
  /* Status of the search. If return value is SSH_CM_STATUS_OK then
     the search state can be ignored. However, the state may contain
     interesting information for the application to monitor. */
  SshCMStatus status;

  /* The state of the full search, including all tries. May not tell
     much, but then again can tell everything. In particular, this may
     be useful when needing of simple things to say to the user, for
     example, "CRL was not found". The application doesn't currently
     know what is the real reason for the failure of the search, but
     may guess. Usually something big like "CRL in future" may be good
     enough of a guess. */
  SshCMSearchState state;

} *SshCMSearchInfo;

/* The values for controlling the set of allowed databases for the
   manager. */
typedef enum
{
  SSH_CM_CONFIG_ALLOWED_LOCAL_DB,
  SSH_CM_CONFIG_DENIED_LOCAL_DB,
  SSH_CM_CONFIG_ALLOWED_LDAP,
  SSH_CM_CONFIG_DENIED_LDAP
} SshCMConfigAllow;

/* Subject or issuer name selection from certificates. */
typedef enum
{
  SSH_CM_KEY_CLASS_SUBJECT = 0,
  SSH_CM_KEY_CLASS_ISSUER  = 128
} SshCMKeyClass;

/* Officiel search rules. Its unlikely that other rules are needed. */
typedef enum
{
  SSH_CM_SEARCH_RULE_AND,
  SSH_CM_SEARCH_RULE_OR
} SshCMSearchRule;

/* Key types for searching. */

typedef enum
{
  /* Identification number at cache, not given to EDB's. */
  SSH_CM_KEY_TYPE_IDNUMBER         = 0,
  /* Hash of the BER encoding (using the default hash algorithm). */
  SSH_CM_KEY_TYPE_BER_HASH         = 1,
  /* Directory name. */
  SSH_CM_KEY_TYPE_DIRNAME          = 2,
  /* Distinguished name. */
  SSH_CM_KEY_TYPE_DISNAME          = 3,
  /* IP address. */
  SSH_CM_KEY_TYPE_IP               = 4,
  /* DNS name. */
  SSH_CM_KEY_TYPE_DNS              = 5,
  /* URI (unique resource identifier). */
  SSH_CM_KEY_TYPE_URI              = 6,
  /* X.400 name. */
  SSH_CM_KEY_TYPE_X400             = 7,
  /* Serial number. */
  SSH_CM_KEY_TYPE_SERIAL_NO        = 8,
  /* Unique identifier. */
  SSH_CM_KEY_TYPE_UNIQUE_ID        = 9,
  /* RFC822 name (email address). */
  SSH_CM_KEY_TYPE_RFC822           = 10,
  /* Other name (given as BER encoded blob, see PKIX) */
  SSH_CM_KEY_TYPE_OTHER            = 11,
  /* RID (registered identifier). */
  SSH_CM_KEY_TYPE_RID              = 12,
  /* Public key identifier (hashed public key). */
  SSH_CM_KEY_TYPE_PUBLIC_KEY_ID    = 13,
  /* Hash of the serial number and issuer name. */
  SSH_CM_KEY_TYPE_SI_HASH          = 14,
  /* PKIX style public key identifier as described in RFC 2459. */
  SSH_CM_KEY_TYPE_X509_KEY_IDENTIFIER = 15,

  /* The last type, not used. */
  SSH_CM_KEY_TYPE_NUM
} SshCMKeyType;

/* Data types for searching. */

typedef enum
{
  /* User certificate. */
  SSH_CM_DATA_TYPE_CERTIFICATE    = 0,
  /* End user CRL. */
  SSH_CM_DATA_TYPE_CRL            = 1,
  /* CA certificate. */
  SSH_CM_DATA_TYPE_CA_CERTIFICATE = 2,
  /* Authority CRL. */
  SSH_CM_DATA_TYPE_AUTH_CRL       = 3,

  /* Last type not used. */
  SSH_CM_DATA_TYPE_NUM
} SshCMDataType;


#ifdef SSHDIST_VALIDATOR_OCSP
/* Enumeration that defines how OCSP and CRLs are used in certificate
   verification. */
typedef enum
{
  /* OCSP is not used at all. Checking is done using CRLs. */
  SSH_CM_OCSP_NO_OCSP = 0,
  /* OCSP checking is used. If OCSP operation fails, CRLs are not used. */
  SSH_CM_OCSP_OCSP_ONLY,
  /* OCSP checking is used. If OCSP operation fails, CRLs are used. */
  SSH_CM_OCSP_CRL_AFTER_OCSP,
  /* For internal use only. */
  SSH_CM_OCSP_NO_END_ENTITY_OCSP
} SshCMOcspMode;

/* Flags for the OCSP responder. */
typedef unsigned int SshCMOcspResponderFlags;
/* No flags on. */
#define SSH_CM_OCSP_RESPONDER_VOID             0x00000000
/* If this flag is on, producedAt time of the response tells the
   time when the certificate is known to be valid. Otherwise thisUpdate
   time is used. */
#define SSH_CM_OCSP_RESPONDER_USE_PRODUCED_AT  0x00000001
/* If this flag is on, we do not accept responses with no nonce. Nonces
   are always sent with the requests regardless of this flag. */
#define SSH_CM_OCSP_RESPONDER_REQUIRE_NONCE    0x00000002

#endif /* SSHDIST_VALIDATOR_OCSP */

/* The hash algorithm we are using if such is needed. This algorithm
   should be supported by our crypto library. You can of course choose
   any you like best, but it should collision resistant. */
#define SSH_CM_HASH_ALGORITHM "sha1"

/* Definition of all types supported. */

/* Lists */

typedef struct SshCertDBEntryListRec *SshCMCertList;
typedef struct SshCertDBEntryListRec *SshCMCrlList;

/* The search constraints. Used in determining the certificate
   wanted to look for from the databases. */
typedef struct SshCMSearchConstraintsRec *SshCMSearchConstraints;

/* The CM certificate context. */
typedef struct SshCMCertificateRec *SshCMCertificate;

/* The CRL context. */
typedef struct SshCMCrlRec *SshCMCrl;

/* Configuration structure. */
typedef struct SshCMConfigRec *SshCMConfig;

/* The main CMi context. */
typedef struct SshCMContextRec *SshCMContext;

/* External search index handle. */
typedef int SshCMSearchIndexHandle;

/* Application callback. This is used only when searching for
   certificates. */


/* This callback is called when a certificate is revoked. The idea is
   to allow the application to keep track of certificates which have
   been revoked, without the need to search them all the time.
   Further the application can figure out easily when its own
   certificates get invalidated, etc.

   The SshCMCertificate given should not be modified nor freed, only
   viewed and looked at. After the callback returns the certificate is
   no longer valid.

   NOTE: In future this function may be extended in
   behaviour. However, this is only in our drawing board. */

typedef SshCMStatus
(*SshCMRevocationNotifyCallback)(void            *context,
                                 SshCMCertificate cert);

/* General nofication functions.

   Notification   Description

   *_NEW          A new object has been introduced to CM.

   *_FREE         The local cache has decided to free this object (and
                  it will no longer be available though CM). This
                  event cannot cause the object to stay in the cache,
                  although it can copy the contents and add the
                  object again to the cache.

   *_REVOKED      The certificate has been just revoked. Notice that
                  this may be noticed only if the certificate is
                  traversed during a validation operation.

   The explicit values for the notifications are not important, except,
   that they may be used later to build masks of them. This may of
   use for some event combinations in later versions.

   All events can be ignored, they are only informational.
 */

typedef unsigned int SshCMNotifyEventType;
#define SSH_CM_EVENT_CERT_NEW      0x00000001
#define SSH_CM_EVENT_CERT_FREE     0x00000002
#define SSH_CM_EVENT_CERT_REVOKED  0x00000004
#define SSH_CM_EVENT_CRL_NEW       0x00000100
#define SSH_CM_EVENT_CRL_FREE      0x00000200

/* Notification for certificate events. The application shall need to
   create handling functions for both certificates and CRLs, or place
   NULL markers instead to the events structure. */

typedef void
(*SshCMNotifyCertificateEvent)(void                *caller_context,
                               SshCMNotifyEventType event_type,
                               SshCMCertificate     cert);

typedef void
(*SshCMNotifyCrlEvent)(void                *caller_context,
                       SshCMNotifyEventType event_type,
                       SshCMCrl             crl);


/* This structure is passed as an argument to the notification
   registration. It contains currently only two notification
   functions, one for certificate and one for CRL. They can be
   substituted with NULL if no notification of that type is
   needed. Default operation does not have any notifications. */

typedef struct SshCMNotifyEventsRec
{
  SshCMNotifyCertificateEvent certificate;
  SshCMNotifyCrlEvent         crl;
} *SshCMNotifyEvents, SshCMNotifyEventsStruct;


/* Destructor for the private information in certificates. */
typedef void (*SshCMPrivateDataDestructor)(SshCMCertificate cm_cert,
                                           void *context);

/* Function definitions. */

/***********************************************************************
 * CONFIGURATION
 */

/* Allocate CM configuration. Every time application opens up CM it
   needs to configure it. Opening CM will steal the configuration,
   thus the application does not usually need to free it. */
SshCMConfig ssh_cm_config_allocate(void);

/* Free configuration in case the application needs to quit without
   opening up the CM. The CM will free the configuration automatically
   when closed. */
void ssh_cm_config_free(SshCMConfig config);

/* CM defaults to "ssh_time()" as a default wall clock retrieval
   function.  However, applications are allowed to set another
   one. Each subsequent call to time function is expected to return
   value greater or equal to previous call to same function. */
typedef SshTime (*SshCMTimeFunc)(void *caller_context);

#ifdef SUNWIPSEC
/***** Error code handling */

/* Convert CM status code to string */
const char *cm_status_to_string(SshCMStatus code);

#endif

/***** Configuration. */

/* Time function set up.
   The default value is function 'ssh_time()' */
void
ssh_cm_config_set_time_function(SshCMConfig config,
                                SshCMTimeFunc func,
                                void *caller_context);

/* Set the default time lock used by local database.

   The CM quarantees the entries given to the local database are
   available during this period of time (starting from the addition to
   the database). However, after given time the entry can be removed
   (due memory limitations etc.). This applies to both certificates and
   CRL's.

   Default value is 5 seconds, and maximum value is 120 seconds. */
void
ssh_cm_config_set_default_time_lock(SshCMConfig config,
                                    unsigned int seconds);

/* Set the maximum number of intermediate CA certificates in a
   certificate path.

   Default value is 15. */
void
ssh_cm_config_set_max_path_length(SshCMConfig config,
                                  unsigned int max_path_length);

/* Set the maximum number of delay allowed for EDB operation to
   consume.  After the given number of milliseconds the operation (for
   example, CRL search) will timeout. This does not mean that the
   validation process would necessarily fail in this many
   milliseconds, but that the process must look some other way to
   proceed in the validation.

   Default value is 10000 milliseconds. */
void
ssh_cm_config_set_max_operation_delay(SshCMConfig config, unsigned int msecs);

/* Set the maximum number of restarts in one search. We cannot fetch
   more than this many new certificates or crls from the directories
   with one search.

   Default value is 4 (application may want to make this larger, at
   least for testing). */
void
ssh_cm_config_set_max_restarts(SshCMConfig config, unsigned int max_restarts);

/* This function instructs the validator on CRL refetching.

   The argument 'minsecs' gives the minimum time for CRL refetch. CRL
   will be considered valid this many seconds since it was issued
   (thisUpdate), and will not be refetched. If the CRL provides
   information about next update time, it will be used instead of user
   provided information.

   The 'maxsecs' is the maximum time CRL is considered valid since it
   was last time introduced to the validator. A refetch of CRL is
   forced when the CRL is referenced, and this many seconds has passed
   since last retrieval. Also here the CRL nextupdate takes priority
   over the user supplied value.

   Default values are 1 hour and 12 hours respectively. */

void
ssh_cm_config_set_crl_validity_secs(SshCMConfig config,
                                    unsigned int minsecs,
                                    unsigned int maxsecs);

#ifdef SSHDIST_VALIDATOR_OCSP

/* Set the maximum number of seconds the result of the OCSP query
   result will be considered valid. The smaller of the re-check time
   of the server and this value is added to the producedAt time found
   in the response. This will overwrite the nextUpdate potentially
   given in the OCSP response.

   Default is 300 seconds. */
void
ssh_cm_config_set_ocsp_validity_secs(SshCMConfig config, unsigned int secs);
#endif /* SSHDIST_VALIDATOR_OCSP */

/* Set the maximum number of seconds a certificate will be considered
   valid after successful validation. This helps the system to keep up
   to speed. The larger the better for the system, but usually one
   should set from the range 1 hour to 7 days.

   Default is 7 days. */
void
ssh_cm_config_set_validity_secs(SshCMConfig config, unsigned int secs);

/* Set the number of seconds a LDAP (or any other external database)
   search will be in the negative cache (meaning no same search is
   done during that time).

   This ensures that we do not end up in loop trying again and again
   for getting data from the same server. This number also makes the
   minimum value for the crl and certificate validity times, because
   even if the certificate or crl is valid we will not start search
   from the external database before this negative caching time has
   expired.

   Default value is 60 seconds. */
void
ssh_cm_config_set_nega_cache_invalid_secs(SshCMConfig config,
                                          unsigned int secs);

/* This function sets the wait time for a timeout after the operation
   control function was blocked. That is, if a operation such as call
   to find was launched from a callback called from the CM itself this
   would set up a timeout such that the launched search will begin
   itself later.

   Default value is 0 seconds, 0 microseconds, e.g. operation control
   is called from the bottom of the event loop. */
void
ssh_cm_config_set_timeout(SshCMConfig config,
                          long seconds, long microseconds);

/* The maximum number of certificates held in the cache. If full the
   less used certificates will be thrown out (if there exists a local
   database on disk, then that might as well be used). Can be disabled
   by setting to zero.

   Default is to cache 1024 certificates. */
void
ssh_cm_config_set_cache_max_entries(SshCMConfig config, unsigned int entries);

/* Memory cache size limit in bytes. If cache is full the less used
   certificates will be thrown out. Cache cleanups
   can be disabled by setting to zero.

   Default is 1024 * 1024 bytes */
void
ssh_cm_config_set_cache_size(SshCMConfig config, unsigned int bytes);

/* Callback to return hash data for the external search index.

   This function is called every time the CM needs to convert
   certificate to hash data which is then inserted to the hash
   table. The hash data can be binary data of any length. The returned
   hash data must be allocated using ssh_malloc, and the CM will free
   it when it is no longer needed.

   The context pointer is the pointer given to the
   ssh_cm_config_register_search_index function.

   If the 'hash_data_return' is set to NULL then the certificate
   doesn't have valid data for requested kind of key. This function is
   not allowed to modify the certificate given to it, and it must
   always return exactly same hash for the same certificate. */
typedef void (*SshCMGenerateHashDataCB)(SshCMCertificate certificate,
                                        unsigned char **hash_data_return,
                                        size_t *hash_data_len_return,
                                        void *context);

/* Register external search index. This is given a callback function
   that is used to get hash data out of certificate. There is no way
   to add new search indexes after the cmi has been allocated, and
   there is no way to remove indexes. This returns an
   SshCMSearchIndexHandle that can be used to identify the index later
   (it is given to the ssh_cm_key_set_external).

   Default is not to use external hash generators.
*/
SshCMSearchIndexHandle
ssh_cm_config_register_search_index(SshCMConfig config,
                                    SshCMGenerateHashDataCB generate_hash_cb,
                                    void *generate_hash_cb_context);

/* Configure the notify callbacks to be used. No callbacks need to
   defined for the CM, but sometimes the application can be interested
   in finding out things in real-time, rather than by polling the
   databases itself.

   Default is no callbacks */
void
ssh_cm_config_set_notify_callbacks(SshCMConfig config,
                                   const SshCMNotifyEvents event_callbacks,
                                   void *notify_context);


/* Configure validators internal LDAP client. Zero length will keep
   current setting.

   Default maximum respose length is 5M
           and keep connection open (compatibility). */
void
ssh_cm_config_ldap_configure(SshCMConfig config,
                             size_t max_response_length,
                             SshUInt32 idle_timeout_seconds);

/* Configure validators external object sizes. Objects larger than
   these will be discarded by the valídator. Zero values will keep
   current setting.

   Default certificate limit is 16k
           crl limit is 5M */
void
ssh_cm_config_sizes_configure(SshCMConfig config,
                              size_t max_certificate_length,
                              size_t max_crl_length);


/***********************************************************************
 * KEYs on certificate database
 */

typedef struct SshCertDBKeyRec SshCertDBKey, SshCertDBKeyStruct;

/* Some of the key handling routines are given for simplicity and can
   ease of use. However, we might want to change it in future to allow
   more general applicability.

   NOTE: When the system gets in a 'char *' string and a 'size_t'
   length the length can be given as 0 for null-terminated strings
   (and the code will compute the length itself).

   The Boolean return TRUE indicates key was updated with values
   given. */

/* Set the DER encoded distinguished name. */
Boolean
ssh_cm_key_set_dn(SshCertDBKey **key,
                  const unsigned char *der_dn, size_t der_dn_len);

/* Set the LDAP style distinguished name. The value should be given as
   a US Ascii string (or depending upon the settings in distribution
   one can also use UTF-8). */
Boolean
ssh_cm_key_set_ldap_dn(SshCertDBKey **key, const char *ldap_dn);

/* Give a IP address value. */
Boolean
ssh_cm_key_set_ip(SshCertDBKey **key,
                  const unsigned char *ip, size_t ip_len);

/* Set the DNS style name. This should be US Ascii. No verification
   for the validity of the name is performed. */
Boolean
ssh_cm_key_set_dns(SshCertDBKey **key, const char *dns, size_t dns_len);

/* Set the RFC822 style email name. This should be US Ascii. No
   verification for the validity of the name is performed. */
Boolean
ssh_cm_key_set_email(SshCertDBKey **key,
                     const char *email, size_t email_len);

/* Set the URI style name. This should be US Ascii. No verification
   for the validity of the name is performed. */
Boolean
ssh_cm_key_set_uri(SshCertDBKey **key, const char *uri, size_t uri_len);

/* Set the RID style OID. This should be US Ascii. No verification for
   the validity of the OID is performed (at this level). */
Boolean
ssh_cm_key_set_rid(SshCertDBKey **key,
                   const char *rid, size_t rid_len);

/* Set the LDAP style directory name. The value should be given as a
   c-string encoded according to LDAP encoding rules. Note naming
   difference to ssh_cm_key_set_dn */
Boolean
ssh_cm_key_set_directory_name(SshCertDBKey **key, const char *ldap_dn);

/* Set the DER encoded directory name. */
Boolean
ssh_cm_key_set_directory_name_der(SshCertDBKey **key,
                                  const unsigned char *der_dn,
                                  size_t der_dn_len);

/* Set the serial number. */
Boolean
ssh_cm_key_set_serial_no(SshCertDBKey **key, SshMPInteger serial_no);

/* Set the public key identifier to the key list. */
Boolean
ssh_cm_key_set_public_key(SshCertDBKey **key, SshPublicKey public_key);

/* Set the public key identifier in PKIX style. */
Boolean
ssh_cm_key_set_x509_key_identifier(SshCertDBKey **key,
                                   const unsigned char *kid, size_t kid_len);

/* Set the cache identifier 'id' to the key list. */
Boolean
ssh_cm_key_set_cache_id(SshCertDBKey **key, unsigned int id);

/* Given a certificate you can get all the names of the subject or
   issuer with suitable choise of 'class'. The names are in the local
   DB format and usually not useful for the application. */
Boolean
ssh_cm_key_set_from_cert(SshCertDBKey **key,
                         SshCMKeyClass key_class, SshCMCertificate cm_cert);

/* Equivalent to the certificate case. */
Boolean
ssh_cm_key_set_from_crl(SshCertDBKey **key, SshCMCrl cm_crl);

/* Useful function for converting the X.509 names used by our library
   to the style handled by the local DB. */
Boolean
ssh_cm_key_convert_from_x509_name(SshCertDBKey **key, SshX509Name name);

/* Set external search index data for given search index. The data
   must be something similar than what is returned by the
   SshCMGenerateHashDataCB function. */
Boolean
ssh_cm_key_set_external(SshCertDBKey **key,
                        SshCMSearchIndexHandle index_handle,
                        const unsigned char *data, size_t len);


/***********************************************************************
 * SEARCH CONSTRAINTS
 */

/* Search constraints are used to identify the object, usually a
   certificate, which is to be found from the local DB or possibly
   from any available external database too.

   Mainly the identification is done with the key/name supplied, but
   also verification is performed for the validity period etc. */

/* Allocate a search constraints. */
SshCMSearchConstraints ssh_cm_search_allocate(void);

/* Free the search constraints context. This should be called if the
   constraints were not given to CM find functions. */
void
ssh_cm_search_free(SshCMSearchConstraints search);

/* Set the time constraints for the validity period one is interested. */
void
ssh_cm_search_set_time(SshCMSearchConstraints search,
                       SshBerTime not_before, SshBerTime not_after);

/* Search keys/names. The rule by which the elements are returned when
   multiple keys are given is currently just the intersection of
   elements which satisfy all the given keys/names. This function must
   be called at most once. */
void
ssh_cm_search_set_keys(SshCMSearchConstraints search,
                       SshCertDBKey *keys);

/* Set the X.509 supported public key algorithm type. Thus the end
   user key must match this found key type. */
void
ssh_cm_search_set_key_type(SshCMSearchConstraints search,
                           SshX509PkAlgorithm algorithm);

/* Set the X.509 key usage flag to which the end user key usage must
   match. */
void
ssh_cm_search_set_key_usage(SshCMSearchConstraints search,
                            SshX509UsageFlags flags);

/* The maximum path to be searched this time. Default is taken from
   the CM configuration. */
void
ssh_cm_search_set_path_length(SshCMSearchConstraints search,
                              size_t path_length);

/* Name constraints.
   - select if to check name constraints
   - and optionally give constraints the path has to fullfill. */

/* Policy constraints.
   - select if to check policy constraints
   - required policy identifier

   if not called, will work as if constraint is any-policy.
*/

void
ssh_cm_search_set_policy(SshCMSearchConstraints search,
                         SshUInt32 explicit_policy,
                         SshUInt32 inhibit_policy_mappings,
                         SshUInt32 inhibit_any_policy);

void
ssh_cm_search_add_user_initial_policy(SshCMSearchConstraints constraints,
                                      char *policy_oid);

/* For the search to use only local databases for certificate and/or
   CRL searches. This feature is most useful when you just want to
   validate some certificates in the cache (or that you believe that
   they still are there), but don't want to spend time if your guess
   was wrong. Also it is a good precaution when the certificate is
   guaranteed to be in a local database.

   (To be explicit, the term "local database" does not necessarily
   restrict to the certificate and CRL cache, but the set of
   databases that are flagged as "local". Of course, the certificate and
   CRL cache is always local and the hope is that all local databases
   perform their search quickly and in "real-time".) */
void
ssh_cm_search_force_local(SshCMSearchConstraints search,
                          Boolean cert, Boolean crl);


#ifdef SSHDIST_VALIDATOR_OCSP
/* Set the relation between OCSP and CRL's.

   `mode' defines the relation. Possible values are SSH_CM_OCSP_NO_OCSP,
   SSH_CM_OCSP_OCSP_ONLY and SSH_CM_OCSP_CRL_AFTER_OCSP. The first means
   that OCSP is not used but the status is checked using CRLs. The second
   mode is used when OCSP is wanted to be used without CRLs as a backup.
   In the last mode, CLRs are used if OCSP check fails. */

void
ssh_cm_search_set_ocsp_vs_crl(SshCMSearchConstraints search,
                              SshCMOcspMode mode);
#endif /* SSHDIST_VALIDATOR_OCSP */

/* This call is used to set revocation check status for a search.
   If 'onoff' is TRUE, revocation check is done, and FALSE disables
   revocation checks. When revocation checks are disabled, the
   validation will not access revocation information from the network,
   nor use locally cached copies of CRL's.

   The default it to check revocation using the means specified. */
void
ssh_cm_search_check_revocation(SshCMSearchConstraints search,
                               Boolean onoff);

/* This function sets the "trusted set" for the search constraints. By
   default it is zero (0), meaning that all sets are allowed. What
   sets can be specified is totally up to the application, it is not
   mandated by the certificate manager. */
void
ssh_cm_search_set_trusted_set(SshCMSearchConstraints search,
                              SshMPInteger trusted_set);

/* The way the names affect the search. The default is
   SSH_CM_SEARCH_RULE_AND, which is most sensible when searching for
   certificates. The exact meanings of these rules are

   SSH_CM_SEARCH_RULE_AND
      Return only certificates having all the searched names.

   SSH_CM_SEARCH_RULE_OR
      Return only certificates having any of the searched names.

   It seems that certificates should be usually searched with *_AND
   rule, however, for CRL's the *_OR rule is useful. */
void
ssh_cm_search_set_rule(SshCMSearchConstraints search, SshCMSearchRule rule);

/* The constraint for group mode. This defines the resulting list of
   certificates to the callback differently. Instead of returning a
   whole path from the end user to the trusted CA this search just
   returns all the end users that match the search, not just the first
   lucky one.

   It is assumed that this style of searching would be used with
   caution, due possibility of taking quite a long time (given such
   search keys).  That is, you should try to work without this mode as
   long as you can. */
void
ssh_cm_search_set_group_mode(SshCMSearchConstraints search);

/* Searching mode which forces a full verification path to a trusted
   root. This option is forced in all search constraint
   combinations. In practice this option is useful when the
   application needs a path from a trusted to root to the end user
   certificate, and doesn't necessary want to choose the CA
   certificate.

   Using this option is often not necessary and may be avoided due to
   performance penalty. In fact, search using this option cannot take
   into account some incremental optimizations available. */
void
ssh_cm_search_set_until_root(SshCMSearchConstraints search);

/***********************************************************************
 * ENTRY LIST
 */

/* Routines for traversing the certificate and CRL lists.

   These lists are given to the callbacks after successful
   searches. The application is allowed to hold the list as long as
   necessary, however, when CM is run it can change the status of
   these certificate and CRL's (e.g. they can become revoked or
   removed from the local DB).

   Entry list should be released as soon as possible. If the
   application needs certificates from the list, it should copy the
   certificate. */

/* Test predicate for certificate or CRL list being empty. */
Boolean ssh_cm_cert_list_empty(SshCMCertList list);
Boolean ssh_cm_crl_list_empty(SshCMCrlList list);

/* Traversing functions in the certificate list. You will get pointers
   to the CM certificates. You don't need to free them, and can ignore
   or study them. The list keeps a pointer to the current entry. */
SshCMCertificate ssh_cm_cert_list_first(SshCMCertList list);
SshCMCertificate ssh_cm_cert_list_next(SshCMCertList list);
SshCMCertificate ssh_cm_cert_list_prev(SshCMCertList list);
SshCMCertificate ssh_cm_cert_list_last(SshCMCertList list);
SshCMCertificate ssh_cm_cert_list_current(SshCMCertList list);

/* Traversing functions in the crl list. You will get pointers to the
   CM CRL's. You don't need to free them, and can ignore or study
   them. The list keeps a pointer to the current entry. */
SshCMCrl ssh_cm_crl_list_first(SshCMCrlList list);
SshCMCrl ssh_cm_crl_list_next(SshCMCrlList list);
SshCMCrl ssh_cm_crl_list_prev(SshCMCrlList list);
SshCMCrl ssh_cm_crl_list_last(SshCMCrlList list);
SshCMCrl ssh_cm_crl_list_current(SshCMCrlList list);

/* After the list has been analysed you must free them. The free will
   release the locks in the database. */
void ssh_cm_cert_list_free(SshCMContext cm, SshCMCertList list);
void ssh_cm_crl_list_free(SshCMContext cm, SshCMCrlList list);

/***********************************************************************
 * CRL
 */

/* This interface is mainly intended to be used when supplying CRL's
   from outside. However, the system uses the same interface
   internally. */

/* Allocate a CRL data structure used by CM. */
SshCMCrl ssh_cm_crl_allocate(SshCMContext cm);

/* Free the CRL data structure. Application should use this interface
   only if the CM doesn't free the data structures itself. */
void ssh_cm_crl_free(SshCMCrl crl);

/* Add a crl to the database. */
SshCMStatus ssh_cm_add_crl(SshCMCrl crl);

/* Remove the CRL from the cache. Note that if the CRL was used to
   validate certificates, removing CRL does not clear validity status
   of those certificates found revoked. */
void ssh_cm_crl_remove(SshCMCrl crl);

/* Set a ASN.1 BER coded X.509 CRL to CM CRL data structure. This will
   return a failure status if the X.509 decoding failed. */
SshCMStatus
ssh_cm_crl_set_ber(SshCMCrl crl,
                   const unsigned char *ber, size_t ber_length);

/* Get corresponding ASN.1 DER binary blob of the CM CRL. */
SshCMStatus
ssh_cm_crl_get_ber(SshCMCrl c,
                   unsigned char **ber, size_t *ber_length);

/* Get an X.509 CRL from CM CRl data structure. The pointer is a
   reference to the same data structure included in 'c'. Application
   should not free the CRL by itself. When 'c' is freed also 'crl'
   becomes undefined. */
SshCMStatus ssh_cm_crl_get_x509(SshCMCrl c, SshX509Crl *crl);

/* This function seeks the local cache identifier (e.g. cache id, or
   entry id) of the CRL given as an input, or in case the CRL already
   lies in the cache (e.g. in duplicate) fetches the id of the one in
   the cache. */
unsigned int ssh_cm_crl_get_cache_id(SshCMCrl c);

/***********************************************************************
 * CERTIFICATE
 */

/* Allocate a CM certificate. This context is used to create a
   certificate framework for the certificate manager. */
SshCMCertificate ssh_cm_cert_allocate(SshCMContext cm);

/* Free a CM certificate. This should be used only for those
   certificates that are not added to the database. If you try to free
   a certificate that is part of the database the system will call
   ssh_fatal. */
void ssh_cm_cert_free(SshCMCertificate c);

/* Add a certificate to the database. This operation assumes that the
   certificate is a structurally correct, although, it will check
   against it too. The certificate can be revoked etc. This library is
   trying to know whether a certificate is revoked or not. */
SshCMStatus ssh_cm_add(SshCMCertificate cert);

/* Remove the certificate from the cache. The certificate must not be
   locked when calling this function. */
void ssh_cm_cert_remove(SshCMCertificate c);

/* Taking and removing a reference to a certificate. The certificate
   must already be in the certificate cache for this to take effect. A
   taken reference must always be removed or the certificate will stay
   in the cache forever. */
void ssh_cm_cert_take_reference(SshCMCertificate cert);
void ssh_cm_cert_remove_reference(SshCMCertificate cert);

/* Initialize the CM certificate structure with ASN.1 BER coded
   certicate. The certificate should be valid X.509 certificate or an
   error will be returned. */

SshCMStatus
ssh_cm_cert_set_ber(SshCMCertificate c,
                    const unsigned char *ber, size_t ber_length);

/* Set the maximum path allowed under this certificate within the
   local database. This limit can only restrict it won't allow longer
   paths than the certificate itself. */
void ssh_cm_cert_set_path_length(SshCMCertificate c, size_t path_length);

/* Define the "trusted set" of the trusted root certificate. The
   trusted set is by default for each certificate zero (0). This
   function is meaningful only for trusted roots, as it classifies the
   trusted roots for the searches. The application of this set is to
   partition the set of trusted root certificates so that within
   certificate manager one can select suitable trusted root set for
   validation.

   For example, you have trusted roots A and B, but wish only to use A
   in some search you could utilize the "trusted set" for this. For
   example, by setting A's trusted set to be 1, and let B's be 0. Then
   in search constraints you need to specify the trusted set 1, so
   that only certificates trusted by A are considered.

   Please observe that using very large integers grows also the size
   of the cache (naturally). It is immediate that the number of
   possible sets is limited in practice to, say, 100. However, you can
   use combinations of them.

   There should be very little penalty on performance for using
   trusted sets, as the certificate manager attempts to cache
   information based on previous searches. */
void ssh_cm_cert_set_trusted_set(SshCMCertificate c,
                                 SshMPInteger trusted_set);

/* This function returns the trusted set of the current certificate.
   For trusted roots this will be most important, as this defines the
   "classes" to which the certificate belongs to (and the application
   may change them). For other (non-root) certificates it can be used
   to determine which trusted root "classes" were needed to find
   validation for the certificate. However, ultimately the search
   constraints define what are the possibilities, as you must define
   there the available sets.

   You should NOT free the returned large integer. It is freed by
   certificate manager when the certificate is removed from the
   cache. Keeping a reference to the given certificate you can use the
   integer as long as necessary.  */
SshMPInteger ssh_cm_cert_get_trusted_set(SshCMCertificate c);

/* This function set the time after the certificate shall not be trusted
   in searches (although it still maintains the status of a trusted root
   within).

   Remark. Currently this functionality may work less than
   optimally. It is optional feature, and not required. There will be
   no notification of trusted root becoming "untrusted" at the
   moment.

   XXX Implement notification. */
void ssh_cm_cert_set_trusted_not_after(SshCMCertificate c,
                                       SshBerTime trusted_not_after);


/* Set private data to the certificate. The destructor is called when
   the certificate is destroyed (e.g. flushed out of the cache). Using
   this function again removes the previous arguments (and calls the
   previous desctruction while doing this). */
SshCMStatus
ssh_cm_cert_set_private_data(SshCMCertificate c,
                             void *private_context,
                             SshCMPrivateDataDestructor destructor);

/* Get private data from the certificate. This function does not
   remove the data, only obtains a pointer to it. */
SshCMStatus
ssh_cm_cert_get_private_data(SshCMCertificate c,
                             void **private_context);


/* Force the current certificate to be trusted without any attempts to
   deny it.

   This operation causes the certificate to be moved to the trusted
   class. When the trust status is changed the certificate is returned to
   the class where it was previously.

   The reason why trusted certificates are set to particular class always
   is to allow libraries to have a way to know all the trusted certificates
   without application interaction.
   */
SshCMStatus ssh_cm_cert_force_trusted(SshCMCertificate c);

/* Remove the trusted status of the certificate. Changes the class
   of the certificate back to the previous class in which it belonged. */
SshCMStatus ssh_cm_cert_force_untrusted(SshCMCertificate c);

/* Force not to need CRL's when acting as a CA. This option is to be
   used with care, however, for testing and similar this may be
   useful. Basically, applying this option to a certificate you force
   CM to never search for a CRL issued by this certificate. That is,
   it assumes that certificates issued by (the principal behind) this
   certificate are valid upto their full validity period.

   If the certificate is an end user certificate this is assumed and
   you don't need to use this function in that case. The CA status is
   implied by the authentication hierarchy.  */
SshCMStatus ssh_cm_cert_non_crl_issuer(SshCMCertificate c);
SshCMStatus ssh_cm_cert_make_crl_issuer(SshCMCertificate c);

/* Make the certificate to seek/not seek for CRL. */
SshCMStatus ssh_cm_cert_non_crl_user(SshCMCertificate c);
SshCMStatus ssh_cm_cert_make_crl_user(SshCMCertificate c);

/* This function seeks the local cache identifier (e.g. cache id, or
   entry id) of the certificate given as an input, or in case the
   certificate already lies in the cache (e.g. in duplicate) fetches
   the id of the one in the cache. This works most of the time in
   similar fashion as the ssh_cm_cert_get_entry_id, but differently
   when the certificate already exists in the cache. */
unsigned int ssh_cm_cert_get_cache_id(SshCMCertificate cert);

/* Get the subject/issuer keys/names of the CM certificate. This data
   structure output is not useful for use with X.509 library due the
   names might be transformed into more useful form for the DB. */
SshCMStatus ssh_cm_cert_get_subject_keys(SshCMCertificate c,
                                         SshCertDBKey **keys);
SshCMStatus ssh_cm_cert_get_issuer_keys(SshCMCertificate c,
                                        SshCertDBKey **keys);

/* Get corresponding ASN.1 BER binary blob of the CM certificate. The
   blob returned must not be freed by the application, and a copy
   should be taken in case the returned certificate is to be used
   after the application has visited the bottom of the event loop. */
SshCMStatus ssh_cm_cert_get_ber(SshCMCertificate c,
                                unsigned char **ber, size_t *ber_length);

/* Get the X.509 certificate out of the CM certificate.
   The application needs to free the returned certificate, if the
   return indicates success. */
SshCMStatus ssh_cm_cert_get_x509(SshCMCertificate c,
                                 SshX509Certificate *cert);

/* Get the validity times computed during the last search. This
   information is not updated unless the certificate is searched
   again. To search a certificate repeatedly one can take the entry
   identifier as the key.

   Returns a failure if the certificate is not trusted.
   */
SshCMStatus ssh_cm_cert_get_computed_validity(SshCMCertificate c,
                                              SshBerTime not_before,
                                              SshBerTime not_after);


/* Last time the certificate was verified by the system. This function
   returns the time the CMi last used this certificate in
   searching. */
SshCMStatus ssh_cm_cert_get_computed_time(SshCMCertificate c,
                                          SshBerTime computed);

/* Is this certificate a trusted root certificate? If the certificate is
   trusted by the system and a trusted root then returns TRUE, otherwise
   FALSE. In practice this ensures that certificates that are trusted roots,
   can be revoked. */
Boolean ssh_cm_cert_is_trusted_root(SshCMCertificate c);

/* Is the this certificate a CRL issuer or not? */
Boolean ssh_cm_cert_is_crl_issuer(SshCMCertificate c);

/* Is the this certificate a CRL user or not? */
Boolean ssh_cm_cert_is_crl_user(SshCMCertificate c);

/* Check if locked. */
Boolean ssh_cm_cert_is_locked(SshCMCertificate c);

/* Is the certificate revoked? Returns TRUE if the certificate is a
   revoked or untrusted. If the certificate is valid in the cache then
   returns FALSE. */
Boolean ssh_cm_cert_is_revoked(SshCMCertificate c);

/* Information that needs the knowledge of the Certificate Manager
   context. */

/* Lock the certificate to the cache. This function can be used before
   the certificate has been added to the cache, and hence it will be useful
   for e.g. keeping the certificate locked to the application over the
   addition operation (which may otherwise make the pointer to the
   certificate undefined). */
SshCMStatus ssh_cm_cert_set_locked(SshCMCertificate c);

/* Unlock the certificate. */
SshCMStatus ssh_cm_cert_set_unlocked(SshCMCertificate c);


/* Derive a certificate manager context from a certificate. */
SshCMContext ssh_cm_cert_derive_cm_context(SshCMCertificate c);


/* Manipulation of the certificate classes. */

/* A certificate class is used to partition the certificate in the cache.
   Each class can be enumerated through separately, and hence applications
   can, for example, find all the trusted certificate in the cache.
   */

/* The fixed classes for the certificate manager. The invalid class
   denotes a class that is not used. Trusted class contains all trusted
   root certificates. The locked class contains all locked certificates,
   if their class has not been altered. The default class is the place where
   all certificates are initially placed. */
#define SSH_CM_CCLASS_INVALID ((~(unsigned int)0) - 4)
#define SSH_CM_CCLASS_DEFAULT ((~(unsigned int)0) - 2)
#define SSH_CM_CCLASS_LOCKED  ((~(unsigned int)0) - 1)
#define SSH_CM_CCLASS_TRUSTED ((~(unsigned int)0) - 0)


#define SSH_CM_CCLASS_MAX     256

/* Change the class of a certificate. This does not override
   the *_TRUSTED class, however, it can be used to override all
   other classes. */

SshCMStatus ssh_cm_cert_set_class(SshCMCertificate c,
                                  unsigned int app_class);

/* Get the class of a certificate. */
unsigned int ssh_cm_cert_get_class(SshCMCertificate c);

/* Run through all the classes. */
unsigned int ssh_cm_cert_get_next_class(SshCMContext cm,
                                        unsigned int app_class);

/* The callback for the enumeration function. */
typedef void (*SshCMCertEnumerateCB)(SshCMCertificate cert,
                                     void *context);

/* Enumerate through all the certificates in a particular class. */
SshCMStatus ssh_cm_cert_enumerate_class(SshCMContext cm,
                                        unsigned int app_class,
                                        SshCMCertEnumerateCB callback,
                                        void *context);

typedef void (*SshCMCrlEnumerateCB)(SshCMCrl crl, void *context);
SshCMStatus
ssh_cm_crl_enumerate(SshCMContext cm,
		     SshCMCrlEnumerateCB callback, void *callback_context);

/***** Interface to auxliary methods for entering certificates and crls. */

/* XXX: this interface is not needed, CMI should operate with
   primitive objects, namely certificates and crls. The application
   should open the PKCS7 and decide which parts of its contents are to
   be added.*/

/* Add all certificates, and CRLs available from the given PKCS-7 packet.
   The routines do not open encryptions, nor check any validity of
   cryptographical information. However, they run through the
   PKCS-7 structure and add all the certificates and crl (even those in
   low levels) to the local cache.

   Currently the application cannot control the settings of the
   certificates, nor the CRLs. They are added without any specific
   information. Usually this should not be a problem.
   */

SshCMStatus ssh_cm_add_pkcs7(SshCMContext cm, SshPkcs7 packet);
SshCMStatus ssh_cm_add_pkcs7_ber(SshCMContext cm,
                                 unsigned char *ber_buf,
                                 size_t         ber_length);


/****************************************************************************
 * Certificate manager initialization
 */

/* Allocate a Certificate Manager context. You can do all searching
   and basic operations through it. All database operations will be
   done through this interface.

   This will always steal the config data, even if this fails. If the
   application keeps the provided configuration data, it may change
   some values from it on the fly. However doing this may not
   neccessarily yield into desired results immediately. */
SshCMContext ssh_cm_allocate(SshCMConfig config);

/* Free the Certificate Manager.

   This function free's all the databases and clients available. This
   function must not be called with a running validator. One must
   first shut down the validator with a call to ssh_cm_stop, and wait
   for it to indicate completion with call to provided callback. When
   the callback has been called, the validator will no longer have any
   timeouts, nor open connections, and all the active searches have
   been completed.

   It is legal to call ssh_cm_free without stopping it first only if
   the caller knows it has not any outstanding searches on the
   validator. */

void ssh_cm_free(SshCMContext cm);

/* Callback to indicate Certificate validator has stopped. */
typedef void (*SshCMDestroyedCB)(void *context);


/* This function stops the Certificate validator. The provided
   'callback' will be called at the time validator has finally been
   stopped e.g. after all the outstanding operations have been
   completed, and all timeouts and handles have been released. This
   operation can not be aborted. New searches using the stopping
   validator are banned. The given callback can be called from within
   the call, or at a later time.

   After being stopped, the validator still contains Certificates and
   CRL it had before stopping, and operation can be resumed by
   performing any search from the validator. */

void ssh_cm_stop(SshCMContext cm,
                 SshCMDestroyedCB callback, void *callback_context);


/* The CM OP Control functionality. When called, the current search
   will proceed if something to search still exists. Will do several
   searches, the depth of searching can be configured.

   The CM will attempt to call itself through the event loop if the
   application calls the system in event loops. The timeout function
   is actually this very operation control function. The duration for
   timeout can be configured. Also if the depth of search become too
   long then a timeout will be lauched.

   Use ssh_cm_searching function to poll whether you need to call this.  */
SshCMStatus ssh_cm_operation_control(SshCMContext cm);

/***********************************************************************
 * SEARCHING
 */

/* Callback function the CM calls to return search results to the
   application. */
typedef void (*SshCMSearchResult)(void *caller_context,
                                  SshCMSearchInfo info,
                                  SshCMCertList   list);

/* Find a certificate from the database, local or external, configured
   to be used with this Certificate Manager context 'cm'. The result
   of the search will be informed through the callback
   'result_callback' called with 'caller_context' as its
   'caller_context' argument.

   Input parameter 'search', specifies the object to look for, will be
   stolen and freed by the this library. Caller must not alter it
   after this call returns.

   The return value SSH_CM_STATUS_SEARCHING indicates that the search
   did not succeed at first, and it will be tried again after a while.
   The callback should be ready to take the result, it will always be
   acknowledged of the searchs final state.

   The return value SSH_CM_STATUS_OK indicates that the search completed
   directly. It doesn't necessarily mean that the search was a success.
   */
SshCMStatus ssh_cm_find(SshCMContext cm,
                        SshCMSearchConstraints search,
                        SshCMSearchResult result_callback,
                        void *caller_context);

/* Similar to the previous function, but this searches for a path of
   certificates from the CA to the end certificate. Result is returned
   to the callback.

   Same constraints apply to the search_ca, and search_cert as in the
   previous call.

   Return value SSH_CM_STATUS_SEARCHING indicates that the search did
   not succeed at first and will be completed later.

   The return value SSH_CM_STATUS_OK indicates that the search
   completed directly. It doesn't necessarily mean that the search was
   a success.
   */
SshCMStatus ssh_cm_find_path(SshCMContext cm,
                             SshCMSearchConstraints search_ca,
                             SshCMSearchConstraints search_cert,
                             SshCMSearchResult result_callback,
                             void *caller_context);

/***************************************************/

/* Searching of certificates without computing the authentication
   path. That is routines for find any certificate, CRL etc., even a
   revoked one. */

/* CAUTION: Following functions are to be used only for study of the
   contents of the CM local database or cache. You should not trust
   certificates found through these function calls. Also these functions
   do not necessarily support all the search constraints. */

SshCMStatus ssh_cm_find_local_cert(SshCMContext cm,
                                   SshCMSearchConstraints search,
                                   SshCMCertList *cert_list);

SshCMStatus ssh_cm_find_local_crl(SshCMContext cm,
                                  SshCMSearchConstraints search,
                                  SshCMCrlList *crl_list);

/* Find the issuer of the certificate pointed by the search data. This
   is just a wrapper of the previously defined functions for searching for
   a certificate and getting the issuer names out of it.

   Return SSH_CM_STATUS_OK if issuer found. */
SshCMStatus ssh_cm_find_local_cert_issuer(SshCMContext cm,
                                          SshCMSearchConstraints search,
                                          SshCMCertList *issuer_list);

/***************************************************/

/* EDB (External DataBase) interface. The external database denotes
   a method for searching certificates beyond the certificate cache. This
   includes LDAP, HTTP etc. Application may add new methods with this
   interface.
   */

/* The local network data structure. Application needs to fill this
   when configuring the certificate manager. */

typedef struct
{
  /* Socks server. */
  char *socks;
  /* HTTP proxy. */
  char *proxy;

  /* A timeout delay. Time is given in milliseconds. */
  SshUInt32 timeout_msecs;

  /* Future local network information. */

} *SshCMLocalNetwork, SshCMLocalNetworkStruct;

/* Local network parameters. */

/* Copy local_network parameters from the structure to internal context. The
   data structure is copied, and must be freed by the application (at any
   time it wants).

   Most external databases look-up the local network parameters only at
   the initialization. This implies that later changes will not likely
   cause any changes in the behaviour. If application finds dynamic
   changing of local network parameters useful the external database
   code needs to be changed accordingly (sometimes it may be difficult).
   */
void ssh_cm_edb_set_local_network(SshCMContext cm,
                                  SshCMLocalNetwork local_network);

/* Returns a pointer to the internal data structure. Applications should
   not change the structure. */
SshCMLocalNetwork ssh_cm_edb_get_local_network(SshCMContext cm);

#ifdef SSHDIST_VALIDATOR_OCSP

/****************************************************************************
 * OCSP Responder setup.
 */

/* Initialise OCSP in CMi. This has to be called before using any
   other OCSP functions of CMi. */
Boolean ssh_cm_edb_ocsp_init(SshCMContext cm);

/* Add a new OCSP responder pattern for the manager. The arguments
   have following semantics:

   `requestor_name' denotes the name of the requestor (i.e.
   certificate manager, i.e. client). It must be present and
   dynamically allocated. The library frees the name.

   `requestor_private_key' is the private key of the requestor, used
   when signing the OCSP request.  The library frees the private key.

   `responder_url' is the address of the responder (e.g. OCSP
   server). The URL is copied by the library.

   `hash_algorithm' is the hash algorithm to be used for the
   connection.  The hash algorithm field is copied by the library, and
   thus caller can (and must) free the argument.

   `ca_key_identifier' identifies the CA whose certificates this
   responder can handle (can be NULL). The value is compared to the
   issuer key hash found in the OCSP request. This value is copied by
   the library and thus caller can (and must) free the argument.

   `ca_kid_len' defines the length of the `ca_key_identifier'.

   The `recheck_time_secs' gives the seconds a response from an OCSP
   responder is taken to be valid.

   `flags' determines how to deal with nonces and times in the
   response.

   The return value will be either 0 or an unique integer above or
   equal to 1 denoted for the created responder context. Return value
   0 implies that the operation did not construct a new responder
   (probably because a responder with same data already exists or then
   some of the values are invalid). */
unsigned int
ssh_cm_edb_ocsp_add_responder(SshCMContext cm,
                              SshX509Name requestor_name,
                              SshPrivateKey requestor_private_key,
                              const char *responder_url,
                              const char *hash_algorithm,
                              const unsigned char *ca_key_identifier,
                              size_t ca_kid_len,
                              const unsigned char *responder_certificate,
                              size_t responder_certificate_length,
                              SshUInt32 recheck_time_secs,
                              SshCMOcspResponderFlags flags);

/* Remove the OCSP responder having the `id'.

   XXX This should definitely do lazy removal, in the sense that
       if the responder is currently needed then it would not be
       removed until some later time. (E.g. it could be thrown to a
       suitable freelist.) */
void ssh_cm_edb_ocsp_remove_responder(SshCMContext cm, unsigned int id);


#endif /* SSHDIST_VALIDATOR_OCSP */

/***** Build-in databases. */

#ifdef SSHDIST_LDAP
/* All default databases are implemented as database methods using the
   above system.

   In practice these routines are used to set up the database, or just
   to "put it on".

   Application does not need to use these databases, as they can be
   easily substituted by other external databases. */

/* LDAP method. Database identifier "ssh.ldap".

   Uses the SSH LDAP client. The input 'default_servers' is a comma separated
   list of names of LDAP servers with format

      name1:port1,name2:port2,name3:port3,...

   these are not URL's.

   It is valid to add new servers after the CM has been initialized,
   this call will always remove all previously added ldap servers
   before adding new ones.  If the default_servers is NULL then ldap
   server is disabled.

   Returns FALSE if initialization of some (any) servers failed. */
Boolean ssh_cm_edb_ldap_init(SshCMContext cm,
                             const char *default_servers);
#endif /* SSHDIST_LDAP */

/* HTTP method. Database identifier "ssh.http".

   Uses the SSH HTTP client. After initialization all
   reinitializations are ignored.

   Return FALSE if initialization failed.  */
Boolean ssh_cm_edb_http_init(SshCMContext cm);

/***************************************************/

#endif /* CMI_H */
