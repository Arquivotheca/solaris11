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

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <libdevinfo.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/varargs.h>
#include <libsysevent.h>
#include "devchassisd_impl.h"

typedef	char	dntstr_t;	/* double null terminated string */

/* A tree of nodes that represents a set of path->link mappings */
typedef struct	node {
	struct node	*node_parent;	/* parent node */
	char		*node_name;	/* node name */
	char		*node_path;	/* path to node */
	char		*node_link;	/* -> link */
	int		node_tag;	/* NODE_TAG_* value */

	struct node	*node_sib;	/* sibling list */
	struct node	*node_child;	/* head child->sibling list */
} node_t;
#define	NODE_TAG_ALL		0	/* special walk-all tag */
#define	NODE_TAG_CREATE		1	/* tree node is new */
#define	NODE_TAG_VERIFY		2	/* No change to tree node */
#define	NODE_TAG_REMOVE		3	/* tree node is gone */
char	*node_tag_string[] = {"-ALL--", "CREATE", "-KEEP-", "REMOVE"};


sysevent_handle_t	*shp = NULL;
node_t			*devchassis_root = NULL;

static void		work_tree(node_t **rootp);
static int		clean_dir(char *path, int self, int level);

/* When daemon is not running, remove to prevent use of stale data */
static int
cro_cleanup()
{
	int		err = 0;

	/* Always remove /etc/chassis namespace */
	err |= clean_dir(DI_CRODC_DEVCHASSIS, 0, 0);

	return (err);
}

/*ARGSUSED*/
static void
cro_dbupdate_handler(sysevent_t *evp)
{
	work_tree(&devchassis_root);	/* do the work off sysevent */
}

int
cro_sysevent_receive_setup(void)
{
	const char		*esc[1];

	/* if we can't start clean, fail */
	if (cro_cleanup())
		return (1);

	if ((shp = sysevent_bind_handle(cro_dbupdate_handler)) == NULL)
		return (2);	/* failure */

	esc[0] = ESC_CRO_DBUPDATE;
	if (sysevent_subscribe_event(shp, EC_CRO, esc, 1))
		return (3);	/* failure */


	work_tree(&devchassis_root);		/* work once from setup */

	return (0);		/* success */
}

void
cro_sysevent_receive_teardown()
{
	(void) cro_cleanup();
}

/*
 * create str as double-null-terminated string with single-null-terminated
 * components (per separators).
 */
static dntstr_t *
dntstr_create(char *str, char *sep)
{
	dntstr_t	*dntstr;
	int		len;		/* of dntstr buffer */
	int		pcs;		/* previous char was separator */
	char		*s;
	dntstr_t	*ds;

	if (str == NULL)
		return (NULL);

	/* determine length of dntstr (skip leading separators) */
	for (len = 0, pcs = 0, s = str; *s; s++) {
		if (sep && strchr(sep, *s)) {
			if (len) {
				if (!pcs)
					len++;	/* end component */
				pcs++;
			}
		} else {
			len++;
			pcs = 0;
		}
	}

	/* allocate dntstr */
	dntstr = malloc(len + 2);	/* double null terminated */

	/* copy str over to dntstr (skip leading separators) */
	for (len = 0, pcs = 0, s = str, ds = dntstr; *s; s++) {
		if (sep && strchr(sep, *s)) {
			if (len) {
				if (!pcs) {
					*ds++ = 0;	/* end component */
					len++;
				}
				pcs++;
			}
		} else {
			*ds++ = *s;
			len++;
			pcs = 0;
		}
	}

	if (pcs == 0)
		*ds++ = 0;
	*ds++ = 0;		/* add double null termination */

	return (dntstr);
}

static void
dntstr_destroy(char *dntstr)
{
	free(dntstr);
}

/* determine if we are currently on the last component of a dntstr */
/*ARGSUSED*/
static int
dntstr_last_component(dntstr_t *dntstr, char *ds)
{
	int	lc;

	lc = *(ds + strlen(ds) + 1) ? 0 : 1;
	return (lc);
}

/* walk components of a dntstr, invoking callback on each component */
static void
dntstr_walk(char *dntstr, void *arg,
    void *(*callback)(dntstr_t *, char *ds, void *arg, void *argl))
{
	char	*ds = dntstr;
	void	*argl = NULL;

	while (*ds) {
		argl = (*callback)(dntstr, ds, arg, argl);
		ds += strlen(ds) + 1;
	}
}

