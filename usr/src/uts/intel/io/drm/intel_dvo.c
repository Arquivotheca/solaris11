/* BEGIN CSTYLED */

/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright 2006 Dave Airlie <airlied@linux.ie>
 * Copyright © 2006-2007 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 */
#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_sun_i2c.h"  /* OSOL_i915 */
#include "intel_drv.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "dvo.h"

#define SIL164_ADDR	0x38
#define CH7xxx_ADDR	0x76
#define TFP410_ADDR	0x38

static struct intel_dvo_device intel_dvo_devices[] = {
	{
		.type = INTEL_DVO_CHIP_TMDS,
		.name = "sil164",
		.dvo_reg = DVOC,
		.slave_addr = SIL164_ADDR,
		.dev_ops = &sil164_ops,
	},
	{
		.type = INTEL_DVO_CHIP_TMDS,
		.name = "ch7xxx",
		.dvo_reg = DVOC,
		.slave_addr = CH7xxx_ADDR,
		.dev_ops = &ch7xxx_ops,
	},
	{
		.type = INTEL_DVO_CHIP_LVDS,
		.name = "ivch",
		.dvo_reg = DVOA,
		.slave_addr = 0x02, /* Might also be 0x44, 0x84, 0xc4 */
		.dev_ops = &ivch_ops,
	},
	{
		.type = INTEL_DVO_CHIP_TMDS,
		.name = "tfp410",
		.dvo_reg = DVOC,
		.slave_addr = TFP410_ADDR,
		.dev_ops = &tfp410_ops,
	},
	{
		.type = INTEL_DVO_CHIP_LVDS,
		.name = "ch7017",
		.dvo_reg = DVOC,
		.slave_addr = 0x75,
		.gpio = GPIOE,
		.dev_ops = &ch7017_ops,
	}
};

static void intel_dvo_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_i915_private *dev_priv = encoder->dev->dev_private;
	struct intel_encoder *intel_encoder = enc_to_intel_encoder(encoder);
	struct intel_dvo_device *dvo = intel_encoder->dev_priv;
	u32 dvo_reg = dvo->dvo_reg;
	u32 temp = I915_READ(dvo_reg);

	if (mode == DRM_MODE_DPMS_ON) {
		I915_WRITE(dvo_reg, temp | DVO_ENABLE);
		I915_READ(dvo_reg);
		dvo->dev_ops->dpms(dvo, mode);
	} else {
		dvo->dev_ops->dpms(dvo, mode);
		I915_WRITE(dvo_reg, temp & ~DVO_ENABLE);
		I915_READ(dvo_reg);
	}
}

static void intel_dvo_save(struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_dvo_device *dvo = intel_encoder->dev_priv;

	/* Each output should probably just save the registers it touches,
	 * but for now, use more overkill.
	 */
	dev_priv->s3_state.saveDVOA = I915_READ(DVOA);
	dev_priv->s3_state.saveDVOB = I915_READ(DVOB);
	dev_priv->s3_state.saveDVOC = I915_READ(DVOC);

	dvo->dev_ops->save(dvo);
}

static void intel_dvo_restore(struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = connector->dev->dev_private;
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_dvo_device *dvo = intel_encoder->dev_priv;

	dvo->dev_ops->restore(dvo);

	I915_WRITE(DVOA, dev_priv->s3_state.saveDVOA);
	I915_WRITE(DVOB, dev_priv->s3_state.saveDVOB);
	I915_WRITE(DVOC, dev_priv->s3_state.saveDVOC);
}

static int intel_dvo_mode_valid(struct drm_connector *connector,
				struct drm_display_mode *mode)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_dvo_device *dvo = intel_encoder->dev_priv;

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN)
		return MODE_NO_DBLESCAN;

	/* XXX: Validate clock range */

	if (dvo->panel_fixed_mode) {
		if (mode->hdisplay > dvo->panel_fixed_mode->hdisplay)
			return MODE_PANEL;
		if (mode->vdisplay > dvo->panel_fixed_mode->vdisplay)
			return MODE_PANEL;
	}

	return dvo->dev_ops->mode_valid(dvo, mode);
}

