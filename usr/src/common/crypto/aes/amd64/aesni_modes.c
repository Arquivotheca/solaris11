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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file replaces the aes_decrypt_contiguous_blocks() and
 * aes_encrypt_contiguous_blocks() functions in aes_modes.c with
 * Intel 64-specific functions.  These functions execute AESNI instructions
 * when it detects the microprocessor has the capability.  Otherwise, they
 * execute generic hardware-independent software functions.
 *
 * I based the encrypt and decrypt functions here on similarly-named
 * functions in ../../modes/{ecb.c|cbc.c|ctr.c}, and added-in
 * calls to optimized assembly function calls.
 */

#include <sys/types.h>
#include <sys/byteorder.h>
#include <modes/modes.h>
#include "aes_impl.h"
#include "aesni.h"

#ifdef _KERNEL
#include <sys/cpuvar.h>		/* cpu_t, CPU */
#include <sys/x86_archext.h>	/* is_x86_feature(), X86FSET_* */
#include <sys/disp.h>		/* kpreempt_disable(), kpreempt_enable */

/* Workaround for no XMM kernel thread save/restore */
#define	KPREEMPT_DISABLE	kpreempt_disable()
#define	KPREEMPT_ENABLE		kpreempt_enable()

#else
#include <strings.h>		/* bcopy() */
#include <sys/auxv.h>		/* getisax() */
#include <sys/auxv_386.h>	/* AV_386_AES bit */
#define	KPREEMPT_DISABLE
#define	KPREEMPT_ENABLE
#endif	/* _KERNEL */

#define	OTHER_BUFFER(a, ctx) \
	(((a) == (ctx)->cbc_lastblock) ? (ctx)->cbc_iv : (ctx)->cbc_lastblock)

/* Relevant processor feature flags */
#define	PF_NONE			0
#define	PF_UNINITIALIZED	1
#define	PF_AES			2
#define	PF_PCLMUL		4
static uint32_t processor_features = PF_UNINITIALIZED;

/* AES-NI assembly functions for each AES mode */
typedef	void (*aes_ecb_crypt_t)(unsigned char *ks, unsigned char *pt,
    unsigned char *ct, size_t length);
typedef	void (*aes_cbc_encrypt_t)(unsigned char *ks, unsigned char *pt,
    unsigned char *ct, size_t length, unsigned char *IV);
typedef	void (*aes_cbc_decrypt_t)(unsigned char *ks, unsigned char *pt,
    unsigned char *ct, size_t length, unsigned char *IV,
    unsigned char *feedback);
typedef void (*aes_ctr_crypt_t)(unsigned char *ks, unsigned char *pt,
    unsigned char *ct, size_t length, unsigned char *IV,
    unsigned char *feedback);

typedef union {
	uint64_t	aes_block[2];
	long double	force_align128;	/* 0 mod 128 alignment */
	} align_aes_block_128_t;


/*
 * Return relevant features on this processor:
 * PF_AES		if AES-NI instructions are supported
 * PF_PCLMUL		if and PCLMUL* instructions are supported
 * PF_AES|PF_PCLMUL	if both AES-NI and PCLMUL* instructions are supported
 * PF_NONE (0)		if AES-NI and PCLMUL* instructions are not supported
 *
 * Note: the userland version uses getisax().  The kernel version uses
 * is_x86_feature().
 */
static uint32_t
get_processor_features(void)
{
	uint32_t res = PF_NONE;

#ifdef _KERNEL
	if (is_x86_feature(x86_featureset, X86FSET_AES))
		res |= PF_AES;
	if (is_x86_feature(x86_featureset, X86FSET_PCLMULQDQ))
		res |= PF_PCLMUL;
#else
	uint_t		ui = 0;

	(void) getisax(&ui, 1);
	if ((ui & AV_386_AES) != 0)
		res |= PF_AES;
	if ((ui & AV_386_PCLMULQDQ) != 0)
		res |= PF_PCLMUL;
#endif	/* _KERNEL */

	return (res);
}


/*
 * AES encrypt/decrypt function in Electronic Code Book (ECB) mode
 * using AES-NI instructions.
 *
 * Note: based on function ecb_cipher_contiguous_blocks() in ../../modes/ecb.c.
 * The last 2 parameters there are hard coded here:
 * block_size becomes AES_BLOCK_LEN (16), and
 * cipher() becomes aes_ecb_encrypt_asm() or aes_ecb_decrypt_asm().
 */
