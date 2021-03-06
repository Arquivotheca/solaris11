/* BEGIN CSTYLED */

/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/* i915_irq.c -- IRQ support for the I915 -*- linux-c -*-
 */
/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright (c) 2009, Intel Corporation.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "intel_drv.h"

#define MAX_NOPID ((u32)~0)

/**
 * Interrupts that are always left unmasked.
 *
 * Since pipe events are edge-triggered from the PIPESTAT register to IIR,
 * we leave them always unmasked in IMR and then control enabling them through
 * PIPESTAT alone.
 */
#define I915_INTERRUPT_ENABLE_FIX			\
	(I915_ASLE_INTERRUPT |				\
	 I915_DISPLAY_PIPE_A_EVENT_INTERRUPT |		\
	 I915_DISPLAY_PIPE_B_EVENT_INTERRUPT |		\
	 I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT |	\
	 I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT |	\
	 I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT)

/** Interrupts that we mask and unmask at runtime. */
#define I915_INTERRUPT_ENABLE_VAR (I915_USER_INTERRUPT)

#define I915_PIPE_VBLANK_STATUS	(PIPE_START_VBLANK_INTERRUPT_STATUS |\
				 PIPE_VBLANK_INTERRUPT_STATUS)

#define I915_PIPE_VBLANK_ENABLE	(PIPE_START_VBLANK_INTERRUPT_ENABLE |\
				 PIPE_VBLANK_INTERRUPT_ENABLE)

#define DRM_I915_VBLANK_PIPE_ALL	(DRM_I915_VBLANK_PIPE_A | \
					 DRM_I915_VBLANK_PIPE_B)

void
ironlake_enable_graphics_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->gt_irq_mask_reg & mask) != 0) {
		dev_priv->gt_irq_mask_reg &= ~mask;
		I915_WRITE(GTIMR, dev_priv->gt_irq_mask_reg);
		(void) I915_READ(GTIMR);
	}
}

static inline void
ironlake_disable_graphics_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->gt_irq_mask_reg & mask) != mask) {
		dev_priv->gt_irq_mask_reg |= mask;
		I915_WRITE(GTIMR, dev_priv->gt_irq_mask_reg);
		(void) I915_READ(GTIMR);
	}
}

/* For display hotplug interrupt */
void
ironlake_enable_display_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask_reg & mask) != 0) {
		dev_priv->irq_mask_reg &= ~mask;
		I915_WRITE(DEIMR, dev_priv->irq_mask_reg);
		(void) I915_READ(DEIMR);
	}
}

static inline void
ironlake_disable_display_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask_reg & mask) != mask) {
		dev_priv->irq_mask_reg |= mask;
		I915_WRITE(DEIMR, dev_priv->irq_mask_reg);
		(void) I915_READ(DEIMR);
	}
}

void
i915_enable_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask_reg & mask) != 0) {
		dev_priv->irq_mask_reg &= ~mask;
		I915_WRITE(IMR, dev_priv->irq_mask_reg);
		(void) I915_READ(IMR);
	}
}

static inline void
i915_disable_irq(drm_i915_private_t *dev_priv, u32 mask)
{
	if ((dev_priv->irq_mask_reg & mask) != mask) {
		dev_priv->irq_mask_reg |= mask;
		I915_WRITE(IMR, dev_priv->irq_mask_reg);
		(void) I915_READ(IMR);
	}
}

static inline u32
i915_pipestat(int pipe)
{
	if (pipe == 0)
		return PIPEASTAT;
	if (pipe == 1)
		return PIPEBSTAT;
	return 0;
}

void
i915_enable_pipestat(drm_i915_private_t *dev_priv, int pipe, u32 mask)
{
	if ((dev_priv->pipestat[pipe] & mask) != mask) {
		u32 reg = i915_pipestat(pipe);

		dev_priv->pipestat[pipe] |= mask;
		/* Enable the interrupt, clear any pending status */
		I915_WRITE(reg, dev_priv->pipestat[pipe] | (mask >> 16));
		(void) I915_READ(reg);
	}
}

void
i915_disable_pipestat(drm_i915_private_t *dev_priv, int pipe, u32 mask)
{
	if ((dev_priv->pipestat[pipe] & mask) != 0) {
		u32 reg = i915_pipestat(pipe);

		dev_priv->pipestat[pipe] &= ~mask;
		I915_WRITE(reg, dev_priv->pipestat[pipe]);
		(void) I915_READ(reg);
	}
}

/**
 * i915_pipe_enabled - check if a pipe is enabled
 * @dev: DRM device
 * @pipe: pipe to check
 *
 * Reading certain registers when the pipe is disabled can hang the chip.
 * Use this routine to make sure the PLL is running and the pipe is active
 * before reading such registers if unsure.
 */
static int
i915_pipe_enabled(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long pipeconf = pipe ? PIPEBCONF : PIPEACONF;

	if (I915_READ(pipeconf) & PIPEACONF_ENABLE)
		return 1;

	return 0;
}

/* Called from drm generic code, passed a 'crtc', which
 * we use as a pipe index
 */
