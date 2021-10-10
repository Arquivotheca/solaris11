/*
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
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
#include <sys/param.h>
#include <sys/lwp.h>
#include <sys/fs/zfs.h>
#include <sys/mtio.h>
#include <sys/time.h>
#include <sys/processor.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <libzfs.h>
#include <stdio.h>
#include <sys/dmu.h>
#include <sys/zfs_stat.h>
#include "ndmpd_common.h"
#include "ndmpd.h"

typedef struct {
	char nzs_findprop[ZFS_MAXPROPLEN]; /* prop substring to find */
	char nzs_snapname[ZFS_MAXNAMELEN]; /* snap's name */
	char nzs_snapprop[ZFS_MAXPROPLEN]; /* snap's prop value */
	char nzs_snapskip[ZFS_MAXPROPLEN]; /* snap to skip */
	uint32_t nzs_prop_major;	   /* property major version */
	uint32_t nzs_prop_minor;	   /* property minor version */
} ndmpd_zfs_snapfind_t;

mutex_t ndmpd_zfs_fd_lock;

static int ndmpd_zfs_open_fds(ndmpd_zfs_args_t *);
static void ndmpd_zfs_close_fds(ndmpd_zfs_args_t *);

static void ndmpd_zfs_close_one_fd(ndmpd_zfs_args_t *, int);

static int ndmpd_zfs_header_write(ndmpd_session_t *);
static int ndmpd_zfs_header_read(ndmpd_zfs_args_t *);

static int ndmpd_zfs_backup_send_read(ndmpd_zfs_args_t *);

static int ndmpd_zfs_restore(ndmpd_zfs_args_t *);
static int ndmpd_zfs_restore_tape_read(ndmpd_zfs_args_t *);
static int ndmpd_zfs_restore_recv_write(ndmpd_zfs_args_t *);

static int ndmpd_zfs_reader_writer(ndmpd_zfs_args_t *, int **, int **);

static int ndmpd_zfs_addenv_backup_size(ndmpd_zfs_args_t *, u_longlong_t);

static boolean_t ndmpd_zfs_backup_pathvalid(ndmpd_zfs_args_t *);
static int ndmpd_zfs_backup_getpath(ndmpd_zfs_args_t *, char *, int);
static int ndmpd_zfs_backup_getenv(ndmpd_zfs_args_t *);
static boolean_t ndmpd_zfs_backup_check_fs(ndmpd_zfs_args_t *,
    zfs_handle_t *);

static boolean_t ndmpd_zfs_restore_pathvalid(ndmpd_zfs_args_t *);
static int ndmpd_zfs_restore_getpath(ndmpd_zfs_args_t *);
static int ndmpd_zfs_restore_getenv(ndmpd_zfs_args_t *);

static int ndmpd_zfs_getenv(ndmpd_zfs_args_t *);
static int ndmpd_zfs_getenv_zfs_mode(ndmpd_zfs_args_t *);
static int ndmpd_zfs_getenv_zfs_force(ndmpd_zfs_args_t *);
static int ndmpd_zfs_getenv_level(ndmpd_zfs_args_t *);
static int ndmpd_zfs_getenv_update(ndmpd_zfs_args_t *);
static int ndmpd_zfs_getenv_dmp_name(ndmpd_zfs_args_t *);
static int ndmpd_zfs_getenv_zfs_backup_size(ndmpd_zfs_args_t *);

static boolean_t ndmpd_zfs_dmp_name_valid(ndmpd_zfs_args_t *, char *);
static boolean_t ndmpd_zfs_is_incremental(ndmpd_zfs_args_t *);

static int ndmpd_zfs_snapshot_prepare(ndmpd_zfs_args_t *);
static int ndmpd_zfs_snapshot_cleanup(ndmpd_zfs_args_t *, int);
static int ndmpd_zfs_snapshot_create(ndmpd_zfs_args_t *);
static int ndmpd_zfs_snapshot_unuse(ndmpd_zfs_args_t *,
    boolean_t, ndmpd_zfs_snapfind_t *);
static boolean_t ndmpd_zfs_snapshot_ndmpd_generated(char *);
static int ndmpd_zfs_snapshot_find(ndmpd_zfs_args_t *, ndmpd_zfs_snapfind_t *);

static int ndmpd_zfs_snapshot_prop_find(zfs_handle_t *, void *);
static int ndmpd_zfs_snapshot_prop_get(zfs_handle_t *, char *);
static int ndmpd_zfs_snapshot_prop_add(ndmpd_zfs_args_t *);
static int ndmpd_zfs_snapshot_prop_create(ndmpd_zfs_args_t *, char *,
    boolean_t *);
static int ndmpd_zfs_prop_create_subprop(ndmpd_zfs_args_t *, char *, int, int);
static int ndmpd_zfs_snapshot_prop_remove(ndmpd_zfs_args_t *,
    ndmpd_zfs_snapfind_t *);
static boolean_t ndmpd_zfs_prop_version_check(char *, uint32_t *, uint32_t *);

static int ndmpd_zfs_snapname_create(ndmpd_zfs_args_t *, char *, int);

static void ndmpd_zfs_zerr_dma_log(ndmpd_zfs_args_t *);

static int ndmpd_zfs_backup_buf_rd_tape_wr(ndmpd_zfs_args_t *);
static int ndmpd_zfs_backup_buf_tape_wr(ndmpd_zfs_args_t *);
static int ndmpd_zfs_backup(ndmpd_zfs_args_t *);
static int ndmpd_zfs_fhist(ndmpd_zfs_args_t *);

static int ndmpd_zfs_restore_buf_read(ndmpd_zfs_args_t *);

static int ndmpd_find_cpu();
static void ndmpd_bind_cpu(ndmpd_zfs_args_t *);
static void ndmpd_unbind_cpu(ndmpd_zfs_args_t *);
mutex_t ndmpd_cur_cpu_lock;
uint_t ndmpd_cur_cpu = 0;
int *ndmpd_cpulist = NULL;

static boolean_t ndmpd_x86_arch = B_FALSE;
static int ndmpd_max_cpu = 0;	/* max cpu id */

/*
 * Syntax for com.sun.ndmp:incr property value:
 *	#.#.n|u/$LEVEL.$DMP_NAME.$ZFS_MODE(/ ...)
 *
 * where
 *	#.# is the version number
 *	'n' means ndmp-generated; 'u' means user-supplied
 *	$LEVEL: backup (incremental) level [0-9]
 *	$DMP_NAME: set name [default: "level"]
 *	$ZFS_MODE: d | r | p [for dataset, recursive, or package]
 *
 * Examples:
 *
 * 	0.0.n/0.bob.p
 * 	0.0.u/1.bob.p/0.jane.d
 *
 * Note: NDMPD_ZFS_SUBPROP_MAX is calculated based on ZFS_MAXPROPLEN
 */

#define	NDMPD_ZFS_PROP_INCR "com.sun.ndmp:incr"
#define	NDMPD_ZFS_SUBPROP_MAX	28

/*
 * NDMPD_ZFS_LOG_ZERR
 *
 * As coded, there should be no races in the retrieval of the ZFS errno
 * from the ndmpd_zfs_args->nz_zlibh.  I.e., for a given ndmpd_zfs backup
 * or restore, there should only ever be one ZFS library call taking place
 * at any one moment in time.
 */

#define	NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, ...) {			\
	NDMP_LOG(LOG_ERR, __VA_ARGS__);					\
	NDMP_LOG(LOG_ERR, "%s--%s",					\
	    libzfs_error_action((ndmpd_zfs_args)->nz_zlibh),           	\
	    libzfs_error_description((ndmpd_zfs_args)->nz_zlibh));     	\
	ndmpd_zfs_zerr_dma_log((ndmpd_zfs_args));			\
}

int
ndmpd_zfs_init(ndmpd_session_t *session)
{
	ndmpd_zfs_args_t *ndmpd_zfs_args = &session->ns_ndmpd_zfs_args;
	int version = session->ns_protocol_version;

	bzero(ndmpd_zfs_args, sizeof (*ndmpd_zfs_args));

	if ((version < NDMPV3) || (version > NDMPV4)) {
		NDMP_LOG(LOG_ERR, "Unknown or unsupported version %d", version);
		return (-1);
	}

	if ((ndmpd_zfs_args->nz_zlibh = libzfs_init()) == NULL) {
		NDMP_LOG(LOG_ERR, "libzfs init error [%d]", errno);
		return (-1);
	}

	if (ndmpd_zfs_open_fds(ndmpd_zfs_args) < 0) {
		NDMP_LOG(LOG_ERR, "open_fds() failure(): %d\n", errno);
		return (-1);
	}

	ndmpd_zfs_args->nz_bufsize = ndmp_buffer_get_size(session);
	ndmpd_zfs_args->nz_window_len = session->ns_mover.md_window_length;

	ndmpd_zfs_args->nz_nlp = ndmp_get_nlp(session);

	assert(ndmpd_zfs_args->nz_nlp != NULL);

	ndmpd_zfs_args->nz_nlp->nlp_bytes_total = 0;

	session->ns_data.dd_module.dm_module_cookie = ndmpd_zfs_args;
	session->ns_data.dd_data_size = 0;
	session->ns_data.dd_module.dm_stats.ms_est_bytes_remaining = 0;
	session->ns_data.dd_module.dm_stats.ms_est_time_remaining  = 0;

	session->ns_data.dd_bytes_left_to_read = 0;
	session->ns_data.dd_position = 0;
	session->ns_data.dd_discard_length = 0;
	session->ns_data.dd_read_offset = 0;
	session->ns_data.dd_read_length = 0;

	ndmpd_zfs_params->mp_get_env_func = ndmpd_api_get_env;
	ndmpd_zfs_params->mp_add_env_func = ndmpd_api_add_env;
	ndmpd_zfs_params->mp_set_env_func = ndmpd_api_set_env;
	ndmpd_zfs_params->mp_dispatch_func = ndmpd_api_dispatch;
	ndmpd_zfs_params->mp_daemon_cookie = (void *)session;
	ndmpd_zfs_params->mp_protocol_version = session->ns_protocol_version;
	ndmpd_zfs_params->mp_stats = &session->ns_data.dd_module.dm_stats;
	ndmpd_zfs_params->mp_add_file_handler_func =
	    ndmpd_api_add_file_handler;
	ndmpd_zfs_params->mp_remove_file_handler_func =
	    ndmpd_api_remove_file_handler;
	ndmpd_zfs_params->mp_seek_func = 0;

	switch (version) {
	case NDMPV3:
		ndmpd_zfs_params->mp_write_func = ndmpd_api_write_v3;
		ndmpd_zfs_params->mp_read_func = ndmpd_api_read_v3;
		ndmpd_zfs_params->mp_get_name_func = ndmpd_api_get_name_v3;
		ndmpd_zfs_params->mp_done_func = ndmpd_api_done_v3;
		ndmpd_zfs_params->mp_log_func_v3 = ndmpd_api_log_v3;
		ndmpd_zfs_params->mp_file_recovered_func =
		    ndmpd_api_file_recovered_v3;
		break;
	case NDMPV4:
		ndmpd_zfs_params->mp_write_func = ndmpd_api_write_v3;
		ndmpd_zfs_params->mp_read_func = ndmpd_api_read_v3;
		ndmpd_zfs_params->mp_get_name_func = ndmpd_api_get_name_v3;
		ndmpd_zfs_params->mp_done_func = ndmpd_api_done_v3;
		ndmpd_zfs_params->mp_log_func_v3 = ndmpd_api_log_v4;
		ndmpd_zfs_params->mp_file_recovered_func =
		    ndmpd_api_file_recovered_v4;
		break;
	default:
		/* error already returned above for this case */
		break;
	}

	return (0);
}

void
ndmpd_zfs_fini(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	libzfs_fini(ndmpd_zfs_args->nz_zlibh);

	ndmpd_zfs_close_fds(ndmpd_zfs_args);
}

void
ndmpd_cpu_init()
{
	int cpu;
	processor_info_t info;

	(void) mutex_init(&ndmpd_cur_cpu_lock, 0, NULL);
	/*
	 * Initialize parameters to find cpu, cpu id is not in sequential
	 * order, need to walk the max cpu id to find the 1st available cpu.
	 * -max # of cpus
	 * -cpu architecture
	 * -ndmpd_cpulist
	 */
	ndmpd_max_cpu = sysconf(_SC_CPUID_MAX) + 1;
	NDMP_LOG(LOG_DEBUG, "cpu max  %d\n", ndmpd_max_cpu);

	for (cpu = 0; cpu < ndmpd_max_cpu; cpu++) {
		if (processor_info(cpu, &info) == 0) {
			if (strcasestr(info.pi_processor_type, "sparc")
			    != NULL)
				ndmpd_x86_arch = B_FALSE;
			else
				ndmpd_x86_arch = B_TRUE;
			return;
		}
	}
}

