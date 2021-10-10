#
# Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
#
# lib/libc_i18n/Makefile.com

LIBRARY=
LIB_PIC=	libc_i18n.a
VERS=		.1

COLLATE_C=			\
	__fnmatch_std_c.o

COLLATE_SB=			\
	collcmp_sbyte.o		\
	collstr_sbyte.o		\
	__strcoll_std_sbyte.o	\
	__strxfrm_std_sbyte.o	\
	__fnmatch_std_sbyte.o

COLLATE_WC=			\
	collcmp_wide.o

NORM_OBJECTS=	\
	__btowc_dense.o		\
	__btowc_euc.o		\
	__btowc_sb.o		\
	__eucpctowc_gen.o	\
	__fgetwc_dense.o	\
	__fgetwc_sb.o		\
	__fnmatch_std.o		\
	__iswctype_sb.o		\
	__iswctype_bc.o		\
	__iswctype_std.o	\
	__localeconv_std.o	\
	__mbftowc_dense.o	\
	__mbftowc_sb.o		\
	__mblen_gen.o		\
	__mblen_sb.o		\
	__mbrlen_gen.o		\
	__mbrlen_sb.o		\
	__mbrtowc_dense.o	\
	__mbrtowc_euc.o		\
	__mbrtowc_sb.o		\
	__mbsinit_gen.o		\
	__mbsinit_sb.o		\
	__mbsrtowcs_dense.o	\
	__mbsrtowcs_euc.o	\
	__mbsrtowcs_sb.o	\
	__mbstowcs_dense.o	\
	__mbstowcs_euc.o	\
	__mbstowcs_sb.o		\
	__mbtowc_dense.o	\
	__mbtowc_euc.o		\
	__mbtowc_sb.o		\
	__nl_langinfo_std.o	\
	__regcomp_C.o		\
	__regcomp_std.o		\
	__regerror_std.o	\
	__regexec_C.o		\
	__regexec_std.o		\
	__regfree_std.o		\
	__strcoll_std.o		\
	__strfmon_std.o		\
	__strxfrm_std.o		\
	__towctrans_bc.o	\
	__towlower_bc.o		\
	__towupper_bc.o		\
	__wcrtomb_dense.o	\
	__wcrtomb_euc.o		\
	__wcrtomb_sb.o		\
	__wcscoll_std.o		\
	__wcsrtombs_dense.o	\
	__wcsrtombs_euc.o	\
	__wcsrtombs_sb.o	\
	__wcstombs_dense.o	\
	__wcstombs_euc.o	\
	__wcstombs_sb.o		\
	__wcswidth_dense.o	\
	__wcswidth_euc.o	\
	__wcswidth_sb.o		\
	__wcswidth_std.o	\
	__wcsxfrm_std.o		\
	__wctob_dense.o		\
	__wctob_euc.o		\
	__wctob_sb.o		\
	__wctoeucpc_gen.o	\
	__wctomb_dense.o	\
	__wctomb_euc.o		\
	__wctomb_sb.o		\
	__wcwidth_dense.o	\
	__wcwidth_euc.o		\
	__wcwidth_sb.o		\
	_trwctype.o		\
	btowc.o			\
	collcmp.o		\
	collutil.o		\
	collstr.o		\
	collwstr.o		\
	colval.o		\
	euc_info.o		\
	fgetwc.o		\
	fgetws.o		\
	fnmatch.o		\
	fputwc.o		\
	fputws.o		\
	fwide.o			\
	getws.o			\
	isalnum.o		\
	isenglish.o		\
	iswalnum.o		\
	iswctype.o		\
	libc_interface.o	\
	loc_setup.o		\
	localeconv.o		\
	mbftowc.o		\
	mblen.o			\
	mbrlen.o		\
	mbrtowc.o		\
	mbsinit.o		\
	mbsrtowcs.o		\
	mbstowcs.o		\
	mbtowc.o		\
	nl_langinfo.o		\
	regcomp.o		\
	regerror.o		\
	regexec.o		\
	regfree.o		\
	scrwidth.o		\
	setlocale.o		\
	strcoll.o		\
	strfmon.o		\
	strftime.o		\
	strptime.o		\
	strxfrm.o		\
	tolower.o		\
	towctrans.o		\
	ungetwc.o		\
	wcrtomb.o		\
	wcscoll.o		\
	wcsftime.o		\
	wcsrtombs.o		\
	wcstombs.o		\
	wcswidth.o		\
	wcsxfrm.o		\
	wctob.o			\
	wctomb.o		\
	wctrans.o		\
	wctype.o		\
	wcwidth.o		\
	wscol.o

