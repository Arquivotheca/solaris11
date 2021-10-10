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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */



#include "nfs_mdb.h"
#include "nfs4_mdb.h"

static char *
fact_to_str(nfs4_fact_type_t ftype)
{
	switch (ftype) {
	case RF_BADOWNER:
		return ("RF_BADOWNER");
	case RF_ERR:
		return ("RF_ERR");
	case RF_RENEW_EXPIRED:
		return ("RF_RENEW_EXPIRED");
	case RF_SRV_NOT_RESPOND:
		return ("RF_SRV_NOT_RESPOND");
	case RF_SRV_OK:
		return ("RF_SRV_OK");
	case RF_SRVS_NOT_RESPOND:
		return ("RF_SRVS_NOT_RESPOND");
	case RF_SRVS_OK:
		return ("RF_SRVS_OK");
	case RF_DELMAP_CB_ERR:
		return ("RF_DELMAP_CB_ERR");
	default:
		return ("Illegal_Fact");
	}
}

static char *
event_to_str(nfs4_event_type_t etype)
{
	switch (etype) {
	case RE_BAD_SEQID:
		return ("RE_BAD_SEQID");
	case RE_BADHANDLE:
		return ("RE_BADHANDLE");
	case RE_CLIENTID:
		return ("RE_CLIENTID");
	case RE_DEAD_FILE:
		return ("RE_DEAD_FILE");
	case RE_END:
		return ("RE_END");
	case RE_FAIL_RELOCK:
		return ("RE_FAIL_RELOCK");
	case RE_FAIL_REMAP_LEN:
		return ("RE_FAIL_REMAP_LEN");
	case RE_FAIL_REMAP_OP:
		return ("RE_FAIL_REMAP_OP");
	case RE_FAILOVER:
		return ("RE_FAILOVER");
	case RE_FILE_DIFF:
		return ("RE_FILE_DIFF");
	case RE_LOST_STATE:
		return ("RE_LOST_STATE");
	case RE_OPENS_CHANGED:
		return ("RE_OPENS_CHANGED");
	case RE_SIGLOST:
		return ("RE_SIGLOST");
	case RE_SIGLOST_NO_DUMP:
		return ("RE_SIGLOST_NO_DUMP");
	case RE_START:
		return ("RE_START");
	case RE_UNEXPECTED_ACTION:
		return ("RE_UNEXPECTED_ACTION");
	case RE_UNEXPECTED_ERRNO:
		return ("RE_UNEXPECTED_ERRNO");
	case RE_UNEXPECTED_STATUS:
		return ("RE_UNEXPECTED_STATUS");
	case RE_WRONGSEC:
		return ("RE_WRONGSEC");
	case RE_LOST_STATE_BAD_OP:
		return ("RE_LOST_STATE_BAD_OP");
	default:
		return ("Illegal_Event");
	}
}

/*
 * Global array of ctags for the nfs4_diag dcmd.
 */
ctag_t nfs4_diag_ctags[] = NFS4_TAG_INITIALIZER;

static char *
action_to_str(nfs4_recov_t recov_action)
{
	switch (recov_action) {
	case NR_UNUSED:
		return ("NR_UNUSED");
	case NR_CLIENTID:
		return ("NR_CLIENTID");
	case NR_OPENFILES:
		return ("NR_OPENFILES");
	case NR_FHEXPIRED:
		return ("NR_FHEXPIRED");
	case NR_FAILOVER:
		return ("NR_FAILOVER");
	case NR_WRONGSEC:
		return ("NR_WRONGSEC");
	case NR_EXPIRED:
		return ("NR_EXPIRED");
	case NR_BAD_STATEID:
		return ("NR_BAD_STATEID");
	case NR_BAD_SEQID:
		return ("NR_BAD_SEQID");
	case NR_BADHANDLE:
		return ("NR_BADHANDLE");
	case NR_OLDSTATEID:
		return ("NR_OLDSTATEID");
	case NR_GRACE:
		return ("NR_GRACE");
	case NR_DELAY:
		return ("NR_DELAY");
	case NR_LOST_LOCK:
		return ("NR_LOST_LOCK");
	case NR_LOST_STATE_RQST:
		return ("NR_LOST_STATE_RQST");
	case NR_STALE:
		return ("NR_STALE");
	default:
		return ("Illegal_Action");
	}
}

