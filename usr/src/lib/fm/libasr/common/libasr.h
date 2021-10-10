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

#ifndef _LIBASR_H
#define	_LIBASR_H

#ifdef	__cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <sys/nvpair.h>
#include <sys/varargs.h>

/*
 * Consolidation Private interfaces for creating and sending ASR
 * telemetry messages.
 * Supports ASR message schema 2.1
 */

typedef struct asr_handle asr_handle_t;

/*
 * All ASR functions require that an handle first be initialized.
 * In general the library is not MT-safe but, it is safe within a single handle.
 * Multithreaded clients will have to synchronize access to the same ASR handle.
 * The initialization requires a configuration name that is either a service
 * FMRI to use to read configuration properties or a path to a file that
 * contains a stored ASR configuration file.  The configuration file is
 * normally used for debugging only.
 */
extern asr_handle_t *asr_hdl_init(char *);

/*
 * Cleans up all resources used by the ASR library for the given handle.
 */
extern void asr_hdl_destroy(asr_handle_t *);

/*
 * Cleans up all internal resources used by the ASR library after all
 * handles have been destroyed.
 */
extern void asr_cleanup();

/*
 * Each ASR handle has a list of configuration properties that store client
 * registration information as well as state and status of the ASR connection.
 * It is up to the caller to use the following functions to get or set
 * property values.
 * The property names used are defined by the ASR_PROP macro.
 */
extern int asr_setprop_str(asr_handle_t *, const char *, const char *);
extern int asr_setprop_strd(asr_handle_t *ah,
    const char *, const char *, const char *);
extern char *asr_getprop_str(asr_handle_t *, const char *);
extern char *asr_getprop_strd(asr_handle_t *, const char *, char *);
extern long asr_getprop_long(asr_handle_t *, const char *, long);
extern boolean_t asr_getprop_bool(asr_handle_t *, const char *, boolean_t);
extern char *asr_getprop_path(asr_handle_t *, const char *, const char *);
extern int asr_nvl_add_strf(nvlist_t *, const char *, const char *, ...);

/* Utility functions to get/save common ASR configuration properties. */
extern nvlist_t *asr_get_config(asr_handle_t *);
extern int asr_set_config_name(asr_handle_t *, const char *);
extern int asr_save_config(asr_handle_t *);

extern char *asr_get_assetid(asr_handle_t *ah);
extern long asr_get_http_timeout(asr_handle_t *ah);
extern char *asr_get_systemid(asr_handle_t *ah);
extern char *asr_get_siteid(asr_handle_t *ah);
extern char *asr_get_productid(asr_handle_t *ah);
extern size_t asr_get_keylen(asr_handle_t *ah);
extern char *asr_get_regid(asr_handle_t *ah);

extern void asr_set_logfile(asr_handle_t *ah, FILE *log);
extern FILE *asr_get_logfile(asr_handle_t *ah);

/* Debugging function to print out ASR handle configuration to file */
extern int asr_print_config(asr_handle_t *, FILE *);

#define	ASR_CLIENT_NAME		"asr-notify"
#define	ASR_CLIENT_VERSION	"1.0"

#define	ASR_PROP(p)	#p

/*
 * ASR Service configuration properties.
 */
#define	ASR_PROP_DEBUG		ASR_PROP(config/debug)
#define	ASR_PROP_ROOTDIR	ASR_PROP(config/rootdir)
#define	ASR_PROP_DATA_DIR	ASR_PROP(config/data_dir)
#define	ASR_PROP_KEYLEN		ASR_PROP(config/keylen)
#define	ASR_PROP_POLL		ASR_PROP(config/poll)

#define	ASR_PROP_SCHEMA_VERSION	ASR_PROP(config/schema_version)
#define	ASR_SCHEMA_VERSION_2_0	"2.0"
#define	ASR_SCHEMA_VERSION_2_1	"2.1"
#define	ASR_SCHEMA_VERSION	ASR_SCHEMA_VERSION_2_0

/* Auto-reg settings */
#define	ASR_PROP_AREG_USER		ASR_PROP(autoreg/user)
#define	ASR_PROP_AREG_PASS		ASR_PROP(autoreg/password)
#define	ASR_PROP_AREG_PROXY_HOST	ASR_PROP(autoreg/proxy-host)
#define	ASR_PROP_AREG_PROXY_USER	ASR_PROP(autoreg/proxy-user)
#define	ASR_PROP_AREG_PROXY_PASS	ASR_PROP(autoreg/proxy-password)

/* HTTP timeout in seconds used by libcurl */
#define	ASR_PROP_HTTP_TIMEOUT	ASR_PROP(config/http_timeout)
#define	ASR_PROP_HTTPS_VERIFY	ASR_PROP(config/https_verify)

/* Internet connection HTTP proxy properties */
#define	ASR_PROP_PROXY_HOST	ASR_PROP(config/proxy_host)
#define	ASR_PROP_PROXY_PORT	ASR_PROP(config/proxy_port)
#define	ASR_PROP_PROXY_TYPE	ASR_PROP(config/proxy_type)
#define	ASR_PROP_PROXY_USER	ASR_PROP(reg/proxy_user)
#define	ASR_PROP_PROXY_PASS	ASR_PROP(reg/proxy_pass)

