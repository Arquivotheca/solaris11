/*
 * All Rights Reserved, Copyright (c) FUJITSU LIMITED 2008
 */

#ifndef	_SCFTIMER_H
#define	_SCFTIMER_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Timer code
 */
typedef enum {
	SCF_TIMERCD_CMDBUSY,	/* SCF command busy watch timer */
	SCF_TIMERCD_CMDEND,	/* SCF command completion watch timer */
	SCF_TIMERCD_ONLINE,	/* SCF online watch timer */
	SCF_TIMERCD_NEXTRECV,	/* Next receive wait timer */
	SCF_TIMERCD_DSCP_ACK,	/* DSCP interface TxACK watch timer */
	SCF_TIMERCD_DSCP_END,	/* DSCP interface TxEND watch timer */
	SCF_TIMERCD_DSCP_BUSY,	/* DSCP interface busy watch timer */
	SCF_TIMERCD_DSCP_CALLBACK, /* DSCP interface callback timer */
	SCF_TIMERCD_BUF_FUL,	/* SCF command BUF_FUL retry timer */
	SCF_TIMERCD_RCI_BUSY,	/* SCF command RCI_BUSY retry timer */
	SCF_TIMERCD_DSCP_INIT,	/* DSCP INIT_REQ retry timer */
	SCF_TIMERCD_DKMD_INIT,	/* DKMD INIT_REQ retry timer */
	SCF_TIMERCD_MAX		/* Max timer code */
} scf_tm_code_t;

/*
 * Timer table
 */
typedef struct scf_timer_tbl {
	uint8_t		code;		/* Timer code */
	uint8_t		rsv[3];		/* reserved */
	timeout_id_t	id;		/* Timer ID */
} scf_timer_tbl_t;

/*
 * Timer control table
 */
typedef struct scf_timer {
	scf_timer_tbl_t	tbl[2];
	uint8_t		start;		/* Timer start flag */
	uint8_t		restart;	/* Timer restart flag */
	uint8_t		stop;		/* Timer stop flag */
	uint8_t		side;		/* Use table side */
	uint32_t	value;		/* Timer value */
} scf_timer_t;

/*
 * scf_timer_check() return value
 */
#define	SCF_TIMER_NOT_EXEC	0
#define	SCF_TIMER_EXEC		1

/*
 * Timer value (ms)
 */
	/* SCF command busy timer value (10s) */
#define	SCF_TIMER_VALUE_DEVBUSY		10000
	/* SCF command completion timer value (60s) */
#define	SCF_TIMER_VALUE_CMDEND		60000
	/* SCF online timer value (10s) */
#define	SCF_TIMER_VALUE_ONLINE		10000
	/* Next receive timer value (20ms) */
#define	SCF_TIMER_VALUE_NEXTRCV		20
	/* DSCP interface timer value (60s) */
#define	SCF_TIMER_VALUE_DSCP_ACK	60000
	/* DSCP interface TxEND timer value (60s) */
#define	SCF_TIMER_VALUE_DSCP_END	60000
	/* DSCP interface busy timer value (2s) */
#define	SCF_TIMER_VALUE_DSCP_BUSY	2000
	/* DSCP interface callback timer value (20ms) */
#define	SCF_TIMER_VALUE_DSCP_CALLBACK	20
	/* DSCP INIT_REQ retry timer value (5ms) */
#define	SCF_TIMER_VALUE_DSCP_INIT	5000

/*
 * Timer value convert macro
 */
#define	SCF_MIL2MICRO(x)	((x) * 1000)
#define	SCF_SEC2MICRO(x)	((x) * 1000000)

/*
 * External function
 */
extern	void	scf_timer_init(void);
extern	void	scf_timer_start(int);
extern	void	scf_timer_stop(int);
extern	void	scf_timer_all_stop(void);
extern	int	scf_timer_check(int);
extern	uint32_t	scf_timer_value_get(int);
extern	void	scf_tout(void *);
extern	int	scf_timer_stop_collect(timeout_id_t *tmids, int size);
extern	void	scf_timer_untimeout(timeout_id_t *tmids, int size);

extern	scf_timer_t	scf_timer[SCF_TIMERCD_MAX];

/*
 * scf_shutdown_callb()/scf_panic_callb() timer value
 */
	/* SCF command end wait timer value (1s) */
#define	SCF_CALLB_CMDEND_TIME		1000
	/* SCF command end retry counter */
#define	SCF_CALLB_CMDEND_RCNT		4
	/* Buff full wait retry timer value (500ms) */
#define	SCF_CALLB_BUFF_FULL_RTIME	500
	/* Buff full retry counter */
#define	SCF_CALLB_BUFF_FULL_RCNT	10
	/* RCI busy wait retry timer value (3s) */
#define	SCF_CALLB_RCI_BUSY_RTIME	3000
	/* RCI busy retry counter */
#define	SCF_CALLB_RCI_BUSY_RCNT		3
	/* SCF online watch timer */
#define	SCF_CALLB_ONLINE_TIME		1000
	/* SCF online watch retry counter */
#define	SCF_CALLB_ONLINE_RCNT		20

#ifdef	__cplusplus
}
#endif

#endif	/* _SCFTIMER_H */
