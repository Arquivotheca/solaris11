/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
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

#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <time.h>
#include "ndmpd.h"
#include <bitmap.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socketvar.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/filio.h>
#include <sys/mtio.h>
#include <sys/scsi/impl/uscsi.h>
#include <sys/scsi/scsi.h>
#include "tlm.h"
#include <sys/sysevent/eventdefs.h>
#include <libsysevent.h>
#include <libnvpair.h>
#include <sys/sysevent.h>

/*
 * Mutex to protect Nlp
 */
mutex_t nlp_mtx;

/*
 * Patchable socket buffer sizes in kilobytes.
 * ssb: send buffer size.
 * rsb: receive buffer size.
 */
int ndmp_sbs = 60;
int ndmp_rbs = 60;


/*
 * Force to backup all the intermediate directories leading to an object
 * to be backed up in 'dump' format backup.
 */
boolean_t ndmp_dump_path_node = FALSE;


/*
 * Force to backup all the intermediate directories leading to an object
 * to be backed up in 'tar' format backup.
 */
boolean_t ndmp_tar_path_node = FALSE;


/*
 * Should the 'st_ctime' be ignored during incremental level backup?
 */
boolean_t ndmp_ignore_ctime = FALSE;

/*
 * Should the 'st_lmtime' be included during incremental level backup?
 */
boolean_t ndmp_include_lmtime = FALSE;

/*
 * Force to send the file history node entries along with the file history
 * dir entries for all directories containing the changed files to the client
 * for incremental backup.
 *
 * Note: This variable is added to support Bakbone Software's Netvault DMA
 * which expects to get the FH ADD NODES for all upper directories which
 * contain the changed files in incremental backup along with the FH ADD DIRS.
 */
boolean_t ndmp_fhinode = FALSE;

/*
 * Maximum permitted sequence number in the token-based backup.  The
 * value of this variable can be changed by the administrator and is
 * saved in the NDMP configuration file.
 */
static int ndmp_max_tok_seq = NDMP_MAX_TOKSEQ;

/*
 * Force backup directories in incremental backups.  If the
 * directory is not modified itself, it's not backed up by
 * default.
 */
int ndmp_force_bk_dirs = 0;

/*
 * Keeps track of the open SCSI (including tape and robot) devices.
 * When a SCSI device is opened its name must be added to this list and
 * when it's closed its name must be removed from this list.  The main
 * purpose of this list is the robot device.  If the robot devices are not
 * attached in SASD layer, Local Backup won't see them. If they are
 * attached and we open the robot devices, then wrong commands are sent
 * to robot by SASD since it assumes that the robot is a tape (sequential
 * access) device.
 */
struct open_list {
	LIST_ENTRY(open_list) ol_q;
	int ol_nref;
	char *ol_devnm;
	int ol_sid;
	int ol_lun;
	int ol_fd;
	ndmp_connection_t *cl_conn;
};
LIST_HEAD(ol_head, open_list);


/*
 * Head of the opened SCSI devices list.
 */
static struct ol_head ol_head;

mutex_t ol_mutex = DEFAULTMUTEX;


/*
 * List of things to be exluded from backup.
 */
static char *exls[] = {
	EXCL_PROC,
	EXCL_TMP,
	NULL, /* reserved for a copy of the "backup.directory" */
	NULL
};
static int ndmp_job_cnt = 0;

static int scsi_test_unit_ready(int dev_id);
static void *indirect_start_routine(void *new_thread_data);

/*
 * ndmpd_add_file_handler
 *
 * Adds a file handler to the file handler list.
 * The file handler list is used by ndmpd_api_dispatch.
 *
 * Parameters:
 *   session (input) - session pointer.
 *   cookie  (input) - opaque data to be passed to file hander when called.
 *   fd      (input) - file descriptor.
 *   mode    (input) - bitmask of the following:
 *		     1 = watch file for ready for reading
 *		     2 = watch file for ready for writing
 *		     4 = watch file for exception
 *   class   (input) - handler class. (HC_CLIENT, HC_MOVER, HC_MODULE)
 *   func    (input) - function to call when the file meets one of the
 *		     conditions specified by mode.
 *
 * Returns:
 *   0 - success.
 *  -1 - error.
 */
int
ndmpd_add_file_handler(ndmpd_session_t *session, void *cookie, int fd,
    ulong_t mode, ulong_t class, ndmpd_file_handler_func_t *func)
{
	ndmpd_file_handler_t *new;

	new = ndmp_malloc(sizeof (ndmpd_file_handler_t));
	if (new == 0)
		return (-1);

	new->fh_cookie = cookie;
	new->fh_fd = fd;
	new->fh_mode = mode;
	new->fh_class = class;
	new->fh_func = func;
	new->fh_next = session->ns_file_handler_list;
	session->ns_file_handler_list = new;
	return (0);
}


/*
 * ndmpd_remove_file_handler
 *
 * Removes a file handler from the file handler list.
 *
 * Parameters:
 *   session (input) - session pointer.
 *   fd      (input) - file descriptor.
 *
 * Returns:
 *   0 - success.
 *  -1 - error.
 */
int
ndmpd_remove_file_handler(ndmpd_session_t *session, int fd)
{
	ndmpd_file_handler_t **last;
	ndmpd_file_handler_t *handler;

	last = &session->ns_file_handler_list;
	while (*last != 0) {
		handler = *last;

		if (handler->fh_fd == fd) {
			*last = handler->fh_next;
			(void) free(handler);
			return (1);
		}
		last = &handler->fh_next;
	}

	return (0);
}


/*
 * ndmp_connection_closed
 *
 * If the connection closed or not.
 *
 * Parameters:
 *   fd (input) : file descriptor
 *
 * Returns:
 *   0  - connection is still valid
 *   1  - connection is not valid anymore
 *   -1 - Internal kernel error
 */
int
ndmp_connection_closed(int fd)
{
	fd_set fds;
	int closed, ret;
	struct timeval timeout;

	if (fd < 0) /* We are not using the mover */
		return (-1);

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	ret = select(FD_SETSIZE, &fds, NULL, NULL, &timeout);

	closed = (ret == -1 && errno == EBADF);

	return (closed);
}

/*
 * ndmp_check_mover_state
 *
 * Checks the mover connection status and sends an appropriate
 * NDMP message to client based on that.
 *
 * Parameters:
 *   ndmpd_session_t *session (input) : session pointer
 *
 * Returns:
 *   void.
 */
void
ndmp_check_mover_state(ndmpd_session_t *session)
{
	int moverfd;
	/*
	 * NDMPV3 Spec (Three-way restore):
	 * Once all of the files have been recovered, NDMP DATA Server closes
	 * the connection to the mover on the NDMP TAPE Server. THEN
	 * The NDMP client should receive an NDMP_NOTIFY_MOVER_HALTED message
	 * with an NDMP_MOVER_CONNECT_CLOSED reason from the NDMP TAPE Server
	 */
	moverfd = session->ns_mover.md_sock;
	/* If connection is closed by the peer */
	if (moverfd >= 0 &&
	    session->ns_mover.md_mode == NDMP_MOVER_MODE_WRITE) {
		int closed, reason;

		closed = ndmp_connection_closed(moverfd);
		if (closed) {
			/* Connection closed or internal error */
			if (closed > 0) {
				NDMP_LOG(LOG_ERR,
				    "ndmp mover: connection closed by peer");
				reason = NDMP_MOVER_HALT_CONNECT_CLOSED;
			} else {
				NDMP_LOG(LOG_ERR,
				    "ndmp mover: Internal error");
				reason = NDMP_MOVER_HALT_INTERNAL_ERROR;
			}
			ndmpd_mover_error(session, reason);

		}
	}
}


/*
 * ndmpd_select
 *
 * Calls select on the the set of file descriptors from the
 * file handler list masked by the fd_class argument.
 * Calls the file handler function for each
 * file descriptor that is ready for I/O.
 *
 * Parameters:
 *   session (input) - session pointer.
 *   block   (input) - if TRUE, ndmpd_select waits until at least one
 *		     file descriptor is ready for I/O. Otherwise,
 *		     it returns immediately if no file descriptors are
 *		     ready for I/O.
 *   class_mask (input) - bit mask of handler classes to be examined.
 *		     Provides for excluding some of the handlers from
 *		     being called.
 *
 * Returns:
 *  -1 - error.
 *   0 - no handlers were called.
 *   1 - at least one handler was called.
 */
int
ndmpd_select(ndmpd_session_t *session, boolean_t block, ulong_t class_mask)
{
	fd_set rfds;
	fd_set wfds;
	fd_set efds;
	int n;
	ndmpd_file_handler_t *handler;
	struct timeval timeout;

	nlp_event_rv_set(session, 0);

	if (session->ns_file_handler_list == 0)
		return (0);


	/*
	 * If select should be blocked, then we poll every ten seconds.
	 * The reason is in case of three-way restore we should be able
	 * to detect if the other end closed the connection or not.
	 * NDMP client(DMA) does not send any information about the connection
	 * that was closed in the other end.
	 */

	if (block == TRUE)
		timeout.tv_sec = 10;
	else
		timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	do {
		/* Create the fd_sets for select. */
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_ZERO(&efds);

		for (handler = session->ns_file_handler_list; handler != 0;
		    handler = handler->fh_next) {
			if ((handler->fh_class & class_mask) == 0)
				continue;

			if (handler->fh_mode & NDMPD_SELECT_MODE_READ)
				FD_SET(handler->fh_fd, &rfds);
			if (handler->fh_mode & NDMPD_SELECT_MODE_WRITE)
				FD_SET(handler->fh_fd, &wfds);
			if (handler->fh_mode & NDMPD_SELECT_MODE_EXCEPTION)
				FD_SET(handler->fh_fd, &efds);
		}
		ndmp_check_mover_state(session);
		n = select(FD_SETSIZE, &rfds, &wfds, &efds, &timeout);
	} while (n == 0 && block == TRUE);

	if (n < 0) {
		int connection_fd = ndmp_get_fd(session->ns_connection);

		if (errno == EINTR)
			return (0);

		NDMP_LOG(LOG_ERR, "Select error: %m");

		for (handler = session->ns_file_handler_list; handler != 0;
		    handler = handler->fh_next) {
			if ((handler->fh_class & class_mask) == 0)
				continue;

			if (handler->fh_mode & NDMPD_SELECT_MODE_READ) {
				if (FD_ISSET(handler->fh_fd, &rfds) &&
				    connection_fd == handler->fh_fd)
					session->ns_eof = TRUE;
			}
			if (handler->fh_mode & NDMPD_SELECT_MODE_WRITE) {
				if (FD_ISSET(handler->fh_fd, &wfds) &&
				    connection_fd == handler->fh_fd)
					session->ns_eof = TRUE;
			}
			if (handler->fh_mode & NDMPD_SELECT_MODE_EXCEPTION) {
				if (FD_ISSET(handler->fh_fd, &efds) &&
				    connection_fd == handler->fh_fd)
					session->ns_eof = TRUE;
			}
		}

		nlp_event_rv_set(session, -1);
		return (-1);
	}
	if (n == 0)
		return (0);

	handler = session->ns_file_handler_list;
	while (handler != 0) {
		ulong_t mode = 0;

		if ((handler->fh_class & class_mask) == 0) {
			handler = handler->fh_next;
			continue;
		}
		if (handler->fh_mode & NDMPD_SELECT_MODE_READ) {
			if (FD_ISSET(handler->fh_fd, &rfds)) {
				mode |= NDMPD_SELECT_MODE_READ;
				FD_CLR(handler->fh_fd, &rfds);
			}
		}
		if (handler->fh_mode & NDMPD_SELECT_MODE_WRITE) {
			if (FD_ISSET(handler->fh_fd, &wfds)) {
				mode |= NDMPD_SELECT_MODE_WRITE;
				FD_CLR(handler->fh_fd, &wfds);
			}
		}
		if (handler->fh_mode & NDMPD_SELECT_MODE_EXCEPTION) {
			if (FD_ISSET(handler->fh_fd, &efds)) {
				mode |= NDMPD_SELECT_MODE_EXCEPTION;
				FD_CLR(handler->fh_fd, &efds);
			}
		}
		if (mode) {
			(*handler->fh_func) (handler->fh_cookie,
			    handler->fh_fd, mode);

			/*
			 * K.L. The list can be modified during the execution
			 * of handler->fh_func. Therefore, handler will start
			 * from the beginning of the handler list after
			 * each execution.
			 */
			handler = session->ns_file_handler_list;

			/*
			 * Release the thread which is waiting for a request
			 * to be proccessed.
			 */
			nlp_event_nw(session);
		} else
			handler = handler->fh_next;

	}

	nlp_event_rv_set(session, 1);
	return (1);
}


