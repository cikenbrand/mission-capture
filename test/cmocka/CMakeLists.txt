project(obs-cmocka)

# Mission Capture: obs-deps ships cmocka with a modern CMake config package that
# exports the imported target cmocka::cmocka but does NOT set the old-style
# CMOCKA_INCLUDE_DIR / CMOCKA_LIBRARIES variables this file previously used. With
# those variables empty the tests configured but did not link. Use the target.
#
# The old add_test() calls also hardcoded ${CMAKE_CURRENT_BINARY_DIR}/<name>,
# which is wrong for multi-config generators like Visual Studio -- the binary
# lands in a per-configuration subdirectory. COMMAND <target> lets CMake resolve
# the real path.
find_package(cmocka CONFIG REQUIRED)

# Mission Capture: obs.dll imports FFmpeg and zlib, which live in the prebuilt
# dependency bundle and are only copied next to the binaries by a *packaged*
# build -- not by the plain build the tests run against. Derive their location
# from CMAKE_PREFIX_PATH rather than reconstructing the bundle name, which
# encodes a date that moves with buildspec.json.
set(_obs_cmocka_dep_bins "")
foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
  if(EXISTS "${_prefix}/bin")
    list(APPEND _obs_cmocka_dep_bins "PATH=path_list_prepend:${_prefix}/bin")
  endif()
endforeach()

function(add_obs_cmocka_test target)
  add_executable(${target} ${target}.c)
  target_link_libraries(${target} PRIVATE OBS::libobs cmocka::cmocka ${ARGN})
  set_target_properties(${target} PROPERTIES FOLDER tests)
  add_test(NAME ${target} COMMAND ${target})

  # The test executables link libobs, so they need obs.dll at run time, and it
  # is not next to them. Without this the outcome depends on what happens to be
  # on PATH: on a machine with OBS Studio installed the tests silently load the
  # *installed* obs.dll and pass, testing someone else's build; on a clean
  # machine such as a CI runner they fail with 0xc0000135, DLL not found.
  #
  # Prepending our own output directory makes the test deterministic and makes
  # it test the binary we just built. The dependency bin directories are needed
  # too, and neither set alone is enough: the run directory holds obs.dll and
  # w32-pthreads.dll, the dependency bundle holds everything obs.dll imports.
  #
  # Our run directory goes last in the list so it ends up first on PATH -- each
  # path_list_prepend inserts at the front, so the final entry wins.
  set(env_mod ${_obs_cmocka_dep_bins})
  list(APPEND env_mod "PATH=path_list_prepend:${OBS_OUTPUT_DIR}/$<CONFIG>/${OBS_EXECUTABLE_DESTINATION}")
  set_tests_properties(${target} PROPERTIES ENVIRONMENT_MODIFICATION "${env_mod}")
endfunction()

add_obs_cmocka_test(test_serializer)
add_obs_cmocka_test(test_darray)
add_obs_cmocka_test(test_bitstream)
add_obs_cmocka_test(test_os_path)

# Mission Capture: the channel registry. Headless by design -- no window, no
# device, no render loop -- so it runs in the same harness as the libobs tests.
add_obs_cmocka_test(test_mc_channel OBS::mc-data)
add_obs_cmocka_test(test_mc_frame OBS::mc-data)
