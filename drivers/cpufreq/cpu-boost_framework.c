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
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

struct boost_policy cpu_boost_policy[CONFIG_NR_CPUS];
static struct delayed_work boost_work;

static bool init_done = false;

static unsigned int input_boost_freq;
module_param(input_boost_freq, uint, 0644);

static unsigned int input_boost_ms;
module_param(input_boost_ms, uint, 0644);

void cpu_boost_timeout(unsigned int freq, unsigned int duration_ms)
{
	unsigned int cpu, cpu_boosted;

	if (init_done) {
		cpu_boosted = cpu_boost_policy[0].cpu_boosted;
		if (cpu_boosted)
			cancel_delayed_work(&boost_work);
		for_each_possible_cpu(cpu) {
			if (cpu_boosted)
				cpu_boost_policy[cpu].cpu_boosted = 2;
			cpu_boost_policy[cpu].boost_freq = freq;
			cpu_boost_policy[cpu].boost_ms = duration_ms;
		}
		schedule_delayed_work(&boost_work, 0);
	}
}

static void save_orig_minfreq(void)
{
	struct cpufreq_policy *policy;
	unsigned int cpu;

	get_online_cpus();
	for_each_possible_cpu(cpu) {
		if (cpu_online(cpu)) {
			policy = cpufreq_cpu_get(cpu);
			if (likely(policy)) {
				cpu_boost_policy[cpu].saved_min = policy->user_policy.min;
				cpufreq_cpu_put(policy);
			}
		}
	}
	put_online_cpus();
}

static void set_new_minfreq(unsigned int minfreq)
{
	struct cpufreq_policy *policy;
	unsigned int cpu;

	get_online_cpus();
	for_each_possible_cpu(cpu) {
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
		cpu_boost_policy[cpu].boost_freq = minfreq;
	}
	put_online_cpus();
}

static void restore_orig_minfreq(void)
{
	struct cpufreq_policy *policy;
	unsigned int cpu;

	get_online_cpus();
	for_each_possible_cpu(cpu) {
		if (cpu_boost_policy[cpu].saved_min) {
			if (cpu_online(cpu)) {
				policy = cpufreq_cpu_get(cpu);
				if (likely(policy)) {
					policy->user_policy.min = cpu_boost_policy[cpu].saved_min;
					cpufreq_cpu_put(policy);
					cpufreq_update_policy(cpu);
				}
			}
		}
		cpu_boost_policy[cpu].boost_freq = 0;
		cpu_boost_policy[cpu].boost_ms = 0;
		cpu_boost_policy[cpu].cpu_boosted = 0;
	}
	put_online_cpus();
}

static void __cpuinit cpu_boost_main(struct work_struct *work)
{
	unsigned int cpu;

	if (!cpu_boost_policy[0].cpu_boosted)
		save_orig_minfreq();
	else if (cpu_boost_policy[0].cpu_boosted == 1) {
		restore_orig_minfreq();
		return;
	}

	if (cpu_boost_policy[0].boost_freq) {
		for_each_possible_cpu(cpu)
			cpu_boost_policy[cpu].cpu_boosted = 1;
		set_new_minfreq(cpu_boost_policy[0].boost_freq);
	}

	schedule_delayed_work(&boost_work,
				msecs_to_jiffies(cpu_boost_policy[0].boost_ms));
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

	init_done = true;

	return ret;
}
late_initcall(cpu_boost_init);

MODULE_AUTHOR("Sultanxda <sultanxda@gmail.com>");
MODULE_DESCRIPTION("CPU-boost framework");
MODULE_LICENSE("GPLv2");
