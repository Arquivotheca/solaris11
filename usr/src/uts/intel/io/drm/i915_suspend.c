/* BEGIN CSTYLED */

/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *
 * Copyright 2008 (c) Intel Corporation
 *   Jesse Barnes <jbarnes@virtuousgeek.org>
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
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "intel_drv.h"

static bool i915_pipe_enabled(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32	dpll_reg;

	if (HAS_PCH_SPLIT(dev)) {
		dpll_reg = (pipe == PIPE_A) ? PCH_DPLL_A: PCH_DPLL_B;
	} else {
		dpll_reg = (pipe == PIPE_A) ? DPLL_A: DPLL_B;
	}

	return (I915_READ(dpll_reg) & DPLL_VCO_ENABLE);
}

static void i915_save_palette(struct drm_device *dev, enum pipe pipe, struct drm_i915_hw_state *state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long reg = (pipe == PIPE_A ? PALETTE_A : PALETTE_B);
	u32 *array;
	int i;

	if (!i915_pipe_enabled(dev, pipe))
		return;

	if (HAS_PCH_SPLIT(dev))
		reg = (pipe == PIPE_A) ? LGC_PALETTE_A : LGC_PALETTE_B;

	if (pipe == PIPE_A)
		array = state->save_palette_a;
	else
		array = state->save_palette_b;

	for(i = 0; i < 256; i++)
		array[i] = I915_READ(reg + (i << 2));
}

static void i915_restore_palette(struct drm_device *dev, enum pipe pipe, struct drm_i915_hw_state *state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long reg = (pipe == PIPE_A ? PALETTE_A : PALETTE_B);
	u32 *array;
	int i;

	if (!i915_pipe_enabled(dev, pipe))
		return;

	if (HAS_PCH_SPLIT(dev))
		reg = (pipe == PIPE_A) ? LGC_PALETTE_A : LGC_PALETTE_B;

	if (pipe == PIPE_A)
		array = state->save_palette_a;
	else
		array = state->save_palette_b;

	for(i = 0; i < 256; i++)
		I915_WRITE(reg + (i << 2), array[i]);
}

static u8 i915_read_indexed(struct drm_device *dev, u16 index_port, u16 data_port, u8 reg)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE8(index_port, reg);
	return I915_READ8(data_port);
}

static u8 i915_read_ar(struct drm_device *dev, u16 st01, u8 reg, u16 palette_enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_READ8(st01);
	I915_WRITE8(VGA_AR_INDEX, palette_enable | reg);
	return I915_READ8(VGA_AR_DATA_READ);
}

static void i915_write_ar(struct drm_device *dev, u16 st01, u8 reg, u8 val, u16 palette_enable)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_READ8(st01);
	I915_WRITE8(VGA_AR_INDEX, palette_enable | reg);
	I915_WRITE8(VGA_AR_DATA_WRITE, val);
}

static void i915_write_indexed(struct drm_device *dev, u16 index_port, u16 data_port, u8 reg, u8 val)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	I915_WRITE8(index_port, reg);
	I915_WRITE8(data_port, val);
}

static void i915_save_vga(struct drm_device *dev, struct drm_i915_hw_state *state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;
	u16 cr_index, cr_data, st01;

	/* VGA color palette registers */
	state->saveDACMASK = I915_READ8(VGA_DACMASK);

	/* MSR bits */
	state->saveMSR = I915_READ8(VGA_MSR_READ);
	if (state->saveMSR & VGA_MSR_CGA_MODE) {
		cr_index = VGA_CR_INDEX_CGA;
		cr_data = VGA_CR_DATA_CGA;
		st01 = VGA_ST01_CGA;
	} else {
		cr_index = VGA_CR_INDEX_MDA;
		cr_data = VGA_CR_DATA_MDA;
		st01 = VGA_ST01_MDA;
	}

	/* CRT controller regs */
	i915_write_indexed(dev, cr_index, cr_data, 0x11,
			   i915_read_indexed(dev, cr_index, cr_data, 0x11) &
			   (~0x80));
	for (i = 0; i <= 0x24; i++)
		state->saveCR[i] =
			i915_read_indexed(dev, cr_index, cr_data, i);
	/* Make sure we don't turn off CR group 0 writes */
	state->saveCR[0x11] &= ~0x80;

	/* Attribute controller registers */
	I915_READ8(st01);
	state->saveAR_INDEX = I915_READ8(VGA_AR_INDEX);
	for (i = 0; i <= 0x14; i++)
		state->saveAR[i] = i915_read_ar(dev, st01, i, 0);
	I915_READ8(st01);
	I915_WRITE8(VGA_AR_INDEX, state->saveAR_INDEX);
	I915_READ8(st01);

	/* Graphics controller registers */
	for (i = 0; i < 9; i++)
		state->saveGR[i] =
			i915_read_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, i);

	state->saveGR[0x10] =
		i915_read_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x10);
	state->saveGR[0x11] =
		i915_read_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x11);
	state->saveGR[0x18] =
		i915_read_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x18);

	/* Sequencer registers */
	for (i = 0; i < 8; i++)
		state->saveSR[i] =
			i915_read_indexed(dev, VGA_SR_INDEX, VGA_SR_DATA, i);
}

static void i915_restore_vga(struct drm_device *dev, struct drm_i915_hw_state *state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;
	u16 cr_index, cr_data, st01;

	/* MSR bits */
	I915_WRITE8(VGA_MSR_WRITE, state->saveMSR);
	if (state->saveMSR & VGA_MSR_CGA_MODE) {
		cr_index = VGA_CR_INDEX_CGA;
		cr_data = VGA_CR_DATA_CGA;
		st01 = VGA_ST01_CGA;
	} else {
		cr_index = VGA_CR_INDEX_MDA;
		cr_data = VGA_CR_DATA_MDA;
		st01 = VGA_ST01_MDA;
	}

	/* Sequencer registers, don't write SR07 */
	for (i = 0; i < 7; i++)
		i915_write_indexed(dev, VGA_SR_INDEX, VGA_SR_DATA, i,
				   state->saveSR[i]);

	/* CRT controller regs */
	/* Enable CR group 0 writes */
	i915_write_indexed(dev, cr_index, cr_data, 0x11, state->saveCR[0x11]);
	for (i = 0; i <= 0x24; i++)
		i915_write_indexed(dev, cr_index, cr_data, i, state->saveCR[i]);

	/* Graphics controller regs */
	for (i = 0; i < 9; i++)
		i915_write_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, i,
				   state->saveGR[i]);

	i915_write_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x10,
			   state->saveGR[0x10]);
	i915_write_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x11,
			   state->saveGR[0x11]);
	i915_write_indexed(dev, VGA_GR_INDEX, VGA_GR_DATA, 0x18,
			   state->saveGR[0x18]);

	/* Attribute controller registers */
	I915_READ8(st01); /* switch back to index mode */
	for (i = 0; i <= 0x14; i++)
		i915_write_ar(dev, st01, i, state->saveAR[i], 0);
	I915_READ8(st01); /* switch back to index mode */
	I915_WRITE8(VGA_AR_INDEX, state->saveAR_INDEX | 0x20);
	I915_READ8(st01);

	/* VGA color palette registers */
	I915_WRITE8(VGA_DACMASK, state->saveDACMASK);
}

