/*
 *
 * Author: Tero Kivinen <kivinen@iki.fi>
 *
 *  Copyright:
 *          Copyright (c) 2002, 2003 SFNT Finland Oy.
 */
/*
 *        Program: sshisakmp
 *        $Source: /project/cvs/src/ipsec/lib/sshisakmp/isakmp_udp.c,v $
 *        $Author: irwin $
 *
 *        Creation          : 21:52 Jun  1 1998 kivinen
 *        Last Modification : 15:58 Jan 20 2003 kivinen
 *        Last check in     : $Date: 2003/11/28 20:00:48 $
 *        Revision number   : $Revision: 1.2.2.3 $
 *        State             : $State: Exp $
 *        Version           : 1.1052
 *        
 *
 *        Description       : Isakmp network code
 *
 *
 *        $Log: isakmp_udp.c,v $ *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
 *        
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
#include "isakmp_util.h"
#include "isakmp_internal.h"
#include "isakmp_notify.h"
#include "sshdebug.h"
#include "sshtimeouts.h"

#define SSH_DEBUG_MODULE "SshIkeNet"

#ifdef SUNWIPSEC
void ike_process_packet(SshIkeServerContext server,
                        SshIkeSA sa,
                        SshIkeNegotiation negotiation,
			uchar_t *remote_port,
                        SshBuffer buffer);
#else
void ike_process_packet(SshIkeServerContext server,
                        SshIkeSA sa,
                        SshIkeNegotiation negotiation,
                        SshBuffer buffer);
#endif

/*                                                              shade{0.9}
 * Call done callback of the policy manager, and
 * send notification to caller if such callback is
 * registered.                                                  shade{1.0}
 */
void ike_call_callbacks(SshIkeNegotiation negotiation,
                        SshIkeNotifyMessageType ret)
{
  if (negotiation->notification_state !=
      SSH_IKE_NOTIFICATION_STATE_ALREADY_SENT)
    {
      negotiation->notification_state =
        SSH_IKE_NOTIFICATION_STATE_ALREADY_SENT;
      switch (negotiation->exchange_type)
        {
        case SSH_IKE_XCHG_TYPE_NONE:
        case SSH_IKE_XCHG_TYPE_ANY:
        case SSH_IKE_XCHG_TYPE_BASE:
        case SSH_IKE_XCHG_TYPE_AO:
          ssh_fatal("Invalid exchange type in ike_call_callbacks");
          break;
        case SSH_IKE_XCHG_TYPE_IP:
        case SSH_IKE_XCHG_TYPE_AGGR:
          ssh_policy_negotiation_done_isakmp(negotiation->ike_pm_info, ret);
          if (ret != SSH_IKE_NOTIFY_MESSAGE_CONNECTED && ret != 0)
            {
              if (negotiation->ike_pm_info->this_end_is_initiator)
                {
                  negotiation->sa->server_context->
                    statistics.total_init_failures++;
                  if (ret == SSH_IKE_NOTIFY_MESSAGE_TIMEOUT &&
                      negotiation->ed->number_of_packets_in == 0)
                    negotiation->sa->server_context->
                      statistics.total_init_no_response++;
                }
              else
                negotiation->sa->server_context->statistics.
                  total_resp_failures++;
            }

          break;
        case SSH_IKE_XCHG_TYPE_QM:
          ssh_policy_negotiation_done_qm(negotiation->qm_pm_info, ret);
          break;
        case SSH_IKE_XCHG_TYPE_NGM:
          ssh_policy_negotiation_done_phase_ii(negotiation->ngm_pm_info, ret);
          break;
#ifdef SSHDIST_ISAKMP_CFG_MODE
        case SSH_IKE_XCHG_TYPE_CFG:
          ssh_policy_negotiation_done_phase_ii(negotiation->cfg_pm_info, ret);
          break;
#endif /* SSHDIST_ISAKMP_CFG_MODE */
        case SSH_IKE_XCHG_TYPE_INFO:
          ssh_policy_negotiation_done_phase_ii(negotiation->info_pm_info, ret);
          break;
        }
      if (negotiation->ed->notify_callback != NULL_FNPTR)
        (*negotiation->ed->notify_callback)(ret, negotiation,
                                            negotiation->ed->
                                            notify_callback_context);
    }
}

/*                                                              shade{0.9}
 * Isakmp retransmit timer calculation. Returns
 * retry timer values for the next retransmission
 * packet.                                                      shade{1.0}
 */
void ike_retransmit_timer(SshIkeNegotiation negotiation,
                          SshUInt32 *seconds,
                          SshUInt32 *useconds)
{
  SshUInt32 multiplier;
  int shift_count;

  /* XXX we should calculate real round trip and use that as base of the
     retransmission timer. The problem is that processing of the packet
     in the other end might different quite a lot depending if Diffie-Hellman
     or RSA encryption is needed for that packet or not. */
  shift_count = negotiation->ed->retry_limit - negotiation->ed->retry_count;
  if (shift_count > 15 || negotiation->ed->retry_timer >= 32768)
    {
      *seconds = negotiation->ed->retry_timer_max;
      *useconds = negotiation->ed->retry_timer_max_usec;
      return;
    }

  multiplier = 1 << shift_count;
  /* Take care of the overflows. Because the maximum multiplier is 32768, and
     the maximum retry timer seconds is 32768, the multiplier * retry_timer
     fits in the 32 bit interger.

     The usec part must be smaller than 1 000 000, so if we divide that by 16,
     and the multiply 32768 we get at most 2 048 000 000, that still fits int
     the 32 bit value, and after that we convert it to seconds by divinding it
     by 62 500.

     We do not loose any precission, because 16 * 32768 is 524 288, that is
     still less than second. */
  *seconds = negotiation->ed->retry_timer * multiplier +
    ((negotiation->ed->retry_timer_usec / 16) * multiplier) / 62500;

  if (negotiation->ed->retry_timer_usec < 131072 || multiplier < 4096)
    *useconds = (negotiation->ed->retry_timer_usec * multiplier) % 1000000;
  else
    *useconds =
      (((negotiation->ed->retry_timer_usec / 16) * multiplier) % 62500) * 16;
  if (*seconds > negotiation->ed->retry_timer_max ||
      (*seconds == negotiation->ed->retry_timer_max &&
       *useconds > negotiation->ed->retry_timer_max_usec))
    {
      *seconds = negotiation->ed->retry_timer_max;
      *useconds = negotiation->ed->retry_timer_max_usec;
      return;
    }
  return;
}

/*                                                              shade{0.9}
 * Isakmp max lifetime calculation. Returns
 * max lifetime.                                                shade{1.0}
 */
void ike_negotiation_max_lifetime(SshIkeNegotiation negotiation,
                                  SshUInt32 *seconds,
                                  SshUInt32 *useconds)
{
  *seconds = negotiation->ed->retry_limit *
    negotiation->ed->retry_timer_max +
    negotiation->ed->retry_limit * negotiation->ed->retry_timer_max_usec
    / 1000000;
  *useconds = (negotiation->ed->retry_limit *
               negotiation->ed->retry_timer_max_usec) % 1000000;
  if (*seconds > negotiation->ed->expire_timer ||
      (*seconds == negotiation->ed->expire_timer &&
       *useconds > negotiation->ed->expire_timer_usec))
    {
      *seconds = negotiation->ed->expire_timer;
      *useconds = negotiation->ed->expire_timer_usec;
    }
}

/*                                                              shade{0.9}
 * Isakmp retransmit callback. Called from timer to
 * retransmit packet.                                           shade{1.0}
 */
void ike_retransmit_callback(void *context)
{
  SshIkeNegotiation negotiation = (SshIkeNegotiation) context;
  SshIkeSA sa = negotiation->sa;

  sa->last_use_time = ssh_time();
  ssh_cancel_timeouts(ike_retransmit_callback, negotiation);

  SSH_DEBUG(5, ("Start, retransmit SA = { %08x %08x - %08x %08x}, nego = %d",
                SSH_IKE_GET32(sa->cookies.initiator_cookie),
                SSH_IKE_GET32(sa->cookies.initiator_cookie + 4),
                SSH_IKE_GET32(sa->cookies.responder_cookie),
                SSH_IKE_GET32(sa->cookies.responder_cookie + 4),
                negotiation->negotiation_index));
  SSH_IKE_DEBUG(6, negotiation, ("Retransmitting packet, retries = %d",
                                 negotiation->ed->retry_count));

  if (negotiation->ed->retry_count-- == 0)
    {
      SSH_DEBUG(3, ("Isakmp query retry limit reached, deleting"));
      ssh_ike_audit(negotiation, SSH_AUDIT_IKE_RETRY_LIMIT_REACHED,
                    "ISAKMP negotiation retry limit reached");

      ike_send_notify(sa->server_context, negotiation,
                      SSH_IKE_NOTIFY_MESSAGE_TIMEOUT);
      return;
    }
  /* Send the packet. Even if this fails, it should reinsert the timeout, thus
     only this packet is lost, and after next retransmit time we try again.
     This means that we don't need to do anything special even when this
     fails. */
  ike_send_packet(negotiation,
                  negotiation->ed->last_sent_packet,
                  negotiation->ed->last_sent_packet_len, TRUE, FALSE);
}


/*                                                              shade{0.9}
 * Send isakmp packet. If retransmit is true then this is
 * retransmit and we should not reset retry_count or
 * last_sent_packet information.                                shade{1.0}
 */
SshIkeNotifyMessageType ike_send_packet(SshIkeNegotiation negotiation,
                                        const unsigned char *p,
                                        size_t len,
                                        Boolean retransmit,
                                        Boolean no_timers)
{
  const unsigned char *remote_ip, *remote_port;
  SshIkeSA sa = negotiation->sa;
  SshIkeServerContext context;
  SshUdpListener listener;
  const unsigned char *local_port;
  unsigned char *natt_p = NULL;
  size_t natt_len;
#ifdef SUNWIPSEC
  void *packet_context = negotiation->packet_context;
#endif /* SUNWIPSEC */

  if (!no_timers)
    ssh_cancel_timeouts(ike_retransmit_callback, negotiation);

  context = negotiation->sa->server_context;

  local_port = context->server_port;
  listener = NULL;
  if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_INFO)
    {
      remote_ip = negotiation->info_pm_info->remote_ip;
      remote_port = negotiation->info_pm_info->remote_port;
    }
  else if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_NGM)
    {
      remote_ip = negotiation->ngm_pm_info->remote_ip;
      remote_port = negotiation->ngm_pm_info->remote_port;
    }
  else if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_QM)
    {
      remote_ip = negotiation->qm_pm_info->remote_ip;
      remote_port = negotiation->qm_pm_info->remote_port;
    }
