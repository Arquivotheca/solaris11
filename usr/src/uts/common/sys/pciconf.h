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

#ifndef _SYS_PCICONF_H
#define	_SYS_PCICONF_H

#include <sys/dditypes.h>
#include <sys/ddipropdefs.h>
#include <sys/nvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif


#define	VF_PREFIX		"VF["

#define	MAX_PCICONF_FILE_SIZE	65536
#define	MAX_SUB_NVLIST_DEPTH	3
#define	PCICONF_FAILURE		-1
#define	FOUND			0x20
#define	EXPECT			0x40
#define	MAX_IDS_IN_ID_FILTER	5
#define	ID_INDEX_MASK		0xf
#define	FOUND_RIGHT_SQUARE_BRACKET	(FOUND | 0x10)
#define	FILTER_NAME_LEN		80
#define	ACTION_NAME_LEN		80
#define	LABEL_LEN		80
#define	PARAM_LEN		80
#define	MAX_STRING_LEN		80
#define	MAX_ELEMS		80
#define	NUMERIC_LEN		40
#define	PATHNAME_LEN		128
#define	NUM_REGD		3
#define	NUM_DIGITS		10
#define	REGD_INDEX_MASK		0xf
#define	COMPATIBLE_NAME_LEN	80
#define	OPERATOR_MASK		0x3
#define	EQUAL_OPERATOR		0
#define	OR_OPERATOR		1
#define	AND_OPERATOR		2
#define	EXCLUSIVE_OR_OPERATOR	4
#define	MINUS_OPERATOR		1
#define	PLUS_OPERATOR		2
#define	FOUND_EQUAL_OPERATOR	(FOUND | EQUAL_OPERATOR)
#define	FOUND_MINUS_OPERATOR	(FOUND | MINUS_OPERATOR)
#define	FOUND_PLUS_OPERATOR	(FOUND | PLUS_OPERATOR)
#define	NUM_OF_COMPATIBLE_OPERATORS	3
#define	SYSTEM_CONFIG		0
#define	DEVICE_CONFIG		1
#define	NUM_CONFIGS		2
#define	EXCLUSIVE_OR_OPERATOR		4
#define	CONFIG_REG		1

#define	CONFIG_ID			regd[0]
#define	BAR_NUM				regd[0]
#define	REG_OFFSET			regd[1]
#define	REG_SIZE			regd[2]

typedef	nvlist_t	pci_param_list_t;

typedef	struct pci_param_hdl {
	dev_info_t		*dip;
	pci_param_list_t	*plist;		/* primary plist */
	nvlist_t		*scratch_list;
} pci_param_hdl_t;


struct	id {
	boolean_t	id_defined[MAX_IDS_IN_ID_FILTER];
	uint32_t	id_val[MAX_IDS_IN_ID_FILTER];
};

struct classcode {
	uint16_t	classcode;
	boolean_t	mask_defined;
	uint16_t	mask;
};

struct filter {
	struct filter	*next;
	char	name[FILTER_NAME_LEN + 1];
	union {
		char			path[PATHNAME_LEN +1];
		struct	classcode	classcode_info;
		struct	id		id_info;
		int			vf_num;
	} filter_info;
};

struct reg {
	uint32_t	regd[NUM_REGD];
	int		reg_type;
	u_longlong_t	value;
	int		assign_operator;
};

struct compatible {
	char	compatible_string[NUM_OF_COMPATIBLE_OPERATORS]
	    [COMPATIBLE_NAME_LEN +1];
};

struct action {
	struct	action			*next;
	char				name[ACTION_NAME_LEN + 1];
	union {
		uint16_t		num_vf;
		struct	compatible	compatible_info;
		struct reg		reg_info;
	} action_info;
};

struct	filter_parse_table {
	char		*name;
	struct filter	*(*f)(void *);
};

struct	action_parse_table {
	char		*name;
	struct action	*(*f)(void *);
};
typedef struct nvl_hierarchy {
	struct nvl_hierarchy	*child;
	struct nvl_hierarcy	*parent;
	nvlist_t		*nvl;
} nvl_hierarchy_t;

struct	config_data {
	struct config_data	*next;
	char			label[LABEL_LEN + 1];
	struct	filter		*filterp;
	void			*datap;
	nvlist_t		*current_nvl;
	nvl_hierarchy_t		nvl;
};

struct action_table {
	char	*name;
	int	(*f)(struct action *, dev_info_t *);
};

struct filter_table {
	char	*name;
	int	(*f)(struct filter *, dev_info_t *);
};

struct pciconf_data {
	struct	config_data	*config_datap[NUM_CONFIGS];
};

struct pciconf_spec {
	struct pciconf_spec	*pciconf_next;
	char			*pciconf_device_pathname;
	int			pciconf_num_vfs;
};

#define	PCICONF_FILE	"/etc/pci.conf"
extern struct pciconf_data pciconf_data_hd;
extern struct _buf *pciconf_file;
extern int64_t	pciconf_file_last_atime;
extern	int pciconf_parse_now(char *);
extern void clean_pciconf_data();
extern	void read_pciconf_file();
extern void apply_pciconf_configs(dev_info_t *);

#ifdef DEBUG
void pciconf_dbg(char *fmt, ...);
void parse_dbg(struct _buf *file, char *fmt, ...);

#define	PCICONF_DBG(...)				\
	do {						\
		if (pciconf_debug)			\
			pciconf_dbg(__VA_ARGS__);	\
	_NOTE(CONSTCOND) } while (0);
#define	PARSE_DBG(...)					\
	do {						\
		if (pciconf_parse_debug)		\
			parse_dbg(__VA_ARGS__);		\
	_NOTE(CONSTCOND) } while (0);
#else
#define	PCICONF_DBG(...)	if (0)  do { } while (0)
#define	PARSE_DBG(...)		if (0)  do { } while (0)
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PCICONF_H */
