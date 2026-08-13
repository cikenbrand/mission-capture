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

#include <util/config-file.h>

#include <string>

/*
 * Mission Capture recording defaults -- the Rig template.
 *
 * WHY DEFAULTS AND NOT SETTINGS
 * -----------------------------
 * Everything here goes through config_set_default_*, which only takes effect
 * where the operator has not chosen a value. That is the whole point: a Rig
 * that has been tuned on a vessel must survive an upgrade, while a fresh Rig
 * must come up recording something usable without anyone opening Settings.
 *
 * The exception is called out at its call site: base resolution is force-set by
 * upstream before we run, so it cannot be steered from here.
 *
 * WHAT MAKES THESE DIFFERENT FROM OBS's
 * -------------------------------------
 * OBS defaults are tuned for streaming to a platform: a bitrate-targeted
 * encoder, a container that streams well, and audio from the desktop. An
 * inspection recorder wants none of that. See docs/subsea/phase-1-shell-and-
 * layers.md task 1.7 for the reasoning behind each value.
 */

namespace MCDefaults {

/*
 * Container, rate control, filename template and split behaviour.
 *
 * Call FIRST in OBSBasic::InitBasicConfigDefaults(), before upstream sets its
 * own. This looks backwards and is not: config_set_item_default() copies the
 * value into the user section map when no user value exists, and reads consult
 * that map first, so the *first* default set for a key is the effective one.
 * Setting ours afterwards would be silently ignored.
 */
void apply(config_t *config);

/*
 * Recording encoder, chosen from what the machine actually has.
 *
 * Separate from apply() because upstream splits it the same way: encoder
 * probing needs libobs up, so it happens in InitBasicConfigDefaults2(). Call
 * first there too, for the same reason.
 */
void applyEncoders(config_t *config);

/*
 * Expands Mission Capture's own filename tokens.
 *
 * %JOB%    -- the current Job (scene collection)
 * %CANVAS% -- the Canvas being recorded
 *
 * libobs' os_generate_formatted_filename() knows the date and time tokens but
 * nothing about our vocabulary, so these are substituted before it runs. Both
 * are sanitised for use in a filename.
 */
std::string expandTokens(const char *format);

} // namespace MCDefaults