void
ndmpd_cpu_fini()
{
	if (ndmpd_cpulist != NULL)
		free(ndmpd_cpulist);
	(void) mutex_destroy(&ndmpd_cur_cpu_lock);
}

/*
 * ndmpd_find_cpu()
 *
 * Find the next cpu to bind, use the cpu with the least amount of
 * backup jobs.  Don't cpu bind for Sparc platform.
 */
static int
ndmpd_find_cpu()
{
	int cpu, new_cpu;
	processor_info_t info;
	int idx, small = -1;

	/*
	 * if ndmpd_cpulist is not allocated or fail calloc eralier
	 * try again.  Continue backup if calloc failed.
	 */
	if (ndmpd_cpulist == NULL) {
		ndmpd_cpulist = (int *)calloc(ndmpd_max_cpu, sizeof (int));
		if (ndmpd_cpulist == NULL) {
			NDMP_LOG(LOG_ERR, "calloc failed %d\n", errno);
			return (PBIND_NONE);
		}
	}

	/* start the search from the last used cpu */
	idx = ndmpd_cur_cpu == 0 ? ndmpd_max_cpu - 1 : ndmpd_cur_cpu - 1;
	new_cpu = ndmpd_cur_cpu;
	for (;;) {
		cpu = ndmpd_cur_cpu++;
		if (ndmpd_cur_cpu >= ndmpd_max_cpu)
			ndmpd_cur_cpu = 0;

		if (processor_info(cpu, &info) == 0 &&
		    info.pi_state == P_ONLINE) {
			/* cpu has 0 job, use it */
			if (ndmpd_cpulist[cpu] == 0) {
				ndmpd_cpulist[cpu]++;
				return (cpu);
			} else {
				/* new_cpu has the least work */
				if (small == -1 || ndmpd_cpulist[cpu] < small) {
					small = ndmpd_cpulist[cpu];
					new_cpu = cpu;
				}
				/* done searching use new_cpu */
				if (cpu == idx) {
					ndmpd_cpulist[new_cpu]++;
					return (new_cpu);
				}
			}
		}
	}
}

static void
ndmpd_bind_cpu(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	int cpu;

	(void) mutex_lock(&ndmpd_cur_cpu_lock);

	if (ndmpd_x86_arch == B_TRUE) {
		cpu = ndmpd_zfs_args->nz_zfs_cpu = ndmpd_find_cpu();
		if (processor_bind(P_LWPID, P_MYID, cpu, NULL) != 0) {
			NDMP_LOG(LOG_ERR, "zfs_backup fails to bind cpu %d\n",
			    cpu);
		} else {
			NDMP_LOG(LOG_DEBUG, "zfs_backup bind to cpu  %d\n",
			    cpu);
		}
	}
	(void) mutex_unlock(&ndmpd_cur_cpu_lock);
}

static void
ndmpd_unbind_cpu(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	/* decrement cpu job count */
	(void) mutex_lock(&ndmpd_cur_cpu_lock);
	if (ndmpd_cpulist != NULL && ndmpd_zfs_args->nz_zfs_cpu != PBIND_NONE)
		if (--ndmpd_cpulist[ndmpd_zfs_args->nz_zfs_cpu] == 0)
			ndmpd_cur_cpu = ndmpd_zfs_args->nz_zfs_cpu;
	(void) mutex_unlock(&ndmpd_cur_cpu_lock);
}

static int
ndmpd_zfs_open_fds(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	int err;

	err = pipe(ndmpd_zfs_args->nz_pipe_fd);
	if (err)
		NDMP_LOG(LOG_ERR, "pipe(2) failed: %s:\n", strerror(errno));

	return (err);
}

/*
 * ndmpd_zfs_close_fds()
 *
 * In the abort case, use dup2() to redirect the end of the pipe that is
 * being written to (to a new pipe).  Close the ends of the new pipe to cause
 * EPIPE to be returned to the writing thread.  This will cause the writer
 * and reader to terminate without having any of the writer's data erroneously
 * go to any reopened descriptor.
 */

static void
ndmpd_zfs_close_fds(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	int pipe_end;
	int fds[2];

	if (session->ns_data.dd_state != NDMP_DATA_STATE_ACTIVE) {
		ndmpd_zfs_close_one_fd(ndmpd_zfs_args, PIPE_ZFS);
		ndmpd_zfs_close_one_fd(ndmpd_zfs_args, PIPE_TAPE);
		return;
	}

	(void) mutex_lock(&ndmpd_zfs_fd_lock);

	if (ndmpd_zfs_params->mp_operation == NDMP_DATA_OP_BACKUP) {
		pipe_end = PIPE_ZFS;
	} else {
		pipe_end = PIPE_TAPE;
	}

	if (ndmpd_zfs_args->nz_pipe_fd[pipe_end] != -1) {
		if (pipe(fds) != 0) {
			(void) mutex_unlock(&ndmpd_zfs_fd_lock);
			NDMP_LOG(LOG_ERR, "pipe(2) failed: %s:\n",
			    strerror(errno));
			return;
		}

		(void) dup2(fds[0], ndmpd_zfs_args->nz_pipe_fd[pipe_end]);
		(void) close(fds[0]);
		(void) close(fds[1]);

		ndmpd_zfs_args->nz_pipe_fd[pipe_end] = -1;
	}

	(void) mutex_unlock(&ndmpd_zfs_fd_lock);
}

static void
ndmpd_zfs_close_one_fd(ndmpd_zfs_args_t *ndmpd_zfs_args, int pipe_end)
{
	(void) mutex_lock(&ndmpd_zfs_fd_lock);
	(void) close(ndmpd_zfs_args->nz_pipe_fd[pipe_end]);
	ndmpd_zfs_args->nz_pipe_fd[pipe_end] = -1;
	(void) mutex_unlock(&ndmpd_zfs_fd_lock);
}

static int
ndmpd_zfs_header_write(ndmpd_session_t *session)
{
	ndmpd_zfs_args_t *ndmpd_zfs_args = &session->ns_ndmpd_zfs_args;
	int32_t bufsize = ndmpd_zfs_args->nz_bufsize;
	ndmpd_zfs_header_t *tape_header = &ndmpd_zfs_args->nz_tape_header;
	char *buf;

	buf = ndmp_malloc(bufsize);
	if (buf == NULL) {
		NDMP_LOG(LOG_ERR, "buf NULL");
		return (-1);
	}

	(void) strlcpy(tape_header->nzh_magic, NDMPUTF8MAGIC,
	    sizeof (NDMPUTF8MAGIC));
	tape_header->nzh_major = LE_32(NDMPD_ZFS_MAJOR_VERSION);
	tape_header->nzh_minor = LE_32(NDMPD_ZFS_MINOR_VERSION);
	tape_header->nzh_hdrlen = LE_32(bufsize);

	bzero(buf, bufsize);
	(void) memcpy(buf, tape_header, sizeof (ndmpd_zfs_header_t));

	NDMP_LOG(LOG_DEBUG, "header (major, minor, length): %u %u %u",
	    NDMPD_ZFS_MAJOR_VERSION,
	    NDMPD_ZFS_MINOR_VERSION,
	    bufsize);

	if (MOD_WRITE(ndmpd_zfs_params, buf, bufsize) != 0) {
		free(buf);
		NDMP_LOG(LOG_ERR, "MOD_WRITE error");
		return (-1);
	}

	free(buf);

	session->ns_data.dd_module.dm_stats.ms_bytes_processed = bufsize;

	return (0);
}

static int
ndmpd_zfs_header_read(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	int32_t bufsize = ndmpd_zfs_args->nz_bufsize;
	ndmpd_zfs_header_t *tape_header = &ndmpd_zfs_args->nz_tape_header;
	uint32_t hdrlen;
	int32_t header_left;
	int err;
	char *buf;

	buf = ndmp_malloc(bufsize);
	if (buf == NULL) {
		NDMP_LOG(LOG_ERR, "buf NULL");
		return (-1);
	}

	bzero(buf, bufsize);

	/*
	 * Read nz_bufsize worth of bytes first (the size of a mover record).
	 */

	err = MOD_READ(ndmpd_zfs_params, buf, bufsize);

	if (err != 0) {
		NDMP_LOG(LOG_ERR, "MOD_READ error: %d", err);
		free(buf);
		return (-1);
	}

	(void) memcpy(tape_header, buf, sizeof (ndmpd_zfs_header_t));

	if (strcmp(tape_header->nzh_magic, NDMPUTF8MAGIC) != 0) {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "bad magic string\n");
		goto _err;
	}

	if (tape_header->nzh_major > LE_32(NDMPD_ZFS_MAJOR_VERSION)) {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "major number larger than supported: (%d %d)\n",
		    LE_32(tape_header->nzh_major), NDMPD_ZFS_MAJOR_VERSION);
		goto _err;
	}

	/*
	 * Major version 0 (regardless of minor version):
	 * Header must be a multiple of the mover record size.
	 */

	hdrlen = LE_32(tape_header->nzh_hdrlen);
	if (hdrlen > bufsize) {
		header_left = hdrlen - bufsize;
		while (header_left > 0) {
			err = MOD_READ(ndmpd_zfs_params, buf, bufsize);
			if (err == -1) {
				ndmpd_zfs_dma_log(ndmpd_zfs_args,
				    NDMP_LOG_ERROR, "bad header\n");
				goto _err;
			}
			header_left -= bufsize;
		}
	}

	NDMP_LOG(LOG_DEBUG, "tape header: %s; %u %u; %u ",
	    tape_header->nzh_magic,
	    LE_32(tape_header->nzh_major),
	    LE_32(tape_header->nzh_minor),
	    LE_32(tape_header->nzh_hdrlen));

	ndmpd_zfs_args->nz_nlp->nlp_bytes_total = hdrlen;

	free(buf);
	return (0);

_err:

	NDMP_LOG(LOG_ERR, "tape header: %s; %u %u; %u ",
	    tape_header->nzh_magic,
	    LE_32(tape_header->nzh_major),
	    LE_32(tape_header->nzh_minor),
	    LE_32(tape_header->nzh_hdrlen));

	free(buf);
	return (-1);
}

int
ndmpd_zfs_backup_starter(void *arg)
{
	ndmpd_zfs_args_t *ndmpd_zfs_args = arg;
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	int cleanup_err = 0;
	pthread_t pth;
	boolean_t fhist = B_TRUE;
	int err = 0;
	char *envp;

	ndmpd_post_sysevent(SYSEVENT_OP_BACKUP, SYSEVENT_START, session,
	    NDMP_NO_ERR);

	if (ndmpd_zfs_snapshot_prepare(ndmpd_zfs_args) != 0) {
		err = -1;
		goto _done;
	}

	envp = MOD_GETENV(ndmpd_zfs_params, "HIST");
	if (!envp || !(strchr("YT", toupper(*envp)))) {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_WARNING,
		    "HIST is not set.  No file history will be "
		    "generated.\n");
		fhist = B_FALSE;
	}
	/* Send the rest of the file history */
	if (fhist &&
	    ndmp_pthread_create(&pth, NULL, (funct_t)ndmpd_zfs_fhist,
	    ndmpd_zfs_args) != 0) {
		NDMP_LOG(LOG_ERR, "Cannot create zfs_fhist thread");
		ndmpd_file_history_cleanup(session, TRUE);
		err = -1;
		cleanup_err = ndmpd_zfs_snapshot_cleanup(ndmpd_zfs_args, err);
		goto _done;
	}

	err = ndmpd_zfs_backup(ndmpd_zfs_args);

	cleanup_err = ndmpd_zfs_snapshot_cleanup(ndmpd_zfs_args, err);

	NDMP_LOG(LOG_DEBUG,
	    "data bytes_total(including header):%llu",
	    session->ns_data.dd_module.dm_stats.ms_bytes_processed);

	if (fhist) {
		(void) pthread_join(pth, NULL);
		ndmpd_file_history_cleanup(session, TRUE);
	}

_done:
	MOD_DONE(ndmpd_zfs_params, err ? err : cleanup_err);
	ndmpd_post_sysevent(SYSEVENT_OP_BACKUP, SYSEVENT_FINISH, session,
	    err ? err : cleanup_err);
	ndmpd_zfs_fini(ndmpd_zfs_args);
	ndmp_session_unref(session);
	NS_DEC(nbk);

	return (err);
}

