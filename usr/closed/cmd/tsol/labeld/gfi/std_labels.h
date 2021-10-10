/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file may contain confidential information of the Defense
 * Intelligence Agency and MITRE Corporation and should not be
 * distributed in source form without approval from Sun Legal.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifndef	_STD_LABELS_H
#define	_STD_LABELS_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * CMW Labels Release 2.2; 8/25/93: std_labels.h
 */

/*
 * std_labels.h contains definitions and variable declarations needed by
 * l_init.c, std_labels.c, and their callers.
 *
 * The first portion of this file defines how compartments, compartment masks,
 * markings, and marking masks are stored on the system (i.e. how big they
 * are) and the needed operations on them.  These definitions can be changed
 * to accomodate other size compartments and markings, and all using programs
 * can be recompiled to automatically handle other size labels.
 *
 * The remainder of the file contains external variable and structure
 * definitions, as well as other key definitions needed by the routines
 * listed above and their callers.
 *
 * Since it is intended that this file be compiled with other programs that call
 * the subroutines in std_labels.c, all variables, structure names, and
 * structure component names start with "l_" to hopefully distinguish them
 * from other names.  Finally, common looking defines like END and NONE start
 * with L_ to distinguish them also.
 *
 * NOTE: Those portions of this file that are intended to be changeable by users
 * of the subroutines in std_labels.c, l_init.c, and l_eof.c are grouped at
 * the beginning of this file.  The end of the file must not be changed, and
 * has a comment denoting the where the non-changeable portion begins.
 */

/*
 * Header files needed by l_init.c, std_labels.c, or l_eof.c.
 */

#include <ctype.h>		/* needed for isspace, isdigit, islower, */
				/* toupper */
#include <stdio.h>		/* needed for printf, vprintf, sprintf, */
				/* fopen, fgetc, flcose, fseek */
#include <stdlib.h>		/* needed for strtol, calloc, free */
#include <string.h>		/* needed for strlen, strcpy, strcmp, */
				/* strncmp, strcat */
#include <sys/types.h>		/* for integer types */

/*
 * Prototypes for vendor-changeable external functions in l_eof.c and
 * l_error.c.
 */

#ifdef __STDC__
extern void
l_error(const unsigned int line_number,
	const char *pattern,
	...);
#else
extern void
l_error(/* const unsigned int line_number,
	const char *pattern,
	... */);
#endif

#ifndef	TSOL
extern int
l_eof(/* const int counting,
	const unsigned int line_number */);
#else	/* TSOL */
extern int
l_eof(const int counting, const unsigned int *line_number);
#endif	/* !TSOL */


/*
 * Define the number of bits contained in each sizeof unit, and a constant
 * equal to the leftmost bit in a byte.
 */

#define	BITS_PER_BYTE 8
#define	LEFTMOST_BIT_IN_BYTE 0x80

/*
 * Define the greatest boundary on which longs or pointers must be aligned on
 * the machine running the labeling subroutines.  Set L_ALIGNMENT to 2 to
 * align on even addresses.  Set L_ALIGNMENT to 4 to align on 4-byte
 * boundaries, etc. It is acceptable to choose a larger than needed alignment
 * amount; the result will be wasted allocated memory.
 */

#ifdef	_LP64
#define	L_ALIGNMENT 8
#else	/* !_LP64 */
#define	L_ALIGNMENT 4
#endif	/* _LP64 */

/*
 * The define the size of the chunks in which memory will be allocated to
 * hold the encodings tables.  The maximum amount of memory that can be used
 * to store the encodings will be L_CHUNK times the largest integer that can
 * fit in an unsigned int on this machine.
 */

#define	L_CHUNK 256

/*
 * The macro L_ALIGN (variable, boundary) aligns the variable to the
 * specified boundary.
 *
 * The definition below works ONLY IF a long is large enough to hold a pointer
 * on this machine.  Otherwise, the definition must be adapted to the machine
 * on which this software is running.
 */

#define	L_ALIGN(variable, boundary) \
	variable += ((unsigned long) variable)%boundary;

/*
 * The following definition is the maximum length of a keyword and its value
 * in the label encodings file.  This is also the maximum length of
 * sensitivity labels and word combinations in the encodings file.
 */

#define	MAX_ENCODINGS_LINE_LENGTH 256

/*
 * The following typedef is for a declarator for storing the internal form of
 * a classification.
 */

typedef	int16_t   CLASSIFICATION;

/*
 * The following definitions are for declarators and operations on
 * compartment and marking bit masks.  The operations perform normal bit
 * manipulation and testing.  Only needed operations appear below, so not all
 * operations are defined for both compartments and markings.
 *
 * COMPARTMENTS must be the same size as COMPARTMENT_MASKs, and MARKINGS must be
 * the same size as MARKING_MASKs.
 *
 * COMPARTMENT_MASKs and MARKING_MASKs can be of any type desired, but
 * COMPARTMENTS_SIZE and MARKINGS_SIZE must be defined such that:
 *
 * COMPARTMENTS_SIZE * sizeof(COMPARTMENT_MASK) is the amount of memory (in
 * bytes) that a COMPARTMENT_MASK occupies, and
 *
 * MARKINGS_SIZE * sizeof(MARKING_MASK) is the amount of memory (in bytes) that
 * a MARKING_MASKs occupies.
 *
 */

struct l_256_bits {
	uint32_t   l_1;
	uint32_t   l_2;
	uint32_t   l_3;
	uint32_t   l_4;
	uint32_t   l_5;
	uint32_t   l_6;
	uint32_t   l_7;
	uint32_t   l_8;
};

