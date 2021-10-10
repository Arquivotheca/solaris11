/*
 * Copyright 1991 NCR Corporation - Dayton, Ohio, USA
 *
 * Copyright (c) 1994, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/* NCR OS2.00.00 1.1 */

/*
 * This is the interface routines for the client side of the Lock
 * Manager. See the LM protocol specification for a description of
 * this interface.
 */
#include <sys/types.h>
#include <sys/buf.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/utsname.h>
#include <sys/pathname.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/flock.h>
#include <sys/debug.h>
#include <sys/zone.h>
#include <sys/cmn_err.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <nfs/nfs_clnt.h>
#include <nfs/lm.h>
#include <nfs/lm_nlm.h>
#include <nfs/lm_impl.h>
#include <fs/fs_subr.h>

/*
 * map NLM return status value into an errno value
 */
static int
lm_stat_to_errno[] = {
	0,	/* nlm_granted */
	EAGAIN,	/* nlm_denied */
	ENOLCK,	/* nlm_denied_nolocks */
	EAGAIN,	/* nlm_blocked */
	EAGAIN,	/* nlm_denied_grace_period */
	EDEADLK	/* nlm_deadlck */
};

/* Forward declarations of static functions. */
static void register_share_locally(vnode_t *vp, struct lm_sysid *ls,
    int cmd, struct shrlock *shr, int flag);

/*
 * lm_find_sysid
 *
 * Find the sysid for the knetconfig associated with the given mi and
 * if needed update it with the current info.
 */
static struct lm_sysid *
lm_find_sysid(lm_globals_t *lm, mntinfo_t *mi)
{
	struct knetconfig miconfig;
	bool_t nchange;
	struct lm_sysid *ls = NULL;

	rw_enter(&lm->lm_sysids_lock, RW_READER);

	mutex_enter(&mi->mi_lock);
	lm_copy_config(&miconfig,
	    mi->mi_klmconfig ? mi->mi_klmconfig : ORIG_KNCONF(mi));
	mutex_exit(&mi->mi_lock);

	ls = lm_get_sysid_locked(lm, &miconfig, &mi->mi_curr_serv->sv_addr,
	    mi->mi_curr_serv->sv_hostname, TRUE, NULL, &nchange);
	lm_free_config(&miconfig);
	if (nchange == TRUE) {
		mutex_enter(&ls->lock);
		lm_dup_config(&ls->config, mi);
		mutex_exit(&ls->lock);
	}
	rw_exit(&lm->lm_sysids_lock);
	return (ls);
}

/*
 * lm_frlock
 *
 * This function calls the server and the local locking code.
 *
 * !!!NOTICE!!!
 * There are two versions of the locking routines: one for NFSv2 (lm_frlock)
 * and the other for NFSv3 (lm4_frlock).  Changes made to one routine MUST
 * be made to the corresponding code in the other routine.
 *
 * The local locking code is called to maintain the reclocks in the
 * client kernel. This serves two purposes:
 * - The reclocks can be used as a reclock cache by F_SETLK and F_GETLK.
 * - We need the reclocks to perform server crash recovery. Therefore the
 *   reclocks in the client kernel must only contain the locks granted by
 *   the server.  Also, they must be registered using the server's sysid,
 *   not as local locks.
 *
 * The algorithm is:
 *
 * if cmd == F_*GETLK then
 *   if local locking says that another process got the lock then
 *	return this info
 *   else
 *	call server and return result
 *   fi
 * fi
 *
 * if cmd == F_SETLK*  &&  l_type == F_UNLCK then
 *   call server
 *   call local locking
 * fi
 *
 * if cmd == F_SETLK  &&  l_type != F_UNLCK then
 *   call local locking with cmd=F_GETLK
 *   if local locking says that another process got the lock then
 *	return EAGAIN
 *   else
 *	call server
 *	call local locking with cmd=F_SETLK  --- Note, this will always succeed
 *   fi
 * fi
 *
 * if cmd == F_SETLKW  &&  l_type != F_UNLCK then
 *   call server
 *   call local locking -- Note, this will always succeed
 * fi
 *
 * XXX NCR porting issues:
 *	1. If a signal is posted to the calling thread during the
 *		CLNT_CALL(), and flk->l_whence == 2, the VOP_GETATTR() in
 *		convoff() may fail and flk may not be restored correctly.
 *
 */
int
lm_frlock(struct vnode *vp, int cmd, struct flock64 *flk, int flag,
    u_offset_t offset, struct cred *cr, netobj *fh,
    struct flk_callback *flk_cbp)
{
	union {
		nlm_testargs    nta;
		nlm_lockargs    nla;
		nlm_unlockargs  nua;
		nlm_cancargs    nca;
	} arg;

	union {
		nlm_testres	ntr;
		nlm_res		nr;
	} res;

	nlm_lock	alk;
	int		cookie;
	int		proc, origproc;
	xdrproc_t	xdr_arg;
	xdrproc_t	xdr_res;
	struct lm_sysid *ls = NULL;
	short		whence;
	int		error = 0;
	int		blocked_rexmit = 0; /* retrans. of blocked request */
	int		signalled;
	struct lm_sleep *lslp = NULL;
	char		oh_bytes[LM_OH_LEN];
	uint_t		off;
	uint_t		len;
	int		on_list =  0;
	callb_cpr_t 	*cprp;
	klwp_t		*lwp;
	lm_globals_t	*lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	LM_DEBUG((1, "frlck",
	    "cmd= %d, offset= %lld, type= %d, start= %" PRId64
	    ", len= %" PRId64 ", whence= %d",
	    cmd, offset, flk->l_type, flk->l_start,
	    flk->l_len, flk->l_whence));

	/* Check ranges to ensure we don't overflow NLM3 limit */
	if ((flk->l_start > MAXOFF32_T) || (flk->l_end > MAXOFF32_T) ||
	    (flk->l_len > MAXOFF32_T) ||
	    (flk->l_start+flk->l_len-1 > MAXOFF32_T))
		return (EINVAL);

	mutex_enter(&lm_stat.lock);
	cookie = lm_stat.cookie++;
	mutex_exit(&lm_stat.lock);

#ifdef DEBUG
	if (lm_gc_sysids)
		lm_free_sysid_table(lm);
#endif

	/*
	 * Convert the offset. It must be restored before returning.
	 * (And not before.) If whence == 2, we need the newest
	 * length of the file. Therefore invalidate the ATTR_CACHE.
	 */
	whence = flk->l_whence;

	if (whence == 2) {
		PURGE_ATTRCACHE(vp);
	}
	if (error = convoff(vp, flk, 0, offset)) {
		LM_DEBUG((2, "frlck", "convoff  =>  error= %d\n", error));
		return (error);
	}

	if ((lwp = ttolwp(curthread)) != NULL)
		lwp->lwp_nostop++;

	/* Reset arg and res. */
	bzero(&arg, sizeof (arg));
	bzero(&res, sizeof (res));

	/*
	 * Initialize alock. The oh field is set to the current time,
	 * followed by the value of the cookie.  For blocking lock
	 * requests, this is so that it can be used to match up a GRANTED
	 * call with a blocked lock reliably (even across reboots).  For
	 * other requests, the oh still needs to be set to something, and
	 * this combination is pretty cheap to use.
	 */
	alk.caller_name = uts_nodename();
	alk.fh = *fh;
	/* oh_bytes holds the process id and the system name */
	lm_set_oh(lm, oh_bytes, sizeof (oh_bytes), curproc->p_pid);
	alk.oh.n_len = sizeof (oh_bytes);
	alk.oh.n_bytes = &oh_bytes[0];
	alk.svid = curproc->p_pid;
	alk.l_offset = (uint_t)flk->l_start;
	alk.l_len = (uint_t)flk->l_len;

	/* Initialize call-specific parameters. */
	switch (cmd) {
	case F_GETLK:
	case F_O_GETLK:
		proc = NLM_TEST;
		xdr_arg = xdr_nlm_testargs;
		xdr_res = xdr_nlm_testres;
		arg.nta.cookie.n_len = sizeof (cookie);
		arg.nta.cookie.n_bytes = (char *)&cookie;
		arg.nta.exclusive = flk->l_type == F_WRLCK;
		arg.nta.alock = alk;
		break;

	case F_SETLK:
	case F_SETLKW:
		if (flk->l_type == F_UNLCK) {
			proc = NLM_UNLOCK;
			xdr_arg = xdr_nlm_unlockargs;
			xdr_res = xdr_nlm_res;
			arg.nua.cookie.n_len = sizeof (cookie);
			arg.nua.cookie.n_bytes = (char *)&cookie;
			arg.nua.alock = alk;
		} else {
			proc = NLM_LOCK;
			xdr_arg = xdr_nlm_lockargs;
			xdr_res = xdr_nlm_res;
			arg.nla.cookie.n_len = sizeof (cookie);
			arg.nla.cookie.n_bytes = (char *)&cookie;
			arg.nla.block = cmd == F_SETLKW;
			arg.nla.exclusive = flk->l_type == F_WRLCK;
			arg.nla.alock = alk;
			arg.nla.reclaim = FALSE;
			arg.nla.state = 1;
		}
		break;

	default:
		LM_DEBUG((2, "frlck", "cmd= %d  =>  EINVAL\n", cmd));
		error = EINVAL;
		goto out;
	}