#ifdef SSHDIST_ISAKMP_CFG_MODE
  else if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_CFG)
    {
      remote_ip = negotiation->cfg_pm_info->remote_ip;
      remote_port = negotiation->cfg_pm_info->remote_port;
    }
#endif /* SSHDIST_ISAKMP_CFG_MODE */
  else
    {
      remote_ip = negotiation->ike_pm_info->remote_ip;
      remote_port = negotiation->ike_pm_info->remote_port;
      local_port = negotiation->ike_pm_info->local_port;
      listener = negotiation->ike_ed->listener;
    }

  if (!retransmit)
    {
      SSH_DEBUG(5, ("Start, send "
                    "SA = { %08x %08x - %08x %08x}, nego = %d, dst = %s:%s",
                    SSH_IKE_GET32(sa->cookies.initiator_cookie),
                    SSH_IKE_GET32(sa->cookies.initiator_cookie + 4),
                    SSH_IKE_GET32(sa->cookies.responder_cookie),
                    SSH_IKE_GET32(sa->cookies.responder_cookie + 4),
                    negotiation->negotiation_index, remote_ip, remote_port));

      negotiation->ed->retry_count = negotiation->ed->retry_limit;
      if (negotiation->ed->last_sent_packet != NULL)
        {
          if (negotiation->ed->last_sent_packet_len > len)
            {
              memcpy(negotiation->ed->last_sent_packet, p, len);
              negotiation->ed->last_sent_packet_len = len;
            }
          else
            {
              ssh_free(negotiation->ed->last_sent_packet);
              negotiation->ed->last_sent_packet = ssh_memdup(p, len);
              if (negotiation->ed->last_sent_packet == NULL)
                return SSH_IKE_NOTIFY_MESSAGE_OUT_OF_MEMORY;
              negotiation->ed->last_sent_packet_len = len;
            }
        }
      else
        {
          negotiation->ed->last_sent_packet = ssh_memdup(p, len);
          if (negotiation->ed->last_sent_packet == NULL)
            return SSH_IKE_NOTIFY_MESSAGE_OUT_OF_MEMORY;
          negotiation->ed->last_sent_packet_len = len;
        }

      sa->byte_count += len;

      if (sa->kbyte_limit != 0 && sa->byte_count / 1024 >= sa->kbyte_limit)
        {
          /* Life time expired, process this negotiation, but mark the
             negotiation to be deleted, so it will not be used anymore. */
          ssh_ike_remove_isakmp_sa(sa->isakmp_negotiation,
                                   SSH_IKE_REMOVE_FLAGS_SEND_DELETE);
          sa->kbyte_limit = 0;
        }
    }
  else
    {
      SSH_DEBUG(5, ("Start, retransmit previous packet "
                    "SA = { %08x %08x - %08x %08x}, nego = %d, dst = %s:%s",
                    SSH_IKE_GET32(sa->cookies.initiator_cookie),
                    SSH_IKE_GET32(sa->cookies.initiator_cookie + 4),
                    SSH_IKE_GET32(sa->cookies.responder_cookie),
                    SSH_IKE_GET32(sa->cookies.responder_cookie + 4),
                    negotiation->negotiation_index,
                    remote_ip, remote_port));
    }

  ssh_time_measure_reset(negotiation->ed->last_packet_time);
  SSH_IKE_DEBUG_BUFFER(13, negotiation, "Sending packet", len, p);

  /* Update statistics. This includes all retransmissions etc. */
  sa->statistics.packets_out++;
  sa->statistics.octects_out += len;
  context->statistics.total_packets_out++;
  context->statistics.total_octets_out += len;

#ifdef SUNWIPSEC
  if (sa->sa_natt_state >= NATT_STATE_PORT)
    {
      SSH_DEBUG(6, ("SENDING PACKET ON NATT PORT"));
      if (sa->sa_other_port == NULL)
	{
	  sa->sa_other_port =
	      ssh_xstrdup(context->isakmp_context->default_natt_port);
	  sa->sa_natt_state = NATT_STATE_GOT_PORT;
	}
      if (sa->listener_natt)
	{
	  SSH_DEBUG(7, ("sa listener %s", sa->sa_other_port));
	  ssh_udp_send(sa->listener_natt,
#ifdef SUNWIPSEC
		       packet_context,
#endif /* SUNWIPSEC */
		       NULL, (uchar_t *)sa->sa_other_port, p, len);
	}
      else
	{
	  SSH_DEBUG(7, ("ctx listener %s %s",
			remote_ip, sa->sa_other_port));
	  ssh_udp_send(context->listener_natt,
#ifdef SUNWIPSEC
		       packet_context,
#endif /* SUNWIPSEC */
		       remote_ip, (uchar_t *)sa->sa_other_port, p, len);
	}
    }
  else
    {
      SSH_DEBUG(6, ("SENDING PACKET ON REGULAR PORT"));
#else
  /* Check if the packet must be prepended with a non-ESP marker. */
  if (strcmp(ssh_csstr(local_port), "4500") == 0)
    {
      /* We need a non-ESP marker. */
      natt_len = len + 4;
      if (natt_len > 65535)
        /* Too long payload. */
        return SSH_IKE_NOTIFY_MESSAGE_OUT_OF_MEMORY;

      natt_p = ssh_malloc(natt_len);
      if (natt_p == NULL)
        return SSH_IKE_NOTIFY_MESSAGE_OUT_OF_MEMORY;

      memset(natt_p, 0, 4);
      memcpy(natt_p + 4, p, len);

      /* And send our encapsulated packet. */
      p = natt_p;
      len = natt_len;
    }
#endif

  if (listener)
    ssh_udp_send(listener,
#ifdef SUNWIPSEC
	         packet_context,
#endif /* SUNWIPSEC */
	         NULL, NULL, p, len);
  else
    ssh_udp_send(context->listener,
#ifdef SUNWIPSEC
	         packet_context,
#endif /* SUNWIPSEC */
		 remote_ip, remote_port, p, len);

#ifdef SUNWIPSEC
  }

  if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_IP)
      negotiation->ike_pm_info->p1_natt_state = sa->sa_natt_state; 
#else
  /* Free possible NAT-T prepared IKE packet copy. */
  if (natt_p)
    ssh_free(natt_p);
#endif

  if (!no_timers)
    {
      SshUInt32 sec, usec;
      ike_retransmit_timer(negotiation, &sec, &usec);
      SSH_DEBUG(8, ("Inserting retransmission timer after %d.%06d seconds",
                    sec, usec));
      ssh_xregister_timeout(sec, usec, ike_retransmit_callback, negotiation);
    }
  return 0;
}


/*                                                              shade{0.9}
 * ike_state_restart_packet will take last isakmp
 * packet received and feed it again to state machine.
 * If state machine step produces output packet that
 * packet is sent and retransmission timers are
 * initialized. If no packet is sent then this
 * just returns.                                                shade{1.0}
 */
void ike_state_restart_packet(void *context)
{
  SshIkeNegotiation negotiation = (SshIkeNegotiation) context;
  SshIkeSA sa = negotiation->sa;
  SshIkeNotifyMessageType ret;
  SshBuffer buffer;
  SshIkePacket isakmp_packet_out;

  SSH_IKE_DEBUG(6, negotiation, ("Restart packet"));

  negotiation->lock_flags &= ~(SSH_IKE_NEG_LOCK_FLAG_WAITING_PM_REPLY);

  /* Check if the negotiation is already deleted during the policy manager
     call? */
  if (negotiation->ed->current_state == SSH_IKE_ST_DELETED)
    {
      /* Yes, do the final delete and return */
      ike_delete_negotiation(negotiation);
      return;
    }

  SSH_DEBUG(5,
            ("Start, restart packet SA = { %08x %08x - %08x %08x}, nego = %d",
             SSH_IKE_GET32(sa->cookies.initiator_cookie),
             SSH_IKE_GET32(sa->cookies.initiator_cookie + 4),
             SSH_IKE_GET32(sa->cookies.responder_cookie),
             SSH_IKE_GET32(sa->cookies.responder_cookie + 4),
             negotiation->negotiation_index));

  if (negotiation->notification_state ==
      SSH_IKE_NOTIFICATION_STATE_SEND_NOW)
    {
      ike_send_notify(sa->server_context, negotiation,
                      negotiation->ed->code);
      return;
    }

  if (negotiation->ed->number_of_packets_in == 0 &&
      negotiation->ed->current_state_function == -1)
    {
      /* This is first phase 2 packet that was doing asyncronous DH. call
         ike_process_packet to reprocess the packet */
      SshBuffer buffer;

      buffer = ssh_buffer_allocate();
      if (buffer == NULL)
        goto phase_2_error;
      if (!ssh_buffer_append(buffer, negotiation->ed->last_recv_packet,
                             negotiation->ed->last_recv_packet_len) !=
          SSH_BUFFER_OK)
        goto phase_2_error;
#ifdef SUNWIPSEC
      ike_process_packet(sa->server_context, sa, negotiation, NULL, buffer);
#else
      ike_process_packet(sa->server_context, sa, negotiation, buffer);
#endif
      ssh_buffer_free(buffer);
      return;
    phase_2_error:
      if (buffer)
        ssh_buffer_free(buffer);
      /* Kill the negotiation, because of error */
      ike_send_notify(sa->server_context, negotiation,
                      SSH_IKE_NOTIFY_MESSAGE_OUT_OF_MEMORY);
      return;
    }
  isakmp_packet_out = NULL;

  /* Advance state machine */
  if (negotiation->lock_flags & SSH_IKE_NEG_LOCK_FLAG_WAITING_FOR_DONE)
    {
      ret = ike_state_step(sa->server_context->isakmp_context, NULL,
                           NULL, sa, negotiation);
    }
  else
    {
      ret = ike_state_step(sa->server_context->isakmp_context, NULL,
                           &isakmp_packet_out, sa, negotiation);
    }
  if (ret == SSH_IKE_NOTIFY_MESSAGE_CONNECTED)
    {
      SSH_DEBUG(7, ("Connected, sending notify"));
      ike_send_notify(sa->server_context, negotiation, ret);
      return;
    }
  if (ret != 0)
    goto error;

  /* Check if we have output packet */
  if (isakmp_packet_out == NULL)
    {
      /* Clear the retransmission buffer */
      negotiation->ed->last_sent_packet_len = 0;
      if (negotiation->notification_state ==
          SSH_IKE_NOTIFICATION_STATE_SEND_NOW)
        {
          ike_send_notify(sa->server_context, negotiation,
                          negotiation->ed->code);
        }

      /* No output packet, return */
      SSH_DEBUG(7, ("No output packet, returning"));
      return;
    }

  buffer = ssh_buffer_allocate();
  if (buffer == NULL)
    goto error;

  /* Encode response packet */
  ret = ike_encode_packet(sa->server_context->isakmp_context,
                          isakmp_packet_out,
                          sa, negotiation, buffer);
  if (ret != 0)
    {
      ssh_buffer_free(buffer);
      goto error;
    }

  /* Send response packet */
  ret = ike_send_packet(negotiation,
                        ssh_buffer_ptr(buffer),
                        ssh_buffer_len(buffer), FALSE, FALSE);
  if (ret != 0)
    {
      ssh_buffer_free(buffer);
      goto error;
    }

  if (negotiation->notification_state == SSH_IKE_NOTIFICATION_STATE_SEND_NOW)
    {
      ike_send_notify(sa->server_context, negotiation, negotiation->ed->code);
    }

  ssh_buffer_free(buffer);
  return;
error:
  SSH_DEBUG(7, ("Error, send notify"));
  ike_send_notify(sa->server_context, negotiation, ret);
  return;
}


