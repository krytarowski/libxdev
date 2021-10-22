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
#include <sys/queue.h>

#include <assert.h>
#include <stdlib.h>

#include "xdev.h"
#include "xdev_list.h"

struct xdev_list_entry *
xdev_list_get_head(struct xdev_list *xl)
{
	struct xdev_list_entry *first;

	if (__predict_false(xl == NULL))
		return NULL;

	first = TAILQ_FIRST(xl);

	if (__predict_false(first->magic != XDEV_LIST_ENTRY_MAGIC))
		return NULL;

	return first;
}

struct xdev_list_entry *
xdev_list_entry_get_next(struct xdev_list_entry *xle)
{
	struct xdev_list_entry *next;

	if (__predict_false(xle == NULL))
		return NULL;

	next = TAILQ_NEXT(xle, link);

	if (next != NULL && __predict_false(next->magic != XDEV_LIST_ENTRY_MAGIC))
		return NULL;

	return next;
}

struct xdev_device *
xdev_list_entry_get_device(struct xdev_list_entry *xle)
{

	if (__predict_false(xle == NULL))
		return NULL;

	if (__predict_false(xle->magic != XDEV_LIST_ENTRY_MAGIC))
		return NULL;

	if (__predict_false(xle->device->magic != XDEV_DEVICE_MAGIC))
		return NULL;

	return xdev_device_ref(xle->device);
}

struct xdev_list_entry *
xdev_list_entry_new(struct xdev_device *xd)
{
	struct xdev_list_entry *e;

	assert(xd != NULL);
	assert(xd->magic == XDEV_DEVICE_MAGIC);

	e = (struct xdev_list_entry *)malloc(sizeof(*e));
	if (__predict_false(e == NULL))
		return NULL;

	e->device = xd;
	e->magic = XDEV_LIST_ENTRY_MAGIC;

	return e;
}

void
xdev_list_free(struct xdev_list *list)
{
	struct xdev_list_entry *e;

	while ((e = TAILQ_FIRST(list)) != NULL) {
		assert(e->magic == XDEV_LIST_ENTRY_MAGIC);

		TAILQ_REMOVE(list, e, link);
		xdev_device_unref(e->device);
		free(e);
	}
}
