/*
 *
 * Author: Tero Kivinen <kivinen@iki.fi>
 *
*  Copyright:
*          Copyright (c) 2002, 2003 SFNT Finland Oy.
 */
/*
 *        Program: sshisakmp
 *        $Source: /project/cvs/src/ipsec/lib/sshisakmp/isakmp_state.c,v $
 *        $Author: irwin $
 *
 *        Creation          : 14:48 Jul 30 1997 kivinen
 *        Last Modification : 00:28 Oct 21 2002 kivinen
 *        Last check in     : $Date: 2003/11/28 20:00:48 $
 *        Revision number   : $Revision: 1.1.6.2 $
 *        State             : $State: Exp $
 *        Version           : 1.4026
 *        
 *
 *        Description       : Isakmp state machine functions module
 *
 *
 *        $Log: isakmp_state.c,v $ *        
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
#include "isakmp.h"
#include "isakmp_internal.h"
#include "sshdebug.h"
#include "sshtimeouts.h"
#include "sshtcp.h"
#include "sshdsprintf.h"

#define SSH_DEBUG_MODULE "SshIkeState"

/*                                                              shade{0.9}
 * Isakmp state machine description.                            shade{1.0}
 */
/* XXX commit bit not supported yet */
const struct SshIkeStateMachineRec ike_state_machine[] = {
  /* First input packet, Main mode.
     ike_st_i_sa_proposal will set the authentication type */
  { SSH_IKE_ST_START_SA_NEGOTIATION_R, SSH_IKE_ST_MM_SA_R,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_SA, SSH_IKE_FIELDS_VID_N_CERT_CR,
    { ike_st_i_vid, ike_st_i_sa_proposal, ike_st_i_cr, ike_st_i_cert,
      ike_st_i_status_n, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_sa_values,
#ifdef SSHDIST_ISAKMP_AUTH_CERT
      ike_st_o_optional_certs,
#endif /* SSHDIST_ISAKMP_AUTH_CERT */
      ike_st_o_vids, ike_st_o_private, 
#ifdef SUNWIPSEC
      ike_st_o_natd,	/* Only used by RSA encryption, see function code. */
#endif
      NULL_FNPTR } },

  /* First input packet, Aggressive mode.
     ike_st_i_sa_proposal will set the authentication type */
  { SSH_IKE_ST_START_SA_NEGOTIATION_R, SSH_IKE_ST_AM_SA_R,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_NONCE_ID_KE_SA, SSH_IKE_FIELDS_VID_N_CERT_CR_HASH,
    { ike_st_i_vid, ike_st_i_sa_proposal, ike_st_i_nonce,
#ifdef SSHDIST_ISAKMP_AUTH_CERT
      ike_st_i_cert, ike_st_i_hash_key,
#endif /* SSHDIST_ISAKMP_AUTH_CERT */
      ike_st_i_id, ike_st_i_ke, ike_st_i_cr,
      ike_st_i_status_n, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_sa_values, ike_st_o_ke, ike_st_o_nonce, ike_st_o_id,
#ifdef SSHDIST_ISAKMP_AUTH_CERT
      ike_st_o_certs, ike_st_o_cr,
#endif /* SSHDIST_ISAKMP_AUTH_CERT */
      ike_st_o_sig_or_hash, ike_st_o_vids,
      ike_st_o_status_n, ike_st_o_private, ike_st_o_calc_skeyid,
      NULL_FNPTR } },

#ifdef SSHDIST_ISAKMP_AUTH_CERT
  /* Initiate negotiation, Main mode, public key encryption */
  { SSH_IKE_ST_START_SA_NEGOTIATION_I, SSH_IKE_ST_MM_SA_I,
    SSH_IKE_AUTH_METHOD_PUBLIC_KEY_ENCRYPTION, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_NONE, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_sa_proposal, ike_st_o_cr, ike_st_o_vids,
      ike_st_o_private, NULL_FNPTR } },
#endif /* SSHDIST_ISAKMP_AUTH_CERT */

  /* Initiate negotiation, Main mode, any authentication */
  { SSH_IKE_ST_START_SA_NEGOTIATION_I, SSH_IKE_ST_MM_SA_I,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_NONE, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_sa_proposal, ike_st_o_vids, ike_st_o_private, NULL_FNPTR } },

#ifdef SSHDIST_ISAKMP_AUTH_CERT
  /* Initiate negotiation, Aggressive mode, public key encryption */
  { SSH_IKE_ST_START_SA_NEGOTIATION_I, SSH_IKE_ST_AM_SA_I,
    SSH_IKE_AUTH_METHOD_PUBLIC_KEY_ENCRYPTION, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_NONE, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_sa_proposal, ike_st_o_hash_key, ike_st_o_ke,
      ike_st_o_nonce, ike_st_o_id,
      ike_st_o_certs, ike_st_o_cr, ike_st_o_vids, ike_st_o_private,
      NULL_FNPTR } },

  /* Initiate negotiation, Aggressive mode, any signature authentication
     method */
  { SSH_IKE_ST_START_SA_NEGOTIATION_I, SSH_IKE_ST_AM_SA_I,
    SSH_IKE_AUTH_METHOD_SIGNATURES, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_NONE, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_sa_proposal, ike_st_o_ke, ike_st_o_nonce,
      ike_st_o_id, ike_st_o_certs, ike_st_o_cr, ike_st_o_vids,
      ike_st_o_private, NULL_FNPTR } },
#endif /* SSHDIST_ISAKMP_AUTH_CERT */

  /* Initiate negotiation, Aggressive mode, any authentication except
     public key based (handled above) */
  { SSH_IKE_ST_START_SA_NEGOTIATION_I, SSH_IKE_ST_AM_SA_I,
    SSH_IKE_AUTH_METHOD_PRE_SHARED_KEY, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_NONE, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_sa_proposal, ike_st_o_ke, ike_st_o_nonce,
      ike_st_o_id, ike_st_o_vids, ike_st_o_private, NULL_FNPTR } },

  /* Waiting for done (waiting for retransmits), Main mode, any
     authentication mode. If we receive phase 2 packet we can immediately
     advance to done state. */
  { SSH_IKE_ST_MM_FINAL_R, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_done, NULL_FNPTR } },

  /* Waiting for done (waiting for other ends duplicate retransmits), Main
     mode, any authentication mode. If we receive phase 2 packet we can
     immediately advance to done state. */
  { SSH_IKE_ST_MM_DONE_I, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_done, NULL_FNPTR } },

  /* Waiting for done (waiting for retransmits), Aggressive mode, any
     authentication mode. If we receive phase 2 packet we can
     immediately go to that state. */
  { SSH_IKE_ST_AM_FINAL_I, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_done, NULL_FNPTR } },

  /* Waiting for done (waiting for other ends duplicate retransmits),
     Aggressive mode, any authentication mode. If we receive phase 2 packet we
     can immediately go to that state. */
  { SSH_IKE_ST_AM_DONE_R, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_done, NULL_FNPTR } },