typedef struct l_256_bits COMPARTMENT_MASK;
typedef struct l_256_bits MARKING_MASK;

#define	COMPARTMENTS_SIZE 1
#define	MARKINGS_SIZE 1

/*
 * COMPARTMENT_MASK modification operations.  The first argument is modified.
 */

/*
 * Zero COMPARTMENT_MASK c.
 */

#define	COMPARTMENT_MASK_ZERO(c) \
	(c)->l_1 = 0; \
	(c)->l_2 = 0; \
	(c)->l_3 = 0; \
	(c)->l_4 = 0; \
	(c)->l_5 = 0; \
	(c)->l_6 = 0; \
	(c)->l_7 = 0; \
	(c)->l_8 = 0
/*
 * Copy COMPARTMENT_MASK c2 to COMPARTMENT_MASK c1.
 */

#define	COMPARTMENT_MASK_COPY(c1, c2) *(c1) = *(c2)

/*
 * Combine the bits in COMPARTMENT_MASK c2 with those in COMPARTMENT_MASK c1.
 */

#define	COMPARTMENT_MASK_COMBINE(c1, c2) \
	(c1)->l_1 |= (c2)->l_1; \
	(c1)->l_2 |= (c2)->l_2; \
	(c1)->l_3 |= (c2)->l_3; \
	(c1)->l_4 |= (c2)->l_4; \
	(c1)->l_5 |= (c2)->l_5; \
	(c1)->l_6 |= (c2)->l_6; \
	(c1)->l_7 |= (c2)->l_7; \
	(c1)->l_8 |= (c2)->l_8

/*
 * Set bit b (numbered from the left starting at 0) in COMPARTMENT_MASK c.
 */

#ifdef	TSOL
#define	COMPARTMENT_MASK_BIT_SET(c, b) \
	((char *)(&c))[b/BITS_PER_BYTE] |= \
	LEFTMOST_BIT_IN_BYTE >> (b%BITS_PER_BYTE)
#else	/* !TSOL */
#define	COMPARTMENT_MASK_BIT_SET(c, b) \
	((unsigned long *)(&c))[b/32] |= \
	((unsigned long) 020000000000 >> (b%32))
#endif	/* TSOL */

/*
 * COMPARTMENT_MASK testing operations.
 */

/*
 * Return TRUE iff the  bits in COMPARTMENT_MASK c1 are all on in
 * COMPARTMENT_MASK c2.
 */

#define	COMPARTMENT_MASK_IN(c1, c2) \
	((c1)->l_1 == ((c2)->l_1&(c1)->l_1) && \
	(c1)->l_2 == ((c2)->l_2&(c1)->l_2) && \
	(c1)->l_3 == ((c2)->l_3&(c1)->l_3) && \
	(c1)->l_4 == ((c2)->l_4&(c1)->l_4) && \
	(c1)->l_5 == ((c2)->l_5&(c1)->l_5) && \
	(c1)->l_6 == ((c2)->l_6&(c1)->l_6) && \
	(c1)->l_7 == ((c2)->l_7&(c1)->l_7) && \
	(c1)->l_8 == ((c2)->l_8&(c1)->l_8))

/*
 * Return TRUE iff the bits in COMPARTMENT_MASK c1 dominate those in
 * COMPARTMENT_MASK c2.
 */

#define	COMPARTMENT_MASK_DOMINATE(c1, c2) \
	((c2)->l_1 == ((c1)->l_1&(c2)->l_1) && \
	(c2)->l_2 == ((c1)->l_2&(c2)->l_2) && \
	(c2)->l_3 == ((c1)->l_3&(c2)->l_3) && \
	(c2)->l_4 == ((c1)->l_4&(c2)->l_4) && \
	(c2)->l_5 == ((c1)->l_5&(c2)->l_5) && \
	(c2)->l_6 == ((c1)->l_6&(c2)->l_6) && \
	(c2)->l_7 == ((c1)->l_7&(c2)->l_7) && \
	(c2)->l_8 == ((c1)->l_8&(c2)->l_8))

/*
 * Return TRUE iff COMPARTMENT_MASKs c1 and c2 are equal.
 */

#define	COMPARTMENT_MASK_EQUAL(c1, c2) \
	((c1)->l_1 == (c2)->l_1 && \
	(c1)->l_2 == (c2)->l_2 && \
	(c1)->l_3 == (c2)->l_3 && \
	(c1)->l_4 == (c2)->l_4 && \
	(c1)->l_5 == (c2)->l_5 && \
	(c1)->l_6 == (c2)->l_6 && \
	(c1)->l_7 == (c2)->l_7 && \
	(c1)->l_8 == (c2)->l_8)

/*
 * MARKING_MASK modification operations.  The first argument is modified.
 */

/*
 * Zero MARKING_MASK m.
 */

#define	MARKING_MASK_ZERO(m) \
	(m)->l_1 = 0; \
	(m)->l_2 = 0; \
	(m)->l_3 = 0; \
	(m)->l_4 = 0; \
	(m)->l_5 = 0; \
	(m)->l_6 = 0; \
	(m)->l_7 = 0; \
	(m)->l_8 = 0

/*
 * Copy MARKING_MASK m2 to MARKING_MASK m1.
 */

#define	MARKING_MASK_COPY(m1, m2) *(m1) = *(m2)

/*
 * Combine the bits in MARKING_MASK m2 with those in MARKING_MASK m1.
 */

#define	MARKING_MASK_COMBINE(m1, m2) \
	(m1)->l_1 |= (m2)->l_1; \
	(m1)->l_2 |= (m2)->l_2; \
	(m1)->l_3 |= (m2)->l_3; \
	(m1)->l_4 |= (m2)->l_4; \
	(m1)->l_5 |= (m2)->l_5; \
	(m1)->l_6 |= (m2)->l_6; \
	(m1)->l_7 |= (m2)->l_7; \
	(m1)->l_8 |= (m2)->l_8

