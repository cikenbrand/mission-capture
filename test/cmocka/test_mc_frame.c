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

#include <mc-data/mc-frame.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>

#include <stdio.h>
#include <string.h>

/*
 * Task 3.2.
 *
 * The plan calls for feeding byte streams "chopped at every possible boundary"
 * and asserting identical framing regardless of chunking, because that is where
 * this class of bug lives: a delimiter that happens to land on a read boundary,
 * a sentinel split across two reads, a partial frame that survives one call but
 * not two.
 *
 * So the short streams here really are chopped every possible way -- all
 * 2^(n-1) splits -- and the longer ones at every fixed chunk size. A failure
 * reports the exact split that produced it, because "one of 131072 chunkings
 * failed" is not a debuggable statement.
 */

#define MAX_FRAMES 16
#define MAX_FRAME_BYTES 128

struct collector {
	uint8_t frames[MAX_FRAMES][MAX_FRAME_BYTES];
	size_t lens[MAX_FRAMES];
	size_t count;
	bool overflowed;
};

static void collect(void *ctx, const uint8_t *frame, size_t len)
{
	struct collector *c = ctx;

	if (c->count >= MAX_FRAMES || len > MAX_FRAME_BYTES) {
		c->overflowed = true;
		return;
	}

	memcpy(c->frames[c->count], frame, len);
	c->lens[c->count] = len;
	c->count++;
}

/* Describes a chunking as "1,3,2" so a failure names the split that broke. */
static void describe(const size_t *chunks, size_t n, char *out, size_t out_size)
{
	size_t used = 0;
	out[0] = '\0';

	for (size_t i = 0; i < n && used + 8 < out_size; i++) {
		used += (size_t)snprintf(out + used, out_size - used, i ? ",%zu" : "%zu", chunks[i]);
	}
}

static void feed(mc_frame_assembler_t *assembler, const char *stream, const size_t *chunks, size_t chunk_count,
		 struct collector *c)
{
	size_t offset = 0;

	for (size_t i = 0; i < chunk_count; i++) {
		mc_frame_push(assembler, (const uint8_t *)stream + offset, chunks[i], 0, collect, c);
		offset += chunks[i];
	}
}

/* Compares against the expected frames; returns NULL on a match, or a
 * description of the first difference. */
static const char *compare(const struct collector *c, const char *const *expected, size_t expected_count)
{
	static char reason[256];

	if (c->overflowed) {
		snprintf(reason, sizeof(reason), "collector overflowed");
		return reason;
	}

	if (c->count != expected_count) {
		snprintf(reason, sizeof(reason), "got %zu frames, expected %zu", c->count, expected_count);
		return reason;
	}

	for (size_t i = 0; i < expected_count; i++) {
		const size_t want = strlen(expected[i]);

		if (c->lens[i] != want || memcmp(c->frames[i], expected[i], want) != 0) {
			snprintf(reason, sizeof(reason), "frame %zu was \"%.*s\", expected \"%s\"", i, (int)c->lens[i],
				 (const char *)c->frames[i], expected[i]);
			return reason;
		}
	}

	return NULL;
}

/*
 * Every one of the 2^(n-1) ways to split the stream. Bit i of the mask means
 * "end a read after byte i".
 */
static void assert_framing_every_split(const mc_frame_config_t *config, const char *stream, const char *const *expected,
				       size_t expected_count)
{
	const size_t n = strlen(stream);

	/* 2^19 runs is already a second of work; a longer stream belongs in
	 * assert_framing_every_chunk_size instead. */
	assert_true(n >= 1 && n <= 20);

	const uint32_t splits = 1u << (n - 1);

	for (uint32_t mask = 0; mask < splits; mask++) {
		size_t chunks[24];
		size_t chunk_count = 0;
		size_t run = 0;

		for (size_t i = 0; i < n; i++) {
			run++;
			if (i == n - 1 || (mask >> i) & 1u) {
				chunks[chunk_count++] = run;
				run = 0;
			}
		}

		struct collector c;
		c.count = 0;
		c.overflowed = false;

		mc_frame_assembler_t *assembler = mc_frame_create(config);
		assert_non_null(assembler);
		feed(assembler, stream, chunks, chunk_count, &c);
		mc_frame_destroy(assembler);

		const char *reason = compare(&c, expected, expected_count);
		if (reason) {
			char split[128];
			describe(chunks, chunk_count, split, sizeof(split));
			fail_msg("split [%s]: %s", split, reason);
		}
	}
}

