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

#include "MCDiskSpace.hpp"
#include "MCRecordIndicator.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs-frontend-api.h>
#include <util/platform.h>

#include <QLabel>
#include <QStatusBar>
#include <QTimer>

#include "moc_MCDiskSpace.cpp"

namespace {

constexpr quint64 kGiB = 1024ULL * 1024ULL * 1024ULL;

quint64 lastFree = 0;
MCDiskSpace::Level lastLevelSeen = MCDiskSpace::Level::Ample;

/* Only announce a level when it is entered, not on every poll. A warning
 * repeated every two seconds is noise, and noise is ignored. */
MCDiskSpace::Level lastAnnounced = MCDiskSpace::Level::Ample;

QString humanBytes(quint64 bytes)
{
	if (bytes >= kGiB) {
		return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / kGiB, 0, 'f', 1);
	}
	return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 0);
}

} // namespace

MCDiskSpace::MCDiskSpace(QObject *parent) : QObject(parent) {}

MCDiskSpace::Thresholds MCDiskSpace::thresholds()
{
	config_t *config = App()->GetUserConfig();

	Thresholds t;
	t.cautionBytes = static_cast<quint64>(config_get_int(config, "BasicWindow", "DiskCautionGB")) * kGiB;
	t.criticalBytes = static_cast<quint64>(config_get_int(config, "BasicWindow", "DiskCriticalGB")) * kGiB;
	t.stopBytes = static_cast<quint64>(config_get_int(config, "BasicWindow", "DiskStopGB")) * kGiB;
	return t;
}

const char *MCDiskSpace::levelName(Level level)
{
	switch (level) {
	case Level::Ample:
		return "ample";
	case Level::Caution:
		return "caution";
	case Level::Critical:
		return "critical";
	case Level::Stopping:
		return "stopping";
	}
	return "unknown";
}

quint64 MCDiskSpace::lastFreeBytes()
{
	return lastFree;
}

MCDiskSpace::Level MCDiskSpace::lastLevel()
{
	return lastLevelSeen;
}

MCDiskSpace *MCDiskSpace::install(OBSBasic *main)
{
	if (!main || !main->statusBar()) {
		return nullptr;
	}

	auto *monitor = new MCDiskSpace(main);
	monitor->main_ = main;

	monitor->label_ = new QLabel(main);
	monitor->label_->setObjectName(QStringLiteral("diskSpaceLabel"));
	monitor->label_->setToolTip(QTStr("DiskSpace.Tooltip"));

	/* Permanent, so transient status messages cannot cover it. The whole
	 * point is that it is there to be glanced at, not surfaced on demand. */
	main->statusBar()->addPermanentWidget(monitor->label_);

	monitor->timer_ = new QTimer(monitor);
	/*
	 * Two seconds. os_get_free_disk_space() is a stat call on the volume, so
	 * this is cheap, and the figure needs to be current enough that the
	 * critical warning is not the first thing anyone reads.
	 */
	monitor->timer_->setInterval(2000);
	connect(monitor->timer_, &QTimer::timeout, monitor, &MCDiskSpace::poll);
	monitor->timer_->start();

	monitor->poll();
	return monitor;
}

void MCDiskSpace::poll()
{
	const char *path = main_ ? main_->GetCurrentOutputPath() : nullptr;
	if (!path || !*path) {
		label_->setText(QTStr("DiskSpace.Unknown"));
		return;
	}

	const quint64 freeBytes = os_get_free_disk_space(path);
	lastFree = freeBytes;

	/*
	 * Zero means the volume could not be read -- a bad path, a disconnected
	 * drive, a permissions failure -- not a full disk. Treating the two alike
	 * would let a transient stat failure stop a healthy recording, which is the
	 * exact harm this class exists to prevent.
	 *
	 * Found by a test whose output path was invalid: the monitor read 0 bytes
	 * and went straight to Stopping.
	 */
	if (freeBytes == 0) {
		lastLevelSeen = Level::Ample;
		label_->setText(QTStr("DiskSpace.Unknown"));
		label_->setStyleSheet(QString());
		return;
	}

	const Thresholds t = thresholds();

	Level level = Level::Ample;
	if (freeBytes <= t.stopBytes) {
		level = Level::Stopping;
	} else if (freeBytes <= t.criticalBytes) {
		level = Level::Critical;
	} else if (freeBytes <= t.cautionBytes) {
		level = Level::Caution;
	}

	lastLevelSeen = level;
	updateLabel(freeBytes, level);

	/*
	 * The stop only applies while something is being written. Below the stop
	 * threshold with nothing recording is a warning, not an emergency -- and
	 * refusing to start is already handled by upstream's check in
	 * StartRecording().
	 */
	if (level == Level::Stopping && MCRecordIndicator::isRecording()) {
		blog(LOG_WARNING,
		     "[MCDiskSpace] Stopping the recording: %llu bytes free, at or below the %llu byte floor",
		     static_cast<unsigned long long>(freeBytes), static_cast<unsigned long long>(t.stopBytes));

		/*
		 * The graceful path, deliberately. obs_frontend_recording_stop() runs
		 * the same stop the button does, so the muxer writes its trailer and
		 * the file is playable -- which is the entire reason for stopping
		 * early rather than at upstream's 50 MB, where there may not be room
		 * for the trailer.
		 */
		obs_frontend_recording_stop();

		announce(level, freeBytes);
		return;
	}

	announce(level, freeBytes);
}

void MCDiskSpace::updateLabel(quint64 freeBytes, Level level)
{
	label_->setText(QTStr("DiskSpace.Free").arg(humanBytes(freeBytes)));

	/* Colour only once there is something to say. A field that is always
	 * coloured reads as decoration. */
	switch (level) {
	case Level::Ample:
		label_->setStyleSheet(QString());
		break;
	case Level::Caution:
		label_->setStyleSheet(QStringLiteral("QLabel { color: #ef6c00; font-weight: bold; }"));
		break;
	case Level::Critical:
	case Level::Stopping:
		label_->setStyleSheet(QStringLiteral("QLabel { color: #ffffff; background-color: #c62828;"
						     " font-weight: bold; padding: 0 6px; border-radius: 3px; }"));
		break;
	}
}

void MCDiskSpace::announce(Level level, quint64 freeBytes)
{
	if (level == lastAnnounced) {
		return;
	}

	/* Recovering quietly is right: an operator who has just freed space does
	 * not need to be told they succeeded, and a drive hovering on a boundary
	 * must not produce a stream of alternating messages. */
	const bool worse = static_cast<int>(level) > static_cast<int>(lastAnnounced);
	lastAnnounced = level;

	if (!worse || level == Level::Ample) {
		return;
	}

	blog(LOG_WARNING, "[MCDiskSpace] %s: %llu bytes free on the recording volume", levelName(level),
	     static_cast<unsigned long long>(freeBytes));

	if (!main_ || !main_->statusBar()) {
		return;
	}

	/*
	 * Status bar, not a modal. A dialog in front of a live preview during a
	 * dive is worse than the problem it reports -- it hides the video and
	 * needs an answer from someone who may be flying. The permanent field is
	 * already turning colour beside it.
	 */
	const QString message = (level == Level::Stopping) ? QTStr("DiskSpace.Stopped")
							   : QTStr("DiskSpace.Warning").arg(humanBytes(freeBytes));

	main_->statusBar()->showMessage(message, level == Level::Stopping ? 0 : 15000);
}
