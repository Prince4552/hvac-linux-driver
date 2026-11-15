// SPDX-License-Identifier: MIT
/*
 * watchdog: open, kick and cleanly close a Linux watchdog device through its standard
 * interface (<linux/watchdog.h>): WDIOC_SETTIMEOUT, WDIOC_KEEPALIVE and the magic-close 'V'.
 */
#include "watchdog.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/watchdog.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define WDT_MAGIC_CLOSE 'V'

void wdt_init(wdt_t *w)
{
	w->fd = -1;
}

int wdt_open(wdt_t *w, const char *path, uint32_t timeout_s)
{
	int timeout = (int)timeout_s;	/* range-checked below, fits an int */

	if (w == NULL || path == NULL || timeout_s < WDT_TIMEOUT_MIN_S || timeout_s > WDT_TIMEOUT_MAX_S)
		return -EINVAL;
	w->fd = open(path, O_WRONLY | O_CLOEXEC);
	if (w->fd < 0) {
		int err = -errno;

		w->fd = -1;
		return err;
	}
	if (ioctl(w->fd, WDIOC_SETTIMEOUT, &timeout) != 0) {
		int err = -errno;

		(void)wdt_close_clean(w);	/* do not leave an armed watchdog behind */
		return err;
	}
	return 0;
}

int wdt_kick(wdt_t *w)
{
	int dummy = 0;

	if (w == NULL || w->fd < 0)
		return -EBADF;
	return ioctl(w->fd, WDIOC_KEEPALIVE, &dummy) == 0 ? 0 : -errno;
}

int wdt_close_clean(wdt_t *w)
{
	const char magic = WDT_MAGIC_CLOSE;
	int err = 0;

	if (w == NULL || w->fd < 0)
		return 0;
	if (write(w->fd, &magic, 1) != 1)
		err = -errno;
	if (close(w->fd) != 0 && err == 0)
		err = -errno;
	w->fd = -1;
	return err;
}
