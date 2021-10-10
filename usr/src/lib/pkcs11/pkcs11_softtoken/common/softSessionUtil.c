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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <md5.h>
#include <pthread.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/sha1.h>
#include <sys/sha2.h>
#include <security/cryptoki.h>
#include "softGlobal.h"
#include "softSession.h"
#include "softObject.h"
#include "softOps.h"
#include "softKeystore.h"
#include "softKeystoreUtil.h"


CK_ULONG soft_session_cnt = 0;		/* the number of opened sessions */
CK_ULONG soft_session_rw_cnt = 0;	/* the number of opened R/W sessions */

#define	DIGEST_MECH_OK(_m_)	((_m_) == CKM_MD5 || (_m_) == CKM_SHA_1 || \
				(_m_) == CKM_SHA256 || (_m_) == CKM_SHA384 || \
				(_m_) == CKM_SHA512)

extern soft_session_t token_session;

/*
 * Delete all the sessions. First, obtain the global session
 * list lock. Then start to delete one session at a time.
 * Release the global session list lock before returning to
 * caller.
 */
CK_RV
soft_delete_all_sessions(boolean_t force)
{

	CK_RV rv = CKR_OK;
	CK_RV rv1;
	soft_session_t *session_p;
	soft_session_t *session_p1;

	/* Acquire the global session list lock */
	(void) pthread_mutex_lock(&soft_sessionlist_mutex);

	session_p = soft_session_list;

	/* Delete all the sessions in the session list */
	while (session_p) {
		session_p1 = session_p->next;

		/*
		 * Delete a session by calling soft_delete_session()
		 * with a session pointer and a boolean arguments.
		 * Boolean value TRUE is used to indicate that the
		 * caller holds the lock on the global session list.
		 *
		 */
		rv1 = soft_delete_session(session_p, force, B_TRUE);

		/* Record the very first error code */
		if (rv == CKR_OK) {
			rv = rv1;
		}

		session_p = session_p1;
	}

	/* No session left */
	soft_session_list = NULL;

	/* Release the global session list lock */
	(void) pthread_mutex_unlock(&soft_sessionlist_mutex);

	return (rv);

}


/*
 * Create a new session struct, and add it to the session linked list.
 *
 * This function will acquire the global session list lock, and release
 * it after adding the session to the session linked list.
 */
CK_RV
soft_add_session(CK_FLAGS flags, CK_VOID_PTR pApplication,
	CK_NOTIFY notify, CK_ULONG *sessionhandle_p)
{

	soft_session_t *new_sp = NULL;

	/* Allocate a new session struct */
	new_sp = calloc(1, sizeof (soft_session_t));
	if (new_sp == NULL) {
		return (CKR_HOST_MEMORY);
	}

	new_sp->magic_marker = SOFTTOKEN_SESSION_MAGIC;
	new_sp->pApplication = pApplication;
	new_sp->Notify = notify;
	new_sp->flags = flags;
	new_sp->state = CKS_RO_PUBLIC_SESSION;
	new_sp->object_list = NULL;
	new_sp->ses_refcnt = 0;
	new_sp->ses_close_sync = 0;

	(void) pthread_mutex_lock(&soft_giant_mutex);
	if (soft_slot.authenticated) {
		(void) pthread_mutex_unlock(&soft_giant_mutex);
		if (flags & CKF_RW_SESSION) {
			new_sp->state = CKS_RW_USER_FUNCTIONS;
		} else {
			new_sp->state = CKS_RO_USER_FUNCTIONS;
		}
	} else {
		(void) pthread_mutex_unlock(&soft_giant_mutex);
		if (flags & CKF_RW_SESSION) {
			new_sp->state = CKS_RW_PUBLIC_SESSION;
		} else {
			new_sp->state = CKS_RO_PUBLIC_SESSION;
		}
	}

	/* Initialize the lock for the newly created session */
	if (pthread_mutex_init(&new_sp->session_mutex, NULL) != 0) {
		free(new_sp);
		return (CKR_CANT_LOCK);
	}

	(void) pthread_cond_init(&new_sp->ses_free_cond, NULL);

	/* Acquire the global session list lock */
	(void) pthread_mutex_lock(&soft_sessionlist_mutex);

	/* Insert the new session in front of session list */
	if (soft_session_list == NULL) {
		soft_session_list = new_sp;
		new_sp->next = NULL;
		new_sp->prev = NULL;
	} else {
		soft_session_list->prev = new_sp;
		new_sp->next = soft_session_list;
		new_sp->prev = NULL;
		soft_session_list = new_sp;
	}

	/* Type casting the address of a session struct to a session handle */
	*sessionhandle_p =  (CK_ULONG)new_sp;
	++soft_session_cnt;
	if (flags & CKF_RW_SESSION)
		++soft_session_rw_cnt;

	if (soft_session_cnt == 1)
		/*
		 * This is the first session to be opened, so we can set
		 * validate the public token objects in token list now.
		 */
		soft_validate_token_objects(B_TRUE);

	/* Release the global session list lock */
	(void) pthread_mutex_unlock(&soft_sessionlist_mutex);

	return (CKR_OK);

}

