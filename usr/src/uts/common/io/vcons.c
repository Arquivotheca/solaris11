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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/termios.h>
#include <sys/termio.h>
#include <sys/ttold.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/tty.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/procset.h>
#include <sys/fault.h>
#include <sys/siginfo.h>
#include <sys/debug.h>
#include <sys/kd.h>
#include <sys/vt.h>
#include <sys/vtdaemon.h>
#include <sys/session.h>
#include <sys/door.h>
#include <sys/kmem.h>
#include <sys/cpuvar.h>
#include <sys/kbio.h>
#include <sys/strredir.h>
#include <sys/fs/snode.h>
#include <sys/consdev.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/console.h>
#include <sys/promif.h>
#include <sys/note.h>
#include <sys/polled_io.h>
#include <sys/systm.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/esunddi.h>
#include <sys/sunldi.h>
#include <sys/debug.h>
#include <sys/console.h>
#include <sys/ddi_impldefs.h>
#include <sys/policy.h>
#include <sys/tem.h>
#include <sys/wscons.h>
#include <sys/systm.h>
#include <sys/modctl.h>
#include <sys/vt_impl.h>
#include <sys/consconfig_dacf.h>

/*
 * This file belongs to wc STREAMS module which has a D_MTPERMODE
 * inner perimeter. See "Locking Policy" comment in wscons.c for
 * more information.
 */

/*
 * Minor	name		device file		Hotkeys
 *
 * 0	the system console	/dev/console		Alt + F1
 * 0:	virtual console #1	/dev/vt/0		Alt + F1
 *
 * 2:   virtual console #2	/dev/vt/2		Alt + F2
 * 3:	virtual console #3	/dev/vt/3		Alt + F3
 * ......
 * n:	virtual console #n	/dev/vt/n		Alt + Fn
 *
 * Note that vtdaemon is running on /dev/vt/1 (minor=1),
 * which is not available to end users.
 *
 */

#define	VT_DAEMON_MINOR	1
#define	VT_IS_DAEMON(minor)	((minor) == VT_DAEMON_MINOR)

extern void	wc_get_size(vc_state_t *pvc);
extern boolean_t consconfig_console_is_tipline(void);


minor_t vc_last_console = VT_MINOR_INVALID;	/* the last used console */
volatile uint_t	vc_target_console;		/* arg (1..n) */

static volatile minor_t vc_inuse_max_minor = 0;
static list_t vc_waitactive_list;

_NOTE(MUTEX_PROTECTS_DATA(vc_lock, vc_last_console))
_NOTE(MUTEX_PROTECTS_DATA(vc_lock, vc_target_console))
_NOTE(MUTEX_PROTECTS_DATA(vc_lock, vc_inuse_max_minor))
_NOTE(MUTEX_PROTECTS_DATA(vc_lock, vc_active_console vc_cons_user vc_avl_root))
_NOTE(MUTEX_PROTECTS_DATA(vc_lock, vc_waitactive_list))
/*
 * wait active message variables are referenced in vt_ioctl() and
 * vc_waitactive_reply().
 * 1. In vt_ioctl(), the dynamically allocated data block is
 *    invisible to other threads befor it is inserted into
 *    the list and there is no need to lock the data.
 * 2. In vc_waitactive_reply(), the data block is indexed
 *    by a stream queue(a driver minor) and is protected by
 *    "vc_lock".
 */
_NOTE(SCHEME_PROTECTS_DATA("vc_lock", vc_waitactive_msg_t))

static int vt_pending_vtno = -1;
kmutex_t vt_pending_vtno_lock;
_NOTE(MUTEX_PROTECTS_DATA(vt_pending_vtno_lock, vt_pending_vtno))
_NOTE(MUTEX_PROTECTS_DATA(vc_state_t::vc_state_lock, vc_state_t::vc_hold_cnt))
_NOTE(MUTEX_PROTECTS_DATA(vc_state_t::vc_state_lock, vc_state_t::vc_flags))

static int vt_activate(minor_t vc_minor, uint_t vt_no, cred_t *credp);
static void vt_copyout(queue_t *qp, mblk_t *mp, mblk_t *tmp, uint_t size);
static void vt_copyin(queue_t *qp, mblk_t *mp, uint_t size);
static void vt_iocnak(queue_t *qp, mblk_t *mp, int error);
static void vt_iocack(queue_t *qp, mblk_t *mp);

static uint_t vt_minor2arg(minor_t minor);
static minor_t vt_arg2minor(uint_t arg);

/*
 * If the system console is directed to tipline, consider /dev/vt/0 as
 * not being used.
 * For other VT, if it is opened and tty is initialized, consider it
 * as being used.
 */
#define	VT_IS_INUSE(id)						\
	(((vt_minor2vc_locked(id, B_FALSE))->vc_flags & WCS_ISOPEN) &&	\
	((vt_minor2vc_locked(id, B_FALSE))->vc_flags & WCS_INIT) &&	\
	(id != 0 || !consconfig_console_is_tipline()))

/*
 * the vt switching message is encoded as:
 *
 *   -------------------------------------------------------------
 *   |  \033  |  'Q'  |  vtno + 'A'  |  opcode  |  'z'  |  '\0'  |
 *   -------------------------------------------------------------
 */
#define	VT_MSG_SWITCH(mp)					\
	((int)((mp)->b_wptr - (mp)->b_rptr) >= 5 &&		\
	*((mp)->b_rptr) == '\033' &&				\
	*((mp)->b_rptr + 1) == 'Q' &&				\
	*((mp)->b_rptr + 4) == 'z')

#define	VT_MSG_VTNO(mp)		(*((mp)->b_rptr + 2) - 'A')
#define	VT_MSG_OPCODE(mp)	(*((mp)->b_rptr + 3))

#define	VT_DOORCALL_MAX_RETRY	3

static void
vt_init_ttycommon(tty_common_t *pcommon)
{
	struct termios *termiosp;
	int len;

	mutex_init(&pcommon->t_excl, NULL, MUTEX_DEFAULT, NULL);
	pcommon->t_iflag = 0;

	/*
	 * Get the default termios settings (cflag).
	 * These are stored as a property in the
	 * "options" node.
	 */
	if (ddi_getlongprop(DDI_DEV_T_ANY,
	    ddi_root_node(), 0, "ttymodes",
	    (caddr_t)&termiosp, &len) == DDI_PROP_SUCCESS) {

		if (len == sizeof (struct termios))
			pcommon->t_cflag = termiosp->c_cflag;
		else
			cmn_err(CE_WARN,
			    "wc: Couldn't get ttymodes property!");

		kmem_free(termiosp, len);
	} else {
		/*
		 * Gack!  Whine about it.
		 */
		cmn_err(CE_WARN,
		    "wc: Couldn't get ttymodes property!");
	}

	pcommon->t_iocpending = NULL;
}

