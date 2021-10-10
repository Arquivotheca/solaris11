/*
 * Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef __LINUX_VIDEODEV2_H
#define	__LINUX_VIDEODEV2_H


#ifdef	__cplusplus
extern "C" {
#endif

/*
 *	Video for Linux Two
 *
 *	Header file for v4l or V4L2 drivers and applications
 * with public API.
 * All kernel-specific stuff were moved to media/v4l2-dev.h, so
 * no #if __KERNEL tests are allowed here
 *
 *	See http://linuxtv.org for more info
 *
 *	Author: Bill Dirks <bdirks@pacbell.net>
 *		Justin Schoeman
 *		et al.
 */

#include <sys/ioccom.h>

/*
 * Common stuff for both V4L1 and V4L2
 * Moved from videodev.h
 */
#define	VIDEO_MAX_FRAME		32

#define	VID_TYPE_CAPTURE	1	/* Can capture */
#define	VID_TYPE_TUNER		2	/* Can tune */
#define	VID_TYPE_TELETEXT	4	/* Does teletext */
#define	VID_TYPE_OVERLAY	8	/* Overlay onto frame buffer */
#define	VID_TYPE_CHROMAKEY	16	/* Overlay by chromakey */
#define	VID_TYPE_CLIPPING	32	/* Can clip */
#define	VID_TYPE_FRAMERAM	64	/* Uses the frame buffer memory */
#define	VID_TYPE_SCALES		128	/* Scalable */
#define	VID_TYPE_MONOCHROME	256	/* Monochrome only */
#define	VID_TYPE_SUBCAPTURE	512	/* Can capture subareas of the image */
#define	VID_TYPE_MPEG_DECODER	1024	/* Can decode MPEG streams */
#define	VID_TYPE_MPEG_ENCODER	2048	/* Can encode MPEG streams */
#define	VID_TYPE_MJPEG_DECODER	4096	/* Can decode MJPEG streams */
#define	VID_TYPE_MJPEG_ENCODER	8192	/* Can encode MJPEG streams */

/*
 *	M I S C E L L A N E O U S
 */

/*  Four-character-code (FOURCC) */
#define	v4l2_fourcc(a, b, c, d) \
	(((uint32_t)(a)<<0) | ((uint32_t)(b)<<8) | \
	((uint32_t)(c)<<16)|((uint32_t)(d)<<24))

/*
 *	E N U M S
 */
enum v4l2_field {

	/*
	 * driver can choose from none, top, bottom, interlaced
	 * depending on whatever it thinks is approximate ...
	 */
	V4L2_FIELD_ANY		= 0,
	V4L2_FIELD_NONE		= 1, /* this device has no fields ... */
	V4L2_FIELD_TOP		= 2, /* top field only */
	V4L2_FIELD_BOTTOM	= 3, /* bottom field only */
	V4L2_FIELD_INTERLACED	= 4, /* both fields interlaced */

	/* both fields sequential into one buffer, top-bottom order */
	V4L2_FIELD_SEQ_TB	= 5,

	V4L2_FIELD_SEQ_BT	= 6, /* same as above + bottom-top order */

	/* both fields alternating into	separate buffers */
	V4L2_FIELD_ALTERNATE	= 7
};
#define	V4L2_FIELD_HAS_TOP(field)	 \
	((field) == V4L2_FIELD_TOP	|| \
	(field) == V4L2_FIELD_INTERLACED || \
	(field) == V4L2_FIELD_SEQ_TB	|| \
	(field) == V4L2_FIELD_SEQ_BT)
#define	V4L2_FIELD_HAS_BOTTOM(field)	 \
	((field) == V4L2_FIELD_BOTTOM	|| \
	(field) == V4L2_FIELD_INTERLACED || \
	(field) == V4L2_FIELD_SEQ_TB	|| \
	(field) == V4L2_FIELD_SEQ_BT)
#define	V4L2_FIELD_HAS_BOTH(field)	 \
	((field) == V4L2_FIELD_INTERLACED || \
	(field) == V4L2_FIELD_SEQ_TB	|| \
	(field) == V4L2_FIELD_SEQ_BT)

enum v4l2_buf_type {
	V4L2_BUF_TYPE_VIDEO_CAPTURE	 = 1,
	V4L2_BUF_TYPE_VIDEO_OUTPUT	 = 2,
	V4L2_BUF_TYPE_VIDEO_OVERLAY	 = 3,
	V4L2_BUF_TYPE_VBI_CAPTURE	 = 4,
	V4L2_BUF_TYPE_VBI_OUTPUT	 = 5,
#if 1
	/* Experimental Sliced VBI */
	V4L2_BUF_TYPE_SLICED_VBI_CAPTURE = 6,
	V4L2_BUF_TYPE_SLICED_VBI_OUTPUT  = 7,
#endif
	V4L2_BUF_TYPE_PRIVATE		 = 0x80
};

enum v4l2_ctrl_type {
	V4L2_CTRL_TYPE_INTEGER		= 1,
	V4L2_CTRL_TYPE_BOOLEAN		= 2,
	V4L2_CTRL_TYPE_MENU		= 3,
	V4L2_CTRL_TYPE_BUTTON		= 4,
	V4L2_CTRL_TYPE_INTEGER64	= 5,
	V4L2_CTRL_TYPE_CTRL_CLASS	= 6
};

enum v4l2_tuner_type {
	V4L2_TUNER_RADIO	= 1,
	V4L2_TUNER_ANALOG_TV	= 2,
	V4L2_TUNER_DIGITAL_TV	= 3
};

enum v4l2_memory {
	V4L2_MEMORY_MMAP	= 1,
	V4L2_MEMORY_USERPTR	= 2,
	V4L2_MEMORY_OVERLAY	= 3
};

/* see also http: vektor.theorem.ca/graphics/ycbcr/ */
enum v4l2_colorspace {
	/* ITU-R 601 -- broadcast NTSC/PAL */
	V4L2_COLORSPACE_SMPTE170M	= 1,

	/* 1125-Line (US) HDTV */
	V4L2_COLORSPACE_SMPTE240M	= 2,

	/* HD and modern captures. */
	V4L2_COLORSPACE_REC709		= 3,

	/* broken BT878 extents (601, luma range 16-253 instead of 16-235) */
	V4L2_COLORSPACE_BT878		= 4,

	/* These should be useful.  Assume 601 extents. */
	V4L2_COLORSPACE_470_SYSTEM_M  = 5,
	V4L2_COLORSPACE_470_SYSTEM_BG = 6,

	/*
	 * I know there will be cameras that send this.  So, this is
	 * unspecified chromaticities and full 0-255 on each of the
	 * Y'CbCr components
	 */
	V4L2_COLORSPACE_JPEG		= 7,

	/* For RGB colourspaces, this is probably a good start. */
	V4L2_COLORSPACE_SRGB		= 8
};

enum v4l2_priority {
	V4L2_PRIORITY_UNSET	  = 0,	/* not initialized */
	V4L2_PRIORITY_BACKGROUND  = 1,
	V4L2_PRIORITY_INTERACTIVE = 2,
	V4L2_PRIORITY_RECORD	  = 3,
	V4L2_PRIORITY_DEFAULT	  = V4L2_PRIORITY_INTERACTIVE
};

struct v4l2_rect {
	int32_t   left;
	int32_t   top;
	int32_t   width;
	int32_t   height;
};

struct v4l2_fract {
	uint32_t   numerator;
	uint32_t   denominator;
};

/*
 *	D R I V E R   C A P A B I L I T I E S
 */
struct v4l2_capability
{
	uint8_t		driver[16];	/* i.e. "bttv" */
	uint8_t		card[32];	/* i.e. "Hauppauge WinTV" */
	uint8_t		bus_info[32];	/* "PCI:" + pci_name(pci_dev) */
	uint32_t	version;	/* should use KERNEL_VERSION() */
	uint32_t	capabilities;	/* Device capabilities */
	uint32_t	reserved[4];
};

/* Values for 'capabilities' field */

/* Is a video capture device */
#define	V4L2_CAP_VIDEO_CAPTURE		0x00000001

/* Is a video output device */
#define	V4L2_CAP_VIDEO_OUTPUT		0x00000002
#define	V4L2_CAP_VIDEO_OVERLAY		0x00000004  /* Can do video overlay */

/* Is a raw VBI capture device */
#define	V4L2_CAP_VBI_CAPTURE		0x00000010

