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

#include <string>

class OBSBasic;

/*
 * Mission Capture UI manifest.
 *
 * Writes a JSON description of the application's visible UI -- menu actions,
 * docks, element types, and feature-flag state -- so that decluttering can be
 * asserted by a test instead of eyeballed by a person.
 *
 * Phase 1 removes a great deal of UI. Without a machine-readable record, "did
 * we hide the right things, and only those things?" is answerable only by
 * clicking through menus, and regressions creep back silently on every upstream
 * merge. The harness diffs this manifest against a golden copy
 * (docs/subsea/testing.md, suite T1).
 *
 * Also records each action's keyboard shortcut. Hiding a QAction does not
 * unregister its shortcut (see MCFeatures.hpp), so the manifest is what makes
 * that leak visible and testable once Phase 1 task 1.5 addresses it.
 */

namespace MCUIManifest {

/*
 * Serialises the main window's UI to JSON at `path`.
 *
 * Call after the window is fully constructed and MCFeatures::apply() has run,
 * otherwise everything still looks visible. Returns false on write failure.
 */
bool write(OBSBasic *main, const std::string &path);

} // namespace MCUIManifest
