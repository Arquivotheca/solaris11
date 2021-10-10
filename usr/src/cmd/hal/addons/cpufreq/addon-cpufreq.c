/*
 * Licensed under the Academic Free License version 2.1
 */

/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * addon-cpufreq.c : Routines to support CPUFreq interface
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/dkio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <dbus/dbus-glib.h>
#include <priv.h>
#include <pwd.h>

#include <syslog.h>

#include <libhal.h>
#include "../../hald/logger.h"
#include "../../utils/adt_data.h"

#include <pwd.h>
#ifdef HAVE_POLKIT
#include <libpolkit.h>
#endif

#ifdef sun
#include <bsm/adt.h>
#include <bsm/adt_event.h>
#include <sys/pm.h>
#endif

#include <libnvpair.h>
#include <libpower.h>

#define	DEV_PM "/dev/pm"

#define	FILE_ARR_SIZE 256
#define	EDIT_TYPE_SIZE 64
#define	ERR_BUF_SIZE 256

#define	WAIT_TIME 30


const char *sender;
unsigned long 	uid;

/*
 * Specify different CPUFreq related HAL activities that can be done
 */
enum hal_type {
	CPU_GOV,
	CPU_PERFORMANCE		/* Obsolete */
};
typedef enum hal_type power_conf_hal_type;

/*
 * Various CPUFreq related editable parameters in the power.conf file
 */
typedef struct {
	char	cpu_gov[EDIT_TYPE_SIZE];
	int	cpu_th;
} pconf_edit_type;

/*
 * CPUFreq interospect XML that exports the various CPUFreq HAL interface
 * supported methods
 */
/* BEGIN CSTYLED */
const char *cpufreq_introspect_xml = \
	"	<method name= \"SetCPUFreqGovernor\">\n \
		<arg type= \"s\" name= \"governor\" direction= \"in\"/>\n \
	</method>\n \
	<method name= \"GetCPUFreqGovernor\">\n \
		<type= \"s\" direction= \"out\"/>\n \
	</method>\n \
	<method name= \"GetCPUFreqAvailableGovernors\">\n \
		<type=\"s\" direction=\"out\"/>\n \
	</method>\n";
/* END CSTYLED */


/*
 * List of governors that are currently supported
 */
char *const gov_list[] = {
	"ondemand",
	"performance",
	NULL
};

static char current_gov[EDIT_TYPE_SIZE];


/*
 * Local function declarations
 */
static void check_and_free_error(DBusError *);	/* *error */
static void drop_privileges(void);
static DBusHandlerResult hald_dbus_cpufreq_filter(
			    DBusConnection *	/* *con */,
			    DBusMessage *	/* *msg */,
			    void * 		/* *udata */
);
static void set_cpufreq_gov(DBusConnection *	/* *con */,
			    DBusMessage *	/* *msg */,
			    void * 		/* *udata */
);
static void get_cpufreq_gov(DBusConnection *	/* *con */,
			    DBusMessage *	/* *msg */,
			    void * 		/* *udata */
);
static void get_cpufreq_avail_gov(
			    DBusConnection *	/* *con */,
			    DBusMessage *	/* *msg */,
			    void * 		/* *udata */
);
static int cpufreq_apply(char *);
static int cpupm_enable(boolean_t);

