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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <curl/curl.h>
#include <sys/sysmacros.h>
#include <strings.h>
#include <fm/libasr.h>

#include "asr_base64.h"
#include "asr_curl.h"
#include "asr_scrk.h"
#include "asr_ssl.h"
#include "asr_stag.h"

/*
 * Initializes the ASR handle to use the DTS transport.
 */
int
asr_scrk_init(asr_handle_t *ah)
{
	return asr_set_transport(ah, "SCRK",
	    asr_scrk_register_client,
	    asr_scrk_unregister_client,
	    asr_scrk_send_msg);
}

/*
 * Given the HTTP response from a Sun web service that responds with simple
 * key-value pairs, parse it into a struct describing the fields we might be
 * interested in. An example input might look like:
 *
 * TYPE=SUCCESS
 * CODE=0
 * MESSAGE=Success
 *
 * This routine modifies the original buffer.
 */
static int
asr_scrk_parse_response(asr_buf_t *bp, nvlist_t *ret)
{
	int err = 0;
	char *pp;
	char *tok;
	char *rsp;

	if (bp == NULL || bp->asrb_length == 0)
		/* The request failed, but we have no response. */
		return (ASR_FAILURE);

	rsp = bp->asrb_data;

	for (pp = strtok_r(rsp, "\r\n", &tok); pp != NULL;
	    pp = strtok_r(NULL, "\r\n", &tok)) {
		char *eqloc, *key, *val;

		if ((eqloc = strchr(pp, '=')) == NULL)
			continue;
		*eqloc = '\0';

		key = pp;
		val = eqloc + 1;

		if (strcmp("CODE", key) == 0) {
			err |= nvlist_add_string(ret, ASR_PROP_REG_CODE, val);
			continue;
		}

		if (strcmp("MESSAGE", key) == 0) {
			err |= nvlist_add_string(
			    ret, ASR_PROP_REG_MESSAGE, val);
			continue;
		}

		if (strcmp("SC_CLIENT_REG_ID", key) == 0) {
			err |= nvlist_add_string(ret,
			    ASR_PROP_REG_CLIENT_ID, val);
			continue;
		}
	}
	return (err);
}

/*
 * Takes the result of a web RPC call, checks the response code, and logs the
 * call. The akd-internal 'scrk' log logs all phone home RPC messages with the
 * following members:
 *
 * 	service		scrk, prs, asr, supportfiles (string)
 *
 * 	method		client_registration, svctag_registration, etc. (string)
 *
 *	arguments	specific to xml-rpc call (object)
 *
 * 	result		result of call, an object with the following members:
 *
 * 		code		HTTP response code (int)
 *
 *		status		'ok', 'errror_conn', or 'error_other' for
 *				success, connectivity failure, and other
 *				failure, respectively.
 *
 * 		akerrno		our interpretation of the rpc result (ak_errno)
 *
 * 		akerrmsg	ditto (ak_errmsg)
 *
 * 		details		parsed RPC response (object, may be null)
 *
 * 		response	contents of response, if request made (string)
 *
 * The arguments to this routine represent the service, method, arguments,
 * expected and actual HTTP response code, the response, and a pointer for the
 * parsed response (for certain types of responses). If NULL, this last pointer
 * is ignored and the response is not parsed.
 */
static int
asr_scrk_process_response(uint16_t expected_resp,
    uint16_t actual_resp, asr_buf_t *bp, nvlist_t *rsp)
{
	char *code, *msg;

	/*
	 * Determine whether we failed.
	 * This can manifest itself in several ways.
	 */
	if (bp == NULL || bp->asrb_length == 0)
		/* Failed to make the request at all. */
		return (asr_set_errno(EASR_SC));
	else if (expected_resp != actual_resp)
		/* RPC returned wrong HTTP status code */
		return (asr_error(EASR_SC, "web rpc failed "
		    "(expected HTTP code %u, got code %u)\n", expected_resp,
		    actual_resp));

	/* ASR service returned wrong/missing status code */
	if (nvlist_lookup_string(rsp, ASR_PROP_REG_CODE, &code) != 0)
		return (asr_error(EASR_SC, "No return code received."));
	else if (strcmp(code, ASR_SCRK_CRS_ERRCODE_BADAUTH) == 0)
		return (asr_set_errno(EASR_SC_AUTH));
	else if (strcmp(code, ASR_SCRK_CRS_OK) == 0) {
		/*
		 * This shouldn't ever happen, since the web service has to
		 * supply us with a client registration ID.
		 */
		if (0 != nvlist_lookup_string(
		    rsp, ASR_PROP_REG_CLIENT_ID, &msg))
			(void) asr_error(EASR_SC,
			    "web rpc response didn't include"
			    "client registration id");
		return (ASR_OK);
	}

	if (nvlist_lookup_string(rsp, ASR_PROP_REG_MESSAGE, &msg) != 0)
		msg = "unknown";
	return (asr_error(EASR_SC, "Error from server: %s", msg));
}

