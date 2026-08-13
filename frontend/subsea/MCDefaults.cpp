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

#include "MCDefaults.hpp"

#include <widgets/OBSBasic.hpp>

#include <obs-frontend-api.h>
#include <obs.hpp>
#include <util/base.h>

#include <algorithm>
#include <cstring>

/* Declared at each use site rather than in a header -- upstream's own
 * convention, matched here so a future merge does not have to reconcile two
 * ways of reaching the same function (see OBSBasic.cpp, SimpleOutput.cpp). */
extern bool EncoderAvailable(const char *encoder);

namespace MCDefaults {

namespace {

/* Characters Windows forbids in a filename, plus the separators. A Job named
 * "Pipeline 12/A" must not silently become a subdirectory. */
constexpr const char *kIllegal = "<>:\"/\\|?*";

std::string sanitise(const char *raw, const char *fallback)
{
	if (!raw || !*raw) {
		return fallback;
	}

	std::string out{raw};
	for (char &c : out) {
		if (strchr(kIllegal, c) != nullptr || static_cast<unsigned char>(c) < 0x20) {
			c = '-';
		}
	}

	/* Trailing dots and spaces are legal to write but not to open on Windows. */
	while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
		out.pop_back();
	}

	return out.empty() ? fallback : out;
}

void replaceAll(std::string &haystack, const std::string &needle, const std::string &replacement)
{
	for (size_t pos = haystack.find(needle); pos != std::string::npos;
	     pos = haystack.find(needle, pos + replacement.size())) {
		haystack.replace(pos, needle.size(), replacement);
	}
}

} // namespace

void apply(config_t *config)
{
	if (!config) {
		return;
	}

	/*
	 * Container: MKV in both output modes.
	 *
	 * This is the one default that is not a preference. MKV keeps a playable
	 * file after a power loss or a hard kill; MP4 writes its index at the end,
	 * so the same event leaves an unopenable file. On a dive that is the whole
	 * recording. Upstream's default is hybrid_mov, which has the same failure.
	 */
	config_set_default_string(config, "SimpleOutput", "RecFormat2", "mkv");
	config_set_default_string(config, "AdvOut", "RecFormat2", "mkv");

	/*
	 * ...and never quietly convert it afterwards. Auto-remux runs after the
	 * recording stops, can fail with nobody watching, and would undo the
	 * property above. Remux stays available as a deliberate action in the File
	 * menu.
	 */
	config_set_default_bool(config, "Video", "AutoRemux", false);

	/*
	 * Quality-targeted rather than bitrate-targeted. "HQ" is CRF 16 and drives
	 * CQP on both NVENC and AMF (SimpleOutput::UpdateRecordingSettings). A
	 * fixed bitrate spends the same bits on a static pipe as on silt in
	 * suspension; inspection footage is judged on the detail in the busy parts.
	 */
	config_set_default_string(config, "SimpleOutput", "RecQuality", "HQ");

	/*
	 * Filename template. %JOB% and %CANVAS% are ours -- see expandTokens().
	 * Ordered coarse to fine so a directory listing sorts by Job, then Canvas,
	 * then time, which is how footage gets reviewed.
	 */
	config_set_default_string(config, "Output", "FilenameFormatting", "%JOB%_%CANVAS%_%CCYY%%MM%%DD%_%hh%%mm%%ss%");

	/*
	 * Split recordings by default. A dive produces hours of footage, and one
	 * enormous file is awkward to copy to a client's drive and catastrophic to
	 * lose.
	 *
	 * NOTE: splitting only exists in Advanced output mode, and Phase 1 leaves
	 * the mode at Simple. These values are correct for when Phase 6 task 6.8
	 * switches modes; until then they are set but inert. Recorded in
	 * PROGRESS.md rather than left as a surprise.
	 */
	config_set_default_bool(config, "AdvOut", "RecSplitFile", true);
	config_set_default_string(config, "AdvOut", "RecSplitFileType", "Time");
	config_set_default_uint(config, "AdvOut", "RecSplitFileTime", 15);
}

void applyEncoders(config_t *config)
{
	if (!config) {
		return;
	}

	/*
	 * Pick hardware if the machine has it. The operator should never have to
	 * know which vendor is in the box -- and getting this wrong means x264
	 * eating the CPU that the capture cards need.
	 *
	 * NVENC first, then AMF, matching the two GPU vendors in the field
	 * (docs/subsea/hardware-baseline.md). H.264 in both cases: HEVC and AV1
	 * encode smaller but are still awkward in the review and NLE software
	 * clients use.
	 */
	const char *encoder = SIMPLE_ENCODER_X264;
	const char *why = "no hardware encoder detected";

	if (EncoderAvailable("ffmpeg_nvenc")) {
		encoder = SIMPLE_ENCODER_NVENC;
		why = "NVENC available";
	} else if (EncoderAvailable("h264_texture_amf")) {
		encoder = SIMPLE_ENCODER_AMD;
		why = "AMF available";
	}

	config_set_default_string(config, "SimpleOutput", "RecEncoder", encoder);

	blog(LOG_INFO, "[MCDefaults] Default recording encoder: %s (%s)", encoder, why);
}

std::string expandTokens(const char *format)
{
	if (!format) {
		return {};
	}

	std::string out{format};

	/* Cheap out: nothing to resolve, so do not touch libobs at all. This runs
	 * on every screenshot and every recording start. */
	if (out.find('%') == std::string::npos) {
		return out;
	}

	if (out.find("%JOB%") != std::string::npos) {
		BPtr<char> collection = obs_frontend_get_current_scene_collection();
		replaceAll(out, "%JOB%", sanitise(collection, "Job"));
	}

	if (out.find("%CANVAS%") != std::string::npos) {
		/*
		 * The program Canvas. That is the only thing being recorded today;
		 * Phase 6 records several at once and will need to say which one,
		 * so this resolves per recording rather than being baked into the
		 * template at save time.
		 */
		OBSSourceAutoRelease scene = obs_frontend_get_current_scene();
		replaceAll(out, "%CANVAS%", sanitise(obs_source_get_name(scene), "Canvas"));
	}

	return out;
}

} // namespace MCDefaults
