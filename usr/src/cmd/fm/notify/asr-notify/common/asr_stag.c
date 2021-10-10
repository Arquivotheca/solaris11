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
#include <strings.h>
#include <sys/systeminfo.h>

#include <fm/libasr.h>
#include "asr_curl.h"
#include "asr_ssl.h"
#include "asr_stag.h"

static char *
asr_stag_nvl_str(nvlist_t *nv, char *name)
{
	char *val;
	if (nvlist_lookup_string(nv, name, &val) != 0)
		val = NULL;
	return (val);
}

/*
 * Creates an agent urn from the given system serial number.
 */
int
asr_stag_agent_urn(char *serial, asr_buf_t *urn)
{
	return (asr_buf_append(urn, "urn:st:serial:%s:asr-notify", serial));
}

/*
 * Creates an instance urn from the given system serial number.
 */
char *
asr_stag_inst_urn(char *id)
{
	asr_buf_t *urn;
	if ((urn = asr_buf_alloc(64)) == NULL)
		return (NULL);
	if (asr_buf_append(urn, "urn:st:%s", id) != 0) {
		asr_buf_free(urn);
		return (NULL);
	}
	return (asr_buf_free_struct(urn));
}

/*
 * Creates the agent service tag XML with the given agent URN
 */
int
asr_stag_create_agent(char *serial, char *agent_urn, asr_buf_t *out)
{
	int err = 0;
	int pad = 0;

	time_t now;
	struct tm *gmnow;
	char info[80];

	err |= asr_buf_append_str(out,
	    "<?xml version='1.0' encoding='UTF-8'?>\n");
	err |= asr_buf_append_str(out,
	    "<st1:request xmlns:st1='http://www.sun.com/stv1/agent'>\n");

	err |= asr_buf_append_str(out, "<agent>\n");
	pad++;
	err |= asr_buf_append_xml_nv(out, pad, "agent_urn", agent_urn);
	if (err != 0)
		return (err);

	(void) time(&now);
	gmnow = gmtime(&now);
	(void) strftime(info, sizeof (info), "%F %T GMT", gmnow);
	err |= asr_buf_append_xml_nv(out, pad, "agent_timestamp", info);
	pad++;
	err |= asr_buf_append_str(out, "\t<system_info>\n");

	if (err != 0)
		return (err);

	if (sysinfo(SI_SYSNAME, info, sizeof (info)) == -1) /* SunOS */
		return (-1);
	err |= asr_buf_append_xml_nv(out, pad, "system", info);

	if (sysinfo(SI_HOSTNAME, info, sizeof (info)) == -1)
		return (-1);
	err |= asr_buf_append_xml_nv(out, pad, "host", info);

	if (sysinfo(SI_RELEASE, info, sizeof (info)) == -1) /* 5.11 */
		return (-1);
	err |= asr_buf_append_xml_nv(out, pad, "release", info);

	if (sysinfo(SI_ARCHITECTURE, info, sizeof (info)) == -1) /* i386 */
		return (-1);
	err |= asr_buf_append_xml_nv(out, pad, "architecture", info);

	if (sysinfo(SI_PLATFORM, info, sizeof (info)) == -1) /* i86pc */
		return (-1);
	err |= asr_buf_append_xml_nv(out, pad, "platform", info);

	if (sysinfo(SI_HW_PROVIDER, info, sizeof (info)) == -1)
		return (-1);
	if (info[0] == '\0')
		(void) strcpy(info, "N/A");
	err |= asr_buf_append_xml_nv(out, pad, "manufacturer", info);

	err |= asr_buf_append_xml_nv(out, pad, "cpu_manufacturer", "N/A");
	err |= asr_buf_append_xml_nv(out, pad, "serial_number", serial);
	if (sysinfo(SI_HW_SERIAL, info, sizeof (info)) == -1)
		return (-1);
	err |= asr_buf_append_xml_nv(out, pad, "hostid", info);

	err |= asr_buf_append_str(out, "\t</system_info>\n");
	err |= asr_buf_append_str(out, "</agent>\n");
	err |= asr_buf_append_str(out, "</st1:request>\n");

	return (err);
}

