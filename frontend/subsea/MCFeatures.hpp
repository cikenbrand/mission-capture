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

#include <cstddef>

class QWidget;
class OBSBasic;

/*
 * Mission Capture feature flags.
 *
 * WHY
 * ---
 * The fork removes a great deal of OBS's product surface, but it must not
 * remove the *code*: deleting upstream files or ripping blocks out of
 * OBSBasic.cpp would conflict on every merge, forever. See the fork strategy in
 * docs/subsea/README.md.
 *
 * So features are hidden, not deleted. Each flag names one or more Qt objects
 * by objectName; when the flag is off, apply() finds them and hides them.
 *
 * HOW TO ADD A FLAG
 * -----------------
 * Add an entry to the Feature enum and a matching row to the table in
 * MCFeatures.cpp. The row lists the config key, the compiled-in default, and
 * the objectNames to hide. Nothing else needs to change.
 *
 * OVERRIDING IN THE FIELD
 * -----------------------
 * <config>/features.ini, section [Features]. The file is written with all
 * defaults and explanatory comments on first run, so a support engineer can
 * see what exists and flip a value without a rebuild. Every flag's state and
 * where it came from is written to the application log at startup.
 *
 * LIMITATION -- READ THIS
 * -----------------------
 * Hiding a QAction does NOT unregister its hotkey. A hidden feature can still
 * be triggered by its keyboard shortcut. Unregistering hotkeys for hidden
 * features is Phase 1 task 1.5; until that lands, treat these flags as
 * "removed from view", not "removed".
 */

namespace MCFeatures {

enum class Feature {
	/* Broadcast-oriented UI with no role in inspection work */
	StudioMode,
	ReplayBuffer,
	VirtualCam,
	StreamingUI,
	Transitions,

	/* Panels and tools */
	Stats,
	AdvancedAudio,
	SourceToolbar,

	/* Things that reach the network or the OBS Project */
	Updater,
	HelpLinks,
	LogUpload,

	/* Setup flows we replace with our own */
	AutoConfigWizard,
	SceneCollectionImportExport,

	/* Developer surfaces */
	IdianPlayground,

	/* Phase 1 replaces these two docks with the Layers tree. Kept ON here so
	 * 0.4 does not silently break the app before that work exists. */
	ScenesDock,
	SourcesDock,

	Count_
};

/* True if the feature should be present in the UI. */
bool enabled(Feature feature);

/* Stable config key, e.g. "StudioMode". Also what appears in features.ini. */
const char *key(Feature feature);

/* One-line explanation, written into features.ini as a comment. */
const char *description(Feature feature);

/*
 * Loads features.ini (creating it with documented defaults if absent) and logs
 * every flag's state and origin. Safe to call more than once; later calls
 * reload. Called from OBSApp before the main window is constructed.
 */
void load();

/*
 * Hides the Qt objects belonging to every disabled feature.
 *
 * Call once, late in OBSBasic::OBSInit(), after the UI and docks exist. An
 * objectName that cannot be found is logged as a warning rather than treated as
 * fatal -- upstream renames widgets from time to time, and a renamed widget
 * should degrade to "this feature is visible again", not a crash.
 *
 * Returns the number of objectNames that could not be found, so tests and the
 * startup log can notice drift.
 */
size_t apply(OBSBasic *main);

} // namespace MCFeatures
