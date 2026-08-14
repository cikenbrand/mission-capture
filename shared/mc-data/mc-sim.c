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

#include "mc-data/mc-sim.h"

#include <util/bmem.h>
#include <util/platform.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MC_SIM_LINE_MAX 512
#define MC_SIM_PENDING_MAX (MC_SIM_LINE_MAX + MC_SIM_TERMINATOR_MAX)
#define MC_SIM_DEFAULT_SEED 0x9E3779B97F4A7C15ULL

struct sim_line {
	const char *text;
	size_t len;
	uint64_t delta_ms; /* honour_timestamps only */
};

struct mc_sim {
	mc_sim_config_t cfg;

	/* File mode: the whole file, with lines pointing into it. Fixtures are
	 * small and this keeps the read path allocation-free. */
	char *file_data;
	struct sim_line *lines;
	size_t line_count;
	size_t next_line;

	/* Bytes produced but not yet collected. A caller with a small buffer must
	 * change the chunking, never the content. */
	uint8_t pending[MC_SIM_PENDING_MAX];
	size_t pending_len;
	size_t pending_taken;

	bool started;
	uint64_t start_ns;
	uint64_t next_due_ns;

	uint64_t emitted; /* lines produced, for fault counting */
	uint64_t lines_out;
	uint64_t dropped;

	uint64_t rng;
	double ramp_phase;

	bool exhausted;
};

/* --- deterministic randomness ------------------------------------------- */

static uint64_t next_random(mc_sim_t *sim)
{
	/* xorshift64*. Small, fast, and identical on every platform -- which is
	 * the only property that matters when the goal is reproducing a fault. */
	uint64_t x = sim->rng;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	sim->rng = x;
	return x * 0x2545F4914F6CDD1DULL;
}

static double random_in(mc_sim_t *sim, double min, double max)
{
	const double unit = (double)(next_random(sim) >> 11) / (double)(1ULL << 53);
	return min + unit * (max - min);
}

/* --- file loading -------------------------------------------------------- */

static bool load_file(mc_sim_t *sim, const char *path)
{
	sim->file_data = os_quick_read_utf8_file(path);
	if (!sim->file_data) {
		return false;
	}

	/* Two passes: count, then fill. The file is already in memory, so this is
	 * cheaper than growing an array and much easier to read. */
	size_t count = 0;
	for (char *c = sim->file_data; *c;) {
		char *end = strpbrk(c, "\r\n");
		if (!end) {
			if (*c) {
				count++;
			}
			break;
		}
		if (end != c) {
			count++;
		}
		c = end + ((end[0] == '\r' && end[1] == '\n') ? 2 : 1);
	}

	if (count == 0) {
		return false;
	}

	sim->lines = bzalloc(sizeof(struct sim_line) * count);
	sim->line_count = 0;

	for (char *c = sim->file_data; *c;) {
		char *end = strpbrk(c, "\r\n");
		const size_t len = end ? (size_t)(end - c) : strlen(c);

		if (len > 0) {
			struct sim_line *line = &sim->lines[sim->line_count];
			line->text = c;
			line->len = len;
			line->delta_ms = 0;

			if (sim->cfg.honour_timestamps) {
				/* "delta_ms,payload" -- the capture format. */
				const char *comma = memchr(c, ',', len);
				if (!comma) {
					return false;
				}
				line->delta_ms = strtoull(c, NULL, 10);
				line->text = comma + 1;
				line->len = len - (size_t)(comma + 1 - c);
			}

			sim->line_count++;
		}

		if (!end) {
			break;
		}
		c = end + ((end[0] == '\r' && end[1] == '\n') ? 2 : 1);
	}

	return sim->line_count > 0;
}

/* --- construction -------------------------------------------------------- */

