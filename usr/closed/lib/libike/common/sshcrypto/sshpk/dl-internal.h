/*
  File: dl-internal.h

  Description:
        Discrete Logarithm Internal Header

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                  All rights reserved.
*/

#ifndef DL_INTERNAL_H
#define DL_INTERNAL_H

/* Crypto Stack for DL family functions. */
typedef unsigned int SshCStackToken;
typedef struct SshCStackRec
{
  SshCStackToken token;
  struct SshCStackRec *next;
  void (*destructor)(struct SshCStackRec *thisp);
} *SshCStack, SshCStackStruct;

/*
   Macros to make the prefix for the structure.

   SSH_CSTACK_BEGIN( stack )
   char *hello_world;
   SSH_CSTACK_END( stack );
*/

#define SSH_CSTACK_BEGIN(name) \
typedef struct name##Rec  \
{                         \
  SshCStackToken token;   \
  SshCStack next;         \
  void (*destructor)(SshCStack thisp);

#define SSH_CSTACK_END(name) \
} name

/*
   Macros for generating the destructor code for prefixes. These are
   called having 'type' some selected type name, which you are willing
   to use.  'name' some variable which you are willing to use. Then

   SSH_CSTACK_DESTRUCTOR_BEGIN( MyType, stack )
     free(stack->hello_world);
   SSH_CSTACK_DESTRUCTOR_END( MyType, stack )
   destroys your MyType structure.
*/

#define SSH_CSTACK_DESTRUCTOR_BEGIN(type, name)                 \
void ssh_cstack_##type##_destructor(SshCStack name##_cstack)    \
{                                                               \
  type *name = (type *)name##_cstack;                           \
  if (name) {                                                   \

#define SSH_CSTACK_DESTRUCTOR_END(type, name)                   \
    ssh_free(name);                                             \
  }                                                             \
}

/*
   Macros for generating the constructor code for prefixes. Generates
   constructor with name e.g.

     MyType *ssh_cstack_MyType_constructor();

   use as

   SSH_CSTACK_CONSTRUCTOR_BEGIN( MyType, stack, context,
                                 MY_TYPE_DISTINCT_TOKEN )
     stack->hello_world = NULL;
   SSH_CSTACK_CONSTRUCTOR_END( MyType, stack )

   Note! if name differs in _BEGIN and _END then compiler will state
   an error.
*/

#define SSH_CSTACK_CONSTRUCTOR_BEGIN(type,stack_name,context_name,t)    \
type *ssh_cstack_##type##_constructor(void *context_name)               \
{                                                                       \
  type *stack_name = ssh_malloc(sizeof(*stack_name));                   \
  if (stack_name) {                                                     \
     stack_name->token = t;                                             \
     stack_name->next = NULL;                                           \
     stack_name->destructor = ssh_cstack_##type##_destructor;           \

#define SSH_CSTACK_CONSTRUCTOR_END(type,stack_name)                     \
  }                                                                     \
  return stack_name;                                                    \
}

/* Push a element (this) into the stack pointed by (head). */
void ssh_cstack_push(SshCStack *head, void *thisp);

/* Pop element with (token) out of the stack. */
SshCStack ssh_cstack_pop(SshCStack *head, SshCStackToken token);

/* Free the full stack. */
void *ssh_cstack_free(void *head);

/* Count number of elements of type token in the stack. */
unsigned int ssh_cstack_count(SshCStack *head, SshCStackToken token);

typedef struct SshDlpInitCtxRec
{
  SshMPIntegerStruct p, g, q, x, y;
  unsigned int size;
  unsigned int exponent_entropy;
  const char *predefined;

} SshDLPInitCtx;

typedef struct SshDLParamRec
{
  struct SshDLParamRec *next, *prev;
  SshCStack stack;
  unsigned int reference_count;

  /* Predefined parameter sets have this defined. */
  const char *predefined;

  /* Actual parameter information. */
  SshMPIntegerStruct p;
  SshMPIntegerStruct g;
  SshMPIntegerStruct q;

  /* Precomputed. */
  Boolean base_defined;
  void *base;   /*  SshMPIntModPowPrecomp pointer */

  /* Information about the policy when generating random numbers. */
  unsigned int exponent_entropy;
} *SshDLParam, SshDLParamStruct;

/* Discrete Logarithm key structures. */

/* Public key:

   parameters and
   y - public key (g^x mod p)
   */

typedef struct SshDLPublicKeyRec
{
  SshDLParam param;
  SshMPIntegerStruct y;
} SshDLPublicKey;

/* Private key:

   parameters and
   y - public key (g^x mod p)
   x - private key
   */

typedef struct SshDLPrivateKeyRec
{
  SshDLParam param;
  SshMPIntegerStruct x;
  SshMPIntegerStruct y;
} SshDLPrivateKey;


void ssh_dlp_init_param(SshDLParam param);
void ssh_dlp_init_public_key(SshDLPublicKey *pub_key, SshDLParam param);
void ssh_dlp_init_private_key(SshDLPrivateKey *prv_key, SshDLParam param);

void ssh_dlp_clear_param(SshDLParam param);
void ssh_dlp_clear_public_key(SshDLPublicKey *pub_key);
void ssh_dlp_clear_private_key(SshDLPrivateKey *prv_key);

SshDLParam ssh_dlp_param_list_add(SshDLParam param);
SshDLParam ssh_dlp_param_create_predefined(const char *predefined);
SshDLParam ssh_dlp_param_create(SshMPIntegerConst p,
                                SshMPIntegerConst q,
                                SshMPIntegerConst g);

SshCryptoStatus ssh_dlp_action_make(SshDLPInitCtx *ctx,
                                    SshDLParam param,
                                    int type,
                                    void **return_ctx);

#define SSH_DLP_STACK_RANDOMIZER  0x1

/* Randomizer */

SSH_CSTACK_BEGIN( SshDLStackRandomizer )
  SshMPIntegerStruct k;
  SshMPIntegerStruct gk;
SSH_CSTACK_END( SshDLStackRandomizer );

#endif /* DL_INTERNAL_H */
