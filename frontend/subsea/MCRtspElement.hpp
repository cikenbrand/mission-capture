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

#include <obs.hpp>

#include <QString>

class QWidget;

/*
 * An RTSP camera as an Element.
 *
 * NOT A NEW DECODER, AND NOT A NEW PLUGIN
 * ---------------------------------------
 * OBS already speaks RTSP: ffmpeg_source with is_local_file off is a network
 * demuxer with reconnection built in. The work here is the same as task 2.1's
 * -- settings, defaults and failure behaviour -- not transport code. The phase
 * doc sketched a plugins/mc-rtsp/ wrapper; a frontend factory does the same job
 * without a new build target, and matches what 2.1 settled on.
 *
 * WHY THE DEFAULTS ARE NOT OBS's
 * ------------------------------
 * OBS tunes ffmpeg_source for playing a file or a stream you are watching. Two
 * of its defaults are actively wrong for a camera someone is flying on:
 *
 *   buffering_mb        2 MB is seconds of latency on a live feed
 *   reconnect_delay_sec 10 s of black is a long time on a dive
 *
 * TCP, NOT UDP
 * ------------
 * RTP over UDP drops packets silently and produces smeared macroblocks that
 * look exactly like a camera fault -- the operator ends up diagnosing the
 * wrong thing. On a vessel LAN retransmission is nearly free, and the failure
 * mode becomes honest: either the picture is right or the connection is
 * visibly down. UDP stays available for cameras that need it.
 */

namespace MCRtspElement {

/* Everything the operator supplies. Credentials are kept apart from the URL
 * until the moment the source is built -- see composeUrl(). */
struct Config {
	QString url;
	QString username;
	QString password;
	bool useTcp = true;

	/* Lowest trades resilience for delay, Stable does the reverse. Balanced
	 * is the default and is what a normal vessel LAN wants. */
	enum class Latency { Lowest, Balanced, Stable };
	Latency latency = Latency::Balanced;
};

/*
 * Builds the connection URL with credentials embedded, which is the only form
 * ffmpeg accepts.
 *
 * Kept in one function so there is exactly one place that can leak a password,
 * and so scrubUrl() below has a single shape to undo.
 */
QString composeUrl(const Config &config);

/*
 * The same URL with any password replaced by "***".
 *
 * Every log line and every Phase 8 manifest entry must go through this. A
 * password in a support log is a credential disclosure that outlives the dive
 * -- logs get emailed, attached to tickets, and kept.
 */
QString scrubUrl(const QString &url);

/* The ffmpeg_options string for a latency preset. Separate so a test can read
 * it without building a source. */
QString ffmpegOptionsFor(const Config &config);

/* Settings as they will be handed to the source. Exposed for the same reason
 * as the capture factory's: it is the part most likely to be silently wrong. */
OBSData settingsFor(const Config &config);

/* Creates the Element in `scene`. Returns a null item on failure. */
OBSSceneItem addTo(obs_scene_t *scene, const Config &config, const QString &name);

/* The source type behind an RTSP Element. */
const char *sourceId();

/*
 * Asks for a URL and credentials, then creates the Element on `scene`.
 *
 * A modest dialog rather than a page in the Add Element picker, because that
 * picker is task 2.5 and this needs to be reachable before it exists.
 */
bool promptAndAdd(QWidget *parent, obs_scene_t *scene);

} // namespace MCRtspElement
