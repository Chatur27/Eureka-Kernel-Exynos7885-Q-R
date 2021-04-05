/*
 * Samsung Exynos5 SoC series Sensor driver
 *
 *
 * Copyright (c) 2018 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#endif
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include <exynos-fimc-is-sensor.h>
#include "fimc-is-hw.h"
#include "fimc-is-core.h"
#include "fimc-is-device-sensor.h"
#include "fimc-is-device-sensor-peri.h"
#include "fimc-is-resourcemgr.h"
#include "fimc-is-dt.h"

#include "fimc-is-device-module-base.h"
#include "fimc-is-device-module-gc5035.h"

// Reference Version : GC5035 Setting Beta V0.40.xlsx

static struct fimc_is_sensor_cfg config_gc5035[] = {
	/* 2576x1932@30fps 4:3 */
	FIMC_IS_SENSOR_CFG_EXT(2576, 1932, 30, 19, 0, CSI_DATA_LANES_2, 897, 0, 0, 0),
	/* 2560x1440@30fps 16:9 */
	FIMC_IS_SENSOR_CFG_EXT(2560, 1440, 30, 19, 1, CSI_DATA_LANES_2, 897, 0, 0, 0),
	/* 2224x1080@30fps 18.5:9 */
	FIMC_IS_SENSOR_CFG_EXT(2224, 1080, 30, 19, 2, CSI_DATA_LANES_2, 897, 0, 0, 0),
	/* 2576x1188@30fps 19.5:9 */
	FIMC_IS_SENSOR_CFG_EXT(2576, 1188, 30, 19, 3, CSI_DATA_LANES_2, 897, 0, 0, 0),
	/* 2576x1160@30fps 20:9 */
	FIMC_IS_SENSOR_CFG_EXT(2576, 1160, 30, 19, 4, CSI_DATA_LANES_2, 897, 0, 0, 0),
	/* 1920x1920@30fps 1:1 */
	FIMC_IS_SENSOR_CFG_EXT(1920, 1920, 30, 19, 5, CSI_DATA_LANES_2, 897, 0, 0, 0),
	/*800x600@60fps */
	FIMC_IS_SENSOR_CFG_EXT(800, 600, 60, 9, 6, CSI_DATA_LANES_2, 897, 0, 0, 0),
};

static struct fimc_is_vci vci_module_gc5035[] = {
	{
		.pixelformat = V4L2_PIX_FMT_SBGGR10,
		.config = {{0, HW_FORMAT_RAW10}, {1, HW_FORMAT_UNKNOWN}, {2, HW_FORMAT_USER}, {3, 0}}
	}, {
		.pixelformat = V4L2_PIX_FMT_SBGGR12,
		.config = {{0, HW_FORMAT_RAW10}, {1, HW_FORMAT_UNKNOWN}, {2, HW_FORMAT_USER}, {3, 0}}
	}, {
		.pixelformat = V4L2_PIX_FMT_SBGGR16,
		.config = {{0, HW_FORMAT_RAW10}, {1, HW_FORMAT_UNKNOWN}, {2, HW_FORMAT_USER}, {3, 0}}
	}
};

static const struct v4l2_subdev_core_ops core_ops = {
	.init = sensor_module_init,
	.g_ctrl = sensor_module_g_ctrl,
	.s_ctrl = sensor_module_s_ctrl,
	.g_ext_ctrls = sensor_module_g_ext_ctrls,
	.s_ext_ctrls = sensor_module_s_ext_ctrls,
	.ioctl = sensor_module_ioctl,
	.log_status = sensor_module_log_status,
};

static const struct v4l2_subdev_video_ops video_ops = {
	.s_stream = sensor_module_s_stream,
	.s_parm = sensor_module_s_param
};

static const struct v4l2_subdev_pad_ops pad_ops = {
	.set_fmt = sensor_module_s_format
};

static const struct v4l2_subdev_ops subdev_ops = {
	.core = &core_ops,
	.video = &video_ops,
	.pad = &pad_ops
};

#ifdef CONFIG_OF
static int module_gc5035_power_setpin(struct device *dev,
	struct exynos_platform_fimc_is_module *pdata)
{
	struct device_node *dnode;
	int gpio_reset = 0;
	int gpio_mclk = 0;
	int gpio_none = 0;
	int gpio_1p2_a2p8_en = 0;
	int gpio_cam_core_en = 0;
	int gpio_cam_dvdd_en = 0;
	char *cam_dvdd_1p2 = NULL;
	char *cam_avdd_2p8 = NULL;
	struct fimc_is_core *core;
	bool shared_mclk = false;
	bool shared_camio_1p2_2p8 = false;

	BUG_ON(!dev);

	dnode = dev->of_node;

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	if (!core) {
		err("core is NULL");
		return -EINVAL;
	}