/*
 * This function adds the to-be-freed session to a linked list.
 * When the number of sessions queued in the linked list reaches the
 * maximum threshold MAX_SES_TO_BE_FREED, it will free the first
 * session (FIFO) in the list.
 */
void
session_delay_free(soft_session_t *sp)
{
	soft_session_t *tmp;

	(void) pthread_mutex_lock(&ses_delay_freed.ses_to_be_free_mutex);

	/* Add the newly deleted session at the end of the list */
	sp->next = NULL;
	if (ses_delay_freed.first == NULL) {
		ses_delay_freed.last = sp;
		ses_delay_freed.first = sp;
	} else {
		ses_delay_freed.last->next = sp;
		ses_delay_freed.last = sp;
	}

	if (++ses_delay_freed.count >= MAX_SES_TO_BE_FREED) {
		/*
		 * Free the first session in the list only if
		 * the total count reaches maximum threshold.
		 */
		ses_delay_freed.count--;
		tmp = ses_delay_freed.first->next;
		free(ses_delay_freed.first);
		ses_delay_freed.first = tmp;
	}
	(void) pthread_mutex_unlock(&ses_delay_freed.ses_to_be_free_mutex);
}

/*
 * Delete a session:
 * - Remove the session from the session linked list.
 *   Holding the lock on the global session list is needed to do this.
 * - Release all the objects created by the session.
 *
 * The boolean argument lock_held is used to indicate that whether
 * the caller of this function holds the lock on the global session
 * list or not.
 * - When called by soft_delete_all_sessions(), which is called by
 *   C_Finalize() or C_CloseAllSessions() -- the lock_held = TRUE.
 * - When called by C_CloseSession() -- the lock_held = FALSE.
 *
 * When the caller does not hold the lock on the global session
 * list, this function will acquire that lock in order to proceed,
 * and also release that lock before returning to caller.
 */
