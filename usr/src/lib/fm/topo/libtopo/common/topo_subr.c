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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <alloca.h>
#include <ctype.h>
#include <limits.h>
#include <syslog.h>
#include <strings.h>
#include <unistd.h>

#include <topo_error.h>
#include <topo_subr.h>

void
topo_hdl_lock(topo_hdl_t *thp)
{
	(void) pthread_mutex_lock(&thp->th_lock);
}

void
topo_hdl_unlock(topo_hdl_t *thp)
{
	(void) pthread_mutex_unlock(&thp->th_lock);
}

const char *
topo_stability2name(topo_stability_t s)
{
	switch (s) {
	case TOPO_STABILITY_INTERNAL:	return (TOPO_STABSTR_INTERNAL);
	case TOPO_STABILITY_PRIVATE:	return (TOPO_STABSTR_PRIVATE);
	case TOPO_STABILITY_OBSOLETE:	return (TOPO_STABSTR_OBSOLETE);
	case TOPO_STABILITY_EXTERNAL:	return (TOPO_STABSTR_EXTERNAL);
	case TOPO_STABILITY_UNSTABLE:	return (TOPO_STABSTR_UNSTABLE);
	case TOPO_STABILITY_EVOLVING:	return (TOPO_STABSTR_EVOLVING);
	case TOPO_STABILITY_STABLE:	return (TOPO_STABSTR_STABLE);
	case TOPO_STABILITY_STANDARD:	return (TOPO_STABSTR_STANDARD);
	default:			return (TOPO_STABSTR_UNKNOWN);
	}
}

topo_stability_t
topo_name2stability(const char *name)
{
	if (strcmp(name, TOPO_STABSTR_INTERNAL) == 0)
		return (TOPO_STABILITY_INTERNAL);
	else if (strcmp(name, TOPO_STABSTR_PRIVATE) == 0)
		return (TOPO_STABILITY_PRIVATE);
	else if (strcmp(name, TOPO_STABSTR_OBSOLETE) == 0)
		return (TOPO_STABILITY_OBSOLETE);
	else if (strcmp(name, TOPO_STABSTR_EXTERNAL) == 0)
		return (TOPO_STABILITY_EXTERNAL);
	else if (strcmp(name, TOPO_STABSTR_UNSTABLE) == 0)
		return (TOPO_STABILITY_UNSTABLE);
	else if (strcmp(name, TOPO_STABSTR_EVOLVING) == 0)
		return (TOPO_STABILITY_EVOLVING);
	else if (strcmp(name, TOPO_STABSTR_STABLE) == 0)
		return (TOPO_STABILITY_STABLE);
	else if (strcmp(name, TOPO_STABSTR_STANDARD) == 0)
		return (TOPO_STABILITY_STANDARD);

	return (TOPO_STABILITY_UNKNOWN);
}

static const topo_debug_mode_t _topo_dbout_modes[] = {
	{ "stderr", "send debug messages to stderr", TOPO_DBOUT_STDERR },
	{ "syslog", "send debug messages to syslog", TOPO_DBOUT_SYSLOG },
	{ NULL, NULL, 0 }
};

static const topo_debug_mode_t _topo_dbflag_modes[] = {
	{ "error", "error handling debug messages enabled", TOPO_DBG_ERR },
	{ "module", "module debug messages enabled", TOPO_DBG_MOD },
	{ "modulesvc", "module services debug messages enabled",
	    TOPO_DBG_MODSVC },
	{ "walk", "walker subsystem debug messages enabled", TOPO_DBG_WALK },
	{ "xml", "xml file parsing messages enabled", TOPO_DBG_XML },
	{ "devinfoforce", "devinfo DINFOFORCE snapshot used", TOPO_DBG_FORCE },
	{ "snap", "snapshot messages enabled", TOPO_DBG_SNAP },
	{ "uuid", "use uuid_generate_time()", TOPO_DBG_UUID },
	{ "all", "all debug modes enabled", TOPO_DBG_ALL},
	{ NULL, NULL, 0 }
};

