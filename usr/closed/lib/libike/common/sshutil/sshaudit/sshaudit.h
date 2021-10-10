/*
 *
 * sshaudit.h
 *
 * Author: Henri Ranki <ranki@ssh.fi>
 *         Markku Rossi <mtr@ssh.fi>
 *
 *  Copyright:
 *          Copyright (c) 2002, 2003 SFNT Finland Oy.
 *               All rights reserved.
 *
 * Audit event handling.
 *
 */

#ifndef SSHAUDIT_H
#define SSHAUDIT_H

#include "sshenum.h"
#include "sshbuffer.h"

/********************************** Types ***********************************/

/* List of auditable events. These identify the event which is being
   audited. */
typedef enum
{
  /* The following events are for IPsec engine. */

  /* An attempt to transmit a packet that would result in Sequence Number
     overflow if anti-replay is enabled. The audit log entry SHOULD include
     the SPI value, date/time, Source Address, Destination Address,
     and (in IPv6) the Flow ID. */
  SSH_AUDIT_AH_SEQUENCE_NUMBER_OVERFLOW = 1,

  /* If a packet offered to AH for processing appears to be an IP
     fragment. The audit log entry for this event SHOULD include
     the SPI value, date/time, Source Address, Destination Address,
     and (in IPv6) the Flow ID. */
  SSH_AUDIT_AH_IP_FRAGMENT = 2,

  /* When mapping the IP datagram to the appropriate SA, the SA
     lookup fails. The audit log entry for this event SHOULD include
     the SPI value, date/time, Source Address, Destination Address,
     and (in IPv6) the cleartext Flow ID. */
  SSH_AUDIT_AH_SA_LOOKUP_FAILURE = 3,

  /* If a received packet does not fall within the receivers sliding
     window, the receiver MUST discard the received IP datagram as
     invalid; The audit log entry for this event SHOULD include the
     SPI value, date/time, Source Address, Destination Address, the
     Sequence Number, and (in IPv6) the Flow ID.*/
  SSH_AUDIT_AH_SEQUENCE_NUMBER_FAILURE = 4,

  /* If the computed and received ICV's do not match, then the receiver
     MUST discard the received IP datagram as invalid.
     The audit log entry SHOULD include the SPI value, date/time
     received, Source Address, Destination Address, and (in IPv6)
     the Flow ID.*/
  SSH_AUDIT_AH_ICV_FAILURE = 5,

  /* An attempt to transmit a packet that would result in Sequence Number
     overflow if anti-replay is enabled. The audit log entry SHOULD include
     the SPI value, date/time, Source Address, Destination Address,
     and (in IPv6) the Flow ID.*/
  SSH_AUDIT_ESP_SEQUENCE_NUMBER_OVERFLOW = 6,

  /* If a packet offered to ESP for processing appears to be an IP
     fragment. The audit log entry for this event SHOULD include
     the SPI value, date/time, Source Address, Destination Address,
     and (in IPv6) the Flow ID.*/
  SSH_AUDIT_ESP_IP_FRAGMENT = 7,

  /* When mapping the IP datagram to the appropriate SA, the SA
     lookup fails. The audit log entry for this event SHOULD include
     the SPI value, date/time, Source Address, Destination Address,
     and (in IPv6) the cleartext Flow ID.*/
  SSH_AUDIT_ESP_SA_LOOKUP_FAILURE = 8,

  /* If a received packet does not fall within the receivers sliding
     window, the receiver MUST discard the received IP datagram as
     invalid; The audit log entry for this event SHOULD include the
     SPI value, date/time, Source Address, Destination Address, the
     Sequence Number, and (in IPv6) the Flow ID.*/
  SSH_AUDIT_ESP_SEQUENCE_NUMBER_FAILURE = 9,

  /* If the computed and received ICV's do not match, then the receiver
     MUST discard the received IP datagram as invalid.
     The audit log entry SHOULD include the SPI value, date/time
     received, Source Address, Destination Address, and (in IPv6)
     the Flow ID.*/
  SSH_AUDIT_ESP_ICV_FAILURE = 10,

  /* The following events are for IPsec policy manager. */

  /* The other IKE peer tried to negotiate ESP SA with both a NULL
     encryption and a NULL authentication algorithm. */
  SSH_AUDIT_PM_ESP_NULL_NULL_NEGOTIATION = 11,

  /* The following events are for ISAKMP. */

  /* The message retry limit is reached when transmitting ISAKMP
     messages.*/
  SSH_AUDIT_IKE_RETRY_LIMIT_REACHED = 12,

  /* When ISAKMP message is received and the cookie validation fails. */
  SSH_AUDIT_IKE_INVALID_COOKIE = 13,

  /* If the Version field validation fails.*/
  SSH_AUDIT_IKE_INVALID_ISAKMP_VERSION = 14,

  /* If the Exchange Type field validation fails. */
  SSH_AUDIT_IKE_INVALID_EXCHANGE_TYPE = 15,

  /* If the Flags field validation fails.*/
  SSH_AUDIT_IKE_INVALID_FLAGS = 16,

  /* If the Message ID validation fails. (not used)*/
  SSH_AUDIT_IKE_INVALID_MESSAGE_ID = 17,

  /* When any of the ISAKMP Payloads are received and if the NextPayload
     field validation fails.*/
  SSH_AUDIT_IKE_INVALID_NEXT_PAYLOAD = 18,

  /* If the value in the RESERVED field is not zero.*/
  SSH_AUDIT_IKE_INVALID_RESERVED_FIELD = 19,

  /*  If the DOI determination fails.*/
  SSH_AUDIT_IKE_INVALID_DOI = 20,

  /* If the Situation determination fails.*/
  SSH_AUDIT_IKE_INVALID_SITUATION = 21,

  /* If the Security Association Proposal is not accepted.*/
  SSH_AUDIT_IKE_INVALID_PROPOSAL = 22,

  /* If the SPI is invalid.*/
  SSH_AUDIT_IKE_INVALID_SPI = 23,

  /*If the proposals are not formed correctly.*/
  SSH_AUDIT_IKE_BAD_PROPOSAL_SYNTAX = 24,

  /* If the Transform-ID field is invalid. (not used)*/
  SSH_AUDIT_IKE_INVALID_TRANSFORM = 25,

  /* If the transforms are not formed correctly. (not used)*/
  SSH_AUDIT_IKE_INVALID_ATTRIBUTES = 26,

  /* If the Key Exchange determination fails. (not used)*/
  SSH_AUDIT_IKE_INVALID_KEY_INFORMATION = 27,

  /* If the Identification determination fails. (not used)*/
  SSH_AUDIT_IKE_INVALID_ID_INFORMATION = 28,

  /* If the Certificate Data is invalid or improperly formatted. (not
     used)*/
  SSH_AUDIT_IKE_INVALID_CERTIFICATE = 29,

  /* If the Certificate Encoding is invalid. (not used)*/
  SSH_AUDIT_IKE_INVALID_CERTIFICATE_TYPE = 30,

  /* If the Certificate Encoding is not supported. (not used)*/
  SSH_AUDIT_IKE_CERTIFICATE_TYPE_UNSUPPORTED = 31,

  /* If the Certificate Authority is invalid or improperly
     formatted. (not used)*/
  SSH_AUDIT_IKE_INVALID_CERTIFICATE_AUTHORITY = 32,

  /* If a requested Certificate Type with the specified Certificate
     Authority is not available. (not used)*/
  SSH_AUDIT_IKE_CERTIFICATE_UNAVAILABLE = 33,

  /* If the Hash determination fails. (not used)*/
  SSH_AUDIT_IKE_INVALID_HASH_INFORMATION = 34,

  /* If the Hash function fails. (not used)*/
  SSH_AUDIT_IKE_INVALID_HASH_VALUE = 35,

  /* If the Signature determination fails. (not used)*/
  SSH_AUDIT_IKE_INVALID_SIGNATURE_INFORMATION = 36,

  /* If the Signature function fails. (not used)*/
  SSH_AUDIT_IKE_INVALID_SIGNATURE_VALUE = 37,

  /* When receivers notification payload check fails. (not used)*/
  SSH_AUDIT_IKE_NOTIFICATION_PAYLOAD_RECEIVED = 38,

  /* If the Protocol-Id determination fails. */
  SSH_AUDIT_IKE_INVALID_PROTOCOL_ID = 39,

  /* If the Notify Message Type is invalid. (not used)*/
  SSH_AUDIT_IKE_INVALID_MESSAGE_TYPE = 40,

  /* If receiver detects an error in Delete Payload. */
  SSH_AUDIT_IKE_DELETE_PAYLOAD_RECEIVED = 41,

  /* If receiver detects an error in payload lengths.*/
  SSH_AUDIT_IKE_UNEQUAL_PAYLOAD_LENGTHS = 42,

  /* The following events are for packet processing engine. */

  /* Start of a new session. */
  SSH_AUDIT_ENGINE_SESSION_START = 43,

  /* End of a session. */
  SSH_AUDIT_ENGINE_SESSION_END = 44,

  /* Suspicious packet received and dropped */
  SSH_AUDIT_CORRUPT_PACKET = 46,

  /* Audit message rate limit hit */
  SSH_AUDIT_FLOOD = 47,

  /* Rule match */
  SSH_AUDIT_RULE_MATCH = 48,

  /* New configuration */
  SSH_AUDIT_NEW_CONFIGURATION = 49,

  /* Appgw session start/end */
  SSH_AUDIT_APPGW_SESSION_START = 50,
  SSH_AUDIT_APPGW_SESSION_END = 51,

  /* HTTP request */
  SSH_AUDIT_HTTP_REQUEST = 52,

  /* Protocol parse error */
  SSH_AUDIT_PROTOCOL_PARSE_ERROR = 53,

  /* Misc. warning/notify events. Please use this only if there
     is NO point in defining a separate event which is the cause
     of the warning (e.g. it would be specific to the implementation
     and not the problem domain). */
  SSH_AUDIT_WARNING = 54,
  SSH_AUDIT_NOTICE = 55,

  /* CIFS session start/end */
  SSH_AUDIT_CIFS_SESSION_START = 56,
  SSH_AUDIT_CIFS_SESSION_STOP = 57,

  /* A CIFS file operation (open, close, rename, etc..) took place */
  SSH_AUDIT_CIFS_OPERATION = 58,

  /* FTP protocol did try to change the client or server IP in a data
     channel opening. */
  SSH_AUDIT_FTP_IP_CHANGE = 59,
  
  /**********************************************************************/
  /* Secure shell auditable events. */
  /* Normal operation flow is:

     connect, login, [session_channel_open]

     and then some

     forwarding_channel_open, forwarding_channel_close,
     auth_channel_open, auth_channel_close

     session_channel_open can be received multiple times during operation.

     Filetranfer example is: session_channel_open(command = sftp-server),
     filexfer_start, filexfer_end... session_channel_close.

     closing connection is:

     [forwarding_channel_close], [session_channel_close], logout.


     Unsuccessful connection flow is:

     connect, login(error = auth_fail etc)
  */

  /* TCP/IP Connection open. */
  /* source_address, destination_address, source_port, destination_port */
  SSH_AUDIT_SECSH_CONNECT = 400,

  /* User login (successful or unsuccessful) */
  /* source_address, destination_address, source_port, destination_port,
     session_id, user, [remote_user], [error_code (if unsuccessful)]. */
  SSH_AUDIT_SECSH_LOGIN = 410,

  /* User logout */
  /* session_id, [source_address, destination_address, source_port,
     destination_port, user, remote_user], [error_code (if session ended
     because of error)] */
  SSH_AUDIT_SECSH_LOGOUT = 411,

  /* Session channel forwarding open. */
  /* session_id, command, [error_code]. */
  SSH_AUDIT_SECSH_SESSION_CHANNEL_OPEN = 420,

  /* Session channel forwarding close. */
  /* session_id, [error_code]. */
  SSH_AUDIT_SECSH_SESSION_CHANNEL_CLOSE = 421,

  /* Forwarding channel forwarding open. */
  /* session_id, [source_address, destination_address, source_port,
     destination_port], [error_code]. */
  SSH_AUDIT_SECSH_FORWARDING_CHANNEL_OPEN = 422,

  /* Forwarding channel forwarding close. */
  /* session_id, [source_address, destination_address, source_port,
     destination_port], [error_code]. */
  SSH_AUDIT_SECSH_FORWARDING_CHANNEL_CLOSE = 423,

  /* Authentication channel forwarding open. */
  /* session_id, [error_code]. */
  SSH_AUDIT_SECSH_AUTH_CHANNEL_OPEN = 424,

  /* Authentication channel forwarding close. */
  /* session_id, [error_code]. */
  SSH_AUDIT_SECSH_AUTH_CHANNEL_CLOSE = 425,

  /* File transfer begin. */
  /* session_id, sub_id, filename, total_length, [error_code]. */
  SSH_AUDIT_SECSH_FILEXFER_START = 500,

  /* File transfer end. */
  /* session_id, sub_id, filename, [data_written], [data_read], [error_code].*/
  SSH_AUDIT_SECSH_FILEXFER_END = 501,

  /* This must be the last item in the list marking the maximum
     defined audit event. */
  SSH_AUDIT_MAX_VALUE
} SshAuditEvent;