CK_RV
soft_delete_session(soft_session_t *session_p,
    boolean_t force, boolean_t lock_held)
{

	/*
	 * Check to see if the caller holds the lock on the global
	 * session list. If not, we need to acquire that lock in
	 * order to proceed.
	 */
	if (!lock_held) {
		/* Acquire the global session list lock */
		(void) pthread_mutex_lock(&soft_sessionlist_mutex);
	}

	/*
	 * Remove the session from the session linked list first.
	 */
	if (soft_session_list == session_p) {
		/* Session is the first one in the list */
		if (session_p->next) {
			soft_session_list = session_p->next;
			session_p->next->prev = NULL;
		} else {
			/* Session is the only one in the list */
			soft_session_list = NULL;
		}
	} else {
		/* Session is not the first one in the list */
		if (session_p->next) {
			/* Session is in the middle of the list */
			session_p->prev->next = session_p->next;
			session_p->next->prev = session_p->prev;
		} else {
			/* Session is the last one in the list */
			session_p->prev->next = NULL;
		}
	}

	--soft_session_cnt;
	if (session_p->flags & CKF_RW_SESSION)
		--soft_session_rw_cnt;

	if (!lock_held) {
		/*
		 * If the global session list lock is obtained by
		 * this function, then release that lock after
		 * removing the session from session linked list.
		 * We want the releasing of the objects of the
		 * session, and freeing of the session itself to
		 * be done without holding the global session list
		 * lock.
		 */
		(void) pthread_mutex_unlock(&soft_sessionlist_mutex);
	}


	/* Acquire the individual session lock */
	(void) pthread_mutex_lock(&session_p->session_mutex);
	/*
	 * Make sure another thread hasn't freed the session.
	 */
	if (session_p->magic_marker != SOFTTOKEN_SESSION_MAGIC) {
		(void) pthread_mutex_unlock(&session_p->session_mutex);
		return (CKR_OK);
	}

	/*
	 * The deletion of a session must be blocked when the session
	 * reference count is not zero. This means if any session related
	 * operation starts prior to the session close operation gets in,
	 * the session closing thread must wait for the non-closing
	 * operation to be completed before it can proceed the close
	 * operation.
	 *
	 * Unless we are being forced to shut everything down, this only
	 * happens if the libraries _fini() is running not of someone
	 * explicitly called C_Finalize().
	 */
	if (force)
		session_p->ses_refcnt = 0;

	while (session_p->ses_refcnt != 0) {
		/*
		 * We set the SESSION_REFCNT_WAITING flag before we put
		 * this closing thread in a wait state, so other non-closing
		 * operation thread will signal to wake it up only when
		 * the session reference count becomes zero and this flag
		 * is set.
		 */
		session_p->ses_close_sync |= SESSION_REFCNT_WAITING;
		(void) pthread_cond_wait(&session_p->ses_free_cond,
		    &session_p->session_mutex);
	}

	session_p->ses_close_sync &= ~SESSION_REFCNT_WAITING;

	/*
	 * Remove all the objects created in this session.
	 */
	soft_delete_all_objects_in_session(session_p, force);

	/*
	 * Mark session as no longer valid. This can only be done after all
	 * objects created by this session are free'd since the marker is
	 * still needed in the process of removing objects from the session.
	 */
	session_p->magic_marker = 0;

	(void) pthread_cond_destroy(&session_p->ses_free_cond);

	/* In case application did not call Final */
	if (session_p->digest.context != NULL)
		free(session_p->digest.context);

	if (session_p->encrypt.context != NULL)
		/*
		 * 1st B_TRUE: encrypt
		 * 2nd B_TRUE: caller is holding session_mutex.
		 */
		soft_crypt_cleanup(session_p, B_TRUE, B_TRUE);

	if (session_p->decrypt.context != NULL)
		/*
		 * 1st B_FALSE: decrypt
		 * 2nd B_TRUE: caller is holding session_mutex.
		 */
		soft_crypt_cleanup(session_p, B_FALSE, B_TRUE);

	if (session_p->sign.context != NULL)
		free(session_p->sign.context);

	if (session_p->verify.context != NULL)
		free(session_p->verify.context);

	if (session_p->find_objects.context != NULL) {
		find_context_t *fcontext;
		fcontext = (find_context_t *)session_p->find_objects.context;
		free(fcontext->objs_found);
		free(fcontext);
	}

	/* Reset SESSION_IS_CLOSIN flag. */
	session_p->ses_close_sync &= ~SESSION_IS_CLOSING;

	(void) pthread_mutex_unlock(&session_p->session_mutex);
	/* Destroy the individual session lock */
	(void) pthread_mutex_destroy(&session_p->session_mutex);

	/* Delay freeing the session */
	session_delay_free(session_p);

	return (CKR_OK);
}


/*
 * This function is used to type cast a session handle to a pointer to
 * the session struct. Also, it does the following things:
 * 1) Check to see if the session struct is tagged with a session
 *    magic number. This is to detect when an application passes
 *    a bogus session pointer.
 * 2) Acquire the lock on the designated session.
 * 3) Check to see if the session is in the closing state that another
 *    thread is performing.
 * 4) Increment the session reference count by one. This is to prevent
 *    this session from being closed by other thread.
 * 5) Release the lock held on the designated session.
 */
