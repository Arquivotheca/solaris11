/*
  cmi-debug.c

  Copyright:
        Copyright (c) 2002, 2003 SFNT Finland Oy.
	All rights reserved.

  Validator debug output routines.
*/

#include "sshincludes.h"
#include "cmi.h"
#include "cmi-internal.h"
#include "sshbuffer.h"
#include "oid.h"
#include "sshgetput.h"
#include "sshfingerprint.h"

#define SSH_DEBUG_MODULE "SshCertCMi"

static void
ssh_cm_names_dump(SshBuffer buffer, SshX509Name names);

/* Debug stuff. */
void ssh_buffer_append_str(SshBuffer buffer, char *str)
{
  ssh_buffer_append_cstrs(buffer, str, NULL);
}

char *ssh_buffer_make_str(SshBuffer buffer)
{
  return ssh_memdup(ssh_buffer_ptr(buffer), ssh_buffer_len(buffer));
}

const SshKeywordStruct ssh_cm_debug_state_strs[] =
{
  { "certificate-algorithm-mismatch" ,SSH_CM_SSTATE_CERT_ALG_MISMATCH },
  { "certificate-key-usage-mismatch" ,SSH_CM_SSTATE_CERT_KEY_USAGE_MISMATCH },
  { "certificate-not-in-time-interval" ,SSH_CM_SSTATE_CERT_NOT_IN_INTERVAL },
  { "certificate-was-invalid" ,SSH_CM_SSTATE_CERT_INVALID },
  { "certificate-signature-invalid" ,SSH_CM_SSTATE_CERT_INVALID_SIGNATURE },
  { "certificate-was-revoked" ,SSH_CM_SSTATE_CERT_REVOKED },
  { "certificate-was-not-added-to-the-cache",SSH_CM_SSTATE_CERT_NOT_ADDED },
  { "certificate-decoding-failed" ,SSH_CM_SSTATE_CERT_DECODE_FAILED },
  { "certificate-was-not-found" ,SSH_CM_SSTATE_CERT_NOT_FOUND },
  { "certificate-chain-looped" ,SSH_CM_SSTATE_CERT_CHAIN_LOOP },
  { "certificate-critical-extension" ,SSH_CM_SSTATE_CERT_CRITICAL_EXT },
  { "certificate-ca-invalid" ,SSH_CM_SSTATE_CERT_CA_INVALID },
  { "crl-was-too-old" ,SSH_CM_SSTATE_CRL_OLD },
  { "crl-was-invalid" ,SSH_CM_SSTATE_CRL_INVALID },
  { "crl-signature-invalid" ,SSH_CM_SSTATE_CRL_INVALID_SIGNATURE },
  { "crl-was-not-found" ,SSH_CM_SSTATE_CRL_NOT_FOUND },
  { "crl-was-not-added-to-the-cache" ,SSH_CM_SSTATE_CRL_NOT_ADDED },
  { "crl-decoding-failed" ,SSH_CM_SSTATE_CRL_DECODE_FAILED },
  { "crl-valid-only-in-future" ,SSH_CM_SSTATE_CRL_IN_FUTURE },
  { "crl-duplicate-serial-number" ,SSH_CM_SSTATE_CRL_DUPLICATE_SERIAL_NO },
  { "time-interval-was-invalid" ,SSH_CM_SSTATE_INTERVAL_NOT_VALID },
  { "time-information-unavailable" ,SSH_CM_SSTATE_TIMES_UNAVAILABLE },
  { "database-method-search-timeout" ,SSH_CM_SSTATE_DB_METHOD_TIMEOUT },
  { "database-method-search-failed" ,SSH_CM_SSTATE_DB_METHOD_FAILED },
  { "path-was-not-verified" ,SSH_CM_SSTATE_PATH_NOT_VERIFIED },
  { "maximum-path-length-reached" ,SSH_CM_SSTATE_PATH_LENGTH_REACHED },
  { NULL },
};


SshCMStatus ssh_cm_sanity_check(SshCMContext cm)
{
#ifdef DEBUG_LIGHT
  ssh_certdb_sanity_check_dump(cm->db);
#endif /* DEBUG_LIGHT */
  return SSH_CM_STATUS_OK;
}

