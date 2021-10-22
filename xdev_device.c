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
#include <sys/sysctl.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xdev.h"
#include "xdev_device.h"
#include "xdev_list.h"
#include "xdev_private.h"
#include "xdev_utils.h"

struct xdev_device *
xdev_device_new(struct xdev *x, const char *devname, const char *driver,
	const char *devclass, const char *devsubclass, const char *event,
	const char *parent, const char *xml, uint32_t unit)
{
	struct xdev_device *xd;

	assert(x != NULL);
	assert(x->magic == XDEV_MAGIC);
	assert(devname != NULL);
	assert(driver != NULL);
	assert(devclass != NULL);
	assert(devsubclass != NULL);
	assert(event != NULL);
	assert(parent != NULL);
	assert(xml != NULL);

	xd = (struct xdev_device *)calloc(sizeof(*xd), 1);
	if (__predict_false(xd == NULL))
		return NULL;

	xd->refcnt = 1;
	xd->magic = XDEV_DEVICE_MAGIC;
	xd->xdev = x;

	xd->devname = strdup(devname);
	if (__predict_false(xd->devname == NULL))
		goto fail1;

	xd->driver = strdup(driver);
	if (__predict_false(xd->driver == NULL))
		goto fail2;

	xd->devclass = strdup(devclass);
	if (__predict_false(xd->devclass == NULL))
		goto fail3;

	xd->devsubclass = strdup(devsubclass);
	if (__predict_false(xd->devsubclass == NULL))
		goto fail4;

	xd->event = strdup(event);
	if (__predict_false(xd->event == NULL))
		goto fail5;

	xd->parent = strdup(parent);
	if (__predict_false(xd->parent == NULL))
		goto fail6;

	xd->xml = strdup(xml);
	if (__predict_false(xd->xml == NULL))
		goto fail7;

	xd->unit = unit;

	return xd;

fail7:
	free(xd->parent);
fail6:
	free(xd->event);
fail5:
	free(xd->devsubclass);
fail4:
	free(xd->devclass);
fail3:
	free(xd->driver);
fail2:
	free(xd->devname);
fail1:
	free(xd);

	return NULL;
}

struct xdev_device *
xdev_device_from_node(struct xdev *x, devmajor_t major, uint32_t unit, mode_t m)
{
	struct xdev_device *xd;
	struct kinfo_drivers *kid;
	size_t i, cnt;
	int ret;
	char *driver;
	char *devname;

	if (__predict_false(x == NULL))
		return NULL;

	if (__predict_false(x->magic != XDEV_MAGIC))
		return NULL;

	if (__predict_false(m != S_IFCHR && m != S_IFBLK))
		return NULL;

	kid = kinfo_getdrivers(&cnt);
	if (__predict_false(kid == NULL))
		return NULL;

	driver = NULL;
	switch(m) {
	case S_IFCHR:
		for (i = 0; i < cnt; i++) {
			if (kid[i].d_cmajor == major) {
				driver = kid[i].d_name;
				break;
			}
		}
		break;
	case S_IFBLK:
		for (i = 0; i < cnt; i++) {
			if (kid[i].d_bmajor == major) {
				driver = kid[i].d_name;
				break;
			}
		}
		break;
	}
	if (__predict_false(driver == NULL))
		goto fail;

	ret = asprintf(&devname, "%s%" PRIu32, driver, unit);
	if (__predict_false(ret == -1))
		goto fail;

	free(kid);

	xd = xdev_device_from_devname(x, devname);
	free(devname);
	return xd;

fail:
	free(kid);
	return NULL;
}

struct xdev_device *
xdev_device_from_devname(struct xdev *x, const char *devname)
{
	struct xdev_device *xd;
	prop_string_t s;
	prop_dictionary_t c, a, d;
	prop_dictionary_t result_data;
	int r;
	int8_t perr;
	bool b;

	char *driver;
	char *parent;
	uint32_t unit;

	char *xml;

	if (__predict_false(x == NULL))
		return NULL;

	if (__predict_false(x->magic != XDEV_MAGIC))
		return NULL;

	if (__predict_false(devname == NULL))
		return NULL;

	c = prop_dictionary_create();
	a = prop_dictionary_create();

	s = prop_string_create_cstring_nocopy("get-properties");
	prop_dictionary_set(c, "drvctl-command", s);
	prop_object_release(s);

	s = prop_string_create_cstring(devname);
	prop_dictionary_set(a, "device-name", s);
	prop_object_release(s);

	prop_dictionary_set(c, "drvctl-arguments", a);
	prop_object_release(a);

	r = prop_dictionary_sendrecv_ioctl(c, x->drvctl_fd, DRVCTLCOMMAND, &d);
	prop_object_release(c);
	if (__predict_false(r != 0))
		return NULL;

	b = prop_dictionary_get_int8(d, "drvctl-error", &perr);
	if (__predict_false(b == false || perr != 0)) {
		prop_object_release(d);
		return NULL;
	}

	result_data = prop_dictionary_get(d, "drvctl-result-data");
	if (__predict_false(result_data == false)) {
		prop_object_release(d);
		return NULL;
	}

	b = prop_dictionary_get_cstring(result_data, "device-driver", &driver);
	if (__predict_false(b == false)) {
		prop_object_release(d);
		return NULL;
	}

	b = prop_dictionary_get_cstring(result_data, "device-parent", &parent);
	if (__predict_false(b == false)) {
		/* If missing, the node is a top-level entry in the tree. */
		parent = "";
	}

	b = prop_dictionary_get_uint32(result_data, "device-unit", &unit);
	if (__predict_false(b == false)) {
		prop_object_release(d);
		return NULL;
	}

	xml = prop_dictionary_externalize(result_data);
	if (__predict_false(xml == NULL)) {
		prop_object_release(d);
		return NULL;
	}

	xd = xdev_device_new(x, devname, driver, "???", "???", "device-attach", parent, xml, unit);
	free(xml);
	prop_object_release(d);
	return xd;
}