u32 i915_get_vblank_counter(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	unsigned long high_frame;
	unsigned long low_frame;
	u32 high1, high2, low, count;

	high_frame = pipe ? PIPEBFRAMEHIGH : PIPEAFRAMEHIGH;
	low_frame = pipe ? PIPEBFRAMEPIXEL : PIPEAFRAMEPIXEL;

	if (!i915_pipe_enabled(dev, pipe)) {
		DRM_DEBUG_DRIVER("trying to get vblank count for disabled "
				"pipe %d\n", pipe);
		return 0;
	}

	/*
	 * High & low register fields aren't synchronized, so make sure
	 * we get a low value that's stable across two reads of the high
	 * register.
	 */
	do {
		high1 = ((I915_READ(high_frame) & PIPE_FRAME_HIGH_MASK) >>
			 PIPE_FRAME_HIGH_SHIFT);
		low =  ((I915_READ(low_frame) & PIPE_FRAME_LOW_MASK) >>
			PIPE_FRAME_LOW_SHIFT);
		high2 = ((I915_READ(high_frame) & PIPE_FRAME_HIGH_MASK) >>
			 PIPE_FRAME_HIGH_SHIFT);
	} while (high1 != high2);

	count = (high1 << 8) | low;

	return count;
}

u32 gm45_get_vblank_counter(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int reg = pipe ? PIPEB_FRMCOUNT_GM45 : PIPEA_FRMCOUNT_GM45;

	if (!i915_pipe_enabled(dev, pipe)) {
		DRM_DEBUG_DRIVER("trying to get vblank count for disabled "
					"pipe %d\n", pipe);
		return 0;
	}

	return I915_READ(reg);
}

/*
 * Handle hotplug events outside the interrupt handler proper.
 */
static void i915_hotplug_work_func(struct work_struct *work)
{
	drm_i915_private_t *dev_priv = container_of(work, drm_i915_private_t,
						    hotplug_work);
	struct drm_device *dev = dev_priv->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;

	if (mode_config->num_connector) {
		list_for_each_entry(connector, struct drm_connector, &mode_config->connector_list, head) {
			struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	
			if (intel_encoder->hot_plug)
				(*intel_encoder->hot_plug) (intel_encoder);
		}
	}
	/* Just fire off a uevent and let userspace tell us what to do */
	/* OSOL_i915: drm_sysfs_hotplug_event(dev); */
}

irqreturn_t ironlake_irq_handler(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int ret = IRQ_NONE;
	u32 de_iir, gt_iir, de_ier, pch_iir;
	u32 hotplug_mask;
	struct drm_i915_master_private *master_priv;

	/* disable master interrupt before clearing iir  */
	de_ier = I915_READ(DEIER);
	I915_WRITE(DEIER, de_ier & ~DE_MASTER_IRQ_CONTROL);
	(void)I915_READ(DEIER);

	de_iir = I915_READ(DEIIR);
	gt_iir = I915_READ(GTIIR);
	pch_iir = I915_READ(SDEIIR);

	if (de_iir == 0 && gt_iir == 0 && pch_iir == 0)
		goto done;

	if (HAS_PCH_CPT(dev))
		hotplug_mask = SDE_HOTPLUG_MASK_CPT;
	else
		hotplug_mask = SDE_HOTPLUG_MASK;

	ret = IRQ_HANDLED;

	if (dev->primary->master) {
		master_priv = dev->primary->master->driver_priv;
		if (master_priv->sarea_priv)
			master_priv->sarea_priv->last_dispatch =
				READ_BREADCRUMB(dev_priv);
	}

	if (gt_iir & GT_PIPE_NOTIFY) {
		u32 seqno = i915_get_gem_seqno(dev);
		dev_priv->mm.irq_gem_seqno = seqno;
		/* OSOL_i915: trace_i915_gem_request_complete(dev, seqno); */
		DRM_WAKEUP(&dev_priv->irq_queue);
	}

	if (de_iir & DE_PLANEA_FLIP_DONE) {
		intel_prepare_page_flip(dev, 0);
		intel_finish_page_flip(dev, 0);
	}

	if (de_iir & DE_PLANEB_FLIP_DONE) {
		intel_prepare_page_flip(dev, 1);
		intel_finish_page_flip(dev, 1);
	}

	if (de_iir & DE_PIPEA_VBLANK)
		drm_handle_vblank(dev, 0);

	if (de_iir & DE_PIPEB_VBLANK)
		drm_handle_vblank(dev, 1);

	/* check event from PCH */
	if ((de_iir & DE_PCH_EVENT) && (pch_iir & hotplug_mask)) {
		(void) queue_work(dev_priv->wq, &dev_priv->hotplug_work);
	}

	/* should clear PCH hotplug event before clear CPU irq */
	I915_WRITE(SDEIIR, pch_iir);
	I915_WRITE(GTIIR, gt_iir);
	I915_WRITE(DEIIR, de_iir);

done:
	I915_WRITE(DEIER, de_ier);
	(void)I915_READ(DEIER);

	return ret;
}

/**
 * i915_error_work_func - do process context error handling work
 * @work: work struct
 *
 * Fire an error uevent so userspace can see that a hang or error
 * was detected.
 */
