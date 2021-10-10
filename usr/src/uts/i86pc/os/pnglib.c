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
/* BEGIN CSTYLED */
/*
 * Simple PNG library to load images from kernel land. Indexed and Interlaced
 * images are not supported.
 *
 * This code is partially based on pnglite.c by Daniel Karling
 * (http://sourceforge.net/projects/pnglite/), see copyright notice in
 * i86pc/sys/pnglib.h.
 */
/* END CSTYLED */

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/kobj.h>
#include <sys/zmod.h>
#include <sys/crc32.h>

#include <sys/pnglib-private.h>

char *
png_strerror(int error)
{
	switch (error) {
	case PNG_NO_ERROR:
		return ("No error");
	case PNG_FILE_ERROR:
		return ("Unknown file error.");
	case PNG_HEADER_ERROR:
		return ("No PNG header found.");
	case PNG_IO_ERROR:
		return ("Failure while reading file.");
	case PNG_EOF_ERROR:
		return ("Reached end of file.");
	case PNG_CRC_ERROR:
		return ("CRC or chunk length error.");
	case PNG_MEMORY_ERROR:
		return ("Could not allocate memory.");
	case PNG_ZLIB_ERROR:
		return ("zlib reported an error.");
	case PNG_UNKNOWN_FILTER:
		return ("Unknown filter method used in scanline.");
	case PNG_DONE:
		return ("PNG done.");
	case PNG_NOT_SUPPORTED:
		return ("The PNG image specified is unsupported.");
	default:
		return ("Unknown error.");
	}
}

static ssize_t
png_read(png_t *png, void *dest, size_t size)
{
	size_t	ret;

	ret = kobj_read_file(png->kobj_handle, dest, size, png->kobj_offset);
	if (ret < 0)
		return (-1);

	png->kobj_offset += ret;
	return (ret);
}

static int
png_read_ul(png_t *png, uint32_t *out)
{
	unsigned char buf[4];

	if (png_read(png, buf, 4) != 4)
		return (PNG_FILE_ERROR);

	*out = (buf[0]<<24) | (buf[1]<<16) | (buf[2]<<8) | buf[3];

	return (PNG_NO_ERROR);
}

static unsigned
png_get_ul(unsigned char *buf)
{
	unsigned result;
	unsigned char foo[4];

	(void) memcpy(foo, buf, 4);

	result = (foo[0]<<24) | (foo[1]<<16) | (foo[2]<<8) | foo[3];

	return (result);
}


static void
png_filter_sub(int stride, unsigned char *in, unsigned char *out, int len)
{
	int i;
	unsigned char a = 0;

	for (i = 0; i < len; i++) {
		if (i >= stride)
			a = out[i - stride];

		out[i] = in[i] + a;
	}
}

/*ARGSUSED*/
static void
png_filter_up(int stride, unsigned char *in, unsigned char *out,
    unsigned char *prev_line, int len)
{
	int i;

	if (prev_line) {
		for (i = 0; i < len; i++)
			out[i] = in[i] + prev_line[i];
	} else {
		(void) memcpy(out, in, len);
	}
}

static void
png_filter_average(int stride, unsigned char *in, unsigned char *out,
    unsigned char *prev_line, int len)
{
	int i;
	unsigned char a = 0;
	unsigned char b = 0;
	unsigned int sum = 0;

	for (i = 0; i < len; i++) {
		if (prev_line)
			b = prev_line[i];

		if (i >= stride)
			a = out[i - stride];

		sum = a;
		sum += b;

		out[i] = (char)(in[i] + sum/2);
	}
}

static int
abs(int arg)
{
	return (arg >= 0 ? arg : -arg);
}


static unsigned char
png_paeth(unsigned char a, unsigned char b, unsigned char c)
{
	int p = (int)a + b - c;
	int pa = abs(p - a);
	int pb = abs(p - b);
	int pc = abs(p - c);

	int pr;

	if (pa <= pb && pa <= pc)
		pr = a;
	else if (pb <= pc)
		pr = b;
	else
		pr = c;

	return ((char)pr);
}

static void
png_filter_paeth(int stride, unsigned char *in, unsigned char *out,
    unsigned char *prev_line, int len)
{
	int i;
	unsigned char a;
	unsigned char b;
	unsigned char c;

	for (i = 0; i < len; i++) {
		if (prev_line && i >= stride) {
			a = out[i - stride];
			b = prev_line[i];
			c = prev_line[i - stride];
		} else {
			if (prev_line)
				b = prev_line[i];
			else
				b = 0;

			if (i >= stride)
				a = out[i - stride];
			else
				a = 0;

			c = 0;
		}
		out[i] = in[i] + png_paeth(a, b, c);
	}
}

