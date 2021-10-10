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

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <dirent.h>
#include <errno.h>
#include <libscf.h>
#include <priv_utils.h>
#include <netdb.h>
#include <pthread.h>
#include <pwd.h>
#include <secdb.h>
#include <sys/nvpair.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <zone.h>
#include <fm/libasr.h>

#include "asr_notify.h"
#include "asr_notify_scf.h"
#include "asr_ssl.h"
#include "libasr.h"
#include "libfmnotify.h"

/* Phone Home ASR service state and configuration */
typedef struct phone_home {
	nd_hdl_t *ph_nhdl;		/* FM Notify handle */
	char *ph_cfg;			/* configuration FMRI instance name */
	char *ph_msgdir;		/* data directory for asr messages */
	boolean_t ph_cfg_chg;		/* Configuration needs updating */
	boolean_t ph_forground;		/* flag to run as daemon or program */
	boolean_t ph_registered;	/* ASR client is registered */
	boolean_t ph_autoreg;		/* True if autoreg attempted */
	long ph_interval;		/* Polling interval */
	long ph_audit_interval;		/* Hours between audit events */
	long ph_hb_interval;		/* Hours between heartbeat events */
	long ph_msg_counter;		/* Counts messages between polling */
	long ph_max_msgs;		/* Max messages that can be sent */
	pthread_mutex_t ph_asr_lock;	/* ASR lock */
} phone_home_t;

static phone_home_t *ph_hdl;
static char *optstr = "dfR:c:";
static char *opt_R = NULL;
static char *opt_c = PH_FMRI;
static boolean_t opt_d = B_FALSE;
static boolean_t opt_f = B_FALSE;

/*
 * Prints the usage for this command to stderr. (-c & -d options are hidden)
 */
static void
ph_usage(const char *pname)
{
	(void) fprintf(stderr, "Usage: %s [-f] [-R <altroot>]\n",
	    pname);

	(void) fprintf(stderr,
	    "\t-f  stay in foreground\n"
	    "\t-R  specify alternate root\n");
}

/*
 * Free the ASR library and unlock it.
 */
static void
ph_destroy_asr(phone_home_t *ph, asr_handle_t *asrh)
{
	if (asrh != NULL) {
		asr_hdl_destroy(asrh);
		(void) pthread_mutex_unlock(&ph->ph_asr_lock);
	}
}

/*
 * Initialize and lock the ASR library.
 */
static asr_handle_t *
ph_init_asr(phone_home_t *ph)
{
	asr_handle_t *asrh = NULL;
	char *config = ph->ph_cfg == NULL ? PH_FMRI : ph->ph_cfg;

	(void) pthread_mutex_lock(&ph->ph_asr_lock);
	nd_debug(ph->ph_nhdl, "Loading ASR config from %s", config);
	if ((asrh = asr_hdl_init(config)) == NULL) {
		nd_error(ph->ph_nhdl, "Failed to initialize ASR handle.");
		(void) pthread_mutex_unlock(&ph->ph_asr_lock);
		return (NULL);
	}
	(void) asr_set_errno(EASR_NONE);
	if (opt_d)
		asr_set_debug(asrh, B_TRUE);

	/* errors/debug info will be logged to SMF log for asr-notify */
	asr_set_logfile(asrh, stderr);

	if (ph_read_reg(asrh) != 0) {
		nd_error(ph->ph_nhdl,
		    "Failed to read registration properties.");
		ph_destroy_asr(ph, asrh);
		return (NULL);
	}
	if (ph_tprt_init(asrh) != 0) {
		nd_error(ph->ph_nhdl,
		    "Failed to initialize ASR transport");
		ph_destroy_asr(ph, asrh);
		return (NULL);
	}
	nd_debug(ph->ph_nhdl, "ASR Initialized");
	return (asrh);
}


/*
 * Makes a directory owned by the asr-notify phone home process.
 */
static int
ph_mkdir(phone_home_t *ph, char *path, mode_t mode)
{
	int err = 0;
	struct stat st;

	if (stat(path, &st) != 0) {
		if ((err = mkdir(path, mode)) != 0)
			nd_error(ph->ph_nhdl,
			    "Unable to mkdir %s (%s)", path, strerror(errno));
		else if ((err = chown(path, PH_UID, PH_UID)) != 0)
			nd_error(ph->ph_nhdl,
			    "Unable to chown %s (%s)", path, strerror(errno));
	}
	return (err);
}

/*
 * Makes all the directories in a path as needed.
 */
static int
ph_mkdirs(phone_home_t *ph, char *root, char *path, mode_t mode)
{
	int err = 0;
	char *full, *s, *p;
	int rlen, plen;

	if (path == NULL || path[0] == '\0')
		return (PH_FAILURE);

	rlen = root == NULL ? 0 : strlen(root);
	plen = strlen(path);
	if ((full = malloc(rlen + plen + 1)) == NULL)
		return (PH_FAILURE);

	if (root != NULL)
		(void) strcpy(full, root);
	p = full + rlen;
	(void) strcpy(p, path);

	for (s = p; s[0] != '\0' && err == 0; s++) {
		if (s[0] == '/') {
			s[0] = '\0';
			if (full == NULL || full[0] == '\0')
				continue;
			err = ph_mkdir(ph, full, mode);
			*s = '/';
		}
	}
	free(full);
	return (err);
}