	origproc = proc;
	/*
	 * If we are setting a lock, check that the file is opened
	 * with the correct mode.
	 */
	if (proc == NLM_LOCK) {
		if ((flk->l_type == F_RDLCK && (flag & FREAD) == 0) ||
		    (flk->l_type == F_WRLCK && (flag & FWRITE) == 0)) {
			error = EBADF;
			goto out;
		}
	}

	/* Find the lm_sysid */
	ls = lm_find_sysid(lm, VTOMI(vp));
	if (ls == NULL) {
		error = ENOLCK;
		goto out;
	}

	/*
	 * Before calling server, use the local reclocks as a cache
	 * for NLM_TEST and non-blocking NLM_LOCK calls. Note, there
	 * is no point doing this for blocking NLM_LOCK calls, since
	 * we always want to sleep.
	 */
	if ((proc == NLM_TEST) || (proc == NLM_LOCK && !arg.nla.block)) {
		struct flock64    f = *flk;

		LM_DEBUG((2, "frlck", "Checking reclock cache"));

		f.l_pid = ttoproc(curthread)->p_pid;
		f.l_sysid = ls->sysid | LM_SYSID_CLIENT;
		/*
		 * Simulate an F_GETLK call.  Note that the result is
		 * determined by what is left in f, rather than by looking
		 * at the value returned.
		 */
		if ((error = reclock(vp, &f, 0, flag, offset, NULL)) != 0) {
			LM_DEBUG((2, "frlck",
			    "fs_frlock(F_GETLK)  =>  error= %d\n", error));
			goto out;
		}
		if (f.l_type != F_UNLCK) {
			/* This lock conflicts with a local lock. */
			if (proc == NLM_TEST) {
				*flk = f;

				LM_DEBUG((2, "frlck", "fs_frlock(F_GETLK)  "
				    "=>  NLM_TEST conflict\n"));
				error = 0;

				/*
				 * When fcntl is called with F_GETLK,
				 * and whence field is other than 0,
				 * then the call to convoff at exit
				 * modifies the lock fields
				 * incorrectly. So put the whence
				 * field to zero before calling
				 * convoff().
				 */
				if ((cmd == F_GETLK) || (cmd == F_O_GETLK))
					whence = 0;
				goto out;
			} else {
				LM_DEBUG((2, "frlck", "fs_frlock(F_GETLK)  "
				    "=>  NLM_LOCK conflict\n"));
				error = EAGAIN;
				goto out;
			}
		}
	}

	/*
	 * Check to see if any signals have been posted to our thread.
	 * If so, bail out now unless it is an unlock in which case we go ahead
	 */
	if ((proc != NLM_UNLOCK) && lm_sigispending()) {
		LM_DEBUG((2, "frlck", "signal before lm_callrpc() loop"));
		error = EINTR;
		goto out;
	}

	/*
	 * Unregister the lock with the local locking code before
	 * contacting the server.  This avoids a potential race where
	 * another process gets notified that it has been granted a lock
	 * before we can unregister ourselves locally.
	 */
	if (proc == NLM_UNLOCK) {
		flk->l_pid = ttoproc(curthread)->p_pid;
		lm_register_lock_locally(vp, ls, flk, flag, offset);
	}

	/*
	 * If this is a blocking request, allocate an lm_sleep for it.  We
	 * need to do this now, because if we wait until we get a "blocked"
	 * response, there is a window during which the GRANTED call could
	 * come in before we can make note of its arrival.
	 *
	 * Also tell statd to keep track of the server.  This guarantees
	 * the server will eventually get notified if client crashes and
	 * reboots.
	 */
	if (proc == NLM_LOCK && arg.nla.block) {
		lslp = lm_get_sleep(lm, ls, &alk.fh, &alk.oh,
		    (u_offset_t)alk.l_offset, (len_t)alk.l_len, vp);
		lm_sm_client(lm, ls, lm_get_me());
	}

call_server:
	/*
	 * Call the server. Loop until success or EINTR or EINVAL.
	 * (Done by the continue statement.) After each try, sleep
	 * LM_ERROR_SLP seconds. If server lock-manager is in its
	 * grace period, sleep LM_GRACE seconds. Before each try,
	 * free the res struct (this is also OK the first time).
	 */
	for (;;) {
		signalled = 0;
		xdr_free(xdr_res, (caddr_t)&res);

		error = lm_callrpc(lm, ls, NLM_PROG, NLM_VERS, proc, xdr_arg,
		    (caddr_t)&arg, xdr_res, (caddr_t)&res,
		    lm->lm_sa.retransmittimeout, LM_RETRY, FALSE);

		LM_DEBUG((2, "frlck", "lm_callrpc() returned %d", error));

		/*
		 * If a signal was posted while we were ignoring them,
		 * take note.
		 */
		if (lm_sigispending()) {
			LM_DEBUG((2, "frlck", "signal after lm_callrpc()"));
			signalled = 1;
		}

		/*
		 * It is possible that the request has actually been
		 * granted, but the server's granted response never
		 * made it to us because of this signal or error.
		 * Record this fact so an unlock request will be
		 * generated in nfs_lockrelease() when the file is
		 * closed.
		 */
		if (!on_list && (origproc == NLM_LOCK)) {
			on_list = 1;
			nfs_add_locking_id(vp, ttoproc(curthread)->p_pid,
			    RLMPL_PID, (char *)&ttoproc(curthread)->p_pid,
			    sizeof (pid_t));
		}

		switch (error) {
		case 0:
			if (res.nr.stat.stat == nlm_denied_grace_period) {
				LM_DEBUG((2, "frlck",
				    "Server in grace -- sleeping"));
				if (signalled) {
					xdr_free(xdr_res, (caddr_t)&res);
					error = EINTR;
					goto out;
				}
				if (error = delay_sig(LM_GRACE_SLP * hz)) {
					break;
				}
				continue;
			}
			break;

		case EINVAL:
			/*
			 * A serious error in the RPC call. Do not
			 * call again.
			 */
			LM_DEBUG((2, "frlck", "RPC call had EINVAL."));
			break;

		case EINTR:
			/*
			 * The RPC call was interrupted.  Do not
			 * call again.
			 */
			LM_DEBUG((2, "frlck", "RPC call interrupted."));
			break;

		case EIO:
		default:
			/*
			 * We got an error in the RPC call. Wait a
			 * bit, and call again, - unless we got
			 * interrupted.
			 */
			LM_DEBUG((2, "frlck",
			    "RPC call failed: error= %d -- sleeping", error));
			if (signalled) {
				/*
				 * There's a possible race condition here.
				 * A signal may have arrived after we timed
				 * out (EIO) but the request actually made
				 * it to the server.  If we were blocking on
				 * a lock, we cancel it just to be safe.
				 */
				if ((proc == NLM_LOCK) && (arg.nla.block)) {
					proc = NLM_CANCEL;
					xdr_arg = xdr_nlm_cancargs;
					ASSERT(xdr_res == xdr_nlm_res);
					mutex_enter(&lm_stat.lock);
					cookie = lm_stat.cookie++;
					mutex_exit(&lm_stat.lock);
					arg.nca.cookie.n_len = sizeof (cookie);
					arg.nca.cookie.n_bytes =
					    (char *)&cookie;
					arg.nca.block = cmd == F_SETLKW;
					arg.nca.exclusive =
					    flk->l_type == F_WRLCK;
					arg.nca.alock = alk;
					goto call_server;
				}
				error = EINTR;
				goto out;
			}

			/*
			 * Don't retry if the zone is going away
			 */
			if (zone_status_get(curproc->p_zone)
			    >= ZONE_IS_SHUTTING_DOWN) {
				break;
			}

			if (error = delay_sig(LM_ERROR_SLP * hz)) {
				break;
			}
			continue;
		}
		/* If we get here, then break the loop. */
		break;
	}


