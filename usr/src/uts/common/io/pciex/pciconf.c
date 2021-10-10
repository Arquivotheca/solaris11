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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/inttypes.h>
#include <sys/stdbool.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/disp.h>
#include <sys/conf.h>
#include <sys/bootconf.h>
#include <sys/sysconf.h>
#include <sys/promif.h>
#include <sys/sunddi.h>
#include <sys/esunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/kmem.h>
#include <sys/vmem.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/hwconf.h>
#include <sys/modctl.h>
#include <sys/cmn_err.h>
#include <sys/kobj.h>
#include <sys/kobj_lex.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/autoconf.h>
#include <sys/callb.h>
#include <sys/sysmacros.h>
#include <sys/dacf.h>
#include <vm/seg_kmem.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/pathname.h>
#include <sys/ndi_impldefs.h>
#include <sys/pciconf.h>
#include <sys/pci_cap.h>
#include <sys/pci_impl.h>
#include <sys/iovcfg.h>
#include <sys/pcie_impl.h>
#include <sys/pci_cfgacc.h>
#include <sys/pci_param.h>
#include <sys/iov_param.h>

extern	int override_plat_config;

char	*reg_name[] = {"REG_BAR", "REG_CONFIG"};
static char tok_err[] = "Unexpected token '%s'";
static char	*config_section_name[] = {
"SYSTEM", "DEVICE", "IOVCFG", "INVALID"};

static char	*operator_name[] = {
"EQUAL", "OR", "AND", "INVALID", "EXCLUSIVE_OR", "INVALID"};

static char
    compatible_string[NUM_OF_COMPATIBLE_OPERATORS][COMPATIBLE_NAME_LEN + 1];

static char *id_name[] = {
"vid",
"did",
"rev_id",
"sub_vid",
"sub_sid",
""};

#define	isequals(ch)	((ch) == '=')
#define	isquotes(ch)	((ch) == '"')
static char pathname_overflow_err[] = "device pathname too long";
static char value_string_overflow_err[] = "value string too long";
static char missing_pathname_err[] = "pathname missing";
static char missing_equals_err[] = "assignment missing";
struct pciconf_data pciconf_data_hd = {NULL};

