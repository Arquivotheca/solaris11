/*
  File: dl-utility.c

  Authors:
        Mika Kojo <mkojo@ssh.fi>
        Patrick Irwin <irwin@ssh.fi>

  Description:
        Discrete Logarithm Utility Functions

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
#include "sshencode.h"
#include "dl-internal.h"


/* Crypto Stack implementation. */

void ssh_cstack_push(SshCStack *head, void *thisp)
{
  SshCStack stack = thisp;

  stack->next = *head;
  *head = stack;
}

unsigned int ssh_cstack_count(SshCStack *head, SshCStackToken token)
{
  SshCStack temp;
  unsigned int count;

  for (temp = *head, count = 0; temp; temp = temp->next)
    if (temp->token == token)
      count++;
  return count;
}

SshCStack ssh_cstack_pop(SshCStack *head, SshCStackToken token)
{
  SshCStack temp, prev;

  temp = *head;
  prev = NULL;
  while (temp)
    {
      /* Compare */
      if (temp->token == token)
        {
          /* Remove from list (our stack). */
          if (prev)
            prev->next = temp->next;
          else
            *head = temp->next;
          temp->next = NULL;
          break;
        }
      prev = temp;
      temp = temp->next;
    }

  /* Return either NULL or valid stack entry. */
  return temp;
}

/* Free any stack element or stack itself! */
void *ssh_cstack_free(void *head)
{
  SshCStack temp, temp2;

  temp = head;
  while (temp)
    {
      temp2 = temp->next;
      /* Free. */
      (*temp->destructor)(temp);
      temp = temp2;
    }

  /* Tell upper-layer that all were freed successfully. */
  return NULL;
}

/* Allocation and deletion of stack elements. */

/* Randomizer */

SSH_CSTACK_DESTRUCTOR_BEGIN( SshDLStackRandomizer, stack )
     ssh_mprz_clear(&stack->k);
     ssh_mprz_clear(&stack->gk);
SSH_CSTACK_DESTRUCTOR_END( SshDLStackRandomizer, stack )

SSH_CSTACK_CONSTRUCTOR_BEGIN( SshDLStackRandomizer, stack, context,
                              SSH_DLP_STACK_RANDOMIZER )
     ssh_mprz_init(&stack->k);
     ssh_mprz_init(&stack->gk);
SSH_CSTACK_CONSTRUCTOR_END( SshDLStackRandomizer, stack )


/* Global parameter list. This will contain only _unique_ parameters,
   allowing the generation of randomizers in transparent way. */

static SshDLParam ssh_dlp_param_list = NULL;

#ifdef SSHDIST_PLATFORM_VXWORKS
#ifdef VXWORKS
#ifdef ENABLE_VXWORKS_RESTART_WATCHDOG
void ssh_dlp_restart(void)
{
  ssh_dlp_param_list = NULL;
}
#endif /* ENABLE_VXWORKS_RESTART_WATCHDOG */
#endif /* VXWORKS */
#endif /* SSHDIST_PLATFORM_VXWORKS */

/* Routines for parameter handling. */

void ssh_dlp_init_param(SshDLParam param)
{
  param->next = NULL;
  param->prev = NULL;
  param->stack = NULL;
  param->reference_count = 0;

  /* We assume that this parameter set is not predefined. */
  param->predefined = NULL;

  ssh_mprz_init(&param->p);
  ssh_mprz_init(&param->g);
  ssh_mprz_init(&param->q);

  param->base_defined = FALSE;

  /* Handle the entropy! Lets denote by zero that most secure settings
     should be used. */
  param->exponent_entropy = 0;
}

void ssh_dlp_param_add_ref(SshDLParam param)
{
  param->reference_count++;
}


/* Free parameter set only if reference count tells so. */
void ssh_dlp_clear_param(SshDLParam param)
{
  /* Keep the linked list updated. */
  if (param->prev)
    param->prev->next = param->next;
  else
    {
      /* In this case we might have the first entry in the
         parameter list either different or equal to the parameters
         in question. */
      if (ssh_dlp_param_list == param)
        ssh_dlp_param_list = param->next;
    }
  if (param->next)
    param->next->prev = param->prev;

  /* Free stack. */
  ssh_cstack_free(param->stack);

  ssh_mprz_clear(&param->p);
  ssh_mprz_clear(&param->g);
  ssh_mprz_clear(&param->q);

#ifndef SSHMATH_MINIMAL
  /* Clear the base. */
  if (param->base_defined)
    ssh_mprz_powm_precomp_destroy(param->base);
#endif /* !SSHMATH_MINIMAL */

  param->base_defined = FALSE;

  /* Clean pointers. */
  param->next  = NULL;
  param->prev  = NULL;
  param->stack = NULL;
}


