/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * BSD 3 Clause License
 *
 * Copyright (c) 2007, The Storage Networking Industry Association.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 	- Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *
 * 	- Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in
 *	  the documentation and/or other materials provided with the
 *	  distribution.
 *
 *	- Neither the name of The Storage Networking Industry Association (SNIA)
 *	  nor the names of its contributors may be used to endorse or promote
 *	  products derived from this software without specific prior written
 *	  permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/* Copyright (c) 2007, The Storage Networking Industry Association. */
/* Copyright (c) 1996, 1997 PDC, Network Appliance. All Rights Reserved */

#include <sys/param.h>
#include <fcntl.h>
#include <sys/mtio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "ndmpd_common.h"
#include "ndmpd.h"

static void tape_open_send_reply(ndmp_connection_t *connection, int err);
static void unbuffered_read(ndmpd_session_t *session, char *buf, long wanted,
    ndmp_tape_read_reply *reply);
static boolean_t validmode(int mode);
static void common_tape_open(ndmp_connection_t *connection, char *devname,
    int ndmpmode);
static void common_tape_close(ndmp_connection_t *connection);

/*
 * Configurable delay & time when the tape is
 * busy during opening the tape.
 */
int ndmp_tape_open_retries = 5;
int ndmp_tape_open_delay = 1000;

/*
 * ************************************************************************
 * NDMP V2 HANDLERS
 * ************************************************************************
 */

/*
 * ndmpd_tape_open_v2
 *
 * This handler opens the specified tape device.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
void
ndmpd_tape_open_v2(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_open_request_v2 *request = (ndmp_tape_open_request_v2 *) body;
	ndmpd_session_t *session = ndmp_get_client_data(connection);
	char adptnm[SCSI_MAX_NAME];
	int mode;
	int sid, lun;
	int err;
	scsi_adapter_t *sa;
	int devid;

	err = NDMP_NO_ERR;

	if (session->ns_tape.td_fd != -1 || session->ns_scsi.sd_is_open != -1) {
		NDMP_LOG(LOG_INFO,
		    "Connection already has a tape or scsi device open");
		err = NDMP_DEVICE_OPENED_ERR;
	} else if (request->mode != NDMP_TAPE_READ_MODE &&
	    request->mode != NDMP_TAPE_WRITE_MODE &&
	    request->mode != NDMP_TAPE_RAW1_MODE) {
		err = NDMP_ILLEGAL_ARGS_ERR;
	}

	if ((sa = scsi_get_adapter(0)) != NULL) {
		(void) strlcpy(adptnm, request->device.name, SCSI_MAX_NAME-2);
		adptnm[SCSI_MAX_NAME-1] = '\0';
		sid = lun = -1;
	}
	/* try to get the scsi id etc.... */
	if (sa) {
		scsi_find_sid_lun(sa, request->device.name, &sid, &lun);
		if (ndmp_open_list_find(request->device.name, sid, lun) == 0 &&
		    (devid = tape_open(request->device.name,
		    O_RDWR | O_NDELAY)) < 0) {
			NDMP_LOG(LOG_ERR, "Failed to open device %s: %m.",
			    request->device.name);
			err = NDMP_NO_DEVICE_ERR;
		}
		else
			(void) close(devid);
	} else {
		NDMP_LOG(LOG_ERR, "%s: No such tape device.",
		    request->device.name);
		err = NDMP_NO_DEVICE_ERR;
	}
	if (err != NDMP_NO_ERR) {
		tape_open_send_reply(connection, err);
		return;
	}

	switch (ndmp_open_list_add(connection, adptnm, sid, lun, devid)) {
	case 0:
		err = NDMP_NO_ERR;
		break;
	case EBUSY:
		err = NDMP_DEVICE_BUSY_ERR;
		break;
	case ENOMEM:
		err = NDMP_NO_MEM_ERR;
		break;
	default:
		err = NDMP_IO_ERR;
	}
	if (err != NDMP_NO_ERR) {
		tape_open_send_reply(connection, err);
		return;
	}

	/*
	 * According to Connectathon 2001, the 0x7fffffff is a secret
	 * code between "Workstartion Solutions" and * net_app.
	 * If mode is set to this value, tape_open() won't fail if
	 * the tape device is not ready.
	 */
	if (request->mode != NDMP_TAPE_RAW1_MODE &&
	    !is_tape_unit_ready(adptnm, 0)) {
		(void) ndmp_open_list_del(adptnm, sid, lun);
		tape_open_send_reply(connection, NDMP_NO_TAPE_LOADED_ERR);
		return;
	}

	mode = (request->mode == NDMP_TAPE_READ_MODE) ? O_RDONLY : O_RDWR;
	mode |= O_NDELAY;
	if ((session->ns_tape.td_fd = open(request->device.name, mode)) < 0) {
			NDMP_LOG(LOG_ERR, "Failed to open tape device %s: %m.",
			    request->device.name);
			switch (errno) {
			case EACCES:
				err = NDMP_WRITE_PROTECT_ERR;
				break;
			case ENXIO:
			case ENOENT:
				err = NDMP_NO_DEVICE_ERR;
				break;
			case EBUSY:
				err = NDMP_DEVICE_BUSY_ERR;
				break;
			default:
				err = NDMP_IO_ERR;
			}

			(void) ndmp_open_list_del(adptnm, sid, lun);
			tape_open_send_reply(connection, err);
			return;
		}

	session->ns_tape.td_mode = request->mode;
	session->ns_tape.td_sid = sid;
	session->ns_tape.td_lun = lun;
	(void) strlcpy(session->ns_tape.td_adapter_name, adptnm, SCSI_MAX_NAME);
	session->ns_tape.td_record_count = 0;
	session->ns_tape.td_eom_seen = FALSE;

	tape_open_send_reply(connection, NDMP_NO_ERR);
}


