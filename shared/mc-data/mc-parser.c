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

#include "mc-data/mc-parser.h"

#include <util/bmem.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct mc_parser {
	mc_parser_config_t cfg;
	size_t highest_index; /* highest mapped position, for short-row detection */

	uint64_t rows;
	uint64_t short_rows;
	uint64_t bad_fields;
	uint64_t rejected;
};

/* --- configuration ------------------------------------------------------ */

static bool config_is_sane(const mc_parser_config_t *cfg)
{
	if (cfg->field_count > MC_MAX_FIELDS) {
		return false;
	}

	if (cfg->type == MC_PARSER_DELIMITED) {
		if (cfg->separator_len == 0 || cfg->separator_len > MC_SEPARATOR_MAX) {
			return false;
		}
	} else if (cfg->type != MC_PARSER_NMEA) {
		return false;
	}

	for (size_t i = 0; i < cfg->field_count; i++) {
		const mc_field_map_t *f = &cfg->fields[i];

		if (f->index >= MC_MAX_FIELDS || !mc_channel_name_valid(f->channel)) {
			return false;
		}

		/*
		 * The operator's rule, enforced rather than trusted: one name at one
		 * position. Two names on a position, or one name at two positions,
		 * makes the display depend on evaluation order -- a bug that would
		 * only ever show up on a dive.
		 */
		for (size_t j = 0; j < i; j++) {
			if (cfg->fields[j].index == f->index) {
				return false;
			}
			if (strcmp(cfg->fields[j].channel, f->channel) == 0) {
				return false;
			}
		}
	}

	return true;
}

mc_parser_t *mc_parser_create(const mc_parser_config_t *config)
{
	if (!config || !config_is_sane(config)) {
		return NULL;
	}

	mc_parser_t *parser = bzalloc(sizeof(*parser));
	parser->cfg = *config;

	for (size_t i = 0; i < config->field_count; i++) {
		if (config->fields[i].index > parser->highest_index) {
			parser->highest_index = config->fields[i].index;
		}
	}

	return parser;
}

void mc_parser_destroy(mc_parser_t *parser)
{
	bfree(parser);
}

/* --- one field ---------------------------------------------------------- */

static const mc_field_map_t *mapping_for(const mc_parser_t *parser, size_t index)
{
	for (size_t i = 0; i < parser->cfg.field_count; i++) {
		if (parser->cfg.fields[i].index == index) {
			return &parser->cfg.fields[i];
		}
	}
	return NULL;
}

static bool is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

/*
 * Publishes one field into its channel.
 *
 * A field that is present but unreadable marks its own channel BAD and leaves
 * every other channel in the row alone -- one bad reading is not a reason to
 * blank the display.
 */
static void publish_field(mc_parser_t *parser, mc_registry_t *registry, const mc_field_map_t *map, const char *start,
			  size_t len)
{
	if (parser->cfg.trim) {
		while (len > 0 && is_space(*start)) {
			start++;
			len--;
		}
		while (len > 0 && is_space(start[len - 1])) {
			len--;
		}
	}

	/* Strip a matched pair of quotes, so a separator inside them was literal. */
	const char quote = parser->cfg.quote;
	if (quote && len >= 2 && start[0] == quote && start[len - 1] == quote) {
		start++;
		len -= 2;
	}

	char token[MC_TEXT_MAX + 1];
	const size_t copy = len < MC_TEXT_MAX ? len : MC_TEXT_MAX;
	memcpy(token, start, copy);
	token[copy] = '\0';

	if (!map->numeric) {
		/* Text by nature: "12:01" is a good Time and a bad double. Never BAD
		 * merely for failing to be a number. */
		mc_registry_publish(registry, map->channel, NAN, token, MC_QUALITY_GOOD);
		return;
	}

	if (copy == 0) {
		/* An empty field is the device saying it has no reading -- routine in
		 * NMEA, and exactly what BAD means: present, unusable. */
		parser->bad_fields++;
		mc_registry_publish(registry, map->channel, NAN, token, MC_QUALITY_BAD);
		return;
	}

	char *end = NULL;
	const double value = strtod(token, &end);

	/* The whole token must be consumed. "12.3abc" is not 12.3, it is a fault
	 * worth seeing. */
	if (end == token || *end != '\0') {
		parser->bad_fields++;
		mc_registry_publish(registry, map->channel, NAN, token, MC_QUALITY_BAD);
		return;
	}

	mc_registry_publish(registry, map->channel, value, token, MC_QUALITY_GOOD);
}

/* Marks every mapped position the row never reached. */
static void mark_missing(mc_parser_t *parser, mc_registry_t *registry, size_t fields_seen)
{
	for (size_t i = 0; i < parser->cfg.field_count; i++) {
		const mc_field_map_t *f = &parser->cfg.fields[i];
		if (f->index >= fields_seen) {
			mc_registry_publish(registry, f->channel, NAN, "", MC_QUALITY_BAD);
		}
	}
}

/* --- delimited ---------------------------------------------------------- */

/*
 * Splits on the separator and publishes mapped positions as it goes.
 *
 * Returns the number of fields the row actually contained. Fields past the
 * highest mapped position are counted but not examined: a row carrying eight
 * values when four are mapped is the normal case, not a malformed row.
 */