static char *
op_to_str(uint_t op)
{
	switch (op) {
	case OP_ACCESS:
		return ("OP_ACCESS");
	case OP_CLOSE:
		return ("OP_CLOSE");
	case OP_COMMIT:
		return ("OP_COMMIT");
	case OP_CREATE:
		return ("OP_CREATE");
	case OP_DELEGPURGE:
		return ("OP_DELEGPURGE");
	case OP_DELEGRETURN:
		return ("OP_DELEGRETURN");
	case OP_GETATTR:
		return ("OP_GETATTR");
	case OP_GETFH:
		return ("OP_GETFH");
	case OP_LINK:
		return ("OP_LINK");
	case OP_LOCK:
		return ("OP_LOCK");
	case OP_LOCKT:
		return ("OP_LOCKT");
	case OP_LOCKU:
		return ("OP_LOCKU");
	case OP_LOOKUP:
		return ("OP_LOOKUP");
	case OP_LOOKUPP:
		return ("OP_LOOKUPP");
	case OP_NVERIFY:
		return ("OP_NVERIFY");
	case OP_OPEN:
		return ("OP_OPEN");
	case OP_OPENATTR:
		return ("OP_OPENATTR");
	case OP_OPEN_CONFIRM:
		return ("OP_OPEN_CONFIRM");
	case OP_OPEN_DOWNGRADE:
		return ("OP_OPEN_DOWNGRADE");
	case OP_PUTFH:
		return ("OP_PUTFH");
	case OP_PUTPUBFH:
		return ("OP_PUTPUBFH");
	case OP_PUTROOTFH:
		return ("OP_PUTROOTFH");
	case OP_READ:
		return ("OP_READ");
	case OP_READDIR:
		return ("OP_READDIR");
	case OP_READLINK:
		return ("OP_READLINK");
	case OP_REMOVE:
		return ("OP_REMOVE");
	case OP_RENAME:
		return ("OP_RENAME");
	case OP_RENEW:
		return ("OP_RENEW");
	case OP_RESTOREFH:
		return ("OP_RESTOREFH");
	case OP_SAVEFH:
		return ("OP_SAVEFH");
	case OP_SECINFO:
		return ("OP_SECINFO");
	case OP_SETATTR:
		return ("OP_SETATTR");
	case OP_SETCLIENTID:
		return ("OP_SETCLIENTID");
	case OP_SETCLIENTID_CONFIRM:
		return ("OP_SETCLIENTID_CONFIRM");
	case OP_VERIFY:
		return ("OP_VERIFY");
	case OP_WRITE:
		return ("OP_WRITE");
	case OP_RELEASE_LOCKOWNER:
		return ("OP_RELEASE_LOCKOWNER");
	case OP_ILLEGAL:
		return ("OP_ILLEGAL");
	default:
		return ("Illegal_Op");
	}
}

