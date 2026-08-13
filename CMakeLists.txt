cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(obs-studio VERSION ${OBS_VERSION_CANONICAL})

if(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/architecture.cmake")
  if(NOT OBS_PARENT_ARCHITECTURE STREQUAL CMAKE_VS_PLATFORM_NAME)
    return()
  endif()
endif()

include(compilerconfig)
include(defaults)
include(helpers)

option(ENABLE_FRONTEND "Enable building with UI (requires Qt)" ON)
option(ENABLE_SCRIPTING "Enable scripting support" ON)
option(ENABLE_HEVC "Enable HEVC encoders" ON)

# Mission Capture: shared/obs-scripting is only ever added by the frontend-tools
# plugin, so with that plugin disabled nothing reads ENABLE_SCRIPTING -- scripting
# then disappears without ever appearing in the configuration summary, and CMake
# warns that the variable was unused. Record it explicitly so the summary is
# honest about what is not being built.
if(NOT ENABLE_SCRIPTING)
  set_property(GLOBAL APPEND PROPERTY OBS_FEATURES_DISABLED "Scripting Support")
endif()

add_subdirectory(libobs)

# Mission Capture: the headless data core. After libobs because it uses the
# portable platform helpers; before test/ and frontend/, which both link it.
add_subdirectory(shared/mc-data)
if(OS_WINDOWS)
  add_subdirectory(libobs-d3d11)
  add_subdirectory(libobs-winrt)
endif()
add_subdirectory(libobs-opengl)
if(OS_MACOS)
  add_subdirectory(libobs-metal)
endif()
add_subdirectory(plugins)

add_subdirectory(test/test-input)

# Mission Capture: upstream never adds test/, so its cmocka suite is unreachable
# and there is no CTest registration anywhere in the tree. Both are wired up here.
# BUILD_TESTS stays undefined, so test/CMakeLists.txt only reaches its cmocka
# branch and test-input is not added twice.
option(ENABLE_UNIT_TESTS "Build the cmocka unit test suite" OFF)
if(ENABLE_UNIT_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(test)
endif()

add_subdirectory(frontend)

message_configuration()