void
env_process_value(topo_hdl_t *thp, const char *begin, const char *end)
{
	char buf[MAXNAMELEN];
	size_t count;
	topo_debug_mode_t *dbp;

	while (begin < end && isspace(*begin))
		begin++;

	while (begin < end && isspace(*(end - 1)))
		end--;

	if (begin >= end)
		return;

	count = end - begin;
	count += 1;

	if (count > sizeof (buf))
		return;

	(void) snprintf(buf, count, "%s", begin);

	for (dbp = (topo_debug_mode_t *)_topo_dbflag_modes;
	    dbp->tdm_name != NULL; ++dbp) {
		if (strcmp(buf, dbp->tdm_name) == 0)
			thp->th_debug |= dbp->tdm_mode;
	}
}

void
topo_debug_set(topo_hdl_t *thp, const char *dbmode, const char *dout)
{
	char *end, *value, *next;
	topo_debug_mode_t *dbp;

	topo_hdl_lock(thp);
	value = (char *)dbmode;

	for (end = (char *)dbmode; *end != '\0'; value = next) {
		end = strchr(value, ',');
		if (end != NULL)
			next = end + 1;	/* skip the comma */
		else
			next = end = value + strlen(value);

		env_process_value(thp, value, end);
	}

	if (dout == NULL) {
		topo_hdl_unlock(thp);
		return;
	}

	for (dbp = (topo_debug_mode_t *)_topo_dbout_modes;
	    dbp->tdm_name != NULL; ++dbp) {
		if (strcmp(dout, dbp->tdm_name) == 0)
			thp->th_dbout = dbp->tdm_mode;
	}
	topo_hdl_unlock(thp);
}

void
topo_vdprintf(topo_hdl_t *thp, int mask, const char *mod, const char *format,
    va_list ap)
{
	char *msg;
	size_t len;
	char c;

	if (!(thp->th_debug & mask))
		return;

	len = vsnprintf(&c, 1, format, ap);
	msg = alloca(len + 2);
	(void) vsnprintf(msg, len + 1, format, ap);

	if (msg[len - 1] != '\n')
		(void) strcpy(&msg[len], "\n");

	if (thp->th_dbout == TOPO_DBOUT_SYSLOG) {
		if (mod == NULL) {
			syslog(LOG_DEBUG | LOG_USER, "libtopo DEBUG: %s", msg);
		} else {
			syslog(LOG_DEBUG | LOG_USER, "libtopo DEBUG: %s: %s",
			    mod, msg);
		}
	} else {
		if (mod == NULL) {
			(void) fprintf(stderr, "libtopo DEBUG: %s", msg);
		} else {
			(void) fprintf(stderr, "libtopo DEBUG: %s: %s", mod,
			    msg);
		}
	}
}

/*PRINTFLIKE3*/
void
topo_dprintf(topo_hdl_t *thp, int mask, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	topo_vdprintf(thp, mask, NULL, format, ap);
	va_end(ap);
}

tnode_t *
topo_hdl_root(topo_hdl_t *thp, const char *scheme)
{
	ttree_t *tp;

	for (tp = topo_list_next(&thp->th_trees); tp != NULL;
	    tp = topo_list_next(tp)) {
		if (strcmp(scheme, tp->tt_scheme) == 0)
			return (tp->tt_root);
	}

	return (NULL);
}

/*
 * buf_append -- Append str to buf (if it's non-NULL).  Place prepend
 * in buf in front of str and append behind it (if they're non-NULL).
 * Continue to update size even if we run out of space to actually
 * stuff characters in the buffer.
 */
void
topo_fmristr_build(ssize_t *sz, char *buf, size_t buflen, char *str,
    char *prepend, char *append)
{
	ssize_t left;

	if (str == NULL)
		return;

	if (buflen == 0 || (left = buflen - *sz) < 0)
		left = 0;

	if (buf != NULL && left != 0)
		buf += *sz;

	if (prepend == NULL && append == NULL)
		*sz += snprintf(buf, left, "%s", str);
	else if (append == NULL)
		*sz += snprintf(buf, left, "%s%s", prepend, str);
	else if (prepend == NULL)
		*sz += snprintf(buf, left, "%s%s", str, append);
	else
		*sz += snprintf(buf, left, "%s%s%s", prepend, str, append);
}

