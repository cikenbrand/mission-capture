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

#include <mc-data/mc-sim.h>
#include <mc-data/mc-frame.h>
#include <mc-data/mc-parser.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Task 3.5.
 *
 * The fixtures here are a real capture, not invented data: 32 lines taken off
 * an ESP32 survey simulator on COM3 at 115200 baud, five positional
 * comma-separated floats, CRLF terminated, nominally 1 Hz.
 *
 * That matters. "Nominally 1 Hz" is 972 to 1038 ms in practice, and the timed
 * fixture keeps that jitter. Code that has only ever seen exact intervals has
 * not been tested against anything real.
 *
 * The last test drives that capture through the whole chain -- simulator to
 * frame assembler to parser to registry -- which is the first end-to-end proof
 * in this phase that the pieces fit together on data nobody wrote by hand.
 */

#ifndef MC_FIXTURE_DIR
#define MC_FIXTURE_DIR "."
#endif

#define MS 1000000ULL

static const char *fixture(const char *name)
{
	static char path[512];
	snprintf(path, sizeof(path), "%s/%s", MC_FIXTURE_DIR, name);
	return path;
}

static mc_sim_config_t file_config(const char *name, double rate_hz)
{
	mc_sim_config_t cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.mode = MC_SIM_FILE;
	cfg.path = fixture(name);
	cfg.rate_hz = rate_hz;
	return cfg;
}

/*
 * Creates a simulator and starts its clock at t=0.
 *
 * The epoch is set by the first read, exactly as a real transport's would be by
 * the moment the port opens -- so a test that jumped straight to t=10s without
 * this would set the epoch there and correctly receive nothing.
 */
static mc_sim_t *make_sim(const mc_sim_config_t *cfg)
{
	mc_sim_t *sim = mc_sim_create(cfg);
	if (sim) {
		uint8_t discard[1];
		assert_int_equal(mc_sim_read(sim, 0, discard, sizeof(discard)), 0);
	}
	return sim;
}

/* Collects everything the simulator produces between two instants. */
static size_t drain(mc_sim_t *sim, uint64_t now_ns, uint8_t *out, size_t out_size)
{
	size_t total = 0;
	size_t n;

	while (total < out_size && (n = mc_sim_read(sim, now_ns, out + total, out_size - total)) > 0) {
		total += n;
	}

	return total;
}

/* --- configuration ------------------------------------------------------ */

static void test_bad_config_is_refused(void **state)
{
	(void)state;

	assert_null(mc_sim_create(NULL));

	mc_sim_config_t cfg = file_config("esp32-survey.txt", 0.0);
	assert_null(mc_sim_create(&cfg)); /* no rate and no timestamps */

	cfg = file_config("no-such-file.txt", 10.0);
	assert_null(mc_sim_create(&cfg));

	memset(&cfg, 0, sizeof(cfg));
	cfg.mode = MC_SIM_SYNTHETIC;
	cfg.rate_hz = 10.0;
	cfg.channel_count = 0;
	assert_null(mc_sim_create(&cfg));
}

/* --- file playback ------------------------------------------------------ */

static void test_file_playback_at_a_fixed_rate(void **state)
{
	(void)state;

	mc_sim_config_t cfg = file_config("esp32-survey.txt", 10.0);
	mc_sim_t *sim = mc_sim_create(&cfg);
	assert_non_null(sim);

	uint8_t buf[4096];

	/* Nothing is due at t=0; the first line lands one interval in. */
	assert_int_equal(mc_sim_read(sim, 0, buf, sizeof(buf)), 0);

	const size_t n = drain(sim, 100 * MS, buf, sizeof(buf));
	assert_true(n > 0);

	/* One line, terminator included, and it is the real first line of the
	 * capture rather than something this test made up. */
	buf[n] = '\0';
	assert_memory_equal(buf + n - 2, "\r\n", 2);
	assert_non_null(strstr((const char *)buf, "456122.81"));
	assert_int_equal(mc_sim_lines(sim), 1);

	mc_sim_destroy(sim);
}

static void test_file_playback_stops_when_not_looping(void **state)
{
	(void)state;

	mc_sim_config_t cfg = file_config("esp32-survey.txt", 1000.0);
	mc_sim_t *sim = make_sim(&cfg);
	assert_non_null(sim);

	uint8_t buf[8192];
	drain(sim, 10000 * MS, buf, sizeof(buf));

	assert_int_equal(mc_sim_lines(sim), 32);
	assert_true(mc_sim_exhausted(sim));

	/* And stays exhausted rather than starting over. */
	assert_int_equal(mc_sim_read(sim, 20000 * MS, buf, sizeof(buf)), 0);

	mc_sim_destroy(sim);
}

