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
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 */
#ifndef _SYS_AUDIOHD_IMPL_H_
#define	_SYS_AUDIOHD_IMPL_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * vendor IDs of PCI audio controllers
 */
#define	AUDIOHD_VID_ATI		0x1002
#define	AUDIOHD_VID_CIRRUS	0x1013
#define	AUDIOHD_VID_NVIDIA	0x10de
#define	AUDIOHD_VID_REALTEK	0x10ec
#define	AUDIOHD_VID_CREATIVE	0x1102
#define	AUDIOHD_VID_IDT		0x111d
#define	AUDIOHD_VID_ANALOG	0x11d4
#define	AUDIOHD_VID_CONEXANT	0x14f1
#define	AUDIOHD_VID_SIGMATEL	0x8384
#define	AUDIOHD_VID_INTEL	0x8086

/*
 * specific audiohd controller device id
 */
#define	AUDIOHD_CONTROLLER_MCP51	0x10de026c

/*
 * codec special initial flags
 */
#define	NO_GPIO		0x00000001
#define	NO_MIXER	0x00000002
#define	NO_SPDIF	0x00000004
#define	EN_PIN_BEEP	0x00000008

#define	AUDIOHD_DEV_CONFIG	"onboard1"
#define	AUDIOHD_DEV_VERSION	"a"

/*
 * Only for Intel hardware:
 * PCI Express traffic class select register in PCI configure space
 */
#define	AUDIOHD_INTEL_PCI_TCSEL 0x44
#define	AUDIOHD_INTEL_TCS_MASK	0xf8

/*
 * Only for ATI SB450:
 * MISC control register 2
 */
#define	AUDIOHD_ATI_PCI_MISC2	0x42
#define	AUDIOHD_ATI_MISC2_MASK	0xf8
#define	AUDIOHD_ATI_MISC2_SNOOP	0x02

/* NVIDIA snoop */
#define	AUDIOHD_NVIDIA_SNOOP	0x0f

#define	AUDIOHDC_NID(x)		x
#define	AUDIOHDC_NULL_NODE	-1
#define	AUDIOHD_NULL_CONN	((uint_t)(-1))

#define	AUDIOHD_EXT_AMP_MASK	0x00010000
#define	AUDIOHD_EXT_AMP_ENABLE	0x02

/* Power On/Off */
#define	AUDIOHD_PW_D0		0
#define	AUDIOHD_PW_D2		2

/* Pin speaker On/Off */
#define	AUDIOHD_SP_ON		1
#define	AUDIOHD_SP_OFF		0

#define	AUDIOHD_PORT_MAX	15
#define	AUDIOHD_CODEC_MAX	16
#define	AUDIOHD_MEMIO_LEN	0x4000

#define	AUDIOHD_RETRY_TIMES	60
#define	AUDIOHD_TEST_TIMES	500
#define	AUDIOHD_OUTSTR_NUM_OFF	12
#define	AUDIOHD_INSTR_NUM_OFF	8

#define	AUDIOHD_CORB_SIZE_OFF	0x4e

#define	AUDIOHD_URCAP_MASK	0x80
#define	AUDIOHD_DTCCAP_MASK	0x4
#define	AUDIOHD_UR_ENABLE_OFF	8
#define	AUDIOHD_UR_TAG_MASK	0x3f

#define	AUDIOHD_CIS_MASK	0x40000000

#define	AUDIOHD_RIRB_UR_MASK	0x10
#define	AUDIOHD_RIRB_CODEC_MASK	0xf
#define	AUDIOHD_RIRB_WID_OFF	27
#define	AUDIOHD_RIRB_INTRCNT	0x0
#define	AUDIOHD_RIRB_WPMASK	0xff

#define	AUDIOHD_FORM_MASK	0x0080
#define	AUDIOHD_LEN_MASK	0x007f
#define	AUDIOHD_PIN_CAP_MASK	0x00000010
#define	AUDIOHD_PIN_CONF_MASK	0xc0000000
#define	AUDIOHD_PIN_CON_MASK	3
#define	AUDIOHD_PIN_CON_STEP	30
#define	AUDIOHD_PIN_IO_MASK	0X0018
#define	AUDIOHD_PIN_SEQ_MASK	0x0000000f
#define	AUDIOHD_PIN_ASO_MASK	0x000000f0
#define	AUDIOHD_PIN_ASO_OFF	0x4
#define	AUDIOHD_PIN_DEV_MASK	0x00f00000
#define	AUDIOHD_PIN_DEV_OFF	20
#define	AUDIOHD_PIN_NUMS	6
#define	AUDIOHD_PIN_NO_CONN	0x40000000
#define	AUDIOHD_PIN_IN_ENABLE	0x20
#define	AUDIOHD_PIN_OUT_ENABLE	0x40
#define	AUDIOHD_PIN_PRES_MASK	0x80000000
#define	AUDIOHD_PIN_CONTP_OFF	0x1e
#define	AUDIOHD_PIN_CON_JACK	0
#define	AUDIOHD_PIN_CON_FIXED	0x2
#define	AUDIOHD_PIN_CONTP_MASK	0x3
#define	AUDIOHD_PIN_VREF_L1	0x20
#define	AUDIOHD_PIN_VREF_L2	0x10
#define	AUDIOHD_PIN_VREF_L3	0x04
#define	AUDIOHD_PIN_VREF_L4	0x02
#define	AUDIOHD_PIN_VREF_OFF	8
#define	AUDIOHD_PIN_VREF_MASK	0xff
#define	AUDIOHD_PIN_CLR_MASK	0xf
#define	AUDIOHD_PIN_CLR_OFF	12

