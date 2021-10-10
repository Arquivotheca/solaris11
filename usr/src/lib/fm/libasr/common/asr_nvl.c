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

#include <stdlib.h>
#include <stdarg.h>
#include <strings.h>
#include <sys/varargs.h>
#include <time.h>

#include "asr_buf.h"
#include "asr_err.h"
#include "asr_mem.h"
#include "asr_nvl.h"

/*
 * libnvpair wrappers
 */

nvlist_t *
asr_nvl_alloc()
{
	nvlist_t *nvl;

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0) {
		(void) asr_set_errno(EASR_NOMEM);
		return (NULL);
	}

	return (nvl);
}

nvlist_t *
asr_nvl_dup(nvlist_t *src)
{
	nvlist_t *dst;

	if (nvlist_dup(src, &dst, 0) != 0) {
		(void) asr_set_errno(EASR_NOMEM);
		return (NULL);
	}

	return (dst);
}

int
asr_nvl_merge(nvlist_t *dst, nvlist_t *src)
{
	if (nvlist_merge(dst, src, 0) != 0) {
		return (asr_set_errno(EASR_NOMEM));
	}

	return (ASR_OK);
}

/*
 * Frees an nvlist if it isn't null
 */
void
asr_nvl_free(nvlist_t *nvl)
{
	if (nvl != NULL)
		nvlist_free(nvl);
}

/*
 * Adds a string to a name value list.  If the value is NULL then nothing is
 * added.
 * Returns non-zero if there is an error.
 */
int
asr_nvl_add_str(
    nvlist_t *nvl, const char *property, const char *value)
{
	int err = 0;
	if (nvl == NULL || property == NULL)
		return (ASR_FAILURE);

	if (value != NULL)
		err = nvlist_add_string(nvl, property, value);

	return (err);
}

/*
 * Removes a string property from  a name value list.
 * Returns non-zero if there is an error.
 */
int
asr_nvl_rm_str(nvlist_t *nvl, const char *property)
{
	int err = 0;
	if (nvl == NULL || property == NULL)
		return (ASR_FAILURE);

	err = nvlist_remove(nvl, property, DATA_TYPE_STRING);

	return (err);
}

/*PRINTFLIKE3*/
int
asr_nvl_add_strf(nvlist_t *nvl, const char *property,
		const char *format, ...)
{
	va_list ap;
	size_t len;
	char *value;
	int err = 0;

	va_start(ap, format);
	len = vsnprintf(NULL, 0, format, ap) + 1;
	va_end(ap);

	value = asr_alloc(len);
	if (value == NULL)
		return (ASR_FAILURE);

	va_start(ap, format);
	if (vsnprintf(value, len, format, ap) < 0)
		err = -1;
	va_end(ap);

	if (err == 0)
		err = nvlist_add_string(nvl, property, value);

	free(value);
	return (err);
}

/*
 * Gets a string value form an nvlist.  If the property isn't found or there
 * is an error then NULL is returned
 */
char *
asr_nvl_str(nvlist_t *nvl, const char *property_name)
{
	int err;
	char *value;
	err = nvlist_lookup_string(nvl, property_name, &value);
	if (err != 0) {
		return (NULL);
	}
	return (value);
}

/*
 * Copies a property value from a source nvlist to a dest list.  If the value
 * is NULL then it is removed in the dest list.
 */
int
asr_nvl_cp_str(nvlist_t *src, nvlist_t *dest, const char *prop)
{
	char *val = asr_nvl_str(src, prop);
	if (val == NULL)
		return (asr_nvl_rm_str(dest, prop));
	else
		return (asr_nvl_add_str(dest, prop, val));
}


/*
 * Copies a property value from a source nvlist to a dest list.  If the value
 * is NULL then the default value is used.
 */
int
asr_nvl_cp_strd(nvlist_t *src, nvlist_t *dest, const char *prop, char *dval)
{
	char *val = asr_nvl_strd(src, prop, dval);
	if (val == NULL)
		val = dval;
	if (val == NULL)
		return (asr_nvl_rm_str(dest, prop));
	else
		return (asr_nvl_add_str(dest, prop, val));
}

/*
 * Gets a string value form an nvlist. If the property isn't found or there
 * is an error then the default value is returned
 */
char *
asr_nvl_strd(nvlist_t *nvl, const char *prop, char *dval)
{
	int err;
	char *value;
	err = nvlist_lookup_string(nvl, prop, &value);
	if (err != 0) {
		return (dval);
	}
	return (value);
}

