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

/*
 * This transport follows the DTS 2.7 Client specification
 * See PSARC 2011/109 for more details
 */
#include <strings.h>
#include <stdarg.h>

#include <fm/libasr.h>

#include "asr_curl.h"
#include "asr_base64.h"
#include "asr_dts.h"
#include "asr_ssl.h"
#include "asr_stag.h"
#include "asr_buf.h"

/*
 * Gets the configured destination URL to be used for ASR telemetry.
 * This URL is used for initial registration.
 */
static char *
asr_dts_get_desturl(asr_handle_t *ah)
{
	return (asr_getprop_strd(ah, ASR_PROP_DEST_URL, ASR_DEFAULT_DTS_URL));
}

/*
 * Gets the URL that was saved during the last successful registration
 * If that isn't availble return the URL that should be used for registration
 */
static char *
asr_dts_get_regurl(asr_handle_t *ah)
{
	char *url = asr_getprop_str(ah, ASR_PROP_REG_URL);
	if (url == NULL)
		url = asr_dts_get_desturl(ah);
	return (url);
}

/*
 * Gets the DTS virtual path.  e.g. /v1 or /v2
 */
static char *
asr_dts_get_vpath(asr_handle_t *ah)
{
	return (asr_getprop_strd(
	    ah, ASR_DTS_PROP_VIRTUAL_PATH, ASR_DTS_VIRTUAL_PATH));
}

/*
 * Adds service tags headers to the DTS registration request.
 * DTS requires the service tag XML as part of the registration request.
 */
static int
asr_dts_add_stag(asr_handle_t *ah, const asr_regreq_t *regreq,
    nvlist_t *post, nvlist_t *reg)
{
	int err = ASR_OK;
	char *user = asr_regreq_get_user(regreq);
	char *agent_id = asr_get_assetid(ah);
	char *system_id = asr_get_systemid(ah);
	char *product_urn = asr_get_productid(ah);
	char *domain_name = NULL;
	asr_buf_t *agent = NULL, *stag = NULL;

	if ((agent = asr_buf_alloc(256)) == NULL ||
	    (stag = asr_buf_alloc(256)) == NULL) {
		err = ASR_FAILURE;
		goto finally;
	}

	if (nvlist_add_string(reg, ASR_PROP_REG_ASSET_ID, agent_id) != 0 ||
	    nvlist_add_string(reg, ASR_PROP_REG_SYSTEM_ID, system_id) != 0 ||
	    asr_nvl_add_strf(reg, ASR_PROP_REG_DOMAIN_NAME, "$%s", user) != 0 ||
	    nvlist_lookup_string(
	    reg, ASR_PROP_REG_DOMAIN_NAME, &domain_name) != 0) {
		err = asr_set_errno(EASR_NVLIST);
		goto finally;
	}

	if ((err = asr_stag_create_agent(system_id, agent_id, agent)) != 0)
		goto finally;

	if ((err = asr_stag_create(agent_id, agent_id, product_urn,
	    user, NULL, domain_name, stag)) != 0)
		goto finally;

	if (nvlist_add_string(post, DTS_STAG_URN_PARAMETER, agent_id) != 0 ||
	    nvlist_add_string(
	    post, DTS_STAG_XML_PARAMETER, asr_buf_data(stag)) != 0 ||
	    nvlist_add_string(
	    post, DTS_STAG_AGENT_XML_PARAMETER, asr_buf_data(agent)) != 0 ||
	    nvlist_add_string(
	    post, DTS_STAG_AGENT_URN_PARAMETER, agent_id) != 0)
		err = asr_set_errno(EASR_NVLIST);

finally:
	asr_buf_free(agent);
	asr_buf_free(stag);
	return (err != 0 ? ASR_FAILURE : ASR_OK);
}

/*
 * Adds the date in the format "Date: Tue, 22 Jul 2008 12:05:00 GMT"
 */
static int
asr_dts_add_date(char *date, size_t dlen, struct curl_slist **hdrs)
{
	time_t now;
	struct tm *gmnow;

	(void) time(&now);
	gmnow = gmtime(&now);
	(void) strftime(date, dlen, "%a, %d %b %Y %T GMT", gmnow);

	if (asr_curl_hdr_append(hdrs, "Date: %s", date) != 0)
		return (ASR_FAILURE);
	return (ASR_OK);
}

/*
 * Calculates client signiture used for authentication and
 * adds an Authorization header with value:
 * Sun-DTS-signed (client-registration-id) (signature)
 */
static int
asr_dts_add_client_sig(asr_handle_t *ah, struct curl_slist **hdrs)
{
	int result = ASR_OK;
	char *sig64;
	char *key = asr_getprop_str(ah, ASR_PROP_REG_MSG_KEY);
	char *client = asr_get_regid(ah);

	if (client == NULL || key == NULL) {
		(void) asr_error(EASR_SC_REG, "Failed to sign client id");
		return (ASR_FAILURE);
	}

	if ((sig64 = asr_ssl_sign64(key, client, strlen(client))) == NULL)
		return (ASR_FAILURE);

	if (asr_curl_hdr_append(hdrs, "Authorization: Sun-DTS-signed %s\n\t%s",
	    client, sig64) != 0)
		result = ASR_FAILURE;

	free(sig64);
	return (result);
}

/*
 * Calculates client signiture used for authentication.
 *
 * DTS requests use an Authorization header with value:
 * Sun-DTS-signed (client-registration-id) (signature)
 * The content used to generate the signature is:
 * (requestMethod)|(URL-path)|(URL-query)|(Date-header)(payload)
 * A maximum of 100,000 bytes of the payload are used.
 * Example: POST|/v1/queue/test|null|Tue, 22 Jul 2008 12:05:00 GMTHello world
 * For GET, HEAD and DELETE requests there is no payload,
 * so only the other items are used.
 */
