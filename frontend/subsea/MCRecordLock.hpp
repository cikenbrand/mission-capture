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

class QWidget;

/*
 * Layers editing is locked while a recording is running.
 *
 * WHY
 * ---
 * Every edit in the Layers tree changes what is being written to disk, right
 * now, with no undo that reaches the file. Hiding an Element mid-dive removes
 * it from the footage; reordering changes what occludes what; deleting one is
 * unrecoverable once the frames are muxed. None of these announce themselves --
 * the recording carries on looking healthy.
 *
 * A brushed elbow on a trackball in a moving shack is a real input method.
 *
 * DELIBERATELY OVERRIDABLE
 * ------------------------
 * Legitimate reasons to edit mid-recording exist -- a camera fails and its
 * Element needs hiding, an overlay is wrong. So the lock is a speed bump, not a
 * wall: one explicit toggle releases it.
 *
 * The override clears when the recording stops, so it cannot silently carry
 * into the next dive. That is the whole point of making it deliberate.
 */

namespace MCRecordLock {

/* True when edits should be refused: recording, and not overridden. */
bool locked();

/* True when a recording is running -- whether or not the lock is overridden.
 * Distinct from locked() so callers can explain *why* something is refused. */
bool recording();

/* Operator's explicit decision to edit anyway. Cleared automatically when the
 * recording stops. */
void setOverridden(bool overridden);
bool overridden();

/*
 * Explains the refusal once, in the status bar rather than a modal.
 *
 * A dialog would be worse than the problem: it steals focus from an operator
 * who is flying, and the answer to "why did nothing happen" needs to be
 * readable, not dismissed.
 */
void explainRefusal(QWidget *context);

/* Called by MCRecordIndicator when recording stops. */
void onRecordingStopped();

} // namespace MCRecordLock
