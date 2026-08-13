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

#include <mc-data/mc-parser.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>

#include <math.h>
#include <string.h>

/*
 * Task 3.3.
 *
 * The operator's row, used throughout:
 *
 *     12:01,5.2,1.3,2.1,180,10,20,30
 *
 * Four of those eight positions are mapped. That is the normal case, not a
 * truncated configuration, and the four that are not mapped must cost nothing
 * and count for nothing.
 *
 * Field 0 is Time, which is text: "12:01" is a good reading and a bad double,
 * and must never be marked BAD for it.
 */

static void declare(const char *name)
{
	mc_channel_def_t def;
	memset(&def, 0, sizeof(def));
	strncpy(def.name, name, MC_NAME_MAX);
	def.scale = 1.0;
	def.offset = 0.0;
	assert_true(mc_registry_declare(mc_registry_get(), &def));
}

static mc_value_t read_channel(const char *name)
{
	mc_value_t value;
	assert_true(mc_registry_read(mc_registry_get(), name, &value));
	return value;
}

static void map_field(mc_parser_config_t *cfg, size_t index, const char *channel, bool numeric)
{
	mc_field_map_t *f = &cfg->fields[cfg->field_count++];
	f->index = index;
	strncpy(f->channel, channel, MC_NAME_MAX);
	f->numeric = numeric;
}

/* The operator's map: four names on four positions of an eight-field row. */
static mc_parser_config_t survey_config(void)
{
	mc_parser_config_t cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.type = MC_PARSER_DELIMITED;
	cfg.separator[0] = ',';
	cfg.separator_len = 1;
	cfg.trim = true;

	map_field(&cfg, 0, "Time", false);
	map_field(&cfg, 1, "Depth", true);
	map_field(&cfg, 2, "Roll", true);
	map_field(&cfg, 3, "Pitch", true);

	return cfg;
}

static void declare_survey_channels(void)
{
	declare("Time");
	declare("Depth");
	declare("Roll");
	declare("Pitch");
}

static int setup(void **state)
{
	(void)state;
	mc_registry_clear(mc_registry_get());
	return 0;
}

static void feed(mc_parser_t *parser, const char *line)
{
	mc_parser_feed(parser, mc_registry_get(), (const uint8_t *)line, strlen(line));
}

/* --- configuration ------------------------------------------------------ */

static void test_conflicting_map_is_refused(void **state)
{
	(void)state;

	/* The operator's own rule, enforced rather than trusted. */
	mc_parser_config_t cfg = survey_config();
	map_field(&cfg, 4, "Depth", true); /* Depth already claimed position 1 */
	assert_null(mc_parser_create(&cfg));

	cfg = survey_config();
	map_field(&cfg, 1, "Heading", true); /* position 1 already claimed */
	assert_null(mc_parser_create(&cfg));

	cfg = survey_config();
	map_field(&cfg, MC_MAX_FIELDS, "Heading", true);
	assert_null(mc_parser_create(&cfg));

	cfg = survey_config();
	map_field(&cfg, 4, "not a valid name", true);
	assert_null(mc_parser_create(&cfg));

	cfg = survey_config();
	cfg.separator_len = 0;
	assert_null(mc_parser_create(&cfg));

	assert_null(mc_parser_create(NULL));
}

/* --- the ordinary case -------------------------------------------------- */

static void test_sparse_map_over_a_longer_row(void **state)
{
	(void)state;
	declare_survey_channels();

	const mc_parser_config_t cfg = survey_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	feed(parser, "12:01,5.2,1.3,2.1,180,10,20,30");

	/* Text field: kept verbatim, no number, and emphatically not BAD. */
	mc_value_t time = read_channel("Time");
	assert_int_equal(time.quality, MC_QUALITY_GOOD);
	assert_string_equal(time.text, "12:01");
	assert_true(isnan(time.numeric));

	mc_value_t depth = read_channel("Depth");
	assert_int_equal(depth.quality, MC_QUALITY_GOOD);
	assert_float_equal(depth.numeric, 5.2, 0.0001);

	assert_float_equal(read_channel("Roll").numeric, 1.3, 0.0001);
	assert_float_equal(read_channel("Pitch").numeric, 2.1, 0.0001);

	/* The four unmapped values cost nothing and count for nothing. */
	assert_int_equal(mc_parser_rows(parser), 1);
	assert_int_equal(mc_parser_short_rows(parser), 0);
	assert_int_equal(mc_parser_bad_fields(parser), 0);
	assert_int_equal(mc_parser_rejected(parser), 0);

	mc_parser_destroy(parser);
}