int
asr_nvl_logf(FILE *logfile, time_t *logtime, nvlist_t *nvl)
{
	int err = 0;
	int out;
	char timebuf[128];
	struct tm *tmptr;
	time_t now;

	if (logtime == NULL) {
		now = time(NULL);
		logtime = &now;
	}
	tmptr = localtime(logtime);
	(void) strftime(timebuf, sizeof (timebuf), "%c", tmptr);
	out = fprintf(logfile, "%s\n", timebuf);
	if (out < 0) {
		err++;
	} else {
		asr_nvl_print_json(logfile, nvl);
		out = fputc('\n', logfile);
		if (out == EOF) {
			err++;
		}
	}
	return (err);
}

static int
asr_nvl_print_prop_string(FILE *fp, char *str)
{
	int i;
	char c;
	if (str != NULL) {
		for (i = 0, c = str[0]; c != '\0'; c = str[++i]) {
			if (c == '\n') {
				if (EOF == fputs("\\n", fp))
					return (ASR_FAILURE);
			} else if (c == '\t') {
				if (EOF == fputs("\\t", fp))
					return (ASR_FAILURE);
			} else if (c == '\\') {
				if (EOF == fputs("\\\\", fp))
				return (ASR_FAILURE);
			} else if (c == '"') {
				if (EOF == fputc('\\', fp))
				return (ASR_FAILURE);
				if (EOF == fputc('"', fp))
					return (ASR_FAILURE);
			} else if (c == '\'') {
				if (EOF == fputc('\\', fp))
					return (ASR_FAILURE);
				if (EOF == fputc('\'', fp))
					return (ASR_FAILURE);
			} else {
				if (EOF == fputc(c, fp))
					return (ASR_FAILURE);
			}
		}
	}
	return (ASR_OK);
}

/*
 * Prints elements in an event buffer in property format
 */
