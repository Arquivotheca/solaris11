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

/*
 * ==============================================
 * Name Service Switch libscf to python interface
 * ==============================================
 */

/* Python specific includes */
#include <Python.h>
#include <structmember.h>

/* libscf and other includes */
#include <libscf.h>
#include <stdlib.h>
#include <string.h>

/*
 * ===================================
 * Nssscf Method structure definitions
 * ===================================
 */

static PyObject *NssscfError;

typedef struct {
	PyObject_HEAD
	/* Nssscf Type-specific fields go here. */
	PyObject	*service;	/* python service object (name) */
	scf_handle_t	*handle;	/* smf handle */
	scf_scope_t	*scope;		/* smf scope */
	scf_service_t	*svc;		/* smf service handle */
	scf_instance_t	*inst;		/* instance handle */
} Nssscf_t;

/*
 * =======================
 * Nssscf helper functions
 * =======================
 */
static char **__strlist_add(char **list, const char *str);
static int __strlist_len(char **list);
static void __strlist_free(char **list);

static char *__getscf_svcname(PyObject *service);
static char *__getscf_instname(Nssscf_t *self);
static char *__getscf_instfmri(Nssscf_t *self);
static char *__getscf_inststate(Nssscf_t *self);
static int __setscf_svc(Nssscf_t *self, PyObject *service);

#define	FREE_INSTANCE()	{ if (instance) free((void *)instance); }

static PyObject *Nssscf_ns1_convert(Nssscf_t *, PyObject *, PyObject *);

/*
 * ==========================
 * Nssscf Instance management
 * ==========================
 */

static void
Nssscf_dealloc(Nssscf_t *self)
{
	/* libscf instance cleanup */
	if (self->inst != NULL) {
		scf_instance_destroy(self->inst);
		self->inst = NULL;
	}
	if (self->svc != NULL) {
		scf_service_destroy(self->svc);
		self->svc = NULL;
	}
	if (self->scope != NULL) {
		scf_scope_destroy(self->scope);
		self->scope = NULL;
	}
	if (self->handle != NULL) {
		scf_handle_destroy(self->handle);
		self->handle = NULL;
	}

	Py_XDECREF(self->service);
	self->ob_type->tp_free((PyObject *)self);
}

static PyObject *
Nssscf_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	Nssscf_t	*self;
	scf_handle_t	*h;
	scf_scope_t	*s;
	scf_service_t	*v;
	scf_instance_t	*i;

	self = (Nssscf_t *)type->tp_alloc(type, 0);
	if (self != NULL) {
		self->service = PyString_FromString("");
		if (self->service == NULL) {
			Py_DECREF(self);
			return (NULL);
		}
	}
	/* libscf instance generic setup */
	h = scf_handle_create(SCF_VERSION);
	s = scf_scope_create(h);
	v = scf_service_create(h);
	i = scf_instance_create(h);
	if (h == NULL || s == NULL || v == NULL || i == NULL ||
	    scf_handle_bind(h) == -1 ||
	    scf_handle_get_scope(h, SCF_SCOPE_LOCAL, s) == -1) {
		if (i != NULL)
			scf_instance_destroy(i);
		if (v != NULL)
			scf_service_destroy(v);
		if (s != NULL)
			scf_scope_destroy(s);
		if (h != NULL)
			scf_handle_destroy(h);
		Py_DECREF(self->service);
		Py_DECREF(self);
		return (NULL);
	}
	self->handle = h;
	self->scope = s;
	self->svc = v;
	self->inst = i;

	return ((PyObject *)self);
}

static int
Nssscf_init(Nssscf_t *self, PyObject *args, PyObject *kwds)
{
	PyObject *service = NULL;
	PyObject *tmp;
	static char *kwlist[] = {"service", NULL};

	if (! PyArg_ParseTupleAndKeywords(args, kwds, "|S", kwlist, &service))
		return (-1);

	if (service) {
		/* libscf instance specific init */
		if (__setscf_svc(self, service) < 0)
			return (-1);
		/* python specific */
		tmp = self->service;
		Py_INCREF(service);
		self->service = service;
		Py_DECREF(tmp);
	}
	return (0);
}

/*
 * ================================
 * Nssscf Instance member functions
 * ================================
 */

static PyObject *
Nssscf_getservice(Nssscf_t *self, void *closure)
{
	Py_INCREF(self->service);
	return (self->service);
}

static int
Nssscf_setservice(Nssscf_t *self, PyObject *value, void *closure)
{
	PyObject *tmp;

	if (value == NULL) {
		PyErr_SetString(PyExc_TypeError,
		    "Cannot delete the service attribute");
		return (-1);
	}

	if (! PyString_Check(value)) {
		PyErr_SetString(PyExc_TypeError,
		    "The service attribute value must be a string");
		return (-1);
	}

	/* libscf instance specific init */
	if (__setscf_svc(self, value) < 0) {
		PyErr_SetString(NssscfError, "invalid SMF service");
		return (-1);
	}
	tmp = self->service;
	Py_INCREF(value);
	self->service = value;
	Py_DECREF(tmp);

	/* re-set any libscf instance specific init */
	return (0);
}

/*
 * =======================
 * Nssscf Instance methods
 * =======================
 */

	/* Get Operations */
/*
 * Get all Service Property groups
 * IN: nothing
 * OUT: Tuple of property group names and types
 */