OBJECTS=	\
	$(NORM_OBJECTS)		\
	$(COLLATE_C)		\
	$(COLLATE_SB)		\
	$(COLLATE_WC)


include $(SRC)/lib/Makefile.lib
include $(SRC)/lib/Makefile.rootfs
$(TONICBUILD)include $(CLOSED)/Makefile.tonic


# This is necessary to avoid problems with calling _ex_unwind().
# We probably don't want any inlining anyway.
XINLINE = -xinline=

# Setting THREAD_DEBUG = -DTHREAD_DEBUG (make THREAD_DEBUG=-DTHREAD_DEBUG ...)
# enables ASSERT() checking in the threads portion of the library.
# This is automatically enabled for DEBUG builds, not for non-debug builds.
THREAD_DEBUG=
$(NOT_RELEASE_BUILD)THREAD_DEBUG = -DTHREAD_DEBUG

ROOTLIB_PIC=	$(LIB_PIC:%=$(ROOTLIBDIR)/%)
ROOTLIB_PIC64=	$(LIB_PIC:%=$(ROOTLIBDIR64)/%)

SRCDIR= 	../common
SRCS=		$(NORM_OBJECTS:%.o=../common/%.c)
CLOBBERFILES +=	$(LIB_PIC)

# The "$(GREP) -v ' L '" part is necessary only
# until lorder is fixed to ignore thread-local variables.
BUILD.AR=	$(RM) $@; \
		$(AR) q $@ `$(LORDER) $(PICS) | $(GREP) -v ' L ' | $(TSORT)`

LIBCDIR=	../../../../src/lib/libc

C99MODE=	$(C99_ENABLE)

# NOTE: libc_i18n.a will be part of libc.so.1.  Therefore, the compilation
# conditions such as the settings of CFLAGS and CPPFLAGS for the libc_i18n
# stuff need to be compatible with the ones for the libc stuff.
# The compilation conditions of libc_i18n need to be kept to be compatible
# with the ones for libc.  
#
# EXTN_CFLAGS, EXTN_CPPFLAGS, and EXTN_LINTFLAGS will be set in enclosing
# Makefile

CFLAGS +=	$(CCVERBOSE) $(EXTN_CFLAGS) $(XINLINE) $(THREAD_DEBUG)
CFLAGS64 +=	             $(EXTN_CFLAGS) $(XINLINE) $(THREAD_DEBUG)

CPPFLAGS=	-D_REENTRANT $(EXTN_CPPFLAGS) -I../inc \
		-I$(LIBCDIR)/inc -I$(LIBCDIR)/port/i18n -I$(LIBCDIR)/port/gen \
		$(CPPFLAGS.master)

$(COLLATE_C:%=pics/%) :=	\
	CPPFLAGS += -D_C_COLL

$(COLLATE_SB:%=pics/%) :=	\
	CPPFLAGS += -D_SB_COLL

$(COLLATE_WC:%=pics/%) :=	\
	CPPFLAGS += -D_WC_COLL

.KEEP_STATE:

all:	$(LIB_PIC)

lint	:=	CPPFLAGS += -D_MSE_INT_H -D_LCONV_C99
lint	:=	LINTFLAGS += -mn $(EXTN_LINTFLAGS)
lint	:=	LINTFLAGS64 += -mn $(EXTN_LINTFLAGS)

lint:	lintcheck

pics/%_c.o:	$(SRCDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%_sbyte.o:	$(SRCDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%_wide.o:	$(SRCDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o:	$(SRCDIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include $(SRC)/lib/Makefile.targ

$(ROOTLIBDIR)/$(LIB_PIC) :=		FILEMODE= 755
$(ROOTLIBDIR64)/$(LIB_PIC) :=		FILEMODE= 755

DIR= pics
$(LIB_PIC):	pics .WAIT $$(PICS)
	$(BUILD.AR)
	$(POST_PROCESS_A)