static int
vt_config(uint_t count)
{
	if (consmode != CONS_KFB)
		return (ENOTSUP);

	/* one for system console, one for vtdaemon */
	if (count < 2)
		return (ENXIO);

	mutex_enter(&vc_lock);
	/*
	 * Shouldn't allow to shrink the max vt minor to be smaller than
	 * the max in used minor.
	 */
	if (count <= vc_inuse_max_minor) {
		mutex_exit(&vc_lock);
		return (EBUSY);
	}

	vt_resize(count);
	mutex_exit(&vc_lock);

	return (0);
}

void
vt_clean(queue_t *q, vc_state_t *pvc)
{
	ASSERT(MUTEX_HELD(&pvc->vc_state_lock));

	if (pvc->vc_bufcallid != 0) {
		qunbufcall(q, pvc->vc_bufcallid);
		pvc->vc_bufcallid = 0;
	}
	if (pvc->vc_timeoutid != 0) {
		(void) quntimeout(q, pvc->vc_timeoutid);
		pvc->vc_timeoutid = 0;
	}
	ttycommon_close(&pvc->vc_ttycommon);

	pvc->vc_flags &= ~WCS_INIT;
}

static void
vc_waitactive_reply_one(vc_waitactive_msg_t *index, boolean_t close)
{
	vc_state_t *pvc = index->wa_msg_vc;

	/*
	 * drop all the lock bfore calling vt_iocnack()
	 * or vt_ioccak()
	 */
	mutex_exit(&vc_lock);

	ASSERT(pvc->vc_flags & WCS_ISOPEN);
	if (close)
		vt_iocnak(pvc->vc_wq,
		    index->wa_mp, ENXIO);
	else
		vt_iocack(pvc->vc_wq, index->wa_mp);
	mutex_enter(&pvc->vc_state_lock);
	ASSERT(pvc->vc_hold_cnt);
	pvc->vc_hold_cnt--;
	if (!pvc->vc_hold_cnt)
		cv_broadcast(&pvc->vc_state_cv);
	mutex_exit(&pvc->vc_state_lock);
	mutex_enter(&vc_lock);
	kmem_free(index, sizeof (*index));
}


/*
 * Reply the VT_WAITACTIVE ioctl.
 * Argument 'close' usage:
 * B_TRUE: the function is called vt_close()
 *
 * B_FALSE: the function is from vt_switch(), not from vt_close()
 * Argument 'vc_minor'usage: the minor number for the current stream
 *          from which msg is from
 * Argument 'minor' usage: the minor number for the targeting stream
 */
static void
vc_waitactive_reply(minor_t vc_minor, int minor, boolean_t close)
{
	vc_waitactive_msg_t *index;

	/*
	 * "vc_lock" protects vt_minor2vc_locked()&vc_waitactive_list below
	 */
	ASSERT(MUTEX_HELD(&vc_lock));
	ASSERT(!close || (vc_minor == minor));

	index = list_head(&vc_waitactive_list);


	/*
	 * Use a simple order to protect two threads waiting
	 * on two different queues trying to signal each other.
	 * The simple order is:
	 * 1). First try to find the wait active message entry waiting
	 *	on its own queue.
	 * 2). Second try to find the wait active message entry waiting on
	 *	othe queues.
	 * Suppose there is a thread T1 on stream Q1 trying to signal
	 * thread T2 on stream Q2. From the simple order,
	 * we know that all the threads on stream Q1 have
	 * been signaled before Q1 try to signal thread T2 on
	 * stream Q2. Also, beccause this module is single
	 * thread  per stream, there is no chance for a thread to insert
	 * the wait entry in the process of wait active messages for one
	 * specific stream. So T2 doesn't get a chance to signal any
	 * thread on Q1.
	 */

	/* first try */
	while (index != NULL) {
		if ((index->wa_msg_vc->vc_minor != vc_minor) ||
		    (index->wa_wait_minor != minor)) {
			index = list_next(&vc_waitactive_list, index);
			continue;
		}

		list_remove(&vc_waitactive_list, index);
		vc_waitactive_reply_one(index, close);

		/*
		 * reset the index after the vc_lock was released
		 * a while right before vt_iocnak/ack() in
		 * vc_waitactive_reply_one().
		 */
		index = list_head(&vc_waitactive_list);

	}

	index = list_head(&vc_waitactive_list);
	/* second try */
	while (index != NULL) {
		if (index->wa_wait_minor != minor) {
			index = list_next(&vc_waitactive_list, index);
			continue;
		}

		list_remove(&vc_waitactive_list, index);
		vc_waitactive_reply_one(index, close);
		/*
		 * reset the index after the vc_lock was released
		 * a while right before vt_iocnak/ack() in
		 * vc_waitactive_reply_one().
		 */
		index = list_head(&vc_waitactive_list);
	}

}

void
vt_close(queue_t *q, vc_state_t *pvc, cred_t *credp)
{
	minor_t index;

	mutex_enter(&pvc->vc_state_lock);

	while (pvc->vc_hold_cnt) {
		cv_wait(&pvc->vc_state_cv, &pvc->vc_state_lock);
	}

	vt_clean(q, pvc);
	mutex_exit(&pvc->vc_state_lock);

	tem_destroy(pvc->vc_tem, credp);
	pvc->vc_tem = NULL;

	index = pvc->vc_minor;
	mutex_enter(&vc_lock);
	if (index == vc_inuse_max_minor) {
		while ((--index > 0) && !VT_IS_INUSE(index))
			;
		vc_inuse_max_minor = index;
	}

	vc_waitactive_reply(pvc->vc_minor, pvc->vc_minor, B_TRUE);

	mutex_enter(&pvc->vc_state_lock);
	pvc->vc_flags &= ~WCS_ISOPEN;
	mutex_exit(&pvc->vc_state_lock);

	mutex_exit(&vc_lock);

	/*
	 * All the threads are opening this stream and
	 * waiting for other VT  to be active should exit after
	 * vc_waitactive_reply().
	 */
}