int
main(int argc, char **argv)
{

	LibHalContext *ctx = NULL;
	char *udi;
	DBusError error;
	DBusConnection *conn;

	GMainLoop *loop = g_main_loop_new(NULL, FALSE);

	drop_privileges();
	openlog("hald-addon-cpufreq", LOG_PID, LOG_DAEMON);
	setup_logger();

	bzero(current_gov, EDIT_TYPE_SIZE-1);

	if ((udi = getenv("UDI")) == NULL) {
		HAL_INFO(("\n Could not get the UDI in addon-cpufreq"));
		return (0);
	}

	dbus_error_init(&error);
	if ((ctx = libhal_ctx_init_direct(&error)) == NULL) {
		HAL_ERROR(("main(): init_direct failed\n"));
		return (0);
	}
	dbus_error_init(&error);
	if (!libhal_device_addon_is_ready(ctx, getenv("UDI"), &error)) {
		check_and_free_error(&error);
		return (0);
	}

	/*
	 * Claim the cpufreq interface
	 */

	HAL_DEBUG(("cpufreq Introspect XML: %s", cpufreq_introspect_xml));

	if (!libhal_device_claim_interface(ctx, udi,
	    "org.freedesktop.Hal.Device.CPUFreq",
	    cpufreq_introspect_xml, &error)) {
		HAL_DEBUG((" Cannot claim the CPUFreq interface"));
		check_and_free_error(&error);
		return (0);
	}

	conn = libhal_ctx_get_dbus_connection(ctx);

	/*
	 * Add the cpufreq capability
	 */
	if (!libhal_device_add_capability(ctx, udi,
	    "cpufreq_control", &error)) {
		HAL_DEBUG((" Could not add cpufreq_control capability"));
		check_and_free_error(&error);
		return (0);
	}
	/*
	 * Watches and times incoming messages
	 */

	dbus_connection_setup_with_g_main(conn, NULL);

	/*
	 * Add a filter function which gets called when a message comes in
	 * and processes the message
	 */

	if (!dbus_connection_add_filter(conn, hald_dbus_cpufreq_filter,
	    NULL, NULL)) {
		HAL_INFO((" Cannot add the CPUFreq filter function"));
		return (0);
	}

	dbus_connection_set_exit_on_disconnect(conn, 0);

	g_main_loop_run(loop);
}

static DBusHandlerResult
hald_dbus_cpufreq_filter(DBusConnection *con, DBusMessage *msg, void *udata)
{
	HAL_DEBUG((" Inside CPUFreq filter:%s", dbus_message_get_path(msg)));
	/*
	 * Check for method types
	 */
	if (!dbus_connection_get_is_connected(con))
		HAL_DEBUG(("Connection disconnected in cpufreq addon"));

	if (dbus_message_is_method_call(msg,
	    "org.freedesktop.Hal.Device.CPUFreq",
	    "SetCPUFreqGovernor")) {
		HAL_DEBUG(("---- SetCPUFreqGovernor is called "));

		set_cpufreq_gov(con, msg, udata);

	} else if (dbus_message_is_method_call(msg,
	    "org.freedesktop.Hal.Device.CPUFreq",
	    "GetCPUFreqGovernor")) {
		HAL_DEBUG(("---- GetCPUFreqGovernor is called "));

		get_cpufreq_gov(con, msg, udata);
	} else if (dbus_message_is_method_call(msg,
	    "org.freedesktop.Hal.Device.CPUFreq",
	    "GetCPUFreqAvailableGovernors")) {
		HAL_DEBUG(("---- GetCPUFreqAvailableGovernors is called "));

		get_cpufreq_avail_gov(con, msg, udata);
	} else {
		HAL_DEBUG(("---Not Set/Get cpufreq gov---"));
	}


	return (DBUS_HANDLER_RESULT_HANDLED);

}

/*
 * Free up the mem allocated to hold the DBusError
 */
static void
check_and_free_error(DBusError *error)
{
	if (dbus_error_is_set(error)) {
		dbus_error_free(error);
	}
}

/*
 * Depending on the type(Governor or Performance) to read, get the current
 * values through PM ioctls().
 * For "Governor", return the cpupm state and for "Performance" return the
 * current cpu threshold.
 * Return the corresponding value through cur_value and return 1 from the
 * function for success. Return -1 on error
 */