/*
 * ndmpd_tape_close_v2
 *
 * This handler closes the currently open tape device.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
/*ARGSUSED*/
void
ndmpd_tape_close_v2(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_close_reply reply;
	ndmpd_session_t *session = ndmp_get_client_data(connection);

	if (session->ns_tape.td_fd == -1) {
		NDMP_LOG(LOG_ERR, "Tape device is not open.");
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_close reply");
		return;
	}
	common_tape_close(connection);

}

/*
 * ndmpd_tape_get_state_v2
 *
 * This handler handles the tape_get_state request.
 * Status information for the currently open tape device is returned.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
/*ARGSUSED*/
void
ndmpd_tape_get_state_v2(ndmp_connection_t *connection, void *body)

{
	ndmp_tape_get_state_reply_v2 reply;
	ndmpd_session_t *session = ndmp_get_client_data(connection);
	struct mtget mtstatus;
	struct mtdrivetype_request dtpr;
	struct mtdrivetype dtp;

	if (session->ns_tape.td_fd == -1) {
		NDMP_LOG(LOG_ERR, "Tape device is not open.");
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_get_state reply");
		return;
	}

	if (ioctl(session->ns_tape.td_fd, MTIOCGET, &mtstatus) < 0) {
		NDMP_LOG(LOG_ERR, "Failed to get status from tape: %m.");
		reply.error = NDMP_IO_ERR;
		ndmp_send_reply(connection, (void *)&reply,
		    "sending tape_get_state reply");
		return;
	}

	dtpr.size = sizeof (struct mtdrivetype);
	dtpr.mtdtp = &dtp;
	if (ioctl(session->ns_tape.td_fd, MTIOCGETDRIVETYPE, &dtpr) == -1) {
		NDMP_LOG(LOG_ERR,
		    "Failed to get drive type information from tape: %m.");
		reply.error = NDMP_IO_ERR;
		ndmp_send_reply(connection, (void *)&reply,
		    "sending tape_get_state reply");
		return;
	}

	reply.flags = 0;

	reply.file_num = mtstatus.mt_fileno;
	reply.soft_errors = 0;
	reply.block_size = dtp.bsize;
	if (dtp.bsize == 0)
		reply.blockno = mtstatus.mt_blkno;
	else
		reply.blockno = mtstatus.mt_blkno *
		    (session->ns_mover.md_record_size / dtp.bsize);

	reply.soft_errors = 0;
	reply.total_space = long_long_to_quad(0);	/* not supported */
	reply.space_remain = long_long_to_quad(0);	/* not supported */

	reply.error = NDMP_NO_ERR;
	ndmp_send_reply(connection, (void *) &reply,
	    "sending tape_get_state reply");
}


