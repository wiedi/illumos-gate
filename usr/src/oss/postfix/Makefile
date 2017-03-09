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

include ../../Makefile.master

SOURCE=postfix-2.10.2.tar.gz
SOURCE_DIR=$(SOURCE:%.tar.gz=%)
PATCH=$(SOURCE:%.tar.gz=%.patch)

MAKE_ARGS =


SRCDIR=$(DISTFILES)
PATCHDIR=$(CURDIR)/patch
BUILDDIR=$(PICDIR)/$(SOURCE_DIR)

.PHONY: all expand post_expand install build config install_h clean clobber install_dst post_install
.PHONY: pre_config post_patch apply_patch post_expand expand patch

all: build

install: post_install

post_install: install_dst

install_dst: all
	cd $(BUILDDIR) ; \
		$(MAKE) non-interactive-package install_root=$(ROOT) manpage_directory=/usr/share/man

build: config
	cd $(BUILDDIR); \
		$(MAKE) $(MAKE_ARGS)

config: pre_config
	cd $(BUILDDIR); \
		$(MAKE) -f Makefile.init makefiles \
		CC="$(GNU_CC) -isystem $(ROOT)/usr/include -fPIC --sysroot=$(ROOT) " \
		CCARGS="-DNO_NIS" \
		AUXLIBS="-Wl,--no-relax --sysroot=$(ROOT)"

clean clobber:
	$(RM) -rf $(PICDIR)

pre_config: post_patch

post_patch: apply_patch

apply_patch: post_expand
	cd $(PICDIR)/$(SOURCE_DIR); for p in $(PATCH:%=$(PATCHDIR)/%) ; do patch -p0 < $${p}; done

post_expand: expand

expand:
	$(RM) -rf $(PICDIR)
	mkdir -p $(BUILDDIR)
	cd $(PICDIR); tar -xf $(SOURCE:%=$(SRCDIR)/%)

install_h:

patch:
	diff -cNr orig/$(SOURCE_DIR) $(SOURCE_DIR) > patch/$(PATCH); ec

$(ROOT)/etc/postfix/dist:
	$(INS.dir)

$(ROOT)/var/svc/manifest/network:
	$(INS.dir)

$(ROOT)/etc/postfix/dist/postinstall: files/postinstall $(ROOT)/etc/postfix/dist
	$(INS.file)

$(ROOT)/var/svc/manifest/network/postfix.xml: files/postfix.xml $(ROOT)/var/svc/manifest/network
	$(INS.file)

$(ROOT)/etc/postfix/dist/postinstall: install_dst
$(ROOT)/var/svc/manifest/network/postfix.xml: install_dst
post_install: $(ROOT)/etc/postfix/dist/postinstall
post_install: $(ROOT)/var/svc/manifest/network/postfix.xml

