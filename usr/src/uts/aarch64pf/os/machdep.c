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
 * Copyright 2017 Hayashi Naoyuki
 * Copyright (c) 1992, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2010, Intel Corporation.
 * All rights reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/promif.h>
#include <sys/modctl.h>
#include <sys/clock.h>
#include <sys/archsystm.h>
#include <sys/stack.h>
#include <sys/systm.h>
#include <sys/panic.h>
#include <sys/gic.h>
#include <sys/irq.h>
#include <sys/privregs.h>
#include <sys/dumphdr.h>
#include <sys/sunddi.h>
#include <sys/machparam.h>
#include <sys/dtrace.h>
#include <sys/memlist.h>
#include <sys/clock_impl.h>
#include <sys/smp_impldefs.h>
#include <sys/hold_page.h>

int maxphys = MMU_PAGESIZE * 16;	/* 128k */
int klustsize = MMU_PAGESIZE * 16;	/* 128k */

hrtime_t hres_last_tick;
volatile int hres_lock;
hrtime_t hrtime_base;

int64_t timedelta;
int64_t hrestime_adj;
// 2015/11/11 00:00 $B$K$7$F$*$/(B
volatile timestruc_t hrestime = { .tv_sec = 1447167600 };
int gethrtime_hires = 0;
char panicbuf[PANICBUFSIZE];
static cpuset_t	panic_idle_enter;
#define	PANIC_IDLE_MAXWAIT	0x100

int vac;

/*
 * set_idle_cpu is called from idle() when a CPU becomes idle.
 */
void
set_idle_cpu(int cpun)
{
}

/*
 * unset_idle_cpu is called from idle() when a CPU is no longer idle.
 */
void
unset_idle_cpu(int cpun)
{
}

/*
 * This routine is almost correct now, but not quite.  It still needs the
 * equivalent concept of "hres_last_tick", just like on the sparc side.
 * The idea is to take a snapshot of the hi-res timer while doing the
 * hrestime_adj updates under hres_lock in locore, so that the small
 * interval between interrupt assertion and interrupt processing is
 * accounted for correctly.  Once we have this, the code below should
 * be modified to subtract off hres_last_tick rather than hrtime_base.
 *
 * I'd have done this myself, but I don't have source to all of the
 * vendor-specific hi-res timer routines (grrr...).  The generic hook I
 * need is something like "gethrtime_unlocked()", which would be just like
 * gethrtime() but would assume that you're already holding CLOCK_LOCK().
 * This is what the GET_HRTIME() macro is for on sparc (although it also
 * serves the function of making time available without a function call
 * so you don't take a register window overflow while traps are disabled).
 */
void
pc_gethrestime(timespec_t *tp)
{
	int lock_prev;
	timestruc_t now;
	int nslt;		/* nsec since last tick */
	int adj;		/* amount of adjustment to apply */

loop:
	lock_prev = hres_lock;
	now = hrestime;
	nslt = (int)(gethrtime() - hres_last_tick);
	if (nslt < 0) {
		/*
		 * nslt < 0 means a tick came between sampling
		 * gethrtime() and hres_last_tick; restart the loop
		 */

		goto loop;
	}
	now.tv_nsec += nslt;
	if (hrestime_adj != 0) {
		if (hrestime_adj > 0) {
			adj = (nslt >> ADJ_SHIFT);
			if (adj > hrestime_adj)
				adj = (int)hrestime_adj;
		} else {
			adj = -(nslt >> ADJ_SHIFT);
			if (adj < hrestime_adj)
				adj = (int)hrestime_adj;
		}
		now.tv_nsec += adj;
	}
	while ((unsigned long)now.tv_nsec >= NANOSEC) {

		/*
		 * We might have a large adjustment or have been in the
		 * debugger for a long time; take care of (at most) four
		 * of those missed seconds (tv_nsec is 32 bits, so
		 * anything >4s will be wrapping around).  However,
		 * anything more than 2 seconds out of sync will trigger
		 * timedelta from clock() to go correct the time anyway,
		 * so do what we can, and let the big crowbar do the
		 * rest.  A similar correction while loop exists inside
		 * hres_tick(); in all cases we'd like tv_nsec to
		 * satisfy 0 <= tv_nsec < NANOSEC to avoid confusing
		 * user processes, but if tv_sec's a little behind for a
		 * little while, that's OK; time still monotonically
		 * increases.
		 */

		now.tv_nsec -= NANOSEC;
		now.tv_sec++;
	}
	if ((hres_lock & ~1) != lock_prev)
		goto loop;

	*tp = now;
}

