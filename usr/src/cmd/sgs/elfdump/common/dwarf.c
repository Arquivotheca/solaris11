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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include	<_libelf.h>
#include	<dwarf.h>
#include	<stdio.h>
#include	<unistd.h>
#include	<errno.h>
#include	<strings.h>
#include	<debug.h>
#include	<conv.h>
#include	<msg.h>
#include	<_elfdump.h>


/*
 * Data from eh_frame section used by dump_cfi()
 */
typedef struct {
	const char	*file;		/* Name of file */
	const char	*shname;	/* Name of section */
	int		shndx;		/* Section index */
	Half		e_machine;	/* ehdr->e_machine */
	uchar_t		*e_ident;	/* ehdr->e_ident */
	uint64_t	sh_addr;	/* Address of eh_frame section */
	int		do_swap;	/* True if object and system byte */
					/*	order differs */
	int		cieRflag;	/* R flag from current CIE */
	uint64_t	ciecalign;	/* CIE code align factor */
	int64_t		ciedalign;	/* CIE data align factor */
	uint64_t	fdeinitloc;	/* FDE initial location */
	uint64_t	got_addr;	/* Address of .got section */
} dump_cfi_state_t;


/*
 * Extract an unsigned integer value from an .eh_frame section, converting it
 * from its native byte order to that of the running machine if necessary.
 *
 * entry:
 *	data - Base address from which to extract datum
 *	ndx - Address of variable giving index to start byte in data.
 *	size - # of bytes in datum. Must be one of: 1, 2, 4, 8
 *	do_swap - True if the data is in a different byte order than that
 *		of the host system.
 *
 * exit:
 *	*ndx is incremented by the size of the extracted datum.
 *
 *	The requested datum is extracted, byte swapped if necessary,
 *	and returned.
 */
static uint64_t
dwarf_extract_uint(uchar_t *data, uint64_t *ndx, int size, int do_swap)
{
	switch (size) {
	case 1:
		return (data[(*ndx)++]);
	case 2:
		{
			Half	r;
			uchar_t	*p = (uchar_t *)&r;

			data += *ndx;
			if (do_swap)
				UL_ASSIGN_BSWAP_HALF(p, data);
			else
				UL_ASSIGN_HALF(p, data);

			(*ndx) += 2;
			return (r);
		}
	case 4:
		{
			Word	r;
			uchar_t *p = (uchar_t *)&r;

			data += *ndx;
			if (do_swap)
				UL_ASSIGN_BSWAP_WORD(p, data);
			else
				UL_ASSIGN_WORD(p, data);

			(*ndx) += 4;
			return (r);
		}

	case 8:
		{
			uint64_t	r;
			uchar_t		*p = (uchar_t *)&r;

			data += *ndx;
			if (do_swap)
				UL_ASSIGN_BSWAP_LWORD(p, data);
			else
				UL_ASSIGN_LWORD(p, data);

			(*ndx) += 8;
			return (r);
		}
	}

	/* If here, an invalid size was specified */
	assert(0);
	return (0);
}

/*
 * Map a DWARF register constant to the machine register name it
 * corresponds to, formatting the result into buf.
 *
 * The assignment of DWARF register numbers is part of the system
 * specific ABI for each platform.
 *
 * entry:
 *	regno - DWARF register number
 *	mach - ELF machine code for platform
 *	buf, bufsize - Buffer to receive the formatted result string
 *
 * exit:
 *	The results are formatted into buf, and buf is returned.
 *	If the generated output would exceed the size of the buffer
 *	provided, it will be clipped to fit.
 */
static const char *
dwarf_regname(Half mach, int regno, char *buf, size_t bufsize)
{
	Conv_inv_buf_t	inv_buf;
	const char	*name;
	int		good_name;

	name = conv_dwarf_regname(mach, regno, 0, &good_name, &inv_buf);

	/*
	 * If there is a good mnemonic machine name for the register,
	 * format the result as 'r# (mnemonic)'.  If there is no good
	 * name for it, then simply format the dwarf name as 'r#'.
	 */
	if (good_name)
		(void) snprintf(buf, bufsize, MSG_ORIG(MSG_REG_FMT_NAME),
		    regno, name);
	else
		(void) snprintf(buf, bufsize, MSG_ORIG(MSG_REG_FMT_BASIC),
		    regno);

	return (buf);
}


/*
 * Wrapper around dwarf_ehe_extract() that tests for invalid situation
 * in which the object contains a signed negative value. If we see such
 * a case, we issue a warning and keep going.
 */
