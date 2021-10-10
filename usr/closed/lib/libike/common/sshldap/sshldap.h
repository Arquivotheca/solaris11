/*
  File: sshldap.h

  Description:
        LDAPv3 client side API at extend needed when using LDAP to
        access PKI related information.

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
        Finland.
        All rights reserved.
*/

#ifndef SSHLDAP_H
#define SSHLDAP_H

#include "sshenum.h"
#include "sshtcp.h"
#include "sshoperation.h"

/* LDAP protocol version. */
typedef enum {
  SSH_LDAP_VERSION_2 = 2,
  SSH_LDAP_VERSION_3 = 3
} SshLdapVersion;

/* LDAP client. */
typedef struct SshLdapClientRec *SshLdapClient;

/* LDAP Operation Result Codes. */
typedef enum {
  SSH_LDAP_RESULT_SUCCESS                       = 0,

  SSH_LDAP_RESULT_OPERATIONS_ERROR              = 1,
  SSH_LDAP_RESULT_PROTOCOL_ERROR                = 2,
  SSH_LDAP_RESULT_TIME_LIMIT_EXCEEDED           = 3,
  SSH_LDAP_RESULT_SIZE_LIMIT_EXCEEDED           = 4,
  SSH_LDAP_RESULT_COMPARE_FALSE                 = 5,
  SSH_LDAP_RESULT_COMPARE_TRUE                  = 6,
  SSH_LDAP_RESULT_AUTH_METHOD_NOT_SUPPORTED     = 7,
  SSH_LDAP_RESULT_STRONG_AUTH_REQUIRED          = 8,

  /* Only at LDAPv3. */
  SSH_LDAP_RESULT_REFERRAL                      = 10,
  SSH_LDAP_RESULT_ADMINLIMITEXCEEDED            = 11,
  SSH_LDAP_RESULT_UNAVAILABLECRITICALEXTENSION  = 12,
  SSH_LDAP_RESULT_CONFIDENTIALITYREQUIRED       = 13,
  SSH_LDAP_RESULT_SASLBINDINPROGRESS            = 14,

  SSH_LDAP_RESULT_NO_SUCH_ATTRIBUTE             = 16,
  SSH_LDAP_RESULT_UNDEFINED_ATTRIBUTE_TYPE      = 17,
  SSH_LDAP_RESULT_INAPPROPRIATE_MATCHING        = 18,
  SSH_LDAP_RESULT_CONSTRAINT_VIOLATION          = 19,
  SSH_LDAP_RESULT_ATTRIBUTE_OR_VALUE_EXISTS     = 20,
  SSH_LDAP_RESULT_INVALID_ATTRIBUTE_SYNTAX      = 21,

  SSH_LDAP_RESULT_NO_SUCH_OBJECT                = 32,
  SSH_LDAP_RESULT_ALIAS_PROBLEM                 = 33,
  SSH_LDAP_RESULT_INVALID_DN_SYNTAX             = 34,
  SSH_LDAP_RESULT_IS_LEAF                       = 35,
  SSH_LDAP_RESULT_ALIAS_DEREFERENCING_PROBLEM   = 36,

  SSH_LDAP_RESULT_INAPPROPRIATE_AUTHENTICATION  = 48,
  SSH_LDAP_RESULT_INVALID_CREDENTIALS           = 49,
  SSH_LDAP_RESULT_INSUFFICIENT_ACCESS_RIGHTS    = 50,
  SSH_LDAP_RESULT_BUSY                          = 51,
  SSH_LDAP_RESULT_UNAVAILABLE                   = 52,
  SSH_LDAP_RESULT_UNWILLING_TO_PERFORM          = 53,
  SSH_LDAP_RESULT_LOOP_DETECT                   = 54,

  SSH_LDAP_RESULT_NAMING_VIOLATION              = 64,
  SSH_LDAP_RESULT_OBJECT_CLASS_VIOLATION        = 65,
  SSH_LDAP_RESULT_NOT_ALLOWED_ON_NON_LEAF       = 66,
  SSH_LDAP_RESULT_NOT_ALLOWED_ON_RDN            = 67,
  SSH_LDAP_RESULT_ENTRY_ALREADY_EXISTS          = 68,
  SSH_LDAP_RESULT_OBJECT_CLASS_MODS_PROHIBITED  = 69,

  /* Only at LDAPv3. */
  SSH_LDAP_RESULT_AFFECTSMULTIPLEDSAS           = 71,

  SSH_LDAP_RESULT_OTHER                         = 80,

  /* Codes reserved for APIs. */
  SSH_LDAP_RESULT_ABORTED                       = 81,
  SSH_LDAP_RESULT_IN_PROGRESS                   = 82,
  SSH_LDAP_RESULT_INTERNAL                      = 83,
  SSH_LDAP_RESULT_DISCONNECTED                  = 84
} SshLdapResult;

