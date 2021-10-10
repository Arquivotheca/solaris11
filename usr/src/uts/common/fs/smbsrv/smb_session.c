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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */
#include <sys/atomic.h>
#include <sys/strsubr.h>
#include <sys/synch.h>
#include <sys/types.h>
#include <sys/socketvar.h>
#include <sys/sdt.h>
#include <smbsrv/netbios.h>
#include <smbsrv/smb_kproto.h>
#include <smbsrv/string.h>
#include <smbsrv/smb_xdr.h>
#include <smbsrv/smb_door.h>

static volatile uint64_t smb_kids;

uint32_t smb_keep_alive = SSN_KEEP_ALIVE_TIMEOUT;

static void smb_session_cancel(smb_session_t *);
static int smb_session_message(smb_session_t *);
static int smb_session_xprt_puthdr(smb_session_t *, smb_xprt_t *,
    uint8_t *, size_t);
void dump_smb_inaddr(smb_inaddr_t *ipaddr);
static uint32_t smb_session_auth_user(smb_session_t *,
	smb_authreq_t *, smb_authrsp_t *);
static int smb_session_add_user(smb_request_t *, smb_session_t *,
    smb_user_t *, smb_token_t *);
static void smb_session_delete_user(void *);

void
smb_session_timers(smb_llist_t *ll)
{
	smb_session_t	*session;

	smb_llist_enter(ll, RW_READER);
	session = smb_llist_head(ll);
	while (session != NULL) {
		/*
		 * Walk through the table and decrement each keep_alive
		 * timer that has not timed out yet. (keepalive > 0)
		 */
		SMB_SESSION_VALID(session);
		if (session->keep_alive &&
		    (session->keep_alive != (uint32_t)-1))
			session->keep_alive--;
		session = smb_llist_next(ll, session);
	}
	smb_llist_exit(ll);
}

void
smb_session_correct_keep_alive_values(smb_llist_t *ll, uint32_t new_keep_alive)
{
	smb_session_t		*sn;

	if (new_keep_alive == smb_keep_alive)
		return;
	/*
	 * keep alive == 0 means do not drop connection if it's idle
	 */
	smb_keep_alive = (new_keep_alive) ? new_keep_alive : -1;

	/*
	 * Walk through the table and set each session to the new keep_alive
	 * value if they have not already timed out.  Block clock interrupts.
	 */
	smb_llist_enter(ll, RW_READER);
	sn = smb_llist_head(ll);
	while (sn != NULL) {
		SMB_SESSION_VALID(sn);
		if (sn->keep_alive != 0)
			sn->keep_alive = new_keep_alive;
		sn = smb_llist_next(ll, sn);
	}
	smb_llist_exit(ll);
}

/*
 * smb_reconnection_check
 *
 * This function is called when a client indicates its current connection
 * should be the only one it has with the server, as indicated by VC=0 in
 * a SessionSetupX request. We go through the session list and destroy any
 * stale connections for that client.
 *
 * Clients don't associate IP addresses and servers. So a client may make
 * independent connections (i.e. with VC=0) to a server with multiple
 * IP addresses. So, when checking for a reconnection, we need to include
 * the local IP address, to which the client is connecting, when checking
 * for stale sessions.
 *
 * Also check the server's NetBIOS name to support simultaneous access by
 * multiple clients behind a NAT server.  This will only work for SMB over
 * NetBIOS on TCP port 139, it will not work SMB over TCP port 445 because
 * there is no NetBIOS name.  See also Knowledge Base article Q301673.
 */
void
smb_session_reconnection_check(smb_llist_t *ll, smb_session_t *sess)
{
	smb_session_t	*sn;

	smb_llist_enter(ll, RW_READER);
	sn = smb_llist_head(ll);
	while (sn != NULL) {
		SMB_SESSION_VALID(sn);
		if ((sn != sess) &&
		    smb_inet_equal(&sn->ipaddr, &sess->ipaddr) &&
		    smb_inet_equal(&sn->local_ipaddr, &sess->local_ipaddr) &&
		    (strcasecmp(sn->workstation, sess->workstation) == 0) &&
		    (sn->opentime <= sess->opentime) &&
		    (sn->s_kid < sess->s_kid)) {
			smb_session_disconnect(sn);
		}
		sn = smb_llist_next(ll, sn);
	}
	smb_llist_exit(ll);
}

/*
 * Send a session message - supports SMB-over-NBT and SMB-over-TCP.
 *
 * The mbuf chain is copied into a contiguous buffer so that the whole
 * message is submitted to smb_sosend as a single request.  This should
 * help Ethereal/Wireshark delineate the packets correctly even though
 * TCP_NODELAY has been set on the socket.
 *
 * If an mbuf chain is provided, it will be freed and set to NULL here.
 */
int
smb_session_send(smb_session_t *session, uint8_t type, mbuf_chain_t *mbc)
{
	smb_txreq_t	*txr;
	smb_xprt_t	hdr;
	int		rc;

	switch (session->s_state) {
	case SMB_SESSION_STATE_DISCONNECTED:
	case SMB_SESSION_STATE_TERMINATED:
		if ((mbc != NULL) && (mbc->chain != NULL)) {
			m_freem(mbc->chain);
			mbc->chain = NULL;
			mbc->flags = 0;
		}
		return (ENOTCONN);
	default:
		break;
	}

	txr = smb_net_txr_alloc();

	if ((mbc != NULL) && (mbc->chain != NULL)) {
		rc = mbc_moveout(mbc, (caddr_t)&txr->tr_buf[NETBIOS_HDR_SZ],
		    sizeof (txr->tr_buf) - NETBIOS_HDR_SZ, &txr->tr_len);
		if (rc != 0) {
			smb_net_txr_free(txr);
			return (rc);
		}
	}

	hdr.xh_type = type;
	hdr.xh_length = (uint32_t)txr->tr_len;

	rc = smb_session_xprt_puthdr(session, &hdr, txr->tr_buf,
	    NETBIOS_HDR_SZ);

	if (rc != 0) {
		smb_net_txr_free(txr);
		return (rc);
	}
	txr->tr_len += NETBIOS_HDR_SZ;
	smb_server_add_txb(session->s_server, (int64_t)txr->tr_len);
	return (smb_net_txr_send(session->sock, &session->s_txlst, txr));
}

