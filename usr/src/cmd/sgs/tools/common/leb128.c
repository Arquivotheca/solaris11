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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/elf.h>
#include <dwarf.h>
#include <sgs.h>

/*
 * Little Endian Base 128 (LEB128) numbers.
 * ----------------------------------------
 *
 * LEB128 is a scheme for encoding integers densely that exploits the
 * assumption that most integers are small in magnitude. (This encoding
 * is equally suitable whether the target machine architecture represents
 * data in big-endian or little- endian
 *
 * Unsigned LEB128 numbers are encoded as follows: start at the low order
 * end of an unsigned integer and chop it into 7-bit chunks. Place each
 * chunk into the low order 7 bits of a byte. Typically, several of the
 * high order bytes will be zero; discard them. Emit the remaining bytes in
 * a stream, starting with the low order byte; set the high order bit on
 * each byte except the last emitted byte. The high bit of zero on the last
 * byte indicates to the decoder that it has encountered the last byte.
 * The integer zero is a special case, consisting of a single zero byte.
 *
 * Signed, 2s complement LEB128 numbers are encoded in a similar except
 * that the criterion for discarding high order bytes is not whether they
 * are zero, but whether they consist entirely of sign extension bits.
 * Consider the 32-bit integer -2. The three high level bytes of the number
 * are sign extension, thus LEB128 would represent it as a single byte
 * containing the low order 7 bits, with the high order bit cleared to
 * indicate the end of the byte stream.
 *
 * Note that there is nothing within the LEB128 representation that
 * indicates whether an encoded number is signed or unsigned. The decoder
 * must know what type of number to expect.
 *
 * DWARF Exception Header Encoding
 * -------------------------------
 *
 * The DWARF Exception Header Encoding is used to describe the type of data
 * used in the .eh_frame_hdr section. The upper 4 bits indicate how the
 * value is to be applied. The lower 4 bits indicate the format of the data.
 *
 * DWARF Exception Header value format
 *
 * Name		Value Meaning
 * DW_EH_PE_omit	    0xff No value is present.
 * DW_EH_PE_absptr	    0x00 Value is a void*
 * DW_EH_PE_uleb128	    0x01 Unsigned value is encoded using the
 *				 Little Endian Base 128 (LEB128)
 * DW_EH_PE_udata2	    0x02 A 2 bytes unsigned value.
 * DW_EH_PE_udata4	    0x03 A 4 bytes unsigned value.
 * DW_EH_PE_udata8	    0x04 An 8 bytes unsigned value.
 * DW_EH_PE_signed          0x08 bit on for all signed encodings
 * DW_EH_PE_sleb128	    0x09 Signed value is encoded using the
 *				 Little Endian Base 128 (LEB128)
 * DW_EH_PE_sdata2	    0x0A A 2 bytes signed value.
 * DW_EH_PE_sdata4	    0x0B A 4 bytes signed value.
 * DW_EH_PE_sdata8	    0x0C An 8 bytes signed value.
 *
 * DWARF Exception Header application
 *
 * Name	    Value Meaning
 * DW_EH_PE_absptr	   0x00 Value is used with no modification.
 * DW_EH_PE_pcrel	   0x10 Value is reletive to the location of itself
 * DW_EH_PE_textrel	   0x20
 * DW_EH_PE_datarel	   0x30 eh_frame value is relative to the GOT,
 *                              while eh_frame_hdr value is relative to the
 *                              beginning of the eh_frame_hdr section
 * DW_EH_PE_funcrel        0x40
 * DW_EH_PE_aligned        0x50 value is an aligned void*
 * DW_EH_PE_indirect       0x80 bit to signal indirection after relocation
 * DW_EH_PE_omit	   0xff No value is present.
 *
 */

uint64_t
uleb_extract(uchar_t *data, uint64_t *dotp)
{
	uint64_t	dot = *dotp;
	uint64_t	res = 0;
	int		more = 1;
	int		shift = 0;
	int		val;

	data += dot;

	while (more) {
		/*
		 * Pull off lower 7 bits
		 */
		val = (*data) & 0x7f;

		/*
		 * Add prepend value to head of number.
		 */
		res = res | (val << shift);

		/*
		 * Increment shift & dot pointer
		 */
		shift += 7;
		dot++;

		/*
		 * Check to see if hi bit is set - if not, this
		 * is the last byte.
		 */
		more = ((*data++) & 0x80) >> 7;
	}
	*dotp = dot;
	return (res);
}

int64_t
sleb_extract(uchar_t *data, uint64_t *dotp)
{
	uint64_t	dot = *dotp;
	int64_t		res = 0;
	int		more = 1;
	int		shift = 0;
	int		val;

	data += dot;

	while (more) {
		/*
		 * Pull off lower 7 bits
		 */
		val = (*data) & 0x7f;

		/*
		 * Add prepend value to head of number.
		 */
		res = res | (val << shift);

		/*
		 * Increment shift & dot pointer
		 */
		shift += 7;
		dot++;

		/*
		 * Check to see if hi bit is set - if not, this
		 * is the last byte.
		 */
		more = ((*data++) & 0x80) >> 7;
	}
	*dotp = dot;

	/*
	 * Make sure value is properly sign extended.
	 */
	res = (res << (64 - shift)) >> (64 - shift);

	return (res);
}

/*
 * Extract a non-leb encoded integer value, where the bytes are simply
 * found in order in the input.
 */

