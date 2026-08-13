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

#include "MCFeatures.hpp"
#include "MCBranding.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <util/config-file.h>
#include <util/platform.h>

#include <obs.h>
#include <qt-wrappers.hpp>

#include <QAction>
#include <QDialog>
#include <QDockWidget>
#include <QListWidget>
#include <QMenu>
#include <QStackedWidget>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace MCFeatures {

namespace {

/*
 * Which window owns a feature's objects. The two apply() overloads each run the
 * whole table but only search their own tree, so without this a settings-only
 * row warns "not found" on every startup of the main window, and vice versa.
 */
enum class Scope { MainWindow, Settings };

struct FeatureDef {
	Feature feature;
	const char *key;
	bool defaultEnabled;
	const char *description;
	/* Qt objectNames to hide when the feature is off. Verified against
	 * frontend/forms/*.ui and the widgets created in OBSBasic.cpp. */
	std::initializer_list<const char *> objectNames;
	/* Defaults to the main window, which is where nearly everything lives. */
	Scope scope = Scope::MainWindow;
};

/*
 * The single table. Adding a flag means adding a row here and an enum value.
 *
 * Defaults reflect Mission Capture's product decisions, not OBS's. Dispositions
 * and the reasoning behind each are in docs/subsea/ui-audit.md; this table is
 * the audit made executable, so the two should be changed together.
 */
const std::array<FeatureDef, static_cast<size_t>(Feature::Count_)> featureTable{{
	{Feature::StudioMode, "StudioMode", false, "Preview/Program studio mode switching", {"modeSwitch"}},
	/* Only widgets and actions are listed -- a QLayout is neither, and hiding a
	 * layout is not a thing in Qt. Hide the buttons; the layout collapses. */
	{Feature::ReplayBuffer,
	 "ReplayBuffer",
	 false,
	 "Replay buffer controls",
	 {"replayBufferButton", "saveReplayButton"}},
	{Feature::VirtualCam,
	 "VirtualCam",
	 false,
	 "Virtual camera output",
	 {"virtualCamButton", "virtualCamConfigButton"}},
	{Feature::StreamingUI,
	 "StreamingUI",
	 false,
	 "Streaming controls (re-enabled by Phase 9 with a WHIP panel)",
	 {"streamButton", "broadcastButton"}},
	{Feature::Transitions, "Transitions", false, "Scene transitions dock; inspection work cuts", {"transitionsDock"}},

	{Feature::Stats,
	 "Stats",
	 false,
	 "Upstream stats dock (Phase 1 replaces it with a Health panel)",
	 {"statsDock", "stats"}},
	{Feature::AdvancedAudio,
	 "AdvancedAudio",
	 false,
	 "Advanced audio properties dialog",
	 {"actionAdvAudioProperties", "actionMixerToolbarAdvAudio"}},
	{Feature::SourceToolbar, "SourceToolbar", true, "Context toolbar beneath the preview", {"toggleContextBar"}},

	{Feature::Updater,
	 "Updater",
	 false,
	 "Update check and release notes (endpoints belong to the OBS Project)",
	 {"actionCheckForUpdates", "actionReleaseNotes", "actionRepair", "actionRestartSafe"}},
	{Feature::HelpLinks,
	 "HelpLinks",
	 false,
	 "Help portal, website and Discord links to the OBS Project",
	 {"actionHelpPortal", "actionWebsite", "actionDiscord"}},
	{Feature::LogUpload,
	 "LogUpload",
	 false,
	 "Log and crash-log upload (disabled in code; this hides the menu items)",
	 {"actionUploadCurrentLog", "actionUploadLastLog", "actionUploadLastCrashLog", "menuCrashLogs"}},

	{Feature::AutoConfigWizard,
	 "AutoConfigWizard",
	 false,
	 "OBS auto-configuration wizard (Phase 1 replaces it with New Job)",
	 {"autoConfigure", "autoConfigure2"}},
	{Feature::SceneCollectionImportExport,
	 "SceneCollectionImportExport",
	 false,
	 "Import/export of scene collections and profiles",
	 {"actionImportSceneCollection", "actionExportSceneCollection", "actionImportProfile", "actionExportProfile"}},

	{Feature::IdianPlayground,
	 "IdianPlayground",
	 false,
	 "Upstream widget-gallery developer surface",
	 {"idianPlayground"}},

	{Feature::ScenesDock, "ScenesDock", false, "Scenes dock (Phase 1 replaces it with Layers)", {"scenesDock"}},
	{Feature::SourcesDock, "SourcesDock", false, "Sources dock (Phase 1 replaces it with Layers)", {"sourcesDock"}},
	{Feature::LayersDock,
	 "LayersDock",
	 true,
	 "The Layers tree: Canvases and their Elements in one panel",
	 {"layersDock"}},

	/* Task 1.5, from the UI audit. */
	{Feature::SceneViewModes,
	 "SceneViewModes",
	 false,
	 "List/grid mode for the retired Scenes dock",
	 {"actionSceneListMode", "actionSceneGridMode"}},
	{Feature::BrowserInteraction,
	 "BrowserInteraction",
	 false,
	 "Interact with browser sources (obs-browser is not built in this fork)",
	 {"actionInteract"}},
	{Feature::PluginManager,
	 "PluginManager",
	 false,
	 "Plugin manager; this build ships a fixed plugin set",
	 {"actionOpenPluginManager"}},
	/* Only the page itself. Its inner pages (servicePage, loginPage,
	 * streamKeyPage) live in a nested QStackedWidget that swaps between them, so
	 * hiding them individually would fight that logic for no gain -- taking away
	 * the navigation row makes the whole branch unreachable. */
	{Feature::StreamSettingsPage,
	 "StreamSettingsPage",
	 false,
	 "Stream settings page: services, OAuth and stream keys (Phase 9 replaces it)",
	 {"streamPage"},
	 Scope::Settings},
	{Feature::StreamStatusBar,
	 "StreamStatusBar",
	 false,
	 "Streaming fields in the status bar: uptime, and the stream delay buffer",
	 {"streamFrame", "delayFrame"}},
	/* The submenu entry only. The actions themselves stay visible and keep
	 * their shortcuts -- they moved to the Layers context menu, which is where
	 * the order they act on is actually shown. */
	{Feature::EditMenuOrder,
	 "EditMenuOrder",
	 false,
	 "Order submenu under Edit; the same actions live in the Layers context menu",
	 {"orderMenu"}},
	/* No objectNames: apply() has nothing to hide for this one. MCElementTypes
	 * reads it directly to decide what the Add Element list offers. */
	{Feature::AllSourceTypes,
	 "AllSourceTypes",
	 false,
	 "Offer every OBS source type when adding an Element, not just the three this product uses",
	 {}},
	/* Also behavioural rather than a widget. A new Job gets a comms mic and
	 * nothing else: desktop audio on an inspection recorder captures Windows
	 * notification sounds into the record. */
	{Feature::DesktopAudio,
	 "DesktopAudio",
	 false,
	 "Capture desktop audio on a new Job, in addition to the comms mic",
	 {}},

	/* Task 1.8 replaced both with File > New Job. Switching between existing
	 * Jobs still needs a home -- Scene Collection's list is the only route
	 * today -- so SceneCollectionMenu stays ON until that lands, and only the
	 * Rig menu retires. Tracked as OI-51. */
	{Feature::ProfileMenu,
	 "ProfileMenu",
	 false,
	 "Rig menu; a Rig is created with its Job by the New Job wizard",
	 {"profileMenu"}},
	{Feature::SceneCollectionMenu,
	 "SceneCollectionMenu",
	 true,
	 "Job menu; still the only way to switch between existing Jobs",
	 {"sceneCollectionMenu"}},

	/* On by default. Turning it off restores upstream behaviour, where a
	 * Layers edit mid-recording silently changes the footage. */
	{Feature::LockLayersWhileRecording,
	 "LockLayersWhileRecording",
	 true,
	 "Refuse Layers edits while recording, unless deliberately unlocked",
	 {}},
}};

/* Resolved state, and whether it came from features.ini or the compiled default. */
struct FeatureState {
	bool enabled = true;
	bool fromFile = false;
};

std::array<FeatureState, static_cast<size_t>(Feature::Count_)> states{};
bool loaded = false;

const FeatureDef &def(Feature feature)
{
	return featureTable[static_cast<size_t>(feature)];
}

/* Writes a features.ini listing every flag with its default and a comment, so
 * the file is self-documenting for whoever opens it on a vessel. */
void writeTemplate(const char *path)
{
	FILE *file = os_fopen(path, "w");
	if (!file) {
		blog(LOG_WARNING, "[MCFeatures] Could not create '%s'", path);
		return;
	}

	fprintf(file, "; Mission Capture feature flags.\n");
	fprintf(file, "; Set a value to false to hide a feature, true to show it.\n");
	fprintf(file, "; Restart the application after editing. Values below are the defaults.\n");
	fprintf(file, ";\n");
	fprintf(file, "; Hiding a feature also unregisters its keyboard shortcut and removes it\n");
	fprintf(file, "; from the Docks menu, so there is no way back to it while it is off.\n");
	fprintf(file, "\n[Features]\n");

	for (const FeatureDef &entry : featureTable) {
		fprintf(file, "; %s\n", entry.description);
		fprintf(file, "%s=%s\n\n", entry.key, entry.defaultEnabled ? "true" : "false");
	}

	fclose(file);
	blog(LOG_INFO, "[MCFeatures] Wrote default feature flags to '%s'", path);
}

} // namespace

