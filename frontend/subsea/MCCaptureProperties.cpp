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

#include "MCCaptureProperties.hpp"

#include <util/base.h>

#include <array>
#include <cstring>

namespace MCCaptureProperties {

namespace {

bool advanced = false;
int hiddenLastRun = 0;

/*
 * Kept visible. Names verified against the plugins rather than guessed:
 *   plugins/win-dshow/win-dshow.cpp
 *   plugins/decklink/const.h
 *
 * The two backends share several names (color_space, color_range, buffering,
 * deactivate_when_not_showing), so one list covers both. A name that does not
 * exist on a given backend simply never matches.
 */
constexpr std::array<const char *, 10> keep{{
	/* Which camera. */
	"video_device_id", /* DirectShow */
	"device_hash",     /* DeckLink */

	/* What it is sending. On DeckLink mode_id is the auto-detect toggle
	 * itself, which the plan asks for by name. */
	"res_type",       /* DirectShow: Device Default / Custom */
	"resolution",     /* DirectShow, shown when res_type is Custom */
	"frame_interval", /* DirectShow: FPS */
	"mode_id",        /* DeckLink: Auto, or a pinned format */

	/* The washed-out-video pair. */
	"color_space",
	"color_range",

	/* Audio in, and latency. */
	"audio_output_mode", /* DirectShow */
	"buffering",
}};

bool isKept(const char *name)
{
	if (!name) {
		return true;
	}
	for (const char *candidate : keep) {
		if (strcmp(candidate, name) == 0) {
			return true;
		}
	}
	return false;
}

} // namespace

bool isCaptureSource(const char *sourceId)
{
	if (!sourceId) {
		return false;
	}
	return strcmp(sourceId, "dshow_input") == 0 || strcmp(sourceId, "decklink-input") == 0;
}

bool advancedShown()
{
	return advanced;
}

void setAdvancedShown(bool shown)
{
	advanced = shown;
}

int lastHiddenCount()
{
	return hiddenLastRun;
}

obs_properties_t *reload(void *source)
{
	auto *src = static_cast<obs_source_t *>(source);
	obs_properties_t *props = obs_source_properties(src);

	hiddenLastRun = 0;

	if (!props || advanced || !isCaptureSource(obs_source_get_id(src))) {
		return props;
	}

	for (obs_property_t *prop = obs_properties_first(props); prop; obs_property_next(&prop)) {
		const char *name = obs_property_name(prop);
		if (isKept(name)) {
			continue;
		}

		/*
		 * Only hide what is currently visible. A property the plugin has
		 * already hidden for its own reasons -- DirectShow hides the
		 * resolution list unless res_type is Custom, DeckLink hides the
		 * format list when auto-detect is on -- must stay hidden when
		 * Advanced is switched on, or turning Advanced on would show rows
		 * that do not apply.
		 */
		if (!obs_property_visible(prop)) {
			continue;
		}

		obs_property_set_visible(prop, false);
		hiddenLastRun++;
	}

	blog(LOG_DEBUG, "[MCCaptureProperties] Hid %d advanced propert%s for '%s'", hiddenLastRun,
	     hiddenLastRun == 1 ? "y" : "ies", obs_source_get_id(src));

	return props;
}

} // namespace MCCaptureProperties