static int
asr_nvl_print_prop_path(FILE *fp, nvlist_t *nvl, char *path)
{
	int err = 0;
	int i;
	char *name;
	uint_t nelem;
	nvpair_t *nvp;
	asr_buf_t *buf;

	if (nvl == NULL)
		return (ASR_FAILURE);
	if ((buf = asr_buf_alloc(64)) == NULL)
		return (ASR_FAILURE);

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL && err == 0;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {

		data_type_t type = nvpair_type(nvp);
		name = nvpair_name(nvp);

		if (type == DATA_TYPE_NVLIST) {
			char *pname;
			nvlist_t *val;

			if (path != NULL) {
				asr_buf_reset(buf);
				if ((err = asr_buf_append(
				    buf, "%s.%s", path, name)) != 0)
					break;
				pname = asr_buf_data(buf);
			} else {
				pname = name;
			}

			(void) nvpair_value_nvlist(nvp, &val);
			err = asr_nvl_print_prop_path(fp, val, pname);
			continue;
		}
		if (type == DATA_TYPE_NVLIST_ARRAY) {
			nvlist_t **val;

			(void) nvpair_value_nvlist_array(nvp, &val, &nelem);
			for (i = 0; i < nelem; i++) {
				asr_buf_reset(buf);
				if (path == NULL)
					err = asr_buf_append(buf,
					    "%s.%d", name, i);
				else
					err = asr_buf_append(buf,
					    "%s.%s.%d", path, name, i);
				if (err != 0)
					break;
				err = asr_nvl_print_prop_path(fp, val[i],
				    asr_buf_data(buf));
			}
			continue;
		}

		if (path != NULL) {
			if (fprintf(fp, "%s.%s=", path, name) < 0)
				err = ASR_FAILURE;
		} else {
			if (fprintf(fp, "%s=", name) < 0)
				err = ASR_FAILURE;
		}
		if (err != 0)
			break;
		nelem = 0;
		switch (type) {
		case DATA_TYPE_BOOLEAN:
		{
			if (fprintf(fp, "1") < 0)
				err = ASR_FAILURE;
			break;
		}
		case DATA_TYPE_BOOLEAN_VALUE:
		{
			boolean_t val;
			(void) nvpair_value_boolean_value(nvp, &val);
			if (fprintf(fp, "%d", val) < 0)
				err = ASR_FAILURE;
			break;
		}
		case DATA_TYPE_BYTE:
		{
			uchar_t val;
			(void) nvpair_value_byte(nvp, &val);
			if (fprintf(fp, "0x%2.2x", val) < 0)
				err = ASR_FAILURE;
			break;
		}
		case DATA_TYPE_INT8:
		{
			int8_t val;
			(void) nvpair_value_int8(nvp, &val);
			if (fprintf(fp, "%d", val) < 0)
				err = ASR_FAILURE;
			break;
		}
		case DATA_TYPE_UINT8:
		{
			uint8_t val;
			(void) nvpair_value_uint8(nvp, &val);
			if (fprintf(fp, "0x%x", val) < 0)
				err = ASR_FAILURE;
			break;
		}
		case DATA_TYPE_INT16:
		{
			int16_t val;
			(void) nvpair_value_int16(nvp, &val);
			if (fprintf(fp, "%d", val) < 0)
				err = ASR_FAILURE;
			break;
		}
		case DATA_TYPE_UINT16:
		{
			uint16_t val;
			(void) nvpair_value_uint16(nvp, &val);
			if (fprintf(fp, "0x%x", val) < 0)
				err = ASR_FAILURE;
			break;
		}
		case DATA_TYPE_INT32:
		{
			int32_t val;
			(void) nvpair_value_int32(nvp, &val);
			if (fprintf(fp, "%d", val) < 0)
				err = ASR_FAILURE;
			break;
		}
		case DATA_TYPE_UINT32:
		{
			uint32_t val;
			(void) nvpair_value_uint32(nvp, &val);
			if (fprintf(fp, "0x%x", val) < 0)
				err = ASR_FAILURE;
			break;
		}
		case DATA_TYPE_INT64:
		{
			int64_t val;
			(void) nvpair_value_int64(nvp, &val);
			if (fprintf(fp, "%lld", (longlong_t)val) < 0)
				err = ASR_FAILURE;
			break;
		}
		case DATA_TYPE_UINT64:
		{
			uint64_t val;
			(void) nvpair_value_uint64(nvp, &val);
			if (fprintf(fp, "0x%llx", (u_longlong_t)val) < 0)
				err = ASR_FAILURE;
			break;
		}
#ifdef DATA_TYPE_DOUBLE
		case DATA_TYPE_DOUBLE:
		{
			double val;
			(void) nvpair_value_double(nvp, &val);
			if (fprintf(fp, "0x%llf", val) < 0)
				err = ASR_FAILURE;
			break;
		}
#endif
		case DATA_TYPE_STRING:
		{
			char *val;
			(void) nvpair_value_string(nvp, &val);
			err = asr_nvl_print_prop_string(fp, val);
			break;
		}
		case DATA_TYPE_BOOLEAN_ARRAY:
		{
			boolean_t *val;
			(void) nvpair_value_boolean_array(nvp, &val, &nelem);
			for (i = 0; i < nelem && err == 0; i++) {
				if (fprintf(fp, "%d", val[i]) < 0)
					err = ASR_FAILURE;
				if (i < nelem - 1)
					if (fprintf(fp, ",") < 0)
						err = ASR_FAILURE;
			}
			break;
		}
		case DATA_TYPE_BYTE_ARRAY:
		{
			uchar_t *val;
			(void) nvpair_value_byte_array(nvp, &val, &nelem);
			for (i = 0; i < nelem && err == 0; i++) {
				if (fprintf(fp, "0x%2.2x", val[i]) < 0)
					err = ASR_FAILURE;
				if (i < nelem - 1)
					if (fprintf(fp, ",") < 0)
						err = ASR_FAILURE;
			}
			break;
		}
		case DATA_TYPE_INT8_ARRAY:
		{
			int8_t *val;
			(void) nvpair_value_int8_array(nvp, &val, &nelem);
			for (i = 0; i < nelem && err == 0; i++) {
				if (fprintf(fp, "%d", val[i]) < 0)
					err = ASR_FAILURE;
				if (i < nelem - 1)
					if (fprintf(fp, ",") < 0)
						err = ASR_FAILURE;
			}
			break;
		}
		case DATA_TYPE_UINT8_ARRAY:
		{
			uint8_t *val;
			(void) nvpair_value_uint8_array(nvp, &val, &nelem);
			for (i = 0; i < nelem && err == 0; i++) {
				if (fprintf(fp, "0x%x", val[i]) < 0)
					err = ASR_FAILURE;
				if (i < nelem - 1)
					if (fprintf(fp, ",") < 0)
						err = ASR_FAILURE;
			}
			break;
		}
		case DATA_TYPE_INT16_ARRAY:
		{
			int16_t *val;
			(void) nvpair_value_int16_array(nvp, &val, &nelem);
			for (i = 0; i < nelem && err == 0; i++) {
				if (fprintf(fp, "%d", val[i]) < 0)
					err = ASR_FAILURE;
				if (i < nelem - 1)
					if (fprintf(fp, ",") < 0)
						err = ASR_FAILURE;
			}
			break;
		}
		case DATA_TYPE_UINT16_ARRAY:
		{
			uint16_t *val;
			(void) nvpair_value_uint16_array(nvp, &val, &nelem);
			for (i = 0; i < nelem && err == 0; i++) {
				if (fprintf(fp, "0x%x", val[i]) < 0)
					err = ASR_FAILURE;
				if (i < nelem - 1)
					if (fprintf(fp, ",") < 0)
						err = ASR_FAILURE;
			}
			break;
		}
		case DATA_TYPE_INT32_ARRAY:
		{
			int32_t *val;
			(void) nvpair_value_int32_array(nvp, &val, &nelem);
			for (i = 0; i < nelem && err == 0; i++) {
				if (fprintf(fp, "%d", val[i]) < 0)
					err = ASR_FAILURE;
				if (i < nelem - 1)
					if (fprintf(fp, ",") < 0)
						err = ASR_FAILURE;
			}
			break;
		}
		case DATA_TYPE_UINT32_ARRAY:
		{
			uint32_t *val;
			(void) nvpair_value_uint32_array(nvp, &val, &nelem);
			for (i = 0; i < nelem && err == 0; i++) {
				if (fprintf(fp, "0x%x", val[i]) < 0)
					err = ASR_FAILURE;
				if (i < nelem - 1)
					if (fprintf(fp, ",") < 0)
						err = ASR_FAILURE;
			}
			break;
		}
		case DATA_TYPE_INT64_ARRAY:
		{
			int64_t *val;
			(void) nvpair_value_int64_array(nvp, &val, &nelem);
			for (i = 0; i < nelem && err == 0; i++) {
				if (fprintf(fp, " %lld",
				    (longlong_t)val[i]) < 0)
					err = ASR_FAILURE;
				if (i < nelem - 1)
					if (fprintf(fp, ",") < 0)
						err = ASR_FAILURE;
			}
			break;
		}
		case DATA_TYPE_UINT64_ARRAY:
		{
			uint64_t *val;
			(void) nvpair_value_uint64_array(nvp, &val, &nelem);
			for (i = 0; i < nelem && err == 0; i++) {
				if (fprintf(fp, "0x%llx",
				    (u_longlong_t)val[i]) < 0)
					err = ASR_FAILURE;
				if (i < nelem - 1)
					if (fprintf(fp, ",") < 0)
						err = ASR_FAILURE;
			}
			break;
		}
		case DATA_TYPE_STRING_ARRAY:
		{
			char **val;
			(void) nvpair_value_string_array(nvp, &val, &nelem);
			for (i = 0; i < nelem && err == 0; i++) {
				if (EOF == fputc('"', fp)) {
					err = ASR_FAILURE;
					break;
				}
				if ((err = asr_nvl_print_prop_string(
				    fp, val[i])) != 0)
					break;
				if (EOF == fputc('"', fp)) {
					err = ASR_FAILURE;
					break;
				}
				if (i < nelem - 1)
					if (fprintf(fp, ",") < 0)
						err = ASR_FAILURE;
			}
			break;
		}
		case DATA_TYPE_HRTIME:
		{
			hrtime_t val;
			(void) nvpair_value_hrtime(nvp, &val);
			if (fprintf(fp, "0x%llx", val) < 0)
				err = ASR_FAILURE;
			break;
		}

		default:
			if (fprintf(fp, "unknown data type (%d)", type) < 0)
				err = ASR_FAILURE;
			break;
		}
		if (fprintf(fp, "\n") < 0)
			err = ASR_FAILURE;
	}
	asr_buf_free(buf);
	return (err);
} /* asr_nvl_print_prop_path */

