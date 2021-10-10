/*

  macs.c

  Author: Mika Kojo <mkojo@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Tue Mar 18 17:25:07 1997 [mkojo]

  Mac functions used by SSH Crypto Library.

  */

#include "sshincludes.h"
#include "sshcrypt.h"
#include "sshhash_i.h"
#include "sshmac_i.h"
#ifdef SSHDIST_CRYPT_MD5
#include "ike/md5.h"
#endif /* SSHDIST_CRYPT_MD5 */
#ifdef SSHDIST_CRYPT_SHA
#include "sha.h"
#endif /* SSHDIST_CRYPT_SHA */
#include "macs.h"

typedef struct
{
  const SshHashDefStruct *hash_def;
  void *hash_context;
  unsigned char *key;
  size_t keylen;
} SshKdkMacCtx;


size_t
ssh_kdk_mac_ctxsize(const SshHashDefStruct *hash_def)
{
  return
    sizeof(SshKdkMacCtx) +
    (*hash_def->ctxsize)();
}

void
ssh_kdk_mac_init(void *context, const unsigned char *key, size_t keylen,
                 const SshHashDefStruct *hash_def)
{
  SshKdkMacCtx *created = context;

  created->hash_context = (unsigned char *)created + sizeof(SshKdkMacCtx);
  created->key = (unsigned char *)created->hash_context +
    (*hash_def->ctxsize)();
  created->keylen = keylen;

  /* Copy key. */
  memcpy(created->key, key, keylen);

  /* Remember the hash function. */
  created->hash_def = (SshHashDefStruct *) hash_def;
}

void ssh_kdk_mac_start(void *context)
{
  SshKdkMacCtx *ctx = context;
  (*ctx->hash_def->reset_context)(ctx->hash_context);
  (*ctx->hash_def->update)(ctx->hash_context, ctx->key, ctx->keylen);
}

void ssh_kdk_mac_update(void *context, const unsigned char *buf,
                        size_t len)
{
  SshKdkMacCtx *ctx = context;
  (*ctx->hash_def->update)(ctx->hash_context, buf, len);
}

void ssh_kdk_mac_final(void *context, unsigned char *digest)
{
  SshKdkMacCtx *ctx = context;
  (*ctx->hash_def->update)(ctx->hash_context, ctx->key, ctx->keylen);
  (*ctx->hash_def->final)(ctx->hash_context, digest);
}

void ssh_kdk_mac_96_final(void *context, unsigned char *digest)
{
  SshKdkMacCtx *ctx = context;
  unsigned char buffer[SSH_MAX_HASH_DIGEST_LENGTH];
  (*ctx->hash_def->update)(ctx->hash_context, ctx->key, ctx->keylen);
  (*ctx->hash_def->final)(ctx->hash_context, buffer);
  memcpy(digest, buffer, 12);
}

void ssh_kdk_mac_64_final(void *context, unsigned char *digest)
{
  SshKdkMacCtx *ctx = context;
  unsigned char buffer[SSH_MAX_HASH_DIGEST_LENGTH];
  (*ctx->hash_def->update)(ctx->hash_context, ctx->key, ctx->keylen);
  (*ctx->hash_def->final)(ctx->hash_context, buffer);
  memcpy(digest, buffer, 8);
}

void ssh_kdk_mac_of_buffer(void *context, const unsigned char *buf,
                           size_t len, unsigned char *digest)
{
  ssh_kdk_mac_start(context);
  ssh_kdk_mac_update(context, buf, len);
  ssh_kdk_mac_final(context, digest);
}

void ssh_kdk_mac_96_of_buffer(void *context, const unsigned char *buf,
                              size_t len, unsigned char *digest)
{
  ssh_kdk_mac_start(context);
  ssh_kdk_mac_update(context, buf, len);
  ssh_kdk_mac_96_final(context, digest);
}

void ssh_kdk_mac_64_of_buffer(void *context, const unsigned char *buf,
                              size_t len, unsigned char *digest)
{
  ssh_kdk_mac_start(context);
  ssh_kdk_mac_update(context, buf, len);
  ssh_kdk_mac_64_final(context, digest);
}

#ifdef SSHDIST_CRYPT_MD5

/* XXX Some specific macs. These are becoming fast obsolete. */

#define MD5_OUTPUT_BLOCK_SIZE 16

/* MD5 MAC */

typedef struct SshMacMd5Rec
{
  unsigned char *key;
  size_t keylen;

  void *md5_context;
} SshMacMd5;

size_t ssh_mac_md5_ctxsize()
{
  return sizeof(SshMacMd5) + ssh_md5_ctxsize();
}

