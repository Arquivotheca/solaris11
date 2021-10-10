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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Scan device configuration information for allocable devices
 * and construct device_allocate(4) and device_maps(4) files.
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <stropts.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <libintl.h>
#include <syslog.h>
#include <libdevinfo.h>
#include <secdb.h>
#include <deflt.h>
#include <auth_list.h>
#include <dev_alloc.h>
#include <devalloc_impl.h>
#include <tsol/label.h>
#include <sys/sunddi.h>

#ifndef TEXT_DOMAIN
#define	TEXT_DOMAIN	"SUNW_OST_OSCMD"
#endif

#define	MKDEVALLOC	"mkdevalloc"
#define	MKDEVMAPS	"mkdevmaps"

#define	MODE_FILES	0x01
#define	MODE_DEVALLOC	0x02
#define	MODE_DEVMAPS	0x04

#define	SECLIB		"/etc/security/lib"
#define	TAPE_CLEAN	SECLIB"/st_clean"
#define	AUDIO_CLEAN	SECLIB"/audio_clean"
#define	CD_CLEAN	SECLIB"/sr_clean"
#define	FLOPPY_CLEAN	SECLIB"/fd_clean"

typedef struct linklist_node_s {
	void *data;
	struct linklist_node_s *next;
} linklist_node_t;

typedef struct {
	linklist_node_t *head;
	linklist_node_t *tail;
	void (*data_free)(void *);
} linklist_t;

/* data sources for devlink data */
typedef enum {
	DEVLINK_ORIGIN_DEVINFO,		/* provided by libdevinfo */
	DEVLINK_ORIGIN_DEVALLOC		/* added internally by mkdevalloc */
} devlink_origin_t;

typedef struct {
	devlink_origin_t origin;
	int type;
	char *path;
} devlink_data_t;

/* list(s) of directories to search for missing devlinks */
static const char *audio_link_dirs[] = {
	"/dev",
	"/dev/sound",
	NULL
};

/*
 * The system_labeled global variable, set in main, is used instead
 * of the is_system_labeled() function throughout this program.
 */
static int system_labeled;

/*
 * The progname global variable, set in main, is used to construct
 * error messages throughout this program.
 */
static char *progname;

/*
 * linklist_init
 *
 * Arguments:
 *   data_free:
 *     If the data stored in list nodes is itself a nested data structure,
 *     this is a pointer to the function that frees memory associated with
 *     that structure.  If the data stored in list nodes does not need to
 *     be freed, this should be NULL.
 *
 * Returns:
 *   A pointer to the new linked list header structure, or NULL in case of
 *   malloc failure.
 */
static linklist_t *
linklist_init(void (*data_free)(void *))
{
	linklist_t *list;

	if ((list = malloc(sizeof (linklist_t))) == NULL)
		return (NULL);

	list->head = NULL;
	list->tail = NULL;
	list->data_free = data_free;

	return (list);
}

/*
 * linklist_append
 *
 * Arguments:
 *   list:  Pointer to existing linked list header structure.
 *   data:  Pointer to data element to be appended.
 *
 * Returns:
 *   A pointer to the new list node, or NULL in case of malloc failure.
 */
static linklist_node_t *
linklist_append(linklist_t *list, void *data)
{
	linklist_node_t *node;

	if ((node = malloc(sizeof (linklist_node_t))) == NULL)
		return (NULL);

	node->data = data;
	node->next = NULL;
	if (list->head == NULL) {
		list->head = node;
		list->tail = node;
	} else {
		list->tail->next = node;
		list->tail = node;
	}

	return (node);
}

/*
 * linklist_search
 *
 * Arguments:
 *   list:    The linked list to search.
 *   key:     The data to search for.
 *   compar:  A comparison function.
 *
 * The function prototype and return value of compar is modeled after
 * the comparison functions used by tsearch(3C), qsort(3C), etc.
 *
 * When called by this function, the first argument to compar is the
 * key argument of this function, and the second argument to compar
 * is the list data element being examined.  The types of the two
 * arguments need not match.  This allows compar functions to be
 * written that search linked lists of structured data by passing
 * only a simple value as the search key.
 *
 * Returns:
 *   A pointer to the matching list data element, or NULL in case there
 *   is no matching element.
 */
static void *
linklist_search(const linklist_t *list, const void *key,
    int (*compar)(const void *, const void *))
{
	linklist_node_t *node;

	if (list != NULL) {
		node = list->head;
		while (node != NULL) {
			if (compar(key, node->data) == 0)
				return (node->data);
			node = node->next;
		}
	}
	return (NULL);
}

/*
 * linklist_join
 *
 * Data elements from linked lists a and b are joined to form a new list.
 * The lists must have been initialized to use the same data_free function.
 *
 * Arguments:
 *   a:  Pointer to existing linked list header structure.
 *   b:  Pointer to existing linked list header structure.
 *
 * Returns:
 *   On success, returns a pointer to a new linked list header structure,
 *   and the header structures of lists a and b are freed.
 *   Even if both lists are empty, an empty joined list will be returned.
 *
 *   On failure, returns NULL and lists a and b are unchanged.
 */