/*
 * Prints elements in an event buffer in property format
 */
int
asr_nvl_print_properties(FILE *fp, nvlist_t *nvl)
{
	return (asr_nvl_print_prop_path(fp, nvl, NULL));
}

static char *
convert_nvpropstr(char *value)
{
	int i, j;
	int vlen = strlen(value);
	char *s;

	if ((s = malloc(vlen)) == NULL) {
		(void) asr_set_errno(EASR_NOMEM);
		return (NULL);
	}

	for (i = 0, j = 0; i < vlen; i++, j++) {
		char c = value[i];
		if (c == '\\') {
			c = value[++i];
			if (c == 'n') {
				c = '\n';
			} else if (c == 't') {
				c = '\t';
			}
		}
		s[j] = c;
	}
	s[j - 1] = '\0';

	return (s);
}

/*
 * Reads properties from a file into a nvlist.
 * This only supports string data types.
 */
int
asr_nvl_read_properties(FILE *ifp, nvlist_t *nvl)
{
	int err = 0;
	char buf[2048];
	int line;
	char *value;
	char *p, *s;

	for (line = 1;
	    fgets(buf, sizeof (buf), ifp) != NULL && err == 0;
	    line++) {
		if (buf[0] == '#')
			continue;
		value = NULL;
		for (p = buf; *p != '\0'; p++) {
			if (*p == '=') {
				*p = '\0';
				value = ++p;
				break;
			}
		}
		s = convert_nvpropstr(value);
		if (s != NULL) {
			err |= nvlist_add_string(nvl, buf, s);
			free(s);
		}
	}

	if (err != 0) {
		(void) asr_set_errno(EASR_NVLIST);
	}
	return (err);
}