static void test_exactly_as_many_fields_as_mapped(void **state)
{
	(void)state;
	declare_survey_channels();

	const mc_parser_config_t cfg = survey_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	feed(parser, "12:01,5.2,1.3,2.1");

	assert_int_equal(mc_parser_short_rows(parser), 0);
	assert_float_equal(read_channel("Pitch").numeric, 2.1, 0.0001);

	mc_parser_destroy(parser);
}

static void test_trim_removes_the_crlf_leftover(void **state)
{
	(void)state;
	declare_survey_channels();

	const mc_parser_config_t cfg = survey_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	/* 3.2 deliberately leaves the trailing \r on a CRLF device framed on \n.
	 * This is the layer that was nominated to clean it up. */
	feed(parser, " 12:01 , 5.2 , 1.3 , 2.1 \r");

	assert_string_equal(read_channel("Time").text, "12:01");
	assert_int_equal(read_channel("Pitch").quality, MC_QUALITY_GOOD);
	assert_float_equal(read_channel("Pitch").numeric, 2.1, 0.0001);
	assert_int_equal(mc_parser_bad_fields(parser), 0);

	mc_parser_destroy(parser);
}

/* --- malformed corpus --------------------------------------------------- */

static void test_one_bad_field_does_not_lose_the_others(void **state)
{
	(void)state;
	declare_survey_channels();

	const mc_parser_config_t cfg = survey_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	/* Roll is garbage. The plan is explicit: publish the rest. */
	feed(parser, "12:01,5.2,####,2.1,180");

	assert_int_equal(read_channel("Roll").quality, MC_QUALITY_BAD);
	assert_true(isnan(read_channel("Roll").numeric));
	assert_string_equal(read_channel("Roll").text, "####");

	assert_int_equal(read_channel("Depth").quality, MC_QUALITY_GOOD);
	assert_float_equal(read_channel("Depth").numeric, 5.2, 0.0001);
	assert_int_equal(read_channel("Pitch").quality, MC_QUALITY_GOOD);
	assert_float_equal(read_channel("Pitch").numeric, 2.1, 0.0001);

	assert_int_equal(mc_parser_bad_fields(parser), 1);
	assert_int_equal(mc_parser_short_rows(parser), 0);

	mc_parser_destroy(parser);
}

static void test_trailing_garbage_is_not_a_number(void **state)
{
	(void)state;
	declare_survey_channels();

	const mc_parser_config_t cfg = survey_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	/* "5.2m" must not quietly become 5.2. A unit suffix appearing means the
	 * device is not sending what the map claims, and that is worth seeing. */
	feed(parser, "12:01,5.2m,1.3,2.1");

	assert_int_equal(read_channel("Depth").quality, MC_QUALITY_BAD);
	assert_string_equal(read_channel("Depth").text, "5.2m");
	assert_int_equal(mc_parser_bad_fields(parser), 1);

	mc_parser_destroy(parser);
}

static void test_empty_field_is_bad_not_zero(void **state)
{
	(void)state;
	declare_survey_channels();

	const mc_parser_config_t cfg = survey_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	/* A device saying "no reading" must not read as a depth of zero. */
	feed(parser, "12:01,,1.3,2.1");

	assert_int_equal(read_channel("Depth").quality, MC_QUALITY_BAD);
	assert_true(isnan(read_channel("Depth").numeric));
	assert_int_equal(mc_parser_bad_fields(parser), 1);

	mc_parser_destroy(parser);
}

