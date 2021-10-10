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
/*
 * This file implements the door communication mechanism between our server
 * and the kernel client. The door server is attached to the
 * /etc/svc/volatile/vbiosd.door (defined in vbiosd.h) and the door descriptor
 * is passed back to the kernel through an ioctl to /dev/fb. (The choice of
 * /dev/fb reflects the current only consumer of these interfaces).
 *
 * An handler for SIGUSR2 is set up to catch kernel 'timeout' requests.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysi86.h>
#include <sys/psw.h>
#include <sys/stat.h>
#include <sys/fbio.h>
#include <stdio.h>
#include <stdlib.h>
#include <door.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <alloca.h>

#include "vbiosd.h"

static vbios_call_results_t	error_res;
static int			upcall_door = -1;

void
vbiosd_release_door()
{
	if (upcall_door > 0) {
		(void) fdetach(VBIOS_DOOR_PATH);
		(void) door_revoke(upcall_door);
		upcall_door = -1;
	}
}

int
vbiosd_exec(struct vbiosd_ctx *ctx)
{

	if (vbiosd_do_int10(ctx)) {
		vbiosd_error(NO_ERRNO, "error executing int 0x10");
		return (VBIOSD_ERROR);
	}
	return (VBIOSD_SUCCESS);
}

/* ARGSUSED */
static void
vbiosd_sigusr2_hndlr(int sig)
{
	vbiosd_debug(NO_ERRNO, "getting killed by SIGUSR2");
	(void) door_return(NULL, 0, NULL, 0);
	pthread_exit(0);
}

/*
 * Handle a received vbios command. Note that blob->res has been set by the
 * caller to 'error_res' so we need to modify it only if we get to execute the
 * BIOS call.
 */
static void
vbiosd_handle_command(vbios_cmd_req_t *cmd, vbiosd_res_blob_t *blob) {
	uint16_t		type = cmd->type;

	switch (type) {
	case VBIOS_VGA_CALL:
		vbiosd_debug(NO_ERRNO, "VGA command received");
		vbiosd_handle_vga_call(cmd, blob);
		break;
	case VBIOS_VESA_CALL:
		vbiosd_debug(NO_ERRNO, "VESA command received");
		vbiosd_handle_vesa_call(cmd, blob);
		break;
	default:
		vbiosd_warn(NO_ERRNO, "unknown command received");
	}
}

/*
 * vbiosd_setup_timeout().
 * Register the handler for SIGUSR2, the signal that the kernel uses to kill
 * an hanging servicing thread.
 */
static void
vbiosd_setup_timeout()
{
	/* Setup the handler. */
	vbiosd_setup_sighandler(SIGUSR2, 0, vbiosd_sigusr2_hndlr);
}

/*
 * Kernel upcall servicing entrypoint.
 * The kernel counterpart is responsible of all the queueing and serializing
 * of requests.
 */
/* ARGSUSED */
static void
vbiosd_handle_upcall(void *cookie, char *args, size_t arg_size,
    door_desc_t *dp, uint_t n_desc)
{
	vbios_cmd_req_t		*cmd;
	vbiosd_res_blob_t 	blob;
	ucred_t			*cred = NULL;

	vbiosd_debug(NO_ERRNO, "VBIOS call handler called");

	/*
	 * Setup the timeout mechanism. We unblock SIGUSR2 so that the kernel
	 * has a way to kill us if we get stuck.
	 */
	vbiosd_debug(NO_ERRNO, "setting up timeout mechanism");
	vbiosd_setup_timeout();

	/* Initialize the return value to 'error'. */
	blob.res = &error_res;
	blob.to_free = B_FALSE;
	blob.res_size = sizeof (error_res);

	/*
	 * Unless we are accepting calls from the userland (hidden debug mode),
	 * make sure that is really the kernel who is calling us.
	 */
	if (door_ucred(&cred) == -1) {
		vbiosd_error(LOG_ERRNO, "getting caller credentials failed");
		goto out;
	} else {
		pid_t	pid = ucred_getpid(cred);
		if (vbiosd_accept_uland == B_FALSE && pid != 0) {
			vbiosd_error(NO_ERRNO, "only the kernel is allowed to"
			    " call this server, not pid:%d", pid);
			goto out;
		}
	}

	/* No empty requests allowed. */
	if (args == NULL) {
		vbiosd_debug(NO_ERRNO, "kernel sent an empty request");
		goto out;
	}
	cmd = (vbios_cmd_req_t *)args;

	/* We need IOPL=3 to allow our user land task to access all hw ports. */
	if (sysi86(SI86V86, V86SC_IOPL, PS_IOPL) < 0) {
		vbiosd_error(LOG_ERRNO, "unable to set IO Privilege Level");
		/* We can't proceed without. */
		goto out;
	}

	/* Parse and execute the received command. */
	vbiosd_handle_command(cmd, &blob);
out:
	vbiosd_debug(NO_ERRNO, "Return buffer size is %d, return value %d",
	    blob.res_size, blob.res->ret);

	if (blob.to_free == B_TRUE) {
		int	size = blob.res_size;
		char	*dest;

		dest = alloca(size);
		(void) memcpy(dest, blob.res, size);
		vbiosd_blob_res_free(&blob);
		blob.res = (vbios_call_results_t *)dest;
	}

	(void) door_return((void *)blob.res, blob.res_size, NULL, 0);
	(void) door_return(NULL, 0, NULL, 0);
}

