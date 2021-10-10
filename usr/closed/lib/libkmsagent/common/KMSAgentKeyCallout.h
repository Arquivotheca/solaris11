/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/**
 * \file    KMSAgentKeyCallout.h
 *
 */

#ifndef KMSAGENT_KEYCALLOUT_H
#define KMSAGENT_KEYCALLOUT_H

#include "KMSAgent.h"

/**
 *  Behavior is up to customizers of the KMS Agent reference implementation. 
 *  A possible usage of this function is to encrypt the plaintext 
 *  key value.  This function will be invoked by the following KMS Agent API
 *  functions upon successful receipt of a key from a KMS transaction:
 *  <ul>
 *  <li>KMSAgent_CreateKey
 *  <li>KMSAgent_RetrieveKey
 *  <li>KMSAgent_RetrieveDataUnitKeys - once for each key retrieved
 *  <li>KMSAgent_RetrieveProtectAndProcessKey
 *  </ul>
 *
 *  @param io_pKey   a plaintext key
 *  @return 0 if success   
 */
int KMSAgentKeyCallout( unsigned char io_aKey[KMS_MAX_KEY_SIZE] );


#endif

