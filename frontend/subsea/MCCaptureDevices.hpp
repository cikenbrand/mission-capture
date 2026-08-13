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

#include <QString>
#include <QVector>

/*
 * Capture devices present on this machine, across both backends.
 *
 * The Job wizard needs a list of cameras before any Element exists, so this
 * reads the device lists straight off the source types' property definitions --
 * obs_get_source_properties() builds them without instantiating a source, which
 * means no device is opened and nothing is taken away from a running capture.
 *
 * Task 2.1 unifies these two backends behind one picker for good. This is the
 * narrow version of that: enough to enumerate and name, not to configure.
 */

namespace MCCaptureDevices {

struct Device {
	QString id;       /* Backend-specific identifier, stored on the Element in phase 2 */
	QString name;     /* What the device calls itself */
	QString sourceId; /* "dshow_input" or "decklink-input" */
	QString backend;  /* Human-readable, shown as a subtitle */
};

/*
 * Every capture device found, DeckLink first.
 *
 * DeckLink leads because on a vessel it is the SDI path -- the cameras that
 * matter -- while DirectShow tends to enumerate webcams and virtual devices
 * alongside the real capture cards.
 *
 * An empty list is a normal outcome, not an error: a machine being configured
 * before the hardware arrives is a real case, and the wizard has to work there.
 */
QVector<Device> enumerate();

} // namespace MCCaptureDevices
