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
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/audio/audio_driver.h>
#include <sys/note.h>
#include <sys/beep.h>
#include <sys/pci.h>
#include "audiohd.h"

#define	DRVNAME			"audiohd"

/*
 * Module linkage routines for the kernel
 */
static int audiohd_attach(dev_info_t *, ddi_attach_cmd_t);
static int audiohd_detach(dev_info_t *, ddi_detach_cmd_t);
static int audiohd_quiesce(dev_info_t *);
static int audiohd_resume(audiohd_state_t *);
static int audiohd_suspend(audiohd_state_t *);

/*
 * Local routines
 */
static int audiohd_init_state(audiohd_state_t *, dev_info_t *);
static int audiohd_init_pci(audiohd_state_t *, ddi_device_acc_attr_t *);
static void audiohd_fini_pci(audiohd_state_t *);
static int audiohd_reset_controller(audiohd_state_t *);
static int audiohd_init_controller(audiohd_state_t *);
static void audiohd_fini_controller(audiohd_state_t *);
static void audiohd_stop_dma(audiohd_state_t *);
static void audiohd_disable_intr(audiohd_state_t *);
static int audiohd_create_codec(audiohd_state_t *);
static void audiohd_build_path(audiohd_state_t *);
static void audiohd_destroy_codec(audiohd_state_t *);
static int audiohd_alloc_dma_mem(audiohd_state_t *, audiohd_dma_t *,
    size_t, ddi_dma_attr_t *, uint_t);
static void audiohd_finish_output_path(hda_codec_t *);
static uint32_t audioha_codec_verb_get(void *, uint8_t,
    uint8_t, uint16_t, uint8_t);
static uint32_t audioha_codec_4bit_verb_get(void *, uint8_t,
    uint8_t, uint16_t, uint16_t);
static int audiohd_reinit_hda(audiohd_state_t *);
static int audiohd_response_from_codec(audiohd_state_t *,
    uint32_t *, uint32_t *);
static void audiohd_restore_codec_gpio(audiohd_state_t *);
static void audiohd_change_speaker_state(audiohd_state_t *, int);
static int audiohd_allocate_port(audiohd_state_t *);
static void audiohd_free_port(audiohd_state_t *);
static void audiohd_restore_path(audiohd_state_t *);
static void audiohd_create_controls(audiohd_state_t *);
static void audiohd_get_channels(audiohd_state_t *);
static void audiohd_init_path(audiohd_state_t *);
static void audiohd_del_controls(audiohd_state_t *);
static void audiohd_destroy(audiohd_state_t *);
static void audiohd_beep_on(void *);
static void audiohd_beep_off(void *);
static void audiohd_beep_freq(void *, int);
static wid_t audiohd_find_beep(hda_codec_t *, wid_t, int);
static void audiohd_build_beep_path(hda_codec_t *);
static void audiohd_build_beep_amp(hda_codec_t *);
static void  audiohd_finish_beep_path(hda_codec_t *);
static void audiohd_do_set_beep_volume(audiohd_state_t *,
    audiohd_path_t *, uint64_t);
static void audiohd_set_beep_volume(audiohd_state_t *);
static int audiohd_set_beep(void *, uint64_t);
static void audiohd_pin_sense(audiohd_state_t *, uint32_t, uint32_t);

static	int	audiohd_beep;
static	int	audiohd_beep_divider;
static	int	audiohd_beep_vol = 1;

/* Warlock annotation */
_NOTE(SCHEME_PROTECTS_DATA("unshared data", audiohd_beep))
_NOTE(SCHEME_PROTECTS_DATA("unshared data", audiohd_beep_divider))
_NOTE(SCHEME_PROTECTS_DATA("unshared data", audiohd_beep_vol))

static ddi_device_acc_attr_t hda_dev_accattr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

static const char *audiohd_dtypes[] = {
	AUDIO_PORT_LINEOUT,
	AUDIO_PORT_SPEAKER,
	AUDIO_PORT_HEADPHONES,
	AUDIO_PORT_CD,
	AUDIO_PORT_SPDIFOUT,
	AUDIO_PORT_DIGOUT,
	AUDIO_PORT_MODEM,
	AUDIO_PORT_HANDSET,
	AUDIO_PORT_LINEIN,
	AUDIO_PORT_AUX1IN,
	AUDIO_PORT_MIC,
	AUDIO_PORT_PHONE,
	AUDIO_PORT_SPDIFIN,
	AUDIO_PORT_DIGIN,
	AUDIO_PORT_STEREOMIX,
	AUDIO_PORT_NONE,	/* reserved port, don't use */
	AUDIO_PORT_OTHER,
	NULL,
};

static audiohd_codec_info_t audiohd_codecs[] = {
	{0x1002aa01, "ATI R600 HDMI", 0x0},
	{0x10134206, "Cirrus CS4206", 0x0},
	{0x10de0002, "nVidia MCP78 HDMI", 0x0},
	{0x10de0003, "nVidia MCP78 HDMI", 0x0},
	{0x10de0006, "nVidia MCP78 HDMI", 0x0},
	{0x10de0007, "nVidia MCP7A HDMI", 0x0},
	{0x10ec0260, "Realtek ALC260", (NO_GPIO)},
	{0x10ec0262, "Realtek ALC262", (NO_GPIO | EN_PIN_BEEP)},
	{0x10ec0268, "Realtek ALC268", 0x0},
	{0x10ec0272, "Realtek ALC272", 0x0},
	{0x10ec0662, "Realtek ALC662", 0x0},
	{0x10ec0663, "Realtek ALC663", 0x0},
	{0x10ec0861, "Realtek ALC861", 0x0},
	{0x10ec0862, "Realtek ALC862", 0x0},
	{0x10ec0880, "Realtek ALC880", 0x0},
	{0x10ec0882, "Realtek ALC882", 0x0},
	{0x10ec0883, "Realtek ALC883", 0x0},
	{0x10ec0885, "Realtek ALC885", 0x0},
	{0x10ec0888, "Realtek ALC888", (NO_SPDIF)},
	{0x111d7603, "Integrated Devices 92HD75B3X5", 0x0},
	{0x111d7608, "Integrated Devices 92HD75B2X5", (NO_MIXER)},
	{0x111d76b2, "Integrated Devices 92HD71B7X", (NO_MIXER)},
	{0x11d4194a, "Analog Devices AD1984A", 0x0},
	{0x11d41981, "Analog Devices AD1981", (NO_MIXER)},
	{0x11d41983, "Analog Devices AD1983", 0x0},
	{0x11d41984, "Analog Devices AD1984", 0x0},
	{0x11d41986, "Analog Devices AD1986A", 0x0},
	{0x11d41988, "Analog Devices AD1988A", 0x0},
	{0x11d4198b, "Analog Devices AD1988B", 0x0},
	{0x13f69880, "CMedia CMI19880", 0x0},
	{0x14f15045, "Conexant CX20549", (NO_MIXER)},
	{0x14f15051, "Conexant CX20561", 0x0},
	{0x434d4980, "CMedia CMI19880", 0x0},
	{0x80862802, "Intel HDMI", 0x0},
	{0x83847610, "Sigmatel STAC9230XN", 0x0},
	{0x83847611, "Sigmatel STAC9230DN", 0x0},
	{0x83847612, "Sigmatel STAC9230XT", 0x0},
	{0x83847613, "Sigmatel STAC9230DT", 0x0},
	{0x83847614, "Sigmatel STAC9229X", 0x0},
	{0x83847615, "Sigmatel STAC9229D", 0x0},
	{0x83847616, "Sigmatel STAC9228X", 0x0},
	{0x83847617, "Sigmatel STAC9228D", 0x0},
	{0x83847618, "Sigmatel STAC9227X", 0x0},
	{0x83847619, "Sigmatel STAC9227D", 0x0},
	{0x83847620, "Sigmatel STAC9274", 0x0},
	{0x83847621, "Sigmatel STAC9274D", 0x0},
	{0x83847622, "Sigmatel STAC9273X", 0x0},
	{0x83847623, "Sigmatel STAC9273D", 0x0},
	{0x83847624, "Sigmatel STAC9272X", 0x0},
	{0x83847625, "Sigmatel STAC9272D", 0x0},
	{0x83847626, "Sigmatel STAC9271X", 0x0},
	{0x83847627, "Sigmatel STAC9271D", 0x0},
	{0x83847628, "Sigmatel STAC9274X5NH", 0x0},
	{0x83847629, "Sigmatel STAC9274D5NH", 0x0},
	{0x83847662, "Sigmatel STAC9872AK", 0x0},
	{0x83847664, "Sigmatel STAC9872K", 0x0},
	{0x83847680, "Sigmatel STAC9221A1", 0x0},
	{0x83847680, "Sigmatel STAC9221A1", 0x0},
	{0x83847681, "Sigmatel STAC9220D", 0x0},
	{0x83847682, "Sigmatel STAC9221", 0x0},
	{0x83847683, "Sigmatel STAC9221D", 0x0},
	{0x83847690, "Sigmatel STAC9200", 0x0},
	{0x838476a0, "Sigmatel STAC9205", 0x0},
	{0x838476a1, "Sigmatel STAC9205D", 0x0},
	{0x838476a2, "Sigmatel STAC9204", 0x0},
	{0x838476a3, "Sigmatel STAC9204D", 0x0},
	{0x838476a4, "Sigmatel STAC9255", 0x0},
	{0x838476a5, "Sigmatel STAC9255D", 0x0},
	{0x838476a6, "Sigmatel STAC9254", 0x0},
	{0x838476a7, "Sigmatel STAC9254D", 0x0},
	{0x83847880, "Sigmatel STAC9220A1", 0x0},
	{0x83847882, "Sigmatel STAC9220A2", 0x0},
	{0x0, "Unknown 0x00000000", 0x0},
};

static void
audiohd_set_chipset_info(audiohd_state_t *statep)
{
	uint32_t		devid;
	const char		*name;
	const char		*vers;

	devid = pci_config_get16(statep->hda_pci_handle, PCI_CONF_VENID);
	devid <<= 16;
	devid |= pci_config_get16(statep->hda_pci_handle, PCI_CONF_DEVID);
	statep->devid = devid;

	name = AUDIOHD_DEV_CONFIG;
	vers = AUDIOHD_DEV_VERSION;

	switch (devid) {
	case 0x1002437b:
		name = "ATI HD Audio";
		vers = "SB450";
		break;
	case 0x10024383:
		name = "ATI HD Audio";
		vers = "SB600";
		break;
	case 0x10029442:
		name = "ATI HD Audio";
		vers = "Radeon HD 4850";
		break;
	case 0x1002aa30:
		name = "ATI HD Audio";
		vers = "HD 48x0";
		break;
	case 0x1002aa38:
		name = "ATI HD Audio";
		vers = "Radeon HD 4670";
		break;
	case 0x10de026c:
		name = "NVIDIA HD Audio";
		vers = "MCP51";
		break;
	case 0x10de0371:
		name = "NVIDIA HD Audio";
		vers = "MCP55";
		break;
	case 0x10de03e4:
		name = "NVIDIA HD Audio";
		vers = "MCP61";
		break;
	case 0x10de03f0:
		name = "NVIDIA HD Audio";
		vers = "MCP61A";
		break;
	case 0x10de044a:
		name = "NVIDIA HD Audio";
		vers = "MCP65";
		break;
	case 0x10de055c:
		name = "NVIDIA HD Audio";
		vers = "MCP67";
		break;
	case 0x10de0774:
		name = "NVIDIA HD Audio";
		vers = "MCP78S";
		break;
	case 0x10de0ac0:
		name = "NVIDIA HD Audio";
		vers = "MCP79";
		break;
	case 0x11063288:
		name = "VIA HD Audio";
		vers = "HDA";
		break;
	case 0x80862668:
		name = "Intel HD Audio";
		vers = "ICH6";
		break;
	case 0x808627d8:
		name = "Intel HD Audio";
		vers = "ICH7";
		break;
	case 0x8086284b:
		name = "Intel HD Audio";
		vers = "ICH8";
		break;
	case 0x8086293e:
		name = "Intel HD Audio";
		vers = "ICH9";
		break;
	case 0x80863a3e:
		name = "Intel HD Audio";
		vers = "ICH10";
		break;
	}
	/* set device information */
	audio_dev_set_description(statep->adev, name);
	audio_dev_set_version(statep->adev, vers);
}

static int
audiohd_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	audiohd_state_t		*statep;
	int			instance;

	instance = ddi_get_instance(dip);
	switch (cmd) {
	case DDI_ATTACH:
		break;
	case DDI_RESUME:
		statep = ddi_get_driver_private(dip);
		ASSERT(statep != NULL);
		return (audiohd_resume(statep));
	default:
		return (DDI_FAILURE);
	}

	/* allocate the soft state structure */
	statep = kmem_zalloc(sizeof (*statep), KM_SLEEP);
	ddi_set_driver_private(dip, statep);

	mutex_init(&statep->hda_mutex, NULL, MUTEX_DRIVER, 0);
	mutex_enter(&statep->hda_mutex);

	/* interrupt cookie and initialize mutex */
	if (audiohd_init_state(statep, dip) != DDI_SUCCESS) {
		audio_dev_warn(NULL, "audiohd_init_state failed");
		goto error;
	}

	/* Set PCI command register to enable bus master and memeory I/O */
	if (audiohd_init_pci(statep, &hda_dev_accattr) != DDI_SUCCESS) {
		audio_dev_warn(statep->adev,
		    "couldn't init pci regs");
		goto error;
	}

	audiohd_set_chipset_info(statep);

	if (audiohd_init_controller(statep) != DDI_SUCCESS) {
		audio_dev_warn(statep->adev,
		    "couldn't init controller");
		goto error;
	}

	if (audiohd_create_codec(statep) != DDI_SUCCESS) {
		audio_dev_warn(statep->adev,
		    "couldn't create codec");
		goto error;
	}

	audiohd_build_path(statep);

	audiohd_get_channels(statep);
	if (audiohd_allocate_port(statep) != DDI_SUCCESS) {
		audio_dev_warn(statep->adev, "allocate port failure");
		goto error;
	}
	audiohd_init_path(statep);

	/* set up kernel statistics */
	if ((statep->hda_ksp = kstat_create(DRVNAME, instance,
	    DRVNAME, "controller", KSTAT_TYPE_INTR, 1,
	    KSTAT_FLAG_PERSISTENT)) != NULL) {
		kstat_install(statep->hda_ksp);
	}

	/* disable interrupts and clear interrupt status */
	audiohd_disable_intr(statep);

	/*
	 * Register audio controls.
	 */
	audiohd_create_controls(statep);

	if (audio_dev_register(statep->adev) != DDI_SUCCESS) {
		audio_dev_warn(statep->adev,
		    "unable to register with framework");
		goto error;
	}
	ddi_report_dev(dip);

	mutex_exit(&statep->hda_mutex);
	return (DDI_SUCCESS);
error:
	mutex_exit(&statep->hda_mutex);
	audiohd_destroy(statep);
	return (DDI_FAILURE);
}

static int
audiohd_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	audiohd_state_t		*statep;

	statep = ddi_get_driver_private(dip);
	ASSERT(statep != NULL);

	switch (cmd) {
	case DDI_DETACH:
		break;

	case DDI_SUSPEND:
		return (audiohd_suspend(statep));

	default:
		return (DDI_FAILURE);
	}
	if (audio_dev_unregister(statep->adev) != DDI_SUCCESS)
		return (DDI_FAILURE);

	if (audiohd_beep)
		(void) beep_fini();
	audiohd_destroy(statep);
	return (DDI_SUCCESS);
}

static struct dev_ops audiohd_dev_ops = {
	DEVO_REV,		/* rev */
	0,			/* refcnt */
	NULL,			/* getinfo */
	nulldev,		/* identify */
	nulldev,		/* probe */
	audiohd_attach,		/* attach */
	audiohd_detach,		/* detach */
	nodev,			/* reset */
	NULL,			/* cb_ops */
	NULL,			/* bus_ops */
	NULL,			/* power */
	audiohd_quiesce,	/* quiesce */
};

static struct modldrv audiohd_modldrv = {
	&mod_driverops,			/* drv_modops */
	"AudioHD",			/* linkinfo */
	&audiohd_dev_ops,		/* dev_ops */
};

static struct modlinkage modlinkage = {
	MODREV_1,
	{ &audiohd_modldrv, NULL }
};

int
_init(void)
{
	int	rv;

	audio_init_ops(&audiohd_dev_ops, DRVNAME);
	if ((rv = mod_install(&modlinkage)) != 0) {
		audio_fini_ops(&audiohd_dev_ops);
	}
	return (rv);
}