static void i915_save_modeset_reg(struct drm_device *dev, struct drm_i915_hw_state *state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (drm_core_check_feature(dev, DRIVER_MODESET) && (state == &dev_priv->s3_state) && !IS_I965GM(dev))
		return;

	if (HAS_PCH_SPLIT(dev)) {
		state->savePCH_DREF_CONTROL = I915_READ(PCH_DREF_CONTROL);
		state->saveDISP_ARB_CTL = I915_READ(DISP_ARB_CTL);
	}

	/* Pipe & plane A info */
	state->savePIPEACONF = I915_READ(PIPEACONF);
	state->savePIPEASRC = I915_READ(PIPEASRC);
	if (HAS_PCH_SPLIT(dev)) {
		state->saveFPA0 = I915_READ(PCH_FPA0);
		state->saveFPA1 = I915_READ(PCH_FPA1);
		state->saveDPLL_A = I915_READ(PCH_DPLL_A);
	} else {
		state->saveFPA0 = I915_READ(FPA0);
		state->saveFPA1 = I915_READ(FPA1);
		state->saveDPLL_A = I915_READ(DPLL_A);
	}
	if (IS_I965G(dev) && !HAS_PCH_SPLIT(dev))
		state->saveDPLL_A_MD = I915_READ(DPLL_A_MD);
	state->saveHTOTAL_A = I915_READ(HTOTAL_A);
	state->saveHBLANK_A = I915_READ(HBLANK_A);
	state->saveHSYNC_A = I915_READ(HSYNC_A);
	state->saveVTOTAL_A = I915_READ(VTOTAL_A);
	state->saveVBLANK_A = I915_READ(VBLANK_A);
	state->saveVSYNC_A = I915_READ(VSYNC_A);
	if (!HAS_PCH_SPLIT(dev))
		state->saveBCLRPAT_A = I915_READ(BCLRPAT_A);

	if (HAS_PCH_SPLIT(dev)) {
		state->savePIPEA_DATA_M1 = I915_READ(PIPEA_DATA_M1);
		state->savePIPEA_DATA_N1 = I915_READ(PIPEA_DATA_N1);
		state->savePIPEA_LINK_M1 = I915_READ(PIPEA_LINK_M1);
		state->savePIPEA_LINK_N1 = I915_READ(PIPEA_LINK_N1);

		state->saveFDI_TXA_CTL = I915_READ(FDI_TXA_CTL);
		state->saveFDI_RXA_CTL = I915_READ(FDI_RXA_CTL);

		state->savePFA_CTL_1 = I915_READ(PFA_CTL_1);
		state->savePFA_WIN_SZ = I915_READ(PFA_WIN_SZ);
		state->savePFA_WIN_POS = I915_READ(PFA_WIN_POS);

		state->saveTRANSACONF = I915_READ(TRANSACONF);
		state->saveTRANS_HTOTAL_A = I915_READ(TRANS_HTOTAL_A);
		state->saveTRANS_HBLANK_A = I915_READ(TRANS_HBLANK_A);
		state->saveTRANS_HSYNC_A = I915_READ(TRANS_HSYNC_A);
		state->saveTRANS_VTOTAL_A = I915_READ(TRANS_VTOTAL_A);
		state->saveTRANS_VBLANK_A = I915_READ(TRANS_VBLANK_A);
		state->saveTRANS_VSYNC_A = I915_READ(TRANS_VSYNC_A);
	}

	state->saveDSPACNTR = I915_READ(DSPACNTR);
	state->saveDSPASTRIDE = I915_READ(DSPASTRIDE);
	state->saveDSPASIZE = I915_READ(DSPASIZE);
	state->saveDSPAPOS = I915_READ(DSPAPOS);
	state->saveDSPAADDR = I915_READ(DSPAADDR);
	if (IS_I965G(dev)) {
		state->saveDSPASURF = I915_READ(DSPASURF);
		state->saveDSPATILEOFF = I915_READ(DSPATILEOFF);
	}
	i915_save_palette(dev, PIPE_A, state);
	state->savePIPEASTAT = I915_READ(PIPEASTAT);

	/* Pipe & plane B info */
	state->savePIPEBCONF = I915_READ(PIPEBCONF);
	state->savePIPEBSRC = I915_READ(PIPEBSRC);
	if (HAS_PCH_SPLIT(dev)) {
		state->saveFPB0 = I915_READ(PCH_FPB0);
		state->saveFPB1 = I915_READ(PCH_FPB1);
		state->saveDPLL_B = I915_READ(PCH_DPLL_B);
	} else {
		state->saveFPB0 = I915_READ(FPB0);
		state->saveFPB1 = I915_READ(FPB1);
		state->saveDPLL_B = I915_READ(DPLL_B);
	}
	if (IS_I965G(dev) && !HAS_PCH_SPLIT(dev))
		state->saveDPLL_B_MD = I915_READ(DPLL_B_MD);
	state->saveHTOTAL_B = I915_READ(HTOTAL_B);
	state->saveHBLANK_B = I915_READ(HBLANK_B);
	state->saveHSYNC_B = I915_READ(HSYNC_B);
	state->saveVTOTAL_B = I915_READ(VTOTAL_B);
	state->saveVBLANK_B = I915_READ(VBLANK_B);
	state->saveVSYNC_B = I915_READ(VSYNC_B);
	if (!HAS_PCH_SPLIT(dev))
		state->saveBCLRPAT_B = I915_READ(BCLRPAT_B);

	if (HAS_PCH_SPLIT(dev)) {
		state->savePIPEB_DATA_M1 = I915_READ(PIPEB_DATA_M1);
		state->savePIPEB_DATA_N1 = I915_READ(PIPEB_DATA_N1);
		state->savePIPEB_LINK_M1 = I915_READ(PIPEB_LINK_M1);
		state->savePIPEB_LINK_N1 = I915_READ(PIPEB_LINK_N1);

		state->saveFDI_TXB_CTL = I915_READ(FDI_TXB_CTL);
		state->saveFDI_RXB_CTL = I915_READ(FDI_RXB_CTL);

		state->savePFB_CTL_1 = I915_READ(PFB_CTL_1);
		state->savePFB_WIN_SZ = I915_READ(PFB_WIN_SZ);
		state->savePFB_WIN_POS = I915_READ(PFB_WIN_POS);

		state->saveTRANSBCONF = I915_READ(TRANSBCONF);
		state->saveTRANS_HTOTAL_B = I915_READ(TRANS_HTOTAL_B);
		state->saveTRANS_HBLANK_B = I915_READ(TRANS_HBLANK_B);
		state->saveTRANS_HSYNC_B = I915_READ(TRANS_HSYNC_B);
		state->saveTRANS_VTOTAL_B = I915_READ(TRANS_VTOTAL_B);
		state->saveTRANS_VBLANK_B = I915_READ(TRANS_VBLANK_B);
		state->saveTRANS_VSYNC_B = I915_READ(TRANS_VSYNC_B);
	}

	state->saveDSPBCNTR = I915_READ(DSPBCNTR);
	state->saveDSPBSTRIDE = I915_READ(DSPBSTRIDE);
	state->saveDSPBSIZE = I915_READ(DSPBSIZE);
	state->saveDSPBPOS = I915_READ(DSPBPOS);
	state->saveDSPBADDR = I915_READ(DSPBADDR);
	if (IS_I965GM(dev) || IS_GM45(dev)) {
		state->saveDSPBSURF = I915_READ(DSPBSURF);
		state->saveDSPBTILEOFF = I915_READ(DSPBTILEOFF);
	}
	i915_save_palette(dev, PIPE_B, state);
	state->savePIPEBSTAT = I915_READ(PIPEBSTAT);
	return;
}

