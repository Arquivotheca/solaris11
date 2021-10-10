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
#include <sys/byteorder.h>

static void hash_done(n2cp_request_t *);
static void hash_final_done(n2cp_request_t *);
static int hash_start(n2cp_request_t *);
static int hash_final_start(n2cp_request_t *);

#define	MAX_DATA_BITS	(CW_MAX_DATA_LEN * 8)

/*
 * The following are the fixed IVs that hardware
 * is expecting for the respective hash algorithms.
 */
extern fixed_iv_t	iv_md5, iv_sha1, iv_sha256, iv_sha384, iv_sha512;

static const uchar_t	md5_nulldigest[] = {
	0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
	0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e
};
static const uchar_t	sha1_nulldigest[] = {
	0xDA, 0x39, 0xA3, 0xEE, 0x5E, 0x6B, 0x4B, 0x0D,
	0x32, 0x55, 0xBF, 0xEF, 0x95, 0x60, 0x18, 0x90,
	0xAF, 0xD8, 0x07, 0x09
};
static const uchar_t	sha256_nulldigest[] = {
	0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
	0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
	0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
	0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
};
static const uchar_t	sha384_nulldigest[] = {
	0x38, 0xb0, 0x60, 0xa7, 0x51, 0xac, 0x96, 0x38,
	0x4c, 0xd9, 0x32, 0x7e, 0xb1, 0xb1, 0xe3, 0x6a,
	0x21, 0xfd, 0xb7, 0x11, 0x14, 0xbe, 0x07, 0x43,
	0x4c, 0x0c, 0xc7, 0xbf, 0x63, 0xf6, 0xe1, 0xda,
	0x27, 0x4e, 0xde, 0xbf, 0xe7, 0x6f, 0x65, 0xfb,
	0xd5, 0x1a, 0xd2, 0xf1, 0x48, 0x98, 0xb9, 0x5b
};
static const uchar_t	sha512_nulldigest[] = {
	0xcf, 0x83, 0xe1, 0x35, 0x7e, 0xef, 0xb8, 0xbd,
	0xf1, 0x54, 0x28, 0x50, 0xd6, 0x6d, 0x80, 0x07,
	0xd6, 0x20, 0xe4, 0x05, 0x0b, 0x57, 0x15, 0xdc,
	0x83, 0xf4, 0xa9, 0x21, 0xd3, 0x6c, 0xe9, 0xce,
	0x47, 0xd0, 0xd1, 0x3c, 0x5d, 0x85, 0xf2, 0xb0,
	0xff, 0x83, 0x18, 0xd2, 0x87, 0x7e, 0xec, 0x2f,
	0x63, 0xb9, 0x31, 0xbd, 0x47, 0x41, 0x7a, 0x81,
	0xa5, 0x38, 0x32, 0x7a, 0xf9, 0x27, 0xda, 0x3e
};