/*
 * Read, process and respond to a NetBIOS session request.
 *
 * A NetBIOS session must be established for SMB-over-NetBIOS.  Validate
 * the calling and called name format and save the client NetBIOS name,
 * which is used when a NetBIOS session is established to check for and
 * cleanup leftover state from a previous session.
 *
 * Session requests are not valid for SMB-over-TCP, which is unfortunate
 * because without the client name leftover state cannot be cleaned up
 * if the client is behind a NAT server.
 */
static int
smb_session_request(smb_session_t *session)
{
	int			rc;
	char			*calling_name;
	char			*called_name;
	char 			client_name[NETBIOS_NAME_SZ];
	struct mbuf_chain 	mbc;
	char 			*names = NULL;
	smb_wchar_t		*wbuf = NULL;
	smb_xprt_t		hdr;
	char *p;
	int rc1, rc2;

	session->keep_alive = smb_keep_alive;

	if ((rc = smb_session_xprt_gethdr(session, &hdr)) != 0)
		return (rc);

	DTRACE_PROBE2(receive__session__req__xprthdr, struct session *, session,
	    smb_xprt_t *, &hdr);

	if ((hdr.xh_type != SESSION_REQUEST) ||
	    (hdr.xh_length != NETBIOS_SESSION_REQUEST_DATA_LENGTH)) {
		DTRACE_PROBE1(receive__session__req__failed,
		    struct session *, session);
		return (EINVAL);
	}

	names = kmem_alloc(hdr.xh_length, KM_SLEEP);

	if ((rc = smb_sorecv(session->sock, names, hdr.xh_length)) != 0) {
		kmem_free(names, hdr.xh_length);
		DTRACE_PROBE1(receive__session__req__failed,
		    struct session *, session);
		return (rc);
	}

	DTRACE_PROBE3(receive__session__req__data, struct session *, session,
	    char *, names, uint32_t, hdr.xh_length);

	called_name = &names[0];
	calling_name = &names[NETBIOS_ENCODED_NAME_SZ + 2];

	rc1 = netbios_name_isvalid(called_name, 0);
	rc2 = netbios_name_isvalid(calling_name, client_name);

	if (rc1 == 0 || rc2 == 0) {

		DTRACE_PROBE3(receive__invalid__session__req,
		    struct session *, session, char *, names,
		    uint32_t, hdr.xh_length);

		kmem_free(names, hdr.xh_length);
		MBC_INIT(&mbc, MAX_DATAGRAM_LENGTH);
		(void) smb_mbc_encodef(&mbc, "b",
		    DATAGRAM_INVALID_SOURCE_NAME_FORMAT);
		(void) smb_session_send(session, NEGATIVE_SESSION_RESPONSE,
		    &mbc);
		return (EINVAL);
	}

	DTRACE_PROBE3(receive__session__req__calling__decoded,
	    struct session *, session,
	    char *, calling_name, char *, client_name);

	/*
	 * The client NetBIOS name is in oem codepage format.
	 * We need to convert it to unicode and store it in
	 * multi-byte format.  We also need to strip off any
	 * spaces added as part of the NetBIOS name encoding.
	 */
	wbuf = kmem_alloc((SMB_PI_MAX_HOST * sizeof (smb_wchar_t)), KM_SLEEP);
	(void) oemtoucs(wbuf, client_name, SMB_PI_MAX_HOST, OEM_CPG_850);
	(void) smb_wcstombs(session->workstation, wbuf, SMB_PI_MAX_HOST);
	kmem_free(wbuf, (SMB_PI_MAX_HOST * sizeof (smb_wchar_t)));

	if ((p = strchr(session->workstation, ' ')) != 0)
		*p = '\0';

	kmem_free(names, hdr.xh_length);
	return (smb_session_send(session, POSITIVE_SESSION_RESPONSE, NULL));
}

/*
 * Read 4-byte header from the session socket and build an in-memory
 * session transport header.  See smb_xprt_t definition for header
 * format information.
 *
 * Direct hosted NetBIOS-less SMB (SMB-over-TCP) uses port 445.  The
 * first byte of the four-byte header must be 0 and the next three
 * bytes contain the length of the remaining data.
 */
int
smb_session_xprt_gethdr(smb_session_t *session, smb_xprt_t *ret_hdr)
{
	int		rc;
	unsigned char	buf[NETBIOS_HDR_SZ];

	if ((rc = smb_sorecv(session->sock, buf, NETBIOS_HDR_SZ)) != 0)
		return (rc);

	switch (session->s_local_port) {
	case IPPORT_NETBIOS_SSN:
		ret_hdr->xh_type = buf[0];
		ret_hdr->xh_length = (((uint32_t)buf[1] & 1) << 16) |
		    ((uint32_t)buf[2] << 8) |
		    ((uint32_t)buf[3]);
		break;

	case IPPORT_SMB:
		ret_hdr->xh_type = buf[0];

		if (ret_hdr->xh_type != 0) {
			cmn_err(CE_WARN, "invalid type (%u)", ret_hdr->xh_type);
			dump_smb_inaddr(&session->ipaddr);
			return (EPROTO);
		}

		ret_hdr->xh_length = ((uint32_t)buf[1] << 16) |
		    ((uint32_t)buf[2] << 8) |
		    ((uint32_t)buf[3]);
		break;

	default:
		cmn_err(CE_WARN, "invalid port %u", session->s_local_port);
		dump_smb_inaddr(&session->ipaddr);
		return (EPROTO);
	}

	return (0);
}

/*
 * Encode a transport session packet header into a 4-byte buffer.
 * See smb_xprt_t definition for header format information.
 */
