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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Get battery device information from HAL
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <inttypes.h>
#include <getopt.h>

#ifndef DBUS_API_SUBJECT_TO_CHANGE
#define	DBUS_API_SUBJECT_TO_CHANGE 1
#endif

#include <dbus/dbus.h>
#include <libhal.h>
#include "libddudev.h"

int list_battery_devices(LibHalContext *hal_ctx);
int prt_dev_props(LibHalContext *hal_ctx, char *dev_name);

/* bat_detect usage */
void
usage()
{
	FPRINTF(stderr, "Usage: bat_detect [-l][-d (device name)]\n");
	FPRINTF(stderr, "       -l: ");
	FPRINTF(stderr, "list battery devices from hal\n");
	FPRINTF(stderr, "       -d {devfs path}: ");
	FPRINTF(stderr, "list device properties\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	DBusError error;
	DBusConnection *conn;
	LibHalContext *hal_ctx;
	int err;
	int	c;
	int	o_list = 0;
	int	o_dev = 0;
	char 	*dev_name;

	if (argc < 2) {
		usage();
	}

	while ((c = getopt(argc, argv, "ld:")) != EOF) {
		switch (c) {
		case 'l':
			/* list battery devices from hal */
			o_list = 1;
			break;
		case 'd':
			/* list device properties */
			o_dev = 1;
			dev_name = optarg;
			break;
		default:
			usage();
		}
	}

	/*
	 * Get devices information from hald
	 * for connect to hald:
	 * 1. Connect to D-BUS for message process
	 *    dbus_error_init -> Connect error message to D-BUS
	 *    dbus_bus_get -> Connect to bus daemon
	 * 2. Connect to hald:
	 *    libhal_ctx_new -> Create a new LibHalContext
	 *    libhal_ctx_set_dbus_connection -> Set DBus connection to hald
	 *    libhal_ctx_init -> Initialize the connection to hald
	 */

	/* Connect error message to D-BUS */
	dbus_error_init(&error);

	/* Connects to a bus daemon and registers the client with it */
	/* Hal use D-BUS to process message */
	if (!(conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error))) {
		FPRINTF(stderr, "error: dbus_bus_get: %s: %s\n",
		    error.name, error.message);
		/* LINTED E_CONSTANT_CONDITION */
		LIBHAL_FREE_DBUS_ERROR(&error);
		return (2);
	}

	/* Create a new LibHalContext */
	/* hal_ctx: Context for connection to hald */
	if (!(hal_ctx = libhal_ctx_new())) {
		perror("fail to connect to hald");
		return (3);
	}

	/* Set DBus connection to use to talk to hald */
	/* conn: D-BUS connection */
	if (!libhal_ctx_set_dbus_connection(hal_ctx, conn)) {
		perror("fail to connect D-BUS\n");
		return (4);
	}

	/* Initialize the connection to hald */
	if (!libhal_ctx_init(hal_ctx, &error)) {
		if (dbus_error_is_set(&error)) {
			FPRINTF(stderr,
			"error: libhal_ctx_init: %s: %s\n",
			    error.name, error.message);
			/* LINTED E_CONSTANT_CONDITION */
			LIBHAL_FREE_DBUS_ERROR(&error);
		}
		FPRINTF(stderr,
		    "Could not initialise connection to hald.\n"
		    "Normally this means the HAL daemon (hald)"
		    " is not running or not ready.\n");
		return (5);
	}

	err = 0;

	if (o_list) {
		err = list_battery_devices(hal_ctx);
	}

	if (o_dev) {
		err = prt_dev_props(hal_ctx, dev_name);
	}

	/* Shut down a connection to hald */
	(void) libhal_ctx_shutdown(hal_ctx, &error);
	/* Free a LibHalContext resource */
	(void) libhal_ctx_free(hal_ctx);
	/* disconnect from D-BUS, and free resource */
	dbus_connection_unref(conn);
	dbus_error_free(&error);

	return (err);
}

/*
 * Print device properties
 *
 * dev_name: device name
 *
 * Return:
 * 0: Successful get and print device properties
 * 1: Fail to get device properties
 */
