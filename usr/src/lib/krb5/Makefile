#
# Copyright 2017 Hayashi Naoyuki
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"
#
# lib/krb5/Makefile
#
# include global definitions
include ../../Makefile.master
plugins: dyn kdb kadm5 ss
kadm5: dyn kdb
SUBDIRS= \
	dyn	\
	\
	kdb	\
	\
	kadm5	\
	\
	ss	\
	plugins

all : 		TARGET= all
install : 	TARGET= install
clean : 	TARGET= clean
clobber : 	TARGET= clobber
lint : 		TARGET= lint
_msg : 		TARGET= _msg

.KEEP_STATE:

all install clean clobber lint _msg: $(SUBDIRS)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
