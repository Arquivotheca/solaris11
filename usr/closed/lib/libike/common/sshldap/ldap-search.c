/*
  File: ldap-search.c

  Description:
        Initiate LDAP search request and process the response objects.

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

#define SSH_DEBUG_MODULE "SshLdapSearch"

/* Parse search response from 'result', and allocate object for the
   result callback. */
void ssh_ldap_process_search_response(SshLdapClient client,
                                      SshAsn1Context asn1context,
                                      SshAsn1Node result,
                                      SshLdapClientOperation operation)
{
  SshLdapObject object;
  SshAsn1Status status;
  SshAsn1Node attributes = NULL, firstattribute, values, firstvalue;
  SshLdapAttribute attr;
  Boolean attributes_found;
  size_t nthvalue, nthattr;

  if (!operation->search_cb)
    return;

  if ((object = ssh_malloc(sizeof(struct SshLdapObjectRec))) != NULL)
    {
      if ((status =
           ssh_asn1_read_node(asn1context, result,
                              "(octet-string ())"
                              "(sequence () (optional (any ())))",
                              &object->object_name, &object->object_name_len,
                              &attributes_found, &attributes))
          != SSH_ASN1_STATUS_OK)
        {
          ssh_free(object);
          return;
        }

      /* Then count attributes. */
      for (firstattribute = attributes, object->number_of_attributes = 0;
           attributes;
           attributes = ssh_asn1_node_next(attributes))
        object->number_of_attributes += 1;
      attributes = firstattribute;

      if ((object->attributes =
           ssh_calloc(object->number_of_attributes,
                      sizeof(struct SshLdapAttributeRec))) == NULL)
        {
          ssh_free(object);
          return;
        }

      for (nthattr = 0;
           attributes;
           attributes = ssh_asn1_node_next(attributes), nthattr++)
        {
          attr = &(object->attributes[nthattr]);

          if ((status =
               ssh_asn1_read_node(asn1context, attributes,
                                  "(sequence ()"
                                  " (octet-string ())"
                                  " (set () (any ())))",
                                  &attr->attribute_type,
                                  &attr->attribute_type_len,
                                  &values))
              != SSH_ASN1_STATUS_OK)
            {
              ssh_ldap_free_object(object);
              return;
            }

          /* Count values. */
          for (firstvalue = values, attr->number_of_values = 0;
               values;
               values = ssh_asn1_node_next(values))
            attr->number_of_values += 1;
          values = firstvalue;

          attr->values =
            ssh_calloc(attr->number_of_values, sizeof(char *));
          attr->value_lens =
            ssh_calloc(attr->number_of_values, sizeof(size_t));
          if (attr->values == NULL || attr->value_lens == NULL)
            {
              ssh_ldap_free_object(object);
              return;
            }

          for (nthvalue = 0;
               values;
               values = ssh_asn1_node_next(values), nthvalue += 1)
            {
              if ((status =
                   ssh_asn1_read_node(asn1context, values,
                                      "(octet-string ())", /* Value */
                                      &attr->values[nthvalue],
                                      &attr->value_lens[nthvalue]))
                  != SSH_ASN1_STATUS_OK)
                {
                  ssh_ldap_free_object(object);
                  attr->number_of_values -= 1;
                }
            }
        }
    }

  (*operation->search_cb)(client, object, operation->search_cb_context);
}

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
                       void *callback_context)
{
  SshLdapClientOperation op;
  SshAsn1Context asn1context;
  SshAsn1Status status;
  SshAsn1Tree message;
  SshAsn1Node filter_asn1, attributes_asn1, node;
  int i;
  SshLdapResultInfoStruct info;
  SshLdapResult lresult;

  memset(&info, 0, sizeof(info));

  SSH_DEBUG(SSH_D_HIGHSTART,
            ("search(0x%x) base=%s", client, base_object));

  START(client,
        SSH_LDAP_OPERATION_SEARCH_REQUEST,
        callback, callback_context, info,
        op, asn1context);

  op->search_cb = search_callback;
  op->search_cb_context = search_callback_context;

  attributes_asn1 = NULL;
  for (i = 0; i < number_of_attributes; i++)
    {
      if ((status =
           ssh_asn1_create_node(asn1context, &node,
                                "(octet-string ())",
                                attribute_types[i], attribute_type_lens[i]))
          != SSH_ASN1_STATUS_OK)
        {
          MAKEINFO(&info, "Can't create Asn.1 encoding for attributes.");
          ssh_ldap_result(client, op, SSH_LDAP_RESULT_INTERNAL, &info);
          ssh_asn1_free(asn1context);
          return NULL;
        }
      attributes_asn1 = ssh_asn1_add_list(attributes_asn1, node);
    }

  filter_asn1 = NULL;
  if (filter &&
      (filter_asn1 = ssh_ldap_create_filter(asn1context, filter)) == NULL)
    {
      MAKEINFO(&info, "Can't create Asn.1 encoding for filter.");
      ssh_ldap_result(client, op, SSH_LDAP_RESULT_INTERNAL, &info);
      ssh_asn1_free(asn1context);
      return NULL;
    }

  if ((status =
       ssh_asn1_create_tree(asn1context, &message,
                            "(sequence ()"
                            " (integer-short ())" /* Message id */
                            " (sequence (a 3)" /* Search */
                            "  (octet-string ())" /* Base Object */
                            "  (enum-short ())" /* Scope */
                            "  (enum-short ())" /* Deref Aliases */
                            "  (integer-short ())" /* Size Limit */
                            "  (integer-short ())" /* Time Limit */
                            "  (boolean ())" /* Attributes Only */
                            "  (any ())" /* Filter */
                            "  (sequence ()" /* Sequence of Attributes */
                            "   (any ()))))", /* Attributes */
                            (SshWord) op->id,
                            base_object, strlen(base_object),
                            (SshWord) scope,
                            (SshWord) deref,
                            (SshWord) ((size_limit < 0) ?
                                       client->size_limit :
                                       size_limit),
                            (SshWord) ((time_limit < 0) ?
                                       client->time_limit :
                                       time_limit),
                            attributes_only,
                            filter_asn1,
                            attributes_asn1))
      != SSH_ASN1_STATUS_OK)
    {
      MAKEINFO(&info, "Can't create Asn.1 encoding for request.");
      ssh_ldap_result(client, op, SSH_LDAP_RESULT_INTERNAL, &info);
      ssh_asn1_free(asn1context);
      return NULL;
    }

  /* Send search request */
  lresult = ssh_ldap_send_operation(client, asn1context, message);
  if (lresult != SSH_LDAP_RESULT_SUCCESS)
    {
      MAKEINFO(&info, "Can't send request.");
      ssh_ldap_result(client, op, lresult, &info);
      ssh_asn1_free(asn1context);
      return NULL;
    }
  ssh_asn1_free(asn1context);
  return op->operation;
}
