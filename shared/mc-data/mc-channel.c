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

#include "mc-data/mc-channel.h"

#include <util/platform.h>
#include <util/threading.h>

#include <math.h>
#include <string.h>

/*
 * A flat array rather than a hash table.
 *
 * The ceiling is 64 channels (docs/subsea/phase-3-data-core.md). A linear scan
 * over 64 short strings is a handful of cache lines and beats a hash table on a
 * set this small -- and it keeps snapshot ordering stable, which a hash table
 * would not. If that ceiling ever moves by an order of magnitude this is the
 * thing to revisit, and nothing outside this file changes when it does.
 */
#define MC_MAX_CHANNELS 64

struct mc_channel {
	mc_channel_def_t def;
	mc_value_t value;
	bool received; /* separates "declared, never published" from a real 0.0 */
};

struct mc_registry {
	pthread_mutex_t mutex;
	struct mc_channel channels[MC_MAX_CHANNELS];
	size_t count;
};

static struct mc_registry global_registry;
static bool global_initialised = false;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

mc_registry_t *mc_registry_get(void)
{
	pthread_mutex_lock(&init_mutex);
	if (!global_initialised) {
		memset(&global_registry, 0, sizeof(global_registry));
		pthread_mutex_init(&global_registry.mutex, NULL);
		global_initialised = true;
	}
	pthread_mutex_unlock(&init_mutex);

	return &global_registry;
}

bool mc_channel_name_valid(const char *name)
{
	if (!name || !*name) {
		return false;
	}

	size_t length = 0;
	for (const char *c = name; *c; c++, length++) {
		const char ch = *c;
		const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
				ch == '_';
		if (!ok) {
			return false;
		}
	}

	return length <= MC_NAME_MAX;
}

/* Caller holds the lock. */
static struct mc_channel *find_channel(mc_registry_t *registry, const char *name)
{
	for (size_t i = 0; i < registry->count; i++) {
		if (strcmp(registry->channels[i].def.name, name) == 0) {
			return &registry->channels[i];
		}
	}
	return NULL;
}

bool mc_registry_declare(mc_registry_t *registry, const mc_channel_def_t *def)
{
	if (!registry || !def || !mc_channel_name_valid(def->name)) {
		return false;
	}

	pthread_mutex_lock(&registry->mutex);

	struct mc_channel *existing = find_channel(registry, def->name);
	if (existing) {
		/*
		 * Re-declaring updates the definition and keeps the reading. Loading
		 * a config that already matches what is on screen should not blank
		 * the overlay for a second.
		 */
		existing->def = *def;
		pthread_mutex_unlock(&registry->mutex);
		return true;
	}

	if (registry->count >= MC_MAX_CHANNELS) {
		pthread_mutex_unlock(&registry->mutex);
		return false;
	}

	struct mc_channel *channel = &registry->channels[registry->count++];
	memset(channel, 0, sizeof(*channel));
	channel->def = *def;
	channel->value.numeric = NAN;
	channel->value.quality = MC_QUALITY_NODATA;

	pthread_mutex_unlock(&registry->mutex);
	return true;
}

void mc_registry_publish(mc_registry_t *registry, const char *name, double numeric, const char *text,
			 mc_quality_t quality)
{
	if (!registry || !name) {
		return;
	}

	pthread_mutex_lock(&registry->mutex);

	struct mc_channel *channel = find_channel(registry, name);
	if (!channel) {
		/* A field nobody mapped. Normal: survey strings carry more than any
		 * one job cares about, and dropping the extras is the correct
		 * response rather than an error worth reporting. */
		pthread_mutex_unlock(&registry->mutex);
		return;
	}

	/* Scale and offset applied once, here, so no overlay and no export has to.
	 * A bad reading is left alone -- scaling garbage produces confident
	 * garbage, which is worse than an obvious gap. */
	if (quality == MC_QUALITY_BAD || isnan(numeric)) {
		channel->value.numeric = NAN;
	} else {
		channel->value.numeric = numeric * channel->def.scale + channel->def.offset;
	}

	if (text) {
		strncpy(channel->value.text, text, MC_TEXT_MAX);
		channel->value.text[MC_TEXT_MAX] = '\0';
	} else {
		channel->value.text[0] = '\0';
	}

	channel->value.ts_ns = os_gettime_ns();
	channel->value.wall_ns = os_gettime_ns();
	channel->value.quality = quality;
	channel->value.seq++;
	channel->received = true;

	pthread_mutex_unlock(&registry->mutex);
}

/* Caller holds the lock. Staleness is decided here rather than by a timer. */
static mc_quality_t effective_quality(const struct mc_channel *channel, uint64_t now_ns)
{
	if (!channel->received) {
		return MC_QUALITY_NODATA;
	}
	if (channel->value.quality == MC_QUALITY_BAD) {
		return MC_QUALITY_BAD;
	}
	if (channel->def.stale_timeout_ms == 0) {
		return channel->value.quality;
	}

	const uint64_t age_ms = (now_ns - channel->value.ts_ns) / 1000000ULL;
	return age_ms > channel->def.stale_timeout_ms ? MC_QUALITY_STALE : channel->value.quality;
}

bool mc_registry_read(mc_registry_t *registry, const char *name, mc_value_t *out)
{
	if (!registry || !name || !out) {
		return false;
	}

	pthread_mutex_lock(&registry->mutex);

	struct mc_channel *channel = find_channel(registry, name);
	if (!channel) {
		pthread_mutex_unlock(&registry->mutex);
		return false;
	}

	*out = channel->value;
	out->quality = effective_quality(channel, os_gettime_ns());

	pthread_mutex_unlock(&registry->mutex);
	return true;
}

size_t mc_registry_count(mc_registry_t *registry)
{
	if (!registry) {
		return 0;
	}

	pthread_mutex_lock(&registry->mutex);
	const size_t count = registry->count;
	pthread_mutex_unlock(&registry->mutex);
	return count;
}

size_t mc_registry_snapshot(mc_registry_t *registry, char names[][MC_NAME_MAX + 1], mc_value_t *values, size_t max)
{
	if (!registry || !names || !values) {
		return 0;
	}

	pthread_mutex_lock(&registry->mutex);

	/*
	 * One lock for the whole set. An overlay frame showing depth from one
	 * instant beside heading from another would be wrong in a way nobody could
	 * spot afterwards, which is the worst kind of wrong for a record that ends
	 * up with a client.
	 */
	const uint64_t now = os_gettime_ns();
	size_t written = 0;

	for (size_t i = 0; i < registry->count && written < max; i++, written++) {
		strncpy(names[written], registry->channels[i].def.name, MC_NAME_MAX);
		names[written][MC_NAME_MAX] = '\0';

		values[written] = registry->channels[i].value;
		values[written].quality = effective_quality(&registry->channels[i], now);
	}

	pthread_mutex_unlock(&registry->mutex);
	return written;
}

void mc_registry_clear(mc_registry_t *registry)
{
	if (!registry) {
		return;
	}

	pthread_mutex_lock(&registry->mutex);
	registry->count = 0;
	memset(registry->channels, 0, sizeof(registry->channels));
	pthread_mutex_unlock(&registry->mutex);
}