CK_RV
handle2session(CK_SESSION_HANDLE hSession, soft_session_t **session_p)
{

	soft_session_t *sp = (soft_session_t *)(hSession);

	/*
	 * No need to hold soft_sessionlist_mutex as we are
	 * just reading the value and 32-bit reads are atomic.
	 */
	if (all_sessions_closing) {
		return (CKR_SESSION_CLOSED);
	}

	if ((sp == NULL) ||
	    (sp->magic_marker != SOFTTOKEN_SESSION_MAGIC)) {
		return (CKR_SESSION_HANDLE_INVALID);
	}
	(void) pthread_mutex_lock(&sp->session_mutex);

	if (sp->ses_close_sync & SESSION_IS_CLOSING) {
		(void) pthread_mutex_unlock(&sp->session_mutex);
		return (CKR_SESSION_CLOSED);
	}

	/* Increment session ref count. */
	sp->ses_refcnt++;

	(void) pthread_mutex_unlock(&sp->session_mutex);

	*session_p = sp;

	return (CKR_OK);
}

/*
 * The format to be saved in the pOperationState will be:
 * 1. internal_op_state_t
 * 2. crypto_active_op_t
 * 3. actual context of the active operation
 */
CK_RV
soft_get_operationstate(soft_session_t *session_p, CK_BYTE_PTR pOperationState,
    CK_ULONG_PTR pulOperationStateLen)
{

	internal_op_state_t *p_op_state;
	CK_ULONG op_data_len = 0;
	CK_RV rv = CKR_OK;

	if (pulOperationStateLen == NULL)
		return (CKR_ARGUMENTS_BAD);

	(void) pthread_mutex_lock(&session_p->session_mutex);

	/* Check to see if encrypt operation is active. */
	if (session_p->encrypt.flags & CRYPTO_OPERATION_ACTIVE) {
		rv = CKR_STATE_UNSAVEABLE;
		goto unlock_session;
	}

	/* Check to see if decrypt operation is active. */
	if (session_p->decrypt.flags & CRYPTO_OPERATION_ACTIVE) {
		rv = CKR_STATE_UNSAVEABLE;
		goto unlock_session;
	}

	/* Check to see if sign operation is active. */
	if (session_p->sign.flags & CRYPTO_OPERATION_ACTIVE) {
		rv = CKR_STATE_UNSAVEABLE;
		goto unlock_session;
	}

	/* Check to see if verify operation is active. */
	if (session_p->verify.flags & CRYPTO_OPERATION_ACTIVE) {
		rv = CKR_STATE_UNSAVEABLE;
		goto unlock_session;
	}

	/* Check to see if digest operation is active. */
	if (session_p->digest.flags & CRYPTO_OPERATION_ACTIVE) {
		op_data_len = sizeof (internal_op_state_t) +
		    sizeof (crypto_active_op_t);

		switch (session_p->digest.mech.mechanism) {
		case CKM_MD5:
			op_data_len += sizeof (MD5_CTX);
			break;
		case CKM_SHA_1:
			op_data_len += sizeof (SHA1_CTX);
			break;
		case CKM_SHA256:
		case CKM_SHA384:
		case CKM_SHA512:
			op_data_len += sizeof (SHA2_CTX);
			break;
		default:
			rv = CKR_STATE_UNSAVEABLE;
			goto unlock_session;
		}

		if (pOperationState == NULL_PTR) {
			*pulOperationStateLen = op_data_len;
			goto unlock_session;
		} else {
			if (*pulOperationStateLen < op_data_len) {
				*pulOperationStateLen = op_data_len;
				rv = CKR_BUFFER_TOO_SMALL;
				goto unlock_session;
			}
		}

		/* Save internal_op_state_t */
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		p_op_state = (internal_op_state_t *)pOperationState;
		p_op_state->op_len = op_data_len;
		p_op_state->op_active = DIGEST_OP;
		p_op_state->op_session_state = session_p->state;

		/* Save crypto_active_op_t */
		(void) memcpy((CK_BYTE *)pOperationState +
		    sizeof (internal_op_state_t),
		    &session_p->digest,
		    sizeof (crypto_active_op_t));

		switch (session_p->digest.mech.mechanism) {
		case CKM_MD5:
			/* Save MD5_CTX for the active digest operation */
			(void) memcpy((CK_BYTE *)pOperationState +
			    sizeof (internal_op_state_t) +
			    sizeof (crypto_active_op_t),
			    session_p->digest.context,
			    sizeof (MD5_CTX));
			break;

		case CKM_SHA_1:
			/* Save SHA1_CTX for the active digest operation */
			(void) memcpy((CK_BYTE *)pOperationState +
			    sizeof (internal_op_state_t) +
			    sizeof (crypto_active_op_t),
			    session_p->digest.context,
			    sizeof (SHA1_CTX));
			break;

		case CKM_SHA256:
		case CKM_SHA384:
		case CKM_SHA512:
			/* Save SHA2_CTX for the active digest operation */
			(void) memcpy((CK_BYTE *)pOperationState +
			    sizeof (internal_op_state_t) +
			    sizeof (crypto_active_op_t),
			    session_p->digest.context,
			    sizeof (SHA2_CTX));
			break;

		default:
			rv = CKR_STATE_UNSAVEABLE;
		}
	} else {
		rv = CKR_OPERATION_NOT_INITIALIZED;
		goto unlock_session;
	}

	*pulOperationStateLen = op_data_len;

unlock_session:
	(void) pthread_mutex_unlock(&session_p->session_mutex);

	return (rv);

}

