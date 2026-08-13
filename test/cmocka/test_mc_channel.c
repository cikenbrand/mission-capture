/******************************************************************************
    Copyright (C) 2026 by Cyberian Resources

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <mc-data/mc-channel.h>

#include <util/platform.h>
#include <util/threading.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Task 3.1. The registry is deliberately free of ports, parsers and meaning, so
 * every one of these runs headless -- no device, no window, no render loop.
 *
 * The values here are dummy on purpose: real survey systems disagree about
 * which index means what, so a fixture claiming otherwise would be testing one
 * vessel's convention rather than the registry.
 */

static mc_channel_def_t make_def(const char *name, double scale, double offset, uint64_t stale_ms)
{
	mc_channel_def_t def;
	memset(&def, 0, sizeof(def));
	strncpy(def.name, name, MC_NAME_MAX);
	def.scale = scale;
	def.offset = offset;
	def.stale_timeout_ms = stale_ms;
	return def;
}

static int setup(void **state)
{
	(void)state;
	mc_registry_clear(mc_registry_get());
	return 0;
}

/* --- names ------------------------------------------------------------- */

static void test_name_rules(void **state)
{
	(void)state;

	assert_true(mc_channel_name_valid("CP"));
	assert_true(mc_channel_name_valid("KP"));
	assert_true(mc_channel_name_valid("Water_Temp_2"));

	/* Rejected so that a format string like "Depth: {DEPTH} m" can be parsed
	 * without ambiguity later. A name containing a brace or a space would make
	 * that grammar guesswork. */
	assert_false(mc_channel_name_valid(""));
	assert_false(mc_channel_name_valid(NULL));
	assert_false(mc_channel_name_valid("has space"));
	assert_false(mc_channel_name_valid("has-dash"));
	assert_false(mc_channel_name_valid("{braced}"));
	assert_false(mc_channel_name_valid("dots.are.out"));

	char too_long[MC_NAME_MAX + 3];
	memset(too_long, 'A', sizeof(too_long) - 1);
	too_long[sizeof(too_long) - 1] = '\0';
	assert_false(mc_channel_name_valid(too_long));
}

static void test_declare_rejects_bad_names(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	const mc_channel_def_t bad = make_def("no spaces please", 1.0, 0.0, 0);
	assert_false(mc_registry_declare(reg, &bad));
	assert_int_equal(mc_registry_count(reg), 0);
}

/* --- the basic contract ------------------------------------------------- */

static void test_declared_but_unpublished_is_nodata(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	const mc_channel_def_t def = make_def("CP", 1.0, 0.0, 0);
	assert_true(mc_registry_declare(reg, &def));

	mc_value_t value;
	assert_true(mc_registry_read(reg, "CP", &value));

	/* NODATA, not a zero. A channel that has never reported is not the same
	 * as one reporting zero, and an overlay must be able to tell them apart. */
	assert_int_equal(value.quality, MC_QUALITY_NODATA);
	assert_true(isnan(value.numeric));
	assert_int_equal(value.seq, 0);
}

static void test_read_unknown_channel_fails(void **state)
{
	(void)state;
	mc_value_t value;
	assert_false(mc_registry_read(mc_registry_get(), "NEVER_DECLARED", &value));
}

static void test_publish_and_read(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	const mc_channel_def_t def = make_def("KP", 1.0, 0.0, 0);
	assert_true(mc_registry_declare(reg, &def));

	mc_registry_publish(reg, "KP", 5.132, "5.132", MC_QUALITY_GOOD);

	mc_value_t value;
	assert_true(mc_registry_read(reg, "KP", &value));
	assert_int_equal(value.quality, MC_QUALITY_GOOD);
	assert_float_equal(value.numeric, 5.132, 0.0001);
	assert_string_equal(value.text, "5.132");
	assert_int_equal(value.seq, 1);

	/* The raw token is kept alongside the number. A survey system that sends
	 * "5.1320" and one that sends "5.132" mean the same thing numerically but
	 * not on a client deliverable. */
	mc_registry_publish(reg, "KP", 6.0, "6.0000", MC_QUALITY_GOOD);
	assert_true(mc_registry_read(reg, "KP", &value));
	assert_string_equal(value.text, "6.0000");
	assert_int_equal(value.seq, 2);
}