static int
alloc_hash_ctx(n2cp_t *n2cp, crypto_mechanism_t *mechanism,
    n2cp_request_t **argreq)
{
	n2cp_request_t	*reqp;
	n2cp_hash_ctx_t	*hashctx;
	cwq_cw_t	*cb;
	uint32_t	*hashiv;
	uint32_t	hashivsz;

/* EXPORT DELETE START */

	*argreq = NULL;

	if ((reqp = n2cp_getreq(n2cp)) == NULL) {
		DBG0(NULL, DWARN,
		    "alloc_des_request: unable to allocate request for hash");
		return (CRYPTO_HOST_MEMORY);
	}
	cb = &(reqp->nr_cws[0]);
	hashctx = &(reqp->nr_context->hashctx);
	hashctx->residlen = 0;
	hashctx->total_bits[0] = 0;
	hashctx->total_bits[1] = 0;

	switch (mechanism->cm_type) {
	case MD5_MECH_INFO_TYPE:
		hashctx->hashsz = MD5_DIGESTSZ;
		hashiv = iv_md5.iv;
		hashivsz = iv_md5.ivsize;
		reqp->nr_blocksz = 64;
		reqp->nr_job_stat = DS_MD5;
		cb->cw_hlen = MD5_DIGESTSZ - 1;
		break;
	case SHA1_MECH_INFO_TYPE:
		hashctx->hashsz = SHA1_DIGESTSZ;
		hashiv = iv_sha1.iv;
		hashivsz = iv_sha1.ivsize;
		reqp->nr_blocksz = 64;
		reqp->nr_job_stat = DS_SHA1;
		cb->cw_hlen = SHA1_DIGESTSZ - 1;
		break;
	case SHA256_MECH_INFO_TYPE:
		hashctx->hashsz = SHA256_DIGESTSZ;
		hashiv = iv_sha256.iv;
		hashivsz = iv_sha256.ivsize;
		reqp->nr_blocksz = 64;
		reqp->nr_job_stat = DS_SHA256;
		cb->cw_hlen = SHA256_DIGESTSZ - 1;
		break;
	case SHA384_MECH_INFO_TYPE:
		hashctx->hashsz = SHA384_DIGESTSZ;
		hashiv = iv_sha384.iv;
		hashivsz = iv_sha384.ivsize;
		reqp->nr_blocksz = 128;
		reqp->nr_job_stat = DS_SHA384;
		cb->cw_hlen = SHA512_DIGESTSZ - 1;
		break;
	case SHA512_MECH_INFO_TYPE:
		hashctx->hashsz = SHA512_DIGESTSZ;
		hashiv = iv_sha512.iv;
		hashivsz = iv_sha512.ivsize;
		reqp->nr_blocksz = 128;
		reqp->nr_job_stat = DS_SHA512;
		cb->cw_hlen = SHA512_DIGESTSZ - 1;
		break;
	default:
		cmn_err(CE_WARN, "alloc_hash_ctx: mech(%ld) unsupported",
		    mechanism->cm_type);
		n2cp_freereq(reqp);
		return (CRYPTO_MECHANISM_INVALID);
	}

	BCOPY(hashiv, hashctx->iv, hashivsz);

	reqp->nr_cmd = mechanism->cm_type;
	reqp->nr_cmd |= N2CP_OP_DIGEST;
	reqp->nr_n2cp = n2cp;
	*argreq = reqp;

/* EXPORT DELETE END */

	return (CRYPTO_SUCCESS);
}

int
n2cp_hashinit(crypto_ctx_t *ctx, crypto_mechanism_t *mechanism)
{
	int		rv;
	n2cp_t		*n2cp = (n2cp_t *)ctx->cc_provider;
	n2cp_request_t	*reqp;

	rv = alloc_hash_ctx(n2cp, mechanism, &reqp);
	if (rv != CRYPTO_SUCCESS) {
		return (rv);
	}

	ctx->cc_provider_private = reqp;
	/* release ulcwq buffer */
	check_draining(reqp);

	return (CRYPTO_SUCCESS);
}