	gpio_reset = of_get_named_gpio(dnode, "gpio_reset", 0);
	if (!gpio_is_valid(gpio_reset)) {
		dev_err(dev, "failed to get PIN_RESET\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_reset, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_reset);
	}

	gpio_mclk = of_get_named_gpio(dnode, "gpio_mclk", 0);
	if (!gpio_is_valid(gpio_mclk)) {
		dev_err(dev, "failed to get gpio_mclk\n");
		return -EINVAL;
	} else {
		gpio_request_one(gpio_mclk, GPIOF_OUT_INIT_LOW, "CAM_MCLK_OUTPUT_LOW");
		gpio_free(gpio_mclk);
	}

	gpio_1p2_a2p8_en = of_get_named_gpio(dnode, "gpio_1p2_a2p8_en", 0);
	if (!gpio_is_valid(gpio_1p2_a2p8_en)) {
		dev_warn(dev, "Failed to get regulator_avdd name property\n");
	} else {
		gpio_request_one(gpio_1p2_a2p8_en, GPIOF_OUT_INIT_LOW, "CAM_VDDIO_EN");
		gpio_free(gpio_1p2_a2p8_en);
	}

	gpio_cam_core_en = of_get_named_gpio(dnode, "gpio_cam_core_en", 0);
	if (!gpio_is_valid(gpio_cam_core_en)) {
		dev_warn(dev, "failed to get gpio_cam_core_en\n");
	} else {
		gpio_request_one(gpio_cam_core_en, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_cam_core_en);
	}

	gpio_cam_dvdd_en = of_get_named_gpio(dnode, "gpio_cam_dvdd_en", 0);
	if (!gpio_is_valid(gpio_cam_dvdd_en)) {
		dev_warn(dev, "failed to get gpio_cam_dvdd_en\n");
	} else {
		gpio_request_one(gpio_cam_dvdd_en, GPIOF_OUT_INIT_LOW, "CAM_GPIO_OUTPUT_LOW");
		gpio_free(gpio_cam_dvdd_en);
	}

	if (of_property_read_string(dnode, "cam_dvdd_1p2", (const char **) &cam_dvdd_1p2)) {
		dev_info(dev, "Failed to get regulator_dvdd name property\n");
	}

	if (of_property_read_string(dnode, "cam_avdd_2p8", (const char **) &cam_avdd_2p8)) {
		dev_info(dev, "Failed to get regulator_avdd name property\n");
	}

	shared_mclk = of_property_read_bool(dnode, "shared_mclk");
	shared_camio_1p2_2p8 = of_property_read_bool(dnode, "shared_camio_1p2_2p8");

	SET_PIN_INIT(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_ON);
	SET_PIN_INIT(pdata, SENSOR_SCENARIO_READ_ROM, GPIO_SCENARIO_OFF);

	/* Normal on */
	if (gpio_is_valid(gpio_cam_core_en)) {
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_cam_core_en, "gpio_cam_core_en", PIN_OUTPUT, 1, 0);
	} else {
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "CAM_VDDIO_1P8", PIN_REGULATOR, 1, 200); //ldo3
	}

	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_reset, "sen_rst low", PIN_OUTPUT, 0, 0);
	if (gpio_is_valid(gpio_1p2_a2p8_en))
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_1p2_a2p8_en, "camio_1p2_2p8_en", PIN_OUTPUT, 1, 50);
	else {
		if(gpio_is_valid(gpio_cam_dvdd_en))
			SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_cam_dvdd_en, "gpio_cam_dvdd_en", PIN_OUTPUT, 1, 50);
		else
			SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, cam_dvdd_1p2, PIN_REGULATOR, 1, 50);
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, cam_avdd_2p8, PIN_REGULATOR, 1, 50);
	}

	if(shared_camio_1p2_2p8) {
		SET_PIN_SHARED(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, SRT_ACQUIRE,
				&core->shared_rsc_slock[SHARED_PIN1], &core->shared_rsc_count[SHARED_PIN1], 1);
	}

	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_reset, "sen_rst high", PIN_OUTPUT, 1, 100);

	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, gpio_none, "pin", PIN_FUNCTION, 2, 50);
	if(shared_mclk) {
		SET_PIN_SHARED(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_ON, SRT_ACQUIRE,
				&core->shared_rsc_slock[SHARED_PIN0], &core->shared_rsc_count[SHARED_PIN0], 1);
	}

	/* Normal off */
	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 0, 0);
	if(shared_mclk) {
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 1, 0);
		SET_PIN_SHARED(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, SRT_RELEASE,
				&core->shared_rsc_slock[SHARED_PIN0], &core->shared_rsc_count[SHARED_PIN0], 0);
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "pin", PIN_FUNCTION, 0, 2000);
	}

	SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_reset, "sen_rst low", PIN_OUTPUT, 0, 20);

	if (gpio_is_valid(gpio_1p2_a2p8_en))
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_1p2_a2p8_en, "camio_1p2_2p8_en", PIN_OUTPUT, 0, 50);
	else {
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, cam_avdd_2p8, PIN_REGULATOR, 0, 50);
		if(gpio_is_valid(gpio_cam_dvdd_en))
			SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_cam_dvdd_en, "gpio_cam_dvdd_en", PIN_OUTPUT, 0, 50);
		else
			SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, cam_dvdd_1p2, PIN_REGULATOR, 0, 50);
	}

	if(shared_camio_1p2_2p8) {
		SET_PIN_SHARED(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, SRT_RELEASE,
				&core->shared_rsc_slock[SHARED_PIN1], &core->shared_rsc_count[SHARED_PIN1], 0);
	}

	if (gpio_is_valid(gpio_cam_core_en)) {
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_cam_core_en, "gpio_cam_core_en", PIN_OUTPUT, 0, 0);
	} else {
		SET_PIN(pdata, SENSOR_SCENARIO_NORMAL, GPIO_SCENARIO_OFF, gpio_none, "CAM_VDDIO_1P8", PIN_REGULATOR, 0, 50);
	}

	dev_info(dev, "%s X v4\n", __func__);

	return 0;
}
#endif /* CONFIG_OF */

