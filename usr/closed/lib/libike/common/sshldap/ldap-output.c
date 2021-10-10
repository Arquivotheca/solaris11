/*
  File: ldap-output.c

  Description:
        Output data from Asn.1 encoder to the LDAP server.

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
        Helsinki, Finland.
        All rights reserved.
*/

#include "sshincludes.h"
#include "sshldap.h"
#include "ldap-internal.h"

#define SSH_DEBUG_MODULE "SshLdapOutput"

/* Send LDAP message to remote end */
SshLdapResult ssh_ldap_send_operation(SshLdapClient client,
                                      SshAsn1Context asn1context,
                                      SshAsn1Tree message)
{
  SshAsn1Status status;
  unsigned char *data;
  size_t data_len;

  if (!client->connected)
    return SSH_LDAP_RESULT_DISCONNECTED;

  status = ssh_asn1_encode(asn1context, message);
  if (status != SSH_ASN1_STATUS_OK)
    return SSH_LDAP_RESULT_INTERNAL;

  ssh_asn1_get_data(message, &data, &data_len);
  ssh_buffer_append(client->out_buffer, data, data_len);
  ssh_free(data);
  ssh_ldap_stream_callback(SSH_STREAM_CAN_OUTPUT, client);

  return SSH_LDAP_RESULT_SUCCESS;
}