#ifdef SUNWIPSEC
static
SshIkePMPhaseI phase_i_pminfo(SshIkeNegotiation negotiation)
{
	switch (negotiation->exchange_type) {
	case SSH_IKE_XCHG_TYPE_IP:
		return (negotiation->ike_pm_info);
	case SSH_IKE_XCHG_TYPE_INFO:
		return (negotiation->info_pm_info == NULL ? NULL :
		    negotiation->info_pm_info->phase_i);
	case SSH_IKE_XCHG_TYPE_QM:
		return (negotiation->qm_pm_info == NULL ? NULL :
		    negotiation->qm_pm_info->phase_i);
	}
	return (NULL);
}
#endif
/*                                                              shade{0.9}
 * Isakmp udp-packet handler. Called from udp_callback
 * to process packet after sa is found and initialized.         shade{1.0}
 */
#ifdef SUNWIPSEC
void ike_process_packet(SshIkeServerContext server,
                        SshIkeSA sa,
                        SshIkeNegotiation negotiation,
			uchar_t *remote_port,
                        SshBuffer buffer)
#else
void ike_process_packet(SshIkeServerContext server,
                        SshIkeSA sa,
                        SshIkeNegotiation negotiation,
                        SshBuffer buffer)
#endif
{
  unsigned char *p;
  size_t len;
  SshIkePacket isakmp_packet_in, isakmp_packet_out;
  SshIkeNotifyMessageType ret;
  Boolean decrypted = FALSE, rx = TRUE;
  SshIkePMPhaseI pm_info;

  p = ssh_buffer_ptr(buffer);
  len = ssh_buffer_len(buffer);

  SSH_IKE_DEBUG_BUFFER(13, negotiation, "Received packet", len, p);

  if (negotiation->lock_flags & SSH_IKE_NEG_LOCK_FLAG_WAITING_PM_REPLY)
    return;

  /* Check if this is first packet, and if so make sure we have keying material
     ready if we need it */
  if (negotiation->ed->last_recv_packet == NULL &&
      sa->phase_1_done &&
      (SSH_IKE_GET8(p + 19) & SSH_IKE_FLAGS_ENCRYPTION))
    {
      ret = ike_calc_skeyid(server->isakmp_context, sa, negotiation);
      if (ret == SSH_IKE_NOTIFY_MESSAGE_RETRY_LATER)
        {
          /* Asyncronous operation, store packet to buffer */
          negotiation->ed->last_recv_packet = ssh_memdup(p, len);
          if (negotiation->ed->last_recv_packet == NULL)
            {
              /* Out of memory, remove the whole negotiation */
              ike_send_notify(server, negotiation,
                              SSH_IKE_NOTIFY_MESSAGE_OUT_OF_MEMORY);
              return;
            }
          negotiation->ed->last_recv_packet_len = len;
          ssh_buffer_clear(buffer);

          /* Cancel retransmit timeouts for this sa */
          ssh_cancel_timeouts(ike_retransmit_callback, negotiation);

          /* Clear the retransmission buffer */
          negotiation->ed->last_sent_packet_len = 0;
          /* Return and the asyncronous operation will then call the
             ike_state_restart_packet function which will detect that this is
             is first input packet which hasn't been even decoded yet, thus it
             will call this function again. */
          return;
        }
    }

  /* Decode packet */
  ret = ike_decode_packet(server->isakmp_context, &isakmp_packet_in,
                          sa, negotiation, buffer);
  if (ret != 0) {
    decrypted = (isakmp_packet_in != NULL);
    goto error;
  }
  decrypted = TRUE;

#ifdef SUNWIPSEC
  /*
   * At this point, we know it's a decrypted packet, and we can apply
   * Section 7 from RFC 3947, where we can rewrite the remote-port in the case
   * where we're a non-NAT-ted packet.
   */
  if (sa->sa_natt_state >= NATT_STATE_PORT && sa->sa_other_port != NULL &&
      (isakmp_packet_in->flags & SSH_IKE_FLAGS_ENCRYPTION) &&
      remote_port != NULL &&
      strcmp((char *)remote_port, (char *)sa->sa_other_port) != 0) {
	  SshIkePMPhaseI pm_info = phase_i_pminfo(negotiation);

	  ssh_policy_sun_info("Possible RFC 3947 Section 7 port-jump "
	      "from %s to %s", (char *)sa->sa_other_port, (char *)remote_port);
	  if (pm_info != NULL && !pm_info->behind_nat) {
		  ssh_xfree(sa->sa_other_port);
		  sa->sa_other_port = ssh_xstrdup(remote_port);
		  ssh_policy_sun_info("Jumped ports.\n");
	  } else {
		  ssh_policy_sun_info("Didn't jump:  %s",
		      (pm_info == NULL) ? "can't find internal state" :
		      "Local host behind a NAT, not allowed.");
	  }
  }
#endif

  if (negotiation->ed->last_recv_packet == NULL)
    {
      negotiation->ed->last_recv_packet = ssh_memdup(p, len);
      if (negotiation->ed->last_recv_packet == NULL)
        {
          /* Out of memory, simply ignore this packet, and try again with next
             retransmission packet. */
          ike_free_packet(isakmp_packet_in,
                          negotiation->ed->compat_flags);
          return;
        }
      negotiation->ed->last_recv_packet_len = len;
    }
  else
    {
      /* Store received packet as last packet received */
      if (negotiation->ed->last_recv_packet_len > len)
        {
          memcpy(negotiation->ed->last_recv_packet, p, len);
          negotiation->ed->last_recv_packet_len = len;
        }
      else
        {
          ssh_free(negotiation->ed->last_recv_packet);
          negotiation->ed->last_recv_packet = ssh_memdup(p, len);
          if (negotiation->ed->last_recv_packet == NULL)
            {
              /* Out of memory, simply ignore this packet, and try again with
                 next retransmission packet. */
              ike_free_packet(isakmp_packet_in,
                              negotiation->ed->compat_flags);
              return;
            }
          negotiation->ed->last_recv_packet_len = len;
        }
    }

  /* Advance state machine */
  ret = ike_state_step(server->isakmp_context, isakmp_packet_in,
                       &isakmp_packet_out, sa, negotiation);
  if (ret == SSH_IKE_NOTIFY_MESSAGE_CONNECTED)
    {
      SSH_DEBUG(7, ("Connected, sending notify"));
      ike_send_notify(server, negotiation, ret);
      return;
    }
  if (ret != 0)
    goto error;

  /* Cancel retransmit timeouts for this sa */
  ssh_cancel_timeouts(ike_retransmit_callback, negotiation);

  /* Check if we have output packet */
  if (isakmp_packet_out == NULL)
    {
      /* Clear the retransmission buffer */
      negotiation->ed->last_sent_packet_len = 0;
      if (negotiation->notification_state ==
          SSH_IKE_NOTIFICATION_STATE_SEND_NOW)
        {
          ike_send_notify(server, negotiation, negotiation->ed->code);
        }

      /* No output packet, return */
      SSH_DEBUG(7, ("No output packet, returning"));
      return;
    }

  ssh_buffer_clear(buffer);

  /* 
   * For error reporting, if necessary.  We're done processing the received
   * packet, so any errors that occur now are relatec to transmit.
   */
  rx = FALSE;

  /* Encode response packet */
  ret = ike_encode_packet(server->isakmp_context, isakmp_packet_out,
                          sa, negotiation, buffer);
  if (ret != 0)
    goto error;

  /* Send response packet */
  ret = ike_send_packet(negotiation, ssh_buffer_ptr(buffer),
                        ssh_buffer_len(buffer), FALSE, FALSE);
  if (ret != 0)
    goto error;

  if (negotiation->notification_state == SSH_IKE_NOTIFICATION_STATE_SEND_NOW)
    {
      ike_send_notify(sa->server_context, negotiation, negotiation->ed->code);
    }

  return;
error:
  /*
   * Only pass in the pm_info struct if we're doing a phase 1 (main mode)
   * negotiation... otherwisse, the null poinrter will cause the error to
   * be noted, but the handler won't attempt to update phase 1 stats.
   */
  pm_info = (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_IP) ?
      negotiation->ike_pm_info : NULL;
  ike_report_error(pm_info, ret, decrypted, rx);
  SSH_DEBUG(7, ("Error, send notify"));
  ike_send_notify(server, negotiation, ret);
  return;
}

/*                                                              shade{0.9}
 * New connection callback done. This is called
 * when ssh_policy_new_connection function is done.             shade{1.0}
 */