uint64_t
elfdump_ehe_extract(const char *file, const char *shname, int shndx,
    uchar_t *base_data, uchar_t *data, uint64_t *dotp, uint_t ehe_flags,
    uchar_t *eident, uint64_t pcrel_base, uint64_t datarel_base)
{
	Boolean		sneg;
	uint64_t	value;
	uint64_t	dot = *dotp;

	value = dwarf_ehe_extract(data, dotp, ehe_flags, eident,
	    &sneg, pcrel_base, datarel_base);
	if (sneg)
		(void) fprintf(stderr, MSG_INTL(MSG_WARN_EHNEGINT),
		    file, EC_WORD(shndx), shname,
		    EC_OFF(data + dot - base_data));

	return (value);
}

/*
 * Decode eh_frame Call Frame Instructions, printing each one on a
 * separate line.
 *
 * entry:
 *	data - Address of base of eh_frame section being processed
 *	off - Offset of current FDE within eh_frame
 *	ndx - Index of current position within current FDE
 *	len - Length of eh_frame section
 *	state - Object, CIE, and FDE state for current request
 *	msg - Header message to issue before producing output.
 *	indent - # of indentation characters issued for each line of output.
 *
 * exit:
 *	The Call Frame Instructions have been decoded and printed.
 *
 *	*ndx has been incremented to contain the index of the next
 *		byte of data to be processed in eh_frame.
 *
 * note:
 *	The format of Call Frame Instructions in .eh_frame sections is based
 *	on the DWARF specification.
 */
