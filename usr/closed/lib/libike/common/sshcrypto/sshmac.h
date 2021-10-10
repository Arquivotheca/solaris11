/*

  sshmac.h

  Author: Santeri Paavolainen <santtu@ssh.com>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  General mac (message authentication code) functions, these are to
  allow transparent use of all SSH supported mac types.

  A MAC is allocated by calling ssh_mac_allocate. The actual MAC
  computation is then performed by calling ssh_mac_update (maybe
  multiple times) and the digest can be received with ssh_mac_final
  function.  Notice that the digest must be reallocated and of correct
  length.

  The MAC can be reset with call to ssh_mac_reset, which is implicit
  on ssh_mac_allocate, and must be called if the same mac context is
  to be used for multiple MAC computations.

*/

#ifndef SSHMAC_H
#define SSHMAC_H

/*********************** Mac functions ************************************/

typedef struct SshMacRec *SshMac;

/* Returns a comma-separated list of supported mac types.  The caller
   must free the list with ssh_crypto_free(). */
char *
ssh_mac_get_supported(void);

/* Returns TRUE or FALSE depending whether the MAC called "name" is
   supported with this version of crypto library. */
Boolean
ssh_mac_supported(const char *name);

/* Returns TRUE or FALSE depending whether the MAC is a FIPS approved
   MAC (eg. whether the hash used in it is approved -- no cipher based
   MACs are approved at the moment). */
Boolean
ssh_mac_is_fips_approved(const char *name);

/* Returns the minimum key length in bytes for the MAC. This can
   be zero if there is no maximum key length (and this is no typo). */
size_t
ssh_mac_get_min_key_length(const char *name);

/* Returns maximum key length in bytes for the MAC. This can be zero
   if there is no maximum key length. If there is only a fixed key
   size, then minimum == maximum.*/
size_t
ssh_mac_get_max_key_length(const char *name);

/* This returns the block length of the MAC if it is defined, else
   this returns 0. */
size_t
ssh_mac_get_block_length(const char *name);

/* Allocate a MAC for use in a session */
SshCryptoStatus
ssh_mac_allocate(const char *type,
                 const unsigned char *key, size_t keylen, SshMac *mac);

/* Free the MAC. */
void
ssh_mac_free(SshMac mac);

/* Returns the name of the MAC. The name is same as that what was used in
   ssh_mac_allocate. The name points to an internal data structure and
   should NOT be freed, modified, or used after ssh_mac_free has been
   called. */
const char *
ssh_mac_name(SshMac mac);

/* Get the length in bytes of the MAC digest.  The maximum length is
   SSH_MAX_HASH_DIGEST_LENGTH. */
size_t
ssh_mac_length(const char *name);

/* Reset the MAC to its initial state.  This must be called before
   processing a new packet/message with a MAC pointer used for previous
   messages. */
void
ssh_mac_reset(SshMac mac);

/* Update the MAC by adding data from the given buffer. */
void
ssh_mac_update(SshMac mac, const unsigned char *data, size_t len);

/* Get the resulting MAC digest. The user allocated digest buffer must be
   at least ssh_mac_length(mac) bytes long.  */
SshCryptoStatus
ssh_mac_final(SshMac mac, unsigned char *digest);

#endif /* SSHMAC_H */