/*
 * Creates the service tag instance for the host.  The XML is appended
 * to the supplied out buffer.
 */
static int
asr_scrk_stag_create(char *urn, char *agent,
    asr_handle_t *ah, const asr_regreq_t *regreq, nvlist_t *reg,
    asr_buf_t *out)
{
	char *product_urn = asr_get_productid(ah);
	char *domain_id = NULL, *domain_name = NULL;
	(void) nvlist_lookup_string(
	    reg, ASR_PROP_REG_DOMAIN_ID, &domain_id);
	(void) nvlist_lookup_string(
	    reg, ASR_PROP_REG_DOMAIN_NAME, &domain_name);

	return (asr_stag_create(urn, agent, product_urn,
	    asr_regreq_get_user(regreq), domain_id, domain_name, out));
}


/*
 * Sets the service tag user domain id and name values in the
 * registration response nvlist.
 * Returns non-zero if there is an error.
 */
static int
asr_scrk_stag_set_domain(
    asr_handle_t *ah, const asr_regreq_t *regreq, nvlist_t *reg)
{
	int err;
	nvpair_t *nvp = NULL;
	nvlist_t *domains;

	if (nvlist_alloc(&domains, NV_UNIQUE_NAME, 0) != 0)
		return (ASR_FAILURE);

	if (asr_stag_get_domains(ah, reg, domains) == 0)
		nvp = nvlist_next_nvpair(domains, NULL);

	if (nvp == NULL) {
		char *user = asr_regreq_get_user(regreq);
		err = asr_nvl_add_strf(reg,
		    ASR_PROP_REG_DOMAIN_NAME, "$%s", user);
	} else {
		char *val;
		(void) nvpair_value_string(nvp, &val);
		err = nvlist_add_string(reg, ASR_PROP_REG_DOMAIN_ID,
		    nvpair_name(nvp));
		err |= nvlist_add_string(reg, ASR_PROP_REG_DOMAIN_NAME, val);
	}

	nvlist_free(domains);
	return (err);
}

/*
 * Registers the service tags for the client taking the following steps.
 * 1. Register the service tags agent.
 * 2. Find the default users domain.
 * 3. Register the host to the default user domain.
 */
static int
asr_scrk_stag_reg(asr_handle_t *ah, const asr_regreq_t *regreq, nvlist_t *reg)
{
	int err;
	asr_buf_t *agent = NULL, *stag = NULL;
	char *serial = asr_get_systemid(ah);
	char *urn = asr_stag_inst_urn(asr_get_assetid(ah));

	if (urn == NULL || (agent = asr_buf_alloc(64)) == NULL ||
	    (stag = asr_buf_alloc(256)) == NULL) {
		err = asr_set_errno(EASR_NOMEM);
		goto finally;
	}

	if ((err = asr_stag_agent_urn(serial, agent)) != 0)
		goto finally;

	if ((err = nvlist_add_string(
	    reg, ASR_PROP_REG_ASSET_ID, agent->asrb_data)) != 0)
		goto finally;

	if ((err = asr_stag_create_agent(serial, agent->asrb_data, stag)) != 0)
		goto finally;

	asr_log_debug(ah, "Service Tag Agent\n%s", stag->asrb_data);

	if ((err = asr_stag_register(
	    ah, reg, ASR_STAG_REG_TYPE_AGENT, agent->asrb_data, stag)) != 0 ||
	    (err = asr_stag_check_reg(
	    ah, reg, ASR_STAG_REG_TYPE_AGENT, agent->asrb_data)) != 0)
		goto finally;

	if ((err = asr_scrk_stag_set_domain(ah, regreq, reg)) != 0)
		goto finally;

	asr_buf_reset(stag);
	if ((err = asr_scrk_stag_create(
	    urn, agent->asrb_data, ah, regreq, reg, stag)) != 0)
		goto finally;

	asr_log_debug(ah, "Service Tag Host\n%s", stag->asrb_data);

	/* Service Tags no longer required so we just do best effort. */
	(void) asr_stag_register(ah, reg, ASR_STAG_REG_TYPE_SVCTAG, urn, stag);

finally:
	if (urn != NULL)
		free(urn);
	asr_buf_free(stag);
	asr_buf_free(agent);
	return (err);
}

