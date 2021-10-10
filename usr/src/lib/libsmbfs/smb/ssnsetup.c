/*
 * Copyright (c) 2000-2001 Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * SMB Session Setup, and related.
 * Copied from the driver: smb_smb.c
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <netdb.h>
#include <libintl.h>
#include <xti.h>
#include <assert.h>
#include <syslog.h>

#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/byteorder.h>
#include <sys/socket.h>
#include <sys/fcntl.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <netsmb/mchain.h>
#include <netsmb/netbios.h>
#include <netsmb/smb_dev.h>
#include <smb/smb.h>

#include <netsmb/smb_lib.h>
#include <netsmb/nb_lib.h>

#include "private.h"
#include "charsets.h"
#include "ntlm.h"
#include "smb_crypt.h"

#define	NATIVE_OS_BUFSIZE	24

static int smb__ssnsetup(struct smb_ctx *, struct mbdata *, struct mbdata *,
    uint32_t *, uint16_t *);
static void smb_ssnsetup_lookup_os(char *, size_t);

/*
 * Session Setup: NULL session (anonymous)
 */
int
smb_ssnsetup_null(struct smb_ctx *ctx)
{
	int err;
	uint32_t ntstatus;
	uint16_t action = 0;

	if (ctx->ct_sopt.sv_caps & CAP_EXTENDED_SECURITY) {
		/* Should not get here with... */
		err = EINVAL;
		goto out;
	}

	err = smb__ssnsetup(ctx, NULL, NULL, &ntstatus, &action);
	if (err)
		goto out;

	DPRINT("status 0x%x action 0x%x", ntstatus, (int)action);
	if (ntstatus != 0)
		err = EAUTH;

out:
	return (err);
}


/*
 * SMB Session Setup, using NTLMv1 (and maybe LMv1)
 */
int
smb_ssnsetup_ntlm1(struct smb_ctx *ctx)
{
	struct mbdata lm_mbc, nt_mbc;
	int err;
	uint32_t ntstatus;
	uint16_t action = 0;

	if (ctx->ct_sopt.sv_caps & CAP_EXTENDED_SECURITY) {
		/* Should not get here with... */
		err = EINVAL;
		goto out;
	}

	/* Make mb_done calls at out safe. */
	bzero(&lm_mbc, sizeof (lm_mbc));
	bzero(&nt_mbc, sizeof (nt_mbc));

	/* Put the LM,NTLM responses (as mbdata). */
	err = ntlm_put_v1_responses(ctx, &lm_mbc, &nt_mbc);
	if (err)
		goto out;

	/*
	 * If we negotiated signing, compute the MAC key
	 * and start signing messages, but only on the
	 * first non-null session login.
	 */
	if ((ctx->ct_vcflags & SMBV_WILL_SIGN) &&
	    (ctx->ct_hflags2 & SMB_FLAGS2_SMB_SECURITY_SIGNATURE) == 0) {
		struct mbuf *m = nt_mbc.mb_top;
		char *p;

		/*
		 * MAC_key = concat(session_key, nt_response)
		 */
		ctx->ct_mackeylen = NTLM_HASH_SZ + m->m_len;
		ctx->ct_mackey = malloc(ctx->ct_mackeylen);
		if (ctx->ct_mackey == NULL) {
			ctx->ct_mackeylen = 0;
			err = ENOMEM;
			goto out;
		}
		p = ctx->ct_mackey;
		memcpy(p, ctx->ct_ssn_key, NTLM_HASH_SZ);
		memcpy(p + NTLM_HASH_SZ, m->m_data, m->m_len);

		/* OK, start signing! */
		ctx->ct_hflags2 |= SMB_FLAGS2_SMB_SECURITY_SIGNATURE;
	}

	err = smb__ssnsetup(ctx, &lm_mbc, &nt_mbc, &ntstatus, &action);
	if (err)
		goto out;

	DPRINT("status 0x%x action 0x%x", ntstatus, (int)action);
	if (ntstatus != 0)
		err = EAUTH;

out:
	mb_done(&lm_mbc);
	mb_done(&nt_mbc);

	return (err);
}

/*
 * SMB Session Setup, using NTLMv2 (and LMv2)
 */
