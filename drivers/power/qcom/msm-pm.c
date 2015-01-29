/* Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/smp.h>
#include <linux/tick.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/cpu_pm.h>
#include <linux/remote_spinlock.h>
#include <linux/msm_remote_spinlock.h>
#include <linux/msm-bus.h>
#include <soc/qcom/avs.h>
#include <soc/qcom/spm.h>
#include <soc/qcom/pm.h>
#include <soc/qcom/scm.h>
#include <soc/qcom/scm-boot.h>
#include <asm/suspend.h>
#include <asm/cacheflush.h>
#ifdef CONFIG_VFP
#include <asm/vfp.h>
#endif
#include <soc/qcom/jtag.h>
#include "idle.h"
#include "pm-boot.h"
#include "../../../arch/arm/mach-msm/clock.h"

#define CREATE_TRACE_POINTS
#include <trace/events/trace_msm_low_power.h>

#ifdef CONFIG_HTC_POWER_DEBUG
#include <soc/qcom/htc_util.h>
#include <linux/seq_file.h>
#include <linux/qpnp/pin.h>
#include <mach/devices_dtb.h>
#include <soc/qcom/rpm_stats.h>
#ifdef CONFIG_PINCTRL_MSM_TLMM_V3
#include <mach/gpio.h>
#elif defined(CONFIG_PINCTRL_MSM_TLMM_V4)
#include <linux/pinctrl/pinctrl.h>
#endif
extern int htc_vregs_dump(char *vreg_buffer, int curr_len);
#endif

#if defined(CONFIG_HTC_DEBUG_WATCHDOG)
extern int msm_watchdog_suspend_deferred(void);
extern int msm_watchdog_resume_deferred(void);
#else
static inline int msm_watchdog_suspend_deferred(void) { return 0; }
static inline int msm_watchdog_resume_deferred(void) { return 0; }
#endif

#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
#include <mach/htc_footprint.h>
#endif
#define SCM_CMD_TERMINATE_PC	(0x2)
#define SCM_CMD_CORE_HOTPLUGGED (0x10)

#define SCLK_HZ (32768)

#define MAX_BUF_SIZE  512

enum {
	MSM_PM_DEBUG_SUSPEND = BIT(0),
	MSM_PM_DEBUG_POWER_COLLAPSE = BIT(1),
	MSM_PM_DEBUG_SUSPEND_LIMITS = BIT(2),
	MSM_PM_DEBUG_CLOCK = BIT(3),
	MSM_PM_DEBUG_RESET_VECTOR = BIT(4),
	MSM_PM_DEBUG_IDLE_CLK = BIT(5),
	MSM_PM_DEBUG_IDLE = BIT(6),
	MSM_PM_DEBUG_IDLE_LIMITS = BIT(7),
	MSM_PM_DEBUG_HOTPLUG = BIT(8),
#ifdef CONFIG_HTC_POWER_DEBUG
	MSM_PM_DEBUG_GPIO = BIT(9),
	MSM_PM_DEBUG_BLOCK_XO_CLOCK = BIT(10),
	MSM_PM_DEBUG_RPM_STAT = BIT(12),
	MSM_PM_DEBUG_VREG = BIT(13),
	MSM_PM_DEBUG_REGISTER = BIT(14),
#endif
};

#ifdef CONFIG_HTC_POWER_DEBUG
static int msm_pm_debug_mask = MSM_PM_DEBUG_SUSPEND | MSM_PM_DEBUG_RPM_STAT | MSM_PM_DEBUG_BLOCK_XO_CLOCK;
#else
static int msm_pm_debug_mask = 1;
#endif

module_param_named(
        debug_mask, msm_pm_debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP
);

enum msm_pc_count_offsets {
	MSM_PC_ENTRY_COUNTER,
	MSM_PC_EXIT_COUNTER,
	MSM_PC_FALLTHRU_COUNTER,
	MSM_PC_UNUSED,
	MSM_PC_NUM_COUNTERS,
};

static bool msm_pm_ldo_retention_enabled = true;
static bool msm_no_ramp_down_pc;
static struct msm_pm_sleep_status_data *msm_pm_slp_sts;
DEFINE_PER_CPU(struct clk *, cpu_clks);
static struct clk *l2_clk;

static int cpu_count;
static DEFINE_SPINLOCK(cpu_cnt_lock);
#define SCM_HANDOFF_LOCK_ID "S:7"
static remote_spinlock_t scm_handoff_lock;

static void __iomem *msm_pc_debug_counters;

static enum msm_pm_l2_scm_flag msm_pm_flush_l2_flag = MSM_SCM_L2_OFF;

void msm_pm_set_l2_flush_flag(enum msm_pm_l2_scm_flag flag)
{
	msm_pm_flush_l2_flag = flag;
}
EXPORT_SYMBOL(msm_pm_set_l2_flush_flag);

static enum msm_pm_l2_scm_flag msm_pm_get_l2_flush_flag(void)
{
	return msm_pm_flush_l2_flag;
}

static cpumask_t retention_cpus;
static DEFINE_SPINLOCK(retention_lock);

static inline void msm_arch_idle(void)
{
	mb();
	wfi();
}

static bool msm_pm_is_L1_writeback(void)
{
	u32 cache_id;

#if defined(CONFIG_CPU_V7)
	u32 sel = 0;
	asm volatile ("mcr p15, 2, %[ccselr], c0, c0, 0\n\t"
		      "isb\n\t"
		      "mrc p15, 1, %[ccsidr], c0, c0, 0\n\t"
		      :[ccsidr]"=r" (cache_id)
		      :[ccselr]"r" (sel)
		     );
	return cache_id & BIT(31);
#elif defined(CONFIG_ARM64)
	u32 sel = 0;
	asm volatile("msr csselr_el1, %[ccselr]\n\t"
		     "isb\n\t"
		     "mrs %[ccsidr],ccsidr_el1\n\t"
		     :[ccsidr]"=r" (cache_id)
		     :[ccselr]"r" (sel)
		    );
	return cache_id & BIT(30);
#else
#error No valid CPU arch selected
#endif
}

static enum msm_pm_time_stats_id msm_pm_swfi(bool from_idle)
{
	if (!from_idle && smp_processor_id() == 0) {
		if (suspend_console_deferred)
			suspend_console();

		msm_watchdog_suspend_deferred();
	}

	msm_arch_idle();

	if (!from_idle && smp_processor_id() == 0) {
		msm_watchdog_resume_deferred();

		if (suspend_console_deferred)
			resume_console();
	}

	return MSM_PM_STAT_IDLE_WFI;
}

static enum msm_pm_time_stats_id msm_pm_retention(bool from_idle)
{
	int ret = 0;
	unsigned int cpu = smp_processor_id();
	struct clk *cpu_clk = per_cpu(cpu_clks, cpu);

	spin_lock(&retention_lock);

	if (!msm_pm_ldo_retention_enabled)
		goto bailout;

	cpumask_set_cpu(cpu, &retention_cpus);
	spin_unlock(&retention_lock);

	clk_disable(cpu_clk);

	ret = msm_spm_set_low_power_mode(MSM_SPM_MODE_POWER_RETENTION, false);
	WARN_ON(ret);

	msm_arch_idle();

	ret = msm_spm_set_low_power_mode(MSM_SPM_MODE_CLOCK_GATING, false);
	WARN_ON(ret);

	if (clk_enable(cpu_clk))
		pr_err("%s(): Error restore cpu clk\n", __func__);

	spin_lock(&retention_lock);
	cpumask_clear_cpu(cpu, &retention_cpus);
bailout:
	spin_unlock(&retention_lock);
	return MSM_PM_STAT_RETENTION;
}

static inline void msm_pc_inc_debug_count(uint32_t cpu,
		enum msm_pc_count_offsets offset)
{
	uint32_t cnt;
	int cntr_offset = cpu * 4 * MSM_PC_NUM_COUNTERS + offset * 4;

	if (!msm_pc_debug_counters)
		return;

	cnt = readl_relaxed(msm_pc_debug_counters + cntr_offset);
	writel_relaxed(++cnt, msm_pc_debug_counters + cntr_offset);
	mb();
}

static bool msm_pm_pc_hotplug(void)
{
	uint32_t cpu = smp_processor_id();

#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	set_reset_vector(cpu);
#endif

	if (msm_pm_is_L1_writeback())
		flush_cache_louis();

	msm_pc_inc_debug_count(cpu, MSM_PC_ENTRY_COUNTER);

#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	set_cpu_foot_print(cpu, 0x1);
#endif

	scm_call_atomic1(SCM_SVC_BOOT, SCM_CMD_TERMINATE_PC,
			SCM_CMD_CORE_HOTPLUGGED);

	
	msm_pc_inc_debug_count(cpu, MSM_PC_FALLTHRU_COUNTER);
	return 0;
}

int msm_pm_collapse(unsigned long unused)
{
	uint32_t cpu = smp_processor_id();
	enum msm_pm_l2_scm_flag flag = MSM_SCM_L2_ON;
#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	set_reset_vector(cpu);
#endif
	spin_lock(&cpu_cnt_lock);
	cpu_count++;
	if (cpu_count == num_online_cpus())
		flag = msm_pm_get_l2_flush_flag();

	pr_debug("cpu:%d cores_in_pc:%d L2 flag: %d\n",
			cpu, cpu_count, flag);

	remote_spin_lock_rlock_id(&scm_handoff_lock,
				  REMOTE_SPINLOCK_TID_START + cpu);
	spin_unlock(&cpu_cnt_lock);

	if (flag == MSM_SCM_L2_OFF)
		flush_cache_all();
	else if (msm_pm_is_L1_writeback())
		flush_cache_louis();

	msm_pc_inc_debug_count(cpu, MSM_PC_ENTRY_COUNTER);

#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	set_cpu_foot_print(cpu, 0x1);
#endif

	scm_call_atomic1(SCM_SVC_BOOT, SCM_CMD_TERMINATE_PC, flag);

	msm_pc_inc_debug_count(cpu, MSM_PC_FALLTHRU_COUNTER);

	return 0;
}
EXPORT_SYMBOL(msm_pm_collapse);

#ifdef CONFIG_HTC_POWER_DEBUG
static char *gpio_sleep_status_info;

int print_gpio_buffer(struct seq_file *m)
{
	if (gpio_sleep_status_info)
		seq_printf(m, gpio_sleep_status_info);
	else
		seq_printf(m, "Device haven't suspended yet!\n");
	return 0;
}
EXPORT_SYMBOL(print_gpio_buffer);

int free_gpio_buffer(void)
{
	kfree(gpio_sleep_status_info);
	gpio_sleep_status_info = NULL;

	return 0;
}
EXPORT_SYMBOL(free_gpio_buffer);

static char *vreg_sleep_status_info;

int print_vreg_buffer(struct seq_file *m)
{
	if (vreg_sleep_status_info)
		seq_printf(m, vreg_sleep_status_info);
	else
		seq_printf(m, "Device haven't suspended yet!\n");

	return 0;
}
EXPORT_SYMBOL(print_vreg_buffer);

int free_vreg_buffer(void)
{
	kfree(vreg_sleep_status_info);
	vreg_sleep_status_info = NULL;

	return 0;
}
EXPORT_SYMBOL(free_vreg_buffer);

static char *pmic_reg_sleep_status_info;

int print_pmic_reg_buffer(struct seq_file *m)
{
	if (pmic_reg_sleep_status_info)
		seq_printf(m, pmic_reg_sleep_status_info);
	else
		seq_printf(m, "Device haven't suspended yet!\n");

	return 0;
}
EXPORT_SYMBOL(print_pmic_reg_buffer);

int free_pmic_reg_buffer(void)
{
	kfree(pmic_reg_sleep_status_info);
	pmic_reg_sleep_status_info = NULL;

	return 0;
}
EXPORT_SYMBOL(free_pmic_reg_buffer);
#endif

static bool __ref msm_pm_spm_power_collapse(
	unsigned int cpu, bool from_idle, bool notify_rpm)
{
	void *entry;
	bool collapsed = 0;
	int ret;
	bool save_cpu_regs = !cpu || from_idle;
#ifdef CONFIG_HTC_POWER_DEBUG
        int curr_len = 0;
#endif

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: notify_rpm %d\n",
			cpu, __func__, (int) notify_rpm);

	if (from_idle)
		cpu_pm_enter();

	ret = msm_spm_set_low_power_mode(
			MSM_SPM_MODE_POWER_COLLAPSE, notify_rpm);
	WARN_ON(ret);

	entry = save_cpu_regs ?  cpu_resume : msm_secondary_startup;

	msm_pm_boot_config_before_pc(cpu, virt_to_phys(entry));

	if (MSM_PM_DEBUG_RESET_VECTOR & msm_pm_debug_mask)
		pr_info("CPU%u: %s: program vector to %p\n",
			cpu, __func__, entry);

#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	init_cpu_foot_print(cpu, from_idle, notify_rpm);
#endif

	msm_jtag_save_state();

#ifdef CONFIG_HTC_POWER_DEBUG
	if (!from_idle && smp_processor_id() == 0) {
		if (MSM_PM_DEBUG_GPIO & msm_pm_debug_mask) {
			if (gpio_sleep_status_info) {
				memset(gpio_sleep_status_info, 0,
					sizeof(gpio_sleep_status_info));
			} else {
				gpio_sleep_status_info = kmalloc(25000, GFP_ATOMIC);
				if (!gpio_sleep_status_info) {
					pr_err("[PM] kmalloc memory failed in %s\n",
					__func__);

				}
			}

			curr_len = msm_dump_gpios(NULL, curr_len,
						gpio_sleep_status_info);
			curr_len = qpnp_pin_dump(NULL, curr_len,
						gpio_sleep_status_info);

		}

		if (MSM_PM_DEBUG_VREG & msm_pm_debug_mask) {
			curr_len = 0;
			if (vreg_sleep_status_info) {
				memset(vreg_sleep_status_info, 0,
					sizeof(vreg_sleep_status_info));
			} else {
				vreg_sleep_status_info = kmalloc(25000, GFP_ATOMIC);
				if (!vreg_sleep_status_info) {
					pr_err("kmalloc memory failed in %s\n",
						__func__);

				}
			}
			curr_len = htc_vregs_dump(vreg_sleep_status_info, curr_len);
		}
		pr_info("[R] suspend end\n");

		if (suspend_console_deferred)
			suspend_console();

		msm_watchdog_suspend_deferred();
	}
#endif

#ifdef CONFIG_CPU_V7
	collapsed = save_cpu_regs ?
		!cpu_suspend(0, msm_pm_collapse) : msm_pm_pc_hotplug();
#else
	collapsed = save_cpu_regs ?
		!cpu_suspend(0) : msm_pm_pc_hotplug();
#endif
#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	set_cpu_foot_print(cpu, 0xa);
	clean_reset_vector_debug_info(cpu);
#endif

	if (save_cpu_regs) {
		spin_lock(&cpu_cnt_lock);
		cpu_count--;
		BUG_ON(cpu_count > num_online_cpus());
		spin_unlock(&cpu_cnt_lock);
	}
	msm_jtag_restore_state();
#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	set_cpu_foot_print(cpu, 0xb);
#endif

	if (!from_idle && smp_processor_id() == 0) {
		msm_watchdog_resume_deferred();

		if (suspend_console_deferred)
			resume_console();

		pr_info("[R] resume start\n");
	}

	if (collapsed)
		local_fiq_enable();

	msm_pm_boot_config_after_pc(cpu);

	if (from_idle)
		cpu_pm_exit();

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: msm_pm_collapse returned, collapsed %d\n",
			cpu, __func__, collapsed);

	ret = msm_spm_set_low_power_mode(MSM_SPM_MODE_CLOCK_GATING, false);
	WARN_ON(ret);
	return collapsed;
}

static enum msm_pm_time_stats_id msm_pm_power_collapse_standalone(
		bool from_idle)
{
	unsigned int cpu = smp_processor_id();
	unsigned long saved_acpuclk_rate = 0;
	unsigned int avsdscr;
	unsigned int avscsr;
	bool collapsed;

#ifdef CONFIG_HTC_POWER_DEBUG
	if ((from_idle && (MSM_PM_DEBUG_IDLE_CLK & msm_pm_debug_mask)) ||
			(!from_idle && (smp_processor_id() == 0))) {
		clock_debug_print_enabled();

		if (MSM_PM_DEBUG_BLOCK_XO_CLOCK & msm_pm_debug_mask)
			clock_blocked_print();
	}
	if (smp_processor_id() == 0) {
		if ((!from_idle) && (MSM_PM_DEBUG_RPM_STAT & msm_pm_debug_mask)){
			msm_rpm_dump_stat();
		}
	}
#endif

	avsdscr = avs_get_avsdscr();
	avscsr = avs_get_avscsr();
	avs_set_avscsr(0); 


#ifdef CONFIG_HTC_POWER_DEBUG
	if ((!from_idle) && (MSM_PM_DEBUG_CLOCK & msm_pm_debug_mask))
#else
	if (MSM_PM_DEBUG_CLOCK & msm_pm_debug_mask)
#endif
		pr_info("CPU%u: %s: change clock rate (old rate = %lu)\n",
			cpu, __func__, saved_acpuclk_rate);

	collapsed = msm_pm_spm_power_collapse(cpu, from_idle, false);


#ifdef CONFIG_HTC_POWER_DEBUG
	if (cpu_online(cpu)) {
		if ((!from_idle) && (MSM_PM_DEBUG_RPM_STAT & msm_pm_debug_mask))
			msm_rpm_dump_stat();
	}
#endif
	avs_set_avsdscr(avsdscr);
	avs_set_avscsr(avscsr);
	return collapsed ? MSM_PM_STAT_IDLE_STANDALONE_POWER_COLLAPSE :
			MSM_PM_STAT_IDLE_FAILED_STANDALONE_POWER_COLLAPSE;
}

static int ramp_down_last_cpu(int cpu)
{
	struct clk *cpu_clk = per_cpu(cpu_clks, cpu);
	int ret = 0;

	clk_disable(cpu_clk);
	clk_disable(l2_clk);

	return ret;
}

static int ramp_up_first_cpu(int cpu, int saved_rate)
{
	struct clk *cpu_clk = per_cpu(cpu_clks, cpu);
	int rc = 0;

	if (MSM_PM_DEBUG_CLOCK & msm_pm_debug_mask)
		pr_info("CPU%u: %s: restore clock rate\n",
				cpu, __func__);

	if (l2_clk) {
		rc = clk_enable(l2_clk);
		if (rc)
			pr_err("%s(): Error restoring l2 clk\n",
					__func__);
	}

	if (cpu_clk) {
		int ret = clk_enable(cpu_clk);

		if (ret) {
			pr_err("%s(): Error restoring cpu clk\n",
					__func__);
			return ret;
		}
	}

	return rc;
}

static enum msm_pm_time_stats_id msm_pm_power_collapse(bool from_idle)
{
	unsigned int cpu = smp_processor_id();
	unsigned long saved_acpuclk_rate = 0;
	unsigned int avsdscr;
	unsigned int avscsr;
	bool collapsed;

	
	if (!cpu && !from_idle)
		keep_dig_voltage_low_in_idle(false);

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: idle %d\n",
			cpu, __func__, (int)from_idle);

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: pre power down\n", cpu, __func__);

	if ((!from_idle && cpu_online(cpu))
			|| (MSM_PM_DEBUG_IDLE_CLK & msm_pm_debug_mask)) {
		clock_debug_print_enabled();
#ifdef CONFIG_HTC_POWER_DEBUG
		if (MSM_PM_DEBUG_CLOCK & msm_pm_debug_mask)
			clock_blocked_print();
#endif
	}

#ifdef CONFIG_HTC_POWER_DEBUG
	if (smp_processor_id() == 0) {
		if ((!from_idle) && (MSM_PM_DEBUG_RPM_STAT & msm_pm_debug_mask)){
			msm_rpm_dump_stat();
		}
	}
#endif

	avsdscr = avs_get_avsdscr();
	avscsr = avs_get_avscsr();
	avs_set_avscsr(0); 

	if (cpu_online(cpu) && !msm_no_ramp_down_pc)
		saved_acpuclk_rate = ramp_down_last_cpu(cpu);

	collapsed = msm_pm_spm_power_collapse(cpu, from_idle, true);

	if (cpu_online(cpu)) {
#ifdef CONFIG_HTC_POWER_DEBUG
		if ((!from_idle) && (MSM_PM_DEBUG_RPM_STAT & msm_pm_debug_mask))
			msm_rpm_dump_stat();
#endif
	if (!msm_no_ramp_down_pc &&
		ramp_up_first_cpu(cpu, saved_acpuclk_rate)
		< 0)
		pr_err("CPU%u: %s: failed to restore clock rate(%lu)\n",
				cpu, __func__, saved_acpuclk_rate);
	}

	avs_set_avsdscr(avsdscr);
	avs_set_avscsr(avscsr);

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: post power up\n", cpu, __func__);

	if (MSM_PM_DEBUG_POWER_COLLAPSE & msm_pm_debug_mask)
		pr_info("CPU%u: %s: return\n", cpu, __func__);
	
	if (!cpu && !from_idle)
		keep_dig_voltage_low_in_idle(true);
	return collapsed ? MSM_PM_STAT_IDLE_POWER_COLLAPSE :
			MSM_PM_STAT_IDLE_FAILED_POWER_COLLAPSE;
}

void arch_idle(void)
{
	return;
}

static inline void msm_pm_ftrace_lpm_enter(unsigned int cpu,
		uint32_t latency, uint32_t sleep_us,
		uint32_t wake_up,
		enum msm_pm_sleep_mode mode)
{
	switch (mode) {
	case MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT:
		trace_msm_pm_enter_wfi(cpu, latency, sleep_us, wake_up);
		break;
	case MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE:
		trace_msm_pm_enter_spc(cpu, latency, sleep_us, wake_up);
		break;
	case MSM_PM_SLEEP_MODE_POWER_COLLAPSE:
		trace_msm_pm_enter_pc(cpu, latency, sleep_us, wake_up);
		break;
	case MSM_PM_SLEEP_MODE_RETENTION:
		trace_msm_pm_enter_ret(cpu, latency, sleep_us, wake_up);
		break;
	default:
		break;
	}
}

static inline void msm_pm_ftrace_lpm_exit(unsigned int cpu,
		enum msm_pm_sleep_mode mode,
		bool success)
{
	switch (mode) {
	case MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT:
		trace_msm_pm_exit_wfi(cpu, success);
		break;
	case MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE:
		trace_msm_pm_exit_spc(cpu, success);
		break;
	case MSM_PM_SLEEP_MODE_POWER_COLLAPSE:
		trace_msm_pm_exit_pc(cpu, success);
		break;
	case MSM_PM_SLEEP_MODE_RETENTION:
		trace_msm_pm_exit_ret(cpu, success);
		break;
	default:
		break;
	}
}

static enum msm_pm_time_stats_id (*execute[MSM_PM_SLEEP_MODE_NR])(bool idle) = {
	[MSM_PM_SLEEP_MODE_WAIT_FOR_INTERRUPT] = msm_pm_swfi,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE_STANDALONE] =
		msm_pm_power_collapse_standalone,
	[MSM_PM_SLEEP_MODE_RETENTION] = msm_pm_retention,
	[MSM_PM_SLEEP_MODE_POWER_COLLAPSE] = msm_pm_power_collapse,
};

void msm_cpu_pm_enter_sleep(enum msm_pm_sleep_mode mode, bool from_idle)
{
	int64_t time = 0;
	enum msm_pm_time_stats_id exit_stat = -1;
	unsigned int cpu = smp_processor_id();

	if ((!from_idle  && cpu_online(cpu))
			|| (MSM_PM_DEBUG_IDLE & msm_pm_debug_mask))
		pr_info("CPU%u:%s mode:%d during %s\n", cpu, __func__,
				mode, from_idle ? "idle" : "suspend");

	if (from_idle)
		time = sched_clock();

	if (execute[mode])
		exit_stat = execute[mode](from_idle);

	if (from_idle) {
		time = sched_clock() - time;
		if (exit_stat >= 0)
			msm_pm_add_stat(exit_stat, time);
	}

#ifdef CONFIG_HTC_POWER_DEBUG
	if(from_idle){
		if((get_kernel_flag() & KERNEL_FLAG_PM_MONITOR) || !(get_kernel_flag() & KERNEL_FLAG_TEST_PWR_SUPPLY)){
			htc_idle_stat_add(mode, (u32)time);
		}
	}
#endif

}

int msm_pm_wait_cpu_shutdown(unsigned int cpu)
{
	int timeout = 10;

	if (!msm_pm_slp_sts)
		return 0;
	if (!msm_pm_slp_sts[cpu].base_addr)
		return 0;
	while (1) {
		int acc_sts = __raw_readl(msm_pm_slp_sts[cpu].base_addr);

		if (acc_sts & msm_pm_slp_sts[cpu].mask)
			return 0;

		udelay(100);
		WARN(++timeout == 20, "CPU%u didn't collapse in 2ms\n", cpu);
	}

	return -EBUSY;
}

static void msm_pm_ack_retention_disable(void *data)
{
}
void msm_pm_enable_retention(bool enable)
{
	if (enable == msm_pm_ldo_retention_enabled)
		return;

	msm_pm_ldo_retention_enabled = enable;

	if (!enable) {
		preempt_disable();
		smp_call_function_many(&retention_cpus,
				msm_pm_ack_retention_disable,
				NULL, true);
		preempt_enable();
	}
}
EXPORT_SYMBOL(msm_pm_enable_retention);

bool msm_pm_retention_enabled(void)
{
	return msm_pm_ldo_retention_enabled;
}
EXPORT_SYMBOL(msm_pm_retention_enabled);

static int msm_pm_snoc_client_probe(struct platform_device *pdev)
{
	int rc = 0;
	static struct msm_bus_scale_pdata *msm_pm_bus_pdata;
	static uint32_t msm_pm_bus_client;

	msm_pm_bus_pdata = msm_bus_cl_get_pdata(pdev);

	if (msm_pm_bus_pdata) {
		msm_pm_bus_client =
			msm_bus_scale_register_client(msm_pm_bus_pdata);

		if (!msm_pm_bus_client) {
			pr_err("%s: Failed to register SNOC client", __func__);
			rc = -ENXIO;
			goto snoc_cl_probe_done;
		}

		rc = msm_bus_scale_client_update_request(msm_pm_bus_client, 1);

		if (rc)
			pr_err("%s: Error setting bus rate", __func__);
	}

snoc_cl_probe_done:
	return rc;
}

static int msm_cpu_status_probe(struct platform_device *pdev)
{
	struct msm_pm_sleep_status_data *pdata;
	char *key;
	u32 cpu;

	if (!pdev)
		return -EFAULT;

	msm_pm_slp_sts = devm_kzalloc(&pdev->dev,
			sizeof(*msm_pm_slp_sts) * num_possible_cpus(),
			GFP_KERNEL);

	if (!msm_pm_slp_sts)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		struct resource *res;
		u32 offset;
		int rc;
		u32 mask;

		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!res)
			return -ENODEV;

		key = "qcom,cpu-alias-addr";
		rc = of_property_read_u32(pdev->dev.of_node, key, &offset);

		if (rc)
			return -ENODEV;

		key = "qcom,sleep-status-mask";
		rc = of_property_read_u32(pdev->dev.of_node, key, &mask);

		if (rc)
			return -ENODEV;

		for_each_possible_cpu(cpu) {
			phys_addr_t base_c = res->start + cpu * offset;
			msm_pm_slp_sts[cpu].base_addr =
				devm_ioremap(&pdev->dev, base_c,
						resource_size(res));
			msm_pm_slp_sts[cpu].mask = mask;

			if (!msm_pm_slp_sts[cpu].base_addr)
				return -ENOMEM;
		}
	} else {
		pdata = pdev->dev.platform_data;
		if (!pdev->dev.platform_data)
			return -EINVAL;

		for_each_possible_cpu(cpu) {
			msm_pm_slp_sts[cpu].base_addr =
				pdata->base_addr + cpu * pdata->cpu_offset;
			msm_pm_slp_sts[cpu].mask = pdata->mask;
		}
	}

	return 0;
};

static struct of_device_id msm_slp_sts_match_tbl[] = {
	{.compatible = "qcom,cpu-sleep-status"},
	{},
};

static struct platform_driver msm_cpu_status_driver = {
	.probe = msm_cpu_status_probe,
	.driver = {
		.name = "cpu_slp_status",
		.owner = THIS_MODULE,
		.of_match_table = msm_slp_sts_match_tbl,
	},
};

static struct of_device_id msm_snoc_clnt_match_tbl[] = {
	{.compatible = "qcom,pm-snoc-client"},
	{},
};

static struct platform_driver msm_cpu_pm_snoc_client_driver = {
	.probe = msm_pm_snoc_client_probe,
	.driver = {
		.name = "pm_snoc_client",
		.owner = THIS_MODULE,
		.of_match_table = msm_snoc_clnt_match_tbl,
	},
};

static int msm_pm_init(void)
{
#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	unsigned int addr;
#endif

	enum msm_pm_time_stats_id enable_stats[] = {
		MSM_PM_STAT_IDLE_WFI,
		MSM_PM_STAT_RETENTION,
		MSM_PM_STAT_IDLE_STANDALONE_POWER_COLLAPSE,
		MSM_PM_STAT_IDLE_FAILED_STANDALONE_POWER_COLLAPSE,
		MSM_PM_STAT_IDLE_POWER_COLLAPSE,
		MSM_PM_STAT_IDLE_FAILED_POWER_COLLAPSE,
		MSM_PM_STAT_SUSPEND,
	};
	msm_pm_mode_sysfs_add(KBUILD_MODNAME);
	msm_pm_add_stats(enable_stats, ARRAY_SIZE(enable_stats));

#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	store_pm_boot_entry_addr();
	get_pm_boot_vector_symbol_address(&addr);
	pr_info("%s: msm_pm_boot_vector 0x%x", __func__, addr);
	store_pm_boot_vector_addr(addr);

	clean_reset_vector_debug_info(0);
	init_cpu_foot_print(0, false, true);
	set_cpu_foot_print(0, 0xb);
	set_reset_vector_address_after_pc(0);
	set_reset_vector_value_after_pc(0);
#endif

	keep_dig_voltage_low_in_idle(true);

	suspend_console_deferred = 1;

	return 0;
}

struct msm_pc_debug_counters_buffer {
	void __iomem *reg;
	u32 len;
	char buf[MAX_BUF_SIZE];
};

static inline u32 msm_pc_debug_counters_read_register(
		void __iomem *reg, int index , int offset)
{
	return readl_relaxed(reg + (index * 4 + offset) * 4);
}

static char *counter_name[] = {
		"PC Entry Counter",
		"Warmboot Entry Counter",
		"PC Bailout Counter"
};

static int msm_pc_debug_counters_copy(
		struct msm_pc_debug_counters_buffer *data)
{
	int j;
	u32 stat;
	unsigned int cpu;

	for_each_possible_cpu(cpu) {
		data->len += scnprintf(data->buf + data->len,
				sizeof(data->buf)-data->len,
				"CPU%d\n", cpu);

		for (j = 0; j < MSM_PC_NUM_COUNTERS; j++) {
			stat = msm_pc_debug_counters_read_register(
					data->reg, cpu, j);
			data->len += scnprintf(data->buf + data->len,
					sizeof(data->buf)-data->len,
					"\t%s : %d\n", counter_name[j],
					stat);
		}

	}

	return data->len;
}

static ssize_t msm_pc_debug_counters_file_read(struct file *file,
		char __user *bufu, size_t count, loff_t *ppos)
{
	struct msm_pc_debug_counters_buffer *data;

	data = file->private_data;

	if (!data)
		return -EINVAL;

	if (!bufu)
		return -EINVAL;

	if (!access_ok(VERIFY_WRITE, bufu, count))
		return -EFAULT;

	if (*ppos >= data->len && data->len == 0)
		data->len = msm_pc_debug_counters_copy(data);

	return simple_read_from_buffer(bufu, count, ppos,
			data->buf, data->len);
}

static int msm_pc_debug_counters_file_open(struct inode *inode,
		struct file *file)
{
	struct msm_pc_debug_counters_buffer *buf;
	void __iomem *msm_pc_debug_counters_reg;

	msm_pc_debug_counters_reg = inode->i_private;

	if (!msm_pc_debug_counters_reg)
		return -EINVAL;

	file->private_data = kzalloc(
		sizeof(struct msm_pc_debug_counters_buffer), GFP_KERNEL);

	if (!file->private_data) {
		pr_err("%s: ERROR kmalloc failed to allocate %zu bytes\n",
		__func__, sizeof(struct msm_pc_debug_counters_buffer));

		return -ENOMEM;
	}

	buf = file->private_data;
	buf->reg = msm_pc_debug_counters_reg;

	return 0;
}

static int msm_pc_debug_counters_file_close(struct inode *inode,
		struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static const struct file_operations msm_pc_debug_counters_fops = {
	.open = msm_pc_debug_counters_file_open,
	.read = msm_pc_debug_counters_file_read,
	.release = msm_pc_debug_counters_file_close,
	.llseek = no_llseek,
};

static int msm_pm_clk_init(struct platform_device *pdev)
{
	bool synced_clocks;
	u32 cpu;
	char clk_name[] = "cpu??_clk";
	char *key;

	key = "qcom,synced-clocks";
	synced_clocks = of_property_read_bool(pdev->dev.of_node, key);

	for_each_possible_cpu(cpu) {
		struct clk *clk;
		snprintf(clk_name, sizeof(clk_name), "cpu%d_clk", cpu);
		clk = devm_clk_get(&pdev->dev, clk_name);
		if (IS_ERR(clk)) {
			if (cpu && synced_clocks)
				return 0;
			else
				return PTR_ERR(clk);
		}
		per_cpu(cpu_clks, cpu) = clk;
	}

	if (synced_clocks)
		return 0;

	l2_clk = devm_clk_get(&pdev->dev, "l2_clk");

	return PTR_RET(l2_clk);
}

static int msm_cpu_pm_probe(struct platform_device *pdev)
{
	struct dentry *dent = NULL;
	struct resource *res = NULL;
	int i;
	int ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return  0;
	msm_pc_debug_counters_phys = res->start;
	WARN_ON(resource_size(res) < SZ_64);
	msm_pc_debug_counters = devm_ioremap(&pdev->dev, res->start,
					resource_size(res));
	if (msm_pc_debug_counters) {
		for (i = 0; i < resource_size(res)/4; i++)
			__raw_writel(0, msm_pc_debug_counters + i * 4);

		dent = debugfs_create_file("pc_debug_counter", S_IRUGO, NULL,
				msm_pc_debug_counters,
				&msm_pc_debug_counters_fops);
		if (!dent)
			pr_err("%s: ERROR debugfs_create_file failed\n",
					__func__);
	} else {
		msm_pc_debug_counters = 0;
		msm_pc_debug_counters_phys = 0;
	}

	ret = remote_spin_lock_init(&scm_handoff_lock, SCM_HANDOFF_LOCK_ID);
	if (ret) {
		pr_err("%s: Failed initializing scm_handoff_lock (%d)\n",
			__func__, ret);
		return ret;
	}

	if (pdev->dev.of_node) {
		ret = msm_pm_clk_init(pdev);
		if (ret) {
			pr_info("msm_pm_clk_init returned error\n");
			return ret;
		}
	}

	msm_pm_init();
	if (pdev->dev.of_node)
		of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);

	return ret;
}

static struct of_device_id msm_cpu_pm_table[] = {
	{.compatible = "qcom,pm"},
	{},
};

static struct platform_driver msm_cpu_pm_driver = {
	.probe = msm_cpu_pm_probe,
	.driver = {
		.name = "msm-pm",
		.owner = THIS_MODULE,
		.of_match_table = msm_cpu_pm_table,
	},
};

static int __init msm_pm_drv_init(void)
{
	int rc;

	cpumask_clear(&retention_cpus);

	rc = platform_driver_register(&msm_cpu_pm_snoc_client_driver);

	if (rc) {
		pr_err("%s(): failed to register driver %s\n", __func__,
				msm_cpu_pm_snoc_client_driver.driver.name);
		return rc;
	}

	return platform_driver_register(&msm_cpu_pm_driver);
}
late_initcall(msm_pm_drv_init);

int __init msm_pm_sleep_status_init(void)
{
	static bool registered;

	if (registered)
		return 0;
	registered = true;

	return platform_driver_register(&msm_cpu_status_driver);
}
arch_initcall(msm_pm_sleep_status_init);