/*
 * Gets the modification time of a file contained in the phone home
 * data directory. Used to tell time to send new heartbeat and audit events.
 */
static time_t
ph_getmtime(phone_home_t *ph, char *path)
{
	int md = -1;
	long retime = 0;
	struct stat st;

	if ((md = open(ph->ph_msgdir, O_SEARCH)) == -1)
		return (0);

	if (fstatat(md, path, &st, 0) == 0)
		retime = st.st_mtim.tv_sec;

	(void) close(md);
	return (retime);
}

/*
 * Sets a phone-home SMF property
 */
static int
ph_set_cfg(phone_home_t *ph, char *name, char *value)
{
	char *config = ph->ph_cfg == NULL ? PH_FMRI : ph->ph_cfg;
	return (ph_scf_set_string(config, name, value));
}

/*
 * Sets the asset identity of the system if not already set.
 * We want this saved to the SMF properties so that future registrations
 * will continue to have the same asset id.
 */
static int
ph_set_identity(phone_home_t *ph, asr_handle_t *asrh)
{
	char *asset_id = asr_get_assetid(asrh);

	if (asset_id == NULL || asset_id[0] == '\0') {
		uuid_t uuid;
		char uuidbuf[UUID_PRINTABLE_STRING_LENGTH];
		uuid_generate(uuid);
		uuid_unparse(uuid, uuidbuf);
		if (asr_setprop_str(asrh, ASR_PROP_ASSET_ID, uuidbuf) != 0)
			return (PH_FAILURE);
		return (ph_set_cfg(ph, ASR_PROP_ASSET_ID, uuidbuf));
	}
	return (0);
}

/*
 * Reads ASR configuration properties and modifies phone home service state.
 * The service will exit by calling nd_abort if unable read the configuration.
 */
static void
ph_read_config_asr(phone_home_t *ph, asr_handle_t *asrh)
{
	char *datadir = asr_getprop_strd(
	    asrh, ASR_PROP_DATA_DIR, PH_DEFAULT_DATADIR);
	char *prop;
	char *root = opt_R;

	if (opt_d)
		ph->ph_nhdl->nh_debug = B_TRUE;
	else
		ph->ph_nhdl->nh_debug = asr_getprop_bool(
		    asrh, ASR_PROP_DEBUG, opt_d);

	nd_debug(ph->ph_nhdl, "Phone home reading configuration properties.");

	prop = asr_get_regid(asrh);
	if (prop == NULL || prop[0] == '\0') {
		ph->ph_registered = B_FALSE;
		nd_debug(ph->ph_nhdl, "ASR client not registered.");
	} else {
		char *id = asr_get_systemid(asrh);
		char *reg = asr_getprop_str(asrh, ASR_PROP_REG_SYSTEM_ID);
		if (id == NULL || reg == NULL || strcmp(id, reg) != 0) {
			nd_abort(ph->ph_nhdl,
			    "Registered id, %s not the same as system id, %s\n"
			    "System must be registered again with new id.",
			    reg, id);
		}
		ph->ph_registered = B_TRUE;
		nd_debug(ph->ph_nhdl, "System registered with id %s.", reg);
	}

	if (root == NULL)
		root = asr_getprop_strd(asrh, ASR_PROP_ROOTDIR, "/");
	if (ph->ph_nhdl->nh_rootdir == NULL) {
		ph->ph_nhdl->nh_rootdir = strdup(root);
		if (ph_mkdirs(ph, root, datadir,
		    S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) != 0)
			nd_abort(ph->ph_nhdl,
			    "Unable to setup configuration directory %s",
			    datadir);
	}
	if (ph->ph_msgdir == NULL) {
		size_t len;
		len = snprintf(NULL, 0, "%s%s%s",
		    ph->ph_nhdl->nh_rootdir, datadir, PH_MSGDIR);
		ph->ph_msgdir = malloc(len + 1);
		(void) snprintf(ph->ph_msgdir, len + 1, "%s%s%s",
		    ph->ph_nhdl->nh_rootdir, datadir, PH_MSGDIR);
		if (ph_mkdirs(ph, root, ph->ph_msgdir + strlen(root),
		    S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) != 0)
			nd_abort(ph->ph_nhdl,
			    "Unable to setup message directory %s",
			    ph->ph_msgdir);
	}

	ph->ph_audit_interval = asr_getprop_long(
	    asrh, ASR_PROP_AUDIT_INTERVAL, ASR_AUDIT_INTERVAL_DEFAULT);
	nd_debug(ph->ph_nhdl, "Audit interval = %dl", ph->ph_audit_interval);
	ph->ph_hb_interval = asr_getprop_long(
	    asrh, ASR_PROP_HB_INTERVAL, ASR_HB_INTERVAL_DEFAULT);
	nd_debug(ph->ph_nhdl, "Heartbeat interval = %dl", ph->ph_hb_interval);
	ph->ph_interval = asr_getprop_long(
	    asrh, ASR_PROP_POLL, PH_DEFAULT_INTERVAL);
	nd_debug(ph->ph_nhdl, "Fault interval = %dl", ph->ph_interval);
	ph->ph_max_msgs = asr_getprop_long(
	    asrh, PH_PROP_MAX_MSGS, PH_DEFAULT_MAX_MSGS);
	nd_debug(ph->ph_nhdl, "Max faults per interval = %dl", ph->ph_max_msgs);
}

