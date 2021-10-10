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

#include <Python.h>
#include <zone.h>

/* ARGSUSED */
static PyObject *
_getzoneid(PyObject *self, PyObject *junk)
{
	return (Py_BuildValue("l", getzoneid()));
}

/* ARGSUSED */
static PyObject *
_getzoneidbyname(PyObject *self, PyObject *args)
{
	char *zonename;
	zoneid_t zid;

	if (PyArg_ParseTuple(args, "s", &zonename) == NULL) {
		return (NULL);
	}

	if ((zid = getzoneidbyname(zonename)) == -1) {
		return (PyErr_SetFromErrno(PyExc_OSError));
	}

	return (Py_BuildValue("l", zid));
}

static PyObject *
_getzonenamebyid(PyObject *self, PyObject *args)
{
	zoneid_t zid;
	char buf[ZONENAME_MAX];
	ssize_t ret;

	if (PyArg_ParseTuple(args, "l", &zid) == NULL) {
		return (NULL);
	}

	if ((ret = getzonenamebyid(zid, buf, ZONENAME_MAX)) == -1) {
		return (PyErr_SetFromErrno(PyExc_OSError));
	}

	return (Py_BuildValue("s", buf));
}

static struct PyMethodDef zonesMethods[] = {
	{"getzoneid", (PyCFunction)_getzoneid, METH_NOARGS,
		"getzoneid() -> zoneid\n\nReturn the zone ID of the calling "
		"process."},
	{"getzoneidbyname", (PyCFunction)_getzoneidbyname, METH_VARARGS,
		"getzoneidbyname(name) -> zoneid\n\nReturn the zone ID "
		"corresponding to the named zone, if that zone is currently "
		"active.  If name is None, return the zone ID of the calling "
		"process."},
	{"getzonenamebyid", (PyCFunction)_getzonenamebyid, METH_VARARGS,
		"getzonenamebyid(id) -> name\n\nReturn the zone name "
		"corresponding to the given zone ID."},
	{NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC
initzones(void)
{
	/* PyMODINIT_FUNC; */
	(void) Py_InitModule("zones", zonesMethods);
}
