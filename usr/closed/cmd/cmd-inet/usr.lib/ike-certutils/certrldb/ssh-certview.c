/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
  t-x509view.c

  Author Mika Kojo <mkojo@ssh.fi>
  Copyright (c) 1998 SSH Communications Security, Ltd.
  All rights reserved.

  A viewing program for X.509 certificates and CRL's.

 */

#include <ike/sshincludes.h>
#include <ike/sshmp.h>
#include <ike/sshcrypt.h>
#include <ike/sshasn1.h>
#include <ike/x509.h>
#include <ike/dn.h>
#include <ike/oid.h>
#include <ike/sshfileio.h>
#include <ike/sshpsystem.h>

#include "parse-x509-forms.h"
#include "iprintf.h"


int base     = 16;

char *ext_table[] =
{
  "authority key identifier",
  "subject key identifier",
  "key usage",
  "private key usage period",
  "certificate policies",
  "policy mappings",
  "subject alternative names",
  "issuer alternative names",
  "subject directory attributes",
  "basic constraints",
  "name constraints",
  "policy constraints",
  "private internet extensions",
  "authority information access",
  "CRL distribution points",
  "extended key usage",

  "CRL reason code",
  "hold instruction code",
  "invalidity date",
  "CRL number",
  "issuing distribution point",
  "delta CRL indicator",
  "certificate issuer",

  "unknown",

  NULL
};

char *get_ext(unsigned int ext)
{
  int i;

  for (i = 0; ext_table[i]; i++)
    {
      if (i == ext)
        return ext_table[i];
    }
  return "failure";
}

void ssh_dump_name(char *name)
{
      iprintf("#I<%s>#i\n", name);
}

Boolean ssh_dump_crl_ext(SshX509Crl c)
{
	int i, k;
	Boolean critical;

  iprintf("Available = #I");
  for (i = 0, k = 0; i < SSH_X509_EXT_MAX; i++)
    {
      if (ssh_x509_crl_ext_available(c, i, &critical))
        {
          if (k > 0)
            iprintf(", ");
          iprintf("%s", get_ext(i));
          if (critical)
            iprintf("(critical)");
          k++;
        }
    }
  if (k == 0)
    iprintf("(not available)");

  iprintf("#i\n");
  return FALSE;
}

Boolean ssh_dump_revoked_ext(SshX509RevokedCerts c)
{
  int i, k;
  Boolean critical;

  iprintf("Available = #I");
  for (i = 0, k = 0; i < SSH_X509_EXT_MAX; i++)
    {
      if (ssh_x509_revoked_ext_available(c, i, &critical))
        {
          if (k > 0)
            iprintf(", ");
          iprintf("%s", get_ext(i));
          if (critical)
            iprintf("(critical)");
          k++;
        }
    }
  if (k == 0)
    iprintf("(not available)");
  iprintf("#i\n");
  return FALSE;
}

/* Dumper routines. */

Boolean ssh_dump_time(SshBerTimeStruct *ber_time)
{
  char *name;
  ssh_ber_time_to_string(ber_time, &name);
  iprintf("%s", name);
  ssh_free(name);
  iprintf("\n");
  return FALSE;
}

Boolean ssh_dump_number(SshMPIntegerStruct *number)
{
  char *buf;
  buf = ssh_mp_get_str(NULL, base, number);
  iprintf("%s\n", buf);
  ssh_free(buf);
  return FALSE;
}

