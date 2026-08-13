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

#include "MCRtspElement.hpp"

#include <OBSApp.hpp>
#include <qt-wrappers.hpp>

#include <util/base.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QRegularExpression>
#include <QUrl>

namespace MCRtspElement {

namespace {

constexpr const char *kSourceId = "ffmpeg_source";

} // namespace

const char *sourceId()
{
	return kSourceId;
}

QString composeUrl(const Config &config)
{
	const QString trimmed = config.url.trimmed();
	if (trimmed.isEmpty() || config.username.isEmpty()) {
		return trimmed;
	}

	/*
	 * QUrl rather than string surgery: a password containing '@' or ':' --
	 * both common in generated credentials -- would otherwise produce a URL
	 * that parses as a different host entirely, and the operator would see a
	 * connection failure with no clue why.
	 */
	QUrl url(trimmed);
	if (!url.isValid()) {
		return trimmed;
	}

	url.setUserName(config.username);
	if (!config.password.isEmpty()) {
		url.setPassword(config.password);
	}

	return url.toString(QUrl::FullyEncoded);
}

QString scrubUrl(const QString &url)
{
	if (url.isEmpty()) {
		return url;
	}

	QUrl parsed(url);
	if (parsed.isValid() && !parsed.password().isEmpty()) {
		parsed.setPassword(QStringLiteral("***"));
		return parsed.toString(QUrl::FullyEncoded);
	}

	/*
	 * Fall back to a pattern for anything QUrl could not parse. A malformed
	 * URL is exactly when a password is most likely to end up in a log, so
	 * failing to parse must not mean failing to scrub.
	 */
	static const QRegularExpression credentials(QStringLiteral("://([^:/@]+):([^@/]+)@"));
	QString out = url;
	out.replace(credentials, QStringLiteral("://\\1:***@"));
	return out;
}

QString ffmpegOptionsFor(const Config &config)
{
	QStringList options;

	/*
	 * Transport first. Parsed by av_dict_parse_string(..., "=", " ", 0) in
	 * shared/media-playback/media-playback/media.c, so this is a space-
	 * separated list of key=value handed straight to the demuxer -- any
	 * AVOption is reachable without touching FFmpeg.
	 */
	options << (config.useTcp ? QStringLiteral("rtsp_transport=tcp") : QStringLiteral("rtsp_transport=udp"));

	switch (config.latency) {
	case Config::Latency::Lowest:
		/* Hand frames on as soon as they arrive and do not wait to build a
		 * reorder buffer. Costs resilience to a jittery link. */
		options << QStringLiteral("fflags=nobuffer") << QStringLiteral("flags=low_delay")
			<< QStringLiteral("max_delay=0");
		break;
	case Config::Latency::Balanced:
		options << QStringLiteral("fflags=nobuffer");
		break;
	case Config::Latency::Stable:
		/* Let FFmpeg buffer normally. For a link that drops packets, a
		 * little delay is cheaper than a picture that keeps breaking up. */
		break;
	}

	return options.join(QLatin1Char(' '));
}

OBSData settingsFor(const Config &config)
{
	OBSDataAutoRelease settings = obs_data_create();

	obs_data_set_bool(settings, "is_local_file", false);
	obs_data_set_string(settings, "input", QT_TO_UTF8(composeUrl(config)));
	obs_data_set_string(settings, "input_format", "rtsp");

	/* No buffering. Upstream's 2 MB is seconds of delay on a live camera, and
	 * a pilot flying on a delayed picture is the failure this avoids. */
	obs_data_set_int(settings, "buffering_mb", 0);

	/* Two seconds, not upstream's ten. On a dive, ten seconds of black is a
	 * long time to wonder whether the camera is coming back. */
	obs_data_set_int(settings, "reconnect_delay_sec", 2);

	obs_data_set_string(settings, "ffmpeg_options", QT_TO_UTF8(ffmpegOptionsFor(config)));

	/* Frees CPU for the encoders in Phase 6. Ignored where unsupported. */
	obs_data_set_bool(settings, "hw_decode", true);

	/*
	 * Keep the connection open when the Element is not on the program Canvas,
	 * for the same reason as the capture Elements in 2.1: Phase 6 records
	 * Canvases that are not on screen, and a camera that disconnects when you
	 * look away records black.
	 */
	obs_data_set_bool(settings, "close_when_inactive", false);
	obs_data_set_bool(settings, "restart_on_activate", false);

	return settings.Get();
}

OBSSceneItem addTo(obs_scene_t *scene, const Config &config, const QString &name)
{
	if (!scene) {
		return nullptr;
	}

	OBSData settings = settingsFor(config);

	OBSSourceAutoRelease source = obs_source_create(kSourceId, QT_TO_UTF8(name), settings, nullptr);
	if (!source) {
		blog(LOG_ERROR, "[MCRtspElement] libobs refused to create '%s'", QT_TO_UTF8(name));
		return nullptr;
	}

	OBSSceneItem item = obs_scene_add(scene, source);

	/* Scrubbed, always. This is the line most likely to end up in a support
	 * ticket. */
	blog(LOG_INFO, "[MCRtspElement] Added '%s' -> %s", QT_TO_UTF8(name), QT_TO_UTF8(scrubUrl(composeUrl(config))));

	return item;
}

bool promptAndAdd(QWidget *parent, obs_scene_t *scene)
{
	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("rtspDialog"));
	dialog.setWindowTitle(QTStr("Rtsp.Title"));