SshDLParam ssh_dlp_param_list_add(SshDLParam param)
{
  SshDLParam temp;

  temp = ssh_dlp_param_list;
  while (temp)
    {
      if (ssh_mprz_cmp(&temp->p, &param->p) == 0 &&
          ssh_mprz_cmp(&temp->q, &param->q) == 0 &&
          ssh_mprz_cmp(&temp->g, &param->g) == 0 &&

          /* Must also check the policies! */
          temp->exponent_entropy == param->exponent_entropy)
        {
          return temp;
        }
      temp = temp->next;
    }

  /* Make first, that is this is the first incarnation of a
     parameter set with these settings. */
  param->next = ssh_dlp_param_list;
  if (ssh_dlp_param_list)
    ssh_dlp_param_list->prev = param;
  ssh_dlp_param_list = param;
  return NULL;
}

SshDLParam ssh_dlp_param_create_predefined(const char *predefined)
{
  SshDLParam param, temp;

  if ((param = ssh_malloc(sizeof(*param))) != NULL)
    {
      ssh_dlp_init_param(param);

      if (!ssh_dlp_set_param(predefined,
                             &param->predefined,
                             &param->p, &param->q, &param->g))
        {
          ssh_dlp_clear_param(param);
          ssh_free(param);
          return NULL;
        }
      temp = ssh_dlp_param_list_add(param);
      if (temp)
        {
          ssh_dlp_clear_param(param);
          ssh_free(param);
          param = temp;
        }
    }
  return param;
}

SshDLParam ssh_dlp_param_create(SshMPIntegerConst p,
                                SshMPIntegerConst q,
                                SshMPIntegerConst g)
{
  SshDLParam param, temp;

  if ((param = ssh_malloc(sizeof(*param))) != NULL)
    {
      ssh_dlp_init_param(param);
      ssh_mprz_set(&param->p, p);
      ssh_mprz_set(&param->q, q);
      ssh_mprz_set(&param->g, g);

      temp = ssh_dlp_param_list_add(param);
      if (temp)
        {
          ssh_dlp_clear_param(param);
          ssh_free(param);
          param = temp;
        }
    }
  return param;
}

SshCryptoStatus ssh_dlp_action_init(void **context)
{
  SshDLPInitCtx *ctx = ssh_malloc(sizeof(*ctx));

  if (ctx)
    {
      ctx->size = 0;
      ctx->exponent_entropy = 0;
      ctx->predefined = NULL;

      ssh_mprz_init_set_ui(&ctx->p, 0);
      ssh_mprz_init_set_ui(&ctx->g, 0);
      ssh_mprz_init_set_ui(&ctx->q, 0);
      ssh_mprz_init_set_ui(&ctx->x, 0);
      ssh_mprz_init_set_ui(&ctx->y, 0);

      *context = (void *)ctx;
      return SSH_CRYPTO_OK;
    }
  else
    {
      return SSH_CRYPTO_NO_MEMORY;
    }
}


void ssh_dlp_action_free(void *context)
{
  SshDLPInitCtx *ctx = context;

  ssh_mprz_clear(&ctx->p);
  ssh_mprz_clear(&ctx->q);
  ssh_mprz_clear(&ctx->g);
  ssh_mprz_clear(&ctx->x);
  ssh_mprz_clear(&ctx->y);

  ssh_free(ctx);
}

SshCryptoStatus ssh_dlp_action_public_key_init(void **context)
{
  return ssh_dlp_action_init(context);
}


/* Parameters are in a list, and contain the stack used in many
   operations.

   p - prime
   g - generator
   q - order of g (prime)

*/



/* Decode one parameter blob. */
size_t ssh_dlp_param_decode(const unsigned char *buf, size_t len,
                            SshDLParam param,
                            SshUInt32 value)
{
  size_t ret_value;
  char *predefined;

  if (value == 0)
    {
      return ssh_decode_array(buf, len,
                              SSH_FORMAT_SPECIAL,
                              ssh_mprz_decode_rendered, &param->p,
                              SSH_FORMAT_SPECIAL,
                              ssh_mprz_decode_rendered, &param->g,
                              SSH_FORMAT_SPECIAL,
                              ssh_mprz_decode_rendered, &param->q,
                              SSH_FORMAT_END);
    }
  else
    {
      ret_value = ssh_decode_array(buf, len,
                                   SSH_FORMAT_UINT32_STR, &predefined, NULL,
                                   SSH_FORMAT_END);
      if (ret_value != 0)
        {
          if (ssh_dlp_set_param(predefined, &param->predefined,
                                &param->p, &param->q,
                                &param->g) == FALSE)
            {
              ssh_free(predefined);
              return 0;
            }
        }
      else
        return ret_value;

      ssh_free(predefined);
      return ret_value;
    }
}