#define	AUDIOHD_VERB_ADDR_OFF	28
#define	AUDIOHD_VERB_NID_OFF	20
#define	AUDIOHD_VERB_CMD_OFF	8
#define	AUDIOHD_VERB_CMD16_OFF	16

#define	AUDIOHD_RING_MAX_SIZE	0x00ff
#define	AUDIOHD_REC_TAG_OFF	4
#define	AUDIOHD_PLAY_TAG_OFF	4
#define	AUDIOHD_PLAY_CTL_OFF	2
#define	AUDIOHD_REC_CTL_OFF	2

#define	AUDIOHD_SPDIF_ON	1
#define	AUDIOHD_SPDIF_MASK	0x00ff

#define	AUDIOHD_GAIN_OFF	8

#define	AUDIOHD_CODEC_STR_OFF	16
#define	AUDIOHD_CODEC_STR_MASK	0x000000ff
#define	AUDIOHD_CODEC_NUM_MASK	0x000000ff
#define	AUDIOHD_CODEC_TYPE_MASK	0x000000ff

#define	AUDIOHD_ROUNDUP(x, algn)	(((x) + ((algn) - 1)) & ~((algn) - 1))
#define	AUDIOHD_BDLE_BUF_ALIGN	128
#define	AUDIOHD_CMDIO_ENT_MASK	0x00ff	/* 256 entries for CORB/RIRB */
#define	AUDIOHD_CDBIO_CORB_LEN	1024	/* 256 entries for CORB, 1024B */
#define	AUDIOHD_CDBIO_RIRB_LEN	2048	/* 256 entries for RIRB, 2048B */
#define	AUDIOHD_BDLE_NUMS	4	/* 4 entires for record/play BD list */

#define	AUDIOHD_PORT_UNMUTE	(0xffffffff)

/*
 * Audio registers of high definition
 */
#define	AUDIOHD_REG_GCAP		0x00
#define	AUDIOHDR_GCAP_OUTSTREAMS	0xf000
#define	AUDIOHDR_GCAP_INSTREAMS		0x0f00
#define	AUDIOHDR_GCAP_BSTREAMS		0x00f8
#define	AUDIOHDR_GCAP_NSDO		0x0006
#define	AUDIOHDR_GCAP_64OK		0x0001

#define	AUDIOHD_REG_VMIN		0x02
#define	AUDIOHD_REG_VMAJ		0x03
#define	AUDIOHD_REG_OUTPAY		0x04
#define	AUDIOHD_REG_INPAY		0x06
#define	AUDIOHD_REG_GCTL		0x08
#define	AUDIOHD_REG_WAKEEN		0x0C
#define	AUDIOHD_REG_STATESTS		0x0E
#define	AUDIOHD_STATESTS_BIT_SDINS	0x7F

#define	AUDIOHD_REG_GSTS		0x10
#define	AUDIOHD_REG_INTCTL		0x20
#define	AUDIOHD_INTCTL_BIT_GIE		0x80000000
#define	AUDIOHD_INTCTL_BIT_CIE		0x40000000
#define	AUDIOHD_INTCTL_BIT_SIE		0x3FFFFFFF


#define	AUDIOHD_REG_INTSTS		0x24
#define	AUDIOHD_INTSTS_BIT_GIS		0x80000000
#define	AUDIOHD_INTSTS_BIT_CIS		0x40000000
#define	AUDIOHD_INTSTS_BIT_SINTS	(0x3fffffff)

#define	AUDIOHD_REG_WALCLK		0x30
#define	AUDIOHD_REG_SYNC		0x38

#define	AUDIOHD_REG_CORBLBASE		0x40
#define	AUDIOHD_REG_CORBUBASE		0x44
#define	AUDIOHD_REG_CORBWP		0x48
#define	AUDIOHD_REG_CORBRP		0x4A
#define	AUDIOHD_REG_CORBCTL		0x4C
#define	AUDIOHD_REG_CORBST		0x4D
#define	AUDIOHD_REG_CORBSIZE		0x4E

#define	AUDIOHD_REG_RIRBLBASE		0x50
#define	AUDIOHD_REG_RIRBUBASE		0x54
#define	AUDIOHD_REG_RIRBWP		0x58
#define	AUDIOHD_REG_RINTCNT		0x5A
#define	AUDIOHD_REG_RIRBCTL		0x5C
#define	AUDIOHD_REG_RIRBSTS		0x5D
#define	AUDIOHD_REG_RIRBSIZE		0x5E

#define	AUDIOHD_REG_IC			0x60
#define	AUDIOHD_REG_IR			0x64
#define	AUDIOHD_REG_IRS			0x68
#define	AUDIOHD_REG_DPLBASE		0x70
#define	AUDIOHD_REG_DPUBASE		0x74

#define	AUDIOHD_REG_SD_BASE		0x80
#define	AUDIOHD_REG_SD_LEN		0x20

