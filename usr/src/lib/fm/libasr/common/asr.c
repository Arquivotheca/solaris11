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
#include <netdb.h>
#include <strings.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/fm/protocol.h>
#include <sys/systeminfo.h>
#include <sys/varargs.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "asr.h"
#include "asr_mem.h"
#include "asr_nvl.h"
#include "asr_scf.h"
#include "fm/libasr.h"
#include "libasr.h"

#define	ASR_ACTIVATE_BUFSIZE 1279

static pthread_mutex_t asr_sysprops_lock;
static nvlist_t *asr_sysprops = NULL;
static int asr_sysinfo(asr_handle_t *);

/*
 * Formats local time into a string for debugging messages.
 */
static void
asr_time(char *timebuf, int tlen)
{
	struct tm *tmptr;
	time_t now = time(NULL);
	tmptr = localtime(&now);
	if (tmptr == NULL)
		(void) strcpy(timebuf, "?");
	else
		(void) strftime(timebuf, tlen, "%c", tmptr);
}

/*
 * Sets the property name in the ASR handle with the given value.
 */
int
asr_setprop_str(asr_handle_t *ah, const char *name, const char *value)
{
	if (ah == NULL || name == NULL || ah->asr_cfg == NULL) {
		asr_log_errno(ah, EASR_NULLDATA);
		return (EASR_NULLDATA);
	}

	if (asr_nvl_add_str(ah->asr_cfg, name, value) != 0) {
		asr_log_error(ah, EASR_NVLIST,
		    "Unable to set property %s to %s",
		    name, value == NULL ? "null" : value);
		return (ASR_FAILURE);
	}

	return (ASR_OK);
}

/*
 * Sets the property name in the ASR handle with the given value.
 * If value isn't set because it is NULL then the default_value will be used.
 */
int
asr_setprop_strd(asr_handle_t *ah, const char *name,
    const char *value, const char *default_value)
{
	return (asr_setprop_str(ah, name,
	    (value == NULL) ? default_value : value));
}

/*
 * Gets a property from the ASR handle.  If the property doesn't exist,
 * its value is an empty string or there is an error then the default value is
 * returned.
 * If there is an error during the operation then asr_errno is set.
 */
char *
asr_getprop_strd(asr_handle_t *ah, const char *name, char *default_value)
{
	int err = 1;
	char *val;

	if (ah == NULL || name == NULL || ah->asr_cfg == NULL) {
		asr_log_errno(ah, EASR_NULLDATA);
		return (NULL);
	}

	err = nvlist_lookup_string(ah->asr_cfg, name, &val);

	if (err)
		val = default_value;

	if (val == NULL || val[0] == '\0')
		val = default_value;

	return (val);
}

/*
 * Gets a property from the ASR handle.  If the property doesn't exist, its
 * value is empty or there was an error then NULL is returned.
 */
char *
asr_getprop_str(asr_handle_t *ah, const char *name)
{
	return (asr_getprop_strd(ah, name, NULL));
}

/*
 * Gets a boolean property from the ASR handle.  If the property doesn't
 * exist then the default value is returned.
 */
boolean_t
asr_getprop_bool(asr_handle_t *ah, const char *name, boolean_t default_value)
{
	char *val = asr_getprop_str(ah, name);
	boolean_t retval = default_value;

	if (val == NULL) {
		boolean_t bval;
		if (nvlist_lookup_boolean_value(ah->asr_cfg, name, &bval) == 0)
			retval = bval;
	} else {
		if (strcmp(ASR_VALUE_TRUE, val) == 0)
			retval = B_TRUE;
		else if (strcmp(ASR_VALUE_FALSE, val) == 0)
			retval = B_FALSE;
	}
	return (retval);
}

/*
 * Gets a number property from the ASR handle.  If the property doesn't
 * exist or can't be parsed then the default value is returned.
 */
long
asr_getprop_long(asr_handle_t *ah, const char *name, long default_value)
{
	char *val = asr_getprop_str(ah, name);
	long retval = default_value;

	if (val == NULL) {
		int64_t ival;
		if (nvlist_lookup_int64(ah->asr_cfg, name, &ival) == 0)
			retval = ival;
	} else {
		long lval;
		errno = 0;
		lval = atol(val);
		if (errno == 0)
			retval = lval;
	}
	return (retval);
}

/*
 * Get the path value for the given property name.  The root directory will
 * be appended to the path.  It is the responsibility of the caller to
 * free the resulting string when done.
 * If the property isn't set then the default_value will be returned.
 * If there is an error then NULL will be returned.
 */
char *asr_getprop_path(
    asr_handle_t *ah, const char *name, const char *default_value)
{
	char *path = NULL;
	char *root = asr_getprop_strd(ah, ASR_PROP_ROOTDIR, "/");
	int rlen = strlen(root);
	char *file = asr_getprop_str(ah, name);
	int plen;

	if (file == NULL || file[0] == '\0')
		file = (char *)default_value;
	if (file == NULL || file[0] == '\0')
		return (NULL);
	plen = rlen + strlen(file) + 2;
	if ((path = malloc(plen)) == NULL)
		return (NULL);
	if ('/' == root[rlen-1])
		(void) snprintf(path, plen, "%s%s", root, file);
	else
		(void) snprintf(path, plen, "%s/%s", root, file);
	return (path);
}

/*
 * Gets the registered asset id of the Solaris host.  If one doesn't exist
 * a new uuid is generated.
 */
char *
asr_get_assetid(asr_handle_t *ah)
{
	char *asset_id = asr_getprop_str(ah, ASR_PROP_ASSET_ID);

	if (asset_id == NULL || asset_id[0] == '\0')
		asset_id = asr_getprop_str(ah, ASR_PROP_REG_ASSET_ID);
	if (asset_id == NULL || asset_id[0] == '\0') {
		uuid_t uuid;
		char uuidbuf[UUID_PRINTABLE_STRING_LENGTH];
		uuid_generate(uuid);
		uuid_unparse(uuid, uuidbuf);
		if (asr_setprop_str(ah, ASR_PROP_ASSET_ID, uuidbuf) == 0)
			asset_id = asr_getprop_str(ah, ASR_PROP_ASSET_ID);
	}
	return (asset_id);
}

/*
 * Gets a system property. If it isn't set parse topo and set it.
 */
char *
asr_get_sysprop(asr_handle_t *ah, char *propname)
{
	char *value = asr_getprop_str(ah, propname);
	if (value == NULL || value[0] == '\0') {
		if (asr_sysinfo(ah) == 0)
			value = asr_getprop_str(ah, propname);
	}
	return (value);
}