/*
 * Attempt to do auto-registration based on preset SMF autoreg properties.
 * If autoreg properties are set and the service fails registration then
 * the service will exit calling nd_abort to put the service in
 * maintenane mode.
 */
static void
ph_autoreg(phone_home_t *ph, asr_handle_t *ah)
{
	int err = 0;
	char *user = asr_getprop_str(ah, ASR_PROP_AREG_USER);
	char *pass = asr_getprop_str(ah, ASR_PROP_AREG_PASS);
	char *phost = asr_getprop_str(ah, ASR_PROP_AREG_PROXY_HOST);
	char *puser = asr_getprop_str(ah, ASR_PROP_AREG_PROXY_USER);
	char *ppass = asr_getprop_str(ah, ASR_PROP_AREG_PROXY_PASS);
	asr_regreq_t *regreq;
	nvlist_t *regrsp = NULL;

	if (phost != NULL) {
		char *sep = strstr(phost, ":");
		if (sep == NULL) {
			err |= asr_setprop_str(ah, ASR_PROP_PROXY_HOST, phost);
			err |= asr_setprop_str(
			    ah, ASR_PROP_PROXY_PORT, ASR_PROXY_DEFAULT_PORT);
		} else {
			char *port = sep + 1;
			char *proxy = strndup(phost, sep - phost);
			if (proxy == NULL) {
				nd_abort(ph->ph_nhdl,
				    "failed to allocate proxy");
			}
			err |= asr_setprop_str(ah, ASR_PROP_PROXY_HOST, proxy);
			err |= asr_setprop_str(ah, ASR_PROP_PROXY_PORT, port);
			free(proxy);
		}
		if (puser != NULL)
			err |= asr_setprop_str(ah, ASR_PROP_PROXY_USER, puser);
		if (ppass != NULL)
			err |= asr_setprop_str(ah, ASR_PROP_PROXY_PASS, ppass);
		if (err != 0)
			nd_abort(ph->ph_nhdl, "failed to setup proxy");
	}
	if (user == NULL || user[0] == '\0' || pass == NULL || pass[0] == '\0')
		return;

	nd_debug(ph->ph_nhdl, "Attempting auto registration.");
	if ((regreq = asr_regreq_init()) == NULL ||
	    asr_regreq_set_user(regreq, user) != 0 ||
	    asr_regreq_set_password(regreq, pass) != 0)
		nd_abort(ph->ph_nhdl,
		    "failed to initialize registration request");

	if (asr_reg(ah, regreq, &regrsp) != 0) {
		char *msg = asr_getprop_strd(ah,
		    ASR_PROP_REG_MESSAGE, (char *)asr_errmsg());
		(void) sleep(5);
		nd_abort(ph->ph_nhdl, "failed to register client: %s\n", msg);
	}

	if (phost != NULL) {
		if (ph_scf_set_string(opt_c, ASR_PROP_PROXY_HOST,
		    asr_getprop_str(ah, ASR_PROP_PROXY_HOST)) != 0 ||
		    ph_scf_set_string(opt_c, ASR_PROP_PROXY_PORT,
		    asr_getprop_str(ah, ASR_PROP_PROXY_PORT)) != 0) {
			nd_abort(ph->ph_nhdl,
			    "failed to save proxy properties.\n");
		}
	}

	if (ph_save_reg(ah) != 0) {
		nd_abort(ph->ph_nhdl,
		    "failed to save registration: %s.\n", asr_errmsg());
	}

	err |= ph_scf_set_string(opt_c, ASR_PROP_AREG_USER, NULL);
	err |= ph_scf_set_string(opt_c, ASR_PROP_AREG_PASS, NULL);
	if (phost != NULL)
		err |= ph_scf_set_string(opt_c, ASR_PROP_AREG_PROXY_HOST, NULL);
	if (puser != NULL)
		err |= ph_scf_set_string(opt_c, ASR_PROP_AREG_PROXY_USER, NULL);
	if (ppass != NULL)
		err |= ph_scf_set_string(opt_c, ASR_PROP_AREG_PROXY_PASS, NULL);
	if (err != 0)
		nd_error(ph->ph_nhdl,
		    "failed to clear auto-registration values");
	(void) smf_refresh_instance(opt_c);

	nd_debug(ph->ph_nhdl, "Auto registration complete");
	if (regrsp != NULL)
		nvlist_free(regrsp);
	asr_regreq_destroy(regreq);
	ph->ph_registered = B_TRUE;
	ph->ph_autoreg = B_TRUE;
}

/*
 * Initializes phone home with service properties if needed
 * (like after a SIGHUP)
 */