static linklist_t *
linklist_join(linklist_t *a, linklist_t *b)
{
	linklist_t *list;

	if (a->data_free != b->data_free)
		return (NULL);

	if ((list = malloc(sizeof (linklist_t))) == NULL)
		return (NULL);

	list->data_free = a->data_free;
	if (a->head != NULL)
		list->head = a->head;
	else
		list->head = b->head;

	if (b->tail != NULL)
		list->tail = b->tail;
	else
		list->tail = a->tail;

	if (a->tail != NULL)
		a->tail->next = b->head;
	free(a);
	free(b);

	return (list);
}

/*
 * linklist_free
 *
 * Frees a linked list header, all data nodes, and all data stored
 * in the list.
 *
 * Arguments:
 *   list:  Pointer to existing linked list header structure.
 */
static void
linklist_free(linklist_t *list)
{
	linklist_node_t *head;
	linklist_node_t *node;

	if (list != NULL) {
		head = list->head;
		while (head != NULL) {
			node = head;
			head = head->next;
			if (list->data_free != NULL)
				list->data_free(node->data);
			free(node);
		}
		free(list);
	}
}

/*
 * fatal_exit
 *
 * Emit the message corresponding to the current errno and exit.
 */
static void
fatal_exit()
{
	syslog(LOG_ERR, "%m");
	(void) fprintf(stderr, "%s: %s\n", progname, strerror(errno));
	exit(1);
}

/*
 * lookup_devname
 *
 * Arguments:
 *   alloc_type:  One of the following constant values:
 *     DA_AUDIO, DA_CD, DA_FLOPPY, DA_TAPE, DA_RMDISK
 *
 * Returns:
 *   A character string representing the base name for devices of type
 *   alloc_type, or NULL in case of error (invalid alloc_type).
 */
static const char *
lookup_devname(int alloc_type)
{
	switch (alloc_type) {
	case DA_AUDIO:
		return (DA_AUDIO_NAME);
	case DA_CD:
		return (system_labeled ? DA_CD_NAME : DA_CD_TYPE);
	case DA_FLOPPY:
		return (system_labeled ? DA_FLOPPY_NAME : DA_FLOPPY_TYPE);
	case DA_TAPE:
		return (system_labeled ? DA_TAPE_NAME : DA_TAPE_TYPE);
	case DA_RMDISK:
		return (system_labeled ? DA_RMDISK_NAME : NULL);
	default:
		return (NULL);
	}
}

/*
 * lookup_devtype
 *
 * Arguments:
 *   alloc_type:  One of the following constant values:
 *     DA_AUDIO, DA_CD, DA_FLOPPY, DA_TAPE, DA_RMDISK
 *
 * Returns:
 *   A character string representing the device type corresponding to the
 *   integer alloc_type, or NULL in case of error (invalid alloc_type).
 */
static const char *
lookup_devtype(int alloc_type)
{
	switch (alloc_type) {
	case DA_AUDIO:
		return (DA_AUDIO_TYPE);
	case DA_CD:
		return (DA_CD_TYPE);
	case DA_FLOPPY:
		return (DA_FLOPPY_TYPE);
	case DA_TAPE:
		return (DA_TAPE_TYPE);
	case DA_RMDISK:
		return (system_labeled ? DA_RMDISK_TYPE : NULL);
	default:
		return (NULL);
	}
}

/*
 * lookup_devclean
 *
 * Arguments:
 *   alloc_type:  One of the following constant values:
 *     DA_AUDIO, DA_CD, DA_FLOPPY, DA_TAPE, DA_RMDISK
 *
 * Returns:
 *   A character string containing the path of the clean script for devices
 *   of type alloc_type, or NULL in case of error (invalid alloc_type).
 *
 */
static const char *
lookup_devclean(int alloc_type)
{
	switch (alloc_type) {
	case DA_AUDIO:
		return (system_labeled ? DA_DEFAULT_AUDIO_CLEAN : AUDIO_CLEAN);
	case DA_CD:
		return (system_labeled ? DA_DEFAULT_DISK_CLEAN : CD_CLEAN);
	case DA_FLOPPY:
		return (system_labeled ? DA_DEFAULT_DISK_CLEAN : FLOPPY_CLEAN);
	case DA_TAPE:
		return (system_labeled ? DA_DEFAULT_TAPE_CLEAN : TAPE_CLEAN);
	case DA_RMDISK:
		return (system_labeled ? DA_DEFAULT_DISK_CLEAN : NULL);
	default:
		return (NULL);
	}
}