/*
 * ndmpd_tape_mtio_v2
 *
 * This handler handles tape_mtio requests.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
void
ndmpd_tape_mtio_v2(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_mtio_request *request = (ndmp_tape_mtio_request *) body;
	ndmp_tape_mtio_reply reply;
	ndmpd_session_t *session = ndmp_get_client_data(connection);

	struct mtop tapeop;
	struct mtget mtstatus;
	int retry = 0;
	int rc;

	reply.resid_count = 0;

	if (session->ns_tape.td_fd == -1) {
		NDMP_LOG(LOG_ERR, "Tape device is not open.");
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_mtio reply");
		return;
	}

	reply.error = NDMP_NO_ERR;
	switch (request->tape_op) {
	case NDMP_MTIO_FSF:
		tapeop.mt_op = MTFSF;
		break;
	case NDMP_MTIO_BSF:
		tapeop.mt_op = MTBSF;
		break;
	case NDMP_MTIO_FSR:
		tapeop.mt_op = MTFSR;
		break;
	case NDMP_MTIO_BSR:
		tapeop.mt_op = MTBSR;
		break;
	case NDMP_MTIO_REW:
		tapeop.mt_op = MTREW;
		break;
	case NDMP_MTIO_EOF:
		if (session->ns_tape.td_mode == NDMP_TAPE_READ_MODE)
			reply.error = NDMP_PERMISSION_ERR;
		tapeop.mt_op = MTWEOF;
		break;
	case NDMP_MTIO_OFF:
		tapeop.mt_op = MTOFFL;
		break;

	case NDMP_MTIO_TUR: /* test unit ready */

		if (is_tape_unit_ready(session->ns_tape.td_adapter_name,
		    session->ns_tape.td_fd) == 0)
			/* tape not ready ? */
			reply.error = NDMP_NO_TAPE_LOADED_ERR;
		break;

	default:
		reply.error = NDMP_ILLEGAL_ARGS_ERR;
	}

	if (reply.error == NDMP_NO_ERR && request->tape_op != NDMP_MTIO_TUR) {
		tapeop.mt_count = request->count;

		do {
			NS_UPD(twait, trun);
			rc = ioctl(session->ns_tape.td_fd, MTIOCTOP, &tapeop);
			NS_UPD(trun, twait);
		} while (rc < 0 && errno == EIO &&
		    retry++ < 5);

		/*
		 * Ignore I/O errors since these usually are the result of
		 * attempting to position past the beginning or end of the tape.
		 * The residual count will be returned and can be used to
		 * determine that the call was not completely successful.
		 */
		if (rc < 0) {
			NDMP_LOG(LOG_ERR,
			    "Failed to send command to tape: %m.");

			/* MTWEOF doesnt have residual count */
			if (tapeop.mt_op == MTWEOF)
				reply.error = NDMP_IO_ERR;
			else
				reply.error = NDMP_NO_ERR;
			reply.resid_count = tapeop.mt_count;
			ndmp_send_reply(connection, (void *)&reply,
			    "sending tape_mtio reply");
			return;
		}

		if (request->tape_op != NDMP_MTIO_REW &&
		    request->tape_op != NDMP_MTIO_OFF) {
			if (ioctl(session->ns_tape.td_fd, MTIOCGET,
			    &mtstatus) < 0) {
				NDMP_LOG(LOG_ERR,
				    "Failed to send command to tape: %m.");
				reply.error = NDMP_IO_ERR;
				ndmp_send_reply(connection, (void *)&reply,
				    "sending tape_mtio reply");

				return;
			}

			reply.resid_count = labs(mtstatus.mt_resid);
		}
	}

	ndmp_send_reply(connection, (void *) &reply, "sending tape_mtio reply");
}


/*
 * ndmpd_tape_write_v2
 *
 * This handler handles tape_write requests.
 * This interface is a non-buffered interface. Each write request
 * maps directly to a write to the tape device. It is the responsibility
 * of the NDMP client to pad the data to the desired record size.
 * It is the responsibility of the NDMP client to ensure that the
 * length is a multiple of the tape block size if the tape device
 * is in fixed block mode.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
void
ndmpd_tape_write_v2(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_write_request *request = (ndmp_tape_write_request *) body;
	ndmp_tape_write_reply reply;
	ndmpd_session_t *session = ndmp_get_client_data(connection);
	ssize_t n;

	reply.count = 0;

	if (session->ns_tape.td_fd == -1) {
		NDMP_LOG(LOG_ERR, "Tape device is not open.");
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_write reply");
		return;
	}
	if (session->ns_tape.td_mode == NDMP_TAPE_READ_MODE) {
		NDMP_LOG(LOG_INFO, "Tape device opened in read-only mode");
		reply.error = NDMP_PERMISSION_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_write reply");
		return;
	}
	if (request->data_out.data_out_len == 0) {
		reply.error = NDMP_NO_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_write reply");
		return;
	}

	if (session->ns_tape.td_eom_seen) {
		/*
		 * Refer to the comment at the top of this file for
		 * Mammoth2 tape drives.
		 */
		NDMP_LOG(LOG_DEBUG, "EOM detected");
		ndmpd_write_eom(session->ns_tape.td_fd);

		session->ns_tape.td_eom_seen = FALSE;
		reply.error = NDMP_EOM_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_write reply");
		return;
	}

	n = write(session->ns_tape.td_fd, request->data_out.data_out_val,
	    request->data_out.data_out_len);
	if (n >= 0) {
		session->ns_tape.td_write = 1;
		NS_ADD(wtape, n);
	}
	if (n == 0) {
		reply.error = NDMP_EOM_ERR;
		session->ns_tape.td_eom_seen = FALSE;
	} else if (n < 0) {
		NDMP_LOG(LOG_ERR, "Tape write error: %m.");
		reply.error = NDMP_IO_ERR;
	} else {
		reply.count = n;
		reply.error = NDMP_NO_ERR;

		/*
		 * a logical end of tape will return number of bytes written
		 * less than rquested, and one more request to write will
		 * give 0, and then no-space
		 */
		if (n < request->data_out.data_out_len) {
			NDMP_LOG(LOG_DEBUG, "Reached LEOT: %d < %d",
			    n, request->data_out.data_out_len);
			session->ns_tape.td_eom_seen = TRUE;
		} else {
			session->ns_tape.td_eom_seen = FALSE;
		}
	}
	ndmp_send_reply(connection, &reply,
	    "sending tape_write reply");
}


