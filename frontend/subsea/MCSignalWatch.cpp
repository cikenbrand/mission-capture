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

#include "MCSignalWatch.hpp"
#include "MCCaptureProperties.hpp"
#include "MCVideoCaptureElement.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <util/base.h>
#include <util/platform.h>

#include <QTimer>

#include <atomic>
#include <mutex>
#include <set>
#include <cstring>
#include <string>

namespace MCSignalWatch {

namespace {

constexpr const char *kFilterId = "mc_signal_watch";
constexpr const char *kFilterName = "Mission Capture signal watch";

/*
 * Per-filter state. The counter is written on the capture thread and read on
 * the UI thread, so it is atomic; nothing else here is touched from both.
 */
struct Watch {
	std::atomic<quint64> frames{0};
	quint64 lastSampled = 0;
	uint64_t lastFrameNs = 0;

	/* Frames per second measured over the last sampling interval, not read
	 * from the source's declared rate. A camera configured for 25 fps that is
	 * delivering 12 is the exact fault an operator needs to see, and the
	 * declared figure would hide it. */
	double fps = 0.0;
	uint64_t lastFpsNs = 0;
	quint64 lastFpsFrames = 0;
	State state = State::Unknown;

	/* The filter itself. Held as a raw pointer deliberately: the filter owns
	 * this struct and destroys it first, so it cannot dangle -- and taking a
	 * reference would make the filter immortal. Used only to find the parent
	 * Element for logging. */
	obs_source_t *filter = nullptr;