static char *
stat_to_str(nfsstat4 stat)
{
	switch (stat) {
	case NFS4_OK:
		return ("NFS4_OK");
	case NFS4ERR_PERM:
		return ("NFS4ERR_PERM");
	case NFS4ERR_NOENT:
		return ("NFS4ERR_NOENT");
	case NFS4ERR_IO:
		return ("NFS4ERR_IO");
	case NFS4ERR_NXIO:
		return ("NFS4ERR_NXIO");
	case NFS4ERR_ACCESS:
		return ("NFS4ERR_ACCESS");
	case NFS4ERR_EXIST:
		return ("NFS4ERR_EXIST");
	case NFS4ERR_XDEV:
		return ("NFS4ERR_XDEV");
	case NFS4ERR_NOTDIR:
		return ("NFS4ERR_NOTDIR");
	case NFS4ERR_ISDIR:
		return ("NFS4ERR_ISDIR");
	case NFS4ERR_INVAL:
		return ("NFS4ERR_INVAL");
	case NFS4ERR_FBIG:
		return ("NFS4ERR_FBIG");
	case NFS4ERR_NOSPC:
		return ("NFS4ERR_NOSPC");
	case NFS4ERR_ROFS:
		return ("NFS4ERR_ROFS");
	case NFS4ERR_MLINK:
		return ("NFS4ERR_MLINK");
	case NFS4ERR_NAMETOOLONG:
		return ("NFS4ERR_NAMETOOLONG");
	case NFS4ERR_NOTEMPTY:
		return ("NFSS4ERR_NOTEMPTY");
	case NFS4ERR_DQUOT:
		return ("NFS4ERR_DQUOT");
	case NFS4ERR_STALE:
		return ("NFS4ERR_STALE");
	case NFS4ERR_BADHANDLE:
		return ("NFS4ERR_BADHANDLE");
	case NFS4ERR_BAD_COOKIE:
		return ("NFS4ERR_BAD_COOKIE");
	case NFS4ERR_NOTSUPP:
		return ("NFS4ERR_NOTSUPP");
	case NFS4ERR_TOOSMALL:
		return ("NFS4ERR_TOOSMALL");
	case NFS4ERR_SERVERFAULT:
		return ("NFS4ERR_SERVERFAULT");
	case NFS4ERR_BADTYPE:
		return ("NFS4ERR_BADTYPE");
	case NFS4ERR_DELAY:
		return ("NFS4ERR_DELAY");
	case NFS4ERR_SAME:
		return ("NFS4ERR_SAME");
	case NFS4ERR_DENIED:
		return ("NFS4ERR_DENIED");
	case NFS4ERR_EXPIRED:
		return ("NFS4ERR_EXPIRED");
	case NFS4ERR_LOCKED:
		return ("NFS4ERR_LOCKED");
	case NFS4ERR_GRACE:
		return ("NFS4ERR_GRACE");
	case NFS4ERR_FHEXPIRED:
		return ("NFS4ERR_FHEXPIRED");
	case NFS4ERR_SHARE_DENIED:
		return ("NFS4ERR_SHARE_DENIED");
	case NFS4ERR_WRONGSEC:
		return ("NFS4ERR_WRONGSEC");
	case NFS4ERR_CLID_INUSE:
		return ("NFS4ERR_CLID_INUSE");
	case NFS4ERR_RESOURCE:
		return ("NFS4ERR_RESOURCE");
	case NFS4ERR_MOVED:
		return ("NFS4ERR_MOVED");
	case NFS4ERR_NOFILEHANDLE:
		return ("NFS4ERR_NOFILEHANDLE");
	case NFS4ERR_MINOR_VERS_MISMATCH:
		return ("NFS4ERR_MINOR_VERS_MISMATCH");
	case NFS4ERR_STALE_CLIENTID:
		return ("NFS4ERR_STALE_CLIENTID");
	case NFS4ERR_STALE_STATEID:
		return ("NFS4ERR_STALE_STATEID");
	case NFS4ERR_OLD_STATEID:
		return ("NFS4ERR_OLD_STATEID");
	case NFS4ERR_BAD_STATEID:
		return ("NFS4ERR_BAD_STATEID");
	case NFS4ERR_BAD_SEQID:
		return ("NFS4ERR_BAD_SEQID");
	case NFS4ERR_NOT_SAME:
		return ("NFS4ERR_NOT_SAME");
	case NFS4ERR_LOCK_RANGE:
		return ("NFS4ERR_LOCK_RANGE");
	case NFS4ERR_SYMLINK:
		return ("NFS4ERR_SYMLINK");
	case NFS4ERR_RESTOREFH:
		return ("NFS4ERR_RESTOREFH");
	case NFS4ERR_LEASE_MOVED:
		return ("NFS4ERR_LEASE_MOVED");
	case NFS4ERR_ATTRNOTSUPP:
		return ("NFS4ERR_ATTRNOTSUPP");
	case NFS4ERR_NO_GRACE:
		return ("NFS4ERR_NO_GRACE");
	case NFS4ERR_RECLAIM_BAD:
		return ("NFS4ERR_RECLAIM_BAD");
	case NFS4ERR_RECLAIM_CONFLICT:
		return ("NFS4ERR_RECLAIM_CONFLICT");
	case NFS4ERR_BADXDR:
		return ("NFS4ERR_BADXDR");
	case NFS4ERR_LOCKS_HELD:
		return ("NFS4ERR_LOCKS_HELD");
	case NFS4ERR_OPENMODE:
		return ("NFS4ERR_OPENMODE");
	case NFS4ERR_BADOWNER:
		return ("NFS4ERR_BADOWNER");
	case NFS4ERR_BADCHAR:
		return ("NFS4ERR_BADCHAR");
	case NFS4ERR_BADNAME:
		return ("NFS4ERR_BADNAME");
	case NFS4ERR_BAD_RANGE:
		return ("NFS4ERR_BAD_RANGE");
	case NFS4ERR_LOCK_NOTSUPP:
		return ("NFS4ERR_LOCK_NOTSUPP");
	case NFS4ERR_OP_ILLEGAL:
		return ("NFS4ERR_OP_ILLEGAL");
	case NFS4ERR_DEADLOCK:
		return ("NFS4ERR_DEADLOCK");
	case NFS4ERR_FILE_OPEN:
		return ("NFS4ERR_FILE_OPEN");
	case NFS4ERR_ADMIN_REVOKED:
		return ("NFS4ERR_ADMIN_REVOKED");
	case NFS4ERR_CB_PATH_DOWN:
		return ("NFS4ERR_CB_PATH_DOWN");
	default:
		return ("Illegal_Stat");
	}
}