#define	PNG_RGB_INFO	(rgb)

#define	REDSHIFT	(PNG_RGB_INFO.red.shift)
#define	GREENSHIFT	(PNG_RGB_INFO.green.shift)
#define	BLUESHIFT	(PNG_RGB_INFO.blue.shift)

#define	REDSIZE		(PNG_RGB_INFO.red.size)
#define	GREENSIZE	(PNG_RGB_INFO.green.size)
#define	BLUESIZE	(PNG_RGB_INFO.blue.size)

/* BEGIN CSTYLED */
#define	RGB32TO16(rgb) (uint16_t)( 					       \
    ((rgb[0] >> (8 - REDSIZE)) & RGB_MASK_16(8 - REDSIZE)) << REDSHIFT |       \
    ((rgb[1] >> (8 - GREENSIZE)) & RGB_MASK_16(8 - GREENSIZE)) << GREENSHIFT | \
    ((rgb[2] >> (8 - BLUESIZE)) & RGB_MASK_16(8 - BLUESIZE)) << BLUESHIFT)
/* END CSTYLED */

/*
 * Blend the foreground image into the background one.
 */
static void png_alpha_blend(uint8_t *fg, uint8_t *bg, uint8_t *blend)
{
	int		i = 0;
	uint8_t		alpha = fg[3];

	/*
	 * Blend foreground and background according to alpha value.
	 * In the alpha == 0 case, the foreground image is fully transparent,
	 * while in the alpha == 0xff case, it is fully opaque.
	 */
	for (i = 0; i < 3; i++) {
		if (alpha == 0xff)
			blend[i] = fg[i];
		else if (alpha == 0)
			blend[i] = bg[i];
		else
			blend[i] = ((fg[i] * alpha) +
			    (bg[i] * (uint8_t)(0xff - alpha))) >> 8;
	}
}


/*ARGSUSED*/
static void
png_push_16(rgb_t PNG_RGB_INFO, uint8_t *data, uint8_t *scanline, uint32_t len,
    uint8_t skip)
{
	int 		k;
	uint16_t	*dest = (uint16_t *)data;

	for (k = 0; k < len; k += skip) {
		if (skip == 3) {
			*dest = RGB32TO16(scanline);
		} else {
			uint8_t	bg[3];
			uint8_t temp[3];

			/*
			 * Convert the 16 bit background pixel in a 32-bit
			 * one to use with the alpha blending code.
			 */
			bg[0] = ((*dest >> REDSHIFT) & RGB_MASK_16(8 - REDSIZE))
			    << (8 - REDSIZE);
			bg[1] = ((*dest >> GREENSHIFT) &
			    RGB_MASK_16(8 - GREENSIZE)) << (8 - GREENSIZE);
			bg[2] = ((*dest >> BLUESHIFT) &
			    RGB_MASK_16(8 - BLUESIZE)) << (8 - BLUESIZE);

			png_alpha_blend(scanline, bg, temp);
			*dest = RGB32TO16(temp);
		}
		dest++;
		scanline += skip;
	}
}

#define	RGB32TO32(rgb)	(uint32_t)((rgb[0] << REDSHIFT |		       \
	rgb[1] << GREENSHIFT | rgb[2] << BLUESHIFT))

/*ARGSUSED*/
static void
png_push_32(rgb_t PNG_RGB_INFO, uint8_t *data, uint8_t *scanline, uint32_t len,
    uint8_t skip)
{
	int		k;
	uint32_t	*dest = (uint32_t *)data;

	for (k = 0; k < len; k += skip) {
		if (skip == 3) {
			/* RGB image. */
			*dest = RGB32TO32(scanline);
		} else {
			uint8_t	bg[3];
			uint8_t	temp[3];

			/*
			 * png_alpha_blend matches the scanline with a RGB
			 * background, let's provide it.
			 */
			bg[0] = (*dest >> REDSHIFT) & 0xff;
			bg[1] = (*dest >> GREENSHIFT) & 0xff;
			bg[2] = (*dest >> BLUESHIFT) & 0xff;

			png_alpha_blend(scanline, bg, temp);
			*dest = RGB32TO32(temp);
		}
		dest++;
		scanline += skip;
	}
}

