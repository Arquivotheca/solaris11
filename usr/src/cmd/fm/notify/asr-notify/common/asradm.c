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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <libscf.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/nvpair.h>
#include <sys/utsname.h>
#include <sys/fm/protocol.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

#include <fm/fmd_adm.h>
#include <fm/fmd_msg.h>
#include <fm/libtopo.h>
#include <fm/fmd_log.h>

#include <fm/libasr.h>
#include "asr_notify.h"
#include "asr_notify_scf.h"
#include "asr_dts.h"
#include "asr_scrk.h"

static boolean_t debug = B_FALSE;	/* Toggle debugging */
static boolean_t opt_transmit = B_TRUE; /* Disable transmission of msgs */
static boolean_t opt_direct = B_FALSE;  /* Direct internet connection */
static boolean_t opt_beta = B_FALSE;	/* Connect to configured beta site */
static char *asr_conf = PH_FMRI;	/* Default service configuration FMRI */
static char *opt_user = NULL;		/* SSO user name */
static char *opt_passfile = NULL;	/* SSO password file or '-' for stdin */
static char *opt_endpoint = NULL;	/* Transport endpoint URL */
static char *opt_proxy_host = NULL;	/* HTTP Proxy host name */
static char *opt_proxy_user = NULL;	/* HTTP Proxy user name */
static char *opt_proxy_pass = NULL;	/* HTTP proxy password */
static char *opt_uuid = NULL;		/* fault uuid filter */

#define	EMPTY ""
#define	MAX_PASSLEN 256
static char password[MAX_PASSLEN];

static void
asradm_usage()
{
	(void) fprintf(stderr,
	    "asradm - ASR Notify Administration Usage\n"
	    "\tlist\n"
	    "\tsend [-n] [activate|audit|deactivate|fault|heartbeat|test]\n"
	    "\tregister [-u user] [-p password-file] [-e endpoint-url]\n"
	    "\tset-proxy <-i | -h host[:port]> [-u user] [-p password-file]\n"
	    "\tunregister\n");
}

static int
asradm_parse_opts(int argc, char **argv, char *opts)
{
	int c, err = 0;
	while (err == 0 && (c = getopt(argc, argv, opts)) != -1) {
		switch (c) {
		case 'c':
		{
			asr_conf = optarg;
			break;
		}
		case 'e':
		{
			opt_endpoint = optarg;
			break;
		}
		case 'n':
		{
			opt_transmit = B_FALSE;
			break;
		}
		case 'u':
		{
			opt_user = optarg;
			break;
		}
		case 'p':
		{
			opt_passfile = optarg;
			break;
		}
		case 'U':
		{
			opt_proxy_user = optarg;
			break;
		}
		case 'P':
		{
			opt_proxy_pass = optarg;
			break;
		}
		case 'H':
		{
			opt_proxy_host = optarg;
			break;
		}
		case 'h':
		{
			opt_proxy_host = optarg;
			break;
		}
		case 'B':
		{
			opt_beta = B_TRUE;
			break;
		}
		case 'i':
		{
			opt_direct = B_TRUE;
			break;
		}
		case 'd':
		{
			debug = 1;
			break;
		}
		case ':':
		{
			(void) fprintf(stderr,
			    "Missing value for option (%c)\n", optopt);
			err++;
			break;
		}
		case '?':
		default:
		{
			(void) fprintf(stderr,
			    "Unrecognized option (%c)\n", optopt);
			err++;
		}
		}
	}
	return (err);
}

/*
 * Parses a service config command line override option name=value and adds
 * it to the nvlist
 */
static int
asradm_addopt(nvlist_t *nv, char *opt)
{
	char *sep = strstr(opt, "=");
	int err = 0;

	if (sep != NULL) {
		char *v = sep + 1;
		int len = sep - opt;
		char *n = strndup(opt, len);
		if (n == NULL)
			return (PH_FAILURE);
		err = nvlist_add_string(nv, n, v);
		free(n);
	} else {
		return (PH_FAILURE);
	}
	return (err);
}

/*
 * Determines if process has permissions to save registration properties
 */