/*
 * Creates a service tag document to register to the service tag URL
 * https://sunconnection-beta.sun.com/ProductRegistrationService/
 *     svctag/urn:st:serial:FG43410018
 *
 * <service_tag>
 *   <instance_urn>urn:st:ece2217c-a21e-4e91-d526-f42a7fadfa7e</instance_urn>
 *   <product_name>Solaris 10 Operating System</product_name>
 *   <product_version>10</product_version>
 *   <product_urn>urn:uuid:5005588c-36f3-11d6-9cec-fc96f718e113</product_urn>
 *   <product_parent_urn>urn:uuid:596ffcfa-63d5-11d7-9886-ac816a682f92
 *   </product_parent_urn>
 *   <product_parent>Solaris Operating System</product_parent>
 *   <product_defined_inst_id/>
 *   <product_vendor>Sun Microsystems</product_vendor>
 *   <platform_arch>i386</platform_arch>
 *   <timestamp>2010-06-12 23:28:52 GMT</timestamp>
 *   <container>global</container>
 *   <source>SUNWstosreg</source>
 *   <installer_uid>95</installer_uid>
 * </service_tag>
 *
 */
int
asr_stag_create(char *inst_urn, char *agent_urn, char *product_urn,
    char *userid, char *domain_id, char *domain_name, asr_buf_t *out)
{
	int err = 0;
	int pad = 0;
	char info[128];
	time_t now;
	struct tm *gmnow;

	err |= asr_buf_append_str(
	    out, "<?xml version='1.0' encoding='UTF-8'?>\n");
	err |= asr_buf_append_str(
	    out, "<st1:request xmlns:st1='http://www.sun.com/stv1/svctag'\n");
	err |= asr_buf_append_str(
	    out, "xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance'>\n");

	err |= asr_buf_append_str(
	    out, "<service_tag>\n");
	pad++;
	err |= asr_buf_append_xml_nv(out, pad, "instance_urn", inst_urn);
	err |= asr_buf_append_xml_nv(out, pad, "sun_user_id", userid);
	err |= asr_buf_append_xml_nv(out, pad, "agent_urn", agent_urn);

	if (sysinfo(SI_SYSNAME, info, sizeof (info)) == -1)
		return (-1);
	err |= asr_buf_append_xml_nv(out, pad, "product_name", info);

	if (sysinfo(SI_RELEASE, info, sizeof (info)) == -1)
		return (-1);
	err |= asr_buf_append_xml_nv(out, pad, "product_version", info);

	if (sysinfo(SI_HW_PROVIDER, info, sizeof (info)) == -1)
		return (-1);
	err |= asr_buf_append_xml_nv(out, pad,
	    "product_vendor", ASR_STAG_PRODUCT_VENDOR);

	if (product_urn == NULL)
		product_urn = ASR_STAG_PRODUCT_URN;
	err |= asr_buf_append_xml_nv(out, pad, "product_urn", product_urn);

	err |= asr_buf_append_xml_nv(out, pad, "product_parent_urn", "null");
	err |= asr_buf_append_xml_nv(out, pad, "product_parent", "null");
	err |= asr_buf_append_str(
	    out, "\t<product_defined_inst_id></product_defined_inst_id>\n");
	err |= asr_buf_append_xml_nv(out, pad, "status", "REGISTERED");

	(void) time(&now);
	gmnow = gmtime(&now);
	(void) strftime(info, sizeof (info), "%F %T GMT", gmnow);
	err |= asr_buf_append_xml_nv(out, pad, "timestamp", info);

	if (sysinfo(SI_HOSTNAME, info, sizeof (info)) == -1)
		return (-1);
	err |= asr_buf_append_xml_nv(out, pad, "customer_asset_tag", info);

	if (domain_name == NULL) {
		(void) snprintf(info, sizeof (info), "$%s", userid);
		domain_name = info;
	}
	err |= asr_buf_append_xml_nv(out, pad, "group_name", domain_name);

	if (domain_id != NULL && domain_id[0] != '\0')
		err |= asr_buf_append_xml_nv(out, pad, "group_id", domain_id);

	err |= asr_buf_append_xml_nv(out, pad, "container", "global");

	err |= asr_buf_append_xml_nv(out, pad, "source", "asr-notify");

	err |= asr_buf_append_str(out, "</service_tag>\n");
	err |= asr_buf_append_str(out, "</st1:request>\n");

	return (err);
}

/*
 * Internal function to do the actual XML paring of domain id and name
 * entries.
 * The results are put in the domains nvlist with property name equal to the
 * domain id and the value set to the domain name.
 */