/*
 * Offset of Stream Descriptor Registers
 */
#define	AUDIOHD_SDREG_OFFSET_CTL		0x00
#define	AUDIOHD_SDREG_OFFSET_STS		0x03
#define	AUDIOHD_SDREG_OFFSET_LPIB		0x04
#define	AUDIOHD_SDREG_OFFSET_CBL		0x08
#define	AUDIOHD_SDREG_OFFSET_LVI		0x0c
#define	AUDIOHD_SDREG_OFFSET_FIFOW		0x0e
#define	AUDIOHD_SDREG_OFFSET_FIFOSIZE		0x10
#define	AUDIOHD_SDREG_OFFSET_FORMAT		0x12
#define	AUDIOHD_SDREG_OFFSET_BDLPL		0x18
#define	AUDIOHD_SDREG_OFFSET_BDLPU		0x1c

/* bits for stream descriptor control reg */
#define	AUDIOHDR_SD_CTL_DEIE		0x000010
#define	AUDIOHDR_SD_CTL_FEIE		0x000008
#define	AUDIOHDR_SD_CTL_IOCE		0x000004
#define	AUDIOHDR_SD_CTL_SRUN		0x000002
#define	AUDIOHDR_SD_CTL_SRST		0x000001

/* bits for stream descriptor status register */
#define	AUDIOHDR_SD_STS_BCIS		0x0004
#define	AUDIOHDR_SD_STS_FIFOE		0x0008
#define	AUDIOHDR_SD_STS_DESE		0x0010
#define	AUDIOHDR_SD_STS_FIFORY		0x0020
#define	AUDIOHDR_SD_STS_INTRS	\
	(AUDIOHDR_SD_STS_BCIS | \
	AUDIOHDR_SD_STS_FIFOE |	\
	AUDIOHDR_SD_STS_DESE)

/* bits for GCTL register */
#define	AUDIOHDR_GCTL_CRST		0x00000001
#define	AUDIOHDR_GCTL_URESPE		0x00000100

/* bits for CORBRP register */
#define	AUDIOHDR_CORBRP_RESET		0x8000
#define	AUDIOHDR_CORBRP_WPTR		0x00ff

/* bits for CORBCTL register */
#define	AUDIOHDR_CORBCTL_CMEIE		0x01
#define	AUDIOHDR_CORBCTL_DMARUN		0x02

/* bits for CORB SIZE register */
#define	AUDIOHDR_CORBSZ_8		0
#define	AUDIOHDR_CORBSZ_16		1
#define	AUDIOHDR_CORBSZ_256		2

/* bits for RIRBCTL register */
#define	AUDIOHDR_RIRBCTL_RINTCTL	0x01
#define	AUDIOHDR_RIRBCTL_DMARUN		0x02
#define	AUDIOHDR_RIRBCTL_RIRBOIC	0x04
#define	AUDIOHDR_RIRBCTL_RSTINT		0xfe

/* bits for RIRBWP register */
#define	AUDIOHDR_RIRBWP_RESET		0x8000
#define	AUDIOHDR_RIRBWP_WPTR		0x00ff

/* bits for RIRB SIZE register */
#define	AUDIOHDR_RIRBSZ_8		0
#define	AUDIOHDR_RIRBSZ_16		1
#define	AUDIOHDR_RIRBSZ_256		2

#define	AUDIOHD_BDLE_RIRB_SDI		0x0000000f
#define	AUDIOHD_BDLE_RIRB_UNSOLICIT	0x00000010

/* HD spec: ID of Root node is 0 */
#define	AUDIOHDC_NODE_ROOT		0x00

/* HD spec: ID of audio function group is "1" */
#define	AUDIOHDC_AUDIO_FUNC_GROUP	1

/*
 * HD audio verbs can be either 12-bit or 4-bit in length.
 */
#define	AUDIOHDC_12BIT_VERB_MASK	0xfffff000
#define	AUDIOHDC_4BIT_VERB_MASK		0xfffffff0

#define	AUDIOHDC_SAMPR48000		48000
#define	AUDIOHDC_MAX_BEEP_GEN		12000
#define	AUDIOHDC_MIX_BEEP_GEN		47
#define	AUDIOHDC_MUTE_BEEP_GEN		0x0

/*
 * 12-bit verbs
 */
#define	AUDIOHDC_VERB_GET_PARAM			0xf00

#define	AUDIOHDC_VERB_GET_CONN_SEL		0xf01
#define	AUDIOHDC_VERB_SET_CONN_SEL		0x701

#define	AUDIOHDC_VERB_GET_CONN_LIST_ENT		0xf02
#define	AUDIOHDC_VERB_GET_PROCESS_STATE		0xf03
#define	AUDIOHDC_VERB_GET_SDI_SEL		0xf04

#define	AUDIOHDC_VERB_GET_POWER_STATE		0xf05
#define	AUDIOHDC_VERB_SET_POWER_STATE		0x705

#define	AUDIOHDC_VERB_GET_STREAM_CHANN		0xf06
#define	AUDIOHDC_VERB_SET_STREAM_CHANN		0x706

#define	AUDIOHDC_VERB_GET_PIN_CTRL		0xf07
#define	AUDIOHDC_VERB_SET_PIN_CTRL		0x707