static bool intel_dvo_mode_fixup(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	struct intel_encoder *intel_encoder = enc_to_intel_encoder(encoder);
	struct intel_dvo_device *dvo = intel_encoder->dev_priv;

	/* If we have timings from the BIOS for the panel, put them in
	 * to the adjusted mode.  The CRTC will be set up for this mode,
	 * with the panel scaling set up to source from the H/VDisplay
	 * of the original mode.
	 */
	if (dvo->panel_fixed_mode != NULL) {
#define C(x) adjusted_mode->x = dvo->panel_fixed_mode->x
		C(hdisplay);
		C(hsync_start);
		C(hsync_end);
		C(htotal);
		C(vdisplay);
		C(vsync_start);
		C(vsync_end);
		C(vtotal);
		C(clock);
		drm_mode_set_crtcinfo(adjusted_mode, CRTC_INTERLACE_HALVE_V);
#undef C
	}

	if (dvo->dev_ops->mode_fixup)
		return dvo->dev_ops->mode_fixup(dvo, mode, adjusted_mode);

	return true;
}

static void intel_dvo_mode_set(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	struct intel_encoder *intel_encoder = enc_to_intel_encoder(encoder);
	struct intel_dvo_device *dvo = intel_encoder->dev_priv;
	int pipe = intel_crtc->pipe;
	u32 dvo_val;
	u32 dvo_reg = dvo->dvo_reg, dvo_srcdim_reg;
	int dpll_reg = (pipe == 0) ? DPLL_A : DPLL_B;

	switch (dvo_reg) {
	case DVOA:
	default:
		dvo_srcdim_reg = DVOA_SRCDIM;
		break;
	case DVOB:
		dvo_srcdim_reg = DVOB_SRCDIM;
		break;
	case DVOC:
		dvo_srcdim_reg = DVOC_SRCDIM;
		break;
	}

	dvo->dev_ops->mode_set(dvo, mode, adjusted_mode);

	/* Save the data order, since I don't know what it should be set to. */
	dvo_val = I915_READ(dvo_reg) &
		  (DVO_PRESERVE_MASK | DVO_DATA_ORDER_GBRG);
	dvo_val |= DVO_DATA_ORDER_FP | DVO_BORDER_ENABLE |
		   DVO_BLANK_ACTIVE_HIGH;

	if (pipe == 1)
		dvo_val |= DVO_PIPE_B_SELECT;
	dvo_val |= DVO_PIPE_STALL;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PHSYNC)
		dvo_val |= DVO_HSYNC_ACTIVE_HIGH;
	if (adjusted_mode->flags & DRM_MODE_FLAG_PVSYNC)
		dvo_val |= DVO_VSYNC_ACTIVE_HIGH;

	I915_WRITE(dpll_reg, I915_READ(dpll_reg) | DPLL_DVO_HIGH_SPEED);

	/*I915_WRITE(DVOB_SRCDIM,
	  (adjusted_mode->hdisplay << DVO_SRCDIM_HORIZONTAL_SHIFT) |
	  (adjusted_mode->VDisplay << DVO_SRCDIM_VERTICAL_SHIFT));*/
	I915_WRITE(dvo_srcdim_reg,
		   (adjusted_mode->hdisplay << DVO_SRCDIM_HORIZONTAL_SHIFT) |
		   (adjusted_mode->vdisplay << DVO_SRCDIM_VERTICAL_SHIFT));
	/*I915_WRITE(DVOB, dvo_val);*/
	I915_WRITE(dvo_reg, dvo_val);
}

/**
 * Detect the output connection on our DVO device.
 *
 * Unimplemented.
 */
static enum drm_connector_status intel_dvo_detect(struct drm_connector *connector)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_dvo_device *dvo = intel_encoder->dev_priv;

	return dvo->dev_ops->detect(dvo);
}

static int intel_dvo_get_modes(struct drm_connector *connector)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_dvo_device *dvo = intel_encoder->dev_priv;

	/* We should probably have an i2c driver get_modes function for those
	 * devices which will have a fixed set of modes determined by the chip
	 * (TV-out, for example), but for now with just TMDS and LVDS,
	 * that's not the case.
	 */
	(void) intel_ddc_get_modes(intel_encoder);
	if (!list_empty(&connector->probed_modes))
		return 1;


	if (dvo->panel_fixed_mode != NULL) {
		struct drm_display_mode *mode;
		mode = drm_mode_duplicate(connector->dev, dvo->panel_fixed_mode);
		if (mode) {
			drm_mode_probed_add(connector, mode);
			return 1;
		}
	}
	return 0;
}