void ssh_dlp_param_encode(SshBuffer buffer, const SshDLParamStruct *param)
{
  if (param->predefined)
    {
      ssh_encode_buffer(buffer,
                        SSH_FORMAT_UINT32, (SshUInt32) 1,
                        SSH_FORMAT_UINT32_STR, param->predefined,
                        strlen(param->predefined),
                        SSH_FORMAT_END);
    }
  else
    {
      ssh_encode_buffer(buffer,
                        SSH_FORMAT_UINT32, (SshUInt32) 0,
                        SSH_FORMAT_SPECIAL,
                        ssh_mprz_encode_rendered, &param->p,
                        SSH_FORMAT_SPECIAL,
                        ssh_mprz_encode_rendered, &param->g,
                        SSH_FORMAT_SPECIAL,
                        ssh_mprz_encode_rendered, &param->q,
                        SSH_FORMAT_END);
    }
}

SshCryptoStatus
ssh_dlp_param_import(const unsigned char *buf, size_t len,
                     void **parameters)
{
  SshDLParam param, temp;
  SshUInt32 value;
  size_t parsed;

  if ((param = ssh_malloc(sizeof(*param))) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  ssh_dlp_init_param(param);

  /* Decode */
  parsed = ssh_decode_array(buf, len,
                            SSH_FORMAT_UINT32, &value,
                            SSH_FORMAT_END);
  if (parsed == 0)
    {
    error:
      ssh_dlp_clear_param(param);
      ssh_free(param);
      return SSH_CRYPTO_OPERATION_FAILED;
    }

  parsed += ssh_dlp_param_decode(buf + parsed, len - parsed, param, value);
  if (parsed != len)
    goto error;

  /* Check the global parameter list, if already exists then just use
     reference counting. */
  if ((temp = ssh_dlp_param_list_add(param)) != NULL)
    {
      ssh_dlp_clear_param(param);
      ssh_free(param);
      param = temp;
    }
  ssh_dlp_param_add_ref(param);

  /* Reading was successful. */
  *parameters = (void *)param;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus ssh_dlp_param_export(const void *parameters,
                                     unsigned char **buf,
                                     size_t *length_return)
{
  const SshDLParamStruct *param = parameters;
  SshBufferStruct buffer;

  ssh_buffer_init(&buffer);
  ssh_dlp_param_encode(&buffer, param);

  if ((*length_return = ssh_buffer_len(&buffer)) != 0)
    {
      *buf = ssh_memdup(ssh_buffer_ptr(&buffer), ssh_buffer_len(&buffer));
      if (*buf == NULL) *length_return = 0;
    }
  ssh_buffer_uninit(&buffer);

  if (*length_return != 0)
    return SSH_CRYPTO_OK;

  return SSH_CRYPTO_OPERATION_FAILED;
}

void ssh_dlp_param_free(void *parameters)
{
  SshDLParam param = parameters;

  if (param->reference_count == 0)
    ssh_fatal("ssh_dlp_param_free: reference counting failed.");

  if (--param->reference_count > 0)
    return;

  ssh_dlp_clear_param(param);
  ssh_free(parameters);
}

SshCryptoStatus ssh_dlp_param_copy(void *param_src, void **param_dest)
{
  SshDLParam param = param_src;

  ssh_dlp_param_add_ref(param);
  *param_dest = param_src;

  return SSH_CRYPTO_OK;
}


/* Discrete Logarithms key control functions. */

void ssh_dlp_init_public_key(SshDLPublicKey *pub_key, SshDLParam param)
{
  /* Reference count, parameter indexed from here also. */
  ssh_dlp_param_add_ref(param);
  pub_key->param = param;
  ssh_mprz_init(&pub_key->y);
}

void ssh_dlp_clear_public_key(SshDLPublicKey *pub_key)
{
  ssh_mprz_clear(&pub_key->y);
  ssh_dlp_param_free(pub_key->param);
}

void ssh_dlp_init_private_key(SshDLPrivateKey *prv_key, SshDLParam param)
{
  /* Reference count, parameter indexed from here also. */
  ssh_dlp_param_add_ref(param);
  prv_key->param = param;
  ssh_mprz_init(&prv_key->y);
  ssh_mprz_init(&prv_key->x);
}

void ssh_dlp_clear_private_key(SshDLPrivateKey *prv_key)
{
  ssh_mprz_clear(&prv_key->y);
  ssh_mprz_clear(&prv_key->x);
  ssh_dlp_param_free(prv_key->param);
}

/* Public key primitives. */

SshCryptoStatus ssh_dlp_public_key_import(const unsigned char *buf,
                                          size_t len,
                                          void **public_key)
{
  SshDLPublicKey *pub_key;
  SshDLParam param, temp;
  SshMPIntegerStruct y;
  SshUInt32 value;
  size_t parsed;

  if ((param = ssh_malloc(sizeof(*param))) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  if ((pub_key = ssh_malloc(sizeof(*pub_key))) == NULL)
    {
      ssh_free(param);
      return SSH_CRYPTO_NO_MEMORY;
    }

  ssh_dlp_init_param(param);
  ssh_mprz_init(&y);

  parsed = ssh_decode_array(buf, len,
                            SSH_FORMAT_UINT32, &value,
                            SSH_FORMAT_END);

  if (parsed == 0)
    {
    error:
      ssh_free(pub_key);
      ssh_mprz_clear(&y);
      ssh_dlp_clear_param(param);
      ssh_free(param);
      return SSH_CRYPTO_OPERATION_FAILED;
    }
  parsed += ssh_dlp_param_decode(buf + parsed, len - parsed, param, value);
  parsed += ssh_decode_array(buf + parsed, len - parsed,
                             SSH_FORMAT_SPECIAL, ssh_mprz_decode_rendered, &y,
                             SSH_FORMAT_END);
  if (parsed != len)
    goto error;

  /* Verify that this is unique parameter set. */
  temp = ssh_dlp_param_list_add(param);
  if (temp)
    {
      ssh_dlp_clear_param(param);
      ssh_free(param);
      param = temp;
    }
  ssh_dlp_init_public_key(pub_key, param);
  ssh_mprz_set(&pub_key->y, &y);
  ssh_mprz_clear(&y);

  /* Reading was successful. */
  *public_key = (void *)pub_key;

  return SSH_CRYPTO_OK;
}

SshCryptoStatus ssh_dlp_public_key_export(const void *public_key,
                                          unsigned char **buf,
                                          size_t *length_return)
{
  const SshDLPublicKey *pub_key = public_key;
  SshBufferStruct buffer;

  ssh_buffer_init(&buffer);
  ssh_dlp_param_encode(&buffer, pub_key->param);
  ssh_encode_buffer(&buffer,
                    SSH_FORMAT_SPECIAL, ssh_mprz_encode_rendered, &pub_key->y,
                    SSH_FORMAT_END);

  if ((*length_return = ssh_buffer_len(&buffer)) != 0)
    {
      *buf = ssh_memdup(ssh_buffer_ptr(&buffer), ssh_buffer_len(&buffer));
      if (*buf == NULL) *length_return = 0;
    }

  ssh_buffer_uninit(&buffer);

  if (*length_return != 0)
    return SSH_CRYPTO_OK;

  return SSH_CRYPTO_OPERATION_FAILED;
}

void ssh_dlp_public_key_free(void *public_key)
{
  ssh_dlp_clear_public_key((SshDLPublicKey *)public_key);
  ssh_free(public_key);
}

SshCryptoStatus
ssh_dlp_public_key_copy(void *public_key_src, void **public_key_dest)
{
  SshDLPublicKey *pub_src = public_key_src;
  SshDLPublicKey *pub_dest;

  if ((pub_dest = ssh_malloc(sizeof(*pub_dest))) != NULL)
    {
      ssh_dlp_init_public_key(pub_dest, pub_src->param);
      ssh_mprz_set(&pub_dest->y, &pub_src->y);
    }
  else
    {
      return SSH_CRYPTO_NO_MEMORY;
    }

  *public_key_dest = (void *)pub_dest;
  return SSH_CRYPTO_OK;
}

/* Derive parameters from public key. */
SshCryptoStatus ssh_dlp_public_key_derive_param(void *public_key,
                                                void **parameters)
{
  SshDLPublicKey *pub_key = public_key;
  SshDLParam param = pub_key->param;

  /* Reference count... */
  ssh_dlp_param_add_ref(param);
  *parameters = (void *)param;
  return SSH_CRYPTO_OK;
}

/* Private key primitives. */

SshCryptoStatus
ssh_dlp_private_key_import(const unsigned char *buf,
                           size_t len,
                           void **private_key)
{
  SshDLPrivateKey *prv_key;
  SshDLParam param, temp;
  SshMPIntegerStruct x, y;
  SshUInt32 value;
  size_t parsed;

  if ((param = ssh_malloc(sizeof(*param))) == NULL)
      return SSH_CRYPTO_NO_MEMORY;
  if ((prv_key = ssh_malloc(sizeof(*prv_key))) == NULL)
    {
      ssh_free(param);
      return SSH_CRYPTO_NO_MEMORY;
    }

  /* Temporary variables. */
  ssh_mprz_init(&x);
  ssh_mprz_init(&y);

  ssh_dlp_init_param(param);

  parsed = ssh_decode_array(buf, len,
                            SSH_FORMAT_UINT32, &value,
                            SSH_FORMAT_END);
  if (parsed == 0)
    {
    error:
      ssh_mprz_clear(&x);
      ssh_mprz_clear(&y);
      ssh_dlp_clear_param(param);
      ssh_free(param);
      ssh_free(prv_key);
      return SSH_CRYPTO_OPERATION_FAILED;
    }
  parsed += ssh_dlp_param_decode(buf + parsed, len - parsed, param, value);
  parsed += ssh_decode_array(buf + parsed, len - parsed,
                             SSH_FORMAT_SPECIAL, ssh_mprz_decode_rendered, &y,
                             SSH_FORMAT_SPECIAL, ssh_mprz_decode_rendered, &x,
                             SSH_FORMAT_END);
  if (parsed != len)
    goto error;

  /* Check that param is unique and add to list or output param set
     that is equal and already exists in the list. */
  temp = ssh_dlp_param_list_add(param);
  if (temp)
    {
      ssh_dlp_clear_param(param);
      ssh_free(param);
      param = temp;
    }
  ssh_dlp_init_private_key(prv_key, param);
  ssh_mprz_set(&prv_key->x, &x);
  ssh_mprz_set(&prv_key->y, &y);

  ssh_mprz_clear(&x);
  ssh_mprz_clear(&y);

  *private_key = (void *)prv_key;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus ssh_dlp_private_key_export(const void *private_key,
                                           unsigned char **buf,
                                           size_t *length_return)
{
  const SshDLPrivateKey *prv_key = private_key;
  SshBufferStruct buffer;

  ssh_buffer_init(&buffer);
  ssh_dlp_param_encode(&buffer, prv_key->param);
  ssh_encode_buffer(&buffer,
                    SSH_FORMAT_SPECIAL, ssh_mprz_encode_rendered, &prv_key->y,
                    SSH_FORMAT_SPECIAL, ssh_mprz_encode_rendered, &prv_key->x,
                    SSH_FORMAT_END);

  if ((*length_return = ssh_buffer_len(&buffer)) != 0)
    {
      *buf = ssh_memdup(ssh_buffer_ptr(&buffer), ssh_buffer_len(&buffer));
      if (*buf == NULL) *length_return = 0;
    }

  ssh_buffer_uninit(&buffer);

  if (*length_return != 0)
    return SSH_CRYPTO_OK;

  return SSH_CRYPTO_OPERATION_FAILED;
}

void ssh_dlp_private_key_free(void *private_key)
{
  ssh_dlp_clear_private_key((SshDLPrivateKey *)private_key);
  ssh_free(private_key);
}

SshCryptoStatus
ssh_dlp_private_key_copy(void *private_key_src, void **private_key_dest)
{
  SshDLPrivateKey *prv_src = private_key_src;
  SshDLPrivateKey *prv_dest = ssh_malloc(sizeof(*prv_dest));

  if (prv_dest)
    {
      ssh_dlp_init_private_key(prv_dest, prv_src->param);
      ssh_mprz_set(&prv_dest->x, &prv_src->x);
      ssh_mprz_set(&prv_dest->y, &prv_src->y);
    }
  else
    {
      return SSH_CRYPTO_NO_MEMORY;
    }

  *private_key_dest = (void *)prv_dest;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_dlp_private_key_derive_public_key(const void *private_key,
                                      void **public_key)
{
  SshDLPublicKey *pub_key = ssh_malloc(sizeof(*pub_key));
  const SshDLPrivateKey *prv_key = private_key;

  if (pub_key)
    {
      ssh_dlp_init_public_key(pub_key, prv_key->param);
      ssh_mprz_set(&pub_key->y, &prv_key->y);
    }
  else
    {
      return SSH_CRYPTO_NO_MEMORY;
    }

  *public_key = (void *)pub_key;
  return SSH_CRYPTO_OK;
}

/* Derive parameters from a private key. */
SshCryptoStatus
ssh_dlp_private_key_derive_param(void *private_key,
                                 void **parameters)
{
  SshDLPrivateKey *prv_key = private_key;
  SshDLParam param = prv_key->param;

  ssh_dlp_param_add_ref(param);
  *parameters = (void *)param;
  return SSH_CRYPTO_OK;
}




/********************** Discrete Logarithm ********************/

/* Discrete Logarithm parameter structures. */


/* Finally something that can use our nice ;) stack approach. */

unsigned int ssh_dlp_param_count_randomizers(void *parameters)
{
  return ssh_cstack_count(&((SshDLParam)parameters)->stack,
                          SSH_DLP_STACK_RANDOMIZER);
}

SshCryptoStatus
ssh_dlp_param_export_randomizer(void *parameters,
                                unsigned char **buf,
                                size_t *length_return)
{
  SshDLStackRandomizer *stack;
  SshDLParam param = parameters;

  stack = (SshDLStackRandomizer *)ssh_cstack_pop(&param->stack,
                                                 SSH_DLP_STACK_RANDOMIZER);
  if (stack)
    {
      *length_return =
        ssh_encode_array_alloc(buf,
                               SSH_FORMAT_SPECIAL,
                               ssh_mprz_encode_rendered, &stack->k,
                               SSH_FORMAT_SPECIAL,
                               ssh_mprz_encode_rendered, &stack->gk,
                               SSH_FORMAT_END);
      ssh_cstack_free(stack);
      return SSH_CRYPTO_OK;
    }
  *buf = NULL;
  *length_return = 0;
  return SSH_CRYPTO_OPERATION_FAILED;
}

SshCryptoStatus
ssh_dlp_param_import_randomizer(void *parameters,
                                const unsigned char *buf, size_t length)
{
  SshDLStackRandomizer *stack =
    ssh_cstack_SshDLStackRandomizer_constructor(NULL);
  SshDLParam param = parameters;

  if (ssh_decode_array(buf, length,
                       SSH_FORMAT_SPECIAL,
                       ssh_mprz_decode_rendered, &stack->k,
                       SSH_FORMAT_SPECIAL,
                       ssh_mprz_decode_rendered, &stack->gk,
                       SSH_FORMAT_END) == 0)
    {
      ssh_cstack_free(stack);
      return SSH_CRYPTO_OPERATION_FAILED;
    }

  ssh_cstack_push(&param->stack, stack);
  return SSH_CRYPTO_OK;
}


char *ssh_dlp_action_put(void *context, va_list ap,
                         void *input_context,
                         SshCryptoType type,
                         SshPkFormat format)
{

  SshDLPInitCtx *ctx = context;
  SshMPInteger temp;
  char *r;

  r = "p";
  switch (format)
    {
    case SSH_PKF_SIZE:
      if (type & SSH_CRYPTO_TYPE_PUBLIC_KEY)
        return NULL;
      ctx->size = va_arg(ap, unsigned int);
      r = "i";
      break;
    case SSH_PKF_RANDOMIZER_ENTROPY:
      ctx->exponent_entropy = va_arg(ap, unsigned int);
      r = "i";
      /* In case the application suggests too small entropy value
         lets force the maximum. Clearly the application didn't know
         what it was doing. */
      if (ctx->exponent_entropy < SSH_RANDOMIZER_MINIMUM_ENTROPY)
        ctx->exponent_entropy = 0;
      break;
    case SSH_PKF_PRIME_P:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(&ctx->p, temp);
      break;
    case SSH_PKF_PRIME_Q:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(&ctx->q, temp);
      break;
    case SSH_PKF_GENERATOR_G:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(&ctx->g, temp);
      break;
    case SSH_PKF_SECRET_X:
      if (type & (SSH_CRYPTO_TYPE_PUBLIC_KEY | SSH_CRYPTO_TYPE_PK_GROUP))
        return NULL;
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(&ctx->x, temp);
      break;
    case SSH_PKF_PUBLIC_Y:
      if (type & SSH_CRYPTO_TYPE_PK_GROUP)
        return NULL;
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(&ctx->y, temp);
      break;
    case SSH_PKF_PREDEFINED_GROUP:
      ctx->predefined = va_arg(ap, const char *);
      break;
    default:
      return NULL;
      break;
    }
  return r;
}

const char *
ssh_dlp_action_private_key_put(void *context, va_list ap,
                               void *input_context,
                               SshPkFormat format)
{
  return ssh_dlp_action_put(context, ap,
                            input_context,
                            SSH_CRYPTO_TYPE_PRIVATE_KEY,
                            format);
}

const char *
ssh_dlp_action_private_key_get(void *context, va_list ap,
                               void **output_context,
                               SshPkFormat format)
{
  SshDLPrivateKey *prv = context;
  SshMPInteger temp;
  unsigned int *size;
  char *r;

  r = "p";
  switch (format)
    {
    case SSH_PKF_SIZE:
      size = va_arg(ap, unsigned int *);
      *size = ssh_mprz_bit_size(&prv->param->p);
      break;
    case SSH_PKF_RANDOMIZER_ENTROPY:
      size = va_arg(ap, unsigned int *);
      if (!prv->param->exponent_entropy)
        /* In case the entropy is the maximal possible, lets fool the
           application to think that we really think in terms of
           bits for this case also. */
        *size = ssh_mprz_bit_size(&prv->param->q);
      else
        /* Otherwise lets just give the real value used. */
        *size = prv->param->exponent_entropy;
      break;
    case SSH_PKF_PRIME_P:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &prv->param->p);
      break;
    case SSH_PKF_PRIME_Q:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &prv->param->q);
      break;
    case SSH_PKF_GENERATOR_G:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &prv->param->g);
      break;
    case SSH_PKF_SECRET_X:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &prv->x);
      break;
    case SSH_PKF_PUBLIC_Y:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &prv->y);
      break;
    default:
      return NULL;
      break;
    }
  return r;
}