int sensor_module_gc5035_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct fimc_is_core *core;
	struct v4l2_subdev *subdev_module;
	struct fimc_is_module_enum *module;
	struct fimc_is_device_sensor *device;
	struct sensor_open_extended *ext;
	struct exynos_platform_fimc_is_module *pdata;
	struct device *dev;
	struct pinctrl_state *s;

	BUG_ON(!fimc_is_dev);

	core = (struct fimc_is_core *)dev_get_drvdata(fimc_is_dev);
	if (!core) {
		probe_err("core device is not yet probed");
		return -EPROBE_DEFER;
	}

	dev = &pdev->dev;

	fimc_is_module_parse_dt(dev, module_gc5035_power_setpin);

	pdata = dev_get_platdata(dev);
	device = &core->sensor[pdata->id];

	subdev_module = kzalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (!subdev_module) {
		probe_err("subdev_module is NULL");
		ret = -ENOMEM;
		goto p_err;
	}

	probe_info("%s pdata->id(%d), module_enum id(%d), position(%d) \n",
		__func__, pdata->id, atomic_read(&device->module_count), pdata->position);
	probe_info("%s start1\n", __func__);
	module = &device->module_enum[atomic_read(&device->module_count)];
	atomic_inc(&device->module_count);
	clear_bit(FIMC_IS_MODULE_GPIO_ON, &module->state);
	module->pdata = pdata;
	module->dev = dev;
	module->sensor_id = SENSOR_NAME_GC5035;
	module->subdev = subdev_module;
	module->device = pdata->id;
	module->client = NULL;
	module->active_width = 2576;
	module->active_height = 1932;
	module->margin_left = 0;
	module->margin_right = 0;
	module->margin_top = 0;
	module->margin_bottom = 0;
	module->pixel_width = module->active_width + 0;
	module->pixel_height = module->active_height + 0;
	module->max_framerate = 120;
	module->position = pdata->position;
	module->mode = CSI_MODE_DT_ONLY;
	module->lanes = CSI_DATA_LANES_2;
	module->bitwidth = 10;
	module->vcis = ARRAY_SIZE(vci_module_gc5035);
	module->vci = vci_module_gc5035;
	module->sensor_maker = "GALAXYCORE";
	module->sensor_name = "GC5035";
	module->setfile_name = "setfile_gc5035.bin";
	module->cfgs = ARRAY_SIZE(config_gc5035);
	module->cfg = config_gc5035;
	module->ops = NULL;

    /* Sensor peri */
	module->private_data = kzalloc(sizeof(struct fimc_is_device_sensor_peri), GFP_KERNEL);
	if (!module->private_data) {
		probe_err("fimc_is_device_sensor_peri is NULL");
		ret = -ENOMEM;
		goto p_err;
	}
	fimc_is_sensor_peri_probe((struct fimc_is_device_sensor_peri*)module->private_data);
	PERI_SET_MODULE(module);

	ext = &module->ext;
	ext->mipi_lane_num = module->lanes;
	ext->I2CSclk = 0;
#ifdef CONFIG_SENSOR_RETENTION_USE
	ext->use_retention_mode = SENSOR_RETENTION_DISABLE;