/* Is a raw VBI output device */
#define	V4L2_CAP_VBI_OUTPUT		0x00000020
#if 1

/* Is a sliced VBI capture device */
#define	V4L2_CAP_SLICED_VBI_CAPTURE	0x00000040

/* Is a sliced VBI output device */
#define	V4L2_CAP_SLICED_VBI_OUTPUT	0x00000080
#endif
#define	V4L2_CAP_RDS_CAPTURE		0x00000100  /* RDS data capture */

#define	V4L2_CAP_TUNER			0x00010000  /* has a tuner */
#define	V4L2_CAP_AUDIO			0x00020000  /* has audio support */
#define	V4L2_CAP_RADIO			0x00040000  /* is a radio device */

#define	V4L2_CAP_READWRITE		0x01000000  /* read/write systemcalls */
#define	V4L2_CAP_ASYNCIO		0x02000000  /* async I/O */
#define	V4L2_CAP_STREAMING		0x04000000  /* streaming I/O ioctls */

/*
 *	V I D E O   I M A G E	F O R M A T
 */
struct v4l2_pix_format
{
	uint32_t		width;
	uint32_t		height;
	uint32_t		pixelformat;
	enum			v4l2_field	field;
	uint32_t		bytesperline; /* for padding, zero if unused */
	uint32_t		sizeimage;
	enum v4l2_colorspace	colorspace;
	uint32_t		priv; /* private data, depends on pixelformat */
};

/*	Pixel format		FOURCC			depth Description */
#define	V4L2_PIX_FMT_RGB332  v4l2_fourcc('R', 'G', 'B', '1') /*  8 RGB-3-3-2 */
#define	V4L2_PIX_FMT_RGB555  v4l2_fourcc('R', 'G', 'B', 'O') /* 16 RGB-5-5-5 */
#define	V4L2_PIX_FMT_RGB565  v4l2_fourcc('R', 'G', 'B', 'P') /* 16 RGB-5-6-5 */

/* 16 RGB-5-5-5 BE */
#define	V4L2_PIX_FMT_RGB555X v4l2_fourcc('R', 'G', 'B', 'Q')

/* 16 RGB-5-6-5 BE */
#define	V4L2_PIX_FMT_RGB565X v4l2_fourcc('R', 'G', 'B', 'R')

#define	V4L2_PIX_FMT_BGR24 v4l2_fourcc('B', 'G', 'R', '3') /* 24 BGR-8-8-8 */
#define	V4L2_PIX_FMT_RGB24 v4l2_fourcc('R', 'G', 'B', '3') /* 24 RGB-8-8-8 */
#define	V4L2_PIX_FMT_BGR32 v4l2_fourcc('B', 'G', 'R', '4') /* 32 BGR-8-8-8-8 */
#define	V4L2_PIX_FMT_RGB32 v4l2_fourcc('R', 'G', 'B', '4') /* 32 RGB-8-8-8-8 */
#define	V4L2_PIX_FMT_GREY  v4l2_fourcc('G', 'R', 'E', 'Y') /*  8 Greyscale */
#define	V4L2_PIX_FMT_YVU410 v4l2_fourcc('Y', 'V', 'U', '9') /*	9 YVU 4:1:0 */
#define	V4L2_PIX_FMT_YVU420 v4l2_fourcc('Y', 'V', '1', '2') /* 12 YVU 4:2:0 */
#define	V4L2_PIX_FMT_YUYV v4l2_fourcc('Y', 'U', 'Y', 'V') /* 16 YUV 4:2:2 */
#define	V4L2_PIX_FMT_UYVY v4l2_fourcc('U', 'Y', 'V', 'Y') /* 16 YUV 4:2:2 */

/* 16 YVU422 planar */
#define	V4L2_PIX_FMT_YUV422P v4l2_fourcc('4', '2', '2', 'P')

/* 16 YVU411 planar */
#define	V4L2_PIX_FMT_YUV411P v4l2_fourcc('4', '1', '1', 'P')

#define	V4L2_PIX_FMT_Y41P v4l2_fourcc('Y', '4', '1', 'P') /* 12 YUV 4:1:1 */

/* two planes -- one Y, one Cr + Cb interleaved  */
#define	V4L2_PIX_FMT_NV12 v4l2_fourcc('N', 'V', '1', '2') /* 12 Y/CbCr 4:2:0 */
#define	V4L2_PIX_FMT_NV21 v4l2_fourcc('N', 'V', '2', '1') /* 12 Y/CrCb 4:2:0 */

/* The following formats are not defined in the V4L2 specification */
#define	V4L2_PIX_FMT_YUV410 v4l2_fourcc('Y', 'U', 'V', '9') /* 9 YUV 4:1:0 */
#define	V4L2_PIX_FMT_YUV420 v4l2_fourcc('Y', 'U', '1', '2') /* 12  YUV 4:2:0 */
#define	V4L2_PIX_FMT_YYUV v4l2_fourcc('Y', 'Y', 'U', 'V') /* 16  YUV 4:2:2 */
#define	V4L2_PIX_FMT_HI240 v4l2_fourcc('H', 'I', '2', '4') /* 8 8-bit colo */

/* 8  YUV 4:1:1 16x16 macroblocks */
#define	V4L2_PIX_FMT_HM12    v4l2_fourcc('H', 'M', '1', '2')

/* see www.siliconimaging.com/RGB%20Bayer.htm */

/* 8 BGBG.. GRGR.. */
#define	V4L2_PIX_FMT_SBGGR8  v4l2_fourcc('B', 'A', '8', '1')

/* compressed formats */

/* Motion-JPEG */
#define	V4L2_PIX_FMT_MJPEG	v4l2_fourcc('M', 'J', 'P', 'G')
#define	V4L2_PIX_FMT_JPEG	v4l2_fourcc('J', 'P', 'E', 'G') /* JFIF JPEG */
#define	V4L2_PIX_FMT_DV		v4l2_fourcc('d', 'v', 's', 'd') /* 1394 */
#define	V4L2_PIX_FMT_MPEG	v4l2_fourcc('M', 'P', 'E', 'G') /* MPEG-1/2/4 */

/*  Vendor-specific formats   */

/* Winnov hw compress */
#define	V4L2_PIX_FMT_WNVA	v4l2_fourcc('W', 'N', 'V', 'A')

/* SN9C10x compression */
#define	V4L2_PIX_FMT_SN9C10X	v4l2_fourcc('S', '9', '1', '0')

/* pwc older webcam */
#define	V4L2_PIX_FMT_PWC1	v4l2_fourcc('P', 'W', 'C', '1')

/* pwc newer webcam */
#define	V4L2_PIX_FMT_PWC2	v4l2_fourcc('P', 'W', 'C', '2')

/* ET61X251 compression */
#define	V4L2_PIX_FMT_ET61X251 v4l2_fourcc('E', '6', '2', '5')

/*
 *	F O R M A T   E N U M E R A T I O N
 */
struct v4l2_fmtdesc
{
	uint32_t		index;			/* Format number */
	enum			v4l2_buf_type  type;	/* buffer type */
	uint32_t		flags;
	char			description[32];   /* Description string */
	uint32_t		pixelformat;	   /* Format fourcc	 */
	uint32_t		reserved[4];
};

#define	V4L2_FMT_FLAG_COMPRESSED 0x0001

/*
 *	T I M E C O D E
 */
struct v4l2_timecode
{
	uint32_t	type;
	uint32_t	flags;
	uint8_t	frames;
	uint8_t	seconds;
	uint8_t	minutes;
	uint8_t	hours;
	uint8_t	userbits[4];
};

/*  Type  */
#define	V4L2_TC_TYPE_24FPS		1
#define	V4L2_TC_TYPE_25FPS		2
#define	V4L2_TC_TYPE_30FPS		3
#define	V4L2_TC_TYPE_50FPS		4
#define	V4L2_TC_TYPE_60FPS		5

/*  Flags  */
#define	V4L2_TC_FLAG_DROPFRAME		0x0001 /* "drop-frame" mode */
#define	V4L2_TC_FLAG_COLORFRAME		0x0002
#define	V4L2_TC_USERBITS_field		0x000C
#define	V4L2_TC_USERBITS_USERDEFINED	0x0000
#define	V4L2_TC_USERBITS_8BITCHARS	0x0008
/* The above is based on SMPTE timecodes */

