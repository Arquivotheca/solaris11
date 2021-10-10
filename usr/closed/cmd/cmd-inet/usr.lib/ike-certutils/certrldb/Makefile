#
#
# Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
#
# cmd/cmd-inet/ike-certutils/certrldb/Makefile

PROG=	certrldb
OBJ1=	certrldb.o ssh-certview.o iprintf.o
OBJ2=	../dumputils.o
OBJS=	$(OBJ1) $(OBJ2)
SRCS=	$(OBJ1:%.o=%.c) 
LINTS=	$(OBJ1:%.o=%.ln)

include		$(SRC)/cmd/Makefile.cmd
$(TONICBUILD)include		$(CLOSED)/Makefile.tonic

LDLIBS +=	-like -lnsl -lmd5
CPPFLAGS +=	-I..

.KEEP_STATE:

.PARALLEL:	$(OBJ1)

all:	$(PROG)

$(PROG):	$(OBJ1)
# CRYPT DELETE START
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

nop:
	# Dummy target for when I create CRYPT source.
# CRYPT DELETE END
	$(ECHO) "#!/bin/false" > ./certrldb
	$(CHMOD) 0755 ./certrldb

include  $(SRC)/cmd/cmd-inet/usr.lib/Makefile.lib

install:	all .WAIT $(ROOTLIBINETPROG)

clean:
	$(RM) $(OBJ1) $(CLEANFILES)

lint:	$(LINTS)
	$(LINT.c) $(OBJS:%.o=%.ln) $(LDLIBS)

# EXPORT DELETE START
EXPORT_SRC:	CRYPT_SRC
	sed -e "/^# EXPORT DELETE START/,/^# EXPORT DELETE END/d" \
		< Makefile > Makefile+
	$(MV) Makefile+ Makefile

CRYPT_SRC:
# CRYPT DELETE START
	sed -e "/^# CRYPT DELETE START/,/^# CRYPT DELETE END/d" < Makefile | \
	    sed -e 's/(PROG):	.*/(PROG):/g' > Makefile+
	$(MV) Makefile+ Makefile
# CRYPT DELETE END

# EXPORT DELETE END

include $(SRC)/cmd/Makefile.targ
