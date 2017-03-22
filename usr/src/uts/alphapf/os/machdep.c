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
 * Copyright (c) 2009-2010, Intel Corporation.
 * All rights reserved.
 */

#include <sys/pit.h>
#include <sys/strlog.h>
#include <sys/clock.h>
#include <sys/debug.h>
#include <sys/rtc.h>
#include <sys/cpupart.h>
#include <sys/cpuvar.h>
#include <sys/cpu_event.h>
#include <sys/cmt.h>
#include <sys/cpu.h>
#include <sys/disp.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <sys/sysmacros.h>
#include <sys/memlist.h>
#include <sys/param.h>
#include <sys/promif.h>
#include <sys/cpu_pm.h>
#include <sys/mach_intr.h>
#include <vm/hat_alpha.h>
#include <sys/kdi_machimpl.h>
#include <sys/sdt.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/cpc_pcbe.h>
#include <sys/cmn_err.h>
#include <sys/bootconf.h>
#include <sys/reboot.h>
#include <sys/dtrace.h>
#include <sys/stack.h>
#include <sys/privregs.h>
#include <sys/dumphdr.h>
#include <sys/panic.h>
#include <sys/pal.h>
#include <vm/vm_dep.h>
#include <sys/clock_impl.h>
#include <sys/smp_impldefs.h>
#include <sys/hwrpb.h>
#include <sys/spl.h>
#include <sys/hrt.h>


struct rpb *hwrpb = (struct rpb *)CONSOLE_BASE;


/*
 * defined here, though unused on Alpha,
 * to make kstat_fr.c happy.
 */
int vac;
/*
 * maxphys - used during physio
 * klustsize - used for klustering by swapfs and specfs
 */
int maxphys = 56 * 1024;    /* XXX See vm_subr.c - max b_count in physio */
int klustsize = 56 * 1024;

int64_t	timedelta;
hrtime_t	hres_last_tick;
volatile timestruc_t hrestime;
int64_t	hrestime_adj;
volatile int		hres_lock;
uint_t			nsec_scale;
hrtime_t	hrtime_base;
uint_t			nsec_shift;
uint_t			adj_shift;
uint_t			nsec_unscale;

/*
 * Platform callback prior to writing crash dump.
 */
/*ARGSUSED*/
void
panic_dump_hw(int spl)
{
	/* Nothing to do here */
}

/*ARGSUSED*/
void
plat_tod_fault(enum tod_fault_type tod_bad)
{}

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


void
tenmicrosec(void)
{
	extern int gethrtime_hires;

	if (gethrtime_hires) {
		hrtime_t end = gethrtime() + (10 * (NANOSEC / MICROSEC));
		while (gethrtime() < end)
			SMT_PAUSE();
	} else {
		uint64_t timer_freq = hwrpb->counter;
		uint32_t start_count = (__builtin_alpha_rpcc()&0xffffffff);

		while ((uint32_t)((__builtin_alpha_rpcc()&0xffffffff) - start_count) < timer_freq / (MICROSEC / 10))
			SMT_PAUSE();
	}
}

/*
 * Halt the machine and return to the monitor
 */