/* form a new string from dntstr to (and including) dss component of dntstr */
static char *
dntstr_str(dntstr_t *dntstr, char *sep, char *dss)
{
	char	*ds = dntstr;
	int	len;
	char	*str;
	char	*s;

	if (dss < dntstr)
		return (NULL);

	/* 1 + at begining because we add back a leading separator */
	len = 1 + (dss - dntstr) + strlen(dss) + 1;
	s = str = malloc(len);

	while (*ds) {
		*s++ = *sep;
		s = stpcpy(s, ds);
		ds += strlen(ds) + 1;
		if (ds > dss)
			break;
	}
	return (str);
}


/*
 * Tree node manipulation routines
 */
struct _cbarg {
	node_t	**cbarg_rootp;
	char	*cbarg_link;
	char	*cbarg_sep;
};

/*
 * dntstr_walk callback that finds/builds the tree node
 * associated with a component of a path string.
 */
static void *
_path_comp_cb(dntstr_t *path, char *comp, void *arg, void *argl)
{
	node_t	**rootp = ((struct _cbarg *)arg)->cbarg_rootp;
	char	*link = ((struct _cbarg *)arg)->cbarg_link;
	char	*sep = ((struct _cbarg *)arg)->cbarg_sep;
	node_t	*parent = (node_t *)argl;
	node_t	*root;
	node_t	*child;

	/* find/allocate our root */
	if (*rootp == NULL) {
		root = calloc(1, sizeof (*root));
		root->node_name = strdup("");
		root->node_path = strdup("/");
		root->node_tag = NODE_TAG_VERIFY;
		*rootp = root;
	} else
		root = *rootp;

	/* ensure we have a parent */
	if (parent == NULL)
		parent = root;

	/* check for existing named child */
	for (child = parent->node_child; child; child = child->node_sib)
		if (strcmp(comp, child->node_name) == 0) {
			if (child->node_tag == NODE_TAG_REMOVE)
				child->node_tag = NODE_TAG_VERIFY;
			return (child);
		}

	/* create a new child */
	child = calloc(1, sizeof (*child));
	child->node_name = strdup(comp);
	child->node_parent = parent;
	child->node_path = dntstr_str(path, sep, comp);
	child->node_tag = NODE_TAG_CREATE;

	/* last component gets the link */
	if (dntstr_last_component(path, comp))
		child->node_link = link ? strdup(link) : NULL;
	else
		child->node_link = NULL;

	child->node_sib = parent->node_child;
	parent->node_child = child;
	return (child);
}

/* form a tree that represents a set of paths */
static node_t *
tree_path_build(node_t **rootp, dntstr_t *dntstr, char *sep, char *link)
{
	struct	_cbarg	cbarg;

	cbarg.cbarg_rootp = rootp;
	cbarg.cbarg_link = link;
	cbarg.cbarg_sep = sep;

	dntstr_walk(dntstr, &cbarg, _path_comp_cb);
	return (*rootp);
}

/* walk a tree, depth first, invoking callback on the way down */
static void
tree_walk_depth_cbdown(node_t *node, int tag, int level, void *arg,
    void (*callback)(node_t *, int level, void *arg))
{
	node_t	*child;

	if (node == NULL)
		return;

	/* if node->node_tag matches tag, invoke node_callback */
	if ((tag == NODE_TAG_ALL) || (tag == node->node_tag))
		(*callback)(node, level, arg);

	for (child = node->node_child; child; child = child->node_sib)
		tree_walk_depth_cbdown(child, tag, level + 1, arg, callback);
}

/* walk a tree, depth first, invoking callback on the way back up */
static void
tree_walk_depth_cbup(node_t *node, int tag, int level, void *arg,
    void (*callback)(node_t *, int level, void *arg))
{
	node_t	*child;

	if (node == NULL)
		return;

	for (child = node->node_child; child; child = child->node_sib)
		tree_walk_depth_cbup(child, tag, level + 1, arg, callback);

	/* if node->node_tag matches tag, invoke callback */
	if ((tag == NODE_TAG_ALL) || (tag == node->node_tag))
		(*callback)(node, level, arg);
}

/*ARGSUSED*/
static void
_tree_print_cb(node_t *node, int level, void *arg)
{
	int	i;

	for (i = 0; i < level + 1; i++)
		dprintf("   ");		/* indent by level */
	dprintf("%s %s", node_tag_string[node->node_tag], node->node_name);
	if (node->node_link)
		dprintf("-> %s", node->node_link);
	dprintf("\n");
}

/* print a tree (tests tree_walk_depth_cbdown) */
static void
tree_print(char *header, node_t *node)
{
	if (header)
		dprintf("%s\n", header);
	tree_walk_depth_cbdown(node, NODE_TAG_ALL, 1, NULL, _tree_print_cb);
	if (node)
		dprintf("\n");
}