static int
ph_read_config(phone_home_t *ph)
{
	int err = 0;
	if (ph->ph_cfg_chg == B_TRUE) {
		asr_handle_t *asrh;
		nd_debug(ph->ph_nhdl, "Reading ASR configuration properties.");

		if ((asrh = ph_init_asr(ph)) == NULL)
			return (PH_FAILURE);
		ph_read_config_asr(ph, asrh);
		err |= ph_set_identity(ph, asrh);
		if (!ph->ph_autoreg)
			ph_autoreg(ph, asrh);
		ph_destroy_asr(ph, asrh);

		ph->ph_cfg_chg = B_FALSE;
	}
	return (err);
}

/*
 * Releases all resources used by the phone-home daemon.
 */
static void
ph_fini(phone_home_t *ph)
{
	if (ph == NULL)
		return;

	if (ph->ph_nhdl != NULL) {
		nd_debug(ph->ph_nhdl, "Phone home shutting down...");
		nd_cleanup(ph->ph_nhdl);
		if (ph->ph_nhdl->nh_rootdir != NULL)
			free(ph->ph_nhdl->nh_rootdir);
		free(ph->ph_nhdl);
	}

	if (ph->ph_cfg != NULL)
		free(ph->ph_cfg);
	if (ph->ph_msgdir != NULL)
		free(ph->ph_msgdir);
	free(ph);
	asr_cleanup();
}

/*
 * Initializes the phone-home structure
 */
static phone_home_t *
ph_init()
{
	phone_home_t *ph = malloc(sizeof (phone_home_t));

	if (ph == NULL) {
		(void) fprintf(stderr, "Failed to get memory for phone home"
		    "handle (%s)", strerror(errno));
		return (NULL);
	}
	bzero(ph, sizeof (phone_home_t));

	if ((ph->ph_nhdl = malloc(sizeof (nd_hdl_t))) == NULL) {
		free(ph);
		(void) fprintf(stderr, "Failed to get memory for notify handle"
		    " (%s)", strerror(errno));
		return (NULL);
	}
	bzero(ph->ph_nhdl, sizeof (nd_hdl_t));
	ph->ph_nhdl->nh_keep_running = B_TRUE;
	ph->ph_nhdl->nh_debug = opt_d;
	ph->ph_nhdl->nh_log_fd = stderr;

	ph->ph_cfg = strdup(opt_c);
	ph->ph_forground = opt_f;
	ph->ph_cfg_chg = B_TRUE;
	ph->ph_interval = PH_DEFAULT_INTERVAL;

	if (ph_read_config(ph) != 0) {
		ph_fini(ph);
		ph = NULL;
	}
	return (ph);
}

/*
 * Rereads the phone home service configuration in response to a configuration
 * change signal.  Running "svcadm refresh" will generate this signal.
 */
static void
ph_svc_config(phone_home_t *ph)
{
	if (ph != NULL)
		ph->ph_cfg_chg = B_TRUE;
}

/*
 * Shuts down the phone home service in response to a shutdown signal.
 */
static void
ph_shutdown(phone_home_t *ph)
{
	if (ph != NULL && ph->ph_nhdl != NULL)
		ph->ph_nhdl->nh_keep_running = B_FALSE;
}

/*
 * Handles asynchronous signals.
 */
/*ARGSUSED*/
static void
ph_sighandler(int sig, siginfo_t *si, void *data)
{
	/* ignore signals from internal threads */
	if (si != NULL && getpid() == si->si_pid)
		return;

	if (sig == SIGHUP)
		ph_svc_config(ph_hdl);
	else
		ph_shutdown(ph_hdl);
}

/*
 * Sets up the phone home transport module.
 */
int
ph_tprt_init(asr_handle_t *asrh)
{
	int err = 0;
	char *tprt = asr_getprop_strd(
	    asrh, ASR_PROP_TRANSPORT, PH_DEFAULT_TRANSPORT);

	if (strcmp(tprt, "DTS") == 0)
		err = asr_dts_init(asrh);
	else if (strcmp(tprt, "SCRK") == 0)
		err = asr_scrk_init(asrh);
	else {
		(void) fprintf(stderr, "Unknown transport %s\n", tprt);
		err = 1;
	}
	return (err);
}

/*
 * Saves the registration properties to a file and set the owner and modq
 */
int
ph_save_reg_file(char *key, char *dat, nvlist_t *cfg)
{
	int err;
	if ((err = asr_ssl_write_aes_config_names(
	    key, dat, cfg, "reg/")) != 0 ||
	    (err = chmod(key, S_IRUSR|S_IWUSR)) != 0 ||
	    (err = chmod(dat, S_IRUSR|S_IWUSR)) != 0 ||
	    (err = chown(key, PH_UID, PH_UID)) != 0 ||
	    (err = chown(dat, PH_UID, PH_UID)) != 0)
		return (err);

	return (ASR_OK);
}

/*
 * Saves registration properties to either the file system or to SMF
 * properties.  When saved to the file system the data is encrypted and
 * the files are readable only by PH_UID.
 */
