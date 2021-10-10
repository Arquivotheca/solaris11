/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdint.h>
#include "KMSAgentKeyCallout.h"

#ifdef METAWARE
extern "C" int ecpt_get_pc_key_and_xor( unsigned char * key );
#endif

/**
 *  Hook function to get the key in the clear (XOR is presently used)
 *  @returns 0=ok, nonzero = bad
 */
int KMSAgentKeyCallout( unsigned char io_aKey[KMS_MAX_KEY_SIZE] )
{
#ifndef METAWARE
    return 0;
#else
    return ecpt_get_pc_key_and_xor( io_aKey );
#endif    
}