static void
vt_init_tty(vc_state_t *pvc)
{
	ASSERT(MUTEX_HELD(&pvc->vc_state_lock));

	pvc->vc_flags |= WCS_INIT;
	vt_init_ttycommon(&pvc->vc_ttycommon);
	wc_get_size(pvc);
}

/*
 * minor 0:	/dev/vt/0	(index = 0, indicating the system console)
 * minor 1:	/dev/vt/1	(index = 1, vtdaemon special console)
 * minor 2:	/dev/vt/2	(index = 2, virtual consoles)
 * ......
 * minor n:	/dev/vt/n	(index = n)
 *
 *
 * The system console (minor 0), is opened firstly and used during console
 * configuration.  It also acts as the system hard console even when all
 * virtual consoles go off.
 *
 * In tipline case, minor 0 (/dev/vt/0) is reserved, and cannot be switched to.
 * And the system console is redirected to the tipline. During normal cases,
 * we can switch from virtual consoles to it by pressing 'Alt + F1'.
 *
 * minor 1 (/dev/vt/1) is reserved for vtdaemon special console, and it's
 * not available to end users.
 *
 * During early console configuration, consconfig_dacf opens wscons and then
 * issue a WC_OPEN_FB ioctl to kick off terminal init process. So during
 * consconfig_dacf first opening of wscons, tems (of type tem_state_t) is
 * not initialized. We do not initialize the tem_vt_state_t instance returned
 * by tem_init() for this open, since we do not have enough info to handle
 * normal terminal operation at this moment. This tem_vt_state_t instance
 * will get initialized when handling WC_OPEN_FB.
 */
int
vt_open(minor_t minor, queue_t *rq, cred_t *crp)
{
	vc_state_t *pvc;

	if (!vt_minor_valid(minor))
		return (ENXIO);

	mutex_enter(&vc_lock);
	pvc = vt_minor2vc_locked(minor, B_FALSE);
	if (pvc == NULL) {
		mutex_exit(&vc_lock);
		return (ENXIO);
	}

	mutex_enter(&pvc->vc_state_lock);

	if (!(pvc->vc_flags & WCS_ISOPEN)) {
		/*
		 * vc_tem might not be intialized if !tems.ts_initialized,
		 * and this only happens during console configuration.
		 */
		pvc->vc_tem = tem_init(crp);
	}

	if (!(pvc->vc_flags & WCS_INIT))
		vt_init_tty(pvc);

	/*
	 * In normal case, the first screen is the system console;
	 * In tipline case, the first screen is the first VT that gets started.
	 */
	if (vc_active_console == VT_MINOR_INVALID && minor != VT_DAEMON_MINOR)
		if (minor == 0 || consmode == CONS_KFB) {
			boolean_t unblank = B_FALSE;

			vc_active_console = minor;
			vc_last_console = minor;
			if (minor != 0) {
				/*
				 * If we are not opening the system console
				 * as the first console, clear the phyical
				 * screen.
				 */
				unblank = B_TRUE;
			}

			tem_activate(pvc->vc_tem, unblank, crp);
		}

	if ((pvc->vc_ttycommon.t_flags & TS_XCLUDE) &&
	    (secpolicy_excl_open(crp) != 0)) {
		mutex_exit(&pvc->vc_state_lock);
		mutex_exit(&vc_lock);
		return (EBUSY);
	}

	if (minor > vc_inuse_max_minor)
		vc_inuse_max_minor = minor;

	pvc->vc_flags |= WCS_ISOPEN;
	pvc->vc_ttycommon.t_readq = rq;
	pvc->vc_ttycommon.t_writeq = WR(rq);

	mutex_exit(&pvc->vc_state_lock);
	mutex_exit(&vc_lock);

	rq->q_ptr = pvc;
	WR(rq)->q_ptr = pvc;
	pvc->vc_wq = WR(rq);

	qprocson(rq);
	return (0);
}

static minor_t
vt_find_prev(minor_t cur)
{
	minor_t i, t, max;

	ASSERT(MUTEX_HELD(&vc_lock));
	ASSERT(vc_active_console != VT_MINOR_INVALID);

	max = VC_INSTANCES_COUNT;

	for (i = cur - 1; (t = (i + max) % max) != cur; i--)
		if (!VT_IS_DAEMON(t) && VT_IS_INUSE(t))
			return (t);

	return (VT_MINOR_INVALID);
}

static minor_t
vt_find_next(minor_t cur)
{
	minor_t i, t, max;

	ASSERT(MUTEX_HELD(&vc_lock));
	ASSERT(vc_active_console != VT_MINOR_INVALID);

	max = VC_INSTANCES_COUNT;

	for (i = cur + 1; (t = (i + max) % max) != cur; i++)
		if (!VT_IS_DAEMON(t) && VT_IS_INUSE(t))
			return (t);

	return (VT_MINOR_INVALID);
}

/* ARGSUSED */
void
vt_send_hotkeys(void *timeout_arg)
{
	door_handle_t door;
	vt_cmd_arg_t arg;
	int error = 0;
	int retries = 0;
	door_arg_t door_arg;

	arg.vt_ev = VT_EV_HOTKEYS;

	mutex_enter(&vt_pending_vtno_lock);
	arg.vt_num = vt_pending_vtno;
	mutex_exit(&vt_pending_vtno_lock);

	/* only available in kernel context or user context */
	if (door_ki_open(VT_DAEMON_DOOR_FILE, &door) != 0) {
		mutex_enter(&vt_pending_vtno_lock);
		vt_pending_vtno = -1;
		mutex_exit(&vt_pending_vtno_lock);
		return;
	}

	door_arg.rbuf = NULL;
	door_arg.rsize = 0;
	door_arg.data_ptr = (void *)&arg;
	door_arg.data_size = sizeof (arg);
	door_arg.desc_ptr = NULL;
	door_arg.desc_num = 0;

	/*
	 * Make door upcall
	 */
	while ((error = door_ki_upcall(door, &door_arg)) != 0 &&
	    retries < VT_DOORCALL_MAX_RETRY)
		if (error == EAGAIN || error == EINTR)
			retries++;
		else
			break;

	door_ki_rele(door);

	mutex_enter(&vt_pending_vtno_lock);
	vt_pending_vtno = -1;
	mutex_exit(&vt_pending_vtno_lock);
}

