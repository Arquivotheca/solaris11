#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
# Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
#
# This make file will build mech_krb5.so.1. This shared object
# contains all the functionality needed to support the Kereros V5 GSS-API
# mechanism. No other Kerberos libraries are needed.
#

LIBRARY= mech_krb5.a
VERS = .1

FILEMODE=	755

# objects are listed by source directory
REL_PATH= ../

to_all:	all

# crypto
CRYPTO = cksumtype_to_string.o \
	coll_proof_cksum.o  \
	keyed_checksum_types.o keyed_cksum.o \
	make_random_key.o string_to_cksumtype.o \
	string_to_key.o valid_cksumtype.o \
	enctype_util.o pkcs11slot.o state.o pbkdf2.o old_api_glue.o \
	keylengths.o random_to_key.o prf.o cf2.o enctype_util_solaris.o

CRYPTO_UTS= cksumtypes.o decrypt.o encrypt.o encrypt_length.o \
	etypes.o nfold.o verify_checksum.o default_state.o \
	prng.o block_size.o make_checksum.o checksum_length.o hmac.o \
	mandatory_sumtype.o combine_keys.o key.o keyblocks.o

# crypto/raw
CRYPTO_RAW= raw_decrypt.o raw_encrypt.o

# crypto/des user space only
CRYPTO_DES= afsstring2key.o string2key.o 

# crypto/des common to user and kernel space
CRYPTO_DES_UTS= f_cbc.o f_cksum.o f_parity.o weak_key.o d3_cbc.o 

# crypto/arcfour user space only
CRYPTO_ARCFOUR= arcfour_str2key.o

# crypto/aes user space only
CRYPTO_AES=	aes_s2k.o

# crypto/arcfour common to user and kernel space
CRYPTO_ARCFOUR_UTS = k5_arcfour.o

#crypto/dk
CRYPTO_DK= stringtokey.o

CRYPTO_DK_UTS= checksum.o derive.o dk_decrypt.o dk_encrypt.o

# crypto/crc32
CRYPTO_CRC32= crc.o

# crypto/crc32
CRYPTO_CRC32_UTS= crc32.o

# crypto/md4
CRYPTO_MD4= md4.o

# crypto/enc_provider
CRYPTO_ENC= des.o des3.o arcfour_provider.o aes_provider.o

# crypto/hash_provider
CRYPTO_HASH= hash_md5.o hash_sha1.o hash_ef_generic.o
CRYPTO_HASH_UTS= hash_crc32.o

# crypto/keyhash_provider
CRYPTO_KEYHASH= k5_md5des.o hmac_md5.o
CRYPTO_KEYHASH_UTS= descbc.o

# crypto/old
CRYPTO_OLD=  des_stringtokey.o 

# crypto/old
CRYPTO_OLD_UTS=  old_encrypt.o old_decrypt.o

DTRACE_OBJS = kerberos_dtrace.o

# et error_tables
ET=	adb_err.o adm_err.o asn1_err.o chpass_util_strings.o error_message.o \
	com_err.o gssapi_err_generic.o import_err.o \
	gssapi_err_krb5.o kadm_err.o kdb5_err.o kpasswd_strings.o kdc5_err.o \
	krb5_err.o kv5m_err.o prof_err.o pty_err.o ss_err.o

# krb5/asn.1
K5_ASN1= asn1_decode.o asn1_k_decode.o asn1_encode.o \
	asn1_get.o asn1_make.o \
	asn1buf.o krb5_decode.o krb5_encode.o \
	asn1_k_encode.o asn1_misc.o ldap_key_seq.o

# krb5/ccache
K5_CC= cc_file.o cc_memory.o ccbase.o ccfns.o ccdefault.o ccdefops.o ser_cc.o \
       cc_retr.o cccopy.o cccursor.o

# krb5/keytab
K5_KT=	ktadd.o ktbase.o ktdefault.o ktfr_entry.o \
	ktremove.o read_servi.o kt_file.o kt_srvtab.o ktfns.o kt_findrealm.o \
	kt_solaris.o kt_memory.o