static int
asr_scrk_fill_post(nvlist_t *post,
    char *user, char *passwd, char *id, char *key)
{
	if (nvlist_add_string(
	    post, ASR_SCRK_MSG_VERSION, ASR_SCRK_VERSION) != 0)
		return (ASR_FAILURE);
	if (nvlist_add_string(post, ASR_SCRK_MSG_SOA_ID, user) != 0)
		return (ASR_FAILURE);
	if (nvlist_add_string(post, ASR_SCRK_MSG_SOA_PW, passwd) != 0)
		return (ASR_FAILURE);
	if (nvlist_add_string(post, ASR_SCRK_MSG_ASSET_ID, id) != 0)
		return (ASR_FAILURE);
	if (nvlist_add_string(post, ASR_SCRK_MSG_PUBLIC_KEY, key) != 0)
		return (ASR_FAILURE);
	return (ASR_OK);
}

static char *
asr_scrk_get_desturl(asr_handle_t *ah)
{
	boolean_t beta = B_FALSE;
	char *url;
	char *bval = asr_getprop_str(ah, ASR_PROP_BETA);
	if (bval != NULL && strcmp(bval, ASR_VALUE_TRUE) == 0)
		beta = B_TRUE;
	if (beta) {
		url = asr_getprop_strd(ah, ASR_PROP_SCRK_BETA_URL,
		    ASR_SCRK_BETA_URL);
	} else {
		url = asr_getprop_strd(ah, ASR_PROP_SCRK_URL, ASR_SCRK_URL);
	}

	return (url);
}

static char *
asr_scrk_get_reg_path(asr_handle_t *ah)
{
	asr_buf_t *path;
	boolean_t beta = B_FALSE;
	char *url;
	char *bval = asr_getprop_str(ah, ASR_PROP_BETA);

	if (bval != NULL && strcmp(bval, ASR_VALUE_TRUE) == 0)
		beta = B_TRUE;

	if (beta) {
		url = asr_getprop_strd(ah, ASR_STAG_BETA_URL_PROP,
		    ASR_STAG_DEFAULT_BETA_URL);
	} else {
		url = asr_getprop_strd(ah, ASR_STAG_URL_PROP,
		    ASR_STAG_DEFAULT_URL);
	}

	if ((path = asr_buf_alloc(128)) == NULL)
		return (NULL);
	if (asr_buf_append(path, "%s%s", url, ASR_SCRK_REG_PATH) != 0) {
		asr_buf_free(path);
		return (NULL);
	}
	return (asr_buf_free_struct(path));
}

static char *
asr_scrk_get_msg_path(asr_handle_t *ah)
{
	asr_buf_t *path;
	char *url =  asr_scrk_get_desturl(ah);

	if ((path = asr_buf_alloc(128)) == NULL)
		return (NULL);
	if (asr_buf_append(path, "%s%s", url, ASR_SCRK_MSG_PATH) != 0) {
		asr_buf_free(path);
		return (NULL);
	}
	return (asr_buf_free_struct(path));
}

/*
 * Performs SCRK client registration given Sun/Oracle Online Account user name
 * and password. Creates an RSA public/private key pair registers it
 * (via the SCRK web service) for the given account and the host's asset id.
 * Returns registration properties in the registration nvlist
 * and error status.  Any non-zero value is an error.
 */