/*
 * Set up the door server to which the kernel sends requests.
 */
#define	DOOR_PATH_MASK (O_CREAT | O_NONBLOCK | O_NOFOLLOW | O_NOLINKS | O_RDWR)

int
vbiosd_upcall_setup()
{
	int			fd = -1, device_fd = -1;
	int			upcall_door;
	fbio_load_handle_t	ldoor = { 0 };

	errno = 0;

	/* Remove the upcall door file, if a stale one is present. */
	vbiosd_debug(NO_ERRNO, "setting up user land door and attaching it to"
	    " %s", VBIOS_DOOR_PATH);
	(void) unlink(VBIOS_DOOR_PATH);

	/*
	 * Check if the path we plan to attach our door exists. If not, create
	 * it.
	 */
	fd = open(VBIOS_DOOR_PATH, DOOR_PATH_MASK, 0600);
	if (fd == -1) {
		vbiosd_debug(LOG_ERRNO, "unable to open %s", VBIOS_DOOR_PATH);
		goto out;
	}
	(void) close(fd);

	/* Create the door server. */
	upcall_door = door_create(vbiosd_handle_upcall, NULL,
	    DOOR_REFUSE_DESC | DOOR_NO_CANCEL);
	if (upcall_door == -1) {
		vbiosd_debug(LOG_ERRNO, "door_create() failed");
		goto out;
	}

	/* Populate the 'default' and 'timeout' error returning structure. */
	error_res.ret = VBIOS_VCALL_EFAIL;
	error_res.size = 0;

	(void) fdetach(VBIOS_DOOR_PATH);
	/* Attach the door file descriptor to our filesystem path. */
	while (fattach(upcall_door, VBIOS_DOOR_PATH) != 0) {
		if (errno == EBUSY)
			continue;
		vbiosd_debug(LOG_ERRNO, "Unable to attach the door descriptor"
		    " to %s", VBIOS_DOOR_PATH);
		goto out_revoke;
	}

	/*
	 * Notify the kernel that we are up and running. We send the door id
	 * via an IOCTL to /dev/fb. Currently that works fine because we
	 * are tightly linked to the video card driver: in the future we may
	 * want to have a separated kernel-exported device.
	 */
	device_fd = open(FB_DEVICE, O_RDONLY);
	if (device_fd == -1) {
		vbiosd_debug(LOG_ERRNO, "open of %s failed", FB_DEVICE);
		goto out_detach;
	}

	/*
	 * Register the door with the kernel vbios subsystem. If the -u
	 * debugging option is turned on, do not consider a failure fatal.
	 */
	ldoor.id = upcall_door;
	if (ioctl(device_fd, FBIOLOADHNDL, &ldoor) == -1) {
		vbiosd_debug(LOG_ERRNO, "ioctl failed");
		if (vbiosd_accept_uland == B_FALSE)
			goto out_detach;
	}

	vbiosd_debug(NO_ERRNO, "user land door server is up and running");
	return (VBIOSD_SUCCESS);
out_detach:
	(void) fdetach(VBIOS_DOOR_PATH);
out_revoke:
	(void) door_revoke(upcall_door);
out:
	return (VBIOSD_ERROR);

}
