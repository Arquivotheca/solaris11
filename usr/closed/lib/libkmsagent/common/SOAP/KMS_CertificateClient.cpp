/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* KMS_CertificateClient.cpp
   Generated by gSOAP 2.7.17 from ../gsoapStubs/CertificateService/KMS_Certificate_SOAP.h
   Copyright(C) 2000-2010, Robert van Engelen, Genivia Inc. All Rights Reserved.
   This part of the software is released under one of the following licenses:
   GPL, the gSOAP public license, or Genivia's license for commercial use.

   The above Copyright notice pertains to Genivia Inc.'s licensing policies with
   respect to the gSOAP 2.7.17 and ../gsoapStubs/CertificateService/KMS_Certificate_SOAP.h 
   code generator and NOT this generated code produced thereby. 
*/

#if defined(__BORLANDC__)
#pragma option push -w-8060
#pragma option push -w-8004
#endif
#include "KMS_CertificateH.h"

namespace KMS_Certificate {

SOAP_SOURCE_STAMP("@(#) KMS_CertificateClient.cpp ver 2.7.17 2010-06-08 19:16:38 GMT")


SOAP_FMAC5 int SOAP_FMAC6 soap_call_KMS_Certificate__RetrieveEntityCertificate(struct soap *soap, const char *soap_endpoint, const char *soap_action, char *EntityID, struct xsd__hexBinary ClientAuthenticationResponse, struct xsd__hexBinary ServerAuthenticationChallenge, struct KMS_Certificate__RetrieveEntityCertificateResponse &result)
{	struct KMS_Certificate__RetrieveEntityCertificate soap_tmp_KMS_Certificate__RetrieveEntityCertificate;
	soap_tmp_KMS_Certificate__RetrieveEntityCertificate.EntityID = EntityID;
	soap_tmp_KMS_Certificate__RetrieveEntityCertificate.ClientAuthenticationResponse = ClientAuthenticationResponse;
	soap_tmp_KMS_Certificate__RetrieveEntityCertificate.ServerAuthenticationChallenge = ServerAuthenticationChallenge;
	soap_begin(soap);
	soap_serializeheader(soap);
	soap_serialize_KMS_Certificate__RetrieveEntityCertificate(soap, &soap_tmp_KMS_Certificate__RetrieveEntityCertificate);
	if (soap_begin_count(soap))
		return soap->error;
	if (soap->mode & SOAP_IO_LENGTH)
	{	if (soap_envelope_begin_out(soap)
		 || soap_putheader(soap)
		 || soap_body_begin_out(soap)
		 || soap_put_KMS_Certificate__RetrieveEntityCertificate(soap, &soap_tmp_KMS_Certificate__RetrieveEntityCertificate, "KMS-Certificate:RetrieveEntityCertificate", NULL)
		 || soap_body_end_out(soap)
		 || soap_envelope_end_out(soap))
			 return soap->error;
	}
	if (soap_end_count(soap))
		return soap->error;
	if (soap_connect(soap, soap_endpoint, soap_action)
	 || soap_envelope_begin_out(soap)
	 || soap_putheader(soap)
	 || soap_body_begin_out(soap)
	 || soap_put_KMS_Certificate__RetrieveEntityCertificate(soap, &soap_tmp_KMS_Certificate__RetrieveEntityCertificate, "KMS-Certificate:RetrieveEntityCertificate", NULL)
	 || soap_body_end_out(soap)
	 || soap_envelope_end_out(soap)
	 || soap_end_send(soap))
		return soap_closesock(soap);
	if (!&result)
		return soap_closesock(soap);
	soap_default_KMS_Certificate__RetrieveEntityCertificateResponse(soap, &result);
	if (soap_begin_recv(soap)
	 || soap_envelope_begin_in(soap)
	 || soap_recv_header(soap)
	 || soap_body_begin_in(soap))
		return soap_closesock(soap);
	if (soap_recv_fault(soap, 1))
		return soap->error;
	soap_get_KMS_Certificate__RetrieveEntityCertificateResponse(soap, &result, "", "");
	if (soap->error)
		return soap_recv_fault(soap, 0);
	if (soap_body_end_in(soap)
	 || soap_envelope_end_in(soap)
	 || soap_end_recv(soap))
		return soap_closesock(soap);
	return soap_closesock(soap);
}

} // namespace KMS_Certificate


#if defined(__BORLANDC__)
#pragma option pop
#pragma option pop
#endif

/* End of KMS_CertificateClient.cpp */