#define	AUDIOHDC_VERB_GET_UNS_ENABLE		0xf08
#define	AUDIOHDC_VERB_SET_UNS_ENABLE		0x708

#define	AUDIOHDC_VERB_GET_PIN_SENSE		0xf09
#define	AUDIOHDC_VERB_GET_PIN_SENSE		0xf09
#define	AUDIOHDC_VERB_EXEC_PIN_SENSE		0x709

#define	AUDIOHDC_VERB_GET_BEEP_GEN		0xf0a
#define	AUDIOHDC_VERB_SET_BEEP_GEN		0x70a

#define	AUDIOHDC_VERB_GET_EAPD			0xf0c
#define	AUDIOHDC_VERB_SET_EAPD			0x70c

#define	AUDIOHDC_VERB_GET_DEFAULT_CONF		0xf1c
#define	AUDIOHDC_VERB_GET_SPDIF_CTL		0xf0d
#define	AUDIOHDC_VERB_SET_SPDIF_LCL		0x70d

#define	AUDIOHDC_VERB_GET_GPIO_MASK		0xf16
#define	AUDIOHDC_VERB_SET_GPIO_MASK		0x716

#define	AUDIOHDC_VERB_GET_UNSOL_ENABLE_MASK	0xf19
#define	AUDIOHDC_VERB_SET_UNSOL_ENABLE_MASK	0x719

#define	AUDIOHDC_VERB_GET_GPIO_DIREC		0xf17
#define	AUDIOHDC_VERB_SET_GPIO_DIREC		0x717

#define	AUDIOHDC_VERB_GET_GPIO_DATA		0xf15
#define	AUDIOHDC_VERB_SET_GPIO_DATA		0x715

#define	AUDIOHDC_VERB_GET_GPIO_STCK		0xf1a
#define	AUDIOHDC_VERB_SET_GPIO_STCK		0x71a

#define	AUDIOHDC_GPIO_ENABLE			0xff
#define	AUDIOHDC_GPIO_DIRECT			0xf1

#define	AUDIOHDC_GPIO_DATA_CTRL			0xff
#define	AUDIOHDC_GPIO_STCK_CTRL			0xff
/*
 * 4-bit verbs
 */
#define	AUDIOHDC_VERB_GET_CONV_FMT		0xa
#define	AUDIOHDC_VERB_SET_CONV_FMT		0x2

#define	AUDIOHDC_VERB_GET_AMP_MUTE		0xb
#define	AUDIOHDC_VERB_SET_AMP_MUTE		0x3
#define	AUDIOHDC_VERB_SET_BEEP_VOL		0x3A0

/*
 * parameters of nodes
 */
#define	AUDIOHDC_PAR_VENDOR_ID			0x00
#define	AUDIOHDC_PAR_SUBSYS_ID			0x01
#define	AUDIOHDC_PAR_REV_ID			0x02
#define	AUDIOHDC_PAR_NODE_COUNT			0x04
#define	AUDIOHDC_PAR_FUNCTION_TYPE		0x05
#define	AUDIOHDC_PAR_AUDIO_FG_CAP		0x08
#define	AUDIOHDC_PAR_AUDIO_WID_CAP		0x09
#define	AUDIOHDC_PAR_PCM			0x0a
#define	AUDIOHDC_PAR_STREAM			0x0b
#define	AUDIOHDC_PAR_PIN_CAP			0x0c
#define	AUDIOHDC_PAR_INAMP_CAP			0x0d
#define	AUDIOHDC_PAR_CONNLIST_LEN		0x0e
#define	AUDIOHDC_PAR_POWER_STATE		0x0f
#define	AUDIOHDC_PAR_PROC_CAP			0x10
#define	AUDIOHDC_PAR_GPIO_CAP			0x11
#define	AUDIOHDC_PAR_OUTAMP_CAP			0x12

/*
 * bits for get/set amplifier gain/mute
 */
#define	AUDIOHDC_AMP_SET_OUTPUT			0x8000
#define	AUDIOHDC_AMP_SET_INPUT			0x4000
#define	AUDIOHDC_AMP_SET_LEFT			0x2000
#define	AUDIOHDC_AMP_SET_RIGHT			0x1000
#define	AUDIOHDC_AMP_SET_MUTE			0x0080
#define	AUDIOHDC_AMP_SET_LNR			0x3000
#define	AUDIOHDC_AMP_SET_LR_INPUT		0x7000
#define	AUDIOHDC_AMP_SET_LR_OUTPUT		0xb000
#define	AUDIOHDC_AMP_SET_INDEX_OFFSET		8
#define	AUDIOHDC_AMP_SET_GAIN_MASK		0x007f
#define	AUDIOHDC_GAIN_MAX			0x7f
#define	AUDIOHDC_GAIN_BITS			7
#define	AUDIOHDC_GAIN_DEFAULT			0x0f

#define	AUDIOHDC_AMP_GET_OUTPUT			0x8000
#define	AUDIOHDC_AMP_GET_INPUT			0x0000

/* value used to set max volume for left output */
#define	AUDIOHDC_AMP_LOUT_MAX	\
	(AUDIOHDC_AMP_SET_OUTPUT | \
	AUDIOHDC_AMP_SET_LEFT | \
	AUDIOHDC_GAIN_MAX)