const char *key(Feature feature)
{
	return def(feature).key;
}

const char *description(Feature feature)
{
	return def(feature).description;
}

bool enabled(Feature feature)
{
	if (!loaded) {
		/* Defensive: a caller before load() gets the compiled default rather
		 * than an uninitialised value. */
		return def(feature).defaultEnabled;
	}
	return states[static_cast<size_t>(feature)].enabled;
}

void load()
{
	/* Sits alongside global.ini and user.ini rather than in a profile: these are
	 * product-level decisions, not per-Rig ones. */
	char path[512];
	if (GetAppConfigPath(path, sizeof(path), MC_CONFIG_DIR "/features.ini") <= 0) {
		blog(LOG_WARNING, "[MCFeatures] Could not resolve features.ini path; using compiled defaults");
		for (size_t i = 0; i < featureTable.size(); i++) {
			states[i] = {featureTable[i].defaultEnabled, false};
		}
		loaded = true;
		return;
	}

	if (!os_file_exists(path)) {
		writeTemplate(path);
	}

	ConfigFile config;
	const bool opened = config.Open(path, CONFIG_OPEN_EXISTING) == CONFIG_SUCCESS;
	if (!opened) {
		blog(LOG_WARNING, "[MCFeatures] Could not read '%s'; using compiled defaults", path);
	}

	for (size_t i = 0; i < featureTable.size(); i++) {
		const FeatureDef &entry = featureTable[i];
		const bool present = opened && config_has_user_value(config, "Features", entry.key);

		states[i].fromFile = present;
		states[i].enabled = present ? config_get_bool(config, "Features", entry.key) : entry.defaultEnabled;
	}

	loaded = true;

	/* Log every flag. A support ticket that includes the log should not need a
	 * follow-up question about which features were on. */
	blog(LOG_INFO, "[MCFeatures] Feature flags (%s):", path);
	for (size_t i = 0; i < featureTable.size(); i++) {
		blog(LOG_INFO, "[MCFeatures]   %-28s %-8s (%s)", featureTable[i].key,
		     states[i].enabled ? "enabled" : "disabled", states[i].fromFile ? "features.ini" : "default");
	}
}

