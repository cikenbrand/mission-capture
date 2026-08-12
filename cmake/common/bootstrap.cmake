# OBS CMake bootstrap module

include_guard(GLOBAL)

# Map fallback configurations for optimized build configurations
# gersemi: off
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO
    RelWithDebInfo
    Release
    MinSizeRel
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL
    MinSizeRel
    Release
    RelWithDebInfo
    None
    ""
)
set(
  CMAKE_MAP_IMPORTED_CONFIG_RELEASE
    Release
    RelWithDebInfo
    MinSizeRel
    None
    ""
)
# gersemi: on

# Prohibit in-source builds
if("${CMAKE_CURRENT_BINARY_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
  message(
    FATAL_ERROR
    "In-source builds of OBS are not supported. "
    "Specify a build directory via 'cmake -S <SOURCE DIRECTORY> -B <BUILD_DIRECTORY>' instead."
  )
  file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/CMakeCache.txt" "${CMAKE_CURRENT_SOURCE_DIR}/CMakeFiles")
endif()

# Set default global project variables
#
# Mission Capture fork: these five strings are the root of all product branding.
# They propagate into every module's Windows version resource (*.rc.in), the CPack
# configuration, and the installer. Changing them here is intentionally the only
# place product identity is defined.
#
# The OBS_* variable NAMES are kept as-is on purpose -- they are referenced from
# ~30 upstream .rc.in and cmake files, and renaming them would conflict on every
# merge for no benefit. Only the values are ours.
set(OBS_COMPANY_NAME "Cyberian Resources")
set(OBS_PRODUCT_NAME "Mission Capture")
set(OBS_WEBSITE "https://github.com/cikenbrand/mission-capture")
set(OBS_COMMENTS "Subsea inspection video and data recorder")
set(OBS_LEGAL_COPYRIGHT "(C) Cyberian Resources. Based on OBS Studio, (C) Lain Bailey.")
set(OBS_CMAKE_VERSION 3.0.0)

# Directory under %APPDATA% for user configuration. Consumed by MC_CONFIG_DIR in
# frontend/subsea/MCBranding.hpp -- keep the two in step.
set(MC_CONFIG_DIR "Cyberian Resources/Mission Capture")

# Configure default version strings
set(_obs_default_version "0" "0" "1")
set(_obs_release_candidate 0)
set(_obs_beta 0)

# Add common module directories to default search path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/common" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/finders")

include(policies NO_POLICY_SCOPE)
include(versionconfig)
include(buildnumber)
include(osconfig)

# Allow selection of common build types via UI
if(NOT CMAKE_GENERATOR MATCHES "(Xcode|Visual Studio .+)")
  if(NOT CMAKE_BUILD_TYPE)
    set(
      CMAKE_BUILD_TYPE
      "RelWithDebInfo"
      CACHE STRING
      "OBS build type [Release, RelWithDebInfo, Debug, MinSizeRel]"
      FORCE
    )
    set_property(
      CACHE CMAKE_BUILD_TYPE
      PROPERTY STRINGS Release RelWithDebInfo Debug MinSizeRel
    )
  endif()
endif()

# Enable default inclusion of targets' source and binary directory
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