/*
 * ndmpd_save_env
 *
 * Saves a copy of the environment variable list from the data_start_backup
 * request or data_start_recover request.
 *
 * Parameters:
 *   session (input) - session pointer.
 *   env     (input) - environment variable list to be saved.
 *   envlen  (input) - length of variable array.
 *
 * Returns:
 *   error code.
 */
ndmp_error
ndmpd_save_env(ndmpd_session_t *session, ndmp_pval *env, ulong_t envlen)
{
	ulong_t i;
	char *namebuf;
	char *valbuf;

	session->ns_data.dd_env_len = 0;

	if (envlen == 0)
		return (NDMP_NO_ERR);

	NDMP_LOG(LOG_DEBUG, "-------- Operation initiated --------");
	session->ns_data.dd_env = ndmp_malloc(sizeof (ndmp_pval) * envlen);
	if (session->ns_data.dd_env == 0)
		return (NDMP_NO_MEM_ERR);

	for (i = 0; i < envlen; i++) {
		namebuf = strdup(env[i].name);
		if (namebuf == 0)
			return (NDMP_NO_MEM_ERR);

		valbuf = strdup(env[i].value);
		if (valbuf == 0) {
			free(namebuf);
			return (NDMP_NO_MEM_ERR);
		}

		NDMP_LOG(LOG_DEBUG, "env(%s): \"%s\"",
		    namebuf, valbuf);

		(void) mutex_lock(&session->ns_lock);
		session->ns_data.dd_env[i].name = namebuf;
		session->ns_data.dd_env[i].value = valbuf;
		session->ns_data.dd_env_len++;
		(void) mutex_unlock(&session->ns_lock);
	}

	return (NDMP_NO_ERR);
}


/*
 * ndmpd_free_env
 *
 * Free the previously saved environment variable array.
 *
 * Parameters:
 *   session - NDMP session pointer.
 *
 * Returns:
 *   void.
 */
void
ndmpd_free_env(ndmpd_session_t *session)
{
	ulong_t i;
	int count = session->ns_data.dd_env_len;

	(void) mutex_lock(&session->ns_lock);
	session->ns_data.dd_env_len = 0;
	for (i = 0; i < count; i++) {
		free(session->ns_data.dd_env[i].name);
		free(session->ns_data.dd_env[i].value);
	}

	free((char *)session->ns_data.dd_env);
	session->ns_data.dd_env = 0;
	(void) mutex_unlock(&session->ns_lock);
}


/*
 * ndmpd_save_nlist_v2
 *
 * Save a copy of list of file names to be restored.
 *
 * Parameters:
 *   nlist    (input) - name list from data_start_recover request.
 *   nlistlen (input) - length of name list.
 *
 * Returns:
 *   array of file name pointers.
 *
 * Notes:
 *   free_nlist should be called to free the returned list.
 *   A null pointer indicates the end of the list.
 */
ndmp_error
ndmpd_save_nlist_v2(ndmpd_session_t *session, ndmp_name *nlist,
    ulong_t nlistlen)
{
	ulong_t i;
	char *namebuf;
	char *destbuf;

	if (nlistlen == 0)
		return (NDMP_NO_ERR);

	session->ns_data.dd_nlist_len = 0;
	session->ns_data.dd_nlist = ndmp_malloc(sizeof (ndmp_name)*nlistlen);
	if (session->ns_data.dd_nlist == 0)
		return (NDMP_NO_MEM_ERR);

	for (i = 0; i < nlistlen; i++) {
		namebuf = ndmp_malloc(strlen(nlist[i].name) + 1);
		if (namebuf == 0)
			return (NDMP_NO_MEM_ERR);

		destbuf = ndmp_malloc(strlen(nlist[i].dest) + 1);
		if (destbuf == 0) {
			free(namebuf);
			return (NDMP_NO_MEM_ERR);
		}
		(void) strlcpy(namebuf, nlist[i].name,
		    strlen(nlist[i].name) + 1);
		(void) strlcpy(destbuf, nlist[i].dest,
		    strlen(nlist[i].dest) + 1);

		session->ns_data.dd_nlist[i].name = namebuf;
		session->ns_data.dd_nlist[i].dest = destbuf;
		session->ns_data.dd_nlist[i].ssid = nlist[i].ssid;
		session->ns_data.dd_nlist[i].fh_info = nlist[i].fh_info;
		session->ns_data.dd_nlist_len++;
	}

	return (NDMP_NO_ERR);
}


/*
 * ndmpd_free_nlist_v2
 *
 * Free a list created by ndmpd_save_nlist_v2.
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   void
 */
void
ndmpd_free_nlist_v2(ndmpd_session_t *session)
{
	ulong_t i;

	for (i = 0; i < session->ns_data.dd_nlist_len; i++) {
		free(session->ns_data.dd_nlist[i].name);
		free(session->ns_data.dd_nlist[i].dest);
	}

	if (session->ns_data.dd_nlist != NULL)
		free((char *)session->ns_data.dd_nlist);
	session->ns_data.dd_nlist = 0;
	session->ns_data.dd_nlist_len = 0;
}


/*
 * ndmpd_free_nlist_v3
 *
 * Free a list created by ndmpd_save_nlist_v3.
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   void
 */
void
ndmpd_free_nlist_v3(ndmpd_session_t *session)
{
	ulong_t i;
	mem_ndmp_name_v3_t *tp; /* destination entry */

	tp = session->ns_data.dd_nlist_v3;
	for (i = 0; i < session->ns_data.dd_nlist_len; tp++, i++) {
		NDMP_FREE(tp->nm3_opath);
		NDMP_FREE(tp->nm3_dpath);
		NDMP_FREE(tp->nm3_newnm);
	}

	NDMP_FREE(session->ns_data.dd_nlist_v3);
	session->ns_data.dd_nlist_len = 0;
}


/*
 * ndmpd_save_nlist_v3
 *
 * Save a copy of list of file names to be restored.
 *
 * Parameters:
 *   nlist    (input) - name list from data_start_recover request.
 *   nlistlen (input) - length of name list.
 *
 * Returns:
 *   array of file name pointers.
 *
 * Notes:
 *   free_nlist should be called to free the returned list.
 *   A null pointer indicates the end of the list.
 */
ndmp_error
ndmpd_save_nlist_v3(ndmpd_session_t *session, ndmp_name_v3 *nlist,
    ulong_t nlistlen)
{
	ulong_t i;
	ndmp_error rv;
	ndmp_name_v3 *sp; /* source entry */
	mem_ndmp_name_v3_t *tp; /* destination entry */

	if (nlistlen == 0)
		return (NDMP_ILLEGAL_ARGS_ERR);

	session->ns_data.dd_nlist_len = 0;
	tp = session->ns_data.dd_nlist_v3 =
	    ndmp_malloc(sizeof (mem_ndmp_name_v3_t) * nlistlen);
	if (session->ns_data.dd_nlist_v3 == 0)
		return (NDMP_NO_MEM_ERR);

	rv = NDMP_NO_ERR;
	sp = nlist;
	for (i = 0; i < nlistlen; tp++, sp++, i++) {
		tp->nm3_opath = strdup(sp->original_path);
		if (!tp->nm3_opath) {
			rv = NDMP_NO_MEM_ERR;
			break;
		}
		if (!*sp->destination_dir) {
			tp->nm3_dpath = NULL;
			/* In V4 destination dir cannot be NULL */
			if (session->ns_protocol_version == NDMPV4) {
				rv = NDMP_ILLEGAL_ARGS_ERR;
				break;
			}
		} else if (!(tp->nm3_dpath = strdup(sp->destination_dir))) {
			rv = NDMP_NO_MEM_ERR;
			break;
		}
		if (!*sp->new_name)
			tp->nm3_newnm = NULL;
		else if (!(tp->nm3_newnm = strdup(sp->new_name))) {
			rv = NDMP_NO_MEM_ERR;
			break;
		}

		tp->nm3_node = quad_to_long_long(sp->node);
		tp->nm3_fh_info = quad_to_long_long(sp->fh_info);
		tp->nm3_err = NDMP_NO_ERR;
		session->ns_data.dd_nlist_len++;
	}

	if (rv != NDMP_NO_ERR)
		ndmpd_free_nlist_v3(session);

	return (rv);
}


/*
 * ndmpd_free_nlist
 *
 * Free the recovery list based on the version
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   void
 */