static int
get_cur_val(pconf_edit_type *cur_value,
    power_conf_hal_type pc_hal_type)
{

	int pm_fd;
	int res = -1;
	int pm_ret;

	pm_fd = open(DEV_PM, O_RDONLY);
	if (pm_fd == -1) {
		HAL_ERROR(("Error opening %s: %s \n", DEV_PM,
		    strerror(errno)));
		return (res);
	}

	switch (pc_hal_type) {
	case CPU_GOV:
		/*
		 * First check the PM_GET_CPUPM_STATE. If it is not available
		 * then check PM_GET_PM_STATE
		 */
		pm_ret = ioctl(pm_fd, PM_GET_CPUPM_STATE);
		if (pm_ret < 0) {
			HAL_ERROR(("Error in ioctl PM_GET_CPUPM_STATE: %s \n",
			    strerror(errno)));
			goto out;
		}
		switch (pm_ret) {
		case PM_CPU_PM_ENABLED:
			sprintf(cur_value->cpu_gov, "%s", "ondemand");
			res = 1;
			goto out;
		case PM_CPU_PM_DISABLED:
			sprintf(cur_value->cpu_gov, "%s", "performance");
			res = 1;
			goto out;
		case PM_CPU_PM_NOTSET:
			/*
			 * Check for PM_GET_PM_STATE
			 */
			pm_ret = ioctl(pm_fd, PM_GET_PM_STATE);
			if (pm_ret < 0) {
				HAL_ERROR(("Error in ioctl PM_GET_PM_STATE: "
				    "%s", strerror(errno)));
				goto out;
			}
			switch (pm_ret) {
			case PM_SYSTEM_PM_ENABLED:
				sprintf(cur_value->cpu_gov, "%s", "ondemand");
				res = 1;
				goto out;
			case PM_SYSTEM_PM_DISABLED:
				sprintf(cur_value->cpu_gov, "%s",
				    "performance");
				res = 1;
				goto out;
			default:
				HAL_ERROR(("PM Internal error during ioctl "
				    "PM_GET_PM_STATE"));
				goto out;
			}
		default:
			HAL_ERROR(("Unknown value ioctl PM_GET_CPUPM_STATE"));
			goto out;
		}
	default :
		HAL_DEBUG(("Cannot recognize the HAL type to get value"));
		goto out;
	}
out:
	close(pm_fd);
	return (res);
}
/*
 * Send an error message as a response to the pending call
 */
static void
generate_err_msg(DBusConnection *con,
    DBusMessage *msg,
    const char *err_name,
    char *fmt, ...)
{

	DBusMessage	*err_msg;
	char		err_buf[ERR_BUF_SIZE];
	va_list		va_args;

	va_start(va_args, fmt);
	vsnprintf(err_buf, ERR_BUF_SIZE, fmt, va_args);
	va_end(va_args);

	HAL_DEBUG((" Sending error message: %s", err_buf));

	err_msg = dbus_message_new_error(msg, err_name, err_buf);
	if (err_msg == NULL) {
		HAL_ERROR(("No Memory for DBUS error msg"));
		return;
	}

	if (!dbus_connection_send(con, err_msg, NULL)) {
		HAL_ERROR((" Out Of Memory!"));
	}
	dbus_connection_flush(con);

}

static void
gen_unknown_gov_err(DBusConnection *con,
    DBusMessage *msg,
    char *err_str)
{

	generate_err_msg(con, msg,
	    "org.freedesktop.Hal.CPUFreq.UnknownGovernor",
	    "Unknown CPUFreq Governor: %s", err_str);
}

static void
gen_no_suitable_gov_err(DBusConnection *con,
    DBusMessage *msg,
    char *err_str)
{

	generate_err_msg(con, msg,
	    "org.freedesktop.Hal.CPUFreq.NoSuitableGovernor",
	    "Could not find a suitable governor: %s", err_str);
}

static void
gen_cpufreq_err(DBusConnection *con,
    DBusMessage *msg,
    char *err_str)
{
	generate_err_msg(con, msg,
	    "org.freedesktop.Hal.CPUFreq.Error",
	    "%s: Syslog might give more information", err_str);
}


/*
 * Puts the required cpufreq audit data and calls adt_put_event()
 * to generate auditing
 */
static void
audit_cpufreq(const adt_export_data_t *imported_state, au_event_t event_id,
    int result, const char *auth_used, const int cpu_thr_value)
{
	adt_session_data_t	*ah;
	adt_event_data_t	*event;
	struct passwd		*msg_pwd;
	uid_t			gid;

	if (adt_start_session(&ah, imported_state, 0) != 0) {
		HAL_INFO(("adt_start_session failed: %s", strerror(errno)));
		return;
	}

	if ((event = adt_alloc_event(ah, event_id)) == NULL) {
		HAL_INFO(("adt_alloc_event audit_cpufreq failed: %s",
		    strerror(errno)));
		return;
	}

	switch (event_id) {
	case ADT_cpu_ondemand:
		event->adt_cpu_ondemand.auth_used = (char *)auth_used;
		break;
	case ADT_cpu_performance:
		event->adt_cpu_performance.auth_used = (char *)auth_used;
		break;
	case ADT_cpu_threshold:
		event->adt_cpu_threshold.auth_used = (char *)auth_used;
		event->adt_cpu_threshold.threshold = cpu_thr_value;
		break;
	default:
		goto clean;
	}

	if (result == 0) {
		if (adt_put_event(event, ADT_SUCCESS, ADT_SUCCESS) != 0) {
			HAL_INFO(("adt_put_event(%d, ADT_SUCCESS) failed",
			    event_id));
		}
	} else {
		if (adt_put_event(event, ADT_FAILURE, result) != 0) {
			HAL_INFO(("adt_put_event(%d, ADT_FAILURE) failed",
			    event_id));
		}
	}

clean:
	adt_free_event(event);
	(void) adt_end_session(ah);
}