static int
ph_can_save_reg(asr_handle_t *asrh)
{
	char *datadir, *rootdir;
	int rd = -1;
	uid_t user = geteuid();
	int result;

	if (user == 0)
		return (ASR_OK);
	rootdir = asr_getprop_strd(
	    asrh, ASR_PROP_ROOTDIR, PH_DEFAULT_DATADIR);
	if ((rd = open(rootdir, O_SEARCH)) == -1)
		return (PH_FAILURE);
	datadir = asr_getprop_strd(
	    asrh, ASR_PROP_DATA_DIR, PH_DEFAULT_DATADIR);
	result = faccessat(rd, datadir, W_OK, 0);
	(void) close(rd);
	return (result);
}

/*
 * Strips ending whitespace from a string buffer by adjusting string
 * termination charactor.
 */
static void
asradm_strip(char *input)
{
	int i, len;
	if (input == NULL)
		return;
	len = strlen(input);
	if (len > 0) {
		for (i = len - 1; i >= 0; i--)
			if (input[i] != ' ' && input[i] != '\n' &&
			    input[i] != '\t')
				break;
		input[i + 1] = '\0';
	}
}

/*
 * Erases the memory contents of a password to comply with security guidlines.
 */
static void
asradm_erase_password(char *pass, int len)
{
	int i;
	int d = '\0';
	(void) memset(pass, d, len);
	for (i = 0; i < len; i++)
		if (pass[i] != d)
			break;
}

static void
asradm_erase_pass_file()
{
	asradm_erase_password(password, MAX_PASSLEN);
}

/*
 * Reads in a password from a file.  All text from the first line
 * is read and returned into the global password variable.
 */
static int
asradm_get_pass_file(char *path)
{
	FILE *cred;
	int usestdin;
	password[0] = '\0';

	if ((usestdin = strcmp(path, "-")) == 0) {
		cred = stdin;
	} else if ((cred = fopen(path, "r")) == NULL)
		return (PH_FAILURE);

	if (NULL == fgets(password, MAX_PASSLEN, cred)) {
		(void) fclose(cred);
		return (PH_FAILURE);
	}
	asradm_strip(password);
	if (usestdin != 0)
		(void) fclose(cred);
	return (ASR_OK);
}

static int
asradm_chk_proxy(char *host, char *port)
{
	struct addrinfo hints;
	struct addrinfo *res;
	int err;

	bzero(&hints, sizeof (hints));
	hints.ai_flags = AI_ADDRCONFIG;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	err = getaddrinfo(host, port, &hints, &res);
	if (err == 0)
		freeaddrinfo(res);
	else
		(void) fprintf(stderr,
		    "failed to lookup proxy host %s:%s (%s)\n",
		    host, port, gai_strerror(err));
	return (err);
}

/*
 * Save proxy settings.
 */
static int
asradm_save_proxy(char *host, char *port, char *user, char *pass)
{
	if (user != NULL) {
		asr_handle_t *asrh = NULL;
		if (pass == NULL)
			pass = "";
		if ((asrh = asr_hdl_init(asr_conf)) == NULL) {
			(void) fprintf(stderr, "Unable to initialize ASR.\n");
			return (PH_FAILURE);
		}
		if (ph_read_reg(asrh) != 0) {
			(void) fprintf(stderr,
			    "Unable read registration properties.\n");
			asr_hdl_destroy(asrh);
			return (PH_FAILURE);
		}
		if (asr_setprop_str(asrh, ASR_PROP_PROXY_USER, user) != 0 ||
		    asr_setprop_str(asrh, ASR_PROP_PROXY_PASS, pass) != 0 ||
		    ph_save_reg(asrh) != 0) {
			(void) fprintf(stderr,
			    "Failed to write proxy properties. (%s)\n",
			    strerror(errno));
			asr_hdl_destroy(asrh);
			return (PH_FAILURE);
		}
		asr_hdl_destroy(asrh);
	}

	if (ph_scf_set_string(asr_conf, ASR_PROP_PROXY_HOST, host) != 0 ||
	    ph_scf_set_string(asr_conf, ASR_PROP_PROXY_PORT, port) != 0) {
		(void) fprintf(stderr,
		    "Failed to set proxy properties.\n");
		return (PH_FAILURE);
	}
	return (PH_OK);
}

/*
 * Setup ASR internet connection settings (HTTP proxy).
 */
