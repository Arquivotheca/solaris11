/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SCU_SCSA_H
#define	_SCU_SCSA_H
#ifdef __cplusplus
extern "C" {
#endif

#define	SCU_ADDR_TO_TRAN(ap)	((ap)->a_hba_tran)

#define	SCU_ADDR_TO_HBA(ap)	(SCU_TRAN_TO_HBA(SCU_ADDR_TO_TRAN(ap)))

#define	SCU_TRAN_TO_HBA(tran)	\
	(((scu_iport_t *)(tran)->tran_hba_private)->scui_ctlp)

#define	SCU_TRAN_TO_IPORT(tran)	\
	((scu_iport_t *)(tran)->tran_hba_private)

#define	SCU_PKT_TO_CMD(pkt)	((scu_cmd_t *)(pkt->pkt_ha_private))

struct scu_task_arg {
	uint64_t	scu_task_lun;
	uint16_t	scu_task_tag;
	int		scu_task_function;
	uint8_t		*scu_task_resp_addr;
	uint32_t	scu_task_resp_len;
};

struct scu_cmd {
	union {
		struct scsi_pkt		*cmd_pkt;
		struct smp_pkt		*cmd_smp_pkt;
	} cmd_u;
	STAILQ_ENTRY(scu_cmd)	cmd_next;
	scu_tgt_t		*cmd_tgtp;
	scu_lun_t		*cmd_lunp;
	SCI_IO_REQUEST_HANDLE_T	cmd_lib_io_request;
	uint32_t
		cmd_tag_assigned_by_ossl	:1,
		cmd_dma			:1,
		cmd_poll		:1,
		cmd_sync		:1,
		cmd_noretry		:1,
		cmd_wq_queued		:1,	/* proteced by scut_wq_lock */
		cmd_prepared		:1,
		cmd_started		:1,
		cmd_retried		:1,
		cmd_finished		:1,
		cmd_timeout		:1,
		cmd_task		:1,
		cmd_smp			:1,
		cmd_flags		:19;
	uint16_t		cmd_tag;	/* task context / io slot tag */
	scu_io_slot_t		*cmd_slot;	/* slot for task/io */

	/* Task request related */
	scu_task_arg_t		cmd_task_arg;
	int			cmd_task_reason;

	/* SMP request related */
	int			cmd_smp_resplen;
};

#define	cmd_pkt			cmd_u.cmd_pkt
#define	cmd_smp_pkt		cmd_u.cmd_smp_pkt
#define	SCU_CMD_NAME(scu_cmdp) \
	((scu_cmdp)->cmd_task ? "task" : (scu_cmdp)->cmd_smp ? "SMP" : "I/O")

int	scu_scsa_setup(scu_ctl_t *);
void	scu_scsa_teardown(scu_ctl_t *);
void	scu_cq_handler(void *arg);
void	scu_start_wq(scu_ctl_t *, int);
void	scu_start_wqs(scu_ctl_t *);
int	scu_reset_lun(scu_subctl_t *, scu_lun_t *, int);
int	scu_reset_target(scu_subctl_t *, scu_tgt_t *, int);
int	scu_hard_reset(scu_subctl_t *, scu_tgt_t *, int);
int	scu_reset_controller(scu_subctl_t *, int);
void	scu_clear_tgt_timeout(scu_tgt_t *);
void	scu_clear_tgt_timeouts(scu_subctl_t *);

#ifdef __cplusplus
}
#endif
#endif	/* _SCU_SCSA_H */