static int
smb_session_xprt_puthdr(smb_session_t *session, smb_xprt_t *hdr,
    uint8_t *buf, size_t buflen)
{
	if (session == NULL || hdr == NULL ||
	    buf == NULL || buflen < NETBIOS_HDR_SZ) {
		return (-1);
	}

	switch (session->s_local_port) {
	case IPPORT_NETBIOS_SSN:
		buf[0] = hdr->xh_type;
		buf[1] = ((hdr->xh_length >> 16) & 1);
		buf[2] = (hdr->xh_length >> 8) & 0xff;
		buf[3] = hdr->xh_length & 0xff;
		break;

	case IPPORT_SMB:
		buf[0] = hdr->xh_type;
		buf[1] = (hdr->xh_length >> 16) & 0xff;
		buf[2] = (hdr->xh_length >> 8) & 0xff;
		buf[3] = hdr->xh_length & 0xff;
		break;

	default:
		cmn_err(CE_WARN, "invalid port %u", session->s_local_port);
		dump_smb_inaddr(&session->ipaddr);
		return (-1);
	}

	return (0);
}

/*
 * smb_request_cancel
 *
 * Handle a cancel for a request properly depending on the current request
 * state.
 */
void
smb_request_cancel(smb_request_t *sr)
{
	mutex_enter(&sr->sr_mutex);
	switch (sr->sr_state) {
	case SMB_REQ_STATE_INITIALIZING:
	case SMB_REQ_STATE_SUBMITTED:
	case SMB_REQ_STATE_ACTIVE:
	case SMB_REQ_STATE_CLEANED_UP:
		sr->sr_state = SMB_REQ_STATE_CANCELED;
		break;

	case SMB_REQ_STATE_WAITING_LOCK:
		/*
		 * This request is waiting on a lock.  Wakeup everything
		 * waiting on the lock so that the relevant thread regains
		 * control and notices that is has been canceled.  The
		 * other lock request threads waiting on this lock will go
		 * back to sleep when they discover they are still blocked.
		 */
		sr->sr_state = SMB_REQ_STATE_CANCELED;

		ASSERT(sr->sr_awaiting != NULL);
		mutex_enter(&sr->sr_awaiting->l_mutex);
		cv_broadcast(&sr->sr_awaiting->l_cv);
		mutex_exit(&sr->sr_awaiting->l_mutex);
		break;

	case SMB_REQ_STATE_WAITING_EVENT:
	case SMB_REQ_STATE_EVENT_OCCURRED:
		/*
		 * Cancellations for these states are handled by the
		 * notify-change code
		 */
		break;

	case SMB_REQ_STATE_COMPLETED:
	case SMB_REQ_STATE_CANCELED:
		/*
		 * No action required for these states since the request
		 * is completing.
		 */
		break;

	case SMB_REQ_STATE_FREE:
	default:
		SMB_PANIC();
	}
	mutex_exit(&sr->sr_mutex);
}

/*
 * smb_session_receiver
 *
 * Receives request from the network and dispatches them to a worker.
 */
void
smb_session_receiver(smb_session_t *session)
{
	int	rc;

	SMB_SESSION_VALID(session);

	session->s_thread = curthread;

	if (session->s_local_port == IPPORT_NETBIOS_SSN) {
		rc = smb_session_request(session);
		if (rc != 0) {
			smb_rwx_rwenter(&session->s_lock, RW_WRITER);
			session->s_state = SMB_SESSION_STATE_DISCONNECTED;
			smb_rwx_rwexit(&session->s_lock);
			return;
		}
	}

	smb_rwx_rwenter(&session->s_lock, RW_WRITER);
	session->s_state = SMB_SESSION_STATE_ESTABLISHED;
	smb_rwx_rwexit(&session->s_lock);

	(void) smb_session_message(session);

	smb_rwx_rwenter(&session->s_lock, RW_WRITER);
	session->s_state = SMB_SESSION_STATE_DISCONNECTED;
	smb_rwx_rwexit(&session->s_lock);

	smb_soshutdown(session->sock);

	DTRACE_PROBE2(session__drop, struct session *, session, int, rc);

	smb_session_cancel(session);
	/*
	 * At this point everything related to the session should have been
	 * cleaned up and we expect that nothing will attempt to use the
	 * socket.
	 */
}

/*
 * smb_session_disconnect
 *
 * Disconnects the session passed in.
 */
void
smb_session_disconnect(smb_session_t *session)
{
	SMB_SESSION_VALID(session);

	smb_rwx_rwenter(&session->s_lock, RW_WRITER);
	switch (session->s_state) {
	case SMB_SESSION_STATE_INITIALIZED:
	case SMB_SESSION_STATE_CONNECTED:
	case SMB_SESSION_STATE_ESTABLISHED:
	case SMB_SESSION_STATE_NEGOTIATED:
	case SMB_SESSION_STATE_OPLOCK_BREAKING:
	case SMB_SESSION_STATE_WRITE_RAW_ACTIVE:
	case SMB_SESSION_STATE_READ_RAW_ACTIVE:
		smb_soshutdown(session->sock);
		session->s_state = SMB_SESSION_STATE_DISCONNECTED;
		_NOTE(FALLTHRU)
	case SMB_SESSION_STATE_DISCONNECTED:
	case SMB_SESSION_STATE_TERMINATED:
		break;
	}
	smb_rwx_rwexit(&session->s_lock);
}

/*
 * Read and process SMB requests.
 *
 * Returns:
 *	0	Success
 *	1	Unable to read transport header
 *	2	Invalid transport header type
 *	3	Invalid SMB length (too small)
 *	4	Unable to read SMB header
 *	5	Invalid SMB header (bad magic number)
 *	6	Unable to read SMB data
 *	2x	Write raw failed
 */