/*
 * ndmpd_tape_read_v2
 *
 * This handler handles tape_read requests.
 * This interface is a non-buffered interface. Each read request
 * maps directly to a read to the tape device. It is the responsibility
 * of the NDMP client to issue read requests with a length that is at
 * least as large as the record size used write the tape. The tape driver
 * always reads a full record. Data is discarded if the read request is
 * smaller than the record size.
 * It is the responsibility of the NDMP client to ensure that the
 * length is a multiple of the tape block size if the tape device
 * is in fixed block mode.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
void
ndmpd_tape_read_v2(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_read_request *request = (ndmp_tape_read_request *) body;
	ndmp_tape_read_reply reply;
	ndmpd_session_t *session = ndmp_get_client_data(connection);
	char *buf;

	reply.data_in.data_in_len = 0;

	if (session->ns_tape.td_fd == -1) {
		NDMP_LOG(LOG_ERR, "Tape device is not open.");
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(connection, (void *)&reply,
		    "sending tape_read reply");
		return;
	}
	if (request->count == 0) {
		reply.error = NDMP_NO_ERR;
		ndmp_send_reply(connection, (void *)&reply,
		    "sending tape_read reply");
		return;
	}
	if ((buf = ndmp_malloc(request->count)) == 0) {
		reply.error = NDMP_NO_MEM_ERR;
		ndmp_send_reply(connection, (void *)&reply,
		    "sending tape_read reply");
		return;
	}

	session->ns_tape.td_eom_seen = FALSE;

	unbuffered_read(session, buf, request->count, &reply);

	ndmp_send_reply(connection, (void *) &reply, "sending tape_read reply");
	(void) free(buf);
}


/*
 * ndmpd_tape_execute_cdb_v2
 *
 * This handler handles tape_execute_cdb requests.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
void
ndmpd_tape_execute_cdb_v2(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_execute_cdb_request *request;
	ndmp_tape_execute_cdb_reply reply;
	ndmpd_session_t *session = ndmp_get_client_data(connection);

	request = (ndmp_tape_execute_cdb_request *) body;

	if (session->ns_tape.td_fd == -1) {
		(void) memset((void *) &reply, 0, sizeof (reply));

		NDMP_LOG(LOG_ERR, "Tape device is not open.");
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_execute_cdb reply");
	} else {
		session->ns_tape.td_eom_seen = FALSE;
		ndmp_execute_cdb(session, session->ns_tape.td_adapter_name,
		    session->ns_tape.td_sid, session->ns_tape.td_lun,
		    (ndmp_execute_cdb_request *)request);
	}
}


/*
 * ************************************************************************
 * NDMP V3 HANDLERS
 * ************************************************************************
 */

/*
 * ndmpd_tape_open_v3
 *
 * This handler opens the specified tape device.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
void
ndmpd_tape_open_v3(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_open_request_v3 *request = (ndmp_tape_open_request_v3 *)body;

	common_tape_open(connection, request->device, request->mode);
}


/*
 * ndmpd_tape_get_state_v3
 *
 * This handler handles the ndmp_tape_get_state_request.
 * Status information for the currently open tape device is returned.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
/*ARGSUSED*/
void
ndmpd_tape_get_state_v3(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_get_state_reply_v3 reply;
	ndmpd_session_t *session = ndmp_get_client_data(connection);
	struct mtdrivetype_request dtpr;
	struct mtdrivetype dtp;
	struct mtget mtstatus;

	if (session->ns_tape.td_fd == -1) {
		NDMP_LOG(LOG_ERR, "Tape device is not open.");
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_get_state reply");
		return;
	}

	if (ioctl(session->ns_tape.td_fd, MTIOCGET, &mtstatus) == -1) {
		NDMP_LOG(LOG_ERR, "Failed to get status from tape: %m.");

		reply.error = NDMP_IO_ERR;
		ndmp_send_reply(connection, (void *)&reply,
		    "sending tape_get_state reply");
		return;
	}

	dtpr.size = sizeof (struct mtdrivetype);
	dtpr.mtdtp = &dtp;
	if (ioctl(session->ns_tape.td_fd, MTIOCGETDRIVETYPE, &dtpr) == -1) {
		NDMP_LOG(LOG_ERR,
		    "Failed to get drive type information from tape: %m.");

		reply.error = NDMP_IO_ERR;
		ndmp_send_reply(connection, (void *)&reply,
		    "sending tape_get_state reply");
		return;
	}

	reply.flags = 0;

	reply.file_num = mtstatus.mt_fileno;
	reply.soft_errors = 0;
	reply.block_size = dtp.bsize;
	if (dtp.bsize == 0)
		reply.blockno = mtstatus.mt_blkno;
	else
		reply.blockno = mtstatus.mt_blkno *
		    (session->ns_mover.md_record_size / dtp.bsize);
	reply.total_space = long_long_to_quad(0); /* not supported */
	reply.space_remain = long_long_to_quad(0); /* not supported */
	reply.partition = 0; /* not supported */

	reply.soft_errors = 0;
	reply.total_space = long_long_to_quad(0LL);
	reply.space_remain = long_long_to_quad(0LL);

	reply.invalid = NDMP_TAPE_STATE_SOFT_ERRORS_INVALID |
	    NDMP_TAPE_STATE_TOTAL_SPACE_INVALID |
	    NDMP_TAPE_STATE_SPACE_REMAIN_INVALID |
	    NDMP_TAPE_STATE_PARTITION_INVALID;

	reply.error = NDMP_NO_ERR;
	ndmp_send_reply(connection, (void *) &reply,
	    "sending tape_get_state reply");
}