static CK_BYTE_PTR alloc_digest(CK_ULONG mech)
{
	CK_BYTE_PTR	ret_val;

	switch (mech) {
		case CKM_MD5:
			ret_val = (CK_BYTE_PTR) malloc(sizeof (MD5_CTX));
			break;
		case CKM_SHA_1:
			ret_val = (CK_BYTE_PTR) malloc(sizeof (SHA1_CTX));
			break;
		case CKM_SHA256:
		case CKM_SHA384:
		case CKM_SHA512:
			ret_val = (CK_BYTE_PTR) malloc(sizeof (SHA2_CTX));
			break;
		default:
			ret_val = NULL;
	}

	return (ret_val);
}

/*
 * The format to be restored from the pOperationState will be:
 * 1. internal_op_state_t
 * 2. crypto_active_op_t
 * 3. actual context of the saved operation
 */
CK_RV
soft_set_operationstate(soft_session_t *session_p, CK_BYTE_PTR pOperationState,
    CK_ULONG ulOperationStateLen, CK_OBJECT_HANDLE hEncryptionKey,
    CK_OBJECT_HANDLE hAuthenticationKey)
{

	CK_RV		rv = CKR_OK;
	internal_op_state_t *p_op_state;
	crypto_active_op_t *p_active_op;
	CK_ULONG offset = 0;
	CK_ULONG mech;
	void *free_it = NULL;

	/* LINTED E_BAD_PTR_CAST_ALIGN */
	p_op_state = (internal_op_state_t *)pOperationState;

	if (p_op_state->op_len != ulOperationStateLen) {
		/*
		 * The supplied data length does not match with
		 * the saved data length.
		 */
		return (CKR_SAVED_STATE_INVALID);
	}

	if (p_op_state->op_active != DIGEST_OP)
		return (CKR_SAVED_STATE_INVALID);

	if ((hAuthenticationKey != 0) || (hEncryptionKey != 0)) {
		return (CKR_KEY_NOT_NEEDED);
	}

	offset = sizeof (internal_op_state_t);
	/* LINTED E_BAD_PTR_CAST_ALIGN */
	p_active_op = (crypto_active_op_t *)(pOperationState + offset);
	offset += sizeof (crypto_active_op_t);
	mech = p_active_op->mech.mechanism;

	if (!DIGEST_MECH_OK(mech)) {
		return (CKR_SAVED_STATE_INVALID);
	}

	/*
	 * We may reuse digest.context in case the digest mechanisms (the one,
	 * which belongs to session and the operation, which we are restoring)
	 * are the same. If digest mechanisms are different, we have to release
	 * the digest context, which belongs to session and allocate a new one.
	 */
	(void) pthread_mutex_lock(&session_p->session_mutex);

	if (session_p->state != p_op_state->op_session_state) {
		/*
		 * The supplied session state does not match with
		 * the saved session state.
		 */
		rv = CKR_SAVED_STATE_INVALID;
		goto unlock_session;
	}

	if (session_p->digest.context &&
	    (session_p->digest.mech.mechanism != mech)) {
		free_it = session_p->digest.context;
		session_p->digest.context = NULL;
	}

	if (session_p->digest.context == NULL) {
		session_p->digest.context = alloc_digest(mech);

		if (session_p->digest.context == NULL) {
			/*
			 * put back original context into session in case
			 * allocation of new context has failed.
			 */
			session_p->digest.context = free_it;
			free_it = NULL;
			rv = CKR_HOST_MEMORY;
			goto unlock_session;
		}
	}

	/* Restore crypto_active_op_t */
	session_p->digest.mech.mechanism = mech;
	session_p->digest.flags = p_active_op->flags;

	switch (mech) {
		case CKM_MD5:
			/* Restore MD5_CTX from the saved digest operation */
			(void) memcpy((CK_BYTE *)session_p->digest.context,
			    (CK_BYTE *)pOperationState + offset,
			    sizeof (MD5_CTX));
			break;
		case CKM_SHA_1:
			/* Restore SHA1_CTX from the saved digest operation */
			(void) memcpy((CK_BYTE *)session_p->digest.context,
			    (CK_BYTE *)pOperationState + offset,
			    sizeof (SHA1_CTX));
			break;
		case CKM_SHA256:
		case CKM_SHA384:
		case CKM_SHA512:
			/* Restore SHA2_CTX from the saved digest operation */
			(void) memcpy((CK_BYTE *)session_p->digest.context,
			    (CK_BYTE *)pOperationState + offset,
			    sizeof (SHA2_CTX));
			break;
		default:
			rv = CKR_SAVED_STATE_INVALID;
	}

unlock_session:
	(void) pthread_mutex_unlock(&session_p->session_mutex);

	if (free_it != NULL)
		free(free_it);

	return (rv);
}


