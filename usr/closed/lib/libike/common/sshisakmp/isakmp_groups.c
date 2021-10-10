/*
 *
 * Author: Tero Kivinen <kivinen@iki.fi>
 *
*  Copyright:
*          Copyright (c) 2002, 2003 SFNT Finland Oy.
 */
/*
 *        Program: sshisakmp
 *        $Source: /project/cvs/src/ipsec/lib/sshisakmp/isakmp_groups.c,v $
 *        $Author: irwin $
 *
 *        Creation          : 04:35 Aug 21 1997 kivinen
 *        Last Modification : 15:31 Feb 28 2003 kivinen
 *        Last check in     : $Date: 2003/11/28 20:00:47 $
 *        Revision number   : $Revision: 1.3.2.1 $
 *        State             : $State: Exp $
 *        Version           : 1.245
 *        
 *
 *        Description       : Isakmp default groups
 *
 *        $Log: isakmp_groups.c,v $ *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        $EndLog$
 */

#include "sshincludes.h"
#include "sshmp.h"
#include "isakmp.h"
#include "isakmp_internal.h"
#include "sshdebug.h"
#include "sshtimeouts.h"

#ifdef SUNWIPSEC
#include <ike/pkcs11-glue.h>
#endif

#define SSH_DEBUG_MODULE "SshIkeGroup"

/* Oakley group description structure. Used to describe the default groups for
   all Diffie-Hellman operations. */
typedef struct SshIkeGroupDescRec {
  SshIkeAttributeGrpDescValues descriptor;
  SshIkeAttributeGrpTypeValues type;
  const char *name;
  SshUInt32 strength;           /* (modp, ecp, ec2n) optional */
} *SshIkeGroupDesc,SshIkeGroupDescStruct;

struct SshIkeGroupDescRec const ssh_ike_default_group[] = {
  { SSH_IKE_VALUES_GRP_DESC_DEFAULT_MODP_768,
    SSH_IKE_VALUES_GRP_TYPE_MODP,
    "ietf-ike-grp-modp-768", 0x42, },
  { SSH_IKE_VALUES_GRP_DESC_DEFAULT_MODP_1024,
    SSH_IKE_VALUES_GRP_TYPE_MODP,
    "ietf-ike-grp-modp-1024", 0x4d, },
  { SSH_IKE_VALUES_GRP_DESC_DEFAULT_MODP_1536,
    SSH_IKE_VALUES_GRP_TYPE_MODP,
    "ietf-ike-grp-modp-1536", 0x5b, },








  { 14,
    SSH_IKE_VALUES_GRP_TYPE_MODP,
    "ietf-ike-grp-modp-2048", 110, },
  { 15,
    SSH_IKE_VALUES_GRP_TYPE_MODP,
    "ietf-ike-grp-modp-3072", 130, },
  { 16,
    SSH_IKE_VALUES_GRP_TYPE_MODP,
    "ietf-ike-grp-modp-4096", 150, },
  { 17,
    SSH_IKE_VALUES_GRP_TYPE_MODP,
    "ietf-ike-grp-modp-6144", 170, },
  { 18,
    SSH_IKE_VALUES_GRP_TYPE_MODP,
    "ietf-ike-grp-modp-8192", 190, },
#ifdef SUNWIPSEC
  { 19,
    SSH_IKE_VALUES_GRP_TYPE_ECP,
    "ietf-ike-grp-ecp-256", 190, },
  { 20,
    SSH_IKE_VALUES_GRP_TYPE_ECP,
    "ietf-ike-grp-ecp-384", 200, },
  { 21,
    SSH_IKE_VALUES_GRP_TYPE_ECP,
    "ietf-ike-grp-ecp-521", 210, },
  { 22,
    SSH_IKE_VALUES_GRP_TYPE_MODP,
    "ietf-ike-grp-modp-1024-160", 80, },
  { 23,
    SSH_IKE_VALUES_GRP_TYPE_MODP,
    "ietf-ike-grp-modp-2048-224", 120, },
  { 24,
    SSH_IKE_VALUES_GRP_TYPE_MODP,
    "ietf-ike-grp-modp-2048-256", 129, },
  { 25,
    SSH_IKE_VALUES_GRP_TYPE_ECP,
    "ietf-ike-grp-ecp-192", 192, },
  { 26,
    SSH_IKE_VALUES_GRP_TYPE_ECP,
    "ietf-ike-grp-ecp-224", 224, },
#endif
};

const int ssh_ike_default_group_cnt = sizeof(ssh_ike_default_group) /
  sizeof(ssh_ike_default_group[0]);

SshIkeGroupMap *ssh_ike_groups = NULL;
int ssh_ike_groups_count = 0;
SshUInt32 ssh_ike_groups_alloc_count = 0;


/*                                                              shade{0.9}
 * Create randomizers for group if needed.                      shade{1.0}
 */