static int
ndmpd_zfs_backup(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	int *read_err = NULL;
	int *write_err = NULL;
	int result = 0;
	int err;

	if (session->ns_eof)
		return (-1);

	/*
	 * Bind this backup to a CPU, this eliminates data migration
	 * across cpus. If processor_bind() failed continue with backup
	 */

	if (!session->ns_data.dd_abort) {
		if (ndmpd_zfs_header_write(session)) {
			ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
			    "ndmpd_zfs header write error\n");
			return (-1);
		}

		ndmpd_bind_cpu(ndmpd_zfs_args);
		err = ndmpd_zfs_reader_writer(ndmpd_zfs_args,
		    &read_err, &write_err);
		ndmpd_unbind_cpu(ndmpd_zfs_args);

		if (err || read_err || write_err || session->ns_eof)
			result = EPIPE;
	}

	if (session->ns_data.dd_abort) {
		ndmpd_audit_backup(session->ns_connection,
		    ndmpd_zfs_args->nz_dataset,
		    session->ns_data.dd_data_addr.addr_type,
		    ndmpd_zfs_args->nz_dataset, EINTR);
		NDMP_LOG(LOG_DEBUG, "Backing up \"%s\" aborted.",
		    ndmpd_zfs_args->nz_dataset);

		(void) ndmpd_zfs_post_backup(ndmpd_zfs_args);
		err = -1;
	} else {
		ndmpd_audit_backup(session->ns_connection,
		    ndmpd_zfs_args->nz_dataset,
		    session->ns_data.dd_data_addr.addr_type,
		    ndmpd_zfs_args->nz_dataset, result);

		err = ndmpd_zfs_post_backup(ndmpd_zfs_args);
		if (err || result)
			err = -1;

		if (err == 0)  {
			NDMP_LOG(LOG_DEBUG, "Backing up \"%s\" finished.",
			    ndmpd_zfs_args->nz_dataset);
		} else {
			NDMP_LOG(LOG_ERR, "An error occurred while backing up"
			    " \"%s\"", ndmpd_zfs_args->nz_dataset);
		}
	}

	return (err);
}

/*
 * ndmpd_zfs_backup_send_read()
 *
 * This routine executes zfs_send() to create the backup data stream.
 * The value of ZFS_MODE determines the type of zfs_send():
 * 	dataset ('d'): Only the dataset specified (i.e., top level) is backed up
 * 	recursive ('r'): The dataset and its child file systems are backed up
 * 	package ('p'): Same as 'r', except all intermediate snapshots are also
 *			backed up
 *
 * Volumes do not have descednants, so 'd' and 'r' produce equivalent results.
 */

static int
ndmpd_zfs_backup_send_read(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	sendflags_t flags = { 0 };
	char *fromsnap = NULL;
	zfs_handle_t *zhp;
	int err;

	zhp = zfs_open(ndmpd_zfs_args->nz_zlibh,
	    ndmpd_zfs_args->nz_dataset, ndmpd_zfs_args->nz_type);

	if (!zhp) {
		if (!session->ns_data.dd_abort)
			NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_open");
		return (-1);
	}

	switch (ndmpd_zfs_args->nz_zfs_mode) {
	case ('d'):
		flags.props = B_TRUE;
		break;
	case ('r'):
		flags.replicate = B_TRUE;
		flags.selfcont = B_TRUE;
		break;
	case ('p'):
		flags.doall = B_TRUE;
		flags.replicate = B_TRUE;
		break;
	default:
		NDMP_LOG(LOG_ERR, "unknown zfs_mode: %c",
		    ndmpd_zfs_args->nz_zfs_mode);
		zfs_close(zhp);
		return (-1);
	}

	if (ndmpd_zfs_is_incremental(ndmpd_zfs_args)) {
		if (ndmpd_zfs_args->nz_fromsnap[0] == '\0') {
			NDMP_LOG(LOG_ERR, "no fromsnap");
			zfs_close(zhp);
			return (-1);
		}
		fromsnap = ndmpd_zfs_args->nz_fromsnap;
	}

	err = zfs_send(zhp, fromsnap, ndmpd_zfs_args->nz_snapname, flags,
	    ndmpd_zfs_args->nz_pipe_fd[PIPE_ZFS], NULL, NULL, NULL);

	if (err && !session->ns_data.dd_abort)
		NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_send: %d", err);

	zfs_close(zhp);

	return (err);
}


static int
ndmpd_zfs_addenv_backup_size(ndmpd_zfs_args_t *ndmpd_zfs_args,
    u_longlong_t bytes_total)
{
	char zfs_backup_size[32];
	int err;

	(void) snprintf(zfs_backup_size, sizeof (zfs_backup_size), "%llu",
	    bytes_total);

	err = MOD_ADDENV(ndmpd_zfs_params, "ZFS_BACKUP_SIZE", zfs_backup_size);

	if (err) {
		NDMP_LOG(LOG_ERR, "Failed to add ZFS_BACKUP_SIZE env");
		return (-1);
	}

	NDMP_LOG(LOG_DEBUG, "Added ZFS_BACKUP_SIZE env: %s", zfs_backup_size);

	return (0);
}


/*
 * ndmpd_zfs_backup_buf_tape_wr()
 *
 * This thread read from zfs_send pipe and write to a buffer, the buffer
 * will be read by the ndmpd_zfs_backup_buf_rd_tape_wr thread.  This allows
 * the zfs_send pipe to fill during tape write.
 */
static int
ndmpd_zfs_backup_buf_tape_wr(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	int bufsize = ndmpd_zfs_args->nz_bufsize;
	int count;
	char *buf;
	int read_count;
	char *bp;
	buf_list_t *bl_ptr;
	int readsize;

	for (;;) {
		if (ndmpd_get_free_buf(&ndmpd_zfs_args->nz_buf,
		    bufsize, &bl_ptr) < 0) {
			NDMP_LOG(LOG_ERR, "ndmpd malloc error (errno %d)",
			    errno);
			return (-1);
		}
		buf = bl_ptr->bl_bp;
		bzero(buf, bufsize);
		readsize = bufsize;
		count = 0;
		bp = buf;
		do {
			read_count =
			    read(ndmpd_zfs_args->nz_pipe_fd[PIPE_TAPE],
			    bp, readsize);
			if (read_count != -1) {
				count += read_count;
				readsize = bufsize - count;
				bp += read_count;
			} else {
				count = read_count;
			}
		} while ((count < bufsize) && (read_count > 0));

		if (count == 0) {	/* EOF */
			ndmpd_add_free_list(&ndmpd_zfs_args->nz_buf, bl_ptr);
			return (0);
		}

		if (count == -1) {
			NDMP_LOG(LOG_ERR, "pipe read error (errno %d)",
			    errno);
			ndmpd_add_free_list(&ndmpd_zfs_args->nz_buf, bl_ptr);
			return (-1);
		}

		bl_ptr->bl_byte_count = count;
		ndmpd_add_used_list(&ndmpd_zfs_args->nz_buf, bl_ptr);
		NS_ADD(rdisk, count);	/* update stats */
	}
	/* NOTREACHED */
}

/*
 * Additional thread to read buffer and write tape
 */
static int
ndmpd_zfs_backup_buf_rd_tape_wr(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	int count;
	u_longlong_t *bytes_totalp;
	buf_list_t *bl_ptr;
	char *buf;

	bytes_totalp =
	    &(session->ns_data.dd_module.dm_stats.ms_bytes_processed);

	for (;;) {
		if (ndmpd_get_used_buf(&ndmpd_zfs_args->nz_buf, &bl_ptr) == 1) {
			return (ndmpd_zfs_addenv_backup_size(ndmpd_zfs_args,
			    *bytes_totalp));
		}

		count = bl_ptr->bl_byte_count;
		buf = bl_ptr->bl_bp;

		if (MOD_WRITE(ndmpd_zfs_params, buf, count) != 0) {
			(void) ndmpd_zfs_abort((void *) ndmpd_zfs_args);
			NDMP_LOG(LOG_ERR, "MOD_WRITE error");
			ndmpd_add_free_list(&ndmpd_zfs_args->nz_buf, bl_ptr);
			return (-1);
		}

		*bytes_totalp += count;

		bl_ptr->bl_byte_count = 0;

		ndmpd_add_free_list(&ndmpd_zfs_args->nz_buf, bl_ptr);
	}
	/* NOTREACHED */
	return (0);
}

int
ndmpd_zfs_restore_starter(void *arg)
{
	ndmpd_zfs_args_t *ndmpd_zfs_args = arg;
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	int err;

	ndmpd_post_sysevent(SYSEVENT_OP_RESTORE, SYSEVENT_START, session,
	    NDMP_NO_ERR);

	err = ndmpd_zfs_restore(ndmpd_zfs_args);

	MOD_DONE(ndmpd_zfs_params, err);

	ndmpd_post_sysevent(SYSEVENT_OP_RESTORE, SYSEVENT_FINISH, session, err);

	ndmpd_zfs_fini(ndmpd_zfs_args);

	ndmp_session_unref(session);

	NS_DEC(nrs);

	return (err);
}

static int
ndmpd_zfs_restore(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	int *read_err = NULL;
	int *write_err = NULL;
	int result = 0;
	int err;

	if (!session->ns_data.dd_abort) {
		if (ndmpd_zfs_header_read(ndmpd_zfs_args)) {
			ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
			    "ndmpd_zfs header read error\n");
			return (-1);
		}

		err = ndmpd_zfs_reader_writer(ndmpd_zfs_args,
		    &write_err, &read_err);

		if (err || read_err || write_err || session->ns_eof)
			result = EIO;
	}

	if (session->ns_data.dd_abort) {
		NDMP_LOG(LOG_DEBUG, "Restoring to \"%s\" aborted.",
		    ndmpd_zfs_args->nz_dataset);
		ndmpd_audit_restore(session->ns_connection,
		    ndmpd_zfs_args->nz_dataset,
		    session->ns_data.dd_data_addr.addr_type,
		    ndmpd_zfs_args->nz_dataset, EINTR);
		(void) ndmpd_zfs_post_restore(ndmpd_zfs_args);
		err = -1;
	} else {
		ndmpd_audit_restore(session->ns_connection,
		    ndmpd_zfs_args->nz_dataset,
		    session->ns_data.dd_data_addr.addr_type,
		    ndmpd_zfs_args->nz_dataset, result);
		err = ndmpd_zfs_post_restore(ndmpd_zfs_args);
		if (err || result)
			err = -1;

		if (err == 0) {
			NDMP_LOG(LOG_DEBUG, "Restoring to \"%s\" finished",
			    ndmpd_zfs_args->nz_dataset);
		} else {
			NDMP_LOG(LOG_ERR, "An error occurred while restoring"
			    " to \"%s\"", ndmpd_zfs_args->nz_dataset);
		}
	}

	return (err);
}

static int
ndmpd_zfs_restore_tape_read(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_zfs_buf_t	*nzb = &ndmpd_zfs_args->nz_buf;
	int bufsize = ndmpd_zfs_args->nz_bufsize;
	u_longlong_t backup_size = ndmpd_zfs_args->nz_zfs_backup_size;
	u_longlong_t *bytes_totalp;
	u_longlong_t bytes;
	char *buf;
	int err;
	buf_list_t *bl_ptr;

	bytes_totalp = &ndmpd_zfs_args->nz_nlp->nlp_bytes_total;

	while (*bytes_totalp < backup_size) {
		err = ndmpd_get_free_buf(&ndmpd_zfs_args->nz_buf,
		    bufsize,  &bl_ptr);
		if (err < 0) {
			NDMP_LOG(LOG_ERR, "ndmpd malloc error %m", errno);
			return (-1);
		}
		if (err == 1) {
			/* buf_thread exited caused by an abort */
			return (0);
		}

		bytes = backup_size - *bytes_totalp;

		if (bytes >= bufsize)
			bytes = bufsize;

		buf = bl_ptr->bl_bp;

		err = MOD_READ(ndmpd_zfs_params, buf, bytes);

		if (err != 0) {
			NDMP_LOG(LOG_ERR, "MOD_READ error: %d; returning -1",
			    err);
			ndmpd_add_free_list(&ndmpd_zfs_args->nz_buf, bl_ptr);
			return (-1);
		}

		/* add buf to used list */
		bl_ptr->bl_byte_count = bytes;
		ndmpd_add_used_list(&ndmpd_zfs_args->nz_buf, bl_ptr);

		*bytes_totalp += bytes;
	}

	ndmpd_release_used_buf(nzb);

	return (0);
}