static boolean_t
vt_validate_hotkeys(int minor)
{
	/*
	 * minor should not succeed the existing minor numbers range.
	 */
	if (!vt_minor_valid(minor))
		return (B_FALSE);

	mutex_enter(&vc_lock);
	/*
	 * Shouldn't switch to /dev/vt/1 or an unused vt.
	 */
	if (!VT_IS_DAEMON(minor) && VT_IS_INUSE(minor)) {
		mutex_exit(&vc_lock);
		return (B_TRUE);
	}
	mutex_exit(&vc_lock);

	return (B_FALSE);
}

static void
vt_trigger_hotkeys(int vtno)
{
	mutex_enter(&vt_pending_vtno_lock);

	if (vt_pending_vtno != -1) {
		mutex_exit(&vt_pending_vtno_lock);
		return;
	}

	vt_pending_vtno = vtno;
	mutex_exit(&vt_pending_vtno_lock);
	(void) timeout(vt_send_hotkeys, NULL, 1);
}

/*
 * return value:
 *    0:    non msg of vt hotkeys
 *    1:    msg of vt hotkeys
 */
int
vt_check_hotkeys(mblk_t *mp)
{
	int vtno = 0;
	minor_t minor = 0;

	/* LINTED E_PTRDIFF_OVERFLOW */
	if (!VT_MSG_SWITCH(mp))
		return (0);

	switch (VT_MSG_OPCODE(mp)) {
	case 'B':
		mutex_enter(&vc_lock);
		/* find out the previous vt */
		if (vc_active_console == VT_MINOR_INVALID) {
			mutex_exit(&vc_lock);
			return (1);
		}

		if (VT_IS_DAEMON(vc_active_console)) {
			minor = vt_find_prev(vt_arg2minor(vc_target_console));
			mutex_exit(&vc_lock);
			break;
		}

		minor = vt_find_prev(vc_active_console);
		mutex_exit(&vc_lock);
		break;
	case 'F':
		mutex_enter(&vc_lock);
		/* find out the next vt */
		if (vc_active_console == VT_MINOR_INVALID) {
			mutex_exit(&vc_lock);
			return (1);
		}

		if (VT_IS_DAEMON(vc_active_console)) {
			minor = vt_find_next(vt_arg2minor(vc_target_console));
			mutex_exit(&vc_lock);
			break;
		}

		minor = vt_find_next(vc_active_console);
		mutex_exit(&vc_lock);
		break;
	case 'H':
		/* find out the specified vt */
		minor = VT_MSG_VTNO(mp);

		/* check for system console, Alt + F1 */
		if (minor == 1)
			minor = 0;
		break;
	case 'L':
		mutex_enter(&vc_lock);
		/* find out the last vt */
		if ((minor = vc_last_console) == VT_MINOR_INVALID) {
			mutex_exit(&vc_lock);
			return (1);
		}
		mutex_exit(&vc_lock);
		break;
	default:
		return (1);
	}

	if (!vt_validate_hotkeys(minor))
		return (1);

	/*
	 * for system console, the argument of vtno for
	 * vt_activate is 1, though its minor is 0
	 */
	if (minor == 0)
		vtno = 1;	/* for system console */
	else
		vtno = minor;

	vt_trigger_hotkeys(vtno);
	return (1);
}

static void
vt_proc_sendsig(pid_t pid, int sig)
{
	register proc_t *p;

	if (pid <= 0)
		return;

	mutex_enter(&pidlock);
	if ((p = prfind(pid)) == NULL || p->p_stat == SIDL) {
		mutex_exit(&pidlock);
		return;
	}

	psignal(p, sig);
	mutex_exit(&pidlock);
}

static int
vt_proc_exists(pid_t pid)
{
	register proc_t *p;

	if (pid <= 0)
		return (EINVAL);

	mutex_enter(&pidlock);
	if ((p = prfind(pid)) == NULL || p->p_stat == SIDL) {
		mutex_exit(&pidlock);
		return (ESRCH);
	}
	mutex_exit(&pidlock);

	return (0);
}

#define	SIG_VALID(x)	(((x) > 0) && ((x) <= MAXSIG) && \
			((x) != SIGKILL) && ((x) != SIGSTOP))

static int
vt_setmode(vc_state_t *pvc, struct vt_mode *pmode)
{
	if ((pmode->mode != VT_PROCESS) && (pmode->mode != VT_AUTO))
		return (EINVAL);

	if (!SIG_VALID(pmode->relsig) || !SIG_VALID(pmode->acqsig))
		return (EINVAL);

	if (pmode->mode == VT_PROCESS) {
		pvc->vc_pid = curproc->p_pid;
	} else {
		pvc->vc_dispnum = 0;
		pvc->vc_login = 0;
	}

	pvc->vc_switch_mode = pmode->mode;
	pvc->vc_waitv = pmode->waitv;
	pvc->vc_relsig = pmode->relsig;
	pvc->vc_acqsig = pmode->acqsig;

	return (0);
}

static void
vt_reset(vc_state_t *pvc)
{
	pvc->vc_switch_mode = VT_AUTO;
	pvc->vc_pid = -1;
	pvc->vc_dispnum = 0;
	pvc->vc_login = 0;
	pvc->vc_switchto = VT_MINOR_INVALID;
}

/*
 * vc_minor is the minor number of current stream
 * switch to vt_no from vc_active_console
 */