	auto *name = new QLineEdit(&dialog);
	auto *url = new QLineEdit(&dialog);
	auto *user = new QLineEdit(&dialog);
	auto *pass = new QLineEdit(&dialog);
	auto *transport = new QComboBox(&dialog);
	auto *latency = new QComboBox(&dialog);

	name->setObjectName(QStringLiteral("rtspName"));
	url->setObjectName(QStringLiteral("rtspUrl"));
	url->setPlaceholderText(QStringLiteral("rtsp://192.168.1.50:554/stream1"));

	/* Password masked, and never pre-filled from anywhere. */
	pass->setEchoMode(QLineEdit::Password);

	transport->addItem(QTStr("Rtsp.Transport.Tcp"), true);
	transport->addItem(QTStr("Rtsp.Transport.Udp"), false);

	latency->addItem(QTStr("Rtsp.Latency.Lowest"), static_cast<int>(Config::Latency::Lowest));
	latency->addItem(QTStr("Rtsp.Latency.Balanced"), static_cast<int>(Config::Latency::Balanced));
	latency->addItem(QTStr("Rtsp.Latency.Stable"), static_cast<int>(Config::Latency::Stable));
	latency->setCurrentIndex(1);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	auto *form = new QFormLayout(&dialog);
	form->addRow(QTStr("Rtsp.Name"), name);
	form->addRow(QTStr("Rtsp.Url"), url);
	form->addRow(QTStr("Rtsp.Username"), user);
	form->addRow(QTStr("Rtsp.Password"), pass);
	form->addRow(QTStr("Rtsp.Transport"), transport);
	form->addRow(QTStr("Rtsp.Latency"), latency);
	form->addRow(buttons);

	if (dialog.exec() != QDialog::Accepted || url->text().trimmed().isEmpty()) {
		return false;
	}

	Config config;
	config.url = url->text().trimmed();
	config.username = user->text().trimmed();
	config.password = pass->text();
	config.useTcp = transport->currentData().toBool();
	config.latency = static_cast<Config::Latency>(latency->currentData().toInt());

	const QString elementName = name->text().trimmed().isEmpty() ? QTStr("Rtsp.DefaultName")
								     : name->text().trimmed();

	return addTo(scene, config, elementName) != nullptr;
}

} // namespace MCRtspElement