/* URL for remote transport connection */
#define	ASR_PROP_TRANSPORT	ASR_PROP(config/transport)
#define	ASR_PROP_DEST_URL	ASR_PROP(config/endpoint)
#define	ASR_PROP_BETA_URL	ASR_PROP(config/beta_url)
#define	ASR_PROP_BETA		ASR_PROP(config/beta)
#define	ASR_PROP_URL		ASR_PROP(config/url)

/* Message creation intervals in hours */
#define	ASR_PROP_AUDIT_INTERVAL	ASR_PROP(config/audit_interval)
#define	ASR_PROP_HB_INTERVAL	ASR_PROP(config/heartbeat_interval)

/* System Identity properties */
#define	ASR_PROP_ASSET_ID	ASR_PROP(config/asset_id)
#define	ASR_PROP_PRODUCT_ID	ASR_PROP(config/product_id)
#define	ASR_PROP_PRODUCT_NAME	ASR_PROP(config/product_name)
#define	ASR_PROP_SITE_ID	ASR_PROP(config/site_id)
#define	ASR_PROP_SYSTEM_ID	ASR_PROP(config/system_id)

/*
 * Audit data configuration.  Flags to control sending audit
 * and configuration of custom payload content.
 */
#define	ASR_PROP_AUDIT_SEND		ASR_PROP(config/audit_send)
#define	ASR_PROP_AUDIT_SEND_FRU		ASR_PROP(config/audit_send_fru)
#define	ASR_PROP_AUDIT_SEND_SVC		ASR_PROP(config/audit_send_svc)
#define	ASR_PROP_AUDIT_SEND_PKG		ASR_PROP(config/audit_send_pkg)
#define	ASR_PROP_AUDIT_SEND_PAYLOAD	ASR_PROP(config/audit_send_payload)
#define	ASR_PROP_AUDIT_PAYLOAD_CMD	ASR_PROP(config/audit_payload_cmd)
#define	ASR_PROP_AUDIT_PAYLOAD_NAME	ASR_PROP(config/audit_payload_name)

/* AES encrypted property files */
#define	ASR_PROP_REG_KEY_FILE	ASR_PROP(config/index_file)
#define	ASR_PROP_REG_DATA_FILE	ASR_PROP(config/data_file)

/*
 * Client Registration Properties
 */
#define	ASR_PROP_REG_CODE		ASR_PROP(reg/code)
#define	ASR_PROP_REG_CLIENT_ID		ASR_PROP(reg/clreg_id)
#define	ASR_PROP_REG_DOMAIN_ID		ASR_PROP(reg/domain_id)
#define	ASR_PROP_REG_DOMAIN_NAME	ASR_PROP(reg/domain_name)
#define	ASR_PROP_REG_MESSAGE		ASR_PROP(reg/message)
#define	ASR_PROP_REG_MSG_KEY		ASR_PROP(reg/msg_key)
#define	ASR_PROP_REG_PUB_KEY		ASR_PROP(reg/pub_key)
#define	ASR_PROP_REG_SYSTEM_ID		ASR_PROP(reg/system_id)
#define	ASR_PROP_REG_ASSET_ID		ASR_PROP(reg/asset_id)
#define	ASR_PROP_REG_URL		ASR_PROP(reg/url)
#define	ASR_PROP_REG_USER_ID		ASR_PROP(reg/user_id)


/* Proxy Type values */
#define	ASR_PROXY_TYPE_HTTP	"http"
#define	ASR_PROXY_TYPE_SOCKS4	"socks4"
#define	ASR_PROXY_TYPE_SOCKS5	"socks5"
#define	ASR_PROXY_DEFAULT_TYPE	"http"
#define	ASR_PROXY_DEFAULT_PORT	"80"

/* Default ASR Time intervals */
#define	ASR_TIME_INTERVAL_SECS(hrs)	((hrs) * 3600)
#define	ASR_TIME_INTERVAL_MILLS(hrs)	((hrs) * 3600 * 1000)
#define	ASR_AUDIT_INTERVAL_DEFAULT	168
#define	ASR_HB_INTERVAL_DEFAULT		24

/* Boolean property values */
#define	ASR_VALUE_TRUE  "true"
#define	ASR_VALUE_FALSE "false"

/*
 * Registration request used to set user fields for
 * ASR registration.
 */
typedef struct asr_regreq asr_regreq_t;

extern asr_regreq_t *asr_regreq_init();
extern void asr_regreq_destroy(asr_regreq_t *);
extern int asr_regreq_set_user(asr_regreq_t *regreq, const char *user);
extern int asr_regreq_set_password(asr_regreq_t *regreq, const char *password);
extern char *asr_regreq_get_user(const asr_regreq_t *);
extern char *asr_regreq_get_password(const asr_regreq_t *);
extern int asr_create_proxy_url(asr_handle_t *, char **url, char **type);