static void i915_error_work_func(struct work_struct *work)
{
	drm_i915_private_t *dev_priv = container_of(work, drm_i915_private_t,
						    error_work);
	struct drm_device *dev = dev_priv->dev;
	/* LINTED */
	char *error_event[] = { "ERROR=1", NULL };
	/* LINTED */
	char *reset_event[] = { "RESET=1", NULL };
	/* LINTED */
	char *reset_done_event[] = { "ERROR=0", NULL };

	DRM_DEBUG_DRIVER("generating error event\n");
	/* OSOL_i915: kobject_uevent_env(&dev->primary->kdev.kobj, KOBJ_CHANGE, error_event); */

	if (atomic_read(&dev_priv->mm.wedged)) {
		if (IS_I965G(dev)) {
			DRM_DEBUG_DRIVER("resetting chip\n");
			/* OSOL_i915: kobject_uevent_env(&dev->primary->kdev.kobj, KOBJ_CHANGE, reset_event); */
			if (!i965_reset(dev, GDRST_RENDER)) {
				atomic_set(&dev_priv->mm.wedged, 0);
				/* OSOL_i915: kobject_uevent_env(&dev->primary->kdev.kobj, KOBJ_CHANGE, reset_done_event); */
			}
		} else {
			DRM_DEBUG_DRIVER("reboot required\n");
		}
	}
}

/**
 * i915_capture_error_state - capture an error record for later analysis
 * @dev: drm device
 *
 * Should be called when an error is detected (either a hang or an error
 * interrupt) to capture error state from the time of the error.  Fills
 * out a structure which becomes available in debugfs for user level tools
 * to pick up.
 */
static void i915_capture_error_state(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_error_state *error;

	spin_lock_irqsave(&dev_priv->error_lock, flags);
	if (dev_priv->first_error)
		goto out;

	error = kmalloc(sizeof(*error), GFP_ATOMIC);
	if (!error) {
		DRM_DEBUG("out ot memory, not capturing error state\n");
		goto out;
	}

	error->eir = I915_READ(EIR);
	error->pgtbl_er = I915_READ(PGTBL_ER);
	error->pipeastat = I915_READ(PIPEASTAT);
	error->pipebstat = I915_READ(PIPEBSTAT);
	error->instpm = I915_READ(INSTPM);
	if (!IS_I965G(dev)) {
		error->ipeir = I915_READ(IPEIR);
		error->ipehr = I915_READ(IPEHR);
		error->instdone = I915_READ(INSTDONE);
		error->acthd = I915_READ(ACTHD);
	} else {
		error->ipeir = I915_READ(IPEIR_I965);
		error->ipehr = I915_READ(IPEHR_I965);
		error->instdone = I915_READ(INSTDONE_I965);
		error->instps = I915_READ(INSTPS);
		error->instdone1 = I915_READ(INSTDONE1);
		error->acthd = I915_READ(ACTHD_I965);
	}

	do_gettimeofday(&error->time);

	dev_priv->first_error = error;

out:
	spin_unlock_irqrestore(&dev_priv->error_lock, flags);
}

/**
 * i915_handle_error - handle an error interrupt
 * @dev: drm device
 *
 * Do some basic checking of regsiter state at error interrupt time and
 * dump it to the syslog.  Also call i915_capture_error_state() to make
 * sure we get a record and make it available in debugfs.  Fire a uevent
 * so userspace knows something bad happened (should trigger collection
 * of a ring dump etc.).
 */
