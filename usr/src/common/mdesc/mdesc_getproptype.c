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

#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <strings.h>
#endif

#include <sys/mdesc.h>
#include <sys/mdesc_impl.h>

/*
 * Given a node and a property name (curprop) in the node, find the next
 * property in the node; return its name and type in the buffers provided by
 * the caller. If the curprop provided is NULL, find and return the first
 * property.
 * Input:
 * 	ptr:		MD snapshot
 *	node:		MD node to lookup the prop
 *	curprop:	name of the current property
 *	len:		length of the buffer provided for nextprop
 * Returns:
 * 	nextprop:	name of the next property
 * 	rv:		0 on Success; -1 on faiulre
 */
int
md_get_next_prop_type(md_t *ptr, mde_cookie_t node, char *curprop,
    uint_t len, char *nextprop, uint8_t *type)
{
	md_impl_t		*mdp;
	md_element_t		*mdep;
	mde_str_cookie_t	curprop_name;
	char			*p;
	int			idx;
	boolean_t		found_curprop = B_FALSE;

	mdp = (md_impl_t *)ptr;

	if (mdp == NULL || nextprop == NULL ||
	    type == NULL || node == MDE_INVAL_ELEM_COOKIE) {
		return (-1);
	}

	idx = (int)node;
	mdep = &(mdp->mdep[idx]);

	/* Skip over any empty elements */
	while (MDE_TAG(mdep) == MDET_NULL) {
		idx++;
		mdep++;
	}

	/* see if cookie is infact a node */
	if (MDE_TAG(mdep) != MDET_NODE) {
		return (-1);
	}

	/* Find and return the first PROP_VAL element, if curprop is NULL */
	if (curprop == NULL) {
		found_curprop = B_TRUE;
	} else {
		curprop_name = md_find_name(ptr, curprop);
		if (curprop_name == MDE_INVAL_STR_COOKIE) {
			return (-1);
		}
	}

	/*
	 * Simply walk the elements in the node looking for a property with a
	 * name that matches the given curprop. Once that is found, continue
	 * to look for the next prop element and return its name and type to
	 * the caller.
	 */
	for (idx++, mdep++; MDE_TAG(mdep) != MDET_NODE_END; idx++, mdep++) {
		/* Found a property element */
		if (found_curprop == B_TRUE) {
			/*
			 * We've already found curprop; so this is
			 * the next prop that we need; stop searching.
			 */
			break;
		}
		if (MDE_NAME(mdep) == curprop_name) {
			/*
			 * Mark that we found the curprop specified. We
			 * will stop at the next PROP_VAL prop that we
			 * find.
			 */
			found_curprop = B_TRUE;
		}
	}

	if (MDE_TAG(mdep) == MDET_NODE_END) {
		return (-1);	/* no such property name */
	}

	/* Now find the name of this property in the name block */
	idx = MDE_NAME(mdep);
	p = &(mdp->namep[idx]);
	if (len < (strlen(p) + 1)) {
		return (-1);
	}

	/* Return the propname */
	(void) strncpy(nextprop, p, strlen(p) + 1);

	/* Return type */
	*type = MDE_TAG(mdep);

	return (0);
}