/*ARGSUSED*/
static void
_tree_free_cb(node_t *node, int level, void *arg)
{
	node_t	*parent = node->node_parent;
	node_t	**pchild;
	node_t	*child;

	/* remove node from parent's list of children */
	if (parent) {
		for (pchild = &parent->node_child, child = *pchild; child;
		    pchild = &child->node_sib, child = *pchild) {
			if (child == node) {
				*pchild = child->node_sib;
				break;
			}
		}
	}

	if (node->node_name)
		free(node->node_name);
	if (node->node_link)
		free(node->node_link);
	if (node->node_path)
		free(node->node_path);
	free(node);
}

/* Free a tree */
static void
tree_free(node_t *node)
{
	tree_walk_depth_cbup(node, NODE_TAG_ALL, 0, NULL, _tree_free_cb);

}

/*ARGSUSED*/
static void
_tree_tag_cb(node_t *node, int level, void *arg)
{
	int	tag = (int)arg;

	/* Don't re-tag the root */
	if (node->node_parent)
		node->node_tag = tag;
}

/* Tag all nodes in a tree */
static void
tree_tag(node_t *node, int tag)
{
	tree_walk_depth_cbdown(node,
	    NODE_TAG_ALL, 0, (void *)tag, _tree_tag_cb);
}


/*
 * Management of filesystem links
 *
 * Remove everything below path and, possibly, self. Lstat path,
 * recurse down through subdirectories, remove on the way up.
 */
static int
clean_dir(char *path, int self, int level)
{
	DIR		*dirp;
	struct dirent	*direntp;
	struct stat	sbuf;
	int		len;
	char		*sdpath;
	int		rval = 0;

	/* Safety: keep cleandir contained below /dev/chassis */
	if (strncmp(DI_CRODC_DEVCHASSIS, path, strlen(DI_CRODC_DEVCHASSIS))) {
		dprintf("clean_dir: %d: out of control\n", level);
		return (-1);
	}

	dirp = opendir(path);
	if (dirp) {
		while ((direntp = readdir(dirp)) != NULL) {
			if ((strcmp(direntp->d_name, ".") == 0) ||
			    (strcmp(direntp->d_name, "..") == 0))
				continue;	/* skip "." and ".." */

			len = strlen(path) + 1 + strlen(direntp->d_name) + 1;
			sdpath = malloc(len);
			(void) snprintf(sdpath, len, "%s/%s",
			    path, direntp->d_name);
			if ((lstat(sdpath, &sbuf) == 0) &&
			    ((sbuf.st_mode & S_IFMT) == S_IFDIR))
				rval = clean_dir(sdpath, 1, level + 1);
			else {
				rval = remove(sdpath);
			}
			free(sdpath);
		}
		(void) closedir(dirp);
	}

	if (self && (rval == 0) && remove(path)) {
		dprintf("clean_dir: %d: remove failed: %s\n", level, path);
		rval = -1;
	}

	return (rval);
}

/*ARGSUSED*/
static void
delta_remove(node_t *node, int level, void *arg)
{
	(void) clean_dir(node->node_path, 1, level);
	tree_free(node);
}

/*ARGSUSED*/
static void
delta_create(node_t *node, int level, void *arg)
{
	struct stat	sbuf;
	static void	delta_validate(node_t *node, int level, void *arg);

	if (lstat(node->node_path, &sbuf) == 0) {
		delta_validate(node, -1, arg);
		return;
	}

	if (node->node_link) {
		if (symlink(node->node_link, node->node_path) != 0)
			dprintf("delta_create: %d: symlink failed\n", level);
	} else {
		if (mkdir(node->node_path,
		    S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) != 0)
			dprintf("delta_create: %d: mknod failed\n", level);
	}
}

/*
 * Ensure that the node_link matches the symbolic link in the
 * file system.  If it does not, update the symbolic link.
 */