namespace {

/*
 * Hides every objectName belonging to a disabled feature of this scope.
 *
 * Rows outside `scope` are skipped rather than searched and missed, so a
 * "not found" warning always means genuine drift from upstream -- which is the
 * only thing it is useful for.
 */
struct HideResult {
	size_t hidden = 0;
	size_t missing = 0;
};

HideResult hideDisabledObjects(QObject *root, Scope scope)
{
	HideResult result;

	for (size_t i = 0; i < featureTable.size(); i++) {
		if (states[i].enabled || featureTable[i].scope != scope) {
			continue;
		}

		for (const char *objectName : featureTable[i].objectNames) {
			/* QAction is not a QWidget, so both lookups are needed. */
			if (QAction *action = root->findChild<QAction *>(objectName)) {
				action->setVisible(false);
				result.hidden++;
				continue;
			}

			if (QWidget *widget = root->findChild<QWidget *>(objectName)) {
				/* A QMenu is a QWidget, but hiding it does nothing useful:
				 * the popup is hidden anyway until it is opened, and what
				 * shows the entry in the parent menu is its menuAction. This
				 * caught menuCrashLogs, which had been listed under LogUpload
				 * since task 0.4 and was never actually being hidden. */
				if (auto *menu = qobject_cast<QMenu *>(widget); menu && menu->menuAction()) {
					menu->menuAction()->setVisible(false);
				} else {
					widget->setVisible(false);
				}
				result.hidden++;
				continue;
			}

			/* Upstream renamed or removed it. Not fatal -- the feature simply
			 * stays visible -- but it means this table has drifted and should
			 * be checked at the next merge. */
			blog(LOG_WARNING, "[MCFeatures] '%s' (feature %s) not found; feature may still be visible",
			     objectName, featureTable[i].key);
			result.missing++;
		}
	}

	return result;
}

/*
 * A hidden dock keeps a live entry in View > Docks that puts it straight back.
 * Qt creates that action itself, so it has no objectName and the table above
 * cannot name it -- it has to be reached through the dock.
 *
 * setVisible, not setEnabled: setupDockAction() installs an enabledChanged
 * handler that forces the action back to enabled, so disabling does not stick
 * (frontend/widgets/OBSBasic_Docks.cpp).
 *
 * The action is also given a name on the way past, so the UI manifest can see
 * it and a test can assert this stayed fixed. Its absence from the manifest is
 * why task 0.4 reported "0 missing" and still left this open.
 */
size_t hideDockToggles(OBSBasic *main)
{
	size_t hidden = 0;

	for (QDockWidget *dock : main->findChildren<QDockWidget *>()) {
		QAction *toggle = dock->toggleViewAction();
		if (!toggle) {
			continue;
		}

		if (toggle->objectName().isEmpty() && !dock->objectName().isEmpty()) {
			toggle->setObjectName(dock->objectName() + QStringLiteral("Toggle"));
		}

		if (!dock->objectName().isEmpty() && !dock->isVisible() && toggle->isVisible()) {
			/* Only for docks a flag turned off. A dock the operator closed
			 * themselves must keep its way back. */
			const bool flagged =
				std::any_of(featureTable.begin(), featureTable.end(), [&](const FeatureDef &entry) {
					if (states[static_cast<size_t>(entry.feature)].enabled) {
						return false;
					}
					return std::any_of(entry.objectNames.begin(), entry.objectNames.end(),
							   [&](const char *name) {
								   return dock->objectName() == QLatin1String(name);
							   });
				});
			if (flagged) {
				toggle->setVisible(false);
				hidden++;
			}
		}
	}

	return hidden;
}

/*
 * A menu emptied of every item still opens onto nothing. Run last, once all
 * other hiding is done, and only for menus that had actions to begin with --
 * menus populated later at runtime (the per-Canvas projector lists) start empty
 * and would otherwise be hidden before they fill.
 */
size_t hideEmptyMenus(OBSBasic *main)
{
	size_t hidden = 0;

	for (QMenu *menu : main->findChildren<QMenu *>()) {
		QAction *menuAction = menu->menuAction();

		/* Bound once. QMenu::actions() returns the list by value, so calling
		 * it twice for begin() and end() yields iterators into two different
		 * temporaries -- a range that never terminates. */
		const QList<QAction *> actions = menu->actions();

		if (!menuAction || !menuAction->isVisible() || actions.isEmpty()) {
			continue;
		}

		const bool anyVisible = std::any_of(actions.begin(), actions.end(), [](const QAction *action) {
			return action->isVisible() && !action->isSeparator();
		});
		if (!anyVisible) {
			menuAction->setVisible(false);
			hidden++;
			blog(LOG_INFO, "[MCFeatures] Menu '%s' has no visible items; hiding it",
			     QT_TO_UTF8(menu->objectName()));
		}
	}

	return hidden;
}

/*
 * Takes away the navigation row for any settings page a flag disabled.
 *
 * Hiding the page widget itself does nothing useful: QStackedWidget already
 * keeps every page but the current one hidden and shows whichever the sidebar
 * selects, so the row is the only real gate. The row index and the page index
 * are the same by construction -- the .ui lists them in the same order, and
 * OBSBasicSettings connects currentRow to setCurrentIndex -- so the page's own
 * index is looked up rather than a row number being hardcoded here.
 */
size_t hideSettingsNavRows(QDialog *settings)
{
	auto *pages = settings->findChild<QStackedWidget *>("settingsPages");
	auto *nav = settings->findChild<QListWidget *>("listWidget");
	if (!pages || !nav) {
		blog(LOG_WARNING, "[MCFeatures] Settings dialog has no settingsPages/listWidget; pages stay reachable");
		return 0;
	}

	size_t hidden = 0;

	for (size_t i = 0; i < featureTable.size(); i++) {
		if (states[i].enabled || featureTable[i].scope != Scope::Settings) {
			continue;
		}

		for (const char *objectName : featureTable[i].objectNames) {
			QWidget *page = pages->findChild<QWidget *>(objectName);
			if (!page) {
				continue;
			}

			const int index = pages->indexOf(page);
			if (index < 0 || index >= nav->count()) {
				continue;
			}

			nav->item(index)->setHidden(true);
			hidden++;
		}
	}

	/* If the sidebar opened on a page we just took away, move off it. */
	if (hidden > 0) {
		QListWidgetItem *current = nav->currentItem();
		if (!current || current->isHidden()) {
			for (int row = 0; row < nav->count(); row++) {
				if (!nav->item(row)->isHidden()) {
					nav->setCurrentRow(row);
					break;
				}
			}
		}
	}

	return hidden;
}

} // namespace