const char *
ssh_dlp_action_public_key_put(void *context, va_list ap,
                              void *input_context,
                              SshPkFormat format)
{
  return ssh_dlp_action_put(context, ap,
                            input_context,
                            SSH_CRYPTO_TYPE_PUBLIC_KEY,
                            format);
}

const char *
ssh_dlp_action_public_key_get(void *context, va_list ap,
                              void **output_context,
                              SshPkFormat format)
{
  SshDLPublicKey *pub = context;
  SshMPInteger temp;
  unsigned int *size;
  char *r;

  r = "p";
  switch (format)
    {
    case SSH_PKF_SIZE:
      size = va_arg(ap, unsigned int *);
      *size = ssh_mprz_bit_size(&pub->param->p);
      break;
    case SSH_PKF_RANDOMIZER_ENTROPY:
      size = va_arg(ap, unsigned int *);
      if (!pub->param->exponent_entropy)
        /* In case the entropy is the maximal possible, lets fool the
           application to think that we really think in terms of
           bits for this case also. */
        *size = ssh_mprz_bit_size(&pub->param->q);
      else
        /* Otherwise lets just give the real value used. */
        *size = pub->param->exponent_entropy;
      break;
    case SSH_PKF_PRIME_P:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &pub->param->p);
      break;
    case SSH_PKF_PRIME_Q:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &pub->param->q);
      break;
    case SSH_PKF_GENERATOR_G:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &pub->param->g);
      break;
    case SSH_PKF_PUBLIC_Y:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &pub->y);
      break;
    default:
      return NULL;
      break;
    }
  return r;
}