static void i915_restore_modeset_reg(struct drm_device *dev, struct drm_i915_hw_state *state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int dpll_a_reg, fpa0_reg, fpa1_reg;
	int dpll_b_reg, fpb0_reg, fpb1_reg;

	if (drm_core_check_feature(dev, DRIVER_MODESET) && (state == &dev_priv->s3_state) && !IS_I965GM(dev))
		return;

	if (HAS_PCH_SPLIT(dev)) {
		dpll_a_reg = PCH_DPLL_A;
		dpll_b_reg = PCH_DPLL_B;
		fpa0_reg = PCH_FPA0;
		fpb0_reg = PCH_FPB0;
		fpa1_reg = PCH_FPA1;
		fpb1_reg = PCH_FPB1;
	} else {
		dpll_a_reg = DPLL_A;
		dpll_b_reg = DPLL_B;
		fpa0_reg = FPA0;
		fpb0_reg = FPB0;
		fpa1_reg = FPA1;
		fpb1_reg = FPB1;
	}

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(PCH_DREF_CONTROL, state->savePCH_DREF_CONTROL);
		I915_WRITE(DISP_ARB_CTL, state->saveDISP_ARB_CTL);
	}

	/* Pipe & plane A info */
	/* Prime the clock */
	if (state->saveDPLL_A & DPLL_VCO_ENABLE) {
		I915_WRITE(dpll_a_reg, state->saveDPLL_A &
			   ~DPLL_VCO_ENABLE);
		DRM_UDELAY(150);
	}
	I915_WRITE(fpa0_reg, state->saveFPA0);
	I915_WRITE(fpa1_reg, state->saveFPA1);
	/* Actually enable it */
	I915_WRITE(dpll_a_reg, state->saveDPLL_A);
	DRM_UDELAY(150);
	if (IS_I965G(dev) && !HAS_PCH_SPLIT(dev))
		I915_WRITE(DPLL_A_MD, state->saveDPLL_A_MD);
	DRM_UDELAY(150);

	/* Restore mode */
	I915_WRITE(HTOTAL_A, state->saveHTOTAL_A);
	I915_WRITE(HBLANK_A, state->saveHBLANK_A);
	I915_WRITE(HSYNC_A, state->saveHSYNC_A);
	I915_WRITE(VTOTAL_A, state->saveVTOTAL_A);
	I915_WRITE(VBLANK_A, state->saveVBLANK_A);
	I915_WRITE(VSYNC_A, state->saveVSYNC_A);
	if (!HAS_PCH_SPLIT(dev))
		I915_WRITE(BCLRPAT_A, state->saveBCLRPAT_A);

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(PIPEA_DATA_M1, state->savePIPEA_DATA_M1);
		I915_WRITE(PIPEA_DATA_N1, state->savePIPEA_DATA_N1);
		I915_WRITE(PIPEA_LINK_M1, state->savePIPEA_LINK_M1);
		I915_WRITE(PIPEA_LINK_N1, state->savePIPEA_LINK_N1);
		/* FDI need re-train */
/*
		I915_WRITE(FDI_RXA_CTL, state->saveFDI_RXA_CTL);
		I915_WRITE(FDI_TXA_CTL, state->saveFDI_TXA_CTL);

		I915_WRITE(PFA_CTL_1, state->savePFA_CTL_1);
		I915_WRITE(PFA_WIN_SZ, state->savePFA_WIN_SZ);
		I915_WRITE(PFA_WIN_POS, state->savePFA_WIN_POS);

		I915_WRITE(TRANSACONF, state->saveTRANSACONF);
		I915_WRITE(TRANS_HTOTAL_A, state->saveTRANS_HTOTAL_A);
		I915_WRITE(TRANS_HBLANK_A, state->saveTRANS_HBLANK_A);
		I915_WRITE(TRANS_HSYNC_A, state->saveTRANS_HSYNC_A);
		I915_WRITE(TRANS_VTOTAL_A, state->saveTRANS_VTOTAL_A);
		I915_WRITE(TRANS_VBLANK_A, state->saveTRANS_VBLANK_A);
		I915_WRITE(TRANS_VSYNC_A, state->saveTRANS_VSYNC_A);
*/
	}

	/* Restore plane info */
	I915_WRITE(DSPASIZE, state->saveDSPASIZE);
	I915_WRITE(DSPAPOS, state->saveDSPAPOS);
	I915_WRITE(PIPEASRC, state->savePIPEASRC);
	I915_WRITE(DSPAADDR, state->saveDSPAADDR);
	I915_WRITE(DSPASTRIDE, state->saveDSPASTRIDE);
	if (IS_I965G(dev)) {
		I915_WRITE(DSPASURF, state->saveDSPASURF);
		I915_WRITE(DSPATILEOFF, state->saveDSPATILEOFF);
	}

	I915_WRITE(PIPEACONF, state->savePIPEACONF);
	i915_restore_palette(dev, PIPE_A, state);
	/* Enable the plane */
	I915_WRITE(DSPACNTR, state->saveDSPACNTR);
	I915_WRITE(DSPAADDR, I915_READ(DSPAADDR));

	/* Pipe & plane B info */
	if (state->saveDPLL_B & DPLL_VCO_ENABLE) {
		I915_WRITE(dpll_b_reg, state->saveDPLL_B &
			   ~DPLL_VCO_ENABLE);
		DRM_UDELAY(150);
	}
	I915_WRITE(fpb0_reg, state->saveFPB0);
	I915_WRITE(fpb1_reg, state->saveFPB1);
	/* Actually enable it */
	I915_WRITE(dpll_b_reg, state->saveDPLL_B);
	DRM_UDELAY(150);
	if (IS_I965G(dev) && !HAS_PCH_SPLIT(dev))
		I915_WRITE(DPLL_B_MD, state->saveDPLL_B_MD);
	DRM_UDELAY(150);

	/* Restore mode */
	I915_WRITE(HTOTAL_B, state->saveHTOTAL_B);
	I915_WRITE(HBLANK_B, state->saveHBLANK_B);
	I915_WRITE(HSYNC_B, state->saveHSYNC_B);
	I915_WRITE(VTOTAL_B, state->saveVTOTAL_B);
	I915_WRITE(VBLANK_B, state->saveVBLANK_B);
	I915_WRITE(VSYNC_B, state->saveVSYNC_B);
	if (!HAS_PCH_SPLIT(dev))
		I915_WRITE(BCLRPAT_B, state->saveBCLRPAT_B);

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(PIPEB_DATA_M1, state->savePIPEB_DATA_M1);
		I915_WRITE(PIPEB_DATA_N1, state->savePIPEB_DATA_N1);
		I915_WRITE(PIPEB_LINK_M1, state->savePIPEB_LINK_M1);
		I915_WRITE(PIPEB_LINK_N1, state->savePIPEB_LINK_N1);

		/* FDI need re-train */
/*
		I915_WRITE(FDI_RXB_CTL, state->saveFDI_RXB_CTL);
		I915_WRITE(FDI_TXB_CTL, state->saveFDI_TXB_CTL);

		I915_WRITE(PFB_CTL_1, state->savePFB_CTL_1);
		I915_WRITE(PFB_WIN_SZ, state->savePFB_WIN_SZ);
		I915_WRITE(PFB_WIN_POS, state->savePFB_WIN_POS);

		I915_WRITE(TRANSBCONF, state->saveTRANSBCONF);
		I915_WRITE(TRANS_HTOTAL_B, state->saveTRANS_HTOTAL_B);
		I915_WRITE(TRANS_HBLANK_B, state->saveTRANS_HBLANK_B);
		I915_WRITE(TRANS_HSYNC_B, state->saveTRANS_HSYNC_B);
		I915_WRITE(TRANS_VTOTAL_B, state->saveTRANS_VTOTAL_B);
		I915_WRITE(TRANS_VBLANK_B, state->saveTRANS_VBLANK_B);
		I915_WRITE(TRANS_VSYNC_B, state->saveTRANS_VSYNC_B);
*/
	}

	/* Restore plane info */
	I915_WRITE(DSPBSIZE, state->saveDSPBSIZE);
	I915_WRITE(DSPBPOS, state->saveDSPBPOS);
	I915_WRITE(PIPEBSRC, state->savePIPEBSRC);
	I915_WRITE(DSPBADDR, state->saveDSPBADDR);
	I915_WRITE(DSPBSTRIDE, state->saveDSPBSTRIDE);
	if (IS_I965G(dev)) {
		I915_WRITE(DSPBSURF, state->saveDSPBSURF);
		I915_WRITE(DSPBTILEOFF, state->saveDSPBTILEOFF);
	}

	I915_WRITE(PIPEBCONF, state->savePIPEBCONF);

	i915_restore_palette(dev, PIPE_B, state);
	/* Enable the plane */
	I915_WRITE(DSPBCNTR, state->saveDSPBCNTR);
	I915_WRITE(DSPBADDR, I915_READ(DSPBADDR));

	return;
}