void
ndmpd_free_nlist(ndmpd_session_t *session)
{
	switch (session->ns_protocol_version) {
	case 1:
	case 2:
		ndmpd_free_nlist_v2(session);
		break;
	case 3:
	case 4:
		ndmpd_free_nlist_v3(session);
		break;
	}
}


/*
 * fh_cmpv3
 *
 * Comparison function used in sorting the Nlist based on their
 * file history info (offset of the entry on the tape)
 *
 * Parameters:
 *   p (input) - pointer to P
 *   q (input) - pointer to Q
 *
 * Returns:
 *  -1: P < Q
 *   0: P = Q
 *   1: P > Q
 */
static int
fh_cmpv3(const void *p,
		const void *q)
{
#define	FH_INFOV3(p)	(((mem_ndmp_name_v3_t *)p)->nm3_fh_info)

	if (FH_INFOV3(p) < FH_INFOV3(q))
		return (-1);
	else if (FH_INFOV3(p) == FH_INFOV3(q))
		return (0);
	else
		return (1);

#undef FH_INFOV3
}


/*
 * ndmp_sort_nlist_v3
 *
 * Sort the recovery list based on their offset on the tape
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   void
 */
void
ndmp_sort_nlist_v3(ndmpd_session_t *session)
{
	if (!session || session->ns_data.dd_nlist_len == 0 ||
	    !session->ns_data.dd_nlist_v3)
		return;

	(void) qsort(session->ns_data.dd_nlist_v3,
	    session->ns_data.dd_nlist_len,
	    sizeof (mem_ndmp_name_v3_t), fh_cmpv3);
}


/*
 * ndmp_send_reply
 *
 * Send the reply, check for error and print the msg if any error
 * occured when sending the reply.
 *
 *   Parameters:
 *     connection (input) - connection pointer.
 *
 *   Return:
 *     void
 */
void
ndmp_send_reply(ndmp_connection_t *connection, void *reply, char *msg)
{
	if (ndmp_send_response(connection, NDMP_NO_ERR, reply) < 0)
		NDMP_LOG(LOG_ERR, "%s", msg);
}


/*
 * ndmp_mtioctl
 *
 * Performs numerous filemark operations.
 *
 * Parameters:
 * 	fd - file descriptor of the device
 *	cmd - filemark or record command
 * 	count - the number of operations to be performed
 */
int
ndmp_mtioctl(int fd, int cmd, int count)
{
	struct mtop mp;

	mp.mt_op = cmd;
	mp.mt_count = count;
	if (ioctl(fd, MTIOCTOP, &mp) < 0) {
		NDMP_LOG(LOG_ERR, "Failed to send command to tape: %m.");
		return (-1);
	}

	return (0);
}


/*
 * quad_to_long_long
 *
 * Convert type quad to longlong_t
 */
u_longlong_t
quad_to_long_long(ndmp_u_quad q)
{
	u_longlong_t ull;

	ull = ((u_longlong_t)q.high << 32) + q.low;
	return (ull);
}


/*
 * long_long_to_quad
 *
 * Convert long long to quad type
 */
ndmp_u_quad
long_long_to_quad(u_longlong_t ull)
{
	ndmp_u_quad q;

	q.high = (ulong_t)(ull >> 32);
	q.low = (ulong_t)ull;
	return (q);
}


/*
 * ndmp_set_socket_nodelay
 *
 * Set the TCP socket option to nodelay mode
 */
void
ndmp_set_socket_nodelay(int sock)
{
	int flag = 1;

	(void) setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof (flag));
}


/*
 * ndmp_set_socket_snd_buf
 *
 * Set the socket send buffer size
 */
void
ndmp_set_socket_snd_buf(int sock, int size)
{
	if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &size, sizeof (size)) < 0)
		NDMP_LOG(LOG_ERR, "SO_SNDBUF failed errno=%d", errno);
}


/*
 * ndmp_set_socket_rcv_buf
 *
 * Set the socket receive buffer size
 */
void
ndmp_set_socket_rcv_buf(int sock, int size)
{
	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &size, sizeof (size)) < 0)
		NDMP_LOG(LOG_ERR, "SO_RCVBUF failed errno=%d", errno);
}

/*
 * ndmp_get_max_tok_seq
 *
 * Get the maximum permitted token sequence for token-based
 * backups.
 *
 * Parameters:
 *   void
 *
 * Returns:
 *   ndmp_max_tok_seq
 */
int
ndmp_get_max_tok_seq(void)
{
	return (ndmp_max_tok_seq);
}

/*
 * ndmp_buffer_get_size
 *
 * Return the NDMP transfer buffer size
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   buffer size
 */
long
ndmp_buffer_get_size(ndmpd_session_t *session)
{
	long xfer_size;

	if (session == NULL)
		return (0);

	if (session->ns_data.dd_mover.addr_type == NDMP_ADDR_TCP) {
		xfer_size = atoi(ndmpd_get_prop_default(NDMP_MOVER_RECSIZE,
		    "60"));
		if (xfer_size > 0)
			xfer_size *= KILOBYTE;
		else
			xfer_size = REMOTE_RECORD_SIZE;
		NDMP_LOG(LOG_DEBUG, "Remote operation: %d", xfer_size);
	} else {
		NDMP_LOG(LOG_DEBUG,
		    "Local operation: %lu", session->ns_mover.md_record_size);
		if ((xfer_size = session->ns_mover.md_record_size) == 0)
			xfer_size = MAX_RECORD_SIZE;
	}
	return (xfer_size);
}


/*
 * ndmp_lbr_init
 *
 * Initialize the LBR/NDMP backup parameters
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   0: on success
 *  -1: otherwise
 */
int
ndmp_lbr_init(ndmpd_session_t *session)
{
	if (session->ns_ndmp_lbr_params != NULL) {
		return (0);
	}

	session->ns_ndmp_lbr_params = ndmp_malloc(sizeof (ndmp_lbr_params_t));
	if (session->ns_ndmp_lbr_params == NULL)
		return (-1);

	session->ns_ndmp_lbr_params->nlp_bkmap = -1;
	session->ns_ndmp_lbr_params->nlp_session = session;
	(void) cond_init(&session->ns_ndmp_lbr_params->nlp_cv, 0, NULL);
	(void) mutex_init(&session->ns_lock, 0, NULL);
	session->ns_nref = 0;
	return (0);
}


/*
 * ndmp_lbr_cleanup
 *
 * Deallocate and cleanup all NDMP/LBR parameters
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   0: on success
 *  -1: otherwise
 */
void
ndmp_lbr_cleanup(ndmpd_session_t *session)
{
	/*
	 * If in 3-way restore, the connection close is detected after
	 * check in tape_read(), the reader thread of mover may wait forever
	 * for the tape to be changed.  Force the reader thread to exit.
	 */
	nlp_event_rv_set(session, -2);
	nlp_event_nw(session);

	ndmpd_abort_marking_v2(session);
	ndmp_stop_buffer_worker(session);
	ndmp_waitfor_op(session);
	ndmp_free_reader_writer_ipc(session);
	if (session->ns_ndmp_lbr_params) {
		if (session->ns_ndmp_lbr_params->nlp_bkmap != -1)
			(void) dbm_free(session->ns_ndmp_lbr_params->nlp_bkmap);
		tlm_release_list(session->ns_ndmp_lbr_params->nlp_exl);
		tlm_release_list(session->ns_ndmp_lbr_params->nlp_inc);
		(void) cond_destroy(&session->ns_ndmp_lbr_params->nlp_cv);
	}

	NDMP_FREE(session->ns_ndmp_lbr_params);
}


/*
 * nlp_ref_nw
 *
 * Increase the references to the NDMP/LBR parameter to prevent
 * unwanted release
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   void
 */
void
nlp_ref_nw(ndmpd_session_t *session)
{
	ndmp_lbr_params_t *nlp;

	(void) mutex_lock(&nlp_mtx);
	if ((nlp = ndmp_get_nlp(session)) != NULL) {
		nlp->nlp_nw++;
	}
	(void) mutex_unlock(&nlp_mtx);
}


/*
 * nlp_unref_nw
 *
 * Decrease the references to the NDMP/LBR parameter before
 * release
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   void
 */
void
nlp_unref_nw(ndmpd_session_t *session)
{
	ndmp_lbr_params_t *nlp;

	(void) mutex_lock(&nlp_mtx);
	if ((nlp = ndmp_get_nlp(session)) != NULL) {
		if (nlp->nlp_nw > 0)
			nlp->nlp_nw--;
	}
	(void) mutex_unlock(&nlp_mtx);
}


/*
 * nlp_wait_nw
 *
 * Wait for a NDMP/LBR parameter to get available
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   void
 */
void
nlp_wait_nw(ndmpd_session_t *session)
{
	ndmp_lbr_params_t *nlp;
	static int wait = 0;

	(void) mutex_lock(&nlp_mtx);
	if ((nlp = ndmp_get_nlp(session)) != NULL) {
		if (nlp->nlp_nw > 0) {
			if (wait++ == 0)
				NDMP_LOG(LOG_DEBUG, "Waiting");
			while ((nlp->nlp_flag & NLP_READY) == 0)
				(void) cond_wait(&nlp->nlp_cv, &nlp_mtx);
		}
	}
	(void) mutex_unlock(&nlp_mtx);
}


/*
 * nlp_event_nw
 *
 * Signal that a NDMP/LBR parameter is available to wake up the
 * threads waiting on that
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   void
 */
void
nlp_event_nw(ndmpd_session_t *session)
{
	ndmp_lbr_params_t *nlp;

	(void) mutex_lock(&nlp_mtx);
	if ((nlp = ndmp_get_nlp(session)) != NULL) {
		if (nlp->nlp_nw > 0) {
			nlp->nlp_flag |= NLP_READY;
			(void) cond_signal(&nlp->nlp_cv);
		}
	}
	(void) mutex_unlock(&nlp_mtx);
}


/*
 * nlp_event_rv_get
 *
 * Get the return value for each NLP
 *
 * Parameters:
 *   session (input) - session pointer.
 *
 * Returns:
 *   return value
 */
int
nlp_event_rv_get(ndmpd_session_t *session)
{
	ndmp_lbr_params_t *nlp;

	if ((nlp = ndmp_get_nlp(session)) == NULL) {
		return (0);
	}

	return (nlp->nlp_rv);
}


/*
 * nlp_event_rv_set
 *
 * Set the return value for an NLP
 *
 * Parameters:
 *   session (input) - session pointer.
 *   rv (input) - return value
 *
 * Returns:
 *   void
 */
