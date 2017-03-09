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

SOURCE=coreutils-8.25.tar.xz
SOURCE_DIR=$(SOURCE:%.tar.xz=%)
PATCH=$(SOURCE:%.tar.xz=%.patch)

CONFIG_ADDITIONAL_CFLAGS= -I$(SRC)/lib/libc/inc
CONFIG_ADDITIONAL_ARGS+= --bindir=/usr/bin --sbindir=/usr/sbin --libexecdir=/lib --libdir=/lib
CONFIG_ADDITIONAL_ARGS+= --enable-no-install-program=su,kill,uptime,df,hostid,echo,uname
CONFIG_ADDITIONAL_ARGS+= --disable-acl

include ../Makefile.oss