	/*
	 * If no errors, interpret the results.
	 *
	 * First we hack around a bug that some servers have, where they
	 * return an error when a blocked request is retransmitted.
	 * (Note that we can tell a retransmitted blocked request from a
	 * blocking request that was lost, because we know whether we
	 * received a response to the original request.)
	 *
	 * Then we return to the normal result processing.  For NLM_LOCK
	 * calls which have been blocked, we sleep for awhile and wait for
	 * the server to notify us that the lock has been granted.  For
	 * NLM_LOCK or NLM_UNLOCK calls that succeed, the local locking
	 * module must be called to register the result.
	 */
	if (!error) {
		if (proc == NLM_LOCK &&
		    arg.nla.block &&
		    (res.nr.stat.stat == nlm_deadlck ||
		    res.nr.stat.stat == nlm_denied_nolocks) &&
		    blocked_rexmit) {
			res.nr.stat.stat = nlm_blocked;
		}

		if ((proc == NLM_LOCK) && (arg.nla.block) &&
		    (res.nr.stat.stat == nlm_blocked)) {
			/*
			 * We are blocked.  Wait for an NLM_GRANTED,
			 * unless we've been signaled in which case
			 * we cancel.
			 */
			if (signalled) {
				LM_DEBUG((2, "frlck",
				    "signaled while blocking"));
				error = EINTR;
			} else {
				/*
				 * Invoke the callbacks. Ensure that the
				 * CPR information returned is in turn passed
				 * to lm_waitfor_granted so the thread can be
				 * made suspend-safe.
				 */
				cprp = flk_invoke_callbacks(flk_cbp,
				    FLK_BEFORE_SLEEP);
				error = lm_waitfor_granted(lm, lslp, cprp);
				(void) flk_invoke_callbacks(flk_cbp,
				    FLK_AFTER_SLEEP);
			}

			switch (error) {
			case 0:
				/*
				 * Got the lock from an NLM_GRANTED!
				 * Act as if the NLM_LOCK call
				 * succeeded.
				 */
				res.nr.stat.stat = nlm_granted;
				break;

			case EINTR:
				/*
				 * Sleep was interrupted. Issue an
				 * NLM_CANCEL and exit.
				 */
				LM_DEBUG((2, "frlck", "EINTR after blocking"));
				proc = NLM_CANCEL;
				xdr_arg = xdr_nlm_cancargs;
				ASSERT(xdr_res == xdr_nlm_res);
				mutex_enter(&lm_stat.lock);
				cookie = lm_stat.cookie++;
				mutex_exit(&lm_stat.lock);
				arg.nca.cookie.n_len = sizeof (cookie);
				arg.nca.cookie.n_bytes = (char *)&cookie;
				arg.nca.block = cmd == F_SETLKW;
				arg.nca.exclusive = flk->l_type == F_WRLCK;
				arg.nca.alock = alk;
				goto call_server;

			default:
				LM_DEBUG((2, "frlck",
				    "lm_waitfor_granted returned %d", error));
				/*
				 * Sleep timed out. Retransmit
				 * NLM_LOCK call.
				 */
				blocked_rexmit = 1;
				goto call_server;
			}
		}

		switch (proc) {
		case NLM_TEST:
			switch (res.ntr.stat.stat) {
			case nlm_granted:
				flk->l_type = F_UNLCK;
				flk->l_whence = 0;
				error = 0;
				break;

			case nlm_denied:
				flk->l_type =
				    res.ntr.stat.nlm_testrply_u.holder.
				    exclusive ? F_WRLCK : F_RDLCK;
				flk->l_whence = 0;
				off =
				    res.ntr.stat.nlm_testrply_u.holder.l_offset;
				len =
				    res.ntr.stat.nlm_testrply_u.holder.l_len;
				if (off > MAXOFF32_T) {
					flk->l_start = MAXOFF32_T;
					flk->l_end = 0;
				} else if (len > MAXOFF32_T) {
					flk->l_start = off;
					flk->l_end = 0;
				} else if ((off + len - 1) > MAXOFF32_T) {
					flk->l_start = off;
					flk->l_end = 0;
				} else {
					flk->l_start = off;
					flk->l_len = len;
				}
				flk->l_sysid = 0;
				flk->l_pid =
				    res.ntr.stat.nlm_testrply_u.holder.svid;
				error = 0;
				break;

			case nlm_denied_nolocks:
				error = ENOLCK;
				break;

			default:
				LM_DEBUG((1, "frlck",
				    "unexpected test stat= %d",
				    res.nr.stat.stat));
				error = EINVAL;
				break;
			}
			break;

		case NLM_LOCK:
			switch (res.nr.stat.stat) {
			case nlm_granted:
				flk->l_pid = ttoproc(curthread)->p_pid;
				lm_register_lock_locally(vp, ls, flk, flag,
				    offset);
				/*
				 * Invalidate all the buffer cache for the
				 * vnode. We want to be sure that the read
				 * operation gets the newest data. Tell the
				 * SM to monitor the server.
				 */
				error = VOP_PUTPAGE(vp, (u_offset_t)0, 0,
				    B_INVAL, cr, NULL);
				if (error &&
				    (error == ENOSPC || error == EDQUOT)) {
					rnode_t *rp = VTOR(vp);
					mutex_enter(&rp->r_statelock);
					if (!rp->r_error)
						rp->r_error = error;
					mutex_exit(&rp->r_statelock);
				}
				lm_sm_client(lm, ls, lm_get_me());
				error = 0;
				break;

			case nlm_blocked:
			case nlm_denied:
				if (arg.nla.block) {
					LM_DEBUG((2, "frlck",
					    "unexpected stat= %d for "
					    "blocking lock",
					    res.nr.stat.stat));
				}
				error = EAGAIN;
				break;

			case nlm_denied_nolocks:
			case nlm_deadlck:
				error = lm_stat_to_errno[res.ntr.stat.stat];
				break;

			default:
				LM_DEBUG((1, "frlck",
				    "unexpected lock stat= %d",
				    res.nr.stat.stat));
				error = EINVAL;
				break;
			}
			break;

		case NLM_UNLOCK:
			switch (res.nr.stat.stat) {
			case nlm_granted:
				error = 0;
				break;

			case nlm_denied:
				error = EINVAL;	/* shouldn't happen */
				break;

			case nlm_denied_nolocks:
				error = lm_stat_to_errno[res.ntr.stat.stat];
				break;

			default:
				LM_DEBUG((1, "frlck",
				    "unexpected unlock stat= %d",
				    res.nr.stat.stat));
				error = EINVAL;
				break;
			}
			break;

		case NLM_CANCEL:
			/*
			 * Set error to EINTR. This was the reason
			 * for sending NLM_CANCEL.
			 */
			error = EINTR;
			break;
		}
	}
	xdr_free(xdr_res, (caddr_t)&res);

out:
	if (lwp)
		lwp->lwp_nostop--;

	if (convoff(vp, flk, whence, offset)) {
		LM_DEBUG((2, "frlck", "final convoff failed"));
	}

	LM_DEBUG((2, "frlck", "End: error= %d, type= %d\n",
	    error, flk->l_type));

	if (ls != NULL) {
		lm_rel_sysid(ls);
	}
	if (lslp != NULL) {
		lm_rel_sleep(lm, lslp);
	}

	return (error);
}


/*
 * map NLM4 return status value into an errno value
 */
int
lm_stat4_to_errno[] = {
	0,	/* NLM4_GRANTED */
	EAGAIN,	/* NLM4_DENIED */
	ENOLCK,	/* NLM4_DENIED_NOLOCKS */
	EAGAIN,	/* NLM4_BLOCKED */
	EAGAIN,	/* NLM4_DENIED_GRACE_PERIOD */
	EDEADLK,	/* NLM4_DEADLCK */
	EROFS,	/* NLM4_ROFS */
	ESTALE,	/* NLM4_STALE_FH */
	EFBIG,	/* NLM4_FBIG */
	ENOLCK	/* NLM4_FAILED */
};

/*
 * NLM4 version of frlock.  It does the same thing as lm_frlock, but
 * but uses the NLM4 version of the RPC protocol to call the lock
 * manager.  This is called by nfs3_frlock().
 *
 * !!!NOTICE!!!
 * There are two versions of the locking routines: one for NFSv2 (lm_frlock)
 * and the other for NFSv3 (lm4_frlock).  Changes made to one routine MUST
 * be made to the corresponding code in the other routine.
 *
 */
int
lm4_frlock(struct vnode *vp, int cmd, struct flock64 *flk, int flag,
    u_offset_t offset, struct cred *cr, netobj *fh,
    struct flk_callback *flk_cbp)
{
	union {
		nlm4_testargs    nta;
		nlm4_lockargs    nla;
		nlm4_unlockargs  nua;
		nlm4_cancargs    nca;
	} arg;

	union {
		nlm4_testres	ntr;
		nlm4_res		nr;
	} res;

