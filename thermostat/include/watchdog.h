/* SPDX-License-Identifier: MIT */
/*
 * watchdog: hvacd's use of the Linux watchdog device (/dev/watchdog). Once opened, the kernel
 * resets the system unless the daemon "kicks" it at least every timeout_s seconds. Closing with the
 * magic character 'V' disarms it, so a clean shutdown does not trigger a reset (Ext 1).
 */
#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

#define WDT_TIMEOUT_MIN_S 2u
#define WDT_TIMEOUT_MAX_S 60u

typedef struct {
	int fd;		/* -1 when closed */
} wdt_t;

/** Marks the handle closed; call before anything else so wdt_close_clean() is always safe. */
void wdt_init(wdt_t *w);

/** Opens the device and sets the timeout (WDIOC_SETTIMEOUT). Returns 0 or a negative errno. */
int wdt_open(wdt_t *w, const char *path, uint32_t timeout_s);

/** Proves the daemon is alive (WDIOC_KEEPALIVE). Returns 0 or a negative errno. */
int wdt_kick(wdt_t *w);

/** Writes the magic 'V' and closes, so the kernel disarms the timer. Safe on a closed handle. */
int wdt_close_clean(wdt_t *w);

#endif /* WATCHDOG_H */