/*
 * detect_allocable_node
 *
 * Analyze the characteristics of one libdevinfo device node, and
 * determine whether or not the device is subject to device allocation,
 * and if it is, the type of device represented by the device node.
 *
 * For audio, CD, floppy, and tape devices, device allocation is always
 * applicable.  For disk devices, device allocation is only applicable
 * when the device contains one of the device properties "hotpluggable"
 * or "removable-media" and the system is a labeled system.
 *
 * Arguments:
 *   node:   libdevinfo device node for the device being examined.
 *   minor:  libdevinfo minor node for the device being examined.
 *
 * Returns:
 *   If the device is subject to device allocation, returns
 *   one of the following constant values:
 *     DA_AUDIO, DA_CD, DA_FLOPPY, DA_TAPE, DA_RMDISK
 *   If the device is not subject to device allocation, returns 0.
 */
static int
detect_allocable_node(di_node_t node, di_minor_t minor)
{
	char *nodetype;
	di_prop_t prop;
	char *prop_name;
	const char *prop_pluggable = "hotpluggable";
	const char *prop_removable = "removable-media";

	nodetype = di_minor_nodetype(minor);

	if (strcmp(nodetype, DDI_NT_AUDIO) == 0)
		return (DA_AUDIO);

	if (strcmp(nodetype, DDI_NT_CD) == 0 ||
	    strcmp(nodetype, DDI_NT_CD_CHAN) == 0)
		return (DA_CD);

	if (strcmp(nodetype, DDI_NT_FD) == 0)
		return (DA_FLOPPY);

	if (strcmp(nodetype, DDI_NT_TAPE) == 0)
		return (DA_TAPE);

	if (strcmp(nodetype, DDI_NT_BLOCK) == 0 ||
	    strcmp(nodetype, DDI_NT_BLOCK_CHAN) == 0)
		for (prop = di_prop_next(node, DI_PROP_NIL);
		    prop != DI_PROP_NIL;
		    prop = di_prop_next(node, prop))
			if (di_prop_type(prop) == DI_PROP_TYPE_BOOLEAN) {
				prop_name = di_prop_name(prop);
				if (strcmp(prop_name, prop_removable) == 0 ||
				    strcmp(prop_name, prop_pluggable) == 0)
					if (system_labeled)
						return (DA_RMDISK);
			}

	return (0);
}

/*
 * assign_instance
 *
 * Returns the next available instance number for a given device type.
 *
 * Arguments:
 *   alloc_type:  One of the following constant values:
 *     DA_AUDIO, DA_CD, DA_FLOPPY, DA_TAPE, DA_RMDISK
 *
 * Returns:
 *  The instance number assigned to the device, or -1 in case of error
 *  (instance number would exceed DA_MAX_DEVNO, or invalid alloc_type).
 */
static int
assign_instance(int alloc_type)
{
	static int next_audio	= 0;
	static int next_cd	= 0;
	static int next_floppy	= 0;
	static int next_tape	= 0;
	static int next_rmdisk	= 0;

	switch (alloc_type) {
	case DA_AUDIO:
		if ((long)next_audio <= DA_MAX_DEVNO)
			return (next_audio++);
		break;
	case DA_CD:
		if ((long)next_cd <= DA_MAX_DEVNO)
			return (next_cd++);
		break;
	case DA_FLOPPY:
		if ((long)next_floppy <= DA_MAX_DEVNO)
			return (next_floppy++);
		break;
	case DA_TAPE:
		if ((long)next_tape <= DA_MAX_DEVNO)
			return (next_tape++);
		break;
	case DA_RMDISK:
		if ((long)next_rmdisk <= DA_MAX_DEVNO)
			return (next_rmdisk++);
		break;
	}
	return (-1);
}

/*
 * assign_devname
 *
 * Given the type and instance number for an allocable device,
 * create a string containing the device name to be recorded
 * in the device_allocate(4) and device_maps(4) files.
 *
 * As a special case, on unlabeled systems, audio device instance 0
 * is assigned the name "audio" without including an instance number.
 *
 * Arguments:
 *   alloc_type:  One of the following constant values:
 *     DA_AUDIO, DA_CD, DA_FLOPPY, DA_TAPE, DA_RMDISK
 *   instance:  The instance number of the device.
 *
 * Returns:
 *   A newly allocated string containing the device name.
 */
static char *
assign_devname(int alloc_type, int instance)
{
	const char *tmpname;
	size_t devname_len;
	char *devname;

	tmpname = lookup_devname(alloc_type);

	if (!system_labeled && alloc_type == DA_AUDIO && instance == 0)
		devname_len = strlen(tmpname) + 1;
	else
		devname_len = strlen(tmpname) +
		    snprintf(NULL, 0, "%d", instance) + 1;

	if ((devname = malloc(devname_len)) == NULL)
		fatal_exit();

	if (!system_labeled && alloc_type == DA_AUDIO && instance == 0)
		(void) snprintf(devname, devname_len, "%s", tmpname);
	else
		(void) snprintf(devname, devname_len, "%s%d", tmpname,
		    instance);

	return (devname);
}