#endif

	ext->sensor_con.product_name = module->sensor_id /*SENSOR_NAME_GC5035*/;
	ext->sensor_con.peri_type = SE_I2C;
	ext->sensor_con.peri_setting.i2c.channel = pdata->sensor_i2c_ch;
	ext->sensor_con.peri_setting.i2c.slave_address = pdata->sensor_i2c_addr;
	ext->sensor_con.peri_setting.i2c.speed = 400000;

	ext->actuator_con.product_name = ACTUATOR_NAME_NOTHING;
	ext->flash_con.product_name = FLADRV_NAME_NOTHING;
	ext->from_con.product_name = FROMDRV_NAME_NOTHING;
	ext->preprocessor_con.product_name = PREPROCESSOR_NAME_NOTHING;
	ext->ois_con.product_name = OIS_NAME_NOTHING;

	if (pdata->af_product_name !=  ACTUATOR_NAME_NOTHING) {
		ext->actuator_con.product_name = pdata->af_product_name;
		ext->actuator_con.peri_type = SE_I2C;
		ext->actuator_con.peri_setting.i2c.channel = pdata->af_i2c_ch;
		ext->actuator_con.peri_setting.i2c.slave_address = pdata->af_i2c_addr;
		ext->actuator_con.peri_setting.i2c.speed = 400000;
	}

	if (pdata->flash_product_name != FLADRV_NAME_NOTHING) {
		ext->flash_con.product_name = pdata->flash_product_name;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = pdata->flash_first_gpio;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = pdata->flash_second_gpio;
	}

	ext->from_con.product_name = FROMDRV_NAME_NOTHING;

	if (pdata->preprocessor_product_name != PREPROCESSOR_NAME_NOTHING) {
		ext->preprocessor_con.product_name = pdata->preprocessor_product_name;
		ext->preprocessor_con.peri_info0.valid = true;
		ext->preprocessor_con.peri_info0.peri_type = SE_SPI;
		ext->preprocessor_con.peri_info0.peri_setting.spi.channel = pdata->preprocessor_spi_channel;
		ext->preprocessor_con.peri_info1.valid = true;
		ext->preprocessor_con.peri_info1.peri_type = SE_I2C;
		ext->preprocessor_con.peri_info1.peri_setting.i2c.channel = pdata->preprocessor_i2c_ch;
		ext->preprocessor_con.peri_info1.peri_setting.i2c.slave_address = pdata->preprocessor_i2c_addr;
		ext->preprocessor_con.peri_info1.peri_setting.i2c.speed = 400000;
		ext->preprocessor_con.peri_info2.valid = true;
		ext->preprocessor_con.peri_info2.peri_type = SE_DMA;
		if (pdata->preprocessor_dma_channel == DMA_CH_NOT_DEFINED)
			ext->preprocessor_con.peri_info2.peri_setting.dma.channel = FLITE_ID_D;
		else
			ext->preprocessor_con.peri_info2.peri_setting.dma.channel = pdata->preprocessor_dma_channel;
	}

	if (pdata->ois_product_name != OIS_NAME_NOTHING) {
		ext->ois_con.product_name = pdata->ois_product_name;
		ext->ois_con.peri_type = SE_I2C;
		ext->ois_con.peri_setting.i2c.channel = pdata->ois_i2c_ch;
		ext->ois_con.peri_setting.i2c.slave_address = pdata->ois_i2c_addr;
		ext->ois_con.peri_setting.i2c.speed = 400000;
	} else {
		ext->ois_con.product_name = pdata->ois_product_name;
		ext->ois_con.peri_type = SE_NULL;
	}

	v4l2_subdev_init(subdev_module, &subdev_ops);

	v4l2_set_subdevdata(subdev_module, module);
	v4l2_set_subdev_hostdata(subdev_module, device);
	snprintf(subdev_module->name, V4L2_SUBDEV_NAME_SIZE, "sensor-subdev.%d", module->sensor_id);

	s = pinctrl_lookup_state(pdata->pinctrl, "release");

	if (pinctrl_select_state(pdata->pinctrl, s) < 0) {
		probe_err("pinctrl_select_state is fail\n");
		goto p_err;
	}

p_err:
	probe_info("%s done(%d)\n", __func__, ret);
	return ret;
}

static const struct of_device_id exynos_fimc_is_sensor_module_gc5035_match[] = {
	{
		.compatible = "samsung,sensor-module-gc5035",
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_fimc_is_sensor_module_gc5035_match);

static struct platform_driver sensor_module_gc5035_driver = {
	.driver = {
		.name   = "FIMC-IS-SENSOR-MODULE-GC5035",
		.owner  = THIS_MODULE,
		.of_match_table = exynos_fimc_is_sensor_module_gc5035_match,
	}
};


static int __init fimc_is_sensor_module_gc5035_init(void)
{
	int ret;

	ret = platform_driver_probe(&sensor_module_gc5035_driver,
				sensor_module_gc5035_probe);
	if (ret)
		err("failed to probe %s driver: %d\n",
			sensor_module_gc5035_driver.driver.name, ret);

	return ret;
}
late_initcall(fimc_is_sensor_module_gc5035_init);