K5_KRB= addr_comp.o  addr_order.o  addr_srch.o \
	auth_con.o  bld_pr_ext.o  bld_princ.o  chk_trans.o \
	conv_princ.o  copy_addrs.o  copy_creds.o copy_data.o copy_kdcreq.o \
        copy_padata.o copy_tick.o \
	cp_key_cnt.o  decode_kdc.o  decrypt_tk.o  encode_kdc.o  encrypt_tk.o \
	fwd_tgt.o  gc_frm_kdc.o  gc_via_tkt.o  gen_seqnum.o \
	gen_subkey.o  get_creds.o  get_in_tkt.o kdc_rep_dc.o  mk_cred.o  \
	mk_error.o  mk_priv.o  mk_rep.o  mk_req.o  mk_req_ext.o  mk_safe.o \
	pr_to_salt.o   princ_comp.o  privacy_allowed.o  rd_cred.o \
	rd_error.o  rd_priv.o  rd_rep.o  rd_req.o  rd_req_dec.o  rd_safe.o \
	recvauth.o  send_tgs.o  sendauth.o  srv_rcache.o  str_conv.o \
	tgtname.o  valid_times.o  walk_rtree.o appdefault.o deltat.o \
	enc_helper.o gic_keytab.o gic_opt.o gic_pwd.o preauth2.o \
	preauth.o vfy_increds.o vic_opt.o set_realm.o krb5_libinit.o chpw.o \
	init_keyblock.o init_allocated_keyblock.o get_set_keyblock.o kerrs.o \
	getuid.o pac.o etype_list.o copy_athctr.o copy_auth.o copy_cksum.o \
	ser_ctx.o gen_save_subkey.o authdata.o fast.o libdef_parse.o \
	enc_keyhelper.o s4u_creds.o srv_dec_tkt.o allow_weak.o

K5_KRB_UTS= copy_key.o copy_princ.o \
	init_ctx.o kfree.o parse.o ser_actx.o \
	ser_adata.o ser_addr.o ser_auth.o \
	ser_cksum.o ser_key.o \
	ser_princ.o serialize.o unparse.o

K5_OS=	an_to_ln.o def_realm.o ccdefname.o free_krbhs.o \
	full_ipadr.o get_krbhst.o gen_port.o genaddrs.o gen_rname.o \
	hostaddr.o hst_realm.o krbfileio.o \
	ktdefname.o kuserok.o mk_faddr.o localaddr.o locate_kdc.o lock_file.o \
	net_read.o net_write.o osconfig.o port2ip.o \
	read_msg.o read_pwd.o realm_dom.o sendto_kdc.o sn2princ.o \
	unlck_file.o ustime.o write_msg.o safechown.o \
	prompter.o realm_iter.o \
	dnsglue.o dnssrv.o thread_safe.o changepw.o accessor.o

K5_OS_UTS=init_os_ctx.o timeofday.o toffset.o c_ustime.o

K5_POSIX= setenv.o daemon.o

K5_RCACHE=rc_base.o rc_dfl.o rc_mem.o rc_common.o rc_io.o rcdef.o rc_conv.o \
	ser_rc.o rcfns.o rc_none.o

K5_UNICODE=ucstr.o ucdata.o ure.o urestubs.o

MECH= 	accept_sec_context.o store_cred.o \
	add_cred.o disp_com_err_status.o  disp_major_status.o \
	compare_name.o context_time.o copy_ccache.o \
	disp_name.o disp_status.o export_sec_context.o \
	get_tkt_flags.o import_name.o indicate_mechs.o \
	inq_context.o inq_cred.o inq_names.o \
	krb5_gss_glue.o \
	pname_to_uid.o process_context_token.o \
        rel_buffer.o rel_oid.o rel_oid_set.o \
	rel_cred.o  rel_name.o util_buffer.o \
	util_localhost.o \
	util_cksum.o acquire_cred.o init_sec_context.o \
	set_ccache.o acquire_cred_with_pw.o lucid_context.o \
	set_allowable_enctypes.o oid_ops.o export_name.o \
	util_buffer_set.o util_errmap.o s4u_gss_glue.o

MECH_UTS= delete_sec_context.o gssapi_krb5.o \
	import_sec_context.o k5seal.o k5sealv3.o \
	k5unseal.o seal.o ser_sctx.o \
	sign.o unseal.o util_crypt.o  \
	util_ordering.o util_seed.o util_seqnum.o \
	util_set.o  util_token.o util_validate.o \
	val_cred.o verify.o wrap_size_limit.o naming_exts.o

GSSAPI_UTS= gen_oids.o

PROFILE_OBJS= prof_tree.o prof_file.o prof_parse.o prof_init.o \
	prof_set.o prof_get.o prof_solaris.o