/* Every fixed chunk size from 1 byte to the whole stream at once. For streams
 * too long to split exhaustively. */
static void assert_framing_every_chunk_size(const mc_frame_config_t *config, const char *stream,
					    const char *const *expected, size_t expected_count)
{
	const size_t n = strlen(stream);

	for (size_t size = 1; size <= n; size++) {
		size_t chunks[512];
		size_t chunk_count = 0;

		for (size_t remaining = n; remaining > 0;) {
			const size_t take = remaining < size ? remaining : size;
			chunks[chunk_count++] = take;
			remaining -= take;
		}

		struct collector c;
		c.count = 0;
		c.overflowed = false;

		mc_frame_assembler_t *assembler = mc_frame_create(config);
		assert_non_null(assembler);
		feed(assembler, stream, chunks, chunk_count, &c);
		mc_frame_destroy(assembler);

		const char *reason = compare(&c, expected, expected_count);
		if (reason) {
			fail_msg("chunk size %zu: %s", size, reason);
		}
	}
}

static mc_frame_config_t delimiter_config(const char *delimiter)
{
	mc_frame_config_t config;
	memset(&config, 0, sizeof(config));
	config.mode = MC_FRAME_DELIMITER;
	config.delimiter_len = strlen(delimiter);
	memcpy(config.delimiter, delimiter, config.delimiter_len);
	return config;
}

/* --- configuration ------------------------------------------------------ */

static void test_bad_config_is_refused(void **state)
{
	(void)state;

	/* Refused at create time rather than silently never framing at sea. */
	mc_frame_config_t config = delimiter_config("");
	assert_null(mc_frame_create(&config));

	config = delimiter_config("\n");
	config.mode = MC_FRAME_FIXED;
	config.fixed_length = 0;
	assert_null(mc_frame_create(&config));

	config.fixed_length = MC_FRAME_MAX + 1;
	assert_null(mc_frame_create(&config));

	memset(&config, 0, sizeof(config));
	config.mode = MC_FRAME_SENTINEL;
	config.start_sentinel = '$';
	config.end_sentinel = '$'; /* would end the frame it just started */
	assert_null(mc_frame_create(&config));

	memset(&config, 0, sizeof(config));
	config.mode = MC_FRAME_IDLE;
	config.idle_timeout_ms = 0;
	assert_null(mc_frame_create(&config));

	assert_null(mc_frame_create(NULL));
}

/* --- delimiter ---------------------------------------------------------- */

static void test_delimiter_every_split(void **state)
{
	(void)state;

	const mc_frame_config_t config = delimiter_config("\n");
	const char *const expected[] = {"AB", "CD", "EF"};

	assert_framing_every_split(&config, "AB\nCD\nEF\n", expected, 3);
}

static void test_multi_byte_delimiter_every_split(void **state)
{
	(void)state;

	/* The interesting case: a split landing between the \r and the \n. */
	const mc_frame_config_t config = delimiter_config("\r\n");
	const char *const expected[] = {"AB", "CD"};

	assert_framing_every_split(&config, "AB\r\nCD\r\n", expected, 2);
}

static void test_trailing_partial_is_withheld(void **state)
{
	(void)state;

	/* "CD" has no delimiter yet, so it is not a frame yet. Emitting it early
	 * would hand the parser half a survey string. */
	const mc_frame_config_t config = delimiter_config("\n");
	const char *const expected[] = {"AB"};

	assert_framing_every_split(&config, "AB\nCD", expected, 1);
}

