#!/usr/sbin/dtrace -s
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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
#pragma D option flowindent
*/

/*
 * Usage:	./cifs.d -p `pgrep smbd`
 *
 * On multi-processor systems, it may be easier to follow the output
 * if run on a single processor: see psradm.  For example, to disable
 * the second processor on a dual-processor system:	psradm -f 1
 */

BEGIN
{
	printf("SMB Trace Started");
	printf("\n\n");
}

END
{
	printf("SMB Trace Ended");
	printf("\n\n");
}

fbt:smbsrv:smb_pre_*:entry
{
	sr = (smb_request_t *)arg0;

	printf("cmd=%d [uid=%d tid=%d]",
	    sr->sr_com_current, sr->sr_header.hd_uid, sr->sr_header.hd_tid);

	self->status = 0;
}

fbt:smbsrv:smb_pre_*:return
{
}

fbt:smbsrv:smb_post_*:entry
{
	thsd = (smb_thsd_t *)curthread->t_tsd->ts_value[`smb_thkey - 1];
	err = &thsd->tsd_errcode;
	sr = (smb_request_t *)arg0;

	printf("cmd[%d]: status=0x%08x (class=%d code=%d)",
	    sr->sr_com_current, err->se_status,
	    err->se_error.de_class, err->se_error.de_code);

	self->status = err->se_status;
}

fbt:smbsrv:smb_post_*:return
{
}

fbt:smbsrv:smb_post_negotiate:entry
{
	thsd = (smb_thsd_t *)curthread->t_tsd->ts_value[`smb_thkey - 1];
	err = &thsd->tsd_errcode;
	sr = (smb_request_t *)arg0;
	negprot = (smb_arg_negotiate_t *)sr->arg.negprot;

	printf("dialect=%s index=%u caps=0x%08x maxmpx=%u tz=%d time=%u",
	    stringof(negprot->ni_name),
	    negprot->ni_index,
	    negprot->ni_capabilities,
	    negprot->ni_maxmpxcount,
	    negprot->ni_tzcorrection,
	    negprot->ni_servertime.tv_sec);

	printf(" [status=0x%08x (class=%d code=%d)]",
	    err->se_status,
	    err->se_error.de_class, err->se_error.de_code);

	self->status = err->se_status;
}

fbt:smbsrv:smb_pre_session_setup_andx:entry
{
	self->sr = (smb_request_t *)arg0;
}

fbt:smbsrv:smb_pre_session_setup_andx:return
{
	sr = (smb_request_t *)self->sr;
	authreq = (smb_authreq_t *)&self->sr->arg.ssetup->ss_authreq;

	printf("[%s] %s %s %s",
	    (sr->session->s_local_port == 139) ? "NBT" : "TCP",
	    (sr->session->s_local_port == 139) ?
	    stringof(sr->session->workstation) : "",
	    stringof(authreq->au_domain),
	    stringof(authreq->au_username));

	printf(" maxmpx=%u vc=%u maxbuf=%u",
	    authreq->au_maxmpxcount,
	    sr->session->vcnumber,
	    sr->session->smb_msg_size);
}

fbt:smbsrv:smb_post_session_setup_andx:entry
{
	thsd = (smb_thsd_t *)curthread->t_tsd->ts_value[`smb_thkey - 1];
	err = &thsd->tsd_errcode;
	sr = (smb_request_t *)arg0;
	authreq = (smb_authreq_t *)&sr->arg.ssetup->ss_authreq;

	printf("%s/%s: smbuid=%d (%s)",
	    stringof(sr->uid_user->u_domain),
	    stringof(sr->uid_user->u_name),
	    sr->sr_header.hd_uid,
	    (authreq->au_guest == 0) ? "user" : "guest");

	printf(" [status=0x%08x (class=%d code=%d)]",
	    err->se_status,
	    err->se_error.de_class, err->se_error.de_code);

	self->status = err->se_status;
}

fbt:smbsrv:smb_pre_logoff_andx:entry
{
	sr = (smb_request_t *)arg0;

	printf("uid %d: %s/%s", sr->sr_header.hd_uid,
	    stringof(sr->uid_user->u_domain),
	    stringof(sr->uid_user->u_name));
}

fbt:smbsrv:smb_pre_tree_connect_andx:entry
{
	self->sr = (smb_request_t *)arg0;
}

fbt:smbsrv:smb_pre_tree_connect_andx:return
{
	tcon = (struct tcon *)&self->sr->arg.tcon;

	printf("[%s] %s",
                stringof(tcon->service),
                stringof(tcon->path));
}

fbt:smbsrv:smb_post_tree_connect_andx:entry
{
	thsd = (smb_thsd_t *)curthread->t_tsd->ts_value[`smb_thkey - 1];
	err = &thsd->tsd_errcode;
	sr = (smb_request_t *)arg0;

	printf("tid %d: %s", sr->sr_header.hd_tid,
	    (err->se_status == 0) ?
	    stringof(sr->tid_tree->t_sharename) : "");

	printf(" [status=0x%08x (class=%d code=%d)]",
	    err->se_status,
	    err->se_error.de_class, err->se_error.de_code);
}

