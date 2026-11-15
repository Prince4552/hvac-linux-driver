// SPDX-License-Identifier: GPL-2.0-only
/*
 * KUnit tests for the pure conversion functions in hvac_temp_conv.h (suite "hvac_temp_conv").
 * Built as its own module, hvac_temp_kunit.ko; loading it runs the suite, and the results appear
 * in /sys/kernel/debug/kunit/hvac_temp_conv/results.
 */
#include <kunit/test.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "hvac_temp_conv.h"

struct hvac_temp_ref {
	int mc;		/* millidegrees */
	int raw;	/* raw steps */
	u16 reg;	/* chip register value */
};

/* Reference table from PROJECT_PLAN.md section 4.3. */
static const struct hvac_temp_ref hvac_temp_refs[] = {
	{ 125000, 2000, 0x7D00 },
	{ 25000, 400, 0x1900 },
	{ 63, 1, 0x0010 },	/* +0.0625 C = 62.5 m°C, rounded to 63 */
	{ 0, 0, 0x0000 },
	{ -63, -1, 0xFFF0 },
	{ -25000, -400, 0xE700 },
	{ -40000, -640, 0xD800 },
};

static void hvac_temp_conv_reference_table(struct kunit *test)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(hvac_temp_refs); i++) {
		const struct hvac_temp_ref *r = &hvac_temp_refs[i];

		KUNIT_EXPECT_EQ(test, hvac_temp_reg_to_raw(r->reg), r->raw);
		KUNIT_EXPECT_EQ(test, hvac_temp_raw_to_reg(r->raw), r->reg);
		KUNIT_EXPECT_EQ(test, hvac_temp_raw_to_mc(r->raw), r->mc);
		KUNIT_EXPECT_EQ(test, hvac_temp_mc_to_raw(r->mc), r->raw);
	}
}

static void hvac_temp_conv_all_raw_round_trip(struct kunit *test)
{
	int raw;

	for (raw = -2048; raw <= 2047; raw++)
		KUNIT_EXPECT_EQ(test, hvac_temp_reg_to_raw(hvac_temp_raw_to_reg(raw)), raw);
}

static void hvac_temp_conv_low_bits_are_ignored(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, hvac_temp_reg_to_raw(0x190F), 400);
	KUNIT_EXPECT_EQ(test, hvac_temp_reg_to_raw(0xFFFF), -1);
}

static void hvac_temp_conv_rounding(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, hvac_temp_mc_to_raw(31), 0);
	KUNIT_EXPECT_EQ(test, hvac_temp_mc_to_raw(32), 1);
	KUNIT_EXPECT_EQ(test, hvac_temp_mc_to_raw(-31), 0);
	KUNIT_EXPECT_EQ(test, hvac_temp_mc_to_raw(-32), -1);
	KUNIT_EXPECT_EQ(test, hvac_temp_raw_to_mc(1), 63);
	KUNIT_EXPECT_EQ(test, hvac_temp_raw_to_mc(-1), -63);
}

static void hvac_temp_conv_extremes(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, hvac_temp_mc_to_raw(HVAC_TEMP_MIN_MC), -640);
	KUNIT_EXPECT_EQ(test, hvac_temp_mc_to_raw(HVAC_TEMP_MAX_MC), 2000);
	KUNIT_EXPECT_EQ(test, hvac_temp_raw_to_mc(-640), HVAC_TEMP_MIN_MC);
	KUNIT_EXPECT_EQ(test, hvac_temp_raw_to_mc(2000), HVAC_TEMP_MAX_MC);
	KUNIT_EXPECT_EQ(test, hvac_temp_reg_to_raw(0x7FF0), 2047);
	KUNIT_EXPECT_EQ(test, hvac_temp_reg_to_raw(0x8000), -2048);
}

static struct kunit_case hvac_temp_conv_cases[] = {
	KUNIT_CASE(hvac_temp_conv_reference_table),
	KUNIT_CASE(hvac_temp_conv_all_raw_round_trip),
	KUNIT_CASE(hvac_temp_conv_low_bits_are_ignored),
	KUNIT_CASE(hvac_temp_conv_rounding),
	KUNIT_CASE(hvac_temp_conv_extremes),
	{}
};

static struct kunit_suite hvac_temp_conv_suite = {
	.name = "hvac_temp_conv",
	.test_cases = hvac_temp_conv_cases,
};
kunit_test_suite(hvac_temp_conv_suite);

MODULE_DESCRIPTION("KUnit tests for hvac_temp conversion functions");
MODULE_LICENSE("GPL");
