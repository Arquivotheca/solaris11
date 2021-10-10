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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <security/cryptoki.h>
#include "softSession.h"
#include "softObject.h"
#include "softCrypt.h"
#include <aes/aes_impl.h>

#ifdef __amd64
#include <sys/auxv.h>		/* getisax() */
#include <sys/auxv_386.h>	/* AV_386_AES bit */

/* Threshold to use optimized AES-NI Modes code when AES-NI is present */
#define	AESNI_MODES_THRESHOLD	256 /* bytes */


/*
 * Return 1 if executing on Intel with AES-NI instructions,
 * otherwise 0 (i.e., Intel without AES-NI or AMD64).
 * Cache the result, as the CPU can't change.
 */
int
soft_intel_aes_instructions_present(void)
{
	static int	cached_result = -1;

	if (cached_result == -1) { /* first time */
		uint_t		ui = 0;

		(void) getisax(&ui, 1);
		cached_result = (ui & AV_386_AES) != 0;
	}
	return (cached_result);
}
#endif /* __amd64 */


/*
 * Allocate context for the active encryption or decryption operation, and
 * generate AES key schedule to speed up the operation.
 */
CK_RV
soft_aes_crypt_init_common(soft_session_t *session_p,
    CK_MECHANISM_PTR pMechanism, soft_object_t *key_p,
    soft_session_op_t op)
{
	size_t size;
	soft_aes_ctx_t *soft_aes_ctx;

	soft_aes_ctx = calloc(1, sizeof (soft_aes_ctx_t));
	if (soft_aes_ctx == NULL) {
		return (CKR_HOST_MEMORY);
	}

	soft_aes_ctx->key_sched = aes_alloc_keysched(&size, 0);
	if (soft_aes_ctx->key_sched == NULL) {
		free(soft_aes_ctx);
		return (CKR_HOST_MEMORY);
	}

	soft_aes_ctx->keysched_len = size;

	/*
	 * If this is a non-sensitive key and it does NOT have
	 * a key schedule yet, then allocate one and expand it.
	 * Otherwise, if it's a non-sensitive key, and it DOES have
	 * a key schedule already attached to it, just copy the
	 * pre-expanded schedule to the context and avoid the
	 * extra key schedule expansion operation.
	 */
	if (!(key_p->bool_attr_mask & SENSITIVE_BOOL_ON)) {
		if (OBJ_KEY_SCHED(key_p) == NULL) {
			void *ks;

			(void) pthread_mutex_lock(&key_p->object_mutex);
			if (OBJ_KEY_SCHED(key_p) == NULL) {
				ks = aes_alloc_keysched(&size, 0);
				if (ks == NULL) {
					(void) pthread_mutex_unlock(
					    &key_p->object_mutex);
					free(soft_aes_ctx->key_sched);
					free(soft_aes_ctx);
					return (CKR_HOST_MEMORY);
				}
#ifdef	__sparcv9
				/* LINTED */
				aes_init_keysched(OBJ_SEC_VALUE(key_p), (uint_t)
				    (OBJ_SEC_VALUE_LEN(key_p) * 8), ks);
#else	/* !__sparcv9 */
				aes_init_keysched(OBJ_SEC_VALUE(key_p),
				    (OBJ_SEC_VALUE_LEN(key_p) * 8), ks);
#endif	/* __sparcv9 */
				OBJ_KEY_SCHED_LEN(key_p) = size;
				OBJ_KEY_SCHED(key_p) = ks;
			}
			(void) pthread_mutex_unlock(&key_p->object_mutex);
		}
		(void) memcpy(soft_aes_ctx->key_sched, OBJ_KEY_SCHED(key_p),
		    OBJ_KEY_SCHED_LEN(key_p));
		soft_aes_ctx->keysched_len = OBJ_KEY_SCHED_LEN(key_p);
	} else {
		/*
		 * Initialize key schedule for AES. aes_init_keysched()
		 * requires key length in bits.
		 */
#ifdef	__sparcv9
		/* LINTED */
		aes_init_keysched(OBJ_SEC_VALUE(key_p), (uint_t)
		    (OBJ_SEC_VALUE_LEN(key_p) * 8), soft_aes_ctx->key_sched);
#else	/* !__sparcv9 */
		aes_init_keysched(OBJ_SEC_VALUE(key_p),
		    (OBJ_SEC_VALUE_LEN(key_p) * 8), soft_aes_ctx->key_sched);
#endif	/* __sparcv9 */
	}

	soft_set_session_context(session_p, op, soft_aes_ctx, pMechanism);

	return (CKR_OK);
}


