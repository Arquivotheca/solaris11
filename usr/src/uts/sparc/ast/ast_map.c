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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include "ast.h"

static int ast_devmap_map(devmap_cookie_t, dev_t, uint_t, offset_t, size_t,
    void **);
/* LINTED */
static int ast_devmap_access(devmap_cookie_t, void *, offset_t, size_t,
    uint_t, uint_t);
static int ast_devmap_dup(devmap_cookie_t, void *, devmap_cookie_t, void **);
static void ast_devmap_unmap(devmap_cookie_t, void *, offset_t, size_t,
    devmap_cookie_t, void **, devmap_cookie_t, void **);

static struct devmap_callback_ctl ast_devmap_ops = {
	DEVMAP_OPS_REV,
	ast_devmap_map,
	NULL,
	ast_devmap_dup,
	ast_devmap_unmap
};

static ast_regspace_t *
ast_find_space(struct ast_softc *softc, offset_t off)
{
	int i;

	for (i = 0; i < 4; i++) {
		if ((off >= softc->regspace[i].global_offset) &&
		    (off < (softc->regspace[i].global_offset
		    + softc->regspace[i].size))) {
			return (&softc->regspace[i]);
		}
	}

	return (NULL);
}

static struct ast_context *
ast_ctx_create(struct ast_softc *softc, dev_t dev)
{
	struct ast_context	*ctx;

	ctx = kmem_zalloc(sizeof (struct ast_context), KM_SLEEP);

	if (ctx != NULL) {
		ctx->softc	= softc;
		ctx->dev	= dev;
		ctx->mappings	= NULL;
		ctx->next	= NULL;
		ctx->flags	= 0;
	}

	return (ctx);
}

/*
 * find the context for the device
 */
static struct ast_context *
ast_ctx_find(struct ast_softc *softc, dev_t dev)
{
	struct ast_context	*ctx;

	ctx = softc->contexts;

	for (ctx = softc->contexts; ctx != NULL; ctx = ctx->next) {
		if (ctx->dev == dev)
			return (ctx);
	}

	return (ctx);
}

/*
 * create a new mapping
 */
static struct ast_ctx_map *
ast_ctx_new_mapping(struct ast_softc *softc, struct ast_context *ctx,
			devmap_cookie_t dhp, offset_t offset, size_t len)
{
	struct ast_ctx_map *mi;

	if (ctx == NULL) {
		cmn_err(CE_WARN, "ast: new_mapping - ctx=NULL");
		return (NULL);
	}

	mi = kmem_zalloc(sizeof (struct ast_ctx_map), KM_SLEEP);

	mi->next = ctx->mappings;
	if (mi->next != NULL)
		mi->next->prev = mi;

	mi->prev	= NULL;
	mi->softc	= softc;
	mi->ctx		= ctx;
	mi->dhp		= dhp;
	mi->offset	= offset;
	mi->len		= len;

	ctx->mappings	= mi;

	return (mi);
}

/*
 * unload all mappings associated with the context
 */
static void
ast_ctx_unload_mappings(struct ast_softc *softc, struct ast_context *ctx)
{
	struct ast_ctx_map	*mi;
	int			rval;

	if (ctx->mappings == NULL)
		return;

	for (mi = ctx->mappings; mi != NULL; mi = mi->next) {
		rval = devmap_unload(mi->dhp, mi->offset, mi->len);
		if (rval != 0) {
			cmn_err(CE_WARN,
			    "ast: devmap_unload(%llx, %lx) return %d",
			    mi->offset, mi->len, rval);
		}
	}
}

void
ast_ctx_save(struct ast_softc *softc, struct ast_context *ctx)
{
	if ((softc->flags & AST_HW_INITIALIZED) == 0)
		return;

	ctx->cmd_setting = softc->read32(softc, CMD_SETTING);
	ctx->src_pitch	 = softc->read32(softc, SRC_PITCH);
	ctx->dst_pitch	 = softc->read32(softc, DST_PITCH);
	ctx->src_base	 = softc->read32(softc, SRC_BASE);
	ctx->dst_base	 = softc->read32(softc, DST_BASE);
	ctx->src_xy	 = softc->read32(softc, SRC_XY);
	ctx->dst_xy	 = softc->read32(softc, DST_XY);
	ctx->rect_xy	 = softc->read32(softc, RECT_XY);

	ctx->flags |= AST_CTX_SAVED;
}