Boolean ssh_dump_public_key(SshPublicKey public_key)
{
  const SshOidStruct *oids;
  SshX509PkAlgorithmDef algorithm;
  SshMPIntegerStruct e, n, p, q, g, y;

  if (public_key == NULL)
    {
      iprintf("[Public key invalid.]\n");
      return TRUE;
    }

  algorithm = (SshX509PkAlgorithmDef)
      ssh_x509_public_key_algorithm(public_key);
  if (algorithm == NULL)
    {
      iprintf("[Corrupted public key.]\n");
      return TRUE;
    }

  iprintf("Algorithm name   (SSH) : %s{sign{%s}}\n",
         algorithm->name, algorithm->sign);

  oids = ssh_oid_find_by_std_name_of_type(algorithm->known_name, SSH_OID_PK);
  if (oids == NULL)
    {
      iprintf("[Could not map name into a Object Identifier. "
              "Either an unsupported or invalid key.]\n");
      return TRUE;
    }

  iprintf("Algorithm name (X.509) : %s\n", oids->std_name);

  switch (((SshOidPk)(oids->extra))->alg_enum)
    {
    case SSH_X509_PKALG_RSA:
      /* Handle RSA keys. */
      ssh_mp_init(&e);
      ssh_mp_init(&n);
      if (ssh_public_key_get_info(public_key,
                                  SSH_PKF_MODULO_N, &n,
                                  SSH_PKF_PUBLIC_E, &e,
                                  SSH_PKF_END) != SSH_CRYPTO_OK)
        {
          iprintf("[Internal error, could not get RSA parameters.]\n");
          ssh_mp_clear(&e);
          ssh_mp_clear(&n);
          return TRUE;
        }

      iprintf("Modulus n  (%4d bits) : #I\n",
             ssh_mp_get_size(&n, 2));
      ssh_dump_number(&n);
      iprintf("#i");

      iprintf("Exponent e (%4d bits) : #I",
             ssh_mp_get_size(&e, 2));
      ssh_dump_number(&e);
      iprintf("#i");

      ssh_mp_clear(&n);
      ssh_mp_clear(&e);

      break;
    case SSH_X509_PKALG_DSA:
      /* Handle DSA keys. */
      ssh_mp_init(&p);
      ssh_mp_init(&g);
      ssh_mp_init(&q);
      ssh_mp_init(&y);

      if (ssh_public_key_get_info(public_key,
                                  SSH_PKF_PRIME_P, &p,
                                  SSH_PKF_PRIME_Q, &q,
                                  SSH_PKF_GENERATOR_G, &g,
                                  SSH_PKF_PUBLIC_Y, &y,
                                  SSH_PKF_END) != SSH_CRYPTO_OK)
        {
          ssh_mp_clear(&p);
          ssh_mp_clear(&q);
          ssh_mp_clear(&g);
          ssh_mp_clear(&y);
          iprintf("[Internal error, could not get DSA parameters.]\n");
          return TRUE;
        }

      iprintf("Modulus p     (%4d bits) : #I",
             ssh_mp_get_size(&p, 2));
      ssh_dump_number(&p);
      iprintf("#i");

      iprintf("Group order q (%4d bits) : #I",
             ssh_mp_get_size(&q, 2));
      ssh_dump_number(&q);
      iprintf("#i");

      iprintf("Generator g   (%4d bits) : #I",
             ssh_mp_get_size(&g, 2));
      ssh_dump_number(&g);
      iprintf("#i");

      iprintf("Public key y  (%4d bits) : #I",
             ssh_mp_get_size(&y, 2));
      ssh_dump_number(&y);
      iprintf("#i");

      ssh_mp_clear(&p);
      ssh_mp_clear(&q);
      ssh_mp_clear(&g);
      ssh_mp_clear(&y);
      break;
    default:
      iprintf("[Pretty print doesn't support this key type.]\n");
      break;
    }
  return FALSE;
}

Boolean ssh_dump_usage(SshX509UsageFlags flags)
{
  if (flags & SSH_X509_UF_DIGITAL_SIGNATURE)
    iprintf("DigitalSignature ");
  if (flags & SSH_X509_UF_NON_REPUDIATION)
    iprintf("NonRepudiation ");
  if (flags & SSH_X509_UF_KEY_ENCIPHERMENT)
    iprintf("KeyEncipherment ");
  if (flags & SSH_X509_UF_DATA_ENCIPHERMENT)
    iprintf("DataEncipherment ");
  if (flags & SSH_X509_UF_KEY_AGREEMENT)
    iprintf("KeyAgreement ");
  if (flags & SSH_X509_UF_KEY_CERT_SIGN)
    iprintf("KeyCertSign ");
  if (flags & SSH_X509_UF_CRL_SIGN)
    iprintf("CRLSign ");
  if (flags & SSH_X509_UF_ENCIPHER_ONLY)
    iprintf("EncipherOnly ");
  if (flags & SSH_X509_UF_DECIPHER_ONLY)
    iprintf("DecipherOnly ");
  iprintf("\n");
  return FALSE;
}

Boolean ssh_dump_reason(SshX509ReasonFlags flags)
{
  if (flags & SSH_X509_RF_UNSPECIFIED)
    iprintf("Unspecified ");
  if (flags & SSH_X509_RF_KEY_COMPROMISE)
    iprintf("KeyCompromise ");
  if (flags & SSH_X509_RF_CA_COMPROMISE)
    iprintf("CACompromise ");
  if (flags & SSH_X509_RF_AFFILIATION_CHANGED)
    iprintf("AffiliationChanged ");
  if (flags & SSH_X509_RF_SUPERSEDED)
    iprintf("Superseded ");
  if (flags & SSH_X509_RF_CESSATION_OF_OPERATION)
    iprintf("CessationOfOperation ");
  if (flags & SSH_X509_RF_CERTIFICATE_HOLD)
    iprintf("CertificateHold ");
  iprintf("\n");
  return FALSE;
}