static int
smb_session_message(smb_session_t *session)
{
	smb_server_t	*sv;
	smb_request_t	*sr = NULL;
	smb_xprt_t	hdr;
	int		rc;

	sv = session->s_server;

	for (;;) {

		rc = smb_session_xprt_gethdr(session, &hdr);
		if (rc != 0)
			break;

		DTRACE_PROBE2(session__receive__xprthdr, session_t *, session,
		    smb_xprt_t *, &hdr);

		if (hdr.xh_type != SESSION_MESSAGE) {
			/*
			 * Anything other than SESSION_MESSAGE or
			 * SESSION_KEEP_ALIVE is an error.  A SESSION_REQUEST
			 * may indicate a new session request but we need to
			 * close this session and we can treat it as an error
			 * here.
			 */
			if (hdr.xh_type == SESSION_KEEP_ALIVE) {
				session->keep_alive = smb_keep_alive;
				continue;
			}
			rc = EPROTO;
			break;
		}

		if (hdr.xh_length < SMB_HEADER_LEN) {
			rc = EPROTO;
			break;
		}

		session->keep_alive = smb_keep_alive;
		/*
		 * Allocate a request context, read the SMB header and validate
		 * it. The sr includes a buffer large enough to hold the SMB
		 * request payload.  If the header looks valid, read any
		 * remaining data.
		 */
		sr = smb_request_alloc(session, hdr.xh_length);

		rc = mbc_sorecv(&sr->command, session->sock);
		if (rc != 0)
			break;

		smb_server_add_rxb(sv,
		    (int64_t)(hdr.xh_length + NETBIOS_HDR_SZ));

		DTRACE_PROBE1(session__receive__smb, smb_request_t *, sr);

		if (session->signing.flags & SMB_SIGNING_ENABLED) {
			rc = mbc_duplicate(&sr->command, &sr->sr_command_dup);
			if (rc != 0)
				break;
		}

		rc = smb_mbc_decodef(&sr->command, "H", &sr->sr_header);
		if (rc != 0)
			break;

		switch (sr->sr_command) {
		case SMB_COM_WRITE_RAW:
			rc = smb_handle_write_raw(session, sr);
			sr = NULL;
			break;

		case SMB_COM_NT_CANCEL:
			session->signing.seqnum++;
			sr->sr_seqnum = session->signing.seqnum + 1;
			sr->reply_seqnum = 0;
			break;

		default:
			session->signing.seqnum += 2;
			sr->sr_seqnum = sr->session->signing.seqnum;
			sr->reply_seqnum = sr->sr_seqnum + 1;
			break;
		}

		if (rc != 0)
			break;

		if (sr != NULL) {
			sr->sr_time_submitted = gethrtime();
			sr->sr_state = SMB_REQ_STATE_SUBMITTED;
			smb_srqueue_waitq_enter(session->s_srqueue);
			(void) taskq_dispatch(session->s_server->sv_worker_pool,
			    smb_session_worker, sr, TQ_SLEEP);
			sr = NULL;
		}
	}

	if (sr != NULL)
		smb_request_free(sr);

	return (rc);
}

/*
 * Port will be IPPORT_NETBIOS_SSN or IPPORT_SMB.
 */
smb_session_t *
smb_session_create(ksocket_t new_so, uint16_t port, smb_server_t *sv,
    int family)
{
	struct sockaddr_in	sin;
	socklen_t		slen;
	struct sockaddr_in6	sin6;
	smb_session_t		*session;
	int64_t			now;

	session = kmem_cache_alloc(sv->si_cache_session, KM_SLEEP);
	bzero(session, sizeof (smb_session_t));

	if (smb_idpool_constructor(&session->s_uid_pool)) {
		kmem_cache_free(sv->si_cache_session, session);
		return (NULL);
	}

	now = ddi_get_lbolt64();

	session->s_kid = SMB_NEW_KID();
	session->s_state = SMB_SESSION_STATE_INITIALIZED;
	session->native_os = NATIVE_OS_WIN2000;
	session->opentime = now;
	session->keep_alive = smb_keep_alive;
	session->activity_timestamp = now;

	smb_slist_constructor(&session->s_req_list, sizeof (smb_request_t),
	    offsetof(smb_request_t, sr_session_lnd));

	smb_llist_constructor(&session->s_auth_list, sizeof (smb_user_t),
	    offsetof(smb_user_t, u_lnd));

	smb_llist_constructor(&session->s_user_list, sizeof (smb_user_t),
	    offsetof(smb_user_t, u_lnd));

	smb_llist_constructor(&session->s_xa_list, sizeof (smb_xa_t),
	    offsetof(smb_xa_t, xa_lnd));

	list_create(&session->s_oplock_brkreqs, sizeof (mbuf_chain_t),
	    offsetof(mbuf_chain_t, mbc_lnd));

	smb_net_txl_constructor(&session->s_txlst);

	smb_rwx_init(&session->s_lock);

	if (new_so != NULL) {
		if (family == AF_INET) {
			slen = sizeof (sin);
			(void) ksocket_getsockname(new_so,
			    (struct sockaddr *)&sin, &slen, CRED());
			bcopy(&sin.sin_addr,
			    &session->local_ipaddr.au_addr.au_ipv4,
			    sizeof (in_addr_t));
			slen = sizeof (sin);
			(void) ksocket_getpeername(new_so,
			    (struct sockaddr *)&sin, &slen, CRED());
			bcopy(&sin.sin_addr,
			    &session->ipaddr.au_addr.au_ipv4,
			    sizeof (in_addr_t));
		} else {
			slen = sizeof (sin6);
			(void) ksocket_getsockname(new_so,
			    (struct sockaddr *)&sin6, &slen, CRED());
			bcopy(&sin6.sin6_addr,
			    &session->local_ipaddr.au_addr.au_ipv6,
			    sizeof (in6_addr_t));
			slen = sizeof (sin6);
			(void) ksocket_getpeername(new_so,
			    (struct sockaddr *)&sin6, &slen, CRED());
			bcopy(&sin6.sin6_addr,
			    &session->ipaddr.au_addr.au_ipv6,
			    sizeof (in6_addr_t));
		}
		session->ipaddr.a_family = family;
		session->local_ipaddr.a_family = family;
		session->s_local_port = port;
		session->sock = new_so;
		if (port == IPPORT_NETBIOS_SSN)
			smb_server_inc_nbt_sess(sv);
		else
			smb_server_inc_tcp_sess(sv);
	}
	session->s_server = sv;
	smb_server_get_cfg(sv, &session->s_cfg);
	session->s_srqueue = &sv->sv_srqueue;

	session->s_cache_request = sv->si_cache_request;
	session->s_cache = sv->si_cache_session;
	session->s_magic = SMB_SESSION_MAGIC;
	return (session);
}