int
asr_stag_parse_domains(asr_buf_t *xml, nvlist_t *domains)
{
	int err = 0;
	const char *domain = "<domain>";
	const char *domainid = "<domainid>";
	const char *domainname = "<domainname>";
	const char *end = "<";
	char *p = asr_buf_data(xml);
	char *d;

	while (err == 0 && (d = strstr(p, domain)) != NULL) {
		char *endid, *endname;
		char *did = strstr(d, domainid);
		char *dname = strstr(d, domainname);
		if (did == NULL || dname == NULL)
			break;
		did += strlen(domainid);
		if ((endid = strstr(did, end)) == NULL)
			break;
		dname += strlen(domainname);
		if ((endname = strstr(dname, end)) == NULL)
			break;
		*endid = '\0';
		*endname = '\0';
		err = nvlist_add_string(domains, did, dname);
		*endid = '<';
		*endname = '<';
		p = d + 1;
	}

	return (err);
}


static char *
asr_stag_get_desturl(asr_handle_t *ah)
{
	boolean_t beta = B_FALSE;
	char *url;
	char *bval = asr_getprop_str(ah, ASR_PROP_BETA);
	if (bval != NULL && strcmp(bval, ASR_VALUE_TRUE) == 0)
		beta = B_TRUE;

	if (beta)
		url = asr_getprop_strd(ah, ASR_STAG_BETA_URL_PROP,
		    ASR_STAG_DEFAULT_BETA_URL);
	else
		url = asr_getprop_strd(ah, ASR_STAG_URL_PROP,
		    ASR_STAG_DEFAULT_URL);
	return (url);
}

/*
 * Checks that the given service tag for the given URN has been registered.
 *
 * Request URL:
 *    https://inv-cs.sun.com/ProductRegistrationService/agent/$agent_urn
 *
 * Request Headers:
 *    client_reg_id=urn:scn:clregid:20071002170609681
 *    payload=user id|domain id
 *    payload_sig=@payload_sig.txt
 */
int
asr_stag_check_reg(asr_handle_t *ah, nvlist_t *reg, char *type, char *urn)
{
	int err = 0;
	asr_curl_req_t creq;
	struct curl_slist *hdrs = NULL;
	asr_buf_t *url = NULL, *buf = NULL;
	long resp;
	char *user, *key, *sig64;

	if (strcmp(type, ASR_STAG_REG_TYPE_AGENT) == 0)
		user = asr_stag_nvl_str(reg, ASR_PROP_REG_USER_ID);
	else
		user = asr_stag_nvl_str(reg, ASR_PROP_REG_DOMAIN_ID);

	key = asr_stag_nvl_str(reg, ASR_PROP_REG_MSG_KEY);
	sig64 = asr_ssl_sign64(key, user, strlen(user));

	if (sig64 == NULL)
		return (asr_set_errno(EASR_SSL_LIBSSL));

	if ((url = asr_buf_alloc(128)) == NULL) {
		err = -1;
		goto finally;
	}
	if ((err = asr_buf_append(url, "%s/ProductRegistrationService/%s/%s",
	    asr_stag_get_desturl(ah), type, urn)) != 0)
		goto finally;

	if ((buf = asr_buf_alloc(128)) == NULL) {
		err = -1;
		goto finally;
	}

	if ((err = asr_curl_hdr_append(&hdrs, "client_reg_id: %s",
	    asr_stag_nvl_str(reg, ASR_PROP_REG_CLIENT_ID))) != 0)
		goto finally;
	if ((err = asr_curl_hdr_append(&hdrs,
	    "payload_sig: %s", sig64)) != 0)
		goto finally;

	if ((err = asr_curl_hdr_append(&hdrs, "payload: %s", user)) != 0)
		goto finally;

	asr_curl_init_request(&creq, ah, url->asrb_data, NULL);
	err = asr_curl_request(&creq, "GET", hdrs, buf, &resp, NULL);

	if (resp != 200) {
		(void) asr_error(EASR_SC,
		    "Service Tag registration error: %s",
		    asr_buf_data(buf));
		err = resp;
	}

finally:
	asr_buf_free(buf);
	asr_buf_free(url);
	free(sig64);
	return (err);
}

/*
 * Gets the headers used to post agent xml to the stag url.
 *
 * Request URL:
 *    https://inv-cs.sun.com/ProductRegistrationService/agent/$agent_urn
 *
 * Request Headers:
 *    client_reg_id=urn:scn:clregid:20071002170609681
 *    payload_sig=@agent_reg_sig.txt
 */
