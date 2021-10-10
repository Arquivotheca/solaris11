/*
  File: dl-generate.c

  Authors:
        Mika Kojo <mkojo@ssh.fi>
        Patrick Irwin <irwin@ssh.fi>

  Description:
        Discrete Logarithm Parameter and Key Generation

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                  All rights reserved.
*/

#include "sshincludes.h"
#include "sshmp.h"
#include "sshcrypt.h"
#include "sshpk_i.h"
#include "dlfix.h"
#include "dlglue.h"
#include "dl-internal.h"
#include "sshgenmp.h"

SshDLStackRandomizer *
ssh_cstack_SshDLStackRandomizer_constructor(void *context);

/* Precompute randomizer with parameters only, private key and public key. */

SshCryptoStatus ssh_dlp_param_generate_randomizer(void *parameters)
{
  /* Allocate stack element with constructor! */
  SshDLStackRandomizer *stack =
    ssh_cstack_SshDLStackRandomizer_constructor(NULL);
  SshDLParam param = parameters;

  if (!stack)
   return SSH_CRYPTO_NO_MEMORY;

retry:
  /* Add information to stack. */
  if (param->exponent_entropy)
    ssh_mprz_mod_random_entropy(&stack->k, &param->q,
                              param->exponent_entropy);
  else
    ssh_mprz_mod_random(&stack->k, &param->q);
  if (ssh_mprz_cmp_ui(&stack->k, 0) == 0)
    goto retry;

#ifndef SSHMATH_MINIMAL
  if (param->base_defined == FALSE)
    ssh_mprz_powm(&stack->gk, &param->g, &stack->k, &param->p);
  else
    ssh_mprz_powm_with_precomp(&stack->gk, &stack->k, param->base);
#else /* !SSHMATH_MINIMAL */
  ssh_mprz_powm(&stack->gk, &param->g, &stack->k, &param->p);
#endif /* !SSHMATH_MINIMAL */

  /* Push to stack list, in parameter context. No it is visible for
     all, private keys, public keys and parameters. */
  ssh_cstack_push(&param->stack, stack);
  return SSH_CRYPTO_OK;
}


SshCryptoStatus ssh_dlp_param_generate(int bits, int small_bits,
                                       SshDLParam *param_return,
                                       Boolean fips)
{
  SshDLParam param, temp;
  SshCryptoStatus status = SSH_CRYPTO_OPERATION_FAILED;

  if ((param = ssh_malloc(sizeof(*param))) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  ssh_dlp_init_param(param);

  if (fips)
    {
      if ((status = ssh_mp_fips186_random_strong_prime(&param->p, &param->q,
                                                       bits, small_bits))
          != SSH_CRYPTO_OK)
        {
          ssh_dlp_clear_param(param);
          ssh_free(param);
          return status;
        }
    }
  else
    {
      ssh_mprz_random_strong_prime(&param->p, &param->q, bits, small_bits);
    }

  if (ssh_mprz_random_generator(&param->g, &param->q, &param->p) != TRUE)
    {
      ssh_dlp_clear_param(param);
      ssh_free(param);
      return status;
    }

  temp = ssh_dlp_param_list_add(param);
  if (temp)
    {
      ssh_dlp_clear_param(param);
      ssh_free(param);
      param = temp;
    }

  *param_return = param;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_dlp_private_key_action_generate(void *context, void **key_ctx,
                                    Boolean dsa_key, Boolean fips)

{
  SshDLPInitCtx *ctx = context;
  SshCryptoStatus status;
  SshDLParam param;

  /* First generate paramters. */
  if (!ctx->predefined)
    {
      if (ssh_mprz_cmp_ui(&ctx->p, 0) == 0 ||
          ssh_mprz_cmp_ui(&ctx->q, 0) == 0 ||
          ssh_mprz_cmp_ui(&ctx->g, 0) == 0)
        {
          if (ctx->size)
            {
              unsigned int q_size;

              /* For DSA force subprime size to 160 bits, for others
                 make it half of size. That should depend on selected
                 policy, but seems to be pretty good tradeoff here. */
              if (dsa_key)
                {
                  q_size = 160;
                  if (ctx->size < q_size)
                    return SSH_CRYPTO_KEY_SIZE_INVALID;
                }
              else
                q_size = ctx->size / 2;

              if ((status = ssh_dlp_param_generate(ctx->size, q_size,
                                                   &param, fips))
                  != SSH_CRYPTO_OK)
                return status;
            }
          else
            return SSH_CRYPTO_OPERATION_FAILED;
        }
      else
        {
          if ((param = ssh_dlp_param_create(&ctx->p, &ctx->q, &ctx->g))
              == NULL)
            return SSH_CRYPTO_NO_MEMORY;
        }
    }
  else
    {
      if ((param = ssh_dlp_param_create_predefined(ctx->predefined)) == NULL)
        return SSH_CRYPTO_NO_MEMORY;
    }

  /* Then maybe generate private key components. */
  if (ssh_mprz_cmp_ui(&ctx->x, 0) == 0 || ssh_mprz_cmp_ui(&ctx->y, 0) == 0)
    {
      /* Generate secret key. Note, here we definitely don't want to
         use the restriction of random number size for the exponent.
         It would be a poor practice, some attack could find the
         discrete log faster that way.

         Well, that isn't the main point however, just that in
         Diffie-Hellman and signatures you are mainly using for short
         term security, but private keys might last for a long
         while. Thus for sake of clarity we don't do any restrictions
         here. */

      if (fips)
        {
          /* Use the algorithm described in Appendix 3.1 of FIPS 186-2 for
             generating the private key x.*/
          if (ssh_mp_cmp_ui(&ctx->x, 0) == 0)
            {
              status = ssh_mp_fips186_mod_random_private_value(&ctx->x,
                                                               &param->q);

              if (status != SSH_CRYPTO_OK)
                return status;
            }
        }
      else
        {
          if (ssh_mprz_cmp_ui(&ctx->x, 0) == 0)
            ssh_mprz_mod_random(&ctx->x, &param->q);
        }

      /* Compute the public key, y, using modular exponentation. */
#ifndef SSHMATH_MINIMAL
      if (param->base_defined)
        ssh_mprz_powm_with_precomp(&ctx->y, &ctx->x, param->base);
      else
        ssh_mprz_powm(&ctx->y, &param->g, &ctx->x, &param->p);
#else /* !SSHMATH_MINIMAL */
      ssh_mprz_powm(&ctx->y, &param->g, &ctx->x, &param->p);
#endif /* !SSHMATH_MINIMAL */
    }

  return ssh_dlp_action_make(context, param, 2, key_ctx);
}


SshCryptoStatus
ssh_dlp_private_key_action_generate_dsa_fips(void *context, void **key_ctx)
{
  return ssh_dlp_private_key_action_generate(context, key_ctx, TRUE, TRUE);
}

SshCryptoStatus
ssh_dlp_private_key_action_generate_dsa_std(void *context, void **key_ctx)
{
  return ssh_dlp_private_key_action_generate(context, key_ctx, TRUE, FALSE);
}

SshCryptoStatus
ssh_dlp_private_key_action_generate_std(void *context, void **key_ctx)
{
  return ssh_dlp_private_key_action_generate(context, key_ctx, FALSE, FALSE);
}
