/*
 * drivers/input/misc/enhanced_bln.c
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
#include <linux/enhanced_bln.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/wakelock.h>

static struct bln_config {
	bool always_on;
	unsigned int blink_control;
	unsigned int blink_timeout_ms;
	unsigned int off_ms;
	unsigned int on_ms;
} bln_conf = {
	.always_on = false,
	.blink_control = 0,
	.blink_timeout_ms = 600000,
	.off_ms = 0,
	.on_ms = 0,
};

static struct bln_implementation *bln_imp = NULL;
static struct delayed_work bln_main_work;
static struct wake_lock bln_wake_lock;

static bool blink_callback;
static bool suspended;

static u64 bln_start_time;

static void set_bln_blink(unsigned int bln_state)
{
	switch (bln_state) {
	case BLN_OFF:
		if (bln_conf.blink_control) {
			bln_conf.blink_control = BLN_OFF;
			bln_conf.always_on = false;
			bln_imp->led_off(BLN_OFF);
			if (suspended)
				bln_imp->disable_led_reg();
			if (wake_lock_active(&bln_wake_lock))
				wake_unlock(&bln_wake_lock);
		}
		break;
	case BLN_ON:
		if (!bln_conf.blink_control) {
			wake_lock(&bln_wake_lock);
			bln_conf.blink_control = BLN_ON;
			bln_start_time = ktime_to_ms(ktime_get());
			bln_imp->enable_led_reg();
			cancel_delayed_work_sync(&bln_main_work);
			blink_callback = false;
			schedule_delayed_work(&bln_main_work, 0);
		}
		break;
	}
}

static void bln_main(struct work_struct *work)
{
	int blink_ms;
	u64 now;

	if (bln_conf.blink_control) {
		if (blink_callback) {
			blink_callback = false;
			blink_ms = bln_conf.off_ms;
			if (bln_conf.always_on) {
				wake_unlock(&bln_wake_lock);
				return;
			}
			bln_imp->led_off(BLN_BLINK_OFF);
		} else {
			blink_callback = true;
			blink_ms = bln_conf.on_ms;
			bln_imp->led_on();
		}

		if (bln_conf.blink_timeout_ms && !bln_conf.always_on) {
			now = ktime_to_ms(ktime_get());
			if ((now - bln_start_time) >= bln_conf.blink_timeout_ms) {
				set_bln_blink(BLN_OFF);
				return;
			}
		}

		schedule_delayed_work(&bln_main_work, msecs_to_jiffies(blink_ms));
	}
}

static void bln_early_suspend(struct early_suspend *h)
{
	suspended = true;

	/* Resume always-on mode if screen is unlocked and then locked without
	 * clearing the notification. Added 100ms delay to prevent races.
	 */
	if (bln_conf.always_on && !delayed_work_pending(&bln_main_work)) {
		wake_lock(&bln_wake_lock);
		schedule_delayed_work(&bln_main_work, msecs_to_jiffies(100));
	}
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
		return size;
	}

	if (data)
		set_bln_blink(BLN_ON);
	else
		set_bln_blink(BLN_OFF);

	return size;
}

static ssize_t blink_interval_ms_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = sscanf(buf, "%u %u", &bln_conf.on_ms, &bln_conf.off_ms);

	if (ret != 2)
		return -EINVAL;

	if (!bln_conf.off_ms && bln_conf.on_ms == 1)
		bln_conf.always_on = true;

	/* break out of always-on mode */
	if (bln_conf.always_on && (bln_conf.off_ms || bln_conf.on_ms > 1)) {
		cancel_delayed_work_sync(&bln_main_work);
		wake_lock(&bln_wake_lock);
		bln_conf.always_on = false;
		schedule_delayed_work(&bln_main_work, 0);
	}

	return size;
}

static ssize_t blink_timeout_ms_write(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	return sscanf(buf, "%u", &bln_conf.blink_timeout_ms);
}

static ssize_t blink_timeout_ms_read(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", bln_conf.blink_timeout_ms);
}

static DEVICE_ATTR(blink_control, S_IRUGO | S_IWUGO,
		NULL,
		blink_control_write);
static DEVICE_ATTR(blink_interval_ms, S_IRUGO | S_IWUGO,
		NULL,
		blink_interval_ms_write);
static DEVICE_ATTR(blink_timeout_ms, S_IRUGO | S_IWUGO,
		blink_timeout_ms_read,
		blink_timeout_ms_write);

static struct attribute *bln_attributes[] = {
	&dev_attr_blink_control.attr,
	&dev_attr_blink_interval_ms.attr,
	&dev_attr_blink_timeout_ms.attr,
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

static int __init enhanced_bln_init(void)
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

	wake_lock_init(&bln_wake_lock, WAKE_LOCK_SUSPEND, "bln_wake_lock");

	register_early_suspend(&bln_early_suspend_handler);
err:
	return ret;
}
device_initcall(enhanced_bln_init);