#define	TOPO_PLATFORM_PATH	"%s/usr/platform/%s/lib/fm/topo/%s"
#define	TOPO_COMMON_PATH	"%s/usr/lib/fm/topo/%s"

char *
topo_search_path(topo_mod_t *mod, const char *file)
{
	char *pp, sp[PATH_MAX];
	topo_hdl_t *thp = mod->tm_hdl;

	topo_dprintf(mod->tm_hdl, TOPO_DBG_SNAP,
	    "%s: mod=0x%p, file=%s, th_rootdir=%s\n",
	    __func__, (void *)mod, file, thp->th_rootdir);

	/*
	 * Search for file name in order of platform, machine and common
	 * topo directories
	 */
	(void) snprintf(sp, PATH_MAX, TOPO_PLATFORM_PATH, thp->th_rootdir,
	    thp->th_platform, file);
	if (access(sp, F_OK) != 0) {
		(void) snprintf(sp, PATH_MAX, TOPO_PLATFORM_PATH,
		    thp->th_rootdir, thp->th_machine, file);
		if (access(sp, F_OK) != 0) {
			(void) snprintf(sp, PATH_MAX, TOPO_COMMON_PATH,
			    thp->th_rootdir, file);
			if (access(sp, F_OK) != 0) {
				return (NULL);
			}
		}
	}

	pp = topo_mod_strdup(mod, sp);

	return (pp);
}

/*
 * SMBIOS serial numbers can contain characters (particularly ':' and ' ')
 * that are invalid for the authority and can break FMRI parsing.  We translate
 * any invalid characters to a safe '-', as well as trimming any leading or
 * trailing whitespace.  Similarly, '/' can be found in some product names
 * so we translate that to '-'.
 */
char *
topo_cleanup_auth_str(topo_hdl_t *thp, const char *begin)
{
	char buf[MAXNAMELEN];
	const char *end, *cp;
	char *pp;
	char c;
	int i;

	end = begin + strlen(begin);

	while (begin < end && isspace(*begin))
		begin++;
	while (begin < end && isspace(*(end - 1)))
		end--;

	if (begin >= end)
		return (NULL);

	cp = begin;
	for (i = 0; i < MAXNAMELEN - 1; i++) {
		if (cp >= end)
			break;
		c = *cp;
		if (c == ':' || c == '=' || c == '/' || isspace(c) ||
		    !isprint(c))
			buf[i] = '-';
		else
			buf[i] = c;
		cp++;
	}
	buf[i] = 0;

	pp = topo_hdl_strdup(thp, buf);
	return (pp);
}

void
topo_sensor_type_name(uint32_t type, char *buf, size_t len)
{
	topo_name_trans_t *ntp;

	for (ntp = &topo_sensor_type_table[0]; ntp->int_name != NULL; ntp++) {
		if (ntp->int_value == type) {
			(void) strlcpy(buf, ntp->int_name, len);
			return;
		}
	}

	(void) snprintf(buf, len, "0x%02x", type);
}

void
topo_sensor_units_name(uint8_t type, char *buf, size_t len)
{
	topo_name_trans_t *ntp;

	for (ntp = &topo_units_type_table[0]; ntp->int_name != NULL; ntp++) {
		if (ntp->int_value == type) {
			(void) strlcpy(buf, ntp->int_name, len);
			return;
		}
	}

	(void) snprintf(buf, len, "0x%02x", type);
}

void
topo_led_type_name(uint8_t type, char *buf, size_t len)
{
	topo_name_trans_t *ntp;

	for (ntp = &topo_led_type_table[0]; ntp->int_name != NULL; ntp++) {
		if (ntp->int_value == type) {
			(void) strlcpy(buf, ntp->int_name, len);
			return;
		}
	}

	(void) snprintf(buf, len, "0x%02x", type);
}