static void i915_handle_error(struct drm_device *dev, bool wedged)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 eir = I915_READ(EIR);
	u32 pipea_stats = I915_READ(PIPEASTAT);
	u32 pipeb_stats = I915_READ(PIPEBSTAT);

	i915_capture_error_state(dev);

	DRM_DEBUG("render error detected, EIR: 0x%08x\n",
	       eir);

	if (IS_G4X(dev)) {
		if (eir & (GM45_ERROR_MEM_PRIV | GM45_ERROR_CP_PRIV)) {
			u32 ipeir = I915_READ(IPEIR_I965);

			DRM_DEBUG("  IPEIR: 0x%08x\n",
			       I915_READ(IPEIR_I965));
			DRM_DEBUG("  IPEHR: 0x%08x\n",
			       I915_READ(IPEHR_I965));
			DRM_DEBUG("  INSTDONE: 0x%08x\n",
			       I915_READ(INSTDONE_I965));
			DRM_DEBUG("  INSTPS: 0x%08x\n",
			       I915_READ(INSTPS));
			DRM_DEBUG("  INSTDONE1: 0x%08x\n",
			       I915_READ(INSTDONE1));
			DRM_DEBUG("  ACTHD: 0x%08x\n",
			       I915_READ(ACTHD_I965));
			I915_WRITE(IPEIR_I965, ipeir);
			(void)I915_READ(IPEIR_I965);
		}
		if (eir & GM45_ERROR_PAGE_TABLE) {
			u32 pgtbl_err = I915_READ(PGTBL_ER);
			DRM_DEBUG("page table error\n");
			DRM_DEBUG("  PGTBL_ER: 0x%08x\n",
			       pgtbl_err);
			I915_WRITE(PGTBL_ER, pgtbl_err);
			(void)I915_READ(PGTBL_ER);
		}
	}

	if (IS_I9XX(dev)) {
		if (eir & I915_ERROR_PAGE_TABLE) {
			u32 pgtbl_err = I915_READ(PGTBL_ER);
			DRM_DEBUG("page table error\n");
			DRM_DEBUG("  PGTBL_ER: 0x%08x\n",
			       pgtbl_err);
			I915_WRITE(PGTBL_ER, pgtbl_err);
			(void)I915_READ(PGTBL_ER);
		}
	}

	if (eir & I915_ERROR_MEMORY_REFRESH) {
		DRM_DEBUG("memory refresh error\n");
		DRM_DEBUG("PIPEASTAT: 0x%08x\n",
		       pipea_stats);
		DRM_DEBUG("PIPEBSTAT: 0x%08x\n",
		       pipeb_stats);
		/* pipestat has already been acked */
	}
	if (eir & I915_ERROR_INSTRUCTION) {
		DRM_DEBUG("instruction error\n");
		DRM_DEBUG("  INSTPM: 0x%08x\n",
		       I915_READ(INSTPM));
		if (!IS_I965G(dev)) {
			u32 ipeir = I915_READ(IPEIR);

			DRM_DEBUG("  IPEIR: 0x%08x\n",
			       I915_READ(IPEIR));
			DRM_DEBUG("  IPEHR: 0x%08x\n",
			       I915_READ(IPEHR));
			DRM_DEBUG("  INSTDONE: 0x%08x\n",
			       I915_READ(INSTDONE));
			DRM_DEBUG("  ACTHD: 0x%08x\n",
			       I915_READ(ACTHD));
			I915_WRITE(IPEIR, ipeir);
			(void)I915_READ(IPEIR);
		} else {
			u32 ipeir = I915_READ(IPEIR_I965);

			DRM_DEBUG("  IPEIR: 0x%08x\n",
			       I915_READ(IPEIR_I965));
			DRM_DEBUG("  IPEHR: 0x%08x\n",
			       I915_READ(IPEHR_I965));
			DRM_DEBUG("  INSTDONE: 0x%08x\n",
			       I915_READ(INSTDONE_I965));
			DRM_DEBUG("  INSTPS: 0x%08x\n",
			       I915_READ(INSTPS));
			DRM_DEBUG("  INSTDONE1: 0x%08x\n",
			       I915_READ(INSTDONE1));
			DRM_DEBUG("  ACTHD: 0x%08x\n",
			       I915_READ(ACTHD_I965));
			I915_WRITE(IPEIR_I965, ipeir);
			(void)I915_READ(IPEIR_I965);
		}
	}

	I915_WRITE(EIR, eir);
	(void)I915_READ(EIR);
	eir = I915_READ(EIR);
	if (eir) {
		/*
		 * some errors might have become stuck,
		 * mask them.
		 */
		DRM_ERROR("EIR stuck: 0x%08x, masking\n", eir);
		I915_WRITE(EMR, I915_READ(EMR) | eir);
		I915_WRITE(IIR, I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT);
	}

	if (wedged) {
		atomic_set(&dev_priv->mm.wedged, 1);

		/*
		 * Wakeup waiting processes so they don't hang
		 */
		DRM_WAKEUP(&dev_priv->irq_queue);
	}

	(void) queue_work(dev_priv->wq, &dev_priv->error_work);
}

