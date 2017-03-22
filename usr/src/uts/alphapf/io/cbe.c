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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/systm.h>
#include <sys/cyclic.h>
#include <sys/cyclic_impl.h>
#include <sys/spl.h>
#include <sys/x_call.h>
#include <sys/kmem.h>
#include <sys/machsystm.h>
#include <sys/smp_impldefs.h>
#include <sys/atomic.h>
#include <sys/clock.h>
#include <sys/ddi_impldefs.h>
#include <sys/ddi_intr.h>
#include <sys/avintr.h>
#include <sys/note.h>
#include <sys/hwrpb.h>
#include <sys/irq.h>
#include <sys/xc_levels.h>

static int cbe_ticks = 0;
/*
 * cbe_xcall_lock is used to protect the xcall globals since the cyclic
 * reprogramming API does not use cpu_lock.
 */
static kmutex_t cbe_xcall_lock;
static cyc_func_t volatile cbe_xcall_func;
static cpu_t *volatile cbe_xcall_cpu;
static void *cbe_xcall_farg;
static cpuset_t cbe_enabled;

static ddi_softint_hdl_impl_t cbe_low_hdl =
	{0, NULL, NULL, NULL, 0, NULL, NULL, NULL};
static ddi_softint_hdl_impl_t cbe_clock_hdl =
	{0, NULL, NULL, NULL, 0, NULL, NULL, NULL};

cyclic_id_t cbe_hres_cyclic;

void cbe_hres_tick(void);

int
cbe_softclock(void)
{
	cyclic_softint(CPU, CY_LOCK_LEVEL);
	return (1);
}

int
cbe_low_level(void)
{
	cpu_t *cpu = CPU;

	cyclic_softint(cpu, CY_LOW_LEVEL);
	return (1);
}

void
cbe_init_pre(void)
{
	/* Nothing to do on sparc */
}

/*
 * We can be in cbe_fire() either due to a cyclic-induced cross call, or due
 * to the timer firing at level-14.  Because cyclic_fire() can tolerate
 * spurious calls, it would not matter if we called cyclic_fire() in both
 * cases.
 */
int
cbe_fire(void)
{
	cpu_t *cpu = CPU;
	processorid_t me = cpu->cpu_id, i;
	int cross_call = (cbe_xcall_func != NULL && cbe_xcall_cpu == cpu);

	cyclic_fire(cpu);

	if (me == 0 && !cross_call) {
		for (i = 1; i < NCPU; i++) {
			if (CPU_IN_SET(cbe_enabled, i)) {
				send_dirint(i, CBE_HIGH_PIL);
			}
		}
	}

	if (cross_call) {
		ASSERT(cbe_xcall_func != NULL && cbe_xcall_cpu == cpu);
		(*cbe_xcall_func)(cbe_xcall_farg);
		cbe_xcall_func = NULL;
		cbe_xcall_cpu = NULL;
	}

	return (1);
}

/*ARGSUSED*/
void
cbe_softint(void *arg, cyc_level_t level)
{
	switch (level) {
	case CY_LOW_LEVEL:
		(*setsoftint)(CBE_LOW_PIL, cbe_low_hdl.ih_pending);
		break;
	case CY_LOCK_LEVEL:
		(*setsoftint)(CBE_LOCK_PIL, cbe_clock_hdl.ih_pending);
		break;
	default:
		panic("cbe_softint: unexpected soft level %d", level);
	}
}

/*ARGSUSED*/
void
cbe_reprogram(void *arg, hrtime_t time)
{
}

/*ARGSUSED*/
cyc_cookie_t
cbe_set_level(void *arg, cyc_level_t level)
{
	int ipl;

	switch (level) {
	case CY_LOW_LEVEL:
		ipl = CBE_LOW_PIL;
		break;
	case CY_LOCK_LEVEL:
		ipl = CBE_LOCK_PIL;
		break;
	case CY_HIGH_LEVEL:
		ipl = CBE_HIGH_PIL;
		break;
	default:
		panic("cbe_set_level: unexpected level %d", level);
	}

	return (splr(ipltospl(ipl)));
}

/*ARGSUSED*/
void
cbe_restore_level(void *arg, cyc_cookie_t cookie)
{
	splx(cookie);
}

/*ARGSUSED*/
void
cbe_xcall(void *arg, cpu_t *dest, cyc_func_t func, void *farg)
{
	kpreempt_disable();

	if (dest == CPU) {
		(*func)(farg);
		kpreempt_enable();
		return;
	}

	mutex_enter(&cbe_xcall_lock);

	ASSERT(cbe_xcall_func == NULL);

	cbe_xcall_farg = farg;
	membar_producer();
	cbe_xcall_cpu = dest;
	cbe_xcall_func = func;

	send_dirint(dest->cpu_id, CBE_HIGH_PIL);

	while (cbe_xcall_func != NULL || cbe_xcall_cpu != NULL)
		continue;

	mutex_exit(&cbe_xcall_lock);

	kpreempt_enable();
}

