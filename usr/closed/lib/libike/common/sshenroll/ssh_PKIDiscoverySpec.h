/***********************************************************************
 *  Copyright:
 *          Copyright (c) 2002, 2003 SFNT Finland Oy.
 *   All rights reserved                                               *
 *                                                                     *
 *   This file was automatically generated using sshasn1c compiler.    *
 *   Send commentes to Vesa Suontama <vsuontam@ssh.fi>                 *
 *                                                                     *
 ***********************************************************************/
/* Generated on Wed May 15 2002 15:06:30 */


#ifndef PKIDISCOVERYSPEC_H_INCLUDED
#define PKIDISCOVERYSPEC_H_INCLUDED
#include "ssh_asn1_primitives.h"
#include "ssh_asn1_vm.h"
extern const SshAsn1VMModuleStruct ssh_asn1_PKIDiscoverySpec;

/* Forward declarations of types. See the real declarations below. 
   We have to forward declare our types, since the imported headers 
   may import our types.*/

typedef struct SshAsn1PKIDiscoveryEntryRec *SshAsn1PKIDiscoveryEntry;

#define SshAsn1PKIDiscoveryResult SshADTContainer

/* Generated from the ASN.1 type 'PKIDiscoveryEntry' */
struct SshAsn1PKIDiscoveryEntryRec 
{
  /* octet string */
  SshStr protocol;
  /* octet string */
  SshStr serviceURL;
};

#define ssh_asn1_PKIDiscoveryEntry_encode(INST, DATA, LEN, INFO)              \
  ssh_asn1_vm_exec_encode((const SshAsn1VMModule) &ssh_asn1_PKIDiscoverySpec, \
                          0, (void *) INST, DATA, LEN, INFO)

#define ssh_asn1_PKIDiscoveryEntry_decode(DATA, LEN, INST, INFO)              \
  ssh_asn1_vm_exec_decode((const SshAsn1VMModule) &ssh_asn1_PKIDiscoverySpec, \
                          0, (void **) INST, DATA, LEN, INFO)

#define ssh_asn1_PKIDiscoveryEntry_free(INST)                                \
  ssh_asn1_vm_exec_free((const SshAsn1VMModule) &ssh_asn1_PKIDiscoverySpec,  \
                        0,(void *) INST)

/* Generated from the ASN.1 type 'PKIDiscoveryResult' */
/* The SEQUENCE of of which elements are of the type 
  ' SshAsn1PKIDiscoveryEntry' */
#define ssh_asn1_PKIDiscoveryResult_encode(INST, DATA, LEN, INFO)             \
  ssh_asn1_vm_exec_encode((const SshAsn1VMModule) &ssh_asn1_PKIDiscoverySpec, \
                          1, (void *) INST, DATA, LEN, INFO)

#define ssh_asn1_PKIDiscoveryResult_decode(DATA, LEN, INST, INFO)             \
  ssh_asn1_vm_exec_decode((const SshAsn1VMModule) &ssh_asn1_PKIDiscoverySpec, \
                          1, (void **) INST, DATA, LEN, INFO)

#define ssh_asn1_PKIDiscoveryResult_free(INST)                               \
  ssh_asn1_vm_exec_free((const SshAsn1VMModule) &ssh_asn1_PKIDiscoverySpec,  \
                        1, (void *) INST)

#endif /* PKIDISCOVERYSPEC_H_INCLUDED */ 
