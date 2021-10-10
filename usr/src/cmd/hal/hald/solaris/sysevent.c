/***************************************************************************
 *
 * sysevent.c : Solaris sysevents
 *
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * Licensed under the Academic Free License version 2.1
 *
 **************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/dkio.h>
#include <sys/stat.h>
#include <libdevinfo.h>
#include <libsysevent.h>
#include <sys/sysevent/dev.h>
#include <sys/sysevent/pwrctl.h>
#include <sys/sysevent/dr.h>
#include <sys/pbio.h>
#include <glib.h>
#include <config_admin.h>
#include <kstat.h>

#include "../osspec.h"
#include "../logger.h"
#include "../hald.h"
#include "../hald_dbus.h"
#include "../device_info.h"
#include "../util.h"
#include "osspec_solaris.h"
#include "hotplug.h"
#include "devinfo.h"
#include "devinfo_storage.h"
#include "devinfo_acpi.h"
#include "devinfo_usb.h"
#include "sysevent.h"
#include "devinfo_misc.h"
#include "devinfo_cpu.h"

#ifndef ESC_LOFI
#define	ESC_LOFI "lofi"
#endif

static void	sysevent_dev_handler(sysevent_t *);
static gboolean sysevent_iochannel_data(GIOChannel *, GIOCondition, gpointer);
static void	sysevent_dev_add(gchar *, gchar *);
static void	sysevent_dev_remove(gchar *, gchar *);
static void	sysevent_dev_branch(gchar *);
static void	sysevent_lofi_add(gchar *, gchar *);
static void	sysevent_lofi_remove(gchar *, gchar *);
static void	sysevent_devfs_add(gchar *);
static void	sysevent_pwrctl(gchar *, gchar *, gchar *, gchar *, gchar *,
		    gchar *, uint_t);
static void	sysevent_process_dr(gchar *, gchar *);

static sysevent_handle_t	*shp;

static int sysevent_pipe_fds[2];
static GIOChannel *sysevent_iochannel;
static guint sysevent_source_id;

gboolean
sysevent_init(void)
{
	GError *err = NULL;
	const char	*subcl[6];
	int pb_fd;

	/*
	 * pipe used to serialize sysevents through the main loop
	 */
	if (pipe (sysevent_pipe_fds) != 0) {
		HAL_INFO (("pipe() failed errno=%d", errno));
		return (FALSE);
	}
	sysevent_iochannel = g_io_channel_unix_new (sysevent_pipe_fds[0]);
	if (sysevent_iochannel == NULL) {
		HAL_INFO (("g_io_channel_unix_new failed"));
		return (FALSE);
	}
	g_io_channel_set_flags (sysevent_iochannel, G_IO_FLAG_NONBLOCK, &err);
	sysevent_source_id = g_io_add_watch (
	    sysevent_iochannel, G_IO_IN, sysevent_iochannel_data, NULL);

	shp = sysevent_bind_handle(sysevent_dev_handler);
	if (shp == NULL) {
		HAL_INFO (("sysevent_bind_handle failed %d", errno));
		return (FALSE);
	}

	subcl[0] = ESC_DISK;
	subcl[1] = ESC_LOFI;
	subcl[2] = ESC_PRINTER;
	if (sysevent_subscribe_event(shp, EC_DEV_ADD, subcl, 3) != 0) {
		HAL_INFO (("subscribe(dev_add) failed %d", errno));
		sysevent_unbind_handle(shp);
		return (FALSE);
	}
	if (sysevent_subscribe_event(shp, EC_DEV_REMOVE, subcl, 3) != 0) {
		HAL_INFO (("subscribe(dev_remove) failed %d", errno));
		sysevent_unbind_handle(shp);
		return (FALSE);
	}

	subcl[0] = ESC_DEV_BRANCH_REMOVE;
	if (sysevent_subscribe_event(shp, EC_DEV_BRANCH, subcl, 1) != 0) {
		HAL_INFO (("subscribe(dev_branch) failed %d", errno));
		sysevent_unbind_handle(shp);
		return (FALSE);
	}

	/*
	 * This is a work-around from the removal of powerd.
	 * Before listening to the power sysevent,
	 * if "power button" device node can be opened,
	 * start to monitor power button.
	 */
	if ((pb_fd = open("/dev/power_button", O_RDONLY)) != -1) {

		if (ioctl(pb_fd, PB_BEGIN_MONITOR, NULL) != -1) {
			HAL_INFO(("start power button monitor"));
		} else {
			HAL_INFO(("failed to monitor the power button"));
		}
	}

	subcl[0] = ESC_PWRCTL_ADD;
	subcl[1] = ESC_PWRCTL_REMOVE;
	subcl[2] = ESC_PWRCTL_STATE_CHANGE;
	subcl[3] = ESC_PWRCTL_BRIGHTNESS_UP;
	subcl[4] = ESC_PWRCTL_BRIGHTNESS_DOWN;
	subcl[5] = ESC_PWRCTL_POWER_BUTTON;
	if (sysevent_subscribe_event(shp, EC_PWRCTL, subcl, 6) != 0) {
		HAL_INFO(("subscribe(dev_add) failed %d", errno));
		sysevent_unbind_handle(shp);
		return (FALSE);
	}

	subcl[0] = ESC_DEVFS_DEVI_ADD;
	if (sysevent_subscribe_event(shp, EC_DEVFS, subcl, 1) != 0) {
		HAL_INFO (("subscribe(EC_DEVFS) failed %d", errno));
		sysevent_unbind_handle(shp);
		return (FALSE);
	}

	subcl[0] = ESC_DR_AP_STATE_CHANGE;
	if (sysevent_subscribe_event(shp, EC_DR, subcl, 1) != 0) {
		HAL_INFO (("subscribe(dynamic reconfiguration) failed %d",
		    errno));
		sysevent_unbind_handle(shp);
		return (FALSE);
	}

	return (B_TRUE);
}