/* value used to set max volume for right output */
#define	AUDIOHDC_AMP_ROUT_MAX	\
	(AUDIOHDC_AMP_SET_OUTPUT | \
	AUDIOHDC_AMP_SET_RIGHT | \
	AUDIOHDC_GAIN_MAX)


/*
 * Bits for pin widget control verb
 */
#define	AUDIOHDC_PIN_CONTROL_HP_ENABLE		0x80
#define	AUDIOHDC_PIN_CONTROL_OUT_ENABLE		0x40
#define	AUDIOHDC_PIN_CONTROL_IN_ENABLE		0x20

/*
 * Bits for Amplifier capabilities
 */
#define	AUDIOHDC_AMP_CAP_MUTE_CAP		0x80000000
#define	AUDIOHDC_AMP_CAP_STEP_SIZE		0x007f0000
#define	AUDIOHDC_AMP_CAP_STEP_NUMS		0x00007f00
#define	AUDIOHDC_AMP_CAP_0DB_OFFSET		0x0000007f


/*
 * Bits for Audio Widget Capabilities
 */
#define	AUDIOHD_WIDCAP_STEREO		0x00000001
#define	AUDIOHD_WIDCAP_INAMP		0x00000002
#define	AUDIOHD_WIDCAP_OUTAMP		0x00000004
#define	AUDIOHD_WIDCAP_AMP_OVRIDE	0x00000008
#define	AUDIOHD_WIDCAP_FMT_OVRIDE	0x00000010
#define	AUDIOHD_WIDCAP_STRIP		0x00000020
#define	AUDIOHD_WIDCAP_PROC_WID		0x00000040
#define	AUDIOHD_WIDCAP_UNSOL		0x00000080
#define	AUDIOHD_WIDCAP_CONNLIST		0x00000100
#define	AUDIOHD_WIDCAP_DIGIT		0x00000200
#define	AUDIOHD_WIDCAP_PWRCTRL		0x00000400
#define	AUDIOHD_WIDCAP_LRSWAP		0x00000800
#define	AUDIOHD_WIDCAP_TYPE		0x00f00000
#define	AUDIOHD_WIDCAP_TO_WIDTYPE(wcap)		\
	((wcap & AUDIOHD_WIDCAP_TYPE) >> 20)

#define	AUDIOHD_CODEC_FAILURE	(uint32_t)(-1)

/* Higher sample/bits support */
#define	AUDIOHD_BIT_DEPTH16	0x00020000
#define	AUDIOHD_BIT_DEPTH24	0x00080000
#define	AUDIOHD_SAMP_RATE48	0x00000040
#define	AUDIOHD_SAMP_RATE96	0x00000100
#define	AUDIOHD_SAMP_RATE192	0x00000400

/*
 * buffer descriptor list entry of stream descriptor
 */
typedef struct {
	uint64_t	sbde_addr;
	uint32_t	sbde_len;
	uint32_t
		sbde_ioc: 1,
		reserved: 31;
}sd_bdle_t;


#define	AUDIOHD_PLAY_STARTED		0x00000001
#define	AUDIOHD_PLAY_EMPTY		0x00000002
#define	AUDIOHD_PLAY_PAUSED		0x00000004
#define	AUDIOHD_RECORD_STARTED		0x00000008

enum audiohda_widget_type {
	WTYPE_AUDIO_OUT = 0,
	WTYPE_AUDIO_IN,
	WTYPE_AUDIO_MIX,
	WTYPE_AUDIO_SEL,
	WTYPE_PIN,
	WTYPE_POWER,
	WTYPE_VOL_KNOB,
	WTYPE_BEEP,
	WTYPE_VENDOR = 0xf
};

enum audiohda_device_type {
	DTYPE_LINEOUT = 0,
	DTYPE_SPEAKER,
	DTYPE_HP_OUT,
	DTYPE_CD,
	DTYPE_SPDIF_OUT,
	DTYPE_DIGIT_OUT,
	DTYPE_MODEM_SIDE,
	DTYPE_MODEM_HNAD_SIDE,
	DTYPE_LINE_IN,
	DTYPE_AUX,
	DTYPE_MIC_IN,
	DTYPE_TEL,
	DTYPE_SPDIF_IN,
	DTYPE_DIGIT_IN,
	DTYPE_OTHER = 0x0f,
};

enum audiohd_pin_color {
	AUDIOHD_PIN_UNKNOWN = 0,
	AUDIOHD_PIN_BLACK,
	AUDIOHD_PIN_GREY,
	AUDIOHD_PIN_BLUE,
	AUDIOHD_PIN_GREEN,
	AUDIOHD_PIN_RED,
	AUDIOHD_PIN_ORANGE,
	AUDIOHD_PIN_YELLOW,
	AUDIOHD_PIN_PURPLE,
	AUDIOHD_PIN_PINK,
	AUDIOHD_PIN_WHITE = 0xe,
	AUDIOHD_PIN_OTHER = 0xf,
};

/* values for audiohd_widget.path_flags */
#define	AUDIOHD_PATH_DAC	(1 << 0)
#define	AUDIOHD_PATH_ADC	(1 << 1)
#define	AUDIOHD_PATH_MON	(1 << 2)
#define	AUDIOHD_PATH_NOMON	(1 << 3)
#define	AUDIOHD_PATH_BEEP	(1 << 4)
#define	AUDIOHD_PATH_LOOPBACK	(1 << 5)

