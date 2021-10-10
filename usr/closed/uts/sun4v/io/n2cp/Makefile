#
# Copyright (c) 2006, 2011, Oracle and/or its affiliates. All rights reserved.
#
# uts/sun4v/io/n2cp/Makefile
#
# include global definitions
include $(SRC)/Makefile.master

FRC:
	@echo "NO DEFAULT TARGET IN HERE"
	@echo "You probably intended to run make in \$$SRC/closed/sun4v/n2cp"

.KEEP_STATE:

# EXPORT DELETE START
EXPORT_SRC:
	$(RM) Makefile+ n2cp.c+ n2cp_debug.c+ n2cp_kcf.c+ \
		n2cp_block.c+ n2cp_hash.c+ n2cp_hmac.c+ \
		n2cp_kstat.c+ n2cp_md.c+
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< n2cp.c > n2cp.c+
	$(MV) n2cp.c+ n2cp.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< n2cp_debug.c > n2cp_debug.c+
	$(MV) n2cp_debug.c+ n2cp_debug.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< n2cp_kcf.c > n2cp_kcf.c+
	$(MV) n2cp_kcf.c+ n2cp_kcf.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< n2cp_block.c > n2cp_block.c+
	$(MV) n2cp_block.c+ n2cp_block.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< n2cp_hash.c > n2cp_hash.c+
	$(MV) n2cp_hash.c+ n2cp_hash.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< n2cp_hmac.c > n2cp_hmac.c+
	$(MV) n2cp_hmac.c+ n2cp_hmac.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< n2cp_kstat.c > n2cp_kstat.c+
	$(MV) n2cp_kstat.c+ n2cp_kstat.c
	sed -e "/EXPORT DELETE START/,/EXPORT DELETE END/d" \
		< n2cp_md.c > n2cp_md.c+
	$(MV) n2cp_md.c+ n2cp_md.c
	sed -e "/^# EXPORT DELETE START/,/^# EXPORT DELETE END/d" \
		< Makefile > Makefile+
	$(RM) Makefile
	$(MV) Makefile+ Makefile 
	$(CHMOD) 444 Makefile n2cp.c n2cp_debug.c n2cp_kcf.c \
		n2cp_block.c n2cp_hash.c n2cp_hmac.c \
		n2cp_kstat.c n2cp_md.c

# EXPORT DELETE END
