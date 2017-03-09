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
#
# Copyright 2017 Hayashi Naoyuki
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#

CRTI = crti.o
CRTN = crtn.o
CRT1 = crt1.o
GCRT1 = gcrt1.o
VALUES = values-Xa.o values-Xc.o values-Xs.o values-Xt.o \
		values-xpg4.o values-xpg6.o

# include library definitions
include ../../Makefile.lib

POST_PROCESS_O = $(PROCESS_COMMENT) $@ ; $(STRIP) -x $@

OBJECTS = $(VALUES) $(CRTI) $(CRTN)
OBJECTS_i386=		$(CRT1) $(GCRT1)
OBJECTS_amd64=		$(CRT1) $(GCRT1)
OBJECTS_alpha=		$(CRT1)
OBJECTS_aarch64=	$(CRT1)
OBJECTS += $(OBJECTS_$(MACH))

ROOTLIB=	$(ROOT)/usr/lib
ROOTLIB64=	$(ROOTLIB)/$(MACH64)
ROOTOBJECTS=	$(OBJECTS:%=$(ROOTLIB)/%)
ROOTOBJECTS64=	$(OBJECTS:%=$(ROOTLIB64)/%)

ASFLAGS_i386	+= -P -D__STDC__
ASFLAGS_sparc	+= -P -D__STDC__
ASFLAGS_alpha	+= -c
ASFLAGS_aarch64	+= -c
ASFLAGS		+= $(ASFLAGS_$(MACH)) -D_ASM -DPIC

values-xpg6.o :  CPPFLAGS += -I$(SRC)/lib/libc/inc
$(VALUES) :  CFLAGS += $(C_PICFLAGS)
$(VALUES) :  CFLAGS64 += $(C_PICFLAGS64)

.KEEP_STATE:

all:	$(OBJECTS)

clean clobber:
	$(RM) $(OBJECTS)

lint:

%.o:	../common/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

%.o:	%.s
	$(COMPILE.s) -o $@ $<
	$(POST_PROCESS_O)

# install rule for ROOTOBJECTS and ROOTOBJECTS64

$(ROOTLIB)/%.o: %.o
	$(INS.file)

$(ROOTLIB64)/%.o: %.o
	$(INS.file)
