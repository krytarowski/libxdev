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
#include <errno.h>
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

const static uint8_t one = '1';

struct xdev_monitor *
xdev_monitor_new(struct xdev *x)
{
	struct xdev_monitor *xm;

	if (__predict_false(x == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(x->magic != XDEV_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	xm = (struct xdev_monitor *)calloc(sizeof(*xm), 1);
	if (__predict_false(xm == NULL))
		return NULL;

	if (__predict_false(
		pipe2(xm->shutdown_fd, O_CLOEXEC | O_NONBLOCK) == -1))
		goto fail;

	if (__predict_false(pipe2(xm->pipe_fd, O_CLOEXEC | O_NONBLOCK) == -1))
		goto fail2;

	if (__predict_false(pthread_mutex_init(&xm->mutex, NULL) != 0))
		goto fail3;

	xm->refcnt = 1;
	xm->magic = XDEV_MONITOR_MAGIC;
	xm->xdev = x;
	TAILQ_INIT(&xm->devices);

	return xm;

fail3:
	close(xm->pipe_fd[0]);
	close(xm->pipe_fd[1]);

fail2:
	close(xm->shutdown_fd[0]);
	close(xm->shutdown_fd[1]);

fail:
	free(xm);

	return NULL;
}

struct xdev_monitor *
xdev_monitor_ref(struct xdev_monitor *xm)
{

	if (__predict_false(xm == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(xm->magic != XDEV_MONITOR_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	xm->refcnt++;

	return xm;
}

struct xdev_monitor *
xdev_monitor_unref(struct xdev_monitor *xm)
{

	if (__predict_false(xm == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(xm->magic != XDEV_MONITOR_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	if (xm->refcnt == 1) {
		if (xm->thread != NULL) {
			xwrite(xm->shutdown_fd[1], &one, 1);
			pthread_join(xm->thread, NULL);
		}
		close(xm->shutdown_fd[0]);
		close(xm->shutdown_fd[1]);
		close(xm->pipe_fd[0]);
		close(xm->pipe_fd[1]);
		xdev_list_free(&xm->devices);
		pthread_mutex_destroy(&xm->mutex);
		xm->magic = 0xdeadbeef;
		free(xm);
		return NULL;
	}

	xm->refcnt--;

	return xm;
}

struct xdev *
xdev_monitor_get_xdev(struct xdev_monitor *xm)
{

	if (__predict_false(xm == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(xm->magic != XDEV_MONITOR_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	return xm->xdev;
}

int
xdev_monitor_filter(struct xdev_monitor *xm, xdev_filter_cb xfcb,
	void *xfcb_cookie)
{

	if (__predict_false(xm == NULL)) {
		errno = EINVAL;
		return -1;
	}

	if (__predict_false(xm->magic != XDEV_MONITOR_MAGIC)) {
		errno = EINVAL;
		return -1;
	}

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
	struct xdev_list_entry *xle;
	prop_dictionary_t ev;
	struct pollfd pfd[2];
	int num_fds;
	int drvctl_fd;
	int pipe_end;
	int ret;
	const char *event;
	const char *device;
	const char *parent;
	char *xml;
	bool b;

	assert(arg != NULL);

	xm = (struct xdev_monitor *)arg;
	x = xm->xdev;
	drvctl_fd = xm->xdev->drvctl_fd;
	pipe_end = xm->pipe_fd[1];

	pfd[0].fd = drvctl_fd;
	pfd[0].events = POLLIN;

	pfd[1].fd = xm->shutdown_fd[0];
	pfd[1].events = POLLIN;

	assert(xm->magic == XDEV_MONITOR_MAGIC);
	assert(x != NULL);
	assert(x->magic == XDEV_MAGIC);

	for (;;) {
		num_fds = xpoll(pfd, __arraycount(pfd), INFTIM);
		if (__predict_false(num_fds == -1)) {
			break;
		}

		/* drvctl device error */
		if (__predict_false(pfd[0].revents & (POLLERR|POLLNVAL))) {
			break;
		}

		/* self-pipe error */
		if (__predict_false(pfd[1].revents & (POLLERR|POLLNVAL))) {
			break;
		}

		/* drvctl device HUP */
		if (__predict_false(pfd[0].revents & POLLHUP)) {
			break;
		}

		/* self-pipe HUP */
		if (__predict_false(pfd[0].revents & POLLHUP)) {
			break;
		}

		/* self-pipe signal to interrupt */
		if (pfd[1].revents & POLLIN) {
			break;
		}

		/* drvctl is ready to deliver a message */
		if (pfd[0].revents & POLLIN) {
			__nothing;
		} else {
			/* Can we ever land here? */
			continue;
		}

		/* non-blocking read */
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

		xd = xdev_device_new(x, device, "???", "???", "???", event,
			parent, xml, -1);
		free(xml);
		prop_object_release(ev);

		if (__predict_false(xd == NULL))
			continue;

		if (xm->xfcb && xm->xfcb(xd, xm->xfcb_cookie) != 0) {
			xdev_device_unref(xd);
			continue;
		}

		xle = xdev_list_entry_new(xd);
		if (__predict_false(xle == NULL)) {
			break;
		}
		pthread_mutex_lock(&xm->mutex);
		TAILQ_INSERT_TAIL(&xm->devices, xle, link);
		pthread_mutex_unlock(&xm->mutex);

		if (__predict_false(xwrite(xm->pipe_fd[1], &one, 1) != 1)) {
			pthread_mutex_lock(&xm->mutex);
			TAILQ_REMOVE(&xm->devices, xle, link);
			pthread_mutex_unlock(&xm->mutex);
			xdev_device_unref(xd);
			free(xle);
			continue;
		}
	}

	return NULL;
}

int
xdev_monitor_enable_receiving(struct xdev_monitor *xm)
{
	int rv;

	if (__predict_false(xm == NULL)) {
		errno = EINVAL;
		return -1;
	}

	if (__predict_false(xm->magic != XDEV_MONITOR_MAGIC)) {
		errno = EINVAL;
		return -1;
	}

	rv = pthread_create(&xm->thread, NULL, xdev_monitor_thread, xm);
	if (__predict_false(rv != 0)) {
		return -1;
	}

	return 0;
}

int
xdev_monitor_get_fd(struct xdev_monitor *xm)
{

	if (__predict_false(xm == NULL)) {
		errno = EINVAL;
		return -1;
	}

	if (__predict_false(xm->magic != XDEV_MONITOR_MAGIC)) {
		errno = EINVAL;
		return -1;
	}

	return xm->pipe_fd[0];
}

struct xdev_device *
xdev_monitor_receive_device(struct xdev_monitor *xm)
{
	struct xdev_list_entry *xle;
	struct xdev_device *xd;
	uint8_t byte;

	if (__predict_false(xm == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(xm->magic != XDEV_MONITOR_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(xread(xm->pipe_fd[0], &byte, 1) < 0))
		return NULL;

	pthread_mutex_lock(&xm->mutex);
	if (TAILQ_EMPTY(&xm->devices))
		goto fail;
	xle = TAILQ_FIRST(&xm->devices);
	TAILQ_REMOVE(&xm->devices, xle, link);
	pthread_mutex_unlock(&xm->mutex);
	xd = xdev_list_entry_get_device(xle);
	free(xle);

	return xd;

fail:
	pthread_mutex_unlock(&xm->mutex);
	errno = ENOBUFS;
	return NULL;
}
