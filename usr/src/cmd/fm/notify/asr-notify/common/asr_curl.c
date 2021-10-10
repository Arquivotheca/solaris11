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

#include <alloca.h>
#include <sys/sysmacros.h>
#include <strings.h>

#include <fm/libasr.h>

#include "asr_base64.h"
#include "asr_curl.h"
#include "asr_ssl.h"

/*
 * Releases all resources used by a CURL request object.
 */
static void
asr_curl_cleanup(CURL *cp)
{
	if (cp != NULL)
		curl_easy_cleanup(cp);
}

/*
 * Sets the HTTP proxy settings up in the CURL pointer.
 */
static int
asr_curl_set_proxy_opts(CURL *cp, const char *proxy_url, const char *proxy_type)
{
	int err = 0;

	if (proxy_url == NULL)
		return (0);

	if ((err = curl_easy_setopt(cp, CURLOPT_PROXY, proxy_url)) != 0)
		return (err);

	if (proxy_type == NULL ||
	    strcmp(proxy_type, ASR_PROXY_TYPE_HTTP) == 0) {
		err |= curl_easy_setopt(cp, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
		err += curl_easy_setopt(cp, CURLOPT_HTTPPROXYTUNNEL, 1);
	} else if (strcmp(proxy_type, ASR_PROXY_TYPE_SOCKS4) == 0)
		err = curl_easy_setopt(cp, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4);
	else if (strcmp(proxy_type, ASR_PROXY_TYPE_SOCKS5) == 0)
		err = curl_easy_setopt(cp, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5);

	return (err);
}

/*
 * Prints out CURL debug information while stripping passwords.
 */
/* ARGSUSED */
static int
asr_curl_debug_callback(
    CURL *cp, curl_infotype info, char *ptr, size_t size, void *stream)
{
	char *auth, *end;
	char *pw, *id;
	FILE *out = (FILE *)stream;

	if (size == 0)
		return (0);

	switch (info) {
	case CURLINFO_TEXT:
		(void) fprintf(out, "CURL: ");
		break;
	case CURLINFO_HEADER_IN:
		(void) fprintf(out, "CURL IN: ");
		break;
	case CURLINFO_HEADER_OUT:
		auth = strstr(ptr, "Authorization: ");
		end = ptr + size;
		(void) fprintf(out, "CURL OUT: ");
		if (auth != NULL) {
			(void) fwrite(ptr, 1, auth-ptr, out);
			while (auth != end && auth[0] != '\0') {
				auth++;
				if (auth[0] == '\n')
					break;
			}
			(void) fprintf(out, "Authorization: ********");
			(void) fwrite(auth, 1, end-auth, out);
			(void) fflush(out);
			return (0);
		}
		break;
	case CURLINFO_DATA_IN:
		(void) fprintf(out, "CURL DATA IN: %d bytes\n", size);
		break;
	case CURLINFO_DATA_OUT:
		pw = "Content-Disposition: form-data; name=\"SOA_PW\"";
		id = "Content-Disposition: form-data; name=\"ASSET_ID\"";
		auth = strstr(ptr,  pw);
		(void) fprintf(out, "CURL DATA OUT: %d bytes\n", size);
		if (auth != NULL) {
			char *end = ptr + size;
			char *asset = strstr(auth, id);
			(void) fwrite(ptr, 1, auth-ptr, out);
			(void) fwrite(asset, 1, end-asset, out);
			return (0);
		}
		break;
	case CURLINFO_SSL_DATA_OUT:
		return (0);
	case CURLINFO_SSL_DATA_IN:
		return (0);
	}

	(void) fwrite(ptr, 1, size, out);
	if (ptr[size-1] != '\n')
		(void) fputc('\n', out);
	(void) fflush(out);
	return (0);
}

/*
 * Wrapper for curl_easy_init() which sets common options.
 */
static CURL *
asr_curl_newreq_url(asr_handle_t *ah, const char *url)
{
	int err = 0;
	CURL *cp;
	FILE *logfile = NULL;

	if ((cp = curl_easy_init()) == NULL) {
		(void) asr_error(EASR_SC, "failed to init curl easy hdl");
		return (NULL);
	}

	if (asr_get_debug(ah))
		logfile = asr_get_logfile(ah);

	err |= curl_easy_setopt(cp, CURLOPT_NOPROGRESS, 1);
	err |= curl_easy_setopt(cp, CURLOPT_NOSIGNAL, 1);
	err |= curl_easy_setopt(cp, CURLOPT_FOLLOWLOCATION, 1);
	if (asr_get_debug(ah) == B_TRUE) {
		err |= curl_easy_setopt(cp, CURLOPT_VERBOSE, 1);
		err |= curl_easy_setopt(
		    cp, CURLOPT_DEBUGFUNCTION, asr_curl_debug_callback);
		err |= curl_easy_setopt(cp, CURLOPT_DEBUGDATA, stderr);
	}
	if (logfile != NULL)
		err |= curl_easy_setopt(cp, CURLOPT_STDERR, logfile);

	/* Verify certificate 0L=no 1L=yes */
	if (asr_getprop_bool(ah, ASR_PROP_HTTPS_VERIFY, B_TRUE))
		err |= curl_easy_setopt(cp, CURLOPT_SSL_VERIFYPEER, 1L);
	else
		err |= curl_easy_setopt(cp, CURLOPT_SSL_VERIFYPEER, 0L);

	err |= curl_easy_setopt(cp, CURLOPT_SSL_VERIFYHOST, 2L);
	err |= curl_easy_setopt(cp, CURLOPT_CONNECTTIMEOUT_MS,
	    asr_get_http_timeout(ah));
	err |= curl_easy_setopt(cp, CURLOPT_URL, url);

	if (err != 0) {
		(void) asr_error(EASR_SC, "failed to setup request");
		asr_curl_cleanup(cp);
		cp = NULL;
	}

	return (cp);
}

/*
 * Wrapper for curl_easy_init() which sets common options.
 */
static CURL *
asr_curl_newreq(asr_curl_req_t *creq)
{
	CURL *cp = NULL;
	char *url, *proxy = NULL, *type = NULL;

	url = (char *)creq->ac_url;

	if ((cp = asr_curl_newreq_url(creq->ac_asrh, url)) == NULL)
		return (NULL);

	if (asr_create_proxy_url(creq->ac_asrh, &proxy, &type) != 0) {
		(void) asr_error(EASR_SC, "failed to create proxy");
		asr_curl_cleanup(cp);
		return (NULL);
	}
	if (proxy != NULL) {
		if (asr_curl_set_proxy_opts(cp, proxy, type) != 0) {
			(void) asr_error(EASR_SC, "failed to setup proxy");
			asr_curl_cleanup(cp);
			return (NULL);
		}
		free(proxy);
		if (type != NULL)
			free(type);
	}
	return (cp);
}

/*
 * Sets the CURL error state and message.
 */
static int
asr_curl_error(int cerr, char *errbuf)
{
	int err;

	switch (cerr) {
	case CURLE_COULDNT_RESOLVE_PROXY:
		err = EASR_SC_RESOLV_PROXY;
		break;
	case CURLE_COULDNT_RESOLVE_HOST:
		err = EASR_SC_RESOLV_HOST;
		break;
	default:
		err = EASR_SC_CONN;
	}

	(void) asr_set_errno(err);
	return (asr_error(err, "failed to make request: %s (%s)",
	    asr_errmsg(), errbuf));
}

/*
 * libcurl write callback: accumulates data in the passed buffer.
 */
static size_t
asr_curl_write(void *ptr, size_t size, size_t nmemb, void *stream)
{
	asr_buf_t *bp = stream;

	size *= nmemb;

	if (size == 0)
		return (0);

	if (asr_buf_append_raw(bp, ptr, size) != 0)
		return (0);

	return (size);
}

static char *
asr_curl_strnchar(char *str, char c, int max)
{
	int i;
	for (i = 0; i < max && str[i] != '\0'; i++) {
		if (str[i] == c)
			return (&str[i]);
	}
	return (NULL);
}

/*
 * libcurl write callback: accumulates http headers in a nvlist
 */
static size_t
asr_curl_header_write(void *ptr, size_t size, size_t nmemb, void *stream)
{
	nvlist_t *hdrs = stream;
	char *name, *value, *sep, *end;
	int len, i, err;
	char sepch = ':';
	size *= nmemb;
	if (size == 0)
		return (0);

	name = ptr;
	sep = asr_curl_strnchar(name, ':', size);
	if (sep == NULL) {
		if (strncmp("HTTP/", name, 5) == 0) {
			sepch = ' ';
			sep = asr_curl_strnchar(name, ' ', size);
		}
		if (sep == NULL)
			return (size);
	}

	*sep = '\0';
	len = sep - name;
	value = sep + 1;

	/* Trim white space from the beginning */
	for (i = len + 1; i < size; i++) {
		if (name[i] == ' ' || name[i] == '\t')
			value++;
		else
			break;
	}

	/* Trim endline from the header value */
	end = strstr(value, "\r\n");
	if (end != NULL)
		*end = '\0';
	else
		value = "";

	err = nvlist_add_string(hdrs, name, value);
	*sep = sepch;
	if (end != NULL)
		*end = '\r';

	if (err != 0)
		return (0);

	return (size);
}

/*
 * Encodes a single form parameter in URL format in the
 * supplied buffer.
 */
static int
asr_curl_url_encode_nv(asr_buf_t *post, char *name, char *value)
{
	char c;
	char *p;
	int err = asr_buf_append_str(post, name);
	err |= asr_buf_append_char(post, '=');
	for (p = value; *p != '\0'; p++) {
		c = *p;
		if (c == ' ')
			err |= asr_buf_append_char(post, '+');
		else if ((c >= 'a' && c <= 'z') ||
		    (c >= 'A' && c <= 'Z') ||
		    (c >= '-' && c <= '9') ||
		    c == '_')
			err |= asr_buf_append_char(post, c);
		else
			err |= asr_buf_append(post, "%%%2.2X", c);

	}
	return (err);
}

/*
 * Encodes form parameters stored in a nvlist into a URL form into the
 * returned buffer.
 */
static asr_buf_t *
asr_curl_url_encode(nvlist_t *plist)
{
	int err = 0;
	char *name, *value;
	asr_buf_t *post = asr_buf_alloc(1024);
	boolean_t first = B_TRUE;

	if (plist != NULL) {
		nvpair_t *nvp;
		for (nvp = nvlist_next_nvpair(plist, NULL);
		    nvp != NULL && err == 0;
		    nvp = nvlist_next_nvpair(plist, nvp)) {

			data_type_t type = nvpair_type(nvp);
			if (type != DATA_TYPE_STRING)
				continue;
			name = nvpair_name(nvp);
			(void) nvpair_value_string(nvp, &value);
			if (first)
				first = B_FALSE;
			else
				err |= asr_buf_append_char(post, '&');
			err |= asr_curl_url_encode_nv(post, name, value);
		}
	}
	if (err != 0) {
		asr_buf_free(post);
		post = NULL;
	}
	return (post);
}

/*
 * Performs the HTTP operation from the configured CURL handle
 * Data returned from the server will be placed in the asr_buf_t if
 * it isn't NULL, and returned HTTP headers will be placed in the
 * nvlist_t if it isn't NULL.
 */
static int
asr_curl_perform(CURL *cp, long *resp, asr_buf_t *bp, nvlist_t *hdr)
{
	char errbuf[CURL_ERROR_SIZE];

	int cerr = curl_easy_setopt(cp, CURLOPT_ERRORBUFFER, errbuf);
	if (bp != NULL) {
		cerr |= curl_easy_setopt(cp, CURLOPT_WRITEFUNCTION,
		    asr_curl_write);
		cerr |= curl_easy_setopt(cp, CURLOPT_WRITEDATA, bp);
	}
	if (hdr != NULL) {
		cerr |= curl_easy_setopt(cp, CURLOPT_HEADERFUNCTION,
		    asr_curl_header_write);
		cerr |= curl_easy_setopt(cp, CURLOPT_WRITEHEADER, hdr);
	}
	if (cerr != 0)
		return (asr_error(EASR_SC, "failed to finish setup header"));

	if ((cerr = curl_easy_perform(cp)) != 0) {
		if (bp != NULL && bp->asrb_length == 0)
			(void) asr_buf_append_str(bp, errbuf);
		return (asr_curl_error(cerr, errbuf));
	}

	if (curl_easy_getinfo(cp, CURLINFO_RESPONSE_CODE, resp) != 0)
		return (asr_error(EASR_SC, "unable to get response code"));

	/*
	 * We built up this buffer by appending raw data which was not
	 * NULL-terminated. Terminate it now.
	 */
	if (bp != NULL && asr_buf_terminate(bp) != 0)
		return (asr_set_errno(EASR_NOMEM));

	/*
	 * For safety, don't leave the curl object pointing to invalid data.
	 */
	(void) curl_easy_setopt(cp, CURLOPT_ERRORBUFFER, NULL);
	(void) curl_easy_setopt(cp, CURLOPT_WRITEFUNCTION, NULL);
	(void) curl_easy_setopt(cp, CURLOPT_WRITEDATA, NULL);
	(void) curl_easy_setopt(cp, CURLOPT_HEADERFUNCTION, NULL);
	(void) curl_easy_setopt(cp, CURLOPT_WRITEHEADER, NULL);

	/*
	 * Clear out any errors from other subsystems.
	 */
	(void) asr_set_errno(EASR_NONE);

	return (0);
}

void
asr_curl_init_request(asr_curl_req_t *creq,
    asr_handle_t *ah, const char *url, const asr_regreq_t *regreq)
{
	creq->ac_asrh = ah;
	creq->ac_url = url;
	creq->ac_regreq = regreq;
}

/*
 * Adds a header to a curl header list using printf like formatting.
 * Returns 0 if there is no error.
 * If there is an error then the curl_slist is cleaned up and non-zero is
 * returned.
 */
/*PRINTFLIKE2*/
int
asr_curl_hdr_append(struct curl_slist **in_out_hdrs, char *fmt, ...)
{
	struct curl_slist *hdrs = *in_out_hdrs;
	va_list ap;
	asr_buf_t *buf;
	int err = 0;

	if ((buf = asr_buf_alloc(64)) == NULL)
		return (ASR_FAILURE);

	va_start(ap, fmt);
	err = asr_buf_vappend(buf, fmt, ap);
	va_end(ap);

	if (err == 0) {
		hdrs = curl_slist_append(hdrs, asr_buf_data(buf));
		if (hdrs == NULL)
			err = asr_set_errno(EASR_NOMEM);
	} else  {
		curl_slist_free_all(hdrs);
		hdrs = NULL;
		(void) asr_set_errno(EASR_NOMEM);
	}

	asr_buf_free(buf);
	*in_out_hdrs = hdrs;
	return (err != 0 ? ASR_FAILURE : ASR_OK);
}

/*
 * Adds a header value to a curl header list.
 * Returns 0 if there is no error.
 * If there is an error then the curl_slist is cleaned up and non-zero is
 * returned.
 */
int
asr_curl_hdr_add(struct curl_slist **in_out_hdrs, char *header)
{
	struct curl_slist *hdrs = *in_out_hdrs;
	int err = 0;

	hdrs = curl_slist_append(hdrs, header);
	if (hdrs == NULL)
		err = asr_set_errno(EASR_NOMEM);

	*in_out_hdrs = hdrs;
	return (err != 0 ? ASR_FAILURE : ASR_OK);
}

/*
 * Creates curl HTTP headers form a nvlist that contains name value pairs
 */
struct curl_slist *
asr_curl_headers(nvlist_t *hdr)
{
	char *name, *value;
	struct curl_slist *headers = NULL;

	if (hdr != NULL) {
		nvpair_t *nvp;
		asr_buf_t *hb = asr_buf_alloc(80);
		if (hb == NULL) {
			(void) asr_set_errno(EASR_NOMEM);
			return (NULL);
		}
		for (nvp = nvlist_next_nvpair(hdr, NULL);
		    nvp != NULL;
		    nvp = nvlist_next_nvpair(hdr, nvp)) {
			data_type_t type = nvpair_type(nvp);
			if (type != DATA_TYPE_STRING)
				continue;
			name = nvpair_name(nvp);
			(void) nvpair_value_string(nvp, &value);
			if (asr_buf_append(hb, "%s: %s", name, value) != 0) {
				asr_buf_free(hb);
				if (headers != NULL)
					curl_slist_free_all(headers);
				return (NULL);
			}
			headers = curl_slist_append(headers, hb->asrb_data);
			asr_buf_reset(hb);
		}
		asr_buf_free(hb);
	}
	return (headers);
}

/*
 * Does an HTTP GET request to the given URL configured through the optional
 * proxy.  If proxy is NULL then the proxy configured in the asr handle will
 * be used.
 * The function will return 0 on success and non-zero for an error.
 * HTTP body, response and reply headers are stored in the out parameters
 * if they are non-NULL
 */
int
asr_curl_get(asr_curl_req_t *creq, struct curl_slist *hdrs,
    asr_buf_t *out_buf, long *out_resp, nvlist_t *out_hdr)
{
	CURL *cp;
	int err;

	*out_resp = -1;
	cp = asr_curl_newreq(creq);
	if (cp == NULL) {
		return (-1);
	}

	if ((err = curl_easy_setopt(cp, CURLOPT_HTTPHEADER, hdrs)) == 0)
		err = asr_curl_perform(cp, out_resp, out_buf, out_hdr);

	asr_curl_cleanup(cp);
	return (err);
}

/*
 * Does an HTTP request to the given URL.
 * The proxy configured in the asr handle will be used.
 * The function will return 0 on success and non-zero for an error.
 * HTTP body, response and reply headers are stored in the out parameters
 * if they are non-NULL
 */
int
asr_curl_request(asr_curl_req_t *creq,
    const char *request, struct curl_slist *hdrs,
    asr_buf_t *out_buf, long *out_resp, nvlist_t *out_hdr)
{
	CURL *cp;
	int err;
	*out_resp = -1;

	if ((cp = asr_curl_newreq(creq)) == NULL)
		return (-1);

	err = curl_easy_setopt(cp, CURLOPT_CUSTOMREQUEST, request);
	err |= curl_easy_setopt(cp, CURLOPT_HTTPHEADER, hdrs);

	if (strcmp("POST", request) == 0)
		err |= curl_easy_setopt(cp, CURLOPT_POST, 1);
	if (strcmp("HEAD", request) == 0)
		err |= curl_easy_setopt(cp, CURLOPT_NOBODY, 1);

	if (err == 0)
		err = asr_curl_perform(cp, out_resp, out_buf, out_hdr);

	asr_curl_cleanup(cp);
	return (err);
}

/*
 * Does an HTTP post to the given URL and configured through the optional
 * proxy.  If proxy is NULL then the proxy configured in the asr handle will
 * be used.
 * The HTTP headers will be taken from the hdr nvlist and the payload
 * will use the data and len parameters.
 * The function will return 0 on success and non-zero for an error.
 * HTTP body, response and reply headers are stored in the out parameters
 * if they are non-NULL
 */
int
asr_curl_post_data(asr_curl_req_t *creq,
    struct curl_slist *hdrs, char *data, size_t len,
    asr_buf_t *out_buf, long *out_resp, nvlist_t *out_hdr)
{
	CURL *cp;
	int err;

	*out_resp = -1;
	cp = asr_curl_newreq(creq);
	if (cp == NULL) {
		return (-1);
	}

	err = curl_easy_setopt(cp, CURLOPT_POST, 1);
	err |= curl_easy_setopt(cp, CURLOPT_POSTFIELDS, data);
	err |= curl_easy_setopt(cp, CURLOPT_POSTFIELDSIZE, len);
	err |= curl_easy_setopt(cp, CURLOPT_HTTPHEADER, hdrs);

	if (err == 0)
		err = asr_curl_perform(cp, out_resp, out_buf, out_hdr);

	asr_curl_cleanup(cp);
	return (err);
}

/*
 * Curl read callback data and function
 */
struct asr_curl_read_data {
	size_t len;
	size_t num_sent;
	char *data;
};

static size_t
asr_curl_read(void *ptr, size_t size, size_t nmemb, void *data)
{
	struct asr_curl_read_data *rd = (struct asr_curl_read_data *)data;
	size_t tosend = MIN(nmemb * size, rd->len - rd->num_sent);

	if (tosend == 0)
		return (0);

	bcopy(rd->data + rd->num_sent, ptr, tosend);
	rd->num_sent += tosend;
	return (tosend);
}

/*
 * Does an HTTP put to the given URL and configured through the optional
 * proxy.  If proxy is NULL then the proxy configured in the asr handle will
 * be used.
 * The HTTP headers will be taken from the hdr nvlist and the payload
 * will use the data and len parameters.
 * The function will return 0 on success and non-zero for an error.
 * HTTP body, response and reply headers are stored in the out parameters
 * if they are non-NULL
 */
int
asr_curl_put_data(asr_curl_req_t *creq,
    struct curl_slist *hdrs, char *data, size_t len,
    asr_buf_t *out_buf, long *out_resp, nvlist_t *out_hdr)
{
	struct asr_curl_read_data rdata;
	CURL *cp;
	int err;

	*out_resp = -1;
	cp = asr_curl_newreq(creq);
	if (cp == NULL) {
		return (-1);
	}
	rdata.len = len;
	rdata.num_sent = 0;
	rdata.data = data;
	err = curl_easy_setopt(cp, CURLOPT_UPLOAD, 1);
	err |= curl_easy_setopt(cp, CURLOPT_READDATA, &rdata);
	err |= curl_easy_setopt(cp, CURLOPT_READFUNCTION, asr_curl_read);
	err |= curl_easy_setopt(cp, CURLOPT_HTTPHEADER, hdrs);
	err |= curl_easy_setopt(cp, CURLOPT_INFILESIZE, len);
	err |= curl_easy_setopt(cp, CURLOPT_NOPROGRESS, 1);
	err |= curl_easy_setopt(cp, CURLOPT_NOSIGNAL, 1);
	err |= curl_easy_setopt(cp, CURLOPT_FOLLOWLOCATION, 1);

	if (err == 0)
		err = asr_curl_perform(cp, out_resp, out_buf, out_hdr);

	asr_curl_cleanup(cp);
	return (err);
}

/*
 * Does an HTTP post to the given URL and configured through the optional
 * proxy.  If proxy is NULL then the proxy configured in the asr handle will
 * be used.
 * The HTTP headers will be taken from the hdr nvlist and the form name
 * value pairs will be taken from the plist nvlist argument.
 * Each of the form varialbles will be URL encoded as part of the POST payload.
 *
 * The function will return 0 on success and non-zero for an error.
 * HTTP body, response and reply headers are stored in the out parameters
 * if they are non-NULL
 */
int
asr_curl_post_form(asr_curl_req_t *creq,
    nvlist_t *hdr, nvlist_t *plist,
    asr_buf_t *out_buf, long *out_resp, nvlist_t *out_hdr)
{
	struct curl_slist *headers = asr_curl_headers(hdr);
	asr_buf_t *post = NULL;
	int err;

	if (asr_get_debug(creq->ac_asrh) && plist) {
		nvpair_t *nvp;

		for (nvp = nvlist_next_nvpair(plist, NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(plist, nvp)) {
			char *val;
			char *name = nvpair_name(nvp);
			(void) nvpair_value_string(nvp, &val);
			name = nvpair_name(nvp);
			asr_log_debug(creq->ac_asrh, "%s\n%s\n", name, val);
		}
	}

	post = asr_curl_url_encode(plist);
	headers = curl_slist_append(headers, "Expect:");

	err = asr_curl_post_data(creq, headers,
	    post->asrb_data, post->asrb_length,
	    out_buf, out_resp, out_hdr);
	if (headers != NULL)
		curl_slist_free_all(headers);
	asr_buf_free(post);
	return (err);
}

/*
 * Does an HTTP Post to the given URL/proxy
 * Additional HTTP headers will be added to the post if hdr is defined.
 * Post content is defined in the plist.
 * Non-zero is returned if there is an error.
 */
int
asr_curl_post_multi(asr_curl_req_t *creq,
    nvlist_t *hdr, nvlist_t *plist,
    asr_buf_t *out_buf, long *out_resp, nvlist_t *out_hdr)
{
	CURL *cp;
	char *name, *value;
	struct curl_httppost *post = NULL, *last = NULL;
	struct curl_slist *headers = NULL;
	int err = 0;

	if ((cp = asr_curl_newreq(creq)) == NULL) {
		err = ASR_FAILURE;
		goto finally;
	}


	if (hdr != NULL) {
		nvpair_t *nvp;
		for (nvp = nvlist_next_nvpair(hdr, NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(hdr, nvp)) {
			data_type_t type = nvpair_type(nvp);
			if (type != DATA_TYPE_STRING)
				continue;
			(void) nvpair_value_string(nvp, &value);
			headers = curl_slist_append(headers, value);
		}
	}

	if (plist != NULL) {
		nvpair_t *nvp;
		for (nvp = nvlist_next_nvpair(plist, NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(plist, nvp)) {

			data_type_t type = nvpair_type(nvp);
			if (type != DATA_TYPE_STRING)
				continue;
			name = nvpair_name(nvp);
			(void) nvpair_value_string(nvp, &value);
			err = curl_formadd(&post, &last, CURLFORM_PTRNAME, name,
			    CURLFORM_PTRCONTENTS, value, CURLFORM_END);
		}
	}

	if (headers)
		err |= curl_easy_setopt(cp, CURLOPT_HTTPHEADER, headers);
	err |= curl_easy_setopt(cp, CURLOPT_HTTPPOST, post);

	if (err == 0)
		err = asr_curl_perform(cp, out_resp, out_buf, out_hdr);

finally:
	asr_curl_cleanup(cp);
	if (post != NULL)
		curl_formfree(post);
	if (headers != NULL)
		curl_slist_free_all(headers);

	return (err);
}

/*
 * Generic HTTP Transport for notify
 *
 * Sends a message created by the asr message library using a
 * generic HTTP POST.
 */
int
asr_curl_send_msg_url(
    asr_handle_t *ah, const asr_message_t *msg, char *url, nvlist_t *rsp)
{
	long resp = -1;
	struct curl_slist *hdrs = NULL;
	unsigned long len = strlen(msg->asr_msg_data);
	asr_curl_req_t creq;

	hdrs = curl_slist_append(hdrs, "Content-Type: text/xml");

	asr_curl_init_request(&creq, ah, url, NULL);
	(void) asr_curl_post_data(&creq, hdrs, msg->asr_msg_data,
	    len, NULL, &resp, rsp);

	if (hdrs != NULL)
		curl_slist_free_all(hdrs);

	return (resp != 200);
}

/*
 * Sends a message to the default URL if configured.
 */
int
asr_curl_send_msg(
    asr_handle_t *ah, const asr_message_t *msg, nvlist_t *rsp)
{
	char *url = asr_getprop_str(ah, ASR_PROP_URL);
	if (url == NULL || url[0] == '\0')
		return (0);
	return (asr_curl_send_msg_url(ah, msg, url, rsp));
}