irqreturn_t i915_driver_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *)(uintptr_t) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct drm_i915_master_private *master_priv;
	u32 iir, new_iir;
	u32 pipea_stats, pipeb_stats;
	u32 vblank_status;
	/* LINTED */
	u32 vblank_enable;
	int vblank = 0;
	/* LINTED */
	unsigned long irqflags;
	int irq_received;
	int ret = IRQ_NONE;

	atomic_inc(&dev_priv->irq_received);

	if (HAS_PCH_SPLIT(dev))
		return ironlake_irq_handler(dev);

	iir = I915_READ(IIR);

	if (IS_I965G(dev)) {
		vblank_status = I915_START_VBLANK_INTERRUPT_STATUS;
		vblank_enable = PIPE_START_VBLANK_INTERRUPT_ENABLE;
	} else {
		vblank_status = I915_VBLANK_INTERRUPT_STATUS;
		vblank_enable = I915_VBLANK_INTERRUPT_ENABLE;
	}

	for (;;) {
		irq_received = iir != 0;

		/* Can't rely on pipestat interrupt bit in iir as it might
		 * have been cleared after the pipestat interrupt was received.
		 * It doesn't set the bit in iir again, but it still produces
		 * interrupts (for non-MSI).
		 */
		spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
		pipea_stats = I915_READ(PIPEASTAT);
		pipeb_stats = I915_READ(PIPEBSTAT);

		if (iir & I915_RENDER_COMMAND_PARSER_ERROR_INTERRUPT)
			i915_handle_error(dev, false);

		/*
		 * Clear the PIPE(A|B)STAT regs before the IIR
		 */
		if (pipea_stats & 0x8000ffff) {
			if (pipea_stats &  PIPE_FIFO_UNDERRUN_STATUS)
				DRM_DEBUG_DRIVER("pipe a underrun\n");
			I915_WRITE(PIPEASTAT, pipea_stats);
			irq_received = 1;
		}

		if (pipeb_stats & 0x8000ffff) {
			if (pipeb_stats &  PIPE_FIFO_UNDERRUN_STATUS)
				DRM_DEBUG_DRIVER("pipe b underrun\n");
			I915_WRITE(PIPEBSTAT, pipeb_stats);
			irq_received = 1;
		}
		spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);

		if (!irq_received)
			break;

		ret = IRQ_HANDLED;

		/* Consume port.  Then clear IIR or we'll miss events */
		if ((I915_HAS_HOTPLUG(dev)) &&
		    (iir & I915_DISPLAY_PORT_INTERRUPT)) {
			u32 hotplug_status = I915_READ(PORT_HOTPLUG_STAT);

			DRM_DEBUG_DRIVER("hotplug event received, stat 0x%08x\n",
				  hotplug_status);
			if (hotplug_status & dev_priv->hotplug_supported_mask)
				(void) queue_work(dev_priv->wq,
					   &dev_priv->hotplug_work);

			I915_WRITE(PORT_HOTPLUG_STAT, hotplug_status);
			I915_READ(PORT_HOTPLUG_STAT);
		}

		I915_WRITE(IIR, iir);
		new_iir = I915_READ(IIR); /* Flush posted writes */

		if (dev->primary->master) {
			master_priv = dev->primary->master->driver_priv;
			if (master_priv->sarea_priv)
				master_priv->sarea_priv->last_dispatch =
					READ_BREADCRUMB(dev_priv);
		}

		if (iir & I915_USER_INTERRUPT) {
			u32 seqno = i915_get_gem_seqno(dev);
			dev_priv->mm.irq_gem_seqno = seqno;
			/* OSOL_i915: trace_i915_gem_request_complete(dev, seqno); */
			DRM_WAKEUP(&dev_priv->irq_queue);
			dev_priv->hangcheck_count = 0;
			mod_timer(&dev_priv->hangcheck_timer, jiffies + DRM_I915_HANGCHECK_PERIOD);
		}

		if (iir & I915_DISPLAY_PLANE_A_FLIP_PENDING_INTERRUPT)
			intel_prepare_page_flip(dev, 0);

		if (iir & I915_DISPLAY_PLANE_B_FLIP_PENDING_INTERRUPT)
			intel_prepare_page_flip(dev, 1);

		if (pipea_stats & vblank_status) {
			vblank++;
			drm_handle_vblank(dev, 0);
			intel_finish_page_flip(dev, 0);
		}

		if (pipeb_stats & vblank_status) {
			vblank++;
			drm_handle_vblank(dev, 1);
			intel_finish_page_flip(dev, 1);
		}

		/* With MSI, interrupts are only generated when iir
		 * transitions from zero to nonzero.  If another bit got
		 * set while we were handling the existing iir bits, then
		 * we would never get another interrupt.
		 *
		 * This is fine on non-MSI as well, as if we hit this path
		 * we avoid exiting the interrupt handler only to generate
		 * another one.
		 *
		 * Note that for MSI this could cause a stray interrupt report
		 * if an interrupt landed in the time between writing IIR and
		 * the posting read.  This should be rare enough to never
		 * trigger the 99% of 100,000 interrupts test for disabling
		 * stray interrupts.
		 */
		iir = new_iir;
	}

	return ret;
}

static int i915_emit_irq(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
	RING_LOCALS;

	i915_kernel_lost_context(dev);

	DRM_DEBUG_DRIVER("\n");

	dev_priv->counter++;
	if (dev_priv->counter > 0x7FFFFFFFUL)
		dev_priv->counter = 1;
	if (master_priv->sarea_priv)
		master_priv->sarea_priv->last_enqueue = dev_priv->counter;

	BEGIN_LP_RING(4);
	OUT_RING(MI_STORE_DWORD_INDEX);
	OUT_RING(I915_BREADCRUMB_INDEX << MI_STORE_DWORD_INDEX_SHIFT);
	OUT_RING(dev_priv->counter);
	OUT_RING(MI_USER_INTERRUPT);
	ADVANCE_LP_RING();

	if (IS_I965GM(dev)) {
		(void) READ_BREADCRUMB(dev_priv);
		BEGIN_LP_RING(2);
		OUT_RING(0);
		OUT_RING(0);
		ADVANCE_LP_RING();
		(void) READ_BREADCRUMB(dev_priv);
	}

	return dev_priv->counter;
}

void i915_user_irq_get(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	/* LINTED */
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	if (dev->irq_enabled && (++dev_priv->user_irq_refcount == 1)) {
		if (HAS_PCH_SPLIT(dev))
			ironlake_enable_graphics_irq(dev_priv, GT_PIPE_NOTIFY);
		else
			i915_enable_irq(dev_priv, I915_USER_INTERRUPT);
	}
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
}

void i915_user_irq_put(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	/* LINTED */
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	BUG_ON(dev->irq_enabled && dev_priv->user_irq_refcount <= 0);
	if (dev->irq_enabled && (--dev_priv->user_irq_refcount == 0)) {
		if (HAS_PCH_SPLIT(dev))
			ironlake_disable_graphics_irq(dev_priv, GT_PIPE_NOTIFY);
		else
			i915_disable_irq(dev_priv, I915_USER_INTERRUPT);
	}
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
}

static int i915_wait_irq(struct drm_device * dev, int irq_nr)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	struct drm_i915_master_private *master_priv = dev->primary->master->driver_priv;
	int ret = 0;

	DRM_DEBUG_DRIVER("irq_nr=%d breadcrumb=%d\n", irq_nr,
		  READ_BREADCRUMB(dev_priv));

	if (READ_BREADCRUMB(dev_priv) >= irq_nr) {
		if (master_priv->sarea_priv)
			master_priv->sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);
		return 0;
	}

	if (master_priv->sarea_priv)
		master_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;

	i915_user_irq_get(dev);
	DRM_WAIT_ON(ret, &dev_priv->irq_queue, 3 * DRM_HZ,
		    READ_BREADCRUMB(dev_priv) >= irq_nr);
	i915_user_irq_put(dev);

	if (ret == -EBUSY) {
		DRM_ERROR("EBUSY -- rec: %d emitted: %d\n",
			  READ_BREADCRUMB(dev_priv), (int)dev_priv->counter);
	}

	return ret;
}

