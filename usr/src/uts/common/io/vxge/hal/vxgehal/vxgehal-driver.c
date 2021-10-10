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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :   vxgehal-driver.c
 *
 * Description:  HAL driver object functionality
 *
 * Created:       15 January 2007
 */

#include "vxgehal.h"

static __vxge_hal_driver_t g_driver;
__vxge_hal_driver_t *g_vxge_hal_driver;

#ifdef	VXGE_OS_MEMORY_CHECK
vxge_os_malloc_t g_malloc_arr[VXGE_OS_MALLOC_CNT_MAX];
u32 g_malloc_cnt;
#endif

/*
 * Runtime tracing support
 */
u32 g_debug_level;

/*
 * vxge_hal_driver_initialize - Initialize HAL.
 * @config: HAL configuration, see vxge_hal_driver_config_t {}.
 * @uld_callbacks: Upper-layer driver callbacks, e.g. link-up.
 *
 * HAL initialization entry point. Not to confuse with device initialization
 * (note that HAL "contains" zero or more X3100 devices).
 *
 * Returns: VXGE_HAL_OK - success;
 * VXGE_HAL_ERR_BAD_DRIVER_CONFIG - Driver configuration params invalid.
 *
 * See also: vxge_hal_device_initialize(), vxge_hal_status_e {},
 * vxge_hal_uld_cbs_t {}.
 */
vxge_hal_status_e
vxge_hal_driver_initialize(
	vxge_hal_driver_config_t *config,
	vxge_hal_uld_cbs_t *uld_callbacks)
{
	vxge_hal_status_e status;
	g_vxge_hal_driver = &g_driver;

	if ((status = __vxge_hal_driver_config_check(config)) != VXGE_HAL_OK)
		return (status);

	vxge_os_memzero(g_vxge_hal_driver,  sizeof (__vxge_hal_driver_t));

	/* apply config */
	vxge_os_memcpy(&g_vxge_hal_driver->config, config,
	    sizeof (vxge_hal_driver_config_t));

	/* apply ULD callbacks */
	vxge_os_memcpy(&g_vxge_hal_driver->uld_callbacks, uld_callbacks,
	    sizeof (vxge_hal_uld_cbs_t));

	vxge_hal_driver_debug_set(config->level);

	g_vxge_hal_driver->is_initialized = 1;

	return (VXGE_HAL_OK);
}

/*
 * vxge_hal_driver_terminate - Terminate HAL.
 *
 * HAL termination entry point.
 *
 * See also: vxge_hal_device_terminate().
 */
void
vxge_hal_driver_terminate(void)
{
	g_vxge_hal_driver->is_initialized = 0;

	g_vxge_hal_driver = NULL;

#ifdef	VXGE_OS_MEMORY_CHECK
	if (TRUE) {
		u32 i, leaks = 0;
		vxge_os_printf("OSPAL: max g_malloc_cnt %d", g_malloc_cnt);
		for (i = 0; i < g_malloc_cnt; i++) {
			if (g_malloc_arr[i].ptr != NULL) {
				vxge_os_printf("OSPAL: memory leak detected at "
				    "%s:%d:"VXGE_OS_LLXFMT":%d",
				    g_malloc_arr[i].file,
				    g_malloc_arr[i].line,
				    (u64)(ptr_t)g_malloc_arr[i].ptr,
				    g_malloc_arr[i].size);
				leaks++;
			}
		}
		if (leaks) {
			vxge_os_printf("OSPAL: %d memory leaks detected",
			    leaks);
		} else {
			vxge_os_printf("OSPAL: no memory leaks detected");
		}
	}
#endif
}

/*
 * vxge_hal_driver_debug_set - Set the debug module, level and timestamp
 * @level: Debug level as defined in enum vxge_debug_level_e
 *
 * This routine is used to dynamically change the debug output
 */
void vxge_hal_driver_debug_set(
	vxge_debug_level_e level)
{
	g_vxge_hal_driver->debug_level = level;
	g_debug_level = 0;

	switch (level) {
		case VXGE_TRACE:
			g_debug_level |= VXGE_TRACE;
			/*FALLTHRU*/
		case VXGE_INFO:
			g_debug_level |= VXGE_INFO;
			/*FALLTHRU*/
		case VXGE_ERR:
			g_debug_level |= VXGE_ERR;
			/*FALLTHRU*/
		default:
			break;
	}
}

/*
 * vxge_hal_driver_debug_get - Get the debug level
 *
 * This routine returns the current debug level set
 */
u32
vxge_hal_driver_debug_get(void)
{
	return (g_vxge_hal_driver->debug_level);
}