static int
asr_dts_add_signature(asr_handle_t *ah, char *req, char *path, char *query,
    char *content, int len, struct curl_slist **hdrs)
{
	char *hdrname = "Authorization: Sun-DTS-signed";
	char date[64];
	asr_buf_t *hdr = NULL;
	asr_buf_t *sigh = NULL;
	char *sig64 = NULL;
	char *key = asr_getprop_str(ah, ASR_PROP_REG_MSG_KEY);
	char *client = asr_get_regid(ah);
	unsigned int siglen = 0;
	int err = ASR_OK;

	if (asr_dts_add_date(date, sizeof (date), hdrs) != 0)
		return (ASR_FAILURE);

	if (path == NULL)
		path = "null";
	if (query == NULL)
		query = "null";
	if ((hdr = asr_buf_alloc(64)) == NULL)
		return (ASR_FAILURE);
	if (asr_buf_append(hdr, "%s|%s|%s|%s", req, path, query, date) != 0) {
		err = ASR_FAILURE;
		goto finally;
	}

	if (content == NULL || len == 0) {
		if ((sig64 = asr_ssl_sign64(
		    key, hdr->asrb_data, hdr->asrb_length)) == NULL) {
			err = ASR_FAILURE;
			goto finally;
		}
	} else {
		char *sig = NULL;
		int slen = (len > ASR_DTS_MAX_SIGN) ? ASR_DTS_MAX_SIGN : len;
		if ((sig = asr_ssl_sign_pre(
		    key, (unsigned char *)hdr->asrb_data, hdr->asrb_length,
		    (unsigned char *)content, slen, &siglen)) == NULL) {
			err = ASR_FAILURE;
			goto finally;
		}

		if ((sig64 = asr_b64_encode(sig, siglen)) == NULL) {
			if (sig != NULL)
				free(sig);
			err = ASR_FAILURE;
			goto finally;
		}
		if (sig != NULL)
			free(sig);
	}
	if ((sigh = asr_buf_alloc(
	    strlen(hdrname) + 4 + strlen(client) + siglen)) == NULL) {
		err = ASR_FAILURE;
		goto finally;
	}

	if (asr_buf_append(sigh, "%s %s\n\t%s", hdrname, client, sig64) != 0)
		err = ASR_FAILURE;
	else
		err = asr_curl_hdr_add(hdrs, sigh->asrb_data);

finally:
	if (sig64 != NULL)
		free(sig64);
	asr_buf_free(sigh);
	asr_buf_free(hdr);
	return (err);
}

/*
 * Adds the agent dts header.
 */
static int
asr_dts_add_agent(struct curl_slist **hdrs)
{
	if (asr_curl_hdr_append(hdrs, "User-Agent: %s", ASR_DTS_APP_NAME) != 0)
		return (ASR_FAILURE);
	return (ASR_OK);
}

/*
 * Registers a DTS client.
 *
 * Web API
 * ------------
 * POST https://transport.sun.com/v1/registry
 * Each client must register before it may interact with DTS.
 *
 * This is a Content-Type: application/x-www-form-urlencoded
 * request that includes an Authorization header with this value:
 *	Sun-SOA-auth (username) (password)
 *
 * The username/password are for a valid Sun Online Account.
 * The request body is the encoded form data including the following fields:
 *
 * X-Sun-pub-key
 *	Public key that will be used in signature verification
 *	[1.1] 1.0 had a parameter called X-Sun-asset-id which is no longer used
 *	[1.1] The following four fields are now required; service tag data for
 *	      the product that is registering with DTS:
 * X-Sun-service-tag-urn
 *	instance_urn of service tag in the local registry
 * X-Sun-service-tag-xml
 *	xml with full service tag details; the following fields may be given
 *	as empty <tag/> and the server will fill in values:
 *	    sun_user_id, group_id, registration_client_urn
 * X-Sun-service-tag-agent-urn
 *	urn of local agent
 * X-Sun-service-tag-agent-xml
 *	full xml of local agent
 *
 * Response is 200 OK on success and includes a X-Sun-client-reg-id header
 * with the client registration id value to be used in future requests.
 */