static PyObject *
Nssscf_get_svcpgs(Nssscf_t *self, PyObject *args)
{
	scf_propertygroup_t	*pg = NULL;
	scf_iter_t		*iter = NULL;
	PyObject 		*pytuple = NULL;
	PyObject 		*pylist = NULL;
	PyObject 		*pyobject = NULL;
	char			**nmlist = NULL;
	char			**tplist = NULL;
	int			nmlen, tplen, listlen, l;
	char			*pgname, *pgtype;
	PyObject		*ret = NULL;

	if ((iter = scf_iter_create(self->handle)) == NULL) {
		PyErr_SetString(NssscfError, "Cannot create iterator");
		return (NULL);
	}
	if (scf_iter_service_pgs(iter, self->svc) < 0) {
		scf_iter_destroy(iter);
		PyErr_SetString(NssscfError, "Cannot create pg iterator");
		return (NULL);
	}
	if ((pg = scf_pg_create(self->handle)) == NULL) {
		scf_iter_destroy(iter);
		PyErr_SetString(NssscfError, "Cannot create pg handle");
		return (NULL);
	}
	nmlen = scf_limit(SCF_LIMIT_MAX_NAME_LENGTH) + 1;
	if ((pgname = (char *)malloc(nmlen)) == NULL) {
		scf_pg_destroy(pg);
		scf_iter_destroy(iter);
		PyErr_SetString(NssscfError, "Memory Error");
		return (NULL);
	}
	tplen = scf_limit(SCF_LIMIT_MAX_PG_TYPE_LENGTH) + 1;
	if ((pgtype = (char *)malloc(tplen)) == NULL) {
		free((void *)pgname);
		scf_pg_destroy(pg);
		scf_iter_destroy(iter);
		PyErr_SetString(NssscfError, "Memory Error");
		return (NULL);
	}
	while (scf_iter_next_pg(iter, pg) > 0) {
		if (scf_pg_get_name(pg, pgname, nmlen) < 0)
			continue;
		if (scf_pg_get_type(pg, pgtype, tplen) < 0)
			continue;
		nmlist = __strlist_add(nmlist, pgname);
		tplist = __strlist_add(tplist, pgtype);
	}
	if ((listlen = __strlist_len(nmlist)) > 0) {
		if ((pylist = PyList_New(0)) == NULL) {
			PyErr_SetString(NssscfError,
			    "Unable to build Property group tuple");
			ret = NULL;
			goto err;
		}
		for (l = 0; l < listlen; l++) {
			pyobject = Py_BuildValue("(ss)", nmlist[l], tplist[l]);
			if (pyobject == NULL) {
				PyErr_SetString(NssscfError,
				    "Unable to build Property group tuple");
				ret = NULL;
				goto err;
			}
			if (PyList_Append(pylist, pyobject) < 0) {
				PyErr_SetString(NssscfError,
				    "Unable to build Property group tuple");
				ret = NULL;
				goto err;
			}
			Py_DECREF(pyobject);
			pyobject = NULL;
		}
		pytuple = PyList_AsTuple(pylist);
		Py_DECREF(pylist);
		pylist = NULL;
	} else {
		pytuple = Py_BuildValue("()");
	}
	ret = pytuple;
err:
	/* ErrStrings (if any) should be set by now */
	if (pyobject)
		Py_DECREF(pyobject);
	if (pylist)
		Py_DECREF(pylist);
	if (pgname)
		free((void *)pgname);
	if (pgtype)
		free((void *)pgtype);
	__strlist_free(nmlist);
	__strlist_free(tplist);
	scf_pg_destroy(pg);
	scf_iter_destroy(iter);
	return (ret);
}

/*
 * Get all Properties for a Property group
 * IN: arg1=pg
 * OUT: Tuple of properties and types
 */

static PyObject *
Nssscf_get_pgprops(Nssscf_t *self, PyObject *args)
{
	char			*instance = __getscf_instname(self);
	char			*instnm;
	const char		*pgname;
	scf_iter_t		*iter = NULL;
	scf_propertygroup_t	*pg_handle = NULL;
	scf_property_t		*prop_handle = NULL;
	PyObject 		*pytuple = NULL;
	PyObject 		*pylist = NULL;
	PyObject 		*pyobject = NULL;
	char			**nmlist = NULL;
	char			**tplist = NULL;
	int			nmlen, listlen, l;
	char			*prname = NULL;
	scf_type_t		prtype;
	PyObject		*ret = NULL;
	const char		*tpname;

	/* get args */
	if (!PyArg_ParseTuple(args, "s", &pgname)) {
		FREE_INSTANCE();
		PyErr_SetString(PyExc_ValueError,
		    "Property group argument must be a string");
		return (NULL);
	}
	/* alloc temp storage */
	if ((iter = scf_iter_create(self->handle)) == NULL) {
		FREE_INSTANCE();
		PyErr_SetString(NssscfError, "Cannot create iterator");
		return (NULL);
	}
	if ((pg_handle = scf_pg_create(self->handle)) == NULL) {
		scf_iter_destroy(iter);
		FREE_INSTANCE();
		PyErr_SetString(NssscfError, "Cannot create pg handle");
		return (NULL);
	}
	if ((prop_handle = scf_property_create(self->handle)) == NULL) {
		scf_pg_destroy(pg_handle);
		scf_iter_destroy(iter);
		FREE_INSTANCE();
		PyErr_SetString(NssscfError, "Cannot create property handle");
		return (NULL);
	}
	nmlen = scf_limit(SCF_LIMIT_MAX_NAME_LENGTH) + 1;
	if ((prname = (char *)malloc(nmlen)) == NULL) {
		scf_property_destroy(prop_handle);
		scf_pg_destroy(pg_handle);
		scf_iter_destroy(iter);
		FREE_INSTANCE();
		PyErr_SetString(NssscfError, "Memory Error");
		return (NULL);
	}
	/* get property group from service */
	if (instance == NULL) {
		if (scf_service_get_pg(self->svc, pgname, pg_handle) != 0) {
			PyErr_SetString(NssscfError,
			    "Property group does not exist");
			ret = NULL;
			goto err;
		}
	} else {
		instnm = strrchr(instance, ':');
		if (instnm == NULL) {
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			ret = NULL;
			goto err;
		}
		instnm++;
		if (scf_service_get_instance(self->svc,
		    instnm, self->inst) < 0) {
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			ret = NULL;
			goto err;
		}
		if (scf_instance_get_pg_composed(self->inst, NULL,
		    pgname, pg_handle) != 0) {
			PyErr_SetString(NssscfError,
			    "Property group does not exist");
			ret = NULL;
			goto err;
		}
	}
	if (scf_iter_pg_properties(iter, pg_handle) < 0) {
		PyErr_SetString(NssscfError,
		    "Cannot create property iterator");
		ret = NULL;
		goto err;
	}

	/* setup and iterate properties in property group */
	while (scf_iter_next_property(iter, prop_handle) > 0) {
		if (scf_property_get_name(prop_handle, prname, nmlen) < 0)
			continue;
		if (scf_property_type(prop_handle, &prtype) < 0)
			continue;
		nmlist = __strlist_add(nmlist, prname);
		tpname = scf_type_to_string(prtype);
		tplist = __strlist_add(tplist, tpname);
	}
	if ((listlen = __strlist_len(nmlist)) > 0) {
		if ((pylist = PyList_New(0)) == NULL) {
			PyErr_SetString(NssscfError,
			    "Unable to build Property tuple");
			ret = NULL;
			goto err;
		}
		for (l = 0; l < listlen; l++) {
			pyobject = Py_BuildValue("(ss)", nmlist[l], tplist[l]);
			if (pyobject == NULL) {
				PyErr_SetString(NssscfError,
				    "Unable to build Property tuple");
				ret = NULL;
				goto err;
			}
			if (PyList_Append(pylist, pyobject) < 0) {
				PyErr_SetString(NssscfError,
				    "Unable to build Property tuple");
				ret = NULL;
				goto err;
			}
			Py_DECREF(pyobject);
			pyobject = NULL;
		}
		pytuple = PyList_AsTuple(pylist);
		Py_DECREF(pylist);
		pylist = NULL;
	} else {
		pytuple = Py_BuildValue("()");
	}
	ret = pytuple;
err:
	/* ErrStrings (if any) should be set by now */
	if (pyobject)
		Py_DECREF(pyobject);
	if (pylist)
		Py_DECREF(pylist);
	if (prname)
		free((void *)prname);
	FREE_INSTANCE();
	__strlist_free(nmlist);
	__strlist_free(tplist);
	scf_property_destroy(prop_handle);
	scf_pg_destroy(pg_handle);
	scf_iter_destroy(iter);
	return (ret);
}