void
halt(char *s)
{
	if (s)
		prom_printf("(%s) \n", s);
	prom_exit_to_mon();
	/*NOTREACHED*/
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
pc_gethrestime(timestruc_t *tp)
{
	int lock_prev;
	timestruc_t now;
	int nslt;		/* nsec since last tick */
	int adj;		/* amount of adjustment to apply */

loop:
	lock_prev = hres_lock;
	now.tv_sec = hrestime.tv_sec;
	now.tv_nsec = hrestime.tv_nsec;
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
	tp->tv_sec = hrestime.tv_sec;
	tp->tv_nsec = hrestime.tv_nsec;
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
 * void prefetch_smap_w(void *)
 *
 * Prefetch ahead within a linear list of smap structures.
 * Not implemented for alpha.  Stub for compatibility.
 */
/*ARGSUSED*/
void prefetch_smap_w(void *smp)
{}

/*
 * void prefetch_page_r(page_t *)
 * issue prefetch instructions for a page_t.
 * Not implemented for alpha.  Stub for compatibility.
 */
/*ARGSUSED*/
void
prefetch_page_r(void *pp)
{}

/*
 * get_cpu_mstate() is passed an array of timestamps, NCMSTATES
 * long, and it fills in the array with the time spent on cpu in
 * each of the mstates, where time is returned in nsec.
 *
 * No guarantee is made that the returned values in times[] will
 * monotonically increase on sequential calls, although this will
 * be true in the long run. Any such guarantee must be handled by
 * the caller, if needed. This can happen if we fail to account
 * for elapsed time due to a generation counter conflict, yet we
 * did account for it on a prior call (see below).
 *
 * The complication is that the cpu in question may be updating
 * its microstate at the same time that we are reading it.
 * Because the microstate is only updated when the CPU's state
 * changes, the values in cpu_intracct[] can be indefinitely out
 * of date. To determine true current values, it is necessary to
 * compare the current time with cpu_mstate_start, and add the
 * difference to times[cpu_mstate].
 *
 * This can be a problem if those values are changing out from
 * under us. Because the code path in new_cpu_mstate() is
 * performance critical, we have not added a lock to it. Instead,
 * we have added a generation counter. Before beginning
 * modifications, the counter is set to 0. After modifications,
 * it is set to the old value plus one.
 *
 * get_cpu_mstate() will not consider the values of cpu_mstate
 * and cpu_mstate_start to be usable unless the value of
 * cpu_mstate_gen is both non-zero and unchanged, both before and
 * after reading the mstate information. Note that we must
 * protect against out-of-order loads around accesses to the
 * generation counter. Also, this is a best effort approach in
 * that we do not retry should the counter be found to have
 * changed.
 *
 * cpu_intracct[] is used to identify time spent in each CPU
 * mstate while handling interrupts. Such time should be reported
 * against system time, and so is subtracted out from its
 * corresponding cpu_acct[] time and added to
 * cpu_acct[CMS_SYSTEM].
 */

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


/*
 * Initialize a kernel thread's stack
 */
caddr_t
thread_stk_init(caddr_t stk)
{
	caddr_t oldstk;

	oldstk = stk;
	stk = (caddr_t)((uintptr_t)stk & ~(MAX(STACK_ALIGN, 64) - 1ul));
	bzero(stk, oldstk - stk);

	return stk;
}
static uintptr_t get_pa(uintptr_t va)
{
	return
	    (((*((unsigned long *)VPT_BASE + VPT_IDX(va)))>>32)<<MMU_PAGESHIFT) |
	    (va & MMU_PAGEOFFSET);
}
caddr_t
lwp_stk_init(klwp_t *lwp, caddr_t stk)
{
	caddr_t oldstk;

	oldstk = stk;
	stk -= SA(sizeof (struct regs));
	lwp->lwp_regs = (void *)stk;
	bzero(stk, oldstk - stk);

	lwp->lwp_pcb.pcb_self = get_pa((uint64_t)&lwp->lwp_pcb);
	lwp->lwp_pcb.pcb_hw.ptbr = hat_getptbr(lwptoproc(lwp)->p_as->a_hat);
	return (stk);
}

void
lwp_stk_fini(klwp_t *lwp)
{}

pgcnt_t
num_phys_pages()
{
	pgcnt_t npages = 0;
	struct memlist *mp;

	for (mp = phys_install; mp != NULL; mp = mp->ml_next)
		npages += mp->ml_size >> PAGESHIFT;

	return (npages);
}

uint_t dump_plat_mincpu_default = DUMP_PLAT_ALPHA_MINCPU;

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
 * If we're not the panic CPU, we wait in panic_idle for reboot.
 */
void
panic_idle(void)
{
	splx(ipltospl(CLOCK_LEVEL));
	for (;;);
}

/*
 * Stop the other CPUs by cross-calling them and forcing them to enter
 * the panic_idle() loop above.
 */
/*ARGSUSED*/
void
panic_stopcpus(cpu_t *cp, kthread_t *t, int spl)
{
	processorid_t i;
	cpuset_t xcset;

	(void) splzs();

	CPUSET_ALL_BUT(xcset, cp->cpu_id);
	xc_priority(0, 0, 0, CPUSET2BV(xcset), (xc_func_t)panic_idle);

	for (i = 0; i < NCPU; i++) {
		if (i != cp->cpu_id && cpu[i] != NULL &&
		    (cpu[i]->cpu_flags & CPU_EXISTS))
			cpu[i]->cpu_flags |= CPU_QUIESCED;
	}
}

/*
 * Platform callback following each entry to panicsys().
 */
/*ARGSUSED*/
void
panic_enter_hw(int spl)
{
	/* Nothing to do here */
}

/*
 * Platform-specific code to execute after panicstr is set: we invoke
 * the PSM entry point to indicate that a panic has occurred.
 */
/*ARGSUSED*/
void
panic_quiesce_hw(panic_data_t *pdp)
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

/*
 * set_idle_cpu is called from idle() when a CPU becomes idle.
 */
/*LINTED: static unused */
static uint_t last_idle_cpu;

/*ARGSUSED*/
void
set_idle_cpu(int cpun)
{
	last_idle_cpu = cpun;
}

/*
 * unset_idle_cpu is called from idle() when a CPU is no longer idle.
 */
/*ARGSUSED*/
void
unset_idle_cpu(int cpun)
{
}

hrtime_t tsc_read(void)
{ return __builtin_alpha_rpcc()&0xffffffff;}

#if 0
void
thread_onproc(kthread_id_t t, cpu_t *cp)
{
	t->t_state = TS_ONPROC;
	__asm__ __volatile__("mb":::"memory");
	t->t_lockp = &cp->cpu_thread_lock;
}
#endif

u_longlong_t randtick()
{
	return __builtin_alpha_rpcc()&0xffffffff;
}
char panicbuf[PANICBUFSIZE];

void
mdboot(int cmd, int fcn, char *mdep, boolean_t invoke_cb)
{
	prom_reboot("mdboot");
}

/* mdpreboot - may be called prior to mdboot while root fs still mounted */
/*ARGSUSED*/
void
mdpreboot(int cmd, int fcn, char *mdep)
{
}

/*
 * These function are currently unused on alpha.
 */
/*ARGSUSED*/
void
lwp_attach_brand_hdlrs(klwp_t *lwp)
{}

/*ARGSUSED*/
void
lwp_detach_brand_hdlrs(klwp_t *lwp)
{}

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

/*
 * void
 * hres_tick(void)
 *	Tick process for high resolution timer, called once per clock tick.
 */
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

