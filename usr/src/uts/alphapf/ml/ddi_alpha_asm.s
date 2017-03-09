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


#include <sys/asm_linkage.h>
#include <sys/pal.h>
#include "assym.h"

#define NOSWAP(reg, tmp0)

#define SWAP16(reg, tmp0)		\
	srl	reg, 0x8, tmp0 ;	\
	insbl	reg, 0x1, reg ;		\
	or	reg, tmp0, reg

#define SWAP32(reg, tmp0)		\
	inslh	reg, 0x7, tmp0 ;	\
	inswl	reg, 0x3, reg ;		\
	or	tmp0, reg, reg ;	\
	srl	reg, 0x10, tmp0 ;	\
	zapnot	tmp0, 0xf5, tmp0 ;	\
	zapnot	reg, 0xfa, reg ;	\
	addl	tmp0, reg, reg

#define SWAP64(reg, tmp0)		\
	srl	reg, 0x20, tmp0 ;	\
	sll	reg, 0x20, reg ;	\
	or	tmp0, reg, reg ;	\
	srl	reg, 0x10, tmp0 ;	\
	sll	reg, 0x10, reg ;	\
	zapnot	tmp0, 0x33, tmp0 ;	\
	zapnot	reg, 0xcc, reg ;	\
	or	tmp0, reg, reg ;	\
	srl	reg, 0x8, tmp0 ;	\
	sll	reg, 0x8, reg ;		\
	zapnot	reg, 0xaa, reg ;	\
	zapnot	tmp0, 0x55, tmp0 ;	\
	or	tmp0, reg, reg

/*
 *  uint8_t i_ddi_get8(ddi_acc_impl_t *handle, uint8_t *addr)
 *  uint16_t i_ddi_get16(ddi_acc_impl_t *handle, uint16_t *addr)
 *  uint32_t i_ddi_get32(ddi_acc_impl_t *handle, uint32_t *addr)
 *  uint64_t i_ddi_get64(ddi_acc_impl_t *handle, uint64_t *addr)
 */
	ENTRY(i_ddi_get8)
	mb
	ldbu	v0, 0(a1)
	ret
	SET_SIZE(i_ddi_get8)

	ENTRY(i_ddi_get16)
	mb
	ldwu	v0, 0(a1)
	ret
	SET_SIZE(i_ddi_get16)

	ENTRY(i_ddi_get32)
	mb
	ldl	v0, 0(a1)
	ret
	SET_SIZE(i_ddi_get32)

	ENTRY(i_ddi_get64)
	mb
	ldq	v0, 0(a1)
	ret
	SET_SIZE(i_ddi_get64)


/*
 *  void i_ddi_put8(ddi_acc_impl_t *handle, uint8_t *addr, uint8_t value)
 *  void i_ddi_put16(ddi_acc_impl_t *handle, uint16_t *addr, uint16_t value)
 *  void i_ddi_put32(ddi_acc_impl_t *handle, uint32_t *addr, uint32_t value)
 *  void i_ddi_put64(ddi_acc_impl_t *handle, uint64_t *addr, uint64_t value)
 */
	ENTRY(i_ddi_put8)
	mb
	stb	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_put8)

	ENTRY(i_ddi_put16)
	mb
	stw	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_put16)

	ENTRY(i_ddi_put32)
	mb
	stl	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_put32)

	ENTRY(i_ddi_put64)
	mb
	stq	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_put64)


/*
 *  uint8_t i_ddi_io_get8(ddi_acc_impl_t *handle, uint8_t *addr)
 *  uint16_t i_ddi_io_get16(ddi_acc_impl_t *handle, uint16_t *addr)
 *  uint32_t i_ddi_io_get32(ddi_acc_impl_t *handle, uint32_t *addr)
 *  uint64_t i_ddi_io_get64(ddi_acc_impl_t *handle, uint64_t *addr)
 */
	ENTRY(i_ddi_io_get8)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	ldbu	v0, 0(a1)
	ret
	SET_SIZE(i_ddi_io_get8)

	ENTRY(i_ddi_io_get16)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	ldwu	v0, 0(a1)
	ret
	SET_SIZE(i_ddi_io_get16)

	ENTRY(i_ddi_io_get32)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	ldl	v0, 0(a1)
	ret
	SET_SIZE(i_ddi_io_get32)

	ENTRY(i_ddi_io_get64)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	ldq	v0, 0(a1)
	ret
	SET_SIZE(i_ddi_io_get64)


