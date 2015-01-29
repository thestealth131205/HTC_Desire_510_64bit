/* Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <asm/mach/arch.h>
#include <soc/qcom/socinfo.h>
#include <mach/board.h>
#include <mach/msm_memtypes.h>
#include <mach/msm_iomap.h>
#include <soc/qcom/rpm-smd.h>
#include <soc/qcom/smd.h>
#include <soc/qcom/smem.h>
#include <soc/qcom/spm.h>
#include <soc/qcom/pm.h>
#include "board-dt.h"
#include "platsmp.h"
#include <linux/gpio.h>
#include <mach/cable_detect.h>
#include <linux/usb/android.h>
#include <mach/devices_cmdline.h>
#include <mach/devices_dtb.h>
#ifdef CONFIG_HTC_POWER_DEBUG
#include <soc/qcom/htc_util.h>
#include <mach/devices_dtb.h>
#endif

#ifdef CONFIG_BT
#include <mach/htc_bdaddress.h>
#endif
#include <linux/pstore_ram.h>
#include <linux/memblock.h>

#ifdef CONFIG_HTC_BATT_8960
#include "mach/htc_battery_8960.h"
#include "mach/htc_battery_cell.h"
#include <linux/qpnp/qpnp-linear-charger.h>
#include <linux/qpnp/qpnp-vm-bms.h>
#endif 

#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
#include <mach/htc_mnemosyne.h>
#endif

extern int __init htc_8916_dsi_panel_power_register(void);

#define XF 0x5
#define HTC_8916_USB1_HS_ID_GPIO 49 + 902
#define HTC_8916_USB1_HS_ID_GPIO_XF 110 + 902

#ifdef CONFIG_LCD_KCAL
#include <mach/kcal.h>
#include <linux/module.h>
#include "../../../drivers/video/msm/mdss/mdss_fb.h"
extern int update_preset_lcdc_lut(void);

extern int g_kcal_r;
extern int g_kcal_g;
extern int g_kcal_b;

extern int g_kcal_min;

int kcal_set_values(int kcal_r, int kcal_g, int kcal_b)
{

	if (kcal_r > 255 || kcal_r < 0)
		kcal_r = kcal_r < 0 ? 0 : kcal_r;
		kcal_r = kcal_r > 255 ? 255 : kcal_r;
	if (kcal_g > 255 || kcal_g < 0)
		kcal_g = kcal_g < 0 ? 0 : kcal_g;
		kcal_g = kcal_g > 255 ? 255 : kcal_g;
	if (kcal_b > 255 || kcal_b < 0)
		kcal_b = kcal_b < 0 ? 0 : kcal_b;
		kcal_b = kcal_b > 255 ? 255 : kcal_b;

	g_kcal_r = kcal_r < g_kcal_min ? g_kcal_min : kcal_r;
	g_kcal_g = kcal_g < g_kcal_min ? g_kcal_min : kcal_g;
	g_kcal_b = kcal_b < g_kcal_min ? g_kcal_min : kcal_b;

	if (kcal_r < g_kcal_min || kcal_g < g_kcal_min || kcal_b < g_kcal_min)
		update_preset_lcdc_lut();

	return 0;
}

static int kcal_get_values(int *kcal_r, int *kcal_g, int *kcal_b)
{
	*kcal_r = g_kcal_r;
	*kcal_g = g_kcal_g;
	*kcal_b = g_kcal_b;
	return 0;
}

int kcal_set_min(int kcal_min)
{
	g_kcal_min = kcal_min;

	if (g_kcal_min > 255)
		g_kcal_min = 255;

	if (g_kcal_min < 0)
		g_kcal_min = 0;

	if (g_kcal_min > g_kcal_r || g_kcal_min > g_kcal_g || g_kcal_min > g_kcal_b) {
		g_kcal_r = g_kcal_r < g_kcal_min ? g_kcal_min : g_kcal_r;
		g_kcal_g = g_kcal_g < g_kcal_min ? g_kcal_min : g_kcal_g;
		g_kcal_b = g_kcal_b < g_kcal_min ? g_kcal_min : g_kcal_b;
		update_preset_lcdc_lut();
	}

	return 0;
}

static int kcal_get_min(int *kcal_min)
{
	*kcal_min = g_kcal_min;
	return 0;
}

static int kcal_refresh_values(void)
{
	return update_preset_lcdc_lut();
}

static struct kcal_platform_data kcal_pdata = {
	.set_values = kcal_set_values,
	.get_values = kcal_get_values,
	.refresh_display = kcal_refresh_values,
	.set_min = kcal_set_min,
	.get_min = kcal_get_min
};

static struct platform_device kcal_platrom_device = {
	.name = "kcal_ctrl",
	.dev = {
		.platform_data = &kcal_pdata,
	}
};

void __init add_lcd_kcal_devices(void)
{
	pr_info (" LCD_KCAL_DEBUG : %s \n", __func__);
	platform_device_register(&kcal_platrom_device);
};
#endif

static int htc_get_usbid(void)
{
	int usbid_gpio;
	if (of_board_is_a11ul() && of_machine_pcbid() < XF)
		usbid_gpio = HTC_8916_USB1_HS_ID_GPIO;
	else
		usbid_gpio = HTC_8916_USB1_HS_ID_GPIO_XF;
	pr_debug("%s: pcbid=%d, usbid_gpio=%d\n", __func__, of_machine_pcbid(), usbid_gpio);
	return usbid_gpio;
}

static int64_t htc_8x26_get_usbid_adc(void)
{
	return htc_qpnp_adc_get_usbid_adc();
}
static bool specific_rom_check(void)
{
	int rom_check_pass = 0;
#if defined(CONFIG_MACH_DUMMY) || defined(CONFIG_MACH_DUMMY) || defined(CONFIG_MACH_DUMMY)\
				   || defined(CONFIG_MACH_DUMMY)
	rom_check_pass = 1;
#endif
	return ((board_build_flag() == BUILD_MODE_MFG) && rom_check_pass)?1:0;
}

static void htc_8x26_config_usb_id_gpios(bool output)
{
	if (output) {
		if (gpio_direction_output(htc_get_usbid(),1)) {
			printk(KERN_ERR "[CABLE] fail to config usb id, output = %d\n",output);
			return;
		}
		pr_info("[CABLE] %s: %d output high\n",  __func__, htc_get_usbid());
	} else {
		if (gpio_direction_input(htc_get_usbid())) {
			printk(KERN_ERR "[CABLE] fail to config usb id, output = %d\n",output);
			return;
		}
		pr_info("[CABLE] %s: %d intput nopull\n",  __func__, htc_get_usbid());
	}
}

static struct cable_detect_platform_data cable_detect_pdata = {
	.detect_type            = CABLE_TYPE_PMIC_ADC,
	.usb_id_pin_type        = CABLE_TYPE_APP_GPIO,
	.usb_id_pin_gpio        = htc_get_usbid,
	.get_adc_cb             = htc_8x26_get_usbid_adc,
	.config_usb_id_gpios    = htc_8x26_config_usb_id_gpios,
#ifdef CONFIG_FB_MSM_HDMI_MHL
	.mhl_1v2_power = mhl_sii9234_1v2_power,
	.usb_dpdn_switch        = m7_usb_dpdn_switch,
#endif
#ifdef CONFIG_HTC_BATT_8960
	.is_pwr_src_plugged_in	= pm8916_is_pwr_src_plugged_in,
#endif
	.vbus_debounce_retry = 1,
};

static struct platform_device cable_detect_device = {
	.name   = "cable_detect",
	.id     = -1,
	.dev    = {
		.platform_data = &cable_detect_pdata,
	},
};

static struct android_usb_platform_data android_usb_pdata = {
	.vendor_id      = 0x0bb4,
	.product_id     = 0x0dff, 
	.product_name		= "Android Phone",
	.manufacturer_name	= "HTC",
	.serial_number = "123456789012",
	.usb_core_id = 0,
	.usb_rmnet_interface = "smd,bam",
	.usb_diag_interface = "diag",
	.fserial_init_string = "smd:modem,tty,tty:autobot,tty:serial,tty:autobot,tty:acm",
#ifdef CONFIG_MACH_DUMMY
	.match = memwl_usb_product_id_match,
#endif
	.nluns = 1,
	.cdrom_lun = 0x1,
	.vzw_unmount_cdrom = 0,
	.specific_rom_cb = specific_rom_check,
};

#define QCT_ANDROID_USB_REGS 0x086000c8
#define QCT_ANDROID_USB_SIZE 0xc8
static struct resource resources_android_usb[] = {
	{
		.start  = QCT_ANDROID_USB_REGS,
		.end    = QCT_ANDROID_USB_REGS + QCT_ANDROID_USB_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device android_usb_device = {
	.name   = "android_usb",
	.id     = -1,
	.num_resources  = ARRAY_SIZE(resources_android_usb),
	.resource       = resources_android_usb,
	.dev    = {
		.platform_data = &android_usb_pdata,
	},
};

static void msm8916_add_usb_devices(void)
{
	int mode = board_mfg_mode();
	android_usb_pdata.serial_number = board_serialno();

	if (mode != 0) {
		android_usb_pdata.nluns = 1;
		android_usb_pdata.cdrom_lun = 0x0;
	}

	if ((!(get_radio_flag() & BIT(17))) && (mode == MFG_MODE_MFGKERNEL || mode == MFG_MODE_MFGKERNEL_DIAG58)) {
		android_usb_pdata.fserial_init_string = "tty,tty:autobot,tty:serial,tty:autobot,tty:acm";
	}
#ifdef CONFIG_MACH_A11_UL
	android_usb_pdata.product_id = 0x05fd;
#elif defined(CONFIG_MACH_DUMMY)
	android_usb_pdata.product_id = 0x0652;
#elif defined(CONFIG_MACH_DUMMY)
	android_usb_pdata.product_id = 0x0653;
#elif defined(CONFIG_MACH_DUMMY)
	android_usb_pdata.product_id = 0x0654;
#elif defined(CONFIG_MACH_DUMMY)
	android_usb_pdata.product_id = 0x0655;
#elif defined(CONFIG_MACH_DUMMY)
	android_usb_pdata.product_id = 0x05F5;
#elif defined(CONFIG_MACH_DUMMY)
	android_usb_pdata.product_id = 0x05F6;
#elif defined(CONFIG_MACH_DUMMY)
	android_usb_pdata.product_id = 0x05F7;
#elif defined(CONFIG_MACH_DUMMY)
	android_usb_pdata.product_id = 0x05F8;
#endif
	platform_device_register(&android_usb_device);
}

#ifdef CONFIG_HTC_POWER_DEBUG
static struct platform_device cpu_usage_stats_device = {
       .name = "cpu_usage_stats",
       .id = -1,
};

int __init htc_cpu_usage_register(void)
{
       platform_device_register(&cpu_usage_stats_device);
       return 0;
}
#endif

static void msm8916_cable_detect_register(void)
{
	platform_device_register(&cable_detect_device);
}

#ifdef CONFIG_PERFLOCK
extern struct platform_device msm8916_device_perf_lock;
#endif

#define RAMOOPS_MEM_PHY 0x8C800000
#define RAMOOPS_MEM_SIZE SZ_1M

static struct ramoops_platform_data ramoops_data = {
	.mem_size		= RAMOOPS_MEM_SIZE,
	.mem_address	= RAMOOPS_MEM_PHY,
	.console_size	= RAMOOPS_MEM_SIZE,
	.dump_oops		= 1,
};

static struct platform_device ramoops_dev = {
	.name = "ramoops",
	.dev = {
	.platform_data = &ramoops_data,
	},
};

static void __init htc_8916_early_memory(void)
{
	of_scan_flat_dt(dt_scan_for_memory_hole, NULL);
	memblock_reserve(ramoops_data.mem_address, ramoops_data.mem_size);
}

static void __init htc_8916_dt_reserve(void)
{
	of_scan_flat_dt(dt_scan_for_memory_reserve, NULL);
}

static void __init htc_8916_map_io(void)
{
	msm_map_msm8916_io();
}

static struct of_dev_auxdata htc_8916_auxdata_lookup[] __initdata = {
	{}
};

#if defined(CONFIG_HTC_BATT_8960)
#ifdef CONFIG_HTC_PNPMGR
extern int pnpmgr_battery_charging_enabled(int charging_enabled);
#endif 
static int critical_alarm_voltage_mv[] = {3000, 3200, 3400};

static struct htc_battery_platform_data htc_battery_pdev_data = {
	.guage_driver = 0,
	.chg_limit_active_mask = HTC_BATT_CHG_LIMIT_BIT_TALK |
								HTC_BATT_CHG_LIMIT_BIT_NAVI |
								HTC_BATT_CHG_LIMIT_BIT_THRML,
	.critical_low_voltage_mv = 3200,
	.critical_alarm_vol_ptr = critical_alarm_voltage_mv,
	.critical_alarm_vol_cols = sizeof(critical_alarm_voltage_mv) / sizeof(int),
	.overload_vol_thr_mv = 4000,
	.overload_curr_thr_ma = 0,
	.smooth_chg_full_delay_min = 3,
	.critical_low_level_drop = 3,

	.icharger.name = "pm8916",
		.icharger.get_charging_source = pm8916_get_charging_source,
	.icharger.get_charging_enabled = pm8916_get_charging_enabled,
	.icharger.set_charger_enable = pm8916_charger_enable,
	
	.icharger.set_pwrsrc_enable = pm8916_charger_enable,
	.icharger.set_pwrsrc_and_charger_enable =
						pm8916_set_pwrsrc_and_charger_enable,
	.icharger.set_limit_charge_enable = pm8916_limit_charge_enable,
	.icharger.set_chg_iusbmax = pm8916_set_chg_iusbmax,
	.icharger.set_chg_vin_min = pm8916_set_chg_vin_min,
	.icharger.is_ovp = pm8916_is_charger_ovp,
	.icharger.is_batt_temp_fault_disable_chg =
						pm8916_is_batt_temp_fault_disable_chg,
	.icharger.charger_change_notifier_register =
						cable_detect_register_notifier,
	.icharger.is_safty_timer_timeout = pm8916_is_chg_safety_timer_timeout,
	.icharger.get_attr_text = pm8916_charger_get_attr_text,
	.icharger.max_input_current = pm8916_set_hsml_target_ma,
	.icharger.is_battery_full_eoc_stop = pm8916_is_batt_full_eoc_stop,
	.icharger.get_charge_type = pm8916_get_charge_type,
	.icharger.get_chg_usb_iusbmax = pm8916_get_chg_usb_iusbmax,
	.icharger.get_chg_vinmin = pm8916_get_chg_vinmin,
	.icharger.get_input_voltage_regulation =
						pm8916_get_input_voltage_regulation,
	.icharger.dump_all = pm8916_dump_all,

	.igauge.name = "pm8916",
	.igauge.get_battery_voltage = pm8916_get_batt_voltage,
	.igauge.get_battery_current = pm8916_get_batt_current,
	.igauge.get_battery_temperature = pm8916_get_batt_temperature,
	.igauge.get_battery_id = pm8916_get_batt_id,
	.igauge.get_battery_soc = pm8916_get_batt_soc,
	.igauge.get_battery_cc = pm8916_get_batt_cc,
	.igauge.is_battery_full = pm8916_is_batt_full,
	.igauge.is_battery_temp_fault = pm8916_is_batt_temperature_fault,
	.igauge.get_attr_text = pm8916_gauge_get_attr_text,
	.igauge.get_usb_temperature = pm8916_get_usb_temperature,
	.igauge.set_lower_voltage_alarm_threshold =
						pm8916_batt_lower_alarm_threshold_set,
	.igauge.store_battery_data = pm8916_bms_store_battery_data_emmc,
	.igauge.store_battery_ui_soc = pm8916_bms_store_battery_ui_soc,
	.igauge.get_battery_ui_soc = pm8916_bms_get_battery_ui_soc,
			
#ifdef CONFIG_HTC_PNPMGR
	.notify_pnpmgr_charging_enabled = pnpmgr_battery_charging_enabled,
#endif 

};
static struct platform_device htc_battery_pdev = {
	.name = "htc_battery",
	.id = -1,
	.dev    = {
		.platform_data = &htc_battery_pdev_data,
	},
};

static void msm8x16_add_batt_devices(void)
{
	platform_device_register(&htc_battery_pdev);
}

static struct platform_device htc_battery_cell_pdev = {
	.name = "htc_battery_cell",
	.id = -1,
};

int __init htc_batt_cell_register(void)
{
	platform_device_register(&htc_battery_cell_pdev);
	return 0;
}
#endif 

void __init htc_8916_add_drivers(void)
{
	msm_smd_init();
	msm_rpm_driver_init();
	msm_spm_device_init();
	msm_pm_sleep_status_init();
	htc_8916_dsi_panel_power_register();
	msm8916_cable_detect_register();
	msm8916_add_usb_devices();
	platform_device_register(&ramoops_dev);
#ifdef CONFIG_HTC_POWER_DEBUG
        htc_cpu_usage_register();
#endif
#if defined(CONFIG_HTC_BATT_8960)
	htc_batt_cell_register();
	msm8x16_add_batt_devices();
#endif 
}

void __init htc_8916_init_early(void)
{
#ifdef CONFIG_HTC_DEBUG_FOOTPRINT
	mnemosyne_early_init((unsigned int)HTC_DEBUG_FOOTPRINT_PHYS, (unsigned int)HTC_DEBUG_FOOTPRINT_BASE);
#endif
}

static void __init htc_8916_init(void)
{
	struct of_dev_auxdata *adata = htc_8916_auxdata_lookup;

	of_platform_populate(NULL, of_default_bus_match_table, adata, NULL);
	msm_smem_init();

	if (socinfo_init() < 0)
		pr_err("%s: socinfo_init() failed\n", __func__);

       pr_info("%s: pid=%d, pcbid=0x%X, subtype=0x%X, socver=0x%X\n", __func__
               , of_machine_pid(), of_machine_pcbid(), of_machine_subtype(), of_machine_socver());

	htc_8916_add_drivers();

#ifdef CONFIG_HTC_POWER_DEBUG
        htc_monitor_init();
#endif

#ifdef CONFIG_LCD_KCAL
	add_lcd_kcal_devices();
#endif

#ifdef CONFIG_BT
	bt_export_bd_address();
#endif
#ifdef CONFIG_PERFLOCK
	platform_device_register(&msm8916_device_perf_lock);
#endif
}

static const char *htc_8916_dt_match[] __initconst = {
	"htc,msm8916",
	NULL
};

static const char *htc_8936_dt_match[] __initconst = {
	"htc,msm8936",
	NULL
};

DT_MACHINE_START(MSM8916_DT, "UNKNOWN")
	.map_io = htc_8916_map_io,
	.init_early = htc_8916_init_early,
	.init_machine = htc_8916_init,
	.dt_compat = htc_8916_dt_match,
	.reserve = htc_8916_dt_reserve,
	.init_very_early = htc_8916_early_memory,
	.smp = &msm8916_smp_ops,
MACHINE_END

DT_MACHINE_START(MSM8936_DT, "UNKNOWN")
	.map_io = htc_8916_map_io,
	.init_early = htc_8916_init_early,
	.init_machine = htc_8916_init,
	.dt_compat = htc_8936_dt_match,
	.reserve = htc_8916_dt_reserve,
	.init_very_early = htc_8916_early_memory,
	.smp = &msm8936_smp_ops,
MACHINE_END