#ifdef __KERNEL__
/*
 *	M P E G   C O M P R E S S I O N   P A R A M E T E R S
 *
 *  ### WARNING: This experimental MPEG compression API is obsolete.
 *  ###		It is replaced by the MPEG controls API.
 *  ###		This old API will disappear in the near future!
 *
 */
enum v4l2_bitrate_mode {
	V4L2_BITRATE_NONE = 0,	/* not specified */
	V4L2_BITRATE_CBR,	/* constant bitrate */
	V4L2_BITRATE_VBR	/* variable bitrate */
};
struct v4l2_bitrate {
	/* rates are specified in kbit/sec */
	enum v4l2_bitrate_mode	mode;
	uint32_t			min;
	uint32_t			target;  /* use this one for CBR */
	uint32_t			max;
};

enum v4l2_mpeg_streamtype {
	V4L2_MPEG_SS_1,		/* MPEG-1 system stream */
	V4L2_MPEG_PS_2,		/* MPEG-2 program stream */
	V4L2_MPEG_TS_2,		/* MPEG-2 transport stream */

	/* MPEG-2 program stream with DVD header fixups */
	V4L2_MPEG_PS_DVD
};
enum v4l2_mpeg_audiotype {
	V4L2_MPEG_AU_2_I,	/* MPEG-2 layer 1 */
	V4L2_MPEG_AU_2_II,	/* MPEG-2 layer 2 */
	V4L2_MPEG_AU_2_III,	/* MPEG-2 layer 3 */
	V4L2_MPEG_AC3,		/* AC3 */
	V4L2_MPEG_LPCM		/* LPCM */
};
enum v4l2_mpeg_videotype {
	V4L2_MPEG_VI_1,		/* MPEG-1 */
	V4L2_MPEG_VI_2		/* MPEG-2 */
};
enum v4l2_mpeg_aspectratio {
	V4L2_MPEG_ASPECT_SQUARE	= 1,	/* square pixel */
	V4L2_MPEG_ASPECT_4_3	= 2,	/* 4 : 3 */
	V4L2_MPEG_ASPECT_16_9	= 3,	/* 16 : 9 */
	V4L2_MPEG_ASPECT_1_221	= 4	/* 1 : 2,21 */
};

struct v4l2_mpeg_compression {
	/* general */
	enum v4l2_mpeg_streamtype	st_type;
	struct v4l2_bitrate		st_bitrate;

	/* transport streams */
	uint16_t				ts_pid_pmt;
	uint16_t				ts_pid_audio;
	uint16_t				ts_pid_video;
	uint16_t				ts_pid_pcr;

	/* program stream */
	uint16_t				ps_size;
	uint16_t				reserved_1;    /* align */

	/* audio */
	enum v4l2_mpeg_audiotype	au_type;
	struct v4l2_bitrate		au_bitrate;
	uint32_t			au_sample_rate;
	uint8_t 			au_pesid;
	uint8_t 			reserved_2[3]; /* align */

	/* video */
	enum v4l2_mpeg_videotype	vi_type;
	enum v4l2_mpeg_aspectratio	vi_aspect_ratio;
	struct v4l2_bitrate		vi_bitrate;
	uint32_t			vi_frame_rate;
	uint16_t			vi_frames_per_gop;
	uint16_t			vi_bframes_count;
	uint8_t 			vi_pesid;
	uint8_t 			reserved_3[3]; /* align */

	/* misc flags */
	uint32_t			closed_gops:1;
	uint32_t			pulldown:1;
	uint32_t			reserved_4:30; /* align */

	/* I don't expect the above being perfect yet ;) */
	uint32_t				reserved_5[8];
};
#endif

struct v4l2_jpegcompression
{
	int quality;

	/* Number of APP segment to be written, must be 0..15 */
	int  APPn;

	int  APP_len;		/* Length of data in JPEG APPn segment */
	char APP_data[60];	/* Data in the JPEG APPn segment. */

	int  COM_len;		/* Length of data in JPEG COM segment */
	char COM_data[60];	/* Data in JPEG COM segment */

	/*
	 * Which markers should go into the JPEG output. Unless you exactly
	 * know what you do, leave them untouched. Inluding less markers will
	 * make the resulting code smaller, but there will be fewer
	 * aplications which can read it. The presence of the APP and COM
	 * marker is influenced by APP_len and COM_len
	 * ONLY, not by this property!
	 */
	uint32_t jpeg_markers;

#define	V4L2_JPEG_MARKER_DHT (1<<3)    /* Define Huffman Tables */
#define	V4L2_JPEG_MARKER_DQT (1<<4)    /* Define Quantization Tables */
#define	V4L2_JPEG_MARKER_DRI (1<<5)    /* Define Restart Interval */
#define	V4L2_JPEG_MARKER_COM (1<<6)    /* Comment segment */

/* App segment, driver will allways use APP0 */
#define	V4L2_JPEG_MARKER_APP (1<<7)
};

/*
 *	M E M O R Y - M A P P I N G   B U F F E R S
 */
struct v4l2_requestbuffers
{
	uint32_t			count;
	enum v4l2_buf_type	type;
	enum v4l2_memory	memory;
	uint32_t			reserved[2];
};

/*
 * this structure is defined to replace timeval structure which includes
 * "long" type elements
 */
struct v4l2_timeval {
	uint64_t	tv_sec;	/* seconds */
	uint64_t	tv_usec; /* and microseconds */
};

/* this structure is now 64bits alignment */
struct v4l2_buffer
{
	uint32_t			index;
	uint32_t			bytesused;
	enum v4l2_buf_type	type;
	enum v4l2_field		field;
	struct v4l2_timeval		timestamp;
	struct v4l2_timecode	timecode;
	uint32_t			flags;
	uint32_t			sequence;
	union {
		uint32_t	   offset;
		uint64_t   userptr;
	} m;
	/* memory location */
	enum v4l2_memory	memory;
	uint32_t			length;
	uint32_t			input;
	uint32_t			reserved;
};

/*  Flags for 'flags' field */
#define	V4L2_BUF_FLAG_MAPPED	0x0001	/* Buffer is mapped (flag) */
#define	V4L2_BUF_FLAG_QUEUED	0x0002	/* Buffer is queued for processing */
#define	V4L2_BUF_FLAG_DONE	0x0004	/* Buffer is ready */
#define	V4L2_BUF_FLAG_KEYFRAME	0x0008	/* Image is a keyframe (I-frame) */
#define	V4L2_BUF_FLAG_PFRAME	0x0010	/* Image is a P-frame */
#define	V4L2_BUF_FLAG_BFRAME	0x0020	/* Image is a B-frame */
#define	V4L2_BUF_FLAG_TIMECODE	0x0100	/* timecode field is valid */
#define	V4L2_BUF_FLAG_INPUT	0x0200	/* input field is valid */

/*
 *	O V E R L A Y	P R E V I E W
 */
struct v4l2_framebuffer
{
	uint32_t			capability;
	uint32_t			flags;
/*
 * FIXME: in theory we should pass something like PCI device + memory
 * region + offset instead of some physical address
 */
	void*			base;
	struct v4l2_pix_format	fmt;
};
/*  Flags for the 'capability' field. Read only */
#define	V4L2_FBUF_CAP_EXTERNOVERLAY	0x0001
#define	V4L2_FBUF_CAP_CHROMAKEY		0x0002
#define	V4L2_FBUF_CAP_LIST_CLIPPING	0x0004
#define	V4L2_FBUF_CAP_BITMAP_CLIPPING	0x0008
/*  Flags for the 'flags' field. */
#define	V4L2_FBUF_FLAG_PRIMARY		0x0001
#define	V4L2_FBUF_FLAG_OVERLAY		0x0002
#define	V4L2_FBUF_FLAG_CHROMAKEY	0x0004

struct v4l2_clip
{
	struct v4l2_rect	c;
	struct v4l2_clip	 *next; /* __user */
};

struct v4l2_window
{
	struct v4l2_rect	w;
	enum v4l2_field 	field;
	uint32_t			chromakey;
	struct v4l2_clip	 *clips; /* __user */
	uint32_t			clipcount;
	void			 *bitmap; /* __user */
};

/*
 *	C A P T U R E	P A R A M E T E R S
 */
struct v4l2_captureparm
{
	uint32_t	capability;	  /*  Supported modes */
	uint32_t	capturemode;	  /*  Current mode */

