/*
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */


/*
 * Niagara 2 Crypto Provider driver
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/modctl.h>
#include <sys/conf.h>
#include <sys/devops.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/open.h>
#include <sys/file.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cpuvar.h>
#include <sys/disp.h>
#include <sys/machsystm.h>
#include <sys/crypto/common.h>
#include <sys/crypto/spi.h>
#include <sys/ncs.h>
#include <sys/n2cp.h>
#include <sys/hypervisor_api.h>
#include <sys/crypto/ioctl.h>	/* for crypto_mechanism32 */
#include <sys/byteorder.h>
#include <modes/modes.h>


static void block_done(n2cp_request_t *);
static int block_start(n2cp_request_t *);
static int block_final_start(n2cp_request_t *);
static boolean_t ccm_uio_check(ccm_ctx_t *, crypto_data_t *, size_t);

/* The zero_block is read-only */
static const uint64_t zero_block[2] = { 0, 0 };

aes_block_t gcm_mul_vis3_64(aes_block_t x, aes_block_t y);


#define	isDES(cmd) \
	((((cmd) & N2CP_CMD_MASK) == DES_CBC_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == DES_CBC_PAD_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == DES_ECB_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == DES_CFB_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == DES3_CBC_MECH_INFO_TYPE) ||\
	(((cmd) & N2CP_CMD_MASK) == DES3_CBC_PAD_MECH_INFO_TYPE) ||\
	(((cmd) & N2CP_CMD_MASK) == DES3_ECB_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == DES3_CFB_MECH_INFO_TYPE))
#define	isAES(cmd) \
	((((cmd) & N2CP_CMD_MASK) == AES_CBC_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == AES_CFB_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == AES_CBC_PAD_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == AES_ECB_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == AES_CTR_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == AES_CCM_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == AES_GCM_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == AES_GMAC_MECH_INFO_TYPE))
#define	isRC4(cmd) \
	((((cmd) & N2CP_CMD_MASK) == RC4_WSTRM_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == RC4_WOSTRM_MECH_INFO_TYPE))
#define	isCTR(cmd) \
	(((cmd) & N2CP_CMD_MASK) == AES_CTR_MECH_INFO_TYPE)
#define	isCBCPAD(cmd) \
	((((cmd) & N2CP_CMD_MASK) == AES_CBC_PAD_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == DES_CBC_PAD_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == DES3_CBC_PAD_MECH_INFO_TYPE))
#define	isCBC(cmd) \
	((((cmd) & N2CP_CMD_MASK) == DES_CBC_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == DES3_CBC_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == AES_CBC_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == AES_CBC_PAD_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == DES_CBC_PAD_MECH_INFO_TYPE) || \
	(((cmd) & N2CP_CMD_MASK) == DES3_CBC_PAD_MECH_INFO_TYPE))
#define	isCCM(cmd) \
	(((cmd) & N2CP_CMD_MASK) == AES_CCM_MECH_INFO_TYPE)
#define	isGCM(cmd) \
	(((cmd) & N2CP_CMD_MASK) == AES_GCM_MECH_INFO_TYPE)
#define	isGMAC(cmd) \
	(((cmd) & N2CP_CMD_MASK) == AES_GMAC_MECH_INFO_TYPE)
#define	isCFB(cmd) \
	(((cmd) & N2CP_CMD_MASK) == AES_CFB_MECH_INFO_TYPE)
#define	STREAM_CIPHER(cmd) \
	(isRC4(cmd) || isCTR(cmd) || isCFB(cmd))

#ifdef	DEBUG
int	n2cp_hist = 0;
hrtime_t n2cp_max_job_nsec = 0;
hrtime_t n2cp_min_job_nsec = 0;
#endif

#define	GHASH(c, d, t) \
	aes_xor_block((uint8_t *)(d), (uint8_t *)(c)->gcm_ghash); \
	if (is_KT(NULL)) {					\
		*((aes_block_t *)(t)) = gcm_mul_vis3_64(	\
		    *((aes_block_t *)((c)->gcm_ghash)),		\
		    *((aes_block_t *)((c)->gcm_H)));		\
	} else {						\
		gcm_mul((c)->gcm_ghash, (c)->gcm_H, (t));	\
	}


#define	AES_COPY_BLOCK(src, dst) \
	(dst)[0] = (src)[0]; \
	(dst)[1] = (src)[1]; \
	(dst)[2] = (src)[2]; \
	(dst)[3] = (src)[3]; \
	(dst)[4] = (src)[4]; \
	(dst)[5] = (src)[5]; \
	(dst)[6] = (src)[6]; \
	(dst)[7] = (src)[7]; \
	(dst)[8] = (src)[8]; \
	(dst)[9] = (src)[9]; \
	(dst)[10] = (src)[10]; \
	(dst)[11] = (src)[11]; \
	(dst)[12] = (src)[12]; \
	(dst)[13] = (src)[13]; \
	(dst)[14] = (src)[14]; \
	(dst)[15] = (src)[15]

#define	AES_XOR_BLOCK(src, dst) \
	(dst)[0] ^= (src)[0]; \
	(dst)[1] ^= (src)[1]; \
	(dst)[2] ^= (src)[2]; \
	(dst)[3] ^= (src)[3]; \
	(dst)[4] ^= (src)[4]; \
	(dst)[5] ^= (src)[5]; \
	(dst)[6] ^= (src)[6]; \
	(dst)[7] ^= (src)[7]; \
	(dst)[8] ^= (src)[8]; \
	(dst)[9] ^= (src)[9]; \
	(dst)[10] ^= (src)[10]; \
	(dst)[11] ^= (src)[11]; \
	(dst)[12] ^= (src)[12]; \
	(dst)[13] ^= (src)[13]; \
	(dst)[14] ^= (src)[14]; \
	(dst)[15] ^= (src)[15]

static void
aes_copy_block(uint8_t *in, uint8_t *out)
{
	if (IS_P2ALIGNED(in, sizeof (uint32_t)) &&
	    IS_P2ALIGNED(out, sizeof (uint32_t))) {
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[0] = *(uint32_t *)&in[0];
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[4] = *(uint32_t *)&in[4];
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[8] = *(uint32_t *)&in[8];
		/* LINTED: pointer alignment */
		*(uint32_t *)&out[12] = *(uint32_t *)&in[12];
	} else {
		AES_COPY_BLOCK(in, out);
	}
}

/* XOR block of data into dest */
static void
aes_xor_block(uint8_t *data, uint8_t *dst)
{
	if (IS_P2ALIGNED(dst, sizeof (uint32_t)) &&
	    IS_P2ALIGNED(data, sizeof (uint32_t))) {
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[0] ^= *(uint32_t *)&data[0];
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[4] ^= *(uint32_t *)&data[4];
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[8] ^= *(uint32_t *)&data[8];
		/* LINTED: pointer alignment */
		*(uint32_t *)&dst[12] ^= *(uint32_t *)&data[12];
	} else {
		AES_XOR_BLOCK(data, dst);
	}
}

static void
kcf_block_done(n2cp_request_t *reqp)
{
	crypto_data_t *out = reqp->nr_out;

	if (reqp->nr_errno != CRYPTO_SUCCESS) {
		return;
	}
	if (n2cp_use_ulcwq) {
		reqp->nr_errno = n2cp_scatter((char *)reqp->nr_out_buf,
		    reqp->nr_out, reqp->nr_resultlen);
	}

	if (reqp->nr_errno != CRYPTO_SUCCESS) {
		return;
	}
	out->cd_length += reqp->nr_resultlen;
}

int
n2cp_encrypt_block(const void *ks, const uint8_t *pt, uint8_t *ct)
{
	n2cp_request_t	*reqp = (n2cp_request_t *)ks;
	crypto_data_t in, out;
	crypto_data_t *inp = &in;
	crypto_data_t *outp = &out;

	inp->cd_format = CRYPTO_DATA_RAW;
	inp->cd_raw.iov_base = (char *)pt;
	inp->cd_raw.iov_len = AESBLOCK;
	inp->cd_offset = 0;
	inp->cd_length = AESBLOCK;
	inp->cd_miscdata = NULL;

	if (pt == ct) {
		outp = inp;
	} else {
		outp->cd_format = CRYPTO_DATA_RAW;
		outp->cd_raw.iov_base = (char *)ct;
		outp->cd_raw.iov_len = AESBLOCK;
		outp->cd_offset = 0;
		outp->cd_length = AESBLOCK;
		outp->cd_miscdata = NULL;
	}

	N2CP_REQ_SETUP(reqp, inp, outp);
	reqp->nr_callback = kcf_block_done;
	reqp->nr_kcfreq = reqp;

	return (block_start(reqp));
}

/*
 * Used by CCM to compute MAC
 */
static int
compute_mac(n2cp_request_t *reqp, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	n2cp_block_ctx_t *blockctx = &reqp->nr_context->blockctx;
	ccm_ctx_t *ccm_ctx = reqp->nr_mode_ctx;
	size_t saved_length;
	uint32_t saved_cmd;
	boolean_t uio_modified = B_FALSE;
	int rv;

	saved_length = out->cd_length;
	BCOPY(ccm_ctx->ccm_mac_buf, blockctx->nextiv, AESBLOCK);
	blockctx->nextivlen = AESBLOCK;

	uio_modified = ccm_uio_check(ccm_ctx, out, ccm_ctx->ccm_mac_len);
	N2CP_REQ_SETUP(reqp, in, out);
	reqp->nr_callback = block_done;
	reqp->nr_kcfreq = kcfreq;
	saved_cmd = reqp->nr_cmd;
	reqp->nr_cmd &= ~(AES_CCM_MECH_INFO_TYPE | N2CP_OP_DECRYPT);
	reqp->nr_cmd |= (AES_CBC_MECH_INFO_TYPE | N2CP_OP_ENCRYPT);
	rv = block_start(reqp);
	if (uio_modified)
		out->cd_uio->uio_iovcnt = 2;

	if (rv != CRYPTO_SUCCESS)
		goto out;

	reqp->nr_cmd = saved_cmd;
	out->cd_length = saved_length;
	BCOPY(blockctx->nextiv, ccm_ctx->ccm_mac_buf, AESBLOCK);

out:
	reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
	n2cp_free_buf(reqp->nr_n2cp, reqp);
	return (rv);
}

/*
 * Update MAC with any residual bytes in the block context.
 */
static int
mac_final(n2cp_request_t *reqp, crypto_req_handle_t kcfreq)
{
	n2cp_block_ctx_t *blockctx = &reqp->nr_context->blockctx;
	ccm_ctx_t *ccm_ctx = reqp->nr_mode_ctx;
	uint32_t saved_cmd;
	int rv = CRYPTO_SUCCESS;

	saved_cmd = reqp->nr_cmd;

	if (blockctx->residlen > 0) {
		crypto_data_t pt_buf;
		crypto_data_t last_block_buf;
		crypto_data_t *pt = &pt_buf;
		crypto_data_t *last_block = &last_block_buf;
		int zero_bytes;

		BCOPY(ccm_ctx->ccm_mac_buf, blockctx->nextiv, AESBLOCK);
		blockctx->nextivlen = AESBLOCK;
		reqp->nr_callback = block_done;
		reqp->nr_kcfreq = kcfreq;
		reqp->nr_cmd &= ~(AES_CCM_MECH_INFO_TYPE | N2CP_OP_DECRYPT);
		reqp->nr_cmd |= (AES_CBC_MECH_INFO_TYPE | N2CP_OP_ENCRYPT);

		zero_bytes = AESBLOCK - (blockctx->residlen % AESBLOCK);
		pt->cd_format = CRYPTO_DATA_RAW;
		pt->cd_raw.iov_base = (char *)zero_block;
		pt->cd_raw.iov_len = zero_bytes;
		pt->cd_offset = 0;
		pt->cd_length = zero_bytes;
		pt->cd_miscdata = NULL;

		last_block->cd_format = CRYPTO_DATA_RAW;
		last_block->cd_raw.iov_base = (char *)ccm_ctx->ccm_mac_buf;
		last_block->cd_raw.iov_len = AESBLOCK;
		last_block->cd_offset = 0;
		last_block->cd_length = AESBLOCK;
		last_block->cd_miscdata = NULL;

		N2CP_REQ_SETUP(reqp, pt, last_block);
		rv = block_start(reqp);
	}
	reqp->nr_cmd = saved_cmd;
	reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
	n2cp_free_buf(reqp->nr_n2cp, reqp);
	return (rv);
}

/*
 * CCM and GCM: returns number of counter bits.
 */
static uint_t
get_ctr_bits(n2cp_request_t *reqp)
{
	uint_t bits = 0;

	if ((reqp->nr_cmd & N2CP_CMD_MASK) == AES_CCM_MECH_INFO_TYPE) {
		ccm_ctx_t *ctx = reqp->nr_mode_ctx;

		switch (ctx->ccm_counter_mask) {
		case 0x000000000000ffff:
			bits = 16;
			break;
		case 0x0000000000ffffff:
			bits = 24;
			break;
		case 0x00000000ffffffff:
			bits = 32;
			break;
		case 0x000000ffffffffff:
			bits = 40;
			break;
		case 0x0000ffffffffffff:
			bits = 48;
			break;
		case 0x00ffffffffffffff:
			bits = 56;
			break;
		case 0xffffffffffffffff:
			bits = 64;
			break;
		}
	} else {
		bits = 32;
	}
	return (bits);
}

/*
 * Used by CCM and GCM to encrypt/decrypt the data.
 */
static int
do_ctr(n2cp_request_t *reqp, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	n2cp_block_ctx_t *blockctx = &reqp->nr_context->blockctx;
	size_t saved_length;
	uint32_t saved_cmd;
	int rv;
	char *cb;

	if ((reqp->nr_cmd & N2CP_CMD_MASK) == AES_CCM_MECH_INFO_TYPE)
		cb = (char *)((ccm_ctx_t *)reqp->nr_mode_ctx)->ccm_cb;
	else
		cb = (char *)((gcm_ctx_t *)reqp->nr_mode_ctx)->gcm_cb;

	BCOPY(cb, blockctx->nextiv, AESBLOCK);
	blockctx->nextivlen = AESBLOCK;
	blockctx->ctrbits = get_ctr_bits(reqp);
	saved_length = out->cd_length;
	N2CP_REQ_SETUP(reqp, in, out);
	reqp->nr_callback = block_done;
	reqp->nr_kcfreq = kcfreq;
	saved_cmd = reqp->nr_cmd;
	reqp->nr_cmd &= ~((reqp->nr_cmd & N2CP_CMD_MASK) | N2CP_OP_DECRYPT);
	reqp->nr_cmd |= (AES_CTR_MECH_INFO_TYPE | N2CP_OP_ENCRYPT);
	rv = block_start(reqp);
	if (rv != CRYPTO_SUCCESS)
		goto out;

	reqp->nr_cmd = saved_cmd;
	out->cd_offset += out->cd_length;
	out->cd_length = saved_length;
	BCOPY(blockctx->nextiv, cb, AESBLOCK);

out:
	reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
	n2cp_free_buf(reqp->nr_n2cp, reqp);
	return (rv);
}

