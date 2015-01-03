/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 *
 * Copyright (C) 2014-2015, Sultanxda <sultanxda@gmail.com>
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

#define pr_fmt(fmt) "MSM_THERMAL: " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/msm_tsens.h>
#include <linux/notifier.h>

#define TSENS_SENSOR 0

enum {
	NO_THROTTLE = 0,
	UNTHROTTLE,
	LOW_THROTTLE,
	MID_THROTTLE,
	HIGH_THROTTLE,
	THROTTLED,
};

struct throttle_vars {
	unsigned int saved_max;
	unsigned int throttle_freq;
	unsigned int cpu_throttle;
};

static DEFINE_PER_CPU(struct throttle_vars, throttle_info);

static struct delayed_work msm_thermal_main_work;
static struct workqueue_struct *thermal_wq;

static struct msm_thermal_tuners {
	unsigned int start;

	unsigned int trip_high_thresh;
	unsigned int reset_high_thresh;
	unsigned int freq_high_thresh;

	unsigned int trip_mid_thresh;
	unsigned int reset_mid_thresh;
	unsigned int freq_mid_thresh;

	unsigned int trip_low_thresh;
	unsigned int reset_low_thresh;
	unsigned int freq_low_thresh;

	unsigned int poll_ms;
} therm_conf = {
	.start = 0,

	.trip_high_thresh = 80,
	.reset_high_thresh = 75,
	.freq_high_thresh = 384000,

	.trip_mid_thresh = 69,
	.reset_mid_thresh = 65,
	.freq_mid_thresh = 972000,

	.trip_low_thresh = 64,
	.reset_low_thresh = 60,
	.freq_low_thresh = 1188000,

	.poll_ms = 3000,
};

static void msm_thermal_main(struct work_struct *work)
{
	struct cpufreq_policy *policy;
	struct tsens_device tsens_dev;
	struct throttle_vars *t;
	unsigned long temp;
	unsigned int cpu;
	int ret;

	tsens_dev.sensor_num = TSENS_SENSOR;
	ret = tsens_get_temp(&tsens_dev, &temp);
	if (ret || temp > 1000) {
		pr_err("Unable to read tsens sensor #%d\n",
				tsens_dev.sensor_num);
		goto reschedule;
	}

	get_online_cpus();
	for_each_possible_cpu(cpu) {
		t = &per_cpu(throttle_info, cpu);

		/* low trip point */
		if ((temp >= therm_conf.trip_low_thresh) &&
		(temp < therm_conf.trip_mid_thresh) &&
			(t->cpu_throttle == NO_THROTTLE)) {
			pr_warn("Low trip point triggered for CPU%d! temp: %luC\n", cpu, temp);
			t->throttle_freq = therm_conf.freq_low_thresh;
			t->cpu_throttle = LOW_THROTTLE;
		/* low clear point */
		} else if ((temp <= therm_conf.reset_low_thresh) &&
			(t->cpu_throttle > UNTHROTTLE)) {
			pr_warn("Low trip point cleared for CPU%d! temp: %luC\n", cpu, temp);
			t->cpu_throttle = UNTHROTTLE;
		/* mid trip point */
		} else if ((temp >= therm_conf.trip_mid_thresh) &&
			(temp < therm_conf.trip_high_thresh) &&
			(t->cpu_throttle < MID_THROTTLE)) {
			pr_warn("Mid trip point triggered for CPU%d! temp: %luC\n", cpu, temp);
			t->throttle_freq = therm_conf.freq_mid_thresh;
			t->cpu_throttle = MID_THROTTLE;
		/* mid clear point */
		} else if ((temp < therm_conf.reset_mid_thresh) &&
			(t->cpu_throttle > LOW_THROTTLE)) {
			pr_warn("Mid trip point cleared for CPU%d! temp: %luC\n", cpu, temp);
			t->throttle_freq = therm_conf.freq_low_thresh;
			t->cpu_throttle = LOW_THROTTLE;
		/* high trip point */
		} else if ((temp >= therm_conf.trip_high_thresh) &&
			(t->cpu_throttle < HIGH_THROTTLE)) {
			pr_warn("High trip point triggered for CPU%d! temp: %luC\n", cpu, temp);
			t->throttle_freq = therm_conf.freq_high_thresh;
			t->cpu_throttle = HIGH_THROTTLE;
		/* high clear point */
		} else if ((temp < therm_conf.reset_high_thresh) &&
			(t->cpu_throttle > MID_THROTTLE)) {
			pr_warn("High trip point cleared for CPU%d! temp: %luC\n", cpu, temp);
			t->throttle_freq = therm_conf.freq_mid_thresh;
			t->cpu_throttle = MID_THROTTLE;
		}
		/*
		 * CPUs that are online at the time a throttle/unthrottle request is
		 * sent are throttled/unthrottled from here. After user_policy is
		 * modified from here to throttle a CPU, the cpufreq notifier below
		 * does not do anything for said throttled CPU in order to avoid
		 * a user_policy vs. policy condition that causes the maxfreq to
		 * be permanently stuck at the throttle freq if the user attempts to
		 * modify the maxfreq (user_policy) while the CPU is throttled.
		 */
		if (cpu_online(cpu)) {
			policy = cpufreq_cpu_get(cpu);
			if (policy != NULL) {
				switch (t->cpu_throttle) {
				case UNTHROTTLE:
					policy->user_policy.max = t->saved_max;
					t->cpu_throttle = NO_THROTTLE;
					break;
				case LOW_THROTTLE:
				case MID_THROTTLE:
				case HIGH_THROTTLE:
				case THROTTLED: /* Re-throttle online CPUs on every polling interval. */
					if (policy->user_policy.min > t->throttle_freq)
						policy->user_policy.min = policy->cpuinfo.min_freq;
					policy->user_policy.max = t->throttle_freq;
					t->cpu_throttle = THROTTLED;
					break;
				}
				cpufreq_cpu_put(policy);
				cpufreq_update_policy(cpu);
			}
		}
	}
	put_online_cpus();

reschedule:
	queue_delayed_work(thermal_wq, &msm_thermal_main_work,
				msecs_to_jiffies(therm_conf.poll_ms));
}

