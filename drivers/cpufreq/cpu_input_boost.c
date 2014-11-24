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
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/cpu_input_boost.h>
#include <linux/earlysuspend.h>
#include <linux/hardirq.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

enum {
	NO_BOOST = 0,
	BOOSTED,
	OVERRIDE,
};

DEFINE_PER_CPU(struct boost_policy, boost_info);
static struct workqueue_struct *boost_wq;
static struct delayed_work boost_work;

static bool suspended;

static u64 last_input_time;
#define MIN_INPUT_INTERVAL (150 * USEC_PER_MSEC)

static unsigned int input_boost_freq;
module_param(input_boost_freq, uint, 0644);

static unsigned int input_boost_ms;
module_param(input_boost_ms, uint, 0644);

static void cpu_boost_timeout(unsigned int freq, unsigned int duration_ms)
{
	struct boost_policy *b;
	unsigned int cpu;

	if (!suspended) {
		for_each_possible_cpu(cpu) {
			b = &per_cpu(boost_info, cpu);
			if (b->cpu_boosted) {
				if (in_irq())
					return;
				cancel_delayed_work(&boost_work);
				b->cpu_boosted = OVERRIDE;
			}
			b->boost_freq = freq;
			b->boost_ms = duration_ms;
		}
		queue_delayed_work(boost_wq, &boost_work, 0);
	}
}

static void save_orig_minfreq(unsigned int cpu)
{
	struct boost_policy *b = &per_cpu(boost_info, cpu);
	struct cpufreq_policy *policy;

	if (cpu_online(cpu)) {
		policy = cpufreq_cpu_get(cpu);
		if (likely(policy)) {
			b->saved_min = policy->user_policy.min;
			cpufreq_cpu_put(policy);
		}
	}
}

static void set_new_minfreq(unsigned int minfreq, unsigned int cpu)
{
	struct boost_policy *b = &per_cpu(boost_info, cpu);
	struct cpufreq_policy *policy;

	if (cpu_online(cpu)) {
		policy = cpufreq_cpu_get(cpu);
		if (likely(policy)) {
			if (minfreq > policy->user_policy.max)
				minfreq = policy->user_policy.max;
			policy->user_policy.min = minfreq;
			cpufreq_cpu_put(policy);
			cpufreq_update_policy(cpu);
		}
	}
	b->boost_freq = minfreq;
}

static void restore_orig_minfreq(unsigned int cpu)
{
	struct boost_policy *b = &per_cpu(boost_info, cpu);
	struct cpufreq_policy *policy;

	if (b->saved_min) {
		if (cpu_online(cpu)) {
			policy = cpufreq_cpu_get(cpu);
			if (likely(policy)) {
				policy->user_policy.min = b->saved_min;
				cpufreq_cpu_put(policy);
				cpufreq_update_policy(cpu);
			}
		}
	}
	b->boost_freq = NO_BOOST;
	b->boost_ms = NO_BOOST;
	b->cpu_boosted = NO_BOOST;
}

static void __cpuinit cpu_boost_main(struct work_struct *work)
{
	struct boost_policy *b;
	unsigned int cpu;

	get_online_cpus();
	for_each_possible_cpu(cpu) {
		b = &per_cpu(boost_info, cpu);

		switch (b->cpu_boosted) {
		case NO_BOOST:
			save_orig_minfreq(cpu);
			break;
		case BOOSTED:
			restore_orig_minfreq(cpu);
			/* return when the final cpu is unboosted */
			if (cpu == (CONFIG_NR_CPUS - 1)) {
				put_online_cpus();
				return;
			}
			break;
		}

		if (b->boost_freq) {
			b->cpu_boosted = BOOSTED;
			set_new_minfreq(b->boost_freq, cpu);
		}

		if (cpu == (CONFIG_NR_CPUS - 1))
			queue_delayed_work(boost_wq, &boost_work,
				msecs_to_jiffies(b->boost_ms));
	}
	put_online_cpus();
}

static void cpu_boost_early_suspend(struct early_suspend *handler)
{
	struct boost_policy *b;
	unsigned int cpu;

	suspended = true;

	for_each_possible_cpu(cpu) {
		b = &per_cpu(boost_info, cpu);
		if (!in_irq())
			cancel_delayed_work(&boost_work);
		if (b->cpu_boosted)
			restore_orig_minfreq(cpu);
	}
}

static void __cpuinit cpu_boost_late_resume(struct early_suspend *handler)
{
	suspended = false;
}

static struct early_suspend __refdata cpu_boost_early_suspend_handler = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.suspend = cpu_boost_early_suspend,
	.resume = cpu_boost_late_resume,
};

static void cpu_boost_input_event(struct input_handle *handle, unsigned int type,
		unsigned int code, int value)
{
	u64 now;

	if (!input_boost_freq || !input_boost_ms)
		return;

	now = ktime_to_us(ktime_get());
	if (now - last_input_time < MIN_INPUT_INTERVAL)
		return;

	cpu_boost_timeout(input_boost_freq, input_boost_ms);
	last_input_time = ktime_to_us(ktime_get());
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
	/* keypad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

static struct input_handler cpu_boost_input_handler = {
	.event		= cpu_boost_input_event,
	.connect	= cpu_boost_input_connect,
	.disconnect	= cpu_boost_input_disconnect,
	.name		= "cpu_input_boost",
	.id_table	= cpu_boost_ids,
};

static int __init cpu_boost_init(void)
{
	int ret;

	boost_wq = alloc_workqueue("cpu_input_boost_wq", WQ_HIGHPRI, 0);
	if (!boost_wq) {
		pr_err("Failed to allocate workqueue\n");
		ret = -EFAULT;
		goto fail;
	}

	INIT_DELAYED_WORK(&boost_work, cpu_boost_main);

	ret = input_register_handler(&cpu_boost_input_handler);
	if (ret) {
		pr_err("Failed to register input handler, err: %d\n", ret);
		goto fail;
	}

	register_early_suspend(&cpu_boost_early_suspend_handler);
fail:
	return ret;
}
late_initcall(cpu_boost_init);

MODULE_AUTHOR("Sultanxda <sultanxda@gmail.com>");
MODULE_DESCRIPTION("CPU Input Boost");
MODULE_LICENSE("GPLv2");