int
ph_save_reg(asr_handle_t *asrh)
{
	char *key = NULL, *dat = NULL;
	nvlist_t *cfg = asr_get_config(asrh);
	int err;

	key = asr_getprop_path(asrh, ASR_PROP_REG_KEY_FILE, NULL);
	dat = asr_getprop_path(asrh, ASR_PROP_REG_DATA_FILE, NULL);
	if (key == NULL || dat == NULL)
		err = asr_save_config(asrh);
	else
		err = ph_save_reg_file(key, dat, cfg);

	if (key != NULL)
		free(key);
	if (dat != NULL)
		free(dat);

	return (err);
}

/*
 * Reads the registration properties from an encrypted file
 */
int
ph_read_reg(asr_handle_t *asrh)
{
	int err = 0;
	char *key = NULL, *dat = NULL;
	nvlist_t *cfg = asr_get_config(asrh);
	nvlist_t *scfg = NULL;

	if ((key = asr_getprop_path(
	    asrh, ASR_PROP_REG_KEY_FILE, NULL)) == NULL ||
	    (dat = asr_getprop_path(
	    asrh, ASR_PROP_REG_DATA_FILE, NULL)) == NULL)
		goto finally;

	/* If files don't exist then there is nothing to do */
	if (access(key, F_OK) != 0 || access(dat, F_OK) != 0)
		goto finally;

	/* If files can't be read then don't even try and error out. */
	if (access(key, R_OK) != 0 || access(dat, R_OK) != 0) {
		err = PH_FAILURE;
		goto finally;
	}

	if ((err = asr_ssl_read_aes_nvl(key, dat, &scfg)) == ASR_OK) {
		if (nvlist_merge(cfg, scfg, 0) != 0)
			err = PH_FAILURE;
		nvlist_free(scfg);
	}

finally:
	if (key != NULL)
		free(key);
	if (dat != NULL)
		free(dat);

	return (err);
}

/*
 * Saves an ASR message to the filesystem in the configured ASR
 * data directory
 */
static int
ph_save_msg(phone_home_t *ph, char *data, char *out)
{
	int err = 0;
	int md, fd, len;
	uuid_t uuid;
	char tmp[UUID_PRINTABLE_STRING_LENGTH];

	/* Nothing to save here */
	if (data == NULL || data[0] == '\0')
		return (ASR_OK);

	uuid_generate(uuid);
	uuid_unparse(uuid, tmp);

	if ((md = open(ph->ph_msgdir, O_SEARCH)) == -1)
		return (PH_FAILURE);
	if ((fd = openat(md, tmp, O_CREAT|O_TRUNC|O_WRONLY,
	    S_IRUSR|S_IWUSR|S_IRGRP| S_IROTH)) == -1) {
		(void) close(md);
		return (PH_FAILURE);
	}
	len = strlen(data);
	if (len != write(fd, data, strlen(data))) {
		err = PH_FAILURE;
		(void) unlinkat(md, tmp, 0);
	}
	(void) close(fd);
	err = renameat(md, tmp, md, out);
	(void) close(md);

	return (err);
}

/*
 * If we get to noisy or are missconfigured the transport endpoint can
 * tell us to stop trying to send messages.
 */
static void
do_asr_abort(nvlist_t *resp)
{
	char *code = "unknown";
	char *msg = "unknown";
	if (resp != NULL) {
		(void) nvlist_lookup_string(resp, ASR_MSG_RSP_CODE, &code);
		(void) nvlist_lookup_string(resp, ASR_MSG_RSP_MESSAGE, &msg);
	}
	nd_abort(ph_hdl->ph_nhdl, "Fatal tranport error %s (%s)", code, msg);
}

/*
 * Sends an ASR message and logs the result.
 */
static int
ph_send_msg(asr_handle_t *asrh, asr_message_t *msg, char *type)
{
	nvlist_t *resp = NULL;
	int err = 0;

	if ((err = asr_send_msg(asrh, msg, &resp)) == 0) {
		nd_debug(ph_hdl->ph_nhdl, "Sent ASR message %s (size=%ul)\n",
		    type, (unsigned long)msg->asr_msg_len);
	} else {
		char *retry;
		nd_error(ph_hdl->ph_nhdl,
		    "Failed to send %s msg: (%s)\n", type, asr_errmsg());

		/* Ignore Audit errors.  Try again next audit interval */
		if (strcmp(PH_AUDIT_MSG, type) == 0)
			err = 0;

		/* Check to see if endpoint is telling us to abort */
		else if (resp != NULL &&
		    nvlist_lookup_string(
		    resp, ASR_MSG_RSP_RETRY, &retry) == 0 &&
		    strcmp(ASR_VALUE_FALSE, retry) == 0)
			do_asr_abort(resp);
	}
	if (resp != NULL)
		nvlist_free(resp);
	return (err);
}

/*
 * Sends an ASR message over the configured and registered transport
 */