void *
cbe_configure(cpu_t *cpu)
{
	return (cpu);
}

void
cbe_unconfigure(void *arg)
{
	_NOTE(ARGUNUSED(arg));
	ASSERT(!CPU_IN_SET(cbe_enabled, ((cpu_t *)arg)->cpu_id));
}

/*ARGSUSED*/
static void
cbe_suspend(cyb_arg_t arg)
{
}

/*ARGSUSED*/
static void
cbe_resume(cyb_arg_t arg)
{
}

void
cbe_enable(void *arg)
{
	processorid_t me = ((cpu_t *)arg)->cpu_id;

	/* neither enable nor disable cpu0 if TIMER_PERIODIC is set */
	if (me == 0)
		return;

	/*
	 * Added (me == 0) to the ASSERT because the timer isn't
	 * disabled on CPU 0, and cbe_enable is called when we resume.
	 */
	ASSERT((me == 0) || !CPU_IN_SET(cbe_enabled, me));
	CPUSET_ADD(cbe_enabled, me);
}

void
cbe_disable(void *arg)
{
	processorid_t me = ((cpu_t *)arg)->cpu_id;

	/* neither enable nor disable cpu0 if TIMER_PERIODIC is set */
	if (me == 0)
		return;

	ASSERT(CPU_IN_SET(cbe_enabled, me));
	CPUSET_DEL(cbe_enabled, me);
}

/*
 * Unbound cyclic, called once per tick (every nsec_per_tick ns).
 */
void
cbe_hres_tick(void)
{
	int s;

	dtrace_hres_tick();

	/*
	 * Because hres_tick effectively locks hres_lock, we must be at the
	 * same PIL as that used for CLOCK_LOCK.
	 */
	s = splr(ipltospl(XC_HI_PIL));
	hres_tick();
	splx(s);

	if ((cbe_ticks % hz) == 0)
		(*hrtime_tick)();

	cbe_ticks++;
}

void
cbe_init(void)
{
	cyc_backend_t cbe = {
		cbe_configure,		/* cyb_configure */
		cbe_unconfigure,	/* cyb_unconfigure */
		cbe_enable,		/* cyb_enable */
		cbe_disable,		/* cyb_disable */
		cbe_reprogram,		/* cyb_reprogram */
		cbe_softint,		/* cyb_softint */
		cbe_set_level,		/* cyb_set_level */
		cbe_restore_level,	/* cyb_restore_level */
		cbe_xcall,		/* cyb_xcall */
		cbe_suspend,		/* cyb_suspend */
		cbe_resume		/* cyb_resume */
	};
	cyc_handler_t hdlr;
	cyc_time_t when;
	int cbe_vector;
	hrtime_t cbe_timer_resolution;

	cbe_vector = IRQ_ISA_PIT;
	cbe_timer_resolution = (*clkinitf)();

	mutex_init(&cbe_xcall_lock, NULL, MUTEX_DEFAULT, NULL);

	mutex_enter(&cpu_lock);
	cyclic_init(&cbe, cbe_timer_resolution);
	mutex_exit(&cpu_lock);

	add_avintr(NULL, CBE_HIGH_PIL, (avfunc)cbe_fire,
	    "cbe_fire_master", cbe_vector, 0, NULL, NULL, NULL);

	add_avintr(NULL, CBE_HIGH_PIL, (avfunc)cbe_fire,
	    "cbe_fire_slave", XC_IPI_IRQ(CBE_HIGH_PIL),
	    0, NULL, NULL, NULL);

	add_avsoftintr((void *)&cbe_clock_hdl, CBE_LOCK_PIL,
	    (avfunc)cbe_softclock, "softclock", NULL, NULL);

	add_avsoftintr((void *)&cbe_low_hdl, CBE_LOW_PIL,
	    (avfunc)cbe_low_level, "low level", NULL, NULL);

	mutex_enter(&cpu_lock);

	hdlr.cyh_level = CY_HIGH_LEVEL;
	hdlr.cyh_func = (cyc_func_t)cbe_hres_tick;
	hdlr.cyh_arg = NULL;

	when.cyt_when = 0;
	when.cyt_interval = nsec_per_tick;

	cbe_hres_cyclic = cyclic_add(&hdlr, &when);

	mutex_exit(&cpu_lock);
}