const SshKeywordStruct ssh_cm_edb_data_types[] = {
  { "user cert" ,SSH_CM_DATA_TYPE_CERTIFICATE },
  { "CRL" ,SSH_CM_DATA_TYPE_CRL },
  { "ca cert" ,SSH_CM_DATA_TYPE_CA_CERTIFICATE },
  { "ARL" ,SSH_CM_DATA_TYPE_AUTH_CRL },
  { NULL },
};

const SshKeywordStruct ssh_cm_edb_key_types[] = {
  { "ID", SSH_CM_KEY_TYPE_IDNUMBER },
  { "BER hash", SSH_CM_KEY_TYPE_BER_HASH },
  { "DirName", SSH_CM_KEY_TYPE_DIRNAME },
  { "DisName", SSH_CM_KEY_TYPE_DISNAME },
  { "IP", SSH_CM_KEY_TYPE_IP },
  { "DNS", SSH_CM_KEY_TYPE_DNS },
  { "URI", SSH_CM_KEY_TYPE_URI },
  { "X.400", SSH_CM_KEY_TYPE_X400 },
  { "Serial", SSH_CM_KEY_TYPE_SERIAL_NO },
  { "UID", SSH_CM_KEY_TYPE_UNIQUE_ID },
  { "Email", SSH_CM_KEY_TYPE_RFC822 },
  { "Other", SSH_CM_KEY_TYPE_OTHER },
  { "RID", SSH_CM_KEY_TYPE_RID },
  { "KID", SSH_CM_KEY_TYPE_PUBLIC_KEY_ID },
  { "SerialIssuer hash", SSH_CM_KEY_TYPE_SI_HASH },
  { "UI", SSH_CM_KEY_TYPE_X509_KEY_IDENTIFIER },
  { NULL },
};

static int
cm_edb_key_render(char *buf, int buf_size, int precision, void *datum)
{
  int i, so_far = 0;
  unsigned char *key = datum;

  for (i = 0; i < precision && so_far < buf_size; i++)
    {
      if (ssh_snprintf(buf + so_far, buf_size - so_far, "%02x", key[i]) < 0)
        break;
      so_far += 2;
    }
  return so_far;
}

int
ssh_cm_edb_distinguisher_render(char *buf, int buf_size, int precision,
                                void *datum)
{
  int nbytes;
  SshCMDBDistinguisher *d = datum;
  char tmp[256], *ldap;
  SshDNStruct dn;

  switch (d->key_type)
    {
    case SSH_CM_KEY_TYPE_DNS:
    case SSH_CM_KEY_TYPE_URI:
    case SSH_CM_KEY_TYPE_X400:
    case SSH_CM_KEY_TYPE_RFC822:
    case SSH_CM_KEY_TYPE_UNIQUE_ID:
      ssh_snprintf(tmp, sizeof(tmp), "%s", d->key);
      break;

    case SSH_CM_KEY_TYPE_DIRNAME:
    case SSH_CM_KEY_TYPE_DISNAME:
      ssh_dn_init(&dn);
      if (ssh_dn_decode_der(d->key, d->key_length, &dn, NULL))
        {
          if (ssh_dn_encode_ldap(&dn, &ldap))
            {
              strncpy(tmp, ldap, sizeof(tmp));
              ssh_free(ldap);
            }
        }
      ssh_dn_clear(&dn);
      break;

    case SSH_CM_KEY_TYPE_RID:
    case SSH_CM_KEY_TYPE_OTHER:
    case SSH_CM_KEY_TYPE_SERIAL_NO:
    case SSH_CM_KEY_TYPE_BER_HASH:
    case SSH_CM_KEY_TYPE_IDNUMBER:
    case SSH_CM_KEY_TYPE_PUBLIC_KEY_ID:
    case SSH_CM_KEY_TYPE_SI_HASH:
    case SSH_CM_KEY_TYPE_X509_KEY_IDENTIFIER:
      ssh_snprintf(tmp, sizeof(tmp), "%.*@",
                   d->key_length, cm_edb_key_render, d->key);
      break;
    default:
      tmp[0] = '\0';
      break;
    };

  if ((nbytes =
       ssh_snprintf(buf, buf_size, "%s by %s[%s]",
                    ssh_find_keyword_name(ssh_cm_edb_data_types, d->data_type),
                    ssh_find_keyword_name(ssh_cm_edb_key_types, d->key_type),
                    tmp)) == -1)
    return buf_size + 1;
  else
    return nbytes;
}

