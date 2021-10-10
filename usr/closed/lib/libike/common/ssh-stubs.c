/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Stub functions defined here allow functions defined outside of
 * libike to be called from within libike.
 */


#include <sys/types.h>


#pragma weak ssh_policy_isakmp_sa_freed

void
ssh_policy_isakmp_sa_freed(void *one)
{
}

#pragma weak ssh_policy_isakmp_id

void
ssh_policy_isakmp_id(void *one, void *two, void *three)
{
}

#pragma weak ssh_policy_phase_i_notification

void
ssh_policy_phase_i_notification(void *one, int two, int three, void * four,
    size_t five, int six, void *seven, size_t eight)
{
}

#pragma weak ssh_policy_cfg_notify_attrs

void
ssh_policy_cfg_notify_attrs(void *one, int two, void *three)
{
}

#pragma weak ssh_policy_isakmp_vendor_id

void
ssh_policy_isakmp_vendor_id(void *one, void *two, size_t three)
{
}

#pragma weak ssh_policy_cfg_fill_attrs

void
ssh_policy_cfg_fill_attrs(void *one, int two, void *three,
    void *four, void* five)
{
}

#pragma weak ssh_policy_new_connection_phase_ii

void
ssh_policy_new_connection_phase_ii(void *one, void *two, void *three)
{
}

#pragma weak ssh_policy_ngm_select_sa

void
ssh_policy_ngm_select_sa(void *one, void *two, void *three, void *four,
    void *five)
{
}

#pragma weak ssh_policy_delete

void
ssh_policy_delete(void *one, int two, void *three, int four,
    void *five, size_t siz)
{
}

#pragma weak ssh_policy_qm_select_sa

void
ssh_policy_qm_select_sa(void *one, void *two, int three, void *four,
    void *five, void *six)
{
}

#pragma weak ssh_policy_negotiation_done_phase_ii

void
ssh_policy_negotiation_done_phase_ii(void *one, int two)
{
}

#pragma weak ssh_policy_isakmp_select_sa

void
ssh_policy_isakmp_select_sa(void *one, void *two, void *three,
    void *four, void *five)
{
}

#pragma weak ssh_policy_find_public_key

void
ssh_policy_find_public_key(void *one, int two, void *three, void *four,
void *five)
{
}

#pragma weak ssh_policy_notification

void
ssh_policy_notification(void *one, int two, int three, void *four,
    size_t five, int six, void *seven, size_t eight)
{
}

#pragma weak ssh_policy_new_certificate

void
ssh_policy_new_certificate(void *one, int two, void *three, size_t four)
{
}

#pragma weak ssh_policy_negotiation_done_isakmp

void
ssh_policy_negotiation_done_isakmp(void *one, int two)
{
}

#pragma weak ssh_policy_new_connection

void
ssh_policy_new_connection(void *one, void *two, void *three)
{
}

#pragma weak ssh_policy_qm_remote_id

void
ssh_policy_qm_remote_id(void *one, void *two, void *three)
{
}

#pragma weak ssh_policy_isakmp_nonce_data_len

void
ssh_policy_isakmp_nonce_data_len(void *one, void *two, void *three)
{
}

#pragma weak ssh_policy_get_certificate_authorities

void
ssh_policy_get_certificate_authorities(void *one, void *two, void *three)
{
}

#pragma weak ssh_policy_qm_local_id

void
ssh_policy_qm_local_id(void *one, void *two, void *three)
{
}

#pragma weak ssh_policy_negotiation_done_qm

void
ssh_policy_negotiation_done_qm(void *one, int two)
{
}

#pragma weak ssh_policy_phase_qm_notification

void
ssh_policy_phase_qm_notification(void *one, int two, void *three, size_t four,
int five, void *six, size_t seven)
{
}

#pragma weak ike_report_error

void
ike_report_error(void *one, int two, int three, int four)
{
}

#pragma weak ssh_policy_qm_sa_freed

void
ssh_policy_qm_sa_freed(void *one)
{
}

#pragma weak ssh_policy_new_connection_phase_qm

void
ssh_policy_new_connection_phase_qm(void *one, void *two, void *three)
{
}

#pragma weak ssh_policy_phase_ii_sa_freed

void
ssh_policy_phase_ii_sa_freed(void *one)
{
}

#pragma weak ssh_policy_request_certificates

void
ssh_policy_request_certificates(void *one, int two, void *three, void *four,
void *five, void *six, void *seven)
{
}

#pragma weak ssh_policy_find_pre_shared_key

void
ssh_policy_find_pre_shared_key(void *one, void *two, void *three)
{
}

#pragma weak ssh_policy_qm_nonce_data_len

void
ssh_policy_qm_nonce_data_len(void *one, void *two, void *three)
{
}

#pragma weak ssh_policy_find_private_key

void
ssh_policy_find_private_key(void *one, int two, void *three, void *four,
    size_t five, void *six, void *seven)
{
}

#pragma weak ssh_policy_isakmp_request_vendor_ids

void
ssh_policy_isakmp_request_vendor_ids(void *one, void *two, void *three)
{
}

#pragma weak ssh_policy_sun_info

void
ssh_policy_sun_info(void *one, ...)
{
}

#pragma weak ssh_policy_phase_i_server_changed

void
ssh_policy_phase_i_server_changed(void *one, void *two, void *three, void *four)
{
}

#pragma weak ssh_policy_phase_ii_server_changed

void
ssh_policy_phase_ii_server_changed(void *a, void *b, void *c, void *d)
{
}

#pragma weak ssh_policy_phase_qm_server_changed

void
ssh_policy_phase_qm_server_changed(void *a, void *b, void *c, void *d)
{
}