	/*  Time per frame in .1us units */
	struct		v4l2_fract  timeperframe;
	uint32_t	extendedmode;  /*  Driver-specific extensions */
	uint32_t	readbuffers;   /*  # of buffers for read */
	uint32_t	reserved[4];
};

/*  Flags for 'capability' and 'capturemode' fields */
#define	V4L2_MODE_HIGHQUALITY	0x0001	/*  High quality imaging mode */
#define	V4L2_CAP_TIMEPERFRAME	0x1000	/*  timeperframe field is supported */

struct v4l2_outputparm
{
	uint32_t	capability;	 /*  Supported modes */
	uint32_t	outputmode;	 /*  Current mode */

	/*  Time per frame in seconds */
	struct		v4l2_fract  timeperframe;
	uint32_t	extendedmode; /*  Driver-specific extensions */
	uint32_t	writebuffers; /*	# of buffers for write */
	uint32_t	reserved[4];
};

/*
 *	I N P U T   I M A G E	C R O P P I N G
 */
struct v4l2_cropcap {
	enum v4l2_buf_type	type;
	struct v4l2_rect	bounds;
	struct v4l2_rect	defrect;
	struct v4l2_fract	pixelaspect;
};

struct v4l2_crop {
	enum v4l2_buf_type	type;
	struct v4l2_rect	c;
};

/*
 *	A N A L O G   V I D E O   S T A N D A R D
 */

typedef uint64_t v4l2_std_id;

/* one bit for each */
#define	V4L2_STD_PAL_B		((v4l2_std_id)0x00000001)
#define	V4L2_STD_PAL_B1 	((v4l2_std_id)0x00000002)
#define	V4L2_STD_PAL_G		((v4l2_std_id)0x00000004)
#define	V4L2_STD_PAL_H		((v4l2_std_id)0x00000008)
#define	V4L2_STD_PAL_I		((v4l2_std_id)0x00000010)
#define	V4L2_STD_PAL_D		((v4l2_std_id)0x00000020)
#define	V4L2_STD_PAL_D1 	((v4l2_std_id)0x00000040)
#define	V4L2_STD_PAL_K		((v4l2_std_id)0x00000080)

#define	V4L2_STD_PAL_M		((v4l2_std_id)0x00000100)
#define	V4L2_STD_PAL_N		((v4l2_std_id)0x00000200)
#define	V4L2_STD_PAL_Nc 	((v4l2_std_id)0x00000400)
#define	V4L2_STD_PAL_60 	((v4l2_std_id)0x00000800)

#define	V4L2_STD_NTSC_M 	((v4l2_std_id)0x00001000)
#define	V4L2_STD_NTSC_M_JP	((v4l2_std_id)0x00002000)
#define	V4L2_STD_NTSC_443	((v4l2_std_id)0x00004000)
#define	V4L2_STD_NTSC_M_KR	((v4l2_std_id)0x00008000)

#define	V4L2_STD_SECAM_B	((v4l2_std_id)0x00010000)
#define	V4L2_STD_SECAM_D	((v4l2_std_id)0x00020000)
#define	V4L2_STD_SECAM_G	((v4l2_std_id)0x00040000)
#define	V4L2_STD_SECAM_H	((v4l2_std_id)0x00080000)
#define	V4L2_STD_SECAM_K	((v4l2_std_id)0x00100000)
#define	V4L2_STD_SECAM_K1	((v4l2_std_id)0x00200000)
#define	V4L2_STD_SECAM_L	((v4l2_std_id)0x00400000)
#define	V4L2_STD_SECAM_LC	((v4l2_std_id)0x00800000)

/* ATSC/HDTV */
#define	V4L2_STD_ATSC_8_VSB	((v4l2_std_id)0x01000000)
#define	V4L2_STD_ATSC_16_VSB	((v4l2_std_id)0x02000000)

/* some merged standards */
#define	V4L2_STD_MN \
	(V4L2_STD_PAL_M|V4L2_STD_PAL_N|V4L2_STD_PAL_Nc|V4L2_STD_NTSC)
#define	V4L2_STD_B \
	(V4L2_STD_PAL_B|V4L2_STD_PAL_B1|V4L2_STD_SECAM_B)
#define	V4L2_STD_GH \
	(V4L2_STD_PAL_G|V4L2_STD_PAL_H|V4L2_STD_SECAM_G|V4L2_STD_SECAM_H)
#define	V4L2_STD_DK	(V4L2_STD_PAL_DK|V4L2_STD_SECAM_DK)

/* some common needed stuff */
#define	V4L2_STD_PAL_BG		(V4L2_STD_PAL_B		|\
				V4L2_STD_PAL_B1	|\
				V4L2_STD_PAL_G)
#define	V4L2_STD_PAL_DK		(V4L2_STD_PAL_D		|\
				V4L2_STD_PAL_D1	|\
				V4L2_STD_PAL_K)
#define	V4L2_STD_PAL		(V4L2_STD_PAL_BG	|\
				V4L2_STD_PAL_DK	|\
				V4L2_STD_PAL_H		|\
				V4L2_STD_PAL_I)
#define	V4L2_STD_NTSC		(V4L2_STD_NTSC_M	|\
				V4L2_STD_NTSC_M_JP	|\
				V4L2_STD_NTSC_M_KR)
#define	V4L2_STD_SECAM_DK	(V4L2_STD_SECAM_D	|\
				V4L2_STD_SECAM_K	|\
				V4L2_STD_SECAM_K1)
#define	V4L2_STD_SECAM		(V4L2_STD_SECAM_B	|\
				V4L2_STD_SECAM_G	|\
				V4L2_STD_SECAM_H	|\
				V4L2_STD_SECAM_DK	|\
				V4L2_STD_SECAM_L	|\
				V4L2_STD_SECAM_LC)

#define	V4L2_STD_525_60		(V4L2_STD_PAL_M		|\
				V4L2_STD_PAL_60	|\
				V4L2_STD_NTSC		|\
				V4L2_STD_NTSC_443)
#define	V4L2_STD_625_50		(V4L2_STD_PAL		|\
				V4L2_STD_PAL_N		|\
				V4L2_STD_PAL_Nc	|\
				V4L2_STD_SECAM)
#define	V4L2_STD_ATSC		(V4L2_STD_ATSC_8_VSB	|\
				V4L2_STD_ATSC_16_VSB)

#define	V4L2_STD_UNKNOWN	0
#define	V4L2_STD_ALL		(V4L2_STD_525_60	|\
				V4L2_STD_625_50)

/*
 * On amd64, use pack(4) to make structures match the x86 alignment rules.
 */
#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

struct v4l2_standard
{
	uint32_t		index;
	v4l2_std_id		id;
	uint8_t			name[24];
	struct v4l2_fract	frameperiod; /* Frames, not fields */
	uint32_t		framelines;
	uint32_t		reserved[4];
};

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

/*
 *	V I D E O   I N P U T S
 */
struct v4l2_input
{
	uint8_t		name[32];	/*  Label */
	uint32_t	index;		/*  Which input */
	uint32_t	type;		/*  Type of input */
	uint32_t	audioset;	/*  Associated audios (bitfield) */
	uint32_t	tuner;		/*  Associated tuner */
	v4l2_std_id	std;
	uint32_t	status;

	/*
	 * Added for 64bits alignment, or, ENUMINPUT ioctl will fail. Because
	 * 32 bits app and 64bits driver will get different structure size
	 * which cause the values of ENUMINPUT are different. v4l2_input has a
	 * 64-bit element which make v4l2_input structure has to be 64bit
	 * alignment.
	 */
	uint32_t pad;

	uint32_t	reserved[4];
};

/*  Values for the 'type' field */
#define	V4L2_INPUT_TYPE_TUNER		1
#define	V4L2_INPUT_TYPE_CAMERA		2

/* field 'status' - general */
#define	V4L2_IN_ST_NO_POWER    0x00000001  /* Attached device is off */
#define	V4L2_IN_ST_NO_SIGNAL   0x00000002
#define	V4L2_IN_ST_NO_COLOR    0x00000004

/* field 'status' - analog */
#define	V4L2_IN_ST_NO_H_LOCK	0x00000100  /* No horizontal sync lock */
#define	V4L2_IN_ST_COLOR_KILL	0x00000200  /* Color killer is active */