void ike_grp_randomizers(void *context)
{
  SshIkeGroupMap grp_map = (SshIkeGroupMap) context;
  unsigned int cur_cnt, max_cnt, cnt;
  SshCryptoStatus cret = SSH_CRYPTO_OK;
  unsigned int next_time;

  cur_cnt = ssh_pk_group_count_randomizers(grp_map->group);

  if (grp_map->descriptor > 10 || grp_map->descriptor < 0)
    {
      next_time = grp_map->isakmp_context->randomizers_private_retry;
      cnt = grp_map->isakmp_context->randomizers_private_cnt;
      max_cnt = grp_map->isakmp_context->randomizers_private_max_cnt;
    }
  else
    {
      next_time = grp_map->isakmp_context->randomizers_default_retry;
      cnt = grp_map->isakmp_context->randomizers_default_cnt;
      max_cnt = grp_map->isakmp_context->randomizers_default_max_cnt;
    }
  if (cur_cnt < max_cnt)
    {
      SSH_DEBUG(6, ("Adding %d randomizers for group %d (cnt = %d/%d)",
                    cnt, grp_map->descriptor, cur_cnt, max_cnt));
      while (cnt-- > 0)
        {
          cret = ssh_pk_group_generate_randomizer(grp_map->group);
          if (cret != SSH_CRYPTO_OK)
            {
              SSH_DEBUG(3, ("ssh_pk_group_generate_randomizer failed: %.200s",
                            ssh_crypto_status_message(cret)));
              break;
            }
        }
    }
  if (cret == SSH_CRYPTO_OK)
    {
      ssh_xregister_idle_timeout(next_time, 0, ike_grp_randomizers,
                                context);
    }
}

#ifdef SSHDIST_EXTERNALKEY
/* This callback is called when the acceleration of a group has
 * terminated.
 */
void ssh_ike_get_acc_group_cb(SshEkStatus status,
                              SshPkGroup accelerated_group,
                              void *context)
{
  SshIkeGroupMap gmap = context;
  gmap->accelerator_handle = NULL;
  if (status == SSH_EK_OK)
    {
      /* We got the accelerated group. Swap the groups so that all the
         subsequent operations use this accelerated group. */
      SshPkGroup group;

      SSH_DEBUG(SSH_D_LOWOK, ("Received accelerator group. "));
      group = gmap->group;
      gmap->group = accelerated_group;
      gmap->old_group = group;
      }
  else
    {
      /* We did not manage to accelerate a group using the
         EK. Continue using the software group. */
      SSH_DEBUG(SSH_D_LOWOK, ("Could not accelerate the group."));
    }
}

#endif /* SSHDIST_EXTERNALKEY */
/*                                                              shade{0.9}
 * Add new group to group table. Return TRUE if successfull.    shade{1.0}
 */
Boolean ike_add_default_group(SshIkeContext isakmp_context, int descriptor,
                              SshPkGroup group)
{
  if (ssh_ike_groups_alloc_count == ssh_ike_groups_count)
    {
      if (ssh_ike_groups_alloc_count == 0)
        {
          ssh_ike_groups_alloc_count = 10;
          ssh_ike_groups = ssh_calloc(ssh_ike_groups_alloc_count,
                                      sizeof(SshIkeGroupMap));
          if (ssh_ike_groups == NULL)
            return FALSE;
        }
      else
        {
          if (!ssh_recalloc(&ssh_ike_groups,
                            &ssh_ike_groups_alloc_count,
                            ssh_ike_groups_alloc_count + 10,
                            sizeof(SshIkeGroupMap)))
            return FALSE;
        }
    }
  ssh_ike_groups[ssh_ike_groups_count] =
    ssh_calloc(1, sizeof(struct SshIkeGroupMapRec));
  if (ssh_ike_groups[ssh_ike_groups_count] == NULL)
    return FALSE;
  ssh_ike_groups[ssh_ike_groups_count]->isakmp_context = isakmp_context;
  ssh_ike_groups[ssh_ike_groups_count]->descriptor = descriptor;
  ssh_ike_groups[ssh_ike_groups_count]->group = group;
#ifdef SSHDIST_EXTERNALKEY
  /* Try fetching the accelerated group, if we have accelerators defined. */
  if (isakmp_context->external_key && isakmp_context->accelerator_short_name)
    {
      SshOperationHandle handle;
      SshIkeGroupMap gmap = ssh_ike_groups[ssh_ike_groups_count];

      /* XXX temporary casts until library API is changed XXX */
      handle =
        ssh_ek_generate_accelerated_group(isakmp_context->external_key,
                                          ssh_csstr(isakmp_context->
                                                    accelerator_short_name),
                                          group,
                                          ssh_ike_get_acc_group_cb,
                                          gmap);
      if (handle)
        gmap->accelerator_handle = handle;
    }
#endif /* SSHDIST_EXTERNALKEY */
  ssh_xregister_idle_timeout(isakmp_context->randomizers_default_retry, 0,
                            ike_grp_randomizers,
                            ssh_ike_groups[ssh_ike_groups_count]);
  ssh_ike_groups_count++;
  return TRUE;
}

