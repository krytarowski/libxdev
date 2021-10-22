/*	$NetBSD$	*/
/*-
 * Copyright (c) 2021 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Kamil Rytarowski.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _XDEV_H_
#define _XDEV_H_

#include <sys/cdefs.h>
#include <sys/types.h>

struct xdev;
struct xdev_device;
struct xdev_enumerate;
struct xdev_list_entry;
struct xdev_monitor;

__BEGIN_DECLS
struct xdev *xdev_new(void);
struct xdev *xdev_ref(struct xdev *);
struct xdev *xdev_unref(struct xdev *);

void *xdev_get_userdata(struct xdev *);
void xdev_set_userdata(struct xdev *, void *);

#define xdev_list_entry_foreach(entry, head) \
	for (entry = head; entry; entry = xdev_list_entry_get_next(entry))
struct xdev_list_entry *xdev_list_entry_get_next(struct xdev_list_entry *);
struct xdev_device *xdev_list_entry_get_device(struct xdev_list_entry *);

struct xdev_device *xdev_device_from_node(struct xdev *, devmajor_t, uint32_t,
	mode_t);
struct xdev_device *xdev_device_from_devname(struct xdev *, const char *);
struct xdev_device *xdev_device_ref(struct xdev_device *);
struct xdev_device *xdev_device_unref(struct xdev_device *);
struct xdev *xdev_device_get_xdev(struct xdev_device *);

int xdev_device_get_devname(struct xdev_device *, const char **);
int xdev_device_get_driver(struct xdev_device *xd, const char **);
int xdev_device_get_devclass(struct xdev_device *xd, const char **);
int xdev_device_get_devsubclass(struct xdev_device *, const char **);
int xdev_device_get_event(struct xdev_device *, const char **);
int xdev_device_get_parent(struct xdev_device *, const char **);
int xdev_device_get_unit(struct xdev_device *, uint32_t *);
int xdev_device_get_major(struct xdev_device *, mode_t, devmajor_t *);
int xdev_device_externalize(struct xdev_device *, const char **);

typedef int (*xdev_filter_cb)(struct xdev_device *, void *c);

#define XDEV_INF_DEPTH -1

struct xdev_enumerate *xdev_enumerate_new(struct xdev *);
struct xdev_enumerate *xdev_enumerate_ref(struct xdev_enumerate *);
struct xdev_enumerate *xdev_enumerate_unref(struct xdev_enumerate *);
struct xdev *xdev_enumerate_get_xdev(struct xdev_enumerate *);

int xdev_enumerate_filter(struct xdev_enumerate *, xdev_filter_cb, void *);
int xdev_enumerate_scan_devices(struct xdev_enumerate *, const char *, int);
struct xdev_list_entry *xdev_enumerate_get_list_entry(struct xdev_enumerate *);

struct xdev_monitor *xdev_monitor_new(struct xdev *x);
struct xdev_monitor *xdev_monitor_ref(struct xdev_monitor *xm);
struct xdev_monitor *xdev_monitor_unref(struct xdev_monitor *xm);
struct xdev *xdev_monitor_get_xdev(struct xdev_monitor *xm);

int xdev_monitor_filter(struct xdev_monitor *, xdev_filter_cb, void *);
int xdev_monitor_enable_receiving(struct xdev_monitor *);
int xdev_monitor_get_fd(struct xdev_monitor *);
struct xdev_device *xdev_monitor_receive_device(struct xdev_monitor *);
__END_DECLS

#endif /* !_XDEV_H_ */