static void test_short_row_marks_only_what_is_missing(void **state)
{
	(void)state;
	declare_survey_channels();

	const mc_parser_config_t cfg = survey_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	feed(parser, "12:01,5.2,1.3,2.1");
	feed(parser, "12:02,5.3"); /* stops before Roll and Pitch */

	/* What arrived is published. */
	assert_float_equal(read_channel("Depth").numeric, 5.3, 0.0001);

	/*
	 * What did not arrive goes BAD rather than keeping the previous reading.
	 * A stale 1.3 sitting on screen looking current is the failure mode this
	 * exists to prevent -- it ends up on a client deliverable.
	 */
	assert_int_equal(read_channel("Roll").quality, MC_QUALITY_BAD);
	assert_int_equal(read_channel("Pitch").quality, MC_QUALITY_BAD);

	assert_int_equal(mc_parser_short_rows(parser), 1);
	assert_int_equal(mc_parser_rows(parser), 2);

	mc_parser_destroy(parser);
}

static void test_row_missing_only_the_last_mapped_field(void **state)
{
	(void)state;
	declare_survey_channels();

	const mc_parser_config_t cfg = survey_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	feed(parser, "12:01,5.2,1.3,2.1");
	feed(parser, "12:02,5.3,1.4"); /* three fields; Pitch is at position 3 */

	/*
	 * The boundary case, and the one an off-by-one in the short-row check
	 * sails straight past: the row is one field short, so only the last
	 * mapped channel is missing.
	 */
	assert_float_equal(read_channel("Roll").numeric, 1.4, 0.0001);
	assert_int_equal(read_channel("Pitch").quality, MC_QUALITY_BAD);
	assert_int_equal(mc_parser_short_rows(parser), 1);

	mc_parser_destroy(parser);
}

static void test_fragment_from_a_mid_stream_join(void **state)
{
	(void)state;
	declare_survey_channels();

	const mc_parser_config_t cfg = survey_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	/*
	 * OI-64 in practice. The frame assembler cannot tell a fragment from a
	 * short row, and neither can this -- but a fragment is short, so the
	 * mapped positions it never reached go BAD and the row is counted.
	 *
	 * It does NOT make the values it does carry safe: "01" lands in Time and
	 * "5.2" in Depth even though they came from the middle of a row. That is
	 * the residual risk, and the short-row count is what makes it visible.
	 */
	feed(parser, "01,5.2");

	assert_int_equal(mc_parser_short_rows(parser), 1);
	assert_int_equal(read_channel("Roll").quality, MC_QUALITY_BAD);
	assert_int_equal(read_channel("Pitch").quality, MC_QUALITY_BAD);

	mc_parser_destroy(parser);
}

static void test_junk_line_publishes_nothing_usable(void **state)
{
	(void)state;
	declare_survey_channels();

	const mc_parser_config_t cfg = survey_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	feed(parser, "@@@@@@@@");

	/* One field, all of it garbage. Time is text so it takes it; every
	 * numeric channel is BAD, and nothing reads as a plausible number. */
	assert_int_equal(read_channel("Depth").quality, MC_QUALITY_BAD);
	assert_int_equal(read_channel("Roll").quality, MC_QUALITY_BAD);
	assert_int_equal(read_channel("Pitch").quality, MC_QUALITY_BAD);
	assert_int_equal(mc_parser_short_rows(parser), 1);

	mc_parser_destroy(parser);
}

/* --- separators --------------------------------------------------------- */

static void test_multi_character_separator(void **state)
{
	(void)state;
	declare_survey_channels();

	mc_parser_config_t cfg = survey_config();
	cfg.separator[0] = ';';
	cfg.separator[1] = ' ';
	cfg.separator_len = 2;

	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	feed(parser, "12:01; 5.2; 1.3; 2.1");

	assert_float_equal(read_channel("Depth").numeric, 5.2, 0.0001);
	assert_int_equal(mc_parser_bad_fields(parser), 0);

	mc_parser_destroy(parser);
}

static void test_collapse_repeats_for_aligned_output(void **state)
{
	(void)state;
	declare_survey_channels();

	mc_parser_config_t cfg = survey_config();
	cfg.separator[0] = ' ';
	cfg.separator_len = 1;
	cfg.collapse_repeats = true;

	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	feed(parser, "12:01    5.2   1.3      2.1");

	assert_float_equal(read_channel("Depth").numeric, 5.2, 0.0001);
	assert_float_equal(read_channel("Pitch").numeric, 2.1, 0.0001);
	assert_int_equal(mc_parser_short_rows(parser), 0);

	mc_parser_destroy(parser);
}