void ike_new_connection_cb_done(SshIkeNewConnectionCBContext ctx)
{
  SshIkeNegotiation negotiation = ctx->negotiation;
  SshIkeSA sa = negotiation->sa;
  SshIkeServerContext server = ctx->server;
  SshUInt32 message_id = ctx->message_id;
  SshBuffer buffer = ctx->buffer;


  if (negotiation->lock_flags & SSH_IKE_NEG_LOCK_FLAG_WAITING_FOR_REMOVE)
    {
      /* Negotiation wasn't allowed, delete it immediately */
      /* Clear all lock_flags. */
      negotiation->lock_flags &=
        ~(SSH_IKE_NEG_LOCK_FLAG_PROCESSING_PM_QUERY |
          SSH_IKE_NEG_LOCK_FLAG_WAITING_PM_REPLY);
      if (negotiation->negotiation_index == -1 &&
          (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_INFO
#ifdef SSHDIST_ISAKMP_CFG_MODE
           || negotiation->exchange_type == SSH_IKE_XCHG_TYPE_CFG
#endif /* SSHDIST_ISAKMP_CFG_MODE */
           ))
        {
          negotiation->exchange_type = SSH_IKE_XCHG_TYPE_IP;
        }
      ike_delete_negotiation(ctx->negotiation);
      ssh_buffer_free(buffer);
      ssh_free(ctx);
      return;
    }

  /* Set isakmp expire timer */
  ssh_cancel_timeouts(SSH_ALL_CALLBACKS, negotiation);
  ssh_xregister_timeout(negotiation->ed->expire_timer,
                       negotiation->ed->expire_timer_usec,
                       ike_remove_callback,
                       negotiation);
  if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_INFO &&
      negotiation->negotiation_index == -1)
    {
      negotiation->exchange_type = SSH_IKE_XCHG_TYPE_IP;
      negotiation = ike_alloc_negotiation(sa);
      SSH_DEBUG(8, ("New informational mode negotiation message_id "
                    "= %08x initialized, using slot %d",
                    message_id, negotiation->negotiation_index));
      if (!ike_init_info_negotiation(negotiation,
                                     sa->isakmp_negotiation->ike_pm_info,
                                     server->server_name, server->server_port,
                                     sa->isakmp_negotiation->ike_pm_info->
                                     remote_ip,
                                     sa->isakmp_negotiation->ike_pm_info->
                                     remote_port,
                                     sa->isakmp_negotiation->ike_pm_info->
                                     major_version,
                                     sa->isakmp_negotiation->ike_pm_info->
                                     minor_version,
                                     sa->isakmp_negotiation->ike_pm_info->
                                     this_end_is_initiator,
                                     message_id))
        {
          ike_delete_negotiation(negotiation);
          ike_delete_negotiation(ctx->negotiation);
          ssh_buffer_free(buffer);
          ssh_free(ctx);
          return;
        }

      /* Mark it so that we never send any notifications for this */
      negotiation-> notification_state =
        SSH_IKE_NOTIFICATION_STATE_ALREADY_SENT;
    }
#ifdef SSHDIST_ISAKMP_CFG_MODE
  else if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_CFG &&
           negotiation->negotiation_index == -1)
    {
      negotiation->exchange_type = SSH_IKE_XCHG_TYPE_IP;
      negotiation = ike_alloc_negotiation(sa);
      SSH_DEBUG(8, ("New configuration mode negotiation message_id = "
                    "%08x initialized, using slot %d",
                    message_id, negotiation->negotiation_index));
      if (!ike_init_cfg_negotiation(negotiation,
                                    sa->isakmp_negotiation->ike_pm_info,
                                    server->server_name,
                                    server->server_port,
                                    sa->isakmp_negotiation->ike_pm_info->
                                    remote_ip,
                                    sa->isakmp_negotiation->ike_pm_info->
                                    remote_port,
                                    sa->isakmp_negotiation->ike_pm_info->
                                    major_version,
                                    sa->isakmp_negotiation->ike_pm_info->
                                    minor_version,
                                    SSH_IKE_XCHG_TYPE_CFG,
                                    sa->isakmp_negotiation->ike_pm_info->
                                    this_end_is_initiator,
                                    message_id,
                                    (ctx->negotiation->ed->compat_flags &
                                     SSH_IKE_FLAGS_USE_EXTENDED_TIMERS) != 0))
        {
          ike_delete_negotiation(negotiation);
          ike_delete_negotiation(ctx->negotiation);
          ssh_buffer_free(buffer);
          ssh_free(ctx);
          return;
        }
    }
#endif /* SSHDIST_ISAKMP_CFG_MODE */

  /* Check if the SshIkeServerContext is changed. */
  if (negotiation->sa->server_context != server)
    {
      /* Yes, give notification. */
      switch (negotiation->exchange_type)
        {
        case SSH_IKE_XCHG_TYPE_INFO:
          ssh_policy_phase_ii_server_changed(negotiation->info_pm_info,
                                             server,
                                             negotiation->info_pm_info->
                                             remote_ip,
                                             negotiation->info_pm_info->
                                             remote_port);
          break;
        case SSH_IKE_XCHG_TYPE_NGM:
          ssh_policy_phase_ii_server_changed(negotiation->ngm_pm_info,
                                             server,
                                             negotiation->ngm_pm_info->
                                             remote_ip,
                                             negotiation->ngm_pm_info->
                                             remote_port);
          break;
#ifdef SSHDIST_ISAKMP_CFG_MODE
        case SSH_IKE_XCHG_TYPE_CFG:
          ssh_policy_phase_ii_server_changed(negotiation->cfg_pm_info,
                                             server,
                                             negotiation->cfg_pm_info->
                                             remote_ip,
                                             negotiation->cfg_pm_info->
                                             remote_port);
          break;
#endif /* SSHDIST_ISAKMP_CFG_MODE */
        case SSH_IKE_XCHG_TYPE_QM:
          ssh_policy_phase_qm_server_changed(negotiation->qm_pm_info,
                                             server,
                                             negotiation->qm_pm_info->
                                             remote_ip,
                                             negotiation->qm_pm_info->
                                             remote_port);
          break;
        case SSH_IKE_XCHG_TYPE_IP:
        case SSH_IKE_XCHG_TYPE_AGGR:
          /* This means that the new_connection_cb changed the server, no
             need to send notification. */
          break;
        default:
          ssh_fatal("Internal error: Unknown exchange type in the "
                    "negotiation->exchange_type.");
          break;
        }
      /* Make sure that our local copy of the SshIkeServerContext matches
         the current one in the negotiation (in case the notification
         changed it. */
      server = negotiation->sa->server_context;
    }
#ifdef SUNWIPSEC
  ike_process_packet(server, sa, negotiation, NULL, buffer);
#else
  ike_process_packet(server, sa, negotiation, buffer);
#endif
  ssh_buffer_free(buffer);
  ssh_free(ctx);
}


/*                                                              shade{0.9}
 * Isakmp udp-packet handler. Called from udp-listerer
 * when packet is received from that socket.                    shade{1.0}
 */
void ike_udp_callback(SshUdpListener listener,
                      void *context)
{
  SshIkeServerContext server = (SshIkeServerContext) context;
  SshIkeContext isakmp_context = server->isakmp_context;
  unsigned char remote_address[SSH_IKE_IP_ADDR_STR_LEN];
  unsigned char remote_port[SSH_IKE_IP_PORT_STR_LEN];
  SshBuffer buffer;
  SshUdpError error;
  size_t len;
#ifdef SUNWIPSEC
  SshUdpPacketContext packet_context = NULL;
#endif /* SUNWIPSEC */

  SSH_DEBUG(5, ("Packet ready in source %s:%s",
                server->server_name, server->server_port));

  /* We cannot receive errors here, or if we receive them then we
     cannot associate them to negotiation, so we ignore them */
  error = ssh_udp_read(listener,
#ifdef SUNWIPSEC
		       &packet_context,
#endif /* SUNWIPSEC */
		       remote_address, sizeof(remote_address),
                       remote_port, sizeof(remote_port),
                       isakmp_context->udp_input_packet,
                       sizeof(isakmp_context->udp_input_packet),
                       &len);

  if (error != SSH_UDP_OK)
    {
      SSH_DEBUG(3, ("ike_udp_callback returned error: len %d", len));
      return;
    }

  buffer = ssh_buffer_allocate();
  if (buffer == NULL)
    goto skip_packet;

  /* Adjust buffer */
  if (ssh_buffer_append(buffer, isakmp_context->udp_input_packet, len)
      != SSH_BUFFER_OK)
    goto skip_packet;

  /* Call common callback code */
  ike_udp_callback_common(server,
#ifdef SUNWIPSEC
			  packet_context,
#endif /* SUNWIPSEC */
			  remote_address, remote_port, buffer);
  return;

 skip_packet:
  if (buffer)
    ssh_buffer_free(buffer);
#ifdef SUNWIPSEC
  ssh_udp_free_context(packet_context);
#endif /* SUNWIPSEC */
  ssh_udp_read(listener, NULL,
#ifdef SUNWIPSEC
	       NULL,
#endif /* SUNWIPSEC */
	       0, NULL, 0,
               (unsigned char *)remote_address, sizeof(remote_address), &len);
  return;
}


/*                                                              shade{0.9}
 * Isakmp first packet udp-packet handler. Called from
 * udp-listerer when packet is received from that socket.       shade{1.0}
 */