/*
 * Check if the cpufreq related operations are authorized
 */

static int
check_authorization(DBusConnection *con, DBusMessage *msg)
{
	int		adt_res = 0;
#ifdef HAVE_POLKIT
	char		user_id[128];
	char		*udi;
	char		*privilege;
	DBusError	error;
	gboolean	is_priv_allowed;
	gboolean	is_priv_temporary;
	DBusConnection	*system_bus = NULL;
	LibPolKitContext *pol_ctx = NULL;

	/*
	 * Check for authorization before proceeding
	 */
	udi = getenv("HAL_PROP_INFO_UDI");
	privilege = "hal-power-cpu";

	dbus_error_init(&error);
	system_bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (system_bus == NULL) {
		HAL_INFO(("Cannot connect to the system bus"));
		LIBHAL_FREE_DBUS_ERROR(&error);
		gen_cpufreq_err(con, msg, "Cannot connect to the system bus");
		adt_res = EINVAL;
		goto out;
	}

	sender = dbus_message_get_sender(msg);
	HAL_INFO(("Auth Sender: %s", sender));

	if (sender == NULL) {
		HAL_INFO(("Could not get the sender of the message"));
		gen_cpufreq_err(con, msg,
		    "Could not get the sender of the message");
		adt_res = ADT_FAIL_VALUE_AUTH;
		goto out;
	}

	dbus_error_init(&error);
	uid = dbus_bus_get_unix_user(system_bus, sender, &error);
	if (dbus_error_is_set(&error)) {
		HAL_INFO(("Could not get the user id of the message"));
		LIBHAL_FREE_DBUS_ERROR(&error);
		gen_cpufreq_err(con, msg,
		    "Could not get the user id of the message sender");
		adt_res = ADT_FAIL_VALUE_AUTH;
		goto out;
	}

	snprintf(user_id, sizeof (user_id), "%d", uid);
	HAL_DEBUG((" User id is : %d", uid));

	pol_ctx = libpolkit_new_context(system_bus);
	if (pol_ctx == NULL) {
		HAL_INFO(("Cannot get libpolkit context"));
		gen_cpufreq_err(con, msg,
		    "Cannot get libpolkit context to check privileges");
		adt_res = ADT_FAIL_VALUE_AUTH;
		goto out;
	}

	if (libpolkit_is_uid_allowed_for_privilege(pol_ctx,
	    NULL, user_id, privilege, udi,
	    &is_priv_allowed, &is_priv_temporary,
	    NULL) != LIBPOLKIT_RESULT_OK) {
		HAL_INFO(("Cannot lookup privilege from PolicyKit"));
		gen_cpufreq_err(con, msg,
		    "Error looking up privileges from Policykit");
		adt_res = ADT_FAIL_VALUE_AUTH;
		goto out;
	}

	if (!is_priv_allowed) {
		HAL_INFO(("Caller doesn't possess required privilege to"
		    " change the governor"));
		gen_cpufreq_err(con, msg,
		    "Caller doesn't possess required "
		    "privilege to change the governor");
		adt_res = ADT_FAIL_VALUE_AUTH;
		goto out;
	}

	HAL_DEBUG((" Privilege Succeed"));

#endif
out:
	return (adt_res);
}

/*
 * Sets the CPU Freq governor. It sets the gov name in the /etc/power.conf
 * and executes pmconfig. If governor is "ondemand" then "cpupm" is enabled in
 * and if governor is performance, then "cpupm" is disabled
 */
