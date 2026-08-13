# Mission Capture: fork-specific frontend sources.
#
# Everything under frontend/subsea/ is ours. Keeping it in one fragment means
# frontend/CMakeLists.txt needs exactly one added line, which keeps the merge
# surface small. See docs/subsea/architecture.md section 8.
target_sources(
  obs-studio
  PRIVATE
    subsea/MCBranding.hpp
    subsea/MCCaptureDevices.cpp
    subsea/MCCaptureDevices.hpp
    subsea/MCDefaults.cpp
    subsea/MCDefaults.hpp
    subsea/MCElementTypes.cpp
    subsea/MCElementTypes.hpp
    subsea/MCFeatures.cpp
    subsea/MCFeatures.hpp
    subsea/MCJobMetadata.cpp
    subsea/MCJobMetadata.hpp
    subsea/MCJobWizard.cpp
    subsea/MCJobWizard.hpp
    subsea/MCLayersDelegate.cpp
    subsea/MCLayersDelegate.hpp
    subsea/MCLayersModel.cpp
    subsea/MCLayersModel.hpp
    subsea/MCLayersTree.cpp
    subsea/MCLayersTree.hpp
    subsea/MCUIManifest.cpp
    subsea/MCUIManifest.hpp
)