/*
 * Set bit b (numbered from the left starting at 0) in MARKING_MASK m.
 */

#ifdef	TSOL
#define	MARKING_MASK_BIT_SET(m, b) \
	((char *)(&m))[b/BITS_PER_BYTE] |= \
	LEFTMOST_BIT_IN_BYTE >> (b%BITS_PER_BYTE)
#else	/* !TSOL */
#define	MARKING_MASK_BIT_SET(m, b) \
	((unsigned long *)(&m))[b/32] |= \
	((unsigned long) 020000000000 >> (b%32))
#endif	/* TSOL */

/*
 * MARKING_MASK testing operations.
 */

/*
 * Return TRUE iff the  bits in MARKING_MASK m1 are all on in MARKING_MASK
 * m2.
 */

#define	MARKING_MASK_IN(m1, m2) \
	((m1)->l_1 == ((m2)->l_1&(m1)->l_1) && \
	(m1)->l_2 == ((m2)->l_2&(m1)->l_2) && \
	(m1)->l_3 == ((m2)->l_3&(m1)->l_3) && \
	(m1)->l_4 == ((m2)->l_4&(m1)->l_4) && \
	(m1)->l_5 == ((m2)->l_5&(m1)->l_5) && \
	(m1)->l_6 == ((m2)->l_6&(m1)->l_6) && \
	(m1)->l_7 == ((m2)->l_7&(m1)->l_7) && \
	(m1)->l_8 == ((m2)->l_8&(m1)->l_8))

/*
 * Return TRUE iff the bits in MARKING_MASK m1 dominate those in MARKING_MASK
 * m2.
 */

#define	MARKING_MASK_DOMINATE(m1, m2) \
	((m2)->l_1 == ((m1)->l_1&(m2)->l_1) && \
	(m2)->l_2 == ((m1)->l_2&(m2)->l_2) && \
	(m2)->l_3 == ((m1)->l_3&(m2)->l_3) && \
	(m2)->l_4 == ((m1)->l_4&(m2)->l_4) && \
	(m2)->l_5 == ((m1)->l_5&(m2)->l_5) && \
	(m2)->l_6 == ((m1)->l_6&(m2)->l_6) && \
	(m2)->l_7 == ((m1)->l_7&(m2)->l_7) && \
	(m2)->l_8 == ((m1)->l_8&(m2)->l_8))

/*
 * Return TRUE iff MARKING_MASKs m1 and m2 are equal.
 */

#define	MARKING_MASK_EQUAL(m1, m2) \
	((m1)->l_1 == (m2)->l_1 && \
	(m1)->l_2 == (m2)->l_2 && \
	(m1)->l_3 == (m2)->l_3 && \
	(m1)->l_4 == (m2)->l_4 && \
	(m1)->l_5 == (m2)->l_5 && \
	(m1)->l_6 == (m2)->l_6 && \
	(m1)->l_7 == (m2)->l_7 && \
	(m1)->l_8 == (m2)->l_8)

/*
 * The following definitions are for declarators and operations on
 * compartment and marking bits (as opposed to compartment or marking MASKS.
 * The semantics of declaring something as COMPARTMENTS or MARKINGS is that
 * the resultant bits can contain both regular and inverse compartments or
 * markings, and the operations defined below properly operate on these bit
 * strings, taking into account regular and inverse bits.
 *
 * Those operations that have the same meaning for COMPARTMENTS and
 * COMPARTMENT_MASKS are defined below in terms of the COMPARTMENT_MASK
 * operations above.
 *
 * Those operations that have the same meaning for MARKINGS and MARKING_MASKS
 * are defined below in terms of the MARKING_MASK operations above.
 */

typedef struct l_256_bits COMPARTMENTS;
typedef struct l_256_bits MARKINGS;

/*
 * COMPARTMENTS modification operations.  The first argument is modified.
 */

/*
 * Zero COMPARTMENTS c.
 */

#define	COMPARTMENTS_ZERO(c) COMPARTMENT_MASK_ZERO(c)

/*
 * Copy COMPARTMENTS c2 to COMPARTMENTS c1.
 */

#define	COMPARTMENTS_COPY(c1, c2) COMPARTMENT_MASK_COPY(c1, c2)

/*
 * Combine the COMPARTMENTS in c2 with those in c1.
 */

#define	COMPARTMENTS_COMBINE(c1, c2) COMPARTMENT_MASK_COMBINE(c1, c2)

/*
 * Bitwise AND the COMPARTMENTS in c2 with those in c1.
 */

#define	COMPARTMENTS_AND(c1, c2) \
	(c1)->l_1 &= (c2)->l_1; \
	(c1)->l_2 &= (c2)->l_2; \
	(c1)->l_3 &= (c2)->l_3; \
	(c1)->l_4 &= (c2)->l_4; \
	(c1)->l_5 &= (c2)->l_5; \
	(c1)->l_6 &= (c2)->l_6; \
	(c1)->l_7 &= (c2)->l_7; \
	(c1)->l_8 &= (c2)->l_8

/*
 * Bitwise XOR the COMPARTMENTS in c2 with those in c1.
 */

#define	COMPARTMENTS_XOR(c1, c2) \
	(c1)->l_1 ^= (c2)->l_1; \
	(c1)->l_2 ^= (c2)->l_2; \
	(c1)->l_3 ^= (c2)->l_3; \
	(c1)->l_4 ^= (c2)->l_4; \
	(c1)->l_5 ^= (c2)->l_5; \
	(c1)->l_6 ^= (c2)->l_6; \
	(c1)->l_7 ^= (c2)->l_7; \
	(c1)->l_8 ^= (c2)->l_8