/* Mapping from SshAuditEvent to their names. */
extern const SshKeywordStruct ssh_audit_event_names[];

/* Enum types that are used when passing parameters to audit
   function. Audit function takes a variable number of
   arguments. First is the type of the auditable event
   (SshAuditEvent). Following that is listed additional information
   which is inserted to the audit log too. Additional arguments starts
   with type specified here. After that is a couple of parameters
   depending on the type of argument. Needed parameters is commented
   here. List must always end with a SSH_AUDIT_ARGUMENT_END. */
typedef enum
{
  /* Contains the SPI for the packet which caused an auditable event,
     for IKE this is the initiator and responder cookies. If the
     length is zero then this value is ignored. */
  SSH_AUDIT_SPI = 1,                            /* unsigned char *, size_t */

  /* Contains the source address for the packet which caused the
     auditable event, for IKE this is local ip address. If the length
     is zero then this value is ignored.*/
  SSH_AUDIT_SOURCE_ADDRESS = 2,                 /* unsigned char *, size_t */

  /* Contains the destination address for the packet which caused the
     auditable event, for IKE this is remote ip address. If the length
     is zero then this value is ignored.*/
  SSH_AUDIT_DESTINATION_ADDRESS = 3,            /* unsigned char *, size_t */

  /* Contains the source address for the packet which caused the
     auditable event, for IKE this is local ip address. If the pointer
     is NULL then this value is ignored. This contains the source
     address in text format. */
  SSH_AUDIT_SOURCE_ADDRESS_STR = 4,             /* unsigned char * */

  /* Contains the destination address for the packet which caused the
     auditable event, for IKE this is remote ip address. If the
     pointer is NULL then this value is ignored. This contains the
     destination address in text format. */
  SSH_AUDIT_DESTINATION_ADDRESS_STR = 5,        /* unsigned char * */

  /* Contains the Flow ID for the packet which caused the auditable
     event. This conserns only IPv6 addresses. If the length is zero
     then this value is ignored.*/
  SSH_AUDIT_IPV6_FLOW_ID = 6,                   /* unsigned char *, size_t */

  /* Contains the sequence number for the packet which caused the
     auditable event. If the length is zero then this value is
     ignored. */
  SSH_AUDIT_SEQUENCE_NUMBER = 7,                /* unsigned char *, size_t */

  /* Describing text for the event. If the pointer is NULL then this
     value is ignored. */
  SSH_AUDIT_TXT = 8,                            /* unsigned char * */

  /* IP protocol ID. */
  SSH_AUDIT_IPPROTO = 9,                        /* unsigned char *, size_t */

  /* Source port number. */
  SSH_AUDIT_SOURCE_PORT = 10,                   /* unsigned char *, size_t */

  /* Destination port number. */
  SSH_AUDIT_DESTINATION_PORT = 11,              /* unsigned char *, size_t */

  /* Reason packet is corrupted */
  SSH_AUDIT_PACKET_CORRUPTION = 12,             /* unsigned char* */

  /* Packet attack id */
  SSH_AUDIT_PACKET_ATTACK = 13,                 /* unsigned char * */

  /* Source interface name */
  SSH_AUDIT_SOURCE_INTERFACE = 14,              /* unsigned char  */

  /* Destination interface name */
  SSH_AUDIT_DESTINATION_INTERFACE = 15,         /* unsigned char * */

  /* IPv4 option name */
  SSH_AUDIT_IPV4_OPTION = 16,                   /* unsigned char *, size_t * */

  /* ICMP type and code */
  SSH_AUDIT_ICMP_TYPECODE = 17,                 /* unsigned char *, size_t * */

  /* IPv6 ICMP type and code */
  SSH_AUDIT_IPV6ICMP_TYPECODE = 18,             /* unsigned char *, size_t * */

  /* TCP flags */
  SSH_AUDIT_TCP_FLAGS = 19,                     /* unsigned char *, size_t * */

  /* Audit event source */
  SSH_AUDIT_EVENT_SOURCE = 20,                  /* unsigned char * */

  /* HTTP method */
  SSH_AUDIT_HTTP_METHOD = 21,                   /* unsigned char * */

  /* Request URL/URI */
  SSH_AUDIT_REQUEST_URI = 22,                   /* unsigned char * */

  /* HTTP version */
  SSH_AUDIT_HTTP_VERSION = 23,                  /* unsigned char * */

  /* A rule identifier */
  SSH_AUDIT_RULE_NAME = 24,                     /* unsigned char * */

  /* A rule action */
  SSH_AUDIT_RULE_ACTION = 25,                   /* unsigned char * */

  /* Destination and source hosts */
  SSH_AUDIT_SOURCE_HOST = 26,                   /* unsigned char * */
  SSH_AUDIT_DESTINATION_HOST = 27,              /* unsigned char * */

  /* CIFS domain. CIFS username. CIFS command. */
  SSH_AUDIT_CIFS_DOMAIN = 28,                  /* unsigned char * */
  SSH_AUDIT_CIFS_ACCOUNT = 29,                 /* unsigned char * */
  SSH_AUDIT_CIFS_COMMAND = 30,                 /* unsigned char * */
  SSH_AUDIT_CIFS_DIALECT = 31,                 /* unsigned char * */

  /* Key length in bits */
  SSH_AUDIT_KEY_LENGTH = 32,                   /* unsigned char *, size_t */

  /* NetBIOS source/destination names */
  SSH_AUDIT_NBT_SOURCE_HOST = 33,              /* unsigned char * */
  SSH_AUDIT_NBT_DESTINATION_HOST = 34,         /* unsigned char * */

  /* CIFS subcommand */
  SSH_AUDIT_CIFS_SUBCOMMAND = 35,              /* unsigned char * */

  /* FTP command. */
  SSH_AUDIT_FTP_COMMAND = 36,                  /* unsigned char *, size_t */

  /* SOCKS parameters */
  SSH_AUDIT_SOCKS_VERSION = 37,                /* unsigned char *, size_t */
  SSH_AUDIT_SOCKS_SERVER_IP = 38,              /* unsigned char *, size_t */
  SSH_AUDIT_SOCKS_SERVER_PORT = 39,

  /* Generic username */
  SSH_AUDIT_USERNAME = 40,                     /* unsigned char * */

  /* Target IP/Host for various operations. Not the actual TCP connection. */
  SSH_AUDIT_TARGET_IP = 41,
  SSH_AUDIT_TARGET_PORT = 42,

  /* Information about data transmitted. */
  SSH_AUDIT_TRANSMIT_BYTES = 43,               /* unsigned char *, size_t */
  SSH_AUDIT_TRANSMIT_DIGEST = 44,              /* unsigned char *, size_t */

  /* Local and remote username */
  SSH_AUDIT_USER = 50,                          /* unsigned char * */
  SSH_AUDIT_REMOTE_USER = 51,                   /* unsigned char * */

  /* Session identifier */
  SSH_AUDIT_SESSION_ID = 52,                    /* unsigned char *, size_t */

  /* Session sub identifier (channel id, file handle id etc) */
  SSH_AUDIT_SUB_ID = 53,                        /* unsigned char *, size_t */

  /* Error code (remote error code, local error code etc) */
  SSH_AUDIT_ERROR_CODE = 54,                    /* unsigned char *, size_t */

  /* File name */
  SSH_AUDIT_FILE_NAME = 55,                     /* unsigned char *, size_t */

  /* Command */
  SSH_AUDIT_COMMAND = 56,                       /* unsigned char *, size_t */

  /* Length of data */
  SSH_AUDIT_TOTAL_LENGTH = 57,                  /* unsigned char *, size_t */

  /* Length of data written */
  SSH_AUDIT_DATA_WRITTEN = 58,                  /* unsigned char *, size_t */

  /* Length of data read */
  SSH_AUDIT_DATA_READ = 59,                     /* unsigned char *, size_t */

  /* Quicksec tunnel id as an integer (as provided by the engine) */
  SSH_AUDIT_TOTUNNEL_ID = 60,

  /* Quicksec tunnel id as a readable string (as provided by PM) */
  SSH_AUDIT_FROMTUNNEL_ID = 61,

  /* Marks end of the argument list. */
  SSH_AUDIT_ARGUMENT_END = -1
} SshAuditArgumentType;