#ifdef SSHDIST_ISAKMP_AUTH_CERT
  /* Main mode, using signatures */
  /* Responses to other than first packet */
  { SSH_IKE_ST_MM_SA_I, SSH_IKE_ST_MM_KE_I,
    SSH_IKE_AUTH_METHOD_SIGNATURES, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_SA, SSH_IKE_FIELDS_VID_N_CERT_CR,
    { ike_st_i_sa_value, ike_st_i_cr, ike_st_i_cert,
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_ke, ike_st_o_nonce, ike_st_o_cr, ike_st_o_private, 
#ifdef SUNWIPSEC
      ike_st_o_natd,
#endif
      NULL }
  },
  { SSH_IKE_ST_MM_SA_R, SSH_IKE_ST_MM_KE_R,
    SSH_IKE_AUTH_METHOD_SIGNATURES, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_NONCE_KE, SSH_IKE_FIELDS_VID_N_CERT_CR
#ifdef SUNWIPSEC
	| SSH_IKE_FIELDS_NATD
#endif	
	,
    { ike_st_i_nonce, ike_st_i_ke, ike_st_i_cr,
      ike_st_i_status_n, ike_st_i_cert, ike_st_i_vid, ike_st_i_private,
#ifdef SUNWIPSEC
      ike_st_i_natd,
#endif
      NULL_FNPTR
	},
    { ike_st_o_ke, ike_st_o_nonce, ike_st_o_cr, ike_st_o_private,
      ike_st_o_calc_skeyid,
#ifdef SUNWIPSEC
      ike_st_o_natd,
#endif
      NULL_FNPTR
    }
  },
  { SSH_IKE_ST_MM_KE_I, SSH_IKE_ST_MM_FINAL_I,
    SSH_IKE_AUTH_METHOD_SIGNATURES, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_NONCE_KE, SSH_IKE_FIELDS_VID_N_CERT_CR
#ifdef SUNWIPSEC
	| SSH_IKE_FIELDS_NATD
#endif	
	,
    { ike_st_i_nonce, ike_st_i_ke, ike_st_i_cr,
      ike_st_i_status_n, ike_st_i_cert, ike_st_i_vid, ike_st_i_private,
#ifdef SUNWIPSEC
	  ike_st_i_natd, ike_st_i_natt_portjump,
#endif	  
	  NULL_FNPTR },
    { ike_st_o_id, ike_st_o_certs, ike_st_o_sig, ike_st_o_status_n,
      ike_st_o_private, ike_st_o_encrypt, NULL_FNPTR } },
  { SSH_IKE_ST_MM_KE_R, SSH_IKE_ST_MM_FINAL_R,
    SSH_IKE_AUTH_METHOD_SIGNATURES, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_SIG_ID, SSH_IKE_FIELDS_VID_N_CERT_CR,
    { ike_st_i_encrypt, ike_st_i_cert, ike_st_i_id, ike_st_i_sig,
      ike_st_i_status_n, ike_st_i_cr, ike_st_i_vid,
#ifdef SUNWIPSEC
	  ike_st_i_natt_portjump,
#endif
	  ike_st_i_private,	  
	  NULL_FNPTR },
    { ike_st_o_id, ike_st_o_certs, ike_st_o_sig, ike_st_o_status_n,
      ike_st_o_private, ike_st_o_encrypt, ike_st_o_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_MM_FINAL_I, SSH_IKE_ST_MM_DONE_I,
    SSH_IKE_AUTH_METHOD_SIGNATURES, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_SIG_ID, SSH_IKE_FIELDS_VID_N_CERT,
    { ike_st_i_encrypt, ike_st_i_cert, ike_st_i_id, ike_st_i_sig,
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_copy_iv, ike_st_o_wait_done, NULL_FNPTR } },

  /* Aggressive mode, using signatures */
  /* Responses to other than first packet */
  { SSH_IKE_ST_AM_SA_I, SSH_IKE_ST_AM_FINAL_I,
    SSH_IKE_AUTH_METHOD_SIGNATURES, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_NONCE_SIG_ID_KE_SA, SSH_IKE_FIELDS_VID_N_CERT_CR,
    { ike_st_i_sa_value, ike_st_i_nonce,
      ike_st_i_id, ike_st_i_ke, ike_st_i_cert, ike_st_i_sig,
      ike_st_i_cr, ike_st_i_status_n, ike_st_i_vid, ike_st_i_private,
      NULL_FNPTR },
    { ike_st_o_certs, ike_st_o_sig, ike_st_o_status_n, ike_st_o_private,
      ike_st_o_optional_encrypt, ike_st_o_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_AM_SA_R, SSH_IKE_ST_AM_DONE_R,
    SSH_IKE_AUTH_METHOD_SIGNATURES, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_SIG, SSH_IKE_FIELDS_VID_N_CERT,
    { ike_st_i_cert, ike_st_i_sig, ike_st_i_status_n,
      ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_copy_iv, ike_st_o_wait_done, NULL_FNPTR } },

  /* Main mode, using encryption */
  /* Responses to other than first packet */
  { SSH_IKE_ST_MM_SA_I, SSH_IKE_ST_MM_KE_I,
    SSH_IKE_AUTH_METHOD_PUBLIC_KEY_ENCRYPTION, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_SA, SSH_IKE_FIELDS_VID_N_CERT_CR
#ifdef SUNWIPSEC
    | SSH_IKE_FIELDS_NATD
#endif
    ,
    { ike_st_i_sa_value, ike_st_i_cr, ike_st_i_cert,
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private,
#ifdef SUNWIPSEC
      ike_st_i_natd, ike_st_i_natt_portjump,
#endif
      NULL_FNPTR },
    { ike_st_o_hash_key, ike_st_o_nonce, ike_st_o_ke,
      ike_st_o_id, ike_st_o_optional_certs, ike_st_o_private,
#ifdef SUNWIPSEC
	  ike_st_o_natd,
#endif	  
	  NULL_FNPTR } },
  { SSH_IKE_ST_MM_SA_R, SSH_IKE_ST_MM_KE_R,
    SSH_IKE_AUTH_METHOD_PUBLIC_KEY_ENCRYPTION, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_NONCE_ID_KE, SSH_IKE_FIELDS_VID_N_CERT_CR_HASH
#ifdef SUNWIPSEC
	| SSH_IKE_FIELDS_NATD
#endif	
	,
    { ike_st_i_nonce, ike_st_i_hash_key, ike_st_i_cert,
      ike_st_i_id, ike_st_i_ke, ike_st_i_cr,
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private,
#ifdef SUNWIPSEC
	  ike_st_i_natd, ike_st_i_natt_portjump,
#endif	  
	  NULL_FNPTR },
    { ike_st_o_nonce, ike_st_o_ke, ike_st_o_id, ike_st_o_cr,
      ike_st_o_private,
      ike_st_o_calc_skeyid, NULL_FNPTR } },
  { SSH_IKE_ST_MM_KE_I, SSH_IKE_ST_MM_FINAL_I,
    SSH_IKE_AUTH_METHOD_PUBLIC_KEY_ENCRYPTION, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_NONCE_ID_KE, SSH_IKE_FIELDS_VID_N_CERT_CR,
    { ike_st_i_nonce, ike_st_i_cert, ike_st_i_id, ike_st_i_ke,
      ike_st_i_cr, ike_st_i_status_n, ike_st_i_vid, ike_st_i_private,
      NULL_FNPTR },
    { ike_st_o_hash, ike_st_o_status_n,
      ike_st_o_certs, ike_st_o_private, ike_st_o_encrypt, NULL_FNPTR } },
  { SSH_IKE_ST_MM_KE_R, SSH_IKE_ST_MM_FINAL_R,
    SSH_IKE_AUTH_METHOD_PUBLIC_KEY_ENCRYPTION, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_HASH, SSH_IKE_FIELDS_VID_N_CERT,
    { ike_st_i_encrypt, ike_st_i_cert, ike_st_i_hash,
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_hash, ike_st_o_status_n, ike_st_o_certs, ike_st_o_private,
      ike_st_o_encrypt, ike_st_o_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_MM_FINAL_I, SSH_IKE_ST_MM_DONE_I,
    SSH_IKE_AUTH_METHOD_PUBLIC_KEY_ENCRYPTION, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_HASH, SSH_IKE_FIELDS_VID_N_CERT,
    { ike_st_i_encrypt, ike_st_i_cert, ike_st_i_hash,
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_copy_iv, ike_st_o_wait_done, NULL_FNPTR } },

  /* Aggressive mode, using encryption */
  /* Responses to other than first packet */
  { SSH_IKE_ST_AM_SA_I, SSH_IKE_ST_AM_FINAL_I,
    SSH_IKE_AUTH_METHOD_PUBLIC_KEY_ENCRYPTION, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_HASH_NONCE_ID_KE_SA, SSH_IKE_FIELDS_VID_N_CERT_CR,
    { ike_st_i_sa_value, ike_st_i_nonce, ike_st_i_cert,
      ike_st_i_id, ike_st_i_ke, ike_st_i_hash, ike_st_i_cr,
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_hash, ike_st_o_certs, ike_st_o_status_n, ike_st_o_private,
      ike_st_o_optional_encrypt, ike_st_o_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_AM_SA_R, SSH_IKE_ST_AM_DONE_R,
    SSH_IKE_AUTH_METHOD_PUBLIC_KEY_ENCRYPTION, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_HASH, SSH_IKE_FIELDS_VID_N_CERT,
    { ike_st_i_hash, ike_st_i_cert, ike_st_i_status_n,
      ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_copy_iv, ike_st_o_wait_done, NULL_FNPTR } },
#endif /* SSHDIST_ISAKMP_AUTH_CERT */

  /* Main mode, using pre-shared key */
  /* Responses to other than first packet */
  { SSH_IKE_ST_MM_SA_I, SSH_IKE_ST_MM_KE_I,
    SSH_IKE_AUTH_METHOD_PRE_SHARED_KEY, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_SA, SSH_IKE_FIELDS_VID_N_CERT_CR,
    { ike_st_i_sa_value, ike_st_i_cr,
#ifdef SSHDIST_ISAKMP_AUTH_CERT
      ike_st_i_cert,
#endif /* SSHDIST_ISAKMP_AUTH_CERT */
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_ke, ike_st_o_nonce, ike_st_o_private,
#ifdef SUNWIPSEC
	  ike_st_o_natd,
#endif	  
	  NULL_FNPTR } },
  { SSH_IKE_ST_MM_SA_R, SSH_IKE_ST_MM_KE_R,
    SSH_IKE_AUTH_METHOD_PRE_SHARED_KEY, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_NONCE_KE, SSH_IKE_FIELDS_VID_N_CERT_CR
#ifdef SUNWIPSEC
	| SSH_IKE_FIELDS_NATD
#endif		
	,
    { ike_st_i_nonce, ike_st_i_ke,
#ifdef SSHDIST_ISAKMP_AUTH_CERT
      ike_st_i_cr, ike_st_i_cert,
#endif /* SSHDIST_ISAKMP_AUTH_CERT */
#ifdef SUNWIPSEC
	  ike_st_i_natd,
#endif	  
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_ke, ike_st_o_nonce, ike_st_o_get_pre_shared_key,
      ike_st_o_private, ike_st_o_calc_skeyid,
#ifdef SUNWIPSEC
	  ike_st_o_natd,
#endif	  
	  NULL_FNPTR } },
  { SSH_IKE_ST_MM_KE_I, SSH_IKE_ST_MM_FINAL_I,
    SSH_IKE_AUTH_METHOD_PRE_SHARED_KEY, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_NONCE_KE, SSH_IKE_FIELDS_VID_N_CERT_CR
#ifdef SUNWIPSEC
	| SSH_IKE_FIELDS_NATD
#endif		
	,
    { ike_st_i_nonce, ike_st_i_ke,
#ifdef SSHDIST_ISAKMP_AUTH_CERT
      ike_st_i_cr, ike_st_i_cert,
#endif /* SSHDIST_ISAKMP_AUTH_CERT */
#ifdef SUNWIPSEC
	  ike_st_i_natd, ike_st_i_natt_portjump,
#endif 	  
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_id, ike_st_o_hash, ike_st_o_status_n, ike_st_o_private,
      ike_st_o_encrypt, NULL_FNPTR } },
  { SSH_IKE_ST_MM_KE_R, SSH_IKE_ST_MM_FINAL_R,
    SSH_IKE_AUTH_METHOD_PRE_SHARED_KEY, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_HASH_ID, SSH_IKE_FIELDS_VID_N_CERT,
    { ike_st_i_encrypt, ike_st_i_id, ike_st_i_hash,
#ifdef SSHDIST_ISAKMP_AUTH_CERT
      ike_st_i_cert,
#endif /* SSHDIST_ISAKMP_AUTH_CERT */
#ifdef SUNWIPSEC
	  ike_st_i_natt_portjump,
#endif	  
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_id, ike_st_o_hash, ike_st_o_status_n, ike_st_o_private,
      ike_st_o_encrypt, ike_st_o_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_MM_FINAL_I, SSH_IKE_ST_MM_DONE_I,
    SSH_IKE_AUTH_METHOD_PRE_SHARED_KEY, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_HASH_ID, SSH_IKE_FIELDS_VID_N_CERT,
    { ike_st_i_encrypt, ike_st_i_id, ike_st_i_hash,
#ifdef SSHDIST_ISAKMP_AUTH_CERT
      ike_st_i_cert,
#endif /* SSHDIST_ISAKMP_AUTH_CERT */
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_copy_iv, ike_st_o_wait_done, NULL_FNPTR } },

  /* Aggressive mode, using pre-shared key */
  /* Responses to other than first packet */
  { SSH_IKE_ST_AM_SA_I, SSH_IKE_ST_AM_FINAL_I,
    SSH_IKE_AUTH_METHOD_PRE_SHARED_KEY, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_HASH_NONCE_ID_KE_SA, SSH_IKE_FIELDS_VID_N_CERT,
    { ike_st_i_sa_value, ike_st_i_nonce,
      ike_st_i_id, ike_st_i_ke, ike_st_i_hash,
#ifdef SSHDIST_ISAKMP_AUTH_CERT
      ike_st_i_cert,
#endif /* SSHDIST_ISAKMP_AUTH_CERT */
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_hash, ike_st_o_status_n, ike_st_o_private,
      ike_st_o_optional_encrypt, ike_st_o_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_AM_SA_R, SSH_IKE_ST_AM_DONE_R,
    SSH_IKE_AUTH_METHOD_PRE_SHARED_KEY, SSH_IKE_XCHG_TYPE_AGGR,
    SSH_IKE_FIELDS_HASH, SSH_IKE_FIELDS_VID_N_CERT,
    { ike_st_i_hash,
#ifdef SSHDIST_ISAKMP_AUTH_CERT
      ike_st_i_cert,
#endif /* SSHDIST_ISAKMP_AUTH_CERT */
      ike_st_i_status_n, ike_st_i_vid, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_copy_iv, ike_st_o_wait_done, NULL_FNPTR } },

  /* First input packet in initiator, Main mode, no authentication method set
     yet. ike_st_i_sa_value will set the authentication type, and then we
     will retry. */
  { SSH_IKE_ST_MM_SA_I, SSH_IKE_ST_MM_SA_I,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_IP,
    SSH_IKE_FIELDS_SA, SSH_IKE_FIELDS_VID_N_CERT_CR,
    { ike_st_i_sa_value, ike_st_i_retry_now, NULL_FNPTR },
    { NULL_FNPTR } },

  /* Quick mode, phase 2 (== only after isakmp sa is already created) */
  { SSH_IKE_ST_START_QM_I, SSH_IKE_ST_QM_HASH_SA_I,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_QM,
    SSH_IKE_FIELDS_NONE, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_qm_hash_1, ike_st_o_qm_sa_proposals,
      ike_st_o_qm_nonce, ike_st_o_qm_optional_ke,
      ike_st_o_qm_optional_ids, 
#ifdef SUNWIPSEC
	  ike_st_o_natoa,
#endif
	  ike_st_o_private, ike_st_o_encrypt,
	  NULL_FNPTR } },
  { SSH_IKE_ST_START_QM_R, SSH_IKE_ST_QM_HASH_SA_R,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_QM,
    SSH_IKE_FIELDS_HASH_NONCE_SA, SSH_IKE_FIELDS_N_ID_KE
#ifdef SUNWIPSEC
	| SSH_IKE_FIELDS_NATOA
#endif		
	,
    { ike_st_i_encrypt, ike_st_i_qm_hash_1,
      ike_st_i_qm_nonce, ike_st_i_qm_ids,
#ifdef SUNWIPSEC
	  ike_st_i_natoa,
#endif	  
      ike_st_i_qm_ke, ike_st_i_qm_sa_proposals,
      ike_st_i_status_n, ike_st_i_private,
	  NULL_FNPTR },
    { ike_st_o_qm_hash_2, ike_st_o_qm_sa_values,
      ike_st_o_qm_nonce, ike_st_o_qm_optional_ke,
      ike_st_o_qm_optional_ids,
      ike_st_o_qm_optional_responder_lifetime_n, /* Must be after sa_values */
      ike_st_o_private, 
#ifdef SUNWIPSEC
	  ike_st_o_natoa,
#endif
	  ike_st_o_encrypt,	  
	  NULL_FNPTR } },
  { SSH_IKE_ST_QM_HASH_SA_I, SSH_IKE_ST_QM_HASH_I,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_QM,
    SSH_IKE_FIELDS_HASH_NONCE_SA, SSH_IKE_FIELDS_N_ID_KE
#ifdef SUNWIPSEC
	| SSH_IKE_FIELDS_NATOA
#endif
	,
    { ike_st_i_encrypt, ike_st_i_qm_hash_2,
      ike_st_i_qm_sa_values, ike_st_i_qm_nonce,
      ike_st_i_qm_ids, ike_st_i_qm_ke,
      ike_st_i_status_n, ike_st_i_private,
#ifdef SUNWIPSEC
	  ike_st_i_natoa,
#endif	  
	  NULL_FNPTR },
    { ike_st_o_qm_hash_3, ike_st_o_private,
	  ike_st_o_encrypt, ike_st_o_qm_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_QM_HASH_SA_R, SSH_IKE_ST_QM_DONE_R,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_QM,
    SSH_IKE_FIELDS_HASH, SSH_IKE_FIELDS_N
#ifdef SUNWIPSEC
	| SSH_IKE_FIELDS_NATOA
#endif	
	,
    { ike_st_i_encrypt, ike_st_i_qm_hash_3, ike_st_i_status_n,
      ike_st_i_private,
#ifdef SUNWIPSEC
	  ike_st_i_natoa,
#endif	  
	  NULL_FNPTR },
    { ike_st_o_qm_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_QM_HASH_I, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_QM,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_qm_done, NULL_FNPTR} },
  { SSH_IKE_ST_QM_DONE_R, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_QM,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_qm_done, NULL_FNPTR} },

  /* New group mode, phase 1.5 (== only after isakmp sa is
     already created, but before quick mode which uses it) */
  { SSH_IKE_ST_START_NGM_I, SSH_IKE_ST_NGM_HASH_SA_I,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_NGM,
    SSH_IKE_FIELDS_NONE, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_gen_hash, ike_st_o_ngm_sa_proposal, ike_st_o_private,
      ike_st_o_encrypt, NULL_FNPTR } },
  { SSH_IKE_ST_START_NGM_R, SSH_IKE_ST_NGM_HASH_SA_R,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_NGM,
    SSH_IKE_FIELDS_HASH_SA, SSH_IKE_FIELDS_NONE,
    { ike_st_i_encrypt, ike_st_i_gen_hash,
      ike_st_i_ngm_sa_proposal, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_gen_hash, ike_st_o_ngm_sa_values, ike_st_o_private,
      ike_st_o_encrypt, ike_st_o_ngm_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_NGM_HASH_SA_I, SSH_IKE_ST_NGM_DONE_I,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_NGM,
    SSH_IKE_FIELDS_HASH_SA, SSH_IKE_FIELDS_NONE,
    { ike_st_i_encrypt, ike_st_i_gen_hash,
      ike_st_i_ngm_sa_values, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_ngm_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_NGM_HASH_SA_R, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_NGM,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_ngm_done, NULL_FNPTR } },
  { SSH_IKE_ST_NGM_DONE_I, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_NGM,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_ngm_done, NULL_FNPTR } },

#ifdef SSHDIST_ISAKMP_CFG_MODE
  /* Configuration mode, phase II */
  { SSH_IKE_ST_START_CFG_I, SSH_IKE_ST_CFG_HASH_ATTR_I,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_CFG,
    SSH_IKE_FIELDS_NONE, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_gen_hash, ike_st_o_cfg_attr, ike_st_o_private,
      ike_st_o_encrypt, NULL_FNPTR } },
  { SSH_IKE_ST_START_CFG_R, SSH_IKE_ST_CFG_HASH_ATTR_R,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_CFG,
    SSH_IKE_FIELDS_ATTR_HASH, SSH_IKE_FIELDS_NONE,
    { ike_st_i_encrypt, ike_st_i_gen_hash,
      ike_st_i_cfg_attr, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_gen_hash, ike_st_o_cfg_attr, ike_st_o_private,
      ike_st_o_encrypt, ike_st_o_cfg_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_CFG_HASH_ATTR_I, SSH_IKE_ST_CFG_DONE_I,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_CFG,
    SSH_IKE_FIELDS_ATTR_HASH, SSH_IKE_FIELDS_NONE,
    { ike_st_i_encrypt, ike_st_i_gen_hash,
      ike_st_i_cfg_attr, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_cfg_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_CFG_HASH_ATTR_R, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_CFG,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_cfg_done, NULL_FNPTR } },
  { SSH_IKE_ST_CFG_DONE_I, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_CFG,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_cfg_done, NULL_FNPTR } },

  /* Configuration mode, phase I */
  { SSH_IKE_ST_START_CFG_I, SSH_IKE_ST_CFG_HASH_ATTR_I,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_CFG,
    SSH_IKE_FIELDS_NONE, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_cfg_attr, ike_st_o_private, NULL_FNPTR } },
  { SSH_IKE_ST_START_CFG_R, SSH_IKE_ST_CFG_HASH_ATTR_R,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_CFG,
    SSH_IKE_FIELDS_ATTR, SSH_IKE_FIELDS_NONE,
    { ike_st_i_cfg_attr, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_cfg_attr, ike_st_o_private, ike_st_o_cfg_wait_done,
      NULL_FNPTR } },
  { SSH_IKE_ST_CFG_HASH_ATTR_I, SSH_IKE_ST_CFG_DONE_I,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_CFG,
    SSH_IKE_FIELDS_ATTR, SSH_IKE_FIELDS_NONE,
    { ike_st_i_cfg_attr, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_cfg_wait_done, NULL_FNPTR } },
  { SSH_IKE_ST_CFG_HASH_ATTR_R, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_CFG,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_cfg_done, NULL_FNPTR } },
  { SSH_IKE_ST_CFG_DONE_I, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_CFG,
    SSH_IKE_FIELDS_ANY, SSH_IKE_FIELDS_NONE,
    { NULL_FNPTR },
    { ike_st_o_cfg_done, NULL_FNPTR } },
#endif /* SSHDIST_ISAKMP_CFG_MODE */

  /* Notify messages (in phase 2) */
  { SSH_IKE_ST_ANY, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_ANY,
    SSH_IKE_FIELDS_N_HASH, SSH_IKE_FIELDS_ANY,
    { ike_st_i_encrypt, ike_st_i_gen_hash,
      ike_st_i_n, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_n_done, NULL_FNPTR } },

  /* Delete messages (in phase 2) */
  { SSH_IKE_ST_DONE, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_PHASE_1, SSH_IKE_XCHG_TYPE_ANY,
    SSH_IKE_FIELDS_D_HASH, SSH_IKE_FIELDS_ANY,
    { ike_st_i_encrypt, ike_st_i_gen_hash,
      ike_st_i_d, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_d_done, NULL_FNPTR } },

  /* Notify messages (any state) */
  { SSH_IKE_ST_ANY, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_ANY,
    SSH_IKE_FIELDS_N, SSH_IKE_FIELDS_ANY,
    { ike_st_i_n, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_n_done, NULL_FNPTR } },

  /* Delete messages (any state) */
  { SSH_IKE_ST_ANY, SSH_IKE_ST_DONE,
    SSH_IKE_AUTH_METHOD_ANY, SSH_IKE_XCHG_TYPE_ANY,
    SSH_IKE_FIELDS_D, SSH_IKE_FIELDS_ANY,
    { ike_st_i_d, ike_st_i_private, NULL_FNPTR },
    { ike_st_o_d_done, NULL_FNPTR } }
};