typedef struct audiohd_path	audiohd_path_t;
typedef struct audiohd_widget	audiohd_widget_t;
typedef struct audiohd_state	audiohd_state_t;
typedef struct audiohd_codec_info	audiohd_codec_info_t;
typedef struct audiohd_pin	audiohd_pin_t;
typedef struct hda_codec	hda_codec_t;
typedef uint32_t	wid_t;		/* id of widget */
typedef	struct audiohd_entry_prop	audiohd_entry_prop_t;
typedef	enum audiohda_device_type	audiohda_device_type_t;
typedef	enum audiohd_pin_color		audiohd_pin_color_t;

#define	AUDIOHD_MAX_WIDGET		128
#define	AUDIOHD_MAX_CONN		16
#define	AUDIOHD_MAX_PINS		16
#define	AUDIOHD_MAX_DEPTH		8

struct audiohd_entry_prop {
	uint32_t	conn_len;
	uint32_t	mask_range;
	uint32_t	mask_wid;
	wid_t		input_wid;
	int		conns_per_entry;
	int		bits_per_conn;
};
struct audiohd_widget {
	wid_t		wid_wid;
	hda_codec_t	*codec;
	enum audiohda_widget_type type;

	uint32_t	widget_cap;
	uint32_t	pcm_format;
	uint32_t	inamp_cap;
	uint32_t	outamp_cap;

	uint32_t	path_flags;

	int		out_weight;
	int		in_weight;
	int		finish;

	/*
	 * available (input) connections. 0 means this widget
	 * has fixed connection
	 */
	int		nconns;

	/*
	 * wid of possible & selected input & output connections
	 */
	wid_t		avail_conn[AUDIOHD_MAX_CONN];
	wid_t		output_path_next;	/* output pin -> DAC */
	wid_t		input_path_next;	/* ADC -> input pin */
	wid_t		monitor_path_next[AUDIOHD_MAX_CONN];
						/* output pin -> input pin */
	wid_t		beep_path_next;		/* output pin -> beep widget */
	wid_t		loopback_path_next;	/* ADC -> output pin */

	uint16_t 	used;

	/*
	 * pointer to struct depending on widget type:
	 *	1. DAC	audiohd_path_t
	 *	2. ADC	audiohd_path_t
	 *	3. PIN	audiohd_pin_t
	 */
	void	*priv;
};

#define	AUDIOHD_FLAG_LINEOUT		(1 << 0)
#define	AUDIOHD_FLAG_SPEAKER		(1 << 1)
#define	AUDIOHD_FLAG_HP			(1 << 2)
#define	AUDIOHD_FLAG_MONO		(1 << 3)

#define	AUDIOHD_MAX_MIXER		5
#define	AUDIOHD_MAX_PIN			4

#define	PORT_DAC		0
#define	PORT_ADC		1
#define	PORT_MAX		2
typedef enum {
	PLAY = 0,
	RECORD = 1,
	BEEP = 2,
	LOOPBACK = 3,
} path_type_t;

struct audiohd_path {
	wid_t			adda_wid;
	wid_t			beep_wid;

	wid_t			pin_wid[AUDIOHD_MAX_PINS];
	int			sum_selconn[AUDIOHD_MAX_PINS];
	int			mon_wid[AUDIOHD_MAX_PIN][AUDIOHD_MAX_MIXER];
	int			pin_nums;
	int			maxmixer[AUDIOHD_MAX_PINS];

	path_type_t		path_type;

	wid_t			mute_wid;
	int			mute_dir;
	wid_t			gain_wid;
	int			gain_dir;
	uint32_t		gain_bits;

	uint32_t		pin_outputs;
	uint8_t			tag;

	hda_codec_t		*codec;

	wid_t			sum_wid;

	audiohd_state_t		*statep;
};

typedef struct audiohd_port
{
	uint8_t			nchan;
	int			index;
	uint16_t		regoff;

	unsigned		nframes;
	size_t			bufsize;
	size_t			fragsize;
	uint64_t		count;
	int			curpos;

	uint_t			format;
	unsigned		sync_dir;

	ddi_dma_handle_t	samp_dmah;
	ddi_acc_handle_t	samp_acch;
	caddr_t			samp_kaddr;
	uint64_t		samp_paddr;

	ddi_dma_handle_t	bdl_dmah;
	ddi_acc_handle_t	bdl_acch;
	size_t			bdl_size;
	caddr_t			bdl_kaddr;
	uint64_t		bdl_paddr;

	audio_engine_t		*engine;
	audiohd_state_t		*statep;
}audiohd_port_t;

enum {
	CTL_VOLUME = 0,
	CTL_FRONT,
	CTL_SPEAKER,
	CTL_HEADPHONE,
	CTL_REAR,
	CTL_CENTER,
	CTL_SURROUND,
	CTL_LFE,
	CTL_IGAIN,
	CTL_LINEIN,
	CTL_MIC,
	CTL_CD,
	CTL_MONGAIN,
	CTL_MONSRC,
	CTL_RECSRC,
	CTL_BEEP,
	CTL_LOOP,

