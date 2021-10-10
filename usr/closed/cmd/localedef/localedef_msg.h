/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LOCALEDEF_LOCALEDEF_MSG_H
#define	_LOCALEDEF_LOCALEDEF_MSG_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#define	ERR_TOO_LONG_BYTES	\
	"Specified bytes are too long.\n"

#define	ERR_OPEN_READ	\
	"Could not open '%s' for read.\n"

#define	ERR_INTERNAL	\
	"Internal error. [file %s - line %d].\n"

#define	ERR_MEM_ALLOC_FAIL	\
	"Memory allocation failure: [line %d -- module %s].\n"

#define	ERR_WRT_PERM	\
	"Could not open temporary file '%s' for write.\n"

#define	ERR_METHOD_FAILED	\
	"%s failed for %s.\n"

#define	ERR_NOMATCH_MBTOWC_WCTOMB	\
	"mbtowc()<-->wctomb() failed for %s.\n"

#define	ERR_PC_NOMATCH_WC	\
	"pc's don't macth for %s.\n"

#define	ERR_EUCPC_NOMATCH_WC	\
	"eucpc's don't macth between mbtowc() and "\
	"eucpctowc() for character %s.\n"

#define	ERR_WRONG_SYM_TYPE	\
	"The symbol '%s' is not the correct type.\n"

#define	ERR_N_SARGS	\
	"%d arguments expected, but %d arguments received.\n"

#define	ERR_ILL_RANGE_SPEC	\
	"Illegal limit in range specification.\n"

#define	ERR_CHAR_TOO_LONG	\
	"The '%s' character is longer than <mb_cur_max>.\n"

#define	ERR_SYM_UNDEF	\
	"The '%s' character is undefined.\n"

#define	ERR_INVAL_COLL_RANGE	\
	"The start of the range must be numerically less than "\
	"the end of the range.\n"

#define	ERR_INVALID_SYM_RNG	\
	"The symbol range containing %s and %s is incorrectly formatted.\n"

#define	ERR_BAD_STR_FMT	\
	"Illegal character reference or escape sequence in '%s'.\n"

#define	ERR_PC_COLLISION	\
	"Different names specified for the same character '%d'.\n"

#define	ERR_DUP_CHR_SYMBOL	\
	"The character symbol '%s' has already been specified.\n"

#define	ERR_NO_UNDEFINED	\
	"The following character in the codeset is not specified in "\
	"the collation order list: "

#define	ERR_COLLEL_NO_WEIGHTS	\
	"Weights for the collating-element %s aren't specified.\n"

#define	ERR_COLLEL_NO_WEIGHT	\
	"Weight for the collating-element %s (order %d) isn't specified.\n"

#define	ERR_ILL_DEC_CONST	\
	"Illegal decimal constant '%s'.\n"

#define	ERR_ILL_OCT_CONST	\
	"Illegal octal constant '%s'.\n"

#define	ERR_ILL_HEX_CONST	\
	"Illegal hexadecimal constant '%s'.\n"

#define	ERR_MISSING_QUOTE	\
	"Missing closing quote in string '%s'.\n"

#define	ERR_ILL_CHAR	\
	"Illegal character '%c' in input file.  Character will be ignored.\n"

#define	ERR_UNEXPECTED_EOF \
	"Unexpected end of file seen after the escape character.\n"

#define	ERR_ESC_CHAR_MISSING	\
	"Character for escape_char statement missing.  Statement ignored.\n"

#define	ERR_COM_CHAR_MISSING	\
	"Character for <comment_char> statement missing.  Statement ignored.\n"

#define	ERR_CHAR_NOT_PCS	\
	"'%c' is not a POSIX Portable Character.  Statement ignored.\n"

#define	ERR_ILL_CHAR_SYM	\
	"The character symbol '%s' is missing the closing '>'. "\
	"The '>' will be added.\n"

#define	ERR_UNKNOWN_KWD	\
	"Unrecognized keyword '%s' statement ignored.\n"

#define	ERR_UNDEF_RANGE_SYM	\
	"The character symbol '%s' is undefined in the range '%s...%s'. "\
	"The symbol will be ignored.\n"

#define	ERR_UNSUP_ENC	\
	"The encoding specified for the '%s' character is unsupported.\n"

#define	ERR_DUP_COLL_SPEC	\
	"Multiple weight specification found for the same collating symbol "\
	"or character code: %s\n"