const int ike_state_machine_size = sizeof(ike_state_machine) /
sizeof(struct SshIkeStateMachineRec);

/*                                                              shade{0.9}
 * Isakmp state machine. Return 0 if everything ok. Return
 * SshIkeNotifyMessageType error if any errors, in processing
 * message. If error is returned then isakmp_packet_out is
 * not allocated, but the state in isakmp_sa might still be
 * updated. Note that packet is added to isakmp_sa state, so
 * it must not be freed.                                        shade{1.0}
 */
SshIkeNotifyMessageType ike_state_step(SshIkeContext isakmp_context,
                                       SshIkePacket isakmp_packet_in,
                                       SshIkePacket *isakmp_packet_out,
                                       SshIkeSA isakmp_sa,
                                       SshIkeNegotiation negotiation)
{
  int i;
  SshIkeFields fields;
  Boolean rerun = FALSE;
  SshIkeNegotiation o_neg;
#ifdef DEBUG_LIGHT
  const char *func_name;
  const char *state_name;
  Boolean this_end_is_initiator;
#endif /* DEBUG_LIGHT */

  o_neg = isakmp_sa->isakmp_negotiation;

  if (negotiation->ed)
    {
      if (isakmp_packet_in != NULL)
        {
          negotiation->ed->
            packets_in[negotiation->ed->
                      number_of_packets_in++] = isakmp_packet_in;
        }
      else
        {
          if (negotiation->ed->number_of_packets_in != 0)
            isakmp_packet_in =
              negotiation->ed->packets_in[negotiation->ed->
                                         number_of_packets_in - 1];
        }
    }

  if (isakmp_packet_out != NULL)
    *isakmp_packet_out = NULL;

  if (negotiation->ed->current_state_function > 0xff)
    {
      *isakmp_packet_out = negotiation->ed->isakmp_packet_out;
      negotiation->ed->isakmp_packet_out = NULL;
    }

  /* Initialize fields bitmask */
  fields = SSH_IKE_FIELDS_NONE;
  if (isakmp_packet_in != NULL)
    {
      if (isakmp_packet_in->first_sa_payload != NULL)
        fields |= SSH_IKE_FIELDS_SA;
      if (isakmp_packet_in->first_ke_payload != NULL)
        fields |= SSH_IKE_FIELDS_KE;
      if (isakmp_packet_in->first_id_payload != NULL)
        fields |= SSH_IKE_FIELDS_ID;
      if (isakmp_packet_in->first_cert_payload != NULL)
        fields |= SSH_IKE_FIELDS_CERT;
      if (isakmp_packet_in->first_cr_payload != NULL)
        fields |= SSH_IKE_FIELDS_CR;
      if (isakmp_packet_in->first_hash_payload != NULL)
        fields |= SSH_IKE_FIELDS_HASH;
      if (isakmp_packet_in->first_sig_payload != NULL)
        fields |= SSH_IKE_FIELDS_SIG;
      if (isakmp_packet_in->first_nonce_payload != NULL)
        fields |= SSH_IKE_FIELDS_NONCE;
      if (isakmp_packet_in->first_n_payload != NULL)
        fields |= SSH_IKE_FIELDS_N;
      if (isakmp_packet_in->first_d_payload != NULL)
        fields |= SSH_IKE_FIELDS_D;
      if (isakmp_packet_in->first_vid_payload != NULL)
        fields |= SSH_IKE_FIELDS_VID;
#ifdef SSHDIST_ISAKMP_CFG_MODE
      if (isakmp_packet_in->first_attr_payload != NULL)
        fields |= SSH_IKE_FIELDS_ATTR;
#endif /* SSHDIST_ISAKMP_CFG_MODE */
#ifdef SUNWIPSEC
      if (isakmp_packet_in->first_natd_payload != NULL)
	  fields |= SSH_IKE_FIELDS_NATD;
      if (isakmp_packet_in->first_natoa_payload != NULL)
	  fields |= SSH_IKE_FIELDS_NATOA;	  
#endif	  
    }

#ifdef SSHDIST_ISAKMP_CFG_MODE
  SSH_IKE_DEBUG(6, negotiation,
                ("Version = %d.%d, "
                 "Input packet fields = %04x %s%s%s%s%s%s%s%s%s%s%s",
                 o_neg->ike_pm_info->major_version,
                 o_neg->ike_pm_info->minor_version,
                 fields,
                 (fields & SSH_IKE_FIELDS_SA ? "SA " : ""),
                 (fields & SSH_IKE_FIELDS_KE ? "KE " : ""),
                 (fields & SSH_IKE_FIELDS_ID ? "ID " : ""),
                 (fields & SSH_IKE_FIELDS_CERT ? "CERT " : ""),
                 (fields & SSH_IKE_FIELDS_CR ? "CR " : ""),
                 (fields & SSH_IKE_FIELDS_HASH ? "HASH " : ""),
                 (fields & SSH_IKE_FIELDS_SIG ? "SIG " : ""),
                 (fields & SSH_IKE_FIELDS_NONCE ? "NONCE " : ""),
                 (fields & SSH_IKE_FIELDS_N ? "N " : ""),
                 (fields & SSH_IKE_FIELDS_D ? "D " : ""),
                 (fields & SSH_IKE_FIELDS_VID ? "VID " : ""),
                 (fields & SSH_IKE_FIELDS_ATTR ? "ATTR " : "")));
#else /* SSHDIST_ISAKMP_CFG_MODE */
  SSH_IKE_DEBUG(6, negotiation,
                ("Version = %d.%d, "
                 "Input packet fields = %04x %s%s%s%s%s%s%s%s%s%s",
                 o_neg->ike_pm_info->major_version,
                 o_neg->ike_pm_info->minor_version,
                 fields,
                 (fields & SSH_IKE_FIELDS_SA ? "SA " : ""),
                 (fields & SSH_IKE_FIELDS_KE ? "KE " : ""),
                 (fields & SSH_IKE_FIELDS_ID ? "ID " : ""),
                 (fields & SSH_IKE_FIELDS_CERT ? "CERT " : ""),
                 (fields & SSH_IKE_FIELDS_CR ? "CR " : ""),
                 (fields & SSH_IKE_FIELDS_HASH ? "HASH " : ""),
                 (fields & SSH_IKE_FIELDS_SIG ? "SIG " : ""),
                 (fields & SSH_IKE_FIELDS_NONCE ? "NONCE " : ""),
                 (fields & SSH_IKE_FIELDS_N ? "N " : ""),
                 (fields & SSH_IKE_FIELDS_D ? "D " : ""),
                 (fields & SSH_IKE_FIELDS_VID ? "VID " : "")));
                 (fields & SSH_IKE_FIELDS_ATTR ? "ATTR " : ""),
				 ));

