/* field 'status' - digital */
#define	V4L2_IN_ST_NO_SYNC	0x00010000  /* No synchronization lock */
#define	V4L2_IN_ST_NO_EQU	0x00020000  /* No equalizer lock */
#define	V4L2_IN_ST_NO_CARRIER	0x00040000  /* Carrier recovery failed */

/* field 'status' - VCR and set-top box */
#define	V4L2_IN_ST_MACROVISION	0x01000000  /* Macrovision detected */
#define	V4L2_IN_ST_NO_ACCESS	0x02000000  /* Conditional access denied */
#define	V4L2_IN_ST_VTR		0x04000000  /* VTR time constant */

/*
 *	V I D E O   O U T P U T S
 */
struct v4l2_output
{
	uint32_t	index;		/* Which output */
	uint8_t		name[32];	/* Label */
	uint32_t	type;		/* Type of output */
	uint32_t	audioset;	/* Associated audios (bitfield) */
	uint32_t	modulator;	/* Associated modulator */
	v4l2_std_id	std;
	uint32_t	reserved[4];
};
/*  Values for the 'type' field */
#define	V4L2_OUTPUT_TYPE_MODULATOR		1
#define	V4L2_OUTPUT_TYPE_ANALOG			2
#define	V4L2_OUTPUT_TYPE_ANALOGVGAOVERLAY	3

/*
 *	C O N T R O L S
 */
struct v4l2_control
{
	uint32_t	id;
	int32_t		value;
};

struct v4l2_ext_control
{
	uint32_t id;
	uint32_t reserved2[2];
	union {
		int32_t value;
		int64_t value64;
		void *reserved;
	} value;
}; /*  __attribute__ ((packed)), comment for solaris to compile */

struct v4l2_ext_controls
{
	uint32_t ctrl_class;
	uint32_t count;
	uint32_t error_idx;
	uint32_t reserved[2];
	struct v4l2_ext_control *controls;
};

/*  Values for ctrl_class field */
#define	V4L2_CTRL_CLASS_USER 0x00980000	/* Old-style 'user' controls */
#define	V4L2_CTRL_CLASS_MPEG 0x00990000	/* MPEG-compression controls */

#define	V4L2_CTRL_ID_MASK	  (0x0fffffff)
#define	V4L2_CTRL_ID2CLASS(id)	  ((id) & 0x0fff0000UL)
#define	V4L2_CTRL_DRIVER_PRIV(id) (((id) & 0xffff) >= 0x1000)

/*  Used in the VIDIOC_QUERYCTRL ioctl for querying controls */
struct v4l2_queryctrl
{
	uint32_t		id;
	enum			v4l2_ctrl_type type;
	char			name[32];	/* Whatever */
	int32_t			minimum; /* Note signedness */
	int32_t			maximum;
	int32_t			step;
	int32_t			default_value;
	uint32_t		flags;
	uint32_t		reserved[2];
};

/*  Used in the VIDIOC_QUERYMENU ioctl for querying menu items */
struct v4l2_querymenu
{
	uint32_t		id;
	uint32_t		index;
	uint8_t		name[32];	/* Whatever */
	uint32_t		reserved;
};

/*  Control flags  */
#define	V4L2_CTRL_FLAG_DISABLED		0x0001
#define	V4L2_CTRL_FLAG_GRABBED		0x0002
#define	V4L2_CTRL_FLAG_READ_ONLY	0x0004
#define	V4L2_CTRL_FLAG_UPDATE		0x0008
#define	V4L2_CTRL_FLAG_INACTIVE 	0x0010
#define	V4L2_CTRL_FLAG_SLIDER		0x0020

/*  Query flag, to be ORed with the control ID */
#define	V4L2_CTRL_FLAG_NEXT_CTRL	0x80000000

/*  User-class control IDs defined by V4L2 */
#define	V4L2_CID_BASE			(V4L2_CTRL_CLASS_USER | 0x900)
#define	V4L2_CID_USER_BASE		V4L2_CID_BASE
/*  IDs reserved for driver specific controls */
#define	V4L2_CID_PRIVATE_BASE		0x08000000

#define	V4L2_CID_USER_CLASS		(V4L2_CTRL_CLASS_USER | 1)
#define	V4L2_CID_BRIGHTNESS		(V4L2_CID_BASE+0)
#define	V4L2_CID_CONTRAST		(V4L2_CID_BASE+1)
#define	V4L2_CID_SATURATION		(V4L2_CID_BASE+2)
#define	V4L2_CID_HUE			(V4L2_CID_BASE+3)
#define	V4L2_CID_AUDIO_VOLUME		(V4L2_CID_BASE+5)
#define	V4L2_CID_AUDIO_BALANCE		(V4L2_CID_BASE+6)
#define	V4L2_CID_AUDIO_BASS		(V4L2_CID_BASE+7)
#define	V4L2_CID_AUDIO_TREBLE		(V4L2_CID_BASE+8)
#define	V4L2_CID_AUDIO_MUTE		(V4L2_CID_BASE+9)
#define	V4L2_CID_AUDIO_LOUDNESS		(V4L2_CID_BASE+10)
#define	V4L2_CID_BLACK_LEVEL		(V4L2_CID_BASE+11)
#define	V4L2_CID_AUTO_WHITE_BALANCE	(V4L2_CID_BASE+12)
#define	V4L2_CID_DO_WHITE_BALANCE	(V4L2_CID_BASE+13)
#define	V4L2_CID_RED_BALANCE		(V4L2_CID_BASE+14)
#define	V4L2_CID_BLUE_BALANCE		(V4L2_CID_BASE+15)
#define	V4L2_CID_GAMMA			(V4L2_CID_BASE+16)
#define	V4L2_CID_WHITENESS		(V4L2_CID_GAMMA) /* ? Not sure */
#define	V4L2_CID_EXPOSURE		(V4L2_CID_BASE+17)
#define	V4L2_CID_AUTOGAIN		(V4L2_CID_BASE+18)
#define	V4L2_CID_GAIN			(V4L2_CID_BASE+19)
#define	V4L2_CID_HFLIP			(V4L2_CID_BASE+20)
#define	V4L2_CID_VFLIP			(V4L2_CID_BASE+21)
#define	V4L2_CID_HCENTER		(V4L2_CID_BASE+22)
#define	V4L2_CID_VCENTER		(V4L2_CID_BASE+23)
#define	V4L2_CID_LASTP1			(V4L2_CID_BASE+24) /* last CID + 1 */

/*  MPEG-class control IDs defined by V4L2 */
#define	V4L2_CID_MPEG_BASE			(V4L2_CTRL_CLASS_MPEG | 0x900)
#define	V4L2_CID_MPEG_CLASS			(V4L2_CTRL_CLASS_MPEG | 1)

/*  MPEG streams */
#define	V4L2_CID_MPEG_STREAM_TYPE		(V4L2_CID_MPEG_BASE+0)
enum v4l2_mpeg_stream_type {
	V4L2_MPEG_STREAM_TYPE_MPEG2_PS	 = 0, /* MPEG-2 program stream */
	V4L2_MPEG_STREAM_TYPE_MPEG2_TS	 = 1, /* MPEG-2 transport stream */
	V4L2_MPEG_STREAM_TYPE_MPEG1_SS	 = 2, /* MPEG-1 system stream */
	V4L2_MPEG_STREAM_TYPE_MPEG2_DVD  = 3, /* MPEG-2 DVD-compatible stream */
	V4L2_MPEG_STREAM_TYPE_MPEG1_VCD  = 4, /* MPEG-1 VCD-compatible stream */
	V4L2_MPEG_STREAM_TYPE_MPEG2_SVCD = 5 /* MPEG-2 SVCD-compatible stream */
};
#define	V4L2_CID_MPEG_STREAM_PID_PMT		(V4L2_CID_MPEG_BASE+1)
#define	V4L2_CID_MPEG_STREAM_PID_AUDIO		(V4L2_CID_MPEG_BASE+2)
#define	V4L2_CID_MPEG_STREAM_PID_VIDEO		(V4L2_CID_MPEG_BASE+3)
#define	V4L2_CID_MPEG_STREAM_PID_PCR		(V4L2_CID_MPEG_BASE+4)
#define	V4L2_CID_MPEG_STREAM_PES_ID_AUDIO	(V4L2_CID_MPEG_BASE+5)
#define	V4L2_CID_MPEG_STREAM_PES_ID_VIDEO	(V4L2_CID_MPEG_BASE+6)
#define	V4L2_CID_MPEG_STREAM_VBI_FMT		(V4L2_CID_MPEG_BASE+7)
enum v4l2_mpeg_stream_vbi_fmt {
	V4L2_MPEG_STREAM_VBI_FMT_NONE = 0,  /* No VBI in the MPEG stream */