/*
 * devlink_data_free
 *
 * Free all memory associated with a devlink_data_t structure
 *
 * Arguments:
 *   devlink_data:  Pointer to the structure to be freed.
 */
static void
devlink_data_free(devlink_data_t *devlink_data)
{
	free(devlink_data->path);
	free(devlink_data);
}

/*
 * devlink_callback
 *
 * Arguments:
 *   devlink:  The devlink being visited during di_devlink_walk.
 *   arg:      The linked list to accumulate devlink data.
 *
 * Returns:
 *   DI_WALK_CONTINUE
 */
static int
devlink_callback(di_devlink_t devlink, void *arg)
{
	devlink_data_t *devlink_data;
	const char *path;

	if ((devlink_data = malloc(sizeof (devlink_data_t))) == NULL)
		fatal_exit();

	devlink_data->origin = DEVLINK_ORIGIN_DEVINFO;

	if ((devlink_data->type = di_devlink_type(devlink)) == -1)
		fatal_exit();

	if ((path = di_devlink_path(devlink)) == NULL)
		fatal_exit();
	if ((devlink_data->path = strdup(path)) == NULL)
		fatal_exit();

	if (linklist_append(arg, devlink_data) == NULL)
		fatal_exit();

	return (DI_WALK_CONTINUE);
}

/*
 * minor_step
 *
 * This function initiates the inner walk routine, which collects data
 * about devlinks associated with a single minor node associated with
 * a particular allocable device node.
 *
 * Arguments:
 *   minor:  The minor node being visited during walk_minors.
 *   arg:    The linked list to accumulate devlink data.
 */
static void
minor_step(di_minor_t minor, void *arg)
{
	di_devlink_handle_t hdl;
	char *mpath;

	if ((mpath = di_devfs_minor_path(minor)) == NULL)
		fatal_exit();

	if ((hdl = di_devlink_init(mpath, DI_MAKE_LINK)) == DI_LINK_NIL)
		fatal_exit();

	if (di_devlink_walk(hdl, NULL, mpath, 0, arg, devlink_callback) != 0)
		fatal_exit();

	if (di_devlink_fini(&hdl) != 0)
		fatal_exit();
}

/*
 * walk_minors
 *
 * This function initiates the middle walk routine, examining each
 * minor node associated with a particular allocable device node.
 *
 * The first minor node is already available, having been retrieved
 * during node_callback.
 *
 * Arguments:
 *   node:   The node being visited during node_callback.
 *   minor:  The first minor node to be visited during walk_minors.
 *
 * Returns:
 *   A pointer to a linked list of devlink_data_t structures, containing
 *   information about all devlinks associated with this device node.
 */
static linklist_t *
walk_minors(di_node_t node, di_minor_t minor)
{
	linklist_t *devlinks;

	if ((devlinks = linklist_init((void (*)(void *))devlink_data_free)) ==
	    NULL)
		fatal_exit();

	while (minor != DI_MINOR_NIL) {
		minor_step(minor, devlinks);
		minor = di_minor_next(node, minor);
	}

	return (devlinks);
}

/*
 * compar_devlink_path
 *
 * Arguments:
 *   path:  A path name.
 *   data:  The devlink being visited during a linked list search.
 *
 * Returns:
 *   An integer less than, equal to, or greater than 0, according to
 *   the relationship of path and the path element of the devlink.
 */
static int
compar_devlink_path(const char *path, const devlink_data_t *data)
{
	return (strcmp(path, data->path));
}

/*
 * compar_devlink_realpath
 *
 * Arguments:
 *   path:  A path name.
 *   data:  The devlink being visited during a linked list search.
 *
 * Returns:
 *   An integer less than, equal to, or greater than 0, according to
 *   the relationship of path and the path element of the devlink.
 *   Both are converted to the underlying realpaths before comparison.
 *
 * Note:
 *   Returns -1 in case of internal error; therefore this function
 *   should only be used for equality comparisons where such errors
 *   can silently be treated as inequality.
 */
static int
compar_devlink_realpath(const char *path, const devlink_data_t *data)
{
	char path_realpath[PATH_MAX];
	char data_realpath[PATH_MAX];

	if (realpath(path, path_realpath) == NULL)
		return (-1);
	if (realpath(data->path, data_realpath) == NULL)
		return (-1);

	return (strcmp(path_realpath, data_realpath));
}

/*
 * adjust_devlinks
 *
 * Arguments:
 *   devlinks:  A pointer to a linked list of devlink_data_t structures.
 *   alloc_type:  One of the following constant values:
 *     DA_AUDIO, DA_CD, DA_FLOPPY, DA_TAPE, DA_RMDISK
 *
 * Returns:
 *   On success, returns a pointer to a linked list of devlink_data_t
 *   structures containing the adjusted devlinks.  The original devlinks
 *   list is consumed during the call, and need not be freed afterwards.
 *   On failure, returns NULL and the original devlinks list is unchanged.
 *
 * Note:
 *   The devlink_data_t structures added to the devlinks list by this
 *   function will not have (or need) a proper libdevinfo 'type' value.
 *   This is OK because the devlinks list will be discarded once it is
 *   converted to a single string.
 */