Boolean ssh_dump_crl_reason(SshX509CRLReasonCode code)
{
  char *str[] =
  {
    "Unspecified\n",
    "KeyCompromise\n",
    "CACompromise\n",
    "AffiliationChanged\n",
    "Superseded\n",
    "CessationOfOperation\n",
    "CertificateHold\n",
    "\n",
    "RemoveFromCRL\n",
    "PrivilegeWithdrawn\n",
    "AACompromise\n"
  };

  if (code < 0 || code > 10 || code == 7)
    return TRUE;
  iprintf("%s", str[code]);
  return FALSE;
}

Boolean ssh_dump_hex(unsigned char *str, size_t len)
{
  size_t i;
  iprintf(" : ");
  for (i = 0; i < len; i++)
    {
      if (i > 0)
        iprintf(" ");
      if (i > 0 && (i % 20) == 0)
        iprintf("\n : ");
      iprintf("%02x", str[i]);
    }
  iprintf("\n");
  return FALSE;
}

char *name_types[11] =
{
  "distinguished name",
  "unique id",
  "EMAIL (rfc822)",
  "DNS (domain name server name)",
  "IP (ip address)",
  "DN (directory name)",
  "X400 (X.400 name)",
  "EDI (EDI party name)",
  "URI (uniform resource indicator)",
  "RID (registered identifier)",
  "OTHER (other name)"
};

Boolean ssh_dump_names(SshX509Name names)
{
  char *name;
  unsigned char *buf;
  size_t buf_len;
  Boolean rv, ret = FALSE;
  SshX509Name list;
  int i;

  iprintf("Following names detected = #I\n");
  for (list = names, i = 0; list; list = list->next, i++)
    {
      if (i > 0)
        iprintf(", ");
      iprintf("%s", name_types[list->type]);
    }
  if (names == NULL)
    iprintf("n/a");
  iprintf("#i\n");

  iprintf("Viewing specific name types = #I\n");
  do
    {
      rv = ssh_x509_name_pop_ip(names, &buf, &buf_len);
      if (rv == TRUE)
        {
          iprintf("IP  = %d.%d.%d.%d\n",
                 (int)buf[0], (int)buf[1], (int)buf[2], (int)buf[3]);
          ret = TRUE;
        }
    }
  while (rv == TRUE);

  do
    {
      rv = ssh_x509_name_pop_dns(names, &name);
      if (rv == TRUE)
        {
          iprintf("DNS = %s\n", name);
          ret = TRUE;
        }
    }
  while (rv == TRUE);

  do
    {
      rv = ssh_x509_name_pop_uri(names, &name);
      if (rv == TRUE)
        {
          iprintf("URI = #I%s#i\n", name);
          ret = TRUE;
        }
    }
  while (rv == TRUE);


  do
    {
      rv = ssh_x509_name_pop_email(names, &name);
      if (rv == TRUE)
        {
          iprintf("EMAIL = %s\n", name);
          ret = TRUE;
        }
    }
  while (rv == TRUE);

  do
    {
      rv = ssh_x509_name_pop_rid(names, &name);
      if (rv == TRUE)
        {
          iprintf("RID = %s\n", name);
          ret = TRUE;
        }
    }
  while (rv == TRUE);

  do
    {
      rv = ssh_x509_name_pop_directory_name(names, &name);
      if (rv == TRUE)
        {
          iprintf("DN = ");
          ssh_dump_name(name);
          ret = TRUE;
        }
    }
  while (rv == TRUE);

  iprintf("#i");

  if (ret != TRUE)
    iprintf("No names of type IP, DNS, URI, EMAIL, RID, or DN detected.\n");

  return ret;
}