size_t apply(OBSBasic *main)
{
	if (!main) {
		return 0;
	}

	if (!loaded) {
		blog(LOG_WARNING, "[MCFeatures] apply() called before load(); loading now");
		load();
	}

	const HideResult result = hideDisabledObjects(main, Scope::MainWindow);
	const size_t toggles = hideDockToggles(main);
	const size_t menus = hideEmptyMenus(main);

	blog(LOG_INFO,
	     "[MCFeatures] Applied feature flags: %zu object(s) hidden, %zu dock toggle(s), %zu empty menu(s), %zu not found",
	     result.hidden, toggles, menus, result.missing);

	return result.missing;
}

size_t apply(QDialog *settings)
{
	if (!settings) {
		return 0;
	}

	if (!loaded) {
		blog(LOG_WARNING, "[MCFeatures] apply() called before load(); loading now");
		load();
	}

	const HideResult result = hideDisabledObjects(settings, Scope::Settings);
	const size_t pages = hideSettingsNavRows(settings);

	blog(LOG_INFO,
	     "[MCFeatures] Applied feature flags to settings: %zu object(s) hidden, %zu page(s) unreachable, %zu not found",
	     result.hidden, pages, result.missing);

	return result.missing;
}

size_t unregisterHiddenHotkeys()
{
	if (!loaded) {
		load();
	}

	/*
	 * Hotkey names owned by each feature. Deliberately not part of the table
	 * above: those are Qt objectNames looked up in a widget tree, these are
	 * libobs registrations looked up by name in a global registry, and
	 * conflating the two would make both harder to read.
	 *
	 * Matching by name is what keeps this out of OBSBasic's private members
	 * (streamingHotkeys, replayBufHotkeys and friends), so no upstream file
	 * has to change.
	 */
	struct HotkeyGroup {
		Feature feature;
		std::initializer_list<const char *> names;
		/* Quick transitions are registered one per configured transition with
		 * a numeric suffix ("OBSBasic.QuickTransition.1"), so they can only be
		 * matched on the stem. See OBSBasic_Transitions.cpp. */
		std::initializer_list<const char *> prefixes;
	};

	static const std::array<HotkeyGroup, 3> groups{{
		{Feature::StreamingUI,
		 {"OBSBasic.StartStreaming", "OBSBasic.StopStreaming", "OBSBasic.ForceStopStreaming"},
		 {}},
		{Feature::ReplayBuffer, {"OBSBasic.StartReplayBuffer", "OBSBasic.StopReplayBuffer"}, {}},
		{Feature::Transitions, {"OBSBasic.Transition"}, {"OBSBasic.QuickTransition."}},
	}};

	std::vector<std::string> wanted;
	std::vector<std::string> wantedPrefixes;
	for (const HotkeyGroup &group : groups) {
		if (enabled(group.feature)) {
			continue;
		}
		for (const char *name : group.names) {
			wanted.emplace_back(name);
		}
		for (const char *prefix : group.prefixes) {
			wantedPrefixes.emplace_back(prefix);
		}
	}

	if (wanted.empty() && wantedPrefixes.empty()) {
		return 0;
	}

	/*
	 * Collect first, unregister after: obs_enum_hotkeys holds the hotkey mutex
	 * for the whole iteration, so unregistering from inside the callback would
	 * deadlock.
	 */
	struct Collect {
		const std::vector<std::string> *wanted;
		const std::vector<std::string> *prefixes;
		std::vector<obs_hotkey_id> found;
	} collect{&wanted, &wantedPrefixes, {}};

	obs_enum_hotkeys(
		[](void *param, obs_hotkey_id id, obs_hotkey_t *hotkey) {
			auto *ctx = static_cast<Collect *>(param);
			const char *name = obs_hotkey_get_name(hotkey);
			if (!name) {
				return true;
			}

			const std::string_view key{name};
			const bool exact = std::find(ctx->wanted->begin(), ctx->wanted->end(), key) !=
					   ctx->wanted->end();
			/* rfind(p, 0) rather than starts_with: this target is C++17. */
			const bool prefixed = !exact &&
					      std::any_of(ctx->prefixes->begin(), ctx->prefixes->end(),
							  [&](const std::string &p) { return key.rfind(p, 0) == 0; });

			if (exact || prefixed) {
				ctx->found.push_back(id);
			}
			return true;
		},
		&collect);

	/*
	 * Unregistering one half of a registered pair is safe: obs_hotkey_pair_load
	 * and obs_hotkey_pair_save both null-check each half before touching it
	 * (libobs/obs-hotkey.c), so the leftover pair record is inert. That is what
	 * lets this work by name instead of by pair id.
	 */
	for (const obs_hotkey_id id : collect.found) {
		obs_hotkey_unregister(id);
	}

	blog(LOG_INFO, "[MCFeatures] Unregistered %zu hotkey(s) belonging to hidden features", collect.found.size());

	return collect.found.size();
}

} // namespace MCFeatures
