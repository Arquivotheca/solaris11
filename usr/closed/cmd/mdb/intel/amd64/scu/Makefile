#
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

MODULE = scu.so
MDBTGT = kvm

MODSRCS = scu.c

include $(SRC)/cmd/Makefile.cmd
include $(SRC)/cmd/Makefile.cmd.64
include ../../../common/Makefile.closed
include $(SRC)/cmd/mdb/intel/Makefile.amd64
include $(SRC)/cmd/mdb/Makefile.module

C99MODE = -xc99=%all

CPPFLAGS += -I$(SRC)/uts/common/
CPPFLAGS += -I$(SRC)/cmd/mdb/common/
CPPFLAGS += -I$(SRC)/../closed/uts/common/

CPPFLAGS += -I$(SRC)/../closed/uts/common/io/scsi/adapters/scu/include
CPPFLAGS += -I$(SRC)/../closed/uts/common/io/scsi/adapters/scu/base/include
CPPFLAGS += -I$(SRC)/../closed/uts/common/io/scsi/adapters/scu/ossl
CPPFLAGS += -I$(SRC)/../closed/uts/common/io/scsi/adapters/scu/sati/include
CPPFLAGS += -I$(SRC)/../closed/uts/common/io/scsi/adapters/scu/scic/include
CPPFLAGS += -I$(SRC)/../closed/uts/common/io/scsi/adapters/scu/scic/sds/include
CPPFLAGS += -I$(SRC)/../closed/uts/common/io/scsi/adapters/scu/scif/include
CPPFLAGS += -I$(SRC)/../closed/uts/common/io/scsi/adapters/scu/scif/sas/include
CPPFLAGS += -I$(SRC)/../closed/uts/common/io/scsi/adapters/scu/util/include
CPPFLAGS += -I$(SRC)/../closed/uts/common/sys/scsi/adapters/scu/

CPPFLAGS += -DPBG_BUILD -DSCIC_SDS_4_ENABLED	
