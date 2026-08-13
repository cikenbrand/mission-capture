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

#include "mc-data/mc-frame.h"

#include <util/bmem.h>

#include <string.h>

/*
 * Byte at a time.
 *
 * Four devices at 10 Hz sending short strings is a few hundred bytes a second
 * (docs/subsea/phase-3-data-core.md), so there is no argument for a cleverer
 * loop -- and a cleverer loop is exactly how "works unless the delimiter lands
 * on a read boundary" bugs get written. Correctness under arbitrary chunking is
 * the whole job here.
 */

struct mc_frame_assembler {
	mc_frame_config_t cfg;
	size_t max; /* resolved: cfg.max_frame_length, or MC_FRAME_MAX */

	uint8_t buf[MC_FRAME_MAX];
	size_t len;

	/* DELIMITER and IDLE: dropping bytes until the next delimiter, having
	 * already given up on the frame in progress. */
	bool discarding;

	/* SENTINEL: a start sentinel has been seen and not yet closed. */
	bool in_frame;

	/* IDLE: when the last byte arrived, so silence can be measured. */
	uint64_t last_byte_ns;
	bool have_bytes;

	uint64_t frames;
	uint64_t dropped;
};

mc_frame_assembler_t *mc_frame_create(const mc_frame_config_t *config)
{
	if (!config) {
		return NULL;
	}

	size_t max = config->max_frame_length ? config->max_frame_length : MC_FRAME_MAX;
	if (max > MC_FRAME_MAX) {
		max = MC_FRAME_MAX;
	}

	/* Reject a configuration that could never produce a frame, at create time
	 * rather than silently at sea. */
	switch (config->mode) {
	case MC_FRAME_DELIMITER:
		if (config->delimiter_len == 0 || config->delimiter_len > MC_DELIM_MAX) {
			return NULL;
		}
		break;
	case MC_FRAME_FIXED:
		if (config->fixed_length == 0 || config->fixed_length > max) {
			return NULL;
		}
		break;
	case MC_FRAME_SENTINEL:
		if (config->start_sentinel == config->end_sentinel) {
			return NULL;
		}
		break;
	case MC_FRAME_IDLE:
		if (config->idle_timeout_ms == 0) {
			return NULL;
		}
		break;
	default:
		return NULL;
	}

	mc_frame_assembler_t *assembler = bzalloc(sizeof(*assembler));
	assembler->cfg = *config;
	assembler->max = max;
	return assembler;
}

void mc_frame_destroy(mc_frame_assembler_t *assembler)
{
	bfree(assembler);
}

static void emit(mc_frame_assembler_t *assembler, size_t len, mc_frame_cb cb, void *ctx)
{
	assembler->frames++;
	if (cb) {
		cb(ctx, assembler->buf, len);
	}
}

static void drop(mc_frame_assembler_t *assembler)
{
	assembler->dropped++;
	assembler->len = 0;
}

/* True once the tail of the buffer matches the configured delimiter. */
static bool ends_with_delimiter(const mc_frame_assembler_t *assembler)
{
	const size_t dlen = assembler->cfg.delimiter_len;
	if (assembler->len < dlen) {
		return false;
	}
	return memcmp(assembler->buf + assembler->len - dlen, assembler->cfg.delimiter, dlen) == 0;
}

static void push_delimiter(mc_frame_assembler_t *assembler, uint8_t byte, mc_frame_cb cb, void *ctx)
{
	const size_t dlen = assembler->cfg.delimiter_len;

	if (assembler->discarding) {
		/*
		 * Still looking for the end of the frame we gave up on. Keep only
		 * enough tail to recognise the delimiter -- a sliding window, so a
		 * device with no delimiter at all cannot make this grow.
		 */
		if (assembler->len == dlen) {
			memmove(assembler->buf, assembler->buf + 1, dlen - 1);
			assembler->len = dlen - 1;
		}
		assembler->buf[assembler->len++] = byte;

		if (ends_with_delimiter(assembler)) {
			assembler->discarding = false;
			assembler->len = 0;
		}
		return;
	}

	assembler->buf[assembler->len++] = byte;

	if (ends_with_delimiter(assembler)) {
		const size_t payload = assembler->len - dlen;

		/* A blank line is not a reading. Emitting it would hand the parser a
		 * row of nothing, which becomes a row of BAD values on screen. */
		if (payload > 0) {
			emit(assembler, payload, cb, ctx);
		}
		assembler->len = 0;
		return;
	}

	if (assembler->len >= assembler->max) {
		/* Never deliver a truncated frame: half a survey string parses as a
		 * plausible position rather than as an obvious fault. */
		drop(assembler);
		assembler->discarding = true;
	}
}