/* Attempts an ASR client registration and activation. */
extern int asr_reg(asr_handle_t *, asr_regreq_t *, nvlist_t **);

/* Clears any previous ASR registration. */
extern int asr_unreg(asr_handle_t *);


/* ASR Message Types supported by library */
typedef enum {
	ASR_MSG_ACTIVATE,
	ASR_MSG_AUDIT,
	ASR_MSG_DEACTIVATE,
	ASR_MSG_HEARTBEAT,
	ASR_MSG_EVENT_UPDATE,
	ASR_MSG_FAULT,
	ASR_MSG_STATE_CHANGE,
	ASR_MSG_STATUS,
	ASR_MSG_TEST
} asr_msgtype_t;

typedef struct asr_message {
	char *asr_msg_data;
	size_t asr_msg_len;
	asr_msgtype_t asr_msg_type;
} asr_message_t;


/* ASR Message creation functions. */
extern int asr_activate(asr_handle_t *, asr_message_t **);
extern int asr_deactivate(asr_handle_t *, asr_message_t **);
extern int asr_heartbeat(asr_handle_t *, asr_message_t **);
extern int asr_audit(asr_handle_t *, asr_message_t **);
extern int asr_test(asr_handle_t *, char *, asr_message_t **);
extern int asr_fault(asr_handle_t *, nvlist_t *, asr_message_t **);

/*
 * Sends the message over the registered ASR transport
 * Reply data from the transport is returned in the nvlist
 * Returns ASR_FAILURE is there is an error and ASR_OK if message was sent.
 */
#define	ASR_MSG_RSP_CODE 	"CODE"		/* Transport error code */
#define	ASR_MSG_RSP_MESSAGE	"MESSAGE"	/* Transport error message */
#define	ASR_MSG_RSP_RETRY	"RETRY"		/* Trnsport retry flag */
extern int asr_send_msg(asr_handle_t *, const asr_message_t *, nvlist_t **);

/* Releases resources used by a created ASR Message. */
extern void asr_free_msg(asr_message_t *);

/* Define and set a transport */
extern int asr_set_transport(asr_handle_t *ah, char *,
    int (*asr_register_client)(
    asr_handle_t *ah, const asr_regreq_t *req, nvlist_t *rsp),
    int (*asr_unregister_client)(asr_handle_t *ah),
    int (*asr_send_msg)(
    asr_handle_t *ah, const asr_message_t *msg, nvlist_t *rsp));

/* Debugging and error messages */
typedef enum asr_err {
	EASR_NONE,		/* no error */
	EASR_NOMEM,		/* no memory */
	EASR_USAGE,		/* option processing error */
	EASR_UNKNOWN,		/* error of unknown origin */
	EASR_UNSUPPORTED,	/* Unsupported operation */

	EASR_ZEROSIZE,		/* caller attempted zero-length allocation */
	EASR_OVERSIZE,		/* caller attempted to allocate to much data */
	EASR_NULLDATA,		/* caller attempted to operate on NULL data */
	EASR_NULLFREE,		/* caller attempted to free NULL handle */
	EASR_MD_NODATA,		/* failed to open metadata */
	EASR_PROP_USAGE,	/* property keyword syntax error */
	EASR_PROP_NOPROP,	/* no such property name */
	EASR_PROP_SET,		/* unable to set property */

	EASR_SC,		/* error during phone home */
	EASR_SC_RESOLV_PROXY,	/* error resolving proxy */
	EASR_SC_RESOLV_HOST,	/* error resolving host */
	EASR_SC_CONN,		/* error connecting to support service */
	EASR_SC_AUTH,		/* invalid SOA username/password */
	EASR_SC_REG,		/* transport connection not registered */

	EASR_FM,		/* error in fm module */
	EASR_TOPO,		/* failed to determine system topology */
	EASR_SCF,		/* Error with SCF library. */
	EASR_NVLIST,		/* Error in nvlist module */

	EASR_SSL_LIBSSL,	/* SSL Error */
	EASR_SYSTEM,		/* System call failed */
	EASR_MAX		/* maximum errno value */
} asr_err_t;

extern const char *asr_errmsg();
extern boolean_t asr_get_debug(asr_handle_t *);
extern void asr_set_debug(asr_handle_t *, boolean_t);

extern asr_err_t asr_get_errno();
extern int asr_set_errno(asr_err_t);
extern int asr_error(asr_err_t err, const char *format, ...);
extern int asr_verror(asr_err_t err, const char *format, va_list ap);

#define	ASR_OK		0	/* Function returned successfully */
#define	ASR_FAILURE	-1	/* Function failed to execute (see asr_errno) */

/* Logging Functions */
extern void asr_log_error(asr_handle_t *ah, asr_err_t err, char *format, ...);
extern void asr_log_info(asr_handle_t *ah, char *format, ...);
extern void asr_log_warn(asr_handle_t *ah, char *format, ...);
extern void asr_log_debug(asr_handle_t *ah, char *format, ...);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBASR_H */