static int cpu_throttle(struct notifier_block *nb, unsigned long val, void *data)
{
	struct cpufreq_policy *policy = data;
	struct throttle_vars *t = &per_cpu(throttle_info, policy->cpu);

	if (val != CPUFREQ_ADJUST)
		return NOTIFY_OK;

	/*
	 * CPUs that were offline at the time a throttle/unthrottle request was
	 * sent are throttled/unthrottled from here.
	 */
	switch (t->cpu_throttle) {
	case NO_THROTTLE:
		t->saved_max = policy->max;
		break;
	case UNTHROTTLE:
		policy->max = t->saved_max;
		t->cpu_throttle = NO_THROTTLE;
		break;
	case LOW_THROTTLE:
	case MID_THROTTLE:
	case HIGH_THROTTLE:
		if (policy->min > t->throttle_freq)
			policy->min = policy->cpuinfo.min_freq;
		policy->max = t->throttle_freq;
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block cpu_throttle_nb = {
	.notifier_call = cpu_throttle,
};

/**************************** SYSFS START ****************************/
struct kobject *msm_thermal_kobject;

#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)               \
{									\
	return sprintf(buf, "%u\n", therm_conf.object);			\
}

show_one(start, start);
show_one(trip_high_thresh, trip_high_thresh);
show_one(reset_high_thresh, reset_high_thresh);
show_one(freq_high_thresh, freq_high_thresh);
show_one(trip_mid_thresh, trip_mid_thresh);
show_one(reset_mid_thresh, reset_mid_thresh);
show_one(freq_mid_thresh, freq_mid_thresh);
show_one(trip_low_thresh, trip_low_thresh);
show_one(reset_low_thresh, reset_low_thresh);
show_one(freq_low_thresh, freq_low_thresh);

static ssize_t store_start(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	/* one-way switch to init msm_thermal */
	if (!therm_conf.start && input) {
		therm_conf.start = input;
		pr_err("Starting thermal mitigation\n");
		queue_delayed_work(thermal_wq, &msm_thermal_main_work, 0);
	}

	return count;
}

static ssize_t store_trip_high_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.trip_high_thresh = input;

	return count;
}

static ssize_t store_reset_high_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.reset_high_thresh = input;

	return count;
}

static ssize_t store_freq_high_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.freq_high_thresh = input;

	return count;
}

static ssize_t store_trip_mid_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.trip_mid_thresh = input;

	return count;
}

static ssize_t store_reset_mid_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.reset_mid_thresh = input;

	return count;
}

static ssize_t store_freq_mid_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.freq_mid_thresh = input;

	return count;
}

static ssize_t store_trip_low_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.trip_low_thresh = input;

	return count;
}

static ssize_t store_reset_low_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.reset_low_thresh = input;

	return count;
}

static ssize_t store_freq_low_thresh(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	therm_conf.freq_low_thresh = input;

	return count;
}

define_one_global_rw(start);
define_one_global_rw(trip_high_thresh);
define_one_global_rw(reset_high_thresh);
define_one_global_rw(freq_high_thresh);
define_one_global_rw(trip_mid_thresh);
define_one_global_rw(reset_mid_thresh);
define_one_global_rw(freq_mid_thresh);
define_one_global_rw(trip_low_thresh);
define_one_global_rw(reset_low_thresh);
define_one_global_rw(freq_low_thresh);

static struct attribute *msm_thermal_attributes[] = {
	&start.attr,
	&trip_high_thresh.attr,
	&reset_high_thresh.attr,
	&freq_high_thresh.attr,
	&trip_mid_thresh.attr,
	&reset_mid_thresh.attr,
	&freq_mid_thresh.attr,
	&trip_low_thresh.attr,
	&reset_low_thresh.attr,
	&freq_low_thresh.attr,
	NULL
};

static struct attribute_group msm_thermal_attr_group = {
	.attrs = msm_thermal_attributes,
	.name = "conf",
};
/**************************** SYSFS END ****************************/

static int __init msm_thermal_init(void)
{
	int ret = 0;

	thermal_wq = alloc_workqueue("msm_thermal_wq", WQ_HIGHPRI, 0);
	if (!thermal_wq) {
		pr_err("Failed to allocate workqueue\n");
		ret = -EFAULT;
		goto fail;
	}

	cpufreq_register_notifier(&cpu_throttle_nb, CPUFREQ_POLICY_NOTIFIER);

	INIT_DELAYED_WORK(&msm_thermal_main_work, msm_thermal_main);

	msm_thermal_kobject = kobject_create_and_add("msm_thermal", kernel_kobj);
	if (msm_thermal_kobject) {
		ret = sysfs_create_group(msm_thermal_kobject,
							&msm_thermal_attr_group);
		if (ret)
			pr_err("sysfs: ERROR, could not create sysfs group");
	} else
		pr_err("sysfs: ERROR, could not create sysfs kobj");

fail:
	return ret;
}
fs_initcall(msm_thermal_init);