static int
do_asr(int (*func)(asr_handle_t *asrh, asr_message_t **msg), char *out)
{
	int err;
	asr_message_t *msg;
	asr_handle_t *asrh = ph_init_asr(ph_hdl);

	if (asrh == NULL)
		return (-1);

	err = (*func)(asrh, &msg);
	if (err == 0) {
		if ((err = ph_send_msg(asrh, msg, out)) == 0) {
			if (ph_save_msg(ph_hdl, msg->asr_msg_data, out) != 0)
				nd_abort(ph_hdl->ph_nhdl,
				    "Failed to save msg: (%s)\n", out);
		}
		asr_free_msg(msg);
	} else {
		nd_error(ph_hdl->ph_nhdl,
		    "Error creating msg: (%s)\n", asr_errmsg());
	}
	ph_destroy_asr(ph_hdl, asrh);
	return (err);
}

/*
 * Saves an asr fault message to the ASR data directory
 */
static int
ph_save_fault(phone_home_t *ph, nvlist_t *event, asr_message_t *msg)
{
	char *out = NULL;
	char *id;
	int len = 0;
	int err;

	if (nvlist_lookup_string(event, "uuid", &id) != 0)
		return (PH_FAILURE);
	len = snprintf(NULL, 0, "fault-%s.xml", id);
	if ((out = malloc(len+1)) == NULL)
		return (PH_FAILURE);

	(void) snprintf(out, len+1, "fault-%s.xml", id);

	err = ph_save_msg(ph, msg->asr_msg_data, out);
	free(out);
	return (err);
}

static int
ph_read_msg_data(int fd, char **data, size_t *dlen)
{
	FILE *f = fdopen(fd, "r");
	*data = NULL;

	if (f == NULL)
		return (PH_FAILURE);

	if (fseek(f, 0, SEEK_END) != 0) {
		(void) fclose(f);
		return (PH_FAILURE);
	}
	*dlen = ftell(f);
	if (*dlen == 0) {
		(void) fclose(f);
		return (PH_FAILURE);
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		(void) fclose(f);
		return (PH_FAILURE);
	}

	*data = malloc(*dlen+1);
	if (*data == NULL) {
		(void) fclose(f);
		return (PH_FAILURE);
	}

	if (*dlen != fread(*data, 1, *dlen, f)) {
		free(*data);
		*data = NULL;
		(void) fclose(f);
		return (PH_FAILURE);
	}
	(*data)[*dlen] = '\0';
	(void) fclose(f);
	return (ASR_OK);
}

static int
ph_fault_send(phone_home_t *ph, asr_handle_t *asrh, char *name)
{
	int md, fd;
	int err = 0;
	asr_message_t msg;

	if (asrh == NULL)
		return (PH_FAILURE);
	if (ph->ph_msg_counter++ >= ph->ph_max_msgs) {
		(void) asr_error(EASR_SC,
		    "Phone Home sending too many messages.");
		return (PH_FAILURE);
	}

	msg.asr_msg_type = ASR_MSG_FAULT;
	msg.asr_msg_data = NULL;

	if ((md = open(ph->ph_msgdir, O_SEARCH)) == -1)
		return (PH_FAILURE);
	if ((fd = openat(md, name, O_RDONLY)) == -1) {
		(void) close(md);
		return (PH_FAILURE);
	}
	nd_debug(ph->ph_nhdl, "Resending %s\n", name);

	err = ph_read_msg_data(fd, &(msg.asr_msg_data), &(msg.asr_msg_len));
	if (err == 0)
		err = ph_send_msg(asrh, &msg, PH_FAULT_MSG);

	if (msg.asr_msg_data != NULL)
		free(msg.asr_msg_data);
	(void) close(fd);
	if (err == 0)
		(void) unlinkat(md, name, 0);
	(void) close(md);
	return (err);
}

/*
 * Read in fault xml files that were not able to be sent to ASR and retry
 * to send them.
 */
static int
ph_fault(phone_home_t *ph)
{
	DIR *md;
	struct dirent *dp;
	int len;
	int err = 0;
	asr_handle_t *asrh = NULL;

	if ((md = opendir(ph->ph_msgdir)) == NULL)
		return (PH_FAILURE);
	while ((dp = readdir(md)) != NULL) {
		if (strncmp("fault-", dp->d_name, 6) != 0)
			continue;
		len = strlen(dp->d_name);
		if (strcmp(".xml", dp->d_name + len - 4) != 0)
			continue;
		if (asrh == NULL)
			asrh = ph_init_asr(ph);
		if (ph_fault_send(ph, asrh, dp->d_name)) {
			err = PH_FAILURE;
			break;
		}
	}
	ph_destroy_asr(ph, asrh);
	(void) closedir(md);
	return (err);
}

/*
 * Sends an ASR fault message with content from the given FMA event.
 */
static void
ph_fault_event(nvlist_t *event)
{
	asr_message_t *msg;
	asr_handle_t *asrh = ph_init_asr(ph_hdl);

	if (asrh != NULL) {
		if (asr_fault(asrh, event, &msg) == 0) {
			if (ph_save_fault(ph_hdl, event, msg) != 0)
				nd_error(ph_hdl->ph_nhdl,
				    "Error saving fault.xml");
			asr_free_msg(msg);
		} else {
			nd_error(ph_hdl->ph_nhdl,
			"Error creating msg: (%s)\n", asr_errmsg());
		}
		ph_destroy_asr(ph_hdl, asrh);
	}
}

/*
 * Callback function that handles list.suspect events
 *
 * Events will be sent ASR is registered.
 */