void
smb_session_delete(smb_session_t *session)
{
	mbuf_chain_t	*mbc;

	ASSERT(session->s_magic == SMB_SESSION_MAGIC);

	session->s_magic = 0;

	smb_session_destroy_upcall(session);

	smb_rwx_destroy(&session->s_lock);
	smb_net_txl_destructor(&session->s_txlst);

	while ((mbc = list_head(&session->s_oplock_brkreqs)) != NULL) {
		SMB_MBC_VALID(mbc);
		list_remove(&session->s_oplock_brkreqs, mbc);
		smb_mbc_free(mbc);
	}
	list_destroy(&session->s_oplock_brkreqs);

	smb_slist_destructor(&session->s_req_list);
	smb_llist_destructor(&session->s_auth_list);
	smb_llist_destructor(&session->s_user_list);
	smb_llist_destructor(&session->s_xa_list);

	ASSERT(session->s_tree_cnt == 0);
	ASSERT(session->s_file_cnt == 0);
	ASSERT(session->s_dir_cnt == 0);

	smb_idpool_destructor(&session->s_uid_pool);
	if (session->sock != NULL) {
		if (session->s_local_port == IPPORT_NETBIOS_SSN)
			smb_server_dec_nbt_sess(session->s_server);
		else
			smb_server_dec_tcp_sess(session->s_server);
		smb_sodestroy(session->sock);
	}
	kmem_cache_free(session->s_cache, session);
}

static void
smb_session_cancel(smb_session_t *session)
{
	smb_xa_t	*xa, *nextxa;

	/* All the request currently being treated must be canceled. */
	smb_session_cancel_requests(session, NULL, NULL);

	/*
	 * We wait for the completion of all the requests associated with
	 * this session.
	 */
	smb_slist_wait_for_empty(&session->s_req_list);

	/*
	 * At this point the reference count of the users, trees, files,
	 * directories should be zero. It should be possible to destroy them
	 * without any problem.
	 */
	xa = smb_llist_head(&session->s_xa_list);
	while (xa) {
		nextxa = smb_llist_next(&session->s_xa_list, xa);
		smb_xa_close(xa);
		xa = nextxa;
	}

	(void) smb_session_logoff(session, NULL);
	smb_session_auth_cancel(session, SMB_UID_RESERVED);
}

/*
 * Cancel requests.  If a non-null tree is specified, only requests specific
 * to that tree will be cancelled.  If a non-null sr is specified, that sr
 * will be not be cancelled - this would typically be the caller's sr.
 */
void
smb_session_cancel_requests(
    smb_session_t	*session,
    smb_tree_t		*tree,
    smb_request_t	*exclude_sr)
{
	smb_request_t	*sr;

	/* Respond to any pending notify change requests */
	smb_fcn_notify_session(session, tree);

	smb_slist_enter(&session->s_req_list);
	sr = smb_slist_head(&session->s_req_list);

	while (sr) {
		ASSERT(sr->sr_magic == SMB_REQ_MAGIC);
		if ((sr != exclude_sr) &&
		    (tree == NULL || sr->tid_tree == tree))
			smb_request_cancel(sr);

		sr = smb_slist_next(&session->s_req_list, sr);
	}

	smb_slist_exit(&session->s_req_list);
}

void
smb_session_worker(void	*arg)
{
	smb_request_t	*sr;
	smb_srqueue_t	*srq;
	smb_thsd_t	thsd;

	sr = (smb_request_t *)arg;
	SMB_REQ_VALID(sr);

	smb_server_settsd(&thsd);
	srq = sr->session->s_srqueue;
	smb_srqueue_waitq_to_runq(srq);
	sr->sr_worker = curthread;
	mutex_enter(&sr->sr_mutex);
	sr->sr_time_active = gethrtime();
	switch (sr->sr_state) {
	case SMB_REQ_STATE_SUBMITTED:
		mutex_exit(&sr->sr_mutex);
		if (smb_dispatch_request(sr)) {
			mutex_enter(&sr->sr_mutex);
			sr->sr_state = SMB_REQ_STATE_COMPLETED;
			mutex_exit(&sr->sr_mutex);
			smb_request_free(sr);
		}
		break;

	default:
		ASSERT(sr->sr_state == SMB_REQ_STATE_CANCELED);
		sr->sr_state = SMB_REQ_STATE_COMPLETED;
		mutex_exit(&sr->sr_mutex);
		smb_request_free(sr);
		break;
	}
	smb_srqueue_runq_exit(srq);
	smb_server_settsd(NULL);
}

/*
 * Create an smb_user_t object and add it to the session's
 * list of authenticating users.
 */
uint16_t
smb_session_create_user(smb_session_t *session)
{
	smb_user_t	*user;

	SMB_SESSION_VALID(session);

	user = smb_user_alloc(session);
	if (user == NULL)
		return (0);

	smb_llist_enter(&session->s_auth_list, RW_WRITER);
	smb_llist_insert_tail(&session->s_auth_list, user);
	smb_llist_exit(&session->s_auth_list);

	return (user->u_uid);
}

/*
 * Find the specified user (authreq->au_user_id) in the authenticating
 * users list and authenticate the user. When authentication is complete,
 * remove the user from the authenticating users list.
 *
 * Upon successful return (0), the authrsp will have been populated with
 * authentication response data. The caller is responsible for subsequently
 * calling smb_session_authrsp_free() to free that data.
 */
