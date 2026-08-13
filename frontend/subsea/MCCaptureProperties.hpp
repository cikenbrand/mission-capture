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

/*
 * The capture property sheet, cut down to what a dive needs.
 *
 * WHY
 * ---
 * DirectShow offers fifteen properties and DeckLink twelve. Most are
 * meaningful only to someone who already knows the answer -- pixel format,
 * channel format, audio connection, 10-bit allowance, autorotation. An
 * inspection engineer setting up a camera between dives needs perhaps five,
 * and the other ten are what makes the panel feel like someone else's tool.
 *
 * Hidden, never removed. Every property stays reachable behind Advanced,
 * because the one time a camera needs a manual pixel format is the one time
 * the alternative is not recording at all.
 *
 * HOW
 * ---
 * obs_property_set_visible() on the properties object before the view builds
 * it -- OBSPropertiesView::AddProperty already honours that flag, so nothing in
 * the rendering path has to change. Applied through the view's reload callback,
 * because the view rebuilds its properties whenever a value changes and any
 * filtering done once would be undone on the first edit.
 *
 * WHAT STAYS VISIBLE, AND WHY THOSE
 * ---------------------------------
 * Device, resolution and rate, colour space and range, audio input, buffering.
 *
 * Colour range earns its place by being the most common support call on an SDI
 * path: full-versus-limited mismatch produces washed-out or crushed video that
 * looks like a camera fault and is a two-click fix. Burying it costs more than
 * showing it.
 */

namespace MCCaptureProperties {

/* True for the source types the unified capture Element creates. */
bool isCaptureSource(const char *sourceId);

/*
 * Reload callback for OBSPropertiesView, replacing obs_source_properties.
 *
 * Returns the full set for anything that is not a capture source, so the
 * dialog behaves exactly as upstream everywhere else.
 */
obs_properties_t *reload(void *source);

/*
 * Whether the Advanced set is currently shown.
 *
 * Deliberately not persisted. Advanced is for solving a problem in front of
 * you, and an operator who left it on last month should not meet the full
 * fifteen-property sheet on a different vessel.
 */
bool advancedShown();
void setAdvancedShown(bool shown);

/* Number of properties the filter hid last time it ran, for the UI manifest
 * and for a test that would otherwise be asserting against nothing. */
int lastHiddenCount();

} // namespace MCCaptureProperties