static void
set_cpufreq_gov(DBusConnection *con, DBusMessage *msg, void *udata)
{
	DBusMessageIter arg_iter;
	DBusMessage	*msg_reply;
	char		*arg_val;
	int		arg_type;
	int		pid;
	int		done_flag = 0;
	int		sleep_time = 0;
	int		status;
	int		adt_res = 0;
#ifdef sun
	adt_export_data_t *adt_data;
	size_t		adt_data_size;
	DBusConnection	*system_bus = NULL;
	DBusError	error;
#endif

	if (! dbus_message_iter_init(msg, &arg_iter)) {
		HAL_DEBUG(("Incoming message has no arguments"));
		gen_unknown_gov_err(con, msg, "No governor specified");
		adt_res = EINVAL;
		goto out;
	}
	arg_type = dbus_message_iter_get_arg_type(&arg_iter);

	if (arg_type != DBUS_TYPE_STRING) {
		HAL_DEBUG(("Incoming message arg type is not string"));
		gen_unknown_gov_err(con, msg,
		    "Specified governor is not a string");
		adt_res = EINVAL;
		goto out;
	}
	dbus_message_iter_get_basic(&arg_iter, &arg_val);
	if (arg_val != NULL) {
		HAL_DEBUG(("SetCPUFreqGov is: %s", arg_val));
	} else {
		HAL_DEBUG(("Could not get SetCPUFreqGov from message iter"));
		adt_res = EINVAL;
		goto out;
	}

	adt_res = check_authorization(con, msg);

	if (adt_res != 0) {
		goto out;
	}


	/*
	 * Prior to PM 3.0 we used to employ power.conf and pmconfig(1m)
	 * but no longer do so.
	 *
	 * Nowadays we apply the appropriate responsiveness controls
	 * in (time-to-full-capacity) to effectuate 'performance'
	 * vs. 'ondemand'
	 */
	sprintf(current_gov, "%s", arg_val);
	cpufreq_apply(current_gov);    /* apply current CPUPM policy */


	/*
	 * Just return an empty response, so that if the client
	 * is waiting for any response will not keep waiting
	 */
	msg_reply = dbus_message_new_method_return(msg);
	if (msg_reply == NULL) {
		HAL_ERROR(("Out of memory to msg reply"));
		gen_cpufreq_err(con, msg,
		    "Out of memory to create a response");
		adt_res = ENOMEM;
		goto out;
	}

	if (!dbus_connection_send(con, msg_reply, NULL)) {
		HAL_ERROR(("Out of memory to msg reply"));
		gen_cpufreq_err(con, msg,
		    "Out of memory to create a response");
		adt_res = ENOMEM;
		goto out;
	}

	dbus_connection_flush(con);

out:

#ifdef sun
	/*
	 * Audit the new governor change
	 */
	dbus_error_init(&error);
	system_bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (system_bus == NULL) {
		HAL_INFO(("Cannot connect to the system bus %s",
		    error.message));
		LIBHAL_FREE_DBUS_ERROR(&error);
		return;
	}

	adt_data = get_audit_export_data(system_bus, sender, &adt_data_size);
	if (adt_data != NULL) {
		if (strcmp(arg_val, "ondemand") == 0) {
			audit_cpufreq(adt_data, ADT_cpu_ondemand, adt_res,
			    "solaris.system.power.cpu", 0);
		} else if (strcmp(arg_val, "performance") == 0) {
			audit_cpufreq(adt_data, ADT_cpu_performance, adt_res,
			    "solaris.system.power.cpu", 0);
		}
		free(adt_data);
	} else {
		HAL_INFO((" Could not get audit export data"));
	}
#endif /* sun */
}

/*
 * cpufreq_apply() -
 *	Affect the cpu pm policy (using the responsiveness abstraction)
 *
 * N.B. For the moment, CPU is the only category of resource that
 * is controlled by the responsiveness configurarion settings.
 *
 * Later on, when other resource-types are too, we will have to
 * rationalize the use of ttfc for CPU-relevant use with respect to
 * its use for the other resources.
 *
 * The current per-resource-area controls offered by gpm, and its
 * model is therefore too detailed for our new single responsiveness
 * abstraction, in which the responsiveness limit applies to
 * all of the system resource classes.
 */