void
ast_ctx_restore(struct ast_softc *softc, struct ast_context *ctx)
{
	if (ctx->flags & AST_CTX_SAVED) {
		softc->write32(softc, SRC_PITCH,   ctx->src_pitch);
		softc->write32(softc, DST_PITCH,   ctx->dst_pitch);
		softc->write32(softc, SRC_BASE,	   ctx->src_base);
		softc->write32(softc, DST_BASE,	   ctx->dst_base);
		softc->write32(softc, SRC_XY,	   ctx->src_xy);
		softc->write32(softc, DST_XY,	   ctx->dst_xy);
		softc->write32(softc, RECT_XY,	   ctx->rect_xy);
		softc->write32(softc, CMD_SETTING, ctx->cmd_setting);
	}
}

int
ast_ctx_make_current(struct ast_softc *softc, struct ast_context *ctx)
{
	struct ast_context	*old_ctx;

	if (softc->cur_ctx == ctx)
		return (0);

	old_ctx = softc->cur_ctx;

	/*
	 * unload the current context's mappings,
	 * save current context register setting
	 */
	if (old_ctx != NULL) {
		ast_ctx_unload_mappings(softc, old_ctx);
		ast_ctx_save(softc, old_ctx);
	}

	/*
	 * load new context register setting
	 */
	if (ctx != NULL) {
		ast_ctx_restore(softc, ctx);
	}

	softc->cur_ctx = ctx;

	return (0);
}

static int
ast_ctx_mgt(devmap_cookie_t dhp, void *pvtp, offset_t offset,
		size_t len, uint_t type, uint_t rw)
{
	struct ast_ctx_map	*mi;
	struct ast_softc	*softc;
	int			rval;

	mi = (struct ast_ctx_map *)pvtp;

	softc = mi->softc;

	mutex_enter(&softc->ctx_lock);
	mutex_enter(&softc->lock);

	rval = ast_ctx_make_current(softc, mi->ctx);

	if (rval == 0) {
		rval = devmap_default_access(dhp, pvtp, offset, len, type, rw);
	}

	mutex_exit(&softc->lock);
	mutex_exit(&softc->ctx_lock);

	return (rval);
}

static int
ast_devmap_map(devmap_cookie_t dhp, dev_t dev, uint_t flags,
		offset_t offset, size_t len, void **pvtp)
{
	struct ast_softc	*softc = ast_get_softc(getminor(dev));
	struct ast_context	*ctx;

	mutex_enter(&softc->ctx_lock);

	if (flags & MAP_SHARED) {

		ctx = softc->shared_ctx;
		if (ctx == NULL) {
			ctx = ast_ctx_create(softc, dev);
			if (ctx == NULL) {
				mutex_exit(&softc->ctx_lock);
				return (DDI_FAILURE);
			}
			ctx->flags = AST_CTX_SHARED;
			softc->shared_ctx = ctx;
		}
	} else {
		if ((ctx = ast_ctx_find(softc, dev)) == NULL) {
			ctx = ast_ctx_create(softc, dev);
			if (ctx == NULL) {
				mutex_exit(&softc->ctx_lock);
				return (DDI_FAILURE);
			}
			ctx->next = softc->contexts;
			softc->contexts = ctx;
		}
	}

	*pvtp = ast_ctx_new_mapping(softc, ctx, dhp, offset, len);

	devmap_set_ctx_timeout(dhp, (clock_t)drv_usectohz(10000));

	mutex_exit(&softc->ctx_lock);

	return (DDI_SUCCESS);
}

static int
ast_devmap_access(devmap_cookie_t dhp, void *pvtp, offset_t offset,
		size_t len, uint_t type, uint_t rw)
{
	int			rval;
	struct ast_ctx_map	*mi;
	struct ast_softc	*softc;

	if (pvtp == NULL) {
		rval = devmap_default_access(dhp, pvtp, offset, len, type, rw);
		return (rval);
	}

	mi = (struct ast_ctx_map *)pvtp;

	softc = mi->softc;

	/*
	 * if this mapping is not associated with a context, or that
	 * context is already current, simply validate the mapping
	 */
	if ((mi->ctx == NULL) || (mi->ctx == softc->cur_ctx)) {
		rval = devmap_default_access(dhp, pvtp, offset, len, type, rw);
		return (rval);
	}

	/*
	 * context switch
	 */
	rval = devmap_do_ctxmgt(dhp, pvtp, offset, len, type, rw, ast_ctx_mgt);

	return (rval);
}