fbt:smbsrv:smb_pre_tree_disconnect:entry
{
	self->sr = (smb_request_t *)arg0;
}

fbt:smbsrv:smb_pre_tree_disconnect:return
/self->sr->tid_tree != NULL/
{
	sr = (smb_request_t *)self->sr;

	printf("tid %d: %s", sr->sr_header.hd_tid,
	    stringof(sr->tid_tree->t_sharename));
}

fbt:smbsrv:smb_pre_tree_disconnect:return
/self->sr->tid_tree == NULL/
{
	sr = (smb_request_t *)self->sr;

	printf("tid %d: -", sr->sr_header.hd_tid);
}

fbt:smbsrv:smb_pre_nt_transact_create:entry
{
	self->sr = (smb_request_t *)arg0;
}

fbt:smbsrv:smb_pre_nt_transact_create:return
{
	op =  (smb_arg_open_t *)&self->sr->arg.open;

	printf("%s", stringof(op->fqi.fq_path.pn_path));
}

smb:::op-Open-start,
smb:::op-OpenX-start,
smb:::op-Create-start,
smb:::op-CreateNew-start,
smb:::op-CreateTemporary-start,
smb:::op-CreateDirectory-start,
smb:::op-NtCreateX-start
{
	printf("%s", args[1]->soi_curpath);
}

fbt:smbsrv:smb_post_open:entry,
fbt:smbsrv:smb_post_open_andx:entry,
fbt:smbsrv:smb_post_create:entry,
fbt:smbsrv:smb_post_create_new:entry,
fbt:smbsrv:smb_post_create_temporary:entry,
fbt:smbsrv:smb_post_create_directory:entry,
fbt:smbsrv:smb_post_nt_create_andx:entry,
fbt:smbsrv:smb_post_nt_transact_create:entry
{
	sr = (smb_request_t *)arg0;

	printf("%s: fid=%u",
	    stringof(sr->arg.open.fqi.fq_path.pn_path), sr->smb_fid);
}

fbt:smbsrv:smb_pre_read:entry,
fbt:smbsrv:smb_pre_lock_and_read:entry,
fbt:smbsrv:smb_pre_read_andx:entry,
fbt:smbsrv:smb_pre_read_raw:entry,
fbt:smbsrv:smb_pre_write:entry,
fbt:smbsrv:smb_pre_write_and_close:entry,
fbt:smbsrv:smb_pre_write_and_unlock:entry,
fbt:smbsrv:smb_pre_write_andx:entry,
fbt:smbsrv:smb_pre_write_raw:entry
{
	self->sr = (smb_request_t *)arg0;
}

fbt:smbsrv:smb_pre_read:return,
fbt:smbsrv:smb_pre_lock_and_read:return,
fbt:smbsrv:smb_pre_read_andx:return,
fbt:smbsrv:smb_pre_read_raw:return,
fbt:smbsrv:smb_pre_write:return,
fbt:smbsrv:smb_pre_write_and_close:return,
fbt:smbsrv:smb_pre_write_and_unlock:return,
fbt:smbsrv:smb_pre_write_andx:return,
fbt:smbsrv:smb_pre_write_raw:return
{
	sr = (smb_request_t *)self->sr;
	rw =  (smb_rw_param_t *)self->sr->arg.rw;

	printf("fid=%d: %u bytes at offset %u",
	    sr->smb_fid, rw->rw_count, rw->rw_offset);
}

fbt:smbsrv:smb_post_read:entry,
fbt:smbsrv:smb_post_lock_and_read:entry,
fbt:smbsrv:smb_post_read_andx:entry,
fbt:smbsrv:smb_post_read_raw:entry
/self->status == 0/
{
	sr = (smb_request_t *)arg0;
	rw =  (smb_rw_param_t *)sr->arg.rw;

	printf("fid=%d: %u bytes at offset %u",
	    sr->smb_fid, rw->rw_count, rw->rw_offset);
}

fbt:smbsrv:smb_pre_rename:entry
{
	self->sr = (smb_request_t *)arg0;
}

fbt:smbsrv:smb_pre_rename:return
{
	p = (smb_arg_dirop_t *)&self->sr->arg.dirop;

	printf("%s to %s",
	    stringof(p->fqi.fq_path.pn_path),
	    stringof(p->dst_fqi.fq_path.pn_path));
}

fbt:smbsrv:smb_pre_check_directory:entry,
fbt:smbsrv:smb_pre_create_directory:entry,
fbt:smbsrv:smb_pre_delete_directory:entry,
fbt:smbsrv:smb_pre_delete:entry
{
	self->sr = (smb_request_t *)arg0;
}

fbt:smbsrv:smb_pre_check_directory:return,
fbt:smbsrv:smb_pre_create_directory:return,
fbt:smbsrv:smb_pre_delete_directory:return,
fbt:smbsrv:smb_pre_delete:return
{
	p = (smb_arg_dirop_t *)&self->sr->arg.dirop;

	printf("%s", stringof(p->fqi.fq_path.pn_path));
}