mc_sim_t *mc_sim_create(const mc_sim_config_t *config)
{
	if (!config) {
		return NULL;
	}

	const bool timed = config->mode == MC_SIM_FILE && config->honour_timestamps;
	if (!timed && !(config->rate_hz > 0.0)) {
		return NULL;
	}

	if (config->mode == MC_SIM_SYNTHETIC &&
	    (config->channel_count == 0 || config->channel_count > MC_SIM_MAX_CHANNELS)) {
		return NULL;
	}

	if (config->terminator_len > MC_SIM_TERMINATOR_MAX) {
		return NULL;
	}

	mc_sim_t *sim = bzalloc(sizeof(*sim));
	sim->cfg = *config;
	sim->rng = config->seed ? config->seed : MC_SIM_DEFAULT_SEED;

	if (sim->cfg.terminator_len == 0) {
		sim->cfg.terminator[0] = '\r';
		sim->cfg.terminator[1] = '\n';
		sim->cfg.terminator_len = 2;
	}

	if (sim->cfg.mode == MC_SIM_SYNTHETIC && sim->cfg.separator == 0) {
		sim->cfg.separator = ',';
	}

	if (config->mode == MC_SIM_FILE) {
		if (!config->path || !load_file(sim, config->path)) {
			mc_sim_destroy(sim);
			return NULL;
		}
	}

	return sim;
}

void mc_sim_destroy(mc_sim_t *sim)
{
	if (!sim) {
		return;
	}
	bfree(sim->lines);
	bfree(sim->file_data);
	bfree(sim);
}

/* --- line generation ----------------------------------------------------- */

static size_t synthetic_line(mc_sim_t *sim, double elapsed_s, char *out, size_t out_size)
{
	size_t used = 0;

	for (size_t i = 0; i < sim->cfg.channel_count && used + 1 < out_size; i++) {
		const mc_sim_channel_t *ch = &sim->cfg.channels[i];
		const double span = ch->max - ch->min;
		double value;

		switch (ch->wave) {
		case MC_WAVE_SINE: {
			const double period = ch->period_s > 0.0 ? ch->period_s : 1.0;
			const double phase = 2.0 * 3.14159265358979323846 * elapsed_s / period;
			value = ch->min + span * 0.5 * (1.0 + sin(phase));
			break;
		}
		case MC_WAVE_RAMP: {
			const double period = ch->period_s > 0.0 ? ch->period_s : 1.0;
			double t = fmod(elapsed_s, period) / period;
			value = ch->min + span * t;
			break;
		}
		case MC_WAVE_RANDOM:
			value = random_in(sim, ch->min, ch->max);
			break;
		case MC_WAVE_CONSTANT:
		default:
			value = ch->min;
			break;
		}

		char sep[2] = {0, 0};
		if (i > 0) {
			sep[0] = sim->cfg.separator;
		}

		const int n = snprintf(out + used, out_size - used, "%s%.*f", sep, ch->precision, value);
		if (n < 0 || (size_t)n >= out_size - used) {
			break;
		}
		used += (size_t)n;
	}

	return used;
}

/* Replaces one field with something that will not parse. */
static size_t malform(mc_sim_t *sim, char *line, size_t len)
{
	/* Find field boundaries, then corrupt one of them. Corrupting a whole line
	 * would only ever exercise "the row is junk"; corrupting one field is the
	 * case the parser makes promises about. */
	size_t starts[MC_SIM_MAX_CHANNELS + 1];
	size_t count = 0;
	starts[count++] = 0;

	for (size_t i = 0; i < len && count <= MC_SIM_MAX_CHANNELS; i++) {
		if (line[i] == ',') {
			starts[count++] = i + 1;
		}
	}

	const size_t target = starts[next_random(sim) % count];
	size_t end = target;
	while (end < len && line[end] != ',') {
		end++;
	}

	const size_t field_len = end - target;
	if (field_len == 0) {
		return len;
	}

	memset(line + target, '#', field_len);
	return len;
}

/* --- the read path ------------------------------------------------------- */

static uint64_t interval_ns(const mc_sim_t *sim, size_t line_index)
{
	if (sim->cfg.mode == MC_SIM_FILE && sim->cfg.honour_timestamps) {
		return sim->lines[line_index].delta_ms * 1000000ULL;
	}
	return (uint64_t)(1000000000.0 / sim->cfg.rate_hz);
}