static void test_file_playback_loops(void **state)
{
	(void)state;

	mc_sim_config_t cfg = file_config("esp32-survey.txt", 1000.0);
	cfg.loop = true;

	mc_sim_t *sim = make_sim(&cfg);
	assert_non_null(sim);

	uint8_t buf[16384];
	drain(sim, 10000 * MS, buf, sizeof(buf));

	/* A test rig has to be able to run longer than the fixture. */
	assert_true(mc_sim_lines(sim) > 32);
	assert_false(mc_sim_exhausted(sim));

	mc_sim_destroy(sim);
}

static void test_timestamped_playback_keeps_the_jitter(void **state)
{
	(void)state;

	mc_sim_config_t cfg = file_config("esp32-survey.timed.txt", 0.0);
	cfg.honour_timestamps = true;

	mc_sim_t *sim = make_sim(&cfg);
	assert_non_null(sim);

	uint8_t buf[4096];

	/* The first captured gap was 844 ms, so nothing is due at 800 ms. A rig
	 * that rounded everything to exactly 1 Hz would already have fired. */
	assert_int_equal(mc_sim_read(sim, 800 * MS, buf, sizeof(buf)), 0);

	const size_t n = drain(sim, 900 * MS, buf, sizeof(buf));
	assert_true(n > 0);
	assert_int_equal(mc_sim_lines(sim), 1);

	/* The delta prefix is consumed, not replayed as data. */
	buf[n] = '\0';
	assert_null(strstr((const char *)buf, "844,"));
	assert_non_null(strstr((const char *)buf, "456122.81"));

	mc_sim_destroy(sim);
}

static void test_small_buffer_changes_chunking_not_content(void **state)
{
	(void)state;

	uint8_t whole[8192];
	uint8_t piecemeal[8192];

	mc_sim_config_t cfg = file_config("esp32-survey.txt", 1000.0);

	mc_sim_t *a = make_sim(&cfg);
	assert_non_null(a);
	const size_t n_whole = drain(a, 10000 * MS, whole, sizeof(whole));
	mc_sim_destroy(a);

	/* Three bytes at a time, which is what a badly-behaved driver looks like
	 * and exactly what the frame assembler was built to survive. */
	mc_sim_t *b = make_sim(&cfg);
	assert_non_null(b);
	size_t n_piecemeal = 0;
	size_t got;
	while ((got = mc_sim_read(b, 10000 * MS, piecemeal + n_piecemeal, 3)) > 0) {
		n_piecemeal += got;
	}
	mc_sim_destroy(b);

	assert_int_equal(n_whole, n_piecemeal);
	assert_memory_equal(whole, piecemeal, n_whole);
}

/* --- synthetic ---------------------------------------------------------- */

static void test_synthetic_waveforms(void **state)
{
	(void)state;

	mc_sim_config_t cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.mode = MC_SIM_SYNTHETIC;
	cfg.rate_hz = 10.0;
	cfg.channel_count = 3;

	cfg.channels[0].wave = MC_WAVE_CONSTANT;
	cfg.channels[0].min = 12.5;
	cfg.channels[0].precision = 2;

	cfg.channels[1].wave = MC_WAVE_SINE;
	cfg.channels[1].min = 0.0;
	cfg.channels[1].max = 360.0;
	cfg.channels[1].period_s = 10.0;
	cfg.channels[1].precision = 1;

	cfg.channels[2].wave = MC_WAVE_RAMP;
	cfg.channels[2].min = 0.0;
	cfg.channels[2].max = 100.0;
	cfg.channels[2].period_s = 10.0;
	cfg.channels[2].precision = 1;

	mc_sim_t *sim = make_sim(&cfg);
	assert_non_null(sim);

	uint8_t buf[256];
	const size_t n = drain(sim, 100 * MS, buf, sizeof(buf));
	assert_true(n > 0);
	buf[n] = '\0';

	/* Shape check: three comma-separated fields, CRLF, constant where a
	 * constant was asked for. */
	assert_non_null(strstr((const char *)buf, "12.50,"));
	assert_memory_equal(buf + n - 2, "\r\n", 2);

	int commas = 0;
	for (size_t i = 0; i < n; i++) {
		if (buf[i] == ',') {
			commas++;
		}
	}
	assert_int_equal(commas, 2);

	mc_sim_destroy(sim);
}

