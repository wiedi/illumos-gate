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
# Copyright 2017 Hayashi Naoyuki
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# lib/pkcs11/libkcfd/Makefile.com
#

LIBRARY = libkcfd.a
VERS = .1
OBJECTS= sym_import.o
include	../../../Makefile.lib
include $(SRC)/lib/Makefile.rootfs

LDLIBS +=	$(ROOT)/usr/lib/security/pkcs11_softtoken.so -Wl,-rpath -Wl,/usr/lib/security
LIBS=		$(DYNLIB) $(LINTLIB)
SRCDIR=		../common
DYNFLAGS +=	-Wl,-F -Wl,pkcs11_softtoken.so.1
all: $(LIBS)
include	../../../Makefile.targ
pics/%.o: $(SRCDIR)/%.s
	$(COMPILE.s) -c -o $@ $<