static uint64_t
extract_integer(uchar_t *data, uint64_t *dotp, Boolean is_signed,
    uchar_t *eident, uint_t fsize)
{
	uint64_t	dot = *dotp;
	Boolean		lsb = (eident[EI_DATA] == ELFDATA2LSB);
	uint64_t	result;
	uint_t		cnt;

	/*
	 * Extract unaligned LSB/MSB formatted data
	 */
	result = 0;
	for (cnt = 0; cnt < fsize; cnt++, dot++) {
		uint64_t	val;
		int		byte_idx;

		val = data[dot];
		byte_idx = lsb ? cnt : (fsize - cnt - 1);
		result |= val << (byte_idx * 8);
	}

	/*
	 * perform sign extension
	 */
	if (is_signed && (fsize < sizeof (uint64_t))) {
		int64_t	sresult;
		uint_t	bitshift;
		sresult = result;
		bitshift = (sizeof (uint64_t) - fsize) * 8;
		sresult = (sresult << bitshift) >> bitshift;
		result = sresult;
	}

	*dotp = dot;
	return (result);
}

/*
 * Extract a DWARF encoded datum
 *
 * entry:
 *	data - Base of data buffer containing encoded bytes
 *	dotp - Address of variable containing index within data
 *		at which the desired datum starts.
 *	ehe_flags - DWARF encoding
 *	eident - ELF header e_ident[] array for object being processed
 *	sneg - Address of a variable to be set to True if the resulting value
 *		is a signed integer with a negative value, and False
 *		otherwise. See the note below.
 *	pcrel_base - Address of this datum, used to adjust
 *		DW_EH_PE_pcrel values.
 *	datarel_base - Address used as base for adjusting DW_EH_PE_pcrel values.
 *
 * note:
 *	This routine can read a variety of encoding types, converting all
 *	of them to a 64-bit unsigned result. This works well, except in the
 *	case where the input value is a signed negative value. Converted to
 *	unsigned, the caller will see a large positive value rather than
 *	a negative one.
 *
 *	Object files generally deal only with unsigned integers, and as such,
 *	situation is not expected to occur in practice. If that changes, then
 *	the interface to this function, and the code within all of its callers,
 *	will need to be reconsidered. In the meantime, callers must make use
 *	of the sneg argument to this function to detect this case and provide
 *	an error so that it does not go unreported.
 */
uint64_t
dwarf_ehe_extract(uchar_t *data, uint64_t *dotp, uint_t ehe_flags,
    uchar_t *eident, Boolean *sneg, uint64_t pcrel_base, uint64_t datarel_base)
{
	uint_t	    wordsize = (eident[EI_CLASS] == ELFCLASS64) ? 8 : 4;
	Boolean	    is_signed = (ehe_flags & DW_EH_PE_signed) != 0;
	uint64_t    result;

	*sneg = FALSE;

	switch (ehe_flags & 0x0f) {
	case DW_EH_PE_omit:
		return (0);
	case DW_EH_PE_absptr:
		result =
		    extract_integer(data, dotp, is_signed, eident, wordsize);
		break;
	case DW_EH_PE_udata8:
	case DW_EH_PE_sdata8:
		result = extract_integer(data, dotp, is_signed, eident, 8);
		break;
	case DW_EH_PE_udata4:
	case DW_EH_PE_sdata4:
		result = extract_integer(data, dotp, is_signed, eident, 4);
		break;
	case DW_EH_PE_udata2:
	case DW_EH_PE_sdata2:
		result = extract_integer(data, dotp, is_signed, eident, 2);
		break;
	case DW_EH_PE_uleb128:
		result = uleb_extract(data, dotp);
		break;
	case DW_EH_PE_sleb128:
		result = (uint64_t)sleb_extract(data, dotp);
		break;
	default:
		return (0);
	}

	/*
	 * Process the result:
	 *
	 * -	The DWARF spec says that these computations are supposed
	 *	to be done on an integer with the object wordsize, so if
	 *	this is a 32-bit object, we need to clip the result.
	 *
	 * -	Regardless of ELFCLASS, if the value is relative to a
	 *	base address, we must add the base address to value. Note
	 *	that result may be holding the bit pattern of a signed value.
	 *	Adding the base address must be done to a variable with the
	 *	proper signed/unsigned attribute.
	 */
	if (wordsize == 4) {
		if (is_signed) {
			int32_t sresult32 = result;

			switch (ehe_flags & 0xf0) {
			case DW_EH_PE_pcrel:
				sresult32 += pcrel_base;
				break;

			case DW_EH_PE_datarel:
				sresult32 += datarel_base;
				break;
			}

			if (sresult32 < 0)
				*sneg = TRUE;
			result = sresult32;
		} else {
			uint32_t result32 = result;

			switch (ehe_flags & 0xf0) {
			case DW_EH_PE_pcrel:
				result32 += pcrel_base;
				break;

			case DW_EH_PE_datarel:
				result32 += datarel_base;
				break;
			}
			result = result32;
		}
	} else {		/* wordsize == 8 */
		if (is_signed) {
			int64_t sresult = result;

			switch (ehe_flags & 0xf0) {
			case DW_EH_PE_pcrel:
				sresult += pcrel_base;
				break;

			case DW_EH_PE_datarel:
				sresult += datarel_base;
				break;
			}

			if (sresult < 0)
				*sneg = TRUE;
			result = sresult;
		} else {
			switch (ehe_flags & 0xf0) {
			case DW_EH_PE_pcrel:
				result += pcrel_base;
				break;

			case DW_EH_PE_datarel:
				result += datarel_base;
				break;
			}
		}
	}

	return (result);
}
