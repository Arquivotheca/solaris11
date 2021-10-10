/***************************************************************************
 *
 * acpi.h
 *
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This file is licensed under either the Academic Free License
 * version 2.1 or The GNU General Public License version 2.
 *
 **************************************************************************/

#ifndef ACPI_H
#define	ACPI_H

#include "../hald/util.h"

#define	BATTERY_POLL_TIMER		30000

gboolean battery_update(LibHalContext *ctx, const char *udi, int fd);
gboolean ac_adapter_update(LibHalContext *ctx, const char *udi, int fd);
gboolean lid_update(LibHalContext *ctx, const char *udi, int fd);
gboolean laptop_panel_update(LibHalContext *ctx, const char *udi, int fd);
gboolean update_devices(gpointer data);
int open_device(LibHalContext *ctx, char *udi);

#endif /* ACPI_H */