static void test_empty_fields_survive_without_collapse(void **state)
{
	(void)state;
	declare_survey_channels();

	const mc_parser_config_t cfg = survey_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	/* Collapsing here would shift every later value into the wrong channel,
	 * which is why it is opt-in rather than helpful by default. */
	feed(parser, "12:01,,,2.1");

	assert_int_equal(read_channel("Depth").quality, MC_QUALITY_BAD);
	assert_int_equal(read_channel("Roll").quality, MC_QUALITY_BAD);
	assert_float_equal(read_channel("Pitch").numeric, 2.1, 0.0001);

	mc_parser_destroy(parser);
}

static void test_quoted_separator_is_literal(void **state)
{
	(void)state;
	declare_survey_channels();

	mc_parser_config_t cfg = survey_config();
	cfg.quote = '"';

	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	feed(parser, "\"12:01,5\",5.2,1.3,2.1");

	assert_string_equal(read_channel("Time").text, "12:01,5");
	assert_float_equal(read_channel("Depth").numeric, 5.2, 0.0001);

	mc_parser_destroy(parser);
}

/* --- NMEA-0183 ---------------------------------------------------------- */

static mc_parser_config_t dbt_config(void)
{
	mc_parser_config_t cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.type = MC_PARSER_NMEA;
	strncpy(cfg.sentence, "DBT", MC_SENTENCE_MAX - 1);
	cfg.require_checksum = true;

	/* $SDDBT,12.3,f,3.7,M,2.0,F -- depth in feet, metres, fathoms. */
	map_field(&cfg, 2, "Depth", true);

	return cfg;
}

static void test_nmea_valid_sentence(void **state)
{
	(void)state;
	declare("Depth");

	const mc_parser_config_t cfg = dbt_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	feed(parser, "$SDDBT,12.3,f,3.7,M,2.0,F*30");

	assert_int_equal(read_channel("Depth").quality, MC_QUALITY_GOOD);
	assert_float_equal(read_channel("Depth").numeric, 3.7, 0.0001);
	assert_int_equal(mc_parser_rejected(parser), 0);

	mc_parser_destroy(parser);
}

static void test_nmea_bad_checksum_is_rejected_whole(void **state)
{
	(void)state;
	declare("Depth");

	const mc_parser_config_t cfg = dbt_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	feed(parser, "$SDDBT,12.3,f,3.7,M,2.0,F*30");  /* good, to have a value */
	feed(parser, "$SDDBT,99.9,f,99.9,M,9.9,F*00"); /* checksum does not match */

	/*
	 * The whole sentence goes, not one field. That is what a checksum is for:
	 * it says the line is corrupt, so no part of it can be trusted -- unlike a
	 * single unreadable field, where the positions are still sound.
	 */
	assert_float_equal(read_channel("Depth").numeric, 3.7, 0.0001);
	assert_int_equal(mc_parser_rejected(parser), 1);
	assert_int_equal(mc_parser_rows(parser), 1);

	mc_parser_destroy(parser);
}

static void test_nmea_talker_prefix_is_ignored(void **state)
{
	(void)state;
	declare("Depth");

	const mc_parser_config_t cfg = dbt_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	/* Same reading, different box. An operator configuring "DBT" wants depth
	 * from whichever sounder is fitted. */
	feed(parser, "$IIDBT,12.3,f,3.7,M,2.0,F*27");

	assert_int_equal(read_channel("Depth").quality, MC_QUALITY_GOOD);
	assert_float_equal(read_channel("Depth").numeric, 3.7, 0.0001);

	mc_parser_destroy(parser);
}

