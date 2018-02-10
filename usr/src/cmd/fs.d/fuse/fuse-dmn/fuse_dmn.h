/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2012 Nexenta Systems, Inc.  All rights reserved.
 */

#include <sys/note.h>

#define	DPRINT(...)	do \
{ \
	if (fuse_debug) \
		fprintf(stderr, __VA_ARGS__); \
	_NOTE(CONSTCOND) \
} while (0)

extern int fuse_debug;

void
dmn_dispatch(void *cookie, char *cargp, size_t argsz,
		door_desc_t *dp, uint_t n_desc);