int
n2cp_hash(crypto_ctx_t *ctx, crypto_data_t *in, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	n2cp_request_t	*reqp = (n2cp_request_t *)(ctx->cc_provider_private);
	n2cp_hash_ctx_t	*hashctx = &(reqp->nr_context->hashctx);

/* EXPORT DELETE START */


	if (!is_KT(reqp->nr_n2cp)) {
		/*
		 * the chip does not support multi-part, and therefore, we can
		 * support up to MAX_DATA_LEN input
		 */
		if (in->cd_length > MAX_DATA_LEN) {
			return (CRYPTO_DATA_LEN_RANGE);
		}
	}

	if (out->cd_length < hashctx->hashsz) {
		out->cd_length = hashctx->hashsz;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	N2CP_REQ_SETUP(reqp, in, out);

	reqp->nr_callback = hash_done;
	reqp->nr_kcfreq = kcfreq;
	reqp->nr_cmd |= N2CP_OP_SINGLE;

/* EXPORT DELETE END */

	return (hash_start(reqp));
}

int
n2cp_hash_update(crypto_ctx_t *ctx, crypto_data_t *in,
    crypto_req_handle_t kcfreq)
{
	n2cp_request_t	*reqp = (n2cp_request_t *)(ctx->cc_provider_private);
	n2cp_hash_ctx_t *hashctx = &(reqp->nr_context->hashctx);
	uint64_t	maxinlen, inlen, len;

/* EXPORT DELETE START */

	maxinlen = ROUNDDOWN(MAX_DATA_LEN, reqp->nr_blocksz);
	len = inlen = in->cd_length + hashctx->residlen;
	if (inlen > maxinlen) {
		len = maxinlen;
	}
	len = ROUNDDOWN(inlen, reqp->nr_blocksz);

	/*
	 * Accumulate at least a block of data. Updates, except for
	 * the last update, must be multiple of the block size.
	 */
	if (len == 0) {
		n2cp_getbufbytes(in, 0, in->cd_length,
		    hashctx->resid + hashctx->residlen);
		hashctx->residlen += in->cd_length;
		return (CRYPTO_SUCCESS);
	}

	N2CP_REQ_SETUP_HASH_UPDATE(reqp, in);

	reqp->nr_callback = hash_done;
	reqp->nr_kcfreq = kcfreq;
	reqp->nr_cmd |= N2CP_OP_MULTI;

/* EXPORT DELETE END */

	return (hash_start(reqp));
}

int
n2cp_hash_final(crypto_ctx_t *ctx, crypto_data_t *out,
    crypto_req_handle_t kcfreq)
{
	n2cp_request_t	*reqp = (n2cp_request_t *)(ctx->cc_provider_private);
	n2cp_hash_ctx_t	*hashctx = &(reqp->nr_context->hashctx);

/* EXPORT DELETE START */

	if (out->cd_length < hashctx->hashsz) {
		out->cd_length = hashctx->hashsz;
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	N2CP_REQ_SETUP_FINAL(reqp, out);

	reqp->nr_callback = hash_final_done;
	reqp->nr_kcfreq = kcfreq;
	reqp->nr_cmd |= N2CP_OP_SINGLE;

/* EXPORT DELETE END */

	return (hash_final_start(reqp));
}

int
n2cp_hashatomic(n2cp_t *n2cp, crypto_mechanism_t *mechanism,
    crypto_data_t *in, crypto_data_t *out, crypto_req_handle_t kcfreq)
{
	int			rv;
	n2cp_request_t		*reqp;
	n2cp_hash_ctx_t		*hashctx;

/* EXPORT DELETE START */

	if (!is_KT(n2cp)) {
		/*
		 * the chip does not support multi-part, and therefore, we can
		 * support upto MAX_DATA_LEN input
		 */
		if (in->cd_length > MAX_DATA_LEN) {
			return (CRYPTO_DATA_LEN_RANGE);
		}
	}

	rv = alloc_hash_ctx(n2cp, mechanism, &reqp);
	if (rv != CRYPTO_SUCCESS) {
		return (rv);
	}
	hashctx = &(reqp->nr_context->hashctx);

	if (out->cd_length < hashctx->hashsz) {
		out->cd_length = hashctx->hashsz;
		n2cp_freereq(reqp);
		return (CRYPTO_BUFFER_TOO_SMALL);
	}

	/* bind to cep */
	if (n2cp_use_ulcwq &&
	    (rv = n2cp_find_cep_for_req(reqp) != CRYPTO_SUCCESS)) {
		DBG1(NULL, DWARN, "n2cp_hashatomic: n2cp_find_cep_for_req"
		    "failed with 0x%x", rv);
		n2cp_freereq(reqp);
		return (rv);
	}

	N2CP_REQ_SETUP(reqp, in, out);

	reqp->nr_callback = hash_done;
	reqp->nr_kcfreq = kcfreq;
	reqp->nr_cmd |= N2CP_OP_SINGLE;

	rv = hash_start(reqp);

	n2cp_freereq(reqp);

/* EXPORT DELETE END */

	return (rv);
}

static void
set_auth_type_update(n2cp_request_t *reqp)
{
	cwq_cw_t	*cb = &(reqp->nr_cws[0]);

	switch (reqp->nr_cmd & N2CP_CMD_MASK) {
	case MD5_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_MD5_UPDATE;
		break;
	case SHA1_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_SHA1_UPDATE;
		break;
	case SHA256_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_SHA256_UPDATE;
		break;
	case SHA384_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_SHA512_UPDATE;
		break;
	case SHA512_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_SHA512_UPDATE;
	}
}

static void
set_auth_type_final(n2cp_request_t *reqp)
{
	cwq_cw_t	*cb = &(reqp->nr_cws[0]);

	switch (reqp->nr_cmd & N2CP_CMD_MASK) {
	case MD5_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_MD5;
		break;
	case SHA1_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_SHA1;
		break;
	case SHA256_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_SHA256;
		break;
	case SHA384_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_SHA512;
		break;
	case SHA512_MECH_INFO_TYPE:
		cb->cw_auth_type = CW_AUTH_SHA512;
	}
}

static int
hash_start(n2cp_request_t *reqp)
{
	n2cp_t		*n2cp = (n2cp_t *)reqp->nr_n2cp;
	cwq_cw_t	*cb;
	crypto_data_t	*in = reqp->nr_in;
	n2cp_hash_ctx_t *hashctx;
	uint64_t	maxinlen, len, block_multiple_len, n;
	int		rv;

/* EXPORT DELETE START */

	/*
	 * If it is a single part operation, and the input length is 0,
	 * return the hardcoded digest
	 */
	if ((in->cd_length == 0) && (reqp->nr_cmd & N2CP_OP_SINGLE)) {
		switch (reqp->nr_cmd & N2CP_CMD_MASK) {
		case MD5_MECH_INFO_TYPE:
			rv = n2cp_scatter((char *)md5_nulldigest, reqp->nr_out,
			    MD5_DIGESTSZ);
			break;
		case SHA1_MECH_INFO_TYPE:
			rv = n2cp_scatter((char *)sha1_nulldigest,
			    reqp->nr_out, SHA1_DIGESTSZ);
			break;
		case SHA256_MECH_INFO_TYPE:
			rv = n2cp_scatter((char *)sha256_nulldigest,
			    reqp->nr_out, SHA256_DIGESTSZ);
			break;
		case SHA384_MECH_INFO_TYPE:
			rv = n2cp_scatter((char *)sha384_nulldigest,
			    reqp->nr_out, SHA384_DIGESTSZ);
			break;
		case SHA512_MECH_INFO_TYPE:
			rv = n2cp_scatter((char *)sha512_nulldigest,
			    reqp->nr_out, SHA512_DIGESTSZ);
			break;
		default:
			rv = CRYPTO_MECHANISM_INVALID;
		}
		return (rv);
	}

	hashctx = &(reqp->nr_context->hashctx);

	/* assume update until proven otherwise */
	set_auth_type_update(reqp);

	maxinlen = ROUNDDOWN(MAX_DATA_LEN, reqp->nr_blocksz);
	len = in->cd_length + hashctx->residlen;
	if (len > maxinlen) {
		/* break input into smaller chunks */
		len = maxinlen;
	}
	block_multiple_len = ROUNDDOWN(len, reqp->nr_blocksz);
	n = (reqp->nr_cmd & N2CP_OP_SINGLE) ? len : block_multiple_len;

	/*
	 * Set the cw fields assuming cw is not chained for now.
	 * If chained, some fields are overwritten.
	 */
	cb = &(reqp->nr_cws[0]);
	cb->cw_op = CW_OP_MAC_AUTH | CW_OP_INLINE_BIT;
	cb->cw_enc = 0;
	cb->cw_sob = 1;
	cb->cw_eob = 1;
	cb->cw_intr = 1;

	reqp->nr_cwb = cb;
	reqp->nr_cwcnt = 1;

	/* Setup the input */
	cb->cw_length = n - 1;

	if (hashctx->residlen > 0) {
		size_t tmplen = n;
		char *cursor;

		/* get a 64K contig mem from the pool */
		if ((rv = n2cp_get_inbuf(n2cp, reqp)) != CRYPTO_SUCCESS) {
			return (rv);
		}
		cursor = (char *)reqp->nr_in_buf;

		/* Copy the resid to the input buf */
		BCOPY(hashctx->resid, cursor, hashctx->residlen);
		tmplen -= hashctx->residlen;
		cursor += hashctx->residlen;
		hashctx->residlen = 0;

		/*
		 * Copy the data to input buf: n2cp_gather updates
		 * in->cd_length
		 */
		rv = n2cp_gather(in, cursor, tmplen);
		if (rv != CRYPTO_SUCCESS) {
			n2cp_free_buf(n2cp, reqp);
			return (rv);
		}
		cb->cw_src_addr = reqp->nr_in_buf_paddr;
		reqp->nr_flags |= N2CP_GATHER;
	} else if (!n2cp_use_ulcwq && n2cp_sgcheck(in, N2CP_SG_PCONTIG)) {
		if (CRYPTO_DATA_IS_USERSPACE(in) ||
		    ((rv = n2cp_construct_chain(&(reqp->nr_cws[0]),
		    in, n, &reqp->nr_cwcnt)) != CRYPTO_SUCCESS)) {
			/* get a 64K contig mem from the pool */
			if ((rv = n2cp_get_inbuf(n2cp, reqp))
			    != CRYPTO_SUCCESS) {
				return (rv);
			}

			/*
			 * Copy the data to input buf: n2cp_gather updates
			 * in->cd_length
			 */
			rv = n2cp_gather(in, (char *)reqp->nr_in_buf, n);
			if (rv != CRYPTO_SUCCESS) {
				n2cp_free_buf(n2cp, reqp);
				return (rv);
			}
			cb->cw_src_addr = reqp->nr_in_buf_paddr;
			reqp->nr_flags |= N2CP_GATHER;
		}
	} else if (n2cp_use_ulcwq) {
			/*
			 * Copy the data to input buf: n2cp_gather updates
			 * in->cd_length
			 */
			rv = n2cp_gather(in, (char *)reqp->nr_in_buf, n);
			if (rv != CRYPTO_SUCCESS) {
				n2cp_free_buf(n2cp, reqp);
				return (rv);
			}
			cb->cw_src_addr = reqp->nr_in_buf_paddr;
			reqp->nr_flags |= N2CP_GATHER;
	} else {
		cb->cw_src_addr = va_to_pa(n2cp_get_dataaddr(in, 0));
		cb->cw_length = n - 1;
		in->cd_length -= n;
		in->cd_offset += n;
	}

	/* SHA384 and SHA512 use 128 bit counter */
	if ((reqp->nr_cmd & N2CP_CMD_MASK) == SHA384_MECH_INFO_TYPE ||
	    (reqp->nr_cmd & N2CP_CMD_MASK) == SHA512_MECH_INFO_TYPE) {
		n2cp_add_ctr_bits((uchar_t *)hashctx->total_bits, n * 8, 128);
	} else {
		hashctx->total_bits[1] += n * 8;
	}

	/* Setup the output */
	if (reqp->nr_out != NULL) {
		if (n2cp_use_ulcwq ||
		    n2cp_sgcheck(reqp->nr_out, N2CP_SG_PCONTIG)) {
			/* get a 64K contig mem from the pool */
			if ((rv = n2cp_get_outbuf(n2cp, reqp))
			    != CRYPTO_SUCCESS) {
				n2cp_free_buf(n2cp, reqp);
				return (rv);
			}
			cb->cw_dst_addr = reqp->nr_out_buf_paddr;
			reqp->nr_flags |= N2CP_SCATTER;
		} else {
			cb->cw_dst_addr =
			    va_to_pa(n2cp_get_dataaddr(reqp->nr_out, 0));
		}
	}

	if ((reqp->nr_cmd & N2CP_OP_SINGLE) && (in->cd_length == 0) &&
	    (hashctx->total_bits[1] <= MAX_DATA_BITS)) {
		set_auth_type_final(reqp);
	} else {
		cb->cw_dst_addr = va_to_pa(hashctx->iv);
	}

	cb->cw_auth_iv_addr = reqp->nr_context_paddr + HASH_IV_OFFSET;

	/* setup request for post-processing */
	if ((reqp->nr_cmd & N2CP_CMD_MASK) == SHA384_MECH_INFO_TYPE) {
		reqp->nr_resultlen = SHA384_DIGESTSZ;
	} else {
		reqp->nr_resultlen = cb->cw_hlen + 1;
	}

	rv = n2cp_start(n2cp, reqp);
	reqp->nr_errno = rv;

	reqp->nr_callback(reqp);
	n2cp_free_buf(n2cp, reqp);

/* EXPORT DELETE END */

	return (reqp->nr_errno);
}

/*
 * Construct padded block for final digest operation.
 * Any unprocessed (residual) bytes are processed now.
 */
static int
hash_final_start(n2cp_request_t *reqp)
{
	n2cp_t		*n2cp = (n2cp_t *)reqp->nr_n2cp;
	n2cp_hash_ctx_t *hashctx;
	cwq_cw_t	*cb;
	int		rv;

/* EXPORT DELETE START */

	hashctx = &(reqp->nr_context->hashctx);

	cb = &(reqp->nr_cws[0]);
	cb->cw_op = CW_OP_MAC_AUTH | CW_OP_INLINE_BIT;
	cb->cw_enc = 0;
	cb->cw_sob = 1;
	cb->cw_eob = 1;
	cb->cw_intr = 1;
	reqp->nr_cwb = cb;
	reqp->nr_cwcnt = 1;
	int pad_bytes, limit, length_index, length_length = 8;

	/*
	 * Total amount of data (in bits) stored in pad as 64 or 128 bit int.
	 * MD5: little-endian
	 * SHA: big-endian
	 */
	if ((reqp->nr_cmd & N2CP_CMD_MASK) == SHA384_MECH_INFO_TYPE ||
	    (reqp->nr_cmd & N2CP_CMD_MASK) == SHA512_MECH_INFO_TYPE) {
		length_length = 16;
	}

	/* get buffer for final block */
	if ((rv = n2cp_get_inbuf(n2cp, reqp)) != CRYPTO_SUCCESS) {
		return (rv);
	}
	reqp->nr_flags |= N2CP_GATHER;

	/* copy residual bytes to buffer */
	if (hashctx->residlen > 0) {
		BCOPY(hashctx->resid, reqp->nr_in_buf, hashctx->residlen);
		/* SHA384 and SHA512 use 128 bit counter */
		if (length_length == 16) {
			n2cp_add_ctr_bits((uchar_t *)hashctx->total_bits,
			    hashctx->residlen * 8, 128);
		} else {
			hashctx->total_bits[1] += hashctx->residlen * 8;
		}
	}

	/* add pad bytes to the final block */
	cb->cw_src_addr = reqp->nr_in_buf_paddr;
	cb->cw_length = reqp->nr_blocksz - 1;
	pad_bytes = reqp->nr_blocksz - hashctx->residlen - length_length;
	limit = reqp->nr_blocksz - length_length;
	if (pad_bytes > 0 && pad_bytes <= limit) {
		/* padding fits into one block */
		length_index = reqp->nr_blocksz - length_length;
	} else {
		/* padding spills over into next block */
		pad_bytes = (reqp->nr_blocksz * 2) - hashctx->residlen -
		    length_length;
		length_index = (reqp->nr_blocksz * 2) - length_length;
		cb->cw_length += reqp->nr_blocksz;
	}
	reqp->nr_in_buf[hashctx->residlen] = 0x80;
	pad_bytes--;
	bzero(&reqp->nr_in_buf[hashctx->residlen + 1], pad_bytes);

	/* add total length in bits to the final block */
	if (length_length == 16) {
		BCOPY(hashctx->total_bits, &reqp->nr_in_buf[length_index], 16);
	} else {
		if ((reqp->nr_cmd & N2CP_CMD_MASK) == MD5_MECH_INFO_TYPE) {
			uint64_t foo;

			foo = BSWAP_64(hashctx->total_bits[1]);
			BCOPY(&foo, &reqp->nr_in_buf[length_index], 8);
		} else {
			BCOPY(&hashctx->total_bits[1],
			    &reqp->nr_in_buf[length_index], 8);
		}
	}

	/* Setup the output */
	if (n2cp_sgcheck(reqp->nr_out, N2CP_SG_PCONTIG)) {
		/* get a 64K contig mem from the pool */
		if ((rv = n2cp_get_outbuf(n2cp, reqp)) != CRYPTO_SUCCESS) {
			n2cp_free_buf(n2cp, reqp);
			return (rv);
		}
		cb->cw_dst_addr = reqp->nr_out_buf_paddr;
		reqp->nr_flags |= N2CP_SCATTER;
	} else {
		cb->cw_dst_addr = va_to_pa(n2cp_get_dataaddr(reqp->nr_out, 0));
	}

	cb->cw_auth_iv_addr = reqp->nr_context_paddr + HASH_IV_OFFSET;

	/* setup request for post-processing */
	if ((reqp->nr_cmd & N2CP_CMD_MASK) == SHA384_MECH_INFO_TYPE) {
		reqp->nr_resultlen = SHA384_DIGESTSZ;
	} else {
		reqp->nr_resultlen = cb->cw_hlen + 1;
	}

	set_auth_type_update(reqp);

	rv = n2cp_start(n2cp, reqp);
	reqp->nr_errno = rv;

	reqp->nr_callback(reqp);

/* EXPORT DELETE END */

	return (reqp->nr_errno);
}

static void
hash_done(n2cp_request_t *reqp)
{
	n2cp_hash_ctx_t *hashctx = &(reqp->nr_context->hashctx);
	cwq_cw_t *cb = &(reqp->nr_cws[0]);
	size_t len = reqp->nr_in->cd_length;
	int rv = CRYPTO_SUCCESS;
	crypto_data_t *out = reqp->nr_out;
	uint32_t saved_hlen;


/* EXPORT DELETE START */

	if (reqp->nr_errno != CRYPTO_SUCCESS) {
		rv = reqp->nr_errno;
		goto done;
	}

	/* are we done? */
	if ((reqp->nr_cmd & N2CP_OP_SINGLE) &&
	    (hashctx->total_bits[1] <= MAX_DATA_BITS) && (len == 0)) {
		if (reqp->nr_flags & N2CP_SCATTER) {
			rv = n2cp_scatter((char *)reqp->nr_out_buf,
			    reqp->nr_out, reqp->nr_resultlen);
			if (rv != CRYPTO_SUCCESS) {
				goto done;
			}
		} else {
			out->cd_length += reqp->nr_resultlen;
		}
	} else if (len > reqp->nr_blocksz ||
	    ((reqp->nr_cmd & N2CP_OP_SINGLE) && len == reqp->nr_blocksz)) {
		/*
		 * Schedule another pass since there is more than a
		 * block of data to process.
		 */
		reqp->nr_flags &= ~(N2CP_SCATTER | N2CP_GATHER);
		saved_hlen = cb->cw_hlen;
		bzero(reqp->nr_cws, reqp->nr_cwcnt * sizeof (cwq_cw_t));
		cb->cw_hlen = saved_hlen;
		if (n2cp_use_ulcwq &&
		    ((rv = n2cp_set_affinity_for_req(reqp)) !=
		    CRYPTO_SUCCESS)) {
			goto done;
		}
		rv = hash_start(reqp);
	} else if ((len > 0) ||
	    ((len == 0) && (hashctx->total_bits[1] >= MAX_DATA_BITS))) {
		/*
		 * Gather up residual bytes and construct final block.
		 */
		hashctx->residlen = (uint_t)len;
		rv = n2cp_gather(reqp->nr_in, hashctx->resid, len);
		if (rv != CRYPTO_SUCCESS)
			goto done;

		if (reqp->nr_cmd & N2CP_OP_SINGLE) {
			reqp->nr_flags &= ~(N2CP_SCATTER | N2CP_GATHER);
			reqp->nr_callback = hash_final_done;
			saved_hlen = cb->cw_hlen;
			bzero(reqp->nr_cws, reqp->nr_cwcnt * sizeof (cwq_cw_t));
			cb->cw_hlen = saved_hlen;
			if (n2cp_use_ulcwq &&
			    ((rv = n2cp_set_affinity_for_req(reqp)) !=
			    CRYPTO_SUCCESS)) {
				goto done;
			}
			rv = hash_final_start(reqp);
		}
	}
done:
	reqp->nr_errno = rv;

/* EXPORT DELETE END */

}

static void
hash_final_done(n2cp_request_t *reqp)
{
	int rv = CRYPTO_SUCCESS;
	crypto_data_t *out = reqp->nr_out;

/* EXPORT DELETE START */

	if (reqp->nr_errno != CRYPTO_SUCCESS) {
		rv = reqp->nr_errno;
		goto done;
	}

	out->cd_length = 0;
	if (reqp->nr_flags & N2CP_SCATTER) {
		rv = n2cp_scatter((char *)reqp->nr_out_buf,
		    reqp->nr_out, reqp->nr_resultlen);
		if (rv != CRYPTO_SUCCESS) {
			goto done;
		}
	} else {
		out->cd_length += reqp->nr_resultlen;
	}

done:
	reqp->nr_errno = rv;

/* EXPORT DELETE END */

}