CK_RV
soft_login(CK_UTF8CHAR_PTR pPin, CK_ULONG ulPinLen)
{

	/*
	 * Authenticate the input PIN.
	 */
	return (soft_verify_pin(pPin, ulPinLen));

}

void
soft_logout(void)
{

	/*
	 * Delete all the private token objects from the "token_object_list".
	 */
	soft_delete_all_objects_in_session(&token_session, B_TRUE);
	soft_delete_all_in_core_token_objects(PRIVATE_TOKEN);
	return;

}

void
soft_acquire_all_session_mutexes(soft_session_t *session_p)
{
	/* Iterate through sessions acquiring all mutexes */
	while (session_p) {
		soft_object_t *object_p;

		(void) pthread_mutex_lock(&session_p->session_mutex);
		object_p = session_p->object_list;

		/* Lock also all objects related to session */
		while (object_p) {
			(void) pthread_mutex_lock(&object_p->object_mutex);
			object_p = object_p->next;
		}
		session_p = session_p->next;
	}
}

void
soft_release_all_session_mutexes(soft_session_t *session_p)
{
	/* Iterate through sessions releasing all mutexes */
	while (session_p) {
		/*
		 * N.B. Ideally, should go in opposite order to guarantee
		 * lock-order requirements but there is no tail pointer.
		 */
		soft_object_t *object_p = session_p->object_list;

		/* Unlock also all objects related to session */
		while (object_p) {
			(void) pthread_mutex_unlock(&object_p->object_mutex);
			object_p = object_p->next;
		}
		(void) pthread_mutex_unlock(&session_p->session_mutex);
		session_p = session_p->next;
	}
}

