/*
 * include/linux/cpu_boost.h
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

#ifndef _LINUX_CPU_BOOST_H
#define _LINUX_CPU_BOOST_H


#ifdef CONFIG_CPU_BOOST_FRAMEWORK
/**
 * cpu_boost_timeout() - boost online CPUs for a given amount of time
 * @freq_mhz:	frequency in MHz to boost the CPUs by
 * @duration_ms: duration in milliseconds to boost the CPUs
 */
void cpu_boost_timeout(unsigned int freq, unsigned int duration_ms);

struct boost_policy {
	unsigned int boost_freq;
	unsigned int boost_ms;
	unsigned int cpu_boosted;
	unsigned int saved_min;
};

#else
static inline void cpu_boost_timeout(unsigned int freq, unsigned int duration_ms) { }

#endif /* CONFIG_CPU_BOOST_FRAMEWORK */
#endif /* _LINUX_CPU_BOOST_H */