static int cpufreq_apply(char *current_gov)
{
	int err;

	HAL_DEBUG((" cpufreq_apply: entry"));

	if (strcmp(current_gov, "ondemand") == 0) {
		HAL_DEBUG((" Current governor is: %s", current_gov));
		err = cpupm_enable(B_TRUE);

	} else if (strcmp(current_gov, "performance") == 0) {
		HAL_DEBUG((" Current governor is: %s", current_gov));
		err = cpupm_enable(B_FALSE);
	} else {
		HAL_ERROR((" cpufreq_apply: Unknown governor"));
		return (-1);
	}

	if (err != 0) {
		HAL_ERROR((" cpufreq_apply: cpupm_enable() failed %d",
		    err));
		return (-1);
	}
	return (1);
}

/*
 * cpupm_enable(boolean_t enable) -
 *
 *	Send the respective PM configuration controls to the system
 * using libpower(3m)
 *
 * We will either tighten or relax the responsiveness constraint:
 * time-to-full-capacity (ttfc) of the system, based on the given
 * CPUPM policy.
 *
 * If the policy is "performance" we allow 0 (uSec) for ttfc
 * If the policy is "ondemand" we allow 500 (uSec) for ttfc
 *
 * A time-to-minimum-responsiveness (ttmr) is also required to operate
 * the power service (when SMF is the authority).
 *
 * At present, we just set ttmr to zero, but more correctly,
 * iff ttmr is not set, we define it to be 0 (mSec)
 * so that all the required configuration parameters *are* defined.
 * (That is, determine whether ttmr is already defined, and then, only
 * if it is not, define it to be 0).
 */

#define	CPU_ONDEMAND_TTFC	"500"
#define	CPU_PERFORMANCE_TTFC	"0"
#define	CPU_ONDEMAND_TTMR	"0"

static int cpupm_enable(boolean_t enable)
{
	int		err = 0;
	pm_error_t	pm_err = PM_SUCCESS;
	char 		*name;
	char 		*value;
	nvlist_t	*nvl = NULL;

	HAL_DEBUG((" cpupm_enable: enable=%d", enable));

	/*
	 * Construct an nvlist with the required configuration parameters
	 */
	err = nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0);
	if (err != 0) {
		goto out;
	}

	if (enable)
		value = CPU_ONDEMAND_TTFC;	/* 500 uSec */
	else
		value = CPU_PERFORMANCE_TTFC;	/* 0 uSec */

	HAL_DEBUG((" cpupm_enable: TTFC=%s", value));

	/*
	 * Indicate that the Solaris instance (i.e. SMF), not the platform
	 * is now to be the administrative authority for power management
	 */
	err = nvlist_add_string(nvl,
	    PM_PGROUP_ACTIVE_CONTROL PM_SEP_STR PM_PROP_AUTHORITY, "smf");
	if (err != 0) {
		goto out;
	}

	/*
	 * Now set an appropriate responsiveness value for
	 * time-to-full-capacity, so that CPUPM will be enabled
	 */
	err = nvlist_add_string(nvl,
	    PM_PGROUP_ACTIVE_CONFIG PM_SEP_STR PM_PROP_TTFC, value);
	if (err != 0) {
		goto out;
	}

	/*
	 * Also set a value for time-to-minimum-responsiveness, so
	 * that all the required configuration parameters *are*
	 * defined.  for the the power service to be operated (when
	 * SMF is the authority).
	 *
	 * We could be smarter, and iff ttmr is not set, then define it
	 * to be 0 (mSec)
	 */
	err = nvlist_add_string(nvl,
	    PM_PGROUP_ACTIVE_CONFIG PM_SEP_STR PM_PROP_TTMR, "0");
	if (err != 0) {
		goto out;
	}

	/*
	 * Finally, invoke libpower to apply these configuration settings
	 */

	if (err == 0) {
		pm_err = pm_setprop(nvl);
		if (pm_err != PM_SUCCESS)
			err = -1;
	}

out:
	if (nvl != NULL)
		nvlist_free(nvl);
	return (err);
}


/*
 * Returns in the dbus message the current gov.
 */