static void test_random_is_reproducible(void **state)
{
	(void)state;

	mc_sim_config_t cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.mode = MC_SIM_SYNTHETIC;
	cfg.rate_hz = 100.0;
	cfg.channel_count = 1;
	cfg.channels[0].wave = MC_WAVE_RANDOM;
	cfg.channels[0].min = -5.0;
	cfg.channels[0].max = 5.0;
	cfg.channels[0].precision = 4;
	cfg.seed = 12345;

	uint8_t first[2048];
	uint8_t second[2048];

	mc_sim_t *a = make_sim(&cfg);
	const size_t n1 = drain(a, 1000 * MS, first, sizeof(first));
	mc_sim_destroy(a);

	mc_sim_t *b = make_sim(&cfg);
	const size_t n2 = drain(b, 1000 * MS, second, sizeof(second));
	mc_sim_destroy(b);

	/* The whole value of fault injection is that a bad link reproduces. Random
	 * that is not repeatable makes a failing test unarguable-with. */
	assert_int_equal(n1, n2);
	assert_memory_equal(first, second, n1);

	/* A different seed must actually differ, or "reproducible" is just
	 * "constant". */
	cfg.seed = 54321;
	mc_sim_t *c = make_sim(&cfg);
	const size_t n3 = drain(c, 1000 * MS, second, sizeof(second));
	mc_sim_destroy(c);

	/* Not a length comparison: different values format to different widths, so
	 * two seeds legitimately produce different byte counts. Only the content
	 * has to differ. */
	const size_t shortest = n1 < n3 ? n1 : n3;
	assert_memory_not_equal(first, second, shortest);
}

/* --- fault injection ---------------------------------------------------- */

static void test_dropped_lines(void **state)
{
	(void)state;

	mc_sim_config_t cfg = file_config("esp32-survey.txt", 1000.0);
	cfg.faults.drop_every_n = 3;

	mc_sim_t *sim = make_sim(&cfg);
	assert_non_null(sim);

	uint8_t buf[8192];
	const size_t n = drain(sim, 10000 * MS, buf, sizeof(buf));

	/* 32 lines, every third gone. */
	assert_int_equal(mc_sim_dropped(sim), 10);
	assert_int_equal(mc_sim_lines(sim), 22);

	size_t crlf = 0;
	for (size_t i = 1; i < n; i++) {
		if (buf[i - 1] == '\r' && buf[i] == '\n') {
			crlf++;
		}
	}
	assert_int_equal(crlf, 22);

	mc_sim_destroy(sim);
}

static void test_malformed_lines(void **state)
{
	(void)state;

	mc_sim_config_t cfg = file_config("esp32-survey.txt", 1000.0);
	cfg.faults.malform_every_n = 4;

	mc_sim_t *sim = make_sim(&cfg);
	assert_non_null(sim);

	uint8_t buf[8192];
	const size_t n = drain(sim, 10000 * MS, buf, sizeof(buf));
	buf[n] = '\0';

	/* One field corrupted, not the whole line -- the case the parser makes
	 * promises about. */
	assert_non_null(strstr((const char *)buf, "#"));

	size_t crlf = 0;
	for (size_t i = 1; i < n; i++) {
		if (buf[i - 1] == '\r' && buf[i] == '\n') {
			crlf++;
		}
	}
	assert_int_equal(crlf, 32); /* every line still arrives */

	mc_sim_destroy(sim);
}

static void test_stall_goes_quiet_then_recovers(void **state)
{
	(void)state;

	mc_sim_config_t cfg = file_config("esp32-survey.txt", 10.0);
	cfg.loop = true;
	cfg.faults.stall_after_ms = 500;
	cfg.faults.stall_for_ms = 3000;

	mc_sim_t *sim = make_sim(&cfg);
	assert_non_null(sim);

	uint8_t buf[8192];

	drain(sim, 400 * MS, buf, sizeof(buf));
	const uint64_t before = mc_sim_lines(sim);
	assert_true(before > 0);

	/* Silent for three seconds. Indistinguishable from a slow device, which is
	 * exactly the situation a timeout has to resolve. */
	assert_int_equal(drain(sim, 2000 * MS, buf, sizeof(buf)), 0);
	assert_int_equal(mc_sim_lines(sim), before);

	/* And it comes back. A fault that never clears cannot test recovery. */
	assert_true(drain(sim, 4000 * MS, buf, sizeof(buf)) > 0);
	assert_true(mc_sim_lines(sim) > before);

	mc_sim_destroy(sim);
}

/* --- the whole chain, on real bytes -------------------------------------- */