/*                                                              shade{0.9}
 * Uninitialize default group data                              shade{1.0}
 */
void ike_default_groups_uninit(SshIkeContext isakmp_context)
{
  int i;
  for (i = 0; i < ssh_ike_groups_count; i++)
    {
      ssh_pk_group_free(ssh_ike_groups[i]->group);
#ifdef SSHDIST_EXTERNALKEY
      if (ssh_ike_groups[i]->old_group)
        ssh_pk_group_free(ssh_ike_groups[i]->old_group);
      ssh_operation_abort(ssh_ike_groups[i]->accelerator_handle);
#endif /* SSHDIST_EXTERNALKEY */
      ssh_cancel_timeouts(SSH_ALL_CALLBACKS, ssh_ike_groups[i]);
      ssh_ike_groups[i]->group = NULL;
      ssh_free(ssh_ike_groups[i]);
      ssh_ike_groups[i] = NULL;
    }
  ssh_free(ssh_ike_groups);
  ssh_ike_groups = NULL;
  ssh_ike_groups_count = 0;
  ssh_ike_groups_alloc_count = 0;
}

/*                                                              shade{0.9}
 * Initialize default group data                                shade{1.0}
 */
Boolean ike_default_groups_init(SshIkeContext isakmp_context)
{
  int i;
  const SshIkeGroupDescStruct* grp;
  SshPkGroup pk_grp;
  SshCryptoStatus cret;
#ifdef SUNWIPSEC
  pkcs11_inst_t *p11i;
#endif

  for (i = 0; i < ssh_ike_default_group_cnt; i++)
    {
      grp = &(ssh_ike_default_group[i]);

      switch (grp->type)
        {
        case SSH_IKE_VALUES_GRP_TYPE_MODP:
          cret = ssh_pk_group_generate(&pk_grp,
                                       "dl-modp",
                                       SSH_PKF_PREDEFINED_GROUP, grp->name,
                                       SSH_PKF_DH, "plain",
                                       SSH_PKF_RANDOMIZER_ENTROPY,
                                       (unsigned int) ((grp->strength * 5)>>1),
                                       SSH_PKF_END);
#ifdef SUNWIPSEC
	  /*
	   * Attempt to accelerate this group with PKCS#11.  If it fails,
	   * revert back to the SafeNet/SSH group.
	   */
	  if (cret == SSH_CRYPTO_OK) {
		  SshPkGroup newgrp;

		  /* Try using a native D-H supporter first. */
		  p11i = find_p11i_flags(P11F_DH);

		  /* Failing that, then find a slot that supports raw RSA. */
		  if (p11i == NULL)
			  p11i = find_p11i_flags(P11F_DH_RSA);

		  if (p11i != NULL) {
			  newgrp = pkcs11_convert_group(p11i, pk_grp);
			  /*
			   * We can always fall back if PKCS#11 support is
			   * failing us somehow.
			   */
			  if (newgrp != NULL)
				  pk_grp = newgrp;
		  }
	  }
#endif
          break;

        case SSH_IKE_VALUES_GRP_TYPE_EC2N:







          cret = SSH_CRYPTO_UNSUPPORTED;

          break;
        case SSH_IKE_VALUES_GRP_TYPE_ECP:







#ifdef SUNWIPSEC
	  p11i = find_p11i_flags(P11F_ECDHP);
	  pk_grp = (p11i == NULL) ? NULL :
	    pkcs11_generate_ecp(p11i, grp->descriptor);
	  if (pk_grp != NULL)
	    cret = SSH_CRYPTO_OK;
	  else
#endif
          cret = SSH_CRYPTO_UNSUPPORTED;
          break;
        default:
          cret = SSH_CRYPTO_UNSUPPORTED;
          break;
        }

      if (cret == SSH_CRYPTO_OK)
        {
          if (!ike_add_default_group(isakmp_context, grp->descriptor, pk_grp))
            {
              ssh_pk_group_free(pk_grp);
	      SSH_DEBUG(4, ("ike_add_default_group failed."));
              return FALSE;
            }
	  SSH_DEBUG(5, ("ike_add_default_group succeeded."));
        }
      else
        {
	  SSH_DEBUG(5, ("cret not OK, instead it's %d:  %s", cret,
			ssh_crypto_status_message(cret)));
#ifdef SUNWIPSEC
	  ssh_policy_sun_info("Failed to add group %s: %s", grp->name,
			      ssh_crypto_status_message(cret));
#endif
          return FALSE;
        }
    }
  return TRUE;
}