#define	ERR_DUP_COLL_SPEC_IGN \
	"The character '%s' has already been assigned a weight. "\
	"Specification ignored.\n"

#define	ERR_DUP_COLL_RNG_SPEC	\
	"Multiple weight specification found for character %s "\
	"in the range %s...%s\n"

#define	ERR_DUP_COLL_RNG_SPEC_IGN \
	"The character %s in range %s...%s already has a collation weight. "\
	"Range ignored.\n"

#define	ERR_WARN_SUPPRESSED \
	"localedef [WARNING]: %d warning messages are suppressed.\n"

#define	ERR_TOUPPER_NOT_OPT	\
	"No toupper section defined for this locale sourcefile.\n"

#define	ERR_CODESET_DEP	\
	"The use of the \"...\" keyword assumes that the codeset is "\
	"contiguous between the two range endpoints specified.\n"

#define	ERR_ILL_COLL_SUBS	\
	"The collation substitution %s%s contains a symbol which is not "\
	"a character.\n"

#define	ERR_FORWARD_REF	\
	"The symbol %s referenced has not yet been specified in the "\
	"collation order.\n"

#define	ERR_CYCLIC_SYM_REF \
	"Cyclic symbol reference for %s has been found.\n"

#define	ERR_USAGE	\
	"Usage: localedef [-csdvw] [-m [lp64|ilp32]] [-x extfile] "\
	"[-f charmap] [-i locsrc] [-W cc,opts] [-L ld opts] "\
	"[-P tool path] [-u code_set_name] locname\n"

#define	ERR_ERROR	\
	"localedef [ERROR]: FILE: %s, LINE: %d, CHAR: %d\n"

#define	ERR_WARNING	\
	"localedef [WARNING]: FILE: %s, LINE: %d, CHAR: %d\n"

#define	ERR_SYNTAX	\
	"localedef [ERROR]: FILE: %s, LINE: %d, CHAR: %d\n"\
	"Syntax Error.\n"

#define	ERR_TOO_MANY_ORDERS	\
	"Specific collation weight assignment is not valid when no sort "\
	"keywords have been specified.\n"

#define	ERR_BAD_CHDR	\
	"Required header files sys/localedef.h and sys/lc_core.h are "\
	"the wrong version or are missing.\n"

#define	ERR_INV_MB_CUR_MIN	\
	"The <mb_cur_min> keyword must be defined as 1, defined as %d.\n"

#define	ERR_INV_CODE_SET_NAME	\
	"The <code_set_name> must contain only characters from the portable "\
	"character set, %s is not valid.\n"

#define	ERR_FORWARD_BACKWARD	\
	"The collation directives forward and backward are "\
	"mutually exclusive.\n"

#define	ERR_TOO_MANY_ARGS	\
	"Received too many arguments, expected %d.\n"

#define	ERR_DUP_CATEGORY	\
	"The %s category has already been defined.\n"

#define	ERR_EMPTY_CAT	\
	"The %s category is empty.\n"

#define	ERR_UNDEF_CAT	\
	"Unrecognized category %s is not processed by localedef.\n"

#define	ERR_USER_DEF	\
	"The POSIX defined categories must appear before any unrecognized "\
	"categories.\n"

#define	ERR_DIGIT_FC_BAD	\
	"The file code for the digit %s is not one greater than "\
	"the file code for %s.\n"

#define	ERR_DIGIT_PC_BAD	\
	"The process code for the digit %s is not one greater than "\
	"the process code for %s.\n"

#define	ERR_DUP_COLL_SYM	\
	"The symbol %s has already been defined.  Ignorning definition as a "\
	"collating-symbol.\n"

#define	ERR_INVALID_CLASS	\
	"Locale does not conform to POSIX specifications for the LC_CTYPE "\
	"%s keyword.\n"

#define	ERR_INV_DIGIT	\
	"Locale specified other than '0', '1', '2', '3', '4', '5', "\
"'6', '7', '8', and '9' for LC_CTYPE digit keyword.\n"

#define	ERR_INV_XDIGIT	\
	"Locale specified other than '0', '1', '2', '3', '4', '5', "\
"'6', '7', '8', '9', 'a' through 'f', and 'A' through 'F' for "\
"LC_CTYPE xdigit keyword.\n"

