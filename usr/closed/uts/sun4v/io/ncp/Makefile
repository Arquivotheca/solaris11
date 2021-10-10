#
# Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
#
# uts/sun4v/io/ncp/Makefile
#
# include global definitions
include $(SRC)/Makefile.master

.KEEP_STATE:

FRC:
	@echo "NO DEFAULT TARGET IN HERE"
	@echo "You probably intended to run make in \$$SRC/closed/sun4v/ncp"

# EXPORT DELETE START
EXPORT_SRC:
	$(RM) Makefile+ ncp_bignumimpl.c+ ncp.c+ ncp_debug.c+ \
		ncp_dsa.c+ ncp_kcf.c+ ncp_kstat.c+ ncp_rsa.c+ ncp_md.c+ \
		ncp_ecdh.c+ ncp_ecdsa.c+ ncp_ellipticcrv.c+
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp_bignumimpl.c > ncp_bignumimpl.c+
	$(MV) ncp_bignumimpl.c+ ncp_bignumimpl.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp.c > ncp.c+
	$(MV) ncp.c+ ncp.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp_debug.c > ncp_debug.c+
	$(MV) ncp_debug.c+ ncp_debug.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp_dsa.c > ncp_dsa.c+
	$(MV) ncp_dsa.c+ ncp_dsa.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp_kcf.c > ncp_kcf.c+
	$(MV) ncp_kcf.c+ ncp_kcf.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp_kstat.c > ncp_kstat.c+
	$(MV) ncp_kstat.c+ ncp_kstat.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp_rsa.c > ncp_rsa.c+
	$(MV) ncp_rsa.c+ ncp_rsa.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp_md.c > ncp_md.c+
	$(MV) ncp_md.c+ ncp_md.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp_dh.c > ncp_dh.c+
	$(MV) ncp_dh.c+ ncp_dh.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp_ecdh.c > ncp_ecdh.c+
	$(MV) ncp_ecdh.c+ ncp_ecdh.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp_ecdsa.c > ncp_ecdsa.c+
	$(MV) ncp_ecdsa.c+ ncp_ecdsa.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< ncp_ellipticcrv.c > ncp_ellipticcrv.c+
	$(MV) ncp_ellipticcrv.c+ ncp_ellipticcrv.c
	sed -e "/^# EXPORT DELETE START/,/^# EXPORT DELETE END/d" \
		< Makefile > Makefile+
	$(RM) Makefile
	$(MV) Makefile+ Makefile 
	$(CHMOD) 444 Makefile ncp_bignumimpl.c ncp.c ncp_debug.c \
		ncp_dsa.c ncp_kcf.c ncp_kstat.c ncp_rsa.c ncp_md.c ncp_dh.c \
		ncp_ecdh.c ncp_ecdsa.c ncp_ellipticcrv.c

# EXPORT DELETE END