static void
dump_cfi(uchar_t *data, uint64_t off, uint64_t *ndx, uint_t len,
    dump_cfi_state_t *state, const char *msg, int indent)
{
	/*
	 * We use %*s%s to insert leading whitespace and the op name.
	 * PREFIX supplies these arguments.
	 */
#define	PREFIX	indent, MSG_ORIG(MSG_STR_EMPTY), opname

	/* Hide boilerplate clutter in calls to dwarf_regname() */
#define	REGNAME(_rnum, _buf) \
	dwarf_regname(state->e_machine, _rnum, _buf, sizeof (_buf))

	/* Extract the lower 6 bits from an op code */
#define	LOW_OP(_op) (_op & 0x3f)

	char		rbuf1[32], rbuf2[32];
	Conv_inv_buf_t	inv_buf;
	uchar_t		op;
	const char	*opname;
	uint64_t	oper1, oper2, cur_pc;
	int64_t		soper;
	const char	*loc_str;
	int		i;

	dbg_print(0, msg);

	/*
	 * In a CIE/FDE, the length field does not include its own
	 * size. Hence, the value passed in is 4 less than the index
	 * of the actual final location.
	 */
	len += 4;

	/*
	 * There is a concept of the 'current location', which is the PC
	 * to which the current item applies. It starts out set to the
	 * FDE initial location, and can be set or incremented by
	 * various OP codes. cur_pc is used to track this.
	 *
	 * We want to use 'initloc' in the output the first time the location
	 * is referenced, and then switch to 'loc' for subsequent references.
	 * loc_str is used to manage that.
	 */
	cur_pc = state->fdeinitloc;
	loc_str = MSG_ORIG(MSG_STR_INITLOC);

	while (*ndx < len) {
		/*
		 * The first byte contains the primary op code in the top
		 * 2 bits, so there are 4 of them. Primary OP code
		 * 0 uses the lower 6 bits to specify a sub-opcode, allowing
		 * for 64 of them. The other 3 primary op codes use the
		 * lower 6 bits to hold an operand (a register #, or value).
		 *
		 * Check the primary OP code. If it is 1-3, handle it
		 * and move to the next loop iteration. For OP code 0,
		 * fall through to decode the sub-code.
		 */
		op = data[off + (*ndx)++];
		opname = conv_dwarf_cfa(op, 0, &inv_buf);
		switch (op >> 6) {
		case 0x1:		/* v2: DW_CFA_advance_loc, delta */
			oper1 = state->ciecalign * LOW_OP(op);
			cur_pc += oper1;
			dbg_print(0, MSG_ORIG(MSG_CFA_ADV_LOC), PREFIX,
			    loc_str, EC_XWORD(oper1), EC_XWORD(cur_pc));
			loc_str = MSG_ORIG(MSG_STR_LOC);
			continue;

		case 0x2:		/* v2: DW_CFA_offset, reg, offset */
			soper = uleb_extract(&data[off], ndx) *
			    state->ciedalign;
			dbg_print(0, MSG_ORIG(MSG_CFA_CFAOFF), PREFIX,
			    REGNAME(LOW_OP(op), rbuf1), EC_SXWORD(soper));
			continue;

		case 0x3:		/* v2: DW_CFA_restore, reg */
			dbg_print(0, MSG_ORIG(MSG_CFA_REG), PREFIX,
			    REGNAME(LOW_OP(op), rbuf1));
			continue;
		}

		/*
		 * If we're here, the high order 2 bits are 0. The low 6 bits
		 * specify a sub-opcode defining the operation.
		 */
		switch (op) {
		case 0x00:		/* v2: DW_CFA_nop */
			/*
			 * No-ops are used to fill unused space required
			 * for alignment. It is common for there to be
			 * multiple adjacent nops. It saves space to report
			 * them all with a single line of output.
			 */
			for (i = 1;
			    (*ndx < len) && (data[off + *ndx] == 0);
			    i++, (*ndx)++)
				;
			dbg_print(0, MSG_ORIG(MSG_CFA_SIMPLEREP), PREFIX, i);
			break;

		case 0x0a:		/* v2: DW_CFA_remember_state */
		case 0x0b:		/* v2: DW_CFA_restore_state */
		case 0x2d:		/* GNU: DW_CFA_GNU_window_save */
			dbg_print(0, MSG_ORIG(MSG_CFA_SIMPLE), PREFIX);
			break;

		case 0x01:		/* v2: DW_CFA_set_loc, address */
			cur_pc = elfdump_ehe_extract(state->file,
			    state->shname, state->shndx, data, &data[off], ndx,
			    state->cieRflag, state->e_ident,
			    state->sh_addr + off + *ndx, state->got_addr);
			dbg_print(0, MSG_ORIG(MSG_CFA_CFASET), PREFIX,
			    EC_XWORD(cur_pc));
			break;

		case 0x02:	/* v2: DW_CFA_advance_loc_1, 1-byte delta */
		case 0x03:	/* v2: DW_CFA_advance_loc_2, 2-byte delta */
		case 0x04:	/* v2: DW_CFA_advance_loc_4, 4-byte delta */
			/*
			 * Since the codes are contiguous, and the sizes are
			 * powers of 2, we can compute the word width from
			 * the code.
			 */
			i = 1 << (op - 0x02);
			oper1 = dwarf_extract_uint(data + off, ndx, i,
			    state->do_swap) * state->ciecalign;
			cur_pc += oper1;
			dbg_print(0, MSG_ORIG(MSG_CFA_ADV_LOC), PREFIX,
			    loc_str, EC_XWORD(oper1), EC_XWORD(cur_pc));
			loc_str = MSG_ORIG(MSG_STR_LOC);
			break;

		case 0x05:		/* v2: DW_CFA_offset_extended,reg,off */
			oper1 = uleb_extract(&data[off], ndx);
			soper = uleb_extract(&data[off], ndx) *
			    state->ciedalign;
			dbg_print(0, MSG_ORIG(MSG_CFA_CFAOFF), PREFIX,
			    REGNAME(oper1, rbuf1), EC_SXWORD(soper));
			break;

		case 0x06:		/* v2: DW_CFA_restore_extended, reg */
		case 0x0d:		/* v2: DW_CFA_def_cfa_register, reg */
		case 0x08:		/* v2: DW_CFA_same_value, reg */
		case 0x07:		/* v2: DW_CFA_undefined, reg */
			oper1 = uleb_extract(&data[off], ndx);
			dbg_print(0, MSG_ORIG(MSG_CFA_REG), PREFIX,
			    REGNAME(oper1, rbuf1));
			break;


		case 0x09:		/* v2: DW_CFA_register, reg, reg */
			oper1 = uleb_extract(&data[off], ndx);
			oper2 = uleb_extract(&data[off], ndx);
			dbg_print(0, MSG_ORIG(MSG_CFA_REG_REG), PREFIX,
			    REGNAME(oper1, rbuf1), REGNAME(oper2, rbuf2));
			break;

		case 0x0c:		/* v2: DW_CFA_def_cfa, reg, offset */
			oper1 = uleb_extract(&data[off], ndx);
			oper2 = uleb_extract(&data[off], ndx);
			dbg_print(0, MSG_ORIG(MSG_CFA_REG_OFFLLU), PREFIX,
			    REGNAME(oper1, rbuf1), EC_XWORD(oper2));
			break;

		case 0x0e:		/* v2: DW_CFA_def_cfa_offset, offset */
			oper1 = uleb_extract(&data[off], ndx);
			dbg_print(0, MSG_ORIG(MSG_CFA_LLU), PREFIX,
			    EC_XWORD(oper1));
			break;

		case 0x0f:		/* v3: DW_CFA_def_cfa_expression, blk */
			oper1 = uleb_extract(&data[off], ndx);
			dbg_print(0, MSG_ORIG(MSG_CFA_EBLK), PREFIX,
			    EC_XWORD(oper1));
			/* We currently do not decode the expression block */
			*ndx += oper1;
			break;

		case 0x10:		/* v3: DW_CFA_expression, reg, blk */
		case 0x16:		/* v3: DW_CFA_val_expression,reg,blk */
			oper1 = uleb_extract(&data[off], ndx);
			oper2 = uleb_extract(&data[off], ndx);
			dbg_print(0, MSG_ORIG(MSG_CFA_REG_EBLK), PREFIX,
			    REGNAME(oper1, rbuf1), EC_XWORD(oper2));
			/* We currently do not decode the expression block */
			*ndx += oper2;
			break;

		case 0x11:	/* v3: DW_CFA_offset_extended_sf, reg, off */
			oper1 = uleb_extract(&data[off], ndx);
			soper = sleb_extract(&data[off], ndx) *
			    state->ciedalign;
			dbg_print(0, MSG_ORIG(MSG_CFA_CFAOFF), PREFIX,
			    REGNAME(oper1, rbuf1), EC_SXWORD(soper));
			break;

		case 0x12:		/* v3: DW_CFA_def_cfa_sf, reg, offset */
			oper1 = uleb_extract(&data[off], ndx);
			soper = sleb_extract(&data[off], ndx) *
			    state->ciedalign;
			dbg_print(0, MSG_ORIG(MSG_CFA_REG_OFFLLD), PREFIX,
			    REGNAME(oper1, rbuf1), EC_SXWORD(soper));
			break;

		case 0x13:		/* DW_CFA_def_cfa_offset_sf, offset */
			soper = sleb_extract(&data[off], ndx) *
			    state->ciedalign;
			dbg_print(0, MSG_ORIG(MSG_CFA_LLD), PREFIX,
			    EC_SXWORD(soper));
			break;

		case 0x14:		/* v3: DW_CFA_val_offset, reg, offset */
			oper1 = uleb_extract(&data[off], ndx);
			soper = uleb_extract(&data[off], ndx) *
			    state->ciedalign;
			dbg_print(0, MSG_ORIG(MSG_CFA_REG_OFFLLD), PREFIX,
			    REGNAME(oper1, rbuf1), EC_SXWORD(soper));
			break;

		case 0x15:	/* v3: DW_CFA_val_offset_sf, reg, offset */
			oper1 = uleb_extract(&data[off], ndx);
			soper = sleb_extract(&data[off], ndx) *
			    state->ciedalign;
			dbg_print(0, MSG_ORIG(MSG_CFA_REG_OFFLLD), PREFIX,
			    REGNAME(oper1, rbuf1), EC_SXWORD(soper));
			break;

		case 0x1d:	/* GNU: DW_CFA_MIPS_advance_loc8, delta */
			oper1 = dwarf_extract_uint(data + off, ndx, i,
			    state->do_swap) * state->ciecalign;
			cur_pc += oper1;
			dbg_print(0, MSG_ORIG(MSG_CFA_ADV_LOC), PREFIX,
			    loc_str, EC_XWORD(oper1), EC_XWORD(cur_pc));
			loc_str = MSG_ORIG(MSG_STR_LOC);
			break;

		case 0x2e:		/* GNU: DW_CFA_GNU_args_size, size */
			oper1 = uleb_extract(&data[off], ndx);
			dbg_print(0, MSG_ORIG(MSG_CFA_LLU), PREFIX,
			    EC_XWORD(oper1));

			break;

		case 0x2f: /* GNU:DW_CFA_GNU_negative_offset_extended,reg,off */
			oper1 = uleb_extract(&data[off], ndx);
			soper = -uleb_extract(&data[off], ndx) *
			    state->ciedalign;
			dbg_print(0, MSG_ORIG(MSG_CFA_CFAOFF), PREFIX,
			    REGNAME(oper1, rbuf1), EC_SXWORD(soper));
			break;

		default:
			/*
			 * Unrecognized OP code: DWARF data is variable length,
			 * so we don't know how many bytes to skip in order to
			 * advance to the next item. We cannot decode beyond
			 * this point, so dump the remainder in hex.
			 */
			(*ndx)--;	/* Back up to unrecognized opcode */
			dump_hex_bytes(data + off + *ndx, len - *ndx,
			    indent, 8, 1);
			(*ndx) = len;
			break;
		}
	}

#undef PREFIX
#undef REGNAME
#undef LOW_OP
}