static int
png_convert_to_dstscreen(png_t *png, uint8_t *scanline, uint32_t len,
    uint32_t row)
{
	png_conv_info_t	*h_info = &(png->conv_info.width);
	png_conv_info_t	*v_info = &(png->conv_info.height);
	uint8_t		*src = scanline;
	uint8_t		*dst = png->screen.dstbuf;
	uint32_t	size = len;

	/*
	 * We have a raw image that we need to push to the screen.
	 * We start by skipping scanlines outside the visibile screen.
	 */
	if (v_info->type == LARGER_THAN_SCREEN) {
		if (row < v_info->before_gap || row >= (png->image.height -
		    v_info->after_gap))
			return (PNG_NO_ERROR);
	} else {
		dst += v_info->before_gap * png->screen.width * png->screen.bpp;
	}

	/* The scanline needs to be written to the screen: how much of it? */
	if (h_info->type == LARGER_THAN_SCREEN) {
		src += h_info->before_gap * png->image.bpp;
		size -= h_info->offset * png->image.bpp;
	} else { /* Image is horizontally smaller than the screen */
		dst += h_info->before_gap * png->screen.bpp;
	}

	/* Display the scanline. We only support RGB TRUECOLOR */
	if (png->screen.bpp == 2) {
		png_push_16(png->screen.rgb, dst, src, size, png->image.bpp);
	} else if (png->screen.bpp == 4) {
		png_push_32(png->screen.rgb, dst, src, size, png->image.bpp);
	} else {
		cmn_err(CE_WARN, "PNG Convertion failed: destination depth is"
		    " unsupported");
		return (PNG_NOT_SUPPORTED);
	}

	/* Adjust destination offset. */
	png->screen.dstbuf += png->screen.width * png->screen.bpp;

	return (PNG_NO_ERROR);
}

static int
png_unfilter(png_t *png)
{
	unsigned 	i;
	unsigned 	pos = 0;
	unsigned 	row = 0;
	unsigned 	len;
	unsigned char 	*filtered = png->raw_data;
	unsigned char 	*line;
	unsigned char 	*prevline;
	int 		stride = png->image.bpp;

	len = png->image.width * stride;
	line = kmem_zalloc(len, KM_SLEEP);
	prevline = kmem_zalloc(len, KM_SLEEP);

	while (pos < png->raw_datalen) {
		unsigned char filter = filtered[pos];
		pos++;

		if (png->image.depth == 16) {
			for (i = 0; i < png->image.width * stride; i += 2) {
				*(short *)(filtered+pos+i) =
				    (filtered[pos+i] << 8) | filtered[pos+i+1];
			}
		}
		switch (filter) {
		case 0: /* none */
			(void) memcpy(line, filtered+pos, len);
			break;
		case 1: /* sub */
			png_filter_sub(stride, filtered+pos, line, len);
			break;
		case 2: /* up */
			if (row)
				png_filter_up(stride, filtered+pos, line,
				    prevline, len);
			else
				png_filter_up(stride, filtered+pos, line, 0,
				    len);
			break;
		case 3: /* average */
			if (row)
				png_filter_average(stride, filtered+pos, line,
				    prevline, len);
			else
				png_filter_average(stride, filtered+pos, line,
				    0, len);
			break;
		case 4: /* paeth */
			if (row)
				png_filter_paeth(stride, filtered+pos, line,
				    prevline, len);
			else
				png_filter_paeth(stride, filtered+pos, line, 0,
				    len);
			break;
		default:
			kmem_free(line, len);
			kmem_free(prevline, len);
			return (PNG_UNKNOWN_FILTER);
		}

		(void) memcpy(prevline, line, len);

		(void) png_convert_to_dstscreen(png, line, len, row);
		row++;
		pos += png->image.width * stride;
	}

	kmem_free(line, len);
	kmem_free(prevline, len);

	return (PNG_NO_ERROR);
}