static void
update_gcm_hash(gcm_ctx_t *ctx, char *data, size_t len)
{
	size_t remainder;
	uint64_t *ghash;
	uint8_t *blockp;
	size_t new_len;

	if (len == 0)
		return;

	ghash = ctx->gcm_ghash;
	blockp = (uint8_t *)ctx->gcm_remainder;

	/* nothing to do but accumulate bytes */
	if (len + ctx->gcm_remainder_len < AESBLOCK) {
		bcopy(data, &blockp[ctx->gcm_remainder_len], len);
		ctx->gcm_remainder_len += len;
		return;
	}

	/* hash at least one block */
	bcopy(data, &blockp[ctx->gcm_remainder_len],
	    AESBLOCK - ctx->gcm_remainder_len);
	GHASH(ctx, ctx->gcm_remainder, ghash);
	new_len = len - (AESBLOCK - ctx->gcm_remainder_len);
	ctx->gcm_remainder_len = 0;

	blockp = (uint8_t *)&data[len - new_len];
	remainder = new_len;
	/* add ciphertext to the hash */
	while (remainder > 0) {
		if (remainder < AESBLOCK) {
			/* accumulate bytes */
			bcopy(blockp, ctx->gcm_remainder, remainder);
			ctx->gcm_remainder_len = remainder;
			break;
		}
		GHASH(ctx, blockp, ghash);
		blockp += AESBLOCK;
		remainder -= AESBLOCK;
	}
}

static int
gcm_hash_final(gcm_ctx_t *ctx, size_t len)
{
	uint64_t *ghash = ctx->gcm_ghash;
	uchar_t *blockp = (uchar_t *)ctx->gcm_remainder;
	int rv;

	/* hash any remaining bytes */
	if (ctx->gcm_remainder_len > 0) {
		bzero(&blockp[ctx->gcm_remainder_len],
		    AESBLOCK - ctx->gcm_remainder_len);
		GHASH(ctx, ctx->gcm_remainder, ghash);
		ctx->gcm_remainder_len = 0;
	}

	ctx->gcm_len_a_len_c[1] = htonll(len << 3);
	GHASH(ctx, ctx->gcm_len_a_len_c, ghash);
	if ((rv = n2cp_encrypt_block(ctx->gcm_keysched, (uint8_t *)ctx->gcm_J0,
	    (uint8_t *)ctx->gcm_J0)) == CRYPTO_SUCCESS)
		aes_xor_block((uint8_t *)ctx->gcm_J0, (uint8_t *)ghash);
	return (rv);
}

/*
 * A 64K buffer gets allocated whenever a uio contains more than
 * one iovec (hardware only deals with contiguous memory).
 * If the second iovec only contains the MAC then, by changing
 * the iovec count to 1, the second iovec is hidden, and a
 * 64K buffer does not get allocated.
 */
