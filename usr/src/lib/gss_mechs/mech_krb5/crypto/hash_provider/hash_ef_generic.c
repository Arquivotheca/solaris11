/*
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <k5-int.h>
#include <des_int.h>

krb5_error_code
k5_ef_hash(krb5_context context,
	CK_MECHANISM *mechanism,
	unsigned int icount,
	krb5_const krb5_data *input,
	krb5_data *output)
{
	CK_RV rv;
	int i;
	CK_ULONG outlen = output->length;

	if ((rv = C_DigestInit(krb_ctx_hSession(context), mechanism)) !=
	    CKR_OK) {
		return (PKCS_ERR);
	}

	for (i = 0; i < icount; i++) {
		if ((rv = C_DigestUpdate(krb_ctx_hSession(context),
		    (CK_BYTE_PTR)input[i].data,
		    (CK_ULONG)input[i].length)) != CKR_OK) {
			return (PKCS_ERR);
		}
	}

	if ((rv = C_DigestFinal(krb_ctx_hSession(context),
	    (CK_BYTE_PTR)output->data, &outlen)) != CKR_OK) {
		return (PKCS_ERR);
	}

	/* Narrowing conversion OK because hashes are much smaller than 2^32 */
	output->length = outlen;

	return (0);
}


/*
 * Ideally, this would use the PKCS#11 interface
 * for doing DES_CBC_MAC_* operations, but for now we
 * can fake it by using the des-cbc crypto operation.
 * and truncating the output.
 */
krb5_error_code
k5_ef_mac(krb5_context context,
	krb5_keyblock *key,
	krb5_data *ivec,
	krb5_const krb5_data *input,
	krb5_data *output)
{
	krb5_error_code retval = 0;
	char *outbuf = NULL;
	char *inbuf = NULL;
	int inlen;
	int outlen;

	/*
	 * This is ugly but necessary until proper PKCS#11
	 * interface is ready.
	 */
	inlen = K5ROUNDUP(input->length, 8);
	outlen = inlen;

	if (inlen != input->length) {
		inbuf = (char *)malloc(inlen);
		if (inbuf == NULL)
			retval = ENOMEM;
	}
	else
		inbuf = input->data;

	outbuf = calloc(outlen, sizeof (char));
	if (outbuf == NULL)
		retval = ENOMEM;
	if (outbuf != NULL && inbuf != NULL) {
		if (inlen != input->length) {
			(void) memset(inbuf, 0, inlen);
			(void) memcpy(inbuf, input->data, input->length);
		}
		retval = mit_des_cbc_encrypt(context,
		    (const mit_des_cblock *)inbuf,
		    (mit_des_cblock *)outbuf,
		    inlen, key,
		    (unsigned char *)ivec->data, 1);

		if (retval == 0) {
			(void) memcpy(output->data, &outbuf[outlen-8], 8);
			output->length = 8;
		}
	}
	if (inlen != input->length && inbuf != NULL)
		free(inbuf);
	if (outbuf != NULL)
		free(outbuf);
	return (retval);
}