	/* this one must be last */
	CTL_MAX
};

typedef struct audiohd_ctrl
{
	audiohd_state_t		*statep;
	audio_ctrl_t		*ctrl;
	int			num;
	uint64_t		val;
} audiohd_ctrl_t;

struct audiohd_pin {
	audiohd_pin_t	*next;
	wid_t		wid;
	wid_t		mute_wid;	/* node used to mute this pin */
	int		mute_dir;	/* 1: input, 2: output */
	wid_t		gain_wid;	/* node for gain control */
	int		gain_dir;	/* _OUTPUT/_INPUT */
	uint32_t	gain_bits;

	uint8_t		vrefvalue;	/* value of VRef */

	uint32_t	cap;
	uint32_t	config;
	uint32_t	ctrl;
	uint32_t	assoc;
	uint32_t	seq;
	wid_t		adc_wid;
	wid_t		dac_wid;
	wid_t		beep_wid;
	int		no_phys_conn;
	enum audiohda_device_type	device;

	/*
	 * mg_dir, mg_gain, mg_wid are used to store the monitor gain control
	 * widget wid.
	 */
	int		mg_dir[AUDIOHD_MAX_CONN];
	int		mg_gain[AUDIOHD_MAX_CONN];
	int		mg_wid[AUDIOHD_MAX_CONN];
	int		num;
	int		finish;

};

typedef struct {
	ddi_dma_handle_t	ad_dmahdl;
	ddi_acc_handle_t	ad_acchdl;
	caddr_t			ad_vaddr;	/* virtual addr */
	uint64_t		ad_paddr;	/* physical addr */
	size_t			ad_req_sz;	/* required size of memory */
	size_t			ad_real_sz;	/* real size of memory */
} audiohd_dma_t;

struct hda_codec {
	uint8_t		index;		/* codec address */
	uint32_t	vid;		/* vendor id and device id */
	uint32_t	revid;		/* revision id */
	wid_t		wid_afg;	/* id of AFG */
	wid_t		first_wid;	/* wid of 1st subnode of AFG */
	wid_t		last_wid;	/* wid of the last subnode of AFG */
	int		nnodes;		/* # of subnodes of AFG */
	uint8_t		nistream;

	uint32_t	outamp_cap;
	uint32_t	inamp_cap;
	uint32_t	stream_format;
	uint32_t	pcm_format;

	audiohd_state_t		*statep;
	audiohd_codec_info_t	*codec_info;

	/* use wid as index to the array of widget pointers */
	audiohd_widget_t	*widget[AUDIOHD_MAX_WIDGET];

	audiohd_port_t		*port[AUDIOHD_PORT_MAX];
	uint8_t			portnum;
	audiohd_pin_t		*first_pin;
};

#define	AUDIOHD_MAX_ASSOC	15
struct audiohd_state {
	dev_info_t	*hda_dip;
	kstat_t		*hda_ksp;
	kmutex_t	hda_mutex;
	uint32_t	hda_flags;

	caddr_t			hda_reg_base;
	ddi_acc_handle_t	hda_pci_handle;
	ddi_acc_handle_t	hda_reg_handle;

	audiohd_dma_t	hda_dma_corb;
	audiohd_dma_t	hda_dma_rirb;

	uint8_t		hda_rirb_rp;		/* read pointer for rirb */
	uint16_t	hda_codec_mask;

	audio_dev_t	*adev;
	uint32_t	devid;

	int		hda_input_streams;	/* # of input stream */
	int		hda_output_streams;	/* # of output stream */
	int		hda_streams_nums;	/* # of stream */

	uint_t		hda_play_regbase;
	uint_t		hda_record_regbase;

	uint_t		hda_play_stag;		/* tag of playback stream */
	uint_t		hda_record_stag;	/* tag of record stream */
	uint_t		hda_play_lgain;		/* left gain for playback */
	uint_t		hda_play_rgain;		/* right gain for playback */

	/*
	 * Now, for the time being, we add some fields
	 * for parsing codec topology
	 */
	hda_codec_t	*codec[AUDIOHD_CODEC_MAX];

	/*
	 * Suspend/Resume used fields
	 */
	boolean_t	suspended;

	audiohd_path_t	*path[AUDIOHD_PORT_MAX];
	uint8_t		pathnum;
	audiohd_port_t	*port[PORT_MAX];
	uint8_t		pchan;
	uint8_t		rchan;

	uint64_t	inmask;

	uint_t		hda_out_ports;
	uint_t		in_port;

	/* Higher sample/rate */
	uint32_t	sample_rate;
	uint32_t	sample_bit_depth;
	uint8_t		sample_packed_bytes;

	/*
	 * Controls
	 */
	audiohd_ctrl_t	ctrls[CTL_MAX];
	boolean_t	monitor_supported;
	boolean_t	loopback_supported;

	/* for multichannel */
	uint8_t			chann[AUDIOHD_MAX_ASSOC];
	uint8_t			assoc;

};

struct audiohd_codec_info {
	uint32_t	devid;
	const char	*buf;
	uint32_t	flags;
};

/*
 * Operation for high definition audio control system bus
 * interface registers
 */
#define	AUDIOHD_REG_GET8(reg)	\
	ddi_get8(statep->hda_reg_handle, \
	(void *)((char *)statep->hda_reg_base + (reg)))