/*
 * Set bit b (numbered from the left starting at 0) in COMPARTMENTS c.
 */

#define	COMPARTMENTS_BIT_SET(c, b) COMPARTMENT_MASK_BIT_SET(c, b)

/*
 * Logically set the COMPARTMENTS specified by COMPARTMENT_MASK mask and
 * COMPARTMENTS c2 in COMPARTMENTS c1.  Logically set means that those bits
 * specified by mask will be set to the SAME VALUE in c1 as they are in c2.
 */

#define	COMPARTMENTS_SET(c1, mask, c2) \
	(c1)->l_1 = ((c1)->l_1&~((mask)->l_1)) | (c2)->l_1; \
	(c1)->l_2 = ((c1)->l_2&~((mask)->l_2)) | (c2)->l_2; \
	(c1)->l_3 = ((c1)->l_3&~((mask)->l_3)) | (c2)->l_3; \
	(c1)->l_4 = ((c1)->l_4&~((mask)->l_4)) | (c2)->l_4; \
	(c1)->l_5 = ((c1)->l_5&~((mask)->l_5)) | (c2)->l_5; \
	(c1)->l_6 = ((c1)->l_6&~((mask)->l_6)) | (c2)->l_6; \
	(c1)->l_7 = ((c1)->l_7&~((mask)->l_7)) | (c2)->l_7; \
	(c1)->l_8 = ((c1)->l_8&~((mask)->l_8)) | (c2)->l_8

/*
 * COMPARTMENTS testing operations.
 */

/*
 * Return TRUE iff the compartments (inverse or normal) specified by the
 * COMPARTMENT_MASK mask and the COMPARTMENTS c2 are logically present in
 * COMPARTMENTS c1.  Logically present means that the bits specified in mask
 * must have the SAME VALUE in c1 as they do in c2.
 */

#define	COMPARTMENTS_IN(c1, mask, c2) \
	((c2)->l_1 == ((c1)->l_1&(mask)->l_1) && \
	(c2)->l_2 == ((c1)->l_2&(mask)->l_2) && \
	(c2)->l_3 == ((c1)->l_3&(mask)->l_3) && \
	(c2)->l_4 == ((c1)->l_4&(mask)->l_4) && \
	(c2)->l_5 == ((c1)->l_5&(mask)->l_5) && \
	(c2)->l_6 == ((c1)->l_6&(mask)->l_6) && \
	(c2)->l_7 == ((c1)->l_7&(mask)->l_7) && \
	(c2)->l_8 == ((c1)->l_8&(mask)->l_8))

/*
 * Return TRUE iff the COMPARTMENTS in c1 dominate the COMPARTMENTS in c2.
 */

#define	COMPARTMENTS_DOMINATE(c1, c2) COMPARTMENT_MASK_DOMINATE(c1, c2)

/*
 * Return TRUE iff COMPARTMENTs c1 and c2 have any on bits in common.
 */

#define	COMPARTMENTS_ANY_BITS_MATCH(c1, c2) \
	((c1)->l_1&(c2)->l_1 || \
	(c1)->l_2&(c2)->l_2 || \
	(c1)->l_3&(c2)->l_3 || \
	(c1)->l_4&(c2)->l_4 || \
	(c1)->l_5&(c2)->l_5 || \
	(c1)->l_6&(c2)->l_6 || \
	(c1)->l_7&(c2)->l_7 || \
	(c1)->l_8&(c2)->l_8)

/*
 * Return TRUE iff the COMPARTMENTS in c1 equal the COMPARTMENTS in c2.
 */

#define	COMPARTMENTS_EQUAL(c1, c2) COMPARTMENT_MASK_EQUAL(c1, c2)

/*
 * MARKINGS modification operations.  The first argument is modified.
 */

/*
 * Zero MARKINGS m.
 */

#define	MARKINGS_ZERO(m) MARKING_MASK_ZERO(m)

/*
 * Copy MARKINGS m2 to MARKINGS m1.
 */

#define	MARKINGS_COPY(m1, m2) MARKING_MASK_COPY(m1, m2)

/*
 * Combine MARKINGS m2 with MARKINGS m1.
 */

#define	MARKINGS_COMBINE(m1, m2) MARKING_MASK_COMBINE(m1, m2)

/*
 * Bitwise AND the MARKINGS in m2 with those in m1.
 */

#define	MARKINGS_AND(m1, m2) \
	(m1)->l_1 &= (m2)->l_1; \
	(m1)->l_2 &= (m2)->l_2; \
	(m1)->l_3 &= (m2)->l_3; \
	(m1)->l_4 &= (m2)->l_4; \
	(m1)->l_5 &= (m2)->l_5; \
	(m1)->l_6 &= (m2)->l_6; \
	(m1)->l_7 &= (m2)->l_7; \
	(m1)->l_8 &= (m2)->l_8

/*
 * Bitwise XOR the MARKINGS in m2 with those in m1.
 */

#define	MARKINGS_XOR(m1, m2) \
	(m1)->l_1 ^= (m2)->l_1; \
	(m1)->l_2 ^= (m2)->l_2; \
	(m1)->l_3 ^= (m2)->l_3; \
	(m1)->l_4 ^= (m2)->l_4; \
	(m1)->l_5 ^= (m2)->l_5; \
	(m1)->l_6 ^= (m2)->l_6; \
	(m1)->l_7 ^= (m2)->l_7; \
	(m1)->l_8 ^= (m2)->l_8

