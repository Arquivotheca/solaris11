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

#include <string.h>
#include <sys/devctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <bay_impl.h>

static int bay_indicator_mode(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int bay_sensor_reading(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);
static int bay_sensor_state(topo_mod_t *, tnode_t *, topo_version_t,
    nvlist_t *, nvlist_t **);

#define	TOPO_METH_BAY_MODE_VERSION	0
#define	TOPO_METH_BAY_READING_VERSION	0
#define	TOPO_METH_BAY_STATE_VERSION	0

#define	TOPO_METH_BAY_READING_PROP	"propname"
#define	TOPO_METH_BAY_STATE_PROP	"propname"

#define	TOPO_METH_BAY_MODE_INDICATOR	"indicator_name"

static const topo_method_t bay_indicator_methods[] = {
	{ "bay_indicator_mode", TOPO_PROP_METH_DESC,
	    0, TOPO_STABILITY_INTERNAL, bay_indicator_mode }
};

static const topo_method_t bay_sensor_methods[] = {
	{ "bay_sensor_reading", TOPO_PROP_METH_DESC, 0,
	    TOPO_STABILITY_INTERNAL, bay_sensor_reading },
	{ "bay_sensor_state", TOPO_PROP_METH_DESC, 0,
	    TOPO_STABILITY_INTERNAL, bay_sensor_state },
};

/*
 * These are the routines to implement bay facility nodes and methods. The
 * LED manipulation methods require the HBA driver to support SGPIO.
 */

/* ARGSUSED */
static int
bay_sensor_reading(topo_mod_t *mod, tnode_t *tn, topo_version_t vers,
    nvlist_t *in, nvlist_t **out)
{
	nvlist_t	*nvl;
	double		value = 0;


	nvl = NULL;
	if (topo_mod_nvalloc(mod, &nvl, NV_UNIQUE_NAME) != 0 ||
	    nvlist_add_string(nvl, TOPO_PROP_VAL_NAME,
	    TOPO_SENSOR_READING) != 0 ||
	    nvlist_add_uint32(nvl, TOPO_PROP_VAL_TYPE, TOPO_TYPE_DOUBLE) != 0 ||
	    nvlist_add_double(nvl, TOPO_PROP_VAL_VAL, value) != 0) {
		nvlist_free(nvl);
		return (topo_mod_seterrno(mod, EMOD_NOMEM));
	}

	*out = nvl;
	return (0);
}

/* ARGSUSED */
static int
bay_sensor_state(topo_mod_t *mod, tnode_t *tn, topo_version_t vers,
    nvlist_t *in, nvlist_t **out)
{
	uint32_t	state = 0;
	nvlist_t	*nvl;

	nvl = NULL;
	if (topo_mod_nvalloc(mod, &nvl, NV_UNIQUE_NAME) != 0 ||
	    nvlist_add_string(nvl, TOPO_PROP_VAL_NAME,
	    TOPO_SENSOR_STATE) != 0 ||
	    nvlist_add_uint32(nvl, TOPO_PROP_VAL_TYPE, TOPO_TYPE_UINT32) != 0 ||
	    nvlist_add_uint32(nvl, TOPO_PROP_VAL_VAL, state) != 0) {
		nvlist_free(nvl);
		return (topo_mod_seterrno(mod, EMOD_NOMEM));
	}

	*out = nvl;

	return (0);
}

/*
 * LED control via SPGIO.
 */
int
bay_led_ctl(topo_mod_t *mod, tnode_t *tn, char *name, uint32_t mode, int op)
{
	int			fd;
	int			rv;
	int			led_type;
	int			led_num;
	int			dc_getset_led = 0x0;
	char			dev_nm[MAXPATHLEN];
	char			*devfs_path = NULL;
	di_node_t		dnode = DI_NODE_NIL;
	bay_t			*bp = NULL;
	struct dc_led_ctl	led;

	char			*f = "bay_led_ctl";

	/* should be a bay_t struct in node specific area */
	bp = (bay_t *)topo_node_getspecific(tn);
	if (bp == NULL) {
		/* called from fac method - it's the parent */
		bp = (bay_t *)topo_node_getspecific(topo_node_parent(tn));
		if (bp == NULL) {
			topo_mod_dprintf(mod, "%s: No bay_t\n", f);
			return (-1);
		}
	}

	topo_mod_dprintf(mod, "%s: %s %s:%s (%d)\n", f,
	    op == BAY_INDICATOR_GET ? "GET" : "SET", name,
	    op == BAY_INDICATOR_GET ? "" :
	    (mode == DCL_STATE_OFF ? "OFF" :
	    mode == DCL_STATE_ON ? "ON" : "BLINK"), mode);

	led_num = bp->phy;
	dnode = bp->hba_dnode;
	if (dnode == DI_NODE_NIL) {
		topo_mod_dprintf(mod, "%s: DI_NODE_NIL\n", f);
		return (-1);
	}
	devfs_path = di_devfs_path(dnode);
	topo_mod_dprintf(mod,
	    "%s: hba name(%s) phy(%d) devfs_path(%s)\n", f,
	    di_driver_name(dnode), bp->phy, devfs_path);

	/*
	 * Build the device file for ioctl:
	 * "/devices" + di_devfs_path() + ":devctl"
	 */
	(void) snprintf(dev_nm, MAXPATHLEN, "%s%s%s", "/devices",
	    devfs_path, ":devctl");

	di_devfs_path_free(devfs_path);
	topo_mod_dprintf(mod, "%s: device path: %s\n", f, dev_nm);

	/* open the hba parent node */
	fd = open(dev_nm, O_RDWR);
	if (fd == -1) {
		topo_mod_dprintf(mod, "%s: failed to open (%s)\n",
		    f, dev_nm);
		return (-1);
	}

	/* determine the led type */
	if (cmp_str(name, BAY_PROP_IDENT) || cmp_str(name, BAY_PROP_FAULT)) {
		led_type = DCL_TYPE_DEVICE_FAIL;
	} else if (cmp_str(name, BAY_PROP_OK2RM)) {
		led_type = DCL_TYPE_DEVICE_OK2RM;
	} else {
		topo_mod_dprintf(mod, "%s: Unknown led name type (%s)\n",
		    f, name);
		return (-1);
	}

	/* fill in SGPIO strucure */
	led.led_number = led_num;
	led.led_ctl_active = DCL_CNTRL_ON;
	led.led_type = led_type;
	led.led_state = mode;

	/* SGPIO ioctl to drv */
	dc_getset_led = op == BAY_INDICATOR_GET ? DEVCTL_GET_LED :
	    DEVCTL_SET_LED;
	rv = ioctl(fd, dc_getset_led, &led);
	if (rv == -1) {
		topo_mod_dprintf(mod, "%s: SGPIO ioctl failed.\n", f);

		(void) close(fd);
		return (-1);
	}
	(void) close(fd);

	topo_mod_dprintf(mod, "%s: done.\n", f);
	if (op == BAY_INDICATOR_GET) {
		return (led.led_state);
	} else {
		return (0);
	}
}

/*
 * Get/set bay indicator LED
 */
/* ARGSUSED */
static int
bay_indicator_mode(topo_mod_t *mod, tnode_t *tn, topo_version_t vers,
    nvlist_t *in, nvlist_t **out)
{
	nvlist_t	*args;
	nvlist_t	*pargs;
	char		*indicator_name;
	uint32_t	mode = 0;
	nvlist_t	*nvl;

	char		*f = "bay_indicator_mode";

	topo_mod_dprintf(mod, "%s\n", f);

	if (vers > TOPO_METH_BAY_MODE_VERSION)
		return (topo_mod_seterrno(mod, ETOPO_METHOD_VERNEW));

	if (nvlist_lookup_nvlist(in, TOPO_PROP_ARGS, &args) != 0 ||
	    nvlist_lookup_string(args, TOPO_METH_BAY_MODE_INDICATOR,
	    &indicator_name) != 0) {
		topo_mod_dprintf(mod,
		    "%s: invalid arguments to 'mode' method\n", f);
		return (topo_mod_seterrno(mod, EMOD_NVL_INVAL));
	}

	if ((nvlist_lookup_nvlist(in, TOPO_PROP_PARGS, &pargs) == 0) &&
	    nvlist_exists(pargs, TOPO_PROP_VAL_VAL)) {
		/*
		 * SET LED
		 */
		if (nvlist_lookup_uint32(pargs, TOPO_PROP_VAL_VAL,
		    &mode) != 0) {
			topo_mod_dprintf(mod,
			    "%s: no indicator mode prop\n", f);
			(void) topo_mod_seterrno(mod, EMOD_NVL_INVAL);
			goto errout;
		}

		/*
		 * Valid LED states are DCL_STATE_* defined in devctl.h.
		 */
		if (mode != DCL_STATE_OFF && mode != DCL_STATE_ON &&
		    mode != DCL_STATE_SLOW_BLNK &&
		    mode != DCL_STATE_FAST_BLNK &&
		    mode != DCL_STATE_SNGL_BLNK) {
			topo_mod_dprintf(mod,
			    "%s: invalid indicator mode: %d\n", f,
			    mode);
			(void) topo_mod_seterrno(mod, EMOD_NVL_INVAL);
			goto errout;
		}

		/* set the LED */
		if (bay_led_ctl(mod, tn, indicator_name, mode,
		    BAY_INDICATOR_SET) != 0) {
			topo_mod_dprintf(mod,
			    "%s: failed to turn LED %s\n", f,
			    mode == DCL_STATE_OFF ? "off" :
			    mode == DCL_STATE_ON ? "on" : "blink");
			goto errout;
		}
	} else {
		/*
		 * GET LED
		 */
		mode = bay_led_ctl(mod, tn, indicator_name, 0,
		    BAY_INDICATOR_GET);
		/*
		 * Valid LED states are DCL_STATE_* defined in devctl.h.
		 */
		if (mode != DCL_STATE_OFF && mode != DCL_STATE_ON &&
		    mode != DCL_STATE_SLOW_BLNK &&
		    mode != DCL_STATE_FAST_BLNK &&
		    mode != DCL_STATE_SNGL_BLNK) {
			topo_mod_dprintf(mod,
			    "%s: failed to get %s LED mode\n",
			    f, indicator_name);
			goto errout;
		}
		topo_mod_dprintf(mod, "%s: %s mode: %d\n", f,
		    indicator_name, mode);
	}

	nvl = NULL;
	if (topo_mod_nvalloc(mod, &nvl, NV_UNIQUE_NAME) != 0 ||
	    nvlist_add_string(nvl, TOPO_PROP_VAL_NAME,
	    TOPO_LED_MODE) != 0 ||
	    nvlist_add_uint32(nvl, TOPO_PROP_VAL_TYPE, TOPO_TYPE_UINT32) != 0 ||
	    nvlist_add_uint32(nvl, TOPO_PROP_VAL_VAL, mode) != 0) {
		nvlist_free(nvl);
		(void) topo_mod_seterrno(mod, EMOD_NOMEM);
		topo_mod_dprintf(mod, "%s: failed.\n", f);
		goto errout;
	}

	*out = nvl;
	topo_mod_dprintf(mod, "%s: done.\n", f);

	return (0);
errout:
	return (-1);
}

/*
 * Create generic facility node.
 */
static tnode_t *
bay_fac_generic(topo_mod_t *mod, tnode_t *pnode, const char *name,
    const char *type)
{
	int			err;
	tnode_t			*tn;
	topo_pgroup_info_t	pgi;

	char			*f = "bay_fac_generic";

	tn = topo_node_facbind(mod, pnode, name, type);
	if (tn == NULL) {
		topo_mod_dprintf(mod, "%s: failed to bind facility node %s\n",
		    f, name);
		return (NULL);
	}

	pgi.tpi_name = TOPO_PGROUP_FACILITY;
	pgi.tpi_namestab = TOPO_STABILITY_PRIVATE;
	pgi.tpi_datastab = TOPO_STABILITY_PRIVATE;
	pgi.tpi_version = 1;

	if (topo_pgroup_create(tn, &pgi, &err) != 0) {
		topo_mod_dprintf(mod, "%s: failed to create facility property "
		    "group: %s\n", f, topo_strerror(err));
		topo_node_unbind(tn);
		return (NULL);
	}

	return (tn);
}

/*
 * Create LED indicator facility node.
 */
/* ARGSUSED */
static int
bay_add_indicator(topo_mod_t *mod, tnode_t *pnode, int type,
    const char *name, const char *indicator_name)
{
	int		err;
	tnode_t		*tn;
	nvlist_t	*nvl;

	char		*f = "bay_add_indicator";

	/* create generic fac node */
	tn = bay_fac_generic(mod, pnode, name, TOPO_FAC_TYPE_INDICATOR);
	if (tn == NULL) {
		topo_mod_dprintf(mod,
		    "%s: failed to create generic %s fac node.\n",
		    f, name);
		return (-1);
	}

	/* register methods */
	if (topo_method_register(mod, tn, bay_indicator_methods) < 0) {
		topo_mod_dprintf(mod,
		    "%s: failed to register %s fac methods.\n", f, name);
		return (-1);
	}

	/* indicator 'type' property */
	if (topo_prop_set_uint32(tn, TOPO_PGROUP_FACILITY,
	    TOPO_FACILITY_TYPE, TOPO_PROP_IMMUTABLE, type, &err) != 0) {
		topo_mod_dprintf(mod,
		    "%s: failed to set facility type property: %s\n",
		    f, topo_strerror(err));
		topo_node_unbind(tn);
		return (-1);
	}

	/* indicator 'mode' property */
	nvl = NULL;
	if (topo_mod_nvalloc(mod, &nvl, NV_UNIQUE_NAME) != 0 ||
	    nvlist_add_string(nvl, TOPO_METH_BAY_MODE_INDICATOR,
	    indicator_name) != 0) {
		nvlist_free(nvl);
		topo_node_unbind(tn);
		topo_mod_dprintf(mod,
		    "%s: failed to set facility mode property: %s\n",
		    f, topo_strerror(err));
		return (topo_mod_seterrno(mod, EMOD_NOMEM));
	}

	if (topo_prop_method_register(tn, TOPO_PGROUP_FACILITY,
	    TOPO_LED_MODE, TOPO_TYPE_UINT32, "bay_indicator_mode",
	    nvl, &err) != 0) {
		nvlist_free(nvl);
		topo_node_unbind(tn);
		topo_mod_dprintf(mod,
		    "%s: failed to register mode method %s fac node.\n",
		    f, name);
		return (-1);
	}

	if (topo_prop_setmutable(tn, TOPO_PGROUP_FACILITY,
	    TOPO_LED_MODE, &err) != 0) {
		nvlist_free(nvl);
		topo_node_unbind(tn);
		topo_mod_dprintf(mod,
		    "%s: failed to set property as mutable: %s\n", f,
		    topo_strerror(err));
		return (-1);
	}

	nvlist_free(nvl);
	return (0);
}

/*
 * Create generic sensor node.
 */
static tnode_t *
bay_add_sensor_common(topo_mod_t *mod, tnode_t *pnode, const char *name,
    const char *class, int type)
{
	int	err;
	tnode_t	*tn;

	char	*f = "bay_add_sensor_common";

	/* create generic facility node */
	tn = bay_fac_generic(mod, pnode, name, TOPO_FAC_TYPE_SENSOR);
	if (tn == NULL) {
		topo_mod_dprintf(mod,
		    "%s: failed to create generic %s fac node.\n",
		    f, name);
		return (NULL);
	}

	if (topo_method_register(mod, tn, bay_sensor_methods) < 0) {
		topo_mod_dprintf(mod, "failed to register facility methods\n");
		topo_node_unbind(tn);
		return (NULL);
	}

	/* set standard properties */
	if (topo_prop_set_string(tn, TOPO_PGROUP_FACILITY,
	    TOPO_SENSOR_CLASS, TOPO_PROP_IMMUTABLE,
	    class, &err) != 0 ||
	    topo_prop_set_uint32(tn, TOPO_PGROUP_FACILITY,
	    TOPO_FACILITY_TYPE, TOPO_PROP_IMMUTABLE,
	    type, &err) != 0) {
		topo_mod_dprintf(mod,
		    "failed to set facility node properties: %s\n",
		    topo_strerror(err));
		topo_node_unbind(tn);
		return (NULL);
	}

	return (tn);
}

/*
 * Add specific sensor.
 */
static int
bay_add_sensor(topo_mod_t *mod, tnode_t *pnode, const char *name,
    const char *prop, int s_units, int s_type)
{

	int		err;
	tnode_t		*tn;
	nvlist_t	*nvl;

	char		*f = "bay_add_sensor";

	tn = bay_add_sensor_common(mod, pnode, name,
	    TOPO_SENSOR_CLASS_DISCRETE, s_type);
	if (tn == NULL) {
		return (-1);
	}

	if (topo_prop_set_uint32(tn, TOPO_PGROUP_FACILITY,
	    TOPO_SENSOR_UNITS, TOPO_PROP_IMMUTABLE, s_units, &err) != 0) {
		topo_mod_dprintf(mod,
		    "%s: failed to set facility node properties: %s\n",
		    f, topo_strerror(err));
		topo_node_unbind(tn);
		return (-1);
	}

	nvl = NULL;
	if (topo_mod_nvalloc(mod, &nvl, NV_UNIQUE_NAME) != 0 ||
	    nvlist_add_string(nvl, "propname", prop) != 0) {
		nvlist_free(nvl);
		topo_mod_dprintf(mod,
		    "%s: failed to setup method arguments\n", f);
		topo_node_unbind(tn);
		return (-1);
	}

	/* 'state' property */
	if (topo_prop_method_register(tn, TOPO_PGROUP_FACILITY,
	    TOPO_SENSOR_STATE, TOPO_TYPE_UINT32, "bay_sensor_state",
	    nvl, &err) != 0) {
		nvlist_free(nvl);
		topo_mod_dprintf(mod,
		    "%s: failed to register state method: %s\n",
		    f, topo_strerror(err));
		return (-1);
	}

	nvlist_free(nvl);

	return (0);
}

/*
 * Create facility indicator nodes for bay:
 *   - locator LED
 *   - OK2RM LED
 *   - fail LED
 *   - fault sensor
 */
/* ARGSUSED */
int
bay_enum_facility(topo_mod_t *mod, tnode_t *tnode, topo_version_t vers,
    nvlist_t *in, nvlist_t **out)
{
	int	rv;

	char	*f = "bay_enum_facility";

	/* locate LED */
	rv = bay_add_indicator(mod, tnode, TOPO_LED_TYPE_LOCATE,
	    "ident", BAY_PROP_IDENT);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to create bay-ident.\n", f);
		return (-1);
	}

	/* failed LED */
	rv = bay_add_indicator(mod, tnode, TOPO_LED_TYPE_SERVICE,
	    "fail", BAY_PROP_FAULT);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to create bay-fail.\n", f);
		return (-1);
	}

	/* OK2RM LED */
	rv = bay_add_indicator(mod, tnode, TOPO_LED_TYPE_OK2RM,
	    "ok2rm", BAY_PROP_OK2RM);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to create bay-ok2rm.\n", f);
		return (-1);
	}

	/* fault sensor */
	rv = bay_add_sensor(mod, tnode, "fault", "bay_fault", 0,
	    TOPO_SENSOR_TYPE_GENERIC_FAILURE);
	if (rv != 0) {
		topo_mod_dprintf(mod, "%s: failed to create bay-fault.\n", f);
		return (-1);
	}

	return (0);
}