/*
 *  void i_ddi_io_put8(ddi_acc_impl_t *handle, uint8_t *addr, uint8_t value)
 *  void i_ddi_io_put16(ddi_acc_impl_t *handle, uint16_t *addr, uint16_t value)
 *  void i_ddi_io_put32(ddi_acc_impl_t *handle, uint32_t *addr, uint32_t value)
 *  void i_ddi_io_put64(ddi_acc_impl_t *handle, uint64_t *addr, uint64_t value)
 */
	ENTRY(i_ddi_io_put8)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	stb	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_io_put8)

	ENTRY(i_ddi_io_put16)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	stw	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_io_put16)

	ENTRY(i_ddi_io_put32)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	stl	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_io_put32)

	ENTRY(i_ddi_io_put64)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	stq	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_io_put64)


/*
 *  uint16_t i_ddi_swap_get16(ddi_acc_impl_t *handle, uint16_t *addr)
 *  uint32_t i_ddi_swap_get32(ddi_acc_impl_t *handle, uint32_t *addr)
 *  uint64_t i_ddi_swap_get64(ddi_acc_impl_t *handle, uint64_t *addr)
 */
	ENTRY(i_ddi_swap_get16)
	mb
	ldwu	v0, 0(a1)
	SWAP16(v0, t0)
	ret
	SET_SIZE(i_ddi_swap_get16)

	ENTRY(i_ddi_swap_get32)
	mb
	ldl	v0, 0(a1)
	SWAP32(v0, t0)
	ret
	SET_SIZE(i_ddi_swap_get32)

	ENTRY(i_ddi_swap_get64)
	mb
	ldq	v0, 0(a1)
	SWAP64(v0, t0)
	ret
	SET_SIZE(i_ddi_swap_get64)

/*
 *  void i_ddi_swap_put16(ddi_acc_impl_t *handle, uint16_t *addr, uint16_t value)
 *  void i_ddi_swap_put32(ddi_acc_impl_t *handle, uint32_t *addr, uint32_t value)
 *  void i_ddi_swap_put64(ddi_acc_impl_t *handle, uint64_t *addr, uint64_t value)
 */
	ENTRY(i_ddi_swap_put16)
	SWAP16(a2, t0)
	mb
	stw	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_swap_put16)

	ENTRY(i_ddi_swap_put32)
	SWAP32(a2, t0)
	mb
	stl	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_swap_put32)

	ENTRY(i_ddi_swap_put64)
	SWAP64(a2, t0)
	mb
	stq	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_swap_put64)


/*
 *  uint16_t i_ddi_io_swap_get16(ddi_acc_impl_t *handle, uint16_t *addr)
 *  uint32_t i_ddi_io_swap_get32(ddi_acc_impl_t *handle, uint32_t *addr)
 *  uint64_t i_ddi_io_swap_get64(ddi_acc_impl_t *handle, uint64_t *addr)
 */
	ENTRY(i_ddi_io_swap_get16)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	ldwu	v0, 0(a1)
	SWAP16(v0, t0)
	ret
	SET_SIZE(i_ddi_io_swap_get16)

	ENTRY(i_ddi_io_swap_get32)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	ldl	v0, 0(a1)
	SWAP32(v0, t0)
	ret
	SET_SIZE(i_ddi_io_swap_get32)

	ENTRY(i_ddi_io_swap_get64)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	ldq	v0, 0(a1)
	SWAP64(v0, t0)
	ret
	SET_SIZE(i_ddi_io_swap_get64)

/*
 *  void i_ddi_io_swap_put16(ddi_acc_impl_t *handle, uint16_t *addr, uint16_t value)
 *  void i_ddi_io_swap_put32(ddi_acc_impl_t *handle, uint32_t *addr, uint32_t value)
 *  void i_ddi_io_swap_put64(ddi_acc_impl_t *handle, uint64_t *addr, uint64_t value)
 */
	ENTRY(i_ddi_io_swap_put16)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	SWAP16(a2, t0)
	mb
	stw	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_io_swap_put16)

	ENTRY(i_ddi_io_swap_put32)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	SWAP32(a2, t0)
	mb
	stl	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_io_swap_put32)

	ENTRY(i_ddi_io_swap_put64)
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	SWAP64(a2, t0)
	mb
	stq	a2, 0(a1)
	wmb
	ret
	SET_SIZE(i_ddi_io_swap_put64)


#define DDI_REP_GET(n, ldinst, stinst, swap)	\
	lda	t0, -DDI_DEV_AUTOINCR(a4) ;	\
	cmoveq	t0, n, t1 ;			\
	cmovne	t0, 0, t1 ;			\
	mov	n, t0 ;				\