SUPPORT_OBJS= fake-addrinfo.o init-addrinfo.o threads.o errors.o plugins.o \
	      utf8_conv.o utf8.o k5buf.o zap.o gmt_mktime.o

KWARN_OBJS= kwarnd_clnt_stubs.o kwarnd_clnt.o kwarnd_handle.o kwarnd_xdr.o

OBJECTS= \
	$(MECH) $(MECH_UTS) $(GSSAPI_UTS)\
	$(SUPPORT_OBJS) \
	$(KWARN_OBJS) \
	$(PROFILE_OBJS) \
	$(CRYPTO) $(CRYPTO_UTS) \
	$(CRYPTO_CRC32) \
	$(CRYPTO_CRC32_UTS) \
	$(CRYPTO_DES) $(CRYPTO_DES_UTS) \
	$(CRYPTO_MD4) \
	$(CRYPTO_DK) $(CRYPTO_DK_UTS) \
	$(CRYPTO_ARCFOUR) $(CRYPTO_ARCFOUR_UTS) \
	$(CRYPTO_AES) \
	$(CRYPTO_ENC) \
	$(CRYPTO_HASH) $(CRYPTO_HASH_UTS) \
	$(CRYPTO_KEYHASH) $(CRYPTO_KEYHASH_UTS) \
	$(CRYPTO_OLD) $(CRYPTO_OLD_UTS) \
	$(CRYPTO_RAW) \
	$(DTRACE_OBJS) \
	$(ET) \
	$(K5_ASN1) \
	$(K5_CC) \
	$(K5_KT) \
	$(K5_KRB) $(K5_KRB_UTS) \
	$(K5_OS) $(K5_OS_UTS) \
	$(K5_POSIX) $(K5_RCACHE) \
	$(K5_UNICODE)

# include library definitions
include $(REL_PATH)/../../Makefile.lib

# Must come after Makefile.lib so CPPFLAGS doesn't get overwritten
include $(SRC)/lib/gss_mechs/mech_krb5/Makefile.mech_krb5

K5LIBLINK=$(LIBRARY:%.a=lib%.so)

# override default text domain
TEXT_DOMAIN= SUNW_OST_NETRPC
INS.liblink2=	-$(RM) $@; $(SYMLINK) gss/$(LIBLINKPATH)$(LIBLINKS) $@

CPPFLAGS += -I$(REL_PATH)/libgss -I../include  \
		-I$(SRC)/uts/common/gssapi \
		-I$(SRC)/uts/common/gssapi/include \
		-I$(SRC)/lib/gss_mechs/mech_krb5/mech \
		-I$(SRC)/lib/gss_mechs/mech_krb5/include/krb5 \
		-I../dtrace \
		-I../include/krb5 \
		-I../krb5/keytab \
		-I../krb5/krb \
		-I../krb5/os \
		-I../krb5/ccache \
		-I../krb5/rcache \
		-I$(SRC)/lib/krb5 \
		-I$(SRC)/lib/krb5/kadm5 \
		-I$(SRC)/uts/common/gssapi/mechs/krb5/include \
		-I$(SRC)/uts/common/gssapi/mechs/krb5/crypto/des \
		-I.

# KRB5_DEFS can be assigned various preprocessor flags, typically -D
# defines on the make invocation.  These values will be appended to
# CPPFLAGS so the other flags in CPPFLAGS will not be overwritten.

CPPFLAGS += $(KRB5_DEFS)
C99MODE = -xc99=%all
C99LMODE = -Xc99=%all

MAPFILE_EXPORT = ../mapfile-vers-clean
$(EXPORT_RELEASE_BUILD)MAPFILE_EXPORT = \
		$(CLOSED)/lib/gss_mechs/mech_krb5/mapfile-vers-export
MAPFILES =	../mapfile-vers $(MAPFILE_EXPORT)

$(EXPORT_RELEASE_BUILD)include $(CLOSED)/lib/gss_mechs/mech_krb5/Makefile.export

#CPPFLAGS += 	-D_REENTRANT
$(PICS) :=	CFLAGS += $(XFFLAG)
$(PICS) :=	CFLAGS64 += $(XFFLAG)
$(PICS) :=	CCFLAGS += $(XFFLAG)
$(PICS) :=	CCFLAGS64 += $(XFFLAG)

LIBS = $(DYNLIB) $(K5LIBLINK)