static int
png_read_idat(png_t *png, unsigned firstlen)
{
	unsigned 	type = 0;
	char 		*chunk;
	int 		result = 0;
	uint32_t 	length = firstlen;
	uint32_t 	old_len = length;
	uint32_t 	orig_crc;
	uint32_t 	calc_crc;

	chunk = kmem_zalloc(length, KM_SLEEP);
	do {
		if (png_read(png, chunk, length) != length) {
			cmn_err(CE_WARN, "Unable to read CHUNK.");
			kmem_free(chunk, length);
			return (PNG_FILE_ERROR);
		}
		/*
		 * Portable Network Graphics (PNG) Specification (2nd Edition)
		 *
		 * "In PNG, the 32-bit CRC is initialized to all 1's, and then
		 * the data from each byte is processed from the least (1)
		 * significant bit to the most significant bit (128).
		 * After all the data bytes are processed, the CRC is inverted
		 * (its ones complement is taken)."
		 */
		CRC32(calc_crc, (unsigned char *)"IDAT", 4, -1U, crc32_table);
		CRC32(calc_crc, (unsigned char *)chunk, length, calc_crc,
		    crc32_table);
		calc_crc ^= 0xffffffff;

		if (png_read_ul(png, &orig_crc) < 0) {
			cmn_err(CE_WARN, "Unable to read CRC from png file");
			kmem_free(chunk, length);
			return (PNG_FILE_ERROR);
		}

		if (orig_crc != calc_crc) {
			cmn_err(CE_WARN, "Wrong CHUNK CRC: %x (png file: %x)",
			    orig_crc, calc_crc);
			kmem_free(chunk, length);
			return (PNG_CRC_ERROR);
		}

		/* Create a compressed image from all the chunks. */
		(void) memcpy(png->gz_image + png->gz_image_offset, chunk,
		    length);
		png->gz_image_offset += length;

		/* Read next CHUNK size. */
		result = png_read_ul(png, &length);
		if (result < 0) {
			cmn_err(CE_WARN, "Unable to read next CHUNK size");
			kmem_free(chunk, length);
			return (PNG_FILE_ERROR);
		}

		/* Read next CHUNK type. */
		if (png_read(png, &type, 4) != 4) {
			cmn_err(CE_WARN, "Unable to read next CHUNK type");
			kmem_free(chunk, length);
			return (PNG_FILE_ERROR);
		}
		/* Is the next CHUNK bigger? */
		if (length > old_len) {
			kmem_free(chunk, old_len);
			chunk = kmem_zalloc(length, KM_SLEEP);
			old_len = length;
		}
	} while (type == *(unsigned int *)"IDAT");

	kmem_free(chunk, old_len);

	/* Catch a truncated PNG image. */
	if (type != *(unsigned int *)"IEND") {
		cmn_err(CE_WARN, "Malformed PNG file: no IEND after IDAT");
		return (PNG_FILE_ERROR);
	}

	return (PNG_DONE);
}

static int
png_process_chunk(png_t *png)
{
	int 		result = PNG_FILE_ERROR;
	uint32_t 	type;
	uint32_t 	length;

	/* Read in the length of the CHUNK. */
	result = png_read_ul(png, &length);
	if (result < 0)
		return (PNG_FILE_ERROR);

	/* Read in the CHUNK type. */
	if (png_read(png, &type, 4) != 4)
		return (PNG_FILE_ERROR);

	/*
	 * We care only about two CHUNK types: IDAT and IEND.
	 * We keep scanning through the CHUNKS until we find the first IDAT. At
	 * that point, all other IDATs should follow with no other chunks in
	 * between. We error out if an IEND is found before an IDAT.
	 */
	if (type == *(unsigned int *)"IDAT") {
		/* Alloc enough space to store the compressed image. */
		png->gz_image_size = png->image.width * png->image.height *
		    png->image.bpp;
		png->gz_image = kmem_zalloc(png->gz_image_size, KM_SLEEP);
		png->gz_image_offset = 0;

		return (png_read_idat(png, length));
	} else if (type == *(unsigned int *)"IEND") {
			cmn_err(CE_WARN, "IEND found before IDAT");
			return (PNG_FILE_ERROR);
	} else {
		/* Unknown chunk found, just skip reading the file. */
		png->kobj_offset += length + 4;
	}
	return (result);
}

static int
png_inflate(png_t *png)
{
	int		ret;
	size_t		avail_out = png->raw_datalen;
	uint8_t		*dest = png->raw_data;

	ret = z_uncompress(dest, &avail_out, png->gz_image,
	    png->gz_image_offset);
	if (ret != Z_OK) {
		cmn_err(CE_WARN, "ZLIB %s", z_strerror(ret));
		return (PNG_ZLIB_ERROR);
	}

	return (PNG_NO_ERROR);
}

