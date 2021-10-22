#	$NetBSD$

USE_SHLIBDIR=	yes
.include <bsd.own.mk>

LIB=	xdev

SRCS=	xdev.c xdev_list.c xdev_device.c xdev_enumerate.c xdev_monitor.c
SRCS+=	xdev_utils.c
INCS=	xdev.h
INCSDIR=/usr/include

LDADD+= -lprop -lpthread
DPADD+= ${LIBPROP} ${LIBPTHREAD}

DBG+= -g -O0

NOMAN=	# defined

.PHONY: test-monitor
test-monitor:
	gcc -g -O0 -lxdev -I. -L. -Wl,-rpath=${.CURDIR}/ test-monitor.c -o test-monitor

test:
	gcc -g -O0 -ludev -L. -Wl,-rpath=${.CURDIR}/ udev-test.c -o udev-test

.include <bsd.lib.mk>