/*
 * Read from buffer and write to PIPE_TAPE
 */
static int
ndmpd_zfs_restore_buf_read(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_session_t *session;
	buf_list_t *bl_ptr;
	int count;

	session = (ndmpd_session_t *)(ndmpd_zfs_params->mp_daemon_cookie);

	for (;;) {
		if (ndmpd_get_used_buf(&ndmpd_zfs_args->nz_buf, &bl_ptr) == 1) {
			return (0);
		}

		count = write(ndmpd_zfs_args->nz_pipe_fd[PIPE_TAPE],
		    bl_ptr->bl_bp, bl_ptr->bl_byte_count);

		if (count != bl_ptr->bl_byte_count) {
			NDMP_LOG(LOG_ERR, "count (%d) != bytes (%d)",
			    count, bl_ptr->bl_byte_count);

			if (count == -1) {
				NDMP_LOG(LOG_ERR, "pipe write error: errno: %d",
				    errno);

				if (session->ns_data.dd_abort)
					NDMP_LOG(LOG_DEBUG, "abort set");
			}
			ndmpd_release_free_buf(&ndmpd_zfs_args->nz_buf);
			ndmpd_add_free_list(&ndmpd_zfs_args->nz_buf, bl_ptr);
			return (-1);
		}

		NS_ADD(wdisk, count);

		bl_ptr->bl_byte_count = 0;

		ndmpd_add_free_list(&ndmpd_zfs_args->nz_buf, bl_ptr);
	}

	/* NOTREACHED */
	return (0);
}

/*
 * ndmpd_zfs_restore_recv_write()
 *
 * This routine executes zfs_receive() to restore the backup.
 */

static int
ndmpd_zfs_restore_recv_write(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	recvflags_t flags;
	int err;

	bzero(&flags, sizeof (recvflags_t));

	flags.nomount = B_TRUE;

	NDMP_LOG(LOG_DEBUG, "nz_zfs_force: %d\n", ndmpd_zfs_args->nz_zfs_force);

	if (ndmpd_zfs_args->nz_zfs_force)
		flags.force = B_TRUE;

	err = zfs_receive(ndmpd_zfs_args->nz_zlibh, ndmpd_zfs_args->nz_dataset,
	    flags, NULL, ndmpd_zfs_args->nz_pipe_fd[PIPE_ZFS], NULL);

	if (err && !session->ns_data.dd_abort)
		NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_receive: %d", err);

	return (err);
}

/*
 * ndmpd_zfs_reader_writer()
 *
 * Two separate threads are used for actual backup or restore.
 */

static int
ndmpd_zfs_reader_writer(ndmpd_zfs_args_t *ndmpd_zfs_args,
    int **sendrecv_errp, int **tape_errp)
{
	funct_t sendrecv_func;
	funct_t tape_func;
	funct_t buf_func;
	int sendrecv_err;
	int tape_err = 0, buf_err = 0;
	pthread_t sendrecv_thread, tape_thread, buf_thread;
	ndmpd_zfs_buf_t	*nzb = &ndmpd_zfs_args->nz_buf;
	ndmpd_zfs_args_t *nza = ndmpd_zfs_args;
	int buf_errp = 0;

	switch (ndmpd_zfs_params->mp_operation) {
	case NDMP_DATA_OP_BACKUP:
		sendrecv_func = (funct_t)ndmpd_zfs_backup_send_read;
		tape_func = (funct_t)ndmpd_zfs_backup_buf_tape_wr;
		buf_func = (funct_t)ndmpd_zfs_backup_buf_rd_tape_wr;
		break;
	case NDMP_DATA_OP_RECOVER:
		sendrecv_func = (funct_t)ndmpd_zfs_restore_recv_write;
		tape_func = (funct_t)ndmpd_zfs_restore_tape_read;
		buf_func = (funct_t)ndmpd_zfs_restore_buf_read;
		break;
	}
	ndmpd_buf_init(&ndmpd_zfs_args->nz_buf);

	sendrecv_err = ndmp_pthread_create(&sendrecv_thread,
	    NULL, sendrecv_func, ndmpd_zfs_args);

	if (sendrecv_err == 0) {
		tape_err = ndmp_pthread_create(&tape_thread,
		    NULL, tape_func, ndmpd_zfs_args);

		if (tape_err) {
			/*
			 * The close of the tape side of the pipe will cause
			 * sendrecv_thread to error in the zfs_send/recv()
			 * call and to return.  Hence we do not need
			 * to explicitly cancel the sendrecv_thread here
			 * (the pthread_join() below is sufficient).
			 */

			ndmpd_zfs_close_one_fd(ndmpd_zfs_args, PIPE_TAPE);
			NDMP_LOG(LOG_ERR, "Could not start tape thread; "
			    "aborting z-op");
		} else {
			buf_err = ndmp_pthread_create(&buf_thread,
			    NULL, buf_func, nza);

			if (buf_err != 0) {
				(void) pthread_cancel(tape_thread);
				ndmpd_zfs_close_one_fd(ndmpd_zfs_args,
				    PIPE_TAPE);
				NDMP_LOG(LOG_ERR,
				    "Could not start buf_thread; "
				    "aborting z-op");
			}
		}

		(void) pthread_join(sendrecv_thread, (void **) sendrecv_errp);
	} else {
		ndmpd_zfs_close_one_fd(ndmpd_zfs_args, PIPE_TAPE);
	}

	ndmpd_zfs_close_one_fd(ndmpd_zfs_args, PIPE_ZFS);

	if (sendrecv_err == 0 && tape_err == 0) {
		(void) pthread_join(tape_thread, (void **) tape_errp);
		if (buf_err == 0) {
			ndmpd_release_used_buf(nzb);
			(void) pthread_join(buf_thread, (void **) &buf_errp);
			ndmpd_zfs_close_one_fd(ndmpd_zfs_args, PIPE_TAPE);
		}
		ndmpd_buf_fini(nzb);
		return (buf_err);
	}

	ndmpd_buf_fini(nzb);
	return (sendrecv_err ? sendrecv_err : tape_err);
}

int
ndmpd_zfs_abort(void *arg)
{
	ndmpd_zfs_args_t *ndmpd_zfs_args = arg;
	char str[8];

	if (ndmpd_zfs_params->mp_operation == NDMP_DATA_OP_BACKUP)
		(void) strlcpy(str, "backup", 8);
	else
		(void) strlcpy(str, "recover", 8);

	NDMP_LOG(LOG_ERR, "ndmpd_zfs_abort() called...aborting %s operation",
	    str);

	ndmpd_zfs_close_fds(ndmpd_zfs_args);

	return (0);
}

/*
 * ndmpd_zfs_pre_backup()
 *
 * Note: The memset to 0 of nctxp ensures that nctx->nc_cmds == NULL.
 * This ensures that ndmp_include_zfs() will fail, which is
 * a requirement for "zfs"-type backup.
 */

int
ndmpd_zfs_pre_backup(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	ndmp_context_t *nctxp = &ndmpd_zfs_args->nz_nctx;
	int err;

	if (ndmp_pl == NULL || ndmp_pl->np_pre_backup == NULL)
		return (0);

	(void) memset(nctxp, 0, sizeof (ndmp_context_t));
	nctxp->nc_plversion = ndmp_pl->np_plversion;
	nctxp->nc_plname = ndmpd_get_prop(NDMP_PLUGIN_PATH);
	nctxp->nc_ddata = (void *) session;
	nctxp->nc_params = ndmpd_zfs_params;

	err = ndmp_pl->np_pre_backup(ndmp_pl, nctxp,
	    ndmpd_zfs_args->nz_dataset);

	if (err != 0) {
		NDMP_LOG(LOG_ERR, "Pre-backup plug-in: %m");
		(void) ndmpd_zfs_post_backup(ndmpd_zfs_args);
	}

	return (err);
}

int
ndmpd_zfs_post_backup(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmp_context_t *nctxp = &ndmpd_zfs_args->nz_nctx;
	int err = 0;

	if (ndmp_pl == NULL || ndmp_pl->np_post_backup == NULL)
		return (0);

	err = ndmp_pl->np_post_backup(ndmp_pl, nctxp, err);

	if (err == -1)
		NDMP_LOG(LOG_ERR, "Post-backup plug-in: %m");

	return (err);
}

int
ndmpd_zfs_pre_restore(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	ndmp_context_t *nctxp = &ndmpd_zfs_args->nz_nctx;
	char bkpath[ZFS_MAXNAMELEN];
	int err;

	if (ndmp_pl == NULL || ndmp_pl->np_pre_restore == NULL)
		return (0);

	err = ndmpd_zfs_backup_getpath(ndmpd_zfs_args, bkpath, ZFS_MAXNAMELEN);

	if (err != 0) {
		NDMP_LOG(LOG_ERR, "error getting bkup path: %d", err);
		return (-1);
	}

	err = ndmpd_zfs_restore_getpath(ndmpd_zfs_args);

	if (err != 0) {
		NDMP_LOG(LOG_ERR, "error getting restore path: %d", err);
		return (-1);
	}

	(void) memset(nctxp, 0, sizeof (ndmp_context_t));
	nctxp->nc_ddata = (void *) session;
	nctxp->nc_params = ndmpd_zfs_params;

	err = ndmp_pl->np_pre_restore(ndmp_pl, nctxp, bkpath,
	    ndmpd_zfs_args->nz_dataset);

	if (err != 0) {
		NDMP_LOG(LOG_ERR, "Pre-restore plug-in: %m");
		return (-1);
	}

	return (0);
}

int
ndmpd_zfs_post_restore(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmp_context_t *nctxp = &ndmpd_zfs_args->nz_nctx;
	int err = 0;

	if (ndmp_pl == NULL || ndmp_pl->np_post_restore == NULL)
		return (0);

	err = ndmp_pl->np_post_restore(ndmp_pl, nctxp, err);

	if (err == -1)
		NDMP_LOG(LOG_ERR, "Post-restore plug-in: %m");

	return (err);
}

boolean_t
ndmpd_zfs_backup_parms_valid(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_zfs_snapfind_t snapdata;
	int level;

	if (ndmpd_zfs_backup_getenv(ndmpd_zfs_args) != 0)
		return (B_FALSE);

	if (!ndmpd_zfs_backup_pathvalid(ndmpd_zfs_args))
		return (B_FALSE);

	if (ndmpd_zfs_is_incremental(ndmpd_zfs_args)) {

		/*
		 * Check successive prior levels until a snapshot is found.
		 * This is because skips in level for incremental backup are
		 * allowed.
		 */

		for ((level = ndmpd_zfs_args->nz_level-1); level >= 0;
		    level--) {

			(void) ndmpd_zfs_prop_create_subprop(ndmpd_zfs_args,
			    snapdata.nzs_findprop, ZFS_MAXPROPLEN, level);

			snapdata.nzs_snapname[0] = '\0';
			snapdata.nzs_snapprop[0] = '\0';

			if (ndmpd_zfs_snapshot_find(ndmpd_zfs_args, &snapdata))
				return (B_FALSE);

			if (snapdata.nzs_snapname[0] != '\0') /* found */
				break;
		}

		if (snapdata.nzs_snapname[0] == '\0') { /* not found */
			ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
			    "Snapshot for (any) prior level does not exist.\n");
			return (B_FALSE);
		}

		(void) strlcpy(ndmpd_zfs_args->nz_fromsnap,
		    snapdata.nzs_snapname, ZFS_MAXNAMELEN);
	}

	return (B_TRUE);
}

/*
 * ndmpd_zfs_backup_pathvalid()
 *
 * Make sure the path is of an existing dataset
 */

