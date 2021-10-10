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

#ifndef	_SYS_VBIOS_H
#define	_SYS_VBIOS_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/door.h>
#include <sys/fbio.h>
#include <sys/ksynch.h>

#define	VBIOS_VGA_CALL		(0)
#define	VBIOS_VESA_CALL		(1)

#define	VBIOS_TYPE_MAX		VBIOS_VESA_CALL

/*
 * Generic supported commands.
 * These commands are supported for each type category (VESA and VGA at the
 * moment).
 */
#define	VBIOS_GENERIC		('G' << 8)

#define	VBIOS_CMD_SETMODE	(VBIOS_GENERIC | 1)

#define	VBIOS_CMD_MAX		VBIOS_CMD_SETMODE

/*
 * Flags we may use with VESA setmode. This is a subset of the available flags
 * (f.e. no CRTC SET) which reflects the kind of use we are supposed to do.
 */
#define	VBIOS_VESASET_KEEPSCR	0x8000
#define	VBIOS_VESASET_LINFB	0x4000

union vbios_mode {
	struct vbios_vesa_mode {
		uint16_t	val;
		uint16_t	flags;
	} vesa;
	struct vbios_vga_mode {
		uint8_t		val;
	} vga;
} __attribute__((packed));

typedef union vbios_mode vbios_mode_t;

struct vbios_cmd_request {
	uint32_t	cmd;
	uint8_t		type;
	union {
		vbios_mode_t	mode;
	} args;
} __attribute__((packed));

typedef struct vbios_cmd_request vbios_cmd_req_t;

struct vbios_call_results {
	int		ret;
	uint32_t	size;
	char		data[1];
} __attribute__((packed));

typedef struct vbios_call_results vbios_call_results_t;

typedef struct vbios_cmd_response {
	int			call_ret;
	int			call_errtype;
	vbios_call_results_t	*call_results;
	uint32_t		results_size;
} vbios_cmd_reply_t;

/*
 * Return Values for call_results->ret.
 * These values are used when call_ret == 0 (which means that the door call
 * to the user land daemon was successful.
 */
#define	VBIOS_VCALL_SUCCESS	(0)
/* Generic failure. (AH == 01h) */
#define	VBIOS_VCALL_EFAIL	(1)
/* Function not supported in the current hw configuration. (AH == 02h) */
#define	VBIOS_VCALL_EHWNOTSUP	(2)
/* Function call invalid in current video mode (AH == 03h) */
#define	VBIOS_VCALL_EINVAL	(3)
/* Function is not supported. (AL != 0x4f) */
#define	VBIOS_VCALL_ENOTSUP	(4)

/*
 * Kernel land timeout.  The kernel land counterpart sets a timeout of
 * VBIOS_TIMEOUT seconds after which it discards the user land request.
 * This timeout prevents the kernel path from waiting indefinitely if the
 * user lan daemon hangs for some reason (ex. blocked by a SIGSTOP).
 * Once the timeout expires, the kernel sends a SIGUSR2 to the daemon and
 * discards the request.
 */
#define	VBIOS_TIMEOUT		(4)

#ifdef  _KERNEL

/*
 * Return values for call_ret.
 * VBIOS_STUCK gets returned if the kernel timeouts waiting for call
 * results.
 */
#define	VBIOS_SUCCESS		(0)
#define	VBIOS_FAIL		(-1)
#define	VBIOS_STUCK		(-2)

/* Function prototypes. */
int vbios_register_handle(fbio_load_handle_t *);
vbios_cmd_reply_t *vbios_exec_cmd(vbios_cmd_req_t *);
void vbios_free_reply(vbios_cmd_reply_t *);

#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VBIOS_H */
