/*
 * drivers/input/misc/bln.c
 *
 * Copyright (C) 2015, Sultanxda <sultanxda@gmail.com>
 * Rewrote driver and core logic from scratch
 *
 * Based on the original BLN implementation by:
 * Copyright 2011  Michael Richter (alias neldar)
 * Copyright 2011  Adam Kent <adam@semicircular.net>
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

#define pr_fmt(fmt) "BLN: " fmt

#include <linux/device.h>
#include <linux/earlysuspend.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/bln.h>

static struct bln_config {
	unsigned int blink_control;
	unsigned int off_ms;
	unsigned int on_ms;
} bln_conf = {
	.blink_control = 0,
	.off_ms = 2000,
	.on_ms = 500,
};

static struct bln_implementation *bln_imp = NULL;
static struct delayed_work bln_main_work;

static bool blink_callback;
static bool suspended;

static void set_bln_blink(unsigned int bln_state)
{
	switch (bln_state) {
	case BLN_OFF:
		if (bln_conf.blink_control) {
			bln_conf.blink_control = BLN_OFF;
			cancel_delayed_work_sync(&bln_main_work);
			blink_callback = false;
			bln_imp->led_off(BLN_OFF);
			if (suspended)
				bln_imp->disable_led_reg();
		}
		break;
	case BLN_ON:
		if (!bln_conf.blink_control) {
			bln_conf.blink_control = BLN_ON;
			bln_imp->enable_led_reg();
			schedule_delayed_work(&bln_main_work, 0);
		}
		break;
	}
}

static void bln_main(struct work_struct *work)
{
	int blink_ms;

	if (bln_conf.blink_control) {
		if (blink_callback) {
			blink_callback = false;
			blink_ms = bln_conf.off_ms;
			bln_imp->led_off(BLN_BLINK_OFF);
		} else {
			blink_callback = true;
			blink_ms = bln_conf.on_ms;
			bln_imp->led_on();
		}
		schedule_delayed_work(&bln_main_work, msecs_to_jiffies(blink_ms));
	}
}

static void bln_early_suspend(struct early_suspend *h)
{
	suspended = true;
}

static void bln_late_resume(struct early_suspend *h)
{
	suspended = false;
}

static struct early_suspend bln_early_suspend_handler = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.suspend = bln_early_suspend,
	.resume = bln_late_resume,
};

void register_bln_implementation(struct bln_implementation *imp)
{
	bln_imp = imp;
}

/**************************** SYSFS START ****************************/
static ssize_t blink_control_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	unsigned int data;
	int ret = sscanf(buf, "%u", &data);

	if (ret != 1)
		return -EINVAL;

	if (bln_imp == NULL) {
		pr_err("No BLN implementation found, BLN blink failed\n");
		goto err;
	}

	if (data)
		set_bln_blink(BLN_ON);
	else
		set_bln_blink(BLN_OFF);
err:
	return size;
}

static ssize_t blink_control_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", bln_conf.blink_control);
}

static ssize_t blink_interval_ms_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	return sscanf(buf, "%u %u", &bln_conf.on_ms, &bln_conf.off_ms);
}

static ssize_t blink_interval_ms_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u %u\n", bln_conf.on_ms, bln_conf.off_ms);
}

static DEVICE_ATTR(blink_control, S_IRUGO | S_IWUGO,
		blink_control_read,
		blink_control_write);
static DEVICE_ATTR(blink_interval_ms, S_IRUGO | S_IWUGO,
		blink_interval_ms_read,
		blink_interval_ms_write);

static struct attribute *bln_attributes[] = {
	&dev_attr_blink_control.attr,
	&dev_attr_blink_interval_ms.attr,
	NULL
};

static struct attribute_group bln_attr_group = {
	.attrs  = bln_attributes,
};

static struct miscdevice bln_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "bln",
};
/**************************** SYSFS END ****************************/

static int __init bln_control_init(void)
{
	int ret;

	INIT_DELAYED_WORK(&bln_main_work, bln_main);

	ret = misc_register(&bln_device);
	if (ret) {
		pr_err("Failed to register misc device!\n");
		goto err;
	}

	ret = sysfs_create_group(&bln_device.this_device->kobj, &bln_attr_group);
	if (ret) {
		pr_err("Failed to create sysfs group!\n");
		goto err;
	}

	register_early_suspend(&bln_early_suspend_handler);
err:
	return ret;
}
device_initcall(bln_control_init);