static void
ssh_cm_names_dump(SshBuffer buffer, SshX509Name names)
{
  char *name;
  char tmp_str[512];
  unsigned char *buf;
  size_t buf_len;

  while (ssh_x509_name_pop_ip(names, &buf, &buf_len))
    {
      if (buf_len == 4)
        ssh_snprintf(tmp_str, sizeof(tmp_str), "%d.%d.%d.%d",
                     (int)buf[0], (int)buf[1], (int)buf[2],
                     (int)buf[3]);
      else
        {
          size_t len;
          int i;

          len = 0;
          for (i = 0; i < buf_len; i++)
            {
              ssh_snprintf(tmp_str + len, sizeof(tmp_str) - len,
                           "%02x", buf[i]);
              len += strlen(tmp_str + len);
              if (i != buf_len - 1 && (i & 0x1) == 1)
                {
                  ssh_snprintf(tmp_str + len, sizeof(tmp_str) - len, ":");
                  len++;
                }
            }
        }
      ssh_buffer_append_str(buffer, "    ip = ");
      ssh_buffer_append_str(buffer, tmp_str);
      ssh_buffer_append_str(buffer, "\n");
      ssh_free(buf);
    }

  while (ssh_x509_name_pop_dns(names, &name))
    {
      ssh_buffer_append_str(buffer, "    dns = ");
      ssh_buffer_append_str(buffer, name);
      ssh_buffer_append_str(buffer, "\n");
      ssh_free(name);
    }

  while (ssh_x509_name_pop_uri(names, &name))
    {
      ssh_buffer_append_str(buffer, "    uri = ");
      ssh_buffer_append_str(buffer, name);
      ssh_buffer_append_str(buffer, "\n");
      ssh_free(name);
    }

  while (ssh_x509_name_pop_email(names, &name))
    {
      ssh_buffer_append_str(buffer, "    email = ");
      ssh_buffer_append_str(buffer, name);
      ssh_buffer_append_str(buffer, "\n");
      ssh_free(name);
    }

  while (ssh_x509_name_pop_rid(names, &name))
    {
      ssh_buffer_append_str(buffer, "    rid = ");
      ssh_buffer_append_str(buffer, name);
      ssh_buffer_append_str(buffer, "\n");
      ssh_free(name);
    }

  while (ssh_x509_name_pop_directory_name(names, &name))
    {
      ssh_buffer_append_str(buffer, "    directory-name = <");
      ssh_buffer_append_str(buffer, name);
      ssh_buffer_append_str(buffer, ">\n");
      ssh_free(name);
    }
}

static int
cm_debug_renderer_return(SshBuffer buffer, char *buf, int len)
{
  int l = ssh_buffer_len(buffer);

  if (l > len)
    {
      strncpy(buf, (char *)ssh_buffer_ptr(buffer), len - 1);
      ssh_buffer_uninit(buffer);
      return len + 1;
    }
  else
    {
      strncpy(buf, (char *)ssh_buffer_ptr(buffer), l);
      ssh_buffer_uninit(buffer);
      return l;
    }
}