int
smb_ssnsetup_ntlm2(struct smb_ctx *ctx)
{
	struct mbdata lm_mbc, nt_mbc, ti_mbc;
	int err;
	uint32_t ntstatus;
	uint16_t action = 0;

	if (ctx->ct_sopt.sv_caps & CAP_EXTENDED_SECURITY) {
		/* Should not get here with... */
		err = EINVAL;
		goto out;
	}

	/* Make mb_done calls at out safe. */
	bzero(&lm_mbc, sizeof (lm_mbc));
	bzero(&nt_mbc, sizeof (nt_mbc));
	bzero(&ti_mbc, sizeof (ti_mbc));

	/* Build the NTLMv2 "target info" blob (as mbdata) */
	err = ntlm_build_target_info(ctx, NULL, &ti_mbc);
	if (err)
		goto out;

	/* Put the LMv2, NTLMv2 responses (as mbdata). */
	err = ntlm_put_v2_responses(ctx, &ti_mbc, &lm_mbc, &nt_mbc);
	if (err)
		goto out;

	/*
	 * If we negotiated signing, compute the MAC key
	 * and start signing messages, but only on the
	 * first non-null session login.
	 */
	if ((ctx->ct_vcflags & SMBV_WILL_SIGN) &&
	    (ctx->ct_hflags2 & SMB_FLAGS2_SMB_SECURITY_SIGNATURE) == 0) {
		struct mbuf *m = nt_mbc.mb_top;
		char *p;

		/*
		 * MAC_key = concat(session_key, nt_response)
		 */
		ctx->ct_mackeylen = NTLM_HASH_SZ + m->m_len;
		ctx->ct_mackey = malloc(ctx->ct_mackeylen);
		if (ctx->ct_mackey == NULL) {
			ctx->ct_mackeylen = 0;
			err = ENOMEM;
			goto out;
		}
		p = ctx->ct_mackey;
		memcpy(p, ctx->ct_ssn_key, NTLM_HASH_SZ);
		memcpy(p + NTLM_HASH_SZ, m->m_data, m->m_len);

		/* OK, start signing! */
		ctx->ct_hflags2 |= SMB_FLAGS2_SMB_SECURITY_SIGNATURE;
	}

	err = smb__ssnsetup(ctx, &lm_mbc, &nt_mbc, &ntstatus, &action);
	if (err)
		goto out;

	DPRINT("status 0x%x action 0x%x", ntstatus, (int)action);
	if (ntstatus != 0)
		err = EAUTH;

out:
	mb_done(&ti_mbc);
	mb_done(&lm_mbc);
	mb_done(&nt_mbc);

	return (err);
}

int
smb_ssnsetup_spnego(struct smb_ctx *ctx, struct mbdata *hint_mb)
{
	struct mbdata send_mb, recv_mb;
	int		err;
	uint32_t	ntstatus;
	uint16_t	action = 0;

	err = ssp_ctx_create_client(ctx, hint_mb);
	if (err)
		goto out;

	bzero(&send_mb, sizeof (send_mb));
	bzero(&recv_mb, sizeof (recv_mb));

	/* NULL input indicates first call. */
	err = ssp_ctx_next_token(ctx, NULL, &send_mb);
	if (err)
		goto out;

	for (;;) {
		err = smb__ssnsetup(ctx, &send_mb, &recv_mb,
		    &ntstatus, &action);
		if (err)
			goto out;
		if (ntstatus == 0)
			break; /* normal loop termination */
		if (ntstatus != NT_STATUS_MORE_PROCESSING_REQUIRED) {
			err = EAUTH;
			goto out;
		}

		/* middle calls get both in, out */
		err = ssp_ctx_next_token(ctx, &recv_mb, &send_mb);
		if (err)
			goto out;
	}
	DPRINT("status 0x%x action 0x%x", ntstatus, (int)action);

	/* NULL output indicates last call. */
	(void) ssp_ctx_next_token(ctx, &recv_mb, NULL);

out:
	ssp_ctx_destroy(ctx);

	return (err);
}

/*
 * Session Setup function used for all the forms we support.
 * To allow this sharing, the crypto stuff is computed by
 * callers and passed in as mbdata chains.  Also, the args
 * have different meanings for extended security vs. old.
 * Some may be used as either IN or OUT parameters.
 *
 * For NTLM (v1, v2), all parameters are inputs
 * 	mbc1: [in] LM password hash
 * 	mbc2: [in] NT password hash
 * For Extended security (spnego)
 *	mbc1: [in]  outgoing blob data
 *	mbc2: [out] received blob data
 * For both forms, these are optional:
 *	statusp: [out] NT status
 *	actionp: [out] Logon Action (i.e. guest)
 */
