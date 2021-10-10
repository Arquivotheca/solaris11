#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#ident	"%Z%%M%	%I%	%E% SMI"

#
# This is the generic version of the extensions file for producing a
# single- or multi-byte locale which has an EUC based file code
# charmap and will run with EUC process code (Solaris 2.5.1 or earlier).
#

METHODS
process_code	euc
cswidth

eucpctowc	"__eucpctowc_gen" "libc" "/usr/lib"  "libc.so.1"
fgetwc		"__fgetwc_euc"
fgetwc@native	"__fgetwc_dense"
fnmatch		"__fnmatch_std"
getdate		"__getdate_std"
iswctype	"__iswctype_bc"
iswctype@native	"__iswctype_std"
mbftowc		"__mbftowc_euc"
mbftowc@native	"__mbftowc_dense"
mblen		"__mblen_gen"
mbstowcs	"__mbstowcs_euc"
mbstowcs@native	"__mbstowcs_dense"
mbtowc		"__mbtowc_euc"
mbtowc@native	"__mbtowc_dense"
regcomp		"__regcomp_std"
regexec		"__regexec_std"
regerror	"__regerror_std"
regfree		"__regfree_std"
strcoll		"__strcoll_std"
strfmon		"__strfmon_std"
strftime	"__strftime_std"
strptime	"__strptime_std"
strxfrm		"__strxfrm_std"
towctrans	"__towctrans_bc"
towctrans@native "__towctrans_std"
towlower	"__towlower_bc"
towlower@native	"__towlower_std"
towupper	"__towupper_bc"
towupper@native	"__towupper_std"
trwctype	"__trwctype_std"
wcscoll		"__wcscoll_bc"
wcscoll@native	"__wcscoll_std"
wcsftime	"__wcsftime_std"
wcstombs	"__wcstombs_euc"
wcstombs@native	"__wcstombs_dense"
wcswidth	"__wcswidth_euc"
wcswidth@native	"__wcswidth_dense"
wcsxfrm		"__wcsxfrm_bc"
wcsxfrm@native	"__wcsxfrm_std"
wctoeucpc	"__wctoeucpc_gen"
wctomb		"__wctomb_euc"
wctomb@native	"__wctomb_dense"
wctrans		"__wctrans_std"
wctype		"__wctype_std"
wcwidth		"__wcwidth_euc"
wcwidth@native	"__wcwidth_dense"
btowc		"__btowc_euc"
btowc@native	"__btowc_dense"
wctob		"__wctob_euc"
wctob@native	"__wctob_dense"
mbsinit		"__mbsinit_gen"
mbrlen		"__mbrlen_gen"
mbrtowc		"__mbrtowc_euc"
mbrtowc@native	"__mbrtowc_dense"
wcrtomb		"__wcrtomb_euc"
wcrtomb@native	"__wcrtomb_dense"
mbsrtowcs	"__mbsrtowcs_euc"
mbsrtowcs@native	"__mbsrtowcs_dense"
wcsrtombs	"__wcsrtombs_euc"
wcsrtombs@native	"__wcsrtombs_dense"

END METHODS
