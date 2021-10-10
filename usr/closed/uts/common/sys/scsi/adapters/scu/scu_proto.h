/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SCU_PROTO_H
#define	_SCU_PROTO_H
#ifdef __cplusplus
extern "C" {
#endif

scu_iport_t	*scu_get_iport_by_ua(scu_ctl_t *, char *);
scu_phy_t	*scu_find_phy_by_sas_address(scu_ctl_t *, scu_iport_t *,
    char *);
scu_tgt_t	*scu_get_tgt_by_wwn(scu_ctl_t *, uint64_t);
boolean_t	scu_iport_has_tgts(scu_ctl_t *, scu_iport_t *);
SCI_DOMAIN_HANDLE_T	scu_get_domain_by_iport(scu_ctl_t *, scu_iport_t *);

void	scu_phymap_activate(void *, char *, void **);
void	scu_phymap_deactivate(void *, char *, void *);
int	scu_iport_configure_phys(scu_iport_t *);

int	scu_iport_tgtmap_create(scu_iport_t *);
int	scu_iport_tgtmap_destroy(scu_iport_t *);

void	scu_recover(void *);
void	scu_set_pkt_reason(scu_ctl_t *, scu_cmd_t *, uchar_t, uint_t, uint_t);

U32	scu_scic_cb_passthru_get_phy_identifier(void *, U8 *);
U32	scu_scic_cb_passthru_get_port_identifier(void *, U8 *);
U32	scu_scic_cb_passthru_get_connection_rate(void *, void *);
void	scu_scic_cb_passthru_get_destination_sas_address(void *, U8 **);
U32	scu_scic_cb_passthru_get_transfer_length(void *);
U32	scu_scic_cb_passthru_get_data_direction(void *);

U32	scu_scic_cb_smp_passthru_get_request(void *, U8 **);
U8	scu_scic_cb_smp_passthru_get_frame_type(void *);
U8	scu_scic_cb_smp_passthru_get_function(void *);
U8	scu_scic_cb_smp_passthru_get_allocated_response_length(void *);

/*
 * SCU FMA Prototypes
 */
void	scu_fm_ereport(scu_ctl_t *, char *);
int	scu_check_all_handle(scu_ctl_t *);

#ifdef __cplusplus
}
#endif
#endif /* _SCU_PROTO_H */