static void
indent(FILE *fp, int depth)
{
	while (depth-- > 0)
		(void) fprintf(fp, "\t");
}

/*
 * Prints elements in an event buffer in property format
 */
static void
asr_nvl_print_jsoni(FILE *fp, nvlist_t *nvl, int depth, char quote, char *sep)
{
	int first = 1;
	int i;
	char *name;
	uint_t nelem;
	nvpair_t *nvp;

	if (nvl == NULL)
		return;

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {

		data_type_t type = nvpair_type(nvp);
		name = nvpair_name(nvp);

		if (first) {
			(void) fprintf(fp, "{");
			first = 0;
			if (depth > 1)
				(void) fputc('\n', fp);
		} else {
			(void) fprintf(fp, ",\n");
		}
		indent(fp, depth);

		(void) fputc(quote, fp);
		(void) asr_nvl_print_prop_string(fp, name);
		(void) fputc(quote, fp);
		(void) fputs(sep, fp);
		nelem = 0;

		switch (type) {
		case DATA_TYPE_BOOLEAN:
		{
			(void) fprintf(fp, "true");
			break;
		}
		case DATA_TYPE_BOOLEAN_VALUE:
		{
			boolean_t val;
			(void) nvpair_value_boolean_value(nvp, &val);
			(void) fprintf(fp, "%s", val ? "true" : "false");
			break;
		}
		case DATA_TYPE_BYTE:
		{
			uchar_t val;
			(void) nvpair_value_byte(nvp, &val);
			(void) fprintf(fp, "0x%2.2x", val);
			break;
		}
		case DATA_TYPE_INT8:
		{
			int8_t val;
			(void) nvpair_value_int8(nvp, &val);
			(void) fprintf(fp, "%d", val);
			break;
		}
		case DATA_TYPE_UINT8:
		{
			uint8_t val;
			(void) nvpair_value_uint8(nvp, &val);
			(void) fprintf(fp, "0x%x", val);
			break;
		}
		case DATA_TYPE_INT16:
		{
			int16_t val;
			(void) nvpair_value_int16(nvp, &val);
			(void) fprintf(fp, "%d", val);
			break;
		}
		case DATA_TYPE_UINT16:
		{
			uint16_t val;
			(void) nvpair_value_uint16(nvp, &val);
			(void) fprintf(fp, "0x%x", val);
			break;
		}
		case DATA_TYPE_INT32:
		{
			int32_t val;
			(void) nvpair_value_int32(nvp, &val);
			(void) fprintf(fp, "%d", val);
			break;
		}
		case DATA_TYPE_UINT32:
		{
			uint32_t val;
			(void) nvpair_value_uint32(nvp, &val);
			(void) fprintf(fp, "0x%x", val);
			break;
		}
		case DATA_TYPE_INT64:
		{
			int64_t val;
			(void) nvpair_value_int64(nvp, &val);
			(void) fprintf(fp, "%lld", (longlong_t)val);
			break;
		}
		case DATA_TYPE_UINT64:
		{
			uint64_t val;
			(void) nvpair_value_uint64(nvp, &val);
			(void) fprintf(fp, "0x%llx", (u_longlong_t)val);
			break;
		}
#ifdef DATA_TYPE_DOUBLE
		case DATA_TYPE_DOUBLE:
		{
			double val;
			(void) nvpair_value_double(nvp, &val);
			(void) fprintf(fp, "0x%llf", val);
			break;
		}
#endif

		case DATA_TYPE_STRING:
		{
			char *val;
			(void) nvpair_value_string(nvp, &val);
			(void) fputc(quote, fp);
			(void) asr_nvl_print_prop_string(fp, val);
			(void) fputc(quote, fp);
			break;
		}
		case DATA_TYPE_BOOLEAN_ARRAY:
		{
			boolean_t *val;
			(void) nvpair_value_boolean_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				(void) fprintf(fp, "%s", val[i] ?
				    "true" : "false");
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}
		case DATA_TYPE_BYTE_ARRAY:
		{
			uchar_t *val;
			(void) nvpair_value_byte_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				(void) fprintf(fp, "0x%2.2x", val[i]);
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}
		case DATA_TYPE_INT8_ARRAY:
		{
			int8_t *val;
			(void) nvpair_value_int8_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				(void) fprintf(fp, "%d", val[i]);
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}
		case DATA_TYPE_UINT8_ARRAY:
		{
			uint8_t *val;
			(void) nvpair_value_uint8_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				(void) fprintf(fp, "0x%x", val[i]);
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}
		case DATA_TYPE_INT16_ARRAY:
		{
			int16_t *val;
			(void) nvpair_value_int16_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				(void) fprintf(fp, "%d", val[i]);
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}
		case DATA_TYPE_UINT16_ARRAY:
		{
			uint16_t *val;
			(void) nvpair_value_uint16_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				(void) fprintf(fp, "0x%x", val[i]);
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}
		case DATA_TYPE_INT32_ARRAY:
		{
			int32_t *val;
			(void) nvpair_value_int32_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				(void) fprintf(fp, "%d", val[i]);
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}
		case DATA_TYPE_UINT32_ARRAY:
		{
			uint32_t *val;
			(void) nvpair_value_uint32_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				(void) fprintf(fp, "0x%x", val[i]);
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}
		case DATA_TYPE_INT64_ARRAY:
		{
			int64_t *val;
			(void) nvpair_value_int64_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				(void) fprintf(fp, " %lld", (longlong_t)val[i]);
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}
		case DATA_TYPE_UINT64_ARRAY:
		{
			uint64_t *val;
			(void) nvpair_value_uint64_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				(void) fprintf(fp, "0x%llx",
				    (u_longlong_t)val[i]);
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}
		case DATA_TYPE_STRING_ARRAY:
		{
			char **val;
			(void) nvpair_value_string_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				(void) fputc(quote, fp);
				(void) asr_nvl_print_prop_string(fp, val[i]);
				(void) fputc(quote, fp);
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}
		case DATA_TYPE_HRTIME:
		{
			hrtime_t val;
			(void) nvpair_value_hrtime(nvp, &val);
			(void) fprintf(fp, "0x%llx", val);
			break;
		}
		case DATA_TYPE_NVLIST:
		{
			nvlist_t *val;
			(void) nvpair_value_nvlist(nvp, &val);
			asr_nvl_print_jsoni(fp, val, depth + 1, quote, sep);
			break;
		}
		case DATA_TYPE_NVLIST_ARRAY:
		{
			nvlist_t **val;
			(void) nvpair_value_nvlist_array(nvp, &val, &nelem);
			(void) fputc('[', fp);
			for (i = 0; i < nelem; i++) {
				asr_nvl_print_jsoni(
				    fp, val[i], depth + 1, quote, sep);
				if (i < nelem - 1)
					(void) fprintf(fp, ",");
			}
			(void) fputc(']', fp);
			break;
		}

		default:
			(void) fprintf(fp, "unknown data type (%d)", type);
			break;
		}
	}
	(void) fputc('\n', fp);
	indent(fp, depth - 1);
	(void) fprintf(fp, "}");
}