static ddi_device_acc_attr_t	reg_acc_attr = {
	DDI_DEVICE_ATTR_V1,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

#ifdef DEBUG

int	pciconf_parse_debug = 0;
int	pciconf_debug = 0;

/*VARARGS1*/
void
parse_dbg(struct _buf *file, char *fmt, ...)
{
	va_list ap;
	char    buf[512];
	char    *bufp = buf;

	va_start(ap, fmt);
	if (file) {
		kobj_file_err(CE_CONT, file, fmt, ap);
		cmn_err(CE_CONT, "\n");
	} else {
		(void) vsprintf(bufp, fmt, ap);
		cmn_err(CE_CONT, "%s\n", buf);
	}
	va_end(ap);
}

void
pciconf_dbg(char *fmt, ...)
{
	char    buf[512];
	va_list ap;
	char    *bufp = buf;

	va_start(ap, fmt);
	(void) vsprintf(bufp, fmt, ap);
	cmn_err(CE_CONT, "%s\n", buf);
	va_end(ap);
}
#endif /* DEBUG */

token_t
pci_lex(struct _buf *file, char *val, size_t size)
{
	char	*cp;
	int	ch, oval, badquote;
	size_t	remain;
	token_t token = UNEXPECTED;

	if (size < 2)
		return (token);	/* this token is UNEXPECTED */

	cp = val;
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;

	remain = size - 1;
	*cp++ = (char)ch;
	switch (ch) {
	case '=':
		token = EQUALS;
		break;
	case '&':
		token = AMPERSAND;
		break;
	case '|':
		token = BIT_OR;
		break;
	case '*':
		token = STAR;
		break;
	case '#':
		token = POUND;
		break;
	case ':':
		token = COLON;
		break;
	case ';':
		token = SEMICOLON;
		break;
	case ',':
		token = COMMA;
		break;
	case '/':
		token = SLASH;
		break;
	case '[':
		token = LEFT_SQUARE_BRACKET;
		break;
	case ']':
		token = RIGHT_SQUARE_BRACKET;
		break;
	case ' ':
	case '\t':
	case '\f':
		while ((ch = kobj_getc(file)) == ' ' ||
		    ch == '\t' || ch == '\f') {
			if (--remain == 0) {
				token = UNEXPECTED;
				goto out;
			}
			*cp++ = (char)ch;
		}
		(void) kobj_ungetc(file);
		token = WHITE_SPACE;
		break;
	case '\n':
	case '\r':
		token = NEWLINE;
		break;
	case '"':
		remain++;
		cp--;
		badquote = 0;
		while (!badquote && (ch  = kobj_getc(file)) != '"') {
			switch (ch) {
			case '\n':
			case -1:
				kobj_file_err(CE_WARN, file, "Missing \"");
				remain = size - 1;
				cp = val;
				*cp++ = '\n';
				badquote = 1;
				/* since we consumed the newline/EOF */
				(void) kobj_ungetc(file);
				break;

			case '\\':
				if (--remain == 0) {
					token = UNEXPECTED;
					goto out;
				}
				ch = (char)kobj_getc(file);
				if (!isdigit(ch)) {
					/* escape the character */
					*cp++ = (char)ch;
					break;
				}
				oval = 0;
				while (ch >= '0' && ch <= '7') {
					ch -= '0';
					oval = (oval << 3) + ch;
					ch = (char)kobj_getc(file);
				}
				(void) kobj_ungetc(file);
				/* check for character overflow? */
				if (oval > 127) {
					cmn_err(CE_WARN,
					    "Character "
					    "overflow detected.");
				}
				*cp++ = (char)oval;
				break;
			default:
				if (--remain == 0) {
					token = UNEXPECTED;
					goto out;
				}
				*cp++ = (char)ch;
				break;
			}
		}
		token = STRING;
		break;

	case -1:
		token = EOF;
		break;

	default:
		/*
		 * detect a lone '-' (including at the end of a line), and
		 * identify it as a 'name'
		 */
		if (ch == '-') {
			if (--remain == 0) {
				token = UNEXPECTED;
				goto out;
			}
			*cp++ = (char)(ch = kobj_getc(file));
			if (iswhite(ch) || (ch == '\n')) {
				(void) kobj_ungetc(file);
				remain++;
				cp--;
				token = NAME;
				break;
			}
		} else if (isunary(ch)) {
			if (--remain == 0) {
				token = UNEXPECTED;
				goto out;
			}
			*cp++ = (char)(ch = kobj_getc(file));
		}


		if (isdigit(ch)) {
			if (ch == '0') {
				if ((ch = kobj_getc(file)) == 'x') {
					if (--remain == 0) {
						token = UNEXPECTED;
						goto out;
					}
					*cp++ = (char)ch;
					ch = kobj_getc(file);
					while (isxdigit(ch)) {
						if (--remain == 0) {
							token = UNEXPECTED;
							goto out;
						}
						*cp++ = (char)ch;
						ch = kobj_getc(file);
					}
					(void) kobj_ungetc(file);
					token = HEXVAL;
				} else {
					goto digit;
				}
			} else {
				ch = kobj_getc(file);
digit:
				while (isdigit(ch)) {
					if (--remain == 0) {
						token = UNEXPECTED;
						goto out;
					}
					*cp++ = (char)ch;
					ch = kobj_getc(file);
				}
				(void) kobj_ungetc(file);
				token = DECVAL;
			}
		} else if (isalpha(ch) || ch == '\\' || ch == '_') {
			if (ch != '\\') {
				ch = kobj_getc(file);
			} else {
				/*
				 * if the character was a backslash,
				 * back up so we can overwrite it with
				 * the next (i.e. escaped) character.
				 */
				remain++;
				cp--;
			}
			while (isnamechar(ch) || ch == '\\') {
				if (ch == '\\')
					ch = kobj_getc(file);
				if (--remain == 0) {
					token = UNEXPECTED;
					goto out;
				}
				*cp++ = (char)ch;
				ch = kobj_getc(file);
			}
			(void) kobj_ungetc(file);
			token = NAME;
		} else {
			token = UNEXPECTED;
		}
		break;
	}
out:
	*cp = '\0';

	return (token);
}

int
atoi(char *p)
{
	int i = 0;

	while (*p != '\0') {
		if ((*p < '0') || (*p > '9'))
			break;
		i = 10 * i + (*p++ - '0');
	}

	return (i);
}

static int
parse_pathname(struct _buf *file, char *pathname, int pathname_len)
{
	char		*cp;
	int		ch, ret;

	ret = 0;
	cp = pathname;
	while ((ch = kobj_getc(file)) != -1 && !iswhite(ch) &&
	    !isequals(ch) && !isnewline(ch) && (ch != '[') && (ch != ']')) {
		if (((uintptr_t)cp - (uintptr_t)pathname) >=
		    (pathname_len - 1)) {
			kobj_file_err(CE_WARN, file,
			    pathname_overflow_err);
			kobj_find_eol(file);
			ret = 1;
			break;
		}
		*cp++ = (char)ch;
	}
	if (ch != -1)
		(void) kobj_ungetc(file);
	if (ret == 0)
		*cp = '\0';
	return (ret);
}

static int
missing_terminators(struct _buf *file)
{
	int	ch, missing_terminators;

	missing_terminators = 1;
	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	/* check for filter terminators ']]' */
	if (ch == ']') {
		ch = kobj_getc(file);
		if (ch == ']') {
			missing_terminators = 0;
			/* skip white chars */
			while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
				;
			if (ch != '\n') {
				kobj_file_err(CE_WARN, file,
				    "Invalid char %c found after filter"
				    " terminators", (char)ch);
			}
		}
	}
	if (missing_terminators) {
		kobj_file_err(CE_WARN, file,
		    "missing filter terminators ']]'");
	}
	if (ch == '\n') {
		kobj_newline(file);
	} else {
		if (ch != -1)
			kobj_find_eol(file);
	}
	return (missing_terminators);
}

void
clean_pciconf_data()
{
	struct config_data *config_datap, *next_config_datap;
	struct filter *filterp, *next_filterp;
	struct action *actionp, *next_actionp;
	nvlist_t	*nvlp;

	config_datap = pciconf_data_hd.config_datap[SYSTEM_CONFIG];
	while (config_datap) {
		next_config_datap = config_datap->next;
		filterp = config_datap->filterp;
		actionp = (struct action *)config_datap->datap;
		while (filterp) {
			next_filterp = filterp->next;
			kmem_free(filterp, sizeof (struct filter));
			filterp = next_filterp;
		}
		while (actionp) {
			next_actionp = actionp->next;
			kmem_free(actionp, sizeof (struct action));
			actionp = next_actionp;
		}
		kmem_free(config_datap, sizeof (struct config_data));
		config_datap = next_config_datap;
	}
	config_datap = pciconf_data_hd.config_datap[DEVICE_CONFIG];
	while (config_datap) {
		next_config_datap = config_datap->next;
		filterp = config_datap->filterp;
		nvlp = (nvlist_t *)config_datap->datap;
		while (filterp) {
			next_filterp = filterp->next;
			kmem_free(filterp, sizeof (struct filter));
			filterp = next_filterp;
		}
		if (nvlp)
			nvlist_free(nvlp);
		kmem_free(config_datap, sizeof (struct config_data));
		config_datap = next_config_datap;
	}
	pciconf_data_hd.config_datap[SYSTEM_CONFIG] = NULL;
	pciconf_data_hd.config_datap[DEVICE_CONFIG] = NULL;
}

void
dump_nvlist(nvlist_t *nvl, int depth)
{
	nvpair_t	*nvp;
	data_type_t	nvp_type;
	uint32_t	nvp_value32;
	uint64_t	nvp_value64;
	char		*value_string;
	nvlist_t	*value_nvl;
	char		*nvp_name;
	int		i, err;
	char		prefix_string[40];
	nvlist_t	**nvl_array;
	char		**string_array;
	uint_t		nelem;

	if (nvlist_empty(nvl))
		return;
	i = depth;
	prefix_string[0] = '\0';
	while (i--)
		(void) sprintf(prefix_string, "%s\t", prefix_string);
	nvp = NULL;
	while (nvp = nvlist_next_nvpair(nvl, nvp)) {
		nvp_type = nvpair_type(nvp);
		nvp_name = nvpair_name(nvp);
		switch (nvp_type) {
		case DATA_TYPE_UINT64:
			err = nvpair_value_uint64(nvp,
			    &nvp_value64);
			if (!err)
				cmn_err(CE_NOTE, "%s%s = 0x%lux \n",
				    prefix_string, nvp_name,
				    (unsigned long)nvp_value64);
			break;
		case DATA_TYPE_UINT32:
			err = nvpair_value_uint32(nvp,
			    &nvp_value32);
			if (!err)
				cmn_err(CE_NOTE, "%s%s = 0x%x \n",
				    prefix_string, nvp_name, nvp_value32);
			break;
		case DATA_TYPE_INT32:
			err = nvpair_value_int32(nvp,
			    (int32_t *)&nvp_value32);
			if (!err)
				cmn_err(CE_NOTE, "%s%s = 0x%x \n",
				    prefix_string, nvp_name, nvp_value32);
			break;
		case DATA_TYPE_STRING:
			err = nvpair_value_string(nvp,
			    &value_string);
			if (!err)
				cmn_err(CE_NOTE, "%s%s = %s \n",
				    prefix_string, nvp_name, value_string);
			break;
		case DATA_TYPE_NVLIST:
			err = nvpair_value_nvlist(nvp,
			    &value_nvl);
			if (err)
				break;
			cmn_err(CE_NOTE, "%s%s is a nvlist:\n",
			    prefix_string, nvp_name);
			dump_nvlist(value_nvl, depth + 1);
			break;
		case DATA_TYPE_NVLIST_ARRAY:
			err = nvpair_value_nvlist_array(nvp,
			    &nvl_array, &nelem);
			if (err)
				break;
			cmn_err(CE_NOTE,
			    "%s%s is a nvlist_array of %d items:\n",
			    prefix_string, nvp_name, nelem);
			for (i = 0; i < nelem; i++) {
				cmn_err(CE_NOTE, "%s\t item %d:\n",
				    prefix_string, i + 1);
				dump_nvlist(nvl_array[i], depth + 1);
			}
			break;
		case DATA_TYPE_STRING_ARRAY:
			err = nvpair_value_string_array(nvp,
			    &string_array, &nelem);
			if (err)
				break;
			cmn_err(CE_NOTE,
			    "%s%s is a string_array of %d items:\n",
			    prefix_string, nvp_name, nelem);
			for (i = 0; i < nelem; i++) {
				cmn_err(CE_NOTE, "%s\t item %d:%s\n",
				    prefix_string, i + 1, string_array[i]);
			}
			break;
		default:
			cmn_err(CE_NOTE,
			    "Unsupported type %d found\n",
			    nvp_type);
		}
	}
}

static void
dump_pciconf_data()
{
	struct filter *filterp;
	struct action *actionp;
	struct config_data *config_datap;
	nvlist_t	*nvl;
	int		i;
	char		filter_names[256];

	cmn_err(CE_NOTE, "dumping system config section...\n");
	for (config_datap = pciconf_data_hd.
	    config_datap[SYSTEM_CONFIG]; config_datap;
	    config_datap = config_datap->next) {
		cmn_err(CE_NOTE, "label: %s\n",
		    config_datap->label);
		(void) snprintf(filter_names, sizeof (filter_names),
		    "filter names(s):");
		for (i = 0, filterp = config_datap->filterp; filterp;
		    filterp = filterp->next, i++) {
			if (i)
				(void) snprintf(filter_names,
				    sizeof (filter_names),
				    "%s AND", filter_names);
			(void) snprintf(filter_names, sizeof (filter_names),
			    "%s %s", filter_names, filterp->name);
		}
		cmn_err(CE_NOTE, "%s\n", filter_names);
		for (filterp = config_datap->filterp; filterp;
		    filterp = filterp->next) {
			if (strncmp("path", filterp->name, FILTER_NAME_LEN)
			    == 0) {
				cmn_err(CE_NOTE, "pathname: %s\n",
				    filterp->filter_info.path);
			}
			if (strncmp("classcode", filterp->name, FILTER_NAME_LEN)
			    == 0) {
				cmn_err(CE_NOTE, "classcode = 0x%x\n",
				    filterp->filter_info.classcode_info.
				    classcode);
				if (filterp->filter_info.classcode_info.
				    mask_defined) {
					cmn_err(CE_NOTE, "mask = 0x%x\n",
					    filterp->filter_info.classcode_info.
					    mask);
				}
			}
			if (strncmp("id", filterp->name, FILTER_NAME_LEN)
			    == 0) {
				for (i = 0; i < MAX_IDS_IN_ID_FILTER; i++) {
					if (filterp->filter_info.id_info.
					    id_defined[i]) {
						cmn_err(CE_NOTE,
						    "\t%s = 0x%x\n",
						    id_name[i],
						    filterp->filter_info.
						    id_info.id_val[i]);
					} else {
						cmn_err(CE_NOTE,
						    "\t%s undefined\n",
						    id_name[i]);
					}

				}
			}
		}
		for (actionp = (struct action *)config_datap->datap;
		    actionp; actionp = actionp->next) {
			if (strncmp("num-vf", actionp->name, ACTION_NAME_LEN)
			    == 0) {
				cmn_err(CE_NOTE, "num-vf = %d\n",
				    actionp->action_info.num_vf);
			}
			if (strncmp("reg", actionp->name, ACTION_NAME_LEN)
			    == 0) {
				cmn_err(CE_NOTE,
"%s =0x%x reg_offset = 0x%x reg_size = 0x%x reg_value = 0x%llx"
" operator is %s\n",
				    reg_name[actionp->action_info.reg_info.
				    reg_type],
				    actionp->action_info.reg_info.CONFIG_ID,
				    actionp->action_info.reg_info.REG_OFFSET,
				    actionp->action_info.reg_info.REG_SIZE,
				    actionp->action_info.reg_info.value,
				    operator_name[actionp->action_info.reg_info.
				    assign_operator]);
			}
			if (strncmp("compatible", actionp->name,
			    ACTION_NAME_LEN) == 0) {
				cmn_err(CE_NOTE,
				    "string_0: %s\tstring_1: %s\tstring_2:%s\n",
				    actionp->action_info.compatible_info.
				    compatible_string[0],
				    actionp->action_info.compatible_info.
				    compatible_string[1],
				    actionp->action_info.compatible_info.
				    compatible_string[2]);
			}
		}
	}
	cmn_err(CE_NOTE, "dumping device config section...\n");
	for (config_datap = pciconf_data_hd.
	    config_datap[DEVICE_CONFIG]; config_datap;
	    config_datap = config_datap->next) {
		cmn_err(CE_NOTE, "label: %s\n",
		    config_datap->label);
		nvl = (nvlist_t *)config_datap->datap;
		(void) snprintf(filter_names, sizeof (filter_names),
		    "filter names(s):");
		for (i = 0, filterp = config_datap->filterp; filterp;
		    filterp = filterp->next, i++) {
			if (i)
				(void) snprintf(filter_names,
				    sizeof (filter_names),
				    "%s AND", filter_names);
			(void) snprintf(filter_names, sizeof (filter_names),
			    "%s %s", filter_names, filterp->name);
		}
		cmn_err(CE_NOTE, "%s\n", filter_names);
		for (filterp = config_datap->filterp; filterp;
		    filterp = filterp->next) {
			if (strncmp("path", filterp->name, FILTER_NAME_LEN)
			    == 0) {
				cmn_err(CE_NOTE, "pathname: %s\n",
				    filterp->filter_info.path);
			}
			if (strncmp("classcode", filterp->name, FILTER_NAME_LEN)
			    == 0) {
				cmn_err(CE_NOTE, "classcode = 0x%x\n",
				    filterp->filter_info.classcode_info.
				    classcode);
				if (filterp->filter_info.classcode_info.
				    mask_defined) {
					cmn_err(CE_NOTE, "mask = 0x%x\n",
					    filterp->filter_info.classcode_info.
					    mask);
				}
			}
			if (strncmp("id", filterp->name, FILTER_NAME_LEN)
			    == 0) {
				for (i = 0; i < MAX_IDS_IN_ID_FILTER; i++) {
					if (filterp->filter_info.id_info.
					    id_defined[i]) {
						cmn_err(CE_NOTE,
						    "\t%s = 0x%x\n",
						    id_name[i],
						    filterp->filter_info.
						    id_info.id_val[i]);
					} else {
						cmn_err(CE_NOTE,
						    "\t%s undefined\n",
						    id_name[i]);
					}

				}
			}
		}	/* end of filterp */
		if (nvl) {
			dump_nvlist(nvl, 0);
		}
	}	/* end of for loop */
}

typedef enum {
	expecting_classcode = (EXPECT | 0),
	found_classcode = (FOUND | 0),
	expecting_mask = (EXPECT | 1),
	found_mask = (FOUND | 1),
} classcode_state_t;

static u_longlong_t id_ival[MAX_IDS_IN_ID_FILTER];
static boolean_t id_defined[MAX_IDS_IN_ID_FILTER];
static u_longlong_t reg_ival[NUM_REGD];

static struct filter *
id_filter_parse_func(void *arg)
{
	struct	filter	*filterp = NULL;
	token_t 	token;
	char		*tokval;
	int		i, id_index;
	int		state = (EXPECT | 0);
	struct	_buf	*file = (struct _buf *)arg;

	for (i = 0; i < MAX_IDS_IN_ID_FILTER; i++) {
		id_defined[i] = B_FALSE;
	}
	tokval = kmem_alloc(MAX_HWC_LINESIZE, KM_SLEEP);
	/* CONSTANTCONDITION */
	while (true) {
		id_index = (state & ID_INDEX_MASK);
		if (state == FOUND_RIGHT_SQUARE_BRACKET)
			break;
		if ((state & FOUND) &&
		    (id_index) >= (MAX_IDS_IN_ID_FILTER -1)) {
			break;
		}
		token = pci_lex(file, tokval, MAX_HWC_LINESIZE);
		if (token == EOF) {
			kobj_file_err(CE_WARN, file, "Incomplete filter found");
			goto exit;
		}
		switch (token) {
		case NEWLINE:
			kobj_file_err(CE_WARN, file, "Incomplete filter found");
			kobj_newline(file);
			goto exit;
		case RIGHT_SQUARE_BRACKET:
			/* possibly end of filter */
			if (state & FOUND) {
				(void) kobj_ungetc(file);
				state = FOUND_RIGHT_SQUARE_BRACKET;
				break;
			}
			goto default_case;
		case HEXVAL:
		case DECVAL:
			(void) kobj_getvalue(tokval,
			    &id_ival[state & ID_INDEX_MASK]);
			if (state & EXPECT) {
				id_defined[id_index] = B_TRUE;
				state = (id_index | FOUND);
			} else {
				kobj_file_err(CE_WARN, file,
				"Missing comma ");
				kobj_find_eol(file);
				goto exit;
			}
			break;
		case COMMA:
			if (state & FOUND) {
				state = ((id_index + 1) | EXPECT);
				break;
			}
			/*
			 * skip to next state if we are not at the last id.
			 */
			if (id_index != (MAX_IDS_IN_ID_FILTER - 1)) {
				id_defined[id_index] = B_FALSE;
				state = ((id_index + 1) | EXPECT);
				break;
			}
			/* fallthrough to default */
		default:
default_case:
			if (state & FOUND) {
				kobj_file_err(CE_WARN, file,
			"Expecting  comma or filter terminator but found %s ",
				    tokval);
				kobj_find_eol(file);
				goto exit;
			} else {
				kobj_file_err(CE_WARN, file,
			"Expecting  a value in hex or decimal but found %s ",
				    tokval);
				kobj_find_eol(file);
				goto exit;
			}
		}
	}	/* end of while */
	if (!missing_terminators(file)) {
		/* process the id info */
		filterp = kmem_zalloc(
		    sizeof (struct filter), KM_SLEEP);
		(void) strncpy(filterp->name, "id", FILTER_NAME_LEN);
		for (i = 0; i < MAX_IDS_IN_ID_FILTER; i++) {
			filterp->filter_info.id_info.id_val[i] =
			    (uint16_t)id_ival[i];
			filterp->filter_info.id_info.id_defined[i] =
			    (uint16_t)id_defined[i];
		}
	}
exit:
	kmem_free(tokval, MAX_HWC_LINESIZE);
	return (filterp);
}


static struct filter *
classcode_filter_parse_func(void *arg)
{
	uint16_t	classcode;
	uint16_t	mask;
	struct	filter	*filterp = NULL;
	token_t 	token;
	char		*tokval;
	u_longlong_t	ival;
	classcode_state_t	state = expecting_classcode;
	boolean_t	mask_defined = B_FALSE;
	struct	_buf	*file = (struct _buf *)arg;
	int		stay_in_while = 1;

	tokval = kmem_alloc(MAX_HWC_LINESIZE, KM_SLEEP);
	while (stay_in_while) {
		token = pci_lex(file, tokval, MAX_HWC_LINESIZE);
		if (token == EOF) {
			kobj_file_err(CE_WARN, file, "Incomplete filter found");
			goto exit;
		}
		switch (token) {
		case NEWLINE:
			kobj_file_err(CE_WARN, file, "Incomplete filter found");
			kobj_newline(file);
			goto exit;
		case RIGHT_SQUARE_BRACKET:
			/* possibly end of filter */
			if ((state == expecting_classcode) ||
			    (state == expecting_mask)) {
				goto not_a_digit;
			}
			(void) kobj_ungetc(file);
			stay_in_while = 0;
			break;
		case HEXVAL:
		case DECVAL:
			(void) kobj_getvalue(tokval, &ival);
			if (state == expecting_classcode) {
				classcode = (uint16_t)ival;
				state = found_classcode;
			} else if (state == expecting_mask) {
				mask = (uint16_t)ival;
				mask_defined = B_TRUE;
				state = found_mask;
				stay_in_while = 0;
			} else {
				kobj_file_err(CE_WARN, file,
				"Missing comma ");
				kobj_find_eol(file);
				goto exit;
			}
			break;
		case COMMA:
			if (state == found_classcode) {
				state = expecting_mask;
				break;
			}
			/* fallthrough to default */
		default:
			if (state == found_classcode) {
				kobj_file_err(CE_WARN, file,
				    "Expecting  comma but found %s ", tokval);
				kobj_find_eol(file);
				goto exit;
			}
not_a_digit:
			kobj_file_err(CE_WARN, file,
			"Expecting  a value in hex or decimal but found %s ",
			    tokval);
			kobj_find_eol(file);
			goto exit;
		}
	}	/* end of while */
	if (!missing_terminators(file)) {
		/* process the classcode info */
		filterp = kmem_zalloc(
		    sizeof (struct filter), KM_SLEEP);
		(void) strncpy(filterp->name, "classcode", FILTER_NAME_LEN);
		filterp->filter_info.classcode_info.classcode = classcode;
		filterp->filter_info.classcode_info.mask_defined = mask_defined;
		if (mask_defined) {
			filterp->filter_info.classcode_info.mask = mask;
		}
	}
exit:
	kmem_free(tokval, MAX_HWC_LINESIZE);
	return (filterp);

}

static struct filter *
vf_num_filter_parse_func(void *arg)
{
	uint16_t	vf_num;
	struct	filter	*filterp = NULL;
	token_t 	token;
	char		*tokval;
	u_longlong_t	ival;
	struct	_buf	*file = (struct _buf *)arg;

	tokval = kmem_alloc(MAX_HWC_LINESIZE, KM_SLEEP);
	token = kobj_lex(file, tokval, MAX_HWC_LINESIZE);
	if (token == EOF) {
		kobj_file_err(CE_WARN, file, "Incomplete filter found");
		goto exit;
	}
	switch (token) {
	case NEWLINE:
		kobj_file_err(CE_WARN, file, "Incomplete filter found");
		kobj_newline(file);
		goto exit;
	case RIGHT_SQUARE_BRACKET:
		/* possibly end of filter */
		(void) kobj_ungetc(file);
		break;
	case HEXVAL:
	case DECVAL:
		(void) kobj_getvalue(tokval, &ival);
		vf_num = (uint16_t)ival;
		break;
	default:
		kobj_file_err(CE_WARN, file,
		"Expecting  a value in hex or decimal but found %s ",
		    tokval);
		kobj_find_eol(file);
		goto exit;
	}
	if (!missing_terminators(file)) {
		/* process the vf_num info */
		filterp = kmem_zalloc(
		    sizeof (struct filter), KM_SLEEP);
		(void) strncpy(filterp->name, "vf_num", FILTER_NAME_LEN);
		filterp->filter_info.vf_num = vf_num;
	}
exit:
	kmem_free(tokval, MAX_HWC_LINESIZE);
	return (filterp);

}

static struct filter *
path_filter_parse_func(void *arg)
{
	char	pathname[PATHNAME_LEN + 1];
	int	ch, ret;
	struct filter *filterp = NULL;
	struct	_buf	*file = (struct _buf *)arg;

	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	(void) kobj_ungetc(file);
	ret = parse_pathname(file, pathname,
	    sizeof (pathname));
	if (pathname[0] == 0) {
		kobj_file_err(CE_NOTE, file, missing_pathname_err);
		kobj_find_eol(file);
		return (filterp);
	}
	if (ret)
		return (filterp);
	if (ret == 0) {
		PARSE_DBG(file, "found filter path %s ", pathname);
	}
	if (!missing_terminators(file)) {
		/* process the pathname */
		filterp = kmem_zalloc(
		    sizeof (struct filter), KM_SLEEP);
		(void) strncpy(filterp->name, "path", FILTER_NAME_LEN);
		(void) strncpy(filterp->filter_info.path, pathname,
		    PATHNAME_LEN);
	}
	return (filterp);
}

static struct action *
compatible_action_parse_func(void *arg)
{
	struct action		*actionp = NULL;
	int			ch, i, plus_minus_pair_found;
	char			*tokval;
	token_t			token;
	int			state = 0;
	struct	_buf	*file = (struct _buf *)arg;

	plus_minus_pair_found = 0;
	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	compatible_string[0][0] = '\0';
	compatible_string[1][0] = '\0';
	compatible_string[2][0] = '\0';
	if (isequals(ch))
		state = FOUND_EQUAL_OPERATOR;
	if (ch == '-')
		state = FOUND_MINUS_OPERATOR;
	if (ch == '+')
		state = FOUND_PLUS_OPERATOR;
	tokval = kmem_alloc(MAX_HWC_LINESIZE, KM_SLEEP);
found_operator:
	if (state & FOUND) {
		token = pci_lex(file, tokval, MAX_HWC_LINESIZE);
		switch (token) {
		case EOF:
		case NEWLINE:
			kobj_file_err(CE_WARN, file,
			    "Unexpected end of line detected");
			if (token == NEWLINE)
				kobj_newline(file);
			goto exit;
		case STRING:
			(void) strncpy(compatible_string[state & OPERATOR_MASK],
			    tokval, COMPATIBLE_NAME_LEN);
			break;
		default:
			kobj_file_err(CE_WARN, file,
			    "Expecting  a string but found %s ",
			    tokval);
			kobj_find_eol(file);
			goto exit;
		}
		/* skip white chars */
		while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
			;
		if (!plus_minus_pair_found) {
			if (state == FOUND_MINUS_OPERATOR) {
				if (ch == '+') {
					plus_minus_pair_found = 1;
					state = FOUND_PLUS_OPERATOR;
					goto found_operator;
				}
			}
			if (state == FOUND_PLUS_OPERATOR) {
				if (ch == '-') {
					plus_minus_pair_found = 1;
					state = FOUND_MINUS_OPERATOR;
					goto found_operator;
				}
			}
		}
		if (!isnewline(ch) && (ch != -1)) {
			kobj_file_err(CE_WARN, file,
			    "Expecting end of line but found %c", ch);
			kobj_find_eol(file);
			goto exit;
		}
		kobj_newline(file);
	}
	if (state == 0) {
		kobj_file_err(CE_WARN, file,
		    "Expecting '=' or '-' but found %c ",
		    ch);
		kobj_find_eol(file);
		return (actionp);
	}
	actionp = kmem_zalloc(
	    sizeof (struct action), KM_SLEEP);
	(void) strncpy(actionp->name, "compatible", ACTION_NAME_LEN);
	for (i = 0; i < NUM_OF_COMPATIBLE_OPERATORS; i++) {
		(void) strncpy(actionp->action_info.compatible_info.
		    compatible_string[i], compatible_string[i],
		    COMPATIBLE_NAME_LEN);
	}
exit:
	kmem_free(tokval, MAX_HWC_LINESIZE);
	return (actionp);
}

static struct action *
reg_action_parse_func(void *arg)
{
	struct action	*actionp = NULL;
	int		ch, regd_index;
	char		*tokval, *tokp, *digitp;
	token_t		token;
	int		state = (EXPECT | 0);
	int		operator = 0;
	u_longlong_t	reg_assign_value;
	struct	_buf	*file = (struct _buf *)arg;
	int		reg_type = 0;
	char		digits[NUM_DIGITS + 1];
	boolean_t	extended_cap = B_FALSE;

	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	if (ch != '[') {
		kobj_file_err(CE_WARN, file, "missing '['");
		if (ch == -1)
			return (actionp);
		if (isnewline(ch)) {
			kobj_newline(file);
			return (actionp);
		}
		kobj_find_eol(file);
		return (actionp);
	}
	/*
	 * Look for BAR#, offset and size
	 */
	tokval = kmem_alloc(MAX_HWC_LINESIZE, KM_SLEEP);
	/* CONSTANTCONDITION */
	while (true) {
		regd_index = (state & REGD_INDEX_MASK);
		if ((state & FOUND) && (regd_index >= (NUM_REGD -1))) {
			break;
		}
		token = pci_lex(file, tokval, MAX_HWC_LINESIZE);
		switch (token) {
		case EOF:
		case NEWLINE:
			kobj_file_err(CE_WARN, file,
			    "Unexpected end of line detected");
			if (token == NEWLINE)
				kobj_newline(file);
			goto exit;
		case NAME:
			if ((state & EXPECT) && (regd_index == 0)) {
				/*
				 * Process BAR/CONFIG
				 */
				if (strstr(tokval, "BAR") == tokval) {
					tokp = tokval + sizeof ("BAR") - 1;
					digitp = tokp;
					if (*tokp == '\0') {
						kobj_file_err(CE_WARN, file,
						    "Missing  BAR num");
						kobj_find_eol(file);
						goto exit;
					}
					while (isdigit(*digitp)) {
						digitp++;
					}
					if (*digitp != '\0') {
						kobj_file_err(CE_WARN, file,
			"Expecting  BAR num in decimal but found %s ",
						    tokp);
						kobj_find_eol(file);
						goto exit;
					}
					reg_type = 0;
					reg_ival[regd_index] = atoi(tokp);
					state = (FOUND | regd_index);
					break;
				}
				if (strcmp(tokval, "CONFIG") == 0) {
					reg_type = CONFIG_REG;
					/* skip white chars */
					while ((ch = kobj_getc(file)) == ' ' ||
					    ch == '\t')
						;
					if (ch == ',') {
						reg_ival[regd_index] = 0;
						state = (FOUND | regd_index);
						(void) kobj_ungetc(file);
						break;
					}
					if (ch != '+') {
						kobj_file_err(CE_WARN, file,
					"Expecting  + symbol  but found %c ",
						    ch);
						kobj_find_eol(file);
						goto exit;
					}
					/* skip white chars */
					while ((ch = kobj_getc(file)) == ' ' ||
					    ch == '\t')
						;
					digitp = digits;
					while ((isdigit(ch) &&
					    (((uintptr_t)digitp -
					    (uintptr_t)digits)) < NUM_DIGITS)) {
						*digitp++ = (char)ch;
						ch = kobj_getc(file);
					}
					if (digitp == digits) {
						kobj_file_err(CE_WARN, file,
						"Missing  CONFIG id");
						kobj_find_eol(file);
						goto exit;
					}
					if (ch == 'x') {
						extended_cap = B_TRUE;
					} else {
						extended_cap = B_FALSE;
						(void) kobj_ungetc(file);
					}
					*digitp = '\0';
					reg_ival[regd_index] = atoi(digits);
					state = (FOUND | regd_index);
					if (extended_cap)
						reg_ival[regd_index] |=
						    PCI_CAP_XCFG_FLAG;
					break;
				}
			}
			goto default_case;
		case HEXVAL:
		case DECVAL:
			if (state & EXPECT) {
				(void) kobj_getvalue(tokval,
				    &reg_ival[regd_index]);
				state = (FOUND | regd_index);
				break;
			} else
				goto default_case;
		case COMMA:
			if (state & FOUND) {
				state = (EXPECT | (regd_index + 1));
				break;
			}
default_case:
		default:
			if (state  & FOUND) {
				kobj_file_err(CE_WARN, file,
				"Expecting comma but found %s ", tokval);
			} else {
				if (regd_index == 0) {
					kobj_file_err(CE_WARN, file,
			"Expecting  BAR or CONFIG but found %s ",
					    tokval);
				} else {
					kobj_file_err(CE_WARN, file,
			"Expecting  a value in hex or decimal but found %s ",
					    tokval);
				}
			}
			kobj_find_eol(file);
			goto exit;
		}
	}
	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	if (ch != ']') {
		kobj_file_err(CE_WARN, file,
		    "Expecting ']' but found %c", ch);
		kobj_find_eol(file);
		goto exit;
	}
	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	if (ch == '|')
		operator |= OR_OPERATOR;
	if (ch == '&')
		operator |= AND_OPERATOR;
	if (ch == '^')
		operator |= EXCLUSIVE_OR_OPERATOR;
	if (operator)
		ch = kobj_getc(file);
	if (!isequals(ch)) {
		kobj_file_err(CE_WARN, file,
		    "Expecting '=' but found %c", ch);
		kobj_find_eol(file);
		goto exit;
	}
	/*
	 * Now we are expecting a Hex/Decimal value for register assignment
	 */
	token = pci_lex(file, tokval, MAX_HWC_LINESIZE);
	switch (token) {
	case EOF:
	case NEWLINE:
		kobj_file_err(CE_WARN, file,
		    "Unexpected end of line detected");
		if (token == NEWLINE)
			kobj_newline(file);
		goto exit;
	case HEXVAL:
	case DECVAL:
		(void) kobj_getvalue(tokval, &reg_assign_value);
		break;
	default:
		kobj_file_err(CE_WARN, file,
		    "Expecting  a value in hex or decimal but found %s ",
		    tokval);
		kobj_find_eol(file);
		goto exit;
	}
	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	if (isnewline(ch)) {
		/* process the reg info. */
		actionp = kmem_zalloc(
		    sizeof (struct action), KM_SLEEP);
		(void) strncpy(actionp->name, "reg", ACTION_NAME_LEN);
		for (regd_index = 0; regd_index < NUM_REGD;
		    regd_index++) {
			actionp->action_info.reg_info.regd[regd_index] =
			    reg_ival[regd_index];
		}
		actionp->action_info.reg_info.reg_type = reg_type;
		actionp->action_info.reg_info.value = reg_assign_value;
		actionp->action_info.reg_info.assign_operator = operator;
		kobj_newline(file);
	} else {
		if (ch == -1) {
			kobj_file_err(CE_WARN, file,
			    "Unexpected End of file. Expecting end of line");
		}
		kobj_file_err(CE_WARN, file,
		    "Expecting End of line but found char %c", ch);
		kobj_find_eol(file);
	}

exit:
	kmem_free(tokval, MAX_HWC_LINESIZE);
	return (actionp);
}

static struct action *
num_vf_action_parse_func(void *arg)
{
	struct action	*actionp = NULL;
	int		ch;
	char		*tokval;
	u_longlong_t	ival;
	token_t		token;
	struct	_buf	*file = (struct _buf *)arg;

	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	if (!isequals(ch)) {
		kobj_file_err(CE_WARN, file, missing_equals_err);
		if (ch == -1)
			return (actionp);
		if (isnewline(ch)) {
			kobj_newline(file);
			return (actionp);
		}
		kobj_find_eol(file);
		return (actionp);
	}
	tokval = kmem_alloc(MAX_HWC_LINESIZE, KM_SLEEP);
	token = pci_lex(file, tokval, MAX_HWC_LINESIZE);
	switch (token) {
	case EOF:
	case NEWLINE:
		kobj_file_err(CE_WARN, file, "Missing value for num_vf");
		if (token == NEWLINE)
			kobj_newline(file);
		goto exit;
	case HEXVAL:
	case DECVAL:
		(void) kobj_getvalue(tokval, &ival);
		break;
	default:
		kobj_file_err(CE_WARN, file,
		    "Expecting  a value in hex or decimal but found %s ",
		    tokval);
		kobj_find_eol(file);
		goto exit;
	}
	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	if (isnewline(ch)) {
		actionp = kmem_zalloc(
		    sizeof (struct action), KM_SLEEP);
		(void) strncpy(actionp->name, "num-vf", ACTION_NAME_LEN);
		actionp->action_info.num_vf = (uint16_t)ival;
		kobj_newline(file);
	} else {
		if (ch == -1) {
			kobj_file_err(CE_WARN, file,
			    "Unexpected End of file. Expecting end of line");
		}
		kobj_file_err(CE_WARN, file,
		    "Expecting End of line but found char %c", ch);
		kobj_find_eol(file);
	}
exit:
	kmem_free(tokval, MAX_HWC_LINESIZE);
	return (actionp);
}

struct filter_parse_table filter_parse_table[] = {
"id", id_filter_parse_func,
"classcode", classcode_filter_parse_func,
"path", path_filter_parse_func,
"vf_num", vf_num_filter_parse_func,
NULL, NULL};

struct action_parse_table action_parse_table[] = {
"compatible", compatible_action_parse_func,
"reg", reg_action_parse_func,
"num-vf", num_vf_action_parse_func,
NULL, NULL};

static void
get_filter_parse_func(char *name, struct filter *(**f)(void *))
{
	struct filter_parse_table *filter_parse_tablep;

	*f = NULL;
	for (filter_parse_tablep = &filter_parse_table[0];
	    filter_parse_tablep->name != NULL; filter_parse_tablep++) {
		if (strcmp(name, filter_parse_tablep->name) == 0) {
			PARSE_DBG(NULL, "filter: %s detected\n", name);
			*f = filter_parse_tablep->f;
			break;
		}
	}

}

static struct filter *
parse_filter(struct _buf *file)
{
	int		ch;
	struct	filter	*filterp = NULL;
	token_t 	token;
	char		*tokval;
	struct filter	*(*filter_parse_func)(void *);

	PARSE_DBG(NULL, "parsing filter...\n");
	/*
	 * Filters are one of the following:
	 *
	 * id=<vendor_id>,<device_id>,<revision_id>,
	 *	<subsystem-vendorid>,<subsystemid>
	 * classcode=<classcode>[,<mask>]
	 * path=<devpath>
	 */
	tokval = kmem_alloc(MAX_HWC_LINESIZE, KM_SLEEP);
	token = pci_lex(file, tokval, MAX_HWC_LINESIZE);
	if (token == EOF) {
		kobj_file_err(CE_WARN, file, "Incomplete filter found");
		kmem_free(tokval, MAX_HWC_LINESIZE);
		return (filterp);
	}
	switch (token) {
	case NEWLINE:
		kobj_file_err(CE_WARN, file, "Incomplete filter found");
		kobj_newline(file);
		break;
	case NAME:
		get_filter_parse_func(tokval, &filter_parse_func);
		if (filter_parse_func != NULL) {
			/* skip white chars */
			while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
				;
			/* check if assignment operator is present */
			if (!isequals(ch)) {
				kobj_file_err(CE_WARN, file,
				    missing_equals_err);
				if (ch == -1)
					break;
				if (isnewline(ch)) {
					kobj_newline(file);
					break;
				}
				kobj_find_eol(file);
				break;
			}
			filterp = (*filter_parse_func)((void *)file);
			break;
		}
		/* fall thru to default case */
	default:
		kobj_file_err(CE_WARN, file,
		    "Expecting a valid filter name: id|path|classcode"
		    " but found %s ", tokval);
		kobj_find_eol(file);
		break;
	}
	kmem_free(tokval, MAX_HWC_LINESIZE);
	return (filterp);
}

static struct action *
parse_action(char *action_name, struct _buf *file)
{
	struct action_parse_table *action_parse_tablep;
	struct action	*(*action_parse_func)(void *);
	struct	action *actionp = NULL;

	action_parse_func = NULL;
	if ((strcmp(action_name, "compatible-") == 0) ||
	    (strcmp(action_name, "compatible+") == 0)) {
		/*
		 * the '-' at the end of compatible is
		 * not part of action name but since we looked for
		 * a NAME token and '-' is a valid char for NAME token.
		 * We strip the last '-' char and proceed.
		 */
		action_name[strlen(action_name) -1] = '\0';
		(void) kobj_ungetc(file);
	}
	for (action_parse_tablep = &action_parse_table[0];
	    action_parse_tablep->name != NULL; action_parse_tablep++) {
		if (strcmp(action_name, action_parse_tablep->name) == 0) {
			PARSE_DBG(NULL, "action: %s detected\n", action_name);
			action_parse_func = action_parse_tablep->f;
			break;
		}
	}
	if (action_parse_func == NULL) {
		kobj_file_err(CE_WARN, file,
		    "Invalid action: %s found", action_name);
		kobj_find_eol(file);
		return (actionp);
	}
	actionp = (*action_parse_func)((void *)file);
	return (actionp);
}

static int
extract_param_name(struct _buf *file, char *param_name, int config_type)
{
	char		*cp;
	int		ch, ret;

	ret = 0;
	cp = param_name;
	*cp = '\0';
extract:
	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	if (ch == '#') {
		kobj_find_eol(file);
		goto extract;
	}
	if (isnewline(ch)) {
		kobj_newline(file);
		goto extract;
	}
	if (ch == '[') {
		if ((ch = kobj_getc(file)) == '[') {
			/* filter start detected */
			(void) kobj_ungetc(file);
			(void) kobj_ungetc(file);
			return (2);
		}
		if (ch == -1)
			return (-1);
		/* config section start detected */
		(void) kobj_ungetc(file);
		(void) kobj_ungetc(file);
		return (4);
	}
	(void) kobj_ungetc(file);
	while ((ch = kobj_getc(file)) != -1 && !iswhite(ch) &&
	    !isequals(ch) && !isquotes(ch) && !isnewline(ch)) {
		if (ch == '[') {
			if (config_type == SYSTEM_CONFIG) {
				(void) kobj_ungetc(file);
				*cp = '\0';
				return (0);
			}
			if ((ch = kobj_getc(file)) == '[') {
				/* filter start detected */
				(void) kobj_ungetc(file);
				(void) kobj_ungetc(file);
				*cp = '\0';
				return (2);
			}
			if (ch == -1) {
				*cp = '\0';
				return (-1);
			}
			(void) kobj_ungetc(file);
			/* restore ch before continuing */
			ch = '[';
		}
		if (((uintptr_t)cp - (uintptr_t)param_name) >=
		    PARAM_LEN) {
			kobj_file_err(CE_WARN, file,
			    "param name exceeds %d chars",
			    PARAM_LEN);
			kobj_find_eol(file);
			ret = 1;
			break;
		}
		*cp++ = (char)ch;
	}
	if (ch != -1)
		(void) kobj_ungetc(file);
	else
		ret = -1;
	if (ret == 0)
		*cp = '\0';
	return (ret);
}

static int
end_of_nvlist(struct _buf *file)
{
	int	ch;

	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	if (ch == '}') {
		/*
		 * end of sub nvlist.
		 */
		/* skip white chars */
		while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
			;
		if (ch != '\n') {
			kobj_file_err(CE_WARN, file,
		"Unexpected char %c after } Expecting EOL\n", ch);
			kobj_find_eol(file);
			return (1);
		}
		kobj_newline(file);
		return (1);
	}
	if (ch == -1) {
		kobj_file_err(CE_WARN, file,
		    "Unexpected EOF detected.\n");
		return (-1);
	}
	if (isnewline(ch)) {
		kobj_newline(file);
		return (0);
	}
	(void) kobj_ungetc(file);
	return (0);
}

static int
parse_device_list(char *name_string, struct _buf *file, nvlist_t **nvlp,
	int sub_nvlist_depth)
{
	int		err, rval, ch;
	char		*cp;
	char		value_string[MAX_STRING_LEN + 1];
	char		*string_array[MAX_ELEMS];
	char		param_name[PARAM_LEN +1];
	nvlist_t	*sub_nvlist = NULL;
	uint_t		index;

	PARSE_DBG(NULL, "parsing device list...param_name = %s depth = %d\n",
	    name_string, sub_nvlist_depth);
	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
		;
	if ((ch == '\n') || (ch == -1)) {
		kobj_file_err(CE_WARN, file,
		    "Unexpected End of line or file");
		if (ch == '\n') {
			kobj_newline(file);
			return (0);
		}
		return (-1);
	}
	if (!isequals(ch)) {
		kobj_file_err(CE_WARN, file,
		    "Expecting = but found %c\n", ch);
		kobj_find_eol(file);
		return (1);
	}
	/* skip white chars */
	while ((ch = kobj_getc(file)) == ' ' || (ch == '\t'))
		;
	if (ch == '{') {
		if (sub_nvlist_depth++ > MAX_SUB_NVLIST_DEPTH) {
			kobj_file_err(CE_WARN, file,
			    "nesting of sub nvlists exceeds %d\n",
			    MAX_SUB_NVLIST_DEPTH);
			return (-1);
		}
		/*
		 * start of another nvlist
		 * process name = value statements
		 */
		while ((rval = end_of_nvlist(file)) == 0) {
			rval = extract_param_name(file, param_name,
			    DEVICE_CONFIG);
			/*
			 * check for end of nvlist '}' seen by
			 * extract_param_name
			 */
			if (param_name[0] == '}')
				break;
			if (rval == -1)
				break;	/* EOF case */
			if (rval == 1)
				continue;
			if (rval)
				break;
			rval = parse_device_list(param_name, file,
			    &sub_nvlist, sub_nvlist_depth);
			if (rval == -1) break;
		}
		/*
		 * if sub_nvlist is not NULL add sub_nvlist to nvlp
		 */
		if (*nvlp == NULL) {
			err = nvlist_alloc(nvlp, NV_UNIQUE_NAME, KM_SLEEP);
			if (err) {
				kobj_file_err(CE_WARN, file,
				    "Failed to allocate nvlist");
				return (1);
			}
		}
		if (sub_nvlist) {
			err = nvlist_add_nvlist(*nvlp, name_string,
			    sub_nvlist);
			if (err) {
				kobj_file_err(CE_WARN, file,
				    "pciconf:Failed to add %s to nvlist",
				    name_string);
			}
			nvlist_free(sub_nvlist);
		}
		return (rval);
	}
	(void) kobj_ungetc(file);
	cp = value_string;
	*cp = '\0';
	index = 0;
	while ((ch = kobj_getc(file)) != -1 &&
	    !isequals(ch) && !isnewline(ch) && (ch != '[') && (ch != ']')) {
		if (((uintptr_t)cp - (uintptr_t)value_string) >=
		    MAX_STRING_LEN) {
			kobj_file_err(CE_WARN, file,
			    value_string_overflow_err);
			kobj_find_eol(file);
			return (1);
		}
		if (iswhite(ch)) {
			/* skip all white spaces */
			while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
				;
			if (isnewline(ch) || (ch == -1) || (ch == '[') ||
			    (ch == ']'))
				break;
			if (ch != ',') {
				kobj_file_err(CE_WARN, file,
				    "Missing comma ");
				return (1);
			}
		}
		/*
		 * If comma is detected then we have an array.
		 */
		if (ch == ',') {
			/*
			 * Get rid of white spaces preceding comma
			 */
			while (((uintptr_t)cp - (uintptr_t)value_string) > 0) {
				if (!iswhite(*(cp - 1)))
					break;
				cp--;
			}
			*cp = '\0';
			string_array[index] = kmem_alloc(MAX_STRING_LEN + 1,
			    KM_SLEEP);
			(void) strncpy(string_array[index], value_string,
			    MAX_STRING_LEN);
			index++;
			cp = value_string;
			*cp = '\0';
			/* skip white chars */
			while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
				;
			(void) kobj_ungetc(file);
		} else {
			*cp++ = (char)ch;
		}
	}
	*cp = '\0';
	if (index) {
		string_array[index] = kmem_alloc(MAX_STRING_LEN + 1,
		    KM_SLEEP);
		(void) strncpy(string_array[index], value_string,
		    MAX_STRING_LEN);
		index++;
	}
	if (iswhite(ch)) {
		/* skip white chars */
		while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
			;
	}
	if (!isnewline(ch) && (ch != -1)) {
		kobj_file_err(CE_WARN, file,
		    "Expecting end of line or file but found %c", ch);
		kobj_find_eol(file);
		return (1);
	}
	if (isnewline(ch))
		kobj_newline(file);
	if (*nvlp == NULL) {
		err = nvlist_alloc(nvlp, NV_UNIQUE_NAME, KM_SLEEP);
		if (err) {
			kobj_file_err(CE_WARN, file,
			    "Failed to allocate nvlist");
			return (1);
		}
	}
	if (index > 1) {
		err = nvlist_add_string_array(*nvlp, name_string,
		    string_array, index);
		while (index--) {
			kmem_free(string_array[index], MAX_STRING_LEN + 1);
		}
	} else {
		err = nvlist_add_string(*nvlp, name_string,
		    (void *)value_string);
	}
	if (err) {
		kobj_file_err(CE_WARN, file,
		    "pciconf:Failed to add %s to nvlist", name_string);
	}
	return (err);
}

static void
parse_config_section(struct _buf *file, int config_type)
{
	int		ch, rval;
	char		*tokval;
	char		label[LABEL_LEN + 1];
	struct	config_data	*config_datap;
	struct	config_data	*last_datap;
	struct	action	*actionp = NULL;
	struct	action	*last_actionp;
	struct	filter	*filterp = NULL;
	struct	filter	*last_filterp = NULL;
	boolean_t	filter_error = B_FALSE;

	PARSE_DBG(NULL, "parsing %s configuration section...\n",
	    config_section_name[config_type]);
	tokval = kmem_alloc(MAX_HWC_LINESIZE, KM_SLEEP);
	/* CONSTANTCONDITION */
	while (true) {
		label[0] = 0;
		rval = extract_param_name(file, label, config_type);
		if (rval == -1)
			break;
		PARSE_DBG(file, "label %s found ", label);
		/* skip white chars */
		while ((ch = kobj_getc(file)) == ' ' || ch == '\t')
			;
		if (ch == '\n') {
			if (filter_error)
				goto skip_line;
			/*
			 * LINE with just label, no filter
			 * Just store the label and continue.
			 */
			config_datap = kmem_zalloc(
			    sizeof (struct config_data), KM_SLEEP);
			(void) strncpy(config_datap->label, label, LABEL_LEN);
			config_datap->filterp = NULL;
			config_datap->datap = NULL;
			/*
			 * Insert this at the end of pciconf_data_hd
			 */
			for (last_datap =  pciconf_data_hd.
			    config_datap[config_type];
			    (last_datap && last_datap->next);
			    last_datap = last_datap->next)
				;
			if (last_datap)
				last_datap->next = config_datap;
			else {
				pciconf_data_hd.config_datap
				    [config_type] = config_datap;
			}
skip_line:
			kobj_newline(file);
			continue;
		}
		/*
		 * Look ahead for possible filter start.
		 * filter starts with '[['
		 */
		if (ch == '[') {
			/* possible start of filter */
			if ((ch = kobj_getc(file)) == '[') {
				/*
				 * Locate the last datap
				 */
				for (last_datap =  pciconf_data_hd.
				    config_datap[config_type];
				    (last_datap && last_datap->next);
				    last_datap = last_datap->next)
					;
				/* start of filter */
				filterp = parse_filter(file);
				if (!filterp) {
					filter_error = B_TRUE;
					kobj_file_err(CE_WARN, file,
		    "skipping all lines until next valid filter\n");
					continue;
				}
				filter_error = B_FALSE;
				if (last_datap && (label[0] == '\0') &&
				    (last_datap->datap == NULL)) {
					/*
					 * current filter is to be ANDed
					 * with the previous filter so add
					 * it to the filterp list.
					 */
					for (last_filterp = last_datap->filterp;
					    last_filterp && last_filterp->next;
					    last_filterp = last_filterp->next)
						;
					if (last_filterp)
						last_filterp->next = filterp;
					else
						last_datap->filterp = filterp;
					continue;
				}
				config_datap = kmem_zalloc(
				    sizeof (struct config_data),
				    KM_SLEEP);
				(void) strncpy(config_datap->label, label,
				    LABEL_LEN);
				config_datap->filterp = filterp;
				config_datap->datap = NULL;
				config_datap->next = NULL;
				/*
				 * Insert this at the end of pciconf_data_hd
				 */
				if (last_datap)
					last_datap->next = config_datap;
				else {
					pciconf_data_hd.config_datap
					    [config_type] = config_datap;
				}
				continue;
			}
			(void) kobj_ungetc(file);
			if (ch != -1)
				(void) kobj_ungetc(file);
			if (label[0] == 0) {
				/*
				 * '[' is the first non-white char.
				 * This could be start of config. section.
				 */
				break;
			}
		} else {
			(void) kobj_ungetc(file);
		}
		if (filter_error) {
			kobj_find_eol(file);
			continue;
		}
		if (config_type == SYSTEM_CONFIG) {
			/*
			 * We drop here if we are in the action section
			 * of the System Configuration.
			 * possible actions are:
			 *	num-vf
			 *	compatible
			 *	reg
			 */
			actionp = parse_action(label, file);
			if (actionp) {
				actionp->next = NULL;
				/*
				 * Find the end of last config section
				 */
				for (last_datap =  pciconf_data_hd.
				    config_datap[config_type];
				    (last_datap && last_datap->next);
				    last_datap = last_datap->next)
					;
				if (last_datap) {
					/*
					 * Insert actionp at the end of
					 * existing actionp's
					 */
					for (last_actionp =
					    (struct action *)last_datap->datap;
					    (last_actionp) &&
					    (last_actionp->next);
					    last_actionp = last_actionp->next)
						;
					if (last_actionp)
						last_actionp->next = actionp;
					else
						last_datap->datap =
						    (void *)actionp;
				} else {
					/*
					 * We have an action item without label
					 * OR filter and this is the first item.
					 */
					config_datap = kmem_zalloc(
					    sizeof (struct config_data),
					    KM_SLEEP);
					pciconf_data_hd.config_datap
					    [config_type] = config_datap;
					config_datap->next = NULL;
					config_datap->label[0] = '\0';
					config_datap->filterp = NULL;
					config_datap->datap = (void *)actionp;
				}
			}
		}	/* end of system config action */
		if (config_type == DEVICE_CONFIG) {
			/*
			 * Find the end of last config section
			 */
			for (last_datap =  pciconf_data_hd.
			    config_datap[config_type];
			    (last_datap && last_datap->next);
			    last_datap = last_datap->next)
				;
			if (last_datap) {
				(void) parse_device_list(label, file,
				    (nvlist_t **)&last_datap->datap, 0);
			} else {
				/*
				 * We have an assigbment without label
				 * OR filter and this is the first item.
				 */
				config_datap = kmem_zalloc(
				    sizeof (struct config_data),
				    KM_SLEEP);
				pciconf_data_hd.config_datap
				    [config_type] = config_datap;
				config_datap->next = NULL;
				config_datap->label[0] = '\0';
				config_datap->filterp = NULL;
				(void) parse_device_list(label, file,
				    (nvlist_t **)&config_datap->datap, 0);
			}
		}
	}	/* end of while */
	kmem_free(tokval, MAX_HWC_LINESIZE);
}

typedef enum {
	pciconf_begin, config_header_begin, VF_config_section,
	config_section, system_config_section, device_config_section
} pciconf_state_t;

char	pciconf_config_section_name[40];

int
pciconf_parse_now(char *fname)
{
	struct _buf	*file;
	char		*tokval;
	token_t 	token;
	pciconf_state_t	state;
	struct	bootstat	bootstat_buf;

	/*
	 * At boot time we use the pciconf_file which contains the
	 * the contents of /etc/pci.conf
	 */
	if (pciconf_file != (struct _buf *)(-1))
		file = pciconf_file;
	else {
		if ((file = kobj_open_file(fname)) == (struct _buf *)-1) {
			PCICONF_DBG("Cannot open %s", fname);
			return (-1);
		}
		if (kobj_fstat(file->_fd, &bootstat_buf) == 0) {
			if (pciconf_file_last_atime >=
			    (int64_t)bootstat_buf.st_mtim.tv_sec) {
				kobj_close_file(file);
				return (0);
			} else
				pciconf_file_last_atime =
				    bootstat_buf.st_atim.tv_sec;
		}
	}
	clean_pciconf_data();
	/*
	 * Initialize variables
	 */
	tokval = kmem_alloc(MAX_HWC_LINESIZE, KM_SLEEP);
	state = pciconf_begin;
	pciconf_config_section_name[0] = '\0';
	while ((token = pci_lex(file, tokval, MAX_HWC_LINESIZE)) != EOF) {
		switch (token) {
		case POUND:
			/*
			 * Skip comments.
			 */
			if (state == config_header_begin) {
				state = pciconf_begin;
				kobj_file_err(CE_WARN, file, tok_err, tokval);
				kobj_find_eol(file);
				break;
			}
			kobj_find_eol(file);
			break;
		case LEFT_SQUARE_BRACKET:
			if (state == config_header_begin) {
				state = pciconf_begin;
				kobj_file_err(CE_WARN, file,
			    "Unexpected [ found");
				kobj_find_eol(file);
				break;
			}
			state = config_header_begin;
			pciconf_config_section_name[0] = '\0';
			break;
		case RIGHT_SQUARE_BRACKET:
			if (state != config_header_begin) {
				kobj_file_err(CE_WARN, file, tok_err, tokval);
				kobj_find_eol(file);
				break;
			}
			token =  pci_lex(file, tokval, MAX_HWC_LINESIZE);
			if (token != NEWLINE) {
				kobj_file_err(CE_WARN, file, tok_err, tokval);
				kobj_find_eol(file);
				state = pciconf_begin;
				pciconf_config_section_name[0] = '\0';
				break;
			}
			PARSE_DBG(NULL, "config section name is %s\n",
			    pciconf_config_section_name);
			if (strncmp(pciconf_config_section_name,
			    "System_Configuration", MAX_HWC_LINESIZE) == 0) {
				state = system_config_section;
				kobj_newline(file);
				parse_config_section(file, SYSTEM_CONFIG);
				break;
			}
			if (strncmp(pciconf_config_section_name,
			    "Device_Configuration", MAX_HWC_LINESIZE) == 0) {
				state = device_config_section;
				kobj_newline(file);
				parse_config_section(file, DEVICE_CONFIG);
				break;
			} else {
				state = config_section;
				kobj_file_err(CE_WARN, file,
				    "Unrecognized section %s found",
				    pciconf_config_section_name);
			}
			kobj_newline(file);
			break;
		case NAME:
			if (state == config_header_begin) {
				(void) strncat(pciconf_config_section_name,
				    tokval,
				    sizeof (pciconf_config_section_name));
				break;
			}
			kobj_file_err(CE_WARN, file, tok_err, tokval);
			kobj_find_eol(file);
			break;
		case NEWLINE:
			/*
			 * config header should end before NEWLINE
			 */
			if (state == config_header_begin) {
				kobj_file_err(CE_WARN, file,
				"Incomplete configuration header %s found",
				    pciconf_config_section_name);
				state = pciconf_begin;
				pciconf_config_section_name[0] = '\0';
			}
			kobj_newline(file);
			break;
		default:
			if (state == config_header_begin) {
				kobj_file_err(CE_WARN, file,
				"Unexpected %s found in configuration header",
				    tokval);
				state = pciconf_begin;
				pciconf_config_section_name[0] = '\0';
				kobj_find_eol(file);
				break;
			}
			kobj_file_err(CE_WARN, file, tok_err, tokval);
			kobj_find_eol(file);
			break;
		}
	}
	kmem_free(tokval, MAX_HWC_LINESIZE);
	if (pciconf_file != file)
		kobj_close_file(file);
#ifdef DEBUG
	if (pciconf_parse_debug)
		dump_pciconf_data();
#endif
	return (0);	/* always return success */

}

static uint32_t
reg_read_func(ddi_acc_handle_t reg_handle, void *reg_addr, uint32_t reg_size)
{
	uint32_t	reg_value = 0xffffffff;

	switch (reg_size) {
	case 1:
		reg_value = (uint32_t)ddi_get8(reg_handle, (uint8_t *)reg_addr);
		break;
	case 2:
		reg_value = (uint32_t)ddi_get16(reg_handle,
		    (uint16_t *)reg_addr);
		break;
	case 4:
		reg_value = ddi_get32(reg_handle, (uint32_t *)reg_addr);
		break;
	default:
		break;
	}
	return (reg_value);
}

static void
reg_write_func(ddi_acc_handle_t reg_handle, void *reg_addr,
    uint32_t reg_value, uint32_t reg_size)
{
	switch (reg_size) {
	case 1:
		ddi_put8(reg_handle, (uint8_t *)reg_addr, (uint8_t)reg_value);
		break;
	case 2:
		ddi_put16(reg_handle,
		    (uint16_t *)reg_addr, (uint16_t)reg_value);
		break;
	case 4:
		ddi_put32(reg_handle, (uint32_t *)reg_addr, reg_value);
		break;
	default:
		break;
	}
}

static uint32_t
config_read_func(dev_info_t *dip, uint16_t offset, uint32_t reg_size)
{
	uint32_t	reg_value = 0xffffffff;
	pcie_bus_t	*bus_p;
	dev_info_t	*rcdip;

	bus_p = PCIE_DIP2BUS(dip);
	rcdip = PCIE_GET_RC_DIP(bus_p);

	switch (reg_size) {
	case 1:
		reg_value = (uint32_t)pci_cfgacc_get8(rcdip, bus_p->bus_bdf,
		    offset);
		break;
	case 2:
		reg_value = (uint32_t)pci_cfgacc_get16(rcdip, bus_p->bus_bdf,
		    offset);
		break;
	case 4:
		reg_value = pci_cfgacc_get32(rcdip, bus_p->bus_bdf, offset);
		break;
	default:
		break;
	}
	return (reg_value);
}

static void
config_write_func(dev_info_t *dip, uint16_t offset,
    uint32_t reg_value, uint32_t reg_size)
{
	pcie_bus_t	*bus_p;
	dev_info_t	*rcdip;

	bus_p = PCIE_DIP2BUS(dip);
	rcdip = PCIE_GET_RC_DIP(bus_p);
	switch (reg_size) {
	case 1:
		pci_cfgacc_put8(rcdip, offset, bus_p->bus_bdf,
		    (uint8_t)reg_value);
		break;
	case 2:
		pci_cfgacc_put16(rcdip, bus_p->bus_bdf, offset,
		    (uint16_t)reg_value);
		break;
	case 4:
		pci_cfgacc_put32(rcdip, bus_p->bus_bdf, offset, reg_value);
		break;
	default:
		break;
	}
}

static int
check_offset_alignment(uint_t offset, int reg_size, char *path)
{
	int	mask;

	switch (reg_size) {
		case 1:
			break;
		case 2:
		case 4:
			mask = reg_size - 1;
			if (mask & offset) {
				cmn_err(CE_WARN,
				"offset 0x%x not aligned to size boundary"
				" of %d for device %s\n",
				    offset, reg_size, path);
				return (1);
			}
			break;
		default:
			cmn_err(CE_WARN,
			"size should be 1, 2 or 4, found size=%d"
			" for device %s\n",
			    reg_size,
			    path);
			return (1);
	}
	return (0);
}

static int
reg_action(struct action *actionp, dev_info_t *dip)
{
	int		rcount, i, rlen, status;
	pci_regspec_t	*reg = NULL;
	char		*path = NULL;
	int		ret = 0;
	caddr_t		reg_addr;
	ddi_acc_handle_t	reg_handle = NULL;
	uint32_t	reg_value;
	dev_info_t	*pdip;
	uint16_t	base;
	uint16_t	offset;

	path = kmem_zalloc(MAXPATHLEN + 1, KM_SLEEP);
	if (pcie_pathname(dip, path) != 0)
		return (DDI_FAILURE);
	if (actionp->action_info.reg_info.reg_type == CONFIG_REG) {
		PCICONF_DBG(
"Processing CONFIG REG for device %s\n id = 0x%x offset = 0x%x sz = %d\n",
		    path,
		    actionp->action_info.reg_info.CONFIG_ID,
		    actionp->action_info.reg_info.REG_OFFSET,
		    actionp->action_info.reg_info.REG_SIZE);
		if (actionp->action_info.reg_info.CONFIG_ID != 0) {
			/*
			 * We need to Locate the id specified and
			 * get the base addr.
			 */
			(void) pcie_cap_locate(dip,
			    actionp->action_info.reg_info.CONFIG_ID, &base);
			if (base == NULL) {
				cmn_err(CE_WARN,
				"could not locate id 0x%x for device %s\n",
				    actionp->action_info.reg_info.CONFIG_ID,
				    path);
				ret = 1;
				goto exit;
			}
			PCICONF_DBG("base addr is = 0x%x\n", base);
		}
		offset = base + actionp->action_info.reg_info.REG_OFFSET;
		if (check_offset_alignment(offset,
		    actionp->action_info.reg_info.REG_SIZE, path) != 0) {
			ret = 1;
			goto exit;
		}
		if (actionp->action_info.reg_info.assign_operator) {
			/*
			 * We have one of |, &, or ^ operations to be performed
			 * before we write the register.
			 * Read the register first.
			 */
			reg_value = config_read_func(dip, offset,
			    actionp->action_info.reg_info.REG_SIZE);
			PCICONF_DBG(
			" reg_value(offset 0x%x) = 0x%x for device %s\n",
			    offset,
			    reg_value, path);
			switch (actionp->action_info.reg_info.assign_operator) {
				case OR_OPERATOR:
					reg_value |=
					    actionp->action_info.reg_info.value;
					break;
				case AND_OPERATOR:
					reg_value &=
					    actionp->action_info.reg_info.value;
					break;
				case EXCLUSIVE_OR_OPERATOR:
					reg_value ^=
					    actionp->action_info.reg_info.value;
					break;
				default:
					break;
			}
		} else {
			reg_value = actionp->action_info.reg_info.value;
		}
		PCICONF_DBG(
	"reg_value(offset 0x%x) before writing is = 0x%x for device %s\n",
		    offset,
		    reg_value, path);
		config_write_func(dip, offset,
		    actionp->action_info.reg_info.REG_SIZE,
		    reg_value);
		ret = 0;
		goto exit;
	}	/* end of CONFIG REG processing */
	pdip = (dev_info_t *)DEVI(dip)->devi_parent;
	if (DEVI(pdip)->devi_ops == NULL) {
		cmn_err(CE_WARN,
		    "Ignoring reg action for device %s\n", path);
		ret = DDI_FAILURE;
		goto exit;
	}
	status = ddi_getlongprop(DDI_DEV_T_ANY,
	    dip, DDI_PROP_DONTPASS, "reg", (caddr_t)&reg, &rlen);
	switch (status) {
		case DDI_PROP_SUCCESS:
		break;
		case DDI_PROP_NO_MEMORY:
			cmn_err(CE_WARN,
		"reg present, but unable to get memory for device %s\n",
			    path);
			ret = DDI_FAILURE;
			goto exit;
		default:
			cmn_err(CE_WARN, "no reg property found for device %s",
			    path);
			ret = DDI_FAILURE;
			goto exit;
	}
	rcount = rlen / sizeof (pci_regspec_t);
	for (i = 0; i < rcount; i++) {
		if (PCI_REG_REG_G(reg[i].pci_phys_hi) == actionp->action_info.
		    reg_info.BAR_NUM)
			break;
	}
	if (i >= rcount) {
		cmn_err(CE_WARN, "could not find bar# 0x%x for device %s\n",
		    actionp->action_info.reg_info.BAR_NUM, path);
		ret = 1;
		goto exit;
	}
	if (reg[i].pci_size_low <=
	    actionp->action_info.reg_info.REG_OFFSET) {
		cmn_err(CE_WARN,
		    "offset 0x%x exceeds size 0x%x for device %s\n",
		    actionp->action_info.reg_info.REG_OFFSET,
		    reg[i].pci_size_low, path);
		ret = 1;
		goto exit;
	}
	if (check_offset_alignment((uint_t)actionp->action_info.
	    reg_info.REG_OFFSET,
	    (int)actionp->action_info.reg_info.REG_SIZE, path) != 0) {
		ret = 1;
		goto exit;
	}
	ret = ddi_regs_map_setup(dip, i, &reg_addr, 0,
	    reg[i].pci_size_low, &reg_acc_attr, &reg_handle);
	if (ret != DDI_SUCCESS) {
		cmn_err(CE_WARN, "could not map register for device %s\n",
		    path);
		goto exit;
	}
	if (actionp->action_info.reg_info.assign_operator) {
		/*
		 * We have one of |, &, or ^ operations to be performed
		 * before we write the register.
		 * Read the register first.
		 */
		reg_value = reg_read_func(reg_handle,
		    (void *)(reg_addr +
		    actionp->action_info.reg_info.REG_OFFSET),
		    actionp->action_info.reg_info.REG_SIZE);
		PCICONF_DBG(
		    " reg_value(offset 0x%x) = 0x%x for device %s\n",
		    actionp->action_info.reg_info.REG_OFFSET,
		    reg_value, path);
		switch (actionp->action_info.reg_info.assign_operator) {
			case OR_OPERATOR:
				reg_value |=
				    actionp->action_info.reg_info.value;
				break;
			case AND_OPERATOR:
				reg_value &=
				    actionp->action_info.reg_info.value;
				break;
			case EXCLUSIVE_OR_OPERATOR:
				reg_value ^=
				    actionp->action_info.reg_info.value;
				break;
			default:
				break;
		}
	} else {
		reg_value = actionp->action_info.reg_info.value;
	}
	PCICONF_DBG(
	    "reg_value(offset 0x%x) before writing is = 0x%x for device %s\n",
	    actionp->action_info.reg_info.REG_OFFSET,
	    reg_value, path);
	reg_write_func(reg_handle, (void *)(reg_addr +
	    actionp->action_info.reg_info.REG_OFFSET),
	    reg_value,
	    actionp->action_info.reg_info.REG_SIZE);
exit:
	if (reg_handle)
		ddi_regs_map_free(&reg_handle);
	if (path)
		kmem_free(path, MAXPATHLEN + 1);
	if (reg)
		kmem_free(reg, rlen);
	return (ret);
}

static int
rebind_dip(dev_info_t *dip)
{
	char	*path;
	int	error;
	dev_info_t *pdip = ddi_get_parent(dip);

	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	(void) pcie_pathname(dip, path);
	if (DEVI(dip)->devi_compat_names) {
		kmem_free(DEVI(dip)->devi_compat_names,
		    DEVI(dip)->devi_compat_length);
		DEVI(dip)->devi_compat_names = NULL;
	}
	/*
	 * Add an extra hold on the parent to prevent it from ever
	 * having a zero devi_ref during the child rebind process.
	 * This is necessary to ensure that the parent will never
	 * detach(9E) during the rebind.
	 */
	ndi_hold_devi(pdip);		/* extra hold of parent */

	/* Unbind: demote the node back to DS_LINKED.  */
	if ((error = i_ndi_unconfig_node(dip, DS_LINKED, 0)) != DDI_SUCCESS) {
		ndi_rele_devi(pdip);	/* release initial hold */
		cmn_err(CE_WARN, "init_node: unbind for rebind "
		    "of node %s failed", path);
		goto out;
	}

	/*
	 * Now that we are demoted and marked for rebind, repromote.
	 * We need to do this in steps, instead of just calling
	 * ddi_initchild, so that we can redo the merge operation
	 * after we are rebound to the path-bound driver.
	 *
	 * Start by rebinding node to the path-bound driver.
	 */
	if ((error = ndi_devi_bind_driver(dip, 0)) != DDI_SUCCESS) {
		ndi_rele_devi(pdip);	/* release initial hold */
		cmn_err(CE_WARN, "init_node: rebind "
		    "of node %s failed", path);
		goto out;
	}

	/*
	 * Release our initial hold.
	 */
	ndi_rele_devi(pdip);
	PCICONF_DBG("successfully completed rebind of device %s\n", path);
out:
	kmem_free(path, MAXPATHLEN);
	return (error);
}

static int
compatible_action(struct action *actionp, dev_info_t *dip)
{
	char		**compatible_namep = NULL;
	char		**new_compatible_namep = NULL;
	int		new_compatible_namep_size = 0;
	uint_t		num_compatible_names_found;
	uint_t		num_names_to_update = 0;
	char		*path;
	int		i = -1;	/* indicates we are not replacing any */
	int		ret = DDI_PROP_SUCCESS;
	char		*err_msg_format;


	if (ddi_prop_lookup_string_array(DDI_DEV_T_ANY, dip,
	    DDI_PROP_DONTPASS, "compatible", &compatible_namep,
	    &num_compatible_names_found) != DDI_PROP_SUCCESS) {
		path = kmem_zalloc(MAXPATHLEN + 1, KM_SLEEP);
		(void) pcie_pathname(dip, path);
		cmn_err(CE_WARN, "Unable to read compatible"
		    "  property for device %s\n", path);
		kmem_free(path, MAXPATHLEN + 1);
		return (1);
	}
	if (actionp->action_info.compatible_info.
	    compatible_string[MINUS_OPERATOR][0]
	    != '\0') {
		/*
		 * Locate the string to be removed
		 */

		for (i = 0; i < num_compatible_names_found; i++) {
			if (strncmp(compatible_namep[i],
			    actionp->
			    action_info.compatible_info.
			    compatible_string[MINUS_OPERATOR],
			    COMPATIBLE_NAME_LEN) == 0)
				break;
		}
		if (i >= num_compatible_names_found) {
			path = kmem_zalloc(MAXPATHLEN + 1, KM_SLEEP);
			(void) pcie_pathname(dip, path);
			cmn_err(CE_WARN, "Could not find %s in"
			    " compatible property for device %s\n",
			    actionp->
			    action_info.compatible_info.
			    compatible_string[MINUS_OPERATOR],
			    path);
			kmem_free(path, MAXPATHLEN + 1);
			ddi_prop_free(compatible_namep);
			return (1);
		}
		num_names_to_update = num_compatible_names_found -1;
		/*
		 * Left shift the string array to account for
		 * removal of a string.
		 */
		for (; i < (num_compatible_names_found -1); i++)
			compatible_namep[i] =
			    compatible_namep[i+1];
		/*
		 * Since we will be deleting and recreating the
		 * property we need to allocate memory again for
		 * property strings we will be deleting.
		 * We will allocate one more than the required strings so that
		 * we can fit in the additional string if specified in the
		 * action. Example: compatible-"remove_me"+"add_me"
		 */
		new_compatible_namep_size = num_compatible_names_found
		    * sizeof (char *);
		new_compatible_namep = kmem_zalloc(
		    new_compatible_namep_size, KM_SLEEP);
		/*
		 * copy the old string list to the new one because
		 * we will be removing the exiting property and creating
		 * a new one.
		 */
		for (i = 0; i < (num_compatible_names_found -1); i++) {
			new_compatible_namep[i] = kmem_zalloc(
			    strlen(compatible_namep[i]) + 1, KM_SLEEP);
			(void) strcpy(new_compatible_namep[i],
			    compatible_namep[i]);
		}
		/*
		 * the new_compatible_namep now has the list of strings
		 * that the new compatible property should have.
		 * If specified we will add the string to be added in the
		 * code that follows below.
		 */

	}
	if (actionp-> action_info.compatible_info.
	    compatible_string[PLUS_OPERATOR][0] != '\0') {
		/*
		 * We have a string to be added to the property.
		 */
		if (!new_compatible_namep) {
			/*
			 * We do not have a string that is to be deleted
			 * so create new_compatible_namep fresh and copy the
			 * exiting string array into new_compatible_namep.
			 */
			num_names_to_update = num_compatible_names_found + 1;
			new_compatible_namep_size = num_names_to_update *
			    sizeof (char *);
			new_compatible_namep = kmem_zalloc(
			    new_compatible_namep_size, KM_SLEEP);
			for (i = 0; i < num_compatible_names_found; i++) {
				new_compatible_namep[i] = kmem_zalloc(
				    strlen(compatible_namep[i]) + 1, KM_SLEEP);
				(void) strcpy(new_compatible_namep[i],
				    compatible_namep[i]);
			}
		} else {
			/*
			 * This is the case where both '-' and '+" was
			 * specified in compatible action.
			 * We adjust the number of strings to be added to
			 * the property and continue.
			 * Note: The new_compatible_namep has an extra space
			 * allocated to accommodate the new string.
			 */
			num_names_to_update++;
		}
		new_compatible_namep[num_names_to_update - 1] =
		    kmem_zalloc(strlen(actionp-> action_info.
		    compatible_info.
		    compatible_string[PLUS_OPERATOR]) + 1,
		    KM_SLEEP);
		(void) strcpy(new_compatible_namep[num_names_to_update - 1],
		    actionp->action_info.compatible_info.
		    compatible_string[PLUS_OPERATOR]);
	}
	if (actionp-> action_info.compatible_info.
	    compatible_string[EQUAL_OPERATOR][0] != '\0') {
		num_names_to_update = 1;
		new_compatible_namep_size = num_names_to_update *
		    sizeof (char *);
		new_compatible_namep = kmem_zalloc(
		    new_compatible_namep_size, KM_SLEEP);
		new_compatible_namep[num_names_to_update - 1] =
		    kmem_zalloc(strlen(actionp-> action_info.
		    compatible_info.
		    compatible_string[EQUAL_OPERATOR]) + 1,
		    KM_SLEEP);
		(void) strcpy(new_compatible_namep[num_names_to_update - 1],
		    actionp->action_info.compatible_info.
		    compatible_string[EQUAL_OPERATOR]);
	}
	ret = ndi_prop_update_string_array(
	    DDI_DEV_T_NONE, dip, "compatible",
	    new_compatible_namep, num_names_to_update);
	if (ret != DDI_PROP_SUCCESS) {
		err_msg_format =
	"Unable to update compatible property for device %s err=0x%x\n";
		path = kmem_zalloc(MAXPATHLEN + 1, KM_SLEEP);
		(void) pcie_pathname(dip, path);
		cmn_err(CE_WARN, err_msg_format, path, ret);
		kmem_free(path, MAXPATHLEN + 1);
	}
	if (new_compatible_namep) {
		for (i = 0; i < num_names_to_update; i++)
			kmem_free(new_compatible_namep[i],
			    strlen(new_compatible_namep[i]) + 1);
		kmem_free(new_compatible_namep,
		    new_compatible_namep_size);
	}
	ddi_prop_free(compatible_namep);
#ifdef DEBUG
	if ((ret == DDI_PROP_SUCCESS) && pciconf_debug) {
		path = kmem_zalloc(MAXPATHLEN + 1, KM_SLEEP);
		(void) pcie_pathname(dip, path);
		if (actionp->action_info.compatible_info.
		    compatible_string[MINUS_OPERATOR][0] != '\0') {
			PCICONF_DBG("removed the string %s from compatible"
			    "  property for device %s\n",
			    actionp->
			    action_info.compatible_info.
			    compatible_string[MINUS_OPERATOR],
			    path);
		}
		if (actionp->action_info.compatible_info.
		    compatible_string[PLUS_OPERATOR][0] != '\0') {
			PCICONF_DBG("added the string %s to compatible"
			    "  property for device %s\n",
			    actionp->
			    action_info.compatible_info.
			    compatible_string[PLUS_OPERATOR],
			    path);
		}
		if (actionp->action_info.compatible_info.
		    compatible_string[EQUAL_OPERATOR][0] != '\0') {
			PCICONF_DBG("Created new compatible"
			    " property with string %s for device %s\n",
			    actionp->
			    action_info.compatible_info.
			    compatible_string[EQUAL_OPERATOR],
			    path);
		}
		kmem_free(path, MAXPATHLEN + 1);
	}
#endif
	return (ret);
}

static int
num_vf_action(struct action *actionp, dev_info_t *dip)
{
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	char path[MAXPATHLEN + 1];

	if (bus_p == NULL) {
		return (1);
	}
	if (bus_p->sriov_cap_ptr == NULL) {
		/* not a IOV capable device */
		return (0);
	}
	bus_p->num_vf = actionp->action_info.num_vf;
	if (bus_p->num_vf == 0xffff)
		bus_p->num_vf = bus_p->initial_num_vf;
	if (bus_p->num_vf > bus_p->initial_num_vf) {
		(void) pcie_pathname(dip, path);
		cmn_err(CE_WARN,
		    "num-vf %d for device %s exceeds %d. setting num-vf as 0\n",
		    bus_p->num_vf, path, bus_p->initial_num_vf);
		bus_p->num_vf = 0;
		return (1);
	}
	return (0);
}

struct action_table action_table[] = {
"reg", reg_action,
"compatible", compatible_action,
"num-vf", num_vf_action,
NULL, NULL};

void
get_action_func(char *name, int (**f)(struct action *actionp, dev_info_t *))
{
	struct action_table *action_tablep;

	*f = NULL;
	for (action_tablep = &action_table[0];
	    action_tablep->name != NULL; action_tablep++) {
		if (strncmp(name, action_tablep->name, ACTION_NAME_LEN) == 0) {
			PCICONF_DBG("action: %s detected\n", name);
			*f = action_tablep->f;
			break;
		}
	}

}

static int
id_filter(struct filter *filterp, dev_info_t *dip)
{
	dev_info_t	*rcdip;
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	int		i;
	uint16_t	id[MAX_IDS_IN_ID_FILTER];


	if (bus_p == NULL) {
		return (0);
	}
	id[0] = PCIE_VENID(bus_p);
	id[1] = PCIE_DEVID(bus_p);
	id[2] = bus_p->bus_rev_id;
	rcdip = PCIE_GET_RC_DIP(bus_p);
	ASSERT(rcdip != NULL);
	id[3] = pci_cfgacc_get16(rcdip, bus_p->bus_bdf, PCI_CONF_SUBVENID);
	id[4] = pci_cfgacc_get16(rcdip, bus_p->bus_bdf, PCI_CONF_SUBSYSID);
	for (i = 0; i < MAX_IDS_IN_ID_FILTER; i++) {
		if (!filterp->filter_info.id_info.id_defined[i])
			continue;
		if (filterp->filter_info.id_info.id_val[i] != id[i]) {
			return (0);
		}
	}
	/*
	 * found a match
	 */
	return (1);
}

static int
path_filter(struct filter *filterp, dev_info_t *dip)
{
	char path[MAXPATHLEN + 1];

	if (pcie_pathname(dip, path) != 0)
		return (0);
	if (strncmp(path, filterp->filter_info.path, MAXPATHLEN) == 0)
		return (1);
	return (0);
}

static int
classcode_filter(struct filter *filterp, dev_info_t *dip)
{
	dev_info_t	*rcdip;
	uint16_t	classcode;
	uint16_t	mask = 0xffff;
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);

	if (bus_p == NULL) {
		return (1);
	}
	rcdip = PCIE_GET_RC_DIP(bus_p);
	ASSERT(rcdip != NULL);
	classcode = pci_cfgacc_get16(rcdip, bus_p->bus_bdf, PCI_CONF_SUBCLASS);
	if (filterp->filter_info.classcode_info.mask_defined)
		mask = filterp->filter_info.classcode_info.mask;
	classcode &= mask;
	if (classcode == filterp->filter_info.classcode_info.classcode)
		return (1);
	else
		return (0);
}