static linklist_t *
adjust_devlinks(linklist_t *devlinks, int alloc_type)
{
	linklist_t *extra_devlinks;
	linklist_t *new_devlinks;
	devlink_data_t *devlink_data;
	const char **link_dirs;
	int link_dir_len;
	DIR *dirp;
	struct dirent *dire;
	int link_len;
	char *buf;
	int buf_size;
	int i;

	switch (alloc_type) {
	case DA_AUDIO:
		link_dirs = audio_link_dirs;
		break;
	default:
		/* no adjustment needed, return original list */
		return (devlinks);
	}

	if ((extra_devlinks = linklist_init(
	    (void (*)(void *))devlink_data_free)) == NULL)
		return (NULL);

	buf = NULL;
	buf_size = 0;
	for (i = 0; link_dirs[i] != NULL; i++) {

		link_dir_len = strlen(link_dirs[i]);

		if ((dirp = opendir(link_dirs[i])) == NULL) {
			linklist_free(extra_devlinks);
			return (NULL);
		}

		while (errno = 0, dire = readdir(dirp)) {

			/* make sure adequate buffer space is available */
			link_len = link_dir_len + 1 + strlen(dire->d_name);
			if (link_len >= buf_size) {
				buf_size = link_len + 1;
				if ((buf = realloc(buf, buf_size)) == NULL) {
					free(buf);
					linklist_free(extra_devlinks);
					return (NULL);
				}
			}

			/* create complete devlink path */
			if (snprintf(buf, buf_size, "%s/%s",
			    link_dirs[i], dire->d_name) != link_len) {
				free(buf);
				linklist_free(extra_devlinks);
				return (NULL);
			}

			/*
			 * if the exact path of this link is already
			 * in devlinks, skip it to avoid duplicates
			 */
			if (linklist_search(devlinks, buf,
			    (int (*)(const void *, const void *))
			    compar_devlink_path) != NULL)
				continue;

			/*
			 * if the realpath of this link matches the
			 * realpath of another link already in devlinks,
			 * this is a missing link; add it to devlinks
			 */
			if (linklist_search(devlinks, buf,
			    (int (*)(const void *, const void *))
			    compar_devlink_realpath) != NULL) {
				if ((devlink_data = malloc(
				    sizeof (devlink_data_t))) == NULL) {
					free(buf);
					linklist_free(extra_devlinks);
					return (NULL);
				}
				devlink_data->origin = DEVLINK_ORIGIN_DEVALLOC;
				devlink_data->path = buf;
				if (linklist_append(extra_devlinks,
				    devlink_data) == NULL) {
					devlink_data_free(devlink_data);
					linklist_free(extra_devlinks);
					return (NULL);
				}
				/*
				 * buf was attached to devlink_data->path
				 * so, force realloc on next iteration
				 */
				buf = NULL;
				buf_size = 0;
			}
		}
		if (errno != 0) {
			free(buf);
			linklist_free(extra_devlinks);
			return (NULL);
		}
		if (closedir(dirp) != 0) {
			free(buf);
			linklist_free(extra_devlinks);
			return (NULL);
		}
	}
	free(buf);

	if ((new_devlinks = linklist_join(extra_devlinks, devlinks)) == NULL) {
		linklist_free(extra_devlinks);
		return (NULL);
	}

	return (new_devlinks);
}

/*
 * process_devlinks
 *
 * Arguments:
 *   devlinks:  A pointer to a linked list of devlink_data_t structures.
 *   alloc_type:  One of the following constant values:
 *     DA_AUDIO, DA_CD, DA_FLOPPY, DA_TAPE, DA_RMDISK
 *
 * Returns:
 *   A newly allocated string containing all applicable devlinks found
 *   after processing the linked list from the devlinks argument.
 */