void
dump_eh_frame(const char *file, const char *shname, int shndx, uchar_t *data,
    size_t datasize, uint64_t sh_addr, uint64_t got_addr, Half e_machine,
    uchar_t *e_ident)
{
	Conv_dwarf_ehe_buf_t	dwarf_ehe_buf;
	dump_cfi_state_t	cfi_state;
	uint64_t	off, ndx;
	uint_t		cieid, cielength, cieversion, cieretaddr;
	int		ciePflag, cieZflag, cieLflag, cieLflag_present;
	uint_t		cieaugndx, length, id;
	char		*cieaugstr;

	cfi_state.file = file;
	cfi_state.shname = shname;
	cfi_state.shndx = shndx;
	cfi_state.e_machine = e_machine;
	cfi_state.e_ident = e_ident;
	cfi_state.sh_addr = sh_addr;
	cfi_state.do_swap = _elf_sys_encoding() != e_ident[EI_DATA];

	cfi_state.got_addr = got_addr;

	off = 0;
	while (off < datasize) {
		ndx = 0;

		/*
		 * Extract length in native format.  A zero length indicates
		 * that this CIE is a terminator and that processing for this
		 * unwind information should end. However, skip this entry and
		 * keep processing, just in case there is any other information
		 * remaining in this section.  Note, ld(1) will terminate the
		 * processing of the .eh_frame contents for this file after a
		 * zero length CIE, thus any information that does follow is
		 * ignored by ld(1), and is therefore questionable.
		 */
		length = (uint_t)dwarf_extract_uint(data + off, &ndx,
		    4, cfi_state.do_swap);
		if (length == 0) {
			dbg_print(0, MSG_ORIG(MSG_UNW_ZEROTERM));
			off += 4;
			continue;
		}

		/*
		 * extract CIE id in native format
		 */
		id = (uint_t)dwarf_extract_uint(data + off, &ndx,
		    4, cfi_state.do_swap);

		/*
		 * A CIE record has an id of '0', otherwise this is a
		 * FDE entry and the 'id' is the CIE pointer.
		 */
		if (id == 0) {
			uint64_t	persVal, ndx_save;
			uint_t		axsize;

			cielength = length;
			cieid = id;
			ciePflag = cfi_state.cieRflag = cieZflag = 0;
			cieLflag = cieLflag_present = 0;

			dbg_print(0, MSG_ORIG(MSG_UNW_CIE),
			    EC_XWORD(sh_addr + off));
			dbg_print(0, MSG_ORIG(MSG_UNW_CIELNGTH),
			    cielength, cieid);

			cieversion = data[off + ndx];
			ndx += 1;
			cieaugstr = (char *)(&data[off + ndx]);
			ndx += strlen(cieaugstr) + 1;

			dbg_print(0, MSG_ORIG(MSG_UNW_CIEVERS),
			    cieversion, cieaugstr);

			cfi_state.ciecalign = uleb_extract(&data[off], &ndx);
			cfi_state.ciedalign = sleb_extract(&data[off], &ndx);
			cieretaddr = data[off + ndx];
			ndx += 1;

			dbg_print(0, MSG_ORIG(MSG_UNW_CIECALGN),
			    EC_XWORD(cfi_state.ciecalign),
			    EC_XWORD(cfi_state.ciedalign), cieretaddr);

			if (cieaugstr[0])
				dbg_print(0, MSG_ORIG(MSG_UNW_CIEAXVAL));

			for (cieaugndx = 0; cieaugstr[cieaugndx]; cieaugndx++) {
				switch (cieaugstr[cieaugndx]) {
				case 'z':
					axsize = uleb_extract(&data[off], &ndx);
					dbg_print(0, MSG_ORIG(MSG_UNW_CIEAXSIZ),
					    axsize);
					cieZflag = 1;
					/*
					 * The auxiliary section can contain
					 * unused padding bytes at the end, so
					 * save the current index. Along with
					 * axsize, we will use it to set ndx to
					 * the proper continuation index after
					 * the aux data has been processed.
					 */
					ndx_save = ndx;
					break;
				case 'P':
					ciePflag = data[off + ndx];
					ndx += 1;

					persVal = elfdump_ehe_extract(file,
					    shname, shndx, data, &data[off],
					    &ndx, ciePflag, e_ident,
					    sh_addr + off + ndx,
					    cfi_state.got_addr);
					dbg_print(0,
					    MSG_ORIG(MSG_UNW_CIEAXPERS));
					dbg_print(0,
					    MSG_ORIG(MSG_UNW_CIEAXPERSENC),
					    ciePflag, conv_dwarf_ehe(ciePflag,
					    &dwarf_ehe_buf));
					dbg_print(0,
					    MSG_ORIG(MSG_UNW_CIEAXPERSRTN),
					    EC_XWORD(persVal));
					break;
				case 'R':
					cfi_state.cieRflag = data[off + ndx];
					ndx += 1;
					dbg_print(0,
					    MSG_ORIG(MSG_UNW_CIEAXCENC),
					    cfi_state.cieRflag,
					    conv_dwarf_ehe(cfi_state.cieRflag,
					    &dwarf_ehe_buf));
					break;
				case 'L':
					cieLflag_present = 1;
					cieLflag = data[off + ndx];
					ndx += 1;
					dbg_print(0,
					    MSG_ORIG(MSG_UNW_CIEAXLSDA),
					    cieLflag, conv_dwarf_ehe(
					    cieLflag, &dwarf_ehe_buf));
					break;
				default:
					dbg_print(0,
					    MSG_ORIG(MSG_UNW_CIEAXUNEC),
					    cieaugstr[cieaugndx]);
					break;
				}
			}

			/*
			 * If the z flag was present, reposition ndx using the
			 * length given. This will safely move us past any
			 * unaccessed padding bytes in the auxiliary section.
			 */
			if (cieZflag)
				ndx = ndx_save + axsize;

			/*
			 * Any remaining data are Call Frame Instructions
			 */
			if ((cielength + 4) > ndx)
				dump_cfi(data, off, &ndx, cielength, &cfi_state,
				    MSG_ORIG(MSG_UNW_CIECFI), 3);
			off += cielength + 4;

		} else {
			uint_t	    fdelength = length;
			int	    fdecieptr = id;
			uint64_t    fdeaddrrange;

			dbg_print(0, MSG_ORIG(MSG_UNW_FDE),
			    EC_XWORD(sh_addr + off));
			dbg_print(0, MSG_ORIG(MSG_UNW_FDELNGTH),
			    fdelength, fdecieptr);

			cfi_state.fdeinitloc = elfdump_ehe_extract(file,
			    shname, shndx, data, &data[off], &ndx,
			    cfi_state.cieRflag, e_ident,
			    sh_addr + off + ndx, cfi_state.got_addr);
			fdeaddrrange = elfdump_ehe_extract(file, shname, shndx,
			    data, &data[off], &ndx,
			    (cfi_state.cieRflag & ~DW_EH_PE_pcrel), e_ident,
			    sh_addr + off + ndx, cfi_state.got_addr);

			dbg_print(0, MSG_ORIG(MSG_UNW_FDEINITLOC),
			    EC_XWORD(cfi_state.fdeinitloc),
			    EC_XWORD(fdeaddrrange),
			    EC_XWORD(cfi_state.fdeinitloc + fdeaddrrange - 1));

			if (cieaugstr[0])
				dbg_print(0, MSG_ORIG(MSG_UNW_FDEAXVAL));
			if (cieZflag) {
				uint64_t    val;
				uint64_t    lndx;

				val = uleb_extract(&data[off], &ndx);
				lndx = ndx;
				ndx += val;
				dbg_print(0, MSG_ORIG(MSG_UNW_FDEAXSIZE),
				    EC_XWORD(val));
				if (val && cieLflag_present) {
					uint64_t    lsda;

					lsda = elfdump_ehe_extract(file,
					    shname, shndx, data, &data[off],
					    &lndx, cieLflag, e_ident,
					    sh_addr + off + lndx,
					    cfi_state.got_addr);
					dbg_print(0,
					    MSG_ORIG(MSG_UNW_FDEAXLSDA),
					    EC_XWORD(lsda));
				}
			}
			if ((fdelength + 4) > ndx)
				dump_cfi(data, off, &ndx, fdelength, &cfi_state,
				    MSG_ORIG(MSG_UNW_FDECFI), 6);
			off += fdelength + 4;
		}
	}
}