static int
png_fill_conv(png_conv_info_t *conv, uint32_t screen, uint32_t image,
    uint8_t pos)
{
	if (screen >= image) {
		conv->offset = screen - image;
		conv->type = SMALLER_THAN_SCREEN;
		conv->before_gap = conv->offset / 2;
		conv->after_gap = conv->before_gap + (conv->offset % 2);
	} else {
		conv->offset = image - screen;
		conv->type = LARGER_THAN_SCREEN;
		switch (pos) {
		case PNG_HLEFT:
		case PNG_VTOP:
			conv->before_gap = 0;
			conv->after_gap = conv->offset;
			break;
		case PNG_HCENTER:
		case PNG_VCENTER:
			conv->before_gap = conv->offset / 2;
			conv->after_gap = conv->before_gap +
			    (conv->offset % 2);
			break;
		case PNG_HRIGHT:
		case PNG_VBOTTOM:
			conv->before_gap = conv->offset;
			conv->after_gap = 0;
			break;
		default:
			return (PNG_FILE_ERROR);
		}
	}

	return (PNG_NO_ERROR);
}

static int
png_setup_convertion(png_t *png)
{
	png_conv_info_t		*h_info = &(png->conv_info.width);
	png_conv_info_t		*v_info = &(png->conv_info.height);
	int			ret;

	/* Deal with the horizontal positioning. */
	ret = png_fill_conv(h_info, png->screen.width, png->image.width,
	    png->screen.h_pos);
	if (ret != PNG_NO_ERROR)
		return (ret);

	/* Deal with the vertical positioning. */
	return (png_fill_conv(v_info, png->screen.height, png->image.height,
	    png->screen.v_pos));
}

static int
png_fill_data(png_t *png)
{
	int 	result = PNG_NO_ERROR;

	while (result == PNG_NO_ERROR)
		result = png_process_chunk(png);

	if (result != PNG_DONE) {
		if (png->gz_image != NULL)
			kmem_free(png->gz_image, png->gz_image_size);
		return (result);
	}

	/* Alloc enough space to store the uncompressed image. */
	png->raw_datalen = png->image.width * png->image.height *
	    png->image.bpp + png->image.height;
	png->raw_data = kmem_zalloc(png->raw_datalen, KM_SLEEP);

	/* Uncompress the image. */
	result = png_inflate(png);
	kmem_free(png->gz_image, png->gz_image_size);

	if (result != PNG_NO_ERROR) {
		kmem_free(png->raw_data, png->raw_datalen);
		return (result);
	}

	/* Setup convertion information for correct displaying. */
	result = png_setup_convertion(png);
	if (result != PNG_NO_ERROR) {
		kmem_free(png->raw_data, png->raw_datalen);
		return (result);
	}

	/* Unfilter the image into the user provided destination buffer. */
	result = png_unfilter(png);
	kmem_free(png->raw_data, png->raw_datalen);

	return (result);
}


static int
png_get_bpp(png_t *png)
{
	switch (png->image.color_type) {
	case PNG_GREYSCALE:
	case PNG_INDEXED:
		png->image.bpp = 1;
		break;
	case PNG_TRUECOLOR:
		png->image.bpp = 3;
		break;
	case PNG_GREYSCALE_ALPHA:
		png->image.bpp = 2;
		break;
	case PNG_TRUECOLOR_ALPHA:
		png->image.bpp = 4;
		break;
	default:
		return (PNG_FILE_ERROR);
	}

	png->image.bpp *= png->image.depth/8;
	return (PNG_NO_ERROR);
}

#define	PNG_IHDR_SIG		"IHDR"
#define	PNG_IHDR_SIGSIZE	(sizeof (PNG_IHDR_SIG) - 1)
#define	PNG_IHDR_SIZE		(13)
#define	PNG_IHDR_BUF		(PNG_IHDR_SIZE + PNG_IHDR_SIGSIZE)

