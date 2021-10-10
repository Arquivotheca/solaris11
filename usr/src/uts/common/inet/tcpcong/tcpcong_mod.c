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

#include <sys/sysmacros.h>
#include <sys/atomic.h>
#include <sys/strsubr.h>
#include <inet/tcpcong.h>
#include <sys/cmn_err.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/sdt.h>

#define	TCPCONG_MOD_DIR	"tcpcong"

list_t		tcpcong_mod_list;
kmutex_t	tcpcong_mod_list_lock;

void
tcpcong_ddi_g_init()
{
	list_create(&tcpcong_mod_list, sizeof (tcpcong_mod_t),
	    offsetof(tcpcong_mod_t, cm_list));
	mutex_init(&tcpcong_mod_list_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
tcpcong_ddi_g_destroy()
{
	list_destroy(&tcpcong_mod_list);
	mutex_destroy(&tcpcong_mod_list_lock);
}

static tcpcong_mod_t *
tcpcong_mod_find(const char *modname)
{
	tcpcong_mod_t	*modp;

	ASSERT(MUTEX_HELD(&tcpcong_mod_list_lock));

	for (modp = list_head(&tcpcong_mod_list); modp != NULL;
	    modp = list_next(&tcpcong_mod_list, modp)) {
		if (strcmp(modp->cm_ops->co_name, modname) == 0) {
			return (modp);
		}
	}
	return (NULL);
}

int
tcpcong_mod_register(tcpcong_ops_t *ops)
{
	tcpcong_mod_t	*modp;

	if (ops->co_version != TCPCONG_VERSION) {
		cmn_err(CE_WARN, "Failed to register tcpcong module %s: "
		    "version mismatch %d %d", ops->co_name,
		    ops->co_version, TCPCONG_VERSION);
		return (EINVAL);
	}

	modp = kmem_zalloc(sizeof (tcpcong_mod_t), KM_SLEEP);
	modp->cm_ops = ops;

	mutex_enter(&tcpcong_mod_list_lock);
	if (tcpcong_mod_find(ops->co_name) != NULL) {
		mutex_exit(&tcpcong_mod_list_lock);
		kmem_free(modp, sizeof (tcpcong_mod_t));
		return (EEXIST);
	}
	list_insert_tail(&tcpcong_mod_list, modp);
	mutex_exit(&tcpcong_mod_list_lock);

	return (0);
}

int
tcpcong_mod_unregister(tcpcong_ops_t *ops)
{
	tcpcong_mod_t	*modp;

	mutex_enter(&tcpcong_mod_list_lock);
	if ((modp = tcpcong_mod_find(ops->co_name)) != NULL) {
		if (modp->cm_refcnt != 0) {
			mutex_exit(&tcpcong_mod_list_lock);
			return (EBUSY);
		} else {
			list_remove(&tcpcong_mod_list, modp);
			kmem_free(modp, sizeof (tcpcong_mod_t));
			mutex_exit(&tcpcong_mod_list_lock);
			return (0);
		}
	}
	mutex_exit(&tcpcong_mod_list_lock);

	return (ENXIO);
}

/*
 * Lookup module by name, loaded if not loaded yet.
 * Increase reference count.
 * Return a handle for subsequent tcpcong_undef().
 */
tcpcong_handle_t
tcpcong_lookup(const char *modname, tcpcong_ops_t **opsp)
{
	tcpcong_mod_t	*modp;
	int		error;

again:
	mutex_enter(&tcpcong_mod_list_lock);
	if ((modp = tcpcong_mod_find(modname)) != NULL) {
		atomic_inc_uint(&modp->cm_refcnt);
		mutex_exit(&tcpcong_mod_list_lock);
		*opsp = modp->cm_ops;
		return ((tcpcong_handle_t)modp);
	}
	mutex_exit(&tcpcong_mod_list_lock);

	DTRACE_PROBE1(load__tcpcong__module, char *, modname);
	error = modload(TCPCONG_MOD_DIR, modname);
	if (error == -1) {
		cmn_err(CE_CONT, "modload of %s/%s failed",
		    TCPCONG_MOD_DIR, modname);
		return (NULL);
	}
	goto again;
}

/*
 * Decrease module reference count. Don't force unload: let the system decide,
 * which can be memory pressure on non-DEBUG or periodic modunload on DEBUG.
 */
void
tcpcong_unref(tcpcong_handle_t hdl)
{
	tcpcong_mod_t *modp = (tcpcong_mod_t *)hdl;

	ASSERT(modp->cm_refcnt != 0);
	atomic_dec_uint(&modp->cm_refcnt);
}