/* An audit argument. */
struct SshAuditArgumentRec
{
  /* The type of the argument */
  SshAuditArgumentType type;

  /* Argument value and its length.  The field `data_len' is always
     valid, also for arguments taking a null-terminated string.  For
     those strings, it holds the strlen() of the value. */
  unsigned char *data;
  size_t data_len;
};

typedef struct SshAuditArgumentRec SshAuditArgumentStruct;
typedef struct SshAuditArgumentRec *SshAuditArgument;

/* A callback function of this type is called when an audit event is
   logged from the system.  The argument `event' specifies the audit
   event.  The argument `argc' specifies the number of argument the
   event has.  The argument `argv' is an array containing the
   arguments.  The values, pointed by the fields in the `argv' array,
   remain valid as long as the control remains in the callback
   function. */
typedef void (*SshAuditCB)(SshAuditEvent event, SshUInt32 argc,
                           SshAuditArgument argv, void *context);

/* Context structure which is created with the ssh_audit_create
   function. */
typedef struct SshAuditContextRec *SshAuditContext;


/****************** Creating and destroying audit contexts ******************/

/* Creates an audit context.  The argument `callback' is a callback
   function that will be called when an audit event is logged with the
   ssh_audit_event function.  The function returns NULL if the
   operation fails. */
