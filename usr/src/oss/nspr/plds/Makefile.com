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

LIBRARY = libplds4.a
VERS = .1
OBJECTS = plarena.o plhash.o plvrsion.o
include ../../Makefile.nspr

HDRDIR=		$(NSPR_BASE)/lib/ds
SRCDIR=		$(NSPR_BASE)/lib/ds

LIBS =		$(DYNLIB)
CFLAGS += -_gcc=-fvisibility=hidden
CERRWARN +=	-_gcc=-Wno-unused-but-set-variable
CPPFLAGS+= -I.
LDLIBS += -lnspr4
MAPFILE=$(SRCDIR)/plds.def
MAPFILES=mapfile-vers

.PHONY: _pl_bld.h
$(PICS) : _pl_bld.h

all: $(LIBS)
install: all $(ROOTLIBS) $(ROOTLINKS)
install_h: $(ROOTHDRS)

include $(SRC)/lib/Makefile.targ

$(LIBS): $(MAPFILES)
$(MAPFILES): $(MAPFILE)
	grep -v ';-' $< | sed -e 's,;+,,' -e 's; DATA ;;' -e 's,;;,,' -e 's,;.*,;,' > $@

CLEANFILES += _pl_bld.h $(MAPFILES)

_pl_bld.h:
	echo '#define _BUILD_STRING "$(shell date "+%Y-%m-%d %T")"' > $@
	echo '#define _PRODUCTION "libplds4.so"' >> $@

$(ROOTLIBDIR):
	$(INS.dir)
$(ROOTLIBS): $(ROOTLIBDIR)
