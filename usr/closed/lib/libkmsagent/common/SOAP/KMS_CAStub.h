/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* KMS_CAStub.h
   Generated by gSOAP 2.7.17 from ../gsoapStubs/CAService/KMS_CA_SOAP.h
   Copyright(C) 2000-2010, Robert van Engelen, Genivia Inc. All Rights Reserved.
   This part of the software is released under one of the following licenses:
   GPL, the gSOAP public license, or Genivia's license for commercial use.

   The above Copyright notice pertains to Genivia Inc.'s licensing policies with
   respect to the gSOAP 2.7.17 and ../gsoapStubs/CAService/KMS_CA_SOAP.h 
   code generator and NOT this generated code produced thereby. 
*/

#ifndef KMS_CAStub_H
#define KMS_CAStub_H
#ifndef WITH_NONAMESPACES
#define WITH_NONAMESPACES
#endif
#ifndef WITH_NOGLOBAL
#define WITH_NOGLOBAL
#endif
#include "stdsoap2.h"

namespace KMS_CA {

/******************************************************************************\
 *                                                                            *
 * Enumerations                                                               *
 *                                                                            *
\******************************************************************************/


/******************************************************************************\
 *                                                                            *
 * Types with Custom Serializers                                              *
 *                                                                            *
\******************************************************************************/


/******************************************************************************\
 *                                                                            *
 * Classes and Structs                                                        *
 *                                                                            *
\******************************************************************************/


#if 0 /* volatile type: do not declare here, declared elsewhere */

#endif

#ifndef SOAP_TYPE_KMS_CA_xsd__hexBinary
#define SOAP_TYPE_KMS_CA_xsd__hexBinary (18)
/* hexBinary schema type: */
struct xsd__hexBinary
{
public:
	unsigned char *__ptr;
	int __size;
};
#endif

#ifndef SOAP_TYPE_KMS_CA_KMS_CA__RetrieveRootCACertificateResponse
#define SOAP_TYPE_KMS_CA_KMS_CA__RetrieveRootCACertificateResponse (22)
/* KMS-CA:RetrieveRootCACertificateResponse */
struct KMS_CA__RetrieveRootCACertificateResponse
{
public:
	struct xsd__hexBinary RootCACertificate;	/* SOAP 1.2 RPC return element (when namespace qualified) */	/* required element of type xsd:hexBinary */
	long AuthenticationHashIterationCount;	/* required element of type xsd:int */
	struct xsd__hexBinary ClientAuthenticationChallenge;	/* required element of type xsd:hexBinary */
};
#endif

#ifndef SOAP_TYPE_KMS_CA_KMS_CA__RetrieveRootCACertificate
#define SOAP_TYPE_KMS_CA_KMS_CA__RetrieveRootCACertificate (25)
/* KMS-CA:RetrieveRootCACertificate */
struct KMS_CA__RetrieveRootCACertificate
{
public:
	char *EntityID;	/* optional element of type xsd:string */
};
#endif

#ifndef SOAP_TYPE_KMS_CA_KMS_CA__RetrieveLocalClockResponse
#define SOAP_TYPE_KMS_CA_KMS_CA__RetrieveLocalClockResponse (26)
/* KMS-CA:RetrieveLocalClockResponse */
struct KMS_CA__RetrieveLocalClockResponse
{
public:
	char *CurrentTime;	/* SOAP 1.2 RPC return element (when namespace qualified) */	/* optional element of type xsd:dateTime */
};
#endif

#ifndef SOAP_TYPE_KMS_CA_KMS_CA__RetrieveLocalClock
#define SOAP_TYPE_KMS_CA_KMS_CA__RetrieveLocalClock (29)
/* KMS-CA:RetrieveLocalClock */
struct KMS_CA__RetrieveLocalClock
{
public:
	char *EntityID;	/* optional element of type xsd:string */
};
#endif

#ifndef SOAP_TYPE_KMS_CA_SOAP_ENV__Header
#define SOAP_TYPE_KMS_CA_SOAP_ENV__Header (30)
/* SOAP Header: */
struct SOAP_ENV__Header
{
#ifdef WITH_NOEMPTYSTRUCT
private:
	char dummy;	/* dummy member to enable compilation */
#endif
};
#endif

#ifndef SOAP_TYPE_KMS_CA_SOAP_ENV__Code
#define SOAP_TYPE_KMS_CA_SOAP_ENV__Code (31)
/* SOAP Fault Code: */
struct SOAP_ENV__Code
{
public:
	char *SOAP_ENV__Value;	/* optional element of type xsd:QName */
	struct SOAP_ENV__Code *SOAP_ENV__Subcode;	/* optional element of type SOAP-ENV:Code */
};
#endif

#ifndef SOAP_TYPE_KMS_CA_SOAP_ENV__Detail
#define SOAP_TYPE_KMS_CA_SOAP_ENV__Detail (33)
/* SOAP-ENV:Detail */
struct SOAP_ENV__Detail
{
public:
	int __type;	/* any type of element <fault> (defined below) */
	void *fault;	/* transient */
	char *__any;
};
#endif

#ifndef SOAP_TYPE_KMS_CA_SOAP_ENV__Reason
#define SOAP_TYPE_KMS_CA_SOAP_ENV__Reason (36)
/* SOAP-ENV:Reason */
struct SOAP_ENV__Reason
{
public:
	char *SOAP_ENV__Text;	/* optional element of type xsd:string */
};
#endif

#ifndef SOAP_TYPE_KMS_CA_SOAP_ENV__Fault
#define SOAP_TYPE_KMS_CA_SOAP_ENV__Fault (37)
/* SOAP Fault: */
struct SOAP_ENV__Fault
{
public:
	char *faultcode;	/* optional element of type xsd:QName */
	char *faultstring;	/* optional element of type xsd:string */
	char *faultactor;	/* optional element of type xsd:string */
	struct SOAP_ENV__Detail *detail;	/* optional element of type SOAP-ENV:Detail */
	struct SOAP_ENV__Code *SOAP_ENV__Code;	/* optional element of type SOAP-ENV:Code */
	struct SOAP_ENV__Reason *SOAP_ENV__Reason;	/* optional element of type SOAP-ENV:Reason */
	char *SOAP_ENV__Node;	/* optional element of type xsd:string */
	char *SOAP_ENV__Role;	/* optional element of type xsd:string */
	struct SOAP_ENV__Detail *SOAP_ENV__Detail;	/* optional element of type SOAP-ENV:Detail */
};
#endif

/******************************************************************************\
 *                                                                            *
 * Typedefs                                                                   *
 *                                                                            *
\******************************************************************************/

#ifndef SOAP_TYPE_KMS_CA__QName
#define SOAP_TYPE_KMS_CA__QName (5)
typedef char *_QName;
#endif

#ifndef SOAP_TYPE_KMS_CA__XML
#define SOAP_TYPE_KMS_CA__XML (6)
typedef char *_XML;
#endif

#ifndef SOAP_TYPE_KMS_CA_xsd__string
#define SOAP_TYPE_KMS_CA_xsd__string (7)
typedef char *xsd__string;
#endif

#ifndef SOAP_TYPE_KMS_CA_xsd__float
#define SOAP_TYPE_KMS_CA_xsd__float (9)
typedef float xsd__float;
#endif

#ifndef SOAP_TYPE_KMS_CA_xsd__int
#define SOAP_TYPE_KMS_CA_xsd__int (11)
typedef long xsd__int;
#endif

#ifndef SOAP_TYPE_KMS_CA_xsd__boolean
#define SOAP_TYPE_KMS_CA_xsd__boolean (13)
typedef bool xsd__boolean;
#endif

#ifndef SOAP_TYPE_KMS_CA_xsd__long
#define SOAP_TYPE_KMS_CA_xsd__long (15)
typedef LONG64 xsd__long;
#endif

#ifndef SOAP_TYPE_KMS_CA_xsd__dateTime
#define SOAP_TYPE_KMS_CA_xsd__dateTime (16)
typedef char *xsd__dateTime;
#endif

#ifndef SOAP_TYPE_KMS_CA_xsd__duration
#define SOAP_TYPE_KMS_CA_xsd__duration (17)
typedef char *xsd__duration;
#endif


/******************************************************************************\
 *                                                                            *
 * Externals                                                                  *
 *                                                                            *
\******************************************************************************/


/******************************************************************************\
 *                                                                            *
 * Server-Side Operations                                                     *
 *                                                                            *
\******************************************************************************/


SOAP_FMAC5 int SOAP_FMAC6 KMS_CA__RetrieveRootCACertificate(struct soap*, char *EntityID, struct KMS_CA__RetrieveRootCACertificateResponse &result);

SOAP_FMAC5 int SOAP_FMAC6 KMS_CA__RetrieveLocalClock(struct soap*, char *EntityID, struct KMS_CA__RetrieveLocalClockResponse &result);

/******************************************************************************\
 *                                                                            *
 * Server-Side Skeletons to Invoke Service Operations                         *
 *                                                                            *
\******************************************************************************/

SOAP_FMAC5 int SOAP_FMAC6 KMS_CA_serve(struct soap*);

SOAP_FMAC5 int SOAP_FMAC6 KMS_CA_serve_request(struct soap*);

SOAP_FMAC5 int SOAP_FMAC6 soap_serve_KMS_CA__RetrieveRootCACertificate(struct soap*);

SOAP_FMAC5 int SOAP_FMAC6 soap_serve_KMS_CA__RetrieveLocalClock(struct soap*);

/******************************************************************************\
 *                                                                            *
 * Client-Side Call Stubs                                                     *
 *                                                                            *
\******************************************************************************/


SOAP_FMAC5 int SOAP_FMAC6 soap_call_KMS_CA__RetrieveRootCACertificate(struct soap *soap, const char *soap_endpoint, const char *soap_action, char *EntityID, struct KMS_CA__RetrieveRootCACertificateResponse &result);

SOAP_FMAC5 int SOAP_FMAC6 soap_call_KMS_CA__RetrieveLocalClock(struct soap *soap, const char *soap_endpoint, const char *soap_action, char *EntityID, struct KMS_CA__RetrieveLocalClockResponse &result);

} // namespace KMS_CA


#endif

/* End of KMS_CAStub.h */
