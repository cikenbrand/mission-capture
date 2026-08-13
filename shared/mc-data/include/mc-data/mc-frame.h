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
 * The frame assembler: a byte stream in, discrete frames out.
 *
 * WHY THIS IS ITS OWN THING
 * -------------------------
 * A serial port does not deliver messages, it delivers bytes, whenever it feels
 * like it. One read can carry half a frame, six frames, or the tail of one and
 * the head of the next. Everything downstream -- parser, registry, overlay --
 * gets much simpler if exactly one place is responsible for that, and this is
 * that place.
 *
 * The contract that matters: **framing does not depend on how the stream was
 * chopped up**. The same bytes delivered one at a time and delivered in one
 * block produce the same frames. `test_mc_frame.c` asserts this exhaustively
 * rather than trusting it, because this is the classic place bugs hide.
 *
 * THREADING
 * ---------
 * Deliberately NOT thread-safe, unlike the channel registry. An assembler
 * belongs to the one transport thread reading its port, start to finish. Adding
 * a lock here would protect against a situation that should never be built.
 *
 * TIME IS PASSED IN, NOT READ
 * ---------------------------
 * `now_ns` is an argument rather than an `os_gettime_ns()` call inside, so idle
 * timeout framing can be tested to the nanosecond without a single sleep.
 */

/* Hard ceiling on a frame, whatever the configured maximum. The buffer is a
 * fixed array of this size: no allocation on the byte path, and a device
 * spewing without a delimiter cannot grow it. */
#define MC_FRAME_MAX 1024

/* Longest configurable delimiter, e.g. "\r\n". */
#define MC_DELIM_MAX 8

typedef enum {
	MC_FRAME_DELIMITER, /* frames end with a byte sequence -- the common case */
	MC_FRAME_FIXED,     /* every frame is exactly N bytes */
	MC_FRAME_SENTINEL,  /* frames run from a start byte to an end byte, NMEA-style */
	MC_FRAME_IDLE,      /* no framing at all: N ms of silence ends the frame */
} mc_frame_mode_t;

typedef struct {
	mc_frame_mode_t mode;

	/* MC_FRAME_DELIMITER. Bytes, not a C string -- a delimiter may contain a
	 * zero -- so the length is explicit. */
	uint8_t delimiter[MC_DELIM_MAX];
	size_t delimiter_len;

	/* MC_FRAME_FIXED. */
	size_t fixed_length;

	/* MC_FRAME_SENTINEL. */
	uint8_t start_sentinel;
	uint8_t end_sentinel;

	/* MC_FRAME_IDLE. */
	uint64_t idle_timeout_ms;

	/* Longest acceptable frame. Zero means MC_FRAME_MAX. A frame that grows
	 * past this is dropped and the assembler resynchronises; it is never
	 * delivered truncated, because half a survey string parses as a plausible
	 * position rather than as an obvious fault. */
	size_t max_frame_length;
} mc_frame_config_t;

typedef struct mc_frame_assembler mc_frame_assembler_t;

/*
 * Called once per complete frame, from inside mc_frame_push or mc_frame_tick.
 *
 * `frame` points into assembler storage and is valid only for the duration of
 * the call -- copy anything worth keeping. Framing bytes are stripped: no
 * delimiter, no sentinels.
 */
typedef void (*mc_frame_cb)(void *ctx, const uint8_t *frame, size_t len);

/* Returns NULL if the configuration is unusable -- a zero-length delimiter, a
 * fixed length past MC_FRAME_MAX, and so on. */
mc_frame_assembler_t *mc_frame_create(const mc_frame_config_t *config);
void mc_frame_destroy(mc_frame_assembler_t *assembler);

/*
 * Feeds bytes. Emits every frame those bytes completed, in order, and keeps any
 * partial remainder for the next call.
 *
 * `now_ns` is only consulted in MC_FRAME_IDLE mode.
 */
void mc_frame_push(mc_frame_assembler_t *assembler, const uint8_t *data, size_t len, uint64_t now_ns, mc_frame_cb cb,
		   void *ctx);

/*
 * Gives the assembler a chance to act on the passage of time. Only
 * MC_FRAME_IDLE does anything: it emits a pending frame once the line has been
 * quiet for the configured timeout. Harmless and cheap in every other mode, so
 * a transport can call it unconditionally on every poll.
 */
void mc_frame_tick(mc_frame_assembler_t *assembler, uint64_t now_ns, mc_frame_cb cb, void *ctx);

/* Drops any partial frame and returns to hunting. For a port that was just
 * reopened, where whatever is buffered predates the reconnect. */
void mc_frame_reset(mc_frame_assembler_t *assembler);

/* Frames delivered. */
uint64_t mc_frame_count(const mc_frame_assembler_t *assembler);

/*
 * Frames thrown away: too long, or interrupted by a fresh start sentinel. Worth
 * surfacing in the health panel -- a steadily climbing count is how a wrong
 * baud rate or a wrong delimiter looks from the outside.
 */
uint64_t mc_frame_dropped(const mc_frame_assembler_t *assembler);

#ifdef __cplusplus
}
#endif