/*ARGSUSED*/
static void
ph_listev_cb(fmev_t ev, const char *class, nvlist_t *nvl, void *arg)
{
	nd_ev_info_t *ev_info = NULL;
	nvlist_t *fault = NULL;
	boolean_t domsg;

	nd_debug(ph_hdl->ph_nhdl, "Received event of class %s", class);

	if (ph_hdl->ph_registered == B_FALSE) {
		nd_debug(ph_hdl->ph_nhdl, "ASR is not registered so event (%s) "
		    "was not sent.", class);
		return;
	}

	if (nd_get_event_info(ph_hdl->ph_nhdl, class, ev, &ev_info) != 0) {
		nd_error(ph_hdl->ph_nhdl, "Error getting event info for event "
		    "of class %s", class);
		return;
	}

	/*
	 * If the message payload member is set to 0, then it's an event we
	 * typically suppress messaging on, so we won't send an email for it.
	 */
	if (nvlist_lookup_boolean_value(ev_info->ei_payload, FM_SUSPECT_MESSAGE,
	    &domsg) == 0 && !domsg) {
		nd_debug(ph_hdl->ph_nhdl,
		    "Messaging suppressed for this event");
		goto finally;
	}

	if (nvlist_dup(ev_info->ei_payload, &fault, 0) != 0)
		goto finally;
	(void) nvlist_add_string(fault, "description", ev_info->ei_descr);
	(void) nvlist_add_string(fault, "severity", ev_info->ei_severity);
	(void) nvlist_add_string(fault, "reason", ev_info->ei_reason);

	ph_fault_event(fault);

finally:
	if (fault != NULL)
		nvlist_free(fault);

	if (ev_info)
		nd_free_event_info(ev_info);
}

/*
 * Sends an audit event if not sent in the last week (asr/audit/interval)
 */
static int
ph_audit(phone_home_t *ph)
{
	int err = 0;
	long last_sent = ph_getmtime(ph, PH_AUDIT_MSG);
	long interval = ASR_TIME_INTERVAL_SECS(ph->ph_audit_interval);
	time_t now = time(NULL);
	long next = last_sent + interval;

	if (now > next) {
		nd_debug(ph->ph_nhdl, "Sending audit event...");
		if ((err = do_asr(asr_audit, PH_AUDIT_MSG)) != 0)
			nd_debug(ph->ph_nhdl, "Error sending audit event.");
	} else {
		nd_debug(ph->ph_nhdl, "Next audit due in %dl minutes",
		    (next - now)/60l);
	}

	return (err);
}

/*
 * Sends a heartbeat event if not sent in the last day. (hearbeat/interval)
 */
static int
ph_heartbeat(phone_home_t *ph)
{
	int err = 0;
	long last_sent = ph_getmtime(ph, PH_HEARTBEAT_MSG);
	long interval = ASR_TIME_INTERVAL_SECS(ph->ph_hb_interval);
	time_t now = time(NULL);
	long next = last_sent + interval;

	if (now > next) {
		nd_debug(ph->ph_nhdl, "Sending heartbeat event...");
		if ((err = do_asr(asr_heartbeat, PH_HEARTBEAT_MSG)) != 0)
			nd_error(ph_hdl->ph_nhdl,
			    "Failed to send heartbeat (%s)", asr_errmsg());
	} else {
		nd_debug(ph->ph_nhdl, "Next heartbeat due in %dl minutes",
		    (next - now)/60l);
	}

	return (err);
}

/*
 * Checks for conditions that will trigger new ASR messages and
 * then send the messages if needed.
 * Keeps track of message state using service properties.
 */
static int
ph_process()
{
	int err = 0;

	if ((err = ph_read_config(ph_hdl)) != 0)
		return (err);

	ph_hdl->ph_msg_counter = 0;
	if (!ph_hdl->ph_registered)
		return (1);

	if ((err = ph_fault(ph_hdl)) != 0)
		return (err);

	if ((err = ph_heartbeat(ph_hdl)) != 0)
		return (err);

	err = ph_audit(ph_hdl);

	return (err);
}

/*
 * Initialize the service's signal handlers.
 */