static char *
process_devlinks(linklist_t *devlinks, int alloc_type)
{
	linklist_node_t *node;
	devlink_data_t *data;
	const char *devlink;
	size_t devlink_len;
	char *devlist = NULL;
	size_t devlist_len = 0;
	size_t devlist_buflen = 0;
	char *devlist_new;

	for (node = devlinks->head; node != NULL; node = node->next) {
		data = node->data;
		if (!da_check_link(data->path))
			continue;
		if (!(alloc_type == DA_AUDIO ||
		    data->origin == DEVLINK_ORIGIN_DEVALLOC ||
		    (data->origin == DEVLINK_ORIGIN_DEVINFO &&
		    data->type == DI_PRIMARY_LINK)))
			continue;
		devlink = data->path;
		devlink_len = strlen(devlink);
		if (devlink_len == 0)
			continue;
		if ((devlist_len + devlink_len + 2) > devlist_buflen) {
			devlist_buflen = 2 * devlist_len + devlink_len + 2;
			if ((devlist_new = realloc(devlist, devlist_buflen)) ==
			    NULL)
				fatal_exit();
			devlist = devlist_new;
		}
		if (devlist_len > 0) {
			*(devlist + devlist_len) = ' ';
			devlist_len += 1;
		}
		(void) strcpy(devlist + devlist_len, devlink);
		devlist_len += devlink_len;
	}

	if (devlist_len > 0 && (devlist_len + 1) < devlist_buflen)
		if ((devlist_new = realloc(devlist, devlist_len + 1)) != NULL)
			devlist = devlist_new;

	if (devlist_len == 0) {
		if ((devlist = malloc(1)) == NULL)
			fatal_exit();
		*devlist = '\0';
	}

	return (devlist);
}

/*
 * devinfo_create
 *
 * Given a device node and minor node, construct a da_devinfo_t structure
 * containing data about the device.
 *
 * Arguments:
 *   node:   libdevinfo device node for the device being examined.
 *   minor:  libdevinfo minor node for the device being examined.
 *   alloc_type:  One of the following constant values:
 *     DA_AUDIO, DA_CD, DA_FLOPPY, DA_TAPE, DA_RMDISK
 *
 * Returns:
 *   A pointer to a new da_devinfo_t structure representing data for one
 *   allocable device, or NULL in case of error.
 */
static da_devinfo_t *
devinfo_create(di_node_t node, di_minor_t minor, int alloc_type)
{
	linklist_t *devlinks;
	linklist_t *new_devlinks;
	const char *devtype;
	const char *devexec;
	da_devinfo_t *devinfo;
	da_defs_t *da_defs;
	const char *msg;

	if ((devinfo = malloc(sizeof (da_devinfo_t))) == NULL)
		fatal_exit();

	devtype = lookup_devtype(alloc_type);
	if ((devinfo->devtype = strdup(devtype)) == NULL)
		fatal_exit();

	if ((devinfo->instance = assign_instance(alloc_type)) == -1) {
		msg = "cannot assign instance number for a device with type %s";
		syslog(LOG_WARNING, msg, devinfo->devtype);
		(void) fprintf(stderr, "%s: ", progname);
		(void) fprintf(stderr, gettext(msg), devinfo->devtype);
		(void) fprintf(stderr, "\n");
		free(devinfo->devtype);
		free(devinfo);
		return (NULL);
	}

	devinfo->devname = assign_devname(alloc_type, devinfo->instance);

	devinfo->devauths = NULL;
	devinfo->devexec = NULL;
	devinfo->devopts = NULL;

	/* The following code is modeled after libdevalloc::da_add_list() */
	if (system_labeled) {
		kva_t *kva;
		char *kval, *minstr, *maxstr;
		int nlen;
		/*
		 * Look for default label range, authorizations and cleaning
		 * program in devalloc_defaults. If label range is not
		 * specified in devalloc_defaults, assume it to be admin_low
		 * to admin_high.
		 */
		minstr = DA_DEFAULT_MIN;
		maxstr = DA_DEFAULT_MAX;
		setdadefent();
		if (da_defs = getdadeftype(devinfo->devtype)) {
			kva = da_defs->devopts;
			if ((kval = kva_match(kva, DAOPT_MINLABEL)) != NULL)
				minstr = kval;
			if ((kval = kva_match(kva, DAOPT_MAXLABEL)) != NULL)
				maxstr = kval;
			if ((kval = kva_match(kva, DAOPT_AUTHS)) != NULL)
				if ((devinfo->devauths = strdup(kval)) == NULL)
					fatal_exit();
			if ((kval = kva_match(kva, DAOPT_CSCRIPT)) != NULL)
				if ((devinfo->devexec = strdup(kval)) == NULL)
					fatal_exit();
			nlen = strlen(DAOPT_MINLABEL) + strlen(KV_ASSIGN) +
			    strlen(minstr) + strlen(KV_TOKEN_DELIMIT) +
			    strlen(DAOPT_MAXLABEL) + strlen(KV_ASSIGN) +
			    strlen(maxstr) + 1;
			if ((devinfo->devopts = malloc(nlen)) == NULL)
				fatal_exit();
			(void) snprintf(devinfo->devopts, nlen,
			    "%s%s%s%s%s%s%s",
			    DAOPT_MINLABEL, KV_ASSIGN, minstr, KV_TOKEN_DELIMIT,
			    DAOPT_MAXLABEL, KV_ASSIGN, maxstr);
			freedadefent(da_defs);
		}
		enddadefent();
	}

	if (devinfo->devauths == NULL)
		if ((devinfo->devauths = strdup(DEFAULT_DEV_ALLOC_AUTH)) ==
		    NULL)
			fatal_exit();

	if (devinfo->devexec == NULL) {
		devexec = lookup_devclean(alloc_type);
		if ((devinfo->devexec = strdup(devexec)) == NULL)
			fatal_exit();
	}

	devlinks = walk_minors(node, minor);
	if ((new_devlinks = adjust_devlinks(devlinks, alloc_type)) == NULL) {
		msg = gettext("cannot adjust devlinks for device %s");
		syslog(LOG_WARNING, msg, devinfo->devname);
		(void) fprintf(stderr, "%s: ", progname);
		(void) fprintf(stderr, gettext(msg), devinfo->devname);
		(void) fprintf(stderr, "\n");
	} else {
		devlinks = new_devlinks;
	}
	devinfo->devlist = process_devlinks(devlinks, alloc_type);
	linklist_free(devlinks);

	return (devinfo);
}

