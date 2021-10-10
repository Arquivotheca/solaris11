/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LLC2_CONF_H
#define	_LLC2_CONF_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct llc2_conf_entry {
	int			ppa;
	FILE			*fp;
	struct llc2_conf_entry	*next_entry_p;
} llc2_conf_entry_t;

typedef struct llc2_conf_param {
	int llc2_on;		/* On/Off on this device */
	int dev_loopback;	/* Device support loopback or not */
	int time_intrvl;	/* Timer Multiplier */
	int ack_timer;		/* Ack Timer */
	int rsp_timer;		/* Response Timer */
	int poll_timer;		/* Poll Timer */
	int rej_timer;		/* Reject Timer */
	int rem_busy_timer;	/* Remote Busy Timer */
	int inact_timer;	/* Inactivity Timer */
	int max_retry;		/* Maximum Retry Value */
	int xmit_win;		/* Transmit Window Size */
	int recv_win;		/* Receive Window Size */
} llc2_conf_param_t;

extern llc2_conf_entry_t *conf_head;

/* Add an entry to the list. */
#define	ADD_ENTRY(head, new_entry) \
{ \
	(new_entry)->next_entry_p = (head); \
	(head) = (new_entry); \
}

/* Remove and free an entry from the list. */
#define	RM_CONF_ENTRY(prev_confp, confp) \
{ \
	(void) fclose((confp)->fp); \
	if ((prev_confp) == NULL) { \
		conf_head = conf_head->next_entry_p; \
		free(confp); \
	} else { \
		(prev_confp)->next_entry_p = (confp)->next_entry_p; \
		free(confp); \
	} \
}

#define	LLC2_NAME	"llc2"
#define	LLC2_NAME_LEN	4
#define	LLC2_CONF_DIR	"/etc/llc2/default/"

/* Default values of LLC2 parameters. */
#define	TIME_INTRVL_DEF		0
#define	ACK_TIMER_DEF		2
#define	RSP_TIMER_DEF		2
#define	POLL_TIMER_DEF		4
#define	REJ_TIMER_DEF		6
#define	REM_BUSY_TIMER_DEF	8
#define	INACT_TIMER_DEF		30
#define	MAX_RETRY_DEF		6
#define	XMIT_WIN_DEF		14
#define	RECV_WIN_DEF		14
#define	LLC2_ON_DEF		1

/* Function return values. */
#define	LLC2_OK			0
#define	LLC2_FAIL		-1

extern FILE *open_conf(int);
extern int add_conf(void);
extern void print_conf_entry(void);
extern int read_conf_file(FILE *, char *, int *, llc2_conf_param_t *);
extern int create_conf(char *, int, llc2_conf_param_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _LLC2_CONF_H */