int
ph_runtime_init(phone_home_t *ph)
{
	nd_hdl_t *nhdl = ph->ph_nhdl;
	struct rlimit rlim;
	struct sigaction act;
	sigset_t set;
	int err = 0;

	(void) sigfillset(&set);
	(void) sigfillset(&act.sa_mask);
	act.sa_sigaction = ph_sighandler;
	act.sa_flags = SA_SIGINFO;

	err |= sigaction(SIGTERM, &act, NULL);
	err |= sigdelset(&set, SIGTERM);
	err |= sigaction(SIGHUP, &act, NULL);
	err |= sigdelset(&set, SIGHUP);

	if (ph->ph_forground) {
		err |= sigaction(SIGINT, &act, NULL);
		err |= sigdelset(&set, SIGINT);
	} else {
		int fd;
		nd_daemonize(nhdl);
		/* setup stdin to dev/null */
		if ((fd = open("/dev/null", O_RDONLY)) < 0 ||
		    dup2(fd, 0) != 0) {
			nd_error(nhdl, "Failed to setup daemon I/O");
			return (SMF_EXIT_ERR_FATAL);
		}
	}

	if (err != 0) {
		nd_error(nhdl, "Failed to setup signal handlers.");
		return (SMF_EXIT_ERR_FATAL);
	}

	rlim.rlim_cur = RLIM_INFINITY;
	rlim.rlim_max = RLIM_INFINITY;

	(void) setrlimit(RLIMIT_CORE, &rlim);
	nhdl->nh_msghdl = fmd_msg_init(nhdl->nh_rootdir, FMD_MSG_VERSION);
	if (nhdl->nh_msghdl == NULL) {
		nd_abort(nhdl, "Failed to initialize libfmd_msg");
		return (SMF_EXIT_ERR_FATAL);
	}

	/* If process isn't uid 0 then it won't be able to listen for faults */
	if (geteuid() != 0) {
		nd_debug(ph->ph_nhdl,
		    "operation requires additional privilege.");
		return (SMF_EXIT_ERR_PERM);
	}

	ph->ph_nhdl->nh_evhdl = fmev_shdl_init(
	    LIBFMEVENT_VERSION_2, NULL, NULL, NULL);
	if (ph->ph_nhdl->nh_evhdl == NULL) {
		(void) sleep(5);
		nd_abort(ph->ph_nhdl, "failed to initialize libfmevent: %s",
		    fmev_strerror(fmev_errno));
		return (SMF_EXIT_ERR_FATAL);
	}
	nd_debug(ph->ph_nhdl, "Setup FMD event listener.");

	/*
	 * Set up our event subscriptions.
	 */
	nd_debug(nhdl, "Subscribing to list.suspect events");
	if (fmev_shdl_subscribe(nhdl->nh_evhdl, "list.suspect", ph_listev_cb,
	    NULL) != FMEV_SUCCESS) {
		nd_abort(nhdl, "fmev_shdl_subscribe failed: %s",
		    fmev_strerror(fmev_errno));
		return (SMF_EXIT_ERR_FATAL);
	}

	/*
	 * If we're in the global zone, reset all of our privilege sets to
	 * the minimum set of required privileges.  Since we've already
	 * initialized our libmevent handle, we no no longer need to run as
	 * root, so we change our uid/gid to noaccess (60002).
	 *
	 * __init_daemon_priv will also set the process core path for us
	 */
	if (!ph->ph_forground && getzoneid() == GLOBAL_ZONEID) {
		if ((err = __init_daemon_priv(
		    PU_RESETGROUPS | PU_LIMITPRIVS | PU_INHERITPRIVS,
		    PH_UID, PH_UID, PRIV_PROC_SETID, NULL)) != 0) {
			nd_abort(nhdl,
			    "additional privileges required to run (%d)", err);
			return (SMF_EXIT_ERR_PERM);
		}
		nd_debug(ph_hdl->ph_nhdl, "set uid/gid to noaccess");
	}
	return (SMF_EXIT_OK);
}

/*
 * The service daemon run function.  Loops on a timeout interval checking
 * for conditions that will trigger ASR messages.
 * Also checks for configuration changes that may cause reregistration or
 * unregistration with ASR.
 */
static void
ph_run()
{
	nd_debug(ph_hdl->ph_nhdl, "Phone home service running...");

	while (ph_hdl->ph_nhdl->nh_keep_running) {
		(void) ph_process();
		(void) sleep(ph_hdl->ph_interval);
	}
}

/*
 * Tell the administrator that they really should register ASR!
 */
static void
ph_nag()
{
	if (ph_hdl->ph_registered == B_TRUE)
		return;
	openlog("asr-notify", LOG_PID, LOG_DAEMON);
	syslog(LOG_WARNING,
	    "System is not registered with Auto Service Request. "
	    "Please register system using asradm(1M) or visit "
	    "http://www.oracle.com/asr for more information.");
	closelog();
}

/*
 * The phone home service CLI entry point
 */
int
ph_main(int argc, char ** argv)
{
	int err = 0;
	int c;

	while (err == 0 && (c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		case 'd':
			opt_d = B_TRUE;
			break;
		case 'f':
			opt_f = B_TRUE;
			break;
		case 'c':
			opt_c = optarg;
			break;
		case 'R':
			opt_R = optarg;
			break;
		case ':':
			(void) fprintf(stderr,
			    "Missing value for option (%c)\n", optopt);
			err++;
			break;
		case '?':
		default:
			(void) fprintf(stderr,
			    "Unrecognized option (%c)\n", optopt);
			err++;
		}
	}

	if (err) {
		ph_usage(argv[0]);
		return (SMF_EXIT_ERR_CONFIG);
	}

	if ((ph_hdl = ph_init()) == NULL) {
		(void) fprintf(stderr,
		    "Failed to initialize phone home service.\n");
		return (SMF_EXIT_ERR_FATAL);
	}

	if ((err = ph_runtime_init(ph_hdl)) != 0) {
		ph_fini(ph_hdl);
		return (err);
	}

	ph_nag();
	ph_run();
	ph_fini(ph_hdl);

	return (SMF_EXIT_OK);
}