	nlm4_lock	alk;
	int		cookie;
	int		proc, origproc;
	xdrproc_t	xdr_arg;
	xdrproc_t	xdr_res;
	struct lm_sysid *ls = NULL;
	short		whence;
	int		error = 0;
	int		blocked_rexmit = 0; /* retrans. of blocked request */
	int		signalled;
	struct lm_sleep *lslp = NULL;
	char		oh_bytes[LM_OH_LEN];
	int		on_list = 0;
	callb_cpr_t	*cprp;
	klwp_t		*lwp;
	lm_globals_t	*lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	LM_DEBUG((1, "frlck4",
	    "cmd= %d, offset= %lld, type= %d, start= %" PRId64
	    ", len= %" PRId64 ", whence= %d",
	    cmd, offset, flk->l_type, flk->l_start, flk->l_len,
	    flk->l_whence));

	mutex_enter(&lm_stat.lock);
	cookie = lm_stat.cookie++;
	mutex_exit(&lm_stat.lock);

#ifdef DEBUG
	if (lm_gc_sysids)
		lm_free_sysid_table(lm);
#endif

	/*
	 * Convert the offset. It must be restored before returning.
	 * (And not before.) If whence == 2, we need the newest
	 * length of the file. Therefore invalidate the ATTR_CACHE.
	 */
	whence = flk->l_whence;

	if (whence == 2) {
		PURGE_ATTRCACHE(vp);
	}
	if (error = convoff(vp, flk, 0, offset)) {
		LM_DEBUG((2, "frlck4", "convoff  =>  error= %d\n", error));
		return (error);
	}

	if ((lwp = ttolwp(curthread)) != NULL)
		lwp->lwp_nostop++;

	/* Reset arg and res. */
	bzero(&arg, sizeof (arg));
	bzero(&res, sizeof (res));

	/*
	 * Initialize alock. The oh field is set to the current time,
	 * followed by the value of the cookie.  For blocking lock
	 * requests, this is so that it can be used to match up a GRANTED
	 * call with a blocked lock reliably (even across reboots).  For
	 * other requests, the oh still needs to be set to something, and
	 * this combination is pretty cheap to use.
	 */
	alk.caller_name = uts_nodename();
	alk.fh = *fh;
	/* oh_bytes holds the process id and the system name */
	lm_set_oh(lm, oh_bytes, sizeof (oh_bytes), curproc->p_pid);
	alk.oh.n_len = sizeof (oh_bytes);
	alk.oh.n_bytes = &oh_bytes[0];
	alk.svid = curproc->p_pid;
	alk.l_offset = flk->l_start;
	alk.l_len = flk->l_len;

	/* Initialize call-specific parameters. */
	switch (cmd) {
	case F_GETLK:
	case F_O_GETLK:
		proc = NLMPROC4_TEST;
		xdr_arg = xdr_nlm4_testargs;
		xdr_res = xdr_nlm4_testres;
		arg.nta.cookie.n_len = sizeof (cookie);
		arg.nta.cookie.n_bytes = (char *)&cookie;
		arg.nta.exclusive = flk->l_type == F_WRLCK;
		arg.nta.alock = alk;
		break;

	case F_SETLK:
	case F_SETLKW:
		if (flk->l_type == F_UNLCK) {
			proc = NLMPROC4_UNLOCK;
			xdr_arg = xdr_nlm4_unlockargs;
			xdr_res = xdr_nlm4_res;
			arg.nua.cookie.n_len = sizeof (cookie);
			arg.nua.cookie.n_bytes = (char *)&cookie;
			arg.nua.alock = alk;
		} else {
			proc = NLMPROC4_LOCK;
			xdr_arg = xdr_nlm4_lockargs;
			xdr_res = xdr_nlm4_res;
			arg.nla.cookie.n_len = sizeof (cookie);
			arg.nla.cookie.n_bytes = (char *)&cookie;
			arg.nla.block = cmd == F_SETLKW;
			arg.nla.exclusive = flk->l_type == F_WRLCK;
			arg.nla.alock = alk;
			arg.nla.reclaim = FALSE;
			arg.nla.state = 1;
		}
		break;

	default:
		LM_DEBUG((2, "frlck4", "cmd= %d  =>  EINVAL\n", cmd));
		error = EINVAL;
		goto out;
	}

	origproc = proc;
	/*
	 * If we are setting a lock, check that the file is opened
	 * with the correct mode.
	 */
	if (proc == NLMPROC4_LOCK) {
		if ((flk->l_type == F_RDLCK && (flag & FREAD) == 0) ||
		    (flk->l_type == F_WRLCK && (flag & FWRITE) == 0)) {
			error = EBADF;
			goto out;
		}
	}

	/* Find the lm_sysid */
	ls = lm_find_sysid(lm, VTOMI(vp));
	if (ls == NULL) {
		error = ENOLCK;
		goto out;
	}

	/*
	 * Before calling server, use the local reclocks as a cache
	 * for NLMPROC4_TEST and non-blocking NLMPROC4_LOCK calls. Note, there
	 * is no point doing this for blocking NLMPROC4_LOCK calls, since
	 * we always want to sleep.
	 */
	if ((proc == NLMPROC4_TEST) ||
	    (proc == NLMPROC4_LOCK && !arg.nla.block)) {
		struct flock64    f = *flk;

		LM_DEBUG((2, "frlck4", "Checking reclock cache"));

		f.l_pid = ttoproc(curthread)->p_pid;
		f.l_sysid = ls->sysid | LM_SYSID_CLIENT;
		/*
		 * Simulate an F_GETLK call.  Note that the result is
		 * determined by what is left in f, rather than by looking
		 * at the value returned.
		 */
		if ((error = reclock(vp, &f, 0, flag, offset, NULL)) != 0) {
			LM_DEBUG((2, "frlck4",
			    "fs_frlock(F_GETLK)  =>  error= %d\n", error));
			goto out;
		}
		if (f.l_type != F_UNLCK) {
			/* This lock conflicts with a local lock. */
			if (proc == NLMPROC4_TEST) {
				*flk = f;

				LM_DEBUG((2, "frlck4", "fs_frlock(F_GETLK)  "
				    "=>  NLMPROC4_TEST conflict\n"));
				error = 0;

				/*
				 * When fcntl is called with F_GETLK,
				 * and whence field is other than 0,
				 * then the call to convoff at exit
				 * modifies the lock fields
				 * incorrectly. So put the whence
				 * field to zero before calling
				 * convoff().
				 */
				if ((cmd == F_GETLK) || (cmd == F_O_GETLK))
					whence = 0;
				goto out;
			} else {
				LM_DEBUG((2, "frlck4", "fs_frlock(F_GETLK)  "
				    "=>  NLMPROC4_LOCK conflict\n"));
				error = EAGAIN;
				goto out;
			}
		}
	}

	/*
	 * Check to see if any signals have been posted to our thread.
	 * If so, bail out now unless it is an unlock in which case we go ahead
	 */
	if ((proc != NLMPROC4_UNLOCK) && lm_sigispending()) {
		LM_DEBUG((2, "frlck4", "signal before lm_callrpc() loop"));
		error = EINTR;
		goto out;
	}

	/*
	 * Unregister the lock with the local locking code before
	 * contacting the server.  This avoids a potential race where
	 * another process gets notified that it has been granted a lock
	 * before we can unregister ourselves locally.
	 */
	if (proc == NLMPROC4_UNLOCK) {
		flk->l_pid = ttoproc(curthread)->p_pid;
		lm_register_lock_locally(vp, ls, flk, flag, offset);
	}

