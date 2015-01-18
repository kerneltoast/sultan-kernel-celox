/*
 * include/linux/bln.h
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

#ifndef _LINUX_BLN_H
#define _LINUX_BLN_H
struct bln_implementation {
    bool (*enable)(void);
    void (*disable)(void);
    void (*on)(void);
    void (*off)(void);
};

void register_bln_implementation(struct bln_implementation *imp);
#endif