/*
 * ndmpd_tape_write_v3
 *
 * This handler handles tape_write requests.
 * This interface is a non-buffered interface. Each write request
 * maps directly to a write to the tape device. It is the responsibility
 * of the NDMP client to pad the data to the desired record size.
 * It is the responsibility of the NDMP client to ensure that the
 * length is a multiple of the tape block size if the tape device
 * is in fixed block mode.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
void
ndmpd_tape_write_v3(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_write_request *request = (ndmp_tape_write_request *) body;
	ndmp_tape_write_reply reply;
	ndmpd_session_t *session = ndmp_get_client_data(connection);
	ssize_t n;

	reply.count = 0;

	if (session->ns_tape.td_fd == -1) {
		NDMP_LOG(LOG_ERR, "Tape device is not open.");
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_write reply");
		return;
	}
	if (session->ns_tape.td_mode == NDMP_TAPE_READ_MODE) {
		NDMP_LOG(LOG_INFO, "Tape device opened in read-only mode");
		reply.error = NDMP_PERMISSION_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_write reply");
		return;
	}
	if (request->data_out.data_out_len == 0) {
		reply.error = NDMP_NO_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_write reply");
		return;
	}

	/*
	 * V4 suggests that this should not be accepted
	 * when mover is in listen or active state
	 */
	if (session->ns_protocol_version == NDMPV4 &&
	    (session->ns_mover.md_state == NDMP_MOVER_STATE_LISTEN ||
	    session->ns_mover.md_state == NDMP_MOVER_STATE_ACTIVE)) {

		reply.error = NDMP_DEVICE_BUSY_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_write reply");
		return;
	}

	/*
	 * Refer to the comment at the top of this file for
	 * Mammoth2 tape drives.
	 */
	if (session->ns_tape.td_eom_seen) {
		NDMP_LOG(LOG_DEBUG, "EOM detected");
		ndmpd_write_eom(session->ns_tape.td_fd);
		session->ns_tape.td_eom_seen = FALSE;
		reply.error = NDMP_EOM_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_write reply");
		return;
	}

	n = write(session->ns_tape.td_fd, request->data_out.data_out_val,
	    request->data_out.data_out_len);

	session->ns_tape.td_eom_seen = FALSE;
	if (n >= 0) {
		session->ns_tape.td_write = 1;
		NS_ADD(wtape, n);
	}
	if (n == 0) {
		NDMP_LOG(LOG_INFO, "EOM detected");
		reply.error = NDMP_EOM_ERR;
		session->ns_tape.td_eom_seen = TRUE;
	} else if (n < 0) {
		NDMP_LOG(LOG_ERR, "Tape write error: %m.");
		reply.error = NDMP_IO_ERR;
	} else {
		reply.count = n;
		reply.error = NDMP_NO_ERR;
	}

	ndmp_send_reply(connection, (void *) &reply,
	    "sending tape_write reply");
}