void
sysevent_fini(void)
{
	sysevent_unbind_handle(shp);
	shp = NULL;
}

static void
sysevent_dev_handler(sysevent_t *ev)
{
	char		*class;
	char		*subclass;
	nvlist_t	*attr_list;
	char		*phys_path;
	char		*dev_name;
	char		*dev_hid;
	char		*dev_uid;
	uint_t		dev_index;
	char		s[1024];
	ssize_t		nwritten;

	if ((class = sysevent_get_class_name(ev)) == NULL)
		return;

	if ((subclass = sysevent_get_subclass_name(ev)) == NULL)
		return;

	if (sysevent_get_attr_list(ev, &attr_list) != 0)
		return;

	if (strcmp(class, EC_DEVFS) == 0) {
		if (nvlist_lookup_string(attr_list, DEVFS_PATHNAME, &phys_path) != 0) {
			goto out;
		}

		snprintf(s, sizeof (s), "%s %s %s\n",
		    class, subclass, phys_path);
		nwritten = write(sysevent_pipe_fds[1], s, strlen(s) + 1);

		HAL_INFO (("sysevent_dev_handler: wrote %d bytes", nwritten));
		goto out;
	}

	if (strcmp(class, EC_PWRCTL) == 0) {
		if (nvlist_lookup_string(attr_list, PWRCTL_DEV_PHYS_PATH,
		    &phys_path) != 0) {
			goto out;
		}
	} else if (strcmp(class, EC_DR) == 0) {
		if (nvlist_lookup_string(attr_list, DR_AP_ID,
		    &phys_path) != 0) {
			goto out;
		}
	} else if (nvlist_lookup_string(attr_list, DEV_PHYS_PATH, &phys_path)
	    != 0) {
		goto out;
	}

	/*
	 * In case of EC_DR, use dev_name to store DR_HINT val
	 */
	if (strcmp(class, EC_DR) == 0) {
		if (nvlist_lookup_string(attr_list, DR_HINT, &dev_name) != 0) {
			goto out;
		}
	} else if (nvlist_lookup_string(attr_list, DEV_NAME, &dev_name) != 0) {
		if (strcmp(class, EC_PWRCTL) == 0) {
			dev_name = "noname";
		} else {
			dev_name = "";
		}
	}

	if (nvlist_lookup_string(attr_list, PWRCTL_DEV_HID, &dev_hid) != 0) {
		dev_hid = "";
	}
	if (nvlist_lookup_string(attr_list, PWRCTL_DEV_UID, &dev_uid) != 0) {
		dev_uid = "";
	}
	if (nvlist_lookup_uint32(attr_list, PWRCTL_DEV_INDEX, &dev_index)
	    != 0) {
		dev_index = 0;
	}

	snprintf(s, sizeof (s), "%s %s %s %s %s %s %d\n",
	    class, subclass, phys_path, dev_name, dev_hid, dev_uid, dev_index);
	nwritten = write(sysevent_pipe_fds[1], s, strlen(s) + 1);

	HAL_INFO (("sysevent_dev_handler: wrote %d bytes", nwritten));

out:
	nvlist_free(attr_list);
}

