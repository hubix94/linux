// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 power off
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kexec.h>
#include <linux/export.h>
#include <linux/pm.h>

#include <asm/cacheflush.h>
#include <asm/kexec.h>
#include <asm/mipsregs.h>
#include <asm/processor.h>
#include <asm/reboot.h>

#include <asm/mach-ps2/dmac.h>
#include <asm/mach-ps2/scmd.h>

/* Reset vector of the boot ROM, where the console starts at power on. */
#define PS2_BOOT_ROM_VECTOR	0xbfc00000

static void __noreturn power_off(void)
{
	scmd_power_off();

	cpu_relax_forever();
}

/*
 * The SIF driver is a module, so it hands its I/O processor reset over here
 * when it initialises.  Resetting the IOP needs the whole command interface,
 * which lives in that module; this file only needs to be able to ask.
 */
static int (*ps2_iop_reset_fn)(void);

void ps2_set_iop_reset(int (*fn)(void))
{
	ps2_iop_reset_fn = fn;
}
EXPORT_SYMBOL_GPL(ps2_set_iop_reset);

/*
 * Whatever comes next, the I/O processor does not know about it.  It still
 * holds the sub-system interface DMA addresses handed to it by this kernel
 * and would keep writing into that memory while a new kernel, or the boot
 * ROM, is starting up.  Stop both SIF DMA channels first.
 */
static void ps2_stop_sif_dma(void)
{
	outl(0, DMAC_SIF0_CHCR);
	outl(0, DMAC_SIF0_QWC);
	outl(0, DMAC_SIF0_MADR);

	outl(0, DMAC_SIF1_CHCR);
	outl(0, DMAC_SIF1_QWC);
	outl(0, DMAC_SIF1_MADR);
}

/*
 * Restart by way of the boot ROM.
 *
 * There is no service to ask for this.  The PlayStation 2 Linux kit had
 * one - its kernel called sbios(SB_HALT, SB_HALT_MODE_RESTART) and the
 * runtime environment did the work - but that environment is not present
 * here, and its memory belongs to this kernel.  What is always there is the
 * reset vector at 0xbfc00000, so the machine is put back into something
 * resembling its state at power on and control is handed to the ROM, which
 * boots the console the ordinary way.
 */
static void __noreturn ps2_restart(char *command)
{
	/*
	 * Hand the IOP back in the state the ROM expects to find it: freshly
	 * reset, running its own modules rather than the ones this kernel
	 * loaded.  This has to happen while interrupts are still on, because
	 * the reset is a request over the sub-system interface and it waits
	 * for an answer.
	 */
	if (ps2_iop_reset_fn)
		ps2_iop_reset_fn();

	ps2_stop_sif_dma();

	local_irq_disable();

	/*
	 * The ROM starts out running uncached and reinitialises memory, so
	 * write back anything of ours still sitting in the caches, then make
	 * KSEG0 uncached and drop the wired TLB entries, as at a cold start.
	 */
	flush_cache_all();
	set_c0_status(ST0_BEV | ST0_ERL);
	change_c0_config(CONF_CM_CMASK, CONF_CM_UNCACHED);
	write_c0_wired(0);

	__asm__ __volatile__(
		"	jr	%0\n"
		"	nop\n"
		:
		: "r" (PS2_BOOT_ROM_VECTOR));

	cpu_relax_forever();
}

static int __init ps2_init_reboot(void)
{
	pm_power_off = power_off;
	_machine_restart = ps2_restart;

#ifdef CONFIG_KEXEC
	_machine_kexec_shutdown = ps2_stop_sif_dma;
#endif

	return 0;
}
subsys_initcall(ps2_init_reboot);