void
gethrestime_lasttick(timespec_t *tp)
{
	int s;

	s = hr_clock_lock();
	*tp = hrestime;
	hr_clock_unlock(s);
}

time_t
gethrestime_sec(void)
{
	timestruc_t now;

	gethrestime(&now);
	return (now.tv_sec);
}

/*
 * Initialize a kernel thread's stack
 */

caddr_t
thread_stk_init(caddr_t stk)
{
	ASSERT(((uintptr_t)stk & (STACK_ALIGN - 1)) == 0);
	return (stk - SA(MINFRAME));
}

/*ARGSUSED*/
int
blacklist(int cmd, const char *scheme, nvlist_t *fmri, const char *class)
{
	return (ENOTSUP);
}

/*
 * The underlying console output routines are protected by raising IPL in case
 * we are still calling into the early boot services.  Once we start calling
 * the kernel console emulator, it will disable interrupts completely during
 * character rendering (see sysp_putchar, for example).  Refer to the comments
 * and code in common/os/console.c for more information on these callbacks.
 */
/*ARGSUSED*/
int
console_enter(int busy)
{
	return (splzs());
}

/*ARGSUSED*/
void
console_exit(int busy, int spl)
{
	splx(spl);
}

u_longlong_t randtick()
{
	return read_cntpct();
}

void
tenmicrosec(void)
{
	extern int gethrtime_hires;

	if (gethrtime_hires) {
		hrtime_t end = gethrtime() + (10 * (NANOSEC / MICROSEC));
		while (gethrtime() < end)
			SMT_PAUSE();
	} else {
		uint64_t timer_freq = read_cntfrq();
		uint64_t end_count = read_cntpct() + timer_freq / (MICROSEC / 10);

		while (read_cntpct() < end_count)
			SMT_PAUSE();
	}
}

void
panic_enter_hw(int spl)
{
}

void
panic_quiesce_hw(panic_data_t *pdp)
{
}

void
panic_dump_hw(int spl)
{
}

int panic_trigger(int *tp)
{
	if (atomic_swap_uint((uint_t *)tp, 0xdefacedd) == 0)
		return 1;
	return 0;
}

void
vpanic(const char *fmt, va_list adx)
{
	(void) prom_printf("error: ");
	(void) prom_vprintf(fmt, adx);
	(void) prom_printf("\n");
	prom_exit_to_mon();
}

void
panic_stopcpus(cpu_t *cp, kthread_t *t, int spl)
{
	processorid_t	i;
	cpuset_t	xcset;

	(void) splzs();

	/*
	 * Send the highest level interrupt to all CPUs but me.
	 * hilevel_intr_prolog() will call panic_idle().
	 * We don't want to use xc_trycall() because it may not send interrupt.
	 */
	CPUSET_ALL_BUT(xcset, cp->cpu_id);
	gic_send_ipi(xcset, IRQ_IPI_HI);

	for (i = 0; i < NCPU; i++) {
		if (i != cp->cpu_id && cpu[i] != NULL &&
		    (cpu[i]->cpu_flags & CPU_EXISTS)) {
			int	loop;

			if (!(cpu[i]->cpu_flags & CPU_ENABLE)) {
				continue;
			}

			/* Wait for CPU to enter panic idle loop. */
			loop = 0;
			while (!CPU_IN_SET((volatile cpuset_t)panic_idle_enter,
					   i)) {
				gic_send_ipi(CPUSET(i), IRQ_IPI_HI);
				drv_usecwait(10);
				loop++;
				if (loop >= PANIC_IDLE_MAXWAIT) {
					prom_printf("cpu%d: panic_idle() may "
						    "not be called.\n", i);
					break;
				}
			}
		}
	}
}


caddr_t
lwp_stk_init(klwp_t *lwp, caddr_t stk)
{
	caddr_t oldstk;
	struct pcb *pcb = &lwp->lwp_pcb;

	oldstk = stk;
	stk -= SA(sizeof (struct regs) + SA(MINFRAME));
	stk = (caddr_t)((uintptr_t)stk & ~(STACK_ALIGN - 1ul));
	bzero(stk, oldstk - stk);
	lwp->lwp_regs = (void *)(stk + SA(MINFRAME));

	return (stk);
}

