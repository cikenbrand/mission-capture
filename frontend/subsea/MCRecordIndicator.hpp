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

#include <obs-frontend-api.h>

#include <QElapsedTimer>
#include <QWidget>

class QLabel;
class QTimer;

/*
 * "Is it recording, and for how long?" -- answerable from across a container.
 *
 * WHY THIS EXISTS WHEN THE STATUS BAR ALREADY SHOWS IT
 * ----------------------------------------------------
 * Upstream puts an 11-pixel red dot and a timestamp in the status bar. That is
 * fine for a streamer looking at the screen they are working on, and useless in
 * an ROV shack where the operator is watching sonar, the pilot is flying, and
 * the person who needs to know whether the dive is being recorded is standing
 * behind both of them. A recording that silently was not running is the single
 * most expensive failure this product has.
 *
 * So: large, high-contrast, and *absent* rather than grey when idle. Something
 * that is always visible and merely changes colour trains people to stop
 * reading it.
 *
 * ELAPSED TIME IS MEASURED, NOT COUNTED
 * -------------------------------------
 * OBSBasicStatusBar increments a counter once per timer tick, which drifts
 * whenever the UI thread is busy -- exactly when a long recording is under
 * load. This uses a monotonic clock, so the figure an operator reads is the
 * real one, and pause time is excluded rather than quietly included.
 */

class MCRecordIndicator : public QWidget {
	Q_OBJECT

public:
	explicit MCRecordIndicator(QWidget *parent = nullptr);
	~MCRecordIndicator() override;

	/* Seconds of actual recording, excluding any paused stretches. Zero when
	 * not recording. Used by the confirm-on-stop threshold. */
	static qint64 elapsedSeconds();

	/* True between RECORDING_STARTED and RECORDING_STOPPED, pause included. */
	static bool isRecording();

private slots:
	void tick();

private:
	static void frontendEvent(enum obs_frontend_event event, void *data);

	void setRecording(bool recording);
	void setPaused(bool paused);
	void refresh();

	QLabel *label_ = nullptr;
	QTimer *timer_ = nullptr;
};