static void
get_cpufreq_gov(DBusConnection *con, DBusMessage *msg, void *udata)
{

	DBusMessageIter rep_iter;
	DBusMessage	*msg_reply;
	int 		res;
	pconf_edit_type pc_type;
	char		*param;

	/*
	 * Get the governor type from /etc/power.conf if it is present.
	 */
	res = get_cur_val(&pc_type, CPU_GOV);
	if (res != 1) {
		HAL_INFO((" Error in getting the current governor"));
		gen_cpufreq_err(con, msg, "Internal error while getting"
		    " the governor");
		return;
	}

	HAL_DEBUG((" Current governor is: %s", pc_type.cpu_gov));

	msg_reply = dbus_message_new_method_return(msg);
	if (msg_reply == NULL) {
		HAL_ERROR(("Out of memory to msg reply"));
		gen_cpufreq_err(con, msg,
		    "Internal error while getting the governor");
		return;
	}

	/*
	 * Append reply arguments
	 */
	param = (char *) malloc(sizeof (char) * 250);
	if (param == NULL) {
		HAL_ERROR(("\n Could not allocate mem to param"));
		gen_cpufreq_err(con, msg, "Internal error while getting"
		    " the governor");
		return;
	}
	sprintf(param, "%s",  pc_type.cpu_gov);

	dbus_message_iter_init_append(msg_reply, &rep_iter);
	if (!dbus_message_iter_append_basic(&rep_iter, DBUS_TYPE_STRING,
	    &param)) {
		HAL_ERROR(("\n Out Of Memory!\n"));
		gen_cpufreq_err(con, msg, "Internal error while getting"
		    " the governor");
		free(param);
		return;
	}

	if (!dbus_connection_send(con, msg_reply, NULL)) {
		HAL_ERROR(("\n Out Of Memory!\n"));
		gen_cpufreq_err(con, msg, "Internal error while getting"
		    " the governor");
		free(param);
		return;
	}
	dbus_connection_flush(con);
	free(param);
}

/*
 * get_cpufreq_avail_gov() -
 *	Returns list of available governors. Currently just two governors are
 * supported. They are "ondemand" and "performance"
 */

static void
get_cpufreq_avail_gov(DBusConnection *con, DBusMessage *msg, void *udata)
{

	DBusMessageIter rep_iter;
	DBusMessageIter array_iter;
	DBusMessage	*msg_reply;
	int		ngov;

	msg_reply = dbus_message_new_method_return(msg);
	if (msg_reply == NULL) {
		HAL_ERROR(("Out of memory to msg reply"));
		gen_cpufreq_err(con, msg, "Internal error while getting"
		    " the list of governors");
		return;
	}

	/*
	 * Append reply arguments
	 */
	dbus_message_iter_init_append(msg_reply, &rep_iter);

	if (!dbus_message_iter_open_container(&rep_iter,
	    DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING, &array_iter)) {
		HAL_ERROR(("\n Out of memory to msg reply array"));
		gen_cpufreq_err(con, msg, "Internal error while getting"
		    " the list of governors");
		return;
	}

	for (ngov = 0; gov_list[ngov] != NULL; ngov++) {
		if (gov_list[ngov])
			HAL_DEBUG(("\n%d Gov Name: %s", ngov, gov_list[ngov]));
			dbus_message_iter_append_basic(&array_iter,
			    DBUS_TYPE_STRING, &gov_list[ngov]);
	}
	dbus_message_iter_close_container(&rep_iter, &array_iter);

	if (!dbus_connection_send(con, msg_reply, NULL)) {
		HAL_ERROR(("\n Out Of Memory!\n"));
		gen_cpufreq_err(con, msg, "Internal error while getting"
		    " the list of governors");
		return;
	}
	dbus_connection_flush(con);
}

static void
drop_privileges()
{
	priv_set_t *pPrivSet = NULL;
	priv_set_t *lPrivSet = NULL;

	/*
	 * Start with the 'basic' privilege set and then add any
	 * of the privileges that will be required.
	 */
	if ((pPrivSet = priv_str_to_set("basic", ",", NULL)) == NULL) {
		HAL_INFO(("Error in setting the priv"));
		return;
	}

	(void) priv_addset(pPrivSet, PRIV_SYS_DEVICES);

	if (setppriv(PRIV_SET, PRIV_INHERITABLE, pPrivSet) != 0) {
		HAL_INFO(("Could not set the privileges"));
		priv_freeset(pPrivSet);
		return;
	}

	(void) priv_addset(pPrivSet, PRIV_PROC_AUDIT);
	(void) priv_addset(pPrivSet, PRIV_SYS_CONFIG);

	if (setppriv(PRIV_SET, PRIV_PERMITTED, pPrivSet) != 0) {
		HAL_INFO(("Could not set the privileges"));
		priv_freeset(pPrivSet);
		return;
	}

	priv_freeset(pPrivSet);

}