# override ROOTLIBDIR and ROOTLINKS
ROOTLIBDIR=		$(ROOT)/usr/lib/gss
ROOTLIBDIR64=		$(ROOT)/usr/lib/$(MACH64)/gss
STUBROOTLIBDIR=		$(STUBROOT)/usr/lib/gss
STUBROOTLIBDIR64=	$(STUBROOT)/usr/lib/$(MACH64)/gss
LROOTLIBDIR=		$(LROOT)/usr/lib/gss
LROOTLIBDIR64=		$(LROOT)/usr/lib/$(MACH64)/gss

K5MECHLINK=		$(K5LIBLINK:%=$(ROOT)/usr/lib/%)
K5MECHLINK64=		$(K5LIBLINK:%=$(ROOT)/usr/lib/$(MACH64)/%)
STUBK5MECHLINK=		$(K5LIBLINK:%=$(STUBROOT)/usr/lib/%)
STUBK5MECHLINK64=	$(K5LIBLINK:%=$(STUBROOT)/usr/lib/$(MACH64)/%)

ROOTLIBS=	$(LIBS:%=$(ROOTLIBDIR)/%) $(K5MECHLINK)
ROOTLIBS64=	$(LIBS:%=$(ROOTLIBDIR64)/%) $(K5MECHLINK64)
STUBROOTLIBS=	$(LIBS:%=$(STUBROOTLIBDIR)/%) $(STUBK5MECHLINK)
STUBROOTLIBS64=	$(LIBS:%=$(STUBROOTLIBDIR64)/%) $(STUBK5MECHLINK64)

$(ROOTLIBDIR) $(ROOTLIBDIR64) $(STUBROOTLIBDIR) $(STUBROOTLIBDIR64):
	$(INS.dir)

# create libmech_krb5 link locally
$(K5LIBLINK): $(DYNLIB)
	-$(RM) $@; $(SYMLINK) $(DYNLIB) $@

# create libmech_krb5 link in $ROOT/usr/lib/gss/
$(ROOTLIBDIR)/$(K5LIBLINK):	$(ROOTLIBDIR)/$(LIBLINKS)$(VERS)
	$(INS.liblink)
$(STUBROOTLIBDIR)/$(K5LIBLINK):	$(STUBROOTLIBDIR)/$(LIBLINKS)$(VERS)
	$(INS.liblink)

# create libmech_krb5 link in $ROOT/usr/lib/$(MACH64)/gss
$(ROOTLIBDIR64)/$(K5LIBLINK):	$(ROOTLIBDIR64)/$(LIBLINKS)$(VERS)
	$(INS.liblink)
$(STUBROOTLIBDIR64)/$(K5LIBLINK):	$(STUBROOTLIBDIR64)/$(LIBLINKS)$(VERS)
	$(INS.liblink)

# create libmech_krb5 link in ROOT/usr/lib
$(K5MECHLINK):	$(ROOTLIBDIR)/$(LIBLINKS)$(VERS)
	$(INS.liblink2)
$(STUBK5MECHLINK):	$(STUBROOTLIBDIR)/$(LIBLINKS)$(VERS)
	$(INS.liblink2)

# create libmech_krb5 link in ROOT/usr/lib/$(MACH64)
$(K5MECHLINK64):	$(ROOTLIBDIR64)/$(LIBLINKS)$(VERS)
	$(INS.liblink2)
$(STUBK5MECHLINK64):	$(STUBROOTLIBDIR64)/$(LIBLINKS)$(VERS)
	$(INS.liblink2)

LDLIBS += -lgss -lsocket -lresolv -lc -lpkcs11 -lnsl -lkstat
# -z ignore causes linker to ignore unneeded dependencies.  This is
#  needed because -lnsl is only used if DEBUG is defined.
DYNFLAGS += $(ZIGNORE)

# mech lib needs special initialization at load time
DYNFLAGS += -zinitarray=krb5_ld_init

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/mech/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/lib/gss_mechs/mech_krb5/mech/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/crypto/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/crypto/md4/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/des/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/crypto/des/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/arcfour/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/crypto/arcfour/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/crypto/aes/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/dk/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/crypto/dk/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/raw/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/crc32/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/crypto/crc32/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/sha1/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/enc_provider/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/crypto/hash_provider/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/hash_provider/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/crypto/keyhash_provider/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/keyhash_provider/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/old/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/crypto/old/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/crypto/os/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/crypto/sha1/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/dtrace/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/et/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/asn.1/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/ccache/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/ccache/file/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/ccache/stdio/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/ccache/memory/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/keytab/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/keytab/file/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/krb5/krb/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/krb/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(SRC)/uts/common/gssapi/mechs/krb5/krb5/os/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/os/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/posix/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/rcache/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/krb5/unicode/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/profile/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