/*
 * Gets the system id which is usually the chassis serial number
 */
char *
asr_get_systemid(asr_handle_t *ah)
{
	return (asr_get_sysprop(ah, ASR_PROP_SYSTEM_ID));
}

/*
 * Gets the product id of the computer system if available.
 */
char *
asr_get_productid(asr_handle_t *ah)
{
	return (asr_get_sysprop(ah, ASR_PROP_PRODUCT_ID));
}

/*
 * Gets the site id which is usually the same as the systemid.  In the case
 * of an appliance the appliance serial number will be used.
 */
char *
asr_get_siteid(asr_handle_t *ah)
{
	char *site_id = asr_getprop_str(ah, ASR_PROP_SITE_ID);
	if (site_id == NULL || site_id[0] == '\0')
		site_id = asr_get_systemid(ah);
	return (site_id);
}

/*
 * Gets the registered ASR client registration id
 */
char *
asr_get_regid(asr_handle_t *ah)
{
	return (asr_getprop_str(ah, ASR_PROP_REG_CLIENT_ID));
}

/*
 * Gets the message signing key length to be used when generating new
 * client public/private key pairs for signing messages.
 */
size_t
asr_get_keylen(asr_handle_t *ah)
{
	char *keylen = asr_getprop_str(ah, ASR_PROP_KEYLEN);
	unsigned long len;

	if (keylen == NULL)
		len = ASR_DEFAULT_KEYLEN;
	else if (sscanf(keylen, "%lu", &len) != 1) {
		(void) asr_error(EASR_PROP_USAGE,
		    "Error parsing keylen (%s). Using default value %i",
		    keylen, ASR_DEFAULT_KEYLEN);
		len = ASR_DEFAULT_KEYLEN;
	}
	return (len);
}

/*
 * Gets the timeout to be used for http requests.
 */
long
asr_get_http_timeout(asr_handle_t *ah)
{
	char *timeout = asr_getprop_str(ah, ASR_PROP_HTTP_TIMEOUT);
	long nsec;

	if (timeout == NULL)
		nsec = ASR_DEFAULT_HTTP_TIMEOUT;
	else if (sscanf(timeout, "%ld", &nsec) != 1) {
		(void) asr_error(EASR_PROP_USAGE,
		    "Error parsing http timeout (%s). Using default value %i",
		    timeout, ASR_DEFAULT_HTTP_TIMEOUT);
		nsec = ASR_DEFAULT_HTTP_TIMEOUT;
	}
	nsec *= 1000000L;
	return (nsec);
}

/*
 * Gets the directory that the phone home service can use for writing
 * log files, saving state and temporary data.
 */
char *
asr_get_datadir(asr_handle_t *ah)
{
	return (asr_getprop_strd(ah, ASR_PROP_DATA_DIR, "."));
}

/*
 * Gets the registered message signing keys.
 */
char *
asr_get_privkey(asr_handle_t *ah)
{
	return (asr_getprop_strd(ah, ASR_PROP_REG_MSG_KEY, (char *)NULL));
}

/*
 * Gets the current phone-home configuration properties.  If the properties
 * or handle haven't been initialized then NULL is returned.
 */
nvlist_t *
asr_get_config(asr_handle_t *ah)
{
	return (ah == NULL ? NULL : ah->asr_cfg);
}

/*
 * Gets the log file handle used for debugging
 */
FILE *
asr_get_logfile(asr_handle_t *ah)
{
	return (ah == NULL ? NULL : ah->asr_log);
}

/*
 * Sets the log file used for debugging
 */
void
asr_set_logfile(asr_handle_t *ah, FILE *log)
{
	ah->asr_log = log;
}

/*
 * Sets the configuration name used to save the configuration.  The name
 * can either be a filename (for debugging) or a service FMRI.
 */
int
asr_set_config_name(asr_handle_t *ah, const char *name)
{
	if (ah->asr_cfg_name != NULL)
		free(ah->asr_cfg_name);
	if (name == NULL) {
		ah->asr_cfg_name = NULL;
		return (0);
	}
	ah->asr_cfg_name = asr_strdup(name);
	return (ah->asr_cfg_name == NULL ? ASR_FAILURE : ASR_OK);
}

/*
 * Prints the ASR Phone Home configuration properties to the given stream.
 * None zero is returned if there is an error.
 */
int
asr_print_config(asr_handle_t *ah, FILE *out)
{
	if (ah == NULL || ah->asr_cfg == NULL || out == NULL) {
		asr_log_errno(ah, EASR_NULLDATA);
		return (NULL);
	}
	return (asr_nvl_print_properties(out, ah->asr_cfg));
}

/*
 * Determines if the config is a service FMRI (true) or a file name.
 */
static boolean_t
asr_cfg_is_file(char *config)
{
	boolean_t is_file = B_FALSE;

	if (config != NULL) {
		char *svc = "svc:/";
		int i;
		for (i = 0; svc[i] != '\0'; i++) {
			if (config[i] != svc[i]) {
				is_file = B_TRUE;
				break;
			}
		}
	}
	return (is_file);
}

/*
 * Saves the ASR configuration properties.
 */
int
asr_save_config(asr_handle_t *ah)
{
	int err = 0;
	char *config = ah->asr_cfg_name;
	boolean_t is_file = asr_cfg_is_file(config);

	asr_log_debug(ah, "Saving registration to (%s)", config);

	if (config == NULL) {
		return (ASR_FAILURE);
	} else if (is_file) {
		FILE *cfile = fopen(config, "w");
		if (cfile != NULL) {
			err = asr_print_config(ah, cfile);
			err |= fclose(cfile);
		}
	} else {
		err = asr_scf_set_props(config, "reg", asr_get_config(ah));
	}
	return (err);
}

/*
 * Initializes the ASR handle with the given configuration.  If there is an
 * error then NULL is returned and asr_errno is set.
 */
static asr_handle_t *
asr_hdl_initnv(nvlist_t *cfg, char *cfg_name)
{
	char info[256];
	asr_handle_t *ah;

	if (cfg == NULL) {
		cfg = asr_nvl_alloc();
		if (cfg == NULL)
			return (NULL);
	}
	if ((ah = asr_zalloc(sizeof (asr_handle_t))) == NULL) {
		asr_nvl_free(cfg);
		return (NULL);
	}
	ah->asr_cfg = cfg;
	if (asr_set_config_name(ah, cfg_name) != ASR_OK) {
		asr_hdl_destroy(ah);
		return (NULL);
	}
	ah->asr_debug = asr_getprop_bool(ah, ASR_PROP_DEBUG, B_FALSE);

	if (sysinfo(SI_HOSTNAME, info, sizeof (info)) == -1 ||
	    (ah->asr_host_id = asr_strdup(info)) == NULL) {
		(void) asr_error(EASR_UNKNOWN, "System error (%s)",
		    strerror(errno));
		asr_hdl_destroy(ah);
		ah = NULL;
	}

	return (ah);
}