#define	AUDIOHD_REG_GET16(reg)	\
	ddi_get16(statep->hda_reg_handle, \
	(void *)((char *)statep->hda_reg_base + (reg)))

#define	AUDIOHD_REG_GET32(reg)	\
	ddi_get32(statep->hda_reg_handle, \
	(void *)((char *)statep->hda_reg_base + (reg)))

#define	AUDIOHD_REG_GET64(reg)	\
	ddi_get64(statep->hda_reg_handle, \
	(void *)((char *)statep->hda_reg_base + (reg)))

#define	AUDIOHD_REG_SET8(reg, val)	\
	ddi_put8(statep->hda_reg_handle, \
	(void *)((char *)statep->hda_reg_base + (reg)), (val))

#define	AUDIOHD_REG_SET16(reg, val)	\
	ddi_put16(statep->hda_reg_handle, \
	(void *)((char *)statep->hda_reg_base + (reg)), (val))

#define	AUDIOHD_REG_SET32(reg, val)	\
	ddi_put32(statep->hda_reg_handle, \
	(void *)((char *)statep->hda_reg_base + (reg)), (val))

#define	AUDIOHD_REG_SET64(reg, val)	\
	ddi_put64(statep->hda_reg_handle, \
	(void *)((char *)statep->hda_reg_base + (reg)), (val))


/*
 * enable a pin widget to input
 */
#define	AUDIOHD_ENABLE_PIN_IN(statep, caddr, wid) \
{ \
	(void) audioha_codec_verb_get(statep, caddr, wid, \
	    AUDIOHDC_VERB_SET_PIN_CTRL, AUDIOHDC_PIN_CONTROL_IN_ENABLE | 4); \
}

/*
 * disable input pin
 */
#define	AUDIOHD_DISABLE_PIN_IN(statep, caddr, wid) \
{ \
	uint32_t	lTmp; \
\
	lTmp = audioha_codec_verb_get(statep, caddr, wid, \
	    AUDIOHDC_VERB_GET_PIN_CTRL, 0); \
	if (lTmp == AUDIOHD_CODEC_FAILURE) \
		return (DDI_FAILURE); \
	lTmp = audioha_codec_verb_get(statep, caddr, wid, \
	    AUDIOHDC_VERB_SET_PIN_CTRL, \
	    (lTmp & ~AUDIOHDC_PIN_CONTROL_IN_ENABLE)); \
	if (lTmp == AUDIOHD_CODEC_FAILURE) \
		return (DDI_FAILURE); \
}

/*
 * unmute an output pin
 */
#define	AUDIOHD_NODE_UNMUTE_OUT(statep, caddr, wid) \
{ \
	if (audioha_codec_4bit_verb_get(statep, \
	    caddr, wid, AUDIOHDC_VERB_SET_AMP_MUTE, \
	    AUDIOHDC_AMP_SET_LR_OUTPUT | AUDIOHDC_GAIN_MAX) == \
	    AUDIOHD_CODEC_FAILURE) \
		return (DDI_FAILURE); \
}

/*
 * check volume adjust value of 2 channels control
 */
#define	AUDIOHD_CHECK_2CHANNELS_VOLUME(value) \
{ \
	if ((value) & ~0xffff) \
		return (EINVAL); \
	if ((((value) & 0xff00) >> 8) > 100 || \
	    ((value) & 0xff) > 100) \
		return (EINVAL); \
}

/*
 * check volume adjust value of mono channel control
 */
#define	AUDIOHD_CHECK_CHANNEL_VOLUME(value) \
{ \
	if ((value) & ~0xff) \
		return (EINVAL); \
	if (((value) & 0xff) > 100) \
		return (EINVAL); \
}

#ifdef __cplusplus
}
#endif

/* Warlock annotation */
_NOTE(DATA_READABLE_WITHOUT_LOCK(audiohd_ctrl::statep))
_NOTE(DATA_READABLE_WITHOUT_LOCK(audiohd_state::inmask))
_NOTE(DATA_READABLE_WITHOUT_LOCK(audiohd_state::adev))
_NOTE(DATA_READABLE_WITHOUT_LOCK(audiohd_state::sample_bit_depth))
_NOTE(DATA_READABLE_WITHOUT_LOCK(audiohd_state::sample_rate))
_NOTE(READ_ONLY_DATA(audiohd_state::hda_reg_handle))
_NOTE(DATA_READABLE_WITHOUT_LOCK(audiohd_widget::codec))
_NOTE(DATA_READABLE_WITHOUT_LOCK(audiohd_widget::wid_wid))
_NOTE(DATA_READABLE_WITHOUT_LOCK(hda_codec::index))
_NOTE(DATA_READABLE_WITHOUT_LOCK(hda_codec::statep))
_NOTE(DATA_READABLE_WITHOUT_LOCK(hda_codec::vid))
_NOTE(DATA_READABLE_WITHOUT_LOCK(audiohd_port::nchan))
_NOTE(DATA_READABLE_WITHOUT_LOCK(audiohd_port::statep))
_NOTE(DATA_READABLE_WITHOUT_LOCK(audiohd_port::sync_dir))

#endif	/* _SYS_AUDIOHD_IMPL_H_ */