/*
 * Get all Property Values for a pg/property
 * IN: arg1=pg, arg2=prop
 * OUT: Tuple of properties and types
 */

static PyObject *
Nssscf_get_propvals(Nssscf_t *self, PyObject *args)
{
	char			*instance = __getscf_instname(self);
	char			*instnm;
	const char		*pgname, *prname;
	scf_iter_t		*iter = NULL;
	scf_propertygroup_t	*pg_handle = NULL;
	scf_property_t		*prop_handle = NULL;
	scf_value_t		*value_handle = NULL;
	PyObject 		*pytuple = NULL;
	PyObject 		*pylist = NULL;
	PyObject 		*pyobject = NULL;
	char			**vallist = NULL;
	int			listlen, l;
	char			*buf = NULL;
	int			buflen;
	PyObject		*ret = NULL;

	/* get args */
	if (!PyArg_ParseTuple(args, "ss", &pgname, &prname)) {
		FREE_INSTANCE();
		PyErr_SetString(PyExc_ValueError,
		    "Property group and property names must be strings");
		return (NULL);
	}
	/* alloc temp storage */
	if ((iter = scf_iter_create(self->handle)) == NULL) {
		FREE_INSTANCE();
		PyErr_SetString(NssscfError, "Cannot create iterator");
		return (NULL);
	}
	if ((pg_handle = scf_pg_create(self->handle)) == NULL) {
		scf_iter_destroy(iter);
		FREE_INSTANCE();
		PyErr_SetString(NssscfError, "Cannot create pg handle");
		return (NULL);
	}
	if ((prop_handle = scf_property_create(self->handle)) == NULL) {
		scf_pg_destroy(pg_handle);
		scf_iter_destroy(iter);
		FREE_INSTANCE();
		PyErr_SetString(NssscfError, "Cannot create property handle");
		return (NULL);
	}
	if ((value_handle = scf_value_create(self->handle)) == NULL) {
		scf_property_destroy(prop_handle);
		scf_pg_destroy(pg_handle);
		scf_iter_destroy(iter);
		FREE_INSTANCE();
		PyErr_SetString(NssscfError, "Cannot create value handle");
		return (NULL);
	}
	buflen = scf_limit(SCF_LIMIT_MAX_VALUE_LENGTH) + 1;
	if ((buf = (char *)malloc(buflen)) == NULL) {
		scf_value_destroy(value_handle);
		scf_property_destroy(prop_handle);
		scf_pg_destroy(pg_handle);
		scf_iter_destroy(iter);
		FREE_INSTANCE();
		PyErr_SetString(NssscfError, "Memory Error");
		return (NULL);
	}
	/* get property group from service */
	if (instance == NULL) {
		if (scf_service_get_pg(self->svc, pgname, pg_handle) != 0) {
			PyErr_SetString(NssscfError,
			    "Property group does not exist");
			ret = NULL;
			goto err;
		}
	} else {
		instnm = strrchr(instance, ':');
		if (instnm == NULL) {
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			ret = NULL;
			goto err;
		}
		instnm++;
		if (scf_service_get_instance(self->svc,
		    instnm, self->inst) < 0) {
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			ret = NULL;
			goto err;
		}
		if (scf_instance_get_pg_composed(self->inst, NULL,
		    pgname, pg_handle) != 0) {
			PyErr_SetString(NssscfError,
			    "Property group does not exist");
			ret = NULL;
			goto err;
		}
	}
	/* get property from property group */
	if (scf_pg_get_property(pg_handle, prname, prop_handle) != 0) {
		PyErr_SetString(NssscfError, "Property does not exist");
		ret = NULL;
		goto err;
	}
	if (scf_iter_property_values(iter, prop_handle) < 0) {
		PyErr_SetString(NssscfError,
		    "Cannot create property value iterator");
		ret = NULL;
		goto err;
	}

	/* setup and iterate properties in property group */
	while (scf_iter_next_value(iter, value_handle) > 0) {
		if (scf_value_get_as_string(value_handle, buf, buflen) < 0)
			continue;
		vallist = __strlist_add(vallist, buf);
	}
	if ((listlen = __strlist_len(vallist)) > 0) {
		if ((pylist = PyList_New(0)) == NULL) {
			PyErr_SetString(NssscfError,
			    "Unable to build Property tuple");
			ret = NULL;
			goto err;
		}
		for (l = 0; l < listlen; l++) {
			pyobject = Py_BuildValue("s", vallist[l]);
			if (pyobject == NULL) {
				PyErr_SetString(NssscfError,
				    "Unable to build Property tuple");
				ret = NULL;
				goto err;
			}
			if (PyList_Append(pylist, pyobject) < 0) {
				PyErr_SetString(NssscfError,
				    "Unable to build Property tuple");
				ret = NULL;
				goto err;
			}
			Py_DECREF(pyobject);
			pyobject = NULL;
		}
		pytuple = PyList_AsTuple(pylist);
		Py_DECREF(pylist);
		pylist = NULL;
	} else {
		pytuple = Py_BuildValue("()");
	}
	ret = pytuple;
err:
	/* ErrStrings (if any) should be set by now */
	if (pyobject)
		Py_DECREF(pyobject);
	if (pylist)
		Py_DECREF(pylist);
	if (buf)
		free((void *)buf);
	FREE_INSTANCE();
	__strlist_free(vallist);
	scf_value_destroy(value_handle);
	scf_property_destroy(prop_handle);
	scf_pg_destroy(pg_handle);
	scf_iter_destroy(iter);
	return (ret);
}