/*
 * Creates an ASR handle from either a file path name or a service
 * FMRI that has ASR properties defined.
 * NULL is returned and asr_errno is set if there is an error.
 */
asr_handle_t *
asr_hdl_init(char *config)
{
	boolean_t is_file = asr_cfg_is_file(config);
	nvlist_t *nvcfg;

	if ((nvcfg = asr_nvl_alloc()) == NULL)
		return (NULL);

	if (config == NULL)
		return (asr_hdl_initnv(NULL, NULL));

	if (is_file) {
		FILE *in = fopen(config, "r");
		if (in == NULL ||
		    asr_nvl_read_properties(in, nvcfg) != 0) {
			asr_nvl_free(nvcfg);
			return (NULL);
		}
		(void) fclose(in);
	} else {
		if (asr_scf_load(config, nvcfg) != 0) {
			asr_nvl_free(nvcfg);
			return (NULL);
		}
	}
	return (asr_hdl_initnv(nvcfg, config));
}

/*
 * Creates an empty registration request structure used for registing a
 * client with ASR.
 */
asr_regreq_t *
asr_regreq_init()
{
	asr_regreq_t *regreq = asr_zalloc(sizeof (asr_regreq_t));
	return (regreq);
}

/*
 * Sets the registration user name.
 */
int
asr_regreq_set_user(asr_regreq_t *regreq, const char *user)
{
	if (regreq == NULL || user == NULL) {
		(void) asr_set_errno(EASR_NULLDATA);
		return (ASR_FAILURE);
	}
	if ((regreq->asr_user = asr_strdup(user)) == NULL)
		return (ASR_FAILURE);
	return (ASR_OK);
}

/*
 * Sets the registration users credential
 */
int
asr_regreq_set_password(asr_regreq_t *regreq, const char *password)
{
	if (regreq == NULL || password == NULL) {
		(void) asr_set_errno(EASR_NULLDATA);
		return (ASR_FAILURE);
	}
	if ((regreq->asr_password = asr_strdup(password)) == NULL)
		return (ASR_FAILURE);
	return (ASR_OK);
}

/*
 * Gets the name of the user requesting an ASR registration.
 */
char *
asr_regreq_get_user(const asr_regreq_t *regreq)
{
	return (regreq->asr_user);
}

/*
 * Gets the password of the user requesting an ASR registration.
 */
char *
asr_regreq_get_password(const asr_regreq_t *regreq)
{
	return (regreq->asr_password);
}

/*
 * Frees up all resources used for a registration request.
 */
void
asr_regreq_destroy(asr_regreq_t *regreq)
{
	if (regreq != NULL) {
		if (regreq->asr_user != NULL)
			asr_strfree_secure(regreq->asr_user);
		if (regreq->asr_password != NULL)
			asr_strfree_secure(regreq->asr_password);
		free(regreq);
	}
}

/*
 * Builds up a URL representation and type of an HTTP proxy from the
 * ASR configuration.  It is up to the caller to free the url and type
 * when they are finished.
 */
extern int
asr_create_proxy_url(asr_handle_t *ah, char **ret_url, char **ret_type)
{
	int err = 0;
	asr_buf_t *url;
	char *user, *type, *pass, *port;
	char *host = asr_getprop_str(ah, ASR_PROP_PROXY_HOST);

	*ret_url = NULL;
	*ret_type = NULL;

	/* If host isn't set don't use proxy */
	if (host == NULL || host[0] == '\0')
		return (ASR_OK);

	/* Set up proxy type */
	type = asr_getprop_str(ah, ASR_PROP_PROXY_TYPE);
	if (type == NULL) {
		*ret_type = NULL;
	} else {
		if ((*ret_type = asr_strdup(type)) == NULL) {
			return (ASR_FAILURE);
		}
	}

	/* Setup proxy url using user:pass@host format */
	user = asr_getprop_str(ah, ASR_PROP_PROXY_USER);
	pass = asr_getprop_str(ah, ASR_PROP_PROXY_PASS);
	port = asr_getprop_strd(ah, ASR_PROP_PROXY_PORT,
	    ASR_PROXY_DEFAULT_PORT);

	if ((url = asr_buf_alloc(64)) == NULL)
		return (ASR_FAILURE);

	if (user != NULL && user[0] != '\0') {
		err |= asr_buf_append_str(url, user);
		if (pass != NULL && pass[0] != '\0') {
			err |= asr_buf_append_char(url, ':');
			err |= asr_buf_append_str(url, pass);
		}
		err |= asr_buf_append_char(url, '@');
	}

	err |= asr_buf_append_str(url, host);
	err |= asr_buf_append_char(url, ':');
	err |= asr_buf_append_str(url, port);

	if (err == 0) {
		*ret_url = asr_buf_free_struct(url);
	} else {
		asr_buf_free(url);
		if (*ret_type != NULL)
			free(*ret_type);
		*ret_type = NULL;
	}

	return (err == 0 ? ASR_OK : ASR_FAILURE);
}

static void asr_tprt_destroy(asr_transport_t *tprt)
{
	if (tprt == NULL)
		return;
	if (tprt->asr_tprt_name != NULL)
		free(tprt->asr_tprt_name);
	free(tprt);
}

/*
 * Initializes the transport functions needed to transmit ASR messages.
 */
int
asr_set_transport(asr_handle_t *ah, char *name,
    int (*asr_register_client)(
    asr_handle_t *ah, const asr_regreq_t *req, nvlist_t *rsp),
    int (*asr_unregister_client)(asr_handle_t *ah),
    int (*asr_send_msg)(
    asr_handle_t *ah, const asr_message_t *msg, nvlist_t *rsp))
{
	asr_transport_t *tprt = NULL;

	tprt = asr_zalloc(sizeof (asr_transport_t));
	if (tprt == NULL)
		return (ASR_FAILURE);
	tprt->asr_register_client = asr_register_client;
	tprt->asr_unregister_client = asr_unregister_client;
	tprt->asr_send_msg = asr_send_msg;
	tprt->asr_tprt_name = asr_strdup(name);
	asr_tprt_destroy(ah->asr_tprt);
	ah->asr_tprt = tprt;
	return (ASR_OK);
}