void
nlp_event_rv_set(ndmpd_session_t *session,
    int rv)
{
	ndmp_lbr_params_t *nlp;

	(void) mutex_lock(&nlp_mtx);

	if ((nlp = ndmp_get_nlp(session)) != NULL)
		nlp->nlp_rv = rv;
	(void) mutex_unlock(&nlp_mtx);
}

/*
 * is_buffer_erroneous
 *
 * Run a sanity check on the buffer
 *
 * returns:
 *   TRUE: if the buffer seems to have error
 *   FALSE: if the buffer is full and has valid data.
 */
boolean_t
is_buffer_erroneous(tlm_buffer_t *buf)
{
	boolean_t rv;

	rv = (buf == NULL || buf->tb_eot || buf->tb_eof ||
	    buf->tb_errno != 0);
	if (rv) {
		if (buf == NULL) {
			NDMP_LOG(LOG_ERR, "buf == NULL");
		} else {
			NDMP_LOG(LOG_DEBUG, "eot: %u, eof: %u, errno: %d",
			    buf->tb_eot, buf->tb_eof, buf->tb_errno);
		}
	}

	return (rv);
}

/*
 * ndmp_execute_cdb
 *
 * Main SCSI CDB execution program, this is used by message handler
 * for the NDMP tape/SCSI execute CDB requests. This function uses
 * USCSI interface to run the CDB command and sets all the CDB parameters
 * in the SCSI query before calling the USCSI ioctl. The result of the
 * CDB is returned in two places:
 *    cmd.uscsi_status		The status of CDB execution
 *    cmd.uscsi_rqstatus	The status of sense requests
 *    reply.error		The general errno (ioctl)
 *
 * Parameters:
 *   session (input) - session pointer
 *   adapter_name (input) - name of SCSI adapter
 *   sid (input) - SCSI target ID
 *   lun (input) - LUN number
 *   request (input) - NDMP client CDB request
 *
 * Returns:
 *   void
 */
/*ARGSUSED*/
void
ndmp_execute_cdb(ndmpd_session_t *session, char *adapter_name, int sid, int lun,
    ndmp_execute_cdb_request *request)
{
	ndmp_execute_cdb_reply reply;
	struct uscsi_cmd cmd;
	int fd;
	struct open_list *olp;
	char rq_buf[255];

	(void) memset((void *)&cmd, 0, sizeof (cmd));
	(void) memset((void *)&reply, 0, sizeof (reply));
	(void) memset((void *)rq_buf, 0, sizeof (rq_buf));

	if (request->flags == NDMP_SCSI_DATA_IN) {
		cmd.uscsi_flags = USCSI_READ | USCSI_RQENABLE;
		if ((cmd.uscsi_bufaddr =
		    ndmp_malloc(request->datain_len)) == 0) {
			reply.error = NDMP_NO_MEM_ERR;
			(void) ndmp_send_response(session->ns_connection,
			    NDMP_NO_ERR, (void *)&reply);
			return;
		}

		cmd.uscsi_buflen = request->datain_len;
	} else if (request->flags == NDMP_SCSI_DATA_OUT) {
		cmd.uscsi_flags = USCSI_WRITE | USCSI_RQENABLE;
		cmd.uscsi_bufaddr = request->dataout.dataout_val;
		cmd.uscsi_buflen = request->dataout.dataout_len;
	} else {
		cmd.uscsi_flags = USCSI_RQENABLE;
		cmd.uscsi_bufaddr = 0;
		cmd.uscsi_buflen = 0;
	}
	cmd.uscsi_rqlen = sizeof (rq_buf);
	cmd.uscsi_rqbuf = rq_buf;

	cmd.uscsi_timeout = (request->timeout < 1000) ?
	    1 : (request->timeout / 1000);

	cmd.uscsi_cdb = (caddr_t)request->cdb.cdb_val;
	cmd.uscsi_cdblen = request->cdb.cdb_len;

	if (request->cdb.cdb_len > 12) {
		reply.error = NDMP_ILLEGAL_ARGS_ERR;
		ndmp_send_reply(session->ns_connection, (void *) &reply,
		    "sending execute_cdb reply");
		if (request->flags == NDMP_SCSI_DATA_IN)
			free(cmd.uscsi_bufaddr);
		return;
	}

	reply.error = NDMP_NO_ERR;

	if ((olp = ndmp_open_list_find(adapter_name, sid, lun)) != NULL) {
		fd = olp->ol_fd;
	} else {
		reply.error = NDMP_DEV_NOT_OPEN_ERR;
		ndmp_send_reply(session->ns_connection, (void *) &reply,
		    "sending execute_cdb reply");
		if (request->flags == NDMP_SCSI_DATA_IN)
			free(cmd.uscsi_bufaddr);
		return;
	}

	if (ioctl(fd, USCSICMD, &cmd) < 0) {
		if (errno != EIO && errno != 0)
			NDMP_LOG(LOG_ERR,
			    "Failed to send command to device: %m");
		if (cmd.uscsi_status == 0)
			reply.error = NDMP_IO_ERR;
	}

	reply.status = cmd.uscsi_status;

	if (request->flags == NDMP_SCSI_DATA_IN) {
		reply.datain.datain_len = cmd.uscsi_buflen;
		reply.datain.datain_val = cmd.uscsi_bufaddr;
	} else {
		reply.dataout_len = request->dataout.dataout_len;
	}

	reply.ext_sense.ext_sense_len = cmd.uscsi_rqlen - cmd.uscsi_rqresid;
	reply.ext_sense.ext_sense_val = rq_buf;

	(void) ndmp_send_response(session->ns_connection, NDMP_NO_ERR,
	    (void *)&reply);

	if (request->flags == NDMP_SCSI_DATA_IN)
		free(cmd.uscsi_bufaddr);
}


/*
 * ndmp_stop_local_reader
 *
 * Stops a mover reader thread (for local backup only)
 *
 * Parameters:
 *   session (input) - session pointer
 *   cmds (input) - reader/writer command struct
 *
 * Returns:
 *   void
 */
void
ndmp_stop_local_reader(ndmpd_session_t *session, tlm_commands_t *cmds)
{
	if (session != NULL) {
		if (session->ns_data.dd_sock == -1) {
			/*
			 * 2-way restore.
			 */
			NDMP_LOG(LOG_DEBUG, "2-way restore");
			if (cmds != NULL && cmds->tcs_reader_count > 0) {
				nlp_event_rv_set(session, -2);
				nlp_event_nw(session);
			}
		}
	}
}


/*
 * Stops a mover reader thread (for remote backup only)
 *
 * Parameters:
 *   session (input) - session pointer
 *   cmds (input) - reader/writer command struct
 *
 * Returns:
 *   void
 */
void
ndmp_stop_remote_reader(ndmpd_session_t *session)
{
	if (session != NULL) {
		if (session->ns_data.dd_sock >= 0) {
			/*
			 * 3-way restore.
			 */
			NDMP_LOG(LOG_DEBUG,
			    "data.sock: %d", session->ns_data.dd_sock);
			(void) close(session->ns_data.dd_sock);
			session->ns_data.dd_sock = -1;
		}
	}
}


/*
 * ndmp_wait_for_reader
 *
 * Wait for a reader until get done (busy wait)
 */
void
ndmp_wait_for_reader(tlm_commands_t *cmds)
{
	if (cmds == NULL) {
		NDMP_LOG(LOG_ERR, "cmds == NULL");
	} else {
		if (cmds->tcs_reader_count > 0)
			NDMP_LOG(LOG_DEBUG,
			    "Waiting for readers: %d", cmds->tcs_reader_count);

		while (cmds->tcs_reader_count > 0)
			(void) sleep(1);
	}
}


/*
 * ndmp_open_list_find
 *
 * Find a specific device in the open list
 *
 * Parameters:
 *   dev (input) - device name
 *   sid (input) - SCSI target ID
 *   lun (input) - LUN number
 *
 * Returns:
 *   pointer to the open list entry
 */
struct open_list *
ndmp_open_list_find(char *dev, int sid, int lun)
{
	struct ol_head *olhp;
	struct open_list *olp;

	if (dev == NULL || *dev == '\0') {
		NDMP_LOG(LOG_ERR, "Invalid argument");
		return (NULL);
	}

	(void) mutex_lock(&ol_mutex);
	olhp = &ol_head;
	for (olp = LIST_FIRST(olhp); olp != NULL; olp = LIST_NEXT(olp, ol_q))
		if (strcmp(olp->ol_devnm, dev) == 0 && olp->ol_sid == sid &&
		    olp->ol_lun == lun) {
			(void) mutex_unlock(&ol_mutex);
			return (olp);
		}

	(void) mutex_unlock(&ol_mutex);
	return (NULL);
}


/*
 * ndmp_open_list_add
 *
 * Add a specific device to the open list
 *
 * Parameters:
 *   conn (input) - connection pointer
 *   dev (input) - device name
 *   sid (input) - SCSI target ID
 *   lun (input) - LUN number
 *   fd (input) - the device file descriptor
 *
 * Returns:
 *   errno
 */
int
ndmp_open_list_add(ndmp_connection_t *conn, char *dev, int sid, int lun, int fd)
{
	int err;
	struct ol_head *olhp;
	struct open_list *olp;

	if (dev == NULL || *dev == '\0') {
		NDMP_LOG(LOG_ERR, "Invalid argument");
		return (EINVAL);
	}

	err = 0;
	olhp = &ol_head;

	if ((olp = ndmp_open_list_find(dev, sid, lun)) != NULL) {
		/*
		 * The adapter handle can be opened many times by the clients.
		 * Only when the target is set, we must check and reject the
		 * open request if the device is already being used by another
		 * session.
		 */
		if (sid == -1)
			olp->ol_nref++;
		else
			err = EBUSY;
	} else if ((olp = ndmp_malloc(sizeof (struct open_list))) == NULL) {
		err = ENOMEM;
	} else if ((olp->ol_devnm = strdup(dev)) == NULL) {
		NDMP_LOG(LOG_ERR, "Out of memory.");
		free(olp);
		err = ENOMEM;
	} else {
		olp->cl_conn = conn;
		olp->ol_nref = 1;
		olp->ol_sid = sid;
		olp->ol_lun = lun;
		if (fd > 0)
			olp->ol_fd = fd;
		else
			olp->ol_fd = -1;
		(void) mutex_lock(&ol_mutex);
		LIST_INSERT_HEAD(olhp, olp, ol_q);
		(void) mutex_unlock(&ol_mutex);
	}

	return (err);
}


/*
 * ndmp_open_list_del
 *
 * Delete a specific device from the open list
 *
 * Parameters:
 *   dev (input) - device name
 *   sid (input) - SCSI target ID
 *   lun (input) - LUN number
 *
 * Returns:
 *   errno
 */