static int
vt_switch(minor_t vc_minor, uint_t vt_no, cred_t *credp)
{
	vc_state_t *pvc_active;
	vc_state_t *pvc;
	minor_t index;

	ASSERT(MUTEX_HELD(&vc_lock));
	pvc_active = vt_minor2vc_locked(vc_active_console, B_FALSE);
	pvc = vt_minor2vc_locked(vt_no, B_FALSE);
	ASSERT(pvc_active && pvc);

	/* sanity test for the target VT and the active VT */
	if (!((pvc->vc_flags & WCS_ISOPEN) &&
	    (pvc->vc_flags & WCS_INIT))) {
		return (ENXIO);
	}

	if (!(pvc_active->vc_flags & WCS_ISOPEN)) {
		return (ENXIO);
	}

	tem_switch(pvc_active->vc_tem, pvc->vc_tem, credp);

	if (!VT_IS_DAEMON(vc_active_console))
		vc_last_console = vc_active_console;
	else
		vc_last_console = vt_arg2minor(vc_target_console);

	vc_active_console = pvc->vc_minor;

	if (pvc->vc_switch_mode == VT_PROCESS) {
		pvc->vc_switchto = pvc->vc_minor;

		/* send it an acquired signal */
		vt_proc_sendsig(pvc->vc_pid, pvc->vc_acqsig);
	}

	/*
	 * vc_lock protects vc_active_console as a parameter
	 */
	vc_waitactive_reply(vc_minor, vc_active_console, B_FALSE);


	if (!VT_IS_DAEMON(vt_no)) {
		/*
		 * Applications that open the virtual console device may request
		 * asynchronous notification of VT switching from a previous VT
		 * to another one by setting the S_MSG flag in an I_SETSIG
		 * STREAMS ioctl. Such processes receive a SIGPOLL signal when
		 * a VT switching succeeds.
		 */
		for (index = 0; index < VC_INSTANCES_COUNT; index++) {
			vc_state_t *tmp_pvc;
			mblk_t *mp;

			tmp_pvc = vt_minor2vc_locked(index, B_FALSE);

			mutex_enter(&tmp_pvc->vc_state_lock);

			if ((tmp_pvc->vc_flags & WCS_ISOPEN) &&
			    (tmp_pvc->vc_flags & WCS_INIT) &&
			    (mp = allocb(sizeof (unsigned char), BPRI_HI))) {
				mp->b_datap->db_type = M_PCSIG;
				*mp->b_wptr = SIGPOLL;
				mp->b_wptr += sizeof (unsigned char);
				/*
				 * Hold the write queue before it is accessed
				 * in putnext(). vt_close() will block
				 * until the write queue is released.
				 */

				tmp_pvc->vc_hold_cnt++;
				/*
				 * drop all the lock bfore calling putnext()
				 */
				mutex_exit(&tmp_pvc->vc_state_lock);
				mutex_exit(&vc_lock);
				putnext(RD(tmp_pvc->vc_wq), mp);
				mutex_enter(&vc_lock);
				mutex_enter(&tmp_pvc->vc_state_lock);
				tmp_pvc->vc_hold_cnt--;
				if (!tmp_pvc->vc_hold_cnt)
					cv_broadcast(&tmp_pvc->vc_state_cv);

			}
			mutex_exit(&tmp_pvc->vc_state_lock);
		}
	}

	return (0);

}

/*
 * vt_no	from 0 to n
 *
 * 0	for the vtdaemon sepcial console (only vtdaemon will use it)
 * 1    for the system console (Alt + F1, or Alt + Ctrl + F1),
 *      aka Virtual Console #1
 *
 * 2    for Virtual Console #2
 * n    for Virtual Console #n
 */
static minor_t
vt_arg2minor(uint_t arg)
{
	if (arg == 0)
		return (1);

	if (arg == 1)
		return (0);

	return (arg);
}

static uint_t
vt_minor2arg(minor_t minor)
{
	ASSERT(MUTEX_HELD(&vc_lock));

	if (minor == 0)
		return (1);

	if (VT_IS_DAEMON(minor)) {
		/* here it should be the real console */
		return (vc_target_console);
	}

	return (minor);
}

static int
vt_activate(minor_t vc_minor, uint_t vt_no, cred_t *credp)
{
	vc_state_t *pvc;
	minor_t minor;
	int ret;

	minor = vt_arg2minor(vt_no);
	if (!vt_minor_valid(minor))
		return (ENXIO);

	mutex_enter(&vc_lock);
	if (minor == vc_active_console) {
		if (VT_IS_DAEMON(minor)) {
			/*
			 * vtdaemon is reactivating itself to do locking
			 * on behalf of another console, so record current
			 * target console as the last console.
			 */
			vc_last_console = vt_arg2minor(vc_target_console);
		}

		mutex_exit(&vc_lock);
		return (0);
	}

	/*
	 * In tipline case, the system console is redirected to tipline
	 * and thus is always available.
	 */
	if (minor == 0 && consconfig_console_is_tipline()) {
		mutex_exit(&vc_lock);
		return (0);
	}

	if (!VT_IS_INUSE(minor)) {
		mutex_exit(&vc_lock);
		return (ENXIO);
	}

	pvc = vt_minor2vc_locked(minor, B_FALSE);

	if (pvc == NULL) {
		mutex_exit(&vc_lock);
		return (ENXIO);
	}
	if (pvc->vc_tem == NULL) {
		mutex_exit(&vc_lock);
		return (ENXIO);
	}

	pvc = vt_minor2vc_locked(vc_active_console, B_FALSE);
	if (pvc == NULL) {
		mutex_exit(&vc_lock);
		return (ENXIO);
	}
	if (pvc->vc_switch_mode != VT_PROCESS) {
		ret = vt_switch(vc_minor, minor, credp);
		mutex_exit(&vc_lock);
		return (ret);
	}

	mutex_exit(&vc_lock);
	/*
	 * Validate the process, reset the
	 * vt to auto mode if failed.
	 */
	if (pvc->vc_pid == -1 || vt_proc_exists(pvc->vc_pid) != 0) {
		/*
		 * Xserver has not started up yet,
		 * or it dose not exist.
		 */
		vt_reset(pvc);
		return (0);
	}

	/*
	 * Send the release signal to the process,
	 * and wait VT_RELDISP ioctl from Xserver
	 * after its leaving VT.
	 */
	vt_proc_sendsig(pvc->vc_pid, pvc->vc_relsig);
	pvc->vc_switchto = minor;

	/*
	 * We don't need a timeout here, for if Xserver refuses
	 * or fails to respond to release signal using VT_RELDISP,
	 * we cannot successfully switch to our text mode. Actually
	 * users can try again. At present we don't support force
	 * switch.
	 */
	return (0);
}

static int
vt_reldisp(vc_state_t *pvc, int arg, cred_t *credp)
{
	minor_t target_vtno = pvc->vc_switchto;
	int ret;

	mutex_enter(&vc_lock);
	if ((pvc->vc_switch_mode != VT_PROCESS) ||
	    (pvc->vc_minor != vc_active_console)) {
		mutex_exit(&vc_lock);
		return (EACCES);
	}

	if (target_vtno == VT_MINOR_INVALID) {
		mutex_exit(&vc_lock);
		return (EINVAL);
	}

	pvc->vc_switchto = VT_MINOR_INVALID;

	if (arg == VT_ACKACQ) {
		mutex_exit(&vc_lock);
		return (0);
	}

	if (arg == 0) {
		mutex_exit(&vc_lock);
		return (0); /* refuse to release */
	}

	/* Xserver has left VT */
	ret = vt_switch(pvc->vc_minor, target_vtno, credp);
	mutex_exit(&vc_lock);

	return (ret);
}

