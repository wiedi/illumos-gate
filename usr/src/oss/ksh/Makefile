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

PROG= ksh
ROOTFS_PROG= $(PROG)

OBJS = \
	alloc.o c_ksh.o c_sh.o c_test.o c_ulimit.o edit.o emacs.o \
	eval.o exec.o expr.o history.o io.o jobs.o lex.o mail.o \
	main.o misc.o path.o shf.o sigact.o syn.o table.o trap.o \
	tree.o tty.o var.o version.o vi.o
include ../../cmd/Makefile.cmd

SRCDIR = $(EXTRA)/ksh

CPPFLAGS+= -Iinclude -I$(SRCDIR) -I. -D_FILE_OFFSET_BITS=64
CERRWARN += -_gcc=-Wno-unused-but-set-variable

.PHONY: all
.DEFAULT_GOAL := all
emacs.o: emacs.out
trap.o:  siglist.out

siglist.out: $(SRCDIR)/config.h $(SRCDIR)/sh.h $(SRCDIR)/siglist.in $(SRCDIR)/siglist.sh
	sh $(SRCDIR)/siglist.sh "$(AS) -P $(CPPFLAGS) $(CFLAGS)" < $(SRCDIR)/siglist.in > siglist.out.tmp && mv siglist.out.tmp siglist.out

CLEANFILES+= siglist.out.tmp siglist.out tmp*.d

emacs.out: $(SRCDIR)/emacs.c
	sh $(SRCDIR)/emacs-gen.sh $(SRCDIR)/emacs.c > emacs.out.tmp && mv emacs.out.tmp emacs.out

emacs.o: emacs.out
trap.o: siglist.out

CLEANFILES+= emacs.out.tmp emacs.out

all: $(ROOTFS_PROG)
install: $(ROOTPROG)
include $(SRC)/cmd/Makefile.targ

%.o: $(SRCDIR)/%.c
	$(COMPILE.c) -o $@ $<

.PHONY: post_install
install: post_install

post_install:
	rm -f $(ROOT)/usr/bin/sh; ln -s ../bin/ksh $(ROOT)/usr/bin/sh
	rm -f $(ROOT)/usr/sbin/sh; ln -s ../bin/ksh $(ROOT)/usr/sbin/sh
	rm -f $(ROOT)/usr/sbin/ksh; ln -s ../bin/ksh $(ROOT)/usr/sbin/ksh
	rm -f $(ROOT)/sbin/ksh; ln -s ../bin/ksh $(ROOT)/sbin/ksh
	rm -f $(ROOT)/sbin/sh; ln -s ../bin/ksh $(ROOT)/sbin/sh

$(PROG): $(OBJS)
	$(LINK.c) -o $(PROG) $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

clean:
	-$(RM) $(OBJS)
	-$(RM) $(CLEANFILES)