/*
 * Cleans up the ASR handle when done.
 */
void
asr_hdl_destroy(asr_handle_t *ah)
{
	if (ah != NULL) {
		if (ah->asr_cfg != NULL) {
			nvlist_free(ah->asr_cfg);
			ah->asr_cfg = NULL;
		}
		if (ah->asr_log != NULL) {
			if (ah->asr_log != stdout && ah->asr_log != stderr)
				(void) fclose(ah->asr_log);
			ah->asr_log = NULL;
		}
		asr_tprt_destroy(ah->asr_tprt);
		if (ah->asr_cfg_name != NULL)
			free(ah->asr_cfg_name);
		if (ah->asr_host_id != NULL)
			free(ah->asr_host_id);
		free(ah);
	} else {
		(void) asr_set_errno(EASR_NULLFREE);
	}
}

/*
 * Cleans up all cached data inside the ASR library.
 */
void
asr_cleanup()
{
	(void) pthread_mutex_lock(&asr_sysprops_lock);
	if (asr_sysprops != NULL) {
		asr_nvl_free(asr_sysprops);
		asr_sysprops = NULL;
	}
	(void) pthread_mutex_unlock(&asr_sysprops_lock);
}

/*
 * Cleans up an ASR message and its contents.
 */
void
asr_free_msg(asr_message_t *msg)
{
	if (msg != NULL) {
		if (msg->asr_msg_data != NULL)
			free(msg->asr_msg_data);
		free(msg);
	}
}

/*
 * Converts a new ASR message from a buffer and the given type.
 * Returns NULL and frees data if there is an error.
 */
asr_message_t *
asr_message_alloc(asr_buf_t *data, asr_msgtype_t type)
{
	asr_message_t *msg;

	if ((msg = asr_zalloc(sizeof (asr_message_t))) == NULL) {
		asr_buf_free(data);
		return (NULL);
	}
	msg->asr_msg_type = type;
	msg->asr_msg_len = data->asrb_length;
	msg->asr_msg_data = asr_buf_free_struct(data);

	return (msg);
}

/*
 * Fills ASR system ID registration values as the topology is enumerated.
 */
/* ARGSUSED */
static int
asr_reg_fill_topo_enum(topo_hdl_t *thp, tnode_t *node, void *arg)
{
	int err = 0;
	asr_topo_enum_data_t *tdata = arg;
	nvlist_t *sys = tdata->asr_data;
	nvlist_t *resource = NULL, *authority = NULL;
	char *product_id, *system_id;

	if (topo_node_resource(node, &resource, &err) != 0)
		goto finally;

	product_id = asr_nvl_str(sys, ASR_PROP_PRODUCT_NAME);
	system_id = asr_nvl_str(sys, ASR_PROP_SYSTEM_ID);

	if (nvlist_lookup_nvlist(
	    resource, "authority", &authority) != 0)
		goto finally;

	if (NULL == product_id) {
		product_id = asr_nvl_str(authority, "product-id");
		if (NULL != product_id)
			err |= asr_nvl_add_str(sys, ASR_PROP_PRODUCT_NAME,
			    product_id);
	}
	if (NULL == system_id) {
		system_id = asr_nvl_str(authority, "product-sn");
		if (NULL == system_id)
			system_id = asr_nvl_str(authority, "chassis-id");
		if (NULL == system_id)
			system_id = asr_nvl_str(resource, "serial");
		if (NULL != system_id)
			err |= asr_nvl_add_str(sys, ASR_PROP_SYSTEM_ID,
			    system_id);
	}

finally:
	if (authority)
		nvlist_free(authority);
	if (resource)
		nvlist_free(resource);

	if (err != 0)
		return (TOPO_WALK_TERMINATE);
	if (NULL != product_id && NULL != system_id)
		return (TOPO_WALK_TERMINATE);

	return (TOPO_WALK_NEXT);
}

/*
 * Sets the system ID properties in the internal ASR handle using information
 * from libtopo.   If a system property has been set previously then it
 * will not be changed.
 */
static int
asr_reg_set_sysinfo(asr_handle_t *ah, nvlist_t *sys)
{
	char *product_id, *product_name, *system_id;
	char info[80];

	system_id = asr_getprop_str(ah, ASR_PROP_SYSTEM_ID);
	if (system_id == NULL || system_id[0] == '\0') {
		system_id = asr_nvl_str(sys, ASR_PROP_SYSTEM_ID);
		if (system_id == NULL) {
			system_id = info;
			if (sysinfo(SI_HW_SERIAL, info, sizeof (info)) == -1)
				system_id = asr_get_assetid(ah);
			if (system_id == NULL || system_id[0] == '\0' ||
			    strcmp("0", system_id) == 0)
				system_id = asr_get_assetid(ah);
			if (asr_nvl_add_str(
			    sys, ASR_PROP_SYSTEM_ID, system_id) != 0)
				return (ASR_FAILURE);
		}
		if (asr_setprop_str(
		    ah, ASR_PROP_SYSTEM_ID, system_id) != 0)
			return (ASR_FAILURE);
	}

	product_name = asr_getprop_str(ah, ASR_PROP_PRODUCT_NAME);
	if (product_name == NULL || product_name[0] == '\0') {
		product_name = asr_nvl_str(sys, ASR_PROP_PRODUCT_NAME);
		if (product_name == NULL) {
			product_name = ASR_DEFAULT_PRODUCT_NAME;
			if (asr_nvl_add_str(
			    sys, ASR_PROP_PRODUCT_NAME, product_name) != 0)
				return (ASR_FAILURE);
		}
		if (asr_setprop_str(
		    ah, ASR_PROP_PRODUCT_NAME, product_name) != 0)
			return (ASR_FAILURE);
	}

	product_id = asr_getprop_str(ah, ASR_PROP_PRODUCT_ID);
	if (product_id == NULL || product_id[0] == '\0') {
		product_id = asr_nvl_str(sys, ASR_PROP_PRODUCT_ID);
		if (product_id == NULL) {
			product_id = product_name;
			if (asr_nvl_add_str(
			    sys, ASR_PROP_PRODUCT_ID, product_id) != 0)
				return (ASR_FAILURE);
		}
		if (asr_setprop_str(
		    ah, ASR_PROP_PRODUCT_ID, product_id) != 0)
			return (ASR_FAILURE);
	}

	return (EASR_NONE);
}

/*
 * Finds system information such as system-id and product-name from
 * parsing fm libtopo.
 * Stores the properties in a cache so that they are available for
 * future calls.
 */
