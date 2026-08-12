# Mission Capture: fork-specific frontend sources.
#
# Everything under frontend/subsea/ is ours. Keeping it in one fragment means
# frontend/CMakeLists.txt needs exactly one added line, which keeps the merge
# surface small. See docs/subsea/architecture.md section 8.
target_sources(
  obs-studio
  PRIVATE
    subsea/MCBranding.hpp
    subsea/MCFeatures.cpp
    subsea/MCFeatures.hpp
    subsea/MCLayersModel.cpp
    subsea/MCLayersModel.hpp
    subsea/MCUIManifest.cpp
    subsea/MCUIManifest.hpp
)