static int
asradm_connect_cmd(int argc, char **argv)
{
	int err = 0;

	if (asradm_parse_opts(argc, argv, ":ih:u:p:c:d") != 0 ||
	    optind != argc) {
		asradm_usage();
		return (2);
	}
	if (opt_direct) {
		err = asradm_save_proxy(NULL, NULL, EMPTY, EMPTY);
	} else if (opt_proxy_host == NULL) {
		asradm_usage();
		return (2);
	} else {
		char *sep = strstr(opt_proxy_host, ":");
		char *port;
		char *host;
		char *user = EMPTY;
		char *pass = EMPTY;

		if (sep == NULL) {
			port = ASR_PROXY_DEFAULT_PORT;
			host = strdup(opt_proxy_host);
		} else {
			int p, len = sep - opt_proxy_host;
			port = sep + 1;
			p = atoi(port);
			if (p < 1 || p > USHRT_MAX) {
				(void) fprintf(stderr,
				    "Port (%s) must be number [1-%d]\n",
				    port, USHRT_MAX);
				return (PH_FAILURE);
			}
			host = strndup(opt_proxy_host, len);
		}
		if (host == NULL) {
			(void) fprintf(stderr, "Failed to get host");
			return (PH_FAILURE);
		}
		if (opt_user != NULL) {
			user = opt_user;
			if (opt_passfile != NULL) {
				if ((err = asradm_get_pass_file(
				    opt_passfile)) == 0)
					pass = password;
				else
					(void) fprintf(stderr,
					    "Failed to get password.");
			} else {
				pass = getpassphrase(
				    "Enter proxy password: ");
			}
		}
		if (err == 0 && (err = asradm_chk_proxy(host, port)) == 0)
			err = asradm_save_proxy(host, port, user, pass);
		if (opt_passfile != NULL)
			asradm_erase_pass_file();
		free(host);
	}
	(void) smf_refresh_instance(asr_conf);
	return (err);
}

/*
 * Pretty print command for asradm list
 */