const char *
ssh_dlp_action_param_put(void *context, va_list ap,
                         void *input_context,
                         SshPkFormat format)
{
  return ssh_dlp_action_put(context, ap,
                            input_context,
                            SSH_CRYPTO_TYPE_PK_GROUP,
                            format);
}

const char *
ssh_dlp_action_param_get(void *context, va_list ap,
                         void **output_context,
                         SshPkFormat format)
{
  SshDLParam param = context;
  SshMPInteger temp;
  unsigned int *size;
  char *r;

  r = "p";
  switch (format)
    {
    case SSH_PKF_SIZE:
      size = va_arg(ap, unsigned int *);
      *size = ssh_mprz_bit_size(&param->p);
      break;
    case SSH_PKF_RANDOMIZER_ENTROPY:
      size = va_arg(ap, unsigned int *);
      if (!param->exponent_entropy)
        /* In case the entropy is the maximal possible, lets fool the
           application to think that we really think in terms of
           bits for this case also. */
        *size = ssh_mprz_bit_size(&param->q);
      else
        /* Otherwise lets just give the real value used. */
        *size = param->exponent_entropy;
      break;
    case SSH_PKF_PRIME_P:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &param->p);
      break;
    case SSH_PKF_PRIME_Q:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &param->q);
      break;
    case SSH_PKF_GENERATOR_G:
      temp = va_arg(ap, SshMPInt);
      ssh_mprz_set(temp, &param->g);
      break;
    default:
      return NULL;
      break;
    }
  return r;
}