#endif /* SSHDIST_ISAKMP_CFG_MODE */

restart:
#ifdef DEBUG_LIGHT
  state_name = ssh_find_keyword_name(ssh_ike_state_name_keywords,
                                     negotiation->ed->current_state);
  if (state_name == NULL)
    state_name = "unknown";
  if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_QM)
    this_end_is_initiator = negotiation->qm_pm_info->this_end_is_initiator;
  else if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_NGM)
    this_end_is_initiator = negotiation->ngm_pm_info->this_end_is_initiator;
  else if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_INFO)
    this_end_is_initiator = negotiation->info_pm_info->this_end_is_initiator;
#ifdef SSHDIST_ISAKMP_CFG_MODE
  else if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_CFG)
    this_end_is_initiator = negotiation->cfg_pm_info->this_end_is_initiator;
#endif /* SSHDIST_ISAKMP_CFG_MODE */
  else
    this_end_is_initiator = negotiation->ike_pm_info->this_end_is_initiator;
#endif /* DEBUG_LIGHT */
#ifdef SSHDIST_ISAKMP_AUTH_CERT
  SSH_DEBUG(6, ("Current state = %s (%d)/%d, "
                "exchange = %d, auth_method = %s, %s",
                state_name,
                negotiation->ed->current_state,
                negotiation->ed->current_state_function,
                negotiation->exchange_type,
                (negotiation->ed->auth_method_type ==
                 SSH_IKE_AUTH_METHOD_ANY ? "any" :
                 (negotiation->ed->auth_method_type ==
                  SSH_IKE_AUTH_METHOD_PHASE_1 ? "phase1" :
                  (negotiation->ed->auth_method_type ==
                   SSH_IKE_AUTH_METHOD_SIGNATURES ? "signatures" :
                   (negotiation->ed->auth_method_type ==
                    SSH_IKE_AUTH_METHOD_PUBLIC_KEY_ENCRYPTION ? "pub key" :
                    (negotiation->ed->auth_method_type ==
                     SSH_IKE_AUTH_METHOD_PRE_SHARED_KEY ? "pre shared key" :
                     "unknown"))))),
                this_end_is_initiator ? "Initiator" : "Responder"));