/*
 * Set bit b (numbered from the left starting at 0) in MARKINGS m.
 */

#define	MARKINGS_BIT_SET(m, b) MARKING_MASK_BIT_SET(m, b)

/*
 * Logically set the MARKINGS specified by MARKING_MASK mask and MARKINGS m2
 * in MARKINGS m1.  Logically set means that those bits specified by mask
 * will be set to the SAME VALUE in m1 as they are in m2.
 */

#define	MARKINGS_SET(m1, mask, m2) \
	(m1)->l_1 = ((m1)->l_1&~((mask)->l_1)) | (m2)->l_1; \
	(m1)->l_2 = ((m1)->l_2&~((mask)->l_2)) | (m2)->l_2; \
	(m1)->l_3 = ((m1)->l_3&~((mask)->l_3)) | (m2)->l_3; \
	(m1)->l_4 = ((m1)->l_4&~((mask)->l_4)) | (m2)->l_4; \
	(m1)->l_5 = ((m1)->l_5&~((mask)->l_5)) | (m2)->l_5; \
	(m1)->l_6 = ((m1)->l_6&~((mask)->l_6)) | (m2)->l_6; \
	(m1)->l_7 = ((m1)->l_7&~((mask)->l_7)) | (m2)->l_7; \
	(m1)->l_8 = ((m1)->l_8&~((mask)->l_8)) | (m2)->l_8

/*
 * MARKINGS testing operations.
 */

/*
 * Return TRUE iff the markings (inverse or normal) specified by the
 * MARKING_MASK mask and the MARKINGS m2 are logically present in MARKINGS
 * m1.  Logically present means that the bits specified in mask must have the
 * SAME VALUE in m1 as they do in m2.
 */

#define	MARKINGS_IN(m1, mask, m2) \
	((m2)->l_1 == ((m1)->l_1&(mask)->l_1) && \
	(m2)->l_2 == ((m1)->l_2&(mask)->l_2) && \
	(m2)->l_3 == ((m1)->l_3&(mask)->l_3) && \
	(m2)->l_4 == ((m1)->l_4&(mask)->l_4) && \
	(m2)->l_5 == ((m1)->l_5&(mask)->l_5) && \
	(m2)->l_6 == ((m1)->l_6&(mask)->l_6) && \
	(m2)->l_7 == ((m1)->l_7&(mask)->l_7) && \
	(m2)->l_8 == ((m1)->l_8&(mask)->l_8))

/*
 * Return TRUE iff the MARKINGS in m1 dominate the MARKINGS in m2.
 */

#define	MARKINGS_DOMINATE(m1, m2) MARKING_MASK_DOMINATE(m1, m2)

/*
 * Return TRUE iff MARKINGs m1 and m2 have any on bits in common.
 */

#define	MARKINGS_ANY_BITS_MATCH(m1, m2) \
	((m1)->l_1&(m2)->l_1 || \
	(m1)->l_2&(m2)->l_2 || \
	(m1)->l_3&(m2)->l_3 || \
	(m1)->l_4&(m2)->l_4 || \
	(m1)->l_5&(m2)->l_5 || \
	(m1)->l_6&(m2)->l_6 || \
	(m1)->l_7&(m2)->l_7 || \
	(m1)->l_8&(m2)->l_8)

/*
 * Return TRUE iff the MARKINGS in m1 equal the MARKINGS in m2.
 */

#define	MARKINGS_EQUAL(m1, m2) MARKING_MASK_EQUAL(m1, m2)



/*
 * ************************************************************************
 * NOTHING BELOW THIS POINT IN THE FILE SHOULD BE CHANGED BY USERS OF THE *
 * SUBROUTINES IN STD_LABELS.C, L_INIT.C, AND L_EOF.C			  *
 * ************************************************************************
 */

/*
 * The macro TABLES_RESERVE (type, number) is used by l_init to reserve space
 * to store data of type "type", with the number of bytes reserved equal to
 * "number" * sizeof ("type").  The space is reserved by incrementing the
 * variable size_tables to account for the amount of space needed.  Proper
 * alignment must be taken into account in the definition of this macro.  In
 * other words, size_tables must be aligned properly to hold the "type"
 * before it is incremented to account for the space taken up of the type.
 * The alignment is based on the definition L_ALIGNMENT above.
 *
 * TABLES_RESERVE should be used with "number" greater than one ONLY IF there
 * will be a corresponding call to TABLES_ALLOCATE (below) with the same
 * "number". Calls to TABLES_RESERVE must be matched with calls to
 * TABLES_ALLOCATE, in the same order.  Otherwise, alignment could be
 * improper.
 */

#define	TABLES_RESERVE(type, number) \
	L_ALIGN(size_tables, \
	(sizeof (type) < L_ALIGNMENT ? sizeof (type) : L_ALIGNMENT)); \
	size_tables += (sizeof (type) * number);

/*
 * The macro TABLES_ALLOCATE(type, variable, number) is used by l_init to
 * allocate space to store data of type "type" within an already-alloced
 * buffer.  The number of bytes allocated is equal to "number" * sizeof("type").
 * The variable tables (known to l_init), is a pointer into the buffer.  Tables
 * must be aligned properly to hold the "type".  Then, "variable" is set equal
 * to the aligned tables value before tables is incremented to account for the
 * amount of space to be allocated.  The alignment is based on the
 * definition L_ALIGNMENT above.
 *
 * TABLES_ALLOCATE should be used with "number" greater than one ONLY IF there
 * is a corresponding call to TABLES_RESERVE (above) with the same "number".
 * Calls to TABLES_ALLOCATE must be matched with calls to TABLES_RESERVE, in
 * the same order.  Otherwise, alignment could be improper.
 */