1:	beq	a3, 2f ;			\
	subq	a3, 1, a3 ;			\
	mb ;					\
	ldinst	t2, 0(a2) ;			\
	addq	a2, t1, a2 ;			\
	swap(t2, t3) ;				\
	stinst	t2, 0(a1) ;			\
	addq	a1, t0, a1 ;			\
	br	zero, 1b ;			\
2:

#define DDI_REP_PUT(n, ldinst, stinst, swap)	\
	lda	t0, -DDI_DEV_AUTOINCR(a4) ;	\
	cmoveq	t0, n, t1 ;			\
	cmovne	t0, 0, t1 ;			\
	mov	n, t0 ;				\
	mb ;					\
1:	beq	a3, 2f ;			\
	subq	a3, 1, a3 ;			\
	ldinst	t2, 0(a1) ;			\
	addq	a1, t1, a1 ;			\
	swap(t2, t3) ;				\
	stinst	t2, 0(a2) ;			\
	wmb ;					\
	addq	a2, t0, a2 ;			\
	br	zero, 1b ;			\
2:

#define DDI_IO_REP_GET(n, ldinst, stinst, swap)	\
	ldq	t2, AHI_IO_PORT_BASE(a0) ;	\
	addq	t2, a2, a2 ;			\
	DDI_REP_GET(n, ldinst, stinst, swap)

#define DDI_IO_REP_PUT(n, ldinst, stinst, swap)	\
	ldq	t2, AHI_IO_PORT_BASE(a0) ;	\
	addq	t2, a2, a2 ;			\
	DDI_REP_PUT(n, ldinst, stinst, swap)


	ENTRY(i_ddi_rep_get8)
	DDI_REP_GET(1, ldbu, stb, NOSWAP)
	ret
	SET_SIZE(i_ddi_rep_get8)

	ENTRY(i_ddi_rep_get16)
	DDI_REP_GET(2, ldwu, stw, NOSWAP)
	ret
	SET_SIZE(i_ddi_rep_get16)

	ENTRY(i_ddi_rep_get32)
	DDI_REP_GET(4, ldl, stl, NOSWAP)
	ret
	SET_SIZE(i_ddi_rep_get32)

	ENTRY(i_ddi_rep_get64)
	DDI_REP_GET(8, ldq, stq, NOSWAP)
	ret
	SET_SIZE(i_ddi_rep_get64)

	ENTRY(i_ddi_rep_put8)
	DDI_REP_PUT(1, ldbu, stb, NOSWAP)
	ret
	SET_SIZE(i_ddi_rep_put8)

	ENTRY(i_ddi_rep_put16)
	DDI_REP_PUT(2, ldwu, stw, NOSWAP)
	ret
	SET_SIZE(i_ddi_rep_put16)

	ENTRY(i_ddi_rep_put32)
	DDI_REP_PUT(4, ldl, stl, NOSWAP)
	ret
	SET_SIZE(i_ddi_rep_put32)

	ENTRY(i_ddi_rep_put64)
	DDI_REP_PUT(8, ldq, stq, NOSWAP)
	ret
	SET_SIZE(i_ddi_rep_put64)

	ENTRY(i_ddi_io_rep_get8)
	DDI_IO_REP_GET(1, ldbu, stb, NOSWAP)
	ret
	SET_SIZE(i_ddi_io_rep_get8)

	ENTRY(i_ddi_io_rep_get16)
	DDI_IO_REP_GET(2, ldwu, stw, NOSWAP)
	ret
	SET_SIZE(i_ddi_io_rep_get16)

	ENTRY(i_ddi_io_rep_get32)
	DDI_IO_REP_GET(4, ldl, stl, NOSWAP)
	ret
	SET_SIZE(i_ddi_io_rep_get32)

	ENTRY(i_ddi_io_rep_get64)
	DDI_IO_REP_GET(8, ldq, stq, NOSWAP)
	ret
	SET_SIZE(i_ddi_io_rep_get64)

	ENTRY(i_ddi_io_rep_put8)
	DDI_IO_REP_PUT(1, ldbu, stb, NOSWAP)
	ret
	SET_SIZE(i_ddi_io_rep_put8)

	ENTRY(i_ddi_io_rep_put16)
	DDI_IO_REP_PUT(2, ldwu, stw, NOSWAP)
	ret
	SET_SIZE(i_ddi_io_rep_put16)

	ENTRY(i_ddi_io_rep_put32)
	DDI_IO_REP_PUT(4, ldl, stl, NOSWAP)
	ret
	SET_SIZE(i_ddi_io_rep_put32)

	ENTRY(i_ddi_io_rep_put64)
	DDI_IO_REP_PUT(8, ldq, stq, NOSWAP)
	ret
	SET_SIZE(i_ddi_io_rep_put64)

	ENTRY(i_ddi_swap_rep_get16)
	DDI_REP_GET(2, ldwu, stw, SWAP16)
	ret
	SET_SIZE(i_ddi_swap_rep_get16)

	ENTRY(i_ddi_swap_rep_get32)
	DDI_REP_GET(4, ldl, stl, SWAP32)
	ret
	SET_SIZE(i_ddi_swap_rep_get32)

	ENTRY(i_ddi_swap_rep_get64)
	DDI_REP_GET(8, ldq, stq, SWAP64)
	ret
	SET_SIZE(i_ddi_swap_rep_get64)

	ENTRY(i_ddi_swap_rep_put16)
	DDI_REP_PUT(2, ldwu, stw, SWAP16)
	ret
	SET_SIZE(i_ddi_swap_rep_put16)

	ENTRY(i_ddi_swap_rep_put32)
	DDI_REP_PUT(4, ldl, stl, SWAP32)
	ret
	SET_SIZE(i_ddi_swap_rep_put32)

	ENTRY(i_ddi_swap_rep_put64)
	DDI_REP_PUT(8, ldq, stq, SWAP64)
	ret
	SET_SIZE(i_ddi_swap_rep_put64)

	ENTRY(i_ddi_io_swap_rep_get16)
	DDI_IO_REP_GET(2, ldwu, stw, SWAP16)
	ret
	SET_SIZE(i_ddi_io_swap_rep_get16)

	ENTRY(i_ddi_io_swap_rep_get32)
	DDI_IO_REP_GET(4, ldl, stl, SWAP32)
	ret
	SET_SIZE(i_ddi_io_swap_rep_get32)

	ENTRY(i_ddi_io_swap_rep_get64)
	DDI_IO_REP_GET(8, ldq, stq, SWAP64)
	ret
	SET_SIZE(i_ddi_io_swap_rep_get64)

	ENTRY(i_ddi_io_swap_rep_put16)
	DDI_IO_REP_PUT(2, ldwu, stw, SWAP16)
	ret
	SET_SIZE(i_ddi_io_swap_rep_put16)

	ENTRY(i_ddi_io_swap_rep_put32)
	DDI_IO_REP_PUT(4, ldl, stl, SWAP32)
	ret
	SET_SIZE(i_ddi_io_swap_rep_put32)

	ENTRY(i_ddi_io_swap_rep_put64)
	DDI_IO_REP_PUT(8, ldq, stq, SWAP64)
	ret
	SET_SIZE(i_ddi_io_swap_rep_put64)