/*
 * ndmpd_tape_read_v3
 *
 * This handler handles tape_read requests.
 * This interface is a non-buffered interface. Each read request
 * maps directly to a read to the tape device. It is the responsibility
 * of the NDMP client to issue read requests with a length that is at
 * least as large as the record size used write the tape. The tape driver
 * always reads a full record. Data is discarded if the read request is
 * smaller than the record size.
 * It is the responsibility of the NDMP client to ensure that the
 * length is a multiple of the tape block size if the tape device
 * is in fixed block mode.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
void
ndmpd_tape_read_v3(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_read_request *request = (ndmp_tape_read_request *) body;
	ndmp_tape_read_reply reply;
	ndmpd_session_t *session = ndmp_get_client_data(connection);
	char *buf;
	int n, len;

	reply.data_in.data_in_len = 0;

	if (session->ns_tape.td_fd == -1) {
		NDMP_LOG(LOG_ERR, "Tape device is not open.");
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_read reply");
		return;
	}
	if (request->count == 0) {
		reply.error = NDMP_NO_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_read reply");
		return;
	}

	/*
	 * V4 suggests that this should not be accepted
	 * when mover is in listen or active state
	 */
	if (session->ns_protocol_version == NDMPV4 &&
	    (session->ns_mover.md_state == NDMP_MOVER_STATE_LISTEN ||
	    session->ns_mover.md_state == NDMP_MOVER_STATE_ACTIVE)) {

		reply.error = NDMP_DEVICE_BUSY_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_read reply");
		return;
	}

	if ((buf = ndmp_malloc(request->count)) == NULL) {
		reply.error = NDMP_NO_MEM_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_read reply");
		return;
	}
	session->ns_tape.td_eom_seen = FALSE;

	n = read(session->ns_tape.td_fd, buf, request->count);
	if (n < 0) {
		/*
		 * This fix is for Symantec during importing
		 * of spanned data between the tapes.
		 */
		if (errno == ENOSPC) {
			reply.error = NDMP_EOF_ERR;
		} else {
			NDMP_LOG(LOG_ERR, "Tape read error: %m.");
			reply.error = NDMP_IO_ERR;
		}
	} else if (n == 0) {
		(void) ndmp_mtioctl(session->ns_tape.td_fd, MTFSF, 1);

		len = strlen(NDMP_EOM_MAGIC);
		(void) memset(buf, 0, len);
		n = read(session->ns_tape.td_fd, buf, len);
		buf[len] = '\0';

		if (strncmp(buf, NDMP_EOM_MAGIC, len) == 0) {
			reply.error = NDMP_EOM_ERR;
			NDMP_LOG(LOG_DEBUG, "EOM detected");
		} else {
			reply.error = NDMP_EOF_ERR;
			NDMP_LOG(LOG_DEBUG, "EOF detected");
		}
		if (n > 0)
			(void) ndmp_mtioctl(session->ns_tape.td_fd, MTBSR, 1);
	} else {
		/*
		 * Symantec fix for import phase
		 *
		 * As import process from symantec skips filemarks
		 * they can come across to NDMP_EOM_MAGIC and treat
		 * it as data. This fix prevents the magic to be
		 * sent to the client and the read will return zero bytes
		 * and set the NDMP_EOM_ERR error. The tape should
		 * be positioned at the EOT side of the file mark.
		 */
		len = strlen(NDMP_EOM_MAGIC);
		if (n == len && strncmp(buf, NDMP_EOM_MAGIC, len) == 0) {
			reply.error = NDMP_EOM_ERR;
			(void) ndmp_mtioctl(session->ns_tape.td_fd, MTFSF, 1);
			NDMP_LOG(LOG_DEBUG, "EOM detected");
		} else {
			session->ns_tape.td_pos += n;
			reply.data_in.data_in_len = n;
			reply.data_in.data_in_val = buf;
			reply.error = NDMP_NO_ERR;
		}
		NS_ADD(rtape, n);
	}

	ndmp_send_reply(connection, (void *) &reply, "sending tape_read reply");
	free(buf);
}


/*
 * ************************************************************************
 * NDMP V4 HANDLERS
 * ************************************************************************
 */

/*
 * ndmpd_tape_get_state_v4
 *
 * This handler handles the ndmp_tape_get_state_request.
 * Status information for the currently open tape device is returned.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
/*ARGSUSED*/
void
ndmpd_tape_get_state_v4(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_get_state_reply_v4 reply;
	ndmpd_session_t *session = ndmp_get_client_data(connection);
	struct mtget mtstatus;
	struct mtdrivetype_request dtpr;
	struct mtdrivetype dtp;

	if (session->ns_tape.td_fd == -1) {
		NDMP_LOG(LOG_ERR, "Tape device is not open.");
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_get_state reply");
		return;
	}

	/*
	 * Need code to detect NDMP_TAPE_STATE_NOREWIND
	 */

	if (ioctl(session->ns_tape.td_fd, MTIOCGET, &mtstatus) == -1) {
		NDMP_LOG(LOG_ERR,
		    "Failed to get status information from tape: %m.");

		reply.error = NDMP_IO_ERR;
		ndmp_send_reply(connection, (void *)&reply,
		    "sending tape_get_state reply");
		return;
	}

	dtpr.size = sizeof (struct mtdrivetype);
	dtpr.mtdtp = &dtp;
	if (ioctl(session->ns_tape.td_fd, MTIOCGETDRIVETYPE, &dtpr) == -1) {
		NDMP_LOG(LOG_ERR,
		    "Failed to get drive type information from tape: %m.");

		reply.error = NDMP_IO_ERR;
		ndmp_send_reply(connection, (void *)&reply,
		    "sending tape_get_state reply");
		return;
	}

	reply.flags = NDMP_TAPE_NOREWIND;

	reply.file_num = mtstatus.mt_fileno;
	reply.soft_errors = 0;
	reply.block_size = dtp.bsize;

	if (dtp.bsize == 0)
		reply.blockno = mtstatus.mt_blkno;
	else
		reply.blockno = mtstatus.mt_blkno *
		    (session->ns_mover.md_record_size / dtp.bsize);

	reply.total_space = long_long_to_quad(0); /* not supported */
	reply.space_remain = long_long_to_quad(0); /* not supported */

	reply.soft_errors = 0;
	reply.total_space = long_long_to_quad(0LL);
	reply.space_remain = long_long_to_quad(0LL);
	reply.unsupported = NDMP_TAPE_STATE_SOFT_ERRORS_INVALID |
	    NDMP_TAPE_STATE_TOTAL_SPACE_INVALID |
	    NDMP_TAPE_STATE_SPACE_REMAIN_INVALID |
	    NDMP_TAPE_STATE_PARTITION_INVALID;

	reply.error = NDMP_NO_ERR;
	ndmp_send_reply(connection, (void *) &reply,
	    "sending tape_get_state reply");
}
/*
 * ndmpd_tape_close_v4
 *
 * This handler (v4) closes the currently open tape device.
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   body       (input) - request message body.
 *
 * Returns:
 *   void
 */