#define	TABLES_ALLOCATE(type, variable, number) \
	L_ALIGN(tables, \
	(sizeof (type) < L_ALIGNMENT ? sizeof (type) : L_ALIGNMENT)); \
	variable = (type *) tables; \
	tables += sizeof (type) *(number);

/*
 * A type that refers to any of the types: COMPARTMENT_MASK, COMPARTMENTS,
 * MARKING_MASK, MARKINGS, short, or char.  This union is used in declaring
 * arguments to parse_bits, which modifies any of the above types of bit
 * strings.
 */

typedef union {
	COMPARTMENT_MASK l_cm;
	COMPARTMENTS	l_c;
	MARKING_MASK	l_mm;
	MARKINGS	l_m;
	short		l_short;
	char		l_char;
} BIT_STRING;

/*
 * Tables relating to classifications and definitions useful for processing
 * the tables. These tables apply to all types of labels.
 */

struct l_information_label {
	CLASSIFICATION	l_classification;
	COMPARTMENTS	*l_compartments;
	MARKINGS	*l_markings;
};

extern char   **l_long_classification;	/* long name of each classification */
extern char   **l_short_classification;	/* short name of each classification */
				/* alternate name of each classification */
extern char   **l_alternate_classification;
extern struct l_information_label **l_lc_name_information_label;
				/* long classification name information label */
extern struct l_information_label **l_sc_name_information_label;
			/* short classification name information label */
extern struct l_information_label **l_ac_name_information_label;
			/* alternate classification name information label */
extern MARKINGS **l_in_markings;	/* initial markings for each class */
				/* initial compartments for each class */
extern COMPARTMENTS **l_in_compartments;

/*
 * classification value meaning label not set yet
 */
#define	NO_LABEL	-1
/*
 * classification value tells l_parse or l_b_parse to do without
 * any error correction
 */
#define	FULL_PARSE	-2
/*
 * l_parse and l_b_parse return code meaning parse worked
 */
#define	L_GOOD_LABEL	-1
/*
 * l_parse and l_b_parse return code meaning passed classification,
 * minimum classification, or maximum classification were invalid
 */
#define	L_BAD_CLASSIFICATION -2
/*
 * l_parse return code from full parse mode, meaning input could be
 * understood, but did not represent a valid label
 */
#define	L_BAD_LABEL	-3
/*
 * arg for l_convert sez don't output class
 */
#define	NO_CLASSIFICATION ((char **)0)

/*
 * The following declarations are for the accreditation range specification,
 * which applies only to sensitivity labels.  An accreditation range
 * structure is present for each valid classification.  Each accreditation
 * range specification is of one of four types, as defined by l_ar_type.
 * Each specification can state that 1) no compartment combinations are valid
 * with this classification (NONE_VALID), 2) that all compartment
 * combinations are valid for this classification except those listed by
 * l_ar_start and l_ar_end (ALL_VALID_EXCEPT), 3) that all compartment
 * combinations are valid for this classification, or that 4) the only valid
 * compartment combinations for this classification are those listed by
 * l_ar_start and l_ar_end.
 */

struct l_accreditation_range {
				/* type of range for this classification */
	short		l_ar_type;
	COMPARTMENTS	*l_ar_start;	/* ptr to start of COMPARTMENTS list */
				/* ptr beyond end of COMPARTMENTS list */
	COMPARTMENTS	*l_ar_end;
};

extern struct l_accreditation_range *l_accreditation_range;

/*
 * Acceptable values for l_ar_type.
 */

#define	L_NONE_VALID	   0
#define	L_ALL_VALID_EXCEPT 1
#define	L_ALL_VALID	   2
#define	L_ONLY_VALID	   3

/*
 * A linked list structure for storing the optional input names for each
 * word.
 */

struct l_input_name {
	char			*name_string;
	struct l_input_name	*next_input_name;
};

#define	NO_MORE_INPUT_NAMES ((struct l_input_name *)0)

/*
 * Tables relating to words in labels other than classification, and
 * definitions useful for processing the tables.
 */

struct l_word {			/* the structure of each word table entry */
	char		*l_w_output_name;	/* used for output only */
				/* short output name--used for output only */
	char		*l_w_soutput_name;
	char		*l_w_long_name;		/* used for input only */
	char		*l_w_short_name;	/* used for input only */
					/* ptr to linked list of input names */
	struct l_input_name *l_w_input_name;
					/* long name information label */
	struct l_information_label *l_w_ln_information_label;
					/* short name information label */
	struct l_information_label *l_w_sn_information_label;
				/* min classification needed for this entry */
	CLASSIFICATION	l_w_min_class;
			/* min classification needed to output this entry */
	CLASSIFICATION	l_w_output_min_class;
				/* max classification allowed for this entry */
	CLASSIFICATION	l_w_max_class;
			/* max classification needed to output this entry */
	CLASSIFICATION	l_w_output_max_class;
				/* compartment bit mask for this entry */
	COMPARTMENT_MASK *l_w_cm_mask;
	COMPARTMENTS	*l_w_cm_value;	/* compartments value for this entry */
	MARKING_MASK	*l_w_mk_mask;	/* marking bit mask for this entry */
	MARKINGS	*l_w_mk_value;	/* markings value for this entry */
					/*
					 * l_words index of another word
					 * needed to prefix entry, or
					 * L_IS_PREFIX if this is a prefix
					 * entry itself
					 */
	short		l_w_prefix;
					/*
					 * l_words index of another word
					 * needed to suffix entry, or
					 * L_IS_SUFFIX if this is a SUFFIX
					 * entry itself
					 */
	short		l_w_suffix;