/* Convert error code to US english string. */
extern const SshKeywordStruct ssh_ldap_error_keywords[];
const char *ssh_ldap_error_code_to_string(SshLdapResult code);

typedef struct SshLdapResultInfoRec
{
  unsigned char *matched_dn;
  size_t matched_dn_len;
  unsigned char *error_message;
  size_t error_message_len;

  /* Array of referrals. Each element in the array whose size is
     'number_of_referrals' is a pointer to a nul-terminated
     C-string. */
  size_t number_of_referrals;
  char **referrals;

  /* LDAP extension mechanism. */
  char *extension_name;
  unsigned char *extension_data;
  size_t extension_data_len;
} *SshLdapResultInfo, SshLdapResultInfoStruct;

/* LDAP result callback.
   This is called after each operation is finished, e.g. once for each
   operation that is not aborted by the caller. */
typedef void
(*SshLdapClientResultCB)(SshLdapClient client,
                         SshLdapResult result,
                         const SshLdapResultInfo info,
                         void *callback_context);

/* LDAP result callback.  This is called after tls start request has
   finished. This needs to wrap the plaintext 'ldap_stream' into TLS
   stream which this returns. */
typedef SshStream
(*SshLdapClientWrapCB)(SshLdapClient client,
                       SshLdapResult result,
                       const SshLdapResultInfo info,
                       SshStream ldap_stream,
                       void *callback_context);

/* LDAP attribute list. This containts all values for single attribute
   type. */

typedef struct SshLdapAttributeRec {
  /* Allocate attribute type and its size. */
  unsigned char *attribute_type;
  size_t attribute_type_len;

  /* Number of attribute values. */
  int number_of_values;

  /* Allocated table of allocated attribute values and respective sizes. */
  unsigned char **values;
  size_t *value_lens;
} *SshLdapAttribute;

/* LDAP search result object and public operations on the object type.
   This object contains information about one object in the LDAP
   directory server. The object_name is DN to the object found. */

typedef struct SshLdapObjectRec {
  /* Allocated object name and its length. */
  unsigned char *object_name;
  size_t object_name_len;

  /* Number of attributes in the object, and the allocated attributes
     table of size 'number_of_attributes'. */
  int number_of_attributes;
  SshLdapAttribute attributes;
} *SshLdapObject;

/* Free LDAP object */
void ssh_ldap_free_object(SshLdapObject object);

/* Duplicate a LDAP object.
   If null_terminated is TRUE, the strings at the source object are
   null-terminated and the lengths are disgarded. The source object is
   not modified. */
SshLdapObject ssh_ldap_duplicate_object(const SshLdapObject object,
                                        Boolean null_terminated);


/* LDAP search result callback.

   This will be called once for each object found.  After all search
   result objects have been processed the SshLdapClientResultCB will
   be called to notify search has completed and this will not be
   called again for the same operation.

   The copy of object found is given to the this function and it is
   responsible of freeing it after it is not needed any more. */
typedef void (*SshLdapSearchResultCB)(SshLdapClient client,
                                      SshLdapObject object,
                                      void *callback_context);


/* LDAP connect callback.

   This receives the result of LDAP connect operation, which must
   always be done using the API function ssh_ldap_client_connect().
   On successful connection the 'status' contain SSH_TCP_OK. */
typedef void (*SshLdapConnectCB)(SshLdapClient client,
                                 SshTcpError status,
                                 void *callback_context);

/* LDAP client parameters. */
typedef struct SshLdapClientParamsRec {
  /* Socks URL, default is not to use socks. */
  char *socks;

  /* Number of connection attempts, 0 means default, which is one. */
  int connection_attempts;

  /* Maximum number of simultaneus LDAP operations performed using
     same client handle, default, if this is zero is five. */
  int maxoperations;

  /* Number of seconds the LDAP server is allowed to process the
     request. If not given value zero is used, and the server
     policy decides the actual value. */
  int request_timelimit;

  /* Maximum number of objects in the response. This value is sent to the
     server as a hint, it is up the server policy to decide if it should be
     enforced or not. Server can also always default to smaller limit.
     When set to zero the limit is disabled. */
  int response_sizelimit;

  /* Maximum number of bytes in the response, enforced by the client. If 
     input exceeds this value the connection is closed. Set to zero to 
     disable. */
  int response_bytelimit;

  /* Version number of LDAP protocol used by this client. Note that
     the search and modify operations will fail on client code if
     version is 2 and bind has not been performed. Default is two. */
  SshLdapVersion version;

  /* LDAP version 2 flavored TLS wrapping */
  SshLdapClientWrapCB stream_wrap;
  void *stream_wrap_context;

} *SshLdapClientParams, SshLdapClientParamsStruct;