static void test_blank_lines_are_not_frames(void **state)
{
	(void)state;

	/* A blank line carries no reading; passing it on becomes a row of BAD
	 * values on screen. */
	const mc_frame_config_t config = delimiter_config("\n");
	const char *const expected[] = {"AB", "CD"};

	assert_framing_every_split(&config, "AB\n\nCD\n", expected, 2);
}

static void test_crlf_device_configured_for_lf(void **state)
{
	(void)state;

	/*
	 * Documents a real trap rather than hiding it. A device sending CRLF, with
	 * the operator having configured "\n", yields frames with a trailing \r.
	 * The assembler stays literal about framing on purpose -- stripping bytes
	 * it was not told about would corrupt fixed-length binary data -- so it is
	 * the parser (3.3) that must trim.
	 */
	const mc_frame_config_t config = delimiter_config("\n");
	const char *const expected[] = {"AB\r", "CD\r"};

	assert_framing_every_split(&config, "AB\r\nCD\r\n", expected, 2);
}

/* --- length guard ------------------------------------------------------- */

static void test_overlong_frame_is_dropped_and_resyncs(void **state)
{
	(void)state;

	mc_frame_config_t config = delimiter_config("\n");
	config.max_frame_length = 8;

	/* The frame in the middle never ends. What matters is that the assembler
	 * comes back: a wrong baud rate produces exactly this, and the next good
	 * frame must still be delivered. */
	const char *const expected[] = {"SHORT", "OK"};

	assert_framing_every_chunk_size(&config, "SHORT\nWAYTOOLONGFRAME\nOK\n", expected, 2);

	/* And it is counted, because a climbing drop count is how a wrong baud
	 * rate looks from the health panel. */
	struct collector c;
	c.count = 0;
	c.overflowed = false;

	mc_frame_assembler_t *assembler = mc_frame_create(&config);
	assert_non_null(assembler);
	mc_frame_push(assembler, (const uint8_t *)"SHORT\nWAYTOOLONGFRAME\nOK\n", 25, 0, collect, &c);

	assert_int_equal(mc_frame_count(assembler), 2);
	assert_int_equal(mc_frame_dropped(assembler), 1);

	mc_frame_destroy(assembler);
}

static void test_endless_garbage_cannot_grow_the_buffer(void **state)
{
	(void)state;

	mc_frame_config_t config = delimiter_config("\n");
	config.max_frame_length = 16;

	struct collector c;
	c.count = 0;
	c.overflowed = false;

	mc_frame_assembler_t *assembler = mc_frame_create(&config);
	assert_non_null(assembler);

	/* A device with the wrong framing spews forever. Far more bytes than the
	 * buffer could hold, and none of them a delimiter. */
	uint8_t junk[256];
	memset(junk, 'X', sizeof(junk));
	for (int i = 0; i < 100; i++) {
		mc_frame_push(assembler, junk, sizeof(junk), 0, collect, &c);
	}

	assert_int_equal(c.count, 0);
	assert_true(mc_frame_dropped(assembler) > 0);

	/* Still alive: one delimiter and it is back in business. */
	mc_frame_push(assembler, (const uint8_t *)"\nGOOD\n", 6, 0, collect, &c);
	assert_int_equal(c.count, 1);
	assert_memory_equal(c.frames[0], "GOOD", 4);

	mc_frame_destroy(assembler);
}

/* --- sentinel ----------------------------------------------------------- */

static void test_sentinel_skips_garbage_prefix(void **state)
{
	(void)state;

	mc_frame_config_t config;
	memset(&config, 0, sizeof(config));
	config.mode = MC_FRAME_SENTINEL;
	config.start_sentinel = '$';
	config.end_sentinel = '*';

	/* Whatever was already in the driver buffer when the port opened is not a
	 * frame, and neither is whatever trails the last complete one. */
	const char *const expected[] = {"1.031,5.13"};

	assert_framing_every_split(&config, "junk$1.031,5.13*x", expected, 1);
}

