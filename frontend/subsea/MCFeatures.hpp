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
class QDialog;
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
 * WHAT "HIDDEN" MEANS
 * -------------------
 * As of task 1.5, hiding a feature also takes away the other three routes back
 * to it that the UI audit found (docs/subsea/ui-audit.md):
 *
 *   1. its libobs hotkeys are unregistered, so the key no longer fires -- this
 *      is why a hidden Start Streaming cannot be triggered from the keyboard
 *   2. a hidden dock's View > Docks toggle is hidden too, so it cannot be
 *      switched back on
 *   3. settings pages are reachable via the apply() overload below, because the
 *      settings dialog is not a child of the main window
 *
 * A menu left with no visible actions is hidden as well, so nothing opens onto
 * an empty list.
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

	/* Retired by task 1.4; the Layers tree took over. */
	ScenesDock,
	SourcesDock,

	/* The Layers tree that replaces them. */
	LayersDock,

	/* Added by task 1.5 from the UI audit. What's New and the macOS permissions
	 * dialog are absent from the audit's flag list on purpose: upstream already
	 * deletes both outright in this build configuration, so a flag for either
	 * would do nothing but warn forever that it could not find them. */
	SceneViewModes,
	BrowserInteraction,
	PluginManager,
	StreamSettingsPage,
	StreamStatusBar,
	EditMenuOrder,

	/* Task 1.6. Unlike every other flag, this one gates behaviour rather than
	 * a widget: it is read by MCElementTypes, not by apply(). */
	AllSourceTypes,

	/* Task 1.7, also behavioural: read by OBSBasic::CreateFirstRunSources(). */
	DesktopAudio,

	/* Task 1.8. Held back from 1.5 at the user's request until the New Job
	 * wizard existed to replace them -- hiding them earlier would have left a
	 * build with no way to create or switch a Job at all. */
	ProfileMenu,
	SceneCollectionMenu,

	/* Task 1.9a, behavioural: read by MCRecordLock. */
	LockLayersWhileRecording,

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

/*
 * The settings dialog is built on demand and is not a child of the main window,
 * so apply(OBSBasic *) cannot see it. Call this from its constructor, after
 * setupUi(). Without it, pages such as Stream stay fully reachable no matter
 * what their flag says.
 */
size_t apply(QDialog *settings);

/*
 * Unregisters the libobs hotkeys belonging to disabled features.
 *
 * Separate from apply() because it must run after OBSBasic has registered its
 * hotkeys, which happens later in startup than the UI hiding. Matching is by
 * hotkey name, so this needs nothing from OBSBasic's private members.
 *
 * Returns the number of hotkeys unregistered.
 */
size_t unregisterHiddenHotkeys();

} // namespace MCFeatures