static boolean_t
ndmpd_zfs_backup_pathvalid(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	char zpath[ZFS_MAXNAMELEN];
	char propstr[ZFS_MAXPROPLEN];
	zfs_handle_t *zhp;
	zfs_type_t ztype;
	int err;

	if (ndmpd_zfs_backup_getpath(ndmpd_zfs_args, zpath, ZFS_MAXNAMELEN)
	    != 0)
		return (B_FALSE);

	if (ndmpd_zfs_args->nz_snapname[0] != '\0') {
		zhp = zfs_open(ndmpd_zfs_args->nz_zlibh, zpath,
		    ZFS_TYPE_SNAPSHOT);

		if (!zhp) {
			NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args,
			    "zfs_open (snap)");
			ndmpd_zfs_args->nz_snapname[0] = '\0';
			ndmpd_zfs_args->nz_dataset[0] = '\0';
			return (B_FALSE);
		}

		err = ndmpd_zfs_snapshot_prop_get(zhp, propstr);

		zfs_close(zhp);

		if (err)
			return (B_FALSE);

		if (*propstr && ndmpd_zfs_snapshot_ndmpd_generated(propstr)) {
			ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
			    "cannot use an ndmpd-generated snapshot\n");
			return (B_FALSE);
		}
	}

	zhp = zfs_open(ndmpd_zfs_args->nz_zlibh,
	    ndmpd_zfs_args->nz_dataset, ZFS_TYPE_DATASET);

	if (zhp)
		ztype = zfs_get_type(zhp);

	if (!zhp || (zhp &&
	    ztype != ZFS_TYPE_VOLUME &&
	    ztype != ZFS_TYPE_FILESYSTEM)) {
		if (zhp)
			zfs_close(zhp);

		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "Invalid file system or volume.\n");

		return (B_FALSE);
	}

	ndmpd_zfs_args->nz_type = ztype;

	if (ztype == ZFS_TYPE_VOLUME) {
		zfs_close(zhp);
		return (B_TRUE);
	}

	err = ndmpd_zfs_backup_check_fs(ndmpd_zfs_args, zhp);

	zfs_close(zhp);

	return (err);
}

/*
 * ndmpd_zfs_backup_getpath()
 *
 * Retrieve the backup path from the environment, which should
 * be of the form "/dataset[@snap]".  The leading slash is required
 * by certain DMA's but can otherwise be ignored.
 *
 * (Note: "dataset" can consist of more than one component,
 * e.g. "pool", "pool/volume", "pool/fs/fs2".)
 *
 * The dataset name and the snapshot name (if any) will be
 * stored in ndmpd_zfs_args.
 */

static int
ndmpd_zfs_backup_getpath(ndmpd_zfs_args_t *ndmpd_zfs_args, char *zpath,
    int zlen)
{
	char *env_path;
	char *at;

	env_path = get_backup_path_v3(ndmpd_zfs_params);
	if (env_path == NULL)
		return (-1);

	if (env_path[0] != '/') {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "Invalid path: %s (leading slash required)\n", env_path);
		return (-1);
	}

	(void) strlcpy(zpath, &env_path[1], zlen);
	(void) strlcpy(ndmpd_zfs_args->nz_dataset, &env_path[1],
	    ZFS_MAXNAMELEN);

	at = strchr(ndmpd_zfs_args->nz_dataset, '@');
	if (at) {
		*at = '\0';
		(void) strlcpy(ndmpd_zfs_args->nz_snapname, ++at,
		    ZFS_MAXNAMELEN);
	} else {
		ndmpd_zfs_args->nz_snapname[0] = '\0';
	}

	(void) trim_whitespace(ndmpd_zfs_args->nz_dataset);

	return (0);
}

static int
ndmpd_zfs_backup_getenv(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	return (ndmpd_zfs_getenv(ndmpd_zfs_args));
}

static boolean_t
ndmpd_zfs_backup_check_fs(ndmpd_zfs_args_t *ndmpd_zfs_args, zfs_handle_t *zhp)
{
	char propstr[ZFS_MAXPROPLEN];

	if ((zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, propstr,
	    sizeof (propstr), NULL, NULL, 0, B_FALSE)) != 0) {
		NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_prop_get");
		return (B_FALSE);
	}

	if (strcmp(propstr, "legacy") == 0)
		get_zfsmntpnt(ndmpd_zfs_args->nz_dataset,
		    sizeof (propstr), propstr);

	if (!fs_is_exported(propstr)) {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "(%s) mountpoint not specified in export-fs\n",
		    propstr);
		return (B_FALSE);
	}

	return (B_TRUE);
}

boolean_t
ndmpd_zfs_restore_parms_valid(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	if (ndmpd_zfs_restore_getenv(ndmpd_zfs_args) != 0)
		return (B_FALSE);

	if (!ndmpd_zfs_restore_pathvalid(ndmpd_zfs_args))
		return (B_FALSE);

	return (B_TRUE);
}

static boolean_t
ndmpd_zfs_restore_pathvalid(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	zfs_handle_t *zhp;
	char *at;

	if (ndmpd_zfs_restore_getpath(ndmpd_zfs_args) != 0)
		return (B_FALSE);

	at = strchr(ndmpd_zfs_args->nz_dataset, '@');

	if (at) {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_WARNING,
		    "%s ignored in restore path\n", at);
		*at = '\0';
	}

	ndmpd_zfs_args->nz_type = ZFS_TYPE_VOLUME | ZFS_TYPE_FILESYSTEM;

	zhp = zfs_open(ndmpd_zfs_args->nz_zlibh,
	    ndmpd_zfs_args->nz_dataset, ndmpd_zfs_args->nz_type);

	if (zhp) {
		zfs_close(zhp);

		if (!ndmpd_zfs_is_incremental(ndmpd_zfs_args)) {
			ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
			    "Restore dataset exists.\n"
			    "A nonexistent dataset must be specified "
			    "for 'zfs' non-incremental restore.\n");
			return (B_FALSE);
		}
	}

	NDMP_LOG(LOG_DEBUG, "restore path: %s\n", ndmpd_zfs_args->nz_dataset);

	return (B_TRUE);
}

/*
 * ndmpd_zfs_restore_getpath()
 *
 * Be sure to not include the leading slash, which is required for
 * compatibility with backup applications (NBU) but which is not part
 * of the ZFS syntax.  (Note that this done explicitly in all paths
 * below except those calling ndmpd_zfs_backup_getpath(), because it is
 * already stripped in that function.)
 *
 * In addition, the DMA might add a trailing slash to the path.
 * Strip all such slashes.
 */

static int
ndmpd_zfs_restore_getpath(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	int version = ndmpd_zfs_params->mp_protocol_version;
	char zpath[ZFS_MAXNAMELEN];
	mem_ndmp_name_v3_t *namep_v3;
	char *dataset = ndmpd_zfs_args->nz_dataset;
	char *nm;
	char *p;
	int len;
	int err;

	dataset = ndmpd_zfs_args->nz_dataset;

	namep_v3 = (mem_ndmp_name_v3_t *)MOD_GETNAME(ndmpd_zfs_params, 0);

	if (namep_v3 == NULL) {
		NDMP_LOG(LOG_ERR, "Can't get Nlist[0]");
		return (-1);
	}

	if (namep_v3->nm3_dpath) {
		if (namep_v3->nm3_dpath[0] != '/') {
			ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
			    "Invalid path: %s (leading slash required)\n",
			    namep_v3->nm3_dpath);
			return (-1);
		}

		(void) strlcpy(dataset, &(namep_v3->nm3_dpath[1]),
		    ZFS_MAXNAMELEN);

		if (namep_v3->nm3_newnm) {
			(void) strlcat(dataset, "/", ZFS_MAXNAMELEN);
			(void) strlcat(dataset, namep_v3->nm3_newnm,
			    ZFS_MAXNAMELEN);

		} else {
			if (version == NDMPV3) {
				/*
				 * The following does not apply for V4.
				 *
				 * Find the last component of nm3_opath.
				 * nm3_opath has no trailing '/'.
				 */
				p = strrchr(namep_v3->nm3_opath, '/');
				nm = p? p : namep_v3->nm3_opath;
				(void) strlcat(dataset, "/", ZFS_MAXNAMELEN);
				(void) strlcat(dataset, nm, ZFS_MAXNAMELEN);
			}
		}
	} else {
		err = ndmpd_zfs_backup_getpath(ndmpd_zfs_args, zpath,
		    ZFS_MAXNAMELEN);
		if (err)
			return (err);
	}

	len = strlen(dataset);
	while (dataset[len-1] == '/') {
		dataset[len-1] = '\0';
		len--;
	}

	return (0);
}

static int
ndmpd_zfs_restore_getenv(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	if (ndmpd_zfs_getenv_zfs_backup_size(ndmpd_zfs_args) != 0)
		return (-1);

	return (ndmpd_zfs_getenv(ndmpd_zfs_args));
}

static int
ndmpd_zfs_getenv(ndmpd_zfs_args_t *ndmpd_zfs_args)
{

	if (ndmpd_zfs_getenv_level(ndmpd_zfs_args) != 0)
		return (-1);

	if (ndmpd_zfs_getenv_zfs_mode(ndmpd_zfs_args) != 0)
		return (-1);

	if (ndmpd_zfs_getenv_zfs_force(ndmpd_zfs_args) != 0)
		return (-1);

	if (ndmpd_zfs_getenv_update(ndmpd_zfs_args) != 0)
		return (-1);

	if (ndmpd_zfs_getenv_dmp_name(ndmpd_zfs_args) != 0)
		return (-1);

	return (0);
}

static int
ndmpd_zfs_getenv_zfs_mode(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	char *envp;

	envp = MOD_GETENV(ndmpd_zfs_params, "ZFS_MODE");

	if (envp == NULL) {
		NDMP_LOG(LOG_DEBUG, "env(ZFS_MODE) not specified, "
		    "defaulting to recursive");
		ndmpd_zfs_args->nz_zfs_mode = 'r';
		return (0);
	}

	if ((strcmp(envp, "dataset") == 0) || (strcmp(envp, "d") == 0)) {
		ndmpd_zfs_args->nz_zfs_mode = 'd';
	} else if ((strcmp(envp, "recursive") == 0) ||
	    (strcmp(envp, "r") == 0)) {
		ndmpd_zfs_args->nz_zfs_mode = 'r';
	} else if ((strcmp(envp, "package") == 0) || (strcmp(envp, "p") == 0)) {
		ndmpd_zfs_args->nz_zfs_mode = 'p';
	} else {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "Invalid ZFS_MODE value \"%s\".\n", envp);
		return (-1);
	}

	NDMP_LOG(LOG_DEBUG, "env(ZFS_MODE): \"%c\"",
	    ndmpd_zfs_args->nz_zfs_mode);

	return (0);
}

/*
 * ndmpd_zfs_getenv_zfs_force()
 *
 * If SMF property zfs-force-override is set to "yes" or "no", this
 * value will override any value of NDMP environment variable ZFS_FORCE
 * as set by the DMA admin (or override the default of 'n', if ZFS_FORCE
 * is not set).  By default, zfs-force-override is "off", which means it
 * will not override ZFS_FORCE.
 */

static int
ndmpd_zfs_getenv_zfs_force(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	char *envp_force = NULL;
	char *override = NULL;

	ndmpd_zfs_args->nz_zfs_force = B_FALSE;
	envp_force = MOD_GETENV(ndmpd_zfs_params, "ZFS_FORCE");

	/*
	 * ZFS_FORCE is initially set to B_FALSE.
	 *
	 * To set ZFS_FORCE = B_TRUE the value must be either
	 * 't' ("true" for v3)
	 * or
	 * 'y' ("yes" for v4)
	 *
	 * All other values will not change the default (B_FALSE)
	 * of ZFS_FORCE.
	 */
	if (envp_force == NULL) {
		NDMP_LOG(LOG_DEBUG, "env(ZFS_FORCE): not specified, "
		    "defaulting to 'no'");
	} else if (strchr("tTyY", *envp_force)) {
		ndmpd_zfs_args->nz_zfs_force = B_TRUE;
		NDMP_LOG(LOG_DEBUG, "env(ZFS_FORCE): \"%s\"", envp_force);
	} else if (strchr("fFnN", *envp_force)) {
		NDMP_LOG(LOG_DEBUG, "env(ZFS_FORCE): \"%s\"", envp_force);
	} else {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_WARNING,
		    "ZFS_FORCE set to invalid string \"%s\", "
		    "defaulting to 'no'", envp_force);
	}
	override = ndmpd_get_prop(NDMP_ZFS_FORCE_OVERRIDE);

	/*
	 * The override will only be logged if
	 * zfs-force-override is set to an invalid value
	 * or it conflicts with the DMA supplied value.
	 */
	if (override) {
		if (strcasecmp(override, "yes") == 0) {
			if (!ndmpd_zfs_args->nz_zfs_force) {
				ndmpd_zfs_dma_log(ndmpd_zfs_args,
				    NDMP_LOG_WARNING,
				    "SMF property zfs-force-override set to "
				    "'yes', overriding ZFS_FORCE %s('no')",
				    envp_force ? "" : "default");
			}
			ndmpd_zfs_args->nz_zfs_force = B_TRUE;
		} else if (strcasecmp(override, "no") == 0) {
			if (ndmpd_zfs_args->nz_zfs_force) {
				ndmpd_zfs_dma_log(ndmpd_zfs_args,
				    NDMP_LOG_WARNING,
				    "SMF property zfs-force-override set to "
				    "'no', overriding ZFS_FORCE %s('yes')",
				    envp_force ? "" : "default");
			}
			ndmpd_zfs_args->nz_zfs_force = B_FALSE;
		} else if (strcasecmp(override, "off") != 0) {
			ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_WARNING,
			    "SMF property zfs-force-override set "
			    "to invalid value (%s); treating it "
			    "as 'off'.", override);
		}
	} else {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_WARNING,
		    "SMF property zfs-force-override set "
		    "to invalid value (NULL); treating it "
		    "as 'off'.");
	}
	return (0);
}