/*ARGSUSED*/
static void
delta_validate(node_t *node, int level, void *arg)
{
	struct stat	sbuf;
	char		link[MAXPATHLEN + 1];
	ssize_t		slink;

	dprintf("delta_validate: %d: begin: %s\n", level, node->node_path);
	if (lstat(node->node_path, &sbuf)) {
		/* does not exist in the file system? */
		dprintf("delta_validate: %d: PROB: recreate\n", level);
		goto fix;
	}

	if (node->node_link) {
		if ((sbuf.st_mode & S_IFMT) != S_IFLNK) {
			dprintf("delta_validate: %d: PROB: not link\n", level);
			goto fix;
		} else {
			slink = readlink(node->node_path, link, MAXPATHLEN);
			if (slink == -1) {
				dprintf("delta_validate: %d: "
				    "PROB: readlink\n", level);
				goto fix;
			}
			link[slink] = 0;
			if (strcmp(node->node_link, link)) {
				dprintf("delta_validate: %d: "
				    "PROB: wrong link\n", level);
				goto fix;
			}
		}
	} else {
		if ((sbuf.st_mode & S_IFMT) != S_IFDIR) {
			dprintf("delta_validate: %d: not dir\n", level);
			goto fix;
		}
	}

	return;

fix:	(void)clean_dir(node->node_path, 1, level);
	delta_create(node, -1, arg);
}

static void
work_tree(node_t **rootp)
{
	di_cro_hdl_t	h;
	di_cro_reca_t	ra;
	di_cro_rec_t	r;
	char		*devchassis_path;
	char		*link;
	dntstr_t	*path;
	sysevent_id_t   eid;

	/* If we are continuing, everything is potentially NODE_TAG_REMOVE */
	if (*rootp)
		tree_tag(*rootp, NODE_TAG_REMOVE);

	/* Take a cro snapshot and process all records. */
	h = di_cro_init(NULL, 0);
	ra = di_cro_reca_create_query(h, 0, NULL);
	for (r = di_cro_reca_next(ra, NULL); r;
	    r = di_cro_reca_next(ra, r)) {

		/* Form 'raw' /dev/chassis namespace. */
		if (di_crodc_rec_linkinfo(h, r, 0,
		    DI_CRODC_REC_LINKINFO_RAW, &devchassis_path, &link)) {

			path = dntstr_create(devchassis_path, "/");
			*rootp = tree_path_build(rootp, path, "/", link);
			dntstr_destroy(path);

			if (devchassis_path)
				free(devchassis_path);
			if (link)
				free(link);

			/*
			 * Form <alias-id> symlink so 'standard' form
			 * resolves.
			 */
			if (di_crodc_rec_linkinfo(h, r, 0,
			    DI_CRODC_REC_LINKINFO_ALIASLINK,
			    &devchassis_path, &link)) {

				path = dntstr_create(devchassis_path, "/");
				*rootp = tree_path_build(rootp,
				    path, "/", link);
				dntstr_destroy(path);

				if (devchassis_path)
					free(devchassis_path);
				if (link)
					free(link);
			}

		}
	}
	di_cro_reca_destroy(ra);

	/* Make sure well-known SYS <alias-id> namespace is there */
	if (di_crodc_rec_linkinfo(h, NULL, 0,
	    DI_CRODC_REC_LINKINFO_RAWSYS, &devchassis_path, NULL)) {

		path = dntstr_create(devchassis_path, "/");
		*rootp = tree_path_build(rootp, path, "/", NULL);
		dntstr_destroy(path);

		if (devchassis_path)
			free(devchassis_path);

		if (di_crodc_rec_linkinfo(h, NULL, 0,
		    DI_CRODC_REC_LINKINFO_SYSLINK, &devchassis_path, &link)) {

			path = dntstr_create(devchassis_path, "/");
			*rootp = tree_path_build(rootp, path, "/", link);
			dntstr_destroy(path);

			if (devchassis_path)
				free(devchassis_path);
			if (link)
				free(link);
		}
	}

	di_cro_fini(h);

	/*
	 * If we have a tree, walk the tree, and perform the
	 * namespace operations associated with tagged deltas.
	 *
	 * It is more natural to process NODE_TAG_VERIFY, and
	 * NODE_TAG_CREATE depth first with callouts on the way down,
	 * and NODE_TAG_REMOVE depth first with callouts on the way up.
	 */
	if (*rootp) {
		if (debug_flag == B_TRUE)
			tree_print("work_tree: resolve", *rootp);

		tree_walk_depth_cbdown(*rootp,
		    NODE_TAG_CREATE, 0, NULL, delta_create);
		tree_walk_depth_cbdown(*rootp,
		    NODE_TAG_VERIFY, 0, NULL, delta_validate);
		tree_walk_depth_cbup(*rootp,
		    NODE_TAG_REMOVE, 0, NULL, delta_remove);
	}

	/* send EC_CRO completion event */
	(void) sysevent_post_event(EC_CRO, ESC_CRO_DBUPDATE_FINISH,
	    SUNW_VENDOR, "fmd", NULL, &eid);

}
