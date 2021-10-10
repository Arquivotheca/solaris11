/***************************************************************************
 *
 * adt_data.h : Audit facility
 *
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 *
 * This file is licensed under either the Academic Free License
 * version 2.1 or The GNU General Public License version 2.
 *
 ***************************************************************************/

#ifndef ADT_DATA_H
#define ADT_DATA_H

#ifdef sun
#include <bsm/adt.h>
#include <bsm/adt_event.h>

adt_export_data_t *get_audit_export_data(DBusConnection *bus, const char *invoked_by_syscon_name, size_t *data_size);

#endif  /* sun */

#endif /* ADT_DATA_H */
