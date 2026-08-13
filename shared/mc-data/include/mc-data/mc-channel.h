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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The channel registry: one named, typed, timestamped value per survey field.
 *
 * WHAT A CHANNEL IS
 * -----------------
 * A survey system sends "1.031,5.132,6.122,7.451" and nothing else. The parser
 * (task 3.3) turns position into a name using an operator-configured map; this
 * registry is where the named values live, and it is what the overlay renderer
 * and the sidecar log both read from.
 *
 * The registry deliberately knows nothing about serial ports, parsers or
 * meaning. It stores values under names. That is the whole contract, and it is
 * why this can be unit-tested without a device.
 *
 * THREADING
 * ---------
 * Writers are transport threads, readers are the graphics thread and the log
 * writer. A mutex around a hash table is ample: four devices at 10 Hz is forty
 * writes a second (see docs/subsea/phase-3-data-core.md), which is nowhere near
 * where lock contention becomes interesting. Readers copy and release
 * immediately.
 *
 * STALENESS IS COMPUTED ON READ
 * -----------------------------
 * No timer thread, no wakeups. A value is stale if nothing has updated it
 * within its declared timeout, and that is a comparison the reader can make.
 */

/* Longest channel name, excluding the terminator. Bounded so a format string
 * such as "Depth: {DEPTH} m" can be parsed without ambiguity. */
#define MC_NAME_MAX 32

/* Longest raw token kept per value, excluding the terminator. */
#define MC_TEXT_MAX 64

typedef enum {
	MC_QUALITY_GOOD,   /* fresh, parsed cleanly */
	MC_QUALITY_STALE,  /* no update within stale_timeout_ms */
	MC_QUALITY_BAD,    /* present but unparseable */
	MC_QUALITY_NODATA, /* declared, never received */
} mc_quality_t;

/*
 * One reading.
 *
 * `text` is a fixed array rather than the `const char *` the plan sketched.
 * Deliberate: the plan also required that a raw pointer into registry storage
 * never reaches the graphics thread, and those two cannot both hold. An array
 * makes a value self-contained, so a reader owns its copy outright and no
 * lifetime rule has to be remembered at each call site.
 */
typedef struct {
	double numeric;         /* after scale and offset; NaN if non-numeric */
	char text[MC_TEXT_MAX + 1]; /* raw token as received, always populated */
	uint64_t ts_ns;         /* monotonic clock at receive */
	uint64_t wall_ns;       /* UTC wall clock at receive, for the sidecar log */
	mc_quality_t quality;
	uint64_t seq;           /* monotonic update counter, per channel */
} mc_value_t;

typedef struct {
	char name[MC_NAME_MAX + 1];

	/* Applied to the parsed number before storage: numeric = raw*scale + offset.
	 * Survey systems emit centimetres, tenths of a degree and similar; doing the
	 * conversion once here keeps it out of every overlay and every export. */
	double scale;
	double offset;

	/* Nothing for this long means the reading is no longer trustworthy. Zero
	 * disables the check, for a channel that legitimately updates rarely. */
	uint64_t stale_timeout_ms;
} mc_channel_def_t;

typedef struct mc_registry mc_registry_t;

/* Process-wide registry. Created on first use. */
mc_registry_t *mc_registry_get(void);

/*
 * Declares a channel. Config time only.
 *
 * Rejects a malformed name -- case-sensitive, [A-Za-z0-9_] only, 1 to
 * MC_NAME_MAX characters -- so that format-string parsing later cannot be
 * ambiguous. Re-declaring an existing name updates its definition and keeps any
 * value already published, because re-reading a config should not blank the
 * display.
 */
bool mc_registry_declare(mc_registry_t *registry, const mc_channel_def_t *def);

/* True if the name would be accepted by declare. Exposed so the configuration
 * UI can say no before the operator commits. */
bool mc_channel_name_valid(const char *name);

/*
 * Publishes a reading. Applies scale and offset, stamps both clocks and bumps
 * the sequence number. Silently ignores an undeclared name -- a survey system
 * sending a field nobody mapped is normal, not an error.
 */
void mc_registry_publish(mc_registry_t *registry, const char *name, double numeric, const char *text,
			 mc_quality_t quality);

/*
 * Reads one channel. Returns false if the name was never declared.
 *
 * Quality is resolved here: a value whose age exceeds its timeout reads as
 * MC_QUALITY_STALE regardless of how it was published.
 */
bool mc_registry_read(mc_registry_t *registry, const char *name, mc_value_t *out);

/* Number of declared channels. */
size_t mc_registry_count(mc_registry_t *registry);

/*
 * Copies every channel under one lock, so an overlay frame cannot show two
 * fields from different instants. Returns the number written; `names` and
 * `values` must each hold `max` entries.
 */
size_t mc_registry_snapshot(mc_registry_t *registry, char names[][MC_NAME_MAX + 1], mc_value_t *values, size_t max);

/* Forgets every channel. Test support, and a Job switch later. */
void mc_registry_clear(mc_registry_t *registry);

#ifdef __cplusplus
}
#endif
