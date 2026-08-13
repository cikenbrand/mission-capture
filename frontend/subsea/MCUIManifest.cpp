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

#include "MCUIManifest.hpp"
#include "MCDefaults.hpp"
#include "MCElementTypes.hpp"
#include "MCFeatures.hpp"
#include "MCLayersModel.hpp"

#include <OBSApp.hpp>
#include <settings/OBSBasicSettings.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs.h>

#include <QAction>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QMenu>
#include <QStackedWidget>

namespace MCUIManifest {

namespace {

QJsonObject describeAction(const QAction *action)
{
	QJsonObject entry;
	entry["name"] = action->objectName();
	entry["text"] = action->text().remove('&');
	entry["visible"] = action->isVisible();
	entry["enabled"] = action->isEnabled();
	entry["checkable"] = action->isCheckable();

	/* A shortcut on a hidden action is the hotkey-leak case in OI-23: the menu
	 * item is gone but the key still fires. Recording it here is what lets a
	 * test assert the leak is closed. */
	const QKeySequence shortcut = action->shortcut();
	if (!shortcut.isEmpty()) {
		entry["shortcut"] = shortcut.toString(QKeySequence::PortableText);
	}

	return entry;
}

} // namespace

bool write(OBSBasic *main, const std::string &path)
{
	if (!main) {
		return false;
	}

	QJsonObject root;
	root["schema"] = 1;
	root["product"] = QString::fromUtf8(obs_get_version_string());

	/* --- Menu actions ------------------------------------------------- */
	QJsonArray actions;
	QJsonArray hiddenActions;

	for (const QAction *action : main->findChildren<QAction *>()) {
		/* Unnamed actions are separators and dynamically generated entries;
		 * they carry no identity worth diffing against a golden file. */
		if (action->objectName().isEmpty()) {
			continue;
		}

		const QJsonObject entry = describeAction(action);
		if (action->isVisible()) {
			actions.append(entry);
		} else {
			hiddenActions.append(entry);
		}
	}

	root["actions"] = actions;
	root["hiddenActions"] = hiddenActions;

	/* --- Docks --------------------------------------------------------- */
	QJsonArray docks;
	for (const QDockWidget *dock : main->findChildren<QDockWidget *>()) {
		if (dock->objectName().isEmpty()) {
			continue;
		}

		QJsonObject entry;
		entry["name"] = dock->objectName();
		entry["title"] = dock->windowTitle();
		entry["visible"] = dock->isVisible();
		entry["floating"] = dock->isFloating();
		docks.append(entry);
	}
	root["docks"] = docks;

	/* --- Menus --------------------------------------------------------- */
	QJsonArray menus;
	for (const QMenu *menu : main->findChildren<QMenu *>()) {
		if (menu->objectName().isEmpty()) {
			continue;
		}

		QJsonObject entry;
		entry["name"] = menu->objectName();
		entry["title"] = menu->title().remove('&');
		entry["visible"] = menu->menuAction() ? menu->menuAction()->isVisible() : true;
		menus.append(entry);
	}
	root["menus"] = menus;

	/* --- Element types -------------------------------------------------- */
	/* Every registered type is listed, each marked with whether task 1.6's
	 * filter offers it in Add Element. Recording both halves is deliberate: the
	 * point of that task is that unoffered types stay *registered*, so a Job
	 * referencing one still loads. A list of only the offered types could not
	 * tell the difference between filtered and unregistered. */
	QJsonArray elementTypes;
	const char *versionedId = nullptr;
	const char *unversionedId = nullptr;
	for (size_t idx = 1; obs_enum_input_types2(idx, &versionedId, &unversionedId); idx++) {
		const uint32_t caps = obs_get_source_output_flags(versionedId);
		if (caps & OBS_SOURCE_CAP_DISABLED) {
			continue;
		}

		QJsonObject entry;
		entry["id"] = QString::fromUtf8(versionedId);
		entry["unversionedId"] = QString::fromUtf8(unversionedId ? unversionedId : versionedId);
		entry["name"] = QString::fromUtf8(obs_source_get_display_name(versionedId));
		entry["offered"] = MCElementTypes::allowed(unversionedId ? unversionedId : versionedId);
		elementTypes.append(entry);
	}
	root["elementTypes"] = elementTypes;

	/* --- Layers model ---------------------------------------------------- */
	/* Task 1.2 builds the model before task 1.3 builds the view, so this is
	 * how the model's structure and ordering are verified: walk it exactly as
	 * a QTreeView would and record what it reports. */
	{
		MCLayersModel layers;
		QJsonArray canvases;

		for (int c = 0; c < layers.rowCount(); c++) {
			const QModelIndex canvasIndex = layers.index(c, 0);

			QJsonObject canvas;
			canvas["name"] = layers.data(canvasIndex, Qt::DisplayRole).toString();
			canvas["program"] = layers.data(canvasIndex, MCLayersModel::ProgramRole).toBool();

			QJsonArray elements;
			for (int e = 0; e < layers.rowCount(canvasIndex); e++) {
				const QModelIndex elementIndex = layers.index(e, 0, canvasIndex);

				QJsonObject element;
				element["name"] = layers.data(elementIndex, Qt::DisplayRole).toString();
				element["sourceId"] = layers.data(elementIndex, MCLayersModel::SourceIdRole).toString();
				element["visible"] = layers.data(elementIndex, MCLayersModel::VisibleRole).toBool();
				element["locked"] = layers.data(elementIndex, MCLayersModel::LockedRole).toBool();

				/* The reversal that matters: row 0 must be the topmost
				 * item, which is the LAST index libobs enumerates. */
				const int count = layers.rowCount(canvasIndex);
				element["row"] = e;
				element["libobsIndex"] = MCLayersModel::toLibobsIndex(e, count);

				/* parent() must round-trip, or the view shows orphans. */
				element["parentResolves"] = (layers.parent(elementIndex) == canvasIndex);

				elements.append(element);
			}

			canvas["elements"] = elements;
			canvases.append(canvas);
		}

		root["layers"] = canvases;
	}

	/* --- Recording defaults (task 1.7) ----------------------------------- */
	/* The Rig template is applied with config_set_default_*, which by design
	 * writes nothing to basic.ini -- only operator overrides land there. So the
	 * effective values cannot be read off disk, and without recording them here
	 * the defaults would have no automated verification at all. */
	{
		config_t *config = main->Config();
		QJsonObject defaults;

		/* Section-qualified keys. RecFormat2 exists under both SimpleOutput
		 * and AdvOut with different values, so a bare key name would let one
		 * silently overwrite the other. */
		auto str = [&](const char *section, const char *key) {
			const char *value = config_get_string(config, section, key);
			defaults[QString("%1/%2").arg(section, key)] = QString::fromUtf8(value ? value : "");
		};
		auto flag = [&](const char *section, const char *key) {
			defaults[QString("%1/%2").arg(section, key)] = config_get_bool(config, section, key);
		};

		str("Output", "Mode");
		str("SimpleOutput", "RecFormat2");
		str("SimpleOutput", "RecQuality");
		str("SimpleOutput", "RecEncoder");
		str("Output", "FilenameFormatting");
		str("AdvOut", "RecFormat2");
		str("AdvOut", "RecSplitFileType");

		flag("Video", "AutoRemux");
		flag("AdvOut", "RecSplitFile");
		defaults["AdvOut/RecSplitFileTime"] =
			static_cast<int>(config_get_uint(config, "AdvOut", "RecSplitFileTime"));

		/* Resolved, not just templated: proves the tokens actually expand. */
		defaults["filenameExample"] = QString::fromStdString(
			MCDefaults::expandTokens(config_get_string(config, "Output", "FilenameFormatting")));

		/* Which global audio channels a new Job came up with. Channel 1 is
		 * desktop audio and should be empty; channel 3 is the comms mic. */
		QJsonArray audioChannels;
		for (uint32_t channel = 1; channel <= 6; channel++) {
			OBSSourceAutoRelease source = obs_get_output_source(channel);
			if (!source) {
				continue;
			}

			QJsonObject entry;
			entry["channel"] = static_cast<int>(channel);
			entry["name"] = QString::fromUtf8(obs_source_get_name(source));
			entry["id"] = QString::fromUtf8(obs_source_get_id(source));
			audioChannels.append(entry);
		}
		defaults["audioChannels"] = audioChannels;

		root["recordingDefaults"] = defaults;
	}

	/* --- Settings dialog ------------------------------------------------ */
	/* Built here for the same reason the Layers model is above: the settings
	 * dialog is not a child of the main window, so nothing else in this dump
	 * can see it -- and that invisibility is exactly why task 1.5 found the
	 * Stream page still fully reachable with StreamingUI off. Recording which
	 * navigation rows survive is what lets a test hold that fix in place.
	 *
	 * Safe in a dump-and-exit run: the dialog is constructed, read and
	 * destroyed without ever being shown. */
	{
		OBSBasicSettings settings(main);

		QJsonArray pages;
		auto *nav = settings.findChild<QListWidget *>("listWidget");
		auto *stack = settings.findChild<QStackedWidget *>("settingsPages");

		if (nav && stack) {
			for (int row = 0; row < nav->count(); row++) {
				QJsonObject entry;
				entry["row"] = row;
				entry["text"] = nav->item(row)->text();
				entry["visible"] = !nav->item(row)->isHidden();

				if (QWidget *page = stack->widget(row)) {
					entry["page"] = page->objectName();
				}

				pages.append(entry);
			}
		}

		root["settingsPages"] = pages;
	}

	/* --- OBS hotkeys ---------------------------------------------------- */
	/* Distinct from QAction shortcuts, and this is where the real leak lives:
	 * OBS registers hotkeys such as Start Replay Buffer through libobs, so
	 * hiding the button leaves the hotkey registered and firing. Phase 1 task
	 * 1.5 must unregister these for disabled features, and this list is how the
	 * test proves it. */
	QJsonArray hotkeys;
	obs_enum_hotkeys(
		[](void *param, obs_hotkey_id, obs_hotkey_t *hotkey) {
			auto *array = static_cast<QJsonArray *>(param);
			QJsonObject entry;
			entry["name"] = QString::fromUtf8(obs_hotkey_get_name(hotkey));
			entry["description"] = QString::fromUtf8(obs_hotkey_get_description(hotkey));
			array->append(entry);
			return true;
		},
		&hotkeys);
	root["hotkeys"] = hotkeys;

	/* --- Feature flags -------------------------------------------------- */
	QJsonArray features;
	for (size_t i = 0; i < static_cast<size_t>(MCFeatures::Feature::Count_); i++) {
		const auto feature = static_cast<MCFeatures::Feature>(i);

		QJsonObject entry;
		entry["key"] = QString::fromUtf8(MCFeatures::key(feature));
		entry["enabled"] = MCFeatures::enabled(feature);
		features.append(entry);
	}
	root["features"] = features;

	/* --- Write ---------------------------------------------------------- */
	QFile file(QString::fromStdString(path));
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
		blog(LOG_ERROR, "[MCUIManifest] Could not open '%s' for writing", path.c_str());
		return false;
	}

	const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
	const bool written = file.write(json) == json.size();
	file.close();

	if (!written) {
		blog(LOG_ERROR, "[MCUIManifest] Failed writing '%s'", path.c_str());
		return false;
	}

	blog(LOG_INFO, "[MCUIManifest] Wrote %lld actions, %lld hidden, %lld docks, %lld element types to '%s'",
	     static_cast<long long>(actions.size()), static_cast<long long>(hiddenActions.size()),
	     static_cast<long long>(docks.size()), static_cast<long long>(elementTypes.size()), path.c_str());

	return true;
}

} // namespace MCUIManifest