static int
png_parse_ihdr(png_t *png)
{
	uint32_t	length;
	uint32_t	orig_crc;
	uint32_t	calc_crc;
	int		ret;

	/* length should be 13, make room for type (IHDR). */
	unsigned char ihdr[PNG_IHDR_BUF];
	if ((ret = png_read_ul(png, &length)) < 0) {
		cmn_err(CE_WARN, "Error reading length from png file: %s",
		    png_strerror(ret));
		return (ret);
	}

	/* Check if the stored length is the one we expect. */
	if (length != PNG_IHDR_SIZE)
		return (PNG_CRC_ERROR);

	/* Read in the header. */
	if (png_read(png, ihdr, PNG_IHDR_BUF) != PNG_IHDR_BUF)
		return (PNG_EOF_ERROR);

	if ((ret = png_read_ul(png, &orig_crc)) < 0) {
		cmn_err(CE_WARN, "Error reading CRC from png file: %s",
		    png_strerror(ret));
		return (ret);
	}

	/*
	 * Portable Network Graphics (PNG) Specification (Second Edition)
	 *
	 * "In PNG, the 32-bit CRC is initialized to all 1's, and then the data
	 * from each byte is processed from the least significant bit (1) to the
	 * most significant bit (128). After all the data bytes are processed,
	 * the CRC is inverted (its ones complement is taken)."
	 */
	CRC32(calc_crc, ihdr, PNG_IHDR_BUF, -1U, crc32_table);
	calc_crc ^= 0xffffffff;

	if (orig_crc != calc_crc)
		return (PNG_CRC_ERROR);

	/* Fill image information. */
	png->image.width = png_get_ul(ihdr+4);
	png->image.height = png_get_ul(ihdr+8);
	png->image.depth = ihdr[12];
	png->image.color_type = ihdr[13];
	png->image.compression_method = ihdr[14];
	png->image.filter_method = ihdr[15];
	png->image.interlace_method = ihdr[16];

	/* We don't support indexed color images. */
	if (png->image.color_type == PNG_INDEXED)
		return (PNG_NOT_SUPPORTED);

	if (png->image.depth != 8 && png->image.depth != 16)
		return (PNG_NOT_SUPPORTED);

	/* No interlacing method supported. */
	if (png->image.interlace_method)
		return (PNG_NOT_SUPPORTED);

	return (png_get_bpp(png));
}

#define	PNG_HEADER_SIG	"\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"
#define	PNG_SIG_SIZE	(sizeof (PNG_HEADER_SIG) - 1)

static int
png_parse_header(png_t *png)
{
	char 	signature[PNG_SIG_SIZE];

	/* Check png signature. */
	if (png_read(png, signature, PNG_SIG_SIZE) != PNG_SIG_SIZE)
		return (PNG_EOF_ERROR);

	if (memcmp(signature, PNG_HEADER_SIG, PNG_SIG_SIZE) != 0)
		return (PNG_HEADER_ERROR);

	/* Parse ihdr for image information. */
	return (png_parse_ihdr(png));
}


static png_t *
png_init(char *file)
{
	int		ret;
	png_t		*image = kmem_zalloc(sizeof (png_t), KM_SLEEP);

	image->kobj_handle = kobj_open_file(file);
	if (image->kobj_handle == (struct _buf *)-1) {
		cmn_err(CE_WARN, "png_open_file: kobj_open_file failed");
		kmem_free(image, sizeof (png_t));
		return (NULL);
	}

	ret = png_parse_header(image);
	if (ret != PNG_NO_ERROR) {
		cmn_err(CE_WARN, "%s header parsing failed: %s", file,
		    png_strerror(ret));
		kobj_close_file(image->kobj_handle);
		kmem_free(image, sizeof (png_t));
		return (NULL);
	}

	return (image);
}

static void
png_fini(png_t *png)
{
	kobj_close_file(png->kobj_handle);
	kmem_free(png, sizeof (png_t));
}

/*
 * Load the PNG image specified by the 'file' (path) using the information
 * provided by 'screen'. See i86pc/sys/pnglib.h for a more detailed
 * description of png_screen_t.
 */
int
png_load_file(char *file, png_screen_t *screen)
{
	png_t		*image;
	int		ret;

	image = png_init(file);
	if (image == NULL)
		return (PNG_INIT_ERROR);

	image->screen = *screen;
	ret = png_fill_data(image);
	png_fini(image);

	return (ret);
}

/*
 * Read information from the image specified by 'file' into 'info'. See
 * i86pc/sys/pnglib.h for a more detailed description of png_image_t (it
 * basically reflects the PNG Header).
 */
int
png_get_info(char *file, png_image_t *info)
{
	png_t		*image;

	image = png_init(file);
	if (image == NULL)
		return (-1);

	*info = image->image;
	png_fini(image);

	return (PNG_NO_ERROR);
}
