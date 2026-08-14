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
 * The simulator transport: bytes on demand, with no port and no device.
 *
 * This is the workhorse for every later phase's tests. It stands exactly where
 * a serial transport will stand -- hand it a clock, it hands back bytes -- so
 * the frame assembler, the parser and eventually the overlay can all be driven
 * without hardware.
 *
 * TIME IS AN ARGUMENT
 * -------------------
 * Same choice as the frame assembler, for the same reason. `mc_sim_read` is
 * told what time it is rather than asking, so a test can replay an hour of a
 * survey in a millisecond, and a 30-second stall costs nothing to assert.
 *
 * REPRODUCIBILITY IS THE POINT
 * ----------------------------
 * Fault injection is worth more than any other test tool here, and only if a
 * bad link reproduces exactly. Everything random is driven by a seeded PRNG
 * carried in the configuration, so the same seed gives the same corrupted
 * bytes on every machine and every run.
 */

#define MC_SIM_MAX_CHANNELS 16
#define MC_SIM_TERMINATOR_MAX 4

typedef enum {
	MC_SIM_FILE,      /* replay a captured text file */
	MC_SIM_SYNTHETIC, /* generate values per channel */
} mc_sim_mode_t;

typedef enum {
	MC_WAVE_CONSTANT,
	MC_WAVE_SINE,
	MC_WAVE_RAMP,
	MC_WAVE_RANDOM,
} mc_wave_t;

typedef struct {
	mc_wave_t wave;
	double min;
	double max;
	double period_s; /* SINE and RAMP; ignored otherwise */
	int precision;   /* decimal places emitted */
} mc_sim_channel_t;

/*
 * Ways to break the link. Every "every_n" counts emitted lines and is disabled
 * at zero.
 */
typedef struct {
	/* Line vanishes entirely. What a dropped serial frame looks like. */
	uint32_t drop_every_n;

	/* A field is replaced with something unparseable. Exercises the parser's
	 * promise that one bad field does not cost the other channels. */
	uint32_t malform_every_n;

	/* Line is cut short and its terminator withheld, so it runs into the next
	 * one. This is how a mid-stream join and OI-64 are reproduced on a desk. */
	uint32_t partial_every_n;

	/* The line goes quiet. Not an error, and that is the difficult part: a
	 * stalled device looks identical to a slow one until the timeout expires. */
	uint64_t stall_after_ms;
	uint64_t stall_for_ms;
} mc_sim_faults_t;

typedef struct {
	mc_sim_mode_t mode;

	/* Lines per second. Ignored in file mode when honour_timestamps is set. */
	double rate_hz;

	bool loop;

	/* --- MC_SIM_FILE --- */

	const char *path;

	/*
	 * The file's lines carry a leading "delta_ms," prefix giving the gap since
	 * the previous line, and playback honours it. This is what a capture from
	 * a real device replays as, jitter included -- a device that is nominally
	 * 1 Hz is not exactly 1 Hz, and code that only ever saw exact intervals
	 * has not been tested against anything real.
	 */
	bool honour_timestamps;

	/* --- MC_SIM_SYNTHETIC --- */

	mc_sim_channel_t channels[MC_SIM_MAX_CHANNELS];
	size_t channel_count;
	char separator;

	/* --- both --- */

	uint8_t terminator[MC_SIM_TERMINATOR_MAX];
	size_t terminator_len; /* zero means "\r\n" */

	mc_sim_faults_t faults;

	/* Any nonzero value reproduces exactly. Zero picks a fixed default rather
	 * than a time-based one, because a test that is only sometimes the same
	 * test is not a test. */
	uint64_t seed;
} mc_sim_config_t;

typedef struct mc_sim mc_sim_t;

/* NULL if the configuration cannot produce output: a missing or unreadable
 * file, a synthetic mode with no channels, a non-positive rate. */
mc_sim_t *mc_sim_create(const mc_sim_config_t *config);
void mc_sim_destroy(mc_sim_t *sim);

/*
 * Produces the bytes that would have arrived by `now_ns`, up to `out_size`.
 * Returns how many were written. Zero is normal and means nothing was due yet.
 *
 * Bytes not taken this call are kept and returned by the next one, so a small
 * buffer changes the chunking and never the content -- which is precisely the
 * property the frame assembler was built to survive.
 */
size_t mc_sim_read(mc_sim_t *sim, uint64_t now_ns, uint8_t *out, size_t out_size);

/* Lines emitted, and lines withheld by drop or stall. */
uint64_t mc_sim_lines(const mc_sim_t *sim);
uint64_t mc_sim_dropped(const mc_sim_t *sim);

/* True once a non-looping file has been played out and drained. */
bool mc_sim_exhausted(const mc_sim_t *sim);

#ifdef __cplusplus
}
#endif