static size_t split_and_publish(mc_parser_t *parser, mc_registry_t *registry, const char *frame, size_t len,
				const char *sep, size_t sep_len, char quote, bool collapse)
{
	size_t index = 0;
	size_t start = 0;
	bool quoted = false;

	for (size_t i = 0; i <= len;) {
		const bool at_end = (i == len);

		if (!at_end && quote && frame[i] == quote) {
			quoted = !quoted;
			i++;
			continue;
		}

		const bool at_sep = !at_end && !quoted && i + sep_len <= len && memcmp(frame + i, sep, sep_len) == 0;

		if (!at_end && !at_sep) {
			i++;
			continue;
		}

		const mc_field_map_t *map = mapping_for(parser, index);
		if (map) {
			publish_field(parser, registry, map, frame + start, i - start);
		}
		index++;

		if (at_end) {
			break;
		}

		i += sep_len;

		/* Collapse runs of separators, for whitespace-aligned output. Wrong
		 * for CSV, where ",," is a real empty field, so it is opt-in. */
		if (collapse) {
			while (i + sep_len <= len && memcmp(frame + i, sep, sep_len) == 0) {
				i += sep_len;
			}
		}

		start = i;
	}

	return index;
}

/* --- NMEA-0183 ---------------------------------------------------------- */

static int hex_value(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	return -1;
}

/*
 * Strips the NMEA envelope and validates the checksum.
 *
 * On success `body` spans the sentence between '$' and '*', exclusive of both.
 * A sentence with no checksum is accepted unless the configuration demands one;
 * some devices genuinely omit it.
 */
static bool nmea_unwrap(mc_parser_t *parser, const char *frame, size_t len, const char **body, size_t *body_len)
{
	if (len > 0 && frame[0] == '$') {
		frame++;
		len--;
	}

	size_t star = len;
	for (size_t i = 0; i < len; i++) {
		if (frame[i] == '*') {
			star = i;
			break;
		}
	}

	if (star == len) {
		if (parser->cfg.require_checksum) {
			return false;
		}
		*body = frame;
		*body_len = len;
		return true;
	}

	/* Exactly two hex digits follow the star. Anything else is a corrupted
	 * sentence, not a sentence without a checksum. */
	if (len - star < 3) {
		return false;
	}

	const int high = hex_value(frame[star + 1]);
	const int low = hex_value(frame[star + 2]);
	if (high < 0 || low < 0) {
		return false;
	}

	uint8_t computed = 0;
	for (size_t i = 0; i < star; i++) {
		computed ^= (uint8_t)frame[i];
	}

	if (computed != (uint8_t)((high << 4) | low)) {
		return false;
	}

	*body = frame;
	*body_len = star;
	return true;
}

/* True if this sentence is the one configured for. */
static bool nmea_sentence_matches(const mc_parser_t *parser, const char *body, size_t body_len)
{
	if (parser->cfg.sentence[0] == '\0') {
		return true;
	}

	size_t id_len = body_len;
	for (size_t i = 0; i < body_len; i++) {
		if (body[i] == ',') {
			id_len = i;
			break;
		}
	}

	/*
	 * The talker prefix identifies the box, the sentence identifies the
	 * reading, and an operator configuring "DBT" wants depth from whichever
	 * sounder is fitted. So match the tail: "DBT" accepts $SDDBT and $IIDBT
	 * alike.
	 */
	const size_t want = strlen(parser->cfg.sentence);
	if (want > id_len) {
		return false;
	}

	return memcmp(body + id_len - want, parser->cfg.sentence, want) == 0;
}

/* --- feed --------------------------------------------------------------- */

void mc_parser_feed(mc_parser_t *parser, mc_registry_t *registry, const uint8_t *frame, size_t len)
{
	if (!parser || !registry || !frame || len == 0) {
		return;
	}

	const char *text = (const char *)frame;
	size_t fields_seen = 0;

	if (parser->cfg.type == MC_PARSER_NMEA) {
		const char *body = NULL;
		size_t body_len = 0;

		if (!nmea_unwrap(parser, text, len, &body, &body_len)) {
			parser->rejected++;
			return;
		}

		if (!nmea_sentence_matches(parser, body, body_len)) {
			/* Another sentence on a shared line. Not a fault, but not ours;
			 * counted so a map pointed at a sentence the device never sends
			 * is visible rather than merely silent. */
			parser->rejected++;
			return;
		}

		/* Field 0 is the first value after the sentence identifier. */
		size_t offset = body_len;
		for (size_t i = 0; i < body_len; i++) {
			if (body[i] == ',') {
				offset = i + 1;
				break;
			}
		}

		if (offset >= body_len) {
			parser->rejected++;
			return;
		}

		/* NMEA's separator is not negotiable: a comma, no quoting, no
		 * collapsing, because ",," is a field the device left empty. */
		fields_seen = split_and_publish(parser, registry, body + offset, body_len - offset, ",", 1, 0, false);
	} else {
		fields_seen = split_and_publish(parser, registry, text, len, parser->cfg.separator,
						parser->cfg.separator_len, parser->cfg.quote,
						parser->cfg.collapse_repeats);
	}

	if (fields_seen <= parser->highest_index) {
		/*
		 * The row stopped before a position the operator mapped. Those
		 * channels go BAD rather than keeping a previous value that would sit
		 * on screen looking current.
		 */
		parser->short_rows++;
		mark_missing(parser, registry, fields_seen);
	}

	parser->rows++;
}

uint64_t mc_parser_rows(const mc_parser_t *parser)
{
	return parser ? parser->rows : 0;
}

uint64_t mc_parser_short_rows(const mc_parser_t *parser)
{
	return parser ? parser->short_rows : 0;
}

uint64_t mc_parser_bad_fields(const mc_parser_t *parser)
{
	return parser ? parser->bad_fields : 0;
}

uint64_t mc_parser_rejected(const mc_parser_t *parser)
{
	return parser ? parser->rejected : 0;
}