void i915_save_display(struct drm_device *dev, struct drm_i915_hw_state *state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* Display arbitration control */
	state->saveDSPARB = I915_READ(DSPARB);

	/* This is only meaningful in non-KMS mode */
	/* Don't save them in KMS mode */
	i915_save_modeset_reg(dev, state);

	/* Cursor state */
	state->saveCURACNTR = I915_READ(CURACNTR);
	state->saveCURAPOS = I915_READ(CURAPOS);
	state->saveCURABASE = I915_READ(CURABASE);
	state->saveCURBCNTR = I915_READ(CURBCNTR);
	state->saveCURBPOS = I915_READ(CURBPOS);
	state->saveCURBBASE = I915_READ(CURBBASE);
	if (!IS_I9XX(dev))
		state->saveCURSIZE = I915_READ(CURSIZE);

	/* CRT state */
	if (HAS_PCH_SPLIT(dev)) {
		state->saveADPA = I915_READ(PCH_ADPA);
	} else {
		state->saveADPA = I915_READ(ADPA);
	}

	/* LVDS state */
	if (HAS_PCH_SPLIT(dev)) {
		state->savePP_CONTROL = I915_READ(PCH_PP_CONTROL);
		state->saveBLC_PWM_CTL = I915_READ(BLC_PWM_PCH_CTL1);
		state->saveBLC_PWM_CTL2 = I915_READ(BLC_PWM_PCH_CTL2);
		state->saveBLC_CPU_PWM_CTL = I915_READ(BLC_PWM_CPU_CTL);
		state->saveBLC_CPU_PWM_CTL2 = I915_READ(BLC_PWM_CPU_CTL2);
		state->saveLVDS = I915_READ(PCH_LVDS);
	} else {
		state->savePP_CONTROL = I915_READ(PP_CONTROL);
		state->savePFIT_PGM_RATIOS = I915_READ(PFIT_PGM_RATIOS);
		state->saveBLC_PWM_CTL = I915_READ(BLC_PWM_CTL);
		state->saveBLC_HIST_CTL = I915_READ(BLC_HIST_CTL);
		if (IS_I965G(dev))
			state->saveBLC_PWM_CTL2 = I915_READ(BLC_PWM_CTL2);
		if (IS_MOBILE(dev) && !IS_I830(dev))
			state->saveLVDS = I915_READ(LVDS);
	}

	if (!IS_I830(dev) && !IS_845G(dev) && !HAS_PCH_SPLIT(dev))
		state->savePFIT_CONTROL = I915_READ(PFIT_CONTROL);

	if (HAS_PCH_SPLIT(dev)) {
		state->savePP_ON_DELAYS = I915_READ(PCH_PP_ON_DELAYS);
		state->savePP_OFF_DELAYS = I915_READ(PCH_PP_OFF_DELAYS);
		state->savePP_DIVISOR = I915_READ(PCH_PP_DIVISOR);
	} else {
		state->savePP_ON_DELAYS = I915_READ(PP_ON_DELAYS);
		state->savePP_OFF_DELAYS = I915_READ(PP_OFF_DELAYS);
		state->savePP_DIVISOR = I915_READ(PP_DIVISOR);
	}

	/* Display Port state */
	if (SUPPORTS_INTEGRATED_DP(dev)) {
		state->saveDP_B = I915_READ(DP_B);
		state->saveDP_C = I915_READ(DP_C);
		state->saveDP_D = I915_READ(DP_D);
		state->savePIPEA_GMCH_DATA_M = I915_READ(PIPEA_GMCH_DATA_M);
		state->savePIPEB_GMCH_DATA_M = I915_READ(PIPEB_GMCH_DATA_M);
		state->savePIPEA_GMCH_DATA_N = I915_READ(PIPEA_GMCH_DATA_N);
		state->savePIPEB_GMCH_DATA_N = I915_READ(PIPEB_GMCH_DATA_N);
		state->savePIPEA_DP_LINK_M = I915_READ(PIPEA_DP_LINK_M);
		state->savePIPEB_DP_LINK_M = I915_READ(PIPEB_DP_LINK_M);
		state->savePIPEA_DP_LINK_N = I915_READ(PIPEA_DP_LINK_N);
		state->savePIPEB_DP_LINK_N = I915_READ(PIPEB_DP_LINK_N);
	}
	/* FIXME: save TV & SDVO state */

	/* FBC state */
	if (IS_GM45(dev)) {
		state->saveDPFC_CB_BASE = I915_READ(DPFC_CB_BASE);
	} else {
		state->saveFBC_CFB_BASE = I915_READ(FBC_CFB_BASE);
		state->saveFBC_LL_BASE = I915_READ(FBC_LL_BASE);
		state->saveFBC_CONTROL2 = I915_READ(FBC_CONTROL2);
		state->saveFBC_CONTROL = I915_READ(FBC_CONTROL);
	}

	/* VGA state */
	state->saveVGA0 = I915_READ(VGA0);
	state->saveVGA1 = I915_READ(VGA1);
	state->saveVGA_PD = I915_READ(VGA_PD);
	if (HAS_PCH_SPLIT(dev))
		state->saveVGACNTRL = I915_READ(CPU_VGACNTRL);
	else
		state->saveVGACNTRL = I915_READ(VGACNTRL);

	i915_save_vga(dev, state);
}

