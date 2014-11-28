/*
 * include/linux/cpu_input_boost.h
 *
 * Copyright (c) 2014, Sultanxda <sultanxda@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_CPU_INPUT_BOOST_H
#define _LINUX_CPU_INPUT_BOOST_H

#ifdef CONFIG_CPU_INPUT_BOOST
struct boost_policy {
	bool cpu_boosted;
	unsigned int boost_freq;
	unsigned int boost_ms;
	unsigned int cpu;
	unsigned int saved_min;
	unsigned int saved_max;
	struct work_struct boost_work;
	struct delayed_work restore_work;
};

extern DEFINE_PER_CPU(struct boost_policy, boost_info);
#endif /* CONFIG_CPU_INPUT_BOOST */

#endif /* _LINUX_CPU_INPUT_BOOST_H */
