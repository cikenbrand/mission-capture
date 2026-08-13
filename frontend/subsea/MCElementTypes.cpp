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

#include "MCElementTypes.hpp"
#include "MCFeatures.hpp"

#include <util/base.h>

#include <array>
#include <cstring>

namespace MCElementTypes {

namespace {

/*
 * The allowlist, by unversioned source id.
 *
 * Two of the three Element types do not exist yet -- the RTSP camera arrives
 * with Phase 2 task 2.4 and the overlay with Phase 4. Their ids are listed now
 * so that the filter does not have to be revisited when they land, and so this
 * table reads as the product's intended surface rather than as a snapshot of
 * what happens to be built.
 */
constexpr std::array<const char *, 4> allowlist{{
	/* Video Capture Device (task 2.1 unifies these two behind one picker). */
	"dshow_input",       /* AVerMedia and other UVC/DirectShow devices */
	"decklink-input",    /* Blackmagic DeckLink / UltraStudio */
			     /* Not yet built: */
	"mc_rtsp_source",    /* RTSP Camera -- phase 2 task 2.4 */
	"mc_overlay_source", /* Overlay -- phase 4 */
}};

} // namespace

bool allowed(const char *unversionedId)
{
	if (!unversionedId) {
		return false;
	}

	/* The escape hatch. Checked first so that turning it on genuinely restores
	 * upstream behaviour rather than a filtered approximation of it. */
	if (MCFeatures::enabled(MCFeatures::Feature::AllSourceTypes)) {
		return true;
	}

	for (const char *candidate : allowlist) {
		if (strcmp(candidate, unversionedId) == 0) {
			return true;
		}
	}

	return false;
}

size_t allowedCount()
{
	return allowlist.size();
}

} // namespace MCElementTypes