struct filter_table filter_table[] = {
"id", id_filter,
"classcode", classcode_filter,
"path", path_filter,
NULL, NULL};

void
get_filter_func(char *name, int (**f)(struct filter *, dev_info_t *))
{
	struct filter_table *filter_tablep;

	*f = NULL;
	for (filter_tablep = &filter_table[0];
	    filter_tablep->name != NULL; filter_tablep++) {
		if (strncmp(name, filter_tablep->name, FILTER_NAME_LEN) == 0) {
			*f = filter_tablep->f;
			break;
		}
	}

}

void
apply_pciconf_configs(dev_info_t *dip)
{
	struct filter	*filterp;
	struct action	*actionp;
	struct config_data *config_datap;
	int	(*filter_func)(struct filter *filterp, dev_info_t *);
	int	(*action_func)(struct action *actionp, dev_info_t *);
	int	filter_match;
	int	filter_found = 0;
	int	ret;
	boolean_t	compatible_property_modified = B_FALSE;
	pcie_bus_t	*bus_p = PCIE_DIP2BUS(dip);
	char	*path = NULL;
#ifdef	DEBUG
	int	i;
	char	filter_names[256];
#endif

	path = kmem_zalloc(MAXPATHLEN + 1, KM_SLEEP);
	(void) pcie_pathname(dip, path);
	PCICONF_DBG("apply_pciconf_configs:path is %s\n", path);
	for (config_datap = pciconf_data_hd.
	    config_datap[SYSTEM_CONFIG]; config_datap;
	    config_datap = config_datap->next) {
		filterp = config_datap->filterp;
		actionp = (struct action *)config_datap->datap;
		for (; filterp; filterp = filterp->next) {
			filter_found = 1;
			/*
			 * match the filter against the dip
			 * If there is no match continue to next filter.
			 */
			get_filter_func(filterp->name, &filter_func);
			if (filter_func == NULL)
				continue;
			filter_match = (*filter_func)(filterp, dip);
			if (!filter_match)
				break;
		}
		if (!filter_match)
			continue;
		/*
		 * We come here if no filter was specified or
		 * filter matched the dip.
		 */
#ifdef	DEBUG
		if (filter_found && pciconf_debug) {
			filter_names[0] = '\0';
			for (i = 0, filterp = config_datap->filterp; filterp;
			    filterp = filterp->next, i++) {
				if (i)
					(void) snprintf(filter_names,
					    sizeof (filter_names),
					    "%s AND", filter_names);
				(void) snprintf(filter_names,
				    sizeof (filter_names),
				    "%s %s", filter_names, filterp->name);
			}
			PCICONF_DBG(
			    "filter_names %s matched for path %s\n",
			    filter_names,
			    path);
		}
#endif
		for (; actionp; actionp = actionp->next) {
			get_action_func(actionp->name, &action_func);
			if (action_func == NULL)
				continue;
			ret =  (*action_func)(actionp, dip);
			if ((strncmp(actionp->name, "compatible_action",
			    strlen(actionp->name)) == 0) &&
			    (ret == 0))
				compatible_property_modified = B_TRUE;
		}
	}
	/*
	 * If compatible_property was modified and the node is in BOUND
	 * state then we have to rebind.
	 */
	if ((compatible_property_modified) &&
	    (i_ddi_node_state(dip) == DS_BOUND)) {
		(void) rebind_dip(dip);
	}
	if (bus_p->sriov_cap_ptr &&
	    (!override_plat_config)) {
		uint_t		num_vf;
		ret = iovcfg_get_numvfs(path, &num_vf);
		if (ret)
			goto exit;
		bus_p->num_vf = num_vf;
		if (bus_p->num_vf == 0xffff)
			bus_p->num_vf = bus_p->initial_num_vf;
		if (bus_p->num_vf > bus_p->initial_num_vf) {
			cmn_err(CE_WARN,
	"num-vf %d for device %s exceeds %d. setting num-vf as 0\n",
			    bus_p->num_vf, path, bus_p->initial_num_vf);
			bus_p->num_vf = 0;
			goto exit;
		}
		PCICONF_DBG("num of VF defined by MD for %s is %d\n", path,
		    bus_p->num_vf);
		goto exit;
	}
exit:
	if (path)
		kmem_free(path, MAXPATHLEN + 1);
}
