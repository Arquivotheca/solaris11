/*

  genpkcs.c

  Author: Mika Kojo     <mkojo@ssh.fi>
          Patrick Irwin <irwin@ssh.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
  All rights reserved.

  Created: Mon Jun  2 18:43:45 1997 [mkojo]

  Interface code for public key cryptosystems.

*/

#include "sshincludes.h"
#include "sshdsprintf.h"
#include "sshbuffer.h"
#include "sshcrypt.h"
#include "sshcrypt_i.h"
#include "sshpk_i.h"
#include "sshrandom_i.h"
#include "crypto_tests.h"
#include "sshrgf.h"


#define SSH_DEBUG_MODULE "SshCryptoGenpkcs"

/************************************************************************/

SshPkType const *ssh_pk_type_slots[SSH_PK_TYPE_MAX_SLOTS] =
{
  NULL, /* ... continued to the end. */
};

SshCryptoStatus
ssh_pk_provider_register(const SshPkType *type)
{
  int i;







  if (type == NULL)
    return SSH_CRYPTO_OPERATION_FAILED;

  for (i = 0; i < SSH_PK_TYPE_MAX_SLOTS; i++)
    {
      if (ssh_pk_type_slots[i] == NULL)
        {
          /* Empty slot detected. */
          ssh_pk_type_slots[i] = type;
          return SSH_CRYPTO_OK;
        }

      if (ssh_pk_type_slots[i] == type)
        /* Same type added already. */
        return SSH_CRYPTO_OK;
    }

  return SSH_CRYPTO_PROVIDER_SLOTS_EXHAUSTED;
}

/* Search from action list an entry with the given format and
   that has at least 'flags' on. */
const SshPkAction *ssh_pk_find_action(const SshPkAction *list,
                                      SshPkFormat format,
                                      SshPkActionFlag flags)
{
  unsigned int i;

  for (i = 0; list[i].format != SSH_PKF_END; i++)
    {
      /* Check whether the format and flags match. */
      if (list[i].format == format && (list[i].flags & flags) == flags)
        {
          /* Found a correct match (because they are assumed to be unique
             this must be correct). */
          return &list[i];
        }
    }
  /* Failed to find a match. */
  return NULL;
}



/****** Functions for getting and setting the scheme information. *********/


/* Returns a pointer to the scheme in type characterized by format
   and with name scheme_name. We treat the signature, encryption and
   diffie-hellman scheme seperately, rather than a generic approach using
   void pointers. We are assuming that the number of schemes will in the
   future stay small, and this seems likely. */
void * ssh_pk_find_scheme(const SshPkType *type,
                          SshPkFormat format,
                          const char *scheme_name)

{
  unsigned int i;

  if (type == NULL || scheme_name == NULL)
    return NULL;

  if (format == SSH_PKF_SIGN && type->signature_list)
    {
      for (i = 0; type->signature_list[i].name != NULL; i++)
        {
          if (strcmp(type->signature_list[i].name, scheme_name) == 0)
            return (void *) &type->signature_list[i];
        }
    }

  if (format == SSH_PKF_ENCRYPT && type->encryption_list)
    {
      for (i = 0; type->encryption_list[i].name; i++)
        {
          if (strcmp(type->encryption_list[i].name, scheme_name) == 0)
            return (void *) &type->encryption_list[i];
        }
    }

  if (format == SSH_PKF_DH && type->diffie_hellman_list)
    {
      for (i = 0; type->diffie_hellman_list[i].name; i++)
        {
          if (strcmp(type->diffie_hellman_list[i].name, scheme_name) == 0)
            return (void *) &type->diffie_hellman_list[i];
        }
    }

  return NULL;
}


const char * ssh_private_key_find_default_scheme(SshPrivateKeyObject key,
                                                 SshPkFormat format)
{
  const SshPkType *type = key->type;

  if (type == NULL)
    return NULL;

  if (format == SSH_PKF_SIGN && type->signature_list)
      return type->signature_list[0].name;

  if (format == SSH_PKF_ENCRYPT && type->encryption_list)
    return type->encryption_list[0].name;

  if (format == SSH_PKF_DH && type->diffie_hellman_list)
    return type->diffie_hellman_list[0].name;

  return NULL;

}

