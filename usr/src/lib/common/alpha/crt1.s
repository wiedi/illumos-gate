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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2017 Hayashi Naoyuki
 */

#include <sys/asm_linkage.h>

	ENTRY(_start)
	br	pv, 1f
1:	LDGP(pv)

	lda	t0, _DYNAMIC
	beq	t0, 1f
	CALL(atexit)
1:
	lda	a0, _fini
	CALL(atexit)

/*
 * On entry, the process stack looks like this:
 *
 *	#_______________________#  high addresses
 *	#	strings		#
 *	#_______________________#
 *	#	0 word		#
 *	#_______________________#
 *	#	Auxiliary	#
 *	#	entries		#
 *	#	...		#
 *	#	(size varies)	#
 *	#_______________________#
 *	#	0 word		#
 *	#_______________________#
 *	#	Environment	#
 *	#	pointers	#
 *	#	...		#
 *	#	(one word each)	#
 *	#_______________________#
 *	#	0 word		#
 *	#_______________________#
 *	#	Argument	# low addresses
 *	#	pointers	#
 *	#	Argc words	#
 *	#_______________________#
 *	#	argc		#
 *	#_______________________# <- sp
 *
 */

	ldq	a0, 0(sp)	// a0 <- argc
	lda	a1, 8(sp)	// a1 <- &argv[0]

	/* ___Argv */
	lda	t0, ___Argv
	stq	a1, 0(t0)

	/* _environ */
	lda	t0, _environ
	ldq	a2, 0(t0)
	bne	a2, 1f
	lda	a2, 16(sp)
	s8addq	a0, a2, a2	// a2 <- environ
	stq	a2, 0(t0)
1:
	lda	sp, -8*4(sp)
	stq	a0, 8*0(sp)
	stq	a1, 8*1(sp)
	stq	a2, 8*2(sp)

	CALL(__fpstart)
	CALL(_init)

	ldq	a0, 8*0(sp)
	ldq	a1, 8*1(sp)
	ldq	a2, 8*2(sp)
	lda	sp, 8*4(sp)

	CALL(main)

	mov	v0, a0
	CALL(exit)
	CALL(_exit)

2:	br	zero, 2b
	SET_SIZE(_start)