void
asr_nvl_print_json(FILE *fp, nvlist_t *nvl)
{
	asr_nvl_print_jsoni(fp, nvl, 1, '"', " : ");
	(void) fputc('\n', fp);
}

void
asr_nvl_print_perl(FILE *fp, nvlist_t *nvl)
{
	asr_nvl_print_jsoni(fp, nvl, 1, '\'', " => ");
	(void) fputc('\n', fp);
}


static int
indentb(asr_buf_t *out, int depth)
{
	int err = 0;
	while (depth-- > 0)
		err |= asr_buf_append_str(out, "\t");
	return (err);
}

static int
asr_nvl_print_prop_stringb(asr_buf_t *out, char *str)
{
	int i;
	int err = 0;
	char c;
	if (str != NULL) {
		for (i = 0, c = str[0]; c != '\0' && err == 0; c = str[++i]) {
			if (c == '\n') {
				err |= asr_buf_append_str(out, "\\n");
			} else if (c == '\t') {
				err |= asr_buf_append_str(out, "\\t");
			} else if (c == '\\') {
				err |= asr_buf_append_str(out, "\\\\");
			} else if (c == '"') {
				err |= asr_buf_append_char(out, '\\');
				err |= asr_buf_append_char(out, '"');
			} else if (c == '\'') {
				err |= asr_buf_append_char(out, '\\');
				err |= asr_buf_append_char(out, '\'');
			} else {
				err |= asr_buf_append_char(out, c);
			}
		}
	}
	return (err);
}

