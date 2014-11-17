/*
 * Copyright (c) 2014, Sultanxda <sultanxda@gmail.com>
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

#define pr_fmt(fmt) "CPU-boost: " fmt

#include <linux/cpu.h>
#include <linux/cpu_boost.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

static struct delayed_work boost_work;
static DECLARE_COMPLETION(cpu_boost_no_timeout);

static unsigned int boost_freq = 0;
static unsigned int boost_ms = 0;
static unsigned int cpu_boosted = 0;
static unsigned int init_done = 0;
static unsigned int minfreq_orig = 0;

static unsigned int input_boost_freq;
module_param(input_boost_freq, uint, 0644);

static unsigned int input_boost_ms;
module_param(input_boost_ms, uint, 0644);

void cpu_boost_timeout(unsigned int freq, unsigned int duration_ms)
{
	if (init_done) {
		if (cpu_boosted) {
			cpu_boosted = 2;
			cancel_delayed_work(&boost_work);
		}
		boost_freq = freq;
		boost_ms = duration_ms;
		schedule_delayed_work(&boost_work, 0);
	}
}

static void save_orig_minfreq(void)
{
	struct cpufreq_policy *policy;
	unsigned int retry_cnt = 0;

retry:
	policy = cpufreq_cpu_get(0);
	if (unlikely(!policy)) {
		pr_err("%s: Error acquiring CPU0 policy, try #%d\n", __func__, retry_cnt);
		if (retry_cnt <= 3) {
			retry_cnt++;
			goto retry;
		}
		return;
	}

	minfreq_orig = policy->user_policy.min;
	cpufreq_cpu_put(policy);
}

static void set_new_minfreq(unsigned int minfreq)
{
	struct cpufreq_policy *policy;
	unsigned int retry_cnt = 0;

retry:
	policy = cpufreq_cpu_get(0);
	if (unlikely(!policy)) {
		pr_err("%s: Error acquiring CPU0 policy, try #%d\n", __func__, retry_cnt);
		if (retry_cnt <= 3) {
			retry_cnt++;
			goto retry;
		}
		return;
	}

	if (minfreq > policy->user_policy.max)
		minfreq = policy->user_policy.max;

	policy->user_policy.min = minfreq;
	cpufreq_update_policy(0);
	cpufreq_cpu_put(policy);
}

static void restore_orig_minfreq(void)
{
	set_new_minfreq(minfreq_orig);
	boost_ms = 0;
	cpu_boosted = 0;
}

static void __cpuinit cpu_boost_main(struct work_struct *work)
{
	if (!cpu_boosted)
		save_orig_minfreq();
	else if (cpu_boosted == 1) {
		restore_orig_minfreq();
		return;
	}

	if (boost_freq) {
		set_new_minfreq(boost_freq);
		cpu_boosted = 1;
	}

	schedule_delayed_work(&boost_work,
				msecs_to_jiffies(boost_ms));
}

static void cpu_boost_input_event(struct input_handle *handle, unsigned int type,
		unsigned int code, int value)
{
	if (input_boost_freq && input_boost_ms)
		cpu_boost_timeout(input_boost_freq, input_boost_ms);
}

static int cpu_boost_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpufreq";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void cpu_boost_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id cpu_boost_ids[] = {
	/* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) },
	},
	/* touchpad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	},
	{ },
};

static struct input_handler cpu_boost_input_handler = {
	.event		= cpu_boost_input_event,
	.connect	= cpu_boost_input_connect,
	.disconnect	= cpu_boost_input_disconnect,
	.name		= "cpu-boost_framework",
	.id_table	= cpu_boost_ids,
};

static int __init cpu_boost_init(void)
{
	int ret;

	INIT_DELAYED_WORK(&boost_work, cpu_boost_main);
	ret = input_register_handler(&cpu_boost_input_handler);
	if (ret)
		pr_err("Failed to register input handler, err: %d\n", ret);

	init_done = 1;

	return ret;
}
late_initcall(cpu_boost_init);

MODULE_AUTHOR("Sultanxda <sultanxda@gmail.com>");
MODULE_DESCRIPTION("CPU-boost framework");
MODULE_LICENSE("GPLv2");
