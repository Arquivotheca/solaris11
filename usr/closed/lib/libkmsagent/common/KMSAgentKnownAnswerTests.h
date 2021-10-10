/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/** @file             KMSAgent.h 
 *  @defgroup         EncryptionAgent Encryption Agent API
 *
 * The Agent API is used to communicate with the KMS Appliance for the
 * purpose of registering storage devices, obtaining device keys, and
 * receiving notifications of storage device events such as destruction.
 *
 */
#ifndef KMS_AGENT_KNOWN_ANSWER_TESTS_H
#define KMS_AGENT_KNOWN_ANSWER_TESTS_H

/**
 *  This function exercises both <code>aes_key_wrap</code> and <code>aes_key_unwrap</code>
 *  in order to satisfy a FIPS 140-2 requirement for a known answer test, aka KAT.  Test
 *  vectors from RFC 3394 are used for this test.
 *  @return 0 on success, non-zero otherwise
 */
int KnownAnswerTestAESKeyWrap(void);
    
/**
 *  This function exercises both <code>rijndael_encrypt</code> and <code>rijndael_decrypt</code>
 *  in order to satisfy a FIPS 140-2 requirement for a known answer test, aka KAT.  Test
 *  vectors from Infoguard are used for this test.
 *  @return 0 if KAT passed, non-zero otherwise
 */
int KnownAnswerTestAESECB(void);

/**
 *  This function exercises  #HMACBuffers
 *  in order to satisfy a FIPS 140-2 requirement for a known answer test, aka KAT.  Test
 *  vectors from Infoguard are used for this test.
 *  @return 0 if KAT passed, non-zero otherwise
 */
int KnownAnswerTestHMACSHA1(void);

#endif