int
smb_session_authenticate_user(smb_request_t *sr, smb_session_t *session,
    smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	smb_user_t	*user;
	smb_llist_t	*auth_users;
	int		rc = 0;

	SMB_SESSION_VALID(session);

	auth_users = &session->s_auth_list;
	smb_llist_enter(auth_users, RW_WRITER);
	user = smb_llist_head(auth_users);

	while (user) {
		if (user->u_uid == authreq->au_user_id)
			break;
		user = smb_llist_next(auth_users, user);
	}

	if (user == NULL) {
		cmn_err(CE_NOTE, "Authenticate User [uid=%d]: invalid user id",
		    authreq->au_user_id);

		smb_llist_exit(auth_users);
		smb_errcode_seterror(0, ERRSRV, ERRbaduid);
		return (-1);
	}

	/*
	 * secblob is checked here because it needs to be checked after
	 * the UID lookup in order for the correct error status to be
	 * returned in the event that both are invalid.
	 */
	if ((authreq->au_flags & SMB_AUTH_SECBLOB) &&
	    (authreq->au_secblob.len == 0)) {
		smb_errcode_seterror(NT_STATUS_INVALID_PARAMETER,
		    ERRDOS, ERROR_INVALID_PARAMETER);
		smb_llist_remove(&session->s_auth_list, user);
		smb_llist_exit(auth_users);
		smb_user_delete(user);
		return (-1);
	}

	rc = smb_session_auth_user(session, authreq, authrsp);
	if (rc != 0) {
		/* door call mechanism failure. authrsp not populated */
		smb_errcode_seterror(NT_STATUS_NETLOGON_NOT_STARTED,
		    ERRDOS, ERROR_NETLOGON_NOT_STARTED);
		smb_llist_remove(&session->s_auth_list, user);
		smb_llist_exit(auth_users);
		smb_user_delete(user);
		return (-1);
	}

	if ((sr->sr_cfg->skc_extsec_enable == 0) ||
	    (authrsp->ar_status != NT_STATUS_MORE_PROCESSING_REQUIRED))
		smb_llist_remove(&session->s_auth_list, user);

	smb_llist_exit(auth_users);

	switch (authrsp->ar_status) {
	case NT_STATUS_SUCCESS:
		rc = smb_session_add_user(sr, session, user, authrsp->ar_token);
		if (rc != 0) {
			smb_session_authrsp_free(authrsp);
			smb_user_delete(user);
			return (-1);
		}
		break;
	case NT_STATUS_MORE_PROCESSING_REQUIRED:
		smb_errcode_seterror(NT_STATUS_MORE_PROCESSING_REQUIRED, 0, 0);
		if (sr->sr_cfg->skc_extsec_enable == 0) {
			cmn_err(CE_NOTE, "Extended security disable: "
			    "Unexpected STATUS_MORE_PROCESSING_REQUIRED");
			smb_errcode_seterror(NT_STATUS_INTERNAL_ERROR,
			    ERRDOS, ERROR_INTERNAL_ERROR);
			smb_session_authrsp_free(authrsp);
			smb_user_delete(user);
			return (-1);
		}
		break;
	default:
		smb_errcode_seterror(authrsp->ar_status, 0, 0);
		smb_session_authrsp_free(authrsp);
		smb_user_delete(user);
		return (-1);
	}

	authreq->au_guest = SMB_USER_IS_GUEST(user);
	return (0);
}

/*
 * The user information is passed to smbd for authentication.
 */
static uint32_t
smb_session_auth_user(smb_session_t *session,
    smb_authreq_t *authreq, smb_authrsp_t *authrsp)
{
	uint32_t	status;

	authreq->au_level = NETR_NETWORK_LOGON;
	authreq->au_session_id = session->s_kid;
	authreq->au_workstation = session->workstation;
	authreq->au_clnt_ipaddr = session->ipaddr;
	authreq->au_local_ipaddr = session->local_ipaddr;
	authreq->au_local_port = session->s_local_port;
	authreq->au_challenge_key.val = session->challenge_key;
	authreq->au_challenge_key.len = session->challenge_len;

	DTRACE_PROBE1(smb__sessionsetup__clntinfo, smb_authreq_t *, authreq);

	status = smb_user_auth_upcall(authreq, authrsp);
	return (status);
}

/*
 * If smbd can authenticate the user, the access token returned
 * is used to update the user info/state (smb_user_logon).
 * The user is then added to the session's user list: s_user_list.
 */
static int
smb_session_add_user(smb_request_t *sr, smb_session_t *session,
    smb_user_t *user, smb_token_t *token)
{
	if (smb_user_logon(user, token) != 0)
		return (-1);

	(void) smb_user_hold(user);
	smb_llist_enter(&session->s_user_list, RW_WRITER);
	smb_llist_insert_tail(&session->s_user_list, user);
	smb_llist_exit(&session->s_user_list);
	smb_server_inc_users(session->s_server);
	sr->uid_user = user;
	sr->user_cr = user->u_cred;
	return (0);
}

/*
 * Free the internals of the authrsp (allocated by xdr) and zero the
 * authrsp structure.
 */
void
smb_session_authrsp_free(smb_authrsp_t *authrsp)
{
	xdr_free(smb_authrsp_xdr, (char *)authrsp);
	bzero(authrsp, sizeof (smb_authrsp_t));
}

/*
 * Remove (and delete) specified user from session's authenticating user list.
 * Call smb_user_logoff_upcall() to discard associated userspace context.
 * A uid of SMB_UID_RESERVED means ALL users.
 */
void
smb_session_auth_cancel(smb_session_t *session, uint16_t uid)
{
	smb_user_t	*user, *next;
	smb_llist_t	*auth_users;

	SMB_SESSION_VALID(session);

	auth_users = &session->s_auth_list;

	smb_llist_enter(auth_users, RW_WRITER);
	user = smb_llist_head(auth_users);

	while (user) {
		next = smb_llist_next(auth_users, user);
		if ((uid == SMB_UID_RESERVED) || (user->u_uid == uid)) {
			smb_llist_remove(&session->s_auth_list, user);
			smb_user_logoff_upcall(user);
			smb_user_delete(user);

			if (uid != SMB_UID_RESERVED)
				break;
		}
		user = next;
	}

	smb_llist_exit(auth_users);
}