static int
ndmpd_zfs_getenv_level(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	char *envp;

	envp = MOD_GETENV(ndmpd_zfs_params, "LEVEL");

	if (envp == NULL) {
		NDMP_LOG(LOG_DEBUG, "env(LEVEL) not specified, "
		    "defaulting to 0");
		ndmpd_zfs_args->nz_level = 0;
		return (0);
	}

	if (envp[1] != '\0') {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "Invalid backup level \"%s\".\n", envp);
		return (-1);
	}

	if (!isdigit(*envp)) {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "Invalid backup level \"%s\".\n", envp);
		return (-1);
	}

	ndmpd_zfs_args->nz_level = atoi(envp);

	NDMP_LOG(LOG_DEBUG, "env(LEVEL): \"%d\"",
	    ndmpd_zfs_args->nz_level);

	return (0);
}

static int
ndmpd_zfs_getenv_update(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	char *envp_update;

	envp_update = MOD_GETENV(ndmpd_zfs_params, "UPDATE");

	if (envp_update == NULL) {
		NDMP_LOG(LOG_DEBUG,
		    "env(UPDATE) not specified, defaulting to TRUE");
		ndmpd_zfs_args->nz_update = B_TRUE;
		return (0);
	}

	/*
	 * The value can be either 't' ("true" for v3) or 'y' ("yes" for v4).
	 */

	if (strchr("tTyY", *envp_update))
		ndmpd_zfs_args->nz_update = B_TRUE;

	NDMP_LOG(LOG_DEBUG, "env(UPDATE): \"%s\"", envp_update);

	return (0);
}

static int
ndmpd_zfs_getenv_dmp_name(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	char *envp;

	envp = MOD_GETENV(ndmpd_zfs_params, "DMP_NAME");

	if (envp == NULL) {
		NDMP_LOG(LOG_DEBUG,
		    "env(DMP_NAME) not specified, defaulting to 'level'");
		(void) strlcpy(ndmpd_zfs_args->nz_dmp_name, "level",
		    NDMPD_ZFS_DMP_NAME_MAX);
		return (0);
	}

	if (!ndmpd_zfs_dmp_name_valid(ndmpd_zfs_args, envp))
		return (-1);

	(void) strlcpy(ndmpd_zfs_args->nz_dmp_name, envp,
	    NDMPD_ZFS_DMP_NAME_MAX);

	NDMP_LOG(LOG_DEBUG, "env(DMP_NAME): \"%s\"", envp);

	return (0);
}

static int
ndmpd_zfs_getenv_zfs_backup_size(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	char *zfs_backup_size;

	zfs_backup_size = MOD_GETENV(ndmpd_zfs_params, "ZFS_BACKUP_SIZE");

	if (zfs_backup_size == NULL) {
		NDMP_LOG(LOG_ERR, "ZFS_BACKUP_SIZE env is NULL");
		return (-1);
	}

	NDMP_LOG(LOG_DEBUG, "ZFS_BACKUP_SIZE: %s\n", zfs_backup_size);

	(void) sscanf(zfs_backup_size, "%llu",
	    &ndmpd_zfs_args->nz_zfs_backup_size);

	return (0);
}

/*
 * ndmpd_zfs_dmp_name_valid()
 *
 * This function verifies that the dmp_name is valid.
 *
 * The dmp_name is restricted to alphanumeric characters plus
 * the underscore and hyphen, and must be 31 characters or less.
 * This is due to its use in the NDMPD_ZFS_PROP_INCR property
 * and in the ZFS snapshot name (if an ndmpd-generated snapshot
 * is required).
 */

static boolean_t
ndmpd_zfs_dmp_name_valid(ndmpd_zfs_args_t *ndmpd_zfs_args, char *dmp_name)
{
	char *c;

	if (strlen(dmp_name) >= NDMPD_ZFS_DMP_NAME_MAX) {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "DMP_NAME %s is longer than %d\n",
		    dmp_name, NDMPD_ZFS_DMP_NAME_MAX-1);
		return (B_FALSE);
	}

	for (c = dmp_name; *c != '\0'; c++) {
		if (!isalpha(*c) && !isdigit(*c) &&
		    (*c != '_') && (*c != '-')) {
			ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
			    "DMP_NAME %s contains illegal character %c\n",
			    dmp_name, *c);
			return (B_FALSE);
		}
	}

	NDMP_LOG(LOG_DEBUG, "DMP_NAME is valid: %s\n", dmp_name);
	return (B_TRUE);
}

/*
 * ndmpd_zfs_is_incremental()
 *
 * This can only be called after ndmpd_zfs_getenv_level()
 * has been called.
 */

static boolean_t
ndmpd_zfs_is_incremental(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	return (ndmpd_zfs_args->nz_level != 0);
}

/*
 * ndmpd_zfs_snapshot_prepare()
 *
 * If no snapshot was supplied by the user, create a snapshot
 * for use by ndmpd.
 */

static int
ndmpd_zfs_snapshot_prepare(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	boolean_t recursive = B_FALSE;
	int zfs_err = 0;

	if (session->ns_data.dd_abort) {
		NDMP_LOG(LOG_DEBUG, "Backing up \"%s\" aborted.",
		    ndmpd_zfs_args->nz_dataset);
		return (-1);
	}

	if (ndmpd_zfs_args->nz_snapname[0] == '\0') {
		ndmpd_zfs_args->nz_ndmpd_snap = B_TRUE;

		if (ndmpd_zfs_snapshot_create(ndmpd_zfs_args) != 0) {
			ndmpd_zfs_args->nz_snapname[0] = '\0';

			ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
			    "Error creating snapshot for %s\n",
			    ndmpd_zfs_args->nz_dataset);

			return (-1);
		}
	}

	if (ndmpd_zfs_snapshot_prop_add(ndmpd_zfs_args)) {
		if (ndmpd_zfs_args->nz_ndmpd_snap) {

			if (ndmpd_zfs_args->nz_zfs_mode != 'd')
				recursive = B_TRUE;

			(void) snapshot_destroy(ndmpd_zfs_args->nz_dataset,
			    ndmpd_zfs_args->nz_snapname, recursive, B_FALSE,
			    NULL, &zfs_err);
		}

		return (-1);
	}

	return (0);
}

/*
 * ndmpd_zfs_snapshot_cleanup()
 *
 * If UPDATE = y, find the old snapshot (if any) corresponding to
 * {LEVEL, DMP_NAME, ZFS_MODE}. If it was ndmpd-generated,
 * remove the snapshot.  Otherwise, update its NDMPD_ZFS_PROP_INCR
 * property to remove {L, D, Z}.
 *
 * If UPDATE = n, if an ndmpd-generated snapshot was used for backup,
 * remove the snapshot.  Otherwise, update its NDMPD_ZFS_PROP_INCR
 * property to remove {L, D, Z}.
 */

static int
ndmpd_zfs_snapshot_cleanup(ndmpd_zfs_args_t *ndmpd_zfs_args, int err)
{
	ndmpd_session_t *session = (ndmpd_session_t *)
	    (ndmpd_zfs_params->mp_daemon_cookie);
	ndmpd_zfs_snapfind_t snapdata;
	boolean_t ndmpd_generated = B_FALSE;

	bzero(&snapdata, sizeof (ndmpd_zfs_snapfind_t));

	(void) ndmpd_zfs_prop_create_subprop(ndmpd_zfs_args,
	    snapdata.nzs_findprop, ZFS_MAXPROPLEN, ndmpd_zfs_args->nz_level);

	if (ndmpd_zfs_args->nz_update && !session->ns_data.dd_abort && !err) {
		/*
		 * Find the existing snapshot, if any, to "unuse."
		 * Indicate that the current snapshot used for backup
		 * should be skipped in the search.  (The search is
		 * sorted by creation time but this cannot be relied
		 * upon for user-supplied snapshots.)
		 */

		(void) snprintf(snapdata.nzs_snapskip, ZFS_MAXNAMELEN, "%s",
		    ndmpd_zfs_args->nz_snapname);

		if (ndmpd_zfs_snapshot_find(ndmpd_zfs_args, &snapdata))
			goto _remove_tmp_snap;

		if (snapdata.nzs_snapname[0] != '\0') { /* snapshot found */
			ndmpd_generated = ndmpd_zfs_snapshot_ndmpd_generated
			    (snapdata.nzs_snapprop);

			if (ndmpd_zfs_snapshot_unuse(ndmpd_zfs_args,
			    ndmpd_generated, &snapdata) != 0)
				goto _remove_tmp_snap;
		}

		if (session->ns_data.dd_abort)
			goto _remove_tmp_snap;

		return (0);
	}

_remove_tmp_snap:

	(void) snprintf(snapdata.nzs_snapname, ZFS_MAXNAMELEN, "%s",
	    ndmpd_zfs_args->nz_snapname);

	(void) strlcpy(snapdata.nzs_snapprop, ndmpd_zfs_args->nz_snapprop,
	    ZFS_MAXPROPLEN);

	snapdata.nzs_snapskip[0] = '\0';

	if (ndmpd_zfs_snapshot_unuse(ndmpd_zfs_args,
	    ndmpd_zfs_args->nz_ndmpd_snap, &snapdata) != 0)
		return (-1);

	if (!ndmpd_zfs_args->nz_update)
		return (0);

	return (-1);
}

static int
ndmpd_zfs_snapshot_create(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	boolean_t recursive = B_FALSE;

	if (ndmpd_zfs_snapname_create(ndmpd_zfs_args,
	    ndmpd_zfs_args->nz_snapname, ZFS_MAXNAMELEN -1) < 0) {
		NDMP_LOG(LOG_ERR, "Error (%d) creating snapshot name for %s",
		    errno, ndmpd_zfs_args->nz_dataset);
		return (-1);
	}

	if (ndmpd_zfs_args->nz_zfs_mode != 'd')
		recursive = B_TRUE;

	if (snapshot_create(ndmpd_zfs_args->nz_dataset,
	    ndmpd_zfs_args->nz_snapname, recursive, B_FALSE, NULL) != 0) {
		NDMP_LOG(LOG_ERR, "could not create snapshot %s@%s",
		    ndmpd_zfs_args->nz_dataset, ndmpd_zfs_args->nz_snapname);
		return (-1);
	}

	NDMP_LOG(LOG_DEBUG, "created snapshot %s@%s",
	    ndmpd_zfs_args->nz_dataset, ndmpd_zfs_args->nz_snapname);

	return (0);
}

/*
 * ndmpd_zfs_snapshot_unuse()
 *
 * Given a pre-existing snapshot of the given {L, D, Z}:
 * If snapshot is ndmpd-generated, remove snapshot.
 * If not ndmpd-generated, or if the ndmpd-generated snapshot
 * cannot be destroyed, remove the {L, D, Z} substring from the
 * snapshot's NDMPD_ZFS_PROP_INCR property.
 *
 * In the event of a failure, it may be that two snapshots will
 * have the {L, D, Z} property set on them.  This is not desirable,
 * so return an error and log the failure.
 */

static int
ndmpd_zfs_snapshot_unuse(ndmpd_zfs_args_t *ndmpd_zfs_args,
    boolean_t ndmpd_generated, ndmpd_zfs_snapfind_t *snapdata_p)
{
	boolean_t recursive = B_FALSE;
	int zfs_err = 0;
	int err = 0;

	if (ndmpd_generated) {
		if (ndmpd_zfs_args->nz_zfs_mode != 'd')
			recursive = B_TRUE;

		err = snapshot_destroy(ndmpd_zfs_args->nz_dataset,
		    snapdata_p->nzs_snapname, recursive, B_FALSE, NULL,
		    &zfs_err);

		if (err) {
			NDMP_LOG(LOG_ERR, "snapshot_destroy: %s@%s;"
			    " err: %d; zfs_err: %d",
			    ndmpd_zfs_args->nz_dataset,
			    snapdata_p->nzs_snapname, err, zfs_err);
			return (-1);
		}
	}

	if (!ndmpd_generated || zfs_err) {
		if (ndmpd_zfs_snapshot_prop_remove(ndmpd_zfs_args, snapdata_p))
			return (-1);
	}

	return (0);
}