static boolean_t
asradm_printf(char *name, char *value)
{
	if (name != NULL && value != NULL && value[0] != '\0') {
		(void) fprintf(stdout, "%-20s %s\n", name, value);
		return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Prints an ASR property to stdout
 */
static boolean_t
asradm_pval(asr_handle_t *asrh, char *name, char *prop)
{
	return (asradm_printf(name, asr_getprop_str(asrh, prop)));
}

/*
 * Lists ASR registration properties
 */
static int
asradm_list_cmd(int argc, char **argv)
{
	int err = 0;
	asr_handle_t *asrh = NULL;
	char *status;

	if (asradm_parse_opts(argc, argv, ":c:") != 0) {
		asradm_usage();
		return (2);
	}

	if ((asrh = asr_hdl_init(asr_conf)) == NULL) {
		(void) fprintf(stderr,
		    "Unable to initialize.  Check permissions\n");
		return (1);
	}
	if (ph_read_reg(asrh) != 0) {
		(void) fprintf(stderr,
		    "Unable read registration properties. "
		    " Check permissions\n");
		return (1);
	}

	if (asr_get_regid(asrh) == NULL)
		status = "Not Registered";
	else
		status = asr_getprop_strd(
		    asrh, ASR_PROP_REG_MESSAGE, "Registered");

	(void) asradm_printf("PROPERTY", "VALUE");
	(void) asradm_printf("Status", status);
	(void) asradm_pval(asrh, "Product", ASR_PROP_PRODUCT_NAME);
	(void) asradm_pval(asrh, "System Id", ASR_PROP_REG_SYSTEM_ID);
	(void) asradm_pval(asrh, "Asset Id", ASR_PROP_REG_ASSET_ID);
	(void) asradm_pval(asrh, "User", ASR_PROP_REG_USER_ID);
	(void) asradm_pval(asrh, "Endpoint URL", ASR_PROP_REG_URL);

	if (asradm_pval(asrh, "Proxy Host", ASR_PROP_PROXY_HOST)) {
		(void) asradm_pval(asrh, "Proxy Port", ASR_PROP_PROXY_PORT);
		(void) asradm_pval(asrh, "Proxy User", ASR_PROP_PROXY_USER);
	}

finally:
	asr_hdl_destroy(asrh);
	return (err);
}

/*
 * Creates and transmits a message using an initialized ASR handle
 */
static int
do_asr_msg(asr_handle_t *asrh,
    int (*func)(asr_handle_t *asrh, asr_message_t **msg, void *), void *data)
{
	int err = 0;
	asr_message_t *msg = NULL;
	err = (*func)(asrh, &msg, data);
	if (err == 0) {
		nvlist_t *rsp = NULL;
		err = asr_send_msg(asrh, msg, &rsp);
		if (debug)
			(void) fprintf(stdout, "%s", msg->asr_msg_data);
		if (err != 0)
			(void) fprintf(stderr, "failed to send message (%s)\n",
			    asr_errmsg());
		if (rsp != NULL)
			nvlist_free(rsp);
	} else {
		(void) fprintf(stderr, "%s\n", asr_errmsg());
	}
	if (msg != NULL)
		asr_free_msg(msg);

	return (err);
}

/*
 * Creates an ASR message and sends it over the registered transport.
 */
static int
do_asr(int (*func)(asr_handle_t *asrh, asr_message_t **msg, void *), void *data)
{
	int err = 0;
	asr_handle_t *asrh = NULL;
	asr_message_t *msg = NULL;
	char *client_id;

	if ((asrh = asr_hdl_init(asr_conf)) == NULL) {
		(void) fprintf(stderr, "Unable to initialize ASR.\n");
		return (3);
	}
	if (opt_transmit == B_TRUE && ph_read_reg(asrh)) {
		(void) fprintf(stderr,
		    "Unable read registration properties.\n");
		return (3);
	}

	asr_set_debug(asrh, debug);
	if (debug)
		asr_set_logfile(asrh, stderr);

	if (opt_transmit == B_TRUE) {
		client_id = asr_getprop_str(asrh, ASR_PROP_REG_CLIENT_ID);
		if (client_id == NULL || client_id[0] == '\0') {
			asr_hdl_destroy(asrh);
			(void) asr_error(EASR_SC_REG,
			    "ASR is not registered so event was not sent.");
			return (PH_FAILURE);
		}
	}

	if (ph_tprt_init(asrh) != 0) {
		asr_hdl_destroy(asrh);
		return (PH_FAILURE);
	}

	err = (*func)(asrh, &msg, data);

	if (err == 0) {
		if (opt_transmit) {
			nvlist_t *rsp = NULL;
			err = asr_send_msg(asrh, msg, &rsp);
			if (debug)
				(void) fprintf(stdout, "%s", msg->asr_msg_data);
			if (err != 0)
				(void) fprintf(stderr,
				    "failed to send message (%s)\n",
				    asr_errmsg());
			if (rsp != NULL)
				nvlist_free(rsp);
		} else {
			(void) fprintf(stdout, "%s", msg->asr_msg_data);
		}
	} else {
		(void) fprintf(stderr, "%s\n", asr_errmsg());
	}
	if (msg != NULL)
		asr_free_msg(msg);

	(void) asr_hdl_destroy(asrh);
	return (err);
}

/* ARGSUSED */
static int
do_asr_activate(asr_handle_t *asrh, asr_message_t **msg, void *data)
{
	return (asr_activate(asrh, msg));
}

/* ARGSUSED */
static int
do_asr_deactivate(asr_handle_t *asrh, asr_message_t **msg, void *data)
{
	return (asr_deactivate(asrh, msg));
}

/* ARGSUSED */
static int
do_asr_heartbeat(asr_handle_t *asrh, asr_message_t **msg, void *data)
{
	return (asr_heartbeat(asrh, msg));
}

/* ARGSUSED */
static int
do_asr_audit(asr_handle_t *asrh, asr_message_t **msg, void *data)
{
	return (asr_audit(asrh, msg));
}

static int
do_asr_test(asr_handle_t *asrh, asr_message_t **msg, void *data)
{
	return (asr_test(asrh, (char *)data, msg));
}

static int
do_asr_fault(asr_handle_t *asrh, asr_message_t **msg, void *data)
{
	return (asr_fault(asrh, (nvlist_t *)data, msg));
}

/*
 * Sends an ASR test message.
 */
static int
asradm_test_cmd(int argc, char **argv, int opti)
{
	if (asradm_parse_opts(argc, argv, ":c:d") != 0) {
		asradm_usage();
		return (2);
	}

	if (argc != opti + 1) {
		(void) fprintf(stderr,
		    "usage error: Missing email address\n"
		    "asradm send test <email address>\n");
		return (2);
	}
	return (do_asr(do_asr_test, argv[opti]));
}

/*
 * Callback function to handle an FM event.  Skips all resolved cases
 * and skips faults that don't match the optional opt_uuid filter.
 */
/* ARGSUSED */
static int
dfault_rec(const fmd_adm_caseinfo_t *acp, void *arg)
{
	int err = 0;
	nvlist_t *event = acp->aci_event;
	uint32_t case_state = FMD_SUSPECT_CASE_SOLVED;

	if (opt_uuid != NULL) {
		char *uuid = "-";
		(void) nvlist_lookup_string(event, FM_SUSPECT_UUID, &uuid);
		if (strcmp(opt_uuid, uuid) != 0)
			return (0);
	}
	(void) nvlist_lookup_uint32(event, FM_SUSPECT_CASE_STATE, &case_state);
	if (case_state != FMD_SUSPECT_CASE_RESOLVED)
		err = do_asr_msg((asr_handle_t *)arg, do_asr_fault, event);
	return (err);
}

/*
 * Creates a fault using name=value command line options.
 *   id=Event id
 *   code=Event code
 *   description=Event description text
 *   severity=Major,Minor,Critical,Down
 *   reason=Reason for event
 *   time=Time of event
 *   fru.name=Label or name of FRU
 *   fru.id=FRU device ID
 *   fru.serial=FRU serial number
 *   fru.part=FRU part replacement number
 *   fru.revision=Firmware revision of FRU
 *   payload=Optional additional information
 */
static int
asradm_create_fault_cmd(int argc, char **argv, int opti)
{
	int i;
	nvlist_t *props = NULL;
	int err;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		return (1);

	for (i = opti; i < argc; i++) {
		if ((err = asradm_addopt(props, argv[i])) != 0) {
			(void) fprintf(stderr,
			    "Unable to parse %s\n", argv[i]);
			goto finally;
		}
	}
	err = do_asr(do_asr_fault, props);

finally:
	if (props != NULL)
		nvlist_free(props);
	return (err);
}

static int
asradm_send_fault()
{
	int err = PH_OK;
	fmd_adm_t *adm = NULL;

	adm = fmd_adm_open(NULL, FMD_ADM_PROGRAM, FMD_ADM_VERSION);

	if (adm == NULL) {
		(void) fprintf(stderr, "Unable to open FMD. Is it running?\n");
		err = PH_FAILURE;
	} else {
		asr_handle_t *asrh;
		if ((asrh = asr_hdl_init(asr_conf)) == NULL) {
			(void) fprintf(stderr,
			    "Unable to initialize ASR. (%s)\n", asr_errmsg());
			fmd_adm_close(adm);
			return (PH_FAILURE);
		}
		if ((err = fmd_adm_case_iter(adm, NULL, dfault_rec, asrh)) != 0)
			(void) fprintf(stderr, "FMD error: %s\n",
			    fmd_adm_errmsg(adm));
		asr_hdl_destroy(asrh);
		fmd_adm_close(adm);
	}
	return (err);
}

/*
 * Sends an ASR fault message from the FMD fault database
 */
static int
asradm_fault_cmd(int argc, char **argv, int opti)
{
	opt_uuid = (argc > opti) ? argv[opti] : NULL;
	if (opt_uuid != NULL && strstr(opt_uuid, "=") != NULL)
		return (asradm_create_fault_cmd(argc, argv, opti));

	return (asradm_send_fault());
}

/*
 * Manually sends an ASR message
 */
static int
asradm_send_cmd(int argc, char **argv)
{
	int err = 0;
	char *msg = NULL;

	if (asradm_parse_opts(argc, argv, ":c:nd")) {
		asradm_usage();
		return (2);
	}

	if (argc < optind + 1) {
		(void) fprintf(stderr, "usage error: Missing message type\n");
		asradm_usage();
		return (2);
	}
	msg = argv[optind];
	optind++;

	if (strcmp("test", msg) == 0)
		err = asradm_test_cmd(argc, argv, optind);
	else if (strcmp("activate", msg) == 0)
		err = do_asr(do_asr_activate, NULL);
	else if (strcmp("deactivate", msg) == 0)
		err = do_asr(do_asr_deactivate, NULL);
	else if (strcmp("audit", msg) == 0)
		err = do_asr(do_asr_audit, NULL);
	else if (strcmp("heartbeat", msg) == 0)
		err = do_asr(do_asr_heartbeat, NULL);
	else if (strcmp("fault", msg) == 0)
		err = asradm_fault_cmd(argc, argv, optind);
	else {
		err = 1;
		(void) fprintf(stderr, "Unknown event type (%s)\n", msg);
	}
	return (err);
}

/*
 * ASR registration command.  Registers with transport and activates ASR.
 */
static int
asradm_register_cmd(int argc, char **argv)
{
	int i;
	int err = 0;
	asr_handle_t *asrh = NULL;
	asr_regreq_t *regreq = NULL;
	nvlist_t *rsp = NULL;
	int regres;
	char userbuf[128];

	if ((regreq = asr_regreq_init()) == NULL) {
		(void) fprintf(stderr, "Failed to allocate configuration.\n");
		return (1);
	}

	if (asradm_parse_opts(argc, argv, ":c:u:H:P:U:p:e:Bd")) {
		asradm_usage();
		return (2);
	}

	if ((asrh = asr_hdl_init(asr_conf)) == NULL) {
		(void) fprintf(stderr, "Failed to initialize ASR handle.\n");
		err = 1;
		goto finally;
	}

	if (ph_can_save_reg(asrh) != 0 || ph_read_reg(asrh) != 0) {
		(void) fprintf(stderr, "User not allowed to register.\n");
		err = 1;
		goto finally;
	}

	if (opt_user == NULL) {
		if (fprintf(stdout, "Enter Oracle SSO User Name: ") < 0)
			goto finally;
		if (fflush(stdout) != 0)
			goto finally;
		opt_user = fgets(userbuf, sizeof (userbuf), stdin);
		asradm_strip(userbuf);
		if ((err = asr_regreq_set_user(regreq, opt_user)) != 0) {
			(void) fprintf(stderr, "Error setting user name.\n");
			goto finally;
		}
	} else {
		err = asr_regreq_set_user(regreq, opt_user);
	}
	if (opt_passfile == NULL) {
		char *pass = getpassphrase("Enter password: ");
		if ((err = asr_regreq_set_password(regreq, pass)) != 0) {
			(void) fprintf(stderr,
			    "Error setting user password.\n");
			goto finally;
		}
	} else {
		if ((err = asradm_get_pass_file(opt_passfile)) == 0) {
			err = asr_regreq_set_password(regreq, password);
			asradm_erase_pass_file();
		}
	}
	if (opt_proxy_user != NULL) {
		char *proxy_pass = getpassphrase("Enter proxy password: ");
		if ((err = asr_setprop_str(
		    asrh, ASR_PROP_PROXY_PASS, proxy_pass)) != 0) {
			(void) fprintf(stderr,
			    "Error setting proxy password.\n");
			goto finally;
		}
		if ((err = asr_setprop_str(
		    asrh, ASR_PROP_PROXY_USER, opt_proxy_user)) != 0)
			goto finally;
	}

	if (opt_proxy_host != NULL &&
	    (err = asr_setprop_str(
	    asrh, ASR_PROP_PROXY_HOST, opt_proxy_host)) != 0)
		goto finally;
	if (opt_proxy_pass != NULL &&
	    (err = asr_setprop_str(
	    asrh, ASR_PROP_PROXY_PORT, opt_proxy_pass)) != 0)
		goto finally;

	for (i = optind; i < argc; i++)
		if (asradm_addopt(asr_get_config(asrh), argv[i]) != 0) {
			(void) fprintf(stderr,
			    "Unable to parse %s\n", argv[i]);
			goto finally;
		}

	asr_set_debug(asrh, debug);
	if (debug)
		asr_set_logfile(asrh, stderr);
	err |= ph_tprt_init(asrh);
	err |= asr_set_config_name(asrh, asr_conf);

	if (opt_beta) {
		err |= asr_setprop_str(
		    asrh, ASR_PROP_BETA, ASR_VALUE_TRUE);
		if (opt_endpoint)
			err |= asr_setprop_str(
			    asrh, ASR_PROP_BETA_URL, opt_endpoint);
	} else if (opt_endpoint) {
		err |= asr_setprop_str(
		    asrh, ASR_PROP_DEST_URL, opt_endpoint);
	}

	if (err) {
		(void) asr_hdl_destroy(asrh);
		return (err);
	}
	regres = asr_reg(asrh, regreq, &rsp);
	if (regres) {
		char *msg = asr_getprop_strd(asrh,
		    ASR_PROP_REG_MESSAGE, (char *)asr_errmsg());
		(void) fprintf(stderr, "Registration Error: %s\n", msg);
	} else {
		if ((err = do_asr_msg(asrh, do_asr_activate, NULL)) != 0) {
			(void) fprintf(stderr,
			    "Error activating (%s).\n", asr_errmsg());
			goto finally;
		}
		if ((err = ph_save_reg(asrh)) != 0) {
			(void) do_asr_msg(asrh, do_asr_deactivate, NULL);
			(void) fprintf(stderr,
			    "Failed to save registration (%s).\n",
			    strerror(errno));
			goto finally;
		}
		(void) smf_refresh_instance(asr_conf);

		(void) printf("Registration complete.\n");

		if ((err = asradm_send_fault()) != 0) {
			(void) printf("failed to send outstanding faults.\n");
		}
	}

finally:
	if (rsp != NULL)
		nvlist_free(rsp);
	asr_hdl_destroy(asrh);
	asr_regreq_destroy(regreq);
	return (err);
}

/*
 * Sends the deactivation event, unregisters with transport and clears
 * all registration entries.
 */
int
asradm_unregister_cmd(int argc, char **argv)
{
	int err = 0;
	asr_handle_t *asrh = NULL;
	char *client_id;

	if (asradm_parse_opts(argc, argv, ":c:d") != 0) {
		asradm_usage();
		return (2);
	}
	if ((asrh = asr_hdl_init(asr_conf)) == NULL) {
		(void) fprintf(stderr,
		    "Failed to create ASR handle for unregistering\n");
		return (1);
	}
	asr_set_debug(asrh, debug);
	if (debug)
		asr_set_logfile(asrh, stderr);
	if (ph_read_reg(asrh)) {
		(void) fprintf(stderr,
		    "Unable read registration properties.\n");
		return (1);
	}
	if ((err = ph_tprt_init(asrh)) != 0) {
		(void) fprintf(stderr,
		    "Failed to setup ASR transport\n");
		goto finally;
	}

	client_id = asr_get_regid(asrh);
	if (client_id == NULL || client_id[0] == '\0') {
		(void) fprintf(stderr, "Not Registered\n");
		goto finally;
	}

	if (do_asr_msg(asrh, do_asr_deactivate, NULL)) {
		(void) fprintf(stderr,
		    "Failed to send deactivation event. (%s)\n",
		    asr_errmsg());
		goto finally;
	}
	if ((err = asr_unreg(asrh)) != 0) {
		(void) fprintf(stderr,
		    "Failed to unregister client (%s)\n", asr_errmsg());
		goto finally;
	}

	if ((err = ph_save_reg(asrh)) != 0) {
		(void) fprintf(stderr,
		    "Failed to save unregistration (%s)\n", strerror(errno));
	}
	(void) smf_refresh_instance(asr_conf);

finally:
	asr_hdl_destroy(asrh);
	return (err);
}

/*
 * Gets the name of the executable running this command
 */
static char *
asradm_get_command(char *arg)
{
	char *cmd = arg;
	for (cmd = arg; *arg != '\0'; arg++) {
		if (*arg == '/')
			cmd = arg + 1;
	}
	return (cmd);
}

/*
 * asradm - Phone Home ASR Administration CLI command
 */
int
main(int argc, char **argv)
{
	int err = 0;
	char *cmd = asradm_get_command(argv[0]);

	/* Runs the ASR notification service. */
	if (strcmp("asr-notify", cmd) == 0)
		return (ph_main(argc, argv));

	/* Runs the ASR administration command. */
	if (argc == 1) {
		(void) fprintf(stderr, "Error: No command given.\n");
		asradm_usage();
		return (2);
	}
	optind = 2;
	if (strcmp("connect", argv[1]) == 0)
		err = asradm_connect_cmd(argc, argv);
	else if (strcmp("list", argv[1]) == 0)
		err = asradm_list_cmd(argc, argv);
	else if (strcmp("register", argv[1]) == 0)
		err = asradm_register_cmd(argc, argv);
	else if (strcmp("send", argv[1]) == 0)
		err = asradm_send_cmd(argc, argv);
	else if (strcmp("set-proxy", argv[1]) == 0)
		err = asradm_connect_cmd(argc, argv);
	else if (strcmp("start", argv[1]) == 0)
		err = ph_main(argc, argv);
	else if (strcmp("unregister", argv[1]) == 0)
		err = asradm_unregister_cmd(argc, argv);
	else {
		(void) fprintf(stderr,
		    "Error: Unknown command (%s).\n", argv[1]);
		asradm_usage();
		err = 2;
	}
	asr_cleanup();
	return (err);
}