static gboolean
sysevent_iochannel_data (GIOChannel *source,
		    GIOCondition condition,
		    gpointer user_data)
{
	GError *err = NULL;
	gchar *s = NULL;
	gsize len;
	int matches;
	gchar class[1024];
	gchar subclass[1024];
	gchar phys_path[1024];
	gchar dev_name[1024];
	gchar dev_uid[1024];
	gchar dev_hid[1024];
	uint_t dev_index;

	HAL_INFO (("sysevent_iochannel_data"));

	while (g_io_channel_read_line (sysevent_iochannel, &s, &len, NULL,
	    &err) == G_IO_STATUS_NORMAL) {
		if (len == 0) {
			break;
		}
		HAL_INFO (("IOChannel val => %s", s));
		class[0] = subclass[0] = phys_path[0] = dev_name[0] =
		    dev_hid[0] = dev_uid[0] = '\0';
		matches = sscanf(s, "%s %s %s %s %s %s %d", class, subclass,
		    phys_path, dev_name, dev_hid, dev_uid, &dev_index);
		g_free (s);
		s = NULL;
		if (matches < 3) {
			continue;
		}
		HAL_INFO (("sysevent: class=%s, sub=%s", class, subclass));

		if (strcmp(class, EC_DEV_ADD) == 0) {
			if ((strcmp(subclass, ESC_DISK) == 0) ||
			    (strcmp(subclass, ESC_PRINTER) == 0)) {
				sysevent_dev_add(phys_path, dev_name);
			} else if (strcmp(subclass, ESC_LOFI) == 0) {
				sysevent_lofi_add(phys_path, dev_name);
			}
		} else if (strcmp(class, EC_DEV_REMOVE) == 0) {
			if ((strcmp(subclass, ESC_DISK) == 0) ||
			    (strcmp(subclass, ESC_PRINTER) == 0)) {
				sysevent_dev_remove(phys_path, dev_name);
			} else if (strcmp(subclass, ESC_LOFI) == 0) {
				sysevent_lofi_remove(phys_path, dev_name);
			}
		} else if (strcmp(class, EC_DEV_BRANCH) == 0) {
			sysevent_dev_branch(phys_path);
		} else if (strcmp(class, EC_PWRCTL) == 0) {
			sysevent_pwrctl(class, subclass, phys_path,
			    dev_name, dev_hid, dev_uid, dev_index); 
		} else if (strcmp(class, EC_DEVFS) == 0) {
			if (strcmp(subclass, ESC_DEVFS_DEVI_ADD) == 0) {
				sysevent_devfs_add(phys_path);
			}
		} else if (strcmp(class, EC_DR) == 0) {
			/*
			 * Note: AP_ID is stored in phys_path and HINT is
			 * stored in dev_name, to avoid creating separate
			 * variables and multiple conditions checking
			 */
			HAL_DEBUG (("In %s, AP_ID-> %s, Hint-> %s", class,
			    phys_path, dev_name));
			if (strcmp(subclass, ESC_DR_AP_STATE_CHANGE) == 0) {
				sysevent_process_dr(phys_path, dev_name);
			}
		}
	}

	if (err) {
		g_error_free (err);
	}

	return (TRUE);
}