void
lwp_stk_fini(klwp_t *lwp)
{}

uint_t dump_plat_mincpu_default = DUMP_PLAT_AARCH64_MINCPU;

int
dump_plat_addr()
{
	return (0);
}

void
dump_plat_pfn()
{
}

int
dump_plat_data(void *dump_cbuf)
{
	return (0);
}

/*
 * Enter debugger.  Called when the user types ctrl-alt-d or whenever
 * code wants to enter the debugger and possibly resume later.
 */
void
debug_enter(
	char	*msg)		/* message to print, possibly NULL */
{
	if (dtrace_debugger_init != NULL)
		(*dtrace_debugger_init)();

	if (msg)
		prom_printf("%s\n", msg);

	if (dtrace_debugger_fini != NULL)
		(*dtrace_debugger_fini)();
}

void
mdboot(int cmd, int fcn, char *mdep, boolean_t invoke_cb)
{
	prom_reboot("mdboot");
}

void
mdpreboot(int cmd, int fcn, char *mdep)
{
}

pgcnt_t
num_phys_pages()
{
	pgcnt_t npages = 0;
	struct memlist *mp;

	for (mp = phys_install; mp != NULL; mp = mp->ml_next)
		npages += mp->ml_size >> PAGESHIFT;

	return (npages);
}

void
get_cpu_mstate(cpu_t *cpu, hrtime_t *times)
{
	int i;
	hrtime_t now, start;
	uint16_t gen;
	uint16_t state;
	hrtime_t intracct[NCMSTATES];

	/*
	 * Load all volatile state under the protection of membar.
	 * cpu_acct[cpu_mstate] must be loaded to avoid double counting
	 * of (now - cpu_mstate_start) by a change in CPU mstate that
	 * arrives after we make our last check of cpu_mstate_gen.
	 */

	now = gethrtime_unscaled();
	gen = cpu->cpu_mstate_gen;

	membar_consumer();	/* guarantee load ordering */
	start = cpu->cpu_mstate_start;
	state = cpu->cpu_mstate;
	for (i = 0; i < NCMSTATES; i++) {
		intracct[i] = cpu->cpu_intracct[i];
		times[i] = cpu->cpu_acct[i];
	}
	membar_consumer();	/* guarantee load ordering */

	if (gen != 0 && gen == cpu->cpu_mstate_gen && now > start)
		times[state] += now - start;

	for (i = 0; i < NCMSTATES; i++) {
		if (i == CMS_SYSTEM)
			continue;
		times[i] -= intracct[i];
		if (times[i] < 0) {
			intracct[i] += times[i];
			times[i] = 0;
		}
		times[CMS_SYSTEM] += intracct[i];
		scalehrtime(&times[i]);
	}
	scalehrtime(&times[CMS_SYSTEM]);
}

static ddi_softint_hdl_impl_t lbolt_softint_hdl =
	{0, NULL, NULL, NULL, 0, NULL, NULL, NULL};
void
lbolt_softint_add(void)
{
	(void) add_avsoftintr((void *)&lbolt_softint_hdl, LOCK_LEVEL,
	    (avfunc)lbolt_ev_to_cyclic, "lbolt_ev_to_cyclic", NULL, NULL);
}

void
lbolt_softint_post(void)
{
	(*setsoftint)(CBE_LOCK_PIL, lbolt_softint_hdl.ih_pending);
}


void
hres_tick(void)
{
	hrtime_t now;
	hrtime_t diff;
	int s;

	now = gethrtime();
	s = hr_clock_lock();
	diff = now - hres_last_tick;
	hrtime_base += diff;
	hrestime.tv_nsec += diff;
	hres_last_tick = now;
	__adj_hrestime();
	hr_clock_unlock(s);
}

void
plat_tod_fault(enum tod_fault_type tod_bad)
{}

void
kadb_uses_kernel()
{}

void
abort_sequence_enter(char *msg)
{
	debug_enter(msg);
	prom_panic("");
}

void
progressbar_key_abort(ldi_ident_t li)
{}

void
plat_idle_enter(processorid_t cpun)
{
}

void
plat_idle_exit(processorid_t cpun)
{
}

void
splash_key_abort(ldi_ident_t li)
{
}

int
plat_mem_do_mmio(struct uio *uio, enum uio_rw rw)
{
	return (ENOTSUP);
}

pfn_t
impl_obmem_pfnum(pfn_t pf)
{
	return (pf);
}