static int
aesni_ecb_cipher_contiguous_blocks(ecb_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out, boolean_t encrypt)
{
	aes_key_t		*aes_key = ctx->ecb_keysched;
	aes_ecb_crypt_t		aes_ecb_encrypt_asm, aes_ecb_decrypt_asm;
	size_t			remainder = length;
	size_t			need;
	uint8_t			*datap = (uint8_t *)data;
	uint8_t			*blockp, *outbufp;
	void			*iov_or_mp;
	offset_t		offset;
	uint8_t			*out_data_1, *out_data_2;
	size_t			out_data_1_len, amount_to_cipher;

	if (encrypt) {
		switch (aes_key->nr) {
		case 10:
			aes_ecb_encrypt_asm = aes128_ecb_encrypt_asm;
			break;
		case 12:
			aes_ecb_encrypt_asm = aes192_ecb_encrypt_asm;
			break;
		case 14:
			aes_ecb_encrypt_asm = aes256_ecb_encrypt_asm;
			break;
		default:
			return (CRYPTO_ARGUMENTS_BAD);
		}
	} else { /* decrypt */
		switch (aes_key->nr) {
		case 10:
			aes_ecb_decrypt_asm = aes128_ecb_decrypt_asm;
			break;
		case 12:
			aes_ecb_decrypt_asm = aes192_ecb_decrypt_asm;
			break;
		case 14:
			aes_ecb_decrypt_asm = aes256_ecb_decrypt_asm;
			break;
		default:
			return (CRYPTO_ARGUMENTS_BAD);
		}
	}

	/* Check to see if we have a full block */
	if (length + ctx->ecb_remainder_len < AES_BLOCK_LEN) {
		/* accumulate bytes here and return */
		bcopy(datap,
		    (uint8_t *)ctx->ecb_remainder + ctx->ecb_remainder_len,
		    length);
		ctx->ecb_remainder_len += length;
		ctx->ecb_copy_to = datap;
		return (CRYPTO_SUCCESS);
	}

	outbufp = (uint8_t *)ctx->ecb_iv;
	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	do {
		if (ctx->ecb_remainder_len > 0) {
			/* Unprocessed data from last call */
			need = AES_BLOCK_LEN - ctx->ecb_remainder_len;

			if (need > remainder)
				return (CRYPTO_DATA_LEN_RANGE);

			bcopy(datap, &((uint8_t *)ctx->ecb_remainder)
			    [ctx->ecb_remainder_len], need);

			blockp = (uint8_t *)ctx->ecb_remainder;
			amount_to_cipher = AES_BLOCK_LEN;

		} else { /* whole (not fragmented) block(s) */
			blockp = datap;
			/* Round down to 0 mod AES_BLOCK_LEN */
			amount_to_cipher = ((size_t)&data[length] -
			    (size_t)datap) & ~0xfUL;
		}

		if (out == NULL) { /* in-place (no output buffer) */
			KPREEMPT_DISABLE;
			if (encrypt) {
				aes_ecb_encrypt_asm(
				    (unsigned char *)&aes_key->encr_ks.ks32[0],
				    blockp, blockp, amount_to_cipher);
			} else {
				aes_ecb_decrypt_asm(
				    (unsigned char *)&aes_key->decr_ks.ks32[0],
				    blockp, blockp, amount_to_cipher);
			}
			KPREEMPT_ENABLE;
			ctx->ecb_lastp = blockp;

			if (ctx->ecb_remainder_len > 0) {
				bcopy(blockp, ctx->ecb_copy_to,
				    ctx->ecb_remainder_len);
				bcopy(blockp + ctx->ecb_remainder_len, datap,
				    need);
			}

		} else { /* 2 buffers */
			crypto_get_ptrs(out, &iov_or_mp, &offset, &out_data_1,
			    &out_data_1_len, &out_data_2, amount_to_cipher);

			if (out_data_1_len > AES_BLOCK_LEN) {
				/* multiple blocks */
				amount_to_cipher = out_data_1_len & ~0xfUL;
				KPREEMPT_DISABLE;
				if (encrypt) {
					aes_ecb_encrypt_asm(
					    (unsigned char *)
					    &aes_key->encr_ks.ks32[0],
					    datap, out_data_1,
					    amount_to_cipher);
				} else {
					aes_ecb_decrypt_asm(
					    (unsigned char *)
					    &aes_key->decr_ks.ks32[0],
					    out_data_1, datap,
					    amount_to_cipher);
				}
				KPREEMPT_ENABLE;

			} else { /* 1 block */
				amount_to_cipher = AES_BLOCK_LEN;
				KPREEMPT_DISABLE;
				if (encrypt) {
					aes_ecb_encrypt_asm(
					    (unsigned char *)
					    &aes_key->encr_ks.ks32[0],
					    blockp, outbufp, AES_BLOCK_LEN);
				} else {
					aes_ecb_decrypt_asm(
					    (unsigned char *)
					    &aes_key->decr_ks.ks32[0],
					    outbufp, blockp, AES_BLOCK_LEN);
				}
				KPREEMPT_ENABLE;

				/* copy block to where it belongs */
				bcopy(outbufp, out_data_1, out_data_1_len);
				if (out_data_2 != NULL) {
					bcopy(outbufp + out_data_1_len,
					    out_data_2,
					    AES_BLOCK_LEN - out_data_1_len);
				}
			}

			/* update offset */
			out->cd_offset += amount_to_cipher;
		}

		/* Update pointer to next block of data to be processed. */
		if (ctx->ecb_remainder_len != 0) {
			datap += need;
			ctx->ecb_remainder_len = 0;
		} else {
			datap += amount_to_cipher;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		/* Incomplete last block. */
		if (remainder > 0 && remainder < AES_BLOCK_LEN) {
			bcopy(datap, ctx->ecb_remainder, remainder);
			ctx->ecb_remainder_len = remainder;
			ctx->ecb_copy_to = datap;
			return (CRYPTO_SUCCESS);
		}
		ctx->ecb_copy_to = NULL;

	} while (remainder > 0);

	return (CRYPTO_SUCCESS);
}


/*
 * AES encrypt function in Cipher Block Chaining (CBC) mode
 * using AES-NI instructions.
 *
 * Note: based on function cbc_encrypt_contiguous_blocks() in ../../modes/cbc.c.
 * Three parameters there are hard coded here:
 * block_size becomes AES_BLOCK_LEN (16),
 * xor_block() and encrypt() becomes aes_cbc_encrypt_asm(), and
 * copy_block() becomes aes_copy_block().
 */
static int
aesni_cbc_encrypt_contiguous_blocks(cbc_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	aes_key_t		*aes_key = ctx->cbc_keysched;
	aes_cbc_encrypt_t	aes_cbc_encrypt_asm;
	size_t			remainder = length;
	size_t			need;
	uint8_t			*datap = (uint8_t *)data;
	uint8_t			*blockp, *lastp;
	void			*iov_or_mp;
	offset_t		offset;
	uint8_t			*out_data_1, *out_data_2;
	size_t			out_data_1_len, amount_to_encrypt;

	switch (aes_key->nr) {
	case 10:
		aes_cbc_encrypt_asm = aes128_cbc_encrypt_asm;
		break;
	case 12:
		aes_cbc_encrypt_asm = aes192_cbc_encrypt_asm;
		break;
	case 14:
		aes_cbc_encrypt_asm = aes256_cbc_encrypt_asm;
		break;
	default:
		return (CRYPTO_ARGUMENTS_BAD);
	}


	/* Check to see if we have a full block */
	if (length + ctx->cbc_remainder_len < AES_BLOCK_LEN) {
		/* accumulate bytes here and return */
		bcopy(datap,
		    (uint8_t *)ctx->cbc_remainder + ctx->cbc_remainder_len,
		    length);
		ctx->cbc_remainder_len += length;
		ctx->cbc_copy_to = datap;
		return (CRYPTO_SUCCESS);
	}


	lastp = (uint8_t *)ctx->cbc_iv;
	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	do {
		if (ctx->cbc_remainder_len > 0) {
			/* Unprocessed data from last call */
			need = AES_BLOCK_LEN - ctx->cbc_remainder_len;

			if (need > remainder) {
				return (CRYPTO_DATA_LEN_RANGE);
			}

			bcopy(datap, &((uint8_t *)ctx->cbc_remainder)
			    [ctx->cbc_remainder_len], need);

			blockp = (uint8_t *)ctx->cbc_remainder;
			amount_to_encrypt = AES_BLOCK_LEN;

		} else { /* whole (not fragmented) block(s) */
			blockp = datap;
			/* Round down to 0 mod AES_BLOCK_LEN */
			amount_to_encrypt = ((size_t)&data[length] -
			    (size_t)datap) & ~0xfUL;
		}

		if (out == NULL) { /* in-place (no output buffer) */
			/*
			 * XOR the previous cipher block or IV (lastp) with the
			 * current input block, then encrypt with AES.
			 */
			KPREEMPT_DISABLE;
			aes_cbc_encrypt_asm(
			    (unsigned char *)&aes_key->encr_ks.ks32[0],
			    blockp, blockp, amount_to_encrypt, lastp);
			KPREEMPT_ENABLE;

			/* Save last encrypted block to xor with next block */
			lastp = ctx->cbc_lastp = (uint8_t *)ctx->cbc_iv;
			aes_copy_block(
			    &blockp[amount_to_encrypt - AES_BLOCK_LEN], lastp);

			if (ctx->cbc_remainder_len > 0) {
				bcopy(blockp, ctx->cbc_copy_to,
				    ctx->cbc_remainder_len);
				bcopy(blockp + ctx->cbc_remainder_len, datap,
				    need);
			}

		} else { /* 2 buffers */
			crypto_get_ptrs(out, &iov_or_mp, &offset, &out_data_1,
			    &out_data_1_len, &out_data_2, amount_to_encrypt);

			if ((out_data_1_len > AES_BLOCK_LEN) &&
			    (ctx->cbc_remainder_len == 0)) {
				/* multiple blocks */
				amount_to_encrypt = out_data_1_len & ~0xfUL;


				/*
				 * XOR the previous cipher block or IV with the
				 * current input block, then encrypt with AES.
				 */
				KPREEMPT_DISABLE;
				aes_cbc_encrypt_asm(
				    (unsigned char *)&aes_key->encr_ks.ks32[0],
				    datap, out_data_1, amount_to_encrypt,
				    lastp);
				KPREEMPT_ENABLE;

				/*
				 * Save last encrypted block to xor with
				 * next block
				 */
				lastp = ctx->cbc_lastp = (uint8_t *)ctx->cbc_iv;
				aes_copy_block(&out_data_1[amount_to_encrypt
				    - AES_BLOCK_LEN], lastp);

			} else { /* 1 block */
				amount_to_encrypt = AES_BLOCK_LEN;

				/*
				 * XOR the previous cipher block or IV with the
				 * current input block, then encrypt with AES.
				 */
				KPREEMPT_DISABLE;
				aes_cbc_encrypt_asm(
				    (unsigned char *)&aes_key->encr_ks.ks32[0],
				    blockp, lastp, amount_to_encrypt, lastp);
				KPREEMPT_ENABLE;

				/* copy block to where it belongs */
				if (out_data_1_len == AES_BLOCK_LEN) {
					aes_copy_block(lastp, out_data_1);
				} else { /* partial or fragmented block */
					bcopy(lastp, out_data_1,
					    out_data_1_len);
					if (out_data_2 != NULL) {
						bcopy(lastp + out_data_1_len,
						    out_data_2, AES_BLOCK_LEN -
						    out_data_1_len);
					}
				}

			}

			/* update offset */
			out->cd_offset += amount_to_encrypt;
		}

		/* Update pointer to next block of data to be processed. */
		if (ctx->cbc_remainder_len != 0) {
			datap += need;
			ctx->cbc_remainder_len = 0;
		} else {
			datap += amount_to_encrypt;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		/* Incomplete last block. */
		if (remainder > 0 && remainder < AES_BLOCK_LEN) {
			bcopy(datap, ctx->cbc_remainder, remainder);
			ctx->cbc_remainder_len = remainder;
			ctx->cbc_copy_to = datap;
			break;
		}
		ctx->cbc_copy_to = NULL;

	} while (remainder > 0);

	/*
	 * Save the last encrypted block in the context.
	 */
	if ((ctx->cbc_lastp != NULL) &&
	    (ctx->cbc_lastp != (uint8_t *)ctx->cbc_iv)) {
		aes_copy_block((uint8_t *)ctx->cbc_lastp,
		    (uint8_t *)ctx->cbc_iv);
		ctx->cbc_lastp = (uint8_t *)ctx->cbc_iv;
	}

	return (CRYPTO_SUCCESS);
}


/*
 * AES decrypt function in Cipher Block Chaining (CBC) mode
 * using AES-NI instructions.
 *
 * Note: based on function cbc_decrypt_contiguous_blocks() in ../../modes/cbc.c.
 * Three parameters there are hard coded here:
 * block_size becomes AES_BLOCK_LEN (16),
 * xor_block() and decrypt() becomes aes_cbc_decrypt_asm(), and
 * copy_block() becomes aes_copy_block().
 *
 * Note2: previous cipher block must be saved before decryption for feedback
 * into next block (if the encrypted and decrypted blocks use the same buffer).
 */
static int
aesni_cbc_decrypt_contiguous_blocks(cbc_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	aes_key_t		*aes_key = ctx->cbc_keysched;
	aes_cbc_decrypt_t	aes_cbc_decrypt_asm;
	size_t			remainder = length;
	size_t			need;
	uint8_t			*datap = (uint8_t *)data;
	uint8_t			*blockp, *lastp;
	void			*iov_or_mp;
	offset_t		offset;
	uint8_t			*out_data_1, *out_data_2;
	size_t			out_data_1_len, amount_to_decrypt;
	align_aes_block_128_t	feedback;

	switch (aes_key->nr) {
	case 10:
		aes_cbc_decrypt_asm = aes128_cbc_decrypt_asm;
		break;
	case 12:
		aes_cbc_decrypt_asm = aes192_cbc_decrypt_asm;
		break;
	case 14:
		aes_cbc_decrypt_asm = aes256_cbc_decrypt_asm;
		break;
	default:
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/* Check to see if we have a full block */
	if (length + ctx->cbc_remainder_len < AES_BLOCK_LEN) {
		/* accumulate bytes here and return */
		bcopy(datap,
		    (uint8_t *)ctx->cbc_remainder + ctx->cbc_remainder_len,
		    length);
		ctx->cbc_remainder_len += length;
		ctx->cbc_copy_to = datap;
		return (CRYPTO_SUCCESS);
	}

	/* Save last cipher block for use in xoring next block */
	lastp = ctx->cbc_lastp;
	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	do {
		if (ctx->cbc_remainder_len > 0) {
			/* Unprocessed data from last call */
			need = AES_BLOCK_LEN - ctx->cbc_remainder_len;

			if (need > remainder) {
				return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
			}

			bcopy(datap, &((uint8_t *)ctx->cbc_remainder)
			    [ctx->cbc_remainder_len], need);

			blockp = (uint8_t *)ctx->cbc_remainder;
			amount_to_decrypt = AES_BLOCK_LEN;

		} else { /* whole (not fragmented) block(s) */
			blockp = datap;
			amount_to_decrypt = ((size_t)&data[length] -
			    (size_t)datap) & ~0xfUL;
		}

		/* copy cipher block for use in xoring next block */
		aes_copy_block(&blockp[(amount_to_decrypt - 1) & ~0xfUL],
		    (uint8_t *)OTHER_BUFFER((void *)lastp, ctx));

		if (out != NULL) { /* 2 buffers */
			/*
			 * XOR the previous cipher block or IV with the
			 * current decrypted block, then encrypt with
			 * AES.
			 */
			crypto_get_ptrs(out, &iov_or_mp, &offset,
			    &out_data_1, &out_data_1_len, &out_data_2,
			    amount_to_decrypt);

			if ((out_data_1_len > AES_BLOCK_LEN) &&
			    (ctx->cbc_remainder_len == 0)) {
				/* multiple blocks */

				KPREEMPT_DISABLE;
				aes_cbc_decrypt_asm(
				    (unsigned char *)&aes_key->decr_ks.ks32[0],
				    out_data_1, datap,
				    amount_to_decrypt, lastp,
				    (uint8_t *)feedback.aes_block);
				KPREEMPT_ENABLE;

				/*
				 * set to last cipher block for xoring next
				 * block
				 */
				lastp = (uint8_t *)OTHER_BUFFER((void *)lastp,
				    ctx);

			} else { /* 1 block */
				KPREEMPT_DISABLE;
				aes_cbc_decrypt_asm(
				    (unsigned char *)&aes_key->decr_ks.ks32[0],
				    (uint8_t *)&ctx->cbc_remainder[0], blockp,
				    amount_to_decrypt, lastp,
				    (uint8_t *)feedback.aes_block);
				KPREEMPT_ENABLE;

				blockp = (uint8_t *)&ctx->cbc_remainder[0];

				/*
				 * set to saved cipher block for xoring next
				 * block
				 */
				lastp = (uint8_t *)OTHER_BUFFER((void *)lastp,
				    ctx);
				bcopy(blockp, out_data_1, out_data_1_len);
				if (out_data_2 != NULL) {
					bcopy(blockp + out_data_1_len,
					    out_data_2,
					    AES_BLOCK_LEN - out_data_1_len);
				}
			}

			/* update offset */
			out->cd_offset += amount_to_decrypt;

		} else { /* in-place (no output buffer) */
			KPREEMPT_DISABLE;
			aes_cbc_decrypt_asm(
			    (unsigned char *)&aes_key->decr_ks.ks32[0], blockp,
			    blockp, amount_to_decrypt, lastp,
			    (uint8_t *)feedback.aes_block);
			KPREEMPT_ENABLE;


			/* set to saved cipher block for xoring next block */
			lastp = (uint8_t *)OTHER_BUFFER((void *)lastp, ctx);

			if (ctx->cbc_remainder_len > 0) {
				/* copy temporary block to where it belongs */
				bcopy(blockp, ctx->cbc_copy_to,
				    ctx->cbc_remainder_len);
				bcopy(blockp + ctx->cbc_remainder_len, datap,
				    need);
			}
		}

		/* Update pointer to next block of data to be processed. */
		if (ctx->cbc_remainder_len != 0) {
			datap += need;
			ctx->cbc_remainder_len = 0;
		} else {
			datap += amount_to_decrypt;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		/* Incomplete last block. */
		if (remainder > 0 && remainder < AES_BLOCK_LEN) {
			bcopy(datap, ctx->cbc_remainder, remainder);
			ctx->cbc_remainder_len = remainder;
			ctx->cbc_lastp = lastp;
			ctx->cbc_copy_to = datap;
			return (CRYPTO_SUCCESS);
		}
		ctx->cbc_copy_to = NULL;

	} while (remainder > 0);

	ctx->cbc_lastp = lastp;
	return (CRYPTO_SUCCESS);
}


/*
 * For counter mode encryption, ensure the lower half of the counter
 * doesn't wrap-around in assembly code.  If this is about to occur, either
 * (1) reduce the length so the counter doesn't wrap-around
 *	if the counter is less than the wrap-around point, or,
 * (2) set the length to 1 AES block
 *	if the counter is at the exact wrap-around point (e.g., 0xffffffff).
 *
 * This is done because the assembly code is optimized so it can encrypt
 * one or several AES blocks for each call.  But the assembly
 * doesn't handle counter wrap-around with masking
 * (C code handles this instead).
 *
 * Parameters (all parameters are in little-endian format):
 * ctr_lower_swapped		Least-significant 64-bits of counter
 * ctr_lower_masked_swapped	Least-significant 64-bits of counter mask
 * amount_to_encrypt		length to encrypt, in bytes
 *
 *
 * This function is called by aesni_ctr_mode_contiguous_blocks().
 */
static size_t
reduce_if_wrap_around(uint64_t ctr_lower_swapped,
		uint64_t ctr_lower_mask_swapped, size_t amount_to_encrypt)
{
	uint64_t	starting_lower_ctr, ending_lower_ctr;
	size_t		blocks_to_encrypt;

	if (amount_to_encrypt <= AES_BLOCK_LEN) {
		return (amount_to_encrypt);
	}

	starting_lower_ctr = ctr_lower_swapped & ctr_lower_mask_swapped;

	if (starting_lower_ctr == ctr_lower_mask_swapped) {
		/* Just process 1 block for lower counter wrap-around */
		amount_to_encrypt = AES_BLOCK_LEN;

	} else {
		/*
		 * Ensure lower counter doesn't wraparound in assembly
		 * code by reducing amount to encrypt if necessary
		 */
		blocks_to_encrypt = ((amount_to_encrypt -
		    AES_BLOCK_LEN) >> 4) + 1;
		ending_lower_ctr = starting_lower_ctr +
		    blocks_to_encrypt;
		if ((ending_lower_ctr > ctr_lower_mask_swapped) ||
		    (ending_lower_ctr <= starting_lower_ctr)) {
			/* need to reduce length to avoid wrap-around */
			amount_to_encrypt = (ctr_lower_mask_swapped -
			    starting_lower_ctr) * AES_BLOCK_LEN;
		}
	}
	return (amount_to_encrypt);
}

/*
 * XOR less than or equal to block size bytes and copy to output.
 */
static void
xor_and_copy(void *iov_or_mp, offset_t *offset, size_t block_size,
    crypto_data_t *out, uint8_t *datap, uint8_t *encrypted_feedback, size_t amt,
    void (*xor_block)(uint8_t *, uint8_t *))
{
	uint8_t *out_data_1;
	uint8_t *out_data_2;
	uint8_t *src, *dst;
	size_t out_data_1_len;
	int i;

	src = (out == NULL) ? encrypted_feedback : datap;
	dst = (out == NULL) ? datap : encrypted_feedback;

	if (amt == block_size) {
		xor_block(src, dst);
	} else {
		for (i = 0; i < amt; i++)
			dst[i] ^= src[i];
	}
	if (out != NULL) {
		crypto_get_ptrs(out, iov_or_mp, offset, &out_data_1,
		    &out_data_1_len, &out_data_2, amt);

		bcopy(dst, out_data_1, out_data_1_len);
		if (out_data_2 != NULL) {
			bcopy(dst + out_data_1_len, out_data_2,
			    amt - out_data_1_len);
		}
		out->cd_offset += amt;
	}
}

/*
 * Increment up to 128 bit counter depending on the mask.
 */
static void
increment_counter(ctr_ctx_t *ctx)
{
	uint64_t lower_counter, upper_counter, ctr_lower_mask, ctr_upper_mask;

	ctr_lower_mask = ctx->ctr_lower_mask;
	lower_counter = ntohll(ctx->ctr_cb[1] & ctr_lower_mask);
	lower_counter = htonll(lower_counter + 1) & ctr_lower_mask;
	ctx->ctr_cb[1] = (ctx->ctr_cb[1] & ~ctr_lower_mask) | lower_counter;

	/* wrap around */
	if ((lower_counter == 0) && ((ctx->ctr_lower_mask + 1) == 0)) {
		ctr_upper_mask = ctx->ctr_upper_mask;
		upper_counter = ntohll(ctx->ctr_cb[0] & ctr_upper_mask);
		upper_counter = htonll(upper_counter + 1) & ctr_upper_mask;
		ctx->ctr_cb[0] = (ctx->ctr_cb[0] & ~ctr_upper_mask) |
		    upper_counter;
	}
}

/*
 * AES encrypt/decrypt in counter (CTR) mode function using AES-NI instructions.
 *
 * Note: based on function ctr_mode_contiguous_blocks() in ../../modes/ctr.c.
 * The last 3 parameters there are hard coded here:
 * block_size becomes AES_BLOCK_LEN (16),
 * cipher() becomes aes{128,192,256}_ctr_asm(),
 * and xor_block is not used in this implementation.
 */
static int
aesni_ctr_mode_contiguous_blocks(ctr_ctx_t *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	aes_key_t		*aes_key = ctx->ctr_keysched;
	aes_ctr_crypt_t		aes_ctr_asm;
	size_t			remainder = length;
	size_t			need;
	uint8_t			*datap = (uint8_t *)data;
	uint8_t			*blockp, *outbufp;
	void			*iov_or_mp;
	offset_t		offset;
	uint8_t			*out_data_1, *out_data_2;
	size_t			amount_to_encrypt, out_data_1_len;
	size_t			remainder_len = 0;
	uint64_t		old_lower_counter, new_lower_counter;
	uint64_t		lower_counter, upper_counter;
	uint64_t		ctr_lower_mask, ctr_upper_mask;
	align_aes_block_128_t	ctr_swapped, feedback;

	if (length == 0)
		return (CRYPTO_SUCCESS);

	if (out != NULL)
		crypto_init_ptrs(out, &iov_or_mp, &offset);

	switch (aes_key->nr) {
	case 10:
		aes_ctr_asm = aes128_ctr_asm;
		break;
	case 12:
		aes_ctr_asm = aes192_ctr_asm;
		break;
	case 14:
		aes_ctr_asm = aes256_ctr_asm;
		break;
	default:
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/*
	 * Check for unused counter block bytes. Encrypt these bytes and
	 * XOR with input to produce output.
	 *
	 * Important Note: ctx->remainder_len is not used to store the
	 * number of bytes in ctx->remainder. Instead, ctx->remainder_len
	 * is the number of bytes in the counter block that are unused.
	 * Since the encrypted counter block is XORed with plaintext to produce
	 * ciphertext, we have to keep track of where we are in the processing
	 * of the counter block.
	 */
	if (ctx->ctr_remainder_len > 0) {
		uint64_t tmp[2];
		uint8_t *encrypted_counter_block;

		(void) aes_encrypt_block(ctx->ctr_keysched,
		    (uint8_t *)ctx->ctr_cb, (uint8_t *)tmp);

		need = MIN(length, ctx->ctr_remainder_len);
		encrypted_counter_block = (uint8_t *)tmp;
		encrypted_counter_block += (AES_BLOCK_LEN -
		    ctx->ctr_remainder_len);
		xor_and_copy(&iov_or_mp, &offset, AES_BLOCK_LEN, out, datap,
		    (uint8_t *)encrypted_counter_block, need, aes_xor_block);

		ctx->ctr_remainder_len -= need;
		if (ctx->ctr_remainder_len == 0)
			increment_counter(ctx);

		datap += need;
		remainder -= need;
		if (remainder == 0)
			return (CRYPTO_SUCCESS);
	}

	/*
	 * At this point, all unused counter block bytes should be consumed.
	 */
	if (remainder > 0 && remainder < AES_BLOCK_LEN) {
		uint64_t tmp[2];

		(void) aes_encrypt_block(ctx->ctr_keysched,
		    (uint8_t *)ctx->ctr_cb, (uint8_t *)tmp);

		xor_and_copy(&iov_or_mp, &offset, AES_BLOCK_LEN,
		    out, datap, (uint8_t *)tmp, remainder, aes_xor_block);

		ctx->ctr_remainder_len = (AES_BLOCK_LEN - remainder);
		return (CRYPTO_SUCCESS);
	}

	/*
	 * At this point, all unused counter block bytes are consumed and
	 * we have greater than a blocks worth of data to process.
	 */
	do {
		if (remainder_len > 0) {
			/* Unprocessed data from last call */
			need = AES_BLOCK_LEN - remainder_len;

			if (need > remainder)
				return (CRYPTO_DATA_LEN_RANGE);

			bcopy(datap, &((uint8_t *)ctx->ctr_remainder)
			    [remainder_len], need);

			blockp = (uint8_t *)ctx->ctr_remainder;
			amount_to_encrypt = AES_BLOCK_LEN;

		} else { /* whole (not fragmented) block(s) */
			blockp = datap;
			/* Round down to 0 mod AES_BLOCK_LEN */
			amount_to_encrypt = ((size_t)&data[length] -
			    (size_t)datap) & ~0xfUL;
		}

		if (out == NULL) { /* in-place (no output buffer) */
			if (remainder_len > 0) {
				/* unprocessed data from last call */
				outbufp = (uint8_t *)ctx->ctr_tmp;
			} else {
				outbufp = blockp; /* input & output the same */
			}

		} else { /* 2 buffers */
			crypto_get_ptrs(out, &iov_or_mp, &offset, &out_data_1,
			    &out_data_1_len, &out_data_2, amount_to_encrypt);
			if (out_data_1_len >= AES_BLOCK_LEN) {
				if ((out_data_1_len >= amount_to_encrypt) &&
				    (out_data_1_len >= AES_BLOCK_LEN)) {
					amount_to_encrypt = out_data_1_len &
					    ~0xfUL;
					outbufp = out_data_1;
				} else {
					amount_to_encrypt = out_data_1_len;
					outbufp = (uint8_t *)ctx->ctr_tmp;
				}

			} else { /* fragmented AES block */
				outbufp = (uint8_t *)ctx->ctr_tmp;
				amount_to_encrypt = out_data_1_len;
			}
		}

		/* Swap IV to little-endian for assembly code */
		ctr_swapped.aes_block[0] = htonll(ctx->ctr_cb[1]);
		ctr_swapped.aes_block[1] = htonll(ctx->ctr_cb[0]);

		/* Ensure lower counter doesn't wrap-around by > 1 AES block */
		if (amount_to_encrypt > AES_BLOCK_LEN) {
			amount_to_encrypt = reduce_if_wrap_around(
			    ctr_swapped.aes_block[0],
			    htonll(ctx->ctr_lower_mask), amount_to_encrypt);
		}

		/* Encrypt 1 or several AES blocks with optimized assembly: */
		KPREEMPT_DISABLE;
		aes_ctr_asm((unsigned char *)&aes_key->encr_ks.ks32[0],
		    blockp, /* input */
		    outbufp, /* output */
		    amount_to_encrypt,
		    (uint8_t *)ctr_swapped.aes_block, /* IV (counter) */
		    (uint8_t *)feedback.aes_block); /* updated, swapped IV-1 */
		KPREEMPT_ENABLE;

		/*
		 * Increment 128-bit counter by number of blocks encrypted,
		 * and swap back to big-endian.
		 */
		ctr_lower_mask = ctx->ctr_lower_mask;
		old_lower_counter = ntohll(ctx->ctr_cb[1] & ctr_lower_mask);
		new_lower_counter = old_lower_counter +
		    (amount_to_encrypt >> 4);
		lower_counter = htonll(new_lower_counter) & ctr_lower_mask;
		ctx->ctr_cb[1] = (ctx->ctr_cb[1] & ~ctr_lower_mask) |
		    lower_counter;

		/* Handle lower counter wrap-around */
		if ((new_lower_counter <= old_lower_counter) &&
		    ((ctr_lower_mask + 1) == 0)) {
			ctr_upper_mask = ctx->ctr_upper_mask;
			upper_counter = ntohll(ctx->ctr_cb[0] & ctr_upper_mask);
			upper_counter = htonll(upper_counter + 1) &
			    ctr_upper_mask;
			ctx->ctr_cb[0] = (ctx->ctr_cb[0] & ~ctr_upper_mask) |
			    upper_counter;
		}

		if (out == NULL) { /* in-place (no output buffer) */
			if (remainder_len > 0) {
				bcopy(ctx->ctr_tmp, ctx->ctr_copy_to,
				    remainder_len);
				bcopy(ctx->ctr_tmp + remainder_len,
				    datap, need);
			}
		} else { /* 2 buffers */
			if (amount_to_encrypt < AES_BLOCK_LEN) {
				/* copy block to where it belongs */
				bcopy(ctx->ctr_tmp, out_data_1,
				    out_data_1_len);
				if (out_data_2 != NULL) {
					bcopy(ctx->ctr_tmp + out_data_1_len,
					    out_data_2,
					    AES_BLOCK_LEN - out_data_1_len);
				}
			}
			/* update offset */
			out->cd_offset += amount_to_encrypt;
		}

		/* Update pointer to next block of data to be processed. */
		if (remainder_len != 0) {
			datap += need;
			remainder_len = 0;
		} else {
			datap += amount_to_encrypt;
		}

		remainder = (size_t)&data[length] - (size_t)datap;

		if (remainder > 0 && remainder < AES_BLOCK_LEN) {
			uint64_t tmp[2];

			(void) aes_encrypt_block(ctx->ctr_keysched,
			    (uint8_t *)ctx->ctr_cb, (uint8_t *)tmp);

			xor_and_copy(&iov_or_mp, &offset, AES_BLOCK_LEN, out,
			    datap, (uint8_t *)tmp, remainder, aes_xor_block);

			ctx->ctr_remainder_len = (AES_BLOCK_LEN - remainder);
			return (CRYPTO_SUCCESS);
		}
		ctx->ctr_copy_to = NULL;

	} while (remainder > 0);

	return (CRYPTO_SUCCESS);
}


/*
 * Encrypt multiple blocks of data with AES according to mode and processor
 * features.
 *
 * Note: This function replaces the generic function by the same name in
 * ../aes_modes.c.
 */
int
aes_encrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	aes_ctx_t *aes_ctx = ctx;
	int rv;

	if (processor_features == PF_UNINITIALIZED)
		processor_features = get_processor_features();

	switch (aes_ctx->ac_flags & MODE_MASK) {
	case CTR_MODE:
		if ((processor_features & PF_AES) != 0) {
			rv = aesni_ctr_mode_contiguous_blocks(ctx, data,
			    length, out);
		} else {
			rv = ctr_mode_contiguous_blocks(ctx, data, length, out,
			    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
		}
		break;

	case CCM_MODE:
		rv = ccm_mode_encrypt_contiguous_blocks(ctx, data, length,
		    out, AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
		break;

	case GCM_MODE:
	case GMAC_MODE:
		rv = gcm_mode_encrypt_contiguous_blocks(ctx, data,
		    length, out, AES_BLOCK_LEN, aes_encrypt_block,
		    aes_copy_block, aes_xor_block);
		break;

	case CFB128_MODE:
		rv = cfb_mode_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block, B_TRUE /* is_encrypt */);
		break;

	case CBC_MODE:
		if ((processor_features & PF_AES) != 0) {
			rv = aesni_cbc_encrypt_contiguous_blocks(ctx, data,
			    length, out);
		} else {
			rv = cbc_encrypt_contiguous_blocks(ctx,
			    data, length, out, AES_BLOCK_LEN, aes_encrypt_block,
			    aes_copy_block, aes_xor_block);
		}
		break;

	case ECB_MODE:
	default:
		if ((processor_features & PF_AES) != 0) {
			rv = aesni_ecb_cipher_contiguous_blocks(ctx, data,
			    length, out, B_TRUE);
		} else {
			rv = ecb_cipher_contiguous_blocks(ctx, data, length,
			    out, AES_BLOCK_LEN, aes_encrypt_block);
		}
		break;
	}
	return (rv);
}


/*
 * Decrypt multiple blocks of data with AES according to mode and processor
 * features.
 *
 * Note: This function replaces the generic function by the same name in
 * ../aes_modes.c.
 */
int
aes_decrypt_contiguous_blocks(void *ctx, char *data, size_t length,
    crypto_data_t *out)
{
	aes_ctx_t *aes_ctx = ctx;
	int rv;

	if (processor_features == PF_UNINITIALIZED)
		processor_features = get_processor_features();

	switch (aes_ctx->ac_flags & MODE_MASK) {
	case CTR_MODE:
		if ((processor_features & PF_AES) != 0) {
			rv = aesni_ctr_mode_contiguous_blocks(ctx, data,
			    length, out);
		} else {
			rv = ctr_mode_contiguous_blocks(ctx, data, length, out,
			    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
		}
		if (rv == CRYPTO_DATA_LEN_RANGE)
			rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
		break;

	case CCM_MODE:
		rv = ccm_mode_decrypt_contiguous_blocks(ctx, data, length,
		    out, AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
		break;

	case GCM_MODE:
	case GMAC_MODE:
		rv = gcm_mode_decrypt_contiguous_blocks(ctx, data,
		    length, out, AES_BLOCK_LEN, aes_encrypt_block,
		    aes_copy_block, aes_xor_block);
		break;

	case CFB128_MODE:
		rv = cfb_mode_contiguous_blocks(ctx, data, length, out,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block, B_FALSE /* is_encrypt */);
		break;

	case CBC_MODE:
		if ((processor_features & PF_AES) != 0) {
			rv = aesni_cbc_decrypt_contiguous_blocks(ctx, data,
			    length, out);
		} else {
			rv = cbc_decrypt_contiguous_blocks(ctx, data, length,
			    out, AES_BLOCK_LEN, aes_decrypt_block,
			    aes_copy_block, aes_xor_block);
		}
		break;

	case ECB_MODE:
	default:
		if ((processor_features & PF_AES) != 0) {
			rv = aesni_ecb_cipher_contiguous_blocks(ctx, data,
			    length, out, B_FALSE);
		} else {
			rv = ecb_cipher_contiguous_blocks(ctx, data, length,
			    out, AES_BLOCK_LEN, aes_decrypt_block);
		}
		if (rv == CRYPTO_DATA_LEN_RANGE)
			rv = CRYPTO_ENCRYPTED_DATA_LEN_RANGE;
		break;
	}
	return (rv);
}