static boolean_t
ndmpd_zfs_snapshot_ndmpd_generated(char *snapprop)
{
	char origin;

	(void) sscanf(snapprop, "%*u.%*u.%c%*s", &origin);

	return (origin == 'n');
}

/*
 * ndmpd_zfs_snapshot_find()
 *
 * Find a snapshot with a particular value for
 * the NDMPD_ZFS_PROP_INCR property.
 */

static int
ndmpd_zfs_snapshot_find(ndmpd_zfs_args_t *ndmpd_zfs_args,
    ndmpd_zfs_snapfind_t *snapdata)
{
	zfs_handle_t *zhp;
	int err;

	zhp = zfs_open(ndmpd_zfs_args->nz_zlibh, ndmpd_zfs_args->nz_dataset,
	    ndmpd_zfs_args->nz_type);

	if (!zhp) {
		NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_open");
		return (-1);
	}

	err = zfs_iter_snapshots_sorted(zhp, ndmpd_zfs_snapshot_prop_find,
	    snapdata);

	zfs_close(zhp);

	if (err) {
		NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_iter_snapshots: %d",
		    err);
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "Error iterating snapshots\n");
		return (-1);
	}

	return (0);
}

/*
 * ndmpd_zfs_snapshot_prop_find()
 *
 * Find a snapshot with a particular value for
 * NDMPD_ZFS_PROP_INCR.  Fill in data for the first one
 * found (sorted by creation time).  However, skip the
 * the snapshot indicated in nzs_snapskip, if any.
 */

static int
ndmpd_zfs_snapshot_prop_find(zfs_handle_t *zhp, void *arg)
{
	ndmpd_zfs_snapfind_t *snapdata_p = (ndmpd_zfs_snapfind_t *)arg;
	char propstr[ZFS_MAXPROPLEN];
	char findprop_plus_slash[ZFS_MAXPROPLEN];
	char *justsnap;
	int err = 0;

	if (snapdata_p->nzs_snapname[0] != '\0') {
		NDMP_LOG(LOG_DEBUG, "no need to get prop for snapshot %s",
		    (char *)zfs_get_name(zhp));
		goto _done;
	}

	err = ndmpd_zfs_snapshot_prop_get(zhp, propstr);

	if (err)
		goto _done;

	if (propstr[0] == '\0') {
		NDMP_LOG(LOG_DEBUG, "snapshot %s: propr not set",
		    (char *)zfs_get_name(zhp));
		goto _done;
	}

	(void) snprintf(findprop_plus_slash, ZFS_MAXPROPLEN, "/%s",
	    snapdata_p->nzs_findprop);

	if (!strstr((const char *)propstr,
	    (const char *)findprop_plus_slash)) {
		NDMP_LOG(LOG_DEBUG, "snapshot %s: property %s (%s not found)",
		    (char *)zfs_get_name(zhp), propstr,
		    snapdata_p->nzs_findprop);
		goto _done;
	}

	if (!ndmpd_zfs_prop_version_check(propstr,
	    &snapdata_p->nzs_prop_major, &snapdata_p->nzs_prop_minor)) {
		NDMP_LOG(LOG_DEBUG, "snapshot %s: property %s (%s found)",
		    (char *)zfs_get_name(zhp),  propstr,
		    snapdata_p->nzs_findprop);
		NDMP_LOG(LOG_DEBUG, "did not pass version check");
		goto _done;
	}

	justsnap = strchr(zfs_get_name(zhp), '@') + 1;

	if (strcmp(justsnap, snapdata_p->nzs_snapskip) != 0) {
		(void) strlcpy(snapdata_p->nzs_snapname, justsnap,
		    ZFS_MAXNAMELEN);

		(void) strlcpy(snapdata_p->nzs_snapprop, propstr,
		    ZFS_MAXPROPLEN);

		NDMP_LOG(LOG_DEBUG, "found match: %s [%s]\n",
		    snapdata_p->nzs_snapname, snapdata_p->nzs_snapprop);
	}

_done:
	zfs_close(zhp);
	return (err);
}

/*
 * ndmpd_zfs_snapshot_prop_get()
 *
 * Retrieve NDMPD_ZFS_PROP_INCR property from snapshot
 */

static int
ndmpd_zfs_snapshot_prop_get(zfs_handle_t *zhp, char *propstr)
{
	nvlist_t *userprop;
	nvlist_t *propval;
	char *strval;
	int err;

	propstr[0] = '\0';

	userprop = zfs_get_user_props(zhp);

	if (userprop == NULL)
		return (0);

	err = nvlist_lookup_nvlist(userprop, NDMPD_ZFS_PROP_INCR, &propval);

	if (err != 0) {
		if (err == ENOENT)
			return (0);

		NDMP_LOG(LOG_ERR, "nvlist_lookup_nvlist error: %d\n", err);

		return (-1);
	}

	err = nvlist_lookup_string(propval, ZPROP_VALUE, &strval);

	if (err != 0) {
		if (err == ENOENT)
			return (0);

		NDMP_LOG(LOG_ERR, "nvlist_lookup_string error: %d\n", err);

		return (-1);
	}

	(void) strlcpy(propstr, strval, ZFS_MAXPROPLEN);

	return (0);
}

/*
 * ndmpd_zfs_snapshot_prop_add()
 *
 * Update snapshot's NDMPD_ZFS_PROP_INCR property with
 * the current LEVEL, DMP_NAME, and ZFS_MODE values
 * (add property if it doesn't exist)
 */

static int
ndmpd_zfs_snapshot_prop_add(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	char fullname[ZFS_MAXNAMELEN];
	char propstr[ZFS_MAXPROPLEN];
	zfs_handle_t *zhp;
	boolean_t set;
	int err;

	(void) snprintf(fullname, ZFS_MAXNAMELEN, "%s@%s",
	    ndmpd_zfs_args->nz_dataset,
	    ndmpd_zfs_args->nz_snapname);

	zhp = zfs_open(ndmpd_zfs_args->nz_zlibh, fullname, ZFS_TYPE_SNAPSHOT);

	if (!zhp) {
		NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_open (snap)");
		return (-1);
	}

	bzero(propstr, ZFS_MAXPROPLEN);

	if (ndmpd_zfs_snapshot_prop_get(zhp, propstr)) {
		zfs_close(zhp);
		return (-1);
	}

	if (ndmpd_zfs_snapshot_prop_create(ndmpd_zfs_args, propstr, &set)) {
		zfs_close(zhp);
		return (-1);
	}

	if (set) {
		err = zfs_prop_set(zhp, NDMPD_ZFS_PROP_INCR, propstr);
		if (err) {
			NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_prop_set: %d",
			    err);
			NDMP_LOG(LOG_ERR, "error setting property %s",
			    propstr);
			zfs_close(zhp);
			return (-1);
		}
	}

	zfs_close(zhp);

	(void) strlcpy(ndmpd_zfs_args->nz_snapprop, propstr, ZFS_MAXPROPLEN);

	return (0);
}

static int
ndmpd_zfs_snapshot_prop_create(ndmpd_zfs_args_t *ndmpd_zfs_args,
    char *propstr, boolean_t *set)
{
	char subprop[ZFS_MAXPROPLEN];
	char *p = propstr;
	int slash_count = 0;

	*set = B_TRUE;

	(void) ndmpd_zfs_prop_create_subprop(ndmpd_zfs_args,
	    subprop, ZFS_MAXPROPLEN, ndmpd_zfs_args->nz_level);

	if (propstr[0] == '\0') {
		(void) snprintf(propstr, ZFS_MAXPROPLEN, "%u.%u.%c/%s",
		    NDMPD_ZFS_PROP_MAJOR_VERSION,
		    NDMPD_ZFS_PROP_MINOR_VERSION,
		    (ndmpd_zfs_args->nz_ndmpd_snap) ? 'n' : 'u',
		    subprop);
		return (0);
	}

	if (strstr((const char *)propstr, (const char *)subprop)) {
		NDMP_LOG(LOG_DEBUG, "Did not add entry %s as it already exists",
		    subprop);
		*set = B_FALSE;
		return (0);
	}

	while (*p) {
		if (*(p++) == '/')
			slash_count++;
	}

	if (slash_count >= NDMPD_ZFS_SUBPROP_MAX) {
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "snapshot %s: user property %s limit of %d subprops "
		    "reached; cannot complete operation",
		    ndmpd_zfs_args->nz_snapname,
		    NDMPD_ZFS_PROP_INCR,
		    NDMPD_ZFS_SUBPROP_MAX);
		return (-1);
	}

	assert((strlen(propstr) + strlen(subprop) + 2) < ZFS_MAXPROPLEN);

	(void) strlcat(propstr, "/", ZFS_MAXPROPLEN);
	(void) strlcat(propstr, subprop, ZFS_MAXPROPLEN);

	return (0);
}

static int
ndmpd_zfs_prop_create_subprop(ndmpd_zfs_args_t *ndmpd_zfs_args,
    char *subprop, int len, int level)
{
	return (snprintf(subprop, len, "%d.%s.%c",
	    level,
	    ndmpd_zfs_args->nz_dmp_name,
	    ndmpd_zfs_args->nz_zfs_mode));
}

/*
 * ndmpd_zfs_snapshot_prop_remove()
 *
 * Remove specified substring from the snapshot's
 * NDMPD_ZFS_PROP_INCR property
 */

static int
ndmpd_zfs_snapshot_prop_remove(ndmpd_zfs_args_t *ndmpd_zfs_args,
    ndmpd_zfs_snapfind_t *snapdata_p)
{
	char findprop_plus_slash[ZFS_MAXPROPLEN];
	char fullname[ZFS_MAXNAMELEN];
	char newprop[ZFS_MAXPROPLEN];
	char tmpstr[ZFS_MAXPROPLEN];
	zfs_handle_t *zhp;
	char *ptr;
	int err;

	(void) snprintf(fullname, ZFS_MAXNAMELEN, "%s@%s",
	    ndmpd_zfs_args->nz_dataset,
	    snapdata_p->nzs_snapname);

	zhp = zfs_open(ndmpd_zfs_args->nz_zlibh, fullname, ZFS_TYPE_SNAPSHOT);

	if (!zhp) {
		NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_open");
		return (-1);
	}

	bzero(newprop, ZFS_MAXPROPLEN);

	/*
	 * If the substring to be removed is the only {L, D, Z}
	 * in the property, remove the entire property
	 */

	tmpstr[0] = '\0';

	(void) sscanf(snapdata_p->nzs_snapprop, "%*u.%*u.%*c/%1023s", tmpstr);

	if (strcmp(tmpstr, snapdata_p->nzs_findprop) == 0) {

		err = zfs_prop_set(zhp, NDMPD_ZFS_PROP_INCR, newprop);

		zfs_close(zhp);

		if (err) {
			NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_prop_set: %d",
			    err);
			NDMP_LOG(LOG_ERR, "error setting property %s", newprop);
			return (-1);
		}

		return (0);
	}

	(void) snprintf(findprop_plus_slash, ZFS_MAXPROPLEN, "/%s",
	    snapdata_p->nzs_findprop);

	ptr = strstr((const char *)snapdata_p->nzs_snapprop,
	    (const char *)findprop_plus_slash);

	if (ptr == NULL) {
		/*
		 * This shouldn't happen.  Just return success.
		 */
		zfs_close(zhp);

		return (0);
	}

	/*
	 * Remove "nzs_findprop" substring from property
	 *
	 * Example property:
	 *	0.0.u/1.bob.p/0.jane.d
	 *
	 * Note that there will always be a prefix to the
	 * strstr() result.  Hence the below code works for
	 * all cases.
	 */

	ptr--;
	(void) strncpy(newprop, snapdata_p->nzs_snapprop,
	    (char *)ptr - snapdata_p->nzs_snapprop);
	ptr += strlen(snapdata_p->nzs_findprop) + 1;
	(void) strlcat(newprop, ptr, ZFS_MAXPROPLEN);

	err = zfs_prop_set(zhp, NDMPD_ZFS_PROP_INCR, newprop);

	zfs_close(zhp);

	if (err) {
		NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_prop_set: %d", err);
		NDMP_LOG(LOG_ERR, "error modifying property %s", newprop);
		return (-1);
	}

	return (0);
}