	/*
	 * If this is a blocking request, allocate an lm_sleep for it.  We
	 * need to do this now, because if we wait until we get a "blocked"
	 * response, there is a window during which the GRANTED call could
	 * come in before we can make note of its arrival.
	 *
	 * Also tell statd to keep track of the server.  This guarantees
	 * the server will eventually get notified if client crashes and
	 * reboots.
	 */
	if (proc == NLMPROC4_LOCK && arg.nla.block) {
		lslp = lm_get_sleep(lm, ls, &alk.fh, &alk.oh, alk.l_offset,
		    alk.l_len, vp);
		lm_sm_client(lm, ls, lm_get_me());
	}

call_server:
	/*
	 * Call the server. Loop until success or EINTR or EINVAL.
	 * (Done by the continue statement.) After each try, sleep
	 * LM_ERROR_SLP seconds. If server lock-manager is in its
	 * grace period, sleep LM_GRACE seconds. Before each try,
	 * free the res struct (this is also OK the first time).
	 */
	for (;;) {
		signalled = 0;
		xdr_free(xdr_res, (caddr_t)&res);

		error = lm_callrpc(lm, ls, NLM_PROG, NLM4_VERS, proc, xdr_arg,
		    (caddr_t)&arg, xdr_res, (caddr_t)&res,
		    lm->lm_sa.retransmittimeout, LM_RETRY, FALSE);

		LM_DEBUG((2, "frlck4", "lm_callrpc() returned %d", error));

		/*
		 * If a signal was posted while we were ignoring them,
		 * take note.
		 */
		if (lm_sigispending()) {
			LM_DEBUG((2, "frlck4", "signal after lm_callrpc()"));
			signalled = 1;
		}

		/*
		 * It is possible that the request has actually been
		 * granted, but the server's granted response never
		 * made it to us because of this signal or error.
		 * Record this fact so an unlock request will be
		 * generated in nfs_lockrelease() when the file is
		 * closed.
		 */
		if (!on_list && (origproc == NLMPROC4_LOCK)) {
			on_list = 1;
			nfs_add_locking_id(vp, ttoproc(curthread)->p_pid,
			    RLMPL_PID, (char *)&ttoproc(curthread)->p_pid,
			    sizeof (pid_t));
		}

		switch (error) {
		case 0:
			if (res.nr.stat.stat == NLM4_DENIED_GRACE_PERIOD) {
				LM_DEBUG((2, "frlck4",
				    "Server in grace -- sleeping"));
				if (signalled) {
					xdr_free(xdr_res, (caddr_t)&res);
					error = EINTR;
					goto out;
				}
				if (error = delay_sig(LM_GRACE_SLP * hz)) {
					break;
				}
				continue;
			}
			break;

		case EINVAL:
			/*
			 * A serious error in the RPC call. Do not
			 * call again.
			 */
			LM_DEBUG((2, "frlck4", "RPC call had EINVAL."));
			break;

		case EINTR:
			/*
			 * The RPC call was interrupted.  Do not
			 * call again.
			 */
			LM_DEBUG((2, "frlck4", "RPC call interrupted."));
			break;

		case EIO:
		default:
			/*
			 * We got an error in the RPC call. Wait a
			 * bit, and call again, - unless we got
			 * interrupted.
			 */
			LM_DEBUG((2, "frlck4",
			    "RPC call failed: error= %d -- sleeping", error));
			if (signalled) {
				/*
				 * There's a possible race condition here.
				 * A signal may have arrived after we timed
				 * out (EIO) but the request actually made
				 * it to the server.  If we were blocking on
				 * a lock, we cancel it just to be safe.
				 */
				if ((proc == NLMPROC4_LOCK) && arg.nla.block) {
					proc = NLMPROC4_CANCEL;
					xdr_arg = xdr_nlm4_cancargs;
					ASSERT(xdr_res == xdr_nlm4_res);
					mutex_enter(&lm_stat.lock);
					cookie = lm_stat.cookie++;
					mutex_exit(&lm_stat.lock);
					arg.nca.cookie.n_len = sizeof (cookie);
					arg.nca.cookie.n_bytes =
					    (char *)&cookie;
					arg.nca.block = cmd == F_SETLKW;
					arg.nca.exclusive =
					    flk->l_type == F_WRLCK;
					arg.nca.alock = alk;
					goto call_server;
				}
				error = EINTR;
				goto out;
			}

			/*
			 * Don't retry if the zone is going away
			 */
			if (zone_status_get(curproc->p_zone)
			    >= ZONE_IS_SHUTTING_DOWN) {
				break;
			}

			if (error = delay_sig(LM_ERROR_SLP * hz)) {
				break;
			}
			continue;
		}
		/* If we get here, then break the loop. */
		break;
	}


	/*
	 * If no errors, interpret the results.
	 *
	 * First we hack around a bug that some servers have, where they
	 * return an error when a blocked request is retransmitted.
	 * (Note that we can tell a retransmitted blocked request from a
	 * blocking request that was lost, because we know whether we
	 * received a response to the original request.)
	 *
	 * Then we return to the normal result processing.  For
	 * NLMPROC4_LOCK calls which have been blocked, we sleep for awhile
	 * and wait for the server to notify us that the lock has been
	 * granted.  For NLMPROC4_LOCK or NLMPROC4_UNLOCK calls that
	 * succeed, the local locking module must be called to register the
	 * result.
	 */
	if (!error) {
		if (proc == NLMPROC4_LOCK &&
		    arg.nla.block &&
		    (res.nr.stat.stat == NLM4_DEADLCK ||
		    res.nr.stat.stat == NLM4_DENIED_NOLOCKS) &&
		    blocked_rexmit) {
			res.nr.stat.stat = NLM4_BLOCKED;
		}

		if ((proc == NLMPROC4_LOCK) && (arg.nla.block) &&
		    (res.nr.stat.stat == NLM4_BLOCKED)) {
			/*
			 * We are blocked.  Wait for an NLM4_GRANTED,
			 * unless we've been signaled in which case
			 * we cancel.
			 */
			if (signalled) {
				LM_DEBUG((2, "frlck4",
				    "signaled while blocking"));
				error = EINTR;
			} else {
				/*
				 * Invoke the callbacks. Ensure that the
				 * CPR information returned is in turn passed
				 * to lm_waitfor_granted so the thread can be
				 * made suspend-safe.
				 */
				cprp = flk_invoke_callbacks(flk_cbp,
				    FLK_BEFORE_SLEEP);
				error = lm_waitfor_granted(lm, lslp, cprp);
				(void) flk_invoke_callbacks(flk_cbp,
				    FLK_AFTER_SLEEP);
			}

			switch (error) {
			case 0:
				/*
				 * Got the lock from an NLM4_GRANTED!
				 * Act as if the NLMPROC4_LOCK call
				 * succeeded.
				 */
				res.nr.stat.stat = NLM4_GRANTED;
				break;

			case EINTR:
				/*
				 * Sleep was interrupted. Issue an
				 * NLMPROC4_CANCEL and exit.
				 */
				LM_DEBUG((2, "frlck4", "EINTR after blocking"));
				proc = NLMPROC4_CANCEL;
				xdr_arg = xdr_nlm4_cancargs;
				ASSERT(xdr_res == xdr_nlm4_res);
				mutex_enter(&lm_stat.lock);
				cookie = lm_stat.cookie++;
				mutex_exit(&lm_stat.lock);
				arg.nca.cookie.n_len = sizeof (cookie);
				arg.nca.cookie.n_bytes = (char *)&cookie;
				arg.nca.block = cmd == F_SETLKW;
				arg.nca.exclusive = flk->l_type == F_WRLCK;
				arg.nca.alock = alk;
				goto call_server;

			default:
				LM_DEBUG((2, "frlck4",
				    "lm_waitfor_granted returned %d", error));
				/*
				 * Sleep timed out. Retransmit
				 * NLMPROC4_LOCK call.
				 */
				blocked_rexmit = 1;
				goto call_server;
			}
		}

		switch (proc) {
		case NLMPROC4_TEST:
			switch (res.ntr.stat.stat) {
			case NLM4_GRANTED:
				flk->l_type = F_UNLCK;
				flk->l_whence = 0;
				error = 0;
				break;

			case NLM4_DENIED:
				flk->l_type = res.ntr.stat.nlm4_testrply_u.
				    holder.exclusive ? F_WRLCK : F_RDLCK;
				flk->l_start = res.ntr.stat.nlm4_testrply_u.
				    holder.l_offset;
				flk->l_len = res.ntr.stat.nlm4_testrply_u.
				    holder.l_len;
				flk->l_whence = 0;
				flk->l_sysid = 0;
				flk->l_pid = res.ntr.stat.nlm4_testrply_u.
				    holder.svid;
				error = 0;
				break;

			case NLM4_DENIED_NOLOCKS:
			case NLM4_BLOCKED:
			case NLM4_DENIED_GRACE_PERIOD:
			case NLM4_DEADLCK:
			case NLM4_ROFS:
			case NLM4_STALE_FH:
			case NLM4_FBIG:
			case NLM4_FAILED:
				error = lm_stat4_to_errno[res.ntr.stat.stat];
				break;

			default:
				LM_DEBUG((1, "frlck4",
				    "unexpected test stat= %d",
				    res.nr.stat.stat));
				error = EINVAL;
				break;
			}
			break;

		case NLMPROC4_LOCK:
			switch (res.nr.stat.stat) {
			case NLM4_GRANTED:
				flk->l_pid = ttoproc(curthread)->p_pid;
				lm_register_lock_locally(vp, ls, flk, flag,
				    offset);
				/*
				 * Invalidate all the buffer cache for the
				 * vnode. We want to be sure that the read
				 * operation gets the newest data. Tell the
				 * SM to monitor the server.
				 */
				error = VOP_PUTPAGE(vp, (u_offset_t)0, 0,
				    B_INVAL, cr, NULL);
				if (error &&
				    (error == ENOSPC || error == EDQUOT)) {
					rnode_t *rp = VTOR(vp);
					mutex_enter(&rp->r_statelock);
					if (!rp->r_error)
						rp->r_error = error;
					mutex_exit(&rp->r_statelock);
				}
				lm_sm_client(lm, ls, lm_get_me());
				error = 0;
				break;

			case NLM4_BLOCKED:
			case NLM4_DENIED:
				if (arg.nla.block) {
					LM_DEBUG((2, "frlck4",
					    "unexpected stat= %d "
					    "for blocking lock",
					    res.nr.stat.stat));
				}
				error = EAGAIN;
				break;

			case NLM4_DENIED_NOLOCKS:
			case NLM4_DEADLCK:
			case NLM4_DENIED_GRACE_PERIOD:
			case NLM4_ROFS:
			case NLM4_STALE_FH:
			case NLM4_FBIG:
			case NLM4_FAILED:
				error = lm_stat4_to_errno[res.ntr.stat.stat];
				break;

			default:
				LM_DEBUG((1, "frlck4",
				    "unexpected lock stat= %d",
				    res.nr.stat.stat));
				error = EINVAL;
				break;
			}
			break;

		case NLMPROC4_UNLOCK:
			switch (res.nr.stat.stat) {
			case NLM4_GRANTED:
				error = 0;
				break;

			case NLM4_DENIED:
			case NLM4_FAILED:
				error = EINVAL;	/* shouldn't happen */
				break;

			case NLM4_DENIED_NOLOCKS:
			case NLM4_ROFS:
			case NLM4_FBIG:
			case NLM4_STALE_FH:
				error = lm_stat4_to_errno[res.ntr.stat.stat];
				break;

			default:
				LM_DEBUG((1, "frlck4",
				    "unexpected unlock stat= %d",
				    res.nr.stat.stat));
				error = EINVAL;
				break;
			}
			break;

		case NLMPROC4_CANCEL:
			/*
			 * Set error to EINTR. This was the reason
			 * for sending NLMPROC4_CANCEL.
			 */
			error = EINTR;
			break;
		}
	}
	xdr_free(xdr_res, (caddr_t)&res);

out:
	if (lwp)
		lwp->lwp_nostop--;

