/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _LDMA_H
#define	_LDMA_H

#include <libds.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The following definitions are part of the LDoms Agent specification.
 */

/* reply message types */
#define	LDMA_MSG_RESULT			0x8000	/* result message */
#define	LDMA_MSG_ERROR			0x8001	/* error message */

/* error codes for error messages */
#define	LDMA_MSGERR_FAIL		0x0000	/* request has failed */
#define	LDMA_MSGERR_INVALID		0x8001	/* request is invalid */
#define	LDMA_MSGERR_NOTSUP		0x8002	/* request is not supported */
#define	LDMA_MSGERR_DENY		0x8003	/* request is denied */

/*
 * LDoms Device Agent
 */
#define	LDMA_NAME_DEVICE		"agent-device"

#define	LDMA_MSGDEV_VALIDATE_PATH	0x01	/* validate path */
#define	LDMA_MSGDEV_VALIDATE_NIC	0x02	/* validate network interface */

#define	LDMA_DEVPATH_EXIST		0x01	/* path is accessible */
#define	LDMA_DEVPATH_OPENRW		0x02	/* path can be opened rw */
#define	LDMA_DEVPATH_OPENRO		0x04	/* path can be opened ro */

#define	LDMA_DEVPATH_TYPE_UNKNOWN	0x00	/* path points to unknown */
#define	LDMA_DEVPATH_TYPE_FILE		0x01    /* path points to a file */
#define	LDMA_DEVPATH_TYPE_DEVICE	0x02	/* path points to a device */

#define	LDMA_DEVNIC_EXIST		0x01	/* nic is accessible */

/*
 * LDoms System Agent
 */
#define	LDMA_NAME_SYSTEM		"agent-system"

#define	LDMA_MSGSYS_GET_SYSINFO		0x01	/* get system info request */
#define	LDMA_MSGSYS_GET_CHASSISNO	0x02	/* get chassis sno request */

/*
 * LDoms Direct IO Agent
 */
#define	LDMA_NAME_DIO		"agent-dio"

#define	MSGDIO_PCIDEV_INFO		0x1	/* pci device info request */
#define	MSGDIO_IOVDEV_INFO		0x2	/* sriov info request */
#define	MSGDIO_IOVDEV_CFG_VALIDATE	0x3	/* validate sriov devprops */
#define	MSGDIO_STATE_CHANGE_EVT		0x4	/* for config. change events */

/*
 * Size of the header of an agent message. This is the minimal size that
 * a message can have.
 */
#define	LDMA_MESSAGE_HEADER_SIZE	(sizeof (ldma_message_header_t))

/*
 * A constant message number is assigned to unsolicited messages.
 * This message number is not valid for request/response message pairs.
 */
#define	LDMA_MSGNUM_UNSOLICITED		((uint64_t)-1)


/*
 * Macro to compute the size of a message with a msg_data of size dlen.
 * The size of the msg_data field must be a multiple of 8-bytes so dlen
 * is roundup to an 8-bytes multiple.
 */
#define	LDMA_MESSAGE_SIZE(dlen)	(LDMA_MESSAGE_HEADER_SIZE + P2ROUNDUP(dlen, 8))

/*
 * Macro to compute the size of the msg_data field from the size of the message.
 */
#define	LDMA_MESSAGE_DLEN(msgsize)	((msgsize) - LDMA_MESSAGE_HEADER_SIZE)

/*
 * Handy macros for using the message and header structures.
 */
#define	LDMA_HDR2MSG(hdr)	((ldma_message_t *)(hdr))
#define	LDMA_HDR2DATA(hdr)	(LDMA_HDR2MSG(hdr)->msg_data)
#define	LDMA_MSG2HDR(msg)	((ldma_message_header_t *)(msg))

/* agent message header structure */
typedef struct ldma_message_header {
	uint64_t	msg_num; 	/* message number */
	uint32_t	msg_type;	/* message type */
	uint32_t	msg_info;	/* message info */
} ldma_message_header_t;

