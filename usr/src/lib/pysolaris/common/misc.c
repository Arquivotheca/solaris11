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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <Python.h>
#include <zone.h>
#include <libintl.h>
#include <directory.h>

#ifdef __lint
#define	dgettext(x, y) y
#endif

#define	_(s) dgettext(TEXT_DOMAIN, s)

extern int sid_to_id(char *sid, boolean_t user, uid_t *id);

static PyObject *
py_sid_to_id(PyObject *self, PyObject *args)
{
	char *sid;
	int err, isuser;
	uid_t id;

	if (!PyArg_ParseTuple(args, "si", &sid, &isuser))
		return (NULL);

	err = sid_to_id(sid, isuser, &id);
	if (err) {
		PyErr_SetString(PyExc_KeyError, sid);
		return (NULL);
	}

	return (Py_BuildValue("I", id));
}

/*
 * Translate the sid string ("S-1-...") to the user@domain name, if
 * possible.
 */
static PyObject *
py_sid_to_name(PyObject *self, PyObject *args)
{
	int isuser;
	char *name, *sid;
	directory_error_t e;
	uint64_t classes;
	PyObject *ret;

	if (!PyArg_ParseTuple(args, "si", &sid, &isuser))
		return (NULL);
	e = directory_name_from_sid(NULL, sid, &name, &classes);
	if (e != NULL) {
		directory_error_free(e);
		PyErr_SetString(PyExc_KeyError, sid);
		return (NULL);
	}
	if (name == NULL) {
		PyErr_SetString(PyExc_KeyError, sid);
		return (NULL);
	}
	if (isuser) {
		if (!(classes & DIRECTORY_CLASS_USER)) {
			free(name);
			PyErr_SetString(PyExc_KeyError, sid);
			return (NULL);
		}
	} else {
		if (!(classes & DIRECTORY_CLASS_GROUP)) {
			free(name);
			PyErr_SetString(PyExc_KeyError, sid);
			return (NULL);
		}
	}

	ret = PyString_FromString(name);
	free(name);
	return (ret);
}

static PyObject *
py_isglobalzone(PyObject *self, PyObject *args)
{
	return (Py_BuildValue("i", getzoneid() == GLOBAL_ZONEID));
}

static PyObject *
py_gettext(PyObject *self, PyObject *args)
{
	char *message, *result;
	PyObject *ret = NULL;

	if (!PyArg_ParseTuple(args, "s", &message))
		return (NULL);

	result = dgettext(TEXT_DOMAIN, message);

	ret = Py_BuildValue("s", result);
	return (ret);
}

static PyMethodDef solarismethods[] = {
	{"sid_to_id", py_sid_to_id, METH_VARARGS, "Map SID to UID/GID."},
	{"sid_to_name", py_sid_to_name, METH_VARARGS,
	    "Map SID to name@domain."},
	{"isglobalzone", py_isglobalzone, METH_NOARGS,
	    "Determine if this is the global zone."},
	{"gettext", py_gettext, METH_VARARGS, "Native call to gettext(3C)"},
	{NULL, NULL, 0, NULL}
};

void
initmisc(void)
{
	char *noop;

	noop = _("noop");
	PyObject *solaris_misc = Py_InitModule("solaris.misc", solarismethods);
}
