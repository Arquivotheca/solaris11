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

#ifndef	_VBIOSD_H
#define	_VBIOSD_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/vbios.h>
#include <syslog.h>
#include <paths.h>

extern boolean_t	vbiosd_is_daemon;
extern boolean_t	vbiosd_debug_report;
extern boolean_t	vbiosd_accept_uland;
extern int		vbiosd_console_fd;

#define	MYNAME		"vbiosd"

#define	VBIOS_DOOR_PATH		_PATH_SYSVOL "/vbiosd.door"
#define	VBIOSD_LOCK_FILE	_PATH_SYSVOL "/vbiosd.lock"
#define	FB_DEVICE		"/dev/fb"

#define	LOG_ERRNO	B_TRUE
#define	NO_ERRNO	B_FALSE

#define	vbiosd_error(le, ...) vbiosd_print(LOG_ERR, le, __VA_ARGS__)
#define	vbiosd_debug(le, ...) vbiosd_print(LOG_DEBUG, le,  __VA_ARGS__)
#define	vbiosd_warn(le, ...) vbiosd_print(LOG_WARNING, le, __VA_ARGS__)
#define	vbiosd_abort(le, ...) do { vbiosd_print(LOG_ERR, le, __VA_ARGS__); \
			vbiosd_exit(EXIT_FAILURE); } while (0)

#define	VBIOSD_SUCCESS	(0)
#define	VBIOSD_ERROR	(1)

/*
 * Larger structure to hold both the call result and the information
 * necessary to deal with it correctly.
 */
typedef struct vbiosd_res_blob {
	vbios_call_results_t	*res;
	boolean_t		to_free;
	uint32_t		res_size;
} vbiosd_res_blob_t;

struct vbiosd_ctx {
	uint32_t	eax;
	uint32_t	ebx;
	uint32_t	ecx;
	uint32_t	edx;
	uint32_t	edi;
	uint32_t	esi;
	uint32_t	ebp;
	uint32_t	esp;
	uint32_t	eip;
	uint32_t	eflags;
	uint16_t	cs;
	uint16_t	ds;
	uint16_t	es;
	uint16_t	fs;
	uint16_t	gs;
	uint16_t	ss;
};

/* Global function prototypes. */
int vbiosd_blob_res_alloc(vbiosd_res_blob_t *, uint32_t);
void vbiosd_blob_res_free(vbiosd_res_blob_t *blob);
void vbiosd_setup_sighandler(int, int, void (*)());
int vbiosd_upcall_setup();
void vbiosd_handle_vesa_call(vbios_cmd_req_t *, vbiosd_res_blob_t *);
void vbiosd_handle_vga_call(vbios_cmd_req_t *, vbiosd_res_blob_t *);
void vbiosd_setup_log();
void vbiosd_print(int, boolean_t, const char *, ...);
int vbiosd_exec(struct vbiosd_ctx *);
void vbiosd_release_door();
int vbiosd_x86emu_setup();
int vbiosd_do_int10(struct vbiosd_ctx *);


#ifdef	__cplusplus
}
#endif

#endif	/* _VBIOSD_H */