static void intel_dvo_destroy (struct drm_connector *connector)
{
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_dvo_device *dvo = intel_encoder->dev_priv;

	if (dvo) {
		if (dvo->dev_ops->destroy)
			dvo->dev_ops->destroy(dvo);
		if (dvo->panel_fixed_mode)
			kfree(dvo->panel_fixed_mode, sizeof (*(dvo->panel_fixed_mode)));
		/* no need, in i830_dvoices[] now */
		//kfree(dvo);
	}
	if (intel_encoder->i2c_bus)
		intel_i2c_destroy(intel_encoder->i2c_bus);
	if (intel_encoder->ddc_bus)
		intel_i2c_destroy(intel_encoder->ddc_bus);
	/* OSOL_i915: drm_sysfs_connector_remove(connector); */
	drm_connector_cleanup(connector);
	kfree(intel_encoder, sizeof (struct intel_encoder));
}

#ifdef RANDR_GET_CRTC_INTERFACE
static struct drm_crtc *intel_dvo_get_crtc(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_dvo_device *dvo = intel_encoder->dev_priv;
	int pipe = !!(I915_READ(dvo->dvo_reg) & SDVO_PIPE_B_SELECT);

	return intel_pipe_to_crtc(pScrn, pipe);
}
#endif

static const struct drm_encoder_helper_funcs intel_dvo_helper_funcs = {
	.dpms = intel_dvo_dpms,
	.mode_fixup = intel_dvo_mode_fixup,
	.prepare = intel_encoder_prepare,
	.mode_set = intel_dvo_mode_set,
	.commit = intel_encoder_commit,
};

static const struct drm_connector_funcs intel_dvo_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = intel_dvo_save,
	.restore = intel_dvo_restore,
	.detect = intel_dvo_detect,
	.destroy = intel_dvo_destroy,
	.fill_modes = drm_helper_probe_single_connector_modes,
};

static const struct drm_connector_helper_funcs intel_dvo_connector_helper_funcs = {
	.mode_valid = intel_dvo_mode_valid,
	.get_modes = intel_dvo_get_modes,
	.best_encoder = intel_best_encoder,
};

static void intel_dvo_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs intel_dvo_enc_funcs = {
	.destroy = intel_dvo_enc_destroy,
};


/**
 * Attempts to get a fixed panel timing for LVDS (currently only the i830).
 *
 * Other chips with DVO LVDS will need to extend this to deal with the LVDS
 * chip being on DVOB/C and having multiple pipes.
 */
static struct drm_display_mode *
intel_dvo_get_current_mode (struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_encoder *intel_encoder = to_intel_encoder(connector);
	struct intel_dvo_device *dvo = intel_encoder->dev_priv;
	uint32_t dvo_reg = dvo->dvo_reg;
	uint32_t dvo_val = I915_READ(dvo_reg);
	struct drm_display_mode *mode = NULL;

	/* If the DVO port is active, that'll be the LVDS, so we can pull out
	 * its timings to get how the BIOS set up the panel.
	 */
	if (dvo_val & DVO_ENABLE) {
		struct drm_crtc *crtc;
		int pipe = (dvo_val & DVO_PIPE_B_SELECT) ? 1 : 0;

		crtc = intel_get_crtc_from_pipe(dev, pipe);
		if (crtc) {
			mode = intel_crtc_mode_get(dev, crtc);

			if (mode) {
				mode->type |= DRM_MODE_TYPE_PREFERRED;
				if (dvo_val & DVO_HSYNC_ACTIVE_HIGH)
					mode->flags |= DRM_MODE_FLAG_PHSYNC;
				if (dvo_val & DVO_VSYNC_ACTIVE_HIGH)
					mode->flags |= DRM_MODE_FLAG_PVSYNC;
			}
		}
	}
	return mode;
}

