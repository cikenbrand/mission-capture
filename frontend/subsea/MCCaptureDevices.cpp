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

#include "MCCaptureDevices.hpp"

#include <obs.hpp>
#include <util/base.h>

namespace MCCaptureDevices {

namespace {

struct Backend {
	const char *sourceId;
	const char *deviceProperty;
	const char *label;
};

/*
 * The property holding each backend's device list. Both are read straight from
 * the plugin sources rather than guessed:
 *   dshow_input    VIDEO_DEVICE_ID  (plugins/win-dshow/win-dshow.cpp)
 *   decklink-input device_hash      (plugins/decklink/DecklinkInput.cpp)
 */
constexpr Backend backends[] = {
	{"decklink-input", "device_hash", "Blackmagic DeckLink"},
	{"dshow_input", "video_device_id", "DirectShow"},
};

void collect(const Backend &backend, QVector<Device> &out)
{
	/* Null when the plugin is not loaded -- DeckLink fails to initialise on a
	 * machine without the drivers, which is normal and not worth a warning. */
	obs_properties_t *props = obs_get_source_properties(backend.sourceId);
	if (!props) {
		return;
	}

	obs_property_t *list = obs_properties_get(props, backend.deviceProperty);
	if (!list || obs_property_get_type(list) != OBS_PROPERTY_LIST) {
		blog(LOG_WARNING, "[MCCaptureDevices] '%s' has no '%s' list; upstream may have renamed it",
		     backend.sourceId, backend.deviceProperty);
		obs_properties_destroy(props);
		return;
	}

	const size_t count = obs_property_list_item_count(list);
	for (size_t i = 0; i < count; i++) {
		if (obs_property_list_item_disabled(list, i)) {
			continue;
		}

		const char *name = obs_property_list_item_name(list, i);
		const char *value = obs_property_list_item_string(list, i);

		/* Some backends lead with an empty placeholder entry. */
		if (!value || !*value) {
			continue;
		}

		Device device;
		device.id = QString::fromUtf8(value);
		device.name = QString::fromUtf8(name ? name : value);
		device.sourceId = QString::fromUtf8(backend.sourceId);
		device.backend = QString::fromUtf8(backend.label);
		out.append(device);
	}

	obs_properties_destroy(props);
}

} // namespace

QVector<Device> enumerate()
{
	QVector<Device> devices;

	for (const Backend &backend : backends) {
		collect(backend, devices);
	}

	blog(LOG_INFO, "[MCCaptureDevices] Found %d capture device(s)", devices.size());

	return devices;
}

} // namespace MCCaptureDevices