/*
 * soft_aes_encrypt_common()
 *
 * Arguments:
 *      session_p:	pointer to soft_session_t struct
 *	pData:		pointer to the input data to be encrypted
 *	ulDataLen:	length of the input data
 *	pEncrypted:	pointer to the output data after encryption
 *	pulEncryptedLen: pointer to the length of the output data
 *	update:		boolean flag indicates caller is soft_encrypt
 *			or soft_encrypt_update
 *
 * Description:
 *      This function calls the corresponding encrypt routine based
 *	on the mechanism.
 *
 * Returns:
 *      CKR_OK: success
 *      CKR_BUFFER_TOO_SMALL: the output buffer provided by application
 *			      is too small
 *	CKR_FUNCTION_FAILED: encrypt function failed
 *	CKR_DATA_LEN_RANGE: the input data is not a multiple of blocksize
 */
CK_RV
soft_aes_encrypt_common(soft_session_t *session_p, CK_BYTE_PTR pData,
    CK_ULONG ulDataLen, CK_BYTE_PTR pEncrypted,
    CK_ULONG_PTR pulEncryptedLen, boolean_t update)
{

	int rc = 0;
	CK_RV rv = CKR_OK;
	soft_aes_ctx_t *soft_aes_ctx =
	    (soft_aes_ctx_t *)session_p->encrypt.context;
	aes_ctx_t *aes_ctx;
	CK_MECHANISM_TYPE mechanism = session_p->encrypt.mech.mechanism;
	CK_BYTE *in_buf = NULL;
	CK_BYTE *out_buf = NULL;
	CK_ULONG out_len;
	CK_ULONG total_len;
	CK_ULONG remain;

	if (mechanism == CKM_AES_CTR)
		goto do_encryption;

	/*
	 * AES only takes input length that is a multiple of blocksize
	 * for C_Encrypt function with the mechanism CKM_AES_ECB or
	 * CKM_AES_CBC.
	 *
	 * AES allows any input length for C_Encrypt function with the
	 * mechanism CKM_AES_CBC_PAD and for C_EncryptUpdate function.
	 */
	if ((!update) && (mechanism != CKM_AES_CBC_PAD)) {
		if ((ulDataLen % AES_BLOCK_LEN) != 0) {
			rv = CKR_DATA_LEN_RANGE;
			goto cleanup;
		}
	}

	if (!update) {
		/*
		 * Called by C_Encrypt
		 */
		if (mechanism == CKM_AES_CBC_PAD) {
			/*
			 * For CKM_AES_CBC_PAD, compute output length to
			 * count for the padding. If the length of input
			 * data is a multiple of blocksize, then make output
			 * length to be the sum of the input length and
			 * one blocksize. Otherwise, output length will
			 * be rounded up to the next multiple of blocksize.
			 */
			out_len = AES_BLOCK_LEN *
			    (ulDataLen / AES_BLOCK_LEN + 1);
		} else {
			/*
			 * For non-padding mode, the output length will
			 * be same as the input length.
			 */
			out_len = ulDataLen;
		}

		/*
		 * If application asks for the length of the output buffer
		 * to hold the ciphertext?
		 */
		if (pEncrypted == NULL) {
			*pulEncryptedLen = out_len;
			return (CKR_OK);
		}

		/* Is the application-supplied buffer large enough? */
		if (*pulEncryptedLen < out_len) {
			*pulEncryptedLen = out_len;
			return (CKR_BUFFER_TOO_SMALL);
		}

		/* Encrypt pad bytes in a separate operation */
		if (mechanism == CKM_AES_CBC_PAD) {
			out_len -= AES_BLOCK_LEN;
		}

		in_buf = pData;
		out_buf = pEncrypted;
	} else {
		/*
		 * Called by C_EncryptUpdate
		 *
		 * Add the lengths of last remaining data and current
		 * plaintext together to get the total input length.
		 */
		total_len = soft_aes_ctx->remain_len + ulDataLen;

		/*
		 * If the total input length is less than one blocksize,
		 * or if the total input length is just one blocksize and
		 * the mechanism is CKM_AES_CBC_PAD, we will need to delay
		 * encryption until when more data comes in next
		 * C_EncryptUpdate or when C_EncryptFinal is called.
		 */
		if ((total_len < AES_BLOCK_LEN) ||
		    ((mechanism == CKM_AES_CBC_PAD) &&
		    (total_len == AES_BLOCK_LEN))) {
			if (pEncrypted != NULL) {
				/*
				 * Save input data and its length in
				 * the remaining buffer of AES context.
				 */
				(void) memcpy(soft_aes_ctx->data +
				    soft_aes_ctx->remain_len, pData, ulDataLen);
				soft_aes_ctx->remain_len += ulDataLen;
			}

			/* Set encrypted data length to 0. */
			*pulEncryptedLen = 0;
			return (CKR_OK);
		}

		/* Compute the length of remaining data. */
		remain = total_len % AES_BLOCK_LEN;

		/*
		 * Make sure that the output length is a multiple of
		 * blocksize.
		 */
		out_len = total_len - remain;

		/*
		 * If application asks for the length of the output buffer
		 * to hold the ciphertext?
		 */
		if (pEncrypted == NULL) {
			*pulEncryptedLen = out_len;
			return (CKR_OK);
		}

		/* Is the application-supplied buffer large enough? */
		if (*pulEncryptedLen < out_len) {
			*pulEncryptedLen = out_len;
			return (CKR_BUFFER_TOO_SMALL);
		}

		if (soft_aes_ctx->remain_len != 0) {
			/*
			 * Copy last remaining data and current input data
			 * to the output buffer.
			 */
			(void) memmove(pEncrypted + soft_aes_ctx->remain_len,
			    pData, out_len - soft_aes_ctx->remain_len);
			(void) memcpy(pEncrypted, soft_aes_ctx->data,
			    soft_aes_ctx->remain_len);
			bzero(soft_aes_ctx->data, soft_aes_ctx->remain_len);

			in_buf = pEncrypted;
		} else {
			in_buf = pData;
		}
		out_buf = pEncrypted;
	}

do_encryption:
	/*
	 * Begin Encryption now.
	 */
	switch (mechanism) {

	case CKM_AES_ECB:
	{
#ifdef __amd64
		if ((out_len < AESNI_MODES_THRESHOLD) ||
		    ! soft_intel_aes_instructions_present()) {
#else
		/* LINTED E_CONSTANT_CONDITION */
		if (0) {
#endif /* __amd64 */
			/*
			 * AES-NI for small buffers -
			 * call block-level AES encrypt function directly.
			 */
			ulong_t i;
			uint8_t *tmp_inbuf;
			uint8_t *tmp_outbuf;

			for (i = 0; i < out_len; i += AES_BLOCK_LEN) {
				tmp_inbuf = &in_buf[i];
				tmp_outbuf = &out_buf[i];
				/* Crunch one block of data for AES. */
				(void) aes_encrypt_block(
				    soft_aes_ctx->key_sched,
				    tmp_inbuf, tmp_outbuf);
			}

		} else {
			/*
			 * Use combined AES/ECB mode optimization in
			 * aes_encrypt_contiguous_blocks().
			 */
			crypto_data_t out;

			out.cd_format = CRYPTO_DATA_RAW;
			out.cd_offset = 0;
			out.cd_length = out_len;
			out.cd_raw.iov_base = (char *)out_buf;
			out.cd_raw.iov_len = out_len;

			/* Encrypt multiple blocks of data. */

			rc = aes_encrypt_contiguous_blocks(
			    (aes_ctx_t *)soft_aes_ctx->mode_ctx,
			    (char *)in_buf, out_len, &out);

			if (rc != 0) {
				*pulEncryptedLen = 0;
				rv = CKR_FUNCTION_FAILED;
				goto cleanup;
			}
		}

		if (update) {
			/*
			 * For encrypt update, if there is remaining
			 * data, save it and its length in the context.
			 */
			if (remain != 0)
				(void) memcpy(soft_aes_ctx->data, pData +
				    (ulDataLen - remain), remain);
			soft_aes_ctx->remain_len = remain;
		}

		*pulEncryptedLen = out_len;
		break;
	}

	case CKM_AES_CBC:
	case CKM_AES_CBC_PAD:
	{
		crypto_data_t out;

		out.cd_format = CRYPTO_DATA_RAW;
		out.cd_offset = 0;
		out.cd_length = out_len;
		out.cd_raw.iov_base = (char *)out_buf;
		out.cd_raw.iov_len = out_len;

		/* Encrypt multiple blocks of data. */
		rc = aes_encrypt_contiguous_blocks(
		    (aes_ctx_t *)soft_aes_ctx->mode_ctx,
		    (char *)in_buf, out_len, &out);

		if (rc != 0) {
			*pulEncryptedLen = 0;
			rv = CKR_FUNCTION_FAILED;
			goto cleanup;
		}

		if (update) {
			/*
			 * For encrypt update, if there is remaining data,
			 * save it and its length in the context.
			 */
			if (remain != 0)
				(void) memcpy(soft_aes_ctx->data, pData +
				    (ulDataLen - remain), remain);
			soft_aes_ctx->remain_len = remain;

		} else if (mechanism == CKM_AES_CBC_PAD) {
			/*
			 * Save the remainder of the input
			 * block in a temporary block because
			 * we don't want to overrun the buffer
			 * by tacking on pad bytes.
			 */
			CK_BYTE tmpblock[AES_BLOCK_LEN];
			(void) memcpy(tmpblock, in_buf + out_len,
			    ulDataLen - out_len);
			soft_add_pkcs7_padding(tmpblock +
			    (ulDataLen - out_len),
			    AES_BLOCK_LEN, ulDataLen - out_len);

			out.cd_offset = out_len;
			out.cd_length = AES_BLOCK_LEN;
			out.cd_raw.iov_base = (char *)out_buf;
			out.cd_raw.iov_len = out_len + AES_BLOCK_LEN;

			/* Encrypt last block containing pad bytes. */
			rc = aes_encrypt_contiguous_blocks(
			    (aes_ctx_t *)soft_aes_ctx->mode_ctx,
			    (char *)tmpblock, AES_BLOCK_LEN, &out);

			out_len += AES_BLOCK_LEN;
		}

		if (rc != 0) {
			*pulEncryptedLen = 0;
			rv = CKR_FUNCTION_FAILED;
			goto cleanup;
		}

		*pulEncryptedLen = out_len;
		break;
	}

	case CKM_AES_CTR:
	{
		crypto_data_t out;

		out.cd_format = CRYPTO_DATA_RAW;
		out.cd_offset = 0;
		out.cd_length = *pulEncryptedLen;
		out.cd_raw.iov_base = (char *)pEncrypted;
		out.cd_raw.iov_len = *pulEncryptedLen;

		rc = aes_encrypt_contiguous_blocks(soft_aes_ctx->mode_ctx,
		    (char *)pData, ulDataLen, &out);

		if (rc != 0) {
			*pulEncryptedLen = 0;
			rv = CKR_FUNCTION_FAILED;
			goto cleanup;
		}
		*pulEncryptedLen = ulDataLen;
	}
	} /* end switch */

	if (update)
		return (CKR_OK);

	/*
	 * The following code will be executed if the caller is
	 * soft_encrypt() or an error occurred. The encryption
	 * operation will be terminated so we need to do some cleanup.
	 */
cleanup:
	(void) pthread_mutex_lock(&session_p->session_mutex);
	aes_ctx = (aes_ctx_t *)soft_aes_ctx->mode_ctx;
	if (aes_ctx != NULL) {
		bzero(aes_ctx->ac_keysched, aes_ctx->ac_keysched_len);
		free(soft_aes_ctx->mode_ctx);
	}

	bzero(soft_aes_ctx->key_sched, soft_aes_ctx->keysched_len);
	free(soft_aes_ctx->key_sched);
	(void) pthread_mutex_unlock(&session_p->session_mutex);

	soft_clear_session_context(session_p, SOFT_ENCRYPT_OP,
	    sizeof (soft_aes_ctx_t));

	return (rv);
}


/*
 * soft_aes_decrypt_common()
 *
 * Arguments:
 *      session_p:	pointer to soft_session_t struct
 *	pEncrypted:	pointer to the input data to be decrypted
 *	ulEncryptedLen:	length of the input data
 *	pData:		pointer to the output data
 *	pulDataLen:	pointer to the length of the output data
 *	Update:		boolean flag indicates caller is soft_decrypt
 *			or soft_decrypt_update
 *
 * Description:
 *      This function calls the corresponding decrypt routine based
 *	on the mechanism.
 *
 * Returns:
 *      CKR_OK: success
 *      CKR_BUFFER_TOO_SMALL: the output buffer provided by application
 *			      is too small
 *	CKR_ENCRYPTED_DATA_LEN_RANGE: the input data is not a multiple
 *				      of blocksize
 *	CKR_FUNCTION_FAILED: decrypt function failed
 */
CK_RV
soft_aes_decrypt_common(soft_session_t *session_p, CK_BYTE_PTR pEncrypted,
    CK_ULONG ulEncryptedLen, CK_BYTE_PTR pData,
    CK_ULONG_PTR pulDataLen, boolean_t update)
{

	int rc = 0;
	CK_RV rv = CKR_OK;
	soft_aes_ctx_t *soft_aes_ctx =
	    (soft_aes_ctx_t *)session_p->decrypt.context;
	aes_ctx_t *aes_ctx;
	CK_MECHANISM_TYPE mechanism = session_p->decrypt.mech.mechanism;
	CK_BYTE *in_buf = NULL;
	CK_BYTE *out_buf = NULL;
	CK_ULONG out_len;
	CK_ULONG total_len;
	CK_ULONG remain;

	if (mechanism == CKM_AES_CTR)
		goto do_decryption;

	/*
	 * AES only takes input length that is a multiple of 16 bytes
	 * for C_Decrypt function with the mechanism CKM_AES_ECB,
	 * CKM_AES_CBC or CKM_AES_CBC_PAD.
	 *
	 * AES allows any input length for C_DecryptUpdate function.
	 */
	if (!update) {
		/*
		 * Called by C_Decrypt
		 */
		if ((ulEncryptedLen % AES_BLOCK_LEN) != 0) {
			rv = CKR_ENCRYPTED_DATA_LEN_RANGE;
			goto cleanup;
		}

		/*
		 * If application asks for the length of the output buffer
		 * to hold the plaintext?
		 */
		if (pData == NULL) {
			*pulDataLen = ulEncryptedLen;
			return (CKR_OK);
		}

		/* Is the application-supplied buffer large enough? */
		if (mechanism != CKM_AES_CBC_PAD) {
			if (*pulDataLen < ulEncryptedLen) {
				*pulDataLen = ulEncryptedLen;
				return (CKR_BUFFER_TOO_SMALL);
			}
			out_len = ulEncryptedLen;
		} else {
			/*
			 * For CKM_AES_CBC_PAD, we don't know how
			 * many bytes for padding at this time, so
			 * we'd assume one block was padded.
			 */
			if (*pulDataLen < (ulEncryptedLen - AES_BLOCK_LEN)) {
				*pulDataLen = ulEncryptedLen - AES_BLOCK_LEN;
				return (CKR_BUFFER_TOO_SMALL);
			}
			out_len = ulEncryptedLen - AES_BLOCK_LEN;
		}
		in_buf = pEncrypted;
		out_buf = pData;
	} else {
		/*
		 *  Called by C_DecryptUpdate
		 *
		 * Add the lengths of last remaining data and current
		 * input data together to get the total input length.
		 */
		total_len = soft_aes_ctx->remain_len + ulEncryptedLen;

		/*
		 * If the total input length is less than one blocksize,
		 * or if the total input length is just one blocksize and
		 * the mechanism is CKM_AES_CBC_PAD, we will need to delay
		 * decryption until when more data comes in next
		 * C_DecryptUpdate or when C_DecryptFinal is called.
		 */
		if ((total_len < AES_BLOCK_LEN) ||
		    ((mechanism == CKM_AES_CBC_PAD) &&
		    (total_len == AES_BLOCK_LEN))) {
			if (pData != NULL) {
				/*
				 * Save input data and its length in
				 * the remaining buffer of AES context.
				 */
				(void) memcpy(soft_aes_ctx->data +
				    soft_aes_ctx->remain_len,
				    pEncrypted, ulEncryptedLen);
				soft_aes_ctx->remain_len += ulEncryptedLen;
			}

			/* Set output data length to 0. */
			*pulDataLen = 0;
			return (CKR_OK);
		}

		/* Compute the length of remaining data. */
		remain = total_len % AES_BLOCK_LEN;

		/*
		 * Make sure that the output length is a multiple of
		 * blocksize.
		 */
		out_len = total_len - remain;

		if (mechanism == CKM_AES_CBC_PAD) {
			/*
			 * If the input data length is a multiple of
			 * blocksize, then save the last block of input
			 * data in the remaining buffer. C_DecryptFinal
			 * will handle this last block of data.
			 */
			if (remain == 0) {
				remain = AES_BLOCK_LEN;
				out_len -= AES_BLOCK_LEN;
			}
		}

		/*
		 * If application asks for the length of the output buffer
		 * to hold the plaintext?
		 */
		if (pData == NULL) {
			*pulDataLen = out_len;
			return (CKR_OK);
		}

		/*
		 * Is the application-supplied buffer large enough?
		 */
		if (*pulDataLen < out_len) {
			*pulDataLen = out_len;
			return (CKR_BUFFER_TOO_SMALL);
		}

		if (soft_aes_ctx->remain_len != 0) {
			/*
			 * Copy last remaining data and current input data
			 * to the output buffer.
			 */
			(void) memmove(pData + soft_aes_ctx->remain_len,
			    pEncrypted, out_len - soft_aes_ctx->remain_len);
			(void) memcpy(pData, soft_aes_ctx->data,
			    soft_aes_ctx->remain_len);
			bzero(soft_aes_ctx->data, soft_aes_ctx->remain_len);

			in_buf = pData;
		} else {
			in_buf = pEncrypted;
		}
		out_buf = pData;
	}

do_decryption:
	/*
	 * Begin Decryption.
	 */
	switch (mechanism) {

	case CKM_AES_ECB:
	{
#ifdef __amd64
		if ((out_len < AESNI_MODES_THRESHOLD) ||
		    ! soft_intel_aes_instructions_present()) {
#else
		/* LINTED E_CONSTANT_CONDITION */
		if (0) {
#endif
			/*
			 * AES-NI for small buffers -
			 * call block-level AES encrypt function directly.
			 */
			ulong_t i;
			uint8_t *tmp_inbuf;
			uint8_t *tmp_outbuf;

			for (i = 0; i < out_len; i += AES_BLOCK_LEN) {
				tmp_inbuf = &in_buf[i];
				tmp_outbuf = &out_buf[i];
				/* Crunch one block of data for AES. */
				(void) aes_decrypt_block(
				    soft_aes_ctx->key_sched,
				    tmp_inbuf, tmp_outbuf);
			}

		} else {
			/*
			 * Use combined AES/ECB mode optimization in
			 * aes_decrypt_contiguous_blocks().
			 */
			crypto_data_t out;

			out.cd_format = CRYPTO_DATA_RAW;
			out.cd_offset = 0;
			out.cd_length = out_len;
			out.cd_raw.iov_base = (char *)out_buf;
			out.cd_raw.iov_len = out_len;

			/* Decrypt multiple blocks of data. */

			rc = aes_decrypt_contiguous_blocks(
			    (aes_ctx_t *)soft_aes_ctx->mode_ctx,
			    (char *)in_buf, out_len, &out);

			if (rc != 0) {
				*pulDataLen = 0;
				rv = CKR_FUNCTION_FAILED;
				goto cleanup;
			}
		}

		if (update) {
			/*
			 * For decrypt update, if there is remaining
			 * data, save it and its length in the context.
			 */
			if (remain != 0)
				(void) memcpy(soft_aes_ctx->data, pEncrypted +
				    (ulEncryptedLen - remain), remain);
			soft_aes_ctx->remain_len = remain;
		}

		*pulDataLen = out_len;

		break;
	}

	case CKM_AES_CBC:
	case CKM_AES_CBC_PAD:
	{
		crypto_data_t out;
		CK_ULONG rem_len;
		uint8_t last_block[AES_BLOCK_LEN];

		out.cd_format = CRYPTO_DATA_RAW;
		out.cd_offset = 0;
		out.cd_length = out_len;
		out.cd_raw.iov_base = (char *)out_buf;
		out.cd_raw.iov_len = out_len;

		/* Decrypt multiple blocks of data. */
		rc = aes_decrypt_contiguous_blocks(
		    (aes_ctx_t *)soft_aes_ctx->mode_ctx,
		    (char *)in_buf, out_len, &out);

		if (rc != 0) {
			*pulDataLen = 0;
			rv = CKR_FUNCTION_FAILED;
			goto cleanup;
		}

		if ((mechanism == CKM_AES_CBC_PAD) && (!update)) {
			/* Decrypt last block containing pad bytes. */
			out.cd_offset = 0;
			out.cd_length = AES_BLOCK_LEN;
			out.cd_raw.iov_base = (char *)last_block;
			out.cd_raw.iov_len = AES_BLOCK_LEN;

			/* Decrypt last block containing pad bytes. */
			rc = aes_decrypt_contiguous_blocks(
			    (aes_ctx_t *)soft_aes_ctx->mode_ctx,
			    (char *)in_buf + out_len, AES_BLOCK_LEN, &out);

			if (rc != 0) {
				*pulDataLen = 0;
				rv = CKR_FUNCTION_FAILED;
				goto cleanup;
			}

			/*
			 * Remove padding bytes after decryption of
			 * ciphertext block to produce the original
			 * plaintext.
			 */
			rv = soft_remove_pkcs7_padding(last_block,
			    AES_BLOCK_LEN, &rem_len);
			if (rv == CKR_OK) {
				if (rem_len != 0)
					(void) memcpy(out_buf + out_len,
					    last_block, rem_len);
				*pulDataLen = out_len + rem_len;
			} else {
				*pulDataLen = 0;
				goto cleanup;
			}
		} else {
			*pulDataLen = out_len;
		}

		if (update) {
			/*
			 * For decrypt update, if there is remaining data
			 * save it and its length in the context.
			 */
			if (remain != 0)
				(void) memcpy(soft_aes_ctx->data, pEncrypted +
				    (ulEncryptedLen - remain), remain);
			soft_aes_ctx->remain_len = remain;
		}

		if (rc != 0) {
			*pulDataLen = 0;
			rv = CKR_FUNCTION_FAILED;
			goto cleanup;
		}

		break;
	}

	case CKM_AES_CTR:
	{
		crypto_data_t out;

		out.cd_format = CRYPTO_DATA_RAW;
		out.cd_offset = 0;
		out.cd_length = *pulDataLen;
		out.cd_raw.iov_base = (char *)pData;
		out.cd_raw.iov_len = *pulDataLen;

		rc = aes_decrypt_contiguous_blocks(soft_aes_ctx->mode_ctx,
		    (char *)pEncrypted, ulEncryptedLen, &out);

		if (rc != 0) {
			*pulDataLen = 0;
			rv = CKR_FUNCTION_FAILED;
			goto cleanup;
		}
		*pulDataLen = ulEncryptedLen;
	}
	} /* end switch */

	if (update)
		return (CKR_OK);

	/*
	 * The following code will be executed if the caller is
	 * soft_decrypt() or an error occurred. The decryption
	 * operation will be terminated so we need to do some cleanup.
	 */
cleanup:
	(void) pthread_mutex_lock(&session_p->session_mutex);
	aes_ctx = (aes_ctx_t *)soft_aes_ctx->mode_ctx;
	if (aes_ctx != NULL) {
		bzero(aes_ctx->ac_keysched, aes_ctx->ac_keysched_len);
		free(soft_aes_ctx->mode_ctx);
	}

	bzero(soft_aes_ctx->key_sched, soft_aes_ctx->keysched_len);
	free(soft_aes_ctx->key_sched);
	(void) pthread_mutex_unlock(&session_p->session_mutex);

	soft_clear_session_context(session_p, SOFT_DECRYPT_OP,
	    sizeof (soft_aes_ctx_t));

	return (rv);
}

/*
 * Allocate and initialize a context for AES ECB mode of operation.
 * This is done only for AMD64 to use combined AES/ECB mode
 * optimization.  Otherwise, the block-level AES encrypt and decrypt
 * functions are called directly.
 */
void *
aes_ecb_ctx_init(void *key_sched, size_t size)
{

	ecb_ctx_t *ecb_ctx;

	if ((ecb_ctx = calloc(1, sizeof (ecb_ctx_t))) == NULL)
		return (NULL);

	ecb_ctx->ecb_keysched = key_sched;
	ecb_ctx->ecb_keysched_len = size;
	ecb_ctx->ecb_flags |= ECB_MODE;

	return (ecb_ctx);
}


/*
 * Allocate and initialize a context for AES CBC mode of operation.
 */
void *
aes_cbc_ctx_init(void *key_sched, size_t size, uint8_t *ivec)
{

	cbc_ctx_t *cbc_ctx;

	if ((cbc_ctx = calloc(1, sizeof (cbc_ctx_t))) == NULL)
		return (NULL);

	cbc_ctx->cbc_keysched = key_sched;
	cbc_ctx->cbc_keysched_len = size;

	(void) memcpy(&cbc_ctx->cbc_iv[0], ivec, AES_BLOCK_LEN);

	cbc_ctx->cbc_lastp = (uint8_t *)cbc_ctx->cbc_iv;
	cbc_ctx->cbc_flags |= CBC_MODE;

	return (cbc_ctx);
}

/*
 * Allocate and initialize a context for AES CTR mode of operation.
 */
void *
aes_ctr_ctx_init(void *key_sched, size_t size, uint8_t *param)
{

	ctr_ctx_t *ctr_ctx;
	CK_AES_CTR_PARAMS *pp;

	/* LINTED: pointer alignment */
	pp = (CK_AES_CTR_PARAMS *)param;

	if ((ctr_ctx = calloc(1, sizeof (ctr_ctx_t))) == NULL)
		return (NULL);

	ctr_ctx->ctr_keysched = key_sched;
	ctr_ctx->ctr_keysched_len = size;

	if (ctr_init_ctx(ctr_ctx, pp->ulCounterBits, pp->cb, aes_copy_block)
	    != CRYPTO_SUCCESS) {
		free(ctr_ctx);
		return (NULL);
	}
	ctr_ctx->ctr_flags |= CTR_MODE;

	return (ctr_ctx);
}