/* Make DL key of identified type from given context and paramters. */
SshCryptoStatus ssh_dlp_action_make(SshDLPInitCtx *ctx,
                                    SshDLParam param,
                                    int type,
                                    void **ret_ctx)
{
  SshDLPrivateKey *prv_key;
  SshDLPublicKey *pub_key;

  /* Check constraints of type. */
  switch (type)
    {
    case 0: break;
    case 1:
      if (ssh_mprz_cmp_ui(&ctx->y, 0) == 0)
        return SSH_CRYPTO_KEY_INVALID;
      break;
    case 2:
      if ((ssh_mprz_cmp_ui(&ctx->x, 0) == 0) ||
          (ssh_mprz_cmp_ui(&ctx->y, 0) == 0))
        return SSH_CRYPTO_KEY_INVALID;
      break;
    default:
      return SSH_CRYPTO_OPERATION_FAILED;
    }

  /* Finish the parameter generation with setting the policy information. */
  if (ctx->exponent_entropy > ssh_mprz_bit_size(&param->q))
    ctx->exponent_entropy = ssh_mprz_bit_size(&param->q);
  param->exponent_entropy = ctx->exponent_entropy;

  /* Handle the cases for private and public keys. */
  switch (type)
    {
    case 0:
      /* Parameters made. Increase reference count. */
      ssh_dlp_param_add_ref(param);
      *ret_ctx = (void *)param;
      return SSH_CRYPTO_OK;

    case 1:
      /* The public key stuff. */
      if ((pub_key = ssh_malloc(sizeof(*pub_key))) != NULL)
        {
          ssh_dlp_init_public_key(pub_key, param);
          ssh_mprz_set(&pub_key->y, &ctx->y);
          *ret_ctx = (void *)pub_key;
          return SSH_CRYPTO_OK;
        }
      return SSH_CRYPTO_NO_MEMORY;

    case 2:
      /* The private key stuff. */
      if ((prv_key = ssh_malloc(sizeof(*prv_key))) != NULL)
        {
          ssh_dlp_init_private_key(prv_key, param);
          ssh_mprz_set(&prv_key->x, &ctx->x);
          ssh_mprz_set(&prv_key->y, &ctx->y);
          *ret_ctx = (void *)prv_key;
          return SSH_CRYPTO_OK;
        }
      return SSH_CRYPTO_NO_MEMORY;
    }
  return SSH_CRYPTO_OPERATION_FAILED;
}