void
nfs4_diag_help(void)
{
	mdb_printf("<vfs_t>::nfs4_diag <-s>\n"
	    "\t-> assumes client is Solaris NFSv4 client\n"
	    "\t-> -s is for summary mode\n\n");
}

/*
 * Print out a single event.
 */
int
nfs4_event_print(nfs4_debug_msg_t *msg)
{
	char		buf[1024];
	char		buf2[1024];
	char		buf3[1024];
	char		buf4[1024];
	nfs4_revent_t	*ep;

	ep = &msg->rmsg_u.msg_event;

	switch (ep->re_type) {
	case RE_BAD_SEQID:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_readstr(buf2, sizeof (buf2), (uintptr_t)ep->re_char1);
		mdb_readstr(buf3, sizeof (buf3), (uintptr_t)msg->msg_mntpt);
		mdb_printf("[NFS4]%Y: Operation %s for file %s (rnode_pt 0x%p),"
		    " pid %d using seqid %d got %s on server %s.  Last good "
		    "seqid was %d for operation %s.\n",
		    msg->msg_time.tv_sec, nfs4_diag_ctags[ep->re_tag1].ct_str,
		    buf2, (void *)ep->re_rp1, ep->re_pid, ep->re_seqid1,
		    stat_to_str(ep->re_stat4), buf, ep->re_seqid2,
		    nfs4_diag_ctags[ep->re_tag2].ct_str);
		break;
	case RE_BADHANDLE:
		ASSERT(ep->re_rp1 != NULL);
		if (ep->re_char1 != NULL) {
			mdb_readstr(buf, sizeof (buf),
			    (uintptr_t)msg->msg_srv);
			mdb_readstr(buf2, sizeof (buf2),
			    (uintptr_t)ep->re_char1);
			mdb_readstr(buf3, sizeof (buf3),
			    (uintptr_t)msg->msg_mntpt);
			mdb_printf("[NFS4]%Y: server %s said filehandle was "
			    "invalid for file: %s (rnode_pt %p) on mount %s\n",
			    msg->msg_time.tv_sec,
			    buf, buf2, (void *)ep->re_rp1, buf3);
		} else {
			mdb_readstr(buf, sizeof (buf),
			    (uintptr_t)msg->msg_srv);
			mdb_readstr(buf2, sizeof (buf2),
			    (uintptr_t)msg->msg_mntpt);
			mdb_printf("[NFS4]%Y: server %s said filehandle was "
			    "invalid for file: (rnode_pt %p) on mount %s\n",
			    msg->msg_time.tv_sec,
			    buf, (void *)ep->re_rp1, buf2);
		}
		break;
	case RE_CLIENTID:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_printf("[NFS4]%Y: Can't recover clientid on mi 0x%p due to "
		    "error %d (%s), for server %s.  Marking file system as "
		    "unusable.\n",
		    msg->msg_time.tv_sec, (void *)ep->re_mi, ep->re_uint,
		    stat_to_str(ep->re_stat4), buf);
		break;
	case RE_DEAD_FILE:
		mdb_readstr(buf, sizeof (buf),
		    (uintptr_t)msg->msg_srv);
		mdb_readstr(buf2, sizeof (buf2),
		    (uintptr_t)ep->re_char1);
		mdb_printf("[NFS4]%Y: File %s(rnode_pt %p) on server %s "
		    "could not be recovered and was closed.  ",
		    msg->msg_time.tv_sec, buf2,
		    (void *)ep->re_rp1, buf);
		if (ep->re_char2) {
			mdb_readstr(buf4, sizeof (buf4),
			    (uintptr_t)ep->re_char2);
			if (ep->re_stat4)
				mdb_printf("%s %s.", buf4,
				    stat_to_str(ep->re_stat4));
			else
				mdb_printf("%s.", buf4);
		}
		mdb_printf("\n");
		break;
	case RE_END:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_readstr(buf2, sizeof (buf2), (uintptr_t)msg->msg_mntpt);
		mdb_readstr(buf3, sizeof (buf3), (uintptr_t)ep->re_char1);
		mdb_readstr(buf4, sizeof (buf4), (uintptr_t)ep->re_char2);
		mdb_printf("[NFS4]%Y: Recovery done for mount %s (0x%p) on "
		    "server %s, rnode_pt1 %s (0x%p), rnode_pt2 %s (0x%p)\n",
		    msg->msg_time.tv_sec,
		    buf2, (void *)ep->re_mi, buf, ep->re_rp1 ? buf3 : NULL,
		    (void *)ep->re_rp1, ep->re_rp2 ? buf4 : NULL,
		    (void *)ep->re_rp2);
		break;
	case RE_FAILOVER:
		mdb_readstr(buf, sizeof (buf),
		    (uintptr_t)msg->msg_srv);
		if (ep->re_char1) {
			mdb_readstr(buf2, sizeof (buf2),
			    (uintptr_t)ep->re_char1);
			mdb_printf("[NFS4]%Y: Failing over from %s to %s\n",
			    msg->msg_time.tv_sec, buf, buf2);
		} else {
			mdb_printf("[NFS4]%Y: Failing over: selecting "
			    "original server %s\n", msg->msg_time.tv_sec,
			    buf);
		}
		break;
	case RE_FILE_DIFF:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_readstr(buf2, sizeof (buf2), (uintptr_t)ep->re_char1);
		mdb_readstr(buf3, sizeof (buf3), (uintptr_t)ep->re_char2);
		mdb_printf("[NFS4]%Y: Replicas %s and %s: file %s(%p) not "
		    "same\n", msg->msg_time.tv_sec, buf, buf2, buf3,
		    (void *)ep->re_rp1);
		break;
	case RE_FAIL_REMAP_LEN:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_printf("[NFS4]%Y: remap_lookup: server %s returned bad "
		    "fhandle length (%d)\n", msg->msg_time.tv_sec,
		    buf, ep->re_uint);
		break;
	case RE_FAIL_REMAP_OP:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_printf("[NFS4]%Y: remap_lookup: didn't get expected "
		    "OP_GETFH for server %s\n", msg->msg_time.tv_sec, buf);
		break;
	case RE_FAIL_RELOCK:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_readstr(buf2, sizeof (buf2), (uintptr_t)ep->re_char1);
		mdb_printf("[NFS4]%Y: Couldn't reclaim lock for pid %d for "
		    "file %s (rnode_pt %p) on (server %s): error %d\n",
		    msg->msg_time.tv_sec,
		    ep->re_pid, buf2, (void *)ep->re_rp1, buf,
		    ep->re_uint ? ep->re_uint : ep->re_stat4);
		break;
	case RE_LOST_STATE:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		if (ep->re_char1)
			mdb_readstr(buf2, sizeof (buf2),
			    (uintptr_t)ep->re_char1);
		if (ep->re_char2)
			mdb_readstr(buf3, sizeof (buf3),
			    (uintptr_t)ep->re_char2);
		mdb_readstr(buf4, sizeof (buf4), (uintptr_t)msg->msg_mntpt);
		mdb_printf("[NFS4]%Y: client has a lost %s request for "
		    "rnode_pt1 %s (0x%p), rnode_pt2 %s (0x%p) on"
		    " fs %s, server %s.\n",
		    msg->msg_time.tv_sec, op_to_str(ep->re_uint),
		    ep->re_rp1 ? buf2 : NULL,
		    (void *)ep->re_rp1, ep->re_rp2 ? buf3 : NULL,
		    (void *)ep->re_rp2, buf4, buf);
		break;
	case RE_OPENS_CHANGED:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_readstr(buf2, sizeof (buf2), (uintptr_t)msg->msg_mntpt);
		mdb_printf("[NFS4]%Y: Recovery: number of open files changed "
		    "for mount %s (0x%p) (old %d, new %d) on server %s\n",
		    msg->msg_time.tv_sec, buf2,
		    (void *)ep->re_mi, ep->re_uint, ep->re_pid, buf);
		break;
	case RE_SIGLOST:
	case RE_SIGLOST_NO_DUMP:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_readstr(buf2, sizeof (buf2), (uintptr_t)ep->re_char1);
		mdb_printf("[NFS4]%Y: Process %d lost its locks on "
		    "file %s (rnode_pt %p) due to a NFS error (%d) on"
		    " server %s\n",
		    msg->msg_time.tv_sec,
		    ep->re_pid, buf2, (void *)ep->re_rp1,
		    ep->re_uint ? ep->re_uint : ep->re_stat4, buf);
		break;
	case RE_START:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_readstr(buf2, sizeof (buf2), (uintptr_t)msg->msg_mntpt);
		mdb_readstr(buf3, sizeof (buf3), (uintptr_t)ep->re_char1);
		mdb_readstr(buf4, sizeof (buf4), (uintptr_t)ep->re_char2);
		mdb_printf("[NFS4]%Y: Starting recovery for mount %s (0x%p, "
		    "flags 0x%x) on server %s, rnode_pt1 %s (0x%p),"
		    " rnode_pt2 %s (0x%p)\n",
		    msg->msg_time.tv_sec,
		    buf2, (void *)ep->re_mi, ep->re_uint, buf,
		    ep->re_rp1 ? buf3 : NULL, (void *)ep->re_rp1,
		    ep->re_rp2 ? buf4 : NULL, (void *)ep->re_rp2);
		break;
	case RE_UNEXPECTED_ACTION:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_printf("[NFS4]%Y: Recovery: unexpected action (%d) on "
		    "server %s\n", msg->msg_time.tv_sec,
		    ep->re_uint, buf);
		break;
	case RE_UNEXPECTED_ERRNO:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_printf("[NFS4]%Y: Recovery: unexpected errno (%d) on"
		    "server %s\n", msg->msg_time.tv_sec,
		    ep->re_uint, buf);
		break;
	case RE_UNEXPECTED_STATUS:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_printf("[NFS4]%Y: Recovery: unexpected NFS status code "
		    "(%s) on server %s\n", msg->msg_time.tv_sec,
		    stat_to_str(ep->re_stat4), buf);
		break;
	case RE_WRONGSEC:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_readstr(buf2, sizeof (buf2), (uintptr_t)ep->re_char1);
		mdb_readstr(buf3, sizeof (buf3), (uintptr_t)ep->re_char2);
		mdb_printf("[NFS4]%Y: Can't recover from NFS4ERR_WRONGSEC."
		    "  error %d for server %s: rnode_pt1 %s (0x%p),"
		    " rnode_pt2 %s (0x%p)\n",
		    msg->msg_time.tv_sec,
		    ep->re_uint, buf, ep->re_rp1 ? buf2 : NULL,
		    (void *)ep->re_rp1, ep->re_rp2 ? buf3 : NULL,
		    (void *)ep->re_rp2);
		break;
	case RE_LOST_STATE_BAD_OP:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		if (ep->re_char1 != NULL)
			mdb_readstr(buf2, sizeof (buf2),
			    (uintptr_t)ep->re_char1);
		if (ep->re_char2 != NULL)
			mdb_readstr(buf3, sizeof (buf3),
			    (uintptr_t)ep->re_char2);
		mdb_readstr(buf4, sizeof (buf4), (uintptr_t)msg->msg_mntpt);
		mdb_printf("[NFS4]%Y: Bad op (%d) in lost state record."
		    "  fs %s, server %s, pid %d, file %s (0x%p), "
		    "dir %s (0x%p)\n", msg->msg_time.tv_sec,
		    ep->re_uint, buf4, buf, ep->re_pid,
		    ep->re_rp1 ? buf2 : NULL, (void *)ep->re_rp1,
		    ep->re_rp2 ? buf3 : NULL, (void *)ep->re_rp2);
		break;
	default:
		mdb_warn("Illegal event type %d\n", ep->re_type);
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

/*
 * Print out a single fact.
 */
int
nfs4_fact_print(nfs4_debug_msg_t *msg)
{
	char		buf[1024];
	char		mnt_pt[1024];
	nfs4_rfact_t	*fp;

	fp = &msg->rmsg_u.msg_fact;

	switch (fp->rf_type) {
	case RF_BADOWNER:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_readstr(mnt_pt, sizeof (mnt_pt), (uintptr_t)msg->msg_mntpt);
		mdb_printf("[NFS4]%Y: Op %s at mount point: %s got %s error\n",
		    msg->msg_time.tv_sec, op_to_str(fp->rf_op), mnt_pt,
		    stat_to_str(fp->rf_stat4));
		mdb_printf("[NFS4]%Y: NFSMAPID_DOMAIN does not match server: "
		    "%s's domain.\n", msg->msg_time.tv_sec, buf);
		break;
	case RF_ERR:
		if (fp->rf_error)
			mdb_printf("[NFS4]%Y: Op %s got error %d causing "
			    "recovery action %s.%s\n", msg->msg_time.tv_sec,
			    op_to_str(fp->rf_op), fp->rf_error,
			    action_to_str(fp->rf_action), fp->rf_reboot ?
			    "  Client also suspects server rebooted" : "");
		else
			mdb_printf("[NFS4]%Y: Op %s got error %s causing "
			    "recovery action %s.%s\n", msg->msg_time.tv_sec,
			    op_to_str(fp->rf_op), stat_to_str(fp->rf_stat4),
			    action_to_str(fp->rf_action), fp->rf_reboot ?
			    "  Client also suspects server rebooted" : "");
		break;
	case RF_RENEW_EXPIRED:
		break;
	case RF_SRV_NOT_RESPOND:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_printf("[NFS4]%Y: Server %s not responding, still "
		    "trying\n", msg->msg_time.tv_sec, buf);
		break;
	case RF_SRV_OK:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_printf("[NFS4]%Y: Server %s ok\n",
		    msg->msg_time.tv_sec, buf);
		break;
	case RF_SRVS_NOT_RESPOND:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_printf("[NFS4]%Y: Servers %s not responding, still "
		    "trying\n", msg->msg_time.tv_sec, buf);
		break;
	case RF_SRVS_OK:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)msg->msg_srv);
		mdb_printf("[NFS4]%Y: Servers %s ok\n",
		    msg->msg_time.tv_sec, buf);
		break;
	case RF_DELMAP_CB_ERR:
		mdb_readstr(buf, sizeof (buf), (uintptr_t)fp->rf_char1);
		mdb_printf("[NFS4]%Y: Op %s got error %s when executing "
		    "delmap on file %s (rnode_pt 0x%x).\n",
		    msg->msg_time.tv_sec, op_to_str(fp->rf_op),
		    stat_to_str(fp->rf_stat4), buf,
		    (void *)fp->rf_rp1);
		break;
	default:
		mdb_warn("Illegal fact type %d\n", fp->rf_type);
		return (DCMD_ERR);
		/* NOTREACHED */
		break;
	}
	return (DCMD_OK);
}