struct xdev_device *
xdev_device_ref(struct xdev_device *xd)
{

	if (__predict_false(xd == NULL))
		return NULL;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return NULL;

	xd->refcnt++;

	assert(xd->devname != NULL);
	assert(xd->driver != NULL);
	assert(xd->devclass != NULL);
	assert(xd->devsubclass != NULL);
	assert(xd->event != NULL);
	assert(xd->parent != NULL);
	assert(xd->xml != NULL);

	return xd;
}

struct xdev_device *
xdev_device_unref(struct xdev_device *xd)
{

	if (__predict_false(xd == NULL))
		return NULL;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return NULL;

	assert(xd->devname != NULL);
	assert(xd->driver != NULL);
	assert(xd->devclass != NULL);
	assert(xd->devsubclass != NULL);
	assert(xd->event != NULL);
	assert(xd->parent != NULL);
	assert(xd->xml != NULL);

	if (xd->refcnt == 1) {
		free(xd->devname);
		free(xd->driver);
		free(xd->devclass);
		free(xd->devsubclass);
		free(xd->event);
		free(xd->parent);
		free(xd->xml);
		free(xd);
		return NULL;
	}

	xd->refcnt--;

	return xd;
}

struct xdev *
xdev_device_get_xdev(struct xdev_device *xd)
{

	if (__predict_false(xd == NULL))
		return NULL;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return NULL;

	assert(xd->xdev->magic == XDEV_MAGIC);

	return xd->xdev;
}

int
xdev_device_get_devname(struct xdev_device *xd, const char **devname)
{

	if (__predict_false(xd == NULL))
		return -1;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return -1;

	assert(xd->devname != NULL);

	if (devname != NULL)
		*devname = xd->devname;
	return 0;
}

int
xdev_device_get_driver(struct xdev_device *xd, const char **driver)
{

	if (__predict_false(xd == NULL))
		return -1;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return -1;

	assert(xd->driver != NULL);

	if (driver != NULL)
		*driver = xd->driver;
	return 0;
}

int
xdev_device_get_devclass(struct xdev_device *xd, const char **devclass)
{

	if (__predict_false(xd == NULL))
		return -1;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return -1;

	assert(xd->devclass != NULL);

	if (devclass != NULL)
		*devclass = xd->devclass;
	return 0;
}

int
xdev_device_get_devsubclass(struct xdev_device *xd, const char **devsubclass)
{

	if (__predict_false(xd == NULL))
		return -1;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return -1;

	assert(xd->devsubclass != NULL);

	if (devsubclass != NULL)
		*devsubclass = xd->devsubclass;
	return 0;
}

int
xdev_device_get_event(struct xdev_device *xd, const char **event)
{

	if (__predict_false(xd == NULL))
		return -1;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return -1;

	assert(xd->event != NULL);

	if (event != NULL)
		*event = xd->event;
	return 0;
}

int
xdev_device_get_parent(struct xdev_device *xd, const char **parent)
{

	if (__predict_false(xd == NULL))
		return -1;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return -1;

	assert(xd->parent != NULL);

	if (parent != NULL)
		*parent = xd->parent;
	return 0;
}

int
xdev_device_get_unit(struct xdev_device *xd, uint32_t *unit)
{

	if (__predict_false(xd == NULL))
		return -1;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return -1;

	if (unit != NULL)
		*unit = xd->unit;
	return 0;
}

int
xdev_device_get_major(struct xdev_device *xd, mode_t type, devmajor_t *devmajor)
{

	if (__predict_false(xd == NULL))
		return -1;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return -1;

	if (devmajor != NULL)
		*devmajor = getdevmajor(xd->driver, type);
	return 0;
}

int
xdev_device_externalize(struct xdev_device *xd, const char **xml)
{

	if (__predict_false(xd == NULL))
		return -1;

	if (__predict_false(xd->magic != XDEV_DEVICE_MAGIC))
		return -1;

	assert(xd->xml != NULL);

	if (xml != NULL)
		*xml = xd->xml;
	return 0;
}
