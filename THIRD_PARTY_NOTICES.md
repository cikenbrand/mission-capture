# Third-party notices

**Mission Capture** — © Cyberian Resources.

Mission Capture is a derivative work of **OBS Studio**, © Lain Bailey and the OBS Studio
contributors listed in [`AUTHORS`](AUTHORS). It is distributed under the **GNU General Public
License, version 2 or later**, the same licence as OBS Studio. The full licence text is in
[`COPYING`](COPYING).

---

## Source availability

GPLv2 §3 obliges us to make the complete corresponding source code available to anyone we
distribute a binary to. That source is published at:

**<https://github.com/cikenbrand/mission-capture>**

The same URL is shown in the application's About dialog. If you received a binary of Mission
Capture without access to that repository, you are entitled to the source; contact Cyberian
Resources.

---

## Relationship to the OBS Project

Mission Capture is an **independent fork**. It is:

- **not** produced, endorsed, sponsored, or supported by the OBS Project;
- **not** entitled to use the "OBS", "OBS Studio", or "OBS Project" names or logos as its own
  branding.

"OBS" and the OBS logo are trademarks of the OBS Project. They are referred to here only to
identify the upstream work accurately, which is both a licence requirement and simple honesty.

**Do not report Mission Capture bugs to the OBS Project.** Report them to Cyberian Resources.

---

## Bundled third-party components

Mission Capture inherits OBS Studio's dependencies. Each carries its own licence, reproduced in
`frontend/data/license/` in the installed application. Principal components:

| Component | Licence | Notes |
|---|---|---|
| Qt 6 | LGPL v3 | Dynamically linked, as LGPL requires |
| FFmpeg | LGPL v2.1+ / GPL v2+ | Configuration-dependent |
| x264 | GPL v2+ | |
| libcurl | curl licence (MIT-like) | |
| mbedTLS | Apache 2.0 | |
| libdatachannel | MPL 2.0 | WHIP / WebRTC output |
| json11, nlohmann/json | MIT | |
| CEF | BSD 3-clause | Disabled in Mission Capture builds |
| Blackmagic DeckLink SDK | Blackmagic Design licence | **Redistribution terms differ from the rest — review before shipping an installer** |
| NVIDIA NVENC / AMD AMF headers | Vendor SDK licences | Headers only; runtime supplied by the GPU driver |

This table is a summary for orientation, not a substitute for the licence files. **Re-verify it
before the first external release**, particularly the DeckLink SDK terms and whichever FFmpeg
configuration ships.

---

## Attribution retained

The following are deliberately left intact and must stay that way:

- `COPYING` — the GPLv2 text
- `AUTHORS` — the upstream contributor list
- Per-file copyright headers throughout the tree
- `frontend/data/license/` — bundled dependency licences

Removing any of these would breach the licence we ship under.