static void
sysevent_dev_add(gchar *devfs_path, gchar *name)
{
	gchar	*parent_devfs_path, *hotplug_devfs_path;
	HalDevice *parent;

	HAL_INFO (("dev_add: %s %s", name, devfs_path));

	parent = hal_util_find_closest_ancestor (devfs_path, &parent_devfs_path, &hotplug_devfs_path);
	if (parent == NULL) {
		return;
	}

	HAL_INFO (("dev_add: parent=%s", parent_devfs_path));
	HAL_INFO (("dev_add: real=%s", hotplug_devfs_path));

	devinfo_add (parent, hotplug_devfs_path);

	g_free (parent_devfs_path);
	g_free (hotplug_devfs_path);

	hotplug_event_process_queue ();
}

static void
sysevent_dev_remove(gchar *devfs_path, gchar *name)
{
	HAL_INFO (("dev_remove: %s %s", name, devfs_path));

	devinfo_remove_branch (devfs_path, NULL);
	hotplug_event_process_queue ();
}

static void
sysevent_dev_branch(gchar *devfs_path)
{
	HAL_INFO (("branch_remove: %s", devfs_path));

	devinfo_remove_branch (devfs_path, NULL);
	hotplug_event_process_queue ();
}

static void
sysevent_lofi_add(gchar *devfs_path, gchar *name)
{
	di_node_t node;
	const char *parent_udi;
	HalDevice *d, *parent;

	HAL_INFO (("lofi_add: %s %s", name, devfs_path));

	if ((d = hal_device_store_match_key_value_string (hald_get_gdl (),
	    "solaris.devfs_path", devfs_path)) == NULL) {
		HAL_INFO (("device not found in GDL %s", devfs_path));
		return;
	}
	parent_udi = hal_device_property_get_string (d, "info.parent");
	if ((parent_udi == NULL) || (strlen(parent_udi) == 0)) {
		HAL_INFO (("parent not found in GDL %s", parent_udi));
		return;
	}
	if ((parent = hal_device_store_match_key_value_string (hald_get_gdl (),
	    "info.udi", parent_udi)) == NULL) {
		HAL_INFO (("parent not found in GDL %s", parent_udi));
		return;
	}

	if ((node = di_init (devfs_path, DINFOCPYALL)) == DI_NODE_NIL) {
		HAL_INFO (("device not found in devinfo %s", devfs_path));
		return;
	}

	HAL_INFO (("device %s parent %s", hal_device_get_udi (d), parent_udi));
	devinfo_lofi_add_major (parent, node, devfs_path, NULL, TRUE, d);

	di_fini (node);

	hotplug_event_process_queue ();
}

static void
sysevent_lofi_remove(gchar *parent_devfs_path, gchar *name)
{
	devinfo_lofi_remove_minor(parent_devfs_path, name);
	hotplug_event_process_queue ();
}

static HalDevice *
lookup_parent(char *devfs_path)
{
	gchar		*path = NULL;
	HalDevice	*parent = NULL;
	char *p;

	path = strdup (devfs_path);
	p = strrchr (path, '/');
	if (p == NULL) {
		free (path);
		return (NULL);
	}
	*p = '\0';

	/* Look up the parent node in the gdl. */
	parent = hal_device_store_match_key_value_string (hald_get_gdl (),
	    "solaris.devfs_path", path);

	if (parent == NULL) {
		/* Look up the parent node in the tdl. */
		parent = hal_device_store_match_key_value_string (hald_get_tdl (),
		    "solaris.devfs_path", path);
	}

	free (path);
	return (parent);
}

/*
 * Handle the USB bus devices hot plugging events.
 */