/*
 * devinfo_free
 *
 * Frees a da_devinfo_t structure and all attached strings.
 *
 * Arguments:
 *   devinfo:  Pointer to existing da_devinfo_t structure.
 */
static void
devinfo_free(da_devinfo_t *devinfo)
{
	free(devinfo->devname);
	free(devinfo->devtype);
	free(devinfo->devauths);
	free(devinfo->devexec);
	free(devinfo->devopts);
	free(devinfo->devlist);
	free(devinfo);
}

/*
 * devinfo_print_devalloc
 *
 * Print a da_devinfo_t structure on stdout in the format of device_allocate(4).
 *
 * Arguments:
 *   devinfo:  The da_devinfo_t structure to be printed.
 */
static void
devinfo_print_devalloc(da_devinfo_t *devinfo)
{
	const char *fmt;
	char *devopts;
	size_t i, j;

	if (system_labeled && (devinfo->devopts != NULL)) {
		for (i = 0, j = 0; devinfo->devopts[i] != '\0'; i++, j++)
			if (devinfo->devopts[i] == ':')
				j += 3;
		if ((devopts = malloc(j + 1)) == NULL)
			fatal_exit();
		for (i = 0, j = 0; devinfo->devopts[i] != '\0'; i++, j++)
			if ((devopts[j] = devinfo->devopts[i]) == ':') {
				devopts[++j] = '\\';
				devopts[++j] = '\n';
				devopts[++j] = '\t';
			}
		devopts[j] = '\0';
	} else {
		if ((devopts = strdup(DA_RESERVED)) == NULL)
			fatal_exit();
	}

	fmt = system_labeled ?
	    "%s%s\\\n\t%s%s\\\n\t%s%s\\\n\t%s%s\\\n\t%s%s\\\n\t%s\n\n" :
	    "%s%s%s%s%s%s%s%s%s%s%s\n";
	if (printf(fmt,
	    devinfo->devname, KV_DELIMITER,
	    devinfo->devtype, KV_DELIMITER,
	    devopts, KV_DELIMITER,
	    DA_RESERVED, KV_DELIMITER,
	    devinfo->devauths, KV_DELIMITER,
	    devinfo->devexec) < 0) {
		fatal_exit();
	}

	free(devopts);
}

/*
 * devinfo_print_devmaps
 *
 * Print a da_devinfo_t structure on stdout in the format of device_maps(4).
 *
 * Arguments:
 *   devinfo:  The da_devinfo_t structure to be printed.
 */
static void
devinfo_print_devmaps(da_devinfo_t *devinfo)
{
	if (printf("%s%s\\\n\t%s%s\\\n\t%s\n\n",
	    devinfo->devname, KV_TOKEN_DELIMIT,
	    devinfo->devtype, KV_TOKEN_DELIMIT,
	    devinfo->devlist) < 0)
		fatal_exit();
}

/*
 * devinfo_output
 *
 * Emits data from a da_devinfo_t structure in the desired format and location:
 * MODE_FILES:     writes data into device_allocate(4) and device_maps(4).
 * MODE_DEVALLOC:  writes data to stdout in device_allocate(4) format.
 * MODE_DEVMAPS:   writes data to stdout in device_maps(4) format.
 *
 * Arguments:
 *   devinfo:   The da_devinfo_t structure to be recorded.
 *   progmode:  One of the constant values:
 *     MODE_FILES, MODE_DEVALLOC, MODE_DEVMAPS
 */
static void
devinfo_output(da_devinfo_t *devinfo, int progmode)
{
	da_args dargs;
	const char *msg;

	if (progmode & MODE_FILES) {
		dargs.optflag = DA_ADD;
		dargs.rootdir = NULL;
		dargs.devnames = NULL;
		dargs.devinfo = devinfo;
		if (da_update_device(&dargs) != 0) {
			msg = "cannot store configuration for device %s";
			syslog(LOG_DEBUG, msg, devinfo->devname);
			(void) fprintf(stderr, "%s: ", progname);
			(void) fprintf(stderr, gettext(msg), devinfo->devname);
			(void) fprintf(stderr, "\n");
		}
	} else {
		if (progmode & MODE_DEVALLOC)
			devinfo_print_devalloc(devinfo);
		else if (progmode & MODE_DEVMAPS)
			devinfo_print_devmaps(devinfo);
	}
}