static int
asr_sysinfo(asr_handle_t *ah)
{
	int err = 0;
	(void) pthread_mutex_lock(&asr_sysprops_lock);
	if (asr_sysprops == NULL) {
		if ((asr_sysprops = asr_nvl_alloc()) == NULL) {
			err = ASR_FAILURE;
			goto finally;
		}
		if ((err = asr_topo_walk(ah, asr_reg_fill_topo_enum,
		    asr_sysprops)) != 0) {
			asr_nvl_free(asr_sysprops);
			asr_sysprops = NULL;
			goto finally;
		}
		if ((err = asr_reg_set_sysinfo(ah, asr_sysprops)) != 0) {
			asr_nvl_free(asr_sysprops);
			asr_sysprops = NULL;
			goto finally;
		}
		asr_log_debug(ah, "system  ID = %s", asr_get_systemid(ah));
		asr_log_debug(ah, "product ID = %s", asr_get_productid(ah));
	} else {
		err = asr_nvl_merge(ah->asr_cfg, asr_sysprops);
	}
finally:
	(void) pthread_mutex_unlock(&asr_sysprops_lock);
	return (err);
}

/*
 * Saves the registration properties into the ASR handle for all future
 * message transmission on that handle.
 * The name value properties are from a previous call to asr_reg
 * Returns non-zero on error.
 */
int
asr_reg_save(asr_handle_t *ah, nvlist_t *reg)
{
	char *mt = "";
	nvlist_t *cfg = ah->asr_cfg;
	int err = 0;

	err |= asr_nvl_cp_strd(reg, cfg, ASR_PROP_REG_CLIENT_ID, mt);
	err |= asr_nvl_cp_strd(reg, cfg, ASR_PROP_REG_CODE, mt);
	err |= asr_nvl_cp_strd(reg, cfg, ASR_PROP_REG_DOMAIN_ID, mt);
	err |= asr_nvl_cp_strd(reg, cfg, ASR_PROP_REG_DOMAIN_NAME, mt);
	err |= asr_nvl_cp_strd(reg, cfg, ASR_PROP_REG_MESSAGE, mt);
	err |= asr_nvl_cp_strd(reg, cfg, ASR_PROP_REG_MSG_KEY, mt);
	err |= asr_nvl_cp_strd(reg, cfg, ASR_PROP_REG_PUB_KEY, mt);
	err |= asr_nvl_cp_strd(reg, cfg, ASR_PROP_REG_SYSTEM_ID, mt);
	err |= asr_nvl_cp_strd(reg, cfg, ASR_PROP_REG_ASSET_ID, mt);
	err |= asr_nvl_cp_strd(reg, cfg, ASR_PROP_REG_URL, mt);
	err |= asr_nvl_cp_strd(reg, cfg, ASR_PROP_REG_USER_ID, mt);

	return (err);
}

/*
 * Fills in the registration response properties taken from the users
 * registration request structure and system information.
 * Returns ASR_FAILURE if there is an error or ASR_OK if successful.
 */
int
asr_reg_fill(asr_handle_t *ah, const asr_regreq_t *regreq, nvlist_t *reg)
{
	int err = ASR_OK;

	err = asr_nvl_add_str(reg, ASR_PROP_SYSTEM_ID, asr_get_systemid(ah));
	err |= asr_nvl_add_str(reg, ASR_PROP_PRODUCT_ID, asr_get_productid(ah));
	err |= asr_nvl_add_str(reg, ASR_PROP_PRODUCT_NAME,
	    asr_get_sysprop(ah, ASR_PROP_PRODUCT_NAME));
	err |= asr_nvl_add_str(reg, ASR_PROP_REG_USER_ID, regreq->asr_user);

	return (err);
}

/*
 * Registers the ASR client and saves the configuration if successful
 * to the ASR handle properties.
 * Returns non-zero if there is an error.
 */
int
asr_reg(asr_handle_t *ah, asr_regreq_t *request, nvlist_t **out_rsp)
{
	int err = 0;
	nvlist_t *rsp = NULL;

	if ((rsp = asr_nvl_alloc()) == NULL) {
		*out_rsp = NULL;
		(void) asr_set_errno(EASR_NOMEM);
		return (ASR_FAILURE);
	}

	if (ah->asr_tprt != NULL && ah->asr_tprt->asr_register_client != NULL)
		err = ah->asr_tprt->asr_register_client(ah, request, rsp);
	else
		err = asr_error(EASR_PROP_USAGE, "Transport not defined.");

	if (err != 0) {
		asr_nvl_free(rsp);
		rsp = NULL;
	} else {
		char *msg = asr_nvl_str(rsp, ASR_PROP_REG_MESSAGE);
		if (msg == NULL || msg[0] == '\0') {
			char time[80];
			asr_time(time, sizeof (time));
			(void) asr_nvl_add_strf(rsp, ASR_PROP_REG_MESSAGE,
			    "Registered %s", time);
		}
		err = asr_reg_save(ah, rsp);
	}
	*out_rsp = rsp;
	return (err);
}

/*
 * Deactivates and unregisteres ASR for this system.
 */
int
asr_unreg(asr_handle_t *ah)
{
	int err;
	char *regid = asr_get_regid(ah);
	char *mt = "";
	nvlist_t *cfg = ah->asr_cfg;

	if (regid == NULL)
		return (0); /* Already unreg'ed */

	if (ah->asr_tprt != NULL && ah->asr_tprt->asr_unregister_client != NULL)
		err = ah->asr_tprt->asr_unregister_client(ah);

	if (err == ASR_OK) {
		(void) asr_nvl_add_str(cfg, ASR_PROP_REG_CLIENT_ID, mt);
		(void) asr_nvl_add_str(cfg, ASR_PROP_REG_CODE, mt);
		(void) asr_nvl_add_str(cfg, ASR_PROP_REG_DOMAIN_ID, mt);
		(void) asr_nvl_add_str(cfg, ASR_PROP_REG_DOMAIN_NAME, mt);
		(void) asr_nvl_add_str(cfg, ASR_PROP_REG_MESSAGE, mt);
		(void) asr_nvl_add_str(cfg, ASR_PROP_REG_MSG_KEY, mt);
		(void) asr_nvl_add_str(cfg, ASR_PROP_REG_PUB_KEY, mt);
		(void) asr_nvl_add_str(cfg, ASR_PROP_REG_SYSTEM_ID, mt);
		(void) asr_nvl_add_str(cfg, ASR_PROP_REG_ASSET_ID, mt);
		(void) asr_nvl_add_str(cfg, ASR_PROP_REG_URL, mt);
		(void) asr_nvl_add_str(cfg, ASR_PROP_REG_USER_ID, mt);
	}
	return (err);
}