	if (convoff(vp, flk, whence, offset)) {
		LM_DEBUG((2, "frlck4", "final convoff failed"));
	}

	LM_DEBUG((2, "frlck4", "End: error= %d, type= %d\n",
	    error, flk->l_type));

	if (ls != NULL) {
		lm_rel_sysid(ls);
	}
	if (lslp != NULL) {
		lm_rel_sleep(lm, lslp);
	}

	return (error);
}

/*
 * After a lock is successfully obtained from the server, register it
 * locally.  This should always succeed because the server told us we could
 * have the lock.  This routine assumes that the flk->l_pid is set
 * appropriately.
 */
void
lm_register_lock_locally(vnode_t *vp, struct lm_sysid *ls, struct flock64 *flk,
    int flag, u_offset_t offset)
{
	int oldsysid;
	int error;
	bool_t need_free = FALSE;
	lm_globals_t *lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	/*
	 * If no lm_sysid information passed in, find the lm_sysid
	 * thru the vnode (vp).
	 */
	if (ls == NULL) {
		/* Find the lm_sysid */
		ls = lm_find_sysid(lm, VTOMI(vp));

		/*
		 * No lm_sysid information, no local locks.
		 * This is a no-op.
		 */
		if (ls == NULL) {
#ifdef DEBUG
			cmn_err(CE_WARN, "register_lock_locally failed");
			cmn_err(CE_CONT, "can not get sysid\n");
#endif
			return;
		}

		/*
		 * This ls is allocated thru this routine, so it needs
		 * to be freed via this routine.
		 */
		need_free = TRUE;
	}

	oldsysid = flk->l_sysid;
	ASSERT(ls->sysid != 0);
	flk->l_sysid = ls->sysid | LM_SYSID_CLIENT;
	error = reclock(vp, flk, SETFLCK, flag, offset, NULL);
#ifdef lint
	error = error;
#endif
#ifdef DEBUG
	if (error != 0) {
		cmn_err(CE_WARN, "register_lock_locally failed");
		cmn_err(CE_CONT,
		    "error %d, vp 0x%p, pid %d, sysid 0x%x",
		    error, (void *)vp, flk->l_pid, flk->l_sysid);
		cmn_err(CE_CONT, "type %d off 0x%" PRIx64
		    " len 0x%" PRIx64 "\n",
		    flk->l_type, flk->l_start, flk->l_len);
		(void) reclock(vp, flk, 0, flag, offset, NULL);
		cmn_err(CE_CONT, "blocked by pid %d sysid 0x%x type %d "
		    "off 0x%" PRIx64 " len 0x%" PRIx64 "\n",
		    flk->l_pid, flk->l_sysid, flk->l_type, flk->l_start,
		    flk->l_len);
	}
#endif
	flk->l_sysid = oldsysid;

	if (need_free && ls != NULL) {
		lm_rel_sysid(ls);
	}
}

/*
 * After a share is successfully obtained from the server, register it
 * locally.  This should always succeed because the server told us we could
 * have the share.
 */
static void
register_share_locally(vnode_t *vp, struct lm_sysid *ls, int cmd,
    struct shrlock *shr, int flag)
{
	int32_t oldsysid;
	int error;

	ASSERT(shr->s_pid == ttoproc(curthread)->p_pid);
	oldsysid = shr->s_sysid;
	ASSERT(ls->sysid != 0);
	shr->s_sysid = (int32_t)(ls->sysid | LM_SYSID_CLIENT);
	error = fs_shrlock(vp, cmd, shr, flag, CRED(), NULL);
#ifdef lint
	error = error;
#endif
#ifdef DEBUG
	/*
	 * Not all failed fs_shrlocks are errors:
	 *	extra F_UNSHARE requests my be generated on close
	 *	some old servers will allow multiple shares of the
	 *		same request
	 */
	if (share_debug && error) {
		cmn_err(CE_WARN, "register_share_locally failed");
		cmn_err(CE_CONT,
		    "error %d, vp 0x%p, cmd %d, pid %d, sysid 0x%x ",
		    error, (void *)vp, cmd, shr->s_pid, shr->s_sysid);
		cmn_err(CE_CONT, "access %d deny %d\n",
		    shr->s_access, shr->s_deny);
	}
#endif
	shr->s_sysid = oldsysid;
}

/*
 * Mapping of compatibility mode:
 *	0 == if access equal read-only set deny write else set deny read-write
 *	1 == Win 32 set equal to deny none.
 */
static int nlm_shr_compat = 0;