int
asr_scrk_register_client(
    asr_handle_t *ah, const asr_regreq_t *regreq, nvlist_t *reg)
{
	int err = 0;
	long resp = 0;
	asr_curl_req_t creq;
	RSA *message_key;
	char *pemkey = NULL;
	char *privkey = NULL;
	nvlist_t *post = NULL;
	asr_buf_t *bp = NULL;

	char *system_id = asr_get_systemid(ah);
	char *url = asr_scrk_get_reg_path(ah);

	if ((message_key =
	    asr_ssl_rsa_keygen(asr_get_keylen(ah))) == NULL ||
	    (pemkey = asr_ssl_rsa_public_pem(message_key)) == NULL ||
	    (privkey = asr_ssl_rsa_private_pem(message_key)) == NULL ||
	    nvlist_alloc(&post, NV_UNIQUE_NAME, 0) != 0) {
		err = ASR_FAILURE;
		goto finally;
	}

	if (asr_scrk_fill_post(post,
	    asr_regreq_get_user(regreq), asr_regreq_get_password(regreq),
	    system_id, pemkey) != 0) {
		err = asr_error(EASR_SC, "failed to build post form");
		goto finally;
	}

	if ((bp = asr_buf_alloc(128)) == NULL) {
		err = ASR_FAILURE;
		goto finally;
	}

	asr_curl_init_request(&creq, ah, url, regreq);
	if ((err = asr_curl_post_multi(
	    &creq, NULL, post, bp, &resp, NULL)) != 0)
		goto finally;

	if ((err = asr_scrk_parse_response(bp, reg)) != 0)
		goto finally;
	if ((err = asr_scrk_process_response(
	    ASR_SCRK_HTTP_STATUS_OK, resp, bp, reg)) != 0)
		goto finally;

	/*
	 * Fill all the registration properties.
	 */
	if ((err = asr_reg_fill(ah, regreq, reg)) != 0)
		goto finally;
	if ((err = nvlist_add_string(reg, ASR_PROP_REG_URL,
	    asr_scrk_get_desturl(ah))) != 0)
		goto finally;
	if ((err = nvlist_add_string(reg, ASR_PROP_REG_MSG_KEY, privkey)) != 0)
		goto finally;
	if ((err = nvlist_add_string(reg, ASR_PROP_REG_PUB_KEY, pemkey)) != 0)
		goto finally;

	err = asr_scrk_stag_reg(ah, regreq, reg);

finally:
	asr_buf_free(bp);
	if (url != NULL)
		free(url);
	if (post != NULL)
		nvlist_free(post);
	if (privkey != NULL)
		free(privkey);
	if (pemkey != NULL)
		free(pemkey);
	if (message_key != NULL)
		RSA_free(message_key);

	return (err);
}

/*
 * Sends an unregister command if the transport supportes it.
 */
/* ARGSUSED */
int
asr_scrk_unregister_client(asr_handle_t *ah)
{
	return (0);
}

/*
 * Sends the given telemetry data.  If the message isn't already signed
 * then the signature will be created.
 */
int
asr_scrk_send_msg(asr_handle_t *ah, const asr_message_t *msg,
    nvlist_t *reg)
{
	asr_curl_req_t creq;
	char *url = NULL;
	asr_buf_t *rspbuf = NULL;
	char *sig64buf = NULL;
	char *sig64;
	nvlist_t *post;
	long resp = 0;
	int err = 0;

	char *sig = NULL;
	unsigned int siglen;

	if ((sig = asr_ssl_sign(
	    asr_getprop_str(ah, ASR_PROP_REG_MSG_KEY),
	    (unsigned char *)msg->asr_msg_data,
	    strlen(msg->asr_msg_data), &siglen)) == NULL ||
	    nvlist_alloc(&post, NV_UNIQUE_NAME, 0) != 0 ||
	    (rspbuf = asr_buf_alloc(128)) == NULL) {
		err = ASR_FAILURE;
		goto out;
	}

	if ((sig64buf = asr_b64_encode(sig, siglen)) == NULL) {
		free(sig);
		err = ASR_FAILURE;
		goto out;
	}
	free(sig);
	sig64 = sig64buf;

	url = asr_scrk_get_msg_path(ah);

	err |= nvlist_add_string(post, ASR_SCRK_MSG_VERSION, ASR_SCRK_VERSION);
	err |= nvlist_add_string(post,
	    ASR_SCRK_MSG_OFFERING_CLASS, ASR_SCRK_OFFERING_CLASS);
	err |= nvlist_add_string(post,
	    ASR_SCRK_MSG_DESCRIPTION, ASR_SCRK_DESCRIPTION);
	err |= nvlist_add_string(post,
	    ASR_SCRK_MSG_PRODUCT, asr_get_assetid(ah));
	err |= nvlist_add_string(post,
	    ASR_SCRK_MSG_CLIENT_REG, asr_get_regid(ah));
	err |= nvlist_add_string(
	    post, ASR_SCRK_MSG_SIGNATURE, sig64);
	err |= nvlist_add_string(
	    post, ASR_SCRK_MSG_DATA, msg->asr_msg_data);

	if (err != 0) {
		err = asr_error(EASR_SC, "error creating POST fields");
		goto out;
	}

	asr_curl_init_request(&creq, ah, url, NULL);
	err = asr_curl_post_multi(&creq, NULL, post, rspbuf, &resp, NULL);
	if (err == 0)
		err = asr_scrk_parse_response(rspbuf, reg);
	if (resp != ASR_SCRK_HTTP_STATUS_OK) {
		(void) asr_error(EASR_SC_CONN,
		    "Phone Home connection error (%d)", resp);
	}

out:
	if (url != NULL)
		free(url);
	asr_buf_free(rspbuf);
	if (post != NULL)
		nvlist_free(post);

	if (sig64buf != NULL) {
		free(sig64buf);
	}
	return (err);
}