	/* VBI in private packets, IVTV format */
	V4L2_MPEG_STREAM_VBI_FMT_IVTV = 1
};

/*  MPEG audio */
#define	V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ	(V4L2_CID_MPEG_BASE+100)
enum v4l2_mpeg_audio_sampling_freq {
	V4L2_MPEG_AUDIO_SAMPLING_FREQ_44100 = 0,
	V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000 = 1,
	V4L2_MPEG_AUDIO_SAMPLING_FREQ_32000 = 2
};
#define	V4L2_CID_MPEG_AUDIO_ENCODING		(V4L2_CID_MPEG_BASE+101)
enum v4l2_mpeg_audio_encoding {
	V4L2_MPEG_AUDIO_ENCODING_LAYER_1 = 0,
	V4L2_MPEG_AUDIO_ENCODING_LAYER_2 = 1,
	V4L2_MPEG_AUDIO_ENCODING_LAYER_3 = 2
};
#define	V4L2_CID_MPEG_AUDIO_L1_BITRATE		(V4L2_CID_MPEG_BASE+102)
enum v4l2_mpeg_audio_l1_bitrate {
	V4L2_MPEG_AUDIO_L1_BITRATE_32K	= 0,
	V4L2_MPEG_AUDIO_L1_BITRATE_64K	= 1,
	V4L2_MPEG_AUDIO_L1_BITRATE_96K	= 2,
	V4L2_MPEG_AUDIO_L1_BITRATE_128K = 3,
	V4L2_MPEG_AUDIO_L1_BITRATE_160K = 4,
	V4L2_MPEG_AUDIO_L1_BITRATE_192K = 5,
	V4L2_MPEG_AUDIO_L1_BITRATE_224K = 6,
	V4L2_MPEG_AUDIO_L1_BITRATE_256K = 7,
	V4L2_MPEG_AUDIO_L1_BITRATE_288K = 8,
	V4L2_MPEG_AUDIO_L1_BITRATE_320K = 9,
	V4L2_MPEG_AUDIO_L1_BITRATE_352K = 10,
	V4L2_MPEG_AUDIO_L1_BITRATE_384K = 11,
	V4L2_MPEG_AUDIO_L1_BITRATE_416K = 12,
	V4L2_MPEG_AUDIO_L1_BITRATE_448K = 13
};
#define	V4L2_CID_MPEG_AUDIO_L2_BITRATE		(V4L2_CID_MPEG_BASE+103)
enum v4l2_mpeg_audio_l2_bitrate {
	V4L2_MPEG_AUDIO_L2_BITRATE_32K	= 0,
	V4L2_MPEG_AUDIO_L2_BITRATE_48K	= 1,
	V4L2_MPEG_AUDIO_L2_BITRATE_56K	= 2,
	V4L2_MPEG_AUDIO_L2_BITRATE_64K	= 3,
	V4L2_MPEG_AUDIO_L2_BITRATE_80K	= 4,
	V4L2_MPEG_AUDIO_L2_BITRATE_96K	= 5,
	V4L2_MPEG_AUDIO_L2_BITRATE_112K = 6,
	V4L2_MPEG_AUDIO_L2_BITRATE_128K = 7,
	V4L2_MPEG_AUDIO_L2_BITRATE_160K = 8,
	V4L2_MPEG_AUDIO_L2_BITRATE_192K = 9,
	V4L2_MPEG_AUDIO_L2_BITRATE_224K = 10,
	V4L2_MPEG_AUDIO_L2_BITRATE_256K = 11,
	V4L2_MPEG_AUDIO_L2_BITRATE_320K = 12,
	V4L2_MPEG_AUDIO_L2_BITRATE_384K = 13
};
#define	V4L2_CID_MPEG_AUDIO_L3_BITRATE		(V4L2_CID_MPEG_BASE+104)
enum v4l2_mpeg_audio_l3_bitrate {
	V4L2_MPEG_AUDIO_L3_BITRATE_32K	= 0,
	V4L2_MPEG_AUDIO_L3_BITRATE_40K	= 1,
	V4L2_MPEG_AUDIO_L3_BITRATE_48K	= 2,
	V4L2_MPEG_AUDIO_L3_BITRATE_56K	= 3,
	V4L2_MPEG_AUDIO_L3_BITRATE_64K	= 4,
	V4L2_MPEG_AUDIO_L3_BITRATE_80K	= 5,
	V4L2_MPEG_AUDIO_L3_BITRATE_96K	= 6,
	V4L2_MPEG_AUDIO_L3_BITRATE_112K = 7,
	V4L2_MPEG_AUDIO_L3_BITRATE_128K = 8,
	V4L2_MPEG_AUDIO_L3_BITRATE_160K = 9,
	V4L2_MPEG_AUDIO_L3_BITRATE_192K = 10,
	V4L2_MPEG_AUDIO_L3_BITRATE_224K = 11,
	V4L2_MPEG_AUDIO_L3_BITRATE_256K = 12,
	V4L2_MPEG_AUDIO_L3_BITRATE_320K = 13
};
#define	V4L2_CID_MPEG_AUDIO_MODE		(V4L2_CID_MPEG_BASE+105)
enum v4l2_mpeg_audio_mode {
	V4L2_MPEG_AUDIO_MODE_STEREO		= 0,
	V4L2_MPEG_AUDIO_MODE_JOINT_STEREO	= 1,
	V4L2_MPEG_AUDIO_MODE_DUAL		= 2,
	V4L2_MPEG_AUDIO_MODE_MONO		= 3
};
#define	V4L2_CID_MPEG_AUDIO_MODE_EXTENSION	(V4L2_CID_MPEG_BASE+106)
enum v4l2_mpeg_audio_mode_extension {
	V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_4	= 0,
	V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_8	= 1,
	V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_12 = 2,
	V4L2_MPEG_AUDIO_MODE_EXTENSION_BOUND_16 = 3
};
#define	V4L2_CID_MPEG_AUDIO_EMPHASIS		(V4L2_CID_MPEG_BASE+107)
enum v4l2_mpeg_audio_emphasis {
	V4L2_MPEG_AUDIO_EMPHASIS_NONE = 0,
	V4L2_MPEG_AUDIO_EMPHASIS_50_DIV_15_uS = 1,
	V4L2_MPEG_AUDIO_EMPHASIS_CCITT_J17 = 2
};
#define	V4L2_CID_MPEG_AUDIO_CRC 		(V4L2_CID_MPEG_BASE+108)
enum v4l2_mpeg_audio_crc {
	V4L2_MPEG_AUDIO_CRC_NONE = 0,
	V4L2_MPEG_AUDIO_CRC_CRC16 = 1
};

/*  MPEG video */
#define	V4L2_CID_MPEG_VIDEO_ENCODING		(V4L2_CID_MPEG_BASE+200)
enum v4l2_mpeg_video_encoding {
	V4L2_MPEG_VIDEO_ENCODING_MPEG_1 = 0,
	V4L2_MPEG_VIDEO_ENCODING_MPEG_2 = 1
};
#define	V4L2_CID_MPEG_VIDEO_ASPECT		(V4L2_CID_MPEG_BASE+201)
enum v4l2_mpeg_video_aspect {
	V4L2_MPEG_VIDEO_ASPECT_1x1 = 0,
	V4L2_MPEG_VIDEO_ASPECT_4x3 = 1,
	V4L2_MPEG_VIDEO_ASPECT_16x9 = 2,
	V4L2_MPEG_VIDEO_ASPECT_221x100 = 3
};
#define	V4L2_CID_MPEG_VIDEO_B_FRAMES		(V4L2_CID_MPEG_BASE+202)
#define	V4L2_CID_MPEG_VIDEO_GOP_SIZE		(V4L2_CID_MPEG_BASE+203)
#define	V4L2_CID_MPEG_VIDEO_GOP_CLOSURE 	(V4L2_CID_MPEG_BASE+204)
#define	V4L2_CID_MPEG_VIDEO_PULLDOWN		(V4L2_CID_MPEG_BASE+205)
#define	V4L2_CID_MPEG_VIDEO_BITRATE_MODE	(V4L2_CID_MPEG_BASE+206)
enum v4l2_mpeg_video_bitrate_mode {
	V4L2_MPEG_VIDEO_BITRATE_MODE_VBR = 0,
	V4L2_MPEG_VIDEO_BITRATE_MODE_CBR = 1
};
#define	V4L2_CID_MPEG_VIDEO_BITRATE		(V4L2_CID_MPEG_BASE+207)
#define	V4L2_CID_MPEG_VIDEO_BITRATE_PEAK	(V4L2_CID_MPEG_BASE+208)
#define	V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION (V4L2_CID_MPEG_BASE+209)

