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

/*
 * Characters Windows forbids in a filename, plus the separators. A Job named
 * "Pipeline 12/A" must not silently become a subdirectory.
 *
 * '%' is in the list for a different reason: these values are substituted into
 * a string that libobs then parses for its own date tokens, so a Job number
 * containing a percent sign would be read as a malformed token rather than as
 * text.
 */
constexpr const char *kIllegal = "<>:\"/\\|?*%";

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
	 * Filename template. Ordered coarse to fine so a directory listing sorts by
	 * Job, then Canvas, then time, which is how footage gets reviewed.
	 *
	 * MIND THE DELIMITERS -- they are not the same on both halves.
	 *
	 * %JOB% and %CANVAS% are ours and are closed with a second %, because
	 * expandTokens() below matches them literally. libobs' own date tokens are
	 * NOT: os_generate_formatted_filename maps "%CCYY", "%MM", "%DD" with no
	 * trailing delimiter, and treats "%%" as an escaped percent
	 * (libobs/util/platform.c). Writing "%CCYY%%MM%%DD%" -- which is what task
	 * 1.7 shipped -- therefore produced "2026%MM%DD": the first token expanded,
	 * then each "%%" became a literal "%" and the rest stayed as text.
	 *
	 * It took recording an actual file to notice. Nothing that reads config can
	 * tell a valid template from an invalid one.
	 */
	config_set_default_string(config, "Output", "FilenameFormatting", "%JOB%_%CANVAS%_%CCYY%MM%DD_%hh%mm%ss");

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

void applyUserDefaults(config_t *userConfig)
{
	if (!userConfig) {
		return;
	}

	/*
	 * Confirm before stopping a recording. Upstream leaves this unset, which
	 * means off -- so a mis-click on the record button ends a dive with no
	 * question asked. On here, but only past the threshold below, so a short
	 * test recording still stops in one click.
	 */
	config_set_default_bool(userConfig, "BasicWindow", "WarnBeforeStoppingRecord", true);
	config_set_default_int(userConfig, "BasicWindow", "WarnBeforeStoppingRecordAfter", 60);

	/*
	 * Disk-space thresholds, in whole GB on the recording volume.
	 *
	 * 10 GB is roughly twenty minutes at inspection bitrates -- enough notice
	 * to swap a drive or clear space without ending the dive. 2 GB is "finish
	 * what you are doing".
	 *
	 * 1 GB is where we stop. Upstream stops at 50 MB, which at 25 GB/hour is a
	 * few seconds and leaves the muxer writing its trailer into whatever space
	 * the last frame did not take. Stopping a minute early costs a minute of
	 * footage; stopping too late costs the file.
	 */
	config_set_default_int(userConfig, "BasicWindow", "DiskCautionGB", 10);
	config_set_default_int(userConfig, "BasicWindow", "DiskCriticalGB", 2);
	config_set_default_int(userConfig, "BasicWindow", "DiskStopGB", 1);

	/*
	 * How long a camera may go quiet before it is called lost.
	 *
	 * Three seconds is long enough that a brief hiccup or a format renegotiation
	 * does not flash a banner over working video, and short enough that an
	 * operator learns about a dead camera while the ROV is still near whatever
	 * it was looking at.
	 */
	config_set_default_int(userConfig, "BasicWindow", "SignalLostAfterSeconds", 3);
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
