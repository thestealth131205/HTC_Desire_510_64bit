/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
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
#include "msm_sensor.h"

#define ov8858_SENSOR_NAME "ov8858"
#define PLATFORM_DRIVER_NAME "msm_camera_ov8858"
#define ov8858_obj ov8858_##obj

DEFINE_MSM_MUTEX(ov8858_mut);

static struct msm_sensor_ctrl_t ov8858_s_ctrl;

static struct msm_sensor_power_setting ov8858_power_setting[] = {
	{
		.seq_type = SENSOR_CLK,
		.seq_val = SENSOR_CAM_MCLK,
		.config_val = 23880000,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VIO,
		.config_val = 1,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VANA,
		.config_val = 1,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_STANDBY,
		.config_val = GPIO_OUT_HIGH,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VDIG,
		.config_val = 1,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_RESET,
		.config_val = GPIO_OUT_HIGH,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_I2C_MUX,
		.seq_val = 0,
		.config_val = 1,
		.delay = 0,
	},
};

static struct msm_sensor_power_setting ov8858_power_down_setting[] = {
	{
		.seq_type = SENSOR_I2C_MUX,
		.seq_val = 0,
		.config_val = 0,
		.delay = 0,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_RESET,
		.config_val = GPIO_OUT_LOW,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VDIG,
		.config_val = 0,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_STANDBY,
		.config_val = GPIO_OUT_LOW,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VANA,
		.config_val = 0,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VIO,
		.config_val = 0,
		.delay = 1,
	},
	{
		.seq_type = SENSOR_CLK,
		.seq_val = SENSOR_CAM_MCLK,
		.config_val = 23880000,
		.delay = 10,
	},
};

static struct v4l2_subdev_info ov8858_subdev_info[] = {
	{
		.code   = V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.fmt    = 1,
		.order    = 0,
	},
};

static const struct i2c_device_id ov8858_i2c_id[] = {
	{ov8858_SENSOR_NAME, (kernel_ulong_t)&ov8858_s_ctrl},
	{ }
};

static int32_t msm_ov8858_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	return msm_sensor_i2c_probe(client, id, &ov8858_s_ctrl);
}

static struct i2c_driver ov8858_i2c_driver = {
	.id_table = ov8858_i2c_id,
	.probe  = msm_ov8858_i2c_probe,
	.driver = {
		.name = ov8858_SENSOR_NAME,
	},
};

static struct msm_camera_i2c_client ov8858_sensor_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_WORD_ADDR,
};

static const struct of_device_id ov8858_dt_match[] = {
	{.compatible = "htc,ov8858", .data = &ov8858_s_ctrl},
	{}
};

MODULE_DEVICE_TABLE(of, ov8858_dt_match);

static struct platform_driver ov8858_platform_driver = {
	.driver = {
		.name = "htc,ov8858",
		.owner = THIS_MODULE,
		.of_match_table = ov8858_dt_match,
	},
};

static const char *ov8858Vendor = "OmniVision";
static const char *ov8858NAME = "ov8858";
static const char *ov8858Size = "8.0M";

static ssize_t sensor_vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	sprintf(buf, "%s %s %s\n", ov8858Vendor, ov8858NAME, ov8858Size);
	ret = strlen(buf) + 1;

	return ret;
}

static DEVICE_ATTR(sensor, 0444, sensor_vendor_show, NULL);

static struct kobject *android_ov8858;

static int ov8858_sysfs_init(void)
{
	int ret ;
	pr_info("ov8858:kobject creat and add\n");
	android_ov8858 = kobject_create_and_add("android_camera2", NULL);
	if (android_ov8858 == NULL) {
		pr_info("ov8858_sysfs_init: subsystem_register " \
		"failed\n");
		ret = -ENOMEM;
		return ret ;
	}
	pr_info("ov8858:sysfs_create_file\n");
	ret = sysfs_create_file(android_ov8858, &dev_attr_sensor.attr);
	if (ret) {
		pr_info("ov8858_sysfs_init: sysfs_create_file " \
		"failed\n");
		kobject_del(android_ov8858);
	}

	return 0 ;
}

static int32_t ov8858_platform_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	const struct of_device_id *match;
	match = of_match_device(ov8858_dt_match, &pdev->dev);
	rc = msm_sensor_platform_probe(pdev, match->data);
	return rc;
}

static int __init ov8858_init_module(void)
{
	int32_t rc = 0;
	pr_info("%s:%d\n", __func__, __LINE__);
	rc = platform_driver_probe(&ov8858_platform_driver,
		ov8858_platform_probe);
	if (!rc) {
		ov8858_sysfs_init();
		return rc;
	}
	pr_err("%s:%d rc %d\n", __func__, __LINE__, rc);
	return i2c_add_driver(&ov8858_i2c_driver);
}

static void __exit ov8858_exit_module(void)
{
	pr_info("%s:%d\n", __func__, __LINE__);
	if (ov8858_s_ctrl.pdev) {
		msm_sensor_free_sensor_data(&ov8858_s_ctrl);
		platform_driver_unregister(&ov8858_platform_driver);
	} else
		i2c_del_driver(&ov8858_i2c_driver);
	return;
}

static struct msm_sensor_fn_t ov8858_sensor_func_tbl = {
	.sensor_config = msm_sensor_config,
	.sensor_power_up = msm_sensor_power_up,
	.sensor_power_down = msm_sensor_power_down,
	.sensor_match_id = msm_sensor_match_id,
};

static struct msm_sensor_ctrl_t ov8858_s_ctrl = {
	.sensor_i2c_client = &ov8858_sensor_i2c_client,
	.power_setting_array.power_setting = ov8858_power_setting,
	.power_setting_array.size = ARRAY_SIZE(ov8858_power_setting),
	.power_setting_array.power_down_setting = ov8858_power_down_setting,
	.power_setting_array.size_down = ARRAY_SIZE(ov8858_power_down_setting),
	.msm_sensor_mutex = &ov8858_mut,
	.sensor_v4l2_subdev_info = ov8858_subdev_info,
	.sensor_v4l2_subdev_info_size = ARRAY_SIZE(ov8858_subdev_info),
	.func_tbl = &ov8858_sensor_func_tbl,
};

module_init(ov8858_init_module);
module_exit(ov8858_exit_module);
MODULE_DESCRIPTION("ov8858");
MODULE_LICENSE("GPL v2");
