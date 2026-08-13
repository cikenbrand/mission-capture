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

#pragma once

#include "mc-channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The parser: one frame in, published channel values out.
 *
 * POSITION CARRIES NO MEANING
 * ---------------------------
 * A survey system sends "12:01,5.2,1.3,2.1,180,10,20,30" and nothing else. No
 * header, no names, no units. The parser extracts by position and never guesses
 * what a value means; the operator's map supplies that:
 *
 *     field 0 -> Time      field 1 -> Depth
 *     field 2 -> Roll      field 3 -> Pitch
 *
 * The map is SPARSE. Mapping four positions out of eight is the normal case,
 * not a truncated configuration -- the unmapped fields are simply of no
 * interest, and ignoring them is not an error worth counting. This is why the
 * parser has no notion of a "correct" field count.
 *
 * A name may appear at one position only, and a position may carry one name
 * only. Both are rejected when the parser is created, not silently at sea.
 *
 * NOT EVERY FIELD IS A NUMBER
 * ---------------------------
 * "12:01" is a perfectly good Time reading and a perfectly bad double. A
 * mapping declares which it expects, so a text field is never marked BAD for
 * failing to be a number -- which would otherwise leave the Time channel
 * permanently red in the health panel.
 *
 * MALFORMED INPUT MUST NOT THROW AWAY GOOD CHANNELS
 * -------------------------------------------------
 * One unreadable field marks that one channel BAD. Every other mapped channel
 * in the row still publishes. Short rows and bad fields are counted rather than
 * logged per occurrence, because at 10 Hz a noisy line would bury the log.
 */

/* Highest field position that can be addressed. */
#define MC_MAX_FIELDS 64

typedef struct {
	size_t index;                  /* zero-based field position */
	char channel[MC_NAME_MAX + 1]; /* registry channel to publish into */

	/*
	 * False for fields that are text by nature -- a timestamp, a fix quality
	 * letter, a status word. Such a field keeps its raw token, reports no
	 * number, and is never BAD merely for not being numeric.
	 */
	bool numeric;
} mc_field_map_t;

typedef enum {
	MC_PARSER_DELIMITED, /* positional, separator-split -- the common case */
	MC_PARSER_NMEA,      /* NMEA-0183 sentences, with checksum validation */
} mc_parser_type_t;

#define MC_SEPARATOR_MAX 8
#define MC_SENTENCE_MAX 8

typedef struct {
	mc_parser_type_t type;

	/* --- MC_PARSER_DELIMITED --- */

	/* A string, not a character: some systems separate with ", " or "; ". */
	char separator[MC_SEPARATOR_MAX];
	size_t separator_len;

	/* Strip surrounding whitespace from each field. Also removes the trailing
	 * \r left by a CRLF device framed on \n, which 3.2 deliberately preserves
	 * (see OI-64 and the frame assembler notes). */
	bool trim;

	/* Treat a run of separators as one. For whitespace-aligned output; wrong
	 * for CSV, where ",,"" is an empty field and must stay one. */
	bool collapse_repeats;

	/* Quote character, or 0 for none. A separator inside quotes is literal. */
	char quote;

	/* --- MC_PARSER_NMEA --- */

	/*
	 * Sentence to accept, without the talker prefix -- "DBT" matches $SDDBT
	 * and $IIDBT alike, because the talker identifies the box and the sentence
	 * identifies the reading. Empty accepts any sentence.
	 *
	 * NMEA must be framed on its line terminator, NOT with 3.2's sentinel
	 * mode: sentinel framing strips the '$' and '*' and would discard the
	 * checksum before the parser ever saw it.
	 */
	char sentence[MC_SENTENCE_MAX];

	/* Reject a sentence whose checksum does not match. A sentence carrying no
	 * checksum at all is accepted either way -- some devices omit it. */
	bool require_checksum;

	/* --- both --- */

	mc_field_map_t fields[MC_MAX_FIELDS];
	size_t field_count;
} mc_parser_config_t;

typedef struct mc_parser mc_parser_t;

/*
 * Returns NULL for a configuration that could not work: a duplicated channel
 * name, two names on one position, an out-of-range position, a malformed
 * channel name, or a zero-length separator.
 */
mc_parser_t *mc_parser_create(const mc_parser_config_t *config);
void mc_parser_destroy(mc_parser_t *parser);

/*
 * Parses one frame -- as delivered by the frame assembler, framing bytes
 * already stripped -- and publishes every mapped channel it can into the
 * registry.
 *
 * Channels must already be declared; publishing to an undeclared name is
 * silently ignored by the registry, by design.
 */
void mc_parser_feed(mc_parser_t *parser, mc_registry_t *registry, const uint8_t *frame, size_t len);

/* Rows that produced at least one published channel. */
uint64_t mc_parser_rows(const mc_parser_t *parser);

/* Rows too short to reach every mapped position. Normal on a noisy line;
 * a climbing count means the wrong map or the wrong device. */
uint64_t mc_parser_short_rows(const mc_parser_t *parser);

/* Individual fields that were present but unreadable. */
uint64_t mc_parser_bad_fields(const mc_parser_t *parser);

/* Whole frames thrown out: wrong sentence, failed checksum, malformed. */
uint64_t mc_parser_rejected(const mc_parser_t *parser);

#ifdef __cplusplus
}
#endif
