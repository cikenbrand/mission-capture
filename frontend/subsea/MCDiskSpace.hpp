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

#include <QObject>
#include <QString>

class QLabel;
class QTimer;

/*
 * Free space on the recording disk: shown always, warned about twice, and
 * acted on before it runs out.
 *
 * WHY THIS EXISTS WHEN UPSTREAM ALREADY STOPS AT 50 MB
 * ----------------------------------------------------
 * Upstream polls during a recording and stops when fewer than 50 MB remain.
 * Two problems for a dive:
 *
 *   1. 50 MB at inspection bitrates is a handful of seconds. The muxer still
 *      has to write its trailer, and the margin for that is whatever is left
 *      after the frame that tripped the check. Stopping earlier is free;
 *      stopping too late costs the file.
 *   2. Nothing says anything beforehand. The first an operator hears is that
 *      recording has already stopped -- at which point the choice they would
 *      have made an hour ago, to swap a drive or trim old footage, is gone.
 *
 * So this adds the part that matters: a number on screen the whole time, two
 * warnings with room to act, and a stop with enough headroom to close cleanly.
 * Upstream's 50 MB check stays as a backstop and should never be the one that
 * fires.
 *
 * THRESHOLDS ARE CONFIGURABLE, WHICH IS ALSO HOW THIS IS TESTED
 * -------------------------------------------------------------
 * A test cannot fill a disk, and faking free space would mean test-only code in
 * the product. Instead the thresholds move: set the warning to something larger
 * than the volume and it fires on the next poll, on a real disk, through the
 * real code path. See tools/subsea-tests/t1-shell.ps1.
 */

class MCDiskSpace : public QObject {
	Q_OBJECT

public:
	/* Thresholds in bytes, resolved from config each poll so a change in
	 * features.ini or a test takes effect without a restart. */
	struct Thresholds {
		quint64 cautionBytes = 0;
		quint64 criticalBytes = 0;
		quint64 stopBytes = 0;
	};

	explicit MCDiskSpace(QObject *parent = nullptr);

	/* Installs the status-bar field and starts polling. Call once, after the
	 * main window's status bar exists. */
	static MCDiskSpace *install(class OBSBasic *main);

	/* Free bytes at the last poll, or 0 if the path could not be read. */
	static quint64 lastFreeBytes();

	/* The level the last poll landed in. Exposed for the UI manifest. */
	enum class Level { Ample, Caution, Critical, Stopping };
	static Level lastLevel();
	static const char *levelName(Level level);

	static Thresholds thresholds();

private slots:
	void poll();

private:
	void updateLabel(quint64 freeBytes, Level level);
	void announce(Level level, quint64 freeBytes);

	QLabel *label_ = nullptr;
	QTimer *timer_ = nullptr;
	class OBSBasic *main_ = nullptr;
};