SshAuditContext ssh_audit_create(SshAuditCB callback, void *context);

/* Destroys audit context `context'. */
void ssh_audit_destroy(SshAuditContext context);


/************************** Handling audit events ***************************/

/* Inserts specified event into log file.  The event parameter
   specifies the audited event. Each element after that must start
   with a SshAuditformat type, followed by arguments of the
   appropriate type, and the list must end with
   SSH_AUDIT_FORMAT_END. If context is NULL then this call is
   ignored. */
void ssh_audit_event(SshAuditContext context, SshAuditEvent event, ...);

void ssh_audit_event_va(SshAuditContext context,
                        SshAuditEvent event,
                        va_list va);

/* Like the ssh_audit_event function but the event arguments are given
   as an array `argv' having `argc' elements.

   If the `data_len' field of an argument in the `argv' array has the
   value 0 and the argument value is a null-terminated string, the
   function will set the `data_len' field to correct value.  For other
   argument value types, the `data_len' field must have valid
   value. */
void ssh_audit_event_array(SshAuditContext context, SshAuditEvent event,
                           SshUInt32 argc, SshAuditArgument argv);

/* Enables audit event `event'.  As a default all events are enabled
   and they will make a call to the audit context's SshAuditCB
   callback. */
void ssh_audit_event_enable(SshAuditContext context, SshAuditEvent event);

/* Disables audit event `event'.  The audit system will ignore all
   disabled events; it will not call the SshAuditCB for disabled
   events. */
void ssh_audit_event_disable(SshAuditContext context, SshAuditEvent event);

/* Queries the state of the audit event `event'.  The function returns
   TRUE if the event is enabled and FALSE otherwise. */
Boolean ssh_audit_event_query(SshAuditContext context, SshAuditEvent event);


/**************** Help functions for formatting audit events ****************/

/* Format audit event `event' with the `argc' number of arguments in
   `argv' into a human readable string to the buffer `buffer'.  The
   function returns TRUE if the event was formatted and FALSE if the
   system ran out of memory. */
Boolean ssh_audit_format(SshBuffer buffer, SshAuditEvent event,
                         SshUInt32 argc, SshAuditArgument argv);

#endif /* not SSHAUDIT_H */