objs/%.o pics/%.o: $(REL_PATH)/support/%.c
	$(COMPILE.c)  -o $@ $<
	$(POST_PROCESS_O)

# include library targets
include $(REL_PATH)/../../Makefile.targ

SOURCES= \
	$(CRYPTO_OS_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/crypto/os/%.c)\
	$(K5_OS_UTS:%.o=$(SRC)/uts/common/gssapi/mechs/krb5/krb5/os/%.c) \
	$(K5_OS:%.o=$(SRC)/lib/gss_mechs/mech_krb5/krb5/os/%.c) \
	$(CRYPTO:%.o=$(SRC)/lib/gss_mechs/mech_krb5/crypto/%.c) \
	$(CRYPTO_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/crypto/%.c)\
	$(CRYPTO_RAW:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/crypto/raw/%.c)\
	$(CRYPTO_DES:%.o= $(SRC)/lib/gss_mechs/mech_krb5/crypto/des/%.c)\
	$(CRYPTO_DES_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/crypto/des/%.c)\
	$(CRYPTO_ARCFOUR:%.o= $(SRC)/lib/gss_mechs/mech_krb5/crypto/arcfour/%.c)\
	$(CRYPTO_ARCFOUR_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/crypto/arcfour/%.c)\
	$(CRYPTO_AES:%.o= $(SRC)/lib/gss_mechs/mech_krb5/crypto/aes/%.c)\
	$(CRYPTO_DK:%.o= $(SRC)/lib/gss_mechs/mech_krb5/crypto/dk/%.c)\
	$(CRYPTO_DK_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/crypto/dk/%.c)\
	$(CRYPTO_CRC32:%.o= $(SRC)/lib/gss_mechs/mech_krb5/crypto/crc32/%.c)\
	$(CRYPTO_MD4:%.o= $(SRC)/lib/gss_mechs/mech_krb5/crypto/md4/%.c)\
	$(CRYPTO_CRC32_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/crypto/crc32/%.c)\
	$(CRYPTO_ENC:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/crypto/enc_provider/%.c)\
	$(CRYPTO_HASH:%.o= $(SRC)/lib/gss_mechs/mech_krb5/crypto/hash_provider/%.c)\
	$(CRYPTO_HASH_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/crypto/hash_provider/%.c)\
	$(CRYPTO_KEYHASH:%.o= $(SRC)/lib/gss_mechs/mech_krb5/crypto/keyhash_provider/%.c)\
	$(CRYPTO_KEYHASH_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/crypto/keyhash_provider/%.c)\
	$(CRYPTO_OLD:%.o= $(SRC)/lib/gss_mechs/mech_krb5/crypto/old/%.c)\
	$(CRYPTO_OLD_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/crypto/old/%.c)\
	$(DTRACE_OBJS:%.o= $(SRC)/lib/gss_mechs/mech_krb5/dtrace/%.c) \
	$(ET:%.o= $(SRC)/lib/gss_mechs/mech_krb5/et/%.c) \
	$(K5_ASN1:%.o= $(SRC)/lib/gss_mechs/mech_krb5/krb5/asn.1/%.c) \
	$(K5_CC:%.o= $(SRC)/lib/gss_mechs/mech_krb5/krb5/ccache/%.c) \
	$(K5_KT:%.o= $(SRC)/lib/gss_mechs/mech_krb5/krb5/keytab/%.c) \
	$(K5_KRB:%.o= $(SRC)/lib/gss_mechs/mech_krb5/krb5/krb/%.c)\
	$(K5_KRB_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/krb5/krb/%.c)\
	$(K5_OS:%.o= $(SRC)/lib/gss_mechs/mech_krb5/krb5/os/%.c)\
	$(K5_OS_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/krb5/os/%.c)\
	$(K5_POSIX:%.o= $(SRC)/lib/gss_mechs/mech_krb5/krb5/posix/%.c)\
	$(K5_RCACHE:%.o= $(SRC)/lib/gss_mechs/mech_krb5/krb5/rcache/%.c) \
	$(K5_UNICODE:%.o= $(SRC)/lib/gss_mechs/mech_krb5/krb5/unicode/%.c) \
	$(MECH:%.o= $(SRC)/lib/gss_mechs/mech_krb5/mech/%.c) \
	$(MECH_UTS:%.o= $(SRC)/uts/common/gssapi/mechs/krb5/mech/%.c) \
	$(GSSAPI_UTS:%.o= $(SRC)/uts/common/gssapi/%.c) \
	$(PROFILE_OBJS:%.o= $(SRC)/lib/gss_mechs/mech_krb5/profile/%.c) \
	$(SUPPORT_OBJS:%.o= $(SRC)/lib/gss_mechs/mech_krb5/support/%.c)