/*****************************************************************************
 * LDAP client operations
 */

/* Allocate LDAP client context. The input parameters, which may be a
   NULL pointer, is used only within this call, and can be freed after
   the call returns.  The 'params' may be NULL, in which case default
   values are used. */
SshLdapClient ssh_ldap_client_create(const SshLdapClientParams params);

/* Destroy LDAP client. One should call ssh_operation_abort() for all
   pending operations before calling this to avoid memory leaks, or
   risk for NULL or free pointer dereference. */
void ssh_ldap_client_destroy(SshLdapClient client);

/*****************************************************************************
 * LDAP operations on client, connect, disconnect, bind, unbind.
 */

/* Open connection to LDAP server.

   This function must not be called, if the LDAP client given is
   already connected to any server. For reconnect, existing
   connections must first be disconnected. This can be called, if
   search/modify operations have failed with status
   SSH_LDAP_RESULT_DISCONNECTED.

   'ldap_server_name' is IP number or DNS name for the LDAP server to
   connect to.
   'ldap_server_port' is port number, or services entry name.
   'callback' is called when connection is established or has failed.

   If the ldap connection is closed by the server, after having been
   successfully established, this information will be passed to using
   application next time it performs an operation (result callback
   with disconnected status). */
SshOperationHandle
ssh_ldap_client_connect(SshLdapClient client,
                        const char *ldap_server_name,
                        const char *ldap_server_port,
                        SshLdapConnectCB callback,
                        void *callback_context);

/* Disconnect connection to LDAP server. This is also done
   automatically if not done before destroying the client.

   Note, that if there is any operations in progress, they are
   immediately aborted and SshLdapClientResultCB is called for them
   with error code SSH_LDAP_RESULT_ABORTED.  */
void ssh_ldap_client_disconnect(SshLdapClient client);

/* Do LDAP bind.

   This will bind authentication information to the existing connected
   LDAP client. One can bind the same client multiple times.
   Binding is not neccessary for the protocol version 3. */
SshOperationHandle
ssh_ldap_client_bind(SshLdapClient client,
                     const unsigned char *bind_name, size_t bind_name_len,
                     const unsigned char *password, size_t password_len,
                     SshLdapClientResultCB callback, void *callback_context);

/* Do LDAP bind authenticating with SASL. */
SshOperationHandle
ssh_ldap_client_bind_sasl(SshLdapClient client,
                          const char *sasl_mechanism,
                          const unsigned char *bind_name,
                          size_t bind_name_len,
                          const unsigned char *credentials,
                          size_t credentials_len,
                          SshLdapClientResultCB callback,
                          void *callback_context);

/* Do LDAP unbind. This is also done automatically before disconnect,
   and after this only disconnect operation is allowed. */
void ssh_ldap_client_unbind(SshLdapClient client);

/*****************************************************************************
 * LDAP read-only operations on client, search and compare.
 */

/* Scope of the search */
typedef enum {
  SSH_LDAP_SEARCH_SCOPE_BASE_OBJECT                     = 0,
  SSH_LDAP_SEARCH_SCOPE_SINGLE_LEVEL                    = 1,
  SSH_LDAP_SEARCH_SCOPE_WHOLE_SUBTREE                   = 2
} SshLdapSearchScope;

/* Alias dereference options */
typedef enum {
  SSH_LDAP_DEREF_ALIASES_NEVER                          = 0,
  SSH_LDAP_DEREF_ALIASES_IN_SEARCHING                   = 1,
  SSH_LDAP_DEREF_ALIASES_FINDING_BASE_OBJECT            = 2,
  SSH_LDAP_DEREF_ALIASES_ALWAYS                         = 3
} SshLdapDerefAliases;

/* LDAP Attribute value assertion structure */
typedef struct SshLdapAttributeValueAssertionRec {
  unsigned char *attribute_type;
  size_t attribute_type_len;
  unsigned char *attribute_value;
  size_t attribute_value_len;
} *SshLdapAttributeValueAssertion;

/* LDAP Search filter.

   Operation field specifies the which field in then union is used.

   Notice on the usage. The preferred way to use filters on the
   application is to use the string->filter conversion routines
   instead of filling the structure directly. */

typedef struct SshLdapSearchFilterRec *SshLdapSearchFilter;

