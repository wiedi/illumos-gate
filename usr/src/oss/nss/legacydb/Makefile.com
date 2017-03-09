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

LIBRARY = libnssdbm3.a
VERS = .1
OBJECTS = \
	dbmshim.o \
	keydb.o \
	lgattr.o \
	lgcreate.o \
	lgdestroy.o \
	lgfind.o \
	lgfips.o \
	lginit.o \
	lgutil.o \
	lowcert.o \
	lowkey.o \
	pcertdb.o \
	pk11db.o \
	loader.o
include ../../Makefile.nss

HDRDIR=		$(NSS_BASE)/lib/softoken/legacydb
SRCDIR=		$(NSS_BASE)/lib/softoken/legacydb

LIBS =		$(DYNLIB)

MAPFILE=$(SRCDIR)/nssdbm.def
MAPFILES=mapfile-vers

CFLAGS += -DLG_LIB_NAME=\"libnssdbm3.so\"
LDLIBS += -Wl,--whole-archive ../../dbm/$(MACH)/libdbm.a -Wl,--no-whole-archive
LDLIBS += -lnssutil3 $(NSSLIBS) -lbsm

all: $(LIBS)
install: all $(ROOTLIBS) $(ROOTLINKS)

include $(SRC)/lib/Makefile.targ

$(LIBS): $(MAPFILES)
$(MAPFILES): $(MAPFILE)
	grep -v ';-' $< | sed -e 's,;+,,' -e 's; DATA ;;' -e 's,;;,,' -e 's,;.*,;,' > $@

CLEANFILES+=$(MAPFILES)

pics/loader.o: $(NSS_BASE)/lib/freebl/loader.c
	$(COMPILE.c) -o $@ $<
