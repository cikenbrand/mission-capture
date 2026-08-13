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

#include <cstddef>

/*
 * Which Element types a user may add.
 *
 * WHY THIS IS A FILTER AND NOT A DELETION
 * ---------------------------------------
 * OBS registers around a dozen input types. Mission Capture offers three:
 * Video Capture Device, RTSP Camera and Overlay. The other types stay
 * registered on purpose -- an existing Job that references one must still load,
 * and a Job produced by a future version must not be silently emptied by an
 * older one. Only the *offer* is restricted.
 *
 * That distinction is why this cannot be done by unregistering the plugins:
 * unregistering breaks loading, filtering does not.
 *
 * THE FLAG
 * --------
 * MCFeatures::AllSourceTypes brings the full OBS list back. Off by default. It
 * exists so a support engineer can add something unusual on a vessel without a
 * rebuild -- for example a media file to reproduce a fault -- and so the
 * restriction never becomes a dead end.
 */

namespace MCElementTypes {

/*
 * True if `unversionedId` may appear in the Add Element list.
 *
 * Takes the unversioned id ("text_gdiplus", not "text_gdiplus_v3"), which is
 * what obs_enum_input_types2 yields as its second output and what survives
 * upstream bumping a source's version suffix.
 *
 * Always true when MCFeatures::AllSourceTypes is on.
 */
bool allowed(const char *unversionedId);

/* Number of ids on the allowlist, for logging and tests. */
size_t allowedCount();

} // namespace MCElementTypes