/* Convert LDAP Search filter string (as in RFC1960) to
   SshLdapSearchFilter structure which this function
   allocates. Returns TRUE if successfull, and FALSE in case of
   error. */
Boolean ssh_ldap_string_to_filter(const unsigned char *string,
                                  size_t string_len,
                                  SshLdapSearchFilter *filter);

/* Convert SshLdapSearchFilter to string, which this function
   allocates. TRUE if successfull, and FALSE in case of error.*/
Boolean ssh_ldap_filter_to_string(SshLdapSearchFilter filter,
                                  unsigned char **string,
                                  size_t *string_len);

/* Free SshLdapSearchFilter structure. */
void ssh_ldap_free_filter(SshLdapSearchFilter filter);


/* Perform a LDAP search.

   Search can be used to read attributes from single entry, from
   entries immediately below given entry, or from a subtree.

   - 'base_object' is the DN relative to which the search is performed.
   - 'scope' indicates what to return relative to base object.
   - 'deref' indicates if, and when, the server is to expand aliases.
   - 'size_limit' restricts the maximum number of objects returned.
   - 'time_limit' restricts the time used for the search.
   - 'attributes_only' indicates if result should contain values as well.
   - 'filter' that defines conditions the entry must fullfill as a match.
   - 'number_of_attributes' is the size of 'attribute_types'
      and 'attribute_type_lens' arrays.
   - 'attributes' indicate what to return from each matching entry.
      Each attribute may only appear once in the list. Special value '*'
      may be used to fetch all user attributes (unless access control
      denies these to be included).

   If 'size_limit' or 'time_limit' are negative, the value given at
   the client configuration is used. For non negative values these are
   hints for the server. The server policy may decide other values to
   be used.

   The given 'search_callback' will be called once for each matching
   object, and 'callback' once for each search made. The resulting
   object may be a referral, in which case the application should
   perform a new search, with a new client. */

SshOperationHandle
ssh_ldap_client_search(SshLdapClient client,
                       const char *base_object,
                       SshLdapSearchScope scope,
                       SshLdapDerefAliases deref,
                       SshInt32 size_limit,
                       SshInt32 time_limit,
                       Boolean attributes_only,
                       SshLdapSearchFilter filter,
                       int number_of_attributes,
                       unsigned char **attribute_types,
                       size_t *attribute_type_lens,
                       SshLdapSearchResultCB search_callback,
                       void *search_callback_context,
                       SshLdapClientResultCB callback,
                       void *callback_context);


/* Perform a LDAP compare.

   Compare an assertion provided with an entry in the directory.

   - 'object_name' is the name of entry at directory to compare against.
   - 'ava' is the attribute-value pair to compare.

   The result 'callback' will receive comparison result as its 'result'
   status. */

SshOperationHandle
ssh_ldap_client_compare(SshLdapClient client,
                        const unsigned char *object_name,
                        size_t object_name_len,
                        SshLdapAttributeValueAssertion ava,
                        SshLdapClientResultCB callback,
                        void *callback_context);


/*****************************************************************************
 * LDAP read-write operations on client, modifications (add, delete, modify)
 */

/* Modify operation codes */
typedef enum {
  SSH_LDAP_MODIFY_ADD = 0,
  SSH_LDAP_MODIFY_DELETE = 1,
  SSH_LDAP_MODIFY_REPLACE = 2
} SshLdapModifyOperation;

/* Perform a LDAP modify.

   - 'object_name' describes the DN to be modified (no dereferencing).
   - 'number_of_operations' is size of 'operations' and 'attributes' arrays.
   - 'operations' and 'attributes' describe the changes to be made in
      this order as single atomic operation (e.g individual entries may
      violate schema, but the result must be according to the scheme).

   The result 'callback' will indicate if modification was successful, and
   possible failure reasons. */

SshOperationHandle
ssh_ldap_client_modify(SshLdapClient client,
                       const unsigned char *object_name,
                       size_t object_name_len,
                       int number_of_operations,
                       SshLdapModifyOperation *operations,
                       SshLdapAttribute attributes,
                       SshLdapClientResultCB callback,
                       void *callback_context);

/* Perform a LDAP add.

   - 'object' is the object to add. It is added to location specified by
     the 'object_name' at the 'object'. The object life-time for the object
     is this call. The server will not dereference aliases while adding.
     The entry with 'object_name' must not exists while this is called.

   The result 'callback' indicates if the object was added. */

SshOperationHandle
ssh_ldap_client_add(SshLdapClient client,
                    const SshLdapObject object,
                    SshLdapClientResultCB callback,
                    void *callback_context);