int
ndmp_open_list_del(char *dev, int sid, int lun)
{
	struct open_list *olp;

	if (dev == NULL || *dev == '\0') {
		NDMP_LOG(LOG_ERR, "Invalid argument");
		return (EINVAL);
	}
	if ((olp = ndmp_open_list_find(dev, sid, lun)) == NULL) {
		NDMP_LOG(LOG_ERR, "%s not found", dev);
		return (ENOENT);
	}

	(void) mutex_lock(&ol_mutex);
	if (--olp->ol_nref <= 0) {
		LIST_REMOVE(olp, ol_q);
		free(olp->ol_devnm);
		free(olp);
	}
	(void) mutex_unlock(&ol_mutex);

	return (0);
}


/*
 * ndmp_open_list_release
 *
 * Close all the resources belonging to this connection.
 *
 * Parameters:
 *    ndmp_connection_t *conn : connection identifier
 *
 * Returns:
 *   void
 */
void
ndmp_open_list_release(ndmp_connection_t *conn)
{
	struct ol_head *olhp = &ol_head;
	struct open_list *olp;
	struct open_list *next;

	(void) mutex_lock(&ol_mutex);
	olp = LIST_FIRST(olhp);
	while (olp != NULL) {
		next = LIST_NEXT(olp, ol_q);
		if (olp->cl_conn == conn) {
			LIST_REMOVE(olp, ol_q);
			if (olp->ol_fd > 0)
				(void) close(olp->ol_fd);
			free(olp->ol_devnm);
			free(olp);
		}
		olp = next;
	}
	(void) mutex_unlock(&ol_mutex);
}


/*
 * ndmp_stop_buffer_worker
 *
 * Stop all reader and writer threads for a specific buffer.
 *
 * Parameters:
 *   session (input) - session pointer
 *
 * Returns:
 *   void
 */
void
ndmp_stop_buffer_worker(ndmpd_session_t *session)
{
	ndmp_lbr_params_t *nlp;
	tlm_commands_t *cmds;
	static int wait = 0;

	session->ns_tape.td_pos = 0;
	if ((nlp = ndmp_get_nlp(session)) == NULL) {
		NDMP_LOG(LOG_ERR, "nlp == NULL");
	} else {
		cmds = &nlp->nlp_cmds;
		if (cmds->tcs_command) {
			cmds->tcs_reader = cmds->tcs_writer = TLM_ABORT;
			cmds->tcs_command->tc_reader = TLM_ABORT;
			cmds->tcs_command->tc_writer = TLM_ABORT;
			while (cmds->tcs_reader_count > 0 ||
			    cmds->tcs_writer_count > 0) {
				if (wait++ == 0)
					NDMP_LOG(LOG_DEBUG,
					    "Trying to stop buffer worker");
				(void) sleep(1);
			}
		}
	}
}


/*
 * ndmp_stop_reader_thread
 *
 * Stop only the reader threads of a specific buffer
 *
 * Parameters:
 *   session (input) - session pointer
 *
 * Returns:
 *   void
 */
void
ndmp_stop_reader_thread(ndmpd_session_t *session)
{
	ndmp_lbr_params_t *nlp;
	tlm_commands_t *cmds;
	static int wait = 0;

	if ((nlp = ndmp_get_nlp(session)) == NULL) {
		NDMP_LOG(LOG_ERR, "nlp == NULL");
	} else {
		cmds = &nlp->nlp_cmds;
		if (cmds->tcs_command == NULL) {
			NDMP_LOG(LOG_ERR, "cmds->tcs_command == NULL");
		} else {
			cmds->tcs_reader = TLM_ABORT;
			cmds->tcs_command->tc_reader = TLM_ABORT;
			while (cmds->tcs_reader_count > 0) {
				if (wait++ == 0)
					NDMP_LOG(LOG_DEBUG,
					    "Trying to stop reader thread");
				(void) sleep(1);
			}
		}
	}
}


/*
 * ndmp_stop_reader_thread
 *
 * Stop only the writer threads of a specific buffer
 *
 * Parameters:
 *   session (input) - session pointer
 *
 * Returns:
 *   void
 */
void
ndmp_stop_writer_thread(ndmpd_session_t *session)
{
	ndmp_lbr_params_t *nlp;
	tlm_commands_t *cmds;
	static int wait = 0;

	if ((nlp = ndmp_get_nlp(session)) == NULL) {
		NDMP_LOG(LOG_ERR, "nlp == NULL");
	} else {
		cmds = &nlp->nlp_cmds;
		if (cmds->tcs_command == NULL) {
			NDMP_LOG(LOG_ERR, "cmds->tcs_command == NULL");
		} else {
			cmds->tcs_writer = TLM_ABORT;
			cmds->tcs_command->tc_writer = TLM_ABORT;
			while (cmds->tcs_writer_count > 0) {
				if (wait ++ == 0)
					NDMP_LOG(LOG_DEBUG,
					    "trying to stop writer thread");
				(void) sleep(1);
			}
		}
	}
}


/*
 * ndmp_free_reader_writer_ipc
 *
 * Free and release the reader/writer buffers and the IPC structure
 * for reader and writer threads.
 *
 * Parameters:
 *   session (input) - session pointer
 *
 * Returns:
 *   void
 */
void
ndmp_free_reader_writer_ipc(ndmpd_session_t *session)
{
	ndmp_lbr_params_t *nlp;
	tlm_commands_t *cmds;

	if ((nlp = ndmp_get_nlp(session)) != NULL) {
		cmds = &nlp->nlp_cmds;
		if (cmds->tcs_command != NULL) {
			NDMP_LOG(LOG_ERR, "cmds->tcs_command->tc_ref: %d",
			    cmds->tcs_command->tc_ref);
			tlm_release_reader_writer_ipc(cmds->tcs_command);
		}
	}
}


/*
 * ndmp_waitfor_op
 *
 * Wait for a session reference count to drop to zero
 *
 * Parameters:
 *   session (input) - session pointer
 *
 * Returns:
 *   void
 */
void
ndmp_waitfor_op(ndmpd_session_t *session)
{
	static int wait = 0;
	if (session != NULL) {
		while (session->ns_nref > 0) {
			if (wait++ == 0)
				NDMP_LOG(LOG_DEBUG,
				    "waiting for session nref: %d",
				    session->ns_nref);
			(void) sleep(1);
		}
	}
}


/*
 * ndmp_session_ref
 *
 * Increment the reference count of the session
 *
 * Parameters:
 *   session (input) - session pointer
 *
 * Returns:
 *   void
 */
void
ndmp_session_ref(ndmpd_session_t *session)
{
	(void) mutex_lock(&session->ns_lock);
	session->ns_nref++;
	(void) mutex_unlock(&session->ns_lock);
}


/*
 * ndmp_session_unref
 *
 * Decrement the reference count of the session
 *
 * Parameters:
 *   session (input) - session pointer
 *
 * Returns:
 *   void
 */
void
ndmp_session_unref(ndmpd_session_t *session)
{
	(void) mutex_lock(&session->ns_lock);
	session->ns_nref--;
	(void) mutex_unlock(&session->ns_lock);
}


/*
 * ndmp_addr2str_v3
 *
 * Convert the address type to a string
 *
 * Parameters:
 *   type (input) - address type
 *
 * Returns:
 *   type in string
 */
char *
ndmp_addr2str_v3(ndmp_addr_type type)
{
	char *rv;

	switch (type) {
	case NDMP_ADDR_LOCAL:
		rv = "Local";
		break;
	case NDMP_ADDR_TCP:
		rv = "TCP";
		break;
	case NDMP_ADDR_FC:
		rv = "FC";
		break;
	case NDMP_ADDR_IPC:
		rv = "IPC";
		break;
	default:
		rv = "Unknown";
	}

	return (rv);
}


/*
 * ndmp_valid_v3addr_type
 *
 * Make sure that the NDMP address is from any of the
 * valid types
 *
 * Parameters:
 *   type (input) - address type
 *
 * Returns:
 *   1: valid
 *   0: invalid
 */
boolean_t
ndmp_valid_v3addr_type(ndmp_addr_type type)
{
	boolean_t rv;

	switch (type) {
	case NDMP_ADDR_LOCAL:
	case NDMP_ADDR_TCP:
	case NDMP_ADDR_FC:
	case NDMP_ADDR_IPC:
		rv = TRUE;
		break;
	default:
		rv = FALSE;
	}

	return (rv);
}


/*
 * ndmp_copy_addr_v3
 *
 * Copy NDMP address from source to destination (V2 and V3 only)
 *
 * Parameters:
 *   dst (ouput) - destination address
 *   src (input) - source address
 *
 * Returns:
 *   void
 */
void
ndmp_copy_addr_v3(ndmp_addr_v3 *dst, ndmp_addr_v3 *src)
{
	dst->addr_type = src->addr_type;
	switch (src->addr_type) {
	case NDMP_ADDR_LOCAL:
		/* nothing */
		break;
	case NDMP_ADDR_TCP:
		dst->tcp_ip_v3 = htonl(src->tcp_ip_v3);
		dst->tcp_port_v3 = src->tcp_port_v3;
		break;
	case NDMP_ADDR_FC:
	case NDMP_ADDR_IPC:
	default:
		break;
	}
}


/*
 * ndmp_copy_addr_v4
 *
 * Copy NDMP address from source to destination. V4 has a extra
 * environment list inside the address too which needs to be copied.
 *
 * Parameters:
 *   dst (ouput) - destination address
 *   src (input) - source address
 *
 * Returns:
 *   void
 */
void
ndmp_copy_addr_v4(ndmp_addr_v4 *dst, ndmp_addr_v4 *src)
{
	int i;

	dst->addr_type = src->addr_type;
	dst->tcp_len_v4 = src->tcp_len_v4;
	switch (src->addr_type) {
	case NDMP_ADDR_LOCAL:
		/* nothing */
		break;
	case NDMP_ADDR_TCP:
		dst->tcp_addr_v4 = ndmp_malloc(sizeof (ndmp_tcp_addr_v4) *
		    src->tcp_len_v4);
		if (dst->tcp_addr_v4 == 0)
			return;

		for (i = 0; i < src->tcp_len_v4; i++) {
			dst->tcp_ip_v4(i) = htonl(src->tcp_ip_v4(i));
			dst->tcp_port_v4(i) = src->tcp_port_v4(i);
			dst->tcp_env_v4(i).addr_env_len = 0; /* Solaris */
			dst->tcp_env_v4(i).addr_env_val = 0; /* Solaris */
		}
		break;
	case NDMP_ADDR_FC:
	case NDMP_ADDR_IPC:
	default:
		break;
	}
}