static void push_fixed(mc_frame_assembler_t *assembler, uint8_t byte, mc_frame_cb cb, void *ctx)
{
	assembler->buf[assembler->len++] = byte;

	if (assembler->len == assembler->cfg.fixed_length) {
		emit(assembler, assembler->len, cb, ctx);
		assembler->len = 0;
	}
}

static void push_sentinel(mc_frame_assembler_t *assembler, uint8_t byte, mc_frame_cb cb, void *ctx)
{
	if (!assembler->in_frame) {
		/* Anything before the first start sentinel is noise: a half frame
		 * left in the driver buffer, or line garbage from power-up. */
		if (byte == assembler->cfg.start_sentinel) {
			assembler->in_frame = true;
			assembler->len = 0;
		}
		return;
	}

	if (byte == assembler->cfg.end_sentinel) {
		if (assembler->len > 0) {
			emit(assembler, assembler->len, cb, ctx);
		}
		assembler->in_frame = false;
		assembler->len = 0;
		return;
	}

	if (byte == assembler->cfg.start_sentinel) {
		/* A new frame began before the last one ended -- the device reset, or
		 * bytes were lost. The partial is unusable, but the new start is
		 * good, so resynchronise here rather than waiting for an end byte
		 * that is never coming. */
		drop(assembler);
		return;
	}

	assembler->buf[assembler->len++] = byte;

	if (assembler->len >= assembler->max) {
		drop(assembler);
		assembler->in_frame = false;
	}
}

static void push_idle(mc_frame_assembler_t *assembler, uint8_t byte)
{
	assembler->buf[assembler->len++] = byte;

	if (assembler->len >= assembler->max) {
		drop(assembler);
	}
}

void mc_frame_push(mc_frame_assembler_t *assembler, const uint8_t *data, size_t len, uint64_t now_ns, mc_frame_cb cb,
		   void *ctx)
{
	if (!assembler || (!data && len > 0)) {
		return;
	}

	for (size_t i = 0; i < len; i++) {
		switch (assembler->cfg.mode) {
		case MC_FRAME_DELIMITER:
			push_delimiter(assembler, data[i], cb, ctx);
			break;
		case MC_FRAME_FIXED:
			push_fixed(assembler, data[i], cb, ctx);
			break;
		case MC_FRAME_SENTINEL:
			push_sentinel(assembler, data[i], cb, ctx);
			break;
		case MC_FRAME_IDLE:
			push_idle(assembler, data[i]);
			break;
		}
	}

	if (len > 0) {
		assembler->last_byte_ns = now_ns;
		assembler->have_bytes = true;
	}
}

void mc_frame_tick(mc_frame_assembler_t *assembler, uint64_t now_ns, mc_frame_cb cb, void *ctx)
{
	if (!assembler || assembler->cfg.mode != MC_FRAME_IDLE) {
		return;
	}
	if (assembler->len == 0 || !assembler->have_bytes) {
		return;
	}

	const uint64_t quiet_ms = (now_ns - assembler->last_byte_ns) / 1000000ULL;
	if (quiet_ms >= assembler->cfg.idle_timeout_ms) {
		emit(assembler, assembler->len, cb, ctx);
		assembler->len = 0;
	}
}

void mc_frame_reset(mc_frame_assembler_t *assembler)
{
	if (!assembler) {
		return;
	}

	assembler->len = 0;
	assembler->discarding = false;
	assembler->in_frame = false;
	assembler->have_bytes = false;
}

uint64_t mc_frame_count(const mc_frame_assembler_t *assembler)
{
	return assembler ? assembler->frames : 0;
}

uint64_t mc_frame_dropped(const mc_frame_assembler_t *assembler)
{
	return assembler ? assembler->dropped : 0;
}