/*ARGSUSED*/
void
ndmpd_tape_close_v4(ndmp_connection_t *connection, void *body)
{
	ndmp_tape_close_reply reply;
	ndmpd_session_t *session = ndmp_get_client_data(connection);

	if (session->ns_tape.td_fd == -1) {
		NDMP_LOG(LOG_ERR, "Tape device is not open.");
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_close reply");
		return;
	}

	/*
	 * V4 suggests that this should not be accepted
	 * when mover is in listen or active state
	 */
	if (session->ns_mover.md_state == NDMP_MOVER_STATE_LISTEN ||
	    session->ns_mover.md_state == NDMP_MOVER_STATE_ACTIVE) {

		reply.error = NDMP_DEVICE_BUSY_ERR;
		ndmp_send_reply(connection, (void *) &reply,
		    "sending tape_close reply");
		return;
	}

	common_tape_close(connection);
}


/*
 * ************************************************************************
 * LOCALS
 * ************************************************************************
 */
/*
 * tape_open_send_reply
 *
 * Send a reply to the tape open message
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   err (input) - NDMP error
 *
 * Returns:
 *   void
 */
static void
tape_open_send_reply(ndmp_connection_t *connection, int err)
{
	ndmp_tape_open_reply reply;

	reply.error = err;
	ndmp_send_reply(connection, (void *) &reply, "sending tape_open reply");
}

/*
 * unbuffered_read
 *
 * Perform tape read without read-ahead
 *
 * Parameters:
 *   session (input) - session handle
 *   bp (output) - read buffer
 *   wanted (input) - number of bytes wanted
 *   reply (output) - tape read reply message
 *
 * Returns:
 *   void
 */
static void
unbuffered_read(ndmpd_session_t *session, char *buf, long wanted,
    ndmp_tape_read_reply *reply)
{
	int n, len;

	n = read(session->ns_tape.td_fd, buf, wanted);
	if (n < 0) {
		/*
		 * This fix is for Symantec during importing
		 * of spanned data between the tapes.
		 */
		if (errno == ENOSPC) {
			reply->error = NDMP_EOF_ERR;
		} else {
			NDMP_LOG(LOG_ERR, "Tape read error: %m.");
			reply->error = NDMP_IO_ERR;
		}
	} else if (n == 0) {
		NDMP_LOG(LOG_DEBUG, "EOF detected");

		reply->error = NDMP_EOF_ERR;

		(void) ndmp_mtioctl(session->ns_tape.td_fd, MTFSF, 1);

		len = strlen(NDMP_EOM_MAGIC);
		(void) memset(buf, 0, len);
		n = read(session->ns_tape.td_fd, buf, len);
		buf[len] = '\0';

		(void) ndmp_mtioctl(session->ns_tape.td_fd, MTBSF, 1);

		if (strncmp(buf, NDMP_EOM_MAGIC, len) != 0)
			(void) ndmp_mtioctl(session->ns_tape.td_fd, MTFSF, 1);
	} else {
		session->ns_tape.td_pos += n;
		reply->data_in.data_in_len = n;
		reply->data_in.data_in_val = buf;
		reply->error = NDMP_NO_ERR;
		NS_ADD(rtape, n);
	}
}


/*
 * validmode
 *
 * Check the tape read mode is valid
 */
static boolean_t
validmode(int mode)
{
	boolean_t rv;

	switch (mode) {
	case NDMP_TAPE_READ_MODE:
	case NDMP_TAPE_WRITE_MODE:
	case NDMP_TAPE_RAW1_MODE:
	case NDMP_TAPE_RAW2_MODE:
		rv = TRUE;
		break;
	default:
		rv = FALSE;
	}

	return (rv);
}


/*
 * common_tape_open
 *
 * Generic function for opening the tape for all versions
 *
 * Parameters:
 *   connection (input) - connection handle.
 *   devname (input) - tape device name to open.
 *   ndmpmode (input) - mode of opening (read, write, raw)
 *
 * Returns:
 *   void
 */