static void
sysevent_devfs_add(gchar *devfs_path)
{
	di_node_t node;
	HalDevice *parent;
	char *driver_name;

	HAL_INFO (("devfs_handle: %s", devfs_path));

	if ((node = di_init (devfs_path, DINFOCPYALL)) == DI_NODE_NIL) {
		HAL_INFO (("device not found in devinfo %s", devfs_path));
		return;
	}

	if ((driver_name = di_driver_name (node)) == NULL)
		goto out;

	/* The disk and printer devices are handled by EC_DEV_ADD class. */
	if ((strcmp (driver_name, "scsa2usb") == 0) ||
	    (strcmp (driver_name, "usbprn") == 0))
		goto out;

	if ((parent = lookup_parent (devfs_path)) == NULL)
		goto out;

	devinfo_usb_add (parent, node, devfs_path, NULL);

	di_fini (node);

	hotplug_event_process_queue ();

	return;

 out:
	di_fini (node);
}

static void 
sysevent_pwrctl(gchar *class, gchar *subclass, gchar *phys_path,
    gchar *dev_name, gchar *dev_hid, gchar *dev_uid, uint_t dev_index)
{
	const gchar prefix[] = "/org/freedesktop/Hal/devices/pseudo/acpi_drv_0";
	gchar udi[HAL_PATH_MAX];
	int ret;

	if (strcmp(dev_hid, "PNP0C0A") == 0) {
		snprintf(udi, sizeof(udi), "%s_battery%d_0", prefix, dev_index);
		devinfo_battery_rescan(phys_path, udi);
	} else if (strcmp(dev_hid, "ACPI0003") == 0) {
		snprintf(udi, sizeof (udi), "%s_ac%d_0", prefix, dev_index);
		devinfo_battery_rescan(phys_path, udi);
	} else if (strcmp(dev_hid, "PNP0C0D") == 0) {
		snprintf(udi, sizeof (udi), "%s_lid_0", prefix);
		devinfo_lid_event(subclass, udi);
	} else if (strcmp(subclass, ESC_PWRCTL_POWER_BUTTON) == 0) {
		devinfo_power_button_event();

		/*
		 * Monitor the power button, but only take action if
		 * gnome-power-manager is not running.
		 *
		 * ret greater than 0 means could not find process.
		 */
		ret = system("/usr/bin/pgrep -fx gnome-power-manager");
		if (ret) {
			HAL_INFO(("Will go through system poweroff"));

			/*
			 * Send SIGPWR signal to the init process to
			 * shut down the system.
			 */
			if (kill(1, SIGPWR) == -1)
				(void) uadmin(A_SHUTDOWN, AD_POWEROFF, 0);
		}
	} else if ((strcmp(subclass, ESC_PWRCTL_BRIGHTNESS_UP) == 0) ||
	    (strcmp(subclass, ESC_PWRCTL_BRIGHTNESS_DOWN) == 0)) {
		devinfo_brightness_hotkeys_event(subclass);
	} else {
		HAL_INFO(("Unmatched EC_PWRCTL"));
	}
}

