// SPDX-License-Identifier: MIT
/*
 * Unit tests for the watchdog wrapper (Ext 1) without a real watchdog: argument checks, a missing
 * device, a file that is not a watchdog (the timeout ioctl fails and nothing stays open), and safe
 * kick/close on a closed handle. The real expiry behavior is tested in the guest with softdog.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "unity.h"
#include "watchdog.h"

static wdt_t w;

void setUp(void)
{
	wdt_init(&w);
}

void tearDown(void)
{
	TEST_ASSERT_EQUAL_INT(0, wdt_close_clean(&w));
}

static void test_bad_arguments_are_rejected(void)
{
	TEST_ASSERT_EQUAL_INT(-EINVAL, wdt_open(NULL, "/dev/watchdog", 10));
	TEST_ASSERT_EQUAL_INT(-EINVAL, wdt_open(&w, NULL, 10));
	TEST_ASSERT_EQUAL_INT(-EINVAL, wdt_open(&w, "/dev/watchdog", WDT_TIMEOUT_MIN_S - 1u));
	TEST_ASSERT_EQUAL_INT(-EINVAL, wdt_open(&w, "/dev/watchdog", WDT_TIMEOUT_MAX_S + 1u));
	TEST_ASSERT_EQUAL_INT(-1, w.fd);
}

static void test_missing_device_reports_enoent(void)
{
	TEST_ASSERT_EQUAL_INT(-ENOENT, wdt_open(&w, "/nonexistent/watchdog", 10));
	TEST_ASSERT_EQUAL_INT(-1, w.fd);
}

static void test_non_watchdog_file_fails_and_is_closed(void)
{
	char path[] = "/tmp/hvac_test_wdt_XXXXXX";
	int fd = mkstemp(path);
	int ret = 0;

	TEST_ASSERT_TRUE(fd >= 0);
	TEST_ASSERT_EQUAL_INT(0, close(fd));
	ret = wdt_open(&w, path, 10);
	TEST_ASSERT_EQUAL_INT(-ENOTTY, ret);	/* a regular file does not understand the ioctl */
	TEST_ASSERT_EQUAL_INT(-1, w.fd);
	TEST_ASSERT_EQUAL_INT(0, unlink(path));
}

static void test_kick_and_close_on_closed_handle_are_safe(void)
{
	TEST_ASSERT_EQUAL_INT(-EBADF, wdt_kick(&w));
	TEST_ASSERT_EQUAL_INT(-EBADF, wdt_kick(NULL));
	TEST_ASSERT_EQUAL_INT(0, wdt_close_clean(&w));
	TEST_ASSERT_EQUAL_INT(0, wdt_close_clean(NULL));
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_bad_arguments_are_rejected);
	RUN_TEST(test_missing_device_reports_enoent);
	RUN_TEST(test_non_watchdog_file_fails_and_is_closed);
	RUN_TEST(test_kick_and_close_on_closed_handle_are_safe);
	return UNITY_END();
}
