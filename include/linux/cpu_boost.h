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
void cpu_boost_timeout(unsigned int freq_mhz, unsigned int duration_ms);

/**
 * cpu_boost() - boost online CPUs indefinitely
 * @freq_mhz:	frequency in MHz to boost the CPUs by
 */
void cpu_boost(unsigned int freq_mhz);

/**
 * cpu_unboost() - unboost indefinitely-boosted CPUs
 */
void cpu_unboost(void);

/**
 * cpu_boost_shutdown() - disable CPU-boost framework
 */
void cpu_boost_shutdown(void);

/**
 * cpu_boost_startup() - enable CPU-boost framework
 */
void cpu_boost_startup(void);

#else
static inline void cpu_boost_timeout(unsigned int freq_mhz, unsigned int duration_ms) { }
static inline void cpu_boost(unsigned int freq_mhz) { }
static inline void cpu_unboost(void) { }
static inline void cpu_boost_shutdown(void) { }
static inline void cpu_boost_startup(void) { }

#endif /* CONFIG_CPU_BOOST_FRAMEWORK */
#endif /* _LINUX_CPU_BOOST_H */