int
lm_shrlock(struct vnode *vp, int cmd, struct shrlock *shr, int flag, netobj *fh)
{
	nlm_shareargs   nsa;
	nlm_shareres	nsr;

	int		cookie;
	int		proc;
	xdrproc_t	xdr_arg;
	xdrproc_t	xdr_res;
	struct lm_sysid *ls = NULL;
	int		error = 0;
	int		signalled;
	lm_globals_t	*lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	mutex_enter(&lm_stat.lock);
	cookie = lm_stat.cookie++;
	mutex_exit(&lm_stat.lock);

#ifdef DEBUG
	if (lm_gc_sysids)
		lm_free_sysid_table(lm);
#endif

	/* Reset arg and res. */
	bzero(&nsa, sizeof (nsa));
	bzero(&nsr, sizeof (nsr));

	/*
	 * Initialize share argument
	 */
	nsa.share.caller_name = uts_nodename();
	nsa.share.fh = *fh;
	nsa.share.oh.n_len = shr->s_own_len;
	nsa.share.oh.n_bytes = shr->s_owner;

	/*
	 * There is a simpler mapping of these but it would be much less
	 * readable and maintainable.
	 */
	switch (shr->s_access) {
	case 0:
	case F_RDACC:
		/*
		 * We may be passed access == 0 for F_UNSHARE.
		 * It shouldn't matter what we map the access to
		 * in such a case, so we just pick a valid value.
		 */
		nsa.share.access = fsa_R;
		break;
	case F_WRACC:
		nsa.share.access = fsa_W;
		break;
	case F_RWACC:
		nsa.share.access = fsa_RW;
		break;
	default:
		nsa.share.access = fsa_R;	/* Shouldn't ever happen */
		break;
	}

	switch (shr->s_deny) {
	case F_NODNY:
		nsa.share.mode = fsm_DN;
		break;
	case F_RDDNY:
		nsa.share.mode = fsm_DR;
		break;
	case F_WRDNY:
		nsa.share.mode = fsm_DW;
		break;
	case F_RWDNY:
		nsa.share.mode = fsm_DRW;
		break;
	case F_COMPAT:
		/*
		 * There is no NLM equivilant to F_COMPAT so we map it
		 */
		if (nlm_shr_compat == 0) {
			/*
			 * Wabi defaults
			 */
			if (nsa.share.access == fsa_R) {
				nsa.share.mode = fsm_DW;
				shr->s_deny = F_WRDNY;
			} else {
				nsa.share.mode = fsm_DRW;
				shr->s_deny = F_RWDNY;
			}
		} else {
			/*
			 * Win32 defaults
			 */
			nsa.share.mode = fsm_DN;
			shr->s_deny = F_NODNY;
		}
		break;
	default:
		nsa.share.mode = fsm_DN;	/* Shouldn't ever happen */
		break;
	}

	/*
	 * Initialize shareargs argument
	 */
	nsa.cookie.n_len = sizeof (cookie);
	nsa.cookie.n_bytes = (char *)&cookie;
	nsa.reclaim = FALSE;

	/* Initialize call-specific parameters. */
	switch (cmd) {
	case F_SHARE:
		proc = NLM_SHARE;
		break;
	case F_UNSHARE:
		proc = NLM_UNSHARE;
		break;
	default:
		error = EINVAL;
		goto out;
	}
	xdr_arg = xdr_nlm_shareargs;
	xdr_res = xdr_nlm_shareres;

	/* Find the lm_sysid */
	ls = lm_find_sysid(lm, VTOMI(vp));

	/*
	 * Check to see if any signals have been posted to our thread.
	 * If so, bail out now.
	 */
	if (lm_sigispending()) {
		LM_DEBUG((2, "shrlck", "signal before lm_callrpc() loop"));
		error = EINTR;
		goto out;
	}

	/*
	 * Also, unregister the share with the local code before
	 * contacting the server.  This avoids a potential race where
	 * another process gets notified that it has been granted a share
	 * before we can unregister ourselves locally.
	 */
	if (proc == NLM_UNSHARE) {
		register_share_locally(vp, ls, cmd, shr, flag);
	}

call_server:
	/*
	 * Call the server. Loop until success or EINTR or EINVAL.
	 * (Done by the continue statement.) After each try, sleep
	 * LM_ERROR_SLP seconds. If server lock-manager is in its
	 * grace period, sleep LM_GRACE seconds. Before each try,
	 * free the res struct (this is also OK the first time).
	 */
	for (;;) {
		signalled = 0;
		xdr_free(xdr_res, (caddr_t)&nsr);

		error = lm_callrpc(lm, ls, NLM_PROG, NLM_VERS3, proc, xdr_arg,
		    (caddr_t)&nsa, xdr_res, (caddr_t)&nsr,
		    lm->lm_sa.retransmittimeout, LM_RETRY, FALSE);

		LM_DEBUG((6, "shrlck", "lm_callrpc() returned %d", error));

		/*
		 * If a signal was posted while we were ignoring them,
		 * take note.
		 */
		if (lm_sigispending()) {
			LM_DEBUG((2, "shrlck", "signal after lm_callrpc()"));
			signalled = 1;
		}

		/*
		 * It is possible that the request has actually been
		 * granted, but the server's granted response never
		 * made it to us because of this signal or error.
		 * Record this fact so an unshare request will be
		 * generated in nfs_lockrelease() when the file is
		 * closed.
		 */
		if ((signalled == 1) || (error != 0)) {
			nfs_add_locking_id(vp, ttoproc(curthread)->p_pid,
			    RLMPL_OWNER, shr->s_owner, shr->s_own_len);
		}

		switch (error) {
		case 0:
			if (nsr.stat == nlm_denied_grace_period) {
				LM_DEBUG((2, "shrlck",
				    "Server in grace -- sleeping"));
				if (signalled) {
					xdr_free(xdr_res, (caddr_t)&nsr);
					error = EINTR;
					goto out;
				}
				if (error = delay_sig(LM_GRACE_SLP * hz)) {
					break;
				}
				continue;
			}
			break;

		case EINVAL:
			/*
			 * A serious error in the RPC call. Do not
			 * call again.
			 */
			LM_DEBUG((2, "shrlck", "RPC call had EINVAL."));
			break;

		case EINTR:
			/*
			 * The RPC call was interrupted.  Do not
			 * call again.
			 */
			LM_DEBUG((2, "shrlck", "RPC call interrupted."));
			break;

		case EIO:
		default:
			/*
			 * We got an error in the RPC call. Wait a
			 * bit, and call again, - unless we got
			 * interrupted.
			 */
			LM_DEBUG((2, "shrlck",
			    "RPC call failed: error= %d -- sleeping", error));
#ifdef ELIDED
			if (signalled) {
				/*
				 * It is possible that the request has actually
				 * been granted, but the server's granted
				 * response never made it to us because of this
				 * error.  Record this fact so an unshare
				 * request will be generated in
				 * nfs_lockrelease() when the file is closed.
				 */
				nfs_add_locking_id(vp,
				    ttoproc(curthread)->p_pid,
				    RLMPL_OWNER, shr->s_owner,
				    shr->s_own_len);
				error = EINTR;
				goto out;
			}
#endif
			/*
			 * Don't retry if the zone is going away
			 */
			if (zone_status_get(curproc->p_zone)
			    >= ZONE_IS_SHUTTING_DOWN) {
				break;
			}

			if (error = delay_sig(LM_ERROR_SLP * hz)) {
				break;
			}
			continue;
		}
		/* If we get here, then break the loop. */
		break;
	}


	/*
	 * If no errors, interpret the results.
	 *
	 * Then we return to the normal result processing. For
	 * NLM_SHARE or NLM_UNSHARE calls that succeed, the local locking
	 * module must be called to register the result.
	 */
	if (!error) {
		switch (proc) {
		case NLM_SHARE:
			switch (nsr.stat) {
			case nlm_granted:
				register_share_locally(vp, ls, cmd, shr, flag);
				nfs_add_locking_id(vp,
				    ttoproc(curthread)->p_pid, RLMPL_OWNER,
				    shr->s_owner, shr->s_own_len);
				error = 0;
				break;

			case nlm_blocked:
			case nlm_denied:
				error = EAGAIN;
				break;

			case nlm_denied_nolocks:
			case nlm_deadlck:
				error = lm_stat_to_errno[nsr.stat];
				break;

			default:
				error = EINVAL;
				break;
			}
			break;

		case NLM_UNSHARE:
			switch (nsr.stat) {
			case nlm_granted:
				error = 0;
				break;

			case nlm_denied:
				error = EINVAL;	/* shouldn't happen */
				break;

			case nlm_denied_nolocks:
				error = lm_stat_to_errno[nsr.stat];
				break;

			default:
				LM_DEBUG((1, "shrlck",
				    "unexpected unshare stat= %d", nsr.stat));
				error = EINVAL;
				break;
			}
			break;
		}
	}
#ifdef ELIDED
	else {
		/*
		 * It is possible that the request has actually been granted,
		 * but the server's granted response never made it to us
		 * because of this error.  Record this fact so an unshare
		 * request will be generated in nfs_lockrelease() when the
		 * file is closed.
		 */
		nfs_add_locking_id(vp, ttoproc(curthread)->p_pid,
		    RLMPL_OWNER, shr->s_owner, shr->s_own_len);
	}
#endif
	xdr_free(xdr_res, (caddr_t)&nsr);

out:
	LM_DEBUG((6, "shrlck", "End: error= %d\n", error));

	if (ls != NULL) {
		lm_rel_sysid(ls);
	}

	return (error);
}