static void
common_tape_open(ndmp_connection_t *connection, char *devname, int ndmpmode)
{
	ndmpd_session_t *session = ndmp_get_client_data(connection);
	char adptnm[SCSI_MAX_NAME];
	int err;
	int mode;
	int sid, lun;
	scsi_adapter_t *sa;
	int devid;

	err = NDMP_NO_ERR;

	if (session->ns_tape.td_fd != -1 || session->ns_scsi.sd_is_open != -1) {
		NDMP_LOG(LOG_INFO,
		    "Connection already has a tape or scsi device open");
		err = NDMP_DEVICE_OPENED_ERR;
	} else if (!validmode(ndmpmode))
		err = NDMP_ILLEGAL_ARGS_ERR;
	if ((sa = scsi_get_adapter(0)) != NULL) {
		(void) strlcpy(adptnm, devname, SCSI_MAX_NAME-2);
		adptnm[SCSI_MAX_NAME-1] = '\0';
		sid = lun = -1;
	}
	if (sa) {
		scsi_find_sid_lun(sa, devname, &sid, &lun);
		if (ndmp_open_list_find(devname, sid, lun) == 0 &&
		    (devid = open(devname, O_RDWR | O_NDELAY)) < 0) {
			NDMP_LOG(LOG_ERR,
			    "Failed to open device %s: %m.", devname);
			err = NDMP_NO_DEVICE_ERR;
		} else {
			(void) close(devid);
		}
	} else {
		NDMP_LOG(LOG_ERR, "%s: No such tape device.", devname);
		err = NDMP_NO_DEVICE_ERR;
	}

	if (err != NDMP_NO_ERR) {
		tape_open_send_reply(connection, err);
		return;
	}

	/*
	 * If tape is not opened in raw mode and tape is not loaded
	 * return error.
	 */
	if (ndmpmode != NDMP_TAPE_RAW1_MODE &&
	    ndmpmode != NDMP_TAPE_RAW2_MODE &&
	    !is_tape_unit_ready(adptnm, 0)) {
		tape_open_send_reply(connection, NDMP_NO_TAPE_LOADED_ERR);
		return;
	}

	mode = (ndmpmode == NDMP_TAPE_READ_MODE) ? O_RDONLY : O_RDWR;
	mode |= O_NDELAY;
	session->ns_tape.td_fd = open(devname, mode);
	if (session->ns_protocol_version == NDMPV4 &&
	    session->ns_tape.td_fd < 0 &&
	    ndmpmode == NDMP_TAPE_RAW_MODE && errno == EACCES) {
		/*
		 * V4 suggests that if the tape is open in raw mode
		 * and could not be opened with write access, it should
		 * be opened read only instead.
		 */
		ndmpmode = NDMP_TAPE_READ_MODE;
		session->ns_tape.td_fd = open(devname, O_RDONLY);
	}
	if (session->ns_tape.td_fd < 0) {
		NDMP_LOG(LOG_ERR, "Failed to open tape device %s: %m.",
		    devname);
		switch (errno) {
		case EACCES:
			err = NDMP_WRITE_PROTECT_ERR;
			break;
		case ENOENT:
			err = NDMP_NO_DEVICE_ERR;
			break;
		case EBUSY:
			err = NDMP_DEVICE_BUSY_ERR;
			break;
		case EPERM:
			err = NDMP_PERMISSION_ERR;
			break;
		default:
			err = NDMP_IO_ERR;
		}

		tape_open_send_reply(connection, err);
		return;
	}

	switch (ndmp_open_list_add(connection,
	    adptnm, sid, lun, session->ns_tape.td_fd)) {
	case 0:
		err = NDMP_NO_ERR;
		break;
	case EBUSY:
		err = NDMP_DEVICE_BUSY_ERR;
		break;
	case ENOMEM:
		err = NDMP_NO_MEM_ERR;
		break;
	default:
		err = NDMP_IO_ERR;
	}
	if (err != NDMP_NO_ERR) {
		tape_open_send_reply(connection, err);
		return;
	}

	session->ns_tape.td_mode = ndmpmode;
	session->ns_tape.td_sid = sid;
	session->ns_tape.td_lun = lun;
	(void) strlcpy(session->ns_tape.td_adapter_name, adptnm, SCSI_MAX_NAME);
	session->ns_tape.td_record_count = 0;
	session->ns_tape.td_eom_seen = FALSE;

	tape_open_send_reply(connection, NDMP_NO_ERR);
}


/*
 * common_tape_close
 *
 * Generic function for closing the tape
 *
 * Parameters:
 *   connection (input) - connection handle.
 *
 * Returns:
 *   void
 */
static void
common_tape_close(ndmp_connection_t *connection)
{
	ndmpd_session_t *session = ndmp_get_client_data(connection);
	ndmp_tape_close_reply reply;

	(void) ndmp_open_list_del(session->ns_tape.td_adapter_name,
	    session->ns_tape.td_sid, session->ns_tape.td_lun);
	(void) close(session->ns_tape.td_fd);
	session->ns_tape.td_fd = -1;
	session->ns_tape.td_sid = 0;
	session->ns_tape.td_lun = 0;
	session->ns_tape.td_write = 0;
	(void) memset(session->ns_tape.td_adapter_name, 0,
	    sizeof (session->ns_tape.td_adapter_name));
	session->ns_tape.td_record_count = 0;
	session->ns_tape.td_eom_seen = FALSE;

	reply.error = NDMP_NO_ERR;
	ndmp_send_reply(connection, (void *) &reply,
	    "sending tape_close reply");
}

/*
 * tape_open
 *
 * Will try to open the tape with the given flags and
 * path using the given retries and delay intervals
 */
int
tape_open(char *path, int flags)
{
	int fd;
	int i = 0;

	while ((fd = open(path, flags)) == -1 &&
	    i++ < ndmp_tape_open_retries) {
		if (errno != EBUSY)
			break;
		(void) usleep(ndmp_tape_open_delay);
	}
	return (fd);
}