void ike_udp_callback_first(SshUdpListener listener,
                            void *context)
{
  SshIkeNegotiation negotiation = (SshIkeNegotiation) context;
  unsigned char remote_address[SSH_IKE_IP_ADDR_STR_LEN];
  unsigned char remote_port[SSH_IKE_IP_PORT_STR_LEN];
  SshBuffer buffer;
  unsigned char *p;
  SshUdpError error;
  size_t len;
#ifdef SUNWIPSEC
  SshUdpPacketContext packet_context = NULL;
#endif /* SUNWIPSEC */

  SSH_ASSERT(listener == negotiation->ike_ed->listener);
  SSH_DEBUG(5, ("Packet ready in source %s:%s",
                negotiation->ike_pm_info->remote_ip,
                negotiation->ike_pm_info->remote_port));

  buffer = ssh_buffer_allocate();
  if (buffer == NULL)
    goto skip_packet;

  if (ssh_buffer_append_space(buffer, &p, SSH_IKE_MAX_UDP_PACKET) !=
      SSH_BUFFER_OK)
    goto skip_packet;

  error = ssh_udp_read(listener,
#ifdef SUNWIPSEC
		       &packet_context,
#endif /* SUNWIPSEC */
		       remote_address, sizeof(remote_address),
                       remote_port, sizeof(remote_port),
                       p, SSH_IKE_MAX_UDP_PACKET, &len);

  switch (error)
    {
    case SSH_UDP_OK:
      /* Packet successfully received. This means we can destroy to listener
         and start using the default for rest of the packets. */
      ssh_udp_destroy_listener(negotiation->ike_ed->listener);
      negotiation->ike_ed->listener = NULL;
      break;

    case SSH_UDP_HOST_UNREACHABLE:
      /* Got UDP host unreachable, abort negotiation immediately */
      SSH_DEBUG(3, ("ike_udp_callback_first got UDP host "
                    "unreachable from %s:%s",
                    negotiation->ike_pm_info->remote_ip,
                    negotiation->ike_pm_info->remote_port));
      ike_send_notify(negotiation->sa->server_context, negotiation,
                      SSH_IKE_NOTIFY_MESSAGE_UDP_HOST_UNREACHABLE);
#ifdef SUNWIPSEC
      ssh_udp_free_context(packet_context);
#endif /* SUNWIPSEC */
      ssh_buffer_free(buffer);
      return;
    case SSH_UDP_PORT_UNREACHABLE:
      /* Got UDP port unreachable, abort negotiation immediately */
      SSH_DEBUG(3, ("ike_udp_callback_first got UDP port "
                    "unreachable from %s:%s",
                    negotiation->ike_pm_info->remote_ip,
                    negotiation->ike_pm_info->remote_port));
      ike_send_notify(negotiation->sa->server_context, negotiation,
                      SSH_IKE_NOTIFY_MESSAGE_UDP_PORT_UNREACHABLE);
#ifdef SUNWIPSEC
      ssh_udp_free_context(packet_context);
#endif /* SUNWIPSEC */
      ssh_buffer_free(buffer);
      return;
    default:
#ifdef SUNWIPSEC
      ssh_udp_free_context(packet_context);
#endif /* SUNWIPSEC */
      ssh_buffer_free(buffer);
      return;
    }

  /* Adjust buffer */
  ssh_buffer_consume_end(buffer, SSH_IKE_MAX_UDP_PACKET - len);

  /* Call common callback code, note that it will free the buffer. */
  ike_udp_callback_common(negotiation->sa->server_context,
#ifdef SUNWIPSEC
			  packet_context,
#endif /* SUNWIPSEC */
                          remote_address, remote_port, buffer);
  return;
 skip_packet:
  if (buffer)
    ssh_buffer_free(buffer);
#ifdef SUNWIPSEC
  ssh_udp_free_context(packet_context);
#endif /* SUNWIPSEC */
  ssh_udp_read(listener, NULL, NULL, 0, NULL, 0,
               (unsigned char *)remote_address, sizeof(remote_address), &len);
  return;
}


/*                                                              shade{0.9}
 * Isakmp common packet udp-packet handler. Called from
 * udp-listerer when packet is received from that socket.       shade{1.0}
 */