/*
 * ndmp_connect_sock_v3
 *
 * Creates a socket and connects to the specified address/port
 *
 * Parameters:
 *   addr (input) - IP address
 *   port (input) - port number
 *
 * Returns:
 *   0: on success
 *  -1: otherwise
 */
int
ndmp_connect_sock_v3(ulong_t addr, ushort_t port)
{
	int sock;
	struct sockaddr_in sin;
	int flag = 1;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		NDMP_LOG(LOG_ERR, "Socket error: %m");
		return (-1);
	}

	(void) memset((void *) &sin, 0, sizeof (sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(addr);
	sin.sin_port = htons(port);
	if (connect(sock, (struct sockaddr *)&sin, sizeof (sin)) < 0) {
		NDMP_LOG(LOG_ERR, "Connection error: %m");
		(void) close(sock);
		sock = -1;
	} else {
		if (ndmp_sbs > 0)
			ndmp_set_socket_snd_buf(sock, ndmp_sbs*KILOBYTE);
		if (ndmp_rbs > 0)
			ndmp_set_socket_rcv_buf(sock, ndmp_rbs*KILOBYTE);

		ndmp_set_socket_nodelay(sock);
		(void) setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &flag,
		    sizeof (flag));
	}

	return (sock);
}

/*
 * ndmp_create_socket
 *
 * Creates a socket for listening for accepting data connections.
 *
 * Parameters:
 *   session (input)  - session pointer.
 *   addr    (output) - location to store address of socket.
 *   port    (output) - location to store port of socket.
 *
 * Returns:
 *   0 - success.
 *  -1 - error.
 */
int
ndmp_create_socket(ulong_t *addr, ushort_t *port)
{
	char *p;
	socklen_t length;
	int sd;
	struct sockaddr_in sin;

	/* Try the user's prefered NIC IP address */
	p = ndmpd_get_prop(NDMP_MOVER_NIC);

	/* Try host's IP address */
	if (!p || *p == 0)
		p = gethostaddr();

	/* Try default NIC's IP address (if DNS failed) */
	if (!p)
		p = get_default_nic_addr();

	/* Fail if no IP can be obtained */
	if (!p) {
		NDMP_LOG(LOG_ERR, "Undetermined network port.");
		return (-1);
	}

	*addr = inet_addr(p);

	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
		NDMP_LOG(LOG_ERR, "Socket error: %m");
		return (-1);
	}
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = 0;
	length = sizeof (sin);

	if (bind(sd, (struct sockaddr *)&sin, sizeof (sin)) < 0) {
		NDMP_LOG(LOG_ERR, "Bind error: %m");
		(void) close(sd);
		sd = -1;
	} else if (getsockname(sd, (struct sockaddr *)&sin, &length) < 0) {
		NDMP_LOG(LOG_ERR, "getsockname error: %m");
		(void) close(sd);
		sd = -1;
	} else if (listen(sd, 5) < 0) {
		NDMP_LOG(LOG_ERR, "Listen error: %m");
		(void) close(sd);
		sd = -1;
	} else
		*port = sin.sin_port;

	return (sd);
}


/*
 * cctime
 *
 * Convert the specified time into a string.  It's like
 * ctime(), but:
 *     - chops the trailing '\n' of ctime.
 *     - and returns "the epoch" if time is 0.
 *
 * Returns:
 *     "": invalid argument.
 *     "the epoch": if time is 0.
 *     string format of the time.
 */
char *
cctime(time_t *t)
{
	char *bp, *cp;
	static char tbuf[BUFSIZ];

	if (!t)
		return ("");

	if (*t == (time_t)0)
		return ("the epoch");

	if ((bp = ctime_r(t, tbuf, BUFSIZ)) == NULL)
		return ("");

	cp = strchr(bp, '\n');
	if (cp)
		*cp = '\0';

	return (bp);
}


/*
 * ndmp_new_job_name
 *
 * Create a job name for each backup/restore to keep track
 *
 * Parameters:
 *   jname (output) - job name
 *
 * Returns:
 *   jname
 */
char *
ndmp_new_job_name(char *jname)
{
	if (jname != NULL) {
		(void) snprintf(jname, TLM_MAX_BACKUP_JOB_NAME, "%s.%d",
		    NDMP_BASENAME, ndmp_job_cnt++);
	}

	return (jname);
}


/*
 * fs_is_valid_logvol
 *
 * Check if the log path exists
 *
 * Parameters:
 *   path (input) - log path
 *
 * Returns:
 *   FALSE: invalid
 *   TRUE: valid
 */
boolean_t
fs_is_valid_logvol(char *path)
{
	struct stat64 st;

	if (stat64(path, &st) < 0)
		return (FALSE);

	return (TRUE);
}


/*
 * ndmpd_mk_temp
 *
 * Make a temporary file using the working directory path and the
 * jobname
 *
 * Parameters:
 *   buf (output) - the temporary file name path
 *
 * Returns:
 *   buf
 */
char *
ndmpd_mk_temp(char *buf)
{
	char fname[TLM_MAX_BACKUP_JOB_NAME];
	const char *dir;
	char *rv;

	if (!buf)
		return (NULL);

	dir = ndmpd_get_prop(NDMP_DEBUG_PATH);
	if (dir == 0 || *dir == '\0') {
		NDMP_LOG(LOG_ERR, "NDMP work path not specified");
		return (0);
	}

	if (!fs_is_valid_logvol((char *)dir)) {
		NDMP_LOG(LOG_ERR,
		    "Log file path cannot be on system volumes.");
		return (0);
	}

	dir += strspn(dir, " \t");
	if (!*dir) {
		NDMP_LOG(LOG_ERR, "NDMP work path not specified");
		return (0);
	}

	rv = buf;
	(void) ndmp_new_job_name(fname);
	(void) tlm_cat_path(buf, (char *)dir, fname);

	return (rv);
}


/*
 * ndmpd_make_bk_dir_path
 *
 * Make a directory path for temporary files under the NDMP
 * working directory.
 *
 * Parameters:
 *   buf (output) - result path
 *   fname (input) - the file name
 *
 * Returns:
 *   buf
 */
char *
ndmpd_make_bk_dir_path(char *buf, char *fname)
{
	const char *p;
	char *name;
	char path[PATH_MAX];

	if (!buf || !fname || !*fname)
		return (NULL);

	p = ndmpd_get_prop(NDMP_DEBUG_PATH);
	if (p == NULL || *p == '\0' || !fs_is_valid_logvol((char *)p)) {
		return (NULL);
	}

	(void) strlcpy(path, (char *)p, PATH_MAX);
	(void) trim_whitespace(path);

	if ((name = strrchr(fname, '/')) == 0)
		name = fname;

	(void) tlm_cat_path(buf, path, name);
	return (buf);
}


/*
 * ndmp_is_chkpnt_root
 *
 * Is this a root checkpoint (snapshot) directory.
 * Note: a temporary function
 */
boolean_t
ndmp_is_chkpnt_root(char *path)
{
	struct stat64 st;

	if (stat64(path, &st) != 0) {
		NDMP_LOG(LOG_ERR, "Couldn't stat path \"%s\"", path);
		return (TRUE);
	}
	return (FALSE);
}


/*
 * ndmpd_make_exc_list
 *
 * Make a list of files that should not be backed up.
 *
 * Parameters:
 *   void
 *
 * Returns:
 *   list - array of character strings
 */
char **
ndmpd_make_exc_list(void)
{
	char *val, **cpp;
	int i, n;

	n = sizeof (exls);
	if ((cpp = ndmp_malloc(n)) != NULL) {
		for (i = 0; exls[i] != NULL; i++)
			cpp[i] = exls[i];

		/*
		 * If ndmpd_get_prop returns NULL, the array will be
		 * null-terminated.
		 */
		val = ndmpd_get_prop(NDMP_DEBUG_PATH);
		cpp[i] = val;
	}

	return (cpp);
}


/*
 * ndmp_get_bk_dir_ino
 *
 * Get the inode number of the backup directory
 */
int
ndmp_get_bk_dir_ino(ndmp_lbr_params_t *nlp)
{
	int rv;
	struct stat64 st;

	if (stat64(nlp->nlp_backup_path, &st) != 0) {
		rv = -1;
		NDMP_LOG(LOG_ERR, "Getting inode # of \"%s\"",
		    nlp->nlp_backup_path);
	} else {
		rv = 0;
		nlp->nlp_bkdirino = st.st_ino;
	}

	return (rv);
}


/*
 * ndmp_check_utf8magic
 *
 * Check if the magic string for exists in the tar header. This
 * magic string (which also indicates that the file names are in
 * UTF8 format) is used as a crest to indetify our own tapes.
 * This checking is always done before all restores except DAR
 * restores.
 */
boolean_t
ndmp_check_utf8magic(tlm_cmd_t *cmd)
{
	char *cp;
	int err, len, actual_size;

	if (cmd == NULL) {
		NDMP_LOG(LOG_ERR, "cmd == NULL");
		return (FALSE);
	}
	if (cmd->tc_buffers == NULL) {
		NDMP_LOG(LOG_ERR, "cmd->tc_buffers == NULL");
		return (FALSE);
	}

	/* wait until the first buffer gets full. */
	tlm_buffer_in_buf_wait(cmd->tc_buffers);

	err = actual_size = 0;
	cp = tlm_get_read_buffer(RECORDSIZE, &err, cmd->tc_buffers,
	    &actual_size);
	if (cp == NULL) {
		NDMP_LOG(LOG_ERR, "Can't read from buffers, err: %d", err);
		return (FALSE);
	}
	len = strlen(NDMPUTF8MAGIC);
	if (actual_size < len) {
		NDMP_LOG(LOG_ERR, "Not enough data in the buffers");
		return (FALSE);
	}

	return ((strncmp(cp, NDMPUTF8MAGIC, len) == 0) ? TRUE : FALSE);
}


/*
 * ndmp_get_cur_bk_time
 *
 * Get the backup checkpoint time.
 */
int
ndmp_get_cur_bk_time(ndmp_lbr_params_t *nlp, time_t *tp, char *jname)
{
	int err;

	if (!nlp || !nlp->nlp_backup_path || !tp) {
		NDMP_LOG(LOG_ERR, "Invalid argument");
		return (-1);
	}

	if (!fs_is_chkpnt_enabled(nlp->nlp_backup_path)) {
		*tp = time(NULL);
		return (0);
	}

	err = tlm_get_chkpnt_time(nlp->nlp_backup_path, !NLP_ISCHKPNTED(nlp),
	    tp, jname);
	if (err != 0) {
		NDMP_LOG(LOG_ERR, "Can't checkpoint time");
	}

	return (err);
}