static void test_nmea_other_sentences_are_skipped(void **state)
{
	(void)state;
	declare("Depth");

	const mc_parser_config_t cfg = dbt_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	/* A shared line carries everything. Another sentence is not a fault, but
	 * it is counted, so a map pointed at a sentence the device never sends is
	 * visible rather than merely silent. */
	feed(parser, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47");

	assert_int_equal(read_channel("Depth").quality, MC_QUALITY_NODATA);
	assert_int_equal(mc_parser_rejected(parser), 1);
	assert_int_equal(mc_parser_rows(parser), 0);

	mc_parser_destroy(parser);
}

static void test_nmea_missing_checksum(void **state)
{
	(void)state;
	declare("Depth");

	mc_parser_config_t cfg = dbt_config();
	mc_parser_t *strict = mc_parser_create(&cfg);
	assert_non_null(strict);

	feed(strict, "$SDDBT,12.3,f,3.7,M,2.0,F");
	assert_int_equal(mc_parser_rejected(strict), 1);
	assert_int_equal(read_channel("Depth").quality, MC_QUALITY_NODATA);
	mc_parser_destroy(strict);

	/* Some devices genuinely omit it, so the check is configurable. */
	cfg.require_checksum = false;
	mc_parser_t *lenient = mc_parser_create(&cfg);
	assert_non_null(lenient);

	feed(lenient, "$SDDBT,12.3,f,3.7,M,2.0,F");
	assert_int_equal(mc_parser_rejected(lenient), 0);
	assert_float_equal(read_channel("Depth").numeric, 3.7, 0.0001);
	mc_parser_destroy(lenient);
}

static void test_nmea_empty_field_is_bad(void **state)
{
	(void)state;
	declare("Depth");

	const mc_parser_config_t cfg = dbt_config();
	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	/* Routine in NMEA: the sounder has no lock. Must not read as zero depth. */
	feed(parser, "$SDDBT,,f,,M,,F*28");

	assert_int_equal(read_channel("Depth").quality, MC_QUALITY_BAD);
	assert_true(isnan(read_channel("Depth").numeric));

	mc_parser_destroy(parser);
}

static void test_nmea_truncated_sentence(void **state)
{
	(void)state;
	declare("Depth");

	mc_parser_config_t cfg = dbt_config();
	cfg.require_checksum = false;

	mc_parser_t *parser = mc_parser_create(&cfg);
	assert_non_null(parser);

	feed(parser, "$SDDBT");   /* identifier only, no fields */
	feed(parser, "$");        /* nothing at all */
	feed(parser, "$SDDBT,*"); /* a star where a checksum should be */

	assert_int_equal(read_channel("Depth").quality, MC_QUALITY_NODATA);
	assert_true(mc_parser_rejected(parser) > 0);

	mc_parser_destroy(parser);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_conflicting_map_is_refused, setup),
		cmocka_unit_test_setup(test_sparse_map_over_a_longer_row, setup),
		cmocka_unit_test_setup(test_exactly_as_many_fields_as_mapped, setup),
		cmocka_unit_test_setup(test_trim_removes_the_crlf_leftover, setup),
		cmocka_unit_test_setup(test_one_bad_field_does_not_lose_the_others, setup),
		cmocka_unit_test_setup(test_trailing_garbage_is_not_a_number, setup),
		cmocka_unit_test_setup(test_empty_field_is_bad_not_zero, setup),
		cmocka_unit_test_setup(test_short_row_marks_only_what_is_missing, setup),
		cmocka_unit_test_setup(test_row_missing_only_the_last_mapped_field, setup),
		cmocka_unit_test_setup(test_fragment_from_a_mid_stream_join, setup),
		cmocka_unit_test_setup(test_junk_line_publishes_nothing_usable, setup),
		cmocka_unit_test_setup(test_multi_character_separator, setup),
		cmocka_unit_test_setup(test_collapse_repeats_for_aligned_output, setup),
		cmocka_unit_test_setup(test_empty_fields_survive_without_collapse, setup),
		cmocka_unit_test_setup(test_quoted_separator_is_literal, setup),
		cmocka_unit_test_setup(test_nmea_valid_sentence, setup),
		cmocka_unit_test_setup(test_nmea_bad_checksum_is_rejected_whole, setup),
		cmocka_unit_test_setup(test_nmea_talker_prefix_is_ignored, setup),
		cmocka_unit_test_setup(test_nmea_other_sentences_are_skipped, setup),
		cmocka_unit_test_setup(test_nmea_missing_checksum, setup),
		cmocka_unit_test_setup(test_nmea_empty_field_is_bad, setup),
		cmocka_unit_test_setup(test_nmea_truncated_sentence, setup),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