#define	DDI_PROTECT()				\
	excb;					\
	call_pal PAL_rdval;			\
	ldq	t11, CPU_THREAD(v0);		\
	ldq	t4, AHI_ERR(a0);		\
	ldq	t4, ERR_ONTRAP(t4);		\
	ldq	t5, T_ONTRAP(t11);		\
	stq	t5, OT_PREV(t4);		\
	stq	t4, T_ONTRAP(t11);		\
	addq	t4, OT_JMPBUF, t4;		\
	stq	ra, (LABEL_REG_PC*8)(t4);

#define	DDI_NOPROTECT()				\
	excb;					\
	ldq	t4, T_ONTRAP(t11);		\
	ldq	t5, OT_PREV(t4);		\
	stq	t5, T_ONTRAP(t11);

	ENTRY(i_ddi_prot_get8)
	DDI_PROTECT()
	mb
	ldbu	v0, 0(a1)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_get8)

	ENTRY(i_ddi_prot_get16)
	DDI_PROTECT()
	mb
	ldwu	v0, 0(a1)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_get16)

	ENTRY(i_ddi_prot_get32)
	DDI_PROTECT()
	mb
	ldl	v0, 0(a1)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_get32)

	ENTRY(i_ddi_prot_get64)
	DDI_PROTECT()
	mb
	ldq	v0, 0(a1)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_get64)

	ENTRY(i_ddi_prot_put8)
	DDI_PROTECT()
	mb
	stb	a2, 0(a1)
	wmb
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_put8)

	ENTRY(i_ddi_prot_put16)
	DDI_PROTECT()
	mb
	stw	a2, 0(a1)
	wmb
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_put16)

	ENTRY(i_ddi_prot_put32)
	DDI_PROTECT()
	mb
	stl	a2, 0(a1)
	wmb
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_put32)

	ENTRY(i_ddi_prot_put64)
	DDI_PROTECT()
	mb
	stq	a2, 0(a1)
	wmb
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_put64)

	ENTRY(i_ddi_prot_io_get8)
	DDI_PROTECT()
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	ldbu	v0, 0(a1)
	mb
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_get8)

	ENTRY(i_ddi_prot_io_get16)
	DDI_PROTECT()
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	ldwu	v0, 0(a1)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_get16)

	ENTRY(i_ddi_prot_io_get32)
	DDI_PROTECT()
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	ldl	v0, 0(a1)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_get32)

	ENTRY(i_ddi_prot_io_get64)
	DDI_PROTECT()
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	ldq	v0, 0(a1)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_get64)

	ENTRY(i_ddi_prot_io_put8)
	DDI_PROTECT()
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	stb	a2, 0(a1)
	wmb
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_put8)

	ENTRY(i_ddi_prot_io_put16)
	DDI_PROTECT()
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	stw	a2, 0(a1)
	wmb
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_put16)

	ENTRY(i_ddi_prot_io_put32)
	DDI_PROTECT()
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	stl	a2, 0(a1)
	wmb
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_put32)

	ENTRY(i_ddi_prot_io_put64)
	DDI_PROTECT()
	ldq	t0, AHI_IO_PORT_BASE(a0)
	addq	t0, a1, a1
	mb
	stq	a2, 0(a1)
	wmb
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_put64)

	ENTRY(i_ddi_prot_rep_get8)
	DDI_PROTECT()
	DDI_REP_GET(1, ldbu, stb, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_rep_get8)

	ENTRY(i_ddi_prot_rep_get16)
	DDI_PROTECT()
	DDI_REP_GET(2, ldwu, stw, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_rep_get16)

	ENTRY(i_ddi_prot_rep_get32)
	DDI_PROTECT()
	DDI_REP_GET(4, ldl, stl, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_rep_get32)

	ENTRY(i_ddi_prot_rep_get64)
	DDI_PROTECT()
	DDI_REP_GET(8, ldq, stq, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_rep_get64)

	ENTRY(i_ddi_prot_rep_put8)
	DDI_PROTECT()
	DDI_REP_PUT(1, ldbu, stb, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_rep_put8)

	ENTRY(i_ddi_prot_rep_put16)
	DDI_PROTECT()
	DDI_REP_PUT(2, ldwu, stw, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_rep_put16)

	ENTRY(i_ddi_prot_rep_put32)
	DDI_PROTECT()
	DDI_REP_PUT(4, ldl, stl, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_rep_put32)

	ENTRY(i_ddi_prot_rep_put64)
	DDI_PROTECT()
	DDI_REP_PUT(8, ldq, stq, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_rep_put64)

	ENTRY(i_ddi_prot_io_rep_get8)
	DDI_PROTECT()
	DDI_IO_REP_GET(1, ldbu, stb, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_rep_get8)

	ENTRY(i_ddi_prot_io_rep_get16)
	DDI_PROTECT()
	DDI_IO_REP_GET(2, ldwu, stw, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_rep_get16)

	ENTRY(i_ddi_prot_io_rep_get32)
	DDI_PROTECT()
	DDI_IO_REP_GET(4, ldl, stl, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_rep_get32)

	ENTRY(i_ddi_prot_io_rep_get64)
	DDI_PROTECT()
	DDI_IO_REP_GET(8, ldq, stq, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_rep_get64)

	ENTRY(i_ddi_prot_io_rep_put8)
	DDI_PROTECT()
	DDI_IO_REP_PUT(1, ldbu, stb, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_rep_put8)

	ENTRY(i_ddi_prot_io_rep_put16)
	DDI_PROTECT()
	DDI_IO_REP_PUT(2, ldwu, stw, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_rep_put16)

	ENTRY(i_ddi_prot_io_rep_put32)
	DDI_PROTECT()
	DDI_IO_REP_PUT(4, ldl, stl, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_rep_put32)

	ENTRY(i_ddi_prot_io_rep_put64)
	DDI_PROTECT()
	DDI_IO_REP_PUT(8, ldq, stq, NOSWAP)
	DDI_NOPROTECT()
	ret
	SET_SIZE(i_ddi_prot_io_rep_put64)

