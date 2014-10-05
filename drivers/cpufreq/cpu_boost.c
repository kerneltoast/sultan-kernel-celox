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

#include <linux/cpu.h>
#include <linux/cpu_boost.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

static struct delayed_work boost_work;

static DECLARE_COMPLETION(cpu_boost_no_timeout);

static unsigned int boost_duration_ms = 0;
static unsigned int boost_freq_khz = 0;
static unsigned int boost_override = 0;
static unsigned int cpu_boosted = 0;
static unsigned int enable = 1;
static unsigned int init_done = 0;
static unsigned int maxfreq_orig = 0;
static unsigned int minfreq_orig = 0;

void cpu_boost_timeout(unsigned int freq_mhz, unsigned int duration_ms)
{
	if (init_done && enable) {
		if (cpu_boosted) {
			cpu_boosted = 0;
			boost_override = 1;
			cancel_delayed_work(&boost_work);
		}

		boost_freq_khz = freq_mhz * 1000;
		boost_duration_ms = duration_ms;
		schedule_delayed_work(&boost_work, 0);
	}
}

void cpu_boost(unsigned int freq_mhz)
{
	if (init_done && enable) {
		if (cpu_boosted) {
			cpu_boosted = 0;
			boost_override = 1;
			cancel_delayed_work(&boost_work);
		}

		init_completion(&cpu_boost_no_timeout);
		boost_freq_khz = freq_mhz * 1000;
		schedule_delayed_work(&boost_work, 0);
	}
}

void cpu_unboost(void)
{
	if (init_done && enable)
		complete(&cpu_boost_no_timeout);
}

void cpu_boost_shutdown(void)
{
	if (init_done) {
		enable = 0;
		pr_info("%s: CPU-boost disabled!\n", __func__);
	}
}

void cpu_boost_startup(void)
{
	if (init_done) {
		enable = 1;
		pr_info("%s: CPU-boost enabled!\n", __func__);
	}
}

static void save_original_cpu_limits(void)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);

	minfreq_orig = policy->user_policy.min;
	maxfreq_orig = policy->user_policy.max;
}

static void set_new_minfreq(struct cpufreq_policy *policy,
	unsigned int minfreq)
{
	policy->user_policy.min = minfreq;
}

static void restore_original_minfreq(void)
{
	struct cpufreq_policy *policy = NULL;
	unsigned int cpu = 0;

	get_online_cpus();
	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		set_new_minfreq(policy, minfreq_orig);
		cpufreq_update_policy(cpu);
		cpufreq_cpu_put(policy);
	}
	put_online_cpus();

	boost_duration_ms = 0;
	cpu_boosted = 0;
	boost_override = 0;
}

static void __cpuinit cpu_boost_main(struct work_struct *work)
{
	struct cpufreq_policy *policy = NULL;
	unsigned int cpu = 0, minfreq = 0, wait_ms = 0;

	if (cpu_boosted) {
		restore_original_minfreq();
		return;
	}

	if (!boost_override)
		save_original_cpu_limits();

	if (boost_freq_khz) {

		if (boost_freq_khz >= maxfreq_orig) {
			if (maxfreq_orig <= 486000) {
				boost_duration_ms = 0;
				boost_override = 0;
				return;
			} else
				minfreq = maxfreq_orig - 108000;
		} else
			minfreq = boost_freq_khz;

		/* boost online CPUs */
		get_online_cpus();
		for_each_online_cpu(cpu) {
			policy = cpufreq_cpu_get(cpu);
			set_new_minfreq(policy, minfreq);
			cpufreq_update_policy(cpu);
			cpufreq_cpu_put(policy);
		}
		put_online_cpus();
		cpu_boosted = 1;
	}

	if (boost_duration_ms)
		wait_ms = boost_duration_ms;
	else
		wait_for_completion(&cpu_boost_no_timeout);


	schedule_delayed_work(&boost_work,
				msecs_to_jiffies(wait_ms));
}

static int __init cpu_boost_init(void)
{
	INIT_DELAYED_WORK(&boost_work, cpu_boost_main);

	init_done = 1;

	return 0;
}
late_initcall(cpu_boost_init);

MODULE_AUTHOR("Sultanxda <sultanxda@gmail.com>");
MODULE_DESCRIPTION("CPU-boost framework");
MODULE_LICENSE("GPLv2");