/* MPEG-class control IDs specific to the CX2584x driver as defined by V4L2 */
#define	V4L2_CID_MPEG_CX2341X_BASE \
	(V4L2_CTRL_CLASS_MPEG | 0x1000)
#define	V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE \
	(V4L2_CID_MPEG_CX2341X_BASE+0)
enum v4l2_mpeg_cx2341x_video_spatial_filter_mode {
	V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_MANUAL = 0,
	V4L2_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE_AUTO   = 1
};
#define	V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER \
	(V4L2_CID_MPEG_CX2341X_BASE+1)
#define	V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE \
	(V4L2_CID_MPEG_CX2341X_BASE+2)
enum v4l2_mpeg_cx2341x_video_luma_spatial_filter_type {
	V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_OFF = 0,
	V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_1D_HOR  = 1,
	V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_1D_VERT = 2,
	V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_2D_HV_SEPARABLE = 3,
	V4L2_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE_2D_SYM_NON_SEPARABLE =
		4
};
#define	V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE \
	(V4L2_CID_MPEG_CX2341X_BASE+3)
enum v4l2_mpeg_cx2341x_video_chroma_spatial_filter_type {
	V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_OFF	  = 0,
	V4L2_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE_1D_HOR = 1
};
#define	V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE \
	(V4L2_CID_MPEG_CX2341X_BASE+4)
enum v4l2_mpeg_cx2341x_video_temporal_filter_mode {
	V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_MANUAL = 0,
	V4L2_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE_AUTO   = 1
};
#define	V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER \
	(V4L2_CID_MPEG_CX2341X_BASE+5)
#define	V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE \
	(V4L2_CID_MPEG_CX2341X_BASE+6)
enum v4l2_mpeg_cx2341x_video_median_filter_type {
	V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_OFF		= 0,
	V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_HOR		= 1,
	V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_VERT		= 2,
	V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_HOR_VERT	= 3,
	V4L2_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE_DIAG		= 4
};
#define	V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM \
	(V4L2_CID_MPEG_CX2341X_BASE+7)
#define	V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP \
	(V4L2_CID_MPEG_CX2341X_BASE+8)
#define	V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM \
	(V4L2_CID_MPEG_CX2341X_BASE+9)
#define	V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP \
	(V4L2_CID_MPEG_CX2341X_BASE+10)

/*
 *	T U N I N G
 */
struct v4l2_tuner
{
	uint32_t	index;
	uint8_t		name[32];
	enum		v4l2_tuner_type	type;
	uint32_t	capability;
	uint32_t	rangelow;
	uint32_t	rangehigh;
	uint32_t	rxsubchans;
	uint32_t	audmode;
	int32_t		signal;
	int32_t		afc;
	uint32_t	reserved[4];
};

struct v4l2_modulator
{
	uint32_t	index;
	uint8_t		name[32];
	uint32_t	capability;
	uint32_t	rangelow;
	uint32_t	rangehigh;
	uint32_t	txsubchans;
	uint32_t	reserved[4];
};

/*  Flags for the 'capability' field */
#define	V4L2_TUNER_CAP_LOW		0x0001
#define	V4L2_TUNER_CAP_NORM		0x0002
#define	V4L2_TUNER_CAP_STEREO		0x0010
#define	V4L2_TUNER_CAP_LANG2		0x0020
#define	V4L2_TUNER_CAP_SAP		0x0020
#define	V4L2_TUNER_CAP_LANG1		0x0040

/*  Flags for the 'rxsubchans' field */
#define	V4L2_TUNER_SUB_MONO		0x0001
#define	V4L2_TUNER_SUB_STEREO		0x0002
#define	V4L2_TUNER_SUB_LANG2		0x0004
#define	V4L2_TUNER_SUB_SAP		0x0004
#define	V4L2_TUNER_SUB_LANG1		0x0008

/*  Values for the 'audmode' field */
#define	V4L2_TUNER_MODE_MONO		0x0000
#define	V4L2_TUNER_MODE_STEREO		0x0001
#define	V4L2_TUNER_MODE_LANG2		0x0002
#define	V4L2_TUNER_MODE_SAP		0x0002
#define	V4L2_TUNER_MODE_LANG1		0x0003
#define	V4L2_TUNER_MODE_LANG1_LANG2	0x0004

struct v4l2_frequency
{
	uint32_t		tuner;
	enum			v4l2_tuner_type  type;
	uint32_t		frequency;
	uint32_t		reserved[8];
};

/*
 *	A U D I O
 */
struct v4l2_audio
{
	uint32_t	index;
	uint8_t	name[32];
	uint32_t	capability;
	uint32_t	mode;
	uint32_t	reserved[2];
};

/*  Flags for the 'capability' field */
#define	V4L2_AUDCAP_STEREO		0x00001
#define	V4L2_AUDCAP_AVL			0x00002

/*  Flags for the 'mode' field */
#define	V4L2_AUDMODE_AVL		0x00001

struct v4l2_audioout
{
	uint32_t	index;
	uint8_t	name[32];
	uint32_t	capability;
	uint32_t	mode;
	uint32_t	reserved[2];
};

/*
 *	D A T A   S E R V I C E S   ( V B I )
 *
 *	Data services API by Michael Schimek
 */

/* Raw VBI */
struct v4l2_vbi_format
{
	uint32_t	sampling_rate;		/* in 1 Hz */
	uint32_t	offset;
	uint32_t	samples_per_line;
	uint32_t	sample_format;		/* V4L2_PIX_FMT_* */
	int32_t	start[2];
	uint32_t	count[2];
	uint32_t	flags;			/* V4L2_VBI_* */
	uint32_t	reserved[2];		/* must be zero */
};

/*  VBI flags  */
#define	V4L2_VBI_UNSYNC		(1<< 0)
#define	V4L2_VBI_INTERLACED	(1<< 1)

#if 1
/*
 * Sliced VBI
 *
 *    This implements is a proposal V4L2 API to allow SLICED VBI
 * required for some hardware encoders. It should change without
 * notice in the definitive implementation.
 */

struct v4l2_sliced_vbi_format
{
	uint16_t	service_set;

	/*
	 * service_lines[0][...] specifies lines 0-23 (1-23 used) of the first
	 * field
	 * service_lines[1][...] specifies lines 0-23 (1-23 used) of the second
	 * field
	 * (equals frame lines 313-336 for 625 line video standards,
	 * 263-286 for 525 line standards)
	 */
	uint16_t   service_lines[2][24];
	uint32_t   io_size;
	uint32_t   reserved[2];	/* must be zero */
};

/*
 * Teletext World System Teletext
 * (WST), defined on ITU-R BT.653-2
 */
#define	V4L2_SLICED_TELETEXT_B		(0x0001)
/* Video Program System, defined on ETS 300 231 */
#define	V4L2_SLICED_VPS 		(0x0400)
/* Closed Caption, defined on EIA-608 */
#define	V4L2_SLICED_CAPTION_525 	(0x1000)
/* Wide Screen System, defined on ITU-R BT1119.1 */
#define	V4L2_SLICED_WSS_625		(0x4000)

#define	V4L2_SLICED_VBI_525	(V4L2_SLICED_CAPTION_525)
#define	V4L2_SLICED_VBI_625	(V4L2_SLICED_TELETEXT_B | V4L2_SLICED_VPS | \
				V4L2_SLICED_WSS_625)


struct v4l2_sliced_vbi_cap
{
	uint16_t   service_set;
	/*
	 * service_lines[0][...] specifies lines 0-23 (1-23 used) of the first
	 * field
	 * service_lines[1][...] specifies lines 0-23 (1-23 used) of the second
	 * field
	 * (equals frame lines 313-336 for 625 line video standards, 263-286
	 * for 525 line standards)
	 */
	uint16_t   service_lines[2][24];
	uint32_t   reserved[4];    /* must be 0 */
};