static void test_publish_to_unmapped_name_is_ignored(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	/* Not an error. A survey string carries more fields than any one job maps,
	 * and the extras must not become failures. */
	mc_registry_publish(reg, "UNMAPPED", 1.0, "1.0", MC_QUALITY_GOOD);
	assert_int_equal(mc_registry_count(reg), 0);
}

/* --- scale and offset --------------------------------------------------- */

static void test_scale_and_offset_applied_once(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	/* Centimetres arriving, metres wanted. Done here so no overlay and no
	 * export has to remember. */
	const mc_channel_def_t def = make_def("DEPTH", 0.01, 0.0, 0);
	assert_true(mc_registry_declare(reg, &def));

	mc_registry_publish(reg, "DEPTH", 1234.0, "1234", MC_QUALITY_GOOD);

	mc_value_t value;
	assert_true(mc_registry_read(reg, "DEPTH", &value));
	assert_float_equal(value.numeric, 12.34, 0.0001);

	/* The raw token is untouched by scaling -- it is what the wire said. */
	assert_string_equal(value.text, "1234");
}

static void test_bad_quality_is_not_scaled(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	const mc_channel_def_t def = make_def("HEADING", 10.0, 5.0, 0);
	assert_true(mc_registry_declare(reg, &def));

	/* Scaling an unparseable reading would turn garbage into confident
	 * garbage. NaN is the honest answer, and the raw token survives so a
	 * reviewer can see what actually arrived. */
	mc_registry_publish(reg, "HEADING", 0.0, "not-a-number", MC_QUALITY_BAD);

	mc_value_t value;
	assert_true(mc_registry_read(reg, "HEADING", &value));
	assert_int_equal(value.quality, MC_QUALITY_BAD);
	assert_true(isnan(value.numeric));
	assert_string_equal(value.text, "not-a-number");
}

/* --- staleness ---------------------------------------------------------- */

static void test_staleness_is_computed_on_read(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	/* 20 ms, so the test spends 30 ms rather than seconds. */
	const mc_channel_def_t def = make_def("CP", 1.0, 0.0, 20);
	assert_true(mc_registry_declare(reg, &def));

	mc_registry_publish(reg, "CP", 1.031, "1.031", MC_QUALITY_GOOD);

	mc_value_t value;
	assert_true(mc_registry_read(reg, "CP", &value));
	assert_int_equal(value.quality, MC_QUALITY_GOOD);

	os_sleep_ms(40);

	/* Nothing ran in between: no timer thread, no wakeup. The reader decided.
	 * The stored value is untouched -- only its reported quality changed. */
	assert_true(mc_registry_read(reg, "CP", &value));
	assert_int_equal(value.quality, MC_QUALITY_STALE);
	assert_float_equal(value.numeric, 1.031, 0.0001);
}

static void test_zero_timeout_never_goes_stale(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	/* For a channel that legitimately updates rarely -- a vessel name, a job
	 * number -- ageing would be a false alarm. */
	const mc_channel_def_t def = make_def("VESSEL_ID", 1.0, 0.0, 0);
	assert_true(mc_registry_declare(reg, &def));

	mc_registry_publish(reg, "VESSEL_ID", 7.0, "7", MC_QUALITY_GOOD);
	os_sleep_ms(30);

	mc_value_t value;
	assert_true(mc_registry_read(reg, "VESSEL_ID", &value));
	assert_int_equal(value.quality, MC_QUALITY_GOOD);
}

/* --- redeclaration ------------------------------------------------------ */