/*
 * Sends the given telemetry data.  If the message isn't already signed
 * then the signature will be created.
 * The output nvlist will have to be freed by the caller even if an error is
 * returned since error properties can be returned in out_rsp.
 */
int
asr_send_msg(asr_handle_t *ah, const asr_message_t *msg, nvlist_t **out_rsp)
{
	int err = 0;
	nvlist_t *rsp = NULL;

	if ((rsp = asr_nvl_alloc()) == NULL)
		return (ASR_FAILURE);

	if (ah->asr_tprt != NULL && ah->asr_tprt->asr_send_msg != NULL)
		err = ah->asr_tprt->asr_send_msg(ah, msg, rsp);

	if (out_rsp != NULL)
		*out_rsp = rsp;

	return (err);
}

/*
 * Creates an name value list containing ASR common message header properties.
 */
int
asr_msg_start(asr_handle_t *ah, asr_buf_t *buf)
{
	char timebuf[64];
	return (asr_msg_tstart(ah, buf, timebuf, sizeof (timebuf)));
}

char *
asr_get_schema(asr_handle_t *ah)
{
	return (asr_getprop_strd(
	    ah, ASR_PROP_SCHEMA_VERSION, ASR_SCHEMA_VERSION));
}

boolean_t
asr_use_schema_2_1(asr_handle_t *ah)
{
	boolean_t use21 = B_FALSE;
	char *schema = asr_get_schema(ah);
	if (strcmp(ASR_SCHEMA_VERSION_2_1, schema) == 0)
		use21 = B_TRUE;
	return (use21);
}

/*
 * Creates an name value list containing ASR common message header properties
 * and sets the time of the message in the supplied time buffer.
 */
int
asr_msg_tstart(asr_handle_t *ah, asr_buf_t *buf, char *timebuf, size_t tlen)
{
	int err = 0;
	uuid_t uuid;
	time_t now;
	struct tm *gmnow;
	char *asset_id, *site_id, *system_id, *product_name, *product_id;
	char uuidbuf[UUID_PRINTABLE_STRING_LENGTH];
	boolean_t use_schema_2_1 = asr_use_schema_2_1(ah);

	if ((asset_id = asr_get_assetid(ah)) == NULL)
		return (asr_error(EASR_PROP_NOPROP, "Property %s not set",
		    ASR_PROP_ASSET_ID));

	if ((system_id = asr_get_systemid(ah)) == NULL)
		return (asr_error(EASR_PROP_NOPROP,
		    "failed to get system serial number"));
	site_id = asr_get_siteid(ah);
	product_name = asr_get_sysprop(ah, ASR_PROP_PRODUCT_NAME);
	product_id = asr_get_sysprop(ah, ASR_PROP_PRODUCT_ID);
	if (product_name == NULL || product_id == NULL)
		return (asr_error(EASR_PROP_NOPROP,
		    "failed to get product information"));


	(void) strcpy(uuidbuf, "0");
	uuid_generate(uuid);
	uuid_unparse(uuid, uuidbuf);

	(void) time(&now);
	gmnow = gmtime(&now);
	(void) strftime(timebuf, tlen, "%FT%T", gmnow);

	err = asr_buf_append_str(buf,
	    "<?xml version='1.0' encoding='UTF-8'?>\n");
	err |= asr_buf_append_str(buf,
	    "<message xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance'\n");
	if (use_schema_2_1) {
		err |= asr_buf_append_str(buf,
		    "    xsi:noNamespaceSchemaLocation='message_21.xsd'>\n");
	} else {
		err |= asr_buf_append_str(buf,
		    "    xsi:noNamespaceSchemaLocation='message_20.xsd'>\n");
	}
	err |= asr_buf_append_xml_nv(buf, 1, "site-id", site_id);
	err |= asr_buf_append_xml_nv(buf, 1, "host-id", ah->asr_host_id);
	err |= asr_buf_append_xml_nvtoken(buf, 1, "message-uuid", uuidbuf);
	err |= asr_buf_append_xml_anv(buf, 1,
	    "timezone", "UTC", "message-time", timebuf);
	err |= asr_buf_append_xml_nvtoken(buf, 1, "system-id", system_id);
	if (use_schema_2_1) {
		char info[80];
		if (sysinfo(SI_HOSTNAME, info, sizeof (info)) > 0)
			err |= asr_buf_append_xml_nv(
			    buf, 1, "system-name", info);
		if (sysinfo(SI_RELEASE, info, sizeof (info)) > 0) {
			int ilen = strlen(info);
			int freelen = sizeof (info) - ilen;
			if (freelen > 1) {
				info[ilen++] = ' ';
				if (sysinfo(SI_VERSION,
				    info + ilen, freelen - 1) > 0)
					err |= asr_buf_append_xml_nv(
					    buf, 1, "system-version", info);
			}
		}
	}
	err |= asr_buf_append_xml_nvtoken(buf, 1, "asset-id", asset_id);
	err |= asr_buf_append_xml_nvtoken(buf, 1, "product-id", product_id);
	err |= asr_buf_append_xml_nv(buf, 1, "product-name", product_name);
	if (use_schema_2_1) {
		err |= asr_buf_append_xml_nv(
		    buf, 1, "schema-version", asr_get_schema(ah));
		err |= asr_buf_append_xml_nv(
		    buf, 1, "client-name", ASR_CLIENT_NAME);
		err |= asr_buf_append_xml_nv(
		    buf, 1, "client-version", ASR_CLIENT_VERSION);
	}
	return (err);
}

int
asr_msg_end(asr_buf_t *msg)
{
	return (asr_buf_append_xml_end(msg, 0, "message"));
}

/*
 * Creates an ASR activation message.
 */
