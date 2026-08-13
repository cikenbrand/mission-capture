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

#include "MCVideoCaptureElement.hpp"

#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <util/base.h>

#include <QApplication>
#include <QTimer>

namespace MCVideoCaptureElement {

namespace {

constexpr const char *kDeckLink = "decklink-input";
constexpr const char *kDirectShow = "dshow_input";

/* plugins/decklink/decklink-device-mode.hpp */
constexpr long long kDeckLinkModeAuto = -1;

/* plugins/win-dshow/win-dshow.cpp: ResType_Preferred, FPS_MATCHING */
constexpr int kDShowResPreferred = 0;
constexpr long long kDShowFpsMatching = 0;

} // namespace

const char *fallbackSourceId()
{
	return kDirectShow;
}

const char *sourceIdFor(const MCCaptureDevices::Device &device)
{
	if (device.sourceId == QLatin1String(kDeckLink)) {
		return kDeckLink;
	}
	if (device.sourceId == QLatin1String(kDirectShow)) {
		return kDirectShow;
	}

	/* Enumerated by a backend we do not know, or hand-written into a Job by a
	 * newer build. Fall through rather than refuse. */
	blog(LOG_WARNING, "[MCVideoCaptureElement] Unknown backend '%s' for device '%s'; using %s",
	     QT_TO_UTF8(device.sourceId), QT_TO_UTF8(device.name), kDirectShow);
	return fallbackSourceId();
}

OBSData settingsFor(const MCCaptureDevices::Device &device)
{
	OBSDataAutoRelease settings = obs_data_create();
	const char *sourceId = sourceIdFor(device);

	if (strcmp(sourceId, kDeckLink) == 0) {
		obs_data_set_string(settings, "device_hash", QT_TO_UTF8(device.id));

		/*
		 * Auto input-format detection on. A DeckLink fed 1080i50 while
		 * configured for 1080p30 shows nothing at all, and the operator has
		 * no way to know which of the two is wrong. Auto gets a picture; the
		 * manual override stays available for cameras that output something
		 * the card cannot work out (task 2.2 surfaces it).
		 */
		obs_data_set_int(settings, "mode_id", kDeckLinkModeAuto);

		/* No buffering. Latency is the point of an SDI path -- a pilot
		 * flying on a delayed picture is the failure this avoids. */
		obs_data_set_bool(settings, "buffering", false);
	} else {
		obs_data_set_string(settings, "video_device_id", QT_TO_UTF8(device.id));
		obs_data_set_string(settings, "last_video_device_id", QT_TO_UTF8(device.id));

		/*
		 * Let the device pick its own resolution and rate. DirectShow
		 * grabbers vary wildly in which combinations actually work, and
		 * pinning values we invented is the usual cause of "it worked in
		 * OBS but not in yours". Task 2.2 lets the operator pin them.
		 */
		obs_data_set_int(settings, "res_type", kDShowResPreferred);
		obs_data_set_int(settings, "frame_interval", kDShowFpsMatching);

		obs_data_set_bool(settings, "buffering", false);

		/*
		 * Keep capturing while the Element is not on the program Canvas.
		 * Upstream defaults this off to save resources on a streaming rig;
		 * here, a camera that stops when you look away is a camera that
		 * misses the thing you turned away from -- and Phase 6 records
		 * several Canvases at once, only one of which is on screen.
		 */
		obs_data_set_bool(settings, "deactivate_when_not_showing", false);
	}

	return settings.Get();
}

bool matchCanvasToSource(obs_source_t *source)
{
	if (!source) {
		return false;
	}

	const uint32_t cx = obs_source_get_width(source);
	const uint32_t cy = obs_source_get_height(source);

	/*
	 * Zero means the source has not produced a frame yet -- no signal, no
	 * device, or still opening. Resizing to 0x0 would be catastrophic and
	 * silent, so nothing is done and the caller may try again later.
	 */
	if (cx == 0 || cy == 0) {
		return false;
	}

	OBSBasic *main = OBSBasic::Get();
	if (!main) {
		return false;
	}

	config_t *config = main->Config();
	const uint32_t baseCX = static_cast<uint32_t>(config_get_uint(config, "Video", "BaseCX"));
	const uint32_t baseCY = static_cast<uint32_t>(config_get_uint(config, "Video", "BaseCY"));

	if (baseCX == cx && baseCY == cy) {
		return false;
	}

	/*
	 * Written as user values, not defaults. Upstream force-writes BaseCX/BaseCY
	 * as user values during InitBasicConfigDefaults precisely so a changing
	 * default cannot move an operator's canvas, so a default here would be
	 * ignored -- the same last-writer trap task 1.7 hit from the other side.
	 */
	config_set_uint(config, "Video", "BaseCX", cx);
	config_set_uint(config, "Video", "BaseCY", cy);
	config_set_uint(config, "Video", "OutputCX", cx);
	config_set_uint(config, "Video", "OutputCY", cy);
	config_save_safe(config, "tmp", nullptr);

	blog(LOG_INFO, "[MCVideoCaptureElement] Canvas resized from %ux%u to %ux%u to match '%s'", baseCX, baseCY, cx,
	     cy, obs_source_get_name(source));

	/* Applies without a restart; the preview and the encoders both follow. */
	main->ResetVideo();

	return true;
}

OBSSceneItem addTo(obs_scene_t *scene, const MCCaptureDevices::Device &device, const QString &name)
{
	if (!scene) {
		return nullptr;
	}

	const char *sourceId = sourceIdFor(device);
	OBSData settings = settingsFor(device);

	OBSSourceAutoRelease source = obs_source_create(sourceId, QT_TO_UTF8(name), settings, nullptr);
	if (!source) {
		blog(LOG_ERROR, "[MCVideoCaptureElement] libobs refused to create '%s' as '%s'", QT_TO_UTF8(name),
		     sourceId);
		return nullptr;
	}

	OBSSceneItem item = obs_scene_add(scene, source);

	blog(LOG_INFO, "[MCVideoCaptureElement] Added '%s' (%s via %s)", QT_TO_UTF8(name), QT_TO_UTF8(device.name),
	     sourceId);

	/*
	 * Size the Canvas to the camera once it reports a size (OI-48).
	 *
	 * Retried rather than called once: a capture device reports 0x0 until its
	 * first frame arrives, and how long that takes depends on the card, the
	 * cable and whether anything is plugged in at all. Six attempts over three
	 * seconds covers a device that is present and gives up quietly on one that
	 * is not -- there is nothing to match against a camera with no signal, and
	 * guessing would be worse than leaving the Canvas alone.
	 */
	auto *attempts = new int(0);
	auto *timer = new QTimer(qApp);
	timer->setInterval(500);
	QObject::connect(timer, &QTimer::timeout, qApp, [timer, attempts, src = OBSSource(source.Get())]() {
		if (matchCanvasToSource(src) || ++(*attempts) >= 6) {
			timer->stop();
			timer->deleteLater();
			delete attempts;
		}
	});
	timer->start();

	return item;
}

} // namespace MCVideoCaptureElement