void i915_restore_display(struct drm_device *dev, struct drm_i915_hw_state *state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_crtc *crtc;

	if (drm_core_check_feature(dev, DRIVER_MODESET) && (state != &dev_priv->s3_state)) {
		list_for_each_entry(crtc, struct drm_crtc, &dev->mode_config.crtc_list, head) {
			dev_priv->display.dpms(crtc, DRM_MODE_DPMS_OFF);
		}
		DRM_UDELAY(30000);
	}

	/* Display arbitration */
	I915_WRITE(DSPARB, state->saveDSPARB);

	/* Display port ratios (must be done before clock is set) */
	if (SUPPORTS_INTEGRATED_DP(dev)) {
		I915_WRITE(PIPEA_GMCH_DATA_M, state->savePIPEA_GMCH_DATA_M);
		I915_WRITE(PIPEB_GMCH_DATA_M, state->savePIPEB_GMCH_DATA_M);
		I915_WRITE(PIPEA_GMCH_DATA_N, state->savePIPEA_GMCH_DATA_N);
		I915_WRITE(PIPEB_GMCH_DATA_N, state->savePIPEB_GMCH_DATA_N);
		I915_WRITE(PIPEA_DP_LINK_M, state->savePIPEA_DP_LINK_M);
		I915_WRITE(PIPEB_DP_LINK_M, state->savePIPEB_DP_LINK_M);
		I915_WRITE(PIPEA_DP_LINK_N, state->savePIPEA_DP_LINK_N);
		I915_WRITE(PIPEB_DP_LINK_N, state->savePIPEB_DP_LINK_N);
	}

	/* This is only meaningful in non-KMS mode */
	/* Don't restore them in KMS mode */
	i915_restore_modeset_reg(dev, state);

	/* Cursor state */
	I915_WRITE(CURAPOS, state->saveCURAPOS);
	I915_WRITE(CURACNTR, state->saveCURACNTR);
	I915_WRITE(CURABASE, state->saveCURABASE);
	I915_WRITE(CURBPOS, state->saveCURBPOS);
	I915_WRITE(CURBCNTR, state->saveCURBCNTR);
	I915_WRITE(CURBBASE, state->saveCURBBASE);
	if (!IS_I9XX(dev))
		I915_WRITE(CURSIZE, state->saveCURSIZE);

	/* CRT state */
	if (HAS_PCH_SPLIT(dev))
		I915_WRITE(PCH_ADPA, state->saveADPA);
	else
		I915_WRITE(ADPA, state->saveADPA);

	/* LVDS state */
	if (IS_I965G(dev) && !HAS_PCH_SPLIT(dev))
		I915_WRITE(BLC_PWM_CTL2, state->saveBLC_PWM_CTL2);

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(PCH_LVDS, state->saveLVDS);
	} else if (IS_MOBILE(dev) && !IS_I830(dev))
		I915_WRITE(LVDS, state->saveLVDS);

	if (!IS_I830(dev) && !IS_845G(dev) && !HAS_PCH_SPLIT(dev))
		I915_WRITE(PFIT_CONTROL, state->savePFIT_CONTROL);

	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(BLC_PWM_PCH_CTL1, state->saveBLC_PWM_CTL);
		I915_WRITE(BLC_PWM_PCH_CTL2, state->saveBLC_PWM_CTL2);
		I915_WRITE(BLC_PWM_CPU_CTL, state->saveBLC_CPU_PWM_CTL);
		I915_WRITE(BLC_PWM_CPU_CTL2, state->saveBLC_CPU_PWM_CTL2);
		I915_WRITE(PCH_PP_ON_DELAYS, state->savePP_ON_DELAYS);
		I915_WRITE(PCH_PP_OFF_DELAYS, state->savePP_OFF_DELAYS);
		I915_WRITE(PCH_PP_DIVISOR, state->savePP_DIVISOR);
		I915_WRITE(PCH_PP_CONTROL, state->savePP_CONTROL);
		I915_WRITE(MCHBAR_RENDER_STANDBY,
			   state->saveMCHBAR_RENDER_STANDBY);
	} else {
		I915_WRITE(PFIT_PGM_RATIOS, state->savePFIT_PGM_RATIOS);
		I915_WRITE(BLC_PWM_CTL, state->saveBLC_PWM_CTL);
		I915_WRITE(BLC_HIST_CTL, state->saveBLC_HIST_CTL);
		I915_WRITE(PP_ON_DELAYS, state->savePP_ON_DELAYS);
		I915_WRITE(PP_OFF_DELAYS, state->savePP_OFF_DELAYS);
		I915_WRITE(PP_DIVISOR, state->savePP_DIVISOR);
		I915_WRITE(PP_CONTROL, state->savePP_CONTROL);
	}

	if (IS_I965GM(dev)) {
		I915_WRITE(MCHBAR_RENDER_STANDBY,
			state->saveMCHBAR_RENDER_STANDBY);
		(void) I915_READ(MCHBAR_RENDER_STANDBY);
	}

	/* Display Port state */
	if (SUPPORTS_INTEGRATED_DP(dev)) {
		I915_WRITE(DP_B, state->saveDP_B);
		I915_WRITE(DP_C, state->saveDP_C);
		I915_WRITE(DP_D, state->saveDP_D);
	}
	/* FIXME: restore TV & SDVO state */

	/* FBC info */
	if (IS_GM45(dev)) {
		g4x_disable_fbc(dev);
		I915_WRITE(DPFC_CB_BASE, state->saveDPFC_CB_BASE);
	} else {
		i8xx_disable_fbc(dev);
		I915_WRITE(FBC_CFB_BASE, state->saveFBC_CFB_BASE);
		I915_WRITE(FBC_LL_BASE, state->saveFBC_LL_BASE);
		I915_WRITE(FBC_CONTROL2, state->saveFBC_CONTROL2);
		I915_WRITE(FBC_CONTROL, state->saveFBC_CONTROL);
	}

	/* VGA state */
	if (HAS_PCH_SPLIT(dev))
		I915_WRITE(CPU_VGACNTRL, state->saveVGACNTRL);
	else
		I915_WRITE(VGACNTRL, state->saveVGACNTRL);
	I915_WRITE(VGA0, state->saveVGA0);
	I915_WRITE(VGA1, state->saveVGA1);
	I915_WRITE(VGA_PD, state->saveVGA_PD);
	DRM_UDELAY(150);

	i915_restore_vga(dev, state);
}

int i915_save_state(struct drm_device *dev, struct drm_i915_hw_state *state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	(void) pci_read_config_byte(dev->pdev, LBB, &state->saveLBB);

	/* Hardware status page */
	state->saveHWS = I915_READ(HWS_PGA);

	i915_save_display(dev, state);

	/* Interrupt state */
	if (HAS_PCH_SPLIT(dev)) {
		state->saveDEIER = I915_READ(DEIER);
		state->saveDEIMR = I915_READ(DEIMR);
		state->saveGTIER = I915_READ(GTIER);
		state->saveGTIMR = I915_READ(GTIMR);
		state->saveFDI_RXA_IMR = I915_READ(FDI_RXA_IMR);
		state->saveFDI_RXB_IMR = I915_READ(FDI_RXB_IMR);
		state->saveMCHBAR_RENDER_STANDBY =
			I915_READ(MCHBAR_RENDER_STANDBY);
	} else {
		state->saveIER = I915_READ(IER);
		state->saveIMR = I915_READ(IMR);
	}

	if (IS_I965GM(dev))
		state->saveMCHBAR_RENDER_STANDBY =
			I915_READ(MCHBAR_RENDER_STANDBY);

	if (HAS_PCH_SPLIT(dev))
		ironlake_disable_drps(dev);

	/* Cache mode state */
	state->saveCACHE_MODE_0 = I915_READ(CACHE_MODE_0);

	/* Memory Arbitration state */
	state->saveMI_ARB_STATE = I915_READ(MI_ARB_STATE);

	/* Scratch space */
	for (i = 0; i < 16; i++) {
		state->saveSWF0[i] = I915_READ(SWF00 + (i << 2));
		state->saveSWF1[i] = I915_READ(SWF10 + (i << 2));
	}
	for (i = 0; i < 3; i++)
		state->saveSWF2[i] = I915_READ(SWF30 + (i << 2));

	/* Fences */
	if (IS_I965G(dev)) {
		for (i = 0; i < 16; i++)
			state->saveFENCE[i] = I915_READ64(FENCE_REG_965_0 + (i * 8));
	} else {
		for (i = 0; i < 8; i++)
			state->saveFENCE[i] = I915_READ(FENCE_REG_830_0 + (i * 4));

		if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
			for (i = 0; i < 8; i++)
				state->saveFENCE[i+8] = I915_READ(FENCE_REG_945_8 + (i * 4));
	}

	/*
	 * Save page table control register
	 */
	if (IS_I965GM(dev))
		state->pgtbl_ctl = I915_READ(PGTBL_CTL);

	return 0;
}

