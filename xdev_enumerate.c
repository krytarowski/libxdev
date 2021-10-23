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
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "xdev.h"
#include "xdev_enumerate.h"
#include "xdev_list.h"
#include "xdev_private.h"


struct xdev_enumerate *
xdev_enumerate_new(struct xdev *x)
{
	struct xdev_enumerate *xe;

	if (__predict_false(x == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(x->magic != XDEV_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	xe = (struct xdev_enumerate *)calloc(sizeof(*xe), 1);
	if (__predict_false(xe == NULL)) 
		return NULL;

	xe->refcnt = 1;
	xe->magic = XDEV_ENUMERATE_MAGIC;
	xe->xdev = x;
	TAILQ_INIT(&xe->devices);

	return xe;
}

struct xdev_enumerate *
xdev_enumerate_ref(struct xdev_enumerate *xe)
{

	if (__predict_false(xe == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(xe->magic != XDEV_ENUMERATE_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	xe->refcnt++;

	return xe;
}

struct xdev_enumerate *
xdev_enumerate_unref(struct xdev_enumerate *xe)
{

	if (__predict_false(xe == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(xe->magic != XDEV_ENUMERATE_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	if (xe->refcnt == 1) {
		xdev_list_free(&xe->devices);
		xe->magic = 0xdeadbeef;
		free(xe);
		return NULL;
	}

	xe->refcnt--;

	return xe;
}

struct xdev *
xdev_enumerate_get_xdev(struct xdev_enumerate *xe)
{

	if (__predict_false(xe == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(xe->magic != XDEV_ENUMERATE_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	assert(xe->xdev->magic == XDEV_MAGIC);

	return xe->xdev;
}

int
xdev_enumerate_filter(struct xdev_enumerate *xe, xdev_filter_cb xfcb,
	void *xfcb_cookie)
{

	if (__predict_false(xe == NULL)) {
		errno = EINVAL;
		return -1;
	}

	if (__predict_false(xe->magic != XDEV_ENUMERATE_MAGIC)) {
		errno = EINVAL;
		return -1;
	}

	xe->xfcb = xfcb;
	xe->xfcb_cookie = xfcb_cookie;

	return 0;
}

static int
xdev_enumerate_scan_devices_recursive(struct xdev_enumerate *xe,
	const char *devname, int depth, int max_depth)
{
	struct xdev_device *device;
	struct xdev_list_entry *entry;
	char *child;
	struct devlistargs laa;
	size_t i, children;
	int drvctl_fd;
	int ret;

	assert(xe != NULL);
	assert(depth >= 0);

	if (depth > 0 && (devname == NULL || devname[0] == '\0'))
		return 0;

	if (max_depth != XDEV_INF_DEPTH && depth > max_depth)
		return 0;

	drvctl_fd = xe->xdev->drvctl_fd;

	memset(&laa, 0, sizeof(laa));
	strlcpy(laa.l_devname, devname, sizeof(laa.l_devname));

retry:
	if (__predict_false(ioctl(drvctl_fd, DRVLISTDEV, &laa) == -1))
		goto fail;

	if ((children = laa.l_children) == 0)
		goto end;

	ret = reallocarr(&laa.l_childname, children,
		sizeof(laa.l_childname[0]));
	if (__predict_false(ret != 0))
		goto fail;

	if (__predict_false(ioctl(drvctl_fd, DRVLISTDEV, &laa) == -1))
                goto fail;

	if (__predict_false(laa.l_children != children))
		goto retry;

        for (i = 0; i < children; i++) {
		child = laa.l_childname[i];
		device = xdev_device_from_devname(xe->xdev, child);
		if (__predict_false(device == NULL)) {
			/* Device detached? */
			continue;
		}

		ret = xdev_enumerate_scan_devices_recursive(xe, child,
			depth + 1, max_depth);
		if (__predict_false(ret == -1)) {
			xdev_device_unref(device);
			goto fail;
		}

		if (xe->xfcb && xe->xfcb(device, xe->xfcb_cookie) != 0) {
			xdev_device_unref(device);
			continue;
		}

		entry = xdev_list_entry_new(device);
		if (__predict_false(entry == NULL)) {
			xdev_device_unref(device);
			goto fail;
		}

		TAILQ_INSERT_TAIL(&xe->devices, entry, link);
		++xe->num_devices;
        }

end:
	free(laa.l_childname);
	return 0;

fail:
	free(laa.l_childname);
	return -1;
}

int
xdev_enumerate_scan_devices(struct xdev_enumerate *xe, const char *root_devname,
	int max_depth)
{
	int ret;

	if (__predict_false(xe == NULL)) {
		errno = EINVAL;
		return -1;
	}

	if (__predict_false(xe->magic != XDEV_ENUMERATE_MAGIC)) {
		errno = EINVAL;
		return -1;
	}

	xe->num_devices = 0;
	xdev_list_free(&xe->devices);
	TAILQ_INIT(&xe->devices);

	ret = xdev_enumerate_scan_devices_recursive(xe, root_devname, 0,
		max_depth);
	if (__predict_false(ret == -1)) {
		xdev_list_free(&xe->devices);
		return -1;
	}

	return xe->num_devices;
}

struct xdev_list_entry *
xdev_enumerate_get_list_entry(struct xdev_enumerate *xe)
{

	if (__predict_false(xe == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(xe->magic != XDEV_ENUMERATE_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	return TAILQ_FIRST(&xe->devices);
}