int
lm4_shrlock(struct vnode *vp, int cmd, struct shrlock *shr, int flag,
    netobj *fh)
{
	nlm4_shareargs  nsa;
	nlm4_shareres	nsr;

	int		cookie;
	int		proc;
	xdrproc_t	xdr_arg;
	xdrproc_t	xdr_res;
	struct lm_sysid *ls = NULL;
	int		error = 0;
	int		signalled;
	lm_globals_t	*lm;

	lm = zone_getspecific(lm_zone_key, curproc->p_zone);

	mutex_enter(&lm_stat.lock);
	cookie = lm_stat.cookie++;
	mutex_exit(&lm_stat.lock);

#ifdef DEBUG
	if (lm_gc_sysids)
		lm_free_sysid_table(lm);
#endif

	/* Reset arg and res. */
	bzero(&nsa, sizeof (nsa));
	bzero(&nsr, sizeof (nsr));

	/*
	 * Initialize share argument
	 */
	nsa.share.caller_name = uts_nodename();
	nsa.share.fh = *fh;
	nsa.share.oh.n_len = shr->s_own_len;
	nsa.share.oh.n_bytes = shr->s_owner;

	/*
	 * There is a simpler mapping of these but it would be much less
	 * readable and maintainable.
	 */
	switch (shr->s_access) {
	case 0:
	case F_RDACC:
		/*
		 * We may be passed access == 0 for F_UNSHARE.
		 * It shouldn't matter what we map the access to
		 * in such a case, so we just pick a valid value.
		 */
		nsa.share.access = FSA_R;
		break;
	case F_WRACC:
		nsa.share.access = FSA_W;
		break;
	case F_RWACC:
		nsa.share.access = FSA_RW;
		break;
	default:
		nsa.share.access = FSA_R;	/* Shouldn't ever happen */
		break;
	}

	switch (shr->s_deny) {
	case F_NODNY:
		nsa.share.mode = FSM_DN;
		break;
	case F_RDDNY:
		nsa.share.mode = FSM_DR;
		break;
	case F_WRDNY:
		nsa.share.mode = FSM_DW;
		break;
	case F_RWDNY:
		nsa.share.mode = FSM_DRW;
		break;
	case F_COMPAT:
		/*
		 * There is no NLM equivilant to F_COMPAT so we map it
		 */
		if (nlm_shr_compat == 0) {
			/*
			 * Wabi defaults
			 */
			if (nsa.share.access == FSA_R) {
				nsa.share.mode = FSM_DW;
				shr->s_deny = F_WRDNY;
			} else {
				nsa.share.mode = FSM_DRW;
				shr->s_deny = F_RWDNY;
			}
		} else {
			/*
			 * Win32 defaults
			 */
			nsa.share.mode = FSM_DN;
			shr->s_deny = F_NODNY;
		}
		break;
	default:
		nsa.share.mode = FSM_DN;	/* Shouldn't ever happen */
		break;
	}

	/*
	 * Initialize shareargs argument
	 */
	nsa.cookie.n_len = sizeof (cookie);
	nsa.cookie.n_bytes = (char *)&cookie;
	nsa.reclaim = FALSE;

	/* Initialize call-specific parameters. */
	switch (cmd) {
	case F_SHARE:
		proc = NLMPROC4_SHARE;
		break;
	case F_UNSHARE:
		proc = NLMPROC4_UNSHARE;
		break;
	default:
		error = EINVAL;
		goto out;
	}
	xdr_arg = xdr_nlm4_shareargs;
	xdr_res = xdr_nlm4_shareres;

	/* Find the lm_sysid */
	ls = lm_find_sysid(lm, VTOMI(vp));

	/*
	 * Check to see if any signals have been posted to our thread.
	 * If so, bail out now.
	 */
	if (lm_sigispending()) {
		LM_DEBUG((2, "shrlck", "signal before lm_callrpc() loop"));
		error = EINTR;
		goto out;
	}

	/*
	 * Also, unregister the share with the local code before
	 * contacting the server.  This avoids a potential race where
	 * another process gets notified that it has been granted a share
	 * before we can unregister ourselves locally.
	 */
	if (proc == NLMPROC4_UNSHARE) {
		register_share_locally(vp, ls, cmd, shr, flag);
	}

call_server:
	/*
	 * Call the server. Loop until success or EINTR or EINVAL.
	 * (Done by the continue statement.) After each try, sleep
	 * LM_ERROR_SLP seconds. If server lock-manager is in its
	 * grace period, sleep LM_GRACE seconds. Before each try,
	 * free the res struct (this is also OK the first time).
	 */
	for (;;) {
		signalled = 0;
		xdr_free(xdr_res, (caddr_t)&nsr);

		error = lm_callrpc(lm, ls, NLM_PROG, NLM4_VERS, proc, xdr_arg,
		    (caddr_t)&nsa, xdr_res, (caddr_t)&nsr,
		    lm->lm_sa.retransmittimeout, LM_RETRY, FALSE);

		LM_DEBUG((6, "shrlck", "lm_callrpc() returned %d", error));

		/*
		 * If a signal was posted while we were ignoring them,
		 * take note.
		 */
		if (lm_sigispending()) {
			LM_DEBUG((2, "shrlck", "signal after lm_callrpc()"));
			signalled = 1;
		}

		/*
		 * It is possible that the request has actually been
		 * granted, but the server's granted response never
		 * made it to us because of this signal or error.
		 * Record this fact so an unshare request will be
		 * generated in nfs_lockrelease() when the file is
		 * closed.
		 */
		if ((signalled == 1) || (error != 0)) {
			nfs_add_locking_id(vp, ttoproc(curthread)->p_pid,
			    RLMPL_OWNER, shr->s_owner, shr->s_own_len);
		}

		switch (error) {
		case 0:
			if (nsr.stat == NLM4_DENIED_GRACE_PERIOD) {
				LM_DEBUG((2, "shrlck",
				    "Server in grace -- sleeping"));
				if (signalled) {
					xdr_free(xdr_res, (caddr_t)&nsr);
					error = EINTR;
					goto out;
				}
				if (error = delay_sig(LM_GRACE_SLP * hz)) {
					break;
				}
				continue;
			}
			break;

		case EINVAL:
			/*
			 * A serious error in the RPC call. Do not
			 * call again.
			 */
			LM_DEBUG((2, "shrlck", "RPC call had EINVAL."));
			break;

		case EINTR:
			/*
			 * The RPC call was interrupted.  Do not
			 * call again.
			 */
			LM_DEBUG((2, "shrlck", "RPC call interrupted."));
			break;

		case EIO:
		default:
			/*
			 * We got an error in the RPC call. Wait a
			 * bit, and call again, - unless we got
			 * interrupted.
			 */
			LM_DEBUG((2, "shrlck",
			    "RPC call failed: error= %d -- sleeping", error));
#ifdef ELIDED
			if (signalled) {
				/*
				 * It is possible that the request has actually
				 * been granted, but the server's granted
				 * response never made it to us because of this
				 * error.  Record this fact so an unshare
				 * request will be generated in
				 * nfs_lockrelease() when the file is closed.
				 */
				nfs_add_locking_id(vp,
				    ttoproc(curthread)->p_pid, RLMPL_OWNER,
				    shr->s_owner, shr->s_own_len);
				error = EINTR;
				goto out;
			}
#endif
			/*
			 * Don't retry if the zone is going away
			 */
			if (zone_status_get(curproc->p_zone)
			    >= ZONE_IS_SHUTTING_DOWN) {
				break;
			}

			if (error = delay_sig(LM_ERROR_SLP * hz)) {
				break;
			}
			continue;
		}
		/* If we get here, then break the loop. */
		break;
	}


	/*
	 * If no errors, interpret the results.
	 *
	 * Then we return to the normal result processing. For
	 * NLM4_SHARE or NLM4_UNSHARE calls that succeed, the local locking
	 * module must be called to register the result.
	 */
	if (!error) {
		switch (proc) {
		case NLMPROC4_SHARE:
			switch (nsr.stat) {
			case NLM4_GRANTED:
				register_share_locally(vp, ls, cmd, shr, flag);
				nfs_add_locking_id(vp,
				    ttoproc(curthread)->p_pid, RLMPL_OWNER,
				    shr->s_owner, shr->s_own_len);
				error = 0;
				break;

			case NLM4_BLOCKED:
			case NLM4_DENIED:
				error = EAGAIN;
				break;

			case NLM4_DENIED_NOLOCKS:
			case NLM4_DEADLCK:
				error = lm_stat4_to_errno[nsr.stat];
				break;

			default:
				LM_DEBUG((1, "shrlck",
				    "unexpected share stat= %d", nsr.stat));
				error = EINVAL;
				break;
			}
			break;

		case NLMPROC4_UNSHARE:
			switch (nsr.stat) {
			case NLM4_GRANTED:
				error = 0;
				break;

			case NLM4_DENIED:
				error = EINVAL;	/* shouldn't happen */
				break;

			case NLM4_DENIED_NOLOCKS:
				error = lm_stat4_to_errno[nsr.stat];
				break;

			default:
				LM_DEBUG((1, "shrlck",
				    "unexpected unshare stat= %d", nsr.stat));
				error = EINVAL;
				break;
			}
			break;
		}
	}
#ifdef ELIDED
	else {
		/*
		 * It is possible that the request has actually been granted,
		 * but the server's granted response never made it to us
		 * because of this error.  Record this fact so an unshare
		 * request will be generated in nfs_lockrelease() when the
		 * file is closed.
		 */
		nfs_add_locking_id(vp, ttoproc(curthread)->p_pid,
		    RLMPL_OWNER, shr->s_owner, shr->s_own_len);
	}
#endif
	xdr_free(xdr_res, (caddr_t)&nsr);

out:
	LM_DEBUG((6, "shrlck", "End: error= %d\n", error));

	if (ls != NULL) {
		lm_rel_sysid(ls);
	}

	return (error);
}