#else /* SSHDIST_ISAKMP_AUTH_CERT */
  SSH_DEBUG(6, ("Current state = %s (%d)/%d, "
                "exchange = %d, auth_method = %s, %s",
                state_name,
                negotiation->ed->current_state,
                negotiation->ed->current_state_function,
                negotiation->exchange_type,
                (negotiation->ed->auth_method_type ==
                 SSH_IKE_AUTH_METHOD_ANY ? "any" :
                 (negotiation->ed->auth_method_type ==
                  SSH_IKE_AUTH_METHOD_PHASE_1 ? "phase1" :
                  (negotiation->ed->auth_method_type ==
                   SSH_IKE_AUTH_METHOD_PRE_SHARED_KEY ? "pre shared key" :
                   "unknown"))),
                this_end_is_initiator ? "Initiator" : "Responder"));
#endif /* SSHDIST_ISAKMP_AUTH_CERT */

  /* Find matching state */
  for (i = 0; i < ike_state_machine_size; i++)
    {
#ifdef DEBUG_LIGHT
      state_name = ssh_find_keyword_name(ssh_ike_state_name_keywords,
                                         ike_state_machine[i].state);
      if (state_name == NULL)
        state_name = "unknown";
      SSH_DEBUG(12, ("Matching with state = %s (%d), "
                     "xchg = %d, auth = %d, fields = %04x / %04x",
                     state_name, ike_state_machine[i].state,
                     ike_state_machine[i].xchg_type,
                     ike_state_machine[i].auth_method,
                     ike_state_machine[i].mandatory_input_fields,
                     ike_state_machine[i].optional_input_fields));
#endif /* DEBUG_LIGHT */

      /* Does this state match? */
      if ((ike_state_machine[i].state == negotiation->ed->current_state
           || ike_state_machine[i].state == SSH_IKE_ST_ANY)
          && (ike_state_machine[i].xchg_type == SSH_IKE_XCHG_TYPE_ANY
              || (ike_state_machine[i].xchg_type
                  == negotiation->exchange_type))
          && (ike_state_machine[i].auth_method == SSH_IKE_AUTH_METHOD_ANY
              || (ike_state_machine[i].auth_method
                  == SSH_IKE_AUTH_METHOD_PHASE_1
                  && isakmp_sa->phase_1_done)
              || ike_state_machine[i].auth_method
              == negotiation->ed->auth_method_type)
          && (ike_state_machine[i].mandatory_input_fields
              == SSH_IKE_FIELDS_ANY
              || ((ike_state_machine[i].mandatory_input_fields & fields)
                  == ike_state_machine[i].mandatory_input_fields))
          && (ike_state_machine[i].optional_input_fields
              == SSH_IKE_FIELDS_ANY
              || ((fields & ~(ike_state_machine[i].mandatory_input_fields |
                              ike_state_machine[i].optional_input_fields))
                  == 0)))
        {
          /* Match found */
          SshIkeStateMachine state;
          SshIkeNotifyMessageType ret = 0;
          int j;

          SSH_DEBUG(9, ("Matched state = %s (%d)", state_name,
                        ike_state_machine[i].state));

          state = &(ike_state_machine[i]);

          if (negotiation->ed->current_state_function <= 0xff)
            {
              if (negotiation->ed->current_state_function == -1)
                negotiation->ed->current_state_function = 0;
              /* Call input functions */
              for (j = negotiation->ed->current_state_function;
                  state->in_funcs[j] != NULL_FNPTR;
                  j++)
                {
#ifdef DEBUG_LIGHT
                  func_name =
                    ssh_find_keyword_name(ssh_ike_state_input_funcs_keywords,
                                          (unsigned long) state->in_funcs[j]);
                  if (func_name == NULL)
                    func_name = "unknown";

                  SSH_DEBUG(9, ("Calling input function[%d] = %s", j,
                                func_name));
#endif /* DEBUG_LIGHT */

                  ret = (*(state->in_funcs[j]))(isakmp_context,
                                                isakmp_packet_in,
                                                isakmp_sa,
                                                negotiation,
                                                state);
                  /* Check if we want to retry this operation later */
                  if (ret == SSH_IKE_NOTIFY_MESSAGE_RETRY_LATER)
                    {
                      negotiation->lock_flags |=
                        SSH_IKE_NEG_LOCK_FLAG_WAITING_PM_REPLY;
                      negotiation->ed->current_state_function = j;
                      SSH_DEBUG(9,
                                ("Input function[%d] = %s asked retry later",
                                 j, func_name));
                      return 0;
                    }
                  else if (ret == SSH_IKE_NOTIFY_MESSAGE_RETRY_NOW)
                    rerun = TRUE;
                  else if (ret != 0)
                    {
                      SSH_DEBUG(9, ("Input function[%d] = %s failed", j,
                                    func_name));
                      return ret;
                    }
                }
              if (rerun)
                {
                  rerun = FALSE;
                  negotiation->ed->current_state_function = -1;
                  goto restart;
                }
              negotiation->ed->current_state_function = 0xff;
            }

          /* Initialize output packet */
          if (negotiation->ed->current_state_function == 0xff &&
              isakmp_packet_out != NULL)
            {
              *isakmp_packet_out = ssh_calloc(1,
                                              sizeof(struct SshIkePacketRec));
              if (*isakmp_packet_out == NULL)
                return SSH_IKE_NOTIFY_MESSAGE_OUT_OF_MEMORY;
              memmove((*isakmp_packet_out)->cookies.initiator_cookie,
                      isakmp_sa->cookies.initiator_cookie,
                      SSH_IKE_COOKIE_LENGTH);
              memmove((*isakmp_packet_out)->cookies.responder_cookie,
                      isakmp_sa->cookies.responder_cookie,
                      SSH_IKE_COOKIE_LENGTH);
              (*isakmp_packet_out)->major_version =
                o_neg->ike_pm_info->major_version;
              (*isakmp_packet_out)->minor_version =
                o_neg->ike_pm_info->minor_version;
              (*isakmp_packet_out)->exchange_type = negotiation->exchange_type;
              (*isakmp_packet_out)->message_id = negotiation->ed->message_id;
              (*isakmp_packet_out)->payloads =
                ssh_calloc(SSH_IKE_OPERATIONS_MAX, sizeof(SshIkePayload));
              if ((*isakmp_packet_out)->payloads == NULL)
		return SSH_IKE_NOTIFY_MESSAGE_OUT_OF_MEMORY;
              (*isakmp_packet_out)->number_of_payload_packets_allocated =
                SSH_IKE_OPERATIONS_MAX;

              (*isakmp_packet_out)->packet_data_items_alloc = 16;
              (*isakmp_packet_out)->packet_data_items =
                ssh_calloc((*isakmp_packet_out)->packet_data_items_alloc,
                           sizeof(unsigned char *));
              if ((*isakmp_packet_out)->packet_data_items == NULL)
		{
		  (*isakmp_packet_out)->packet_data_items_alloc = 0;
		  return SSH_IKE_NOTIFY_MESSAGE_OUT_OF_MEMORY;
		}
            }
          if (isakmp_packet_out != NULL)
            negotiation->ed->isakmp_packet_out = *isakmp_packet_out;
          if (negotiation->ed->current_state_function == 0xff)
            negotiation->ed->current_state_function = 0x100;

          /* Call output functions */
          for (j = negotiation->ed->current_state_function - 0x100;
              state->out_funcs[j] != NULL_FNPTR;
              j++)
            {
#ifdef DEBUG_LIGHT
              func_name =
                ssh_find_keyword_name(ssh_ike_state_output_funcs_keywords,
                                      (unsigned long) state->out_funcs[j]);
              if (func_name == NULL)
                func_name = "unknown";

              SSH_DEBUG(9, ("Calling output function[%d] = %s", j,
                            func_name));
#endif /* DEBUG_LIGHT */

              ret = (*(state->out_funcs[j]))(isakmp_context, isakmp_packet_in,
                                             (isakmp_packet_out != NULL ?
                                              *isakmp_packet_out : NULL),
                                             isakmp_sa,
                                             negotiation, state);
              /* Check if we want to retry this operation later */
              if (ret == SSH_IKE_NOTIFY_MESSAGE_RETRY_LATER)
                {
                  negotiation->lock_flags |=
                    SSH_IKE_NEG_LOCK_FLAG_WAITING_PM_REPLY;
                  negotiation->ed->current_state_function = j + 0x100;
                  *isakmp_packet_out = NULL;
                  SSH_DEBUG(9, ("Output function[%d] = %s asked retry later",
                                j, func_name));
                  return 0;
                }
              else if (ret == SSH_IKE_NOTIFY_MESSAGE_RETRY_NOW)
                rerun = TRUE;
              else if (ret != 0 && ret != SSH_IKE_NOTIFY_MESSAGE_CONNECTED)
                {
                  SSH_DEBUG(9, ("Output function[%d] = %s failed", j,
                                func_name));
                  if (isakmp_packet_out != NULL)
                    {
                      ike_free_packet(*isakmp_packet_out,
                                      negotiation->ed->compat_flags);
                      *isakmp_packet_out = NULL;
                      negotiation->ed->isakmp_packet_out = NULL;
                    }
                  return ret;
                }
            }
          if (isakmp_packet_out != NULL)
            {
              if ((*isakmp_packet_out)->number_of_payload_packets == 0)
                {
                  ike_free_packet(*isakmp_packet_out,
                                  negotiation->ed->compat_flags);
                  *isakmp_packet_out = NULL;
                }
              else
                {
                  negotiation->ed->packets_out[negotiation->ed->
                                              number_of_packets_out++] =
                    *isakmp_packet_out;
                }
              negotiation->ed->isakmp_packet_out = NULL;
            }

          negotiation->ed->current_state = state->next_state;
          negotiation->ed->current_state_function = -1;
#ifdef DEBUG_LIGHT
          state_name = ssh_find_keyword_name(ssh_ike_state_name_keywords,
                                             negotiation->ed->current_state);
          if (state_name == NULL)
            state_name = "unknown";

          SSH_DEBUG(7, ("All done, new state = %s (%d)",
                        state_name,
                        negotiation->ed->current_state));
#endif /* DEBUG_LIGHT */

          if (rerun)
            {
              SSH_DEBUG(7, ("Rerun asked"));
              rerun = FALSE;
              if (isakmp_packet_out != NULL)
                {
                  SSH_DEBUG(7, ("We have output packet, "
                                "freeing previous packet, rerunning packet"));
                  if (*isakmp_packet_out != NULL)
                    ike_free_packet(*isakmp_packet_out,
                                    negotiation->ed->compat_flags);
                  goto restart;
                }
            }
          return ret;
        }
    }
  SSH_DEBUG(7, ("No state matched, returning error"));
  return SSH_IKE_NOTIFY_MESSAGE_NO_STATE_MATCHED;
}