boolean_t
smb_session_auth_in_progress(smb_session_t *session, uint16_t uid)
{
	smb_user_t	*user;
	smb_llist_t	*auth_users;

	SMB_SESSION_VALID(session);

	auth_users = &session->s_auth_list;
	smb_llist_enter(auth_users, RW_READER);
	user = smb_llist_head(auth_users);

	while (user) {
		SMB_USER_VALID(user);
		ASSERT(user->u_session == session);
		if (user->u_uid == uid)
			break;
		user = smb_llist_next(auth_users, user);
	}

	smb_llist_exit(auth_users);
	return (user != NULL);
}

/*
 * Find a user on the specified session by SMB UID.
 */
smb_user_t *
smb_session_lookup_user(smb_session_t *session, uint16_t uid)
{
	smb_user_t	*user;
	smb_llist_t	*user_list;

	SMB_SESSION_VALID(session);

	user_list = &session->s_user_list;
	smb_llist_enter(user_list, RW_READER);

	user = smb_llist_head(user_list);
	while (user) {
		SMB_USER_VALID(user);
		ASSERT(user->u_session == session);

		if (user->u_uid == uid) {
			if (!smb_user_hold(user))
				break;

			smb_llist_exit(user_list);
			return (user);
		}

		user = smb_llist_next(user_list, user);
	}

	smb_llist_exit(user_list);
	return (NULL);
}

void
smb_session_post_user(smb_session_t *session, smb_user_t *user)
{
	SMB_USER_VALID(user);
	ASSERT(user->u_refcnt == 0);
	ASSERT(user->u_state == SMB_USER_STATE_LOGGED_OFF);
	ASSERT(user->u_session == session);

	smb_llist_post(&session->s_user_list, user, smb_session_delete_user);
}

/*
 * Remove user object from the session's user list and delete it.
 */
static void
smb_session_delete_user(void *arg)
{
	smb_user_t	*user = (smb_user_t *)arg;
	smb_session_t	*session;

	SMB_USER_VALID(user);
	session = user->u_session;

	smb_llist_enter(&session->s_user_list, RW_WRITER);
	smb_llist_remove(&session->s_user_list, user);
	smb_llist_exit(&session->s_user_list);
	smb_user_delete(user);
}

/*
 * Logoff user(s) associated with the specified session.
 * If name is specified, logoff the user(s) matching name.
 * Otherwise logoff all users.
 * Returns: number of users logged off.
 */
int
smb_session_logoff(smb_session_t *session, const char *name)
{
	smb_user_t	*user;
	int		count = 0;

	SMB_SESSION_VALID(session);

	smb_llist_enter(&session->s_user_list, RW_READER);

	user = smb_llist_head(&session->s_user_list);
	while (user) {
		SMB_USER_VALID(user);
		ASSERT(user->u_session == session);

		if (smb_user_hold(user)) {
			if ((name == NULL) || (*name == '\0') ||
			    smb_user_namecmp(user, name)) {
				smb_user_logoff(user);
				++count;
			}
			smb_user_release(user);
		}

		user = smb_llist_next(&session->s_user_list, user);
	}

	smb_llist_exit(&session->s_user_list);
	return (count);
}

/*
 * Logoff the user identified by specified uid.
 *
 * Returns: 0 - user found, logged off
 *         -1 - no such user logged on
 */
int
smb_session_logoff_user(smb_session_t *session, uint16_t uid)
{
	smb_user_t	*user;

	SMB_SESSION_VALID(session);

	smb_llist_enter(&session->s_user_list, RW_READER);

	user = smb_llist_head(&session->s_user_list);
	while (user) {
		SMB_USER_VALID(user);
		ASSERT(user->u_session == session);

		if (smb_user_hold(user)) {
			if (user->u_uid == uid) {
				smb_user_logoff(user);
				smb_user_release(user);
				smb_llist_exit(&session->s_user_list);
				return (0);
			}
			smb_user_release(user);
		}

		user = smb_llist_next(&session->s_user_list, user);
	}

	smb_llist_exit(&session->s_user_list);
	return (-1);
}

/*
 * Disconnect any trees associated with the specified share.
 * Iterate through the users on this session and tell each user
 * to disconnect from the share.
 */
void
smb_session_disconnect_share(smb_session_t *session, uint64_t share_id)
{
	smb_user_t	*user;

	SMB_SESSION_VALID(session);

	smb_llist_enter(&session->s_user_list, RW_READER);

	user = smb_llist_head(&session->s_user_list);
	while (user) {
		SMB_USER_VALID(user);
		ASSERT(user->u_session == session);

		if (smb_user_hold(user)) {
			smb_user_disconnect_share(user, share_id);
			smb_user_release(user);
		}

		user = smb_llist_next(&session->s_user_list, user);
	}

	smb_llist_exit(&session->s_user_list);
}

/*
 * Copy the session workstation/client name to buf.  If the workstation
 * is an empty string (which it will be on TCP connections), use the
 * client IP address.
 */
void
smb_session_getclient(smb_session_t *sn, char *buf, size_t buflen)
{
	char		ipbuf[INET6_ADDRSTRLEN];
	smb_inaddr_t	*ipaddr;

	ASSERT(sn);
	ASSERT(buf);
	ASSERT(buflen);

	*buf = '\0';

	if (sn->workstation[0] != '\0') {
		(void) strlcpy(buf, sn->workstation, buflen);
		return;
	}

	ipaddr = &sn->ipaddr;
	if (smb_inet_ntop(ipaddr, ipbuf, SMB_IPSTRLEN(ipaddr->a_family)))
		(void) strlcpy(buf, ipbuf, buflen);
}

/*
 * Check whether or not the specified client name is the client of this
 * session.  The name may be in UNC format (\\CLIENT).
 *
 * A workstation/client name is setup on NBT connections as part of the
 * NetBIOS session request but that isn't available on TCP connections.
 * If the session doesn't have a client name we typically return the
 * client IP address as the workstation name on MSRPC requests.  So we
 * check for the IP address here in addition to the workstation name.
 */