int
_fini(void)
{
	int	rv;

	if ((rv = mod_remove(&modlinkage)) == 0) {
		audio_fini_ops(&audiohd_dev_ops);
	}
	return (rv);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

/*
 * Audio routines
 */

static int
audiohd_engine_format(void *arg)
{
	audiohd_port_t *port = arg;
	audiohd_state_t *statep = port->statep;

	switch (statep->sample_bit_depth) {
	case AUDIOHD_BIT_DEPTH24:
		return (AUDIO_FORMAT_S32_LE);
	case AUDIOHD_BIT_DEPTH16:
	default:
		return (AUDIO_FORMAT_S16_LE);
	}
}

static int
audiohd_engine_channels(void *arg)
{
	audiohd_port_t *port = arg;

	return (port->nchan);
}

static int
audiohd_engine_rate(void *arg)
{
	audiohd_port_t *port = arg;
	audiohd_state_t *statep = port->statep;

	return (statep->sample_rate);
}
static void
audiohd_free_path(audiohd_state_t *statep)
{
	audiohd_path_t		*path;
	int			i;

	for (i = 0; i < statep->pathnum; i++) {
		if (statep->path[i]) {
			path = statep->path[i];
			kmem_free(path, sizeof (audiohd_path_t));
		}
	}
}
static void
audiohd_destroy(audiohd_state_t *statep)
{
	mutex_enter(&statep->hda_mutex);
	audiohd_stop_dma(statep);
	if (statep->hda_ksp)
		kstat_delete(statep->hda_ksp);
	audiohd_free_port(statep);
	audiohd_free_path(statep);
	audiohd_destroy_codec(statep);
	audiohd_del_controls(statep);
	audiohd_fini_controller(statep);
	audiohd_fini_pci(statep);
	mutex_exit(&statep->hda_mutex);
	mutex_destroy(&statep->hda_mutex);
	if (statep->adev)
		audio_dev_free(statep->adev);
	kmem_free(statep, sizeof (*statep));
}

/*
 * get the max channels the hardware supported
 */
static void
audiohd_get_channels(audiohd_state_t *statep)
{
	int		i;
	uint8_t		maxp, assoc;

	maxp = 2;
	for (i = 0; i < AUDIOHD_MAX_ASSOC; i++) {
		if (maxp < statep->chann[i]) {
			maxp = statep->chann[i];
			assoc = i;
		}
	}
	statep->pchan = maxp;
	statep->assoc = assoc;
	/* for record, support stereo so far */
	statep->rchan = 2;
}

static void
audiohd_init_play_path(audiohd_path_t *path)
{
	int				i;
	uint32_t			ctrl;
	uint8_t				ctrl8;
	uint8_t				nchann;
	audiohd_widget_t		*widget;
	audiohd_pin_t			*pin;
	wid_t				wid;
	audiohd_pin_color_t		color;

	audiohd_state_t		*statep = path->statep;
	hda_codec_t		*codec = path->codec;

	/* enable SPDIF output */
	for (i = 0; i < path->pin_nums; i++) {
		wid = path->pin_wid[i];
		widget = codec->widget[wid];
		pin = (audiohd_pin_t *)widget->priv;
		if (pin->device == DTYPE_SPDIF_OUT) {
			ctrl = audioha_codec_verb_get(
			    statep,
			    codec->index,
			    path->adda_wid,
			    AUDIOHDC_VERB_GET_SPDIF_CTL,
			    0);
			ctrl |= AUDIOHD_SPDIF_ON;
			ctrl8 = ctrl &
			    AUDIOHD_SPDIF_MASK;
			(void) audioha_codec_verb_get(
			    statep,
			    codec->index,
			    path->adda_wid,
			    AUDIOHDC_VERB_SET_SPDIF_LCL,
			    ctrl8);
			/*
			 * We find that on intel ICH10 chipset with codec
			 * ALC888, audio is scratchy if we set the tag on the
			 * SPDIF path. So we just return here without setting
			 * the tag for the path as a workaround.
			 */
			if (codec->codec_info->flags & NO_SPDIF)
				return;
		}
	}
	wid = path->pin_wid[0];
	widget = codec->widget[wid];
	pin = (audiohd_pin_t *)widget->priv;

	/* two channels supported */
	if (pin->device == DTYPE_SPEAKER ||
	    pin->device == DTYPE_HP_OUT ||
	    pin->assoc != statep->assoc) {
		(void) audioha_codec_verb_get(
		    statep,
		    codec->index,
		    path->adda_wid,
		    AUDIOHDC_VERB_SET_STREAM_CHANN,
		    statep->port[PORT_DAC]->index <<
		    AUDIOHD_PLAY_TAG_OFF);
		(void) audioha_codec_4bit_verb_get(
		    statep,
		    codec->index,
		    path->adda_wid,
		    AUDIOHDC_VERB_SET_CONV_FMT,
		    statep->port[PORT_DAC]->format << 4 |
		    statep->pchan - 1);
	/* multichannel supported */
	} else {
		color = (pin->config >> AUDIOHD_PIN_CLR_OFF) &
		    AUDIOHD_PIN_CLR_MASK;
		switch (color) {
		case AUDIOHD_PIN_BLACK:
			nchann = statep->pchan - 2;
			break;
		case AUDIOHD_PIN_ORANGE:
			nchann = 2;
			break;
		case AUDIOHD_PIN_GREY:
			nchann = 4;
			break;
		case AUDIOHD_PIN_GREEN:
			nchann = 0;
			break;
		default:
			nchann = 0;
			break;
		}
		(void) audioha_codec_verb_get(statep,
		    codec->index,
		    path->adda_wid,
		    AUDIOHDC_VERB_SET_STREAM_CHANN,
		    statep->port[PORT_DAC]->index <<
		    AUDIOHD_PLAY_TAG_OFF |
		    nchann);
		(void) audioha_codec_4bit_verb_get(
		    statep,
		    codec->index,
		    path->adda_wid,
		    AUDIOHDC_VERB_SET_CONV_FMT,
		    statep->port[PORT_DAC]->format << 4 |
		    statep->pchan - 1);
	}
}
static void
audiohd_init_record_path(audiohd_path_t *path)
{
	audiohd_state_t		*statep = path->statep;
	hda_codec_t		*codec = path->codec;
	int			i;
	wid_t			wid;
	audiohd_pin_t		*pin;
	audiohd_widget_t	*widget;

	for (i = 0; i < path->pin_nums; i++) {
		wid = path->pin_wid[i];
		widget = codec->widget[wid];
		pin = (audiohd_pin_t *)widget->priv;
	/*
	 * Since there is no SPDIF input device available for test,
	 * we will use this code in the future to support SPDIF input
	 */
#if 0
		if (pin->device == DTYPE_SPDIF_IN) {
			ctrl = audioha_codec_verb_get(
			    statep,
			    codec->index,
			    path->adda_wid,
			    AUDIOHDC_VERB_GET_SPDIF_CTL,
			    0);
			ctrl |= AUDIOHD_SPDIF_ON;
			ctrl8 = ctrl &
			    AUDIOHD_SPDIF_MASK;
			(void) audioha_codec_verb_get(
			    statep,
			    codec->index,
			    path->adda_wid,
			    AUDIOHDC_VERB_SET_SPDIF_LCL,
			    ctrl8);
			statep->inmask |= (1U << DTYPE_SPDIF_IN);
		}
#endif
		if (pin->device == DTYPE_MIC_IN) {
			if (((pin->config >>
			    AUDIOHD_PIN_CONTP_OFF) &
			    AUDIOHD_PIN_CONTP_MASK) ==
			    AUDIOHD_PIN_CON_FIXED)
				statep->port[PORT_ADC]->index = path->tag;
		}
		if ((pin->device == DTYPE_LINE_IN) ||
		    (pin->device == DTYPE_CD) ||
		    (pin->device == DTYPE_MIC_IN)) {
			statep->inmask |= (1U << pin->device);
		}
	}
	(void) audioha_codec_verb_get(statep,
	    codec->index,
	    path->adda_wid,
	    AUDIOHDC_VERB_SET_STREAM_CHANN,
	    path->tag <<
	    AUDIOHD_REC_TAG_OFF);
	(void) audioha_codec_4bit_verb_get(statep,
	    codec->index,
	    path->adda_wid,
	    AUDIOHDC_VERB_SET_CONV_FMT,
	    statep->port[PORT_ADC]->format << 4 | statep->rchan - 1);
}

static void
audiohd_init_path(audiohd_state_t *statep)
{
	int				i;
	audiohd_path_t			*path;

	for (i = 0; i < statep->pathnum; i++) {
		if ((path = statep->path[i]) == NULL)
			continue;
		switch (path->path_type) {
		case PLAY:
			audiohd_init_play_path(path);
			break;
		case RECORD:
			audiohd_init_record_path(path);
			break;
		default:
			break;
		}
	}
	statep->in_port = 0;
}

static int
audiohd_reset_port(audiohd_port_t *port)
{
	uint16_t		regbase;
	audiohd_state_t		*statep;
	uint8_t			bTmp;
	int			i;

	regbase = port->regoff;
	statep = port->statep;

	bTmp = AUDIOHD_REG_GET8(regbase + AUDIOHD_SDREG_OFFSET_CTL);
	/* stop stream */
	bTmp &= ~AUDIOHD_REG_RIRBSIZE;
	AUDIOHD_REG_SET8(regbase + AUDIOHD_SDREG_OFFSET_CTL, bTmp);

	/* wait 40us for stream to stop as HD spec */
	drv_usecwait(40);

	/* reset stream */
	bTmp |= AUDIOHDR_SD_CTL_SRST;
	AUDIOHD_REG_SET8(regbase + AUDIOHD_SDREG_OFFSET_CTL, bTmp);

	for (i = 0; i < AUDIOHD_RETRY_TIMES; i++) {
		/* Empirical testing time, which works well */
		drv_usecwait(50);
		bTmp = AUDIOHD_REG_GET8(regbase + AUDIOHD_SDREG_OFFSET_CTL);
		bTmp &= AUDIOHDR_SD_CTL_SRST;
		if (bTmp)
			break;
	}

	if (!bTmp) {
		audio_dev_warn(statep->adev, "Failed to reset stream %d",
		    port->index);
		return (EIO);
	}

	/* Empirical testing time, which works well */
	drv_usecwait(300);

	/* exit reset stream */
	bTmp &= ~AUDIOHDR_SD_CTL_SRST;
	AUDIOHD_REG_SET8(regbase + AUDIOHD_SDREG_OFFSET_CTL, bTmp);

	for (i = 0; i < AUDIOHD_RETRY_TIMES; i++) {
		/* Empircal testing time */
		drv_usecwait(50);
		bTmp = AUDIOHD_REG_GET8(regbase + AUDIOHD_SDREG_OFFSET_CTL);
		bTmp &= AUDIOHDR_SD_CTL_SRST;
		if (!bTmp)
			break;
	}

	if (bTmp) {
		audio_dev_warn(statep->adev,
		    "Failed to exit reset state for"
		    " stream %d, bTmp=0x%02x", port->index, bTmp);
		return (EIO);
	}

	AUDIOHD_REG_SET32(regbase + AUDIOHD_SDREG_OFFSET_BDLPL,
	    (uint32_t)port->bdl_paddr);
	AUDIOHD_REG_SET32(regbase + AUDIOHD_SDREG_OFFSET_BDLPU,
	    (uint32_t)(port->bdl_paddr >> 32));
	AUDIOHD_REG_SET16(regbase + AUDIOHD_SDREG_OFFSET_LVI,
	    AUDIOHD_BDLE_NUMS - 1);
	AUDIOHD_REG_SET32(regbase + AUDIOHD_SDREG_OFFSET_CBL, port->bufsize);

	AUDIOHD_REG_SET16(regbase + AUDIOHD_SDREG_OFFSET_FORMAT,
	    port->format << 4 | port->nchan - 1);

	/* clear status */
	AUDIOHD_REG_SET8(regbase + AUDIOHD_SDREG_OFFSET_STS,
	    AUDIOHDR_SD_STS_BCIS | AUDIOHDR_SD_STS_FIFOE |
	    AUDIOHDR_SD_STS_DESE);

	/* set stream tag */
	AUDIOHD_REG_SET8(regbase + AUDIOHD_SDREG_OFFSET_CTL +
	    AUDIOHD_PLAY_CTL_OFF,
	    (port->index) << AUDIOHD_PLAY_TAG_OFF);

	return (0);
}

static int
audiohd_engine_open(void *arg, int flag, unsigned *nframes, caddr_t *bufp)
{
	audiohd_port_t	*port = arg;
	audiohd_state_t	*statep = port->statep;

	_NOTE(ARGUNUSED(flag));

	mutex_enter(&statep->hda_mutex);
	port->count = 0;
	port->curpos = 0;
	*nframes = port->nframes;
	*bufp = port->samp_kaddr;
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static int
audiohd_engine_start(void *arg)
{
	audiohd_port_t		*port = arg;
	audiohd_state_t		*statep = port->statep;
	int			rv;

	mutex_enter(&statep->hda_mutex);

	if ((rv = audiohd_reset_port(port)) != 0) {
		mutex_exit(&statep->hda_mutex);
		return (rv);
	}
	/* Start DMA */
	AUDIOHD_REG_SET8(port->regoff + AUDIOHD_SDREG_OFFSET_CTL,
	    AUDIOHDR_SD_CTL_SRUN);

	mutex_exit(&statep->hda_mutex);
	return (0);
}

static void
audiohd_engine_stop(void *arg)
{
	audiohd_port_t		*port = arg;
	audiohd_state_t		*statep = port->statep;

	mutex_enter(&statep->hda_mutex);
	AUDIOHD_REG_SET8(port->regoff + AUDIOHD_SDREG_OFFSET_CTL, 0);
	mutex_exit(&statep->hda_mutex);
}

static void
audiohd_update_port(audiohd_port_t *port)
{
	uint32_t		pos, len;
	audiohd_state_t		*statep = port->statep;
	int			i, ret;
	uint32_t		status, resp = 0, respex = 0;
	uint8_t			rirbsts;

	pos = AUDIOHD_REG_GET32(port->regoff + AUDIOHD_SDREG_OFFSET_LPIB);
	/* Convert the position into a frame count */
	pos /= (port->nchan * statep->sample_packed_bytes);

	ASSERT(pos <= port->nframes);
	if (pos >= port->curpos) {
		len = (pos - port->curpos);
	} else {
		len = pos + port->nframes - port->curpos;
	}

	ASSERT(len <= port->nframes);
	port->curpos = pos;
	port->count += len;

	/*
	 * Check unsolicited response from pins, maybe something plugged in or
	 * out of the jack.
	 */
	status = AUDIOHD_REG_GET32(AUDIOHD_REG_INTSTS);
	if (status == 0) {
		/* No pending interrupt we should take care */
		return;
	}

	if (status & AUDIOHD_CIS_MASK) {
		/* Clear the unsolicited response interrupt */
		rirbsts = AUDIOHD_REG_GET8(AUDIOHD_REG_RIRBSTS);
		AUDIOHD_REG_SET8(AUDIOHD_REG_RIRBSTS, rirbsts);

		/*
		 * We have to wait and try several times to make sure the
		 * unsolicited response is generated by our pins.
		 * we need to make it work for audiohd spec 0.9, which is
		 * just a draft version and requires more time to wait.
		 */
		for (i = 0; i < AUDIOHD_TEST_TIMES; i++) {
			ret = audiohd_response_from_codec(statep, &resp,
			    &respex);
			if ((ret == DDI_SUCCESS) &&
			    (respex & AUDIOHD_RIRB_UR_MASK)) {
				/*
				 * A pin may generate more than one ur rirb,
				 * we only need handle one of them, and clear
				 * the other ones
				 */
				statep->hda_rirb_rp =
				    AUDIOHD_REG_GET16(AUDIOHD_REG_RIRBWP) &
				    AUDIOHD_RIRB_WPMASK;
				audiohd_pin_sense(statep, resp, respex);
				break;
			}
		}
	}
}

static uint64_t
audiohd_engine_count(void *arg)
{
	audiohd_port_t	*port = arg;
	audiohd_state_t	*statep = port->statep;
	uint64_t	val;

	mutex_enter(&statep->hda_mutex);
	audiohd_update_port(port);
	val = port->count;
	mutex_exit(&statep->hda_mutex);
	return (val);
}

static void
audiohd_engine_close(void *arg)
{
	_NOTE(ARGUNUSED(arg));
}

static void
audiohd_engine_sync(void *arg, unsigned nframes)
{
	audiohd_port_t *port = arg;

	_NOTE(ARGUNUSED(nframes));

	(void) ddi_dma_sync(port->samp_dmah, 0, 0, port->sync_dir);

}

audio_engine_ops_t audiohd_engine_ops = {
	AUDIO_ENGINE_VERSION,		/* version number */
	audiohd_engine_open,
	audiohd_engine_close,
	audiohd_engine_start,
	audiohd_engine_stop,
	audiohd_engine_count,
	audiohd_engine_format,
	audiohd_engine_channels,
	audiohd_engine_rate,
	audiohd_engine_sync,
	NULL,
	NULL,
	NULL
};

static int
audiohd_get_control(void *arg, uint64_t *val)
{
	audiohd_ctrl_t	*ac = arg;
	audiohd_state_t	*statep = ac->statep;

	mutex_enter(&statep->hda_mutex);
	*val = ac->val;
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static void
audiohd_do_set_pin_volume(audiohd_state_t *statep, audiohd_path_t *path,
    uint64_t val)
{
	uint8_t				l, r;
	uint_t				tmp;
	int				gain;

	if (path->mute_wid && val == 0) {
		(void) audioha_codec_4bit_verb_get(
		    statep,
		    path->codec->index,
		    path->mute_wid,
		    AUDIOHDC_VERB_SET_AMP_MUTE,
		    path->mute_dir |
		    AUDIOHDC_AMP_SET_LNR |
		    AUDIOHDC_AMP_SET_MUTE);
		return;
	}

	l = (val & 0xff00) >> 8;
	r = (val & 0xff);
	tmp = l * path->gain_bits / 100;
	(void) audioha_codec_4bit_verb_get(statep,
	    path->codec->index,
	    path->gain_wid,
	    AUDIOHDC_VERB_SET_AMP_MUTE,
	    AUDIOHDC_AMP_SET_LEFT | path->gain_dir |
	    tmp);
	tmp = r * path->gain_bits / 100;
	(void) audioha_codec_4bit_verb_get(statep,
	    path->codec->index,
	    path->gain_wid,
	    AUDIOHDC_VERB_SET_AMP_MUTE,
	    AUDIOHDC_AMP_SET_RIGHT | path->gain_dir |
	    tmp);

	if (path->mute_wid && path->mute_wid != path->gain_wid) {
		gain = AUDIOHDC_GAIN_MAX;
		(void) audioha_codec_4bit_verb_get(
		    statep,
		    path->codec->index,
		    path->mute_wid,
		    AUDIOHDC_VERB_SET_AMP_MUTE,
		    path->mute_dir |
		    AUDIOHDC_AMP_SET_LEFT |
		    gain);
		(void) audioha_codec_4bit_verb_get(
		    statep,
		    path->codec->index,
		    path->mute_wid,
		    AUDIOHDC_VERB_SET_AMP_MUTE,
		    path->mute_dir |
		    AUDIOHDC_AMP_SET_RIGHT |
		    gain);
	}
}

static void
audiohd_set_pin_volume(audiohd_state_t *statep, audiohda_device_type_t type)
{
	int				i, j;
	audiohd_path_t			*path;
	audiohd_widget_t		*widget;
	wid_t				wid;
	audiohd_pin_t			*pin;
	hda_codec_t			*codec;
	uint64_t			val;
	audiohd_ctrl_t			control;

	switch (type) {
		case DTYPE_SPEAKER:
			control = statep->ctrls[CTL_SPEAKER];
			if (control.ctrl == NULL)
				return;
			val = control.val;
			break;
		case DTYPE_HP_OUT:
			control = statep->ctrls[CTL_HEADPHONE];
			if (control.ctrl == NULL)
				return;
			val = control.val;
			break;
		case DTYPE_LINEOUT:
			control = statep->ctrls[CTL_FRONT];
			if (control.ctrl == NULL)
				return;
			val = control.val;
			break;
		case DTYPE_CD:
			control = statep->ctrls[CTL_CD];
			if (control.ctrl == NULL)
				return;
			val = control.val;
			break;
		case DTYPE_LINE_IN:
			control = statep->ctrls[CTL_LINEIN];
			if (control.ctrl == NULL)
				return;
			val = control.val;
			break;
		case DTYPE_MIC_IN:
			control = statep->ctrls[CTL_MIC];
			if (control.ctrl == NULL)
				return;
			val = control.val;
			break;
	}

	for (i = 0; i < statep->pathnum; i++) {
		if ((path = statep->path[i]) == NULL)
			continue;

		codec = path->codec;
		for (j = 0; j < path->pin_nums; j++) {
			wid = path->pin_wid[j];
			widget = codec->widget[wid];
			pin = (audiohd_pin_t *)widget->priv;
			if ((pin->device == type) && path->gain_wid) {
				audiohd_do_set_pin_volume(statep, path, val);
			}
		}
	}
}


static void
audiohd_set_pin_volume_by_color(audiohd_state_t *statep,
    audiohd_pin_color_t color)
{
	int			i, j;
	audiohd_path_t		*path;
	audiohd_widget_t	*widget;
	wid_t			wid;
	audiohd_pin_t		*pin;
	hda_codec_t		*codec;
	uint8_t			l, r;
	uint64_t		val;
	audiohd_pin_color_t	clr;
	audiohd_ctrl_t		control;

	switch (color) {
		case AUDIOHD_PIN_GREEN:
			control = statep->ctrls[CTL_FRONT];
			if (control.ctrl == NULL)
				return;
			val = control.val;
			break;
		case AUDIOHD_PIN_BLACK:
			control = statep->ctrls[CTL_REAR];
			if (control.ctrl == NULL)
				return;
			val = control.val;
			break;
		case AUDIOHD_PIN_ORANGE:
			control = statep->ctrls[CTL_CENTER];
			if (control.ctrl == NULL)
				return;
			l = control.val;
			control = statep->ctrls[CTL_LFE];
			if (control.ctrl == NULL)
				return;
			r = control.val;
			val = (l << 8) | r;
			break;
		case AUDIOHD_PIN_GREY:
			control = statep->ctrls[CTL_SURROUND];
			if (control.ctrl == NULL)
				return;
			val = control.val;
			break;
	}

	for (i = 0; i < statep->pathnum; i++) {
		path = statep->path[i];
		if (!path)
			continue;
		codec = path->codec;
		for (j = 0; j < path->pin_nums; j++) {
			wid = path->pin_wid[j];
			widget = codec->widget[wid];
			pin = (audiohd_pin_t *)widget->priv;
			clr = (pin->config >> AUDIOHD_PIN_CLR_OFF) &
			    AUDIOHD_PIN_CLR_MASK;
			if ((clr == color) && path->gain_wid) {
				audiohd_do_set_pin_volume(statep, path, val);
			}
		}
	}
}

static int
audiohd_set_input_pin(audiohd_state_t *statep)
{
	uint64_t		val;
	hda_codec_t		*codec;
	audiohd_pin_t		*pin;
	audiohd_path_t		*path;
	audiohd_widget_t	*widget, *w;
	int			i, j;
	wid_t			wid, pin_wid = 0;
	uint32_t		set_val;

	val = statep->ctrls[CTL_RECSRC].val;
	set_val = ddi_ffs(val & 0xffff) - 1;
	for (i = 0; i < statep->pathnum; i++) {
		if ((path = statep->path[i]) == NULL ||
		    path->path_type != RECORD)
			continue;

		switch (set_val) {
		case DTYPE_LINE_IN:
		case DTYPE_MIC_IN:
		case DTYPE_CD:
			for (j = 0; j < path->pin_nums; j++) {
				wid = path->pin_wid[j];
				widget = path->codec->widget[wid];
				pin = (audiohd_pin_t *)widget->priv;

				if ((1U << pin->device) == val) {
					AUDIOHD_ENABLE_PIN_IN(statep,
					    path->codec->index, pin->wid);
					pin_wid = pin->wid;
					codec = path->codec;
					statep->in_port = pin->device;
				} else if (statep->in_port == pin->device) {
					AUDIOHD_DISABLE_PIN_IN(statep,
					    path->codec->index, pin->wid);
				}
			}
			break;
		default:
			break;
		}
	}

	if (pin_wid == 0)
		return (DDI_SUCCESS);

	w = codec->widget[pin_wid];
	pin = (audiohd_pin_t *)w->priv;
	w = codec->widget[pin->adc_wid];
	path = (audiohd_path_t *)w->priv;

	/*
	 * If there is a real selector in this input path,
	 * we select the right one input for the selector.
	 */
	if (path->sum_wid) {
		w = codec->widget[path->sum_wid];
		if (w->type == WTYPE_AUDIO_SEL) {
			for (i = 0; i < path->pin_nums; i++) {
				if (path->pin_wid[i] == pin->wid) {
					(void) audioha_codec_verb_get(
					    statep, codec->index, path->sum_wid,
					    AUDIOHDC_VERB_SET_CONN_SEL,
					    path->sum_selconn[i]);
					break;
				}
			}
		}
	}

	return (DDI_SUCCESS);
}

static void
audiohd_set_pin_monitor_gain(hda_codec_t *codec, audiohd_state_t *statep,
    uint_t caddr, audiohd_pin_t *pin, uint64_t gain)
{
	int 			i, k;
	uint_t			ltmp, rtmp;
	audiohd_widget_t	*widget;
	uint8_t		l, r;

	l = (gain & 0xff00) >> 8;
	r = (gain & 0xff);

	for (k = 0; k < pin->num; k++) {
		ltmp = l * pin->mg_gain[k] / 100;
		rtmp = r * pin->mg_gain[k] / 100;
		widget = codec->widget[pin->mg_wid[k]];
		if (pin->mg_dir[k] == AUDIOHDC_AMP_SET_OUTPUT) {
			(void) audioha_codec_4bit_verb_get(
			    statep,
			    caddr,
			    pin->mg_wid[k],
			    AUDIOHDC_VERB_SET_AMP_MUTE,
			    AUDIOHDC_AMP_SET_LEFT|
			    pin->mg_dir[k] | ltmp);
			(void) audioha_codec_4bit_verb_get(
			    statep,
			    caddr,
			    pin->mg_wid[k],
			    AUDIOHDC_VERB_SET_AMP_MUTE,
			    AUDIOHDC_AMP_SET_RIGHT|
			    pin->mg_dir[k] | rtmp);
		} else if (pin->mg_dir[k] == AUDIOHDC_AMP_SET_INPUT) {
			for (i = 0; i < widget->used; i++) {
				(void) audioha_codec_4bit_verb_get(
				    statep,
				    caddr,
				    pin->mg_wid[k],
				    AUDIOHDC_VERB_SET_AMP_MUTE,
				    AUDIOHDC_AMP_SET_RIGHT|
				    widget->monitor_path_next[i]<<
				    AUDIOHDC_AMP_SET_INDEX_OFFSET |
				    pin->mg_dir[k] | rtmp);
				(void) audioha_codec_4bit_verb_get(
				    statep,
				    caddr,
				    pin->mg_wid[k],
				    AUDIOHDC_VERB_SET_AMP_MUTE,
				    AUDIOHDC_AMP_SET_LEFT|
				    widget->monitor_path_next[i]<<
				    AUDIOHDC_AMP_SET_INDEX_OFFSET |
				    pin->mg_dir[k] | ltmp);
			}
		}
	}
}

static void
audiohd_set_monitor_gain(audiohd_state_t *statep)
{
	int			i, j;
	audiohd_path_t		*path;
	uint_t			caddr;
	audiohd_widget_t	*w;
	wid_t			wid;
	audiohd_pin_t		*pin;
	audiohd_ctrl_t		ctrl;
	uint64_t		val;

	ctrl = statep->ctrls[CTL_MONGAIN];
	val = ctrl.val;

	for (i = 0; i < statep->pathnum; i++) {
		path = statep->path[i];
		if (path == NULL || path->path_type != PLAY)
			continue;
		caddr = path->codec->index;
		for (j = 0; j < path->pin_nums; j++) {
			wid = path->pin_wid[j];
			w = path->codec->widget[wid];
			pin = (audiohd_pin_t *)w->priv;
			audiohd_set_pin_monitor_gain(path->codec, statep,
			    caddr, pin, val);
		}
	}

}

static void
audiohd_set_beep_volume(audiohd_state_t *statep)
{
	int			i;
	audiohd_path_t		*path;
	hda_codec_t		*codec;
	uint64_t		val;
	uint_t			tmp;
	audiohd_ctrl_t		control;
	uint32_t		vid;

	control = statep->ctrls[CTL_BEEP];
	val = control.val;
	for (i = 0; i < statep->pathnum; i++) {
		path = statep->path[i];
		if (!path || path->path_type != BEEP)
			continue;
		codec = path->codec;
		vid = codec->vid;
		vid = vid >> 16;

		switch (vid) {
		case  AUDIOHD_VID_SIGMATEL:
			/*
			 * Sigmatel HD codec specific operation.
			 * There is a workaround,
			 * Due to Sigmatel HD codec hardware problem,
			 * which it can't mute beep when volume is 0.
			 * So add global value audiohd_beep_vol,
			 * Set freq to 0 when volume is 0.
			 */
			tmp = val * path->gain_bits / 100;
			if (tmp == 0) {
				audiohd_beep_vol = 0;
			} else {
				audiohd_beep_vol = tmp;
				(void) audioha_codec_verb_get(
				    statep,
				    codec->index,
				    path->beep_wid,
				    AUDIOHDC_VERB_SET_BEEP_VOL,
				    tmp);
			}
			break;

		default:
			/* Common operation based on audiohd spec */
			audiohd_do_set_beep_volume(statep, path, val);
			break;
		}
	}
}

static void
audiohd_do_set_beep_volume(audiohd_state_t *statep, audiohd_path_t *path,
    uint64_t val)
{
	uint8_t		l, r;
	uint_t		tmp;
	int		gain;

	if (val == 0) {
		(void) audioha_codec_4bit_verb_get(
		    statep,
		    path->codec->index,
		    path->mute_wid,
		    AUDIOHDC_VERB_SET_AMP_MUTE,
		    path->mute_dir |
		    AUDIOHDC_AMP_SET_LNR |
		    AUDIOHDC_AMP_SET_MUTE);
		return;
	}

	r = (val & 0xff);
	l = r;

	tmp = l * path->gain_bits / 100;
	(void) audioha_codec_4bit_verb_get(statep,
	    path->codec->index,
	    path->gain_wid,
	    AUDIOHDC_VERB_SET_AMP_MUTE,
	    AUDIOHDC_AMP_SET_LEFT | path->gain_dir |
	    tmp);
	tmp = r * path->gain_bits / 100;
	(void) audioha_codec_4bit_verb_get(statep,
	    path->codec->index,
	    path->gain_wid,
	    AUDIOHDC_VERB_SET_AMP_MUTE,
	    AUDIOHDC_AMP_SET_RIGHT | path->gain_dir |
	    tmp);
	if (path->mute_wid != path->gain_wid) {
		gain = AUDIOHDC_GAIN_MAX;
		(void) audioha_codec_4bit_verb_get(
		    statep,
		    path->codec->index,
		    path->mute_wid,
		    AUDIOHDC_VERB_SET_AMP_MUTE,
		    path->mute_dir |
		    AUDIOHDC_AMP_SET_LEFT |
		    gain);
		(void) audioha_codec_4bit_verb_get(
		    statep,
		    path->codec->index,
		    path->mute_wid,
		    AUDIOHDC_VERB_SET_AMP_MUTE,
		    path->mute_dir |
		    AUDIOHDC_AMP_SET_RIGHT |
		    gain);
	}
}

static void
audiohd_configure_output(audiohd_state_t *statep)
{
	audiohd_set_pin_volume(statep, DTYPE_LINEOUT);
	audiohd_set_pin_volume(statep, DTYPE_SPEAKER);
	audiohd_set_pin_volume(statep, DTYPE_HP_OUT);

	audiohd_set_pin_volume_by_color(statep, AUDIOHD_PIN_GREEN);
	audiohd_set_pin_volume_by_color(statep, AUDIOHD_PIN_BLACK);
	audiohd_set_pin_volume_by_color(statep, AUDIOHD_PIN_GREY);
	audiohd_set_pin_volume_by_color(statep, AUDIOHD_PIN_ORANGE);
}

static void
audiohd_configure_input(audiohd_state_t *statep)
{
	(void) audiohd_set_input_pin(statep);
	audiohd_set_monitor_gain(statep);
	audiohd_set_pin_volume(statep, DTYPE_LINE_IN);
	audiohd_set_pin_volume(statep, DTYPE_CD);
	audiohd_set_pin_volume(statep, DTYPE_MIC_IN);
}

static int
audiohd_set_recsrc(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t *statep = pc->statep;

	if (val & ~(statep->inmask))
		return (EINVAL);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_configure_input(statep);
	mutex_exit(&statep->hda_mutex);
	return (0);
}

static int
audiohd_set_rear(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t	*statep = pc->statep;
	AUDIOHD_CHECK_2CHANNELS_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_pin_volume_by_color(statep, AUDIOHD_PIN_BLACK);
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static int
audiohd_set_center(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t	*statep = pc->statep;
	AUDIOHD_CHECK_CHANNEL_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_pin_volume_by_color(statep, AUDIOHD_PIN_ORANGE);
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static int
audiohd_set_surround(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t	*statep = pc->statep;
	AUDIOHD_CHECK_2CHANNELS_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_pin_volume_by_color(statep, AUDIOHD_PIN_GREY);
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static int
audiohd_set_lfe(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t	*statep = pc->statep;
	AUDIOHD_CHECK_CHANNEL_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_pin_volume_by_color(statep, AUDIOHD_PIN_ORANGE);
	mutex_exit(&statep->hda_mutex);

	return (0);
}
static int
audiohd_set_speaker(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t	*statep = pc->statep;
	AUDIOHD_CHECK_2CHANNELS_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_pin_volume(statep, DTYPE_SPEAKER);
	mutex_exit(&statep->hda_mutex);

	return (0);
}
static int
audiohd_set_front(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t	*statep = pc->statep;
	AUDIOHD_CHECK_2CHANNELS_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_pin_volume_by_color(statep, AUDIOHD_PIN_GREEN);
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static int
audiohd_set_headphone(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t	*statep = pc->statep;
	AUDIOHD_CHECK_2CHANNELS_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_pin_volume(statep, DTYPE_HP_OUT);
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static int
audiohd_set_linein(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t	*statep = pc->statep;
	AUDIOHD_CHECK_2CHANNELS_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_pin_volume(statep, DTYPE_LINE_IN);
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static int
audiohd_set_loopback(void *arg, uint64_t val)
{
	audiohd_ctrl_t		*pc = arg;
	audiohd_state_t		*statep = pc->statep;
	audiohd_path_t		*path = NULL;
	audiohd_widget_t	*widget = NULL;
	audiohd_pin_t		*pin = NULL;
	wid_t			wid;
	uint32_t		pinctrl;
	int			i, j;

	mutex_enter(&statep->hda_mutex);
	pc->val = val;

	for (i = 0; i < statep->pathnum; i++) {
		path = statep->path[i];
		if (path == NULL || path->path_type != LOOPBACK)
			continue;

		for (j = 0; j < path->pin_nums; j++) {
			wid = path->pin_wid[j];
			widget = path->codec->widget[wid];
			pin = (audiohd_pin_t *)widget->priv;

			if (val == 1) {
				/* Turn on loopback recording */
				pinctrl = audioha_codec_verb_get(statep,
				    path->codec->index, wid,
				    AUDIOHDC_VERB_GET_PIN_CTRL, 0);
				(void) audioha_codec_verb_get(statep,
				    path->codec->index, wid,
				    AUDIOHDC_VERB_SET_PIN_CTRL,
				    pinctrl | AUDIOHD_PIN_OUT_ENABLE);

				if (pin->cap & AUDIOHD_EXT_AMP_MASK) {
					(void) audioha_codec_verb_get(statep,
					    path->codec->index,
					    wid, AUDIOHDC_VERB_SET_EAPD,
					    AUDIOHD_EXT_AMP_ENABLE);
				}

			} else {
				/* Turn off loopback recording */
				if (pin->device == DTYPE_LINE_IN) {
					pinctrl = audioha_codec_verb_get(statep,
					    path->codec->index, wid,
					    AUDIOHDC_VERB_GET_PIN_CTRL, 0);
					(void) audioha_codec_verb_get(statep,
					    path->codec->index, wid,
					    AUDIOHDC_VERB_SET_PIN_CTRL,
					    pinctrl & ~AUDIOHD_PIN_OUT_ENABLE);
				}
			}

		}
	}
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static int
audiohd_set_mic(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t	*statep = pc->statep;
	AUDIOHD_CHECK_2CHANNELS_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_pin_volume(statep, DTYPE_MIC_IN);
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static int
audiohd_set_cd(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t	*statep = pc->statep;
	AUDIOHD_CHECK_2CHANNELS_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_pin_volume(statep, DTYPE_CD);
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static int
audiohd_set_mongain(void *arg, uint64_t val)
{
	audiohd_ctrl_t	*pc = arg;
	audiohd_state_t	*statep = pc->statep;
	AUDIOHD_CHECK_2CHANNELS_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_monitor_gain(statep);
	mutex_exit(&statep->hda_mutex);

	return (0);
}

static int
audiohd_set_beep(void *arg, uint64_t val)
{
	audiohd_ctrl_t  *pc = arg;
	audiohd_state_t *statep = pc->statep;
	AUDIOHD_CHECK_CHANNEL_VOLUME(val);

	mutex_enter(&statep->hda_mutex);
	pc->val = val;
	audiohd_set_beep_volume(statep);
	mutex_exit(&statep->hda_mutex);

	return (0);
}

#define	PLAYCTL	(AUDIO_CTRL_FLAG_RW | AUDIO_CTRL_FLAG_PLAY)
#define	RECCTL	(AUDIO_CTRL_FLAG_RW | AUDIO_CTRL_FLAG_REC)
#define	MONCTL	(AUDIO_CTRL_FLAG_RW | AUDIO_CTRL_FLAG_MONITOR)
#define	PCMVOL	(PLAYCTL | AUDIO_CTRL_FLAG_PCMVOL)
#define	MONVOL	(MONCTL | AUDIO_CTRL_FLAG_MONVOL)
#define	MAINVOL	(PLAYCTL | AUDIO_CTRL_FLAG_MAINVOL)
#define	RECVOL	(RECCTL | AUDIO_CTRL_FLAG_RECVOL)

static void
audiohd_del_controls(audiohd_state_t *statep)
{
	int		i;
	for (i = 0; i < CTL_MAX; i++) {
		audiohd_ctrl_t *ac = &statep->ctrls[i];
		if (ac->ctrl != NULL) {
			audio_dev_del_control(ac->ctrl);
			ac->ctrl = NULL;
		}
	}
}

static void
audiohd_create_mono(audiohd_state_t *statep, int ctl,
    const char *id, int flags, int defval, audio_ctrl_wr_t fn)
{
	audiohd_ctrl_t		*ac;
	audio_ctrl_desc_t	desc;

	bzero(&desc, sizeof (desc));

	ac = &statep->ctrls[ctl];
	ac->statep = statep;
	ac->num = ctl;

	desc.acd_name = id;
	desc.acd_type = AUDIO_CTRL_TYPE_MONO;
	desc.acd_minvalue = 0;
	desc.acd_maxvalue = 100;
	desc.acd_flags = flags;

	ac->val = defval;
	ac->ctrl = audio_dev_add_control(statep->adev, &desc,
	    audiohd_get_control, fn, ac);
}

static void
audiohd_create_stereo(audiohd_state_t *statep, int ctl,
    const char *id, int flags, int defval, audio_ctrl_wr_t fn)
{
	audiohd_ctrl_t		*ac;
	audio_ctrl_desc_t	desc;

	bzero(&desc, sizeof (desc));

	ac = &statep->ctrls[ctl];
	ac->statep = statep;
	ac->num = ctl;

	desc.acd_name = id;
	desc.acd_type = AUDIO_CTRL_TYPE_STEREO;
	desc.acd_minvalue = 0;
	desc.acd_maxvalue = 100;
	desc.acd_flags = flags;

	ac->val = (defval << 8) | defval;
	ac->ctrl = audio_dev_add_control(statep->adev, &desc,
	    audiohd_get_control, fn, ac);
}

static void
audiohd_create_bool(audiohd_state_t *statep, int ctl,
    const char *id, int defval, audio_ctrl_wr_t fn)
{
	audiohd_ctrl_t		*ac;
	audio_ctrl_desc_t	desc;

	bzero(&desc, sizeof (desc));

	ac = &statep->ctrls[ctl];
	ac->statep = statep;
	ac->num = ctl;

	desc.acd_name = id;
	desc.acd_type = AUDIO_CTRL_TYPE_BOOLEAN;
	desc.acd_minvalue = 0;
	desc.acd_maxvalue = 1;
	desc.acd_flags = RECCTL;

	ac->val = defval;
	ac->ctrl = audio_dev_add_control(statep->adev, &desc,
	    audiohd_get_control, fn, ac);
}

static void
audiohd_create_recsrc(audiohd_state_t *statep)
{
	audiohd_ctrl_t		*ac;
	audio_ctrl_desc_t	desc;

	bzero(&desc, sizeof (desc));

	ac = &statep->ctrls[CTL_RECSRC];
	ac->statep = statep;
	ac->num = CTL_RECSRC;

	desc.acd_name = AUDIO_CTRL_ID_RECSRC;
	desc.acd_type = AUDIO_CTRL_TYPE_ENUM;
	desc.acd_flags = RECVOL;
	desc.acd_minvalue = statep->inmask;
	desc.acd_maxvalue = statep->inmask;
	for (int i = 0; audiohd_dtypes[i]; i++) {
		desc.acd_enum[i] = audiohd_dtypes[i];
	}

	ac->val = (1U << DTYPE_MIC_IN);
	ac->ctrl = audio_dev_add_control(statep->adev, &desc,
	    audiohd_get_control, audiohd_set_recsrc, ac);
}

static void
audiohd_create_controls(audiohd_state_t *statep)
{
	wid_t			wid;
	audiohd_widget_t	*widget;
	audiohd_path_t		*path;
	hda_codec_t		*codec;
	audiohd_pin_t		*pin;
	audiohd_pin_color_t	color;
	int			i, j;

	/*
	 * We always use soft volume control to adjust PCM volume.
	 */
	audio_dev_add_soft_volume(statep->adev);

	/* Allocate other controls */
	for (i = 0; i < statep->pathnum; i++) {
		if ((path = statep->path[i]) == NULL)
			continue;
		codec = path->codec;

		for (j = 0; j < path->pin_nums; j++) {
			wid = path->pin_wid[j];
			widget = codec->widget[wid];
			pin = (audiohd_pin_t *)widget->priv;
			color = (pin->config >> AUDIOHD_PIN_CLR_OFF) &
			    AUDIOHD_PIN_CLR_MASK;
			if (color == AUDIOHD_PIN_GREEN) {
				audiohd_create_stereo(statep, CTL_FRONT,
				    AUDIO_CTRL_ID_FRONT, MAINVOL, 75,
				    audiohd_set_front);
			} else if (color == AUDIOHD_PIN_BLACK &&
			    pin->device != DTYPE_HP_OUT &&
			    pin->device != DTYPE_MIC_IN) {
				audiohd_create_stereo(statep, CTL_REAR,
				    AUDIO_CTRL_ID_REAR, MAINVOL, 75,
				    audiohd_set_rear);
			} else if (color == AUDIOHD_PIN_ORANGE) {
				audiohd_create_mono(statep, CTL_CENTER,
				    AUDIO_CTRL_ID_CENTER, MAINVOL, 75,
				    audiohd_set_center);
				audiohd_create_mono(statep, CTL_LFE,
				    AUDIO_CTRL_ID_LFE, MAINVOL, 75,
				    audiohd_set_lfe);
			} else if (color == AUDIOHD_PIN_GREY) {
				audiohd_create_stereo(statep, CTL_SURROUND,
				    AUDIO_CTRL_ID_SURROUND, MAINVOL, 75,
				    audiohd_set_surround);
			}
			if (pin->device == DTYPE_SPEAKER) {
				audiohd_create_stereo(statep, CTL_SPEAKER,
				    AUDIO_CTRL_ID_SPEAKER, MAINVOL, 75,
				    audiohd_set_speaker);
			} else if (pin->device == DTYPE_HP_OUT) {
				audiohd_create_stereo(statep, CTL_HEADPHONE,
				    AUDIO_CTRL_ID_HEADPHONE, MAINVOL, 75,
				    audiohd_set_headphone);
			} else if (pin->device == DTYPE_LINE_IN) {
				audiohd_create_stereo(statep, CTL_LINEIN,
				    AUDIO_CTRL_ID_LINEIN, RECVOL, 50,
				    audiohd_set_linein);
			} else if (pin->device == DTYPE_MIC_IN) {
				audiohd_create_stereo(statep, CTL_MIC,
				    AUDIO_CTRL_ID_MIC, RECVOL, 50,
				    audiohd_set_mic);
			} else if (pin->device == DTYPE_CD) {
				audiohd_create_stereo(statep, CTL_CD,
				    AUDIO_CTRL_ID_CD, RECVOL, 50,
				    audiohd_set_cd);
			}
		}

		if (path->path_type == BEEP) {
			widget = codec->widget[path->beep_wid];
			if (widget->type == WTYPE_BEEP &&
			    path->gain_wid != 0) {
				audiohd_create_mono(statep, CTL_BEEP,
				    AUDIO_CTRL_ID_BEEP, AUDIO_CTRL_FLAG_RW, 75,
				    audiohd_set_beep);
				continue;
			}
		}
	}

	if (statep->monitor_supported) {
		audiohd_create_stereo(statep, CTL_MONGAIN,
		    AUDIO_CTRL_ID_MONGAIN, MONVOL, 0,
		    audiohd_set_mongain);
	}

	if (statep->loopback_supported) {
		audiohd_create_bool(statep, CTL_LOOP, AUDIO_CTRL_ID_LOOPBACK,
		    0, audiohd_set_loopback);
	}

	audiohd_create_recsrc(statep);
	audiohd_configure_output(statep);
	audiohd_configure_input(statep);
}

/*
 * quiesce(9E) entry point.
 *
 * This function is called when the system is single-threaded at high
 * PIL with preemption disabled. Therefore, this function must not be
 * blocked.
 *
 * This function returns DDI_SUCCESS on success, or DDI_FAILURE on failure.
 * DDI_FAILURE indicates an error condition and should almost never happen.
 */
static int
audiohd_quiesce(dev_info_t *dip)
{
	audiohd_state_t		*statep;

	statep = ddi_get_driver_private(dip);

	mutex_enter(&statep->hda_mutex);
	audiohd_stop_dma(statep);
	mutex_exit(&statep->hda_mutex);

	return (DDI_SUCCESS);
}

static void
audiohd_beep_on(void *arg)
{
	hda_codec_t *codec = ((audiohd_widget_t *)arg)->codec;
	audiohd_state_t *statep = codec->statep;
	int caddr = codec->index;
	wid_t wid = ((audiohd_widget_t *)arg)->wid_wid;

	mutex_enter(&statep->hda_mutex);
	(void) audioha_codec_verb_get(statep, caddr, wid,
	    AUDIOHDC_VERB_SET_BEEP_GEN, audiohd_beep_divider);
	mutex_exit(&statep->hda_mutex);
}

static void
audiohd_beep_off(void *arg)
{
	hda_codec_t *codec = ((audiohd_widget_t *)arg)->codec;
	audiohd_state_t *statep = codec->statep;
	int caddr = codec->index;
	wid_t wid = ((audiohd_widget_t *)arg)->wid_wid;

	mutex_enter(&statep->hda_mutex);
	(void) audioha_codec_verb_get(statep, caddr, wid,
	    AUDIOHDC_VERB_SET_BEEP_GEN, AUDIOHDC_MUTE_BEEP_GEN);
	mutex_exit(&statep->hda_mutex);
}

static void
audiohd_beep_freq(void *arg, int freq)
{
	hda_codec_t 	*codec = ((audiohd_widget_t *)arg)->codec;
	audiohd_state_t	*statep = codec->statep;
	uint32_t	vid = codec->vid >> 16;
	int		divider;

	_NOTE(ARGUNUSED(arg));
	if (freq == 0) {
		divider = 0;
	} else {
		if (freq > AUDIOHDC_MAX_BEEP_GEN)
			freq = AUDIOHDC_MAX_BEEP_GEN;
		else if (freq < AUDIOHDC_MIX_BEEP_GEN)
			freq = AUDIOHDC_MIX_BEEP_GEN;

		switch (vid) {
		case AUDIOHD_VID_SIGMATEL:
			/*
			 * Sigmatel HD codec specification:
			 * frequency = 48000 * (257 - Divider) / 1024
			 */
			divider = 257 - freq * 1024 / AUDIOHDC_SAMPR48000;
			break;
		default:
			divider = AUDIOHDC_SAMPR48000 / freq;
			break;
		}
	}

	if (audiohd_beep_vol == 0)
		divider = 0;

	mutex_enter(&statep->hda_mutex);
	audiohd_beep_divider = divider;
	mutex_exit(&statep->hda_mutex);
}

/*
 * audiohd_init_state()
 *
 * Description
 *	This routine initializes soft state of driver instance,
 *	also, it requests an interrupt cookie and initializes
 *	mutex for soft state.
 */
/*ARGSUSED*/
static int
audiohd_init_state(audiohd_state_t *statep, dev_info_t *dip)
{
	audio_dev_t	*adev;

	statep->hda_dip = dip;
	statep->hda_rirb_rp = 0;

	if ((adev = audio_dev_alloc(dip, 0)) == NULL) {
		audio_dev_warn(NULL, "unable to allocate audio dev");
		return (DDI_FAILURE);
	}
	statep->adev = adev;

	/* set device information */
	audio_dev_set_description(adev, AUDIOHD_DEV_CONFIG);
	audio_dev_set_version(adev, AUDIOHD_DEV_VERSION);

	return (DDI_SUCCESS);
}	/* audiohd_init_state() */

/*
 * audiohd_init_pci()
 *
 * Description
 *	enable driver to access PCI configure space and memory
 *	I/O space.
 */
static int
audiohd_init_pci(audiohd_state_t *statep, ddi_device_acc_attr_t *acc_attr)
{
	uint16_t	cmdreg;
	uint16_t	vid;
	uint8_t		cTmp;
	dev_info_t	*dip = statep->hda_dip;
	audio_dev_t	*adev = statep->adev;

	if (pci_config_setup(dip, &statep->hda_pci_handle) == DDI_FAILURE) {
		audio_dev_warn(adev,
		    "pci config mapping failed");
		return (DDI_FAILURE);
	}

	if (ddi_regs_map_setup(dip, 1, &statep->hda_reg_base, 0,
	    0, acc_attr, &statep->hda_reg_handle) != DDI_SUCCESS) {
		audio_dev_warn(adev,
		    "memory I/O mapping failed");
		return (DDI_FAILURE);
	}

	/*
	 * HD audio control uses memory I/O only, enable it here.
	 */
	cmdreg = pci_config_get16(statep->hda_pci_handle, PCI_CONF_COMM);
	pci_config_put16(statep->hda_pci_handle, PCI_CONF_COMM,
	    cmdreg | PCI_COMM_MAE | PCI_COMM_ME);

	vid = pci_config_get16(statep->hda_pci_handle, PCI_CONF_VENID);
	switch (vid) {
	case AUDIOHD_VID_INTEL:
		/*
		 * Currently, Intel (G)MCH and ICHx chipsets support PCI
		 * Express QoS. It implements two VCs(virtual channels)
		 * and allows OS software to map 8 traffic classes to the
		 * two VCs. Some BIOSes initialize HD audio hardware to
		 * use TC7 (traffic class 7) and to map TC7 to VC1 as Intel
		 * recommended. However, solaris doesn't support PCI express
		 * QoS yet. As a result, this driver can not work for those
		 * hardware without touching PCI express control registers.
		 * Here, we set TCSEL to 0 so as to use TC0/VC0 (VC0 is
		 * always enabled and TC0 is always mapped to VC0) for all
		 * Intel HD audio controllers.
		 */
		cTmp = pci_config_get8(statep->hda_pci_handle,
		    AUDIOHD_INTEL_PCI_TCSEL);
		pci_config_put8(statep->hda_pci_handle,
		    AUDIOHD_INTEL_PCI_TCSEL, (cTmp & AUDIOHD_INTEL_TCS_MASK));
		break;
	case AUDIOHD_VID_ATI:
		/*
		 * Refer to ATI SB450 datesheet. We set snoop for SB450
		 * like hardware.
		 */
		cTmp = pci_config_get8(statep->hda_pci_handle,
		    AUDIOHD_ATI_PCI_MISC2);
		pci_config_put8(statep->hda_pci_handle, AUDIOHD_ATI_PCI_MISC2,
		    (cTmp & AUDIOHD_ATI_MISC2_MASK) | AUDIOHD_ATI_MISC2_SNOOP);
		break;
	case AUDIOHD_VID_NVIDIA:
		/*
		 * Refer to the datasheet, we set snoop for NVIDIA
		 * like hardware
		 */
		cTmp = pci_config_get8(statep->hda_pci_handle,
		    AUDIOHD_CORB_SIZE_OFF);
		pci_config_put8(statep->hda_pci_handle, AUDIOHD_CORB_SIZE_OFF,
		    cTmp | AUDIOHD_NVIDIA_SNOOP);
		break;
	default:
		break;
	}

	return (DDI_SUCCESS);
}	/* audiohd_init_pci() */

/*
 * audiohd_fini_pci()
 *
 * Description
 *	Release mapping for PCI configure space.
 */
static void
audiohd_fini_pci(audiohd_state_t *statep)
{
	if (statep->hda_reg_handle != NULL) {
		ddi_regs_map_free(&statep->hda_reg_handle);
	}

	if (statep->hda_pci_handle != NULL) {
		pci_config_teardown(&statep->hda_pci_handle);
	}

}	/* audiohd_fini_pci() */

/*
 * audiohd_stop_dma()
 *
 * Description
 *	Stop all DMA behaviors of controllers, for command I/O
 *	and each audio stream.
 */
static void
audiohd_stop_dma(audiohd_state_t *statep)
{
	int	i;
	uint_t	base;
	uint8_t	bTmp;

	AUDIOHD_REG_SET8(AUDIOHD_REG_CORBCTL, 0);
	AUDIOHD_REG_SET8(AUDIOHD_REG_RIRBCTL, 0);

	base = AUDIOHD_REG_SD_BASE;
	for (i = 0; i < statep->hda_streams_nums; i++) {
		bTmp = AUDIOHD_REG_GET8(base + AUDIOHD_SDREG_OFFSET_CTL);

		/* for input/output stream, it is the same */
		bTmp &= ~AUDIOHDR_RIRBCTL_DMARUN;

		AUDIOHD_REG_SET8(base + AUDIOHD_SDREG_OFFSET_CTL, bTmp);
		base += AUDIOHD_REG_SD_LEN;
	}

	/* wait 40us for stream DMA to stop */
	drv_usecwait(40);

}	/* audiohd_stop_dma() */

/*
 * audiohd_reset_controller()
 *
 * Description:
 *	This routine is just used to reset controller and
 *	CODEC as well by HW reset bit in global control
 *	register of HD controller.
 */
static int
audiohd_reset_controller(audiohd_state_t *statep)
{
	int		i;
	uint16_t	sTmp;
	uint32_t	gctl;

	/* Reset Status register but preserve the first bit */
	sTmp = AUDIOHD_REG_GET16(AUDIOHD_REG_STATESTS);
	AUDIOHD_REG_SET16(AUDIOHD_REG_STATESTS, sTmp & 0x8000);

	/* reset controller */
	gctl = AUDIOHD_REG_GET32(AUDIOHD_REG_GCTL);
	gctl &= ~AUDIOHDR_GCTL_CRST;
	AUDIOHD_REG_SET32(AUDIOHD_REG_GCTL, gctl);  /* entering reset state */
	for (i = 0; i < AUDIOHD_RETRY_TIMES; i++) {
		/* Empirical testing time: 150 */
		drv_usecwait(150);
		gctl = AUDIOHD_REG_GET32(AUDIOHD_REG_GCTL);
		if ((gctl & AUDIOHDR_GCTL_CRST) == 0)
			break;
	}

	if ((gctl & AUDIOHDR_GCTL_CRST) != 0) {
		audio_dev_warn(statep->adev,
		    "failed to enter reset state");
		return (DDI_FAILURE);
	}

	/* Empirical testing time:300 */
	drv_usecwait(300);

	/* exit reset state */
	AUDIOHD_REG_SET32(AUDIOHD_REG_GCTL, gctl | AUDIOHDR_GCTL_CRST);

	for (i = 0; i < AUDIOHD_RETRY_TIMES; i++) {
		/* Empirical testing time: 150, which works well */
		drv_usecwait(150);
		gctl = AUDIOHD_REG_GET32(AUDIOHD_REG_GCTL);
		if (gctl & AUDIOHDR_GCTL_CRST)
			break;
	}

	if ((gctl & AUDIOHDR_GCTL_CRST) == 0) {
		audio_dev_warn(statep->adev,
		    "failed to exit reset state");
		return (DDI_FAILURE);
	}

	/* HD spec requires to wait 250us at least. we use 500us */
	drv_usecwait(500);

	/* enable unsolicited response */
	AUDIOHD_REG_SET32(AUDIOHD_REG_GCTL, gctl | AUDIOHDR_GCTL_URESPE);

	return (DDI_SUCCESS);

}	/* audiohd_reset_controller() */

/*
 * audiohd_alloc_dma_mem()
 *
 * Description:
 *	This is an utility routine. It is used to allocate DMA
 *	memory.
 */
static int
audiohd_alloc_dma_mem(audiohd_state_t *statep, audiohd_dma_t *pdma,
    size_t memsize, ddi_dma_attr_t *dma_attr_p, uint_t dma_flags)
{
	ddi_dma_cookie_t	cookie;
	uint_t			count;
	dev_info_t		*dip = statep->hda_dip;
	audio_dev_t		*adev = statep->adev;

	if (ddi_dma_alloc_handle(dip, dma_attr_p, DDI_DMA_SLEEP,
	    NULL, &pdma->ad_dmahdl) != DDI_SUCCESS) {
		audio_dev_warn(adev,
		    "ddi_dma_alloc_handle failed");
		return (DDI_FAILURE);
	}

	if (ddi_dma_mem_alloc(pdma->ad_dmahdl, memsize, &hda_dev_accattr,
	    dma_flags & (DDI_DMA_CONSISTENT | DDI_DMA_STREAMING),
	    DDI_DMA_SLEEP, NULL,
	    (caddr_t *)&pdma->ad_vaddr, &pdma->ad_real_sz,
	    &pdma->ad_acchdl) != DDI_SUCCESS) {
		audio_dev_warn(adev,
		    "ddi_dma_mem_alloc failed");
		return (DDI_FAILURE);
	}

	if (ddi_dma_addr_bind_handle(pdma->ad_dmahdl, NULL,
	    (caddr_t)pdma->ad_vaddr, pdma->ad_real_sz, dma_flags,
	    DDI_DMA_SLEEP, NULL, &cookie, &count) != DDI_DMA_MAPPED) {
		audio_dev_warn(adev,
		    "ddi_dma_addr_bind_handle failed");
		return (DDI_FAILURE);
	}

	pdma->ad_paddr = (uint64_t)(cookie.dmac_laddress);
	pdma->ad_req_sz = memsize;

	return (DDI_SUCCESS);
}	/* audiohd_alloc_dma_mem() */

/*
 * audiohd_release_dma_mem()
 *
 * Description:
 *	Release DMA memory.
 */

static void
audiohd_release_dma_mem(audiohd_dma_t *pdma)
{
	if (pdma->ad_dmahdl != NULL) {
		(void) ddi_dma_unbind_handle(pdma->ad_dmahdl);
	}

	if (pdma->ad_acchdl != NULL) {
		ddi_dma_mem_free(&pdma->ad_acchdl);
		pdma->ad_acchdl = NULL;
	}

	if (pdma->ad_dmahdl != NULL) {
		ddi_dma_free_handle(&pdma->ad_dmahdl);
		pdma->ad_dmahdl = NULL;
	}

}	/* audiohd_release_dma_mem() */

/*
 * audiohd_reinit_hda()
 *
 * Description:
 *	This routine is used to re-initialize HD controller and codec.
 */
static int
audiohd_reinit_hda(audiohd_state_t *statep)
{
	uint64_t	addr;

	/* set PCI configure space in case it's not restored OK */
	(void) audiohd_init_pci(statep, &hda_dev_accattr);

	/* reset controller */
	if (audiohd_reset_controller(statep) != DDI_SUCCESS)
		return (DDI_FAILURE);
	AUDIOHD_REG_SET32(AUDIOHD_REG_SYNC, 0); /* needn't sync stream */

	/* Initialize controller RIRB */
	addr = statep->hda_dma_rirb.ad_paddr;
	AUDIOHD_REG_SET32(AUDIOHD_REG_RIRBLBASE, (uint32_t)addr);
	AUDIOHD_REG_SET32(AUDIOHD_REG_RIRBUBASE,
	    (uint32_t)(addr >> 32));
	AUDIOHD_REG_SET16(AUDIOHD_REG_RIRBWP, AUDIOHDR_RIRBWP_RESET);
	AUDIOHD_REG_SET8(AUDIOHD_REG_RIRBSIZE, AUDIOHDR_RIRBSZ_256);
	AUDIOHD_REG_SET8(AUDIOHD_REG_RIRBCTL, AUDIOHDR_RIRBCTL_DMARUN |
	    AUDIOHDR_RIRBCTL_RINTCTL);

	/* Initialize controller CORB */
	addr = statep->hda_dma_corb.ad_paddr;
	AUDIOHD_REG_SET16(AUDIOHD_REG_CORBRP, AUDIOHDR_CORBRP_RESET);
	AUDIOHD_REG_SET32(AUDIOHD_REG_CORBLBASE, (uint32_t)addr);
	AUDIOHD_REG_SET32(AUDIOHD_REG_CORBUBASE,
	    (uint32_t)(addr >> 32));
	AUDIOHD_REG_SET8(AUDIOHD_REG_CORBSIZE, AUDIOHDR_CORBSZ_256);
	AUDIOHD_REG_SET16(AUDIOHD_REG_CORBWP, 0);
	AUDIOHD_REG_SET16(AUDIOHD_REG_CORBRP, 0);
	AUDIOHD_REG_SET8(AUDIOHD_REG_CORBCTL, AUDIOHDR_CORBCTL_DMARUN);

	audiohd_restore_codec_gpio(statep);
	audiohd_restore_path(statep);
	audiohd_init_path(statep);

	return (DDI_SUCCESS);
}	/* audiohd_reinit_hda */

/*
 * audiohd_init_controller()
 *
 * Description:
 *	This routine is used to initialize HD controller. It
 *	allocates DMA memory for CORB/RIRB, buffer descriptor
 *	list and cylic data buffer for both play and record
 *	stream.
 */
static int
audiohd_init_controller(audiohd_state_t *statep)
{
	uint64_t	addr;
	uint16_t	gcap;
	int		retval;

	ddi_dma_attr_t	dma_attr = {
		DMA_ATTR_V0,		/* version */
		0,			/* addr_lo */
		0xffffffffffffffffULL,	/* addr_hi */
		0x00000000ffffffffULL,	/* count_max */
		128,			/* 128-byte alignment as HD spec */
		0xfff,			/* burstsize */
		1,			/* minxfer */
		0xffffffff,		/* maxxfer */
		0xffffffff,		/* seg */
		1,			/* sgllen */
		1,			/* granular */
		0			/* flags */
	};

	gcap = AUDIOHD_REG_GET16(AUDIOHD_REG_GCAP);

	/*
	 * If the device doesn't support 64-bit DMA, we should not
	 * allocate DMA memory from 4G above
	 */
	if ((gcap & AUDIOHDR_GCAP_64OK) == 0)
		dma_attr.dma_attr_addr_hi = 0xffffffffUL;

	statep->hda_input_streams = (gcap & AUDIOHDR_GCAP_INSTREAMS) >>
	    AUDIOHD_INSTR_NUM_OFF;
	statep->hda_output_streams = (gcap & AUDIOHDR_GCAP_OUTSTREAMS) >>
	    AUDIOHD_OUTSTR_NUM_OFF;
	statep->hda_streams_nums = statep->hda_input_streams +
	    statep->hda_output_streams;

	statep->hda_record_regbase = AUDIOHD_REG_SD_BASE;
	statep->hda_play_regbase = AUDIOHD_REG_SD_BASE + AUDIOHD_REG_SD_LEN *
	    statep->hda_input_streams;

	/* stop all dma before starting to reset controller */
	audiohd_stop_dma(statep);

	if (audiohd_reset_controller(statep) != DDI_SUCCESS)
		return (DDI_FAILURE);

	/* check codec */
	statep->hda_codec_mask = AUDIOHD_REG_GET16(AUDIOHD_REG_STATESTS);
	if (!statep->hda_codec_mask) {
		audio_dev_warn(statep->adev,
		    "no codec exists");
		return (DDI_FAILURE);
	}

	/* allocate DMA for CORB */
	retval = audiohd_alloc_dma_mem(statep, &statep->hda_dma_corb,
	    AUDIOHD_CDBIO_CORB_LEN, &dma_attr,
	    DDI_DMA_WRITE | DDI_DMA_STREAMING);
	if (retval != DDI_SUCCESS) {
		audio_dev_warn(statep->adev,
		    "failed to alloc DMA for CORB");
		return (DDI_FAILURE);
	}

	/* allocate DMA for RIRB */
	retval = audiohd_alloc_dma_mem(statep, &statep->hda_dma_rirb,
	    AUDIOHD_CDBIO_RIRB_LEN, &dma_attr,
	    DDI_DMA_READ | DDI_DMA_STREAMING);
	if (retval != DDI_SUCCESS) {
		audio_dev_warn(statep->adev,
		    "failed to alloc DMA for RIRB");
		return (DDI_FAILURE);
	}

	AUDIOHD_REG_SET32(AUDIOHD_REG_SYNC, 0); /* needn't sync stream */

	/* Initialize RIRB */
	addr = statep->hda_dma_rirb.ad_paddr;
	AUDIOHD_REG_SET32(AUDIOHD_REG_RIRBLBASE, (uint32_t)addr);
	AUDIOHD_REG_SET32(AUDIOHD_REG_RIRBUBASE, (uint32_t)(addr >> 32));
	AUDIOHD_REG_SET16(AUDIOHD_REG_RIRBWP, AUDIOHDR_RIRBWP_RESET);
	AUDIOHD_REG_SET8(AUDIOHD_REG_RIRBSIZE, AUDIOHDR_RIRBSZ_256);
	AUDIOHD_REG_SET8(AUDIOHD_REG_RIRBCTL, AUDIOHDR_RIRBCTL_DMARUN |
	    AUDIOHDR_RIRBCTL_RINTCTL);

	/* initialize CORB */
	addr = statep->hda_dma_corb.ad_paddr;
	AUDIOHD_REG_SET16(AUDIOHD_REG_CORBRP, AUDIOHDR_CORBRP_RESET);
	AUDIOHD_REG_SET32(AUDIOHD_REG_CORBLBASE, (uint32_t)addr);
	AUDIOHD_REG_SET32(AUDIOHD_REG_CORBUBASE, (uint32_t)(addr >> 32));
	AUDIOHD_REG_SET8(AUDIOHD_REG_CORBSIZE, AUDIOHDR_CORBSZ_256);
	AUDIOHD_REG_SET16(AUDIOHD_REG_CORBWP, 0);
	AUDIOHD_REG_SET16(AUDIOHD_REG_CORBRP, 0);
	AUDIOHD_REG_SET8(AUDIOHD_REG_CORBCTL, AUDIOHDR_CORBCTL_DMARUN);

	return (DDI_SUCCESS);
}	/* audiohd_init_controller() */

/*
 * audiohd_fini_controller()
 *
 * Description:
 *	Releases DMA memory allocated in audiohd_init_controller()
 */
static void
audiohd_fini_controller(audiohd_state_t *statep)
{
	audiohd_release_dma_mem(&statep->hda_dma_rirb);
	audiohd_release_dma_mem(&statep->hda_dma_corb);

}	/* audiohd_fini_controller() */

/*
 * audiohd_get_conns_from_entry()
 *
 * Description:
 *	Get connection list from every entry for a widget
 */
static void
audiohd_get_conns_from_entry(hda_codec_t *codec, audiohd_widget_t *widget,
    uint32_t entry, audiohd_entry_prop_t *prop)
{
	int	i, k, num;
	wid_t	input_wid;

	for (i = 0; i < prop->conns_per_entry &&
	    widget->nconns < prop->conn_len;
	    i++, entry >>= prop->bits_per_conn) {
		ASSERT(widget->nconns < AUDIOHD_MAX_CONN);
		input_wid = entry & prop->mask_wid;
		if (entry & prop->mask_range) {
			if (widget->nconns == 0) {
				if (input_wid < codec->first_wid ||
				    (input_wid > codec->last_wid)) {
					break;
				}
				widget->avail_conn[widget->nconns++] =
				    input_wid;
			} else {
				for (k = widget->avail_conn[widget->nconns-1] +
				    1; k <= input_wid; k++) {
					ASSERT(widget->nconns <
					    AUDIOHD_MAX_CONN);
					if (k < codec->first_wid ||
					    (k > codec->last_wid)) {
						break;
					} else {
						num = widget->nconns;
						widget->avail_conn[num] = k;
						widget->nconns++;
					}
				}
			}
		} else {
			if ((codec->first_wid <= input_wid) && (input_wid <=
			    codec->last_wid))
				widget->avail_conn[widget->nconns++] =
				    input_wid;
		}
	}
}

/*
 * audiohd_get_conns()
 *
 * Description:
 *	Get all connection list for a widget. The connection list is used for
 *	build output path, input path, and monitor path
 */
static void
audiohd_get_conns(hda_codec_t *codec, wid_t wid)
{
	audiohd_state_t		*statep = codec->statep;
	audiohd_widget_t	*widget = codec->widget[wid];
	uint8_t			caddr = codec->index;
	uint32_t		entry;
	audiohd_entry_prop_t	prop;
	wid_t			input_wid;
	int			i;

	prop.conn_len = audioha_codec_verb_get(statep, caddr, wid,
	    AUDIOHDC_VERB_GET_PARAM, AUDIOHDC_PAR_CONNLIST_LEN);

	if (prop.conn_len & AUDIOHD_FORM_MASK) {
		prop.conns_per_entry = 2;
		prop.bits_per_conn = 16;
		prop.mask_range = 0x00008000;
		prop.mask_wid = 0x00007fff;
	} else {
		prop.conns_per_entry = 4;
		prop.bits_per_conn = 8;
		prop.mask_range = 0x00000080;
		prop.mask_wid = 0x0000007f;
	}
	prop.conn_len &= AUDIOHD_LEN_MASK;

	/*
	 * This should not happen since the ConnectionList bit of
	 * widget capabilities already told us that this widget
	 * has a connection list
	 */
	if (prop.conn_len == 0) {
		widget->nconns = 0;
		audio_dev_warn(statep->adev,
		    "node %d has 0 connections", wid);
		return;
	}

	if (prop.conn_len == 1) {
		entry = audioha_codec_verb_get(statep, caddr,
		    wid, AUDIOHDC_VERB_GET_CONN_LIST_ENT, 0);
		input_wid = entry & prop.mask_wid;
		if ((input_wid < codec->first_wid) ||
		    (input_wid > codec->last_wid)) {
			return;
		}
		widget->avail_conn[0] = input_wid;
		widget->nconns = 1;
		return;
	}
	widget->nconns = 0;
	for (i = 0; i < prop.conn_len; i += prop.conns_per_entry) {
		entry = audioha_codec_verb_get(statep, caddr, wid,
		    AUDIOHDC_VERB_GET_CONN_LIST_ENT, i);
		audiohd_get_conns_from_entry(codec, widget, entry, &prop);
	}
}

/*
 * Read PinCapabilities & default configuration
 */
static void
audiohd_get_pin_config(audiohd_widget_t *widget)
{
	hda_codec_t		*codec = widget->codec;
	audiohd_state_t		*statep = codec->statep;
	audiohd_pin_t		*pin, *prev, *p;

	int		caddr = codec->index;
	wid_t		wid = widget->wid_wid;
	uint32_t	cap, config, pinctrl;
	uint8_t		urctrl, vrefbits;

	cap = audioha_codec_verb_get(statep, caddr, wid,
	    AUDIOHDC_VERB_GET_PARAM, AUDIOHDC_PAR_PIN_CAP);
	config = audioha_codec_verb_get(statep, caddr,
	    wid, AUDIOHDC_VERB_GET_DEFAULT_CONF, 0);
	pinctrl = audioha_codec_verb_get(statep, caddr,
	    wid, AUDIOHDC_VERB_GET_PIN_CTRL, 0);

	pin = (audiohd_pin_t *)kmem_zalloc(sizeof (audiohd_pin_t), KM_SLEEP);
	widget->priv = pin;

	/*
	 * If the pin has no physical connection for port,
	 * we won't link it to pin linkage list ???
	 */
	if (((config >> AUDIOHD_PIN_CON_STEP) & AUDIOHD_PIN_CON_MASK) == 0x1) {
		pin->no_phys_conn = 1;
	}

	/* bit 4:3 are reserved, read-modify-write is needed */
	pin->ctrl = pinctrl & AUDIOHD_PIN_IO_MASK;
	pin->wid = wid;
	pin->cap = cap;
	pin->config = config;
	pin->num = 0;
	pin->finish = 0;

	vrefbits = (cap >> AUDIOHD_PIN_VREF_OFF) & AUDIOHD_PIN_VREF_MASK;
	if (vrefbits & AUDIOHD_PIN_VREF_L1)
		pin->vrefvalue = 0x5;
	else if (vrefbits & AUDIOHD_PIN_VREF_L2)
		pin->vrefvalue = 0x4;
	else if (vrefbits & AUDIOHD_PIN_VREF_L3)
		pin->vrefvalue = 0x2;
	else
		pin->vrefvalue = 0x1;

	pin->seq = config & AUDIOHD_PIN_SEQ_MASK;
	pin->assoc = (config & AUDIOHD_PIN_ASO_MASK) >> AUDIOHD_PIN_ASO_OFF;
	pin->device = (config & AUDIOHD_PIN_DEV_MASK) >> AUDIOHD_PIN_DEV_OFF;

	/* enable the unsolicited response of the pin */
	if ((widget->widget_cap & AUDIOHD_URCAP_MASK) &&
	    (pin->cap & AUDIOHD_DTCCAP_MASK) &&
	    ((pin->device == DTYPE_LINEOUT) ||
	    (pin->device == DTYPE_SPDIF_OUT) ||
	    (pin->device == DTYPE_HP_OUT) ||
	    (pin->device == DTYPE_MIC_IN))) {
			urctrl = (uint8_t)(1 << (AUDIOHD_UR_ENABLE_OFF - 1));
			urctrl |= (wid & AUDIOHD_UR_TAG_MASK);
			(void) audioha_codec_verb_get(statep, caddr,
			    wid, AUDIOHDC_VERB_SET_UNS_ENABLE, urctrl);
	}
	/* accommodate all the pins in a link list sorted by assoc and seq */
	if (codec->first_pin == NULL) {
		codec->first_pin = pin;
	} else {
		prev = NULL;
		p = codec->first_pin;
		while (p) {
			if (p->assoc > pin->assoc)
				break;
			if ((p->assoc == pin->assoc) &&
			    (p->seq > pin->seq))
				break;
			prev = p;
			p = p->next;
		}
		if (prev) {
			pin->next = prev->next;
			prev->next = pin;
		} else {
			pin->next = codec->first_pin;
			codec->first_pin = pin;
		}
	}

}	/* audiohd_get_pin_config() */

/*
 * audiohd_create_widgets()
 *
 * Description:
 *	All widgets are created and stored in an array of codec
 */
static int
audiohd_create_widgets(hda_codec_t *codec)
{
	audiohd_widget_t	*widget;
	audiohd_state_t		*statep = codec->statep;
	wid_t			wid;
	uint32_t		type, widcap;
	int			caddr = codec->index;

	for (wid = codec->first_wid;
	    wid <= codec->last_wid; wid++) {
		widget = (audiohd_widget_t *)
		    kmem_zalloc(sizeof (audiohd_widget_t), KM_SLEEP);
		codec->widget[wid] = widget;
		widget->codec = codec;
		widget->output_path_next = AUDIOHD_NULL_CONN;
		widget->input_path_next = AUDIOHD_NULL_CONN;
		widget->beep_path_next = AUDIOHD_NULL_CONN;
		widget->loopback_path_next = AUDIOHD_NULL_CONN;

		widcap = audioha_codec_verb_get(statep, caddr, wid,
		    AUDIOHDC_VERB_GET_PARAM, AUDIOHDC_PAR_AUDIO_WID_CAP);
		type = AUDIOHD_WIDCAP_TO_WIDTYPE(widcap);
		widget->wid_wid = wid;
		widget->type = type;
		widget->widget_cap = widcap;
		widget->finish = 0;
		widget->used = 0;

		/* if there's connection list */
		if (widcap & AUDIOHD_WIDCAP_CONNLIST) {
			audiohd_get_conns(codec, wid);
		}

		/* if power control, power it up to D0 state */
		if (widcap & AUDIOHD_WIDCAP_PWRCTRL) {
			(void) audioha_codec_verb_get(statep, caddr, wid,
			    AUDIOHDC_VERB_SET_POWER_STATE, 0);
		}

		/*
		 * if this widget has format override, we read it.
		 * Otherwise, it uses the format of audio function.
		 */
		if (widcap & AUDIOHD_WIDCAP_FMT_OVRIDE) {
			widget->pcm_format =
			    audioha_codec_verb_get(statep, caddr, wid,
			    AUDIOHDC_VERB_GET_PARAM, AUDIOHDC_PAR_PCM);
		} else {
			widget->pcm_format = codec->pcm_format;
		}

		/*
		 * Input amplifier. Has the widget input amplifier ?
		 */
		if (widcap & AUDIOHD_WIDCAP_INAMP) {
			/*
			 * if overrided bit is 0, use the default
			 * amplifier of audio function as HD spec.
			 * Otherwise, we read it.
			 */
			if ((widcap & AUDIOHD_WIDCAP_AMP_OVRIDE) == 0)
				widget->inamp_cap = codec->inamp_cap;
			else
				widget->inamp_cap =
				    audioha_codec_verb_get(statep, caddr, wid,
				    AUDIOHDC_VERB_GET_PARAM,
				    AUDIOHDC_PAR_INAMP_CAP);
		} else {
			widget->inamp_cap = 0;
		}

		/*
		 * output amplifier. Has this widget output amplifier ?
		 */
		if (widcap & AUDIOHD_WIDCAP_OUTAMP) {
			if ((widcap & AUDIOHD_WIDCAP_AMP_OVRIDE) == 0)
				widget->outamp_cap = codec->outamp_cap;
			else
				widget->outamp_cap =
				    audioha_codec_verb_get(statep, caddr, wid,
				    AUDIOHDC_VERB_GET_PARAM,
				    AUDIOHDC_PAR_OUTAMP_CAP);
		} else {
			widget->outamp_cap = 0;
		}

		switch (type) {
		case WTYPE_AUDIO_OUT:
		case WTYPE_AUDIO_IN:
		case WTYPE_AUDIO_MIX:
		case WTYPE_AUDIO_SEL:
		case WTYPE_VENDOR:
		case WTYPE_POWER:
		case WTYPE_VOL_KNOB:
			break;
		case WTYPE_PIN:
			/*
			 * Some codec(like ALC262) don't provide beep widget,
			 * it only has input Pin to connect an external beep
			 * (maybe in motherboard or elsewhere). So we open
			 * all PINs here in order to enable external beep
			 * source.
			 */
			if (codec->codec_info->flags & EN_PIN_BEEP) {
				(void) audioha_codec_4bit_verb_get(statep,
				    caddr, widget->wid_wid,
				    AUDIOHDC_VERB_SET_AMP_MUTE,
				    AUDIOHDC_AMP_SET_LR_OUTPUT |
				    AUDIOHDC_GAIN_MAX);
			}

			audiohd_get_pin_config(widget);
			break;
		case WTYPE_BEEP:
			/*
			 * Get the audiohd_beep_switch value from audiohd.conf,
			 * which is for turning on/off widget beep.
			 */
			audiohd_beep = ddi_prop_get_int(DDI_DEV_T_ANY,
			    statep->hda_dip,
			    DDI_PROP_DONTPASS, "audiohd_beep", 1);

			if (audiohd_beep) {
				(void) beep_fini();
				(void) beep_init((void *) widget,
				    audiohd_beep_on,
				    audiohd_beep_off,
				    audiohd_beep_freq);
			}
			break;
		default:
			break;
		}
	}

	return (DDI_SUCCESS);

}	/* audiohd_create_widgets() */

/*
 * audiohd_destroy_widgets()
 */
static void
audiohd_destroy_widgets(hda_codec_t *codec)
{
	for (int i = 0; i < AUDIOHD_MAX_WIDGET; i++) {
		if (codec->widget[i]) {
			kmem_free(codec->widget[i], sizeof (audiohd_widget_t));
			codec->widget[i] = NULL;
		}
	}

}	/* audiohd_destroy_widgets() */

/*
 * audiohd_create_codec()
 *
 * Description:
 *	Searching for supported CODEC. If find, allocate memory
 *	to hold codec structure.
 */
static int
audiohd_create_codec(audiohd_state_t *statep)
{
	hda_codec_t	*codec;
	uint32_t	mask, type;
	uint32_t	nums;
	uint32_t	i, j, len;
	wid_t		wid;
	char		buf[128];
	int		rate, bits;
	dev_info_t	*dip = statep->hda_dip;

	mask = statep->hda_codec_mask;
	ASSERT(mask != 0);

	for (i = 0; i < AUDIOHD_CODEC_MAX; i++) {
		if ((mask & (1 << i)) == 0)
			continue;
		codec = (hda_codec_t *)kmem_zalloc(
		    sizeof (hda_codec_t), KM_SLEEP);
		codec->index = i;
		codec->vid = audioha_codec_verb_get(statep, i,
		    AUDIOHDC_NODE_ROOT, AUDIOHDC_VERB_GET_PARAM,
		    AUDIOHDC_PAR_VENDOR_ID);
		if (codec->vid == (uint32_t)(-1)) {
			kmem_free(codec, sizeof (hda_codec_t));
			continue;
		}

		codec->revid =
		    audioha_codec_verb_get(statep, i,
		    AUDIOHDC_NODE_ROOT, AUDIOHDC_VERB_GET_PARAM,
		    AUDIOHDC_PAR_REV_ID);

		nums = audioha_codec_verb_get(statep,
		    i, AUDIOHDC_NODE_ROOT,
		    AUDIOHDC_VERB_GET_PARAM, AUDIOHDC_PAR_NODE_COUNT);
		if (nums == (uint32_t)(-1)) {
			kmem_free(codec, sizeof (hda_codec_t));
			continue;
		}
		wid = (nums >> AUDIOHD_CODEC_STR_OFF) & AUDIOHD_CODEC_STR_MASK;
		nums = nums & AUDIOHD_CODEC_NUM_MASK;

		/*
		 * Assume that each codec has just one audio function group
		 */
		for (j = 0; j < nums; j++, wid++) {
			type = audioha_codec_verb_get(statep, i, wid,
			    AUDIOHDC_VERB_GET_PARAM,
			    AUDIOHDC_PAR_FUNCTION_TYPE);
			if ((type & AUDIOHD_CODEC_TYPE_MASK) ==
			    AUDIOHDC_AUDIO_FUNC_GROUP) {
				codec->wid_afg = wid;
				break;
			}
		}

		if (codec->wid_afg == 0) {
			kmem_free(codec, sizeof (hda_codec_t));
			continue;
		}

		ASSERT(codec->wid_afg == wid);

		len = sizeof (audiohd_codecs) / sizeof (audiohd_codec_info_t);
		for (j = 0; j < len-1; j++) {
			if (audiohd_codecs[j].devid == codec->vid) {
				codec->codec_info = &(audiohd_codecs[j]);
				break;
			}
		}

		if (codec->codec_info == NULL) {
			codec->codec_info = &(audiohd_codecs[len-1]);
			(void) snprintf(buf, sizeof (buf),
			    "Unknown HD codec: 0x%x", codec->vid);
		} else {
			(void) snprintf(buf, sizeof (buf), "HD codec: %s",
			    codec->codec_info->buf);
		}
		audio_dev_add_info(statep->adev, buf);

		/* work around for Sony VAIO laptop with specific codec */
		if ((codec->codec_info->flags & NO_GPIO) == 0) {
			/*
			 * GPIO controls which are laptop specific workarounds
			 * and might be changed. Some laptops use GPIO,
			 * so we need to enable and set the GPIO correctly.
			 */
			(void) audioha_codec_verb_get(statep, i, wid,
			    AUDIOHDC_VERB_SET_GPIO_MASK, AUDIOHDC_GPIO_ENABLE);
			(void) audioha_codec_verb_get(statep, i, wid,
			    AUDIOHDC_VERB_SET_UNSOL_ENABLE_MASK,
			    AUDIOHDC_GPIO_ENABLE);
			(void) audioha_codec_verb_get(statep, i, wid,
			    AUDIOHDC_VERB_SET_GPIO_DIREC, AUDIOHDC_GPIO_DIRECT);
			(void) audioha_codec_verb_get(statep, i, wid,
			    AUDIOHDC_VERB_SET_GPIO_STCK,
			    AUDIOHDC_GPIO_DATA_CTRL);
			(void) audioha_codec_verb_get(statep, i, wid,
			    AUDIOHDC_VERB_SET_GPIO_DATA,
			    AUDIOHDC_GPIO_STCK_CTRL);
		}

		/* power-up audio function group */
		(void) audioha_codec_verb_get(statep, i, wid,
		    AUDIOHDC_VERB_SET_POWER_STATE, AUDIOHD_PW_D0);

		/* subsystem id is attached to funtion group */
		codec->outamp_cap = audioha_codec_verb_get(statep, i, wid,
		    AUDIOHDC_VERB_GET_PARAM, AUDIOHDC_PAR_OUTAMP_CAP);
		codec->inamp_cap = audioha_codec_verb_get(statep, i, wid,
		    AUDIOHDC_VERB_GET_PARAM, AUDIOHDC_PAR_INAMP_CAP);
		codec->stream_format = audioha_codec_verb_get(statep, i, wid,
		    AUDIOHDC_VERB_GET_PARAM, AUDIOHDC_PAR_STREAM);
		codec->pcm_format = audioha_codec_verb_get(statep, i, wid,
		    AUDIOHDC_VERB_GET_PARAM, AUDIOHDC_PAR_PCM);

		rate = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "sample-rate", 48000);
		if (rate == 192000 &&
		    (codec->pcm_format & AUDIOHD_SAMP_RATE192)) {
			statep->sample_rate = 192000;
		} else if (rate == 96000 &&
		    (codec->pcm_format & AUDIOHD_SAMP_RATE96)) {
			statep->sample_rate = 96000;
		} else {
			statep->sample_rate = 48000;
		}

		bits = ddi_prop_get_int(DDI_DEV_T_ANY, dip,
		    DDI_PROP_DONTPASS, "sample-bits", 16);
		if (bits == 24 &&
		    (codec->pcm_format & AUDIOHD_BIT_DEPTH24)) {
			statep->sample_bit_depth = AUDIOHD_BIT_DEPTH24;
		} else {
			statep->sample_bit_depth = AUDIOHD_BIT_DEPTH16;
		}

		nums = audioha_codec_verb_get(statep, i, wid,
		    AUDIOHDC_VERB_GET_PARAM, AUDIOHDC_PAR_NODE_COUNT);
		wid = (nums >> AUDIOHD_CODEC_STR_OFF) & AUDIOHD_CODEC_STR_MASK;
		nums = nums & AUDIOHD_CODEC_NUM_MASK;
		codec->first_wid = wid;
		codec->last_wid = wid + nums;
		codec->nnodes = nums;

		/*
		 * We output the codec information to syslog
		 */
		statep->codec[i] = codec;
		codec->statep = statep;
		(void) audiohd_create_widgets(codec);
	}

	return (DDI_SUCCESS);

}	/* audiohd_create_codec() */

/*
 * audiohd_destroy_codec()
 *
 * Description:
 *	destroy codec structure, and release its memory
 */
static void
audiohd_destroy_codec(audiohd_state_t *statep)
{
	int			i;
	audiohd_pin_t		*pin, *npin;

	for (i = 0; i < AUDIOHD_CODEC_MAX; i++) {
		if (statep->codec[i]) {
			audiohd_destroy_widgets(statep->codec[i]);
			/*
			 * free pins
			 */
			pin = statep->codec[i]->first_pin;
			while (pin) {
				npin = pin;
				pin = pin->next;
				kmem_free(npin, sizeof (audiohd_pin_t));
			}

			kmem_free(statep->codec[i], sizeof (hda_codec_t));
			statep->codec[i] = NULL;
		}
	}
}	/* audiohd_destroy_codec() */

/*
 * audiohd_find_dac()
 * Description:
 *	Find a dac for a output path. Then the play data can be sent to the out
 *	put pin through the output path.
 *
 * Arguments:
 *	hda_codec_t	*codec		where the dac widget exists
 *	wid_t		wid		the no. of a widget
 *	int		mixer		whether the path need mixer or not
 *	int		*mixernum	the total of mixer in the output path
 *	int		exclusive	an exclusive path or share path
 *	int		depth		the depth of search
 *
 * Return:
 *	1) wid of the first shared widget in the path from
 *	   pin to DAC if exclusive is 0;
 *	2) wid of DAC widget;
 *	3) 0 if no path
 */
static wid_t
audiohd_find_dac(hda_codec_t *codec, wid_t wid,
    int mixer, int *mixernum,
    int exclusive, int depth)
{
	audiohd_widget_t	*widget = codec->widget[wid];
	wid_t			wdac = (uint32_t)(DDI_FAILURE);
	wid_t			retval;

	if (depth > AUDIOHD_MAX_DEPTH)
		return (uint32_t)(DDI_FAILURE);

	if (widget == NULL)
		return (uint32_t)(DDI_FAILURE);

	/*
	 * If exclusive is true, we try to find a path which doesn't
	 * share any widget with other paths.
	 */
	if (exclusive) {
		if (widget->path_flags & AUDIOHD_PATH_DAC)
			return (uint32_t)(DDI_FAILURE);
	} else {
		if (widget->path_flags & AUDIOHD_PATH_DAC)
			return (wid);
	}

	switch (widget->type) {
	case WTYPE_AUDIO_OUT:
		/* We need mixer widget, but the the mixer num is 0, failed  */
		if (mixer && !*mixernum)
			return (uint32_t)(DDI_FAILURE);
		widget->path_flags |= AUDIOHD_PATH_DAC;
		widget->out_weight++;
		wdac = widget->wid_wid;
		break;
	case WTYPE_AUDIO_MIX:
		(*mixernum)++;
		/* FALLTHRU */
	case WTYPE_AUDIO_SEL:
		for (int i = 0; i < widget->nconns; i++) {
			retval = audiohd_find_dac(codec,
			    widget->avail_conn[i],
			    mixer, mixernum,
			    exclusive, depth + 1);
			if (retval != (uint32_t)(DDI_FAILURE)) {
				if (widget->output_path_next ==
				    AUDIOHD_NULL_CONN) {
					widget->output_path_next = i;
					wdac = retval;
				}
				widget->path_flags |= AUDIOHD_PATH_DAC;
				widget->out_weight++;

				/* return when found a path */
				return (wdac);
			}
		}
	default:
		break;
	}

	return (wdac);
}	/* audiohd_find_dac() */

/*
 * audiohd_do_build_output_path()
 *
 * Description:
 *	Search an output path for each pin in the codec.
 * Arguments:
 *	hda_codec_t	*codec		where the output path exists
 *	int		mixer		whether the path needs mixer widget
 *	int		*mnum		total of mixer widget in the path
 *	int		exclusive	an exclusive path or shared path
 *	int		depth		search depth
 */
static void
audiohd_do_build_output_path(hda_codec_t *codec, int mixer, int *mnum,
    int exclusive, int depth)
{
	audiohd_pin_t		*pin;
	audiohd_widget_t	*widget, *wdac;
	audiohd_path_t		*path;
	wid_t			wid;
	audiohd_state_t		*statep = codec->statep;
	int			i;

	for (pin = codec->first_pin; pin; pin = pin->next) {
		if ((pin->cap & AUDIOHD_PIN_CAP_MASK) == 0)
			continue;
		if ((pin->config & AUDIOHD_PIN_CONF_MASK) ==
		    AUDIOHD_PIN_NO_CONN)
			continue;
		if ((pin->device != DTYPE_LINEOUT) &&
		    (pin->device != DTYPE_SPEAKER) &&
		    (pin->device != DTYPE_SPDIF_OUT) &&
		    (pin->device != DTYPE_HP_OUT))
			continue;
		if (pin->finish)
			continue;
		widget = codec->widget[pin->wid];

		widget->inamp_cap = 0;
		for (i = 0; i < widget->nconns; i++) {
			/*
			 * If a dac found, the return value is the wid of the
			 * widget on the path, or the return value is
			 * DDI_FAILURE
			 */
			wid = audiohd_find_dac(codec,
			    widget->avail_conn[i], mixer, mnum, exclusive,
			    depth);
			/*
			 * A dac was not found
			 */
			if (wid == (wid_t)DDI_FAILURE)
				continue;
			if (pin->device != DTYPE_SPEAKER &&
			    pin->device != DTYPE_HP_OUT)
				statep->chann[pin->assoc] += 2;
			path = (audiohd_path_t *)
			    kmem_zalloc(sizeof (audiohd_path_t),
			    KM_SLEEP);
			path->adda_wid = wid;
			path->pin_wid[0] = widget->wid_wid;
			path->pin_nums = 1;
			path->path_type = PLAY;
			path->codec = codec;
			path->statep = statep;
			wdac = codec->widget[wid];
			wdac->priv = path;
			pin->dac_wid = wid;
			pin->finish = 1;
			widget->path_flags |= AUDIOHD_PATH_DAC;
			widget->out_weight++;
			widget->output_path_next = i;
			statep->path[statep->pathnum++] = path;
			break;
		}
	}

}	/* audiohd_do_build_output_path() */

/*
 * audiohd_build_output_path()
 *
 * Description:
 *	Build the output path in the codec for every pin.
 *	First we try to search output path with mixer widget exclusively
 *	Then we try to search shared output path with mixer widget.
 *	Then we try to search output path without mixer widget exclusively.
 *	At last we try to search shared ouput path for the remained pins
 */
static void
audiohd_build_output_path(hda_codec_t *codec)
{
	int 			mnum = 0;
	uint8_t			mixer_allow = 1;

	/*
	 * Work around for laptops which have IDT or AD audio chipset, such as
	 * HP mini 1000 laptop, Dell Lattitude 6400, Lenovo T60, Lenove R61e.
	 * We don't allow mixer widget on such path, which leads to speaker
	 * loud hiss noise.
	 */
	if (codec->codec_info->flags & NO_MIXER)
		mixer_allow = 0;

	/* search an exclusive mixer widget path. This is preferred */
	audiohd_do_build_output_path(codec, mixer_allow, &mnum, 1, 0);

	/* search a shared mixer widget path for the remained pins */
	audiohd_do_build_output_path(codec, mixer_allow, &mnum, 0, 0);

	/* search an exclusive widget path without mixer for the remained pin */
	audiohd_do_build_output_path(codec, 0, &mnum, 1, 0);

	/* search a shared widget path without mixer for the remained pin */
	audiohd_do_build_output_path(codec, 0, &mnum, 0, 0);

}	/* audiohd_build_output_path */

/*
 * audiohd_build_output_amp
 *
 * Description:
 *	Find the gain control and mute control widget
 */
static void
audiohd_build_output_amp(hda_codec_t *codec)
{
	audiohd_path_t		*path;
	audiohd_widget_t	*widget, *wpin;
	audiohd_pin_t		*pin;
	wid_t			wid;
	int			weight;
	int			i;
	uint32_t		gain;

	for (i = 0; i < codec->statep->pathnum; i++) {
		path = codec->statep->path[i];
		if (path == NULL || path->path_type != PLAY ||
		    path->codec != codec)
			continue;

		wid = path->pin_wid[0];
		wpin = codec->widget[wid];
		pin = (audiohd_pin_t *)wpin->priv;
		weight = wpin->out_weight;

		/*
		 * search a node which can mute this pin while
		 * the mute functionality doesn't effect other pins.
		 */
		widget = wpin;
		while (widget) {
			if (widget->outamp_cap & AUDIOHDC_AMP_CAP_MUTE_CAP) {
				pin->mute_wid = widget->wid_wid;
				pin->mute_dir = AUDIOHDC_AMP_SET_OUTPUT;
				break;
			}
			if (widget->inamp_cap & AUDIOHDC_AMP_CAP_MUTE_CAP) {
				pin->mute_wid = widget->wid_wid;
				pin->mute_dir = AUDIOHDC_AMP_SET_INPUT;
				break;
			}
			if (widget->output_path_next == AUDIOHD_NULL_CONN)
				break;
			wid = widget->avail_conn[widget->output_path_next];
			widget = codec->widget[wid];
			if (widget && widget->out_weight != weight)
				break;
		}

		/*
		 * We select the wid which has maxium gain range in
		 * the output path. Meanwhile, the gain controlling
		 * of this node doesn't effect other pins if this
		 * output stream has multiple pins.
		 */
		gain = 0;
		widget = wpin;
		while (widget) {
			gain = widget->outamp_cap & AUDIOHDC_AMP_CAP_STEP_NUMS;
			if (gain && gain > pin->gain_bits) {
				pin->gain_dir = AUDIOHDC_AMP_SET_OUTPUT;
				pin->gain_bits = gain;
				pin->gain_wid = widget->wid_wid;
			}
			gain = widget->inamp_cap & AUDIOHDC_AMP_CAP_STEP_NUMS;
			if (gain && gain > pin->gain_bits) {
				pin->gain_dir = AUDIOHDC_AMP_SET_INPUT;
				pin->gain_bits = gain;
				pin->gain_wid = widget->wid_wid;
			}
			if (widget->output_path_next == AUDIOHD_NULL_CONN)
				break;
			wid = widget->avail_conn[widget->output_path_next];
			widget = codec->widget[wid];
			if (widget && widget->out_weight != weight)
				break;
		}

		pin->gain_bits >>= AUDIOHD_GAIN_OFF;
		path->mute_wid = pin->mute_wid;
		path->mute_dir = pin->mute_dir;
		path->gain_wid = pin->gain_wid;
		path->gain_dir = pin->gain_dir;
		path->gain_bits = pin->gain_bits;
	}

}	/* audiohd_build_output_amp */

/*
 * audiohd_finish_output_path()
 *
 * Description:
 *	Enable the widgets on the output path
 */
static void
audiohd_finish_output_path(hda_codec_t *codec)
{
	audiohd_state_t		*statep = codec->statep;
	audiohd_path_t		*path;
	audiohd_widget_t	*widget;
	audiohd_pin_t		*pin;
	uint_t			caddr = codec->index;
	wid_t			wid;
	int			i;
	uint32_t		lTmp;

	for (i = 0; i < codec->statep->pathnum; i++) {
		path = codec->statep->path[i];
		if (!path || path->path_type != PLAY || path->codec != codec)
			continue;

		wid = path->pin_wid[0];
		widget = codec->widget[wid];
		pin = (audiohd_pin_t *)widget->priv;

		lTmp = audioha_codec_verb_get(statep, caddr, wid,
		    AUDIOHDC_VERB_GET_PIN_CTRL, 0);
		(void) audioha_codec_verb_get(statep, caddr, wid,
		    AUDIOHDC_VERB_SET_PIN_CTRL, (lTmp | pin->vrefvalue |
		    AUDIOHDC_PIN_CONTROL_OUT_ENABLE |
		    AUDIOHDC_PIN_CONTROL_HP_ENABLE) &
		    ~ AUDIOHDC_PIN_CONTROL_IN_ENABLE);

		/* If this pin has external amplifier, enable it */
		if (pin->cap & AUDIOHD_EXT_AMP_MASK) {
			(void) audioha_codec_verb_get(statep, caddr, wid,
			    AUDIOHDC_VERB_SET_EAPD, AUDIOHD_EXT_AMP_ENABLE);
		}

		(void) audioha_codec_verb_get(statep, caddr, wid,
		    AUDIOHDC_VERB_SET_CONN_SEL, widget->output_path_next);

		/*
		 * Set all amplifiers in this path to
		 * the maximum volume and unmute them.
		 */
		while (widget) {
			if (widget->outamp_cap) {
				(void) audioha_codec_4bit_verb_get(
				    statep, caddr, wid,
				    AUDIOHDC_VERB_SET_AMP_MUTE,
				    AUDIOHDC_AMP_SET_LR_OUTPUT |
				    AUDIOHDC_GAIN_MAX);
			}

			if (widget->inamp_cap) {
				(void) audioha_codec_4bit_verb_get(
				    statep, caddr,
				    wid, AUDIOHDC_VERB_SET_AMP_MUTE,
				    AUDIOHDC_AMP_SET_LR_INPUT |
				    AUDIOHDC_GAIN_MAX |
				    (widget->output_path_next <<
				    AUDIOHDC_AMP_SET_INDEX_OFFSET));
			}

			/*
			 * Accoding to HD spec, mixer doesn't support
			 * "select connection"
			 */
			if ((widget->type == WTYPE_AUDIO_SEL) &&
			    (widget->nconns > 1)) {
				(void) audioha_codec_verb_get(statep,
				    caddr, wid, AUDIOHDC_VERB_SET_CONN_SEL,
				    widget->output_path_next);
			}

			if (widget->output_path_next == AUDIOHD_NULL_CONN)
				break;
			wid = widget->avail_conn[widget->output_path_next];
			widget = codec->widget[wid];
		}
	}
}	/* audiohd_finish_output_path() */

/*
 * audiohd_find_input_pins()
 *
 * Description:
 * 	Here we consider a mixer/selector with multi-input as a real sum
 * 	widget. Only the first real mixer/selector widget is permitted in
 * 	an input path(recording path). If there are more mixers/selectors
 * 	execept the first one, only the first input/connection of those
 * 	widgets will be used by our driver, that means, we ignore other
 * 	inputs of those mixers/selectors.
 */
static int
audiohd_find_input_pins(hda_codec_t *codec, wid_t wid, int allowmixer,
    int depth, audiohd_path_t *path)
{
	audiohd_widget_t	*widget = codec->widget[wid];
	audiohd_pin_t		*pin;
	audiohd_state_t		*statep = codec->statep;
	uint_t			caddr = codec->index;
	int			retval = -1;
	int			num, i;
	uint32_t		pinctrl;

	if (depth > AUDIOHD_MAX_DEPTH)
		return (uint32_t)(DDI_FAILURE);
	if (widget == NULL)
		return (uint32_t)(DDI_FAILURE);

	/* we don't share widgets */
	if (widget->path_flags & AUDIOHD_PATH_ADC ||
	    widget->path_flags & AUDIOHD_PATH_DAC)
		return (uint32_t)(DDI_FAILURE);

	switch (widget->type) {
	case WTYPE_PIN:
		pin = (audiohd_pin_t *)widget->priv;
		if (pin->no_phys_conn)
			return (uint32_t)(DDI_FAILURE);
		/* enable the pins' input capability */
		pinctrl = audioha_codec_verb_get(statep, caddr, wid,
		    AUDIOHDC_VERB_GET_PIN_CTRL, 0);
		(void) audioha_codec_verb_get(statep, caddr, wid,
		    AUDIOHDC_VERB_SET_PIN_CTRL,
		    pinctrl | AUDIOHD_PIN_IN_ENABLE);
		if (pin->cap & AUDIOHD_EXT_AMP_MASK) {
			(void) audioha_codec_verb_get(statep, caddr,
			    wid, AUDIOHDC_VERB_SET_EAPD,
			    AUDIOHD_EXT_AMP_ENABLE);
		}
		switch (pin->device) {
		case DTYPE_CD:
		case DTYPE_LINE_IN:
		case DTYPE_MIC_IN:
		case DTYPE_AUX:
			widget->path_flags |= AUDIOHD_PATH_ADC;
			widget->in_weight++;
			path->pin_wid[path->pin_nums++] = wid;
			pin->adc_wid = path->adda_wid;
			return (DDI_SUCCESS);
		}
		break;
	case WTYPE_AUDIO_MIX:
	case WTYPE_AUDIO_SEL:
		/*
		 * If the sum widget has only one input, we don't
		 * consider it as a real sum widget.
		 */
		if (widget->nconns == 1) {
			widget->input_path_next = 0;
			retval = audiohd_find_input_pins(codec,
			    widget->avail_conn[0],
			    allowmixer, depth + 1, path);
			if (retval == DDI_SUCCESS) {
				widget->path_flags |= AUDIOHD_PATH_ADC;
				widget->in_weight++;
			}
			break;
		}

		if (allowmixer) {
			/*
			 * This is a real sum widget, we will reject
			 * other real sum widget when we find more in
			 * the following path-searching.
			 */
			for (int i = 0; i < widget->nconns; i++) {
				retval = audiohd_find_input_pins(codec,
				    widget->avail_conn[i], 0, depth + 1,
				    path);
				if (retval == DDI_SUCCESS) {
					widget->input_path_next = i;
					widget->in_weight++;
					num = path->pin_nums - 1;
					path->sum_selconn[num] = i;
					path->sum_wid = wid;
					widget->path_flags |=
					    AUDIOHD_PATH_ADC;
				}
			}

			/* return SUCCESS if we found at least one input path */
			if (path->pin_nums > 0)
				retval = DDI_SUCCESS;
		} else {
			/*
			 * We had already found a real sum before this one since
			 * allowmixer is 0.
			 */
			for (i = 0; i < widget->nconns; i++) {
				retval = audiohd_find_input_pins(codec,
				    widget->avail_conn[i], 0, depth + 1,
				    path);
				if (retval == DDI_SUCCESS) {
					widget->input_path_next = i;
					widget->path_flags |= AUDIOHD_PATH_ADC;
					widget->in_weight++;
					break;
				}
			}
		}
		break;
	default:
		break;
	}

	return (retval);
}	/* audiohd_find_input_pins */

/*
 * audiohd_build_input_path()
 *
 * Description:
 *	Find input path for the codec
 */
static void
audiohd_build_input_path(hda_codec_t *codec)
{
	audiohd_widget_t	*widget;
	audiohd_path_t		*path = NULL;
	wid_t			wid;
	int			i;
	int			retval;
	uint8_t			rtag = 0;
	audiohd_state_t		*statep = codec->statep;

	for (wid = codec->first_wid; wid <= codec->last_wid; wid++) {

		widget = codec->widget[wid];

		/* check if it is an ADC widget */
		if (widget == NULL || widget->type != WTYPE_AUDIO_IN)
			continue;

		if (path == NULL)
			path = kmem_zalloc(sizeof (audiohd_path_t),
			    KM_SLEEP);
		else
			bzero(path, sizeof (audiohd_port_t));

		path->adda_wid = wid;

		/*
		 * Is there any ADC widget which has more than one input ??
		 * I don't believe. Anyway, we carefully deal with this. But
		 * if hardware vendors embed a selector in a ADC, we just use
		 * the first available input, which has connection to input pin
		 * widget. Because selector cannot perform mixer functionality,
		 * and we just permit one selector or mixer in a recording path,
		 * if we use the selector embedded in ADC,we cannot use possible
		 * mixer during path searching.
		 */
		for (i = 0; i < widget->nconns; i++) {
			retval = audiohd_find_input_pins(codec,
			    widget->avail_conn[i], 1, 0, path);
			if (retval == DDI_SUCCESS) {
				path->codec = codec;
				path->statep = statep;
				path->path_type = RECORD;
				path->tag = ++rtag;
				codec->nistream++;
				statep->path[statep->pathnum++] = path;
				widget->input_path_next = i;
				widget->priv = path;
				path = NULL;
				break;
			}
		}
	}
	if (path)
		kmem_free(path, sizeof (audiohd_path_t));
}	/* audiohd_build_input_path */

/*
 * audiohd_build_input_amp()
 *
 * Description:
 *	Find gain and mute control widgets on the input path
 */
static void
audiohd_build_input_amp(hda_codec_t *codec)
{
	audiohd_path_t		*path;
	audiohd_widget_t	*wsum, *wadc, *w;
	audiohd_pin_t		*pin;
	uint_t			gain;
	wid_t			wid, next;
	int			i, j;
	int			weight;

	for (i = 0; i < codec->statep->pathnum; i++) {
		path = codec->statep->path[i];
		if (path == NULL || path->path_type != RECORD ||
		    path->codec != codec)
			continue;

		wid = path->adda_wid;
		wadc = path->codec->widget[wid];
		weight = wadc->in_weight;

		/*
		 * Search node which has mute functionality for
		 * the whole input path
		 */
		w = wadc;
		while (w) {
			if (w->outamp_cap & AUDIOHDC_AMP_CAP_MUTE_CAP) {
				path->mute_wid = w->wid_wid;
				path->mute_dir = AUDIOHDC_AMP_SET_OUTPUT;
				break;
			}
			if ((w->inamp_cap & AUDIOHDC_AMP_CAP_MUTE_CAP) &&
			    (w->wid_wid != path->sum_wid)) {
				path->mute_wid = w->wid_wid;
				path->mute_dir = AUDIOHDC_AMP_SET_INPUT;
				break;
			}

			next = w->input_path_next;
			if (next == AUDIOHD_NULL_CONN)
				break;
			wid = w->avail_conn[next];
			w = path->codec->widget[wid];
			if (w && w->in_weight != weight)
				break;
		}

		/*
		 * Search a node for amplifier adjusting for the whole
		 * input path
		 */
		w = wadc;
		gain = 0;
		while (w) {
			gain = (w->outamp_cap & AUDIOHDC_AMP_CAP_STEP_NUMS);
			if (gain && gain > path->gain_bits) {
				path->gain_dir = AUDIOHDC_AMP_SET_OUTPUT;
				path->gain_bits = gain;
				path->gain_wid = w->wid_wid;
			}
			gain = w->inamp_cap & AUDIOHDC_AMP_CAP_STEP_NUMS;
			if (gain && (gain > path->gain_bits) &&
			    (w->wid_wid != path->sum_wid)) {
				path->gain_dir = AUDIOHDC_AMP_SET_INPUT;
				path->gain_bits = gain;
				path->gain_wid = w->wid_wid;
			}

			next = w->input_path_next;
			if (next == AUDIOHD_NULL_CONN)
				break;
			wid = w->avail_conn[next];
			w = path->codec->widget[wid];
		}
		path->gain_bits >>= AUDIOHD_GAIN_OFF;

		/*
		 * If the input path has one pin only, the mute/amp
		 * controlling is shared by the whole path and pin
		 */
		if (path->pin_nums == 1) {
			wid = path->pin_wid[0];
			w = path->codec->widget[wid];
			pin = (audiohd_pin_t *)w->priv;
			pin->gain_dir = path->gain_dir;
			pin->gain_bits = path->gain_bits;
			pin->gain_wid = path->gain_wid;
			pin->mute_wid = path->mute_wid;
			pin->mute_dir = path->mute_dir;
			continue;
		}

		/*
		 * For multi-pin device, there must be a selector
		 * or mixer along the input path, and the sum_wid
		 * is the widget's node id.
		 */
		wid = path->sum_wid;
		wsum = path->codec->widget[wid]; /* sum widget */

		for (j = 0; j < path->pin_nums; j++) {
			wid = path->pin_wid[j];
			w = path->codec->widget[wid];
			pin = (audiohd_pin_t *)w->priv;

			/* find node for mute */
			if (wsum->inamp_cap & AUDIOHDC_AMP_CAP_MUTE_CAP) {
				pin->mute_wid = wsum->wid_wid;
				pin->mute_dir = AUDIOHDC_AMP_SET_INPUT;
			} else {
				wid = wsum->avail_conn[path->sum_selconn[i]];
				w = path->codec->widget[wid];
				while (w) {
					if (w->outamp_cap &
					    AUDIOHDC_AMP_CAP_MUTE_CAP) {
						pin->mute_wid = w->wid_wid;
						pin->mute_dir =
						    AUDIOHDC_AMP_SET_OUTPUT;
						break;
					}
					if (w->inamp_cap &
					    AUDIOHDC_AMP_CAP_MUTE_CAP) {
						pin->mute_wid = w->wid_wid;
						pin->mute_dir =
						    AUDIOHDC_AMP_SET_INPUT;
						break;
					}

					next = w->input_path_next;
					if (next == AUDIOHD_NULL_CONN)
						break;
					wid = w->avail_conn[next];
					w = path->codec->widget[wid];
				}
			}

			/* find node for amp controlling */
			gain = (wsum->inamp_cap & AUDIOHDC_AMP_CAP_STEP_NUMS);
			wid = wsum->avail_conn[path->sum_selconn[i]];
			w = path->codec->widget[wid];
			while (w) {
				gain = (w->outamp_cap &
				    AUDIOHDC_AMP_CAP_STEP_NUMS);
				if (gain && gain > pin->gain_bits) {
					pin->gain_dir = AUDIOHDC_AMP_SET_OUTPUT;
					pin->gain_bits = gain;
					pin->gain_wid = w->wid_wid;
				}
				gain = w->inamp_cap &
				    AUDIOHDC_AMP_CAP_STEP_NUMS;
				if (gain && (gain > pin->gain_bits)) {
					pin->gain_dir = AUDIOHDC_AMP_SET_INPUT;
					pin->gain_bits = gain;
					pin->gain_wid = w->wid_wid;
				}

				next = w->input_path_next;
				if (next == AUDIOHD_NULL_CONN)
					break;
				wid = w->avail_conn[next];
				w = path->codec->widget[wid];
			}
			pin->gain_bits >>= AUDIOHD_GAIN_OFF;
		}
	}
}	/* audiohd_build_input_amp() */

/*
 * audiohd_finish_input_path()
 *
 * Description:
 *	Enable the widgets on the input path
 */
static void
audiohd_finish_input_path(hda_codec_t *codec)
{
	audiohd_state_t		*statep = codec->statep;
	audiohd_path_t		*path;
	audiohd_widget_t	*w, *wsum;
	uint_t			caddr = codec->index;
	wid_t			wid;
	int			i, j;

	for (i = 0; i < codec->statep->pathnum; i++) {
		path = codec->statep->path[i];
		if (path == NULL || path->path_type != RECORD ||
		    path->codec != codec)
			continue;
		wid = path->adda_wid;
		w = path->codec->widget[wid];
		while (w && (w->wid_wid != path->sum_wid) &&
		    (w->type != WTYPE_PIN)) {
			if ((w->type == WTYPE_AUDIO_SEL) && (w->nconns > 1))
				(void) audioha_codec_verb_get(statep, caddr,
				    w->wid_wid, AUDIOHDC_VERB_SET_CONN_SEL,
				    w->input_path_next);

			if (w->outamp_cap) {
				(void) audioha_codec_4bit_verb_get(statep,
				    caddr,
				    w->wid_wid, AUDIOHDC_VERB_SET_AMP_MUTE,
				    AUDIOHDC_AMP_SET_LR_OUTPUT |
				    AUDIOHDC_GAIN_MAX);
			}

			if (w->inamp_cap) {
				(void) audioha_codec_4bit_verb_get(statep,
				    caddr,
				    w->wid_wid, AUDIOHDC_VERB_SET_AMP_MUTE,
				    AUDIOHDC_AMP_SET_LR_INPUT |
				    AUDIOHDC_GAIN_MAX |
				    (w->input_path_next <<
				    AUDIOHDC_AMP_SET_INDEX_OFFSET));
			}

			wid = w->avail_conn[w->input_path_next];
			w = path->codec->widget[wid];
		}

		/*
		 * After exiting from the above loop, the widget pointed
		 * by w can be a pin widget or select/mixer widget. If it
		 * is a pin widget, we already finish "select connection"
		 * operation for the whole path.
		 */
		if (w && w->type == WTYPE_PIN)
			continue;

		/*
		 * deal with multi-pin input devices.
		 */
		wid = path->sum_wid;
		wsum = path->codec->widget[wid];
		if (wsum == NULL)
			continue;
		if (wsum->outamp_cap) {
			(void) audioha_codec_4bit_verb_get(statep,
			    caddr,
			    wsum->wid_wid, AUDIOHDC_VERB_SET_AMP_MUTE,
			    AUDIOHDC_AMP_SET_LR_OUTPUT |
			    AUDIOHDC_GAIN_MAX);
		}

		for (j = 0; j < path->pin_nums; j++) {
			if (wsum->inamp_cap) {
				(void) audioha_codec_4bit_verb_get(statep,
				    caddr,
				    wsum->wid_wid, AUDIOHDC_VERB_SET_AMP_MUTE,
				    AUDIOHDC_AMP_SET_LR_INPUT |
				    AUDIOHDC_GAIN_MAX |
				    (path->sum_selconn[j] <<
				    AUDIOHDC_AMP_SET_INDEX_OFFSET));
			}
			if (wsum->type == WTYPE_AUDIO_SEL) {
				(void) audioha_codec_verb_get(statep, caddr,
				    wsum->wid_wid,
				    AUDIOHDC_VERB_SET_CONN_SEL,
				    path->sum_selconn[j]);
			}

			wid = wsum->avail_conn[path->sum_selconn[j]];
			w = path->codec->widget[wid];
			while (w && w->type != WTYPE_PIN) {
				if ((w->type != WTYPE_AUDIO_MIX) &&
				    (w->nconns > 1))
					(void) audioha_codec_verb_get(statep,
					    caddr, w->wid_wid,
					    AUDIOHDC_VERB_SET_CONN_SEL,
					    w->input_path_next);

				if (w->outamp_cap) {
					(void) audioha_codec_4bit_verb_get(
					    statep,
					    caddr,
					    w->wid_wid,
					    AUDIOHDC_VERB_SET_AMP_MUTE,
					    AUDIOHDC_AMP_SET_LR_OUTPUT |
					    AUDIOHDC_GAIN_MAX);
				}

				if (w->inamp_cap) {
					(void) audioha_codec_4bit_verb_get(
					    statep,
					    caddr,
					    w->wid_wid,
					    AUDIOHDC_VERB_SET_AMP_MUTE,
					    AUDIOHDC_AMP_SET_LR_INPUT |
					    AUDIOHDC_GAIN_MAX |
					    (w->input_path_next <<
					    AUDIOHDC_AMP_SET_INDEX_OFFSET));
				}
				wid = w->avail_conn[w->input_path_next];
				w = path->codec->widget[wid];
			}
		}
	}	/* end of istream loop */
}	/* audiohd_finish_input_path */

/*
 * audiohd_find_inpin_for_monitor()
 *
 * Description:
 *	Find input pin for monitor path.
 *
 * Arguments:
 *	hda_codec_t		*codec		where the monitor path exists
 *	wid_t			id		no. of widget being searched
 *	int			mixer		share or not
 */
static int
audiohd_find_inpin_for_monitor(hda_codec_t *codec, wid_t id, int mixer)
{
	wid_t 			wid;
	audiohd_widget_t	*widget, *w;
	audiohd_pin_t		*pin;
	int 			i, find = 0;

	wid = id;
	widget = codec->widget[wid];
	if (widget == NULL)
		return (uint32_t)(DDI_FAILURE);

	if (widget->type == WTYPE_PIN) {
		pin = (audiohd_pin_t *)widget->priv;
		if (pin->no_phys_conn)
			return (uint32_t)(DDI_FAILURE);
		switch (pin->device) {
			case DTYPE_SPDIF_IN:
			case DTYPE_CD:
			case DTYPE_LINE_IN:
			case DTYPE_MIC_IN:
			case DTYPE_AUX:
				widget->path_flags |= AUDIOHD_PATH_MON;
				return (DDI_SUCCESS);
			default:
				return (uint32_t)(DDI_FAILURE);
		}
	}
	/* the widget has been visited and can't be directed to input pin */
	if (widget->path_flags & AUDIOHD_PATH_NOMON) {
		return (uint32_t)(DDI_FAILURE);
	}
	/* the widget has been used by the monitor path, and we can share it */
	if (widget->path_flags & AUDIOHD_PATH_MON) {
		if (mixer)
			return (DDI_SUCCESS);
		else
			return (uint32_t)(DDI_FAILURE);
	}
	switch (widget->type) {
		case WTYPE_AUDIO_MIX:
			for (i = 0; i < widget->nconns; i++) {
				if (widget->output_path_next == i)
					continue;
				if (audiohd_find_inpin_for_monitor(codec,
				    widget->avail_conn[i], mixer) ==
				    DDI_SUCCESS) {
					w = widget;
					w->monitor_path_next[w->used++] = i;
					w->path_flags |= AUDIOHD_PATH_MON;
					find = 1;
				}
			}
			break;
		case WTYPE_AUDIO_SEL:
			for (i = 0; i < widget->nconns; i++) {
				if (widget->output_path_next == i)
					continue;
				if (audiohd_find_inpin_for_monitor(codec,
				    widget->avail_conn[i], mixer) ==
				    DDI_SUCCESS) {
					widget->monitor_path_next[0] = i;
					widget->path_flags |= AUDIOHD_PATH_MON;
					find = 1;
					break;
				}
			}
			break;
		default:
			break;
	}
	if (!find) {
		widget->path_flags |= AUDIOHD_PATH_NOMON;
		return (uint32_t)(DDI_FAILURE);
	}
	else
		return (DDI_SUCCESS);
}	/* audiohd_find_inpin_for_monitor */

/*
 * audiohd_build_monitor_path()
 *
 * Description:
 * 	The functionality of mixer is to mix inputs, such as CD-IN, MIC,
 * 	Line-in, etc, with DAC outputs, so as to minitor what is being
 * 	recorded and implement "What you hear is what you get". However,
 * 	this functionality are really hardware-dependent: the inputs
 * 	must be directed to MIXER if they can be directed to ADC as
 * 	recording sources.
 */
static void
audiohd_build_monitor_path(hda_codec_t *codec)
{
	audiohd_path_t		*path;
	audiohd_widget_t	*widget, *w;
	audiohd_state_t		*statep = codec->statep;
	wid_t			wid, next;
	int			i, j, k, l, find;
	int			mixernum = 0;

	for (i = 0; i < statep->pathnum; i++) {
		path = statep->path[i];
		if (path == NULL || path->codec != codec ||
		    path->path_type != PLAY)
			continue;
		for (j = 0; j < path->pin_nums; j++) {
			wid = path->pin_wid[j];
			widget = codec->widget[wid];
			l = 0;
			while (widget) {
				while (widget &&
				    ((widget->type != WTYPE_AUDIO_MIX) ||
				    (widget->nconns < 2))) {
					next = widget->output_path_next;
					if (next == AUDIOHD_NULL_CONN)
						break;
					wid = widget->avail_conn[next];
					widget = codec->widget[wid];
				}

				/*
				 * No mixer in this output path, we cannot build
				 * mixer path for this path, skip it,
				 * and continue for next output path.
				 */
				if (widget == NULL ||
				    widget->output_path_next ==
				    AUDIOHD_NULL_CONN) {
					break;
				}
				mixernum++;
				for (k = 0; k < widget->nconns; k++) {

					/*
					 * this connection must be routined
					 * to DAC instead of an input pin
					 * widget, we needn't waste time for
					 * it
					 */
					if (widget->output_path_next == k)
						continue;
					find = 0;
					if (audiohd_find_inpin_for_monitor(
					    codec,
					    widget->avail_conn[k], 0) ==
					    DDI_SUCCESS) {
						path->mon_wid[j][l] = wid;
						w = widget;
						w->monitor_path_next[w->used++]
						    = k;
						w->path_flags |=
						    AUDIOHD_PATH_MON;
						find = 1;
					} else if (
					    audiohd_find_inpin_for_monitor(
					    codec,
					    widget->avail_conn[k], 1) ==
					    DDI_SUCCESS) {
						path->mon_wid[j][l] = wid;
						w = widget;
						w->monitor_path_next[w->used++]
						    = k;
						w->path_flags |=
						    AUDIOHD_PATH_MON;
						find = 1;
					}

				}

				/*
				 * we needn't check widget->output_path_next
				 * here since this widget is a selector or
				 * mixer, it cannot be NULL connection.
				 */
				if (!find) {
					path->mon_wid[j][l] = 0;
					widget->path_flags |=
					    AUDIOHD_PATH_NOMON;
				}
				next = widget->output_path_next;
				wid = widget->avail_conn[next];
				widget = codec->widget[wid];
				l++;
			}
			path->maxmixer[j] = l;
		}

	}
	if (mixernum == 0)
		statep->monitor_supported = B_FALSE;
	else
		statep->monitor_supported = B_TRUE;
}	/* audiohd_build_monitor_path */

/*
 * audiohd_do_finish_monitor_path
 *
 * Description:
 *	Enable the widgets on the monitor path
 */
static void
audiohd_do_finish_monitor_path(hda_codec_t *codec, audiohd_widget_t *wgt)
{
	uint_t			caddr = codec->index;
	audiohd_widget_t 	*widget = wgt;
	audiohd_widget_t	*w;
	audiohd_state_t		*statep = codec->statep;
	wid_t			wid;
	int			i;
	int			share = 0;

	if (!widget || widget->finish)
		return;
	if (widget->path_flags & AUDIOHD_PATH_ADC)
		share = 1;
	if ((widget->outamp_cap) && !share)
		(void) audioha_codec_4bit_verb_get(statep, caddr,
		    widget->wid_wid, AUDIOHDC_VERB_SET_AMP_MUTE,
		    AUDIOHDC_AMP_SET_LR_OUTPUT | AUDIOHDC_GAIN_MAX);
	if ((widget->inamp_cap) && !share) {
		for (i = 0; i < widget->used; i++) {
		(void) audioha_codec_4bit_verb_get(statep, caddr,
		    widget->wid_wid, AUDIOHDC_VERB_SET_AMP_MUTE,
		    AUDIOHDC_AMP_SET_LR_INPUT | AUDIOHDC_GAIN_MAX |
		    (widget->monitor_path_next[i]
		    << AUDIOHDC_AMP_SET_INDEX_OFFSET));
		}
	}
	if ((widget->type == WTYPE_AUDIO_SEL) && (widget->nconns > 1) &&
	    !share) {
		(void) audioha_codec_verb_get(statep, caddr, widget->wid_wid,
		    AUDIOHDC_VERB_SET_CONN_SEL, widget->monitor_path_next[0]);
	}
	widget->finish = 1;
	if (widget->used == 0)
		return;
	if (widget->used > 0) {
		for (i = 0; i < widget->used; i++) {
			wid = widget->avail_conn[widget->monitor_path_next[i]];
			w = codec->widget[wid];
			audiohd_do_finish_monitor_path(codec, w);
		}
	}
}	/* audiohd_do_finish_monitor_path */

/*
 * audiohd_finish_monitor_path
 *
 * Description:
 *	Enable the monitor path for every ostream path
 */
static void
audiohd_finish_monitor_path(hda_codec_t *codec)
{
	audiohd_path_t		*path;
	audiohd_widget_t	*widget;
	audiohd_state_t		*statep = codec->statep;
	wid_t			wid;
	int 			i, j, k;

	for (i = 0; i < statep->pathnum; i++) {
		path = statep->path[i];
		if (!path || path->codec != codec || path->path_type != PLAY)
			continue;
		for (j = 0; j < path->pin_nums; j++) {
			for (k = 0; k < path->maxmixer[j]; k++) {
				wid = path->mon_wid[j][k];
				if (wid == 0) {
					continue;
				}
				widget = codec->widget[wid];
				audiohd_do_finish_monitor_path(codec, widget);
			}
		}
	}
}	/* audiohd_finish_monitor_path */

/*
 * audiohd_do_build_monit_amp()
 *
 * Description:
 *	Search for the gain control widget for the monitor path
 */
static void
audiohd_do_build_monitor_amp(hda_codec_t *codec, audiohd_pin_t *pin,
    audiohd_widget_t *widget)
{
	audiohd_widget_t	*w = widget;
	uint32_t		gain;
	int			i;
	wid_t			wid;

	if (!w ||
	    (w->type == WTYPE_PIN) ||
	    !w->used ||
	    (pin->num == AUDIOHD_MAX_CONN) ||
	    (w->path_flags & AUDIOHD_PATH_ADC))
		return;
	if (!(w->path_flags & AUDIOHD_PATH_DAC)) {
		gain = w->outamp_cap & AUDIOHDC_AMP_CAP_STEP_NUMS;
		if (gain) {
			pin->mg_dir[pin->num] = AUDIOHDC_AMP_SET_OUTPUT;
			pin->mg_gain[pin->num] = gain;
			pin->mg_wid[pin->num] = w->wid_wid;
			pin->mg_gain[pin->num] >>= AUDIOHD_GAIN_OFF;
			pin->num++;
			return;
		}
		gain = w->inamp_cap & AUDIOHDC_AMP_CAP_STEP_NUMS;
		if (gain) {
			pin->mg_dir[pin->num] = AUDIOHDC_AMP_SET_INPUT;
			pin->mg_gain[pin->num] = gain;
			pin->mg_wid[pin->num] = w->wid_wid;
			pin->mg_gain[pin->num] >>= AUDIOHD_GAIN_OFF;
			pin->num++;
			return;
		}
	}
	for (i = 0; i < w->used; i++) {
		wid = w->avail_conn[w->monitor_path_next[i]];
		audiohd_do_build_monitor_amp(codec, pin, codec->widget[wid]);
	}


}	/* audiohd_do_build_monitor_amp() */

/*
 * audiohd_build_monitor_amp()
 *
 * Description:
 *	Search gain control widget for every ostream monitor
 */
static void
audiohd_build_monitor_amp(hda_codec_t *codec)
{
	audiohd_path_t		*path;
	audiohd_widget_t	*widget, *w;
	audiohd_state_t		*statep = codec->statep;
	audiohd_pin_t		*pin;
	wid_t			wid, id;
	int			i, j, k;

	for (i = 0; i < statep->pathnum; i++) {
		path = statep->path[i];
		if (!path || path->codec != codec || path->path_type != PLAY)
			continue;
		for (j = 0; j < path->pin_nums; j++) {
			id = path->pin_wid[j];
			w = codec->widget[id];
			pin = (audiohd_pin_t *)(w->priv);
			for (k = 0; k < path->maxmixer[j]; k++) {
				wid = path->mon_wid[j][k];
				if (!wid)
					continue;
				widget = codec->widget[wid];
				audiohd_do_build_monitor_amp(codec, pin,
				    widget);
			}
		}
	}
}

/*
 * audiohd_find_beep()
 * Description:
 *      Find a beep for a beep path. Then the play data can be sent to the out
 *      put pin through the beep path.
 *
 * Arguments:
 *      hda_codec_t     *codec          where the beep widget exists
 *      wid_t           wid             the no. of a widget
 *      int             depth           the depth of search
 *
 * Return:
 *      1) wid of Beep widget;
 *      2) 0 if no path
 */
static wid_t
audiohd_find_beep(hda_codec_t *codec, wid_t wid, int depth)
{
	audiohd_widget_t	*widget = codec->widget[wid];
	wid_t   		wbeep = (uint32_t)(DDI_FAILURE);
	wid_t   		retval;

	if (depth > AUDIOHD_MAX_DEPTH)
		return (uint32_t)(DDI_FAILURE);

	if (widget == NULL)
		return (uint32_t)(DDI_FAILURE);

	switch (widget->type) {
	case WTYPE_BEEP:
		widget->path_flags |= AUDIOHD_PATH_BEEP;
		wbeep = widget->wid_wid;
		break;
	case WTYPE_AUDIO_MIX:
	case WTYPE_AUDIO_SEL:
		for (int i = 0; i < widget->nconns; i++) {
			retval = audiohd_find_beep(codec,
			    widget->avail_conn[i], depth + 1);
			if (retval == DDI_SUCCESS) {
				if (widget->output_path_next !=
				    AUDIOHD_NULL_CONN)
					continue;
				widget->beep_path_next = i;
				wbeep = retval;
				widget->path_flags |= AUDIOHD_PATH_BEEP;
				return (wbeep);
			}
		}
		break;
	default:
		break;
	}

	return (wbeep);
}       /* audiohd_find_beep() */

/*
 * audiohd_build_beep_path()
 *
 * Description:
 *      Search an beep path for each pin in the codec.
 * Arguments:
 *      hda_codec_t     *codec          where the beep path exists
 */
static void
audiohd_build_beep_path(hda_codec_t *codec)
{
	audiohd_pin_t		*pin;
	audiohd_widget_t	*widget;
	audiohd_path_t		*path;
	wid_t			wid;
	audiohd_state_t		*statep;
	int			i;
	boolean_t		beeppath = B_FALSE;

	statep = codec->statep;

	for (pin = codec->first_pin; pin; pin = pin->next) {
		if ((pin->cap & AUDIOHD_PIN_CAP_MASK) == 0)
			continue;
		if ((pin->config & AUDIOHD_PIN_CONF_MASK) ==
		    AUDIOHD_PIN_NO_CONN)
			continue;
		if ((pin->device != DTYPE_LINEOUT) &&
		    (pin->device != DTYPE_SPEAKER) &&
		    (pin->device != DTYPE_SPDIF_OUT) &&
		    (pin->device != DTYPE_HP_OUT))
			continue;
		widget = codec->widget[pin->wid];

		widget->inamp_cap = 0;
		for (i = 0; i < widget->nconns; i++) {
			/*
			 * If a beep found, the return value is the wid of the
			 * widget on the path, or the return value is
			 * DDI_FAILURE
			 */
			wid = audiohd_find_beep(codec,
			    widget->avail_conn[i], 0);
			/*
			 * A beep was not found
			 */
			if (wid == (wid_t)DDI_FAILURE)
				continue;
			if (widget->output_path_next != AUDIOHD_NULL_CONN)
				continue;
			path = (audiohd_path_t *)
			    kmem_zalloc(sizeof (audiohd_path_t),
			    KM_SLEEP);
			path->beep_wid = wid;
			path->pin_wid[0] = widget->wid_wid;
			path->pin_nums = 1;
			path->path_type = BEEP;
			beeppath = 1;
			path->codec = codec;
			path->statep = statep;
			widget->path_flags |= AUDIOHD_PATH_BEEP;
			widget->beep_path_next = i;
			statep->path[statep->pathnum++] = path;
			break;
		}
	}

	if (!beeppath) {
		for (int i = 0; i < AUDIOHD_CODEC_MAX; i++) {
			codec = statep->codec[i];
			if (codec == NULL)
				continue;
			for (wid = codec->first_wid; wid <= codec->last_wid;
			    wid++) {
				widget = codec->widget[wid];

				if (widget->type == WTYPE_BEEP) {
					path = (audiohd_path_t *)
					    kmem_zalloc(sizeof (audiohd_path_t),
					    KM_SLEEP);
					path->beep_wid = wid;
					path->pin_nums = 0;
					path->path_type = BEEP;
					beeppath = 1;
					path->codec = codec;
					path->statep = statep;
					widget->path_flags |= AUDIOHD_PATH_BEEP;
					statep->path[statep->pathnum++] = path;
					break;
				}
			}
		}
	}
}       /* audiohd_build_beep_path() */

/*
 * audiohd_build_beep_amp
 *
 * Description:
 *      Find the gain control and mute control widget
 */
static void
audiohd_build_beep_amp(hda_codec_t *codec)
{
	audiohd_path_t		*path;
	audiohd_widget_t	*widget, *wpin, *wbeep;
	wid_t			wid, next;
	int			i, j;
	uint32_t		gain;

	for (i = 0; i < codec->statep->pathnum; i++) {
		path = codec->statep->path[i];
		if (path == NULL || path->path_type != BEEP ||
		    path->codec != codec)
			continue;
		if (path->pin_nums == 0) {
			path->mute_wid = path->beep_wid;
			path->mute_dir = AUDIOHDC_AMP_SET_OUTPUT;
			wbeep = codec->widget[path->beep_wid];
			gain = (wbeep->outamp_cap &
			    AUDIOHDC_AMP_CAP_STEP_NUMS);
			if (gain) {
				path->gain_dir = AUDIOHDC_AMP_SET_OUTPUT;
				path->gain_bits = gain;
				path->gain_wid = path->beep_wid;
			}
			path->gain_bits >>= AUDIOHD_GAIN_OFF;
			break;
		}
		for (j = 0; j < path->pin_nums; j++) {
			wid = path->pin_wid[j];
			wpin = codec->widget[wid];
			wbeep = codec->widget[path->beep_wid];

			widget = wpin;
			while (widget) {
				if (widget->out_weight == 0 &&
				    widget->outamp_cap &
				    AUDIOHDC_AMP_CAP_MUTE_CAP) {
					path->mute_wid = widget->wid_wid;
					path->mute_dir =
					    AUDIOHDC_AMP_SET_OUTPUT;
					break;
				}
				next = widget->beep_path_next;
				if (next == AUDIOHD_NULL_CONN)
					break;
				wid = widget->avail_conn[next];
				widget = codec->widget[wid];
			}

			gain = 0;
			widget = wpin;
			while (widget) {
				if (widget->out_weight == 0 &&
				    widget->outamp_cap &
				    AUDIOHDC_AMP_CAP_STEP_NUMS) {
					gain = (widget->outamp_cap &
					    AUDIOHDC_AMP_CAP_STEP_NUMS);
					if (gain && gain > path->gain_bits) {
						path->gain_dir =
						    AUDIOHDC_AMP_SET_OUTPUT;
						path->gain_bits = gain;
						path->gain_wid =
						    widget->wid_wid;
					}
				}
				next = widget->beep_path_next;
				if (next == AUDIOHD_NULL_CONN)
					break;
				wid = widget->avail_conn[next];
				widget = codec->widget[wid];
			}
			path->gain_bits >>= AUDIOHD_GAIN_OFF;
		}
	}
}       /* audiohd_build_beep_amp */

/*
 * audiohd_finish_beep_path()
 *
 * Description:
 *      Enable the widgets on the beep path
 */
static void
audiohd_finish_beep_path(hda_codec_t *codec)
{
	audiohd_state_t		*statep = codec->statep;
	audiohd_path_t		*path;
	audiohd_widget_t	*widget;
	uint_t			caddr = codec->index;
	wid_t			wid, next;
	int			i, j;

	for (i = 0; i < codec->statep->pathnum; i++) {
		path = codec->statep->path[i];
		if (!path || path->path_type != BEEP || path->codec != codec)
			continue;
		if (path->pin_nums == 0) {
			widget = codec->widget[path->beep_wid];
			if (widget->outamp_cap) {
				(void) audioha_codec_4bit_verb_get(
				    statep, caddr,
				    path->beep_wid, AUDIOHDC_VERB_SET_AMP_MUTE,
				    AUDIOHDC_AMP_SET_LR_OUTPUT |
				    AUDIOHDC_GAIN_MAX);
			}
			if (widget->inamp_cap) {
				(void) audioha_codec_4bit_verb_get(
				    statep, caddr,
				    path->beep_wid, AUDIOHDC_VERB_SET_AMP_MUTE,
				    AUDIOHDC_AMP_SET_LR_INPUT |
				    AUDIOHDC_GAIN_MAX |
				    (widget->beep_path_next <<
				    AUDIOHDC_AMP_SET_INDEX_OFFSET));
			}
			continue;
		}

		for (j = 0; j < path->pin_nums; j++) {
			wid = path->pin_wid[j];
			widget = codec->widget[wid];

			(void) audioha_codec_verb_get(statep, caddr, wid,
			    AUDIOHDC_VERB_SET_CONN_SEL, widget->beep_path_next);

			wid = widget->avail_conn[widget->beep_path_next];
			widget = codec->widget[wid];

			while (widget) {
				/*
				 * Set all amplifiers in this path to
				 * the maximum volume and unmute them.
				 */
				if (widget->out_weight != 0)
					continue;
				if (widget->outamp_cap) {
					(void) audioha_codec_4bit_verb_get(
					    statep, caddr,
					    wid, AUDIOHDC_VERB_SET_AMP_MUTE,
					    AUDIOHDC_AMP_SET_LR_OUTPUT |
					    AUDIOHDC_GAIN_MAX);
				}
				if (widget->inamp_cap) {
					(void) audioha_codec_4bit_verb_get(
					    statep, caddr,
					    wid, AUDIOHDC_VERB_SET_AMP_MUTE,
					    AUDIOHDC_AMP_SET_LR_INPUT |
					    AUDIOHDC_GAIN_MAX |
					    (widget->beep_path_next <<
					    AUDIOHDC_AMP_SET_INDEX_OFFSET));
				}

				next = widget->beep_path_next;
				if (next == AUDIOHD_NULL_CONN)
					break;
				/*
				 * Accoding to HD spec, mixer doesn't support
				 * "select connection"
				 */
				if ((widget->type != WTYPE_AUDIO_MIX) &&
				    (widget->nconns > 1))
					(void) audioha_codec_verb_get(statep,
					    caddr, wid,
					    AUDIOHDC_VERB_SET_CONN_SEL,
					    widget->beep_path_next);

				wid = widget->avail_conn[next];
				widget = codec->widget[wid];
			}
		}
	}
}       /* audiohd_finish_beep_path */

static int
audiohd_find_output_pins(hda_codec_t *codec, wid_t wid, int depth,
    audiohd_path_t *path)
{
	audiohd_widget_t	*widget = codec->widget[wid];
	audiohd_pin_t		*pin = NULL;
	int			num, retval = (DDI_FAILURE);

	if (depth > AUDIOHD_MAX_DEPTH)
		return (retval);
	if (widget == NULL)
		return (retval);

	switch (widget->type) {
	case WTYPE_PIN:
		pin = (audiohd_pin_t *)widget->priv;
		if (pin->no_phys_conn)
			return (DDI_FAILURE);

		switch (pin->device) {
		case DTYPE_LINE_IN:
			/* Connection between line-in and output pins */
			path->pin_wid[path->pin_nums++] = wid;
			break;
		case DTYPE_LINEOUT:
		case DTYPE_HP_OUT:
		case DTYPE_SPDIF_OUT:
			widget->path_flags |= AUDIOHD_PATH_LOOPBACK;
			widget->in_weight++;
			pin->adc_wid = path->adda_wid;
			path->pin_wid[path->pin_nums++] = wid;
			retval = (DDI_SUCCESS);
			break;
		default:
			break;
		}
		break;
	case WTYPE_AUDIO_MIX:
	case WTYPE_AUDIO_SEL:
		/*
		 * If the sum widget has only one input, we don't
		 * consider it as a real sum widget.
		 */
		if (widget->nconns == 1) {
			widget->loopback_path_next = 0;
			retval = audiohd_find_output_pins(codec,
			    widget->avail_conn[0], depth + 1, path);
			if (retval == (DDI_SUCCESS)) {
				widget->path_flags |= AUDIOHD_PATH_LOOPBACK;
				widget->in_weight++;
			}
			break;
		}

		for (int i = 0; i < widget->nconns; i++) {
			retval = audiohd_find_output_pins(codec,
			    widget->avail_conn[i], depth + 1, path);
			if (retval == (DDI_SUCCESS)) {
				widget->loopback_path_next = i;
				widget->in_weight++;
				num = path->pin_nums - 1;
				path->sum_selconn[num] = i;
				path->sum_wid = wid;
				widget->path_flags |= AUDIOHD_PATH_LOOPBACK;
				break;
			}
		}
		break;
	default:
		break;
	}

	return (retval);
}

static void
audiohd_build_loopback_path(hda_codec_t *codec)
{
	audiohd_state_t		*statep = codec->statep;
	audiohd_widget_t	*widget;
	audiohd_path_t		*path = NULL;
	wid_t			wid;
	int			i, retval;
	uint8_t			rtag = 0;

	for (wid = codec->first_wid; wid <= codec->last_wid; wid++) {
		widget = codec->widget[wid];

		/* check if it is an ADC widget */
		if (widget == NULL || widget->type != WTYPE_AUDIO_IN)
			continue;

		if (path == NULL)
			path = kmem_zalloc(sizeof (audiohd_path_t), KM_SLEEP);
		else
			bzero(path, sizeof (audiohd_port_t));
		path->adda_wid = wid;

		for (i = 0; i < widget->nconns; i++) {
			retval = audiohd_find_output_pins(codec,
			    widget->avail_conn[i], 0, path);
			if (retval == (DDI_SUCCESS)) {
				path->codec = codec;
				path->statep = statep;
				path->path_type = LOOPBACK;
				path->tag = ++rtag;
				codec->nistream++;
				statep->path[statep->pathnum++] = path;
				widget->loopback_path_next = i;
				widget->priv = path;
				path = NULL;
				statep->loopback_supported = B_TRUE;
				break;
			}
		}
	}


	if (path)
		kmem_free(path, sizeof (audiohd_path_t));
}	/* audiohd_build_loopback_path() */

/*
 * audiohd_build_path()
 *
 * Description:
 *	Here we build the output, input, monitor path.
 *	And also enable the path in default.
 *	Search for the gain and mute control for the path
 */
static void
audiohd_build_path(audiohd_state_t *statep)
{
	int		i;

	for (i = 0; i < AUDIOHD_CODEC_MAX; i++) {
		if (statep->codec[i]) {
			audiohd_build_output_path(statep->codec[i]);
			audiohd_build_output_amp(statep->codec[i]);
			audiohd_finish_output_path(statep->codec[i]);

			audiohd_build_input_path(statep->codec[i]);
			audiohd_build_input_amp(statep->codec[i]);
			audiohd_finish_input_path(statep->codec[i]);

			audiohd_build_monitor_path(statep->codec[i]);
			audiohd_build_monitor_amp(statep->codec[i]);
			audiohd_finish_monitor_path(statep->codec[i]);

			audiohd_build_beep_path(statep->codec[i]);
			audiohd_build_beep_amp(statep->codec[i]);
			audiohd_finish_beep_path(statep->codec[i]);

			audiohd_build_loopback_path(statep->codec[i]);
		}
	}
}	/* audiohd_build_path */

/*
 * audiohd_allocate_port()
 */
static int
audiohd_allocate_port(audiohd_state_t *statep)
{
	int			i, j;
	audiohd_port_t		*port;
	int			dir;
	unsigned		caps;
	int			rc;
	audio_dev_t		*adev;
	dev_info_t		*dip;
	ddi_dma_cookie_t	cookie;
	uint_t			count;
	uint64_t		buf_phys_addr;
	sd_bdle_t		*entry;
	uint16_t		gcap;
	size_t			real_size;

	adev = statep->adev;
	dip = statep->hda_dip;

	ddi_dma_attr_t	dma_attr = {
		DMA_ATTR_V0,		/* version */
		0,			/* addr_lo */
		0xffffffffffffffffULL,	/* addr_hi */
		0x00000000ffffffffULL,	/* count_max */
		128,			/* 128-byte alignment as HD spec */
		0xfff,			/* burstsize */
		1,			/* minxfer */
		0xffffffff,		/* maxxfer */
		0xffffffff,		/* seg */
		1,			/* sgllen */
		1,			/* granular */
		0			/* flags */
	};

	gcap = AUDIOHD_REG_GET16(AUDIOHD_REG_GCAP);
	if ((gcap & AUDIOHDR_GCAP_64OK) == 0)
		dma_attr.dma_attr_addr_hi = 0xffffffffUL;

	for (i = 0; i < PORT_MAX; i++) {
		port = kmem_zalloc(sizeof (*port), KM_SLEEP);
		statep->port[i] = port;
		port->statep = statep;
		switch (i) {
		case PORT_ADC:
			dir = DDI_DMA_READ | DDI_DMA_CONSISTENT;
			caps = ENGINE_INPUT_CAP;
			port->sync_dir = DDI_DMA_SYNC_FORKERNEL;
			port->nchan = statep->rchan;
			port->index = 1;
			port->regoff = AUDIOHD_REG_SD_BASE;
			break;
		case PORT_DAC:
			dir = DDI_DMA_WRITE | DDI_DMA_CONSISTENT;
			caps = ENGINE_OUTPUT_CAP;
			port->sync_dir = DDI_DMA_SYNC_FORDEV;
			port->nchan = statep->pchan;
			port->index = statep->hda_input_streams + 1;
			port->regoff = AUDIOHD_REG_SD_BASE +
			    AUDIOHD_REG_SD_LEN *
			    statep->hda_input_streams;
			break;
		default:
			return (DDI_FAILURE);
		}

		switch (statep->sample_rate) {
		case 192000:
			port->format = 0x18 << 4;
			break;
		case 96000:
			port->format = 0x08 << 4;
			break;
		case 48000:
		default: /* 48kHz is default */
			port->format = 0x00;
			break;
		}

		switch (statep->sample_bit_depth) {
		case AUDIOHD_BIT_DEPTH24:
			port->format |= 0x3;
			statep->sample_packed_bytes = 4;
			break;
		case AUDIOHD_BIT_DEPTH16:
		default: /* 16 bits is default */
			port->format |= 0x1;
			statep->sample_packed_bytes = 2;
			break;
		}

		port->nframes = 1024 * AUDIOHD_BDLE_NUMS *
		    statep->sample_rate / 48000;
		port->bufsize = port->nframes * port->nchan *
		    statep->sample_packed_bytes;
		port->fragsize = port->bufsize / AUDIOHD_BDLE_NUMS;

		/* allocate dma handle */
		rc = ddi_dma_alloc_handle(dip, &dma_attr, DDI_DMA_SLEEP,
		    NULL, &port->samp_dmah);
		if (rc != DDI_SUCCESS) {
			audio_dev_warn(adev,
			    "ddi_dma_alloc_handle failed: %d", rc);
			return (DDI_FAILURE);
		}

		/*
		 * Warning: please be noted that allocating the dma memory
		 * with the flag IOMEM_DATA_UNCACHED is a hack due
		 * to an incorrect cache synchronization on NVidia MCP79
		 * chipset which causes the audio distortion problem,
		 * and that it should be fixed later. There should be
		 * no reason you have to allocate UNCACHED memory. In
		 * complex architectures with nested IO caches,
		 * reliance on this flag might lead to failure.
		 */
		rc = ddi_dma_mem_alloc(port->samp_dmah, port->bufsize,
		    &hda_dev_accattr, DDI_DMA_CONSISTENT | IOMEM_DATA_UNCACHED,
		    DDI_DMA_SLEEP, NULL, &port->samp_kaddr,
		    &real_size, &port->samp_acch);
		if (rc == DDI_FAILURE) {
			if (ddi_dma_mem_alloc(port->samp_dmah, port->bufsize,
			    &hda_dev_accattr, DDI_DMA_CONSISTENT,
			    DDI_DMA_SLEEP, NULL,
			    &port->samp_kaddr, &real_size,
			    &port->samp_acch) != DDI_SUCCESS) {
				audio_dev_warn(adev,
				    "ddi_dma_mem_alloc failed");
				return (DDI_FAILURE);
			}
		}

		/* bind DMA buffer */
		rc = ddi_dma_addr_bind_handle(port->samp_dmah, NULL,
		    port->samp_kaddr, real_size, dir,
		    DDI_DMA_SLEEP, NULL, &cookie, &count);
		if ((rc != DDI_DMA_MAPPED) || (count != 1)) {
			audio_dev_warn(adev,
			    "ddi_dma_addr_bind_handle failed: %d", rc);
			return (DDI_FAILURE);
		}
		port->samp_paddr = (uint64_t)cookie.dmac_laddress;

		/*
		 * now, from here we allocate DMA
		 * memory for buffer descriptor list.
		 * we allocate adjacent DMA memory for all DMA engines.
		 */
		rc = ddi_dma_alloc_handle(dip, &dma_attr, DDI_DMA_SLEEP,
		    NULL, &port->bdl_dmah);
		if (rc != DDI_SUCCESS) {
			audio_dev_warn(adev,
			    "ddi_dma_alloc_handle(bdlist) failed");
			return (DDI_FAILURE);
		}

		/*
		 * we allocate all buffer descriptors lists in continuous
		 * dma memory.
		 */
		port->bdl_size = sizeof (sd_bdle_t) * AUDIOHD_BDLE_NUMS;
		rc = ddi_dma_mem_alloc(port->bdl_dmah, port->bdl_size,
		    &hda_dev_accattr, DDI_DMA_CONSISTENT, DDI_DMA_SLEEP, NULL,
		    &port->bdl_kaddr, &real_size, &port->bdl_acch);
		if (rc != DDI_SUCCESS) {
			audio_dev_warn(adev,
			    "ddi_dma_mem_alloc(bdlist) failed");
			return (DDI_FAILURE);
		}

		rc = ddi_dma_addr_bind_handle(port->bdl_dmah, NULL,
		    port->bdl_kaddr,
		    real_size, DDI_DMA_WRITE | DDI_DMA_CONSISTENT,
		    DDI_DMA_SLEEP,
		    NULL, &cookie, &count);
		if ((rc != DDI_DMA_MAPPED) || (count != 1)) {
			audio_dev_warn(adev, "addr_bind_handle failed");
			return (DDI_FAILURE);
		}
		port->bdl_paddr = (uint64_t)cookie.dmac_laddress;

		entry = (sd_bdle_t *)port->bdl_kaddr;
		buf_phys_addr = port->samp_paddr;

		for (j = 0; j < AUDIOHD_BDLE_NUMS; j++) {
			entry->sbde_addr = buf_phys_addr;
			entry->sbde_len = port->fragsize;
			entry->sbde_ioc = 1;
			buf_phys_addr += port->fragsize;
			entry++;
		}
		(void) ddi_dma_sync(port->bdl_dmah, 0, sizeof (sd_bdle_t) *
		    AUDIOHD_BDLE_NUMS, DDI_DMA_SYNC_FORDEV);
		port->curpos = 0;

		port->engine = audio_engine_alloc(&audiohd_engine_ops, caps);
		if (port->engine == NULL) {
			return (DDI_FAILURE);
		}

		audio_engine_set_private(port->engine, port);
		audio_dev_add_engine(adev, port->engine);
	}

	return (DDI_SUCCESS);
}

static void
audiohd_free_port(audiohd_state_t *statep)
{
	int			i;
	audiohd_port_t		*port;

	for (i = 0; i < PORT_MAX; i++) {
		port = statep->port[i];
		if (port == NULL)
			continue;
		if (port->engine) {
			audio_dev_remove_engine(statep->adev,
			    port->engine);
			audio_engine_free(port->engine);
		}
		if (port->samp_dmah) {
			(void) ddi_dma_unbind_handle(port->samp_dmah);
		}
		if (port->samp_acch) {
			ddi_dma_mem_free(&port->samp_acch);
		}
		if (port->samp_dmah) {
			ddi_dma_free_handle(&port->samp_dmah);
		}
		if (port->bdl_dmah) {
			(void) ddi_dma_unbind_handle(port->bdl_dmah);
		}
		if (port->bdl_acch) {
			ddi_dma_mem_free(&port->bdl_acch);
		}
		if (port->bdl_dmah) {
			ddi_dma_free_handle(&port->bdl_dmah);
		}

		kmem_free(port, sizeof (audiohd_port_t));
	}
}

/*
 * audiohd_change_widget_power_state(audiohd_state_t *statep, int state)
 * Description:
 * 	This routine is used to change the widget power betwen D0 and D2.
 * 	D0 is fully on; D2 allows the lowest possible power consuming state
 * 	from which it can return to the fully on state: D0.
 */
static void
audiohd_change_widget_power_state(audiohd_state_t *statep, int state)
{
	int			i;
	wid_t			wid;
	hda_codec_t		*codec;
	audiohd_widget_t	*widget;

	for (i = 0; i < AUDIOHD_CODEC_MAX; i++) {
		codec = statep->codec[i];
		if (codec == NULL)
			continue;
		for (wid = codec->first_wid; wid <= codec->last_wid;
		    wid++) {
			widget = codec->widget[wid];
			if (widget->widget_cap &
			    AUDIOHD_WIDCAP_PWRCTRL) {
				(void) audioha_codec_verb_get(statep,
				    codec->index, wid,
				    AUDIOHDC_VERB_SET_POWER_STATE,
				    state);
			}
		}
	}
}
/*
 * audiohd_restore_path()
 * Description:
 * 	This routine is used to restore the path on the codec.
 */
static void
audiohd_restore_path(audiohd_state_t *statep)
{
	int			i;
	hda_codec_t		*codec;

	for (i = 0; i < AUDIOHD_CODEC_MAX; i++) {
		codec = statep->codec[i];
		if (codec == NULL)
			continue;
		audiohd_finish_output_path(statep->codec[i]);
		audiohd_finish_input_path(statep->codec[i]);
		audiohd_finish_monitor_path(statep->codec[i]);
		audiohd_finish_beep_path(statep->codec[i]);
	}
}

/*
 * audiohd_reset_pins_ur_cap()
 * Description:
 * 	Enable the unsolicited response of the pins which have the unsolicited
 * 	response capability
 */
static void
audiohd_reset_pins_ur_cap(audiohd_state_t *statep)
{
	hda_codec_t		*codec;
	audiohd_pin_t		*pin;
	audiohd_widget_t	*widget;
	uint32_t		urctrl;
	int			i;

	for (i = 0; i < AUDIOHD_CODEC_MAX; i++) {
		codec = statep->codec[i];
		if (codec == NULL)
			continue;
		pin = codec->first_pin;
		while (pin) {
			/* enable the unsolicited response of the pin */
			widget = codec->widget[pin->wid];
			if ((widget->widget_cap &
			    (AUDIOHD_URCAP_MASK) &&
			    (pin->cap & AUDIOHD_DTCCAP_MASK)) &&
			    ((pin->device == DTYPE_LINEOUT) ||
			    (pin->device == DTYPE_SPDIF_OUT) ||
			    (pin->device == DTYPE_HP_OUT) ||
			    (pin->device == DTYPE_MIC_IN))) {
				urctrl = (uint8_t)(1 <<
				    (AUDIOHD_UR_ENABLE_OFF - 1));
				urctrl |= (pin->wid & AUDIOHD_UR_TAG_MASK);
				(void) audioha_codec_verb_get(statep,
				    codec->index,
				    pin->wid,
				    AUDIOHDC_VERB_SET_UNS_ENABLE, urctrl);
			}
			pin = pin->next;
		}
	}
}
static void
audiohd_restore_codec_gpio(audiohd_state_t *statep)
{
	int		i;
	wid_t		wid;
	hda_codec_t	*codec;

	for (i = 0; i < AUDIOHD_CODEC_MAX; i++) {
		codec = statep->codec[i];
		if (codec == NULL)
			continue;
		wid = codec->wid_afg;

		/* power-up audio function group */
		(void) audioha_codec_verb_get(statep, i, wid,
		    AUDIOHDC_VERB_SET_POWER_STATE, AUDIOHD_PW_D0);

		/* work around for Sony VAIO laptop with specific codec */
		if ((codec->codec_info->flags & NO_GPIO) == 0) {
			/*
			 * GPIO controls which are laptop specific workarounds
			 * and might be changed. Some laptops use GPIO,
			 * so we need to enable and set the GPIO correctly.
			 */
			(void) audioha_codec_verb_get(statep, i, wid,
			    AUDIOHDC_VERB_SET_GPIO_MASK, AUDIOHDC_GPIO_ENABLE);
			(void) audioha_codec_verb_get(statep, i, wid,
			    AUDIOHDC_VERB_SET_GPIO_DIREC, AUDIOHDC_GPIO_DIRECT);
			(void) audioha_codec_verb_get(statep, i, wid,
			    AUDIOHDC_VERB_SET_GPIO_STCK,
			    AUDIOHDC_GPIO_DATA_CTRL);
			(void) audioha_codec_verb_get(statep, i, wid,
			    AUDIOHDC_VERB_SET_GPIO_DATA,
			    AUDIOHDC_GPIO_STCK_CTRL);
		}
	}
}
/*
 * audiohd_resume()
 */
static int
audiohd_resume(audiohd_state_t *statep)
{
	uint8_t		rirbsts;

	mutex_enter(&statep->hda_mutex);
	statep->suspended = B_FALSE;
	/* Restore the hda state */
	if (audiohd_reinit_hda(statep) == DDI_FAILURE) {
		audio_dev_warn(statep->adev,
		    "hda reinit failed");
		mutex_exit(&statep->hda_mutex);
		return (DDI_FAILURE);
	}
	/* reset to enable the capability of unsolicited response for pin */
	audiohd_reset_pins_ur_cap(statep);
	/* clear the unsolicited response interrupt */
	rirbsts = AUDIOHD_REG_GET8(AUDIOHD_REG_RIRBSTS);
	AUDIOHD_REG_SET8(AUDIOHD_REG_RIRBSTS, rirbsts);
	/* set widget power to D0 */
	audiohd_change_widget_power_state(statep, AUDIOHD_PW_D0);

	audiohd_configure_output(statep);
	audiohd_configure_input(statep);
	mutex_exit(&statep->hda_mutex);

	audio_dev_resume(statep->adev);

	return (DDI_SUCCESS);
}	/* audiohd_resume */

/*
 * audiohd_suspend()
 */
static int
audiohd_suspend(audiohd_state_t *statep)
{
	audio_dev_suspend(statep->adev);

	mutex_enter(&statep->hda_mutex);
	statep->suspended = B_TRUE;

	/* set widget power to D2 */
	audiohd_change_widget_power_state(statep, AUDIOHD_PW_D2);
	/* Disable h/w */
	audiohd_stop_dma(statep);
	audiohd_fini_pci(statep);
	mutex_exit(&statep->hda_mutex);

	return (DDI_SUCCESS);
}	/* audiohd_suspend */

/*
 * audiohd_disable_pin()
 */
static void
audiohd_disable_pin(audiohd_state_t *statep, int caddr, wid_t wid)
{
	uint32_t	tmp;

	tmp = audioha_codec_verb_get(statep, caddr, wid,
	    AUDIOHDC_VERB_GET_PIN_CTRL, 0);
	if (tmp == AUDIOHD_CODEC_FAILURE)
		return;
	tmp = audioha_codec_verb_get(statep, caddr, wid,
	    AUDIOHDC_VERB_SET_PIN_CTRL,
	    (tmp & ~AUDIOHDC_PIN_CONTROL_OUT_ENABLE));
}

/*
 * audiohd_enable_pin()
 */
static void
audiohd_enable_pin(audiohd_state_t *statep, int caddr, wid_t wid)
{
	uint32_t	tmp;

	tmp = audioha_codec_verb_get(statep, caddr, wid,
	    AUDIOHDC_VERB_GET_PIN_CTRL, 0);
	if (tmp == AUDIOHD_CODEC_FAILURE)
		return;
	tmp = audioha_codec_verb_get(statep, caddr, wid,
	    AUDIOHDC_VERB_SET_PIN_CTRL,
	    tmp | AUDIOHDC_PIN_CONTROL_OUT_ENABLE |
	    AUDIOHDC_PIN_CONTROL_HP_ENABLE);
}

/*
 * audiohd_change_speaker_state()
 */
static void
audiohd_change_speaker_state(audiohd_state_t *statep, int on)
{
	audiohd_path_t		*path;
	audiohd_widget_t	*widget;
	audiohd_pin_t		*pin;
	int			i, j;
	wid_t			wid;

	for (i = 0; i < statep->pathnum; i++) {
		path = statep->path[i];
		if (!path || path->path_type != PLAY)
			continue;
		if (on) {
			for (j = 0; j < path->pin_nums; j++) {
				wid = path->pin_wid[j];
				widget = path->codec->widget[wid];
				pin = (audiohd_pin_t *)widget->priv;
				if (pin->device == DTYPE_SPEAKER) {
					audiohd_enable_pin(statep,
					    path->codec->index,
					    pin->wid);
				}
			}

		} else {
			for (j = 0; j < path->pin_nums; j++) {
				wid = path->pin_wid[j];
				widget = path->codec->widget[wid];
				pin = (audiohd_pin_t *)widget->priv;
				if (pin->device == DTYPE_SPEAKER) {
					audiohd_disable_pin(statep,
					    path->codec->index,
					    pin->wid);
				}
			}
		}
	}
}
/*
 * audiohd_select_mic()
 *
 * Description:
 *	This function is used for the recording path which has a selector
 *	as the sumwidget. We select the external MIC if it is plugged into the
 *	MIC jack, otherwise the internal integrated MIC is selected.
 */
static void
audiohd_select_mic(audiohd_state_t *statep, uint8_t index,
uint8_t id, int select)
{
	hda_codec_t		*codec;
	audiohd_path_t		*path;
	audiohd_widget_t	*widget, *sumwgt = NULL;
	audiohd_pin_t		*pin;
	int			i, j;
	wid_t			wid;

	codec = statep->codec[index];
	if (codec == NULL)
		return;

	for (i = 0; i < statep->pathnum; i++) {
		path = statep->path[i];
		if (path->codec != codec || path->path_type != RECORD)
			continue;
		sumwgt = codec->widget[path->sum_wid];

		for (j = 0; j < path->pin_nums; j++) {
			wid = path->pin_wid[j];
			widget = codec->widget[wid];
			pin = (audiohd_pin_t *)widget->priv;

			if (pin->device != DTYPE_MIC_IN)
				continue;

			if (sumwgt != NULL &&
			    sumwgt->type == WTYPE_AUDIO_SEL) {
				/* Have a selector to choose input pin */

				if (select && pin->wid == id &&
				    (((pin->config >>
				    AUDIOHD_PIN_CONTP_OFF) &
				    AUDIOHD_PIN_CONTP_MASK) ==
				    AUDIOHD_PIN_CON_JACK)) {
					(void) audioha_codec_verb_get(
					    statep,
					    index,
					    path->sum_wid,
					    AUDIOHDC_VERB_SET_CONN_SEL,
					    path->sum_selconn[j]);
					statep->port[PORT_ADC]->index =
					    path->tag;
					return;
				} else if (!select && pin->wid != id &&
				    (((pin->config >>
				    AUDIOHD_PIN_CONTP_OFF) &
				    AUDIOHD_PIN_CONTP_MASK) ==
				    AUDIOHD_PIN_CON_FIXED)) {
					(void) audioha_codec_verb_get(
					    statep,
					    index,
					    path->sum_wid,
					    AUDIOHDC_VERB_SET_CONN_SEL,
					    path->sum_selconn[j]);
					statep->port[PORT_ADC]->index =
					    path->tag;
					return;
				}
			} else {
				/*
				 * No selector widget in the path,
				 * mute unselected input pin
				 */

				/* Open all input pin, and then mute others */
				audiohd_set_pin_volume(statep, DTYPE_MIC_IN);

				if (select == 1) {
					/* Select external mic, mute internal */
					if (wid != id) {
						(void)
						    audioha_codec_4bit_verb_get(
						    statep, path->codec->index,
						    wid,
						    AUDIOHDC_VERB_SET_AMP_MUTE,
						    path->mute_dir |
						    AUDIOHDC_AMP_SET_LNR |
						    AUDIOHDC_AMP_SET_MUTE);
					}
				} else {
					/* Select internal mic, mute external */
					if (wid == id) {
						(void)
						    audioha_codec_4bit_verb_get(
						    statep, path->codec->index,
						    wid,
						    AUDIOHDC_VERB_SET_AMP_MUTE,
						    path->mute_dir |
						    AUDIOHDC_AMP_SET_LNR |
						    AUDIOHDC_AMP_SET_MUTE);
					}
				}
			}
		}
	}

	/*
	 * If the input istream > 1, we should set the record stream tag
	 * respectively. All the input streams sharing one tag may make the
	 * record sound distorted.
	 */
	if (codec->nistream > 1) {
		for (i = 0; i < statep->pathnum; i++) {
			path = statep->path[i];
			if (!path || path->path_type != RECORD)
				continue;
			for (j = 0; j < path->pin_nums; j++) {
				wid = path->pin_wid[j];
				widget = codec->widget[wid];
				if (widget == NULL)
					return;
				pin = (audiohd_pin_t *)widget->priv;
				if (select &&
				    pin->device == DTYPE_MIC_IN &&
				    pin->wid == id &&
				    (((pin->config >>
				    AUDIOHD_PIN_CONTP_OFF) &
				    AUDIOHD_PIN_CONTP_MASK) ==
				    AUDIOHD_PIN_CON_JACK)) {
					statep->port[PORT_ADC]->index =
					    path->tag;
					return;
				} else if (!select &&
				    pin->device == DTYPE_MIC_IN &&
				    (((pin->config >>
				    AUDIOHD_PIN_CONTP_OFF) &
				    AUDIOHD_PIN_CONTP_MASK) ==
				    AUDIOHD_PIN_CON_FIXED)) {
					statep->port[PORT_ADC]->index =
					    path->tag;
					return;
				}
			}
		}
	}
}
/*
 * audiohd_pin_sense()
 *
 * Description
 *
 * 	When the earphone is plugged into the jack associtated with the pin
 * 	complex, we disable the built in speaker. When the earphone is plugged
 * 	out of the jack, we enable the built in speaker.
 */
static void
audiohd_pin_sense(audiohd_state_t *statep, uint32_t resp, uint32_t respex)
{
	uint8_t			index;
	uint8_t			id;
	uint32_t		rs;
	audiohd_widget_t	*widget;
	audiohd_pin_t		*pin;
	hda_codec_t		*codec;

	index = respex & AUDIOHD_RIRB_CODEC_MASK;
	id = resp >> (AUDIOHD_RIRB_WID_OFF - 1);

	codec = statep->codec[index];
	if (codec == NULL)
		return;
	widget = codec->widget[id];
	if (widget == NULL)
		return;

	rs = audioha_codec_verb_get(statep, index, id,
	    AUDIOHDC_VERB_GET_PIN_SENSE, 0);
	if (rs & AUDIOHD_PIN_PRES_MASK) {
		/* A MIC is plugged in, we select the MIC as input */
		if ((widget->type == WTYPE_PIN) &&
		    (pin = (audiohd_pin_t *)widget->priv) &&
		    (pin->device == DTYPE_MIC_IN)) {
			audiohd_select_mic(statep, index, id, 1);
			return;
		}
		/* output pin is plugged */
		audiohd_change_speaker_state(statep, AUDIOHD_SP_OFF);
	} else {
		/*
		 * A MIC is unplugged, we select the built in MIC
		 * as input.
		 */
		if ((widget->type == WTYPE_PIN) &&
		    (pin = (audiohd_pin_t *)widget->priv) &&
		    (pin->device == DTYPE_MIC_IN)) {
			audiohd_select_mic(statep, index, id, 0);
			return;
		}
		/* output pin is unplugged */
		audiohd_change_speaker_state(statep, AUDIOHD_SP_ON);
	}

}

/*
 * audiohd_disable_intr()
 *
 * Description:
 *	Disable all possible interrupts.
 */
static void
audiohd_disable_intr(audiohd_state_t *statep)
{
	int		i;
	uint32_t	base;

	AUDIOHD_REG_SET32(AUDIOHD_REG_INTCTL, 0);
	base = AUDIOHD_REG_SD_BASE;
	for (i = 0; i < statep->hda_streams_nums; i++) {
		AUDIOHD_REG_SET8(base + AUDIOHD_SDREG_OFFSET_STS,
		    AUDIOHDR_SD_STS_INTRS);
		base += AUDIOHD_REG_SD_LEN;
	}
	AUDIOHD_REG_SET32(AUDIOHD_REG_INTSTS, (uint32_t)(-1));

}	/* audiohd_disable_intr() */


/*
 * audiohd_12bit_verb_to_codec()
 *
 * Description:
 *
 */
static int
audiohd_12bit_verb_to_codec(audiohd_state_t *statep, uint8_t caddr,
    uint8_t wid,
    uint16_t cmd, uint8_t param)
{
	uint32_t	verb;
	uint16_t	wptr;
	uint16_t	rptr;

	ASSERT((cmd & AUDIOHDC_12BIT_VERB_MASK) == 0);

	wptr = AUDIOHD_REG_GET16(AUDIOHD_REG_CORBWP) & AUDIOHD_CMDIO_ENT_MASK;
	rptr = AUDIOHD_REG_GET16(AUDIOHD_REG_CORBRP) & AUDIOHD_CMDIO_ENT_MASK;

	wptr++;
	wptr &= AUDIOHD_CMDIO_ENT_MASK;

	/* overflow */
	if (wptr == rptr) {
		return (DDI_FAILURE);
	}

	verb = (caddr & 0x0f) << AUDIOHD_VERB_ADDR_OFF;
	verb |= wid << AUDIOHD_VERB_NID_OFF;
	verb |= cmd << AUDIOHD_VERB_CMD_OFF;
	verb |= param;

	*((uint32_t *)(statep->hda_dma_corb.ad_vaddr) + wptr) = verb;
	(void) ddi_dma_sync(statep->hda_dma_corb.ad_dmahdl, 0,
	    sizeof (sd_bdle_t) * AUDIOHD_BDLE_NUMS, DDI_DMA_SYNC_FORDEV);
	AUDIOHD_REG_SET16(AUDIOHD_REG_CORBWP, wptr);

	return (DDI_SUCCESS);

}	/* audiohd_12bit_verb_to_codec() */

/*
 * audiohd_4bit_verb_to_codec()
 *
 * Description:
 *
 */
static int
audiohd_4bit_verb_to_codec(audiohd_state_t *statep, uint8_t caddr,
    uint8_t wid,
    uint32_t cmd, uint16_t param)
{
	uint32_t	verb;
	uint16_t	wptr;
	uint16_t	rptr;

	ASSERT((cmd & AUDIOHDC_4BIT_VERB_MASK) == 0);

	wptr = AUDIOHD_REG_GET16(AUDIOHD_REG_CORBWP) & AUDIOHD_CMDIO_ENT_MASK;
	rptr = AUDIOHD_REG_GET16(AUDIOHD_REG_CORBRP) & AUDIOHD_CMDIO_ENT_MASK;

	wptr++;
	wptr &= AUDIOHD_CMDIO_ENT_MASK;

	/* overflow */
	if (wptr == rptr) {
		return (DDI_FAILURE);
	}

	verb = (caddr & 0x0f) << AUDIOHD_VERB_ADDR_OFF;
	verb |= wid << AUDIOHD_VERB_NID_OFF;
	verb |= cmd << AUDIOHD_VERB_CMD16_OFF;
	verb |= param;

	*((uint32_t *)(statep->hda_dma_corb.ad_vaddr) + wptr) = verb;
	AUDIOHD_REG_SET16(AUDIOHD_REG_CORBWP, wptr);

	return (DDI_SUCCESS);

}	/* audiohd_4bit_verb_to_codec() */

/*
 * audiohd_response_from_codec()
 *
 * Description:
 *
 */
static int
audiohd_response_from_codec(audiohd_state_t *statep, uint32_t *resp,
    uint32_t *respex)
{
	uint16_t	wptr;
	uint16_t	rptr;
	uint32_t	*lp;

	wptr = AUDIOHD_REG_GET16(AUDIOHD_REG_RIRBWP) & 0x00ff;
	rptr = statep->hda_rirb_rp;

	if (rptr == wptr) {
		return (DDI_FAILURE);
	}

	rptr++;
	rptr &= AUDIOHD_RING_MAX_SIZE;

	lp = (uint32_t *)(statep->hda_dma_rirb.ad_vaddr) + (rptr << 1);
	*resp = *(lp);
	*respex = *(lp + 1);

	statep->hda_rirb_rp = rptr;

	return (DDI_SUCCESS);

}	/* audiohd_response_from_codec() */


/*
 * audioha_codec_verb_get()
 */
static uint32_t
audioha_codec_verb_get(void *arg, uint8_t caddr, uint8_t wid,
    uint16_t verb,
    uint8_t param)
{
	audiohd_state_t	*statep = (audiohd_state_t *)arg;
	uint32_t	resp;
	uint32_t	respex;
	int		ret;
	int		i;

	ret = audiohd_12bit_verb_to_codec(statep, caddr, wid, verb, param);
	if (ret != DDI_SUCCESS) {
		return (uint32_t)(-1);
	}

	/*
	 * Empirical testing times. 50 times is enough for audiohd spec 1.0.
	 * But we need to make it work for audiohd spec 0.9, which is just a
	 * draft version and requires more time to wait.
	 */
	for (i = 0; i < 500; i++) {
		ret = audiohd_response_from_codec(statep, &resp, &respex);
		if (((respex & AUDIOHD_BDLE_RIRB_SDI) == caddr) &&
		    ((respex & AUDIOHD_BDLE_RIRB_UNSOLICIT) == 0) &&
		    (ret == DDI_SUCCESS))
			break;
		/* Empirical testing time, which works well */
		drv_usecwait(30);
	}

	if (ret == DDI_SUCCESS) {
		return (resp);
	}

	if (wid != AUDIOHDC_NODE_ROOT && param != AUDIOHDC_PAR_VENDOR_ID) {
		audio_dev_warn(statep->adev,  "timeout when get "
		    "response from codec: wid=%d, verb=0x%04x, param=0x%04x",
		    wid, verb, param);
	}

	return ((uint32_t)(-1));

}	/* audioha_codec_verb_get() */


/*
 * audioha_codec_4bit_verb_get()
 */
static uint32_t
audioha_codec_4bit_verb_get(void *arg, uint8_t caddr, uint8_t wid,
    uint16_t verb, uint16_t param)
{
	audiohd_state_t	*statep = (audiohd_state_t *)arg;
	uint32_t	resp;
	uint32_t	respex;
	int		ret;
	int		i;

	ret = audiohd_4bit_verb_to_codec(statep, caddr, wid, verb, param);
	if (ret != DDI_SUCCESS) {
		return (uint32_t)(-1);
	}

	for (i = 0; i < 500; i++) {
		ret = audiohd_response_from_codec(statep, &resp, &respex);
		if (((respex & AUDIOHD_BDLE_RIRB_SDI) == caddr) &&
		    ((respex & AUDIOHD_BDLE_RIRB_UNSOLICIT) == 0) &&
		    (ret == DDI_SUCCESS))
			break;
		/* Empirical testing time, which works well */
		drv_usecwait(30);
	}

	if (ret == DDI_SUCCESS) {
		return (resp);
	}

	audio_dev_warn(statep->adev,  "timeout when get "
	    "response from codec: wid=%d, verb=0x%04x, param=0x%04x",
	    wid, verb, param);

	return ((uint32_t)(-1));

}	/* audioha_codec_4bit_verb_get() */