int
asr_dts_register_client(asr_handle_t *ah,
			const asr_regreq_t *regreq,
			nvlist_t *reg)
{
	int result = ASR_OK;
	RSA *message_key;
	char *pemkey = NULL;
	char *privkey = NULL;
	nvlist_t *post = NULL, *hdr = NULL, *out_hdr = NULL;
	asr_buf_t *bp = NULL;
	long resp;
	char url[256];
	char *clreg_id;
	char *dest_url = asr_dts_get_desturl(ah);
	asr_curl_req_t creq;

	if ((message_key =
	    asr_ssl_rsa_keygen(asr_get_keylen(ah))) == NULL ||
	    (pemkey = asr_ssl_rsa_public_pem(message_key)) == NULL ||
	    (privkey = asr_ssl_rsa_private_pem(message_key)) == NULL ||
	    (bp = asr_buf_alloc(128)) == NULL ||
	    nvlist_alloc(&post, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_alloc(&hdr, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_alloc(&out_hdr, NV_UNIQUE_NAME, 0) != 0) {
		result = ASR_FAILURE;
		goto finally;
	}

	if (asr_nvl_add_strf(hdr, "Authorization", "%s %s %s",
	    DTS_HEADER_AUTH, regreq->asr_user, regreq->asr_password) != 0 ||
	    nvlist_add_string(hdr, "Connection", "Keep-Alive") != 0 ||
	    nvlist_add_string(post, DTS_PUBLIC_KEY_PARAMETER, pemkey) != 0) {
		result = ASR_FAILURE;
		(void) asr_set_errno(EASR_NVLIST);
		goto finally;
	}

	if (asr_reg_fill(ah, regreq, reg) != 0 ||
	    nvlist_add_string(reg, ASR_PROP_REG_URL, dest_url) != 0 ||
	    asr_dts_add_stag(ah, regreq, post, reg) != 0) {
		result = ASR_FAILURE;
		goto finally;
	}

	(void) snprintf(url, sizeof (url), "%s%s/%s",
	    dest_url, asr_dts_get_vpath(ah), DTS_REGISTRY);

	asr_curl_init_request(&creq, ah, url, regreq);
	if ((result = asr_curl_post_form(
	    &creq, hdr, post, bp, &resp, out_hdr)) != ASR_OK)
		goto finally;

	if (nvlist_add_string(reg, ASR_PROP_REG_MESSAGE, bp->asrb_data) != 0) {
		(void) asr_set_errno(EASR_NVLIST);
		result = ASR_FAILURE;
		goto finally;
	}

	if (resp == 200) {
		if (nvlist_add_string(reg, ASR_PROP_REG_CODE, "0") != 0) {
			(void) asr_set_errno(EASR_NVLIST);
			result = ASR_FAILURE;
			goto finally;
		}
	} else if (resp == 401) {
		(void) asr_set_errno(EASR_SC_AUTH);
		(void) asr_nvl_add_strf(reg, ASR_PROP_REG_CODE, "%ld", resp);
		result = ASR_FAILURE;
		goto finally;
	} else {
		(void) asr_error(EASR_SC,
		    "Error %ld from phone home server.", resp);
		(void) asr_nvl_add_strf(reg, ASR_PROP_REG_CODE, "%ld", resp);
		result = ASR_FAILURE;
		goto finally;
	}

	if (nvlist_lookup_string(
	    out_hdr, "X-sun-client-reg-id", &clreg_id) != 0) {
		(void) asr_error(EASR_SC,
		    "No client id returned from phone home server.");
		result = ASR_FAILURE;
		goto finally;
	}

	/*
	 * Fill in the registration properties.
	 */
	if (nvlist_add_string(reg, ASR_PROP_REG_CLIENT_ID, clreg_id) != 0 ||
	    nvlist_add_string(reg, ASR_PROP_REG_MSG_KEY, privkey) != 0 ||
	    nvlist_add_string(reg, ASR_PROP_REG_PUB_KEY, pemkey) != 0 ||
	    nvlist_add_string(reg, ASR_PROP_REG_URL, dest_url) != 0)
		result = ASR_FAILURE;

	if (result == ASR_OK) {
		asr_log_info(ah, "Phone home registred %s at %s",
		    regreq->asr_user, url);
	}

finally:
	asr_buf_free(bp);
	if (out_hdr != NULL)
		nvlist_free(out_hdr);
	if (hdr != NULL)
		nvlist_free(hdr);
	if (post != NULL)
		nvlist_free(post);
	if (privkey != NULL)
		free(privkey);
	if (pemkey != NULL)
		free(pemkey);
	if (message_key != NULL)
		RSA_free(message_key);

	return (result);
}

/*
 * Sends a request to the DTS service.
 */
/*PRINTFLIKE6*/
static long
asr_dts_request(asr_handle_t *ah, char *request, struct curl_slist *hdrs,
    asr_buf_t *out_buf, nvlist_t *out_hdr, char *pformat, ...)
{
	long resp;
	va_list ap;
	asr_buf_t *path = NULL, *url = NULL;
	char *sig = NULL;
	asr_curl_req_t creq;

	if ((path = asr_buf_alloc(64)) == NULL ||
	    (url = asr_buf_alloc(64)) == NULL) {
		resp = ASR_FAILURE;
		goto finally;
	}

	va_start(ap, pformat);
	if (asr_buf_vappend(path, pformat, ap) != 0) {
		resp = ASR_FAILURE;
		va_end(ap);
		goto finally;
	}
	va_end(ap);

	if (asr_buf_append(
	    url, "%s%s", asr_dts_get_regurl(ah), path->asrb_data) != 0) {
		resp = ASR_FAILURE;
		goto finally;
	}
	if (asr_dts_add_agent(&hdrs) != ASR_OK) {
		resp = ASR_FAILURE;
		goto finally;
	}
	if (asr_dts_add_signature(
	    ah, request, path->asrb_data, NULL, NULL, 0, &hdrs) != ASR_OK) {
		resp = ASR_FAILURE;
		goto finally;
	}

	asr_curl_init_request(&creq, ah, url->asrb_data, NULL);
	if (asr_curl_request(&creq, request, hdrs,
	    out_buf, &resp, out_hdr) != 0)
		resp = ASR_FAILURE;

	if (resp != ASR_DTS_RSP_OK)
		(void) asr_error(EASR_SC_CONN, "Phone Home Error: %s (%d)",
		    asr_dts_resp_msg(resp), resp);

finally:
	free(sig);
	asr_buf_free(url);
	asr_buf_free(path);
	if (hdrs != NULL)
		curl_slist_free_all(hdrs);
	return (resp);
}

/*
 * Posts a message to the given path
 */
static long
asr_dts_post(asr_handle_t *ah, char *path,
    struct curl_slist **hdrs, char *data, size_t len, nvlist_t *out_hdr)
{
	asr_buf_t *url = NULL;
	asr_buf_t *out_buf = NULL;
	asr_curl_req_t creq;
	long resp;

	if (len > ASR_DTS_MAX_MSG_SIZE) {
		(void) asr_error(EASR_SC,
		    "Phone Home Error: Message size to large");
		return (ASR_FAILURE);
	}
	if ((url = asr_buf_alloc(256)) == NULL ||
	    (out_buf = asr_buf_alloc(64)) == NULL) {
		resp = ASR_FAILURE;
		goto finally;
	}
	if (asr_buf_append(url, "%s%s", asr_dts_get_regurl(ah), path) != 0) {
		resp = ASR_FAILURE;
		goto finally;
	}
	if (asr_dts_add_agent(hdrs) != ASR_OK) {
		resp = ASR_FAILURE;
		goto finally;
	}
	if (asr_dts_add_signature(
	    ah, "POST", path, NULL, data, len, hdrs) != ASR_OK) {
		resp = ASR_FAILURE;
		goto finally;
	}

	asr_curl_init_request(&creq, ah, asr_buf_data(url), NULL);
	if (asr_curl_post_data(
	    &creq, *hdrs, data, len, out_buf, &resp, out_hdr) != 0)
		goto finally;

	if (out_hdr != NULL) {
		char *msg;
		if (out_buf != NULL && out_buf->asrb_length > 0)
			msg = out_buf->asrb_data;
		else
			(void) nvlist_lookup_string(out_hdr, "HTTP/1.1", &msg);
		(void) nvlist_add_string(out_hdr, ASR_PROP_REG_MESSAGE, msg);
	}
	(void) asr_nvl_add_strf(out_hdr, ASR_PROP_REG_CODE, "%ld", resp);

finally:
	asr_buf_free(out_buf);
	asr_buf_free(url);
	return (resp);
}

/*
 * Unregisters a DTS client.
 *
 * Web Client Unregistration API
 * -----------------------------
 * DELETE https://transport.sun.com/v1/registry
 *
 * The Authorization signature differs from other requests in that the signed
 * content is just the client registration id value itself.
 * Response is 200 OK on success.
 */
int
asr_dts_unregister_client(asr_handle_t *ah)
{
	long resp;
	int result;
	asr_buf_t *url = NULL;
	asr_curl_req_t creq;
	struct curl_slist *hdrs = NULL;
	char *client_id = asr_get_regid(ah);

	if (client_id == NULL) {
		(void) asr_error(EASR_SC_CONN,
		    "Phone Home Error: Not Registered");
		return (ASR_FAILURE);
	}

	if ((url = asr_buf_alloc(128)) == NULL)
		return (ASR_FAILURE);

	if (asr_buf_append(url, "%s%s/registry",
	    asr_dts_get_regurl(ah), asr_dts_get_vpath(ah)) != 0) {
		asr_buf_free(url);
		return (ASR_FAILURE);
	}

	if (asr_dts_add_agent(&hdrs) != ASR_OK) {
		resp = ASR_FAILURE;
		goto finally;
	}
	if (asr_dts_add_client_sig(ah, &hdrs) != ASR_OK) {
		resp = ASR_FAILURE;
		goto finally;
	}

	asr_curl_init_request(&creq, ah, url->asrb_data, NULL);
	if (asr_curl_request(&creq, "DELETE", hdrs,
	    NULL, &resp, NULL) != 0)
		resp = ASR_FAILURE;

	if (resp != ASR_DTS_RSP_OK) {
		(void) asr_error(EASR_SC_CONN, "Phone Home Error: %s",
		    asr_dts_resp_msg(resp));
		result = ASR_FAILURE;
	} else {
		asr_log_info(ah, "Phone Home: Unregistered %s", client_id);
		result = ASR_OK;
	}
finally:
	asr_buf_free(url);
	if (hdrs != NULL)
		curl_slist_free_all(hdrs);
	return (result);
}

/*
 * Adds a Message to a Queue
 *
 * Web API
 * -------
 * POST https://transport.sun.com/v1/queue/(name)
 *
 * Include any required metadata in HTTP headers, message payload in HTTP
 * request body (except as described below).
 * [1.1] Previously X-Sun-geo header was always required; now only required if
 * configured for the particular queue.
 *
 * If Successful:
 * 201 Created 	Location: http://transport.sun.com/v1/queue/(name)/message/(id)
 *
 * If Content-Length request header is set to zero then X-Sun-message-length
 * header must also be included to indicate the expected message size, but NO
 * request body should be sent. The message is created with the given metadata,
 * the same Location header is returned, and the response is 202 Accepted
 * (instead of 201 Created). The message payload can be added by POSTing
 * to the given Location.
 *
 * If Error (on Client Side):
 * 400 Bad Request 	returns description of error (exceptions)
 * 401 Unauthorized 	either no auth header, authentication failed,
 *                      or requested method not allowed for client/queue
 * 404 Not Found 	bad queue name
 * 409 Conflict 	received data size doesn't match given content-length
 * 411 Length Required 	missing content-length header (meaning payload length)
 * 413 Request Entity Too Large content-length exceeds limit for this queue
 *
 * If Error (on Server Side):
 * 500 Internal Server Error 	returns description of error (exceptions)
 * 503 Service Unavailable 	server is under maintenance or overloaded
 */
long
asr_dts_add_msg(asr_handle_t *ah, nvlist_t *in_hdrs,
    char *name, char *data, size_t len, nvlist_t **out_hdrs)
{
	asr_buf_t *path = NULL;
	struct curl_slist *hdrs = NULL;
	long resp = ASR_FAILURE;

	*out_hdrs = NULL;

	if (in_hdrs != NULL && (hdrs = asr_curl_headers(in_hdrs)) == NULL)
		goto finally;

	if ((path = asr_buf_alloc(128)) == NULL)
		goto finally;

	if (asr_curl_hdr_append(&hdrs, "Content-Type: text/xml") != 0)
		goto finally;

	if (asr_curl_hdr_append(&hdrs,
	    "%s: %lu", DTS_HEADER_MESSAGE_LENGTH, (unsigned long)len) != 0)
		goto finally;
	if (asr_buf_append(
	    path, "%s/queue/%s", asr_dts_get_vpath(ah), name) != 0)
		goto finally;

	if (nvlist_alloc(out_hdrs, NV_UNIQUE_NAME, 0) != 0)
		goto finally;

	resp = asr_dts_post(ah, path->asrb_data, &hdrs, data, len, *out_hdrs);

finally:
	if (hdrs != NULL)
		curl_slist_free_all(hdrs);
	asr_buf_free(path);

	return (resp);
}

/*
 * Gets the queue name used by DTS to route messages on the back end.
 * Anonymous uses has separate queues since they bypass ASR processing.
 */
static char *
asr_dts_get_queue_name(asr_handle_t *ah, const asr_message_t *msg)
{
	char *queue;

	switch (msg->asr_msg_type) {
	case ASR_MSG_ACTIVATE:
	case ASR_MSG_DEACTIVATE:
		queue = asr_getprop_strd(ah,
		    ASR_DTS_PROP_ACTIVATION_QUEUE, ASR_DTS_ACTIVATION_QUEUE);
		break;
	case ASR_MSG_AUDIT:
		queue = asr_getprop_strd(ah,
		    ASR_DTS_PROP_AUDIT_QUEUE, ASR_DTS_AUDIT_QUEUE);
		break;
	case ASR_MSG_HEARTBEAT:
		queue = asr_getprop_strd(ah,
		    ASR_DTS_PROP_HEARTBEAT_QUEUE, ASR_DTS_HEARTBEAT_QUEUE);
		break;
	case ASR_MSG_FAULT:
	case ASR_MSG_EVENT_UPDATE:
	case ASR_MSG_TEST:
		queue = asr_getprop_strd(ah,
		    ASR_DTS_PROP_EVENT_QUEUE, ASR_DTS_EVENT_QUEUE);
		break;
	case ASR_MSG_STATE_CHANGE:
		queue = asr_getprop_strd(ah,
		    ASR_DTS_PROP_ADMIN_QUEUE, ASR_DTS_ADMIN_QUEUE);
		break;
	default:
		queue = asr_getprop_strd(ah, ASR_DTS_PROP_QUEUE, ASR_DTS_QUEUE);
	}
	return (queue);
}

/*
 * Sets a CURL HTTP formatted header with the given name and value.
 */
static int
asr_dts_set_hdr(struct curl_slist **hdrs, char *name, char *value)
{
	return (asr_curl_hdr_append(hdrs, "%s: %s", name, value));
}

/*
 * Sets the DTS HTTP Event Type Header
 */
static int
asr_dts_set_event_type_hdr(struct curl_slist **hdrs, int type)
{
	int err = 0;
	switch (type) {
	case ASR_MSG_ACTIVATE:
		err = asr_dts_set_hdr(hdrs, DTS_EVENT_TYPE, "activate");
		break;
	case ASR_MSG_AUDIT:
		err = asr_dts_set_hdr(hdrs, DTS_EVENT_TYPE, "audit");
		break;
	case ASR_MSG_DEACTIVATE:
		err = asr_dts_set_hdr(hdrs, DTS_EVENT_TYPE, "deactivate");
		break;
	case ASR_MSG_HEARTBEAT:
		err = asr_dts_set_hdr(hdrs, DTS_EVENT_TYPE, "heartbeat");
		break;
	case ASR_MSG_EVENT_UPDATE:
		/* same as fault */
		err = asr_dts_set_hdr(hdrs, DTS_EVENT_TYPE, "fault");
		break;
	case ASR_MSG_FAULT:
		err = asr_dts_set_hdr(hdrs, DTS_EVENT_TYPE, "fault");
		break;
	case ASR_MSG_STATE_CHANGE:
		err = asr_dts_set_hdr(hdrs, DTS_EVENT_TYPE, "admin");
		break;
	case ASR_MSG_STATUS:
		err = asr_dts_set_hdr(hdrs, DTS_EVENT_TYPE, "status");
		break;
	case ASR_MSG_TEST:
		err = asr_dts_set_hdr(hdrs, DTS_EVENT_TYPE, "test");
		break;
	default:
		/* Don't set an event type header */
		break;
	}
	return (err);
}

/*
 * Sets the DTS HTTP Product Header
 */
static int
asr_dts_set_product_hdr(asr_handle_t *ah, struct curl_slist **hdrs, int type)
{
	asr_buf_t *product = asr_buf_alloc(80);
	int err = 0;

	if (product == NULL)
		return (ASR_FAILURE);
	if (asr_buf_append_str(product, DTS_PRODUCT) != 0 ||
	    asr_buf_append_str(product, ": ") != 0) {
		asr_buf_free(product);
		return (ASR_FAILURE);
	}
	if (type == ASR_MSG_AUDIT)
		err = asr_buf_append_str(product, ASR_STAG_PRODUCT_ID);
	else
		err = asr_buf_append_xml_token(product, asr_get_productid(ah));
	if (err == 0)
		err = asr_curl_hdr_add(hdrs, asr_buf_data(product));
	asr_buf_free(product);
	return (err);
}

/*
 * Sends a message created by the asr message library to the DTS ASR
 * service.
 */
int
asr_dts_send_msg(asr_handle_t *ah, const asr_message_t *msg, nvlist_t *rsp)
{
	struct curl_slist *hdrs = NULL;
	asr_buf_t *path = NULL;
	long resp;
	int err = ASR_OK;

	if ((path = asr_buf_alloc(128)) == NULL) {
		return (ASR_FAILURE);
	}
	if (asr_buf_append(path, "%s/queue/%s",
	    asr_dts_get_vpath(ah), asr_dts_get_queue_name(ah, msg)) != 0) {
		asr_buf_free(path);
		return (ASR_FAILURE);
	}
	if (asr_curl_hdr_add(&hdrs, "Content-Type: text/xml") != 0 ||
	    asr_dts_set_hdr(&hdrs, DTS_TELEMETRY_SOURCE, "FMA") != 0 ||
	    asr_dts_set_event_type_hdr(&hdrs, msg->asr_msg_type) != 0 ||
	    asr_dts_set_product_hdr(ah, &hdrs, msg->asr_msg_type) != 0 ||
	    asr_curl_hdr_append(&hdrs, "%s: %i",
	    DTS_HEADER_MESSAGE_LENGTH, msg->asr_msg_len) != 0) {
		asr_buf_free(path);
		return (ASR_FAILURE);
	}

	resp = asr_dts_post(ah, asr_buf_data(path), &hdrs,
	    msg->asr_msg_data, msg->asr_msg_len, rsp);

	if (resp != ASR_DTS_RSP_CREATED) {
		const char *msg = asr_dts_resp_msg(resp);
		/* DTS transport doesn't want retries with these errors. */
		if (resp >= 400 && resp <= 415) {
			(void) nvlist_add_string(
			    rsp, ASR_MSG_RSP_RETRY, "false");
		}
		(void) nvlist_add_string(rsp, ASR_MSG_RSP_MESSAGE, msg);
		(void) asr_nvl_add_strf(rsp, ASR_MSG_RSP_CODE, "%d", resp);
		(void) asr_error(EASR_SC_CONN, "Phone Home Error: %s", msg);
		err = ASR_FAILURE;
	} else {
		asr_log_info(ah, "Phone Home: %d bytes sent to %s%s ",
		    msg->asr_msg_len,
		    asr_dts_get_regurl(ah), asr_buf_data(path));
	}

	if (hdrs != NULL)
		curl_slist_free_all(hdrs);
	asr_buf_free(path);
	return (err);
}


/*
 * Authenticates and verifies a signature
 *
 * DTS Web API
 * -----------
 * HEAD https://transport.sun.com/v1/registry   [1.2]
 *
 * A HEAD request against the registry simply verifies that it is a properly
 * signed request (thus performing authentication).
 * Response is 200 OK on success with a X-Sun-SOA-username response header
 * indicating the username for the given client-registration-id.
 * Sun-SOA-auth (username) (password)
 *
 * Returns registered name and client id.  Caller is responsible for freeing
 * the name and id if they get set.
 */
long
asr_dts_authenticate(asr_handle_t *ah, char **name, char **id)
{
	long resp;
	nvlist_t *hdrs;
	*name = NULL;
	*id = NULL;

	if (nvlist_alloc(&hdrs, NV_UNIQUE_NAME, 0) != 0)
		return (ASR_FAILURE);

	resp = asr_dts_request(ah, "HEAD", NULL, NULL, hdrs,
	    "%s/registry", asr_dts_get_vpath(ah));

	if (resp == ASR_DTS_RSP_OK) {
		char *xname, *xid;
		(void) nvlist_lookup_string(hdrs, "X-sun-soa-username", &xname);
		(void) nvlist_lookup_string(hdrs, "X-sun-client-cwp-pid", &xid);

		if (xname != NULL)
			if ((*name = strdup(xname)) == NULL) {
				(void) asr_set_errno(EASR_NOMEM);
				resp = ASR_FAILURE;
			}
		else
			*name = NULL;

		if (xid != NULL)
			if ((*id = strdup(xid)) == NULL) {
				(void) asr_set_errno(EASR_NOMEM);
				resp = ASR_FAILURE;
			}
		else
			*id = NULL;
	}

	nvlist_free(hdrs);
	return (resp);
}

/*
 * Creates a Message of a given size on a Queue.
 * A call to asr_dts_add_payload can then be made to populate the
 * message contents.
 *
 * DTS Web API
 * -----------
 * POST https://transport.sun.com/v1/queue/(name)
 *
 * Include any required metadata in HTTP headers, message payload in HTTP
 * request body (except as described below).
 * [1.1] Previously X-Sun-geo header was always required;
 * now only required if configured for the particular queue.
 *
 * If Content-Length request header is set to zero then X-Sun-message-length
 * header must also be included to indicate the expected message size,
 * but NO request body should be sent.
 * The message is created with the given metadata, the same Location header is
 * returned, and the response is 202 Accepted (instead of 201 Created).
 * The message payload can be added by POSTing to the given Location.
 */
/* ARGSUSED */
long
asr_dts_create_msg(asr_handle_t *ah, nvlist_t *in_hdrs,
    char *name, size_t len, nvlist_t **out_hdrs)
{
	struct curl_slist *hdrs = NULL;
	long resp = ASR_FAILURE;
	*out_hdrs = NULL;

	if ((hdrs = asr_curl_headers(in_hdrs)) == NULL)
		return (ASR_FAILURE);

	if (asr_curl_hdr_append(&hdrs, "%s: %lu",
	    DTS_HEADER_MESSAGE_LENGTH, (unsigned long)len) != 0)
		return (ASR_FAILURE);

	if (nvlist_alloc(out_hdrs, NV_UNIQUE_NAME, 0) != 0) {
		curl_slist_free_all(hdrs);
		return (ASR_FAILURE);
	}

	resp = asr_dts_request(ah, "POST", hdrs, NULL, *out_hdrs,
	    "%s/queue/%s", asr_dts_get_vpath(ah), name);

	return (resp);
}

/*
 * Adds payload to incomplete message
 *
 * DTS Web API
 * -----------
 * POST https://transport.sun.com/v1/queue/(name)/message/(id)
 *
 * When Content-Length: 0 is used during message creation (see above) then a
 * POST to the message URL adds the payload.
 * If the transfer is interrupted, this URL can be used again to restart the
 * transfer where it left off.
 * Include X-Sun-resume-from: (index) to specify the restart point.
 * A HEAD request to
 * http://transport.sun.com/v1/queue/(name)/message/(id)/resume
 * will return a X-Sun-resume-from header specifying that index.
 * If Successful returns: 201 Created
 */
/* ARGSUSED */
long
asr_dts_add_payload(asr_handle_t *ah, char *name, char *id,
    int resume_from, char *data, size_t len, nvlist_t **out_hdrs)
{
	struct curl_slist *hdrs = NULL;
	asr_buf_t *path = NULL;
	long resp = ASR_FAILURE;

	*out_hdrs = NULL;
	if (asr_curl_hdr_add(&hdrs, "Content-Type: text/xml") != 0)
		return (ASR_FAILURE);

	if (asr_curl_hdr_append(&hdrs,
	    "%s: %dl", DTS_HEADER_RESUME_FROM, resume_from) != 0)
		return (ASR_FAILURE);

	if ((path = asr_buf_alloc(11 + strlen(name))) == NULL)
		goto finally;

	if (asr_buf_append(path, "%s/queue/%s/message/%s",
	    asr_dts_get_vpath(ah), name, id) != 0)
		goto finally;

	if (nvlist_alloc(out_hdrs, NV_UNIQUE_NAME, 0) != 0)
		goto finally;

	resp = asr_dts_post(ah, path->asrb_data, &hdrs, data, len, *out_hdrs);

finally:
	if (hdrs != NULL)
		curl_slist_free_all(hdrs);
	asr_buf_free(path);
	return (resp);
}

/*
 * Pops a Message from the Queue
 *
 * DTS Web API
 * -----------
 * POST https://transport.sun.com/v1/queue/(name)/pop
 * or
 * POST https://transport.sun.com/v1/queue/(name)/(filter)/pop
 *
 * If (filter) is specified then only messages matching this filter are
 * selected (virtual queue).
 * [1.1] In 1.0 the filter was always against the X-Sun-geo value provided in
 * the message. Starting in DTS 2.0 the filter field is configurable,
 * and a queue may specify that using a filter to pop messages is required.
 *
 * Once the State of a message has been transitioned to "POPPED" it will not
 * be returned for a /pop action unless there is a visibility window timeout.
 *
 * Successful responses:
 * 200 OK	Alters state of next message, then returns metadata in HTTP
 *		headers and payload in response body
 * 204 No Content	If there are no messages in the queue (or virtual queue)
 * -1  Failure	Unable to complete web call.
 */
long
asr_dts_pop(asr_handle_t *ah, char *queue, char *filter,
	    char **out_buf, nvlist_t **out_hdr)
{
	long resp;
	asr_buf_t *buf;
	nvlist_t *hdr;
	char *req = "POST";

	*out_buf = NULL;
	*out_hdr = NULL;

	if ((buf = asr_buf_alloc(256)) == NULL)
		return (ASR_FAILURE);

	if (nvlist_alloc(&hdr, NV_UNIQUE_NAME, 0) != 0) {
		asr_buf_free(buf);
		return (ASR_FAILURE);
	}

	if (filter == NULL)
		resp = asr_dts_request(ah, req, NULL, buf, hdr,
		    "%s/queue/%s/pop", asr_dts_get_vpath(ah), queue);
	else
		resp = asr_dts_request(ah, req, NULL, buf, hdr,
		    "%s/queue/%s/%s/pop", asr_dts_get_vpath(ah), queue, filter);

	*out_buf = asr_buf_free_struct(buf);
	*out_hdr = hdr;
	return (resp);
}

/*
 * Reads a specific message on the Queue
 * Returns ASR_FAILURE if there is an error.  Even if there is an error
 * data can be returned from the web server in the out_buf and out_hdr
 * The out_buf will be the contents of the returned message and the out_hdr
 * will contain the HTTP headers.
 *
 * DTS Web API
 * -----------
 * GET https://transport.sun.com/v1/queue/(name)/message/(id)
 * or
 * GET https://transport.sun.com/v1/queue/(name)/(filter)/message/(id)
 * [1.1] (filter ignored in second form; just a convenience so message/(id)
 * may be appended to a base URL that includes a filter)
 *
 * Returns metadata in HTTP headers and payload in response body.
 * [1.1] If X-Sun-resume-from header is included in the request then metadata
 * is omitted and response body sends payload starting from byte offset given
 * in header (resume download of payload).
 */
long
asr_dts_read(asr_handle_t *ah, char *queue, char *id,
	    char **out_buf, nvlist_t **out_hdr)
{
	long resp;
	asr_buf_t *buf;
	nvlist_t *hdr;

	*out_buf = NULL;
	*out_hdr = NULL;

	if ((buf = asr_buf_alloc(256)) == NULL)
		return (ASR_FAILURE);
	if (nvlist_alloc(&hdr, NV_UNIQUE_NAME, 0) != 0) {
		asr_buf_free(buf);
		return (ASR_FAILURE);
	}

	resp = asr_dts_request(ah, "GET", NULL, buf, hdr,
	    "%s/queue/%s/message/%s", asr_dts_get_vpath(ah), queue, id);

	*out_buf = asr_buf_free_struct(buf);
	*out_hdr = hdr;
	return (resp);
}

/*
 * Polls DTS for a message.
 *
 * DTS Long Polling Web API
 * ------------------------
 * HEAD https://transport.sun.com/v1/queue/(name)/longpoll   [1.4]
 * or
 * HEAD https://transport.sun.com/v1/queue/(name)/(filter)/longpoll   [1.4]
 *
 * Allows a dispatcher to keep an open connection and be notified quickly when
 * a new message arrives in an empty queue.
 *
 * Responses:
 * 204 No Content	If queue is empty and remains empty for 10 minutes
 * 200 OK		Sent immediately if queue is already non-empty,
 *			or as soon as a message arrives if waiting
 */
long
asr_dts_poll(asr_handle_t *ah, char *queue, char *filter)
{
	long resp;
	char *req = "HEAD";

	if (filter == NULL)
		resp = asr_dts_request(ah, req, NULL, NULL, NULL,
		    "%s/queue/%s/longpoll", asr_dts_get_vpath(ah), queue);
	else
		resp = asr_dts_request(
		    ah, req, NULL, NULL, NULL, "%s/queue/%s/%s/longpoll",
		    asr_dts_get_vpath(ah), queue, filter);

	return (resp);
}

/*
 * Deletes a Message from the Queue
 *
 * DTS Web API
 * -----------
 * DELETE https://transport.sun.com/v1/queue/(name)/message/(id)
 * or
 * DELETE https://transport.sun.com/v1/queue/(name)/(filter)/message/(id)
 * (filter ignored in second form; just a convenience so message/(id) may be
 * appended to a base URL that includes a filter)
 *
 * Success returns 200 OK
 */
long
asr_dts_delete_msg(asr_handle_t *ah, char *queue, char *id)
{
	long resp;
	resp = asr_dts_request(
	    ah, "DELETE", NULL, NULL, NULL, "%s/queue/%s/message/%s",
	    asr_dts_get_vpath(ah), queue, id);
	return (resp);
}

/*
 * Get Current Time
 *
 * DTS Web API
 * -----------
 * POST https://transport.sun.com/v1/queue/time/pop   [1.3]
 *
 * DTS 2.3 added this API to retrieve the current time, allowing clients to
 * synchronize their clocks with the DTS server.
 * This is just a fake queue which does not accept messages, but every time a
 * message is popped it returns a message with the current time in a
 * X-Sun-DTS-time header (the body of the response is empty).
 * The date format is: yyyy MMM dd HH:mm:ss z
 */
long
asr_dts_get_time(asr_handle_t *ah)
{
	return (asr_dts_request(ah, "POST", NULL, NULL, NULL,
	    "%s/queue/time/pop", asr_dts_get_vpath(ah)));
}

/*
 * Creates a new DTS Queue
 *
 * DTS Web API
 * -----------
 * Administrator may create queue
 * PUT https://transport.sun.com/v1/queue/(name)
 * 201 Created
 */
long
asr_dts_create_queue(asr_handle_t *ah, char *queue, nvlist_t *props)
{
	return (asr_dts_request(ah, "PUT", asr_curl_headers(props), NULL, NULL,
	    "%s/queue/%s", asr_dts_get_vpath(ah), queue));
}

/*
 * Modifies a DTS Queue
 *
 * DTS Web API
 * -----------
 * Administrator can modify a queue config
 * GET https://transport.sun.com/v1/queue/(name)/config
 * If Successful: 200 OK
 */
long
asr_dts_modify_queue(asr_handle_t *ah, char *queue, nvlist_t *props)
{
	return (asr_dts_request(ah, "GET", asr_curl_headers(props), NULL, NULL,
	    "%s/queue/%s/config", asr_dts_get_vpath(ah), queue));
}

/*
 * Deletes a DTS queue
 *
 * DTS Web AP
 * -----------
 * Administrator may delete queue
 * DELETE https://node1.transport.sun.com/v1/queue/(name)
 * If Successful: 200 OK
 */
long
asr_dts_delete_queue(asr_handle_t *ah, char *queue)
{
	return (asr_dts_request(ah, "DELETE", NULL, NULL, NULL,
	    "%s/queue/%s", asr_dts_get_vpath(ah), queue));
}

/*
 * Converts a DTS response to a text message.
 */
const char *
asr_dts_resp_msg(long resp)
{
	switch (resp) {
	case -1:
		return (asr_errmsg());
	case ASR_DTS_RSP_OK:
		return ("OK");
	case ASR_DTS_RSP_CREATED:
		return ("Created");
	case ASR_DTS_RSP_ACCEPTED:
		return ("Accepted");
	case ASR_DTS_RSP_NO_CONTENT:
		return ("No content available");
	case ASR_DTS_RSP_BAD_REQUEST:
		return ("Bad request");
	case ASR_DTS_RSP_UNAUTHORIZED:
		return ("Unauthorized");
	case ASR_DTS_RSP_NOT_FOUND:
		return ("Not found");
	case ASR_DTS_RSP_CONFLICT:
		return ("Conflist");
	case ASR_DTS_RSP_LENGTH_REQUIRED:
		return ("Missing required length content");
	case ASR_DTS_RSP_REQUEST_ENTITY_TOO_LARGE:
		return ("Request entity too large");
	case ASR_DTS_RSP_INTERNAL_SERVER_ERROR:
		return ("Internal server error");
	case ASR_DTS_RSP_SERVICE_UNAVAILABLE:
		return ("Service unavailable");
	default:
		return ("Unknown");
	}
}

/*
 * Initializes the ASR handle to use the DTS transport.
 */
int
asr_dts_init(asr_handle_t *ah)
{
	return asr_set_transport(ah, ASR_DTS_TRANSPORT,
	    asr_dts_register_client,
	    asr_dts_unregister_client,
	    asr_dts_send_msg);
}