/* Perform a LDAP delete.

   - 'object_name' is the name of object to delete. Aliases are not
      dereferenced for the object to be found. Also the name must specify
      a leaf entry (e.g. with no subentries).

   The result 'callback' indicates if the object was deleted. */

SshOperationHandle
ssh_ldap_client_delete(SshLdapClient client,
                       const unsigned char *object_name,
                       size_t object_name_len,
                       SshLdapClientResultCB callback,
                       void *callback_context);

/* Perform a LDAP modify RDN.

   This operation is used to change the leftmost (least significant)
   component of the name of an entry in the directory, or to move a
   subtree of entries to a new location in the directory.

   - 'object_name' identifies an existing object (whose leftmost component
      is to be changed)
   - 'new_rdn' identifies new value for the leftmost component
      of 'object_name'
   - 'delete_old_rdn' indicates if to keep old RDN as an attribute of
      the entry.

   The result 'callback' indicates if the object was renamed. */

SshOperationHandle
ssh_ldap_client_modify_rdn(SshLdapClient client,
                           const unsigned char *object_name,
                           size_t object_name_len,
                           const unsigned char *new_rdn,
                           size_t new_rdn_len,
                           Boolean delete_old_rdn,
                           SshLdapClientResultCB callback,
                           void *callback_context);

/* Perform a LDAP extension request.

   This operation sends the extension identifier 'oid' and DER coded
   'ext_data' to the server. The result callback will receive the
   server's respose in the SshLdapResultInfo's extension_name
   (contains oid) and extension_data (contains DER) fields. */

SshOperationHandle
ssh_ldap_client_extension(SshLdapClient client,
                          const char *oid,
                          unsigned char *ext_data, size_t ext_data_len,
                          SshLdapClientResultCB callback,
                          void *callback_context);

/* Perform a LDAP search using a LDAP URL.

   Search object from location given at LDAP URL at 'url'.

   In the first form, 'params' will describe parameters used for the
   clients created to perform the search. This call will automatically
   create a new LDAP client, connect and bind it.  If the 'url' does
   not specify server address this will immediately fail with
   'callback' being called with SSH_LDAP_RESULT_INTERNAL.

   If the 'url' does not specify bind name and password, an anonymous
   bind (if protocol version 2), or no bind for version 3 protocol,
   will be tried. If this fails the 'callback' will be called with
   status of SSH_LDAP_RESULT_AUTH_METHOD_NOT_SUPPORTED.

   In the second form, the 'client' is created, connected, and bound
   by the caller. The 'url' should not specify server address, or
   binding information.

   A search described at URL will be made. When search results have
   been processed (after 'callback' having been called that is), the
   temporary client created will be destroyed. If one wishes to follow
   referrals, new ssh_ldap_client_search_url() should be called with
   the referred object URL.

   LDAP URL format is as follows:

   ldap://[<user>:<pass>@]
           <host>[:<port>]/<base>[?<attrs>[?<scope>?<filter>]]  */

SshOperationHandle
ssh_ldap_search_url(SshLdapClientParams params,
                    const char *url,
                    SshLdapSearchResultCB search_callback,
                    void *search_callback_context,
                    SshLdapClientResultCB callback,
                    void *callback_context);

SshOperationHandle
ssh_ldap_client_search_url(SshLdapClient client,
                           const char *url,
                           SshLdapSearchResultCB search_callback,
                           void *search_callback_context,
                           SshLdapClientResultCB callback,
                           void *callback_context);

/* Connect and bind the client into 'server'. Call result callback
   when bind is done is done. If 'wrap_callback' is not a NULL
   pointer, It will be called right after the connect has been
   established, before bind is performed. */
SshOperationHandle
ssh_ldap_client_connect_and_bind(SshLdapClient client,
                                 const char *server, const char *port,
                                 SshLdapClientWrapCB wrap_callback,
                                 const unsigned char *bind_name,
                                 size_t bind_name_len,
                                 const unsigned char *password,
                                 size_t password_len,
                                 SshLdapClientResultCB callback,
                                 void *callback_context);

/* LDAP version 3 way to enable TLS on a connected client. This should
   be done prior to bind operation if password based authentication is
   used and the server supports TLS. This can be done at any point
   later, when there are no outstanding requests on the client.

   The TLS 'configuration' has to remain valid until the callback gets
   called, and if the callback indicates success, the contents of the
   configuration need to remain valid as long as the client is
   connected. */
SshOperationHandle
ssh_ldap_client_enable_tls(SshLdapClient client,
                           SshLdapClientWrapCB callback,
                           void *callback_context);

#endif /* SSHLDAP_H */
