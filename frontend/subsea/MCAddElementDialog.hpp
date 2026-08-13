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

class QWidget;

/*
 * "What do you want to add?" -- three answers, not thirteen.
 *
 * WHAT THIS REPLACES
 * ------------------
 * OBS's Add Source dialog is a scrolling list of every registered type with a
 * thumbnail each. Task 1.6 already cut what it *offers* down to one, which made
 * the list look broken rather than focused: a long dialog with a single row.
 *
 * This is the shape the product wants -- Video Capture Device, RTSP Camera,
 * Overlay -- and it is a shape that stays right as the other two arrive,
 * whereas the filtered list only looks right once all three exist.
 *
 * DEVICE DISCOVERY IS ALREADY DONE
 * --------------------------------
 * The capture button knows how many cameras were found before the operator
 * presses it, and says so. Discovery takes long enough to notice on a machine
 * with several backends, and "0 found" is worth learning before you commit to
 * a dialog rather than after.
 *
 * OVERLAY IS PRESENT AND DISABLED
 * -------------------------------
 * Phase 4 builds it. Showing the button greyed, with a reason, is more honest
 * than a two-button dialog that silently grows a third later -- the same
 * decision the Job wizard made for its overlay and data pages.
 */

namespace MCAddElementDialog {

/*
 * Runs the picker and adds whatever the operator chose to `scene`.
 *
 * Returns true if an Element was created. A cancelled dialog, or a
 * configuration step the operator backed out of, returns false without
 * touching the Job.
 */
bool run(QWidget *parent, obs_scene_t *scene);

} // namespace MCAddElementDialog