void ike_udp_callback_common(SshIkeServerContext server,
#ifdef SUNWIPSEC
			     void *packet_context,
#endif /* SUNWIPSEC */
                             unsigned char *remote_address,
                             unsigned char *remote_port,
                             SshBuffer buffer)
{
  SshIkeExchangeType exchange_type;
  int major_version, minor_version;
  SshIkeNegotiation negotiation;
  SshIkeNotifyMessageType ret;
  SshUInt32 message_id;
  unsigned char *p;
  SshIkeSA sa;
  size_t len;
  int i;
#ifdef SUNWIPSEC
  Boolean weird_check = FALSE;
#endif

  p = ssh_buffer_ptr(buffer);
  len = ssh_buffer_len(buffer);

  SSH_DEBUG(8, ("Packet from %s:%s, data[0..%d] = %08x %08x %08x %08x ...",
                remote_address, remote_port, len,
                SSH_IKE_GET32(p), SSH_IKE_GET32(p + 4),
                SSH_IKE_GET32(p + 8), SSH_IKE_GET32(p + 12)));

#ifndef SUNWIPSEC
  if (strcmp(ssh_csstr(server->server_port), "4500") == 0)
    {
      /* Check that it has a valid non-ESP marker. */
      if (len < 4 || memcmp(p, "\0\0\0\0", 4) != 0)
        {
          SSH_IKE_DEBUG(3, NULL, ("Invalid NAT-T IKE packet, ip = %s:%s",
                                  remote_address, remote_port));
          SSH_IKE_DEBUG_BUFFER(13, NULL, "Received packet", len, p);
          ssh_buffer_free(buffer);
          return;
        }

      /* Remove the non-ESP marker. */
      ssh_buffer_consume(buffer, 4);
      p = ssh_buffer_ptr(buffer);
      len = ssh_buffer_len(buffer);
    }
#endif

  /* Find sa */
  ret = ike_get_sa(server, remote_address, remote_port,
                   &sa, &exchange_type, &message_id,
                   &major_version, &minor_version, buffer);
  if (ret != 0)
    {
      server->statistics.invalid_cookie_count++;
      SSH_IKE_DEBUG(3, NULL, ("Packet to unknown Isakmp SA, ip = %s:%s",
                              remote_address, remote_port));
      SSH_IKE_DEBUG_BUFFER(13, NULL, "Received packet", len, p);
      ssh_udp_free_context(packet_context);
      ssh_buffer_free(buffer);
      return;
    }

  /* Update statistics. This includes all retransmissions etc. */
  sa->statistics.packets_in++;
  sa->statistics.octects_in += len;
  server->statistics.total_packets_in++;
  server->statistics.total_octets_in += len;

  if (sa->lock_flags & SSH_IKE_ISAKMP_LOCK_FLAG_UNINITIALIZED)
    {
      SshIkeNewConnectionCBContext ctx;

      /* New sa */
      SSH_DEBUG(6, ("New SA"));

      /* Initialize isakmp sa, and isakmp negotiation */
      if (!ike_init_isakmp_sa(sa,
#ifdef SUNWIPSEC
			      packet_context,
#endif /* SUNWIPSEC */
			      server->server_name, server->server_port,
                              remote_address, remote_port,
                              major_version, minor_version, exchange_type,
                              FALSE, FALSE))
        {
          ike_sa_delete(server->isakmp_context, sa);
          ssh_udp_free_context(packet_context);
          ssh_buffer_free(buffer);
          ssh_free(sa);
          return;
        }
      packet_context = NULL;
      negotiation = sa->isakmp_negotiation;
      SSH_IKE_DEBUG(6, negotiation, ("New SA"));

      ctx = ssh_calloc(1, sizeof(*ctx));
      if (ctx == NULL)
        {
          ike_delete_negotiation(negotiation);
          ssh_buffer_free(buffer);
          return;
        }
      ctx->negotiation = negotiation;
      ctx->server = server;
      ctx->message_id = message_id;
      ctx->buffer = buffer;

      negotiation->lock_flags |= SSH_IKE_NEG_LOCK_FLAG_PROCESSING_PM_QUERY;
      ssh_policy_new_connection(negotiation->ike_pm_info,
                                ike_policy_reply_new_connection,
                                ctx);
      if (negotiation->lock_flags & SSH_IKE_NEG_LOCK_FLAG_PROCESSING_PM_QUERY)
        {
          /* Policy manager could not reply to query immediately. Return
             RETRY_LATER to state machine so it will postpone processing of the
             packet until the policy manager answers and calls callback
             function. Clear PROCESSING_PM_QUERY flag before returning to the
             state machine. Note that state machine will set the
             WAITING_PM_REPLY flag. */
          negotiation->lock_flags &=
            ~(SSH_IKE_NEG_LOCK_FLAG_PROCESSING_PM_QUERY);
          negotiation->lock_flags |= SSH_IKE_NEG_LOCK_FLAG_WAITING_PM_REPLY;
          return;
        }
      /* Policy manager replied immediately. */
      ike_new_connection_cb_done(ctx);
      return;
    }
  negotiation = NULL;
  ssh_udp_free_context(packet_context);
  packet_context = NULL;

#ifdef SUNWIPSEC
  /* if we are natt, update port info... */
  if (sa->sa_natt_state >= NATT_STATE_VID ||
      sa->sa_natt_state <= NATT_STATE_NATD) {
      if (strcmp((char *)remote_port,
	      (char *)server->isakmp_context->default_port) != 0) {
	  if (sa->sa_other_port)
	      ssh_xfree(sa->sa_other_port);
	  sa->sa_natt_state = NATT_STATE_GOT_PORT;
	  sa->sa_other_port = ssh_xstrdup(remote_port);
          ssh_policy_sun_info("Updating port info for NAT-T, "
	      "remote port = %s", sa->sa_other_port); 
      } else {
	  /* Check for portjump weirdness, but not until we know this is not a
	     duplicate. */
          ssh_policy_sun_info("Using default remote port for NAT-T, "
	     "if active.", sa->sa_natt_state);
	  weird_check = (sa->sa_natt_state == NATT_STATE_NATD);
      }
  }
#endif
  /* Check if we are waiting for done in the isakmp sa. If so receiving
     phase 2 packet (message id != 0) means that the other end must have
     received our final packet, so we can advance ourself to done state. */
  if (sa->isakmp_negotiation->lock_flags &
      SSH_IKE_NEG_LOCK_FLAG_WAITING_FOR_DONE &&
      message_id != 0)
    {
      /* Advance the state machine */
      ret = ike_state_step(server->isakmp_context, NULL,
                           NULL, sa, sa->isakmp_negotiation);
      if (ret == SSH_IKE_NOTIFY_MESSAGE_CONNECTED)
        {
          SSH_DEBUG(7, ("Connected, sending notify"));
          ike_send_notify(server, sa->isakmp_negotiation, ret);
        }
      else if (ret != 0)
        {
          SSH_DEBUG(7, ("Error, send notify"));
          ike_send_notify(server, sa->isakmp_negotiation, ret);
          ssh_buffer_free(buffer);
          return;
        }
    }
  /* Check for info exchanges in the middle of isakmp SA */
  if (exchange_type == SSH_IKE_XCHG_TYPE_INFO)
    {
      /* Informal exchange. This can occur in the middle of isakmp sa
         negotiation with identical message id */
      negotiation = ike_alloc_negotiation(sa);
      SSH_DEBUG(8, ("New informational negotiation message_id = %08x "
                    "initialized using slot %d",
                    message_id, negotiation->negotiation_index));
      if (!ike_init_info_negotiation(negotiation,
                                     sa->isakmp_negotiation->ike_pm_info,
                                     server->server_name, server->server_port,
                                     remote_address, remote_port,
                                     major_version, minor_version,
                                     FALSE, message_id))
        {
          ike_delete_negotiation(negotiation);
          ssh_buffer_free(buffer);
          return;
        }

      /* Mark it so that we never send any notifications for this */
      negotiation-> notification_state =
        SSH_IKE_NOTIFICATION_STATE_ALREADY_SENT;

      SSH_IKE_DEBUG(6, negotiation, ("New informational negotiation"));
    }
  else if (message_id == 0)
    {
      /* Isakmp SA */
      negotiation = sa->isakmp_negotiation;
    }
  else
    {
      /* Find matching negotiation from old sa */
      for (i = 0; i < sa->number_of_negotiations; i++)
        {
          if (sa->negotiations[i] != NULL)
            {
              SSH_DEBUG(9, ("Finding negotiation for %08x, "
                            "[%d].message-id = %08x",
                            message_id, i,
                            sa->negotiations[i]->ed->message_id));
              if (sa->negotiations[i]->ed->message_id == message_id)
                {
                  negotiation = sa->negotiations[i];
                  break;
                }
            }
        }
      if (negotiation && negotiation->exchange_type != exchange_type)
        {
          SSH_IKE_DEBUG(3, NULL,
                        ("Other end change exchange type, packet ignored"));
          SSH_IKE_DEBUG_BUFFER(13, NULL,
                               "Received packet, but the other end changed "
                               "the exchange type, packet ignored", len, p);
          ssh_buffer_free(buffer);
          return;
        }
    }
  /* Check if message_id found */
  if (negotiation)
    {
      const unsigned char *old_remote_ip = NULL, *old_remote_port = NULL;

      /* Old negotiation found */
      SSH_DEBUG(8, ("Old negotiation message_id = %08x, slot %d",
                    message_id, negotiation->negotiation_index));
      SSH_IKE_DEBUG(6, negotiation, ("Packet to old negotiation"));

      if (negotiation->ed == NULL)
        {
          /* This negotiation is already been deleted, and this must be
             retransmit to that negotiation. Ignore the packet */
          ssh_buffer_free(buffer);
          return;
        }

      /* Check if this is retransmit of previous packet */
      if (negotiation->ed->last_recv_packet_len == len &&
          negotiation->ed->last_recv_packet != NULL &&
          memcmp(negotiation->ed->last_recv_packet, p, len) == 0)
        {
          SSH_DEBUG(6, ("Other end retransmitted its packet"));
          if (negotiation->lock_flags &
              SSH_IKE_NEG_LOCK_FLAG_WAITING_PM_REPLY)
            {
              SSH_DEBUG(6, ("Cannot resend our response, "
                            "because we are waiting for reply from the "
                            "policy manager"));
              ssh_buffer_free(buffer);
              return;
            }
          if (negotiation->ed->last_sent_packet_len == 0)
            {
              SSH_DEBUG(6, ("Cannot resend our response, "
                            "because we haven't sent any packets to other end "
                            "yet, or we have already received final packet "
                            "from it"));
              ssh_buffer_free(buffer);
              return;
            }
          if ((negotiation->ed->retry_timer != 0 &&
               ssh_time_measure_stamp(negotiation->ed->last_packet_time,
                                      SSH_TIME_GRANULARITY_SECOND) <
               negotiation->ed->retry_timer) ||
              (negotiation->ed->retry_timer == 0 &&
               ssh_time_measure_stamp(negotiation->ed->last_packet_time,
                                      SSH_TIME_GRANULARITY_MICROSECOND) <
               negotiation->ed->retry_timer_usec))
            {
              SSH_DEBUG(6, ("Cannot resend our response, because we have "
                            "just sent a packet to other end"));
              ssh_buffer_free(buffer);
              return;
            }
          /* Retransmit our own previous packet back */
          SSH_DEBUG(6, ("Resending our previous packet"));

          /* Send the packet. Even if this fails, there is nothing we need to
             do, as the othere end will retry again later, and hopefully we
             have then more memory etc. */
          ike_send_packet(negotiation,
                          negotiation->ed->last_sent_packet,
                          negotiation->ed->last_sent_packet_len,
                          TRUE, TRUE);
          ssh_buffer_free(buffer);
          return;
        }
#ifdef SUNWIPSEC
      else if (weird_check) {
	      /* post-portjump packet on 500. something weird. no natt */
	      ssh_policy_sun_info(
		  "Post-portjump packet on port 500, reverting to no NAT-T");
	      sa->sa_natt_state = NATT_STATE_NEVER;
      }
#endif

      if (negotiation->lock_flags & SSH_IKE_NEG_LOCK_FLAG_WAITING_FOR_REMOVE)
	{
	  /* If we are waiting for removal, do not accept any more input
             for this negotiation. There is no reason to process these
             packets, since we are anyway going to be removed. */
          SSH_DEBUG(6, ("Ignoring IKEv1 packet, since negotiation waiting"
                        " for removal."));

          ssh_buffer_free(buffer);
          return;
        }

      /* Check if the server or the ip/port numbers has changed. */
      switch (negotiation->exchange_type)
        {
        case SSH_IKE_XCHG_TYPE_INFO:
          old_remote_ip = negotiation->info_pm_info->remote_ip;
          old_remote_port = negotiation->info_pm_info->remote_port;
          break;
        case SSH_IKE_XCHG_TYPE_NGM:
          old_remote_ip = negotiation->ngm_pm_info->remote_ip;
          old_remote_port = negotiation->ngm_pm_info->remote_port;
          break;
        case SSH_IKE_XCHG_TYPE_QM:
          old_remote_ip = negotiation->qm_pm_info->remote_ip;
          old_remote_port = negotiation->qm_pm_info->remote_port;
          break;
        case SSH_IKE_XCHG_TYPE_IP:
        case SSH_IKE_XCHG_TYPE_AGGR:
          old_remote_ip = negotiation->ike_pm_info->remote_ip;
          old_remote_port = negotiation->ike_pm_info->remote_port;
          break;
#ifdef SSHDIST_ISAKMP_CFG_MODE
        case SSH_IKE_XCHG_TYPE_CFG:
          old_remote_ip = negotiation->cfg_pm_info->remote_ip;
          old_remote_port = negotiation->cfg_pm_info->remote_port;
          break;
#endif /* SSHDIST_ISAKMP_CFG_MODE */
        default:

          break;
        }

      /* Check if the data is changed. */
      if (old_remote_ip != NULL &&
         (ssh_inet_ip_address_compare(old_remote_ip, remote_address) != 0 ||
          ssh_inet_port_number_compare(old_remote_port, remote_port,
                                       ssh_custr("udp")) != 0 ||
          negotiation->sa->server_context != server))
        {
          /* Yes, give notification. */
          switch (negotiation->exchange_type)
            {
            case SSH_IKE_XCHG_TYPE_INFO:
              ssh_policy_phase_ii_server_changed(negotiation->info_pm_info,
                                                 server, remote_address,
                                                 remote_port);
              break;
            case SSH_IKE_XCHG_TYPE_NGM:
              ssh_policy_phase_ii_server_changed(negotiation->ngm_pm_info,
                                                 server, remote_address,
                                                 remote_port);
              break;
#ifdef SSHDIST_ISAKMP_CFG_MODE
            case SSH_IKE_XCHG_TYPE_CFG:
              ssh_policy_phase_ii_server_changed(negotiation->cfg_pm_info,
                                                 server, remote_address,
                                                 remote_port);
              break;
#endif /* SSHDIST_ISAKMP_CFG_MODE */
            case SSH_IKE_XCHG_TYPE_QM:
              ssh_policy_phase_qm_server_changed(negotiation->qm_pm_info,
                                                 server, remote_address,
                                                 remote_port);
              break;
            case SSH_IKE_XCHG_TYPE_IP:
            case SSH_IKE_XCHG_TYPE_AGGR:
              ssh_policy_phase_i_server_changed(negotiation->ike_pm_info,
                                                server, remote_address,
                                                remote_port);
              break;
            default:
              ssh_fatal("Internal error: Unknown exchange type in the "
                        "negotiation->exchange_type.");
              break;
            }
          /* Make sure that our local copy of the SshIkeServerContext matches
             the current one in the negotiation (in case the notification
             changed it. */
          server = negotiation->sa->server_context;
        }
    }
  else
    {
      /* Not found, check if we can create new one? */
      if (!sa->phase_1_done)
        {
          SSH_IKE_DEBUG(3, NULL,
                        ("Cannot start new phase 2 negotiation, "
                         "because phase 1 still in progress"));
          SSH_IKE_DEBUG_BUFFER(13, NULL,
                               "Received packet, but cannot start phase 2 "
                               "negotiation, because phase 1 still in "
                               "progress", len, p);
          ssh_buffer_free(buffer);
          return;
        }
      else
        {
          SshIkeNewConnectionCBContext ctx;

          switch (exchange_type)
            {
            case SSH_IKE_XCHG_TYPE_NGM:
              negotiation = ike_alloc_negotiation(sa);
              SSH_DEBUG(8, ("New new group mode negotiation message_id = %08x "
                            "initialized, using slot %d",
                            message_id, negotiation->negotiation_index));
              if (!ike_init_ngm_negotiation(negotiation,
                                            sa->isakmp_negotiation->
                                            ike_pm_info,
                                            server->server_name,
                                            server->server_port,
                                            remote_address, remote_port,
                                            major_version, minor_version,
                                            exchange_type, FALSE,
                                            message_id, FALSE))
                {
                  ike_delete_negotiation(negotiation);
                  ssh_buffer_free(buffer);
                  return;
                }
              negotiation->ed->auth_method_type = SSH_IKE_AUTH_METHOD_PHASE_1;
              break;
#ifdef SSHDIST_ISAKMP_CFG_MODE
            case SSH_IKE_XCHG_TYPE_CFG:
              negotiation = ike_alloc_negotiation(sa);
              SSH_DEBUG(8, ("New configuration mode negotiation "
                            "message_id = %08x initialized, using slot %d",
                            message_id, negotiation->negotiation_index));
              if (!ike_init_cfg_negotiation(negotiation,
                                            sa->isakmp_negotiation->
                                            ike_pm_info,
                                            server->server_name,
                                            server->server_port,
                                            remote_address, remote_port,
                                            major_version, minor_version,
                                            exchange_type, FALSE,
                                            message_id, FALSE))
                {
                  ike_delete_negotiation(negotiation);
                  ssh_buffer_free(buffer);
                  return;
                }
              break;
#endif /* SSHDIST_ISAKMP_CFG_MODE */
            case SSH_IKE_XCHG_TYPE_QM:
              negotiation = ike_alloc_negotiation(sa);
              SSH_DEBUG(8, ("New quick mode negotiation message_id = %08x "
                            "initialized, using slot %d",
                            message_id, negotiation->negotiation_index));
              if (!ike_init_qm_negotiation(negotiation,
#ifdef SUNWIPSEC
					   ssh_udp_dup_context(sa->isakmp_negotiation->
							       packet_context),
#endif /* SUNWIPSEC */
                                           sa->isakmp_negotiation->ike_pm_info,
                                           server->server_name,
                                           server->server_port,
                                           remote_address, remote_port,
                                           exchange_type, FALSE,
                                           message_id, FALSE))
                {
                  ike_delete_negotiation(negotiation);
                  ssh_buffer_free(buffer);
                  return;
                }
              break;
            default:
              SSH_IKE_DEBUG(3, NULL,
                            ("Unknown phase 2 negotiation exchange type %d",
                             exchange_type));
              ssh_ike_audit(sa->isakmp_negotiation,
                            SSH_AUDIT_IKE_INVALID_EXCHANGE_TYPE,
                            "Invalid phase 2 exchange type");
              /* XXX Send notify? */
              ssh_buffer_free(buffer);
              return;
              break;
            }

          SSH_IKE_DEBUG(6, negotiation, ("New negotiation"));

          ctx = ssh_calloc(1, sizeof(*ctx));
          if (ctx == NULL)
            {
              ike_delete_negotiation(negotiation);
              ssh_buffer_free(buffer);
              return;
            }
          ctx->negotiation = negotiation;
          ctx->server = server;
          ctx->message_id = message_id;
          ctx->buffer = buffer;

          negotiation->lock_flags |= SSH_IKE_NEG_LOCK_FLAG_PROCESSING_PM_QUERY;
          switch (exchange_type)
            {
            case SSH_IKE_XCHG_TYPE_NGM:
              ssh_policy_new_connection_phase_ii(negotiation->ngm_pm_info,
                                         ike_policy_reply_new_connection, ctx);
              break;
#ifdef SSHDIST_ISAKMP_CFG_MODE
            case SSH_IKE_XCHG_TYPE_CFG:
              ssh_policy_new_connection_phase_ii(negotiation->cfg_pm_info,
                                         ike_policy_reply_new_connection, ctx);
              break;
#endif /* SSHDIST_ISAKMP_CFG_MODE */
            case SSH_IKE_XCHG_TYPE_QM:
              ssh_policy_new_connection_phase_qm(negotiation->qm_pm_info,
                                         ike_policy_reply_new_connection, ctx);
              break;
            default:
              SSH_IKE_DEBUG(3, NULL,
                            ("Unknown phase 2 negotiation exchange type %d",
                             exchange_type));
              ssh_buffer_free(buffer);
              return;
              break;
            }
          if (negotiation->lock_flags &
              SSH_IKE_NEG_LOCK_FLAG_PROCESSING_PM_QUERY)
            {
              /* Policy manager could not reply to query immediately. Return
                 RETRY_LATER to state machine so it will postpone processing of
                 the packet until the policy manager answers and calls callback
                 function. Clear PROCESSING_PM_QUERY flag before returning to
                 the state machine. Note that state machine will set the
                 WAITING_PM_REPLY flag. */
              negotiation->lock_flags &=
                ~(SSH_IKE_NEG_LOCK_FLAG_PROCESSING_PM_QUERY);
              negotiation->lock_flags |=
                SSH_IKE_NEG_LOCK_FLAG_WAITING_PM_REPLY;
              return;
            }
          /* Policy manager replied immediately. */
          ike_new_connection_cb_done(ctx);
          return;
        }
    }
#ifdef SUNWIPSEC
  ike_process_packet(server, sa, negotiation, remote_port, buffer);
#else
  ike_process_packet(server, sa, negotiation, buffer);
#endif
  ssh_buffer_free(buffer);
}