static int
smb__ssnsetup(struct smb_ctx *ctx,
	struct mbdata *mbc1, struct mbdata *mbc2,
	uint32_t *statusp, uint16_t *actionp)
{
	static boolean_t reported = B_FALSE;
	static const char LanMan[] = "NETSMB";
	char NativeOS[NATIVE_OS_BUFSIZE];
	struct smb_sopt *sv = &ctx->ct_sopt;
	struct smb_iods *is = &ctx->ct_iods;
	struct smb_rq	*rqp = NULL;
	struct mbdata	*mbp;
	struct mbuf	*m;
	int err, uc;
	uint32_t caps;
	uint16_t bc, len1, len2, sblen;
	uint8_t wc;

	smb_ssnsetup_lookup_os(NativeOS, NATIVE_OS_BUFSIZE);

	/*
	 * Some of the "capability" bits we offer will be copied
	 * from those offered by the server, with a mask applied.
	 * This is the mask of capabilies copied from the server.
	 * Some others get special handling below.
	 */
	static const uint32_t caps_mask =
	    CAP_UNICODE |
	    CAP_LARGE_FILES |
	    CAP_NT_SMBS |
	    CAP_STATUS32 |
	    CAP_EXTENDED_SECURITY;

	caps = ctx->ct_sopt.sv_caps & caps_mask;
	uc = ctx->ct_hflags2 & SMB_FLAGS2_UNICODE;

	err = smb_rq_init(ctx, SMB_COM_SESSION_SETUP_ANDX, &rqp);
	if (err)
		goto out;

	/*
	 * Build the SMB request.
	 */
	mbp = &rqp->rq_rq;
	smb_rq_wstart(rqp);
	mb_put_uint16le(mbp, 0xff);		/* 0: AndXCommand */
	mb_put_uint16le(mbp, 0);		/* 1: AndXOffset */
	mb_put_uint16le(mbp, sv->sv_maxtx);	/* 2: MaxBufferSize */
	mb_put_uint16le(mbp, sv->sv_maxmux);	/* 3: MaxMpxCount */
	mb_put_uint16le(mbp, 1);		/* 4: VcNumber */
	mb_put_uint32le(mbp, sv->sv_skey);	/* 5,6: Session Key */

	if (caps & CAP_EXTENDED_SECURITY) {
		len1 = mbc1 ? mbc1->mb_count : 0;
		mb_put_uint16le(mbp, len1);	/* 7: Sec. Blob Len */
		mb_put_uint32le(mbp, 0);	/* 8,9: reserved */
		mb_put_uint32le(mbp, caps);	/* 10,11: Capabilities */
		smb_rq_wend(rqp);		/* 12: Byte Count */
		smb_rq_bstart(rqp);
		if (mbc1 && mbc1->mb_top) {
			mb_put_mbuf(mbp, mbc1->mb_top);	/* sec. blob */
			mbc1->mb_top = NULL; /* consumed */
		}
		/* mbc2 is required below */
		if (mbc2 == NULL) {
			err = EINVAL;
			goto out;
		}
	} else {
		len1 = mbc1 ? mbc1->mb_count : 0;
		len2 = mbc2 ? mbc2->mb_count : 0;
		mb_put_uint16le(mbp, len1);	/* 7: LM pass. len */
		mb_put_uint16le(mbp, len2);	/* 8: NT pass. len */
		mb_put_uint32le(mbp, 0);	/* 9,10: reserved */
		mb_put_uint32le(mbp, caps);	/* 11,12: Capabilities */
		smb_rq_wend(rqp);		/* 13: Byte Count */
		smb_rq_bstart(rqp);
		if (mbc1 && mbc1->mb_top) {
			mb_put_mbuf(mbp, mbc1->mb_top);	/* LM password */
			mbc1->mb_top = NULL; /* consumed */
		}
		if (mbc2 && mbc2->mb_top) {
			mb_put_mbuf(mbp, mbc2->mb_top);	/* NT password */
			mbc2->mb_top = NULL; /* consumed */
		}
		mb_put_string(mbp, ctx->ct_user, uc);
		mb_put_string(mbp, ctx->ct_domain, uc);
	}
	mb_put_string(mbp, NativeOS, uc);
	mb_put_string(mbp, LanMan, uc);
	smb_rq_bend(rqp);

	err = smb_rq_internal(ctx, rqp);
	if (err)
		goto out;

	if (statusp)
		*statusp = rqp->rq_status;

	/*
	 * If we have a real error, the response probably has
	 * no more data, so don't try to parse any more.
	 * Note: err=0, means rq_status is valid.
	 */
	if (rqp->rq_status != 0 &&
	    rqp->rq_status != NT_STATUS_MORE_PROCESSING_REQUIRED) {
		syslog(LOG_DEBUG, "SmbfsSessionSetupX: 0x%08x", rqp->rq_status);
		goto out;
	}

	/*
	 * Parse the reply
	 */
	uc = rqp->rq_hflags2 & SMB_FLAGS2_UNICODE;
	is->is_smbuid = rqp->rq_uid;
	mbp = &rqp->rq_rp;

	err = md_get_uint8(mbp, &wc);
	if (err)
		goto out;

	err = EBADRPC; /* for any problems in this section */
	if (caps & CAP_EXTENDED_SECURITY) {
		if (wc != 4)
			goto out;
		md_get_uint16le(mbp, NULL);	/* secondary cmd */
		md_get_uint16le(mbp, NULL);	/* andxoffset */
		md_get_uint16le(mbp, actionp);	/* action */
		md_get_uint16le(mbp, &sblen);	/* sec. blob len */
		md_get_uint16le(mbp, &bc);	/* byte count */
		/*
		 * Get the security blob, after
		 * sanity-checking the length.
		 */
		if (sblen == 0 || bc < sblen)
			goto out;
		err = md_get_mbuf(mbp, sblen, &m);
		if (err)
			goto out;
		mb_initm(mbc2, m);
		mbc2->mb_count = sblen;
	} else {
		if (wc != 3)
			goto out;
		md_get_uint16le(mbp, NULL);	/* secondary cmd */
		md_get_uint16le(mbp, NULL);	/* andxoffset */
		md_get_uint16le(mbp, actionp);	/* action */
		err = md_get_uint16le(mbp, &bc); /* byte count */
		if (err)
			goto out;
	}

	/*
	 * Native OS, LANMGR, & Domain follow here.
	 * Parse these strings and store for later.
	 * If unicode, they should be aligned.
	 *
	 * Note that with Extended security, we may use
	 * multiple calls to this function.  Only parse
	 * these strings on the last one (status == 0).
	 * Ditto for the CAP_LARGE work-around.
	 */
	if (rqp->rq_status != 0)
		goto out;

	/*
	 * Ignore any parsing errors for these strings.
	 * The server may follow these strings but we don't need it.
	 */
	(void) md_get_string(mbp, &ctx->ct_srv_OS, uc);
	(void) md_get_string(mbp, &ctx->ct_srv_LM, uc);
	err = 0;

	if (!reported && ctx->ct_srv_OS != NULL && ctx->ct_srv_LM != NULL) {
		reported = B_TRUE;
		DPRINT("Server OS: %s, LM: %s",
		    ctx->ct_srv_OS, ctx->ct_srv_LM);
		syslog(LOG_DEBUG, "Server OS: %s, LM: %s",
		    ctx->ct_srv_OS, ctx->ct_srv_LM);
	}

	/*
	 * Windows systems don't suport CAP_LARGE_READX,WRITEX
	 * when signing is enabled, so adjust sv_caps.
	 */
	if (ctx->ct_srv_OS &&
	    0 == strncmp(ctx->ct_srv_OS, "Windows ", 8)) {
		if (ctx->ct_vcflags & SMBV_WILL_SIGN) {
			DPRINT("Windows signing: disable CAP_LARGE_RW");
			ctx->ct_sopt.sv_caps &=
			    ~(CAP_LARGE_READX | CAP_LARGE_WRITEX);
		}
	}

out:
	if (rqp)
		smb_rq_done(rqp);

	return (err);
}

static void
smb_ssnsetup_lookup_os(char *buf, size_t buflen)
{
	struct utsname name;

	if (uname(&name) < 0) {
		(void) strlcpy(buf, "Solaris", buflen);
	} else {
		(void) snprintf(buf, buflen, "%s %s %s",
		    name.sysname, name.release, name.version);
	}
}