#ifdef SUNWIPSEC

SshIkeNotifyMessageType
ike_calc_natd_hash(SshIkeSA isakmp_sa,
				   SshIkeNegotiation negotiation,
				   unsigned int ip_addr,
				   unsigned short port,
				   unsigned char *buf,
				   unsigned int *len)
{
    SshCryptoStatus cret;
    SshHash hash;
    
    cret = ssh_hash_allocate((char *)isakmp_sa->hash_algorithm_name, &hash);
    if (cret != SSH_CRYPTO_OK){
	SSH_IKE_DEBUG(3, negotiation,
		      ("ssh_hash_allocate failed: %.200s",
		       ssh_crypto_status_message(cret)));
	return SSH_IKE_NOTIFY_MESSAGE_AUTHENTICATION_FAILED;
    }

    *len = ssh_hash_digest_length((char *)isakmp_sa->hash_algorithm_name);
    
    memset(buf, 0, *len);
    
    ssh_hash_reset(hash);
    ssh_hash_update(hash,
		    isakmp_sa->cookies.initiator_cookie,
		    SSH_IKE_COOKIE_LENGTH);
    ssh_hash_update(hash,
		    isakmp_sa->cookies.responder_cookie,
		    SSH_IKE_COOKIE_LENGTH);
    ssh_hash_update(hash, (uint8_t *)&ip_addr, 4);
    ssh_hash_update(hash, (uint8_t *)&port, 2);
    
    ssh_hash_final(hash, buf);
    ssh_hash_free(hash);

    return 0;
}
#endif