/*
 * get_relative_path
 */
char *
ndmp_get_relative_path(char *base, char *fullpath)
{
	char *p = fullpath;

	if (!base || !*base)
		return (fullpath);

	while (*base) {
		if (*base != *p)
			break;
		p++; base++;
	}

	if (*p == '/')
		p++;

	return ((*base) ? fullpath : p);
}


/*
 * ndmp_get_nlp
 *
 * Get NDMP local backup parameters
 *
 * Parameter:
 *   session cooke
 *
 * Returns:
 *   LBR structure
 */
ndmp_lbr_params_t *
ndmp_get_nlp(void *cookie)
{
	if (cookie == NULL)
		return (NULL);

	return (((ndmpd_session_t *)cookie)->ns_ndmp_lbr_params);
}


/*
 * is_tape_unit_ready
 *
 * Check if the tape device is ready or not
 */
boolean_t
is_tape_unit_ready(char *adptnm, int dev_id)
{
	int try;
	int fd = 0;

	try = TUR_MAX_TRY;
	if (dev_id <= 0) {
		if ((fd = open(adptnm, O_RDONLY | O_NDELAY)) < 0)
			return (FALSE);
	} else {
		fd = dev_id;
	}
	do {
		if (scsi_test_unit_ready(fd) >= 0) {
			if (dev_id <= 0)
				(void) close(fd);

			return (TRUE);
		}

		(void) usleep(TUR_WAIT);
	} while (--try > 0);

	if (dev_id <= 0)
		(void) close(fd);
	return (FALSE);
}


/*
 * scsi_test_unit_ready
 *
 * This is for Test Unit Read, without this function, the only
 * impact is getting EBUSY's before each operation which we have
 * busy waiting loops checking EBUSY error code.
 */
static int
scsi_test_unit_ready(int dev_id)
{
	struct uscsi_cmd ucmd;
	union scsi_cdb cdb;
	int retval;

	(void) memset(&ucmd, 0, sizeof (struct uscsi_cmd));
	(void) memset(&cdb, 0, sizeof (union scsi_cdb));
	cdb.scc_cmd = SCMD_TEST_UNIT_READY;
	ucmd.uscsi_cdb = (caddr_t)&cdb;
	ucmd.uscsi_cdblen = CDB_GROUP0;
	ucmd.uscsi_flags |= USCSI_SILENT;
	ucmd.uscsi_timeout = 60;	/* Allow maximum 1 min */

	retval = ioctl(dev_id, USCSICMD, &ucmd);

	if (retval != 0 && errno != EIO) {
		NDMP_LOG(LOG_ERR,
		    "Failed to send inquiry request to device: %m.");
		retval = -errno;
	} else
		retval = -(ucmd.uscsi_status);

	return (retval);
}


/*
 * ndmp_load_params
 *
 * Load the parameters.
 *
 * Parameter:
 *   void
 *
 * Returns:
 *   void
 */
void
ndmp_load_params(void)
{
	ndmp_dump_path_node = ndmpd_get_prop_yorn(NDMP_DUMP_PATHNODE_ENV) ?
	    TRUE : FALSE;
	ndmp_tar_path_node = ndmpd_get_prop_yorn(NDMP_TAR_PATHNODE_ENV) ?
	    TRUE : FALSE;
	ndmp_ignore_ctime =
	    ndmpd_get_prop_yorn(NDMP_IGNCTIME_ENV) ? TRUE : FALSE;
	ndmp_include_lmtime = ndmpd_get_prop_yorn(NDMP_INCLMTIME_ENV) ?
	    TRUE : FALSE;
	ndmp_max_tok_seq = atoi(ndmpd_get_prop_default(NDMP_MAXSEQ_ENV, "9"));

	ndmp_full_restore_path = ndmpd_get_prop_yorn(NDMP_FULL_RESTORE_PATH) ?
	    TRUE : FALSE;

	ndmp_fhinode = ndmpd_get_prop_yorn(NDMP_FHIST_INCR_ENV) ? TRUE : FALSE;

	/* Get the value from ndmp SMF property. */
	ndmp_dar_support = ndmpd_get_prop_yorn(NDMP_DAR_SUPPORT);

	if ((ndmp_ver = atoi(ndmpd_get_prop(NDMP_VERSION_ENV))) == 0)
		ndmp_ver = NDMPVER;
}

/*
 * randomize
 *
 * Randomize the contents of a buffer
 *
 * Parameter:
 *   buffer (output) - destination buffer
 *   size (input) - buffer size
 *
 * Returns:
 *   void
 */
void
randomize(unsigned char *buffer, int size)
{
	/* LINTED improper alignment */
	unsigned int *p = (unsigned int *)buffer;
	unsigned int dwlen = size / sizeof (unsigned int);
	unsigned int remlen = size % sizeof (unsigned int);
	unsigned int tmp;
	unsigned int i;

	for (i = 0; i < dwlen; i++)
		*p++ = random();

	if (remlen) {
		tmp = random();
		(void) memcpy(p, &tmp, remlen);
	}
}

/*
 * ndmpd_get_file_entry_type
 *
 * Converts the mode to the NDMP file type
 *
 * Parameter:
 *   mode (input) - file mode
 *   ftype (output) - file type
 *
 * Returns:
 *   void
 */
void
ndmpd_get_file_entry_type(int mode, ndmp_file_type *ftype)
{
	switch (mode & S_IFMT) {
	case S_IFIFO:
		*ftype = NDMP_FILE_FIFO;
		break;
	case S_IFCHR:
		*ftype = NDMP_FILE_CSPEC;
		break;
	case S_IFDIR:
		*ftype = NDMP_FILE_DIR;
		break;
	case S_IFBLK:
		*ftype = NDMP_FILE_BSPEC;
		break;
	case S_IFREG:
		*ftype = NDMP_FILE_REG;
		break;
	case S_IFLNK:
		*ftype = NDMP_FILE_SLINK;
		break;
	default:
		*ftype = NDMP_FILE_SOCK;
		break;
	}
}

/*
 * ndmpd_post_sysevent
 *
 * Posts a sysevent for the beginning or completion of an NDMP operation.
 *
 * Parameters:
 *   backup (input) - true if backup operation, false for restore
 *   done (input) - true if completion, false for start
 *   sp (input) - the current ndmp session whose operation started or
 *     completed
 *   error (input) - error code for the operation
 *
 * Returns:
 *   void
 */
void
ndmpd_post_sysevent(boolean_t backup, boolean_t done, ndmpd_session_t *sp,
    int error)
{
	int ii, sid;
	time_t now;
	sysevent_id_t eid;
	nvlist_t *nvl = NULL;
	char *path = NULL, *level = NULL, *sc, *source;
	const char *failure;
	char client[NDMP_CL_ADDR_LEN];

	for (ii = 0; ii < sp->ns_data.dd_env_len; ++ii) {
		if (strcmp("FILESYSTEM", sp->ns_data.dd_env[ii].name) == 0)
			path = sp->ns_data.dd_env[ii].value;

		if (backup && strcmp("LEVEL", sp->ns_data.dd_env[ii].name) == 0)
			level = sp->ns_data.dd_env[ii].value;
	}

	assert(path != NULL);
	ndmp_connect_get_client(sp, &sid, client, NDMP_CL_ADDR_LEN);
	now = time(NULL);
	failure = (error == 0 ? "" : ndmp_strerror(error));
	if ((source = getenv("SMF_FMRI")) == NULL)
		source = "ndmpd";

	if (backup)
		sc = done ? ESC_NDMP_BACKUP_FINISH : ESC_NDMP_BACKUP_START;
	else
		sc = done ? ESC_NDMP_RESTORE_FINISH : ESC_NDMP_RESTORE_START;

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0 ||
	    (backup && level != NULL &&
	    nvlist_add_string(nvl, "level", level) != 0) ||
	    nvlist_add_string(nvl, "source", source) != 0 ||
	    nvlist_add_int32(nvl, "sid", sid) != 0 ||
	    nvlist_add_string(nvl, "path", path) != 0 ||
	    nvlist_add_int64(nvl, "timestamp", now) != 0 ||
	    nvlist_add_string(nvl, "client", client) != 0 ||
	    (done && nvlist_add_string(nvl, "failure", failure) != 0) ||
	    sysevent_post_event(EC_NDMP, sc, SUNW_VENDOR, "ndmp", nvl,
	    &eid) != 0)
		NDMP_LOG(LOG_DEBUG, "failed to post sysevent");

	nvlist_free(nvl);
}

/*
 * Set a private data in the plugin context
 */
void
ndmp_context_set_specific(ndmp_context_t *nctx, void *ptr)
{
	nctx->nc_pldata = ptr;
}

/*
 * Get a private data in the plugin context
 */
void *
ndmp_context_get_specific(ndmp_context_t *nctx)
{
	return (nctx->nc_pldata);
}

ndmpd_backup_type_t
ndmp_get_backup_type(ndmp_context_t *ctx)
{
	ndmpd_session_t *session = (ndmpd_session_t *)ctx->nc_ddata;

	return (session->ns_butype);
}

/*
 * Check the SMF property fs-export to see if it's set to any value,
 * the mountpoint will be considered exported either if this value is null
 * or included as a comma separated value in this property
 */
boolean_t
fs_is_exported(char *mntpnt)
{
	char *exp;
	char *p, *s, *psave;

	if (!mntpnt || !*mntpnt)
		return (B_FALSE);

	if ((exp = ndmpd_get_prop(NDMP_FS_EXPORT)) == NULL || !*exp)
		return (B_TRUE);

	p = psave = strdup(exp);
	while (p) {
		if ((s = strpbrk(p, " ,\t")) != NULL)
			for (*s++ = 0; *s && IS_DELIM(*s); s++)
				;
		if (*p && strncasecmp(mntpnt, p, strlen(p)) == 0) {
			free(psave);
			return (B_TRUE);
		}

		p = s;
	}
	free(psave);

	return (B_FALSE);
}


/*
 * ************************************************************************
 * NDMP buf operations
 * ************************************************************************
 */

void
ndmpd_do_buf_cleanup(buf_list_t *buf_head, int count)
{
	int j;
	buf_list_t *bufp;
	buf_list_t *bhp;

	for (j = count, bhp = buf_head; (j > 0) && (bhp != NULL); j--) {
		bufp = bhp->bl_next;
		free(bhp->bl_bp);
		free(bhp);
		bhp = bufp;
	}
}