SshCryptoStatus
ssh_private_key_set_scheme(SshPrivateKeyObject key,
                           SshPkFormat format, const char *scheme)
{
  void *scheme_ptr;

  /* Get the scheme pointer. */
  scheme_ptr = ssh_pk_find_scheme(key->type, format, scheme);

  if (scheme && !scheme_ptr)
    return SSH_CRYPTO_SCHEME_UNKNOWN;

  /* Set the corresponding scheme. */
  switch (format)
    {
    case SSH_PKF_SIGN:
      key->signature = scheme_ptr;
      break;
    case SSH_PKF_ENCRYPT:
      key->encryption = scheme_ptr;
      break;
    case SSH_PKF_DH:
      key->diffie_hellman = scheme_ptr;
      break;
    default:
      return SSH_CRYPTO_SCHEME_UNKNOWN;
      break;
    }
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_public_key_set_scheme(SshPublicKeyObject key,
                          SshPkFormat format, const char *scheme)
{
  void *scheme_ptr;

  /* Get the scheme pointer. */
  scheme_ptr = ssh_pk_find_scheme(key->type, format, scheme);

  if (scheme && !scheme_ptr)
    return SSH_CRYPTO_SCHEME_UNKNOWN;

  /* Set the corresponding scheme. */
  switch (format)
    {
    case SSH_PKF_SIGN:
      key->signature = scheme_ptr;
      break;
    case SSH_PKF_ENCRYPT:
      key->encryption = scheme_ptr;
      break;
    case SSH_PKF_DH:
      key->diffie_hellman = scheme_ptr;
      break;
    default:
      return SSH_CRYPTO_SCHEME_UNKNOWN;
      break;
    }
  return SSH_CRYPTO_OK;
}

/* Parse key_name to return the scheme name corresponding to to
   scheme_class, the caller shall free this. key_name should be in the
   form of a valid key name, namely a{b{c},d{e},f{g,h,i},j,...,m{n}}.
   a is the key type, the pairs b and c in b{c} are the
   scheme classes and scheme names respectively. The scheme
   name can be optionally omitted, e.g. h, in the above example
   When the scheme_class contains multiple scheme names, the first
   scheme name found is returned, e.g. in f{g,h,i} in the above example
   the string g is returned if called with scheme_class set to f.
*/
char * ssh_pk_get_scheme_name(const char *key_name,
                              const char *scheme_class)
{
  char *ptr, *start, *str = NULL;
  size_t len = 0;

  /* Search for the string scheme_class in key_name. */
  ptr = strstr(key_name, scheme_class);

  if (ptr)
    {
      ptr += strlen(scheme_class);

      /* Invalid key name */
      if (*ptr == '\0' || strchr(ptr, '}') == NULL)
        return NULL;

     if (*ptr != '{')
       return ssh_strdup(SSH_PK_USUAL_NAME);

     /* Step over the '{' character */
     ptr++;
     start = ptr;
     while (*ptr != '}' && *ptr != ',')
       ptr++, len++;

     /* XXX how to report memory failure. */
     if ((str = ssh_malloc(len + 1)) != NULL)
       {
         memcpy(str, start, len);
         str[len] = '\0';
       }
    }

  return str;
}

/* Set the scheme of the private key from a key_name. This is called
   when generating or importing private keys. */
SshCryptoStatus
ssh_private_key_set_scheme_from_key_name(SshPrivateKeyObject key,
                                         const char *key_name)
{
  SshCryptoStatus status;
  char *scheme_name;

  if ((scheme_name = ssh_pk_get_scheme_name(key_name, "sign")) != NULL)
    {
      status = ssh_private_key_set_scheme(key, SSH_PKF_SIGN, scheme_name);
      ssh_free(scheme_name);

      if (status != SSH_CRYPTO_OK)
        return status;
    }

  if ((scheme_name = ssh_pk_get_scheme_name(key_name, "encrypt")) != NULL)
    {
      status = ssh_private_key_set_scheme(key, SSH_PKF_ENCRYPT, scheme_name);
      ssh_free(scheme_name);

      if (status != SSH_CRYPTO_OK)
        return status;
    }

  if ((scheme_name = ssh_pk_get_scheme_name(key_name, "dh")) != NULL)
    {
      status = ssh_private_key_set_scheme(key, SSH_PKF_DH, scheme_name);
      ssh_free(scheme_name);

      if (status != SSH_CRYPTO_OK)
        return status;
    }

  return SSH_CRYPTO_OK;
}

/* Set the scheme of the private key from a key_name. This is called
   when defining or importing public keys. */
SshCryptoStatus
ssh_public_key_set_scheme_from_key_name(SshPublicKeyObject key,
                                        const char *key_name)
{
  SshCryptoStatus status;
  char *scheme_name;

  if ((scheme_name = ssh_pk_get_scheme_name(key_name, "sign")) != NULL)
    {
      status = ssh_public_key_set_scheme(key, SSH_PKF_SIGN, scheme_name);
      ssh_free(scheme_name);

      if (status != SSH_CRYPTO_OK)
        return status;
    }

  if ((scheme_name = ssh_pk_get_scheme_name(key_name, "encrypt")) != NULL)
    {
      status = ssh_public_key_set_scheme(key, SSH_PKF_ENCRYPT, scheme_name);
      ssh_free(scheme_name);

      if (status != SSH_CRYPTO_OK)
        return status;
    }

  if ((scheme_name = ssh_pk_get_scheme_name(key_name, "dh")) != NULL)
    {
      status = ssh_public_key_set_scheme(key, SSH_PKF_DH, scheme_name);
      ssh_free(scheme_name);

      if (status != SSH_CRYPTO_OK)
        return status;
    }

  return SSH_CRYPTO_OK;
}

/* Select new scheme to be used. That is assuming key supports many
   different schemes and/or padding types this can be of some
   use. Note however, that the key stays the same and some method
   assume keys to be of certain form. Such an example is DSA which by
   standard needs to have parameters of certain form, but this
   function could easily switch to DSA with key that is not of that
   form. Nevertheless I feel that such problems do not make switching
   to other methods unusable (even DSA would work with different
   parameters, although would not conform to the digital signature
   standard). */

SshCryptoStatus
ssh_private_key_select_scheme(SshPrivateKey handle, ...)
{
  SshPkFormat format;
  va_list ap;
  const char *scheme;
  SshPrivateKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  if (key->type == NULL)
    return SSH_CRYPTO_KEY_UNINITIALIZED;

  va_start(ap, handle);

  while ((format = va_arg(ap, SshPkFormat)) != SSH_PKF_END)
    {
      scheme = va_arg(ap, const char *);
      if (ssh_private_key_set_scheme(key, format, scheme) != SSH_CRYPTO_OK)
        {
          va_end(ap);
          return SSH_CRYPTO_SCHEME_UNKNOWN;
        }
    }

  va_end(ap);
  return SSH_CRYPTO_OK;
}

/* This is a little bit stupid, maybe same context for private and public
   key (internally) would be a good idea. */
SshCryptoStatus
ssh_public_key_select_scheme(SshPublicKey handle, ...)
{
  SshPkFormat format;
  const char *scheme;
  va_list ap;
  SshPublicKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  if (key->type == NULL)
    return SSH_CRYPTO_KEY_UNINITIALIZED;

  va_start(ap, handle);

  while ((format = va_arg(ap, SshPkFormat)) != SSH_PKF_END)
    {
      scheme = va_arg(ap, const char *);
      if (ssh_public_key_set_scheme(key, format, scheme) != SSH_CRYPTO_OK)
        {
          va_end(ap);
          return SSH_CRYPTO_SCHEME_UNKNOWN;
        }
    }

  va_end(ap);
  return SSH_CRYPTO_OK;
}


/* Routines for getting scheme names from private keys and public
   keys.  No other information is reasonable to expect to be gotten
   from schemes, although one could think of getting descriptions
   etc... */
SshCryptoStatus
ssh_private_key_get_scheme_name(SshPrivateKeyObject key,
                                SshPkFormat format,
                                const char **name)
{
  switch (format)
    {
    case SSH_PKF_SIGN:
      if (key->signature)
        *name = key->signature->name;
      else
        *name = NULL;
      break;
    case SSH_PKF_ENCRYPT:
      if (key->encryption)
        *name = key->encryption->name;
      else
        *name = NULL;
      break;
    case SSH_PKF_DH:
      if (key->diffie_hellman)
        *name = key->diffie_hellman->name;
      else
        *name = NULL;
      break;
    default:
      return SSH_CRYPTO_SCHEME_UNKNOWN;
      break;
    }
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_public_key_get_scheme_name(SshPublicKeyObject key,
                               SshPkFormat format,
                               const char **name)
{
  switch (format)
    {
    case SSH_PKF_SIGN:
      if (key->signature)
        *name = key->signature->name;
      else
        *name = NULL;
      break;
    case SSH_PKF_ENCRYPT:
      if (key->encryption)
        *name = key->encryption->name;
      else
        *name = NULL;
      break;
    case SSH_PKF_DH:
      if (key->diffie_hellman)
        *name = key->diffie_hellman->name;
      else
        *name = NULL;
      break;
    default:
      return SSH_CRYPTO_SCHEME_UNKNOWN;
      break;
    }
  return SSH_CRYPTO_OK;

}

/* Returns a mallocated string describing the key-type (e.g. "if-modn" or
   "dl-modp") from the key name.  The key name is assumed to be of the form
   key-type{scheme-type1{algorithm-name1},scheme-type2{algorithm-name2},...}
   This function will then return the string key-type.
*/
char * ssh_pk_get_key_type(const char *key_name)
{
  const char *c;
  char *str;
  size_t len = 0;

  if (!strstr(key_name, "{"))
    return ssh_strdup(key_name);

  for (c = key_name; *c != '{'; c++)
    len++;

  if ((str = (char *) ssh_malloc(len + 1)) == NULL)
    return NULL;

  memcpy(str, key_name, len);
  str[len] = '\0';

  return str;
}

/* Generate the full name of a particular private key. Inefficient
   (and ugly), but anyway this function does not need to be fast. */
char *
ssh_private_key_object_name(SshPrivateKeyObject key)
{
  char *buf, *tmp[4], *k;
  unsigned int i, j;

  /* If no schemes, just return the key type. */
  if (!key->signature && !key->encryption && !key->diffie_hellman)
    {
      ssh_dsprintf(&buf, "%s", key->type->name);
      return buf;
    }

  /* Generate the key type. */
  ssh_dsprintf(&buf, "%s{", key->type->name);

  for (i = 0; i < 4; i++)
    tmp[i] = NULL;

  /* Generate the scheme information. */
  i = j = 0;
  if (key->signature)
    ssh_dsprintf(&tmp[i++], "sign{%s}", key->signature->name);

  if (key->encryption)
    ssh_dsprintf(&tmp[i++], "encrypt{%s}", key->encryption->name);

  if (key->diffie_hellman)
    ssh_dsprintf(&tmp[i++], "dh{%s}", key->diffie_hellman->name);

  while (tmp[j])
    j++;

  if (j < i || buf == NULL)
    goto memory_fail;

  for (i = 0; i < j ; i++)
    {
      ssh_dsprintf(&k, "%s%s%s%s", buf, i ? "," : "", tmp[i],
                   i == (j - 1) ? "}": "" );

      ssh_free(buf);
      ssh_free(tmp[i]);
      tmp[i] = NULL;
      buf = k;

      if (k == NULL)
        goto memory_fail;
    }

  return k;

 memory_fail:
  for (i = 0; i < 4; i++)
    ssh_free(tmp[i]);
  ssh_free(buf);

  return NULL;
}

char *
ssh_private_key_name(SshPrivateKey handle)
{
  SshPrivateKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return NULL;

  return ssh_private_key_object_name(key);
}

char *
ssh_public_key_object_name(SshPublicKeyObject key)
{
  char *buf, *tmp[4], *k;
  unsigned int i, j;

  /* If no schemes, just return the key type. */
  if (!key->signature && !key->encryption && !key->diffie_hellman)
    {
      ssh_dsprintf(&buf, "%s", key->type->name);
      return buf;
    }

  /* Generate the key type. */
  ssh_dsprintf(&buf, "%s{", key->type->name);

  for (i = 0; i < 4; i++)
    tmp[i] = NULL;

  /* Generate the scheme information. */
  i = j = 0;
  if (key->signature)
    ssh_dsprintf(&tmp[i++], "sign{%s}", key->signature->name);

  if (key->encryption)
    ssh_dsprintf(&tmp[i++], "encrypt{%s}", key->encryption->name);

  if (key->diffie_hellman)
    ssh_dsprintf(&tmp[i++], "dh{%s}", key->diffie_hellman->name);

  while (tmp[j])
    j++;

  if (j < i || buf == NULL)
    goto memory_fail;

  for (i = 0; i < j; i++)
    {
      ssh_dsprintf(&k, "%s%s%s%s", buf, i ? "," : "", tmp[i],
                   i == (j - 1) ? "}" : "");

      ssh_free(buf);
      ssh_free(tmp[i]);
      tmp[i] = NULL;
      buf = k;

      if (k == NULL)
        goto memory_fail;
    }

  return k;

 memory_fail:
  for (i = 0; i < 4; i++)
    ssh_free(tmp[i]);
  ssh_free(buf);

  return NULL;
}

/* Generate the full name of a particular public key. Inefficient
   (and ugly), but anyway this function does not need to be fast. */
char *
ssh_public_key_name(SshPublicKey handle)
{
  SshPublicKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    return NULL;

  return ssh_public_key_object_name(key);
}

































































/* Always return FALSE in this implementation. */
Boolean
ssh_public_key_is_fips_approved(const char *key_type)
{
  return FALSE;
}

/* Always return FALSE in this implementation. */
Boolean
ssh_private_key_is_fips_approved(const char *key_type)
{
  return FALSE;
}


SshCryptoStatus
ssh_public_key_precompute(SshPublicKey handle)
{
  SshCryptoStatus status;
  SshPublicKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (key == NULL)
    return SSH_CRYPTO_OPERATION_FAILED;

  if (key->type->public_key_precompute)
    return (*key->type->public_key_precompute)(key->context);

  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_private_key_precompute(SshPrivateKey handle)
{
  SshCryptoStatus status;
  SshPrivateKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (key == NULL)
    return SSH_CRYPTO_OPERATION_FAILED;

  if (key->type->private_key_precompute)
    return (*key->type->private_key_precompute)(key->context);

  return SSH_CRYPTO_OK;
}



/* This function is needed in X.509 certificate routines. What is
   needed, is a way that creates from a bunch of stuff a valid
   SshPublicKey through sshcrypt header file. */
SshCryptoStatus
ssh_public_key_define(SshPublicKey *public_key,
                      const char *key_type, ...)
{
  SshCryptoStatus status;
  SshPublicKeyObject pub_key;
  const SshPkAction *action;
  SshPkFormat format;
  const char *name, *r;
  char *tmp, consumed[128];
  void *context;
  unsigned int i;
  va_list ap;




  if (!ssh_crypto_library_object_check_use(&status))
    return status;





  status = SSH_CRYPTO_UNKNOWN_KEY_TYPE;

  /* Get the key type (i.e. strip the scheme information from key_type). */
  if ((tmp = ssh_pk_get_key_type(key_type)) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  /* Find out the key type. */
  for (i = 0; ssh_pk_type_slots[i] != NULL && ssh_pk_type_slots[i]->name; i++)
    {





      if (strcmp(ssh_pk_type_slots[i]->name, tmp) != 0)
        continue;

      /* Type matches i.e. we've found our key type, so continue with
         finding schemes and parameters. */
      ssh_free(tmp);

      /* Allocate public key context. */
      if ((pub_key = ssh_malloc(sizeof(*pub_key))) == NULL)
          return SSH_CRYPTO_NO_MEMORY;

      pub_key->type = ssh_pk_type_slots[i];

      /* Clear pointers. */
      pub_key->signature = NULL;
      pub_key->encryption = NULL;
      pub_key->diffie_hellman = NULL;

      /* Initialize actions, and verify that context was allocated. */
      status = (*pub_key->type->public_key_action_init)(&context);
      if (status != SSH_CRYPTO_OK)
        {
          ssh_free(pub_key);
          return status;
        }

      /* Set the scheme from the key_name */
      status = ssh_public_key_set_scheme_from_key_name(pub_key, key_type);

      if (status != SSH_CRYPTO_OK)
        {
          (*pub_key->type->public_key_action_free)(context);
          ssh_free(pub_key);
          va_end(ap);
          return status;
        }

      /* Parse vararg list. */
      consumed[0] = '\000';
      while (TRUE)
        {
          va_start(ap, key_type);
          PROCESS(ap, consumed);

          format = va_arg(ap, SshPkFormat);
          strcat(consumed, "i");
          if (format == SSH_PKF_END)
            break;

          /* If the va list contains scheme parameters, we need to
             set the scheme again. */
          if (format == SSH_PKF_SIGN || format == SSH_PKF_ENCRYPT ||
              format == SSH_PKF_DH)
            {
              name = va_arg(ap, const char *);
              strcat(consumed, "p");
              status = ssh_public_key_set_scheme(pub_key, format, name);

             if (status != SSH_CRYPTO_OK)
                {
                  (*pub_key->type->public_key_action_free)(context);
                  ssh_free(pub_key);
                  va_end(ap);
                  return status;
                }
             va_end(ap);
             continue;
            }

          /* Search name from command lists. */
          action = ssh_pk_find_action(pub_key->type->action_list,
                                      format, SSH_PK_ACTION_FLAG_PUBLIC_KEY);

          if (!action)
            {
              (*pub_key->type->public_key_action_free)(context);
              ssh_free(pub_key);
              va_end(ap);
              return SSH_CRYPTO_UNSUPPORTED_IDENTIFIER;
            }

          /* Supported only scheme selection and special operations. */
          switch (action->flags & SSH_PK_ACTION_FLAG_GET_PUT)
            {
            case SSH_PK_ACTION_FLAG_GET_PUT:
              r = (*action->action_put)(context, ap, NULL, format);
              if (r == NULL)
                {
                  (*pub_key->type->public_key_action_free)(context);
                  ssh_free(pub_key);
                  va_end(ap);
                  return SSH_CRYPTO_INTERNAL_ERROR;
                }
              else
                strcat(consumed, r);
              break;
            default:
              ssh_fatal("ssh_public_key_define: internal error.");
              break;
            }
          va_end(ap);
        }

      /* Make the key and remove context. (One could incorporate
         making and freeing, however this way things seem to work
         also). */
      status =
        (*pub_key->type->public_key_action_make)(context, &pub_key->context);
      (*pub_key->type->public_key_action_free)(context);

      /* Quit unhappily. */
      if (status != SSH_CRYPTO_OK)
        {
          ssh_free(pub_key);
          va_end(ap);
          return status;
        }

      /* Set the address of the public key into the key context. */
      if (pub_key->type->set_key_pointer_to_context)
        {
          status =
            (*pub_key->type->set_key_pointer_to_context)(pub_key,
                                                         pub_key->context);
          if (status != SSH_CRYPTO_OK)
            {
              ssh_public_key_object_free(pub_key);
              va_end(ap);
              return status;
            }
        }

      if (!ssh_crypto_library_object_use(pub_key,
                                         SSH_CRYPTO_OBJECT_TYPE_PUBLIC_KEY))
        {
          ssh_public_key_object_free(pub_key);
          va_end(ap);
          return SSH_CRYPTO_NO_MEMORY;
        }

      /* Quit happily. */
      *public_key = SSH_CRYPTO_PUBLIC_KEY_TO_HANDLE(pub_key);
      va_end(ap);

      return SSH_CRYPTO_OK;
    }

  va_end(ap);

  return SSH_CRYPTO_UNKNOWN_KEY_TYPE;
}

SshCryptoStatus
ssh_private_key_get_info(SshPrivateKey handle, ...)
{
  SshCryptoStatus status;
  const SshPkAction *action;
  SshPkFormat format;
  const char **name_ptr, *r;
  char consumed[128];
  va_list ap;



  SshPrivateKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;





  consumed[0] = '\000';
  while (TRUE)
    {
      va_start(ap, handle);
      PROCESS(ap, consumed);

      format = va_arg(ap, SshPkFormat);
      strcat(consumed, "i");
      if (format == SSH_PKF_END)
        break;

      /* If looking for scheme information. */
      if (format == SSH_PKF_SIGN || format == SSH_PKF_ENCRYPT ||
          format == SSH_PKF_DH)
            {
              name_ptr = va_arg(ap, const char **);
              strcat(consumed, "p");

              status = ssh_private_key_get_scheme_name(key, format, name_ptr);
              if (status != SSH_CRYPTO_OK)
                {
                  va_end(ap);
                  return status;
                }
              va_end(ap);
              continue;
            }

      /* Seek for the action. */
      action = ssh_pk_find_action(key->type->action_list,
                                  format, SSH_PK_ACTION_FLAG_PRIVATE_KEY);

      if (!action)
        {
          va_end(ap);
          return SSH_CRYPTO_UNSUPPORTED_IDENTIFIER;
        }







      switch (action->flags & (SSH_PK_ACTION_FLAG_GET_PUT |
                               SSH_PK_ACTION_FLAG_KEY_TYPE))
        {
        case SSH_PK_ACTION_FLAG_KEY_TYPE:
          name_ptr = va_arg(ap, const char **);
          strcat(consumed, "p");
          *name_ptr = strchr(key->type->name, ':');
          if (*name_ptr)
            (*name_ptr)++;
          else
            *name_ptr = key->type->name; /* ssh_private_key_name(key); */
          break;

        case SSH_PK_ACTION_FLAG_GET_PUT:
          if (action->action_get == NULL_FNPTR)
            return SSH_CRYPTO_UNSUPPORTED;

          r = (*action->action_get)(key->context, ap, NULL, format);
          if (r == NULL)
            return SSH_CRYPTO_INTERNAL_ERROR;
          else
            strcat(consumed, r);
          break;

        default:
          ssh_fatal("ssh_private_key_get_info: internal error.");
          break;
        }

      va_end(ap);
    }

  va_end(ap);
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_public_key_get_info(SshPublicKey handle, ...)
{
  SshCryptoStatus status;
  const SshPkAction *action;
  SshPkFormat format;
  const char **name_ptr, *r;
  char consumed[128];
  va_list ap;



  SshPublicKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;





  consumed[0] = '\000';
  while (TRUE)
    {
      va_start(ap, handle);
      PROCESS(ap, consumed);

      format = va_arg(ap, SshPkFormat);
      strcat(consumed, "i");
      if (format == SSH_PKF_END)
        break;

      /* If looking for scheme information. */
      if (format == SSH_PKF_SIGN || format == SSH_PKF_ENCRYPT ||
          format == SSH_PKF_DH)
        {
          name_ptr = va_arg(ap, const char **);
          strcat(consumed, "p");

          status = ssh_public_key_get_scheme_name(key, format, name_ptr);

          if (status != SSH_CRYPTO_OK)
            {
              va_end(ap);
              return status;
            }
          va_end(ap);
          continue;
        }

      /* Seek for the action. */
      action = ssh_pk_find_action(key->type->action_list,
                                  format, SSH_PK_ACTION_FLAG_PUBLIC_KEY);

      if (!action)
        {
          va_end(ap);
          return SSH_CRYPTO_UNSUPPORTED_IDENTIFIER;
        }







      switch (action->flags & (SSH_PK_ACTION_FLAG_GET_PUT |
                               SSH_PK_ACTION_FLAG_KEY_TYPE))
        {
        case SSH_PK_ACTION_FLAG_KEY_TYPE:
          name_ptr = va_arg(ap, const char **);
          strcat(consumed, "p");
          *name_ptr = strchr(key->type->name, ':');
          if (*name_ptr)
            (*name_ptr)++;
          else
            *name_ptr = key->type->name; /* ssh_private_key_name(key); */
          break;

        case SSH_PK_ACTION_FLAG_GET_PUT:
          if (action->action_get == NULL_FNPTR)
            return SSH_CRYPTO_UNSUPPORTED;

          r = (*action->action_get)(key->context, ap, NULL, format);
          if (r == NULL)
            return SSH_CRYPTO_INTERNAL_ERROR;
          else
            strcat(consumed, r);
          break;

        default:
          ssh_fatal("ssh_public_key_get_info: internal error.");
          break;
        }
      va_end(ap);
    }

  va_end(ap);
  return SSH_CRYPTO_OK;
}

/* Names could be given by gathering all possible combinations, however,
   It would be more useful for the user to get names for some specific
   class of algorithms. Such as signature, encryption or some key exchange
   method. */
char *
ssh_public_key_get_supported(void)
{
  unsigned int i, j, added;
  char *str;
  SshBufferStruct buffer;
  const SshPkType *type;
  size_t buf_length;





  ssh_buffer_init(&buffer);

  /* Run through all registered key types. */
  for (i = 0; ssh_pk_type_slots[i] != NULL && ssh_pk_type_slots[i]->name; i++)
    {
      type = ssh_pk_type_slots[i];






      /* Add the key type identifier node. */
      if (ssh_buffer_append_cstrs(&buffer, i ? ",": "",
                                  type->name,
                                  NULL) != SSH_BUFFER_OK)
        goto fail;
      /* Add the signature schemes. */
      for (j = added = 0; type->signature_list && type->signature_list[j].name
             != NULL; j++)
        {





          if (ssh_buffer_append_cstrs(&buffer, added ? "" : "{sign", NULL)
              != SSH_BUFFER_OK)
            goto fail;

          /* Add scheme names. */
          if (ssh_buffer_append_cstrs(&buffer, added ? "," : "{",
                                      type->signature_list[j].name, NULL)
              != SSH_BUFFER_OK)
            goto fail;

          added++;
        }

      /* Go up if one went down. */
      if (added)
        {
          if (ssh_buffer_append_cstrs(&buffer, "}", NULL)
              != SSH_BUFFER_OK)
            goto fail;
        }

      /* Add the encryption schemes. */
      for (j = added = 0;
           type->encryption_list && type->encryption_list[j].name != NULL;
           j++)
        {





          if (ssh_buffer_append_cstrs(&buffer, added ? "" : ",encrypt", NULL)
              != SSH_BUFFER_OK)
            goto fail;

          /* Add scheme names. */
          if (ssh_buffer_append_cstrs(&buffer, added ? "," : "{",
                                      type->encryption_list[j].name, NULL)
              != SSH_BUFFER_OK)
            goto fail;

          added++;
        }

      /* Go up if one went down. */
      if (added)
        {
          if (ssh_buffer_append_cstrs(&buffer, "}", NULL)
              != SSH_BUFFER_OK)
            goto fail;
        }

      /* Add the Diffie-Hellman schemes. */
      for (j = added = 0;
           type->diffie_hellman_list &&
             type->diffie_hellman_list[j].name != NULL;
           j++)
        {






          if (ssh_buffer_append_cstrs(&buffer, added ? "" : ",dh", NULL)
              != SSH_BUFFER_OK)
            goto fail;

          /* Add scheme names. */
          if (ssh_buffer_append_cstrs(&buffer, added ? "," : "{",
                                      type->diffie_hellman_list[j].name, NULL)
              != SSH_BUFFER_OK)
            goto fail;

          added++;
        }

      if (ssh_buffer_append_cstrs(&buffer, added ? "}}" : "}" , NULL)
          != SSH_BUFFER_OK)
        goto fail;

    }

  buf_length = ssh_buffer_len(&buffer);
  str = ssh_memdup(ssh_buffer_ptr(&buffer), buf_length);
  ssh_buffer_uninit(&buffer);
  return str;

 fail:
  ssh_buffer_uninit(&buffer);
  return NULL;
}


/* Public key routines copy and free. */

/* Doing copy of the key_src, so that both keys can be altered without
   affecting the other. Note, that although keys might seem to be totally
   separate some features might be implemeted with reference counting. */
SshCryptoStatus
ssh_public_key_copy(SshPublicKey handle_src, SshPublicKey *key_dest)
{
  SshPublicKeyObject created, key_src;
  SshCryptoStatus status;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(key_src = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle_src)))
    return SSH_CRYPTO_HANDLE_INVALID;

  if (key_src->type->public_key_copy == NULL_FNPTR)
    return SSH_CRYPTO_UNSUPPORTED;

  if ((created = ssh_malloc(sizeof(*created))) != NULL)
    {
      /* First copy all basic internal stuff and then the context
         explicitly. */
      memcpy(created, key_src, sizeof(*created));

      status =
        (*key_src->type->public_key_copy)(key_src->context, &created->context);

      if (status != SSH_CRYPTO_OK)
        {
          ssh_free(created);
          return status;
        }

      /* Set the address of the copied public key into its key context. */
      if (key_src->type->set_key_pointer_to_context)
        {
          status =
            (*key_src->type->set_key_pointer_to_context)(created,
                                                         created->context);
          if (status != SSH_CRYPTO_OK)
            {
              ssh_public_key_object_free(created);
              return status;
            }
        }

      if (!ssh_crypto_library_object_use(created,
                                         SSH_CRYPTO_OBJECT_TYPE_PUBLIC_KEY))
        {
          ssh_public_key_object_free(created);
          return SSH_CRYPTO_NO_MEMORY;
        }

      *key_dest = SSH_CRYPTO_PUBLIC_KEY_TO_HANDLE(created);
      return SSH_CRYPTO_OK;
    }
  else
    return SSH_CRYPTO_NO_MEMORY;
}

SshCryptoStatus
ssh_public_key_object_allocate(const char *type, SshPublicKeyObject *key)
{
  SshPublicKeyObject public_key;
  char *name;
  int i;

  /* Get the key type (i.e. strip the scheme information from key_type). */
  if (!(name = ssh_pk_get_key_type(type)))
    return SSH_CRYPTO_NO_MEMORY;

  /* Find correct key type. */
  for (i = 0, public_key = NULL;
       ssh_pk_type_slots[i] != NULL && ssh_pk_type_slots[i]->name;
       i++)
    {
      if (strcmp(ssh_pk_type_slots[i]->name, name) == 0)
        {
          /* Initialize public key. */
          if ((public_key = ssh_calloc(1, sizeof(*public_key))) != NULL)
            {
              public_key->type = ssh_pk_type_slots[i];
            }
          else
            {
              ssh_free(name);
              return SSH_CRYPTO_NO_MEMORY;
            }
          break;
        }
    }

  ssh_free(name);
  *key = public_key;

  if (public_key == NULL)
    return SSH_CRYPTO_UNKNOWN_KEY_TYPE;

  return SSH_CRYPTO_OK;
}


void
ssh_public_key_object_free(SshPublicKeyObject key)
{
  if (key->type->public_key_free && key->context)
    (*key->type->public_key_free)(key->context);

  key->context = NULL;
  ssh_free(key);
}

void
ssh_public_key_free(SshPublicKey handle)
{
  SshPublicKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    return;

  ssh_crypto_library_object_release(key);
  ssh_public_key_object_free(key);
}


/* Report the maximal length of bytes which may be encrypted with this
   public key. Return 0 if encryption not available for this public
   key. */
size_t
ssh_public_key_object_max_encrypt_input_len(SshPublicKeyObject key)
{
  SshRGF rgf;
  size_t len;

  if (key->encryption == NULL)
    return 0;

  if (key->encryption->public_key_max_encrypt_input_len == NULL_FNPTR)
    return 0;

  rgf = ssh_rgf_allocate(key->encryption->rgf_def);

  len = (*key->encryption->public_key_max_encrypt_input_len)(key->context,
                                                             rgf);

  ssh_rgf_free(rgf);
  return len;
}

size_t
ssh_public_key_max_encrypt_input_len(SshPublicKey handle)
{
  SshPublicKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    return 0;

  return ssh_public_key_object_max_encrypt_input_len(key);
}

/* This is similar to the previous one, but the maximal output length
   is returned instead the of the maximum input length. */
size_t
ssh_public_key_object_max_encrypt_output_len(SshPublicKeyObject key)
{
  SshRGF rgf;
  size_t len;

  if (key->encryption == NULL)
    return 0;

  if (key->encryption->public_key_max_encrypt_output_len == NULL_FNPTR)
    return 0;

  rgf = ssh_rgf_allocate(key->encryption->rgf_def);

  len = (*key->encryption->public_key_max_encrypt_output_len)(key->context,
                                                              rgf);
  ssh_rgf_free(rgf);
  return len;
}

size_t
ssh_public_key_max_encrypt_output_len(SshPublicKey handle)
{
  SshPublicKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    return 0;

  return ssh_public_key_object_max_encrypt_output_len(key);
}

/* Private key routines copy and free. */
SshCryptoStatus
ssh_private_key_copy(SshPrivateKey handle_src, SshPrivateKey *key_dest)
{
  SshCryptoStatus status;
  SshPrivateKeyObject created, key_src;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(key_src = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle_src)))
    return SSH_CRYPTO_HANDLE_INVALID;

  if (key_src->type->private_key_copy == NULL_FNPTR)
    return SSH_CRYPTO_UNSUPPORTED;

  if ((created = ssh_malloc(sizeof(*created))) != NULL)
    {
      memcpy(created, key_src, sizeof(*created));
      status =
        (*key_src->type->private_key_copy)(key_src->context,
                                           &created->context);

      if (status != SSH_CRYPTO_OK)
        {
          ssh_free(created);
          return status;
        }

      /* Set the address of the copied private key into its context. */
      if (key_src->type->set_key_pointer_to_context)
        {
          status =
            (*key_src->type->set_key_pointer_to_context)(created,
                                                         created->context);

          if (status != SSH_CRYPTO_OK)
            {
              ssh_private_key_object_free(created);
              return status;
            }
        }

      if (!ssh_crypto_library_object_use(created,
                                         SSH_CRYPTO_OBJECT_TYPE_PRIVATE_KEY))
        {
          ssh_private_key_object_free(created);
          *key_dest = NULL;
          return SSH_CRYPTO_NO_MEMORY;
        }

      *key_dest = SSH_CRYPTO_PRIVATE_KEY_TO_HANDLE(created);
      return SSH_CRYPTO_OK;
    }
  else
    return SSH_CRYPTO_NO_MEMORY;
}

/* Allocate a private key structure. This performs only minimal
   instantiation. */
SshCryptoStatus
ssh_private_key_object_allocate(const char *type, SshPrivateKeyObject *key)
{
  SshPrivateKeyObject private_key;
  char *name;
  int i;

  /* Get the key type (i.e. strip the scheme information from key_type). */
  if (!(name = ssh_pk_get_key_type(type)))
    return SSH_CRYPTO_NO_MEMORY;

  /* Find correct key type. */
  for (i = 0, private_key = NULL;
       ssh_pk_type_slots[i] != NULL && ssh_pk_type_slots[i]->name;
       i++)
    {
      if (strcmp(ssh_pk_type_slots[i]->name, name) == 0)
        {
          /* Initialize private key. */
          if ((private_key = ssh_calloc(1, sizeof(*private_key))) != NULL)
            {
              private_key->type = ssh_pk_type_slots[i];
              /*
              private_key->signature = NULL;
              private_key->encryption = NULL;
              private_key->diffie_hellman = NULL;
              private_key->context = NULL;
              */
            }
          else
            {
              ssh_free(name);
              return SSH_CRYPTO_NO_MEMORY;
            }
          break;
        }
    }

  ssh_free(name);
  *key = private_key;

  if (private_key == NULL)
    return SSH_CRYPTO_UNKNOWN_KEY_TYPE;

  return SSH_CRYPTO_OK;
}

/* Release a private key structure. */
void
ssh_private_key_object_free(SshPrivateKeyObject key)
{
  if (key->type->private_key_free && key->context)
    (*key->type->private_key_free)(key->context);

  key->context = NULL;
  ssh_free(key);
}

void
ssh_private_key_free(SshPrivateKey handle)
{
  SshPrivateKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return;

  ssh_crypto_library_object_release(key);
  ssh_private_key_object_free(key);
}

SshCryptoStatus
ssh_private_key_derive_public_key_internal(SshPrivateKeyObject key,
                                           SshPublicKeyObject *public_ret)
{
  SshPublicKeyObject pub;
  void *pub_context;
  SshCryptoStatus status;

  if (key->type->private_key_derive_public_key == NULL_FNPTR)
    return SSH_CRYPTO_UNSUPPORTED;

  status = (*key->type->private_key_derive_public_key)(key->context,
                                                       &pub_context);

  if (status != SSH_CRYPTO_OK)
    return status;

  if ((pub = ssh_malloc(sizeof(*pub))) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  pub->context = pub_context;
  pub->type = key->type;

  /* Set up all schemes for compatibility. */
  pub->signature = key->signature;
  pub->encryption = key->encryption;
  pub->diffie_hellman = key->diffie_hellman;

  /* Set the address of the derived public key into its context. */
  if (pub->type->set_key_pointer_to_context)
    {
      status = (*pub->type->set_key_pointer_to_context)(pub, pub_context);
      if (status != SSH_CRYPTO_OK)
        {
          ssh_public_key_object_free(pub);
          return status;
        }
    }

  *public_ret = pub;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_private_key_derive_public_key(SshPrivateKey handle,
                                  SshPublicKey *public_ret)
{
  SshCryptoStatus status;
  SshPublicKeyObject pub;
  SshPrivateKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  status = ssh_private_key_derive_public_key_internal(key, &pub);

  if (status != SSH_CRYPTO_OK)
    return status;

  /* We have now generated the public key. Test the consistency of the
     public/private key pair. The crypto library enters an error state
     on failure.

     This test is required by FIPS 140-2 4.9.2 "Conditional Tests" */
  status = ssh_crypto_test_pk_consistency(pub, key);
  
  if (status == SSH_CRYPTO_NO_MEMORY)
    {
      ssh_public_key_object_free(pub);
      *public_ret = NULL;
      return SSH_CRYPTO_NO_MEMORY;
    }
  else if (status != SSH_CRYPTO_OK)
    {
      ssh_public_key_object_free(pub);
      ssh_crypto_library_error(SSH_CRYPTO_ERROR_KEY_TEST_FAILURE);
      *public_ret = NULL;
      return SSH_CRYPTO_LIBRARY_ERROR;
    }

  if (!ssh_crypto_library_object_use(pub, SSH_CRYPTO_OBJECT_TYPE_PUBLIC_KEY))
    {
      ssh_public_key_object_free(pub);
      *public_ret = NULL;
      return SSH_CRYPTO_NO_MEMORY;
    }

  *public_ret = SSH_CRYPTO_PUBLIC_KEY_TO_HANDLE(pub);
  return SSH_CRYPTO_OK;
}


/* Signature hash function derivation functions. */

SshCryptoStatus
ssh_public_key_derive_signature_hash(SshPublicKey handle, SshHash *hash_ret)
{
  SshRGF rgf;
  SshHash hash;
  SshCryptoStatus status;
  SshPublicKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  if (key->signature == NULL)
    return SSH_CRYPTO_UNSUPPORTED;

  if ((rgf = ssh_rgf_allocate(key->signature->rgf_def)) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  hash = ssh_rgf_derive_hash(rgf);
  ssh_rgf_free(rgf);

  *hash_ret = hash;
  return SSH_CRYPTO_OK;
}

SshCryptoStatus
ssh_private_key_derive_signature_hash(SshPrivateKey handle, SshHash *hash_ret)
{
  SshRGF rgf;
  SshHash hash;
  SshCryptoStatus status;
  SshPrivateKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  if (key->signature == NULL)
    return SSH_CRYPTO_UNSUPPORTED;

  if ((rgf = ssh_rgf_allocate(key->signature->rgf_def)) == NULL)
    return SSH_CRYPTO_NO_MEMORY;

  hash = ssh_rgf_derive_hash(rgf);
  ssh_rgf_free(rgf);

  *hash_ret = hash;
  return SSH_CRYPTO_OK;
}


/* Perform the public key encryption operation. */

SshCryptoStatus
ssh_public_key_object_encrypt(SshPublicKeyObject key,
                              const unsigned char *plaintext,
                              size_t plaintext_len,
                              unsigned char *ciphertext,
                              size_t buffer_len,
                              size_t *ciphertext_len_return)
{
  SshRGF rgf;
  SshCryptoStatus status;

  if (key->encryption == NULL ||
      key->encryption->public_key_encrypt == NULL_FNPTR)
    return SSH_CRYPTO_UNSUPPORTED;

  rgf = ssh_rgf_allocate(key->encryption->rgf_def);
  if (rgf == NULL)
    return SSH_CRYPTO_OPERATION_FAILED;

  status = (*key->encryption->public_key_encrypt)(key->context,
                                                  plaintext, plaintext_len,
                                                  ciphertext, buffer_len,
                                                  ciphertext_len_return,
                                                  rgf);

  /* Free the RGF */
  ssh_rgf_free(rgf);
  return status;
}

SshCryptoStatus
ssh_public_key_encrypt(SshPublicKey handle,
                       const unsigned char *plaintext,
                       size_t plaintext_len,
                       unsigned char *ciphertext,
                       size_t buffer_len,
                       size_t *ciphertext_len_return)
{
  SshCryptoStatus status;
  SshPublicKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  return
    ssh_public_key_object_encrypt(key, plaintext, plaintext_len,
                                  ciphertext, buffer_len,
                                  ciphertext_len_return);
}

/* Verify a signature. In fact, decrypt the given signature with the
   public key, and then compare the decrypted data to the given
   (supposedly original) data. If the decrypted data and the given
   data are identical (in the sense that they are of equal length and
   their contents are bit-wise same) the function returns TRUE,
   otherways FALSE. */

Boolean
ssh_public_key_object_verify_signature(SshPublicKeyObject key,
                                       const unsigned char *signature,
                                       size_t signature_len,
                                       const unsigned char *data,
                                       size_t data_len)
{
  SshCryptoStatus status;
  SshRGF rgf;

  if (key == NULL ||
      key->signature == NULL ||
      key->signature->public_key_verify == NULL_FNPTR)
    return FALSE;

  /* We need to compute the RGFHash ourselves. */
  rgf = ssh_rgf_allocate(key->signature->rgf_def);
  if (rgf == NULL)
    return FALSE;
  ssh_rgf_hash_update(rgf, data, data_len);

  status = (*key->signature->public_key_verify)(key->context,
                                                signature, signature_len,
                                                rgf);

  /* Free the RGF */
  ssh_rgf_free(rgf);

  if (status == SSH_CRYPTO_OK)
     return TRUE;

  return FALSE;
}

Boolean
ssh_public_key_verify_signature(SshPublicKey handle,
                                const unsigned char *signature,
                                size_t signature_len,
                                const unsigned char *data,
                                size_t data_len)
{
  SshCryptoStatus status;
  SshPublicKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    return FALSE;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  return
    ssh_public_key_object_verify_signature(key, signature, signature_len,
                                           data, data_len);
}


Boolean
ssh_public_key_verify_signature_with_digest(SshPublicKey handle,
                                            const unsigned char *signature,
                                            size_t signature_len,
                                            const unsigned char *digest,
                                            size_t digest_len)
{
  SshRGF rgf;
  SshCryptoStatus status;
  SshPublicKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    return FALSE;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  if (key->signature == NULL ||
      key->signature->public_key_verify == NULL_FNPTR)
    return FALSE;

  rgf = ssh_rgf_allocate(key->signature->rgf_def);
  if (rgf == NULL)
    return FALSE;
  if (ssh_rgf_hash_update_with_digest(rgf,
                                      digest, digest_len) == FALSE)
    {
      ssh_rgf_free(rgf);
      return FALSE;
    }

  status = (*key->signature->public_key_verify)(key->context,
                                                signature, signature_len,
                                                rgf);

  /* Free the RGF Hash */
  ssh_rgf_free(rgf);

  if (status == SSH_CRYPTO_OK)
    return TRUE;

  return FALSE;
}

size_t
ssh_private_key_object_max_signature_input_len(SshPrivateKeyObject key)
{
  SshRGF rgf;
  size_t len;

  if (key->signature == NULL)
    return 0;

  if (key->signature->private_key_max_signature_input_len == NULL_FNPTR)
    return 0;

  rgf = ssh_rgf_allocate(key->signature->rgf_def);

  len = (*key->signature->private_key_max_signature_input_len)(key->context,
                                                               rgf);
  ssh_rgf_free(rgf);
  return len;
}

size_t
ssh_private_key_max_signature_input_len(SshPrivateKey handle)
{
  SshPrivateKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return 0;

  return ssh_private_key_object_max_signature_input_len(key);
}

size_t
ssh_private_key_object_max_signature_output_len(SshPrivateKeyObject key)
{
  SshRGF rgf;
  size_t len;

  if (key->signature == NULL)
    return 0;

  if (key->signature->private_key_max_signature_output_len == NULL_FNPTR)
    return 0;

  rgf = ssh_rgf_allocate(key->signature->rgf_def);

  len = (*key->signature->private_key_max_signature_output_len)(key->context,
                                                                rgf);
  ssh_rgf_free(rgf);
  return len;
}

size_t
ssh_private_key_max_signature_output_len(SshPrivateKey handle)
{
  SshPrivateKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return 0;

  return ssh_private_key_object_max_signature_output_len(key);
}

/* Return the maximal lenght of bytes which may be decrypted with this
   private key. The result is queried from the corresponding private key
   cryptosystem package with a type-specific function. */

size_t
ssh_private_key_object_max_decrypt_input_len(SshPrivateKeyObject key)
{
  SshRGF rgf;
  size_t len;

  if (key->encryption == NULL)
    return 0;

  if (key->encryption->private_key_max_decrypt_input_len == NULL_FNPTR)
    return 0;

  rgf = ssh_rgf_allocate(key->encryption->rgf_def);

  len = (*key->encryption->private_key_max_decrypt_input_len)(key->context,
                                                             rgf);
  ssh_rgf_free(rgf);
  return len;
}

size_t
ssh_private_key_max_decrypt_input_len(SshPrivateKey handle)
{
  SshPrivateKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return 0;

  return ssh_private_key_object_max_decrypt_input_len(key);
}

/* Similar to the previous function except this will return the maximum
   output lenght with decryption. */

size_t
ssh_private_key_object_max_decrypt_output_len(SshPrivateKeyObject key)
{
  SshRGF rgf;
  size_t len;

  if (key->encryption == NULL)
    return 0;

  if (key->encryption->private_key_max_decrypt_output_len == NULL_FNPTR)
    return 0;

  rgf = ssh_rgf_allocate(key->encryption->rgf_def);

  len = (*key->encryption->private_key_max_decrypt_output_len)(key->context,
                                                               rgf);
  ssh_rgf_free(rgf);
  return len;
}

size_t
ssh_private_key_max_decrypt_output_len(SshPrivateKey handle)
{
  SshPrivateKeyObject key;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return 0;

  return ssh_private_key_object_max_decrypt_output_len(key);
}

/* Private key decrypt and encrypt */
SshCryptoStatus
ssh_private_key_object_decrypt(SshPrivateKeyObject key,
                        const unsigned char *ciphertext,
                        size_t ciphertext_len,
                        unsigned char *plaintext,
                        size_t buffer_len,
                        size_t *plaintext_length_return)
{
  SshRGF rgf;
  SshCryptoStatus status;

  if (key->encryption == NULL ||
      key->encryption->private_key_decrypt == NULL_FNPTR)
    return SSH_CRYPTO_UNSUPPORTED;

  rgf = ssh_rgf_allocate(key->encryption->rgf_def);
  if (rgf == NULL)
    return SSH_CRYPTO_OPERATION_FAILED;

  status = (*key->encryption->private_key_decrypt)(key->context,
                                                   ciphertext,
                                                   ciphertext_len,
                                                   plaintext,
                                                   buffer_len,
                                                   plaintext_length_return,
                                                   rgf);

  ssh_rgf_free(rgf);
  return status;
}

SshCryptoStatus
ssh_private_key_decrypt(SshPrivateKey handle,
                        const unsigned char *ciphertext,
                        size_t ciphertext_len,
                        unsigned char *plaintext,
                        size_t buffer_len,
                        size_t *plaintext_length_return)
{
  SshCryptoStatus status;
  SshPrivateKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  return
    ssh_private_key_object_decrypt(key, ciphertext, ciphertext_len,
                                   plaintext, buffer_len,
                                   plaintext_length_return);
}

SshCryptoStatus
ssh_private_key_object_sign(SshPrivateKeyObject key,
                            const unsigned char *data,
                            size_t data_len,
                            unsigned char *signature,
                            size_t signature_len,
                            size_t *signature_length_return)
{
  SshRGF rgf;
  SshCryptoStatus status;

  if (key->signature == NULL ||
      key->signature->private_key_sign == NULL_FNPTR)
    return SSH_CRYPTO_UNSUPPORTED;

  rgf = ssh_rgf_allocate(key->signature->rgf_def);
  if (rgf == NULL)
    return SSH_CRYPTO_OPERATION_FAILED;

  ssh_rgf_hash_update(rgf, data, data_len);

  status = (*key->signature->private_key_sign)(key->context,
                                               rgf,
                                               signature, signature_len,
                                               signature_length_return);

  ssh_rgf_free(rgf);
  return status;
}

SshCryptoStatus
ssh_private_key_sign(SshPrivateKey handle,
                     const unsigned char *data,
                     size_t data_len,
                     unsigned char *signature,
                     size_t signature_len,
                     size_t *signature_length_return)
{
  SshCryptoStatus status;
  SshPrivateKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  return
    ssh_private_key_object_sign(key, data, data_len,
                                signature, signature_len,
                                signature_length_return);
}


SshCryptoStatus
ssh_private_key_object_sign_digest(SshPrivateKeyObject key,
                            const unsigned char *digest,
                            size_t digest_len,
                            unsigned char *signature,
                            size_t signature_len,
                            size_t *signature_length_return)
{
  SshRGF rgf;
  SshCryptoStatus status;

  if (key->signature == NULL ||
      key->signature->private_key_sign == NULL_FNPTR)
    return SSH_CRYPTO_UNSUPPORTED;

  rgf = ssh_rgf_allocate(key->signature->rgf_def);
  if (rgf == NULL)
    return SSH_CRYPTO_OPERATION_FAILED;

  if (ssh_rgf_hash_update_with_digest(rgf,
                                      digest, digest_len) == FALSE)
    {
      ssh_rgf_free(rgf);
      return SSH_CRYPTO_OPERATION_FAILED;
    }

  status = (*key->signature->private_key_sign)(key->context,
                                               rgf,
                                               signature, signature_len,
                                               signature_length_return);

  ssh_rgf_free(rgf);
  return status;
}

SshCryptoStatus
ssh_private_key_sign_digest(SshPrivateKey handle,
                            const unsigned char *digest,
                            size_t digest_len,
                            unsigned char *signature,
                            size_t signature_len,
                            size_t *signature_length_return)
{
  SshCryptoStatus status;
  SshPrivateKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  return
    ssh_private_key_object_sign_digest(key, digest, digest_len,
                                       signature, signature_len,
                                       signature_length_return);
}

/* Diffie-Hellman key exchange method. */

size_t
ssh_pk_group_object_dh_setup_max_output_length(SshPkGroupObject group)
{
  if (group->diffie_hellman == NULL ||
      group->diffie_hellman->diffie_hellman_exchange_max_length == NULL_FNPTR)
    return 0;

  return (*group->diffie_hellman->
          diffie_hellman_exchange_max_length)(group->context);
}

size_t
ssh_pk_group_object_dh_agree_max_output_length(SshPkGroupObject group)
{
  if (group->diffie_hellman == NULL ||
      group->diffie_hellman->diffie_hellman_secret_value_max_length
      == NULL_FNPTR)
    return 0;

  return (*group->diffie_hellman->
          diffie_hellman_secret_value_max_length)(group->context);
}


size_t
ssh_pk_group_dh_setup_max_output_length(SshPkGroup handle)
{
  SshPkGroupObject group;

  if (!(group = SSH_CRYPTO_HANDLE_TO_PK_GROUP(handle)))
    return 0;

  return ssh_pk_group_object_dh_setup_max_output_length(group);
}

size_t
ssh_pk_group_dh_agree_max_output_length(SshPkGroup handle)
{
  SshPkGroupObject group;

  if (!(group = SSH_CRYPTO_HANDLE_TO_PK_GROUP(handle)))
    return 0;

  return ssh_pk_group_object_dh_agree_max_output_length(group);
}


SshCryptoStatus
ssh_pk_group_object_dh_setup(SshPkGroupObject group,
                             SshPkGroupDHSecret *secret,
                             unsigned char *exchange,
                             size_t exchange_length,
                             size_t *return_length)
{
  if (group->diffie_hellman == NULL ||
      group->diffie_hellman->diffie_hellman_setup == NULL_FNPTR)
    return SSH_CRYPTO_UNSUPPORTED;

  return (*group->diffie_hellman->diffie_hellman_setup)(group->context,
                                                        secret,
                                                        exchange,
                                                        exchange_length,
                                                        return_length);
}

SshCryptoStatus
ssh_pk_group_dh_setup(SshPkGroup handle,
                      SshPkGroupDHSecret *secret,
                      unsigned char *exchange,
                      size_t exchange_length,
                      size_t *return_length)
{
  SshCryptoStatus status;
  SshPkGroupObject group;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(group = SSH_CRYPTO_HANDLE_TO_PK_GROUP(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  return
    ssh_pk_group_object_dh_setup(group, secret,
                                 exchange, exchange_length, return_length);
}

SshCryptoStatus
ssh_pk_group_object_dh_agree(SshPkGroupObject group,
                             SshPkGroupDHSecret secret,
                             const unsigned char *exchange,
                             size_t exchange_length,
                             unsigned char *secret_value_buffer,
                             size_t secret_value_buffer_length,
                             size_t *return_length)
{
  if (group->diffie_hellman == NULL ||
      group->diffie_hellman->diffie_hellman_agree == NULL_FNPTR)
    return SSH_CRYPTO_UNSUPPORTED;

  return (*group->diffie_hellman->
          diffie_hellman_agree)(group->context,
                                (void *) secret,
                                exchange,
                                exchange_length,
                                secret_value_buffer,
                                secret_value_buffer_length,
                                return_length);
}

SshCryptoStatus
ssh_pk_group_dh_agree(SshPkGroup handle,
                      SshPkGroupDHSecret secret,
                      const unsigned char *exchange,
                      size_t exchange_length,
                      unsigned char *secret_value_buffer,
                      size_t secret_value_buffer_length,
                      size_t *return_length)
{
  SshCryptoStatus status;
  SshPkGroupObject group;

  if (!ssh_crypto_library_object_check_use(&status))
    return status;

  if (!(group = SSH_CRYPTO_HANDLE_TO_PK_GROUP(handle)))
    return SSH_CRYPTO_HANDLE_INVALID;

  return
    ssh_pk_group_object_dh_agree(group, secret, exchange, exchange_length,
                                 secret_value_buffer,
                                 secret_value_buffer_length, return_length);
}


/* Asyncronous operations implemented using the syncronous software
   implementation if no asynchronous callback was defined for the key. */


static void ssh_rgf_free_cb(Boolean aborted, void *context)
{
  SshRGF rgf = context;

  ssh_rgf_free(rgf);
}


/* Start asyncronous public key encryption operation. The library will call
   given callback when operation is done. Callback may be called immediately
   during this call. The function ssh_operation_abort function may be called to
   abort this operation before it finishes, in which case the callback is not
   called and the SshOperationHandle will be NULL. */
SshOperationHandle
ssh_public_key_encrypt_async(SshPublicKey handle,
                             const unsigned char *plaintext,
                             size_t plaintext_length,
                             SshPublicKeyEncryptCB callback,
                             void *context)
{
  unsigned char *ciphertext;
  size_t return_length, ciphertext_length;
  SshOperationHandle op;
  SshCryptoStatus status;
  SshRGF rgf;
  SshPublicKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    {
      (*callback)(status, NULL, 0, context);
      return NULL;
    }

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    {
      (*callback)(SSH_CRYPTO_HANDLE_INVALID, NULL, 0, context);
      return NULL;
    }

  if (key->encryption && key->encryption->public_key_encrypt_async)
    {
      rgf = ssh_rgf_allocate(key->encryption->rgf_def);

      if (rgf == NULL)
        {
          (*callback)(SSH_CRYPTO_OPERATION_FAILED, NULL, 0, context);
          return NULL;
        }

      op = (*key->encryption->
            public_key_encrypt_async)(key->context,
                                      plaintext, plaintext_length,
                                      rgf,
                                      callback, context);

      /* The RGF Hash is freed when the handle is destroyed. */
      ssh_operation_attach_destructor(op, ssh_rgf_free_cb, rgf);
      return op;
    }

  /* Do it using the synchronous code. */
  ciphertext_length = ssh_public_key_object_max_encrypt_output_len(key);
  if ((ciphertext = ssh_malloc(ciphertext_length)) != NULL)
    {
      status = ssh_public_key_object_encrypt(key,
                                             plaintext, plaintext_length,
                                             ciphertext, ciphertext_length,
                                             &return_length);
      (*callback)(status, ciphertext, return_length, context);
      ssh_free(ciphertext);
    }
  return NULL;
}

/* Start asyncronous public key signature verify operation. The
   library will call given callback when operation is done. Callback
   may be called immediately during this call. The function
   ssh_operation_abort function may be called to abort this operation
   before it finishes, in which case the callback is not called and
   the SshOperationHandle will be NULL. */
SshOperationHandle
ssh_public_key_verify_async(SshPublicKey handle,
                            const unsigned char *signature,
                            size_t signature_length,
                            const unsigned char *data,
                            size_t data_length,
                            SshPublicKeyVerifyCB callback,
                            void *context)
{
  SshCryptoStatus status;
  SshPublicKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    {
      (*callback)(status, context);
      return NULL;
    }

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    {
      (*callback)(SSH_CRYPTO_HANDLE_INVALID, context);
      return NULL;
    }

  if (key->signature && key->signature->public_key_verify_async)
    {
      SshOperationHandle op;
      SshRGF rgf;

      rgf = ssh_rgf_allocate(key->signature->rgf_def);
      if (rgf == NULL)
        {
          (*callback)(SSH_CRYPTO_OPERATION_FAILED, context);
          return NULL;
        }
      ssh_rgf_hash_update(rgf, data, data_length);

      op = (*key->signature->
            public_key_verify_async)(key->context,
                                     signature, signature_length,
                                     rgf,
                                     callback, context);

      /* The RGF Hash is freed when the handle is destroyed. */
      ssh_operation_attach_destructor(op, ssh_rgf_free_cb, rgf);
      return op;
    }

  if (ssh_public_key_verify_signature(handle,
                                      signature, signature_length,
                                      data, data_length))
    {
      (*callback)(SSH_CRYPTO_OK, context);
    }
  else
    {
      (*callback)(SSH_CRYPTO_SIGNATURE_CHECK_FAILED, context);
    }
  return NULL;

}

/* Start asyncronous public key signature verify operation. As
   ssh_public_key_verify_asycn but with this interface one can give
   the exact digest one self. The library will call given callback
   when operation is done. Callback may be called immediately during
   this call. The function ssh_operation_abort function may be called to
   abort this operation before it finishes, in which case the callback
   is not called and the SshOperationHandle will be NULL. */
SshOperationHandle
ssh_public_key_verify_digest_async(SshPublicKey handle,
                                   const unsigned char *signature,
                                   size_t signature_length,
                                   const unsigned char *digest,
                                   size_t digest_length,
                                   SshPublicKeyVerifyCB callback,
                                   void *context)
{
  SshCryptoStatus status;
  SshPublicKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    {
      (*callback)(status, context);
      return NULL;
    }

  if (!(key = SSH_CRYPTO_HANDLE_TO_PUBLIC_KEY(handle)))
    {
      (*callback)(SSH_CRYPTO_HANDLE_INVALID, context);
      return NULL;
    }

  if (key->signature && key->signature->public_key_verify_async)
    {
      SshRGF rgf;
      SshOperationHandle op;

      rgf = ssh_rgf_allocate(key->signature->rgf_def);
      if (rgf == NULL)
        {
          (*callback)(SSH_CRYPTO_OPERATION_FAILED, context);
          return NULL;
        }
      if (ssh_rgf_hash_update_with_digest(rgf,
                                          digest, digest_length) == FALSE)
        {
          (*callback)(SSH_CRYPTO_OPERATION_FAILED, context);
          ssh_rgf_free(rgf);
          return NULL;
        }

      op =  (*key->signature->
             public_key_verify_async)(key->context,
                                      signature, signature_length,
                                      rgf,
                                      callback, context);

      /* The RGF Hash is freed when the handle is destroyed. */
      ssh_operation_attach_destructor(op, ssh_rgf_free_cb, rgf);
      return op;
    }

  if (ssh_public_key_verify_signature_with_digest(handle,
                                                  signature,  signature_length,
                                                  digest, digest_length))
    {
      (*callback)(SSH_CRYPTO_OK, context);
    }
  else
    {
      (*callback)(SSH_CRYPTO_SIGNATURE_CHECK_FAILED, context);
    }
  return NULL;
}


/* Start asyncronous private key decryption operation. The library will call
   given callback when operation is done. Callback may be called immediately
   during this call. The function ssh_operation_abort function may be called to
   abort this operation before it finishes, in which case the callback is not
   called and the SshOperationHandle will be NULL. */
SshOperationHandle
ssh_private_key_decrypt_async(SshPrivateKey handle,
                              const unsigned char *ciphertext,
                              size_t ciphertext_length,
                              SshPrivateKeyDecryptCB callback,
                              void *context)
{
  unsigned char *plaintext;
  size_t return_length, plaintext_length;
  SshOperationHandle op;
  SshCryptoStatus status;
  SshRGF rgf;
  SshPrivateKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    {
      (*callback)(status, NULL, 0, context);
      return NULL;
    }

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    {
      (*callback)(SSH_CRYPTO_HANDLE_INVALID, NULL, 0, context);
      return NULL;
    }

  if (key->encryption && key->encryption->private_key_decrypt_async)
    {
      rgf = ssh_rgf_allocate(key->encryption->rgf_def);

      if (rgf == NULL)
        {
          (*callback)(SSH_CRYPTO_OPERATION_FAILED, NULL, 0, context);
          return NULL;
        }

      /* Asyncronous operation. */
      op = (*key->encryption->
            private_key_decrypt_async)(key->context,
                                       ciphertext, ciphertext_length,
                                       rgf,
                                       callback, context);

      /* The RGF Hash is freed when the handle is destroyed. */
      ssh_operation_attach_destructor(op, ssh_rgf_free_cb, rgf);
      return op;
    }

  plaintext_length = ssh_private_key_object_max_decrypt_output_len(key);
  if ((plaintext = ssh_malloc(plaintext_length)) != NULL)
    {
      status = ssh_private_key_object_decrypt(key,
                                              ciphertext, ciphertext_length,
                                              plaintext, plaintext_length,
                                              &return_length);
      (*callback)(status, plaintext, return_length, context);
      ssh_free(plaintext);
    }
  return NULL;
}

/* Start asyncronous private key signing operation. The library will
   call given callback when operation is done. Callback may be called
   immediately during this call. The function ssh_operation_abort
   function may be called to abort this operation before it finishes,
   in which case the callback is not called and the SshOperationHandle
   will be NULL. */
SshOperationHandle
ssh_private_key_sign_async(SshPrivateKey handle,
                           const unsigned char *data,
                           size_t data_length,
                           SshPrivateKeySignCB callback,
                           void *context)
{
  unsigned char *signature;
  size_t return_length, signature_length;
  SshCryptoStatus status;
  SshPrivateKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    {
      (*callback)(status, NULL, 0, context);
      return NULL;
    }

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    {
      (*callback)(SSH_CRYPTO_HANDLE_INVALID, NULL, 0, context);
      return NULL;
    }

  if (key->signature &&
      key->signature->private_key_sign_async != NULL_FNPTR)
    {
      SshRGF rgf;
      SshOperationHandle handle;

      rgf = ssh_rgf_allocate(key->signature->rgf_def);
      if (rgf == NULL)
        {
          (*callback)(SSH_CRYPTO_OPERATION_FAILED, NULL, 0, context);
          return NULL;
        }
      ssh_rgf_hash_update(rgf, data, data_length);

      /* Asyncronous operation. */
      handle = (*key->signature->
                private_key_sign_async)(key->context,
                                        rgf,
                                        callback, context);

      /* The RGF Hash is freed when the handle is destroyed. */
      ssh_operation_attach_destructor(handle, ssh_rgf_free_cb, rgf);
      return handle;
    }

  signature_length = ssh_private_key_object_max_signature_output_len(key);
  if ((signature = ssh_malloc(signature_length)) != NULL)
    {
      status = ssh_private_key_object_sign(key,
                                           data, data_length,
                                           signature, signature_length,
                                           &return_length);
      (*callback)(status, signature, return_length, context);
      ssh_free(signature);
    }
  return NULL;
}

/* As ssh_private_key_sign but here one can give the hash digest directly. The
   hash which to use can be requested using
   ssh_private_key_derive_signature_hash function. */

SshOperationHandle
ssh_private_key_sign_digest_async(SshPrivateKey handle,
                                  const unsigned char *digest,
                                  size_t digest_length,
                                  SshPrivateKeySignCB callback,
                                  void *context)
{
  unsigned char *signature;
  size_t return_length, signature_length;
  SshCryptoStatus status;
  SshPrivateKeyObject key;

  if (!ssh_crypto_library_object_check_use(&status))
    {
      (*callback)(status, NULL, 0, context);
      return NULL;
    }

  if (!(key = SSH_CRYPTO_HANDLE_TO_PRIVATE_KEY(handle)))
    {
      (*callback)(SSH_CRYPTO_HANDLE_INVALID, NULL, 0, context);
      return NULL;
    }

  if (key->signature &&
      key->signature->private_key_sign_async != NULL_FNPTR)
    {
      SshRGF rgf;
      SshOperationHandle handle;

      rgf = ssh_rgf_allocate(key->signature->rgf_def);
      if (rgf == NULL)
        {
          (*callback)(SSH_CRYPTO_OPERATION_FAILED, NULL, 0, context);
          return NULL;
        }
      if (ssh_rgf_hash_update_with_digest(rgf,
                                          digest, digest_length) == FALSE)
        {
          (*callback)(SSH_CRYPTO_OPERATION_FAILED, NULL, 0, context);
          ssh_rgf_free(rgf);
          return NULL;
        }

      /* Asyncronous operation. */
      handle = (*key->signature->
                private_key_sign_async)(key->context,
                                        rgf,
                                        callback, context);

      /* The RGF Hash is freed when the handle is destroyed. */
      ssh_operation_attach_destructor(handle, ssh_rgf_free_cb, rgf);
      return handle;
    }

  signature_length = ssh_private_key_object_max_signature_output_len(key);
  if ((signature = ssh_malloc(signature_length)) != NULL)
    {
      status =
        ssh_private_key_object_sign_digest(key,
                                           digest, digest_length,
                                           signature, signature_length,
                                           &return_length);
      (*callback)(status, signature, return_length, context);
      ssh_free(signature);
    }
  return NULL;
}

/* Start asyncronous Diffie-Hellman setup operation. The library will call
   given callback when operation is done. Callback may be called immediately
   during this call. The function ssh_operation_abort function may be called to
   abort this operation before it finishes, in which case the callback is not
   called and the SshOperationHandle will be NULL. */
SshOperationHandle
ssh_pk_group_dh_setup_async(SshPkGroup handle,
                            SshPkGroupDHSetup callback,
                            void *context)
{
  SshPkGroupDHSecret secret;
  unsigned char *exchange;
  size_t exchange_length;
  size_t return_length;
  SshCryptoStatus status;
  SshPkGroupObject group;

  if (!ssh_crypto_library_object_check_use(&status))
    {
      (*callback)(status, NULL, NULL, 0, context);
      return NULL;
    }

  if (!(group = SSH_CRYPTO_HANDLE_TO_PK_GROUP(handle)))
    {
      (*callback)(SSH_CRYPTO_HANDLE_INVALID, NULL, NULL, 0, context);
      return NULL;
    }

  if (group->diffie_hellman &&
      group->diffie_hellman->diffie_hellman_setup_async)
    {
      return (*group->diffie_hellman->diffie_hellman_setup_async)
        (group->context, callback, context);
    }

  exchange_length = ssh_pk_group_dh_setup_max_output_length(handle);
  if ((exchange = ssh_malloc(exchange_length)) != NULL)
    {
      status = ssh_pk_group_dh_setup(handle, &secret,
                                     exchange, exchange_length,
                                     &return_length);
      (*callback)(status, secret, exchange, return_length, context);
      ssh_free(exchange);
    }
  return NULL;
}


/* Start asyncronous Diffie-Hellman agree operation. The library will call
   given callback when operation is done. Callback may be called immediately
   during this call. The function ssh_operation_abort function may be called to
   abort this operation before it finishes, in which case the callback is not
   called and the SshOperationHandle will be NULL. */
SshOperationHandle
ssh_pk_group_dh_agree_async(SshPkGroup handle,
                            SshPkGroupDHSecret secret,
                            const unsigned char *exchange,
                            size_t exchange_length,
                            SshPkGroupDHAgree callback,
                            void *context)
{
  unsigned char *secret_buffer;
  size_t secret_buffer_length;
  size_t return_length;
  SshCryptoStatus status;
  SshPkGroupObject group;

  if (!ssh_crypto_library_object_check_use(&status))
    {
      (*callback)(status, NULL, 0, context);
      return NULL;
    }

  if (!(group = SSH_CRYPTO_HANDLE_TO_PK_GROUP(handle)))
    {
      (*callback)(SSH_CRYPTO_HANDLE_INVALID, NULL, 0, context);
      return NULL;
    }

  if (group->diffie_hellman &&
      group->diffie_hellman->diffie_hellman_agree_async)
    {
      return (*group->diffie_hellman->diffie_hellman_agree_async)
        (group->context,
         (void *)secret,
         exchange, exchange_length,
         callback, context);
    }

  secret_buffer_length = ssh_pk_group_dh_agree_max_output_length(handle);

  if ((secret_buffer = ssh_malloc(secret_buffer_length)) != NULL)
    {
      status = ssh_pk_group_dh_agree(handle,
                                     secret,
                                     exchange, exchange_length,
                                     secret_buffer, secret_buffer_length,
                                     &return_length);
      (*callback)(status, secret_buffer, return_length, context);
      ssh_free(secret_buffer);
    }

  return NULL;
}