struct v4l2_sliced_vbi_data
{
	uint32_t   id;
	uint32_t   field;	   /* 0: first field, 1: second field */
	uint32_t   line;	   /* 1-23 */
	uint32_t   reserved;	   /* must be 0 */
	uint8_t    data[48];
};
#endif

/*
 *	A G G R E G A T E   S T R U C T U R E S
 */

/*
 * Stream data format
 */
struct v4l2_format
{
	enum v4l2_buf_type type;

	/*
	 * Added for 64bits alignment, or, S_FMT ioctl will fail. Because
	 * 32 bits app and 64bits driver will get different structure size
	 * which cause the values of S_FMT are different. v4l2_window has
	 * pointers which make v4l2_format structure has to be 64bit alignment.
	 */
	uint32_t pad;
	union
	{
		struct v4l2_pix_format	pix; /* V4L2_BUF_TYPE_VIDEO_CAPTURE */
		struct v4l2_window	win; /* V4L2_BUF_TYPE_VIDEO_OVERLAY */
		struct v4l2_vbi_format	vbi; /* V4L2_BUF_TYPE_VBI_CAPTURE */
#if 1

		/* V4L2_BUF_TYPE_SLICED_VBI_CAPTURE */
		struct v4l2_sliced_vbi_format	sliced;
#endif
		uint8_t	raw_data[200];			 /* user-defined */
	} fmt;
};


/*
 * Stream type-dependent parameters
 */
struct v4l2_streamparm
{
	enum v4l2_buf_type type;
	union
	{
		struct v4l2_captureparm	capture;
		struct v4l2_outputparm	output;
		uint8_t	raw_data[200];	/* user-defined */
	} parm;
};

/*
 *	I O C T L   C O D E S	F O R	V I D E O   D E V I C E S
 *
 */
#define	VIDIOC_QUERYCAP		_IOR('V',  0, struct v4l2_capability)
#define	VIDIOC_RESERVED		_IO('V',  1)
#define	VIDIOC_ENUM_FMT 	_IOWR('V',  2, struct v4l2_fmtdesc)
#define	VIDIOC_G_FMT		_IOWR('V',  4, struct v4l2_format)
#define	VIDIOC_S_FMT		_IOWR('V',  5, struct v4l2_format)
#ifdef __KERNEL__
#define	VIDIOC_G_MPEGCOMP	_IOR('V',  6, struct v4l2_mpeg_compression)
#define	VIDIOC_S_MPEGCOMP	_IOW('V',  7, struct v4l2_mpeg_compression)
#endif
#define	VIDIOC_REQBUFS		_IOWR('V',  8, struct v4l2_requestbuffers)
#define	VIDIOC_QUERYBUF		_IOWR('V',  9, struct v4l2_buffer)
#define	VIDIOC_G_FBUF		_IOR('V', 10, struct v4l2_framebuffer)
#define	VIDIOC_S_FBUF		_IOW('V', 11, struct v4l2_framebuffer)
#define	VIDIOC_OVERLAY		_IOW('V', 14, int)
#define	VIDIOC_QBUF		_IOWR('V', 15, struct v4l2_buffer)
#define	VIDIOC_DQBUF		_IOWR('V', 17, struct v4l2_buffer)
#define	VIDIOC_STREAMON		_IOW('V', 18, int)
#define	VIDIOC_STREAMOFF	_IOW('V', 19, int)
#define	VIDIOC_G_PARM		_IOWR('V', 21, struct v4l2_streamparm)
#define	VIDIOC_S_PARM		_IOWR('V', 22, struct v4l2_streamparm)
#define	VIDIOC_G_STD		_IOR('V', 23, v4l2_std_id)
#define	VIDIOC_S_STD		_IOW('V', 24, v4l2_std_id)
#define	VIDIOC_ENUMSTD		_IOWR('V', 25, struct v4l2_standard)
#define	VIDIOC_ENUMINPUT	_IOWR('V', 26, struct v4l2_input)
#define	VIDIOC_G_CTRL		_IOWR('V', 27, struct v4l2_control)
#define	VIDIOC_S_CTRL		_IOWR('V', 28, struct v4l2_control)
#define	VIDIOC_G_TUNER		_IOWR('V', 29, struct v4l2_tuner)
#define	VIDIOC_S_TUNER		_IOW('V', 30, struct v4l2_tuner)
#define	VIDIOC_G_AUDIO		_IOR('V', 33, struct v4l2_audio)
#define	VIDIOC_S_AUDIO		_IOW('V', 34, struct v4l2_audio)
#define	VIDIOC_QUERYCTRL	_IOWR('V', 36, struct v4l2_queryctrl)
#define	VIDIOC_QUERYMENU	_IOWR('V', 37, struct v4l2_querymenu)
#define	VIDIOC_G_INPUT		_IOR('V', 38, int)
#define	VIDIOC_S_INPUT		_IOWR('V', 39, int)
#define	VIDIOC_G_OUTPUT		_IOR('V', 46, int)
#define	VIDIOC_S_OUTPUT		_IOWR('V', 47, int)
#define	VIDIOC_ENUMOUTPUT	_IOWR('V', 48, struct v4l2_output)
#define	VIDIOC_G_AUDOUT		_IOR('V', 49, struct v4l2_audioout)
#define	VIDIOC_S_AUDOUT		_IOW('V', 50, struct v4l2_audioout)
#define	VIDIOC_G_MODULATOR	_IOWR('V', 54, struct v4l2_modulator)
#define	VIDIOC_S_MODULATOR	_IOW('V', 55, struct v4l2_modulator)
#define	VIDIOC_G_FREQUENCY	_IOWR('V', 56, struct v4l2_frequency)
#define	VIDIOC_S_FREQUENCY	_IOW('V', 57, struct v4l2_frequency)
#define	VIDIOC_CROPCAP		_IOWR('V', 58, struct v4l2_cropcap)
#define	VIDIOC_G_CROP		_IOWR('V', 59, struct v4l2_crop)
#define	VIDIOC_S_CROP		_IOW('V', 60, struct v4l2_crop)
#define	VIDIOC_G_JPEGCOMP	_IOR('V', 61, struct v4l2_jpegcompression)
#define	VIDIOC_S_JPEGCOMP	_IOW('V', 62, struct v4l2_jpegcompression)
#define	VIDIOC_QUERYSTD 	_IOR('V', 63, v4l2_std_id)
#define	VIDIOC_TRY_FMT		_IOWR('V', 64, struct v4l2_format)
#define	VIDIOC_ENUMAUDIO	_IOWR('V', 65, struct v4l2_audio)
#define	VIDIOC_ENUMAUDOUT	_IOWR('V', 66, struct v4l2_audioout)
#define	VIDIOC_G_PRIORITY	_IOR('V', 67, enum v4l2_priority)
#define	VIDIOC_S_PRIORITY	_IOW('V', 68, enum v4l2_priority)
#if 1
#define	VIDIOC_G_SLICED_VBI_CAP _IOR('V', 69, struct v4l2_sliced_vbi_cap)
#endif
#define	VIDIOC_LOG_STATUS	_IO('V', 70)
#define	VIDIOC_G_EXT_CTRLS	_IOWR('V', 71, struct v4l2_ext_controls)
#define	VIDIOC_S_EXT_CTRLS	_IOWR('V', 72, struct v4l2_ext_controls)
#define	VIDIOC_TRY_EXT_CTRLS	_IOWR('V', 73, struct v4l2_ext_controls)

#ifdef __OLD_VIDIOC_
/* for compatibility, will go away some day */
#define	VIDIOC_OVERLAY_OLD	_IOWR('V', 14, int)
#define	VIDIOC_S_PARM_OLD	_IOW('V', 22, struct v4l2_streamparm)
#define	VIDIOC_S_CTRL_OLD	_IOW('V', 28, struct v4l2_control)
#define	VIDIOC_G_AUDIO_OLD	_IOWR('V', 33, struct v4l2_audio)
#define	VIDIOC_G_AUDOUT_OLD	_IOWR('V', 49, struct v4l2_audioout)
#define	VIDIOC_CROPCAP_OLD	_IOR('V', 58, struct v4l2_cropcap)
#endif

#define	BASE_VIDIOC_PRIVATE	192		/* 192-255 are private */


#ifdef	__cplusplus
}
#endif

#endif /* __LINUX_VIDEODEV2_H */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
