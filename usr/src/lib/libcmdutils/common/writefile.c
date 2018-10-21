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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	All Rights Reserved	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

#include <sys/sendfile.h>
#include "libcmdutils.h"

#define	MIN(a, b)	((a) < (b) ? (a) : (b))
#define	MAX_CHUNK	(64 * 1024 * 1024)

int
writefile(int fi, int fo, char *infile, char *outfile, char *asfile,
    char *atfile, struct stat *s1p, struct stat *s2p)
{
	off_t filesize = s1p->st_size;
	off_t offset;
	off_t remains;
	int n, chunk_size;
	size_t src_size;
	size_t targ_size;
	char *srcbuf;
	char *targbuf;

	if (asfile != NULL) {
		src_size = strlen(infile) + strlen(asfile) +
		    strlen(dgettext(TEXT_DOMAIN, " attribute ")) + 1;
	} else {
		src_size = strlen(infile) + 1;
	}
	srcbuf = malloc(src_size);
	if (srcbuf == NULL) {
		(void) fprintf(stderr,
		    dgettext(TEXT_DOMAIN, "could not allocate memory"
		    " for path buffer: "));
		return (1);
	}
	if (asfile != NULL) {
		(void) snprintf(srcbuf, src_size, "%s%s%s",
		    infile, dgettext(TEXT_DOMAIN, " attribute "), asfile);
	} else {
		(void) snprintf(srcbuf, src_size, "%s", infile);
	}

	if (atfile != NULL) {
		targ_size = strlen(outfile) + strlen(atfile) +
		    strlen(dgettext(TEXT_DOMAIN, " attribute ")) + 1;
	} else {
		targ_size = strlen(outfile) + 1;
	}
	targbuf = malloc(targ_size);
	if (targbuf == NULL) {
		(void) fprintf(stderr,
		    dgettext(TEXT_DOMAIN, "could not allocate memory"
		    " for path buffer: "));
		return (1);
	}
	if (atfile != NULL) {
		(void) snprintf(targbuf, targ_size, "%s%s%s",
		    outfile, dgettext(TEXT_DOMAIN, " attribute "), atfile);
	} else {
		(void) snprintf(targbuf, targ_size, "%s", outfile);
	}

	if (S_ISREG(s1p->st_mode)) {
		offset = 0;
		remains = filesize;
		while (remains > 0) {
			ssize_t res;
			chunk_size = MIN(remains, MAX_CHUNK);
			res = sendfile(fo, fi, &offset, chunk_size);
			if (res == -1) {
				if (errno == EINTR)
					continue;

				perror(srcbuf);
				(void) close(fi);
				(void) close(fo);
				if (S_ISREG(s2p->st_mode))
					(void) unlink(targbuf);
				return (1);
			}
			remains -= res;
		}
	} else {
		char buf[SMALLFILESIZE];
		for (;;) {
			n = read(fi, buf, sizeof (buf));
			if (n == 0) {
				return (0);
			} else if (n < 0) {
				(void) close(fi);
				(void) close(fo);
				if (S_ISREG(s2p->st_mode))
					(void) unlink(targbuf);
				if (srcbuf != NULL)
					free(srcbuf);
				if (targbuf != NULL)
					free(targbuf);
				return (1);
			} else if (write(fo, buf, n) != n) {
				(void) close(fi);
				(void) close(fo);
				if (S_ISREG(s2p->st_mode))
					(void) unlink(targbuf);
				if (srcbuf != NULL)
					free(srcbuf);
				if (targbuf != NULL)
					free(targbuf);
				return (1);
			}
		}
	}
	if (srcbuf != NULL)
		free(srcbuf);
	if (targbuf != NULL)
		free(targbuf);
	return (0);
}
