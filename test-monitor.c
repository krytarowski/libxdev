#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <poll.h>
#include <xdev.h>
#include <unistd.h>

static char *Xwhat;

static int
filter(struct xdev_device *dev, void *cookie)
{
	const char *devname;
	const char *xml;

	xdev_device_get_devname(dev, &devname);
	xdev_device_externalize(dev, &xml);

	printf("!!!!!!!!!!!!!!!!!!\n");
	printf("%s\n", xml);

	if (Xwhat == NULL || strncmp(devname, Xwhat, strlen(Xwhat)) == 0)
		return 0;
	else
		return 1;

}

int
main(int argc, char **argv)
{
	if (argc == 2)
		Xwhat = argv[1];

	struct xdev *xdev = xdev_new();
	if (!xdev)
		errx(EXIT_FAILURE, "xdev_new");

	struct xdev_enumerate *enumerate = xdev_enumerate_new(xdev);
	if (!enumerate)
		errx(EXIT_FAILURE, "xdev_enumerate_new");

	int num = xdev_enumerate_scan_devices(enumerate, "", -1);
	printf("Got %d devices\n", num);
	struct xdev_list_entry *head = xdev_enumerate_get_list_entry(enumerate);
	struct xdev_list_entry *entry;
	int i = 0;
	xdev_list_entry_foreach(entry, head) {
		struct xdev_device *dev = xdev_list_entry_get_device(entry);
		const char *devname, *driver, *devclass, *devsubclass, *event, *parent;
		uint32_t unit;
		devmajor_t major;

		xdev_device_get_devname(dev, &devname);
		xdev_device_get_driver(dev, &driver);
		xdev_device_get_devclass(dev, &devclass);
		xdev_device_get_devsubclass(dev, &devsubclass);
		xdev_device_get_event(dev, &event);
		xdev_device_get_parent(dev, &parent),
		xdev_device_get_unit(dev, &unit);
		xdev_device_get_major(dev, S_IFBLK, &major);

		printf("Got Device (%d): devname=%s driver=%s devclass=%s devsubclass=%s event=%s parent=%s unit=%u major=%d\n",
			i++,
			devname, driver, devclass, devsubclass, event, parent, unit, major);

		xdev_device_unref(dev);
	}

	struct xdev_monitor *monitor;

	/* Test for 10 sec to start-stop the monitor. */
	for (size_t iii = 0; iii < 100; iii++) {
	printf(".");fflush(stdout);
	monitor = xdev_monitor_new(xdev);
	if (!monitor)
		errx(EXIT_FAILURE, "xdev_monitor_new");

	xdev_monitor_filter(monitor, filter, NULL);
	xdev_monitor_enable_receiving(monitor);
	usleep(100000);
	xdev_monitor_unref(monitor);
	}

	printf("\nMonitor tested!");

	monitor = xdev_monitor_new(xdev);
	if (!monitor)
		errx(EXIT_FAILURE, "xdev_monitor_new");

	xdev_monitor_filter(monitor, filter, NULL);
	xdev_monitor_enable_receiving(monitor);
	int fd = xdev_monitor_get_fd(monitor);

	for (;;) {
		struct pollfd pfd[1];
		pfd[0].fd = fd;
		pfd[0].events = POLLIN;
		int num_fds = poll(pfd, 1, -1);
		if (num_fds == -1)
			err(EXIT_FAILURE, "poll");
		if (pfd[0].revents & (POLLERR|POLLNVAL))
			pfd[0].fd = -1;

		if (pfd[0].fd == -1) {
			errx(EXIT_FAILURE, "poll");
		}

		struct xdev_device *dev = xdev_monitor_receive_device(monitor);
		if (dev) {
			const char *devname, *driver, *devclass, *devsubclass, *event, *parent;
			uint32_t unit;
			devmajor_t major;

			xdev_device_get_devname(dev, &devname);
			xdev_device_get_driver(dev, &driver);
			xdev_device_get_devclass(dev, &devclass);
			xdev_device_get_devsubclass(dev, &devsubclass);
			xdev_device_get_event(dev, &event);
			xdev_device_get_parent(dev, &parent),
			xdev_device_get_unit(dev, &unit);
			xdev_device_get_major(dev, S_IFBLK, &major);

			printf("Got Device: devname=%s driver=%s devclass=%s devsubclass=%s event=%s parent=%s unit=%u major=%d\n",
				devname, driver, devclass, devsubclass, event, parent, unit, major);
			xdev_device_unref(dev);
		} else {
			printf("No Device from receive_device(). An error occured.\n");
		}
	}

	xdev_monitor_unref(monitor);
	xdev_unref(xdev);

	return EXIT_SUCCESS;
}