SshCryptoStatus
ssh_dlp_action_make_param(void *context, int which, void **ret_ctx)
{
  SshDLPInitCtx *ctx = context;
  SshDLParam param;

  if (ctx->predefined)
    param = ssh_dlp_param_create_predefined(ctx->predefined);
  else
    param = ssh_dlp_param_create(&ctx->p, &ctx->q, &ctx->g);

  if (param)
    return ssh_dlp_action_make(context, param, which, ret_ctx);
  else
    return SSH_CRYPTO_OPERATION_FAILED;
}

SshCryptoStatus ssh_dlp_private_key_action_define(void *context,
                                                  void **key_ctx)
{
  return ssh_dlp_action_make_param(context, 2, key_ctx);
}

SshCryptoStatus ssh_dlp_public_key_action_make(void *context, void **key_ctx)
{
  return ssh_dlp_action_make_param(context, 1, key_ctx);
}

SshCryptoStatus ssh_dlp_param_action_make(void *context, void **key_ctx)
{
  return ssh_dlp_action_make_param(context, 0, key_ctx);
}

/********************* Precomputation ******************/

SshCryptoStatus ssh_dlp_param_precompute(void *context)
{
  SshDLParam param = context;

  if (param->base_defined)
    return SSH_CRYPTO_OK;

#ifndef SSHMATH_MINIMAL
  if ((param->base =
       ssh_mprz_powm_precomp_create(&param->g, &param->p, &param->q))
      != NULL)
    param->base_defined = TRUE;
#endif /* !SSHMATH_MINIMAL */

  return SSH_CRYPTO_OK;
}

SshCryptoStatus ssh_dlp_public_key_precompute(void *context)
{
  SshDLPublicKey *key = context;

  return ssh_dlp_param_precompute(key->param);
}

SshCryptoStatus ssh_dlp_private_key_precompute(void *context)
{
  SshDLPrivateKey *key = context;

  return ssh_dlp_param_precompute(key->param);
}