/*
 * node_callback
 *
 * This function is called once as each device node is visited.
 * If the device node is determined to be of an allocable device
 * type, data about the device is accumulated in a linked list
 * of da_devinfo_t structures.
 *
 * Arguments:
 *   node:  Device node being visited during di_walk_node.
 *   arg:   Pointer to linked list to accumulate da_devinfo_t structures.
 *
 * Returns:
 *   DI_WALK_CONTINUE
 */
static int
node_callback(di_node_t node, void *arg)
{
	di_minor_t minor;
	int alloc_type;
	da_devinfo_t *devinfo;
	char *devpath;
	const char *msg;

	if ((minor = di_minor_next(node, DI_MINOR_NIL)) == DI_MINOR_NIL) {
		if (errno != ENXIO)
			fatal_exit();
		return (DI_WALK_CONTINUE);
	}
	if ((alloc_type = detect_allocable_node(node, minor)) != 0) {
		devinfo = devinfo_create(node, minor, alloc_type);
		if (devinfo == NULL) {
			if ((devpath = di_devfs_path(node)) == NULL)
				fatal_exit();
			msg = "cannot create configuration for devfs node %s";
			syslog(LOG_WARNING, msg, devpath);
			(void) fprintf(stderr, "%s: ", progname);
			(void) fprintf(stderr, gettext(msg), devpath);
			(void) fprintf(stderr, "\n");
			return (DI_WALK_CONTINUE);
		}
		if (linklist_append(arg, devinfo) == NULL)
			fatal_exit();
	}
	return (DI_WALK_CONTINUE);
}

/*
 * walk_nodes
 *
 * This function initiates the outer walk routine, visiting each device
 * node in the system and collecting information about allocable devices.
 *
 * Returns:
 *   A pointer to a linked list of da_devinfo_t structures.
 *
 */
static linklist_t *
walk_nodes()
{
	linklist_t *devices;
	di_node_t root;

	if ((devices = linklist_init((void (*)(void *))devinfo_free)) == NULL)
		fatal_exit();

	if ((root = di_init("/", DINFOCPYALL)) == DI_NODE_NIL)
		fatal_exit();

	if (di_walk_node(root, DI_WALK_CLDFIRST, devices, node_callback) != 0)
		fatal_exit();

	di_fini(root);

	return (devices);
}

/*
 * main
 *
 * When invoked as 'mkdevalloc', write device data to stdout
 * in device_allocate(4) format.
 *
 * When invoked as 'mkdevmaps', write device data to stdout
 * in device_maps(4) format.
 *
 * On labeled systems only, when invoked as 'mkdevalloc system_labeled',
 * write device data directly into the device_allocate(4) and
 * device_maps(4) files.
 *
 * Returns:
 *   0:  Successful execution.
 *   1:  A fatal error occurred.
 */
int
main(int argc, char *argv[])
{
	int progmode;
	linklist_t *devices;
	linklist_node_t *node;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if ((progname = strrchr(argv[0], '/')) == NULL)
		progname = argv[0];
	else
		progname++;

	progmode = 0;
	if (strcmp(progname, MKDEVALLOC) == 0)
		progmode |= MODE_DEVALLOC;
	else if (strcmp(progname, MKDEVMAPS) == 0)
		progmode |= MODE_DEVMAPS;
	else
		exit(1);

	system_labeled = is_system_labeled();

	if (!system_labeled) {
		/*
		 * is_system_labeled() will return false in case we are
		 * starting before the first reboot after Trusted Extensions
		 * is enabled.  Check the setting in /etc/system to see if
		 * TX is enabled (even if not yet booted).
		 */
		if (defopen("/etc/system") == 0) {
			if (defread("set sys_labeling=1") != NULL)
				system_labeled = 1;

			/* close defaults file */
			(void) defopen(NULL);
		}
	}

#ifdef DEBUG
	/* test hook: see also devfsadm.c and allocate.c */
	if (!system_labeled) {
		struct stat	tx_stat;

		system_labeled = is_system_labeled_debug(&tx_stat);
		if (system_labeled) {
			fprintf(stderr, "/ALLOCATE_FORCE_LABEL is set,\n"
			    "forcing system label on for testing...\n");
		}
	}
#endif

	if (system_labeled && (progmode & MODE_DEVALLOC) && (argc == 2) &&
	    (strcmp(argv[1], DA_IS_LABELED) == 0)) {
		/*
		 * write device entries to device_allocate and device_maps.
		 * default is to print them on stdout.
		 */
		progmode |= MODE_FILES;
	}

	devices = walk_nodes();
	for (node = devices->head; node != NULL; node = node->next)
		devinfo_output(node->data, progmode);
	linklist_free(devices);

	return (0);
}
