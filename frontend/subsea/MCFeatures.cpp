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

#include <QAction>
#include <QWidget>

#include <array>
#include <cstdio>
#include <initializer_list>

namespace MCFeatures {

namespace {

struct FeatureDef {
	Feature feature;
	const char *key;
	bool defaultEnabled;
	const char *description;
	/* Qt objectNames to hide when the feature is off. Verified against
	 * frontend/forms/*.ui and the widgets created in OBSBasic.cpp. */
	std::initializer_list<const char *> objectNames;
};

/*
 * The single table. Adding a flag means adding a row here and an enum value.
 *
 * Defaults reflect Mission Capture's product decisions, not OBS's. The two dock
 * flags default ON because Phase 1 has not yet built the Layers tree that
 * replaces them -- turning them off now would leave the app with no way to
 * select a Canvas.
 */
const std::array<FeatureDef, static_cast<size_t>(Feature::Count_)> featureTable{{
	{Feature::StudioMode, "StudioMode", false, "Preview/Program studio mode switching", {"modeSwitch"}},
	/* Only widgets and actions are listed -- a QLayout is neither, and hiding a
	 * layout is not a thing in Qt. Hide the buttons; the layout collapses. */
	{Feature::ReplayBuffer, "ReplayBuffer", false, "Replay buffer controls", {"replayBufferButton", "saveReplayButton"}},
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

	{Feature::Stats, "Stats", false, "Upstream stats dock (Phase 1 replaces it with a Health panel)", {"statsDock", "stats"}},
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
	 {"actionImportSceneCollection", "actionExportSceneCollection", "actionImportProfile",
	  "actionExportProfile"}},

	{Feature::IdianPlayground, "IdianPlayground", false, "Upstream widget-gallery developer surface", {"idianPlayground"}},

	{Feature::ScenesDock, "ScenesDock", true, "Scenes dock (Phase 1 replaces it with Layers)", {"scenesDock"}},
	{Feature::SourcesDock, "SourcesDock", true, "Sources dock (Phase 1 replaces it with Layers)", {"sourcesDock"}},
	{Feature::LayersDock, "LayersDock", true, "The Layers tree: Canvases and their Elements in one panel", {"layersDock"}},
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
	fprintf(file, "; NOTE: hiding a feature does not currently disable its keyboard shortcut.\n");
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

size_t apply(OBSBasic *main)
{
	if (!main) {
		return 0;
	}

	if (!loaded) {
		blog(LOG_WARNING, "[MCFeatures] apply() called before load(); loading now");
		load();
	}

	size_t missing = 0;
	size_t hidden = 0;

	for (size_t i = 0; i < featureTable.size(); i++) {
		if (states[i].enabled) {
			continue;
		}

		for (const char *objectName : featureTable[i].objectNames) {
			/* QAction is not a QWidget, so both lookups are needed. */
			if (QAction *action = main->findChild<QAction *>(objectName)) {
				action->setVisible(false);
				hidden++;
				continue;
			}

			if (QWidget *widget = main->findChild<QWidget *>(objectName)) {
				widget->setVisible(false);
				hidden++;
				continue;
			}

			/* Upstream renamed or removed it. Not fatal -- the feature simply
			 * stays visible -- but it means this table has drifted and should
			 * be checked at the next merge. */
			blog(LOG_WARNING, "[MCFeatures] '%s' (feature %s) not found; feature may still be visible",
			     objectName, featureTable[i].key);
			missing++;
		}
	}

	blog(LOG_INFO, "[MCFeatures] Applied feature flags: %zu object(s) hidden, %zu not found", hidden, missing);

	return missing;
}

} // namespace MCFeatures
