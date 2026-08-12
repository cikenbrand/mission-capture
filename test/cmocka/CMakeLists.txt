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

function(add_obs_cmocka_test target)
  add_executable(${target} ${target}.c)
  target_link_libraries(${target} PRIVATE OBS::libobs cmocka::cmocka)
  set_target_properties(${target} PROPERTIES FOLDER tests)
  add_test(NAME ${target} COMMAND ${target})
endfunction()

add_obs_cmocka_test(test_serializer)
add_obs_cmocka_test(test_darray)
add_obs_cmocka_test(test_bitstream)
add_obs_cmocka_test(test_os_path)
