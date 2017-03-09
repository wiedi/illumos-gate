#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
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

#
# Copyright 2017 Hayashi Naoyuki
#

LIBRARY = libfreebl3.a
VERS = .1
OBJECTS = \
	freeblver.o \
	ldvector.o \
	sysrand.o \
	sha_fast.o \
	md2.o \
	md5.o \
	sha512.o \
	alghmac.o \
	rawhash.o \
	alg2268.o \
	arcfour.o \
	arcfive.o \
	desblapi.o \
	des.o \
	drbg.o \
	chacha20poly1305.o \
	cts.o \
	ctr.o \
	fipsfreebl.o \
	gcm.o \
	hmacct.o \
	rijndael.o \
	aeskeywrap.o \
	camellia.o \
	dh.o \
	ec.o \
	ecdecode.o \
	pqg.o \
	dsa.o \
	rsa.o \
	rsapkcs.o \
	shvfy.o \
	tlsprfalg.o \
	seed.o \
	jpake.o \
	mpprime.o mpmontg.o mplogic.o mpi.o mp_gf2m.o \
	mpcpucache.o \
	ecl.o ecl_curve.o ecl_mult.o ecl_gf.o \
	ecp_aff.o ecp_jac.o ecp_mont.o \
	ec_naf.o ecp_jm.o ecp_256.o ecp_384.o ecp_521.o \
	ecp_256_32.o \
	ec2_aff.o ec2_mont.o ec2_proj.o \
	ec2_163.o ec2_193.o ec2_233.o \
	ecp_192.o ecp_224.o
include ../../Makefile.nss

HDRDIR=		$(NSS_BASE)/lib/freebl
SRCDIR=		$(NSS_BASE)/lib/freebl

LIBS =		$(DYNLIB)

MAPFILE=$(SRCDIR)/freebl.def
MAPFILES=mapfile-vers
CPPFLAGS += -DRIJNDAEL_INCLUDE_TABLES -DMP_API_COMPATIBLE
CFLAGS +=
LDLIBS += -lnssutil3
LDLIBS += $(NSSLIBS)

all: $(LIBS)
install: all $(ROOTLIBS) $(ROOTLINKS)

include $(SRC)/lib/Makefile.targ

$(LIBS): $(MAPFILES)
$(MAPFILES): $(MAPFILE)
	grep -v ';-' $< | sed -e 's,;+,,' -e 's; DATA ;;' -e 's,;;,,' -e 's,;.*,;,' > $@

CLEANFILES+=$(MAPFILES)

pics/%.o: $(SRCDIR)/mpi/%.c
	$(COMPILE.c) -o $@ $<

pics/%.o: $(SRCDIR)/ecl/%.c
	$(COMPILE.c) -o $@ $<