#define	ERR_COLL_WEIGHTS	\
	"The number of operands to LC_COLLATE order "\
	"exceeds COLL_WEIGHTS_MAX.\n"

#define	ERR_NOSHELL	\
	"Unable to exec /usr/bin/sh.\n"

#define	ERR_LOAD_FAIL	\
	"Unable to load extension method for %s from file %s.\n"

#define	ERR_METHOD_REQUIRED	\
	"%s method must be defined explicitly.\n"

#define	ERR_CANNOT_LOAD_LOCALE	\
	"Unable to load locale \"%s\" for copy directive.\n"

#define	ERR_NAME_TOO_LONG	\
	"Locale name longer than PATH_MAX (%d).\n"

#define	ERR_MB_LEN_MAX_TOO_BIG	\
	"cswidth specification: Maximum character length can be no "\
	"bigger than %d.\n"

#define	ERR_TOO_MANY_CODESETS	\
	"Maximum number of codesets is %d.\n"

#define	ERR_MISSING_CHAR	\
	"The file code '%x' is missing from the charmap.\n"

#define	ERR_MISSING_CHARCLASS	\
	"\"charclass %s\" entry missing.\n"

#define	ERR_REDEFINE_CHARCLASS	\
	"Attempting to redefine charclass \"%s\" is not allowed.  "\
	"Directive ignored.\n"

#define	ERR_TOU_TOL_ILL_DEFINED	\
	"Character(s) not in upper/lower class.\n"

#define	ERR_MULTI_FILE_CODE	\
	"Multiple file_code keywords are specified in extension file.\n"

#define	ERR_MULTI_PROC_CODE	\
	"Multiple process_code keywords are specified in extension file.\n"

#define	ERR_NO_CSWIDTH	\
	"cswidth must be specified in extension file.\n"

#define	ERR_INV_CSWIDTH	\
	"cswidth cannot be specified with the file code other than EUC in "\
	"extension file.\n"

#define	ERR_PROC_FILE_MISMATCH	\
	"process_code/file_code mismatch in extension file.\n"

#define	ERR_LP64_WITH_OLD_EXT	\
	"-lp64 specified with old format extension file.\n"

#define	ERR_MIX_NEW_OLD_EXT	\
	"Mixing new format and old format in extension file.\n"

#define	ERR_INVAL_COLL_RANGE2	\
	"Invalid range collating specification '%s' ... '%s' ignored.\n"

#define	ERR_INVAL_COLL	\
	"Invalid collating specification '%s' ignored.\n"

#define	ERR_INVAL_CTYPE	\
	"Invalid character reference '%s' ignored.\n"

#define	ERR_INVAL_CTYPE_RANGE1	\
	"Invalid range character reference \"%s"\
	";...;%s\" ignored.\n"

#define	ERR_INVAL_CTYPE_RANGE2	\
	"Invalid range character reference \""\
	";... ;%s\" ignored.\n"

#define	ERR_INVAL_CTYPE_RANGE3	\
	"Invalid range character reference \"%s"\
	";...;\" ignored.\n"

#define	ERR_INVAL_XLAT	\
	"Invalid character translation ('%s','%s') ignored.\n"

#define	ERR_INVAL_EUCINFO	\
	"EUC Information mismatch between the locale to be copied "\
	"and the extension/charmap file.\n"

#define	ERR_CSNAME_MISMATCH	\
	"Codeset name mismatch between the locale to be copied "\
	"and the charmap file: \"%s\" and \"%s\".\n"

#define	ERR_FCTYPE_MISMATCH	\
	"File code type mismatch between the locale to be copied "\
	"and the extension file.\n"

#define	ERR_PCTYPE_MISMATCH	\
	"Process code type mismatch between the locale to be copied "\
	"and the extension file.\n"

#define	ERR_MBMAX_MISMATCH	\
	"Max encoding length mismatch between the locale to be copied "\
	"and the extension/charmap file.\n"

#define	ERR_MBMIN_MISMATCH	\
	"Min encoding length mismatch between the locale to be copied "\
	"and the extension/charmap file.\n"

#define	ERR_MAXDISP_MISMATCH	\
	"Max display width length mismatch between the locale to be copied "\
	"and the extension/charmap file.\n"

#define	ERR_NULL_IS_NOT_0W	\
	"The column width of the NULL character is not defined as 0.\n"

#define	ERR_NOT_ENOUGH_CHARS	\
	"Not enough characters are defined.\n"