	std::string elementName;
	std::string sourceId;
};

/*
 * Created and destroyed on libobs threads, read on the UI thread, so the set
 * itself needs a lock even though the counter inside each Watch is atomic.
 */
std::set<Watch *> watches;
std::mutex watchesMutex;
QTimer *sampler = nullptr;
bool registered = false;

/* --- the filter ------------------------------------------------------- */

const char *watchName(void *)
{
	return kFilterName;
}

void *watchCreate(obs_data_t *, obs_source_t *context)
{
	auto *watch = new Watch();
	watch->filter = context;

	/* The parent Element is not attached yet at create time, so the names are
	 * resolved lazily by the sampler. */
	{
		const std::lock_guard<std::mutex> lock(watchesMutex);
		watches.insert(watch);
	}
	return watch;
}

void watchDestroy(void *data)
{
	auto *watch = static_cast<Watch *>(data);
	{
		const std::lock_guard<std::mutex> lock(watchesMutex);
		watches.erase(watch);
	}
	delete watch;
}

/* Fills in the Element's name and backend once the filter has a parent. Cheap
 * enough to re-run, and a rename should be picked up. */
void refreshNames(Watch *watch)
{
	if (!watch->filter) {
		return;
	}

	obs_source_t *parent = obs_filter_get_parent(watch->filter);
	if (!parent) {
		return;
	}

	const char *name = obs_source_get_name(parent);
	const char *id = obs_source_get_id(parent);
	watch->elementName = name ? name : "";
	watch->sourceId = id ? id : "";
}

obs_source_frame *watchFilterVideo(void *data, obs_source_frame *frame)
{
	auto *watch = static_cast<Watch *>(data);

	/* The whole mechanism, in one line: a frame passed through here is a
	 * frame the camera actually delivered. Returned untouched -- this filter
	 * must be invisible to the picture. */
	watch->frames.fetch_add(1, std::memory_order_relaxed);

	return frame;
}

obs_source_info makeFilterInfo()
{
	obs_source_info info = {};
	info.id = kFilterId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC;
	info.get_name = watchName;
	info.create = watchCreate;
	info.destroy = watchDestroy;
	info.filter_video = watchFilterVideo;
	return info;
}

/* --- attaching -------------------------------------------------------- */

bool hasWatch(obs_source_t *source)
{
	OBSSourceAutoRelease existing = obs_source_get_filter_by_name(source, kFilterName);
	return existing != nullptr;
}

void attachTo(obs_source_t *source)
{
	if (!source || hasWatch(source)) {
		return;
	}

	OBSSourceAutoRelease filter = obs_source_create_private(kFilterId, kFilterName, nullptr);
	if (!filter) {
		blog(LOG_WARNING, "[MCSignalWatch] Could not create the watch filter for '%s'",
		     obs_source_get_name(source));
		return;
	}

	obs_source_filter_add(source, filter);
}

/*
 * Which Elements are worth watching.
 *
 * Capture devices, and network media -- an RTSP camera is the source *most*
 * likely to drop, so leaving it unwatched would have missed the common case.
 * A local file is excluded: it ends when it ends, and calling that a lost
 * signal would be wrong. In practice task 1.6 does not offer local media at
 * all, so an ffmpeg_source in a Job is a camera.
 */
bool isWatchable(obs_source_t *source)
{
	const char *id = obs_source_get_id(source);
	if (!id) {
		return false;
	}

	if (MCCaptureProperties::isCaptureSource(id)) {
		return true;
	}

	if (strcmp(id, "ffmpeg_source") == 0) {
		OBSDataAutoRelease settings = obs_source_get_settings(source);
		return settings && !obs_data_get_bool(settings, "is_local_file");
	}

	return false;
}

bool attachIfCapture(void *, obs_source_t *source)
{
	if (isWatchable(source)) {
		attachTo(source);
	}
	return true;
}

void onSourceCreated(void *, calldata_t *cd)
{
	auto *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (source && isWatchable(source)) {
		/* Queued: the source is still being constructed when this fires, and
		 * adding a filter to a half-built source is asking for trouble. */
		QMetaObject::invokeMethod(qApp, [ref = OBSSource(source)]() { attachTo(ref); }, Qt::QueuedConnection);
	}
}

/* --- sampling --------------------------------------------------------- */

double thresholdSeconds = 3.0;

void sample()
{
	const uint64_t now = os_gettime_ns();

	const std::lock_guard<std::mutex> lock(watchesMutex);

	for (Watch *watch : watches) {
		refreshNames(watch);

		const quint64 total = watch->frames.load(std::memory_order_relaxed);
		const bool advanced = total != watch->lastSampled;

		if (watch->lastFpsNs != 0) {
			const double seconds = static_cast<double>(now - watch->lastFpsNs) / 1e9;
			if (seconds > 0.0) {
				watch->fps = static_cast<double>(total - watch->lastFpsFrames) / seconds;
			}
		}
		watch->lastFpsNs = now;
		watch->lastFpsFrames = total;

		if (advanced) {
			watch->lastFrameNs = now;
			watch->lastSampled = total;
		}

		if (watch->lastFrameNs == 0) {
			/* Nothing has ever arrived. Not "lost" -- a camera that has
			 * never produced a frame is a different problem from one that
			 * stopped, and calling it lost would put a reconnecting banner
			 * on an Element that was never connected. */
			continue;
		}

		const double idle = static_cast<double>(now - watch->lastFrameNs) / 1e9;
		const State was = watch->state;
		const State is = (idle > thresholdSeconds) ? State::Lost : State::Receiving;

		if (is == was) {
			continue;
		}

		watch->state = is;

		/*
		 * Logged with a timestamp on every transition, both ways. A post-dive
		 * review has to be able to answer "when did camera 2 drop, and did it
		 * come back?" from the log alone -- nobody is watching the screen at
		 * the moment it matters.
		 */
		/* "acquired" the first time, "restored" thereafter: on the first
		 * transition nothing was ever lost, and a log saying otherwise sends a
		 * post-dive reviewer looking for a dropout that never happened. */
		const char *what = (is == State::Lost)       ? "SIGNAL LOST -- no frames for the last few seconds"
				   : (was == State::Unknown) ? "signal acquired"
							     : "signal restored";

		blog(LOG_WARNING, "[MCSignalWatch] '%s' (%s): %s", watch->elementName.c_str(), watch->sourceId.c_str(),
		     what);

		/*
		 * First frames are the moment a camera's resolution is finally
		 * knowable, so this is where the Canvas gets sized to it (OI-48).
		 * Hooked here rather than only after Add Element, because a Job loaded
		 * from disk never goes through that path -- and a camera that returns
		 * at a different format deserves the same treatment.
		 */
		if (is == State::Receiving && watch->filter) {
			if (obs_source_t *parent = obs_filter_get_parent(watch->filter)) {
				MCVideoCaptureElement::matchCanvasToSource(parent);
			}
		}
	}
}

} // namespace

double lostThresholdSeconds()
{
	return thresholdSeconds;
}

void registerFilter()
{
	if (registered) {
		return;
	}

	static obs_source_info info = makeFilterInfo();
	obs_register_source(&info);
	registered = true;

	blog(LOG_INFO, "[MCSignalWatch] Registered '%s'", kFilterId);
}

void start()
{
	if (sampler) {
		return;
	}

	thresholdSeconds =
		static_cast<double>(config_get_int(App()->GetUserConfig(), "BasicWindow", "SignalLostAfterSeconds"));
	if (thresholdSeconds <= 0.0) {
		thresholdSeconds = 3.0;
	}

	/* Everything already loaded... */
	obs_enum_sources(attachIfCapture, nullptr);

	/* ...and everything added later. */
	signal_handler_connect(obs_get_signal_handler(), "source_create", onSourceCreated, nullptr);

	sampler = new QTimer(qApp);
	sampler->setInterval(1000);
	QObject::connect(sampler, &QTimer::timeout, qApp, sample);
	sampler->start();

	blog(LOG_INFO, "[MCSignalWatch] Watching capture Elements; signal considered lost after %.0fs",
	     thresholdSeconds);
}

QVector<Status> statuses()
{
	QVector<Status> out;
	const uint64_t now = os_gettime_ns();

	const std::lock_guard<std::mutex> lock(watchesMutex);

	for (Watch *watch : watches) {
		/* Also resolved here, not only in the sampler: a caller may ask before
		 * the first sample has run -- the UI manifest does exactly that -- and
		 * an unnamed Element in a health report is useless. */
		refreshNames(watch);

		Status status;
		status.elementName = QString::fromStdString(watch->elementName);
		status.sourceId = QString::fromStdString(watch->sourceId);
		status.state = watch->state;
		status.frames = watch->frames.load(std::memory_order_relaxed);
		status.secondsSinceFrame = watch->lastFrameNs ? static_cast<double>(now - watch->lastFrameNs) / 1e9
							      : 0.0;
		out.append(status);
	}

	return out;
}

} // namespace MCSignalWatch