static void
sysevent_dr_remove_cpu()
{

	HalDeviceStore	*gdl;
	GSList		*iter;
	HalDevice	*d, *del_dev;
	int		cpu_id, del_cpuid;
	kstat_ctl_t	*kc;
	kstat_t		*ksp;
	kstat_named_t	*ksdata;
	const char	*cpu_devfs_path;
	/*
	 * Find the CPU's that are DR removed. For each "processor" device in
	 * HAL device tree, check if it has its corresponding kstat_info. If
	 * not, then, that cpu has been removed and can remove the entry from
	 * HAL entry
	 */

	HAL_DEBUG (("sysevent_dr_remove_cpu()"));
	kc = kstat_open ();
	if (kc == NULL) {
		HAL_INFO (("Error in removing HAL cpu entry during DR. Could"
		    " not open kstat to get cpu info: %s", strerror (errno)));
		return;
	}

	/*
	 * Iterate through the HAL device list to get the processor devices
	 */
	gdl = hald_get_gdl ();
	iter = gdl->devices;

	while (iter != NULL) {
		d = HAL_DEVICE (iter->data);

		if (!hal_device_has_property (d, "processor.number")) {
			iter = iter->next;
			continue;
		}

		cpu_id = hal_device_property_get_int (d, "processor.number");

		/*
		 * Check if the above cpu_id has its info in kstat
		 */

		ksp = kstat_lookup (kc, "cpu_info", cpu_id, NULL);
		if (ksp != NULL) {
			iter = iter->next;
			continue;
		}
		/*
		 *  kstat info not found. Delete the device entry
		 */
		HAL_INFO ((" Remove CPU entry: %d", cpu_id));
		iter = iter->next;
		cpu_devfs_path = hal_device_property_get_string (d,
		    "solaris.devfs_path");
		if (cpu_devfs_path == NULL) {
			HAL_INFO (("Could not get cpu_devfs_path to "
			    "remove for cpu_id %d", cpu_id));
		} else {
			/*
			 * Remove the cpu device
			 */
			HAL_DEBUG (("Queue %s for removal", cpu_devfs_path));
			devinfo_remove_enqueue ((char *)cpu_devfs_path, NULL);
			hotplug_event_process_queue ();
		}
	}

	if (kc) {
		kstat_close (kc);
	}
}

int
sysevent_dr_insert_cpu(di_node_t node, void *arg)
{
	char	*devfs_path;
	char	*device_type = NULL;
	DevinfoDevHandler *dh;

	dh = &devinfo_cpu_handler;
	devfs_path = di_devfs_path (node);

	(void) di_prop_lookup_strings (DDI_DEV_T_ANY, node, "device_type",
	    &device_type);

	dh->add (NULL, node, devfs_path, device_type);

	di_devfs_path_free (devfs_path);
	return (DI_WALK_CONTINUE);
}

/*
 * Remove/Add the DR event device
 * Note: Currently it supports only CPU DR events
 */
static void
sysevent_process_dr(gchar *ap_id, gchar *hint_val)
{
	cfga_err_t 		cfgerr;
	cfga_list_data_t 	*cfg_stat;
	int			nlist;
	char			*errstr;
	di_node_t		root_node;

	if ((ap_id == NULL) || (hint_val == NULL))
		return;
	HAL_DEBUG (("sysevent_process_dr: %s", ap_id));

	cfgerr = config_list_ext (1, (char *const *)&ap_id, &cfg_stat, &nlist,
	    NULL, NULL, &errstr, 0);

	if (cfgerr != CFGA_OK) {
		HAL_INFO (("DR sysevent process %d config_list_ext error: %s",
		    ap_id, errstr));
		goto out;
	}
	/*
	 * Check if the device type is CPU
	 */
	HAL_DEBUG ((" Ap-Type: %s, State: %d", cfg_stat->ap_type,
	    cfg_stat->ap_r_state));
	if (strcmp (cfg_stat->ap_type, "CPU") == 0) {
		if (strcmp (hint_val, DR_HINT_REMOVE) == 0) {
			sysevent_dr_remove_cpu();
		} else if (strcmp (hint_val, DR_HINT_INSERT) == 0) {
			/*
			 * Go through the device list and add the new cpu
			 * entries into HAL
			 */
			if ((root_node =
			    di_init ("/", DINFOCPYALL)) == DI_NODE_NIL) {
				HAL_INFO (("di_init failed. "\
				    "Cannot insert CPU"));
				goto out;
			}
			di_walk_node (root_node, DI_WALK_CLDFIRST, NULL,
			    sysevent_dr_insert_cpu);
			di_fini (root_node);
			hotplug_event_process_queue ();
		}
	} else {
		HAL_INFO (("Not a CPU, so cannot DR"));
	}

out:
	if (cfg_stat)
		free (cfg_stat);
	if (errstr)
		free (errstr);
}