int
asr_activate(asr_handle_t *ah, asr_message_t **out_msg)
{
	int err;
	asr_message_t *msg = NULL;
	int pad = 1;
	char *na = "NA";
	char *user = asr_getprop_strd(ah, ASR_PROP_REG_USER_ID,
	    ASR_ANONYMOUS_USER);
	asr_buf_t *buf = asr_buf_alloc(ASR_ACTIVATE_BUFSIZE);

	if (buf == NULL)
		return (asr_set_errno(EASR_NOMEM));

	err = asr_msg_start(ah, buf);
	err |= asr_buf_append_xml_elem(buf, pad, "monitoring-activation");
	pad++;
	err |= asr_buf_append_xml_elem(buf, pad, "activation-user");
	pad++;
	err |= asr_buf_append_xml_nv(buf, pad, "company", na);
	err |= asr_buf_append_xml_nv(buf, pad, "email", na);
	err |= asr_buf_append_xml_nv(buf, pad, "first-name", user);
	err |= asr_buf_append_xml_nv(buf, pad, "last-name", user);
	err |= asr_buf_append_xml_nv(buf, pad, "organization", na);
	err |= asr_buf_append_xml_nv(buf, pad, "phone", na);
	pad--;
	err |= asr_buf_append_xml_end(buf, pad, "activation-user");
	err |= asr_buf_append_xml_elem(buf, pad, "site-address");
	pad++;
	err |= asr_buf_append_xml_nv(buf, pad, "line", na);
	err |= asr_buf_append_xml_nv(buf, pad, "company", na);
	err |= asr_buf_append_xml_nv(buf, pad, "city", na);
	err |= asr_buf_append_xml_nv(buf, pad, "state", na);
	err |= asr_buf_append_xml_nv(buf, pad, "postal-code", na);
	err |= asr_buf_append_xml_nv(buf, pad, "country", na);
	pad--;
	err |= asr_buf_append_xml_end(buf, pad, "site-address");
	err |= asr_buf_append_xml_elem(buf, pad, "contact");
	pad++;
	err |= asr_buf_append_xml_nv(buf, pad, "company", na);
	err |= asr_buf_append_xml_nv(buf, pad, "email", na);
	err |= asr_buf_append_xml_nv(buf, pad, "first-name", na);
	err |= asr_buf_append_xml_nv(buf, pad, "last-name", na);
	err |= asr_buf_append_xml_nv(buf, pad, "phone", na);
	pad--;
	err |= asr_buf_append_xml_end(buf, pad, "contact");
	pad--;
	err |= asr_buf_append_xml_end(buf, pad, "monitoring-activation");
	err |= asr_msg_end(buf);

	if (err == 0) {
		if ((msg = asr_message_alloc(buf, ASR_MSG_ACTIVATE)) == NULL)
			err = EASR_NOMEM;
	} else {
		asr_buf_free(buf);
	}
	*out_msg = msg;
	return (err);
}

/*
 * Creates an ASR deactivation message.  This message gets sent if registration
 * is cleared and ASR should be turned off for the system.
 */
int
asr_deactivate(asr_handle_t *ah, asr_message_t **out_msg)
{
	int err;
	asr_message_t *msg = NULL;
	asr_buf_t *buf = asr_buf_alloc(1024);

	if (buf == NULL)
		return (asr_set_errno(EASR_NOMEM));

	err = asr_msg_start(ah, buf);
	err |= asr_buf_append_xml_nv(buf, 1, "monitoring-deactivation", "");
	err |= asr_msg_end(buf);

	if (err == 0) {
		msg = asr_message_alloc(buf, ASR_MSG_DEACTIVATE);
		if (msg == NULL) {
			err = EASR_NOMEM;
		}
	} else {
		asr_buf_free(buf);
	}
	*out_msg = msg;
	return (err);
}

/*
 * Creates and ASR heartbeat message.
 */
int
asr_heartbeat(asr_handle_t *ah, asr_message_t **out_msg)
{
	int err;
	asr_message_t *msg = NULL;
	char time[64];
	asr_buf_t *buf = asr_buf_alloc(1024);

	if (buf == NULL)
		return (asr_set_errno(EASR_NOMEM));

	err = asr_msg_tstart(ah, buf, time, sizeof (time));
	err |= asr_buf_append_xml_elem(buf, 1, "heartbeat");
	err |= asr_buf_append_xml_anv(buf, 2, "timezone", "UTC", "time", time);
	err |= asr_buf_append_xml_end(buf, 1, "heartbeat");
	err |= asr_msg_end(buf);

	if (err == 0) {
		if ((msg = asr_message_alloc(buf, ASR_MSG_HEARTBEAT)) == NULL)
			err = EASR_NOMEM;
	} else {
		asr_buf_free(buf);
	}
	*out_msg = msg;
	return (err);
}

/*
 * Creates an ASR test message that simulates a fault on the back end and
 * sends an email to the given address.
 */
int
asr_test(asr_handle_t *ah, char *email, asr_message_t **out_msg)
{
	int err = 0;
	asr_message_t *msg = NULL;
	int pad = 1;
	asr_buf_t *mailto, *buf;
	uuid_t uuid;
	char uuidbuf[UUID_PRINTABLE_STRING_LENGTH];
	char time[64];

	*out_msg = NULL;
	if (email == NULL || email[0] == '\0')
		return (asr_set_errno(EASR_PROP_USAGE));

	if ((mailto = asr_buf_alloc(8+strlen(email))) == NULL)
		return (ASR_FAILURE);
	if (asr_buf_append(mailto, "mailto:%s", email) != 0) {
		asr_buf_free(mailto);
		return (ASR_FAILURE);
	}

	(void) strcpy(uuidbuf, "0");
	uuid_generate(uuid);
	uuid_unparse(uuid, uuidbuf);

	if ((buf = asr_buf_alloc(1280)) == NULL)
		return (asr_set_errno(EASR_NOMEM));

	err |= asr_msg_tstart(ah, buf, time, sizeof (time));
	err |= asr_buf_append_xml_elem(buf, pad, "event");
	pad++;
	err |= asr_buf_append_xml_elem(buf, pad, "primary-event-information");
	pad++;
	err |= asr_buf_append_xml_nv(buf, pad, "message-id", "TESTCREATE");
	err |= asr_buf_append_xml_nv(buf, pad, "event-uuid", uuidbuf);
	err |= asr_buf_append_xml_anv(buf, pad,
	    "timezone", "UTC", "event-time", time);
	err |= asr_buf_append_xml_nv(buf, pad, "severity", "Minor");
	err |= asr_buf_append_xml_elem(buf, pad, "component");
	err |= asr_buf_append_xml_nv(buf, pad+1, "uncategorized", "");
	err |= asr_buf_append_xml_end(buf, pad, "component");
	err |= asr_buf_append_xml_nv(buf, pad, "summary", asr_buf_data(mailto));
	err |= asr_buf_append_xml_nv(buf, pad, "description",
	    "Test Message Used for Testing End to End Connection");
	err |= asr_buf_append_xml_nv(buf, pad, "required-action", "None");
	pad--;
	err |= asr_buf_append_xml_end(buf, pad, "primary-event-information");

	pad--;
	err |= asr_buf_append_xml_end(buf, pad, "event");
	err |= asr_msg_end(buf);

	if (err == 0) {
		if ((msg = asr_message_alloc(buf, ASR_MSG_TEST)) == NULL)
			err = EASR_NOMEM;
	} else {
		asr_buf_free(buf);
	}
	asr_buf_free(mailto);
	*out_msg = msg;
	return (err);
}