/* Needs the lock as it touches the ring.
 */
/* LINTED */
int i915_irq_emit(DRM_IOCTL_ARGS)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_emit_t *emit = data;
	int result;

	if (!dev_priv || !dev_priv->ring.virtual_start) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	RING_LOCK_TEST_WITH_RETURN(dev, file_priv);

	mutex_lock(&dev->struct_mutex);
	result = i915_emit_irq(dev);
	mutex_unlock(&dev->struct_mutex);

	if (DRM_COPY_TO_USER(emit->irq_seq, &result, sizeof(int))) {
		DRM_ERROR("copy_to_user\n");
		return -EFAULT;
	}

	return 0;
}

/* Doesn't need the hardware lock.
 */
/* LINTED */
int i915_irq_wait(DRM_IOCTL_ARGS)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_wait_t *irqwait = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	return i915_wait_irq(dev, irqwait->irq_seq);
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
int i915_enable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	/* LINTED */
	unsigned long irqflags;
	int pipeconf_reg = (pipe == 0) ? PIPEACONF : PIPEBCONF;
	u32 pipeconf;

	pipeconf = I915_READ(pipeconf_reg);
	if (!(pipeconf & PIPEACONF_ENABLE))
		return -EINVAL;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	if (HAS_PCH_SPLIT(dev))
		ironlake_enable_display_irq(dev_priv, (pipe == 0) ? 
					    DE_PIPEA_VBLANK: DE_PIPEB_VBLANK);
	else if (IS_I965G(dev))
		i915_enable_pipestat(dev_priv, pipe,
				     PIPE_START_VBLANK_INTERRUPT_ENABLE);
	else
		i915_enable_pipestat(dev_priv, pipe,
				     PIPE_VBLANK_INTERRUPT_ENABLE);
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
	return 0;
}

/* Called from drm generic code, passed 'crtc' which
 * we use as a pipe index
 */
void i915_disable_vblank(struct drm_device *dev, int pipe)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	/* LINTED */
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->user_irq_lock, irqflags);
	if (HAS_PCH_SPLIT(dev))
		ironlake_disable_display_irq(dev_priv, (pipe == 0) ? 
					     DE_PIPEA_VBLANK: DE_PIPEB_VBLANK);
	else
		i915_disable_pipestat(dev_priv, pipe,
				      PIPE_VBLANK_INTERRUPT_ENABLE |
				      PIPE_START_VBLANK_INTERRUPT_ENABLE);
	spin_unlock_irqrestore(&dev_priv->user_irq_lock, irqflags);
}

/* Set the vblank monitor pipe
 */
/* LINTED */
int i915_vblank_pipe_set(DRM_IOCTL_ARGS)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	return 0;
}

/* LINTED */
int i915_vblank_pipe_get(DRM_IOCTL_ARGS)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_vblank_pipe_t *pipe = data;

	if (!dev_priv) {
		DRM_ERROR("called with no initialization\n");
		return -EINVAL;
	}

	pipe->pipe = DRM_I915_VBLANK_PIPE_A | DRM_I915_VBLANK_PIPE_B;

	return 0;
}

/**
 * Schedule buffer swap at given vertical blank.
 */
/* LINTED */
int i915_vblank_swap(DRM_IOCTL_ARGS)
{
	/* The delayed swap mechanism was fundamentally racy, and has been
	 * removed.  The model was that the client requested a delayed flip/swap
	 * from the kernel, then waited for vblank before continuing to perform
	 * rendering.  The problem was that the kernel might wake the client
	 * up before it dispatched the vblank swap (since the lock has to be
	 * held while touching the ringbuffer), in which case the client would
	 * clear and start the next frame before the swap occurred, and
	 * flicker would occur in addition to likely missing the vblank.
	 *
	 * In the absence of this ioctl, userland falls back to a correct path
	 * of waiting for a vblank, then dispatching the swap on its own.
	 * Context switching to userland and back is plenty fast enough for
	 * meeting the requirements of vblank swapping.
	 */
	return -EINVAL;
}

struct drm_i915_gem_request *i915_get_tail_request(struct drm_device *dev) {
	drm_i915_private_t *dev_priv = dev->dev_private;
	return list_entry(dev_priv->mm.request_list.prev, struct drm_i915_gem_request, list);
}

/**
 * This is called when the chip hasn't reported back with completed
 * batchbuffers in a long time. The first time this is called we simply record
 * ACTHD. If ACTHD hasn't changed by the time the hangcheck timer elapses
 * again, we assume the chip is wedged and try to fix it.
 */