int
ssh_cm_render_crl(char *buf, int len, int precision, void *datum)
{
  SshX509Crl crl = datum;
  char *name;
  SshBerTimeStruct this_update, next_update;
  SshBufferStruct buffer;

  if (crl)
    {
      ssh_buffer_init(&buffer);

      ssh_buffer_append_str(&buffer, "\ncrl = { \n");
      if (!ssh_x509_crl_get_issuer_name(crl, &name))
        {
          ssh_buffer_append_str(&buffer, "  missing-issuer-name\n");
        }
      else
        {
          ssh_buffer_append_cstrs(&buffer,
                                  "  issuer-name = <", name, ">\n", NULL);
          ssh_free(name);
        }

      if (!ssh_x509_crl_get_update_times(crl, &this_update, &next_update))
        {
          ssh_buffer_append_str(&buffer, "  missing-update-times\n");
        }
      else
        {
          if (ssh_ber_time_available(&this_update))
            {
              ssh_ber_time_to_string(&this_update, &name);
              ssh_buffer_append_cstrs(&buffer,
                                      "  this-update = ", name, "\n", NULL);
              ssh_free(name);
            }
          if (ssh_ber_time_available(&next_update))
            {
              ssh_ber_time_to_string(&next_update, &name);
              ssh_buffer_append_cstrs(&buffer,
                                      "  next-update = ", name, "\n", NULL);
              ssh_free(name);
            }
        }

      /* Finished. */
      ssh_buffer_append_str(&buffer, "}\n");
      return cm_debug_renderer_return(&buffer, buf, len);
    }
  return 0;
}

int
ssh_cm_render_mp(char *buf, int len, int precision, void *datum)
{
  SshMPInteger mpint;
  char *tmp;
  SshBufferStruct buffer;

  mpint = datum;
  if ((tmp = ssh_mprz_get_str_compat(NULL, 10, mpint)) != NULL)
    {
      ssh_buffer_init(&buffer);
      ssh_buffer_append_str(&buffer, tmp);
      ssh_free(tmp);
      return cm_debug_renderer_return(&buffer, buf, len);
    }
  return 0;
}

int
ssh_cm_render_state(char *buf, int len, int precision, void *datum)
{
  SshCMSearchState state = *(unsigned int *)&datum;
  int i;
  const char *name;
  SshBufferStruct buffer;

  ssh_buffer_init(&buffer);
  ssh_buffer_append_str(&buffer, "\nsearch-state = \n{\n");
  if (state == 0)
    ssh_buffer_append_str(&buffer, "  nil\n");
  else
    {
      for (i = 0; i < 32; i++)
        {
          if (state & (1 << i))
            {
              name =
                ssh_find_keyword_name(ssh_cm_debug_state_strs, (1 << i));
              ssh_buffer_append_cstrs(&buffer, "  ", name, "\n", NULL);
            }
        }
    }
  ssh_buffer_append_str(&buffer, "}\n");
  return cm_debug_renderer_return(&buffer, buf, len);
}

