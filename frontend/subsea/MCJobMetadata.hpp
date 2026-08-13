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

/*
 * The paperwork attached to a Job.
 *
 * WHY IT LIVES IN THE JOB FILE
 * ----------------------------
 * A dive is delivered to a client, and the footage has to say which job, which
 * vessel and which system it came from. Keeping that beside the Canvases means
 * copying the Job file carries its provenance with it -- and it is what
 * Phase 8's manifest reads rather than asking the operator a second time.
 *
 * Stored under "modules" -> "mission-capture" -> "job" in the scene collection
 * JSON, which is the sanctioned place for per-collection data: libobs preserves
 * that object even for plugins that are not loaded, so an older build opening a
 * newer Job will not strip it.
 */

namespace MCJobMetadata {

struct Job {
	QString number;  /* Job or project number -- the client's reference */
	QString client;  /* Who the work is for */
	QString vessel;  /* Vessel or platform */
	QString system;  /* ROV or dive system */
	QString notes;   /* Anything the operator wants on the record */
	QString created; /* ISO-8601 UTC, stamped once when the Job is created */

	bool isEmpty() const
	{
		return number.isEmpty() && client.isEmpty() && vessel.isEmpty() && system.isEmpty() && notes.isEmpty();
	}
};

/*
 * Registers the save and preload callbacks. Call once during startup, before
 * the first Job is loaded, or the metadata of that first Job is not read.
 */
void init();

/* Metadata of the Job currently open. Empty if it was not created by the
 * wizard -- Jobs predating 1.8, or an OBS scene collection carried over. */
const Job &current();

/*
 * Replaces the metadata of the current Job. Marks the collection dirty so the
 * change reaches disk with the next save rather than at shutdown.
 */
void setCurrent(const Job &job);

} // namespace MCJobMetadata