static void test_redeclare_keeps_the_reading(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	mc_channel_def_t def = make_def("CP", 1.0, 0.0, 0);
	assert_true(mc_registry_declare(reg, &def));
	mc_registry_publish(reg, "CP", 2.0, "2.0", MC_QUALITY_GOOD);

	/* Reloading a config must not blank the overlay for a second. */
	def = make_def("CP", 2.0, 1.0, 0);
	assert_true(mc_registry_declare(reg, &def));

	assert_int_equal(mc_registry_count(reg), 1);

	mc_value_t value;
	assert_true(mc_registry_read(reg, "CP", &value));
	assert_int_equal(value.quality, MC_QUALITY_GOOD);

	/* The old reading keeps its old scaling; the new scale applies to the next
	 * publish. Retroactively rescaling a stored number would invent a reading
	 * that never arrived. */
	assert_float_equal(value.numeric, 2.0, 0.0001);

	mc_registry_publish(reg, "CP", 2.0, "2.0", MC_QUALITY_GOOD);
	assert_true(mc_registry_read(reg, "CP", &value));
	assert_float_equal(value.numeric, 5.0, 0.0001);
}

/* --- snapshot ----------------------------------------------------------- */

static void test_snapshot_returns_everything(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	/* The positional CSV the plan describes: index decides the channel, the
	 * operator decides the meaning. Names chosen arbitrarily -- the registry
	 * has no opinion about which is which. */
	const char *names[] = {"CP", "KP", "TEMP", "HEADING"};
	const double values[] = {1.031, 5.132, 6.122, 7.451};

	for (size_t i = 0; i < 4; i++) {
		const mc_channel_def_t def = make_def(names[i], 1.0, 0.0, 0);
		assert_true(mc_registry_declare(reg, &def));
		mc_registry_publish(reg, names[i], values[i], "x", MC_QUALITY_GOOD);
	}

	char out_names[8][MC_NAME_MAX + 1];
	mc_value_t out_values[8];
	const size_t written = mc_registry_snapshot(reg, out_names, out_values, 8);

	assert_int_equal(written, 4);

	/* Declaration order, so an overlay laid out once does not reshuffle
	 * itself between frames. */
	for (size_t i = 0; i < 4; i++) {
		assert_string_equal(out_names[i], names[i]);
		assert_float_equal(out_values[i].numeric, values[i], 0.0001);
	}
}

static void test_snapshot_respects_max(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	for (int i = 0; i < 5; i++) {
		char name[MC_NAME_MAX + 1];
		snprintf(name, sizeof(name), "CH_%d", i);
		const mc_channel_def_t def = make_def(name, 1.0, 0.0, 0);
		assert_true(mc_registry_declare(reg, &def));
	}

	char out_names[2][MC_NAME_MAX + 1];
	mc_value_t out_values[2];

	/* Must not write past the caller's buffer. */
	assert_int_equal(mc_registry_snapshot(reg, out_names, out_values, 2), 2);
}

/* --- capacity ----------------------------------------------------------- */

static void test_channel_cap_is_enforced(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	/* The documented ceiling is 64. Declaring past it must fail rather than
	 * scribble over the array -- a config file is operator-written and can
	 * legitimately be wrong. */
	for (int i = 0; i < 64; i++) {
		char name[MC_NAME_MAX + 1];
		snprintf(name, sizeof(name), "CH_%d", i);
		const mc_channel_def_t def = make_def(name, 1.0, 0.0, 0);
		assert_true(mc_registry_declare(reg, &def));
	}

	const mc_channel_def_t one_too_many = make_def("OVERFLOW", 1.0, 0.0, 0);
	assert_false(mc_registry_declare(reg, &one_too_many));
	assert_int_equal(mc_registry_count(reg), 64);

	/* The refusal must not have disturbed what was already there. */
	mc_value_t value;
	assert_true(mc_registry_read(reg, "CH_0", &value));
	assert_true(mc_registry_read(reg, "CH_63", &value));
	assert_false(mc_registry_read(reg, "OVERFLOW", &value));
}

/* --- threading ---------------------------------------------------------- */

#define MC_TEST_WRITERS 3
#define MC_TEST_READS 3000000

static volatile bool writers_stop = false;
static volatile long writes_done = 0;

