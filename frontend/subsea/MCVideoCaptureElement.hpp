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

#include "MCCaptureDevices.hpp"

#include <obs.hpp>

#include <QString>

/*
 * One "Video Capture Device" Element, whichever backend is behind it.
 *
 * THE PROBLEM THIS SOLVES
 * -----------------------
 * OBS makes the operator choose the backend before the camera: DeckLink and
 * DirectShow are separate source types with separate property sheets and
 * separate vocabulary for the same ideas. An inspection engineer should pick a
 * camera. Which driver stack it happens to sit on is our problem, not theirs.
 *
 * A FACTORY, NOT A NEW SOURCE TYPE
 * --------------------------------
 * This creates upstream's sources with our settings rather than wrapping them
 * in a plugin of our own. A wrapper would mean reimplementing two property
 * sheets, two device-change paths and two failure modes, and would put our code
 * between the capture card and the encoder -- the one place in this product
 * where nothing should sit that does not have to.
 *
 * So the unification is at the point of *choosing*, and after that each Element
 * is an ordinary upstream source that upstream keeps working.
 *
 * WHAT HAPPENS WHEN THE DEVICE IS NOT THERE
 * -----------------------------------------
 * Deliberately, the same thing as when it is: the Element is created and shows
 * no signal. Jobs move between machines, cards get swapped between dives, and a
 * Job that refuses to load because a camera is missing is worse than one that
 * loads with a black Element and says so. Task 2.3 gives that state a banner.
 */

namespace MCVideoCaptureElement {

/* Which upstream source type backs a device. Exposed so the picker can show it
 * as a subtitle, and so a test can assert the mapping without a device. */
const char *sourceIdFor(const MCCaptureDevices::Device &device);

/*
 * The source id used when a device is unrecognised.
 *
 * DirectShow, because that is what unbranded and third-party grabbers present
 * as -- the phase doc's "there may be other capture cards as well" is mostly
 * this case. A wrong guess here produces an Element with no signal, which is
 * recoverable; refusing to create one is not.
 */
const char *fallbackSourceId();

/*
 * Creates the Element in `scene` and returns the new scene item.
 *
 * `name` is what appears in Layers. It must be unique within the Job; the
 * caller is expected to have resolved that (the wizard names by camera, the
 * picker by device).
 *
 * Returns a null item if the scene is missing or libobs refused the source.
 */
OBSSceneItem addTo(obs_scene_t *scene, const MCCaptureDevices::Device &device, const QString &name);

/*
 * Settings this product wants for a given backend, without creating anything.
 * Split out so the values can be asserted directly -- there is no capture card
 * on the development machine, and the settings are the part that would
 * otherwise go unverified until someone had one.
 */
OBSData settingsFor(const MCCaptureDevices::Device &device);

} // namespace MCVideoCaptureElement