static boolean_t
ccm_uio_check(ccm_ctx_t *ctx, crypto_data_t *data, size_t len)
{
	if (data->cd_format == CRYPTO_DATA_UIO &&
	    data->cd_uio->uio_iovcnt == 2 &&
	    (data->cd_uio->uio_iov[0].iov_len - data->cd_offset) ==
	    ctx->ccm_data_len &&
	    data->cd_uio->uio_iov[1].iov_len == ctx->ccm_mac_len) {
		data->cd_length -= len;
		data->cd_uio->uio_iovcnt = 1;
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Similar to ccm_uio_check(), this function does not
 * check the length of the first iovec. This function
 * should only be called when iovec #1 contains all
 * of the data, i.e. single-part operations only.
 */
static boolean_t
gcm_uio_check(gcm_ctx_t *ctx, crypto_data_t *data, size_t len)
{
	if (data->cd_format == CRYPTO_DATA_UIO &&
	    data->cd_uio->uio_iovcnt == 2 &&
	    data->cd_uio->uio_iov[1].iov_len == ctx->gcm_tag_len) {
		data->cd_length -= len;
		data->cd_uio->uio_iovcnt = 1;
		return (B_TRUE);
	}
	return (B_FALSE);
}

#ifndef DEBUG
#pragma inline(ccm_uio_check, gcm_uio_check)
#endif

static int
ccm_encrypt(n2cp_request_t *reqp, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	n2cp_block_ctx_t *blockctx = &reqp->nr_context->blockctx;
	ccm_ctx_t *ctx = reqp->nr_mode_ctx;
	boolean_t uio_modified = B_FALSE;
	int rv;
	uint64_t tmp[2];
	int tmplen = 0;
	crypto_data_t *scratch = out;
	boolean_t shared = B_FALSE;

	/*
	 * The output buffer is normally used as a scratch buffer
	 * to compute the MAC. This arrangement doesn't work when
	 * input and output crypto_data structures point to the
	 * same buffer because plaintext is overwritten.
	 */
	if (in != out && in->cd_format == out->cd_format) {
		switch (in->cd_format) {
		case CRYPTO_DATA_RAW:
			shared = (in->cd_raw.iov_base == out->cd_raw.iov_base);
			break;
		case CRYPTO_DATA_UIO:
			shared = (in->cd_uio == out->cd_uio);
			break;
		case CRYPTO_DATA_MBLK:
			shared = (in->cd_mp == out->cd_mp);
		}
	}
	if (shared) {
		crypto_data_t tmp_out;
		ulong_t saved_length, saved_offset, len;

		/* get 64K contiguous memory */
		if ((rv = n2cp_get_outbuf(reqp->nr_n2cp, reqp)) !=
		    CRYPTO_SUCCESS) {
			return (rv);
		}
		tmp_out.cd_format = CRYPTO_DATA_RAW;
		tmp_out.cd_raw.iov_base = (char *)reqp->nr_out_buf;
		tmp_out.cd_raw.iov_len = BUFSIZE_64K;
		tmp_out.cd_offset = 0;
		tmp_out.cd_length = BUFSIZE_64K;
		tmp_out.cd_miscdata = NULL;

		reqp->nr_flags |= N2CP_SCATTER;
		scratch = &tmp_out;
		/*
		 * Call compute_mac() for each 64K chunk (or less).
		 */
		saved_offset = in->cd_offset;
		saved_length = len = in->cd_length;
		in->cd_length = (len >= BUFSIZE_64K) ? BUFSIZE_64K :
		    len % BUFSIZE_64K;
		while (len > 0) {
			rv = compute_mac(reqp, in, scratch, kcfreq);
			if (rv != CRYPTO_SUCCESS) {
				in->cd_offset = saved_offset;
				in->cd_length = saved_length;
				return (rv);
			}
			in->cd_offset += in->cd_length;
			len -= in->cd_length;
			in->cd_length = (len >= BUFSIZE_64K) ? BUFSIZE_64K :
			    len % BUFSIZE_64K;
		}
		in->cd_offset = saved_offset;
		in->cd_length = saved_length;
	} else {
		rv = compute_mac(reqp, in, scratch, kcfreq);
	}
	if (blockctx->residlen > 0) {
		bcopy(blockctx->resid, (char *)tmp, blockctx->residlen);
		tmplen = blockctx->residlen;
	}
	/* resid will mess up ctr */
	blockctx->residlen = 0;
	if (rv == CRYPTO_SUCCESS) {
		uio_modified = ccm_uio_check(ctx, out, ctx->ccm_mac_len);
		rv = do_ctr(reqp, in, out, kcfreq);
		if (rv == CRYPTO_SUCCESS) {
			ctx->ccm_processed_data_len += in->cd_length;
			ctx->ccm_remainder_len = 0;
		}
		if (uio_modified) {
			out->cd_uio->uio_iovcnt = 2;
			out->cd_length += ctx->ccm_mac_len;
		}
		if (tmplen > 0) {
			bcopy((char *)tmp, blockctx->resid, tmplen);
			blockctx->residlen = tmplen;
		}
	}
	return (rv);
}

static int
ccm_decrypt(n2cp_request_t *reqp, crypto_data_t *in,
    crypto_req_handle_t kcfreq)
{
	ccm_ctx_t *ctx = reqp->nr_mode_ctx;
	crypto_data_t pt_buf;
	crypto_data_t *pt = &pt_buf;
	size_t mac_len = 0, total_len;
	boolean_t uio_modified = B_FALSE;
	int rv;

	total_len = ctx->ccm_data_len + ctx->ccm_mac_len;
	if (in->cd_length > total_len)
		return (CRYPTO_DATA_LEN_RANGE);

	/* save the MAC */
	if (ctx->ccm_processed_mac_len + in->cd_length == total_len) {
		n2cp_getbufbytes(in, in->cd_length - ctx->ccm_mac_len,
		    ctx->ccm_mac_len, (char *)ctx->ccm_mac_input_buf);
		mac_len = ctx->ccm_mac_len;
	}
	ctx->ccm_processed_mac_len += in->cd_length;

	pt->cd_format = CRYPTO_DATA_RAW;
	pt->cd_raw.iov_base = (char *)ctx->ccm_pt_buf +
	    ctx->ccm_processed_data_len;
	pt->cd_raw.iov_len = in->cd_length;
	pt->cd_offset = 0;
	pt->cd_length = 0;
	pt->cd_miscdata = NULL;

	uio_modified = ccm_uio_check(ctx, in, 0);
	in->cd_length -= mac_len;
	rv = do_ctr(reqp, in, pt, kcfreq);
	if (uio_modified)
		in->cd_uio->uio_iovcnt = 2;
	if (rv != CRYPTO_SUCCESS) {
		in->cd_length += mac_len;
		return (rv);
	}
	ctx->ccm_processed_data_len += in->cd_length;
	ctx->ccm_remainder_len = 0;
	in->cd_length += mac_len;
	return (rv);
}

static int
gcm_decrypt_update(n2cp_request_t *reqp, crypto_data_t *in)
{
	gcm_ctx_t *ctx = reqp->nr_mode_ctx;
	size_t new_len, len;
	uint64_t *new;
	int rv;

	/*
	 * Copy ciphertext input blocks to buffer where
	 * it will be decrypted in the final.
	 */
	len = in->cd_length;
	if (len > 0) {
		new_len = ctx->gcm_pt_buf_len + len;
		if ((new = kmem_alloc(new_len, KM_SLEEP)) == NULL)
			return (CRYPTO_HOST_MEMORY);

		if (ctx->gcm_pt_buf_len > 0)
			bcopy(ctx->gcm_pt_buf, new, ctx->gcm_pt_buf_len);
		if ((rv = n2cp_gather(in,
		    &(((char *)new)[ctx->gcm_processed_data_len]), len)) !=
		    CRYPTO_SUCCESS) {
			return (rv);
		}

		if (ctx->gcm_pt_buf != NULL)
			kmem_free(ctx->gcm_pt_buf, ctx->gcm_pt_buf_len);
		ctx->gcm_pt_buf = new;
		ctx->gcm_pt_buf_len = new_len;
		ctx->gcm_processed_data_len += len;
	}
	ctx->gcm_remainder_len = 0;
	return (CRYPTO_SUCCESS);
}

static int
n2cp_ccm_decrypt_final(n2cp_request_t *reqp, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	ccm_ctx_t *ccm_ctx = reqp->nr_mode_ctx;
	crypto_data_t pt_buf;
	crypto_data_t *pt = &pt_buf;
	size_t mac_len;
	uint8_t *ccm_mac_p;
	int rv, scatter_rv;

	mac_len = ccm_ctx->ccm_mac_len;
	if (ccm_ctx->ccm_processed_mac_len != ccm_ctx->ccm_data_len + mac_len)
		return (CRYPTO_DATA_LEN_RANGE);

	pt->cd_format = CRYPTO_DATA_RAW;
	pt->cd_raw.iov_base = (char *)ccm_ctx->ccm_pt_buf;
	pt->cd_raw.iov_len = ccm_ctx->ccm_data_len;
	pt->cd_offset = 0;
	pt->cd_length = ccm_ctx->ccm_data_len;
	pt->cd_miscdata = NULL;

	rv = compute_mac(reqp, pt, out, kcfreq);
	if (rv != CRYPTO_SUCCESS)
		return (rv);

	rv = mac_final(reqp, kcfreq);
	if (rv != CRYPTO_SUCCESS)
		return (rv);

	ccm_mac_p = (uint8_t *)ccm_ctx->ccm_tmp;
	calculate_ccm_mac(ccm_ctx, ccm_mac_p, n2cp_encrypt_block);

	if (bcmp(ccm_ctx->ccm_mac_input_buf, ccm_mac_p, mac_len)) {
		/* zero the plaintext */
		bzero(ccm_ctx->ccm_pt_buf, ccm_ctx->ccm_data_len);
		rv = CRYPTO_INVALID_MAC;
	}
	out->cd_length = 0;
	scatter_rv = n2cp_scatter((char *)ccm_ctx->ccm_pt_buf, out,
	    ccm_ctx->ccm_data_len);

	if (rv == CRYPTO_SUCCESS && scatter_rv == CRYPTO_SUCCESS) {
		/* set offset to amount of data processed */
		out->cd_offset = out->cd_length;
	}
	return ((rv == CRYPTO_SUCCESS) ? scatter_rv : rv);
}

static int
n2cp_gcm_decrypt_final(n2cp_request_t *reqp, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	gcm_ctx_t *ctx = (gcm_ctx_t *)reqp->nr_mode_ctx;
	uint8_t *ghash = (uint8_t *)ctx->gcm_ghash;
	off_t saved_offset = out->cd_offset;
	crypto_data_t ct;
	size_t len;
	int rv;

	/* Once we see the final, we know how much ciphertext there is. */
	len = ctx->gcm_processed_data_len - ctx->gcm_tag_len;

	if (out->cd_length < len) {
		out->cd_length = len;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}
	/* add ciphertext to the hash */
	update_gcm_hash(ctx, (char *)ctx->gcm_pt_buf, len);
	if ((rv = gcm_hash_final(ctx, len)) != CRYPTO_SUCCESS)
		return (rv);

	/*
	 * Compare the input authentication tag with
	 * what we calculated.
	 */
	if (bcmp(&(((char *)(ctx->gcm_pt_buf))[len]),
	    ghash, ctx->gcm_tag_len)) {
		/* They don't match */
		return (CRYPTO_INVALID_MAC);
	} else {
		/* nothing to decrypt */
		if (len == 0) {
			out->cd_length = 0;
			return (CRYPTO_SUCCESS);
		}
		/* now decrypt the buffer and scatter */
		ct.cd_format = CRYPTO_DATA_RAW;
		ct.cd_raw.iov_base = (char *)ctx->gcm_pt_buf;
		ct.cd_raw.iov_len = len;
		ct.cd_offset = 0;
		ct.cd_length = len;
		ct.cd_miscdata = NULL;

		if ((rv = do_ctr(reqp, &ct, out, kcfreq)) != CRYPTO_SUCCESS) {
			out->cd_length = 0;
		} else {
			/* movement of offset indicates data processed */
			out->cd_length = out->cd_offset - saved_offset;
		}
		out->cd_offset = saved_offset;
	}
	return (rv);
}

static int
gcm_hash_data(gcm_ctx_t *ctx, crypto_data_t *data, size_t len)
{
	int rv;

	switch (data->cd_format) {
	case CRYPTO_DATA_RAW:
		update_gcm_hash(ctx,
		    (char *)data->cd_raw.iov_base + data->cd_offset, len);
		break;
	case CRYPTO_DATA_UIO:
		if ((rv = crypto_uio_data(data, NULL, len,
		    GHASH_DATA, ctx, update_gcm_hash))
		    != CRYPTO_SUCCESS)
			return (rv);
		break;
	case CRYPTO_DATA_MBLK:
		if ((rv = crypto_mblk_data(data, NULL, len,
		    GHASH_DATA, ctx, update_gcm_hash))
		    != CRYPTO_SUCCESS)
			return (rv);
		break;
	default:
		return (CRYPTO_ARGUMENTS_BAD);
	}
	return (CRYPTO_SUCCESS);
}

/*
 * This routine does everthing except for the init.
 * It is used by n2cp_block() and n2cp_blockatomic().
 */
static int
gcm_process(n2cp_request_t *reqp, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	gcm_ctx_t *ctx = reqp->nr_mode_ctx;
	n2cp_block_ctx_t *blockctx = &reqp->nr_context->blockctx;
	off_t saved_offset = out->cd_offset;
	boolean_t uio_modified = B_FALSE;
	uint8_t *ghash = (uint8_t *)ctx->gcm_ghash;
	size_t len;
	int rv;

	if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
		if (in->cd_length > 0) {
			blockctx->residlen = 0;
			uio_modified = gcm_uio_check(ctx, out,
			    ctx->gcm_tag_len);
			rv = do_ctr(reqp, in, out, kcfreq);
			out->cd_offset = saved_offset;
			if (uio_modified) {
				out->cd_uio->uio_iovcnt = 2;
				out->cd_length += ctx->gcm_tag_len;
			}
			if (rv != CRYPTO_SUCCESS)
				goto fail;
		}
		ctx->gcm_processed_data_len += in->cd_length;
		len = ctx->gcm_processed_data_len;
		out->cd_length = ctx->gcm_processed_data_len;
		if ((rv = gcm_hash_data(ctx, out, len)) != CRYPTO_SUCCESS)
			goto fail;

		if ((rv = gcm_hash_final(ctx, len)) != CRYPTO_SUCCESS)
			goto fail;

		if ((rv = n2cp_scatter((char *)ghash, out, ctx->gcm_tag_len))
		    != CRYPTO_SUCCESS)
			goto fail;
	} else {
		uint64_t tag[2];

		len = in->cd_length - ctx->gcm_tag_len;
		if ((rv = gcm_hash_data(ctx, in, len)) != CRYPTO_SUCCESS)
			goto fail;

		if ((rv = gcm_hash_final(ctx, len)) != CRYPTO_SUCCESS)
			goto fail;

		/* get the authentication tag from the input */
		in->cd_offset += len;
		rv = n2cp_gather(in, (char *)tag, ctx->gcm_tag_len);
		in->cd_offset = saved_offset;

		/* add back what gather took off */
		in->cd_length += ctx->gcm_tag_len;
		if (rv != CRYPTO_SUCCESS)
			return (rv);
		/*
		 * Compare the input authentication tag with
		 * what we calculated.
		 */
		if (bcmp(tag, ghash, ctx->gcm_tag_len)) {
			/* They don't match */
			return (CRYPTO_INVALID_MAC);
		} else {
			/* nothing to decrypt */
			if (len == 0) {
				out->cd_length = 0;
				return (CRYPTO_SUCCESS);
			}
			uio_modified = gcm_uio_check(ctx, in, 0);
			blockctx->residlen = 0;
			in->cd_length -= ctx->gcm_tag_len;
			rv = do_ctr(reqp, in, out, kcfreq);
			in->cd_length += ctx->gcm_tag_len;
			if (uio_modified)
				in->cd_uio->uio_iovcnt = 2;

			if (rv != CRYPTO_SUCCESS)
				goto fail;

			out->cd_length = len;
			out->cd_offset = saved_offset;
		}
	}
	return (CRYPTO_SUCCESS);
fail:
	out->cd_offset = saved_offset;
	out->cd_length = 0;
	return (rv);
}

/*
 * AES_CTR sub routines
 */
/*
 * This function is used by kEF to copyin the mechanism parameter for AES_CTR.
 * Note: 'inmech' comes in the applications size structure
 * Note: 'outmech' is initialized by the driver so that the mech is in
 * the native size structure.
 */
int
n2cp_aes_ctr_allocmech(crypto_mechanism_t *inmech, crypto_mechanism_t *outmech,
    int *error, int mode)
{
	STRUCT_DECL(crypto_mechanism, mech);
	STRUCT_DECL(CK_AES_CTR_PARAMS, params);
	CK_AES_CTR_PARAMS	*aes_ctr_params;
	caddr_t			param;
	size_t			paramlen;

/* EXPORT DELETE START */

	STRUCT_INIT(mech, mode);
	STRUCT_INIT(params, mode);
	bcopy(inmech, STRUCT_BUF(mech), STRUCT_SIZE(mech));
	param = STRUCT_FGETP(mech, cm_param);
	paramlen = STRUCT_FGET(mech, cm_param_len);

	if (paramlen != STRUCT_SIZE(params)) {
		*error = EINVAL;
		return (CRYPTO_ARGUMENTS_BAD);
	}

	outmech->cm_type = STRUCT_FGET(mech, cm_type);
	outmech->cm_param = NULL;
	outmech->cm_param_len = 0;
	if (inmech->cm_param != NULL) {
		if (ddi_copyin(param, STRUCT_BUF(params),
		    paramlen, mode) != 0) {
			*error = EFAULT;
			return (CRYPTO_FAILED);
		}

		/* allocate the native size structure */
		aes_ctr_params = kmem_alloc(sizeof (CK_AES_CTR_PARAMS),
		    KM_SLEEP);
		aes_ctr_params->ulCounterBits = STRUCT_FGET(params,
		    ulCounterBits);
		if (aes_ctr_params->ulCounterBits > MAX_CTR_BITS) {
			kmem_free(aes_ctr_params, sizeof (CK_AES_CTR_PARAMS));
			*error = ENOMEM;
			return (CRYPTO_HOST_MEMORY);
		}
		bcopy(STRUCT_FGET(params, cb), &aes_ctr_params->cb[0], 16);
		outmech->cm_param = (char *)aes_ctr_params;
		outmech->cm_param_len = sizeof (CK_AES_CTR_PARAMS);
	}

	*error = 0;

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

int
n2cp_aes_ccm_allocmech(crypto_mechanism_t *inmech, crypto_mechanism_t *outmech,
    int *error, int mode)
{
	STRUCT_DECL(crypto_mechanism, mech);
	STRUCT_DECL(CK_AES_CCM_PARAMS, params);
	CK_AES_CCM_PARAMS *aes_ccm_params;
	caddr_t param;
	size_t paramlen;

	STRUCT_INIT(mech, mode);
	STRUCT_INIT(params, mode);
	bcopy(inmech, STRUCT_BUF(mech), STRUCT_SIZE(mech));
	param = STRUCT_FGETP(mech, cm_param);
	paramlen = STRUCT_FGET(mech, cm_param_len);

	if (paramlen != STRUCT_SIZE(params)) {
		*error = EINVAL;
		return (CRYPTO_ARGUMENTS_BAD);
	}

	outmech->cm_type = STRUCT_FGET(mech, cm_type);
	outmech->cm_param = NULL;
	outmech->cm_param_len = 0;
	if (inmech->cm_param != NULL) {
		size_t nonce_len, auth_data_len, total_param_len;

		if (ddi_copyin(param, STRUCT_BUF(params), paramlen,
		    mode) != 0) {
			outmech->cm_param = NULL;
			*error = EFAULT;
			return (CRYPTO_FAILED);
		}

		nonce_len = STRUCT_FGET(params, ulNonceSize);
		auth_data_len = STRUCT_FGET(params, ulAuthDataSize);

		/* allocate param structure */
		total_param_len =
		    sizeof (CK_AES_CCM_PARAMS) + nonce_len + auth_data_len;
		aes_ccm_params = kmem_alloc(total_param_len, KM_SLEEP);
		aes_ccm_params->ulMACSize = STRUCT_FGET(params, ulMACSize);
		aes_ccm_params->ulNonceSize = nonce_len;
		aes_ccm_params->ulAuthDataSize = auth_data_len;
		aes_ccm_params->ulDataSize = STRUCT_FGET(params, ulDataSize);
		aes_ccm_params->nonce = (uchar_t *)aes_ccm_params +
		    sizeof (CK_AES_CCM_PARAMS);
		aes_ccm_params->authData = aes_ccm_params->nonce + nonce_len;

		if (ddi_copyin((char *)STRUCT_FGETP(params, nonce),
		    aes_ccm_params->nonce, nonce_len, mode) != 0) {
			kmem_free(aes_ccm_params, total_param_len);
			outmech->cm_param = NULL;
			*error = EFAULT;
			return (CRYPTO_FAILED);
		}
		if (ddi_copyin((char *)STRUCT_FGETP(params, authData),
		    aes_ccm_params->authData, auth_data_len, mode) != 0) {
			kmem_free(aes_ccm_params, total_param_len);
			outmech->cm_param = NULL;
			*error = EFAULT;
			return (CRYPTO_FAILED);
		}
		outmech->cm_param = (char *)aes_ccm_params;
		outmech->cm_param_len = sizeof (CK_AES_CCM_PARAMS);
	}

	return (CRYPTO_SUCCESS);
}

int
n2cp_aes_gcm_allocmech(crypto_mechanism_t *inmech, crypto_mechanism_t *outmech,
    int *error, int mode)
{
	STRUCT_DECL(crypto_mechanism, mech);
	STRUCT_DECL(CK_AES_GCM_PARAMS, params);
	CK_AES_GCM_PARAMS *aes_gcm_params;
	caddr_t param;
	size_t paramlen;

	STRUCT_INIT(mech, mode);
	STRUCT_INIT(params, mode);
	bcopy(inmech, STRUCT_BUF(mech), STRUCT_SIZE(mech));
	param = STRUCT_FGETP(mech, cm_param);
	paramlen = STRUCT_FGET(mech, cm_param_len);

	if (paramlen != STRUCT_SIZE(params)) {
		*error = EINVAL;
		return (CRYPTO_ARGUMENTS_BAD);
	}

	outmech->cm_type = STRUCT_FGET(mech, cm_type);
	outmech->cm_param = NULL;
	outmech->cm_param_len = 0;
	if (inmech->cm_param != NULL) {
		size_t iv_len, auth_data_len, total_param_len;

		if (ddi_copyin(param, STRUCT_BUF(params), paramlen,
		    mode) != 0) {
			outmech->cm_param = NULL;
			*error = EFAULT;
			return (CRYPTO_FAILED);
		}

		iv_len = STRUCT_FGET(params, ulIvLen);
		auth_data_len = STRUCT_FGET(params, ulAADLen);

		/* allocate param structure */
		total_param_len =
		    sizeof (CK_AES_GCM_PARAMS) + iv_len + auth_data_len;
		aes_gcm_params = kmem_alloc(total_param_len, KM_SLEEP);
		aes_gcm_params->ulTagBits = STRUCT_FGET(params, ulTagBits);
		aes_gcm_params->ulIvLen = iv_len;
		aes_gcm_params->ulAADLen = auth_data_len;
		aes_gcm_params->pIv = (uchar_t *)aes_gcm_params +
		    sizeof (CK_AES_GCM_PARAMS);
		aes_gcm_params->pAAD = aes_gcm_params->pIv + iv_len;

		if (ddi_copyin((char *)STRUCT_FGETP(params, pIv),
		    aes_gcm_params->pIv, iv_len, mode) != 0) {
			kmem_free(aes_gcm_params, total_param_len);
			outmech->cm_param = NULL;
			*error = EFAULT;
			return (CRYPTO_FAILED);
		}
		if (ddi_copyin((char *)STRUCT_FGETP(params, pAAD),
		    aes_gcm_params->pAAD, auth_data_len, mode) != 0) {
			kmem_free(aes_gcm_params, total_param_len);
			outmech->cm_param = NULL;
			*error = EFAULT;
			return (CRYPTO_FAILED);
		}
		outmech->cm_param = (char *)aes_gcm_params;
		outmech->cm_param_len = sizeof (CK_AES_GCM_PARAMS);
	}

	return (CRYPTO_SUCCESS);
}

int
n2cp_aes_gmac_allocmech(crypto_mechanism_t *inmech, crypto_mechanism_t *outmech,
    int *error, int mode)
{
	STRUCT_DECL(crypto_mechanism, mech);
	STRUCT_DECL(CK_AES_GMAC_PARAMS, params);
	CK_AES_GMAC_PARAMS *aes_gmac_params;
	caddr_t param;
	size_t paramlen;

	STRUCT_INIT(mech, mode);
	STRUCT_INIT(params, mode);
	bcopy(inmech, STRUCT_BUF(mech), STRUCT_SIZE(mech));
	param = STRUCT_FGETP(mech, cm_param);
	paramlen = STRUCT_FGET(mech, cm_param_len);

	if (paramlen != STRUCT_SIZE(params)) {
		*error = EINVAL;
		return (CRYPTO_ARGUMENTS_BAD);
	}

	outmech->cm_type = STRUCT_FGET(mech, cm_type);
	outmech->cm_param = NULL;
	outmech->cm_param_len = 0;
	if (inmech->cm_param != NULL) {
		size_t auth_data_len, total_param_len;

		if (ddi_copyin(param, STRUCT_BUF(params), paramlen,
		    mode) != 0) {
			outmech->cm_param = NULL;
			*error = EFAULT;
			return (CRYPTO_FAILED);
		}

		auth_data_len = STRUCT_FGET(params, ulAADLen);

		/* allocate param structure */
		total_param_len = sizeof (CK_AES_GMAC_PARAMS) +
		    AES_GMAC_IV_LEN + auth_data_len;
		aes_gmac_params = kmem_alloc(total_param_len, KM_SLEEP);
		aes_gmac_params->ulAADLen = auth_data_len;
		aes_gmac_params->pIv = (uchar_t *)aes_gmac_params +
		    sizeof (CK_AES_GMAC_PARAMS);
		aes_gmac_params->pAAD = aes_gmac_params->pIv + AES_GMAC_IV_LEN;

		if (ddi_copyin((char *)STRUCT_FGETP(params, pIv),
		    aes_gmac_params->pIv, AES_GMAC_IV_LEN, mode) != 0) {
			kmem_free(aes_gmac_params, total_param_len);
			outmech->cm_param = NULL;
			*error = EFAULT;
			return (CRYPTO_FAILED);
		}
		if (ddi_copyin((char *)STRUCT_FGETP(params, pAAD),
		    aes_gmac_params->pAAD, auth_data_len, mode) != 0) {
			kmem_free(aes_gmac_params, total_param_len);
			outmech->cm_param = NULL;
			*error = EFAULT;
			return (CRYPTO_FAILED);
		}
		outmech->cm_param = (char *)aes_gmac_params;
		outmech->cm_param_len = sizeof (CK_AES_GMAC_PARAMS);
	}

	return (CRYPTO_SUCCESS);
}

int
n2cp_aes_ctr_freemech(crypto_mechanism_t *mech)
{
	if ((mech->cm_param != NULL) && (mech->cm_param_len != 0)) {
		kmem_free(mech->cm_param, mech->cm_param_len);
		mech->cm_param = NULL;
		mech->cm_param_len = 0;
	}
	return (CRYPTO_SUCCESS);
}

int
n2cp_aes_ccm_freemech(crypto_mechanism_t *mech)
{
	CK_AES_CCM_PARAMS *params;
	size_t total_param_len;

	if ((mech->cm_param != NULL) && (mech->cm_param_len != 0)) {
		/* LINTED: pointer alignment */
		params = (CK_AES_CCM_PARAMS *)mech->cm_param;
		total_param_len = mech->cm_param_len + params->ulNonceSize +
		    params->ulAuthDataSize;
		kmem_free(params, total_param_len);
		mech->cm_param = NULL;
		mech->cm_param_len = 0;
	}
	return (CRYPTO_SUCCESS);
}

int
n2cp_aes_gcm_freemech(crypto_mechanism_t *mech)
{
	CK_AES_GCM_PARAMS *params;
	size_t total_param_len;

	if ((mech->cm_param != NULL) && (mech->cm_param_len != 0)) {
		/* LINTED: pointer alignment */
		params = (CK_AES_GCM_PARAMS *)mech->cm_param;
		total_param_len = mech->cm_param_len + params->ulIvLen +
		    params->ulAADLen;
		kmem_free(params, total_param_len);
		mech->cm_param = NULL;
		mech->cm_param_len = 0;
	}
	return (CRYPTO_SUCCESS);
}

int
n2cp_aes_gmac_freemech(crypto_mechanism_t *mech)
{
	CK_AES_GMAC_PARAMS *params;
	size_t total_param_len;

	if ((mech->cm_param != NULL) && (mech->cm_param_len != 0)) {
		/* LINTED: pointer alignment */
		params = (CK_AES_GMAC_PARAMS *)mech->cm_param;
		total_param_len = mech->cm_param_len + AES_GMAC_IV_LEN +
		    params->ulAADLen;
		kmem_free(params, total_param_len);
		mech->cm_param = NULL;
		mech->cm_param_len = 0;
	}
	return (CRYPTO_SUCCESS);
}

/*
 * 'buf' is 128-bit Counter Block.
 */
static uint64_t
max_AES_CTR_bytes(unsigned char *buf, int ctrbits)
{
	uint64_t	ctr = 0;
	uint64_t	maxblocks;
	int		i;

/* EXPORT DELETE START */

	/*
	 * Process up to the point where lower 32-bit counter wraps,
	 * as the hardware does not increment bit 32
	 */
	if (ctrbits > 32) {
		ctrbits = 32;
	}

	for (i = 0; i < 4; ++i) {
		ctr = (ctr << 8) | buf[i + 12];
	}

	if (ctrbits == 32) {
		if (ctr == 0) {
			maxblocks = ((1ULL << 32) - 1);
		} else {
			maxblocks = (1ULL << 32) - ctr;
		}
	} else {
		/* strip out the non-counter part */

		ctr <<= 64 - ctrbits;
		ctr >>= 64 - ctrbits;

		maxblocks = (1ULL << ctrbits) - ctr;
	}

	ctr = maxblocks * AESBLOCK;


/* EXPORT DELETE END */

	return (ctr);
}

/*
 * 'buf' is 128-bit Counter Block.
 */
void
n2cp_add_ctr_bits(unsigned char *buf, uint64_t inc, int ctrbits)
{
	uint64_t	newctr;
	uint64_t	oldctr = 0;
	uint64_t	mask;
	int		i;
	int		carry = 0;

/* EXPORT DELETE START */

	/* Convert the lower 64-bit of the Counter Block into integer */
	for (i = 0; i < 8; ++i) {
		oldctr = (oldctr << 8) | buf[i + 8];
	}

	newctr = oldctr + inc;

	if (ctrbits < 64) {
		mask = 0 - (1ULL << ctrbits);
		/* mask looks like 11100000; 1's where it should not change */
		newctr = (newctr & ~mask) | (oldctr & mask);
	}

	/* if the old ctr is greater than new ctr, there is a carry */
	if (oldctr > newctr) {
		carry = 1;
	}

	/* Convert back to the byte array */
	for (i = 7; i >= 0; --i) {
		buf[i + 8] = newctr & 0xff;
		newctr >>= 8;
	}

	/*
	 * If ctrbits is greater than 64, and there is a carry over,
	 * increment the upper 64-bit by one.
	 */
	if ((ctrbits > 64) && (carry == 1)) {
		/* Convert the upper 64-bit of the Counter Block into integer */
		for (i = 0; i < 8; ++i) {
			oldctr = (oldctr << 8) | buf[i];
		}
		/* increment the upper 64 bits */
		newctr = oldctr + 1;
		if (ctrbits < 128) {
			mask = 0 - (1ULL << (ctrbits - 64));
			/*
			 * mask looks like 11100000; 1's where it should
			 * not change
			 */
			newctr = (newctr & ~mask) | (oldctr & mask);
		}
		/* Convert back to the byte array */
		for (i = 7; i >= 0; --i) {
			buf[i] = newctr & 0xff;
			newctr >>= 8;
		}
	}

/* EXPORT DELETE END */

}

/*
 * setup the context for subsequent use
 */
static int
alloc_des_request(n2cp_t *n2cp, crypto_mechanism_t *mechanism,
    crypto_key_t *key, int is_encrypt, n2cp_request_t **argreq)
{
	n2cp_request_t		*reqp = NULL;
	n2cp_block_ctx_t	*blockctx;
	uchar_t			*keyval;
	int			keylen;

/* EXPORT DELETE START */

	*argreq = NULL;

	if ((reqp = n2cp_getreq(n2cp)) == NULL) {
		DBG0(NULL, DWARN,
		    "alloc_des_request: unable to allocate request for DES");
		return (CRYPTO_HOST_MEMORY);
	}

	/* setup the DES context */
	blockctx = &(reqp->nr_context->blockctx);

	blockctx->residlen = 0;
	blockctx->lastblocklen = 0;

	/* store the key value */
	if (key->ck_format != CRYPTO_KEY_RAW) {
		/* the key must be passed by value */
		n2cp_freereq(reqp);
		return (CRYPTO_KEY_TYPE_INCONSISTENT);
	}
	keyval = key->ck_data;
	keylen = CRYPTO_BITS2BYTES(key->ck_length);
	switch (keylen) {
	case 8:
		if ((mechanism->cm_type != DES_CBC_MECH_INFO_TYPE) &&
		    (mechanism->cm_type != DES_CBC_PAD_MECH_INFO_TYPE) &&
		    (mechanism->cm_type != DES_ECB_MECH_INFO_TYPE) &&
		    (mechanism->cm_type != DES_CFB_MECH_INFO_TYPE)) {
			n2cp_freereq(reqp);
			return (CRYPTO_KEY_TYPE_INCONSISTENT);
		}
		BCOPY(keyval, blockctx->keyvalue, 8);
		blockctx->keylen = 8;
		break;
	case 16:
		/* DES2 Key: used for DES3 operation */
		if ((mechanism->cm_type != DES3_CBC_MECH_INFO_TYPE) &&
		    (mechanism->cm_type != DES3_CBC_PAD_MECH_INFO_TYPE) &&
		    (mechanism->cm_type != DES3_ECB_MECH_INFO_TYPE) &&
		    (mechanism->cm_type != DES3_CFB_MECH_INFO_TYPE)) {
			n2cp_freereq(reqp);
			return (CRYPTO_KEY_TYPE_INCONSISTENT);
		}
		BCOPY(keyval, blockctx->keyvalue, 16);
		BCOPY(keyval, blockctx->keyvalue + 16, 8);
		blockctx->keylen = 24;
		break;
	case 24:
		if ((mechanism->cm_type != DES3_CBC_MECH_INFO_TYPE) &&
		    (mechanism->cm_type != DES3_CBC_PAD_MECH_INFO_TYPE) &&
		    (mechanism->cm_type != DES3_ECB_MECH_INFO_TYPE) &&
		    (mechanism->cm_type != DES3_CFB_MECH_INFO_TYPE)) {
			n2cp_freereq(reqp);
			return (CRYPTO_KEY_TYPE_INCONSISTENT);
		}
		BCOPY(keyval, blockctx->keyvalue, 24);
		blockctx->keylen = 24;
		break;
	default:
		/* the key length must be either 8, 16, or 24 */
		n2cp_freereq(reqp);
		DBG1(reqp->nr_n2cp, DCHATTY, "alloc_des_request: Invalid "
		    "key size [%d]\n", keylen);
		return (CRYPTO_KEY_SIZE_RANGE);
	}

	/* store IV */
	switch (mechanism->cm_type) {
	case DES_CBC_MECH_INFO_TYPE:
	case DES3_CBC_MECH_INFO_TYPE:
	case DES_CBC_PAD_MECH_INFO_TYPE:
	case DES3_CBC_PAD_MECH_INFO_TYPE:
	case DES_CFB_MECH_INFO_TYPE:
	case DES3_CFB_MECH_INFO_TYPE:
		if (mechanism->cm_param_len == DESBLOCK) {
			BCOPY(mechanism->cm_param, blockctx->nextiv, DESBLOCK);
			blockctx->nextivlen = mechanism->cm_param_len;
		} else if (mechanism->cm_param_len == 0) {
			/* parameter may be specified at data.misc_data */
			blockctx->nextivlen = 0;
		} else {
			n2cp_freereq(reqp);
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
		break;
	case DES_ECB_MECH_INFO_TYPE:
	case DES3_ECB_MECH_INFO_TYPE:
		if (mechanism->cm_param_len != 0) {
			n2cp_freereq(reqp);
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
		blockctx->nextivlen = 0;
		break;
	}

	reqp->nr_cmd = mechanism->cm_type;
	reqp->nr_cmd |= (is_encrypt ? N2CP_OP_ENCRYPT : N2CP_OP_DECRYPT);
	reqp->nr_cmd |= (isCBCPAD(reqp->nr_cmd) ? N2CP_OP_PROCESS_PAD : 0);
	reqp->nr_n2cp = n2cp;
	reqp->nr_blocksz = DESBLOCK;

	*argreq = reqp;

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

static int
alloc_aes_request(n2cp_t *n2cp, crypto_mechanism_t *mechanism,
    crypto_key_t *key, int is_encrypt, n2cp_request_t **argreq)
{
	n2cp_request_t	*reqp = NULL;
	n2cp_block_ctx_t	*blockctx;
	uchar_t		*keyval;
	int		keylen;
	int rv;

/* EXPORT DELETE START */

	*argreq = NULL;

	if ((reqp = n2cp_getreq(n2cp)) == NULL) {
		DBG0(NULL, DWARN,
		    "alloc_aes_request: unable to allocate request for AES");
		return (CRYPTO_HOST_MEMORY);
	}

	/* setup the AES context */
	blockctx = &(reqp->nr_context->blockctx);

	blockctx->residlen = 0;
	blockctx->lastblocklen = 0;

	/* store the key value */
	if (key->ck_format != CRYPTO_KEY_RAW) {
		/* the key must be passed by value */
		n2cp_freereq(reqp);
		return (CRYPTO_KEY_TYPE_INCONSISTENT);
	}
	keyval = key->ck_data;
	keylen = CRYPTO_BITS2BYTES(key->ck_length);
	switch (keylen) {
	case 16:
	case 24:
	case 32:
		blockctx->keylen = keylen;
		BCOPY(keyval, blockctx->keyvalue, keylen);
		break;
	default:
		/* the key length must be either 16, 24, or 32 */
		n2cp_freereq(reqp);
		DBG1(reqp->nr_n2cp, DCHATTY, "alloc_aes_request: Invalid "
		    "key size [%d]\n", keylen);
		return (CRYPTO_KEY_SIZE_RANGE);
	}

	/* store IV */
	switch (mechanism->cm_type & N2CP_CMD_MASK) {
	case AES_CBC_MECH_INFO_TYPE:
	case AES_CFB_MECH_INFO_TYPE:
	case AES_CBC_PAD_MECH_INFO_TYPE:
		if (mechanism->cm_param_len == AESBLOCK) {
			BCOPY(mechanism->cm_param, blockctx->nextiv, AESBLOCK);
			blockctx->nextivlen = mechanism->cm_param_len;
		} else if (mechanism->cm_param_len == 0) {
			/* parameter may be specified at data.misc_data */
			blockctx->nextivlen = 0;
		} else {
			n2cp_freereq(reqp);
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
		break;
	case AES_ECB_MECH_INFO_TYPE:
		if (mechanism->cm_param_len != 0) {
			n2cp_freereq(reqp);
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
		blockctx->nextivlen = 0;
		break;
	case AES_CTR_MECH_INFO_TYPE:
	{
		CK_AES_CTR_PARAMS	*aesctr;
		if (mechanism->cm_param_len != sizeof (CK_AES_CTR_PARAMS)) {
			n2cp_freereq(reqp);
			return (CRYPTO_MECHANISM_PARAM_INVALID);
		}
		aesctr = (CK_AES_CTR_PARAMS *)(void *)mechanism->cm_param;
		BCOPY(aesctr->cb, blockctx->nextiv, AESBLOCK);
		blockctx->nextivlen = AESBLOCK;
		blockctx->ctrbits = aesctr->ulCounterBits;
		break;
	}
	case AES_CCM_MECH_INFO_TYPE:
		reqp->nr_mode_ctx = ccm_alloc_ctx(KM_SLEEP);
		((ccm_ctx_t *)reqp->nr_mode_ctx)->ccm_keysched = reqp;
		break;
	case AES_GCM_MECH_INFO_TYPE:
		reqp->nr_mode_ctx = gcm_alloc_ctx(KM_SLEEP);
		((gcm_ctx_t *)reqp->nr_mode_ctx)->gcm_keysched = reqp;
		break;
	case AES_GMAC_MECH_INFO_TYPE:
		reqp->nr_mode_ctx = gmac_alloc_ctx(KM_SLEEP);
		((gcm_ctx_t *)reqp->nr_mode_ctx)->gcm_keysched = reqp;
		break;
	default:
		return (CRYPTO_MECHANISM_INVALID);
	}

	reqp->nr_cmd = mechanism->cm_type;
	reqp->nr_cmd |= (is_encrypt ? N2CP_OP_ENCRYPT : N2CP_OP_DECRYPT);
	reqp->nr_cmd |= (isCBCPAD(reqp->nr_cmd) ? N2CP_OP_PROCESS_PAD : 0);
	reqp->nr_n2cp = n2cp;
	reqp->nr_blocksz = AESBLOCK;

	if ((mechanism->cm_type & N2CP_CMD_MASK) == AES_CCM_MECH_INFO_TYPE) {
		rv = ccm_init_ctx((ccm_ctx_t *)reqp->nr_mode_ctx,
		    mechanism->cm_param, KM_SLEEP, is_encrypt,
		    AESBLOCK, n2cp_encrypt_block, aes_xor_block,
		    B_FALSE /* don't save fp registers */);
		if (rv != CRYPTO_SUCCESS) {
			return (rv);
		}
	}
	if ((mechanism->cm_type & N2CP_CMD_MASK) == AES_GCM_MECH_INFO_TYPE) {
		rv = gcm_init_ctx((gcm_ctx_t *)reqp->nr_mode_ctx,
		    mechanism->cm_param, AESBLOCK, n2cp_encrypt_block,
		    aes_copy_block, aes_xor_block,
		    B_FALSE /* don't save fp registers */);
		if (rv != CRYPTO_SUCCESS) {
			return (rv);
		}
	}
	if ((mechanism->cm_type & N2CP_CMD_MASK) == AES_GMAC_MECH_INFO_TYPE) {
		rv = gmac_init_ctx((gcm_ctx_t *)reqp->nr_mode_ctx,
		    mechanism->cm_param, AESBLOCK, n2cp_encrypt_block,
		    aes_copy_block, aes_xor_block,
		    B_FALSE /* don't save fp registers */);
		if (rv != CRYPTO_SUCCESS) {
			return (rv);
		}

		/* increment counter before using it */
		++((gcm_ctx_t *)reqp->nr_mode_ctx)->gcm_cb[1];
	}
	*argreq = reqp;

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}


static int
alloc_rc4_request(n2cp_t *n2cp, crypto_mechanism_t *mechanism,
    crypto_key_t *key, int is_encrypt, n2cp_request_t **argreq)
{
	n2cp_request_t		*reqp = NULL;
	n2cp_block_ctx_t	*blockctx;
	uchar_t			*keyval;
	int			keylen;
	rc4_key_t		*rc4key;
	uchar_t			ext_keyword[RC4_MAX_KEY_LEN];
	uchar_t			tmp;
	int			i, j;

/* EXPORT DELETE START */

	*argreq = NULL;

	if ((reqp = n2cp_getreq(n2cp)) == NULL) {
		DBG0(NULL, DWARN,
		    "alloc_rc4_request: unable to allocate request for RC4");
		return (CRYPTO_HOST_MEMORY);
	}

	/* setup the RC4 context */
	blockctx = &(reqp->nr_context->blockctx);

	blockctx->residlen = 0;
	blockctx->lastblocklen = 0;

	/* store the key value */
	if (key->ck_format != CRYPTO_KEY_RAW) {
		/* the key must be passed by value */
		n2cp_freereq(reqp);
		return (CRYPTO_KEY_TYPE_INCONSISTENT);
	}
	keyval = key->ck_data;
	keylen = CRYPTO_BITS2BYTES(key->ck_length);
	if ((keylen < RC4_MIN_KEY_LEN) || (keylen > RC4_MAX_KEY_LEN)) {
		n2cp_freereq(reqp);
		DBG1(reqp->nr_n2cp, DCHATTY, "alloc_rc4_request: Invalid "
		    "key size [%d]\n", keylen);
		return (CRYPTO_KEY_SIZE_RANGE);
	}

	blockctx->keylen = keylen;
	rc4key = &(blockctx->rc4keyvalue);
	for (i = j = 0; i < RC4_MAX_KEY_LEN; i++, j++) {
		if (j == keylen)
			j = 0;
		ext_keyword[i] = keyval[j];
	}
	for (i = 0; i < RC4_MAX_KEY_LEN; i++) {
		rc4key->key[i] = (uchar_t)i;
	}
	for (i = j = 0; i < RC4_MAX_KEY_LEN; i++) {
		j = (j + rc4key->key[i] + ext_keyword[i]) % RC4_MAX_KEY_LEN;
		tmp = rc4key->key[i];
		rc4key->key[i] = rc4key->key[j];
		rc4key->key[j] = tmp;
	}
	rc4key->i = 0;
	rc4key->j = 0;

	reqp->nr_cmd = mechanism->cm_type;
	reqp->nr_cmd |= (is_encrypt ? N2CP_OP_ENCRYPT : N2CP_OP_DECRYPT);
	reqp->nr_n2cp = n2cp;
	reqp->nr_blocksz = 1;

	*argreq = reqp;

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

int
n2cp_blockinit(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism,
    crypto_key_t *key, int is_encrypt)
{
	n2cp_t		*n2cp = (n2cp_t *)ctx->cc_provider;
	n2cp_request_t	*reqp;
	int		rv;

/* EXPORT DELETE START */

	if (isDES(mechanism->cm_type)) {
		rv = alloc_des_request(n2cp, mechanism, key,
		    is_encrypt, &reqp);
	} else if (isAES(mechanism->cm_type)) {
		rv = alloc_aes_request(n2cp, mechanism, key,
		    is_encrypt, &reqp);
	} else if (isRC4(mechanism->cm_type)) {
		rv = alloc_rc4_request(n2cp, mechanism, key,
		    is_encrypt, &reqp);
		if (rv == CRYPTO_SUCCESS &&
		    ctx->cc_flags & CRYPTO_INIT_OPSTATE) {
			ctx->cc_opstate =
			    &(reqp->nr_context->blockctx.rc4keyvalue);
			ctx->cc_flags |= CRYPTO_USE_OPSTATE;
		}
	} else {
		return (CRYPTO_MECHANISM_INVALID);
	}
	if (rv == CRYPTO_SUCCESS) {
		ctx->cc_provider_private = reqp;
		/* release ulcwq buffer */
		check_draining(reqp);
	}

/* EXPORT DELETE END */

	return (rv);
}

void
n2cp_clean_blockctx(n2cp_request_t *reqp)
{
	/* clear the key value field - direct write faster than bzero */
	reqp->nr_context->blockctx.keystruct.val64[0]  = 0;
	reqp->nr_context->blockctx.keystruct.val64[1]  = 0;
	reqp->nr_context->blockctx.keystruct.val64[2]  = 0;
	reqp->nr_context->blockctx.keystruct.val64[3]  = 0;
}


int
n2cp_block(crypto_ctx_t *ctx, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	int rv;
	n2cp_request_t	*reqp = (n2cp_request_t *)(ctx->cc_provider_private);
	n2cp_block_ctx_t *blockctx = &(reqp->nr_context->blockctx);

/* EXPORT DELETE START */

	/* if input is zero byte, there is nothing to process */
	if (((reqp->nr_cmd & N2CP_CMD_MASK) != AES_GCM_MECH_INFO_TYPE) &&
	    ((reqp->nr_cmd & N2CP_CMD_MASK) != AES_CCM_MECH_INFO_TYPE) &&
	    ((reqp->nr_cmd & N2CP_CMD_MASK) != AES_GMAC_MECH_INFO_TYPE) &&
	    in->cd_length == 0) {
		return (CRYPTO_SUCCESS);
	}

	/* check input and output size */
	switch (reqp->nr_cmd & N2CP_CMD_MASK) {
	case AES_CTR_MECH_INFO_TYPE:
	case AES_CFB_MECH_INFO_TYPE:
		/* Input can be any length. Output must be as big as input */
		if (out->cd_length < in->cd_length) {
			out->cd_length = in->cd_length;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
		break;

	case AES_CBC_PAD_MECH_INFO_TYPE:
	case DES_CBC_PAD_MECH_INFO_TYPE:
	case DES3_CBC_PAD_MECH_INFO_TYPE:
	{
		int	len;
		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			/*
			 * Input can be any length. Output must large enough
			 * to contain the input and the padding.
			 */
			len = ROUNDDOWN(in->cd_length + reqp->nr_blocksz,
			    reqp->nr_blocksz);
			if (out->cd_length < len) {
				out->cd_length = len;
				return (CRYPTO_BUFFER_TOO_SMALL);
			}
		} else {
			/* the input must be multiple of BLOCKSZ */
			if (in->cd_length % reqp->nr_blocksz) {
				return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
			}
			/* Output must be larger than input - padding */
			len = in->cd_length - reqp->nr_blocksz;
			if (out->cd_length < len) {
				out->cd_length = in->cd_length;
				return (CRYPTO_BUFFER_TOO_SMALL);
			}
		}
		break;
	}

	case AES_CCM_MECH_INFO_TYPE:
	{
		ccm_ctx_t *ccm_ctx = (ccm_ctx_t *)reqp->nr_mode_ctx;
		size_t need;

		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			need = in->cd_length + ccm_ctx->ccm_mac_len;
		} else {
			need = ccm_ctx->ccm_data_len;
		}
		if (out->cd_length < need) {
			out->cd_length = need;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
		break;
	}

	case AES_GCM_MECH_INFO_TYPE:
	{
		gcm_ctx_t *gcm_ctx = (gcm_ctx_t *)reqp->nr_mode_ctx;
		size_t need;

		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			need = in->cd_length + gcm_ctx->gcm_tag_len;
		} else {
			need = in->cd_length - gcm_ctx->gcm_tag_len;
		}
		if (out->cd_length < need) {
			out->cd_length = need;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
		break;
	}

	case AES_GMAC_MECH_INFO_TYPE:
	{
		gcm_ctx_t *gcm_ctx = (gcm_ctx_t *)reqp->nr_mode_ctx;
		size_t need;

		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			if (in->cd_length != 0)
				return (CRYPTO_ARGUMENTS_BAD);
			need = gcm_ctx->gcm_tag_len;
		} else {
			if (out->cd_length != 0)
				return (CRYPTO_ARGUMENTS_BAD);
			need = 0;
		}
		if (out->cd_length < need) {
			out->cd_length = need;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
		break;
	}

	default:
		/* the input must be multiple of BLOCKSZ */
		if (in->cd_length % reqp->nr_blocksz) {
			if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
				return (CRYPTO_DATA_LEN_RANGE);
			} else {
				return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
			}
		}
		/* Output must be as big as input */
		if (out->cd_length < in->cd_length) {
			out->cd_length = in->cd_length;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
	}

	/* IV is passed through crypto_data */
	if (in->cd_miscdata != NULL) {
		if (isCTR(reqp->nr_cmd)) {
			CK_AES_CTR_PARAMS	*ctrparam;
			ctrparam = (CK_AES_CTR_PARAMS *)(void *)in->cd_miscdata;
			BCOPY(ctrparam->cb, blockctx->nextiv, reqp->nr_blocksz);
			blockctx->nextivlen = reqp->nr_blocksz;
			blockctx->ctrbits = ctrparam->ulCounterBits;
		} else {
			BCOPY(in->cd_miscdata, blockctx->nextiv,
			    reqp->nr_blocksz);
			blockctx->nextivlen = reqp->nr_blocksz;
		}
	}

	if (isCCM(reqp->nr_cmd)) {
		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			off_t saved_offset = out->cd_offset;

			rv = ccm_encrypt(reqp, in, out, kcfreq);
			if (rv != CRYPTO_SUCCESS)
				goto out;

			rv = mac_final(reqp, kcfreq);
			if (rv != CRYPTO_SUCCESS)
				goto out;

			/* set offset to start of MAC */
			rv = ccm_encrypt_final(reqp->nr_mode_ctx, out, AESBLOCK,
			    n2cp_encrypt_block, aes_xor_block);
			if (rv != CRYPTO_SUCCESS) {
				out->cd_offset = saved_offset;
				goto out;
			}
			/* movement of offset indicates data processed */
			out->cd_length = out->cd_offset - saved_offset;
			out->cd_offset = saved_offset;
		} else {
			rv = ccm_decrypt(reqp, in, kcfreq);
			if (rv != CRYPTO_SUCCESS)
				goto out;
			rv = n2cp_ccm_decrypt_final(reqp, out, kcfreq);
		}
out:
		reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
		n2cp_free_buf(reqp->nr_n2cp, reqp);
		return (rv);
	}

	if (isGCM(reqp->nr_cmd) || isGMAC(reqp->nr_cmd)) {
		rv = gcm_process(reqp, in, out, kcfreq);
		reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
		n2cp_free_buf(reqp->nr_n2cp, reqp);
		return (rv);
	}
	N2CP_REQ_SETUP(reqp, in, out);

	reqp->nr_callback = block_done;
	reqp->nr_kcfreq = kcfreq;
	reqp->nr_cmd |= N2CP_OP_SINGLE;
	rv = block_start(reqp);

	reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
	n2cp_free_buf(reqp->nr_n2cp, reqp);

/* EXPORT DELETE END */

	return (rv);
}

int
n2cp_blockupdate(crypto_ctx_t *ctx, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t *kcfreq)
{
	int		rv;
	size_t		inlen;
	n2cp_request_t	*reqp = (n2cp_request_t *)(ctx->cc_provider_private);
	n2cp_block_ctx_t *blockctx = &(reqp->nr_context->blockctx);

/* EXPORT DELETE START */

	/* if input is zero byte, there is nothing to process */
	if (in->cd_length == 0) {
		out->cd_length = 0;
		return (CRYPTO_SUCCESS);
	}

	inlen  = in->cd_length + blockctx->residlen + blockctx->lastblocklen;
	if (isGCM(reqp->nr_cmd)) {
		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			if (out->cd_length < in->cd_length) {
				out->cd_length = in->cd_length;
				return (CRYPTO_BUFFER_TOO_SMALL);
			}
		}
	} else if (isGMAC(reqp->nr_cmd)) {
		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			if (in->cd_length != 0)
				return (CRYPTO_ARGUMENTS_BAD);
		}
	} else if (STREAM_CIPHER(reqp->nr_cmd)) {
		/*
		 * Stream cipher modes always return the same amount of output
		 * as the input
		 */
		if (out->cd_length < in->cd_length) {
			out->cd_length = in->cd_length;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
	} else if (isCBCPAD(reqp->nr_cmd) &&
	    (reqp->nr_cmd & N2CP_OP_DECRYPT)) {
		inlen = ROUNDUP(inlen - reqp->nr_blocksz, reqp->nr_blocksz);
		if (out->cd_length < inlen) {
			out->cd_length = inlen;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
	} else if (out->cd_length < ROUNDDOWN(inlen, reqp->nr_blocksz)) {
		out->cd_length = ROUNDDOWN(inlen, reqp->nr_blocksz);
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	/* IV is passed through crypto_data */
	if (in->cd_miscdata != NULL) {
		if (isCTR(reqp->nr_cmd)) {
			CK_AES_CTR_PARAMS	*ctrparam;
			ctrparam = (CK_AES_CTR_PARAMS *)(void *)in->cd_miscdata;
			BCOPY(ctrparam->cb, blockctx->nextiv, reqp->nr_blocksz);
			blockctx->nextivlen = reqp->nr_blocksz;
			blockctx->ctrbits = ctrparam->ulCounterBits;
		} else {
			BCOPY(in->cd_miscdata, blockctx->nextiv,
			    reqp->nr_blocksz);
			blockctx->nextivlen = reqp->nr_blocksz;
		}
	}

	if (isCCM(reqp->nr_cmd)) {
		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			rv = ccm_encrypt(reqp, in, out, kcfreq);
		} else {
			rv = ccm_decrypt(reqp, in, kcfreq);
		}
		reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
		n2cp_free_buf(reqp->nr_n2cp, reqp);
		return (rv);
	} else if (isGCM(reqp->nr_cmd) || isGMAC(reqp->nr_cmd)) {
		gcm_ctx_t *ctx = (gcm_ctx_t *)reqp->nr_mode_ctx;
		off_t saved_offset = out->cd_offset;

		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			blockctx->residlen = 0;
			rv = do_ctr(reqp, in, out, kcfreq);
			if (rv != CRYPTO_SUCCESS) {
				out->cd_length = 0;
				out->cd_offset = saved_offset;
				goto out;
			}

			ctx->gcm_processed_data_len += in->cd_length;

			/* movement of offset indicates data processed */
			out->cd_length = out->cd_offset - saved_offset;
			out->cd_offset = saved_offset;
			update_gcm_hash(ctx, out->cd_raw.iov_base,
			    out->cd_length);
		} else {
			rv = gcm_decrypt_update(reqp, in);
			/* don't return plaintext until the final */
			out->cd_length = 0;
		}
out:
		reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
		n2cp_free_buf(reqp->nr_n2cp, reqp);
		return (rv);
	}

	N2CP_REQ_SETUP(reqp, in, out);

	reqp->nr_callback = block_done;
	reqp->nr_kcfreq = kcfreq;
	reqp->nr_cmd |= N2CP_OP_MULTI;

	rv = block_start(reqp);

	reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
	n2cp_free_buf(reqp->nr_n2cp, reqp);

/* EXPORT DELETE END */

	return (rv);

}

/*ARGSUSED2*/
int
n2cp_blockfinal(crypto_ctx_t *ctx, crypto_data_t *out,
    crypto_req_handle_t *kcfreq)
{
	n2cp_request_t	*reqp = (n2cp_request_t *)(ctx->cc_provider_private);
	n2cp_block_ctx_t	*blockctx = &(reqp->nr_context->blockctx);
	int rv;

/* EXPORT DELETE START */

	switch (reqp->nr_cmd & N2CP_CMD_MASK) {
	case AES_GCM_MECH_INFO_TYPE:
	case AES_GMAC_MECH_INFO_TYPE:
		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			gcm_ctx_t *ctx = (gcm_ctx_t *)reqp->nr_mode_ctx;
			size_t len;
			uint8_t *ghash;
			size_t need;

			ghash = (uint8_t *)ctx->gcm_ghash;
			len = ctx->gcm_processed_data_len;
			need = ctx->gcm_tag_len;
			if (out->cd_length < need) {
				out->cd_length = need;
				return (CRYPTO_BUFFER_TOO_SMALL);
			}

			if ((rv = gcm_hash_final(ctx, len)) != CRYPTO_SUCCESS)
				return (rv);

			out->cd_length = 0;
			rv = n2cp_scatter((char *)ghash, out, ctx->gcm_tag_len);
		} else {
			rv = n2cp_gcm_decrypt_final(reqp, out, kcfreq);
		}
		return (rv);
	case AES_CCM_MECH_INFO_TYPE:
		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			off_t saved_offset = out->cd_offset;

			rv = mac_final(reqp, kcfreq);
			if (rv != CRYPTO_SUCCESS) {
				out->cd_offset = saved_offset;
				return (rv);
			}

			rv = ccm_encrypt_final(reqp->nr_mode_ctx, out, AESBLOCK,
			    n2cp_encrypt_block, aes_xor_block);
			if (rv != CRYPTO_SUCCESS) {
				out->cd_offset = saved_offset;
				return (rv);
			}
			/* movement of offset indicates data processed */
			out->cd_length = out->cd_offset - saved_offset;
			out->cd_offset = saved_offset;
		} else {
			rv = n2cp_ccm_decrypt_final(reqp, out, kcfreq);
		}
		return (rv);
	case AES_CBC_PAD_MECH_INFO_TYPE:
	case DES_CBC_PAD_MECH_INFO_TYPE:
	case DES3_CBC_PAD_MECH_INFO_TYPE:
		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			/*
			 * If this is a CBC PAD encryption, the input needs not
			 * be * multiple of blocksz.  Pad the input, encrypt
			 * the buffer, and return.
			 */
			reqp->nr_in = NULL;
			reqp->nr_out = out;
			reqp->nr_kcfreq = kcfreq;
			reqp->nr_cmd |= N2CP_OP_MULTI;
			reqp->nr_out->cd_length = 0;
			reqp->nr_callback = block_done;
			return (block_final_start(reqp));
		} else {
			int rv;

			/*
			 * This is a CBC PAD Decryption. Total input length
			 * must be multiple of the blocksz
			 */
			if (blockctx->residlen != 0) {
				return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
			}
			/*
			 * Remove the padding from the previously recovered
			 * message, and return the unpadded data.
			 */
			if (blockctx->lastblocklen != reqp->nr_blocksz) {
				return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
			}
			out->cd_length = 0;
			rv = n2cp_scatter_PKCS_unpad(blockctx->lastblock,
			    blockctx->lastblocklen, out, reqp->nr_blocksz);
			blockctx->lastblocklen = 0;
			/* padding was removed */
			reqp->nr_cmd &= ~N2CP_OP_PROCESS_PAD;
			return (rv);
		}
	default:
		if (blockctx->residlen != 0) {
			/*
			 * Illegal input length. Total input length
			 * was not multiple of the blocksz.
			 * For AES CTR mode, residlen is always zero since
			 * input is always processed.
			 */
			DBG1(NULL, DCHATTY, "invalid nonzero residual (%d)",
			    blockctx->residlen);
			if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
				return (CRYPTO_DATA_LEN_RANGE);
			} else {
				return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
			}
		}
	}

	out->cd_length = 0;

/* EXPORT DELETE END */
	return (CRYPTO_SUCCESS);
}


int
n2cp_blockatomic(n2cp_t *n2cp, crypto_mechanism_t *mechanism, crypto_key_t *key,
    crypto_data_t *in, crypto_data_t *out, crypto_req_handle_t kcfreq,
    int is_encrypt)
{
	int			rv;
	n2cp_block_ctx_t	*blockctx;
	n2cp_request_t		*reqp;

/* EXPORT DELETE START */

	if (isDES(mechanism->cm_type)) {
		rv = alloc_des_request(n2cp, mechanism, key,
		    is_encrypt, &reqp);
	} else if (isAES(mechanism->cm_type)) {
		rv = alloc_aes_request(n2cp, mechanism, key,
		    is_encrypt, &reqp);
	} else if (isRC4(mechanism->cm_type)) {
		rv = alloc_rc4_request(n2cp, mechanism, key,
		    is_encrypt, &reqp);
	} else {
		return (CRYPTO_MECHANISM_INVALID);
	}
	if (rv != CRYPTO_SUCCESS) {
		return (rv);
	}
	blockctx = &(reqp->nr_context->blockctx);

	switch (reqp->nr_cmd & N2CP_CMD_MASK) {
	case AES_CTR_MECH_INFO_TYPE:
	case AES_CFB_MECH_INFO_TYPE:
		/* Input can be any length. Output must be as big as input */
		if (out->cd_length < in->cd_length) {
			out->cd_length = in->cd_length;
			n2cp_freereq(reqp);
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
		break;
	case AES_CBC_PAD_MECH_INFO_TYPE:
	case DES_CBC_PAD_MECH_INFO_TYPE:
	case DES3_CBC_PAD_MECH_INFO_TYPE:
	{
		int	len;
		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			/*
			 * Input can be any length. Output must large enough
			 * to contain the input and the padding.
			 */
			len = ROUNDDOWN(in->cd_length + reqp->nr_blocksz,
			    reqp->nr_blocksz);
			if (out->cd_length < len) {
				out->cd_length = len;
				n2cp_freereq(reqp);
				return (CRYPTO_BUFFER_TOO_SMALL);
			}
		} else {
			/* the input must be multiple of BLOCKSZ */
			if (in->cd_length % reqp->nr_blocksz) {
				n2cp_freereq(reqp);
				return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
			}
			/* Output must be larger than (input - padding) */
			len = in->cd_length - reqp->nr_blocksz;
			if (out->cd_length < len) {
				out->cd_length = in->cd_length;
				n2cp_freereq(reqp);
				return (CRYPTO_BUFFER_TOO_SMALL);
			}
		}
	}
	break;
	case AES_CCM_MECH_INFO_TYPE:
	{
		ccm_ctx_t *ccm_ctx = (ccm_ctx_t *)reqp->nr_mode_ctx;
		size_t need;

		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			need = in->cd_length + ccm_ctx->ccm_mac_len;
		} else {
			need = ccm_ctx->ccm_data_len;
		}
		if (out->cd_length < need) {
			out->cd_length = need;
			n2cp_freereq(reqp);
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
	}
	break;
	case AES_GCM_MECH_INFO_TYPE:
	case AES_GMAC_MECH_INFO_TYPE:
	{
		gcm_ctx_t *gcm_ctx = (gcm_ctx_t *)reqp->nr_mode_ctx;
		size_t need;

		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			need = in->cd_length + gcm_ctx->gcm_tag_len;
		} else {
			need = in->cd_length - gcm_ctx->gcm_tag_len;
		}
		if (out->cd_length < need) {
			out->cd_length = need;
			n2cp_freereq(reqp);
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
	}
	break;
	default:
		/* the input must be multiple of BLOCKSZ */
		if (in->cd_length % reqp->nr_blocksz) {
			if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
				n2cp_freereq(reqp);
				return (CRYPTO_DATA_LEN_RANGE);
			} else {
				n2cp_freereq(reqp);
				return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
			}
		}
		/* Output must be as big as input */
		if (out->cd_length < in->cd_length) {
			out->cd_length = in->cd_length;
			n2cp_freereq(reqp);
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
	}

	/* IV is passed through crypto_data */
	if (in->cd_miscdata != NULL) {
		if (isCTR(reqp->nr_cmd)) {
			CK_AES_CTR_PARAMS	*ctrparam;
			ctrparam = (CK_AES_CTR_PARAMS *)(void *)in->cd_miscdata;
			BCOPY(ctrparam->cb, blockctx->nextiv, reqp->nr_blocksz);
			blockctx->nextivlen = reqp->nr_blocksz;
			blockctx->ctrbits = ctrparam->ulCounterBits;
		} else {
			BCOPY(in->cd_miscdata, blockctx->nextiv,
			    reqp->nr_blocksz);
			blockctx->nextivlen = reqp->nr_blocksz;
		}
	}

	if (isCCM(reqp->nr_cmd)) {
		ccm_ctx_t *ccm_ctx = (ccm_ctx_t *)reqp->nr_mode_ctx;
		off_t saved_length = out->cd_length;
		off_t saved_offset = out->cd_offset;

		if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
			rv = ccm_encrypt(reqp, in, out, kcfreq);
			if (rv != CRYPTO_SUCCESS)
				goto fail;

			rv = mac_final(reqp, kcfreq);
			if (rv != CRYPTO_SUCCESS)
				goto fail;

			rv = ccm_encrypt_final(ccm_ctx, out, AESBLOCK,
			    n2cp_encrypt_block, aes_xor_block);
			if (rv != CRYPTO_SUCCESS)
				goto fail;
		} else {
			rv = ccm_decrypt(reqp, in, kcfreq);
			if (rv != CRYPTO_SUCCESS)
				goto fail;
			rv = n2cp_ccm_decrypt_final(reqp, out, kcfreq);
			if (rv != CRYPTO_SUCCESS)
				goto fail;
		}
		/* movement of offset indicates data processed */
		out->cd_length = out->cd_offset - saved_offset;
		out->cd_offset = saved_offset;
		goto success;
fail:
		out->cd_length = saved_length;
		out->cd_offset = saved_offset;
success:
		reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
		n2cp_free_buf(reqp->nr_n2cp, reqp);
		n2cp_freereq(reqp);
		return (rv);
	} else if (isGCM(reqp->nr_cmd) || isGMAC(reqp->nr_cmd)) {
		rv = gcm_process(reqp, in, out, kcfreq);
		reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
		n2cp_free_buf(reqp->nr_n2cp, reqp);
		n2cp_freereq(reqp);
		return (rv);
	}
	N2CP_REQ_SETUP(reqp, in, out);

	reqp->nr_callback = block_done;
	reqp->nr_kcfreq = kcfreq;
	reqp->nr_cmd |= N2CP_OP_SINGLE;

	rv = block_start(reqp);

	reqp->nr_flags &= ~(N2CP_GATHER | N2CP_SCATTER);
	n2cp_free_buf(n2cp, reqp);
	n2cp_freereq(reqp);

/* EXPORT DELETE END */

	return (rv);
}

#ifdef DEBUG
/* histogram interval */
int	n2cp_hist_interval = N2CP_HIST_INTERVAL;

void
n2cp_histogram(n2cp_request_t *reqp, hrtime_t msec)
{
	cwq_entry_t	*cep;
	cwq_t		*cwq;
	int		interval;

	cep = n2cp_map_findcwq(reqp->nr_n2cp, reqp->nr_cwq_id);

	if (!cep)
		return;

	cwq = &cep->mm_queue;

	interval = msec / n2cp_hist_interval;

	if (interval >= N2CP_MAX_INTERVALS)
		interval = N2CP_MAX_INTERVALS - 1;

	atomic_inc_64(&cwq->cq_ks.qks_histogram[interval]);
}
#endif /* DEBUG */

static int
block_start(n2cp_request_t *reqp)
{
	n2cp_block_ctx_t	*blockctx = &(reqp->nr_context->blockctx);
	n2cp_t		*n2cp = (n2cp_t *)reqp->nr_n2cp;
	cwq_cw_t	*cb;
	crypto_data_t	*in = reqp->nr_in;
	char		*in_addr;
	uint64_t	maxinlen, inlen, len;
	int		rv;
	int		aes_type;

/* EXPORT DELETE START */
#ifdef	DEBUG
	hrtime_t	start, end, nsec;
	if (n2cp_hist) {
		start = gethrtime();
	}
#endif
	/* if not bound to the cep (atomic case), bind it here */
	if (n2cp_use_ulcwq &&
	    (curthread->t_affinitycnt <= 0) &&
	    (curthread->t_bound_cpu == NULL) &&
	    (rv = n2cp_find_cep_for_req(reqp) != CRYPTO_SUCCESS)) {
		DBG1(NULL, DWARN, "block_start: n2cp_find_cep_for_req"
		    "failed with 0x%x", rv);
		return (rv);
	}

	if (isCTR(reqp->nr_cmd)) {
		maxinlen =  max_AES_CTR_bytes(blockctx->nextiv,
		    blockctx->ctrbits);
		maxinlen = (maxinlen > MAX_DATA_LEN) ? MAX_DATA_LEN : maxinlen;
	} else {
		maxinlen = ROUNDDOWN(MAX_DATA_LEN, reqp->nr_blocksz);
	}
	inlen = in->cd_length + blockctx->residlen;

	/*
	 * If the previous AES_CTR or AES_CFB multi-part operation has a
	 * residual (lastblocklen > 0), the beginning of this data can be
	 * encrypted by XORing it with the residual.
	 */
	if ((isCTR(reqp->nr_cmd) || isCFB(reqp->nr_cmd)) &&
	    (blockctx->lastblocklen > 0)) {
		char    tmpbuf[AESBLOCK];
		size_t  fraglen;
		int	i;

		fraglen = min((AESBLOCK - blockctx->lastblocklen), (int)inlen);
		rv = n2cp_gather(in, tmpbuf, fraglen);
		if (rv != CRYPTO_SUCCESS) {
			DBG1(NULL, DWARN, "block_start: n2cp_gather "
			    "failed with 0x%x", rv);
			return (rv);
		}

		/*
		 * Save the begining of the input to be used as IV for the
		 * next round of CFB decryption
		 */
		if (isCFB(reqp->nr_cmd) && (reqp->nr_cmd & N2CP_OP_DECRYPT)) {
			bcopy(tmpbuf, blockctx->nextiv + blockctx->lastblocklen,
			    fraglen);
			blockctx->nextivlen += fraglen;
		}


		for (i = 0; i < fraglen; i++) {
			blockctx->lastblock[blockctx->lastblocklen + i] ^=
			    tmpbuf[i];
		}

		/* scatter out the XORed data to the output buff */
		rv = n2cp_scatter(blockctx->lastblock + blockctx->lastblocklen,
		    reqp->nr_out, fraglen);
		if (rv != CRYPTO_SUCCESS) {
			return (rv);
		}

		/* update the context */
		inlen -= fraglen;
		blockctx->lastblocklen += fraglen;
		if (blockctx->lastblocklen >= AESBLOCK) {
			blockctx->lastblocklen = 0;
			/*
			 * blockctx->lastblock (cipher) becomes the IV for the
			 * next round of the CFB encryption
			 */
			if (isCFB(reqp->nr_cmd) &&
			    (reqp->nr_cmd & N2CP_OP_ENCRYPT)) {
				bcopy(blockctx->lastblock,
				    (char *)blockctx->nextiv, reqp->nr_blocksz);
				blockctx->nextivlen = reqp->nr_blocksz;
			}
		}

		if (inlen == 0) {
			/* no more data to process in the HW */
			return (CRYPTO_SUCCESS);
		}
	}

	if (inlen > maxinlen) {
		/*
		 * Input is larger than the max input an operation can support
		 */
		len = maxinlen;
		reqp->nr_resultlen = (int)len;
	} else if (isCTR(reqp->nr_cmd) || isCFB(reqp->nr_cmd)) {
		/*
		 * For the multi-part AES CTR/CFB, if the length of the input
		 * to be processed by HW is not multiple of AESBLOCK, the
		 * input will be padded with 0s.
		 */
		len = (inlen % AESBLOCK) ? ROUNDUP(inlen, AESBLOCK) : inlen;
		reqp->nr_resultlen = (int)inlen;
	} else if (isCBCPAD(reqp->nr_cmd) &&
	    (reqp->nr_cmd & N2CP_OP_SINGLE) &&
	    (reqp->nr_cmd & N2CP_OP_ENCRYPT)) {
		/*
		 * If it is a CBC PAD mode and a single part encryption,
		 * padding will be added and input length will be larger.
		 */
		len = ROUNDDOWN(inlen + reqp->nr_blocksz, reqp->nr_blocksz);
		if (len > maxinlen) {
			len = maxinlen;
		}
		reqp->nr_resultlen = (int)len;
	} else {
		len = ROUNDDOWN(inlen, reqp->nr_blocksz);
		if (len == 0) {
			/*
			 * No blocks being encrypted, so we just accumulate the
			 * input for the next pass and return
			 */
			n2cp_getbufbytes(in, 0, in->cd_length,
			    blockctx->resid + blockctx->residlen);
			blockctx->residlen += in->cd_length;
			reqp->nr_out->cd_length = 0;
			return (CRYPTO_SUCCESS);
		}
		reqp->nr_resultlen = (int)len;
	}

	cb = &(reqp->nr_cws[0]);
	cb->cw_op = CW_OP_ENCRYPT;
	switch (reqp->nr_cmd & N2CP_CMD_MASK) {
	case AES_CTR_MECH_INFO_TYPE:
	case AES_CCM_MECH_INFO_TYPE:
	case AES_GCM_MECH_INFO_TYPE:
	case AES_GMAC_MECH_INFO_TYPE:
		cb->cw_enc = 1;	/* always encrypt */
		break;
	default:
		cb->cw_enc = (reqp->nr_cmd & N2CP_OP_ENCRYPT) ? 1 : 0;
	}

	/*
	 * Set the cw fields assuming cw is not chained for now.
	 * If chained, some fields are overwritten.
	 */
	cb->cw_sob = 1;
	cb->cw_eob = 1;
	cb->cw_intr = 1;
	cb->cw_length = len - 1;
	reqp->nr_cwb = cb;
	reqp->nr_cwcnt = 1;

	if (isAES(reqp->nr_cmd)) {
		switch (blockctx->keylen) {
		case 16:
			aes_type = CW_ENC_ALGO_AES128;
			break;
		case 24:
			aes_type = CW_ENC_ALGO_AES192;
			break;
		case 32:
			aes_type = CW_ENC_ALGO_AES256;
			break;
		default:
			DBG1(reqp->nr_n2cp, DCHATTY, "block_start: "
			    "Invalid key size [%d]\n", blockctx->keylen);
			return (CRYPTO_KEY_SIZE_RANGE);
		}
	}

	switch (reqp->nr_cmd & N2CP_CMD_MASK) {
	case DES_CBC_MECH_INFO_TYPE:
	case DES_CBC_PAD_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(CW_ENC_ALGO_DES,
		    CW_ENC_CHAIN_CBC);
		reqp->nr_job_stat = DS_DES;
		break;
	case DES_ECB_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(CW_ENC_ALGO_DES,
		    CW_ENC_CHAIN_ECB);
		reqp->nr_job_stat = DS_DES;
		break;
	case DES_CFB_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(CW_ENC_ALGO_DES,
		    CW_ENC_CHAIN_CFB);
		reqp->nr_job_stat = DS_DES;
		break;
	case DES3_CBC_MECH_INFO_TYPE:
	case DES3_CBC_PAD_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(CW_ENC_ALGO_3DES,
		    CW_ENC_CHAIN_CBC);
		reqp->nr_job_stat = DS_DES3;
		break;
	case DES3_ECB_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(CW_ENC_ALGO_3DES,
		    CW_ENC_CHAIN_ECB);
		reqp->nr_job_stat = DS_DES3;
		break;
	case DES3_CFB_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(CW_ENC_ALGO_3DES,
		    CW_ENC_CHAIN_CFB);
		reqp->nr_job_stat = DS_DES3;
		break;
	case AES_CBC_MECH_INFO_TYPE:
	case AES_CBC_PAD_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(aes_type, CW_ENC_CHAIN_CBC);
		reqp->nr_job_stat = DS_AES;
		break;
	case AES_CFB_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(aes_type, CW_ENC_CHAIN_CFB);
		reqp->nr_job_stat = DS_AES;
		break;
	case AES_ECB_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(aes_type, CW_ENC_CHAIN_ECB);
		reqp->nr_job_stat = DS_AES;
		break;
	case AES_CTR_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(aes_type,
		    CW_ENC_CHAIN_AESCTR);
		reqp->nr_job_stat = DS_AES;
		break;
	case AES_CCM_MECH_INFO_TYPE:
	case AES_GCM_MECH_INFO_TYPE:
	case AES_GMAC_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(aes_type, CW_ENC_CHAIN_ECB);
		reqp->nr_job_stat = DS_AES;
		break;
	case RC4_WSTRM_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(CW_ENC_ALGO_RC4WSTRM, 0);
		reqp->nr_job_stat = DS_RC4;
		break;
	case RC4_WOSTRM_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(CW_ENC_ALGO_RC4WOSTRM, 0);
		reqp->nr_job_stat = DS_RC4;
		break;
	default:
		cmn_err(CE_WARN,
		    "block_start: mech(%d) unsupported", reqp->nr_cmd);
		return (CRYPTO_MECHANISM_INVALID);
	}

	/*
	 * Setup the input:
	 * If the input consists of multiple buffs or the input needs to be
	 * padded for CFB/CTR/CBC_PAD mode, the pre-allocated buffer is used
	 * for the input.
	 */
	if ((blockctx->residlen > 0) ||
	    n2cp_sgcheck(in, N2CP_SG_PCONTIG) ||
	    isCBCPAD(reqp->nr_cmd) ||
	    ((isCFB(reqp->nr_cmd) || isCTR(reqp->nr_cmd)) && (len > inlen))) {
		int	tmpinlen = (int)len;
		char	*cursor;

		/* Copy the data to input buf */
		if ((isCTR(reqp->nr_cmd) || isCFB(reqp->nr_cmd)) &&
		    (len > inlen) && (inlen % AESBLOCK)) {
			/* get a 64K contig mem from the pool */
			if ((rv = n2cp_get_inbuf(n2cp, reqp))
			    != CRYPTO_SUCCESS) {
				return (rv);
			}
			cursor = (char *)reqp->nr_in_buf;

			rv = n2cp_gather_zero_pad(reqp->nr_in, cursor, inlen,
			    reqp->nr_blocksz - (inlen % reqp->nr_blocksz));
			blockctx->lastblocklen = inlen % AESBLOCK;
			in_addr = (char *)reqp->nr_in_buf + len;
			cb->cw_src_addr = reqp->nr_in_buf_paddr;
			reqp->nr_flags |= N2CP_GATHER;
		} else if (isCBCPAD(reqp->nr_cmd) &&
		    (reqp->nr_cmd & N2CP_OP_SINGLE) &&
		    (reqp->nr_cmd & N2CP_OP_ENCRYPT) &&
		    (inlen < maxinlen)) {
			/* add the padding */
			/* get a 64K contig mem from the pool */
			if ((rv = n2cp_get_inbuf(n2cp, reqp))
			    != CRYPTO_SUCCESS) {
				return (rv);
			}
			cursor = (char *)reqp->nr_in_buf;

			rv = n2cp_gather_PKCS_pad(reqp->nr_in, cursor,
			    inlen, reqp->nr_blocksz);
			in_addr = (char *)reqp->nr_in_buf + len;
			cb->cw_src_addr = reqp->nr_in_buf_paddr;
			reqp->nr_flags |= N2CP_GATHER;
			/* padding was applied. */
			reqp->nr_cmd &= ~N2CP_OP_PROCESS_PAD;
		} else if (!(N2CP_IS_INPLACE(reqp)) ||
		    blockctx->residlen > 0) {
			/* get a 64K contig mem from the pool */
			if ((rv = n2cp_get_inbuf(n2cp, reqp))
			    != CRYPTO_SUCCESS) {
				return (rv);
			}
			cursor = (char *)reqp->nr_in_buf;

			/* Copy the resid to the input buf */
			BCOPY(blockctx->resid, cursor, blockctx->residlen);
			tmpinlen -= blockctx->residlen;
			cursor += blockctx->residlen;
			blockctx->residlen = 0;
			rv = n2cp_gather(reqp->nr_in, cursor, tmpinlen);
			in_addr = (char *)reqp->nr_in_buf + len;
			cb->cw_src_addr = reqp->nr_in_buf_paddr;
			reqp->nr_flags |= N2CP_GATHER;
		} else if ((!n2cp_use_ulcwq &&
		    !CRYPTO_DATA_IS_USERSPACE(reqp->nr_in)) &&
		    ((rv = n2cp_construct_chain(&(reqp->nr_cws[0]),
		    reqp->nr_in, len, &reqp->nr_cwcnt)) == CRYPTO_SUCCESS)) {
			/*
			 * Chain the input: eob and reqp->nr_cwcnt is
			 * changed by n2cp_construct_chain
			 */
			in_addr = n2cp_get_dataaddr(reqp->nr_in, 0);
		} else {
			/* get a 64K contig mem from the pool */
			if ((rv = n2cp_get_inbuf(n2cp, reqp))
			    != CRYPTO_SUCCESS) {
				return (rv);
			}
			cursor = (char *)reqp->nr_in_buf;

			rv = n2cp_gather(reqp->nr_in, cursor, tmpinlen);
			in_addr = (char *)reqp->nr_in_buf + len;
			cb->cw_src_addr = reqp->nr_in_buf_paddr;
			reqp->nr_flags |= N2CP_GATHER;
			reqp->nr_flags &= ~N2CP_INPLACE;
		}
		if (rv != CRYPTO_SUCCESS) {
			return (rv);
		}
	} else {
		in_addr = n2cp_get_dataaddr(in, 0);

		cb->cw_src_addr = va_to_pa(in_addr);
		in->cd_length -= len;
		in->cd_offset += len;
		in_addr += len;
	}

	/*
	 * Setup the output:
	 * If output consists of multiple buffs, the input data was
	 * padded for CFB/CTR mode, or the output buffer may contain the padding
	 * for the CBC PAD mode(in which case, the recovered data may be larger
	 * than the buffer given by the framework), then use the pre-allocated
	 * buffer for the output.
	 */
	if (n2cp_use_ulcwq || (!(N2CP_IS_INPLACE(reqp)) &&
	    n2cp_sgcheck(reqp->nr_out, N2CP_SG_PCONTIG)) ||
	    (isCBCPAD(reqp->nr_cmd) && (reqp->nr_cmd & N2CP_OP_DECRYPT)) ||
	    ((isCTR(reqp->nr_cmd) || isCFB(reqp->nr_cmd)) && (len > inlen))) {
		/* get a 64K contig mem from the pool */
		if ((rv = n2cp_get_outbuf(n2cp, reqp)) != CRYPTO_SUCCESS) {
			return (rv);
		}

		reqp->nr_flags |= N2CP_SCATTER;
		cb->cw_dst_addr = reqp->nr_out_buf_paddr;
	} else {
		if (!(N2CP_IS_INPLACE(reqp))) {
			cb->cw_dst_addr =
			    va_to_pa(n2cp_get_dataaddr(reqp->nr_out,
			    reqp->nr_out->cd_length));
		} else {
			cb->cw_dst_addr = 0;
			cb->cw_op |= CW_OP_INLINE_BIT;
		}
	}

	/* set Key value */
	cb->cw_enc_key_addr = reqp->nr_context_paddr + BLOCK_KEY_OFFSET;
	cb->cw_hmac_keylen = blockctx->keylen - 1;

	/* Set the IV */
	if (blockctx->nextivlen > 0) {
		BCOPY(blockctx->nextiv, blockctx->iv, blockctx->nextivlen);
		blockctx->ivlen = blockctx->nextivlen;
		blockctx->nextivlen = 0;
		cb->cw_enc_iv_addr =
		    reqp->nr_context_paddr + BLOCK_IV_OFFSET;
	}

	/* Set the ctx for the next round */
	if (isCTR(reqp->nr_cmd)) {
		/* increment the CTR part of the counter block */
		n2cp_add_ctr_bits(blockctx->nextiv, len / AESBLOCK,
		    blockctx->ctrbits);
		blockctx->nextivlen = reqp->nr_blocksz;
	} else if ((isCBC(reqp->nr_cmd) || isCFB(reqp->nr_cmd)) &&
	    (reqp->nr_cmd & N2CP_OP_DECRYPT)) {
		char *nextiv;

		/* get blocksz bytes from the end of the data buf */
		nextiv = in_addr - reqp->nr_blocksz;
		BCOPY(nextiv, blockctx->nextiv, reqp->nr_blocksz);
		blockctx->nextivlen = reqp->nr_blocksz;
	}

	rv = n2cp_start(n2cp, reqp);
	reqp->nr_errno = rv;

#ifdef	DEBUG
	if (n2cp_hist) {
		end = gethrtime();
		nsec = end - start;

		n2cp_histogram(reqp, nsec/1000000);

		if (nsec > n2cp_max_job_nsec)
			n2cp_max_job_nsec = nsec;

		if (n2cp_min_job_nsec == 0 || nsec < n2cp_min_job_nsec) {
			n2cp_min_job_nsec = nsec;
		}

	}
#endif /* DEBUG */


	reqp->nr_callback(reqp);

/* EXPORT DELETE END */

	return (reqp->nr_errno);
}

static int
block_final_start(n2cp_request_t *reqp)
{
	n2cp_block_ctx_t	*blockctx = &(reqp->nr_context->blockctx);
	n2cp_t		*n2cp = (n2cp_t *)reqp->nr_n2cp;
	cwq_cw_t	*cb;
	uint64_t	len = reqp->nr_blocksz;
	int		rv;
	int		aes_type;
	char		*cursor;

/* EXPORT DELETE START */
#ifdef	DEBUG
	hrtime_t	start, end, nsec;
	if (n2cp_hist) {
		start = gethrtime();
	}
#endif

	reqp->nr_resultlen = reqp->nr_blocksz;

	cb = &(reqp->nr_cws[0]);
	cb->cw_op = CW_OP_ENCRYPT;
	cb->cw_enc = (reqp->nr_cmd & N2CP_OP_ENCRYPT) ? 1 : 0;

	/*
	 * Set the cw fields
	 */
	cb->cw_sob = 1;
	cb->cw_eob = 1;
	cb->cw_intr = 1;
	cb->cw_length = len - 1;
	reqp->nr_cwb = cb;
	reqp->nr_cwcnt = 1;

	switch (reqp->nr_cmd & N2CP_CMD_MASK) {
	case DES_CBC_PAD_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(CW_ENC_ALGO_DES,
		    CW_ENC_CHAIN_CBC);
		reqp->nr_job_stat = DS_DES;
		break;
	case DES3_CBC_PAD_MECH_INFO_TYPE:
		cb->cw_enc_type = CW_ENC_TYPE(CW_ENC_ALGO_3DES,
		    CW_ENC_CHAIN_CBC);
		reqp->nr_job_stat = DS_DES3;
		break;
	case AES_CBC_PAD_MECH_INFO_TYPE:
		switch (blockctx->keylen) {
		case 16:
			aes_type = CW_ENC_ALGO_AES128;
			break;
		case 24:
			aes_type = CW_ENC_ALGO_AES192;
			break;
		case 32:
			aes_type = CW_ENC_ALGO_AES256;
			break;
		default:
			DBG1(n2cp, DCHATTY, "block_final_start: "
			    "Invalid key size [%d]\n", blockctx->keylen);
			return (CRYPTO_KEY_SIZE_RANGE);
		}
		cb->cw_enc_type = CW_ENC_TYPE(aes_type, CW_ENC_CHAIN_CBC);
		reqp->nr_job_stat = DS_AES;
		break;
	default:
		cmn_err(CE_WARN,
		    "block_final_start: mech(%d) unsupported", reqp->nr_cmd);
		return (CRYPTO_MECHANISM_INVALID);
	}

	/*
	 * Setup the input:
	 * Always use pre-allocated buffer since the input is always
	 * small (BLOCKSZ) and padding may need to be applied
	 */
	/* get a 64K contig mem from the pool */
	if ((rv = n2cp_get_inbuf(n2cp, reqp)) != CRYPTO_SUCCESS) {
		return (rv);
	}
	cb->cw_src_addr = reqp->nr_in_buf_paddr;
	cursor = (char *)reqp->nr_in_buf;
	BCOPY(blockctx->resid, cursor, blockctx->residlen);
	cursor += blockctx->residlen;

	if (reqp->nr_cmd & N2CP_OP_ENCRYPT) {
		int	padlen;
		padlen =
		    reqp->nr_blocksz - (blockctx->residlen % reqp->nr_blocksz);
		(void) memset(cursor, (char)padlen, padlen);
		/* padding was applied. */
		reqp->nr_cmd &= ~N2CP_OP_PROCESS_PAD;
	}


	/*
	 * Setup the output:
	 */
	if (n2cp_sgcheck(reqp->nr_out, N2CP_SG_PCONTIG) ||
	    (n2cp_get_dataaddr(reqp->nr_out, reqp->nr_out->cd_length) == 0)) {
		/* get a 64K contig mem from the pool */
		if ((rv = n2cp_get_outbuf(n2cp, reqp)) != CRYPTO_SUCCESS) {
			return (rv);
		}

		reqp->nr_flags |= N2CP_SCATTER;
		cb->cw_dst_addr = reqp->nr_out_buf_paddr;
	} else {
		cb->cw_dst_addr = va_to_pa(n2cp_get_dataaddr(reqp->nr_out,
		    reqp->nr_out->cd_length));
	}

	/* set Key value */
	cb->cw_enc_key_addr = reqp->nr_context_paddr + BLOCK_KEY_OFFSET;
	cb->cw_hmac_keylen = blockctx->keylen - 1;

	/* Set the IV */
	if (blockctx->nextivlen > 0) {
		BCOPY(blockctx->nextiv, blockctx->iv, blockctx->nextivlen);
		blockctx->ivlen = blockctx->nextivlen;
		blockctx->nextivlen = 0;
		cb->cw_enc_iv_addr =
		    reqp->nr_context_paddr + BLOCK_IV_OFFSET;
	}

	rv = n2cp_start(n2cp, reqp);
	reqp->nr_errno = rv;

#ifdef	DEBUG
	if (n2cp_hist) {
		end = gethrtime();
		nsec = end - start;

		n2cp_histogram(reqp, nsec/1000000);

		if (nsec > n2cp_max_job_nsec)
			n2cp_max_job_nsec = nsec;

		if (n2cp_min_job_nsec == 0 || nsec < n2cp_min_job_nsec) {
			n2cp_min_job_nsec = nsec;
		}

	}
#endif /* DEBUG */


	reqp->nr_callback(reqp);

/* EXPORT DELETE END */

	return (reqp->nr_errno);
}


static void
block_done(n2cp_request_t *reqp)
{
	int		rv = CRYPTO_SUCCESS;
	crypto_data_t	*out = reqp->nr_out;
	n2cp_block_ctx_t	*blockctx = &(reqp->nr_context->blockctx);
	int		residlen = 0;

/* EXPORT DELETE START */

	if (reqp->nr_errno != CRYPTO_SUCCESS) {
		rv = reqp->nr_errno;
		goto done;
	}

	/*
	 * If the output buffer size is 0 (that is when the output buffer
	 * is NULL), return the expected output buffer length.
	 */
	if (n2cp_get_bufsz(out) == 0) {
		rv = CRYPTO_BUFFER_TOO_SMALL;
		out->cd_length = reqp->nr_resultlen;
		goto done;
	}

	/*
	 * if output buffer consists of multiple buffers,
	 * scatter out the result.
	 */
	if ((isCTR(reqp->nr_cmd) || isCFB(reqp->nr_cmd)) &&
	    (blockctx->lastblocklen > 0)) {
		/*
		 * For CTR/CFB mode, if 'lastblocklen' is greater than 0,
		 * it means that the input was padded with 0s for the next round
		 * of operation.
		 */
		rv = n2cp_scatter((char *)reqp->nr_out_buf, reqp->nr_out,
		    reqp->nr_resultlen);
		if (rv != CRYPTO_SUCCESS) {
			goto done;
		}

		/*
		 * Save the last block to the ctx so that the next round
		 * of operation can use it to generate output.
		 */
		BCOPY(reqp->nr_out_buf +
		    ROUNDDOWN(reqp->nr_resultlen, AESBLOCK),
		    blockctx->lastblock, AESBLOCK);
	} else if (isCBCPAD(reqp->nr_cmd) && (reqp->nr_cmd & N2CP_OP_DECRYPT)) {
		/*
		 * copy the last block from the previous update to the output
		 * buffer if there is any
		 */
		rv = n2cp_scatter(blockctx->lastblock, reqp->nr_out,
		    blockctx->lastblocklen);
		if (rv != CRYPTO_SUCCESS) {
			goto done;
		}
		blockctx->lastblocklen = 0;

		/* copy the new output */
		if (reqp->nr_in && (reqp->nr_in->cd_length > 0)) {
			/*
			 * If there is a residual, we know that this
			 * buf does not contain the padding. Return
			 * everything to the caller.
			 */
			rv = n2cp_scatter((char *)reqp->nr_out_buf,
			    reqp->nr_out, reqp->nr_resultlen);
			if (rv != CRYPTO_SUCCESS) {
				goto done;
			}
		} else if (reqp->nr_cmd & N2CP_OP_SINGLE) {
			/*
			 * if there is no residual and single-part, remove
			 * the padding from the output
			 */
			rv = n2cp_scatter_PKCS_unpad((char *)reqp->nr_out_buf,
			    reqp->nr_resultlen, reqp->nr_out, reqp->nr_blocksz);
			if (rv != CRYPTO_SUCCESS) {
				goto done;
			}
			/* padding was removed */
			reqp->nr_cmd &= ~N2CP_OP_PROCESS_PAD;
		} else {
			/*
			 * If there is no residual and multi-part, this buf
			 * may contain the padding. Save the last block_size
			 * bytes of the output
			 */
			rv = n2cp_scatter((char *)reqp->nr_out_buf,
			    reqp->nr_out,
			    reqp->nr_resultlen - reqp->nr_blocksz);
			if (rv != CRYPTO_SUCCESS) {
				goto done;
			}

			/* save last block to the ctx */
			BCOPY(reqp->nr_out_buf + reqp->nr_resultlen -
			    reqp->nr_blocksz, blockctx->lastblock,
			    reqp->nr_blocksz);
			blockctx->lastblocklen = reqp->nr_blocksz;
		}
	} else if (reqp->nr_flags & N2CP_SCATTER) {
		rv = n2cp_scatter((char *)reqp->nr_out_buf, reqp->nr_out,
		    reqp->nr_resultlen);
		if (rv != CRYPTO_SUCCESS) {
			goto done;
		}
	} else {
		out->cd_length += reqp->nr_resultlen;
	}

	/*
	 * For CBC and CFB encryption, we have to grab the IV for the
	 * next pass AFTER encryption.
	 */
	if ((isCBC(reqp->nr_cmd) ||
	    (isCFB(reqp->nr_cmd) && (blockctx->lastblocklen == 0))) &&
	    (reqp->nr_cmd & N2CP_OP_ENCRYPT)) {
		/* get last blocksz bytes for IV of next op */
		n2cp_getbufbytes(out, out->cd_length - reqp->nr_blocksz,
		    reqp->nr_blocksz, (char *)blockctx->nextiv);
		blockctx->nextivlen = reqp->nr_blocksz;
	}

	/* findout the residual data len */
	if (reqp->nr_in) {
		residlen = reqp->nr_in->cd_length;
	}

	if ((residlen == 0) &&
	    ((reqp->nr_cmd & N2CP_OP_PROCESS_PAD) == 0)) {
		/* nothing to process: go to done */
		goto done;
	}

	/*
	 * If there is more to do, then reschedule another
	 * pass. Otherwise, save the residual in the ctx and exit.
	 */
	if (STREAM_CIPHER(reqp->nr_cmd) ||
	    residlen >= reqp->nr_blocksz ||
	    ((reqp->nr_cmd & N2CP_OP_PROCESS_PAD) &&
	    (reqp->nr_cmd & N2CP_OP_SINGLE))) {
		/* more work to do, schedule another pass */
		reqp->nr_flags &= ~(N2CP_SCATTER | N2CP_GATHER);
		bzero(reqp->nr_cws, reqp->nr_cwcnt * sizeof (cwq_cw_t));
		if (n2cp_use_ulcwq &&
		    ((rv = n2cp_set_affinity_for_req(reqp)) !=
		    CRYPTO_SUCCESS)) {
			goto done;
		}
		rv = block_start(reqp);
	} else {
		/* copyin the residual to the context */
		n2cp_getbufbytes(reqp->nr_in, 0, residlen,
		    blockctx->resid + blockctx->residlen);
		blockctx->residlen += residlen;

		/* residual was stashed in the context */
		n2cp_setresid(reqp->nr_in, 0);
	}

done:

	reqp->nr_errno = rv;

/* EXPORT DELETE END */

}