void i915_hangcheck_elapsed(void *data)
{
	struct drm_device *dev = (struct drm_device *)data;
	drm_i915_private_t *dev_priv = dev->dev_private;
	uint32_t acthd;

	/* No reset support on this chip yet. */
	if (IS_GEN6(dev))
		return;

	if (!IS_I965G(dev))
		acthd = I915_READ(ACTHD);
	else
		acthd = I915_READ(ACTHD_I965);

	/* If all work is done then ACTHD clearly hasn't advanced. */
	/* XXX FIXME: if (list_empty(&dev_priv->mm.request_list) ||
		       i915_seqno_passed(i915_get_gem_seqno(dev), i915_get_tail_request(dev)->seqno)) {
			dev_priv->hangcheck_count = 0;
		return;
	} */

	if (dev_priv->last_acthd == acthd && dev_priv->hangcheck_count > 0) {
		DRM_ERROR("Hangcheck timer elapsed... GPU hung\n");
		i915_handle_error(dev, true);
		return;
	} 

	/* Reset timer case chip hangs without another request being added */
	mod_timer(&dev_priv->hangcheck_timer, jiffies + DRM_I915_HANGCHECK_PERIOD);

	if (acthd != dev_priv->last_acthd)
		dev_priv->hangcheck_count = 0;
	else
		dev_priv->hangcheck_count++;

	dev_priv->last_acthd = acthd;
}

/* drm_dma.h hooks
*/
static void ironlake_irq_preinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	I915_WRITE(HWSTAM, 0xeffe);

	/* XXX hotplug from PCH */

	I915_WRITE(DEIMR, 0xffffffff);
	I915_WRITE(DEIER, 0x0);
	(void) I915_READ(DEIER);

	/* and GT */
	I915_WRITE(GTIMR, 0xffffffff);
	I915_WRITE(GTIER, 0x0);
	(void) I915_READ(GTIER);

	/* south display irq */
	I915_WRITE(SDEIMR, 0xffffffff);
	I915_WRITE(SDEIER, 0x0);
	(void) I915_READ(SDEIER);
}

static int ironlake_irq_postinstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	/* enable kind of interrupts always enabled */
	u32 display_mask = DE_MASTER_IRQ_CONTROL | DE_GSE | DE_PCH_EVENT |
			   DE_PLANEA_FLIP_DONE | DE_PLANEB_FLIP_DONE;
	u32 render_mask = GT_PIPE_NOTIFY;
	u32 hotplug_mask = SDE_CRT_HOTPLUG | SDE_PORTB_HOTPLUG |
			   SDE_PORTC_HOTPLUG | SDE_PORTD_HOTPLUG;

	dev_priv->irq_mask_reg = ~display_mask;
	dev_priv->de_irq_enable_reg = display_mask | DE_PIPEA_VBLANK | DE_PIPEB_VBLANK;

	/* should always can generate irq */
	I915_WRITE(DEIIR, I915_READ(DEIIR));
	I915_WRITE(DEIMR, dev_priv->irq_mask_reg);
	I915_WRITE(DEIER, dev_priv->de_irq_enable_reg);
	(void) I915_READ(DEIER);

	/* Gen6 only needs render pipe_control now */
	if (IS_GEN6(dev))
		render_mask = GT_PIPE_NOTIFY;

	dev_priv->gt_irq_mask_reg = ~render_mask;
	dev_priv->gt_irq_enable_reg = render_mask;

	I915_WRITE(GTIIR, I915_READ(GTIIR));
	I915_WRITE(GTIMR, dev_priv->gt_irq_mask_reg);
	if (IS_GEN6(dev))
		I915_WRITE(GEN6_RENDER_IMR, ~GEN6_RENDER_PIPE_CONTROL_NOTIFY_INTERRUPT);
	I915_WRITE(GTIER, dev_priv->gt_irq_enable_reg);
	(void) I915_READ(GTIER);

	dev_priv->pch_irq_mask_reg = ~hotplug_mask;
	dev_priv->pch_irq_enable_reg = hotplug_mask;

	I915_WRITE(SDEIIR, I915_READ(SDEIIR));
	I915_WRITE(SDEIMR, dev_priv->pch_irq_mask_reg);
	I915_WRITE(SDEIER, dev_priv->pch_irq_enable_reg);
	(void) I915_READ(SDEIER);

	if (IS_IRONLAKE_M(dev)) {
		/* Clear & enable PCU event interrupts */
		I915_WRITE(DEIIR, DE_PCU_EVENT);
		I915_WRITE(DEIER, I915_READ(DEIER) | DE_PCU_EVENT);
		ironlake_enable_display_irq(dev_priv, DE_PCU_EVENT);
	}

	return 0;
}

int i915_driver_irq_preinstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	if (!dev_priv->regs)
		return -EINVAL;

	atomic_set(&dev_priv->irq_received, 0);

	INIT_WORK(&dev_priv->hotplug_work, i915_hotplug_work_func);
	INIT_WORK(&dev_priv->error_work, i915_error_work_func);

	if (HAS_PCH_SPLIT(dev)) {
		ironlake_irq_preinstall(dev);
		return 0;
	}

	if (I915_HAS_HOTPLUG(dev)) {
		I915_WRITE(PORT_HOTPLUG_EN, 0);
		I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));
	}

	I915_WRITE(HWSTAM, 0xeffe);
	I915_WRITE(PIPEASTAT, 0);
	I915_WRITE(PIPEBSTAT, 0);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);
	(void) I915_READ(IER);

	return 0;
}

/*
 * Must be called after intel_modeset_init or hotplug interrupts won't be
 * enabled correctly.
 */