boolean_t
smb_session_isclient(smb_session_t *sn, const char *client)
{
	char		buf[INET6_ADDRSTRLEN];
	smb_inaddr_t	*ipaddr;

	client += strspn(client, "\\");

	if (smb_strcasecmp(client, sn->workstation, 0) == 0)
		return (B_TRUE);

	ipaddr = &sn->ipaddr;
	if (smb_inet_ntop(ipaddr, buf, SMB_IPSTRLEN(ipaddr->a_family)) == NULL)
		return (B_FALSE);

	if (smb_strcasecmp(client, buf, 0) == 0)
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * smb_request_alloc
 *
 * Allocate an smb_request_t structure from the kmem_cache.  Partially
 * initialize the found/new request.
 *
 * Returns pointer to a request
 */
smb_request_t *
smb_request_alloc(smb_session_t *session, int req_length)
{
	smb_request_t	*sr;

	ASSERT(session->s_magic == SMB_SESSION_MAGIC);

	sr = kmem_cache_alloc(session->s_cache_request, KM_SLEEP);

	/*
	 * Future:  Use constructor to pre-initialize some fields.  For now
	 * there are so many fields that it is easiest just to zero the
	 * whole thing and start over.
	 */
	bzero(sr, sizeof (smb_request_t));

	mutex_init(&sr->sr_mutex, NULL, MUTEX_DEFAULT, NULL);
	smb_srm_init(sr);
	sr->session = session;
	sr->sr_server = session->s_server;
	sr->sr_gmtoff = session->s_server->si_gmtoff;
	sr->sr_cache = session->s_server->si_cache_request;
	sr->sr_cfg = &session->s_cfg;
	MBC_SETUP(&sr->command, req_length);
	MBC_SETUP(&sr->reply, smb_maxbufsize);
	sr->sr_magic = SMB_REQ_MAGIC;
	sr->sr_state = SMB_REQ_STATE_INITIALIZING;
	smb_slist_insert_tail(&session->s_req_list, sr);
	return (sr);
}

/*
 * smb_request_free
 *
 * release the memories which have been allocated for a smb request.
 */
void
smb_request_free(smb_request_t *sr)
{
	ASSERT(sr->sr_magic == SMB_REQ_MAGIC);
	ASSERT(sr->session);
	ASSERT(sr->sr_xa == NULL);

	if (sr->fid_ofile != NULL) {
		smb_ofile_request_complete(sr->fid_ofile);
		smb_ofile_release(sr->fid_ofile);
	}

	if (sr->tid_tree != NULL)
		smb_tree_release(sr->tid_tree);

	if (sr->uid_user != NULL)
		smb_user_release(sr->uid_user);

	smb_slist_remove(&sr->session->s_req_list, sr);

	sr->session = NULL;

	smb_srm_fini(sr);

	if (sr->command.chain)
		m_freem(sr->command.chain);
	if (sr->reply.chain)
		m_freem(sr->reply.chain);
	if (sr->raw_data.chain)
		m_freem(sr->raw_data.chain);

	sr->sr_magic = 0;
	mutex_destroy(&sr->sr_mutex);
	kmem_cache_free(sr->sr_cache, sr);
}

void
dump_smb_inaddr(smb_inaddr_t *ipaddr)
{
	char ipstr[INET6_ADDRSTRLEN];

	if (smb_inet_ntop(ipaddr, ipstr, SMB_IPSTRLEN(ipaddr->a_family)))
		cmn_err(CE_WARN, "error ipstr=%s", ipstr);
	else
		cmn_err(CE_WARN, "error converting ip address");
}

boolean_t
smb_session_oplocks_enable(smb_session_t *session)
{
	SMB_SESSION_VALID(session);
	if (session->s_cfg.skc_oplock_enable == 0)
		return (B_FALSE);
	else
		return (B_TRUE);
}

boolean_t
smb_session_levelII_oplocks(smb_session_t *session)
{
	SMB_SESSION_VALID(session);
	return (session->capabilities & CAP_LEVEL_II_OPLOCKS);
}

/*
 * smb_session_oplock_break
 *
 * The session lock must NOT be held by the caller of this thread;
 * as this would cause a deadlock.
 */
void
smb_session_oplock_break(smb_session_t *session,
    uint16_t tid, uint16_t fid, uint8_t brk)
{
	mbuf_chain_t	*mbc;

	SMB_SESSION_VALID(session);

	mbc = smb_mbc_alloc(MLEN);

	(void) smb_mbc_encodef(mbc, "Mb19.wwwwbb3.wbb10.",
	    SMB_COM_LOCKING_ANDX,
	    tid,
	    0xFFFF, 0, 0xFFFF, 8, 0xFF,
	    fid,
	    LOCKING_ANDX_OPLOCK_RELEASE,
	    (brk == SMB_OPLOCK_BREAK_TO_LEVEL_II) ? 1 : 0);

	smb_rwx_rwenter(&session->s_lock, RW_WRITER);
	switch (session->s_state) {
	case SMB_SESSION_STATE_NEGOTIATED:
	case SMB_SESSION_STATE_OPLOCK_BREAKING:
	case SMB_SESSION_STATE_WRITE_RAW_ACTIVE:
		session->s_state = SMB_SESSION_STATE_OPLOCK_BREAKING;
		(void) smb_session_send(session, 0, mbc);
		smb_mbc_free(mbc);
		break;

	case SMB_SESSION_STATE_READ_RAW_ACTIVE:
		list_insert_tail(&session->s_oplock_brkreqs, mbc);
		break;

	case SMB_SESSION_STATE_DISCONNECTED:
	case SMB_SESSION_STATE_TERMINATED:
		smb_mbc_free(mbc);
		break;

	default:
		SMB_PANIC();
	}
	smb_rwx_rwexit(&session->s_lock);
}

uint32_t
smb_session_create_upcall(smb_sessionreq_t *request, smb_sessionrsp_t *response)
{
	uint32_t status;

	status = smb_kdoor_upcall(SMB_DR_SESSION_CREATE,
	    request, smb_sessionreq_xdr, response, smb_sessionrsp_xdr);

	return (status);
}

void
smb_session_destroy_upcall(smb_session_t *session)
{
	(void) smb_kdoor_upcall(SMB_DR_SESSION_DESTROY,
	    &session->s_kid, xdr_uint64_t, NULL, NULL);
}