static bool stalled(const mc_sim_t *sim, uint64_t now_ns)
{
	const mc_sim_faults_t *f = &sim->cfg.faults;
	if (f->stall_for_ms == 0) {
		return false;
	}

	const uint64_t elapsed_ms = (now_ns - sim->start_ns) / 1000000ULL;
	return elapsed_ms >= f->stall_after_ms && elapsed_ms < f->stall_after_ms + f->stall_for_ms;
}

/* Builds the next line into `pending`. Returns false when there is nothing
 * more to produce. */
static bool produce(mc_sim_t *sim, uint64_t now_ns)
{
	char line[MC_SIM_LINE_MAX];
	size_t len = 0;

	if (sim->cfg.mode == MC_SIM_FILE) {
		if (sim->next_line >= sim->line_count) {
			if (!sim->cfg.loop) {
				sim->exhausted = true;
				return false;
			}
			sim->next_line = 0;
		}

		const struct sim_line *src = &sim->lines[sim->next_line];
		len = src->len < MC_SIM_LINE_MAX ? src->len : MC_SIM_LINE_MAX;
		memcpy(line, src->text, len);
		sim->next_line++;
	} else {
		const double elapsed_s = (double)(now_ns - sim->start_ns) / 1000000000.0;
		len = synthetic_line(sim, elapsed_s, line, sizeof(line));
	}

	sim->emitted++;

	const mc_sim_faults_t *f = &sim->cfg.faults;

	if (f->drop_every_n && (sim->emitted % f->drop_every_n) == 0) {
		sim->dropped++;
		return true; /* time advances, bytes do not */
	}

	if (f->malform_every_n && (sim->emitted % f->malform_every_n) == 0) {
		len = malform(sim, line, len);
	}

	bool partial = f->partial_every_n && (sim->emitted % f->partial_every_n) == 0;
	if (partial && len > 1) {
		/* Cut it short AND withhold the terminator, so it runs into the next
		 * line. Emitting half a line with a terminator would just be a short
		 * row; this is the fragment case. */
		len /= 2;
	}

	memcpy(sim->pending, line, len);
	sim->pending_len = len;

	if (!partial) {
		memcpy(sim->pending + len, sim->cfg.terminator, sim->cfg.terminator_len);
		sim->pending_len += sim->cfg.terminator_len;
	}

	sim->pending_taken = 0;
	sim->lines_out++;
	return true;
}

size_t mc_sim_read(mc_sim_t *sim, uint64_t now_ns, uint8_t *out, size_t out_size)
{
	if (!sim || !out || out_size == 0) {
		return 0;
	}

	if (!sim->started) {
		sim->started = true;
		sim->start_ns = now_ns;
		sim->next_due_ns = now_ns + interval_ns(sim, 0);
	}

	size_t written = 0;

	while (written < out_size) {
		/* Hand back anything already produced first. A caller with a small
		 * buffer changes the chunking, never the content. */
		if (sim->pending_taken < sim->pending_len) {
			const size_t available = sim->pending_len - sim->pending_taken;
			const size_t room = out_size - written;
			const size_t take = available < room ? available : room;

			memcpy(out + written, sim->pending + sim->pending_taken, take);
			sim->pending_taken += take;
			written += take;
			continue;
		}

		if (now_ns < sim->next_due_ns || sim->exhausted) {
			break;
		}

		if (stalled(sim, now_ns)) {
			/* Deliberately silent, and the clock keeps moving. A stalled
			 * device is indistinguishable from a slow one until something
			 * times out, and that is the situation worth reproducing. */
			break;
		}

		if (!produce(sim, now_ns)) {
			break;
		}

		const size_t index = sim->next_line < sim->line_count ? sim->next_line : 0;
		sim->next_due_ns += interval_ns(sim, index);
	}

	return written;
}

uint64_t mc_sim_lines(const mc_sim_t *sim)
{
	return sim ? sim->lines_out : 0;
}

uint64_t mc_sim_dropped(const mc_sim_t *sim)
{
	return sim ? sim->dropped : 0;
}

bool mc_sim_exhausted(const mc_sim_t *sim)
{
	return sim ? (sim->exhausted && sim->pending_taken >= sim->pending_len) : false;
}
