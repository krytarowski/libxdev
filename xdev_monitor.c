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

#include <sys/cdefs.h>
__RCSID("$NetBSD$");

#include <sys/types.h>
#include <sys/drvctlio.h>
#include <sys/stat.h>

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xdev.h"
#include "xdev_device.h"
#include "xdev_monitor.h"
#include "xdev_list.h"
#include "xdev_private.h"
#include "xdev_utils.h"

struct xdev_monitor *
xdev_monitor_new(struct xdev *x)
{
	struct xdev_monitor *xm;

	if (__predict_false(x == NULL))
		return NULL;

	if (__predict_false(x->magic != XDEV_MAGIC))
		return NULL;

	xm = (struct xdev_monitor *)calloc(sizeof(*xm), 1);
	if (__predict_false(xm == NULL))
		return NULL;

	if (__predict_false(pipe2(xm->pipe_fd, O_CLOEXEC) == -1))
		goto fail;

	if (pthread_mutex_init(&xm->mutex, NULL) != 0)
		goto fail2;

	xm->refcnt = 1;
	xm->magic = XDEV_MONITOR_MAGIC;
	xm->xdev = x;
	SIMPLEQ_INIT(&xm->devices);

	return xm;

fail2:
	close(xm->pipe_fd[0]);
	close(xm->pipe_fd[1]);

fail:
	free(xm);

	return NULL;
}

struct xdev_monitor *
xdev_monitor_ref(struct xdev_monitor *xm)
{

	if (__predict_false(xm == NULL))
		return NULL;

	xm->refcnt++;

	return xm;
}

struct xdev_monitor *
xdev_monitor_unref(struct xdev_monitor *xm)
{

	if (__predict_false(xm == NULL))
		return NULL;

	if (xm->refcnt == 1) {
		free(xm);
		return NULL;
	}

	xm->refcnt--;

	return xm;
}

struct xdev *
xdev_monitor_get_xdev(struct xdev_monitor *xm)
{

	if (__predict_false(xm == NULL))
		return NULL;

	return xm->xdev;
}

int
xdev_monitor_filter(struct xdev_monitor *xm, xdev_filter_cb xfcb, void *xfcb_cookie)
{

	if (__predict_false(xm == NULL))
		return -1;

	xm->xfcb = xfcb;
	xm->xfcb_cookie = xfcb_cookie;

	return 0;
}

static void *
xdev_monitor_thread(void *arg)
{
	struct xdev_monitor *xm;
	struct xdev_device *xd;
	struct xdev *x;
	struct xdev_monitor_devices_entry *xmde;
	prop_dictionary_t ev;
	int drvctl_fd;
	int pipe_end;
	int ret;
	const char *event;
	const char *device;
	const char *parent;
	char *xml;
	bool b;

	const static uint8_t one = 1;

	assert(arg != NULL);

	xm = (struct xdev_monitor *)arg;
	x = xm->xdev;
	drvctl_fd = xm->xdev->drvctl_fd;
	pipe_end = xm->pipe_fd[1];

	assert(xm->magic == XDEV_MONITOR_MAGIC);
	assert(x != NULL);
	assert(x->magic == XDEV_MAGIC);

	for (;;) {
		ret = prop_dictionary_recv_ioctl(drvctl_fd, DRVGETEVENT, &ev);
		if (__predict_false(ret != 0)) {
			break;
		}

		b = prop_dictionary_get_cstring_nocopy(ev, "event", &event);
		if (__predict_false(b == false)) {
			prop_object_release(ev);
			continue;
		}

		b = prop_dictionary_get_cstring_nocopy(ev, "device", &device);
		if (__predict_false(b == false)) {
			prop_object_release(ev);
			continue;
		}

		b = prop_dictionary_get_cstring_nocopy(ev, "parent", &parent);
		if (__predict_false(b == false)) {
			prop_object_release(ev);
			continue;
		}

		xml = prop_dictionary_externalize(ev);
		if (__predict_false(xml == NULL)) {
			prop_object_release(ev);
			continue;
		}

		xd = xdev_device_new(x, device, "???", "???", "???", event, parent, xml, -1);
		free(xml);
		prop_object_release(ev);

		if (__predict_false(xd == NULL))
			continue;

		if (xm->xfcb && xm->xfcb(xd, xm->xfcb_cookie) != 0) {
			xdev_device_unref(xd);
			continue;
		}

		xmde = (struct xdev_monitor_devices_entry *)malloc(sizeof(*xmde));
		if (__predict_false(xmde == NULL)) {
			break;
		}
		xmde->xd = xd;
		pthread_mutex_lock(&xm->mutex);
		SIMPLEQ_INSERT_TAIL(&xm->devices, xmde, link);
		pthread_mutex_unlock(&xm->mutex);

		if (__predict_false(xwrite(xm->pipe_fd[1], &one, 1) != 1)) {
			pthread_mutex_lock(&xm->mutex);
			SIMPLEQ_REMOVE(&xm->devices, xmde, xdev_monitor_devices_entry, link);
			pthread_mutex_unlock(&xm->mutex);
			xdev_device_unref(xd);
			free(xmde);
			continue;
		}
	}

	return NULL;
}

int
xdev_monitor_enable_receiving(struct xdev_monitor *xm)
{
	int rv;

	if (__predict_false(xm == NULL))
		return -1;

	if (__predict_false(xm->magic != XDEV_MONITOR_MAGIC))
		return -1;

	rv = pthread_create(&xm->thread, NULL, xdev_monitor_thread, xm);
	if (__predict_false(rv != 0)) {
		return -1;
	}

	return 0;
}

int
xdev_monitor_get_fd(struct xdev_monitor *xm)
{

	if (__predict_false(xm == NULL))
		return -1;

	if (__predict_false(xm->magic != XDEV_MONITOR_MAGIC))
		return -1;

	return xm->pipe_fd[0];
}

struct xdev_device *
xdev_monitor_receive_device(struct xdev_monitor *xm)
{
	struct xdev_monitor_devices_entry *xmde;
	struct xdev_device *xd;
	uint8_t byte;

	if (__predict_false(xm == NULL))
		return NULL;

	if (__predict_false(xm->magic != XDEV_MONITOR_MAGIC))
		return NULL;

	if (__predict_false(xread(xm->pipe_fd[0], &byte, 1) < 0))
		return NULL;

	pthread_mutex_lock(&xm->mutex);
	if (SIMPLEQ_EMPTY(&xm->devices))
		goto fail;
	xmde = SIMPLEQ_FIRST(&xm->devices);
	SIMPLEQ_REMOVE_HEAD(&xm->devices, link);
	pthread_mutex_unlock(&xm->mutex);
	xd = xmde->xd;
	free(xmde);

	return xd;

fail:
	pthread_mutex_unlock(&xm->mutex);
	return NULL;
}