void
soft_set_session_context(soft_session_t *session_p, soft_session_op_t op,
    void *pContext, CK_MECHANISM_PTR pMechanism)
{
	(void) pthread_mutex_lock(&session_p->session_mutex);

	switch (op) {
	case SOFT_ENCRYPT_OP:
		session_p->encrypt.context = pContext;
		session_p->encrypt.mech.mechanism = pMechanism->mechanism;
		break;
	case SOFT_DECRYPT_OP:
		session_p->decrypt.context = pContext;
		session_p->decrypt.mech.mechanism = pMechanism->mechanism;
		break;
	case SOFT_SIGN_OP:
		session_p->sign.context = pContext;
		session_p->sign.mech.mechanism = pMechanism->mechanism;
		break;
	case SOFT_VERIFY_OP:
		session_p->verify.context = pContext;
		session_p->verify.mech.mechanism = pMechanism->mechanism;
		break;
	case SOFT_DIGEST_OP:
		session_p->digest.context = pContext;
		session_p->digest.mech.mechanism = pMechanism->mechanism;
		if (pMechanism->mechanism == CKM_SHA_1) {
			session_p->digest.mech.pParameter =
			    pMechanism->pParameter;
			session_p->digest.mech.ulParameterLen =
			    pMechanism->ulParameterLen;
		}
		break;
	default:
		break;
	}

	(void) pthread_mutex_unlock(&session_p->session_mutex);
}

void
soft_clear_session_context(soft_session_t *session_p, soft_session_op_t op,
    size_t context_sz)
{
	crypto_active_op_t *active_op;

	(void) pthread_mutex_lock(&session_p->session_mutex);

	switch (op) {
	case SOFT_ENCRYPT_OP:
		active_op = &(session_p->encrypt);
		break;
	case SOFT_DECRYPT_OP:
		active_op = &(session_p->decrypt);
		break;
	case SOFT_SIGN_OP:
		active_op = &(session_p->sign);
		break;
	case SOFT_VERIFY_OP:
		active_op = &(session_p->verify);
		break;
	case SOFT_DIGEST_OP:
		active_op = &(session_p->digest);
		break;
	default:
		(void) pthread_mutex_unlock(&session_p->session_mutex);
		return;
	}

	if (active_op->context != NULL) {
		bzero(active_op->context, context_sz);
		free(active_op->context);
		active_op->context = NULL;
	}

	(void) pthread_mutex_unlock(&session_p->session_mutex);
}

int
soft_session_operation_active(soft_session_t *session_p,
    soft_session_op_t op, boolean_t keep_lock)
{
	crypto_active_op_t *active_op;

	(void) pthread_mutex_lock(&session_p->session_mutex);

	switch (op) {
	case SOFT_ENCRYPT_OP:
		active_op = &(session_p->encrypt);
		break;
	case SOFT_DECRYPT_OP:
		active_op = &(session_p->decrypt);
		break;
	case SOFT_SIGN_OP:
		active_op = &(session_p->sign);
		break;
	case SOFT_VERIFY_OP:
		active_op = &(session_p->verify);
		break;
	default:
		(void) pthread_mutex_unlock(&session_p->session_mutex);
		return (0);
	}

	/* Check to see if the operation is already active. */
	if (active_op->flags & CRYPTO_OPERATION_ACTIVE) {
		if (!keep_lock)
			(void) pthread_mutex_unlock(&session_p->session_mutex);
		return (-1);
	}

	/* This active flag will remain ON during the entire operation. */
	active_op->flags = CRYPTO_OPERATION_ACTIVE;
	(void) pthread_mutex_unlock(&session_p->session_mutex);
	return (0);
}

void
soft_session_operation_inactive(soft_session_t *session_p,
    soft_session_op_t op)
{
	crypto_active_op_t *active_op;

	(void) pthread_mutex_lock(&session_p->session_mutex);

	switch (op) {
	case SOFT_ENCRYPT_OP:
		active_op = &(session_p->encrypt);
		break;
	case SOFT_DECRYPT_OP:
		active_op = &(session_p->decrypt);
		break;
	case SOFT_SIGN_OP:
		active_op = &(session_p->sign);
		break;
	case SOFT_VERIFY_OP:
		active_op = &(session_p->verify);
		break;
	default:
		(void) pthread_mutex_unlock(&session_p->session_mutex);
		return;
	}

	active_op->flags &= ~CRYPTO_OPERATION_ACTIVE;
	(void) pthread_mutex_unlock(&session_p->session_mutex);
}
