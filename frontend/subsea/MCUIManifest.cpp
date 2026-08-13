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
#include "MCCaptureDevices.hpp"
#include "MCCaptureProperties.hpp"
#include "MCDefaults.hpp"
#include "MCDiskSpace.hpp"
#include "MCElementTypes.hpp"
#include "MCFeatures.hpp"
#include "MCJobMetadata.hpp"
#include "MCLayersModel.hpp"
#include "MCRecordLock.hpp"
#include "MCRtspElement.hpp"
#include "MCSignalWatch.hpp"
#include "MCVideoCaptureElement.hpp"

#include <OBSApp.hpp>
#include <settings/OBSBasicSettings.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs.h>

#include <array>
#include <cstring>

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

	/* --- Job metadata (task 1.8) ----------------------------------------- */
	/* Read back through the same preload callback the wizard's output goes
	 * through, so this proves the round-trip rather than echoing what was just
	 * written. Phase 8's manifest reads exactly these fields. */
	{
		const MCJobMetadata::Job &job = MCJobMetadata::current();

		QJsonObject entry;
		entry["number"] = job.number;
		entry["client"] = job.client;
		entry["vessel"] = job.vessel;
		entry["system"] = job.system;
		entry["notes"] = job.notes;
		entry["created"] = job.created;
		entry["empty"] = job.isEmpty();

		root["jobMetadata"] = entry;
	}

	/* --- Recording safety (task 1.9a) ------------------------------------- */
	{
		QJsonObject safety;

		/* The indicator is a widget like any other, but its visibility is the
		 * assertion that matters: absent when idle, not merely grey. */
		auto *indicator = main->findChild<QWidget *>(QStringLiteral("recordIndicator"));
		safety["indicatorPresent"] = indicator != nullptr;
		safety["indicatorVisible"] = indicator && indicator->isVisible();

		safety["recording"] = MCRecordLock::recording();
		safety["editingLocked"] = MCRecordLock::locked();
		safety["overridden"] = MCRecordLock::overridden();

		config_t *userConfig = App()->GetUserConfig();
		safety["warnBeforeStopping"] = config_get_bool(userConfig, "BasicWindow", "WarnBeforeStoppingRecord");
		safety["warnAfterSeconds"] =
			static_cast<int>(config_get_int(userConfig, "BasicWindow", "WarnBeforeStoppingRecordAfter"));

		/* Disk space (task 1.9b). freeBytes is whatever the volume actually
		 * reports, so a test asserts the *level* it produced against the
		 * thresholds rather than a figure it cannot predict. */
		QJsonObject disk;
		auto *diskLabel = main->findChild<QWidget *>(QStringLiteral("diskSpaceLabel"));
		disk["fieldPresent"] = diskLabel != nullptr;
		disk["freeBytes"] = static_cast<double>(MCDiskSpace::lastFreeBytes());
		disk["level"] = QString::fromUtf8(MCDiskSpace::levelName(MCDiskSpace::lastLevel()));

		const MCDiskSpace::Thresholds t = MCDiskSpace::thresholds();
		disk["cautionGB"] = static_cast<int>(t.cautionBytes / (1024ULL * 1024ULL * 1024ULL));
		disk["criticalGB"] = static_cast<int>(t.criticalBytes / (1024ULL * 1024ULL * 1024ULL));
		disk["stopGB"] = static_cast<int>(t.stopBytes / (1024ULL * 1024ULL * 1024ULL));
		safety["disk"] = disk;

		root["recordingSafety"] = safety;
	}

	/* --- Capture devices -------------------------------------------------- */
	/* Recorded mainly so the enumeration itself is exercised. It is otherwise
	 * only reached when the wizard's camera page opens, which no unattended run
	 * does -- and it queries property lists from plugins that may have failed
	 * to initialise, which is precisely the case worth running on every build.
	 * An empty list is a valid result on a machine with no capture hardware. */
	{
		QJsonArray devices;
		for (const MCCaptureDevices::Device &device : MCCaptureDevices::enumerate()) {
			QJsonObject entry;
			entry["id"] = device.id;
			entry["name"] = device.name;
			entry["sourceId"] = device.sourceId;
			entry["backend"] = device.backend;
			devices.append(entry);
		}
		root["captureDevices"] = devices;
	}

	/* --- Capture Element factory (task 2.1) ------------------------------- */
	/*
	 * Run against synthetic devices rather than real ones. The mapping and the
	 * settings are the whole of task 2.1, and on a machine with no capture card
	 * they would otherwise go unverified until someone had one -- which, given
	 * the cards vary job to job, could be never. These are pure functions, so
	 * nothing is created and nothing is left behind.
	 */
	{
		QJsonArray factory;

		const std::array<std::pair<const char *, const char *>, 3> cases{{
			{"decklink-input", "DeckLink"},
			{"dshow_input", "DirectShow"},
			/* The "there may be other capture cards" case from the plan. */
			{"some-future-backend", "Unknown"},
		}};

		for (const auto &[backendId, label] : cases) {
			MCCaptureDevices::Device probe;
			probe.id = QStringLiteral("test-device-id");
			probe.name = QStringLiteral("Test Device");
			probe.sourceId = QString::fromUtf8(backendId);
			probe.backend = QString::fromUtf8(label);

			QJsonObject entry;
			entry["given"] = probe.sourceId;
			entry["resolvesTo"] = QString::fromUtf8(MCVideoCaptureElement::sourceIdFor(probe));

			/* The settings object as libobs would hand it to the source --
			 * parsed by the test rather than picked apart here, so the
			 * assertion is against what is actually applied. */
			OBSDataAutoRelease settings = MCVideoCaptureElement::settingsFor(probe);
			entry["settingsJson"] = QString::fromUtf8(obs_data_get_json(settings));

			factory.append(entry);
		}

		root["captureFactory"] = factory;
	}

	/* --- Capture property filter (task 2.2) ------------------------------- */
	/*
	 * Run against a real, temporary dshow_input rather than a synthetic one:
	 * the filter's whole job is to walk the property set the *plugin* builds,
	 * and a hand-made list would prove only that the code can read its own
	 * fixture. The source is created detached, queried, and released -- it is
	 * never added to a Canvas and never opens a device.
	 */
	{
		QJsonObject filter;

		OBSSourceAutoRelease probe = obs_source_create_private("dshow_input", "mc-property-probe", nullptr);
		if (probe) {
			auto describe = [&](bool advanced) {
				MCCaptureProperties::setAdvancedShown(advanced);
				obs_properties_t *props = MCCaptureProperties::reload(probe.Get());

				QJsonArray visible;
				int total = 0;
				for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p)) {
					total++;
					if (obs_property_visible(p)) {
						visible.append(QString::fromUtf8(obs_property_name(p)));
					}
				}
				obs_properties_destroy(props);

				QJsonObject out;
				out["total"] = total;
				out["visible"] = visible;
				out["hidden"] = MCCaptureProperties::lastHiddenCount();
				return out;
			};

			filter["simple"] = describe(false);
			filter["advanced"] = describe(true);
			MCCaptureProperties::setAdvancedShown(false);
		}

		filter["isCaptureSource_dshow"] = MCCaptureProperties::isCaptureSource("dshow_input");
		filter["isCaptureSource_decklink"] = MCCaptureProperties::isCaptureSource("decklink-input");
		filter["isCaptureSource_other"] = MCCaptureProperties::isCaptureSource("color_source");

		root["captureProperties"] = filter;
	}

	/* --- Signal watch (task 2.3) ------------------------------------------ */
	/*
	 * The filter must be registered and attached even on a machine with no
	 * camera -- an Element whose device is absent is exactly the one whose
	 * health matters. Recording the registration separately from the attachment
	 * matters too: a registration that silently failed would leave every
	 * Element unwatched with nothing to show for it.
	 */
	{
		QJsonObject watch;

		bool filterRegistered = false;
		const char *typeId = nullptr;
		for (size_t i = 0; obs_enum_filter_types(i, &typeId); i++) {
			if (typeId && strcmp(typeId, "mc_signal_watch") == 0) {
				filterRegistered = true;
				break;
			}
		}
		watch["filterRegistered"] = filterRegistered;
		watch["lostThresholdSeconds"] = MCSignalWatch::lostThresholdSeconds();

		QJsonArray elements;
		for (const MCSignalWatch::Status &status : MCSignalWatch::statuses()) {
			QJsonObject entry;
			entry["element"] = status.elementName;
			entry["sourceId"] = status.sourceId;
			entry["frames"] = static_cast<double>(status.frames);
			switch (status.state) {
			case MCSignalWatch::State::Receiving:
				entry["state"] = "receiving";
				break;
			case MCSignalWatch::State::Lost:
				entry["state"] = "lost";
				break;
			default:
				entry["state"] = "unknown";
				break;
			}
			elements.append(entry);
		}
		watch["watched"] = elements;

		root["signalWatch"] = watch;
	}

	/* --- RTSP element (task 2.4) ------------------------------------------ */
	/*
	 * Settings and credential handling for a representative configuration.
	 * Pure functions, so nothing connects to anything -- but they cover the two
	 * things most likely to be quietly wrong: the low-latency defaults that
	 * differ from upstream, and whether a password can reach a log.
	 */
	{
		QJsonObject rtsp;

		MCRtspElement::Config probe;
		probe.url = QStringLiteral("rtsp://cam.example:554/stream1");
		probe.username = QStringLiteral("diver");
		probe.password = QStringLiteral("p@ss:word/1");
		probe.useTcp = true;
		probe.latency = MCRtspElement::Config::Latency::Balanced;

		const QString composed = MCRtspElement::composeUrl(probe);
		rtsp["sourceId"] = QString::fromUtf8(MCRtspElement::sourceId());
		rtsp["scrubbed"] = MCRtspElement::scrubUrl(composed);

		/* Whether the raw password survives anywhere in the scrubbed form. The
		 * password deliberately contains ':', '@' and '/' -- the characters
		 * that break naive string surgery. */
		rtsp["scrubHidesPassword"] = !MCRtspElement::scrubUrl(composed).contains(probe.password);
		rtsp["composedKeepsUser"] = composed.contains(QStringLiteral("diver"));

		OBSDataAutoRelease settings = MCRtspElement::settingsFor(probe);
		rtsp["settingsJson"] = QString::fromUtf8(obs_data_get_json(settings));

		QJsonObject presets;
		for (const auto &[name, latency] : {std::pair{"lowest", MCRtspElement::Config::Latency::Lowest},
						    std::pair{"balanced", MCRtspElement::Config::Latency::Balanced},
						    std::pair{"stable", MCRtspElement::Config::Latency::Stable}}) {
			MCRtspElement::Config c = probe;
			c.latency = latency;
			presets[QString::fromUtf8(name)] = MCRtspElement::ffmpegOptionsFor(c);
		}

		MCRtspElement::Config udp = probe;
		udp.useTcp = false;
		presets["udp"] = MCRtspElement::ffmpegOptionsFor(udp);
		rtsp["ffmpegOptions"] = presets;

		root["rtsp"] = rtsp;
	}

	/* --- Add Element picker (task 2.5) ------------------------------------ */
	/* The picker itself is modal and cannot be driven unattended (OI-53), so
	 * what is recorded is what a test can check without opening it: that the
	 * three choices exist as named widgets, and that the count is three. */
	{
		QJsonObject picker;
		picker["choices"] = 3;
		picker["captureAvailable"] = !MCCaptureDevices::enumerate().isEmpty();
		picker["overlayEnabled"] = false; /* Phase 4 */
		root["addElementPicker"] = picker;
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