void intel_dvo_init(struct drm_device *dev)
{
	struct intel_encoder *intel_encoder;
	struct intel_dvo_device *dvo;
	struct i2c_adapter *i2cbus = NULL;
	int ret = 0;
	int i;
	int encoder_type = DRM_MODE_ENCODER_NONE;
	intel_encoder = kzalloc (sizeof(struct intel_encoder), GFP_KERNEL);
	if (!intel_encoder)
		return;

	/* Set up the DDC bus */
	intel_encoder->ddc_bus = intel_i2c_create(dev, GPIOD, "DVODDC_D");
	if (!intel_encoder->ddc_bus)
		goto free_intel;

	/* Now, try to find a controller */
	for (i = 0; i < ARRAY_SIZE(intel_dvo_devices); i++) {
		struct drm_connector *connector = &intel_encoder->base;
		int gpio;

		dvo = &intel_dvo_devices[i];

		/* Allow the I2C driver info to specify the GPIO to be used in
		 * special cases, but otherwise default to what's defined
		 * in the spec.
		 */
		if (dvo->gpio != 0)
			gpio = dvo->gpio;
		else if (dvo->type == INTEL_DVO_CHIP_LVDS)
			gpio = GPIOB;
		else
			gpio = GPIOE;

		/* Set up the I2C bus necessary for the chip we're probing.
		 * It appears that everything is on GPIOE except for panels
		 * on i830 laptops, which are on GPIOB (DVOA).
		 */
		if (i2cbus != NULL)
			intel_i2c_destroy(i2cbus);
		if (!(i2cbus = intel_i2c_create(dev, gpio,
			gpio == GPIOB ? "DVOI2C_B" : "DVOI2C_E"))) {
			continue;
		}

		if (dvo->dev_ops!= NULL)
			ret = dvo->dev_ops->init(dvo, i2cbus);
		else
			ret = false;

		if (!ret)
			continue;

		intel_encoder->type = INTEL_OUTPUT_DVO;
		intel_encoder->crtc_mask = (1 << 0) | (1 << 1);
		switch (dvo->type) {
		case INTEL_DVO_CHIP_TMDS:
			intel_encoder->clone_mask =
				(1 << INTEL_DVO_TMDS_CLONE_BIT) |
				(1 << INTEL_ANALOG_CLONE_BIT);
			drm_connector_init(dev, connector,
					   &intel_dvo_connector_funcs,
					   DRM_MODE_CONNECTOR_DVII);
			encoder_type = DRM_MODE_ENCODER_TMDS;
			break;
		case INTEL_DVO_CHIP_LVDS:
			intel_encoder->clone_mask =
				(1 << INTEL_DVO_LVDS_CLONE_BIT);
			drm_connector_init(dev, connector,
					   &intel_dvo_connector_funcs,
					   DRM_MODE_CONNECTOR_LVDS);
			encoder_type = DRM_MODE_ENCODER_LVDS;
			break;
		}

		(void) drm_connector_helper_add(connector,
					 &intel_dvo_connector_helper_funcs);
		connector->display_info.subpixel_order = SubPixelHorizontalRGB;
		connector->interlace_allowed = false;
		connector->doublescan_allowed = false;

		intel_encoder->dev_priv = dvo;
		intel_encoder->i2c_bus = i2cbus;

		drm_encoder_init(dev, &intel_encoder->enc,
				 &intel_dvo_enc_funcs, encoder_type);
		drm_encoder_helper_add(&intel_encoder->enc,
				       &intel_dvo_helper_funcs);

		(void) drm_mode_connector_attach_encoder(&intel_encoder->base,
						  &intel_encoder->enc);
		if (dvo->type == INTEL_DVO_CHIP_LVDS) {
			/* For our LVDS chipsets, we should hopefully be able
			 * to dig the fixed panel mode out of the BIOS data.
			 * However, it's in a different format from the BIOS
			 * data on chipsets with integrated LVDS (stored in AIM
			 * headers, likely), so for now, just get the current
			 * mode being output through DVO.
			 */
			dvo->panel_fixed_mode =
				intel_dvo_get_current_mode(connector);
			dvo->panel_wants_dither = true;
		}

		/* OSOL_i915: drm_sysfs_connector_add(connector); */
		return;
	}

	intel_i2c_destroy(intel_encoder->ddc_bus);
	/* Didn't find a chip, so tear down. */
	if (i2cbus != NULL)
		intel_i2c_destroy(i2cbus);
free_intel:
	kfree(intel_encoder, sizeof (struct intel_encoder));
}