int i915_restore_state(struct drm_device *dev, struct drm_i915_hw_state *state)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	(void) pci_write_config_byte(dev->pdev, LBB, state->saveLBB);

	/* Hardware status page */
	I915_WRITE(HWS_PGA, state->saveHWS);
	if (IS_I965GM(dev))
		(void) I915_READ(HWS_PGA);

	/* Fences */
	if (IS_I965G(dev)) {
		for (i = 0; i < 16; i++)
			I915_WRITE64(FENCE_REG_965_0 + (i * 8), state->saveFENCE[i]);
	} else {
		for (i = 0; i < 8; i++)
			I915_WRITE(FENCE_REG_830_0 + (i * 4), state->saveFENCE[i]);
		if (IS_I945G(dev) || IS_I945GM(dev) || IS_G33(dev))
			for (i = 0; i < 8; i++)
				I915_WRITE(FENCE_REG_945_8 + (i * 4), state->saveFENCE[i+8]);
	}

	i915_restore_display(dev, state);

	/* Interrupt state */
	if (HAS_PCH_SPLIT(dev)) {
		I915_WRITE(DEIER, state->saveDEIER);
		I915_WRITE(DEIMR, state->saveDEIMR);
		I915_WRITE(GTIER, state->saveGTIER);
		I915_WRITE(GTIMR, state->saveGTIMR);
		I915_WRITE(FDI_RXA_IMR, state->saveFDI_RXA_IMR);
		I915_WRITE(FDI_RXB_IMR, state->saveFDI_RXB_IMR);
	} else {
		I915_WRITE (IER, state->saveIER);
		I915_WRITE (IMR,  state->saveIMR);
	}

	/* Clock gating state */
	intel_init_clock_gating(dev);

	if (HAS_PCH_SPLIT(dev))
		ironlake_enable_drps(dev);

	/* Cache mode state */
	I915_WRITE (CACHE_MODE_0, state->saveCACHE_MODE_0 | 0xffff0000);

	/* Memory arbitration state */
	I915_WRITE (MI_ARB_STATE, state->saveMI_ARB_STATE | 0xffff0000);

	for (i = 0; i < 16; i++) {
		I915_WRITE(SWF00 + (i << 2), state->saveSWF0[i]);
		I915_WRITE(SWF10 + (i << 2), state->saveSWF1[i]);
	}
	for (i = 0; i < 3; i++)
		I915_WRITE(SWF30 + (i << 2), state->saveSWF2[i]);

	/* I2C state */
	intel_i2c_reset_gmbus(dev);

	if (IS_I965GM(dev)) {
		I915_WRITE(PGTBL_CTL, state->pgtbl_ctl);
		(void) I915_READ(PGTBL_CTL);
	}

	return 0;
}

static int snb_b_fdi_train_param [] = {
	FDI_LINK_TRAIN_400MV_0DB_SNB_B,
	FDI_LINK_TRAIN_400MV_6DB_SNB_B,
	FDI_LINK_TRAIN_600MV_3_5DB_SNB_B,
	FDI_LINK_TRAIN_800MV_0DB_SNB_B,
};