static void test_sentinel_restart_drops_the_partial(void **state)
{
	(void)state;

	mc_frame_config_t config;
	memset(&config, 0, sizeof(config));
	config.mode = MC_FRAME_SENTINEL;
	config.start_sentinel = '$';
	config.end_sentinel = '*';

	/* A second start before the first ended: the device reset, or bytes were
	 * lost. Resynchronise on the new start rather than waiting for an end byte
	 * that is never coming. */
	const char *const expected[] = {"CD"};

	assert_framing_every_split(&config, "$AB$CD*", expected, 1);

	struct collector c;
	c.count = 0;
	c.overflowed = false;

	mc_frame_assembler_t *assembler = mc_frame_create(&config);
	assert_non_null(assembler);
	mc_frame_push(assembler, (const uint8_t *)"$AB$CD*", 7, 0, collect, &c);

	assert_int_equal(mc_frame_count(assembler), 1);
	assert_int_equal(mc_frame_dropped(assembler), 1);

	mc_frame_destroy(assembler);
}

/* --- fixed length ------------------------------------------------------- */

static void test_fixed_length_every_split(void **state)
{
	(void)state;

	mc_frame_config_t config;
	memset(&config, 0, sizeof(config));
	config.mode = MC_FRAME_FIXED;
	config.fixed_length = 4;

	const char *const expected[] = {"ABCD", "EFGH"};

	assert_framing_every_split(&config, "ABCDEFGH", expected, 2);
}

/* --- idle timeout ------------------------------------------------------- */

static void test_idle_timeout_ends_a_frame(void **state)
{
	(void)state;

	mc_frame_config_t config;
	memset(&config, 0, sizeof(config));
	config.mode = MC_FRAME_IDLE;
	config.idle_timeout_ms = 50;

	struct collector c;
	c.count = 0;
	c.overflowed = false;

	mc_frame_assembler_t *assembler = mc_frame_create(&config);
	assert_non_null(assembler);

	const uint64_t ms = 1000000ULL;

	/* Time is an argument, not a clock read, so this is exact and instant. */
	mc_frame_push(assembler, (const uint8_t *)"1.031", 5, 100 * ms, collect, &c);
	assert_int_equal(c.count, 0);

	mc_frame_tick(assembler, 120 * ms, collect, &c); /* only 20 ms of quiet */
	assert_int_equal(c.count, 0);

	mc_frame_push(assembler, (const uint8_t *)",5.132", 6, 130 * ms, collect, &c);
	mc_frame_tick(assembler, 170 * ms, collect, &c); /* 40 ms since last byte */
	assert_int_equal(c.count, 0);

	mc_frame_tick(assembler, 200 * ms, collect, &c); /* 70 ms: the line went quiet */
	assert_int_equal(c.count, 1);
	assert_int_equal(c.lens[0], 11);
	assert_memory_equal(c.frames[0], "1.031,5.132", 11);

	/* Nothing pending, so further quiet produces nothing. */
	mc_frame_tick(assembler, 400 * ms, collect, &c);
	assert_int_equal(c.count, 1);

	mc_frame_destroy(assembler);
}

static void test_tick_is_harmless_in_other_modes(void **state)
{
	(void)state;

	/* A transport should be able to call tick on every poll without caring
	 * which mode its port is in. */
	const mc_frame_config_t config = delimiter_config("\n");

	struct collector c;
	c.count = 0;
	c.overflowed = false;

	mc_frame_assembler_t *assembler = mc_frame_create(&config);
	assert_non_null(assembler);

	mc_frame_push(assembler, (const uint8_t *)"PARTIAL", 7, 0, collect, &c);
	mc_frame_tick(assembler, 999999999ULL, collect, &c);

	assert_int_equal(c.count, 0);

	mc_frame_destroy(assembler);
}

