/*
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * LEGAL NOTICE
 *
 * This file contains source code that implements the Sockets Direct
 * Protocol (SDP) as defined by the InfiniBand Architecture Specification,
 * Volume 1, Annex A4, Version 1.1.  Due to restrictions in the SDP license,
 * source code contained in this file may not be distributed outside of
 * Sun Microsystems without further legal review to ensure compliance with
 * the license terms.
 *
 * Sun employees and contactors are cautioned not to extract source code
 * from this file and use it for other purposes.  The SDP implementation
 * code in this and other files must be kept separate from all other source
 * code.
 *
 * As required by the license, the following notice is added to the source
 * code:
 *
 * This source code may incorporate intellectual property owned by
 * Microsoft Corporation.  Our provision of this source code does not
 * include any licenses or any other rights to you under any Microsoft
 * intellectual property.  If you would like a license from Microsoft
 * (e.g., to rebrand, redistribute), you need to contact Microsoft
 * directly.
 */

/*
 * Sun elects to include this software in this distribution under the
 * OpenIB.org BSD license
 *
 *
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _SYS_IB_CLIENTS_SDP_BUFF_P_H
#define	_SYS_IB_CLIENTS_SDP_BUFF_P_H

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * Definitions
 */
#define	SDP_BUFFER_COUNT_MIN 1024
#define	SDP_BUFFER_COUNT_MAX 1048756
#define	SDP_BUFFER_COUNT_INC 128

#define	SDP_POOL_NAME_MAX   16	/* maximum size pool name */
#define	SDP_ACTIVE_POOL_NAME  "Active per hca"
#define	SDP_BACKUP_POOL_NAME  "Backup per hca"
#define	SDP_BUFF_OUT_LEN    33	/* size of buffer output line */

/*
 * Types
 */
struct sdp_dev_hca_s;
struct sdp_memory_segment_s;

/*
 * Structures
 */
typedef struct sdp_main_pool_s {
	/*
	 * variant
	 */
	sdp_pool_t active_pool;	/* active pool of buffers */
	sdp_pool_t backup_pool;	/* backup pool of freed buffers  */
	kmutex_t active_pool_lock;	/* spin lock for pool access */
	kmutex_t backup_pool_lock;	/* spin lock for pool access */

	uint32_t buff_min;
	uint32_t buff_max;
	uint32_t buff_cur;
	uint32_t buff_size;	/* size of each buffer in the pool */

	struct sdp_memory_segment_s *segs;

	/*
	 * Hca associated with this pool (from which memory is registered and
	 * deregistered with a specific hca).
	 */
	struct sdp_dev_hca_s *hca;
} sdp_main_pool_t;

/*
 * Each memory segment is its own 4K page.
 */
typedef struct sdp_mem_seg_head_s {
	struct sdp_memory_segment_s *next;
	struct sdp_memory_segment_s *prev;
	uint32_t size;
} sdp_mem_seg_head_t;

extern int sdp_msg_buff_size;

#define	SDP_MSG_BUFF_SIZE	8192
#define	SDP_BUFF_COUNT		4096
#define	SDP_MIN_BUFF_COUNT 	3

typedef struct sdp_memory_segment_s {
	sdp_mem_seg_head_t head;
	sdp_buff_t *list;
	size_t alloc_size;
} sdp_memory_segment_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_IB_CLIENTS_SDP_BUFF_P_H */