int
ssh_cm_render_certificate(char *buf, int len, int precision, void *datum)
{

  SshX509Certificate cert = datum;
  char *name, t[128];
  SshX509Name names;
  SshMPIntegerStruct mp;
  SshBerTimeStruct not_before, not_after;
  SshBufferStruct buffer;
  SshX509OidList oid_list;
  const SshOidStruct *oids;
  Boolean critical;
  SshStr str;
  size_t l, kid_len;
  SshPublicKey pub;
  unsigned char *kid;

  if (cert)
    {
      ssh_buffer_init(&buffer);
      ssh_buffer_append_str(&buffer, "\ncertificate = { \n");

      /* Add the serial number. */
      ssh_mprz_init(&mp);
      if (ssh_x509_cert_get_serial_number(cert, &mp) == FALSE)
        {
          ssh_buffer_append_str(&buffer, "  missing-serial-number\n");
        }
      else
        {
          ssh_mprz_get_str_compat(t, 10, &mp);
          ssh_buffer_append_cstrs(&buffer,
                                  "  serial-number = ", t, "\n", NULL);
          ssh_mprz_clear(&mp);
        }

      /* Add suitable names. */
      ssh_x509_name_reset(cert->subject_name);
      if (!ssh_x509_cert_get_subject_name_str(cert, &str))
        {
          ssh_buffer_append_str(&buffer, "  missing-subject-name\n");
        }
      else
        {
          SshStr latin1 = ssh_str_charset_convert(str,SSH_CHARSET_ISO_8859_1);

          name = (char *)ssh_str_get(latin1, &l);
          ssh_buffer_append_cstrs(&buffer,
                                  "  subject-name = <", name, ">\n", NULL);
          ssh_str_free(latin1);
          ssh_free(name);
          ssh_str_free(str);
        }
      ssh_x509_name_reset(cert->issuer_name);
      if (!ssh_x509_cert_get_issuer_name(cert, &name))
        {
          ssh_buffer_append_str(&buffer, "  missing-issuer-name\n");
        }
      else
        {
          ssh_buffer_append_cstrs(&buffer,
                                  "  issuer-name = <", name, ">\n", NULL);
          ssh_free(name);
        }

      /* Validity period. */
      if (!ssh_x509_cert_get_validity(cert, &not_before, &not_after))
        {
          ssh_buffer_append_str(&buffer, "  missing-validity-period\n");
        }
      else
        {
          if (ssh_ber_time_available(&not_before))
            {
              ssh_snprintf(t, sizeof(t),
                           "%@", ssh_ber_time_render, &not_before);
              ssh_buffer_append_cstrs(&buffer,
                                      "  not-before = ", t, "\n", NULL);
            }
          if (ssh_ber_time_available(&not_after))
            {
              ssh_snprintf(t, sizeof(t),
                           "%@", ssh_ber_time_render, &not_after);
              ssh_buffer_append_cstrs(&buffer,
                                      "  not-after = ", t, "\n", NULL);
            }
        }

      if (ssh_x509_cert_get_subject_key_id(cert, &kid, &kid_len, &critical))
        {
          unsigned char *fingerprint;

          if ((fingerprint =
               ssh_fingerprint(kid, kid_len,  SSH_FINGERPRINT_HEX_UPPER))
              != NULL)
            ssh_buffer_append_cstrs(&buffer,
                                    "  subject-kid = ",
                                    fingerprint, "\n", NULL);
          ssh_free(fingerprint);
        }
      if (ssh_x509_cert_get_public_key(cert, &pub))
        {
          unsigned char *key_digest;
          size_t digest_len;

          if (ssh_cm_key_kid_create(pub, &key_digest, &digest_len))
            {
              unsigned char *fingerprint;

              fingerprint = ssh_fingerprint(key_digest,
                                            digest_len,
                                            SSH_FINGERPRINT_HEX_UPPER);
              if (fingerprint)
                {
                  ssh_buffer_append_cstrs(&buffer,
                                          "  pubkey-hash = ",
                                          fingerprint, "\n", NULL);

                }
              ssh_free(fingerprint);
              ssh_free(key_digest);
            }
          ssh_public_key_free(pub);
        }

      /* Some alternate names. */
      if (ssh_x509_cert_get_subject_alternative_names(cert, &names, &critical))
        {
          ssh_x509_name_reset(names);
          ssh_buffer_append_str(&buffer, "  subject-alt-names = { \n");
          ssh_cm_names_dump(&buffer, names);
          ssh_buffer_append_str(&buffer, "  }\n");
        }

      if (ssh_x509_cert_get_issuer_alternative_names(cert, &names, &critical))
        {
          ssh_x509_name_reset(names);
          ssh_buffer_append_str(&buffer, "  issuer-alt-names = { \n");
          ssh_cm_names_dump(&buffer, names);
          ssh_buffer_append_str(&buffer, "  }\n");
        }

      if (ssh_x509_cert_get_ext_key_usage(cert, &oid_list, &critical))
        {
          ssh_buffer_append_str(&buffer, "  extended-key-usage = { \n");
          while (oid_list != NULL)
            {
              /* XXX temporary casts until structure is changed XXX */
              oids = ssh_oid_find_by_oid_of_type(ssh_custr(oid_list->oid),
                                                 SSH_OID_EXT_KEY_USAGE);
              if (oids == NULL)
                ssh_buffer_append_cstrs(&buffer,
                                        "    (", oid_list->oid, ")\n",
                                        NULL);
              else
                ssh_buffer_append_cstrs(&buffer,
                                        "    ", oids->std_name,
                                        " (", oid_list->oid, ")\n",
                                        NULL);
              oid_list = oid_list->next;
            }
          ssh_buffer_append_str(&buffer, "  }\n");
        }
      ssh_buffer_append_str(&buffer, "}\n");

      return cm_debug_renderer_return(&buffer, buf, len);
    }
  return 0;
}