/*
 * Print out a single message.
 */
int
nfs4_msg_print(nfs4_debug_msg_t *msg, uint_t opt_s)
{

	nfs4_revent_t	*ep;
	nfs4_rfact_t	*fp;

	/* check if summary mode */
	if (msg->msg_type == RM_EVENT) {
		/* check if summary mode */
		if (opt_s) {
			ep = &msg->rmsg_u.msg_event;
			mdb_printf("%Y: event %s\n",
			    msg->msg_time.tv_sec,
			    event_to_str(ep->re_type));
			return (DCMD_OK);
		}
		return (nfs4_event_print(msg));
	} else if (msg->msg_type == RM_FACT) {
		/* check if summary mode */
		if (opt_s) {
			fp = &msg->rmsg_u.msg_fact;
			mdb_printf("%Y: fact %s\n",
			    msg->msg_time.tv_sec,
			    fact_to_str(fp->rf_type));
			return (DCMD_OK);
		}
		return (nfs4_fact_print(msg));
	}

	return (DCMD_ERR);
}

/* ARGSUSED */
int
nfs4_diag_walk(uintptr_t addr, void *buf, int *opt_s)
{
	nfs4_debug_msg_t	msg;
	uint_t			do_summary;

	if (addr == NULL)
		return (WALK_DONE);

	if (mdb_vread(&msg, sizeof (nfs4_debug_msg_t), addr) == -1) {
		mdb_warn("Failed to read nfs4_debug_msg forw at %p\n", addr);
		return (WALK_ERR);
	}

	do_summary = opt_s != NULL ? *opt_s : 0;

	if (nfs4_msg_print(&msg, do_summary) != DCMD_OK)
		return (WALK_ERR);

	return (WALK_NEXT);
}