/*                                                              shade{0.9}
 * Call ike_remove_isakmp_sa                                    shade{1.0}
 */
void ike_call_ike_remove_isakmp_sa(void *negotiation)
{
  ssh_ike_remove_isakmp_sa(negotiation, SSH_IKE_REMOVE_FLAGS_SEND_DELETE);
}

/*                                                              shade{0.9}
 * Send isakmp notify.                                          shade{1.0}
 */
void ike_send_notify(SshIkeServerContext server,
                     SshIkeNegotiation negotiation,
                     SshIkeNotifyMessageType ret)
{
  SshIkePacket isakmp_packet_out;
  SshIkePayload pl;
  SshBuffer buffer = NULL;
  SshIkeNegotiation info_negotiation;
  SshIkeSA sa = negotiation->sa;
  SshUInt32 sec, u_sec;

  /* Cancel all timeouts for this sa */
  ssh_cancel_timeouts(SSH_ALL_CALLBACKS, negotiation);

  if (negotiation->negotiation_index == -1 &&
      sa->cipher_iv == NULL && negotiation->ed != NULL)
    {
      /* If this is ISAKMP SA negotiation, make sure we steal the cipher_iv to
         the ISAKMP SA data structure before we free the ISAKMP SA negotiation
         structure. */
      sa->cipher_iv = negotiation->ed->cipher_iv;
      negotiation->ed->cipher_iv = NULL;
    }

  if (negotiation->notification_state !=
      SSH_IKE_NOTIFICATION_STATE_ALREADY_SENT)
    {
      /* Call callback */
      ike_call_callbacks(negotiation, ret);
    }
  ike_negotiation_max_lifetime(negotiation, &sec, &u_sec);

  /* Check for success */
  if (ret == SSH_IKE_NOTIFY_MESSAGE_CONNECTED || ret == 0)
    {
      SSH_DEBUG(5, ("Connected, SA = { %08x %08x - %08x %08x}, nego = %d",
                    SSH_IKE_GET32(sa->cookies.initiator_cookie),
                    SSH_IKE_GET32(sa->cookies.initiator_cookie + 4),
                    SSH_IKE_GET32(sa->cookies.responder_cookie),
                    SSH_IKE_GET32(sa->cookies.responder_cookie + 4),
                    negotiation->negotiation_index));

      SSH_IKE_DEBUG(6, negotiation, ("Connected"));

      /* First check if we need to wait for something. */
      if (negotiation->lock_flags & SSH_IKE_NEG_LOCK_FLAG_WAITING_FOR_REMOVE)
        {
          /* If we are waiting for done, add timer to restart state machine
             after wait time expires. */
          ssh_xregister_timeout(sec, u_sec, ike_remove_callback, negotiation);
          return;
        }

      if (negotiation->lock_flags & SSH_IKE_NEG_LOCK_FLAG_WAITING_FOR_DONE)
        {
          /* If we are waiting for done, add timer to restart state machine
             after wait time expires. */
          ssh_xregister_timeout(sec, u_sec, ike_state_restart_packet,
                               negotiation);
          return;
        }

      /* No need to wait, if this is isakmp SA insert expire timer, and delete
         all other negotiations, immediately. */

      if (negotiation->negotiation_index == -1)
        {
          SshUInt32 t_max;
          SshTime t;

          t_max = sa->retry_limit * sa->retry_timer_max +
            sa->retry_limit * sa->retry_timer_max_usec
            / 1000000;

          if (t_max > sa->expire_timer)
            t_max = sa->expire_timer;

          /* Free negotiation stuff (ciphers contexts, last packets etc, if we
             are not waiting for done.) */
          ike_free_negotiation_isakmp(negotiation);

          t = ssh_time();
          if (t < negotiation->ike_pm_info->sa_expire_time)
            t = negotiation->ike_pm_info->sa_expire_time - t;
          else
            t = 0;
          if (t > t_max && t - t_max > 60)
            t -= t_max;
          ssh_xregister_timeout((SshUInt32) t, 0,
                               ike_call_ike_remove_isakmp_sa,
                               negotiation);
        }
      else
        {
          ike_delete_negotiation(negotiation);
        }
      return;
    }

  if (negotiation->lock_flags & SSH_IKE_NEG_LOCK_FLAG_WAITING_FOR_REMOVE)
    {
      /* If the remove flag is already set, then we remove this negotiation
         immediately. */
      ike_delete_negotiation(negotiation);
      return;
    }

  /* Mark negotiation to waiting to be removed */
  negotiation->lock_flags |= SSH_IKE_NEG_LOCK_FLAG_WAITING_FOR_REMOVE;

  SSH_IKE_DEBUG(3, negotiation,
                ("Error = %s (%d)", ssh_ike_error_code_to_string(ret), ret));

  /* Check for informational exchanges (never send notification for them) */
  if (negotiation->exchange_type == SSH_IKE_XCHG_TYPE_INFO)
    {
      SSH_DEBUG(3, ("Notification to informational exchange ignored"));
      /* Delete negotiation immediately */
      ike_delete_negotiation(negotiation);
      return;
    }

  /* Check that error code is not private  */
  if (ret >= 8192)
    {
      /* Delete negotiation immediately. We dont send the private error codes
         to other and there is no need to keep the old negotiation. */

      SSH_DEBUG(3, ("Private notification, do not send notification"));

      /* Delete negotiation immediately */
      ike_delete_negotiation(negotiation);
      return;
    }

  /* Error send notify */
  if (!ike_init_info_exchange(sa->server_context, sa,
                              &isakmp_packet_out, &info_negotiation, &pl))
    {
      ike_delete_negotiation(negotiation);
      return;
    }

  /* Add n payload */
  isakmp_packet_out->first_n_payload = pl;
  pl->type = SSH_IKE_PAYLOAD_TYPE_N;
  pl->pl.n.doi = SSH_IKE_DOI_IPSEC;
  switch (negotiation->exchange_type)
    {
    case SSH_IKE_XCHG_TYPE_NONE:
    case SSH_IKE_XCHG_TYPE_ANY:
    case SSH_IKE_XCHG_TYPE_BASE:
    case SSH_IKE_XCHG_TYPE_IP:
    case SSH_IKE_XCHG_TYPE_AO:
    case SSH_IKE_XCHG_TYPE_AGGR:
      pl->pl.n.protocol_id = SSH_IKE_PROTOCOL_ISAKMP;
      pl->pl.n.spi_size = SSH_IKE_COOKIE_LENGTH * 2;
      pl->pl.n.spi = ike_register_new(isakmp_packet_out,
                                      SSH_IKE_COOKIE_LENGTH * 2);
      if (pl->pl.n.spi == NULL)
        goto error;

      memcpy(pl->pl.n.spi,
             sa->cookies.initiator_cookie, SSH_IKE_COOKIE_LENGTH);
      memcpy(pl->pl.n.spi + SSH_IKE_COOKIE_LENGTH,
             sa->cookies.responder_cookie, SSH_IKE_COOKIE_LENGTH);
      break;
    case SSH_IKE_XCHG_TYPE_INFO:
      ssh_fatal("Internal error in ike_send_notify");
      break;
#ifdef SSHDIST_ISAKMP_CFG_MODE
    case SSH_IKE_XCHG_TYPE_CFG:
      pl->pl.n.protocol_id = 0;
      pl->pl.n.spi_size = 0;
      pl->pl.n.spi = NULL;
      break;
#endif /* SSHDIST_ISAKMP_CFG_MODE */
    case SSH_IKE_XCHG_TYPE_QM:
      {
        SshIkePayload sa_payload;

        /* Find out the quick mode SPI value from the SA payload, so we can
           send notification to the other end. */
        sa_payload = NULL;
        if (negotiation->qm_ed && negotiation->qm_pm_info)
          {
            if (negotiation->qm_pm_info->this_end_is_initiator)
              {
                if (negotiation->qm_ed->sas_r)
                  sa_payload = negotiation->qm_ed->sas_r[0];
              }
            else
              {
                if (negotiation->qm_ed->sas_i)
                  sa_payload = negotiation->qm_ed->sas_i[0];
              }
          }

        if (sa_payload && sa_payload->pl.sa.number_of_proposals > 0 &&
            sa_payload->pl.sa.proposals &&
            sa_payload->pl.sa.proposals[0].number_of_protocols > 0 &&
            sa_payload->pl.sa.proposals[0].protocols)
          {
            pl->pl.n.protocol_id =
              sa_payload->pl.sa.proposals[0].protocols[0].protocol_id;
            pl->pl.n.spi_size =
              sa_payload->pl.sa.proposals[0].protocols[0].spi_size;
            pl->pl.n.spi =
              ike_register_copy(isakmp_packet_out,
                                sa_payload->pl.sa.proposals[0].
                                protocols[0].spi,
                                pl->pl.n.spi_size);
            if (pl->pl.n.spi == NULL)
              goto error;
          }
        else
          {
            pl->pl.n.protocol_id = 0;
            pl->pl.n.spi_size = 0;
            pl->pl.n.spi = NULL;
          }
      }
      break;
    case SSH_IKE_XCHG_TYPE_NGM:
      {
        SshIkePayload sa_payload;

        sa_payload = NULL;
        if (negotiation->ngm_ed)
          {
            if (negotiation->ngm_pm_info->this_end_is_initiator)
              sa_payload = negotiation->ngm_ed->sa_r;
            else
              sa_payload = negotiation->ngm_ed->sa_i;
          }

        if (sa_payload && sa_payload->pl.sa.number_of_proposals > 0 &&
            sa_payload->pl.sa.proposals &&
            sa_payload->pl.sa.proposals[0].number_of_protocols > 0 &&
            sa_payload->pl.sa.proposals[0].protocols)
          {
            pl->pl.n.protocol_id =
              sa_payload->pl.sa.proposals[0].protocols[0].protocol_id;
            pl->pl.n.spi_size =
              sa_payload->pl.sa.proposals[0].protocols[0].spi_size;
            pl->pl.n.spi =
              ike_register_copy(isakmp_packet_out,
                                sa_payload->pl.sa.proposals[0].
                                protocols[0].spi,
                                pl->pl.n.spi_size);
            if (pl->pl.n.spi == NULL)
              goto error;
          }
        else
          {
            pl->pl.n.protocol_id = 0;
            pl->pl.n.spi_size = 0;
            pl->pl.n.spi = NULL;
          }
      }
      break;
    }
  pl->pl.n.notify_message_type = ret;

  if (negotiation->ed)
    {
      SshIkeSAAttributeList list;
      SshIkeDataAttribute attributes;
      int number_of_attributes, i;

      list = ssh_ike_data_attribute_list_allocate();
      if (list == NULL)
	goto error;

      ssh_ike_data_attribute_list_add_basic(list,
                            SSH_IKE_NOTIFY_CLASSES_VERSION,
                            SSH_IKE_NOTIFY_VALUES_VERSION_1);
      if (negotiation->ed->offending_payload_type !=
          SSH_IKE_PAYLOAD_TYPE_NONE)
        {
          ssh_ike_data_attribute_list_add_basic(list,
                SSH_IKE_NOTIFY_CLASSES_TYPE_OF_OFFENDING_PAYLOAD,
                (SshUInt16)negotiation->ed->offending_payload_type);
          ssh_ike_data_attribute_list_add(list,
                SSH_IKE_NOTIFY_CLASSES_TYPE_OF_OFFENDING_PAYLOAD,
                negotiation->ed->offending_payload,
                negotiation->ed->offending_payload_len);
          if (negotiation->ed->offending_payload_offset != -1)
            {
              ssh_ike_data_attribute_list_add_basic(list,
                        SSH_IKE_NOTIFY_CLASSES_ERROR_POSITION_OFFSET,
                        (SshUInt16)negotiation->ed->offending_payload_offset);
            }
        }
      if (negotiation->ed->error_text)
        {
          ssh_ike_data_attribute_list_add(list,
                                          SSH_IKE_NOTIFY_CLASSES_ERROR_TEXT,
                                          negotiation->ed->error_text,
                              strlen(ssh_csstr(negotiation->ed->error_text)));
        }
      ssh_ike_data_attribute_list_add_int(list,
                          SSH_IKE_NOTIFY_CLASSES_MESSAGE_ID,
                          (SshUInt64) negotiation->ed->message_id);

      if (ret == SSH_IKE_NOTIFY_MESSAGE_INVALID_EXCHANGE_TYPE ||
          ret == SSH_IKE_NOTIFY_MESSAGE_UNSUPPORTED_EXCHANGE_TYPE)
        {
          ssh_ike_data_attribute_list_add_basic(list,
                            SSH_IKE_NOTIFY_CLASSES_EXCHANGE_TYPE,
                            (SshUInt16)negotiation->exchange_type);
        }
      if (negotiation->ed->invalid_flags)
        {
          ssh_ike_data_attribute_list_add_basic(list,
                            SSH_IKE_NOTIFY_CLASSES_INVALID_FLAG_BITS,
                            negotiation->ed->invalid_flags);
        }

      attributes =
        ssh_ike_data_attribute_list_get(list, &number_of_attributes);
      ssh_ike_data_attribute_list_free(list);

      buffer = ssh_buffer_allocate();
      if (buffer == NULL)
        goto error;

      for (i = 0; i < number_of_attributes; i++)
        {
          if (ssh_ike_encode_data_attribute(buffer, &attributes[i], 0) == -1)
            {
              ssh_free(attributes);
              goto error;
            }
        }
      pl->pl.n.notification_data_size = ssh_buffer_len(buffer);
      pl->pl.n.notification_data =
        ike_register_copy(isakmp_packet_out, ssh_buffer_ptr(buffer),
                          ssh_buffer_len(buffer));
      ssh_buffer_free(buffer);
      buffer = NULL;
      ssh_free(attributes);
      if (pl->pl.n.notification_data == NULL)
        goto error;
    }
  else
    {
      pl->pl.n.notification_data_size = 0;
      pl->pl.n.notification_data = (unsigned char *) "";
    }

  buffer = ssh_buffer_allocate();
  if (buffer == NULL)
    goto error;

  SSH_IKE_DEBUG(6, info_negotiation,
                ("Sending negotiation back, error = %d",
                 ret));

  /* Encode response packet */
  ret = ike_encode_packet(sa->server_context->isakmp_context,
                          isakmp_packet_out,
                          sa, info_negotiation, buffer);
  if (ret != 0)
    {
      SSH_IKE_DEBUG(3, negotiation, ("Encoding notify packet failed : %d",
                                     ret));
    }
  else
    {
      SSH_DEBUG(6, ("Sending notification to %s:%s",
                    sa->isakmp_negotiation->ike_pm_info->remote_ip,
                    sa->isakmp_negotiation->ike_pm_info->remote_port));
      ret = ike_send_packet(info_negotiation,
                            ssh_buffer_ptr(buffer),
                            ssh_buffer_len(buffer),
                            FALSE, TRUE);
      if (ret != 0)
        {
          /* Error occurred. Remove the negotiation immediately by
             changing the expire timer to 0 seconds. */
          sec = 0;
          u_sec = 0;
        }
      else
        {
          /* Add the packet to current negotiation as last packet out, so if we
             get retrasnmit, we can resend our notification. */
          if (negotiation->ed->last_sent_packet)
            ssh_free(negotiation->ed->last_sent_packet);
          negotiation->ed->last_sent_packet =
            ssh_memdup(ssh_buffer_ptr(buffer), ssh_buffer_len(buffer));
          if (negotiation->ed->last_sent_packet == NULL)
            negotiation->ed->last_sent_packet_len = 0;
          else
            negotiation->ed->last_sent_packet_len = ssh_buffer_len(buffer);
        }
    }
 error:
  /* Free packet */
  ike_free_packet(isakmp_packet_out, info_negotiation->ed->compat_flags);

  /* Free buffer */
  if (buffer)
    ssh_buffer_free(buffer);

  /* Delete info negotiation */
  ike_delete_negotiation(info_negotiation);

  /* Add expire timer */
  ssh_xregister_timeout(sec, u_sec, ike_remove_callback, negotiation);
}