static int
ast_devmap_dup(devmap_cookie_t dhp, void *pvtp,
		devmap_cookie_t new_dhp, void **new_pvtp)
{
	struct ast_ctx_map	*mi = (struct ast_ctx_map *)pvtp;
	struct ast_softc	*softc;

	if ((mi == NULL) || (mi->dhp != dhp)) {
		*new_pvtp = NULL;
		return (0);
	}

	softc = mi->softc;

	mutex_enter(&softc->ctx_lock);

	/*
	 * We do not create a new context. Since the parent and child
	 * processes share one minordev structure, they share the context
	 */
	if (mi->ctx == NULL)
		cmn_err(CE_WARN, "dup: ctx=NULL mi=0x%p", (void *)mi);

	*new_pvtp = ast_ctx_new_mapping(softc, mi->ctx, new_dhp,
	    mi->offset, mi->len);

	mutex_exit(&softc->ctx_lock);

	return (0);
}

static void
ast_devmap_unmap(devmap_cookie_t dhp, void *pvtp, offset_t offset, size_t len,
		devmap_cookie_t new_dhp1, void **new_pvtp1,
		devmap_cookie_t new_dhp2, void **new_pvtp2)
{
	struct ast_ctx_map	*mi = (struct ast_ctx_map *)pvtp;
	struct ast_softc	*softc;
	struct ast_context	*ctx, *c;

	if ((mi == NULL) || (mi->dhp != dhp)) {
		if (new_pvtp1 != NULL)
			*new_pvtp1 = NULL;

		if (new_pvtp2 != NULL)
			*new_pvtp2 = NULL;

		return;
	}

	softc = mi->softc;

	mutex_enter(&softc->ctx_lock);

	ctx = mi->ctx;

	if (ctx == NULL)
		cmn_err(CE_WARN, "unmap: ctx=NULL");

	/*
	 * Part or all of this mapping is going away.
	 *
	 * If this mapping structure is destroyed and this is the last
	 * mapping that belongs to the context, destroy the context as well
	 */

	/*
	 * partial unmapping.
	 * The unmapping starts somewhere within the original mapping,
	 * leaving the beginning portion of the mapping still mapped
	 */
	if ((new_dhp1 != NULL) && (new_pvtp1 != NULL)) {
		*new_pvtp1 = ast_ctx_new_mapping(softc, ctx, new_dhp1,
		    mi->offset,	offset - mi->offset);
	}

	/*
	 * partial unmapping.
	 * The unnmapping ends somewhere within the original mapping,
	 * leaving the latter portion of the mapping still mapped
	 */
	if ((new_dhp2 != NULL) && (new_pvtp2 != NULL)) {
		*new_pvtp2 = ast_ctx_new_mapping(softc, ctx, new_dhp2,
		    offset + len, mi->offset + mi->len - (offset + len));
	}

	/*
	 * Now destroy the original
	 */
	if (mi->prev != NULL)
		mi->prev->next = mi->next;
	else if (ctx != NULL)
		ctx->mappings = mi->next;

	if (mi->next != NULL)
		mi->next->prev = mi->prev;

	/*
	 * Delete the context if all mappings are gone
	 */
	if ((ctx != NULL) && (ctx->mappings == NULL) &&
	    (softc->shared_ctx != ctx)) {
		if (softc->contexts == ctx) {
			softc->contexts = ctx->next;
		} else {
			for (c = softc->contexts;
			    (c != NULL) && (c->next != ctx); c = c->next)
				;

			if (c == NULL) {
				cmn_err(CE_WARN,
				    "ast: context 0x%p is not found",
				    (void *)ctx);
			} else {
				c->next = ctx->next;
			}
		}

		if (softc->cur_ctx == ctx) {
			softc->cur_ctx = NULL;
		}
		kmem_free(ctx, sizeof (struct ast_context));
	}

	kmem_free(mi, sizeof (struct ast_ctx_map));

	mutex_exit(&softc->ctx_lock);
}

int
ast_devmap(dev_t dev, devmap_cookie_t dhp, offset_t off,
		size_t len, size_t *maplen, uint_t model)
{
	struct ast_softc	*softc = ast_get_softc(getminor(dev));
	ast_regspace_t		*space;
	int			err;

	if (softc == NULL) {
		cmn_err(CE_WARN, "ast%d: ast_devmap, NULL softc ptr",
		    getminor(dev));
		return (EIO);
	}

	if ((space = ast_find_space(softc, off)) == NULL) {
		return (EINVAL);
	}

	if (off + len > space->global_offset + space->size) {
		return (EINVAL);
	}

	err = devmap_devmem_setup(dhp, softc->devi, &ast_devmap_ops,
	    space->rnum, off - space->global_offset + space->register_offset,
	    len, PROT_READ|PROT_WRITE|PROT_USER, 0, space->attrs);

	if (err != 0) {
		cmn_err(CE_WARN,
		    "ast0: ast_devmap: devmap_devmem_setup returns %d", err);
		return (err);
	}

	*maplen = len;

	return (0);
}