static boolean_t
ndmpd_zfs_prop_version_check(char *propstr, uint32_t *major, uint32_t *minor)
{
	(void) sscanf(propstr, "%u.%u.%*c%*s", major, minor);

	if (*major > NDMPD_ZFS_PROP_MAJOR_VERSION) {
		NDMP_LOG(LOG_ERR, "unsupported prop major (%u > %u)",
		    *major, NDMPD_ZFS_PROP_MAJOR_VERSION);
		return (B_FALSE);
	}

	if (*minor > NDMPD_ZFS_PROP_MINOR_VERSION) {
		NDMP_LOG(LOG_ERR, "later prop minor (%u > %u)",
		    *minor, NDMPD_ZFS_PROP_MINOR_VERSION);
	}

	NDMP_LOG(LOG_DEBUG, "passed version check: "
	    "supported prop major (%u <= %u); (snapprop minor: %u [%u])",
	    *major, NDMPD_ZFS_PROP_MAJOR_VERSION,
	    *minor, NDMPD_ZFS_PROP_MINOR_VERSION);

	return (B_TRUE);
}

static int
ndmpd_zfs_snapname_create(ndmpd_zfs_args_t *ndmpd_zfs_args,
    char *snapname, int namelen)
{
	char subprop[ZFS_MAXPROPLEN];
	struct timeval tp;
	int err = 0;

	(void) ndmpd_zfs_prop_create_subprop(ndmpd_zfs_args,
	    subprop, ZFS_MAXPROPLEN, ndmpd_zfs_args->nz_level);

	(void) gettimeofday(&tp, NULL);

	err = snprintf(snapname, namelen,
	    "%s.%s.%ld.%ld",
	    NDMP_BASENAME,
	    subprop,
	    tp.tv_sec,
	    tp.tv_usec);

	if (err > namelen) {
		NDMP_LOG(LOG_ERR, "name of snapshot [%s...] would exceed %d",
		    snapname, namelen);
		return (-1);
	}

	return (0);
}

static void
ndmpd_zfs_zerr_dma_log(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	switch (libzfs_errno(ndmpd_zfs_args->nz_zlibh)) {
	case EZFS_EXISTS:
	case EZFS_BUSY:
	case EZFS_NOENT:
	case EZFS_INVALIDNAME:
	case EZFS_MOUNTFAILED:
	case EZFS_UMOUNTFAILED:
	case EZFS_NAMETOOLONG:
	case EZFS_BADRESTORE:

		/* use existing error text */

		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "%s: %s: %s\n",
		    ndmpd_zfs_args->nz_dataset,
		    libzfs_error_action(ndmpd_zfs_args->nz_zlibh),
		    libzfs_error_description(ndmpd_zfs_args->nz_zlibh));

		break;

	case EZFS_NOMEM:
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "Unable to obtain memory for operation\n");
		break;

	case EZFS_PROPSPACE:
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "A bad ZFS quota or reservation was encountered.\n");
		break;

	case EZFS_BADSTREAM:
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "The backup stream is invalid.\n");
		break;

	case EZFS_ZONED:
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "An error related to the local zone occurred.\n");
		break;

	case EZFS_NOSPC:
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "No more space is available\n");
		break;

	case EZFS_IO:
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "An I/O error occurred.\n");
		break;

	default:
		ndmpd_zfs_dma_log(ndmpd_zfs_args, NDMP_LOG_ERROR,
		    "An internal ndmpd error occurred.  "
		    "Please contact support\n");
		break;
	}
}

void
ndmpd_zfs_dma_log(ndmpd_zfs_args_t *ndmpd_zfs_args, ndmp_log_type log_type,
    char *format, ...)
{
	static char buf[1024];
	va_list ap;

	va_start(ap, format);

	/*LINTED variable format specifier */
	(void) vsnprintf(buf, sizeof (buf), format, ap);
	va_end(ap);

	MOD_LOGV3(ndmpd_zfs_params, log_type, buf);

	if ((log_type) == NDMP_LOG_ERROR) {
		NDMP_LOG(LOG_ERR, buf);
	} else {
		NDMP_LOG(LOG_NOTICE, buf);
	}
}

static int
ndmpd_send_fhist_root(ndmpd_session_t *session)
{
	struct stat64 st;

	/* Build up a sample root dir stat */
	(void) memset(&st, 0, sizeof (struct stat64));
	st.st_mode = S_IFDIR | 0777;
	st.st_mtime = st.st_atime = st.st_ctime = time(NULL);
	st.st_uid = st.st_gid = 0;
	st.st_size = 1;
	st.st_nlink = 1;

	if (ndmpd_api_file_history_dir_v3(session, ".", ROOT_INODE,
	    ROOT_INODE) != 0)
		return (-1);
	if (ndmpd_api_file_history_dir_v3(session, "..", ROOT_INODE,
	    ROOT_INODE) != 0)
		return (-1);
	if (ndmpd_api_file_history_node_v3(session, ROOT_INODE, &st, 0) != 0)
		return (-1);

	return (0);
}

/*ARGSUSED*/
void
diff_fhist_cb(char *dsmnt, uint64_t obj, char type, char *fobjname,
    char *tobjname, zfs_stat_t *fsp, zfs_stat_t *tsp, int delta,
    void *arg)
{
	ndmpd_session_t *session = arg;
	ndmpd_zfs_args_t *ndmpd_zfs_args = &session->ns_ndmpd_zfs_args;
	struct stat64 st;
	zfs_stat_t *msp;
	uint64_t fh_info;
	ulong_t pino;
	char *s;

	msp = (tsp != NULL) ? tsp : fsp;
	if (!msp || tobjname == NULL) {
		NDMP_LOG(LOG_DEBUG, "FH: tsp == NULL");
		return;
	}

	if ((s = strrchr(tobjname, '/')) != NULL)
		s++;

	pino = (ulong_t)msp->zs_parent;
	if (!*s || (obj == pino))
		ndmpd_zfs_args->nz_rootdir = obj;

	/* Build up the node stat */
	(void) memset(&st, 0, sizeof (struct stat64));
	st.st_mode = msp->zs_mode;
	st.st_mtime = msp->zs_mtime[0];
	st.st_atime = msp->zs_atime[0];
	st.st_ctime = msp->zs_ctime[0];
	st.st_uid = msp->zs_uid;
	st.st_gid = msp->zs_gid;
	st.st_size = msp->zs_size;
	st.st_nlink = msp->zs_links;
	fh_info = 0;

	if ((ulong_t)obj == ndmpd_zfs_args->nz_rootdir)
		obj = ROOT_INODE;

	if (pino == ndmpd_zfs_args->nz_rootdir)
		pino = ROOT_INODE;

	if (*s) {
		(void) ndmpd_api_file_history_dir_v3(session, s, (ulong_t)obj,
		    pino);
		(void) ndmpd_api_file_history_node_v3(session, (ulong_t)obj,
		    &st, fh_info);
		return;
	}

	(void) ndmpd_api_file_history_dir_v3(session, ".", (ulong_t)obj, pino);
	(void) ndmpd_api_file_history_node_v3(session, (ulong_t)obj, &st,
	    fh_info);
	(void) ndmpd_api_file_history_dir_v3(session, "..", pino, (ulong_t)obj);
	if (pino != (ulong_t)obj)
		(void) ndmpd_api_file_history_node_v3(session, pino, &st,
		    fh_info);
}

/*ARGSUSED*/
void
diff_fhist_inc_cb(char *dsmnt, uint64_t obj, char type, char *fobjname,
    char *tobjname, zfs_stat_t *fsp, zfs_stat_t *tsp, int delta,
    void *arg)
{
	ndmpd_session_t *session = arg;
	struct stat64 st;
	zfs_stat_t *msp;
	uint64_t fh_info;
	ulong_t pino, ppino;
	char *s, *p;
	char *namep;
	static ulong_t base_ino = 1000;

	msp = (tsp != NULL) ? tsp : fsp;
	if (!msp || tobjname == NULL) {
		NDMP_LOG(LOG_DEBUG, "FH: tsp == NULL");
		return;
	}

	namep = strdup(tobjname);

	/* Build up a sample dir stat */
	(void) memset(&st, 0, sizeof (struct stat64));
	st.st_mode = S_IFDIR | 0777;
	st.st_mtime = st.st_atime = st.st_ctime = time(NULL);
	st.st_uid = st.st_gid = 0;
	st.st_size = 1;
	st.st_nlink = 1;
	fh_info = 0;

	p = namep;

	/* skip the first entry */
	if ((p = strchr(p, '/')) != NULL)
		p++;

	/* send all the parent directories */
	ppino = ROOT_INODE;
	while ((s = strchr(p, '/')) != NULL) {
		*s++ = 0;
		pino = base_ino++;
		(void) ndmpd_api_file_history_dir_v3(session, p, pino, ppino);
		(void) ndmpd_api_file_history_node_v3(session, pino, &st,
		    fh_info);
		(void) ndmpd_api_file_history_dir_v3(session, ".", pino, ppino);
		(void) ndmpd_api_file_history_node_v3(session, pino, &st,
		    fh_info);
		(void) ndmpd_api_file_history_dir_v3(session, "..", ppino,
		    pino);
		(void) ndmpd_api_file_history_node_v3(session, ppino, &st,
		    fh_info);
		ppino = pino;
		p = s;
	}

	if (*p) {
		/* Build up the node stat */
		(void) memset(&st, 0, sizeof (struct stat64));
		st.st_mode = msp->zs_mode;
		st.st_mtime = msp->zs_mtime[0];
		st.st_atime = msp->zs_atime[0];
		st.st_ctime = msp->zs_ctime[0];
		st.st_uid = msp->zs_uid;
		st.st_gid = msp->zs_gid;
		st.st_size = msp->zs_size;
		st.st_nlink = msp->zs_links;

		(void) ndmpd_api_file_history_dir_v3(session, p, (ulong_t)obj,
		    pino);
		(void) ndmpd_api_file_history_node_v3(session, (ulong_t)obj,
		    &st, fh_info);
	}

	free(namep);
}

/*
 * ZFS file history thread uses similar method to libzfs' zfs_show_diff()
 * to enumerate all the files backed up
 */
static int
ndmpd_zfs_fhist(ndmpd_zfs_args_t *ndmpd_zfs_args)
{
	ndmpd_session_t *session;
	zfs_handle_t *zhp;
	char *fromsnap;
	char *tosnap;
	char tosnapname[ZFS_MAXNAMELEN];
	char fromsnapname[ZFS_MAXNAMELEN];
	int flags;
	int rv;

	session = (ndmpd_session_t *)(ndmpd_zfs_params->mp_daemon_cookie);

	if (ndmpd_zfs_args->nz_type == ZFS_TYPE_VOLUME)
		return (ndmpd_send_fhist_root(session));

	(void) snprintf(tosnapname, ZFS_MAXNAMELEN, "%s@%s",
	    ndmpd_zfs_args->nz_dataset,
	    ndmpd_zfs_args->nz_snapname);
	tosnap = tosnapname;

	zhp = zfs_open(ndmpd_zfs_args->nz_zlibh,
	    ndmpd_zfs_args->nz_dataset, ndmpd_zfs_args->nz_type);
	if (!zhp) {
		NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_open");
		return (-1);
	}

	if (ndmpd_zfs_is_incremental(ndmpd_zfs_args)) {
		if (ndmpd_zfs_args->nz_fromsnap[0] == '\0') {
			NDMP_LOG(LOG_ERR, "no fromsnap");
			zfs_close(zhp);
			return (-1);
		}
		(void) snprintf(fromsnapname, ZFS_MAXNAMELEN, "%s@%s",
		    ndmpd_zfs_args->nz_dataset,
		    ndmpd_zfs_args->nz_fromsnap);
		fromsnap = fromsnapname;

		(void) ndmpd_send_fhist_root(session);

		flags = ZFS_DIFF_ENUMERATE;
		rv = zfs_scan_diffs(zhp, fromsnap, tosnap, flags,
		    diff_fhist_inc_cb, session);

	} else {
		flags = ZFS_DIFF_ENUMERATE | ZFS_DIFF_BASE;
		rv = zfs_scan_diffs(zhp, NULL, tosnap, flags,
		    diff_fhist_cb, session);
	}

	if (rv != 0)
		NDMPD_ZFS_LOG_ZERR(ndmpd_zfs_args, "zfs_scan_diffs");

	zfs_close(zhp);
	return (rv);
}