void i915_driver_irq_postinstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u32 enable_mask = I915_INTERRUPT_ENABLE_FIX | I915_INTERRUPT_ENABLE_VAR;
	u32 error_mask;

	DRM_INIT_WAITQUEUE(&dev_priv->irq_queue, DRM_INTR_PRI(dev));

	dev_priv->vblank_pipe = DRM_I915_VBLANK_PIPE_A | DRM_I915_VBLANK_PIPE_B;

	if (HAS_PCH_SPLIT(dev)) {
		(void) ironlake_irq_postinstall(dev);
		return;
	}

	/* Unmask the interrupts that we always want on. */
	dev_priv->irq_mask_reg = (u32) ~I915_INTERRUPT_ENABLE_FIX;

	dev_priv->pipestat[0] = 0;
	dev_priv->pipestat[1] = 0;

	if (I915_HAS_HOTPLUG(dev)) {
		u32 hotplug_en = I915_READ(PORT_HOTPLUG_EN);

		/* Note HDMI and DP share bits */
		if (dev_priv->hotplug_supported_mask & HDMIB_HOTPLUG_INT_STATUS)
			hotplug_en |= HDMIB_HOTPLUG_INT_EN;
		if (dev_priv->hotplug_supported_mask & HDMIC_HOTPLUG_INT_STATUS)
			hotplug_en |= HDMIC_HOTPLUG_INT_EN;
		if (dev_priv->hotplug_supported_mask & HDMID_HOTPLUG_INT_STATUS)
			hotplug_en |= HDMID_HOTPLUG_INT_EN;
		if (dev_priv->hotplug_supported_mask & SDVOC_HOTPLUG_INT_STATUS)
			hotplug_en |= SDVOC_HOTPLUG_INT_EN;
		if (dev_priv->hotplug_supported_mask & SDVOB_HOTPLUG_INT_STATUS)
			hotplug_en |= SDVOB_HOTPLUG_INT_EN;
		if (dev_priv->hotplug_supported_mask & CRT_HOTPLUG_INT_STATUS)
			hotplug_en |= CRT_HOTPLUG_INT_EN;
		/* Ignore TV since it's buggy */

		I915_WRITE(PORT_HOTPLUG_EN, hotplug_en);

		/* Enable in IER... */
		enable_mask |= I915_DISPLAY_PORT_INTERRUPT;
		/* and unmask in IMR */
		i915_enable_irq(dev_priv, I915_DISPLAY_PORT_INTERRUPT);
	}

	/*
	 * Enable some error detection, note the instruction error mask
	 * bit is reserved, so we leave it masked.
	 */
	if (IS_G4X(dev)) {
		error_mask = (u32) ~(GM45_ERROR_PAGE_TABLE |
			       GM45_ERROR_MEM_PRIV |
			       GM45_ERROR_CP_PRIV |
			       I915_ERROR_MEMORY_REFRESH);
	} else {
		error_mask = (u32) ~(I915_ERROR_PAGE_TABLE |
			       I915_ERROR_MEMORY_REFRESH);
	}
	I915_WRITE(EMR, error_mask);

	/* Disable pipe interrupt enables, clear pending pipe status */
	I915_WRITE(PIPEASTAT, I915_READ(PIPEASTAT) & 0x8000ffff);
	I915_WRITE(PIPEBSTAT, I915_READ(PIPEBSTAT) & 0x8000ffff);
	/* Clear pending interrupt status */
	I915_WRITE(IIR, I915_READ(IIR));

	I915_WRITE(IER, enable_mask);
	I915_WRITE(IMR, dev_priv->irq_mask_reg);
	(void) I915_READ(IER);

	return;
}

static void ironlake_irq_uninstall(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	I915_WRITE(HWSTAM, 0xffffffff);

	I915_WRITE(DEIMR, 0xffffffff);
	I915_WRITE(DEIER, 0x0);
	I915_WRITE(DEIIR, I915_READ(DEIIR));

	I915_WRITE(GTIMR, 0xffffffff);
	I915_WRITE(GTIER, 0x0);
	I915_WRITE(GTIIR, I915_READ(GTIIR));
}

void i915_driver_irq_uninstall(struct drm_device * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	if ((!dev_priv) || (dev->irq_enabled == 0))
		return;

	dev_priv->vblank_pipe = 0;

	if (HAS_PCH_SPLIT(dev)) {
		ironlake_irq_uninstall(dev);
		DRM_FINI_WAITQUEUE(&dev_priv->irq_queue);
		return;
	}

	if (I915_HAS_HOTPLUG(dev)) {
		I915_WRITE(PORT_HOTPLUG_EN, 0);
		I915_WRITE(PORT_HOTPLUG_STAT, I915_READ(PORT_HOTPLUG_STAT));
	}

	I915_WRITE(HWSTAM, 0xffffffff);
	I915_WRITE(PIPEASTAT, 0);
	I915_WRITE(PIPEBSTAT, 0);
	I915_WRITE(IMR, 0xffffffff);
	I915_WRITE(IER, 0x0);

	I915_WRITE(PIPEASTAT, I915_READ(PIPEASTAT) & 0x8000ffff);
	I915_WRITE(PIPEBSTAT, I915_READ(PIPEBSTAT) & 0x8000ffff);
	I915_WRITE(IIR, I915_READ(IIR));

	DRM_FINI_WAITQUEUE(&dev_priv->irq_queue);
}