void
vt_ioctl(queue_t *q, mblk_t *mp)
{
	vc_state_t *pvc = (vc_state_t *)q->q_ptr;
	struct iocblk	*iocp;
	struct vt_mode vtmode;
	struct vt_stat vtinfo;
	struct vt_dispinfo vtdisp;
	mblk_t *tmp;
	int minor;
	int arg;
	int error = 0;
	vc_waitactive_msg_t *wait_msg;

	iocp = (struct iocblk *)(void *)mp->b_rptr;
	if (consmode != CONS_KFB && iocp->ioc_cmd != VT_ENABLED) {
		vt_iocnak(q, mp, EINVAL);
		return;
	}

	switch (iocp->ioc_cmd) {
	case VT_ENABLED:
		if (!(tmp = allocb(sizeof (int), BPRI_MED))) {
			error = ENOMEM;
			break;
		}
		*(int *)(void *)tmp->b_rptr = consmode;
		tmp->b_wptr += sizeof (int);
		vt_copyout(q, mp, tmp, sizeof (int));
		return;

	case KDSETMODE:
		arg = *(intptr_t *)(void *)mp->b_cont->b_rptr;
		if (arg != KD_TEXT && arg != KD_GRAPHICS &&
		    arg != KD_RESETTEXT) {
			error = EINVAL;
			break;
		}
		if (tem_get_fbmode(pvc->vc_tem) == arg)
			break;

		tem_set_fbmode(pvc->vc_tem, (uchar_t)arg, iocp->ioc_cr);

		break;

	case KDGETMODE:
		if (!(tmp = allocb(sizeof (int), BPRI_MED))) {
			error = ENOMEM;
			break;
		}
		*(int *)(void *)tmp->b_rptr = tem_get_fbmode(pvc->vc_tem);
		tmp->b_wptr += sizeof (int);
		vt_copyout(q, mp, tmp, sizeof (int));
		return;

	case VT_OPENQRY: /* return number of first free VT */
		if (!(tmp = allocb(sizeof (int), BPRI_MED))) {
			error = ENOMEM;
			break;
		}

		/* minors of 0 and 1 are not available to end users */
		for (minor = 2; vt_minor_valid(minor); minor++) {
			mutex_enter(&vc_lock);
			if (!VT_IS_INUSE(minor)) {
				mutex_exit(&vc_lock);
				break;
			}
			mutex_exit(&vc_lock);
		}

		if (!vt_minor_valid(minor))
			minor = -1;
		*(int *)(void *)tmp->b_rptr = minor; /* /dev/vt/minor */
		tmp->b_wptr += sizeof (int);
		vt_copyout(q, mp, tmp, sizeof (int));
		return;

	case VT_GETMODE:
		vtmode.mode = pvc->vc_switch_mode;
		vtmode.waitv = pvc->vc_waitv;
		vtmode.relsig = pvc->vc_relsig;
		vtmode.acqsig = pvc->vc_acqsig;
		vtmode.frsig = 0;
		if (!(tmp = allocb(sizeof (struct vt_mode), BPRI_MED))) {
			error = ENOMEM;
			break;
		}
		*(struct vt_mode *)(void *)tmp->b_rptr = vtmode;
		tmp->b_wptr += sizeof (struct vt_mode);
		vt_copyout(q, mp, tmp, sizeof (struct vt_mode));
		return;

	case VT_SETMODE:
		vt_copyin(q, mp, sizeof (struct vt_mode));
		return;

	case VT_SETDISPINFO:
		/* always enforce sys_devices privilege for setdispinfo */
		if ((error = secpolicy_console(iocp->ioc_cr)) != 0)
			break;

		pvc->vc_dispnum = *(intptr_t *)(void *)mp->b_cont->b_rptr;
		break;

	case VT_SETDISPLOGIN:
		pvc->vc_login = *(intptr_t *)(void *)mp->b_cont->b_rptr;
		break;

	case VT_GETDISPINFO:
		vtdisp.v_pid = pvc->vc_pid;
		vtdisp.v_dispnum = pvc->vc_dispnum;
		vtdisp.v_login = pvc->vc_login;
		if (!(tmp = allocb(sizeof (struct vt_dispinfo), BPRI_MED))) {
			error = ENOMEM;
			break;
		}
		*(struct vt_dispinfo *)(void *)tmp->b_rptr = vtdisp;
		tmp->b_wptr += sizeof (struct vt_dispinfo);
		vt_copyout(q, mp, tmp, sizeof (struct vt_dispinfo));
		return;

	case VT_RELDISP:
		arg = *(intptr_t *)(void *)mp->b_cont->b_rptr;
		error = vt_reldisp(pvc, arg, iocp->ioc_cr);
		break;

	case VT_CONFIG:
		/* always enforce sys_devices privilege for config */
		if ((error = secpolicy_console(iocp->ioc_cr)) != 0)
			break;

		arg = *(intptr_t *)(void *)mp->b_cont->b_rptr;
		error = vt_config(arg);
		break;

	case VT_ACTIVATE:
		/* always enforce sys_devices privilege for secure switch */
		if ((error = secpolicy_console(iocp->ioc_cr)) != 0)
			break;

		arg = *(intptr_t *)(void *)mp->b_cont->b_rptr;
		error = vt_activate(pvc->vc_minor, arg, iocp->ioc_cr);
		break;

	case VT_WAITACTIVE:
		arg = *(intptr_t *)(void *)mp->b_cont->b_rptr;
		arg = vt_arg2minor(arg);
		if (!vt_minor_valid(arg)) {
			error = ENXIO;
			break;
		}

		wait_msg = kmem_zalloc(sizeof (vc_waitactive_msg_t),
		    KM_NOSLEEP);
		if (wait_msg == NULL) {
			error = ENXIO;
			break;
		}
		/*
		 * The dynamically allocated data block is
		 * invisible to other threads befor it is
		 * inserted into the list and there is no
		 * need to lock the data block.
		 */
		wait_msg->wa_mp = mp;
		wait_msg->wa_msg_vc = pvc;
		wait_msg->wa_wait_minor = arg;

		mutex_enter(&vc_lock);
		if (arg == vc_active_console) {
			mutex_exit(&vc_lock);
			kmem_free(wait_msg, sizeof (vc_waitactive_msg_t));
			break;
		}
		/*
		 * This stream is held until the thread waiting on
		 * it is woke up. The close can make the write
		 * queue("vc_wq") invalid before vt_iocnak()/vt_iocack()
		 * is called. So we should hold the write queue("wc_wq)
		 * before we drop "vc_lock".  vt_close() will block
		 * until the write queue is released.
		 * See the code in vc_waitactive_reply() and
		 * vt_close().
		 */
		mutex_enter(&pvc->vc_state_lock);
		pvc->vc_hold_cnt++;
		mutex_exit(&pvc->vc_state_lock);

		list_insert_head(&vc_waitactive_list, wait_msg);
		mutex_exit(&vc_lock);

		return;

	case VT_GETSTATE:
		vtinfo.v_state = 3;	/* system console and vtdaemon */
		/*
		 * Here v_active is the argument for vt_activate,
		 * not minor.
		 */
		mutex_enter(&vc_lock);
		vtinfo.v_active = vt_minor2arg(vc_active_console);
		/*
		 * we only support 16 vt states since the
		 * v_state is short
		 */
		for (minor = 2; minor < 16; minor++) {
			pvc = vt_minor2vc_locked(minor, B_FALSE);
			if (pvc == NULL)
				break;
			if (VT_IS_INUSE(minor))
				vtinfo.v_state |= (1 << pvc->vc_minor);
		}
		mutex_exit(&vc_lock);

		if (!(tmp = allocb(sizeof (struct vt_stat), BPRI_MED))) {
			error = ENOMEM;
			break;
		}
		*(struct vt_stat *)(void *)tmp->b_rptr = vtinfo;
		tmp->b_wptr += sizeof (struct vt_stat);
		vt_copyout(q, mp, tmp, sizeof (struct vt_stat));
		return;

	case VT_SET_TARGET:
		/* always enforce sys_devices privilege */
		if ((error = secpolicy_console(iocp->ioc_cr)) != 0)
			break;

		arg = *(intptr_t *)(void *)mp->b_cont->b_rptr;

		mutex_enter(&vc_lock);
		/* vtdaemon is doing authentication for this target console */
		vc_target_console = arg;
		mutex_exit(&vc_lock);
		break;

	case VT_GETACTIVE:	/* get real active console (minor) */
		if (!(tmp = allocb(sizeof (int), BPRI_MED))) {
			error = ENOMEM;
			break;
		}
		mutex_enter(&vc_lock);
		*(int *)(void *)tmp->b_rptr = vc_active_console;
		mutex_exit(&vc_lock);
		tmp->b_wptr += sizeof (int);
		vt_copyout(q, mp, tmp, sizeof (int));
		return;

	case VT_GET_CONSUSER:
		if (!(tmp = allocb(sizeof (int), BPRI_MED))) {
			error = ENOMEM;
			break;
		}

		mutex_enter(&vc_lock);
		if (vc_cons_user == VT_MINOR_INVALID) {
			/*
			 * Return -1 if console user link points to
			 * /dev/console
			 */
			*(int *)(void *)tmp->b_rptr = -1;
		} else {
			*(int *)(void *)tmp->b_rptr = vc_cons_user;
		}
		mutex_exit(&vc_lock);

		tmp->b_wptr += sizeof (int);
		vt_copyout(q, mp, tmp, sizeof (int));
		return;

	case VT_RESET_CONSUSER:
		/* always enforce sys_devices privilege */
		if ((error = secpolicy_console(iocp->ioc_cr)) != 0)
			break;

		/* Ensure it comes from /dev/console */
		if (pvc->vc_minor != 0) {
			error = ENXIO;
			break;
		}

		mutex_enter(&vc_lock);
		vc_cons_user = VT_MINOR_INVALID;
		mutex_exit(&vc_lock);
		break;

	case VT_SET_CONSUSER:
		/* always enforce sys_devices privilege */
		if ((error = secpolicy_console(iocp->ioc_cr)) != 0)
			break;

		mutex_enter(&vc_lock);
		vc_cons_user = pvc->vc_minor;
		mutex_exit(&vc_lock);
		break;

	default:
		error = ENXIO;
		break;
	}

	if (error != 0)
		vt_iocnak(q, mp, error);
	else
		vt_iocack(q, mp);
}