#define	ERR_IMPROPER_HEX	\
	"LCBIND improper hexnumber %s.\n"

#define	ERR_VALUE_NOT_FOUND	\
	"LCBIND didn't find value for %s.\n"

#define	ERR_UNSUPPORTED_LEN	\
	"Unsupported length for EUC CS%d length = %d.\n"

#define	ERR_LCBIND_DUPLICATE_NAME	\
	"Duplicate LCBIND symbolic name.\n"

#define	ERR_LCBIND_DUPLICATE_VALUE	\
	"Duplicate LCBIND value: %08x.\n"

#define	ERR_BASE_WIDTH_IS_0	\
	"The column width 0 for the printable characters in the PCS "\
	"is invalid.\n"

#define	ERR_NO_PRINTABLE_PCS	\
	"There is no printable character defined in the PCS.\n"

#define	ERR_MULTIPLE_M_OPT	\
	"localedef: more than one -m option are specified.\n"

#define	ERR_NO_ISA_FOUND	\
	"localedef: unable to find instruction set architectures\n"

#define	ERR_MKTEMP_FAILED	\
	"localedef: mktemp failed to create a unique file name.\n"

#define	ERR_ACCESS_DIR	\
	"localedef: cannot access %s directory.\n"

#define	ERR_NON_DIR_EXIST	\
	"localedef: non-directory %s exists.\n"

#define	ERR_CREATE_DIR	\
	"localedef: cannot create %s directory.\n"

#define	ERR_STAT_FAILED	\
	"localedef: cannot stat %s directory.\n"

#define	ERR_NO_64_OBJ	\
	"Suppressing generating 64-bit locale object.\n"

#define	ERR_CONT_COL_RANGE	\
	"Invalid range collating specification.\n"

#define	ERR_INVAL_COLL_RANGE3	\
	"Invalid starting identifier of the range collating "\
	"specification '%s'.\n"

#define	ERR_BAD_COLL_ELLIPSIS	\
	"Invalid ellipsis in the collating specification.\n"

#define	ERR_LONG_INPUT	\
	"Input too long.\n"

#define	ERR_TOO_MANY_LCBIND	\
	"Cannot add more than %d classifications/transformations.\n"

#define	ERR_TOO_MANY_CLASS	\
	"Too many charclass defined.\n"

#define	ERR_TOO_MANY_LCBIND_SYMBOL	\
	"Too many lcbind symbols.\n"

#define	ERR_NO_CTYPE_MASK	\
	"No character class defined in LC_CTYPE.\n"

#define	ERR_TOO_MANY_MASKS	\
	"Too many charclass combination specified.\n"

#define	ERR_NO_MORE_WEIGHTS	\
	"Cannot allocate an unique weight any more.\n"

#define	ERR_NO_MORE_STACK	\
	"Cannot expand the internal stack to handle the locale.\n"

#define	ERR_ICONV_OPEN_U8_U4	\
	"Failed to open the code conversion from UCS-4 to UTF-8.\n"

#define	ERR_ICONV_OPEN_USER_U8	\
	"Failed to open the code conversion from UTF-8 to %s.\n"

#define	ERR_NO_SUPPORT_UCS4	\
	"Does not support this UCS4 value: 0x%x.\n"

#define	ERR_ICONV_FAIL	\
	"Failed to convert the character to the specified code set: 0x%x\n"

#define	ERR_TOO_LARGE_WIDTH	\
	"Specified width %d is too large.\n"

#define	DIAG_32_NAME	\
	"32-bit locale shared object was created with the name: %s\n"

#define	DIAG_64_DIR_NAME	\
	"64-bit locale shared object was created with the name: %s/%s\n"

#define	DIAG_64_NAME	\
	"64-bit locale shared object was created with the name: %s\n"

#define	DIAG_COPY_USED	\
	"The copy keyword has been used with the following categories:\n"

#define	DIAG_COPY_METHODS	\
	"The following method functions will be used with this locale.\n"\
	"If they are not correct, please provide correct information\n"\
	"about the method functions in the extensions file:\n"

#define	DIAG_COPY_LINKLIBS	\
	"The locale has been linked with the following library/libraries.\n"\
	"If they are not correct, please provide correct information\n"\
	"about the dependent libraries in the extensions file:\n"

#endif /* _LOCALEDEF_LOCALEDEF_MSG_H */