void
ndmpd_buf_init(ndmpd_buf_t *nmb)
{
	(void) pthread_mutex_init(&nmb->nzb_free_mutex, NULL);
	(void) pthread_mutex_init(&nmb->nzb_used_mutex, NULL);
	(void) pthread_cond_init(&nmb->nzb_used_cond, NULL);
	(void) pthread_cond_init(&nmb->nzb_free_cond, NULL);

	nmb->nzb_free_bufs = 0;
	nmb->nzb_used_bufs = 0;
	nmb->nzb_total_bufs = 0;
	nmb->nzb_buf_consumer_stop = 0;
	nmb->nzb_max_bufs_used = NDMPD_MAX_BUFS_CNT;

	nmb->nzb_free_head = NULL;
	nmb->nzb_used_head = NULL;
	nmb->nzb_used_tail = nmb->nzb_used_head;
}


void
ndmpd_buf_fini(ndmpd_buf_t *nmb)
{
	(void) pthread_cond_destroy(&nmb->nzb_used_cond);
	(void) pthread_cond_destroy(&nmb->nzb_free_cond);
	(void) pthread_mutex_destroy(&nmb->nzb_free_mutex);
	(void) pthread_mutex_destroy(&nmb->nzb_used_mutex);
	ndmpd_do_buf_cleanup(nmb->nzb_free_head, nmb->nzb_free_bufs);
	ndmpd_do_buf_cleanup(nmb->nzb_used_head, nmb->nzb_used_bufs);
}


int
ndmpd_alloc_more_free_bufs(buf_list_t **buf_head, int *count, int size,
    ndmpd_buf_t *nmb)
{
	int i;
	int num_alloced = 0;
	buf_list_t *bhp, *bufp;

	bhp = ndmp_malloc(sizeof (buf_list_t));
	if (bhp == NULL) {
		return (1);
	}
	bhp->bl_bp = malloc(size);
	if (bhp->bl_bp == NULL) {
		free(bhp);
		return (1);
	}
	num_alloced++;
	for (i = 0, bufp = bhp; i < (NDMPD_BUF_CNT - 1); i++) {
		bufp->bl_next = ndmp_malloc(sizeof (buf_list_t));
		bufp = bufp->bl_next;
		if (bufp == NULL) {
			ndmpd_do_buf_cleanup(bhp, num_alloced);
			return (1);
		}
		bufp->bl_bp = malloc(size);
		if (bufp->bl_bp == NULL) {
			free(bufp);
			ndmpd_do_buf_cleanup(bhp, num_alloced);
			return (1);
		}
		num_alloced++;
	}
	*buf_head = bhp;
	*count += num_alloced;
	nmb->nzb_total_bufs += num_alloced;
	return (0);
}


int
ndmpd_get_used_buf(ndmpd_buf_t *nmb, buf_list_t **bl)
{
	buf_list_t	*bl_ptr;

	/* Removed from used list */
	(void) pthread_mutex_lock(&nmb->nzb_used_mutex);
	while (nmb->nzb_used_head == NULL) {
		if (nmb->nzb_buf_consumer_stop == 1) {
			(void) pthread_mutex_unlock(
			    &nmb->nzb_used_mutex);
			return (1);
		}
		(void) pthread_cond_wait(&nmb->nzb_used_cond,
		    &nmb->nzb_used_mutex);
	}
	*bl = bl_ptr = nmb->nzb_used_head;
	if (bl_ptr->bl_next != NULL) {
		bl_ptr->bl_next->bl_prev = NULL;
	}
	nmb->nzb_used_head = bl_ptr->bl_next;
	nmb->nzb_used_bufs--;
	(void) pthread_mutex_unlock(&nmb->nzb_used_mutex);
	return (0);
}


int
ndmpd_get_free_buf(ndmpd_buf_t *nmb, int bufsize, buf_list_t **bl)
{
	buf_list_t	*bl_ptr;

	/* Removed from free list */
	(void) pthread_mutex_lock(&nmb->nzb_free_mutex);
	while (nmb->nzb_free_head == NULL) {
		if (nmb->nzb_buf_consumer_stop == 1) {
			(void) pthread_mutex_unlock(
			    &nmb->nzb_free_mutex);
			return (1);
		}
		if (nmb->nzb_total_bufs <
		    nmb->nzb_max_bufs_used) {
			if (ndmpd_alloc_more_free_bufs(&nmb->nzb_free_head,
			    &nmb->nzb_free_bufs, bufsize, nmb) != 0) {
				/*
				 * failed to get the first block of memory
				 * buffers, since there are no buffers already
				 * allocated, can't wait for any so fail the
				 * backup.
				 */
				if (nmb->nzb_total_bufs == 0) {
					(void) pthread_mutex_unlock(
					    &nmb->nzb_free_mutex);
					return (-1);
				}
				/*
				 * allocation failed, just use existing buf
				 * set current value to total bufs used.
				 */
				nmb->nzb_max_bufs_used = nmb->nzb_total_bufs;
			}
		} else {
			/* Used up the buffers, wait for empty buffer */
			(void) pthread_cond_wait(&nmb->nzb_free_cond,
			    &nmb->nzb_free_mutex);
		}
	}
	*bl = bl_ptr = nmb->nzb_free_head;
	nmb->nzb_free_head = nmb->nzb_free_head->bl_next;
	nmb->nzb_free_bufs--;
	bl_ptr->bl_next = NULL;
	bl_ptr->bl_prev = NULL;
	(void) pthread_mutex_unlock(&nmb->nzb_free_mutex);
	return (0);
}


void
ndmpd_add_free_list(ndmpd_buf_t *nmb, buf_list_t *bl_ptr)
{
	(void) pthread_mutex_lock(&nmb->nzb_free_mutex);
	bl_ptr->bl_next = nmb->nzb_free_head;
	nmb->nzb_free_head = bl_ptr;
	nmb->nzb_free_bufs++;
	(void) pthread_cond_signal(&nmb->nzb_free_cond);
	(void) pthread_mutex_unlock(&nmb->nzb_free_mutex);
}


void
ndmpd_add_used_list(ndmpd_buf_t *nmb, buf_list_t *bl_ptr)
{
	(void) pthread_mutex_lock(&nmb->nzb_used_mutex);
	if (nmb->nzb_used_head == NULL) {
		nmb->nzb_used_head = bl_ptr;
		nmb->nzb_used_tail = bl_ptr;
	} else {
		bl_ptr->bl_prev = nmb->nzb_used_tail;
		nmb->nzb_used_tail->bl_next = bl_ptr;
		nmb->nzb_used_tail = bl_ptr;
	}
	nmb->nzb_used_bufs++;
	(void) pthread_cond_signal(&nmb->nzb_used_cond);
	(void) pthread_mutex_unlock(&nmb->nzb_used_mutex);
}

void
ndmpd_release_used_buf(ndmpd_buf_t *nmb)
{
	(void) pthread_mutex_lock(&nmb->nzb_used_mutex);
	nmb->nzb_buf_consumer_stop = 1;
	(void) pthread_cond_signal(&nmb->nzb_used_cond);
	(void) pthread_mutex_unlock(&nmb->nzb_used_mutex);
}

void
ndmpd_release_free_buf(ndmpd_buf_t *nmb)
{
	(void) pthread_mutex_lock(&nmb->nzb_free_mutex);
	nmb->nzb_buf_consumer_stop = 1;
	(void) pthread_cond_signal(&nmb->nzb_free_cond);
	(void) pthread_mutex_unlock(&nmb->nzb_free_mutex);
}

int
ndmpd_get_used_cnt(ndmpd_buf_t *nmb)
{
	int cnt;

	(void) pthread_mutex_lock(&nmb->nzb_used_mutex);
	cnt = nmb->nzb_used_bufs;
	(void) pthread_cond_signal(&nmb->nzb_used_cond);
	(void) pthread_mutex_unlock(&nmb->nzb_used_mutex);
	return (cnt);
}

/*
 * This is a "session-aware" version of pthread_create() which not only spawns
 * a new pthread with all normal properies, but also causes ndmp thread-specific
 * data to propagate from the "parent" thread to the "child" thread. Otherwise,
 * the behavior of a standard call to pthread_create is preserved.
 * ndmp_pthread_create() executes on the thread of its calling routine.
 * Note that indirect_start_routine() executes on the newly spawned thread.
 *
 * Parameters:
 *   all parameters identical to those of pthread_create()
 *
 * Returns:
 *   return value identical to that of pthread_create()
 */
int
ndmp_pthread_create(pthread_t *_RESTRICT_KYWD thread,
    const pthread_attr_t *_RESTRICT_KYWD attr,
    funct_t start_routine, void *_RESTRICT_KYWD arg)
{
	int err_ret;

	thread_data_t *new_thread_data = ndmp_malloc(sizeof (thread_data_t));
	if (new_thread_data != NULL) {
		thread_data_t *current_thread_data =
		    (thread_data_t *)pthread_getspecific(thread_data_key);
		/* This could only return NULL because of a coding error */
		assert(current_thread_data != NULL);
		(void) memcpy(new_thread_data, current_thread_data,
		    sizeof (thread_data_t));

		new_thread_data->start_routine = (funct_t)start_routine;
		new_thread_data->arg = arg;

		err_ret = pthread_create(thread, attr,
		    indirect_start_routine, new_thread_data);

		if (err_ret != 0) {
			/* don't allow a memory leak if thread create failed */
			free(new_thread_data);
		}
		return (err_ret);
	} else {
		/* It is an "out of resources" failure if can't malloc */
		return (EAGAIN);
	}
}

/*
 * Perform indirect execution of the start_routine that would normally be
 * executed directly upon use of pthread_create().  This allows us to
 * propagate the ndmp thread_data from "parent" to "child", transparently
 * to the code which called ndmp_pthread_create().
 * indirect_start_routine() executes on the newly spawned thread.  Note that
 * ndmp_pthread_create() executes on the creator thread (its calling routines).
 *
 * Parameters:
 *   all parameters identical to those of a pthread_create() start_routine()
 *   except that we use thread_data_t to hold the input arguments plus more.
 *
 * Returns:
 *   return value identical to that of a pthread_create() start_routine()
 */
static void *
indirect_start_routine(void *new_thread_data)
{
	void *ret_arg;
	thread_data_t *thread_data = (thread_data_t *)new_thread_data;

	/* setspecific is responsible for freeing thread_data from heap */
	(void) pthread_setspecific(thread_data_key, new_thread_data);

	/* Call the original start_routine, just as if by pthread_create() */
	ret_arg = thread_data->start_routine(thread_data->arg);

	/* Make the same return value as if returning from start_routine */
	return (ret_arg);
}