/* agent message structure */
typedef struct ldma_message {
	ldma_message_header_t	msg_hdr;	/* message header */
	char			msg_data[1];	/* message data */
} ldma_message_t;

/*
 * Additional structures and definition for the implementation.
 */
typedef enum ldma_request_status_t {
	LDMA_REQ_COMPLETED,		/* request was completed */
	LDMA_REQ_FAILED,		/* request has failed */
	LDMA_REQ_INVALID,		/* request is invalid */
	LDMA_REQ_NOTSUP,		/* request is not supported */
	LDMA_REQ_DENIED			/* request was denied */
} ldma_request_status_t;

typedef ldma_request_status_t (ldm_msg_func_t)(ds_ver_t *,
    ldma_message_header_t *, size_t, ldma_message_header_t **, size_t *);

#define	LDMA_CONTROL_DOMAIN_DHDL	0	/* id of the control domain */

/*
 * The domain service framework only allows connexion of a domain with
 * the control domain. So agents not running in the control domain can
 * only receive requests from the control domain. But, agents running
 * on the control can receive requests from any domain.
 *
 * For agents running in the control domain, the LDMA_MSGFLG_ACCESS_*
 * flags control whether messages sent by domains different from the
 * control domain should be processed or not.
 *
 * If a message handler is defined with LDMA_MSGFLG_ACCESS_CONTROL then
 * only messages sent by the control domain should be processed. Otherwise
 * if a message handler is defined with LDMA_MSGFLG_ACCESS_ANY then messages
 * sent by any domain can be processed.
 */
#define	LDMA_MSGFLG_ACCESS_CONTROL	0x00
#define	LDMA_MSGFLG_ACCESS_ANY		0x01

typedef struct ldma_msg_handler {
	uint32_t		msg_type; 	/* message type */
	uint32_t		msg_flags;	/* message flags */
	ldm_msg_func_t		*msg_handler;	/* message handler */
} ldma_msg_handler_t;

typedef struct ldma_agent_info {
	char			*name;		/* agent name */
	ds_ver_t		*vers;		/* supported versions */
	int			nvers;		/* number of versions */
	void 			(*init)(void);
	void			(*reg_cb)(ds_ver_t *ver,
				    ds_domain_hdl_t dhdl, ds_hdl_t hdl);
	void			(*unreg_cb)(ds_hdl_t hdl);
	ldma_msg_handler_t	*handlers;	/* message handlers */
	int			nhandlers;	/* number of handlers */
} ldma_agent_info_t;

/*
 * Macros to check negotiated agent protocol version
 */
#define	VER_EQ(ver, maj, min)	\
	((ver)->major == (maj) && (ver)->minor == (min))

#define	VER_GTEQ(ver, maj, min)	\
	(((ver)->major > (maj)) ||	\
	    ((ver)->major == (maj) && (ver)->minor >= (min)))

/*
 * Helper functions for the daemon and agents.
 */
ldma_message_header_t *ldma_alloc_msg(uint64_t msg_num,
    uint32_t msg_type, size_t msg_dlen);
int ldma_send_unsolicited_msg(ds_hdl_t hdl,
    ldma_message_header_t *msg, size_t len);

/* function to allocate a result message */
ldma_message_header_t *ldma_alloc_result_msg(ldma_message_header_t *, size_t);

/* functions to log messages */
void ldma_err(char *module, char *fmt, ...);
void ldma_info(char *module, char *fmt, ...);
void ldma_dbg(char *module, char *fmt, ...);

/*
 * Macros to log messages. Each module/file using these macros should define
 * LDMA_MODULE as the name under which messages are logged. For a given agent,
 * LDMA_MODULE should be set to the name of the agent.
 */
#define	LDMA_ERR(...)	ldma_err(LDMA_MODULE, __VA_ARGS__)
#define	LDMA_INFO(...)	ldma_info(LDMA_MODULE, __VA_ARGS__)
#define	LDMA_DBG(...)	ldma_dbg(LDMA_MODULE, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* _LDMA_H */
