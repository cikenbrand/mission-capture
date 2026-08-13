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

#include <obs.h>

#include <QString>
#include <QVector>

/*
 * Is each camera still sending?
 *
 * WHY THIS IS HARDER THAN IT LOOKS
 * --------------------------------
 * When a capture card is unplugged or an SDI cable is pulled, neither backend
 * announces it. win-dshow and decklink simply stop calling
 * obs_source_output_video, and libobs keeps rendering the last frame it was
 * given. The preview shows a picture, the recording keeps writing it, and
 * nothing anywhere says the camera died ten minutes ago. That is the failure
 * this task exists to remove, and it is invisible by construction.
 *
 * Nor is it observable from outside: obs_source_get_width() keeps returning the
 * last known size, and obs_source_get_frame() *steals* the pending frame from
 * the renderer, so polling it would break the very preview it was meant to
 * check. libobs tracks last_frame_ts internally and does not export it.
 *
 * SO: A FILTER, WHICH IS WHERE FRAMES ACTUALLY PASS
 * -------------------------------------------------
 * A filter's filter_video callback runs once per delivered frame -- that is the
 * definition of "still sending". This registers a tiny internal filter that
 * counts frames and returns them untouched, attaches one to each capture
 * Element, and samples the counters on a timer.
 *
 * Registered from the frontend rather than as a plugin: obs_register_source is
 * public, the filter has no properties and no UI, and a whole plugin for one
 * counter would be a lot of build system for forty lines.
 *
 * DECKLINK LOSES SIGNAL AND DEVICES SEPARATELY
 * --------------------------------------------
 * The phase doc warns that a pulled cable and a reset card are different
 * events. Counting frames covers both, because both stop the frames -- which is
 * the point of measuring the symptom rather than the cause. What differs is
 * recovery, and that is the backend's job: we re-check and clear the state when
 * frames resume, whichever way they resume.
 */

namespace MCSignalWatch {

enum class State {
	Unknown,   /* Not being watched, or nothing seen yet */
	Receiving, /* Frames arriving */
	Lost,      /* Nothing for longer than the threshold */
};

struct Status {
	QString elementName;
	QString sourceId;
	State state = State::Unknown;
	quint64 frames = 0; /* Total frames seen since the watch attached */
	double secondsSinceFrame = 0.0;
	double fps = 0.0; /* Measured over the last sampling interval */
};

/* Registers the internal filter type. Call once, before any source is created
 * -- from OBSApp, alongside the other libobs-level registrations. */
void registerFilter();

/*
 * Starts watching. Attaches to every capture Element already present and to any
 * added later. Call once the frontend exists.
 */
void start();

/* Everything currently watched, for the Layers tree, the Health panel and the
 * UI manifest. */
QVector<Status> statuses();

/* Seconds without a frame before an Element is called Lost. Configurable
 * because a 1 fps sonar overlay and a 25 fps camera do not fail on the same
 * timescale. */
double lostThresholdSeconds();

} // namespace MCSignalWatch