void
topo_led_state_name(uint8_t type, char *buf, size_t len)
{
	topo_name_trans_t *ntp;

	for (ntp = &topo_led_states_table[0]; ntp->int_name != NULL; ntp++) {
		if (ntp->int_value == type) {
			(void) strlcpy(buf, ntp->int_name, len);
			return;
		}
	}

	(void) snprintf(buf, len, "0x%02x", type);
}

void
topo_sensor_state_name(uint32_t sensor_type, uint8_t state, char *buf,
size_t len)
{
	topo_name_trans_t *ntp;

	switch (sensor_type) {
		case TOPO_SENSOR_TYPE_PHYSICAL:
			ntp = &topo_sensor_states_physical_table[0];
			break;
		case TOPO_SENSOR_TYPE_PLATFORM:
			ntp = &topo_sensor_states_platform_table[0];
			break;
		case TOPO_SENSOR_TYPE_PROCESSOR:
			ntp = &topo_sensor_states_processor_table[0];
			break;
		case TOPO_SENSOR_TYPE_POWER_SUPPLY:
			ntp = &topo_sensor_states_power_supply_table[0];
			break;
		case TOPO_SENSOR_TYPE_POWER_UNIT:
			ntp = &topo_sensor_states_power_unit_table[0];
			break;
		case TOPO_SENSOR_TYPE_MEMORY:
			ntp = &topo_sensor_states_memory_table[0];
			break;
		case TOPO_SENSOR_TYPE_BAY:
			ntp = &topo_sensor_states_bay_table[0];
			break;
		case TOPO_SENSOR_TYPE_FIRMWARE:
			ntp = &topo_sensor_states_firmware_table[0];
			break;
		case TOPO_SENSOR_TYPE_EVENT_LOG:
			ntp = &topo_sensor_states_event_log_table[0];
			break;
		case TOPO_SENSOR_TYPE_WATCHDOG1:
			ntp = &topo_sensor_states_watchdog1_table[0];
			break;
		case TOPO_SENSOR_TYPE_SYSTEM:
			ntp = &topo_sensor_states_system_table[0];
			break;
		case TOPO_SENSOR_TYPE_CRITICAL:
			ntp = &topo_sensor_states_critical_table[0];
			break;
		case TOPO_SENSOR_TYPE_BUTTON:
			ntp = &topo_sensor_states_button_table[0];
			break;
		case TOPO_SENSOR_TYPE_CABLE:
			ntp = &topo_sensor_states_cable_table[0];
			break;
		case TOPO_SENSOR_TYPE_BOOT_STATE:
			ntp = &topo_sensor_states_boot_state_table[0];
			break;
		case TOPO_SENSOR_TYPE_BOOT_ERROR:
			ntp = &topo_sensor_states_boot_error_table[0];
			break;
		case TOPO_SENSOR_TYPE_BOOT_OS:
			ntp = &topo_sensor_states_boot_os_table[0];
			break;
		case TOPO_SENSOR_TYPE_OS_SHUTDOWN:
			ntp = &topo_sensor_states_os_table[0];
			break;
		case TOPO_SENSOR_TYPE_SLOT:
			ntp = &topo_sensor_states_slot_table[0];
			break;
		case TOPO_SENSOR_TYPE_ACPI:
			ntp = &topo_sensor_states_acpi_table[0];
			break;
		case TOPO_SENSOR_TYPE_WATCHDOG2:
			ntp = &topo_sensor_states_watchdog2_table[0];
			break;
		case TOPO_SENSOR_TYPE_ALERT:
			ntp = &topo_sensor_states_alert_table[0];
			break;
		case TOPO_SENSOR_TYPE_PRESENCE:
			ntp = &topo_sensor_states_presence_table[0];
			break;
		case TOPO_SENSOR_TYPE_LAN:
			ntp = &topo_sensor_states_lan_table[0];
			break;
		case TOPO_SENSOR_TYPE_HEALTH:
			ntp = &topo_sensor_states_health_table[0];
			break;
		case TOPO_SENSOR_TYPE_BATTERY:
			ntp = &topo_sensor_states_battery_table[0];
			break;
		case TOPO_SENSOR_TYPE_AUDIT:
			ntp = &topo_sensor_states_audit_table[0];
			break;
		case TOPO_SENSOR_TYPE_VERSION:
			ntp = &topo_sensor_states_version_table[0];
			break;
		case TOPO_SENSOR_TYPE_FRU_STATE:
			ntp = &topo_sensor_states_fru_state_table[0];
			break;
		case TOPO_SENSOR_TYPE_THRESHOLD_STATE:
			ntp = &topo_sensor_states_thresh_table[0];
			break;
		case TOPO_SENSOR_TYPE_GENERIC_USAGE:
			ntp = &topo_sensor_states_generic_usage_table[0];
			break;
		case TOPO_SENSOR_TYPE_GENERIC_STATE:
			ntp = &topo_sensor_states_generic_state_table[0];
			break;
		case TOPO_SENSOR_TYPE_GENERIC_PREDFAIL:
			ntp = &topo_sensor_states_generic_predfail_table[0];
			break;
		case TOPO_SENSOR_TYPE_GENERIC_LIMIT:
			ntp = &topo_sensor_states_generic_limit_table[0];
			break;
		case TOPO_SENSOR_TYPE_GENERIC_PERFORMANCE:
			ntp = &topo_sensor_states_generic_perf_table[0];
			break;
		case TOPO_SENSOR_TYPE_SEVERITY:
			ntp = &topo_sensor_states_severity_table[0];
			break;
		case TOPO_SENSOR_TYPE_GENERIC_PRESENCE:
			ntp = &topo_sensor_states_generic_presence_table[0];
			break;
		case TOPO_SENSOR_TYPE_GENERIC_AVAILABILITY:
			ntp = &topo_sensor_states_generic_avail_table[0];
			break;
		case TOPO_SENSOR_TYPE_GENERIC_STATUS:
			ntp = &topo_sensor_states_generic_status_table[0];
			break;
		case TOPO_SENSOR_TYPE_GENERIC_ACPI:
			ntp = &topo_sensor_states_generic_acpi_pwr_table[0];
			break;
		case TOPO_SENSOR_TYPE_GENERIC_FAILURE:
			ntp = &topo_sensor_states_generic_failure_table[0];
			break;
		case TOPO_SENSOR_TYPE_GENERIC_OK:
			ntp = &topo_sensor_states_generic_ok_table[0];
			break;
		default:
			(void) snprintf(buf, len, "0x%02x", state);
			return;
	}
	for (; ntp->int_name != NULL; ntp++) {
		if (ntp->int_value == state) {
			(void) strlcpy(buf, ntp->int_name, len);
			return;
		}
	}

	(void) snprintf(buf, len, "0x%02x", state);
}

time_t
topo_hdl_timestamp(topo_hdl_t *thp)
{
	return (thp->th_timestamp);
}

char *
topo_hdl_uuid(topo_hdl_t *thp)
{
	return (thp->th_uuid);
}

boolean_t
topo_is_excluded(const char *envvarname, const char *name)
{
	char *value, *next, *end, *valend;
	char *excluded = getenv(envvarname);
	char s[MAXNAMELEN];

	if (excluded == NULL)
		return (B_FALSE);

	value = excluded;
	for (end = excluded; *end != '\0'; value = next) {
		end = strchr(value, ',');
		if (end != NULL)
			next = end + 1; /* skip the comma */
		else
			next = end = value + strlen(value);

		/* Remove whitespace */
		valend = end - 1;
		while (isspace(*value))
			value++;
		while (isspace(*valend))
			valend--;

		(void) strlcpy(s, value, valend - value + 2);
		if (strcmp(name, s) == 0)
			return (B_TRUE);
	}
	return (B_FALSE);
}