/*
 * Prints elements in an event buffer in property format
 */
void
asr_nvl_tostringi(asr_buf_t *out, nvlist_t *nvl,
    int depth, char quote, char *sep)
{
	int first = 1;
	int i;
	char *name;
	uint_t nelem;
	nvpair_t *nvp;

	if (nvl == NULL)
		return;

	for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(nvl, nvp)) {

		data_type_t type = nvpair_type(nvp);
		name = nvpair_name(nvp);

		if (first) {
			(void) asr_buf_append_char(out, '{');
			first = 0;
			if (depth > 1)
				(void) asr_buf_append_char(out, '\n');
		} else {
			(void) asr_buf_append_str(out, ",\n");
		}
		(void) indentb(out, depth);

		(void) asr_buf_append_char(out, quote);
		(void) asr_nvl_print_prop_stringb(out, name);
		(void) asr_buf_append_char(out, quote);
		(void) asr_buf_append_str(out, sep);
		nelem = 0;

		switch (type) {
		case DATA_TYPE_BOOLEAN:
		{
			(void) asr_buf_append_str(out, "true");
			break;
		}
		case DATA_TYPE_BOOLEAN_VALUE:
		{
			boolean_t val;
			(void) nvpair_value_boolean_value(nvp, &val);
			(void) asr_buf_append_str(out, val ? "true" : "false");
			break;
		}
		case DATA_TYPE_BYTE:
		{
			uchar_t val;
			(void) nvpair_value_byte(nvp, &val);
			(void) asr_buf_append(out, "0x%2.2x", val);
			break;
		}
		case DATA_TYPE_INT8:
		{
			int8_t val;
			(void) nvpair_value_int8(nvp, &val);
			(void) asr_buf_append(out,  "%d", val);
			break;
		}
		case DATA_TYPE_UINT8:
		{
			uint8_t val;
			(void) nvpair_value_uint8(nvp, &val);
			(void) asr_buf_append(out, "0x%x", val);
			break;
		}
		case DATA_TYPE_INT16:
		{
			int16_t val;
			(void) nvpair_value_int16(nvp, &val);
			(void) asr_buf_append(out, "%d", val);
			break;
		}
		case DATA_TYPE_UINT16:
		{
			uint16_t val;
			(void) nvpair_value_uint16(nvp, &val);
			(void) asr_buf_append(out, "0x%x", val);
			break;
		}
		case DATA_TYPE_INT32:
		{
			int32_t val;
			(void) nvpair_value_int32(nvp, &val);
			(void) asr_buf_append(out, "%d", val);
			break;
		}
		case DATA_TYPE_UINT32:
		{
			uint32_t val;
			(void) nvpair_value_uint32(nvp, &val);
			(void) asr_buf_append(out, "0x%x", val);
			break;
		}
		case DATA_TYPE_INT64:
		{
			int64_t val;
			(void) nvpair_value_int64(nvp, &val);
			(void) asr_buf_append(out, "%lld", (longlong_t)val);
			break;
		}
		case DATA_TYPE_UINT64:
		{
			uint64_t val;
			(void) nvpair_value_uint64(nvp, &val);
			(void) asr_buf_append(out, "0x%llx", (u_longlong_t)val);
			break;
		}
#ifdef DATA_TYPE_DOUBLE
		case DATA_TYPE_DOUBLE:
		{
			double val;
			(void) nvpair_value_double(nvp, &val);
			(void) asr_buf_append(out, "0x%llf", val);
			break;
		}
#endif

		case DATA_TYPE_STRING:
		{
			char *val;
			(void) nvpair_value_string(nvp, &val);
			(void) asr_buf_append_char(out, quote);
			(void) asr_nvl_print_prop_stringb(out, val);
			(void) asr_buf_append_char(out, quote);
			break;
		}
		case DATA_TYPE_BOOLEAN_ARRAY:
		{
			boolean_t *val;
			(void) nvpair_value_boolean_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				(void) asr_buf_append(out, "%s", val[i] ?
				    "true" : "false");
				if (i < nelem - 1)
					(void) asr_buf_append(out, ",");
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}
		case DATA_TYPE_BYTE_ARRAY:
		{
			uchar_t *val;
			(void) nvpair_value_byte_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				(void) asr_buf_append(out, "0x%2.2x", val[i]);
				if (i < nelem - 1)
					(void) asr_buf_append_char(out, ',');
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}
		case DATA_TYPE_INT8_ARRAY:
		{
			int8_t *val;
			(void) nvpair_value_int8_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				(void) asr_buf_append(out, "%d", val[i]);
				if (i < nelem - 1)
					(void) asr_buf_append_char(out, ',');
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}
		case DATA_TYPE_UINT8_ARRAY:
		{
			uint8_t *val;
			(void) nvpair_value_uint8_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				(void) asr_buf_append(out, "0x%x", val[i]);
				if (i < nelem - 1)
					(void) asr_buf_append_char(out, ',');
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}
		case DATA_TYPE_INT16_ARRAY:
		{
			int16_t *val;
			(void) nvpair_value_int16_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				(void) asr_buf_append(out, "%d", val[i]);
				if (i < nelem - 1)
					(void) asr_buf_append_char(out, ',');
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}
		case DATA_TYPE_UINT16_ARRAY:
		{
			uint16_t *val;
			(void) nvpair_value_uint16_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				(void) asr_buf_append(out, "0x%x", val[i]);
				if (i < nelem - 1)
					(void) asr_buf_append_char(out, ',');
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}
		case DATA_TYPE_INT32_ARRAY:
		{
			int32_t *val;
			(void) nvpair_value_int32_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				(void) asr_buf_append(out, "%d", val[i]);
				if (i < nelem - 1)
					(void) asr_buf_append_char(out, ',');
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}
		case DATA_TYPE_UINT32_ARRAY:
		{
			uint32_t *val;
			(void) nvpair_value_uint32_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				(void) asr_buf_append(out, "0x%x", val[i]);
				if (i < nelem - 1)
					(void) asr_buf_append_char(out, ',');
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}
		case DATA_TYPE_INT64_ARRAY:
		{
			int64_t *val;
			(void) nvpair_value_int64_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				(void) asr_buf_append(out, " %lld",
				    (longlong_t)val[i]);
				if (i < nelem - 1)
					(void) asr_buf_append_char(out, ',');
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}
		case DATA_TYPE_UINT64_ARRAY:
		{
			uint64_t *val;
			(void) nvpair_value_uint64_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				(void) asr_buf_append(out, "0x%llx",
				    (u_longlong_t)val[i]);
				if (i < nelem - 1)
					(void) asr_buf_append_char(out, ',');
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}
		case DATA_TYPE_STRING_ARRAY:
		{
			char **val;
			(void) nvpair_value_string_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				(void) asr_buf_append_char(out, quote);
				(void) asr_nvl_print_prop_stringb(out, val[i]);
				(void) asr_buf_append_char(out, quote);
				if (i < nelem - 1)
					(void) asr_buf_append_char(out, ',');
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}
		case DATA_TYPE_HRTIME:
		{
			hrtime_t val;
			(void) nvpair_value_hrtime(nvp, &val);
			(void) asr_buf_append(out, "0x%llx", val);
			break;
		}
		case DATA_TYPE_NVLIST:
		{
			nvlist_t *val;
			(void) nvpair_value_nvlist(nvp, &val);
			asr_nvl_tostringi(out, val, depth + 1, quote, sep);
			break;
		}
		case DATA_TYPE_NVLIST_ARRAY:
		{
			nvlist_t **val;
			(void) nvpair_value_nvlist_array(nvp, &val, &nelem);
			(void) asr_buf_append_char(out, '[');
			for (i = 0; i < nelem; i++) {
				asr_nvl_tostringi(
				    out, val[i], depth + 1, quote, sep);
				if (i < nelem - 1)
					(void) asr_buf_append_char(out, ',');
			}
			(void) asr_buf_append_char(out, ']');
			break;
		}

		default:
			(void) asr_buf_append(out,
			    "unknown data type (%d)", type);
			break;
		}
	}
	(void) asr_buf_append_char(out, '\n');
	(void) indentb(out, depth - 1);
	(void) asr_buf_append_char(out, '}');
	(void) asr_buf_terminate(out);
}

asr_buf_t *
asr_nvl_toperl(nvlist_t *nvl)
{
	asr_buf_t *out = asr_buf_alloc(1024);
	if (out != NULL)
		asr_nvl_tostringi(out, nvl, 1, '\'', " => ");
	return (out);
}

asr_buf_t *
asr_nvl_tostring(nvlist_t *nvl)
{
	asr_buf_t *out = asr_buf_alloc(1024);
	if (out != NULL)
		asr_nvl_tostringi(out, nvl, 1, '"', " : ");
	return (out);
}