/* --- reset -------------------------------------------------------------- */

static void test_reset_drops_the_partial(void **state)
{
	(void)state;

	const mc_frame_config_t config = delimiter_config("\n");

	struct collector c;
	c.count = 0;
	c.overflowed = false;

	mc_frame_assembler_t *assembler = mc_frame_create(&config);
	assert_non_null(assembler);

	mc_frame_push(assembler, (const uint8_t *)"STALE", 5, 0, collect, &c);

	/* The port was reopened; those bytes predate the reconnect and must not be
	 * glued to whatever arrives next. */
	mc_frame_reset(assembler);
	mc_frame_push(assembler, (const uint8_t *)"FRESH\n", 6, 0, collect, &c);

	assert_int_equal(c.count, 1);
	assert_int_equal(c.lens[0], 5);
	assert_memory_equal(c.frames[0], "FRESH", 5);

	mc_frame_destroy(assembler);
}

/* --- the shape this actually has to carry ------------------------------- */

static void test_positional_csv_stream(void **state)
{
	(void)state;

	/*
	 * The format the operator described: positional comma-separated floats,
	 * CRLF terminated. Meaning is assigned by index later (3.3) -- the
	 * assembler neither knows nor cares what these numbers are.
	 */
	const mc_frame_config_t config = delimiter_config("\r\n");
	const char *const expected[] = {
		"1.031,5.132,6.122,7.451",
		"1.032,5.133,6.121,7.450",
		"1.033,5.134,6.120,7.449",
	};

	assert_framing_every_chunk_size(&config,
					"1.031,5.132,6.122,7.451\r\n"
					"1.032,5.133,6.121,7.450\r\n"
					"1.033,5.134,6.120,7.449\r\n",
					expected, 3);
}

static void test_mid_stream_join_does_not_corrupt_what_follows(void **state)
{
	(void)state;

	/*
	 * What a reconnect looks like from the assembler's side: the stream is
	 * joined partway through a frame.
	 *
	 * The leading fragment IS delivered, and that is not a bug to fix here.
	 * It arrived properly delimiter-terminated, and nothing at this layer can
	 * distinguish a truncated first line from a short real one -- that is
	 * inherent to delimiter framing, not an oversight.
	 *
	 * What matters, and what this asserts, is that the fragment does not
	 * corrupt what follows: it is emitted as its own frame rather than glued
	 * to the next one. Rejecting it is the parser's job, by field count
	 * against the configured channel map (OI-64).
	 */
	const mc_frame_config_t config = delimiter_config("\r\n");
	const char *const expected[] = {"22,6.122,7.451", "1.032,5.133,6.121,7.450"};

	assert_framing_every_chunk_size(&config, "22,6.122,7.451\r\n1.032,5.133,6.121,7.450\r\n", expected, 2);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_bad_config_is_refused),
		cmocka_unit_test(test_delimiter_every_split),
		cmocka_unit_test(test_multi_byte_delimiter_every_split),
		cmocka_unit_test(test_trailing_partial_is_withheld),
		cmocka_unit_test(test_blank_lines_are_not_frames),
		cmocka_unit_test(test_crlf_device_configured_for_lf),
		cmocka_unit_test(test_overlong_frame_is_dropped_and_resyncs),
		cmocka_unit_test(test_endless_garbage_cannot_grow_the_buffer),
		cmocka_unit_test(test_sentinel_skips_garbage_prefix),
		cmocka_unit_test(test_sentinel_restart_drops_the_partial),
		cmocka_unit_test(test_fixed_length_every_split),
		cmocka_unit_test(test_idle_timeout_ends_a_frame),
		cmocka_unit_test(test_tick_is_harmless_in_other_modes),
		cmocka_unit_test(test_reset_drops_the_partial),
		cmocka_unit_test(test_positional_csv_stream),
		cmocka_unit_test(test_mid_stream_join_does_not_corrupt_what_follows),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
