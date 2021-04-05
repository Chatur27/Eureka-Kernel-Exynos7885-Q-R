/*
 * Copyright (c) Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DECON_BOARD_H__
#define __DECON_BOARD_H__

#include <linux/device.h>

extern unsigned int lcdtype;

extern void run_list(struct device *dev, const char *name);
extern int of_gpio_get_active(const char *gpioname);
extern int of_gpio_get_value(const char *gpioname);
extern int of_gpio_set_value(const char *gpioname, int value);
extern int of_get_gpio_with_name(const char *gpioname);

extern struct platform_device *of_find_dsim_platform_device(void);
extern struct platform_device *of_find_decon_platform_device(void);
extern struct device_node *of_find_decon_board(struct device *dev);

extern int of_update_phandle_property(struct device_node *from, const char *phandle_name, const char *node_name);
extern int of_update_phandle_property_list(struct device_node *from, const char *phandle_name, const char **node_names);
#endif

