/*
  cmi-pkcs7.c

  Copyright:
        Copyright (c) 2002, 2003 SFNT Finland Oy.
  	All rights reserved.

  PKCS#7 and CMS input syntax.
*/

#include "sshincludes.h"
#include "cmi.h"
#include "cmi-internal.h"
#include "ssh_berfile.h"

#define SSH_DEBUG_MODULE "SshCertCMi"

#ifdef SSHDIST_VALIDATOR_PKCS
SshCMStatus ssh_cm_add_pkcs7(SshCMContext cm,
                             SshPkcs7     packet)
{
  SshCMStatus status;
  SshUInt32 i, n;
  unsigned char **bers;
  size_t *ber_lens;

  /* Check trivialities. */
  if (packet == NULL)
    return SSH_CM_STATUS_OK;

  /* Try recursively. */
  if (ssh_pkcs7_get_content(packet))
    {
      status = ssh_cm_add_pkcs7(cm, ssh_pkcs7_get_content(packet));
      if (status != SSH_CM_STATUS_OK)
        return status;
    }

  n = ssh_pkcs7_get_certificates(packet, &bers, &ber_lens);
  if (n)
    {
      for (i = 0; i < n; i++)
        {
          SshCMCertificate cm_cert;

	  if ((cm_cert = ssh_cm_cert_allocate(cm)) == NULL)
	    continue;

          status = ssh_cm_cert_set_ber(cm_cert, bers[i], ber_lens[i]);
          if (status != SSH_CM_STATUS_OK)
            {
              /* Ignore broken certificates */
              ssh_cm_cert_free(cm_cert);
              continue;
            }

          status = ssh_cm_add(cm_cert);
          if (status != SSH_CM_STATUS_OK)
            {
              /* Fail if other other error than clash */
              ssh_cm_cert_free(cm_cert);
              if (status == SSH_CM_STATUS_ALREADY_EXISTS)
		continue;
              return status;
            }
        }
      ssh_free(bers);
      ssh_free(ber_lens);
    }
  n = ssh_pkcs7_get_crls(packet, &bers, &ber_lens);
  if (n)
    {
      for (i = 0; i < n; i++)
        {
          SshCMCrl cm_crl;

          if ((cm_crl = ssh_cm_crl_allocate(cm)) == NULL)
	    continue;

          status = ssh_cm_crl_set_ber(cm_crl, bers[i], ber_lens[i]);
          if (status != SSH_CM_STATUS_OK)
            {
              ssh_cm_crl_free(cm_crl);
              return status;
            }

          status = ssh_cm_add_crl(cm_crl);
          if (status != SSH_CM_STATUS_OK)
            {
              ssh_cm_crl_free(cm_crl);
              if (status == SSH_CM_STATUS_ALREADY_EXISTS)
		continue;
              return status;
            }
        }
      ssh_free(bers);
      ssh_free(ber_lens);
    }

  return SSH_CM_STATUS_OK;
}

SshCMStatus ssh_cm_add_pkcs7_ber(SshCMContext cm,
                                 unsigned char *ber_buf,
                                 size_t         ber_length)
{
  SshPkcs7 packet;
  SshCMStatus status;

  if (ssh_pkcs7_decode(ber_buf, ber_length, &packet) != SSH_PKCS7_OK)
    return SSH_CM_STATUS_FAILURE;

  /* Add recursively everything to the cache. */
  status = ssh_cm_add_pkcs7(cm, packet);
  ssh_pkcs7_free(packet);

  return status;
}
#endif /* SSHDIST_VALIDATOR_PKCS */