void ssh_mac_md5_init(void *context,
                      const unsigned char *key, size_t keylen)
{
  SshMacMd5 *created = context;

  memset(created, 0, sizeof(*created) + ssh_md5_ctxsize());
  created->md5_context = ((unsigned char *)created + sizeof(*created));
  created->key = (unsigned char *)created +
    sizeof(*created) + ssh_md5_ctxsize();
  memcpy(created->key, key, keylen);
  created->keylen = keylen;
}

void ssh_mac_md5_start(void *context)
{
  SshMacMd5 *ctx = context;

  ssh_md5_reset_context(ctx->md5_context);
  ssh_md5_update(ctx->md5_context, ctx->key, ctx->keylen);
}

void ssh_mac_md5_update(void *context, const unsigned char *buf,
                        size_t len)
{
  SshMacMd5 *ctx = context;

  ssh_md5_update(ctx->md5_context, buf, len);
}

void ssh_mac_md5_final(void *context, unsigned char *digest)
{
  SshMacMd5 *ctx = context;

  ssh_md5_update(ctx->md5_context, ctx->key, ctx->keylen);
  ssh_md5_final(ctx->md5_context, digest);
}

void ssh_mac_md5_of_buffer(void *context, const unsigned char *buf,
                           size_t len, unsigned char *digest)
{
  ssh_mac_md5_start(context);
  ssh_mac_md5_update(context, buf, len);
  ssh_mac_md5_final(context, digest);
}

/* MD5-8 */

#define SshMacMd5_8 SshMacMd5

void ssh_mac_md5_8_final(void *context, unsigned char *digest)
{
  SshMacMd5_8 *ctx = context;
  unsigned char tmp_digest[MD5_OUTPUT_BLOCK_SIZE];

  ssh_md5_update(ctx->md5_context, ctx->key, ctx->keylen);
  ssh_md5_final(ctx->md5_context, tmp_digest);

  memcpy(digest, tmp_digest, 8);
}

void ssh_mac_md5_8_of_buffer(void *context, const unsigned char *buf,
                             size_t len, unsigned char *digest)
{
  ssh_mac_md5_8_start(context);
  ssh_mac_md5_8_update(context, buf, len);
  ssh_mac_md5_8_final(context, digest);
}

#endif /* SSHDIST_CRYPT_MD5 */


#ifdef SSHDIST_CRYPT_SHA

#define SHA_OUTPUT_BLOCK_SIZE 20

/* SHA */

typedef struct SshMacShaRec
{
  unsigned char *key;
  size_t keylen;

  void *sha_context;
} SshMacSha;

size_t ssh_mac_sha_ctxsize()
{
  return sizeof(SshMacSha) + ssh_sha_ctxsize();
}

void ssh_mac_sha_init(void *context,
                      const unsigned char *key, size_t keylen)
{
  SshMacSha *created = context;

  memset(created, 0, sizeof(SshMacSha) + ssh_sha_ctxsize());
  created->sha_context = (unsigned char *)created + sizeof(SshMacSha);
  created->key = (unsigned char *)created +
    sizeof(SshMacSha) + ssh_sha_ctxsize();
  memcpy(created->key, key, keylen);
  created->keylen = keylen;
}

void ssh_mac_sha_start(void *context)
{
  SshMacSha *ctx = context;

  ssh_sha_reset_context(ctx->sha_context);
  ssh_sha_update(ctx->sha_context, ctx->key, ctx->keylen);
}

void ssh_mac_sha_update(void *context, const unsigned char *buf,
                        size_t len)
{
  SshMacSha *ctx = context;

  ssh_sha_update(ctx->sha_context, buf, len);
}

void ssh_mac_sha_final(void *context, unsigned char *digest)
{
  SshMacSha *ctx = context;

  ssh_sha_update(ctx->sha_context, ctx->key, ctx->keylen);
  ssh_sha_final(ctx->sha_context, digest);
}

void ssh_mac_sha_of_buffer(void *context, const unsigned char *buf,
                           size_t len, unsigned char *digest)
{
  ssh_mac_sha_start(context);
  ssh_mac_sha_update(context, buf, len);
  ssh_mac_sha_final(context, digest);
}

/* SHA-8 */

#define SshMacSha_8 SshMacSha

void ssh_mac_sha_8_final(void *context, unsigned char *digest)
{
  SshMacSha_8 *ctx = context;
  unsigned char tmp_digest[SHA_OUTPUT_BLOCK_SIZE];

  ssh_sha_update(ctx->sha_context, ctx->key, ctx->keylen);
  ssh_sha_final(ctx->sha_context, tmp_digest);

  memcpy(digest, tmp_digest, 8);
}

void ssh_mac_sha_8_of_buffer(void *context, const unsigned char *buf,
                             size_t len, unsigned char *digest)
{
  ssh_mac_sha_8_start(context);
  ssh_mac_sha_8_update(context, buf, len);
  ssh_mac_sha_8_final(context, digest);
}

#endif /* SSHDIST_CRYPT_SHA */

/* macs.c */
