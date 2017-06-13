/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2017 Jason king
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "libcmdutils.h"

#define	INDEX_MAX	(7)	/* the largest prefix supported */

void
nicenum_scale(uint64_t n, size_t units, char *buf, size_t buflen,
    uint32_t flags)
{
	uint64_t divamt = 1024;
	uint64_t divisor = 1;
	int index = 0;
	int rc = 0;
	char u;

	if (units == 0)
		units = 1;

	if (n > 0) {
		n *= units;
		if (n < units)
			goto overflow;
	}

	if (flags & NN_DIVISOR_1000)
		divamt = 1000;

	while (index < INDEX_MAX) {
		uint64_t newdiv = divisor * divamt;

		if (newdiv < divamt)
			goto overflow;

		if (n < newdiv)
			break;

		divisor = newdiv;
		index++;
	}

	u = " KMGTPE"[index];

	if (index == 0) {
		rc = snprintf(buf, buflen, "%llu", n);
	} else if (n % divisor == 0) {
		/*
		 * If this is an even multiple of the base, always display
		 * without any decimal precision.
		 */
		rc = snprintf(buf, buflen, "%llu%c", n / divisor, u);
	} else {
		/*
		 * We want to choose a precision that reflects the best choice
		 * for fitting in 5 characters.  This can get rather tricky
		 * when we have numbers that are very close to an order of
		 * magnitude.  For example, when displaying 10239 (which is
		 * really 9.999K), we want only a single place of precision
		 * for 10.0K.  We could develop some complex heuristics for
		 * this, but it's much easier just to try each combination
		 * in turn.
		 */
		int i;
		for (i = 2; i >= 0; i--) {
			if ((rc = snprintf(buf, buflen, "%.*f%c", i,
			    (double)n / divisor, u)) <= 5)
				break;
		}
	}

	if (rc + 1 > buflen || rc < 0)
		goto overflow;

	return;

overflow:
	/* prefer a more verbose message if possible */
	if (buflen > 10)
		(void) strlcpy(buf, "<overflow>", buflen);
	else
		(void) strlcpy(buf, "??", buflen);
}

void
nicenum(uint64_t num, char *buf, size_t buflen)
{
	nicenum_scale(num, 1, buf, buflen, 0);
}