/*
 * Get Service Property state
 * IN: nothing
 * OUT: String of current property state
 */

static PyObject *
Nssscf_getstate(Nssscf_t *self, PyObject *args)
{
	char		*state = __getscf_inststate(self);
	PyObject 	*pyobject = NULL;

	if (state == NULL) {
		PyErr_SetString(NssscfError, "Service is not an instance");
		return (NULL);
	}
	pyobject = Py_BuildValue("s", state);
	free((void *)state);
	return (pyobject);
}

/*
 * Get Service instance fmri
 * IN: nothing
 * OUT: String of current instance fmri
 */

static PyObject *
Nssscf_getinstfmri(Nssscf_t *self, PyObject *args)
{
	char		*instance = __getscf_instfmri(self);
	PyObject 	*pyobject = NULL;

	if (instance == NULL) {
		PyErr_SetString(NssscfError, "Service is not an instance");
		return (NULL);
	}
	pyobject = Py_BuildValue("s", instance);
	FREE_INSTANCE();
	return (pyobject);
}

	/* Test Operations */
/*
 * Check Service state
 * IN: nothing
 * OUT: True/False
 */

static PyObject *
Nssscf_isenabled(Nssscf_t *self, PyObject *args)
{
	char	*state = __getscf_inststate(self);
	int	enabled = 0;

	if (state == NULL) {
		PyErr_SetString(NssscfError, "Service is not an instance");
		return (NULL);
	}
	if (strcmp(state, SCF_STATE_STRING_ONLINE) == 0)
		enabled++;
	free((void *)state);
	if (enabled) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

/*
 * Test to see if a property group exists in the service
 * IN: args is a property group string
 * OUT: True/False
 */
static PyObject *
Nssscf_pgexists(Nssscf_t *self, PyObject *args)
{
	char	*instance = __getscf_instname(self);
	char	*instnm;
	scf_propertygroup_t	*pg_handle;
	const char	*pg;
	int	ret;

	if (!PyArg_ParseTuple(args, "s", &pg)) {
		FREE_INSTANCE();
		PyErr_SetString(PyExc_ValueError,
		    "The argument must be a string");
		return (NULL);
	}
	/* do work */
	pg_handle = scf_pg_create(self->handle);
	if (instance == NULL) {
		ret = scf_service_get_pg(self->svc, pg, pg_handle);
	} else {
		instnm = strrchr(instance, ':');
		if (instnm == NULL) {
			free((void *)instance);
			scf_pg_destroy(pg_handle);
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			return (NULL);
		}
		instnm++;
		if (scf_service_get_instance(self->svc,
		    instnm, self->inst) < 0) {
			free((void *)instance);
			scf_pg_destroy(pg_handle);
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			return (NULL);
		}
		ret = scf_instance_get_pg(self->inst, pg, pg_handle);
	}
	if (ret == 0) {
		/* ensure we have the most recent pg */
		(void) scf_pg_update(pg_handle);
	}
	scf_pg_destroy(pg_handle);
	FREE_INSTANCE();
	if (ret == 0) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

	/* Add/Modify/Delete Operations */
/*
 * Create a property group given a name and type
 * IN: arg1 is a property group name, arg2 is the type
 * OUT: True/False
 */
static PyObject *
Nssscf_add_pg(Nssscf_t *self, PyObject *args)
{
	char	*instance = __getscf_instname(self);
	char	*instnm;
	scf_propertygroup_t	*pg_handle;
	const char	*pg, *pgtype;
	int	ret;

	if (!PyArg_ParseTuple(args, "ss", &pg, &pgtype)) {
		FREE_INSTANCE();
		PyErr_SetString(PyExc_ValueError,
		    "The arguments must be strings");
		return (NULL);
	}
	/* do work */
	pg_handle = scf_pg_create(self->handle);
	if (instance == NULL) {
		ret = scf_service_add_pg(self->svc, pg, pgtype,
		    0, pg_handle);
	} else {
		instnm = strrchr(instance, ':');
		if (instnm == NULL) {
			FREE_INSTANCE();
			scf_pg_destroy(pg_handle);
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			return (NULL);
		}
		instnm++;
		if (scf_service_get_instance(self->svc,
		    instnm, self->inst) < 0) {
			FREE_INSTANCE();
			scf_pg_destroy(pg_handle);
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			return (NULL);
		}
		ret = scf_instance_add_pg(self->inst, pg, pgtype,
		    0, pg_handle);
	}
	scf_pg_destroy(pg_handle);
	FREE_INSTANCE();
	if (ret == 0) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

/*
 * Delete a property group and it's contents
 * IN: arg is a property group string
 * OUT: True/False
 */
static PyObject *
Nssscf_delete_pg(Nssscf_t *self, PyObject *args)
{
	char	*instance = __getscf_instname(self);
	char	*instnm;
	scf_propertygroup_t	*pg_handle;
	const char	*pg;
	int	ret;

	if (!PyArg_ParseTuple(args, "s", &pg)) {
		FREE_INSTANCE();
		PyErr_SetString(PyExc_ValueError,
		    "The argument must be a string");
		return (NULL);
	}
	/* do work */
	pg_handle = scf_pg_create(self->handle);
	if (instance == NULL) {
		ret = scf_service_get_pg(self->svc, pg, pg_handle);
	} else {
		instnm = strrchr(instance, ':');
		if (instnm == NULL) {
			FREE_INSTANCE();
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			return (NULL);
		}
		instnm++;
		if (scf_service_get_instance(self->svc,
		    instnm, self->inst) < 0) {
			FREE_INSTANCE();
			scf_pg_destroy(pg_handle);
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			return (NULL);
		}
		ret = scf_instance_get_pg(self->inst, pg, pg_handle);
	}
	if (ret == 0) {
		ret = scf_pg_delete(pg_handle);
	} else {
		ret = 0;		/* true if no property group exists */
	}
	scf_pg_destroy(pg_handle);
	FREE_INSTANCE();
	if (ret == 0) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

/*
 * Delcust (delete customizations) on a service property group
 * IN: arg is a property group string
 * OUT: True/False
 */
static PyObject *
Nssscf_delcust_pg(Nssscf_t *self, PyObject *args)
{
	scf_propertygroup_t	*pg_handle;
	const char	*pg;
	int	ret;

	if (!PyArg_ParseTuple(args, "s", &pg)) {
		PyErr_SetString(PyExc_ValueError,
		    "The argument must be a string");
		return (NULL);
	}
	/* do work */
	pg_handle = scf_pg_create(self->handle);
	ret = scf_service_get_pg(self->svc, pg, pg_handle);
	if (ret == 0) {
		ret = scf_pg_delcust(pg_handle);
	} else {
		ret = 0;		/* true if no property group exists */
	}
	scf_pg_destroy(pg_handle);
	if (ret == 0) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

/*
 * Create or add a property value given a name, type and value
 * IN: arg1 is a property group name, arg2 is the property
 *     arg3 is the type and arg4 is the value.  Value may be either
 *     a string or a tuple/list of strings.  The property is
 *     created if it does not previously exist.
 * OUT: True or Exception
 */
static PyObject *
Nssscf_set_propvalue(Nssscf_t *self, PyObject *args)
{
	char	*instance = __getscf_instname(self);
	char	*instnm;
	scf_propertygroup_t	*pg_handle = NULL;
	scf_property_t		*prop_handle = NULL;
	scf_value_t		*value_handle = NULL;
	scf_transaction_t	*tx = NULL;
	scf_transaction_entry_t	*ent = NULL;
	scf_type_t		ptype;
	const char		*pgname, *propname, *proptype;
	PyObject		*obj = NULL;
	PyObject		*item;
	char			*value, **vallist = NULL;
	int			n, i;
	int			badadd = 0;
	int			new_prop = 0;
	int			ret = -1;		/* fail by default */

	if (!PyArg_ParseTuple(args, "sssO",
	    &pgname, &propname, &proptype, &obj)) {
		FREE_INSTANCE();
		PyErr_SetString(PyExc_ValueError, "Invalid arguments");
		return (NULL);
	}
	/* examine obj for string or sequence */
	if (obj == NULL) {
		FREE_INSTANCE();
		PyErr_SetString(PyExc_ValueError, "Invalid arguments");
		return (NULL);
	} else if (PyString_Check(obj)) {
		value = PyString_AsString(obj);
		vallist = __strlist_add(vallist, value);
	} else if (PyTuple_Check(obj) || PyList_Check(obj)) {
		if ((n = PySequence_Length(obj)) > 0) {
			for (i = 0; i < n; i++) {
				item = PySequence_GetItem(obj, i);
				if (item == NULL) {
					PyErr_SetString(PyExc_ValueError,
					    "Invalid tuple");
					badadd = -1;
					break;
				}
				if (!PyString_Check(item)) {
					PyErr_SetString(PyExc_ValueError,
					    "Tuple value not a string");
					badadd = -1;
					break;
				}
				value = PyString_AsString(item);
				vallist = __strlist_add(vallist, value);
			}
		}
	} else {
		FREE_INSTANCE();
		PyErr_SetString(PyExc_ValueError,
		    "The value must be a string or tuple of strings");
		return (NULL);
	}
	if (vallist == NULL || badadd) {
		__strlist_free(vallist);
		FREE_INSTANCE();
		PyErr_SetString(PyExc_ValueError,
		    "The value must be a string or tuple of strings");
		return (NULL);
	}
	/* do work */
	if ((pg_handle = scf_pg_create(self->handle)) == NULL) {
		__strlist_free(vallist);
		FREE_INSTANCE();
		PyErr_SetString(NssscfError, "Cannot create pg handle");
		return (NULL);
	}
	if ((prop_handle = scf_property_create(self->handle)) == NULL) {
		scf_pg_destroy(pg_handle);
		__strlist_free(vallist);
		FREE_INSTANCE();
		PyErr_SetString(NssscfError, "Cannot create property handle");
		return (NULL);
	}
	/* get property group from service */
	if (instance == NULL) {
		if (scf_service_get_pg(self->svc, pgname, pg_handle) != 0) {
			PyErr_SetString(NssscfError,
			    "Property group does not exist");
			ret = -1;
			goto err;
		}
	} else {
		instnm = strrchr(instance, ':');
		if (instnm == NULL) {
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			ret = -1;
			goto err;
		}
		instnm++;
		if (scf_service_get_instance(self->svc,
		    instnm, self->inst) < 0) {
			PyErr_SetString(NssscfError,
			    "Service is not an instance");
			ret = -1;
			goto err;
		}
		if (scf_instance_get_pg_composed(self->inst, NULL,
		    pgname, pg_handle) != 0) {
			PyErr_SetString(NssscfError,
			    "Property group does not exist");
			ret = -1;
			goto err;
		}
	}
	/* get property from property group */
	if (scf_pg_get_property(pg_handle, propname, prop_handle) != 0) {
		if (scf_error() == SCF_ERROR_NOT_FOUND)
			new_prop = 1;
		else {
			PyErr_SetString(NssscfError,
			    "Get property error");
			ret = -1;
			goto err;
		}
	}
	/* setup transaction */
	if ((tx = scf_transaction_create(self->handle)) == NULL ||
	    (ent = scf_entry_create(self->handle)) == NULL) {
		PyErr_SetString(NssscfError,
		    "Could not create transaction or transaction entry");
		ret = -1;
		goto err;
	}
	if (scf_transaction_start(tx, pg_handle) == -1) {
		PyErr_SetString(NssscfError,
		    "Could not start transaction");
		ret = -1;
		goto err;
	}
	/* setup/create property */
	ptype = scf_string_to_type(proptype);
	if (ptype == SCF_TYPE_INVALID) {
		PyErr_SetString(NssscfError,
		    "Invalid property type");
		ret = -1;
		goto err;
	}
	if (new_prop) {
		if (scf_transaction_property_new(tx, ent, propname,
		    ptype) == -1) {
			PyErr_SetString(NssscfError,
			    "Could not create new property transaction");
			ret = -1;
			goto err;
		}
	} else if (scf_transaction_property_change(tx, ent, propname,
	    ptype) == -1)  {
		PyErr_SetString(NssscfError,
		    "Could not create change property transaction");
		ret = -1;
		goto err;
	}

	/* add value(s) */
	for (i = 0; vallist[i] != NULL; i++) {
		if ((value_handle = scf_value_create(self->handle)) == NULL) {
			PyErr_SetString(NssscfError,
			    "Cannot create value handle");
			ret = -1;
			goto err;
		}
		if (scf_value_set_from_string(value_handle,
		    ptype, vallist[i]) == -1) {
			PyErr_SetString(NssscfError,
			    "Cannot create value string");
			ret = -1;
			goto err;
		}
		if (scf_entry_add_value(ent, value_handle) != 0) {
			PyErr_SetString(NssscfError,
			    "Cannot add string to entry");
			ret = -1;
			goto err;
		}
	}

	/* commit */
	if (scf_transaction_commit(tx) > 0) {
		ret = 0;		/* Successful commit */
	} else {
		PyErr_SetString(NssscfError,
		    "Cannot commit values to property");
		ret = -1;
	}
	scf_transaction_reset(tx);
	scf_entry_destroy_children(ent);
	if (scf_pg_update(pg_handle) == -1) {
		PyErr_SetString(NssscfError,
		    "Property group update failure");
		ret = -1;
	}
err:
	scf_transaction_destroy(tx);
	scf_value_destroy(value_handle);
	scf_property_destroy(prop_handle);
	scf_pg_destroy(pg_handle);
	__strlist_free(vallist);
	FREE_INSTANCE();
	if (ret == 0) {
		Py_RETURN_TRUE;
	}
	return (NULL);
}

	/* Admin Operations */
/*
 * Generic Service state changer
 * IN: fmri and new state (fmri defaults to fmri:default if not an instance)
 * OUT: True/False
 */

typedef enum {
	OP_ENABLE, OP_DISABLE, OP_REFRESH, OP_RESTART,
	OP_MAINTAIN, OP_DEGRADE, OP_RESTORE
} nssscf_state_t;

static PyObject *
Nssscf_change_state(Nssscf_t *self, nssscf_state_t state)
{
	char	*instance = __getscf_instfmri(self);
	int	success;

	if (instance == NULL) {
		PyErr_SetString(NssscfError, "Service is not an instance");
		return (NULL);
	}
	switch (state) {
	case OP_ENABLE:
		success = smf_enable_instance(instance, 0);
		break;
	case OP_DISABLE:
		success = smf_disable_instance(instance, 0);
		break;
	case OP_REFRESH:
		success = smf_refresh_instance(instance);
		break;
	case OP_RESTART:
		success = smf_restart_instance(instance);
		break;
	case OP_MAINTAIN:
		success = smf_maintain_instance(instance, SMF_IMMEDIATE);
		break;
	case OP_DEGRADE:
		success = smf_degrade_instance(instance, SMF_IMMEDIATE);
		break;
	case OP_RESTORE:
		success = smf_restore_instance(instance);
		break;
	default:
		success = 0;
	}

	FREE_INSTANCE();
	if (success == 0) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

static PyObject *
Nssscf_inst_enable(Nssscf_t *self, PyObject *args)
{
	return (Nssscf_change_state(self, OP_ENABLE));
}

static PyObject *
Nssscf_inst_disable(Nssscf_t *self, PyObject *args)
{
	return (Nssscf_change_state(self, OP_DISABLE));
}

static PyObject *
Nssscf_inst_refresh(Nssscf_t *self, PyObject *args)
{
	return (Nssscf_change_state(self, OP_REFRESH));
}

static PyObject *
Nssscf_inst_restart(Nssscf_t *self, PyObject *args)
{
	return (Nssscf_change_state(self, OP_RESTART));
}

static PyObject *
Nssscf_inst_maintain(Nssscf_t *self, PyObject *args)
{
	return (Nssscf_change_state(self, OP_MAINTAIN));
}

static PyObject *
Nssscf_inst_degrade(Nssscf_t *self, PyObject *args)
{
	return (Nssscf_change_state(self, OP_DEGRADE));
}

static PyObject *
Nssscf_inst_restore(Nssscf_t *self, PyObject *args)
{
	return (Nssscf_change_state(self, OP_RESTORE));
}

static PyObject *
Nssscf_inst_validate(Nssscf_t *self, PyObject *args)
{
	PyObject *pytuple = NULL;
	char	*instance = __getscf_instfmri(self);
	scf_tmpl_errors_t *errs = NULL;
	char	*msg = NULL;
	int	success;

	if (instance == NULL) {
		PyErr_SetString(NssscfError, "Service is not an instance");
		return (NULL);
	}
	success = scf_tmpl_validate_fmri(self->handle, instance,
	    NULL, &errs, SCF_TMPL_VALIDATE_FLAG_CURRENT);

	if (success == 1 && errs != NULL) {
		scf_tmpl_error_t *err = NULL;
		size_t len = 256;	/* initial error buffer size */

		msg = (char *)malloc(len);
		while ((err = scf_tmpl_next_error(errs)) != NULL) {
			int ret;

			if ((ret = scf_tmpl_strerror(err, msg, len,
			    0)) >= len) {
				len = ret + 1;
				msg = realloc(msg, len);
				if (msg == NULL) {
					PyErr_SetString(NssscfError,
					    "Out of memory.\n");
					Py_RETURN_FALSE;
				}
				(void) scf_tmpl_strerror(err, msg, len, 0);
			}
		}
	}
	if (errs != NULL)
		scf_tmpl_errors_destroy(errs);
	FREE_INSTANCE();
	if (success == 0) {
		pytuple = Py_BuildValue("(is)", 0, NULL);
	} else {
		if (msg) {
			pytuple = Py_BuildValue("(is)", -1, msg);
			free(msg);
		} else {
			PyErr_SetString(NssscfError,
			    "Unknown Validate Error.\n");
			Py_RETURN_FALSE;
		}
	}
	return (pytuple);
}

/*
 * =====================================
 * Nssscf Instance structure definitions
 * =====================================
 */

static PyGetSetDef Nssscf_getseters[] = {
	{ "service", (getter)Nssscf_getservice, (setter)Nssscf_setservice,
	    "service name", NULL },
	{NULL}  /* Sentinel */
};

static PyMemberDef Nssscf_members[] = {
	{NULL}  /* Sentinel */
};

static PyMethodDef Nssscf_methods[] = {
	/* Get Operations */
	{ "get_service_pgs", (PyCFunction)Nssscf_get_svcpgs, METH_NOARGS,
	    "Get the list of the services property groups" },
	{ "get_properties", (PyCFunction)Nssscf_get_pgprops, METH_VARARGS,
	    "Get list of properties for a property group (arg1)" },
	{ "get_prop_values", (PyCFunction)Nssscf_get_propvals, METH_VARARGS,
	    "Get values for property group, property (arg1, arg2)" },
	{ "get_state", (PyCFunction)Nssscf_getstate, METH_NOARGS,
	    "Get Service state" },
	{ "get_instfmri", (PyCFunction)Nssscf_getinstfmri, METH_NOARGS,
	    "Get Service instance FMRI.  (May be identical to service name)" },
	/* Test Operations */
	{ "is_enabled", (PyCFunction)Nssscf_isenabled, METH_NOARGS,
	    "Check if service is enabled" },
	{ "pg_exists", (PyCFunction)Nssscf_pgexists, METH_VARARGS,
	    "True/False if property group (arg1) exists" },
	/* Add/Modify/Delete Operations */
	{ "add_pg", (PyCFunction)Nssscf_add_pg, METH_VARARGS,
	    "Add a property group given a pgname and pgtype (arg1,arg2)" },
	{ "set_propvalue", (PyCFunction)Nssscf_set_propvalue, METH_VARARGS,
	    "Set pg/prop to value(s): pg, name type, value or (value...)" },
	{ "delete_pg", (PyCFunction)Nssscf_delete_pg, METH_VARARGS,
	    "Delete a property group [and it's contents]. (arg1)" },
	{ "delcust_pg", (PyCFunction)Nssscf_delcust_pg, METH_VARARGS,
	    "Delete customizations on a service property group. (arg1)" },
	/* Admin Operations */
	{ "enable", (PyCFunction)Nssscf_inst_enable, METH_NOARGS,
	    "Enable service instance" },
	{ "disable", (PyCFunction)Nssscf_inst_disable, METH_NOARGS,
	    "Disable service instance" },
	{ "refresh", (PyCFunction)Nssscf_inst_refresh, METH_NOARGS,
	    "Refresh service instance" },
	{ "restart", (PyCFunction)Nssscf_inst_restart, METH_NOARGS,
	    "Restart service instance" },
	{ "maintain", (PyCFunction)Nssscf_inst_maintain, METH_NOARGS,
	    "Immediately send service instance to maintenance state" },
	{ "degrade", (PyCFunction)Nssscf_inst_degrade, METH_NOARGS,
	    "Immediately send service instance to degraded state" },
	{ "restore", (PyCFunction)Nssscf_inst_restore, METH_NOARGS,
	    "Restore service instance" },
	{ "validate", (PyCFunction)Nssscf_inst_validate, METH_NOARGS,
	    "Validate current service instance configuration" },
	{NULL}  /* Sentinel */
};

static PyTypeObject nssscf_NssscfType = {
	PyObject_HEAD_INIT(NULL)
	0,				/* ob_size */
	"nssscf.Nssscf",		/* tp_name */
	sizeof (Nssscf_t),		/* tp_basicsize */
	0,				/* tp_itemsize */
	(destructor)Nssscf_dealloc,	/* tp_dealloc */
	0,				/* tp_print */
	0,				/* tp_getattr */
	0,				/* tp_setattr */
	0,				/* tp_compare */
	0,				/* tp_repr */
	0,				/* tp_as_number */
	0,				/* tp_as_sequence */
	0,				/* tp_as_mapping */
	0,				/* tp_hash */
	0,				/* tp_call */
	0,				/* tp_str */
	0,				/* tp_getattro */
	0,				/* tp_setattro */
	0,				/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
	"Nssscf objects",		/* tp_doc */
	0,				/* tp_traverse */
	0,				/* tp_clear */
	0,				/* tp_richcompare */
	0,				/* tp_weaklistoffset */
	0,				/* tp_iter */
	0,				/* tp_iternext */
	Nssscf_methods,			/* tp_methods */
	Nssscf_members,			/* tp_members */
	Nssscf_getseters,		/* tp_getset */
	0,				/* tp_base */
	0,				/* tp_dict */
	0,				/* tp_descr_get */
	0,				/* tp_descr_set */
	0,				/* tp_dictoffset */
	(initproc)Nssscf_init,		/* tp_init */
	0,				/* tp_alloc */
	Nssscf_new,			/* tp_new */
};

/*
 * =============================
 * Nssscf library Initialization
 * =============================
 */

static PyMethodDef NssscfMethods[] = {
	{ "ns1_convert", (PyCFunction)Nssscf_ns1_convert, METH_VARARGS,
	    "SUNW Project Private" },
	{ NULL, NULL, 0, NULL }		/* Sentinel */
};

PyMODINIT_FUNC
initnssscf(void)
{
	PyObject *m;

	if (PyType_Ready(&nssscf_NssscfType) < 0)
		return;

	m = Py_InitModule("nssscf", NssscfMethods);
	if (m == NULL)
		return;

	/* Create Nsscf Exception */
	NssscfError = PyErr_NewException("nssscf.error", NULL, NULL);
	Py_INCREF(NssscfError);
	(void) PyModule_AddObject(m, "error", NssscfError);

	/* Create Nssscf Module */
	Py_INCREF(&nssscf_NssscfType);
	(void) PyModule_AddObject(m, "Nssscf", (PyObject *)&nssscf_NssscfType);
}


/*
 * ===============================
 * Nssscf library helper functions
 * ===============================
 */

static int
__strlist_len(char **list)
{
	int count = 0;

	if (list == NULL)
		return (0);
	for (; *list != NULL; count++, list++)
		;
	return (count);
}

static char **
__strlist_add(char **list, const char *string)
{
	int	len;
	char	*s, **nl;

	if (string == NULL)
		return (list);
	if ((s = strdup(string)) == NULL)
		return (list);
	if (list == NULL) {
		nl = (char **)calloc(2, sizeof (char *));
		if (nl == NULL) {
			free((void *)s);
			return (NULL);
		}
		nl[0] = s;
		return (nl);
	}
	len = __strlist_len(list);
	nl = (char **) realloc((void *)list, sizeof (char *) * (len + 2));
	if (nl == NULL)
		return (NULL);
	nl[len] = s;
	nl[len+1] = NULL;
	return (nl);
}

static void __strlist_free(char **list)
{
	char	**nl;
	if (list == NULL)
		return;
	for (nl = list; *nl != NULL; nl++)
		free((void *)(*nl));
	free((void *)list);
}

/*
 * Get the full AS-IS service name.  [svc:/]service[:instance]
 * caller must prune if/as needed.  Caller must free string.
 */

static char *
__getscf_svcname(PyObject *service)
{
	char	*svcstr = NULL;
	char	*svcn;

	if ((svcstr = PyString_AsString(service)) == NULL)
		return (NULL);
	if (*svcstr == '\0')
		return (NULL);
	if ((svcn = strdup(svcstr)) == NULL)
		return (NULL);
	return (svcn);
}

/*
 * Get the service name if it is an instance, NULL otherwise
 * Caller must free string.
 */
static char *
__getscf_instname(Nssscf_t *self)
{
	char	*svcname, *svcn;

	if ((svcn = __getscf_svcname(self->service)) == NULL)
		return (NULL);
	svcname = svcn;
	if (strncmp("svc:/", svcname, 5) == 0)
		svcname += 5;		/* trim svc:/ if provided */
	if (strchr(svcname, ':') != NULL)
		return (svcn);		/* service has an instance name */
	free((void *)svcn);		/* free strdup storage */
	return (NULL);
}

/*
 * Get the service name if it is not and instance, append :default
 * to it's name.  Caller must free string.
 */
static char *
__getscf_instfmri(Nssscf_t *self)
{
	char	*svcname, *svcn;
	int	newlen;

	if ((svcn = __getscf_svcname(self->service)) == NULL)
		return (NULL);
	svcname = svcn;
	if (strncmp("svc:/", svcname, 5) == 0)
		svcname += 5;		/* trim svc:/ if provided */
	if (strchr(svcname, ':') != NULL)
		return (svcn);		/* service is an instance name */
	newlen = strlen(svcn) + 8 + 1;	/* svcn + ":default" + NUL */
	if ((svcname = realloc(svcn, newlen)) == NULL)
		return (NULL);		/* storage problem return NULL */
	(void) strlcat(svcname, ":default", newlen);
	return (svcname);
}

/*
 * Sets the Python instance's smf service handle
 */
static int
__setscf_svc(Nssscf_t *self, PyObject *service)
{
	char	*svcname, *svcn, *inst;
	int	ret;

	if ((svcn = __getscf_svcname(service)) == NULL)
		return (-1);
	svcname = svcn;
	if (strncmp("svc:/", svcname, 5) == 0)
		svcname += 5;		/* trim svc:/ if provided */
	if ((inst = strchr(svcname, ':')) != NULL) {
		*inst = '\0';		/* trim instance if provided */
	}
	ret = scf_scope_get_service(self->scope, svcname, self->svc);
	free((void *)svcn);		/* free strdup storage */
	return (ret);
}

/*
 * Given a service name, if it is an instance, return the
 * current state.  If service is not aninstance try fmri:default'
 * NULL otherwise
 * Caller must free string.
 */
static char *
__getscf_inststate(Nssscf_t *self)
{
	char	*instance = __getscf_instfmri(self);
	char	*state;

	if (instance == NULL) {
		PyErr_SetString(NssscfError, "Service is not an instance");
		return (NULL);
	}
	state = smf_get_state(instance);
	FREE_INSTANCE();
	return (state);
}

/*
 * NS1 encrypt/decrypt APIs
 */

#include "ns_crypt.c"

static PyObject *
Nssscf_ns1_convert(Nssscf_t *self, PyObject *args, PyObject *kwds)
{
	const char	*input;
	char		*output;
	PyObject	*ret = NULL;
	if (!PyArg_ParseTuple(args, "s", &input)) {
		PyErr_SetString(PyExc_ValueError,
		    "Argument must be a string");
		return (NULL);
	}
	if (strncmp("{NS1}", input, 5) == 0) {		/* decode */
		output = dvalue((char *)input);
	} else {					/* encode */
		output = evalue((char *)input);
	}
	if (output == NULL) {
		PyErr_SetString(PyExc_ValueError,
		    "Illegal value");
		return (NULL);
	}
	ret = Py_BuildValue("s", output);
	free(output);
	return (ret);
}