	short		l_w_flags;	/* flags for each entry */
				/* set of flags indicating type of word */
	short		l_w_type;
				/* index of word this word is exact alias for */
	short		l_w_exact_alias;
};

/*
 * l_w_flags values
 */
			/* l_w_flags value to match all l_word entries */
#define	ALL_ENTRIES	0
		/* l_w_flags value to match only access related entries */
#define	ACCESS_RELATED	1

/*
 * l_w_type values
 */

#define	HAS_COMPARTMENTS	0x01	/* word has compartments specified */
#define	COMPARTMENTS_INVERSE	0x02	/* word has inverse compartments */
#define	HAS_ZERO_MARKINGS	0x04	/* word has zero markings */
			/* word requires prefix with compartments or markings */
#define	SPECIAL_INVERSE		0x08
			/* SPECIAL_INVERSE word has inverse compartments */
#define	SPECIAL_COMPARTMENTS_INVERSE 0x10

/*
 * l_w_exact_alias special value
 */

#define	NO_EXACT_ALIAS -1

	/* the structure of required and invalid combination table entries */
struct l_word_pair {
	short		l_word1;
	short		l_word2;
};

		/* the structure of the combination constraints table */
struct l_constraints {
				/* type of constraint: NOT_WITH or ONLY_WITH */
	short		l_c_type;
	short		*l_c_first_list;	/* start of the first list */
	short		*l_c_second_list;	/* ptr to second of two lists */
					/* ptr beyond end of second list */
	short		*l_c_end_second_list;
};

#define	NOT_WITH 	0	/* constraint type */
#define	ONLY_WITH	1	/* constraint type */

/* the information about each type of label (other than classification */
struct l_tables {
				/* total number of entries in l_words table */
	unsigned int	l_num_entries;
					/* first non-prefix/suffix entry */
	int		l_first_main_entry;
		/* maximum length of label l_converted with this l_tables */
	unsigned int	l_max_length;
	struct l_word	*l_words;	/* the l_word table itself */
				/* table of required combos of two words */
	struct l_word_pair *l_required_combinations;
							/* end of above table */
	struct l_word_pair *l_end_required_combinations;
					/* table of combination constraints */
	struct l_constraints *l_constraints;
	struct l_constraints *l_end_constraints;	/* end of above table */
};

/*
 * The l_tables for each type of label or printer banner output string.
 */

						/* information label tables */
extern struct l_tables *l_information_label_tables;
						/* sensitivity label tables */
extern struct l_tables *l_sensitivity_label_tables;
extern struct l_tables *l_clearance_tables;	/* clearance tables */
extern struct l_tables *l_channel_tables;	/* handle via channel tables */
extern struct l_tables *l_printer_banner_tables;   /* printer banner tables */

/*
 * The following define is for a test to determine whether a particular word
 * in the word table is visible given a specified maximum classification and
 * compartments. This define assumes that the variable l_tables is a pointer
 * to the appropriate label tables, and evaluates to TRUE iff index i of the
 * word table is visible given classification cl and compartments cm.
 */

#define	WORD_VISIBLE(i, cl, cm) \
	((cl) >= l_tables->l_words[i].l_w_min_class && \
	COMPARTMENTS_DOMINATE(cm, l_tables->l_words[i].l_w_cm_value))

/*
 * The following three definitions are values for the l_w_prefix and
 * l_w_suffix entries of the word structure (respectively), indicating that
 * the entry in the word table IS a prefix or suffix as opposed to an entry
 * that REQUIRES a prefix or suffix (if value >= 0) or neither IS nor
 * requires a prefix or suffix (value = -1).
 */

#define	L_IS_PREFIX	-2		/* indicates an entry is a prefix */
#define	L_IS_SUFFIX	-2		/* indicates an entry is a suffix */
		/* indicates an entry does not need a prefix or suffix */
#define	L_NONE		-1

/*
 * The following variables contain the values for various system parameters
 * and useful constant values and temporary compartment and marking
 * variables.
 */

struct l_sensitivity_label {
	CLASSIFICATION	l_classification;
	COMPARTMENTS	*l_compartments;
};

		/* the version string for this version of the encodings */
extern char    *l_version;
					/* lowest classification encoded */
extern CLASSIFICATION *l_min_classification;
extern CLASSIFICATION *l_classification_protect_as;	/* for printer banner */
					/* lowest clearance possible */
extern struct l_sensitivity_label *l_lo_clearance;
							/* lowest SL possible */
extern struct l_sensitivity_label *l_lo_sensitivity_label;
						/* highest SL possible */
extern struct l_sensitivity_label *l_hi_sensitivity_label;
extern MARKINGS *l_hi_markings;			/* highest markings possible */
extern COMPARTMENTS *l_li_compartments;	/* lowest IL (li) compartments */
extern MARKINGS *l_li_markings;			/* lowest IL (li) markings */
			/* a source of zero compartment bits for comparisons */
extern COMPARTMENTS *l_0_compartments;
			/* a source of zero marking bits for comparisons */
extern MARKINGS *l_0_markings;
				/* a temporary compartment bit variable */
extern COMPARTMENTS *l_t_compartments;
extern MARKINGS *l_t_markings;	/* a temporary marking bit variable */
			/* a second temporary compartment bit variable */
extern COMPARTMENTS *l_t2_compartments;
extern MARKINGS *l_t2_markings;	/* a second temporary marking bit variable */
				/* a third temporary compartment bit variable */
extern COMPARTMENTS *l_t3_compartments;
extern MARKINGS *l_t3_markings;	/* a third temporary marking bit variable */
			/* a fourth temporary compartment bit variable */