void
vt_miocdata(queue_t *qp, mblk_t *mp)
{
	vc_state_t *pvc = (vc_state_t *)qp->q_ptr;
	struct copyresp *copyresp;
	struct vt_mode *pmode;
	int error = 0;

	copyresp = (struct copyresp *)(void *)mp->b_rptr;
	if (copyresp->cp_rval) {
		vt_iocnak(qp, mp, EAGAIN);
		return;
	}

	switch (copyresp->cp_cmd) {
	case VT_SETMODE:
		pmode = (struct vt_mode *)(void *)mp->b_cont->b_rptr;
		error = vt_setmode(pvc, pmode);
		break;

	case KDGETMODE:
	case VT_OPENQRY:
	case VT_GETMODE:
	case VT_GETDISPINFO:
	case VT_GETSTATE:
	case VT_ENABLED:
	case VT_GETACTIVE:
		break;

	default:
		error = ENXIO;
		break;
	}

	if (error != 0)
		vt_iocnak(qp, mp, error);
	else
		vt_iocack(qp, mp);
}

static void
vt_iocack(queue_t *qp, mblk_t *mp)
{
	struct iocblk	*iocbp = (struct iocblk *)(void *)mp->b_rptr;

	mp->b_datap->db_type = M_IOCACK;
	mp->b_wptr = mp->b_rptr + sizeof (struct iocblk);
	iocbp->ioc_error = 0;
	iocbp->ioc_count = 0;
	iocbp->ioc_rval = 0;
	if (mp->b_cont != NULL) {
		freemsg(mp->b_cont);
		mp->b_cont = NULL;
	}
	qreply(qp, mp);
}

static void
vt_iocnak(queue_t *qp, mblk_t *mp, int error)
{
	struct iocblk *iocp = (struct iocblk *)(void *)mp->b_rptr;

	mp->b_datap->db_type = M_IOCNAK;
	iocp->ioc_rval = 0;
	iocp->ioc_count = 0;
	iocp->ioc_error = error;
	if (mp->b_cont != NULL) {
		freemsg(mp->b_cont);
		mp->b_cont = NULL;
	}
	qreply(qp, mp);
}