void train_FDI(struct drm_device *dev, struct drm_i915_hw_state *state, int pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int pch_dpll_reg = (pipe == 0) ? PCH_DPLL_A : PCH_DPLL_B;
	int pipeconf_reg = (pipe == 0) ? PIPEACONF : PIPEBCONF;
	int fdi_tx_reg = (pipe == 0) ? FDI_TXA_CTL : FDI_TXB_CTL;
	int fdi_rx_reg = (pipe == 0) ? FDI_RXA_CTL : FDI_RXB_CTL;
	int fdi_rx_iir_reg = (pipe == 0) ? FDI_RXA_IIR : FDI_RXB_IIR;
	int fdi_rx_imr_reg = (pipe == 0) ? FDI_RXA_IMR : FDI_RXB_IMR;
	int transconf_reg = (pipe == 0) ? TRANSACONF : TRANSBCONF;
	int pf_ctl_reg = (pipe == 0) ? PFA_CTL_1 : PFB_CTL_1;
	int pf_win_size = (pipe == 0) ? PFA_WIN_SZ : PFB_WIN_SZ;
#if 0
	int pf_win_pos = (pipe == 0) ? PFA_WIN_POS : PFB_WIN_POS;
	int cpu_htot_reg = (pipe == 0) ? HTOTAL_A : HTOTAL_B;
	int cpu_hblank_reg = (pipe == 0) ? HBLANK_A : HBLANK_B;
	int cpu_hsync_reg = (pipe == 0) ? HSYNC_A : HSYNC_B;
	int cpu_vtot_reg = (pipe == 0) ? VTOTAL_A : VTOTAL_B;
	int cpu_vblank_reg = (pipe == 0) ? VBLANK_A : VBLANK_B;
	int cpu_vsync_reg = (pipe == 0) ? VSYNC_A : VSYNC_B;
	int trans_htot_reg = (pipe == 0) ? TRANS_HTOTAL_A : TRANS_HTOTAL_B;
	int trans_hblank_reg = (pipe == 0) ? TRANS_HBLANK_A : TRANS_HBLANK_B;
	int trans_hsync_reg = (pipe == 0) ? TRANS_HSYNC_A : TRANS_HSYNC_B;
	int trans_vtot_reg = (pipe == 0) ? TRANS_VTOTAL_A : TRANS_VTOTAL_B;
	int trans_vblank_reg = (pipe == 0) ? TRANS_VBLANK_A : TRANS_VBLANK_B;
	int trans_vsync_reg = (pipe == 0) ? TRANS_VSYNC_A : TRANS_VSYNC_B;
#endif
	int dpll_a_reg, fpa0_reg, fpa1_reg;
	int dpll_b_reg, fpb0_reg, fpb1_reg;
	u32 temp;
	int i, n;
	u32 pipe_bpc;

	temp = I915_READ(pipeconf_reg);
	pipe_bpc = temp & PIPE_BPC_MASK;

	/* using LVDS */
	if ((state->saveLVDS & LVDS_PORT_EN) != 0) {
		/* disable cpu pipe, disable after all planes disabled */
		temp = I915_READ(pipeconf_reg);
		if ((temp & PIPEACONF_ENABLE) != 0) {
			I915_WRITE(pipeconf_reg, temp & ~PIPEACONF_ENABLE);
			I915_READ(pipeconf_reg);
			n = 0;
			/* wait for cpu pipe off, pipe state */
			while ((I915_READ(pipeconf_reg) & I965_PIPECONF_ACTIVE) != 0) {
				n++;
				if (n < 60) {
					udelay(500);
					continue;
				} else {
					DRM_DEBUG_KMS("pipe %d off delay\n",
									pipe);
					break;
				}
			}
		} else
			DRM_DEBUG_KMS("crtc %d is disabled\n", pipe);
		udelay(100);
 
		/* Disable PF */
		temp = I915_READ(pf_ctl_reg);
		if ((temp & PF_ENABLE) != 0) {
			I915_WRITE(pf_ctl_reg, temp & ~PF_ENABLE);
			I915_READ(pf_ctl_reg);
		}
		I915_WRITE(pf_win_size, 0);
 
		/* disable CPU FDI tx and PCH FDI rx */
		temp = I915_READ(fdi_tx_reg);
		I915_WRITE(fdi_tx_reg, temp & ~FDI_TX_ENABLE);
		I915_READ(fdi_tx_reg);
 
		temp = I915_READ(fdi_rx_reg);
		/* BPC in FDI rx is consistent with that in pipeconf */
		temp &= ~(0x07 << 16);
		temp |= (pipe_bpc << 11);
		I915_WRITE(fdi_rx_reg, temp & ~FDI_RX_ENABLE);
		I915_READ(fdi_rx_reg);
 
		udelay(100);
	
		/* Ironlake workaround, disable clock pointer after downing FDI */
		if (HAS_PCH_IBX(dev))
			I915_WRITE(FDI_RX_CHICKEN(pipe),
				I915_READ(FDI_RX_CHICKEN(pipe) &
					~FDI_RX_PHASE_SYNC_POINTER_ENABLE));
 
		/* still set train pattern 1 */
		temp = I915_READ(fdi_tx_reg);
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_1;
		I915_WRITE(fdi_tx_reg, temp);
 
		temp = I915_READ(fdi_rx_reg);
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_1;
		I915_WRITE(fdi_rx_reg, temp);
		udelay(100);
 
		temp = I915_READ(PCH_LVDS);
		I915_WRITE(PCH_LVDS, temp & ~LVDS_PORT_EN);
		I915_READ(PCH_LVDS);
		udelay(100);
 
		/* disable PCH transcoder */
		temp = I915_READ(transconf_reg);
		if ((temp & TRANS_ENABLE) != 0) {
			I915_WRITE(transconf_reg, temp & ~TRANS_ENABLE);
			I915_READ(transconf_reg);
			n = 0;
			/* wait for PCH transcoder off, transcoder state */
			while ((I915_READ(transconf_reg) & TRANS_STATE_ENABLE) != 0) {
				 n++;
				if (n < 60) {
					udelay(500);
					continue;
				} else {
					DRM_DEBUG_KMS("transcoder %d off "
						"delay\n", pipe);
					break;
				}
			}
		}
 
		temp = I915_READ(transconf_reg);
		/* BPC in transcoder is consistent with that in pipeconf */
		temp &= ~PIPE_BPC_MASK;
		temp |= pipe_bpc;
		I915_WRITE(transconf_reg, temp);
		I915_READ(transconf_reg);
		udelay(100);
 
		/* disable PCH DPLL */
		temp = I915_READ(pch_dpll_reg);
		if ((temp & DPLL_VCO_ENABLE) != 0) {
			I915_WRITE(pch_dpll_reg, temp & ~DPLL_VCO_ENABLE);
			I915_READ(pch_dpll_reg);
		}
 
		temp = I915_READ(fdi_rx_reg);
		temp &= ~FDI_SEL_PCDCLK;
		I915_WRITE(fdi_rx_reg, temp);
		I915_READ(fdi_rx_reg);
 
		temp = I915_READ(fdi_rx_reg);
		temp &= ~FDI_RX_PLL_ENABLE;
		I915_WRITE(fdi_rx_reg, temp);
		I915_READ(fdi_rx_reg);
 
		/* Disable CPU FDI TX PLL */
		temp = I915_READ(fdi_tx_reg);
		if ((temp & FDI_TX_PLL_ENABLE) != 0) {
			I915_WRITE(fdi_tx_reg, temp & ~FDI_TX_PLL_ENABLE);
			I915_READ(fdi_tx_reg);
			udelay(100);
		}
 
		/* Wait for the clocks to turn off. */
		udelay(100);
 
 
		dpll_a_reg = PCH_DPLL_A;
		dpll_b_reg = PCH_DPLL_B;
		fpa0_reg = PCH_FPA0;
		fpb0_reg = PCH_FPB0;
		fpa1_reg = PCH_FPA1;
		fpb1_reg = PCH_FPB1;
 
		/* Prime the clock */
		if (I915_READ(dpll_a_reg) & DPLL_VCO_ENABLE) {
			I915_WRITE(dpll_a_reg, state->saveDPLL_A &
				   ~DPLL_VCO_ENABLE);
			DRM_UDELAY(150);
		}
		if (I915_READ(dpll_b_reg) & DPLL_VCO_ENABLE) {
			I915_WRITE(dpll_b_reg, state->saveDPLL_B &
				   ~DPLL_VCO_ENABLE);
			DRM_UDELAY(150);
		}
 
		if ((I915_READ(PCH_LVDS) & LVDS_PORT_EN) != 0) {
			temp = I915_READ(PCH_LVDS) & ~LVDS_PORT_EN;
			I915_WRITE(PCH_LVDS, temp);
			DRM_UDELAY(150);
		}
 
		I915_WRITE(fpa0_reg, state->saveFPA0);
		I915_WRITE(fpa1_reg, state->saveFPA1);
		DRM_UDELAY(150);
		I915_WRITE(fpb0_reg, state->saveFPB0);
		I915_WRITE(fpb1_reg, state->saveFPB1);
		DRM_UDELAY(150);
 
		/* Actually enable it */
		I915_WRITE(dpll_a_reg, state->saveDPLL_A);
		DRM_UDELAY(150);
		I915_WRITE(dpll_b_reg, state->saveDPLL_B);
		DRM_UDELAY(150);
 
		I915_WRITE(PCH_LVDS, state->saveLVDS);
		I915_READ(PCH_LVDS);
	}

	/*
	 * make the BPC in FDI Rx be consistent with that in
	 * pipeconf reg.
	 */
	temp = I915_READ(fdi_rx_reg);
	temp &= ~(0x7 << 16);
	temp |= (pipe_bpc << 11);
	I915_WRITE(fdi_rx_reg, temp | FDI_RX_PLL_ENABLE |
			FDI_DP_PORT_WIDTH_X4); /* default 4 lanes */
	I915_READ(fdi_rx_reg);
	udelay(200);

	/* Switch from Rawclk to PCDclk */
	temp = I915_READ(fdi_rx_reg);
	I915_WRITE(fdi_rx_reg, temp | FDI_SEL_PCDCLK);
	I915_READ(fdi_rx_reg);
	udelay(200);

	/* Enable CPU FDI TX PLL, always on for Ironlake */
	temp = I915_READ(fdi_tx_reg);
	if ((temp & FDI_TX_PLL_ENABLE) == 0) {
		I915_WRITE(fdi_tx_reg, temp | FDI_TX_PLL_ENABLE);
		I915_READ(fdi_tx_reg);
		udelay(100);
	}

	/* Enable panel fitting for LVDS */
	I915_WRITE(PFA_CTL_1, state->savePFA_CTL_1);
	I915_WRITE(PFA_WIN_SZ, state->savePFA_WIN_SZ);
	I915_WRITE(PFA_WIN_POS, state->savePFA_WIN_POS);

	temp = I915_READ(pipeconf_reg);
	if ((temp & PIPEACONF_ENABLE) == 0) {
		I915_WRITE(pipeconf_reg, temp | PIPEACONF_ENABLE);
		I915_READ(pipeconf_reg);
		 udelay(100);
	}

	/* For PCH output, training FDI link */
	/* Train 1: umask FDI RX Interrupt symbol_lock and bit_lock bit
	   for train result */
	temp = I915_READ(fdi_rx_imr_reg);
	temp &= ~FDI_RX_SYMBOL_LOCK;
	temp &= ~FDI_RX_BIT_LOCK;
	I915_WRITE(fdi_rx_imr_reg, temp);
	I915_READ(fdi_rx_imr_reg);
	udelay(150);

	/* enable CPU FDI TX and PCH FDI RX */
	temp = I915_READ(fdi_tx_reg);
	temp &= ~(7 << 19);
	temp |= (4 - 1) << 19;
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_PATTERN_1;
	if (IS_GEN6(dev)) {
		temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
		/* SNB-B */
		temp |= FDI_LINK_TRAIN_400MV_0DB_SNB_B;
	}
	I915_WRITE(fdi_tx_reg, temp | FDI_TX_ENABLE);
	I915_READ(fdi_tx_reg);

	temp = I915_READ(fdi_rx_reg);
	if (IS_GEN6(dev) && HAS_PCH_CPT(dev)) {
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_PATTERN_1_CPT;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_1;
	}
	I915_WRITE(fdi_rx_reg, temp | FDI_RX_ENABLE);
	I915_READ(fdi_rx_reg);
	udelay(150);

	/* Ironlake workaround, enable clock pointer after FDI enable*/
	I915_WRITE(FDI_RX_CHICKEN(pipe), FDI_RX_PHASE_SYNC_POINTER_ENABLE);

	if (IS_GEN6(dev)) {
		for (i = 0; i < 4; i++ ) {
			temp = I915_READ(fdi_tx_reg);
			temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
			temp |= snb_b_fdi_train_param[i];
			I915_WRITE(fdi_tx_reg, temp);
			I915_READ(fdi_tx_reg);
			udelay(500);

			temp = I915_READ(fdi_rx_iir_reg);
			DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);

			if (temp & FDI_RX_BIT_LOCK) {
				I915_WRITE(fdi_rx_iir_reg, temp | FDI_RX_BIT_LOCK);
				DRM_DEBUG_KMS("FDI train 1 done.\n");
				break;
			}
		}
		if (i == 4)
			DRM_ERROR("FDI train 1 fail!\n");

		/* Train 2 */
		temp = I915_READ(fdi_tx_reg);
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_2;
		if (IS_GEN6(dev)) {
			temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
			/* SNB-B */
			temp |= FDI_LINK_TRAIN_400MV_0DB_SNB_B;
		}
		I915_WRITE(fdi_tx_reg, temp);

		temp = I915_READ(fdi_rx_reg);
		if (HAS_PCH_CPT(dev)) {
			temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
			temp |= FDI_LINK_TRAIN_PATTERN_2_CPT;
		} else {
			temp &= ~FDI_LINK_TRAIN_NONE;
			temp |= FDI_LINK_TRAIN_PATTERN_2;
		}
		I915_WRITE(fdi_rx_reg, temp);

		POSTING_READ(fdi_rx_reg);
		udelay(150);

		for (i = 0; i < 4; i++ ) {
			temp = I915_READ(fdi_tx_reg);
			temp &= ~FDI_LINK_TRAIN_VOL_EMP_MASK;
			temp |= snb_b_fdi_train_param[i];
			I915_WRITE(fdi_tx_reg, temp);

			POSTING_READ(fdi_tx_reg);
			udelay(500);

			temp = I915_READ(fdi_rx_iir_reg);
			DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);

			if (temp & FDI_RX_SYMBOL_LOCK) {
				I915_WRITE(fdi_rx_iir_reg, temp | FDI_RX_SYMBOL_LOCK);
				DRM_DEBUG_KMS("FDI train 2 done.\n");
				break;
			}
		}
		if (i == 4)
			DRM_ERROR("FDI train 2 fail!\n");

		DRM_DEBUG_KMS("SNB train done.\n");
	} else {
		/* Ironlake workaround, enable clock pointer after FDI enable*/
		I915_WRITE(FDI_RX_CHICKEN(pipe), FDI_RX_PHASE_SYNC_POINTER_ENABLE);

		for (i = 0; i < 5; i++) {
			temp = I915_READ(fdi_rx_iir_reg);
			DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);

			if ((temp & FDI_RX_BIT_LOCK)) {
				DRM_DEBUG_KMS("FDI train 1 done.\n");
				I915_WRITE(fdi_rx_iir_reg, temp | FDI_RX_BIT_LOCK);
				break;
			}
		}
		if (i == 5)
			DRM_ERROR("FDI train 1 fail!\n");

		/* Train 2 */
		temp = I915_READ(fdi_tx_reg);
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_2;
		I915_WRITE(fdi_tx_reg, temp);

		temp = I915_READ(fdi_rx_reg);
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_PATTERN_2;
		I915_WRITE(fdi_rx_reg, temp);
		POSTING_READ(fdi_rx_reg);
		udelay(150);

		for (i = 0; i < 5; i++) {
			temp = I915_READ(fdi_rx_iir_reg);
			DRM_DEBUG_KMS("FDI_RX_IIR 0x%x\n", temp);
			if (temp & FDI_RX_SYMBOL_LOCK) {
				I915_WRITE(fdi_rx_iir_reg, temp | FDI_RX_SYMBOL_LOCK);
				DRM_DEBUG_KMS("train 2 done.\n");
				break;
			}
		}
		if (i == 5)
			DRM_ERROR("FDI train 2 fail!\n");

		DRM_DEBUG_KMS("ironlake train done\n");


	}

	/* enable PCH DPLL */
	temp = I915_READ(pch_dpll_reg);
	if ((temp & DPLL_VCO_ENABLE) == 0) {
		I915_WRITE(pch_dpll_reg, temp | DPLL_VCO_ENABLE);
		POSTING_READ(pch_dpll_reg);
		udelay(200);
	}

	if (HAS_PCH_CPT(dev)) {
		/* Be sure PCH DPLL SEL is set */
		temp = I915_READ(PCH_DPLL_SEL);
		if (pipe == 0 && (temp & TRANSA_DPLL_ENABLE) == 0)
			temp |= (TRANSA_DPLL_ENABLE | TRANSA_DPLLA_SEL);
		else if (pipe == 1 && (temp & TRANSB_DPLL_ENABLE) == 0)
			temp |= (TRANSB_DPLL_ENABLE | TRANSB_DPLLB_SEL);
		I915_WRITE(PCH_DPLL_SEL, temp);
		I915_READ(PCH_DPLL_SEL);
	}

	/* set transcoder timing */
	if (pipe == PIPE_A) {
		I915_WRITE(TRANS_HTOTAL_A, state->saveTRANS_HTOTAL_A);
		I915_WRITE(TRANS_HBLANK_A, state->saveTRANS_HBLANK_A);
		I915_WRITE(TRANS_HSYNC_A, state->saveTRANS_HSYNC_A);
		I915_WRITE(TRANS_VTOTAL_A, state->saveTRANS_VTOTAL_A);
		I915_WRITE(TRANS_VBLANK_A, state->saveTRANS_VBLANK_A);
		I915_WRITE(TRANS_VSYNC_A, state->saveTRANS_VSYNC_A);
	} else {
		I915_WRITE(TRANS_HTOTAL_B, state->saveTRANS_HTOTAL_B);
		I915_WRITE(TRANS_HBLANK_B, state->saveTRANS_HBLANK_B);
		I915_WRITE(TRANS_HSYNC_B, state->saveTRANS_HSYNC_B);
		I915_WRITE(TRANS_VTOTAL_B, state->saveTRANS_VTOTAL_B);
		I915_WRITE(TRANS_VBLANK_B, state->saveTRANS_VBLANK_B);
		I915_WRITE(TRANS_VSYNC_B, state->saveTRANS_VSYNC_B);
	}

	/* enable normal train */
	temp = I915_READ(fdi_tx_reg);
	temp &= ~FDI_LINK_TRAIN_NONE;
	temp |= FDI_LINK_TRAIN_NONE | FDI_TX_ENHANCE_FRAME_ENABLE;
	I915_WRITE(fdi_tx_reg, temp);

	temp = I915_READ(fdi_rx_reg);
	if (HAS_PCH_CPT(dev)) {
		temp &= ~FDI_LINK_TRAIN_PATTERN_MASK_CPT;
		temp |= FDI_LINK_TRAIN_NORMAL_CPT;
	} else {
		temp &= ~FDI_LINK_TRAIN_NONE;
		temp |= FDI_LINK_TRAIN_NONE;
	}
	I915_WRITE(fdi_rx_reg, temp | FDI_RX_ENHANCE_FRAME_ENABLE);

	/* wait one idle pattern time */
	POSTING_READ(fdi_rx_reg);
	udelay(1000);

	/* enable PCH transcoder */
	temp = I915_READ(transconf_reg);
	/*
	 * make the BPC in transcoder be consistent with
	 * that in pipeconf reg.
	 */
	temp &= ~PIPE_BPC_MASK;
	temp |= pipe_bpc;
	I915_WRITE(transconf_reg, temp | TRANS_ENABLE);
	I915_READ(transconf_reg);

	while ((I915_READ(transconf_reg) & TRANS_STATE_ENABLE) == 0)
		;

}

void i915_retrain_console(struct drm_device *dev, struct drm_i915_hw_state *state)
{
	train_FDI(dev, state, PIPE_A);
}