extern COMPARTMENTS *l_t4_compartments;
extern MARKINGS *l_t4_markings;	/* a fourth temporary marking bit variable */
				/* a fifth temporary compartment bit variable */
extern COMPARTMENTS *l_t5_compartments;
extern MARKINGS *l_t5_markings;	/* a fifth temporary marking bit variable */
extern COMPARTMENTS *l_iv_compartments;	/* inverse compartment bit mask */
extern MARKINGS *l_iv_markings;	/* inverse marking bit mask */

/*
 * The following definitions are useful when calling the convert subroutines.
 * NO_PARSE_TABLE, as an argument to l_convert, indicates that the caller
 * does not want l_convert to return a parse table.  SHORT_WORDS and
 * LONG_WORDS, when used as the last argument to l_convert or l_b_convert,
 * indicate whether the short or long names of words should be output.
 * LONG_WORDS uses the l_w_output_name, and SHORT_WORDS uses the
 * l_w_short_name if present, else the l_w_output_name.  NO_INFORMATION_LABEL
 * indicates that the information label of the human-readable output need not
 * be returned.
 */

#define	NO_PARSE_TABLE	((char *)0)
#define	SHORT_WORDS	1
#define	LONG_WORDS	0
#define	NO_INFORMATION_LABEL ((struct l_information_label *)0)

/*
 * Useful definitions used in l_init.c and std_labels.c.
 */

#define	L_MAX(a, b) (a > b ? a : b)
#define	L_MIN(a, b) (a < b ? a : b)

#ifndef TRUE
#define	TRUE 1
#endif

#ifndef FALSE
#define	FALSE 0
#endif

/*
 * Prototype for the subroutine all std_labels.c and l_init.c subroutines
 * call to make sure the encodings are initialized.
 */

/*
 * external subroutine that determines whether encodings have been initialized
 */

extern int
l_encodings_initialized(/* void */);

/*
 * Prototypes for external label subroutines in std_labels.c.
 */

extern int l_parse(/*
    const char *input,
    CLASSIFICATION *class_ptr,
    COMPARTMENTS *comps_ptr,
    MARKINGS *marks_ptr,
    const struct l_tables *l_tables,
    const CLASSIFICATION min_class,
    const COMPARTMENTS *min_comps_ptr,
    const CLASSIFICATION max_class,
    const COMPARTMENTS *max_comps_ptr */);

extern int l_convert(/*
    char *string,
    const CLASSIFICATION class,
    const char *l_classification[],
    const COMPARTMENTS *comps_ptr,
    const MARKINGS *marks_ptr,
    const struct l_tables *l_tables,
    char *caller_parse_table,
    const int use_short_names,
    const int flags,
    const int check_validity,
    struct l_information_label *information_label */);

extern int l_b_parse(/*
    const char *input,
    CLASSIFICATION *iclass_ptr,
    COMPARTMENTS *icomps_ptr,
    MARKINGS *imarks_ptr,
    CLASSIFICATION *sclass_ptr,
    COMPARTMENTS *scomps_ptr,
    const CLASSIFICATION max_class,
    const COMPARTMENTS *max_comps_ptr,
    const int allow_sl_changing */);

extern int l_b_convert(/*
    char *string,
    const CLASSIFICATION iclass,
    const COMPARTMENTS *icomps_ptr,
    const MARKINGS *imarks_ptr,
    char *iparse_table,
    const CLASSIFICATION sclass,
    const COMPARTMENTS *scomps_ptr,
    char *sparse_table,
    const int use_short_names,
    const int check_validity,
    struct l_information_label *information_label */);

extern int l_in_accreditation_range(/*
    const CLASSIFICATION class,
    const COMPARTMENTS *comps_ptr */);

extern int l_valid(/*
    const CLASSIFICATION class,
    const COMPARTMENTS *comps_ptr,
    const MARKINGS *marks_ptr,
    const struct l_tables *l_tables,
    const int flags */);

extern int l_changeability(/*
    char *class_changeable,
    char *word_changeable,
    const char *parse_table,
    const CLASSIFICATION class,
    const register struct l_tables *l_tables,
    const CLASSIFICATION min_class,
    const COMPARTMENTS *min_comps_ptr,
    const CLASSIFICATION max_class,
    const COMPARTMENTS *max_comps_ptr */);

extern int l_visibility(/*
    char *class_visible,
    char *word_visible,
    const struct l_tables *l_tables,
    const CLASSIFICATION min_class,
    const COMPARTMENTS *min_comps_ptr,
    const CLASSIFICATION max_class,
    const COMPARTMENTS *max_comps_ptr */);

#ifdef	TSOL
extern void float_il(
    struct l_information_label *target_il,
    struct l_information_label *source_il);
#endif	/* TSOL */

/*
 * Prototypes for external subroutines in l_init.c.
 */

extern void
l_cleanup(/* void */);

extern int l_init(/*
    const char *filename,
    const unsigned int maximum_class,
    const unsigned int maximum_comps,
    const unsigned int maximum_mark */);

extern int l_next_keyword(/* const char *keywords[] */);


/*
 * External variables in l_init.c needed by vendor-modified l_eof.c.
 */

extern FILE	*l_encodings_file_ptr;	/* file ptr for file to read from */
					/* buffer to read file data into */
extern char	l_buffer[MAX_ENCODINGS_LINE_LENGTH];
extern char	*l_scan_ptr;	/* ptr into l_buffer of current scan point */
extern char	*l_dp;		/* ptr to data returned for each keyword */

#ifdef	__cplusplus
}
#endif

#endif	/* !_STD_LABELS_H */