kwarnd.h:	$(SRC)/cmd/krb5/kwarn/kwarnd.x
	$(RM) $@
	$(RPCGEN) -M -h $(SRC)/cmd/krb5/kwarn/kwarnd.x | \
	$(SED) -e 's!$(SRC)/cmd/krb5/kwarn/kwarnd.h!kwarnd.h!' > $@

kwarnd_xdr.c:	kwarnd.h $(SRC)/cmd/krb5/kwarn/kwarnd.x
	$(RM) $@
	$(RPCGEN) -M -c $(SRC)/cmd/krb5/kwarn/kwarnd.x | \
	$(SED) -e 's!$(SRC)/cmd/krb5/kwarn/kwarnd.h!kwarnd.h!' > $@

kwarnd_clnt.c:   kwarnd.h $(SRC)/cmd/krb5/kwarn/kwarnd.x
	$(RM) $@
	$(RPCGEN) -M -l $(SRC)/cmd/krb5/kwarn/kwarnd.x | \
	$(SED) -e 's!$(SRC)/cmd/krb5/kwarn/kwarnd.h!kwarnd.h!' > $@

kwarnd_clnt_stubs.c: kwarnd.h $(SRC)/cmd/krb5/kwarn/kwarnd_clnt_stubs.c
	$(RM) $@
	$(CP) $(SRC)/cmd/krb5/kwarn/kwarnd_clnt_stubs.c $@

kwarnd_handle.c: $(SRC)/cmd/krb5/kwarn/kwarnd_handle.c
	$(RM) $@
	$(CP) $(SRC)/cmd/krb5/kwarn/kwarnd_handle.c $@

# DTrace

KRB_DTRACE_SRC = $(REL_PATH)dtrace

KERBEROS_D = kerberos.d
KERBEROS_D_SRC = $(KERBEROS_D:%=%.m4)
ROOT_D = $(ROOT)/usr/lib/dtrace/$(KERBEROS_D)

KRB_IMPL_HDR = kerberos_dtrace_impl.h
KRB_IMPL_HDR_SRC = $(KRB_IMPL_HDR:%=%.m4)

PROVIDER_SRC = kerberos_provider.d
PROVIDER_HDR =	$(PROVIDER_SRC:%.d=%.h)
PROVIDER_OBJ =	$(PROVIDER_SRC:%.d=%.o)

DTRACE_HDR = $(KRB_IMPL_HDR) $(PROVIDER_HDR)

$(DYNLIB): $(PROVIDER_OBJ)

# Rules for building intermediate files
$(PROVIDER_HDR): $(KRB_DTRACE_SRC)/$(PROVIDER_SRC)
	$(DTRACE) -xnolibs -h -s $? -o $@

$(PROVIDER_OBJ): $(KRB_DTRACE_SRC)/$(PROVIDER_SRC) $(PICS)
	$(COMPILE.d) -xnolibs -s $(KRB_DTRACE_SRC)/$(PROVIDER_SRC) $(PICS) -o $@

$(KRB_IMPL_HDR): $(KRB_DTRACE_SRC)/$(KRB_IMPL_HDR_SRC)
	$(M4) -D$(M4ARG)=1 $? > $@

$(KERBEROS_D): $(KRB_DTRACE_SRC)/$(KERBEROS_D_SRC)
	$(M4) $? > $@

$(ROOT)/usr/lib/dtrace/%: %
	$(INS.file)

CLEANFILES += $(DTRACE_HDR) $(PROVIDER_OBJ)
CLOBBERFILES += $(KERBEROS_D)

# So lint.out won't be needlessly recreated
lint: $(LINTOUT)

$(LINTOUT): $(DTRACE_HDR) $(SOURCES)
	$(LINT.c) -o $(LIBNAME) $(SOURCES) > $(LINTOUT) 2>&1