int
prt_dev_props(LibHalContext *hal_ctx, char *dev_name)
{
	LibHalPropertySet *props;
	LibHalPropertySetIterator it;
	DBusError error;
	int type;
	char *devfs_path;

	/* Connect error message to D-BUS */
	dbus_error_init(&error);

	/* Retrieve all the properties on a device */
	if (!(props =
	    libhal_device_get_all_properties(hal_ctx, dev_name, &error))) {
		FPRINTF(stderr, "%s: %s\n", error.name, error.message);
		dbus_error_free(&error);
		return (1);
	}

	/* Get devfspath from property "solaris.devfs_path */
	devfs_path = libhal_device_get_property_string(hal_ctx,
	    dev_name, "solaris.devfs_path", &error);
	if (devfs_path) {
		PRINTF("devfs path:%s\n", devfs_path);
		libhal_free_string(devfs_path);
	}

	/* List each property name and value */
	for (libhal_psi_init(&it, props); libhal_psi_has_more(&it);
	    libhal_psi_next(&it)) {
		type = libhal_psi_get_type(&it);
		switch (type) {
			case LIBHAL_PROPERTY_TYPE_STRING:
				PRINTF("%s:'%s'\n",
				    libhal_psi_get_key(&it),
				    libhal_psi_get_string(&it));
				break;
			case LIBHAL_PROPERTY_TYPE_INT32:
				PRINTF("%s:%d\n",
				    libhal_psi_get_key(&it),
				    libhal_psi_get_int(&it));
				break;
			case LIBHAL_PROPERTY_TYPE_UINT64:
				PRINTF("%s:%lld\n",
				    libhal_psi_get_key(&it),
				    (long long) libhal_psi_get_uint64(&it));
				break;
			case LIBHAL_PROPERTY_TYPE_DOUBLE:
				PRINTF("%s:%g\n",
				    libhal_psi_get_key(&it),
				    libhal_psi_get_double(&it));
				break;
			case LIBHAL_PROPERTY_TYPE_BOOLEAN:
				PRINTF("%s:%s\n",
				    libhal_psi_get_key(&it),
				    libhal_psi_get_bool(&it) ? "true" :
				    "false");
				break;
			case LIBHAL_PROPERTY_TYPE_STRLIST:
				{
					char **strlist;

					PRINTF("%s:{ ",
					    libhal_psi_get_key(&it));
					strlist = libhal_psi_get_strlist(&it);
					while (*strlist) {
						PRINTF("'%s'%s",
						    *strlist,
						    strlist[1] ? ", " : "");
						strlist++;
					}
					PRINTF(" }\n");
				}
				break;
			default:
				PRINTF("Unknown type:%d=0x%02x\n",
				    type, type);
				break;
			}
	}

	libhal_free_property_set(props);
	PRINTF("\n");
	dbus_error_free(&error);
	return (0);
}

/*
 * List battery devices name
 * From hald get battery devices, if its property is "battery.present"
 * then list devices name.
 *
 * hal_ctx: Context for connection to hald
 *
 * Return:
 * 0: Get and list battery devices
 * 31: Fail to get battery devices
 */
int
list_battery_devices(LibHalContext *hal_ctx)
{
	int i;
	int num_devices;
	char **device_names;
	DBusError error;

	/* Connect error message to D-BUS */
	dbus_error_init(&error);

	/*
	 * Find battery devices from hald
	 * name of the property: "info.category"
	 * the value to match: "battery"
	 * number of battery devices stored in num_devices
	 */
	if (!(device_names = libhal_manager_find_device_string_match(hal_ctx,
	    "info.category", "battery", &num_devices, &error))) {
		perror("empty HAL device list");
		/* LINTED E_CONSTANT_CONDITION */
		LIBHAL_FREE_DBUS_ERROR(&error);
		return (31);
	}

	/*
	 * List present battery devices
	 * For each battery devices, if property is "battery.present"
	 * then display it.
	 */
	for (i = 0; i < num_devices; i++) {
		if (libhal_device_get_property_bool(hal_ctx, device_names[i],
		    "battery.present", &error)) {
			PRINTF("%s\n", device_names[i]);
		}
	}

	/* Free strings resources */
	libhal_free_string_array(device_names);
	dbus_error_free(&error);

	return (0);
}
