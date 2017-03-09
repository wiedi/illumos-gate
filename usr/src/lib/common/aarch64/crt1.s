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
	adrp	x0, :got:_fini
	ldr	x0, [x0, #:got_lo12:_fini]
	bl	atexit

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
	ldr	x0, [sp]	// x0 <- argc
	add	x1, sp, #8	// x1 <- &argv[0]

	/* ___Argv */
	adrp	x2, :got:___Argv
	ldr	x2, [x2, #:got_lo12:___Argv]
	str	x1, [x2]

	/* _environ */
	add	x2, x0, #2
	add	x2, sp, x2, LSL #3
	adrp	x3, :got:_environ
	ldr	x3, [x3, #:got_lo12:_environ]
	str	x2, [x3]

	stp	x0, x1, [sp, #-16]!
	stp	x2, x3, [sp, #-16]!
	bl	__fpstart
	bl	_init
	ldp	x2, x3, [sp], #16
	ldp	x0, x1, [sp], #16

	bl	main
	bl	exit
	mov	x0, #0
	bl	_exit

1:	b	1b
	SET_SIZE(_start)