/*
 * Creates a message that can get the activation status of the devices for
 * ASR monitoring validation
 */
int
asr_status(asr_handle_t *ah, char *method, asr_message_t **out_msg)
{
	int err;
	int pad = 1;
	asr_message_t *msg = NULL;
	char *user, *serial, *host;
	asr_buf_t *buf = asr_buf_alloc(1024);

	if (buf == NULL)
		return (asr_set_errno(EASR_NOMEM));

	user = asr_getprop_strd(ah, ASR_PROP_REG_USER_ID, ASR_ANONYMOUS_USER);
	serial = asr_getprop_str(ah, ASR_PROP_REG_SYSTEM_ID);
	host = ah->asr_host_id;

	err = asr_msg_start(ah, buf);
	err |= asr_buf_append_xml_elem(buf, pad, "asr-status");
	pad++;
	err |= asr_buf_append_xml_nv(buf, pad, "soa_username", user);
	err |= asr_buf_append_xml_nv(buf, pad, "method", method);
	err |= asr_buf_append_xml_elem(buf, pad, "device");
	pad++;
	err |= asr_buf_append_xml_nv(buf, pad, "serial-number", serial);
	err |= asr_buf_append_xml_nv(buf, pad, "host-name", host);
	pad--;
	err |= asr_buf_append_xml_end(buf, pad, "device");
	pad--;
	err |= asr_buf_append_xml_end(buf, pad, "asr-status");
	err |= asr_msg_end(buf);

	if (err == 0) {
		if ((msg = asr_message_alloc(buf, ASR_MSG_STATUS)) == NULL)
			err = EASR_NOMEM;
	} else {
		asr_buf_free(buf);
	}
	*out_msg = msg;
	return (err);
}

/*
 * Creates a message that identifies a proactive change to the device state.
 * This may have an impact on how Sun views the device.
 * This is used for putting a device into maintenance mode and telling Sun that
 * a device will go offline (implying it will not send heartbeats for the time
 * being).
 */
int
asr_state_change(asr_handle_t *ah, boolean_t online, boolean_t maintenance,
    asr_message_t **out_msg)
{
	int err;
	int pad = 1;
	asr_message_t *msg = NULL;
	asr_buf_t *buf = asr_buf_alloc(1024);

	if (buf == NULL)
		return (asr_set_errno(EASR_NOMEM));

	err = asr_msg_start(ah, buf);
	err |= asr_buf_append_xml_elem(buf, pad, "state-change");
	pad++;
	err |= asr_buf_append_xml_nb(buf, pad, "online", online);
	err |= asr_buf_append_xml_nb(buf, pad, "maintenance", maintenance);
	pad--;
	err |= asr_buf_append_xml_end(buf, pad, "state-change");
	err |= asr_msg_end(buf);

	if (err == 0) {
		if ((msg = asr_message_alloc(buf, ASR_MSG_STATUS)) == NULL)
			err = EASR_NOMEM;
	} else {
		asr_buf_free(buf);
	}
	*out_msg = msg;
	return (err);
}

/*
 * Internal ASR logging. Logs debug messages, warnings and errors to the
 * configured ASR log file.
 */
static void
asr_log(asr_handle_t *ah, const char *level, const char *message)
{
	FILE *logfile = ah->asr_log;
	char timebuf[80];

	if (logfile == NULL)
		logfile = stderr;

	asr_time(timebuf, sizeof (timebuf));
	(void) fprintf(logfile, "[ %s %s ] %s\n", level, timebuf, message);
}

/*
 * Variable arg version of the internal ASR logger.
 */
/* PRINTFLIKE3 */
static int
asr_vlog(asr_handle_t *ah, char *level, char *fmt, va_list ap)
{
	int err = 0;
	char timebuf[80];

	if (ah == NULL || ah->asr_log == NULL)
		return (ASR_FAILURE);

	asr_time(timebuf, sizeof (timebuf));
	if (fprintf(ah->asr_log, "[ %s %s ] ", level, timebuf) < 0 ||
	    vfprintf(ah->asr_log, fmt, ap) < 0 ||
	    fprintf(ah->asr_log, "\n") < 0)
		err = -1;

	return (err);
}

/*
 * Logs the ASR error.  If there is no internal error state this function
 * does nothing.
 */
void
asr_log_err(asr_handle_t *ah)
{
	if (asr_get_errno())
		asr_log(ah, "ERROR", asr_errmsg());
}

/*
 * Sets the ASR errno and log the error.
 */
void
asr_log_errno(asr_handle_t *ah, asr_err_t err)
{
	(void) asr_set_errno(err);
	asr_log(ah, "ERROR", asr_errmsg());
}

/*
 * Logs the ASR error with the given errno and message.
 */
void
asr_log_error(asr_handle_t *ah, asr_err_t err, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void) asr_verror(err, fmt, ap);
	va_end(ap);
	asr_log(ah, "ERROR", asr_errmsg());
}

/*
 * Logs a warning to the ASR log.
 */
void
asr_log_warn(asr_handle_t *ah, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	(void) asr_vlog(ah, "WARNING", fmt, ap);
	va_end(ap);
}

/*
 * Logs an informational message to the ASR log.
 */
void
asr_log_info(asr_handle_t *ah, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	(void) asr_vlog(ah, "INFO", fmt, ap);
	va_end(ap);
}

/*
 * Logs a debug message to the ASR log.
 */
void
asr_log_debug(asr_handle_t *ah, char *fmt, ...)
{
	va_list ap;
	if (ah && ah->asr_debug) {
		va_start(ap, fmt);
		(void) asr_vlog(ah, "DEBUG", fmt, ap);
		va_end(ap);
	}
}

/*
 * Gets the current ASR debug level.
 */
boolean_t
asr_get_debug(asr_handle_t *ah)
{
	return (ah ? ah->asr_debug : B_FALSE);
}

/*
 * Sets the current ASR debug level.
 */
void
asr_set_debug(asr_handle_t *ah, boolean_t debug)
{
	if (ah != NULL) {
		(void) asr_setprop_str(
		    ah, ASR_PROP_DEBUG, debug == B_TRUE ?
		    ASR_VALUE_TRUE : ASR_VALUE_FALSE);
		ah->asr_debug = debug;
	}
}