int
asr_stag_register(asr_handle_t *ah, nvlist_t *reg,
    char *type, char *urn, asr_buf_t *xml)
{
	int err = 0;
	asr_curl_req_t creq;
	struct curl_slist *hdrs = NULL;
	asr_buf_t *url = NULL, *buf = NULL;
	long resp;
	char *key = asr_stag_nvl_str(reg, ASR_PROP_REG_MSG_KEY);
	char *sig64 = asr_ssl_sign64(key, xml->asrb_data, xml->asrb_length);

	if (sig64 == NULL)
		return (asr_set_errno(EASR_SSL_LIBSSL));

	if ((url = asr_buf_alloc(128)) == NULL) {
		err = -1;
		goto finally;
	}
	if ((err = asr_buf_append(url, "%s/ProductRegistrationService/%s/%s",
	    asr_stag_get_desturl(ah), type, urn)) != 0)
	goto finally;

	if ((buf = asr_buf_alloc(128)) == NULL) {
		err = -1;
		goto finally;
	}

	if ((err = asr_curl_hdr_append(&hdrs, "client_reg_id: %s",
	    asr_stag_nvl_str(reg, ASR_PROP_REG_CLIENT_ID))) != 0)
		goto finally;
	if ((err = asr_curl_hdr_append(&hdrs,
	    "payload_sig: %s", sig64)) != 0)
		goto finally;

	asr_curl_init_request(&creq, ah, url->asrb_data, NULL);
	err = asr_curl_put_data(&creq, hdrs,
	    xml->asrb_data, xml->asrb_length, buf, &resp, NULL);

	if (resp != 201) {
		(void) asr_error(EASR_SC,
		    "Service Tag registration error: %s", asr_buf_data(buf));
		err = resp;
	}

finally:
	asr_buf_free(buf);
	asr_buf_free(url);
	free(sig64);
	return (err);
}

/*
 * Gets the list of service tag domains for the given user name.
 * Client registration with SCRK must be done before this call.
 *
 * The domains will be put in a name value list with the domain id
 * as the property name and the domain name as the property value.
 *
 * DTS uses a different call to get domains.  see asr_dts.c to get
 * domains using DTS.
 *
 * curl -G -H "client_reg_id:
 * urn:scn:clregid:a70a04f6-6d3c-40e9-8b87-58c86d787986:20071002170609681"
 * -H "payload: pmonday" -H "payload_sig:
 * <insert base64 encoded sha1 signature of the userid here>"
 * -v https://sunconnection-beta.sun.com/ProductRegistrationService/domain/nm
 *
 * Web service will return XML data like the following:
 * <?xml version="1.0" encoding="UTF-8"?>
 * <usersdomains xmlns="http://xml.netbeans.org/schema/newXMLSchema">
 *   <soaid>james.kremer@sun.com</soaid>
 *   <domain>
 *     <domainid>37985255</domainid>
 *     <domainname>$james.kremer@sun.com</domainname>
 *     <domainparentid>0</domainparentid>
 *   </domain>
 * </usersdomains>
 */
int
asr_stag_get_domains(asr_handle_t *ah, nvlist_t *reg, nvlist_t *domains)
{
	int err = 0;
	long resp;
	asr_curl_req_t creq;
	asr_buf_t *url = NULL, *buf = NULL;
	struct curl_slist *hdrs = NULL;
	char *user = asr_stag_nvl_str(reg, ASR_PROP_REG_USER_ID);
	char *key = asr_stag_nvl_str(reg, ASR_PROP_REG_MSG_KEY);
	char *sig64 = asr_ssl_sign64(key, user, strlen(user));

	if (sig64 == NULL)
		return (asr_set_errno(EASR_SSL_LIBSSL));

	if ((url = asr_buf_alloc(128)) == NULL) {
		err = -1;
		goto finally;
	}
	if ((err = asr_buf_append(url,
	    "%s/ProductRegistrationService/domain/%s",
	    asr_stag_get_desturl(ah), user)) != 0)
		goto finally;

	if ((buf = asr_buf_alloc(128)) == NULL) {
		err = -1;
		goto finally;
	}

	if ((err = asr_curl_hdr_append(&hdrs, "client_reg_id: %s",
	    asr_stag_nvl_str(reg, ASR_PROP_REG_CLIENT_ID))) != 0)
		goto finally;

	if ((err = asr_curl_hdr_append(&hdrs, "payload: %s", user)) != 0)
		goto finally;

	if ((err = asr_curl_hdr_append(&hdrs, "payload_sig: %s", sig64)) != 0)
		goto finally;

	asr_curl_init_request(&creq, ah, asr_buf_data(url), NULL);
	err = asr_curl_get(&creq, hdrs, buf, &resp, NULL);

	if (resp != ASR_STAG_HTTP_STATUS_OK) {
		err = asr_error(EASR_SC,
		    "Web RPC failed with return code %ld", resp);
		goto finally;
	}

	err = asr_stag_parse_domains(buf, domains);

finally:
	asr_buf_free(url);
	asr_buf_free(buf);
	free(sig64);
	return (err);
}