#ifdef _LP64
#define	MNTPT_LEN	25
#else
#define	MNTPT_LEN	41
#endif
#define	RESOURCE_LEN	100

/*
 * This interface expects the address of the
 * mountinfo message head list.
 */
int
nfs4_mimsg(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	uint_t			opt_s = FALSE;

	if (mdb_getopts(argc, argv, 's', MDB_OPT_SETBITS, TRUE,
	    &opt_s, NULL) != argc)
		return (DCMD_USAGE);

	/*
	 * If no address specified report error
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		mdb_warn("no mi_msg_list address specified\n");
		return (DCMD_USAGE);
	}

	if (mdb_pwalk("list", (mdb_walk_cb_t)nfs4_diag_walk, &opt_s,
	    addr) == -1) {
		mdb_warn("Failed to walk mi_msg_list list\n");
		return (DCMD_ERR);
	}

	return (DCMD_OK);
}

int
nfs4_diag(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	mntinfo4_t	mnt4;
	struct vfs	vfs;
	uintptr_t	msglist_ptr;
	uintptr_t	mntinfo_ptr;
	int		opts = 0;

	/*
	 * If no address specified walk the vfs table looking
	 * for NFS vfs entries and call this dcmd.
	 */
	if (!(flags & DCMD_ADDRSPEC)) {
		int status;
		mdb_arg_t *argvv;

		argvv = mdb_alloc((argc)*sizeof (mdb_arg_t), UM_SLEEP);
		bcopy(argv, argvv, argc*sizeof (mdb_arg_t));
		status = mdb_walk_dcmd("nfs4_mnt", "nfs4_diag",
		    argc, argvv);
		mdb_free(argvv, (argc)*sizeof (mdb_arg_t));
		return (status);
	}

	if (mdb_getopts(argc, argv,
	    's', MDB_OPT_SETBITS, NFS_MDB_OPT_SUMMARY, &opts,
	    NULL) != argc)
		return (DCMD_USAGE);

	opts |= nfs4_mdb_opt;

	/*
	 * Get the mount info
	 */
	NFS_OBJ_FETCH(addr, mntinfo4_t, &mnt4, DCMD_ERR);
	NFS_OBJ_FETCH((uintptr_t)mnt4.mi_vfsp, vfs_t, &vfs, DCMD_ERR);

	mntinfo_ptr = (uintptr_t)addr;

	mdb_printf("\n\n*********************************************\n");
	mdb_printf("vfs: %p	mi: %p\n", mnt4.mi_vfsp, mntinfo_ptr);

	pr_vfs_mntpnts(&vfs);

	mdb_printf("Messages queued:\n");
	mdb_printf("=============================================\n");

	msglist_ptr = nfs4_get_mimsg(mntinfo_ptr);

	if (mdb_pwalk("list", (mdb_walk_cb_t)nfs4_diag_walk, &opts,
	    msglist_ptr) == -1) {
		mdb_warn("Failed to walk mi_msg_list list\n");
		return (DCMD_ERR);
	}
	return (DCMD_OK);
}
