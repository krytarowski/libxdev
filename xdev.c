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

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "xdev.h"
#include "xdev_private.h"
#include "xdev_utils.h"

struct xdev *
xdev_new(void)
{
	struct xdev *x;

	x = (struct xdev *)calloc(sizeof(*x), 1);
	if (__predict_false(x == NULL))
		return NULL;

	x->drvctl_fd = xopen(DRVCTLDEV, O_RDWR | O_CLOEXEC);
	if (__predict_false(x->drvctl_fd == -1))
		goto fail;

	x->refcnt = 1;
	x->magic = XDEV_MAGIC;

	return x;

fail:
	free(x);

	return NULL;
}

struct xdev *
xdev_ref(struct xdev *x)
{

	if (__predict_false(x == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(x->magic != XDEV_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	x->refcnt++;

	return x;
}

struct xdev *
xdev_unref(struct xdev *x)
{

	if (__predict_false(x == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(x->magic != XDEV_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	if (x->refcnt == 1) {
		xclose(x->drvctl_fd);
		x->magic = 0xdeadbeef;
		free(x);
		return NULL;
	}

	x->refcnt--;

	return x;
}

void *
xdev_get_userdata(struct xdev *x)
{

	if (__predict_false(x == NULL)) {
		errno = EINVAL;
		return NULL;
	}

	if (__predict_false(x->magic != XDEV_MAGIC)) {
		errno = EINVAL;
		return NULL;
	}

	return x->user;
}

void
xdev_set_userdata(struct xdev *x, void *user)
{

	if (__predict_false(x == NULL)) {
		errno = EINVAL;
		return;
	}

	if (__predict_false(x->magic != XDEV_MAGIC)) {
		errno = EINVAL;
		return;
	}

	x->user = user;
}