static void on_frame(void *ctx, const uint8_t *frame, size_t len)
{
	mc_parser_feed((mc_parser_t *)ctx, mc_registry_get(), frame, len);
}

static void test_end_to_end_on_the_real_capture(void **state)
{
	(void)state;

	mc_registry_clear(mc_registry_get());

	/*
	 * Depth is field 3 of the capture, which really does hover around 12.9 m.
	 * Nothing about that was chosen to make this test pass -- it is what the
	 * device sent.
	 */
	mc_channel_def_t def;
	memset(&def, 0, sizeof(def));
	strncpy(def.name, "Depth", MC_NAME_MAX);
	def.scale = 1.0;
	def.has_range = true;
	def.min = 0.0;
	def.max = 100.0;
	def.has_precision = true;
	def.precision = 2;
	strncpy(def.unit, "m", MC_UNIT_MAX);
	assert_true(mc_registry_declare(mc_registry_get(), &def));

	mc_parser_config_t pcfg;
	memset(&pcfg, 0, sizeof(pcfg));
	pcfg.type = MC_PARSER_DELIMITED;
	pcfg.separator[0] = ',';
	pcfg.separator_len = 1;
	pcfg.trim = true;
	pcfg.fields[0].index = 3;
	strncpy(pcfg.fields[0].channel, "Depth", MC_NAME_MAX);
	pcfg.fields[0].numeric = true;
	pcfg.field_count = 1;

	mc_parser_t *parser = mc_parser_create(&pcfg);
	assert_non_null(parser);

	mc_frame_config_t fcfg;
	memset(&fcfg, 0, sizeof(fcfg));
	fcfg.mode = MC_FRAME_DELIMITER;
	fcfg.delimiter[0] = '\r';
	fcfg.delimiter[1] = '\n';
	fcfg.delimiter_len = 2;

	mc_frame_assembler_t *assembler = mc_frame_create(&fcfg);
	assert_non_null(assembler);

	mc_sim_config_t scfg = file_config("esp32-survey.txt", 1000.0);
	mc_sim_t *sim = make_sim(&scfg);
	assert_non_null(sim);

	/* Seven bytes at a time, so frames land across read boundaries the whole
	 * way through -- the real transport will be no tidier. */
	uint8_t chunk[7];
	size_t got;
	while ((got = mc_sim_read(sim, 10000 * MS, chunk, sizeof(chunk))) > 0) {
		mc_frame_push(assembler, chunk, got, 0, on_frame, parser);
	}

	/* Every line framed, every line parsed, nothing short and nothing bad --
	 * on bytes a device actually sent, chopped seven at a time. */
	assert_int_equal(mc_frame_count(assembler), 32);
	assert_int_equal(mc_frame_dropped(assembler), 0);
	assert_int_equal(mc_parser_rows(parser), 32);
	assert_int_equal(mc_parser_short_rows(parser), 0);
	assert_int_equal(mc_parser_bad_fields(parser), 0);

	mc_value_t depth;
	assert_true(mc_registry_read(mc_registry_get(), "Depth", &depth));
	assert_int_equal(depth.quality, MC_QUALITY_GOOD);
	assert_false(depth.out_of_range);

	/* The capture sits a little under 13 m throughout. Asserted as a band
	 * rather than a value, because the point is that a real reading survived
	 * the whole chain -- not that this particular line did. */
	assert_true(depth.numeric > 12.0 && depth.numeric < 14.0);

	char shown[32];
	assert_true(mc_registry_format(mc_registry_get(), "Depth", true, shown, sizeof(shown)) > 0);
	assert_int_equal(shown[strlen(shown) - 1], 'm');

	mc_sim_destroy(sim);
	mc_frame_destroy(assembler);
	mc_parser_destroy(parser);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_bad_config_is_refused),
		cmocka_unit_test(test_file_playback_at_a_fixed_rate),
		cmocka_unit_test(test_file_playback_stops_when_not_looping),
		cmocka_unit_test(test_file_playback_loops),
		cmocka_unit_test(test_timestamped_playback_keeps_the_jitter),
		cmocka_unit_test(test_small_buffer_changes_chunking_not_content),
		cmocka_unit_test(test_synthetic_waveforms),
		cmocka_unit_test(test_random_is_reproducible),
		cmocka_unit_test(test_dropped_lines),
		cmocka_unit_test(test_malformed_lines),
		cmocka_unit_test(test_stall_goes_quiet_then_recovers),
		cmocka_unit_test(test_end_to_end_on_the_real_capture),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