Boolean ssh_dump_revoked(SshX509RevokedCerts revoked)
{
  char *name;
  SshBerTimeStruct date;
  Boolean rv;
  int number;
  SshMPIntegerStruct s;
  SshX509ReasonFlags reason;
  Boolean critical;

  ssh_mp_init(&s);

  iprintf("RevokedCertList = #I\n");

  if (revoked == NULL)
    iprintf("(not present)\n");

  number = 1;
  while (revoked)
    {
      iprintf("%% Entry %u\n", number);

      rv = ssh_x509_revoked_get_serial_number(revoked, &s);
      if (rv == FALSE)
        goto failed;
      iprintf("SerialNumber = ");
      if (ssh_dump_number(&s))
        return TRUE;

      rv = ssh_x509_revoked_get_revocation_date(revoked, &date);
      if (rv == FALSE)
        goto failed;
      iprintf("RevocationDate = ");
      if (ssh_dump_time(&date))
        return TRUE;

      iprintf("Extensions = #I\n");

      if (ssh_dump_revoked_ext(revoked))
        return TRUE;

      if (ssh_x509_revoked_ext_available(revoked,
	      SSH_X509_CRL_ENTRY_EXT_REASON_CODE, &critical))
        {
          rv = ssh_x509_revoked_get_reason_code(revoked, &reason, &critical);
          iprintf("ReasonCode = ");
          if (ssh_dump_crl_reason(reason))
            return TRUE;
          if (critical)
            iprintf("#I[CRITICAL]#i\n");
        }

      if (ssh_x509_revoked_ext_available(revoked,
	      SSH_X509_CRL_ENTRY_EXT_HOLD_INST_CODE, &critical))
        {
          rv = ssh_x509_revoked_get_hold_instruction_code(revoked, &name,
                                                          &critical);
          iprintf("HoldInstCode = #I");

          if (strcmp(SSH_X509_HOLD_INST_CODE_NONE, name) == 0)
            iprintf("None\n");
          else if (strcmp(SSH_X509_HOLD_INST_CODE_CALLISSUER, name) == 0)
            iprintf("CallIssuer\n");
          else if (strcmp(SSH_X509_HOLD_INST_CODE_REJECT, name) == 0)
            iprintf("Reject\n");
          else
            iprintf("OID = %s\n", name);
          if (critical)
            iprintf("#I[CRITICAL]#i\n");
          printf("#i");
        }

      if (ssh_x509_revoked_ext_available(revoked,
	      SSH_X509_CRL_ENTRY_EXT_INVALIDITY_DATE, &critical))
        {
          rv = ssh_x509_revoked_get_invalidity_date(revoked, &date, &critical);
          iprintf("InvalidityDate = ");
          if (ssh_dump_time(&date))
            return TRUE;
          if (critical)
            iprintf("#I[CRITICAL]#i\n");
        }

      iprintf("#i");

      number++;
      revoked = ssh_x509_revoked_get_next(revoked);
    }
  rv = TRUE;
failed:
  if (rv == FALSE)
    iprintf("#I[error]#i\n");
  iprintf("#i");
  return !rv;
}

Boolean dump_crl(SshX509Crl crl)
{
  char *name;
  SshBerTimeStruct this_update, next_update;
  Boolean rv;
  SshMPIntegerStruct s;
  SshX509Name names;
  Boolean critical;

  ssh_mp_init(&s);

  iprintf("CRL: #I\n");

  /* Issuer name */
  rv = ssh_x509_crl_get_issuer_name(crl, &name);
  if (rv == FALSE)
    goto failed;
  iprintf("IssuerName = ");
  ssh_dump_name(name);

  /* Update times */
  rv = ssh_x509_crl_get_update_times(crl, &this_update, &next_update);
  if (rv == FALSE)
    goto failed;
  if (ssh_ber_time_available(&this_update))
    {
      iprintf("ThisUpdate = ");
      if (ssh_dump_time(&this_update))
        return TRUE;
    }
  if (ssh_ber_time_available(&next_update))
    {
      iprintf("NextUpdate = ");
      if (ssh_dump_time(&next_update))
        return TRUE;
    }

  iprintf("Extensions = #I\n");

  if (ssh_dump_crl_ext(crl))
    return TRUE;

  /* Dump issuer alt names. */
  if (ssh_x509_crl_ext_available(crl, SSH_X509_CRL_EXT_ISSUER_ALT_NAME,
	  &critical))
    {
      iprintf("IssuerAltNames = \n");
      rv = ssh_x509_crl_get_issuer_alternative_names(crl, &names, &critical);
      if (ssh_dump_names(names) == TRUE && critical)
        iprintf("#I[CRITICAL]#i\n");
    }

  if (ssh_x509_crl_ext_available(crl, SSH_X509_CRL_EXT_CRL_NUMBER, &critical))
    {
      rv = ssh_x509_crl_get_crl_number(crl, &s, &critical);
      iprintf("CRLNumber = ");
      if (ssh_dump_number(&s))
        return TRUE;
      if (critical)
        iprintf("#I[CRITICAL]#i\n");
    }

  if (ssh_x509_crl_ext_available(crl, SSH_X509_CRL_EXT_DELTA_CRL_IND,
	  &critical))
    {
      rv = ssh_x509_crl_get_delta_crl_indicator(crl, &s, &critical);
      iprintf("DeltaCRLIndicator = ");
      if (ssh_dump_number(&s))
        return TRUE;
      if (critical)
        iprintf("#I[CRITICAL]#i\n");
    }

  /* Now dump the revokation lists. */

  if (ssh_dump_revoked(ssh_x509_crl_get_revoked(crl)))
    return TRUE;

  rv = TRUE;
failed:
  if (rv == FALSE)
    iprintf("#I[error]#i\n");

  iprintf("#i#i");

  ssh_mp_clear(&s);
  return !rv;
}
