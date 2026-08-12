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

#include <string>
#include <string_view>

/*
 * Mission Capture product identity.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * Upstream hard-codes the string "obs-studio" as the user-configuration
 * directory in 47 places across 16 frontend source files. Replacing every one
 * of those literals would mean touching 16 upstream files and inheriting a
 * merge conflict in each of them, forever -- exactly what the fork strategy in
 * docs/subsea/README.md forbids.
 *
 * Instead we rewrite the leading path component at the two chokepoints every
 * config path already flows through: GetAppConfigPath() and
 * GetAppConfigPathPtr() in frontend/OBSApp.cpp. Call sites keep passing
 * "obs-studio/basic", "obs-studio/logs" and so on, and land under
 * MC_CONFIG_DIR instead.
 *
 * THE TRADE-OFF, STATED PLAINLY
 * -----------------------------
 * This is indirection: a reader who sees GetAppConfigPath(..., "obs-studio/logs")
 * will not guess from the call site that the result is under
 * "Cyberian Resources/Mission Capture/logs". That surprise is the price of not
 * having 47 merge conflicts. The mitigation is that both chokepoints carry a
 * comment pointing here, and t0-foundation.ps1 asserts that no directory named
 * "obs-studio" is ever created.
 *
 * A handful of sites build paths with std::filesystem instead of going through
 * those functions; those are edited directly and each is marked with a comment
 * referencing this header.
 */

/* Directory beneath %APPDATA% holding all user configuration.
 * Keep in step with MC_CONFIG_DIR in cmake/common/bootstrap.cmake. */
#define MC_CONFIG_DIR "Cyberian Resources/Mission Capture"

/* The token upstream call sites still pass. */
#define MC_UPSTREAM_CONFIG_DIR "obs-studio"

/* Product identity shown in the UI. Keep in step with OBS_PRODUCT_NAME and
 * OBS_COMPANY_NAME in cmake/common/bootstrap.cmake. */
#define MC_PRODUCT_NAME "Mission Capture"
#define MC_COMPANY_NAME "Cyberian Resources"

/* GPLv2 obliges us to make the complete corresponding source available to
 * anyone we distribute a binary to. This URL is that offer, and it is surfaced
 * in the About dialog -- it is a licence obligation, not a nicety, so do not
 * remove it without providing the source another way. */
#define MC_SOURCE_URL "https://github.com/cikenbrand/mission-capture"

/* Attribution to the upstream project we are built on. */
#define MC_UPSTREAM_URL "https://obsproject.com"

namespace MCBranding {

/*
 * Rewrites a leading "obs-studio" path component to MC_CONFIG_DIR.
 *
 * Matches only a whole leading component, so "obs-studio/logs" and
 * "obs-studio\\updates\\x.json" are rewritten while a hypothetical
 * "obs-studio-extra" is left alone. Anything not starting with the token is
 * returned unchanged.
 */
inline std::string rewriteConfigPath(std::string_view name)
{
	constexpr std::string_view token{MC_UPSTREAM_CONFIG_DIR};

	if (name.size() >= token.size() && name.compare(0, token.size(), token) == 0) {
		const bool wholeComponent = name.size() == token.size() || name[token.size()] == '/' ||
					    name[token.size()] == '\\';
		if (wholeComponent) {
			std::string rewritten{MC_CONFIG_DIR};
			rewritten.append(name.substr(token.size()));
			return rewritten;
		}
	}

	return std::string{name};
}

} // namespace MCBranding