static void *publisher_thread(void *arg)
{
	mc_registry_t *reg = arg;
	long i = 0;

	while (!writers_stop) {
		/*
		 * A long, self-describing token. The registry writes `numeric` before
		 * `text`, so an unlocked reader landing between the two sees a new
		 * number beside the previous string -- but only if the copy is wide
		 * enough to be caught mid-flight. A two-character token is copied in
		 * one instruction and would hide the bug this test exists to find.
		 */
		const int digit = (int)(i % 9) + 1;
		char text[MC_TEXT_MAX + 1];
		memset(text, '0' + digit, MC_TEXT_MAX);
		text[MC_TEXT_MAX] = '\0';

		mc_registry_publish(reg, "CP", (double)digit, text, MC_QUALITY_GOOD);
		i++;
	}

	os_atomic_inc_long(&writes_done);
	return NULL;
}

static void test_concurrent_publish_and_read(void **state)
{
	(void)state;
	mc_registry_t *reg = mc_registry_get();

	const mc_channel_def_t def = make_def("CP", 1.0, 0.0, 0);
	assert_true(mc_registry_declare(reg, &def));

	/*
	 * Writers are transport threads, readers are the graphics thread. A reader
	 * that catches a half-written value would put a number on a recording that
	 * was never measured, and nobody reviewing the footage could tell it was
	 * wrong. So the check is not "does it crash" -- it is whether every value
	 * a reader sees is internally consistent: `numeric` and `text` are written
	 * together and must be read together.
	 *
	 * Verified to be worth its runtime: with the registry mutex removed this
	 * fails within the first few thousand reads. A gentler version of this
	 * test passed against the unlocked build, which is the same as not
	 * testing it at all.
	 */
	writers_stop = false;
	writes_done = 0;

	pthread_t writers[MC_TEST_WRITERS];
	for (int i = 0; i < MC_TEST_WRITERS; i++) {
		assert_int_equal(pthread_create(&writers[i], NULL, publisher_thread, reg), 0);
	}

	long reads = 0;
	long torn = 0;

	for (long i = 0; i < MC_TEST_READS && torn == 0; i++) {
		mc_value_t value;
		assert_true(mc_registry_read(reg, "CP", &value));

		if (value.quality == MC_QUALITY_NODATA) {
			continue;
		}

		/* Every byte of the token must agree with the number beside it. */
		const char expected = (char)('0' + (int)value.numeric);
		for (size_t c = 0; c < MC_TEXT_MAX; c++) {
			if (value.text[c] != expected) {
				torn++;
				break;
			}
		}

		reads++;
	}

	writers_stop = true;
	for (int i = 0; i < MC_TEST_WRITERS; i++) {
		assert_int_equal(pthread_join(writers[i], NULL), 0);
	}

	assert_int_equal(torn, 0);

	/* Guard against the whole thing passing because it read nothing, or
	 * because the writers never got going. */
	assert_true(reads > 0);
	assert_int_equal(writes_done, MC_TEST_WRITERS);

	mc_value_t final;
	assert_true(mc_registry_read(reg, "CP", &final));
	assert_true(final.seq > 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_name_rules, setup),
		cmocka_unit_test_setup(test_declare_rejects_bad_names, setup),
		cmocka_unit_test_setup(test_declared_but_unpublished_is_nodata, setup),
		cmocka_unit_test_setup(test_read_unknown_channel_fails, setup),
		cmocka_unit_test_setup(test_publish_and_read, setup),
		cmocka_unit_test_setup(test_publish_to_unmapped_name_is_ignored, setup),
		cmocka_unit_test_setup(test_scale_and_offset_applied_once, setup),
		cmocka_unit_test_setup(test_bad_quality_is_not_scaled, setup),
		cmocka_unit_test_setup(test_staleness_is_computed_on_read, setup),
		cmocka_unit_test_setup(test_zero_timeout_never_goes_stale, setup),
		cmocka_unit_test_setup(test_redeclare_keeps_the_reading, setup),
		cmocka_unit_test_setup(test_snapshot_returns_everything, setup),
		cmocka_unit_test_setup(test_snapshot_respects_max, setup),
		cmocka_unit_test_setup(test_channel_cap_is_enforced, setup),
		cmocka_unit_test_setup(test_concurrent_publish_and_read, setup),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