static void
vt_copyin(queue_t *qp, mblk_t *mp, uint_t size)
{
	struct copyreq  *cqp;

	cqp = (struct copyreq *)(void *)mp->b_rptr;
	cqp->cq_addr = *((caddr_t *)(void *)mp->b_cont->b_rptr);
	cqp->cq_size = size;
	cqp->cq_flag = 0;
	cqp->cq_private = (mblk_t *)NULL;
	mp->b_wptr = mp->b_rptr + sizeof (struct copyreq);
	mp->b_datap->db_type = M_COPYIN;
	if (mp->b_cont)
		freemsg(mp->b_cont);
	mp->b_cont = (mblk_t *)NULL;
	qreply(qp, mp);
}

static void
vt_copyout(queue_t *qp, mblk_t *mp, mblk_t *tmp, uint_t size)
{
	struct copyreq  *cqp;

	cqp = (struct copyreq *)(void *)mp->b_rptr;
	cqp->cq_size = size;
	cqp->cq_addr = *((caddr_t *)(void *)mp->b_cont->b_rptr);
	cqp->cq_flag = 0;
	cqp->cq_private = (mblk_t *)NULL;
	mp->b_wptr = mp->b_rptr + sizeof (struct copyreq);
	mp->b_datap->db_type = M_COPYOUT;
	if (mp->b_cont)
		freemsg(mp->b_cont);
	mp->b_cont = tmp;
	qreply(qp, mp);
}

/*
 * Get vc state from minor, locked version.
 * Once a caller gets a vc_state_t from this function,
 * the vc_state_t is guaranteed not being freed before
 * the caller leaves this STREAMS module by the D_MTPERQ
 * perimeter. if polledio is true, no need to get the lock.
 */
vc_state_t *
vt_minor2vc_locked(minor_t minor, boolean_t polledio)
{
	avl_index_t where;
	vc_state_t target;

	_NOTE(ARGUNUSED(polledio));
	ASSERT(polledio || MUTEX_HELD(&vc_lock));

	if (minor != VT_ACTIVE) {
		target.vc_minor = minor;
		return (avl_find(&vc_avl_root, &target, &where));
	}


	if (vc_active_console == VT_MINOR_INVALID)
		target.vc_minor = 0;
	else
		target.vc_minor = vc_active_console;

	return (avl_find(&vc_avl_root, &target, &where));
}
/*
 * Get vc state from minor, non-locked version.
 * Once a caller gets a vc_state_t from this function.
 * The stream  linked to the vc_state_t must be opened.
 */
vc_state_t *
vt_minor2vc(minor_t minor)
{
	vc_state_t *pvc;

	mutex_enter(&vc_lock);
	/*
	 * The only one which can free the vc_state_t is vt_resize().
	 * Fortunately, the vt_resize() can not free the minor node opened.
	 * And vt_minor2vc() should be called while the minor node is opened.
	 */
	pvc =  vt_minor2vc_locked(minor, B_FALSE);
	if (pvc != NULL) {
		ASSERT(pvc->vc_flags & WCS_ISOPEN);
	}
	mutex_exit(&vc_lock);

	return (pvc);
}

static void
vt_state_init(vc_state_t *vcptr, minor_t minor)
{
	cv_init(&vcptr->vc_state_cv, NULL, CV_DRIVER, NULL);
	mutex_init(&vcptr->vc_state_lock, NULL, MUTEX_DRIVER, NULL);

	mutex_enter(&vcptr->vc_state_lock);
	vcptr->vc_flags = 0;
	vcptr->vc_hold_cnt = 0;
	mutex_exit(&vcptr->vc_state_lock);

	vcptr->vc_pid = -1;
	vcptr->vc_dispnum = 0;
	vcptr->vc_login = 0;
	vcptr->vc_switchto = VT_MINOR_INVALID;
	vcptr->vc_switch_mode = VT_AUTO;
	vcptr->vc_relsig = SIGUSR1;
	vcptr->vc_acqsig = SIGUSR1;
	vcptr->vc_tem = NULL;
	vcptr->vc_bufcallid = 0;
	vcptr->vc_timeoutid = 0;
	vcptr->vc_wq = NULL;
	vcptr->vc_minor = minor;
}
static void
vc_state_destory(vc_state_t *vcptr)
{
	cv_destroy(&vcptr->vc_state_cv);
	kmem_free(vcptr, sizeof (vc_state_t));
}

void
vt_resize(uint_t count)
{
	uint_t vc_num, i;

	ASSERT(MUTEX_HELD(&vc_lock));

	vc_num = VC_INSTANCES_COUNT;

	if (count == vc_num)
		return;

	if (count > vc_num) {
		for (i = vc_num; i < count; i++) {
			vc_state_t *vcptr = kmem_zalloc(sizeof (vc_state_t),
			    KM_SLEEP);
			vt_state_init(vcptr, i);
			avl_add(&vc_avl_root, vcptr);
		}
		return;
	}

	for (i = vc_num; i > count; i--) {
		avl_index_t where;
		vc_state_t target, *found;

		target.vc_minor = i - 1;
		found = avl_find(&vc_avl_root, &target, &where);
		ASSERT(found != NULL && found->vc_flags == 0);
		avl_remove(&vc_avl_root, found);
		vc_state_destory(found);
	}
}

static int
vc_avl_compare(const void *first, const void *second)
{
	const vc_state_t *vcptr1 = first;
	const vc_state_t *vcptr2 = second;

	if (vcptr1->vc_minor < vcptr2->vc_minor)
		return (-1);

	if (vcptr1->vc_minor == vcptr2->vc_minor)
		return (0);

	return (1);
}

/*
 * Only called from wc init().
 */
void
vt_init(void)
{
#ifdef	__lock_lint
	ASSERT(NO_COMPETING_THREADS);
#endif

	avl_create(&vc_avl_root, vc_avl_compare, sizeof (vc_state_t),
	    offsetof(vc_state_t, vc_avl_node));

	list_create(&vc_waitactive_list, sizeof (vc_waitactive_msg_t),
	    offsetof(vc_waitactive_msg_t, wa_list_node));

	mutex_init(&vc_lock, NULL, MUTEX_DRIVER, NULL);
	mutex_init(&vt_pending_vtno_lock, NULL, MUTEX_DRIVER, NULL);
}
